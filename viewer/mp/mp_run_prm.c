/* mp_run_prm.c - CPostRun::OverrideInitializeFromMDandCD @1801111a0: post variables re-initialised from the machine /
 * control definition (values + order from mp_prm.c mp_cd_overrides, applied with
 * CPostRunNciUtil::AssignOutputVariableReInit @180125cb0 = skip when +0x68 bit 0x1000, else AssignOutputVariable(2, v)
 * @180125b70 = current value, read-only allowed, no rounding). Log text as in Mastercam's .err file. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "mp_int.h"
#include "mp_prm.h"

int mp_run_override_md_cd(MpRun *r)
{
    const McamFile *f = (const McamFile *)r->env.prm_ctx;
    if (!f) return 0;
    MpOverride ov[1024];
    int n = mp_cd_overrides_ac(f, r->axcombo, ov, 1024);   /* InitialAssignFromMD (rotary part for the axis combination) + CD */
    if (n < 0) return 0;
    if (n > 1024) n = 1024;
    if (getenv("MP_DEBUG")) fprintf(stderr, "mp: %d MD/CD overrides\n", n);
    int quiet = mp_cd_quiet(f), prev = 0;
    int adv = mp_var_find(r, "x_mp_adv_func$");
    for (int i = 0; i < n; i++) {
        const MpOverride *o = &ov[i];
        int assigned = 0;
        if ((o->flags & MPO_ADV) && (adv < 0 || r->vars[adv].v[0] == 0)) { prev = 0; continue; }
        if ((o->flags & MPO_IFPREV) && !prev) { prev = 0; continue; }
        if ((o->flags & MPO_INTERNAL) && !strcmp(o->var, "@+0x13e2")) {   /* InitialAssignFromCD: +0x13e2 = file_ctrl +4 */
            r->rc13e2 = (unsigned short)(int)o->value; prev = 0; continue;
        }
        if (o->flags & (MPO_INTERNAL | MPO_COMMFILTER)) { prev = 0; continue; }   /* NOT DECODED here: component fields, comm_filter$ packing */
        char nm[128]; int elem = 0;
        snprintf(nm, sizeof nm, "%s", o->var);
        char *br = strchr(nm, '['); if (br) { elem = atoi(br + 1); *br = 0; }
        int vi = mp_var_find(r, nm);
        if (vi < 0) { prev = 0; continue; }
        MpVar *v = &r->vars[vi];
        double old = v->v[elem];
        if (o->flags & MPO_DIRECT) { v->v[elem] = o->value; assigned = 1; }
        else if (!(v->f68 & 0x1000)) { v->v[elem] = o->value; assigned = 1; }
        if (assigned) {
            if ((o->flags & MPO_MAX749) && v->v[elem] >= 750) v->v[elem] = 749;
            if ((o->flags & MPO_MIN1) && v->v[elem] < 1e-12) v->v[elem] = 1.0;
            if (o->msg && !quiet && fabs(old - v->v[elem]) > 1e-12)
                fprintf(stderr, "CONTROL DEFINITION - - Post variable '%s' was re-initialized from %g to %g\n", o->var, old, v->v[elem]);
        }
        prev = assigned;
    }
    return 1;
}

/* ---- rd_md$ / rd_cd$ / rd_tlpathgrp$ command variables (CPostOutput::CommandVariableOutput @1800d9960 ->
   CPostRunMCamInfo::ReadMDInfo(rd_mch_ent_no$, 1, 0) @180123460 / ReadCDInfo(1, 0) @180122ac0 / ReadGroupInfo(1, 0)
   @180122ea0, each inside SetStandardFprmMethod(1) .. (0)). The parameter walk is mp_prm's; per delivered parameter
   ReadCDInfoLoop @180122b90 assigns sparameter$ / prmcode$ and calls pmachineinfo$ (pdump$ for a dump read: not used by
   these commands), stops when the call fails and skips the rest when rd_exit$ != 0. rd_params$ = ReadOpInfo(pos,
   rd_prm_op_no$, 2) inside SetStandardFprmMethod(1) .. (0) (CommandVariableOutput, decomp/mcpost_format.c). */
typedef struct { MpRun *r; int blk, exitv, prmv, sp; } PrmCall;
static int prm_cmd_cb(void *ctx, int prmcode, const char *sparameter)
{
    PrmCall *c = ctx;
    if (c->sp >= 0) mp_str_set(c->r, c->sp, sparameter);
    if (c->prmv >= 0) c->r->vars[c->prmv].v[0] = (double)prmcode;
    if (c->blk >= 0 && !mp_block_call(c->r, c->blk)) return -1;
    if (c->exitv >= 0 && c->r->vars[c->exitv].v[0] != 0.0) return 1;
    return 0;
}
int mp_run_prm_cmd(MpRun *r, int var)
{
    const McamFile *f = (const McamFile *)r->env.prm_ctx;
    int md = mp_var_find(r, "rd_md$"), cd = mp_var_find(r, "rd_cd$"), gr = mp_var_find(r, "rd_tlpathgrp$");
    int rp = mp_var_find(r, "rd_params$");
    if (var >= 0 && var == rp) {
        int no = mp_var_find(r, "rd_prm_op_no$");
        mp_run_set_std_fprm(r, 1);
        mp_run_read_op_info(r, no >= 0 ? (int)r->vars[no].v[0] : 0, 2);
        mp_run_set_std_fprm(r, 0);
        return 1;
    }
    if (var < 0 || (var != md && var != cd && var != gr)) return 0;
    if (!f) return 1;                                      /* no part: Mastercam's message 0x2331, nothing read */
    PrmCall c = { r, mp_block_find(r, "pmachineinfo$"), mp_var_find(r, "rd_exit$"), mp_var_find(r, "prmcode$"),
                  mp_str_find(r, "sparameter$") };
    if (c.exitv >= 0) r->vars[c.exitv].v[0] = 0.0;
    mp_run_set_std_fprm(r, 1);
    if (var == md) {
        int ent = mp_var_find(r, "rd_mch_ent_no$");
        mp_prm_md(f, ent >= 0 ? (int)r->vars[ent].v[0] : 0, prm_cmd_cb, &c);
    } else if (var == cd) mp_prm_cd(f, prm_cmd_cb, &c);
    else mp_prm_group(f, prm_cmd_cb, &c);
    mp_run_set_std_fprm(r, 0);
    return 1;
}

/* ---- operation parameters ----
   CPostRunMCamInfo::ReadOpInfoLoop @180124640: rd_exit$ = 0; per delivered parameter (header, numeric "%.15lg", string,
   footer 19999) sparameter$ / prmcode$ are assigned and, inside SetStandardFprmMethod(1) .. (0), pparameter$ is called
   (pwrttparam$ while CPostRunComponents +0x386 is set = the wrtt pre-read; pdump$ for a dump read: no caller here
   passes one). A failed call ends the read (the standard-fprm method stays set, as in the decomp); rd_exit$ != 0 after
   a parameter skips the rest (the footer is still delivered: mp_prm deliver). */
typedef struct { MpRun *r; int blk, exitv, prmv, sp; } OpCall;
static int op_cb(void *ctx, int prmcode, const char *sparameter)
{
    OpCall *c = ctx;
    if (c->sp >= 0) mp_str_set(c->r, c->sp, sparameter);
    if (c->prmv >= 0) c->r->vars[c->prmv].v[0] = (double)prmcode;
    mp_run_set_std_fprm(c->r, 1);
    if (c->blk >= 0 && !mp_block_call(c->r, c->blk)) return -1;
    mp_run_set_std_fprm(c->r, 0);
    if (c->exitv >= 0 && c->r->vars[c->exitv].v[0] != 0.0) return 1;
    return 0;
}
/* CPostRunMCamInfo::ReadOpInfo @1801243b0. mode 2 (rd_params$) reads only when pparameter$ has a body. The op is
   TpOpList::OpByID(op_id) of the main op list = the part's operation with that id (GetPostParam / AddPostParam only
   cache the PrmAppMPParameter). Not found: run message 0x2331 (AddRunPostTableMsg, not fatal) and 0. No part loaded:
   nothing is read (as rd_cd$ / rd_md$ here). */
int mp_run_read_op_info(MpRun *r, int op_id, int mode)
{
    if (mode == 2) {
        int pb = mp_block_find(r, "pparameter$");
        if (pb < 0 || !mp_block_defined(r, pb)) return 0;
    }
    const McamFile *f = (const McamFile *)r->env.prm_ctx;
    if (!f) return 0;
    int k = -1;
    for (int i = 0; i < f->nops; i++) if (f->ops[i].id == op_id) { k = i; break; }
    if (k < 0 || !f->ops[k].raw) {
        if (getenv("MP_DEBUG")) fprintf(stderr, "mp: ReadOpInfo: operation %d not found (CPostMessages 0x2331)\n", op_id);
        return 0;
    }
    OpCall c = { r, mp_block_find(r, r->rc386 ? "pwrttparam$" : "pparameter$"), mp_var_find(r, "rd_exit$"),
                 mp_var_find(r, "prmcode$"), mp_str_find(r, "sparameter$") };
    if (c.exitv >= 0) r->vars[c.exitv].v[0] = 0.0;
    return mp_prm_op(f, k, op_cb, &c) == 1;
}

/* CPostReadNci::CallReadOpInfo @1800fca30 (after rd1020 in the read loop with param2 0, in wrtt with param2 = its
   command-variable argument). param2 == 0 needs a pparameter$ body. param_read$ == 3 reads nothing. op_id$ changed
   since the last call (+0x13e8) -> its parameters; when xform_op_id$ / nesting_op_id$ differ from op_id$ and +0x13e2
   & 0xfffd == 0, those ops (each only when it changed: +0x13f0 / +0x13f8) are read first and op_id$'s op is then
   read even when unchanged; param_read$ == 4 also forces it. */
void mp_run_call_read_op_info(MpRun *r, int param2)
{
    if (param2 == 0) {
        int pb = mp_block_find(r, "pparameter$");
        if (pb < 0 || !mp_block_defined(r, pb)) return;
    }
    int pr = mp_var_find(r, "param_read$");
    double prv = pr >= 0 ? r->vars[pr].v[0] : 0.0;
    if (prv == 3.0) return;
    int a = mp_var_find(r, "op_id$"), b = mp_var_find(r, "xform_op_id$"), c = mp_var_find(r, "nesting_op_id$");
    double d5 = a >= 0 ? mp_get_var(r, a, 0) : 0.0, d6 = b >= 0 ? mp_get_var(r, b, 0) : 0.0,
           d7 = c >= 0 ? mp_get_var(r, c, 0) : 0.0;
    int mode = param2;
    int rd = d5 != r->opinfo_last[0];
    if (d6 != d5 || d7 != d5) {
        if ((r->rc13e2 & 0xfffd) == 0) {
            if (d6 != d5 && d6 != r->opinfo_last[1]) { mp_run_read_op_info(r, (int)d6, mode); rd = 1; }
            if (d7 != d5 && d7 != r->opinfo_last[2]) { mp_run_read_op_info(r, (int)d7, mode); rd = 1; }
            if (prv == 4.0) rd = 1;
        }
        if (d6 != d5) r->opinfo_last[1] = d6;
        if (d7 != d5) r->opinfo_last[2] = d7;
    }
    if (rd) mp_run_read_op_info(r, (int)d5, mode);
    r->opinfo_last[0] = d5;
}
