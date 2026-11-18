/* mp_out.c - NC output: CPostOutput::OutputVariable @1800dd320, IsModal @1800dbae0, OutputString @1800dc170,
 * OutputUpdate @1800dd110, CommandVariableOutput @1800d9960, OutputToNcOnEob @1800dc620, PadSpacesOnLine @1800ddc30,
 * UpdateVariableOptions @1800deb10, UpdateOnLineOutput @1800dea90, BumpSequenceNumber @1800d9760,
 * BumpWriteCount @1800d9820, AppendEndOfBlock @1800d9660, OutputToNcStream @1800dc9c0, CPostFstrsel::SelectString
 * @18009a2e0, CPostMath::DressedFormattedNumberString @1800bb9a0. Number text comes from mp_fmt.c. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "mp_int.h"

/* --------------------------------------------------------------- small helpers */
static int V(MpRun *r, const char *name) { return mp_var_find(r, name); }
static double getv(MpRun *r, const char *name) { int i = V(r, name); return i >= 0 ? r->vars[i].v[0] : 0; }
static double *refv(MpRun *r, const char *name) { int i = V(r, name); return i >= 0 ? &r->vars[i].v[0] : NULL; }

const char *mp_out_eol(MpRun *r);
static void line_add(MpRun *r, const char *s, int n)
{
    if (n < 0) n = (int)strlen(s);
    if (r->linelen + n + 1 > r->linecap) { r->linecap = (r->linelen + n + 1) * 2 + 256; r->line = realloc(r->line, r->linecap); }
    memcpy(r->line + r->linelen, s, n); r->linelen += n; r->line[r->linelen] = 0;
}
/* PadSpacesOnLine @1800ddc30: unless the one-shot no_spc$ was output (this+0x1ba), and no_spc$ <> 1, spaces$ <> 0 and the
   line is not empty: append spaces$ blanks (PadStringWithSpacesByLength @18014be90) */
static void pad(MpRun *r)
{
    if (r->nospc_once) { r->nospc_once = 0; return; }
    if (getv(r, "no_spc$") == 1.0) return;
    int sp = (int)getv(r, "spaces$");
    if (sp == 0 || r->linelen == 0) return;
    for (int i = 0; i < sp; i++) line_add(r, " ", 1);
}
static void bump(MpRun *r) { r->writecount++; r->wrote = 1; }     /* BumpWriteCount @1800d9820 + SetPostBlockWrote(1,1) */

static MpFmtCtx fctx(MpRun *r) { MpFmtCtx c; c.round_opt = getv(r, "round_opt$"); return c; }
static int metric(MpRun *r) { return r->env.metric != 0; }   /* CPostComponents +0xe6 (GetIsMetric) */

/* FormattedNumber with the output's format statement: this+0x170 = the variable's fs, else the default (fs 0, this+0x168
   = component +0xe8; CPostOutput::Initialize). The flag passed is !(+0x68 & 4) - NOT DECODED: read-only values are rounded
   the same way here. */
static const MpFs *out_fs(MpRun *r, int var)
{
    int n = r->vars[var].fs;
    if (n != MP_NOFS && n >= MP_FSMIN && n < MP_FSMAX && r->fs[n - MP_FSMIN].defined) return &r->fs[n - MP_FSMIN];
    return &r->fs[0 - MP_FSMIN];
}
double mp_fmt_value(MpRun *r, int var, double v)
{
    MpFmtCtx c = fctx(r);
    return mp_fmt_round(mp_var_fs(r, var), metric(r), v, &c);
}

/* DressedFormattedNumberString @1800bb9a0 (types 900/901, 0x385.., 0x387/0x388 debug, 0x38b prv). Returns the text
   length; *nopad = 1 when the variable has a fmt with an empty prefix or the text is empty (param_5). */
static int dressed(MpRun *r, int code, int var, char *out, size_t n, int *nopad)
{
    MpVar *v = &r->vars[var];
    double val = v->v[0]; int raw = 0; const MpFs *fs = NULL;
    int vfs = v->fs;
    const MpFs *varfs = (vfs != MP_NOFS && vfs >= MP_FSMIN && vfs < MP_FSMAX && r->fs[vfs - MP_FSMIN].defined) ? &r->fs[vfs - MP_FSMIN] : NULL;
    double bug4 = getv(r, "bug4$");
    *nopad = 0; out[0] = 0;
    if (code == EL_VAR_PRV) { val = v->v[1]; fs = varfs; }
    else if (code == EL_VAR_DEBUG || code == EL_VAR_DBGPRV) {
        int p = code == EL_VAR_DBGPRV;
        if (bug4 >= 0) { fs = varfs; val = v->v[p]; }
        else { fs = NULL; raw = 1; val = bug4 == -2.0 ? v->v[p ? 3 : 2] : v->v[p]; }
    } else {                     /* 900 / 901 (and forced types share the dressing) */
        fs = varfs;
        if (!fs) fs = mp_fs_get(r, 1);                      /* NULL fs -> component +0xf8 (fs 1) */
        val = mp_fmt_incr_value(fs, v->v[0], v->v[1]);      /* 'i' incremental: cur - prev */
    }
    if (!fs) fs = mp_fs_get(r, 1);
    char num[128]; MpFmtCtx c = fctx(r);
    mp_fmt_number_ex(fs, metric(r), val, &c, raw ? MP_FMT_RAW : 0, num, sizeof num, NULL, NULL);
    int dbg = code == EL_VAR_DEBUG || code == EL_VAR_DBGPRV;
    size_t k = 0;
    if (v->fmt < 0) {
        if (dbg) k = snprintf(out, n, "%s ", v->name);
        k += snprintf(out + k, n - k, "%s", num);
    } else {
        MpFmt *f = &r->fmts[v->fmt];
        const char *pre = f->prefix_str >= 0 ? mp_get_str(r, f->prefix_str) : f->prefix ? f->prefix : "";
        if (!*pre) { if (dbg) k = snprintf(out, n, "%s ", v->name); else *nopad = 1; }
        else k = snprintf(out, n, "%s", pre);
        k += snprintf(out + k, n - k, "%s", num);
        const char *suf = f->suffix_str >= 0 ? mp_get_str(r, f->suffix_str) : f->suffix ? f->suffix : "";
        k += snprintf(out + k, n - k, "%s", suf);
    }
    if (k == 0) *nopad = 1;
    return (int)k;
}
int mp_fmt_var_text(MpRun *r, int var, double v, char *out, size_t n, int with_prefix)
{
    double save = r->vars[var].v[0]; r->vars[var].v[0] = v; int np;
    int k = dressed(r, EL_VAR, var, out, n, &np);
    r->vars[var].v[0] = save;
    if (!with_prefix && r->vars[var].fmt >= 0) {
        char num[128]; MpFmtCtx c = fctx(r);
        k = mp_fmt_number_ex(mp_var_fs(r, var), metric(r), v, &c, 0, num, sizeof num, NULL, NULL);
        snprintf(out, n, "%s", num);
    }
    return k;
}

/* ------------------------------------------------- previous-value bookkeeping */
static void upd_push(MpRun *r, int var)
{
    if (r->nupd == r->cupd) { r->cupd = r->cupd ? r->cupd * 2 : 64; r->upd = realloc(r->upd, r->cupd * sizeof(int)); }
    r->upd[r->nupd++] = var;
}
/* UpdateVariableOptions @1800deb10 (no_updprv$ in this+0x1b8; param_3 = deferred) */
static void update_options(MpRun *r, int var, int inc, int deferred)
{
    int nu = (int)getv(r, "no_updprv$");
    if (nu == 0) {
        if (!deferred) {
            if (!(r->vars[var].f68 & 8)) r->vars[var].v[1] = r->vars[var].v[0];
            if (inc < 0) return;
            if (!(r->vars[inc].f68 & 8)) r->vars[inc].v[1] = r->vars[inc].v[0];
        }
    } else if (!deferred) {
        if (nu != 1) return;
    } else if (nu == 2) return;
    upd_push(r, var);
    if (inc >= 0) upd_push(r, inc);
}
/* UpdateOnLineOutput @1800dea90: after a written line every listed variable's previous value = current */
static void update_on_line(MpRun *r)
{
    for (int i = 0; i < r->nupd; i++) { MpVar *v = &r->vars[r->upd[i]]; if (!(v->f68 & 8)) v->v[1] = v->v[0]; }
}

/* IsModal @1800dbae0: one-shot force -> not modal (+flag); delta formats ('d' fs 0x2000 or fmt 0x400) are modal at
   |v| <= 5e-8; others when current == previous */
static int is_modal(MpRun *r, int var, int *forced)
{
    MpVar *v = &r->vars[var];
    if (v->oneshot) { v->oneshot = 0; *forced = 1; return 0; }
    const MpFs *fs = out_fs(r, var);
    MpFmt *f = v->fmt >= 0 ? &r->fmts[v->fmt] : NULL;
    if ((fs->flags & 0x2000) || (f && (f->flags & 0x400))) return fabs(v->v[0]) <= 5e-08;
    if (isnan(v->v[0]) || isnan(v->v[1])) return 0;
    return v->v[0] == v->v[1];
}

/* --------------------------------------------------------- NC streams (OutputToNcStream @1800dc9c0) */
static FILE *stream_for(MpRun *r, int *which)
{
    int so = (int)getv(r, "subout$");
    *which = so;
    if (so >= 1 && so <= 4) {
        if (!r->sub[so]) r->sub[so] = tmpfile();   /* CPostSubFiles::NewSubAuxExtLcc (ours: temporary file) */
        return r->sub[so];
    }
    return r->nc;
}
void mp_out_raw(MpRun *r, const char *s)
{
    int which; FILE *f = stream_for(r, &which);
    if (!f) return;
    fputs(s, f);
    static const char *cnt[] = {"nchar$", "nchsub$", "nchaux$", "nchext$", "nchlcc$"};
    double *c = refv(r, cnt[which >= 0 && which <= 4 ? which : 0]); if (c) *c += strlen(s);
}
/* AppendEndOfBlock @1800d9660 */
static void append_eob(MpRun *r)
{
    if (getv(r, "use_alt_eob$") != 0) {
        int i = V(r, "eob$");
        if (i >= 0) {
            char c = (char)(int)r->vars[i].v[0]; line_add(r, &c, 1);
            char c2 = (char)((int)r->vars[i].v[1] & 0xff); if (c2) line_add(r, &c2, 1);
        }
    }
    if (getv(r, "omitcrlf$") == 0) { const char *e = mp_out_eol(r); line_add(r, e, (int)strlen(e)); }   /* this+0x1c0 = DAT_180210aa0 "\r\n" (MpEnv.eol) */
}
/* BumpSequenceNumber @1800d9760 */
static void bump_seq(MpRun *r)
{
    if (!r->seqpending || r->writecount == 0) return;
    double *n = refv(r, "n$");
    if (!n) return;
    *n += getv(r, "seqinc$");
    if (*n > getv(r, "seqmax$")) *n = getv(r, "seqno$");
}
/* OutputToNcOnEob @1800dc620 */
int mp_out_eob(MpRun *r, int forced)
{
    int nonc = (int)getv(r, "no_nc_out$");
    /* OutputToNcOnEob @1800dc620: no_nc_out$ == -1, or 0 with GetOutputNcProbe < 1 and GetOutputNcSubs < 1
       (probe: NOT DECODED, taken as 0) */
    int ok = nonc == -1 || (nonc == 0 && r->out_nc_subs < 1);
    int wrote = 0;
    if ((r->linelen != 0 || forced) && r->writecount != 0) {
        if (ok) {
            append_eob(r);
            /* NOT DECODED: TranslateNcString (transchr$ translation table) */
            mp_out_raw(r, r->line);
        }
        wrote = 1;
    }
    if (ok || getv(r, "omitseq$") != -1.0) bump_seq(r);
    if (wrote) update_on_line(r);
    r->nupd = 0;
    r->linelen = 0; if (r->line) r->line[0] = 0;
    r->writecount = 0; r->seqpending = 0;
    return wrote;
}

/* ------------------------------------------------- command variables (CommandVariableOutput @1800d9960) */
/* returns 0 = not a command variable, 1 = handled (nothing to append), 2 = text in *out to append */
static int command_var(MpRun *r, int var, char *out, size_t n)
{
    MpVar *v = &r->vars[var];
    out[0] = 0;
    if (!(v->f68 & 0x10)) return 0;
    if (v->fs != MP_NOFS && v->fs != 0) return 0;          /* a format statement makes it a plain numeric output */
    const char *nm = v->name;
    struct tm tmv; memset(&tmv, 0, sizeof tmv); tmv.tm_isdst = -1;
    tmv.tm_mday = r->env.day; tmv.tm_mon = r->env.mon - 1; tmv.tm_year = r->env.year - 1900;
    tmv.tm_hour = r->env.hour; tmv.tm_min = r->env.min; tmv.tm_sec = r->env.sec;
    mktime(&tmv);
    const char *tf = NULL;                                  /* FUN_1800aea00 = strftime on the posting time */
    if (!strcmp(nm, "time$")) tf = "%H:%M";
    else if (!strcmp(nm, "date$")) tf = "%d-%m-%y";
    else if (!strcmp(nm, "day$")) tf = "%d";
    else if (!strcmp(nm, "mon$")) tf = "%b";
    else if (!strcmp(nm, "month$")) tf = "%m";
    else if (!strcmp(nm, "year$")) tf = "%y";
    if (tf) { strftime(out, n, tf, &tmv); bump(r); return 2; }
    if (!strcmp(nm, "no_spc$")) { r->nospc_once = 1; return 1; }
    if (!strcmp(nm, "ex$")) { r->status = 4; return 1; }
    if (!strcmp(nm, "nci_rewind$")) { r->status = 5; return 1; }
    if (!strcmp(nm, "exitpost$")) { r->status = 6; return 1; }
    if (!strcmp(nm, "fleader$")) {
        char c = (char)(int)getv(r, "ldrcode$"); char s[2] = {c, 0};
        for (int i = 0; i < (int)getv(r, "ncldr$"); i++) mp_out_raw(r, s);
        return 2;
    }
    static const char *mrg[] = {"mergesub$", "mergeaux$", "mergeext$", "mergelcc$"};
    static const char *clr[] = {"clearsub$", "clearaux$", "clearext$", "clearlcc$"};
    static const char *nw[] = {"newsub$", "newaux$", "newext$", "newlcc$"};
    for (int i = 0; i < 4; i++) {
        if (!strcmp(nm, mrg[i])) {         /* CPostSubFiles::MergeToNcSubAuxExtLcc: copy the file into the current stream */
            FILE *src = r->sub[i + 1];
            if (src) {
                fflush(src); long end = ftell(src); rewind(src);
                char b[4096]; long left = end;
                int which; FILE *dst = stream_for(r, &which);
                while (left > 0) { size_t k = fread(b, 1, left > (long)sizeof b ? sizeof b : (size_t)left, src); if (!k) break;
                                   if (dst && dst != src) fwrite(b, 1, k, dst); left -= (long)k; }
                fseek(src, end, SEEK_SET);
            }
            return 1;
        }
        if (!strcmp(nm, clr[i])) { if (r->sub[i + 1]) { fclose(r->sub[i + 1]); r->sub[i + 1] = NULL; } return 1; }
        if (!strcmp(nm, nw[i])) { if (r->sub[i + 1]) { fclose(r->sub[i + 1]); } r->sub[i + 1] = tmpfile(); return 1; }
    }
    if (r->cmd_hook && r->cmd_hook(r, var)) return 1;      /* comment$, canceldc$, tooltable$, ... (NCI loop) */
    if (mp_run_prm_cmd && mp_run_prm_cmd(r, var)) return 1; /* rd_md$ / rd_cd$ / rd_tlpathgrp$ (mp_run_prm.c) */
    /* NOT DECODED here: c_msng$ / c_mmlt$ (transform subprograms), c_rcc_setup$, probe commands, rd_params$
       (CPostRunMCamInfo::ReadOpInfo), db_*, listtpr$/listrad$, mrg_subfile$, redo_proc$, progname$ */
    return 1;
}

/* ------------------------------------------------- the output elements */
static void out_variable(MpRun *r, MpItem *it)
{
    int var = it->idx; MpVar *v = &r->vars[var];
    int code = it->code, seq = 0;
    char text[512]; text[0] = 0; int nopad = 0;
    int nvar = V(r, "n$"), evar = V(r, "e$");
    /* sequence number n$ */
    if (var == nvar) {
        if (code != EL_VAR && code != EL_VAR_BTICK) seq = 1;
        else if (v->oneshot) { v->oneshot = 0; code = EL_VAR_FORCE; seq = 1; }
        else if (getv(r, "omitseq$") != 1.0) seq = 1;
        else return;
    }
    if (code == EL_VAR || code == EL_VAR_BTICK) {
        const MpFs *fs = out_fs(r, var); MpFmt *f = v->fmt >= 0 ? &r->fmts[v->fmt] : NULL;
        if ((fs->flags & 0x400) || (f && (f->flags & 0x200) && !(f->flags & 0x400))) code = EL_VAR_FORCE;
    }
    int deferred = 0, do_upd = 1;
    MpFmtCtx c = fctx(r);
    switch (code) {
    case EL_VAR: {
        if (var == evar) { if (mp_out_eob(r, 0)) r->wrote = 0; return; }
        int cr = command_var(r, var, text, sizeof text);
        if (cr == 1) return;
        if (cr == 2) { do_upd = 0; goto append; }
        if (v->gformula && (v->f78 & 0x40)) mp_get_var(r, var, 0);               /* GlobalVariable @1800dba80 */
        if (!(v->f68 & 4)) v->v[0] = mp_fmt_round(out_fs(r, var), metric(r), v->v[0], &c);
        int forced = 0, modal = is_modal(r, var, &forced);
        if (!modal || seq) {
            /* NOT DECODED: incremental companion (+0x58) output via OutputVariableIncremental */
            dressed(r, code, var, text, sizeof text, &nopad);
            if (!seq) bump(r);
        }
        break;
    }
    case EL_VAR_FORCE:
        if (var == evar) { bump(r); if (mp_out_eob(r, 1)) r->wrote = 0; return; }
        if (v->gformula && (v->f78 & 0x40)) mp_get_var(r, var, 0);
        if (!(v->f68 & 4)) v->v[0] = mp_fmt_round(out_fs(r, var), metric(r), v->v[0], &c);
        dressed(r, EL_VAR, var, text, sizeof text, &nopad);
        bump(r);
        v->oneshot = 0;
        break;
    case EL_VAR_BTICK: {
        if (var == evar) { if (mp_out_eob(r, 0)) r->wrote = 0; return; }
        if (v->gformula && (v->f78 & 0x40)) mp_get_var(r, var, 0);
        if (!(v->f68 & 4)) v->v[0] = mp_fmt_round(out_fs(r, var), metric(r), v->v[0], &c);
        int forced = 0;
        if (!is_modal(r, var, &forced)) { dressed(r, EL_VAR, var, text, sizeof text, &nopad); if (forced) bump(r); }
        deferred = 1;
        break;
    }
    case EL_VAR_PRV:
        if (v->gformula && (v->f78 & 0x80)) mp_get_var(r, var, 1);
        /* fall through */
    case EL_VAR_DEBUG: case EL_VAR_DBGPRV:
        dressed(r, code, var, text, sizeof text, &nopad);
        bump(r);
        do_upd = 0;
        break;
    default: return;
    }
    if (do_upd) update_options(r, var, v->incvar, deferred);
    /* NOT DECODED: LegacyUpdateOnXr @1800dbb90 (tlchng_aft$ = 0 and +0x68 bit 1: copies the output x/y/z into the
       prv of x$/xr$/xh$ ...) */
append:
    if (text[0] && !nopad) pad(r);
    line_add(r, text, -1);
    if (seq) r->seqpending = 1;
    r->nospc_once = 0;
}

/* CPostFstrsel::SelectString @18009a2e0: select the string by the selector variable's value into the target string;
   returns the selector variable */
static int select_string(MpRun *r, int target, int update_mode)
{
    const MpFstrsel *f = &r->fstrsels[r->strs[target].sel];
    int sv = f->selvar;
    if (r->vars[sv].gformula && (r->vars[sv].f78 & 0x40) && update_mode) mp_get_var(r, sv, 0);
    double d = r->vars[sv].v[(f->flags & 1) ? 1 : 0];
    int sel = (short)(int)d, oor = 0;
    if (f->count >= 0 && f->count <= sel) oor = 1;
    if (sel < 0) {
        if (f->under == 0) oor = 1;
        else if (f->under < 0) { if (getv(r, "ss_underflow$") == 0) oor = 1; }   /* NOT DECODED: the underflow walk */
        else if (sel < -f->under) oor = 1;
    }
    int si = f->base;
    if (!oor) {
        si = f->base + sel;     /* SelectStringGetNext / GetPrev walk the string list in declaration order */
        if (si < 0 || si >= r->nstrs) { oor = 1; si = f->base; }
    }
    if (oor && (f->flags & 2)) mp_set_strv(r, target, "");
    else {
        const char *s = mp_get_str(r, si);
        char *cp = strdup(s); mp_set_strv(r, target, cp); free(cp);
        if (oor) mp_rt_error(r, 0, "string select %s: selector %s = %g out of range", r->strs[target].name, r->vars[sv].name, d);
    }
    return sv;
}

static void out_string(MpRun *r, MpItem *it)
{
    int code = it->code;
    char *text = NULL; const char *lit = NULL; int litlen = -1;
    switch (code) {
    case EL_STR: case EL_STR_FORCE: case EL_STR_BTICK: {
        int s = it->idx;
        if (r->strs[s].gformula) mp_get_str(r, s);
        int selv = -1;
        if (r->strs[s].sel >= 0) selv = select_string(r, s, 2);
        int skip = 0, forced = 0, deferred = 0;
        if (selv >= 0) {
            MpVar *v = &r->vars[selv];
            const MpFs *fs = out_fs(r, selv); MpFmt *f = v->fmt >= 0 ? &r->fmts[v->fmt] : NULL;
            if (f && (f->flags & 0x800)) { if (code == EL_STR_FORCE) forced = 1; }
            else if ((fs->flags & 0x400) || (f && (f->flags & 0x200))) forced = 1;
            else if (code != EL_STR_FORCE) skip = is_modal(r, selv, &forced);
            if (code == EL_STR_BTICK && !forced) deferred = 1;
            update_options(r, selv, -1, deferred);
            if (skip) return;
        }
        lit = mp_get_str(r, s);
        if (!(code == EL_STR_BTICK && !forced) && lit[0]) bump(r);
        break;
    }
    case EL_SQUOTE: lit = it->text; break;                 /* 0x38d: no write count */
    case EL_DQUOTE: case EL_STR_DEBUG: lit = code == EL_DQUOTE ? it->text : mp_get_str(r, it->idx); bump(r); break;
    case EL_ASCII: lit = it->text; litlen = it->textlen; bump(r); break;
    default: return;
    }
    if (lit && (litlen > 0 || (litlen < 0 && lit[0]))) pad(r);
    if (lit) line_add(r, lit, litlen < 0 ? (int)strlen(lit) : litlen);
    r->nospc_once = 0;
    free(text);
}

/* OutputUpdate @1800dd110: '!' updates (round, previous = current), '@' rounds, '!string' re-selects */
static void out_update(MpRun *r, MpItem *it)
{
    MpFmtCtx c = fctx(r);
    if (it->code == EL_VAR_UPD) {
        MpVar *v = &r->vars[it->idx];
        if (v->gformula && (v->f78 & 0x40)) mp_get_var(r, it->idx, 0);
        if (!(v->f68 & 4)) v->v[0] = mp_fmt_round(out_fs(r, it->idx), metric(r), v->v[0], &c);
        if (v->incvar >= 0) {
            MpVar *iv = &r->vars[v->incvar];
            if (!(iv->f68 & 4)) iv->v[0] = v->v[0] - v->v[1];
            if (!(iv->f68 & 8)) iv->v[1] = iv->v[0];
        }
        if (!(v->f68 & 8)) v->v[1] = v->v[0];
        return;
    }
    if (it->code == EL_VAR_RND) {
        MpVar *v = &r->vars[it->idx];
        if (!(v->f68 & 4)) v->v[0] = mp_fmt_round(out_fs(r, it->idx), metric(r), v->v[0], &c);
        return;
    }
    if (it->code == EL_STR_UPD) {
        int s = it->idx;
        if (r->strs[s].gformula) mp_get_str(r, s);
        if (r->strs[s].sel >= 0) select_string(r, s, 1);
    }
}

void mp_out_item(MpRun *r, MpItem *it)
{
    switch (it->code) {
    case EL_VAR: case EL_VAR_FORCE: case EL_VAR_BTICK: case EL_VAR_DEBUG: case EL_VAR_DBGPRV: case EL_VAR_PRV:
        out_variable(r, it); break;
    case EL_VAR_UPD: case EL_VAR_RND: case EL_STR_UPD:
        out_update(r, it); break;
    default:
        out_string(r, it); break;
    }
}
/* hooks used by mp_eval_fn.c */
const char *mp_out_eol(MpRun *r) { return r && r->env.eol ? r->env.eol : "\r\n"; }   /* CPostOutput +0x1c0 = DAT_180210aa0; MpEnv.eol (ours, P3) */
