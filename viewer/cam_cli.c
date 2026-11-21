/* cam_cli - test harness for cam2d: list chains of a part, generate a toolpath, write G-code + an SVG preview.
 *
 * Usage: cam_cli PART.mcx-9 list
 *        cam_cli PART.mcx-9 contour|pocket|drill CHAIN[,CHAIN..] [--dia D] [--depth Z] [--step S] [--side out|in|on]
 *                [--top Z] [--stepover PCT] [--nc OUT.nc] [--svg OUT.svg]
 *                [--tool N (part tool number)] [--lib LIB.tooldb|.tools --libtool NAME-or-index]
 *                [--tipcomp] [--coolant]
 * ops: contour pocket drill face slot helix circle thread dynamic area dyncontour peel
 *      [--chamfer W --chamfer-tip T | --engrave] [--pitch P --major D --external --lh --passes N --single]
 *      [--outside --wall W --width W --minrad PCT] [--oneway --angle DEG --box X0 Y0 X1 Y1]
 * CHAIN = index printed by `list`.
 * ours (test tooling, cli-opts 2026-11-19) - each sets an operation record field with its decoded meaning:
 *      [--comp left|right] [--comp-type computer|control|wear|reverse|off] [--chain-dir cw|ccw]
 *      [--tab-auto | --tab-manual (also implied by --tabpts)] [--tab-num N | --tab-maxdist D] [--tab-width W]
 *      [--tab-height H] [--tab-full | --tab-partial]
 *      [--tab-ramp DEG | --tab-vertical]
 *        cam_cli PART.mcx-9 list [--ids]   (--ids: each chain's entity ids as an ids: spec)
 *        cam_cli PART.mcx-9 ents           (every entity: id, type, geometry, chain index)
 *
 * Subcommands (each is one cmd_* function below, its options described there; main picks it):
 *   without a part:  sobool A B, sooff A B, chktrim-test, nci-filter IN OUT ...
 *   on a part:       bpinfo, vsim, nci-filterpost, post, reverse, areaparams, nciw-replace, nciw, fileop, stockmodel,
 *                    tprefs, saveref, list, ents
 *   anything else:   an operation type name (the names[] table in cmd_operation: contour, pocket, drill, ...) on the
 *                    chains given as CHAIN[,CHAIN..] or ids:...
 */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "cam2d.h"
#include "toollib.h"
#include "offset.h"
#include "mcam_saveops.h"
#include "xt_mesh.h"
#include "cam_fileop.h"
#include "nc_post.h"
#include "backplot.h"
#include "verify.h"
#include "nci_reverse.h"
#include "area_mill.h"
#include "nci_write.h"
#include "stockmodel.h"
#include "mcce_ctour.h"

/* ------------------------------------------------------------------------------------------------ constants */

#define MAX_CHAINS 4096                 /* chains found in a part (`list`) */
#define MAX_OP_CHAINS 64                /* chains given to one operation (ids: specs) */
#define MAX_ENGAGEMENT_SAMPLES 4000000  /* --verify: engagement angles kept */

/* operation record fields the --comp / --comp-type / --tab-* options set (described where they are parsed) */
enum {
    REC_COMP_TYPE = 0x5d0,      /* S16: 0 Computer, 1 Control, 2 Wear, 3 Reverse wear, 4 Off */
    REC_COMP_DIR = 0x5d2,       /* u8: 0 left, 1 right */
    REC_TAB_ON = 0x1380,        /* u8: tabs on */
    REC_TAB_AUTO = 0x1381,      /* u8: 1 Automatic, 0 Manual */
    REC_TAB_NUM = 0x1382,       /* S16: the number of tabs */
    REC_TAB_WIDTH = 0x1388,     /* double */
    REC_TAB_HEIGHT = 0x1390,    /* double: thickness */
    REC_TAB_FULL = 0x1398,      /* u8: 1 full, 0 partial */
    REC_TAB_RAMP = 0x139c,      /* S16: 1 Ramp, 0 vertical */
    REC_TAB_RAMP_ANGLE = 0x13b0,/* double: angle (rad) */
    REC_TAB_BY = 0x1440,        /* u8: 0 by number, 1 by max distance */
    REC_TAB_MAXDIST = 0x1448,   /* double */
};
#define COMP_TYPE_OFF 4         /* REC_COMP_TYPE: Off */

/* --------------------------------------------------------------------------------------- chains of the part */

static Chain chains[MAX_CHAINS];
static int nch;

/* Chains every planar line / arc of F (each entity in one chain at most) into chains[] / nch. */
static void find_chains(const McamFile *f) {
    char *used = calloc(f->nents, 1);
    for (int i = 0; i < f->nents && nch < MAX_CHAINS; i++) {
        if (used[i] || !(f->ents[i].type == E_LINE || f->ents[i].type == E_ARC) || !f->ents[i].planar)
            continue;
        Chain c;
        if (!cam_chain(f, i, cam_chain_tol(mcam_is_metric(f)), &c))
            continue;
        for (int k = 0; k < c.n; k++)
            used[c.ent[k]] = 1;
        chains[nch++] = c;
    }
    free(used);
}

/* -------------------------------------------------------------------------------------------- small helpers */

/* Q moved past the current item of a comma-separated list (and its comma). */
static const char *next_list_item(const char *q) {
    while (*q && *q != ',')
        q++;
    if (*q)
        q++;
    return q;
}

/* The index in F.ops of the stored op with id ID (the last one if the id repeats); -1 when there is none. */
static int op_index_by_id(const McamFile *f, int id) {
    int idx = -1;
    for (int i = 0; i < f->nops; i++)
        if (f->ops[i].id == id)
            idx = i;
    return idx;
}

/* Appends a hole (centre X, Y, diameter D) to OP. */
static void add_hole(CamOp *op, double x, double y, double d) {
    op->holes = realloc(op->holes, (op->nholes + 1) * sizeof(Hole));
    op->holes[op->nholes].x = x;
    op->holes[op->nholes].y = y;
    op->holes[op->nholes].d = d;
    op->nholes++;
}

/* Grows the box LO..HI to hold the point V (x, y). */
static void grow_box(double lo[2], double hi[2], const double *v) {
    for (int j = 0; j < 2; j++) {
        if (v[j] < lo[j])
            lo[j] = v[j];
        if (v[j] > hi[j])
            hi[j] = v[j];
    }
}

/* Moves of an op not in the Top plane: from world coordinates into the op's view (tool plane). */
static void moves_to_view(CamOp *op) {
    if (!cam_op_top(&op->p))
        for (int i = 0; i < op->nm; i++) {
            double w[3] = { op->m[i].p.x, op->m[i].p.y, op->m[i].p.z }, v[3];
            cam_plane_w2v(op->p.tplane, op->p.torg, w, v);
            op->m[i].p.x = v[0];
            op->m[i].p.y = v[1];
            op->m[i].p.z = v[2];
        }
}

/* Writes the moves to PATH, one "x y z rapid" line each (--moves, for comparisons). */
static void write_moves(const char *path, const CamOp *op) {
    FILE *o = fopen(path, "w");
    for (int i = 0; o && i < op->nm; i++)
        fprintf(o, "%.6f %.6f %.6f %d\n", op->m[i].p.x, op->m[i].p.y, op->m[i].p.z, op->m[i].rapid);
    if (o)
        fclose(o);
}

/* --movesf: as write_moves plus the feed. */
static void write_moves_feed(const char *path, const CamOp *op) {
    FILE *o = fopen(path, "w");
    for (int k = 0; o && k < op->nm; k++)
        fprintf(o, "%.6f %.6f %.6f %d %.4f\n", op->m[k].p.x, op->m[k].p.y, op->m[k].p.z, op->m[k].rapid,
                op->m[k].feed);
    if (o)
        fclose(o);
}

/* --moves-arcs: as write_moves plus each move's arc tag (arc, cw, cx, cy). */
static void write_moves_arcs(const char *path, const CamOp *op) {
    FILE *o = fopen(path, "w");
    for (int k = 0; o && k < op->nm; k++)
        fprintf(o, "%.6f %.6f %.6f %d %d %d %.10f %.10f\n", op->m[k].p.x, op->m[k].p.y, op->m[k].p.z,
                op->m[k].rapid, op->m[k].arc, op->m[k].cw, op->m[k].cx, op->m[k].cy);
    if (o)
        fclose(o);
}

/* --chains-out: the op's chains as polylines: "chain N closed|open", then x y per point. */
static void write_chain_polylines(const McamFile *f, const char *path, const CamOp *op) {
    FILE *o = fopen(path, "w");
    for (int c = 0; o && c < op->nchains; c++) {
        double *xy, z;
        int n = cam_chain_polyline(f, &op->chains[c], 0.01, &xy, &z);
        fprintf(o, "chain %d %s\n", c, op->chains[c].closed ? "closed" : "open");
        for (int k = 0; k < n; k++)
            fprintf(o, "%.6f %.6f\n", xy[2 * k], xy[2 * k + 1]);
        free(xy);
    }
    if (o)
        fclose(o);
}

/* ------------------------------------------------------------------------------------------------- --verify */

/* --verify: replay the moves at final depth on a raster of the region (cell = D / 60). Reports the region left uncut,
 * cuts outside the region (gouges into walls / islands, beyond 0.02 mm) and the engagement angle (contact arc on the
 * tool's front half against material not yet cut) along cutting moves. */

/* the raster: cell (i, j) = index j * nx + i, its centre at x0 + (i + 0.5) * res, y0 + (j + 0.5) * res */
typedef struct {
    double x0, y0, res;
    int nx, ny;
    unsigned char *in;          /* 1 = inside the region */
    unsigned char *mat;         /* 1 = material left */
    unsigned char *cut;         /* 1 = cut */
} VRaster;

/* qsort comparator: doubles in ascending order. */
static int cmpd(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

/* The region of OP: its closed chains as polylines into REG, their bounding box into LO / HI. */
static void verify_region(const McamFile *f, const CamOp *op, CPaths *reg, double lo[2], double hi[2]) {
    for (int c = 0; c < op->nchains; c++) {
        double *xy, z;
        int n = cam_chain_polyline(f, &op->chains[c], 0.005, &xy, &z);
        if (!op->chains[c].closed || n < 3) {
            free(xy);
            continue;
        }
        reg->p[reg->n].xy = xy;
        reg->p[reg->n].n = n;
        reg->n++;
        for (int k = 0; k < n; k++)
            for (int j = 0; j < 2; j++) {
                lo[j] = fmin(lo[j], xy[2 * k + j]);
                hi[j] = fmax(hi[j], xy[2 * k + j]);
            }
    }
}

/* Marks the raster cells inside REG (R->in). */
static void verify_fill(const CPaths *reg, VRaster *r) {
    for (int j = 0; j < r->ny; j++) {                  /* scanline even-odd fill */
        double y = r->y0 + (j + 0.5) * r->res;
        double xs[4096];
        int nxs = 0;
        for (int c = 0; c < reg->n; c++) {
            const CPath *pp = &reg->p[c];
            for (int i = 0, k = pp->n - 1; i < pp->n; k = i++) {
                double ya = pp->xy[2 * k + 1], yb = pp->xy[2 * i + 1];
                if ((ya > y) != (yb > y) && nxs < 4096)
                    xs[nxs++] = pp->xy[2 * k] + (y - ya) * (pp->xy[2 * i] - pp->xy[2 * k]) / (yb - ya);
            }
        }
        qsort(xs, nxs, sizeof(double), cmpd);
        for (int q = 0; q + 1 < nxs; q += 2)
            for (int i = (int)((xs[q] - r->x0) / r->res - 0.5); i <= (int)((xs[q + 1] - r->x0) / r->res - 0.5); i++)
                if (i >= 0 && i < r->nx) {
                    double x = r->x0 + (i + 0.5) * r->res;
                    if (x >= xs[q] && x <= xs[q + 1])
                        r->in[(size_t)j * r->nx + i] = 1;
                }
    }
}

/* The tool (radius R) at X, Y moving along HX, HY: of the ring samples on its front half, how many are on material
   (returned) and how many there are (*TOT). */
static int verify_ring_hits(const VRaster *r, double x, double y, double R, double hx, double hy, int *tot) {
    int hit = 0;
    *tot = 0;
    for (int q = 0; q < 180; q++) {
        double an = 2 * M_PI * q / 180, ux = cos(an), uy = sin(an);
        if (ux * hx + uy * hy < 0)
            continue;
        (*tot)++;
        int ix = (int)((x + R * ux - r->x0) / r->res), iy = (int)((y + R * uy - r->y0) / r->res);
        if (ix >= 0 && iy >= 0 && ix < r->nx && iy < r->ny && r->mat[(size_t)iy * r->nx + ix])
            hit++;
    }
    return hit;
}

/* Sweeps the tool (radius R) along every cutting move of OP at or below ZCUT: clears R->mat, marks R->cut, and keeps
   the engagement angle of each step of a cutting move in ENGS. Returns the number of angles kept. */
static long verify_sweep(const CamOp *op, VRaster *r, double R, double zcut, double *engs) {
    int rr = (int)ceil(R / r->res) + 1;                /* tool radius in cells (+1) */
    long ne = 0;
    for (int k = 1; k < op->nm; k++) {
        const Move *a = &op->m[k - 1], *b = &op->m[k];
        if (b->rapid || a->p.z > zcut || b->p.z > zcut)
            continue;
        /* links (back feed) and entry helix / plunges (plunge feed) */
        int link = fabs(b->feed - op->p.feed) > 1e-6;
        double L = hypot(b->p.x - a->p.x, b->p.y - a->p.y);
        int steps = (int)(L / (r->res * 0.7)) + 1;
        double hx = L > 0 ? (b->p.x - a->p.x) / L : 1, hy = L > 0 ? (b->p.y - a->p.y) / L : 0;   /* move direction */
        for (int s = 1; s <= steps; s++) {
            double x = a->p.x + (b->p.x - a->p.x) * s / steps, y = a->p.y + (b->p.y - a->p.y) * s / steps;
            int cx = (int)((x - r->x0) / r->res), cy = (int)((y - r->y0) / r->res);
            if (!link && L > 0 && ne < MAX_ENGAGEMENT_SAMPLES) {   /* engagement: front-half ring samples on material */
                int tot;
                int hit = verify_ring_hits(r, x, y, R, hx, hy, &tot);
                if (hit)
                    engs[ne++] = 180.0 * hit / tot;
                if (getenv("VERIFY_HIGH") && hit && 180.0 * hit / tot > atof(getenv("VERIFY_HIGH")))
                    fprintf(stderr, "high %.0f at move %d (%.2f,%.2f) feed %.0f\n", 180.0 * hit / tot, k, x, y,
                            b->feed);
            }
            /* the tool's disc: material removed, cells cut */
            for (int j = cy - rr; j <= cy + rr; j++)
                for (int i = cx - rr; i <= cx + rr; i++) {
                    if (i < 0 || j < 0 || i >= r->nx || j >= r->ny)
                        continue;
                    double px = r->x0 + (i + 0.5) * r->res - x, py = r->y0 + (j + 0.5) * r->res - y;
                    if (px * px + py * py <= R * R) {
                        r->mat[(size_t)j * r->nx + i] = 0;
                        r->cut[(size_t)j * r->nx + i] = 1;
                    }
                }
        }
    }
    return ne;
}

/* 1 when a cell within G cells of (I, J) is inside the region. */
static int verify_near_region(const VRaster *r, int i, int j, int g) {
    int nearb = 0;
    for (int dj = -g; dj <= g && !nearb; dj++)
        for (int di = -g; di <= g && !nearb; di++) {
            int ii = i + di, jj = j + dj;
            if (ii >= 0 && jj >= 0 && ii < r->nx && jj < r->ny && r->in[(size_t)jj * r->nx + ii])
                nearb = 1;
        }
    return nearb;
}

/* Prints the --verify report from the swept raster and the NE engagement angles ENGS (sorted here); VERIFY_PGM=FILE
   writes the raster map. */
static void verify_report(const CamOp *op, const VRaster *r, double D, double R, int stay_inside, double *engs,
                          long ne) {
    double res = r->res;
    long nin = 0, left = 0, gouge = 0;
    int g = (int)ceil(0.02 / res);                       /* tolerance band at the boundary */
    for (int j = 0; j < r->ny; j++)
        for (int i = 0; i < r->nx; i++) {
            size_t c = (size_t)j * r->nx + i;
            if (r->in[c]) {
                nin++;
                if (r->mat[c])
                    left++;
            } else if (r->cut[c] && stay_inside) {
                /* outside the region: only a gouge if clear of the boundary band */
                if (!verify_near_region(r, i, j, g))
                    gouge++;
            }
        }
    printf("verify: region %.1f mm2, left uncut %.2f%% (%.1f mm2), gouge outside the region %.2f mm2\n",
           nin * res * res, 100.0 * left / (nin ? nin : 1), left * res * res, gouge * res * res);
    if (ne) {
        qsort(engs, ne, sizeof(double), cmpd);
        printf("verify: engagement deg  p50 %.0f  p90 %.0f  p99 %.0f  max %.0f"
               "  (stepover %.0f%% -> %.0f deg nominal)\n",
               engs[ne / 2], engs[ne * 9 / 10], engs[ne * 99 / 100], engs[ne - 1], op->p.stepover_pct,
               acos(1 - D * op->p.stepover_pct / 100 / R) * 180 / M_PI);
    }
    if (getenv("VERIFY_PGM")) {                        /* map: white = region cut, black = left, grey = outside */
        FILE *o = fopen(getenv("VERIFY_PGM"), "wb");
        fprintf(o, "P5 %d %d 255\n", r->nx, r->ny);
        for (int j = r->ny - 1; j >= 0; j--)
            for (int i = 0; i < r->nx; i++) {
                size_t c = (size_t)j * r->nx + i;
                fputc(!r->in[c] ? (r->cut[c] ? 160 : 90) : r->mat[c] ? 0 : 255, o);
            }
        fclose(o);
    }
}

/* Prints the --verify report for OP (see above). STAY_INSIDE: count cuts outside the region as gouges.
   Environment: VERIFY_HIGH=DEG prints each engagement above DEG to stderr; VERIFY_PGM=FILE writes the raster map. */
static void verify(const McamFile *f, const CamOp *op, int stay_inside) {
    double D = op->p.tool_dia;
    double R = D / 2;
    double res = D / 60;                               /* raster cell */
    double zcut = op->p.final_z + 1e-3;                /* moves above this are not at final depth */

    /* the region: the closed chains as polylines, and its bounding box */
    CPaths reg = { calloc(op->nchains + 1, sizeof(CPath)), 0 };
    double lo[2] = { 1e300, 1e300 }, hi[2] = { -1e300, -1e300 };
    verify_region(f, op, &reg, lo, hi);
    if (!reg.n) {
        printf("verify: needs closed chains\n");
        return;
    }

    /* the raster: the box grown by 2 D on every side */
    VRaster r;
    r.res = res;
    r.x0 = lo[0] - D * 2;
    r.y0 = lo[1] - D * 2;
    r.nx = (int)((hi[0] - lo[0] + 4 * D) / res) + 1;
    r.ny = (int)((hi[1] - lo[1] + 4 * D) / res) + 1;
    r.in = calloc((size_t)r.nx * r.ny, 1);
    r.mat = calloc((size_t)r.nx * r.ny, 1);
    r.cut = calloc((size_t)r.nx * r.ny, 1);
    verify_fill(&reg, &r);
    memcpy(r.mat, r.in, (size_t)r.nx * r.ny);

    double *engs = malloc(sizeof(double) * MAX_ENGAGEMENT_SAMPLES);
    long ne = verify_sweep(op, &r, R, zcut, engs);
    verify_report(op, &r, D, R, stay_inside, engs, ne);
    free(engs);
    free(r.in);
    free(r.mat);
    free(r.cut);
    cl_free(&reg);
}

/* ---------------------------------------------------------------------------------------- parameter options */

static int idn_max;     /* the part's highest entity id (set in main) */

/* --set name=value,...: any CamParams field (for replaying a Mastercam operation's settings) */
#define PF(n, t) { #n, offsetof(CamParams, n), t }
static const struct { const char *name; size_t off; int dbl; } PFIELDS[] = {
    PF(tool_num, 0), PF(tool_dia, 1), PF(rpm, 0), PF(feed, 1), PF(plunge_feed, 1), PF(top_z, 1), PF(final_z, 1),
    PF(stepdown, 1), PF(clearance, 1), PF(retract_z, 1), PF(feed_plane, 1), PF(side, 0), PF(roll, 0), PF(side_rel, 0),
    PF(lead_mid, 0), PF(lead_sw, 1), PF(stock_xy, 1), PF(stepover_pct, 1), PF(ramp_deg, 1), PF(pocket_style, 0),
    PF(pocket_angle, 1), PF(helix_r, 1), PF(helix_zclr, 1), PF(r_plane, 1), PF(peck, 1), PF(drill_cycle, 0),
    PF(tol, 1), PF(lead_r, 1), PF(lead_len, 1), PF(finish_n, 0), PF(finish_step, 1), PF(tabs, 0), PF(tab_w, 1),
    PF(tab_h, 1), PF(arcs, 0),
    PF(cleanup, 0), PF(tool_type, 0), PF(corner_r, 1), PF(tip_angle, 1), PF(coolant, 0), PF(tip_comp, 0),
    PF(contour_type, 0),
    PF(chamfer_w, 1), PF(chamfer_tip, 1), PF(tip_dia, 1), PF(flute_len, 1), PF(helix_pitch, 1), PF(thread_major, 1),
    PF(thread_pitch, 1),
    PF(thread_depth, 1), PF(thread_internal, 0), PF(thread_rh, 0), PF(thread_passes, 0), PF(thread_full, 0),
    PF(thread_step, 1), PF(thread_up, 0), PF(face_style, 0), PF(face_along, 1),
    PF(face_angle, 1), PF(face_overlap, 1), PF(dyn_outside, 0), PF(min_rad_pct, 1), PF(micro_lift, 1), PF(back_feed, 1),
    PF(link_retract, 1), PF(entry_r, 1), PF(wall_stock, 1), PF(slot_width, 1),
    PF(lead_on, 0), PF(lead_in_on, 0), PF(lead_out_on, 0), PF(lead_in_perp, 0), PF(lead_out_perp, 0), PF(lead_xlen, 1),
    PF(lead_xr, 1),
    PF(ct_rtype, 0), PF(rm_src, 0), PF(rm_dia, 1), PF(rm_clr, 1), PF(rm_clr_pct, 1), PF(rm_cell, 1), PF(rm_tol_pct, 1),
    PF(rm_notrim, 0), PF(ct_comp, 0),
    PF(lead_xsw, 1), PF(lead_ovl, 1), PF(lead_gouge, 0), PF(dc_taper, 0), PF(dc_taper_a, 1), PF(dc_bydepth, 0),
    PF(mc_lead_dz[0], 1), PF(mc_lead_dz[1], 1), PF(mc_lead_dz[2], 1), PF(mc_lead_dz[3], 1), PF(adj_s, 0),
    PF(adj_s_ext, 0), PF(adj_s_len, 1),
    PF(adj_e, 0), PF(adj_e_ext, 0), PF(adj_e_len, 1), PF(mc_depth_nf, 0), PF(mc_depth_fs, 1), PF(mc_depth_on, 0),
};

/* --lead / --set lead_r / lead_len / lead_sw: this program's one lead for both sides, as before the two sides
   (2026-10-23), unless an exit value is given as well */
static int cli_lead_in, cli_lead_out;

/* Copies the entry lead onto the exit (see above) when only the entry was given. */
static void cli_lead_mirror(CamParams *p) {
    if (!cli_lead_in || cli_lead_out)
        return;
    p->lead_on = p->lead_r > 0;
    p->lead_in_on = p->lead_out_on = 1;
    p->lead_pct = 0;
    p->lead_xr = p->lead_r;
    p->lead_xlen = p->lead_len;
    p->lead_xsw = p->lead_sw;
}

/* --rec OFF=b:V / s:V / d:V (ours, test tooling, batch 6): a record patch as the op dialog writes it
   (cam_params_from_patches). A appends one patch (hex offset; b u8, s S16, i S32, anything else a double). */
static void cli_rec_patch(CamParams *p, const char *a) {
    unsigned off = 0;
    char t = 0;
    double v = 0;
    if (sscanf(a, "%x=%c:%lf", &off, &t, &v) != 3 || p->nrpatch >= CAM_NRPATCH) {
        fprintf(stderr, "bad --rec %s\n", a);
        return;
    }
    int k = p->nrpatch++;
    p->rpatch[k].off = (uint16_t)off;
    p->rpatch[k].height = 0;
    if (t == 'b') {
        p->rpatch[k].n = 1;
        p->rpatch[k].v[0] = (uint8_t)v;
    } else if (t == 's') {
        int16_t x = (int16_t)v;
        p->rpatch[k].n = 2;
        memcpy(p->rpatch[k].v, &x, 2);
    } else if (t == 'i') {
        int32_t x = (int32_t)v;
        p->rpatch[k].n = 4;
        memcpy(p->rpatch[k].v, &x, 4);
    } else {
        p->rpatch[k].n = 8;
        memcpy(p->rpatch[k].v, &v, 8);
    }
}

/* ours (test tooling, cli-opts 2026-11-19): one record field set as the op dialog would (a record patch, replacing an
   earlier patch of the same offset): cam_params_from_patches maps it onto the engines' fields (cam_fileop.c, e.g.
   cam_tab_mirror for the Tabs page), and the save writes it over the saved record. t: 'b' u8, 's' S16, 'd' double */
static void cli_rec_set(CamParams *p, unsigned off, char t, double v) {
    int k = 0;
    while (k < p->nrpatch && p->rpatch[k].off != off)
        k++;
    if (k == p->nrpatch) {                             /* a new patch */
        if (p->nrpatch >= CAM_NRPATCH) {
            cam_rpatch_full((int)off);
            return;
        }
        p->nrpatch++;
    }
    p->rpatch[k].off = (uint16_t)off;
    p->rpatch[k].height = 0;
    if (t == 'b') {
        p->rpatch[k].n = 1;
        p->rpatch[k].v[0] = (uint8_t)v;
    } else if (t == 's') {
        int16_t x = (int16_t)v;
        p->rpatch[k].n = 2;
        memcpy(p->rpatch[k].v, &x, 2);
    } else {
        p->rpatch[k].n = 8;
        memcpy(p->rpatch[k].v, &v, 8);
    }
}

/* --rp OFF:HEX record patches, as the operations sidecar's "RP OFF 0 HEX" lines (an op dialog edit: decimal or
   0x offset, little-endian bytes in hex, e.g. --rp 0x12a2:0100 = S16 1). A appends one patch and applies the
   patches to P. */
static void cli_rp_patch(CamParams *p, const char *a) {
    unsigned off;
    char hx[40];
    int k = p->nrpatch;
    if (k < CAM_NRPATCH && sscanf(a, "%i:%39s", (int *)&off, hx) == 2 && strlen(hx) % 2 == 0 && strlen(hx) <= 16) {
        p->rpatch[k].off = (uint16_t)off;
        p->rpatch[k].height = 0;
        p->rpatch[k].n = (uint8_t)(strlen(hx) / 2);
        for (int b = 0; b < p->rpatch[k].n; b++) {
            unsigned v;
            sscanf(hx + 2 * b, "%2x", &v);
            p->rpatch[k].v[b] = (uint8_t)v;
        }
        p->nrpatch++;
        cam_params_from_patches(p);
    } else {
        fprintf(stderr, "bad --rp %s\n", a);
    }
}

/* --set: applies SPEC (name=value,...) to P - a PFIELDS field, else a sidecar key; notes the lead options given. */
static void set_params(CamParams *p, const char *spec) {
    char buf[2048];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *sv = NULL, *kv = strtok_r(buf, ",", &sv); kv; kv = strtok_r(NULL, ",", &sv)) {
        char *eq = strchr(kv, '=');
        if (!eq)
            continue;
        *eq++ = 0;
        int hit = 0;
        if (!strcmp(kv, "lead_r") || !strcmp(kv, "lead_len") || !strcmp(kv, "lead_sw"))
            cli_lead_in = 1;
        if (!strncmp(kv, "lead_x", 6) || !strcmp(kv, "lead_out_on"))
            cli_lead_out = 1;
        for (size_t k = 0; k < sizeof PFIELDS / sizeof PFIELDS[0]; k++)
            if (!strcmp(PFIELDS[k].name, kv)) {
                if (PFIELDS[k].dbl)
                    *(double *)((char *)p + PFIELDS[k].off) = atof(eq);
                else
                    *(int *)((char *)p + PFIELDS[k].off) = atoi(eq);
                hit = 1;
            }
        if (!hit)
            hit = cam_set_param(p, kv, atof(eq));   /* the sidecar keys (cam2d.c PKEYS) */
        if (!hit)
            fprintf(stderr, "unknown parameter %s\n", kv);
    }
}

/* ---------------------------------------------------------------------------------------------- ids: chains */

/* Appends entity index ENT (reversed: REV) to the chain OUT, growing its arrays (capacity *CAP). */
static void chain_append(Chain *out, int *cap, int ent, int rev) {
    if (out->n == *cap) {
        *cap *= 2;
        out->ent = realloc(out->ent, sizeof(int) * *cap);
        out->rev = realloc(out->rev, sizeof(int) * *cap);
    }
    out->ent[out->n] = ent;
    out->rev[out->n] = rev;
    out->n++;
}

/* a chain from entity ids in order; each entity runs from the previous one's end (rev set accordingly).
   An id with a suffix gives the direction instead: 12f forward, 12r reversed (Mastercam's chain record flag).
   Returns 0 when no entity was found. */
static int chain_from_ids(McamFile *f, const char *ids, Chain *out) {
    memset(out, 0, sizeof *out);
    int cap = 16, explicit_dir = 0;
    out->ent = malloc(sizeof(int) * cap);
    out->rev = malloc(sizeof(int) * cap);
    char buf[4096];
    snprintf(buf, sizeof buf, "%s", ids);
    for (char *sv = NULL, *t = strtok_r(buf, ",", &sv); t; t = strtok_r(NULL, ",", &sv)) {
        int id = atoi(t), k = -1;
        char sfx = t[strlen(t) - 1];
        for (int i = 0; i < f->nents; i++)
            if (f->ents[i].type && f->ents[i].idn == id) {
                k = i;
                break;
            }
        if (k < 0) {                                   /* a solid: its loop, in loop order (explicit directions) */
            int n, first = cam_solid_chain_ents(f, id, &n);
            for (int j = 0; j < n; j++)
                chain_append(out, &cap, first + j, sfx == 'r');
            if (n > 0)
                explicit_dir = 1;
            continue;
        }
        if (sfx == 'f' || sfx == 'r')
            explicit_dir = 1;
        chain_append(out, &cap, k, sfx == 'r');
    }
    if (!out->n)
        return 0;
    double tol = cam_chain_tol(mcam_is_metric(f));
/* END / BEG: the end / start of chain member i traversed reversed (r) or not; D2: XY distance */
#define END(i, r) ((r) ? f->ents[out->ent[i]].a : f->ents[out->ent[i]].b)
#define BEG(i, r) ((r) ? f->ents[out->ent[i]].b : f->ents[out->ent[i]].a)
#define D2(p, q) hypot((p).x - (q).x, (p).y - (q).y)
    if (out->n > 1 && !explicit_dir) {                 /* direction of the first: towards the second */
        V3 a = f->ents[out->ent[0]].a, b = f->ents[out->ent[0]].b;
        V3 s2 = f->ents[out->ent[1]].a, e2 = f->ents[out->ent[1]].b;
        out->rev[0] = fmin(D2(a, s2), D2(a, e2)) < fmin(D2(b, s2), D2(b, e2)) - tol;
    }
    for (int i = 1; i < out->n && !explicit_dir; i++) {   /* each next one: from the previous one's end */
        V3 pe = END(i - 1, out->rev[i - 1]);
        out->rev[i] = D2(f->ents[out->ent[i]].b, pe) < D2(f->ents[out->ent[i]].a, pe);
    }
    out->closed = D2(END(out->n - 1, out->rev[out->n - 1]), BEG(0, out->rev[0])) < tol;
    if (out->n == 1 && f->ents[out->ent[0]].type == E_ARC && fabs(fabs(f->ents[out->ent[0]].sw) - 2 * M_PI) < 1e-6)
        out->closed = 1;                               /* a full circle */
#undef END
#undef BEG
#undef D2
    return 1;
}

/* ------------------------------------------------------------------------------ subcommands without a part */

/* batch 10 (testing, the oracle): nci-filter IN OUT tol rmin rmax look xy xz yz oneway rtype rval metric - the
   Line/Arc filter (cam_filter.c) over nodes "type x y z cx cy cz plane feed cc". Returns the exit code. */
static int cmd_nci_filter(int argc, char **argv) {
    if (argc < 15) {
        fprintf(stderr, "nci-filter IN OUT tol rmin rmax look xy xz yz oneway rtype rval metric\n");
        return 1;
    }
    FILE *in = fopen(argv[2], "r"), *out = fopen(argv[3], "w");
    if (!in || !out) {
        fprintf(stderr, "cannot open\n");
        return 1;
    }
    cam_filter_nodes(in, out, atof(argv[4]), atof(argv[5]), atof(argv[6]), atoi(argv[7]), atoi(argv[8]),
                     atoi(argv[9]), atoi(argv[10]), atoi(argv[11]), atoi(argv[12]), atof(argv[13]), atoi(argv[14]));
    fclose(in);
    fclose(out);
    return 0;
}

/* ----------------------------------------------------------------------------------- subcommands on a part */

/* bpinfo: backplot motion list + Info tab values (formats/backplot.md). Returns the exit code. */
static int cmd_bpinfo(McamFile *f) {
    NcMotion *mo;
    int nm = nc_motions(f, NULL, 0, NULL, &mo);
    Backplot bp;
    bp_init(&bp, mo, nm);
    BpInfo in;
    bp_info(&bp, &in);
    int k[6] = { 0 };                                  /* motions by kind */
    for (int i = 0; i < nm; i++)
        if (mo[i].kind >= 0 && mo[i].kind < 6)
            k[mo[i].kind]++;
    printf("motions %d (rapid %d line %d cw %d ccw %d drill %d tool %d), path %.3f\n", nm, k[0], k[1], k[2], k[3],
           k[4], k[5], bp.total);
    printf("feed length %.3f rapid length %.3f feed time %.2f s;"
           " X %.3f..%.3f Y %.3f..%.3f Z %.3f..%.3f; feed %.1f..%.1f\n",
           in.feed_len, in.rapid_len, in.feed_time_s, in.lo[0], in.hi[0], in.lo[1], in.hi[1], in.lo[2], in.hi[2],
           in.fmin, in.fmax);
    bp.cs.at_ops = 1;
    bp_compute_stops(&bp);
    printf("stops at operations: %d\n", bp.nstops);
    bp.cs.at_ops = 0;
    bp.cs.at_toolchange = 1;
    bp_compute_stops(&bp);
    printf("stops at tool changes: %d\n", bp.nstops);
    bp_free(&bp);
    return 0;
}

/* vsim: the ops' highest top of stock into *ZTOP; returns 0 when no op could be read. */
static int vsim_ops_top(McamFile *f, double *ztop) {
    int have_top = 0;               /* the ops' highest top of stock (op record +0x280 via cam_op_from_file) */
    for (int i = 0; i < f->nops; i++) {
        CamOp t;
        char e[64];
        if (cam_op_from_file(f, i, &t, e, sizeof e)) {
            if (!have_top || t.p.top_z > *ztop)
                *ztop = t.p.top_z;
            have_top = 1;
            for (int ci = 0; ci < t.nchains; ci++)
                cam_chain_free(&t.chains[ci]);   /* stack CamOp: free the arrays only */
            free(t.chains);
            free(t.holes);
            free(t.m);
        }
    }
    return have_top;
}

/* vsim, VSIM_MESH set: prints the stock mesh's vertex count and box. */
static void vsim_mesh_report(VF *v) {
    float *mv = NULL;
    int vn = vf_mesh(v, NULL, NULL, &mv);
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    int bad = 0;
    for (int i = 0; i < vn; i++) {
        for (int k = 0; k < 3; k++) {
            float q = mv[i * 9 + k];
            if (!isfinite(q)) {
                bad = 1;
                continue;
            }
            if (q < lo[k])
                lo[k] = q;
            if (q > hi[k])
                hi[k] = q;
        }
    }
    printf("mesh: %d verts, x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f%s\n", vn, lo[0], hi[0], lo[1], hi[1], lo[2],
           hi[2], bad ? " NON-FINITE!" : "");
    free(mv);
}

/* vsim [--cell X] [--pgm F]: the stock-removal simulation (verify.c, OURS - not Mastercam's Verify), all stored ops
   in order. Returns the exit code. */
static int cmd_vsim(McamFile *f, int argc, char **argv) {
    double cell = 0;
    const char *pgm = NULL;
    for (int i = 3; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--cell"))
            cell = atof(argv[++i]);
        else if (!strcmp(argv[i], "--pgm"))
            pgm = argv[++i];
    }
    NcMotion *mo;
    int nm = nc_motions(f, NULL, 0, NULL, &mo);
    if (nm <= 0) {
        printf("vsim: no motions\n");
        free(mo);
        return 1;
    }
    double maxr = 0, mind = 1e300;                     /* the largest tool radius, the smallest diameter */
    for (int i = 0; i < nm; i++) {
        const Tool *tl = mcam_tool(f, mo[i].tool);
        if (tl && tl->dia > 0) {
            if (tl->dia / 2 > maxr)
                maxr = tl->dia / 2;
            if (tl->dia < mind)
                mind = tl->dia;
        }
    }
    double ztop = 0;
    int have_top = vsim_ops_top(f, &ztop);
    double box[6];
    if (!vf_stock_box(f, mo, nm, maxr, have_top ? ztop : NAN, box)) {
        printf("vsim: no stock box\n");
        free(mo);
        return 1;
    }
    double diag = hypot(box[3] - box[0], box[4] - box[1]);
    if (cell <= 0)
        cell = diag / 400;
    if (mind < 1e200 && mind / 20 > cell)
        cell = mind / 20;   /* ours */
    VF *v = vf_new(box[0], box[1], box[3], box[4], box[5], box[2], cell);
    printf("vsim: stock %.3f..%.3f x %.3f..%.3f, z %.3f..%.3f; %d x %d cells of %.4f\n",
           box[0], box[3], box[1], box[4], box[2], box[5], v->nx, v->ny, cell);
    VfTool vt[256];
    int have[256] = { 0 };         /* by tool number */
    int curop = -1;
    double vol0 = 0;
    for (int i = 0; i < nm; i++) {
        if (mo[i].op_index != curop) {                 /* an op ends: the volume it removed */
            if (curop >= 0) {
                double vol1 = vf_volume_removed(v);
                printf("  op %d (%s): removed %.2f\n", curop, f->ops[mo[i - 1].op_index].type, vol1 - vol0);
                vol0 = vol1;
            }
            curop = mo[i].op_index;
        }
        const Tool *tl = mcam_tool(f, mo[i].tool);
        if (!tl || tl->num < 0 || tl->num >= 256)
            continue;
        if (!have[tl->num])
            have[tl->num] = vf_tool(tl, &vt[tl->num]);
        if (have[tl->num])
            vf_cut(v, &vt[tl->num], &mo[i]);
    }
    printf("vsim: removed %.2f total, %d rapid-collision warning(s)\n", vf_volume_removed(v), v->nwarn);
    for (int i = 0; i < v->nwarn && i < 8; i++)
        printf("  rapid cut at %.3f %.3f %.3f\n", v->warn[i][0], v->warn[i][1], v->warn[i][2]);
    if (pgm) {                                         /* the stock's heights as a grey map */
        FILE *o = fopen(pgm, "wb");
        fprintf(o, "P5 %d %d 255\n", v->nx, v->ny);
        for (int j = v->ny - 1; j >= 0; j--)
            for (int i = 0; i < v->nx; i++) {
                double t = (v->z[(size_t)j * v->nx + i] - v->zbot) / (v->ztop - v->zbot);
                fputc((int)(t * 255), o);
            }
        fclose(o);
    }
    if (getenv("VSIM_MESH"))
        vsim_mesh_report(v);
    vf_free(v);
    free(mo);
    return 0;
}

/* batch 10 (testing, ours): nci-filterpost IN OUT.nc tol rmin rmax look xy xz yz oneway rtype rval - the filtered
   nodes as a contour op in place of the part's first op, posted. Returns the exit code. */
static int cmd_nci_filterpost(McamFile *f, char **argv) {
    FILE *in = fopen(argv[3], "r");
    if (!in)
        return 1;
    CamOp op;
    memset(&op, 0, sizeof op);
    cam_defaults(&op.p, CAM_CONTOUR, f);
    op.op_index = 0;   /* in place of the first stored op */
    op.filtered = 1;
    snprintf(op.name, sizeof op.name, "FILTER TEST");
    cam_filter_nodes_moves(in, atof(argv[5]), atof(argv[6]), atof(argv[7]), atoi(argv[8]), atoi(argv[9]),
                           atoi(argv[10]), atoi(argv[11]), atoi(argv[12]), atoi(argv[13]), atof(argv[14]),
                           mcam_is_metric(f), &op.m, &op.nm);
    fclose(in);
    FILE *o = fopen(argv[4], "w");
    CamOp *ops1[1] = { &op };
    NcPostOpts po = { "r180", NULL, NULL };
    char err[200];
    if (!o)
        return 1;
    int ok = nc_post_part(o, f, ops1, 1, &po, err, sizeof err);
    fclose(o);
    printf("%d moves\n", op.nm);
    if (!ok) {
        fprintf(stderr, "post: %s\n", err);
        return 1;
    }
    return 0;
}

/* post [OUT.nc] [--style r180|ij-quad]: the LinuxCNC post, all ops. Returns the exit code. */
static int cmd_post(McamFile *f, int argc, char **argv) {
    NcPostOpts po = { "r180", NULL, NULL };
    const char *outp = NULL;
    static char only[4096];
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--style") && i + 1 < argc) {
            po.style = argv[++i];
        } else if (!strcmp(argv[i], "--ops") && i + 1 < argc) {
            /* batch 8 (ours, testing): --ops N[,N..] = the 1-based tree positions */
            memset(only, 0, sizeof only);
            for (char *q = argv[++i]; *q; ) {
                int n = atoi(q);
                if (n >= 1 && n <= (int)sizeof only)
                    only[n - 1] = 1;
                q = strchr(q, ',');
                if (!q)
                    break;
                q++;
            }
            po.only = only;
        } else {
            outp = argv[i];
        }
    }
    FILE *o = outp ? fopen(outp, "w") : stdout;
    char err[200];
    if (!o) {
        fprintf(stderr, "cannot write %s\n", outp);
        return 1;
    }
    int ok = nc_post_part(o, f, NULL, 0, &po, err, sizeof err);
    if (outp)
        fclose(o);
    if (!ok) {
        fprintf(stderr, "post: %s\n", err);
        return 1;
    }
    return 0;
}

/* reverse N[,N..] [--post OUT.nc] [--save OUT.mcx-9]: Reverse toolpath on tree operations N (1-based, repeats
   allowed), then post / save. Returns the exit code. */
static int cmd_reverse(McamFile *f, int argc, char **argv) {
    const char *outp = NULL, *savep = NULL;
    int rc = 0;
    for (int i = 4; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--post"))
            outp = argv[++i];
        else if (!strcmp(argv[i], "--save"))
            savep = argv[++i];
    }
    for (const char *q = argv[3]; *q; ) {
        int k = atoi(q) - 1;
        char msg[300];
        int r = nci_reverse_op(f, k, msg, sizeof msg);
        printf("op %d (id %d): %s%s%s\n", k + 1, k >= 0 && k < f->nops ? f->ops[k].id : -1,
               r > 0 ? "reversed" : r < 0 ? "skipped (multiaxis)" : "not reversed", msg[0] ? " - " : "", msg);
        if (!r)
            rc = 1;
        q = next_list_item(q);
    }
    if (outp) {
        FILE *o = fopen(outp, "w");
        char err[200];
        NcPostOpts po = { "r180", NULL, NULL };
        if (!o || !nc_post_part(o, f, NULL, 0, &po, err, sizeof err)) {
            fprintf(stderr, "post: %s\n", o ? err : "cannot write");
            rc = 1;
        }
        if (o)
            fclose(o);
    }
    if (savep) {
        char msg[700];
        int ok = mcam_save_with_ops(argv[1], savep, f, NULL, 0, msg, sizeof msg);
        printf("%s\n", msg);
        if (!ok)
            rc = 1;
    }
    return rc;
}

/* areaparams <op id>: the NCG macro mckernel builds for an Area Mill op (viewer/area_mill.c, formats/area_mill.md);
   units from the NCI. Returns the exit code. */
static int cmd_areaparams(McamFile *f, char **argv) {
    int id = atoi(argv[3]);
    AreaMillMacro m;
    for (int i = 0; i < f->nops; i++)
        if (f->ops[i].id == id) {
            if (!area_mill_macro(f->ops[i].raw, f->ops[i].raw_n, mcam_is_metric(f),
                                 mc_systol_units(mcam_is_metric(f))->tol, 0, 0.0, &m)) {
                printf("not an Area Mill op\n");
                return 1;
            }
            printf("mode %d: %s (0x%x), %s (0x%x)%s; tol %g, scale %g%s\n", m.mode, m.passes_plan, m.passes_id,
                   m.linking_plan, m.linking_id,
                   m.edit_to_boundaries ? " + Edit Passes to Boundaries (0x1416)" : "", m.tol, m.scale,
                   m.expansion_not_decoded ? " (CoreRoughingExpansionX/Y NOT DECODED for mode 0)" : "");
            for (int k = 0; k < m.n; k++)
                printf("  %s = %.9g\n", m.p[k].name, m.p[k].value);
            return 0;
        }
    printf("no such op\n");
    return 1;
}

/* nciw-replace ID[,ID..] OUT.mcx-9: those stored ops regenerated here, their NCI sections replaced by nci_write.c's
   (the op's own record), saved as OUT (the NC oracle: mp_cli posts OUT and the part, tools/tests/nciw_nc_cmp.sh).
   Returns the exit code. */
static int cmd_nciw_replace(McamFile *f, char **argv) {
    NciwPart *P = nciw_part_load(argv[1]);
    int rc = 0;
    for (const char *q = argv[3]; *q; ) {
        int id = atoi(q);
        int idx = op_index_by_id(f, id);
        CamOp op;
        char err[300] = "";
        if (idx < 0 || !cam_op_from_file(f, idx, &op, err, sizeof err) || !cam_generate(f, &op)) {
            printf("op %d: FAILED %s\n", id, err[0] ? err : op.error);
            rc = 1;
        } else {
            moves_to_view(&op);
            NciwOp no = { f->ops[idx].raw, f->ops[idx].raw_n, NULL, 0, f, mcam_is_metric(f), 0 };
            NciwBuf b = { 0 };
            NciwStats st;
            memset(&st, 0, sizeof st);
            if (!nciw_section(P, &no, op.m, op.nm, &b, &st, err, sizeof err)) {
                printf("op %d: REFUSED %s\n", id, err);
                rc = 1;
                free(b.b);
            } else {
                mcam_op_set_nci(f, idx, b.b, b.n);
                printf("op %d: %ld records written\n", id, st.records);
            }
        }
        q = next_list_item(q);
    }
    nciw_part_free(P);
    char msg[700];
    int ok = mcam_save_with_ops(argv[1], argv[4], f, NULL, 0, msg, sizeof msg);
    printf("%s\n", msg);
    return ok ? rc : 1;
}

/* nciw <op id> OUT [--sof X,Y,Z]: the NCI writer (nci_write.c) on a stored op's own record - the section from the op
   regenerated here (fileop), or with --sof the -2 + section-start records alone at that first position
   (tools/tests/nciw_oracle.py). Returns the exit code. */
static int cmd_nciw(McamFile *f, int argc, char **argv) {
    int id = atoi(argv[3]);
    double first[3];
    int sofonly = 0;
    for (int i = 5; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--sof") && sscanf(argv[i + 1], "%lf,%lf,%lf", &first[0], &first[1], &first[2]) == 3)
            sofonly = 1;
    int idx = op_index_by_id(f, id);
    if (idx < 0 || !f->ops[idx].raw) {
        printf("FAILED: no such op\n");
        return 1;
    }
    NciwPart *P = nciw_part_load(argv[1]);
    NciwOp no = { f->ops[idx].raw, f->ops[idx].raw_n, NULL, 0, f, mcam_is_metric(f), 0 };
    NciwBuf b = { 0 };
    NciwStats st;
    memset(&st, 0, sizeof st);
    char err[300] = "";
    int ok = 1;
    if (sofonly) {
        nciw_sof(P, &no, first, &b, &st);
    } else {
        CamOp op;
        if (!cam_op_from_file(f, idx, &op, err, sizeof err)) {
            printf("FAILED: %s\n", err);
            return 1;
        }
        if (!cam_generate(f, &op)) {
            printf("FAILED: %s\n", op.error);
            return 1;
        }
        moves_to_view(&op);
        ok = nciw_section(P, &no, op.m, op.nm, &b, &st, err, sizeof err);
    }
    if (ok) {
        FILE *o = fopen(argv[4], "wb");
        if (o) {
            fwrite(b.b, 1, b.n, o);
            fclose(o);
        }
    }
    printf("%s: %ld records, %ld stand-in values%s%s%s%s\n", ok ? "ok" : "REFUSED", st.records, st.not_decoded,
           st.note[0] ? " (" : "", st.note, st.note[0] ? ")" : "", ok ? "" : err);
    free(b.b);
    nciw_part_free(P);
    return ok ? 0 : 2;
}

/* fileop: the edits of the command line (--set, --rp, --rec, then the lead mirror) applied to the stored op's
   parameters P. */
static void fileop_edits(CamParams *p, int argc, char **argv) {
    for (int i = 4; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--set"))
            set_params(p, argv[++i]);   /* edits */
    for (int i = 4; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--rp"))
            cli_rp_patch(p, argv[++i]);
    {   /* --rec patches (see cli_rec_patch), applied together */
        int np0 = p->nrpatch;
        for (int i = 4; i + 1 < argc; i++)
            if (!strcmp(argv[i], "--rec"))
                cli_rec_patch(p, argv[++i]);
        if (p->nrpatch > np0)
            cam_params_from_patches(p);
    }
    cli_lead_mirror(p);
}

/* fileop <op id> [--moves F] [--nc F]: regenerate a stored op. Returns the exit code. */
static int cmd_fileop(McamFile *f, int argc, char **argv) {
    int id = atoi(argv[3]);
    const char *mo = NULL, *ncf = NULL;
    const char *mc = NULL;
    for (int i = 4; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--moves"))
            mo = argv[++i];
        else if (!strcmp(argv[i], "--nc"))
            ncf = argv[++i];
        else if (!strcmp(argv[i], "--mcam"))
            mc = argv[++i];
    }
    int idx = op_index_by_id(f, id);
    CamOp op;
    char err[200];
    if (idx < 0 || !cam_op_from_file(f, idx, &op, err, sizeof err)) {
        printf("FAILED: %s\n", idx < 0 ? "no such op" : err);
        return 1;
    }
    fileop_edits(&op.p, argc, argv);
    if (!cam_generate(f, &op)) {
        printf("FAILED: %s\n", op.error);
        return 1;
    }
    if (op.warning[0])
        printf("WARNING: %s\n", op.warning);   /* decisions batch dec1: a generated op's stand-in warning */
    printf("%s (%s): %d moves, %d chains, %d holes, tool T%d D%.3f, top %.3f -> %.3f\n", cam_op_label(&op),
           cam_type_label(op.p.type), op.nm, op.nchains, op.nholes, op.p.tool_num, op.p.tool_dia, op.p.top_z,
           op.p.final_z);
    for (int i = 4; i < argc; i++)
        if (!strcmp(argv[i], "--verify"))
            verify(f, &op, 1);   /* as the chain path's --verify */
    /* --params: ours (tests, tools/tests/regen_consistency.py): the op's parameters as the sidecar writes them */
    for (int i = 4; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--params")) {
            CamOp *ops1[1] = { &op };
            cam_save_ops(argv[++i], ops1, 1);
        }
    for (int i = 4; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--movesf"))              /* ours (tests): --moves + the feed */
            write_moves_feed(argv[++i], &op);
    if (mo)
        write_moves(mo, &op);
    /* --moves-arcs F (ours, tools/tests/engrave_test.sh APP=1): as --moves plus the arc tag of each move (Move.arc,
       cw, cx, cy: a run of moves with one tag = one arc from the move before the run, the Line/Arc filter's or the
       engine's) */
    for (int i = 4; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--moves-arcs"))
            write_moves_arcs(argv[++i], &op);
    if (ncf) {
        FILE *o = fopen(ncf, "w");
        CamOp *ops1[1] = { &op };
        if (o) {
            cam_write_gcode(o, f, ops1, 1, "FILEOP");
            fclose(o);
        }
    }
    /* --chains-out: ours (testing, tools/tests/hst_check.py), as the chain path's */
    for (int i = 4; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--chains-out"))
            write_chain_polylines(f, argv[++i], &op);
    /* --post: batch 10 (testing, ours): the LinuxCNC post (nc_post.c) of the part with this op regenerated in place
       of the stored one */
    for (int i = 4; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--post")) {
            FILE *o = fopen(argv[i + 1], "w");
            CamOp *ops1[1] = { &op };
            NcPostOpts po = { "r180", NULL, NULL };
            char err[200];
            op.op_index = idx;
            if (!o)
                return 1;
            int ok = nc_post_part(o, f, ops1, 1, &po, err, sizeof err);
            fclose(o);
            if (!ok) {
                fprintf(stderr, "post: %s\n", err);
                return 1;
            }
        }
    if (mc) {
        char msg[700];
        CamOp *ops1[1] = { &op };
        int ok = mcam_save_with_ops(argv[1], mc, f, ops1, 1, msg, sizeof msg);
        printf("%s\n", msg);
        if (!ok)
            return 1;
    }
    return 0;
}

/* stockmodel: the stored Stock Model ops (opcode 139; stockmodel.c, ours: a test harness). Returns the exit code. */
static int cmd_stockmodel(McamFile *f) {
    int n = 0;
    for (int i = 0; i < f->nops; i++) {
        StockModelOp sm;
        if (stockmodel_read(f, &f->ops[i], &sm)) {
            stockmodel_print(stdout, &sm);
            n++;
        }
    }
    printf("machine group stock: %.6f %.6f %.6f origin %.6f %.6f %.6f sel %d\n", f->stock[0], f->stock[1], f->stock[2],
           f->stock_org[0], f->stock_org[1], f->stock_org[2], f->stock_sel);
    printf("%d stock model op(s) of %d\n", n, f->nops);
    return 0;
}

/* tprefs (ours, testing): the parsed TP references of every op - kind 4 as CamRefs (cam_refs_from_tp), kinds 6 / 7 /
   1 by their fields (formats/surface_selection.md 2.1). Returns the exit code. */
static int cmd_tprefs(McamFile *f) {
    for (int i = 0; i < f->nops; i++) {
        const Op *o = &f->ops[i];
        static CamRef rr[CAM_REF_MAX];
        char e[200] = "";
        int n = cam_refs_from_tp(f, o->id, rr, CAM_REF_MAX, e, sizeof e);
        if (n < 0) {
            printf("op %d %s\n", o->id, e);
            continue;
        }
        for (int k = 0; k < n; k++)
            printf("op %d code %d k4 cls %x fl %x link %d ent %d idx %d\n", o->id, o->opcode, rr[k].cls,
                   rr[k].flags, rr[k].link, rr[k].idn, rr[k].index);
        for (int k = 0; k < f->ntp; k++) {
            const TpRef *t = &f->tp[k];
            if (t->op_id != o->id)
                continue;
            if (t->kind == 6)
                printf("op %d code %d k6 role %d chain %d\n", o->id, o->opcode, t->role, t->chain);
            else if (t->kind == 7)
                printf("op %d code %d k7 chain %d index %d rev %d\n", o->id, o->opcode, t->chain, t->index, t->rev);
        }
    }
    return 0;
}

/* saveref ID[,ID..]|all OUT [--gen] [--tpdump DIR] (ours, testing; formats/surface_selection.md 6 "S6"): the stored
   3D ops with TP kind 4 records taken over as edited ops of their port type (parameters from their record, chains
   from their kind 6 / 7 records) and saved as OUT (mcam_save_part; "-" = only the records, mcam_op_records_tp).
   --gen: the references as a CamRef list (the writer's new records) instead of the records kept as stored;
   --newsolid: as --gen with every solid reference made a whole body picked here (link 0: a new SldToolPOp).
   --tpdump DIR: each op's TP / OP records as the save made them, DIR/op<ID>.tp / .op */

/* saveref: the port type of stored op O (0: none) - opcode 132 by its style (*ST_OUT, -1 for other opcodes), the
   others by opcode. */
static int saveref_port_type(const Op *o, int *st_out) {
    int st = o->opcode == 132 && o->raw_n > 0x16bc ? o->raw[0x16bc] : -1, type = 0;
    for (int t = 1; t < CAM_NTYPES && !type; t++) {
        if (t == CAM_OPTIROUGH ? (o->opcode == 132 && st == 1)
                               : cam_hst_style(t) >= 0 ? (o->opcode == 132 && st == cam_hst_style(t)) : 0)
            type = t;
    }
    static const int oc3[][2] = {
        { 5, CAM_SRPARALLEL }, { 7, CAM_SRPROJECT }, { 10, CAM_SRPOCKET }, { 11, CAM_SFPARALLEL },
        { 12, CAM_SFRADIAL }, { 13, CAM_SFPROJECT }, { 15, CAM_SFCONTOUR }, { 39, CAM_SFPENCIL },
        { 44, CAM_SRPLUNGE }, { 109, CAM_BLEND }, { 131, CAM_SRMPOCKET }
    };
    for (size_t k = 0; k < sizeof oc3 / sizeof oc3[0] && !type; k++)
        if (o->opcode == oc3[k][0])
            type = oc3[k][1];
    *st_out = st;
    return type;
}

/* saveref: OP's chains rebuilt from stored op O's TP records; returns the number of chains that could not be. */
static int saveref_chains(McamFile *f, const Op *o, CamOp *op) {
    /* chains: the kind 6 headers and their kind 7 members (roles 2 / 3) */
    int32_t *ids = malloc(sizeof(int32_t) * (size_t)(f->ntp + 1));
    int *rv = malloc(sizeof(int) * (size_t)(f->ntp + 1));
    int nid = 0, role = 0, lost = 0;
    for (int k = 0; k <= f->ntp; k++) {
        const TpRef *t = k < f->ntp ? &f->tp[k] : NULL;   /* NULL: past the last record */
        if (t && t->op_id != o->id)
            continue;
        if (!t || t->kind == 6) {                      /* a header (or the end): the chain before it is complete */
            if (nid) {
                Chain c;
                if (cam_chain_from_idns(f, ids, rv, nid, &c)) {
                    c.role = role;
                    op->chains = realloc(op->chains, sizeof(Chain) * (size_t)(op->nchains + 1));
                    op->chains[op->nchains++] = c;
                } else {
                    lost++;
                }
            }
            nid = 0;
            if (!t)
                break;
            role = t->role;
            continue;
        }
        if (t->kind == 7) {
            ids[nid] = t->ent;
            rv[nid++] = t->rev;
        }
    }
    free(ids);
    free(rv);
    return lost;
}

/* saveref: stored op F.ops[I] taken over as an edited op of its port type; NULL (with a message) when it cannot be.
   OUT: the save's file name ("-": the records only); GEN: --gen (1) / --newsolid (2). */
static CamOp *saveref_take_op(McamFile *f, int i, const char *out, int gen) {
    const Op *o = &f->ops[i];
    int st;
    int type = saveref_port_type(o, &st);
    if (!type) {
        printf("op %d code %d style %d: not a port type\n", o->id, o->opcode, st);
        return NULL;
    }
    CamOp *op = calloc(1, sizeof *op);
    cam_defaults_base(&op->p, type, f);
    int ti = cam_op_tool_index(f, i);
    if (ti >= 0)
        cam_params_from_tool(&op->p, &f->tools[ti]);
    cam_params_from_raw(f, o->raw, o->raw_n, type, op);
    op->p.type = type;
    op->p.file_op_id = o->id;
    {
        char why[200] = "";
        /* "-": the records only (2018 test copies: their plane ids are not read here) */
        if (!cam_planes_from_record(o->raw, o->raw_n, &op->p, why, sizeof why) && strcmp(out, "-")) {
            printf("op %d code %d: planes: %s\n", o->id, o->opcode, why);
            free(op);
            return NULL;
        }
    }
    snprintf(op->name, sizeof op->name, "%s", o->comment);   /* op +6, none = none (L-5) */
    int lost = saveref_chains(f, o, op);
    if (lost) {
        printf("op %d code %d: %d chain(s) not rebuilt here\n", o->id, o->opcode, lost);
        free(op->chains);
        free(op);
        return NULL;
    }
    if (gen) {
        char e[200] = "";
        op->nref = cam_refs_from_tp(f, o->id, op->ref, CAM_REF_MAX, e, sizeof e);
        if (op->nref < 0) {
            printf("op %d: %s\n", o->id, e);
            op->nref = 0;
        }
    }
    for (int q = 0; gen == 2 && q < op->nref; q++)
        if (op->ref[q].cls == TP_ECLS_SOLID)
            op->ref[q].link = 0;   /* --newsolid: as a whole body picked here (a new SldToolPOp) */
    printf("op %d code %d style %d: taken as %s, %d chain(s)%s\n", o->id, o->opcode, st, cam_type_label(type),
           op->nchains, gen ? ", references as a list" : "");
    return op;
}

/* saveref: see above. Returns the exit code. */
static int cmd_saveref(McamFile *f, int argc, char **argv) {
    int gen = 0;
    const char *dump = NULL;
    for (int i = 5; i < argc; i++) {
        if (!strcmp(argv[i], "--gen"))
            gen = 1;
        else if (!strcmp(argv[i], "--newsolid"))
            gen = 2;
        else if (!strcmp(argv[i], "--tpdump") && i + 1 < argc)
            dump = argv[++i];
    }
    CamOp **ops = calloc((size_t)f->nops + 1, sizeof *ops);
    int n = 0, rc = 0;
    for (int i = 0; i < f->nops; i++) {
        const Op *o = &f->ops[i];
        int want = !strcmp(argv[3], "all");            /* the op is in the ID list */
        for (const char *q = argv[3]; !want && *q; ) {
            if (atoi(q) == o->id)
                want = 1;
            q = next_list_item(q);
        }
        int nk4 = 0;                                   /* its kind 4 records */
        for (int k = 0; k < f->ntp; k++)
            if (f->tp[k].op_id == o->id && f->tp[k].kind == 4)
                nk4++;
        if (!want || !nk4 || !o->raw)
            continue;
        CamOp *op = saveref_take_op(f, i, argv[4], gen);
        if (op)
            ops[n++] = op;
    }
    /* the records (for "-" or --tpdump), then the save */
    char msg[2000] = "";
    int ok;
    uint8_t **rec = calloc((size_t)n + 1, sizeof *rec), **tl = calloc((size_t)n + 1, sizeof *tl),
            **tp = calloc((size_t)n + 1, sizeof *tp);
    uint32_t *recn = calloc((size_t)n + 1, sizeof *recn), *tln = calloc((size_t)n + 1, sizeof *tln),
             *tpn = calloc((size_t)n + 1, sizeof *tpn);
    if (!strcmp(argv[4], "-") || dump)
        ok = mcam_op_records_tp(argv[1], f, ops, n, rec, recn, tl, tln, tp, tpn, msg, sizeof msg);
    else
        ok = 1;
    for (int k = 0; dump && k < n; k++) {
        char path[1200];
        FILE *g;
        snprintf(path, sizeof path, "%s/op%d.tp", dump, ops[k]->p.file_op_id);
        if (tp[k] && (g = fopen(path, "wb"))) {
            fwrite(tp[k], 1, tpn[k], g);
            fclose(g);
        }
        snprintf(path, sizeof path, "%s/op%d.op", dump, ops[k]->p.file_op_id);
        if (rec[k] && (g = fopen(path, "wb"))) {
            fwrite(rec[k], 1, recn[k], g);
            fclose(g);
        }
    }
    for (int k = 0; k < n; k++) {
        free(rec[k]);
        free(tl[k]);
        free(tp[k]);
    }
    free(rec);
    free(tl);
    free(tp);
    free(recn);
    free(tln);
    free(tpn);
    if (msg[0])
        printf("records: %s\n", msg);
    if (strcmp(argv[4], "-")) {
        msg[0] = 0;
        ok = mcam_save_with_ops(argv[1], argv[4], f, ops, n, msg, sizeof msg);
        printf("%s\n", msg);
    }
    for (int k = 0; k < n; k++) {
        for (int c = 0; c < ops[k]->nchains; c++)
            cam_chain_free(&ops[k]->chains[c]);
        free(ops[k]->chains);
        free(ops[k]);
    }
    free(ops);
    if (!ok)
        rc = 1;
    return rc;
}

/* list [--ids]: the part's chains (index, entities, z, box, area). Returns the exit code. */
static int cmd_list(McamFile *f, int argc, char **argv) {
    for (int i = 0; i < nch; i++) {
        double *xy, z;
        int n = cam_chain_polyline(f, &chains[i], 0.01, &xy, &z);
        double lo[2] = { 1e300, 1e300 }, hi[2] = { -1e300, -1e300 }, a = 0;   /* box, twice the signed area */
        for (int k = 0; k < n; k++) {
            grow_box(lo, hi, &xy[2 * k]);
            int q = (k + 1) % n;
            a += xy[2 * k] * xy[2 * q + 1] - xy[2 * q] * xy[2 * k + 1];
        }
        printf("chain %3d: %3d entities %-6s z=%.3f  box %.2f..%.2f x %.2f..%.2f  area %.2f", i, chains[i].n,
               chains[i].closed ? "closed" : "open", z, lo[0], hi[0], lo[1], hi[1],
               chains[i].closed ? fabs(a) / 2 : 0);
        /* ours (test tooling): the chain as an ids: spec (chain_from_ids: f / r = as chained here, the chain's
           direction = its signed area: + CCW) */
        if (argc > 3 && !strcmp(argv[3], "--ids")) {
            printf("  %s  ids:", chains[i].closed ? (a > 0 ? "ccw" : "cw") : "");
            for (int k = 0; k < chains[i].n; k++)
                printf("%s%d%c", k ? "," : "", f->ents[chains[i].ent[k]].idn, chains[i].rev[k] ? 'r' : 'f');
        }
        printf("\n");
        free(xy);
    }
    return 0;
}

/* ents: ours (test tooling): every entity - id (ent idn, as ids: specs take), type, geometry (line a -> b; arc
   centre, radius, start angle, sweep in deg, its ends), and the `list` chain it is in (-1 = none). Returns the exit
   code. */
static int cmd_ents(McamFile *f) {
    int *inch = malloc(sizeof(int) * (size_t)(f->nents + 1));   /* entity -> its chain */
    for (int i = 0; i < f->nents; i++)
        inch[i] = -1;
    for (int c = 0; c < nch; c++)
        for (int k = 0; k < chains[c].n; k++)
            inch[chains[c].ent[k]] = c;
    for (int i = 0; i < f->nents; i++) {
        const Ent *e = &f->ents[i];
        if (!e->type)
            continue;
        printf("ent %6d chain %3d ", e->idn, inch[i]);
        if (e->type == E_LINE)
            printf("line  %.4f %.4f %.4f -> %.4f %.4f %.4f\n", e->a.x, e->a.y, e->a.z, e->b.x, e->b.y, e->b.z);
        else if (e->type == E_ARC)
            printf("arc   c %.4f %.4f %.4f r %.4f start %.3f sweep %.3f  %.4f %.4f -> %.4f %.4f%s\n",
                   e->c.x, e->c.y, e->c.z, e->r, e->a0 * 180 / M_PI, e->sw * 180 / M_PI,
                   e->a.x, e->a.y, e->b.x, e->b.y,
                   e->vw ? " (not an XY arc)" : "");
        else if (e->type == E_POINT)
            printf("point %.4f %.4f %.4f\n", e->a.x, e->a.y, e->a.z);
        else if (e->type == E_SPLINE)
            printf("spline %.4f %.4f %.4f -> %.4f %.4f %.4f\n", e->a.x, e->a.y, e->a.z, e->b.x, e->b.y, e->b.z);
        else
            printf("type 0x%x\n", e->type);
    }
    free(inch);
    return 0;
}

/* ------------------------------------------------------------------------------ an operation made on chains */

/* what an operation run's options ask for besides the op's own fields */
typedef struct {
    const char *nc, *svg, *moves_out, *chains_out;   /* output files */
    const char *mcam_out;
    int top_set;            /* --top (or --set top_z=) given */
    int side_svg;           /* --side-svg: the SVG in side view (x, z * 5) */
    int do_verify;
    /* ours (test tooling): --comp / --comp-type / --chain-dir / --tab-* */
    int cli_comp_dir, cli_comp_off, cli_chain_dir, cli_tab_on;
} OpCli;

/* --tool N: the part's tool number N into P; 0 when the part has none. */
static int use_part_tool(McamFile *f, CamParams *p, int n) {
    for (int k = 0; k < f->ntools; k++)
        if (f->tools[k].num == n) {
            cam_params_from_tool(p, &f->tools[k]);
            if (f->tools[k].retract > 0)
                p->mc_rrate = f->tools[k].retract;
            return 1;
        }
    return 0;
}

/* --lib LIB --libtool WANT: a library tool (the first whose name contains WANT, else WANT as an index) into P, added
   to the part's tools; 0 (with a message) when there is none. */
static int use_library_tool(McamFile *f, CamParams *p, const char *lib, const char *want) {
    ToolLib L;
    char err[256];
    if (!toollib_load(lib, &L, err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        return 0;
    }
    int k = -1;
    for (int j = 0; j < L.n && k < 0; j++)
        if (strstr(L.t[j].name, want))
            k = j;
    if (k < 0 && want[0] >= '0' && want[0] <= '9')
        k = atoi(want);
    if (k < 0 || k >= L.n) {
        fprintf(stderr, "no tool '%s' in %s\n", want, lib);
        return 0;
    }
    Tool *t = &L.t[k];
    cam_params_from_tool(p, t);
    if (t->retract > 0)
        p->mc_rrate = t->retract;   /* GetFeedsAndSpeedsFromTool (cam_fileop.c cam_defaults_from_file) */
    /* the library tool keeps its number (a part may hold two tools of one number in different slots: a tool is
       its slot + number, cam_fileop.c op_tool / NAKTO-BRAKE-BRACKET's two T2). regen-consistency 2026-11-19: it
       gets a slot of its own (op_tool_info +0x264 = record 0x5ac, CamParams.tool_slot) - with none, the save
       took the part's first tool of that number (mcam_saveops.c: mcam_tool) and the saved op regenerated with
       another tool. The save gives the new tool its part slot (TpToolList::OpenSlot, mcam_saveops.c open_slot);
       here only one no part tool holds */
    if (f->ntools < 256) {
        int s = 0;
        for (int j = 0; j < f->ntools; j++)
            if (f->tools[j].slot > s)
                s = f->tools[j].slot;
        tool_copy(&f->tools[f->ntools], t);
        f->tools[f->ntools++].slot = s + 1;
        p->tool_slot = s + 1;
    }
    printf("library tool: T%d %s D%.3f %s S%d F%.0f\n", t->num, tool_type_name(t->type), t->dia, t->name,
           t->rpm, t->feed);
    toollib_free(&L);
    return 1;
}

/* --holes: x,y,d;x,y,d;... holes in this order (after any from chains) */
static void parse_holes(CamOp *op, const char *spec) {
    static char hb[8192];
    snprintf(hb, sizeof hb, "%s", spec);
    for (char *sv = NULL, *t = strtok_r(hb, ";", &sv); t; t = strtok_r(NULL, ";", &sv)) {
        double x, y, dd;
        if (sscanf(t, "%lf,%lf,%lf", &x, &y, &dd) != 3)
            continue;
        add_hole(op, x, y, dd);
    }
}

/* --tabpts: x,y;x,y;... tab centres. Ours (test tooling, user decision CLI-2 2026-11-19): they make the op's tabs
   Manual - the decoded record field 0x1381 = 0 (cam_tab_mirror -> tab_auto; Automatic would make the tabs again and
   drop these, cam2d.c gen_contour); --tab-auto after it wins */
static void parse_tab_points(CamParams *p, const char *spec) {
    char buf[2048];
    snprintf(buf, sizeof buf, "%s", spec);
    p->ntab_xy = 0;
    cli_rec_set(p, REC_TAB_AUTO, 'b', 0);
    for (char *sv = NULL, *t = strtok_r(buf, ";", &sv); t && p->ntab_xy < CAM_TAB_MAX; t = strtok_r(NULL, ";", &sv))
        if (sscanf(t, "%lf,%lf", &p->tab_xy[2 * p->ntab_xy], &p->tab_xy[2 * p->ntab_xy + 1]) == 2)
            p->ntab_xy++;
}

/* Reads the options (argv[4..]) of an operation run into OP and CLI; 0 (with a message) on a bad option. */
static int op_parse_options(McamFile *f, CamOp *op, OpCli *cli, int argc, char **argv) {
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--dia")) {
            op->p.tool_dia = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--depth")) {
            op->p.final_z = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--top")) {
            op->p.top_z = atof(argv[++i]);
            cli->top_set = 1;
        } else if (!strcmp(argv[i], "--step")) {
            op->p.stepdown = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--stepover")) {
            op->p.stepover_pct = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--side")) {
            const char *s = argv[++i];
            op->p.side = !strcmp(s, "in") ? SIDE_INSIDE : !strcmp(s, "on") ? SIDE_ON : SIDE_OUTSIDE;
        } else if (!strcmp(argv[i], "--tabs")) {
            op->p.tabs = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--finish")) {
            op->p.finish_n = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--lead")) {
            op->p.lead_r = atof(argv[++i]);
            cli_lead_in = 1;
        } else if (!strcmp(argv[i], "--stock")) {
            op->p.stock_xy = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--noarcs")) {
            op->p.arcs = 0;
        } else if (!strcmp(argv[i], "--side-svg")) {
            cli->side_svg = 1;
        } else if (!strcmp(argv[i], "--tool")) {       /* a tool of the part, by its number */
            int n = atoi(argv[++i]);
            if (!use_part_tool(f, &op->p, n)) {
                fprintf(stderr, "no tool T%d in the part\n", n);
                return 0;
            }
        } else if (!strcmp(argv[i], "--lib") && i + 2 < argc && !strcmp(argv[i + 2], "--libtool") && i + 3 < argc) {
            if (!use_library_tool(f, &op->p, argv[i + 1], argv[i + 3]))
                return 0;
            i += 3;
        } else if (!strcmp(argv[i], "--holes")) {
            parse_holes(op, argv[++i]);
        } else if (!strcmp(argv[i], "--tabpts")) {
            parse_tab_points(&op->p, argv[++i]);
        }
        /* ours (test tooling, cli-opts 2026-11-19): Compensation direction / type and the chain direction, as
           Mastercam's record and chaining: --comp = record 0x5d2 (0 left, 1 right of the chain's direction; a stored
           contour with chain directions reads as side 1 / 2 + side_rel, cam_fileop.c params_from_record; slot / helix /
           circle read it as mc_comp_right); --comp-type = 0x5d0 (0 Computer, 1 Control, 2 Wear, 3 Reverse wear, 4 Off:
           a contour with 4 is SIDE_ON, params_from_record); --chain-dir = every closed chain turned CW / CCW as
           FUN_1801e8660 turns a chain to config 6001 (cam_chain_set_dir) */
        else if (!strcmp(argv[i], "--comp") && i + 1 < argc) {
            int right = !strcmp(argv[++i], "right");
            if (!right && strcmp(argv[i], "left")) {
                fprintf(stderr, "--comp left|right\n");
                return 0;
            }
            cli_rec_set(&op->p, REC_COMP_DIR, 'b', right);
            cli->cli_comp_dir = 1;
        } else if (!strcmp(argv[i], "--comp-type") && i + 1 < argc) {
            static const char *ct[] = { "computer", "control", "wear", "reverse", "off" };
            int k = -1;
            for (int j = 0; j < 5; j++)
                if (!strcmp(argv[i + 1], ct[j]))
                    k = j;
            if (k < 0) {
                fprintf(stderr, "--comp-type computer|control|wear|reverse|off\n");
                return 0;
            }
            i++;
            cli_rec_set(&op->p, REC_COMP_TYPE, 's', k);
            cli->cli_comp_off = k == COMP_TYPE_OFF ? 1 : -1;
        } else if (!strcmp(argv[i], "--chain-dir") && i + 1 < argc) {
            i++;
            cli->cli_chain_dir = !strcmp(argv[i], "ccw") ? 1 : !strcmp(argv[i], "cw") ? 0 : -1;
            if (cli->cli_chain_dir < 0) {
                fprintf(stderr, "--chain-dir cw|ccw\n");
                return 0;
            }
        }
        /* ours (test tooling, cli-opts 2026-11-19): the Tabs page 2834 as record fields (cam_fileop.c
           params_from_record / cam_tab_mirror; cam2d.c gen_contour, ct_auto_tabs, cut_path). Any of them turns tabs on
           (0x1380; tabs = the number 0x1382, at least 1, as params_from_record). --tab-auto / --tab-manual 0x1381
           (Manual: the --tabpts centres); --tab-num N = 0x1440 0 (by number) + 0x1382 N; --tab-maxdist D = 0x1440 1 +
           0x1448 D; --tab-width 0x1388, --tab-height (thickness) 0x1390; --tab-full / --tab-partial 0x1398; --tab-ramp
           DEG = 0x139c Ramp (1) + 0x13b0 angle (rad); --tab-vertical = 0x139c 0 */
        else if (!strcmp(argv[i], "--tab-auto")) {
            cli_rec_set(&op->p, REC_TAB_AUTO, 'b', 1);
            cli->cli_tab_on = 1;
        } else if (!strcmp(argv[i], "--tab-manual")) {
            cli_rec_set(&op->p, REC_TAB_AUTO, 'b', 0);
            cli->cli_tab_on = 1;
        } else if (!strcmp(argv[i], "--tab-num") && i + 1 < argc) {
            cli_rec_set(&op->p, REC_TAB_BY, 'b', 0);
            cli_rec_set(&op->p, REC_TAB_NUM, 's', atoi(argv[++i]));
            cli->cli_tab_on = 1;
        } else if (!strcmp(argv[i], "--tab-maxdist") && i + 1 < argc) {
            cli_rec_set(&op->p, REC_TAB_BY, 'b', 1);
            cli_rec_set(&op->p, REC_TAB_MAXDIST, 'd', atof(argv[++i]));
            cli->cli_tab_on = 1;
        } else if (!strcmp(argv[i], "--tab-width") && i + 1 < argc) {
            op->p.tab_w = atof(argv[++i]);
            cli_rec_set(&op->p, REC_TAB_WIDTH, 'd', op->p.tab_w);
            cli->cli_tab_on = 1;
        } else if (!strcmp(argv[i], "--tab-height") && i + 1 < argc) {
            op->p.tab_h = atof(argv[++i]);
            cli_rec_set(&op->p, REC_TAB_HEIGHT, 'd', op->p.tab_h);
            cli->cli_tab_on = 1;
        } else if (!strcmp(argv[i], "--tab-full") || !strcmp(argv[i], "--tab-partial")) {
            cli_rec_set(&op->p, REC_TAB_FULL, 'b', !strcmp(argv[i], "--tab-full"));
            cli->cli_tab_on = 1;
        } else if (!strcmp(argv[i], "--tab-ramp") && i + 1 < argc) {
            cli_rec_set(&op->p, REC_TAB_RAMP, 's', 1);
            cli_rec_set(&op->p, REC_TAB_RAMP_ANGLE, 'd', atof(argv[++i]) * M_PI / 180);
            cli->cli_tab_on = 1;
        } else if (!strcmp(argv[i], "--tab-vertical")) {
            cli_rec_set(&op->p, REC_TAB_RAMP, 's', 0);
            cli->cli_tab_on = 1;
        } else if (!strcmp(argv[i], "--tipcomp")) {
            op->p.tip_comp = 1;
        } else if (!strcmp(argv[i], "--chamfer")) {
            op->p.contour_type = 1;
            op->p.chamfer_w = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--chamfer-tip")) {
            op->p.chamfer_tip = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--engrave")) {
            op->p.contour_type = 2;
        } else if (!strcmp(argv[i], "--pitch")) {
            op->p.thread_pitch = atof(argv[++i]);
            op->p.helix_pitch = op->p.thread_pitch;
        } else if (!strcmp(argv[i], "--major")) {
            op->p.thread_major = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--external")) {
            op->p.thread_internal = 0;
        } else if (!strcmp(argv[i], "--lh")) {
            op->p.thread_rh = 0;
        } else if (!strcmp(argv[i], "--passes")) {
            op->p.thread_passes = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--single")) {
            op->p.thread_full = 0;
        } else if (!strcmp(argv[i], "--outside")) {
            op->p.dyn_outside = 1;
        } else if (!strcmp(argv[i], "--wall")) {
            op->p.wall_stock = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--width")) {
            op->p.slot_width = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--minrad")) {
            op->p.min_rad_pct = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--oneway")) {
            op->p.face_style = 1;
        } else if (!strcmp(argv[i], "--angle")) {
            op->p.face_angle = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--box")) {
            for (int k = 0; k < 4; k++)
                op->p.box[k] = atof(argv[++i]);
            op->p.use_box = 1;
        } else if (!strcmp(argv[i], "--coolant")) {
            op->p.coolant = 1;
        } else if (!strcmp(argv[i], "--verify")) {
            cli->do_verify = 1;
        } else if (!strcmp(argv[i], "--mcam")) {
            cli->mcam_out = argv[++i];
        } else if (!strcmp(argv[i], "--nc")) {
            cli->nc = argv[++i];
        } else if (!strcmp(argv[i], "--svg")) {
            cli->svg = argv[++i];
        } else if (!strcmp(argv[i], "--moves")) {
            cli->moves_out = argv[++i];
        } else if (!strcmp(argv[i], "--chains-out")) {
            cli->chains_out = argv[++i];   /* ours (testing, tools/tests/hst_check.py): chain polylines */
        } else if (!strcmp(argv[i], "--set")) {
            if (strstr(argv[i + 1], "top_z="))
                cli->top_set = 1;
            set_params(&op->p, argv[++i]);
        } else if (!strcmp(argv[i], "--rec")) {
            cli_rec_patch(&op->p, argv[++i]);
        } else if (!strcmp(argv[i], "--solid") && op->nref < CAM_REF_MAX) {   /* 3drough: a drive solid (1-based) */
            CamRef *r = &op->ref[op->nref++];
            memset(r, 0, sizeof *r);
            r->cls = TP_ECLS_SOLID;
            r->index = atoi(argv[++i]) - 1;
            if (r->index >= 0 && r->index < f->nsolids)
                r->idn = f->solids[r->index].idn;
        } else if (!strcmp(argv[i], "--surf") && op->nref < CAM_REF_MAX) {
            /* a drive surface (1-based, F.surfs order) */
            CamRef *r = &op->ref[op->nref++];
            memset(r, 0, sizeof *r);
            r->cls = TP_ECLS_SURF;
            r->index = atoi(argv[++i]) - 1;
            if (r->index >= 0 && r->index < f->nsurfs)
                r->idn = f->surfs[r->index].idn;
        } else if (!strcmp(argv[i], "--stl")) {
            cam3d_set_stl(argv[++i]);   /* 3drough (ours, testing): an STL drive model */
        } else if (!strcmp(argv[i], "--lathe-defaults")) {
            /* lface (ours, testing): the lathe .mcam-defaults a new lathe op takes its record from */
            cam_lathe_defaults_set(argv[++i]);
        }
    }
    return 1;
}

/* The record fields the options set: the patches applied, tabs on, the contour's compensation side. */
static void op_apply_record_options(CamOp *op, int type, const OpCli *cli) {
    if (op->p.nrpatch)
        cam_params_from_patches(&op->p);
    /* Tabs on 0x1380 (params_from_record: tabs = max(1, the number 0x1382)) */
    if (cli->cli_tab_on) {
        cli_rec_set(&op->p, REC_TAB_ON, 'b', 1);
        if (op->p.tabs <= 0)
            op->p.tabs = op->p.tab_n > 0 ? op->p.tab_n : 1;
        if (!op->p.tab_rec)
            fprintf(stderr, "warning: no default operation record - the --tab-* fields are not read by this op\n");
    }
    if (type == CAM_CONTOUR && cli->cli_comp_off > 0) {
        op->p.side = SIDE_ON;   /* 0x5d0 = 4 (params_from_record: off -> SIDE_ON) */
    } else if (type == CAM_CONTOUR && (cli->cli_comp_dir || cli->cli_comp_off < 0)) {
        /* 0x5d2 (ct_off_right, after the patches) against each chain's direction: params_from_record's known-direction
           branch */
        if (op->p.side == SIDE_ON && cli->cli_comp_off == 0) {
            fprintf(stderr, "note: --comp with compensation Off (0x5d0 = 4): kept off\n");
        } else {
            op->p.side = op->p.ct_off_right ? 2 : 1;
            op->p.side_rel = 1;
        }
    }
}

/* The op's chains from LIST (ids:... specs, or chain indexes of `list`), and for the hole ops their holes; then the
   --chain-dir and the top of stock (the first chain's z) unless given. LIST is consumed. */
static void op_add_chains(McamFile *f, CamOp *op, int type, char *list, const OpCli *cli) {
    op->chains = calloc(MAX_OP_CHAINS, sizeof(Chain));
    /* ids:1,7,34/20 - chains as entity ids in the operation's order */
    if (!strncmp(list, "ids:", 4)) {
        for (char *sv = NULL, *c = strtok_r(list + 4, "/", &sv); c && op->nchains < MAX_OP_CHAINS;
             c = strtok_r(NULL, "/", &sv)) {
            int avoid = !strncmp(c, "a:", 2);
            if (avoid)
                c += 2;   /* a:ids - an avoidance region (Dynamic family) */
            Chain ch;
            if (chain_from_ids(f, c, &ch)) {
                ch.avoid = avoid;
                ch.role = avoid ? 4 : 0;
                op->chains[op->nchains++] = ch;
            }
            if (cam_type_uses_holes(type))             /* holes: its arcs' centres and points */
                for (int e = 0; e < ch.n; e++) {
                    const Ent *en = &f->ents[ch.ent[e]];
                    if (en->type == E_ARC || en->type == E_POINT)
                        add_hole(op, en->type == E_ARC ? en->c.x : en->a.x, en->type == E_ARC ? en->c.y : en->a.y,
                                 en->type == E_ARC ? 2 * en->r : 0);
                }
        }
        list[0] = 0;
    }
    for (char *t = strtok(list, ","); t; t = strtok(NULL, ",")) {   /* chain indexes of `list` */
        int k = atoi(t);
        if (k < 0 || k >= nch)
            continue;
        op->chains[op->nchains++] = chains[k];
        if (cam_type_uses_holes(type)) {   /* holes: centres (and diameters) of circles in the chain */
            for (int e = 0; e < chains[k].n; e++) {
                const Ent *en = &f->ents[chains[k].ent[e]];
                if (en->type == E_ARC && fabs(fabs(en->sw) - 2 * M_PI) < 1e-6)
                    add_hole(op, en->c.x, en->c.y, 2 * en->r);
            }
        }
    }
    if (cli->cli_chain_dir >= 0)                       /* --chain-dir: FUN_1801e8660 (closed chains only) */
        for (int c = 0; c < op->nchains; c++)
            cam_chain_set_dir(f, &op->chains[c], cli->cli_chain_dir);
    if (!cli->top_set && op->nchains) {                /* top of stock: the first chain's z */
        double *xy, z;
        cam_chain_polyline(f, &op->chains[0], 0.01, &xy, &z);
        free(xy);
        op->p.top_z = z;
    }
}

/* The record rules a saved op is regenerated with, applied to the op as cut here (below); 0 (with a message) when
   --engrave contradicts --comp-type. */
static int op_record_rules(CamOp *op, int type, const OpCli *cli) {
    /* regen-consistency 2026-11-19: depth cuts on (record 0x868) as the save writes it for a contour / pocket - more
       than one cut at the max rough step (mcam_saveops.c op_fields_to_record) - so the rules that read 0x868 (cam2d.c
       gen_contour: keep tool down between depths op +0x829, warning 0x93b's rough / finish counts) act on the op as cut
       as on the saved op */
    if ((type == CAM_CONTOUR || type == CAM_POCKET) && op->p.stepdown > 1e-6 && !op->p.mc_depth_on)
        op->p.mc_depth_on = (int)ceil((op->p.top_z - op->p.final_z) / op->p.stepdown - 1e-9) > 1;
    /* regen-consistency 2026-11-19: --finish N on a contour = Multi Passes on (record 0x8a8) with rough 1 (0x8aa) and N
       finish passes (0x8b8) at the finish spacing (0x8c0), as the save writes it (mcam_saveops.c op_fields_to_record);
       the pass rules then read the default op's own Multi Passes fields (rough spacing 0x8b0, finish at every depth
       0x1230: cam2d.c gen_contour, decomp/mcmill_ctour.c l.1506-1700) as the saved op does */
    if (type == CAM_CONTOUR && op->p.ct_mp_rec && op->p.finish_n > 0 && !op->p.mc_mp_on) {
        op->p.mc_mp_on = 1;
        op->p.mc_mp_nr = 1;
        op->p.mc_mp_nf = op->p.finish_n;
        op->p.mc_mp_fs = op->p.finish_step;
    }
    /* --engrave (or --set contour_type=2): not a Mastercam Contour mode - the contour type combo (record S16 0x1308) is
       0 2D, 1 3D, 2 2D chamfer, 3 3D chamfer, 4 Ramp, 5 Remachining, 6 Oscillate (cam_fileop.c params_from_record;
       formats/audit_2026-10-26_coreui.md 1.7: "Engrave" was ours, Mastercam's Engrave is engrave.dll opcode 416 =
       CAM_ENGRAVE). It made a 2D contour with the tool on the chain and no leads (cam2d.c gen_contour) while the save
       wrote 0x8f0 from lead_on, i.e. the default op's Lead In/Out (on in Mill_MM.mcam-defaults): the saved op
       regenerated with ~R10 arcs. regen-consistency 2026-11-19: it is now that 2D contour in the record's own terms -
       Compensation Off (0x5d0 = 4, params_from_record -> SIDE_ON) and Lead In/Out off (0x8f0 = 0, LEADS -> lead_on;
       after --lead) - which the save writes and every regeneration reads (tools/tests/regen_consistency.py) */
    if (type == CAM_CONTOUR && op->p.contour_type == 2) {
        if (cli->cli_comp_off < 0) {
            fprintf(stderr, "--engrave is Compensation Off (0x5d0 = 4): --comp-type %s contradicts it\n",
                    "computer|control|wear|reverse");
            return 0;
        }
        cam_contour_engrave_compat(&op->p);
    }
    return 1;
}

/* --svg: the op as an SVG picture - top view (SIDE_SVG: side view, x, z * 5): chains black, moves coloured. */
static void write_svg(McamFile *f, const CamOp *op, const char *svg, int side_svg) {
    double lo[2] = { 1e300, 1e300 }, hi[2] = { -1e300, -1e300 };
    for (int i = 0; i < op->nm; i++) {
        double v[2] = { op->m[i].p.x, side_svg ? op->m[i].p.z * 5 : op->m[i].p.y };
        grow_box(lo, hi, v);
    }
    for (int c = 0; c < op->nchains && !side_svg; c++) {
        double *xy, z;
        int n = cam_chain_polyline(f, &op->chains[c], 0.01, &xy, &z);
        for (int k = 0; k < n; k++)
            grow_box(lo, hi, &xy[2 * k]);
        free(xy);
    }
    /* scale (800 px across, 90 %) and origin (5 % margin) */
    double s = 800 / fmax(hi[0] - lo[0], hi[1] - lo[1]) * 0.9;
    double ox = lo[0] - (hi[0] - lo[0]) * 0.05, oy = hi[1] + (hi[1] - lo[1]) * 0.05;
    FILE *o = fopen(svg, "w");
    fprintf(o, "<svg xmlns='http://www.w3.org/2000/svg' width='%d' height='%d' style='background:#fff'>\n",
            (int)((hi[0] - lo[0]) * s * 1.12) + 20, (int)((hi[1] - lo[1]) * s * 1.12) + 20);
    for (int c = 0; c < op->nchains && !side_svg; c++) {
        double *xy, z;
        int n = cam_chain_polyline(f, &op->chains[c], 0.01, &xy, &z);
        fprintf(o, "<polyline fill='none' stroke='#000' stroke-width='2' points='");
        for (int k = 0; k <= n - (op->chains[c].closed ? 0 : 1); k++)
            fprintf(o, "%.2f,%.2f ", (xy[2 * (k % n)] - ox) * s, (oy - xy[2 * (k % n) + 1]) * s);
        fprintf(o, "'/>\n");
        free(xy);
    }
    /* moves: rapid orange, at final depth blue, above it green */
    for (int i = 1; i < op->nm; i++)
        fprintf(o, "<line x1='%.2f' y1='%.2f' x2='%.2f' y2='%.2f' stroke='%s' stroke-width='%s'/>\n",
                (op->m[i - 1].p.x - ox) * s, (oy - (side_svg ? op->m[i - 1].p.z * 5 : op->m[i - 1].p.y)) * s,
                (op->m[i].p.x - ox) * s, (oy - (side_svg ? op->m[i].p.z * 5 : op->m[i].p.y)) * s,
                op->m[i].rapid ? "#e0a000" : (op->m[i].p.z <= op->p.final_z + 1e-6 ? "#0050d0" : "#20a040"),
                op->m[i].rapid ? "0.6" : "1");
    fprintf(o, "</svg>\n");
    fclose(o);
    printf("wrote %s\n", svg);
}

/* an operation type (argv[2]) on chains (argv[3]): CHAIN[,CHAIN..] indexes of `list`, or ids:... - generated,
   reported, written. Returns the exit code. */
static int cmd_operation(McamFile *f, int argc, char **argv) {
    static const char *names[] = {
        "", "contour", "pocket", "drill", "face", "slot", "helix", "circle", "thread", "dynamic", "area", "dyncontour",
        "peel", "optirough", "srpocket", "srparallel", "waterline", "sfparallel", "raster", "arearough", "spiral",
        "radial", "scallop", "sfcontour", "hstproject", "srproject", "engrave", "pencil", "blend", "srmpocket",
        "srplunge", "sfradial", "sfproject", "horizarea", "sfpencil", "lface"
    };
    int type = 0;
    for (int t = 1; t < CAM_NTYPES; t++)
        if (!strcmp(argv[2], names[t]))
            type = t;
    if (!type) {
        fprintf(stderr, "unknown operation %s\n", argv[2]);
        return 1;
    }
    CamOp op;
    memset(&op, 0, sizeof op);
    cam_defaults(&op.p, type, f);
    op.name[0] = 0;   /* a new op's comment (op +6) is empty (InitOperation, L-7) */
    OpCli cli = { .cli_chain_dir = -1 };
    char *list = argc > 3 ? strdup(argv[3]) : strdup("");

    if (!op_parse_options(f, &op, &cli, argc, argv))
        return 1;
    op_apply_record_options(&op, type, &cli);
    op_add_chains(f, &op, type, list, &cli);
    cli_lead_mirror(&op.p);
    if (!op_record_rules(&op, type, &cli))
        return 1;

    /* generate, report, write the outputs */
    if (!cam_generate(f, &op)) {
        printf("FAILED: %s\n", op.error);
        return 1;
    }
    if (op.warning[0])
        printf("WARNING: %s\n", op.warning);   /* decisions batch dec1: a generated op's stand-in warning */
    double len = 0;
    int cuts = 0;
    for (int i = 1; i < op.nm; i++)
        if (!op.m[i].rapid) {
            cuts++;
            len += sqrt(pow(op.m[i].p.x - op.m[i - 1].p.x, 2) + pow(op.m[i].p.y - op.m[i - 1].p.y, 2)
                        + pow(op.m[i].p.z - op.m[i - 1].p.z, 2));
        }
    printf("%s: %d moves (%d cutting), cut length %.1f, tool D%.3f, top %.3f -> %.3f\n", argv[2], op.nm, cuts, len,
           op.p.tool_dia, op.p.top_z, op.p.final_z);
    if (cli.do_verify)
        verify(f, &op, !op.p.dyn_outside && type != CAM_FACE);
    if (cli.mcam_out) {
        char msg[600];
        CamOp *ops1[1] = { &op };
        mcam_save_with_ops(argv[1], cli.mcam_out, f, ops1, 1, msg, sizeof msg);
        printf("%s\n", msg);
    }
    for (int i = 4; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--params")) {            /* ours (tests, as fileop's) */
            CamOp *ops1[1] = { &op };
            cam_save_ops(argv[++i], ops1, 1);
        }
    for (int i = 4; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--movesf"))              /* ours (tests, as fileop's) */
            write_moves_feed(argv[++i], &op);
    if (cli.moves_out)                                 /* x y z rapid per move, for comparisons */
        write_moves(cli.moves_out, &op);
    if (cli.chains_out)                                /* "chain N closed|open", then x y per point */
        write_chain_polylines(f, cli.chains_out, &op);
    if (cli.nc) {
        FILE *o = fopen(cli.nc, "w");
        CamOp *ops[1] = { &op };
        cam_write_gcode(o, f, ops, 1, "CAM_CLI");
        fclose(o);
        printf("wrote %s\n", cli.nc);
    }
    if (cli.svg)
        write_svg(f, &op, cli.svg, cli.side_svg);
    return 0;
}

/* ----------------------------------------------------------------------------------------------------- main */

int cam3d_chktrim_selftest(FILE *fo);   /* cam3d_checktrim.c (through cam2d.c) */

int main(int argc, char **argv) {
    /* ---- the subcommands without a part */
    /* ours (testing): mcce.dll region boolean unit oracle, port side (tools/tests/sobool_oracle.sh) */
    if (argc == 4 && !strcmp(argv[1], "sobool"))
        return mcce_region_bool_file(argv[2], argv[3]);
    /* ours (testing): mcce.dll MCCECalcOffset unit oracle, port side (tools/tests/off_oracle.sh) */
    if (argc == 4 && !strcmp(argv[1], "sooff"))
        return mcce_offset_file(argv[2], argv[3]);
    /* ours (testing): Edit Passes to Surfaces 0x141f on analytic cases */
    if (argc == 2 && !strcmp(argv[1], "chktrim-test"))
        return cam3d_chktrim_selftest(stdout) ? 1 : 0;
    if (argc < 3) {
        fprintf(stderr, "usage: see source\n");
        return 1;
    }
    if (!strcmp(argv[1], "nci-filter"))
        return cmd_nci_filter(argc, argv);

    /* ---- load the part */
    cam_set_prev_ops_fn(cam_prev_ops_stored);   /* Remachining sources 0 / 1 on a stored op */
    McamFile f;
    if (!mcam_load(&f, argv[1])) {
        fprintf(stderr, "%s\n", mcam_error());   /* (a newer release's part: mccoreres 5280) */
        return 1;
    }
    for (int i = 0; i < f.nents; i++)
        if (f.ents[i].type && f.ents[i].idn > 0 && f.ents[i].idn < 1 << 20)
            idn_max = f.ents[i].idn > idn_max ? f.ents[i].idn : idn_max;
    find_chains(&f);

    /* ---- the subcommands on a part */
    if (!strcmp(argv[2], "bpinfo"))
        return cmd_bpinfo(&f);
    if (!strcmp(argv[2], "vsim"))
        return cmd_vsim(&f, argc, argv);
    if (!strcmp(argv[2], "nci-filterpost") && argc > 14)
        return cmd_nci_filterpost(&f, argv);
    if (!strcmp(argv[2], "post"))
        return cmd_post(&f, argc, argv);
    if (!strcmp(argv[2], "reverse") && argc > 3)
        return cmd_reverse(&f, argc, argv);
    if (!strcmp(argv[2], "areaparams") && argc > 3)
        return cmd_areaparams(&f, argv);
    if (!strcmp(argv[2], "nciw-replace") && argc > 4)
        return cmd_nciw_replace(&f, argv);
    if (!strcmp(argv[2], "nciw") && argc > 4)
        return cmd_nciw(&f, argc, argv);
    if (!strcmp(argv[2], "fileop") && argc > 3)
        return cmd_fileop(&f, argc, argv);
    if (!strcmp(argv[2], "stockmodel"))
        return cmd_stockmodel(&f);
    if (!strcmp(argv[2], "tprefs"))
        return cmd_tprefs(&f);
    if (!strcmp(argv[2], "saveref") && argc > 4)
        return cmd_saveref(&f, argc, argv);
    if (!strcmp(argv[2], "list"))
        return cmd_list(&f, argc, argv);
    if (!strcmp(argv[2], "ents"))
        return cmd_ents(&f);

    /* ---- anything else: an operation type */
    return cmd_operation(&f, argc, argv);
}
