/* mcce_dyn.h - port of Mastercam 2017's mcce.dll Dynamic Mill engine (MCCECalcDynPock), as decoded in
 * ../formats/mcce_engines.md (cited as §x) from the decompile text decomp/mcce_all.c (cited as l.N).
 * Plan, substitutions and the list of NOT DECODED stubs: ../formats/mcce_port_plan.md. */
#ifndef MCCE_DYN_H
#define MCCE_DYN_H
#include <stdint.h>
#include "offset.h"

/* CDynPockParm (formats/dynamic.md "Operation -> engine parameters") plus the derived fields of CDynPockParmDer
   (FUN_180069560, §3 B.2). Field comments give the Parm offset. */
typedef struct {
    uint32_t flags;          /* +0x08 mode bits (§3 B.1) */
    int start_sel;           /* +0x0c start-point selector (8 = explicit point, Peel) */
    double start_x, start_y; /* +0x10 / +0x18 */
    int order_mode;          /* +0x20 (op 0x16bf) */
    double R, r;             /* +0x28 tool radius, +0x30 corner radius */
    double eng;              /* +0x38 engagement angle (rad) */
    double min_rad;          /* +0x40 min toolpath radius */
    double micro_lift;       /* +0x48 */
    double approach;         /* +0x50 approach distance */
    double sliver;           /* +0x58 = 20 x chain tolerance */
    double first_off;        /* +0x60 first pass offset */
    double plunge_ang;       /* +0x68 rad */
    double pitch;            /* +0x70 entry pitch */
    double top;              /* +0x78 top of stock (absolute) */
    double depth;            /* +0x80 cut level (absolute) */
    double zclear;           /* +0x88 entry Z clearance */
    double helix_r;          /* +0x90 */
    double troch_r;          /* +0x98 */
    double slot_add;         /* +0xa0 additional slot width */
    double fp_red;           /* +0xa8 first pass feed reduction % */
    double f_retract, f_back, f_ramp, f_cut, f_first, f_conv;   /* +0xb0 .. +0xd8 */
    double gap;              /* +0xe0 gap size */
    double ct_t0, ct_t1, ct_t2;   /* +0xe8 / +0xf0 / +0xf8 */
    int ct_n; double ct_w;   /* +0x100 / +0x108 */
    double chain_tol;        /* +0x110 */
    /* derived (CDynPockParmDer) */
    int climb;               /* +0x1b0 = flags & 1 */
    double ramp_len;         /* +0x1b8 = 10.666 R */
    double d1c0, d1c8, d1d0; /* +0x1c0 / +0x1c8 / +0x1d0 */
    double var_rad;          /* +0x1d8 */
    double step;             /* +0x1e0 march step */
    double step15;           /* +0x1e8 = 1.5 step */
    double pi_minrad;        /* +0x1f0 = pi x min radius */
    double s_work, s_10, s_170, s_025;   /* +0x1f8 / +0x200 / +0x208 / +0x210 */
    uint8_t reg_read;        /* +0x21c */
    uint8_t assert_on, var_rad_avail, bull_avail, write_matmod;   /* +0x21d .. +0x220 */
    double min_cut_rad;      /* +0x228 dMinCutRad (rl) */
    double rend_ang;         /* +0x230 dRendAngDeg */
    uint8_t bands, cut_ord_len, air_cut_v3, rate_of_eng;          /* +0x238 .. +0x23b */
    double air_dwell;        /* +0x240 */
    int call_num, num_red_pass;   /* +0x248 / +0x24c */
    double dis_trans_ovr, dis_trans_tot;   /* +0x250 / +0x258 */
    uint8_t old_gsl_cut_ord, ctour_guide;  /* +0x260 / +0x261 */
} McceParm;

/* one engine output record (CCetp record, §3 D 4.1): entity with absolute Z, CeFlags, move flags, feed */
typedef struct {
    char id;                 /* 'L' line, 'A' arc */
    double x1, y1, x2, y2;   /* line */
    double cx, cy, r, sa, sw;   /* arc */
    double z0, z1;           /* absolute Z at the start / end of the entity */
    uint8_t ce;              /* CeFlags (+0x12): 1 rapid, 2 back feed, 4 link, 8 entry, 0x10 sweep, 0x20 first pass, 0x40 state 0/1 */
    uint32_t mf;             /* move flags (+0x90) */
    double feed;
} McceRec;

/* one input boundary (pock_bound): closed loop (or open chain), island flag */
typedef struct { double *xy; int n; int island; int open;
                 int ne; char *ety; double *ent; } McceBnd;   /* ne > 0: the bound's own entities (ety 'L' / 'A', ent 5 doubles each: L x1 y1 x2 y2,
                                                                 A cx cy r sa sw) - mcmill hands MCCECalcDynPock pock_bounds of lines and arcs
                                                                 (formats/dynamic.md "2D HS input"); xy stays their chord polyline */

/* LoadDynPockParm (mcmill FUN_1801af380) on an operation record (entity offsets, OP_ID record); metric = part units */
void mcce_parm_from_record(McceParm *p, const uint8_t *rec, uint32_t n, int metric);
/* MCCECalcDynPock (l.12159): motion / material / third boundary sets -> records (malloc'd) */
void mcce_bnd_free_ents(McceBnd *b);           /* frees ety / ent (not xy) */
void mcce_thread_release(void);             /* ours: free the calling thread's engine caches (a worker thread before it ends) */
void mcce_abort_set(int on);                     /* ours: 1 = stop (Cancel, from another thread); 0 = run (the caller resets it) */
int mcce_abort_get(void);
/* mcce PeelAesthetics FUN_1800df710 (formats/mcce_engines.md §1.2): reshapes the Peel boundaries A / B (one shape each); 1 = replaced */
int mcce_peel_aesthetics(double R, double mr, double px, double py, double chain_tol, const McceBnd *A, int na, const McceBnd *B, int nb,
                         McceBnd **ao, int *nao, McceBnd **bo, int *nbo);
/* mcmill FUN_1801ac7e0 (decomp/mcmill_hst2d.c l.17287-17320): each avoidance chain grown on its own by MCCECalcOffset(+d)
   (our port); outputs flagged island, chain direction kept; chord = arc linearisation tolerance of the output polylines */
int mcce_grow_islands(double d, double chord, const McceBnd *in, int n, McceBnd **out, int *nout);
int mcce_wall_region(double d, int from_out, double chord, const McceBnd *reg, McceBnd **out, int *nout);   /* mcmill FUN_1801ac7e0, region */
int mcce_calc_dyn_pock(const McceParm *p, const McceBnd *s1, int n1, const McceBnd *s2, int n2, const McceBnd *s3, int n3,
                       McceRec **out, int *nout, char *err, int errn);
#endif
