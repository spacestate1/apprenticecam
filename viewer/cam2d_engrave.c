/* cam2d_engrave.c - Engrave (opcode 416, CAM_ENGRAVE) in the app: the op's record -> engrave.c (engrave.dll + the EStamp7 engine
 * port) -> moves. Included by cam2d.c (needs MB). Spec: ../formats/engrave.md (record, pages, lifecycle) and
 * ../formats/engrave_engine.md; oracle tools/tests/engrave_test.sh (results formats/oracle_results.md).
 *
 * The record: the stored op's own, or for an op made here the defaults library's Engrave op (cam_default_op_record, as every
 * other type); then the op dialog's record patches (pages 1802 / 1803 write the Engrave block S, the filter fields 0x320..0x33b).
 * Chains: engrave.dll FUN_180019800 / FUN_18001e400 build contours with chain_to_ctour_array at the tolerance S+0x28 / 10;
 * here cam_chain_polyline at that chord tolerance: lines, arcs sampled (ours: chain_to_ctour_array keeps them as 'A' records), spline
 * curves broken into lines as chain_to_ctour_array does (mccore FUN_180282bd0 / brk_spline_const_err, formats/spline_chain.md; Engrave
 * is the one op type here that chains splines). Open chains are ignored (engraveres 4); chains on other heights are forced to the
 * first chain's height (engraveres 10). */
#include "engrave.h"

/* the record S lives in: the 2017 record +0x12f0, the 2018 one +0x12f8 (formats/engrave.md 2.2: the 'ENGR' magic at S+0x128) */
static int eg_s_off(const uint8_t *r, uint32_t n) {
    for (int o = 0x12f0; o <= 0x12f8; o += 8) if ((uint32_t)o + 0x1d6 <= n && !memcmp(r + o + 0x128, "ENGR", 4)) return o;
    return -1;
}

static int gen_engrave(const McamFile *f, CamOp *op, MB *b) {
    const CamParams *p = &op->p;
    const Op *fo = NULL;
    if (p->file_op_id > 0) for (int i = 0; i < f->nops; i++) if (f->ops[i].id == p->file_op_id && f->ops[i].raw) { fo = &f->ops[i]; break; }
    uint32_t rn = 0; const uint8_t *src = fo ? fo->raw : cam_default_op_record(CAM_ENGRAVE, mcam_is_metric(f), &rn);
    if (fo) rn = fo->raw_n;
    if (!src || !rn) { snprintf(op->error, sizeof op->error, "Engrave: no Engrave operation in the defaults library to start from"); return 0; }
    uint8_t *rec = malloc(rn); memcpy(rec, src, rn);
    for (int k = 0; k < p->nrpatch; k++) {                    /* the op dialog's edits (pf_apply) */
        int o = p->rpatch[k].off, m = p->rpatch[k].n;
        if (!p->rpatch[k].height && (uint32_t)(o + m) <= rn) memcpy(rec + o, p->rpatch[k].v, m);
    }
    int so = eg_s_off(rec, rn);
    if (so < 0) { free(rec); snprintf(op->error, sizeof op->error, "Engrave: the op record has no Engrave block (ENGR magic)"); return 0; }
    double tol = 0; memcpy(&tol, rec + so + 0x28, 8); tol /= 10;   /* FUN_18001e400: S+0x28 / 10 */
    if (!(tol > 0)) tol = mcam_is_metric(f) ? 0.0025 : 0.0001;

    EngraveIn in; memset(&in, 0, sizeof in);
    in.S = rec + so; in.metric = mcam_is_metric(f);
    in.tool_type = p->tool_type; in.tool_dia = p->tool_dia; in.tool_angle = p->tip_angle;   /* TL tool +0x20: chamfer mill angle per side */
    in.tool_tip_dia = p->tip_dia;
    if (!(in.tool_tip_dia > 0)) for (int i = 0; i < f->ntools; i++) if (f->tools[i].num == p->tool_num) { in.tool_tip_dia = f->tools[i].tl_tip_dia; break; }
    in.feed = p->feed; in.plunge = p->plunge_feed; in.rpm = p->rpm;
    memcpy(&in.retract, rec + 0x388, 8);                      /* op +0x320 retract rate (regen_disasm.txt 0x1800188f3) */
    in.op141 = rec[0x1a9];                                    /* op +0x141 */
    in.plane_z = 0;                                           /* op coordinates: the Tplane origin (cam_generate maps the chains) */

    EgChain *ch = calloc(op->nchains ? op->nchains : 1, sizeof *ch); double **pts = calloc(op->nchains ? op->nchains : 1, sizeof *pts);
    int nch = 0, open_n = 0, have_z = 0;
    for (int i = 0; i < op->nchains; i++) {
        if (!op->chains[i].closed) { open_n++; continue; }    /* engraveres 4 "Open chains are ignored" */
        double *xy, z; int n = cam_chain_polyline(f, &op->chains[i], tol, &xy, &z);
        if (n < 3) { free(xy); continue; }
        if (!have_z) { in.chain_z = z; have_z = 1; }          /* FUN_180019800: the first chain's z */
        xy = realloc(xy, (n + 1) * 2 * sizeof *xy); xy[2 * n] = xy[0]; xy[2 * n + 1] = xy[1]; n++;   /* closed: back to the start */
        pts[nch] = xy; ch[nch].xy = xy; ch[nch].n = n; nch++;
    }
    int ok = 0;
    if (!nch) snprintf(op->error, sizeof op->error, "Engrave: no closed chains%s", open_n ? " (open chains are ignored)" : "");
    else {
        Move *m = NULL; int nm = 0; char err[200] = "";
        ok = engrave_generate(&in, ch, nch, &m, &nm, err, sizeof err);
        if (!ok) snprintf(op->error, sizeof op->error, "%s", err[0] ? err : "Engrave: the engine failed");
        for (int k = 0; ok && k < nm; k++) {
            mv(b, m[k].rapid, m[k].p.x, m[k].p.y, m[k].p.z, m[k].feed);
        }
        free(m);
        if (ok && !b->n) { snprintf(op->error, sizeof op->error, "Engrave: the engine made no toolpath"); ok = 0; }
        if (ok && open_n) snprintf(op->warning, sizeof op->warning, "Open chains are ignored.");   /* engraveres 4 */
    }
    for (int i = 0; i < nch; i++) free(pts[i]);
    free(pts); free(ch); free(rec);
    return ok;
}
