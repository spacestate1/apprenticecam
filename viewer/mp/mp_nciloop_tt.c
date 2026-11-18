/* mp_nciloop_tt.c - tool table pre-read of the MP NCI loop. #included by mp_nciloop.c (it uses that file's static
 * helpers); not compiled on its own. Port of mcpost.dll (Mastercam 2017):
 *   CPostValidateToolChanges (ctor @1801f0a10, ValidateRectifyToolChanges @1801f0e60, ValidateFirstToolChange
 *     @1801f0cc0, ProcessNeedStartToolChange @1801f0c70, FilterRedundant1001 @1801f0b40, ValidateToolChange1000_1002
 *     @1801f0fb0, ForceToolChange1002 @1801f0b90, ProbeToolChangeForce @1801f0c00),
 *   the tool-change part of CPostStreams::BuildEnhancedToolTable @18013b100 (CPostToolTable::BuildAddToolTableRecord
 *     @18015bb00 -> ValidateRectifyToolChanges in tool-table mode; CPostToolTable::UpdateToolChangeFromTooltable
 *     @18015fed0 applied by CPostReadNci::ReadNciData @180106df0),
 *   the tool list PPostToolList (CPostRunComponents +0x210; FUN_180156890 reset, FUN_180156ef0 / FUN_180157190 /
 *     FUN_180157490 / FUN_180157620 add + finish, FUN_180157fe0 query modes 0..16, FUN_180158350 / FUN_180158410
 *     searches, FUN_180157740, FUN_180157b30, FUN_180156dd0 return code) with CPostReadNci::GetNextTool @1800ff8d0
 *     and the t_list() formula function (CPostFormulaSolver::SolveFormulaVariable, disassembly 0x1800927c3..),
 *   CPostNciPreProcess::wrtt @1800cd4b0 (pwrtt$ / pwrttparam$ pre-read: pass 0 = the whole NCI, pass 1 = re-read up to
 *     the current record), GetTooltableDrillCycle @1800cc3d0, GetTooltableNextop @1800cc6c0, CheckAndSignToolUsage
 *     @1800cc0f0 / CheckToolUsed @1800cc280, rd1018_wrtt @1800cd1b0, CPostRunNciUtil::SetMinMax @180129300 /
 *     InitMinMax @180127570 / UnAssembleSetGetValueHelper @180129530, BackupBufferBreak @180125f80 /
 *     RestoreBufferBreak @1801288b0.
 * Probing is not ported anywhere in this engine: CPostRunComponents +0x1150 (GetProbeOption) is taken as 0, i.e.
 * PostSupportsProbe() = 0, GetProbeActiveOutputOption() = -1, the probe tool flag (+0x210 +0xf8, +0x1148) = 0. */

/* ---------------------------------------------------------------------------------------------------------------
 * CPostValidateToolChanges (0x60 bytes). Field = mcpost offset.
 * ------------------------------------------------------------------------------------------------------------- */
typedef struct {
    double cur;        /* +0x10 tool of this tool change (-1) */
    int n;             /* +0x18 */
    char isprobe;      /* +0x1c the current tool is a probe */
    double prev;       /* +0x20 previous standard tool (-1) */
    int nprev;         /* +0x28 */
    double prevp;      /* +0x30 previous probe tool (-1) */
    int nprevp;        /* +0x38 */
    char first_done;   /* +0x3c */
    char force;        /* +0x40 force_tlchg (1020) == 1 */
    char probe_in;     /* +0x41 */
    short probe_opt;   /* +0x42 */
    short need_start;  /* +0x44 */
    short *g;          /* +0x48 strt_nci gcode (NCI mode) / +0x50 strt_tooltable +0x162 (tool-table mode) */
    unsigned *fl;      /* strt_nci +0xbe0 / strt_tooltable +0x30 */
    char nci_mode;     /* +0x58 */
} Vtc;

#define TT_FORCED 0x4000000u   /* strt_nci +0xbe0 / strt_tooltable +0x30 bit set by ProbeToolChangeForce */

static void vtc_init(Vtc *v, int nci_mode)
{
    memset(v, 0, sizeof *v);
    v->cur = v->prev = v->prevp = -1.0;
    v->probe_opt = -1;
    v->nci_mode = (char)nci_mode;
}
/* ProbeToolChangeForce @1801f0c00 */
static int vtc_force(Vtc *v, short code, int noflag)
{
    if (!v->g) return 0;
    if (code == *v->g) return 0;
    *v->g = code;
    if (!noflag && v->fl) *v->fl |= TT_FORCED;
    return 1;
}
/* ValidateToolChange1000_1002 @1801f0fb0 (PostSupportsProbe() = 0) */
static int vtc_1000_1002(Vtc *v)
{
    short code = v->cur == v->prev ? 1000 : 0x3ea;
    int r = vtc_force(v, code, 0);
    if (v->fl) *v->fl &= ~TT_FORCED;      /* (+0x1c || !PostSupportsProbe) */
    return r;
}
/* FilterRedundant1001 @1801f0b40 */
static int vtc_filter1001(Vtc *v)
{
    if (!v->g || *v->g != 0x3e9) return 0;
    return vtc_1000_1002(v);
}
/* ForceToolChange1002 @1801f0b90 */
static int vtc_force1002(Vtc *v)
{
    if (v->force) { vtc_force(v, 0x3ea, 1); return 1; }
    return 0;
}
/* ProcessNeedStartToolChange @1801f0c70 (+0x1150 == 0 -> the flag argument is 1) */
static int vtc_need_start(Vtc *v)
{
    if (v->need_start == 0 && !v->probe_in) { vtc_force(v, 0x3e9, 1); v->need_start = 1; return 1; }
    return 0;
}
/* ValidateFirstToolChange @1801f0cc0: with +0x41 == 0 the first tool change becomes a 1001 (the probe branches need
   +0x1150 != 0) */
static int vtc_first(Vtc *v)
{
    if (!v->probe_in) { vtc_force(v, 0x3e9, 1); v->need_start = 1; return 1; }
    vtc_force(v, 0x3e9, 1);                /* NOT DECODED: probe option branches (+0x1150 bits 12/14) */
    v->need_start = 0;
    return 0;
}
/* ValidateRectifyToolChanges @1801f0e60 (p1 = CPostRunComponents +0x38e, p2 = +0x390) */
static int vtc_rectify(Vtc *v, short p1, short p2, double force, double tool, int probe, short popt, short *g,
                       unsigned *fl)
{
    (void)p1;
    if (!g) return 0;
    v->force = force == 1.0;
    v->probe_opt = popt;
    v->probe_in = (char)probe;
    v->g = g; v->fl = fl;
    int n;
    if (!v->isprobe) { v->prev = v->cur; n = v->n; v->nprev = n; }
    else { v->prevp = v->cur; n = v->n; v->nprevp = n; }
    v->isprobe = (char)probe;
    v->cur = tool;
    v->n = n + 1;
    if (p2 != -3) {
        if (!v->first_done) { v->first_done = 1; return vtc_first(v); }
        if ((v->need_start != 0 || !vtc_need_start(v)) && !vtc_filter1001(v)) {
            if (p2 == -2) vtc_force1002(v);
            else if (p2 == 0 && !vtc_force1002(v)) { vtc_1000_1002(v); return 1; }
        }
        return 1;
    }
    if (!v->first_done) { v->need_start = 1; v->first_done = 1; }
    return 1;
}

/* ---------------------------------------------------------------------------------------------------------------
 * PPostToolList (CPostRunComponents +0x210)
 * ------------------------------------------------------------------------------------------------------------- */
enum { TL_FIRST = 0x1, TL_FIRSTUSE = 0x2, TL_SAME = 0x10, TL_CHANGE = 0x100, TL_LASTPRB = 0x1000, TL_LASTCHG = 0x2000,
       TL_LAST = 0x10000, TL_LASTOFKIND = 0x20000, TL_STD = 0x10000000, TL_PROBE = 0x20000000 };

static int tl_seen(Nci *N, double t)            /* FUN_180157620 (standard map +0x10): 1 = the tool was in the map */
{
    for (int i = 0; i < N->tl.ndist; i++) if (N->tl.dist[i] == t) return 1;
    N->tl.dist = realloc(N->tl.dist, (size_t)(N->tl.ndist + 1) * sizeof *N->tl.dist);
    N->tl.dist[N->tl.ndist++] = t;
    return 0;
}
/* FUN_180156ef0 -> FUN_180157190(pos, 0) -> FUN_180157490 (standard tools only) */
static void tl_add(Nci *N, double tool, int rec)
{
    int i = N->tl.n;
    N->tl.tool = realloc(N->tl.tool, (size_t)(i + 1) * sizeof *N->tl.tool);
    N->tl.fl = realloc(N->tl.fl, (size_t)(i + 1) * sizeof *N->tl.fl);
    unsigned f = TL_STD;
    if (!N->tl.f9) f |= TL_FIRST;
    int seen = tl_seen(N, tool);
    if (!seen && N->tl.e4 == -1) f |= TL_FIRSTUSE;
    if (N->tl.e4 >= 0) {
        if (N->tl.tool[N->tl.e4] == tool) f |= TL_SAME;
        else { f |= TL_CHANGE; N->tl.ec = i; }
        if (i - 1 == N->tl.e8 && N->tl.e8 >= 0) { f |= 0x200; N->tl.f0 = i; }
    }
    N->tl.e4 = i;
    N->tl.tool[i] = tool;
    N->tl.fl[i] = f;
    N->rec[rec].tl = i;                          /* map +0xa8: file position -> index */
    N->tl.n = i + 1;
    N->tl.f9 = 1;
}
/* FUN_180158350: forward search from *idx for (flags & mask) == mask; the tool or -1 */
static double tl_fwd(Nci *N, int *idx, unsigned mask, unsigned *fo)
{
    if (*idx >= 0 && *idx < N->tl.n)
        while ((N->tl.fl[*idx] & mask) != mask && ++*idx < N->tl.n) ;
    if (*idx < N->tl.n && *idx >= 0) { *fo = N->tl.fl[*idx]; return N->tl.tool[*idx]; }
    return -1.0;
}
/* FUN_180158410: backward search */
static double tl_back(Nci *N, int *idx, unsigned mask, unsigned *fo)
{
    if (*idx < 0) return -1.0;
    if (*idx < N->tl.n) {
        while ((N->tl.fl[*idx] & mask) != mask) { if (--*idx < 0) return -1.0; }
    }
    if (*idx >= N->tl.n) return -1.0;
    *fo = N->tl.fl[*idx];
    return N->tl.tool[*idx];
}
static double tl_at(Nci *N, int idx, unsigned *fo)        /* FUN_180157740 */
{
    if (idx < 0 || idx >= N->tl.n) return -1.0;
    *fo = N->tl.fl[idx];
    return N->tl.tool[idx];
}
static int tl_past(Nci *N, int idx)                       /* FUN_180157b30 */
{
    if (N->tl.n == 0) return 0;
    if (idx < 0) return -1;
    return idx > N->tl.n - 1;
}
/* FUN_180157fe0: query mode (jump table @0x18015830c); probe = FUN_180157fa0 (0 here). returns value != -1 */
static int tl_query(Nci *N, int mode, int *idx, int probe, unsigned *fo, double *out)
{
    double v = -1.0;
    *out = -1.0;
    switch (mode) {
    case 0: v = tl_fwd(N, idx, probe ? 1u : 0x10000002u, fo); break;
    case 1: v = tl_fwd(N, idx, 0x10000002u, fo); break;
    case 2: if (!probe) return 0; v = tl_fwd(N, idx, 0x20000002u, fo); break;
    case 3: ++*idx; v = probe ? tl_at(N, *idx, fo) : tl_fwd(N, idx, TL_STD, fo); break;
    case 4: ++*idx; v = tl_fwd(N, idx, TL_STD, fo); break;
    case 5: if (!probe) return 0; ++*idx; v = tl_fwd(N, idx, TL_PROBE, fo); break;
    case 6: ++*idx; v = tl_fwd(N, idx, probe ? 0x100u : 0x10000100u, fo); break;
    case 7: ++*idx; v = tl_fwd(N, idx, 0x10000100u, fo); break;
    case 8: ++*idx; v = tl_fwd(N, idx, 0x20000100u, fo); break;
    case 9:
        *idx = N->tl.n - 1;
        if (probe) { v = tl_back(N, idx, TL_LASTPRB, fo); break; }
        v = tl_back(N, idx, TL_LASTCHG, fo);
        *out = v;
        if (v != -1.0) return 1;
        if (N->tl.n < 1) return 0;            /* (mcpost: CArray bounds exception) */
        *out = N->tl.tool[0];
        *fo = N->tl.fl[0];
        return *out != -1.0;
    case 10: *idx = N->tl.n - 1; v = tl_back(N, idx, 0x10002000u, fo); break;
    case 11: if (!probe) return 0; *idx = N->tl.n - 1; v = tl_back(N, idx, 0x20002000u, fo); break;
    case 12: v = (double)0 + (double)N->tl.ndist; break;          /* +0x60 (probe map) + +0x28 (standard map) */
    case 13: v = (double)N->tl.ndist; break;
    case 14: if (!probe) return 0; v = 0.0; break;
    case 15:
        if (N->tl.curpos < 0 || N->tl.curpos >= N->nrec || N->rec[N->tl.curpos].tl < 0) { *out = -1.0; return 0; }
        *idx = N->rec[N->tl.curpos].tl;
        v = (double)*idx;
        break;
    case 16: v = tl_at(N, *idx, fo); break;
    default: return 0;
    }
    *out = v;
    return v != -1.0;
}
/* FUN_180157190(pos, 1) at the end of BuildEnhancedToolTable -> first_tool$ / last_tool$ / ntools$ */
static void tl_finish(Nci *N)
{
    double first = 0, last = 0, ntools = 0;
    if (N->tl.n >= 1) {
        if (N->tl.ec >= 0) N->tl.fl[N->tl.ec] |= TL_LASTCHG;
        if (N->tl.f0 >= 0) N->tl.fl[N->tl.f0] |= TL_LASTCHG;
        if (N->tl.f4 >= 0) N->tl.fl[N->tl.f4] |= TL_LASTCHG;
        N->tl.fl[N->tl.n - 1] |= TL_LAST;
        if (N->tl.e4 >= 0) N->tl.fl[N->tl.e4] |= TL_LASTOFKIND;
        if (N->tl.e8 >= 0) N->tl.fl[N->tl.e8] |= TL_LASTOFKIND;
        int idx = 0; unsigned fo = 0;
        if (!tl_query(N, 0, &idx, 0, &fo, &first)) first = 0;
        if (!tl_query(N, 9, &idx, 0, &fo, &last)) last = 0;
        if (!tl_query(N, 12, &idx, 0, &fo, &ntools)) ntools = 0;
    }
    V("first_tool$") = first;
    V("last_tool$") = last;
    V("ntools$") = ntools;
}

/* CPostStreams::BuildEnhancedToolTable @18013b100, tool-change records only: every 1000/1001/1002 adds a tool-list
   entry (FUN_180156ef0: tool = the 1000 record's 4th field t; uselstation$ != 0 on a non-wire post gives 0 there -
   NOT DECODED: the lathe station path FUN_1801570b0) and a tool-table record whose gcode (+0x162) is rectified by
   CPostValidateToolChanges in tool-table mode with +0x38e = 0, +0x390 = force_tlchg$ (the post's value when the
   table is built), force = the last 1020's force_tlchg$ field (12th),
   tool = t (or 1016 lstation when uselstation$ != 0 and not wire). The read loop then takes the tool change gcode and
   flag +0x30 from that record (ReadNciData -> UpdateToolChangeFromTooltable). Records inside a probe block (1042 ..
   1043) are not skipped here - NOT DECODED: probing. The first record's axis combination (strt_tooltable +0x38 =
   CPostGcode950 +0x38 = the 950's 4th field, objdump of ReadParametersFromString @1800a4290) goes to MpRun.axcombo. Must run after read_all, before OverrideInitializeFromMDandCD. */
static void build_tool_table(Nci *N)
{
    Vtc v;
    vtc_init(&v, 0);
    short p2 = (short)(int)V("force_tlchg$");
    double force = 0.0, lstation = 0.0, combo = 0.0;
    int first = 1;
    N->tl.e4 = N->tl.e8 = N->tl.ec = N->tl.f0 = N->tl.f4 = -1;
    N->tl.curpos = -1;
    for (int i = 0; i < N->nrec; i++) {
        NRec *rc = &N->rec[i];
        if (rc->end) break;
        if (rc->g == 0x3fc) { double f[13] = {0}; scan_d(rc->prm, f, 13); force = f[11]; continue; }
        if (rc->g == 0x3f8) { double f[14] = {0}; scan_d(rc->prm, f, 14); lstation = f[13]; continue; }
        if (rc->g == 950) { double f[4] = {0}; scan_d(rc->prm, f, 4); combo = f[3]; continue; }   /* CPostGcode950 +0x38 */
        if (!(999 < rc->g && rc->g < 0x3eb)) continue;
        double f[4] = {0};
        scan_d(rc->prm, f, 4);
        int use_l = V("uselstation$") != 0.0 && N->posttype != 3;
        tl_add(N, use_l ? 0.0 : f[3], i);
        if (first) { N->r->axcombo = (int)combo; first = 0; }    /* strt_tooltable +0x38 of the first record */
        short g = (short)rc->g;
        vtc_rectify(&v, 0, p2, force, use_l ? lstation : f[3], 0, -1, &g, &rc->fl);
        rc->g = g;
    }
    tl_finish(N);
}

/* CPostReadNci::GetNextTool @1800ff8d0 (disassembly: the early returns) */
static void get_next_tool(Nci *N)
{
    N->tl.curpos = N->cur;
    if (V("bldnxtool$") == 0.0 || N->tl.n == 0) { V("ttblend$") = 1.0; return; }
    V("ttblend$") = 0.0;
    int idx = N->rec[N->cur].tl, i0 = idx;
    if (idx < 0) { V("ttblend$") = 1.0; return; }
    unsigned fo = 0;
    if (!tl_query(N, 3, &idx, 0, &fo, VR(N->r, "next_t$", 0)) && tl_past(N, idx) == 1) {
        V("next_t$") = V("last_tool$");
    } else {
        idx = i0;
        if (tl_query(N, 6, &idx, 0, &fo, VR(N->r, "next_tool$", 0))) return;
        if (tl_past(N, idx) != 1) return;
    }
    V("next_tool$") = V("first_tool$");
    V("ttblend$") = 1.0;
}

/* t_list(mode [, index]) (SolveFormulaVariable, disassembly 0x1800927c3..0x18009294a): result = the queried tool
   (-1 none), B = status code, C (when given) = the list index. Called by mp_eval_fn.c. */
int mp_nci_t_list(MpRun *r, int mode, int has_idx, int *idx, int *set_idx, double *status, double *value)
{
    Nci *N = (Nci *)r->nci;
    *status = -1.0; *value = -1.0; *set_idx = 0;
    if (!N || N->tl.n == 0) return 1;
    if (!has_idx || (unsigned)mode <= 2) *idx = 0;
    unsigned fo = 0;
    tl_query(N, mode, idx, 0, &fo, value);
    if (mode < 12 || (14 < mode && mode != 15)) {
        if (*value == -1.0) {
            int p = tl_past(N, *idx);
            if (p == -1) *status = -3.0;
            else if (p == 1) *status = -2.0;
        } else {
            /* FUN_180156dd0 (probe = 0) */
            double s = 0.0;
            if (fo & 1) s = 1.0; else if (fo & 2) s = 2.0;
            if (fo & 0x100) s += 10.0; else if (fo & 0x200) s += 20.0;
            if (fo & 0x1000) s += 100.0; else if (fo & 0x2000) s += 200.0;
            if (fo & 0x10000) s += 1000.0; else if (fo & 0x20000) s += 2000.0;
            if (fo & 0x20000000) s += 100000.0;
            *status = s;
        }
        *set_idx = has_idx;
    } else if (*value != -1.0) *status = 0.0;
    return 1;
}

/* ---------------------------------------------------------------------------------------------------------------
 * CPostNciPreProcess::wrtt @1800cd4b0
 * ------------------------------------------------------------------------------------------------------------- */
/* CPostRunNciUtil::SetMinMax @180129300 (+ Set3dPoint x_max$ / x_min$) */
static void set_min_max(Nci *N, double x, double y, double z, double *mx, double *mn)
{
    double p[3] = {x, y, z};
    for (int a = 0; a < 3; a++) {
        if (p[a] < mn[a]) mn[a] = p[a];
        if (mx[a] <= p[a] && p[a] != mx[a]) mx[a] = p[a];
    }
    set3(N->r, "x_max$", 0, mx);
    set3(N->r, "x_min$", 0, mn);
}
/* rcc_flg$ upper/lower capture of rd1/rd2 in wrtt pass 0 */
static void wrtt_rcc(Nci *N)
{
    double rcc = V("rcc_flg$");
    if (rcc == 0.0) return;
    if (V("tool_op$") == 208.0) {
        if (rcc == 1.0) { copy3(N->r, "upper_x$", "xnci$"); V("rcc_flg$") = rcc + 1.0; }
        else if (rcc == 3.0) { copy3(N->r, "lower_x$", "xnci$"); V("rcc_flg$") = 0.0; }
    } else {
        if (rcc == 3.0) { copy3(N->r, "upper_x$", "xnci$"); V("rcc_flg$") = rcc + 1.0; }
        else if (rcc == 4.0) copy3(N->r, "lower_x$", "xnci$");
    }
}
/* GetTooltableDrillCycle @1800cc3d0 (disassembly 0x1800cc591: the 81 record's first field, + 100 below 100 for
   tool_op$ 16). from = raw index after the tool change */
static void tt_drill_cycle(Nci *N, int from, double opcode, double tool_op)
{
    double *dc = VR(N->r, "nextdc$", 0);
    *dc = -1.0;
    if (!(opcode == 3.0 || opcode == 16.0 || opcode == 105.0 || opcode == 203.0)) return;
    for (int i = from; i < N->nraw; i++) {
        if (N->raw[i].g == 0x51) {
            double v = -1.0; sscanf(N->raw[i].prm, "%lf", &v);
            if (tool_op == 16.0 && v < 100.0) v += 100.0;
            *dc = v;
        }
        if (*dc != -1.0) break;
    }
}
/* GetTooltableNextop @1800cc6c0 */
static void tt_nextop(Nci *N, int from, double tool_op)
{
    double *nop = VR(N->r, "nextop$", 0), *ncf = VR(N->r, "nextcflg$", 0), *ncc = VR(N->r, "nextccomp$", 0),
           *m5 = VR(N->r, "mill5$", 0);
    *nop = 0; *ncf = 0; *ncc = 0; *m5 = 0;
    for (int i = from; i < N->nraw; i++) {
        int g = N->raw[i].g;
        *nop = (double)g;
        double f0 = 0;
        switch (g) {
        case 0: case 1: case 0x14: case 0x15: case 2: case 3: case 0x16: case 0x17:
            sscanf(N->raw[i].prm, "%lf", &f0);
            *ncf = 0.0;
            *ncc = nci_comp_to_ccomp((short)(int)f0);
            break;
        case 0xb:
            *ncf = 0.0; *ncc = 0.0;
            *m5 = tool_op != 28.0 ? 1.0 : 2.0;
            break;
        }
        if (g < 1000) break;
    }
}
/* rd1018_wrtt @1800cd1b0 (table @18029d140: sub_grp_id$ ... sub_nst_flg$ wdum3$) */
static void rd1018_wrtt(Nci *N, const char *p)
{
    static const char *const nm[] = {"sub_grp_id$", "sub_trns_id$", "sub_sec_no$", "sub_totl_no$", "sub_chn_no$",
        "sub_inc$", "sub_trnstyp$", "sub_trnmthd$", "sub_m1$", "sub_m2$", "sub_m3$", "sub_m4$", "sub_m5$", "sub_m6$",
        "sub_m7$", "sub_m8$", "sub_m9$", "sub_trnsx$", "sub_trnsy$", "sub_trnsz$", "sub_nxt_t$", "sub_nxt_h$",
        "sub_nxt_tid$", "sub_mny_t$", "sub_nst_flg$", "wdum3$"};
    double f[27] = {0};
    scan_d(p, f, 7);
    assign_fields(N, f + 1, nm, 6);
    if (V("sub_trns_id$") <= 0.0) { V("sub_op_id$") = f[0]; V("sub_ref_id$") = 0.0; return; }
    memset(f, 0, sizeof f);
    scan_d(p, f, 27);
    assign_fields(N, f + 1, nm, 26);
    V("sub_ref_id$") = f[0];
    V("sub_op_id$") = 0.0;
}

/* param_1 = 1 when called from the tooltable$ command variable (a new CPostRunPostTable is used for the calls - no
   difference here). returns 1 ok, 0 failed (mcpost LAB_1800d0122) */
static int wrtt(Nci *N, int cmdvar)
{
    MpRun *r = N->r;
    long tv = (long)V("tooltable$");
    int neg = tv < 0; if (neg) tv = -tv;
    int tens = (int)(tv % 100) / 10, ones = (int)(tv % 10);
    if (neg) return 0;                                   /* local_1255: the whole body is skipped */
    int even = (tens & 0xfff9) == 0;                     /* bVar31 */
    int b124c = (tens & 0xfffa) == 0;
    int ok = 1;
    /* used-tool map s_mapStandardToolUsed (cleared) */
    double *used = NULL; int nused = 0;
    double mx[3] = {0, 0, 0}, mn[3] = {0, 0, 0};       /* local_11d0 / local_11e8 (zeroed, not InitMinMax) */
    N->wrtt = 1;                                         /* +0x386 */
    r->rc386 = 1;
    r->opinfo_last[0] = r->opinfo_last[1] = r->opinfo_last[2] = 0.0;   /* +0x13e8 / +0x13f0 / +0x13f8 */
    V("tooltable_active$") = 1.0;
    double pa = V("probe_active$");
    V("probe_active$") = 0.0;
    int sav_gcode = N->gcode;
    double s_nextop = V("nextop$"), s_nextdc = V("nextdc$"), s_nextcflg = V("nextcflg$"), s_rcc = V("rcc_flg$"),
           s_cutpos2 = V("cutpos2$"), s_mill5 = V("mill5$"), s_force = V("force_tlchg$");
    static const char *const bb[] = {"cc$", "cstop$", "cgstop$", "cend$", "cstart$", "rpd_typ$", "coolant$"};
    double bbuf[7];
    for (int k = 0; k < 7; k++) bbuf[k] = V(bb[k]);      /* BackupBufferBreak (non-wire argument list) */
    double bdrl[20];
    int ib = vi(r, "bdrl_x$");
    for (int k = 0; k < 20; k++) bdrl[k] = *VRi(r, ib < 0 ? -1 : ib + k, 0);
    Vtc v;
    vtc_init(&v, 1);
    double t1220 = -1.0, t1100 = -1.0, cflg = 0.0;
    int stop_pos = (N->cur >= 0 && N->cur < N->nrec) ? N->rec[N->cur].raw : -1;   /* local_11f0 (+0xbd4) */
    if (stop_pos < 0) stop_pos = -1;
    int pass = 0, bvar4 = 0, prev = 0;
    for (pass = 0; pass < 2 && ok; pass++) {
        if (pass == 1) {                                 /* LAB_1800ce2a8 */
            N->gcode = -1;
            r->opinfo_last[0] = r->opinfo_last[1] = r->opinfo_last[2] = 0.0;
            V("probe_active$") = 0.0;
            vtc_init(&v, 1);
            t1220 = t1100 = -1.0;
            V("cc_pos$") = 0.0;
        }
        int done = 0;
        prev = 0;
        for (int i = 0; i < N->nraw && !done && ok; i++) {
            /* (GetMcamPostReturnMode & 1: NOT DECODED, taken as 0) */
            if (pass == 0 && even && prev == 0x3eb) break;
            NRaw *rw = &N->raw[i];
            int g = rw->g;
            short gs = (short)g;
            unsigned rfl = 0;
            prev = g;
            V("gnci$") = (double)g; V("gcode$") = (double)g;
            N->gcode = g;
            V("nci_line$") = rw->line;
            const char *p = rw->prm;
            int brk = 1;                                 /* 1: break path (LAB_1800d0049), 0: LAB_1800d0068 */
            switch (g) {
            case 0: case 1: case 0x14: case 0x15:
                rd1(N, p);
                if (bvar4) { done = 1; break; }
                if (pass == 0) {
                    wrtt_rcc(N);
                    set_min_max(N, V("xnci$"), V("ynci$"), V("znci$"), mx, mn);
                    N->tt_plane = 0;
                }
                break;
            case 2: case 3: case 0x16: case 0x17:
                rd2(N, p);
                if (bvar4) { done = 1; break; }
                if (pass != 0) break;
                wrtt_rcc(N);
                if (V("plane$") != 0.0) {
                    if (V("ttplane$") == 0.0) V("ttplane$") = V("plane$");
                    else if (V("ttplane$") != V("plane$")) V("ttplane$") = -1.0;
                }
                /* SetArcMinMax @180128f60: NOT DECODED the quadrant points (GetQuadCount / MapToCompatibleArcPlane);
                   its last step, the end point, is applied for the XY plane */
                if (N->tt_plane == 0 && V("plane$") == 0.0) set_min_max(N, V("xnci$"), V("ynci$"), V("znci$"), mx, mn);
                N->tt_plane = (short)(int)V("plane$");
                break;
            case 4: rd4(N, p); break;
            case 0xb:
                /* rd11 (5-axis): NOT DECODED; SetMinMax of xnci$ */
                if (bvar4) { done = 1; break; }
                if (pass == 0) set_min_max(N, V("xnci$"), V("ynci$"), V("znci$"), mx, mn);
                break;
            case 0x1e: case 0x1f: case 0x20: case 0x21:
                /* rd1w / rd2w (wire): NOT DECODED */
                V("wiredirect$") = 1.0;
                if (bvar4) done = 1;
                break;
            case 0x50: break;
            case 0x51: case 100:
                if (g == 0x51) rd81(N, p, &cflg, 0); else rd100(N, p, &cflg, 0);
                if (g == 0x51 && bvar4) { done = 1; break; }
                if (pass == 0) {
                    set_min_max(N, V("drl_depth_x$"), V("drl_depth_y$"), V("drl_depth_z$"), mx, mn);
                    set_min_max(N, V("drl_init_x$"), V("drl_init_y$"), V("drl_init_z$"), mx, mn);
                }
                break;
            case 0x52: {
                static const char *const nm[] = {"drl_prm1$", "drl_prm2$", "drl_prm3$", "drl_prm4$", "drl_prm5$",
                                                 "drl_prm6$", "drl_prm7$", "drl_prm8$", "drl_prm9$", "drl_prm10$"};
                rd_list(N, p, nm, 10);
                break;
            }
            case 0x53: break;                            /* rd83: NOT DECODED (bdrl_* fields) */
            case 200: case 0xc9:
                if (g == 0xc9 && bvar4) done = 1;        /* rd200 / rd201 (lathe thread): NOT DECODED */
                break;
            case 900: case 901: case 902: case 903: case 904: case 905:
                rd_simple(N, g, p);
                if (bvar4) done = 1;
                break;
            case 911: case 912: rd_simple(N, g, p); break;
            case 950: rd950(N, p); break;
            case 999: rd999(N, p); break;
            case 1000: case 0x3e9: case 0x3ea: {
                if (pass == 0) {
                    /* rd1000_next (4th field) into local_1220 when uselstation$ < 1 (or wire), else local_1100 */
                    double f[4] = {0}; scan_d(p, f, 4);
                    if (V("uselstation$") < 1.0 || N->posttype == 3) t1220 = f[3]; else t1100 = f[3];
                    /* ValidatePreventStartOfFile: probe only */
                    vtc_rectify(&v, 0, (short)(int)N->tt_p2, V("force_tlchg$"), t1220, 0, -1, &gs, &rfl);
                    g = gs;
                    V("gcode$") = (double)g; N->gcode = g;
                    double seq = 0;
                    rd1000(N, p, &seq, &seq);
                    if (!rd1000_proc(N)) { ok = 0; break; }
                    tt_drill_cycle(N, i + 1, V("opcode$"), V("tool_op$"));
                    tt_nextop(N, i + 1, V("tool_op$"));
                    /* CheckAndSignToolUsage @1800cc0f0 */
                    double *t = VR(r, "t$", 0);
                    if (*t < 0.0) {
                        mp_rt_error(r, 0, "tool table: negative tool number %g (CPostMessages 0x2711)", *t);
                        ok = 0; break;
                    }
                    int seen = 0;
                    for (int k = 0; k < nused; k++) if (used[k] == *t) seen = 1;
                    if (seen) *t = -*t;
                    else { used = realloc(used, (size_t)(nused + 1) * sizeof *used); used[nused++] = *t; }
                    if (!BC(r, "pwrtt$")) { ok = 0; break; }
                    tv = (long)V("tooltable$");
                    if (tv < 0) { done = 1; break; }
                    tens = (int)(tv % 100) / 10;
                    even = (tens & 0xfff9) == 0;
                    b124c = (tens & 0xfffa) == 0;
                    for (int a = 0; a < 3; a++) { mx[a] = -1e38; mn[a] = 1e38; }   /* InitMinMax (+-0x47d2ced32a16a1b1) */
                    set3(r, "x_max$", 0, mx);
                    set3(r, "x_min$", 0, mn);
                    V("ttplane$") = V("plane$");
                    V("wt_max$") = 0.0;
                    brk = 1;
                    break;
                }
                vtc_rectify(&v, 0, (short)(int)N->tt_p2, V("force_tlchg$"), t1220, 0, -1, &gs, &rfl);
                g = gs;
                V("gcode$") = (double)g; N->gcode = g;
                double seq = 0;
                rd1000(N, p, &seq, &seq);
                /* rotaxtyp$ < 6: CPostRunFiveAxis::setmap / load_matm - NOT DECODED */
                if (!rd1000_proc(N)) { ok = 0; break; }
                if (!BC(r, "psof00$")) { ok = 0; break; }
                if (b124c) {
                    if (N->tlkind != 0 && 3 < tens) { bvar4 = 1; brk = 0; }
                    else done = 1;
                }
                break;
            }
            case 0x3eb:
                if (pass != 0) { if (bvar4) done = 1; break; }
                if (ones == 3) {
                    if (even && !BC(r, "pwrtt$")) ok = 0;
                }
                break;
            case 0x3ed: case 0x3ee: case 0x3ef: case 0x3f0: case 0x402: case 0x41b: case 0x41c: case 0x41d:
            case 0x41e: case 0x420:
                if (pass != 0) break;
                if (mp_run_set_std_fprm) mp_run_set_std_fprm(r, 1);
                SSET(r, "sparameter$", p);
                V("prmcode$") = (double)g;
                if (!BC(r, "pwrttparam$")) ok = 0;
                if (mp_run_set_std_fprm) mp_run_set_std_fprm(r, 0);
                break;
            case 0x3f2: break;                           /* rd1010w (wire): NOT DECODED */
            case 0x3f3:
                if (bvar4) { done = 1; break; }
                rd1011(N, p);                            /* CPostNciPreProcess::rd1011 @1800cd070 */
                if (pass != 0 && !BC(r, "pmiscreal$")) ok = 0;
                break;
            case 0x3f4:
                rd1012(N, p);
                if (pass != 0 && !BC(r, "pmiscint$")) ok = 0;
                break;
            case 0x3f5: {
                rd1013(N, p);
                double c = nci_comp_to_ccomp((short)(int)V("cc$"));
                V("ccomp$") = c;
                if (pass == 0 || V("cc_1013$") != 0.0) {
                    ccomp_to_ccpos((short)(int)c, VR(r, "cc_pos$", 0));
                    AR(N, V("cc_pos$"), "cc_pos$");
                }
                break;
            }
            case 0x3f6: rd1014(N, p); break;             /* + SetInitialToolplaneMatrix: NOT DECODED (5-axis) */
            case 0x3f7: rd1015(N, p); break;
            case 0x3f8:
                rd1016(N, p);
                if (N->posttype != 3) t1220 = V("lstation$");
                break;
            case 0x3f9: rd1017(N, p); break;
            case 0x3fa:
                if (pass == 0) { rd1018_wrtt(N, p); if (V("rcc_flg$") != 0.0) V("rcc_flg$") += 1.0; }
                break;
            case 0x3fb:
                if (pass == 0) { rd1019(N, p); if (V("rcc_flg$") != 0.0) V("rcc_flg$") = 0.0; }
                break;
            case 0x3fc:
                rd1020(N, p);
                /* CallReadOpInfo(pos, cmdvar != 0): pass 0 with +0x386 = 1 (pwrttparam$), pass 1 with +0x386 = 0
                   (pparameter$) then 1 again (wrtt @1800cd4b0 case 0x3fc; the probe branch bVar6 is never taken here) */
                if (mp_run_call_read_op_info) {
                    if (pass == 0) mp_run_call_read_op_info(r, cmdvar != 0);
                    else { r->rc386 = 0; mp_run_call_read_op_info(r, cmdvar != 0); r->rc386 = 1; }
                }
                break;
            case 0x401: zero1025(N); rd1025(N, p); get1025(N); break;
            case 0x403: rd1027(N, p); break;
            case 0x404: rd1028(N, p); break;
            case 0x405: rd1029(N, p); break;
            case 0x407: rd1031(N, p); break;
            case 0x408: rd1032(N, p); break;
            case 0x412: V("probe_active$") = 1.0; break;
            case 0x413: V("probe_active$") = 0.0; break;
            case 0x41a: rd1050(N, p); break;
            default:
                /* parameters and every other code: pwrttparam$ (pass 0) / pparameter$ (pass 1) unless a 10000-19999
                   parameter with param_read$ == 3 */
                if (19999 < g || V("param_read$") != 3.0) {
                    if (V("strtool_v7$") != 0.0 && g == 0x4e21) {
                        if (V("strtool_v7$") == 1.0) filename_to_strings(N, p, "strtoolpath$", "strtool$", "strtoolext$", -1);
                        else SSET(r, "strtool$", p);
                        /* NOT DECODED: the strtoolpath$ drive/".\" fix-up (Ordinal_4459 / 7744 / 1665) */
                    }
                    SSET(r, "sparameter$", p);
                    V("prmcode$") = (double)g;
                    if (mp_run_set_std_fprm) mp_run_set_std_fprm(r, 1);
                    if (!BC(r, pass == 0 ? "pwrttparam$" : "pparameter$")) ok = 0;
                    if (mp_run_set_std_fprm) mp_run_set_std_fprm(r, 0);
                }
                break;
            }
            if (!ok || done) break;
            if (brk && pass == 1 && stop_pos >= 0 && stop_pos <= i) break;   /* LAB_1800d0049 */
        }
        if (!ok) break;
        if (done) { pass = 2; break; }
        if (pass == 0 && ones == 3 && !even) {           /* LAB_1800d0088 ((retmode & 1) || !bVar31) */
            V("gcode$") = 1003.0;
            if (!BC(r, "pwrtt$")) { ok = 0; break; }
        }
    }
    /* LAB_1800d0126: restore */
    if (mp_run_set_std_fprm) mp_run_set_std_fprm(r, 0);
    N->wrtt = 0;
    r->rc386 = 0;
    V("tooltable_active$") = 0.0;
    N->gcode = sav_gcode;
    V("nextop$") = s_nextop; V("nextdc$") = s_nextdc; V("nextcflg$") = s_nextcflg; V("rcc_flg$") = s_rcc;
    V("cutpos2$") = s_cutpos2; V("mill5$") = s_mill5; V("force_tlchg$") = s_force;
    V("probe_active$") = pa;
    for (int k = 0; k < 7; k++) V(bb[k]) = bbuf[k];      /* RestoreBufferBreak(.., 1, 1, 1) */
    for (int k = 0; k < 20; k++) if (ib >= 0) ARi(N, bdrl[k], ib + k);
    free(used);
    return ok;
}
