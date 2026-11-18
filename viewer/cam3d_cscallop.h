/* cam3d_cscallop.h - Constant Scallop (op 43) stage B: the scanner mcmsurf FUN_1801ea7e0 (formats/surf_classic3.md 12.3, 12.8, 12.12, 12.13).
 * Ported so far: the scanner's tool / tolerance setup, phase A (FUN_1801e8360: the machine-model triangles offset to the tool centre ->
 * ID2371.tmp + their ERRCHK.tmp groups; FUN_1801e7e20: the edge / corner groups), the corner arc table (FUN_1801da800 / FUN_1801d9e20 /
 * FUN_1801db440) and, per raster line, phase B's section up to the piece batch: FUN_1801daa60 (all group kinds the op meets),
 * FUN_1801db8c0 = FUN_1801dba80 (triangles: FUN_1801dc360 / FUN_1801dbc50), the edge cylinders (FUN_1801ed160 -> FUN_1801e1ac0 /
 * FUN_1801e2030; activation FUN_1801df420 + the edge quads FUN_1801e0ae0) and the corner spheres (FUN_1801ec6b0; FUN_1801df380), the
 * pieces in FUN_1801e40c0's order (cam3d_cscallop_edge.c, cam3d_cscallop_sect.c). NOT PORTED yet: the envelope merge FUN_1801e7350 /
 * FUN_1801e5200 and everything after it (FUN_1801e02b0, FUN_1801e0680, FUN_1801ed9b0); triangles without normals (kinds 4 / 5: not on
 * the op 43 path, the scanner's param_2 is 1). */
#ifndef CAM3D_CSCALLOP_H
#define CAM3D_CSCALLOP_H
#include <stddef.h>

/* one ERRCHK.tmp record (0x20 bytes, FUN_1801e8360 local_198..local_180): +0 xmin, +8 xmax, +0x10 byte offset in ID2371.tmp, +0x14
   count, +0x18 kind (4 / 5: triangles without normals, 10 / 11 with normals; even = drive surface, odd = check surface) */
typedef struct { double xmin, xmax; int off, n; unsigned char kind; } CsGroup;

/* one piece of a line (the 0x58-byte record FUN_1801dbc50 / FUN_1801dc360 hand to DAT_180501d90 = FUN_1801e40c0): +0 code, +8 / +0x10 the
   two ends' line parameter (frame y), +0x18 / +0x20 their z, +0x28 / +0x40 their normals */
typedef struct { short code; double s[2], z[2], n[2][3]; } CsPiece;

/* an active triangle (the 0xa0-byte node of FUN_1801df620: +8 v[3], +0x50 n[3], +0x98 kind) */
typedef struct CsTri { struct CsTri *next; double v[3][3], n[3][3]; unsigned char kind; } CsTri;
/* an active edge (the 0xb0-byte node of FUN_1801e0d10 / FUN_1801e1050, list +0x80, free +0x88): +8 P1, +0x20 P2, +0x38 unit direction,
   +0x50 length, +0x58 / +0x60 x range, +0x68 / +0x70 the ellipse's first point (a, b), +0x78 / +0x80 cos / sin of its turn in the (y, z)
   plane, +0x88 number of steps, +0x90 / +0x98 / +0xa0 the step's cos and scaled sines, +0xa8 kind (0 / 1, or 8 / 9 for an edge across x) */
typedef struct CsEdge { struct CsEdge *next; double P1[3], P2[3], dir[3], len, xmin, xmax, a, b, c78, s80; unsigned short nst; double c90, s98, sa0;
                        unsigned char kind; } CsEdge;
/* an active corner (the 0x58-byte node of FUN_1801df380, list +0x100, free +0x108): +8 P, +0x20 d1, +0x38 d2, +0x50 kind (2 / 3) */
typedef struct CsCorner { struct CsCorner *next; double P[3], d1[3], d2[3]; unsigned char kind; } CsCorner;
/* the corner arc table (DAT_180501dc0, FUN_1801da800 / FUN_1801d9e20 / FUN_1801db440): nlev levels of the sphere of radius r, per level
   its circle radius, point count and unit half-circle points */
typedef struct { int nlev; double r, step, cdev; int cnt[100]; double rad[100]; int off[100]; double *pts; int npts; } CsArcs;

/* a growing byte buffer (the scan / wall files as written) */
typedef struct { unsigned char *p; size_t n, cap; } CsBuf;

/* a line-list node ("estore", DAT_1804ea4e0, 0x68 bytes): +0 flag, +8 s0, +0x10 z0, +0x18 n0, +0x30 flag, +0x38 s1, +0x40 z1, +0x48 n1,
   +0x60 code, +0x62 next index (-1 = none); the wall store (DAT_180501d50) holds the same records */
typedef struct { unsigned char f0; double s0, z0, n0[3]; unsigned char f1; double s1, z1, n1[3]; short code, next; } CsNode;

typedef struct CsScan {
    /* the scanner's globals (FUN_1801ea7e0 l.326181-326290) */
    double tol;               /* param_9 */
    double t90;               /* DAT_1804ea560 = 0.9 tol */
    double half;              /* DAT_1804ea568 = (tol - 0.9 tol) / 2 */
    double e;                 /* DAT_1804ea570 = 0.01 x 0.9 tol */
    double systol;            /* DAT_1804ea578 = min(system tol, 0.0001 x 0.9 tol) */
    double cdev;              /* DAT_1804ea580 (corner deviation) */
    double s;                 /* DAT_1804ea598 */
    double off, hoff;         /* DAT_1804ea588 / DAT_1804ea590: the normal / horizontal offset */
    char off_on, hoff_on;     /* DAT_1804ea4f3 / DAT_1804ea4f6 */
    double off_drive, hoff_drive, off_chk, hoff_chk;   /* DAT_1804ea5a0 / 5a8 / 5b0 / 5b8 */
    int mode;                 /* DAT_180501d4c: the model's side mode (mms +0x64) and the pass flips (0 / 1 / 2 / 4); static 0 */
    double pos;               /* DAT_1804ea5c0: the current line position (frame x) */
    double lo, hi;            /* the line window +0x120 / +0x128 = pos -+ 2 e */
    /* ID2371.tmp (as doubles) and ERRCHK.tmp */
    double *tf; size_t ntf, ctf;
    int toff;                 /* FUN_1801e8360's *param_13: the byte offset */
    CsGroup *g; int ng, cg;
    /* phase B state (the scanner's local_31c8 block) */
    int first, first_next;    /* +0x118 / +0x11c */
    CsTri *act, *pool;        /* +0x60 active list (push-front), +0x68 free nodes */
    CsPiece *pc; int npc, cpc;   /* the batch DAT_1804ea640 (DAT_1804d5fb8 - 1 = 999 records) in call order */
    int nflush;               /* number of FUN_1801e7350 calls the batch would have made */
    /* the edge / corner groups (FUN_1801e7e20): tmpedg0.tmp (0xb0 records) and tmpcrn0.tmp (0x70 records), each surface's records
       sorted in place; group offsets are byte offsets in them */
    unsigned char *edg, *crn; size_t nedg, ncrn, cedg, ccrn;
    double ctol;              /* mccore ?tol@@3NA (*tol_exref): the system tolerance */
    CsEdge *eact, *epool;     /* +0x80 / +0x88 */
    CsCorner *cact, *cpool;   /* +0x100 / +0x108 */
    double *b90; int n90, c90; /* the work point arrays +0x90 / +0xc0 (FUN_1801ea2e0 / FUN_1801e77c0) */
    double *bc0; int nc0, cc0;
    CsArcs arc;               /* DAT_180501dc0 (drive surfaces; DAT_1805025f0 for check surfaces is not built: no check surfaces) */
    /* the line list (cam3d_cscallop_merge.c) */
    CsNode *est;              /* DAT_1804ea4e0 */
    short ecap;               /* DAT_1804ea5e4 (min(0x7fff, pl2MemBufRemainFit); N13-28: 0x7fff) */
    short ehead, etail, efree, nfree;   /* DAT_1804ea5f0 / 5fc / 600 / 604 */
    short hprev, hnode;       /* the search hint DAT_1804ea5f4 / 5f8 */
    char hint_on;             /* DAT_1804ea55f (FUN_1801e7cd0's argument) */
    short eerr;               /* DAT_1804ea60c */
    CsPiece *batch, **bptr; short nbatch;   /* DAT_1804ea640 / DAT_1804ffe00 / DAT_1804ea614 */
    CsNode *wall; int nwall, cwall;         /* DAT_180501d50 / d40 / d44 */
    int (*wall_flush)(struct CsScan *);     /* FUN_1801ed9b0 when the wall store is full */
    short wmax;                             /* DAT_180501d48: the most walls of one line */
    int trace, trace_line;                  /* test drivers: cs_clean_runs prints "RUN line start end start-after" per run */
} CsScan;

/* FUN_1801ea7e0's setup (l.326181-326290): tol = param_9, p3 / p4 / p5 = param_3 / 4 / 5, corner_dev = msurf_corner_dev, systol = mccore
   tol. Returns 0, or -16 (error 0x27d8: not a ball tool; FUN_1801cfb60 then takes FUN_1801d6390, NOT PORTED) */
int cs_setup(CsScan *S, double tol, double p3, double p4, double p5, double corner_dev, double systol);
void cs_free(CsScan *S);
/* FUN_1801e99f0 / FUN_1801ea5f0 / FUN_1801e7db0 on one vertex (n is changed as the DLL changes its copy) */
void cs_vtx_offset(const CsScan *S, double P[3], double n[3]);
void cs_vtx_unoffset(const CsScan *S, double P[3], const double n[3]);
void cs_nrm_out(const CsScan *S, const double n[3], double out[3]);
/* FUN_1801e8360 on one fill block (np points / normals in the frame, nt triangles), side = param_6 (drive surface), has_n = param_1,
   mode = setup_mach_model_buffer's mms +0x64. Appends to S->tf / S->g. The DLL runs it per fill block; the block split depends on the
   licence-sized arena (N13-28: the port assumes the large buffer = one block per surface, surf_classic3.md 12.12). 0 ok */
int cs_phase_a(CsScan *S, int has_n, int side, int mode, int np, const double *pts, const double *nrm, int nt, const int *tri);
/* FUN_1801e7e20 for one surface: its ne tmpedg0 records (0xb0 bytes) and ne tmpcrn0 records (0x70 bytes) appended to S->edg / S->crn,
   each sorted (MSVC qsort, LAB_1801df300 by min(P1.x, P2.x) / LAB_1801df2e0 by P.x) and one ERRCHK group each (kinds 0 / 2 drive, 1 / 3
   check). Call before cs_phase_a for the surface, as the scanner does (l.326355-326360). */
void cs_edge_groups(CsScan *S, int side, int ne, const unsigned char *edg, const unsigned char *crn);
/* the arc table of the corner spheres (FUN_1801da800(100, r, cdev) l.326251, FUN_1801d9e20 l.326287, FUN_1801db440 l.326394) */
int cs_arc_setup(CsScan *S);
/* the groups sorted as the scanner does (qsort LAB_1801df2c0 by xmin, MSVC CRT qsort) */
void cs_sort_groups(CsScan *S);
/* one line at position pos, window [lo, hi]: FUN_1801daa60 (triangle groups only) + FUN_1801dba80. Pieces are appended to S->pc in call order.
   Returns 0, -1 (a group of a kind not ported: edges 0 / 1, corners 2 / 3, triangles without normals 4 / 5) */
int cs_line_tris(CsScan *S, double pos, double lo, double hi, const char **why);
/* the whole per-line section (FUN_1801daa60 + FUN_1801db8c0 without its final FUN_1801e7350): triangles, edges, corners. 0, or -1 */
int cs_line(CsScan *S, double pos, double lo, double hi, const char **why);
int cs_line_act(CsScan *S, double pos, double lo, double hi, const char **why);   /* FUN_1801daa60 alone */
int cs_line_sect(CsScan *S, const char **why);                                    /* FUN_1801db8c0 but its FUN_1801e7350 */
void cs_edge_bytes(const CsEdge *e, unsigned char out[0xb0]);
void cs_corner_bytes(const CsCorner *c, unsigned char out[0x58]);
/* FUN_1801dbc50 / FUN_1801dc360 alone (unit checks) */
int cs_tri_section(CsScan *S, unsigned char kind, const double V[3][3], const double (*N)[3], int i0, int i1, int i2);
int cs_tri_span(CsScan *S, const CsTri *t);
void cs_piece_bytes(const CsPiece *p, unsigned char out[0x58]);
/* the line list (cam3d_cscallop_merge.c): LAB_1801e5190 (all nodes free), FUN_1801e7cd0 (the hint), LAB_1801e4460 (the line's nodes
   returned), and per line the batch FUN_1801e40c0 + FUN_1801e7350 (MSVC qsort, FUN_1801e5200 per piece) on S->pc: 0, or -1 (estore error) */
int cs_est_init(CsScan *S, int cap);
void cs_est_hint(CsScan *S, int on);
void cs_est_release(CsScan *S);
int cs_merge_line(CsScan *S);
void cs_node_bytes(const CsNode *n, unsigned char out[0x68]);
/* FUN_1801e02b0 (DAT_180501d60): the line list cleaned (FUN_1801e9ec0 short nodes, FUN_1801e3190 runs, FUN_1801e23f0 straight stretches) */
void cs_clean_line(CsScan *S);
/* FUN_1801e0680 (DAT_180501d78): the line's block appended to out (the scan file at byte offset *off; table = the per-line int table) */
int cs_line_write(CsScan *S, int flag, CsBuf *out, int *off, int *table, int line, double pos);
/* FUN_1801ed9b0: the line's walls appended to out (plmvcs00.tmp at *off; table = its nl x 8-byte line table) */
int cs_wall_write(CsScan *S, CsBuf *out, int *off, unsigned char *table, int line, double pos);
/* ---- stage C part 1 (cam3d_cscallop_lines.c, formats/surf_classic3.md 12.16): FUN_180133f10 before its FUN_1801cfb60 call ---- */
typedef struct { double x[3], z[3], y[3]; } CsFrame;   /* DAT_1804e8dd8 / DAT_1804e8df0 / DAT_1804e8e08 */
/* the machine-model files FUN_18025dd50 / FUN_180261f40 / FUN_18012f9c0 read and rewrite, in memory */
typedef struct {
    unsigned char *mm3; size_t n3;    /* tmpmm3.tmp: +0 short 0x1c, +2 entry count, a name line, a flag byte, 0x96 bytes, rows a / b / c, entries */
    unsigned char *rec; int nrec;     /* tmpmm.tmp: the 0x78-byte records */
    int cnt2;                         /* tmpmm2.tmp's record count */
    unsigned char *edg; size_t nedg;  /* tmpedg0.tmp */
    unsigned char *crn; size_t ncrn;  /* tmpcrn0.tmp */
    int flag;                         /* tmpmm3's byte after the name line */
    int turned; double ang;           /* FUN_180261f40 ran, its angle */
} CsMM;
typedef struct { double lo4, hi5, lo6, hi7; char f8, f9; } CsExt;   /* FUN_18012f9c0's param_4..param_9 */
void cs_s1_frame(int mode, double cut_angle, const double V[9], CsFrame *F);
double cs_s1_step(int method, int optype, const unsigned char *op, double D, double chord, double *slice, double *pct, double *colaps_dist);
int cs_mm_frame(const double V[9], const double z17[3], double a[3], double b[3], double c[3]);
int cs_mm_reframe(CsMM *mm, const double *p1, const double *p2, const double *p3, const double V[9], int *count, const char **why);
int cs_extents(const CsMM *mm, double d, double stock, int n, double chord, CsExt *E);
void cs_s1_lines(double step, const CsExt *E, int *n, double *first, double *step_out);
typedef struct { double chord, cdev, slice, cut_angle, pct, stock, D, r, gap; } CsOp43;   /* msurf_map's values for op 0x2b (gap = msurf_max_short_gap) */
void cs_op43_params(const unsigned char *op, double nc_tol, CsOp43 *P);
/* stage C part 2 (12.17): the scan file's header (FUN_180229640) and the scanner's trailer (l.326713-326734) */
void cs_scan_header(unsigned char h[0x38], int a, int b, int c);
void cs_scan_trailer(CsBuf *out, const CsScan *S, const double F[9], double D, double first, double step, int p18, int n, const int *table);
void cs_scan_appendix(CsBuf *out, const char *name, int cnt2, int head2, int ne, const unsigned char *ent, const double tols[9], int opid,
                      int ent30, const unsigned char *op, const unsigned char *tool, const double itool[13]);
/* ---- stage C item 3 (cam3d_cscallop_cfb.c, formats/surf_classic3.md 12.19): FUN_1801cfb60's per-line stages ---- */
typedef struct { double t9238, t9240, t9248, t9250, t9258, systol; } CsCfbConst;   /* DAT_1804e9238 .. 9258, mccore tol */
typedef int (*CsSegFn)(void *ctx, int code, const unsigned char fl[2], double s[2], double z[2], double n[6]);
int cs_cfb_err(int c);
void cs_cfb_consts(int kind, double chord, double systol, double cdev, CsCfbConst *C);
int cs_code_vert(int code);
int cs_seg_vertical(double tol, double systol, int code, const double s[2], const double z[2], const double n[6]);
int cs_cfb_walk(const unsigned char *f, size_t len, int pos, CsSegFn seg, void *ctx, double *p8, int *cap);
int cs_cfb_table(const unsigned char *f, size_t len, int **table);
int cs_code_afd0(int code); int cs_code_a310(int code); int cs_code_f3f30(int code); int cs_code_bc40(int code);
int cs_code_af60(int code); int cs_code_b0e0(int code); int cs_code_f6c0(int code);
/* FUN_1801cfb60's line list (DAT_1804e9328 ..): CsNode store */
typedef struct { CsNode *nd; int cap; short head, tail, freeh, nfree, last, err; } CsEst;
int cs_est_init_cfb(CsEst *L, int cap);
void cs_est_reset(CsEst *L);
void cs_est_free_line(CsEst *L);
int cs_est_insert(CsEst *L, const CsCfbConst *C, int code, const unsigned char fl[2], double s[2], double z[2], const double n[6]);
int cs_est_array(const CsEst *L, CsNode **out);
void cs_cfb_clean(CsEst *L, const CsCfbConst *C);
void cs_cfb_clean_short(CsEst *L, const CsCfbConst *C);   /* FUN_1801d7da0 alone (FUN_1801974e0 first step) */
void cs_clean_runs(CsScan *S);
void cs_conn_codes(int n, CsNode *arr);
int cs_join_runs(int p1, double tol, int n, CsNode *arr);
int cs_cfb_walls(int off, int count);
int cs_cfb_to_array(const CsEst *L, const CsCfbConst *C, int flag1, int nvcs, int cap, CsNode *arr);
int cs_cfb_to_array_v(const CsEst *L, const CsCfbConst *C, int flag1, int nvcs, CsNode *vcs, int cap, CsNode *arr);
/* the walls (cam3d_cscallop_wall.c, surf_classic3.md 12.22) */
short cs_wall_split(const double pts[4], double d, int n, const CsNode *v, double tol, CsNode *A, CsNode *B, CsNode *C, CsNode *out);
short cs_wall_connector(const double pts[4], int n, CsNode *v, const CsCfbConst *C, CsNode *out);
int cs_wall_read(const unsigned char *vcs, size_t vlen, const unsigned char *t98, int line, CsNode *pool, double *lp);
int cs_cfb_from_array(CsEst *L, const CsCfbConst *C, int n, const CsNode *arr);
/* FUN_18018fd80 (cam3d_cscallop_fd80.c): the 0x58-byte array node of FUN_1801cad70 (cs_fd_node_bytes = the DLL's layout) */
typedef struct { short link; unsigned char flag; double pos, s, z, na[3], nb[3]; short ca, cb, a, b; } CsFdNode;
typedef struct { int dir, pits, n, nruns, ncorners, n3, spread; } CsFdResult;
void cs_fd_node_bytes(const CsFdNode *n, unsigned char out[0x58]);
double cs_v_angle_3d(const double *a, const double *b);
double cs_fd_limit(int k, const CsCfbConst *C, int optype, double op12d0);
int cs_fd_dir(const CsEst *L, const CsCfbConst *C);
int cs_fd_mark_pits(CsEst *L, const CsCfbConst *C, short code);
void cs_fd_drop_code(CsEst *L, short code);
int cs_fd_straights(short nruns, int n, CsFdNode *arr, double tol, double ang, unsigned char *mark);
int cs_fd_corners(int dir, short nruns, CsFdNode *arr, double dmax, double dmin, double ang, double d6, double t9250, double tol, short *list);
int cs_fd_flags_back(CsEst *L, short nruns, const CsFdNode *arr);
int cs_fd_spread(CsEst *L, const CsCfbConst *C, unsigned char f);
/* test drivers: called after each step of cs_fd_endflags (stage 0 pits, 1 array, 2 straights, 3 corners, 4 flags back) */
extern void (*cs_fd_trace)(int stage, const CsEst *L, const CsFdNode *arr, int n, const short *list, int k, int v);
int cs_fd_endflags(CsEst *L, const CsCfbConst *C, double pos, int optype, double op12d0, CsFdNode *arr, short *list, unsigned char *mark,
                   CsFdResult *R);
int cs_rgn_block(const CsEst *L, const CsCfbConst *C, int line, double pos, int *off, int *table, CsBuf *out);   /* FUN_1801a1a70 */
/* the temp.rgn frame (cam3d_cscallop_rgn.c, formats/surf_classic3.md 12.21) */
typedef struct { double s, b9288, a9290, d9148; unsigned char c9298, c9299; } CsRgnTool;   /* DAT_1804e9288 .. 9299, DAT_1804e9148 */
void cs_rgn_tool(double stock, double r, double D, double t9248, CsRgnTool *T);
int cs_rgn_vcs_copy(CsBuf *out, const unsigned char *vcs, size_t vlen, int n, unsigned char *t98, short *a4);   /* FUN_1801c9350 */
void cs_rgn_trailer(CsBuf *out, const double F[9], const CsCfbConst *C, const CsRgnTool *T, double D, double t9230, double t9228, int p21,
                    int n, const int *table, short a4, const unsigned char *t98);
/* stage D1 (cam3d_cscallop_emesh.c, formats/surf_classic3.md 12.25): FUN_18018e630, temp.rgn -> tmpemesh.tmp */
typedef struct {
    const unsigned char *f; size_t len;            /* temp.rgn (ctx +0x368 / +0x428 = &DAT_1804e9080) */
    int nA, nB;                                    /* +0x338 / +0x3f8 */
    double firstA, stepA, lastA, firstB, stepB, lastB;   /* +0x340 / 348 / 350, +0x400 / 408 / 410 */
    double sA[2], sB[2];                           /* +0x358 / 360, +0x418 / 420 */
    const int *tabA, *tabB;                        /* +0x370, +0x430 */
    unsigned char kindA, kindB;                    /* +0x399, +0x459 */
} CsEmCtx;
typedef struct CsEmTrace {   /* test drivers: called on the line list after FUN_1801a1f60 ("EL") / FUN_180182800 ("EC"), the array, each FUN_1801a9150 */
    void (*list)(struct CsEmTrace *, const char *tag, int line, const CsEst *L);
    void (*arr)(struct CsEmTrace *, const char *tag, int line, const CsNode *a, int n);
    void (*ext)(struct CsEmTrace *, int line, int kind, int ret, int cnt, const double out[3]);
} CsEmTrace;
int cs_em_ctx(const unsigned char *f, size_t len, CsEmCtx *X, double tl6[4], const char **why);
int cs_em_dir(const CsEmCtx *X, const CsCfbConst *C, int dirB, int tei, int pool, CsEst *L, CsNode *arr, CsNode *work, int *otab,
              CsBuf *out, double *pos, int *toff, CsEmTrace *T, const char **why);
int cs_em_pool(int b6, int a4_6, int b7, int a4_7, int nA, int nB);
int cs_em_tei(int optype, int v1268, int swap, int cutmeth);
/* stage D2 part (cam3d_cscallop_seg.c, surf_classic3.md 12.27): FUN_18018a640's 0xb0 segment records (pointers as indices, -1 = none) and
   0x20 line headers */
typedef struct CsSeg { int next, prev; long long z10, z18; unsigned char b20; int line, p28, p2c; short s30, s32; int i34, i38; short z3c;
                 long long pt, cd, fl;   /* +0x40 / +0x48 / +0x50: FUN_1801b88e0's point / code / flag array entries (index, -1 = NULL) */
                 short s58, s5a, s5c, s5e; double v60, v68, s0, z0, s1, z1, v90, v98, sa0, sa8;
                 int sn, sp; unsigned char fr;   /* not record bytes: freed by FUN_1801a9c80(1, ..) (fr), its +0 / +8 then (sn / sp, left so by the DLL) */
                 unsigned char rf; int rud, rui;   /* not record bytes: a freed record's memory taken again by FUN_1801b8870 (rf) for the port
                                                      record (rud, rui) (CsFree, surf_classic3.md 12.41) */
               } CsSeg;
typedef struct { int first, last; short n10, n12; unsigned char dir; double pos; } CsSegLine;
int cs_code_5e70(int m, int code);
int cs_seg_dir(const CsEmCtx *X, double tol, int dir, int p4, int p5, double p6, double p7, int p9, CsSeg *S, CsSegLine *H, int *n25,
               short *err);
void cs_seg_bytes(const CsSeg *R, unsigned char o[0xb0]);
/* stage D2 (surf_classic3.md 12.29): FUN_1801b4e80, FUN_1801a7fd0's in-memory arrays, FUN_1801b88e0, FUN_1801b84f0 */
typedef struct { double (*pt)[2]; short *cd; unsigned char *fl; int n4, n5, n6; } CsSegPts;
void cs_seg_sizes(const CsEmCtx *X, int dir, short *mx, int *tot);
int cs_seg_pts_alloc(CsSegPts *P, int tot);
void cs_seg_pts_free(CsSegPts *P);
int cs_seg_points(const CsEmCtx *X, int nsegs, CsSeg *S, CsSegPts *P);
void cs_seg_flip(const double sc[2], CsSegLine *H, int n, CsSeg *S, CsSegPts *P);
/* stage D2 (cam3d_cscallop_d15.c, surf_classic3.md 12.30): FUN_18015d0f0's callees; one direction's records (growable: the 0xb0 pool),
   line headers and point arrays */
/* the DLL's free list of 0xb0 records (DAT_1804e97b8, LIFO: FUN_1801a9c80(1, ..) pushes, FUN_1801b8870 in FUN_1801ab500 pops; empty = the
   FUN_1801b86c0 pool) as the port's (direction, index) of each freed record; occ[X] = the port record now in the memory of direction X's
   record 0 (header n of X reads it, surf_classic3.md 12.34) */
typedef struct { int (*t)[2]; int n, cap; int occ[2][2];
                 int n0[2]; int (*pool)[2]; int np, pcap; struct CsSeg **S[2];   /* the original records per direction (array slots 0..n0-1); the k-th record taken
                                                               from the FUN_1801b86c0 pool (empty free list) = pool[k] (surf_classic3.md 12.41) */ } CsFree;
typedef struct { CsSeg *S; int n, cap; CsSegLine *H; int nl; CsSegPts P; int dir; CsFree *fl; } CsSegDir;
int cs_cls_7620(int cls, int code);
int cs_d15_split(CsSegDir *D, int b2b0, short *err);   /* FUN_180150840 */
int cs_d15_merge(CsSegDir *D, double gap);            /* FUN_18014d5c0 */
int cs_d15_gap(const CsSegDir *D, double gap, int p4, int r);   /* FUN_180156cf0 */
void cs_d15_free(CsSegDir *D, int r);                           /* FUN_1801a9c80(1, ..) */
/* FUN_180149a20 (cam3d_cscallop_9a20.c, surf_classic3.md 12.32): the crossing pass over both directions' records. X = the line frames
   (ctx +0x338.. / +0x3f8..), tol = mccore tol, g = ctx +0x2b8 (DAT_1804e9238), t9250 = DAT_1804e9250, gap = ctx +0x2d8; zl / z1 / z2 =
   DAT_18050341a / DAT_180503438 / DAT_1804d6fb8 (op +0x1280 / +0x1288 / +0x1290). Returns the error (0 fine; -100 NOT PORTED: a record
   without its in-memory point array, FUN_1801a7fd0's windowed arm); cnt[] gets the last pass's counters as the DLL keeps them */
typedef struct { CsSegDir *D[2]; const CsEmCtx *X; double tol, g, t9250, gap; int zl; double z1, z2; long cov[64];
                 int f30a;   /* ctx +0x30a = DAT_1804d569a: 1 from FUN_1801b1ad0 asm 1801b2a9f (kind 0x11) before FUN_18015d0f0 */ } CsLoop;
int cs_d15_9a20(CsLoop *L, int *passes);
int cs_d15_insert(CsSegDir *D, int p3, const CsSeg *L, int at);   /* FUN_1801ab500 */
int cs_9a20_up(CsLoop *L, int dir);
void cs_9a20_pt(CsLoop *L, int side, int dir, int r, double P[3]);
int cs_9a20_zok(const CsLoop *L, const double P[3]);
int cs_9a20_lines(CsLoop *L, const double P[3], int *a, int *b, int *c, int *d);
short cs_9a20_walk(const CsLoop *L, double x, short k0, const double P0[2], short k1, const double P1[2], double (*pts)[2], short *idx,
                   double O[2], double *d);
/* FUN_180146e60(dir, ctx, abort) (cam3d_cscallop_6e60.c, surf_classic3.md 12.34): every record of the direction cut where it crosses the
   other direction's lines (FUN_1801470e0). Returns the error (0 fine; -4 an array full; -100 NOT PORTED) */
int cs_d15_6e60(CsLoop *L, int dir, long *cov);
double cs_d15_c8f0(const CsLoop *L);   /* FUN_18015c8f0 (ctx +0x309 == 0) */
/* stage D5, the zone builder FUN_180187850 (cam3d_cscallop_zone.c, surf_classic3.md 12.36) */
int cs_zb_count(CsSegDir *D);                                       /* FUN_1801bc5c0 */
void cs_zb_code0(CsSegDir *D, int r, short code, int *cnt);         /* FUN_1801c3490 (+ FUN_1801c34b0) */
void cs_zb_code1(CsSegDir *D, int r, short code, int *cnt);         /* FUN_1801c3470 */
void cs_zb_ends(CsSegDir *D, double t9250, int *cnt);               /* FUN_1801c0320 (FUN_1801bec50, FUN_1801c1c40) */
void cs_zb_clear(CsSegDir *D, short code, int *cnt);                /* FUN_1801c32f0 */
void cs_zb_clear_stall(CsSegDir *D, const short *L, short code, int *cnt);   /* FUN_1801beff0 */
void cs_zb_stall_set(short *L, short c, short v);                   /* FUN_1801bf0d0 */
int cs_zb_stall_on(const short *L, short c);                        /* FUN_1801bd8a0 */
void cs_zb_stall_init(short *L, short m);                           /* FUN_1801be460 */
int cs_zb_c1c40(const CsSegDir *D, int up, const double key[2], int line);           /* FUN_1801c1c40 */
int cs_zb_bec50(const CsSegDir *D, int cl, int up, const double key[2], int line);   /* FUN_1801bec50 */
/* stage D5 / Z1 (cam3d_cscallop_zbuf.c, surf_classic3.md 12.38): the zone buffer (the builder's zonbuf_ctrl local_9a18) and its files */
typedef struct { unsigned char *p; size_t n, cap, pos; } CsZFile;                       /* a SysFile on memory */
typedef struct { int i0; double p[3]; int rdir, rec; unsigned char b28, b29; unsigned short e2a; short s2c; } CsZStep;   /* 0x30 step */
typedef struct { int cnt; short pass, side; unsigned char one; } CsZHdr;               /* +0x7c / +0x9c: the 12-byte zone header */
typedef struct CsZbc {
    short zone, pass, side;              /* +0x18 / +0x1a / +0x1c */
    unsigned char fopen, zsh;            /* +0x1e / +0x1f */
    short draw, err;                     /* +0x20 / +0x22 */
    double d28, tol, t9250, t9238;       /* +0x28 / +0x30 / +0x38; DAT_1804e9238 (FUN_1801a9460) */
    double box[6];                       /* +0x40..+0x68 */
    CsZFile f[2];                        /* +0x70 tmpprojz.tmp, +0x90 tmpdztmp.tmp */
    int spos[2];                         /* +0x78 / +0x98 */
    CsZHdr h[2];                         /* +0x7c / +0x9c */
    int n88[2];                          /* +0x88 / +0xa8 */
    int ac, cap, b4, b8, bc, c0, last[2];   /* +0xac, +0xb0, +0xb4, +0xb8, +0xbc, +0xc0, +0xc4 / +0xc8 */
    CsZStep *st; unsigned char *fl; double (*e0)[3];   /* +0xd0 / +0xd8 / +0xe0 */
    int frec, fdir; short f0;            /* +0xe8 / +0xf0 */
    void (*trace)(struct CsZbc *, const char *name, unsigned long ra, int arg, const CsZStep *s);   /* test drivers */
    void *user;
} CsZbc;
int cs_zb_buf_init(CsZbc *Z, int cap, int fopen, unsigned char f9ffd, short draw, double d28, double tol, double t9250, double t9238);
void cs_zb_buf_free(CsZbc *Z);
void cs_zf_rec(long long v, int *dir, int *r);
void cs_zb_reset(CsZbc *Z, unsigned long ra);                                     /* FUN_1801aa460 */
void cs_zb_start(CsZbc *Z, short zone, short pass, short side, unsigned long ra);   /* FUN_1801ab410 */
short cs_zb_side(const CsZbc *Z, CsSegDir *const *DD, int n, const CsZStep *st);   /* FUN_1801ad550 */
int cs_zb_flush(CsZbc *Z, CsSegDir *const *DD, unsigned long ra);                 /* FUN_1801aa5e0 */
int cs_zb_add(CsZbc *Z, CsSegDir *const *DD, const CsZStep *s, unsigned long ra);   /* FUN_1801a7d60 */
void cs_zb_headers(CsZbc *Z, unsigned long ra);                                    /* FUN_1801a95e0 */
int cs_zb_check(CsZbc *Z, unsigned long ra);                                       /* FUN_1801a9460 */
void cs_zb_drop(CsZbc *Z, short zone, unsigned long ra);                           /* FUN_1801adec0 */
int cs_zb_95c0(double tol, CsSegDir *const *DD, const CsZStep *S0, const CsZStep *S1, int flag, short *t);   /* FUN_1801695c0 */
/* stage D5 / Z2 (cam3d_cscallop_zcore.c, surf_classic3.md 12.39): the walk point (0x48 bytes: +0 record, +8 its header array, +0x10 end,
   +0x18 point, +0x30 the cell's four lines, +0x40 corner, +0x41) and the shared core */
typedef struct { int rdir, rec, hd; short end; double P[3]; int ln[4]; unsigned char b40, b41; } CsWPt;
int cs_zc_first(const CsZbc *Z, int end, int dir, int rec);                                        /* FUN_1801c24f0 */
int cs_zc_cell(CsLoop *L, const double P[3], int ln[4], unsigned short cell[2]);                    /* FUN_1801716d0 */
int cs_zc_beside(CsLoop *L, const double P[3], const double v[3], int ln[2][4], unsigned short cell[2][2]);   /* FUN_18016e860 */
int cs_zc_pair(CsLoop *L, double d, CsWPt *W1, CsWPt *W2, unsigned char *out);                      /* FUN_18016eb80 */
void cs_zc_box(int mode, const double *A, const double *B, int widen, double w, double out[4]);   /* FUN_1801b6000 */
int cs_zc_skip(CsLoop *L, int up, int hd, int dir, int rec, const double box[4]);                  /* FUN_1801aed20 */
int cs_zc_outcode(const double box[4], const double P[2]);                                         /* FUN_1802537e0 */
/* the rest of Z2 (surf_classic3.md 12.41): FUN_1801aaea0's tree. Records as (direction, index); a freed record's memory followed to the
   port record that took it again (cs_zc_res); ND (-101) = NOT PORTED (a header / line index past the headers, header n's bytes +0x10..) */
void cs_zc_res(const CsLoop *L, int *dir, int *rec);
int cs_zc_next(const CsLoop *L, int d, int r, int prev, int *nd);   /* a record's +0 (prev: +8), freed ones as they were */
int cs_zc_mem(const CsLoop *L, int kind, int a, int b, int *dir, int *rec);   /* "F d i" (kind 0) / "P k" (kind 1) -> the record there now */
int cs_zc_ovl(const CsLoop *L, int a, int up, int rd, int r, int qd, int q);                       /* FUN_1801bfbd0 */
int cs_zc_adj(const CsLoop *L, int up, int rd, int r, int line, int n, int hd, int *q);            /* FUN_1801c2a10 */
int cs_zc_ibox(const CsLoop *L, double w, int rd, int r, int q, int hd, double box[4]);            /* FUN_1801aeb30 */
int cs_zc_edge(int code, const double box[4], const double seg[4], double I[2], double *u, double *t);   /* FUN_1801aba60 */
int cs_zc_clip(const CsLoop *L, const double seg[4], double w2, int rd, int r, int inc, int n, int hd);  /* FUN_1801ab730 */
int cs_zc_seg(CsLoop *L, int mode, double w, int dir, const double A[3], const double B[3]);       /* FUN_1801aaea0 */
void cs_d15_clamp(const CsEmCtx *X, const double P[3], int *a, int *b, int *c, int *d);           /* FUN_1801c65e0 */
int cs_9a20_ent(CsLoop *L, int side, int dir, int r);   /* FUN_18014a950: 1 when the entry is made on the record, 0 not, -100 not ported */
void cs_d15_boxlines(CsLoop *L, const double box[4], int out[4]);                                  /* FUN_1801aad80 / FUN_1801c9520 */
/* stage D5 / Z3 (cam3d_cscallop_zwalk.c, surf_classic3.md 12.42): the walk FUN_1801bca00. The candidate / walk entry (0xa8 bytes): +0 byte,
   +0x20, +0x30 point, +0x48 point - walk point, +0x60 end code, +0x68 distance, +0x70 / +0x78 angles, +0x80 / +0x88, +0x90 record, +0x98
   its headers, +0xa0 direction, +0xa2 end */
typedef struct { unsigned char b0, b8, b28, b60; short s2, s2c; int i4, ln[4];   /* +0 / +8 / +0x28 / +0x60 bytes, +2 / +0x2c shorts, +4 cell,
                                                                                    +0xc..+0x1b its lines */
                 long long q20; double p[3], dl[3], d68, a70, a78, p80, p88; int rdir, rec, hd; short dir, end;
                 unsigned char b1c, b1d, b29, b2a;   /* +0x1c corner, +0x1d its code, +0x29 / +0x2a the neighbours' codes (FUN_18016a7b0);
                                                        +0x20 also holds a double there (FUN_180175dc0), kept in q20's bytes */ } CsZCand;
int cs_zw_prev(const CsZbc *Z, double tol, int i);                                                    /* FUN_1801bed30 */
int cs_zw_endcode(const CsLoop *L, const short *SL, int rd, int r, int end, int mode);               /* FUN_1801ab6d0 */
int cs_zw_ends(const CsLoop *L, int hd, int rd, int r, const double box[4], double out[6]);          /* FUN_1801aef30 */
double cs_zw_turn(double tol, int side, const double a[3], const double b[3]);                       /* FUN_1801c5ef0 */
int cs_zc_cellab(CsLoop *L, double d, const double *A, const double *B, int ln[4], unsigned short cell[2]);   /* FUN_180179f30 */
int cs_zw_cell2(CsLoop *L, double d, const CsZCand *W, const CsZCand *C, int ln[2][4], unsigned short cell[2][2]);   /* FUN_18017a2c0 */
int cs_zw_cells(CsLoop *L, double d, const CsZCand *W, int max, short *cnt, CsZCand *arr, short *nc, int *err);      /* FUN_18016fb60 */
int cs_zw_choose(double tol, double g, double c320, double c330, double w, int n, const CsZCand *arr);   /* FUN_18015bf40 */
int cs_zw_code4(unsigned char c);                                                                    /* FUN_18017c2e0 */
int cs_zw_cands(CsLoop *L, const short *SL, int mode, int side, const CsZCand *W, const double v[2], const double box[4],
                const double zr[2], int dir, int max, short *cnt, CsZCand *arr);                      /* FUN_1801bf450 */
int cs_zw_side2(const CsLoop *L, short side, const CsZCand *W, const CsZCand *C, int *err);           /* FUN_1801bfdc0 (+ FUN_18015a1c0) */
int cs_zw_pick(const CsLoop *L, short side, const CsZCand *W, int n, const short *idx, CsZCand *arr, int *err);   /* FUN_18015c2e0 */
int cs_zw_group(CsLoop *L, CsZCand *W, int n, CsZCand *const *in, short *cnt, short *out, int *err);   /* FUN_18016a7b0 */
int cs_zw_group2(CsLoop *L, short side, double c2f0, int c309, short max, CsZCand *W, int n, CsZCand *const *in, int *err);   /* FUN_180179090 */
int cs_zw_score(CsLoop *L, const CsZbc *Z, int p1, int p3, int dir, int i, const CsZCand *W, int r7, short p8, double d, CsZCand *E, int *err);   /* FUN_180180090 */
/* the zonbuf pool (zbc +0x10) and FUN_18017daa0's cell triangulation (surf_classic3.md 12.46): a vertex identity (FUN_18017e180's 0x18
   keys: kind 0 = a cell corner (A line, B line), 1 = a record end (the record's address order, end)), a node's end pair (+0x70 / +0x80:
   record, +0xa0 / +0xa2 bytes), the 0xa0 edge node, a 0x38 triangle, a 0xfb8 block, a 0x28 vertex */
typedef struct { unsigned char t; long long x, y; } CsZId;
typedef struct { int rdir, rec; unsigned char dir, end; } CsZPr;
typedef struct { int next; unsigned char b8; double pt[2][3]; CsZId key[2]; CsZPr pr[2]; int t90, t98; } CsZNode;
typedef struct { double *slot[3]; int node[3]; } CsZTri;
typedef struct { int id; unsigned short cell[2]; short cap, used, ntri; double pt[0x32][3]; CsZTri tri[0x32]; } CsZBlk;
typedef struct { unsigned char t; double *slot; const CsZCand *ent; short s18; double d20; } CsZVert;
typedef struct { CsZNode *nd; int total, used, head, tail; short nmax, nblk, cur, vcap, vcnt; CsZBlk blk[9]; CsZVert V[0x32];
                 long long *rk[2]; int rkn[2], rknp; } CsZPool;
void cs_zw_pool_init(CsZPool *P);                                                                     /* FUN_18015d200(1, 9, 0x32, 1000) */
void cs_zw_pool_free(CsZPool *P);
void cs_zw_pool_reset(CsZPool *P);                                                                    /* FUN_18017d8d0 */
int cs_zw_tri(CsLoop *L, CsZPool *P, const short *SL, int mode, const CsZCand *W, int n, CsZCand *arr, short *out);   /* FUN_18017daa0 */
/* FUN_1801bca00's outputs (surf_classic3.md 12.47): found (a record returned), p10w (p10 written), e100 (zbc +0x100 set to -16), the record,
   p10 byte, p11 angle, p12 / p13 (+0xa0 / +0xa2), p14 point */
typedef struct { int found, p10w, e100, rdir, rec; unsigned char p10; double p11; unsigned short p12, p13; double p14[3]; } CsZWOut;
int cs_zw_walk(CsLoop *L, const CsZbc *Z, CsZPool *P, const short *SL, int p1, int p2, int p3, int mode, int i, int p7, double p8,
               double c2f0, int c309, CsZWOut *o, int *err);                                          /* FUN_1801bca00 */
#endif
