/* mcsrfint_p.h - the internals of mcsrfint.c (the MaCurve / MaLoop / MaFace objects and the helpers) shared with mcsrftrim.c (the face trimming core).
 * Every shared name gets the mi_ prefix through a macro. */
#ifndef MCSRFINT_P_H
#define MCSRFINT_P_H
#include "mcsrfint.h"
#include <stdint.h>
#define span mi_span
#define bders_n mi_bders_n
#define bders mi_bders
#define verify_normal mi_verify_normal
#define offset_ev mi_offset_ev
#define g_tr_inbase mi_g_tr_inbase
#define dot mi_dot
#define len mi_len
#define unit mi_unit
#define d3 mi_d3
#define has_deg mi_has_deg
#define has_seams mi_has_seams
#define limit_uv mi_limit_uv
#define ext_eval mi_ext_eval
#define seg_dist mi_seg_dist
#define boundary_uvpt mi_boundary_uvpt
#define uv_on_edge mi_uv_on_edge
#define grid_build mi_grid_build
#define estimate_uv mi_estimate_uv
#define box_add mi_box_add
#define box_init mi_box_init
#define box_expand mi_box_expand
#define box_plane mi_box_plane
#define iso_set mi_iso_set
#define iso_eval mi_iso_eval
#define iso_tangent mi_iso_tangent
#define iso_hunt mi_iso_hunt
#define iso_closed mi_iso_closed
#define plane_intersect mi_plane_intersect
#define pvi mi_pvi
#define bound_dir mi_bound_dir
#define unit_normal_ev mi_unit_normal_ev
#define duvdt mi_duvdt
#define uv_near_pole mi_uv_near_pole
#define lastn mi_lastn
#define tangent_plane mi_tangent_plane
#define inv3 mi_inv3
#define du_allow mi_du_allow
#define dv_allow mi_dv_allow
#define dt_allow mi_dt_allow
#define curve_intersect mi_curve_intersect
#define final_intersect mi_final_intersect
#define line_pmd mi_line_pmd
#define march mi_march
#define plane_init mi_plane_init
#define init_cut mi_init_cut
#define init_cut_ss mi_init_cut_ss
#define reverse_cut mi_reverse_cut
#define march_top mi_march_top
#define best_fit mi_best_fit
#define cub_ev mi_cub_ev
#define cub_pmd mi_cub_pmd
#define cub_store mi_cub_store
#define curve_fit mi_curve_fit
#define gen_ulist mi_gen_ulist
#define to_c1_nurb mi_to_c1_nurb
#define sharp_nodes mi_sharp_nodes
#define to_g0g1_nurb mi_to_g0g1_nurb
#define to_nurb mi_to_nurb
#define weed mi_weed
#define patch_list mi_patch_list
#define unique_knots mi_unique_knots
#define iso_tan_at mi_iso_tan_at
#define iso_degenerate mi_iso_degenerate
#define iso_sharp_t mi_iso_sharp_t
#define is_smooth mi_is_smooth
#define cmp_d mi_cmp_d
#define slope_list mi_slope_list
#define plane_one mi_plane_one
#define part_curves mi_part_curves
#define mc_eval mi_mc_eval
#define mc_ends mi_mc_ends
#define mc_closed mi_mc_closed
#define mc_degenerate mi_mc_degenerate
#define mc_reverse mi_mc_reverse
#define sq3 mi_sq3
#define mc_shares_both mi_mc_shares_both
#define mc_neighbor mi_mc_neighbor
#define mc_idx mi_mc_idx
#define mc_last mi_mc_last
#define mc_all mi_mc_all
#define mc_regen_knots mi_mc_regen_knots
#define mc_concat mi_mc_concat
#define mc_join_into mi_mc_join_into
#define g_arena mi_g_arena
#define ta mi_ta
#define ta_free mi_ta_free
#define g_chord_dev mi_g_chord_dev
#define g_chain_tol mi_g_chain_tol
#define cv_periodic mi_cv_periodic
#define cv_eval mi_cv_eval
#define cv_dirty mi_cv_dirty
#define dist3 mi_dist3
#define cv_set_range mi_cv_set_range
#define cv_set_ofs mi_cv_set_ofs
#define pl_trim mi_pl_trim
#define cv_trim mi_cv_trim
#define cv_closed_info mi_cv_closed_info
#define cv_closed mi_cv_closed
#define limit_t mi_limit_t
#define cv_bin mi_cv_bin
#define cv_hunt mi_cv_hunt
#define cv_box mi_cv_box
#define box_box mi_box_box
#define cv_pmd mi_cv_pmd
#define cv_pt mi_cv_pt
#define cv_start mi_cv_start
#define cv_end mi_cv_end
#define cv_mid mi_cv_mid
#define pl_pmd mi_pl_pmd
#define pl_closed mi_pl_closed
#define pl_box mi_pl_box
#define pl_plane mi_pl_plane
#define sort3d mi_sort3d
#define uv_in_pl mi_uv_in_pl
#define cv_new mi_cv_new
#define cv_copy mi_cv_copy
#define loop_copy mi_loop_copy
#define pc_of_poly mi_pc_of_poly
#define loop_poly mi_loop_poly
#define loop_dirty mi_loop_dirty
#define loop_box mi_loop_box
#define loop_closed mi_loop_closed
#define loop_start mi_loop_start
#define loop_end mi_loop_end
#define loop_pmd mi_loop_pmd
#define uv_rel mi_uv_rel
#define cv_len mi_cv_len
#define cv_param_at mi_cv_param_at
#define sample_start mi_sample_start
#define sample_end mi_sample_end
#define td_new mi_td_new
#define lid_add mi_lid_add
#define pld_add mi_pld_add
#define line_int mi_line_int
#define pl_pl mi_pl_pl
#define cc_int mi_cc_int
#define pc_full_coinc mi_pc_full_coinc
#define cv_align mi_cv_align
#define cv_overlap mi_cv_overlap
#define pc_partial mi_pc_partial
#define loop_of mi_loop_of
#define loop_concat mi_loop_concat
#define uv_dist mi_uv_dist
#define concat_loops mi_concat_loops
#define loop_trim mi_loop_trim
#define loop_travel mi_loop_travel
#define loop_coinc mi_loop_coinc
#define loop_loop mi_loop_loop
#define trim_to_loop mi_trim_to_loop
#define face_active mi_face_active
#define trim_to_face mi_trim_to_face
#define pc_of_march mi_pc_of_march
#define cub_move mi_cub_move
#define loop_move mi_loop_move
#define loop_snap mi_loop_snap
#define uv_ends_close mi_uv_ends_close
#define flo_refine mi_flo_refine
#define flo_add mi_flo_add
#define flo_corner mi_flo_corner
#define flo_linear mi_flo_linear
#define flo_join mi_flo_join
#define flo_break mi_flo_break
#define flo_dir mi_flo_dir
#define flo_pjoin mi_flo_pjoin
#define flo_concat mi_flo_concat
#define flo_loop mi_flo_loop
#define face_uvminmax mi_face_uvminmax
#define pl_area mi_pl_area
#define pl_winding mi_pl_winding
#define face_check mi_face_check
#define bz_blossom mi_bz_blossom
#define regen_dir mi_regen_dir
#define unit_normal2 mi_unit_normal2
#define surf_box mi_surf_box
#define u_beyond mi_u_beyond
#define v_beyond mi_v_beyond
#define adjust_uv mi_adjust_uv
#define all_ci mi_all_ci
#define iso_high_seam mi_iso_high_seam
#define ss_init mi_ss_init
#define uv_near_seam mi_uv_near_seam
#define ss_main mi_ss_main
#define cos_eval mi_cos_eval
#define maline_pmd mi_maline_pmd
#define binary_uv mi_binary_uv
#define facet_pc mi_facet_pc
#define cubic_of mi_cubic_of
#define pc_wrap mi_pc_wrap
#define pc_poly_cubic mi_pc_poly_cubic
#define cubic_line mi_cubic_line
#define loop_srf mi_loop_srf
#define pc_valid mi_pc_valid
#define loop_valid mi_loop_valid
#define refacet mi_refacet
#define pc_break mi_pc_break
#define pc_truncate mi_pc_truncate
#define loop_move_tr mi_loop_move_tr
#define loop_new mi_loop_new
#define g_refused mi_g_refused
#define refuse mi_refuse
typedef struct { ISrf *s; int dir; double par, t0, t1; double *hu; int nh; double box[6]; int hasbox; int ci, cl; } Iso;   /* ci / cl: GetClosedInfo cached (MaCurve +0x23 / +0x24, reset by SetDirty) */
typedef struct { double a[3], b[3], c[3], d[3]; } Cub;   /* x(t) = a t^3 + b t^2 + c t + d */
typedef struct { int n; double (*p)[2]; double *u; int hasbox; double box[6]; } IPoly;   /* MaPolyLine (dim 2): Pts, UList, the box */
typedef struct ICv {
    int kind;   /* 0 MaCubicSpline (uv), 1 MaNURBSpline degree 1 (uv, a face edge), 2 MaNURBSpline (model space, the whole fitted cut), 3 MaCurveOnSurface, 4 MaParameterCurve */
    int metric; double t0, t1, dt, ofs, tmin, tmax;   /* MaCurve +0x28 +0x30 +0x38 +0x110, TMin/TMaxAllowable */
    int ci, cl, per; int nh; double *hu, (*hp)[3]; int hasbox; double box[6];   /* +0x23 +0x24 +0x25, +0x50 the hunting points, +0x48 the box */
    int nseg; double *C, *U;         /* kind 0: MaCubicSpline +0x128 +0x130 +0x138 */
    int deg, ncp, nk; double *K, *P, *W; /* kind 1, 2: order - 1, +300, +0x130, knots +0x148, Verts +0x140 (x y z; W the 4th component, NULL = 1: a
                                         non-rational curve's subdivided vertices keep the w the subdivision gives, MaNURBSpline::Break) */
    struct ICv *inner; ISrf *s;      /* kind 3 (+0x130 the curve, +0x128 the surface), kind 4 */
    IPoly poly; double tol;          /* kind 4 +0x138, +0x140 */
} ICv;
typedef struct { int n; ICv **pc; IPoly *lp; int hasbox; double box[6]; int surf; } ILoop;   /* MaLoop +0x10 +0x8 +0x18 +0x20; surf: byte +0 (MaLoop(MaSurface*) 0x18042e630: the surface's boundary, UVPointRelative against the surface's range) */
struct MiFace { ISrf *s; ILoop *outer; ILoop **inner; int ninner; double uvbox[4]; int reparam; int ext, f9; };   /* ext: MaFace +8 (extend the cut to the surface's edges, MaTrimSurf +0x3c), f9: +9 */
typedef struct { int n; int *a, *b; double *ta, *tb; int cap; } LID;   /* MaLoopIntersectData */
typedef struct { int n; int *a; double *t; } LTD;    /* MaLoopTrimData */
typedef struct ANode { struct ANode *next; } ANode;
typedef struct { int n, cap; double (*p)[3]; } PPts;
typedef struct { int n, cap; int *a, *b; double (*p)[3]; } PLD;   /* MaPolyLineIntersectData */
typedef struct { int n; double (*p)[2]; } FPoly;
int span(const double *K, int n, int deg, double t);
void bders_n(int p, const double *K, double u, int s, int nmax, double N[][26]);
void bders(int p, const double *K, double u, int s, double N[3][26]);
int verify_normal(const ISrf *s, const double *E, int *count, double *u, double *v);
void offset_ev(const ISrf *s, int order, double u, double v, double *E);
extern int g_tr_inbase;
double dot(const double *a, const double *b);
double len(const double *a);
void unit(double *v);
double d3(const double *a, const double *b);
int has_deg(ISrf *s);
int has_seams(ISrf *s);
void limit_uv(const ISrf *s, double *u, double *v);
void ext_eval(ISrf *s, int order, double u, double v, double *E);
double seg_dist(const double a[2], const double b[2], const double p[2], double *tout);
void boundary_uvpt(ISrf *s, const double p[2], int *e, double *dist);
int uv_on_edge(ISrf *s, double u, double v);
void grid_build(ISrf *s);
void estimate_uv(ISrf *s, const double *P, double *u, double *v);
void box_add(double b[6], const double *p);
void box_init(double b[6]);
void box_expand(double b[6], int metric);
int box_plane(const double b[6], const double *pl);
void iso_set(Iso *c, ISrf *s, int dir, double par);
void iso_eval(const Iso *c, int order, double t, double *o);
void iso_tangent(const Iso *c, double t, double *p, double *T);
void iso_hunt(Iso *c);
int iso_closed(const Iso *c);
void plane_intersect(Iso *c, int hunt, const double *pl, int cap, double *t, int *n);
int pvi(const double pl[4], const double p[3], const double d[3], double o[3]);
void bound_dir(ISrf *s, const double uv[2], const double dir[2], int *e, double out[2]);
void unit_normal_ev(const double *E, double *o);
void duvdt(double *E, const double *P, const double *T, double *du, double *dv);
int uv_near_pole(ISrf *s, const double *p);
extern int lastn[2];
void tangent_plane(ISrf *s, uint8_t *c, int side, double *P, double *plane, int *st);
int inv3(const double *a, double *o);
double du_allow(const ISrf *s);
double dv_allow(const ISrf *s);
double dt_allow(const Iso *c);
int curve_intersect(ISrf *ss, const Iso *iso, double p2, double p3, double *x, double dt38);
void final_intersect(ISrf *s, ISrf *other, uint8_t *c, int side);
void line_pmd(const double *a, const double *b, const double *q, double *dist, double *t);
int march(ISrf *th, ISrf *dr, uint8_t *c, double **L558, double **L4b8, double **L560, double **L568, double **L518, double **L510, int *L508, char *L528, char *L3e8);
void plane_init(ISrf *s, const double *pl, int multi, double **seeds, int max, char *flags, int *nseed);
int init_cut(ISrf *s, MaInt *m);
int init_cut_ss(ISrf *s, MaInt *m);
void reverse_cut(MaInt *m);
int march_top(ISrf *s, MaInt *m);
void best_fit(const double *P0, const double *T0, const double *P1, const double *T1, Cub *C);
void cub_ev(const Cub *C, int order, double t, double *o);
void cub_pmd(const Cub *C, int hunt, const double *P, double *dist, double *tio);
void cub_store(double *dst, const Cub *C);
int curve_fit(double **P, double **T, int n, double tol, double *out);
void gen_ulist(const double *C, int nseg, double *U);
void to_c1_nurb(const double *C, int nseg, const double *U, MiCurve *o);
int sharp_nodes(const double *C, int nseg, const double *U, double *S);
void to_g0g1_nurb(const double *C, int nseg, const double *U, const double *S, int ns, MiCurve *o);
void to_nurb(const double *C, int nseg, const double *U, MiCurve *o);
void weed(ISrf *s, MaInt *m, double **seeds, double **seeds2, int i, int nseed);
int patch_list(const ISrf *s, int dir_v, double *o);
int unique_knots(const ISrf *s, int along_v, double *uq);
void iso_tan_at(ISrf *s, int dir, double par, double t, double *T);
int iso_degenerate(ISrf *s, int dir, double par);
int iso_sharp_t(ISrf *s, int dir, double par, double ang, double *o);
int is_smooth(ISrf *s, double ang);
int cmp_d(const void *a, const void *b);
int slope_list(ISrf *s, int vlist, double ang, double *o);
int plane_one(ISrf *s, MiFace *F, const double pl[4], double chord, double chord2, double maxstep, double minstep, int multi, MiCurve **res, int *ncurves, int *cap);
void part_curves(ISrf *piece, MiFace *F, MaInt *m, const MiCurve *whole, MiCurve **res_, int *ncurves_, int *cap_);
void mc_eval(const MiCurve *c, double t, double *P, double *D);
void mc_ends(const MiCurve *c, double *a, double *b);
int mc_closed(const MiCurve *c, double tol);
int mc_degenerate(const MiCurve *c, double tol);
void mc_reverse(MiCurve *c);
double sq3(const double *a, const double *b);
int mc_shares_both(const MiCurve *a, const MiCurve *b, double tol, int *rev);
MiCurve *mc_neighbor(MiCurve *a, MiCurve **L, int n, double tol, const int *used);
int mc_idx(MiCurve **L, int n, const MiCurve *c);
MiCurve *mc_last(MiCurve *a, MiCurve **L, int n, double tol, const int *used);
int mc_all(MiCurve *a, MiCurve **L, int n, double tol, int *used, MiCurve ***out);
void mc_regen_knots(MiCurve *c);
void mc_concat(MiCurve *a, const MiCurve *b);
void mc_join_into(MiCurve *a, MiCurve **rest, int nrest);
extern ANode *g_arena;
void *ta(size_t n);
void ta_free(void);
extern double g_chord_dev;
extern double g_chain_tol;
int cv_periodic(ICv *c);
void cv_eval(const ICv *c, int order, double t, double *o);
void cv_dirty(ICv *c);
double dist3(const double *a, const double *b);
void cv_set_range(ICv *c, double a, double b);
void cv_set_ofs(ICv *c, double v);
void pl_trim(IPoly *l, double a, double b, const double *ps, const double *pe);
void cv_trim(ICv *c, double a, double b);
void cv_closed_info(ICv *c, int *closed, int *periodic);
int cv_closed(ICv *c);
void limit_t(ICv *c, double *t);
int cv_bin(ICv *c, double ang, double *T);
void cv_hunt(ICv *c);
double *cv_box(ICv *c);
int box_box(const double *a, const double *b);
void cv_pmd(ICv *c, int hunt, const double *P, double *dist, double *foot, double *tio);
void cv_pt(const ICv *c, double t, double *o);
void cv_start(const ICv *c, double *o);
void cv_end(const ICv *c, double *o);
void cv_mid(const ICv *c, double *o);
void pl_pmd(const IPoly *l, const double *P, double *dist, double *foot, int *seg);
int pl_closed(const IPoly *l);
double *pl_box(IPoly *l, int metric);
void pl_plane(IPoly *l, const double *pl, int metric, PPts *o);
void sort3d(double (*p)[3], int n, int axis);
int uv_in_pl(IPoly *l, const double *p3, int metric, int wrt_y);
ICv *cv_new(int kind, int metric);
ICv *cv_copy(const ICv *c);
ILoop *loop_copy(const ILoop *l);
ICv *pc_of_poly(ISrf *s, const double (*p)[2], int n, double tol);
IPoly *loop_poly(ILoop *L);
void loop_dirty(ILoop *L);
double *loop_box(ILoop *L);
int loop_closed(const ILoop *L);
void loop_start(const ILoop *L, double *o);
void loop_end(const ILoop *L, double *o);
void loop_pmd(ILoop *L, const double *uv, double *dist, int *pidx, double *foot, double *t);
int uv_rel(ILoop *L, const double *uv);
double cv_len(const ICv *c, double a, double b);
int cv_param_at(const ICv *c, double dist, double t, double *out);
int sample_start(ILoop *L, ILoop *o);
int sample_end(ILoop *L, ILoop *o);
LTD *td_new(int n);
void lid_add(LID *d, int a, int b, double ta_, double tb);
void pld_add(PLD *d, int a, int b, const double *q);
void line_int(const double *A, const double *Ad, const double *B, const double *Bd, double *o0, double *o1, double *o2, double *t1, double *t2, int *st);
void pl_pl(const IPoly *a, const IPoly *b, PLD *out);
void cc_int(ICv *a, const double *lim, ICv *b, double *t, double tol, double *res, int *st);
int pc_full_coinc(ICv *a, ICv *b);
void cv_align(ICv *c, ICv *o);
int cv_overlap(ICv *th, ICv *o, double tol, double *r1, double *r2, double *dmax, double *davg, double *amax);
int pc_partial(ICv *pc, ILoop *L, double (**out)[2], int *n);
ILoop *loop_of(ICv **pc, int n);
void loop_concat(ILoop *a, const ILoop *b);
double uv_dist(const double *a, const double *b);
void concat_loops(ILoop ***R, int *n);
int loop_trim(const ILoop *L, const LTD *d, int *pn, ILoop ***pout, int concat);
int loop_travel(ILoop *L, const double *a, const double *b, int dir, ILoop **out);
int loop_coinc(ILoop *L, ILoop *o);
LID *loop_loop(ILoop *L, ILoop *o);
int trim_to_loop(ILoop *L, ILoop *o, int p2, LID *ld, int *pn, ILoop ***pout);
int face_active(MiFace *f, const double *uv);
int trim_to_face(ILoop *L, MiFace *F, int *pn, ILoop ***pout);
ICv *pc_of_march(ISrf *s, double **P, double **T, double **UV, int n, double tol);
void cub_move(double *C, const double *v, int end);
void loop_move(ILoop *L, const double *v, int end);
void loop_snap(ILoop *L, ILoop *o, ISrf *s);
void uv_ends_close(ISrf *piece, double **UV, int n);
void flo_refine(ISrf *s, FPoly *L, double tol);
void flo_add(FPoly **o, int *no, const double (*p)[2], int n);
int flo_corner(ISrf *s, double u, double v);
int flo_linear(const FPoly *L);
int flo_join(ISrf *s, const FPoly *a, const FPoly *b, double ang);
void flo_break(ISrf *s, const FPoly *L, double ang, FPoly **o, int *no);
void flo_dir(ISrf *s, const double *uv, const double *d, double *T);
int flo_pjoin(ISrf *s, const FPoly *a, const FPoly *b);
void flo_concat(ISrf *s, FPoly **o, int *no);
int flo_loop(ISrf *s, double (*pts)[2], int n, double chord, double ang, FPoly **o);
void face_uvminmax(MiFace *f);
double pl_area(const IPoly *l);
double pl_winding(const IPoly *l);
void face_check(MiFace *f);
void bz_blossom(const double *K, int ncp, int p, int s, const double *Q, const double *args, double *out);
void regen_dir(ISrf *s, int dir, MiRegen *R);
void unit_normal2(const double *G, double *o);
void surf_box(ISrf *s, double b[6]);
int u_beyond(const ISrf *s, double u);
int v_beyond(const ISrf *s, double v);
void adjust_uv(ISrf *s, double *u, double *v);
void all_ci(ISrf *s, Iso *iso, const double sb[6], int max, double (*out)[3], int *nout);
int iso_high_seam(const ISrf *s, int dir, double par);
void ss_init(ISrf *th, ISrf *dr, int multi, int max, double **a, double **b, char *flags, int *nseed);
int uv_near_seam(const ISrf *s, double a, double b);
void ss_main(ISrf *s, ISrf *dr, double chord, double chord2, double maxstep, double minstep, int multi, MiCurve **res_, int *ncurves_, int *cap_);
/* the march hook (mcsrftrim.c): MaSurfSurfIntersect::ComputeIntersectionsMain(true) builds part loops (vtable +0x28) instead of the intersection curves */
extern void (*mi_march_hook)(ISrf *piece, MaInt *m, void *ctx); extern void *mi_march_ctx;
/* the surf / surf march hook (mcsrftrim.c): pieces of A and B, the march (UV on A, UV2 on B) and its fitted NURBS */
extern void (*mi_ss_hook)(ISrf *pa, ISrf *pb, MaInt *m, const MiCurve *whole, void *ctx);
extern const char *g_refused; void refuse(const char *why);
void cos_eval(ISrf *s, const ICv *uvc, double t, double *P, double *uv);
void maline_pmd(const double *a, const double *b, const double *q, double *dist, double *t, double *foot);
int binary_uv(ISrf *s, const ICv *uvc, double tol, IPoly *out);
void facet_pc(ICv *q);
ICv *cubic_of(int nseg, double *C, double *U, int metric);
ICv *pc_wrap(ISrf *s, ICv *c, double tol);
ICv *pc_poly_cubic(ISrf *s, const double (*p)[2], int n, double tol2);
ICv *cubic_line(const double *a, const double *b, int metric);
ISrf *loop_srf(const ILoop *L);
int pc_valid(const ICv *q);
int loop_valid(const ILoop *L);
void refacet(ICv *q);
int pc_break(ICv *q, double t, int keep_first);
void pc_truncate(ICv *q);
void loop_move_tr(ILoop *L, const double *v, int end);
ILoop *loop_new(ICv **pc, int n);
#ifdef MI_TRACE
/* the trimmer-level trace (TRIMSS_TRIM=1 with TRIMSS_TRACE; tools/peloader mccore_trim.c "Z" lines, tools/tests/trimcore_cmp.py) */
#include <stdio.h>
extern FILE *mi_trf;
int mi_ztr(void);   /* the Z lines on */
extern int mi_zm_loop, mi_zec;
void mi_zq(int hunt, const double *P, double u, double v, double d);   /* ZQ: MaSurface::PointMinDist at the trimmer's call sites */
#define ZQ(h, P, u, v, d) mi_zq(h, P, u, v, d)
void mi_zl(const char *tag, const ILoop *L);
void mi_zf(const char *tag, const struct MiFace *f);
#else
#define ZQ(h, P, u, v, d) ((void)0)
#endif
#endif
