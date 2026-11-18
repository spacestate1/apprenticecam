/* mp_run.c - posting run state and postblock execution: CPostRunPostTable::BlockCall @180129c70 / ProcessBlockCall
 * @18012a8c0 (post table walk: 0x3eb call, 0x7d3 formula, 0x7d5 boolean, 0x2c0 else, output elements 900..0x393),
 * CPostInitPreDefs::InitializeVariablesAndStrings @1800a8fe0 / InitializePreviousVariables @1800a8c80,
 * CPostVariable::SetDefaultValues @1801b9960 (run-time parts). The NCI read loop is mp_nciloop.c. */
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include "mp_int.h"
#include "mp_parse_tables.h"

void mp_rt_error(MpRun *r, int line, const char *fmt, ...)
{
    char b[480]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    r->nerrors++;
    if (line > 0) snprintf(r->err, sizeof r->err, "line %d: %s", line, b); else snprintf(r->err, sizeof r->err, "%s", b);
    if (r->nerrors <= 50) fprintf(stderr, "mp: %s\n", r->err);
}

/* ------------------------------------------------------------------ symbols (mp.h) */
int mp_var_find(const MpRun *r, const char *name) { return mp_find_var_p(r->p, name); }
int mp_var_idx(MpRun *r, const char *name) { return mp_find_var_p(r->p, name); }
double *mp_var(MpRun *r, int idx) { return idx >= 0 && idx < r->nvars ? &r->vars[idx].v[0] : NULL; }
double *mp_var_prv(MpRun *r, int idx) { return idx >= 0 && idx < r->nvars ? &r->vars[idx].v[1] : NULL; }
int mp_str_find(const MpRun *r, const char *name) { return mp_find_str_p(r->p, name); }
const char *mp_str_get(const MpRun *r, int idx) { return idx >= 0 && idx < r->nstrs ? r->strs[idx].val : NULL; }
void mp_str_set(MpRun *r, int idx, const char *s) { mp_set_strv(r, idx, s); }
int mp_block_find(const MpRun *r, const char *name) { return mp_find_block_p(r->p, name); }
int mp_block_defined(const MpRun *r, int idx) { return idx >= 0 && idx < r->p->nblocks && r->p->blocks[idx].defined; }
int mp_run_stopped(const MpRun *r) { return r->stopped; }
const char *mp_run_error(const MpRun *r) { return r->err[0] ? r->err : NULL; }

static double getv(MpRun *r, const char *n) { int i = mp_var_find(r, n); return i >= 0 ? mp_get_var(r, i, 0) : 0; }
static void setall(MpRun *r, const char *n, double d) { int i = mp_var_find(r, n); if (i >= 0) for (int k = 0; k < 4; k++) r->vars[i].v[k] = d; }

/* ------------------------------------------------------------------ run creation + initialisation */
MpRun *mp_run_new(const MpPost *p, const MpEnv *env, FILE *nc_out, char *err, size_t errn)
{
    (void)err; (void)errn;
    MpRun *r = calloc(1, sizeof *r);
    r->p = p; r->env = *env; r->nc = r->nc_main = nc_out;
    r->nvars = p->nvars; r->vars = malloc(r->nvars * sizeof(MpVar)); memcpy(r->vars, p->vars, r->nvars * sizeof(MpVar));
    r->nstrs = p->nstrs; r->strs = malloc(r->nstrs * sizeof(MpStrVar)); memcpy(r->strs, p->strs, r->nstrs * sizeof(MpStrVar));
    for (int i = 0; i < r->nstrs; i++) r->strs[i].val = strdup(p->strs[i].val ? p->strs[i].val : "");
    memcpy(r->fs, p->fs, sizeof r->fs);
    r->nfmts = p->nfmts; r->fmts = malloc((r->nfmts + 1) * sizeof(MpFmt)); memcpy(r->fmts, p->fmts, r->nfmts * sizeof(MpFmt));
    r->nfstrsels = p->nfstrsels;
    r->fstrsels = malloc((p->nfstrsels + 1) * sizeof(MpFstrsel));
    if (p->nfstrsels) memcpy(r->fstrsels, p->fstrsels, p->nfstrsels * sizeof(MpFstrsel));
    r->stk = calloc(p->nstacks + 1, sizeof *r->stk);
    r->line = calloc(1, 256); r->linecap = 256;
    r->tlchg_kind = 1;
    /* SetDefaultValues @1801b9960, run-time parts: posting clock (time$ = hour + min*0.01 + sec*0.0001, date$ = weekday,
       day$, month$ = mon$, year$ - 2000), post-type dependent tolerances (wire: mtol$ 1e-5, met_mtol$ 0.0001) */
    {
        struct tm t; memset(&t, 0, sizeof t); t.tm_isdst = -1;
        t.tm_mday = env->day; t.tm_mon = env->mon - 1; t.tm_year = env->year - 1900; t.tm_hour = env->hour; t.tm_min = env->min; t.tm_sec = env->sec;
        mktime(&t);
        setall(r, "time$", env->min * 0.01 + env->hour + env->sec * 0.0001);
        setall(r, "date$", t.tm_wday); setall(r, "day$", env->day); setall(r, "month$", env->mon); setall(r, "mon$", env->mon);
        setall(r, "year$", env->year - 2000.0);
        if (env->product == 3) { setall(r, "mtol$", 1e-05); setall(r, "met_mtol$", 0.0001); }
        /* NOT DECODED: chord_tol$ / arc_tol$ / minarc$ / minrad$ / xtol$ run-time dependent defaults (dVar mix in
           SetDefaultValues), sim_no$ / sim_type$ */
    }
    mp_run_init_values(r);
    return r;
}

/* InitializeVariablesAndStrings @1800a8fe0: met_tool$ = units (all values, flag 0x800), then every declaration formula in
   file order is solved; previous = current unless the declaration was for prv_ (f78 0x20 / 0x80). */
void mp_run_init_values(MpRun *r)
{
    int mt = mp_var_find(r, "met_tool$");
    if (mt >= 0) { for (int k = 0; k < 4; k++) r->vars[mt].v[k] = r->env.metric ? 1 : 0; r->vars[mt].f68 |= 0x800; }
    for (int i = 0; i < r->p->ninits; i++) {
        MpExpr *x = r->p->inits[i];
        MpExpr *t = x->a;
        if (t->kind == EX_VAR) {
            MpVar *v = &r->vars[t->idx];
            if ((v->f68 & 0x1000) || (v->f68 & 0x800)) continue;
            unsigned save = v->f68; v->f68 &= ~0xcu;       /* the declaration itself may set a read-only (':=') value */
            MpVal val; if (mp_eval(r, x, &val)) mp_val_free(&val);
            v->f68 = save;
            if (!t->prv && !(v->f78 & 0x20) && !(v->f78 & 0x80)) v->v[1] = v->v[0];
        } else {
            MpVal val; if (mp_eval(r, x, &val)) mp_val_free(&val);
        }
    }
}

void mp_run_free(MpRun *r)
{
    if (!r) return;
    for (int i = 0; i < r->nstrs; i++) free(r->strs[i].val);
    for (int i = 1; i <= 4; i++) if (r->sub[i]) fclose(r->sub[i]);
    for (int i = 0; i < r->p->nstacks; i++) {
        for (int k = 0; k < r->stk[i].n; k++) free(r->stk[i].rec[k]);
        free(r->stk[i].rec);
    }
    free(r->stk); free(r->fstrsels);
    free(r->strs); free(r->vars); free(r->fmts); free(r->line); free(r->upd);
    free(r);
}

/* ------------------------------------------------------------------ execution */
/* CPostRunPostTable::ProcessBlockCall @18012a8c0: walk the post table of one postblock */
int mp_exec(MpRun *r, MpItem *it)
{
    for (; it; it = it->next) {
        if (r->stopped) return 0;
        if (++r->tick > 200000000LL) { mp_rt_error(r, it->line, "run aborted: too many steps (loop?)"); r->stopped = 1; return 0; }
        switch (it->code) {
        case EL_FORMULA: {                                       /* 0x7d3/0x7d4 -> CPostOutput::Formula */
            MpVal v; if (mp_eval(r, it->expr, &v)) mp_val_free(&v);
            else mp_rt_error(r, it->line, "formula: %s", r->err);
            break;
        }
        case EL_IF: {                                            /* 0x7d5: CPostOutput::Boolean, SetValue 700: |v| >= 5e-8 */
            double c = mp_eval_num(r, it->expr);
            if (fabs(c) >= 5e-08) { if (!mp_exec(r, it->body)) return 0; }
            else if (it->has_else) { if (!mp_exec(r, it->els)) return 0; }
            break;
        }
        case EL_WHILE: {
            long guard = 0;
            while (fabs(mp_eval_num(r, it->expr)) >= 5e-08) {
                if (!mp_exec(r, it->body)) return 0;
                if (++guard > 10000000) { mp_rt_error(r, it->line, "while loop runaway"); r->stopped = 1; return 0; }
            }
            break;
        }
        case EL_GROUP: if (!mp_exec(r, it->body)) return 0; break;
        case EL_CALL: case EL_CALLARGS:                           /* 0x3eb / 0x3ec: nested ProcessBlockCall */
            if (!mp_process_block(r, it->idx)) return 0;
            break;
        case EL_QUESTION:
            /* CPostOutput::PromptQuestion @1800ddd00: the fq prompt (CPostFq::MakeFqPromptString) is shown with the
               variable's current value as the default (WaitForValueRunDialog(prompt, +0x10) for 800 numeric,
               WaitForStringRunDialog(prompt, string) for 0x324) and the answer is stored back. No UI here: the
               default is kept, i.e. the variable is left unchanged (prompt log messages AddPromptMessage not
               reproduced) */
            break;
        default:
            mp_out_item(r, it);
            if (r->status) {                                       /* ex$ / nci_rewind$ / exitpost$ end the block chain */
                if (r->status == 6 || r->status == 4) r->stopped = 1;
                return 0;
            }
            break;
        }
    }
    return !r->stopped;
}

int mp_process_block(MpRun *r, int idx)
{
    if (idx < 0 || idx >= r->p->nblocks) return 1;
    const MpBlock *b = &r->p->blocks[idx];
    if (!b->defined) return 1;
    if (r->opt_trace) fprintf(stderr, "%*s%s\n", r->calldepth * 2, "", b->name);
    if (++r->calldepth > 500) { mp_rt_error(r, b->line, "postblock recursion too deep (%s)", b->name); r->calldepth--; r->stopped = 1; return 0; }
    int ok = mp_exec(r, b->body);
    r->calldepth--;
    return ok;
}

static int pbi(MpRun *r, const char *name) { return mp_find_block_p(r->p, name); }

/* hooks for mp_nciloop.c: CPostRunComponents +0x382 (tool change kind) and +0x384 (BlockCall ran the tool change) */
void mp_run_set_tlchg_kind(MpRun *r, int kind) { r->tlchg_kind = kind; }
int  mp_run_tlchg_done(MpRun *r) { return r->tlchg_done; }
void mp_run_set_tlchg_done(MpRun *r, int v) { r->tlchg_done = v; }
/* CPostComponents::SetStandardFprmMethod: NOT DECODED effect on fprm() (kept as a flag) */
void mp_run_set_std_fprm(MpRun *r, int on) { r->std_fprm = on; }

/* the tool-change part of BlockCall (+0x382: 1 start of file, 2 tool change, else null tool change; product +0x39e) */
static int tlchg_dispatch(MpRun *r)
{
    int prod = r->env.product, b = -1;
    if (r->tlchg_kind == 1) {
        if (getv(r, "t$") == 0 && getv(r, "tool_zero$") != 0 && getv(r, "rotaxtyp$") < 6) {
            if (prod < 2 && mp_block_defined(r, pbi(r, "msof0$"))) b = pbi(r, "msof0$");
            else if (prod == 2 && mp_block_defined(r, pbi(r, "lsof0$"))) b = pbi(r, "lsof0$");
            else b = pbi(r, "psof0$");
        } else {
            if (prod < 2 && mp_block_defined(r, pbi(r, "msof$"))) b = pbi(r, "msof$");
            else if (prod == 2 && mp_block_defined(r, pbi(r, "lsof$"))) b = pbi(r, "lsof$");
            else b = pbi(r, "psof$");
        }
    } else if (r->tlchg_kind == 2) {
        if (prod < 2 && mp_block_defined(r, pbi(r, "mtlchg$"))) b = pbi(r, "mtlchg$");
        else if (prod == 2 && mp_block_defined(r, pbi(r, "ltlchg$"))) b = pbi(r, "ltlchg$");
        else b = pbi(r, "ptlchg$");
    } else {
        /* NOT DECODED: probe tool (IsProbeType 0x97) forces tlngno$ output */
        if (prod < 2 && mp_block_defined(r, pbi(r, "mtlchg0$"))) b = pbi(r, "mtlchg0$");
        else if (prod == 2 && mp_block_defined(r, pbi(r, "ltlchg0$"))) b = pbi(r, "ltlchg0$");
        else b = pbi(r, "ptlchg0$");
    }
    int ok = mp_process_block(r, b);
    r->tlchg_done = 1;
    return ok;
}

/* CPostRunPostTable::BlockCall @180129c70 */
int mp_block_call(MpRun *r, int idx)
{
    if (r->stopped) return 0;
    r->status = 0;
    if (!r->wrote) { int i = mp_var_find(r, "nc_lout$"); if (i >= 0) r->vars[i].v[0] = -1; }
    int bp_rapid = pbi(r, "prapid$"), bp_toolend = pbi(r, "ptoolend$");
    if (getv(r, "rotaxtyp$") > 6 && idx >= bp_rapid && idx < bp_toolend)
        if (!mp_process_block(r, pbi(r, "pmx0$"))) return 0;
    if (idx != -1) {
        if (getv(r, "tlchng_aft$") == 0.0) return mp_process_block(r, idx);
        if (r->tlchg_done || r->tlchg_kind == 0 || idx < pbi(r, "pmx$") || idx > pbi(r, "pcan8$"))
            return mp_process_block(r, idx);
    }
    if (!tlchg_dispatch(r)) return 0;
    /* after the delayed tool change: postblocks pg32$..lmisc2$ are still called; for others the NCI position is
       remembered (xnci$/ynci$/znci$ previous = current) */
    if (idx < pbi(r, "pg32$") || idx > pbi(r, "lmisc2$")) {
        static const char *n[] = {"xnci$", "ynci$", "znci$"};
        for (int k = 0; k < 3; k++) { int i = mp_var_find(r, n[k]); if (i >= 0) r->vars[i].v[1] = r->vars[i].v[0]; }
        return 1;
    }
    return mp_process_block(r, idx);
}
int mp_block_call_tlchg(MpRun *r) { return mp_block_call(r, -1); }
