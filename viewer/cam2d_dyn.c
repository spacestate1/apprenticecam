/* cam2d_dyn.c - 2D High Speed (op 134) Dynamic Mill, Dynamic Contour, Peel Mill and Area Mill Core Roughing dispatch.
 * Included by cam2d.c. Dynamic Mill runs the port of Mastercam's mcce.dll engine (mcce_dyn.c,
 * ../formats/mcce_port_plan.md) with the mcmill glue decoded below; Dynamic Contour (closed chains) and Peel Mill (the CHST2DProcess
 * path) run the same engine since the HST engines pass (formats/opdlg_port_audit.md "HST engines (post-batch 9)"). Area Mill Core
 * Roughing is NOT DECODED and is not generated (a stored op stays as Mastercam made it). Batch 9: hst_record = the record + the op dialog's
 * record patches, read by LoadDynPockParm and (cam2d.c) the Area Mill macro. */

#include "mcce_dyn.h"
static int gen_dynamic_mcce(const McamFile *f, CamOp *op, MB *b);
static int hst_has_patch_at(const CamParams *p, int off);

/* what mcmill's input stage does and this program does not (the op is refused rather than generated wrongly; N9-15). record =
   hst_record; style 0x1228: 0 Dynamic Mill, 1 Area Mill, 2 Dynamic Contour, 3 Peel. Style 2 reads only chain set 0 (FUN_1801b58f0
   decomp/mcmill_hst2d.c l.11331 chains_from_db(0)), style 3 only chain set 0 (PrepareChains: every other conversion is guarded by
   style < 2, l.6160-6197): their other roles are skipped, not refused. */
/* a chain's role: avoidance is stored in `avoid` by every path (project files, cam_cli a:, the op dialog), the other roles only by cam_fileop */
static int hst_role(const Chain *c) { return c->avoid ? 4 : c->role; }
static int hst_input_refused(const McamFile *f, CamOp *op, const uint8_t *rec, uint32_t rn) {
    const CamParams *p = &op->p;
#define HST_R(...) do { snprintf(op->error, sizeof op->error, __VA_ARGS__); return 1; } while (0)
    int style = p->type == CAM_DYNAMIC ? 0 : p->type == CAM_AREA ? 1 : p->type == CAM_DYNCONTOUR ? 2 : 3;
    int isl_dep = rn > 0x1740 && ((rec[0x868] && rec[0x892]) || rec[0x1740]);   /* SetParams l.9303: this+0x56 = (record 0x868 && 0x892) || 0x1740, not for style 2 */
    if (isl_dep && style != 2) HST_R("2D High Speed \"island depths\" (PackageSomething l.5669-5702 and its per-level regeneration FUN_1801b33b0) is NOT DECODED here; not generated");
    /* prm_hem::OverrideFeedsAndSpeeds (mccore.dll 0x1806c85b0, on op +0x1698 = record 0x1700): (b3 && b2) || (b0 && b1). FUN_18029cb90 dyn l.112-125 then
       runs the engine with feed op +0x16b8 and spindle op +0x16c0; our op's feed / spindle fields are not switched */
    if (rn > 0x1703 && ((rec[0x1703] && rec[0x1702]) || (rec[0x1700] && rec[0x1701])))
        HST_R("2D High Speed feed / speed override (prm_hem::OverrideFeedsAndSpeeds) is not ported here; not generated");
    /* FUN_1801afa30 runs through LoadDynPockParm FUN_1801af380 (Dynamic, Dynamic Contour, Peel: mm l.303957, l.455792, l.461469); the Area Mill
       worker FUN_18029d3e0 does not call it. A tool the tool manager cannot find keeps R / r of the op (local_728 false): tool_type 0 here. */
    if (style != 1 && (p->tool_type == 0x17 || p->tool_type == 0x19 || p->tool_type == 0x0c || p->tool_type == 0x15))
        HST_R("2D High Speed with tool type 0x%x (FUN_1801afa30 tool-manager radius refinement) is NOT DECODED here; not generated", p->tool_type);
    if (style >= 2) return 0;
    int nmach = 0;
    for (int i = 0; i < op->nchains; i++) {
        const Chain *c = &op->chains[i];
        if (hst_role(c) == 5) HST_R("2D High Speed entry chains (chain role 5: PrepareChains l.6154, S3 / FUN_1802b8d50) are NOT DECODED here; not generated");
        if (hst_role(c) == 6) HST_R("2D High Speed air chains (chain role 6: PackageChains l.5085-5095, rest material path l.5099-5149) are NOT DECODED here; not generated");
        if (hst_role(c) == 7) HST_R("2D High Speed containment chains (chain role 7: PackageChains l.4739-4835) are NOT DECODED here; not generated");
        if (c->avoid && !c->closed) HST_R("An open avoidance region chain was detected.");   /* PrepareChainsReview l.6326: LogWarning(4), mcmillres 0x171, and PrepareChains stops for style < 2 */
        if (!c->avoid && !c->closed) HST_R("2D High Speed open machining chains (PrepareOpenMachPockBounds / OpenClosedMachPockBounds / CloseOpenMachPockBounds) are NOT DECODED here; not generated");
        if (!c->avoid) nmach++;
    }
    (void)f;
    /* SetOperationID hst2d l.9197: this+0x57 for style 0 with record 0x16bb = 5; PrepareChainsReview l.6315-6323: no non-point entry chain -> LogError(4),
       PrepareChains l.6160-6163 stops (entry chains, role 5, are refused above) */
    if (style == 0 && rn > 0x16bb && rec[0x16bb] == 5) HST_R("Custom entry is chosen but no open/closed (Entry) chains have been selected.");   /* mcmillres 0x13c */
    /* record 0x17b8 trim to rest material, Area Mill: plan "Edit To Boundaries" 0x1416 of the kernel (area_mill.c mode 4): not ported */
    if (style == 1 && rn > 0x17b8 && rec[0x17b8]) HST_R("Area Mill trim to rest material (kernel plan Edit To Boundaries 0x1416) is NOT DECODED here; not generated");
    if (style == 1) {
        if (nmach > 1) HST_R("Area Mill with several machining chains (one region per chain, PackageChains l.5005-5080) is NOT DECODED here; not generated");
        if (rn > 0x15ce && rec[0x15ce]) HST_R("Area Mill trochoidal post-step (record 0x15ce: mccore Mutl trochoid functions) is NOT DECODED here; not generated");
        if (rn > 0x1566 && rec[0x1566]) HST_R("Area Mill profile smoothing (record 0x1566: FUN_180557f60 with MaxRadius) is NOT DECODED here; not generated");
    }
#undef HST_R
    return 0;
}
static uint8_t *hst_record(const McamFile *f, const CamParams *p, const Op **fo_out, uint32_t *n_out);
static int hst_refused(const McamFile *f, CamOp *op) {
    uint32_t rn = 0; uint8_t *rec = hst_record(f, &op->p, NULL, &rn);
    int r = hst_input_refused(f, op, rec, rn);
    free(rec);
    return r;
}

/* 2D High Speed dispatch */
static int gen_dynamic(const McamFile *f, CamOp *op, MB *b) {
    CamParams *p = &op->p;
    if (hst_refused(f, op)) return 0;
    if (p->type == CAM_DYNAMIC) return gen_dynamic_mcce(f, op, b);
    if (p->type == CAM_DYNCONTOUR) {
        /* HST engines (formats/opdlg_port_audit.md "HST engines (post-batch 9)"): generated by default since the band roles of
           FUN_1800922e0 were re-read (mcce_dyn.c prep_ctour: the wide band is the motion region M, the thin stock band the
           material) and MCCECalcOffset's sign rule decoded (formats/mcce_engines.md 6.13). No Dynamic Contour op with NCI
           exists anywhere: checked by gouge / rapid / stock self-consistency only (tools/tests/hst_check.py). */
        return gen_dynamic_mcce(f, op, b);
    }
    if (p->type == CAM_PEEL) return gen_dynamic_mcce(f, op, b);   /* HST engines: gen_peel_mcce (CHST2DProcess path) */
    /* Area Mill with "from outside" (record 0x1567 = 1, mode 0): McHmm Core Roughing. The passes are the Area Clearance
       evaluator's (formats/hst3d_port_plan.md "ar" 2: both constructors set Eva +0xff0, area_mill_passes), but the pass region and what
       FUN_1802ef450 trims to without trim boundaries are NOT DECODED (ar 4; the 3D Area Roughing port's box is labelled ours and
       measured on surfaces, not this input). The mckernel mode-0 expansion FUN_1802a9c60 / a9e80 / a9d80 (decomp/mcmill_all.c
       l.464506-464640) is read but feeds only the link planner's box (CoreRoughingExpansionX / Y, hp l.2995-3020), and the linker is not ported. */
    snprintf(op->error, sizeof op->error, "Area Mill Core Roughing (from outside): its pass region (McHmm FUN_1802ef450) is NOT DECODED here; not generated");
    return 0;
}

/* ------------------------------------------------------------------------------------------------ mcce engine glue */
/* records -> moves (mcmill ProcessDynamicMillExt, decomp/mcmill_hst2d_dyn.c 3185-3390: Z = entity +0x78, arcs with a Z
   change linearised with Z interpolated along the pieces). Decoded: the hooks at the switch into / out of CeFlags-1
   (rapid) records write no motion - FUN_1801afb70 only calls mccore set_heights, FUN_1801afb60 returns 1
   (decomp/mcmill_hst2d_dyn.c l.6044-6070) - and OutputMotionToBnci writes every record as G0 (flag 0) / G1 straight to
   its own XYZ (decomp/mcmill_hst2d.c l.4445 G0 / G1 = record flag, l.4534-4540 gr_lin_mill), so Mastercam's rapids are the engine's rapid records (its NCI has
   vertical G0 retracts from the floor, and on from-outside ops G0 descents into air, e.g. buckle-v6 op 31 to Z-0.5).
   This port's rapid records are not proven to be in air, so the SAFE rule stays: rapid up to the feed plane, across,
   rapid down to the feed plane and the plunge feed to the next record's start. */
static void mcce_moves(MB *b, const CamParams *p, const McceRec *r, int n, double fp, int *down) {
    double tol = fmax(p->tol, 0.001);
    for (int i = 0; i < n; i++) {
        const McceRec *e = &r[i];
        double sx = e->id == 'A' ? e->cx + e->r * cos(e->sa) : e->x1, sy = e->id == 'A' ? e->cy + e->r * sin(e->sa) : e->y1;
        double ex = e->id == 'A' ? e->cx + e->r * cos(e->sa + e->sw) : e->x2, ey = e->id == 'A' ? e->cy + e->r * sin(e->sa + e->sw) : e->y2;
        if (e->ce & 1) {   /* rapid class */
            if (*down) { V3 l = lastp(b); mv(b, 1, l.x, l.y, fp, 0); *down = 0; }
            if (!b->n) mv(b, 1, ex, ey, p->clearance, 0);
            mv(b, 1, ex, ey, fp, 0);
            continue;
        }
        if (!*down && e->id != 'A' && fabs(ex - sx) < 1e-9 && fabs(ey - sy) < 1e-9 && e->z1 > e->z0) {
            /* a vertical record going UP after a rapid (the engine's rapids run at the level, e.g. NAKTO op 2 record 7184 at z -8, then
               7185 straight up to the ramp start 3.0): OutputMotionToBnci writes a record straight to its own XYZ (header above), and the
               stored NCI there is a G0 down to 3.0 from the clearance (NAKTO op 2, after its second section-start block) - no plunge to
               the rapid's level first (the SAFE rule below would plunge to z0 = the level and feed back up) */
            if (!b->n) mv(b, 1, sx, sy, p->clearance, 0);
            V3 l = lastp(b);
            if (fabs(l.x - sx) > 1e-9 || fabs(l.y - sy) > 1e-9) { if (l.z < fp) mv(b, 1, l.x, l.y, fp, 0); mv(b, 1, sx, sy, fp, 0); }
            if (e->z1 < fp) { mv(b, 1, sx, sy, fp, 0); mv(b, 0, sx, sy, e->z1, p->plunge_feed > 0 ? p->plunge_feed : e->feed); }
            else mv(b, 1, sx, sy, e->z1, 0);
            *down = 1;
            continue;
        }
        if (!*down) {
            if (!b->n) mv(b, 1, sx, sy, p->clearance, 0);
            V3 l = lastp(b);
            if (fabs(l.x - sx) > 1e-9 || fabs(l.y - sy) > 1e-9) { if (l.z < fp) mv(b, 1, l.x, l.y, fp, 0); mv(b, 1, sx, sy, fp, 0); }
            /* never rapid below the feed plane: down to it at rapid, then at the plunge feed (as plunge_move; the decoded
               Mastercam writes the record's own Z: the SAFE rule, header above) */
            if (e->z0 < fp) { mv(b, 1, sx, sy, fp, 0); mv(b, 0, sx, sy, e->z0, p->plunge_feed > 0 ? p->plunge_feed : e->feed); }
            else mv(b, 1, sx, sy, e->z0, 0);
            *down = 1;
        }
        if (e->id == 'A') {
            int m = (int)ceil(fabs(e->sw) / (2 * acos(fmax(-1, 1 - tol / fmax(e->r, tol))))); if (m < 1) m = 1; if (m > 720) m = 720;
            int from = b->n;
            for (int k = 1; k <= m; k++) { double t = (double)k / m, a = e->sa + e->sw * t;
                mv(b, 0, e->cx + e->r * cos(a), e->cy + e->r * sin(a), e->z0 + (e->z1 - e->z0) * t, e->feed); }
            if (fabs(e->sw) <= 2 * M_PI + 1e-9) mb_tag_arc(b, from, e->cx, e->cy, e->sw < 0);   /* the 'A' record for the post */
        } else mv(b, 0, ex, ey, e->z1, e->feed);
    }
}

/* Dynamic Contour (op 134 style 2): mcmill FUN_1801b58f0 (decomp/mcmill_hst2d.c l.11126-11653, "PeelMill.cpp"):
   LoadDynPockParm, chains_from_db(0) (chain set 0 only, l.11331: other chain roles are not read) -> chains_to_pock_bounds (one
   bound per chain, FUN_1801adf10 drops point entities), then per chain in chain order: set_depths, the first level (with depth
   cuts the count from FUN_1801ba5d0 and level 1 of the formula at l.11417-11428, then copies of the chain's output by
   FUN_1800451f0 - d += rough step for copies before nr, finish step 0x880 after, decomp/mcmill_all.c l.48656), the wall stock
   offset FUN_1801ac7e0 (style 2 branch l.17419-17423 / l.17528-17545: MCCECalcContourOffset(1, sign x d, bound, 0x105, 0, chain_tol,
   1e-6, 1e-6), sign = record 0x5d2 ? -1 : 1; empty result = the bound unchanged, l.17542) and one MCCECalcDynPock per resulting
   bound (S2 / S3 none) with the CTOUR flags (mcce prep_ctour). Refused: open chains (FUN_1800930c0 band kernels), the finish
   pass FUN_180094f20 (record 0x1241), tapered walls (FUN_180050910 path), per-chain Z of incremental heights (the first
   chain's Z is used). */
/* the wall stock of a Dynamic Contour chain: the port of MCCECalcContourOffset on the chain's own entities (flags 0x105, angle 0, gap
   chain_tol, 1e-6 / 1e-6: FUN_1801ac7e0 l.17539-17541). The output's entities become chords of `chord` (the engine takes polylines here).
   No output shape: the bound stays (l.17542). */
static void dyn_ents_clear(void);
static void dyn_ents_add(const McamFile *f, const Chain *c, const double *xy);
static void dyn_ents_of(McceBnd *b);
typedef struct { int ne; char *ety; double *ent; } DcEnts;   /* a bound's entities (McceBnd.ne / ety / ent form) */
static void dc_ents_rev(DcEnts *E) {   /* the same loop the other way round */
    for (int i = 0, j = E->ne - 1; i < j; i++, j--) { char t = E->ety[i]; E->ety[i] = E->ety[j]; E->ety[j] = t; double w[5]; memcpy(w, E->ent + 5 * i, sizeof w); memcpy(E->ent + 5 * i, E->ent + 5 * j, sizeof w); memcpy(E->ent + 5 * j, w, sizeof w); }
    for (int i = 0; i < E->ne; i++) { double *q = E->ent + 5 * i;
        if (E->ety[i] == 'L') { double x = q[0], y = q[1]; q[0] = q[2]; q[1] = q[3]; q[2] = x; q[3] = y; }
        else { q[3] += q[4]; q[4] = -q[4]; } }
}
static CPaths dc_offset_out(McceResult *rp, double chord, const CPaths *orig, const char **why, DcEnts **eout) {
    McceResult res = *rp;
    if (res.refused) { *why = res.refused; return cl_bool(orig, NULL, 0); }
    CPaths o = { calloc(res.n + 1, sizeof(CPath)), 0 };
    if (eout) *eout = calloc(res.n + 1, sizeof(DcEnts));
    for (int k = 0; k < res.n; k++) {
        const McceShape *sh = &res.s[k]; int cap = 64, m = 0; double *q = malloc(sizeof(double) * 2 * cap);
#define DP(X, Y) do { if (m == cap) { cap *= 2; q = realloc(q, sizeof(double) * 2 * cap); } if (!m || fabs(q[2 * m - 2] - (X)) > 1e-9 || fabs(q[2 * m - 1] - (Y)) > 1e-9) { q[2 * m] = (X); q[2 * m + 1] = (Y); m++; } } while (0)
        for (int e = 0; e < sh->n; e++) {
            const McceEnt *x = &sh->e[e];
            if (x->id == 'L') { DP(x->a[0], x->a[1]); DP(x->a[2], x->a[3]); continue; }
            int nn = (int)ceil(fabs(x->a[4]) / (2 * acos(fmax(-1, 1 - chord / fmax(x->a[2], chord))))); if (nn < 1) nn = 1;
            for (int j = 0; j <= nn; j++) { double t = x->a[3] + x->a[4] * j / nn; DP(x->a[0] + x->a[2] * cos(t), x->a[1] + x->a[2] * sin(t)); }
        }
#undef DP
        if (m > 1 && fabs(q[0] - q[2 * m - 2]) < 1e-9 && fabs(q[1] - q[2 * m - 1]) < 1e-9) m--;
        if (m >= 3) {
            if (eout) { DcEnts *E = &(*eout)[o.n]; E->ne = sh->n; E->ety = malloc((size_t)sh->n + 1); E->ent = malloc(sizeof(double) * 5 * ((size_t)sh->n + 1));
                        for (int e = 0; e < sh->n; e++) { E->ety[e] = sh->e[e].id; memcpy(E->ent + 5 * e, sh->e[e].a, sizeof(double) * 5); } }
            o.p[o.n].xy = q; o.p[o.n].n = m; o.n++;
        } else free(q);
    }
    mcce_shapes_free(&res);
    if (!o.n) { free(o.p); return cl_bool(orig, NULL, 0); }
    return o;
}
static CPaths dc_stock_offset(const McamFile *f, const Chain *ch, double d, double gap, double chord, int dir, const CPaths *orig, const char **why, DcEnts **eout) {
    if (ch->cut0 > 1e-12 || ch->cut1 > 1e-12) { *why = "a chain with trimmed ends is NOT DECODED here"; return cl_bool(orig, NULL, 0); }   /* (the offset is of the chain's whole entities) */
    McceEnt *ents = malloc((size_t)(ch->n ? ch->n : 1) * 2 * sizeof(McceEnt));
    /* the bound is ConvertChainsToPockBounds' (decomp/mcmill_hst2d.c l.2276-2470): a full-circle single-arc chain in two halves (as dyn_ents_add) */
    if (ch->n == 1 && f->ents[ch->ent[0]].type == E_ARC && fabs(fabs(f->ents[ch->ent[0]].sw) - 2 * M_PI) < 1e-9) {
        const Ent *E = &f->ents[ch->ent[0]]; int r = ch->rev[0]; double sa = r ? E->a0 + E->sw : E->a0, sw = r ? -E->sw : E->sw;
        for (int h = 0; h < 2; h++) { McceEnt *x = &ents[h]; memset(x, 0, sizeof *x); x->id = 'A'; x->a[0] = E->c.x; x->a[1] = E->c.y; x->a[2] = E->r; x->a[3] = sa + h * sw / 2; x->a[4] = sw / 2; }
        McceShape in2 = { ents, 2, dir, 0 };
        McceResult res2 = mcce_contour_offset(&in2, 1, d, 0x105, 0.0, gap, 1e-6, 1e-6);
        free(ents);
        return dc_offset_out(&res2, chord, orig, why, eout);
    }
    for (int i = 0; i < ch->n; i++) {
        const Ent *E = &f->ents[ch->ent[i]]; int r = ch->rev[i]; McceEnt *x = &ents[i];
        memset(x, 0, sizeof *x);
        if (E->type == E_LINE) { V3 u = r ? E->b : E->a, v = r ? E->a : E->b; x->id = 'L'; x->a[0] = u.x; x->a[1] = u.y; x->a[2] = v.x; x->a[3] = v.y; }
        else if (E->type == E_ARC) { x->id = 'A'; x->a[0] = E->c.x; x->a[1] = E->c.y; x->a[2] = E->r; x->a[3] = r ? E->a0 + E->sw : E->a0; x->a[4] = r ? -E->sw : E->sw; }
        else { free(ents); *why = "a chain entity that is not a line or arc is NOT DECODED here"; return cl_bool(orig, NULL, 0); }
    }
    McceShape in = { ents, ch->n, dir, 0 };
    McceResult res = mcce_contour_offset(&in, 1, d, 0x105, 0.0, gap, 1e-6, 1e-6);
    free(ents);
    return dc_offset_out(&res, chord, orig, why, eout);
}
static int gen_dyncontour_mcce(const McamFile *f, CamOp *op, MB *b, const McceParm *P0, int dc_fin) {
    CamParams *p = &op->p;
    (void)f;
    if (dc_fin) { snprintf(op->error, sizeof op->error, "Dynamic Contour finish passes (FUN_180094540) are not ported"); return 0; }
    int dc_n = 0;
    for (int i = 0; i < op->nchains; i++) {
        if (hst_role(&op->chains[i])) continue;                 /* FUN_1801b58f0 reads chain set 0 only (l.11331) */
        if (!op->chains[i].closed) { snprintf(op->error, sizeof op->error, "Dynamic Contour on open chains (mcce FUN_1800930c0) is NOT DECODED here"); return 0; }
        dc_n++;
    }
    if (!dc_n) { snprintf(op->error, sizeof op->error, "needs closed chains"); return 0; }
    double zl[200]; int nr_dc = 1; double st_dc = 0; int nz = hst_depths(p, zl, 200, &nr_dc, &st_dc);
    double fp = p->top_z + p->feed_plane, tol = fmax(p->tol, p->tool_dia * 0.002);
    int down = 0;
    for (int ci = 0; ci < op->nchains; ci++) {
        if (hst_role(&op->chains[ci])) continue;
        double *xy, z; int n = cam_chain_polyline(f, &op->chains[ci], p->tol, &xy, &z);
        if (n < 3) { free(xy); continue; }
        CPath c0 = { xy, n }; double A0 = cl_area(&c0);
        int dir = A0 > 0 ? 1 : -1;
        CPaths R = { &c0, 1 }, Q;
        /* the engine input as true lines / arcs (NS-6): ConvertChainsToPockBounds keeps the chain's entities (decomp/mcmill_hst2d.c
           l.2276-2470; FUN_1801b58f0 calls chains_to_pock_bounds, l.11331), MCCECalcContourOffset returns entities (mcce_contour_offset);
           the DLL gets them as they are. MCCE_DCENTS=0 = the old chord polylines. */
        int ents_on = !(getenv("MCCE_DCENTS") && *getenv("MCCE_DCENTS") == '0');
        DcEnts *QE = NULL;
        if (fabs(p->stock_xy) > 1e-12) {
            int sg = (P0->flags & 0x400000) ? 1 : -1;           /* FUN_1801ac7e0: record 0x5d2 (op 0x56a) != 0 -> -1 (0x400000 = 0x56a == 0, LoadDynPockParm) */
            const char *why = NULL;
            Q = dc_stock_offset(f, &op->chains[ci], sg * p->stock_xy, P0->chain_tol, tol, dir, &R, &why, ents_on ? &QE : NULL);
            if (why) { snprintf(op->error, sizeof op->error, "Dynamic Contour wall stock (MCCECalcContourOffset 0x105): %s", why); cl_free(&Q); free(xy); free(QE); return 0; }
        } else {
            DcEnts E0 = { 0, NULL, NULL };
            if (ents_on) { dyn_ents_clear(); dyn_ents_add(f, &op->chains[ci], xy); McceBnd t = { xy, n, 0, 0, 0, NULL, NULL }; dyn_ents_of(&t);
                           if (t.ne) { E0.ne = t.ne; E0.ety = malloc((size_t)t.ne); E0.ent = malloc(sizeof(double) * 5 * (size_t)t.ne); memcpy(E0.ety, t.ety, (size_t)t.ne); memcpy(E0.ent, t.ent, sizeof(double) * 5 * (size_t)t.ne); }
                           dyn_ents_clear(); }
            if (E0.ne) {   /* the chain itself, as its polyline and entities */
                Q.p = calloc(2, sizeof(CPath)); Q.n = 1; Q.p[0].xy = malloc(sizeof(double) * 2 * (size_t)n); memcpy(Q.p[0].xy, xy, sizeof(double) * 2 * (size_t)n); Q.p[0].n = n;
                QE = calloc(2, sizeof(DcEnts)); QE[0] = E0;
            } else Q = cl_bool(&R, NULL, 0);
        }
        McceRec *all = NULL; int nall = 0;
        for (int k = 0; k < Q.n; k++) {
            CPath *q = &Q.p[k]; if (q->n < 3) continue;
            DcEnts *E = QE && QE[k].ne ? &QE[k] : NULL;
            if ((cl_area(q) > 0) != (dir > 0)) { reverse_path(q); if (E) dc_ents_rev(E); }   /* keep the chain's direction */
            McceBnd bd = { q->xy, q->n, 0, 0, E ? E->ne : 0, E ? E->ety : NULL, E ? E->ent : NULL };
            McceParm P = *P0; P.depth = zl[0];
            McceRec *rec = NULL; int nrec = 0; char err[160] = "";
            if (!mcce_calc_dyn_pock(&P, &bd, 1, NULL, 0, NULL, 0, &rec, &nrec, err, sizeof err)) {
                snprintf(op->error, sizeof op->error, "%s", err); free(rec); free(all);
                if (QE) { for (int j = 0; j < Q.n; j++) { free(QE[j].ety); free(QE[j].ent); } free(QE); }
                cl_free(&Q); free(xy); return 0; }
            all = realloc(all, sizeof(McceRec) * (nall + nrec + 1)); memcpy(all + nall, rec, sizeof(McceRec) * nrec); nall += nrec; free(rec);
        }
        if (QE) { for (int k = 0; k < Q.n; k++) { free(QE[k].ety); free(QE[k].ent); } free(QE); }
        cl_free(&Q); free(xy);
        McceRec *cp = nall ? malloc(sizeof(McceRec) * nall) : NULL;
        double d = 0;
        for (int k = 0; k < nz && nall; k++) {
            if (k) d += k - 1 < nr_dc - 1 ? st_dc : p->mc_depth_fs;
            for (int i = 0; i < nall; i++) { cp[i] = all[i]; cp[i].z0 -= d; cp[i].z1 -= d; }
            mcce_moves(b, p, cp, nall, fp, &down);
            if (down) { V3 l = lastp(b); mv(b, 1, l.x, l.y, fp, 0); down = 0; }
        }
        free(cp); free(all);
    }
    if (!b->n) { snprintf(op->error, sizeof op->error, "nothing to cut"); return 0; }
    { V3 e = lastp(b); mv(b, 1, e.x, e.y, p->clearance, 0); }
    return 1;
}

/* ------------------------------------------------------------------------------------------------ Peel Mill */
/* Peel Mill (op 134 style 3, record 0x17d8 = 0 "Dynamic peel"): mcmill CHST2DProcess::PreparePockBoundsPeel @1802b4090
   (decomp/mcmill_hst2d.c l.6475-8724; spec decomp/hst2/peel_mcmill_spec.txt) builds the Material loop (this+0x348) and the
   Motion loop (this+0x328) round the two side curves, mcce PeelAesthetics reshapes them, and the work item runs
   MCCECalcDynPock(parm, S1 = Motion, S2 = Material, S3 none) (executor FUN_1802a5760 decomp/mcmill_hst2d_dyn.c l.397-416).
   Record offsets below = op offset + 0x68. */
static double rdd_rec(const uint8_t *r, uint32_t n, uint32_t off) { double v = 0; if (off + 8 <= n) memcpy(&v, r + off, 8); return v; }
static void wrd_rec(uint8_t *r, uint32_t n, uint32_t off, double v) { if (off + 8 <= n) memcpy(r + off, &v, 8); }
/* PreparePockBoundsPeel l.6849-6853: op 0x14ff = 0, op 0x15a0 = 10.0, op 0x1657 = 1, op 0x1653 = 0, op 0x1651 = 0 (written into
   cur_op, which LoadDynPockParm reads next) */
static void peel_forced_writes(uint8_t *rec, uint32_t rn) {
    if (rn > 0x1567) rec[0x1567] = 0;
    wrd_rec(rec, rn, 0x1608, 10.0);
    if (rn > 0x16bf) { rec[0x16bf] = 1; rec[0x16bb] = 0; rec[0x16b9] = 0; }
}
typedef struct { double *xy; int n, cap; } PL;      /* polyline under construction */
static void pl_add(PL *l, double x, double y) {
    if (l->n && fabs(l->xy[2 * l->n - 2] - x) < 1e-7 && fabs(l->xy[2 * l->n - 1] - y) < 1e-7) return;   /* zero-length entities dropped
                                                                                                            (as FUN_18012c200 1e-10 cleans the engine's input; ours: 1e-7 for the chained polylines) */
    if (l->n == l->cap) { l->cap = l->cap ? 2 * l->cap : 64; l->xy = realloc(l->xy, l->cap * 2 * sizeof(double)); }
    l->xy[2 * l->n] = x; l->xy[2 * l->n + 1] = y; l->n++;
}
/* unit start / end tangent of an open polyline (mcmill: of its first / last entity, l.6660-6774; with chained arcs as chords
   here the first / last chord - ours, within the chain tolerance) */
static int pl_tan(const double *xy, int n, int end, double *tx, double *ty) {
    for (int i = 0; i + 1 < n; i++) {
        int a = end ? n - 2 - i : i, b = a + 1;
        double dx = xy[2 * b] - xy[2 * a], dy = xy[2 * b + 1] - xy[2 * a + 1], L = hypot(dx, dy);
        if (L > 1e-12) { *tx = dx / L; *ty = dy / L; return 1; }
    }
    return 0;
}
/* ccomp_pock(chain, d, 'R' / 'L', set_min_fil_angle(2) = 0.01 rad, ...) (l.6652-6653, 6826-6827): mcmill / mccore import, internals
   NOT DECODED; this program's open-chain offset (cam2d.c offset_open + offset_open_trim, the Contour path: outside corners rolled)
   is used; d = 0 keeps the chain [inferred]. The decompile is decomp/mccore_pock.c l.485 (core FUN_1805d8690): not ported here */
static CPath peel_side(const double *xy, int n, double d, int right, double tol) {
    if (d <= 1e-12) { CPath c = { malloc(sizeof(double) * 2 * n), n }; memcpy(c.xy, xy, sizeof(double) * 2 * n); return c; }
    double sd = right ? -d : d;
    CPath raw = offset_open(xy, n, sd, 2);
    CPath o = offset_open_trim_x(xy, n, sd, 2, tol, raw, 1);
    /* decisions batch dec2 (Q5, ours): the trim walks Clipper's loop (1e-5 grid) from the exact end offsets, so its first / last
       vertex can be a rounding copy of the end point (6e-6 away on bracket4-dashed chain 4): drop points closer than 1e-4 to the
       one kept before them (the exact end points stay). Such a sliver chord gave pl_tan a random start / end direction - the
       extension rays then met at a false dS / dE (10.4 mm instead of none) and the Material / Motion ends were wrong: "nothing
       to cut" (no air entry, CE_AIR_ENTRY_ONLY) with both extensions on, and the 0.17 mm wall stock of chain 4 left width 12.
       ccomp_pock's own output has no such entity. */
    int m = 0;
    for (int i = 0; i < o.n; i++) {
        double x = o.xy[2 * i], y = o.xy[2 * i + 1];
        if (m && hypot(x - o.xy[2 * m - 2], y - o.xy[2 * m - 1]) < 1e-4) { if (i == o.n - 1) { o.xy[2 * m - 2] = x; o.xy[2 * m - 1] = y; } continue; }
        o.xy[2 * m] = x; o.xy[2 * m + 1] = y; m++;
    }
    if (m >= 2) o.n = m;
    return o;
}
/* CalcLnLnIntersectPt of two rays (p + s u, q + t v): 1 with s, t when not parallel */
static int ray_ray(double px, double py, double ux, double uy, double qx, double qy, double vx, double vy, double *s, double *t) {
    double den = ux * vy - uy * vx;
    if (fabs(den) < 1e-12) return 0;
    double wx = qx - px, wy = qy - py;
    *s = (wx * vy - wy * vx) / den; *t = (wx * uy - wy * ux) / den;
    return 1;
}
/* the closed loop of l.7060-7635 (Material, ext = the user extensions with the tolP test) / l.7709-8242 (Motion, the long
   extensions, no tolP test): [entry ext of A] A [exit ext of A] connector [exit ext of B] REV(B) [entry ext of B] closer */
static PL peel_loop(const CPath *A, const CPath *B, double eS, double eX, int tolp_test, double tolP, double *sx, double *sy) {
    PL L = { 0 }; double tsx, tsy, tex, tey, bsx, bsy, bex, bey;
    pl_tan(A->xy, A->n, 0, &tsx, &tsy); pl_tan(A->xy, A->n, 1, &tex, &tey);
    pl_tan(B->xy, B->n, 0, &bsx, &bsy); pl_tan(B->xy, B->n, 1, &bex, &bey);
    double ax0 = A->xy[0], ay0 = A->xy[1], ax1 = A->xy[2 * A->n - 2], ay1 = A->xy[2 * A->n - 1];
    double bx0 = B->xy[0], by0 = B->xy[1], bx1 = B->xy[2 * B->n - 2], by1 = B->xy[2 * B->n - 1];
    int inE = !tolp_test || eS > tolP, inX = !tolp_test || eX > tolP;
    if (sx) { *sx = ax0; *sy = ay0; }                                                   /* op 0x14e0 = start(A) l.7079-7081 */
    if (inE) { pl_add(&L, ax0 - tsx * eS, ay0 - tsy * eS); if (sx) { *sx = ax0 - tsx * eS; *sy = ay0 - tsy * eS; } }   /* l.7083-7151 */
    for (int i = 0; i < A->n; i++) pl_add(&L, A->xy[2 * i], A->xy[2 * i + 1]);
    if (inX) pl_add(&L, ax1 + tex * eX, ay1 + tey * eX);                                /* exit ext of A l.7237-7303 */
    if (inX) pl_add(&L, bx1 + bex * eX, by1 + bey * eX);                                /* connector end + exit ext of B l.7330-7395 */
    for (int i = B->n - 1; i >= 0; i--) pl_add(&L, B->xy[2 * i], B->xy[2 * i + 1]);    /* REV(B) l.7396-7503 */
    if (inE) pl_add(&L, bx0 - bsx * eS, by0 - bsy * eS);                                /* entry ext of B l.7504-7580 */
    return L;                                                                           /* closing line: implicit (closed bound) */
}
/* ours (a guard, not Mastercam's): 1 when the closed loop crosses itself - the side curves of this program's open-chain offset
   (peel_side) overlap when the width is too large for the chain's curvature; ccomp_pock's behaviour there is NOT DECODED */
static int pl_self_isect(const PL *l) {
    int n = l->n;
    for (int i = 0; i < n; i++) {
        double ax = l->xy[2 * i], ay = l->xy[2 * i + 1], bx = l->xy[2 * ((i + 1) % n)], by = l->xy[2 * ((i + 1) % n) + 1];
        for (int j = i + 2; j < n; j++) {
            if (i == 0 && j == n - 1) continue;                                        /* adjacent through the closing edge */
            double cx = l->xy[2 * j], cy = l->xy[2 * j + 1], dx = l->xy[2 * ((j + 1) % n)], dy = l->xy[2 * ((j + 1) % n) + 1];
            if (fmax(ax, bx) < fmin(cx, dx) || fmax(cx, dx) < fmin(ax, bx) || fmax(ay, by) < fmin(cy, dy) || fmax(cy, dy) < fmin(ay, by)) continue;
            double d1 = (dx - cx) * (ay - cy) - (dy - cy) * (ax - cx), d2 = (dx - cx) * (by - cy) - (dy - cy) * (bx - cx);
            double d3 = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax), d4 = (bx - ax) * (dy - ay) - (by - ay) * (dx - ax);
            if (((d1 > 1e-12 && d2 < -1e-12) || (d1 < -1e-12 && d2 > 1e-12)) && ((d3 > 1e-12 && d4 < -1e-12) || (d3 < -1e-12 && d4 > 1e-12))) return 1;
        }
    }
    return 0;
}
static int gen_peel_mcce(const McamFile *f, CamOp *op, MB *b, McceParm *P, const uint8_t *rec, uint32_t rn, int dc_fin) {
    CamParams *p = &op->p;
    int metric = mcam_is_metric(f);
    if (rn > 0x17d8 && rec[0x17d8]) { snprintf(op->error, sizeof op->error, "Peel Mill cutting style \"Peel\" (bnci_peel 0x1801b68d0) is NOT DECODED here"); return 0; }
    if (dc_fin) { snprintf(op->error, sizeof op->error, "Peel Mill finish passes (FUN_180094540) are NOT DECODED here"); return 0; }
    double D = p->tool_dia, tol = CAM_SYS_TOL;                                          /* tol_exref (config 1000, McSysTol) */
    double tolP = rdd_rec(rec, rn, 0x1288); if (tolP < 1e-12) tolP = metric ? 0.025 : 0.001;   /* SetParams l.9241-9250 */
    double ctol = fmax(p->tol, D * 0.002);
    /* chains: open only, the first two (l.6619-6628, 6868) */
    double *cx[2] = { NULL, NULL }; int cn[2] = { 0, 0 }, nc = 0; double ztop = 0;
    for (int i = 0; i < op->nchains && nc < 2; i++) {
        if (hst_role(&op->chains[i])) continue;                                         /* chain set 0 only (PrepareChains l.6152, the others guarded by style < 2) */
        if (op->chains[i].closed) { for (int k = 0; k < nc; k++) free(cx[k]); snprintf(op->error, sizeof op->error, "Peel Mill needs open chains (closed chain: LogError 8)"); return 0; }
        double z; cn[nc] = cam_chain_polyline(f, &op->chains[i], p->tol, &cx[nc], &z);
        if (cn[nc] < 2) { free(cx[nc]); continue; }
        if (!nc) ztop = z;
        nc++;
    }
    (void)ztop;
    if (!nc) { snprintf(op->error, sizeof op->error, "Peel Mill needs one or two open chains"); return 0; }
    double stock = p->stock_xy;                                                         /* this+0x150 = record 0x2e0 */
    CPath A, B;
    if (nc == 1) {                                                                      /* l.6630-6657 */
        double w = rdd_rec(rec, rn, 0x1258);
        if (p->slot_width > tol && !hst_has_patch_at(p, 0x1258)) w = p->slot_width;   /* ours: the properties panel's "Slot width" (CamParams only) */
        if (w <= tol) w = 4 * D;
        int16_t side = 0; if (rn > 0x1271) memcpy(&side, rec + 0x1270, 2);
        double dR = side == 1 ? 0 : side == 2 ? w : w / 2, dL = side == 1 ? w : side == 2 ? 0 : w / 2;
        A = peel_side(cx[0], cn[0], dR, 1, ctol); B = peel_side(cx[0], cn[0], dL, 0, ctol);
    } else {                                                                            /* l.6658-6836 */
        int a = 0, bb = 1; double tax, tay, tbx, tby;
        if (pl_tan(cx[0], cn[0], 0, &tax, &tay) && pl_tan(cx[1], cn[1], 0, &tbx, &tby)) {
            /* raw start direction vA = e2 - e1 of the first entity (here the first chord) */
            double PAx = cx[0][0], PAy = cx[0][1], PBx = cx[1][0], PBy = cx[1][1];
            double QAx = cx[0][2], QAy = cx[0][3], QBx = cx[1][2], QBy = cx[1][3];
            double d1 = hypot(PBx - PAx, PBy - PAy), d2 = hypot(QBx - PAx, QBy - PAy), d3 = hypot(QAx - PAx, QAy - PAy),
                   d4 = hypot(QBx - PBx, QBy - PBy), d5 = hypot(QAx - PBx, QAy - PBy), d6 = hypot(QAx - QBx, QAy - QBy);
            if (d1 >= tol && d2 >= tol && d3 >= tol && d4 >= tol && d5 >= tol && d6 >= tol) {
                double c1 = (PBx - PAx) * (QBy - PAy) - (PBy - PAy) * (QBx - PAx), c2 = (PAx - PBx) * (QAy - PBy) - (PAy - PBy) * (QAx - PBx);
                if (fabs(c1) >= tol && fabs(c2) >= tol && c1 > 0 && c2 < 0) { a = 1; bb = 0; }   /* swap (l.6806-6815); else keep */
            }
        }
        if (stock < -tol) { A = peel_side(cx[a], cn[a], -stock, 1, ctol); B = peel_side(cx[bb], cn[bb], -stock, 0, ctol); stock = 0; }   /* l.6823-6834 */
        else { A.n = cn[a]; A.xy = malloc(sizeof(double) * 2 * A.n); memcpy(A.xy, cx[a], sizeof(double) * 2 * A.n);
               B.n = cn[bb]; B.xy = malloc(sizeof(double) * 2 * B.n); memcpy(B.xy, cx[bb], sizeof(double) * 2 * B.n); }
    }
    for (int k = 0; k < nc; k++) free(cx[k]);
    if (A.n < 2 || B.n < 2) { free(A.xy); free(B.xy); snprintf(op->error, sizeof op->error, "Peel Mill: no side curves"); return 0; }
    /* extensions (l.6859-7062) */
    double extE = (rn > 0x1240 && rec[0x1240]) ? rdd_rec(rec, rn, 0x1278) : 0, extX = (rn > 0x1272 && rec[0x1272]) ? rdd_rec(rec, rn, 0x1280) : 0;
    double dS = 1e38, dE = 1e38, s, t, ux, uy, vx, vy;
    if (pl_tan(A.xy, A.n, 0, &ux, &uy) && pl_tan(B.xy, B.n, 0, &vx, &vy) && ray_ray(A.xy[0], A.xy[1], -ux, -uy, B.xy[0], B.xy[1], -vx, -vy, &s, &t) && s > tol && t > tol)
        dS = fmin(s, t);                                                                /* |I - start| = s, t: unit rays */
    if (pl_tan(A.xy, A.n, 1, &ux, &uy) && pl_tan(B.xy, B.n, 1, &vx, &vy) && ray_ray(A.xy[2 * A.n - 2], A.xy[2 * A.n - 1], ux, uy, B.xy[2 * B.n - 2], B.xy[2 * B.n - 1], vx, vy, &s, &t) && s > tol && t > tol)
        dE = fmin(s, t);
    if (dS + tol < extE) extE = dS;
    if (dE + tol < extX) extX = dE;
    /* Material (+0x348) and the start point op 0x14e0 (l.7060-7635) */
    double px = 0, py = 0;
    PL mat = peel_loop(&A, &B, extE, extX, 1, tolP, &px, &py);
    /* Motion (+0x328) (l.7709-8242): eE = 3 max(extE, D) (+ stock if > tol; + approach 0x1660 if below it, added as decompiled),
       eX = 2 max(extX, D), both capped by dS / dE */
    double eE = 3 * fmax(extE, D), approach = rdd_rec(rec, rn, 0x1660);
    if (stock > tol) eE += stock;
    if (eE < approach) eE += approach;
    double eX = 2 * fmax(extX, D);
    if (dS + tol < eE) eE = dS;
    if (eX > dE + tol) eX = dE;
    PL mot = peel_loop(&A, &B, eE, eX, 0, tolP, NULL, NULL);
    free(A.xy); free(B.xy);
    if (pl_self_isect(&mat) || pl_self_isect(&mot)) {
        free(mat.xy); free(mot.xy);
        snprintf(op->error, sizeof op->error, "Peel Mill: the side curves cross themselves (width too large for the chain, or two chains in opposite directions); not generated");
        return 0; }
    /* PeelAesthetics(R = D/2, mr = D x 0x1658 %, P = op 0x14e0, &Motion, &Material) (l.8716-8720) */
    McceBnd bm = { mot.xy, mot.n, 0, 0 }, bt = { mat.xy, mat.n, 0, 0 };
    McceBnd *ao = NULL, *bo = NULL; int nao = 0, nbo = 0;
    int pa = mcce_peel_aesthetics(D * 0.5, D * rdd_rec(rec, rn, 0x1658) * 0.01, px, py, P->chain_tol, &bm, 1, &bt, 1, &ao, &nao, &bo, &nbo);
    const McceBnd *S1 = pa ? ao : &bm, *S2 = pa ? bo : &bt; int n1 = pa ? nao : 1, n2 = pa ? nbo : 1;
    if (getenv("PEEL_DUMP")) {   /* ours (testing): the loops before / after PeelAesthetics */
        FILE *o = fopen(getenv("PEEL_DUMP"), "w");
        const McceBnd *L[4] = { &bm, &bt, S1, S2 }; const char *nm[4] = { "motion0", "material0", "motion", "material" };
        for (int k = 0; o && k < 4; k++) { fprintf(o, "%s %d\n", nm[k], L[k]->n); for (int i = 0; i < L[k]->n; i++) fprintf(o, "%.6f %.6f\n", L[k]->xy[2 * i], L[k]->xy[2 * i + 1]); }
        if (o) { fprintf(o, "start %.6f %.6f aesthetics %d\n", px, py, pa); fclose(o); }
    }
    /* LoadDynPockParm for style 3 (+0x0c = 8, +0x10 = op 0x14e0) and FUN_18029cb90 l.136-138: | 0x4000000 (CE_AIR_ENTRY_ONLY) */
    P->start_sel = 8; P->start_x = px; P->start_y = py;
    P->flags |= 0x4000000;
    /* the wall stock: PackageSomething l.5703-5733, FUN_1801ac7e0 with record 0x1567 forced to 0 above: d > 0 the dummy-box inset,
       every piece a work item (mcmill_hst2d.c l.17477-17522); d < 0 grown, the largest piece kept (l.17329-17369) */
    CPaths mreg = { NULL, 0 };
    {   CPath *cp = calloc(n1 + 8, sizeof(CPath)); int m = 0, cap = n1 + 8;
        for (int i = 0; i < n1; i++) { if (S1[i].n < 3) continue;
            McceBnd one = S1[i], *pc = NULL; int np = 0; mcce_wall_region(stock, 0, ctol, &one, &pc, &np);
            for (int k = 0; k < np; k++) { if (m == cap) { cap *= 2; cp = realloc(cp, cap * sizeof(CPath)); } cp[m].xy = pc[k].xy; cp[m].n = pc[k].n; m++; }
            free(pc); }
        mreg.p = cp; mreg.n = m; }
    int okall = 1, down = 0;
    if (!mreg.n) { okall = 0; snprintf(op->error, sizeof op->error, "Peel Mill: the motion region vanishes with the wall stock"); }
    double zl[200]; int nr_dc = 1; double st_dc = 0; int nz = hst_depths(p, zl, 200, &nr_dc, &st_dc);
    double fp = p->top_z + p->feed_plane;
    if (okall) {
        /* one engine call per wall-stock piece, then one copy per further level of the whole motion (PackageSomething l.5766-5821, work items) */
        P->depth = zl[0];
        McceRec *r = NULL; int nrec = 0;
        for (int i = 0; i < mreg.n && okall; i++) {
            McceBnd bd = { mreg.p[i].xy, mreg.p[i].n, 0, 0 };
            McceRec *rc = NULL; int nc = 0; char err[160] = "";
            if (!mcce_calc_dyn_pock(P, &bd, 1, S2, n2, NULL, 0, &rc, &nc, err, sizeof err)) { snprintf(op->error, sizeof op->error, "%s", err); okall = 0; free(rc); break; }
            r = realloc(r, sizeof(McceRec) * (nrec + nc + 1)); memcpy(r + nrec, rc, sizeof(McceRec) * nc); nrec += nc; free(rc);
        }
        if (okall) {
            McceRec *cp = nz > 1 ? malloc(sizeof(McceRec) * (nrec ? nrec : 1)) : NULL; double d = 0;
            for (int k = 0; k < nz; k++) {
                if (k == 0) mcce_moves(b, p, r, nrec, fp, &down);
                else { d += k - 1 < nr_dc - 1 ? st_dc : p->mc_depth_fs;
                       for (int i = 0; i < nrec; i++) { cp[i] = r[i]; cp[i].z0 -= d; cp[i].z1 -= d; }
                       mcce_moves(b, p, cp, nrec, fp, &down); }
                if (down) { V3 l = lastp(b); mv(b, 1, l.x, l.y, fp, 0); down = 0; }
            }
            free(cp);
        }
        free(r);
    }
    cl_free(&mreg);
    if (pa) { for (int i = 0; i < nao; i++) free(ao[i].xy); for (int i = 0; i < nbo; i++) free(bo[i].xy); }
    free(ao); free(bo); free(mot.xy); free(mat.xy);
    if (!okall) return 0;
    if (!b->n) { snprintf(op->error, sizeof op->error, "nothing to cut"); return 0; }
    { V3 e = lastp(b); mv(b, 1, e.x, e.y, p->clearance, 0); }
    return 1;
}

/* batch 9 (formats/opdlg_port_audit.md 13): the op record the 2D High Speed engines read (LoadDynPockParm FUN_1801af380 /
   the McHmm macro FUN_1802a1670): the stored op's record, or for an op made here Mastercam's default op of this type
   (Mill_MM / Mill_Inch.mcam-defaults, the one cam_defaults read; none: an all-zero record = LoadDynPockParm's own
   fallbacks) with this op's style, region choice and tool; then the op dialog's record patches (core_ui cui_opdlg.c
   pf_apply: the 2D HST page fields, record 0x1228.. and the Dynamic Contour side 0x5d2) over it - Mastercam's pages write
   the same fields into the op (HST2DCutParametersPageBase / HSTEntryMotionPage UpdateLocalOp, decomp/mcmill_all.c), which
   the engines then read. An op without such patches reads exactly the record as before (stored ops regenerate
   unchanged). Height patches (rpatch .height) are not engine inputs here. Caller frees. */
static int hst_patch_off(int off) { return off >= 0x1228 || off == 0x5d2 || (off >= 0x320 && off < 0x348); }   /* batch 10: LoadDynPockParm
                                                    also reads the Line/Arc filter's tolerance 0x320 when 0x33a is on (mcce_dyn.c 724) */
static uint8_t *hst_record(const McamFile *f, const CamParams *p, const Op **fo_out, uint32_t *n_out) {
    const Op *fo = NULL;
    if (p->file_op_id > 0) for (int i = 0; i < f->nops; i++) if (f->ops[i].id == p->file_op_id && f->ops[i].raw) { fo = &f->ops[i]; break; }
    uint8_t *rec; uint32_t rn;
    if (fo) { rn = fo->raw_n; rec = malloc(rn ? rn : 1); memcpy(rec, fo->raw, rn); }
    else {
        uint32_t dn = 0; const uint8_t *dr = cam_default_op_record(p->type, mcam_is_metric(f), &dn);
        rn = dr && dn > 0x1800 ? dn : 0x1800;
        rec = calloc(rn, 1);
        if (dr) memcpy(rec, dr, dn < rn ? dn : rn);
    }
    for (int k = 0; k < p->nrpatch; k++) {
        int o = p->rpatch[k].off, m = p->rpatch[k].n;
        if (!p->rpatch[k].height && hst_patch_off(o) && (uint32_t)(o + m) <= rn) memcpy(rec + o, p->rpatch[k].v, m);
    }
    if (!fo && !cam_type_is_3d(p->type)) {   /* srough: not ops 10 / 5 either (their 0x1228 is the surface block) */   /* op 134 only (op 132's 0x1228 is the stepdown) */
        rec[0x1228] = p->type == CAM_DYNCONTOUR ? 2 : p->type == CAM_PEEL ? 3 : p->type == CAM_AREA ? 1 : 0;
        rec[0x1567] = p->dyn_outside ? 1 : 0;
    }
    if (!fo) { memcpy(rec + 0x350, &p->tool_dia, 8); memcpy(rec + 0x358, &p->corner_r, 8); }   /* P10005 tool dia, corner radius */
    if (!fo) {   /* regen-consistency 2026-11-19: and the op's feed / plunge / retract rate / spindle speed as the save writes them into the
                    op's tool block (mcam_saveops.c: blk +0x30 / +0x38 / +0x40 / +0x48 = record 0x378 / 0x380 / 0x388 / 0x390) - the engine
                    reads its feeds from these (mcce_parm_from_record: OPD(0x310) feed, first-pass / ramp / back feeds from it); a new op
                    cut with the default op's feed where the saved op cut with its own */
        int32_t rpm = p->rpm;
        memcpy(rec + 0x378, &p->feed, 8); memcpy(rec + 0x380, &p->plunge_feed, 8); if (p->mc_rrate > 0) memcpy(rec + 0x388, &p->mc_rrate, 8); memcpy(rec + 0x390, &rpm, 4);
    }
    if (fo_out) *fo_out = fo;
    *n_out = rn;
    return rec;
}
static int hst_has_patch_at(const CamParams *p, int off) {
    for (int k = 0; k < p->nrpatch; k++) if (!p->rpatch[k].height && p->rpatch[k].off == off) return 1;
    return 0;
}
static int hst_has_patches(const CamParams *p) {
    for (int k = 0; k < p->nrpatch; k++) if (!p->rpatch[k].height && hst_patch_off(p->rpatch[k].off)) return 1;
    return 0;
}

/* decisions batch dec2 (Q2, ours): Peel Mill and Dynamic Contour stay on by default, but no Mastercam NCI of either exists to
   compare against (formats/opdlg_port_audit.md "HST engines (post-batch 9)" H.3: self-consistency and gouge checks only) - the
   op carries this note (CamOp.warning: the status line, cam_cli "WARNING:"; the dialog shows it as an "(ours)" marker) */
#define HST_UNVERIFIED_TEXT "unverified (no Mastercam NCI to compare) - checked only for gouges / wall stock here (ours)"
static void hst_unverified(CamOp *op, const char *what) {
    if (!op->warning[0]) snprintf(op->warning, sizeof op->warning, "%s: " HST_UNVERIFIED_TEXT, what);
}
/* the polyline of a loop of XtEdges (lines; arcs at cam_chain_polyline's step) */
static CPath xt_loop_path(const XtEdge *e, int n, double tol) {
    CPath o = { malloc(sizeof(double) * 2 * 64), 0 }; int cap = 64;
#define XP(X, Y) do { if (o.n == cap) { cap *= 2; o.xy = realloc(o.xy, sizeof(double) * 2 * cap); } \
                      if (!o.n || fabs(o.xy[2 * o.n - 2] - (X)) > 1e-9 || fabs(o.xy[2 * o.n - 1] - (Y)) > 1e-9) { o.xy[2 * o.n] = (X); o.xy[2 * o.n + 1] = (Y); o.n++; } } while (0)
    for (int i = 0; i < n; i++) {
        if (e[i].type != 4) { XP(e[i].a[0], e[i].a[1]); XP(e[i].b[0], e[i].b[1]); continue; }
        double step = e[i].r > tol ? 2 * acos(1 - tol / e[i].r) : M_PI / 8;
        if (step > M_PI / 18) step = M_PI / 18;
        int k = (int)ceil(fabs(e[i].sw) / step); if (k < 2) k = 2;
        for (int j = 0; j <= k; j++) { double a = e[i].a0 + e[i].sw * j / k; XP(e[i].c[0] + e[i].r * cos(a), e[i].c[1] + e[i].r * sin(a)); }
    }
#undef XP
    if (o.n > 1 && fabs(o.xy[0] - o.xy[2 * o.n - 2]) < 1e-6 && fabs(o.xy[1] - o.xy[2 * o.n - 1]) < 1e-6) o.n--;
    return o;
}

/* mcmill's machining regions, in its order, chain direction kept (chain_to_pock_bound decomp/mccore_pock.c l.1451; MCCECalcOffset keeps the
   input orientation; the stored clockwise chains regenerate unchanged in the DLL, audit_2026-11-09). Both Dynamic Mill cases, from outside
   and stay inside: CHST2DProcess::PrepareChains (decomp/mcmill_hst2d.c l.6120-6205): the role-0 chains (this+0x10) -> ConvertChainsToPockBounds
   -> list this+0x328, one pock_bound per chain, each chain's bound put in front of the list built so far (l.2276-2470, LAB_1802a0417) =
   reverse chain order; a bound whose length (+0x48) is below tolerance is dropped (param_3 true, l.2337; record 0x1288).
   PackageChains (l.4986-5398; no containment / air chains): until no bound is left, FUN_1802a7cf0 (l.15138) = the first bound of the
   list with the highest z (+0x18, greater by 1e-12), run as a region of its own (PackageMachiningRegion l.5400; no nesting test on
   this path; HasOuterPocket l.5068 only changes a nested region's top of stock: outer_pockets). A "Solid Chain" (formats/mcam.md: its edges are NOT DECODED) gives every loop of the
   solid's top face in XT loop order (cam_solid_face_loops; CORRELATED with buckle-v6/7/8 op 31's NCI), each loop's XT lines / arcs as the
   bound's entities (dyn_ents_add_xt; MCCE_XTENTS=0 = the old polyline): op 31 x3 regenerate move for move (formats/nci_save.md 7.3). *zout = each region's z (malloc); *zbase = the first chain's z;
   *lpos = each region's position in the bound list +0x328 (malloc; outer_pockets needs the list order). */
/* The engine input as mcmill builds it: ConvertChainsToPockBounds (decomp/mcmill_hst2d.c l.2276-2470) makes one pock_bound per chain from
   its entities - lines and arcs as they are (a full-circle single-arc chain split in two halves) - and the region / island offsets of
   FUN_1801ac7e0 (MCCECalcOffset) work on those entities; MCCECalcDynPock gets the result (formats/dynamic.md "2D HS input": the DLL on
   buckle-v6 op 33's region as true arcs reproduces the stored NCI, p95 0.000 mm, as 15-degree chords 0.059 mm). The bounds here keep
   that entity form next to their polylines (McceBnd.ne): looked up by the polyline (cam_chain_polyline of the same chain). */
typedef struct { const double *xy; int ne; char *ety; double *ent; } DynEnts;
static _Thread_local DynEnts *g_dyn_ents; static _Thread_local int g_dyn_nents;
static void dyn_ents_clear(void) { for (int i = 0; i < g_dyn_nents; i++) { free(g_dyn_ents[i].ety); free(g_dyn_ents[i].ent); } free(g_dyn_ents); g_dyn_ents = NULL; g_dyn_nents = 0; }
static void dyn_ents_add(const McamFile *f, const Chain *c, const double *xy) {   /* only chains of untrimmed lines / arcs; else the polyline stays */
    if (!c->n || c->cut0 > 1e-12 || c->cut1 > 1e-12 || c->solid_idn > 0) return;
    for (int i = 0; i < c->n; i++) { int t = f->ents[c->ent[i]].type; if (t != E_LINE && t != E_ARC) return; }
    int full = c->n == 1 && f->ents[c->ent[0]].type == E_ARC && fabs(fabs(f->ents[c->ent[0]].sw) - 2 * M_PI) < 1e-9;
    int ne = full ? 2 : c->n; if (ne <= 0 || ne > 1000000) return; char *ty = malloc((size_t)ne); double *v = malloc(sizeof(double) * 5 * (size_t)ne);
    for (int i = 0, k = 0; i < c->n; i++) {
        const Ent *E = &f->ents[c->ent[i]]; int r = c->rev[i];
        if (E->type == E_LINE) { V3 u = r ? E->b : E->a, w = r ? E->a : E->b; ty[k] = 'L'; double *q = v + 5 * k++; q[0] = u.x; q[1] = u.y; q[2] = w.x; q[3] = w.y; q[4] = 0; continue; }
        double sa = r ? E->a0 + E->sw : E->a0, sw = r ? -E->sw : E->sw;
        for (int h = 0; h < (full ? 2 : 1); h++) { ty[k] = 'A'; double *q = v + 5 * k++; q[0] = E->c.x; q[1] = E->c.y; q[2] = E->r;
            q[3] = full ? sa + h * sw / 2 : sa; q[4] = full ? sw / 2 : sw; }
    }
    g_dyn_ents = realloc(g_dyn_ents, sizeof *g_dyn_ents * (size_t)(g_dyn_nents + 1));
    g_dyn_ents[g_dyn_nents++] = (DynEnts){ xy, ne, ty, v };
}
/* a solid chain's face loop (XtEdge lines / arcs, cam_solid_face_loops) in the same entity form: ConvertChainsToPockBounds takes the chain's
   entities as they are (decomp/mcmill_hst2d.c l.2276-2470), a full-circle single-arc loop in two halves (as dyn_ents_add) */
static void dyn_ents_add_xt(const XtEdge *e, int n, const double *xy) {
    if (n <= 0 || n > 1000000) return;
    for (int i = 0; i < n; i++) if (e[i].type != 2 && e[i].type != 4) return;
    int full = n == 1 && e[0].type == 4 && fabs(fabs(e[0].sw) - 2 * M_PI) < 1e-9;
    int ne = full ? 2 : n; char *ty = malloc((size_t)ne); double *v = malloc(sizeof(double) * 5 * (size_t)ne);
    for (int i = 0, k = 0; i < n; i++) {
        if (e[i].type == 2) { ty[k] = 'L'; double *q = v + 5 * k++; q[0] = e[i].a[0]; q[1] = e[i].a[1]; q[2] = e[i].b[0]; q[3] = e[i].b[1]; q[4] = 0; continue; }
        for (int h = 0; h < (full ? 2 : 1); h++) { ty[k] = 'A'; double *q = v + 5 * k++; q[0] = e[i].c[0]; q[1] = e[i].c[1]; q[2] = e[i].r;
            q[3] = full ? e[i].a0 + h * e[i].sw / 2 : e[i].a0; q[4] = full ? e[i].sw / 2 : e[i].sw; }
    }
    g_dyn_ents = realloc(g_dyn_ents, sizeof *g_dyn_ents * (size_t)(g_dyn_nents + 1));
    g_dyn_ents[g_dyn_nents++] = (DynEnts){ xy, ne, ty, v };
}
static void dyn_ents_of(McceBnd *b) {   /* the entities of the polyline b->xy, if registered (shared, not copied) */
    for (int i = 0; i < g_dyn_nents; i++) if (g_dyn_ents[i].xy == b->xy) { b->ne = g_dyn_ents[i].ne; b->ety = g_dyn_ents[i].ety; b->ent = g_dyn_ents[i].ent; return; }
}
static CPaths facing_regions(const McamFile *f, CamOp *op, double tolP, double **zout, double *zbase, int **lpos) {
    int cap = op->nchains + 8, n = 0, have0 = 0;
    CPath *L = calloc(cap, sizeof(CPath)); double *Z = calloc(cap, sizeof(double));
    *zbase = 0;
    for (int i = 0; i < op->nchains; i++) {
        const Chain *c = &op->chains[i];
        if (!c->closed || c->avoid) continue;
        if (c->solid_idn > 0) {
            XtEdge *e = NULL; int ln[256], nl = 0, ol = -1, ne = cam_solid_face_loops(f, c->solid_idn, &e, ln, 256, &nl, &ol);
            if (ne > 0) {
                for (int k = 0, k0 = 0; k < nl; k0 += ln[k], k++) {
                    if (n + 1 >= cap) { cap *= 2; L = realloc(L, cap * sizeof(CPath)); Z = realloc(Z, cap * sizeof(double)); }
                    L[n] = xt_loop_path(e + k0, ln[k], op->p.tol); Z[n] = ln[k] ? e[k0].a[2] : 0;
                    if (!have0) { *zbase = Z[n]; have0 = 1; }
                    if (L[n].n >= 3) { if (getenv("MCCE_XTENTS") == NULL || *getenv("MCCE_XTENTS") != '0') dyn_ents_add_xt(e + k0, ln[k], L[n].xy); n++; } else free(L[n].xy);
                }
                free(e);
                continue;
            }
            free(e);
        }
        double *xy, z; int m = cam_chain_polyline(f, c, op->p.tol, &xy, &z);
        if (m < 3) { free(xy); continue; }
        if (!have0) { *zbase = z; have0 = 1; }
        { double per = 0; for (int k = 0; k < m; k++) { int j = (k + 1) % m; per += hypot(xy[2 * j] - xy[2 * k], xy[2 * j + 1] - xy[2 * k + 1]); }
          if (per < tolP) { free(xy); continue; } }
        if (n + 1 >= cap) { cap *= 2; L = realloc(L, cap * sizeof(CPath)); Z = realloc(Z, cap * sizeof(double)); }
        L[n].xy = xy; L[n].n = m; Z[n] = z; dyn_ents_add(f, c, xy); n++;
    }
    CPaths out = { calloc(n + 1, sizeof(CPath)), 0 };
    *zout = calloc(n + 1, sizeof(double));
    *lpos = calloc(n + 1, sizeof(int));
    char *done = calloc(n + 1, 1);
    for (int it = 0; it < n; it++) {   /* the list is L reversed; take its first bound of the highest z */
        int best = -1;
        for (int k = n - 1; k >= 0; k--) if (!done[k] && (best < 0 || Z[best] + 1e-12 < Z[k])) best = k;
        done[best] = 1; out.p[out.n] = L[best]; (*zout)[out.n] = Z[best]; (*lpos)[out.n] = n - 1 - best; out.n++;
    }
    free(done); free(L); free(Z);
    return out;
}

/* Dynamic Mill trim to rest material (record 0x17b8, CHST2DRestMaterial): the rest data S2 per depth (user decision N9-18).
   FUN_1802ba2c0 (hst2d l.15475; args: disassembly of PackageChains 0x1802b0xxx): rest +0x08 = tol (0x1288), +0x10 = this+0x378 (2 D when style < 2 && 0x1567, SetParams l.9334, else 0),
   +0x18 = this+0x380 (0x16f0 signed by 0x16fd, l.9338-9351), +0x20 = D, +0x28 = the op, +0x2c = this+0x69 = 0x16fe, +0x30 = this+0x80 = 0x16f8, +0x48 = this+0x158 = 0x16fc (source), +0x49 = this+0x388.
   FUN_1802cb260 (mm l.486926-487373): map band bbox y of the region +- (tol + p7) grown by 4/7 of its height, cell 0.045 mm / 0.00177 in; the earlier ops swept into `cut`
   (verify_2d_operation(z + nc_tol, rad_adj = this+0x380)); with p7 > 0 and source < 2 the outside band: MCCECalcOffset(1, D/2 + p7, largest, 0x3f) swept with radius D/2 + p7 (l.487233-487249);
   rest = fill(region) - cut (pbmap_fill_pock_bounds, pbmap_diff), traced with D/2 (pbmap_to_pbmap_bnds, the file written l.487266+), read back as line loops, sort_pock_bounds(0.025 / 0.001: FUN_1801aec30 mm l.299270).
   ours: the region is the polyline of the chain (Mastercam's: lines and arcs); +0x49 (only with air chains) is not used, air chains are refused. */
typedef struct { int on, src, grp, opid; double tolP, p7, p8, D; } HstRest;
static RmB *hst_rest_bound(const double *xy, int n) {
    RmB *b = rm_b_new(n);
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; RmEnt *e = &b->e[i]; memset(e, 0, sizeof *e); e->id = 'L'; e->a[0] = xy[2 * i]; e->a[1] = xy[2 * i + 1]; e->a[2] = xy[2 * j]; e->a[3] = xy[2 * j + 1]; }
    b->valid = 1; b->next = b->prev = NULL;
    rm_b_infos(b); rm_b_bboxs(b);
    return b;
}
/* one level; *out = line loops (malloc'd), n = 0 for no rest. 0 = refused (err) */
static int hst_rest_level(const McamFile *f, const CamOp *op, const CPath *reg, const HstRest *R, double z, int metric, McceBnd **out, int *nout, char *err, size_t errn) {
    *out = NULL; *nout = 0;
    double ymin = 1e38, ymax = -1e38, cell = metric ? 0.045 : 0.0017716535433070866;   /* the constants of FUN_1802cb260: 0x3FA70A3D70A3D70A / 0x3F5D06DA81D06DA8 */
    { const McSysTol *st = mc_systol_units(metric); rm_tol = st->tol; rm_nc_tol = g_nc_tol; rm_chain_tol = st->chain_tol; }   /* the rm_* globals (mccore tol / nc_tol / chain_tol), as cam2d_pock.c */
    for (int i = 0; i < reg->n; i++) { double y = reg->xy[2 * i + 1]; if (y < ymin) ymin = y; if (y > ymax) ymax = y; }
    double y0 = ymin - (R->tolP + R->p7), y1 = ymax + R->tolP + R->p7, w = (y1 - y0) * 0.5714285714285714; y0 -= w; y1 += w;
    RmMap cut, fill; memset(&cut, 0, sizeof cut); memset(&fill, 0, sizeof fill);
    rm_map_init(&cut, y0, y1, cell); rm_map_init(&fill, y0, y1, cell);
    CamPrevOp *pv = NULL; int npv = 0;
    if (!g_prev_ops_fn || !g_prev_ops_fn(f, op, &pv, &npv)) { snprintf(err, errn, "Trim to rest material: the operation list is not available here"); rm_map_free(&cut); rm_map_free(&fill); return 0; }
    int bad = 0;
    for (int k = 0; k < npv && !bad; k++) {
        if (pv[k].pocket_1340) { snprintf(err, errn, "Trim to rest material: a previous Pocket has op +0x1340 = 1 (FUN_1802cb260 stop)"); bad = 1; break; }   /* mm l.487203, 0x1802cb757-0x1802cb765: opcode 3 and op +0x1340 == 1 (no test of the finish flag) */
        if (pv[k].nci) rm_verify_nci(pv[k].nci, pv[k].nci_n, z + g_nc_tol, R->p8, &cut, pv[k].xlate, pv[k].xv, 0.0);
        else { double rd = pv[k].dia * 0.5; if (0.0 <= rd + R->p8) rd += R->p8; rm_verify_moves(pv[k].m, pv[k].nm, z + g_nc_tol, rd, &cut); }   /* the radius rule of rm_verify_nci's 0x3f5 record */
    }
    for (int k = 0; k < npv; k++) free(pv[k].nci);
    free(pv);
    if (bad) { rm_map_free(&cut); rm_map_free(&fill); return 0; }
    RmB *rb = hst_rest_bound(reg->xy, reg->n);
    if (R->p7 > 0 && R->src < 2) {   /* the outside band (l.487233-487249): flags 0x3f (ours: the wall offset's arcs, no 0.57 deg corner fillet flag) */
        McceBnd one = { reg->xy, reg->n, 0, 0 }, *gb = NULL; int ng = 0;
        mcce_grow_islands(R->D * 0.5 + R->p7, fmax(g_nc_tol, R->D * 0.002), &one, 1, &gb, &ng);
        for (int k = 0; k < ng; k++) { RmB *b = hst_rest_bound(gb[k].xy, gb[k].n); rm_verify_bounds(b, R->D * 0.5 + R->p7, &cut); rm_b_free(b); free(gb[k].xy); }   /* xmm6 = D / 2 + p7 for both calls (0x1802cb82a-0x1802cb88a) */
        free(gb);
    }
    rm_fill_bounds(rb, &fill);
    rm_b_free(rb);
    if (getenv("MCCE_DEBUG")) { fprintf(stderr, "rest z %.3f: %d earlier ops, nci ", z, npv); fprintf(stderr, "cut area %.3f fill area %.3f\n", rm_map_area(&cut), rm_map_area(&fill)); }
    rm_map_diff(&fill, &cut, cell);
    RmBnd *bnds = NULL; RmB *restb = NULL;
    rm_map_trace(&fill, R->D * 0.5, 0, &bnds);
    if (bnds) { rm_bnds_to_bounds(&fill, bnds, &restb); short ng; rm_sort(&restb, 1, metric ? 0.025 : 0.001, &ng); }
    rm_bnds_free(bnds);
    if (restb) {
        int cap = 8; McceBnd *r = calloc(cap, sizeof(McceBnd));
        for (RmB *b = restb; b; b = b->next) {
            if (*nout == cap) { cap *= 2; r = realloc(r, cap * sizeof(McceBnd)); }
            double *xy = malloc(sizeof(double) * 2 * (b->n ? b->n : 1)); for (int i = 0; i < b->n; i++) { xy[2 * i] = b->e[i].a[0]; xy[2 * i + 1] = b->e[i].a[1]; }
            r[*nout].xy = xy; r[*nout].n = b->n; r[*nout].island = b->isl; r[*nout].open = 0; (*nout)++;
        }
        *out = r;
        rm_b_free_list(&restb);
    }
    rm_map_free(&cut); rm_map_free(&fill);
    return 1;
}
static void hst_rest_free(McceBnd *b, int n) { for (int i = 0; i < n; i++) free(b[i].xy); free(b); }
/* FUN_1801b1440 (mm l.300737): both files have the same loops (coordinates within its tolerance) */
static int hst_rest_same(const McceBnd *a, int na, const McceBnd *b, int nb, double tol) {
    if (na != nb) return 0;
    for (int i = 0; i < na; i++) { if (a[i].n != b[i].n) return 0; for (int k = 0; k < 2 * a[i].n; k++) if (fabs(a[i].xy[k] - b[i].xy[k]) > tol) return 0; }
    return 1;
}

/* The link from one machining region's motion to the next (decoded 2026-11-13). Mastercam writes each work item's records as a group
   behind a contour marker (OutputMotionToBnci, decomp/mcmill_hst2d.c l.4103-4630: bnci_wr_ctour_marker(group counter, 1, header +0x20,
   header depth, header top, ..) l.4497, the header = FUN_1802bd100 from FUN_18029f830, decomp/mcmill_hst2d_dyn.c l.2463: depth = the
   group's lowest z, top = top_of_stock, 0x18029ff9e-0x18029ffb5) and the depth writer FUN_180045770 (decomp/mcmill_hst2d.c
   l.13699-14820) links the groups. The group counter differs from group to group, so at a group's first move (l.14450-14530) the
   tool, if down and farther than nc_tol, leaves through set_heights(previous depth, previous depth) and clear_move (record 0x179
   clearance off or 0x319 off) else retract_move, then set_heights(depth, top) and plunge_move(start, z, param_5 = 0x179 off or
   0x319 off) (l.14533-14562). mccore (decomp/mccore_moves.c, pocket_b6/decomp_comp_pock_bnds_more.c set_heights 0x18089fa80):
   set_heights(d, t): init = feed incremental (0x198) ? feed + t : the absolute feed plane, never below d; retract = 0x189 on ?
   (incremental 0x188 ? value + t : absolute) : init; clearance likewise (0x179 / 0x178). clear_move 0x18089e6a0: feed up to init at the
   retract rate (0x388) unless rapid retract (0x1a9), then rapid to the clearance when on, else the retract when on, else (rapid
   retract) init. retract_move 0x18089f390: the same feed, then the retract when on, else the clearance when on, else init.
   plunge_move 0x18089ed00 (tool up): with param_5 a rapid across at the clearance when on (else the retract when on) if both init and
   z lie below it; then the rapid to max(init, z) and the plunge feed (0x380) - left to mcce_moves (its approach at fp = init).
   Only the region-to-region link is this here; the links inside a region (the engine's rapid records) keep the SAFE rule (mcce_moves). */
typedef struct { int clr_on, clr_end, rapid_ret; double rrate; } HstLink;
static double mrec_sx(const McceRec *e) { return e->id == 'A' ? e->cx + e->r * cos(e->sa) : e->x1; }
static double mrec_sy(const McceRec *e) { return e->id == 'A' ? e->cy + e->r * sin(e->sa) : e->y1; }
static int hst_ht_byte(const CamParams *p, const uint8_t *rec, uint32_t rn, int off) {   /* a height page flag: the dialog's patch, else the record */
    for (int k = p->nrpatch - 1; k >= 0; k--) if (p->rpatch[k].height && p->rpatch[k].off == off && p->rpatch[k].n >= 1) return p->rpatch[k].v[0] != 0;
    return (uint32_t)off < rn ? rec[off] != 0 : 0;
}
typedef struct { double init, ret, clr; int ret_on; } HstHts;
static HstHts hst_set_heights(const CamParams *pr, double top0, const HstLink *lk, double d, double t) {   /* mccore set_heights(d, t) on a region's heights; top0 = the op's top */
    HstHts h; double top = pr->top_z;
    h.init = (pr->ht_inc & 4) ? pr->feed_plane + t : top0 + pr->feed_plane;                  /* absolute: CamParams keeps it as value - the op's top */
    if (h.init < d) h.init = d;
    h.ret_on = pr->retract_z > -1e29;
    h.ret = h.ret_on ? ((pr->ht_inc & 2) ? (pr->retract_z - top) + t : pr->retract_z) : h.init;
    h.clr = lk->clr_on ? ((pr->ht_inc & 1) ? (pr->clearance - top) + t : pr->clearance) : h.ret;
    return h;
}
/* pp / dprev: the previous region's heights and its last group's depth; pn / dn: the next region's and its first group's depth, (sx, sy, z)
   its first point. The tool stands at the previous region's last point (its SAFE rise to the feed plane, if any, is taken back). */
/* Returns the index of the first move after the retract (the next region's section-start block goes before it: FUN_180045770 l.6732 writes
   wr_sof at a group's first record and cancels it only when the tool is still down there, l.6774 - here it was lifted), -1 = no link */
static int hst_group_link(MB *b, const CamParams *pp, double dprev, const CamParams *pn, double dn, double top0, const HstLink *lk, double sx, double sy, double z) {
    if (b->n >= 2) { const Move *up = &b->m[b->n - 1], *bot = &b->m[b->n - 2];
        if (up->rapid && fabs(up->p.x - bot->p.x) < 1e-9 && fabs(up->p.y - bot->p.y) < 1e-9 && up->p.z > bot->p.z) b->n--; }
    V3 e = lastp(b);
    if (hypot(e.x - sx, e.y - sy) <= g_nc_tol) return -1;                                     /* l.14462-14466: no link within nc_tol */
    HstHts h = hst_set_heights(pp, top0, lk, dprev, dprev);
    double zc = e.z;
    if (!lk->rapid_ret && zc + CAM_SYS_TOL < h.init) { if (lk->rrate > 0) mv(b, 0, e.x, e.y, h.init, lk->rrate); else mv(b, 1, e.x, e.y, h.init, 0); zc = h.init; }
    int clear = !lk->clr_on || !lk->clr_end;
    if (clear) {                                                                                /* clear_move */
        if (lk->clr_on) { if (zc + CAM_SYS_TOL < h.clr) { mv(b, 1, e.x, e.y, h.clr, 0); zc = h.clr; } }
        else if (h.ret_on) { if (zc + CAM_SYS_TOL < h.ret) { mv(b, 1, e.x, e.y, h.ret, 0); zc = h.ret; } }
        else if (lk->rapid_ret && zc + CAM_SYS_TOL < h.init) { mv(b, 1, e.x, e.y, h.init, 0); zc = h.init; }
    } else {                                                                                    /* retract_move */
        if (h.ret_on) { if (zc + CAM_SYS_TOL < h.ret) { mv(b, 1, e.x, e.y, h.ret, 0); zc = h.ret; } }
        else if (lk->clr_on) { if (zc + CAM_SYS_TOL < h.clr) { mv(b, 1, e.x, e.y, h.clr, 0); zc = h.clr; } }
        else if (zc + CAM_SYS_TOL < h.init) { mv(b, 1, e.x, e.y, h.init, 0); zc = h.init; }
    }
    int t0 = b->n;
    HstHts g = hst_set_heights(pn, top0, lk, dn, pn->top_z);                                        /* set_heights(depth, top) */
    if (lk->clr_on) { if (g.init + CAM_SYS_TOL < g.clr && z + CAM_SYS_TOL < g.clr && clear) mv(b, 1, sx, sy, g.clr, 0); }   /* plunge_move, tool up */
    else if (g.ret_on && g.init + CAM_SYS_TOL < g.ret && z + CAM_SYS_TOL < g.ret && clear) mv(b, 1, sx, sy, g.ret, 0);
    /* the rapid down to max(init, z) and the plunge feed: mcce_moves' approach (its fp is this init) */
    return t0;
}

/* The top of stock of a region nested in another machining region (decoded 2026-11-13, mcmill.dll PackageChains, disassembly
   0x1802b05ae-0x1802b0701, the arguments of the CHST2DDepthCuts setter FUN_1802ca970 that the decompile drops, decomp/mcmill_hst2d.c
   l.5066-5077): before the region's depth list is set, CHST2DProcess::HasOuterPocket(region, list) (0x1802a9af0, l.3124-3146) walks the
   bound list +0x328 - the bounds not yet run, with each bound already run put back at the FRONT (l.5005-5020 unlinks the region,
   l.5348-5355 relinks it before the others) - and returns the innermost bound the region lies in: b when mccore IsContourNested(region,
   b, tol +0x48 = record 0x1288) and (no b yet or IsContourNested(b, best)). When one is found the setter gets top-incremental = 0
   (dil cleared, 0x1802b05d1) and top = b +0x18 (b's z) - the view z of the plane origin this+0xd0 (0x1802b05d4-0x1802b0605), so
   FUN_1802ca790 (decomp/mcmill_all.c l.486548) makes top_of_stock = b's z exactly; else this+0x110 / +0x118 (the op's own top).
   top_of_stock then sets the feed plane / retract / clearance when incremental (FUN_1802ca790 l.486553-486573), the depth list
   (FUN_1802ca490 from FUN_1802ca5f0, decomp/mcmill_hst2d.c l.15556 / 15565), the engine's P+0x78 (PackageSomething reads it at
   0x1802b201e into xmm11 = the work item's param_6, call 0x1802b262d) and the group header's top that the depth writer
   FUN_180045770 approaches each group with (FUN_18029f830 -> FUN_1802bd100 param_7, 0x18029ffa4; set_heights(depth, top) l.13945,
   plunge_move to init_depth). buckle-v6/7/8 op 31: the holes of each solid's top face are regions inside its outer loop, so they
   are approached at their z + 3 = -0.5 instead of the op's top + 3. otop[r] = that z, or NAN (no outer pocket). */
static void outer_pockets(const CPaths *outs, const double *Z, const int *lpos, double tolP, int metric, double *otop) {
    int n = outs->n;
    { const McSysTol *st = mc_systol_units(metric); rm_tol = st->tol; rm_nc_tol = g_nc_tol; rm_chain_tol = st->chain_tol; }   /* the mccore globals IsContourNested reads */
    RmB **B = calloc(n + 1, sizeof(RmB *));
    for (int r = 0; r < n; r++) B[r] = hst_rest_bound(outs->p[r].xy, outs->p[r].n);
    int *it = malloc(sizeof(int) * (n + 1));
    for (int r = 0; r < n; r++) {
        int m = 0;
        for (int k = r - 1; k >= 0; k--) it[m++] = k;                                           /* the bounds already run, the last run first */
        for (int q = 0; q <= n; q++) for (int k = r + 1; k < n; k++) if (lpos[k] == q) it[m++] = k;   /* the rest in list order */
        int best = -1;
        for (int j = 0; j < m; j++) {
            int k = it[j];
            if (rm_contour_nested(B[r], B[k], tolP) && (best < 0 || rm_contour_nested(B[k], B[best], tolP))) best = k;
        }
        otop[r] = best >= 0 ? Z[best] : NAN;
    }
    free(it);
    for (int r = 0; r < n; r++) rm_b_free(B[r]);
    free(B);
}

static int gen_dynamic_mcce(const McamFile *f, CamOp *op, MB *b) {
    CamParams *p = &op->p;
    McceParm P; memset(&P, 0, sizeof P);
    const Op *fo = NULL;
    int metric = mcam_is_metric(f);
    int dc_fin = 0;
    uint32_t rn = 0; uint8_t *rec = hst_record(f, p, &fo, &rn);
    if (p->type == CAM_PEEL) peel_forced_writes(rec, rn);
    mcce_parm_from_record(&P, rec, rn, metric);
    dc_fin = rn > 0x1241 && rec[0x1241];   /* Dynamic Contour / Peel finish pass on (HSTFinCtourPage 3301 3310 / 2833 2916) */
    /* values the operation dialog edits (CamParams; the same op record fields, cam_fileop.c params_from_record) */
    double D = p->tool_dia;
    P.R = D * 0.5;
    { double s = D * p->stepover_pct / 100, x = 1 - s / P.R; if (x < -1) x = -1; if (x > 1) x = 1; P.eng = acos(x); }
    P.min_rad = p->min_rad_pct * 0.01 * D;
    P.micro_lift = p->micro_lift;
    P.f_cut = p->feed; P.f_back = p->back_feed > 1e-5 ? p->back_feed : p->feed;
    if (P.f_ramp <= 0) P.f_ramp = p->feed;
    if (P.f_first <= 0) P.f_first = p->feed;
    if (P.f_conv <= 0) P.f_conv = p->feed;
    if (p->ramp_deg > 0) P.plunge_ang = p->ramp_deg * 0.01745329251994329;
    if (p->link_retract > 0 && p->type != CAM_PEEL) P.gap = p->link_retract;   /* Peel: forced 10 % (peel_forced_writes) */
    P.top = p->top_z;
    if (p->type == CAM_PEEL) { int ok = gen_peel_mcce(f, op, b, &P, rec, rn, dc_fin); free(rec); if (ok) hst_unverified(op, "Peel Mill"); return ok; }
    double tolP = rdd_rec(rec, rn, 0x1288); if (tolP < 1e-12) tolP = metric ? 0.025 : 0.001;   /* SetParams l.9241-9250: ConvertChainsToPockBounds' tolerance */
    HstRest rest; memset(&rest, 0, sizeof rest);
    if (p->type == CAM_DYNAMIC && rn > 0x17b8 && rec[0x17b8]) {   /* this+0x58 = style < 2 && 0x17b8 */
        rest.on = 1; rest.src = rec[0x16fc]; rest.grp = rec[0x16fe]; memcpy(&rest.opid, rec + 0x16f8, 4); rest.tolP = tolP; rest.D = D;
        rest.p7 = rec[0x1567] ? 2.0 * D : 0.0;                                                  /* SetParams l.9334 this+0x378 (this+0x55) */
        double v = rdd_rec(rec, rn, 0x16f0);                                                    /* this+0x380: 0x16fd 1 / 2 with a value above tol */
        rest.p8 = rec[0x16fd] == 1 && v > CAM_SYS_TOL ? v : rec[0x16fd] == 2 && v > CAM_SYS_TOL ? -v : 0.0;
        if (rest.src > 1) { free(rec); snprintf(op->error, sizeof op->error, "Trim to rest material from a tool (FUN_1802cbdd0, record 0x16fc = 2) is NOT DECODED here; not generated"); return 0; }
        if (rest.src == 1) { free(rec); snprintf(op->error, sizeof op->error, "Trim to rest material from one chosen operation (record 0x16fc = 1) is NOT DECODED here; not generated"); return 0; }
        if (rest.grp) { free(rec); snprintf(op->error, sizeof op->error, "Trim to rest material within the toolpath group (record 0x16fe) is NOT DECODED here; not generated"); return 0; }
    }
    HstLink lk = { hst_ht_byte(p, rec, rn, 0x179), hst_ht_byte(p, rec, rn, 0x319), hst_ht_byte(p, rec, rn, 0x1a9), rdd_rec(rec, rn, 0x388) };   /* group link flags (hst_group_link) */
    free(rec);
    if (p->type == CAM_DYNCONTOUR) { int ok = gen_dyncontour_mcce(f, op, b, &P, dc_fin); if (ok) hst_unverified(op, "Dynamic Contour"); return ok; }

    int facing = (P.flags & 0x40) != 0;   /* style 0 && record 0x1567 = FUN_1801ac7e0's "from outside" (LoadDynPockParm) */
    double tol = fmax(p->tol, D * 0.002), *Zr = NULL, zbase = 0; int *Lp = NULL;
    dyn_ents_clear();                                                                          /* (no stale polyline keys) */
    CPaths outs = facing_regions(f, op, tolP, &Zr, &zbase, &Lp);
    (void)zbase;
    if (!outs.n) { cl_free(&outs); free(Zr); free(Lp); snprintf(op->error, sizeof op->error, "needs closed chains"); return 0; }
    double *Otop = malloc(sizeof(double) * (outs.n + 1));
    outer_pockets(&outs, Zr, Lp, tolP, metric, Otop);
    free(Lp);
    /* islands: every raw avoidance chain (chain direction kept, list +0x330) grown on its own by FUN_1801ac7e0 with the stock, the
       same list in every work item (l.5787, vtable +0x18); a chain shorter than the tolerance is dropped (ConvertChains param_3) */
    McceBnd *isl = NULL; int nisl = 0;
    {   double zav; CPaths raw = chain_paths(f, op, 2, &zav);
        /* the same chains in chain_paths' order (cam2d.c: closed, avoidance, at least 2 points) */
        for (int i = 0, k = 0; i < op->nchains && k < raw.n; i++) {
            const Chain *c = &op->chains[i]; if (!c->closed || !c->avoid) continue;
            double *t, tz; int tn = cam_chain_polyline(f, c, op->p.tol, &t, &tz); free(t); if (tn < 2) continue;
            dyn_ents_add(f, c, raw.p[k++].xy);
        }
        McceBnd *rb = calloc(raw.n + 1, sizeof(McceBnd)); int m = 0;
        for (int i = 0; i < raw.n; i++) { double per = 0; for (int k = 0; k < raw.p[i].n; k++) { int j = (k + 1) % raw.p[i].n; per += hypot(raw.p[i].xy[2 * j] - raw.p[i].xy[2 * k], raw.p[i].xy[2 * j + 1] - raw.p[i].xy[2 * k + 1]); }
            if (per < tolP) continue; rb[m].xy = raw.p[i].xy; rb[m].n = raw.p[i].n; dyn_ents_of(&rb[m]); m++; }
        mcce_grow_islands(p->stock_xy, tol, rb, m, &isl, &nisl);
        free(rb); cl_free(&raw); }
    int down = 0, okall = 1, any_skip = 0, any_region = 0; double end_clear = p->clearance;
    CamParams prevp = *p; double prevd = 0; int have_prev = 0;                                 /* the last region's heights / depth (hst_group_link) */
    McceBnd *sub = calloc(nisl + 2, sizeof(McceBnd));
    for (int r = 0; r < outs.n && okall; r++) {
        /* depths per bound: FUN_1802ca5f0(dc, bound z +0x18) (PackageChains l.5080) -> FUN_1802ca790 (mcmill_all.c l.486527): the bound's
           z replaces the chain z for incremental depth (+0x60) and incremental top (+0x80); non-incremental: the op's own values */
        CamParams pr = *p;
        /* FUN_1802ca790 mm l.486527: the depth (+0x60) and top (+0x80) incremental on the bound's z (as set_depths, cam2d.c gen_contour3d: dep_cz = the z
           they were read with); the feed plane (+0x90), retract (+0xa0), clearance (+0xb0) absolute, or incremental on that top (l.486554-486573) */
        pr.final_z = p->dep_inc ? Zr[r] + (p->final_z - p->dep_cz) : p->final_z;
        pr.top_z = p->top_inc ? Zr[r] + (p->top_z - p->dep_cz) : p->top_z;
        if (!isnan(Otop[r])) pr.top_z = Otop[r];                                               /* inside another region: its z, absolute (outer_pockets) */
        double dtop = pr.top_z - p->top_z;
        if (p->ht_inc & 1) pr.clearance = p->clearance + dtop;
        if ((p->ht_inc & 2) && p->retract_z > -1e29) pr.retract_z = p->retract_z + dtop;
        double zl[200]; int nr_dc = 1; double st_dc = 0; int nz = hst_depths(&pr, zl, 200, &nr_dc, &st_dc);
        double fp = (p->ht_inc & 4) ? pr.top_z + p->feed_plane : p->top_z + p->feed_plane;
        if (fp < pr.final_z) fp = pr.final_z;                                              /* l.486563-486564 */
        if (fabs(fp - pr.final_z) < CAM_SYS_TOL) { fp = pr.clearance; if (pr.retract_z > fp) fp = pr.retract_z; }   /* l.486574-486578 */
        end_clear = pr.clearance;
        P.top = pr.top_z;
        P.depth = zl[0];
        /* the wall offset of the region: FUN_1801ac7e0 (d = stock + vtable +0x38 FUN_1802a70d0, dyn l.483-500: 0 without 0x17b8) */
        McceBnd reg = { outs.p[r].xy, outs.p[r].n, 0, 0, 0, NULL, NULL }, *pc = NULL; int np = 0; dyn_ents_of(&reg);
        double dwall = p->stock_xy;                                                            /* + CHST2DDynamic vtable +0x38 FUN_1802a70d0 dyn l.483-500: style 0, 0x17b8 and 0x1567 */
        if (rest.on && facing) { dwall += 2.0 * tolP + 1.5 * D; if (rest.p8 < 0) dwall += fabs(rest.p8); }
        mcce_wall_region(dwall, facing, tol, &reg, &pc, &np);
        /* rest data: FUN_1802b77a0 (hst2d l.14854): the deepest level (FUN_1802ca590(dc, count)); an empty level -> *param_7 = 1 (mm l.487264-487266, hst2d l.15131) and PackageChains skips this region
           (0x1802b0882-0x1802b088b, 0x1802b0adc-0x1802b0adf), warning 0xb / error 10 at the end (l.5388-5394). One level: that S2. Else level 1: empty -> skipped; same loops as the deepest (FUN_1801b1440) -> one S2,
           the copies stay; different -> a table of the levels (FUN_1802aa210, -2 = unknown), 0 and n - 1 known, FUN_1802a82c0 takes the middle of the first run of unknown levels (lo = just before it, hi = just after it):
           its rest (empty -> skipped), same as lo's -> the levels mid .. lo + 1 share lo's (FUN_1802ae390), else same as hi's -> mid .. hi - 1 share hi's, else its own (FUN_1802ba9d0) */
        McceBnd **s2l = NULL; int *s2n = NULL, *s2of = NULL, per_level = 0, skip = 0;
        if (rest.on) {
            double same_tol = metric ? 0.0025 : 0.0001;   /* FUN_1801b1440 mm l.300798-300804 */
            s2l = calloc(nz, sizeof *s2l); s2n = calloc(nz, sizeof *s2n); s2of = malloc(nz * sizeof *s2of); for (int k = 0; k < nz; k++) s2of[k] = k;
            char e2[200] = "";
#define RL(lev) hst_rest_level(f, op, &outs.p[r], &rest, zl[lev], metric, &s2l[lev], &s2n[lev], e2, sizeof e2)
            if (!RL(nz - 1)) { snprintf(op->error, sizeof op->error, "%s", e2); okall = 0; }
            else if (!s2n[nz - 1]) skip = 1;
            else if (nz > 1) {
                if (!RL(0)) { snprintf(op->error, sizeof op->error, "%s", e2); okall = 0; }
                else if (!s2n[0]) skip = 1;
                else if (hst_rest_same(s2l[0], s2n[0], s2l[nz - 1], s2n[nz - 1], same_tol)) s2of[0] = nz - 1;   /* the deepest level's data stays */
                else {
                    per_level = 1;
                    int *st = malloc(nz * sizeof(int)); for (int k = 0; k < nz; k++) st[k] = -2; st[0] = 0; st[nz - 1] = nz - 1;
                    for (;;) {
                        int run = -1; for (int k = 1; k < nz; k++) if (st[k] == -2) { run = k; break; }
                        if (run < 0) break;
                        int end = run; while (end + 1 < nz && st[end + 1] == -2) end++;
                        int lo = run - 1, hi = end + 1, mid = run, q = end; while (mid < q) { mid++; q--; }
                        if (!RL(mid)) { snprintf(op->error, sizeof op->error, "%s", e2); okall = 0; break; }
                        if (!s2n[mid]) { skip = 1; break; }
                        if (hst_rest_same(s2l[mid], s2n[mid], s2l[st[lo]], s2n[st[lo]], same_tol)) { for (int k = lo + 1; k <= mid; k++) st[k] = st[lo]; }
                        else if (hst_rest_same(s2l[mid], s2n[mid], s2l[st[hi]], s2n[st[hi]], same_tol)) { for (int k = mid; k < hi; k++) st[k] = st[hi]; }
                        else st[mid] = mid;
                    }
                    if (okall && !skip) for (int k = 0; k < nz; k++) s2of[k] = st[k];
                    free(st);
                }
            }
#undef RL
            if (skip) { any_skip = 1; if (getenv("MCCE_DEBUG")) fprintf(stderr, "rest: region %d has no remaining material\n", r); }
            else any_region = 1;
            if (getenv("MCCE_DEBUG")) for (int k = 0; k < nz; k++) { double A = 0; int s = s2of[k]; for (int i = 0; i < s2n[s]; i++) { CPath c = { s2l[s][i].xy, s2l[s][i].n }; A += cl_area(&c); } fprintf(stderr, "rest level %d z %.3f: %d loops area %.3f per_level %d\n", k, zl[k], s2n[s], A, per_level); }
        } else any_region = 1;
        if (skip) {
            for (int k = 0; k < nz; k++) if (s2l[k]) hst_rest_free(s2l[k], s2n[k]);
            free(s2l); free(s2n); free(s2of);
            for (int k = 0; k < np; k++) { free(pc[k].xy); mcce_bnd_free_ents(&pc[k]); }
            free(pc);
            continue;
        }
        McceRec *all = NULL; int nall = 0;
        for (int lv = 0; lv < (per_level ? nz : 1) && okall; lv++) {
            int li = rest.on ? s2of[per_level ? lv : nz - 1] : 0;
            if (per_level) { nall = 0; P.depth = zl[lv]; P.top = lv ? zl[lv - 1] : pr.top_z; }   /* the level loop of PackageSomething: the top of stock is the previous level (l.5611, l.6073) */
        for (int k = 0; k < np && okall; k++) {
            int ns = 0;
            for (int i = 0; i < nisl; i++) sub[ns++] = isl[i];   /* S1 = the islands, then the region (FUN_18029cb90 dyn l.80-99) */
            sub[ns++] = pc[k];
            McceRec *rc = NULL; int nrec = 0; char err[160] = "";
            if (mcce_abort_get()) { snprintf(op->error, sizeof op->error, "cancelled"); okall = 0; break; }   /* ours: Cancel, between regions */
            const McceBnd *S2 = rest.on && s2n[li] ? s2l[li] : NULL; int n2 = rest.on ? s2n[li] : 0;
            if (!mcce_calc_dyn_pock(&P, sub, ns, S2, n2, NULL, 0, &rc, &nrec, err, sizeof err)) { snprintf(op->error, sizeof op->error, "%s", err); okall = 0; free(rc); break; }
            all = realloc(all, sizeof(McceRec) * (nall + nrec + 1)); memcpy(all + nall, rc, sizeof(McceRec) * nrec); nall += nrec; free(rc);
        }
            if (per_level && okall) {                                                            /* the engine at every level: FUN_1802ca5f0 with +0x10 set, no copies */
                int s0 = have_prev && nall && !down ? hst_group_link(b, &prevp, prevd, &pr, zl[lv], p->top_z, &lk, mrec_sx(&all[0]), mrec_sy(&all[0]), all[0].z0) : -1;
                mcce_moves(b, &pr, all, nall, fp, &down);
                if (s0 >= 0 && s0 < b->n) b->m[s0].sof = 1;   /* the region's section-start block (Move.sof) */
                if (nall) { prevp = pr; prevd = zl[lv]; have_prev = 1; }
                if (down) { V3 l = lastp(b); mv(b, 1, l.x, l.y, fp, 0); down = 0; }
            }
        }
        if (rest.on) { for (int k = 0; k < nz; k++) if (s2l[k]) hst_rest_free(s2l[k], s2n[k]); free(s2l); free(s2n); free(s2of); }
        if (per_level) { nall = 0; nz = 0; }                                                     /* (the copies below are skipped) */
        for (int k = 0; k < np; k++) { free(pc[k].xy); mcce_bnd_free_ents(&pc[k]); }
        free(pc);
        /* depth cuts: the engine runs once per region at the first level (PackageSomething's level loop runs *(short*)(dc+2) = 1 time when
           process +0x56 / +0x98 are clear), then the CHST2DWorkItemDCS work item (FUN_1802b11f0 l.15759, run by FUN_1802bb260
           decomp/mcmill_all.c l.476064) copies the region's records total - 1 times, each copy Z - d with d += rough step for the first
           nr - 1 copies and += the finish step (op +0x818 = record 0x880) after them. Island depths and taper are refused. */
        McceRec *cp = nz > 1 && nall ? malloc(sizeof(McceRec) * nall) : NULL; double d = 0;
        for (int k = 0; k < nz && okall; k++) {
            if (k == 0) {
                int s0 = have_prev && nall && !down ? hst_group_link(b, &prevp, prevd, &pr, zl[0], p->top_z, &lk, mrec_sx(&all[0]), mrec_sy(&all[0]), all[0].z0) : -1;
                mcce_moves(b, &pr, all, nall, fp, &down);
                if (s0 >= 0 && s0 < b->n) b->m[s0].sof = 1;   /* the region's section-start block (Move.sof) */
            } else {
                d += k - 1 < nr_dc - 1 ? st_dc : p->mc_depth_fs;
                for (int i = 0; i < nall; i++) { cp[i] = all[i]; cp[i].z0 -= d; cp[i].z1 -= d; }
                mcce_moves(b, &pr, cp, nall, fp, &down);
            }
            if (nall) { prevp = pr; prevd = zl[k]; have_prev = 1; }
            if (down) { V3 l = lastp(b); mv(b, 1, l.x, l.y, fp, 0); down = 0; }
        }
        free(cp); free(all);
    }
    free(sub); for (int i = 0; i < nisl; i++) { free(isl[i].xy); mcce_bnd_free_ents(&isl[i]); } free(isl);
    cl_free(&outs); free(Zr); free(Otop); dyn_ents_clear();
    if (!okall) return 0;
    if (any_skip) {                                                                             /* PackageChains l.5388-5394: LogError 10 (mcmillres 0x187) / LogWarning 0xb (0x17f) */
        if (!any_region) { snprintf(op->error, sizeof op->error, "All machining regions have no remaining material."); return 0; }
        snprintf(op->warning, sizeof op->warning, "At least one machining region has no remaining material.");
    }
    if (!b->n) { snprintf(op->error, sizeof op->error, "nothing to cut"); return 0; }
    /* the end: one rapid from the last cut straight up to the clearance - measured: 40 / 40 stored 2D High Speed NCI sections end with the
       last feed record then a single G0, no two upward rapids at one XY anywhere in them (formats/nci_save.md 7.3); the per-level retract to the
       feed plane just written (the SAFE rule, mcce_moves) is lifted to the clearance instead of a second stop */
    { Move *l = &b->m[b->n - 1];
      if (b->n >= 2 && l->rapid && fabs(l->p.x - b->m[b->n - 2].p.x) < 1e-9 && fabs(l->p.y - b->m[b->n - 2].p.y) < 1e-9 && l->p.z > b->m[b->n - 2].p.z && end_clear >= l->p.z && !b->m[b->n - 2].rapid) l->p.z = end_clear;
      else { V3 e = lastp(b); mv(b, 1, e.x, e.y, end_clear, 0); } }
    /* the NCI gate (user rule NS-1 / NS-4: lifted where every corpus section that regenerates move for move places its blocks as
       Mastercam's): the Dynamic region path, blocks at the region links (hst_group_link; FUN_180045770 l.6732) - 6 / 6 such 2D HS
       sections (formats/nci_save.md 7.3). Dynamic Contour (gen_dyncontour_mcce) stays gated. */
    op->nci_blocks = 1;
    return 1;
}
