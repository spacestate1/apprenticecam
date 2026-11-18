/* cam2d - chaining and 2D toolpath generation (contour, pocket, drill) on McamFile geometry, plus G-code output. */
#ifndef CAM2D_H
#define CAM2D_H
#include <stdio.h>
#include "mcam_load.h"

/* the defaults (FUN_1803bc7d0) of chain_tol: metric 0.002 mm, inch 0.0001 in; tol 5e-5 in both */
#define CAM_CHAIN_TOL 0.002
#define CAM_CHAIN_TOL_INCH 0.0001
#define CAM_SYS_TOL (mc_systol.tol)           /* tol_exref (config 1000) */
static inline double cam_chain_tol(int metric) { return mc_systol_units(metric)->chain_tol; }   /* chain_tol (config 1001) */

typedef struct {
    int *ent; int *rev; int n;   /* entity indices (into F.ents) in chain order; rev = traversed end->start */
    int closed;
    int avoid;     /* Dynamic family: an avoidance region (Mastercam chain role 4, TP_CHN +0xe0), not cut */
    int role;      /* Dynamic family: the stored chain role (TP_CHN +0xe0): 0 machining, 4 avoidance, 5 entry, 6 air, 7 containment (PrepareChains decomp/mcmill_hst2d.c l.6152-6156).
                      3D surface ops store 2 = tool containment (msurf FUN_18001cff0 save_chain role 2), 3 = curves / blend chains (FUN_18001c4d0
                      role 3) - formats/surface_selection.md 2.4 (TpRef.role) */
    int32_t solid_idn;   /* > 0: a "Solid Chain" of that solid (its entities are generated, not file geometry) */
    int partial, branch; /* Chaining dialog flags: a partial chain (CHAIN +10), stopped at a branch point (CHAIN +9) */
    double cut0, cut1;   /* Dynamic: fraction of the first / last entity (as traversed) left out at the start / end */
    int sld;             /* a chain picked on a solid (the Chaining dialog's Solids mode, formats/chaining.md "Solid chaining"):
                            its entities are Ent.gen curves of the solid's edges; 2 = still in the solid selection (not ended) */
} Chain;

/* chain through connected entity endpoints starting at `seed` (in its own direction); stops at branches */
extern int cam_chain_color_filter, cam_chain_level_filter;   /* Options Mask Color / Level (config 6018 / 6019) */
extern int cam_chain_plane_filter, cam_chain_ignore_depths, cam_chain_start_pt;   /* C-plane radio, Options 2466 / 2468 */
extern double cam_chain_plane_z;                         /* the C-plane depth */
#define cam_chain_plane_tol (mc_systol.chain_plane_tol)   /* chain_plane_tol: config 1002 planar chaining tolerance (McSysTol) */
int cam_chain_point(const McamFile *f, int e, Chain *out);   /* Point mode: a chain of one point entity */
int cam_face_link_feed_on(void);   /* MCAM_FACE_LINK_FEED=1: Facing honours "Feed rate between cuts" (an F word; cam2d_face.c) */
int cam_chain_is_point(const McamFile *f, const Chain *c);
void cam_chain_start_at_point(const McamFile *f, Chain *c, double tol);   /* Options Start chain at point (6009) */
void cam_chain_reverse_inner(const McamFile *f, Chain *cs, int n, double ptol);   /* Options Reverse inner chains (6005) */
int cam_chain_sort_nested(const McamFile *f, Chain *cs, int n, int mode, double ptol);   /* Options Sorting (6006) */
int cam_chain(const McamFile *f, int seed, double tol, Chain *out);
int cam_chain_single(const McamFile *f, int seed, double tol, Chain *out);   /* Chaining dialog Single mode: one entity */
int cam_chain_partial(const McamFile *f, int e1, double px, double py, int e2, double tol, Chain *out);   /* Partial mode */
void cam_chain_move_start(Chain *c, int forward);   /* Start Back (0) / Forward (1), closed chains */
int cam_chain_window(const McamFile *f, const char *sel, double sx, double sy, double tol, int dir_ccw, int zigzag,
                     int keep_open, int keep_closed, Chain *out, int max);   /* Window / Polygon chaining */
int cam_chain_area(const McamFile *f, double px, double py, double tol, int dir_ccw, int keep_open, int infinite,
                   Chain *out, int max);   /* Area chaining */
int cam_chain_order(const McamFile *f, Chain *cs, int nc, double sx, double sy, int zigzag, Chain *out, int max);
void cam_chain_set_dir(const McamFile *f, Chain *c, int ccw);   /* closed chain to CW / CCW */
int cam_chain_extend(const McamFile *f, Chain *c, int pick, double tol);   /* continue at the branch point (4269) */
int cam_chain_partial_more(const McamFile *f, Chain *c, int pick, double tol);   /* Partial + Wait: extend to a further pick */
int cam_chain_dynamic(const McamFile *f, Chain *c, int at_end, int pick, double px, double py);   /* Dynamic start / end */
int cam_chain_end_move(const McamFile *f, Chain *c, int at_end, int forward, double tol);   /* Start / End Back / Forward */
int cam_chain_vector(const McamFile *f, const char *crossed, const double *cross_xy, double sx, double sy, double tol,
                     int dir_ccw, int zigzag, int keep_open, int keep_closed, Chain *out, int max);   /* Vector chaining */
void cam_chain_orient(const McamFile *f, Chain *c, int seed, double px, double py);   /* cursor-position direction */
void cam_chain_free(Chain *c);
/* the chain as an XY polyline (arcs sampled to chordal tolerance); returns point count, z of the chain in *z */
int cam_chain_polyline(const McamFile *f, const Chain *c, double tol, double **xy, double *z);
int cam_chain_3d(const McamFile *f, const Chain *c);   /* a chain not flat in world Z (cam2d.c) */
/* CS-7 (formats/chaining.md "Curves outside the C-plane, splines and NURBS"): Mastercam's chain entity mask (lines, arcs, splines, NURBS in
   any plane) while cam_chain_all is set (an operation's chaining); cam_ent_chainable = the current rule, cam_ent_chainable_any = Mastercam's */
extern int cam_chain_all;
int cam_ent_chainable_any(const Ent *e);
int cam_ent_chainable(const Ent *e);
int cam_ent_tilted(const Ent *e);   /* a line / spline not parallel to XY, an arc of a tilted view */
int cam_ent_cplane(const McamFile *f, const Ent *e, double tol, double *depth);   /* FUN_1801fa6e0: parallel to the C-plane (Top) */
extern int cam_chain_pick_view; extern double cam_chain_pick_vm[6], cam_chain_pick_pk[2];   /* a click's view rows / cursor (view x / y) */
int cam_spline_samples(const McamFile *f, const Ent *e, double tol, double **ts, double **xyz);   /* E_SPLINE break points (malloc'd) */

enum { CAM_CONTOUR = 1, CAM_POCKET = 2, CAM_DRILL = 3, CAM_FACE = 4, CAM_SLOT = 5, CAM_HELIX = 6, CAM_CIRCLE = 7,
       CAM_THREAD = 8, CAM_DYNAMIC = 9, CAM_AREA = 10, CAM_DYNCONTOUR = 11, CAM_PEEL = 12, CAM_OPTIROUGH = 13, CAM_SRPOCKET = 14, CAM_SRPARALLEL = 15,
       CAM_WATERLINE = 16, CAM_SFPARALLEL = 17, CAM_RASTER = 18, CAM_AREAROUGH = 19, CAM_SPIRAL = 20, CAM_RADIAL = 21, CAM_SCALLOP = 22, CAM_SFCONTOUR = 23, CAM_HSTPROJECT = 24, CAM_SRPROJECT = 25, CAM_ENGRAVE = 26, CAM_PENCIL = 27, CAM_BLEND = 28, CAM_SRMPOCKET = 29, CAM_SRPLUNGE = 30, CAM_SFRADIAL = 31, CAM_SFPROJECT = 32, CAM_HORIZAREA = 33, CAM_SFPENCIL = 34, CAM_LFACE = 35, CAM_NTYPES = 36 };
/* CAM_LFACE (2026-11-16) = Lathe Face, opcode 66 (mclathe.dll CLFaceTp): regenerated from a stored op's record, or new from the lathe defaults record (cam_cli lface, L-6 stage 1; viewer/lathe_face.c,
   formats/lathe.md 6 / 7); no op dialog, not written to Mastercam files, not posted (decision P4) */
/* CAM_ENGRAVE (2026-10-26) = Engrave, opcode 416 (the engrave.dll C-hook and its EStamp7 engine, viewer/engrave.c; formats/engrave.md) */
/* 3drough (2026-10-24): CAM_OPTIROUGH = Surface High Speed (op 132) style 1 "Dynamic OptiRough", made here only (stored op 132
   stays as Mastercam made it); cam3d_rough.c, formats/hst3d_port_plan.md 6 */
/* srough (2026-10-24): CAM_SRPOCKET = classic Surface Rough Pocket (op 10), CAM_SRPARALLEL = Surface Rough Parallel (op 5), made
   here only (stored ops 10 / 5 stay as Mastercam made them); cam3d_srough.c, formats/surf_rough.md */
/* CAM_WATERLINE (2026-10-25) = Surface High Speed (op 132) style 2 Waterline, cam3d_finish.c; CAM_AREAROUGH = style 0 Area Roughing;
   CAM_SPIRAL / CAM_RADIAL / CAM_SCALLOP = styles 9 / 10 / 3 (2026-10-26); CAM_SFCONTOUR = classic Surface Finish Contour (op 15); CAM_HSTPROJECT = style 15 Project; CAM_SRPROJECT = classic Surface Rough Project (op 7) */
/* the op 132 UI style byte 0x16bc of the HST types whose default op is the first op 132 of that style (-1 = not one) */
static inline int cam_hst_style(int t) { return t == CAM_WATERLINE ? 2 : t == CAM_RASTER ? 5 : t == CAM_AREAROUGH ? 0 : t == CAM_SPIRAL ? 9 : t == CAM_RADIAL ? 10 : t == CAM_SCALLOP ? 3 : t == CAM_PENCIL ? 6 : t == CAM_HSTPROJECT ? 15 : t == CAM_HORIZAREA ? 4 : -1; }
static inline int cam_type_is_3d(int t) { return t == CAM_OPTIROUGH || t == CAM_SRPOCKET || t == CAM_SRPARALLEL || t == CAM_WATERLINE || t == CAM_SFPARALLEL || t == CAM_RASTER || t == CAM_AREAROUGH || t == CAM_SPIRAL || t == CAM_RADIAL || t == CAM_SCALLOP || t == CAM_SFCONTOUR || t == CAM_HSTPROJECT || t == CAM_SRPROJECT || t == CAM_PENCIL || t == CAM_BLEND || t == CAM_SRMPOCKET || t == CAM_SRPLUNGE || t == CAM_SFRADIAL || t == CAM_SFPROJECT || t == CAM_HORIZAREA || t == CAM_SFPENCIL; }
int cam_type_from_gallery(const char *gallery, const char *item, int occurrence);   /* a ribbon gallery item -> type (0 = greyed) */
const char *cam_type_label(int type);          /* "Contour", "Dynamic Mill", ... (Mastercam gallery names) */
int cam_type_from_label(const char *label);     /* 0 if not supported */
int cam_type_uses_holes(int type);              /* drill / helix / circle / thread: picks points or circles */
enum { SIDE_ON = 0, SIDE_OUTSIDE = 1, SIDE_INSIDE = 2 };   /* closed chains; for open chains 1 = left, 2 = right of the chain direction */

#define CAM_NRPATCH 256            /* record patches per op (ours): heights 13 + Tool page 6 + Edit Common 9 + the op dialog's
                                      template pages (cui_opdlg.c PF_NED, at most every live field of one op type with its
                                      partners, < 200); a patch that does not fit is reported (cam_rpatch_full), never dropped quietly */
void cam_rpatch_full(int off);   /* cam_fileop.c: the warning when an op has no room for another record patch */
typedef struct {
    int type;
    int tool_num; double tool_dia;
    int rpm; double feed, plunge_feed;
    double top_z;            /* top of material (absolute); defaults to the chain Z */
    double final_z;          /* bottom of the cut (absolute) */
    double stepdown;         /* max depth per pass */
    double clearance;        /* absolute Z for rapids between operations */
    double retract_z;        /* absolute Z for rapids between passes (Mastercam retract plane); below the feed plane = feed plane */
    double feed_plane;       /* incremental above top_z for rapid approach / retract between passes */
    int dep_inc, top_inc; double dep_cz;   /* record 0x1a8 depth / 0x2f0 top of stock incremental (op +0x140 / +0x288) and the chain Z
                                the incremental ones were resolved against (top_z / final_z = value + dep_cz): the 3D contour
                                (cam2d.c gen_contour3d, FUN_18005ef90) takes each chain's own start Z as mccore set_depths does */
    int side;                /* contour: SIDE_* */
    int roll;                /* roll the cutter around outside corners: 0 none, 1 sharp (turn >= 45 deg), 2 all: record 0x5d3
                                (u8, contour page 2938 control 1335, UpdateLocalOp FUN_180157e30) */
    double lead_sw;          /* lead arc sweep (rad); 0 = a quarter */
    int lead_mid;
    int lead_right;          /* runtime: the free side of the current pass is on the right (leads mirror to CW arcs) */            /* contour, closed chains with leads: enter at the midpoint of the first entity (Mastercam default) */
    int side_rel;            /* 1: side 1 / 2 = left / right of each chain's direction, closed chains too (Mastercam comp) */
    double stock_xy;         /* stock to leave on walls */
    double stepover_pct;     /* pocket: % of tool diameter */
    double ramp_deg;         /* pocket entry ramp angle along the first loop */
    int pocket_style;        /* 0 contour-parallel loops, 1 zigzag (Mastercam cutting method 0) */
    double pocket_angle;     /* zigzag direction (deg from X) */
    int feed_retract;        /* contour / pocket: record 0x1a9 (rapid retract, op +0x141) = 0 -> feed up at the retract rate
                                0x388 to the feed plane before rapids (mccore retract_move / clear_move, cam2d.c retract_up) */
    int pocket_no_rough;     /* record 0x1238 (P12045 "Roughing enabled") = 0: finish passes only (machine_pocket) */
    double helix_r, helix_zclr;   /* zigzag entry helix: radius (0 = tool diameter), start height above the top */
    double r_plane;          /* drill: absolute R plane */
    double peck;             /* drill: 1st peck (op +0x1230), used by cycles 1 / 2 */
    int drill_cycle;         /* drill: Mastercam cycle (op +0x1228): 0 Drill/Counterbore, 1 Peck Drill, 2 Chip Break, 3 Tap, ...
                                (mccoreres 7645 + 11 n); 0 / 1 / 2 are posted (G81 / G83 / G73); 3 / 4 and a dwell only with
                                MCAM_DRILL_CYCLES=1 (batch 8, nc_post.c) */
    double drill_dwell;      /* drill: dwell, record 0x1250 (P10112; bnci_wr_81 +0x38) - posted only with MCAM_DRILL_CYCLES=1 */
    double drill_bt;         /* drill: tip comp breakthrough amount, record 0x1260 (HSTDrillTipCompPage 1338; bnci_drill_depth
                                0x1800694d0 subtracts it with the tip length when tip comp 0x1268 is on) */
    double tol;              /* chordal tolerance for arcs / offsets */
    double lead_r;           /* lead-in/out arc radius (0 = none) */
    double lead_len;         /* straight lead before / after the arc */
    int lead_pct;            /* 1: lead_len / lead_r (and the exit's lead_xlen / lead_xr) follow the tool as % of D: bnci_ctour
                                l.394-406 (decomp/mcmill_ctour.c) recalculates each side's absolute values from its % with leads
                                (0x8f0) and that side (0x8f8 / 0x940) on, CToolProp::Recalc likewise */
    double lead_len_pct, lead_r_pct;   /* entry: record 0x930 / 0x938, line length / arc radius in % of the tool diameter */
    /* Lead In/Out 3013 / 2793 beyond the entry values above (lead_len = 0x900, lead_r = 0x910, lead_sw = 0x918, lead_mid =
       0x990): HSTLeadIOPageBase load FUN_180171320 / store FUN_1801735c0, the contour lead application FUN_180051990
       (decomp/mcmill_all.c l.55862) and the piece builder FUN_1801c24a0 (l.311657) */
    int lead_on, lead_in_on, lead_out_on;  /* 0x8f0 Lead In/Out, 0x8f8 Entry, 0x940 Exit */
    int lead_in_perp, lead_out_perp;       /* 0x8f9 / 0x941: the line along the normal (Perpendicular) instead of the tangent */
    double lead_xlen, lead_xr, lead_xsw;   /* exit line 0x948, arc radius 0x958, sweep 0x960 (rad) */
    double lead_xlen_pct, lead_xr_pct;     /* exit % of D 0x978 / 0x980 */
    int lead_sw_rec;                       /* bit 0 entry / bit 1 exit: the sweep came from the op record (cam_fileop.c LEADS) or a
                                              record patch (the dialog), where 0 = no arc (FUN_180051990 bVar22 / bVar24: sweep >
                                              1e-6); else (sidecar keys, cam_cli) 0 = a quarter (ours, lead_sw above) - review 2026-10-23 */
    double lead_ovl;                       /* 0x988 overlap (closed contours, FUN_180050a10) */
    int lead_fin_on, lead_fout_on; double lead_fin_v, lead_fout_v;   /* contour: Lead In/Out "Override feed rate" entry 0x1358 / 0x1350, exit
                                              0x1368 / 0x1360 (ctour_prm +0x130 / +0x128, +0x140 / +0x138: the writer FUN_180056be0 l.8139-8160 feeds
                                              the entry / exit pieces at them) */
    int mp_ff_on; double mp_ff_v;          /* contour: Multi Passes feed override 0x1464 / 0x1458 (ctour_prm +0x23c / +0x230): the finish passes' feed
                                              (contour loop 0x18005cfe7-0x18005d021, the writer l.8113-8119) */
    int mp_fin_rpm;                        /* contour: Multi Passes spindle override on 0x1465: writes nothing in 2017 (cam2d.c gen_contour; was refused until 2026-11-07) */
    int kd_ok;
    int pass_last_depth;                   /* runtime (gen_contour): this pass is on the last depth (Retract before last move: clear_move there) */                             /* runtime (gen_contour): keep the tool down into this pass (multipass 0x8c8 / depth 0x891) */
    int lead_skip;                         /* runtime (gen_contour): bit 0 no entry lead / bit 1 no exit lead on this pass (lead_xflags 0 / 2) */
    int lead_gouge;                        /* 0x991 gouge check: FUN_1801c46f0 runs only with it (here: the measured lead fit) */
    int lead_xflags;                       /* bit 0 0x928 enter on first depth cut only, 1 0x929 use entry point, 2 0x970 exit on last
                                              depth cut only, 3 0x971 use exit point, 4 0x992 plunge after first move, 5 0x993 retract
                                              before last move (Lead In/Out 3013; the pocket finish leads FUN_1801bdc00, batch 6),
                                              6 0x92a / 7 0x972 use point depth (entry / exit) */
    int pt_in, pt_out; double pt_x, pt_y, pt_z;   /* runtime (gen_contour): this pass starts / ends at the entry / exit point (cam2d.c pass) */
    int hz_on, hz_last, hz_opend; double hz_in;     /* runtime (gen_contour): incremental heights per pass (cam2d.c ht_rel; hz_in NAN = from the top) */
    int adj_s, adj_s_ext, adj_e, adj_e_ext;   /* contour adjust start / end of contour: ctour_prm +0xf8 / +0xf9 / +0x110 / +0x111 =
                                                record 0x1320 / 0x1321 (1 Extend, 0 Shorten) / 0x1338 / 0x1339 */
    double adj_s_len, adj_e_len;           /* the lengths the engine uses: record 0x1328 / 0x1340 (FUN_180054420, FUN_1800566e0) */
    double adj_s_pct, adj_e_pct;           /* their % of D (0x1330 / 0x1348; the page's pairs FUN_18015a1d0 / FUN_18015a100) */
    /* Depth Cuts 25598 (contour): tapered walls (ctour_prm +0xe2 / +0xe8 = record 0x130a / 0x1310, FUN_180056a70 /
       FUN_1800569a0), depth cut order (ctour_prm +0xf0 = record 0x1318, the sort FUN_1800447e0), tip comp (ctour_prm +9 =
       record 0x1231: the taper is measured from the corner radius) */
    int dc_taper; double dc_taper_a; int dc_bydepth, ct_tipcomp;
    /* Contour Cut 2938 rest (batch 5, 2026-10-23): ct_comp = record 0x5d0 while the compensation is not Off (side != SIDE_ON):
       0 Computer, 1 Control, 2 Wear, 3 Reverse wear (decomp/mccore_cc.c: cc_in_computer 0 / 2 / 3: the path offset by D/2, cc_in_control
       1 / 2 / 3: G41 / G42 cc codes, cc_control_gcode_2: reverse wear swaps them); ct_ccopt = 0x5d4 "Optimize cutter comp in
       control" (FUN_180051990 l.56085: Control only); ct_crnd / ct_cbrk = 0x1450 internal corner rounding / 0x1468 external
       corner break radius (ctour_prm +0x228 / +0x240, FUN_180051990 l.56089-56135); ct_doff = record 0x370 as read (op +0x308, the
       tool's diameter offset number, informational: the post takes the op's tool's dia_off - the same field of its tool block -
       Generic Fanuc 3X Mill.pst pccdia "tloffno$") */
    int ct_comp, ct_ccopt, ct_doff; double ct_crnd, ct_cbrk;
    int ct_nolook;           /* contour: "Infinite look ahead" 0x1238 (ctour_prm +0x10) off: ccomp_pock_from_ctour -> ccomp_pock passes it on to
                                MCCECalcContourOffset as flag 0x20000 (0x60027 instead of 0x40027; decomp/mccore_ccomp_pock2.c), whose
                                limited look-ahead (mcce_all.c l.8695, 107991-114635) is ported for closed chains (mcce_ctour.c, user decision D-5); open chains / refused paths: the op is refused (cam2d.c chain_offset) */
    /* Break Through 25599: the amount mccore set_depths (decomp/mccore_depths.c l.379) subtracts from the depth when the floor
       stock is below tol and the opcode's flag is on - final_z already holds it; bt_eff = that amount (0 = none), kept for the
       contour's partial tab height (FUN_180056be0: tab top = depth + break through + thickness, l.59048 dStack_12ac0) */
    double bt_eff;
    /* Tabs 2834: 0x139c Vertical (0) / Ramp (1) moves, 0x13b0 ramp angle (rad) - the contour's tab profile (cam2d.c cut_path) */
    int tab_ramp; double tab_ang;
    int finish_n;            /* finish passes (contour + pocket walls), cut at final depth */
    double finish_step;      /* spacing of finish passes */
    int tabs;                /* contour: tabs on (> 0; with a record: 0x1380 on -> max(1, the number of tabs 0x1382)) */
    double tab_w, tab_h;     /* tab width (material left, as Mastercam), height above final depth */
#define CAM_TAB_MAX 64
    double tab_xy[2 * CAM_TAB_MAX]; int ntab_xy;   /* tab centres (x, y) near the chain: Mastercam's tab entities (Manual), or the
                                                       automatic ones (cam2d.c ct_auto_tabs); 0 = spread evenly (ops without a record) */
    /* Tabs 2834 (formats/contour_100_plan.md 2; op + X = record X + 0x68): tab_rec = the fields below come from a record;
       tab_auto 0x1381 Automatic, tab_n 0x1382 number, tab_bydist 0x1440 by max distance, tab_maxd 0x1448, tab_all 0x13f0 "Tab
       all" (else only shapes an X x Y rectangle 0x13e0 / 0x13e8 does not fit in), tab_full 0x1398 Full (else Partial), tab_fplane
       0x13b9 "Use feed plane for full height tabs", tab_square 0x13ba "Use square point" (Start / Mid / End 0x139a) */
    int ct_cutoff_op;          /* the Tab Cutoff operation (opcode 0x82, mcmill bnci_cutoff): FUN_18005b340 writes no pass of it (l.1753), only
                                  the cutoff pass after each chain (l.1957) - cam2d.c gen_contour, cui_ops.c cutoff_children */
    int ct_uid, ct_parent_uid; /* ours: a contour op with a separate Tab Cutoff (0x13f2 = 3) and its 0x82 child (Mastercam: the child's op +0xd0 =
                                  record 0x138 = the parent's op id, CDependentOperations::DetectChildOperationIDs); saved as the op ids at export */
    int tab_cut, tab_cut_ffp;  /* Tab Cutoff 3022: 0x13f2 (0 off, 1 after all chains, 2 after each chain, 3 separate operation), 0x13f4 cut tabs
                                  with first finish pass (cam2d.c cut_tab_cutoff) */
    int tab_rec, tab_auto, tab_n, tab_bydist, tab_all, tab_full, tab_fplane, tab_square, tab_sqpos; double tab_maxd, tab_sx, tab_sy;
    /* "Overwrite edited tabs" 0x13f1 (tab_ow) and the stored tabs' own values (tab_xy order): tab_ed bit 0 = tab_own read from the
       file (else the op's values), bit 1 = the tab's edited flag (TP kind 30 +0x110), tab_own = width, thickness, ramp angle, Full, Ramp (+0xd0 / +0xd8 / +0xf8 / +0xe0 / +0xe4). mccore
       edit_operation (decomp/mccore_edit_operation.c l.3022-3046): on an edit of the op, Manual tabs not edited, or every tab with
       0x13f1 on, take the op's values; the tab writer (FUN_180056be0) cuts each tab with its own. cam2d.c gen_contour. */
    int tab_ow; unsigned char tab_ed[CAM_TAB_MAX]; double tab_own[5 * CAM_TAB_MAX];
    double ct_rec_tol;       /* contour: ctour_prm +0 = record 0x1228, the tol chain_to_ctour_array_break gets (bnci_ctour, decomp/mcmill_ctour.c
                                l.1422): a line whose end is within it of the last record in x and y makes no record (formats/spline_chain.md);
                                0 = not set */
    double tab_rise_z;       /* Full tabs: the height mccore retract_move reaches (decomp/mccore_moves.c l.201-270: retract 0x189 on ->
                                retract height, else clearance 0x179 on -> clearance height, else the feed plane); 0 = not set */
    int tab_rise_rc;         /* retract 0x189 or clearance 0x179 on (op +0x121 / +0x111): the tab writer's ramp branch takes Full tabs to
                                tab_rise_z (FUN_180056be0 bVar17, decomp/mcmill_ctour.c l.9310-9326) */
    int arcs;                /* post: fit G2/G3 arcs to the motion (1) or output lines (0) */
    int cleanup;             /* pocket: add corner clean-up moves for material the loops left */
    int tool_type;           /* tool type (see toollib.h tool_type_name), corner radius, tip angle: from the tool */
    double corner_r, tip_angle;
    int coolant;             /* 1 = flood (M8 / M9) */
    int tip_comp;            /* drill: depth is where the full diameter reaches (adds the tip length) */
    /* contour type: 0 2D, 1 2D chamfer (chamfer mill / spot drill / countersink), 2 engrave (on the line, no leads) */
    int contour_type; double chamfer_w, chamfer_tip, tip_dia;
    int ct_rmode, ct_r1way, ct_rlin, ct_rfinal; double ct_rang, ct_rdep, ct_rtol, ct_ovl;   /* Ramp contour (type 4, child page 2997 FUN_18015ae90,
                                MapToDlg FUN_18015b220): 0x12c0 ramp motion 0 Angle / 1 Depth / 2 Plunge, 0x12c8 ramp angle (deg), 0x12d0 ramp depth,
                                0x12d8 one way ramping for open contours, 0x12d9 linearize helixes, 0x12e0 its tolerance, 0x12e8 make pass at final
                                depth; ct_ovl = 0x988 (op +0x920) raw - create_ramped_ctour's added length (cam2d.c ct_ramp) */
    int ct_omode; double ct_odist, ct_odep;   /* Oscillate contour (type 6, child page 2998 HSTContourOscillatePage): 0x1408 0 Linear / 1 High speed,
                                0x1410 distance along contour, 0x1418 maximum depth (cam2d.c ct_osc_chain) */
    /* Remachining (contour type 5, record 0x1228 ctour_prm; dialog 2999): 0x124a source (0 All previous / 1 Previous / 2 Roughing tool), 0x1250 roughing tool diameter, 0x1258 clearance %, 0x1260
       clearance, 0x1268 (no page writes it) finish passes untrimmed, 0x1270 tolerance %, 0x1278 tolerance (pbmap cell), 0x1280 Display stock */
    int rm_src, rm_notrim, rm_display; double rm_dia, rm_clr_pct, rm_clr, rm_tol_pct, rm_cell;
    int ct_off_right;        /* contour: record 0x5d2 (op +0x56a) as stored, 0 left / 1 right: under compensation Off (side SIDE_ON) the Remachining driver FUN_1800550c0
                                still offsets to this side (decomp/mcmill_all.c l.58324-58327 DAT_1804f16f8, no 0x5d0 test) */
    int ct_rtype;            /* contour: record 0x1308 as stored (ctour_prm +0xe0: 0 2D, 1 3D, 2 2D chamfer, 3 3D chamfer, 4 Ramp, 5 Remachining,
                                6 Oscillate - the 2064 combo's data); gen_contour refuses 1 / 3-6 (their engines NOT DECODED) */
    double flute_len;        /* from the tool (thread mill: full-profile when it covers the thread length) */
    /* helix bore / circle mill */
    double helix_pitch;      /* depth per revolution */
    /* thread mill (gen_thread reads thread_pitch / thread_internal / thread_rh / thread_up = record 0x1258 / 0x1290 / 0x12a0 / 0x12a2;
       threadmill 2026-10-26: thread_major / thread_depth / thread_full / thread_passes / thread_step are only kept as operations-sidecar
       keys - the engine takes the diameter 0x1298 and the multipass fields from the record, as FUN_1802374d0) */
    double thread_major, thread_pitch, thread_depth;
    int thread_internal, thread_rh, thread_passes, thread_full;
    double thread_step;      /* the dialog's view of the multipasses (pf_thread_sync), not read by gen_thread */
    int thread_up;           /* 1 = bottom to top (Mastercam default), 0 = top to bottom */
    /* face */
    int face_style;          /* Mastercam's op +0x1228: 0 zigzag, 1 one way, 2 one pass, 4 dynamic */
    double face_angle, face_overlap;   /* cut direction (deg from X), overlap past the edges in % of D */
    double face_along;       /* % of D the tool edge runs past the chain at the ends of each pass (Mastercam's along overlap);
                                0 = old behaviour: passes span the whole bounding box */
    int use_box; double box[4];        /* face without chains: x0 y0 x1 y1 (stock) */
    int use_circ; double circ[3];      /* face without chains, cylindrical stock (make_facing_pock_bound): centre x, y, radius */
    /* the operation's planes (op_view: Tplane op +0x570, Cplane +0x620, WCS +0x6d0 = record 0x5d8 / 0x688 / 0x738; +2 view
       number, +8 matrix, +0x50 origin, +0x68 plane id; formats/mcam_write.md 2): the system plane ids 1 Top .. 7 Iso here,
       0 = Top. The work offset is the Tplane op_view's +0x92 (record 0x66a, PlanesPageBase::RefreshData): -1 = Automatic */
    /* wofs = the Tplane op_view's +0x92 (and the page's value 0x15da2), wofs_manual = the page's Manual radio (0x15da0);
       wwofs / cwofs = the WCS / Cplane op_views' own +0x92 (ResyncOpViews / MapToOp / SetToolPlane use them); all -1 by
       default (Plane::Init +0x330 = -1 via Plane::Convert, InitOperationViewsToActivePlanes) */
    int tplane, cplane, wcs; double torg[3], corg[3], worg[3]; int wofs_manual, wofs, wwofs, cwofs;
    int tplane_rec_id;       /* the stored op's Tplane plane id (op_view +0x68, record 0x640) when tplane is only its matrix's system plane
                                (CAM_LFACE: the lathe plane, id 9 on millturn op 1); 0 = tplane. The Toolpaths manager row's
                                "[Tplane: ...]" (COpMgrTree::InitTvOpText @1800c3350: GetPlanePtrByID(op +0x5d8)) */
    int lf_new, lf_slot;     /* a new Lathe Face made by the menu / ribbon command (lathe_face 0x1801a3290; viewer/lathe_newop.c): lf_new 1 =
                                its dialog's Face page was not opened (no CLFaceProp::OnOK: the bnci path makes the face points), 2 = it
                                was (OnOK 0x180042c40 made them from Finish Z); lf_slot = the part lathe tool picked (LTL_ID body +0 Slot).
                                0 = not such an op (a stored op, or cam_cli's `lface -` test path from the defaults record) */
    /* dynamic family */
    int dyn_outside;         /* 1 = material can be entered from outside the chains (open edges) */
    double min_rad_pct;      /* min toolpath radius, % of D */
    double micro_lift, back_feed;      /* links: lift above the floor and feed */
    double link_retract;     /* links longer than this retract to the feed plane (0 = only when blocked) */
    double entry_r;          /* entry helix radius (0 = auto) */
    double wall_stock;       /* dynamic contour: width of material along the wall */
    double slot_width;       /* peel mill: slot width along an open chain */
    int file_op_id;          /* > 0: this op replaces operation <id> stored in the part (edited from Mastercam's) */
    int tool_slot;           /* the op's tool slot (op +0x544 = record +0x5ac): 1-based TL record order; 0 = by tool number */
    /* Edit Common Parameters (opmanres dialog 243, mastercam FUN_180145b50): operation record bytes it set (record
       offset = operation offset + 0x68), written over the saved record; `height` ones (heights and their abs / inc /
       on flags, which the save writes in its own convention) are dropped when the parameters dialog regenerates the op */
    struct { uint16_t off; uint8_t n, height; uint8_t v[8]; } rpatch[CAM_NRPATCH]; int nrpatch;
    /* Slot Mill / Helix Bore / Circle Mill (mcmill.dll engines, ../formats/mcmill_2d_engines.md): operation record
       fields they read that have no CamParams field above (cam_fileop.c params_from_record; record offsets) */
    int mc_ok;               /* 1: the fields below came from an operation record (stored op or Mastercam's default op) */
    int mc_comp, mc_comp_right;        /* +0x5d0 compensation type (S16), +0x5d2 direction (0 left, 1 right) */
    int mc_depth_on, mc_depth_nf;      /* +0x868 depth cuts on, S16 +0x878 depth finish cuts */
    double mc_depth_fs;                /* +0x880 depth finish step */
    int mc_keep_down, mc_lead_on, mc_tip;            /* +0x891 keep tool down, +0x8f0 lead in/out, +0x2f8 compensate to tip (circle
                                                       0x1238; contour / pocket tip comp is 0x1231, mccore op_is_tip_comp) */
    int mc_clr_on, mc_clr_end;                       /* +0x179 clearance on, +0x319 clearance only at start / end */
    int mc_mp_kd;                                    /* multipasses keep tool down +0x8c8 (contour, cam2d.c kd_link_ok) */
    int mc_mp_on, mc_mp_nr, mc_mp_nf; double mc_mp_rs, mc_mp_fs;   /* multipasses +0x8a8, S16 +0x8aa, S16 +0x8b8, +0x8b0, +0x8c0 */
    int ct_mp_rec, ct_fin_all;          /* contour: multipass counts from the record (rough = mc_mp_nr / mc_mp_rs; finish =
                                           finish_n / finish_step); record 0x1230 (ctour_prm +8) finish passes at every depth */
    int mc_ret_on;           /* record 0x189 (op +0x121) retract on (retract_move / clear_move / plunge_move) */
    int mc_isl_depths;       /* record 0x892 (op +0x82a) Use island depths */
    int pk_ok, pk_roll;      /* pocket: record 0x1228..0x1447 copied (pk_p), op +0x56b = record 0x5d3 (set_min_fil_angle) */
    uint8_t pk_p[0x220];
    uint8_t mc_p[0x120];     /* record 0x1228..0x1347: the engines' parameter block P (set_slotmill_prm / set_circmill_prm) */
    double mc_lead_dz[4];    /* lead ramp heights: entry line +0x908, entry arc +0x920, exit line +0x950, exit arc +0x968 (also the
                                contour's: FUN_180051990 adds each piece's +0x78 going away from the cut) */
    double mc_rrate; int mc_rapid_ret;   /* retract rate +0x388, rapid retract +0x1a9 */
    int ht_inc;              /* heights incremental (from top of stock): 1 clearance +0x178, 2 retract +0x188, 4 feed plane +0x198
                                (Facing: mccore adjust_depths re-bases them on each pass depth, cam2d_face.c) */
    int mc_passes0; double mc_step0;     /* thread mill: thread_passes / thread_step as mapped from the record (sidecar bookkeeping only) */
    /* 2D High Speed Area Mill run by McHmm.dll (area_mill.c, ../formats/area_mill.md 11): macro values from the op
       record (area_mill_macro), in part units */
    int am_ok, am_mode;      /* am_mode = hmmCoreAreaMillDat +0x84: 0 Core Roughing, 1 Area Clearance, 4 trim, 5 horizontal */
    double am_min_off, am_max_off;     /* MinOffset / MaxOffset */
    int am_smoothing; double am_corner_off_tol;   /* ProfileSmoothing, ProfileSmoothingCornerOffsetTolerance */
    int am_ramp_style; double am_ramp_deg, am_min_ramp_dia;   /* ProfileRampStyle (0 profile ramp), MaxRampAngle, MinProfileRampDiameter */
    int am_climb;            /* LinkClimbMilling */
    int op_seq;              /* ours: the order an op made here was made in (Mastercam: op +0 = OMgetNextOpId, ids grow as ops are made), for
                                list_op_get_prev's "highest op id" (cui_ops.c cutoff_children); 0 = reopened from a project (tree order). */
} CamParams;

typedef struct { double x, y, d; } Hole;           /* d = circle diameter (0 for a point) */

/* a 3D op's reference to a surface / solid / mesh: one TP_ID kind 4 record (formats/surface_selection.md 2.1 / 2.2). A solid is one
   record per solid; which of its faces and their roles live in the solid's history (the SldToolPOp `link`, 2.3: stage S3). */
typedef struct {
    uint32_t cls;       /* the entity's class (TP +0x78): TP_ECLS_SURF / _SOLID / _MESH / _FILE */
    int index;          /* F.surfs (surface) / F.solids (solid) index; -1 = not in the part */
    int32_t idn;        /* the entity's idn (TP +0x6c) */
    unsigned flags;     /* TP +0x98: 0x4 check, 0x20 gouge, 0x1 / 0x2 (tp_ref_role) */
    int link;           /* a solid's SldToolPOp link id (TP +0x9a; 0 = none, an op made here) */
} CamRef;
#define CAM_REF_MAX 1024   /* ours (a fixed list in the op; the corpus' largest op has 420 references) */
/* S4 (formats/surface_selection.md 6 "S4", 1.5): a solid FACE picked here for a 3D op (mcgeomsld's face list with its type,
   sld_set_toolp_srf_type). Mastercam keeps them in the solid's SldToolPOp (2.3, written by S6); here per op: the solid's reference
   (index into CamOp.ref, cls TP_ECLS_SOLID), the face's ordinal in this program's face list of that solid (cui_sldchain.c topo: XT
   FACE node order = xt_faces() for a part solid, the OCCT face order for an imported one) and its type (0 drive, 4 check, 0x20
   gouge). Which face is picked is OUR hit test (decision N13-3). A solid reference with entries here is not a whole body (unless
   it is also picked whole: CamRef.flags = the body's type). */
typedef struct { int ref, face, type; } CamRefFace;
#define CAM_REF_FACE_MAX 1024   /* ours */

typedef struct CamOp_ {
    CamParams p;
    Chain *chains; int nchains;
    Hole *holes; int nholes;          /* drill */
    Move *m; int nm;                  /* generated motion (for display / G-code) */
    char name[64];
    int op_index;                     /* index into F.ops once added */
    char error[160];
    int filtered;                     /* batch 10: the moves went through the Line/Arc filter (cam_filter.c): the post writes its arcs as
                                         they are, without this program's own arc fitting (CamParams.arcs) */
    CamRef ref[CAM_REF_MAX]; int nref;   /* 3D ops: the drive / check references (formats/surface_selection.md 2.1 / 2.2; side file "REF ...",
                                         older files' "DRIVE" / "DSURF" lines read as drive references). Empty for a stored op regenerated
                                         from its record (p.file_op_id): its TP kind 4 records are read then (cam_refs_from_tp). The op's
                                         containment / curve chains are `chains` (Chain.role 2 / 3). */
    CamRefFace rface[CAM_REF_FACE_MAX]; int nrface;   /* S4: solid faces picked here (side file "RFACE ..."); a 3D op with any is
                                         refused at generate until the engines take single faces (cam2d.c cam_generate; stage S7) */
    char warning[200];                /* decisions batch dec1: set by an engine that generated with a known stand-in (dec1: Pocket's
                                         Minimize tool burial fallback link, gone since medax); cleared by cam_generate; shown by the front end (ours) */
    /* dirty operations (formats/opdlg_port_audit.md "Dirty operations"): Mastercam's op +0x94 (record +0xfc) of an op made here -
       CAM_DIRTY_PARAMS = its parameters were changed at OK (mccore edit_operation l.2582-2588), CAM_DIRTY_TOOL = its tool was
       changed (FUN_180a45440: IsDifferentEnoughToDirtyOp); the toolpath (m) is the one computed before, as Mastercam keeps the old
       NCI until the op is regenerated. EDIT = the op as changed, not computed yet (no moves; NULL = no parameter change): the
       next regeneration computes it and it replaces this op. Owned: cam_op_free frees it. */
    int dirty;
    struct CamOp_ *edit;
    int gen_units;                    /* the units its moves were computed in (cam_generate_op: IsMetric() + 1 = 1 inch / 2 mm; 0 = not computed).
                                         Mastercam writes IsMetric() into the NCI (1016 +0x44) when it makes the toolpath; a units change
                                         (cui_units_set) converts no operation - the post refuses moves in the other units (formats/units.md) */
} CamOp;
/* CamOp.name is the op comment (op +6 = record 0x6e; the Toolpaths manager row's " - comment", COpMgrTree::InitTvOpText
   @1800c3350, shown only when not empty). Our messages name an op by it, else by its type (ours) */
static inline const char *cam_op_label(const CamOp *op) { return op->name[0] ? op->name : cam_type_label(op->p.type); }
enum { CAM_DIRTY_PARAMS = 1, CAM_DIRTY_TOOL = 2 };

void cam_defaults(CamParams *p, int type, const McamFile *f);       /* base + Mastercam's default op (Mill_*.mcam-defaults) */
void cam_defaults_base(CamParams *p, int type, const McamFile *f);  /* built-in values only */
/* take tool number, diameter, shape, speeds / feeds, coolant and (drill) peck from a tool */
void cam_params_from_tool(CamParams *p, const Tool *t);
void cam_lead_recalc(CamParams *p);   /* lead lengths / radii from their % of D (each side that is on, lead_pct) */
double cam_drill_z(const CamParams *p);
double cam_tool_tip_length(double dia, double ang);   /* mccore tool_tip_length (batch 8) */
/* 2D chamfer: tool-centre offset from the chain and tip Z, or 0 + reason */
int chamfer_preview(const CamParams *p, double *r_eff, double *z_tip, char *err, size_t errn);                    /* final drill Z including tip compensation */
int cam_generate(const McamFile *f, CamOp *op);
/* Remachining sources 0 / 1 (FUN_180053a70): the operations before OP in the operation list, in order (bnci_ctour runs with the main op list). A front end that can give
   them sets the callback; without it sources 0 / 1 are refused. An entry: a generated op (moves, its tool's diameter) or a stored op's NCI section (malloc'd; freed by the caller
   of the callback, gen_remach). */
typedef struct { const Move *m; int nm; double dia; int control; uint8_t *nci; size_t nci_n; int xlate; double xv[3]; int opcode; int pk_fin_ctl; int pocket_1340; } CamPrevOp;   /* pk_fin_ctl: a Pocket op whose finish passes are in control (FUN_1801ebc60 refusal) */
typedef int (*CamPrevOpsFn)(const McamFile *f, const CamOp *op, CamPrevOp **out, int *n);
void cam_set_prev_ops_fn(CamPrevOpsFn fn);
/* the previous ops of a stored op: the stored ops of F before it (cam_cli, tests); the app's front end has its own (cui_ops.c) */
int cam_prev_ops_stored(const McamFile *f, const CamOp *op, CamPrevOp **out, int *n);
int cam_prev_op_entry(const McamFile *f, int i, CamPrevOp *e);   /* one stored op; 0 = no NCI */
void cam3d_set_stl(const char *path);   /* ours (testing, cam_cli --stl): an STL file as the 3D ops' drive model; NULL = the drive solids */
int cam3d_stl_load(const char *path, double **tri);
/* batch 10, cam_filter.c: the Line/Arc filter's G18 / G19 opt-in, and the node-list filter for cam_cli nci-filter (testing) */
int cam_arc_filter_planes_on(void);
int cam_filter_nodes(FILE *in, FILE *out, double max_err, double rmin, double rmax, int look, int xy, int xz, int yz, int oneway,
                     int rtype, double rval, int metric);
int cam_filter_nodes_moves(FILE *in, double max_err, double rmin, double rmax, int look, int xy, int xz, int yz, int oneway, int rtype,
                           double rval, int metric, Move **m, int *nm);
const double *cam_plane_matrix(int id);                    /* system plane 1 Top .. 7 Iso: rows x / y / z axes */
void cam_plane_w2v(int id, const double org[3], const double w[3], double v[3]);   /* wpt_to_op */
void cam_plane_v2w(int id, const double org[3], const double v[3], double w[3]);   /* op_to_wpt */
int cam_planes_default(const CamParams *p);                /* all three planes Top at the world origin, Automatic work offset */
int cam_plane_from_matrix(const double m[9]);              /* the system plane 1..7 with this matrix, 0 if none */
int cam_planes_from_record(const uint8_t *r, uint32_t n, CamParams *p, char *why, size_t whyn);   /* op_views -> p; 0 = not a system plane */
void cam_planes_to_record(uint8_t *r, const CamParams *p);
int cam_lface_points(const McamFile *f, const CamOp *op, double pts[2][3], char *err, size_t errn);   /* a new Lathe Face's two face points (the save) */  /* p's planes -> the three op_views of an OP_ID record */
int cam_plane_register(int id, const double m[9]);           /* a plane made here (id > 7, ours registry) */
void cam_plane_clear_user(void);
void cam_tab_mirror(CamParams *p, int off, int n, const uint8_t *v);   /* cam_fileop.c: a Tabs record edit -> the tab fields */
int cam_tabprm_edited(const McamFile *f, const CamParams *p);   /* cam_fileop.c: the tab block (record 0x1380..0x13f7) changed by an edit (edit_operation) */
int cam_arc_in_plane(const Ent *e, const double m[9]);    /* the arc's normal is the plane's Z (ours) */
extern int cam_chain_views;                                 /* chaining for an operation: view arcs chainable (ours) */
extern int cam_chain_splines;                               /* chaining for an Engrave operation: spline curves chainable (formats/spline_chain.md) */
int cam_op_top(const CamParams *p);                        /* the Tplane is Top at the world origin */              /* fills op->m; 0 + op->error on failure */
/* G-code in the user's post style (see ../formats/post.md); writes all given ops in order */
int cam_write_gcode(FILE *out, const McamFile *f, CamOp **ops, int nops, const char *progname);
/* operations sidecar ("<part>.mcamui"): params + chains (entity indices) + drill points, regenerated on load */
int cam_save_ops(const char *path, CamOp **ops, int nops);
int cam_set_param(CamParams *p, const char *key, double v);               /* by sidecar key name; 0 if unknown */
int cam_write_ops(FILE *f, CamOp **ops, int nops, const McamFile *mf);
/* an entity index written by an older project (ours): indices >= FROM move up by BY (part notes became entities after the surfaces -
   core_ui cui_doc.c doc_read sets them while it reads such a project; BY 0 = as written) */
extern int cam_load_remap_from, cam_load_remap_by;
static inline int cam_load_ent_remap(int i) { return cam_load_remap_by && i >= cam_load_remap_from ? i + cam_load_remap_by : i; }
int cam_load_ops_ord(const char *path, McamFile *mf, CamOp ***ops_out, int **ord);   /* + each op's position in the file */                        /* the OP..END blocks only */
void cam_op_free(CamOp *op);                                                 /* frees a heap CamOp and its arrays */
int cam_load_ops(const char *path, McamFile *f, CamOp ***ops_out);    /* F: solid chains add their edges to F.ents */   /* returns count; ops are generated */
/* 3D references (formats/surface_selection.md 2): a stored op's TP kind 4 records in file order, each resolved to F.surfs / F.solids by
   its idn (index -1 when the part has no such entity); returns the count, -1 + err when there are more than max */
int cam_refs_from_tp(const McamFile *f, int32_t op_id, CamRef *ref, int max, char *err, size_t errn);
/* an op made here: its drive list = these solids (F.solids indices), then these surfaces (F.surfs indices), all drive (flags 0) */
void cam_op_set_drive(CamOp *op, const McamFile *f, const int *solids, int nsolids, const int *surfs, int nsurfs);

#endif
