/* mp - MP post language engine (port of Mastercam 2017 mcpost.dll). Shared contract between the modules:
 *   mp_parse*.c  .pst reading + parsing into a post table          (interpreter agent)
 *   mp_run*.c    runtime: postblock execution, formulas, functions, output, modality, buffers, files (interpreter agent)
 *   mp_nci*.c    NCI read loop: ASCII NCI -> predefined variables -> predefined postblock dispatch (NCI-loop agent)
 *   mp_fmt.c     number formatting (mp_fmt.h)          nci_ascii.c  binary NCI -> ASCII NCI (nci_ascii.h)
 *   mp_prm.c     MD / CD / group / op parameters (mp_prm.h)     mp_cli.c  command line driver
 * Every behaviour cites its mcpost.dll function (+ address); see ../../formats/mp_engine.md.
 * Modules may ADD declarations here; do not change existing signatures without updating every caller. */
#ifndef MP_H
#define MP_H
#include <stdio.h>
#include <stddef.h>

typedef struct MpPost MpPost;   /* a parsed post (symbol tables + post table); immutable after mp_post_load */
typedef struct MpRun  MpRun;    /* one posting run: variable values, output state, open files */

/* ---- loading (CPostFindVars::Process @18007ab00 + CPostBuildTable::Process @180040990) ---- */
MpPost *mp_post_load(const char *pst_path, char *err, size_t errn);   /* NULL on fatal error (err) */
void    mp_post_free(MpPost *p);
int     mp_post_nwarnings(const MpPost *p);                           /* non-fatal parse messages */
const char *mp_post_warning(const MpPost *p, int i);

/* ---- run state ---- */
typedef struct {
    const char *nc_path;      /* output file name (spathnc$ / snamenc$ / sextnc$) */
    const char *nci_path;     /* (spathnci$ ...) */
    const char *pst_path;     /* (spathpst$ ...) */
    const char *part_path;    /* smcpath$ / smcname$ / smcext$ */
    int product;              /* 0 mill, 1 router, 2 lathe, 3 wire (CPostComponents product / post type) */
    int metric;               /* initial met_tool$ / IsMetric */
    int day, mon, year, hour, min, sec;   /* posting time (date$ / time$ ...); ours: supplied by the caller */
    void *prm_ctx;            /* opaque context for mp_prm callbacks (const McamFile * etc.) */
    const char *eol;          /* the standard end of block AppendEndOfBlock @1800d9660 appends when omitcrlf$ == 0: NULL = mcpost's
                                 own CPostOutput +0x1c0 = DAT_180210aa0 "\r\n" (mp_cli default); the app passes "\n" (ours: user
                                 decision P3, formats/user_decisions.md "Post engine"). omitcrlf$ / use_alt_eob$ / eob$ (the post's
                                 and the control definition's own settings) are honoured either way. */
} MpEnv;

MpRun *mp_run_new(const MpPost *p, const MpEnv *env, FILE *nc_out, char *err, size_t errn);
void   mp_run_free(MpRun *r);

/* symbols: index lookups return -1 when absent. Predefined names include the '$' (e.g. "x$"). */
int     mp_var_find(const MpRun *r, const char *name);
double *mp_var(MpRun *r, int idx);          /* current value */
double *mp_var_prv(MpRun *r, int idx);      /* previous value (prv_name) */
int     mp_str_find(const MpRun *r, const char *name);
const char *mp_str_get(const MpRun *r, int idx);
void    mp_str_set(MpRun *r, int idx, const char *s);
int     mp_block_find(const MpRun *r, const char *name);
int     mp_block_defined(const MpRun *r, int idx);      /* 1 if the post defines a body for it */

/* call a postblock (CPostRunPostTable::BlockCall @180129c70 semantics incl. its predefined-call special cases).
   returns 1 ok, 0 on a run error / exitpost$ */
int mp_block_call(MpRun *r, int idx);

/* run-level status for the read loop (exitpost$, fatal errors) */
int mp_run_stopped(const MpRun *r);
const char *mp_run_error(const MpRun *r);

/* NCI read loop (CPostRun::RunTraditional @180112130 + CPostReadNci::ReadLoop @180101990): run the whole post over an
   ASCII NCI text file. Returns 1 ok. */
int mp_run_nci(MpRun *r, FILE *nci);

#endif
