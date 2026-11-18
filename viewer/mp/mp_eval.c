/* mp_eval.c - formula evaluation (CPostFormulaSolver::SolveFormula @1800893f0, SolveFormulaMathOperator @18008e100,
 * GetValue @1800858e0 / SetValue @180087320 / GetString @1800855b0 / SetString @1800870c0 / IsTargetReal @180086300,
 * CPostMath::fcndiv @1800c3760 / pwr @1800c7e50). mcpost compiles a formula into 3-address instructions over formula
 * work variables (CPostFormula::WriteFormula); we evaluate the expression tree (ours) in the same precedence order
 * (CPostFormula::SetPrecedenceLevels @180080c90, see mp_parse.c). Functions: mp_eval_fn.c. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mp_int.h"

#define TOL 5e-08   /* comparison tolerance used throughout SolveFormulaMathOperator */

static char *sdup(const char *s) { size_t n = strlen(s ? s : ""); char *d = malloc(n + 1); memcpy(d, s ? s : "", n + 1); return d; }
void mp_val_free(MpVal *v) { if (v && v->t == MPV_STR) { free(v->s); v->s = NULL; } }
static void val_num(MpVal *v, double d) { memset(v, 0, sizeof *v); v->t = MPV_NUM; v->v[0] = d; }
static void val_str(MpVal *v, char *s) { memset(v, 0, sizeof *v); v->t = MPV_STR; v->s = s; }

const MpFs *mp_fs_get(MpRun *r, int num)
{
    if (num < MP_FSMIN || num >= MP_FSMAX || !r->fs[num - MP_FSMIN].defined) return &r->fs[0 - MP_FSMIN];
    return &r->fs[num - MP_FSMIN];
}
/* CPostFormulaSolver::GetFs @180085100 (decompiled at mcpost_all.c ~71815): the variable's format statement, else the
   component's fs 1 (+0xf8) */
const MpFs *mp_var_fs(MpRun *r, int idx)
{
    if (idx >= 0 && idx < r->nvars) {
        int n = r->vars[idx].fs;
        if (n != MP_NOFS && n >= MP_FSMIN && n < MP_FSMAX && r->fs[n - MP_FSMIN].defined) return &r->fs[n - MP_FSMIN];
    }
    return mp_fs_get(r, 1);
}

/* ---- global formulas: a variable declared 'name = expr' at column 0 is re-solved every time it is read
   (GetValue / GetString -> IsGlobalFormula -> SolveFormula). Re-entrancy guard is ours. ---- */
static int solving_global(MpRun *r, MpExpr *f, int set)
{
    static MpExpr *stack[64]; static int n;
    if (set > 0) { if (n < 64) stack[n++] = f; return 0; }
    if (set < 0) { if (n) n--; return 0; }
    for (int i = 0; i < n; i++) if (stack[i] == f) return 1;
    (void)r; return 0;
}
static void solve_global(MpRun *r, MpExpr *f)
{
    if (!f || solving_global(r, f, 0)) return;
    solving_global(r, f, 1);
    MpVal v; if (mp_eval(r, f, &v)) mp_val_free(&v);
    solving_global(r, f, -1);
}

double mp_get_var(MpRun *r, int idx, int prv)
{
    if (idx < 0 || idx >= r->nvars) return 0;
    MpVar *v = &r->vars[idx];
    if (v->gformula && ((!prv && (v->f78 & 0x40)) || (prv && (v->f78 & 0x80)))) solve_global(r, v->gformula);
    return v->v[prv ? 1 : 0];
}
/* SetValue @180087320: +0x68 bit 4 (current) / bit 8 (previous) = read only (':=' declarations, constants like pi$) */
void mp_set_var(MpRun *r, int idx, int prv, double d)
{
    if (idx < 0 || idx >= r->nvars) return;
    MpVar *v = &r->vars[idx];
    if (v->f68 & (prv ? 0x8 : 0x4)) return;
    v->v[prv ? 1 : 0] = d;
    /* NOT DECODED: linked variables (flag 0x20, linklvar$ / linktolvar$ / linkplnvar$ -> CPostMath::SetLinkedVarTolerance /
       SetLinkedVarPlanes / SetLinkedVarLathe) are not propagated */
}
const char *mp_get_str(MpRun *r, int idx)
{
    if (idx < 0 || idx >= r->nstrs) return "";
    MpStrVar *s = &r->strs[idx];
    if (s->gformula) solve_global(r, s->gformula);
    return s->val ? s->val : "";
}
void mp_set_strv(MpRun *r, int idx, const char *s)
{
    if (idx < 0 || idx >= r->nstrs) return;
    char *d = sdup(s); free(r->strs[idx].val); r->strs[idx].val = d;
}

static double val_d(const MpVal *v)
{
    if (v->t == MPV_STR) return v->s ? atof(v->s) : 0;   /* NOT DECODED: GetValue on a string operand */
    return v->v[0];
}
static char *val_s(const MpVal *v)
{
    if (v->t == MPV_STR) return sdup(v->s);
    char b[64]; snprintf(b, sizeof b, "%.15g", v->v[0]);   /* NOT DECODED: GetString on a numeric operand */
    return sdup(b);
}

/* argument value types from the operator table word 1 (formats/mp.md): nibble 2+2k = type of argument k
   (1 real, 2 vector, 4 string, 8 matrix) - ours: derived from the table rows (vequ 0x210, dot 0x21210, vmap 0x81210) */
static int arg_type(int op, int k)
{
    int n; const MpOpDef *t = mp_op_table(&n);
    if (op < 0 || op >= n) return 1;
    unsigned w = t[op].w1 >> (8 + 8 * k);
    int ty = w & 0xf;
    return ty ? ty : 1;
}

static int eval_arg(MpRun *r, MpExpr *e, int want, MpArg *a)
{
    memset(a, 0, sizeof *a); a->var = a->str = -1;
    if (e->kind == EX_VAR) {
        a->var = e->idx; a->prv = e->prv;
        a->val.t = want == 2 ? MPV_VEC : want == 8 ? MPV_MAT : MPV_NUM;
        int m = want == 2 ? 3 : want == 8 ? 9 : 1;
        for (int i = 0; i < m; i++) a->val.v[i] = e->idx + i < r->nvars ? mp_get_var(r, e->idx + i, e->prv) : 0;
        return 1;
    }
    if (e->kind == EX_STRVAR) { a->str = e->idx; val_str(&a->val, sdup(mp_get_str(r, e->idx))); return 1; }
    return mp_eval(r, e, &a->val);
}

/* assignment (SolveFormulaMathOperator op_space / op_colon / op_assign / op_eq branch): string target -> SetString,
   else SetValue; vector / matrix results fill consecutive variables (SolveFormula3dVectorMath SetValue loop) */
static void assign(MpRun *r, MpExpr *t, MpVal *v)
{
    if (t->kind == EX_STRVAR) { char *s = val_s(v); mp_set_strv(r, t->idx, s); free(s); return; }
    if (t->kind != EX_VAR) return;
    int m = v->t == MPV_VEC ? 3 : v->t == MPV_MAT ? 9 : 1;
    if (m == 1) { mp_set_var(r, t->idx, t->prv, val_d(v)); return; }
    for (int i = 0; i < m && t->idx + i < r->nvars; i++) mp_set_var(r, t->idx + i, t->prv, v->v[i]);
}

int mp_eval(MpRun *r, MpExpr *e, MpVal *out)
{
    memset(out, 0, sizeof *out); out->t = MPV_NUM;
    if (!e) return 1;
    switch (e->kind) {
    case EX_NUM: out->v[0] = e->num; return 1;
    case EX_STRLIT: val_str(out, sdup(e->str)); return 1;
    case EX_VAR: out->v[0] = mp_get_var(r, e->idx, e->prv); return 1;
    case EX_STRVAR: val_str(out, sdup(mp_get_str(r, e->idx))); return 1;
    case EX_ASSIGN: {
        MpVal v;
        /* a function result is solved with the formula target known (some functions use it) */
        if (e->b && e->b->kind == EX_FUNC) {
            MpArg tg; memset(&tg, 0, sizeof tg); tg.var = tg.str = -1;
            if (e->a->kind == EX_VAR) { tg.var = e->a->idx; tg.prv = e->a->prv; } else if (e->a->kind == EX_STRVAR) tg.str = e->a->idx;
            MpExpr *f = e->b;
            MpArg a[16]; int n = f->nargs > 16 ? 16 : f->nargs, ok = 1;
            for (int i = 0; i < n; i++) ok &= eval_arg(r, f->args[i], arg_type(f->op, i), &a[i]);
            if (ok) ok = mp_fn_call(r, f->op, a, n, &tg, &v);
            for (int i = 0; i < n; i++) mp_val_free(&a[i].val);
            /* mp_eval_fn returns the target's current value when the call fails or does not write the target
               (mcpost does not assign on a hard error) - assigning it back is a no-op */
            if (n < 0) ok = 0;
            assign(r, e->a, &v); *out = v; return 1;
        }
        if (!mp_eval(r, e->b, &v)) return 0;
        assign(r, e->a, &v);
        *out = v;
        return 1;
    }
    case EX_FUNC: {
        MpArg a[16]; int n = e->nargs > 16 ? 16 : e->nargs, ok = 1;
        for (int i = 0; i < n; i++) ok &= eval_arg(r, e->args[i], arg_type(e->op, i), &a[i]);
        if (ok) ok = mp_fn_call(r, e->op, a, n, NULL, out);
        for (int i = 0; i < n; i++) mp_val_free(&a[i].val);
        return ok;
    }
    case EX_UNOP: {   /* op_minus: GetValue * -1 */
        MpVal a; if (!mp_eval(r, e->a, &a)) return 0;
        if (a.t == MPV_VEC || a.t == MPV_MAT) { *out = a; for (int i = 0; i < 9; i++) out->v[i] = -out->v[i]; return 1; }
        double d = val_d(&a); mp_val_free(&a);
        out->v[0] = -d; return 1;
    }
    case EX_BINOP: {
        MpVal a, b;
        if (!mp_eval(r, e->a, &a)) return 0;
        if (!mp_eval(r, e->b, &b)) { mp_val_free(&a); return 0; }
        int str = a.t == MPV_STR || b.t == MPV_STR;
        double x = val_d(&a), y = val_d(&b), d = 0;
        switch (e->op) {
        case 11:                                                   /* CPostMath::pwr @1800c7e50 */
            if (fabs(x) < 1e-05) d = 0;
            else if (y == 0) d = 1;
            else { double bx = x; if (bx <= 0 && (bx >= 0 || floor(y) != y)) bx = fabs(bx); d = pow(bx, y); }
            break;
        case 12: d = x * y; break;
        case 13: d = fabs(y) < TOL ? 0 : x / y; break;             /* CPostMath::fcndiv @1800c3760 (error 0x20) */
        case 14:
            if (str) {                                             /* string target: concatenation */
                char *s1 = val_s(&a), *s2 = val_s(&b);
                char *c = malloc(strlen(s1) + strlen(s2) + 1); strcpy(c, s1); strcat(c, s2); free(s1); free(s2);
                mp_val_free(&a); mp_val_free(&b); val_str(out, c); return 1;
            }
            d = x + y; break;
        case 15: d = x - y; break;
        case 18: d = (fabs(x - y) <= TOL || x <= y) ? 0 : 1; break;            /* > */
        case 19: d = (fabs(x - y) > TOL && x < y) ? 1 : 0; break;              /* < */
        case 20: d = (y - TOL <= x) ? 1 : 0; break;                            /* >= */
        case 21: d = (x <= y + TOL) ? 1 : 0; break;                            /* <= */
        case 17: case 16:                                                      /* = (op_eq2 / op_space2) */
            if (a.t == MPV_STR) d = strcmp(a.s ? a.s : "", b.t == MPV_STR && b.s ? b.s : "") == 0;   /* Ordinal_2857 (Compare) */
            else d = fabs(x - y) < TOL ? 1 : 0;
            break;
        case 22:                                                               /* <> */
            if (a.t == MPV_STR) d = strcmp(a.s ? a.s : "", b.t == MPV_STR && b.s ? b.s : "") != 0;
            else d = fabs(x - y) >= TOL ? 1 : 0;
            break;
        case 23: d = (fabs(x) > TOL && fabs(y) > TOL) ? 1 : 0; break;          /* & */
        case 24: d = (fabs(x) > TOL || fabs(y) > TOL) ? 1 : 0; break;          /* | */
        default: mp_rt_error(r, 0, "operator %d not handled", e->op); d = 0; break;
        }
        mp_val_free(&a); mp_val_free(&b);
        out->v[0] = d; return 1;
    }
    }
    return 1;
}

double mp_eval_num(MpRun *r, MpExpr *e)
{
    MpVal v; if (!mp_eval(r, e, &v)) return 0;
    double d = val_d(&v); mp_val_free(&v); return d;
}
