/* mp_nciloop_sub.c - subprograms of the NCI read loop (#included by mp_nciloop.c; port of mcpost.dll
 * CPostSubsTraditional + CPostReadNci::rd1018 + CPostLatheCannedTraditional::LatheCannedOn1018/1019).
 *
 *   CPostSubsTraditional::Case1018 @180150740 / Case1019 @180151110 (NCI 1018 / 1019 in the read loop),
 *   CPostReadNci::rd1018 @18010ca00 (Ghidra failed; ported from the disassembly, see below),
 *   CopySubLevelStructure @180151ff0, CopySubNonTransStructure @180152840, DoTransformSubs @180152c90
 *   (c_msng$ / c_mmlt$ in CPostOutput::CommandVariableOutput @1800d9960), the sub-level arrays of
 *   CPostRunComponents (+0x1218 / +0x1240 / +0x1268: CArray of strt_trans_sub_data, 0xd8 bytes; SetSize(10) in
 *   CPostRun::OverrideInitializeFromPprep @180111290, FUN_1801540a0 = SetAtGrow, FUN_180153fe0 = SetAt),
 *   CPostRunComponents::SetOutputNcSubs @18011b250 (+0x13dc: > 0 suppresses NC lines, OutputToNcOnEob @1800dc620).
 * NOT DECODED here: the per-level sub files of the transform modes 3/4 (CPostSubsTraditional::FileNewSub @180153580,
 * CreateLevelCfile @180152b10, MergeSubFiles @180153d60, ClearAndRemoveCFile @180151f50; strt_trans_sub_data +0x00 /
 * +0x08 hold their CFile pointers - kept as opaque values), CPostRunComponents::Get/SetOutputNcProbe (probe). */

/* strt_trans_sub_data (offsets relative to the struct; CPostRunComponents +0x1290 = the current one) */
typedef struct {
    long long f0, f8;       /* +0x00 / +0x08 (sub file handles of modes 3/4) */
    double seqno;           /* +0x10 seqno$ at the sub start (sub_seq_typ$ 1) */
    int ref;                /* +0x1c 1st field of the 1018 line (sub_ref_id$ / main_prg_no$) */
    int grp, trns, sec, totl, chn, inc, trnstyp, trnmthd;   /* +0x20 .. +0x3c */
    double m[9], t[3];      /* +0x40 .. +0x80 sub_m1..9$, +0x88 .. +0x98 sub_trnsx/y/z$ */
    int nxt_t, nxt_h, nxt_tid, mny_t, nst;                  /* +0xa0 .. +0xb0 */
    char b4, b5, b6;        /* +0xb4 sub call pending, +0xb5 no output (sub_level$ -1 at the call), +0xb6 output on */
} SubLvl;

typedef struct { SubLvl *a; int n; } SubArr;

typedef struct {
    SubArr org, prv, lvl;   /* +0x1218 / +0x1240 / +0x1268 */
    SubLvl cur;             /* +0x1290 */
    struct { int op, grp, trns, sec, totl, chn, inc; char a0, a1, a2; } nt;   /* +0x1368 strt_non_trans_sub_data
                               (+0x1c .. +0x34; +0x38 / +0x39 / +0x3a = run +0x13a0 / +0x13a1 / +0x13a2) */
    int lvl_save;           /* +0x13c8 */
    int level;              /* +0x13cc (-1 = inside a non-transform sub) */
    int seq;                /* +0x13d0 */
    int main;               /* +0x13d4 */
    int max;                /* +0x13d8 */
    char deep;              /* +0x1212 transform nesting deeper than modes 1/2 support */
} SubState;

static SubState *SUBS(Nci *N)
{
    if (!N->subs) N->subs = calloc(1, sizeof(SubState));
    return (SubState *)N->subs;
}
static void sub_free(Nci *N)
{
    SubState *S = N->subs;
    if (!S) return;
    free(S->org.a); free(S->prv.a); free(S->lvl.a); free(S); N->subs = NULL;
}

static void subarr_grow(SubArr *A, int n)
{
    if (n <= A->n) return;
    A->a = realloc(A->a, (size_t)n * sizeof *A->a);
    memset(A->a + A->n, 0, (size_t)(n - A->n) * sizeof *A->a);
    A->n = n;
}
/* element access with CArray's bound check (Ordinal_2300 = AfxThrowInvalidArgException: mcpost aborts the run) */
static SubLvl *SA(Nci *N, SubArr *A, int i)
{
    if (i < 0 || i >= A->n) {
        mp_rt_error(N->r, 0, "subprogram level %d out of range (CArray bound: AfxThrowInvalidArgException)", i);
        N->r->status = 6;
        static SubLvl dummy; memset(&dummy, 0, sizeof dummy); return &dummy;
    }
    return &A->a[i];
}
static void SA_set_grow(SubArr *A, int i, const SubLvl *s) { if (i < 0) return; subarr_grow(A, i + 1); A->a[i] = *s; }

/* OverrideInitializeFromPprep @180111290 (sub-mode != -1): SetSize(10) of the three arrays, then the entry +0x13d0:
   +0xb4 = 1, +0xb5 = 0, +0xb6 = 1, +0x10 = seqno$ */
static void sub_init(Nci *N)
{
    SubState *S = SUBS(N);
    if (S->org.n == 0) subarr_grow(&S->org, 10);
    if (S->prv.n == 0) subarr_grow(&S->prv, 10);
    if (S->lvl.n == 0) subarr_grow(&S->lvl, 10);
    SubLvl *e = SA(N, &S->lvl, S->seq);
    e->b4 = 1; e->b5 = 0; e->b6 = 1;
    e->seqno = V("seqno$");
}

/* CPostReadNci::rd1018 @18010ca00 (objdump; Ghidra's decompile fails). param = 1 (transform): +0x1344 = 1,
   +0x1345 / +0x1346 = 0, sscanf "%i %i %i %i %i %i %i %i %i %lf x12 %i %i %i %i %i" into +0x12ac .. +0x1340, then
   AssignReadNci: sub_grp_id$ +0x20, sub_op_id$ 0, sub_sec_no$ +0x28, sub_totl_no$ +0x2c, sub_ref_id$ +0x1c,
   sub_chn_no$ +0x30, sub_trnmthd$ +0x3c, sub_inc$ +0x34, sub_trnstyp$ +0x38, sub_m1..9$, sub_trnsx/y/z$,
   sub_nxt_t$ sub_nxt_h$ sub_nxt_tid$ sub_mny_t$ sub_nst_flg$, sub_trns_id$ +0x24.
   param = 0: +0x13a0 = 1, +0x13a1 / +0x13a2 = 0, sscanf "%i %i %i %i %i %i %i" into +0x1384 .. +0x139c (no assignment). */
static void rd1018(Nci *N, const char *p, int trans)
{
    SubState *S = SUBS(N);
    if (!trans) {
        S->nt.a0 = 1; S->nt.a1 = 0; S->nt.a2 = 0;
        sscanf(p, "%i %i %i %i %i %i %i", &S->nt.op, &S->nt.grp, &S->nt.trns, &S->nt.sec, &S->nt.totl, &S->nt.chn,
               &S->nt.inc);
        return;
    }
    SubLvl *c = &S->cur;
    c->b4 = 1; c->b5 = 0; c->b6 = 0;
    sscanf(p, "%i %i %i %i %i %i %i %i %i %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %i %i %i %i %i",
           &c->ref, &c->grp, &c->trns, &c->sec, &c->totl, &c->chn, &c->inc, &c->trnstyp, &c->trnmthd,
           &c->m[0], &c->m[1], &c->m[2], &c->m[3], &c->m[4], &c->m[5], &c->m[6], &c->m[7], &c->m[8],
           &c->t[0], &c->t[1], &c->t[2], &c->nxt_t, &c->nxt_h, &c->nxt_tid, &c->mny_t, &c->nst);
    static const char *const mn[] = {"sub_m1$", "sub_m2$", "sub_m3$", "sub_m4$", "sub_m5$", "sub_m6$", "sub_m7$",
                                     "sub_m8$", "sub_m9$"};
    AR(N, c->grp, "sub_grp_id$");
    AR(N, 0.0, "sub_op_id$");
    AR(N, c->sec, "sub_sec_no$");
    AR(N, c->totl, "sub_totl_no$");
    AR(N, c->ref, "sub_ref_id$");
    AR(N, c->chn, "sub_chn_no$");
    AR(N, c->trnmthd, "sub_trnmthd$");
    AR(N, c->inc, "sub_inc$");
    AR(N, c->trnstyp, "sub_trnstyp$");
    for (int k = 0; k < 9; k++) AR(N, c->m[k], mn[k]);
    AR(N, c->t[0], "sub_trnsx$"); AR(N, c->t[1], "sub_trnsy$"); AR(N, c->t[2], "sub_trnsz$");
    AR(N, c->nxt_t, "sub_nxt_t$"); AR(N, c->nxt_h, "sub_nxt_h$"); AR(N, c->nxt_tid, "sub_nxt_tid$");
    AR(N, c->mny_t, "sub_mny_t$"); AR(N, c->nst, "sub_nst_flg$");
    AR(N, c->trns, "sub_trns_id$");
}

/* CopySubLevelStructure @180151ff0: org 0 = sub_*$, 1 = sub_org_*$; slot = GetVarRef index (0 current, 1 previous);
   flags: 1 -> sub_op_id = 0 (else +0x1c), 2 -> sub_op_id untouched; sub_ref_id = +0x1c unless flags & 8 (the decomp's
   flags & 4 zero is overwritten by the following unconditional +0x1c store) */
static void copy_sub_level(Nci *N, int org, int slot, const SubLvl *s, int flags)
{
    static const char *const n0[] = {"sub_grp_id$", "sub_op_id$", "sub_sec_no$", "sub_totl_no$", "sub_ref_id$",
        "sub_chn_no$", "sub_trnmthd$", "sub_inc$", "sub_trnstyp$", "sub_m1$", "sub_m2$", "sub_m3$", "sub_m4$",
        "sub_m5$", "sub_m6$", "sub_m7$", "sub_m8$", "sub_m9$", "sub_trnsx$", "sub_trnsy$", "sub_trnsz$", "sub_nxt_t$",
        "sub_nxt_h$", "sub_nxt_tid$", "sub_mny_t$", "sub_nst_flg$", "sub_trns_id$"};
    static const char *const n1[] = {"sub_org_grp_id$", "sub_org_op_id$", "sub_org_sec_no$", "sub_org_totl_no$",
        "sub_org_ref_id$", "sub_org_chn_no$", "sub_org_trnmthd$", "sub_org_inc$", "sub_org_trnstyp$", "sub_org_m1$",
        "sub_org_m2$", "sub_org_m3$", "sub_org_m4$", "sub_org_m5$", "sub_org_m6$", "sub_org_m7$", "sub_org_m8$",
        "sub_org_m9$", "sub_org_trnsx$", "sub_org_trnsy$", "sub_org_trnsz$", "sub_org_nxt_t$", "sub_org_nxt_h$",
        "sub_org_nxt_tid$", "sub_org_mny_t$", "sub_org_nst_flg$", "sub_org_trns_id$"};
    const char *const *n = org ? n1 : n0;
    MpRun *r = N->r;
    *VR(r, n[0], slot) = s->grp;
    if (!(flags & 2)) *VR(r, n[1], slot) = (flags & 1) ? 0.0 : s->ref;
    *VR(r, n[2], slot) = s->sec;
    *VR(r, n[3], slot) = s->totl;
    if (!(flags & 8)) *VR(r, n[4], slot) = s->ref;
    *VR(r, n[5], slot) = s->chn;
    *VR(r, n[6], slot) = s->trnmthd;
    *VR(r, n[7], slot) = s->inc;
    *VR(r, n[8], slot) = s->trnstyp;
    for (int k = 0; k < 9; k++) *VR(r, n[9 + k], slot) = s->m[k];
    for (int k = 0; k < 3; k++) *VR(r, n[18 + k], slot) = s->t[k];
    *VR(r, n[21], slot) = s->nxt_t;
    *VR(r, n[22], slot) = s->nxt_h;
    *VR(r, n[23], slot) = s->nxt_tid;
    *VR(r, n[24], slot) = s->mny_t;
    *VR(r, n[25], slot) = s->nst;
    *VR(r, n[26], slot) = s->trns;
}
/* CopySubNonTransStructure @180152840 */
static void copy_sub_nontrans(Nci *N)
{
    SubState *S = SUBS(N);
    V("sub_op_id$") = S->nt.op;
    V("sub_grp_id$") = S->nt.grp;
    V("sub_org_trns_id$") = S->nt.trns;
    V("sub_sec_no$") = S->nt.sec;
    V("sub_totl_no$") = S->nt.totl;
    V("sub_chn_no$") = S->nt.chn;
    V("sub_inc$") = S->nt.inc;
}

static void set_out_nc_subs(Nci *N, int v) { N->r->out_nc_subs = (short)v; }   /* SetOutputNcSubs @18011b250 */

/* CPostLatheCannedTraditional::LatheCannedOn1018 @1800aa0c0 (rcc = rcc_flg$, lvl = sub_level$, prg = sub_prg_no$) */
static int lathe_canned_1018(Nci *N, double *rcc, double *lvl, double *prg)
{
    SubState *S = SUBS(N);
    if (*rcc == 4.0) {
        if (N->sub_mode != 0) { set_out_nc_subs(N, 0); S->nt.a2 = 1; S->nt.a0 = 0; }
        if (!BC(N->r, "prcc_call_st$")) return 0;
        *lvl = (double)S->level;
    } else *rcc += 1.0;
    if (*rcc == 7.0) {
        *prg = V("sub_op_id$");
        if (!BC(N->r, "pfcc_call$")) return 0;
        if (N->sub_mode != 0) { set_out_nc_subs(N, 1); S->nt.a2 = 0; }
    }
    return 1;
}
/* CPostLatheCannedTraditional::LatheCannedOn1019 @1800aa1f0 */
static int lathe_canned_1019(Nci *N, char *flag)
{
    double *rcc = VR(N->r, "rcc_flg$", 0);
    if (*rcc == 0.0) return 1;
    *flag = 1;
    int v = (int)*rcc;
    if (v == 4) { *rcc += 1.0; return BC(N->r, "prcc_call_end$") ? 1 : 0; }
    if (v == 5) return 1;
    if (v == 7) { *rcc += 1.0; if (N->sub_mode != 0) set_out_nc_subs(N, 0); return 1; }
    if (v == 8) { if (!BC(N->r, "pfcc_end$")) return 0; }
    /* else: _MCAssert (PPostLatheCannedTraditional.cpp 0xa8) */
    *rcc = 0.0;
    return 1;
}

/* the run message 0x2711 (CPostMessages::AddRunGeneralMsg, NCI error) of Case1018 / Case1019 */
static void sub_msg(Nci *N, int flag)
{
    mp_rt_error(N->r, 0, "NCI subprogram error (CPostMessages 0x2711 flag %#x)", flag);
}

/* CPostSubsTraditional::Case1018 @180150740. returns 1 ok, 0 failed (the read loop stops) */
static int case1018(Nci *N, const char *p)
{
    if (N->sub_mode == -1) return 1;
    SubState *S = SUBS(N);
    double f[3] = {0, 0, 0};
    sscanf(p, "%lf %lf %lf", &f[0], &f[1], &f[2]);
    if (f[2] <= 0.0) {
        S->lvl_save = S->level;
        S->level = -1;
        rd1018(N, p, 0);
    } else {
        S->level++;
        S->main = S->level;
        if (N->sub_mode < 3 && 1 < S->level) {
            sub_msg(N, 4);
            S->deep = 1;
            S->main = 1;
            return 1;
        }
        rd1018(N, p, 1);
        if (S->max < S->level) {
            S->cur.f0 = 0; S->cur.f8 = 0;
            S->cur.seqno = V("seqno$");
            SA_set_grow(&S->org, S->level, &S->cur);
            SA_set_grow(&S->prv, S->level, &S->cur);
            S->max = S->level;
        } else {
            SubLvl *e = SA(N, &S->lvl, S->level);
            S->cur.f0 = e->f0;
            S->cur.f8 = e->f8;
        }
        if (S->cur.nst % 10 != 0) {
            SA_set_grow(&S->org, S->level, &S->cur);
            SA_set_grow(&S->prv, S->level, &S->cur);
        }
        SA_set_grow(&S->lvl, S->level, &S->cur);
    }
    V("sub_rd_level$") = (double)S->level;
    SA(N, &S->lvl, 0)->inc = V("absinc$") != 0.0;
    if (S->level != -1) return 1;
    /* non-transform subprogram */
    copy_sub_nontrans(N);
    double *rcc = VR(N->r, "rcc_flg$", 0), *sl = VR(N->r, "sub_level$", 0), *prg = VR(N->r, "sub_prg_no$", 0);
    if (*rcc != 0.0) return lathe_canned_1018(N, rcc, sl, prg);
    *prg = V("sub_op_id$");
    if (!BC(N->r, "psub_call_s$")) return 0;
    if (V("sub_seq_typ$") == 1.0 && -1 < S->seq) SA(N, &S->lvl, S->seq)->seqno = V("n$");
    if (*sl == -1.0) S->nt.a1 = 1;
    else S->seq = S->level;
    if (N->sub_mode < 3) {
        *sl = 2.0;
        if (N->sub_mode != 0) V("subout$") = 2.0;
    } else *sl = (double)S->level;
    if (N->sub_mode != 0 && !S->nt.a1) {
        if (V("sub_sec_no$") == 1.0) { set_out_nc_subs(N, 0); S->nt.a2 = 1; }
        else { set_out_nc_subs(N, 1); S->nt.a2 = 0; }
    }
    if (!S->nt.a1) {
        if (V("sub_seq_typ$") == 1.0) V("n$") = V("seqno$");
        V("absinc$") = (double)S->nt.inc;
        if (!BC(N->r, "psub_st_s$")) return 0;
        S->nt.a0 = 0;
    }
    return 1;
}

/* CPostSubsTraditional::Case1019 @180151110 */
static int case1019(Nci *N, const char *p)
{
    if (N->sub_mode == -1) return 1;
    SubState *S = SUBS(N);
    double *subout = VR(N->r, "subout$", 0), *sl = VR(N->r, "sub_level$", 0);
    V("sub_rd_level$") = (double)S->level;
    if (S->deep && S->level != -1) {
        S->level--;
        V("sub_rd_level$") = (double)S->level;
        if (S->level < 2) S->deep = 0;
        return 1;
    }
    rd1019(N, p);
    int nontrans = 0, nooutput = 0;
    char lc = 0;
    int u7, u19;
    if (S->level == -1) {
        nontrans = 1;
        if (!lathe_canned_1019(N, &lc)) return 0;
        S->level = S->lvl_save;
        V("sub_rd_level$") = (double)S->level;
        u7 = u19 = S->level;
    } else {
        S->cur = *SA(N, &S->org, S->level);
        copy_sub_level(N, 1, 0, &S->cur, 0);
        V("sub_ref_id$") = S->cur.ref;
        S->cur = *SA(N, &S->prv, S->level);
        copy_sub_level(N, 0, 1, &S->cur, 1);
        S->cur = *SA(N, &S->lvl, S->level);
        copy_sub_level(N, 0, 0, &S->cur, 1);
        V("main_prg_no$") = S->cur.ref;
        *SA(N, &S->prv, S->level) = S->cur;                 /* FUN_180153fe0 = SetAt (no grow) */
        u7 = S->level;
        u19 = u7 - 1;
    }
    if (STATUS(N->r) == 6) return 0;
    if (!S->deep) {
        if (u19 < 0) { sub_msg(N, 0x10); return 0; }
        while (SA(N, &S->lvl, u19)->b5 != 0 && 0 < u19) u19--;
        if (u19 < 0) u19 = 0;
    } else u19 = 0 < u7;
    V("absinc$") = (double)SA(N, &S->lvl, u19)->inc;
    if (nontrans) {
        if (!S->nt.a1 && !lc && !BC(N->r, "psub_end_s$")) return 0;
        S->nt.a1 = 1;
        S->nt.a2 = 0;
    } else {
        int l = S->level;
        if (SA(N, &S->lvl, l)->b5 == 0) {
            V("sub_rd_level$") = (double)l;
            if (!BC(N->r, S->level == S->main ? "psub_end_m$" : "psub_end_tt$")) return 0;
        } else nooutput = 1;
    }
    S->seq = u19;
    if (!nontrans) {
        S->level--;
        if (S->level < 0) {
            sub_msg(N, 0x10);
            S->level = 0;
            V("sub_rd_level$") = 0.0;
        }
    }
    int m = N->sub_mode, bb3 = 0;
    if (m < 3) bb3 = 1;
    else if (m != 3 || S->seq != 0) *sl = (double)S->level;
    else bb3 = 1;
    if (bb3) {
        if (nontrans) {
            if (S->seq == 0) {
                *sl = 0.0;
                if (m == 2) *subout = 3.0;
                else {
                    *subout = 0.0;
                    if (!lathe_canned_1019(N, &lc)) return 0;
                }
            } else { *sl = 1.0; *subout = 1.0; }
        } else {
            *sl = 0.0;
            *subout = m == 2 ? 3.0 : 0.0;
        }
    }
    if (V("sub_seq_typ$") == 1.0) V("n$") = SA(N, &S->lvl, S->seq)->seqno;
    if (!nontrans && !nooutput && !BC(N->r, "psub_end_m_r$")) return 0;
    int save = S->level;
    if (S->deep) S->level = 1;
    if (N->sub_mode != 0) set_out_nc_subs(N, SA(N, &S->lvl, S->level)->b6 == 0);
    S->cur = *SA(N, &S->org, S->level);
    copy_sub_level(N, 1, 0, &S->cur, 0);
    V("sub_ref_id$") = S->cur.ref;
    S->cur = *SA(N, &S->prv, S->level);
    copy_sub_level(N, 0, 1, &S->cur, 1);
    S->cur = *SA(N, &S->lvl, S->level);
    copy_sub_level(N, 0, 0, &S->cur, 1);
    V("main_prg_no$") = S->cur.ref;
    if (S->deep) S->level = save;
    return STATUS(N->r) == 6 ? 0 : 1;
}

/* CPostSubsTraditional::DoTransformSubs @180152c90: the pending transform-sub calls (levels whose +0xb4 is still set,
   from the deepest started one up to the current level) */
static int do_transform_subs(Nci *N)
{
    SubState *S = SUBS(N);
    int save = S->level;
    if (S->deep) S->level = 1;
    if (0 < S->level && N->sub_mode != -1) {
        int orig = S->level, i = orig;
        int l21 = S->seq;
        while (SA(N, &S->lvl, i)->b4 != 0 && 0 < i) i--;
        for (S->level = i + 1; S->level <= orig; S->level++) {
            int L = S->level;
            S->cur = *SA(N, &S->lvl, L);
            copy_sub_level(N, 0, 0, &S->cur, 1);
            V("main_prg_no$") = S->cur.ref;
            S->cur = *SA(N, &S->org, L);
            copy_sub_level(N, 1, 0, &S->cur, 0);
            V("sub_ref_id$") = S->cur.ref;
            if (V("sub_chn_no$") != 1.0) S->cur = *SA(N, &S->prv, L);
            copy_sub_level(N, 0, 1, &S->cur, 1);
            const char *blk = "psub_call_tt$";
            if (L == 1) blk = SA(N, &S->lvl, 1)->mny_t != 0 ? "psub_call_mm$" : "psub_call_m$";
            if (STATUS(N->r) == 6 || !BC(N->r, blk)) return 0;
            double *sl = VR(N->r, "sub_level$", 0);
            if (*sl == -1.0) SA(N, &S->lvl, L)->b5 = 1;
            else S->seq = L;
            int l220 = 0;
            if (N->sub_mode < 3) {
                *sl = 1.0;
                if (N->sub_mode != 0) { V("subout$") = 1.0; l220 = 1; }
            } else { *sl = (double)L; l220 = 1; }
            if (l220 && N->sub_mode != 0 && SA(N, &S->lvl, L)->b5 == 0) {
                if (V("sub_sec_no$") == 0.0) { set_out_nc_subs(N, 0); SA(N, &S->lvl, L)->b6 = 1; }
                else { set_out_nc_subs(N, 1); SA(N, &S->lvl, L)->b6 = 0; }
            }
            if (SA(N, &S->lvl, L)->b5 == 0) {
                if (V("sub_seq_typ$") == 1.0) {
                    if (l21 != -1) { SA(N, &S->lvl, l21)->seqno = V("n$"); l21 = -1; }
                    SA(N, &S->lvl, S->seq)->seqno = V("seqno$");
                    V("n$") = V("seqno$");
                }
                V("absinc$") = (double)SA(N, &S->lvl, L)->inc;
                if (!BC(N->r, L != S->main ? "psub_st_tt$" : "psub_st_m$")) return 0;
                SA(N, &S->lvl, L)->b4 = 0;
            }
        }
        S->level = orig;
    }
    if (S->deep) S->level = save;
    return 1;
}

/* c_msng$ / c_mmlt$ (CommandVariableOutput @1800d9960): DoTransformSubs when the current level's sub_mny_t field is
   0 (c_msng$) / not 0 (c_mmlt$); outside a transform level (+0x13cc == -1): post-table message 0x2714 */
static void sub_cmd(Nci *N, int mmlt)
{
    SubState *S = SUBS(N);
    if (S->level == -1) {
        mp_rt_error(N->r, 0, "%s outside a transform subprogram (CPostMessages 0x2714)", mmlt ? "c_mmlt$" : "c_msng$");
        return;
    }
    if (N->sub_mode == -1) return;
    int mny = SA(N, &S->lvl, S->level)->mny_t;
    if (mmlt ? mny != 0 : mny == 0) do_transform_subs(N);   /* its result is ignored (CommandVariableOutput) */
}
