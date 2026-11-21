/* cam_fileop.c - an operation stored in a Mastercam part as an editable CamOp: its parameters (OP_ID record) mapped to
 * CamParams, its chains / points / tabs from the TP_ID records. The rules were measured against Mastercam's own
 * toolpaths with ../tools/oracle.py (Python reference of this file); offsets in ../formats/mcam.md. */
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "paths.h"
#include "cam2d.h"
#include "cam_fileop.h"
#include "area_mill.h"

/* the op's tool: by its slot (op +0x5ac = the TL_ID +0x2cc slot, formats/part_tools.md 1.1) - tool numbers can repeat
   (BRACKET4 has two T8s) - else the first tool with its number */
static int rd32_(const uint8_t *r, uint32_t n, uint32_t o) { int32_t v = 0; if (o + 4 <= n) memcpy(&v, r + o, 4); return v; }
static const Tool *op_tool(const McamFile *f, const Op *o, int tnum) {
    if (o && o->slot >= 0) for (int k = 0; k < f->ntools; k++) if (f->tools[k].slot == o->slot && f->tools[k].num == tnum) return &f->tools[k];
    for (int k = 0; k < f->ntools; k++) if (f->tools[k].num == tnum) return &f->tools[k];
    return NULL;
}
/* the index in f->tools of the tool stored op OP_INDEX cuts with: op_tool's choice (the op's own slot first, then the
   number), the tool cam_op_from_file takes its diameter / shape from; -1 = none. A part may hold two tools with one number
   in different slots (NAKTO-BRAKE-BRACKET: T2 6 mm flat and T2 3 mm flat) - the op dialog must not pick by number alone. */
int cam_op_tool_index(const McamFile *f, int op_index) {
    if (op_index < 0 || op_index >= f->nops) return -1;
    const Op *o = &f->ops[op_index]; if (!o->raw) return -1;
    const Tool *t = op_tool(f, o, rd32_(o->raw, o->raw_n, 0x348));
    return t ? (int)(t - f->tools) : -1;
}
#include "xt_mesh.h"

static double rdd(const uint8_t *r, uint32_t n, uint32_t o) { double v = 0; if (o + 8 <= n) memcpy(&v, r + o, 8); return v; }
static int rd16(const uint8_t *r, uint32_t n, uint32_t o) { int16_t v = 0; if (o + 2 <= n) memcpy(&v, r + o, 2); return v; }
static int rd32(const uint8_t *r, uint32_t n, uint32_t o) { int32_t v = 0; if (o + 4 <= n) memcpy(&v, r + o, 4); return v; }
static int rd8(const uint8_t *r, uint32_t n, uint32_t o) { return o < n ? r[o] : 0; }

int cam_fileop_type(const Op *o) {
    if (!o->raw) return 0;
    switch (o->opcode) {
    case 1: case 0x82: return CAM_CONTOUR;      /* 0x82 = Tab Cutoff (bnci_cutoff: the contour engine, CamParams.ct_cutoff_op) */
    case 3: return CAM_POCKET;
    case 2: return CAM_DRILL;
    case 100: return CAM_THREAD;
    case 102: case 4: return CAM_FACE;
    case 105: return CAM_SLOT;
    case 106: return CAM_HELIX;
    case 18: return CAM_CIRCLE;
    case 416: return CAM_ENGRAVE;   /* Engrave (engrave.dll C-hook, viewer/engrave.c) */
    case 66: return CAM_LFACE;      /* Lathe Face (mclathe.dll CLFaceTp, viewer/lathe_face.c; formats/lathe.md 6) */
    case 134: switch (rd16(o->raw, o->raw_n, 0x1228)) {   /* 2D High Speed style */
        case 0: return CAM_DYNAMIC; case 1: return CAM_AREA; case 2: return CAM_DYNCONTOUR; case 3: return CAM_PEEL; }
    }
    return 0;
}

/* "Solid Chain": the TP record names only the solid; the chain is the outer loop of its top face (buckle-v6/7/8 op 31:
 * Mastercam's tool centre stays within R + stock of it). The edges become entities appended to F.ents (no polylines,
 * idn -1); returns the index of the first, or -1. */
static double xt_part_scale(const McamFile *f, const double *lo, const double *hi);
int cam_solid_chain_ents(McamFile *f, int idn, int *count) {
    *count = 0;
    for (int i = 0; i < f->nsolids; i++) {
        Solid *s = &f->solids[i];
        if (s->idn != idn) continue;
        XtSchema *sch = s->xt ? xt_schema_for(s->xt, s->xt_size) : NULL;   /* an imported solid (no XT): not read by these (formats/cad_import.md) */
        XtEdge *e = NULL; int n = sch ? xt_top_loop(sch, s->xt, s->xt_size, &e) : 0;
        if (n <= 0) { free(e); return -1; }
        double lo[3] = { 1e300, 1e300, 1e300 }, hi[3] = { -1e300, -1e300, -1e300 };
        for (int k = 0; k < n; k++) for (int j = 0; j < 3; j++) { lo[j] = fmin(lo[j], fmin(e[k].a[j], e[k].b[j])); hi[j] = fmax(hi[j], fmax(e[k].a[j], e[k].b[j])); }
        double sc = xt_part_scale(f, lo, hi);                 /* xt_top_loop is in mm; inch parts / 25.4 */
        for (int k = 0; k < n; k++) { for (int j = 0; j < 3; j++) { e[k].a[j] *= sc; e[k].b[j] *= sc; e[k].c[j] *= sc; } e[k].r *= sc; }
        int first = f->nents;
        Ent *ne = xrealloc(f->ents, (f->nents + n) * sizeof(Ent));
        if (!ne) { free(e); return -1; }
        f->ents = ne;
        for (int k = 0; k < n; k++) {
            Ent *en = &f->ents[f->nents++]; memset(en, 0, sizeof *en);
            en->type = e[k].type == 4 ? E_ARC : E_LINE; en->cls = e[k].type; en->planar = 1; en->idn = -1;
            en->first_poly = f->npolys;
            en->a = (V3){ e[k].a[0], e[k].a[1], e[k].a[2] }; en->b = (V3){ e[k].b[0], e[k].b[1], e[k].b[2] };
            if (e[k].type == 4) { en->c = (V3){ e[k].c[0], e[k].c[1], e[k].c[2] }; en->r = e[k].r; en->a0 = e[k].a0; en->sw = e[k].sw;
                en->a = (V3){ en->c.x + en->r * cos(en->a0), en->c.y + en->r * sin(en->a0), en->c.z };
                en->b = (V3){ en->c.x + en->r * cos(en->a0 + en->sw), en->c.y + en->r * sin(en->a0 + en->sw), en->c.z }; }
        }
        free(e); *count = n; return first;
    }
    return -1;
}

/* Every loop of the same top face (xt_top_face_loops: XT loop order, material on the left), in part units with the scale
 * cam_solid_chain_ents uses (from the outer loop's extents). loop_n[k] = edges of loop k, *outer = the outer loop.
 * Mastercam's "Solid Chain" selection is stored as Parasolid tags (formats/mcam.md "Solid chains": NOT DECODED); on
 * buckle-v6/7/8 op 31 its NCI machines every loop of that face as a region of its own (CORRELATED, formats/dynamic.md
 * "Machining regions"). Reads only (the schema cache is filled when the chain is loaded). */
int cam_solid_face_loops(const McamFile *f, int idn, XtEdge **out, int *loop_n, int max_loops, int *nloops, int *outer) {
    *out = NULL; *nloops = 0; *outer = -1;
    for (int i = 0; i < f->nsolids; i++) {
        const Solid *s = &f->solids[i];
        if (s->idn != idn) continue;
        XtSchema *sch = s->xt ? xt_schema_for(s->xt, s->xt_size) : NULL;
        int n = sch ? xt_top_face_loops(sch, s->xt, s->xt_size, out, loop_n, max_loops, nloops, outer) : 0;
        if (n <= 0 || *outer < 0) { free(*out); *out = NULL; *nloops = 0; return 0; }
        int k0 = 0; for (int k = 0; k < *outer; k++) k0 += loop_n[k];
        double lo[3] = { 1e300, 1e300, 1e300 }, hi[3] = { -1e300, -1e300, -1e300 };
        for (int k = k0; k < k0 + loop_n[*outer]; k++) for (int j = 0; j < 3; j++) {
            lo[j] = fmin(lo[j], fmin((*out)[k].a[j], (*out)[k].b[j])); hi[j] = fmax(hi[j], fmax((*out)[k].a[j], (*out)[k].b[j])); }
        double sc = xt_part_scale(f, lo, hi);
        for (int k = 0; k < n; k++) { XtEdge *e = &(*out)[k]; for (int j = 0; j < 3; j++) { e->a[j] *= sc; e->b[j] *= sc; e->c[j] *= sc; } e->r *= sc;
            if (e->type == 4) { e->a[0] = e->c[0] + e->r * cos(e->a0); e->a[1] = e->c[1] + e->r * sin(e->a0);
                e->b[0] = e->c[0] + e->r * cos(e->a0 + e->sw); e->b[1] = e->c[1] + e->r * sin(e->a0 + e->sw); } }
        return n;
    }
    return 0;
}

static int ent_by_idn(const McamFile *f, int32_t idn) {
    for (int i = 0; i < f->nents; i++) if (f->ents[i].type && f->ents[i].idn == idn) return i;
    return -1;
}

/* a chain from file entity ids in chain order; rev[k] 0/1 = direction from the chain record, -1 = unknown (then each
 * piece is oriented to follow the previous one, the first towards the second) */
int cam_chain_from_idns(McamFile *f, const int32_t *idn, const int *rev, int n, Chain *out) {
    memset(out, 0, sizeof *out); int cap = 16, explicit_dir = 1;
    out->ent = xmalloc(sizeof(int) * cap); out->rev = xmalloc(sizeof(int) * cap);
    for (int i = 0; i < n; i++) if (rev[i] != 0 && rev[i] != 1) explicit_dir = 0;
    for (int i = 0; i < n; i++) {
        int k = ent_by_idn(f, idn[i]);
        if (k < 0) {                                          /* a solid: its loop, in loop order */
            int m, first = cam_solid_chain_ents(f, idn[i], &m);
            if (m > 0) out->solid_idn = idn[i];
            for (int j = 0; j < m; j++) {
                if (out->n == cap) { cap *= 2; out->ent = xrealloc(out->ent, sizeof(int) * cap); out->rev = xrealloc(out->rev, sizeof(int) * cap); }
                out->ent[out->n] = first + j; out->rev[out->n] = rev[i] == 1; out->n++;
            }
            continue;
        }
        if (out->n == cap) { cap *= 2; out->ent = xrealloc(out->ent, sizeof(int) * cap); out->rev = xrealloc(out->rev, sizeof(int) * cap); }
        out->ent[out->n] = k; out->rev[out->n] = rev[i] == 1; out->n++;
    }
    if (!out->n) { free(out->ent); free(out->rev); memset(out, 0, sizeof *out); return 0; }
    double tol = cam_chain_tol(mcam_is_metric(f));
#define END(i, r) ((r) ? f->ents[out->ent[i]].a : f->ents[out->ent[i]].b)
#define BEG(i, r) ((r) ? f->ents[out->ent[i]].b : f->ents[out->ent[i]].a)
#define D2(p, q) hypot((p).x - (q).x, (p).y - (q).y)
    if (out->n > 1 && !explicit_dir) {
        V3 a = f->ents[out->ent[0]].a, b = f->ents[out->ent[0]].b, s2 = f->ents[out->ent[1]].a, e2 = f->ents[out->ent[1]].b;
        out->rev[0] = fmin(D2(a, s2), D2(a, e2)) < fmin(D2(b, s2), D2(b, e2)) - tol;
    }
    for (int i = 1; i < out->n && !explicit_dir; i++) { V3 pe = END(i - 1, out->rev[i - 1]); out->rev[i] = D2(f->ents[out->ent[i]].b, pe) < D2(f->ents[out->ent[i]].a, pe); }
    out->closed = D2(END(out->n - 1, out->rev[out->n - 1]), BEG(0, out->rev[0])) < tol;
    if (out->n == 1 && f->ents[out->ent[0]].type == E_ARC && fabs(fabs(f->ents[out->ent[0]].sw) - 2 * M_PI) < 1e-6) out->closed = 1;
#undef END
#undef BEG
#undef D2
    return 1;
}

/* XT geometry comes out of xt_mesh in mm (metres x 1000); part units: x 1 (metric) or / 25.4 (inch). Unknown units
   (no NCI): the scale whose extents fit the part's geometry, as the display does (scene.c). */
static double xt_part_scale(const McamFile *f, const double *lo, const double *hi) {
    if (f->metric == 1) return 1;
    if (f->metric == 0) return 1 / 25.4;
    double best = 1e300, sc = 1; const double cand[2] = { 1, 1 / 25.4 };
    if (!f->npts) return 1;
    for (int c = 0; c < 2; c++) {
        double e = 0;
        for (int k = 0; k < 3; k++) { double a = (&f->lo.x)[k], b = (&f->hi.x)[k];
            e += fabs((lo[k] + hi[k]) / 2 * cand[c] - (a + b) / 2) + fabs((hi[k] - lo[k]) * cand[c] - (b - a)); }
        if (e < best) { best = e; sc = cand[c]; }
    }
    return sc;
}
/* vertical solid cylinders (x, y, r) of the part in part units, for holes picked on a solid (TP kind 27) */
static int part_cylinders(const McamFile *f, double *xyr, int max) {
    int n = 0;
    for (int i = 0; i < f->nsolids && n < max; i++) {
        XtSchema *sch = f->solids[i].xt ? xt_schema_for(f->solids[i].xt, f->solids[i].xt_size) : NULL;
        if (sch) n += xt_cylinders(sch, f->solids[i].xt, f->solids[i].xt_size, xyr + 3 * n, max - n);
    }
    if (n) {
        double lo[3] = { 1e300, 1e300, 0 }, hi[3] = { -1e300, -1e300, 0 };
        for (int c = 0; c < n; c++) for (int k = 0; k < 2; k++) { lo[k] = fmin(lo[k], xyr[3 * c + k]); hi[k] = fmax(hi[k], xyr[3 * c + k]); }
        double sc = f->metric == 0 || f->metric == 1 ? xt_part_scale(f, lo, hi) : 1;   /* cylinders alone don't span the part */
        for (int c = 0; c < 3 * n; c++) xyr[c] *= sc;
    }
    return n;
}

static double chain_signed_area(const McamFile *f, const Chain *c, double *z) {
    double *xy, A = 0; int n = cam_chain_polyline(f, c, 0.05, &xy, z);
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; A += xy[2 * i] * xy[2 * j + 1] - xy[2 * j] * xy[2 * i + 1]; }
    free(xy); return A / 2;
}

/* the OP_ID record's parameters onto op->p (chains already in op: contour side and face angle look at them); cz is
   the Z of the first chain entity, for incremental heights. Offsets measured with ../tools/oracle.py. */
/* Break Through 25599: mccore set_depths (decomp/mccore_depths.c l.369-403): with the floor stock (op +0x820 = record 0x888;
   Facing 0x66 the constant 1e-308) below tol the depth loses the opcode's amount when its flag is on - record offsets (op +
   0x68): contour types 1 / 0x33-0x35 / 0x82 / 0x85 / 0x87 0x1370 / 0x1378, pocket 3 0x1460 / 0x1468, circle 0x12 0x1228 /
   0x1230, slot 0x69 0x12c8 / 0x12d0, 0x71 0x1358 / 0x1360, 2D HST 0x86 0x13d0 / 0x13d8. Returns the flag's record offset
   (amount at +8), 0 for an opcode without one */
int cam_bt_offset(int opcode) {
    switch (opcode) { case 1: case 0x33: case 0x34: case 0x35: case 0x82: case 0x85: case 0x87: return 0x1370;
        case 3: return 0x1460; case 0x12: return 0x1228; case 0x69: return 0x12c8; case 0x71: return 0x1358; case 0x86: return 0x13d0; }
    return 0;
}
double cam_bt_eff(int opcode, int on, double amount, double fstock) {   /* the amount set_depths subtracts */
    if (opcode == 0x66) fstock = 0;
    return cam_bt_offset(opcode) && on && fstock < CAM_SYS_TOL ? amount : 0;
}
static void params_from_record(McamFile *f, const Op *o, int type, double cz, CamOp *op) {
    const uint8_t *r = o->raw; uint32_t rn = o->raw_n; CamParams *p = &op->p;
    int tnum = rd32(r, rn, 0x348);
#define D(off) rdd(r, rn, off)
#define S16(off) rd16(r, rn, off)
#define B(off) rd8(r, rn, off)
    /* common parameters (P10005 +0x350 tool dia, P10027 +0x2e8 top, P10028 +0x2f0 top incremental, P10029 +0x1a0 depth,
       +0x1a8 depth incremental, P10068 +0x888 floor stock, P10024 +0x190 feed plane (+0x198 incremental), heights
       clearance +0x170 (+0x178 inc, +0x179 on), retract +0x180 (+0x188 inc, +0x189 on), feeds +0x378 / +0x380, rpm +0x390,
       P10010 +0x2e0 wall stock) */
    if (D(0x350) > 0) p->tool_dia = D(0x350);
    p->tool_num = tnum;
    p->tool_slot = rd32(r, rn, 0x5ac);
    double top = D(0x2e8) + (rd32(r, rn, 0x2f0) ? cz : 0), depth = D(0x1a0);
    double fin = B(0x1a8) ? cz + depth : depth;
    { int bo = cam_bt_offset(o->opcode); p->bt_eff = bo ? cam_bt_eff(o->opcode, B(bo), D(bo + 8), D(0x888)) : 0; fin -= p->bt_eff; }   /* set_depths */
    fin += D(0x888);
    p->top_z = top; p->final_z = fin;
    p->dep_inc = B(0x1a8) != 0; p->top_inc = B(0x2f0) != 0; p->dep_cz = cz;
    p->feed_plane = B(0x198) ? D(0x190) : D(0x190) - top;
    p->ht_inc = (B(0x178) ? 1 : 0) | (B(0x188) ? 2 : 0) | (B(0x198) ? 4 : 0);
    double ret = D(0x180) + (B(0x188) ? top : 0), clr = D(0x170) + (B(0x178) ? top : 0);
    if (B(0x189)) p->retract_z = ret;
    p->clearance = B(0x179) ? clr : (B(0x189) ? ret : top + p->feed_plane);
    if (D(0x378) > 0) p->feed = D(0x378);
    if (D(0x380) > 0) p->plunge_feed = D(0x380);
    if (rd32(r, rn, 0x390) > 0) p->rpm = rd32(r, rn, 0x390);
    p->coolant = S16(0x398) == 0x10;   /* regen-consistency 2026-11-19: the op's coolant word (op +0x330, cam_postvals.h PV_COOL_OLD: 8 off, 0x10
                                          flood), which mcam_saveops.c writes from CamParams.coolant; was the tool's (always 0 for a part tool) */
    p->stock_xy = D(0x2e0);
    if (type == CAM_CONTOUR || type == CAM_POCKET || type == CAM_SLOT || type == CAM_CIRCLE) p->stepdown = B(0x868) ? D(0x870) : 0;

    /* leads: HSTLeadIOPageBase load FUN_180171320 / store FUN_1801735c0 (formats/opdlg_port_audit.md 6.1): master on
       0x8f0; entry on 0x8f8, perpendicular 0x8f9, line length abs 0x900 / % 0x930, ramp height 0x908, arc radius abs 0x910 /
       % 0x938, sweep 0x918 (rad), helix height 0x920; exit on 0x940, 0x941, line 0x948 / % 0x978, 0x950, radius 0x958 /
       % 0x980, sweep 0x960, 0x968; overlap 0x988, midpoint 0x990, gouge check 0x991. Each side is its own (batch 4,
       2026-10-23; was one lead for both sides). The absolute values the engines take are recalculated from the % with the
       leads and that side on: bnci_ctour decomp/mcmill_ctour.c l.394-406 (op +0x898 / +0x8a8 = +0x8c8 / +0x8d0 x 0.01 x D
       when op +0x888 and +0x890; +0x8e0 / +0x8f0 from +0x910 / +0x918 when +0x8d8) - this settles the "abs or %" question of
       6.3 (was: % when 0x940, the same numbers in every stored op). The midpoint is the contour's own 0x990 (shown for
       opcode 1 only, FUN_180171320); pocket keeps 1 (its finish entry at the first entity's midpoint, measured) */
#define LEADS(ct) do { p->lead_pct = 0; p->lead_on = B(0x8f0); p->lead_xflags = (B(0x928) ? 1 : 0) | (B(0x929) ? 2 : 0) | (B(0x970) ? 4 : 0) | (B(0x971) ? 8 : 0) | (B(0x992) ? 16 : 0) | (B(0x993) ? 32 : 0) | (B(0x92a) ? 64 : 0) | (B(0x972) ? 128 : 0); p->lead_in_on = B(0x8f8); p->lead_out_on = B(0x940); \
        p->lead_in_perp = B(0x8f9); p->lead_out_perp = B(0x941); p->lead_ovl = D(0x988); p->lead_gouge = B(0x991); \
        p->lead_xlen = D(0x948); p->lead_xr = D(0x958); p->lead_xsw = D(0x960); p->lead_xlen_pct = D(0x978); p->lead_xr_pct = D(0x980); \
        p->lead_sw_rec = B(0x8f0) ? 3 : 2;   /* the sweeps read here: 0 = no arc (cam2d.h) */ \
        p->mc_lead_dz[0] = D(0x908); p->mc_lead_dz[1] = D(0x920); p->mc_lead_dz[2] = D(0x950); p->mc_lead_dz[3] = D(0x968); \
        if (B(0x8f0)) { p->lead_len = D(0x900); p->lead_r = D(0x910); p->lead_sw = D(0x918); p->lead_mid = (ct) ? B(0x990) : 1; \
            p->lead_pct = 1; p->lead_len_pct = D(0x930); p->lead_r_pct = D(0x938); \
            if (B(0x8f8)) { p->lead_len = p->lead_len_pct * 0.01 * p->tool_dia; p->lead_r = p->lead_r_pct * 0.01 * p->tool_dia; } \
            if (B(0x940)) { p->lead_xlen = p->lead_xlen_pct * 0.01 * p->tool_dia; p->lead_xr = p->lead_xr_pct * 0.01 * p->tool_dia; } } \
        else { p->lead_len = 0; p->lead_r = 0; p->lead_mid = 0; } } while (0)
    if (type == CAM_CONTOUR) {
        int comp = S16(0x5d0), left = B(0x5d2) == 0, off = comp == 4;
        p->roll = B(0x5d3);                              /* CamParams.roll: Mastercam's 1335 = u8 0x5d3 (UpdateLocalOp FUN_180157e30) */
        /* batch 5: compensation type 0x5d0 (0..3 while not Off), optimize 0x5d4, corner rounding 0x1450 / break 0x1468, the D word
           0x370; tabs Vertical / Ramp 0x139c and ramp angle 0x13b0 (cam2d.h) */
        p->ct_comp = comp >= 0 && comp <= 3 ? comp : 0; p->ct_ccopt = B(0x5d4); p->ct_crnd = D(0x1450); p->ct_cbrk = D(0x1468);
        p->ct_doff = rd32(r, rn, 0x370); p->tab_ramp = S16(0x139c) != 0; p->tab_ang = D(0x13b0);
        p->ct_nolook = !B(0x1238);
        LEADS(1);
        /* adjust start / end of contour (lead page 3013; ctour_prm = record 0x1228, FUN_180159770): on 0x1320 / 0x1338,
           Extend (1) / Shorten (0) 0x1321 / 0x1339, length 0x1328 / 0x1340 (what FUN_180054420 / FUN_1800566e0 take), % of D
           0x1330 / 0x1348 */
        p->adj_s = B(0x1320); p->adj_s_ext = B(0x1321); p->adj_s_len = D(0x1328); p->adj_s_pct = D(0x1330);
        p->adj_e = B(0x1338); p->adj_e_ext = B(0x1339); p->adj_e_len = D(0x1340); p->adj_e_pct = D(0x1348);
        /* depth cuts: finish cuts S16 0x878 / step 0x880 (calc_depth_cuts), tapered walls 0x130a / angle 0x1310 (rad), order
           0x1318 (1 = By depth), tip comp 0x1231 (the taper's reference, FUN_1800569a0) */
        p->mc_depth_nf = S16(0x878); p->mc_depth_fs = p->mc_depth_nf > 0 ? D(0x880) : 0;   /* the step matters only with finish cuts
                                                                                (0 otherwise: the projects' keys stay as before) */
        p->dc_taper = B(0x130a); p->dc_taper_a = D(0x1310); p->dc_bydepth = B(0x1318); p->ct_tipcomp = B(0x1231);
        p->finish_n = B(0x8a8) ? S16(0x8b8) : 0; p->finish_step = B(0x8a8) ? D(0x8c0) : p->finish_step;
        /* XY multipasses (cam2d.c gen_contour): on 0x8a8, rough count S16 0x8aa / spacing 0x8b0, finish at every
           depth 0x1230 (mcmill contour loop, decomp/mcmill_ctour.c l.1506-1700) */
        p->feed_retract = !B(0x1a9); p->mc_rrate = D(0x388);
        p->mc_mp_kd = B(0x8c8); p->mc_keep_down = B(0x891);
        p->lead_fin_on = B(0x1358); p->lead_fin_v = D(0x1350); p->lead_fout_on = B(0x1368); p->lead_fout_v = D(0x1360);
        p->mp_ff_on = B(0x1464); p->mp_ff_v = D(0x1458); p->mp_fin_rpm = B(0x1465);      /* keep tool down: multipasses / depth cuts (cam2d.c pass, kd_link_ok) */
        p->ct_mp_rec = 1; p->mc_mp_on = B(0x8a8); p->mc_mp_nr = S16(0x8aa); p->mc_mp_rs = D(0x8b0);
        p->mc_mp_nf = S16(0x8b8); p->mc_mp_fs = D(0x8c0); p->ct_fin_all = B(0x1230); p->mc_depth_on = B(0x868);
        p->tabs = 0;
        p->tab_w = D(0x1388); p->tab_h = D(0x1390);           /* tab width / height (P16005 / P16006) */
        /* Tabs 2834 (formats/contour_100_plan.md 2): Automatic -> the tabs are made again at every regeneration (bnci_ctour ->
           mccore add_auto_tabs, cam2d.c ct_auto_tabs); Manual -> the stored tab entities (ntab_xy) */
        p->tab_rec = 1; p->tab_auto = B(0x1381); p->tab_n = S16(0x1382); p->tab_bydist = B(0x1440); p->tab_maxd = D(0x1448);
        p->tab_all = B(0x13f0); p->tab_sx = D(0x13e0); p->tab_sy = D(0x13e8); p->tab_full = B(0x1398); p->tab_fplane = B(0x13b9);
        p->tab_square = B(0x13ba); p->tab_sqpos = B(0x139a); p->tab_ow = B(0x13f1); p->tab_cut = S16(0x13f2); p->tab_cut_ffp = B(0x13f4);
        p->tab_rise_z = B(0x189) ? ret : B(0x179) ? clr : top + p->feed_plane; p->tab_rise_rc = B(0x189) || B(0x179);
        p->ct_rec_tol = D(0x1228);   /* retract_move (Full tabs, cam2d.c cut_path) */
        if (B(0x1380)) p->tabs = p->tab_n > 0 ? p->tab_n : 1;
        if (!B(0x1380) || (p->tab_auto && !p->tab_square)) p->ntab_xy = 0;   /* square point: the stored tabs (make_geo_tabs made them
                                                                                   at Mastercam's last regeneration; not ported - ours) */
        p->ct_cutoff_op = o->opcode == 0x82;
        if (o->opcode == 0x82) p->ct_parent_uid = -rd32(r, rn, 0x138);   /* op +0xd0: the parent's op id (stored: negative here, cui_ops.c cutoff_children) */
        p->contour_type = 0; p->ct_rtype = S16(0x1308);
        p->ct_off_right = B(0x5d2) != 0;                   /* the side FUN_1800550c0 uses whatever 0x5d0 holds (Off included) */
        p->rm_src = S16(0x124a); p->rm_dia = D(0x1250); p->rm_clr_pct = D(0x1258); p->rm_clr = D(0x1260); p->rm_notrim = B(0x1268) != 0; p->rm_tol_pct = D(0x1270); p->rm_cell = D(0x1278); p->rm_display = B(0x1280) != 0;   /* dialog 2999 (MapToDlg FUN_18015bc90) */
        p->ct_rmode = S16(0x12c0); p->ct_rang = D(0x12c8); p->ct_rdep = D(0x12d0); p->ct_r1way = B(0x12d8); p->ct_rlin = B(0x12d9);
        p->ct_rtol = D(0x12e0); p->ct_rfinal = B(0x12e8); p->ct_ovl = D(0x988);
        p->ct_omode = S16(0x1408); p->ct_odist = D(0x1410); p->ct_odep = D(0x1418);
        p->chamfer_w = D(0x1288); p->chamfer_tip = D(0x1290);  /* chamfer width / tip offset (used by type 2) */
        /* 3D chamfer (3) on flat chains is a chamfer as 2D chamfer: FUN_180053480 (decomp/mcmill_ctour.c l.9883) tests type 2 or 3; 3D (1)
           cuts as 2D (the contour loop tests only 4 / 6, l.1191-1199); the 3D types' one other use (FUN_180045770 l.6672) is refused by
           gen_contour where it can act; 3D chains are refused there */
        if (S16(0x1308) == 2 || S16(0x1308) == 3) {           /* 2D chamfer: tip at depth - width - tip offset */
            p->contour_type = 1;
            p->feed_plane += p->top_z - p->final_z; p->top_z = p->final_z;
        }
        /* compensation side per chain from its own direction when the chain records give directions */
        int known = 1;
        for (int i = 0; i < f->ntp; i++) if (f->tp[i].op_id == o->id && f->tp[i].kind == 7 && f->tp[i].rev != 0 && f->tp[i].rev != 1) known = 0;
        if (off) { p->side = SIDE_ON; p->side_rel = known; }
        else if (known) { p->side = left ? 1 : 2; p->side_rel = 1; }
        else if (op->nchains && op->chains[0].closed) { double z; double a = chain_signed_area(f, &op->chains[0], &z); p->side = (a > 0) == left ? 2 : 1; p->side_rel = 0; }
        else { p->side = left ? 1 : 2; p->side_rel = 0; }
    }
    if (type == CAM_POCKET) {
        p->pocket_style = S16(0x123c) == 0 ? 1 : 0;
        p->pocket_angle = D(0x1250) * 180 / M_PI;   /* batch 6: rough_angle = pock_prm +0x28 is radians (process_pocket l.323238, cos / sin
                                                       in Pockzigr); pocket_angle is degrees (cam2d.c gen_pocket_zigzag) */
        p->pocket_no_rough = !B(0x1238);      /* P12045 Roughing enabled (param_reference.tsv; pock_prm +0x10) */
        p->feed_retract = !B(0x1a9); p->mc_rrate = D(0x388);   /* rapid retract off: cam2d.c retract_up */
        /* the pocket engine's parameter block (cam2d_pock.c: pock_prm = record 0x1228) and its other inputs */
        p->pk_ok = rn >= 0x1228 + sizeof p->pk_p;
        if (p->pk_ok) memcpy(p->pk_p, r + 0x1228, sizeof p->pk_p);
        p->pk_roll = B(0x5d3);
        p->ct_ccopt = B(0x5d4);              /* Optimize cutter comp in control (op +0x56c; the pocket finish FUN_1801cc170, batch 6) */
        p->mc_depth_on = B(0x868); p->mc_depth_nf = S16(0x878); p->mc_depth_fs = D(0x880);
        p->mc_isl_depths = B(0x892);
        p->mc_keep_down = B(0x891);          /* Depth Cuts "Keep tool down" (op +0x829): the keep-tool-down link of wr_pock_toolpath (cam2d_pock.c pk_ktd_*) */
        p->mc_ret_on = B(0x189); p->mc_clr_end = B(0x319);
        p->mc_clr_on = B(0x179);
        if (B(0x12a0) && S16(0x12a2) == 0) { p->helix_r = p->tool_dia * D(0x1300) / 100; p->helix_zclr = D(0x12b8); p->ramp_deg = D(0x12c0) * 180 / M_PI; }
        LEADS(0);
        p->stepover_pct = D(0x1248);
        p->finish_n = B(0x1239) ? S16(0x125a) : 0; if (B(0x1239)) p->finish_step = D(0x1260);
    }
    if (type == CAM_DRILL) {
        int cyc = S16(0x1228);
        p->drill_cycle = cyc; p->peck = cyc == 1 || cyc == 2 ? D(0x1230) : 0;
        p->tip_comp = B(0x1268);                              /* u8: HSTDrillTipCompPage::MapToOpEnt 0x180165720 writes one byte */
        p->drill_bt = D(0x1260);                              /* batch 8: breakthrough (op +0x11f8, bnci_drill_depth 0x1800694d0) */
        p->drill_dwell = D(0x1250);                           /* batch 8: dwell (op +0x11e8 = bnci_wr_81's +0x38, decomp mccore) */
        p->r_plane = D(0x190) + (rd32(r, rn, 0x198) ? top : 0);
    }
    if (type == CAM_FACE) {
        p->face_style = B(0x1228); p->stepover_pct = D(0x1240); p->face_along = D(0x1250); p->face_overlap = D(0x1260);
        p->face_angle = D(0x1290);
        if (S16(0x1288) && op->nchains) {                     /* automatic angle: along the longer side of the chains' box */
            double lo[2] = { 1e300, 1e300 }, hi[2] = { -1e300, -1e300 };
            for (int c = 0; c < op->nchains; c++) { double *xy, z; int n = cam_chain_polyline(f, &op->chains[c], 0.5, &xy, &z);
                for (int k = 0; k < n; k++) for (int j = 0; j < 2; j++) { if (xy[2 * k + j] < lo[j]) lo[j] = xy[2 * k + j]; if (xy[2 * k + j] > hi[j]) hi[j] = xy[2 * k + j]; }
                free(xy); }
            p->face_angle = hi[1] - lo[1] > hi[0] - lo[0] ? 90 : 0;
        }
        p->stepdown = B(0x868) ? D(0x870) : 0;
    }
    if (type == CAM_SLOT) {                                   /* Slot Mill rough finish parameters (mcmillres dialog 3020) */
        p->stepover_pct = D(0x1268); p->ramp_deg = D(0x1278);   /* Stepover [%], Plunge angle (deg) */
    }
    if (type == CAM_HELIX) {                                  /* Helixbore Cut parameters (mcmillres dialog 2823) */
        p->helix_pitch = D(0x1250);                           /* Rough pitch */
        p->finish_n = B(0x1278) ? 1 : 0; p->finish_step = D(0x1288);   /* Finish, Finish stepover */
    }
    if (type == CAM_THREAD) {
        /* thread mill's own heights (FUN_1802374d0 / FUN_180236810, decomp/mcmill_all.c l.386306 / l.385946; op +0x11c8..
           = record +0x1230..): top 0x1240, bottom 0x1248, both from the point's Z when S16 0x1250 = 1 (else absolute);
           clearance 0x1230 (absolute), feed plane 0x1238 (from the point's Z with 0x1250, as top) */
        int inc = S16(0x1250) == 1;
        p->top_z = D(0x1240) + (inc ? cz : 0); p->final_z = D(0x1248) + (inc ? cz : 0);
        p->clearance = D(0x1230); p->feed_plane = D(0x1238) - D(0x1240);
        p->thread_pitch = D(0x1258); p->helix_pitch = p->thread_pitch; p->thread_internal = S16(0x1290) == 0;
        p->thread_rh = B(0x12a0) == 0; p->thread_up = B(0x12a2) == 1; p->thread_full = 0; p->thread_major = 0;
        /* radial passes as the dialog shows them: rough + finish + spring passes (FUN_1802374d0) */
        p->thread_passes = (B(0x8a8) ? S16(0x8aa) + S16(0x8b8) : 1) + S16(0x12b0); p->thread_step = B(0x8a8) ? D(0x8c0) / 2 : 0;
    }
    if (type == CAM_DYNAMIC || type == CAM_AREA || type == CAM_DYNCONTOUR || type == CAM_PEEL) {
        /* stepover %: LoadDynPockParm (decomp/mcmill_hst2d_dyn.c l.1612-1624; op +0x16c8 / +0x1518 / +0x1698 / +0x1699 =
           record 0x1730 / 0x1580 / 0x1700 / 0x1701): 0x1730 when 0x1700 and 0x1701 are both set, else 0x1580 */
        p->stepover_pct = B(0x1700) && B(0x1701) ? D(0x1730) : D(0x1580);
        p->dyn_outside = B(0x1567) ? 1 : 0;                  /* 1 = from outside (centre up to R + stock outside) */
        /* LoadDynPockParm (mcmill FUN_1801af380, decomp/mcmill_hst2d_dyn.c; its op pointer = record + 0x68):
           min toolpath radius % (+0x1658), micro lift (+0x16d0), plunge angle deg (+0x1678),
           back feedrate (+0x1248, else the feed), gap size = +0x1600 or, when +0x15cd is set, +0x1608 % of D */
        p->min_rad_pct = D(0x1658); p->micro_lift = D(0x16d0); p->ramp_deg = D(0x1678);
        /* helix radius +0x16c0 not taken: it belongs to Mastercam's entry methods (+0x16bb: helix only / profile / ...),
           which our entry does not model - with it, 10 mm helices for 3 mm tools left material (oracle 2026-10-15) */
        p->back_feed = D(0x1248) > 1e-5 ? D(0x1248) : p->feed;
        p->link_retract = B(0x15cd) ? D(0x1608) * p->tool_dia * 0.01 : D(0x1600);
        /* depth cuts (CHST2DProcess::SetOperationID -> SetParams p5 / p21-p23 = record 0x868 / 0x870 / 0x878 / 0x880,
           decomp/mcmill_hst2d.c l.9200; levels: cam2d.c hst_depths) */
        p->mc_depth_on = B(0x868); p->stepdown = B(0x868) ? D(0x870) : 0;
        p->mc_depth_nf = S16(0x878); p->mc_depth_fs = D(0x880);
    }
    if (type == CAM_AREA) cam_area_macro_params(p, r, rn, mcam_is_metric(f));   /* McHmm macro values */
    if (type == CAM_SLOT || type == CAM_HELIX || type == CAM_CIRCLE || type == CAM_THREAD || type == CAM_FACE) {
        /* mcmill.dll engines (Facing: bnci_face, cam2d_face.c; batch 7) (../formats/mcmill_2d_engines.md §0.2 common inputs; §1.1 / §2 / §3.1 P = record 0x1228) */
        p->mc_ok = rn >= 0x1228 + sizeof p->mc_p;
        if (p->mc_ok) memcpy(p->mc_p, r + 0x1228, sizeof p->mc_p);
        p->mc_comp = S16(0x5d0); p->mc_comp_right = B(0x5d2);
        p->mc_depth_on = B(0x868); p->mc_depth_nf = S16(0x878); p->mc_depth_fs = D(0x880);
        /* tip comp: mccore op_is_tip_comp (decomp/mccore_op_is_tip_comp.c) - circle 0x12 op+0x11d0 = record 0x1238, slot /
           helix 0x69 / 0x6a op+0x290 = 0x2f8 (thread 0x64: the global comp_to_tip, not a record field) */
        p->mc_keep_down = B(0x891); p->mc_lead_on = B(0x8f0); p->mc_tip = type == CAM_CIRCLE ? B(0x1238) : B(0x2f8);
        p->mc_clr_on = B(0x179); p->mc_clr_end = B(0x319);
        p->mc_lead_dz[0] = D(0x908); p->mc_lead_dz[1] = D(0x920); p->mc_lead_dz[2] = D(0x950); p->mc_lead_dz[3] = D(0x968);
        p->mc_mp_on = B(0x8a8); p->mc_mp_nr = S16(0x8aa); p->mc_mp_rs = D(0x8b0); p->mc_mp_nf = S16(0x8b8); p->mc_mp_fs = D(0x8c0);
        p->mc_rrate = D(0x388); p->mc_rapid_ret = B(0x1a9);   /* retract rate (op +0x320), rapid retract (op +0x141) */
        p->mc_passes0 = p->thread_passes; p->mc_step0 = p->thread_step;   /* the dialog's view of the multipasses, as mapped */
    }
#undef LEADS
#undef D
#undef S16
#undef B
}

/* McHmm macro values of an Area Mill record (area_mill.c / ../formats/area_mill.md 2-3) into CamParams am_*: params_from_record,
   and (batch 9) cam2d.c with the op dialog's record patches over the record (cam2d_dyn.c hst_record) */
void cam_area_macro_params(CamParams *p, const uint8_t *r, uint32_t rn, int metric) {
    AreaMillMacro am;
    if (area_mill_macro(r, rn, metric, mc_systol_units(metric)->tol, 0, 0.0, &am)) {
        double us = am.scale;                 /* the macro is in mm: back to part units */
        p->am_ok = 1; p->am_mode = am.mode;
        p->am_min_off = area_mill_param(&am, "MinOffset", 0) / us; p->am_max_off = area_mill_param(&am, "MaxOffset", 0) / us;
        p->am_smoothing = area_mill_param(&am, "ProfileSmoothing", 0) != 0;
        p->am_corner_off_tol = area_mill_param(&am, "ProfileSmoothingCornerOffsetTolerance", 0) / us;
        p->am_ramp_style = (int)area_mill_param(&am, "ProfileRampStyle", -1);
        p->am_ramp_deg = area_mill_param(&am, "MaxRampAngle", 3.0);
        p->am_min_ramp_dia = area_mill_param(&am, "MinProfileRampDiameter", 0) / us;
        p->am_climb = area_mill_param(&am, "LinkClimbMilling", 1) != 0;
    }
}

void cam_rpatch_full(int off) { fprintf(stderr, "record patch 0x%x not kept: the operation already holds %d patches (CAM_NRPATCH)\n", off, CAM_NRPATCH); }


/* an operations sidecar written before the two lead sides (2026-10-23): its ops carry this program's one lead in the
   keys lead_r / lead_len / lead_sw, which the engines applied to both sides when lead_r > 0. An op whose record patches hold
   no exit field (the dialog patches the whole exit side with any lead edit, cui_opdlg.c pf_lead_put) gets that meaning
   back: the exit = the entry, leads on = lead_r > 0, and none of the pieces the one lead did not have (ours) */
static int patch_exit_side(const CamParams *p) {
    for (int k = 0; k < p->nrpatch; k++) { int o = p->rpatch[k].off; if ((o >= 0x940 && o <= 0x987) || o == 0x993) return 1; }
    return 0;
}
void cam_lead_sidecar_compat(CamParams *p) {
    if ((p->type != CAM_CONTOUR && p->type != CAM_POCKET) || patch_exit_side(p)) return;
    p->lead_on = p->lead_r > 0; p->lead_in_on = p->lead_out_on = 1; p->lead_in_perp = p->lead_out_perp = 0;
    p->lead_xlen = p->lead_len; p->lead_xr = p->lead_r; p->lead_xsw = p->lead_sw; p->lead_xlen_pct = p->lead_len_pct; p->lead_xr_pct = p->lead_r_pct;
    for (int d = 0; d < 4; d++) p->mc_lead_dz[d] = 0;
    p->lead_ovl = 0; p->adj_s = p->adj_e = 0;   /* the gouge check stays the defaults op's (contour: on = the one lead's fit) */
}

/* the op dialog's record patches (core_ui cui_opdlg.c pf_apply) back onto the fields the engines take from the record
   that the operations sidecar does not keep as keys (a reloaded op starts from the defaults op, cam_defaults): the
   engine blocks (pocket pk_p = record 0x1228.., slot / helix / circle / thread mc_p = 0x1228..) and the values
   params_from_record derives. Nothing for ops without such patches. */
/* batch 7: the Facing fields params_from_record maps (style 0x1228, stepover % 0x1240, along % 0x1250, across % 0x1260,
   roughing angle 0x1290: radians in the record, CamParams degrees) kept in step with a record edit; the decoded engine
   (cam2d_face.c) reads the record block, the measured one these */
void cam_face_mirror(CamParams *p, int o, int n, const uint8_t *v) {
    double d = 0; int16_t s = 0; if (n == 8) memcpy(&d, v, 8); if (n == 2) memcpy(&s, v, 2);
    if (o == 0x1228 && n == 2) p->face_style = s;
    if (o == 0x1240 && n == 8) p->stepover_pct = d;
    if (o == 0x1250 && n == 8) p->face_along = d;
    if (o == 0x1260 && n == 8) p->face_overlap = d;
    if (o == 0x1290 && n == 8) p->face_angle = d * 180 / M_PI;
}
/* Tabs 2834: the contour's tab fields params_from_record maps (formats/contour_100_plan.md 2) kept in step with a record edit */
/* mccore edit_operation (decomp/mccore_edit_operation.c l.2463-2466, 3021-3046): Contour (opcode 1 / 0x85) - the tab block (op +0x1318,
   0x78 bytes = record 0x1380..0x13f7) compared before / after the edit; only when it changed (and tabs are on, Manual) the tab records
   take the op's values (cam2d.h tab_ow). Ours: the tab fields the dialog sets directly, or a record patch in that range, differ from
   the op's stored record */
int cam_tabprm_edited(const McamFile *f, const CamParams *p) {
    const uint8_t *raw = NULL; uint32_t rn = 0;
    for (int i = 0; f && p->file_op_id > 0 && i < f->nops; i++) if (f->ops[i].id == p->file_op_id) { raw = f->ops[i].raw; rn = f->ops[i].raw_n; }
    if (!raw || rn < 0x13f8) return p->file_op_id > 0;
    double d; int16_t s;
#define TD(o, v) (memcpy(&d, raw + (o), 8), fabs(d - (v)) > 1e-12)
    memcpy(&s, raw + 0x139c, 2);
    if ((raw[0x1380] != 0) != (p->tabs > 0) || raw[0x1381] != (p->tab_auto != 0) || raw[0x1398] != (p->tab_full != 0)
        || (s != 0) != (p->tab_ramp != 0) || raw[0x139a] != p->tab_sqpos || raw[0x13b9] != (p->tab_fplane != 0)
        || raw[0x13ba] != (p->tab_square != 0) || raw[0x13f0] != (p->tab_all != 0) || raw[0x13f1] != (p->tab_ow != 0)
        || TD(0x1388, p->tab_w) || TD(0x1390, p->tab_h) || TD(0x13b0, p->tab_ang) || TD(0x13e0, p->tab_sx) || TD(0x13e8, p->tab_sy)) return 1;
#undef TD
    memcpy(&s, raw + 0x1382, 2); if (s != p->tab_n) return 1;
    for (int k = 0; k < p->nrpatch; k++) {
        int o = p->rpatch[k].off, n = p->rpatch[k].n;
        if (o + n <= 0x1380 || o >= 0x13f8) continue;
        if ((uint32_t)(o + n) > rn || memcmp(raw + o, p->rpatch[k].v, (size_t)n)) return 1;
    }
    return 0;
}
void cam_tab_mirror(CamParams *p, int o, int n, const uint8_t *v) {
    double d = 0; int16_t s = 0; int b = v[0]; if (n == 8) memcpy(&d, v, 8); if (n >= 2) memcpy(&s, v, 2);
    if (o == 0x1381 && n == 1) p->tab_auto = b;
    if (o == 0x1382 && n == 2) { p->tab_n = s; if (p->tabs > 0) p->tabs = s > 0 ? s : 1; }
    if (o == 0x1440 && n == 1) p->tab_bydist = b;
    if (o == 0x1448 && n == 8) p->tab_maxd = d;
    if (o == 0x13f0 && n == 1) p->tab_all = b;
    if (o == 0x13e0 && n == 8) p->tab_sx = d;
    if (o == 0x13e8 && n == 8) p->tab_sy = d;
    if (o == 0x1398 && n == 1) p->tab_full = b;
    if (o == 0x13b9 && n == 1) p->tab_fplane = b;
    if (o == 0x13ba && n == 1) p->tab_square = b;
    if (o == 0x139a && n == 1) p->tab_sqpos = b;
    if (o == 0x13f1 && n == 1) p->tab_ow = b;
    if (o == 0x12c0 && n == 2) p->ct_rmode = s;               /* Ramp contour 2997 (cam2d.c ct_ramp_chain) */
    if (o == 0x12c8 && n == 8) p->ct_rang = d;
    if (o == 0x12d0 && n == 8) p->ct_rdep = d;
    if (o == 0x12d8 && n == 1) p->ct_r1way = b;
    if (o == 0x12d9 && n == 1) p->ct_rlin = b;
    if (o == 0x12e0 && n == 8) p->ct_rtol = d;
    if (o == 0x12e8 && n == 1) p->ct_rfinal = b;
    if (o == 0x1408 && n == 2) p->ct_omode = s;               /* Oscillate 2998 */
    if (o == 0x1410 && n == 8) p->ct_odist = d;
    if (o == 0x1418 && n == 8) p->ct_odep = d;
    if (o == 0x124a && n == 2) p->rm_src = s;                 /* Remachining 2999 */
    if (o == 0x1250 && n == 8) p->rm_dia = d;
    if (o == 0x1258 && n == 8) p->rm_clr_pct = d;
    if (o == 0x1260 && n == 8) p->rm_clr = d;
    if (o == 0x1268 && n == 1) p->rm_notrim = b;
    if (o == 0x1270 && n == 8) p->rm_tol_pct = d;
    if (o == 0x1278 && n == 8) p->rm_cell = d;
    if (o == 0x1280 && n == 1) p->rm_display = b;
    if (o == 0x13f2 && n == 2) p->tab_cut = s;              /* Tab Cutoff 3022 (cam2d.c cut_tab_cutoff) */
    if (o == 0x13f4 && n == 1) p->tab_cut_ffp = b;
    /* 2026-11-04: feed overrides, keep tool down (multi passes / depth cuts) - cam2d.c lead_in / lead_out, gen_contour, kd_link_ok */
    if (o == 0x1358 && n == 1) p->lead_fin_on = b;
    if (o == 0x1350 && n == 8) p->lead_fin_v = d;
    if (o == 0x1368 && n == 1) p->lead_fout_on = b;
    if (o == 0x1360 && n == 8) p->lead_fout_v = d;
    if (o == 0x1464 && n == 1) p->mp_ff_on = b;
    if (o == 0x1458 && n == 8) p->mp_ff_v = d;
    if (o == 0x1465 && n == 1) p->mp_fin_rpm = b;
    if (o == 0x8c8 && n == 1) p->mc_mp_kd = b;
    if (o == 0x891 && n == 1 && p->type == CAM_CONTOUR) p->mc_keep_down = b;
}
void cam_params_from_patches(CamParams *p) {
    int t = p->type, mc = t == CAM_SLOT || t == CAM_HELIX || t == CAM_CIRCLE || t == CAM_THREAD || t == CAM_FACE, mp = 0, lead_pct_patch = 0;
    int bt_on = -1; double bt_amt = -1, bt_fs = -1;            /* Break Through patches (contour: the tab height, cam2d.c cut_path) */
    for (int k = 0; k < p->nrpatch; k++) {
        int o = p->rpatch[k].off, n = p->rpatch[k].n; const uint8_t *v = p->rpatch[k].v;
        double d = 0; int16_t s = 0; if (n == 8) memcpy(&d, v, 8); if (n >= 2) memcpy(&s, v, 2);
        int b = v[0], iv = n == 1 ? b : n == 2 ? s : 0;
        if (t == CAM_POCKET && p->pk_ok && o >= 0x1228 && o + n <= 0x1228 + (int)sizeof p->pk_p) memcpy(p->pk_p + (o - 0x1228), v, n);
        if (n == 1 && o == 0x1a8) p->dep_inc = b != 0;           /* hf_apply's depth / top flags (gen_contour3d) */
        if (n == 1 && o == 0x2f0) p->top_inc = b != 0;
        if (n == 1 && (o == 0x178 || o == 0x188 || o == 0x198)) { int bit = o == 0x178 ? 1 : o == 0x188 ? 2 : 4; p->ht_inc = b ? p->ht_inc | bit : p->ht_inc & ~bit; }   /* hf_apply */
        if (mc && p->mc_ok && o >= 0x1228 && o + n <= 0x1228 + (int)sizeof p->mc_p) memcpy(p->mc_p + (o - 0x1228), v, n);
        if (t == CAM_FACE) cam_face_mirror(p, o, n, v);
        if (t == CAM_POCKET) {                                 /* params_from_record's pocket rules */
            if (o == 0x123c && n == 2) p->pocket_style = s == 0 ? 1 : 0;
            if (o == 0x1250 && n == 8) p->pocket_angle = d * 180 / M_PI;   /* radians in the record (batch 6) */
            if (o == 0x1238 && n == 1) p->pocket_no_rough = !b;
            if (o == 0x5d3 && n == 1) p->pk_roll = b;
            if (o == 0x5d4 && n == 1) p->ct_ccopt = b;
        }
        if (mc) {
            if (o == 0x5d0 && n == 2) p->mc_comp = s;
            if (o == 0x5d2 && n == 1) p->mc_comp_right = b;
            if (o == 0x8f0 && n == 1) p->mc_lead_on = b;
            if (o == 0x891 && n == 1) p->mc_keep_down = b;            /* Depth Cuts "Keep tool down" (slot / circle) */
            if (o == 0x179 && n == 1) p->mc_clr_on = b;               /* Linking: clearance on / only at start and end (hf_apply) */
            if (o == 0x868 && n == 1 && t == CAM_FACE) p->mc_depth_on = b;
            if (o == 0x878 && n == 2 && t == CAM_FACE) p->mc_depth_nf = s;
            if (o == 0x880 && n == 8 && t == CAM_FACE) p->mc_depth_fs = d;
            if (o == 0x319 && n == 1) p->mc_clr_end = b;
            if (o == 0x2f8 && n == 1 && t != CAM_CIRCLE) p->mc_tip = b;
            if (o == 0x1238 && n == 1 && t == CAM_CIRCLE) p->mc_tip = b;
        }
        if (mc || t == CAM_CONTOUR) {
            if (o == 0x8a8 && n == 1) { p->mc_mp_on = b; mp = 1; }
            if (o == 0x8aa && n == 2) { p->mc_mp_nr = iv; mp = 1; }
            if (o == 0x8b0 && n == 8) { p->mc_mp_rs = d; mp = 1; }
            if (o == 0x8b8 && n == 2) { p->mc_mp_nf = iv; mp = 1; }
            if (o == 0x8c0 && n == 8) { p->mc_mp_fs = d; mp = 1; }
            if (o == 0x12b0 && n == 2 && t == CAM_THREAD) mp = 1;
        }
        if (t == CAM_CONTOUR && o == 0x1230 && n == 1) p->ct_fin_all = b;
        if (t == CAM_POCKET && n == 1) {                     /* audit R-U3: the flags the Pocket engines read, for a patched / reloaded op (the Linking page: hf_apply for the same fields) */
            if (o == 0x892) p->mc_isl_depths = b; else if (o == 0x891) p->mc_keep_down = b; else if (o == 0x179) p->mc_clr_on = b; else if (o == 0x189) p->mc_ret_on = b; else if (o == 0x319) p->mc_clr_end = b;
        }
        if (t == CAM_CONTOUR) cam_tab_mirror(p, o, n, v);
        if (t == CAM_DRILL && n == 8 && o == 0x1260) p->drill_bt = d;      /* batch 8: Tip Comp 25594 1338 */
        if (t == CAM_DRILL && n == 8 && o == 0x1250) p->drill_dwell = d;   /* batch 8: Drill 25595 1089 */
        if (t == CAM_CONTOUR) {                               /* batch 5 (cui_opdlg.c): Contour Cut 2938, Tabs 2834 */
            if (o == 0x1238 && n == 1) p->ct_nolook = !b;
            if (o == 0x5d0 && n == 2) p->ct_comp = s >= 0 && s <= 3 ? s : 0;
            if (o == 0x5d2 && n == 1) p->ct_off_right = b != 0;
            if (o == 0x5d4 && n == 1) p->ct_ccopt = b;
            if (o == 0x1231 && n == 1) p->ct_tipcomp = b;
            if (o == 0x1450 && n == 8) p->ct_crnd = d;
            if (o == 0x1468 && n == 8) p->ct_cbrk = d;
            if (o == 0x139c && n == 2) p->tab_ramp = s != 0;
            if (o == 0x13b0 && n == 8) p->tab_ang = d;
            if (o == 0x1370 && n == 1) bt_on = b;
            if (o == 0x1378 && n == 8) bt_amt = d;
            if (o == 0x888 && n == 8) bt_fs = d;
        }
        if (t == CAM_CONTOUR || t == CAM_POCKET) {           /* Lead In/Out 3013 / 2793 and Depth Cuts (batches 3 / 4) */
            if (n == 1) switch (o) {
                case 0x8f0: p->lead_on = b; break; case 0x8f8: p->lead_in_on = b; break; case 0x940: p->lead_out_on = b; break;
                case 0x8f9: p->lead_in_perp = b; break; case 0x941: p->lead_out_perp = b; break;
                case 0x990: if (t == CAM_CONTOUR) p->lead_mid = b; break;
                case 0x991: p->lead_gouge = b; break; case 0x868: p->mc_depth_on = b; break;
                case 0x928: p->lead_xflags = (p->lead_xflags & ~1) | (b ? 1 : 0); break; case 0x929: p->lead_xflags = (p->lead_xflags & ~2) | (b ? 2 : 0); break;
                case 0x970: p->lead_xflags = (p->lead_xflags & ~4) | (b ? 4 : 0); break; case 0x971: p->lead_xflags = (p->lead_xflags & ~8) | (b ? 8 : 0); break;
                case 0x92a: p->lead_xflags = (p->lead_xflags & ~64) | (b ? 64 : 0); break; case 0x972: p->lead_xflags = (p->lead_xflags & ~128) | (b ? 128 : 0); break;
                case 0x992: p->lead_xflags = (p->lead_xflags & ~16) | (b ? 16 : 0); break; case 0x993: p->lead_xflags = (p->lead_xflags & ~32) | (b ? 32 : 0); break;
            }
            if (n == 8) switch (o) {
                case 0x900: p->lead_len = d; break; case 0x910: p->lead_r = d; break; case 0x918: p->lead_sw = d; p->lead_sw_rec |= 1; break;
                case 0x930: p->lead_len_pct = d; lead_pct_patch = 1; break; case 0x938: p->lead_r_pct = d; lead_pct_patch = 1; break;
                case 0x948: p->lead_xlen = d; break; case 0x958: p->lead_xr = d; break; case 0x960: p->lead_xsw = d; p->lead_sw_rec |= 2; break;
                case 0x978: p->lead_xlen_pct = d; lead_pct_patch = 1; break; case 0x980: p->lead_xr_pct = d; lead_pct_patch = 1; break;
                case 0x908: p->mc_lead_dz[0] = d; break; case 0x920: p->mc_lead_dz[1] = d; break;
                case 0x950: p->mc_lead_dz[2] = d; break; case 0x968: p->mc_lead_dz[3] = d; break;
                case 0x988: p->lead_ovl = d; break; case 0x880: p->mc_depth_fs = d; break;
            }
            if (o == 0x878 && n == 2) p->mc_depth_nf = s;
        }
        if (t == CAM_CONTOUR) {
            if (n == 1) switch (o) {
                case 0x1320: p->adj_s = b; break; case 0x1321: p->adj_s_ext = b; break; case 0x1338: p->adj_e = b; break; case 0x1339: p->adj_e_ext = b; break;
                case 0x130a: p->dc_taper = b; break; case 0x1318: p->dc_bydepth = b; break;
            }
            if (n == 8) switch (o) {
                case 0x1328: p->adj_s_len = d; break; case 0x1330: p->adj_s_pct = d; break; case 0x1340: p->adj_e_len = d; break; case 0x1348: p->adj_e_pct = d; break;
                case 0x1310: p->dc_taper_a = d; break;
            }
        }
    }
    /* the break through the sidecar's bottom (final_z key) already holds: back as bt_eff (floor stock: its patch, else the
       contour default ops' 0 - Mill_MM / Mill_Inch 0x888 = 0) */
    if (t == CAM_CONTOUR && (bt_on >= 0 || bt_amt >= 0)) {
        if (bt_on < 0) bt_on = 0; if (bt_amt < 0) bt_amt = 0;
        p->bt_eff = cam_bt_eff(1, bt_on, bt_amt, bt_fs < 0 ? 0 : bt_fs);
    }
    /* thread: the dialog's radial passes (a sidecar key) were set from these fields, so the engine takes the record's */
    if (t == CAM_THREAD && mp) { p->mc_passes0 = p->thread_passes; p->mc_step0 = p->thread_step; }
    /* the lead lengths / radii from their % of the op's tool (bnci_ctour decomp/mcmill_ctour.c l.394-406 recalculates them at
       generation, as LEADS does for an op read from a record): the patches hold the distances of the tool the lead was edited
       with, a later tool change (cam_params_from_tool) is in the sidecar's tool keys - review 2026-10-23: a reopened sidecar
       cut the old tool's leads, the reopened export the new ones */
    if (lead_pct_patch && (t == CAM_CONTOUR || t == CAM_POCKET)) { p->lead_pct = 1; cam_lead_recalc(p); }
}

/* srough: an op record that is not a stored op of the file (cam3d_srough.c: op 10's pocket block moved down to the 2D Pocket's place)
   through params_from_record */
void cam_params_from_raw(McamFile *f, const uint8_t *r, uint32_t n, int type, CamOp *op) {
    Op o; memset(&o, 0, sizeof o); o.raw = (uint8_t *)r; o.raw_n = n; params_from_record(f, &o, type, 0, op);
}
/* B2 (formats/new_toolpath_audit.md): a stored op of the 3D ports' codes -> its port type (the map of cam_cli saveref, S6 of
   formats/surface_selection.md: op 132 by its style byte 0x16bc - 1 OptiRough, cam_hst_style for the others - and the classic
   surface codes); 0 = none */
int cam_fileop_type3d(const Op *o) {
    if (!o->raw) return 0;
    int st = o->opcode == 132 && o->raw_n > 0x16bc ? o->raw[0x16bc] : -1;
    if (o->opcode == 132) { if (st == 1) return CAM_OPTIROUGH; for (int t = 1; t < CAM_NTYPES; t++) if (t != CAM_OPTIROUGH && cam_hst_style(t) >= 0 && cam_hst_style(t) == st) return t; return 0; }
    static const int oc3[][2] = { { 5, CAM_SRPARALLEL }, { 7, CAM_SRPROJECT }, { 10, CAM_SRPOCKET }, { 11, CAM_SFPARALLEL }, { 12, CAM_SFRADIAL },
                                  { 13, CAM_SFPROJECT }, { 15, CAM_SFCONTOUR }, { 39, CAM_SFPENCIL }, { 44, CAM_SRPLUNGE }, { 109, CAM_BLEND }, { 131, CAM_SRMPOCKET } };
    for (size_t k = 0; k < sizeof oc3 / sizeof oc3[0]; k++) if (o->opcode == oc3[k][0]) return oc3[k][1];
    return 0;
}
/* a stored 3D op as an op of its port type, as cam_cli saveref builds it: defaults, the op's tool, the parameters from its record
   (cam_params_from_raw), its planes, its containment / curve chains (TP kind 6 / 7, roles 2 / 3); its drive / check references stay
   the TP kind 4 records the engines read for a stored op (p.file_op_id, cam_refs_from_tp) */
static int op_from_file_3d(McamFile *f, const Op *o, int op_index, int type, CamOp *op, char *err, size_t errn) {
    cam_defaults_base(&op->p, type, f);
    { const Tool *tl = op_tool(f, o, rd32(o->raw, o->raw_n, 0x348)); if (tl) cam_params_from_tool(&op->p, tl); }
    cam_params_from_raw(f, o->raw, o->raw_n, type, op);
    op->p.type = type; op->p.file_op_id = o->id;
    snprintf(op->name, sizeof op->name, "%s", o->comment);   /* op +6, none = none (L-5) */
    { char why[200] = ""; if (!cam_planes_from_record(o->raw, o->raw_n, &op->p, why, sizeof why)) { snprintf(err, errn, "operation %d: %s; it stays as stored", op_index + 1, why); return 0; } }
    int32_t *ids = xmalloc(sizeof(int32_t) * (size_t)(f->ntp + 1)); int *rv = xmalloc(sizeof(int) * (size_t)(f->ntp + 1)); int nid = 0, role = 0, lost = 0;
    for (int k = 0; k <= f->ntp; k++) {
        const TpRef *t = k < f->ntp ? &f->tp[k] : NULL;
        if (t && t->op_id != o->id) continue;
        if (!t || t->kind == 6) {
            if (nid) { Chain c; if (cam_chain_from_idns(f, ids, rv, nid, &c)) { c.role = role; op->chains = xrealloc(op->chains, sizeof(Chain) * (size_t)(op->nchains + 1)); op->chains[op->nchains++] = c; } else lost++; }
            nid = 0; if (!t) break; role = t->role; continue;
        }
        if (t->kind == 7) { ids[nid] = t->ent; rv[nid++] = t->rev; }
        if (type == CAM_SRPLUNGE && t->kind == 1) {          /* Rough Plunge: the grid corners = the op's point records (surf_classic2.md 2.9) */
            Hole h = { 0 }; h.x = t->xyz[0]; h.y = t->xyz[1]; op->holes = xrealloc(op->holes, sizeof(Hole) * (size_t)(op->nholes + 1)); op->holes[op->nholes++] = h; }
    }
    free(ids); free(rv);
    if (lost) { for (int c = 0; c < op->nchains; c++) cam_chain_free(&op->chains[c]); free(op->chains); op->chains = NULL; op->nchains = 0;
        snprintf(err, errn, "operation %d: %d chain(s) cannot be rebuilt here (missing geometry); it stays as stored", op_index + 1, lost); return 0; }
    return 1;
}
int cam_op_from_file(McamFile *f, int op_index, CamOp *op, char *err, size_t errn) {
    memset(op, 0, sizeof *op);
    if (op_index < 0 || op_index >= f->nops) { snprintf(err, errn, "no such operation"); return 0; }
    const Op *o = &f->ops[op_index];
    const uint8_t *r = o->raw; uint32_t rn = o->raw_n;
    int type = cam_fileop_type(o);
    if (!r) { snprintf(err, errn, "operation %d has no parameter record", op_index + 1); return 0; }
    /* Op 139 Stock Model: mccore CStockModel::Regen -> mckernel TPStockModel::Compute -> tpiftools ComputeMCSweep -> ifsweep.dll + the Interfacial
       DLLs (third party, 16.8 MB, not ported); op 107 Restmill: its rest source is that model, the earlier ops' temp files, or a roughing
       tool / STL model (mcmsurf FUN_18013b2f0 / FUN_1802d9780; formats/stock_model.md 3, 5). */
    if (o->opcode == 0x8b) { snprintf(err, errn, "%s: the Stock Model sweep (ifsweep.dll, formats/stock_model.md) is not ported; the operation stays as stored", o->type); return 0; }
    if (o->opcode == 0x6b) { snprintf(err, errn, "%s: the rest material engine (formats/stock_model.md) is not ported; the operation stays as stored", o->type); return 0; }
    if (!type && cam_fileop_type3d(o)) return op_from_file_3d(f, o, op_index, cam_fileop_type3d(o), op, err, errn);   /* B2: the 3D ports */
    if (!type) { snprintf(err, errn, "%s operations can't be regenerated here yet", o->type[0] ? o->type : "this kind of"); return 0; }
    if (rn < 0x1600) { snprintf(err, errn, "operation record too short (0x%x bytes)", rn); return 0; }
    if (type == CAM_LFACE) {   /* Lathe Face: no chains (its two face points are read by gen_lathe_face), no mill parameters; the
                                  tool is the op's own ltool_rec (op +0x4b8, ent 0x520): nose radius +8 -> tool_dia 2 r (cam_generate's
                                  checks); feed op +0x310, rpm op +0x328; the tool plane = system plane 5 (its rows; the record's plane
                                  id is not a system one - lathe_face.c checks the matrix) so the moves map to the NCI's (X, 0, Z) */
        CamParams *p = &op->p;
        cam_defaults_base(p, type, f);
        p->tool_num = rd32(r, rn, 0x348); p->tool_dia = 2 * rdd(r, rn, 0x520 + 8); p->tool_type = 0;
        p->feed = rdd(r, rn, 0x378); p->rpm = rd32(r, rn, 0x390);
        p->file_op_id = o->id; p->tplane = 5; p->cplane = 0; p->wcs = 0; p->tplane_rec_id = rd16(r, rn, 0x5d8 + 0x68);
        snprintf(op->name, sizeof op->name, "%s", o->comment);   /* the stored op's own comment (op +6), none = none: decision L-5 */
        return 1;
    }
#define D(off) rdd(r, rn, off)
#define S16(off) rd16(r, rn, off)
#define B(off) rd8(r, rn, off)
    CamParams *p = &op->p;
    cam_defaults_base(p, type, f);
    int tnum = rd32(r, rn, 0x348);
    { const Tool *tl = op_tool(f, o, tnum); if (tl) cam_params_from_tool(p, tl); }
    p->file_op_id = o->id;
    snprintf(op->name, sizeof op->name, "%s", o->comment);   /* the stored op's own comment (op +6), none = none: decision L-5 */

    /* chains (kind 6 header starts one; kind 7 entities), roles, tabs (kind 30), points (kinds 1 / 27) */
    int32_t *ids = xmalloc(sizeof(int32_t) * (f->ntp + 1)); int *revs = xmalloc(sizeof(int) * (f->ntp + 1)); int nid = 0, role = 0;
    Chain chains[256]; int roles[256], cnum[256], nch = 0, chnum = 0;
    int hole_type = cam_type_uses_holes(type);
    double cyl[3 * 512]; int ncyl = -1; double hole_z = 0; int have_hz = 0;
    int lost_ch = 0, lost_tab = 0;                           /* nothing is dropped quietly: such an op stays as stored */
    for (int i = 0; i <= f->ntp; i++) {
        const TpRef *t = i < f->ntp ? &f->tp[i] : NULL;
        if (t && t->op_id != o->id) continue;
        if (!t || t->kind == 6) {                             /* close the previous chain */
            if (nid) { if (nch < 256 && cam_chain_from_idns(f, ids, revs, nid, &chains[nch])) { cnum[nch] = chnum; roles[nch++] = role; } else lost_ch++; }
            nid = 0;
            if (!t) break;
            role = t->role; chnum = t->chain; continue;
        }
        if (t->kind == 30) {                                  /* tab: entity + parameter t on it */
            int k = ent_by_idn(f, t->ent); if (k < 0 || p->ntab_xy >= CAM_TAB_MAX) { lost_tab++; continue; }
            const Ent *e = &f->ents[k]; double u = t->t;
            if (e->type == E_LINE) { p->tab_xy[2 * p->ntab_xy] = e->a.x + (e->b.x - e->a.x) * u; p->tab_xy[2 * p->ntab_xy + 1] = e->a.y + (e->b.y - e->a.y) * u; }
            else if (e->type == E_ARC) { double a = e->a0 + e->sw * u; p->tab_xy[2 * p->ntab_xy] = e->c.x + e->r * cos(a); p->tab_xy[2 * p->ntab_xy + 1] = e->c.y + e->r * sin(a); }
            else { lost_tab++; continue; }
            p->tab_ed[p->ntab_xy] = 1 | (t->tab_ed ? 2 : 0); double *ow = &p->tab_own[5 * p->ntab_xy];   /* the tab's own values (cam2d.h tab_ow) */
            ow[0] = t->tab_w; ow[1] = t->tab_h; ow[2] = t->tab_ang; ow[3] = t->tab_full; ow[4] = t->tab_ramp;
            p->ntab_xy++; continue;
        }
        if (hole_type && (t->kind == 1 || t->kind == 27)) {   /* holes in Mastercam's order */
            Hole h = { 0 }; int ok = 0, k = ent_by_idn(f, t->ent);
            if (t->kind == 1 && k >= 0 && f->ents[k].type == E_ARC) {
                const Ent *a = &f->ents[k];
                h.x = a->c.x; h.y = a->c.y; h.d = 2 * a->r; ok = 1;
                /* the point record's own xyz (+0x98) is the drilled position: an arc picked by a point on it (e.g. its
                   quadrant) is drilled there, not at its centre (mc-test01 op 4: NCI G81 at (-1.6, 1.85) for a circle
                   centred (-1.6, 1.6), r 0.25). Used when it lies at the centre or on the circle (older records may
                   carry zeros there). */
                double dc = hypot(t->xyz[0] - a->c.x, t->xyz[1] - a->c.y);
                if (dc < 1e-6 || fabs(dc - a->r) < 1e-6) { h.x = t->xyz[0]; h.y = t->xyz[1]; }
            }
            else if (t->kind == 1 && k >= 0 && f->ents[k].type == E_POINT) { h.x = f->ents[k].a.x; h.y = f->ents[k].a.y; ok = 1; }
            else if (t->kind == 27) {                         /* picked on the solid: the smallest coaxial cylinder */
                if (ncyl < 0) ncyl = part_cylinders(f, cyl, 512);
                double best = 1e300;
                for (int c = 0; c < ncyl; c++) if (hypot(cyl[3 * c] - t->xyz[0], cyl[3 * c + 1] - t->xyz[1]) < 1e-3 && cyl[3 * c + 2] < best) best = cyl[3 * c + 2];
                h.x = t->xyz[0]; h.y = t->xyz[1]; h.d = best < 1e300 ? 2 * best : 0; ok = 1;
            } else if (t->kind == 1) { h.x = t->xyz[0]; h.y = t->xyz[1]; ok = 1; }
            /* Thread Mill: bnci_threadmill (decomp/mcmill_all.c l.385180-385213) takes the diameter of an arc (or of the solid's
               point data, kind 27) only when the point's mode byte +0xb0 is 0x43 ('C', its centre); every other point uses the
               op's diameter rec 0x1298 (gen_thread: d = 0) */
            if (type == CAM_THREAD && t->pmode != 0x43) h.d = 0;
            if (ok && !have_hz) { have_hz = 1; hole_z = k >= 0 ? (f->ents[k].type == E_ARC ? f->ents[k].c.z : f->ents[k].a.z) : t->xyz[2]; }
            if (ok) { op->holes = xrealloc(op->holes, (op->nholes + 1) * sizeof(Hole)); op->holes[op->nholes++] = h; }
            continue;
        }
        if (t->kind == 7 || t->kind == 1) { ids[nid] = t->ent; revs[nid] = t->kind == 7 && (t->rev == 0 || t->rev == 1) ? t->rev : -1; nid++; }
    }
    free(ids); free(revs);
    if (lost_ch || lost_tab) {
        for (int c = 0; c < nch; c++) cam_chain_free(&chains[c]);
        free(op->holes); op->holes = NULL; op->nholes = 0;
        snprintf(err, errn, "operation %d: %d chain(s) / %d tab(s) cannot be rebuilt here (limits 256 / 16, or missing geometry); it stays as stored",
                 op_index + 1, lost_ch, lost_tab);
        return 0;
    }
    if (!hole_type) {
        /* the chains in chain-number order (header +0x98): mccore tp_get_chains walks the op's association list, which runs by
           chain number, not in the records' file order - Mastercam's NCI cuts them that way (24 of the user parts' 93 multi-chain
           ops store them out of order; their NCI follows the numbers in 16, the file order in 2 - formats/contour_100_plan.md 9) */
        for (int a = 1; a < nch; a++) for (int b2 = a; b2 > 0 && cnum[b2] < cnum[b2 - 1]; b2--) {
            Chain tc = chains[b2]; chains[b2] = chains[b2 - 1]; chains[b2 - 1] = tc;
            int tr = roles[b2]; roles[b2] = roles[b2 - 1]; roles[b2 - 1] = tr; int tn = cnum[b2]; cnum[b2] = cnum[b2 - 1]; cnum[b2 - 1] = tn; }
        op->chains = xcalloc(nch ? nch : 1, sizeof(Chain));
        int dyn = type == CAM_DYNAMIC || type == CAM_AREA || type == CAM_DYNCONTOUR || type == CAM_PEEL;
        for (int c = 0; c < nch; c++) { chains[c].avoid = dyn && roles[c] == 4; chains[c].role = dyn ? roles[c] : 0; op->chains[op->nchains++] = chains[c]; }
        if (!op->nchains && type != CAM_FACE) { snprintf(err, errn, "operation %d: no chains found", op_index + 1); free(op->chains); op->chains = NULL; return 0; }
    } else for (int c = 0; c < nch; c++) cam_chain_free(&chains[c]);

    /* the op's planes (op_view x3, cam2d.c cam_planes_from_record: system planes only, by id + matrix + origin) */
    {
        char why[200];
        if (!cam_planes_from_record(r, rn, p, why, sizeof why)) {
            for (int c = 0; c < op->nchains; c++) cam_chain_free(&op->chains[c]);
            free(op->chains); op->chains = NULL; op->nchains = 0; free(op->holes); op->holes = NULL; op->nholes = 0;
            snprintf(err, errn, "operation %d: %s; it stays as stored", op_index + 1, why);
            return 0;
        }
    }
    /* chain Z: the first entity's start (or centre), in op coordinates (get_chain_depth_op: wpt_to_op) */
    double cz = hole_z;
    if (op->nchains) { const Ent *e0 = &f->ents[op->chains[0].ent[0]]; cz = e0->type == E_ARC ? e0->c.z : e0->a.z;
        if (!cam_op_top(p)) { const V3 *q = e0->type == E_ARC ? &e0->c : &e0->a; double w[3] = { q->x, q->y, q->z }, v[3]; cam_plane_w2v(p->tplane, p->torg, w, v); cz = v[2]; } }
    else if (op->nholes && !cam_op_top(p)) { double w[3] = { op->holes[0].x, op->holes[0].y, hole_z }, v[3]; cam_plane_w2v(p->tplane, p->torg, w, v); cz = v[2]; }

    params_from_record(f, o, type, cz, op);
    /* 2D High Speed tapered walls: SetParams keeps process +0x98 = record 0x136a only with depth cuts (0x868) and a
       taper angle (0x1370 / 0x1738, SetParams p19 / p20) not both below tol (decomp/mcmill_hst2d.c l.9307-9313); the per-depth offsets
       (ComputeOffsetsAtDepth 0x18029f010 + FUN_180050910, then the CHST2DBase vtable +0x38 term and FUN_1801ac7e0) are
       NOT DECODED, so such an op is not regenerated (it stays as stored) */
    if ((type == CAM_DYNAMIC || type == CAM_AREA || type == CAM_DYNCONTOUR || type == CAM_PEEL) && B(0x868) && B(0x136a) &&
        !(D(0x1370) < 5e-5 && D(0x1738) < 5e-5)) {
        for (int c = 0; c < op->nchains; c++) cam_chain_free(&op->chains[c]);
        free(op->chains); op->chains = NULL; op->nchains = 0;
        snprintf(err, errn, "operation %d: tapered walls with depth cuts are not decoded here; it stays as stored", op_index + 1);
        return 0;
    }
#undef D
#undef S16
#undef B
    return 1;
}

/* Mastercam's operation defaults: the first operation of this type in the shared Mill_MM / Mill_Inch.mcam-defaults
   (the file Mastercam fills a new operation's parameters from), mapped like a stored op. The tool stays the caller's
   (p->tool_*); rpm / feeds come from the caller's tool when it has them, else from the default operation. Returns 1 if
   a default operation was found. */
static McamFile cdf[2]; static int cdf_tried[2], cdf_ok[2];
/* ours: the Qt op dialog generates in a worker thread (viewer-qt opdlg.cpp runJob) while the main thread repaints the dialog; both
   reach this file - loaded once under the lock (a second caller used to see "tried" before the load finished: no default op) */
static pthread_mutex_t cdf_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t *msp_rec[2]; static uint32_t msp_n[2];   /* Multisurface Pocket's default record, un-shifted (cam_default_op_record) */
void cam_defaults_reset(void) {   /* Q19 (ours): the defaults file was saved - read again */
    pthread_mutex_lock(&cdf_lock);
    for (int k = 0; k < 2; k++) { if (cdf_ok[k]) mcam_free(&cdf[k]); cdf_tried[k] = cdf_ok[k] = 0; free(msp_rec[k]); msp_rec[k] = NULL; msp_n[k] = 0; }
    pthread_mutex_unlock(&cdf_lock);
}
static McamFile *defaults_file(int metric) {
    McamFile *df = cdf; int *tried = cdf_tried, *ok = cdf_ok;
    int k = metric == 0 ? 1 : 0;
    pthread_mutex_lock(&cdf_lock);
    if (!tried[k]) { char path[1100], rd[1100]; site_file(path, sizeof path, "shared", k ? "mill/Ops/Mill_Inch.mcam-defaults" : "mill/Ops/Mill_MM.mcam-defaults");
        ok[k] = mcam_load(&df[k], defaults_read(path, rd, sizeof rd)); tried[k] = 1; }   /* Q19: the program's saved copy when there is one (paths.h) */
    McamFile *r = ok[k] ? &df[k] : NULL;
    pthread_mutex_unlock(&cdf_lock);
    return r;
}
static const Op *default_op(McamFile *f, int type) {
    /* 3drough: OptiRough = the first opcode 132 op with style byte 0x16bc = 1 (msurf_hmm_opti_core_toolbar sets op +0x1654 = 1,
       decomp/mcmsurf_all.c l.211780; Mill_MM / Mill_Inch hold three such ops, 64-66 / 66-68: which one init_operation_surface
       takes is NOT DECODED - the first, as for every other type here) */
    if (type == CAM_OPTIROUGH) { for (int i = 0; f && i < f->nops; i++) { const Op *o = &f->ops[i];
        if (o->opcode == 132 && o->raw && o->raw_n > 0x1760 && o->raw[0x16bc] == 1) return o; } return NULL; }
    /* srough: Surface Rough Pocket / Parallel = the first opcode 10 / 5 op (Mill_MM / Mill_Inch hold one each, the msurf_assoc(10 / 5)
       defaults; stored ops of these codes keep cam_fileop_type 0 = not regenerated) */
    if (cam_hst_style(type) >= 0) { int st = cam_hst_style(type); for (int i = 0; f && i < f->nops; i++) { const Op *o = &f->ops[i];   /* the first op 132 with style 0x16bc = cam_hst_style */
        if (o->opcode == 132 && o->raw && o->raw_n > 0x1760 && o->raw[0x16bc] == st) return o; } return NULL; }
    if (type == CAM_SRPOCKET || type == CAM_SRPARALLEL || type == CAM_SFPARALLEL || type == CAM_SFCONTOUR || type == CAM_SRPROJECT || type == CAM_BLEND || type == CAM_SRMPOCKET || type == CAM_SRPLUNGE || type == CAM_SFRADIAL || type == CAM_SFPROJECT || type == CAM_SFPENCIL) { int oc = type == CAM_SFPENCIL ? 39 : type == CAM_SFPROJECT ? 13 : type == CAM_SRPOCKET ? 10 : type == CAM_SRPLUNGE ? 44 : type == CAM_SFRADIAL ? 12 : type == CAM_SFPARALLEL ? 11 : type == CAM_SFCONTOUR ? 15 : type == CAM_SRPROJECT ? 7 : type == CAM_BLEND ? 109 : type == CAM_SRMPOCKET ? 131 : 5;
        for (int i = 0; f && i < f->nops; i++) { const Op *o = &f->ops[i]; if (o->opcode == oc && o->raw && o->raw_n >= 0x1700) return o; } return NULL; }
    for (int i = 0; f && i < f->nops; i++) {
        const Op *o = &f->ops[i];
        if (cam_fileop_type(o) == type && o->raw && o->raw_n >= 0x1600) return o;
    }
    return NULL;
}
const uint8_t *cam_default_op_record(int type, int metric, uint32_t *n) {
    const Op *o = default_op(defaults_file(metric), type);
    if (!o) return NULL;
    if (type == CAM_SRMPOCKET) {
        /* Multisurface Pocket (op 131): the shipped records hold op 10's pocket block 8 bytes lower than Mastercam's code reads it
           (formats/surf_classic2.md 1.4, measured in Mill_MM and Mill_Inch: 0x1300-0x165f; below 0x1320 the values are op 10's own
           layout). User decision T-1 (formats/user_decisions.md): the block from 0x13c8 (the shift starts in the all-zero 0x1320-0x13c7)
           moved up by 8 - the file's own values at the offsets the code reads (ours). Then the 1312 page's init forcing (FUN_1800f1170
           l.155015-155021, surf_classic2.md 1.2): finish passes 0x147a = 1, quick zigzag 0x1420 = 0, thin wall 0x1656 = 0, spring passes
           0x1640 = 0, minimise burial 0x1479 = 1, rough 0x1458 = 1, finish spacing 0x1480 = 0 */
        int k = metric == 0 ? 1 : 0;
        pthread_mutex_lock(&cdf_lock);
        if (!msp_rec[k] && o->raw_n >= 0x1670) {
            uint8_t *r = malloc(o->raw_n); memcpy(r, o->raw, o->raw_n);
            memmove(r + 0x13c8 + 8, o->raw + 0x13c8, 0x1660 - 0x13c8); memset(r + 0x13c8, 0, 8);
            int16_t one = 1, zero16 = 0; double zd = 0;
            memcpy(r + 0x147a, &one, 2); r[0x1420] = 0; r[0x1656] = 0; memcpy(r + 0x1640, &zero16, 2); r[0x1479] = 1; r[0x1458] = 1; memcpy(r + 0x1480, &zd, 8);
            msp_rec[k] = r; msp_n[k] = o->raw_n;
        }
        const uint8_t *r = msp_rec[k]; *n = msp_n[k];
        pthread_mutex_unlock(&cdf_lock);
        return r;
    }
    *n = o->raw_n; return o->raw;
}
/* lathe defaults (cam_fileop.h cam_lathe_default_record) - loaded once per unit under cdf_lock */
static McamFile ldf[2]; static int ldf_tried[2], ldf_ok[2]; static char ldf_path[1100];
void cam_lathe_defaults_set(const char *path) {
    pthread_mutex_lock(&cdf_lock);
    snprintf(ldf_path, sizeof ldf_path, "%s", path ? path : "");
    for (int k = 0; k < 2; k++) { if (ldf_ok[k]) mcam_free(&ldf[k]); ldf_tried[k] = ldf_ok[k] = 0; }
    pthread_mutex_unlock(&cdf_lock);
}
const uint8_t *cam_lathe_default_record(int opcode, int metric, uint32_t *n) {
    int k = metric == 0 ? 1 : 0; const uint8_t *r = NULL;
    pthread_mutex_lock(&cdf_lock);
    if (!ldf_tried[k]) {
        char path[1100], rd[1100];
        if (ldf_path[0]) snprintf(path, sizeof path, "%s", ldf_path);
        else site_file(path, sizeof path, "shared", k ? "lathe/Ops/LATHE_INCH.mcam-defaults" : "lathe/Ops/LATHE_MM.mcam-defaults");
        ldf_ok[k] = mcam_load(&ldf[k], ldf_path[0] ? path : defaults_read(path, rd, sizeof rd)); ldf_tried[k] = 1;
    }
    for (int i = 0; ldf_ok[k] && i < ldf[k].nops && !r; i++) {
        const Op *o = &ldf[k].ops[i];
        if (o->opcode == opcode && o->raw) { r = o->raw; *n = o->raw_n; }
    }
    pthread_mutex_unlock(&cdf_lock);
    return r;
}
int cam_defaults_from_file(CamParams *p, int type, int metric, const Tool *tool) {
    McamFile *f = defaults_file(metric);
    if (!f) return 0;
    {
        const Op *o = default_op(f, type);
        if (!o) return 0;
        CamOp tmp; memset(&tmp, 0, sizeof tmp); tmp.p = *p;
        CamParams keep = *p;
        int tnum = rd32(o->raw, o->raw_n, 0x348);
        { const Tool *tl = op_tool(f, o, tnum); if (tl) cam_params_from_tool(&tmp.p, tl); }
        params_from_record(f, o, type, 0, &tmp);
        *p = tmp.p;
        p->tool_num = keep.tool_num; p->tool_dia = keep.tool_dia; p->tool_type = keep.tool_type; p->corner_r = keep.corner_r;
        cam_lead_recalc(p);                                   /* Recalc (bnci_ctour l.394-406: each side that is on) */
        /* regen-consistency 2026-11-19: the 2D High Speed gap size as % of D (0x15cd, params_from_record) with the op's tool, as it reads
           back from the saved op - it was the default op's tool's D (a new Dynamic op linked with another gap than its saved copy) */
        if ((type == CAM_DYNAMIC || type == CAM_AREA || type == CAM_DYNCONTOUR || type == CAM_PEEL) && o->raw_n > 0x1610 && o->raw[0x15cd]) {
            double pct; memcpy(&pct, o->raw + 0x1608, 8); p->link_retract = pct * p->tool_dia * 0.01; }
        p->tip_angle = keep.tip_angle; p->tip_dia = keep.tip_dia; p->flute_len = keep.flute_len; p->coolant = keep.coolant;
        if (tool) { if (tool->rpm > 0) p->rpm = tool->rpm; if (tool->feed > 0) p->feed = tool->feed; if (tool->plunge > 0) p->plunge_feed = tool->plunge;
                    if (tool->pitch > 0) p->thread_pitch = tool->pitch;
                    if (tool->retract > 0) p->mc_rrate = tool->retract; }   /* regen-consistency 2026-11-19: the retract rate with the feeds -
                    GetFeedsAndSpeedsFromTool (mccore 0x180884bf0) takes feed / plunge / retract / rpm of the tool's op_tool_info (+0x30 /
                    +0x38 / +0x40 / +0x48; cui_ops.c fs_from_source); the save writes the tool's block (toollib.c tool_op_info) */
        /* regen-consistency 2026-11-19: 2D High Speed back feed 0 = the op's feed (params_from_record, LoadDynPockParm) - the feed the op
           has now (the tool's), not the default op's */
        if ((type == CAM_DYNAMIC || type == CAM_AREA || type == CAM_DYNCONTOUR || type == CAM_PEEL) && o->raw_n > 0x1250) {
            double bf; memcpy(&bf, o->raw + 0x1248, 8); if (!(bf > 1e-5)) p->back_feed = p->feed; }
        p->file_op_id = 0;
        return 1;
    }
    return 0;
}

/* ---- Edit Common Parameters (opmanres dialog 243; mastercam.dll CEditCommonDlg, apply = FUN_180145b50) ----
 * The dialog writes operation fields; the record keeps the operation at +0x68, so op +X = record +0x68+X:
 * tool # +0x2e0 (rec 0x348), head # +0x53c (0x5a4), feed +0x310 (0x378), plunge +0x318 (0x380), len. offset +0x30c
 * (0x374), dia. offset +0x308 (0x370), retract rate +0x320 (0x388), spindle +0x328 (0x390, < 0 = CCW); clearance
 * +0x108 / inc +0x110 / on +0x111 / when +0x2b1 (0x170 / 0x178 / 0x179 / 0x319); retract +0x118 / +0x120 / +0x121
 * (0x180 / 0x188 / 0x189), on drill ops (opcode 2) the retract goes to +0x128 / +0x130; feed plane +0x128 / +0x130
 * (0x190 / 0x198, skipped on op_is_drill ops); top of stock +0x280 / +0x288 (0x2e8 / 0x2f0); depth +0x138 / +0x140
 * (0x1a0 / 0x1a8); rapid retract +0x141 (0x1a9) = combo 0; force tool change +0x289 (0x2f1) = combo 0; batch +0x291
 * (0x2f9) = combo 0. The heights become this program's parameters with params_from_record's formulas. */
static void rpatch_set(CamParams *p, int off, const void *v, int n, int height) {
    int k = 0;
    while (k < p->nrpatch && p->rpatch[k].off != off) k++;
    if (k == p->nrpatch) { if (p->nrpatch >= CAM_NRPATCH) { cam_rpatch_full(off); return; } p->nrpatch++; }
    p->rpatch[k].off = (uint16_t)off; p->rpatch[k].n = (uint8_t)n; p->rpatch[k].height = (uint8_t)height;
    memcpy(p->rpatch[k].v, v, n);
}
static int rpatch_byte(const CamParams *p, int off, int dflt) {
    for (int k = 0; k < p->nrpatch; k++) if (p->rpatch[k].off == off) return p->rpatch[k].v[0];
    return dflt;
}
void cam_common_drop_heights(CamParams *p) {
    int j = 0;
    for (int k = 0; k < p->nrpatch; k++) if (!p->rpatch[k].height) p->rpatch[j++] = p->rpatch[k];
    p->nrpatch = j;
}

void cam_common_apply(McamFile *f, CamOp *op, const CamCommonEdit *ce, const uint8_t *orig, uint32_t orign) {
    CamParams *p = &op->p; int drill = p->type == CAM_DRILL;
    if (ce->toolsel) {   /* the op's tool (internal tool system +0x118 call - its feed / speed handling NOT DECODED: the tool's shape only) */
        const Tool *t = ce->toolsel;
        p->tool_num = t->num; p->tool_dia = t->dia; p->tool_type = t->type; p->corner_r = t->corner_r; p->tip_angle = t->tip_angle;
        p->flute_len = t->flute_len; p->tip_dia = t->tip_dia;
    }
    if (ce->on_tool) { int32_t v = ce->tool; rpatch_set(p, 0x348, &v, 4, 0); }
    if (ce->on_head) { int32_t v = ce->head; rpatch_set(p, 0x5a4, &v, 4, 0); }
    if (ce->on_feed) p->feed = ce->feed;
    if (ce->on_plunge) p->plunge_feed = ce->plunge;
    if (ce->on_lofs) { int32_t v = ce->lofs; rpatch_set(p, 0x374, &v, 4, 0); }
    if (ce->on_dofs) { int32_t v = ce->dofs; rpatch_set(p, 0x370, &v, 4, 0); }
    if (ce->on_rrate) rpatch_set(p, 0x388, &ce->rrate, 8, 0);
    int ccw = p->rpm < 0;                                             /* speed keeps the op's direction ... */
    if (ce->on_rpm) p->rpm = ccw ? -abs(ce->rpm) : abs(ce->rpm);
    if (ce->on_dir) p->rpm = ce->dir ? -abs(p->rpm) : abs(p->rpm);   /* ... unless the direction is set too */

    /* heights in record terms (value, incremental, on) with the flags the saved record has: the stored op's when it is
       taken over now, else this program's save (clearance / retract absolute, feed plane incremental except drill) */
#define OF(off, d) (orig ? rd8(orig, orign, off) : rpatch_byte(p, off, d))
    double cz = 0;
    if (op->nchains && op->chains[0].n) { const Ent *e0 = &f->ents[op->chains[0].ent[0]]; cz = e0->type == E_ARC ? e0->c.z : e0->a.z; }
    int clr_inc = OF(0x178, 0), clr_on = OF(0x179, 1), ret_inc = OF(0x188, 0), ret_on = OF(0x189, p->retract_z > -1e29);
    int fp_inc = OF(0x198, !drill), top_inc = OF(0x2f0, 0), dep_inc = OF(0x1a8, 0);
#undef OF
    double top = p->top_z;
    double v_top = top_inc ? top - cz : top, v_dep = dep_inc ? p->final_z - cz : p->final_z;
    double v_fp = drill ? (fp_inc ? p->r_plane - top : p->r_plane) : (fp_inc ? p->feed_plane : top + p->feed_plane);
    double v_ret = ret_on ? (ret_inc ? p->retract_z - top : p->retract_z) : 0;
    double v_clr = clr_inc ? p->clearance - top : p->clearance;
    uint8_t b;
#define FLAG(off, var, combo, yes) { if (combo) var = (combo) == (yes); b = (uint8_t)var; rpatch_set(p, off, &b, 1, 1); }   /* written with the value */
    if (ce->on_clr) { v_clr = ce->clr; rpatch_set(p, 0x170, &v_clr, 8, 1); FLAG(0x179, clr_on, ce->clr_use, 1); FLAG(0x178, clr_inc, ce->clr_inc, 2);
                      if (ce->clr_when) { b = ce->clr_when == 2; rpatch_set(p, 0x319, &b, 1, 0); } }
    if (ce->on_ret) {
        if (drill) { v_fp = ce->ret; rpatch_set(p, 0x190, &v_fp, 8, 1); FLAG(0x198, fp_inc, ce->ret_inc, 2); }
        else { v_ret = ce->ret; rpatch_set(p, 0x180, &v_ret, 8, 1); FLAG(0x189, ret_on, ce->ret_use, 1); FLAG(0x188, ret_inc, ce->ret_inc, 2); }
    }
    if (ce->on_fp && !drill) { v_fp = ce->fp; rpatch_set(p, 0x190, &v_fp, 8, 1); FLAG(0x198, fp_inc, ce->fp_inc, 2); }
    if (ce->on_top) { v_top = ce->top; rpatch_set(p, 0x2e8, &v_top, 8, 1); FLAG(0x2f0, top_inc, ce->top_inc, 2); }
    if (ce->on_dep) { v_dep = ce->dep; rpatch_set(p, 0x1a0, &v_dep, 8, 1); FLAG(0x1a8, dep_inc, ce->dep_inc, 2); }
#undef FLAG
    if (ce->on_rapid) { b = ce->rapid == 0; rpatch_set(p, 0x1a9, &b, 1, 0); }
    if (ce->on_ftc) { b = ce->ftc == 0; rpatch_set(p, 0x2f1, &b, 1, 0); }
    if (ce->on_batch) { b = ce->batch == 0; rpatch_set(p, 0x2f9, &b, 1, 0); }

    /* back to parameters (params_from_record) */
    top = v_top + (top_inc ? cz : 0);
    p->top_z = top; p->final_z = dep_inc ? cz + v_dep : v_dep;
    p->dep_inc = dep_inc != 0; p->top_inc = top_inc != 0; p->dep_cz = cz;
    if (drill) p->r_plane = v_fp + (fp_inc ? top : 0);
    else p->feed_plane = fp_inc ? v_fp : v_fp - top;
    double ret = v_ret + (ret_inc ? top : 0), clr = v_clr + (clr_inc ? top : 0);
    p->retract_z = ret_on ? ret : -1e30;
    p->clearance = clr_on ? clr : (ret_on ? ret : top + p->feed_plane);
}
