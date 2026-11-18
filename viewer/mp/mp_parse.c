/* mp_parse.c - .pst reading: line classification (CPostFindVars::Process @18007ab00), declarations
 * (CPostStringUtil::ParseDeclaration @18014bed0, CPostFmt::ParseFmt @18007eda0, CPostFstrsel::ParseFstrsel @180099700,
 * CPostFlkTbl / CPostFprmTbl / CPostFbuf / CPostFq), and post table build (CPostBuildTable::Process @180040990,
 * CPostPostTable::ProcessPostBlock @1800efeb0 / GetElementType @1800eb2b0 / ParseOutputItem @18014c850 /
 * ProcessFormula @1800ee380 / ProcessPostBoolean). The post table is held as an AST (MpItem / MpExpr, mp_int.h) instead of
 * mcpost's linked token table (ours); the semantics it encodes are cited where they are decided.
 * See ../../formats/mp_engine.md "## Interpreter". */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>
#include "mp_int.h"
#include "mp_parse_tables.h"

int MP_OP_NOT = -1;

/* ------------------------------------------------------------------ utilities */
static void *xrealloc(void *p, size_t n) { void *q = realloc(p, n ? n : 1); if (!q) { perror("mp"); exit(1); } return q; }
static char *xstrdup(const char *s) { size_t n = strlen(s); char *d = xrealloc(NULL, n + 1); memcpy(d, s, n + 1); return d; }
static char *xstrndup(const char *s, size_t n) { char *d = xrealloc(NULL, n + 1); memcpy(d, s, n); d[n] = 0; return d; }

static void warn(MpPost *p, int line, const char *fmt, ...)
{
    char b[600]; va_list ap; va_start(ap, fmt);
    int k = snprintf(b, sizeof b, "%s:%d: ", p->path ? p->path : "pst", line);
    vsnprintf(b + k, sizeof b - k, fmt, ap); va_end(ap);
    p->warn = xrealloc(p->warn, (p->nwarn + 1) * sizeof(char *));
    p->warn[p->nwarn++] = xstrdup(b);
}

const MpOpDef *mp_op_table(int *n) { if (n) *n = mp_nopdefs; return mp_opdefs; }

/* function / named operator lookup (CPostComponents::FindOperatorLabel): names from `not` on (group != 0) */
int mp_op_find(const char *name)
{
    for (int i = 0; i < mp_nopdefs; i++)
        if (mp_opdefs[i].group && !strcmp(mp_opdefs[i].name, name)) return i;
    return -1;
}

/* label comparisons are case-insensitive in mcpost (FindVariableLabel uses CompareNoCase - Ordinal_2863, see
   ReadLabelModifier @18014dca0 which compares "prv_" with it and separately flags "PRV"). */
static unsigned hashname(const char *s) { unsigned h = 5381; for (; *s; s++) h = h * 33 + (unsigned)tolower((unsigned char)*s); return h; }

/* we keep it simpler: linear lists with a small hash per table built on demand */
typedef struct { int *slot; int cap; char **alias; int *aliasidx; int nalias; } NameIdx;

static int name_eq(const char *a, const char *b) { return strcasecmp(a, b) == 0; }

struct Lookup { NameIdx v, s, b; };

static int idx_find(NameIdx *ix, const char *name, int n, const char *(*nm)(const MpPost *, int), const MpPost *p)
{
    if (!ix->slot) return -1;
    unsigned h = hashname(name) & (ix->cap - 1);
    for (int k = 0; k < ix->cap; k++) {
        int i = ix->slot[(h + k) & (ix->cap - 1)];
        if (i < 0) return -1;
        if (i < n && name_eq(nm(p, i), name)) return i;
        if (i >= 0x40000000 && name_eq(ix->alias[i - 0x40000000], name)) return ix->aliasidx[i - 0x40000000];
    }
    return -1;
}
static void idx_add(NameIdx *ix, const char *name, int i);
static void idx_add_alias(NameIdx *ix, const char *name, int i)
{
    ix->alias = xrealloc(ix->alias, (ix->nalias + 1) * sizeof(char *)); ix->aliasidx = xrealloc(ix->aliasidx, (ix->nalias + 1) * sizeof(int));
    ix->alias[ix->nalias] = xstrdup(name); ix->aliasidx[ix->nalias] = i;
    idx_add(ix, name, 0x40000000 + ix->nalias++);
}
static void idx_add(NameIdx *ix, const char *name, int i)
{
    if (!ix->slot) { ix->cap = 4096; ix->slot = xrealloc(NULL, ix->cap * sizeof(int)); memset(ix->slot, 0xff, ix->cap * sizeof(int)); }
    unsigned h = hashname(name) & (ix->cap - 1);
    for (int k = 0; k < ix->cap; k++) {
        int *s = &ix->slot[(h + k) & (ix->cap - 1)];
        if (*s < 0) { *s = i; return; }
    }
}
static const char *vname(const MpPost *p, int i) { return p->vars[i].name; }
static const char *sname(const MpPost *p, int i) { return p->strs[i].name; }
static const char *bname(const MpPost *p, int i) { return p->blocks[i].name; }

/* the lookup indices live beside the post (ours) */
typedef struct { MpPost post; struct Lookup lk; } PostX;
#define LK(p) (&((PostX *)(p))->lk)

int mp_find_var_p(const MpPost *p, const char *name) { return idx_find(&LK(p)->v, name, p->nvars, vname, p); }
int mp_find_str_p(const MpPost *p, const char *name) { return idx_find(&LK(p)->s, name, p->nstrs, sname, p); }
int mp_find_block_p(const MpPost *p, const char *name) { return idx_find(&LK(p)->b, name, p->nblocks, bname, p); }

static int add_var(MpPost *p, const char *name)
{
    if (p->nvars == p->cvars) { p->cvars = p->cvars ? p->cvars * 2 : 2048; p->vars = xrealloc(p->vars, p->cvars * sizeof(MpVar)); }
    MpVar *v = &p->vars[p->nvars]; memset(v, 0, sizeof *v);
    v->name = xstrdup(name); v->fmt = -1; v->incvar = -1;
    /* user variables: CPostFindVars -> CPostVariable::CheckAndAddDecVariable(..., 0, 2, 0xf, ...) with the fs 1 statement
       (CPostVariable::CPostVariable @ FindStatement(1)); flags +0x68 = 1 like the predefined ones (mp_var_flags.tsv) */
    v->fs = 1; v->f68 = 0x1; v->f78 = 0xf;
    idx_add(&LK(p)->v, name, p->nvars);
    return p->nvars++;
}
static int add_str(MpPost *p, const char *name)
{
    if (p->nstrs == p->cstrs) { p->cstrs = p->cstrs ? p->cstrs * 2 : 1024; p->strs = xrealloc(p->strs, p->cstrs * sizeof(MpStrVar)); }
    MpStrVar *s = &p->strs[p->nstrs]; memset(s, 0, sizeof *s);
    s->name = xstrdup(name); s->val = xstrdup(""); s->sel = -1;
    idx_add(&LK(p)->s, name, p->nstrs);
    return p->nstrs++;
}
static int add_block(MpPost *p, const char *name)
{
    if (p->nblocks == p->cblocks) { p->cblocks = p->cblocks ? p->cblocks * 2 : 512; p->blocks = xrealloc(p->blocks, p->cblocks * sizeof(MpBlock)); }
    MpBlock *b = &p->blocks[p->nblocks]; memset(b, 0, sizeof *b);
    b->name = xstrdup(name); b->predef = -1;
    idx_add(&LK(p)->b, name, p->nblocks);
    return p->nblocks++;
}

/* ------------------------------------------------------------------ predefined symbols (ProcessPreDefines @1800a9360) */
static void fs_set(MpFs *f, int ii, int di, int im, int dm, unsigned fl)
{
    memset(f, 0, sizeof *f); f->defined = 1; f->idig[0] = ii; f->ddig[0] = di; f->idig[1] = im; f->ddig[1] = dm; f->flags = fl;
}
static void predefine(MpPost *p)
{
    /* CPostInitPreDefs::ProcessPreDefines @1800a9360: built-in format statements -22..1 via CPostFs::AddStatement
       (num, inch int, inch dec, metric int, metric dec, ..., flags) */
    static const struct { int n, a, b, c, d; unsigned fl; } bfs[] = {
        {-22,4,0,4,0,0x8d4},{-21,4,0,4,0,0x900},{-20,4,0,4,0,0x800},{-19,3,0,3,0,0x8d4},{-18,3,0,3,0,0x900},
        {-17,3,0,3,0,0x800},{-16,2,0,2,0,0x8d4},{-15,2,0,2,0,0x900},{-14,2,0,2,0,0x800},{-13,0,0,0,0,0x800},
        {-12,0,8,0,8,0x844},{-11,4,3,4,3,0x844},{-10,3,5,4,4,0x844},{-9,4,4,5,3,0x844},{-8,4,4,5,3,0x844},
        {-7,4,4,4,4,0x844},{-6,4,3,4,3,0x844},{-5,4,2,5,1,0x844},{-4,4,4,5,3,0x2044},{-3,4,3,4,3,0x844},
        {-2,4,4,5,3,0x844},{-1,4,3,4,3,0x844},{0,4,4,5,3,0x844},{1,4,4,5,3,0x844}};
    for (size_t i = 0; i < sizeof bfs / sizeof *bfs; i++)
        fs_set(&p->fs[bfs[i].n - MP_FSMIN], bfs[i].a, bfs[i].b, bfs[i].c, bfs[i].d, bfs[i].fl);
    for (int i = 0; i < mp_npredef_vars; i++) {
        int k = add_var(p, mp_predef_vars[i].name);
        MpVar *v = &p->vars[k];
        v->predef = 1; v->f68 = mp_predef_vars[i].f68; v->f78 = mp_predef_vars[i].f78;
        v->v[0] = v->v[1] = v->v[2] = v->v[3] = mp_predef_vars[i].def;   /* SetDefaultValues @1801b9960 */
        v->fs = mp_predef_vars[i].nofs ? MP_NOFS : 1;                    /* AddVariable param_3 = FindStatement(1) or none */
        if (mp_predef_vars[i].label[0]) {                                /* CPostFmt::AddAssignment(prefix, NULL fs) */
            if (p->nfmts == p->cfmts) { p->cfmts = p->cfmts ? p->cfmts * 2 : 256; p->fmts = xrealloc(p->fmts, p->cfmts * sizeof(MpFmt)); }
            MpFmt *f = &p->fmts[p->nfmts]; memset(f, 0, sizeof *f);
            f->prefix = xstrdup(mp_predef_vars[i].label); f->suffix = xstrdup(""); f->prefix_str = f->suffix_str = -1; f->fs = MP_NOFS;
            v->fmt = p->nfmts++;
        }
    }
    /* alias names registered with the predefined variable (mp_variables.tsv other_names "op_id$ |op_id$") */
    { static const char *al[][2] = {{"op_id$", "op_id_mp$"}, {"cc_computer$", "cc_computer_mp$"}};
      for (size_t i = 0; i < sizeof al / sizeof *al; i++) { int k = mp_find_var_p(p, al[i][1]); if (k >= 0) idx_add_alias(&LK(p)->v, al[i][0], k); } }
    p->npredef_vars = p->nvars;
    for (int i = 0; i < mp_npredef_strs; i++) {
        int k = add_str(p, mp_predef_strs[i].name);
        free(p->strs[k].val); p->strs[k].val = xstrdup(mp_predef_strs[i].def);
        p->strs[k].predef = 1;
    }
    p->npredef_strs = p->nstrs;
    for (int i = 0; i < mp_npredef_blocks; i++) { int k = add_block(p, mp_predef_blocks[i]); p->blocks[k].predef = i; }
    p->npredef_blocks = p->nblocks;
    MP_OP_NOT = mp_op_find("not");
}

/* ------------------------------------------------------------------ source lines */
typedef struct { char *s; int no; } Line;
typedef struct { Line *l; int n; } Src;

/* comment strip: '#' outside quotes ends the line (s_StandardCharMap '#' = 6; quotes '"' 11 / '\'' 10) */
static void strip_comment(char *s)
{
    char q = 0;
    for (char *c = s; *c; c++) {
        if (q) { if (*c == q) q = 0; continue; }
        if (*c == '"' || *c == '\'') { q = *c; continue; }
        if (*c == '#') { *c = 0; break; }
    }
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == '\x1a')) s[--n] = 0;
}
static int read_src(const char *path, Src *src)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char buf[MP_MAXLINE * 4]; int no = 0;
    memset(src, 0, sizeof *src); int cap = 0;
    while (fgets(buf, sizeof buf, f)) {
        no++;
        size_t n = strlen(buf);
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
        if (src->n == cap) { cap = cap ? cap * 2 : 4096; src->l = xrealloc(src->l, cap * sizeof(Line)); }
        src->l[src->n].s = xstrdup(buf); src->l[src->n].no = no; src->n++;
    }
    fclose(f);
    return 1;
}

/* ------------------------------------------------------------------ tokenizer (ours over the mcpost char maps) */
enum { T_END, T_EOL, T_NAME, T_NUM, T_STR, T_OP };
typedef struct { int t; char *s; double num; char q; int sp; int line; } Tok;   /* sp: whitespace before */
typedef struct { Tok *t; int n, i; MpPost *p; } Toks;

static int is_lab(int c) { return isalnum(c) || c == '_' || c == '$'; }
static void tok_push(Toks *k, int t, const char *s, size_t n, double num, char q, int sp, int line)
{
    k->t = xrealloc(k->t, (k->n + 1) * sizeof(Tok));
    Tok *o = &k->t[k->n++]; o->t = t; o->s = s ? xstrndup(s, n) : NULL; o->num = num; o->q = q; o->sp = sp; o->line = line;
}
static void tokenize(Toks *k, const char *s, int line)
{
    int sp = 1;
    while (*s) {
        if (*s == ' ' || *s == '\t' || *s == '\v' || *s == '\r') { s++; sp = 1; continue; }
        if (*s == '"' || *s == '\'') {         /* CPostStringUtil::ReadUserString: quote kinds 11 ('"') / 10 ('\'') */
            char q = *s++; const char *e = strchr(s, q);
            size_t n = e ? (size_t)(e - s) : strlen(s);
            tok_push(k, T_STR, s, n, 0, q, sp, line); s += n + (e ? 1 : 0); sp = 0; continue;
        }
        if (isdigit((unsigned char)*s) || (*s == '.' && isdigit((unsigned char)s[1]))) {   /* ParsePostDouble @18014d170 */
            char *e; double v = strtod(s, &e);
            if (e > s && (e[-1] == 'e' || e[-1] == 'E')) e--;
            /* a label may start with digits only when it continues with letters (e.g. none in stock posts) */
            tok_push(k, T_NUM, s, (size_t)(e - s), v, 0, sp, line); s = e; sp = 0; continue;
        }
        if (is_lab((unsigned char)*s)) {
            const char *b = s; while (is_lab((unsigned char)*s)) s++;
            tok_push(k, T_NAME, b, (size_t)(s - b), 0, 0, sp, line); sp = 0; continue;
        }
        if ((s[0] == '<' && (s[1] == '=' || s[1] == '>')) || (s[0] == '>' && s[1] == '=') || (s[0] == ':' && s[1] == '=')) {
            tok_push(k, T_OP, s, 2, 0, 0, sp, line); s += 2; sp = 0; continue;
        }
        tok_push(k, T_OP, s, 1, 0, 0, sp, line); s++; sp = 0;
    }
}
static void toks_free(Toks *k) { for (int i = 0; i < k->n; i++) free(k->t[i].s); free(k->t); memset(k, 0, sizeof *k); }
static Tok *tk(Toks *k) { static Tok end = {T_END}; return k->i < k->n ? &k->t[k->i] : &end; }
static Tok *tk1(Toks *k) { static Tok end = {T_END}; return k->i + 1 < k->n ? &k->t[k->i + 1] : &end; }
static int isop(Tok *t, const char *o) { return t->t == T_OP && !strcmp(t->s, o); }
static int isname(Tok *t, const char *n) { return t->t == T_NAME && !strcasecmp(t->s, n); }

/* ------------------------------------------------------------------ label resolution */
/* CPostStringUtil::ReadLabelModifier @18014dca0: a leading "prv_" (case-insensitive) = previous value (code 100) */
static int strip_prv(const char *name, const char **rest)
{
    if (!strncasecmp(name, "prv_", 4) && name[4]) { *rest = name + 4; return 1; }
    *rest = name; return 0;
}
/* label kinds by first character, s_FirstCharMap (CPostStringUtil::LoadCharMaps @1801477f0): s/S 32/33 strings,
   p/l/m P/L/M 34/35 postblocks, f/F 36/37 (format keywords, else variables), letters/_ 30/31 numeric variables.
   CPostStringUtil::ParseOutputItem @18014c850: strings are looked up as strings first then variables, postblocks as
   postblocks first then variables. */
enum { LK_NONE, LK_VAR, LK_STR, LK_BLOCK, LK_FUNC };
static int resolve(MpPost *p, const char *name, int *idx, int allow_func)
{
    int c = name[0];
    if (allow_func) { int o = mp_op_find(name); if (o >= 0) { *idx = o; return LK_FUNC; } }
    if (c == 's' || c == 'S') { int i = mp_find_str_p(p, name); if (i >= 0) { *idx = i; return LK_STR; } }
    if (c == 'p' || c == 'l' || c == 'm' || c == 'P' || c == 'L' || c == 'M') {
        int i = mp_find_block_p(p, name); if (i >= 0) { *idx = i; return LK_BLOCK; }
    }
    int i = mp_find_var_p(p, name); if (i >= 0) { *idx = i; return LK_VAR; }
    i = mp_find_str_p(p, name); if (i >= 0) { *idx = i; return LK_STR; }
    i = mp_find_block_p(p, name); if (i >= 0) { *idx = i; return LK_BLOCK; }
    return LK_NONE;
}

/* ------------------------------------------------------------------ expressions */
static MpExpr *ex_new(int kind) { MpExpr *e = xrealloc(NULL, sizeof *e); memset(e, 0, sizeof *e); e->kind = kind; e->idx = -1; return e; }

/* operator ids (rows of mp_operators.tsv) */
enum { OPI_SPACE = 6, OPI_COLON = 7, OPI_ASSIGN2 = 8, OPI_ASSIGN = 9, OPI_NEG = 10, OPI_POW = 11, OPI_MUL = 12, OPI_DIV = 13,
       OPI_PLUS = 14, OPI_MINUS = 15, OPI_SPACE2 = 16, OPI_EQ = 17, OPI_GT = 18, OPI_LT = 19, OPI_GE = 20, OPI_LE = 21,
       OPI_NE = 22, OPI_AND = 23, OPI_OR = 24 };

typedef struct { Toks *k; MpPost *p; int err; int line; } EP;
static MpExpr *ep_low(EP *e);

/* stop tokens for an expression at paren depth 0: ',' ']' '[' EOL END (CPostPostTable::GetFormulaEnded @1800eb900) */
static int ep_stop(Tok *t) { return t->t == T_END || t->t == T_EOL || isop(t, ",") || isop(t, "]") || isop(t, "[") || isop(t, ")"); }

static MpExpr *ep_primary(EP *e)
{
    Tok *t = tk(e->k);
    if (t->t == T_NUM) { e->k->i++; MpExpr *x = ex_new(EX_NUM); x->num = t->num; return x; }
    if (t->t == T_STR) { e->k->i++; MpExpr *x = ex_new(EX_STRLIT); x->str = xstrdup(t->s); return x; }
    if (isop(t, "(")) {
        e->k->i++; MpExpr *x = ep_low(e);
        if (isop(tk(e->k), ")")) e->k->i++; else { if (!e->err) warn(e->p, e->line, "missing ')'"); e->err = 1; }
        return x;
    }
    if (isop(t, "-")) {   /* unary minus: CPostFormula::SetPrecedenceLevels @180080c90 level 6 (op_minus, first) */
        e->k->i++; MpExpr *x = ex_new(EX_UNOP); x->op = OPI_NEG; x->a = ep_primary(e);
        if (isop(tk(e->k), "^")) { /* -a^b: unary minus binds first (level 6 before ^ level 5) */ }
        return x;
    }
    if (isop(t, "+")) { e->k->i++; return ep_primary(e); }
    if (t->t == T_NAME) {
        e->k->i++;
        const char *rest; int prv = strip_prv(t->s, &rest);
        int idx = -1, kind = resolve(e->p, prv ? rest : t->s, &idx, !prv);
        if (kind == LK_FUNC) {
            MpExpr *x = ex_new(EX_FUNC); x->op = idx;
            if (isop(tk(e->k), "(")) {
                /* function followed by its parenthesis group: evaluated inside that level
                   (CPostFormula::SetParenthesisLevels @180080920 pulls the function item into the inner level) */
                e->k->i++;
                if (!isop(tk(e->k), ")")) {
                    for (;;) {
                        MpExpr *a = ep_low(e);
                        x->args = xrealloc(x->args, (x->nargs + 1) * sizeof(MpExpr *)); x->args[x->nargs++] = a;
                        if (isop(tk(e->k), ",")) { e->k->i++; continue; }
                        break;
                    }
                }
                if (isop(tk(e->k), ")")) e->k->i++; else { if (!e->err) warn(e->p, e->line, "missing ')' after %s", t->s); e->err = 1; }
                /* not(x) and other functions written with parentheses are complete here */
            } else {
                /* function without parentheses: lowest precedence, applies to the rest of the range
                   (SetPrecedenceLevels: functions / not are the last pass, local_b8 == 0) */
                MpExpr *a = ep_low(e);
                x->args = xrealloc(NULL, sizeof(MpExpr *)); x->args[0] = a; x->nargs = 1;
            }
            return x;
        }
        if (kind == LK_VAR) { MpExpr *x = ex_new(EX_VAR); x->idx = idx; x->prv = prv; return x; }
        if (kind == LK_STR) { MpExpr *x = ex_new(EX_STRVAR); x->idx = idx; return x; }
        /* undeclared label in a formula (the stock 5-axis posts read e.g. n_tpln_mch / cuttype that nothing declares):
           mcpost accepts the post; ours: create a numeric variable (value 0) and keep a warning. NOT DECODED */
        warn(e->p, e->line, "undeclared label '%s' in formula (created)", t->s);
        MpExpr *x = ex_new(EX_VAR); x->idx = add_var(e->p, prv ? rest : t->s); x->prv = prv; return x;
    }
    if (!e->err) warn(e->p, e->line, "unexpected '%s' in formula", t->s ? t->s : "end of line");
    e->err = 1;
    if (!ep_stop(t)) e->k->i++;
    return ex_new(EX_NUM);
}
static MpExpr *bin(int op, MpExpr *a, MpExpr *b) { MpExpr *x = ex_new(EX_BINOP); x->op = op; x->a = a; x->b = b; return x; }
/* precedence (SetPrecedenceLevels @180080c90, evaluated first to last): unary minus, ^, * /, + -, comparisons, & |,
   functions/not. Within a level left to right. */
static MpExpr *ep_pow(EP *e) { MpExpr *a = ep_primary(e); while (isop(tk(e->k), "^")) { e->k->i++; a = bin(OPI_POW, a, ep_primary(e)); } return a; }
static MpExpr *ep_mul(EP *e)
{
    MpExpr *a = ep_pow(e);
    for (;;) {
        Tok *t = tk(e->k);
        if (isop(t, "*")) { e->k->i++; a = bin(OPI_MUL, a, ep_pow(e)); }
        else if (isop(t, "/")) { e->k->i++; a = bin(OPI_DIV, a, ep_pow(e)); }
        else return a;
    }
}
static MpExpr *ep_add(EP *e)
{
    MpExpr *a = ep_mul(e);
    for (;;) {
        Tok *t = tk(e->k);
        if (isop(t, "+")) { e->k->i++; a = bin(OPI_PLUS, a, ep_mul(e)); }
        else if (isop(t, "-")) { e->k->i++; a = bin(OPI_MINUS, a, ep_mul(e)); }
        else return a;
    }
}
static MpExpr *ep_cmp(EP *e)
{
    MpExpr *a = ep_add(e);
    for (;;) {
        Tok *t = tk(e->k); int op = -1;
        if (isop(t, "=")) op = OPI_EQ; else if (isop(t, ">")) op = OPI_GT; else if (isop(t, "<")) op = OPI_LT;
        else if (isop(t, ">=")) op = OPI_GE; else if (isop(t, "<=")) op = OPI_LE; else if (isop(t, "<>")) op = OPI_NE;
        if (op < 0) return a;
        e->k->i++; a = bin(op, a, ep_add(e));
    }
}
static MpExpr *ep_andor(EP *e)
{
    MpExpr *a = ep_cmp(e);
    for (;;) {
        Tok *t = tk(e->k);
        if (isop(t, "&")) { e->k->i++; a = bin(OPI_AND, a, ep_cmp(e)); }
        else if (isop(t, "|")) { e->k->i++; a = bin(OPI_OR, a, ep_cmp(e)); }
        else return a;
    }
}
static MpExpr *ep_low(EP *e) { return ep_andor(e); }

/* assignment / formula: TARGET (= | : | :=) expr  (CPostPostTable::ProcessFormula @1800ee380) */
static MpExpr *ep_formula(EP *e, int target_kind, int target_idx, int prv, int op)
{
    MpExpr *t = ex_new(target_kind == LK_STR ? EX_STRVAR : EX_VAR); t->idx = target_idx; t->prv = prv;
    MpExpr *x = ex_new(EX_ASSIGN); x->op = op; x->a = t; x->b = ep_low(e);
    return x;
}

/* ------------------------------------------------------------------ post table (postblock bodies) */
typedef struct { Toks *k; MpPost *p; int block; } BP;
static MpItem *it_new(int code, int line) { MpItem *i = xrealloc(NULL, sizeof *i); memset(i, 0, sizeof *i); i->code = code; i->line = line; i->idx = -1; return i; }

static MpItem *bp_items(BP *b, MpItem **last_if_out);
static MpItem *bp_seq(BP *b, int in_group);

/* one output-list element (GetElementType @1800eb2b0 + ParseOutputItem @18014c850 + ProcessPostBlock cases 0xf..0x15) */
static MpItem *bp_element(BP *b)
{
    Tok *t = tk(b->k); int line = t->line;
    if (t->t == T_STR) {            /* case 0x12/0x13: (quote != '\'') + 0x38d */
        b->k->i++; MpItem *it = it_new(t->q == '\'' ? EL_SQUOTE : EL_DQUOTE, line); it->text = xstrdup(t->s); it->textlen = strlen(t->s);
        return it;
    }
    if (t->t == T_NUM) {            /* case 0x11: CPostString::CheckAndAddAsciiString - number <= 0xff = ASCII character */
        b->k->i++; MpItem *it = it_new(EL_ASCII, line);
        int c = (int)t->num;
        if (t->num != floor(t->num) || c < 0 || c > 0xff) warn(b->p, line, "bad ASCII code %s", t->s);
        it->text = xrealloc(NULL, 2); it->text[0] = (char)c; it->text[1] = 0; it->textlen = 1;
        return it;
    }
    if (isop(t, "[")) {
        b->k->i++; MpItem *it = it_new(EL_GROUP, line);
        it->body = bp_seq(b, 1);
        if (isop(tk(b->k), "]")) b->k->i++; else warn(b->p, line, "missing ']'");
        return it;
    }
    /* label modifier (s_LabelModifierCharMap: '*' 50 '`' 51 '~' 52 '!' 53 '@' 54) glued to the label */
    int mod = 0;
    if (t->t == T_OP && strchr("*`~!@", t->s[0]) && tk1(b->k)->t == T_NAME && !tk1(b->k)->sp) { mod = t->s[0]; b->k->i++; t = tk(b->k); }
    if (t->t != T_NAME) {
        warn(b->p, line, "unexpected '%s' in postblock %s", t->s ? t->s : "?", b->p->blocks[b->block].name);
        b->k->i++; return NULL;
    }
    b->k->i++;
    const char *rest; int prv = strip_prv(t->s, &rest);
    int idx = -1, kind = resolve(b->p, rest, &idx, 0);
    Tok *n = tk(b->k);
    /* formula: label followed by '=' (ParseOutputItem returns 9 / 0xb / 10 when the next delimiter is '=' (0x11)) */
    if (!mod && (isop(n, "=") || isop(n, ":") || isop(n, ":="))) {
        if (kind == LK_NONE) {
            /* undeclared formula target in a postblock: created as a numeric variable (the stock 5-axis posts assign
               'result' without declaring it; mcpost accepts them - creation point NOT DECODED, ours: append) */
            idx = (rest[0] == 's' || rest[0] == 'S') && tk1(b->k)->t == T_STR ? add_str(b->p, rest) : add_var(b->p, rest);
            kind = (rest[0] == 's' || rest[0] == 'S') && tk1(b->k)->t == T_STR ? LK_STR : LK_VAR;
        }
        if (kind != LK_VAR && kind != LK_STR) { warn(b->p, line, "formula target '%s' is not a variable", t->s); kind = LK_NONE; }
        int op = isop(n, "=") ? OPI_ASSIGN : isop(n, ":") ? OPI_COLON : OPI_ASSIGN2;
        b->k->i++;
        EP e = {b->k, b->p, 0, line};
        MpItem *it = it_new(EL_FORMULA, line);
        if (kind == LK_NONE) { ep_low(&e); free(it); return NULL; }
        it->expr = ep_formula(&e, kind, idx, prv, op);
        return it;
    }
    if (kind == LK_NONE) {
        /* qN = numbered prompt question (ParseOutputItem: "q"/"Q" + number -> FindQuestionPrompt, 0x15) */
        if ((t->s[0] == 'q' || t->s[0] == 'Q') && isdigit((unsigned char)t->s[1])) {
            MpItem *it = it_new(EL_QUESTION, line); it->idx = atoi(t->s + 1); return it;
        }
        warn(b->p, line, "unknown label '%s' in postblock %s", t->s, b->p->blocks[b->block].name);
        return NULL;
    }
    if (kind == LK_BLOCK) {
        MpItem *it = it_new(EL_CALL, line); it->idx = idx;
        if (isop(tk(b->k), "(") && !tk(b->k)->sp) {
            /* NOT DECODED: postblock arguments (0x3ec, CPostPostTable::ProcessPostBlockArgument @1800f24a0,
               CPostRunPostTable::TransferBlockArguments); arguments are skipped */
            int d = 0;
            do { Tok *x = tk(b->k); if (x->t == T_END || x->t == T_EOL) break; if (isop(x, "(")) d++; if (isop(x, ")")) d--; b->k->i++; } while (d > 0);
            it->code = EL_CALLARGS;
            warn(b->p, line, "postblock arguments not supported (%s)", t->s);
        }
        return it;
    }
    if (kind == LK_VAR) {
        /* ProcessPostBlock case 0xf: modifier -> element code */
        int code = EL_VAR;
        switch (mod) {
        case '*': code = EL_VAR_FORCE; break;
        case '`': code = EL_VAR_BTICK; break;
        case '~': code = prv ? EL_VAR_DBGPRV : EL_VAR_DEBUG; break;   /* '~prv_' = 0x65 -> 0x388 */
        case '!': code = EL_VAR_UPD; break;
        case '@': code = EL_VAR_RND; break;
        default:  code = prv ? EL_VAR_PRV : EL_VAR; break;
        }
        if (mod && mod != '~' && prv) warn(b->p, line, "modifier with prv_ on %s (treated as plain modifier)", t->s);
        MpItem *it = it_new(code, line); it->idx = idx;
        return it;
    }
    /* string (case 0x10) */
    int code = EL_STR;
    switch (mod) { case '*': code = EL_STR_FORCE; break; case '`': code = EL_STR_BTICK; break;
                   case '~': code = EL_STR_DEBUG; break; case '!': code = EL_STR_UPD; break; default: break; }
    MpItem *it = it_new(code, line); it->idx = idx;
    return it;
}

/* rest of a line (stops before EOL / END / ']'): comma separated elements; if/while consume the rest of the line as
   their body (the boolean's false jump goes to the next postline - CPostPostTable::SetBooleanLinkage / UpdateBooleanJump) */
static MpItem *bp_items2(BP *b, MpItem **last_if_out, int stop_at_else);
static MpItem *bp_items(BP *b, MpItem **last_if_out) { return bp_items2(b, last_if_out, 0); }
static MpItem *bp_items2(BP *b, MpItem **last_if_out, int stop_at_else)
{
    MpItem *head = NULL, **tail = &head; MpItem *lastif = NULL;
    for (;;) {
        Tok *t = tk(b->k);
        if (t->t == T_END || t->t == T_EOL || isop(t, "]")) break;
        if (stop_at_else && isname(t, "else") && (isop(tk1(b->k), ",") || tk1(b->k)->t == T_EOL)) break;
        if (isop(t, ",")) { b->k->i++; continue; }
        MpItem *it = NULL;
        if ((isname(t, "if") || isname(t, "while")) && !(tk1(b->k)->t == T_OP && (isop(tk1(b->k), "=") || isop(tk1(b->k), ":")))) {
            int w = isname(t, "while"); int line = t->line;
            b->k->i++;
            EP e = {b->k, b->p, 0, line};
            it = it_new(w ? EL_WHILE : EL_IF, line);
            it->expr = ep_low(&e);
            if (isop(tk(b->k), ",")) b->k->i++;
            else if (!(tk(b->k)->t == T_EOL || tk(b->k)->t == T_END)) warn(b->p, line, "expected ',' after %s condition", w ? "while" : "if");
            MpItem *inner = NULL;
            it->body = bp_items2(b, &inner, !w);
            if (!w && isname(tk(b->k), "else")) {
                /* else inside the same postline ("[if a, x, else, y]"): body of the if ends at the else */
                b->k->i++; if (isop(tk(b->k), ",")) b->k->i++;
                it->els = bp_items(b, NULL); it->has_else = 1;
            }
            *tail = it; tail = &it->next;
            lastif = w ? NULL : it;
            if (!w && inner) lastif = it;   /* else binds to this if (outer) */
            break;   /* consumed the rest of the line */
        }
        if (isname(t, "else")) {
            warn(b->p, t->line, "'else' not at the start of a postline");
            b->k->i++; continue;
        }
        it = bp_element(b);
        if (it) { *tail = it; tail = &it->next; }
        Tok *n = tk(b->k);
        if (!(isop(n, ",") || n->t == T_EOL || n->t == T_END || isop(n, "]") || isop(n, "["))) {
            /* element followed directly by another one (e.g. "ptime sclose_prn") - mcpost reads the next label too */
        }
    }
    if (last_if_out) *last_if_out = lastif;
    return head;
}

/* sequence of postlines (a postblock body or a [ ] group): 'else' at the start of a postline attaches to the if that
   started / ended the previous postline (CPostPostTable::UpdateAddElseComma / SetBooleanLinkage) */
static MpItem *bp_seq(BP *b, int in_group)
{
    MpItem *head = NULL, **tail = &head; MpItem *pending_if = NULL;
    for (;;) {
        Tok *t = tk(b->k);
        if (t->t == T_END) break;
        if (t->t == T_EOL) { b->k->i++; continue; }
        if (isop(t, "]")) { if (in_group) break; warn(b->p, t->line, "unmatched ']'"); b->k->i++; continue; }
        if (isname(t, "else") && (isop(tk1(b->k), ",") || tk1(b->k)->t == T_EOL || tk1(b->k)->t == T_END || isop(tk1(b->k), "["))) {
            int line = t->line;
            b->k->i++;
            if (isop(tk(b->k), ",")) b->k->i++;
            /* else body: the rest of this postline; if it is empty the next postline / [ ] group */
            while (tk(b->k)->t == T_EOL) b->k->i++;
            MpItem *inner = NULL;
            MpItem *body = bp_items(b, &inner);
            if (!pending_if) { warn(b->p, line, "'else' without 'if'"); }
            else { pending_if->els = body; pending_if->has_else = 1; }
            pending_if = inner;            /* else, if ... chains */
            if (!pending_if) {
                /* an else whose body is a single if (possibly via a group) keeps the chain */
                MpItem *x = body; while (x && x->next) x = x->next;
                if (x && x->code == EL_IF && !x->has_else) pending_if = x;
            }
            continue;
        }
        MpItem *lastif = NULL;
        MpItem *items = bp_items(b, &lastif);
        if (items) { *tail = items; while (*tail) tail = &(*tail)->next; }
        pending_if = lastif;
        if (!lastif) {
            /* a postline ending with a group whose last element is an if: else still pairs with the line's if only */
        }
    }
    return head;
}

/* ------------------------------------------------------------------ declarations */
static int parse_long(const char **s, long *out)
{
    while (**s == ' ' || **s == '\t') (*s)++;
    char *e; double d = strtod(*s, &e);
    if (e == *s) return 0;
    *out = (long)d; *s = e; return 1;
}
static void skipws(const char **s) { while (**s == ' ' || **s == '\t') (*s)++; }
static int read_label(const char **s, char *out, size_t n)
{
    skipws(s); size_t k = 0;
    while (**s && is_lab((unsigned char)**s)) { if (k + 1 < n) out[k++] = **s; (*s)++; }
    out[k] = 0; return k > 0;
}
/* quoted string or string-variable label (CPostFmt::PrefixSuffixString @18007f860). returns 1 quoted (q set), 2 label, 0 none */
static int read_strarg(const char **s, char *out, size_t n, char *q)
{
    skipws(s);
    if (**s == '"' || **s == '\'') {
        *q = **s; (*s)++; size_t k = 0;
        while (**s && **s != *q) { if (k + 1 < n) out[k++] = **s; (*s)++; }
        out[k] = 0; if (**s) (*s)++; return 1;
    }
    if (isalpha((unsigned char)**s) || **s == '_') { const char *b = *s; if (read_label(s, out, n)) return 2; *s = b; }
    return 0;
}

/* fmt [prefix] fs var [suffix] [incremental] [n/d/s]  (CPostFmt::ParseFmt @18007eda0) */
static void decl_fmt(MpPost *p, const char *s, int line)
{
    char buf[256], q = 0;
    MpFmt f; memset(&f, 0, sizeof f); f.prefix_str = f.suffix_str = -1; f.fs = MP_NOFS;
    const char *save = s;
    skipws(&s);
    if (!isdigit((unsigned char)*s) && *s != '-') {
        int k = read_strarg(&s, buf, sizeof buf, &q);
        if (k == 1) { f.prefix = xstrdup(buf); f.flags |= q == '\'' ? 0x20 : 0x40; }
        else if (k == 2) {
            int si = mp_find_str_p(p, buf);
            if (si < 0) { warn(p, line, "fmt: unknown prefix string '%s'", buf); f.prefix = xstrdup(buf); }
            else { f.prefix_str = si; f.flags |= 0x4; }
        }
    }
    long fsn;
    if (!parse_long(&s, &fsn)) { warn(p, line, "fmt: missing format number: %s", save); return; }
    f.fs = (int)fsn;
    char vn[256];
    if (!read_label(&s, vn, sizeof vn)) { warn(p, line, "fmt: missing variable"); return; }
    const char *rest; int prv = strip_prv(vn, &rest);
    if (prv) warn(p, line, "fmt on prv_ variable %s", vn);
    skipws(&s);
    if (*s == '"' || *s == '\'' || isalpha((unsigned char)*s)) {
        const char *b = s; int k = read_strarg(&s, buf, sizeof buf, &q);
        if (k == 1) { f.suffix = xstrdup(buf); f.flags |= q == '\'' ? 0x80 : 0x100; }
        else if (k == 2) {
            int si = mp_find_str_p(p, buf);
            if (si >= 0) { f.suffix_str = si; f.flags |= 0x10; }
            else s = b;   /* not a string: option letters */
        }
    }
    long inc;
    const char *b2 = s;
    if (parse_long(&s, &inc)) { if (inc) f.flags |= 0x1; } else s = b2;
    skipws(&s);
    /* option letters: n/N 0x200, d/D 0x400, s/S (first) 0x800 */
    if (*s == 's' || *s == 'S') { f.flags |= 0x800; s++; }
    for (; *s; s++) { if (*s == 'n' || *s == 'N') f.flags |= 0x200; if (*s == 'd' || *s == 'D') f.flags |= 0x400; }
    if (!f.prefix) f.prefix = xstrdup("");
    if (!f.suffix) f.suffix = xstrdup("");
    int vi = mp_find_var_p(p, rest);
    if (vi < 0) {
        if ((rest[0] == 's' || rest[0] == 'S') && mp_find_str_p(p, rest) >= 0) {
            /* fmt on a string variable: NOT DECODED (string formats) - ignored */
            warn(p, line, "fmt on string %s ignored", rest); return;
        }
        vi = add_var(p, rest);
    }
    if (p->nfmts == p->cfmts) { p->cfmts = p->cfmts ? p->cfmts * 2 : 256; p->fmts = xrealloc(p->fmts, p->cfmts * sizeof(MpFmt)); }
    p->fmts[p->nfmts] = f;
    p->vars[vi].fmt = p->nfmts++;
    p->vars[vi].fs = f.fs;              /* FmtIncremental / ParseFmtAddUpdateAssignment: the variable's fs = fmt's fs */
    p->vars[vi].inpost = 1;
}

/* fstrsel first selector target count [under [x [y]]] (CPostFstrsel::ParseFstrsel @180099700) */
static void decl_fstrsel(MpPost *p, const char *s, int line)
{
    char a[256], v[256], t[256];
    if (!read_label(&s, a, sizeof a) || !read_label(&s, v, sizeof v) || !read_label(&s, t, sizeof t)) { warn(p, line, "fstrsel: syntax"); return; }
    MpFstrsel f; memset(&f, 0, sizeof f);
    f.base = mp_find_str_p(p, a); f.target = mp_find_str_p(p, t);
    const char *vr; int prv = strip_prv(v, &vr);
    f.selvar = mp_find_var_p(p, vr);
    /* selector not declared before (only assigned in postblocks, e.g. plane_2 in the 5-axis posts): ParseFstrsel looks it
       up with FindVariableLabel(.., 2, ..) which accepts it; ours: create it here */
    if (f.selvar < 0 && f.base >= 0 && f.target >= 0 && mp_find_str_p(p, vr) < 0) f.selvar = add_var(p, vr);
    if (prv) f.flags |= 1;   /* ParseFstrsel: ReadLabelModifier 'd' on the selector -> flag 1 */
    if (f.base < 0 || f.target < 0 || f.selvar < 0) { warn(p, line, "fstrsel: unknown label (%s %s %s)", a, v, t); return; }
    long n = -1, u = -1, x;
    if (parse_long(&s, &n)) {
        if ((n < -1) || n == 0) warn(p, line, "fstrsel: bad count %ld", n);
        if (parse_long(&s, &u)) {
            if (u < -1) u = -1;
            if (parse_long(&s, &x)) { f.extra[0] = (int)x; if (x % 10 == 1) f.flags |= 4; if (x / 10 == 1) f.flags |= 8;
                if (parse_long(&s, &x)) { f.extra[1] = (int)x; if (parse_long(&s, &x)) f.extra[2] = (int)x; } }
        }
    }
    /* count -1 = to the last string of the list (FindStringPositionRange) */
    if (n == -1) n = p->nstrs - f.base;
    f.count = (int)n; f.under = (int)u;
    p->fstrsels = xrealloc(p->fstrsels, (p->nfstrsels + 1) * sizeof *p->fstrsels);
    p->fstrsels[p->nfstrsels] = f;
    p->strs[f.target].sel = p->nfstrsels++;
}

/* ------------------------------------------------------------------ the two passes */
static const char *DECLKW[] = {"fs", "fs2", "fmt", "fstrsel", "flktbl", "fq", "fbuf", "fprmtbl", "fstack", NULL};
static int decl_kw(const char *s, int *len)
{
    for (int i = 0; DECLKW[i]; i++) {
        size_t n = strlen(DECLKW[i]);
        if (!strncmp(s, DECLKW[i], n) && (s[n] == ' ' || s[n] == '\t' || s[n] == 0)) { *len = (int)n; return i; }
    }
    return -1;
}

/* logical text of a postblock body: lines ending with ',' continue on the next line (ours: continuation rule as observed
   in all stock posts; CPostFindVars::PostLineRecord marks first/continued/last lines with flags 1/4/8/0x10) */
/* a postline continues on the next line when it ends with ',' or an operator, or the next line starts with an
   operator (ours: rule derived from the stock posts, e.g. "if (a) |" / "  & b, ..." / "x = a +" ; mcpost joins these in
   CPostFindVars::ConvertCommentContinue / PostLineRecord line flags 1/4/8/0x10 - NOT DECODED in detail) */
static int line_continues(Src *src, int i, int to)
{
    const char *s = src->l[i].s; size_t n = strlen(s);
    int opend = n && strchr("+-*/^&|=<>:(", s[n - 1]);
    if (opend) return 1;
    for (int j = i + 1; j < to; j++) {
        const char *t = src->l[j].s; while (*t == ' ' || *t == '\t') t++;
        if (!*t) continue;
        if (!strncasecmp(t, "else", 4) && !is_lab((unsigned char)t[4])) return 0;   /* "if a," followed by "else," = empty body */
        if (n && s[n - 1] == ',') return 1;
        return strchr("&|+/^=<>)", *t) != NULL;   /* not '*' (force modifier) nor '-' */
    }
    return 0;
}
static void body_tokens(MpPost *p, Src *src, int from, int to, const char *head_rest, Toks *k)
{
    memset(k, 0, sizeof *k); k->p = p;
    if (head_rest && *head_rest) { tokenize(k, head_rest, src->l[from - 1].no); tok_push(k, T_EOL, NULL, 0, 0, 0, 1, src->l[from - 1].no); }
    for (int i = from; i < to; i++) {
        char *s = src->l[i].s;
        if (!*s) continue;
        tokenize(k, s, src->l[i].no);
        if (line_continues(src, i, to)) continue;
        tok_push(k, T_EOL, NULL, 0, 0, 0, 1, src->l[i].no);
    }
}

static int is_body_line(const char *raw) { return raw[0] == ' ' || raw[0] == '\t' || raw[0] == 0; }

MpPost *mp_post_load(const char *pst_path, char *err, size_t errn)
{
    Src src;
    if (!read_src(pst_path, &src)) { snprintf(err, errn, "cannot open %s", pst_path); return NULL; }
    PostX *px = xrealloc(NULL, sizeof *px); memset(px, 0, sizeof *px);
    MpPost *p = &px->post;
    p->path = xstrdup(pst_path);
    predefine(p);
    /* strip comments in place; remember raw first char */
    char *first = xrealloc(NULL, src.n + 1);
    for (int i = 0; i < src.n; i++) { first[i] = src.l[i].s[0]; strip_comment(src.l[i].s); }

    /* ---- pass 1 (CPostFindVars::Process): column-0 lines declare symbols in file order ---- */
    enum { K_NONE, K_BLOCK, K_TABLE, K_TEXT };
    int mode = K_NONE, ctrl_text = 0;
    for (int i = 0; i < src.n; i++) {
        char *s = src.l[i].s; int no = src.l[i].no;
        if (ctrl_text) {                     /* [CTRL_...] post text until [CTRL_TEXT_END] (CPostFindVars state 6) */
            if (!strncmp(s, "[CTRL_TEXT_END]", 15)) ctrl_text = 0;
            continue;
        }
        if (!*s) continue;
        if (is_body_line(s)) {
            /* formula targets inside postblocks are known before the post table is built (5-axis posts use variables
               above the postblock that assigns them); ours: declare them here in file order (creation point NOT DECODED) */
            if (mode != K_BLOCK) continue;
            Toks k; memset(&k, 0, sizeof k); k.p = p; tokenize(&k, s, no);
            for (k.i = 0; k.i < k.n; k.i++) {
                Tok *t = &k.t[k.i];
                int start = k.i == 0 || isop(&k.t[k.i - 1], ",") || isop(&k.t[k.i - 1], "[");
                if (!start || t->t != T_NAME || !isop(tk1(&k), "=")) continue;
                if (isname(t, "if") || isname(t, "while") || isname(t, "else")) continue;
                const char *rest; strip_prv(t->s, &rest);
                int dummy; if (resolve(p, rest, &dummy, 0) != LK_NONE) continue;
                Tok *v = k.i + 2 < k.n ? &k.t[k.i + 2] : NULL;
                if ((rest[0] == 's' || rest[0] == 'S') && v && (v->t == T_STR || (v->t == T_NAME && mp_find_str_p(p, v->s) >= 0))) add_str(p, rest);
                else add_var(p, rest);
            }
            toks_free(&k);
            continue;
        }
        if (s[0] == '[') {
            if (!strncmp(s, "[CTRL_", 6)) ctrl_text = 1;
            else if (!strncmp(s, "[POST_VERSION]", 14)) p->version_line = xstrdup(s);
            mode = K_NONE; continue;
        }
        if (isdigit((unsigned char)s[0])) continue;      /* numbered question (pass 2) */
        int kl, kw = decl_kw(s, &kl);
        if (kw >= 0) {
            if (kw == 2) decl_fmt(p, s + kl, no);        /* fmt declares its variable here (CheckAndAddDecVariable) */
            mode = K_NONE; continue;
        }
        char name[256]; const char *c = s;
        if (!read_label(&c, name, sizeof name)) { warn(p, no, "unreadable line: %s", s); continue; }
        skipws(&c);
        const char *rest; int prv = strip_prv(name, &rest);
        if (*c == ':' || (*c == '=' )) {
            /* ParseDeclaration @18014bed0: ':' (0xf) init, ':=' read-only init, '=' (0x11) global formula.
               s-names are strings when the value is quoted / a string label, else numeric. */
            const char *v = c + 1; if (*c == ':' && *v == '=') v++;
            skipws(&v);
            int isstr = 0;
            if (rest[0] == 's' || rest[0] == 'S') {
                if (*v == '"' || *v == '\'') isstr = 1;
                else { char l2[256]; const char *v2 = v; if (read_label(&v2, l2, sizeof l2) && mp_find_str_p(p, l2) >= 0 && mp_op_find(l2) < 0) isstr = 1; }
                if (mp_find_str_p(p, rest) >= 0) isstr = 1;
            }
            if (isstr) { if (mp_find_str_p(p, rest) < 0) add_str(p, rest); }
            else if (mp_find_var_p(p, rest) < 0) {
                if (mp_find_str_p(p, rest) >= 0) { /* string declared earlier */ }
                else add_var(p, rest);
            }
            (void)prv; mode = K_NONE;
            continue;
        }
        /* postblock header (FirstCharMap 34/35: p/l/m) - CheckAndAddDecPostBlock */
        int b = mp_find_block_p(p, name);
        if (b < 0) {
            if (!strchr("plmPLM", name[0])) warn(p, no, "postblock name '%s' does not start with p/l/m", name);
            b = add_block(p, name);
        }
        mode = K_BLOCK;
    }

    /* ---- pass 2 (CPostBuildTable::Process): declarations, initialisations, formulas, postblock bodies ---- */
    ctrl_text = 0; mode = K_NONE;
    char ctrl_section[128] = "";
    int cur_tbl = -1, cur_kind = -1, tbl_rows = 0;
    for (int i = 0; i < src.n; i++) {
        char *s = src.l[i].s; int no = src.l[i].no;
        if (ctrl_text) {
            if (!strncmp(s, "[CTRL_TEXT_END]", 15)) { ctrl_text = 0; continue; }
            if (s[0] == '[') { char *e = strchr(s, ']'); snprintf(ctrl_section, sizeof ctrl_section, "%.*s", e ? (int)(e - s - 1) : 0, s + 1); continue; }
            if (isdigit((unsigned char)s[0])) {          /* N. "text"//default (CPostText / CPostStringUtil::ParseTextLine) */
                p->ptext = xrealloc(p->ptext, (p->nptext + 1) * sizeof *p->ptext);
                p->ptext[p->nptext].section = xstrdup(ctrl_section); p->ptext[p->nptext].n = atoi(s);
                const char *q = strchr(s, '"'); const char *q2 = q ? strchr(q + 1, '"') : NULL;
                p->ptext[p->nptext].text = q && q2 ? xstrndup(q + 1, q2 - q - 1) : xstrdup("");
                const char *d = q2 ? strstr(q2, "//") : strstr(s, "//");
                p->ptext[p->nptext].def = d ? xstrdup(d + 2) : NULL;
                p->nptext++;
            }
            continue;
        }
        if (!*s) continue;
        if (is_body_line(s)) {
            if (mode == K_TABLE && cur_tbl >= 0) {
                /* table rows (flktbl / fprmtbl): CPostFlkTbl::ParseFlktblItem, CPostFprmTbl::ParseFprmtblItem */
                const char *c = s;
                if (cur_kind == 4) {
                    MpLktbl *t = &p->lktbls[cur_tbl];
                    if (t->n >= tbl_rows) { mode = K_NONE; continue; }
                    char *e; double a = strtod(c, &e); if (e == c) { warn(p, no, "flktbl row"); continue; }
                    c = e; skipws(&c);
                    t->a = xrealloc(t->a, (t->n + 1) * sizeof(double)); t->b = xrealloc(t->b, (t->n + 1) * sizeof(double));
                    t->bstr = xrealloc(t->bstr, (t->n + 1) * sizeof(int));
                    t->a[t->n] = a; t->bstr[t->n] = -1;
                    if (isalpha((unsigned char)*c)) { char l[256]; read_label(&c, l, sizeof l);
                        int si = mp_find_str_p(p, l); int vi = mp_find_var_p(p, l);
                        if (si >= 0) { t->bstr[t->n] = si; t->b[t->n] = 0; }
                        else if (vi >= 0) t->b[t->n] = p->vars[vi].v[0];
                        else warn(p, no, "flktbl: unknown %s", l); }
                    else t->b[t->n] = strtod(c, NULL);
                    t->n++;
                } else if (cur_kind == 7) {
                    MpPrmtbl *t = &p->prmtbls[cur_tbl];
                    if (t->n >= tbl_rows) { mode = K_NONE; continue; }
                    long code; if (!parse_long(&c, &code)) { warn(p, no, "fprmtbl row"); continue; }
                    char l[256]; if (!read_label(&c, l, sizeof l)) { warn(p, no, "fprmtbl row label"); continue; }
                    const char *r2; strip_prv(l, &r2);
                    int si = (l[0] == 's' || l[0] == 'S') ? mp_find_str_p(p, l) : -1;
                    int vi = si < 0 ? mp_find_var_p(p, l) : -1;
                    if (si < 0 && vi < 0) { warn(p, no, "fprmtbl: unknown %s", l); continue; }
                    t->code = xrealloc(t->code, (t->n + 1) * sizeof(int)); t->var = xrealloc(t->var, (t->n + 1) * sizeof(int));
                    t->isstr = xrealloc(t->isstr, (t->n + 1) * sizeof(int));
                    t->code[t->n] = (int)code; t->var[t->n] = si >= 0 ? si : vi; t->isstr[t->n] = si >= 0; t->n++;
                }
            }
            continue;
        }
        if (s[0] == '[') {
            if (!strncmp(s, "[CTRL_", 6)) { ctrl_text = 1; char *e = strchr(s, ']'); snprintf(ctrl_section, sizeof ctrl_section, "%.*s", e ? (int)(e - s - 1) : 0, s + 1); }
            mode = K_NONE; continue;
        }
        if (isdigit((unsigned char)s[0])) {
            /* NOT DECODED: numbered questions outside post text (CPostNoQuestion::ParseNumberedQuestion) - ignored */
            warn(p, no, "numbered question ignored: %.40s", s); mode = K_NONE; continue;
        }
        mode = K_NONE;
        int kl, kw = decl_kw(s, &kl);
        const char *c = s + kl;
        if (kw == 0 || kw == 1) {      /* fs / fs2: number formatting is mp_fmt.c (CPostFs::ParseFs2 @180098130) */
            int num; MpFs fs; char e2[200];
            if (!mp_fs_parse(s, &num, &fs, e2, sizeof e2)) { warn(p, no, "%s", e2); continue; }
            if (num < MP_FSMIN || num >= MP_FSMAX) { warn(p, no, "format number %d out of range", num); continue; }
            p->fs[num - MP_FSMIN] = fs;
            continue;
        }
        if (kw == 2) continue;                          /* fmt: done in pass 1 */
        if (kw == 3) { decl_fstrsel(p, c, no); continue; }
        if (kw == 4 || kw == 7) {                       /* flktbl N rows / fprmtbl N rows */
            long n, rows;
            if (!parse_long(&c, &n) || !parse_long(&c, &rows)) { warn(p, no, "%s: syntax", DECLKW[kw]); continue; }
            if (kw == 4) { p->lktbls = xrealloc(p->lktbls, (p->nlktbls + 1) * sizeof *p->lktbls); memset(&p->lktbls[p->nlktbls], 0, sizeof *p->lktbls);
                           p->lktbls[p->nlktbls].num = (int)n; cur_tbl = p->nlktbls++; }
            else { p->prmtbls = xrealloc(p->prmtbls, (p->nprmtbls + 1) * sizeof *p->prmtbls); memset(&p->prmtbls[p->nprmtbls], 0, sizeof *p->prmtbls);
                   p->prmtbls[p->nprmtbls].num = (int)n; cur_tbl = p->nprmtbls++; }
            cur_kind = kw; tbl_rows = (int)rows; mode = K_TABLE;
            continue;
        }
        if (kw == 6) {                                  /* fbuf N a b c d (CPostFbuf::ParseFbuf) */
            long n; MpFbufDef f; memset(&f, 0, sizeof f);
            if (!parse_long(&c, &n)) { warn(p, no, "fbuf: syntax"); continue; }
            f.num = (int)n; for (int k2 = 0; k2 < 4; k2++) { long v; if (parse_long(&c, &v)) f.p[k2] = (double)v; }
            p->fbufs = xrealloc(p->fbufs, (p->nfbufs + 1) * sizeof f); p->fbufs[p->nfbufs++] = f;
            continue;
        }
        if (kw == 5) {                                  /* fq N var "prompt" (CPostFq::ParseFq) */
            long n; char l[256], pr[512], q;
            MpFq f; memset(&f, 0, sizeof f);
            if (!parse_long(&c, &n) || !read_label(&c, l, sizeof l)) { warn(p, no, "fq: syntax"); continue; }
            f.num = (int)n; const char *r2; strip_prv(l, &r2);
            f.var = mp_find_var_p(p, r2); if (f.var < 0) { f.var = mp_find_str_p(p, l); f.isstr = 1; }
            f.prompt = xstrdup(read_strarg(&c, pr, sizeof pr, &q) == 1 ? pr : "");
            p->fqs = xrealloc(p->fqs, (p->nfqs + 1) * sizeof f); p->fqs[p->nfqs++] = f;
            continue;
        }
        if (kw == 8) {                                  /* fstack N size (CPostFstack::ParseFstackAddStack @1801d88f0) */
            long n, sz;
            if (!parse_long(&c, &n) || !parse_long(&c, &sz) || sz < 1) { warn(p, no, "fstack: syntax"); continue; }
            int dup = 0;   /* FindStack hit: message 8, the entry is added anyway (first one is found later) */
            for (int i = 0; i < p->nstacks; i++) if (p->stacks[i].num == n) dup = 1;
            if (dup) warn(p, no, "fstack %ld defined twice", n);
            p->stacks = xrealloc(p->stacks, (p->nstacks + 1) * sizeof *p->stacks);
            p->stacks[p->nstacks].num = n; p->stacks[p->nstacks].size = (int)(short)sz; p->nstacks++;
            continue;
        }
        /* label line */
        char name[256]; c = s;
        if (!read_label(&c, name, sizeof name)) continue;
        skipws(&c);
        const char *rest; int prv = strip_prv(name, &rest);
        if (*c == ':' || *c == '=') {
            int op = OPI_ASSIGN; const char *v = c + 1;
            if (*c == ':') { op = OPI_COLON; if (*v == '=') { op = OPI_ASSIGN2; v++; } }
            Toks k; memset(&k, 0, sizeof k); k.p = p; tokenize(&k, v, no);
            EP e = {&k, p, 0, no};
            int si = mp_find_str_p(p, rest), vi = mp_find_var_p(p, rest);
            int isstr = si >= 0 && (vi < 0 || rest[0] == 's' || rest[0] == 'S');
            if (isstr && vi >= 0) {
                /* both exist: numeric when the value is numeric */
                Tok *t0 = tk(&k);
                if (t0->t == T_NUM || (t0->t == T_OP && !strcmp(t0->s, "-"))) isstr = 0;
            }
            MpExpr *x = ep_formula(&e, isstr ? LK_STR : LK_VAR, isstr ? si : vi, prv, op);
            if (tk(&k)->t != T_END && !e.err) warn(p, no, "trailing text after declaration: %s", s);
            if (op == OPI_ASSIGN) {
                /* global formula: re-solved whenever the variable is read (GetValue / IsGlobalFormula) */
                if (isstr) p->strs[si].gformula = x;
                else { p->vars[vi].gformula = x; p->vars[vi].f78 |= prv ? 0x80 : 0x40; }
                p->gforms = xrealloc(p->gforms, (p->ngforms + 1) * sizeof(MpExpr *)); p->gforms[p->ngforms++] = x;
            } else {
                p->inits = xrealloc(p->inits, (p->ninits + 1) * sizeof(MpExpr *)); p->inits[p->ninits++] = x;
                if (isstr) { p->strs[si].init = x; p->strs[si].inpost = 1; }
                else {
                    p->vars[vi].init = x; p->vars[vi].inpost = 1;
                    p->vars[vi].f78 |= prv ? 0x20 : 0x10;
                    if (op == OPI_ASSIGN2) p->vars[vi].f68 |= prv ? 0x8 : 0x4;   /* ':=' read only (ParseDeclaration) */
                }
            }
            toks_free(&k);
            continue;
        }
        /* postblock: body = following indented / blank lines */
        int b = mp_find_block_p(p, name);
        if (b < 0) continue;
        int j = i + 1;
        /* body = following lines whose first character is white space, '#' (stripped to empty) or empty
           (CPostFindVars::Process: FirstCharMap types 4/0x1d white, 6 '#', 1 empty continue the postblock) */
        while (j < src.n && is_body_line(src.l[j].s)) j++;
        Toks k; body_tokens(p, &src, i + 1, j, c, &k);
        BP bp = {&k, p, b};
        MpItem *body = bp_seq(&bp, 0);
        if (p->blocks[b].defined) {
            /* a second body for the same postblock: appended (NOT DECODED: mcpost reports 'already defined'?) */
            warn(p, no, "postblock %s defined twice", name);
            MpItem **t = &p->blocks[b].body; while (*t) t = &(*t)->next; *t = body;
        } else p->blocks[b].body = body;
        p->blocks[b].defined = 1; p->blocks[b].line = no;
        toks_free(&k);
        i = j - 1;
    }
    for (int i = 0; i < src.n; i++) free(src.l[i].s);
    free(src.l); free(first);
    return p;
}

void mp_post_free(MpPost *p)
{
    if (!p) return;
    /* ours: the post lives for the process in the CLI; free the big arrays only */
    for (int i = 0; i < p->nwarn; i++) free(p->warn[i]);
    free(p->warn);
    free(p->vars); free(p->strs); free(p->blocks); free(p->fmts);
    free(LK(p)->v.slot); free(LK(p)->s.slot); free(LK(p)->b.slot);
    free(p->path);
    free(p);
}
int mp_post_nwarnings(const MpPost *p) { return p->nwarn; }
const char *mp_post_warning(const MpPost *p, int i) { return i >= 0 && i < p->nwarn ? p->warn[i] : NULL; }
