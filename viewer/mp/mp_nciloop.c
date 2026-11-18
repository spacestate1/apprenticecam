/* mp_nciloop.c - MP NCI read loop: ASCII NCI -> predefined variables -> predefined postblock dispatch.
 * Port of mcpost.dll (Mastercam 2017):
 *   CPostRun::RunTraditional @180112130, CPostReadNci::ReadLoop @180101990 and its helpers (CPostReadNci::rdN readers,
 *   AssignReadNci @1800fc910, call_case1 @1801091f0 / call_case2 @1801098e0 / call_case11 @180108ca0,
 *   CPostProcessLinear::LegacyLinearProcess @1800ac8a0 / LegacyLinearPostCall @1800ac1a0 / Break_XY_Z @1800abb60,
 *   CPostArc::CheckArc @180031c30 / LegacyCircularProcess @180034b80 / CircularSegment @180032bc0 /
 *   LegacyCircularPostCall @1800348e0, LinearizeArc @180036060, CPostRunNciUtil break variables (CopyToPredefined
 *   @180126650 / Backup @180125d10 / ZeroOnFirstBreak @1801295a0 / ZeroPredefined @180129940 / Restore @180128650),
 *   CPostProcessDrill::Case80/81/100 @1800f6ca0.. / drill @1800f8210 /
 *   canned @1800f7a10 / rd80_proc @1800fc140 / rd81_proc @1800fc340 / rd100_proc @1800fb020,
 *   comments: CaptureCommentParameter @1800fcc40, MarkNciCommentParameterTrigger @1801010b0,
 *   ReadLoopCommentOutput @180105140, CommentOutput @1800fd2c0, CommentOutputPostCall @1800fd7c0,
 *   ReadLoopParameterOutput @1801066a0).
 * Every rule cites its function + address. "NOT DECODED:" marks behaviour that is not ported (see the list at the end
 * of this comment); "ours:" marks plumbing that has no mcpost counterpart (data structures of this port).
 *
 * Model of mcpost's NCI buffer (ours): mcpost reads the NCI through a sliding window of strt_nci records
 * (CPostReadNci::ReadNciToBuffer @180107910 / ReadNciFromBuffer @1801073b0, current = window slot 0x19, lookahead =
 * slots above it). Comment (1005-1008, 1026, 1051-1054, 1056) and parameter (10000-31999) records never occupy a slot
 * (ReadNciData @180106df0: CaptureCommentParameter returns 1 -> record dropped and attached to the next record).
 * This port reads the whole file once into an array (same order, same per-record read-time processing), so the
 * window's lookahead becomes "the following array entries"; file positions (strt_nci +0xbcc/+0xbd0/+0xbd4) become
 * record indices.
 *
 * NOT DECODED (kept minimal, see the report): CPostRunFiveAxis (setmap/toolpl/load_matm/fiveax/parse_rev5 - tool plane
 * matrices are left as the post/NCI set them), CPostMath::find_angle (move_ang$), BreakLines for
 * 5-axis moves (gnci$ 11, BreakLinesPostCallUVW) and the breaks() function path (param_5 0/1: pbreaks_in$/out$,
 * BreakLinesOperationPostCall), long-hand drill cycles (CPostProcessDrill::peck), 5-axis drilling
 * (Setup5axisLegacyDrill), probing (everything behind PostSupportsProbe / +0x1150), the rest of the enhanced tool
 * table (CPostToolTable query functions opinfo/opchng/..., stream info, lathe canned data), lathe canned / thread
 * cycles, wire (product 3) G60/immediate taper handling, the per-level sub files of transform modes 3/4
 * (CPostSubsTraditional::FileNewSub / MergeSubFiles).
 * Ported 2026-10-23 (3rd pass): tool table pre-read (mp_nciloop_tt.c), CPostProcessLinear::BreakLines @1800aa450 for
 * LegacyLinearProcess, the CircularSegment nobrk$ 2 helix split, rd950 + psynclath$, rd1019,
 * OverrideInitializeFromPprep's sub_level$ part.
 * 4th pass: subprograms (mp_nciloop_sub.c: Case1018 / Case1019 / rd1018 / DoTransformSubs, c_msng$ / c_mmlt$),
 * CallReadOpInfo after 1020 (mp_run_prm.c), sprogname$ / smcext$ / strtoolext$. */
#include "mp_int.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------------------------------------------------------------------------------------------------------------
 * Hooks requested from the runtime (mp_run*.c). Weak so this file links before they exist; fallbacks noted.
 * ------------------------------------------------------------------------------------------------------------- */
/* CPostRunComponents +0x382: 1 start of file, 2 tool change, 3 null tool change, 4 end of file, 0 none
   (written by ReadLoop @180101990 before BlockCall(-1) and reset at the loop top when +0x384 is set). */
extern void mp_run_set_tlchg_kind(MpRun *r, int kind) __attribute__((weak));
/* CPostRunPostTable::BlockCall(-1) @180129c70: psof$/ptlchg$/ptlchg0$ (m/l variants) by +0x382. */
extern int  mp_block_call_tlchg(MpRun *r) __attribute__((weak));
/* CPostRunComponents +0x384 (byte 900): set to 1 by BlockCall @180129c70 (LAB_18012a574) after it ran the
   (possibly delayed, tlchng_aft$) tool change postblock; read + cleared by ReadLoop. */
extern int  mp_run_tlchg_done(MpRun *r) __attribute__((weak));
extern void mp_run_set_tlchg_done(MpRun *r, int v) __attribute__((weak));
/* CPostComponents::SetStandardFprmMethod (ReadLoopParameterOutput @1801066a0 around the pparameter$ call). */
extern void mp_run_set_std_fprm(MpRun *r, int on) __attribute__((weak));
/* CPostRun::OverrideInitializeFromMDandCD @1801111a0 (mp_prm overrides applied with AssignOutputVariableReInit);
   returns 1 when a control definition was applied (CPostRunComponents +0x13e0 / +0x13e1 set by
   CPostMachineInfo::InitialAssignFromCD @1800b32b0). */
extern int  mp_run_override_md_cd(MpRun *r) __attribute__((weak));

/* ---------------------------------------------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------------------------------------------- */
enum { RF_SKIP = 0x1, RF_RPD = 0x2, RF_PROBE = 0x4, RF_MOTION = 0x10, RF_BDRL = 0x100000, RF_BDRL1 = 0x200000,
       RF_BDRLEND = 0x400000, RF_COMM = 0x800000, RF_PCOMM = 0x1000000, RF_PARAM = 0x2000000 };

typedef struct {            /* strt_nci (0xc00 bytes in mcpost) - the fields this port uses */
    int g;                  /* +0x000 NCI gcode */
    char *prm;              /* +0x002 parameter line */
    double line;            /* +0xbc0 nci line number (counter of lines read, at the parameter line) */
    int pos;                /* ours: record index (stands for the file positions +0xbcc/+0xbd0/+0xbd4) */
    int end;                /* ours: 1 = the virtual record past the end (+0xbd4 == -2) */
    unsigned fl;            /* +0xbe0 flags */
    int com;                /* +0xbe8 first attached comment (index into comment list) or -1 */
    int par;                /* +0xbf8 first attached parameter or -1 */
    int raw;                /* ours: index in the raw record stream (stands for the file position +0xbd4) */
    int tl;                 /* ours: tool-list index of a tool change (PPostToolList map +0xa8), -1 */
} NRec;

typedef struct {            /* ours: one NCI record pair in file order, as CPostRunNciUtil::ReadNciGcodeParamString
                               returns it (comments / parameters included) - the stream wrtt re-reads */
    int g;
    char *prm;
    double line;
} NRaw;

typedef struct {            /* strt_nci_comm_param */
    int g;                  /* +0x00 code */
    char *text;             /* the NCI line (mcpost re-reads it at +0x14 position) */
    double line;            /* +0x08 */
    unsigned fl;            /* +0x20 flags: 0x4 probe, 0x100 commvar, 0x200 '~', 0x2000/0x3000 output, 0x4000,
                               0x8000 probe active, 0x10000 first of group, 0x20000 last of group */
} NCom;

typedef struct {
    MpRun *r;
    NRec *rec; int nrec;
    NCom *com; int ncom, ccom;      /* CPostRunComponents +0x18 comment list */
    NCom *par; int npar, cpar;      /* CPostRunComponents +0xf8 parameter list */
    int cur, next;                  /* CPostReadNci +0x24c0 (current) / +0x24c8 (next motion, -1 none) */
    /* read-time capture state (CPostReadNci +0x50 flags, +0x58 group start, +0x60 previous group, +0x90 params) */
    unsigned pend; int pend_com, prev_grp, pend_par, last_com, last_par;
    int out_com, out_end;           /* CPostReadNci +0x68 / +0x70 buffered comments (CommentOutput) */
    /* CPostRunComponents fields */
    int cont;                       /* +0x380 */
    int tlkind;                     /* +0x382 */
    int gcode;                      /* +0x38a */
    int plane;                      /* +0x38c */
    int rd_eof;                     /* +0x392 1003 read */
    int probe_on;                   /* +0x393 */
    int header_done;                /* +0x396 */
    int tl_seen;                    /* +0x39a */
    int eof_done;                   /* +0x39b */
    int posttype;                   /* +0x39e */
    int cstart, cend;               /* +0x3a0 / +0x3a4 */
    int comm_mode;                  /* +0x115c */
    unsigned comm_state;            /* +0x115e */
    int tlop_flag;                  /* +0x1161 */
    int blk, blk_rec;               /* +0x1162 / +0x1164 */
    int canceldc;                   /* +0x1166 */
    int candrl;                     /* +0x1167 */
    int arcres;                     /* +0x1168 */
    int drlcyc;                     /* +0x116a */
    double cant[20]; int ncant;     /* +0x1170.. 1025 cantext values */
    int cd_loaded;                  /* +0x13e0 */
    int eof_synth;                  /* CPostReadNci +0x24dc */
    int comm_order;                 /* CPostReadNci +0x24d8 */
    int tool_op_rd, tool_op;        /* CPostReadNci +0x24d0 (read time) / +0x24d6 (loop) */
    /* motion points (CPostRunComponents +0x1400 .. +0x1478) */
    double p0[3], pb[3], pc[3], cen[3], p1[3]; int pflag;
    double dir[3], dirp[3];         /* CPostProcessLinear +0xb8.. / +0xd0.. (dirtest) */
    /* CPostArc */
    double arc_sweep;               /* +0x18 */
    int ck_opt, ck_raw;             /* +0xc / +0x8 (SetCheckArcOptions) */
    short ck10, ck12, ck14, ck16;   /* +0x10 +0x12 +0x14 +0x16 */
    int a_plane, a_g;               /* +0xe8 / +0xea */
    int a_brk, a_helix, a_full, a_type, a_lin; /* +0xf8 breakarcs, +0xfa helix, +0xfe do_full_arc, +0xfc arctype, +0x100 linarc */
    double a_pitch, a_rad;          /* +0xf0 / +0x108 */
    double a_c[2], a_zs, a_ze;      /* +0xc8/+0xd0 center, +0xd8 start z, +0xe0 end z */
    double a_save[3], a_xn[3];      /* +0x110 / +0x128 */
    double drl_far;                 /* CPostProcessDrill +0x10 */
    int zidx;                       /* index of z$ (AssignReadNci skips slots <= var_z_) */
    NRaw *raw; int nraw, craw;      /* ours: raw record stream */
    struct {                        /* PPostToolList (CPostRunComponents +0x210), mp_nciloop_tt.c */
        double *tool; unsigned *fl; int n;   /* +0x88 entries (tool, flags), +0xe0 count */
        int e4, e8, ec, f0, f4, f9;          /* +0xe4 .. +0xf4 indices, +0xf9 first entry done */
        double *dist; int ndist;             /* +0x10 standard tool map (distinct tools) */
        int curpos;                          /* +0x8 current tool change position */
    } tl;
    int wrtt;                       /* CPostRunComponents +0x386: wrtt pre-read active */
    short tt_plane;                 /* wrtt local_1200 (plane of the previous motion) */
    short sub_mode;                 /* CPostRunComponents +0x1210 transform-subprogram mode */
    double sub_seqno;
    void *subs;                     /* SubState (mp_nciloop_sub.c): sub-level arrays and counters +0x1218..+0x13d8 */
    double tt_p2;                   /* CPostRunComponents +0x390 = force_tlchg$ when the tool table was built */
    MpFs st0;                       /* CPostComponents::FindStatement(0): the internal format statement whose decimals
                                       CPostMath::SetMtolerance @1800c1290 derives from mtol$ / met_mtol$ */
} Nci;

/* ---------------------------------------------------------------------------------------------------------------
 * Variable / string / postblock access
 * ------------------------------------------------------------------------------------------------------------- */
static double g_dummy[4];

static int vi(MpRun *r, const char *name)
{
    /* ours: name -> index cache (mcpost uses the var_* slot globals) */
    static struct { const char *p; int idx; const void *post; } cache[1024];
    unsigned h = (unsigned)(((size_t)name >> 3) & 1023);
    for (int k = 0; k < 8; k++) {
        unsigned s = (h + k) & 1023;
        if (cache[s].p == name && cache[s].post == r->p) return cache[s].idx;
        if (!cache[s].p) {
            cache[s].p = name; cache[s].post = r->p; cache[s].idx = mp_var_find(r, name);
            return cache[s].idx;
        }
    }
    return mp_var_find(r, name);
}
/* CPostMath::GetVarRef(slot, k): k 0 current, 1 previous, 2 = +0x20 */
static double *VR(MpRun *r, const char *name, int k)
{
    int i = vi(r, name);
    if (i < 0 || i >= r->nvars) { memset(g_dummy, 0, sizeof g_dummy); return &g_dummy[k & 3]; }
    return &r->vars[i].v[k];
}
#define V(name)  (*VR(N->r, name, 0))
#define VP(name) (*VR(N->r, name, 1))
static double *VRi(MpRun *r, int i, int k)
{
    if (i < 0 || i >= r->nvars) { memset(g_dummy, 0, sizeof g_dummy); return &g_dummy[k & 3]; }
    return &r->vars[i].v[k];
}

/* CPostReadNci::AssignReadNci @1800fc910: assigns only slots above var_z_ (x$/y$/z$ are never written directly) */
static void AR(Nci *N, double v, const char *name)
{
    int i = vi(N->r, name);
    if (i > N->zidx && i < N->r->nvars) N->r->vars[i].v[0] = v;
}
static void ARi(Nci *N, double v, int i)
{
    if (i > N->zidx && i < N->r->nvars) N->r->vars[i].v[0] = v;
}

static void SSET(MpRun *r, const char *name, const char *s)
{
    int i = mp_str_find(r, name);
    if (i >= 0) mp_str_set(r, i, s);
}

static int bfind(MpRun *r, const char *name) { return mp_block_find(r, name); }
/* "post has a body" test: *(postblock + 0x18) != 0 (e.g. ReadLoop 900 pstck_trans$, LegacyLinearPostCall mlin$) */
static int BDEF(MpRun *r, const char *name)
{
    int i = bfind(r, name);
    return i >= 0 && mp_block_defined(r, i);
}
/* CPostRunPostTable::BlockCall @180129c70 */
static int BC(MpRun *r, const char *name)
{
    int i = bfind(r, name);
    if (i < 0) return 1;            /* ours: postblock not registered in this table: nothing to run */
    return mp_block_call(r, i);
}
static int STATUS(MpRun *r) { return r->status; }   /* CPostRunComponents +0x388 */

static void set_tlkind(Nci *N, int k)
{
    N->tlkind = k;
    if (mp_run_set_tlchg_kind) mp_run_set_tlchg_kind(N->r, k);
}
static int tl_done(Nci *N) { return mp_run_tlchg_done ? mp_run_tlchg_done(N->r) : 0; }
static void set_tl_done(Nci *N, int v) { if (mp_run_set_tlchg_done) mp_run_set_tlchg_done(N->r, v); }
/* BlockCall(-1) */
static int BC_tlchg(Nci *N)
{
    if (mp_block_call_tlchg) return mp_block_call_tlchg(N->r);
    /* fallback (ours, until the runtime hook exists): BlockCall's psof$/ptlchg$/ptlchg0$ selection by +0x382 */
    int ok = BC(N->r, N->tlkind == 1 ? "psof$" : N->tlkind == 2 ? "ptlchg$" : "ptlchg0$");
    return ok;
}

static void fill3(MpRun *r, const char *name, int k, double o[3])
{
    int i = vi(r, name);
    for (int a = 0; a < 3; a++) o[a] = *VRi(r, i < 0 ? -1 : i + a, k);
}
static void set3(MpRun *r, const char *name, int k, const double v[3])
{
    int i = vi(r, name);
    if (i < 0) return;
    for (int a = 0; a < 3; a++) *VRi(r, i + a, k) = v[a];
}
static void copy3(MpRun *r, const char *dst, const char *src)   /* CPostMath::Copy3dPoint(dst, src, 0) */
{
    double t[3]; fill3(r, src, 0, t); set3(r, dst, 0, t);
}

/* CPostMath::post_round @1800c7460 */
static int post_round(double v)
{
    if (v < 0.0) return (short)(int)ceil((v - 0.5) - 1e-12);
    return (short)(int)floor(v + 0.5 + 1e-12);
}

/* CPostReadNci::ScaleByPostVars @1801080c0: n consecutive variables from `name` scaled by scalex$.. when non-zero */
static void scale_post(Nci *N, int n, const char *name)
{
    if (V("fastmode$") == 1.0) return;
    int s = vi(N->r, "scalex$"), d = vi(N->r, name);
    if (s < 0 || d < 0) return;
    for (int k = 0; k < n; k++) {
        double f = *VRi(N->r, s + k, 0);
        if (f != 0.0) *VRi(N->r, d + k, 0) *= f;
    }
}

/* sscanf of up to n "%lf" fields (sscanf_s in mcpost); missing fields keep `def` */
static int scan_d(const char *s, double *o, int n)
{
    int k = 0;
    const char *p = s;
    while (k < n && p && *p) {
        char *e;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        double v = strtod(p, &e);
        if (e == p) break;
        o[k++] = v;
        p = e;
    }
    return k;
}
/* assign a field table: fields[i] = variable name or NULL (field read into a local) */
static void assign_fields(Nci *N, const double *f, const char *const *names, int n)
{
    for (int i = 0; i < n; i++) if (names[i]) AR(N, f[i], names[i]);
}

/* ---------------------------------------------------------------------------------------------------------------
 * Small CPostReadNci helpers
 * ------------------------------------------------------------------------------------------------------------- */
/* CPostReadNci::NciCompToCcomp @180101230 */
static double nci_comp_to_ccomp(int cc)
{
    if (cc == 0x28) return 1.0;
    if (cc == 0x29) return 2.0;
    if (cc == 0x2a) return 3.0;
    if (cc != 0x8c) return 0.0;
    return 4.0;
}
/* CPostReadNci::CcompToCcPos @1800fd120 */
static void ccomp_to_ccpos(int c, double *pos)
{
    if (c == 2) { *pos = 1.0; return; }
    if (c == 3) { *pos = 2.0; return; }
    if (c == 1 || c == 4) *pos = 0.0;
}
/* CPostReadNci::GetFeed @1800fede0 */
static void get_feed(Nci *N, double f)
{
    if (N->posttype == 2) {
        AR(N, 1e-12 < f ? 0.0 : 1.0, "ipr_actv$");
        f = fabs(f);
    } else {
        AR(N, 0.0, "ipr_actv$");
        if (f < 0.0) return;
    }
    AR(N, f, "fr_pos$");
}
/* CPostReadNci::GetSpeed @1800ffce0 */
static void get_speed(Nci *N, double ss)
{
    if (N->posttype == 2) {
        if (ss <= -1e-12 && ss != -1e-12) { AR(N, 1.0, "css_actv$"); return; }
        AR(N, 0.0, "css_actv$");
        return;
    }
    AR(N, 0.0, "css_actv$");
    if (ss < -1e-12) { AR(N, -1.0, "spdir$"); return; }
    AR(N, 1e-12 < ss ? 1.0 : 0.0, "spdir$");
}
/* CPostReadNci::AxisSubstitution @1800fc970 */
static void axis_subst(Nci *N, double x, double y)
{
    double d = V("rotdia$");
    if (5e-05 < fabs(d)) {
        AR(N, (x * 360.0) / (d * 3.141592653589793), "xs$");
        AR(N, (y * 360.0) / (d * 3.141592653589793), "ys$");
    }
}
/* CPostReadNci::MakeContourFlags @180100c60 (lathe canned 60000 flag: NOT DECODED) */
static int make_contour_flags(Nci *N, double cflg)
{
    int f = (int)cflg;
    AR(N, cflg, "cur_cflg$");
    double v7 = V("rpd_typ_v7$");
    if (v7 == 0.0) { AR(N, (double)(f / 1000000), "rev5$"); f = f % 1000000; }
    if (f / 100000 != 0) AR(N, (double)(f / 100000) - 1.0, "coolant$");
    int rest = (f % 100000) % 10000;
    AR(N, (double)((f % 100000) / 10000), "rpd_typ$");
    double *rt = VR(N->r, "rpd_typ$", 0);
    if (v7 != 0.0) {
        if (*rt == 4.0) *rt = -1.0;
        else if (*rt == 5.0) *rt = -2.0;
    }
    /* *rt == 6 -> CPostLatheCannedTraditional::LatheCannedContourFlag60000: NOT DECODED */
    double skp = V("skp_lead_flgs$");
    int r1000 = rest % 1000;
    N->cstart = rest / 1000;
    AR(N, (double)N->cstart, "cstart$");
    if (N->cstart == 3) N->cstart = 1; else if (N->cstart == 2) N->cstart = 0;
    if (skp != 0.0) V("cstart$") = N->cstart == 0 ? 0.0 : 1.0;
    int r100 = r1000 % 100;
    N->cend = r1000 / 100;
    AR(N, (double)N->cend, "cend$");
    if (N->cend == 3) N->cend = 1; else if (N->cend == 2) N->cend = 0;
    if (skp != 0.0) V("cend$") = N->cend == 0 ? 0.0 : 1.0;
    AR(N, (double)(r100 / 10), "cgstop$");
    AR(N, (double)(r100 % 10), "cstop$");
    double *cp = VR(N->r, "cutpos2$", 0), mill5 = V("mill5$");
    if (*cp != 0.0) {
        if (*cp == 2.0 && N->cstart != 0) { N->cstart = 0; V("cstart$") = 0.0; }
        double d = *cp;
        if (d == 3.0) *cp = 4.0;
        else if (d != 1.0 || (N->cstart == 0 && mill5 <= 0.0)) { if (d < 1.0) return 1; }
        else *cp = 2.0;
        if (N->cend != 0) *cp = 3.0;
    }
    return 1;
}
/* CPostReadNci::GetControlFlagWithNext @1800fe950 */
static long ctl_flag_next(Nci *N, double v)
{
    long l = (long)v;
    if (N->posttype != 3) {
        if (V("rpd_typ_v7$") != 0.0 && 99999 < l) {
            int a = (int)((l % 100000) / 10000), b = (int)(l / 100000), s = 1;
            if (3 < a) {
                s = -1; b = 0;
                if (a == 4) a = 1; else if (a == 5) a = 2;
            }
            l = ((l % 100000) % 10000 + (a + b * 10) * 10000) * s;
        }
        if (V("skp_lead_flgs$") != 0.0) {
            int r = (int)(l % 10000);
            int t = r / 1000;
            if (t == 3 || t == 2) l -= 2000;
            t = (r % 1000) / 100;
            if (t == 3 || t == 2) return l - 200;
        }
    }
    return l;
}
/* CPostReadNci::GetCutPosition @1800feab0 */
static void get_cut_position(Nci *N)
{
    double *cp = VR(N->r, "cutpos2$", 0);
    if (V("mill5$") == 0.0 && (*cp == 0.0 || *cp == 3.0 || *cp == 4.0) &&
        ((((int)V("nextcflg$") % 10000) / 1000) & ~2) != 0) {
        if (*cp == 3.0) { *cp = 2.0; AR(N, 0.0, "cend$"); return; }
        *cp = 1.0;
    }
}
/* CPostReadNci::Zero1025 @180108bc0 */
static void zero1025(Nci *N)
{
    int p = vi(N->r, "cant_pos1$"), v = vi(N->r, "cant_val1$");
    for (int k = 0; k < 20; k++) {
        if (p >= 0) ARi(N, -1.0, p + k);
        if (v >= 0) ARi(N, 0.0, v + k);
    }
    AR(N, 0.0, "cant_no$");
}
/* CPostReadNci::Get1025 @1800fe800 */
static void get1025(Nci *N)
{
    if (V("cant_no$") == 0.0) return;
    int p = vi(N->r, "cant_pos1$"), v = vi(N->r, "cant_val1$"), n = 0;
    for (int k = 0; k < 20; k++) {
        if (N->cant[k] == 0.0) break;
        int c = (short)(int)N->cant[k];
        double pos = (double)(c / 1000);
        if (p >= 0) ARi(N, pos, p + k);
        if (v >= 0) ARi(N, (double)(c + (short)(int)pos * -1000), v + k);
        n++;
    }
    AR(N, (double)n, "cant_no$");
    AR(N, V("cant_val1$"), "cantext$");
}
/* CPostReadNci::ZeroAfterOutput @180108c50 */
static void zero_after_output(Nci *N)
{
    zero1025(N);
    AR(N, 0.0, "cantext$");
    AR(N, 0.0, "commvar$");
}
/* CPostReadNci::StartLoopSetContourFlags @180108af0 */
static void start_loop_contour_flags(Nci *N)
{
    AR(N, 0.0, "cstart$"); AR(N, 0.0, "cend$"); AR(N, 0.0, "cgstop$"); AR(N, 0.0, "cstop$");
}
/* CPostReadNci::SetBeforeOutput @1801088f0 */
static void set_before_output(Nci *N)
{
    get_cut_position(N);
    get1025(N);
    double c = nci_comp_to_ccomp((short)(int)V("cc$"));
    AR(N, (double)(int)(short)(int)c, "ccomp$");
    ccomp_to_ccpos((short)(int)c, VR(N->r, "cc_pos$", 0));
}
/* CPostReadNci::IsCommvar @180100780 */
static int is_commvar(Nci *N, const char *s, int assign)
{
    if (strrchr(s, 'd') || strrchr(s, 'D') || strrchr(s, 'e') || strrchr(s, 'E')) return 0;
    char *e;
    double d = strtod(s, &e);
    if (d == 0.0) return 0;
    if (s + strlen(s) <= e) {
        if (assign) AR(N, d, "commvar$");
        return 1;
    }
    return 0;
}
/* the text from the n-th blank separated token (1-based) on (CPostMath::FindSpaceIndex @1800bc8e0 users) */
static const char *token_from(const char *s, int n)
{
    int len = (int)strlen(s), i = 0, k = 0;
    while (i < len) {
        if (s[i] == ' ') { i++; continue; }
        int st = i;
        while (i < len && s[i] != ' ') i++;
        if (++k == n) return s + st;
    }
    return "";
}

/* CPostFileUtil::SplitPath @180078d20 + CPostRunFileUtil::FilenameToPredefinedString @18011d080 / @18011ce60
   (ours: '/' is accepted as a directory separator as well, for Linux paths) */
static void filename_to_strings(Nci *N, const char *path, const char *sp, const char *sn, const char *se, int num)
{
    char drive[2048] = "", dir[2048] = "", name[2048] = "", ext[2048] = "", rest[2048];
    snprintf(rest, sizeof rest, "%s", path ? path : "");
    char *c = strchr(rest, ':');
    if (c) {
        size_t l = (size_t)(c - rest) + 1;
        if (l < sizeof drive) { memcpy(drive, rest, l); drive[l] = 0; }
        memmove(rest, c + 1, strlen(c + 1) + 1);
    }
    char *bs = strrchr(rest, '\\'), *fs = strrchr(rest, '/');
    char *sl = bs > fs ? bs : fs;
    if (sl) {
        size_t l = (size_t)(sl - rest) + 1;
        if (l < sizeof dir) { memcpy(dir, rest, l); dir[l] = 0; }
        if (l == 1) dir[0] = 0;
        else if (dir[0] != '\\' && dir[0] != '/') { memmove(dir + 1, dir, strlen(dir) + 1); dir[0] = sl[0]; }
        memmove(rest, sl + 1, strlen(sl + 1) + 1);
    }
    char *dot = strrchr(rest, '.');
    if (!dot) snprintf(name, sizeof name, "%s", rest);
    else { snprintf(ext, sizeof ext, "%s", dot); *dot = 0; snprintf(name, sizeof name, "%s", rest); }
    if (strchr(drive, ':') && !dir[0]) snprintf(dir, sizeof dir, "\\");
    char full[4200];
    snprintf(full, sizeof full, "%s%s", drive, dir);
    if (sp) SSET(N->r, sp, full);
    if (num >= 0) {                     /* the 6-argument variant appends "_" + number to the name */
        char t[2100]; snprintf(t, sizeof t, "%s_%d", name, num);
        if (sn) SSET(N->r, sn, t);
    } else if (sn) SSET(N->r, sn, name);
    if (se) SSET(N->r, se, ext);
}

/* ---------------------------------------------------------------------------------------------------------------
 * NCI readers (CPostReadNci::rdN). Field order verified against the sscanf_s argument lists in the disassembly
 * (the decompiler drops all but the first two arguments). NULL = field read into a local.
 * ------------------------------------------------------------------------------------------------------------- */
#define NF(a) ((int)(sizeof(a) / sizeof(a[0])))

/* rd1 @18010e2b0 (mill/lathe: "%lf x6" cc x y z fr cflg; table of 5 assigned; xnci/ynci/znci = x/y/z) */
static int rd1(Nci *N, const char *p)
{
    double f[6] = {0};
    scan_d(p, f, 6);
    static const char *const nm[] = {"cc$", "x$", "y$", "z$", "fr$"};
    assign_fields(N, f, nm, 5);
    AR(N, f[1], "xnci$"); AR(N, f[2], "ynci$"); AR(N, f[3], "znci$");
    if (N->posttype != 2 && V("rotaxtyp$") < 6.0) axis_subst(N, f[1], f[2]);
    get_feed(N, f[4]);
    if (!make_contour_flags(N, f[5])) return 0;
    scale_post(N, 3, "xnci$");
    return 1;
}
/* rd2 @18010edb0 ("%lf x10" plane cc x y xc yc z fr cflg full_arc_flg; 9 table entries assigned) */
static int rd2(Nci *N, const char *p)
{
    double f[10] = {0};
    scan_d(p, f, 10);
    static const char *const nm[] = {"plane$", "cc$", "x$", "y$", "xc$", "yc$", "z$", "fr$", NULL, "full_arc_flg$"};
    assign_fields(N, f, nm, 10);
    AR(N, f[6], "zc$");
    AR(N, f[2], "xnci$"); AR(N, f[3], "ynci$"); AR(N, f[6], "znci$");
    AR(N, f[4], "xcnci$"); AR(N, f[5], "ycnci$"); AR(N, f[6], "zcnci$");
    if (N->posttype != 2 && V("rotaxtyp$") < 6.0) axis_subst(N, f[2], f[3]);
    N->plane = (short)(int)V("plane$");
    get_feed(N, f[7]);
    if (!make_contour_flags(N, f[8])) return 0;
    scale_post(N, 3, "xnci$");
    scale_post(N, 3, "xc$");
    return 1;
}
/* rd4 @18010f880 (mill: dwell ss ?) */
static void rd4(Nci *N, const char *p)
{
    double f[3] = {0};
    scan_d(p, f, 3);
    if (N->posttype == 2) {
        static const char *const nm[] = {"dwell$", "ss$", "spdir$"};
        assign_fields(N, f, nm, 3);
    } else {
        AR(N, f[0], "dwell$"); AR(N, f[1], "ss$");
    }
    get_speed(N, f[1]);
}
/* rd1000 @18010a1f0 (mill: progno seqno seqinc t tloffno tlngno plane ss fr coolant xr yr zr xh yh zh rotaxis rotdia;
   lathe: 17 fields, 15 assigned; wire: NOT DECODED) */
static void rd1000(Nci *N, const char *p, double *seqno, double *seqinc)
{
    double f[18] = {0};
    scan_d(p, f, 18);
    if (seqno) *seqno = f[1];
    if (seqinc) *seqinc = f[2];
    if (N->posttype == 2) {
        /* NOT DECODED: lathe table 0x18029c7c0 field order (progno t tloffno maxss ... per nci_fields.txt) */
        static const char *const nm[] = {"progno$", NULL, NULL, "t$", "tloffno$", "maxss$"};
        assign_fields(N, f, nm, NF(nm));
        if (V("uselstation$") != 0.0) AR(N, V("lstation$"), "t$");
        AR(N, 0.0, "plane$"); AR(N, 0.0, "rotaxis$"); AR(N, 0.0, "tlngno$");
        AR(N, 0.0, "rotary_type$"); AR(N, 0.0, "rotary_axis$");
    } else {
        static const char *const nm[] = {"progno$", NULL, NULL, "t$", "tloffno$", "tlngno$", "plane$", "ss$", "fr$",
                                         "coolant$", "xr$", "yr$", "zr$", "xh$", "yh$", "zh$", "rotaxis$", "rotdia$"};
        assign_fields(N, f, nm, 18);
        if (V("uselstation$") != 0.0) AR(N, V("lstation$"), "t$");
        AR(N, 0.0, "rotaxis_dir$");
        double ra = f[16], axis = 0.0;
        if (ra == 0.0) {
            AR(N, 0.0, "rotary_type$");
        } else {
            int a = abs((int)(short)(int)ra);
            int typ = a / 10 + 1, sub = a % 10;        /* CPostRunNciUtil::GetRotaryTypeTraditional @1801273e0 */
            double dir = 0.0, rax = 0.0;
            if (typ == 1) { dir = ra < 0.0 ? 1.0 : 0.0; rax = fabs(ra); axis = fabs((double)sub - 3.0); }
            else axis = 0.0;     /* NOT DECODED: CPostRunNciUtil::GetRotaryAxisTraditional (matrix m1$.. + rotary_vec) */
            AR(N, dir, "rotaxis_dir$");
            AR(N, rax, "rotaxis$");
            AR(N, (double)typ, "rotary_type$");
        }
        AR(N, axis, "rotary_axis$");
        AR(N, 0.0, "orient$");
        N->plane = (short)(int)V("plane$");
    }
    get_speed(N, V("ss$"));
    if (N->gcode != 1000 || N->wrtt) get_feed(N, V("fr$"));    /* || +0x386: wrtt pre-read flag */
}
/* rd1003 @18010af20 (mill: xh yh zh; then scale threadx$ for non-wire - as in the binary) */
static void rd1003(Nci *N, const char *p)
{
    double f[3] = {0};
    scan_d(p, f, 3);
    static const char *const nm[] = {"xh$", "yh$", "zh$"};
    assign_fields(N, f, nm, 3);
    scale_post(N, 3, N->posttype != 3 ? "threadx$" : "xh$");
}
static void rd_list(Nci *N, const char *p, const char *const *nm, int n)
{
    double f[24] = {0};
    scan_d(p, f, n);
    assign_fields(N, f, nm, n);
}
/* rd1011 @18010b870 (posttype re-read via GetPosttypeToolOp: NOT DECODED, kept) */
static void rd1011(Nci *N, const char *p)
{
    static const char *const nm[] = {"mr1$", "mr2$", "mr3$", "mr4$", "mr5$", "mr6$", "mr7$", "mr8$", "mr9$", "mr10$"};
    AR(N, (double)N->posttype, "posttype$");
    rd_list(N, p, nm, 10);
}
/* rd1012 @18010b990 */
static void rd1012(Nci *N, const char *p)
{
    static const char *const nm[] = {"mi1$", "mi2$", "mi3$", "mi4$", "mi5$", "mi6$", "mi7$", "mi8$", "mi9$", "mi10$"};
    rd_list(N, p, nm, 10);
}
/* rd1013 @18010ba50 (mill: cc tldia tcr cctotip tlplnno tox toy toz opcode, then the tool name from token 10) */
static void rd1013(Nci *N, const char *p)
{
    double f[9] = {0};
    scan_d(p, f, 9);
    if (N->posttype == 2) {
        static const char *const nm[] = {"cc$", "tcr$", "tldia$", "tlplnno$", "tox$", "toy$", "toz$", "opcode$"};
        assign_fields(N, f, nm, 8);
        AR(N, f[2] * 0.5, "tlrad$");
        AR(N, f[2] * 0.5 - f[1], "trflat$");
    } else {
        static const char *const nm[] = {"cc$", "tldia$", "tcr$", "cctotip$", "tlplnno$", "tox$", "toy$", "toz$",
                                         "opcode$"};
        assign_fields(N, f, nm, 9);
        AR(N, f[1] * 0.5, "tlrad$");
        AR(N, f[1] * 0.5 - f[2], "trflat$");
    }
    if (V("strtool_v7$") == 0.0) filename_to_strings(N, token_from(p, 10), "strtoolpath$", "strtool$", NULL, -1);
}
static void rd1014(Nci *N, const char *p)   /* rd1014 @18010c040 */
{
    static const char *const nm[] = {"m1$", "m2$", "m3$", "m4$", "m5$", "m6$", "m7$", "m8$", "m9$"};
    rd_list(N, p, nm, 9);
}
static void rd1015(Nci *N, const char *p)   /* rd1015 @18010c100 */
{
    static const char *const nm[] = {"subtyp$", "subno$", "td_ct_flg$", "tabcut$", "wtrim$", "skimpass$", "cutx$",
        "cuty$", "trimplane1$", "trimplane2$", "reg1$", "reg2$", "reg3$", "reg4$", "reg5$", "reg6$", "reg7$", "reg8$",
        "reg9$", "reg10$"};
    rd_list(N, p, nm, 20);
}
/* rd1016 @18010c240 (mill: 19 fields; nesting_op_id$ zeroed before the scan) */
static void rd1016(Nci *N, const char *p)
{
    static const char *const nm[] = {"op_id$", "tool_typ$", "tool_op$", "cplnno$", "corgx$", "corgy$", "corgz$",
        "cc_computer$", "workofs$", "met_tool$", "n_flutes$", "spindle_no$", "n_tap_thds$", "lstation$", "lturret$",
        "ltool_id$", "xform_op_id$", "tap_pitch$", "nesting_op_id$"};
    double f[19] = {0};
    scan_d(p, f, 19);
    assign_fields(N, f, nm, 19);
    if (V("nesting_op_id$") <= 0.0) V("nesting_op_id$") = V("op_id$");
    /* +0xe6 metric flag / CPostMath::SetMtolerance: NOT DECODED (runtime's met_tool$ handling) */
}
static void rd1017(Nci *N, const char *p)   /* rd1017 @18010c940 */
{
    static const char *const nm[] = {"cm1$", "cm2$", "cm3$", "cm4$", "cm5$", "cm6$", "cm7$", "cm8$", "cm9$"};
    rd_list(N, p, nm, 9);
}
/* rd1019 @18010d0a0 (objdump: field 1 -> local, 2..6 -> esub_grp_id$ esub_trns_id$ esub_sec_no$ esub_totl_no$
   esub_chn_no$; the local goes to esub_ref_id$ when esub_trns_id$ > 0, else to esub_op_id$, the other one = 0) */
static void rd1019(Nci *N, const char *p)
{
    static const char *const nm[] = {"esub_grp_id$", "esub_trns_id$", "esub_sec_no$", "esub_totl_no$", "esub_chn_no$"};
    double f[6] = {0};
    scan_d(p, f, 6);
    assign_fields(N, f + 1, nm, 5);
    if (f[2] <= 0.0) { AR(N, f[0], "esub_op_id$"); AR(N, 0.0, "esub_ref_id$"); }
    else { AR(N, f[0], "esub_ref_id$"); AR(N, 0.0, "esub_op_id$"); }
}
/* rd1020 @18010d190 (mill: 13 fields; sscanf_s args from objdump 18010d26e..18010d2fa: fields 1..10 -> slots
   0x18029d330.., field 11 -> a stack dummy, 12 -> force_tlchg$, 13 -> maxss$; stck_matl$ from token 14.
   Wire branch (12 fields: 1-7, 11 tank$, 12 force_tlchg$): NOT DECODED here) */
static void rd1020(Nci *N, const char *p)
{
    static const char *const nm[] = {"stck_ht$", "stck_wdth$", "stck_thck$", "stck_x$", "stck_y$", "stck_z$",
        "stck_crnr$", "rotary_vecx$", "rotary_vecy$", "rotary_vecz$", NULL, "force_tlchg$", "maxss$"};
    rd_list(N, p, nm, 13);
    SSET(N->r, "stck_matl$", token_from(p, 14));
}
/* rd1025 @18010d470 */
static void rd1025(Nci *N, const char *p)
{
    if (N->tlkind == 0 || V("cant_tlchng$") <= 0.0) {
        if (N->tlkind != 0 && V("cant_tlchng$") == -1.0) zero1025(N);
        memset(N->cant, 0, sizeof N->cant);
        scan_d(p, N->cant, 20);
    }
    AR(N, N->cant[0] != 0.0 ? -1.0 : 0.0, "cant_no$");
}
static void rd1027(Nci *N, const char *p)   /* rd1027 @18010d710 */
{
    static const char *const nm[] = {"t_wcs_m1$", "t_wcs_m2$", "t_wcs_m3$", "t_wcs_m4$", "t_wcs_m5$", "t_wcs_m6$",
        "t_wcs_m7$", "t_wcs_m8$", "t_wcs_m9$", "t_orgin_x$", "t_orgin_y$", "t_orgin_z$"};
    rd_list(N, p, nm, 12);
}
static void rd1028(Nci *N, const char *p)   /* rd1028 @18010d8a0 */
{
    static const char *const nm[] = {"ra_type$", "ra_offset$", "ra_vecx$", "ra_vecy$", "ra_vecz$", "ra_svecx$",
        "ra_svecy$", "ra_svecz$", "ra_block$", "ra_station$", "ra_head_grp$", "ra_tc_type$"};
    rd_list(N, p, nm, 12);
}
static void rd1029(Nci *N, const char *p)   /* rd1029 @18010d980 */
{
    static const char *const nm[] = {"ra_hvecx$", "ra_hvecy$", "ra_hvecz$", "ra_bvecx$", "ra_bvecy$", "ra_bvecz$",
        "ra_tvecx$", "ra_tvecy$", "ra_tvecz$", "ra_translated$", "ra_rot_head$"};
    rd_list(N, p, nm, 11);
}
static void rd1031(Nci *N, const char *p)   /* rd1031 @18010da50 */
{
    static const char *const nm[] = {"trans_mr1$", "trans_mr2$", "trans_mr3$", "trans_mr4$", "trans_mr5$",
        "trans_mr6$", "trans_mr7$", "trans_mr8$", "trans_mr9$", "trans_mr10$"};
    rd_list(N, p, nm, 10);
}
static void rd1032(Nci *N, const char *p)   /* rd1032 @18010db10 */
{
    static const char *const nm[] = {"trans_mi1$", "trans_mi2$", "trans_mi3$", "trans_mi4$", "trans_mi5$",
        "trans_mi6$", "trans_mi7$", "trans_mi8$", "trans_mi9$", "trans_mi10$"};
    rd_list(N, p, nm, 10);
}
/* rd1050 @18010dbd0 (+ smcpath$/smcname$/smcext$ from token 9: FilenameToPredefinedString @18011d080, the third
   string index str_smcext$ is the stack argument at 0x18010dcf2 that Ghidra drops) */
static void rd1050(Nci *N, const char *p)
{
    static const char *const nm[] = {"vers_no$", "m_vers_no$", "mc_day$", "mc_mon$", "mc_year$", "mc_hour$",
        "mc_min$", "mc_sec$"};
    rd_list(N, p, nm, 8);
    filename_to_strings(N, token_from(p, 9), "smcpath$", "smcname$", "smcext$", -1);
}
/* rd950 @180110760 (objdump 0x1801108c3..: fields 1..6 -> synclath$ syncbfor$ syncmode$ syncaxis$ syncstream$
   syncmodebfor$ (slots 0x18029c650..0x18029c6a0), fields 7..26 into locals) */
static void rd950(Nci *N, const char *p)
{
    static const char *const nm[] = {"synclath$", "syncbfor$", "syncmode$", "syncaxis$", "syncstream$", "syncmodebfor$"};
    rd_list(N, p, nm, 6);
}
/* rd999 @180110990 */
static void rd999(Nci *N, const char *p)
{
    static const char *const nm[] = {"synctool_op$", "syncstream$", "syncop_id$"};
    rd_list(N, p, nm, 3);
}
static void rd_simple(Nci *N, int g, const char *p)   /* rd900 .. rd912 (lathe stock / misc ops: tables only) */
{
    static const char *const n900[] = {"stck_spindle$", "stck_init_z$", "stck_final_z$", "stck_chuk_st_z$",
        "stck_chuk_st_x$", "stck_chuk_end_z$", "stck_chuk_end_x$", "stck_chuk_st_dz$", "stck_chuk_st_dx$",
        "stck_chuk_end_dz$", "stck_chuk_end_dx$"};
    static const char *const n902[] = {"stck_spindle$", "stck_op$", "stck_clear$", "stck_grip$", "stck_tool_x$",
        "stck_init_z$", "stck_final_z$", "stck_appr_fr$", "stck_adv_fr$", "stck_chuk_st_z$", "stck_chuk_st_x$",
        "stck_chuk_end_z$", "stck_chuk_end_x$"};
    static const char *const n903[] = {"clmp_spindle$", "clmp_op$", "stck_chuk_st_z$", "stck_chuk_st_x$",
        "stck_chuk_end_z$", "stck_chuk_end_x$"};
    static const char *const n904[] = {"tlstck_on$", "stck_init_z$", "stck_final_z$"};
    static const char *const n911[] = {"miscops_mr1$", "miscops_mr2$", "miscops_mr3$", "miscops_mr4$",
        "miscops_mr5$", "miscops_mr6$", "miscops_mr7$", "miscops_mr8$", "miscops_mr9$", "miscops_mr10$"};
    static const char *const n912[] = {"miscops_mi1$", "miscops_mi2$", "miscops_mi3$", "miscops_mi4$",
        "miscops_mi5$", "miscops_mi6$", "miscops_mi7$", "miscops_mi8$", "miscops_mi9$", "miscops_mi10$"};
    switch (g) {
    case 900: rd_list(N, p, n900, NF(n900)); break;
    case 901: rd_list(N, p, n900, 7); break;
    case 902: rd_list(N, p, n902, NF(n902)); break;
    case 903: rd_list(N, p, n903, NF(n903)); break;
    case 904: rd_list(N, p, n904, NF(n904)); break;
    case 911: rd_list(N, p, n911, NF(n911)); break;
    case 912: rd_list(N, p, n912, NF(n912)); break;
    default: break;   /* 905 rd905 / 950 rd950 / 200 / 201: NOT DECODED beyond nci_fields.txt */
    }
}
/* rd1_next @18010e670 / rd2_next @18010f430 / rd100_next @18010b520 / rd81_next @18010fe50 / rd1000_next @18010aca0 */
static void rd1_next(Nci *N, const char *p)
{
    double f[6] = {0};
    scan_d(p, f, 6);
    AR(N, f[1], "nextx$"); AR(N, f[2], "nexty$"); AR(N, f[3], "nextz$");
    AR(N, nci_comp_to_ccomp((short)(int)f[0]), "nextccomp$");
    AR(N, (double)ctl_flag_next(N, f[5]), "nextcflg$");
}
static void rd2_next(Nci *N, const char *p)
{
    /* mill: plane cc x y xc yc z fr ? full_arc_flg (the 9th field into a local, as rd2) */
    double f[10] = {0};
    scan_d(p, f, 10);
    AR(N, f[0], "nextplane$"); AR(N, f[2], "nextx$"); AR(N, f[3], "nexty$"); AR(N, f[4], "nextxc$");
    AR(N, f[5], "nextyc$"); AR(N, f[6], "nextz$"); AR(N, f[9], "nextfullarcflg$");
    /* NOT DECODED: rd2_next's tail (nextccomp$/nextcflg$ from the locals) */
    AR(N, nci_comp_to_ccomp((short)(int)f[1]), "nextccomp$");
    AR(N, (double)ctl_flag_next(N, f[8]), "nextcflg$");
}
static void rd100_next(Nci *N, const char *p)
{
    double f[4] = {0};
    scan_d(p, f, 4);
    AR(N, f[1], "nextx$"); AR(N, f[2], "nexty$"); AR(N, f[3], "nextz$");
}
static void rd81_next(Nci *N, const char *p, int found)
{
    double f[4] = {0};
    scan_d(p, f, 4);
    if (!found) { AR(N, f[1], "nextx$"); AR(N, f[2], "nexty$"); AR(N, f[3], "nextz$"); }
    AR(N, f[0], "nextdc$");
}

/* ---------------------------------------------------------------------------------------------------------------
 * Reading the NCI file (CPostRunNciUtil::ReadNciGcodeParamString @1801282f0 + ReadNciLine @180128470,
 * CPostReadNci::ReadNciData @180106df0, CaptureCommentParameter @1800fcc40, MarkNciCommentParameterTrigger @1801010b0)
 * ------------------------------------------------------------------------------------------------------------- */
static char *read_line(FILE *f, int *eof)
{
    /* CPostRunNciUtil::ReadNciLine: one text line, CR/LF stripped (CPostStringUtil::StripCrLf). Lines longer than the
       buffer (0x78 gcode / 0x2ee parameters) are a run error in mcpost (message 0x2711) - ours: accepted. */
    size_t cap = 256, n = 0;
    char *s = malloc(cap);
    int c;
    *eof = 0;
    while ((c = fgetc(f)) != EOF) {
        if (c == '\n') break;
        if (n + 2 >= cap) { cap *= 2; s = realloc(s, cap); }
        s[n++] = (char)c;
    }
    if (c == EOF && n == 0) { free(s); *eof = 1; return NULL; }
    while (n && (s[n - 1] == '\r')) n--;
    s[n] = 0;
    return s;
}

static int is_comment_code(int g)
{
    /* CaptureCommentParameter @1800fcc40: (g - 0x3ed) < 0x34 with mask 0xbc0000020000f:
       1005 1006 1007 1008 1026 1051 1052 1053 1054 1056 */
    unsigned u = (unsigned)(g - 0x3ed);
    return u <= 0x33 && ((0xbc0000020000fULL >> u) & 1);
}

static void push_com(NCom **a, int *n, int *c, int g, const char *t, double line, unsigned fl)
{
    if (*n == *c) { *c = *c ? *c * 2 : 64; *a = realloc(*a, (size_t)*c * sizeof **a); }
    (*a)[*n].g = g; (*a)[*n].text = strdup(t ? t : ""); (*a)[*n].line = line; (*a)[*n].fl = fl;
    (*n)++;
}

/* CaptureCommentParameter: returns 1 when the record is a comment / parameter (it does not get a slot) */
static int capture(Nci *N, int g, const char *text, double line)
{
    if ((unsigned short)(g + 0xd8f0) < 22000) {           /* 10000 .. 31999: parameters */
        if (N->probe_on) return 1;                        /* (PostSupportsProbe: NOT DECODED - no probing) */
        unsigned fl = (~(N->pend >> 9) & 0x10000);
        push_com(&N->par, &N->npar, &N->cpar, g, text, line, fl);
        if (!(N->pend & RF_PARAM)) N->pend_par = N->npar - 1;
        N->last_par = N->npar - 1;
        N->pend |= RF_PARAM;
        return 1;
    }
    if (!is_comment_code(g)) return 0;
    unsigned fl = N->probe_on ? 0x8000 : 0;
    if (g == 0x420) return 1;                             /* probe comments (1056): NOT DECODED */
    if (is_commvar(N, text, 0)) fl |= 0x100;              /* (+0x1160 == 0) */
    if (text[0] == '~' && g != 0x3ef && g != 0x402) fl |= 0x200;
    fl |= (~(N->pend >> 7) & 0x10000);
    push_com(&N->com, &N->ncom, &N->ccom, g, text, line, fl);
    if (!(N->pend & RF_COMM)) N->pend_com = N->ncom - 1;
    N->last_com = N->ncom - 1;
    N->pend |= RF_COMM;
    return 1;
}

/* MarkNciCommentParameterTrigger(rec, 0) */
static void mark_trigger(Nci *N, NRec *rc)
{
    unsigned u = (unsigned)(rc->g - 0x3f3);
    if (N->pend != 0 && 0x31 < (unsigned short)(rc->g - 0x3b6) && (0x15 < u || ((0x3fc1ffU >> (u & 0x1f)) & 1) == 0)) {
        rc->fl |= N->pend;
        if (N->pend & RF_COMM) {
            N->com[N->last_com].fl |= 0x20000;            /* last comment of the group */
            rc->com = N->pend_com;
            N->prev_grp = N->pend_com;
        }
        if (N->pend & RF_PARAM) {
            N->par[N->last_par].fl |= 0x20000;
            rc->par = N->pend_par;
        }
        N->pend = 0;
    }
}

static void add_rec(Nci *N, int g, char *prm, double line, int end)
{
    N->rec = realloc(N->rec, (size_t)(N->nrec + 1) * sizeof *N->rec);
    NRec *rc = &N->rec[N->nrec];
    memset(rc, 0, sizeof *rc);
    rc->g = g; rc->prm = prm; rc->line = line; rc->pos = N->nrec; rc->end = end; rc->com = -1; rc->par = -1; rc->raw = N->nraw - 1; rc->tl = -1;
    N->nrec++;
}

/* ReadNciData's per-record processing (in file order, as ReadNciToBuffer reads ahead) */
static void read_all(Nci *N, FILE *f)
{
    double line = 0;
    int eof;
    for (;;) {
        char *gl = read_line(f, &eof);
        if (eof) break;
        line += 1.0;
        int g = -1;
        if (sscanf(gl, "%d", &g) != 1) { free(gl); continue; }   /* sscanf_s "%hd" */
        free(gl);
        char *pl = read_line(f, &eof);
        if (eof) break;
        line += 1.0;
        if (N->nraw == N->craw) { N->craw = N->craw ? N->craw * 2 : 256; N->raw = realloc(N->raw, (size_t)N->craw * sizeof *N->raw); }
        N->raw[N->nraw].g = g; N->raw[N->nraw].prm = strdup(pl); N->raw[N->nraw].line = line; N->nraw++;
        if (capture(N, g, pl, line)) { free(pl); continue; }
        add_rec(N, g, pl, line, 0);
        NRec *rc = &N->rec[N->nrec - 1];
        unsigned short ug = (unsigned short)g;
        if (ug < 4 || ug == 0xb) rc->fl = RF_MOTION;
        if (N->probe_on) rc->fl |= RF_PROBE;
        if ((unsigned short)(ug - 0x410) < 4) {           /* 1040..1043 probe markers */
            rc->fl |= RF_SKIP;
            if (ug == 0x412) N->probe_on = 1;
            else if (ug == 0x413) N->probe_on = 0;
            continue;
        }
        if (g == 999) {
            int op = 0; sscanf(pl, "%d", &op); N->tool_op_rd = op;   /* rd999_tool_op @180110a60 */
        } else if (999 < g && g < 0x3eb) {
            unsigned short t = (unsigned short)N->tool_op_rd;
            if (((unsigned short)(t - 2) < 0x3f && ((0x60e0000004000001ULL >> ((t - 2) & 0x3f)) & 1)) ||
                t == 0x68 || t == 0x88 || t == 0x132)
                N->tlop_flag = 1;
            /* CPostToolTable::UpdateToolChangeFromTooltable: NOT DECODED */
        } else if (g == 0x3eb) N->rd_eof = 1;
        if (N->tlop_flag && (rc->fl & RF_MOTION)) { rc->fl |= RF_RPD; N->tlop_flag = 0; }
        if (g == 0x53) {                                  /* block drilling */
            if (N->blk && N->blk_rec >= 0) N->rec[N->blk_rec].fl |= RF_BDRLEND;
            N->blk = 1; N->blk_rec = -1;
        }
        if (N->blk) {
            if (g == 0x51 || g == 100) {
                rc->fl |= RF_BDRL;
                if (N->blk_rec == -1) rc->fl |= RF_BDRL1;
                N->blk_rec = N->nrec - 1;
            } else if ((rc->fl & RF_MOTION) || g == 0x50) {
                if (N->blk_rec >= 0) N->rec[N->blk_rec].fl |= RF_BDRLEND;
                N->blk = 0; N->blk_rec = -1;
            }
        }
        mark_trigger(N, rc);
    }
    add_rec(N, -1, strdup(""), line, 1);                 /* past the end: +0xbd4 == -2 */
    N->rec[N->nrec - 1].raw = N->nraw;
    if (N->pend) {                                        /* comments after the last record ride on it */
        NRec *rc = &N->rec[N->nrec - 1];
        rc->g = 0x3eb;
        mark_trigger(N, rc);
        rc->g = -1;
    }
}

/* ---------------------------------------------------------------------------------------------------------------
 * Comments / parameters output
 * ------------------------------------------------------------------------------------------------------------- */
/* CPostComponents::UnAssembleCommentFilter @180052190: comm_filter$ decimal digits 10^9 .. 10^0 = "filtered" flags
   of 1056 1054 1053 1052 1051 1026 1008 1007 1006 1005 (the order CommentOutput @1800fd2c0 tests them). */
static int comm_filtered(Nci *N, int g)
{
    long v = (long)V("comm_filter$");
    static const int code[10] = {0x420, 0x41e, 0x41d, 0x41c, 0x41b, 0x402, 0x3f0, 0x3ef, 0x3ee, 0x3ed};
    long div = 1000000000L;
    for (int k = 0; k < 10; k++, div /= 10)
        if (code[k] == g) return ((v / div) % 10) != 0;
    return 0;
}

/* CPostReadNci::CommentOutputPostCall @1800fd7c0 */
static int comment_post_call(Nci *N, NCom *c)
{
    char buf[1024];
    double sg = V("gcode$"), sl = V("nci_line$");
    snprintf(buf, sizeof buf, "%s", c->text);
    unsigned u = (unsigned)(c->g - 0x3ed);
    if (u < 0x32 && ((0x3c00000000009ULL >> u) & 1)) {   /* 1005 1008 1051..1054: limited to maxnccomment$ */
        double mx = V("maxnccomment$");
        if (mx < (double)strlen(buf) && mx >= 0) buf[(int)mx] = 0;
    }
    AR(N, (double)c->g, "gcode$");
    SSET(N->r, "scomm$", buf);
    AR(N, (double)strlen(buf), "commlng$");
    AR(N, c->line, "nci_line$");
    int ok = 1;
    if (c->fl & 0x200) {                                  /* "~nnn~text": skip the leading numbers */
        int len = (int)strlen(buf), i = 1;
        char *s = buf + 1, *e;
        while (i < len) {
            strtol(s, &e, 10);
            if (e <= s) break;
            i = (int)(e - buf);
            if (buf[i] != '~') break;
            i++; s = buf + i;
        }
        if (len > 1) {
            if (!buf[i]) { SSET(N->r, "scomm$", ""); goto done; }
            SSET(N->r, "scomm$", buf + i);
            AR(N, (double)strlen(buf + i), "commlng$");
        }
    }
    ok = BC(N->r, (c->fl & 4) ? "pcomment_probe$" : "pcomment$");
done:
    SSET(N->r, "scomm$", "");
    V("gcode$") = sg;
    *VR(N->r, "nci_line$", 2) = sl;
    return ok;
}

/* CommentOutput's filter: comm_filter$ plus 1026 / 1007 whenever prv_comm_call$ != -1007 */
static int co_filtered(Nci *N, int g)
{
    if ((g == 0x402 || g == 0x3ef) && VP("comm_call$") != -1007.0) return 1;
    return comm_filtered(N, g);
}
/* CPostReadNci::CommentOutput @1800fd2c0 (param = 1 from the comment$ command variable) */
static int comment_output(Nci *N, int param)
{
    /* CommentBlockCallFilter @1800fd1f0 */
    if (!((param || N->comm_mode < 2) && (N->out_com >= 0 || N->comm_mode == 3))) return 1;
    if (V("comm_cnt$") <= 0.0) return 1;
    int any = 0, okall = 1, cnt = 0;
    if (N->out_com >= 0) {
        V("comm_call$") += 1.0;
        if (N->comm_mode == 3) {
            /* range mode (comm_st_nci$ .. comm_end_nci$ over the whole list) */
            for (int i = 0; i < N->ncom; i++) {
                NCom *c = &N->com[i];
                if (V("comm_st_nci$") <= c->line && c->line <= V("comm_end_nci$") && !co_filtered(N, c->g) &&
                    !(c->fl & 0x100) && !(c->fl & 0x4000)) {
                    if (!comment_post_call(N, c)) { okall = 0; break; }
                    any = 1;
                }
                if (V("comm_end_nci$") <= c->line) break;
            }
        } else {
            for (int i = N->out_com; i < N->ncom; i++) {
                NCom *c = &N->com[i];
                if (V("comm_cnt$") < (double)cnt) break;
                if (!co_filtered(N, c->g) && !(c->fl & 0x100) && !(c->fl & 0x4000)) {
                    if (!comment_post_call(N, c)) { okall = 0; break; }
                    cnt++; any = 1;
                }
                if (i == N->out_end) break;
            }
        }
    }
    N->out_com = -1;
    V("comm_cnt$") = 0.0;
    *VR(N->r, "comm_cnt$", 2) = 0.0;
    if (!okall) return 0;
    return (short)(any + 1);
}

/* CPostReadNci::CommentBufferCallOnPath @1800fd250 (+ CommentsBufferCallOnPath @1800fe3b0; probe part: NOT DECODED) */
static int comments_on_path(Nci *N)
{
    if (N->comm_mode != 0 && N->tlkind == 0) {            /* CommentBufferCallOnPathFilter @1800fd2a0 */
        N->comm_state |= 4;
        if (comment_output(N, 0) == 0) return 0;
        N->comm_state &= ~4u;
    }
    return 1;
}

/* CPostReadNci::ReadLoopCommentFilter @180105110 */
static int rl_comment_filter(Nci *N, int g)
{
    return (N->tl_seen == 0 || N->comm_mode != 0) && g != 0x3ef && g != 0x402;
}

/* CPostReadNci::ReadLoopCommentOutput @180105140 */
static int rl_comment_output(Nci *N, NRec *rc)
{
    if (rc->com < 0 || !(rc->fl & RF_COMM)) return 1;
    int first = 1, ok = 1;
    if (N->out_com < 0) {
        N->out_com = rc->com;
        V("comm_cnt$") = 0.0;
        *VR(N->r, "comm_cnt$", 2) = 0.0;
    }
    double sg = V("gcode$"), sl = V("nci_line$");
    for (int i = rc->com; i < N->ncom; i++) {
        NCom *c = &N->com[i];
        AR(N, (double)c->g, "gnci$");
        AR(N, (double)c->g, "gcode$");
        N->gcode = c->g;
        AR(N, c->line, "nci_line$");
        if (!(c->fl & 0x100)) {
            N->comm_state |= 4;
            SSET(N->r, "scomm0$", c->text);
            AR(N, (double)strlen(c->text), "commlng$");
            if (!BC(N->r, "pscomm0$")) { if (4 < STATUS(N->r)) { ok = 0; break; } goto next; }
            N->comm_state &= ~4u;
        } else is_commvar(N, c->text, 1);
        if (c->fl & 0x200) {                              /* "~sync~" codes -> psynclath$ */
            const char *s = c->text + 1;
            char *e;
            int len = (int)strlen(c->text);
            if (len > 1) {
                for (;;) {
                    long v = strtol(s, &e, 10);
                    if (e <= s) break;
                    AR(N, (double)(v / 1000000), "syncbfor$");
                    int m = (int)((v % 1000000) % 100000);
                    AR(N, (double)((v % 1000000) / 100000), "syncaxis$");
                    AR(N, (double)(m / 1000), "syncmode$");
                    AR(N, (double)(m % 1000), "synclath$");
                    if (!BC(N->r, "psynclath$")) { if (4 < STATUS(N->r)) { ok = 0; } goto out; }
                    int k = (int)(e - c->text);
                    if (c->text[k] != '~' || len <= k + 1) break;
                    s = c->text + k + 1;
                }
            }
        }
        if (c->g != 0x3ef && c->g != 0x402) {
            if (first) { AR(N, c->line, "comm_st_nci$"); first = 0; }
            AR(N, c->line, "comm_end_nci$");
        }
        if (!comm_filtered(N, c->g)) {
            if (!(c->fl & 0x100)) {
                SSET(N->r, "scomm$", c->text);
                AR(N, (double)strlen(c->text), "commlng$");
            } else {
                SSET(N->r, "scomm$", "");
                AR(N, 0.0, "commlng$");
            }
            if (!rl_comment_filter(N, c->g) && !(c->fl & 0x100)) {
                if (c->g == 0x3ef || c->g == 0x402) N->comm_state |= 1; else N->comm_state |= 4;
                if (!BC(N->r, "pcomment$")) { if (4 < STATUS(N->r)) { ok = 0; break; } goto next; }
                if (!(N->comm_state & 1)) N->comm_state &= ~4u;
                else { N->comm_state |= 2; N->comm_state &= ~1u; }
            } else {
                N->comm_state |= 4;
                if (!BC(N->r, "pcomment0$")) { if (4 < STATUS(N->r)) { ok = 0; break; } goto next; }
                V("comm_cnt$") += 1.0;
                N->comm_state &= ~4u;
            }
        }
        if (N->comm_state == 0) {
            SSET(N->r, "scomm0$", ""); SSET(N->r, "scomm$", ""); AR(N, 0.0, "commlng$");
        }
        *VR(N->r, "comm_cnt$", 2) += 1.0;
    next:
        if (c->fl & 0x20000) { N->out_end = i; break; }
    }
out:
    if (N->comm_state == 0) {
        SSET(N->r, "scomm0$", ""); SSET(N->r, "scomm$", ""); AR(N, 0.0, "commlng$");
    }
    V("gcode$") = sg;
    *VR(N->r, "nci_line$", 2) = sl;
    return ok;
}

/* CPostReadNci::ReadLoopParameterOutput @1801066a0 */
static int rl_param_output(Nci *N, NRec *rc)
{
    if (rc->par < 0 || !(rc->fl & RF_PARAM) || (N->par[rc->par].fl & 0x2000)) return 1;
    double sg = V("gcode$"), sl = V("nci_line$"), pr = V("param_read$");
    int ok = 1;
    for (int i = rc->par; i < N->npar; i++) {
        NCom *c = &N->par[i];
        c->fl |= 0x3000;
        if (pr == 3.0 || 19999 < c->g) {
            AR(N, (double)c->g, "prmcode$");
            AR(N, (double)c->g, "gcode$");
            AR(N, (double)c->g, "gnci$");
            N->gcode = c->g;
            SSET(N->r, "sparameter$", c->text);
            AR(N, c->line, "nci_line$");
            if (V("strtool_v7$") != 0.0 && N->gcode == 0x4e21) {     /* 20001: tool name */
                /* FilenameToPredefinedString @18011d080 with str_strtoolext$ (stack argument, objdump 0x180106b3b) */
                if (V("strtool_v7$") == 1.0) filename_to_strings(N, c->text, "strtoolpath$", "strtool$", "strtoolext$", -1);
                else SSET(N->r, "strtool$", c->text);
            }
            const char *blk = "pparameter$";
            if (mp_run_set_std_fprm) mp_run_set_std_fprm(N->r, 1);
            if (!BDEF(N->r, "pparameter$")) {
                if (mp_run_set_std_fprm) mp_run_set_std_fprm(N->r, 0);
                blk = "pnci20000$";
            }
            if (!BC(N->r, blk)) {
                if (4 < STATUS(N->r)) { ok = 0; break; }
            } else {
                if (mp_run_set_std_fprm) mp_run_set_std_fprm(N->r, 0);
                if (c->fl & 0x20000) break;
            }
        }
        if (c->fl & 0x20000) break;
    }
    V("gcode$") = sg;
    *VR(N->r, "nci_line$", 2) = sl;
    return ok;
}

/* CPostReadNci::ReadLoopCommentsParametersOutput @180106510 (probe comments: NOT DECODED) */
static int rl_comments_params(Nci *N, NRec *rc)
{
    switch (N->comm_order) {
    case 1: case 2: case 4:
        if (N->comm_order == 4) { if (!rl_param_output(N, rc)) return 0; return rl_comment_output(N, rc); }
        if (!rl_comment_output(N, rc)) return 0;
        return rl_param_output(N, rc);
    case 3: case 5:
        if (!rl_param_output(N, rc)) return 0;
        return rl_comment_output(N, rc);
    default:
        if (!rl_comment_output(N, rc)) return 0;
        return rl_param_output(N, rc);
    }
}

/* ---------------------------------------------------------------------------------------------------------------
 * Lookahead (CPostReadNci::GetNextNextNci @1800ff720, ScanForNextMove @1801081c0, GetNextMove @1800fefb0)
 * ------------------------------------------------------------------------------------------------------------- */
static int get_next_next(Nci *N, int from)
{
    for (int i = from; i < N->nrec; i++) {
        NRec *rc = &N->rec[i];
        if (rc->end) return -1;
        if (rc->fl & RF_SKIP) continue;            /* ReadContinue @1801017c0: skipped records */
        if (rc->g == 0x3f3) return -1;             /* stops at 1011 */
        if (rc->fl & RF_MOTION) return i;
    }
    return -1;
}

/* CPostReadNci::CheckForDrillScan @1800fd160 */
static int check_drill_scan(Nci *N)
{
    double op = V("opcode$");
    return V("nextdc$") == -1.0 && (op == 3.0 || op == 16.0 || op == 105.0);
}

static void get_next_move(Nci *N, int *found, int *drlscan, int k, int idx)
{
    NRec *rc = &N->rec[idx];
    int g = rc->g;
    if (0x3ea < g) {
        if (g == 0x3eb) { AR(N, 0.0, "nextwt$"); AR(N, 0.0, "nextwt_immd$"); *found = 1; return; }
        if (g == 0x3ec) { if (V("get_1004$") == 0.0) return; *found = 1; return; }
        if (g == 0x3f3) { *found = 1; return; }
        /* 1013: rd1013_next (NOT DECODED) */
        return;
    }
    if (999 < g) {
        *drlscan = check_drill_scan(N);
        /* rd1000_next: values read into locals only */
        *found = 1;
        return;
    }
    switch (g) {
    case 0:
        *drlscan = check_drill_scan(N);
        /* fallthrough */
    case 1: case 0x14: case 0x15:
        if (!*found) rd1_next(N, rc->prm);
        break;
    case 2: case 3: case 0x16: case 0x17:
        if (!*found) rd2_next(N, rc->prm);
        break;
    case 0xb:
        break;                                     /* rd11_next: NOT DECODED (5-axis) */
    case 0x51:
        rd81_next(N, rc->prm, *found);
        *found = 1; *drlscan = 0;
        return;
    case 0x53:
        return;
    case 100:
        if (!*found) rd100_next(N, rc->prm);
        break;
    case 0xc9:
        break;
    default:
        return;
    }
    (void)k;
    *found = 1;
}

static void scan_for_next_move(Nci *N, int tlchg, int getnextop)
{
    int found = 0, drlscan = 0;
    if (!getnextop) {
        if (N->next < 0) { V("nextcflg$") = 0.0; return; }
        V("nextop$") = (double)N->rec[N->next].g;
        get_next_move(N, &found, &drlscan, 1, N->next);
        return;
    }
    V("nextcflg$") = 0.0; V("nextop$") = -1.0; V("nextdc$") = -1.0; V("nextplane$") = 0.0; V("nextccomp$") = 0.0;
    V("nextx$") = 9999.0; V("nexty$") = 9999.0; V("nextz$") = 9999.0; V("nextxc$") = 9999.0; V("nextyc$") = 9999.0;
    V("nextu$") = 9999.0; V("nextv$") = 9999.0; V("nextw$") = 9999.0; V("nextwc$") = -1.0;
    if (V("immediate$") != 0.0) V("immediate$") = 1.0;
    (void)tlchg;
    for (int k = 1; ; k++) {
        int idx = N->cur + k;
        if (idx >= N->nrec || N->rec[idx].end) break;
        if (!found) V("nextop$") = (double)N->rec[idx].g;
        get_next_move(N, &found, &drlscan, k, idx);
        if (found && N->posttype != 3 /* +0x3e2 == 0 */ && N->rec[idx - 1].line == N->rec[idx].line) found = 0;
        if (k == 1) {
            static const char *const nx[] = {"nextop$", "nextccomp$", "nextdc$", "nextcflg$", "nextx$", "nexty$",
                                              "nextz$", "nextxc$", "nextyc$", "nextwc$", "nextwt$"};
            for (int a = 0; a < NF(nx); a++) VP(nx[a]) = V(nx[a]);
        } else if (VP("nextop$") == 80.0) {
            VP("nextop$") = V("nextop$");
            V("nextop$") = 80.0;
        }
        if (found && !drlscan) break;
    }
}

/* ---------------------------------------------------------------------------------------------------------------
 * Math helpers (CPostMath)
 * ------------------------------------------------------------------------------------------------------------- */
/* CPostMath::SetMtolerance(char metric) @1800c1290: statement 0 decimals from the first significant digit of the
   fraction of mtol$ (met_mtol$): '1' -> n+1 decimals (round 2 when the next digit > '5'), '2' -> n+1 round 2,
   '4'/'5' -> n+1 round 5, else n; trunc_mtol$ -> round 9 (n = -_fcvt decimal point). Both unit slots are set. */
static void set_mtolerance(Nci *N, int metric)
{
    double *t = VR(N->r, metric ? "met_mtol$" : "mtol$", 0);
    double f = *t - floor(fabs(*t));
    if (f < 1e-12) { f = metric ? 0.001 : 0.0001; *t = f; }
    char buf[64];
    snprintf(buf, sizeof buf, "%.8f", f);          /* _fcvt(f, 8) digits (RoundFcvtBuffer) */
    const char *d = strchr(buf, '.');
    d = d ? d + 1 : buf;
    int n = 0;
    while (d[n] == '0') n++;
    char c1 = d[n], c2 = d[n] ? d[n + 1] : '0';
    int dd = n, rd = 0;
    if (c1 == '1') { dd = n + 1; if ('5' < c2) rd = 2; }
    else if (c1 == '2') { dd = n + 1; rd = 2; }
    else if (c1 == '4' || c1 == '5') { dd = n + 1; rd = 5; }
    if (V("trunc_mtol$") != 0.0) rd = 9;
    memset(&N->st0, 0, sizeof N->st0);
    N->st0.defined = 1;
    N->st0.idig[0] = N->st0.idig[1] = 5;
    N->st0.ddig[0] = N->st0.ddig[1] = dd;
    N->st0.round[0] = N->st0.round[1] = rd;
    N->st0.flags = MP_FS_PT_IN | MP_FS_PT_MM;
}
/* CPostMath::FormattedNumber(value, FindStatement(0), metric, 1) */
static double fmtn(Nci *N, double v)
{
    MpFmtCtx cx = {V("round_opt$")};
    return mp_fmt_round(&N->st0, V("met_tool$") != 0.0, v, &cx);
}
/* CPostMath::mul_mat_vector2 @1800c6a10: out[i] = row i . v */
static void mul_mat_vec(const double *m, const double *v, double *o)
{
    double t[3];
    for (int i = 0; i < 3; i++) t[i] = m[i * 3] * v[0] + m[i * 3 + 1] * v[1] + m[i * 3 + 2] * v[2];
    memcpy(o, t, sizeof t);
}
static void fill_matrix(Nci *N, const char *first, double m[9])
{
    int i = vi(N->r, first);
    for (int k = 0; k < 9; k++) m[k] = *VRi(N->r, i < 0 ? -1 : i + k, 0);
}
/* CPostMath::MapTop @1800bfb30 */
static void map_top(Nci *N, const char *src, int k, const char *dst)
{
    double p[3], m[9];
    fill3(N->r, src, k, p);
    fill_matrix(N, "matm1$", m);
    mul_mat_vec(m, p, p);
    set3(N->r, dst, 0, p);
}
/* CPostMath::tpmap @1800c8750 (only rotaxtyp$ 2 / 4). n = 3: the three variables from `src`; n = -3: one value
   (`src`) mapped as the z component of the point `base` (initht$/refht$/depth$ users) */
static void tpmap(Nci *N, const char *src, const char *base, int n)
{
    double rt = V("rotaxtyp$");
    if (rt != 2.0 && rt != 4.0) return;
    double p[3], m[9], o[3], ofs[3];
    int si = vi(N->r, src);
    if (n == 3) fill3(N->r, src, 0, p);
    else {
        int bi = vi(N->r, base), sel = abs(n) - 1;
        for (int a = 0; a < 3; a++) p[a] = a == sel ? *VRi(N->r, si, 0) : *VRi(N->r, bi < 0 ? -1 : bi + a, 0);
    }
    fill_matrix(N, "matm1$", m);
    mul_mat_vec(m, p, o);
    fill3(N->r, "vtlofsx$", 0, ofs);
    for (int a = 0; a < 3; a++) o[a] += ofs[a];
    if (rt == 4.0) o[2] -= V("vtlngz$");
    if (n == 3) set3(N->r, src, 0, o);
    else *VRi(N->r, si, 0) = o[abs(n) - 1];
}
/* CPostMath::addofs @1800c2150 */
static void addofs(Nci *N, int centers)
{
    if (V("rotaxtyp$") != 2.0) return;
    V("xnci$") += V("vox$"); V("ynci$") += V("voy$"); V("znci$") += V("voz$");
    if (centers) { V("xc$") += V("vox$"); V("yc$") += V("voy$"); V("zc$") += V("voz$"); }
}
/* CPostMath::GetVarRefTolerance @1800bef40 (metric flag = met_tool$) */
static double tol(Nci *N, int which)
{
    static const char *const in[] = {"mtol$", "chord_tol$", "vert_tol$", "ltol$", "atol$", "max_atol$", "arc_tol$",
                                     "helix_tol$", "minarc$", "minrad$", "maxrad$", "xtol$"};
    static const char *const me[] = {"met_mtol$", "met_chord_tol$", "met_vert_tol$", "met_ltol$", "atol$",
                                     "max_atol$", "met_arc_tol$", "met_helix_tol$", "met_minarc$", "met_minrad$",
                                     "met_maxrad$", "met_xtol$"};
    if (which < 0 || which > 11) which = 0;
    return V(V("met_tool$") != 0.0 ? me[which] : in[which]);
}
/* CPostMath::fsg3 @1800c5070 */
static double fsg3(Nci *N, double v)
{
    double t = V("xtol$");
    if (t <= fabs(v)) return v >= 0.0 ? 2.0 : 0.0;
    return 1.0;
}
/* CPostMath::DeltaChange @1800bb7f0 (formatted end - formatted start; statement at CPostComponents +0xe8 taken as
   statement 0: NOT DECODED whether they differ) */
static void delta_change(Nci *N, const double a[3], const double b[3], double d[3])
{
    for (int k = 0; k < 3; k++) d[k] = fmtn(N, b[k]) - fmtn(N, a[k]);
}
/* CPostMath::restrict_angle @1800c7f70 */
static void restrict_angle(double lo, double hi, double *a)
{
    while (hi < *a) *a -= 6.283185307179586;
    while (*a < lo) *a += 6.283185307179586;
}
/* CPostMath::GetVarRefArcs @1800bd810: selector -> (mill / lathe) variable by arc plane */
static double arcs_var(Nci *N, int sel, int plane)
{
    int l = N->posttype == 2;
    static const char *const t1[3][2] = {{"do_xy_arcs$", "ldo_xy_arcs$"}, {"do_yz_arcs$", "ldo_yz_arcs$"},
                                         {"do_xz_arcs$", "ldo_xz_arcs$"}};
    static const char *const t3[3][2] = {{"breakarcs$", "lbreakarcs$"}, {"breakarcsyz$", "lbreakarcsyz$"},
                                         {"breakarcsxz$", "lbreakarcsxz$"}};
    static const char *const t4[3][2] = {{"do_full_arc$", "ldo_full_arc$"}, {"do_full_arcyz$", "ldo_full_arcyz$"},
                                         {"do_full_arcxz$", "ldo_full_arcxz$"}};
    static const char *const t6[3][2] = {{"arctype$", "larctype$"}, {"arctypeyz$", "larctypeyz$"},
                                         {"arctypexz$", "larctypexz$"}};
    int p = plane >= 0 && plane <= 2 ? plane : 0;
    switch (sel) {
    case 0: return V(l ? "llinarc$" : "linarc$");
    case 1: return V(t1[p][l]);
    case 3: return V(t3[p][l]);
    case 4: return V(t4[p][l]);
    case 5: return V(l ? "lhelix_arc$" : "helix_arc$");
    case 6: return V(t6[p][l]);
    case 14: return V(l ? "larccheck$" : "arccheck$");
    default: return 0.0;
    }
}
/* arc plane <-> view (CPostMath::MapToArcPlane @1800bfab0 / MapFromArcPlane @1800bf620) */
static void map_to_arc(int plane, const double *a, double *o)
{
    static const double yz[9] = {0, 1, 0, 0, 0, 1, 1, 0, 0}, xz[9] = {1, 0, 0, 0, 0, 1, 0, -1, 0};
    if (plane == 1) mul_mat_vec(yz, a, o);
    else if (plane == 2) mul_mat_vec(xz, a, o);
    else if (o != a) memcpy(o, a, 3 * sizeof *o);
}
static void map_from_arc(int plane, const double *a, double *o)
{
    static const double yz[9] = {0, 0, 1, 1, 0, 0, 0, 1, 0}, xz[9] = {1, 0, 0, 0, 0, -1, 0, 1, 0};
    if (plane == 1) mul_mat_vec(yz, a, o);
    else if (plane == 2) mul_mat_vec(xz, a, o);
    else if (o != a) memcpy(o, a, 3 * sizeof *o);
}
/* CPostMath::s_MapX/Y/ZfromArcPln (0x1802bc774): variable offsets of the arc-plane x/y/z components */
static const int MAPX[3] = {0, 1, 0}, MAPY[3] = {0, 1, 1}, MAPZ[3] = {0, -2, -1};
static double *VOFF(Nci *N, const char *name, int off, int k)
{
    int i = vi(N->r, name);
    return VRi(N->r, i < 0 ? -1 : i + off, k);
}

/* ---------------------------------------------------------------------------------------------------------------
 * Linear moves (CPostProcessLinear)
 * ------------------------------------------------------------------------------------------------------------- */
/* FUN_180031bc0 (CPostRunComponents +0x1400 bookkeeping) */
static void last_point_update(Nci *N)
{
    if (!(fabs(N->pc[0] - N->p1[0]) <= 5e-8 && fabs(N->pc[1] - N->p1[1]) <= 5e-8 && fabs(N->pc[2] - N->p1[2]) <= 5e-8))
        memcpy(N->pb, N->pc, sizeof N->pc);
    memcpy(N->pc, N->p1, sizeof N->pc);
}
/* the "+0x1478 == 0 -> previous point = current point" step before reading a new point (call_case1/2/11, rd81_proc) */
static void point_advance(Nci *N)
{
    if (N->pflag == 0) memcpy(N->p0, N->p1, sizeof N->p0);
    fill3(N->r, "xnci$", 0, N->p1);
    N->pflag = 0;
}
/* CPostProcessLinear::MotionTest @1800ad2b0 (wire part: NOT DECODED) */
static int motion_test(Nci *N, const double d[3])
{
    double g = V("gcode$"), t = tol(N, 0);
    double *m = VR(N->r, "motst$", 0);
    *m = 0.0;
    *VR(N->r, "motst$", 2) = 0.0;
    if ((g == 2.0 || g == 3.0) && V("full_arc_flg$") != 0.0) *m = 3.0;
    else {
        if (t - 1e-12 <= fabs(d[0])) *m = 1.0;
        if (t - 1e-12 <= fabs(d[1])) *m += 2.0;
    }
    if (t - 1e-12 <= fabs(d[2])) *m += 4.0;
    if (*m != 0.0) return 1;
    if (V("bug9$") == 1.0 && V("tool_op$") == 56.0 && V("c$") != VP("c$")) return 1;
    return 0;
}
/* CPostProcessLinear::dirtest @1800ae400 */
static void dirtest(Nci *N, const double d[3])
{
    for (int k = 0; k < 3; k++) if (1e-12 < fabs(d[k])) N->dir[k] = fsg3(N, d[k]) - 1.0;
    int ch = 0;
    for (int k = 0; k < 3; k++) { if (1e-06 < fabs(N->dir[k] - N->dirp[k])) ch = 1; N->dirp[k] = N->dir[k]; }
    V("dirchg$") = (double)ch;
}
/* m/l/p selection used by LegacyLinearPostCall @1800ac1a0 / LinearPostCall @1800acd30 / LegacyCircularPostCall */
static int call_mlp(Nci *N, const char *m, const char *l, const char *p)
{
    if (N->posttype < 2 && m && BDEF(N->r, m)) return BC(N->r, m);
    if (N->posttype == 2 && l && BDEF(N->r, l)) return BC(N->r, l);
    return BC(N->r, p);
}
/* CPostProcessLinear::LegacyLinearPostCall @1800ac1a0 (param zblock = "pz$ allowed") */
static int legacy_linear_post_call(Nci *N, const double d[3], int zblock)
{
    double g = V("gcode$"), rt = V("rotaxtyp$"), skip = V("skipmotest$"), whatno = V("whatno$"),
           whatline = V("whatline$"), motst;
    int moved = motion_test(N, d);
    last_point_update(N);
    if (rt == 4.0 && !BC(N->r, "ptplane$")) return 0;
    if (!moved && N->tlkind == 0 && skip != 1.0 && (skip != -1.0 || V("mill5$") == 0.0)) return 1;
    dirtest(N, d);
    /* CPostMath::FindMoveAngle @1800bc310 -> find_angle: NOT DECODED (move_ang$, move_ang3$ not updated) */
    motst = V("motst$");
    int z = N->posttype < 2 && motst == 4.0 && zblock;
    if (whatno == 1.0 || 6.0 <= rt) {
        if (g == 1.0 || g == 11.0) {
            if (z && BDEF(N->r, "pz$")) return BC(N->r, "pz$");
            return call_mlp(N, "mlin$", "llin$", "plin$");
        }
        if (z && BDEF(N->r, "pzrapid$")) return BC(N->r, "pzrapid$");
        return call_mlp(N, "mrapid$", "lrapid$", "prapid$");
    }
    /* legacy whatline$ selection (whatno$ == 0) */
    int zz = N->posttype < 2 && motst == 4.0 && zblock;
    int wl = post_round(whatline);
    const char *b;
    if (whatno == 0.0) {
        if (wl == 1) b = zz ? "pz1$" : "plin1$";
        else if (wl == 2) b = zz ? "pz$" : "plin2$";
        else if (wl == 3) b = zz ? "pz$" : (N->posttype == 3 ? "plin3$" : "plin$");
        else if (g == 0.0) b = zz ? "pzrapid$" : "prapid$";
        else b = zz ? "pz$" : "plin$";
    } else if (g == 0.0) b = zz ? "pzrapid$" : "prapid$";
    else {
        if (wl == 1) b = zz ? "pz1$" : "plin1$";
        else if (wl == 2) b = zz ? "pz$" : "plin2$";
        else if (wl == 3) b = zz ? "pz$" : (N->posttype == 3 ? "plin3$" : "plin$");
        else b = zz ? "pz$" : "plin$";
    }
    return BC(N->r, b) ? 1 : 0;
}
/* CPostProcessLinear::Break_XY_Z @1800abb60 (break-variable backup: NOT DECODED) */
static int break_xy_z(Nci *N, const double pt[3], double d[3])
{
    double t = tol(N, 0);
    double xn = pt[0], yn = pt[1], zn = pt[2];
    int zb = V("z_pstblock$") != 1.0;
    if (t <= fabs(d[0]) || t <= fabs(d[1])) {
        if (V("bug9$") != 6.0) V("nextop$") = V("gcode$");
        double sx = d[0], sy = d[1], sz = d[2];
        if (0.0 <= d[2]) {                                /* Z up first, then XY */
            V("x$") = VP("x$"); V("y$") = VP("y$"); V("z$") = zn;
            d[0] = 0.0; d[1] = 0.0;
            if (!legacy_linear_post_call(N, d, zb)) return 0;
            V("x$") = xn; V("y$") = yn;
            d[0] = sx; d[1] = sy; d[2] = 0.0;
            if (!legacy_linear_post_call(N, d, zb)) return 0;
        } else {                                          /* XY first, then Z down */
            V("x$") = xn; V("y$") = yn; V("z$") = VP("z$");
            d[2] = 0.0;
            if (!legacy_linear_post_call(N, d, zb)) return 0;
            V("z$") = zn;
            d[0] = 0.0; d[1] = 0.0; d[2] = sz;
            if (!legacy_linear_post_call(N, d, zb)) return 0;
        }
        VP("xnci$") = xn; VP("ynci$") = yn; VP("znci$") = zn;
        return 1;
    }
    V("x$") = xn; V("y$") = yn; V("z$") = zn;
    if (!legacy_linear_post_call(N, d, zb)) return 0;
    VP("xnci$") = xn; VP("ynci$") = yn; VP("znci$") = zn;
    return 1;
}
static void copy_to_break_vars(Nci *N);
static void backup_break_vars(Nci *N, double *b);
static void zero_on_first_break(Nci *N);
static void zero_break_vars(Nci *N, double *b);
static void restore_break_vars(Nci *N, const double *b);
/* CPostProcessLinear::BreakLinesPostCall @1800ab720 (posttype < 5) */
static int break_lines_post_call(Nci *N, const double p[3], double d[3])
{
    if (5e-05 < fabs(d[2]) && V("nobrk$") == 2.0 && N->tlkind == 0) return break_xy_z(N, p, d);
    V("x$") = p[0]; V("y$") = p[1]; V("z$") = p[2];
    return legacy_linear_post_call(N, d, 0);
}
/* CPostBreakNci::ToCenterAxis @18003cea0: 1 when |end x start| (formatted, the rotary axis component zeroed:
   type 4 x, 5 y, 6 z) <= 5e-8, i.e. the move lies in a plane through the axis - no breaking */
static int to_center_axis(Nci *N, int type)
{
    double a[3] = {fmtn(N, V("xnci$")), fmtn(N, V("ynci$")), fmtn(N, V("znci$"))};
    double b[3] = {fmtn(N, VP("xnci$")), fmtn(N, VP("ynci$")), fmtn(N, VP("znci$"))};
    if (type >= 4 && type <= 6) { a[type - 4] = 0.0; b[type - 4] = 0.0; }
    double c0 = b[2] * a[1] - b[1] * a[2];
    double c1 = b[0] * a[2] - b[2] * a[0];
    double c2 = b[1] * a[0] - b[0] * a[1];
    return fabs(sqrt(c0 * c0 + c1 * c1 + c2 * c2)) <= 5e-08;
}
/* CPostProcessLinear::BreakLines @1800aa450 for LegacyLinearProcess (param_5 = -1; the breaks() function path
   param_5 0/1 with pbreaks_in$ / pbreaks_out$ / BreakLinesOperationPostCall: NOT DECODED). end = xnci$ (param_1),
   d = end - prv x$ (param_2). Control flow checked against the disassembly (0x1800aaab4: types 1..7 take the chord
   loop, 4..6 after ToCenterAxis; types -1 / 8..11 the even split, which ends with the final call). */
static int break_lines(Nci *N, const double end[3], double d[3])
{
    double brk[21] = {0};
    copy_to_break_vars(N);
    backup_break_vars(N, brk);
    zero_on_first_break(N);
    double type = V("brklinestype$");
    double len = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), dir[3] = {0, 0, 0};
    if (len != 0.0) for (int k = 0; k < 3; k++) dir[k] = d[k] / len;
    double p[3];
    fill3(N->r, "x$", 1, p);
    int ok = 1, split = 0, chord = 0;
    if (type == -1.0) split = 1;
    else {
        double t0 = tol(N, 0);
        if (t0 <= len) {
            double pl;
            if (type == 1.0 || type == 8.0) pl = sqrt(d[2] * d[2] + d[1] * d[1]);
            else if (type == 2.0 || type == 9.0) pl = sqrt(d[2] * d[2] + d[0] * d[0]);
            else if (type == 3.0 || type == 10.0) pl = sqrt(d[0] * d[0] + d[1] * d[1]);
            else pl = t0;                                   /* other types: straight to LAB_1800aaab4 */
            if (t0 <= pl) {
                if (0.0 < type && type < 8.0) chord = !(3.0 < type && type < 7.0 && to_center_axis(N, (int)type));
                else split = 1;
            }
        }
    }
    if (split) {
        int n;
        double step;
        if (type == -1.0) {                                 /* brklineslen$ = number of pieces */
            n = (short)(int)fabs(V("brklineslen$"));
            if (n < 1) n = 1;
            step = len / (double)n;
        } else {                                            /* brklineslen$ = piece length */
            double bl = fabs(V("brklineslen$"));
            n = post_round(len / bl);
            if (n < 1) n = 1;
            if (bl * 0.1 < len - (double)n * bl) n++;
            step = len / (double)n;
        }
        double sd[3] = {dir[0] * step, dir[1] * step, dir[2] * step};
        for (int i = 1; i < n; i++) {
            if (i == 2) zero_break_vars(N, brk);
            for (int k = 0; k < 3; k++) p[k] += sd[k];
            if (V("getnextop$") != 0.0) {
                V("nextx$") = p[0] + sd[0]; V("nexty$") = p[1] + sd[1]; V("nextz$") = p[2] + sd[2];
            }
            double dd[3] = {sd[0], sd[1], sd[2]};
            if (!break_lines_post_call(N, p, dd)) { restore_break_vars(N, brk); return 0; }   /* (LAB_1800aa754) */
        }
        delta_change(N, end, p, d);                         /* DeltaChange(param_1, point, param_2) */
    } else if (chord) {
        double t1 = tol(N, 1);
        for (int k = 0;;) {
            double rem = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            double r = sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            if (r < 0.001) r = 0.001;
            double L = sqrt(t1 * 8.0 * r);
            if (rem < L) break;
            if (rem < L * 1.9) L = rem * 0.5;
            if (++k == 2) zero_break_vars(N, brk);
            double sd[3] = {dir[0] * L, dir[1] * L, dir[2] * L};
            for (int a = 0; a < 3; a++) p[a] += sd[a];
            if (V("getnextop$") != 0.0) {
                double rd[3];
                delta_change(N, end, p, rd);
                double rl = sqrt(rd[0] * rd[0] + rd[1] * rd[1] + rd[2] * rd[2]);
                double xe = sqrt(V("xnci$") * V("xnci$") + V("ynci$") * V("ynci$") + V("znci$") * V("znci$"));
                if (xe < 0.001) xe = 0.001;
                double L2 = sqrt(t1 * 8.0 * xe);
                if (rl < L2 * 1.9) L2 = rl * 0.5;
                for (int a = 0; a < 3; a++) sd[a] = dir[a] * L2;     /* (the step handed to the post call is this one) */
                V("nextx$") = p[0] + sd[0]; V("nexty$") = p[1] + sd[1]; V("nextz$") = p[2] + sd[2];
            }
            if (!break_lines_post_call(N, p, sd)) break;
            delta_change(N, end, p, d);
        }
    }
    /* LAB_1800ab3c6 */
    restore_break_vars(N, brk);
    if (!break_lines_post_call(N, end, d)) return 0;
    VP("xnci$") = V("xnci$"); VP("ynci$") = V("ynci$"); VP("znci$") = V("znci$");
    return ok;
}
/* CPostProcessLinear::LegacyLinearProcess @1800ac8a0 */
static int legacy_linear_process(Nci *N)
{
    double *wl = VR(N->r, "whatline$", 0);
    if (*wl < 4.0) *wl += 1.0;
    double a[3], b[3], d[3];
    fill3(N->r, "xnci$", 0, a);
    fill3(N->r, "x$", 1, b);
    delta_change(N, b, a, d);
    double bl = V("brklinestype$");
    if (bl < -1.0 || 11.0 < bl) { V("brklinestype$") = 0.0; bl = 0.0; }   /* FixBreakLines @1800ac060 */
    double g = V("gcode$");
    if (bl != 0.0 && (g == 1.0 || (g == 11.0 && V("fr$") != -2.0)) && V("uvflag$") == 0.0 && V("arcrad$") == 0.0 &&
        (bl != -1.0 || (V("gnci$") == 11.0 && VP("gnci$") == 11.0))) {
        if (V("gnci$") != 11.0) return break_lines(N, a, d);
        /* NOT DECODED: BreakLines for 5-axis moves (gnci$ 11: BreakLinesPostCallUVW @1800ab8b0, u/v/w) - unbroken */
    }
    if (N->posttype != 3 && V("rotaxtyp$") < 6.0 && V("arcrad$") == 0.0) {
        V("x4$") = V("xnci$"); V("y4$") = V("ynci$"); V("z4$") = V("znci$");
    }
    double nobrk = V("nobrk$");
    if (N->posttype < 2 && d[2] != 0.0 && N->tlkind == 0 && ((nobrk == 0.0 && N->gcode == 0) || nobrk == 2.0) &&
        V("drill5$") == 0.0)
        return break_xy_z(N, a, d);
    V("x$") = a[0]; V("y$") = a[1]; V("z$") = a[2];
    if (!legacy_linear_post_call(N, d, 0)) return 0;
    VP("xnci$") = a[0]; VP("ynci$") = a[1]; VP("znci$") = a[2];
    return 1;
}

/* CPostReadNci::call_case1 @1801091f0 (mode 0: read the record, 1: values already set, 3: only the end part) */
static int call_case1(Nci *N, int mode)
{
    if (mode == 0) {
        if (!comments_on_path(N)) return 0;
        if (!rd1(N, N->rec[N->cur].prm)) return 0;
    }
    if (mode != 3) {
        point_advance(N);
        set_before_output(N);
        tpmap(N, "xnci$", NULL, 3);
        map_top(N, "x$", 1, "x4$");
        addofs(N, 0);
        if (!BC(N->r, "plin0$")) return 0;
        if (!legacy_linear_process(N)) return 0;
    }
    zero_after_output(N);
    if (N->tlkind == 0 && !BC(N->r, "plin_end$")) return 0;
    return 1;
}

/* ---------------------------------------------------------------------------------------------------------------
 * Arcs (CPostArc)
 * ------------------------------------------------------------------------------------------------------------- */
/* CPostArc::SetCheckArcOptions @180036820 */
static void set_check_arc_options(Nci *N)
{
    int v = (int)arcs_var(N, 14, N->plane);
    N->ck_raw = v;
    N->ck16 = (short)(v / 10000); v -= N->ck16 * 10000;
    N->ck14 = (short)(v / 1000); v -= N->ck14 * 1000;
    N->ck12 = (short)(v / 100); v -= N->ck12 * 100;
    N->ck10 = (short)(v / 10); v -= N->ck10 * 10;
    N->ck_opt = ((v - 1) & ~2) == 0;   /* units digit 1 or 3 */
}
/* CPostArc::CheckArc @180031c30: returns 0 line, 2 linearize, 4 arcs off in the plane, 5 end point adjusted,
   7 arc. sweep (deg) -> N->arc_sweep. (arccheck$ rotation test: ported; minrad/maxrad as in the binary) */
static int check_arc(Nci *N, const double ps[3], const double pe[3], const double pc[3], int plane, int g,
                     double full, double padj[3], int setopt)
{
    if (V("plot_2d_uv$") < 0.0) return 7;
    double ltol = tol(N, 3), maxrad = tol(N, 10), minarc = tol(N, 8), arctol = tol(N, 6), atol = tol(N, 4),
           minrad = tol(N, 9);
    double s[3], e[3], c[3];
    map_to_arc(plane, ps, s);
    memcpy(e, pe, sizeof e);
    memcpy(c, pc, sizeof c);
    double a1 = atan2(s[1] - c[1], s[0] - c[0]), a2 = atan2(e[1] - c[1], e[0] - c[0]);
    restrict_angle(0.0, 6.283185307179586, &a1);
    restrict_angle(0.0, 6.283185307179586, &a2);
    double rr = sqrt((c[0] - e[0]) * (c[0] - e[0]) + (c[1] - e[1]) * (c[1] - e[1]));
    double eps = 1.0 <= rr ? 5e-8 : 1e-7;     /* 0x3e6ad7f29abcaf48 / 0x3e7ad7f29abcaf48 */
    double sw = plane == 2 ? a2 - a1 : a1 - a2, sw2;
    if (g == 2) { if (sw < eps) sw += 6.283185307179586; }
    else if (eps < sw) sw -= 6.283185307179586;
    if (full == 0.0) {
        sw2 = (6.283185307179586 <= sw || sw <= -6.283185307179586) ? 0.0 : sw;
    } else if (fabs(sw) <= eps || 6.283185307179586 - fabs(sw) <= eps) {
        sw2 = g != 2 ? -6.283185307179586 : 6.283185307179586;
    } else {
        full = 0.0; V("full_arc_flg$") = 0.0;      /* (message 0x2711 / 0x40000: full circle flag on a partial arc) */
        sw2 = sw;
    }
    N->arc_sweep = -(sw2 * 57.29577951308233);
    if (arcs_var(N, 1, plane) == 0.0) return 4;
    if (setopt) set_check_arc_options(N);
    for (int k = 0; k < 3; k++) { s[k] = fmtn(N, s[k]); e[k] = fmtn(N, e[k]); c[k] = fmtn(N, c[k]); }
    if (N->ck_raw == 0) {
        if (full == 0.0 && sqrt((e[0] - s[0]) * (e[0] - s[0]) + (e[1] - s[1]) * (e[1] - s[1])) < ltol &&
            fabs(sw2) < 3.141592653589793)
            return 0;
        return 7;
    }
    double sx = c[0] - s[0], sy = c[1] - s[1], ex = c[0] - e[0], ey = c[1] - e[1];
    double swr = sw2;
    if (full == 0.0) {
        a1 = atan2(sy, sx); a2 = atan2(ey, ex);
        restrict_angle(0.0, 6.283185307179586, &a1);
        restrict_angle(0.0, 6.283185307179586, &a2);
        swr = plane == 2 ? a2 - a1 : a1 - a2;
        if (g == 2) { if (swr < eps) swr += 6.283185307179586; }
        else if (eps < swr) swr -= 6.283185307179586;
        if (6.283185307179586 <= swr || swr <= -6.283185307179586 || fabs(swr) <= eps) { N->arc_sweep = 0.0; return 0; }
    }
    double rad = sqrt((c[0] - s[0]) * (c[0] - s[0]) + (c[1] - s[1]) * (c[1] - s[1]));
    double len = fabs(swr) * rad;
    int res = 0;
    if (maxrad != 0.0 && maxrad < rad) goto fail;
    if (N->ck_opt == 1 && len < minarc) return 2;
    /* full circle: straight to 7 (LAB_180032a4d), no rotation / atol / minrad tests */
    if (full != 0.0) return 7;
    /* arccheck$ tens digit 1 (this+0x10): an arc whose end points lie on one axis line through the center
       (start or end straight above/below the center and both at the same y, or both left/right of it at the same
       x) fails the check (falls through to LAB_1800325d0 -> 0 / 2) */
    if (N->ck10 == 1) {
        if (fabs(sx) < 1e-12 || fabs(ex) < 1e-12) {
            if (!(1e-12 <= fabs(s[1] - e[1]))) goto fail;
        } else if (!((1e-12 <= fabs(sy) && 1e-12 <= fabs(ey)) || 1e-12 <= fabs(s[0] - e[0]))) goto fail;
    }
    /* LAB_180032606 */
    if ((unsigned short)(N->ck12 - 1) < 3) {
        /* rotate the start point about the center by the sweep and compare with the end point
           (CPostMath::rotate_pt @1800c8010: out = c + (d.x cos - d.y sin, d.y cos + d.x sin)) */
        double a = plane != 2 ? -swr : swr, cs = cos(a), sn = sin(a);
        double rx = c[0] + (s[0] - c[0]) * cs - (s[1] - c[1]) * sn;
        double ry = c[1] + (s[1] - c[1]) * cs + (s[0] - c[0]) * sn;
        if (N->ck12 < 3) {
            rx = fmtn(N, rx); ry = fmtn(N, ry);
            if (!(fabs(rx - e[0]) < arctol + 1e-12 && fabs(ry - e[1]) < arctol + 1e-12)) {
                if (N->ck12 != 2) goto fail;
                res = 5;
                e[0] = rx; e[1] = ry;
                ex = c[0] - rx; ey = c[1] - ry;
            }
        } else if (arctol + 1e-12 <= sqrt((e[1] - ry) * (e[1] - ry) + (e[0] - rx) * (e[0] - rx))) return 2;
    }
    if (N->ck14 == 1) {
        /* radius direction consistency (atol in degrees) */
        double b1 = (1e-12 <= sy * sy + sx * sx) ? atan2(sy, sx) : 0.0;
        if (b1 < 0.0) b1 += 6.283185307179586;
        double vx = e[0] - s[0], vy = e[1] - s[1];
        double b2 = (1e-12 <= vy * vy + vx * vx) ? atan2(vy, vx) : 0.0;
        if (b2 < 0.0) b2 += 6.283185307179586;
        double d1 = b2 - b1;
        if (d1 < 0.0 && 1e-12 < fabs(d1)) d1 += 6.283185307179586;
        double c1 = (1e-12 <= ex * ex + ey * ey) ? atan2(ey, ex) : 0.0;
        if (c1 < 0.0) c1 += 6.283185307179586;
        double wx = s[0] - e[0], wy = s[1] - e[1];
        double c2 = (1e-12 <= wy * wy + wx * wx) ? atan2(wy, wx) : 0.0;
        if (c2 < 0.0) c2 += 6.283185307179586;
        double d2 = c1 - c2;
        if (d2 < 0.0 && 1e-12 < fabs(d2)) d2 += 6.283185307179586;
        if (atol * 0.01745329251994329 < fabs(d1 - d2)) goto fail;
    }
    if (N->ck16 != 1 || minrad - 1e-12 < fmtn(N, rad)) {
        if (res == 5) { map_from_arc(plane, e, padj); return 5; }
        return 7;
    }
fail:
    return len < ltol ? 0 : 2;
}

/* ---- CPostRunNciUtil break variables (strt_run_break = CPostArc +0x20, 0xa8 bytes: 21 doubles, slot 12 (+0x60)
   unused) ---- */
static const char *const BRK_SRC[21] = {"cant_no$", "cantext$", "commvar$", "cc$", "ccomp$", "cc_pos$", "nextop$",
                                        "nextccomp$", "nextcflg$", "cstop$", "cgstop$", "cend$", NULL, "thrd_cut$",
                                        "rpd_typ$", "coolant$", "water$", "power$", "tank$", "epac_flg$", "cutpos2$"};
static const char *const BRK_DST[21] = {NULL, "brk_cantext$", "brk_commvar$", "brk_cc$", "brk_ccomp$", "brk_cc_pos$",
                                        "brk_nextop$", "brk_nextccomp$", "brk_nextcflg$", "brk_cstop$", "brk_cgstop$",
                                        "brk_cend$", NULL, "brk_thrd_cut$", "brk_rpd_typ$", "brk_coolant$",
                                        "brk_water$", "brk_power$", "brk_tank$", "brk_epac_flg$", "brk_cutpos2$"};
/* CPostMath::SetVarValue @1800c17f0 (k 0: current value, skipped when +0x68 bit 0x4 read only) */
static void set_var_value(Nci *N, const char *name, double v)
{
    int i = vi(N->r, name);
    if (i < 0 || i >= N->r->nvars || (N->r->vars[i].f68 & 0x4)) return;
    N->r->vars[i].v[0] = v;
}
/* CPostRunNciUtil::CopyToPredefinedBreakVariables @180126650 (CPostMath::CopyVarValue @1800bb600, k 0) */
static void copy_to_break_vars(Nci *N)
{
    for (int k = 1; k < 21; k++) if (BRK_DST[k]) set_var_value(N, BRK_DST[k], V(BRK_SRC[k]));
}
/* CPostRunNciUtil::BackupBreakVariables @180125d10 */
static void backup_break_vars(Nci *N, double *b)
{
    for (int k = 0; k < 21; k++) if (BRK_SRC[k]) b[k] = V(BRK_SRC[k]);
}
/* CPostRunNciUtil::ZeroOnFirstBreak @1801295a0 */
static void zero_on_first_break(Nci *N)
{
    set_var_value(N, "cantext$", 0.0); set_var_value(N, "commvar$", 0.0);
    set_var_value(N, "cc$", 0.0); set_var_value(N, "ccomp$", 0.0);
    V("cc_pos$") = VP("cc_pos$");
    set_var_value(N, "nextop$", V("gcode$"));
    set_var_value(N, "nextccomp$", V("ccomp$"));
    set_var_value(N, "nextcflg$", 0.0); set_var_value(N, "cstop$", 0.0); set_var_value(N, "cgstop$", 0.0);
    set_var_value(N, "cend$", 0.0); set_var_value(N, "rpd_typ$", 0.0); set_var_value(N, "thrd_cut$", 0.0);
    V("coolant$") = VP("coolant$"); V("water$") = VP("water$"); V("power$") = VP("power$");
    V("tank$") = VP("tank$"); V("epac_flg$") = VP("epac_flg$");
    double *c = VR(N->r, "cutpos2$", 0), pc = VP("cutpos2$");
    if (*c == 1.0 || pc == 2.0) *c = pc;
    if (*c == 3.0 && pc == 1.0) *c = 2.0;
}
/* CPostRunNciUtil::ZeroPredefinedBreakVariables @180129940 */
static void zero_break_vars(Nci *N, double *b)
{
    for (int k = 1; k < 21; k++) if (BRK_DST[k]) set_var_value(N, BRK_DST[k], k == 6 ? -1.0 : 0.0);
    if (VP("cutpos2$") == 1.0) { V("cutpos2$") = 2.0; b[20] = 2.0; }
    set_var_value(N, "cstart$", 0.0);
}
/* CPostRunNciUtil::RestoreBreakVariables @180128650 */
static void restore_break_vars(Nci *N, const double *b)
{
    if (b[0] != 0.0) { set_var_value(N, "cant_no$", b[0]); get1025(N); }
    for (int k = 1; k < 21; k++) if (BRK_SRC[k]) set_var_value(N, BRK_SRC[k], b[k]);
}

/* CPostArc::GetStartEndSweep @180034530 */
static void get_start_end_sweep(Nci *N, double sweep, double *sa, double *ea, int full)
{
    int dup = 0;
    if ((0.0 < sweep && N->a_plane != 2) || (sweep < 0.0 && N->a_plane == 2)) {
        restrict_angle(0.0, 6.283185307179586, sa);
        if (*sa == 6.283185307179586) *sa = 0.0;
        while (*ea <= *sa) { if (fabs(*ea - *sa) <= 1e-06) dup = 1; *ea += 6.283185307179586; }
        if (!full && dup && 4.712388980384691 < *ea - *sa) *ea -= 6.283185307179586;
    } else {
        restrict_angle(0.0, 6.283185307179586, ea);
        if (fabs(*ea - 6.283185307179586) < 1e-12) *ea = 0.0;
        while (*sa <= *ea) { if (fabs(*ea - *sa) <= 1e-06) dup = 1; *sa += 6.283185307179586; }
        if (!full && dup && 4.712388980384691 < *sa - *ea) *sa -= 6.283185307179586;
    }
    double d = N->a_plane == 2 ? *sa - *ea : *ea - *sa;
    V("sweep$") = d * 57.29577951308233;
    V("sa$") = *sa * 57.29577951308233;
    V("ea$") = *ea * 57.29577951308233;
}
/* CPostArc::CalculateNewArcData @180031ac0 */
static void calc_new_arc_data(Nci *N, double sa, double ea, double zs, double ze, double sa0, double sw, double *out,
                              int last)
{
    *out = N->a_plane == 2 ? sa - ea : ea - sa;
    N->a_ze = last ? ze : ((sa0 - ea) / sw) * (zs - ze) + zs;
    N->a_ze = fmtn(N, N->a_ze);
}
/* CPostArc::HelixCheck @1800347a0 */
static int helix_check(Nci *N, double zs, double ze, int helix, double htol)
{
    if (1 < N->posttype) return 0;
    double d = zs - ze;
    int r = 0;
    if (helix == 0) { if (d != 0.0) r = 1; }
    else if (helix == 2 && d != 0.0 && N->a_plane != 0) r = 1;
    else if (helix >= 1 && !(d == 0.0 || htol - 1e-12 <= fabs(d))) r = 1;
    if (fabs(d) < 5e-08) r = 2;
    return r;
}
/* CPostArc::GetIJKarctype @180033db0 (arctype$ >= 5: the arctype variable's previous value) */
static int ijk_arctype(Nci *N, int t)
{
    if (t < 5) return t;
    static const char *const m[3] = {"arctype$", "arctypeyz$", "arctypexz$"};
    static const char *const l[3] = {"larctype$", "larctypeyz$", "larctypexz$"};
    if (N->plane < 0 || N->plane > 2) return t;
    return (short)(int)VP(N->posttype == 2 ? l[N->plane] : m[N->plane]);
}
/* CPostArc::LinearizeArc @180036060: the arc (seg = 0: the whole arc, end = the NCI end point +0x128; seg = 1: the
   current CircularSegment piece) as chord_tol$ linear moves through LegacyLinearProcess. Uses the arc data of
   LegacyCircularProcess: +0x108 radius, +0xc8/+0xd0 center, +0xd8/+0xe0 start/end z, sa$ / sweep$ (degrees).
   (radius <= 0: run message 0x2711 / 0x40 - messages not ported) */
static int legacy_linear_process(Nci *N);
static int linearize_arc(Nci *N, int seg)
{
    double sweep = V("sweep$"), ang = V("sa$") * 0.01745329251994329, rad = N->a_rad;
    double ctol = tol(N, 1);
    int n = post_round((rad * 6.283185307179586) / sqrt(ctol * 8.0 * rad));
    n = post_round(((double)n * sweep) / 360.0);
    n = (short)(int)fabs((double)n) + 1;
    if (n < 1) n = 1;
    double step = sweep / (double)n;
    step *= N->a_plane == 2 ? -0.01745329251994329 : 0.01745329251994329;
    double dz = N->posttype < 2 ? (N->a_ze - N->a_zs) / (double)n : 0.0;
    double brk[21] = {0};
    copy_to_break_vars(N);
    backup_break_vars(N, brk);
    zero_on_first_break(N);
    N->gcode = 1;
    V("gcode$") = 1.0;
    int p = N->a_plane;
    for (int k = 1; k <= n; k++) {
        ang += step;
        if (!seg && k == n) {
            V("xnci$") = N->a_xn[0]; V("ynci$") = N->a_xn[1]; V("znci$") = N->a_xn[2];
        } else {
            *VOFF(N, "xnci$", MAPX[p], 0) = cos(ang) * rad + N->a_c[0];
            *VOFF(N, "ynci$", MAPY[p], 0) = sin(ang) * rad + N->a_c[1];
            *VOFF(N, "znci$", MAPZ[p], 0) = (double)k * dz + N->a_zs;
        }
        if (V("getnextop$") != 0.0) {
            *VOFF(N, "nextx$", MAPX[p], 0) = cos(step + ang) * rad + N->a_c[0];
            *VOFF(N, "nexty$", MAPY[p], 0) = sin(step + ang) * rad + N->a_c[1];
            *VOFF(N, "nextz$", MAPZ[p], 0) = (double)(k + 1) * dz + N->a_zs;
            V("nextop$") = 1.0;
        }
        if (k == 2) zero_break_vars(N, brk);
        if (k == n) restore_break_vars(N, brk);
        if (V("rotaxtyp$") < 6.0) {
            if (V("rotdia$") != 0.0 && N->posttype < 2) axis_subst(N, V("xnci$"), V("ynci$"));
            tpmap(N, "xnci$", NULL, 3);
        }
        if (!legacy_linear_process(N)) return 0;
    }
    return 1;
}
/* CPostArc::LegacyCircularPostCall @1800348e0 */
static int legacy_circular_post_call(Nci *N, const double d[3])
{
    int moved = motion_test(N, d);
    last_point_update(N);
    if (!moved && V("skipmotest$") == 0.0 && N->tlkind == 0) return 1;
    dirtest(N, d);
    /* FindMoveAngle: NOT DECODED */
    if (V("whatno$") == 1.0 || 6.0 <= V("rotaxtyp$")) return call_mlp(N, "mcir$", "lcir$", "pcir$");
    int w = post_round(V("whatline$"));
    const char *b = w == 1 ? "pcir1$" : w == 2 ? "pcir2$" : (w == 3 && N->posttype == 3) ? "pcir3$" : "pcir$";
    return BC(N->r, b);
}
/* CPostArc::CircularSegment @180032bc0 (s/e relative to the center in the arc plane; final = last segment) */
static int circular_segment(Nci *N, const double s[3], const double e[3], int last, const double adj[3])
{
    double cx = N->a_c[0], cy = N->a_c[1];
    double sx = cx + s[0], sy = cy + s[1], ex = cx + e[0], ey = cy + e[1];
    double g0 = V("gcode$");
    int savg = N->gcode;
    int r = N->arcres;
    double ex0 = ex, ey0 = ey;
    if (0 < N->a_brk) {
        double ps[3] = {sx, sy, 0}, pe[3] = {ex, ey, 0}, pc[3] = {cx, cy, 0}, pa[3];
        r = check_arc(N, ps, pe, pc, 0, N->a_g, V("full_arc_flg$"), pa, 0);
        if (N->a_plane == 2) N->arc_sweep = -N->arc_sweep;
        if (N->posttype < 2 && 4 < r) {
            V("arc_pitch$") = N->a_pitch;
            int h = helix_check(N, N->a_zs, N->a_ze, N->a_helix, tol(N, 7));
            if (h == 1) r = 2;
            else if (h == 2) V("arc_pitch$") = 0.0;
        }
        if (r == 5) { ex = pa[0]; ey = pa[1]; }
    }
    if (r == 5 && adj && !(0 < N->a_brk)) { ex = adj[0]; ey = adj[1]; }
    int p = N->a_plane;
    *VOFF(N, "xnci$", MAPX[p], 1) = sx;
    *VOFF(N, "ynci$", MAPY[p], 1) = sy;
    *VOFF(N, "znci$", MAPZ[p], 1) = N->a_zs;
    double *px = VOFF(N, "x$", MAPX[p], 0), *py = VOFF(N, "y$", MAPY[p], 0), *pz = VOFF(N, "z$", MAPZ[p], 0);
    *px = ex; *py = ey; *pz = N->a_ze;
    double rt = V("rotaxtyp$");
    if (rt < 6.0 && (rt == 2.0 || rt == 4.0) && V("bug1$") != -1.0) {
        N->a_save[0] = *px; N->a_save[1] = *py; N->a_save[2] = *pz;
        tpmap(N, "x$", NULL, 3);
    }
    double a[3], b[3], d[3];
    fill3(N->r, "xnci$", 1, a);
    fill3(N->r, "x$", 0, b);
    delta_change(N, a, b, d);
    V("dmax$") = fabs(d[0]) <= fabs(d[1]) ? fabs(d[1]) : fabs(d[0]);
    if (r == 0) {
        V("gcode$") = 1.0; N->gcode = 1;
        if (!legacy_linear_post_call(N, d, V("z_pstblock$") != 1.0)) return 0;
        V("gcode$") = g0; N->gcode = savg;
    } else if (r == 2) {
        if (!linearize_arc(N, 1)) return 0;
    } else {
        V("i$") = 0.0; V("j$") = 0.0; V("k$") = 0.0;
        double *pi = VOFF(N, "i$", MAPX[p], 0), *pj = VOFF(N, "j$", MAPY[p], 0);
        double dx = sx - cx, dy = sy - cy;
        int t = ijk_arctype(N, N->a_type);
        if (t == 1) { *pi = cx; *pj = cy; }
        else if (t == 2) { *pi = -dx; *pj = -dy; }
        else if (t == 3) { *pi = dx; *pj = dy; }
        else if (t == 4) { *pi = fabs(dx); *pj = fabs(dy); }
        if (rt < 6.0 && (rt == 2.0 || rt == 4.0) && V("bug1$") != -1.0) tpmap(N, "i$", NULL, 3);
        /* nobrk$ == 2 helix split (disassembly 0x18003336d..0x1800339b1): mill/router, arc plane not 1/2 (the
           MapX/Y/Z offsets are then 0), |breakarcs| == 1 or breakarcs == 2. d[2] > 1e-12: cVar19 = (vtoolz$ <= 0) + 1,
           d[2] < 1e-12: (vtoolz$ > 0) + 1, == 1e-12: 0. 1 = the Z move first (x$/y$ held at prv, nc_prv_modal$ =
           NcPreviousModal(x, y) = 3, gcode$ 1), then the flat arc; 2 = the flat arc (z$ held at prv, nc_prv_modal$ = 4),
           then the Z move. NcPreviousModal @180036780: 1/2/4 per x$/y$/z$ slot. */
        int split = 0;
        if (N->posttype < 2 && p != 1 && p != 2 && V("nobrk$") == 2.0 && (abs(N->a_brk) == 1 || N->a_brk == 2)) {
            if (1e-12 < d[2]) split = (V("vtoolz$") <= 0.0) + 1;
            else if (d[2] < 1e-12) split = (0.0 < V("vtoolz$")) + 1;
        }
        double hx = *px, hy = *py, hz = *pz, hd[3] = {d[0], d[1], d[2]};
        int zpst = V("z_pstblock$") != 1.0;
        if (split == 1) {
            *px = VP("x$"); *py = VP("y$");
            d[0] = 0.0; d[1] = 0.0;
            V("nc_prv_modal$") = 3.0;
            V("gcode$") = 1.0; N->gcode = 1;
            if (!legacy_linear_post_call(N, d, zpst)) return 0;
            *px = hx; *py = hy;
            d[0] = hd[0]; d[1] = hd[1]; d[2] = 0.0;
            V("nc_prv_modal$") = 0.0;
        } else if (split == 2) {
            *pz = VP("z$");
            d[2] = 0.0;
            V("nc_prv_modal$") = 4.0;
        }
        V("gcode$") = g0;
        N->gcode = savg;
        if (!legacy_circular_post_call(N, d)) return 0;
        V("nc_prv_modal$") = 0.0;
        if (split == 2) {
            *pz = hz;
            d[0] = 0.0; d[1] = 0.0; d[2] = hd[2];
            V("gcode$") = 1.0; N->gcode = 1;
            if (!legacy_linear_post_call(N, d, zpst)) return 0;
        }
        if (r == 5) {
            /* adjusted end point: a linear move to the real end point */
            *VOFF(N, "xnci$", MAPX[p], 1) = ex; *VOFF(N, "ynci$", MAPY[p], 1) = ey;
            *VOFF(N, "znci$", MAPZ[p], 1) = N->a_ze;
            *px = ex0; *py = ey0; *pz = N->a_ze;
            fill3(N->r, "xnci$", 1, a);
            fill3(N->r, "x$", 0, b);
            delta_change(N, a, b, d);
            V("dmax$") = fabs(d[0]) <= fabs(d[1]) ? fabs(d[1]) : fabs(d[0]);
            V("gcode$") = 1.0; N->gcode = 1;
            if (!legacy_linear_post_call(N, d, V("z_pstblock$") != 1.0)) return 0;
        }
    }
    if (rt < 6.0 && (rt == 2.0 || rt == 4.0) && V("bug1$") != -1.0) {
        *px = N->a_save[0]; *py = N->a_save[1]; *pz = N->a_save[2];
    }
    /* every path ends restoring gcode$ (local_190) and +0x38a (uVar3) */
    V("gcode$") = g0;
    N->gcode = savg;
    (void)last;
    return 1;
}

/* CPostArc::GetOctantBit @180033f80 */
static int octant_bit(double v, double t)
{
    if (v <= -(t - 1e-12)) return 0;
    return v < t - 1e-12 ? 1 : 2;
}
/* CPostArc::GetOctantQuad @180033fc0 (tables aOctant @1802a03d8, aOctantToQuadStartCCW @1802a0708,
   aOctantToQuadStartCW @1802a0828); tolerance = 10^-decimals of the format statement (x$'s fs here) */
static void octant_quad(Nci *N, double sweep, const double s[2], const double e[2], int *startq, int *count, int full)
{
    static const short aOct[9] = {5, 4, 3, 6, 0, 2, 7, 0, 1};
    static const short qCCW[8] = {1, 1, 2, 2, 3, 3, 0, 0}, qCW[8] = {3, 0, 0, 1, 1, 2, 2, 3};
    const MpFs *fs = &N->st0;
    int met = V("met_tool$") != 0.0;
    double t = pow(10.0, -(double)fs->ddig[met]);
    if (fs->round[met] != 0) t *= (double)fs->round[met];
    int o1 = aOct[octant_bit(s[1], t) + octant_bit(s[0], t) * 3];
    int o2 = aOct[octant_bit(e[1], t) + octant_bit(e[0], t) * 3];
    int a, b, d;
    int big = 4.212388980384689 < fabs(sweep * 0.01745329251994329);
    if ((0.0 < sweep && N->a_plane != 2) || (sweep < 0.0 && N->a_plane == 2)) {
        *startq = qCCW[o1];
        if (o2 < o1 || (o2 == o1 && (full || big))) o2 += 8;
        b = o2; a = o1; d = b - a;
    } else {
        *startq = qCW[o1];
        if (o1 < o2 || (o2 == o1 && (full || big))) o1 += 8;
        a = o1; b = o2; d = a - b;
    }
    *count = d / 2;
    if ((a % 2) == 0 && (b % 2) == 0) *count -= 1;
    V("so$") = (double)a;
    V("eo$") = (double)b;
}

/* CPostArc::LegacyCircularProcess @180034b80 */
static int legacy_circular_process(Nci *N, const double adj[3])
{
    fill3(N->r, "xnci$", 0, N->a_xn);
    double nobrkxz = V("nobrkxz$"), nobrkyz = V("nobrkyz$"), nobrk = V("nobrk$");
    double *full = VR(N->r, "full_arc_flg$", 0);
    int isfull = *full != 0.0;
    double *wl = VR(N->r, "whatline$", 0);
    if (*wl < 4.0) *wl += 1.0;
    V("i$") = 0.0; V("j$") = 0.0; V("k$") = 0.0;
    N->a_g = N->gcode;
    N->a_plane = N->plane;
    N->a_type = (short)(int)arcs_var(N, 6, N->plane);
    N->a_brk = (short)(int)arcs_var(N, 3, N->plane);
    N->a_full = (short)(int)arcs_var(N, 4, N->plane);
    N->a_helix = (short)(int)arcs_var(N, 5, N->plane);
    N->a_lin = (short)(int)arcs_var(N, 0, N->plane);
    double nb = N->a_plane == 1 ? nobrkyz : N->a_plane == 2 ? nobrkxz : nobrk;
    if (nb == 2.0 || (N->a_plane != 0 && N->a_helix == 2)) N->a_helix = 0;
    if (N->a_brk == 0) {
        if (N->a_full == 0 && *full != 0.0) { N->a_brk = 2; *full = 0.0; }
    } else if (N->a_full == 0 || *full == 0.0) *full = 0.0;
    else N->a_brk = -1;
    if (N->a_plane == 2) N->a_g = (N->a_g == 2) + 2;
    double s[3], e[3];
    map_to_arc(N->a_plane, N->p0, s);
    map_to_arc(N->a_plane, N->p1, e);
    double cen[3] = {N->cen[0], N->cen[1], N->cen[2]};
    if (N->a_plane == 2) { s[2] = -s[2]; e[2] = -e[2]; cen[2] = -cen[2]; }
    if (1 < N->posttype || fabs(N->arc_sweep) <= 1e-06) N->a_pitch = 0.0;
    else N->a_pitch = (e[2] - s[2]) * (360.0 / fabs(N->arc_sweep));
    if (fabs(N->a_pitch) < tol(N, 0) || N->a_helix == 0) N->a_pitch = 0.0;
    V("arc_pitch$") = N->a_pitch;
    /* round_opt$ digit rounding of arcrad$ (UnAssembleRoundOption): NOT DECODED (first branch skipped) */
    for (int k = 0; k < 3; k++) { s[k] = fmtn(N, s[k]); e[k] = fmtn(N, e[k]); }
    N->a_c[0] = fmtn(N, cen[0]); N->a_c[1] = fmtn(N, cen[1]);
    double zs0 = s[2], ze0 = e[2];
    N->a_zs = zs0; N->a_ze = ze0;
    double rs[3] = {s[0] - N->a_c[0], s[1] - N->a_c[1], 0}, re[3] = {e[0] - N->a_c[0], e[1] - N->a_c[1], 0};
    N->a_rad = sqrt(rs[1] * rs[1] + rs[0] * rs[0]);
    V("arcrad$") = N->a_rad;
    V("arcrad_end$") = sqrt(re[1] * re[1] + re[0] * re[0]);
    double sa = atan2(rs[1], rs[0]), ea = atan2(re[1], re[0]);
    get_start_end_sweep(N, N->arc_sweep, &sa, &ea, isfull);
    double sa0 = V("sa$"), ea0 = V("ea$"), sw = N->arc_sweep;
    int lin = N->a_lin != 0 || N->arcres < 5;
    int startq = 0, count = 0;
    octant_quad(N, N->arc_sweep, rs, re, &startq, &count, isfull);
    double eo0 = V("eo$");
    if (getenv("MPNCI_DEBUG"))   /* ours: debug trace */
        fprintf(stderr, "arc: line %g res %d brk %d full %d/%d helix %d lin %d sweep %g startq %d count %d type %d\n",
                N->rec[N->cur].line, N->arcres, N->a_brk, N->a_full, isfull, N->a_helix, N->a_lin, N->arc_sweep, startq,
                count, N->a_type);
    if (N->a_brk == 2 && (fabs(N->arc_sweep) <= 180.00000005 || fabs(V("sweep$")) <= 180.00000005)) N->a_brk = -1;
    if (N->a_brk == 1 && count == 0) N->a_brk = -1;
    if (!lin && N->posttype < 2 && N->a_brk < 1) {
        int h = helix_check(N, zs0, ze0, N->a_helix, tol(N, 7));
        if (h == 1) lin = 1;
        else if (h == 2) { V("arc_pitch$") = 0.0; N->a_helix = 0; }
    }
    if (lin) {
        /* LAB_180035986 */
        if (!linearize_arc(N, 0)) return 0;
        if (!call_case1(N, 3)) return 0;
    } else {
        double cur[3] = {rs[0], rs[1], 0}, dsw;
        if (0 < N->a_brk) {
            double brk[21] = {0};
            copy_to_break_vars(N);
            backup_break_vars(N, brk);
            zero_on_first_break(N);
            if (N->a_brk == 1) {
                static const short aQuad[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};   /* aQuad @1802a0858 */
                static const double qang[4] = {0.0, 90.0, 180.0, 270.0};               /* QuadAngle @1802a0868 */
                short ix[4] = {0, 1, 2, 3};                                            /* ixQuadReset */
                for (int k = 0; k < count; k++) {
                    int q = ix[startq & 3];
                    double nxt[3] = {aQuad[q][0] * N->a_rad, aQuad[q][1] * N->a_rad, 0};
                    double ang = qang[q], sad = V("sa$");
                    if (N->a_plane == 2) {
                        if (sw <= 0.0) { while (ang < sad) ang += 360.0; while (360.0 < ang - sad) ang -= 360.0; }
                        else { while (sad < ang) ang -= 360.0; while (360.0 < sad - ang) ang += 360.0; }
                    } else if (sw <= 0.0) { while (sad < ang) ang -= 360.0; while (360.0 < sad - ang) ang += 360.0; }
                    else { while (ang < sad) ang += 360.0; }
                    V("ea$") = ang;
                    calc_new_arc_data(N, sad, ang, zs0, ze0, sa0, sw, &dsw, 0);
                    V("sweep$") = dsw;
                    if (!circular_segment(N, cur, nxt, 0, NULL)) return 0;
                    N->a_zs = N->a_ze;
                    cur[0] = nxt[0]; cur[1] = nxt[1];
                    V("sa$") = V("ea$");
                    if (N->a_g == 2) { short t = ix[3]; memmove(ix + 1, ix, 3 * sizeof *ix); ix[0] = t; }   /* ShiftQuadCW */
                    else { short t = ix[0]; memmove(ix, ix + 1, 3 * sizeof *ix); ix[3] = t; }               /* ShiftQuadCCW */
                }
            } else {
                double mid[3] = {-rs[0], -rs[1], 0};
                double ang = N->a_g == 2 ? V("sa$") - 180.0 : V("sa$") + 180.0;
                V("ea$") = ang;
                calc_new_arc_data(N, V("sa$"), ang, zs0, ze0, sa0, sw, &dsw, 0);
                V("sweep$") = dsw;
                if (!circular_segment(N, cur, mid, 0, NULL)) return 0;
                N->a_zs = N->a_ze;
                cur[0] = mid[0]; cur[1] = mid[1];
                V("sa$") = V("ea$");
            }
            V("ea$") = ea0;
            V("eo$") = eo0;
            calc_new_arc_data(N, V("sa$"), V("ea$"), zs0, ze0, sa0, sw, &dsw, 1);
            V("sweep$") = dsw;
            zero_break_vars(N, brk);
            restore_break_vars(N, brk);
        }
        if (!circular_segment(N, cur, re, 1, adj)) return 0;
    }
    V("i$") = 0.0; V("j$") = 0.0; V("k$") = 0.0;
    VP("xnci$") = N->a_xn[0]; VP("ynci$") = N->a_xn[1]; VP("znci$") = N->a_xn[2];
    return lin ? 1 : 2;
}

/* CPostReadNci::call_case2 @1801098e0: 0 error, 1 handled as a line, 2 arc */
static int call_case2(Nci *N, int read)
{
    if (read) {
        if (!comments_on_path(N)) return 0;
        if (!rd2(N, N->rec[N->cur].prm)) return 0;
    }
    point_advance(N);
    fill3(N->r, "xc$", 0, N->cen);
    double adj[3] = {0, 0, 0};
    N->arcres = check_arc(N, N->p0, N->p1, N->cen, N->plane, N->rec[N->cur].g, (double)(short)(int)V("full_arc_flg$"),
                          adj, 1);
    map_from_arc(N->plane, N->p1, N->p1);    /* CPostMath::MapNciFromArcPlane (point already in view: identity
                                                 for plane 0; NOT DECODED for YZ/XZ arc-plane NCI coordinates) */
    if (N->plane == 2) V("zc$") = -V("zc$");
    set3(N->r, "xnci$", 0, N->p1);
    if (0 < N->arcres) {
        set_before_output(N);
        V("sweep$") = N->arc_sweep;
        if (BC(N->r, "pcir0$") && legacy_circular_process(N, adj)) {
            zero_after_output(N);
            if (N->tlkind != 0 || BC(N->r, "pcir_end$")) return 2;
        }
        return 0;
    }
    V("gcode$") = 1.0;
    N->gcode = 1;
    return call_case1(N, 1) ? 1 : 0;
}

/* ---------------------------------------------------------------------------------------------------------------
 * Drilling (CPostProcessDrill)
 * ------------------------------------------------------------------------------------------------------------- */
/* CPostProcessDrill::GetDrillPositions @1800f6ea0 */
static void get_drill_positions(Nci *N, double vec[3], double *dist)
{
    double ini = V("drl_sel_ini$"), ref = V("drl_sel_ref$"), tos = V("drl_sel_tos$");
    copy3(N->r, "u$", "drl_init_x$");
    double a[3], b[3], z[3], r[3], t[3];
    fill3(N->r, "drl_init_x$", 0, a);
    fill3(N->r, "drl_depth_x$", 0, b);
    for (int k = 0; k < 3; k++) vec[k] = a[k] - b[k];
    *dist = sqrt(vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2]);
    double l = *dist;
    for (int k = 0; k < 3; k++) vec[k] = l == 0.0 ? 0.0 : vec[k] / l;
    set3(N->r, "drl_vtoolx$", 0, vec);
    for (int k = 0; k < 3; k++) z[k] = -ini * vec[k] + a[k];
    set3(N->r, "drl_zdrl_x$", 0, z);
    for (int k = 0; k < 3; k++) r[k] = z[k] + ref * vec[k];
    set3(N->r, "drl_ref_x$", 0, r);
    double dr = sqrt((a[1] - r[1]) * (a[1] - r[1]) + (a[0] - r[0]) * (a[0] - r[0]) + (a[2] - r[2]) * (a[2] - r[2]));
    N->drl_far = *dist < dr;
    for (int k = 0; k < 3; k++) t[k] = tos * vec[k] + z[k];
    set3(N->r, "drl_tos_x$", 0, t);
}
static int vec_is_z(const double v[3])   /* eq_vec_tol(v, (0,0,1), 1, 5e-8) */
{
    return fabs(v[0]) <= 5e-8 && fabs(v[1]) <= 5e-8 && fabs(v[2] - 1.0) <= 5e-8;
}
/* CPostProcessDrill::SetupLegacyDrill @1800f7630 */
static int setup_legacy_drill(Nci *N, const double vec[3])
{
    if (N->posttype == 2) V("depth$") = V("znci$");
    else V("znci$") = V("drl_depth_z$");
    if ((!vec_is_z(vec) || V("tool_op$") == 28.0) && N->posttype != 2) return 0;
    V("zdrl$") = V("drl_zdrl_z$"); V("refht$") = V("drl_ref_z$"); V("tosz$") = V("drl_tos_z$");
    V("initht$") = V("drl_init_z$"); V("depth$") = V("drl_depth_z$");
    if (N->posttype != 2) {
        if (V("rotaxtyp$") < 6.0) axis_subst(N, V("xnci$"), V("ynci$"));
        V("znci$") = (N->tlkind != 0 || 6.0 <= V("rotaxtyp$")) ? V("drl_init_z$") : V("drl_depth_z$");
        return 1;
    }
    V("xdrl$") = V("drl_depth_x$"); V("x$") = V("drl_depth_x$");
    V("znci$") = V("drl_init_z$"); V("z$") = V("drl_init_z$");
    return 1;
}
/* CPostProcessDrill::DrillSkewed @1800f6d60 */
static int drill_skewed(Nci *N, const double v[3])
{
    V("drl_skewed$") = 0.0;
    if (!vec_is_z(v) && N->posttype != 2) {
        V("drl_skewed$") = 1.0;
        if (fabs(v[0] - 1.0) <= 5e-8 && fabs(v[1]) <= 5e-8 && fabs(v[2]) <= 5e-8) { V("drl_plane$") = 1.0; return 0; }
        /* (DAT_1802bc7a8 comparison -> drl_plane$ 2) */
        V("drl_plane$") = (fabs(v[0]) <= 5e-8 && fabs(v[1] - 1.0) <= 5e-8 && fabs(v[2]) <= 5e-8) ? 2.0 : -1.0;
        return 0;
    }
    V("drl_plane$") = 0.0;
    return 1;
}
/* rd81_proc @1800fc340 / rd100_proc @1800fb020 (common top-plane part; drill matrices / 5-axis: NOT DECODED) */
static int drill_proc(Nci *N, double cflg, int is100)
{
    double vec[3], dist;
    get_drill_positions(N, vec, &dist);
    int ok = setup_legacy_drill(N, vec);
    V("drl_skewed$") = 0.0;
    if (V("tool_op$") == 28.0) {
        /* NOT DECODED: Setup5axisLegacyDrill */
    } else {
        if (is100 && ok == 0 && vec_is_z(vec)) {
            V("zdrl$") = V("drl_init_z$") - V("drl_sel_ini$");
            V("refht$") = V("zdrl$") + V("drl_sel_ref$");
            V("initht$") = V("drl_init_z$");
            V("tosz$") = V("zdrl$") + V("drl_sel_tos$");
            V("depth$") = V("drl_depth_z$");
            if (V("rotaxtyp$") < 6.0) axis_subst(N, V("xnci$"), V("ynci$"));
            V("znci$") = (N->tlkind != 0 || 6.0 <= V("rotaxtyp$")) ? V("drl_init_z$") : V("drl_depth_z$");
            ok = 1;
        }
        tpmap(N, "drl_vtoolx$", NULL, 3);
        double v[3]; fill3(N->r, "drl_vtoolx$", 0, v);
        drill_skewed(N, v);
        if (V("drl_skewed$") != 0.0 && !ok) {
            V("zdrl$") = 9999.0; V("xdrl$") = 9999.0; V("refht$") = 9999.0; V("initht$") = 9999.0; V("tosz$") = 9999.0;
        }
    }
    get_feed(N, V("frplunge$"));
    point_advance(N);
    if (!make_contour_flags(N, cflg)) return 0;
    scale_post(N, 3, "xnci$");
    return 1;
}
/* rd81 @18010f9c0 (mill: 20 fields, 19 assigned; field 18 = contour flag) */
static int rd81(Nci *N, const char *p, double *cflg, int noproc)
{
    double f[20] = {0};
    scan_d(p, f, 20);
    static const char *const nm[] = {"drillcyc$", "drl_depth_x$", "drl_depth_y$", "drl_depth_z$", "dwell$",
        "frplunge$", "peck1$", "peck2$", "peckclr$", "retr$", "drl_sel_ini$", "drl_sel_ref$", "drl_sel_tos$",
        "shftdrl$", "drl_init_x$", "drl_init_y$", "drl_init_z$", NULL, "rev_drl5$", "drl_dia$"};
    N->drlcyc = (short)(int)f[0];
    assign_fields(N, f, nm, 20);
    *cflg = f[17];
    AR(N, f[0], "drl_cycle$");
    AR(N, f[1], "xnci$"); AR(N, f[2], "ynci$"); AR(N, f[3], "znci$");
    AR(N, f[3], "depth$");
    AR(N, f[10], "initht$"); AR(N, f[11], "refht$"); AR(N, f[12], "zdrl$"); AR(N, f[12], "xdrl$");
    if (!noproc) return drill_proc(N, *cflg, 0);
    return 1;
}
/* rd100 @18010b090 (mill: 24 fields, 22 assigned) */
static int rd100(Nci *N, const char *p, double *cflg, int noproc)
{
    double f[24] = {0};
    scan_d(p, f, 24);
    static const char *const nm[] = {NULL, "drl_depth_x$", "drl_depth_y$", "drl_depth_z$", "drl_sel_ini$",
        "drl_sel_ref$", "dwell$", "frplunge$", "drl_init_x$", "drl_init_y$", "drl_init_z$", NULL, "rev_drl5$",
        "drl_sel_tos$", "drl_m1$", "drl_m2$", "drl_m3$", "drl_m4$", "drl_m5$", "drl_m6$", "drl_m7$", "drl_m8$",
        "drl_m9$", "drl_dia$"};
    AR(N, (double)N->drlcyc, "drillcyc$");
    assign_fields(N, f, nm, 24);
    *cflg = f[11];
    if (N->posttype == 2) { AR(N, f[1], "xnci$"); AR(N, f[2], "depth$"); AR(N, f[3], "znci$"); }
    else { AR(N, f[1], "xnci$"); AR(N, f[2], "ynci$"); AR(N, f[3], "znci$"); }
    AR(N, f[3], "depth$");
    AR(N, f[5], "zdrl$"); AR(N, f[5], "xdrl$");
    if (!noproc) return drill_proc(N, *cflg, 1);
    return 1;
}
/* CPostProcessDrill::UsingCanDrillCycle @1800f79d0 (+ GetVarRefDrillCycle @1800be630) */
static int using_can_drill_cycle(Nci *N, int cyc)
{
    static const char *const m[8] = {"usecandrill$", "usecanpeck$", "usecanchip$", "usecantap$", "usecanbore1$",
                                     "usecanbore2$", "usecanmisc1$", "usecanmisc2$"};
    static const char *const l[8] = {"lusecandrill$", "lusecanpeck$", "lusecanchip$", "lusecantap$",
                                     "lusecanbore1$", "lusecanbore2$", "lusecanmisc1$", "lusecanmisc2$"};
    if ((unsigned short)cyc < 8 && V(N->posttype == 2 ? l[cyc] : m[cyc]) != 1.0) return 0;
    return 1;
}
/* CPostProcessDrill::canned @1800f7a10 (vtable slot 1; ReadLoop's wire path) */
static __attribute__((unused)) int drill_canned(Nci *N, int g)
{
    static const char *const a[8] = {"pcan1$", "pcan2$", "pcan3$", "pcan4$", "pcan5$", "pcan6$", "pcan7$", "pcan8$"};
    static const char *const b[8] = {"pcan1_2$", "pcan2_2$", "pcan3_2$", "pcan4_2$", "pcan5_2$", "pcan6_2$",
                                     "pcan7_2$", "pcan8_2$"};
    int c = (short)(int)V("cancyc$");
    const char *blk = g == 0x51 ? (c >= 0 && c < 8 ? a[c] : "pcancst$") : (c >= 0 && c < 8 ? b[c] : "pcancst_2$");
    if (!BC(N->r, blk)) return 0;
    last_point_update(N);
    return 1;
}
/* CPostProcessDrill::drill @1800f8210 (vtable slot 0) */
static int drill(Nci *N)
{
    double rt = V("rotaxtyp$");
    if (99 < post_round(V("drillcyc$"))) { V("drill5$") = 1.0; V("drillcyc$") = (double)N->drlcyc; return 1; }
    if (V("drill5$") == 0.0) {
        if (rt < 6.0) addofs(N, 0);
        V("x$") = V("xnci$"); V("y$") = V("ynci$"); V("z$") = V("znci$");
    }
    N->candrl = using_can_drill_cycle(N, N->drlcyc);
    if (!N->candrl) {
        if (V("drill5$") == 0.0 || N->gcode != 0x51) {
            V("drl_cycle_actv$") = 0.0;
            /* NOT DECODED: CPostProcessDrill::peck (long-hand drill cycles, @1800f9xxx) */
        }
        goto tail;
    }
    VP("drl_cycle_actv$") = V("drl_cycle_actv$");
    V("drl_cycle_actv$") = 1.0;
    if (6.0 <= rt) {
        /* drill5$ tool-axis geometry for rotaxtyp$ >= 6: NOT DECODED */
    } else {
        tpmap(N, "drl_init_x$", NULL, 3); tpmap(N, "u$", NULL, 3); tpmap(N, "drl_zdrl_x$", NULL, 3);
        tpmap(N, "drl_ref_x$", NULL, 3); tpmap(N, "drl_tos_x$", NULL, 3); tpmap(N, "drl_depth_x$", NULL, 3);
        tpmap(N, "initht$", "x$", -3); tpmap(N, "refht$", "x$", -3); tpmap(N, "depth$", "x$", -3);
        tpmap(N, "x$", NULL, 3);
    }
    V("whatline$") = 4.0;
    {
        double d5 = V("drill5$");
        int first = (N->gcode == 0x51 && d5 == 0.0) || (rt <= 6.0 && N->gcode != 0x51 && d5 == 1.0) ||
                    (6.0 < rt && V("gnci$") == 81.0 && d5 != 0.0) || (N->canceldc && VP("drl_cycle_actv$") <= 0.0);
        double a[3], b[3], d[3];
        fill3(N->r, "xnci$", 0, a);
        fill3(N->r, "x$", 1, b);
        delta_change(N, b, a, d);
        motion_test(N, d);
        /* FindMoveAngle: NOT DECODED */
        static const char *const m1[8] = {"mdrill$", "mpeck$", "mchpbrk$", "mtap$", "mbore1$", "mbore2$", "mmisc1$",
                                          "mmisc2$"};
        static const char *const l1[8] = {"ldrill$", "lpeck$", "lchpbrk$", "ltap$", "lbore1$", "lbore2$", "lmisc1$",
                                          "lmisc2$"};
        static const char *const p1[8] = {"pdrill$", "ppeck$", "pchpbrk$", "ptap$", "pbore1$", "pbore2$", "pmisc1$",
                                          "pmisc2$"};
        static const char *const m2[8] = {"mdrill_2$", "mpeck_2$", "mchpbrk_2$", "mtap_2$", "mbore1_2$", "mbore2_2$",
                                          "mmisc1_2$", "mmisc2_2$"};
        static const char *const l2[8] = {"ldrill_2$", "lpeck_2$", "lchpbrk_2$", "ltap_2$", "lbore1_2$", "lbore2_2$",
                                          "lmisc1_2$", "lmisc2_2$"};
        static const char *const p2[8] = {"pdrill_2$", "ppeck_2$", "pchpbrk_2$", "ptap_2$", "pbore1_2$", "pbore2_2$",
                                          "pmisc1_2$", "pmisc2_2$"};
        int c = N->drlcyc;
        const char *blk;
        if (c < 0 || c > 7) blk = first ? "pdrlcst$" : "pdrlcst_2$";
        else {
            const char *mb = V("posttype$") == 2.0 ? (first ? l1[c] : l2[c]) : (first ? m1[c] : m2[c]);
            blk = BDEF(N->r, mb) ? mb : (first ? p1[c] : p2[c]);
        }
        if (!BC(N->r, blk)) return 0;
    }
tail:
    if (rt < 6.0 && V("drill5$") != 0.0 && N->gcode != 0x51) V("drill5$") += 1.0;
    return 1;
}
/* CPostProcessDrill::rd80_proc @1800fc140 (param 1 = from the canceldc$ command variable) */
static int rd80_proc(Nci *N, int fromcmd)
{
    (void)fromcmd;
    if ((N->canceldc && V("drl_cycle_actv$") == 1.0) || (!N->canceldc && N->candrl)) {
        if (V("drill5$") == 0.0 && V("drl_skewed$") == 0.0)
            V("z$") = V(V("rtnrefht$") != 0.0 ? "refht$" : "initht$");
        if (V("rotaxtyp$") < 6.0) V("drill5$") = 0.0;
        if (!BC(N->r, "pcanceldc$")) return 0;
        zero_after_output(N);
    }
    V("drill5$") = 0.0;
    N->candrl = 0;
    V("drl_cycle_actv$") = 0.0;
    return 1;
}
/* call_case11 @180108ca0 entry used by drilling with drill5$ (5-axis): NOT DECODED -> no output */
static int call_case11(Nci *N, int read)
{
    (void)read;
    V("mill5$") = V("tool_op$") == 28.0 ? 2.0 : 1.0;
    /* NOT DECODED: rd11 / parse_rev5 / CPostRunFiveAxis::fiveax / pmx$ */
    return 1;
}
static int toolchange_end_call(Nci *N);
/* CPostProcessDrill::case81_proc @1800f7db0 / Case81 @1800f6cb0 */
static int case81(Nci *N, double *cflg)
{
    NRec *rc = &N->rec[N->cur];
    if (rc->fl & RF_BDRL) {
        /* rd81_100_bdrl + pblockdrl$: NOT DECODED (bdrl_* fields) */
        if (!BC(N->r, "pblockdrl$")) return 0;
    }
    if ((rc->fl & RF_BDRL) && !(rc->fl & RF_BDRLEND)) return 1;
    if (!comments_on_path(N)) return 0;
    if (!rd81(N, rc->prm, cflg, 0)) return 0;
    get1025(N);
    V("drillcur$") = 1.0;
    if (6.0 <= V("rotaxtyp$") && V("tool_op$") == 28.0) V("drill5$") = 1.0;
    if (!BC(N->r, "pdrill0$")) return 0;
    if ((!using_can_drill_cycle(N, N->drlcyc) && V("nextop$") == 80.0) || 7 < N->drlcyc) {
        V("nextop$") = VP("nextop$");
        VP("nextop$") = 80.0;
    }
    if (!drill(N)) return 0;
    if (V("drill5$") == 1.0) {
        N->gcode = 0xb; V("gcode$") = 11.0;
        if (!call_case11(N, 0)) return 0;
    }
    zero_after_output(N);
    if (N->tlkind == 0) return BC(N->r, "pdrl_end$");
    return toolchange_end_call(N);
}
/* CPostProcessDrill::Case100 @1800f6730 (+ case100_proc @1800f7bc0) */
static int case100(Nci *N, double *cflg)
{
    NRec *rc = &N->rec[N->cur];
    if (!(rc->fl & RF_BDRL) && V("drl_cycle_actv$") == 0.0) V("drl_cycle_actv$") = -1.0;
    if (rc->fl & RF_BDRL) { if (!BC(N->r, "pblockdrl$")) return 0; }
    if ((rc->fl & RF_BDRL) && !(rc->fl & RF_BDRLEND)) return 1;
    if (!comments_on_path(N)) return 0;
    if (!rd100(N, rc->prm, cflg, 0)) return 0;
    V("drillcur$") = 1.0;
    get1025(N);
    V("drillcur$") = 1.0;
    if (!N->canceldc || 0.0 < V("drl_cycle_actv$")) {
        if (V("drill5$") == 0.0) {
            if (!BC(N->r, "pdrill0$")) return 0;
            if ((!using_can_drill_cycle(N, N->drlcyc) && V("nextop$") == 80.0) || 7 < N->drlcyc) {
                V("nextop$") = VP("nextop$");
                VP("nextop$") = 80.0;
            }
            if (!drill(N)) return 0;
        } else {
            if (V("rpd_typ_v7$") == 0.0 && !BC(N->r, "pdrill0$")) return 0;
            N->gcode = 0xb; V("gcode$") = 11.0;
            if (!call_case11(N, 0)) return 0;
        }
    } else {
        N->gcode = 0x51; V("gcode$") = 81.0; V("gnci$") = 81.0;
        if (V("tool_op$") == 28.0) V("drillcyc$") = (double)N->drlcyc + 100.0;
        if (6.0 <= V("rotaxtyp$") && V("tool_op$") == 28.0) V("drill5$") = 1.0;
        if (!BC(N->r, "pdrill0$")) return 0;
        if ((!using_can_drill_cycle(N, N->drlcyc) && V("nextop$") == 80.0) || 7 < N->drlcyc) {
            V("nextop$") = VP("nextop$");
            VP("nextop$") = 80.0;
        }
        if (!drill(N)) return 0;
        if (V("drill5$") == 1.0) {
            N->gcode = 0xb; V("gcode$") = 11.0;
            if (!call_case11(N, 0)) return 0;
        }
    }
    zero_after_output(N);
    if (N->tlkind == 0) return BC(N->r, "pdrl_end$");
    return toolchange_end_call(N);
}

/* ---------------------------------------------------------------------------------------------------------------
 * Tool changes, end of file, loop
 * ------------------------------------------------------------------------------------------------------------- */
/* CPostReadNci::ToolchangeEndPostblockCall @180108b50 */
static int toolchange_end_call(Nci *N)
{
    if (N->tlkind == 1) return BC(N->r, "psof_end$");
    if (N->tlkind == 2) return BC(N->r, "ptlchg_end$");
    return BC(N->r, "ptlchg0_end$");
}
/* CPostReadNci::rd1000_proc @18010ace0 */
static int rd1000_proc(Nci *N)
{
    if (!BC(N->r, "prd1000$")) return 0;
    if (N->posttype == 3) { scale_post(N, 3, "threadx$"); scale_post(N, 3, "startx$"); }
    else { scale_post(N, 3, "xr$"); scale_post(N, 3, "xh$"); }
    if (V("rotaxtyp$") < 6.0 && N->posttype != 3) { tpmap(N, "xr$", NULL, 3); tpmap(N, "xh$", NULL, 3); }
    return 1;
}
/* CPostReadNci::GetEndGcode @1800fec20 (rd1000_end / rd1013_end / rd1016_end: NOT DECODED field tables) */
static int get_end_gcode(Nci *N)
{
    for (int i = N->cur; i < N->nrec; i++) {
        NRec *rc = &N->rec[i];
        if (rc->end) break;
        if (999 < rc->g && rc->g < 0x3eb) { AR(N, (double)rc->g, "end_gcode$"); break; }
    }
    double *ert = VR(N->r, "end_rot_type$", 0);
    if (99.0 < V("end_opcode$") || *ert == 0.0) *ert = 0.0;
    else *ert = (double)post_round(fabs(*ert) / 10.0) + 1.0;
    return 1;
}

#include "mp_nciloop_tt.c"   /* tool table: CPostValidateToolChanges, PPostToolList, GetNextTool, wrtt */
#include "mp_nciloop_sub.c"  /* subprograms: CPostSubsTraditional Case1018 / Case1019 / DoTransformSubs */

/* tool change records 1000 / 1001 / 1002 (ReadLoop @180101990 cases 1000 / 0x3e9 / 0x3ea).
   returns 1 ok (-> default end of iteration), 0 = status check (LAB_180102d11), -1 = return 0 */
static int do_toolchange(Nci *N, int g)
{
    NRec *rc = &N->rec[N->cur];
    double seqno = 0, seqinc = 0;
    int ttc = 0;                                          /* tooltable$ units digit */
    if (g == 0x3e9) {
        int v = (int)V("tooltable$"), a = v < 0 ? -v : v;
        ttc = (short)(a % 10);
        if (ttc == 2) ttc = 1;
    }
    rd1000(N, rc->prm, &seqno, &seqinc);
    if (g == 0x3e9) {
        if (!N->cd_loaded) { AR(N, seqno, "seqno$"); AR(N, seqinc, "seqinc$"); }
        AR(N, V("seqno$"), "n$");
    }
    if (V("rotaxtyp$") < 6.0) map_top(N, "x$", 1, "x4$");     /* + CPostRunFiveAxis::setmap/toolpl: NOT DECODED */
    if (!BC(N->r, g == 0x3e9 ? "prot0$" : "prot$")) return 0;
    /* CPostRunFiveAxis::load_matm: NOT DECODED */
    if (!rd1000_proc(N)) return 0;
    if (V("rotaxtyp$") < 6.0) { map_top(N, "x$", 1, "x4$"); map_top(N, "tox$", 0, "tox4$"); }
    if (g != 0x3e9) get_next_tool(N);
    get1025(N);
    if (g == 0x3e9) {
        if (!BC(N->r, "psof00$")) return 0;
        if (ttc != -1 && !N->header_done) {
            if (!BC(N->r, "pheader$")) return 0;
            N->header_done = 1;
        }
        if (0 < ttc && BDEF(N->r, "pwrtt$")) {
            if (!wrtt(N, 0)) { if (4 < STATUS(N->r)) return -1; }
        }
        if (N->comm_mode == 0 && comment_output(N, 0) == 0) return 0;
        get_next_tool(N);
        if (V("tlchng_aft$") == 0.0) {
            set_tlkind(N, 1);
            if (!BC_tlchg(N)) return 0;
            zero_after_output(N);
            if (!BC(N->r, "psof_end$")) { if (4 < STATUS(N->r)) return -1; N->r->status = 0; }
        } else {
            if (!BC(N->r, "ptlchg1002$")) return 0;
            if (V("cant_tlchng$") == -1.0) zero1025(N);
            set_tlkind(N, 1);
            set_tl_done(N, 0);
        }
        V("tooltable$") = -1.0;
        N->tl_seen = 1;
        return 1;
    }
    if (!BC(N->r, "ptlchg00$")) return 0;
    if (!N->header_done) {        /* (&& !(+0x1150 & 0x4000000)) */
        if (!BC(N->r, "pheader$") && STATUS(N->r) < 5) return -1;
        N->header_done = 1;
    }
    int kind = g == 1000 ? 3 : 2;
    if ((g == 1000 && V("tlchng_aft$") == 2.0) || (g == 0x3ea && V("tlchng_aft$") != 0.0)) {
        if (!BC(N->r, "ptlchg1002$")) return 0;
        if (V("cant_tlchng$") == -1.0) zero1025(N);
        set_tlkind(N, kind);
        set_tl_done(N, 0);
    } else {
        set_tlkind(N, kind);
        if (!BC_tlchg(N)) return 0;
        zero_after_output(N);
        if (!BC(N->r, g == 1000 ? "ptlchg0_end$" : "ptlchg_end$")) {
            if (4 < STATUS(N->r)) return -1;
            N->r->status = 0;
        }
    }
    if (g == 0x3ea) N->tl_seen = 1;
    return 1;
}

/* 1003 end of file (ReadLoop case 0x3eb). returns as do_toolchange */
static int do_eof(Nci *N)
{
    if (!tl_done(N) && N->tl_seen) {
        if (!BC_tlchg(N)) { if (4 < STATUS(N->r)) return -1; set_tl_done(N, 1); }
        if (!toolchange_end_call(N) && 4 < STATUS(N->r)) return -1;
    }
    /* (+0x1159 probe checks: NOT DECODED) */
    set_tlkind(N, 4);
    if (!comments_on_path(N)) return 0;
    if (N->eof_synth < 1) rd1003(N, N->rec[N->cur].prm);
    get1025(N);
    V("nextop$") = 1003.0;
    if (!BC(N->r, "peof00$")) return 0;
    const char *b;
    if (V("t$") == 0.0 && V("tool_zero$") != 0.0) {
        if (N->posttype < 2 && BDEF(N->r, "meof0$")) b = "meof0$";
        else if (N->posttype == 2 && BDEF(N->r, "leof0$")) b = "leof0$";
        else b = "peof0$";
    } else {
        /* the binary tests meof$ / leof$ but then calls meof0$ / leof0$ (ReadLoop @1801046cf) */
        if (N->posttype < 2 && BDEF(N->r, "meof$")) b = "meof0$";
        else if (N->posttype == 2 && BDEF(N->r, "leof$")) b = "leof0$";
        else b = "peof$";
    }
    if (!BC(N->r, b)) return 0;
    zero_after_output(N);
    N->eof_done = 1;
    set_tlkind(N, 0);
    return 1;
}

/* the motion-end step (LAB_180102e52 / LAB_180102e63 / LAB_180102e6b) */
static int motion_end(Nci *N, const char *endblk)
{
    int ok;
    if (N->tlkind != 0) ok = toolchange_end_call(N);
    else if (endblk) ok = BC(N->r, endblk);
    else return 1;
    if (!ok) {
        if (STATUS(N->r) == 0) return -1;
        N->r->status = 0;
    }
    return 1;
}

/* one record (ReadLoop's dispatch from LAB_1801029a8). returns 1 continue, 0 status check, -1 stop (return 0) */
static int dispatch(Nci *N)
{
    NRec *rc = &N->rec[N->cur];
    int g = rc->g, r;
    AR(N, (double)g, "gnci$");
    N->gcode = g;
    AR(N, 0.0, "uvflag$");
    AR(N, (double)N->gcode, "gcode$");
    AR(N, rc->line, "nci_line$");
    AR(N, 0.0, "comm_call$");
    start_loop_contour_flags(N);
    switch (g) {
    case 0: case 1:
        if (!call_case1(N, V("redo_proc$") == -9999.0 ? 1 : 0)) return 0;
        return motion_end(N, NULL) < 0 ? -1 : 1;
    case 2: case 3:
        r = call_case2(N, V("redo_proc$") != -9999.0);
        if (r == 0) return 0;
        if (r == 1) return 1;
        return motion_end(N, NULL) < 0 ? -1 : 1;
    case 4:
        if (!comments_on_path(N)) return 0;
        rd4(N, rc->prm);
        get1025(N);
        if (!BC(N->r, "pdwl_spd$")) return 0;
        V("dwell$") = 0.0;
        zero_after_output(N);
        return 1;
    case 0xb:
        if (!call_case11(N, V("redo_proc$") != -9999.0)) return 0;
        if (N->tlkind == 0) { if (!BC(N->r, "pmx_end$")) { if (STATUS(N->r) == 0) return -1; N->r->status = 0; } }
        else if (motion_end(N, NULL) < 0) return -1;
        return 1;
    case 0x50:
        if (!comments_on_path(N)) return 0;
        if ((N->canceldc && V("drl_cycle_actv$") == 1.0) || (!N->canceldc && N->candrl)) get1025(N);
        return rd80_proc(N, 0) ? 1 : 0;
    case 0x51: {
        double cflg = 0;
        if (!case81(N, &cflg)) return 0;
        if (V("posttype$") == 2.0) { N->candrl = 0; V("drl_cycle_actv$") = 0.0; }
        return 1;
    }
    case 0x52:
        if (!comments_on_path(N)) return 0;
        /* rd82 (drl_prm1$..drl_prm10$) */
        {
            static const char *const nm[] = {"drl_prm1$", "drl_prm2$", "drl_prm3$", "drl_prm4$", "drl_prm5$",
                                             "drl_prm6$", "drl_prm7$", "drl_prm8$", "drl_prm9$", "drl_prm10$"};
            rd_list(N, rc->prm, nm, 10);
        }
        return 1;
    case 0x53:
        if (!comments_on_path(N)) return 0;
        /* rd83 (bdrl_*): NOT DECODED beyond the field names in nci_fields.txt */
        return 1;
    case 100: {
        double cflg = 0;
        if (!case100(N, &cflg)) return 0;
        return 1;
    }
    case 200: case 0xc9:
        if (!comments_on_path(N)) return 0;
        /* rd200 / rd201 + pthread0$ / CPostThread::ProcessThread / pthd_end$: NOT DECODED (lathe threading) */
        return 1;
    case 900:
        if (!comments_on_path(N)) return 0;
        rd_simple(N, 900, rc->prm);
        if (!BDEF(N->r, "pstck_trans$")) return 1;
        get1025(N);
        if (!BC(N->r, "pstck_trans$")) return 0;
        zero_after_output(N);
        return motion_end(N, NULL) < 0 ? -1 : 1;
    case 901: case 902: case 903: case 904: case 905: {
        static const char *const b[] = {"pstck_flip$", "pstck_bar_fd$", "pchuck$", "ptailstock$", "psteadyrest$"};
        if (!comments_on_path(N)) return 0;
        rd_simple(N, g, rc->prm);
        if (!BDEF(N->r, b[g - 901])) return 1;
        get1025(N);
        if (!BC(N->r, b[g - 901])) return 0;
        zero_after_output(N);
        if (N->tlkind != 0 && !toolchange_end_call(N)) { if (STATUS(N->r) == 0) return -1; N->r->status = 0; }
        return 1;
    }
    case 911: case 912:
        rd_simple(N, g, rc->prm);
        return 1;
    case 950:                                         /* ReadLoop case 0x3b6 */
        rd950(N, rc->prm);
        if (V("cant_tlchng$") != -1.0) get1025(N);
        if (!BC(N->r, "psynclath$")) return 0;
        /* CPostRunFiveAxis::SetInitialToolplaneMatrix: NOT DECODED */
        if (V("cant_tlchng$") != -1.0) zero1025(N);
        return 1;
    case 999: {
        rd999(N, rc->prm);
        /* IsManualEntryOp / IsMiscOpOp / FindStreamInfoSequence: NOT DECODED; operation_seq$ untouched */
        return 1;
    }
    case 1000: case 0x3e9: case 0x3ea:
        r = do_toolchange(N, g);
        return r;
    case 0x3eb:
        return do_eof(N);
    case 0x3ec:
        if (!comments_on_path(N)) return STATUS(N->r) > 4 ? -1 : 1;
        return BC(N->r, "pcancelcc$") ? 1 : 0;
    case 0x3f2:
        /* rd1010w (wire): NOT DECODED */
        get1025(N);
        if (!BC(N->r, "pconchg$")) return 0;
        zero_after_output(N);
        return 1;
    case 0x3f3:
        if (!tl_done(N) && N->tl_seen) {
            if (!BC_tlchg(N)) { if (4 < STATUS(N->r)) return -1; set_tl_done(N, 1); }
            if (!toolchange_end_call(N) && 4 < STATUS(N->r)) return -1;
        }
        zero_after_output(N);
        if (N->tl_seen) {
            if (!get_end_gcode(N)) return -1;
            if (!call_mlp(N, "mtoolend$", "ltoolend$", "ptoolend$")) return 0;
            /* operation_seq$ = FindStreamInfoSequence: NOT DECODED */
        }
        V("mill5$") = 0.0;
        V("cutpos2$") = 0.0;
        rd1011(N, rc->prm);
        return BC(N->r, "pmiscreal$") ? 1 : 0;
    case 0x3f4:
        rd1012(N, rc->prm);
        return BC(N->r, "pmiscint$") ? 1 : 0;
    case 0x3f5: {
        rd1013(N, rc->prm);
        double c = nci_comp_to_ccomp((short)(int)V("cc$"));
        AR(N, (double)(int)(short)(int)c, "ccomp$");
        if (V("cc_1013$") != 0.0) {
            ccomp_to_ccpos((short)(int)c, VR(N->r, "cc_pos$", 0));
            AR(N, V("cc_pos$"), "cc_pos$");
        }
        if (!BC(N->r, "pmiscparam$")) return 0;
        if (N->posttype != 3 && 6.0 <= V("rotaxtyp$") && !BC(N->r, "protofs$")) return 0;
        return 1;
    }
    case 0x3f6:
        rd1014(N, rc->prm);
        if (6.0 <= V("rotaxtyp$")) copy3(N->r, "vtoolx$", "m7$");
        return 1;
    case 0x3f7: {
        rd1015(N, rc->prm);
        int v = (int)V("tooltable$"), a = v < 0 ? -v : v;
        if (-1 < (short)(a % 10) && V("subprg$") != 0.0 && !BC(N->r, "psubcall$")) return 0;
        return 1;
    }
    case 0x3f8: rd1016(N, rc->prm); return 1;
    case 0x3f9: rd1017(N, rc->prm); return 1;
    case 0x3fa: return case1018(N, rc->prm);     /* CPostSubsTraditional::Case1018 */
    case 0x3fb: return case1019(N, rc->prm);     /* CPostSubsTraditional::Case1019 */
    case 0x3fc:
        rd1020(N, rc->prm);
        if (mp_run_call_read_op_info) mp_run_call_read_op_info(N->r, 0);   /* CallReadOpInfo(0, 0) */
        return 1;
    case 0x401: rd1025(N, rc->prm); return 1;
    case 0x403: rd1027(N, rc->prm); return 1;
    case 0x404: rd1028(N, rc->prm); return 1;
    case 0x405: rd1029(N, rc->prm); return 1;
    case 0x407:
        rd1031(N, rc->prm);
        return BC(N->r, "ptransreal$") ? 1 : 0;
    case 0x408:
        rd1032(N, rc->prm);
        return BC(N->r, "ptransint$") ? 1 : 0;
    case 0x41a:
        rd1050(N, rc->prm);
        return 1;
    default:
        return 1;
    }
}

/* CPostReadNci::ReadLoop @180101990 (non-wire path) */
static int read_loop(Nci *N)
{
    MpRun *r = N->r;
    /* InitReadLoop @180100210 */
    AR(N, (double)N->posttype, "posttype$");
    N->eof_synth = -1;
    N->gcode = -1;
    N->header_done = 0; N->tl_seen = 0; N->eof_done = 0;
    set_tl_done(N, 0);
    set_tlkind(N, 0);
    zero1025(N);
    if (V("use_2d_uv$") == 0.0) V("plot_2d_uv$") = 0.0;
    V("tooltable_active$") = 0.0;
    V("probe_active$") = 0.0;
    if (!BC(r, "pq$") && 4 < STATUS(r)) return 0;
    N->cont = 1;
    N->cur = -1;
    for (;;) {
        if (!N->cont) return 1;
        if (mp_run_stopped(r) && STATUS(r) > 4) return 0;
        double cc = V("comm_call$");
        if (cc < -5e-08 && N->comm_mode != 0) {
            if (cc == -1.0) N->comm_mode = 2;
            else if (cc == -2.0) N->comm_mode = 3;
            else if (cc == -9999.0) N->comm_mode = 1;
        }
        V("comm_call$") = 0.0;
        if (tl_done(N)) set_tlkind(N, 0);
        V("arcrad$") = 0.0;
        V("drillcur$") = 0.0;
        /* ReadNciProcessInhibitsSkips @180107660 -> ReadNextNci (rcc_flg$ 3..5 sub-program skipping: NOT DECODED) */
        N->cur++;
        if (N->cur >= N->nrec) return 1;
        NRec *rc = &N->rec[N->cur];
        if (rc->end) N->cont = 0;
        N->next = rc->end ? -1 : get_next_next(N, N->cur + 1);
        if (!N->cont) {
            /* past the end: a 1003 is synthesised (LAB_1801023d6) */
            rc->g = 0x3eb;
            rc->fl &= ~5u;
            if (N->eof_done) { N->eof_synth = 0; if (rc->com >= 0 && !rl_comments_params(N, rc)) return 0; continue; }
            N->eof_synth = 1;
        }
        if ((rc->fl & RF_RPD) && V("rpd_typ_v7$") != 0.0) { rc->fl |= RF_SKIP; continue; }
        if (rc->g == 999) {
            int op = 0; sscanf(rc->prm, "%d", &op);
            N->tool_op = op;                              /* rd999_tool_op (operation ID: NOT DECODED) */
        }
        if (rc->fl & RF_SKIP) continue;                   /* ReadContinue @1801017c0 */
        if (!rl_comments_params(N, rc)) return 0;
        int tl = ((unsigned short)(rc->g - 1000) < 3) || N->tlkind != 0;
        scan_for_next_move(N, tl, (int)V("getnextop$"));
        int d = dispatch(N);
        if (d < 0) return 0;
        if (d == 0) { if (4 < STATUS(r)) return 0; }
        if (!N->cont) return 1;
    }
}

/* ---------------------------------------------------------------------------------------------------------------
 * Command variables (CPostOutput::CommandVariableOutput @1800d9960 -> mcpost NCI objects)
 * ------------------------------------------------------------------------------------------------------------- */
static int nci_cmd_hook(MpRun *r, int var)
{
    Nci *N = (Nci *)r->nci;
    if (!N) return 0;
    if (var == vi(r, "comment$")) {
        double cnt = V("comm_cnt$");
        int s = comment_output(N, 1);
        if (0 < s) {
            N->comm_state |= 4;
            s = 2;
            if (0.0 < cnt && !BC(r, "pcomment_end$")) s = 1;
            N->comm_state &= ~4u;
            return s;
        }
        return 1;
    }
    if (var == vi(r, "canceldc$")) { rd80_proc(N, 1); return 1; }
    if (var == vi(r, "c_msng$")) { sub_cmd(N, 0); return 1; }
    if (var == vi(r, "c_mmlt$")) { sub_cmd(N, 1); return 1; }
    if (var == vi(r, "tooltable$")) {
        if (!wrtt(N, 1)) r->status = 6;
        return 1;
    }
    if (var == vi(r, "comment_probe$") || var == vi(r, "x_tooltable$") || var == vi(r, "c_rcc_setup$")) {
        return 1;      /* NOT DECODED: CommentProbeOutput / ProcessToolTableCall / prcc_setup$ */
    }
    return 0;
}

/* does the post output comment$ in a postblock (CPostPostTable::ProcessPostBlock @1800efeb0 ->
   CPostComponents::SetCommentInPostblock) */
static int items_use(const MpItem *it, int var)
{
    for (; it; it = it->next) {
        if (it->code >= EL_VAR && it->code <= EL_VAR_PRV && it->idx == var) return 1;
        if (items_use(it->body, var) || items_use(it->els, var)) return 1;
    }
    return 0;
}
static int comment_in_postblock(MpRun *r)
{
    int v = vi(r, "comment$");
    if (v < 0) return 0;
    for (int b = 0; b < r->p->nblocks; b++) if (items_use(r->p->blocks[b].body, v)) return 1;
    return 0;
}

/* CPostRunNciUtil::AssignOutputVariableReInit @180125cb0: skipped when the post-table flag 0x1000 is set */
static void reinit_var(Nci *N, const char *name, double v)
{
    int i = vi(N->r, name);
    if (i < 0 || (N->r->vars[i].f68 & 0x1000)) return;
    N->r->vars[i].v[0] = v;
}
/* OverrideInitializeFromPprep @180111290, sub_level$ part: transform-subprogram mode CPostRunComponents +0x1210
   (1/2 traditional subs + subprg$/auxprg$ (/extprg$, subout$ 3), 3/4 with psub_call_tt$/psub_st_tt$/psub_end_tt$,
   -1 none); sub_level$ is then re-initialised to 0. The sub-level arrays +0x1218/+0x1240/+0x1268 (SetSize 10, entry
   +0x13d0: +0xb4 = 1, +0xb5 = 0, +0xb6 = 1, +0x10 = seqno$): sub_init (mp_nciloop_sub.c). */
static void override_pprep_sub_level(Nci *N)
{
    double *sl = VR(N->r, "sub_level$", 0);
    int tt = BDEF(N->r, "psub_call_tt$") && BDEF(N->r, "psub_st_tt$") && BDEF(N->r, "psub_end_tt$");
    int lv = (int)*sl;
    if (lv == 1 && tt) *sl = 3.0;
    else if (lv == 2 && tt) *sl = 4.0;
    else if (lv == 3 && !tt) *sl = 1.0;
    else if (lv == 4 && !tt) *sl = 2.0;
    double d = *sl;
    switch ((int)d) {
    case 1: case 2:
        N->sub_mode = (short)(int)d;
        if (V("subprg$") == 0.0) reinit_var(N, "subprg$", 1.0);
        if (V("auxprg$") == 0.0) reinit_var(N, "auxprg$", 1.0);
        if (*sl == 2.0) {
            if (V("extprg$") == 0.0) reinit_var(N, "extprg$", 1.0);
            reinit_var(N, "subout$", 3.0);
        }
        *sl = 0.0;
        break;
    case 3: case 4: N->sub_mode = (short)(int)d; *sl = 0.0; break;
    case -2: N->sub_mode = -1; break;
    case -1: case 0: N->sub_mode = 0; *sl = -1.0; break;
    default: N->sub_mode = -1; *sl = -2.0; break;
    }
    if (N->sub_mode != -1) { N->sub_seqno = V("seqno$"); sub_init(N); }
    *sl = 0.0;
    reinit_var(N, "sub_level$", 0.0);
}

/* CPostRun::RunTraditional @180112130 */
int mp_run_nci(MpRun *r, FILE *nci)
{
    if (!r || !nci) return 0;
    Nci *N = calloc(1, sizeof *N);
    N->r = r;
    N->zidx = mp_var_find(r, "z$");
    N->next = -1; N->out_com = -1; N->out_end = -1; N->pend_com = -1; N->pend_par = -1; N->prev_grp = -1;
    N->blk_rec = -1;
    N->posttype = r->env.product < 0 ? 0 : r->env.product;    /* InitReadLoop: CPostRunNciUtil::GetPosttypeToolOp - NOT DECODED */
    r->nci = N;
    r->cmd_hook = nci_cmd_hook;
    read_all(N, nci);
    int ok = 0;
    /* Read1050NciHeader @180101600: the first record must be readable (rd1050 when it is a 1050) */
    if (N->nrec <= 1) goto done;
    if (N->rec[0].g == 0x41a) rd1050(N, N->rec[0].prm);
    filename_to_strings(N, r->env.nci_path, "spathnci$", "snamenci$", "sextnci$", -1);
    filename_to_strings(N, r->env.pst_path, "spathpst$", "snamepst$", "sextpst$", -1);
    filename_to_strings(N, r->env.nc_path, "spathnc$", "snamenc$", "sextnc$", -1);
    /* CPostStreams::BuildEnhancedToolTable(force_tlchg$) (tool-change part, mp_nciloop_tt.c); SetDefaultSubFileNames:
       runtime */
    N->tt_p2 = V("force_tlchg$");
    build_tool_table(N);
    N->comm_mode = comment_in_postblock(r) ? 1 : 0;
    if (mp_run_override_md_cd) N->cd_loaded = mp_run_override_md_cd(r) == 1;
    if (N->posttype != 3) set_mtolerance(N, r->env.metric > 0);   /* SetMtolerance(GetIsMetric) */
    /* CPostInitPreDefs::ReInitializeArgsFromMDandCD: runtime */
    /* OverrideInitializeFromPost @1801111d0 */
    V("param_read$") = N->cd_loaded ? 1.0 : 3.0;
    /* pst_mode$ = GetMcamPostMode, ApplyControlDefaultTypeValue, SetNciArrayEnum: runtime / NOT DECODED */
    if (!BC(r, "pprep$") && STATUS(r) == 6) goto done;
    /* OverrideInitializeFromPprep @180111290 */
    if (V("canceldc$") == 1.0) N->canceldc = 1;
    override_pprep_sub_level(N);
    /* OverrideInitializeFromPprep's tail: AssignOutputStringReInit(snamenc$ -> sprogname$) @180125ab0 (skipped when the
       string's +0x48 flag 0x100 is set), then MakeUpper (Ordinal_8263) when progname$ != 0 */
    {
        int sn = mp_str_find(r, "snamenc$"), sp = mp_str_find(r, "sprogname$");
        if (sn >= 0 && sp >= 0 && !(r->strs[sp].flags & 0x100)) {
            char *t = strdup(mp_get_str(r, sn) ? mp_get_str(r, sn) : "");
            if (V("progname$") != 0.0) for (char *q = t; *q; q++) *q = (char)toupper((unsigned char)*q);
            mp_str_set(r, sp, t);
            free(t);
        }
    }
    /* OpenNcFile / transchr$ translation table: runtime */
    int rl;
    do {
        rl = read_loop(N);
    } while (STATUS(r) == 5);    /* nci_rewind$: NOT DECODED beyond re-running the loop */
    if (rl || STATUS(r) != 0) {
        if (STATUS(r) != 6) BC(r, "ppost$");
        ok = 1;
    }
done:
    r->cmd_hook = NULL;
    r->nci = NULL;
    for (int i = 0; i < N->nrec; i++) free(N->rec[i].prm);
    for (int i = 0; i < N->ncom; i++) free(N->com[i].text);
    for (int i = 0; i < N->npar; i++) free(N->par[i].text);
    for (int i = 0; i < N->nraw; i++) free(N->raw[i].prm);
    free(N->raw); free(N->tl.tool); free(N->tl.fl); free(N->tl.dist);
    free(N->rec); free(N->com); free(N->par);
    sub_free(N);
    free(N);
    return ok;
}
