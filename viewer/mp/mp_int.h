/* mp_int.h - private structures of the MP interpreter (mp_parse*.c / mp_run*.c / mp_eval*.c / mp_out*.c / mp_nciloop*.c).
 * Port of mcpost.dll (Mastercam 2017). Layout is ours; every field notes the mcpost structure offset it models.
 * See ../../formats/mp_engine.md "## Interpreter". */
#ifndef MP_INT_H
#define MP_INT_H
#include "mp.h"
#include "mp_fmt.h"

#define MP_MAXLINE 4096

/* ---------------- element codes (strt_post_table +0x28 / +0x18), CPostPostTable::ProcessPostBlock @1800efeb0 --------- */
enum {
    EL_VAR        = 900,   /* plain numeric variable                                   */
    EL_VAR_FORCE  = 0x385, /* '*'  (modifier type 50)                                   */
    EL_VAR_BTICK  = 0x386, /* '`'  (51)                                                  */
    EL_VAR_DEBUG  = 0x387, /* '~'  (52)                                                  */
    EL_VAR_DBGPRV = 0x388, /* '~prv_' (ReadLabelModifier 0x65)                           */
    EL_VAR_UPD    = 0x389, /* '!'  (53) update only                                      */
    EL_VAR_RND    = 0x38a, /* '@'  (54) round only                                       */
    EL_VAR_PRV    = 0x38b, /* 'prv_' (100)                                               */
    EL_STR        = 0x38c, /* string variable                                            */
    EL_SQUOTE     = 0x38d, /* 'text' literal (does not count as written)                 */
    EL_DQUOTE     = 0x38e, /* "text" literal                                             */
    EL_ASCII      = 0x38f, /* bare number in an output list = ASCII character           */
    EL_STR_FORCE  = 0x390, /* '*string'                                                  */
    EL_STR_BTICK  = 0x391, /* '`string'                                                  */
    EL_STR_DEBUG  = 0x392, /* '~string'                                                  */
    EL_STR_UPD    = 0x393, /* '!string' (update the fstrsel selection)                   */
    EL_QUESTION   = 0x3ea, /* qN prompt question (CPostOutput::PromptQuestion)           */
    EL_CALL       = 0x3eb, /* postblock call                                             */
    EL_CALLARGS   = 0x3ec, /* postblock call with arguments                              */
    EL_FORMULA    = 0x7d3, /* formula in a postblock                                     */
    EL_IF         = 0x7d5, /* boolean if                                                 */
    EL_WHILE      = 0x7d6, /* ours: boolean while (0x7d5 with the while flag in mcpost)  */
    EL_GROUP      = 0x7e0  /* ours: [ ... ] group                                        */
};

/* ---------------- expressions ---------------- */
enum { EX_NUM = 1, EX_VAR, EX_STRVAR, EX_STRLIT, EX_UNOP, EX_BINOP, EX_FUNC, EX_ASSIGN };
typedef struct MpExpr {
    int kind;
    int op;            /* operator / function index in the operator table (mp_operators.tsv row, 0-based) */
    int idx;           /* variable / string index */
    int prv;           /* EX_VAR: 1 = prv_ value */
    double num;
    char *str;         /* EX_STRLIT */
    struct MpExpr *a, *b;        /* operands (binop / unop / assign: a = target, b = value) */
    struct MpExpr **args; int nargs;   /* EX_FUNC */
} MpExpr;

/* values: types follow the operator table result codes (formats/mp.md): 1 real, 2 vector, 4 string, 8 matrix */
enum { MPV_NUM = 1, MPV_VEC = 2, MPV_STR = 4, MPV_MAT = 8 };
typedef struct { int t; double v[9]; char *s; } MpVal;   /* s malloc'ed when t == MPV_STR */

/* an evaluated function argument: value plus, when the argument was a bare variable, its identity
   (functions like force(), fmtrnd(), vequ(), rbuf() act on the variable itself) */
typedef struct { MpVal val; int var; int prv; int str; } MpArg;   /* var / str = index or -1 */

/* ---------------- post table (AST of a postblock body) ---------------- */
typedef struct MpItem {
    int code;            /* EL_* */
    int idx;             /* variable / string / postblock / question index */
    char *text;          /* literal text (EL_SQUOTE / EL_DQUOTE / EL_ASCII) */
    int textlen;         /* EL_ASCII may hold a NUL */
    MpExpr *expr;        /* EL_FORMULA / EL_IF / EL_WHILE condition */
    struct MpItem *body; /* EL_IF / EL_WHILE / EL_GROUP */
    struct MpItem *els;  /* EL_IF else branch */
    int has_else;
    struct MpItem *next;
    int line;            /* .pst line number (1-based) */
} MpItem;

/* ---------------- symbol tables ---------------- */
typedef struct {
    char *name;
    double v[4];          /* strt_variable +0x10 current, +0x18 previous, +0x20, +0x28 (CPostVariable::SetDefaultValue @1801b9910) */
    unsigned f68;         /* +0x68 flags: 0x1 xyz legacy, 0x4 read only, 0x8 prv read only, 0x10 command variable,
                             0x20 linked, 0x40 incremental companion, 0x80, 0x800 set by met_tool, 0x1000, 0x2000 CD variable */
    unsigned f78;         /* +0x78 */
    int fs;               /* +0x38 format statement number (MP_NOFS = none) */
    int fmt;              /* +0x40 index into fmts, -1 none */
    int oneshot;          /* +0x70 bit 0x1000000000: one-shot force (force() / BlockCall tlngno$) */
    MpExpr *gformula;     /* +0x48 global formula (top level 'name = expr'), re-solved on every read (IsGlobalFormula) */
    MpExpr *init;         /* declaration 'name : value' (solved by InitializeVariablesAndStrings @1800a8fe0) */
    int predef;           /* 1 = predefined (mp_variables.tsv), registration order = index */
    int inpost;           /* post declares / initialises it (CPostComponents::VariableInPost) */
    int incvar;           /* +0x58 incremental companion variable or -1 */
} MpVar;

typedef struct {
    char *name;
    char *val;            /* +0x18 */
    int sel;              /* +0x20 fstrsel (index into fstrsels) when this string is an fstrsel target, else -1 */
    MpExpr *gformula;     /* +0x38 string formula (0x7d2) */
    MpExpr *init;
    int predef;
    int inpost;
    unsigned flags;       /* +0x48 */
    double num[2];        /* +0x28 / +0x30 numeric values of the string (getstrval / setstrval, extend slot 0 / 1) */
} MpStrVar;

typedef struct {
    char *prefix, *suffix;     /* literal prefix / suffix */
    int prefix_str, suffix_str;/* string variable used as prefix / suffix (-1 none) */
    int fs;                    /* format statement number, MP_NOFS = none (use the variable's / default) */
    unsigned flags;            /* CPostFmt::ParseFmt @18007eda0: 0x1 incremental, 0x200 'n', 0x400 'd', 0x800 's',
                                  prefix 0x20 single / 0x40 double quote / 0x4 string var, suffix 0x80 / 0x100 / 0x10 */
} MpFmt;

typedef struct {
    int base;      /* first selectable string (index into strs) */
    int selvar;    /* selector numeric variable */
    int target;    /* target string */
    int count;     /* +0x28 number of strings (-1 = to the end) */
    int under;     /* +0x2a underflow control */
    int extra[3];  /* optional params (+0x2e, +0x30, flags) */
    unsigned flags;/* +0x34 */
} MpFstrsel;

typedef struct { int num; int n; double *a; double *b; int *bstr; } MpLktbl;   /* flktbl */
typedef struct { int num; int n; int *code; int *var; int *isstr; } MpPrmtbl;  /* fprmtbl: param code -> var/string */
typedef struct { int num; double p[4]; int str; } MpFbufDef;                     /* fbuf N a b c d */
typedef struct { int num; int var; int isstr; char *prompt; } MpFq;              /* fq N var "prompt" */
typedef struct { long num; int size; } MpFstackDef;  /* fstack N size (CPostFstack::ParseFstack @1801d8700 / AddStack
                                                        @1801d8530: list entry +0x18 number, +0x1c record size) */

typedef struct {
    char *name;
    MpItem *body;
    int predef;           /* index in mp_postblocks.tsv, -1 user */
    int defined;          /* post has a body for it */
    int line;
} MpBlock;

#define MP_NOFS   (-1000)
#define MP_FSMIN  (-32)
#define MP_FSMAX  1024

/* operator table row (mp_operators.tsv) */
typedef struct { const char *name; unsigned group; unsigned w1, w2; } MpOpDef;

struct MpPost {
    char *path;
    MpVar *vars; int nvars, cvars, npredef_vars;
    MpStrVar *strs; int nstrs, cstrs, npredef_strs;
    MpBlock *blocks; int nblocks, cblocks, npredef_blocks;
    MpFmt *fmts; int nfmts, cfmts;
    MpFs fs[MP_FSMAX - MP_FSMIN];          /* indexed by number - MP_FSMIN */
    MpFstrsel *fstrsels; int nfstrsels;
    MpLktbl *lktbls; int nlktbls;
    MpPrmtbl *prmtbls; int nprmtbls;
    MpFbufDef *fbufs; int nfbufs;
    MpFq *fqs; int nfqs;
    MpFstackDef *stacks; int nstacks;
    MpExpr **gforms; int ngforms;          /* top-level formulas in file order */
    MpExpr **inits; int ninits;            /* declaration formulas 'name : value' in file order (0x7d1 / 0x7d2) */
    char **warn; int nwarn;
    char *version_line;                    /* [POST_VERSION] line */
    /* post text ([CTRL_...] pages): misc integer / real defaults "N. "text"//default" */
    struct { char *section; int n; char *text; char *def; } *ptext; int nptext;
};

/* ---------------- run state ---------------- */
typedef struct { double *v; int n, cap; } MpDbuf;

struct MpRun {
    const MpPost *p;
    MpEnv env;
    /* per-run copies of the mutable symbol data (posts are immutable) */
    MpVar *vars; int nvars;
    MpStrVar *strs; int nstrs;
    MpFs fs[MP_FSMAX - MP_FSMIN];
    MpFmt *fmts; int nfmts;
    MpFstrsel *fstrsels; int nfstrsels;    /* per-run copy: setselund/setselsiz/setselrtn/setselsel/setseloffs change it */
    /* fstack run lists (CPostRunStack: the stack entry +0x10 = CList of CArray<double> records, head first) */
    struct { double **rec; int n, cap; } *stk;
    FILE *nc;                 /* current NC stream */
    FILE *nc_main;
    /* CPostOutput state */
    char *line; int linelen, linecap;      /* this+8 line under construction */
    int writecount;           /* this+0xa8 */
    int seqpending;           /* this+0xaa: n$ output on the line */
    int nospc_once;           /* this+0x1ba (no_spc$) */
    int wrote;                /* this+0x1d2 postblock wrote */
    int *upd; int nupd, cupd; /* this+0xb0 deferred previous-value updates (UpdateOnLineOutput) */
    int calldepth;
    int status;               /* CPostRunComponents +0x388: 0 run, 4 ex$, 5 nci_rewind$, 6 exitpost$ */
    int stopped;
    char err[512];
    int nerrors;
    /* buffers (fbuf) */
    struct { int defined; double p[4]; MpDbuf num; char **s; int ns, cs; FILE *f; } buf[11];
    /* hooks into the NCI loop (mp_nciloop.c); NULL when no NCI loop is running */
    void *nci;                                       /* opaque NCI loop state */
    int (*cmd_hook)(MpRun *r, int var);              /* command variable not handled by the runtime (comment$, canceldc$,
                                                        tooltable$ ...); returns 1 handled */
    /* deferred eof/subprogram files (mergesub$ ...) */
    FILE *sub[5]; char *subpath[5];
    long long tick;           /* ours: loop guard */
    int tlchg_kind;           /* CPostRunComponents +0x382: 1 start of file, 2 tool change, other = null tool change */
    int tlchg_done;           /* +0x384: the delayed tool change postblock has been called */
    int std_fprm;             /* CPostComponents::SetStandardFprmMethod */
    char opt_trace;           /* ours: trace postblock calls to stderr */
    int axcombo;              /* axis combination of the first tool-table record (strt_tooltable +0x38), set by the
                                 NCI loop before OverrideInitializeFromMDandCD (mp_md_rotary) */
    char rc386;               /* CPostRunComponents +0x386: 1 while the wrtt pre-read runs (ReadOpInfoLoop calls
                                 pwrttparam$ instead of pparameter$) */
    unsigned short rc13e2;    /* +0x13e2 = control file_ctrl +4 (InitialAssignFromCD @1800b32b0; 0 without a control) */
    short out_nc_subs;        /* CPostRunComponents +0x13dc (SetOutputNcSubs @18011b250): > 0 = NC lines suppressed
                                 (transform / lathe-canned subprogram copies; OutputToNcOnEob @1800dc620) */
    double opinfo_last[3];    /* +0x13e8 / +0x13f0 / +0x13f8: op_id$ / xform_op_id$ / nesting_op_id$ last read by
                                 CallReadOpInfo (zeroed by the ctor and at each wrtt pass) */
};

/* ---- mp_parse.c ---- */
int  mp_find_var_p(const MpPost *p, const char *name);
int  mp_find_str_p(const MpPost *p, const char *name);
int  mp_find_block_p(const MpPost *p, const char *name);
const MpOpDef *mp_op_table(int *n);
int  mp_op_find(const char *name);           /* function / named operator index or -1 */
extern int MP_OP_NOT;

/* ---- mp_eval.c ---- */
int    mp_eval(MpRun *r, MpExpr *e, MpVal *out);            /* 1 ok */
double mp_eval_num(MpRun *r, MpExpr *e);
void   mp_val_free(MpVal *v);
double mp_get_var(MpRun *r, int idx, int prv);              /* GetValue incl. global formula */
void   mp_set_var(MpRun *r, int idx, int prv, double v);    /* SetValue incl. read-only / linked rules */
const char *mp_get_str(MpRun *r, int idx);                  /* GetString incl. string formula */
void   mp_set_strv(MpRun *r, int idx, const char *s);
const MpFs *mp_var_fs(MpRun *r, int idx);                   /* CPostFormulaSolver::GetFs: var fs or fs 1 */
/* function library (mp_eval_fn.c): solve op over evaluated args; target = assignment target (may be NULL).
   returns 1 ok, 0 error (r->err) */
int mp_fn_call(MpRun *r, int op, MpArg *a, int n, const MpArg *target, MpVal *out);

/* ---- mp_out.c ---- */
void mp_out_item(MpRun *r, MpItem *it);       /* output-list element (variables / strings / literals) */
int  mp_out_eob(MpRun *r, int forced);        /* CPostOutput::OutputToNcOnEob */
void mp_out_raw(MpRun *r, const char *s);     /* OutputToNcStream for a ready line (no eob handling) */
double mp_fmt_value(MpRun *r, int var, double v);       /* CPostMath::FormattedNumber with the variable's fs */
int  mp_fmt_var_text(MpRun *r, int var, double v, char *out, size_t n, int with_prefix);
const MpFs *mp_fs_get(MpRun *r, int num);

/* ---- mp_run.c ---- */
void mp_rt_error(MpRun *r, int line, const char *fmt, ...);   /* report a run error (ours) */
int  mp_exec(MpRun *r, MpItem *it);           /* execute an item list; 0 = stop */
int  mp_process_block(MpRun *r, int idx);     /* CPostRunPostTable::ProcessBlockCall (no BlockCall special cases) */
/* hooks for the NCI loop (mp_nciloop.c) */
void mp_run_set_tlchg_kind(MpRun *r, int kind); /* +0x382 = kind */
int  mp_run_tlchg_done(MpRun *r);              /* +0x384 */
void mp_run_set_tlchg_done(MpRun *r, int v);
void mp_run_set_std_fprm(MpRun *r, int on);
int  mp_run_override_md_cd(MpRun *r);          /* mp_run_prm.c: OverrideInitializeFromMDandCD with mp_prm (env.prm_ctx = McamFile*) */
int  mp_run_prm_cmd(MpRun *r, int var) __attribute__((weak));   /* mp_run_prm.c: rd_md$ / rd_cd$ / rd_tlpathgrp$ (1 handled) */
/* mp_run_prm.c: CPostRunMCamInfo::ReadOpInfo @1801243b0 (mode = param_3: 0 read loop, 1 wrtt, 2 rd_params$; returns 1
   read) and CPostReadNci::CallReadOpInfo @1800fca30 (after an NCI 1020; param2 = its int argument) */
int  mp_run_read_op_info(MpRun *r, int op_id, int mode) __attribute__((weak));
void mp_run_call_read_op_info(MpRun *r, int param2) __attribute__((weak));
int  mp_block_call_tlchg(MpRun *r);           /* BlockCall(-1): the tool change postblock selection */
int  mp_var_idx(MpRun *r, const char *name);  /* -1 */
void mp_run_init_values(MpRun *r);            /* InitializeVariablesAndStrings + InitializePreviousVariables */
void mp_buf_reset(MpRun *r);

#endif
