/* mp_eval_fn.c - MP formula function library: every operator-table row from `not` onward (formats/mp/mp_operators.tsv).
 * Port of mcpost.dll CPostFormulaSolver::SolveFormula* (dispatch: CPostFormulaSolver::SolveFormula @1800893f0):
 *   SolveFormulaMathFunction @18008de60     not abs sqrt log log10 exp round int frac fmtrnd
 *   SolveFormulaMathTrigFunction @18008e960 sin cos tan asin acos atan atan2
 *   SolveFormula2dVectorMath @180087570     dot lng axb vad vsb vsc vsa vnr vlh vrh rot veq
 *   SolveFormula3dVectorMath @180088440     dot3 lng3 vaxb vadd vsub vscl vsca vnrm rotv rotp vequ
 *   SolveFormulaMatrixMath @18008eb40       vmap matt mmul mteq
 *   SolveFormulaStringSelect @180091c80     grpix setseloffs setselund setselsiz setselrtn setselsel updstr
 *   SolveFormulaLookUp @18008d090           plcval fsg fsg1 fsg2 fsg3 flook frange finc rpar rparsngl rparsprm fprm
 *   SolveFormulaBuffer @180089d00           wbuf rbuf pop push turn
 *   SolveFormulaVariable @1800920b0         force unforce cpyforce prvneq inc_var setinc_var updgbl altgbl setstrval
 *                                           getstrval t_list comment_fx setncstr vlout vlin slout slin raw raw3 raw6 raw9
 *   SolveFormulaIntersection @18008b800     breaks xll xla xaa
 *   SolveFormulaString @180091330           ucase lcase strstr strlen brksps scan no2asc no2str drs_str
 *   SolveFormulaFormat @18008aed0           newfs copyfs modfs nwadrs nwsufx nwadrsval nwsufxval copyfmt
 *   SolveFormulaFile @18008a320             fnewsub rename remove fexist fclose directory
 *   SolveFormulaLaunch @18008cb60           launch dll runvbs
 *   SolveFormulaScreen @180090870           mprint drwlin drwarc db_view db_rot db_trn db_proc db_clr
 *   SolveFormulaQuery @18008f130            sysinfo opchng opmatrix opvector acomboinfo uidinfo opinfo groupinfo cdinfo
 *                                           mdinfo streaminfo
 *   SolveFormulaBitwiseMath @180089bb0      xor and ior
 *   SolveFormulaOther @18008f040            delay ger
 * In SolveFormula* the operands are (op, A, B, C, D): A..C the arguments, D the formula target. A missing argument
 * repeats the previous one (CPostFormulaSolver::IsTwoArgument @180086330 / IsOneArgument @1800862a0 test B == C).
 * Vector / matrix operands walk consecutive variables (GetNextPosition @1800852f0); a constant operand gives the same
 * value for every component (GetValue @1800858e0 ignores the component index for constants).
 *
 * Result conventions (see the report to the interpreter):
 *  - *out holds the value the evaluator assigns to the target (NUM / STR / VEC / MAT).
 *  - functions that do not assign their target in mcpost (they write other variables, or fail before SetValue) return
 *    the target's CURRENT value so that the evaluator's assignment is a no-op (r_keep()).
 *  - 2D vector results write only 2 components in mcpost: v[2] is the target's current 3rd component.
 *  - return 0 + r->err only where mcpost returns before SetValue (status 0x80000 type error, 1 unsupported);
 *    status bits mcpost raises after assigning (sqrt of a negative, table misses, ...) are reported with mp_rt_error
 *    and the call returns 1 (mcpost: AddRunPostTableMsg 7000 / 0x2715, the run goes on). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "mp_int.h"
#include "mp_prm.h"

#define D2R 0.01745329251994329     /* CPostMath::sinPost @1800c83b0 */
#define R2D 57.29577951308233       /* CPostMath::asinPost @1800c24e0 */
#define TOL 5e-08                   /* SolveFormulaMathOperator / prvneq / fsg */
#define NOVAL (-99999.0)            /* 0xc0f869f000000000: "no value" (setseloffs, TableParameterMD/CD) */

/* t_list: the NCI loop's tool list (mp_nciloop_tt.c); absent -> -1 */
extern int mp_nci_t_list(MpRun *r, int mode, int has_idx, int *idx, int *set_idx, double *status, double *value)
    __attribute__((weak));
/* ---- optional hooks the interpreter may provide (weak: absent -> the NOT DECODED fallback is used) ---- */
/* CPostFstrsel::SelectString @18009a2e0 for updstr(): select string `str` (an fstrsel target) by its selector.
   returns 1 when a string was selected */
extern int mp_fstrsel_update(MpRun *r, int str) __attribute__((weak));
/* CPostPostTable::ReplaceFormula (updgbl): parse `text` as a formula; NULL on a syntax error */
extern MpExpr *mp_parse_formula_text(MpRun *r, const char *text) __attribute__((weak));
/* CPostRunBuffers::ConvertTextToBinaryBuffer @1801134a0 (fbuf read flag) / ConvertBinaryBufferToText @180113020
   (fbuf type 1 / 2 at close). return 1 ok */
extern int mp_buf_text_in(MpRun *r, int b) __attribute__((weak));
extern int mp_buf_text_out(MpRun *r, int b) __attribute__((weak));
/* fclose("name.ext"): close the NC / sub / aux / ext / lcc stream whose snameXX$+sextXX$ matches (which: 0 nc, 1 sub,
   2 aux, 3 ext, 4 lcc). return 1 closed */
extern int mp_out_close_stream(MpRun *r, int which) __attribute__((weak));
/* CPostOutput +0x1c0: the end-of-line string appended by AppendEndOfBlock @1800d9660 */
extern const char *mp_out_eol(MpRun *r) __attribute__((weak));

/* ------------------------------------------------------------------ call context */
typedef struct Fn {
    MpRun *r;
    int op;
    const char *name;
    MpArg *a; int n;
    const MpArg *tg;
    MpVal *out;
    unsigned long long st;     /* CPostFormulaSolver +0x38 status bits */
    int hard;                  /* mcpost returned before SetValue */
    unsigned w2;
} Fn;
typedef void (*FnH)(Fn *f);

static char *sdup(const char *s) { size_t n = strlen(s ? s : ""); char *d = malloc(n + 1); memcpy(d, s ? s : "", n + 1); return d; }

/* MSVC (int) conversion of a double (cvttsd2si: out of range / NaN -> INT_MIN) */
static int ms_int(double d) { if (!(d > -2147483649.0 && d < 2147483648.0)) return INT_MIN; return (int)d; }
static short ms_short(double d) { return (short)ms_int(d); }

/* ---- predefined variables read through CPostMath::GetVarRef @1800bd770 (raw slot, no global formula) ---- */
static int vix(MpRun *r, const char *name)
{
    static struct { const MpPost *p; const char *name; int idx; } c[64];
    static int nc;
    for (int i = 0; i < nc; i++) if (c[i].p == r->p && c[i].name == name) return c[i].idx;
    int idx = mp_find_var_p(r->p, name);
    int k = nc < 64 ? nc++ : (int)(((size_t)name >> 3) % 64);
    c[k].p = r->p; c[k].name = name; c[k].idx = idx;
    return idx;
}
static int six(MpRun *r, const char *name)
{
    static struct { const MpPost *p; const char *name; int idx; } c[16];
    static int nc;
    for (int i = 0; i < nc; i++) if (c[i].p == r->p && c[i].name == name) return c[i].idx;
    int idx = mp_find_str_p(r->p, name);
    int k = nc < 16 ? nc++ : (int)(((size_t)name >> 3) % 16);
    c[k].p = r->p; c[k].name = name; c[k].idx = idx;
    return idx;
}
static double gref(MpRun *r, const char *name, int slot)
{
    int i = vix(r, name);
    return (i >= 0 && i < r->nvars && slot >= 0 && slot < 4) ? r->vars[i].v[slot] : 0.0;
}
static void gref_set(MpRun *r, const char *name, int slot, double v)
{
    int i = vix(r, name);
    if (i >= 0 && i < r->nvars && slot >= 0 && slot < 4) r->vars[i].v[slot] = v;
}
static int metric(MpRun *r) { return gref(r, "met_tool$", 0) != 0.0; }
static MpFmtCtx fmtctx(MpRun *r) { MpFmtCtx c; c.round_opt = gref(r, "round_opt$", 0); return c; }

/* ---- arguments ---- */
static const MpArg g_none = { { MPV_NUM, {0}, NULL }, -1, 0, -1 };
static const MpArg *A(Fn *f, int i) { if (f->n <= 0) return &g_none; return &f->a[i < f->n ? i : f->n - 1]; }
/* CPostFormulaSolver::IsArgumentString @1800860a0 */
static int a_isstr(const MpArg *x) { return x->str >= 0 || (x->var < 0 && x->val.t == MPV_STR); }
static int a_var(MpRun *r, const MpArg *x, int k) { return (x->var >= 0 && x->var + k < r->nvars) ? x->var + k : -1; }
/* GetValue @1800858e0 of component k (GetNextPosition k times). String operands read 0 (switch default) */
static double gv(Fn *f, const MpArg *x, int k)
{
    if (x->str >= 0) return 0.0;
    if (x->var >= 0) { int i = a_var(f->r, x, k); return i >= 0 ? mp_get_var(f->r, i, x->prv) : 0.0; }
    if (x->val.t == MPV_VEC || x->val.t == MPV_MAT) return (k >= 0 && k < 9) ? x->val.v[k] : 0.0;
    if (x->val.t == MPV_STR) return 0.0;
    return x->val.v[0];
}
/* GetValue with the "extend" value slot (+0x10 + slot*8 / prv +0x18 + slot*8, SetVariableExtendToFunctionOption) */
static double gv_slot(Fn *f, const MpArg *x, int k, int slot)
{
    if (slot == 0 || x->var < 0) return gv(f, x, k);
    int i = a_var(f->r, x, k); if (i < 0) return 0.0;
    mp_get_var(f->r, i, x->prv);               /* IsGlobalFormula re-solve, then the slot */
    int s = (x->prv ? 1 : 0) + slot;
    return s < 4 ? f->r->vars[i].v[s] : 0.0;
}
/* SetValue @180087320 with a value slot: read-only flags 0x4 (current) / 0x8 (previous); init mode not modelled */
static void var_put(MpRun *r, int idx, int prv, int slot, double v)
{
    if (idx < 0 || idx >= r->nvars) return;
    if (slot == 0) { mp_set_var(r, idx, prv, v); return; }
    MpVar *x = &r->vars[idx];
    if (x->f68 & (prv ? 0x8u : 0x4u)) return;
    int s = (prv ? 1 : 0) + slot;
    if (s < 4) x->v[s] = v;
}
/* GetString @1800855b0: string operands only (numeric operands give "") */
static const char *gs(Fn *f, const MpArg *x)
{
    if (x->str >= 0) return mp_get_str(f->r, x->str);
    if (x->var < 0 && x->val.t == MPV_STR) return x->val.s ? x->val.s : "";
    return "";
}
/* CPostFormulaSolver::GetFs @180085110: the variable's fs, else the component's fs (+0xf8 = fs 1, see mp_eval.c) */
static const MpFs *fs_of(Fn *f, const MpArg *x)
{
    return x->var >= 0 ? mp_var_fs(f->r, x->var) : mp_fs_get(f->r, 1);
}
/* CPostComponents::FindStatement: the fs numbered `num` or NULL */
static MpFs *find_fs(MpRun *r, int num)
{
    if (num < MP_FSMIN || num >= MP_FSMAX || !r->fs[num - MP_FSMIN].defined) return NULL;
    return &r->fs[num - MP_FSMIN];
}

/* ---- target ---- */
/* CPostFormulaSolver::IsTargetReal @180086300; nested call (no target): ours, the operator table result type
   (type word 2 bits 4..7: 1 real, 2 string, 4 either -> real) */
static int tgt_real(Fn *f)
{
    if (f->tg) {
        if (f->tg->str >= 0) return 0;
        if (f->tg->var >= 0) return 1;
        return f->tg->val.t != MPV_STR;
    }
    return ((f->w2 >> 4) & 0xf) != 2;
}
static int tgt_var(Fn *f, int k) { return (f->tg && f->tg->var >= 0 && f->tg->var + k < f->r->nvars) ? f->tg->var + k : -1; }

/* ---- results ---- */
static void r_clear(Fn *f) { if (f->out->t == MPV_STR) free(f->out->s); memset(f->out, 0, sizeof *f->out); }
static void r_num(Fn *f, double v) { r_clear(f); f->out->t = MPV_NUM; f->out->v[0] = v; }
static void r_str(Fn *f, const char *s) { r_clear(f); f->out->t = MPV_STR; f->out->s = sdup(s); }
/* the target is not assigned by mcpost: hand back its current value (assignment becomes a no-op) */
static void r_keep(Fn *f)
{
    r_clear(f);
    if (f->tg && f->tg->str >= 0) { f->out->t = MPV_STR; f->out->s = sdup(mp_get_str(f->r, f->tg->str)); return; }
    f->out->t = MPV_NUM;
    int i = tgt_var(f, 0);
    if (i >= 0) f->out->v[0] = f->r->vars[i].v[f->tg->prv ? 1 : 0];
    else if (f->tg) f->out->v[0] = f->tg->val.v[0];
}
static void r_vec(Fn *f, const double *v, int k)
{
    r_clear(f); f->out->t = MPV_VEC;
    for (int i = 0; i < k && i < 3; i++) f->out->v[i] = v[i];
    if (k < 3) { int i = tgt_var(f, 2); f->out->v[2] = i >= 0 ? f->r->vars[i].v[f->tg->prv ? 1 : 0] : 0.0; }
}
static void r_mat(Fn *f, const double *m) { r_clear(f); f->out->t = MPV_MAT; for (int i = 0; i < 9; i++) f->out->v[i] = m[i]; }
/* status set before SetValue: the target keeps its value and the formula reports the error */
static void bad(Fn *f, unsigned long long bits) { f->st |= bits; f->hard = 1; r_keep(f); }
/* status raised after the value was produced (mcpost assigns, then reports) */
static void soft(Fn *f, unsigned long long bits) { f->st |= bits; }

/* ==================================================================== SolveFormulaMathFunction @18008de60 */
/* CPostMath::sqrtPost @1800c83c0 */
static double sqrt_post(Fn *f, double x)
{
    if (-1e-12 < x && x < 1e-12) return 0.0;
    if (x < 0.0) { soft(f, 0x40); return 0.0; }
    return sqrt(x);
}
/* CPostMath::logPost @1800c65b0 / log10Post @1800c6570 */
static double log_post(Fn *f, double x, int ten)
{
    if (-1e-12 < x && x < 1e-12) { soft(f, 0x100); return 0.0; }
    if (x < 0.0) { soft(f, 0x200); return 0.0; }
    return ten ? log10(x) : log(x);
}
/* CPostMath::expPost @1800c3730 */
static double exp_post(Fn *f, double x)
{
    if (709.7827 < fabs(x)) { soft(f, 0x80); return 0.0; }
    return exp(x);
}
/* CPostMath::round_long @1800c82c0 */
static int round_long(double x)
{
    if (x < 0.0) return ms_int(ceil((x - 0.5) - 1e-12));
    return ms_int(floor(x + 0.5 + 1e-12));
}
/* CPostMath::mptrunc @1800c69e0 */
static double mptrunc(double x) { return x < 0.0 ? ceil(x - 1e-12) : floor(x + 1e-12); }
/* CPostMath::frac @1800c4f50 */
static double mpfrac(double x) { return x < 0.0 ? x - ceil(x - 1e-12) : x - floor(x + 1e-12); }

static void h_math(Fn *f)
{
    /* every function of SolveFormulaMathFunction requires a real target (IsTargetReal, else status 0x80000) */
    if (!tgt_real(f)) { bad(f, 0x80000); return; }
    const char *nm = f->name;
    double x = gv(f, A(f, 0), 0), y = 0.0;
    if (!strcmp(nm, "not")) y = x == 0.0 ? 1.0 : 0.0;                 /* _op_not */
    else if (!strcmp(nm, "abs")) y = fabs(x);
    else if (!strcmp(nm, "sqrt")) y = sqrt_post(f, x);
    else if (!strcmp(nm, "log")) y = log_post(f, x, 0);
    else if (!strcmp(nm, "log10")) y = log_post(f, x, 1);
    else if (!strcmp(nm, "exp")) y = exp_post(f, x);
    else if (!strcmp(nm, "round")) y = (double)round_long(x);
    else if (!strcmp(nm, "int")) y = mptrunc(x);
    else if (!strcmp(nm, "frac")) y = mpfrac(x);
    else if (!strcmp(nm, "fmtrnd")) {
        /* CPostMath::FormattedNumber(value, GetFs(A), met_tool$ != 0, round = 1): the argument's own format */
        MpFmtCtx c = fmtctx(f->r);
        const MpFs *fs = fs_of(f, A(f, 0));
        y = fs ? mp_fmt_round(fs, metric(f->r), x, &c) : x;
    }
    r_num(f, y);
}

/* ==================================================================== SolveFormulaMathTrigFunction @18008e960 */
/* CPostMath::atn2 @1800c2590 (radians, 0..2pi) */
static double atn2(double y, double x)
{
    if (1e-12 <= fabs(x)) {
        if (1e-12 <= fabs(y)) { double d = atan2(y, x); if (d < 0.0) d += 6.283185307179586; return d; }
        return x < 0.0 ? 3.141592653589793 : 0.0;
    }
    return y < 0.0 ? 4.712388980384688 : 1.570796326794896;
}
static void h_trig(Fn *f)
{
    if (!tgt_real(f)) { bad(f, 0x80000); return; }
    const char *nm = f->name;
    double x = gv(f, A(f, 0), 0), y = 0.0;
    if (!strcmp(nm, "sin")) y = sin(x * D2R);                          /* sinPost @1800c83b0 */
    else if (!strcmp(nm, "cos")) y = cos(x * D2R);                     /* cosPost @1800c2f30 */
    else if (!strcmp(nm, "tan")) {                                     /* tanPost @1800c86f0 */
        if (89.999999999999 < x && x < 90.000000000001) { soft(f, 0x400); y = 0.0; }
        else if (-90.000000000001 < x && x < -89.999999999999) { soft(f, 0x800); y = 0.0; }
        else y = tan(x * D2R);
    } else if (!strcmp(nm, "asin")) {                                  /* asinPost @1800c24e0 */
        if (-1.000000000001 < x && x < -0.999999999999) y = -89.99999999999997;
        else if (0.999999999999 < x && x < 1.000000000001) y = 89.99999999999997;
        else if (-1.0 <= x && x <= 1.0) y = asin(x) * R2D;
        else { soft(f, 0x2000); y = 0.0; }
    } else if (!strcmp(nm, "acos")) {                                  /* acosPost @1800c20d0 */
        if (-1.000000000001 < x && x < -0.999999999999) y = 180.00000000000003;
        else if (x <= 0.999999999999 || 1.000000000001 <= x) {
            if (-1.0 <= x && x <= 1.0) y = acos(x) * R2D;
            else { soft(f, 0x1000); y = 0.0; }
        } else y = 0.0;
    } else if (!strcmp(nm, "atan")) y = atan(x) * R2D;                 /* atanPost @1800c2570 */
    else if (!strcmp(nm, "atan2")) y = atn2(x, gv(f, A(f, 1), 0)) * R2D;   /* atn2(A, B) * 57.29577951308233 */
    r_num(f, y);
}

/* ==================================================================== SolveFormula2dVectorMath @180087570 */
static void h_vec2(Fn *f)
{
    if (!tgt_real(f)) { bad(f, 0x80000); return; }
    const char *nm = f->name;
    const MpArg *a = A(f, 0), *b = A(f, 1);
    double o[3] = {0, 0, 0};
    if (!strcmp(nm, "dot")) {             /* local_90 = A, local_a8 = B: A1*B1 + A0*B0 */
        double a0 = gv(f, a, 0), a1 = gv(f, a, 1), b0 = gv(f, b, 0), b1 = gv(f, b, 1);
        r_num(f, a1 * b1 + a0 * b0); return;
    }
    if (!strcmp(nm, "lng")) {             /* sqrt(y*y + x*x + 0*0) */
        double x = gv(f, a, 0), y = gv(f, a, 1), z = 0.0;
        r_num(f, sqrt(y * y + x * x + z * z)); return;
    }
    if (!strcmp(nm, "axb")) {             /* local_a8 = A, local_90 = B: A0*B1 - B0*A1 */
        double a0 = gv(f, a, 0), a1 = gv(f, a, 1), b0 = gv(f, b, 0), b1 = gv(f, b, 1);
        r_num(f, a0 * b1 - b0 * a1); return;
    }
    if (!strcmp(nm, "vad")) { for (int k = 0; k < 2; k++) o[k] = gv(f, a, k) + gv(f, b, k); }
    else if (!strcmp(nm, "vsb")) { for (int k = 0; k < 2; k++) o[k] = gv(f, a, k) - gv(f, b, k); }
    else if (!strcmp(nm, "vsc")) { double s = gv(f, a, 0); o[0] = gv(f, b, 0) * s; o[1] = gv(f, b, 1) * s; }
    else if (!strcmp(nm, "vsa")) {        /* B normalised (len 0 -> 0,0) times A */
        double s = gv(f, a, 0), x = gv(f, b, 0), y = gv(f, b, 1);
        double l = sqrt(x * x + y * y), ux = 0.0, uy = 0.0;
        if (l != 0.0) { ux = x / l; uy = y / l; }
        o[0] = ux * s; o[1] = uy * s;
    } else if (!strcmp(nm, "vnr")) {
        double x = gv(f, a, 0), y = gv(f, a, 1), l = sqrt(x * x + y * y);
        if (l != 0.0) { o[0] = x / l; o[1] = y / l; }
    } else if (!strcmp(nm, "vlh")) {      /* CPostMath::vlh @1800c8a90: (-y, x) */
        double x = gv(f, a, 0), y = gv(f, a, 1);
        o[0] = -y; o[1] = x;
    } else if (!strcmp(nm, "vrh")) {      /* CPostMath::vrh @1800c8ab0: (y, -x) */
        double x = gv(f, a, 0), y = gv(f, a, 1);
        o[0] = y; o[1] = -x;
    } else if (!strcmp(nm, "rot")) {      /* CPostMath::rotate_pt @1800c8010 about (0,0), cos/sin of A degrees */
        double ang = gv(f, a, 0), px = gv(f, b, 0), py = gv(f, b, 1), cx = 0.0, cy = 0.0;
        double s = sin(ang * D2R), c = cos(ang * D2R);
        double ry = (py - cy) * c + (px - cx) * s;
        double rx = (px - cx) * c - (py - cy) * s;
        o[0] = cx + rx; o[1] = ry + cy;
    } else if (!strcmp(nm, "veq")) { o[0] = gv(f, a, 0); o[1] = gv(f, a, 1); }
    r_vec(f, o, 2);
}

/* ==================================================================== SolveFormula3dVectorMath @180088440 */
/* FUN_180086520: normalise in place (length 0 -> 0,0,0) */
static void unit3(double *v)
{
    double l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l == 0.0) { v[0] = v[1] = v[2] = 0.0; return; }
    v[0] /= l; v[1] /= l; v[2] /= l;
}
/* FUN_180086600: rotate v about unit axis k by ang radians (Rodrigues) */
static void rot_axis(double *o, const double *v, const double *k, double ang)
{
    double k1 = k[1], k2 = k[2], k0 = k[0];
    double d = k0 * v[0] + k1 * v[1] + k2 * v[2];
    double p1 = v[1] - k1 * d, p2 = v[2] - k2 * d, p0 = v[0] - k0 * d;
    double c = cos(ang), s = sin(ang);
    o[0] = (k1 * p2 - k2 * p1) * s + p0 * c + k0 * d;
    o[1] = (k2 * p0 - k0 * p2) * s + p1 * c + k1 * d;
    o[2] = (k0 * p1 - k1 * p0) * s + p2 * c + k2 * d;
}
/* CPostMath::Fill3dPoint @1800bbe90 / @1800bbed0: three consecutive variables, raw slot */
static void fill3(MpRun *r, const char *name, int slot, double *p)
{
    int i = vix(r, name);
    for (int k = 0; k < 3; k++) p[k] = (i >= 0 && i + k < r->nvars) ? r->vars[i + k].v[slot] : 0.0;
}
static void h_vec3(Fn *f)
{
    if (!tgt_real(f)) { bad(f, 0x80000); return; }
    const char *nm = f->name;
    const MpArg *a = A(f, 0), *b = A(f, 1);
    double o[3] = {0, 0, 0};
    if (!strcmp(nm, "dot3")) {            /* local_b8 = A, local_a0 = B: A1*B1 + A0*B0 + A2*B2 */
        double a0 = gv(f, a, 0), a1 = gv(f, a, 1), a2 = gv(f, a, 2), b0 = gv(f, b, 0), b1 = gv(f, b, 1), b2 = gv(f, b, 2);
        r_num(f, a1 * b1 + a0 * b0 + a2 * b2); return;
    }
    if (!strcmp(nm, "lng3")) {
        double x = gv(f, a, 0), y = gv(f, a, 1), z = gv(f, a, 2);
        r_num(f, sqrt(y * y + x * x + z * z)); return;
    }
    if (!strcmp(nm, "vaxb")) {
        double a0 = gv(f, a, 0), a1 = gv(f, a, 1), a2 = gv(f, a, 2), b0 = gv(f, b, 0), b1 = gv(f, b, 1), b2 = gv(f, b, 2);
        double t = b1 * a2;
        o[1] = b0 * a2 - a0 * b2;
        o[2] = a0 * b1 - b0 * a1;
        o[0] = a1 * b2 - t;
    } else if (!strcmp(nm, "vadd")) { for (int k = 0; k < 3; k++) o[k] = gv(f, a, k) + gv(f, b, k); }
    else if (!strcmp(nm, "vsub")) { for (int k = 0; k < 3; k++) o[k] = gv(f, a, k) - gv(f, b, k); }
    else if (!strcmp(nm, "vscl")) { double s = gv(f, a, 0); for (int k = 0; k < 3; k++) o[k] = gv(f, b, k) * s; }
    else if (!strcmp(nm, "vsca")) {
        double s = gv(f, a, 0), v[3]; for (int k = 0; k < 3; k++) v[k] = gv(f, b, k);
        unit3(v); for (int k = 0; k < 3; k++) o[k] = v[k] * s;
    } else if (!strcmp(nm, "vnrm")) {
        double v[3]; for (int k = 0; k < 3; k++) v[k] = gv(f, a, k);
        double l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l != 0.0) { o[0] = v[0] / l; o[1] = v[1] / l; o[2] = v[2] / l; }
    } else if (!strcmp(nm, "rotv")) {     /* rotate B about (axisx$, axisy$, axisz$) by A degrees */
        double ang = gv(f, a, 0), v[3], ax[3]; for (int k = 0; k < 3; k++) v[k] = gv(f, b, k);
        fill3(f->r, "axisx$", 0, ax); unit3(ax);
        rot_axis(o, v, ax, ang * D2R);
    } else if (!strcmp(nm, "rotp")) {     /* rotate point B about the axis through (ptfixx$..) by A degrees */
        double ang = gv(f, a, 0), v[3], pv[3], ax[3], t[3];
        for (int k = 0; k < 3; k++) v[k] = gv(f, b, k);
        fill3(f->r, "ptfixx$", 0, pv); fill3(f->r, "axisx$", 0, ax);
        v[2] = v[2] - pv[2]; v[1] = v[1] - pv[1]; v[0] = v[0] - pv[0];
        unit3(ax);
        rot_axis(t, v, ax, ang * D2R);
        o[2] = t[2] + pv[2]; o[1] = t[1] + pv[1]; o[0] = t[0] + pv[0];
    } else if (!strcmp(nm, "vequ")) { for (int k = 0; k < 3; k++) o[k] = gv(f, a, k); }
    r_vec(f, o, 3);
}

/* ==================================================================== SolveFormulaMatrixMath @18008eb40 */
static void h_mat(Fn *f)
{
    if (!tgt_real(f)) { bad(f, 0x80000); return; }
    const char *nm = f->name;
    const MpArg *a = A(f, 0), *b = A(f, 1);
    double m[9], n[9], o[9];
    if (!strcmp(nm, "vmap")) {            /* CPostMath::mul_mat_vector2 @1800c6a10(B, A): o[i] = row i of B . A */
        double v[3]; for (int k = 0; k < 3; k++) v[k] = gv(f, a, k);
        for (int k = 0; k < 9; k++) m[k] = gv(f, b, k);
        double r3[3];
        for (int i = 0; i < 3; i++) r3[i] = v[1] * m[i * 3 + 1] + v[0] * m[i * 3] + v[2] * m[i * 3 + 2];
        r_vec(f, r3, 3); return;
    }
    if (!strcmp(nm, "matt")) {            /* CPostMath::mattrn @1800c6870 */
        for (int k = 0; k < 9; k++) m[k] = gv(f, a, k);
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) o[j * 3 + i] = m[i * 3 + j];
    } else if (!strcmp(nm, "mmul")) {     /* CPostMath::mul_matrix_33_2 @1800c6ba0(A, B): o[i][j] = sum_k B[i][k] * A[k][j] */
        for (int k = 0; k < 9; k++) { m[k] = gv(f, a, k); n[k] = gv(f, b, k); }
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
            double s = 0.0;
            for (int k = 0; k < 3; k++) s = s + m[k * 3 + j] * n[i * 3 + k];
            o[i * 3 + j] = s;
        }
    } else {                              /* mteq: copy 9 */
        for (int k = 0; k < 9; k++) o[k] = gv(f, a, k);
    }
    r_mat(f, o);
}

/* ==================================================================== SolveFormulaStringSelect @180091c80 */
/* the fstrsel whose target is string argument x (strt_string +0x20); the setsel* functions change the run's copy
   (MpRun.fstrsels, the parsed post stays immutable) */
static MpFstrsel *fstrsel_of(Fn *f, const MpArg *x)
{
    if (x->str < 0 || x->str >= f->r->nstrs) return NULL;
    int s = f->r->strs[x->str].sel;
    if (s < 0 || s >= f->r->nfstrsels) return NULL;
    return &f->r->fstrsels[s];
}
static void h_select(Fn *f)
{
    const char *nm = f->name;
    int real = tgt_real(f);
    if (!strcmp(nm, "updstr")) {
        /* the argument must be a string variable (0x32f / 0x324) */
        if (A(f, 0)->str < 0) { bad(f, 0x80000); return; }
        int sel = 0, si = A(f, 0)->str;
        if (f->r->strs[si].sel >= 0) {
            if (mp_fstrsel_update) sel = mp_fstrsel_update(f->r, si) ? 1 : 0;
            /* NOT DECODED here: CPostFstrsel::SelectString @18009a2e0 (selection is the interpreter's) */
        }
        if (!real) r_str(f, mp_get_str(f->r, si));
        else r_num(f, sel ? 1.0 : 0.0);
        return;
    }
    if (!real) { bad(f, 0x80000); return; }
    if (!strcmp(nm, "grpix")) {
        double x = gv(f, A(f, 0), 0), g = (double)ms_int(gv(f, A(f, 1), 0));
        r_num(f, g <= 0.0 ? g : (double)ms_int(x / g) * g);
        return;
    }
    MpFstrsel *s = fstrsel_of(f, A(f, 0));
    double res = 0.0;
    if (!strcmp(nm, "setseloffs")) {
        res = NOVAL;
        if (s) {
            s->extra[2] = ms_short(gv(f, A(f, 1), 0));     /* +0x32 (ours: extra[2]) */
            /* NOT DECODED: CPostFstrsel::SelectStringValue @18009ad10 returns the numeric value stored with the
               selected string (strt_string +0x28) - MpStrVar has no numeric value; -99999 returned */
        }
    } else if (!strcmp(nm, "setselund")) {
        if (s) { short v = ms_short(gv(f, A(f, 1), 0)); if (v < 0) v = -1; if (v != s->under) res = 1.0; s->under = v; }
    } else if (!strcmp(nm, "setselsiz")) {
        if (s) { short v = ms_short(gv(f, A(f, 1), 0)); if (v < 0) v = -1; if (v != s->count) res = 1.0; s->count = v; }
    } else if (!strcmp(nm, "setselrtn")) {
        if (s) {
            short v = ms_short(gv(f, A(f, 1), 0));
            if (v == 0) { if (s->flags & 2) res = 1.0; s->flags &= ~2u; }
            else if (v == 1) { if (!(s->flags & 2)) res = 1.0; s->flags |= 2u; }
            else if (v == 2) { if (s->flags & 4) res = 1.0; s->flags &= ~4u; }
            else if (v == 3) { if (!(s->flags & 4)) res = 1.0; s->flags |= 4u; }
        }
    } else if (!strcmp(nm, "setselsel")) {
        /* +0x18 selector variable := B; with 3 arguments and C a string: +0x10 first string := C */
        if (s) {
            int nv = A(f, 1)->var;
            if (s->selvar != nv) { res = 1.0; s->selvar = nv; }
            if (f->n > 2 && a_isstr(A(f, 2)) && A(f, 2)->str >= 0) s->base = A(f, 2)->str;
        }
    }
    r_num(f, res);
}

/* ==================================================================== SolveFormulaLookUp @18008d090 */
static const MpLktbl *find_lk(MpRun *r, int num)          /* CPostComponents::FindLookUp */
{
    for (int i = 0; i < r->p->nlktbls; i++) if (r->p->lktbls[i].num == num) return &r->p->lktbls[i];
    return NULL;
}
/* rows: a[i] = first column (strt item +0x10 value, result), b[i] / bstr[i] = second column (+0x20 value / +0x28 string,
   key). The parser stores no first-column strings (strt item +0x18): the string-result lookups find nothing. */
static int lk_keystr(const MpLktbl *t, int i) { return t->bstr && t->bstr[i] >= 0; }

/* CPostFlkTbl::TableLookUpValVal @18007e030 */
static double lk_valval(Fn *f, int num, double x)
{
    const MpLktbl *t = find_lk(f->r, num);
    if (!t) { soft(f, 0x4000); return 0.0; }
    if (t->n <= 0) { soft(f, 0x8000); return 0.0; }
    double best = 1e+38, res = 0.0;
    for (int i = 0; i < t->n; i++) {
        if (!lk_keystr(t, i)) {
            if (fabs(x - t->b[i]) < best) { res = t->a[i]; best = fabs(x - t->b[i]); }
        } else soft(f, 0x4000);
    }
    return res;
}
/* CPostFlkTbl::TableLookUpValStr @18007df40 (Ordinal_2857 = CStringT::Compare, same assumption as mp_eval.c) */
static double lk_valstr(Fn *f, int num, const char *key)
{
    const MpLktbl *t = find_lk(f->r, num);
    if (!t) { soft(f, 0x4000); return 0.0; }
    if (t->n <= 0) { soft(f, 0x8000); return 0.0; }
    for (int i = 0; i < t->n; i++) {
        if (!lk_keystr(t, i)) { soft(f, 0x4000); continue; }
        if (!strcmp(key, mp_get_str(f->r, t->bstr[i]))) return t->a[i];
    }
    return 0.0;
}
/* CPostFlkTbl::TableRangeValVal @18007e230: the result of the last row with key <= x before the first row with
   key > x; row 0 when the first key is already > x */
static double lk_range(Fn *f, int num, double x)
{
    const MpLktbl *t = find_lk(f->r, num);
    if (!t) { soft(f, 0x4000); return 0.0; }
    if (t->n <= 0) { soft(f, 0x8000); return 0.0; }
    int passed = 0; double res = 0.0;
    for (int i = 0; i < t->n; i++) {
        if (t->b[i] <= x) { if (!passed || i == 0) res = t->a[i]; }
        else { passed = 1; if (i == 0) res = t->a[i]; }
    }
    return res;
}
/* CPostFlkTbl::TableIncrement @18007dbf0: every row whose key equals x (1e-12) gets its result + 1 */
static double lk_inc(Fn *f, int num, double x)
{
    MpLktbl *t = (MpLktbl *)find_lk(f->r, num);
    if (!t) { soft(f, 0x4000); return 0.0; }
    if (t->n <= 0) { soft(f, 0x8000); return 0.0; }
    double res = 0.0, prev = 0.0;
    for (int i = 0; i < t->n; i++) {
        res = prev;
        if (fabs(t->b[i] - x) < 1e-12) { res = t->a[i] + 1.0; t->a[i] = res; }
        prev = res;
    }
    return res;
}

/* CPostMath::FindSpaceIndex @1800bc8e0: the `want`-th blank separated token; *st = index before it, *en = index after */
static int find_space_index(const char *s, int want, int *st, int *en, int have, int cont)
{
    int len = (int)strlen(s), i = 0;
    if (want > 0 && len > 0) {
        if (cont == 0) { have = 0; i = 0; }
        else if (want <= have || (i = *en + 1) >= len) goto fail;
        if (have == want) return 1;
        while (i < len) {
            if (s[i] == ' ') { i++; continue; }
            *st = i - 1;
            while (i < len && s[i] != ' ') i++;
            have++;
            *en = i;
            if (len <= i) { if (have == want) return 1; break; }
            if (have == want) return 1;
        }
    }
fail:
    *en = len; *st = len;
    return 0;
}
static double token_value(const char *s, int st, int en)
{
    char b[512]; int n = en - st - 1; if (n < 0) n = 0; if (n > 511) n = 511;
    memcpy(b, s + st + 1, n); b[n] = 0;
    return atof(b);
}
/* rpar / rparsprm loop: tokens from `first` (1-based) into the target's consecutive variables */
static void rpar_loop(Fn *f, const char *s, int first0, int count)
{
    int st = 0, en = 0, found = 0, firsttok = 1, ok;
    int k = first0;                    /* lVar17 / iVar13: tokens already passed */
    double v0 = 0.0;
    do {
        st = en;
        ok = find_space_index(s, k + 1, &st, &en, k, found != 0);
        if (ok) {
            double v = token_value(s, st, en);
            if (firsttok) { gref_set(f->r, "st_str_ix$", 0, (double)st + 2.0); firsttok = 0; }
            gref_set(f->r, "end_str_ix$", 0, (double)en + 1.0);
            int ti = tgt_var(f, found);
            if (ti >= 0) mp_set_var(f->r, ti, f->tg->prv, v);
            if (found == 0) v0 = v;
            found++;
        }
    } while (found < count && (k++, ok));
    if (tgt_var(f, 0) >= 0) r_keep(f);          /* written above */
    else if (found) r_num(f, v0);               /* ours: nested call - first value */
    else r_keep(f);
}

/* CPostMath::RoundFcvtBuffer @1800bfbd0 */
static int round_fcvt_buffer(char *buf, size_t bsz, double v, int ndig, int *decpt, int *sign, short mode)
{
    if (ndig < 0) ndig = 0;
    int m12 = (unsigned short)(mode - 1) <= 1;
    int err = mp_ms_fcvt_s(buf, bsz, v, m12 ? 25 : ndig, decpt, sign);
    int d = *decpt;
    if (d < -20) { buf[0] = '1'; buf[1] = 0; *decpt = -20; return 5; }
    if (d >= 39) { buf[0x26] = 0; for (int k = 0x25; k > 0; k--) buf[k] = '0'; buf[0] = '1'; *decpt = 0x26; return 5; }
    if (!m12) return err;
    if (err) return err;
    int i = (int)strlen(buf) - 1;
    for (;;) {
        int dd = *decpt, c = dd < 1 ? (i > -1) : (dd < i);
        if (!c || buf[i] != '0') break;
        buf[i] = 0; i--;
    }
    int len = (int)strlen(buf), dd = *decpt;
    if (len != 0) {
        if ((len - dd) - ndig < 1) return 0;
        int k = dd + ndig;
        if (k >= 0) {
            int first = 1;
            if (len <= k) return 4;                    /* _MCAssert("PPostMath.cpp", 0x6a7) */
            int L = len;
            if (buf[k] != '4') { buf[k + 1] = 0; L = (int)strlen(buf); }
            int ii = L - 1;
            char *p = buf + ii;
            int carry = '4' < *p;
            if (ii > -1) {
                do {
                    long pos = p - buf;
                    if (pos < k) {
                        if (!carry) return 0;
                        char c = (char)(*p + 1);
                        carry = c > '9';
                        if (carry) c = '0';
                        *p = c;
                        if (first) {
                            if (c == '0' && *decpt <= ii) { *p = 0; goto next; }
                            first = 0;
                        }
                    } else {
                        if (carry) *p = (char)(*p + 1);
                        carry = (k == pos || mode == 1) ? (*p >= '5') : (*p >= '9');
                        *p = 0;
                    }
                next:
                    p--; ii--;
                } while (p - buf > -1);
            }
            if (!carry) return 0;
            if (ii == -1) {
                size_t n = strlen(buf);
                memmove(buf + 1, buf, n + 1);
                buf[0] = '1'; (*decpt)++;
            }
            return 0;
        }
    }
    buf[0] = '0'; buf[1] = 0; *decpt = 0;
    return 0;
}
/* CPostMath::ValidateNumber @1800c1bd0 */
static double validate_number(double v, int *ovf, int small)
{
    double lim = small ? 999999999.9999999 : 1e+20;
    *ovf = 0;
    if (lim < v) { *ovf = 1; return lim; }
    if (v < -lim) { *ovf = 1; return -lim; }
    if (fabs(v) <= (small ? 1e-09 : 1e-20)) return 0.0;
    return v;
}
/* CPostMath::FindPlaceIndex @1800bc770 */
static int find_place_index(double v, short place, short *res, char *buf, size_t bsz, int ndig, int *decpt, int *sign,
                            short mode, int numeric)
{
    int ok = !numeric;
    if (numeric) {
        int ovf; double vv = validate_number(v, &ovf, 1);
        if (ovf) return ok;
        if (round_fcvt_buffer(buf, bsz, vv, ndig, decpt, sign, 1) != 0) return ok;
        ok = 1;
    }
    int len = (int)strlen(buf);
    int idx = (((place >= 0) ? 1 : 0) - 1) - (int)place + *decpt;
    if (idx >= 0 && idx <= len) {
        char one[2] = { buf[idx], 0 };
        *res = (short)atoi(one);
        if (mode != 0 && (double)(int)*res != 0.0 && *sign != 0 && (mode == 1 || idx == 0)) *res = (short)-*res;
    }
    return ok;
}
static void h_plcval(Fn *f)
{
    if (!tgt_real(f)) { bad(f, 0x80000); return; }
    double res = 9999.0;
    short place = (short)ms_int(gv(f, A(f, 1), 0));
    int numeric = !a_isstr(A(f, 0));
    static char cbuf[0x15d];                       /* CPostMath::m_szConvertBuffer */
    if ((numeric && abs((int)place) > 9) || place == 0) { r_num(f, res); return; }
    int decpt = 0, sign = 0;
    if (numeric) {
        short r0 = 0;
        if (find_place_index(gv(f, A(f, 0), 0), place, &r0, cbuf, sizeof cbuf, 9, &decpt, &sign, 1, 1)) res = r0;
        r_num(f, res); return;
    }
    /* string argument: digits with an optional leading '-' and '.' */
    char s[0x15d]; snprintf(s, sizeof s, "%s", gs(f, A(f, 0)));
    if (!s[0]) { r_num(f, res); return; }
    char *m = strchr(s, '-'); int fm = m ? (int)(m - s) : -1;
    if (fm + 1 >= 2) { r_num(f, res); return; }
    int cnt = 0; { char *w = s; for (char *q = s; *q; q++) { if (*q == '-') cnt++; else *w++ = *q; } *w = 0; }
    if (!(cnt == 0 || fm != -1)) { r_num(f, res); return; }
    sign = fm != -1;
    int len = (int)strlen(s);
    char *dp = strchr(s, '.'); int d = dp ? (int)(dp - s) : -1;
    if (d == len - 1) {
        int nd = 0; char *w = s; for (char *q = s; *q; q++) { if (*q == '.') nd++; else *w++ = *q; } *w = 0;
        if (nd != 1) { r_num(f, res); return; }
        d = -1; len--;
    }
    if (len == 0) { r_num(f, res); return; }
    if (d == -1) { d = (int)strlen(s); snprintf(cbuf, sizeof cbuf, "%.*s", d, s); }
    else if (d == 0) snprintf(cbuf, sizeof cbuf, "%s", s + 1);
    else snprintf(cbuf, sizeof cbuf, "%.*s%s", d, s, s + d + 1);
    decpt = d;
    short r0 = 9999;
    if (find_place_index(0.0, place, &r0, cbuf, sizeof cbuf, 9, &decpt, &sign, 2, 0)) res = r0;
    r_num(f, res);
}

/* fprm: parameters of one source collected from the mp_prm stream (code -> text) */
typedef struct { int n, cap; int *code; char **val; } PrmSet;
static int prm_collect(void *ctx, int code, const char *s)
{
    PrmSet *p = ctx;
    if (p->n == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 64;
        p->code = realloc(p->code, p->cap * sizeof *p->code);
        p->val = realloc(p->val, p->cap * sizeof *p->val);
    }
    p->code[p->n] = code; p->val[p->n] = sdup(s); p->n++;
    return 0;
}
static const char *prm_find(const PrmSet *p, int code)
{
    for (int i = 0; i < p->n; i++) if (p->code[i] == code) return p->val[i];
    return NULL;
}
static void prm_free(PrmSet *p) { for (int i = 0; i < p->n; i++) free(p->val[i]); free(p->code); free(p->val); }

static void h_fprm(Fn *f)
{
    /* GetStandardFprmMethod() is never true in mcpost (CPostComponents +0xe0 is 0 from the constructor and only
       SetStandardFprmMethod(0) is called - CPostNciPreProcess::wrtt @1800cd4b0): the table / source path is the one used */
    if (!tgt_real(f)) { bad(f, 0x80000); return; }
    int tbl = ms_int(gv(f, A(f, 0), 0)), src = ms_int(gv(f, A(f, 1), 0));
    long extra = f->n > 2 ? (long)gv(f, A(f, 2), 0) : 0;
    const MpPrmtbl *t = NULL;
    for (int i = 0; i < f->r->p->nprmtbls; i++) if (f->r->p->prmtbls[i].num == tbl) { t = &f->r->p->prmtbls[i]; break; }
    const McamFile *mf = (const McamFile *)f->r->env.prm_ctx;
    double res = 0.0;
    if (!t || !mf) { r_num(f, 0.0); return; }        /* FindParameter == NULL / no machine data -> 0 */
    PrmSet ps; memset(&ps, 0, sizeof ps);
    int got = -1;
    if (src == 1 && extra == 0) got = mp_prm_md(mf, 0, prm_collect, &ps);   /* TableParameterMD @1801251b0 machine object */
    else if (src == 2) got = mp_prm_cd(mf, prm_collect, &ps);              /* TableParameterCD @180124dd0 */
    else if (src == 3) got = mp_prm_group(mf, prm_collect, &ps);           /* TableParameterGroup */
    /* NOT DECODED: src 0 = CPostToolTable::TableParameter (tool table position), src 1 with a machine entity number */
    if (got == 1) {
        res = 1.0;
        for (int i = 0; i < t->n; i++) {
            const char *v = prm_find(&ps, t->code[i]);
            if (t->isstr[i]) {
                char b[64];
                if (!v) { snprintf(b, sizeof b, "%.15lg", NOVAL); v = b; }   /* "%.15lg" of the missing value */
                mp_set_strv(f->r, t->var[i], v);
            } else {
                int vi = t->var[i];
                /* missing parameter -> -99999 (0xc0f869f000000000); the stream gives "%.15lg" text, Mastercam the
                   double itself (ours: precision of 15 digits). prv_ rows (0x321 -> +0x18) are not kept by the parser */
                if (vi >= 0 && vi < f->r->nvars) f->r->vars[vi].v[0] = v ? strtod(v, NULL) : NOVAL;
            }
        }
    }
    prm_free(&ps);
    r_num(f, res);
}

static void h_lookup(Fn *f)
{
    const char *nm = f->name;
    int real = tgt_real(f);
    if (!strcmp(nm, "plcval")) { h_plcval(f); return; }
    if (!strcmp(nm, "fprm")) { h_fprm(f); return; }
    if (!strcmp(nm, "fsg")) {
        /* |A| >= 5e-8 -> B else C (string target: GetString, else GetValue) */
        double v = gv(f, A(f, 0), 0);
        const MpArg *pick = fabs(v) < TOL ? A(f, 2) : A(f, 1);
        if (!real) r_str(f, gs(f, pick)); else r_num(f, gv(f, pick, 0));
        return;
    }
    if (!strcmp(nm, "flook")) {
        int num = ms_int(gv(f, A(f, 0), 0));
        if (!real) {
            /* TableLookUpStrVal @18007ddf0 / TableLookUpStrStr @18007dcc0 need first-column strings: not stored by
               the parser (NOT DECODED until MpLktbl carries them) -> "" */
            r_str(f, ""); return;
        }
        double v = a_isstr(A(f, 1)) ? lk_valstr(f, num, gs(f, A(f, 1))) : lk_valval(f, num, gv(f, A(f, 1), 0));
        r_num(f, v); return;
    }
    if (!strcmp(nm, "frange")) {
        double tn = gv(f, A(f, 0), 0), x = gv(f, A(f, 1), 0);
        if (!real) { r_str(f, ""); return; }    /* TableRangeStrVal @18007e110: first-column strings, see flook */
        r_num(f, lk_range(f, ms_int(tn), x)); return;
    }
    if (!real) { bad(f, 0x80000); return; }
    if (!strcmp(nm, "fsg1") || !strcmp(nm, "fsg2") || !strcmp(nm, "fsg3")) {
        /* CPostMath::fsg1 @1800c4fb0 / fsg2 @1800c5010 / fsg3 @1800c5070 against xtol$ */
        double v = gv(f, A(f, 0), 0), tol = gref(f->r, "xtol$", 0), y;
        if (nm[3] == '1') y = v < -fabs(tol) ? 0.0 : 1.0;
        else if (nm[3] == '2') y = fabs(v) < tol ? 0.0 : 1.0;
        else y = (tol <= fabs(v)) ? (0.0 <= v ? 2.0 : 0.0) : 1.0;
        r_num(f, y); return;
    }
    if (!strcmp(nm, "finc")) { r_num(f, lk_inc(f, ms_int(gv(f, A(f, 0), 0)), gv(f, A(f, 1), 0))); return; }
    if (!strcmp(nm, "rpar")) {
        gref_set(f->r, "st_str_ix$", 0, 0.0); gref_set(f->r, "end_str_ix$", 0, 0.0);
        char *s = sdup(gs(f, A(f, 0)));
        rpar_loop(f, s, 0, ms_int(gv(f, A(f, 1), 0)));
        free(s); return;
    }
    if (!strcmp(nm, "rparsprm")) {
        gref_set(f->r, "st_str_ix$", 0, 0.0); gref_set(f->r, "end_str_ix$", 0, 0.0);
        int si = six(f->r, "sparameter$");
        char *s = sdup(si >= 0 ? (f->r->strs[si].val ? f->r->strs[si].val : "") : "");
        rpar_loop(f, s, ms_int(gv(f, A(f, 0), 0)) - 1, ms_int(gv(f, A(f, 1), 0)));
        free(s); return;
    }
    if (!strcmp(nm, "rparsngl")) {
        gref_set(f->r, "st_str_ix$", 0, 0.0); gref_set(f->r, "end_str_ix$", 0, 0.0);
        char *s = sdup(gs(f, A(f, 0)));
        int st = 0, en = 0;
        if (find_space_index(s, ms_int(gv(f, A(f, 1), 0)), &st, &en, 0, 0)) {
            double v = token_value(s, st, en);
            gref_set(f->r, "st_str_ix$", 0, (double)st + 2.0);
            gref_set(f->r, "end_str_ix$", 0, (double)en + 1.0);
            r_num(f, v);
        } else r_keep(f);
        free(s); return;
    }
}

/* ==================================================================== SolveFormulaBuffer @180089d00 */
static const MpFbufDef *find_fbuf(MpRun *r, int num)        /* CPostComponents::FindBuffer @18004b270 */
{
    for (int i = 0; i < r->p->nfbufs; i++) if (r->p->fbufs[i].num == num) return &r->p->fbufs[i];
    return NULL;
}
/* fbuf N type size [read [string]] (CPostFbuf::ParseFbuf @180075f60): p[0] type (flags 1 / 2), p[1] record size,
   p[2] read-at-open (flag 4), p[3] string buffer (flag 8; also when size == 80 and no 4th number - the parser zero
   fills missing numbers, ours: size 80 with p[2] == p[3] == 0) */
static int fb_size(const MpFbufDef *d) { return (short)(int)d->p[1]; }
static int fb_isstr(const MpFbufDef *d) { return d->p[3] != 0 || d->str || (fb_size(d) == 0x50 && d->p[2] == 0 && d->p[3] == 0); }
static int fb_open(MpRun *r, int b) { return r->buf[b].num.v != NULL || r->buf[b].s != NULL || r->buf[b].defined > 1; }

/* CPostRunBuffers::CheckValidBinaryBuffer @180112de0: 1 valid, 2 invalid (message only), 0 open failed */
static int buf_check(Fn *f, int b, int idx, int reading)
{
    if ((unsigned)(b - 1) >= 10) { mp_rt_error(f->r, 0, "buffer %d: invalid buffer number", b); return 2; }
    if ((idx < 0 && reading) || (idx < 1 && !reading)) { mp_rt_error(f->r, 0, "buffer %d: invalid index %d", b, idx); return 2; }
    const MpFbufDef *d = find_fbuf(f->r, b);
    if (!d) { mp_rt_error(f->r, 0, "buffer %d: not defined", b); return 2; }
    if (!fb_open(f->r, b)) {
        /* CPostRunBuffers::OpenBufferFile @180113e10: new CMemFile; read flag -> ConvertTextToBinaryBuffer */
        f->r->buf[b].defined = 2;                       /* ours: 2 = memory file open */
        if (d->p[2] != 0) {
            if (mp_buf_text_in) { if (!mp_buf_text_in(f->r, b)) return 0; }
            /* NOT DECODED here: ConvertTextToBinaryBuffer @1801134a0 (text file -> buffer) */
        }
    }
    return 1;
}
static int buf_noindex(Fn *f) { return gref(f->r, "buf_no_index$", 0) != 0.0; }

/* ---- fstack (CPostRunStack): stack N = list of records of `size` doubles ---- */
static int stk_find(MpRun *r, long num)           /* CPostComponents::FindStack: first entry with the number */
{
    for (int i = 0; i < r->p->nstacks; i++) if (r->p->stacks[i].num == num) return i;
    return -1;
}
static void stk_insert(MpRun *r, int s, int at, double *rec)
{
    if (r->stk[s].n >= r->stk[s].cap) {
        r->stk[s].cap = r->stk[s].cap ? r->stk[s].cap * 2 : 10;   /* FUN_1801ee600(.., 10) block size */
        r->stk[s].rec = realloc(r->stk[s].rec, r->stk[s].cap * sizeof(double *));
    }
    memmove(r->stk[s].rec + at + 1, r->stk[s].rec + at, (r->stk[s].n - at) * sizeof(double *));
    r->stk[s].rec[at] = rec; r->stk[s].n++;
}
static void stk_remove(MpRun *r, int s, int at)
{
    free(r->stk[s].rec[at]);
    memmove(r->stk[s].rec + at, r->stk[s].rec + at + 1, (r->stk[s].n - at - 1) * sizeof(double *));
    r->stk[s].n--;
}
/* CPostRunStack::AssignFromArray @1801ee7c0: the record into the target's `size` consecutive variables; returns 1
   (disassembly 0x1801ee883 `mov al,1`; the Ghidra listing shows `return 0`) */
static double stk_assign_from(Fn *f, const double *rec, int size)
{
    for (int k = 0; k < size; k++) { int i = tgt_var(f, k); if (i >= 0) var_put(f->r, i, f->tg->prv, 0, rec[k]); }
    return 1.0;
}
/* CPostRunStack::AssignToArray @1801ee8a0 */
static double *stk_assign_to(Fn *f, int size)
{
    double *rec = calloc(size > 0 ? size : 1, sizeof *rec);
    for (int k = 0; k < size; k++) {
        int i = tgt_var(f, k);
        rec[k] = i >= 0 ? mp_get_var(f->r, i, f->tg->prv) : (f->tg ? f->tg->val.v[k < 9 ? k : 0] : 0.0);
    }
    return rec;
}
static void stack_fn(Fn *f)
{
    MpRun *r = f->r;
    const char *nm = f->name;
    int s = stk_find(r, (long)ms_int(gv(f, A(f, 0), 0)));
    if (!strcmp(nm, "turn")) {
        /* ProcessRotateFunction @1801ef350: more than 2 records; 0: tail -> head, 1: head -> tail */
        short dir = ms_short(gv(f, A(f, 1), 0));
        double res = 0.0;
        if (s >= 0 && r->stk[s].n > 2 && (dir == 0 || dir == 1)) {
            int n = r->stk[s].n;
            if (dir == 0) { double *t = r->stk[s].rec[n - 1]; memmove(r->stk[s].rec + 1, r->stk[s].rec, (n - 1) * sizeof(double *)); r->stk[s].rec[0] = t; }
            else { double *t = r->stk[s].rec[0]; memmove(r->stk[s].rec, r->stk[s].rec + 1, (n - 1) * sizeof(double *)); r->stk[s].rec[n - 1] = t; }
            res = 1.0;
        }
        r_num(f, res);
        return;
    }
    int idx = ms_int(gv(f, A(f, 1), 0));
    short mode = f->n > 2 ? ms_short(gv(f, A(f, 2), 0)) : 0;     /* IsTwoArgument: mode 0 */
    int size = s >= 0 ? r->p->stacks[s].size : 0;
    double res = 0.0;
    int statusk = 0;                     /* component of B that receives the result */
    if (!strcmp(nm, "pop")) {
        /* ProcessPopFunction @1801eec00 (no list yet: 0) */
        if (s >= 0 && r->stk[s].rec) {
            int n = r->stk[s].n;
            switch (mode) {
            case 0: {                    /* the target := record count */
                int ti = tgt_var(f, 0); if (ti >= 0) var_put(r, ti, f->tg->prv, 0, (double)n);
                res = 1.0; break;
            }
            case 1:                      /* head: read + remove */
                if (n) { res = stk_assign_from(f, r->stk[s].rec[0], size); stk_remove(r, s, 0); }
                break;
            case 2:                      /* drop the head, then read the new head (falls into case 3) */
                if (n) stk_remove(r, s, 0);
                /* fallthrough */
            case 3:                      /* read the head */
                if (r->stk[s].n) res = stk_assign_from(f, r->stk[s].rec[0], size);
                break;
            case 4:                      /* idx -1..1: empty the stack; else remove from record |idx| to the tail
                                            (idx > 0) or to the head (idx < 0) (RemoveStackIndex @1801ef4f0) */
                if ((unsigned)(idx + 1) < 3) {
                    while (r->stk[s].n) stk_remove(r, s, r->stk[s].n - 1);
                    free(r->stk[s].rec); r->stk[s].rec = NULL; r->stk[s].cap = 0;
                    res = 1.0;
                } else if (n) {
                    int a = abs(idx) - 1;
                    if (a < n) {
                        if (idx < 0) { for (int k = a; k >= 0; k--) stk_remove(r, s, k); }
                        else while (r->stk[s].n > a) stk_remove(r, s, r->stk[s].n - 1);
                    }
                    res = 1.0;
                }
                { int ti = tgt_var(f, 0); if (ti >= 0) var_put(r, ti, f->tg->prv, 0, res); }
                r_keep(f);
                return;          /* SolveFormulaBuffer: mode 4 / -1 skip the status assignment */
            case 5: case 6:      /* record idx (1-based): read; 6 also removes it */
                if (idx - 1 >= 0 && idx - 1 < n) {
                    res = stk_assign_from(f, r->stk[s].rec[idx - 1], size);
                    if (mode == 6) stk_remove(r, s, idx - 1);
                }
                break;
            case 7:                      /* tail: read + remove */
                if (n) { res = stk_assign_from(f, r->stk[s].rec[n - 1], size); stk_remove(r, s, n - 1); }
                break;
            case -1:
                /* NOT DECODED here: write every record to fbuf idx (CPostRunBuffers::WriteToBinaryBufferReals) */
                r_keep(f);
                return;
            default: break;
            }
        }
        if (mode == -1 || mode == 4) { r_keep(f); return; }
        if (res == 0.0 && mode == 0) {
            int ti = tgt_var(f, 0); if (ti >= 0) var_put(r, ti, f->tg->prv, 0, 0.0);
        }
        if (mode == 5 || mode == 6) statusk = 1;
    } else {   /* push: ProcessPushFunction @1801eef80, the record is the target's values */
        if (s < 0) { res = 0.0; }
        else if (mode == -1) {
            /* NOT DECODED here: reload the stack from fbuf idx (ReadFromBinaryBufferReals, records added at the head) */
            res = 0.0;
        } else {
            if (!r->stk[s].rec) { r->stk[s].cap = 10; r->stk[s].rec = malloc(10 * sizeof(double *)); }
            int n = r->stk[s].n, ok = 1;
            if (mode == 0) stk_insert(r, s, 0, stk_assign_to(f, size));          /* AddHead */
            else if (mode == 1) stk_insert(r, s, n, stk_assign_to(f, size));     /* AddTail */
            else if (mode == 2) {                                                 /* InsertAfter record idx */
                if (idx - 1 >= 0 && idx - 1 < n) stk_insert(r, s, idx, stk_assign_to(f, size));
                else ok = 0;
            } else ok = 0;
            res = ok ? 1.0 : 0.0;
        }
        if (mode == -1) {   /* the status goes to the target */
            int ti = tgt_var(f, 0); if (ti >= 0) var_put(r, ti, f->tg->prv, 0, res);
            r_keep(f); return;
        }
        if (mode == 2) statusk = 1;
    }
    /* SetValue(B [+1 for pop 5/6, push 2], result) - the result goes to the second argument, not the target */
    int bi = a_var(r, A(f, 1), statusk);
    if (bi >= 0) var_put(r, bi, A(f, 1)->prv, 0, res);
    r_keep(f);
}

static void h_buffer(Fn *f)
{
    const char *nm = f->name;
    MpRun *r = f->r;
    int real = tgt_real(f);
    if (!strcmp(nm, "wbuf")) {
        int b = ms_int(gv(f, A(f, 0), 0)), idx = ms_int(gv(f, A(f, 1), 0));
        const MpFbufDef *d = find_fbuf(r, b);
        if (!real) {
            /* WriteToBinaryBufferString @180115070: the target string, min(size, 749) bytes, zero padded */
            int c = buf_check(f, b, idx, 0);         /* not 1: nothing written, WriteToBinaryBufferString returns 1 */
            if (c == 1 && d) {
                int sz = fb_size(d) < 0x2ee ? fb_size(d) : 0x2ed;
                const char *s = f->tg ? mp_get_str(r, f->tg->str) : "";
                if ((int)strlen(s) > sz) mp_rt_error(r, 0, "buffer %d: string longer than %d", b, sz);
                if (r->buf[b].ns < idx) {                 /* PadBinaryBuffer @180114300 */
                    if (r->buf[b].cs < idx) { r->buf[b].cs = idx + 16; r->buf[b].s = realloc(r->buf[b].s, r->buf[b].cs * sizeof(char *)); }
                    while (r->buf[b].ns < idx) r->buf[b].s[r->buf[b].ns++] = sdup("");
                }
                char *cp = malloc(sz + 1); snprintf(cp, sz + 1, "%s", s);
                free(r->buf[b].s[idx - 1]); r->buf[b].s[idx - 1] = cp;
                if (!buf_noindex(f)) idx++;
            }
        } else {
            if (!d) { bad(f, 0x80000); return; }
            int sz = fb_size(d);
            if (buf_check(f, b, idx, 0) == 1) {
                /* WriteToBinaryBufferReals @180114dc0: the target's `size` consecutive values at record idx */
                MpDbuf *db = &r->buf[b].num;
                int need = (idx - 1) * sz + sz;
                if (db->cap < need) { db->cap = need + 64; db->v = realloc(db->v, db->cap * sizeof(double)); }
                while (db->n < (idx - 1) * sz + 1 && db->n < need) db->v[db->n++] = 0.0;   /* PadBinaryBuffer: 8 bytes at a time */
                for (int k = 0; k < sz; k++) {
                    int ti = tgt_var(f, k);
                    double v = ti >= 0 ? mp_get_var(r, ti, f->tg->prv) : (f->tg ? f->tg->val.v[k < 9 ? k : 0] : 0.0);
                    int at = (idx - 1) * sz + k;
                    if (at < db->n) db->v[at] = v; else db->v[db->n++] = v;
                }
                if (!buf_noindex(f)) idx++;
            }
        }
        /* SetValue(B, index) - the target itself is not assigned */
        int bi = a_var(r, A(f, 1), 0);
        if (bi >= 0) mp_set_var(r, bi, A(f, 1)->prv, (double)idx);
        r_keep(f);
        return;
    }
    if (!strcmp(nm, "rbuf")) {
        int b = ms_int(gv(f, A(f, 0), 0)), idx = ms_int(gv(f, A(f, 1), 0));
        const MpFbufDef *d = find_fbuf(r, b);
        if (!real) {
            if (idx < 0) { bad(f, 0x80000); return; }
            /* ReadFromBinaryBufferString @180114800 */
            int c = buf_check(f, b, idx, 1);
            if (c != 1) {                           /* invalid / not opened: the read returns 1 with "" */
                r_str(f, "");
                int bi = a_var(r, A(f, 1), 0);
                if (bi >= 0) mp_set_var(r, bi, A(f, 1)->prv, (double)idx);
                return;
            }
            if (!d || idx - 1 >= r->buf[b].ns || idx < 1) {
                mp_rt_error(r, 0, "buffer %d: read past the end (record %d)", b, idx);
                f->hard = 1; r_keep(f); return;
            }
            char *s = sdup(r->buf[b].s[idx - 1]);
            if (!buf_noindex(f)) idx++;
            r_str(f, s); free(s);
            int bi = a_var(r, A(f, 1), 0);
            if (bi >= 0) mp_set_var(r, bi, A(f, 1)->prv, (double)idx);
            return;
        }
        if (idx == -1) {
            /* SetBinaryBufferGrowSize @180114c50: only before the buffer is opened (memory grow size: ours, unused) */
            double ok = 0.0;
            if ((unsigned)(b - 1) >= 10) mp_rt_error(r, 0, "buffer %d: invalid buffer number", b);
            else if (!d) mp_rt_error(r, 0, "buffer %d: not defined", b);
            else if (fb_open(r, b)) mp_rt_error(r, 0, "buffer %d: already open", b);
            else ok = 1.0;
            r_num(f, ok); return;
        }
        if (idx != 0) {
            /* ReadFromBinaryBufferReals @180114530 (param_5 = 1: reading past the end is an error) */
            int c = buf_check(f, b, idx, 1);
            if (c != 1) { r_keep(f); return; }     /* invalid / open failed: the read returns 1, nothing read */
            if (!d) { bad(f, 0x80000); return; }
            int sz = fb_size(d);
            MpDbuf *db = &r->buf[b].num;
            int off = sz * (idx - 1);
            if (!(off < db->n)) { mp_rt_error(r, 0, "buffer %d: read past the end (record %d)", b, idx); bad(f, 0x80000); return; }
            double vals[9] = {0}; double last = 0.0;
            for (int k = 0; k < sz; k++) {
                if (off + k < db->n) last = db->v[off + k];     /* a short read keeps the previous value */
                int ti = tgt_var(f, k);
                if (ti >= 0) mp_set_var(r, ti, f->tg->prv, last);
                if (k < 9) vals[k] = last;
            }
            if (!buf_noindex(f)) idx++;
            int bi = a_var(r, A(f, 1), 0);
            if (bi >= 0) mp_set_var(r, bi, A(f, 1)->prv, (double)idx);
            if (tgt_var(f, 0) >= 0) r_keep(f);
            else if (sz <= 1) r_num(f, vals[0]);
            else { r_clear(f); f->out->t = sz <= 3 ? MPV_VEC : MPV_MAT; for (int k = 0; k < 9; k++) f->out->v[k] = vals[k]; }
            return;
        }
        /* GetBinaryBufferSize @180113d00: number of records */
        int c = buf_check(f, b, 0, 1);
        double n = 0.0;
        if (c == 1 && d) {
            int sz = fb_size(d);
            if (fb_isstr(d)) n = r->buf[b].ns;
            else n = sz > 0 ? (double)(r->buf[b].num.n / sz) : 0.0;
        }
        r_num(f, n);
        return;
    }
    /* pop / push / turn: SolveFormulaBuffer @180089d00 -> CPostRunStack (fstack lists, head first) */
    if (!real) { bad(f, 0x80000); return; }
    stack_fn(f);
}

/* CPostRunBuffers::CloseBinaryBuffer @180112f70 */
static int buf_close(Fn *f, int b)
{
    MpRun *r = f->r;
    if ((unsigned)(b - 1) >= 10) return 0;
    const MpFbufDef *d = find_fbuf(r, b);
    if (d && fb_open(r, b)) {
        if (d->p[0] != 0) {                        /* type 1 / 2: ConvertBinaryBufferToText @180113020 */
            if (mp_buf_text_out) { if (!mp_buf_text_out(r, b)) return 0; }
            /* NOT DECODED here: the text file written at close */
        }
        free(r->buf[b].num.v); memset(&r->buf[b].num, 0, sizeof r->buf[b].num);
        for (int i = 0; i < r->buf[b].ns; i++) free(r->buf[b].s[i]);
        free(r->buf[b].s); r->buf[b].s = NULL; r->buf[b].ns = r->buf[b].cs = 0;
        if (r->buf[b].defined > 1) r->buf[b].defined = 1;
    }
    return 1;
}

/* ==================================================================== SolveFormulaVariable @1800920b0 */
static void h_variable(Fn *f)
{
    const char *nm = f->name;
    MpRun *r = f->r;
    int real = tgt_real(f);
    if (!strcmp(nm, "force") || !strcmp(nm, "unforce")) {
        /* set / clear strt_variable +0x70 bit 0x1000000000 on A .. B (consecutive); B must follow A */
        if (!real) { bad(f, 0x80000); return; }
        const MpArg *a = A(f, 0), *b = A(f, 1);
        if (a_isstr(a) || a_isstr(b)) { r_num(f, 0.0); return; }
        /* B not reached walking from A: status 0x80000, then SetValue(D, 0) (the target IS assigned) */
        if (a->var < 0 || b->var < a->var) { soft(f, 0x80000); r_num(f, 0.0); return; }
        int on = !strcmp(nm, "force");
        for (int i = a->var; i <= b->var && i < r->nvars; i++) r->vars[i].oneshot = on;
        r_num(f, 1.0); return;
    }
    if (!strcmp(nm, "cpyforce")) {
        /* target's one-shot force := A's; B == 1 clears A's. The target is not assigned */
        if (!real) { bad(f, 0x80000); return; }
        const MpArg *a = A(f, 0);
        if (!a_isstr(a) && a->var >= 0) {
            int ti = tgt_var(f, 0);
            double mode = gv(f, A(f, 1), 0);
            if (ti >= 0) r->vars[ti].oneshot = r->vars[a->var].oneshot;
            if (ms_int(mode) == 1) r->vars[a->var].oneshot = 0;
        }
        r_keep(f); return;
    }
    if (!strcmp(nm, "prvneq")) {
        if (!real) { bad(f, 0x80000); return; }
        double res = 1.0;
        int mode = ms_int(gv(f, A(f, 1), 0));
        const MpArg *a = A(f, 0);
        if ((unsigned)mode < 2 && a->var >= 0) {
            double c = mp_get_var(r, a->var, 0), p = mp_get_var(r, a->var, 1);
            if (mode == 1) {                        /* both FormattedNumber with A's fs */
                MpFmtCtx cx = fmtctx(r); const MpFs *fs = fs_of(f, a);
                if (fs) { c = mp_fmt_round(fs, metric(r), c, &cx); p = mp_fmt_round(fs, metric(r), p, &cx); }
            }
            if (fabs(c - p) < TOL) res = r->vars[a->var].oneshot ? -1.0 : 0.0;
        }
        r_num(f, res); return;
    }
    if (!strcmp(nm, "inc_var")) {
        if (!real) { bad(f, 0x80000); return; }
        const MpArg *a = A(f, 0); double res = 0.0;
        if (a->var >= 0 && r->vars[a->var].incvar >= 0 && !(r->vars[a->var].f68 & 0x40)) {
            if (gv(f, A(f, 1), 0) == 0.0) r->vars[a->var].f68 &= ~0x80u; else r->vars[a->var].f68 |= 0x80u;
            res = 1.0;
        }
        r_num(f, res); return;
    }
    if (!strcmp(nm, "setinc_var")) {
        if (!real) { bad(f, 0x80000); return; }
        const MpArg *a = A(f, 0), *b = A(f, 1); double res = 0.0;
        if (a->var >= 0) {
            int c = r->vars[a->var].incvar;
            if (c >= 0 && c < r->nvars && !(r->vars[a->var].f68 & 0x40) && (r->vars[c].f68 & 0x40) && !a_isstr(b)) {
                if (r->vars[c].incvar != b->var) res = 1.0;
                r->vars[c].incvar = b->var;
            }
        }
        r_num(f, res); return;
    }
    if (!strcmp(nm, "updgbl")) {
        /* CPostPostTable::ReplaceFormula: A's global formula := text of B + "\r\n". The result is 1 when replaced
           (no IsTargetReal test) */
        const MpArg *a = A(f, 0); double res = 0.0;
        MpExpr **slot = NULL;
        if (a->str >= 0) slot = &r->strs[a->str].gformula;
        else if (a->var >= 0) slot = &r->vars[a->var].gformula;   /* prv_ (+0x50) not separate in our MpVar */
        if (slot && *slot && mp_parse_formula_text) {
            MpExpr *e = mp_parse_formula_text(r, gs(f, A(f, 1)));
            if (e) { *slot = e; res = 1.0; }          /* the old tree belongs to the post (not freed) */
        }
        /* NOT DECODED without the parser hook: the formula is left unchanged, result 0 */
        if (real) r_num(f, res); else r_keep(f);
        return;
    }
    if (!strcmp(nm, "raw") || !strcmp(nm, "raw3") || !strcmp(nm, "raw6") || !strcmp(nm, "raw9")) {
        if (!real) { bad(f, 0x80000); return; }
        int opt;
        if (f->n <= 2) { opt = ms_int(gv(f, A(f, 1), 0)); if (opt == -1) opt = ms_int(gref(r, "extend$", 0)); }
        else opt = ms_int(gref(r, "extend$", 0));
        /* SetVariableExtendToFunctionOption @1800874d0: 0 set/get slot 0, 1 get slot 2, 2 set slot 2, 3 both */
        int sset, sget;
        if (opt == 0) sset = sget = 0; else if (opt == 1) { sset = 0; sget = 2; } else if (opt == 2) { sset = 2; sget = 0; }
        else if (opt == 3) sset = sget = 2; else { soft(f, 0x80000); sset = sget = 0; }
        int cnt = nm[3] == 0 ? 1 : nm[3] - '0';
        double vals[9];
        for (int k = 0; k < cnt; k++) {
            vals[k] = gv_slot(f, A(f, 0), k, sget);
            int ti = tgt_var(f, k);
            if (ti >= 0) var_put(r, ti, f->tg->prv, sset, vals[k]);
        }
        if (tgt_var(f, 0) >= 0) r_keep(f);
        else if (cnt == 1) r_num(f, vals[0]);
        else { r_clear(f); f->out->t = cnt == 3 ? MPV_VEC : MPV_MAT; for (int k = 0; k < cnt; k++) f->out->v[k] = vals[k]; }
        return;
    }
    if (!strcmp(nm, "vlout") || !strcmp(nm, "vlin")) {
        if (!real) { bad(f, 0x80000); return; }
        int ext = 0, kind = 0;
        if (f->n > 2) {
            /* CPostRunNciUtil::UnAssembleSetGetValueHelper @180129530: |C| = a*100 + ext*10 + kind */
            int c = ms_int(gv(f, A(f, 2), 0)); if (c < 0) c = -c;
            ext = (c % 100) / 10; kind = (c % 100) % 10;
        }
        int opt = 0;
        if (ext != 0) opt = ms_int(gref(r, "extend$", 0)); else if (kind == 1) opt = 2;
        int sset, sget;
        if (opt == 0) sset = sget = 0; else if (opt == 1) { sset = 0; sget = 2; } else if (opt == 2) { sset = 2; sget = 0; }
        else if (opt == 3) sset = sget = 2; else { soft(f, 0x80000); sset = sget = 0; }
        int off = ms_int(gv(f, A(f, 0), 0));
        const MpArg *b = A(f, 1);
        int vi = b->var >= 0 ? b->var + off : -1;
        if (vi < 0 || vi >= r->nvars) { r_keep(f); return; }    /* walked off the variable list */
        if (!strcmp(nm, "vlout")) {
            /* target := variable B+off (the -1..-4 list sizes need kind < 0, which UnAssemble never gives) */
            mp_get_var(r, vi, b->prv);
            int s = (b->prv ? 1 : 0) + sget; double v = s < 4 ? r->vars[vi].v[s] : 0.0;
            int ti = tgt_var(f, 0);
            if (sset == 0 || ti < 0) r_num(f, v);
            else { var_put(r, ti, f->tg->prv, sset, v); r_keep(f); }
        } else {
            /* variable B+off := the target's value; the target is not assigned */
            double v = 0.0; int ti = tgt_var(f, 0);
            if (ti >= 0) { mp_get_var(r, ti, f->tg->prv); int s = (f->tg->prv ? 1 : 0) + sget; v = s < 4 ? r->vars[ti].v[s] : 0.0; }
            var_put(r, vi, b->prv, sset, v);
            r_keep(f);
        }
        return;
    }
    if (!strcmp(nm, "slout") || !strcmp(nm, "slin")) {
        if (real) { bad(f, 0x80000); return; }
        int off = ms_int(gv(f, A(f, 0), 0));
        const MpArg *b = A(f, 1);
        int si = b->str >= 0 ? b->str + off : -1;
        if (!strcmp(nm, "slout")) {
            if (f->n <= 2) {
                if (si >= 0 && si < r->nstrs) r_str(f, mp_get_str(r, si)); else r_keep(f);
            } else {
                /* NOT DECODED: slout(n, "label", opt) - label lists (FindVariableLabel / FindStringLabel /
                   FindPostBlockLabel / FindOperatorLabel) with prmcode$ / scomm$ side outputs */
                r_str(f, "");
            }
        } else {
            if (si >= 0 && si < r->nstrs && f->tg && f->tg->str >= 0) {
                char *s = sdup(mp_get_str(r, f->tg->str)); mp_set_strv(r, si, s); free(s);
            }
            r_keep(f);
        }
        return;
    }
    if (!strcmp(nm, "comment_fx")) {
        int mode = ms_int(gv(f, A(f, 2), 0));
        int n = ms_int(gv(f, A(f, 0), 0));          /* (uint) then tested as int */
        if (!real) {
            const char *s = gs(f, A(f, 1));
            if ((short)mode == 0) {
                char *c = sdup(""); if (n > 0) { free(c); c = sdup(s); if ((int)strlen(c) > n) c[n] = 0; }   /* Left(n) */
                r_str(f, c); free(c); return;
            }
            if ((short)mode == 1) { r_str(f, ""); return; }   /* NOT DECODED: PathCompactPathExA(n + 1) (Win32 shell) */
            if ((short)mode == 2 || (short)mode == 3 || (short)mode == 4) { r_str(f, ""); return; }   /* NOT DECODED: translation table / NCI comment list */
            bad(f, 0x80000); return;
        }
        /* NOT DECODED: numeric modes (probe comment state, comment list insertion) need the NCI reader */
        r_num(f, 0.0); return;
    }
    if (!strcmp(nm, "getstrval") || !strcmp(nm, "setstrval")) {
        /* SolveFormulaVariable @1800920b0: the string's numeric value strt_string +0x28 + slot * 8
           (GetStringValue @180085890 / SetStringValue @1800872c0; slots from SetStringExtendToFunctionOption
           @180087240: option 0 set 0 get 0, 1 set 0 get 1, 2 set 1 get 0, 3 set 1 get 1, else status 0x80000) */
        if (!real) { bad(f, 0x80000); return; }
        int get = !strcmp(nm, "getstrval"), opt;
        if (get) { opt = f->n == 3 ? -1 : ms_int(gv(f, A(f, 1), 0)); }     /* IsTwoArgument: B is the option */
        else opt = f->n == 3 ? ms_int(gv(f, A(f, 2), 0)) : -1;
        if (opt == -1) opt = ms_int(gref(r, "extend$", 0));
        int sset = 0, sget = 0;
        if (opt == 1) sget = 1; else if (opt == 2) sset = 1; else if (opt == 3) sset = sget = 1;
        else if (opt != 0) soft(f, 0x80000);
        const MpArg *a = A(f, 0);
        if (get) { r_num(f, a->str >= 0 ? r->strs[a->str].num[sget] : 0.0); return; }
        /* setstrval: old value read through the SET slot (get slot := set slot), new = B's value or B's string value */
        double old = a->str >= 0 ? r->strs[a->str].num[sset] : 0.0;
        const MpArg *b = A(f, 1);
        double nv = a_isstr(b) ? (b->str >= 0 ? r->strs[b->str].num[sget] : 0.0) : gv(f, b, 0);
        if (a->str >= 0) r->strs[a->str].num[sset] = nv;
        r_num(f, fabs(old - nv) < 5e-08 ? 0.0 : 1.0);
        return;
    }
    if (!strcmp(nm, "altgbl")) {
        /* SolveFormulaVariable altgbl(A, B, C): A's global formula solved with its first variable operand (item
           types 0x329/0x32a in the formula list) replaced by C when that operand is B; target := result. Ours: the
           first operand = the leftmost variable of the expression tree (source order) */
        if (!real) { bad(f, 0x80000); return; }
        const MpArg *a = A(f, 0), *b = A(f, 1);
        MpExpr *g = (a->var >= 0 && a->var < r->nvars) ? r->vars[a->var].gformula : NULL;
        if (!g || b->var < 0) { r_keep(f); return; }
        MpExpr *stack[64]; int sp = 0; MpExpr *first = NULL;
        stack[sp++] = g->kind == EX_ASSIGN ? g->b : g;     /* gformula = 'A = expr' */
        while (sp && !first) {                    /* pre-order, left operand first */
            MpExpr *e = stack[--sp];
            if (!e) continue;
            if (e->kind == EX_VAR) { first = e; break; }
            if (e->kind == EX_FUNC) { for (int k = e->nargs - 1; k >= 0 && sp < 64; k--) stack[sp++] = e->args[k]; }
            else { if (e->b && sp < 64) stack[sp++] = e->b; if (e->a && sp < 64) stack[sp++] = e->a; }
        }
        if (!first || first->idx != b->var) { r_keep(f); return; }
        MpExpr save = *first;
        first->kind = EX_NUM; first->num = gv(f, A(f, 2), 0);
        /* GetValue(A): IsGlobalFormula solves A's formula (A is assigned), then A's value */
        MpVal v; double res = 0.0;
        if (mp_eval(r, g, &v)) mp_val_free(&v);
        *first = save;
        res = r->vars[a->var].v[a->prv ? 1 : 0];
        r_num(f, res);
        return;
    }
    if (!strcmp(nm, "setncstr")) {
        /* SolveFormulaVariable setncstr(mode, "text" [, flags]): MCPost::GetEditorSettings() (the NC editor launched
           after posting - no effect on the NC file). mode 2: insert text into the editor's string set (+0x28,
           std::set: 1 when inserted), 3: erase it (1 when erased), 4: flags (UnAssembleSetGetValueHelper digits ->
           bytes +0 / +1) and editor path +8 when the file exists (1; empty text 1). Ours: the set is process-wide
           (the editor settings are a global singleton) */
        static char **set; static int nset;
        if (!real) { bad(f, 0x80000); return; }
        short mode = ms_short(gv(f, A(f, 0), 0));
        const char *t = gs(f, A(f, 1));
        double res = 0.0;
        if (mode == 2 && *t) {
            int found = 0;
            for (int i = 0; i < nset; i++) if (!strcmp(set[i], t)) found = 1;
            if (!found) { set = realloc(set, (nset + 1) * sizeof *set); set[nset++] = sdup(t); res = 1.0; }
        } else if (mode == 3 && *t) {
            for (int i = 0; i < nset; i++)
                if (!strcmp(set[i], t)) { free(set[i]); set[i] = set[--nset]; res = 1.0; break; }
        } else if (mode == 4) {
            struct stat st;
            if (!*t) res = 1.0; else if (stat(t, &st) == 0) res = 1.0;   /* PathFileExistsA */
        }
        r_num(f, res);
        return;
    }
    /* t_list(mode, status [, index]) (SolveFormulaVariable disassembly 0x1800927c3..0x18009294a): the tool list
       PPostToolList (mp_nciloop_tt.c). result = the tool (-1 none), B = status code, C = the list index (when C is
       given: !IsTwoArgument) */
    {
        int mode = (int)gv(f, A(f, 0), 0);
        int has = f->n > 2, idx = has ? (int)gv(f, A(f, 2), 0) : 0, setc = 0;
        double st = -1.0, val = -1.0;
        if (mp_nci_t_list) mp_nci_t_list(r, mode, has, &idx, &setc, &st, &val);
        if (setc && A(f, 2)->var >= 0) mp_set_var(r, A(f, 2)->var, A(f, 2)->prv, (double)idx);
        if (A(f, 1)->var >= 0) mp_set_var(r, A(f, 1)->var, A(f, 1)->prv, st);
        r_num(f, val);
    }
}

/* ==================================================================== SolveFormulaString @180091330 */
static void h_string(Fn *f)
{
    const char *nm = f->name;
    MpRun *r = f->r;
    int real = tgt_real(f);
    if (!strcmp(nm, "ucase") || !strcmp(nm, "lcase")) {    /* MakeUpper / MakeLower */
        if (real) { bad(f, 0x80000); return; }
        char *s = sdup(gs(f, A(f, 0)));
        for (char *p = s; *p; p++) *p = (char)(nm[0] == 'u' ? toupper((unsigned char)*p) : tolower((unsigned char)*p));
        r_str(f, s); free(s); return;
    }
    if (!strcmp(nm, "strstr")) {
        /* position (1-based) of A in B; st_str_ix$ / end_str_ix$ = start / start + len(A) */
        if (!real) { bad(f, 0x80000); return; }
        const char *needle = gs(f, A(f, 0)), *hay = gs(f, A(f, 1));
        const char *p = strstr(hay, needle);
        int st = 0, en = 0;
        if (p) { st = (int)(p - hay) + 1; en = st + (int)strlen(needle); }
        gref_set(r, "st_str_ix$", 0, (double)st); gref_set(r, "end_str_ix$", 0, (double)en);
        r_num(f, (double)st); return;
    }
    if (!strcmp(nm, "strlen")) {
        if (!real) { bad(f, 0x80000); return; }
        r_num(f, (double)strlen(gs(f, A(f, 0)))); return;
    }
    if (!strcmp(nm, "brksps")) {
        /* split B at character A: B := Left(A-1), target := Mid(A-1) (real target: length of the left part) */
        int n = ms_int(gv(f, A(f, 0), 0));
        char *s = sdup(gs(f, A(f, 1)));
        int len = (int)strlen(s);
        char *left, *right;
        if (len < n) { left = sdup(s); right = sdup(""); }
        else if (n < 2) { left = sdup(""); right = sdup(s); }
        else { right = sdup(s + (n - 1)); s[n - 1] = 0; left = sdup(s); }
        if (A(f, 1)->str >= 0) mp_set_strv(r, A(f, 1)->str, left);
        if (!real) r_str(f, right); else r_num(f, (double)strlen(left));
        free(s); free(left); free(right); return;
    }
    if (!strcmp(nm, "scan")) {
        /* the number after label A in B (sscanf "%lf"); -9999 when absent */
        if (!real) { bad(f, 0x80000); return; }
        const char *lab = gs(f, A(f, 0)), *src = gs(f, A(f, 1));
        const char *p = strstr(src, lab);
        double res = -9999.0; int st = 0, en = 0;
        if (p) {
            st = (int)(p - src) + 1;
            int e7 = (int)strlen(lab) + st;
            double d;
            if (sscanf(src + (e7 - 1), "%lf", &d) == 1) {
                res = d;
                int i6 = e7 - 1, i7;
                int slen = (int)strlen(src);
                for (;;) {
                    i7 = i6; i6 = i7 + 1;
                    char c = i6 < slen ? src[i6] : 0;
                    /* CPostStringUtil::s_ScanNumericMap (LoadCharMaps @1801477f0): '0'..'9' '-' 2, ' ' 4, '\t' '\v'
                       0x1d, other control characters 'b' (message 0x10000000), '\0' 1 */
                    int ty = (c >= '0' && c <= '9') || c == '-' ? 2 : c == ' ' ? 4 : (c == '\t' || c == '\v') ? 0x1d :
                             c == 0 ? 1 : (c > 0 && c < 0x20) ? 'b' : 0;
                    if (ty == 'b') mp_rt_error(r, 0, "scan: invalid character %d", (int)c);
                    else if (ty == 2 || ty == 4 || ty == 0x1d) continue;
                    if (c != '.') break;
                }
                en = i7 + 2;
            }
        }
        gref_set(r, "st_str_ix$", 0, (double)st); gref_set(r, "end_str_ix$", 0, (double)en);
        r_num(f, res); return;
    }
    if (!strcmp(nm, "no2asc")) {
        if (real) { bad(f, 0x80000); return; }
        char s[2] = { (char)ms_int(gv(f, A(f, 0), 0)), 0 };
        r_str(f, s); return;
    }
    if (!strcmp(nm, "no2str")) {
        /* FormattedNumberString(A, round, fs, met_tool$): fs = FindStatement(B) with two arguments, else A's */
        if (real) { bad(f, 0x80000); return; }
        const MpArg *a = A(f, 0);
        const MpFs *fs = NULL;
        if (f->n >= 2 && !(A(f, 1)->var >= 0 && A(f, 1)->var == a->var && A(f, 1)->prv == a->prv)) fs = find_fs(r, ms_int(gv(f, A(f, 1), 0)));
        if (!fs) fs = fs_of(f, a);
        MpFmtCtx cx = fmtctx(r);
        char b[256]; b[0] = 0;
        if (fs) mp_fmt_number_ex(fs, metric(r), gv(f, a, 0), &cx, MP_FMT_ROUND, b, sizeof b, NULL, NULL);
        r_str(f, b); return;
    }
    if (!strcmp(nm, "drs_str")) {
        if (real) { bad(f, 0x80000); return; }
        int mode = ms_int(gv(f, A(f, 0), 0));
        char b[512]; b[0] = 0;
        if (mode == 0) {                           /* PadStringWithSpacesByLength(spaces$) */
            int n = (short)ms_int(gref(r, "spaces$", 0)); int k = 0;
            for (; k < n && k < (int)sizeof b - 1; k++) b[k] = ' ';
            b[k] = 0;
        } else if (mode == 1) {                    /* CPostOutput::AppendEndOfBlock @1800d9660 on "" */
            int k = 0;
            if (gref(r, "use_alt_eob$", 0) != 0.0) {
                b[k++] = (char)ms_int(gref(r, "eob$", 0));
                char c2 = (char)ms_int(gref(r, "eob$", 1));
                if (c2) b[k++] = c2;
            }
            b[k] = 0;
            if (gref(r, "omitcrlf$", 0) == 0.0) {
                const char *eol = mp_out_eol ? mp_out_eol(r) : "\n";   /* ours without the hook: "\n" */
                snprintf(b + k, sizeof b - k, "%s", eol);
            }
        } else if (mode == 2 && f->n >= 2 && A(f, 1)->var >= 0) {
            /* DressedFormattedNumberString of B (prefix + number + suffix as output) */
            mp_fmt_var_text(r, A(f, 1)->var, mp_get_var(r, A(f, 1)->var, A(f, 1)->prv), b, sizeof b, 1);
        } else { bad(f, 0x80000); return; }
        r_str(f, b); return;
    }
}

/* ==================================================================== SolveFormulaFormat @18008aed0 */
static MpFmt *fmt_of(MpRun *r, const MpArg *x)       /* CPostFormulaSolver::GetFmt @180084fd0 */
{
    if (x->var < 0 || x->var >= r->nvars) return NULL;
    int i = r->vars[x->var].fmt;
    return (i >= 0 && i < r->nfmts) ? &r->fmts[i] : NULL;
}
static void h_format(Fn *f)
{
    const char *nm = f->name;
    MpRun *r = f->r;
    if (!tgt_real(f)) { bad(f, 0x80000); return; }
    double res = 0.0;
    if (!strcmp(nm, "newfs")) {                /* B's fs := statement A */
        int fsn = ms_short(gv(f, A(f, 0), 0));
        if (A(f, 1)->var >= 0 && find_fs(r, fsn)) { r->vars[A(f, 1)->var].fs = fsn; res = 1.0; }
    } else if (!strcmp(nm, "copyfs")) {        /* digits, round digits and max values of B into A (flags kept) */
        MpFs *d = find_fs(r, ms_short(gv(f, A(f, 0), 0))), *s = find_fs(r, ms_short(gv(f, A(f, 1), 0)));
        if (d && s) {
            memcpy(d->idig, s->idig, sizeof d->idig); memcpy(d->ddig, s->ddig, sizeof d->ddig);
            memcpy(d->round, s->round, sizeof d->round); memcpy(d->maxv, s->maxv, sizeof d->maxv);
            res = 1.0;
        }
    } else if (!strcmp(nm, "modfs")) {         /* option A of statement B (strt_fs +0x28 flags); 1 = changed */
        MpFs *s = find_fs(r, ms_short(gv(f, A(f, 1), 0)));
        if (s) {
            short op = ms_short(gv(f, A(f, 0), 0));
            unsigned *fl = &s->flags;
            switch (op) {
            case 0: if (*fl & 1) res = 1; *fl &= ~1u; break;
            case 1: if (!(*fl & 1)) res = 1; *fl |= 1u; break;
            case 2: if (*fl & 2) res = 1; *fl &= ~2u; break;
            case 3: if (!(*fl & 2)) res = 1; *fl |= 2u; break;
            case 4: if (*fl & 4) res = 1; *fl &= ~4u; break;
            case 5: if (!(*fl & 4)) res = 1; *fl = (*fl & ~8u) | 4u; break;
            case 6: if (*fl & 8) res = 1; *fl &= ~8u; break;
            case 7: if (!(*fl & 8)) res = 1; *fl = (*fl & ~4u) | 8u; break;
            case 8: if (*fl & 0x10) res = 1; *fl &= ~0x10u; break;
            case 9: if (!(*fl & 0x10)) res = 1; *fl |= 0x10u; break;
            case 10: if (*fl & 0x20) res = 1; *fl &= ~0x20u; break;
            case 11: if (!(*fl & 0x20)) res = 1; *fl = (*fl & ~0x40u) | 0x20u; break;
            case 12: if (*fl & 0x40) res = 1; *fl &= ~0x40u; break;
            case 13: if (!(*fl & 0x40)) res = 1; *fl = (*fl & ~0x20u) | 0x40u; break;
            case 14: if (*fl & 0x80) res = 1; *fl &= ~0x80u; break;
            case 15: if (!(*fl & 0x80)) res = 1; *fl |= 0x80u; break;
            case 16: if (*fl & 0x100) res = 1; *fl &= ~0x100u; break;
            case 17: if (*fl & 0x100) res = 1; *fl |= 0x100u; break;    /* as decompiled: 1 when it was already set */
            case 18: if (*fl & 0x200) res = 1; *fl &= ~0x200u; break;
            case 19: if (*fl & 0x200) res = 1; *fl |= 0x200u; break;    /* as decompiled */
            case 20: if (*fl & 0x400) res = 1; *fl &= ~0x400u; break;
            case 21: if (*fl & 0x400) res = 1; *fl |= 0x400u; break;    /* as decompiled */
            case 22: if (*fl & 0x800) res = 1; *fl &= ~0x800u; break;
            case 23: if (!(*fl & 0x800)) res = 1; *fl = (*fl & 0xcfffu) | 0x800u; break;
            case 26: if (*fl & 0x2000) res = 1; *fl &= ~0x2000u; break;
            case 27: if (!(*fl & 0x2000)) res = 1; *fl = (*fl & 0xe7ffu) | 0x2000u; break;
            case 28: if (*fl & 0x4000) res = 1; *fl &= ~0x4000u; break;
            case 29: if (!(*fl & 0x4000)) res = 1; *fl |= 0x4000u; break;
            default:
                if (op < 0) {                      /* max value (inch +0x28 / metric +0x30) := |A| */
                    double v = fabs(gv(f, A(f, 0), 0));
                    s->maxv[metric(r) ? 1 : 0] = v; res = 1;
                }
                break;
            }
        }
    } else if (!strcmp(nm, "nwadrs") || !strcmp(nm, "nwsufx")) {
        MpFmt *m = fmt_of(r, A(f, 1));
        if (m) {
            const char *s = gs(f, A(f, 0));
            if (nm[2] == 'a') { free(m->prefix); m->prefix = sdup(s); m->flags = (m->flags & ~4u) | 2u; }
            else { free(m->suffix); m->suffix = sdup(s); m->flags = (m->flags & ~0x10u) | 8u; }
            res = 1.0;
        }
    } else if (!strcmp(nm, "nwadrsval") || !strcmp(nm, "nwsufxval")) {
        MpFmt *m = fmt_of(r, A(f, 1));
        if (!m) { r_keep(f); goto done; }          /* no SetValue */
        if (nm[2] == 'a') { m->prefix_str = A(f, 0)->str; m->flags = (m->flags & ~2u) | 4u; }
        else { m->suffix_str = A(f, 0)->str; m->flags = (m->flags & ~8u) | 0x10u; }
        res = 0.0;   /* NOT DECODED: GetStringValue(A) = the string's numeric value (strt_string +0x28), not in MpStrVar */
    } else if (!strcmp(nm, "copyfmt")) {       /* fmt of B into fmt of A (A's bit 1 kept) */
        MpFmt *d = fmt_of(r, A(f, 0)), *s = fmt_of(r, A(f, 1));
        if (d && s && d != s) {
            free(d->prefix); d->prefix = s->prefix ? sdup(s->prefix) : NULL;
            free(d->suffix); d->suffix = s->suffix ? sdup(s->suffix) : NULL;
            d->prefix_str = s->prefix_str; d->suffix_str = s->suffix_str;
            unsigned keep = d->flags & 1u;
            d->flags = (s->flags & ~1u) | keep;
            res = 1.0;
        } else if (d && s) res = 1.0;
    }
    r_num(f, res);
done:;
}

/* ==================================================================== SolveFormulaFile @18008a320 */
static void upcase(char *s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }
static void h_file(Fn *f)
{
    const char *nm = f->name;
    MpRun *r = f->r;
    if (!tgt_real(f)) { bad(f, 0x80000); return; }
    if (!strcmp(nm, "rename")) { rename(gs(f, A(f, 0)), gs(f, A(f, 1))); r_num(f, 1.0); return; }
    if (!strcmp(nm, "remove")) { r_num(f, remove(gs(f, A(f, 0))) == 0 ? 1.0 : 0.0); return; }
    if (!strcmp(nm, "fexist")) { FILE *fp = fopen(gs(f, A(f, 0)), "r"); if (fp) fclose(fp); r_num(f, fp ? 1.0 : 0.0); return; }
    if (!strcmp(nm, "fclose")) {
        /* the operator slot labelled op_directory in the decompile (slot 0x1802e700c) */
        if (!a_isstr(A(f, 0))) { r_num(f, buf_close(f, ms_int(gv(f, A(f, 0), 0))) ? 1.0 : 0.0); return; }
        const char *path = gs(f, A(f, 0));
        if (!path[0]) { bad(f, 0x80000); return; }
        /* CPostFileUtil::SplitPath: compare upper-case name+ext with snamenc$+sextnc$, snamesub$+sextsub$,
           snameaux$+sextaux$, snameext$+sextext$, snamelcc$+sextlcc$ (CString::Find of the file name in them) */
        const char *base = strrchr(path, '/'); const char *b2 = strrchr(path, '\\');
        if (b2 && (!base || b2 > base)) base = b2;
        base = base ? base + 1 : path;
        char nmx[512]; snprintf(nmx, sizeof nmx, "%s", base); upcase(nmx);
        static const char *const pairs[5][2] = { {"snamenc$", "sextnc$"}, {"snamesub$", "sextsub$"}, {"snameaux$", "sextaux$"},
                                                 {"snameext$", "sextext$"}, {"snamelcc$", "sextlcc$"} };
        double res = 0.0;
        for (int w = 0; w < 5; w++) {
            int a = six(r, pairs[w][0]), b = six(r, pairs[w][1]);
            char cand[1024];
            snprintf(cand, sizeof cand, "%s%s", a >= 0 && r->strs[a].val ? r->strs[a].val : "", b >= 0 && r->strs[b].val ? r->strs[b].val : "");
            upcase(cand);
            if (strstr(cand, nmx)) {
                if (mp_out_close_stream) mp_out_close_stream(r, w);
                /* NOT DECODED without the hook: the stream is not closed (interpreter's output files) */
                res = 1.0; break;
            }
        }
        r_num(f, res); return;
    }
    if (!strcmp(nm, "directory")) {
        /* CPostFileUtil::RemoveDirectoryA / CreateDirectoryA / FileExists (internals not decoded: plain rmdir / mkdir /
           stat) */
        int mode = ms_int(gv(f, A(f, 0), 0)); const char *p = gs(f, A(f, 1));
        double res = 0.0; struct stat sb;
        if (mode == 0) res = rmdir(p) == 0;
        else if (mode == 1) res = mkdir(p, 0777) == 0;
        else if (mode == 2) res = stat(p, &sb) == 0;
        r_num(f, res); return;
    }
    /* fnewsub: CPostSubFiles::FileNewSub / CPostSubsTraditional::FileNewSub - NOT DECODED */
    r_num(f, 0.0);
}

/* ==================================================================== SolveFormulaScreen @180090870 */
static void h_screen(Fn *f)
{
    MpRun *r = f->r;
    if (!strcmp(f->name, "mprint")) {
        const MpArg *a = A(f, 0), *b = A(f, 1);
        char text[1024]; int mode = 0; double res;
        if (!a_isstr(a)) {
            const MpFs *fs = fs_of(f, a); MpFmtCtx cx = fmtctx(r); text[0] = 0;
            if (fs) mp_fmt_number_ex(fs, metric(r), gv(f, a, 0), &cx, MP_FMT_ROUND, text, sizeof text, NULL, NULL);
            res = 0.0;
            if (!a_isstr(b) && f->n >= 2 && !(b->var >= 0 && b->var == a->var && b->prv == a->prv)) mode = 1;
        } else {
            snprintf(text, sizeof text, "%s", gs(f, a));
            res = 1.0;
            if (!a_isstr(b)) mode = 1;
        }
        /* AfxMessageBox(text, mode - 1) when pst_mode$ != 1 (its return value is the result); ours: no UI - printed to
           stderr and the pst_mode$ == 1 result (0) */
        fprintf(stderr, "mprint: %s\n", text);
        if (mode) res = 0.0;
        if (tgt_real(f)) r_num(f, res); else r_keep(f);
        return;
    }
    /* drwlin drwarc db_*: Mastercam database / graphics - NOT DECODED */
    if (tgt_real(f)) r_num(f, 0.0); else r_str(f, "");
}

/* ==================================================================== SolveFormulaBitwiseMath @180089bb0 */
static void h_bitwise(Fn *f)
{
    if (!tgt_real(f)) { bad(f, 0x80000); return; }
    int b = round_long(gv(f, A(f, 1), 0)), a = round_long(gv(f, A(f, 0), 0)), v;
    if (!strcmp(f->name, "xor")) v = b ^ a; else if (!strcmp(f->name, "and")) v = b & a; else v = b | a;
    r_num(f, (double)v);
}

/* ==================================================================== SolveFormulaOther @18008f040 */
static void h_other(Fn *f)
{
    if (!strcmp(f->name, "delay")) {          /* Sleep((int)A * 1000) -> 1 */
        int s = ms_int(gv(f, A(f, 0), 0));
        if (s > 0) sleep((unsigned)s);
        r_num(f, 1.0); return;
    }
    if (!tgt_real(f)) { bad(f, 0x80000); return; }
    /* ger: get_sim_ver_lev - NOT DECODED */
    r_num(f, 0.0);
}

/* not decoded groups: SolveFormulaLaunch / SolveFormulaQuery / SolveFormulaIntersection */
/* ==================================================================== SolveFormulaIntersection @18008b800 */
/* operand layout (GetNextPosition walk, component counter +0x1a): a line = 6 values (x1 y1 . x2 y2 .), an arc = 5
   values (cx cy r start-angle sweep, angles in degrees); the result fills components 0,1 (first point) and 3,4 (second
   point) of the target. t1$/t2$ get the parameters of the first point, prv_t1$/prv_t2$ (GetVarRef k + 1) those of the
   second (k = formula-solver +4, 0 outside the extend mode); xresult$ the number of intersections. The tolerance is
   xtol$ / met_xtol$ (GetVarRefTolerance 0xb). */
static double xs_unit(double *v)                    /* FUN_18003c720 (p_2d normalize) */
{
    double l = sqrt(v[0] * v[0] + v[1] * v[1]);
    if (l != 0.0) { v[0] /= l; v[1] /= l; } else { v[0] = 0.0; v[1] = 0.0; }
    return l;
}
/* CPostMath::t_on_line @1800c8610 (l = x1 y1 x2 y2) */
static double xs_t_line(const double *p, const double *l, double tol)
{
    double dx = l[2] - l[0], dy = l[3] - l[1], d = dy * dy + dx * dx;
    if (tol < d) return ((p[1] - l[1]) * dy + (p[0] - l[0]) * dx) / d;
    return 0.0;
}
/* CPostMath::t_on_arc @1800c84c0 (a = cx cy r sa sweep, radians) */
static double xs_t_arc(const double *p, const double *a, double tol)
{
    if (fabs(a[4]) < tol) return 0.0;
    double dx = p[0] - a[0], dy = p[1] - a[1], ang = 0.0;
    if (1e-12 <= dy * dy + dx * dx) { ang = atan2(dy, dx); if (ang < 0.0) ang += 6.283185307179586; }
    if (!(tol <= fabs(ang - a[3]))) return 0.0;
    double t = (ang - a[3]) / a[4];
    if (t < 0.0) { double w = 6.283185307179586 / a[4]; t = a[4] <= 0.0 ? t - w : t + w; }
    return t;
}
/* CPostMath::pt_near @1800c7910: p1 becomes the point nearer to n */
static void xs_pt_near(double *p1, double *p2, const double *n)
{
    double d1 = sqrt((n[0] - p1[0]) * (n[0] - p1[0]) + (n[1] - p1[1]) * (n[1] - p1[1]));
    double d2 = sqrt((n[0] - p2[0]) * (n[0] - p2[0]) + (n[1] - p2[1]) * (n[1] - p2[1]));
    if (d2 < d1) { double t0 = p1[0], t1 = p1[1]; p1[0] = p2[0]; p1[1] = p2[1]; p2[0] = t0; p2[1] = t1; }
}
/* CPostMath::ln_ln_ints @1800c5b20: 0 = one intersection (pt, t[0] on a, t[1] on b), 1 = degenerate, 2 = parallel
   apart; the collinear-overlap analysis (pt_on_line / v_equal: codes 3 4 -1..-5) is reduced to 3 here because
   SolveFormulaIntersection only distinguishes 0, 1/2 and the rest (xresult$ -1, all outputs -99999) */
static int xs_ln_ln(const double *a, const double *b, double *pt, double *t, double tol)
{
    double ax = a[2] - a[0], ay = a[3] - a[1], bx = b[0] - b[2], by = b[1] - b[3];
    double ex = b[0] - a[0], ey = b[1] - a[1];
    double cr = by * ax - bx * ay, t2 = tol * tol, num = ex * by - ey * bx, dt = by * ay + bx * ax;
    if (!(fabs(dt) < t2) && fabs(cr / dt) < tol) {
        double l = sqrt(bx * bx + by * by);
        if (fabs(l) <= tol) return 2;
        if (tol <= fabs(num / l)) return 2;
        return 3;
    }
    if (fabs(dt) < t2 && fabs(cr) < t2) return 1;
    t[0] = num / cr;
    pt[0] = a[0] + t[0] * ax; pt[1] = a[1] + t[0] * ay;
    t[1] = (ey * ax - ex * ay) / cr;
    return 0;
}
/* CPostMath::la_ints_new @1800c5880 (proj_pt_ln @1800c77f0) */
static int xs_la(const double *l, const double *a, double tol, double *p1, double *p2)
{
    double dx = l[2] - l[0], dy = l[3] - l[1], d = dy * dy + dx * dx;
    if (d == 0.0) return 0;
    double t = ((a[1] - l[1]) * dy + (a[0] - l[0]) * dx) / d;
    double fx = (1.0 - t) * l[0] + t * l[2], fy = (1.0 - t) * l[1] + t * l[3];
    double dist = sqrt((fy - a[1]) * (fy - a[1]) + (fx - a[0]) * (fx - a[0]));
    double dv = a[2] - dist;
    if (!(-tol <= dv)) return 0;
    if (!(1e-08 <= fabs(dv))) { p1[0] = p2[0] = fx; p1[1] = p2[1] = fy; return 1; }
    if (tol < fabs(dv)) {
        int n = dv < 1e-08 ? 1 : 2;
        double h = sqrt(a[2] * a[2] - dist * dist), u[2] = {dx, dy};
        xs_unit(u);
        p1[0] = u[0] * h + fx; p1[1] = u[1] * h + fy;
        p2[0] = u[0] * -h + fx; p2[1] = u[1] * -h + fy;
        return n;
    }
    double u[2] = {fx - a[0], fy - a[1]};
    xs_unit(u);
    p1[0] = fx + u[0] * dv; p1[1] = fy + u[1] * dv;
    p2[0] = fx; p2[1] = fy;
    return 1;
}
/* CPostMath::aa_ints_new @1800c1c40 (post_sign @1800c74b0) */
static double xs_sign(double v) { return v == 0.0 ? 0.0 : v < 0.0 ? -1.0 : 1.0; }
static int xs_aa(const double *a, const double *b, double tol, double *p1, double *p2)
{
    double r1 = a[2], r2 = b[2], dx = a[0] - b[0], dy = a[1] - b[1];
    double dd = dy * dy + dx * dx, dist = sqrt(dd), dr = r1 - r2;
    if (!(tol <= dist)) return fabs(dr) < tol ? -1 : 0;
    if (!(fabs(dr) - dist <= tol && (dist - r1) - r2 <= tol)) return 0;
    double q1 = r1 * r1, q2 = r2 * r2;
    double ka = ((q1 - q2) + dd) / (dist * 2.0), kb = ((q2 - q1) + dd) / (dist * 2.0), h1, h2;
    int n;
    if (r1 - ka <= tol || r2 - kb <= tol) { n = 1; h1 = 0.0; h2 = 0.0; ka = xs_sign(ka) * r1; kb = xs_sign(kb) * r2; }
    else { n = 2; h1 = sqrt(q1 - ka * ka); h2 = sqrt(q2 - kb * kb); }
    double u[2] = {b[0] - a[0], b[1] - a[1]};
    xs_unit(u);
    p1[0] = -u[1] * h1 + u[0] * ka + a[0];
    p1[1] = u[1] * ka + u[0] * h1 + a[1];
    double w[2] = {dx, dy};
    xs_unit(w);
    p2[0] = -w[1] * h2 + w[0] * kb + b[0];
    p2[1] = w[1] * kb + w[0] * h2 + b[1];
    return n;
}
static void xs_line(Fn *f, const MpArg *x, double *l) { l[0] = gv(f, x, 0); l[1] = gv(f, x, 1); l[2] = gv(f, x, 3); l[3] = gv(f, x, 4); }
static void xs_arc(Fn *f, const MpArg *x, double *a)
{
    for (int k = 0; k < 3; k++) a[k] = gv(f, x, k);
    a[3] = gv(f, x, 3) * D2R; a[4] = gv(f, x, 4) * D2R;
}
static void h_intersect(Fn *f)
{
    MpRun *r = f->r;
    if (!tgt_real(f)) { bad(f, 0x80000); return; }
    if (!strcmp(f->name, "breaks")) {
        /* NOT DECODED: breaks() = CPostProcessLinear::BreakLines @1800aa450 with a user postblock (pbreaks_in$ /
           pbreaks_out$, recursion list s_aLinearRecursion) */
        snprintf(r->err, sizeof r->err, "breaks(): not decoded");
        f->st |= 1; f->hard = 1; r_keep(f);
        return;
    }
    double tol = metric(r) ? gref(r, "met_xtol$", 0) : gref(r, "xtol$", 0);
    double p1[2] = {-99999.0, -99999.0}, p2[2] = {-99999.0, -99999.0};
    double t1 = -99999.0, t2 = -99999.0, t1b = -99999.0, t2b = -99999.0, res;
    double nr[2] = {gref(r, "ptnearx$", 0), gref(r, "ptneary$", 0)};
    if (!strcmp(f->name, "xll")) {
        double la[4], lb[4], pt[2], t[2];
        xs_line(f, A(f, 0), la); xs_line(f, A(f, 1), lb);
        int n = xs_ln_ln(la, lb, pt, t, tol);
        if (n == 0) { res = 1.0; p1[0] = p2[0] = pt[0]; p1[1] = p2[1] = pt[1]; t1 = t1b = t[0]; t2 = t2b = t[1]; }
        else res = (n == 1 || n == 2) ? 0.0 : -1.0;
    } else if (!strcmp(f->name, "xla")) {
        double l[4], a[5];
        xs_line(f, A(f, 0), l); xs_arc(f, A(f, 1), a);
        int n = xs_la(l, a, tol, p1, p2);
        if (n == 0 || n == -1) { res = n; p1[0] = p1[1] = p2[0] = p2[1] = -99999.0; }
        else {
            res = n == 1 ? 1.0 : 2.0;
            xs_pt_near(p1, p2, nr);
            t1 = xs_t_line(p1, l, 5e-08); t2 = xs_t_arc(p1, a, 5e-08);
            t1b = xs_t_line(p2, l, 5e-08); t2b = xs_t_arc(p2, a, 5e-08);
        }
    } else {   /* xaa */
        double a[5], b[5];
        xs_arc(f, A(f, 0), a); xs_arc(f, A(f, 1), b);
        int n = xs_aa(a, b, tol, p1, p2);
        res = n;
        if (n == 0 || n == -1) { p1[0] = p1[1] = p2[0] = p2[1] = -99999.0; }
        else {
            xs_pt_near(p1, p2, nr);
            t1 = xs_t_arc(p1, a, 5e-08); t2 = xs_t_arc(p1, b, 5e-08);
            t1b = xs_t_arc(p2, a, 5e-08); t2b = xs_t_arc(p2, b, 5e-08);
        }
    }
    gref_set(r, "t1$", 0, t1); gref_set(r, "t2$", 0, t2);
    gref_set(r, "t1$", 1, t1b); gref_set(r, "t2$", 1, t2b);
    gref_set(r, "xresult$", 0, res);
    int ti = tgt_var(f, 0);
    if (ti >= 0) {
        static const int comp[4] = {0, 1, 3, 4};
        double v[4] = {p1[0], p1[1], p2[0], p2[1]};
        for (int k = 0; k < 4; k++) { int i = tgt_var(f, comp[k]); if (i >= 0) var_put(r, i, f->tg->prv, 0, v[k]); }
    }
    r_keep(f);
}

static void h_nd(Fn *f)
{
    /* NOT DECODED: launch dll runvbs (OS), sysinfo opchng opmatrix opvector acomboinfo uidinfo opinfo groupinfo cdinfo
       mdinfo streaminfo (Mastercam database), breaks xll xla xaa (CPostProcessLinear / CPostMath::ln_ln_ints etc.) */
    if (!strcmp(f->name, "opmatrix") || !strcmp(f->name, "opvector")) { r_keep(f); return; }
    if (tgt_real(f)) r_num(f, 0.0); else r_str(f, "");
}

/* ==================================================================== dispatch */
static const struct { const char *name; FnH h; } FNS[] = {
    {"not", h_math}, {"abs", h_math}, {"sqrt", h_math}, {"log", h_math}, {"log10", h_math}, {"exp", h_math},
    {"round", h_math}, {"int", h_math}, {"frac", h_math}, {"fmtrnd", h_math},
    {"sin", h_trig}, {"cos", h_trig}, {"tan", h_trig}, {"asin", h_trig}, {"acos", h_trig}, {"atan", h_trig}, {"atan2", h_trig},
    {"dot", h_vec2}, {"lng", h_vec2}, {"axb", h_vec2}, {"vad", h_vec2}, {"vsb", h_vec2}, {"vsc", h_vec2}, {"vsa", h_vec2},
    {"vnr", h_vec2}, {"vlh", h_vec2}, {"vrh", h_vec2}, {"rot", h_vec2}, {"veq", h_vec2},
    {"dot3", h_vec3}, {"lng3", h_vec3}, {"vaxb", h_vec3}, {"vadd", h_vec3}, {"vsub", h_vec3}, {"vscl", h_vec3},
    {"vsca", h_vec3}, {"vnrm", h_vec3}, {"rotv", h_vec3}, {"rotp", h_vec3}, {"vequ", h_vec3},
    {"vmap", h_mat}, {"matt", h_mat}, {"mmul", h_mat}, {"mteq", h_mat},
    {"grpix", h_select}, {"setseloffs", h_select}, {"setselund", h_select}, {"setselsiz", h_select},
    {"setselrtn", h_select}, {"setselsel", h_select}, {"updstr", h_select},
    {"plcval", h_lookup}, {"fsg", h_lookup}, {"fsg1", h_lookup}, {"fsg2", h_lookup}, {"fsg3", h_lookup},
    {"flook", h_lookup}, {"frange", h_lookup}, {"finc", h_lookup}, {"rpar", h_lookup}, {"rparsngl", h_lookup},
    {"rparsprm", h_lookup}, {"fprm", h_lookup},
    {"wbuf", h_buffer}, {"rbuf", h_buffer}, {"pop", h_buffer}, {"push", h_buffer}, {"turn", h_buffer},
    {"force", h_variable}, {"unforce", h_variable}, {"cpyforce", h_variable}, {"prvneq", h_variable},
    {"inc_var", h_variable}, {"setinc_var", h_variable}, {"updgbl", h_variable}, {"altgbl", h_variable},
    {"setstrval", h_variable}, {"getstrval", h_variable}, {"t_list", h_variable}, {"comment_fx", h_variable},
    {"setncstr", h_variable}, {"vlout", h_variable}, {"vlin", h_variable}, {"slout", h_variable}, {"slin", h_variable},
    {"raw", h_variable}, {"raw3", h_variable}, {"raw6", h_variable}, {"raw9", h_variable},
    {"breaks", h_intersect}, {"xll", h_intersect}, {"xla", h_intersect}, {"xaa", h_intersect},
    {"ucase", h_string}, {"lcase", h_string}, {"strstr", h_string}, {"strlen", h_string}, {"brksps", h_string},
    {"scan", h_string}, {"no2asc", h_string}, {"no2str", h_string}, {"drs_str", h_string},
    {"newfs", h_format}, {"copyfs", h_format}, {"modfs", h_format}, {"nwadrs", h_format}, {"nwsufx", h_format},
    {"nwadrsval", h_format}, {"nwsufxval", h_format}, {"copyfmt", h_format},
    {"fnewsub", h_file}, {"rename", h_file}, {"remove", h_file}, {"fexist", h_file}, {"fclose", h_file}, {"directory", h_file},
    {"launch", h_nd}, {"dll", h_nd}, {"runvbs", h_nd},
    {"mprint", h_screen}, {"drwlin", h_screen}, {"drwarc", h_screen}, {"db_view", h_screen}, {"db_rot", h_screen},
    {"db_trn", h_screen}, {"db_proc", h_screen}, {"db_clr", h_screen},
    {"sysinfo", h_nd}, {"opchng", h_nd}, {"opmatrix", h_nd}, {"opvector", h_nd}, {"acomboinfo", h_nd}, {"uidinfo", h_nd},
    {"opinfo", h_nd}, {"groupinfo", h_nd}, {"cdinfo", h_nd}, {"mdinfo", h_nd}, {"streaminfo", h_nd},
    {"xor", h_bitwise}, {"and", h_bitwise}, {"ior", h_bitwise},
    {"delay", h_other}, {"ger", h_other},
};

/* status bits -> text (the CPostMessages run messages are not reproduced; ours) */
static const char *status_text(unsigned long long st)
{
    if (st & 0x80000) return "argument / target type";
    if (st & 0x40) return "square root of a negative number";
    if (st & 0x100) return "log of zero";
    if (st & 0x200) return "log of a negative number";
    if (st & 0x80) return "exp overflow";
    if (st & 0x400) return "tan(90)";
    if (st & 0x800) return "tan(-90)";
    if (st & 0x1000) return "acos argument out of range";
    if (st & 0x2000) return "asin argument out of range";
    if (st & 0x4000) return "lookup table missing / row type mismatch";
    if (st & 0x8000) return "lookup table empty";
    if (st & 1) return "not supported";
    return "error";
}

int mp_fn_call(MpRun *r, int op, MpArg *a, int n, const MpArg *target, MpVal *out)
{
    static const MpOpDef *tab; static int ntab; static FnH *map;
    if (!map) {
        tab = mp_op_table(&ntab);
        map = calloc(ntab > 0 ? ntab : 1, sizeof *map);
        for (int i = 0; i < ntab; i++)
            for (size_t k = 0; k < sizeof FNS / sizeof FNS[0]; k++)
                if (tab[i].name && !strcmp(tab[i].name, FNS[k].name)) { map[i] = FNS[k].h; break; }
    }
    memset(out, 0, sizeof *out); out->t = MPV_NUM;
    if (op < 0 || op >= ntab || !map[op]) {
        snprintf(r->err, sizeof r->err, "function %d (%s) not handled by the function library", op,
                 (op >= 0 && op < ntab && tab[op].name) ? tab[op].name : "?");
        return 0;
    }
    Fn f; memset(&f, 0, sizeof f);
    f.r = r; f.op = op; f.name = tab[op].name; f.a = a; f.n = n; f.tg = target; f.out = out; f.w2 = tab[op].w2;
    map[op](&f);
    if (f.st) {
        if (f.hard) {
            snprintf(r->err, sizeof r->err, "%s(): %s (status 0x%llx)", f.name, status_text(f.st), f.st);
            return 0;
        }
        mp_rt_error(r, 0, "%s(): %s (status 0x%llx)", f.name, status_text(f.st), f.st);
    }
    return 1;
}
