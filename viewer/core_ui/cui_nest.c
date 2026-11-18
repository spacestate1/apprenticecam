/* cui_nest.c - Transform > Geometry Nesting: NESTING.DLL's geometry flow (CO do_nest_g -> NE m_nest_g, mode 2), shared by
 * the front ends. Spec: formats/nesting_addin.md (the flow, the pages, the run, the write-back) + its decode notes of
 * 2026-11-05 (chains -> parts, write-back, dialogs; the corrections to §5 are cited where they apply). Citations:
 * "L<n>" / FUN_<addr> = decomp/nest/nesting_all.c (nesting.dll, image base 0x180000000); "asm @<addr>" = objdump of
 * nesting.dll; P+x = the CParam block (live copy 0x180137840 + x); part+x / sheet+x = CPart (0x558) / CSheet (0x398).
 *
 * The engines: NestLib's Rectangular engine (viewer/nest/nest_rect.c) and TrueShape engine (viewer/nest/nest_ts.c + ts/), both
 * bit-exact ports of n64dll.dll, through the back-end interface of viewer/nest/nest_job.h (nest_engine_rect / nest_engine_ts,
 * viewer/nest/nest_ts_job.c).
 *
 * Ours (not Mastercam's; listed in the report): the licence check (FUN_180038350) is dropped (user decision) - the CParam ctor's
 * licence byte is a licensed Mastercam's 1; the data folder is paths.conf `nesting` (user decision NS-1; the shipped .defx text
 * is written there when the file is missing, as the installation does); no nesting.nip; the windows are drawn by the front end
 * from the templates; the scene steps nest* (cui_ns_script).
 * Optimized pairs (TrueShape AutoPairs): the pair flag of FUN_180037400 L38267-38275 (job_build), the cluster calls in the
 * back-end, the part names of the results parsed back by ns_parse_prtname (nest_parse_prtname, nest_job.h).
 * Settings files (cui_nest_ini.inc): the CParam block with every [PARAMETERS] key, the .defx defaults (read at the start, Save /
 * Load defaults, Restore, the Configuration's Save as Defaults), sessions (.nstx: New / Open / Save / Save As).
 * NOT PORTED (greyed / left out): Groups page and groups, user clusters, solids, parts / sheets from
 * files / folders / libraries, Attach geometry, Material, origin Select, Restore last session (a registry value), the legacy
 * binary .nst, attached notes of a session's parts / sheets, the results' Sort / Customize / Drag / Preview, trim
 * lines, sheet scrap, reports, part labels (SetPartLabelPositions FUN_180041d00: get_ext text extents + a
 * TrueShape run per part with the label as part and the part as sheet - not ported), Delete original chains of sheets, notes
 * scan / auto-attach (NOT DECODED). Level names on OK (FUN_1800414f0) are ported (the part's level table, mcam_load.h).
 * The results' Copy / Copy Blank (ns_results_copy), Save and Delete (40031, "Results Delete": the placed instances NsPPI, entity
 * picks and the window event 0x20 - decision NS-2) are ported; Delete isolates by level visibility (mcam_load.h mcam_level_visible).
 * A TrueShape run stops on Esc (the progress callback's KeyPress(0x1b): cui_hooks.key_press, nest_ts_cancel).
 * Details (8016): the ActiveReports "Default Nesting Report" in the report window (ns_details; images NOT DECODED).
 * Chains in any one view (FUN_1800c9150 / get_view_from_chain; formats/nesting_addin.md section 12 "Chains in any view"); the output
 * in a Cplane other than Top is NOT PORTED (refused). The sheet-name note of every output sheet (sheet_name_note) and the colour / level cycling (FUN_1800412e0 / FUN_1800413e0 /
 * FUN_180039560 / FUN_1800c9a00) are ported. */
#include "core_ui.h"
#include "../nest/nest_job.h"
#include "../nest/nest_job_io.h"
#include "draft_note.h"
#include "paths.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdarg.h>

#include "cui_nest_defx.inc"

/* the module's message boxes (MCMessageBox / dbmmess_mbok, caption ours); the last one is kept for the tests (ns_dump) */
static char ns_last_msg[512];
static void ns_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void ns_msg(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); vsnprintf(ns_last_msg, sizeof ns_last_msg, fmt, ap); va_end(ap);
  cui_message("Nesting", "%s", ns_last_msg);
}

/* ------------------------------------------------------------------------------------------------ the model */
typedef struct {          /* ctour_rec (0x60): +0 end point, +0x10 centre, +0x20 CW, +0x21 'L' / 'A', +0x40 depth, +0x48 circle */
  char t; double x, y, cx, cy; int cw, circle; double z;
} NsRec;
typedef struct {          /* CCtour (0x90) */
  NsRec *r; int n;        /* +0x08 / +0x14 */
  int type;               /* +0x64: 0 outer, 1 hole, 2 point, 3 carried along (not nested) */
  double depth;           /* +0x48 */
  int color, level, ln_style, pt_style; double width;   /* +0x50 / +0x54 / +0x60 / +0x62 / +0x58: the chain's first entity */
  int *src; int nsrc;     /* ours: the chain's entities (Delete original chains) */
  double xpos, ypos;      /* +0x20 / +0x28 (0 from the ctor FUN_18001ac80; session files carry them) */
  int part_num, view;     /* +0x40 cluster element (0) / +0x44 view (1) */
} NsCtour;
typedef struct {          /* CPart (0x558) */
  int id;                 /* +0x230 */
  char name[256], label[256];          /* +8 / +0x108 */
  double len, wid;        /* +0x238 / +0x240 */
  int added_rect, user_rect;           /* +0x248 ADDEDRECTANGULAR / +0x24c USERRECTANGULAR ("Use bounding box") */
  int minq, fillq;        /* +0x250 / +0x254 */
  double step;            /* +0x258 */
  int prio, commontool, mirror;        /* +0x260 / +0x264 / +0x268 */
  char grain;             /* +0x278 */
  int cluster;            /* +0x27c CLUSTERMETHOD */
  double basex, basey;    /* +0x280 / +0x288 BASEPOINT */
  double mmx, mmy;        /* +0x290 / +0x298 MINMAX */
  double ptol;            /* +0x410 */
  int delgeo;             /* +0x4b8 */
  int skip;               /* +0x514 */
  char toobig;            /* +0x518 */
  int color;              /* +0x4cc the CycleColors colour (FUN_1800412e0 L45128) */
  NsCtour *ct; int nct;   /* +0x468 */
  int previd;             /* +0x234 PREVID */
  char file[256], fileext[256], material[256];   /* +0x220 / +0x228 / +0x210 (CStrings) */
} NsPart;
typedef struct {          /* CSheet (0x398) */
  int id;                 /* +8 */
  char name[256];         /* +0xc */
  double len, wid;        /* +0x230 / +0x238 */
  int added_rect;         /* +0x240 */
  double ox, oy;          /* +0x248 / +0x250 ORIGINPOINT */
  double ax, ay;          /* +0x258 / +0x260: the automatic origin (FUN_180037d60) */
  int prio, qty;          /* +0x268 / +0x26c */
  double basex, basey;    /* +0x270 / +0x278 */
  double mmx, mmy;        /* +0x280 / +0x288 */
  int amar;               /* +0x290 "Create necessary quantity" */
  int delgeo;             /* +0x338 */
  int outputs;            /* +0x344 the copies output in this run (reset per run, L44076) */
  int color;              /* +0x354 the CycleColors colour (FUN_1800412e0 L45128) */
  NsCtour *ct; int nct;   /* +0x298 */
  char file[256], fileext[256], material[256];   /* +0x220 / +0x228 / +0x210 */
} NsSheet;
typedef struct {          /* CParam (the geometry-relevant fields, §3.1) */
  int trueshape;          /* +0x1f */
  int rescbox; double resuser, resolution;   /* +0x28 / +0x30 / +0x20 */
  double p2p, s2p, s2s;   /* +0x48 PARTTOPARTDIST / +0x40 SHEETTOPARTDIST / +0x38 SHEETTOSHEETDIST */
  int corner; char filldir, grain;           /* +0xb0 STARTINGCORNER / +0xd9 SHEETFILLDIRECTION / +0xd8 (= [SHEETn] GRAINDIRECTION) */
  int guillotine, autoorigins, fillall, fillers, autopairs, fitpip, prefhole;   /* +0x54 / +0xd4 / +0x58 / +0x60 / +0x5c / +0x50 / +0x64 */
  int addlabels; double labelh; int labelauto, nameaslabel;   /* +0xc0 / +0xc8 / +0xd0 / +0xa */
  int deletechains, cyclecolors, cyclelevels, cycleunused, resultcolor, resultlevel;   /* +0x68 / +0x74 / +0x78 / +0x7c / +0x80 / +0x84 */
  int dispresults, dispgroups, loaddefsheet, compress, savescrap, checkdup, highres;   /* +0x94 / +0x90 / +0x98 / +0x1e / +0xa0 / +0xb / +0x9c */
  char scrapname[256];    /* +0x130 */
  int ignoreholes, addasgroup, scannotes, autoattach, alignlong;   /* +0xac / +0xa4 / +0xbc / +0x8c / +0xa8 */
  double ptol;            /* +0x148 PROFILETOLERANCE */
  uint8_t cp[0x1c8];      /* the CParam block itself at Mastercam's offsets (every [PARAMETERS] key, toolpath-only ones too);
                             the named fields above are views of it (ns_pull / ns_push, cui_nest_ini.inc) */
} NsParams;

typedef struct {          /* one output sheet (CResult 0x8b0 + its CNST_Result) */
  int sheet;              /* the CSheet index */
  int copy;               /* instance of the identical-sheet repetition (0 ..) */
  int copies;             /* the repetition count of its layout */
  double util;
  int ts;                 /* TrueShape: the CNST_Result values below (FUN_180007b60 L6330-6370) */
  double len, wid, area, cutArea, nestLen, nestWid;   /* +0x780 / +0x788 / +0x7a0 / +0x7a8 / +0x7b0 / +0x7b8 */
  NestOutInst *inst; int ninst;
  double ox, oy;          /* the output sheet's origin (§7.1) */
  int ent_from, ent_to;   /* the entities ns_output created for it [ent_from, ent_to) */
  int parts;              /* CResult +0x880: made with its parts (ns_output's param_3; 0 = a Copy Blank) */
  int lv_sheet, lv_parts; /* CResult +0x6c8 / +0x6cc = DAT_180130604 / DAT_180130600 at its output (L6378-6380) */
  int rid;                /* +0x8a0: the result count when ns_output appended it (L6396-6418; also every new PPI's +0xb4) */
  int sheet_to;           /* the sheet's entities [ent_from, sheet_to): the list +0x670 (FUN_18008d820 param_3, L94436-94441: every
                             entity the sheet output stores) */
  int modified;           /* +0x881: instances deleted (Results Delete, FUN_180087fb0 L91076) - Copy refuses it (13325-13327) */
  /* the trims (FUN_180085c80 L89333, formats/nesting_addin.md section 8 "Trim lines") */
  double lo[3], hi[3];    /* +0x698 / +0x6b0: the extent of the placed instances (ctor 1e38 / -1e38, FUN_180087530 L90334) */
  double partArea;        /* CNST_Result +0x158 (TrueShape NInqNestedSheet; 0 from the Rectangular back-end) */
  int hval, vval;         /* +0x7e0 / +0x808: the horizontal / vertical trim line is valid */
  int hact, vact;         /* +0x5a4 / +0x5a0: the trim is active (the check boxes 9268 / 9275); ctor 0 (FUN_1800853e0: +0x5a0 8-byte 0) */
  double hcut, vcut;      /* +0x7d0 / +0x7d8: the cutoffs (geometry mode: the line's distance from the sheet box's min y / min x) */
  double ht[4], vt[4], bt[4];   /* +0x7e8.. (L, trimmed height, area, util) / +0x810.. (trimmed width, W, area, util) / +0x830.. both */
  int hent, vent;         /* +0x590 / +0x598: the trim line entities (-1 none) */
  int *pq;                /* per part (Pt index): the result's own CPart copy +0x568 (FUN_180085bb0 L89302, made once per part from its
                             first CNST_Part entry, L6238-6250) - its +0x250 = that entry's instance count (param_3 = the XML part's
                             instance count +0x70); -1 = no copy of that part. Results Delete lowers it (FUN_180087450 L90270) */
} NsResult;
/* the placed instances (PPI, DAT_180140d50 / count DAT_180140d60): one per instance ns_output places, CPart::Place FUN_18005a260
   L61819 appending it at L63065 */
typedef struct {
  int rid;                /* +0xb4 = the result's +0x8a0 (set after the result is appended, L6405-6418) */
  int part_id;            /* +0xa4 = part +0x230 (L63003) */
  int ord;                /* +0xac = part +0x4c4 (L63004): 0 at each CNST_Part entry (L5952), ++ before each instance (L6121) */
  int elem;               /* +0x34 = part +0x4c8, the element of the engine part name, < 0 -> 0 (L5948-5950, L63005-63009) */
  int from, to;           /* its entity list +0x1e0 / +0x1e8: the entities Place stores for it [from, to) */
} NsPPI;
static NsPPI *PPI; static int nPPI, capPPI;

static NsParams P, Pdef;                  /* live 0x180137840 / defaults copy 0x180137650 */
static NsSheet Sdef; static NsPart Pdpart;   /* default sheet 0x180138090 / default part 0x180137a30 */
static NsSheet *S; static int nS, capS;    /* DAT_180140e18 (count DAT_180140e28) */
static NsPart *Pt; static int nP, capP;    /* DAT_180138428 (count DAT_180138438) */
static NsResult *R; static int nR;
static int sheet_counter = 1;             /* DAT_180136e84 = 1 at the session start (L42211) */
static int ns_metric;
int ns_open, ns_page, ns_cfg_open, ns_res_open, ns_chaining;
static int sel_sheet = -1, sel_part = -1, res_sel;
static int res_main_level;                /* +0xac: the main level when the Results dialog opened (FUN_1800892c0 L85430) */
static int run_from = -1;                 /* the undo transaction: F.nents at UndoTransStart (mod_begin) */
static int ns_inited;

static void ct_free(NsCtour *c, int n) { for (int i = 0; i < n; i++) { free(c[i].r); free(c[i].src); } free(c); }
static void results_free(void) { for (int i = 0; i < nR; i++) { free(R[i].inst); free(R[i].pq); } free(R); R = NULL; nR = 0; nPPI = 0; }

/* ------------------------------------------------------------------------------------------------ numbers */
/* FUN_1800c9fb0 L139240 (precision -5): |v| < tol -> "0.0"; "%.5f", trailing '0's trimmed, a trailing '.' gets "0" */
static void ns_fmt(double v, char *out, int n) {
  if (fabs(v) < mc_systol.tol) { snprintf(out, n, "0.0"); return; }
  if (fabs(v) > 1e38) { snprintf(out, n, "**********"); return; }
  snprintf(out, n, "%.*f", 5, v);
  int k = (int)strlen(out); while (k > 0 && out[k - 1] == '0') out[--k] = 0;
  if (k > 0 && out[k - 1] == '.') snprintf(out + k, n - k, "0");
}

#include "cui_nest_ini.inc"

/* ------------------------------------------------------------------------------------------------ chains -> contours (§5) */
/* one chain as Mastercam's pock_bound (chains_to_pock_bounds(chains, 1, 0, 1, nc_tol) FUN_1800c4d20 L135829): its records
   (pock_bound_to_ctour L58288: rec[0] = the start point, then per entity its end point, 'L' / 'A', centre, CW, circle flag),
   a polygon for the containment tests (ours: chain_polyline3), closed / point. How pock_bound_to_ctour sets the CW flag
   and the circle flag is mastercam.dll's (NOT DECODED): [inferred] an arc traversed against its stored (CCW) sense is CW, a
   full-circle entity has the circle flag. */
typedef struct { int chain; int closed, point; NsRec *r; int n; double *pg; int npg; double z; } NsBound;
static void bounds_free(NsBound *b, int n) { for (int i = 0; i < n; i++) { free(b[i].r); free(b[i].pg); } free(b); }

/* world_to_view (rows R, no origin: mcViews' matrices) */
static V3 ns_w2v(const double R[9], V3 p) { return (V3){ R[0] * p.x + R[1] * p.y + R[2] * p.z, R[3] * p.x + R[4] * p.y + R[5] * p.z, R[6] * p.x + R[7] * p.y + R[8] * p.z }; }
static V3 ns_v2w(const double R[9], V3 q) { return (V3){ R[0] * q.x + R[3] * q.y + R[6] * q.z, R[1] * q.x + R[4] * q.y + R[7] * q.z, R[2] * q.x + R[5] * q.y + R[8] * q.z }; }
static int arc_view(const Ent *e) { return e->vw >= 2 ? e->vw : 1; }   /* Ent.vw 0 = an XY (Top) arc */
/* chains_to_pock_bounds runs with the active plane = the chains' view (FUN_1800c4d20 L135817-135840: ActivePlaneSaveRestore(mcViews
   [view])): the records are in that view's coordinates (x, y in the plane, z the depth); view V's rows R. An arc runs the other way in
   a view antiparallel to its own (views_parallel) */
static int bound_make(const Chain *c, int ci, NsBound *b, int V, const double R[9]) {
  memset(b, 0, sizeof *b); b->chain = ci; b->closed = c->closed;
  b->r = calloc((size_t)c->n + 1, sizeof(NsRec)); if (!b->r) return 0;
  const Ent *e0 = &F.ents[c->ent[0]];
  if (c->n == 1 && e0->type == E_POINT) {          /* pock_bound_is_point: one point record */
    V3 a = ns_w2v(R, e0->a);
    b->point = 1; b->r[0].t = 'P'; b->r[0].x = a.x; b->r[0].y = a.y; b->r[0].z = a.z; b->n = 1; b->z = a.z;
    b->pg = malloc(2 * sizeof(double)); b->pg[0] = a.x; b->pg[1] = a.y; b->npg = 1; return 1;
  }
  V3 s = ns_w2v(R, c->rev[0] ? e0->b : e0->a);
  b->r[0].t = 'L'; b->r[0].x = s.x; b->r[0].y = s.y; b->r[0].z = s.z; b->z = s.z; b->n = 1;
  for (int i = 0; i < c->n; i++) {
    const Ent *e = &F.ents[c->ent[i]]; NsRec *q = &b->r[b->n++]; V3 p = ns_w2v(R, c->rev[i] ? e->a : e->b);
    q->x = p.x; q->y = p.y; q->z = p.z;
    if (e->type == E_ARC) {
      int anti = 0; mc_views_parallel(arc_view(e), V, &anti);
      V3 cc = ns_w2v(R, e->c);
      q->t = 'A'; q->cx = cc.x; q->cy = cc.y; q->cw = (c->rev[i] ? 1 : 0) ^ (e->sw < 0) ^ anti;   /* a clockwise arc (sweep < 0: a mirrored one) runs the other way */
      q->circle = fabs(e->sw) >= 2 * M_PI - 1e-9;
    }
    else q->t = 'L';
  }
  double *pts = NULL; int np = chain_polyline3(c, mc_systol.curve_chord_dev, &pts);
  b->pg = malloc(sizeof(double) * 2 * (np > 0 ? np : 1)); b->npg = np;
  for (int i = 0; i < np; i++) { V3 q = ns_w2v(R, (V3){ pts[3 * i], pts[3 * i + 1], pts[3 * i + 2] }); b->pg[2 * i] = q.x; b->pg[2 * i + 1] = q.y; }
  free(pts);
  return 1;
}
/* polygon helpers (ours: the geometric predicates behind the mastercam.dll calls named) */
static double ns_seg_dist(double px, double py, double ax, double ay, double bx, double by) {
  double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
  if (t < 0) t = 0; if (t > 1) t = 1;
  return hypot(px - ax - t * dx, py - ay - t * dy);
}
/* pt_in_ctour(b, pt, nc_tol, tol): 1 inside, 0 on (within tol of the boundary), -1 outside */
static int pt_in_bound(const NsBound *b, double x, double y, double tol) {
  if (b->npg < 2) return -1;
  for (int i = 0; i + 1 < b->npg; i++) if (ns_seg_dist(x, y, b->pg[2 * i], b->pg[2 * i + 1], b->pg[2 * i + 2], b->pg[2 * i + 3]) <= tol) return 0;
  int in = 0;
  for (int i = 0, j = b->npg - 1; i < b->npg; j = i++) {
    double xi = b->pg[2 * i], yi = b->pg[2 * i + 1], xj = b->pg[2 * j], yj = b->pg[2 * j + 1];
    if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
  }
  return in ? 1 : -1;
}
/* IsContourNested(a, b, tol): a lies inside b - [inferred] every vertex of a inside b or within tol of its boundary */
static int is_nested(const NsBound *a, const NsBound *b, double tol) {
  if (!b->closed || b->point || b->npg < 3) return 0;
  for (int i = 0; i < a->npg; i++) if (pt_in_bound(b, a->pg[2 * i], a->pg[2 * i + 1], tol) < 0) return 0;
  return a->npg > 0;
}
/* pock_bound_intersect(a, b): [inferred] a segment of a crosses a segment of b */
static int seg_cross(const double *p, const double *q, const double *r, const double *s) {
  double d1x = q[0] - p[0], d1y = q[1] - p[1], d2x = s[0] - r[0], d2y = s[1] - r[1], den = d1x * d2y - d1y * d2x;
  if (fabs(den) < 1e-14) return 0;
  double t = ((r[0] - p[0]) * d2y - (r[1] - p[1]) * d2x) / den, u = ((r[0] - p[0]) * d1y - (r[1] - p[1]) * d1x) / den;
  return t > 1e-9 && t < 1 - 1e-9 && u > 1e-9 && u < 1 - 1e-9;
}
static int bounds_intersect(const NsBound *a, const NsBound *b) {
  for (int i = 0; i + 1 < a->npg; i++) for (int j = 0; j + 1 < b->npg; j++) if (seg_cross(a->pg + 2 * i, a->pg + 2 * i + 2, b->pg + 2 * j, b->pg + 2 * j + 2)) return 1;
  return 0;
}
/* FUN_180063a70 L68124: a inside or crossing b (b not a point) */
static int in_or_cross(const NsBound *a, const NsBound *b, double tol, int *cross) {
  if (b->point) return 0;
  if (bounds_intersect(a, b)) { if (cross) *cross = 1; return 1; }
  return is_nested(a, b, tol);
}
static double bound_area(const NsBound *b) {   /* |pock_ccw area| */
  double a = 0; for (int i = 0, j = b->npg - 1; i < b->npg; j = i++) a += b->pg[2 * j] * b->pg[2 * i + 1] - b->pg[2 * i] * b->pg[2 * j + 1];
  return fabs(a / 2);
}

/* FUN_1800c74f0 L137154 (param_5 = 1): the outer of the next part / sheet and the bounds that go with it; marks mk[i] = 1.
   Returns the marked count, -10060 when there is no outer, 0 when nothing is left */
static int outer_select(NsBound *b, int n, const int *left, int *mk, int *outer) {
  int any = 0; for (int i = 0; i < n; i++) { mk[i] = 0; any += left[i]; }
  if (!any) return 0;
  int o = -1;
  for (int i = 0; i < n && o < 0; i++) {           /* the first closed non-point bound not inside any other (L137210-137240) */
    if (!left[i] || b[i].point || !b[i].closed) continue;
    int ok = 1; for (int j = 0; j < n && ok; j++) if (j != i && left[j] && !b[j].point && is_nested(&b[i], &b[j], 0.1)) ok = 0;
    if (ok) o = i;
  }
  if (o < 0) return -10060;                         /* L137365 */
  mk[o] = 1;
  double nct = mc_systol.nc_tol;
  for (int i = 0; i < n; i++) if (left[i] && i != o) {   /* L137255-137275 */
    if (b[i].point) { if (pt_in_bound(&b[o], b[i].pg[0], b[i].pg[1], nct) >= 0) mk[i] = 1; }
    else if (in_or_cross(&b[i], &b[o], 0.1, NULL)) mk[i] = 1;
  }
  for (int i = 0; i < n; i++) if (left[i] && !mk[i])      /* one more pass in index order (L137280-137320) */
    for (int j = 0; j < n && !mk[i]; j++) if (mk[j] && !b[j].point) {
      if (b[i].point ? pt_in_bound(&b[j], b[i].pg[0], b[i].pg[1], nct) >= 0 : in_or_cross(&b[i], &b[j], 0.1, NULL)) mk[i] = 1; }
  *outer = o;
  int c = 0; for (int i = 0; i < n; i++) c += mk[i];
  return c;
}

/* CCtour build FUN_180054eb0 L58077 + finalise FUN_180057b20 L60191 / the sheet's FUN_18008a340 L92545-92790: the records
   relative to the outer's first point, then to the bbox min; the bbox from break_arc_n points (FUN_1800d0bb0 L143946);
   returns 0 when no contour was made. The contour order: the outer first, then the others in chain order (L58219-58260) */
static int ctours_build(NsBound *b, int n, const int *mk, const int *type, int outer, const Chain *ch, int V,
                        NsCtour **pct, int *pnct, double *len, double *wid, double *bx, double *by) {
  double mnx = 1e38, mny = 1e38, mxx = -1e38, mxy = -1e38;   /* L58262-58265 */
  double base0x = 0, base0y = 0;
  NsCtour *ct = calloc((size_t)n, sizeof(NsCtour)); int nct = 0;
  int order[256], no = 0; order[no++] = outer; for (int i = 0; i < n; i++) if (mk[i] && i != outer && no < 256) order[no++] = i;
  double chord = mc_systol.curve_chord_dev;
  for (int k = 0; k < no; k++) {
    NsBound *bd = &b[order[k]]; NsCtour *c = &ct[nct++];
    c->type = type[order[k]]; c->depth = bd->z; c->view = V; c->n = bd->n;   /* +0x44 the chains' view, +0x48 the depth in it */ c->r = malloc(sizeof(NsRec) * (size_t)bd->n); memcpy(c->r, bd->r, sizeof(NsRec) * (size_t)bd->n);
    const Chain *cc = &ch[bd->chain]; const Ent *e0 = &F.ents[cc->ent[0]];   /* the chain's first entity's attributes (L58468-58497) */
    c->color = e0->color; c->level = e0->level; c->width = e0->width;
    if (e0->type == E_POINT) { c->ln_style = 1; c->pt_style = e0->pt_style; } else { c->ln_style = e0->ln_style; c->pt_style = 1; }
    c->src = malloc(sizeof(int) * (size_t)cc->n); c->nsrc = cc->n; memcpy(c->src, cc->ent, sizeof(int) * (size_t)cc->n);
    if (bd->point) continue;                        /* point contours never touch the bbox (§1.3) */
    double px = c->r[0].x, py = c->r[0].y, bxk = c->r[0].x, byk = c->r[0].y;   /* base[k] = rec[0] */
    if (k == 0) { base0x = bxk; base0y = byk; }
    for (int s = 1; s < c->n; s++) {
      NsRec *q = &c->r[s]; double pts[42][2]; int cnt = 0;
      if (q->t == 'A') {                            /* constr_ns_arc / constr_circle, then break_arc_n (L58326-58363) */
        double r = hypot(px - q->cx, py - q->cy), sa = atan2(py - q->cy, px - q->cx), ea = atan2(q->y - q->cy, q->x - q->cx), sw;
        if (q->circle) sw = q->cw ? -2 * M_PI : 2 * M_PI;
        else { sw = q->cw ? sa - ea : ea - sa; while (sw <= 0) sw += 2 * M_PI; if (q->cw) sw = -sw; }
        if (r <= 0) { q->t = 'L'; goto line; }
        double st = sqrt(8 * chord / r); int nb = 2;
        if (fabs(st) > 1e-6) { nb = (int)floor(fabs(sw / st) + 0.5); nb = nb > 20 ? 20 : (nb > 1 ? nb : 2); }
        for (int j = 0; j <= nb; j++) { double t = sa + sw * j / nb; pts[cnt][0] = q->cx + r * cos(t); pts[cnt][1] = q->cy + r * sin(t); cnt++; }
      } else { line: pts[0][0] = px; pts[0][1] = py; pts[1][0] = q->x; pts[1][1] = q->y; cnt = 2; }
      for (int j = 0; j < cnt; j++) {               /* strict < / > (L58374-58393) */
        if (pts[j][0] < mnx) mnx = pts[j][0]; if (pts[j][0] > mxx) mxx = pts[j][0];
        if (pts[j][1] < mny) mny = pts[j][1]; if (pts[j][1] > mxy) mxy = pts[j][1];
      }
      px = q->x; py = q->y;
    }
  }
  if (mnx > mxx) { *pct = ct; *pnct = nct; return 0; }
  /* net effect of the shifts (L58400-58463, L60198-60300): rec = absolute - bbox min; centre too for 'A', else 0 */
  for (int k = 0; k < nct; k++) for (int s = 0; s < ct[k].n; s++) {
    NsRec *q = &ct[k].r[s]; q->x -= mnx; q->y -= mny;
    if (q->t == 'A') { q->cx -= mnx; q->cy -= mny; } else { q->cx = 0; q->cy = 0; }
  }
  (void)base0x; (void)base0y;
  *len = fabs(mxx - mnx); *wid = fabs(mxy - mny); *bx = mnx; *by = mny;   /* L60301-60304 */
  *pct = ct; *pnct = nct; return 1;
}

/* ---- the chains' view (decomp/nest/mastercam_chainview.c: mastercam.dll get_view_from_chain @1801af9d0, FUN_1801aee30; NESTING.DLL
   FUN_1800c9150 L138637 through FUN_1800c4d20 L135792) */
/* FUN_1801aee30 (chainview.c 452-626) on one entity: 0 flat so far at *z (*have set), 1 not flat in view V, -1 an error. A point: its
   view z; a line: both ends at one view z (within tol); an arc: its view parallel to V (views_parallel) and its centre's view z
   (the centre +0x98 in its own view -> world -> V). Every entity also at the depth of the first (|d| <= tol). Splines (8 / 0x80:
   FUN_1801ae690 / make_view_k / find_view_m / creep_along_ent) NOT PORTED: -1 */
static int ent_view_z(const Ent *e, int V, const double R[9], double *z, int *have) {
  double tol = mc_systol.tol, d;
  if (e->type == E_POINT) d = ns_w2v(R, e->a).z;
  else if (e->type == E_LINE) { double za = ns_w2v(R, e->a).z, zb = ns_w2v(R, e->b).z; if (fabs(za - zb) > tol) return 1; d = za; }
  else if (e->type == E_ARC) { int anti = 0; if (!mc_views_parallel(arc_view(e), V, &anti)) return 1; d = ns_w2v(R, e->c).z; }
  else return -1;
  if (*have && fabs(*z - d) > tol) return 1;
  if (!*have) { *z = d; *have = 1; }
  return 0;
}
static int chain_is_point(const Chain *c) { return c->n == 1 && F.ents[c->ent[0]].type == E_POINT; }   /* FUN_1800c4850 L135557: the chain's
                                                                     one node (+0x30 next = 0) is a point entity */
/* get_view_from_chain (chainview.c 48-450): a point chain -> 0; else the first view 1..n_views the chain is flat in (FUN_1801aee30 = 0)
   with its depth; FUN_1801aee30 < 0 -> 0. A chain flat in no view: the plane from the geometry (an arc's own view; else two lines'
   plane, Plane::GetMatrixFrom2lines -> Matrix33::GetViewNo(.., true), which can add a view; splines make_view_k / find_view_m) - NOT
   PORTED (AddMCView is not ported, formats/wireframe.md): -2 */
static int chain_view_get(const Chain *c, int *view, double *depth) {
  if (chain_is_point(c)) return 0;
  int nv = mc_view_count();
  for (int v = 1; v <= nv; v++) {
    double R[9]; if (!mc_view_rows(v, R)) continue;
    double z = 0; int have = 0, r = 0;
    for (int k = 0; k < c->n && r == 0; k++) r = ent_view_z(&F.ents[c->ent[k]], v, R, &z, &have);
    if (r < 0) return 0;
    if (r == 0 && have) { *view = v; *depth = z; return 1; }
  }
  return -2;
}
/* FUN_1800c9150(chains, 0, &view, &depth) L138637-138760: per chain that is no point chain, get_view_from_chain; on success the view
   becomes the lowest-numbered view parallel to it (the depth negated for an antiparallel one, L138700-138716), and every chain must
   give that view (else -10062, "Warning - all chains must lie in same view"), the first one's depth kept; a chain get_view_from_chain
   fails on keeps the view so far and takes the last depth read (L138697-138698). |depth| <= tol -> 0. Only point chains: view 1 and the first point's Top z (L138765-138780). 1 = ok, 0 = 10062,
   -2 = NOT PORTED (above) */
static int chains_view(const Chain *ch, int n, int *pview, double *pdepth) {
  int have = 0, sv = 1, cur = 1; double dep = 0, d = 0;
  for (int i = 0; i < n; i++) {
    if (chain_is_point(&ch[i])) continue;
    int v = cur, r = chain_view_get(&ch[i], &v, &d);
    if (r == -2) return -2;
    if (r == 1) {
      for (int u = v - 1; u >= 1; u--) { int anti = 0; if (mc_views_parallel(u, v, &anti)) { v = u; if (anti) d = -d; } }
      cur = v;
      if (have) { if (v != sv) return 0; continue; }                         /* the same view: the depth so far is kept (L138731-138733) */
    } else v = cur;
    sv = v; dep = d; have = 1;
  }
  if (!have) { sv = 1; dep = 0; for (int i = 0; i < n; i++) if (chain_is_point(&ch[i])) { dep = F.ents[ch[i].ent[0]].a.z; break; } }
  if (fabs(dep) <= mc_systol.tol) dep = 0;
  *pview = sv; *pdepth = dep; return 1;
}
/* the chains' view for Add chained parts / sheets: 10062 (L79240-79254 / L101900) or ours, NOT PORTED */
static int chains_view_ok(const Chain *ch, int n, int *V, double R[9]) {
  double dep = 0; int r = chains_view(ch, n, V, &dep);
  if (r == 0) { ns_msg("Warning - all chains must lie in same view"); return 0; }
  if (r < 0) { ns_msg("These chains lie on a plane that is no view: a new view (Matrix33::GetViewNo / AddMCView) is NOT PORTED."); return 0; }
  return mc_view_rows(*V, R);
}

static void part_copy_params(NsPart *d, const NsPart *s) {   /* FUN_180057490 L59901 / FUN_1800572f0 L59849 (no contours, notes cleared) */
  NsCtour *ct = d->ct; int nct = d->nct; *d = *s; d->ct = ct; d->nct = nct;
}
static int part_next_id(void) { int m = 0; for (int i = 0; i < nP; i++) if (Pt[i].id > m) m = Pt[i].id; return m + 1; }   /* FUN_180039610 L39456 */
/* FUN_18005f430 L65452: the label of a new part from the template's label and the part count */
static void part_label(NsPart *p, const char *tmpl, int count) {
  if (count == 0) { snprintf(p->label, sizeof p->label, "%s", tmpl); return; }
  char t[256]; snprintf(t, sizeof t, "%s", tmpl);
  for (int d = 1; d < 10; d++) {
    char *f = strchr(t, '0' + d);
    if (f == t) { t[0] = 'P'; t[1] = 0; break; }   /* found at 0: "P%d" / "C%d" */
    if (f) { *f = 0; break; }
  }
  if (!strcmp(t, "P")) snprintf(p->label, sizeof p->label, p->cluster == 4 ? "C%d" : "P%d", p->id);
  else snprintf(p->label, sizeof p->label, "%s%d", t, p->id);
}
static NsPart *part_push(void) { if (nP == capP) { capP = capP ? capP * 2 : 16; Pt = realloc(Pt, sizeof(NsPart) * (size_t)capP); } memset(&Pt[nP], 0, sizeof(NsPart)); return &Pt[nP++]; }
static NsSheet *sheet_push(void) { if (nS == capS) { capS = capS ? capS * 2 : 8; S = realloc(S, sizeof(NsSheet) * (size_t)capS); } memset(&S[nS], 0, sizeof(NsSheet)); return &S[nS++]; }

/* Add chained parts FUN_1800745e0 L78893 on the chains of chain_it_point (10030 "Add Part: select chain") */
static void parts_from_chains(const Chain *ch, int n) {
  int V = 1; double R[9]; if (!chains_view_ok(ch, n, &V, R)) return;   /* 10062 (L79240-79254) */
  NsBound *b = calloc((size_t)n, sizeof(NsBound)); int *left = calloc((size_t)n, sizeof(int)), *mk = calloc((size_t)n, sizeof(int)), *type = calloc((size_t)n, sizeof(int));
  for (int i = 0; i < n; i++) { bound_make(&ch[i], i, &b[i], V, R); left[i] = 1; }
  int tmplsel = sel_part, added = 0;
  for (;;) {
    NsPart np; memset(&np, 0, sizeof np); part_ctor(&np);
    part_copy_params(&np, &Pdpart); np.cluster = 7;                         /* L79015-79016 */
    if (nP > 0 && tmplsel >= 0 && tmplsel < nP) { part_copy_params(&np, &Pt[tmplsel]); np.cluster = 7; }   /* L79017-79024 */
    np.ct = NULL; np.nct = 0;
    int outer = -1, c = outer_select(b, n, left, mk, &outer);
    np.id = part_next_id();
    char nm[64]; snprintf(nm, sizeof nm, "Part #%d", np.id);                /* 10085 + id (L79097-79101) */
    if (c < 0) { ns_msg("Warning - invalid chains\n%s\n(0)", nm); break; }   /* LoadString(|code|) + "\n" + name + "\n(0)" (L79031-79048) */
    if (c == 0) break;
    /* classification FUN_180054930 L57821 on this part's chains */
    int cnt = 0, only = -1; for (int i = 0; i < n; i++) if (mk[i]) { cnt++; only = i; }
    int o = -1;
    if (cnt == 1) {                                                         /* L58040-58058 */
      if (!b[only].closed) { ns_msg("Warning - chain must be closed\n%s\n(0)", nm); break; }   /* -10063 */
      o = only;
    } else {
      int big = -1; double ba = -1e38;                                      /* the largest closed non-point bound first (L57875-57913) */
      for (int i = 0; i < n; i++) if (mk[i] && b[i].closed && !b[i].point) { double a = bound_area(&b[i]); if (a > ba) { ba = a; big = i; } }
      int ord[256], no = 0; if (big >= 0) ord[no++] = big; for (int i = 0; i < n && no < 256; i++) if (mk[i] && i != big) ord[no++] = i;
      for (int k = 0; k < no && o < 0; k++) {                               /* L57930-57965 */
        int i = ord[k]; if (b[i].point || !b[i].closed) continue;
        int ok = 1;
        for (int j = 0; j < n && ok; j++) if (mk[j] && j != i) {
          if (b[j].point) { if (pt_in_bound(&b[i], b[j].pg[0], b[j].pg[1], mc_systol.nc_tol) < 0) ok = 0; }
          else if (is_nested(&b[i], &b[j], 0.1)) ok = 0;
        }
        if (ok) o = i;
      }
      if (o < 0) { ns_msg("Warning - invalid chains\n%s\n(0)", nm); break; }   /* -10060 (L58061) */
    }
    for (int i = 0; i < n; i++) if (mk[i]) {                                /* types (L57978-58034) */
      if (i == o) { type[i] = 0; continue; }
      if (b[i].point) { type[i] = 2; continue; }
      if (!b[i].closed) { type[i] = 3; continue; }
      if (!is_nested(&b[i], &b[o], 0.1)) { int cr = 0; type[i] = in_or_cross(&b[i], &b[o], mc_systol.tol, &cr) && cr ? 0 : 3; continue; }   /* L57999-58003 */
      int inner = 0;
      for (int j = 0; j < n && !inner; j++) if (mk[j] && j != i && j != o && b[j].closed && !b[j].point && in_or_cross(&b[j], &b[i], mc_systol.tol, NULL)) inner = 1;   /* asm @180054d6f */
      type[i] = inner ? 3 : (P.ignoreholes ? 3 : 1);
    }
    double L = 0, W = 0, bx = 0, by = 0;
    if (!ctours_build(b, n, mk, type, o, ch, V, &np.ct, &np.nct, &L, &W, &bx, &by)) { ct_free(np.ct, np.nct); break; }   /* +1: no contours - stop (L79090-79095) */
    np.len = L; np.wid = W; np.basex = bx; np.basey = by; np.mmx = L; np.mmy = W;   /* L60301-60306 */
    snprintf(np.name, sizeof np.name, "%s", nm);
    part_label(&np, Pdpart.label, nP);                                      /* FUN_18005f430(part, default label, count) */
    np.added_rect = 0; np.user_rect = 0;                                    /* L79150 */
    *part_push() = np; added++;
    tmplsel = nP - 1;                                                       /* param_13 = the part just added (L79154) */
    for (int i = 0; i < n; i++) if (mk[i]) left[i] = 0;                    /* remaining = rest (L79153) */
  }
  if (added) { sel_part = nP - 1; }
  bounds_free(b, n); free(left); free(mk); free(type);
}

/* Add chained sheets FUN_180097f50 L101826 / FUN_18008a340 L92259 (§5.2 of the decode notes) */
static void sheets_from_chains(const Chain *ch, int n) {
  int V = 1; double R[9]; if (!chains_view_ok(ch, n, &V, R)) return;   /* 10062 (FUN_180097f50) */
  NsBound *b = calloc((size_t)n, sizeof(NsBound)); int *left = calloc((size_t)n, sizeof(int)), *mk = calloc((size_t)n, sizeof(int)), *type = calloc((size_t)n, sizeof(int));
  for (int i = 0; i < n; i++) { bound_make(&ch[i], i, &b[i], V, R); left[i] = 1; }
  int tmplsel = sel_sheet;
  for (;;) {
    NsSheet ns = Sdef;                                                       /* FUN_18008c900 from the default sheet, then the selected (L101934-101946) */
    if (tmplsel >= 0 && tmplsel < nS) { ns = S[tmplsel]; }
    ns.ct = NULL; ns.nct = 0;
    int outer = -1, c = outer_select(b, n, left, mk, &outer);
    char nm[64]; snprintf(nm, sizeof nm, "Sheet #%d", sheet_counter);
    if (c < 0) { ns_msg("Warning - invalid chains\n %s", nm); break; }   /* L101951-101960 */
    if (c == 0) break;
    int cnt = 0, only = -1; for (int i = 0; i < n; i++) if (mk[i]) { cnt++; only = i; }
    int o = -1;
    if (cnt == 1) { if (!b[only].closed) { ns_msg("Warning - chain must be closed\n %s", nm); break; } o = only; }   /* L92419-92426 */
    else for (int i = 0; i < n && o < 0; i++) {                            /* L92429-92461: no largest-area step */
      if (!mk[i] || b[i].point || !b[i].closed) continue;
      int ok = 1;
      for (int j = 0; j < n && ok; j++) if (mk[j] && j != i) {
        if (b[j].point) { if (pt_in_bound(&b[i], b[j].pg[0], b[j].pg[1], mc_systol.nc_tol) < 0) ok = 0; }
        else if (is_nested(&b[i], &b[j], 0.1)) ok = 0;
      }
      if (ok) o = i;
    }
    if (o < 0) { ns_msg("Warning - invalid chains\n %s", nm); break; }
    for (int i = 0; i < n; i++) if (mk[i]) {                                /* L92476-92508 */
      if (i == o) { type[i] = 0; continue; }
      if (b[i].point) { type[i] = 2; continue; }
      if (!b[i].closed || !is_nested(&b[i], &b[o], 0.1)) { type[i] = 3; continue; }
      int in = 0;
      for (int j = 0; j < n && !in; j++) if (mk[j] && j != i && j != o && b[j].closed && !b[j].point && in_or_cross(&b[i], &b[j], mc_systol.tol, NULL)) in = 1;   /* asm @18008a6b6 */
      type[i] = in ? 3 : 1;                                                 /* ignore-holes = 0 for sheets (L101978) */
    }
    double L = 0, W = 0, bx = 0, by = 0;
    if (!ctours_build(b, n, mk, type, o, ch, V, &ns.ct, &ns.nct, &L, &W, &bx, &by)) { ct_free(ns.ct, ns.nct); break; }
    ns.len = L; ns.wid = W; ns.basex = bx; ns.basey = by; ns.ox = bx; ns.oy = by; ns.mmx = L; ns.mmy = W;   /* L92545-92610 */
    ns.id = sheet_counter++; snprintf(ns.name, sizeof ns.name, "%s", nm);   /* L101975-101981 */
    ns.added_rect = 0;                                                      /* L101983 */
    *sheet_push() = ns; tmplsel = nS - 1;
    for (int i = 0; i < n; i++) if (mk[i]) left[i] = 0;
  }
  sel_sheet = nS - 1;
  bounds_free(b, n); free(left); free(mk); free(type);
}

/* ------------------------------------------------------------------------------------------------ the engines */
/* the Rectangular back-end: viewer/nest/nest_rect_job.c (nest_engine_rect) */

/* ------------------------------------------------------------------------------------------------ the run (§6) */
static double clearance(void) { return P.p2p; }    /* FUN_1800cad50 L139900: PartToPartDist (0 only in toolpath common edge) */
static double collar_c;                            /* DAT_180136e58 */
static void collar_calc(void) {                    /* FUN_1800cddf0 L142120 */
  double tol = mc_systol.tol;
  if (fabs(P.s2p) < tol) P.s2p = 0;
  if (P.p2p < tol) P.p2p = 0;
  double d = P.s2p - clearance();                  /* DAT_180130610 set [inferred: geometry nesting] */
  collar_c = fabs(d) < tol ? 0 : d;
}
/* FUN_18005be90 L63156: the engine min / max (DAT_1801352b8 redo-count subtraction: Customize only, not ported) */
static void part_qty(const NsPart *p, int *mn, int *mx) {
  if (p->minq == 0 && p->fillq == 0) { *mn = *mx = 0; return; }
  int a = p->minq, b = p->fillq + a;
  if (P.fillall) b = 50000;
  *mn = a < 0 ? 0 : a; *mx = b < 0 ? 0 : b;
}
/* FUN_1800599b0 L61427: the part is rectangular (FUN_1800c7960 the TrueShape upgrade - groups / materials / local area, none
   here - is false) */
static int part_is_rect(const NsPart *p) { if (P.trueshape && p->nct > 0 && !p->added_rect) return p->user_rect != 0; return 1; }
/* the points of record q from (px, py) for the bbox: CtourLengthWidth FUN_1800c5260 L136027 - an arc / circle through
   constr_ns_arc / constr_circle and break_arc_n (FUN_1800d0bb0's count), a line its two end points; the same as
   ctours_build's loop (a failed arc counts as a line) */
static int rec_pts(double px, double py, const NsRec *q, double pts[][2]) {
  int cnt = 0;
  if (q->t == 'A') {
    double r = hypot(px - q->cx, py - q->cy), sa = atan2(py - q->cy, px - q->cx), ea = atan2(q->y - q->cy, q->x - q->cx), sw;
    if (q->circle) sw = q->cw ? -2 * M_PI : 2 * M_PI;
    else { sw = q->cw ? sa - ea : ea - sa; while (sw <= 0) sw += 2 * M_PI; if (q->cw) sw = -sw; }
    if (r > 0) {
      double st = sqrt(8 * mc_systol.curve_chord_dev / r); int nb = 2;
      if (fabs(st) > 1e-6) { nb = (int)floor(fabs(sw / st) + 0.5); nb = nb > 20 ? 20 : (nb > 1 ? nb : 2); }
      for (int j = 0; j <= nb; j++) { double t = sa + sw * j / nb; pts[cnt][0] = q->cx + r * cos(t); pts[cnt][1] = q->cy + r * sin(t); cnt++; }
      return cnt;
    }
  }
  if (q->t == 'A' || q->t == 'L') { pts[0][0] = px; pts[0][1] = py; pts[1][0] = q->x; pts[1][1] = q->y; return 2; }
  return 0;
}
/* FUN_180056c30 L59445: the min / max over every contour of the part (FUN_1800c5260 per contour, strict < / >); 0 without
   contours */
static int part_bbox(const NsPart *p, double *mnx, double *mny, double *mxx, double *mxy) {
  if (p->nct < 1) return 0;
  *mnx = *mny = 1e38; *mxx = *mxy = -1e38;
  for (int k = 0; k < p->nct; k++) {
    const NsCtour *c = &p->ct[k]; double px = 0, py = 0;
    for (int i = 0; i < c->n; i++) {
      const NsRec *q = &c->r[i];
      if (i > 0) {
        double pts[42][2]; int cnt = rec_pts(px, py, q, pts);
        for (int j = 0; j < cnt; j++) {
          if (pts[j][0] < *mnx) *mnx = pts[j][0]; if (pts[j][1] < *mny) *mny = pts[j][1];
          if (pts[j][0] > *mxx) *mxx = pts[j][0]; if (pts[j][1] > *mxy) *mxy = pts[j][1];
        }
      }
      px = q->x; py = q->y;
    }
  }
  return 1;
}
/* FUN_180058910 L60822 (param_2 = holes count): 0 = not a rectangle, 1 = a rectangle, 2 = a square - every outer contour
   (code 0) only 'L' records, each axis-parallel and lying on the bbox; a square when the box is square and every edge is
   the part's length / width; a user cluster (CLUSTERMETHOD 4) is never one; no contours: 1 */
static int part_rect_shape(const NsPart *p, int holes) {
  if (p->cluster == 4) return 0;
  double mnx, mny, mxx, mxy, tol = mc_systol.tol;
  if (!part_bbox(p, &mnx, &mny, &mxx, &mxy)) return 1;
  int sq = fabs(fabs(mxx - mnx) - fabs(mxy - mny)) < tol, any = 0;
  for (int k = 0; k < p->nct; k++) {
    const NsCtour *c = &p->ct[k];
    if (c->type == 0) {
      double qx = 0, qy = 0;
      for (int i = 0; i < c->n; i++) {
        if (c->r[i].t != 'L') return 0;
        double x = c->r[i].x, y = c->r[i].y;
        if (i > 0) {
          double dx = fabs(x - qx), dy = fabs(y - qy);
          if (tol < dx) {
            if (dy > tol) return 0;
            if (tol < fabs(y - mny) && fabs(y - mxy) > tol) return 0;
            if (sq && fabs(dx - p->len) > tol) sq = 0;
          }
          if (dy > tol) {
            if (tol < fabs(x - mnx) && fabs(x - mxx) > tol) return 0;
            if (sq && fabs(dy - p->wid) > tol) sq = 0;
          }
        }
        qx = x; qy = y;
      }
      any = 1;
    }
    if (c->type == 1 && holes) return 0;
  }
  return any ? sq + 1 : 0;
}
/* FUN_180058650 L60730: the part is a circle - the first arc record of the outer contours has the circle flag and every
   arc of them has the same centre and radius (tol), no line after a contour's start record; a user cluster or a part
   without contours is not */
static int part_is_circle(const NsPart *p) {
  if (p->cluster == 4 || p->nct < 1) return 0;
  int seen = 0, circ = 0; double r0 = 0, cx0 = 0, cy0 = 0, tol = mc_systol.tol;
  for (int k = 0; k < p->nct; k++) {
    const NsCtour *c = &p->ct[k]; if (c->type != 0) continue;
    for (int i = 0; i < c->n; i++) {
      const NsRec *q = &c->r[i];
      if (q->t == 'A') {
        double r = sqrt((q->y - q->cy) * (q->y - q->cy) + (q->x - q->cx) * (q->x - q->cx));
        if (seen) {
          if (tol < fabs(r - r0) || tol < fabs(q->cx - cx0) || fabs(q->cy - cy0) > tol) return 0;
        } else if (q->circle) circ = 1;
        cx0 = q->cx; cy0 = q->cy; r0 = r; seen = 1;
      } else if (i > 0) return 0;
    }
  }
  return circ;
}
/* FUN_18008d7d0 L94581: the sheet is rectangular */
static int sheet_is_rect(const NsSheet *s) { return !P.trueshape || s->nct == 0 || s->added_rect; }
/* FUN_18008ced0: the sheet's outer contours are axis-parallel lines on its box (holes count with chk_holes) */
static int sheet_rect_shape(const NsSheet *s, int chk_holes) {
  if (s->nct == 0) return 1;
  int r = 0; double tol = mc_systol.tol;
  for (int k = 0; k < s->nct; k++) {
    const NsCtour *c = &s->ct[k];
    if (c->type == 0) {
      for (int i = 0; i < c->n; i++) {
        if (c->r[i].t != 'L') return 0;
        if (i > 0) {
          double dx = fabs(c->r[i].x - c->r[i - 1].x), dy = fabs(c->r[i].y - c->r[i - 1].y);
          if (dx > tol) { if (dy > tol) return 0; if (fabs(c->r[i].y) > tol && fabs(c->r[i].y - s->wid) > tol) return 0; }
          if (dy > tol) { if (fabs(c->r[i].x) > tol && fabs(c->r[i].x - s->len) > tol) return 0; }
        }
      }
      r = 1;
    }
    if (c->type == 1 && chk_holes) return 0;
  }
  return r;
}

/* the pre-checks of the driver (L42514-42566) and FUN_18003fb40's quantities; 0 = back to the dialog */
static int run_prechecks(void) {
  if (nS < 1) { ns_msg("Warning - no sheets defined"); return 0; }          /* 10016 L42514 */
  if (nP < 1) { ns_msg("Warning - no parts defined"); return 0; }           /* 10017 L42522 */
  if (P.grain == 'N') for (int i = 0; i < nP; i++) if (Pt[i].grain != 'N') {                /* FUN_180039a40 L39714 */
    ns_msg("Warning - sheet grain direction required"); return 0; }         /* 10029 */
  /* FUN_18003f400 L43681: part too big */
  collar_calc();
  double c = fabs(collar_c), maxl = 0, maxw = 0;
  for (int i = 0; i < nS; i++) {
    double l = collar_c < 0 ? S[i].len + 2 * c : S[i].len - 2 * c, w = collar_c < 0 ? S[i].wid + 2 * c : S[i].wid - 2 * c;
    if (l > maxl) maxl = l; if (w > maxw) maxw = w;
  }
  for (int i = 0; i < nP; i++) {
    NsPart *p = &Pt[i]; double a = fabs(p->step);
    if (!P.trueshape) {                             /* FUN_1800c4be0 snaps a copy of the step */
      double v = a, best = fabs(v - 0.0); int k = 0; static const double ang[4] = { 0, 90, 180, 270 };
      for (int j = 1; j < 4; j++) if (fabs(v - ang[j]) < best) { best = fabs(v - ang[j]); k = j; }
      a = best <= fabs(v - 360.0) && k >= 1 ? ang[k] : 360.0;
    }
    int ia;                                          /* FUN_1800ccbf0 L141313 */
    if (a > 359.25 || a < 0.75) ia = 0; else if (fabs(a - 270) < 0.25) ia = 270; else if (fabs(a - 180) < 0.25) ia = 180; else if (fabs(a - 90) < 0.25) ia = 90; else ia = (int)a;
    if (ia != 0 && ia != 90 && ia != 180 && ia != 270) { p->toobig = 0; continue; }
    int rot = ia == 90 || ia == 270;
    double l = clearance() + p->len, w = clearance() + p->wid, tol = mc_systol.tol;
    p->toobig = 1;
    for (int s = 0; s < nS; s++) {
      double sl = collar_c < 0 ? S[s].len + 2 * c : S[s].len - 2 * c, sw = collar_c < 0 ? S[s].wid + 2 * c : S[s].wid - 2 * c;
      if (l < tol + sl && w < tol + sw) { p->toobig = 0; break; }
      if (l < tol + sw && w < tol + sl && rot) { p->toobig = 0; break; }
    }
    if (p->toobig) {
      char a1[64], a2[64], a3[64], a4[64]; ns_fmt(l, a1, 64); ns_fmt(w, a2, 64); ns_fmt(maxl, a3, 64); ns_fmt(maxw, a4, 64);
      ns_msg("Warning - part too big for sheet\n%s\n(%s x %s) > (%s x %s)", p->name, a1, a2, a3, a4);   /* 10021 + name + sizes */
      return 0;
    }
  }
  /* FUN_18003fe50 L44175: rectangular mode with non-rectangular sheets (10080) / sheets with holes (5), "Proceed with
     rectangular nesting?" (4, Yes / No) */
  if (!P.trueshape) {
    int code = 0;
    for (int i = 0; i < nS && !code; i++) if (!sheet_rect_shape(&S[i], 0)) code = 10080;
    for (int i = 0; i < nS && !code; i++) if (!sheet_rect_shape(&S[i], 1)) code = 5;
    if (code) {
      const char *t = code == 10080 ? "Warning - non-rectangular sheet in rectangular nesting." : "Warning - holes in sheet will be ignored.";
      char q[200]; snprintf(q, sizeof q, "%s\nProceed with rectangular nesting?", t);
      int yes = cui_hooks.ask_yes_no ? cui_hooks.ask_yes_no("Nesting", q) : 1;
      if (!yes) return 0;
    }
  }
  return 1;
}

/* the automatic resolution FUN_180037e40 (asm @180037e40-180038134, the table of §6.3: the mean of max(L, W) over the parts)
   and FUN_180038410's minimum (L38739) */
static double resolution_auto(void) {
  static const double in[3][5] = { { 0.025, 0.1, 0.25, 0.5, 1.0 }, { 0.1, 0.25, 0.5, 1.0, 2.0 }, { 0.25, 0.5, 1.0, 2.0, 5.0 } };
  static const double mm[3][5] = { { 0.5, 1, 2, 5, 7.5 }, { 1, 2, 5, 7.5, 10 }, { 2, 5, 7.5, 10, 20 } };
  double r = P.resuser;
  if (P.rescbox < 3) {
    double sum = 0; int n = 0;
    for (int i = 0; i < nP; i++) { sum += Pt[i].len > Pt[i].wid ? Pt[i].len : Pt[i].wid; n++; }
    double a = n ? sum / n : 0; int col = a < 10 ? 0 : a < 100 ? 1 : a < 1000 ? 2 : a < 3000 ? 3 : 4;
    int row = P.rescbox > 2 ? 1 : P.rescbox;
    r = ns_metric ? mm[row][col] : in[row][col];
  }
  return r;
}
static double resolution_calc(void) {
  double r = resolution_auto();
  if (P.rescbox < 3) P.resolution = r;               /* RESOLUTION is overwritten (L39950) */
  double m = 0; for (int i = 0; i < nS; i++) { if (S[i].len > m) m = S[i].len; if (S[i].wid > m) m = S[i].wid; }
  m = m * 2e-05 + 0.001;
  if (r <= m) { char t[64]; snprintf(t, sizeof t, "%.5f", m); ns_msg("Warning - Minimum resolution is %s", t); r = m + 0.001; }   /* 10143 (make_rstr(.., 5)) */
  return r;
}

/* the geometry callbacks' profiles (for a TrueShape back-end; the Rectangular engine takes sizes only). Part FUN_180006f90
   L4917: a rectangular part -> FUN_1800073c0 L5120, one closed profile of 4 lines (0,0)-(L,0)-(L,W)-(0,W); else per CCtour
   with > 1 entity: code 0 -> NDwgClosedProfStart(1), code 1 -> (0), codes 2 / 3 skipped; NDwgAddLine(prev, end) only when
   |dx| or |dy| > tol; NDwgAddArc(cx, cy, prev, sweep), sweep = the atan2 difference in (0, 2pi], - 2pi when CW (asm
   @180007277-1800072fb); a full circle NDwgAddCircle(c, |end - c|). Sheet FUN_1800074b0 L5154: outer contours then holes
   (the negative-collar offset FUN_1800ca890 is not ported) */
static void prof_add(NestProfile **pp, int *pn, int outer) { *pp = realloc(*pp, sizeof(NestProfile) * (size_t)(*pn + 1)); NestProfile *q = &(*pp)[(*pn)++]; q->outer = outer; q->e = NULL; q->n = 0; }
static NestDwgEnt *dwg_add(NestProfile *q) { q->e = realloc(q->e, sizeof(NestDwgEnt) * (size_t)(q->n + 1)); NestDwgEnt *d = &q->e[q->n++]; memset(d, 0, sizeof *d); return d; }
static void ctours_profiles(const NsCtour *ct, int nct, int rect, double L, double W, int holes_after, NestProfile **pp, int *pn) {
  double tol = mc_systol.tol;
  if (rect) {
    prof_add(pp, pn, 1); NestProfile *q = &(*pp)[*pn - 1]; double xs[5] = { 0, L, L, 0, 0 }, ys[5] = { 0, 0, W, W, 0 };
    for (int i = 0; i < 4; i++) { NestDwgEnt *d = dwg_add(q); d->kind = 0; d->x1 = xs[i]; d->y1 = ys[i]; d->x2 = xs[i + 1]; d->y2 = ys[i + 1]; }
    return;
  }
  for (int pass = 0; pass < (holes_after ? 2 : 1); pass++)
    for (int k = 0; k < nct; k++) {
      const NsCtour *c = &ct[k]; if (c->n <= 1 || (c->type != 0 && c->type != 1)) continue;
      if (holes_after && c->type != pass) continue;
      prof_add(pp, pn, c->type == 0); NestProfile *q = &(*pp)[*pn - 1];
      double px = c->r[0].x, py = c->r[0].y;
      for (int i = 1; i < c->n; i++) {
        const NsRec *r = &c->r[i];
        if (r->t == 'A' && r->circle) { NestDwgEnt *d = dwg_add(q); d->kind = 2; d->cx = r->cx; d->cy = r->cy; d->r = hypot(r->x - r->cx, r->y - r->cy); }
        else if (r->t == 'A') {
          double sw = atan2(r->y - r->cy, r->x - r->cx) - atan2(py - r->cy, px - r->cx);
          while (sw <= 0) sw += 2 * M_PI; while (sw > 2 * M_PI) sw -= 2 * M_PI;
          if (r->cw) sw -= 2 * M_PI;
          NestDwgEnt *d = dwg_add(q); d->kind = 1; d->cx = r->cx; d->cy = r->cy; d->x1 = px; d->y1 = py; d->sweep = sw;
        } else if (fabs(r->x - px) > tol || fabs(r->y - py) > tol) { NestDwgEnt *d = dwg_add(q); d->kind = 0; d->x1 = px; d->y1 = py; d->x2 = r->x; d->y2 = r->y; }
        px = r->x; py = r->y;
      }
    }
}
static void profiles_free(NestProfile *p, int n) { for (int i = 0; i < n; i++) free(p[i].e); free(p); }

/* the NestJob of the session (FUN_180039c90 / FUN_180037400 / FUN_18003a3f0) */
static void job_build(NestJob *j) {
  memset(j, 0, sizeof *j);
  collar_calc();
  j->clearance = clearance(); j->cutter = 0; j->resolution = resolution_calc();
  j->fillersUptoNestedHeight = P.fillers; j->fitPartInPart = P.fitpip; j->preferHoleFilling = P.prefhole; j->guillotine = P.guillotine;
  j->parts = calloc((size_t)nP, sizeof(NestJobPart)); j->sheets = calloc((size_t)nS, sizeof(NestJobSheet));
  /* FUN_1800393f0 L39306: P = max(#parts, max part priority) + 1 (no groups) */
  int prioP = nP; for (int i = 0; i < nP; i++) if (Pt[i].prio > prioP) prioP = Pt[i].prio; prioP += 1;
  /* FUN_18003fa40: parts with max < 1 are skipped (+0x514 = +0x518 first) */
  for (int i = 0; i < nP; i++) {
    NsPart *p = &Pt[i]; p->skip = p->toobig;
    int mn, mx; part_qty(p, &mn, &mx);
    if (p->skip || mx < 1) { p->skip = 1; continue; }
    NestJobPart *q = &j->parts[j->nparts++];
    snprintf(q->name, sizeof q->name, "%dp", p->id);                     /* FUN_180056f20 "%dp" */
    q->minq = mn < 1 ? 1 : mn; q->maxq = mx;
    q->isRect = part_is_rect(p);
    q->len = p->len; q->wid = p->wid;
    double st = p->step; if (st < 1.0 - mc_systol.tol) st = 360.0;       /* L38158-38160 */
    if (!P.trueshape) {                               /* FUN_1800c4be0 L135720 */
      double best = fabs(st - 0.0); int k = 0; static const double ang[4] = { 0, 90, 180, 270 };
      for (int a = 1; a < 4; a++) if (fabs(st - ang[a]) < best) { best = fabs(st - ang[a]); k = a; }
      if (best <= fabs(st - 360.0) && k >= 1) { st = ang[k]; q->rotate = 1; } else { st = 360.0; q->rotate = 0; }
    }
    q->grain = p->grain; if (p->grain != 'N') st = 90.0;                  /* L38171 */
    q->step = st;
    q->prio = prioP - p->prio + 1;                                        /* L38167 */
    q->commonTool = P.trueshape ? p->commontool : 0; q->mirror = P.trueshape ? (p->mirror == 2 ? 3 : p->mirror) : 0;
    q->cutterDia = 0;
    q->id = p->id;
    /* Optimized pairs (FUN_180037400 L38267-38275): AutoPairs (P+0x5c), TrueShape, the part not sent as a rectangle, not
       a rectangle / square (FUN_180058910(part, 0)) nor a circle (FUN_180058650), and max != 1 */
    q->pair = P.autopairs && P.trueshape && !q->isRect && !part_rect_shape(p, 0) && !part_is_circle(p) && mx != 1;
    ctours_profiles(p->ct, p->nct, q->isRect, p->len, p->wid, 0, &q->prof, &q->nprof);
  }
  /* sheets FUN_18003a3f0 L40201 */
  int anyGrain = 0; for (int i = 0; i < nP; i++) if (Pt[i].grain != 'N') anyGrain = 1;   /* FUN_18003f3a0 */
  int prioS = nS; for (int i = 0; i < nS; i++) if (S[i].prio > prioS) prioS = S[i].prio; prioS += 1;   /* FUN_1800394a0 */
  for (int i = 0; i < nS; i++) {
    NsSheet *s = &S[i]; NestJobSheet *q = &j->sheets[j->nsheets++];
    int qty = nS == 1 && S[0].amar ? 10000 : s->qty;                     /* 10000 with one sheet type and "Create necessary quantity" */
    snprintf(q->name, sizeof q->name, "%ds%dq", s->id, qty);             /* FUN_18008c100 "%ds%dq" */
    q->qty = qty; q->isRect = sheet_is_rect(s);
    q->len = s->len; q->wid = s->wid; q->collar = fabs(collar_c) <= mc_systol.tol ? 0 : collar_c;
    if (collar_c < 0) {                              /* rectangular engine: L and W enlarged by 2|c| (asm @18003a6b6-18003a6ce) */
      if (!P.trueshape || q->isRect) { q->len += fabs(collar_c) * 2; q->wid += fabs(collar_c) * 2; }
      q->collar = 0;
    }
    q->corner = P.corner;
    q->dir = (unsigned char)(P.filldir - 'X') <= 1 ? P.filldir : (s->len <= s->wid ? 'Y' : 'X');   /* L40355-40358: Calculate */
    q->grain = anyGrain ? P.grain : 'N';
    q->prio = prioS - s->prio + 1;
    if (!q->isRect) ctours_profiles(s->ct, s->nct, 0, s->len, s->wid, 1, &q->prof, &q->nprof);
  }
}
static void job_free(NestJob *j) {
  for (int i = 0; i < j->nparts; i++) profiles_free(j->parts[i].prof, j->parts[i].nprof);
  for (int i = 0; i < j->nsheets; i++) profiles_free(j->sheets[i].prof, j->sheets[i].nprof);
  free(j->parts); free(j->sheets); memset(j, 0, sizeof *j);
}

/* ------------------------------------------------------------------------------------------------ the templates */
/* mcnestingres.dll dialogs (tools/pe_dialogs.py + the control styles): roles from the styles - BS_GROUPBOX group, BS_AUTOCHECKBOX
   check, BS_AUTORADIOBUTTON radio, BS_OWNERDRAW owner-drawn, a "|" caption = MCButton (bitmap, tooltip after the "|"),
   CBS_DROPDOWNLIST drop list, CBS_DROPDOWN editable combo (CSkRCmb), SS_SUNKEN static = a value box. Ids are the template's
   (16-bit: 40031..40033 = the results' Delete / Copy / Copy Blank, 0xffff = -1 statics) */
static const NsCtl NS_101[] = {   /* mcnestingres dialog 101 "Sheets" (402 x 270 DLU) */
  { 3001u, NSR_STATIC, 7, 8, 120, 12, "Sheet List:" },
  { 3002u, NSR_STATIC, 157, 8, 40, 12, "1 of 2" },
  { 3003u, NSR_LIST, 7, 24, 190, 100, "" },
  { 2008u, NSR_GROUP, 7, 130, 189, 133, "Parameters" },
  { 3006u, NSR_STATIC, 14, 141, 45, 12, "Size:" },
  { 3007u, NSR_COMBO, 65, 141, 60, 66, "" },
  { 3009u, NSR_COMBO, 131, 141, 60, 66, "" },
  { 3018u, NSR_CHECK, 14, 166, 110, 12, "Create necessary quantity" },
  { 3016u, NSR_STATIC, 27, 181, 35, 12, "Quantity:" },
  { 3017u, NSR_EDIT, 65, 181, 60, 12, "" },
  { 1129u, NSR_STATIC, 131, 166, 60, 12, "Priority:" },
  { 1130u, NSR_DROP, 131, 180, 60, 66, "" },
  { 3010u, NSR_STATIC, 14, 210, 29, 18, "Origin:" },
  { 3011u, NSR_STATIC, 52, 205, 10, 12, "X:" },
  { 3013u, NSR_STATIC, 51, 220, 10, 12, "Y:" },
  { 3012u, NSR_COMBO, 65, 205, 60, 66, "" },
  { 3014u, NSR_COMBO, 65, 220, 60, 66, "" },
  { 3015u, NSR_BMPBTN, 131, 210, 18, 18, "|&Select" },
  { 1117u, NSR_BUTTON, 14, 243, 45, 14, "Material..." },
  { 1118u, NSR_EDIT, 65, 244, 126, 12, "" },
  { 0xffffffffu, NSR_VALUE, 204, 8, 190, 12, "" },
  { 3023u, NSR_OWNER, 204, 24, 190, 100, " " },
  { 3019u, NSR_GROUP, 204, 130, 191, 133, "Common Parameters (all sheets)" },
  { 3020u, NSR_STATIC, 211, 142, 50, 12, "Grain Direction:" },
  { 3021u, NSR_DROP, 265, 142, 122, 66, "" },
  { 7028u, NSR_STATIC, 211, 159, 50, 12, "Nesting Corner:" },
  { 7029u, NSR_DROP, 265, 159, 122, 66, "" },
  { 3025u, NSR_STATIC, 211, 176, 50, 12, "Fill Direction:" },
  { 3026u, NSR_DROP, 265, 176, 122, 66, "" },
  { 7033u, NSR_CHECK, 211, 198, 156, 10, "Guillotine cut" },
  { 3022u, NSR_CHECK, 211, 213, 156, 10, "Automatic sheet origins" },
  { 7001u, NSR_STATIC, 221, 226, 75, 12, "Sheet-Sheet Distance:" },
  { 7002u, NSR_COMBO, 301, 226, 60, 66, "" },
  { 7003u, NSR_STATIC, 211, 244, 50, 12, "Sheet Margin:" },
  { 7004u, NSR_COMBO, 264, 244, 60, 66, "" },
  { 3005u, NSR_EDIT, 7, 24, 190, 12, "" },
};
static const NsCtl NS_105[] = {   /* mcnestingres dialog 105 "Parts" (402 x 270 DLU) */
  { 4001u, NSR_STATIC, 7, 8, 120, 12, "Part List:" },
  { 4002u, NSR_STATIC, 157, 8, 40, 12, "1 of 2" },
  { 4003u, NSR_LIST, 7, 24, 190, 100, "" },
  { 2038u, NSR_GROUP, 7, 130, 190, 133, "Parameters" },
  { 4006u, NSR_STATIC, 14, 141, 45, 12, "Size:" },
  { 4007u, NSR_COMBO, 67, 141, 60, 66, "" },
  { 4009u, NSR_COMBO, 131, 141, 60, 66, "" },
  { 4010u, NSR_STATIC, 14, 156, 45, 12, "Minimum Qty:" },
  { 4011u, NSR_EDIT, 67, 156, 60, 12, "" },
  { 4012u, NSR_STATIC, 14, 171, 45, 12, "Filler Qty:" },
  { 4013u, NSR_EDIT, 67, 171, 60, 12, "" },
  { 4014u, NSR_STATIC, 14, 186, 45, 12, "Step Angle:" },
  { 4015u, NSR_COMBO, 67, 186, 60, 66, "" },
  { 4016u, NSR_STATIC, 14, 201, 45, 12, "Priority:" },
  { 4017u, NSR_DROP, 67, 201, 60, 66, "" },
  { 4032u, NSR_STATIC, 14, 216, 45, 12, "Label:" },
  { 4033u, NSR_EDIT, 67, 216, 124, 12, "" },
  { 1116u, NSR_BUTTON, 14, 230, 45, 14, "Material..." },
  { 1115u, NSR_EDIT, 67, 231, 124, 12, "" },
  { 4019u, NSR_CHECK, 14, 249, 80, 10, "Use bounding box" },
  { 1100u, NSR_CHECK, 101, 249, 91, 10, "Common Edge" },
  { 1086u, NSR_OWNER, 215, 8, 20, 12, " " },
  { 1088u, NSR_STATIC, 237, 8, 31, 12, "Profile" },
  { 1087u, NSR_OWNER, 271, 8, 20, 12, " " },
  { 1089u, NSR_STATIC, 293, 8, 28, 12, "Hole" },
  { 1084u, NSR_OWNER, 323, 8, 20, 12, " " },
  { 1085u, NSR_STATIC, 347, 8, 35, 12, "Ignore" },
  { 4018u, NSR_OWNER, 204, 24, 190, 100, " " },
  { 0xffffffffu, NSR_GROUP, 204, 130, 190, 51, "" },
  { 4023u, NSR_STATIC, 211, 142, 48, 12, "Grain Direction:" },
  { 4024u, NSR_DROP, 265, 142, 122, 66, "" },
  { 4021u, NSR_STATIC, 211, 159, 49, 12, "Mirror:" },
  { 4022u, NSR_DROP, 265, 159, 122, 66, "" },
  { 4039u, NSR_GROUP, 204, 187, 190, 76, "Common Parameters (all parts)" },
  { 7005u, NSR_STATIC, 211, 202, 60, 12, "Part-Part Distance:" },
  { 7006u, NSR_COMBO, 275, 202, 60, 66, "" },
  { 4034u, NSR_CHECK, 211, 218, 124, 12, "Add part labels" },
  { 4035u, NSR_RADIO, 222, 230, 35, 12, "Height" },
  { 4037u, NSR_RADIO, 327, 230, 60, 12, "Automatic" },
  { 4036u, NSR_COMBO, 260, 230, 60, 66, "" },
  { 7027u, NSR_CHECK, 211, 249, 75, 10, "Optimized pairs" },
  { 1013u, NSR_CHECK, 297, 249, 77, 10, "Fill all sheets" },
  { 4005u, NSR_EDIT, 7, 24, 190, 12, "" },
};
static const NsCtl NS_201[] = {   /* mcnestingres dialog 201 "Nesting Configuration" (379 x 350 DLU) */
  { 2028u, NSR_GROUP, 7, 7, 365, 131, "General Options" },
  { 2032u, NSR_CHECK, 17, 23, 145, 12, "Load default sheet" },
  { 2034u, NSR_CHECK, 17, 39, 145, 12, "Display Results dialog" },
  { 2033u, NSR_CHECK, 17, 55, 145, 12, "Display Group page" },
  { 7021u, NSR_CHECK, 17, 71, 145, 12, "Delete original chains" },
  { 7027u, NSR_CHECK, 17, 87, 145, 12, "Use Name as Label" },
  { 7016u, NSR_CHECK, 17, 103, 145, 12, "Restore last session" },
  { 2040u, NSR_CHECK, 17, 119, 145, 12, "Compress results" },
  { 7007u, NSR_CHECK, 203, 23, 94, 12, "Cycle colors starting with" },
  { 7008u, NSR_EDIT, 302, 23, 20, 12, "" },
  { 7009u, NSR_OWNER, 326, 23, 16, 12, " " },
  { 7010u, NSR_BMPBTN, 345, 17, 18, 18, "|Sele&ct" },
  { 7012u, NSR_CHECK, 203, 39, 94, 12, "Cycle levels starting with" },
  { 7013u, NSR_EDIT, 302, 39, 40, 12, "" },
  { 7014u, NSR_BMPBTN, 345, 39, 18, 18, "|Se&lect" },
  { 7017u, NSR_CHECK, 214, 55, 127, 12, "Only unused levels" },
  { 7200u, NSR_CHECK, 203, 71, 145, 12, "Higher resolution images in reports" },
  { 7018u, NSR_CHECK, 203, 87, 134, 12, "Save sheet scrap" },
  { 7019u, NSR_EDIT, 214, 103, 127, 12, "" },
  { 7020u, NSR_BMPBTN, 345, 100, 18, 18, "|&Select" },
  { 7028u, NSR_CHECK, 214, 119, 150, 12, "Check for duplicates during save (slower)" },
  { 7030u, NSR_GROUP, 7, 145, 180, 53, "Nesting Mode" },
  { 7022u, NSR_STATIC, 17, 161, 35, 12, "Accuracy:" },
  { 7023u, NSR_DROP, 59, 161, 70, 66, "" },
  { 7024u, NSR_EDIT, 135, 161, 40, 12, "" },
  { 7031u, NSR_RADIO, 21, 179, 72, 14, "Rectangular" },
  { 7032u, NSR_RADIO, 104, 179, 72, 14, "TrueShape" },
  { 2030u, NSR_GROUP, 192, 145, 180, 53, "TrueShape Options" },
  { 7025u, NSR_CHECK, 203, 161, 162, 12, "Nest smaller parts in the holes of larger parts" },
  { 7026u, NSR_CHECK, 214, 178, 150, 12, "Nest in holes before nesting on sheet" },
  { 2031u, NSR_GROUP, 7, 206, 365, 110, "Toolpath Attributes" },
  { 9107u, NSR_GROUP, 17, 218, 170, 25, " Nest to " },
  { 9108u, NSR_RADIO, 26, 229, 69, 10, "Tool center" },
  { 9109u, NSR_RADIO, 104, 229, 73, 10, "Tool diameter" },
  { 2035u, NSR_CHECK, 23, 261, 150, 10, "Automatically attach operation's geometry" },
  { 2036u, NSR_CHECK, 23, 276, 150, 12, "Automatically attach geometry (search)" },
  { 2039u, NSR_CHECK, 23, 292, 150, 12, "Create separate block drill operations" },
  { 9110u, NSR_GROUP, 192, 218, 170, 25, " Include rapid moves " },
  { 9111u, NSR_RADIO, 203, 229, 41, 10, "None" },
  { 9112u, NSR_RADIO, 254, 229, 40, 10, "All ops" },
  { 9113u, NSR_RADIO, 299, 229, 55, 10, "Only drill ops" },
  { 9115u, NSR_BUTTON, 192, 253, 170, 21, "Duplicate tool check during operation import..." },
  { 9114u, NSR_GROUP, 192, 278, 170, 30, " Tool origin location " },
  { 1098u, NSR_STATIC, 203, 290, 60, 12, "Corner of sheet:" },
  { 1099u, NSR_DROP, 271, 289, 80, 66, "" },
  { 1005u, NSR_GROUP, 7, 320, 79, 23, "" },
  { 1004u, NSR_CHECK, 12, 328, 69, 12, "Save as Defaults" },
  { 1u, NSR_BMPBTN, 259, 325, 36, 18, "|OK" },
  { 2u, NSR_BMPBTN, 297, 325, 36, 18, "|Cancel" },
  { 9u, NSR_BMPBTN, 336, 325, 36, 18, "|&Help" },
  { 1131u, NSR_GROUP, 17, 249, 170, 59, "" },
};
static const NsCtl NS_205[] = {   /* mcnestingres dialog 205 "Nesting Results" (636 x 430 DLU) */
  { 8019u, NSR_OWNER, 8, 28, 620, 320, "z" },
  { 8002u, NSR_DROP, 110, 8, 207, 66, "" },
  { 8014u, NSR_VALUE, 319, 8, 100, 12, "Utilization:" },
  { 8015u, NSR_VALUE, 421, 8, 207, 12, "000.00%" },
  { 9268u, NSR_CHECK, 8, 352, 300, 12, "Horizontal trim" },
  { 9275u, NSR_CHECK, 319, 352, 300, 12, "Vertical trim" },
  { 8006u, NSR_VALUE, 8, 368, 100, 14, "Total Size:" },
  { 8007u, NSR_VALUE, 110, 368, 207, 14, "0000000.00000 x 0000000.00000" },
  { 8008u, NSR_VALUE, 8, 384, 100, 14, "Nested Size:" },
  { 8009u, NSR_VALUE, 110, 384, 207, 14, "0000000.00000 x 0000000.00000" },
  { 8010u, NSR_VALUE, 319, 368, 100, 14, "Total Area:" },
  { 8011u, NSR_VALUE, 421, 368, 207, 14, "0000000.00000" },
  { 8012u, NSR_VALUE, 319, 384, 100, 14, "Nested Area:" },
  { 8013u, NSR_VALUE, 421, 384, 207, 14, "0000000.00000" },
  { 8016u, NSR_BMPBTN, 8, 404, 18, 18, "|De&tails" },
  { 8017u, NSR_BMPBTN, 28, 404, 18, 18, "|&Redo" },
  { 8018u, NSR_BMPBTN, 48, 404, 18, 18, "|&Save" },
  { 40032u, NSR_BMPBTN, 68, 404, 18, 18, "|&Copy" },
  { 40033u, NSR_BMPBTN, 88, 404, 18, 18, "|Copy &Blank (no parts)" },
  { 40031u, NSR_BMPBTN, 108, 404, 18, 18, "|&Delete" },
  { 9233u, NSR_BMPBTN, 128, 404, 18, 18, "|S&ort" },
  { 1102u, NSR_BMPBTN, 148, 404, 18, 18, "|Customi&ze..." },
  { 1101u, NSR_BMPBTN, 168, 404, 18, 18, "|Dra&g" },
  { 1u, NSR_BMPBTN, 517, 404, 36, 18, "|OK" },
  { 2u, NSR_BMPBTN, 555, 404, 36, 18, "|Cancel" },
  { 9u, NSR_BMPBTN, 592, 404, 36, 18, "|&Help" },
  { 8001u, NSR_VALUE, 8, 8, 100, 14, "Sheet (Result)" },
};

/* ------------------------------------------------------------------------------------------------ the write-back (§7) */
static int out_lv_sheet, out_lv_parts;       /* DAT_180130604 / DAT_180130600 */
/* FUN_1800c9a00 L138968: the level is in the database-levels list DAT_180140e68 or the allocated-levels list DAT_180140e40
   (FUN_1800c90d0's, toolpath only). The database list is made by FUN_1800c9df0 L139170 (RemoveAll, then the level of every
   entity get_ent(.., 1, 0x1f1fff) returns, each once) - in the run only by FUN_1800413e0 L45197-45201 at the start, when
   CycleLevels and CycleUnused are both on (L42580): the levels the run itself makes are not in it */
static int *db_levels, n_db_levels;
static void db_levels_collect(void) {
  n_db_levels = 0; free(db_levels); db_levels = malloc(sizeof(int) * (size_t)(F.nents + 1));
  for (int i = 0; i < F.nents; i++) if (F.ents[i].type) {
    int lv = F.ents[i].level, k = 0; while (k < n_db_levels && db_levels[k] != lv) k++;
    if (k == n_db_levels) db_levels[n_db_levels++] = lv;
  }
}
/* the allocated-levels list DAT_180140e40: FUN_1800c90d0 appends each level it hands out (L138632; the Results Delete's free level,
   FUN_180089890); cleared at the session end (FUN_180043e60 L47141, from FUN_18003e8d0) */
static int *alloc_levels, n_alloc_levels;
static int level_used(int lv) {
  for (int i = 0; i < n_db_levels; i++) if (db_levels[i] == lv) return 1;
  for (int i = 0; i < n_alloc_levels; i++) if (alloc_levels[i] == lv) return 1;
  return 0;
}
/* FUN_180039560 L39421: the level pair of the next output sheet (CycleLevels) */
static void next_levels(void) {
  int v = out_lv_sheet == 0 ? (out_lv_parts == 0 ? P.resultlevel : out_lv_parts + 1) : (out_lv_parts == 0 ? out_lv_sheet + 1 : out_lv_parts + 1);
  int pr[2];
  for (int k = 0; k < 2; k++) { while (P.cycleunused && level_used(v)) v++; pr[k] = v; v++; }
  out_lv_sheet = pr[0]; out_lv_parts = pr[1];
}
static void cycle_colors(void) {
  int c = P.resultcolor;
  for (int i = 0; i < nS; i++) { while (c == (cfg_back_color & 0xff) || c > 0xff) { c++; if (c > 0xff) c = 0; } S[i].color = c++; }
  for (int i = 0; i < nP; i++) { while (c == (cfg_back_color & 0xff) || c > 0xff) { c++; if (c > 0xff) c = 0; } Pt[i].color = c++; }
}
static void ent_set_attrs(int k, int ln_style, double width, int pt_style) {   /* store_ent's attributes e+0x10..+0x1f */
  if (k < 0) return; Ent *e = &F.ents[k];
  e->ln_style = ln_style; e->width = width; if (e->type == E_POINT) e->pt_style = pt_style;
  for (int q = e->first_poly; q < e->first_poly + e->npoly && q < F.npolys; q++) { F.polys[q].ln_style = (uint8_t)ln_style; F.polys[q].width = (uint8_t)width; }
}
/* XformEnt's maths on one point: (p - base) rotated CCW by a about Z, + P (CPart::XformEnt FUN_1800621b0 L67571-67634;
   CSheet::XformEnt FUN_180091b90 L97704-97737 has no rotation). The depth is kept (the translations are in XY, z = 0) */
typedef struct { double bx, by, c, s, px, py, a; } NsXf;
static V3 xf_pt(const NsXf *t, V3 p) { double x = p.x - t->bx, y = p.y - t->by; return (V3){ t->c * x - t->s * y + t->px, t->s * x + t->c * y + t->py, p.z }; }
/* a copy of entity e through t (chain_to_ents + XformEnt + store_ent), colour / level co / lv (-1 = the entity's own) */
/* XformEnt's first step (CPart::XformEnt FUN_1800621b0 L67500-67560 / CSheet::XformEnt FUN_180091b90 L97640-97700, param_16 / param_10 =
   the contour's view +0x44 when >= 2): xform_ent by mcViews[1] x mcViews[view] with the translation that keeps the base point at its
   depth - the copy lands in Top at its coordinates in the view (world -> view rows; the translation cancels). An arc: its centre and
   start point in the view, the sweep negated in an antiparallel view (the Top arc mccore's xform_ent leaves, XfStdView: cui_xform.c
   xf_arc_views). The last step (Top -> GetActiveViewNo(Planes, 0), L67640-67700) is the identity with the Cplane Top (ns_run) */
static int xf_copy(int e, const NsXf *t, int co, int lv, int V) {
  const Ent *en = &F.ents[e]; int k = -1; V3 z0 = { 0, 0, 0 };
  int col = co >= 0 ? co : en->color, lev = lv >= 0 ? lv : en->level;
  double R[9]; if (V < 2 || !mc_view_rows(V, R)) V = 1;
  if (en->type == E_POINT) { V3 p = xf_pt(t, V > 1 ? ns_w2v(R, en->a) : en->a); k = mcam_new_ent(&F, E_POINT, p, p, z0, 0, 0, 0, lev, col); }
  else if (en->type == E_LINE) { V3 a = xf_pt(t, V > 1 ? ns_w2v(R, en->a) : en->a), b = xf_pt(t, V > 1 ? ns_w2v(R, en->b) : en->b); k = mcam_new_ent(&F, E_LINE, a, b, z0, 0, 0, 0, lev, col); }
  else if (en->type == E_ARC && V == 1 && arc_view(en) == 1) { V3 c = xf_pt(t, en->c); double a0 = fmod(en->a0 + t->a, 2 * M_PI); if (a0 < 0) a0 += 2 * M_PI; k = mcam_new_ent(&F, E_ARC, c, c, c, en->r, a0, en->sw, lev, col); }
  else if (en->type == E_ARC) {
    if (V == 1) mc_view_rows(1, R);
    double ca = cos(en->a0), sa = sin(en->a0); V3 sw0;
    if (en->vw >= 2) sw0 = (V3){ en->c.x + en->r * (ca * en->vm[0] + sa * en->vm[3]), en->c.y + en->r * (ca * en->vm[1] + sa * en->vm[4]), en->c.z + en->r * (ca * en->vm[2] + sa * en->vm[5]) };
    else sw0 = (V3){ en->c.x + en->r * ca, en->c.y + en->r * sa, en->c.z };
    V3 cv = ns_w2v(R, en->c), sv = ns_w2v(R, sw0); int anti = 0; mc_views_parallel(arc_view(en), V, &anti);
    double a0 = atan2(sv.y - cv.y, sv.x - cv.x) + t->a, sw = anti ? -en->sw : en->sw;
    a0 = fmod(a0, 2 * M_PI); if (a0 < 0) a0 += 2 * M_PI;
    V3 c = xf_pt(t, cv); k = mcam_new_ent(&F, E_ARC, c, c, c, en->r, a0, sw, lev, col);
  }
  if (k >= 0) { en = &F.ents[e]; ent_set_attrs(k, en->ln_style, en->width, en->pt_style); }
  return k;
}
/* ctour_to_ents (mastercam.dll @18013f0a0; decomp/nest/mastercam_ctour.c) of a contour that is no chain (the L x W rectangle
   FUN_1800c3180, a session's contours), through t: one record = a point at it; else per 'L' record a line from the previous
   record's point to it, per 'A' record an arc - constr_arc_new(previous point, end, centre, the CW flag) and, when CW, start +=
   sweep (restrict_angle 0 .. 2pi - tol) and the sweep made positive; with the circle flag constr_circle(the point, centre) - a
   full circle from the point; depth = the contour's; colour / level co / lv; FUN_18001c1b0's attributes. Ours: constr_arc_new /
   constr_circle as the geometry (radius from the start point, angles by atan2) - their code is mastercam.dll's, not read */
static void ct_ents(const NsCtour *c, const NsXf *t, int co, int lv) {
  V3 z0 = { 0, 0, 0 };
  if (c->n == 1) { V3 p = xf_pt(t, (V3){ c->r[0].x, c->r[0].y, c->depth }); int k = mcam_new_ent(&F, E_POINT, p, p, z0, 0, 0, 0, lv, co); ent_set_attrs(k, 1, c->width, c->pt_style); return; }
  for (int i = 1; i < c->n; i++) {
    const NsRec *q = &c->r[i], *pq = &c->r[i - 1];
    if (q->t == 'L') {
      V3 a = xf_pt(t, (V3){ pq->x, pq->y, c->depth }), b = xf_pt(t, (V3){ q->x, q->y, c->depth });
      int k = mcam_new_ent(&F, E_LINE, a, b, z0, 0, 0, 0, lv, co); ent_set_attrs(k, c->ln_style, c->width, 1);
    } else if (q->t == 'A') {
      double r, a0, sw;
      if (q->circle) { r = hypot(q->x - q->cx, q->y - q->cy); a0 = atan2(q->y - q->cy, q->x - q->cx); sw = 2 * M_PI; }
      else {
        r = hypot(pq->x - q->cx, pq->y - q->cy);
        double sa = atan2(pq->y - q->cy, pq->x - q->cx), ea = atan2(q->y - q->cy, q->x - q->cx);
        sw = q->cw ? sa - ea : ea - sa; while (sw <= 0) sw += 2 * M_PI; while (sw > 2 * M_PI) sw -= 2 * M_PI;
        a0 = q->cw ? ea : sa;
      }
      a0 += t->a; while (a0 < 0) a0 += 2 * M_PI; while (a0 >= 2 * M_PI - mc_systol.tol) a0 -= 2 * M_PI; if (a0 < 0) a0 = 0;
      V3 cc = xf_pt(t, (V3){ q->cx, q->cy, c->depth });
      int k = mcam_new_ent(&F, E_ARC, cc, cc, cc, r, a0, sw, lv, co); ent_set_attrs(k, c->ln_style, c->width, 1);
    }
  }
}
/* FUN_1800c3180 L134548: the L x W rectangle contour (records (0,0), (0,W), (L,W), (L,0), (0,0) - clockwise; outer; view 1;
   depth 0; main colour / level / attributes), appended for good to a part / sheet without contours (L62049-62055, L94714) */
static void rect_ctour(NsCtour **pct, int *pnct, double L, double W) {
  *pct = realloc(*pct, sizeof(NsCtour) * (size_t)(*pnct + 1)); NsCtour *c = &(*pct)[(*pnct)++]; memset(c, 0, sizeof *c);
  c->n = 5; c->r = calloc(5, sizeof(NsRec)); double xs[5] = { 0, 0, L, L, 0 }, ys[5] = { 0, W, W, 0, 0 };
  for (int i = 0; i < 5; i++) { c->r[i].t = 'L'; c->r[i].x = xs[i]; c->r[i].y = ys[i]; }
  c->type = 0; c->color = geo_color; c->level = geo_level; c->ln_style = cfg_ln_style; c->width = cfg_ln_width >= 1 ? cfg_ln_width : 1; c->pt_style = 1;
}
static int is_chained(const NsCtour *c) { return c->nsrc > 0; }

/* ns_output FUN_180007b60 L5455 for one result: the sheet (FUN_18008d820 L94604 + CSheet::XformEnt), then every instance
   (FUN_18005a260 L61824 + CPart::XformEnt) */
/* the sheet-name note of an output sheet. FUN_18008d820 L94890-94916: with a non-empty output name (sheet+0x10c = 10161
   "%s - Result %d" of the sheet name and the output count + 1, L5749-5752) one more entity follows the outer contour's
   (code 0) entities, at (sheet+0x360, sheet+0x368) in the contour's view and depth (+ BASEPOINT for a chained sheet, which
   CSheet::XformEnt takes off again): +0x360 = 0 (ctor FUN_180089fa0 L92181), +0x368 = clearance / 10 + width (set for every
   sheet at the run start, FUN_180041d00 L45676-45682). CSheet::XformEnt FUN_180091b90 L97806-97822 makes it a note:
   calc_note(the name) - the user's note settings (InitNOTE) - then height (SheetToSheetDist - clearance * 0.02) * 0.8 (1.0 below
   tol), extra spacing 0.1 (+0xd0), angle / slant / character rotation 0 (+0xa8 / +0xb0 / +0xb8), font 0 (+0xf4), no text box
   (+0xf6), +0xf0 = low byte | 0xc00 (Left, Base, path Right), +0xf2 = (+0xf2 & 0xfff3) | 1 (no mirror, proportional);
   store_ent with the sheet's colour / level (L94741-94770: CycleColors sheet+0x354, CycleLevels DAT_180130604, else the main
   colour / level - a chained sheet's -1 = the note's own, InitNOTE's main colour / level [inferred as for the contours]) */
static void sheet_name_note(const NsResult *r, const NsSheet *s, double depth, int sco, int slv) {
  char nm[320]; snprintf(nm, sizeof nm, "%s - Result %d", s->name, r->copy + 1);
  DnGlobals g; dn_globals_config(&g, mcam_is_metric(&F));
  double h = (P.s2s - clearance() * 0.02) * 0.8; if (h < mc_systol.tol) h = 1.0;
  g.height = h; g.extra_sp = 0.1; g.angle = 0; g.slant = 0; g.rot = 0; g.font = 0; g.text_bx = 0;
  g.h_aln = 0; g.v_aln = 3; g.text_path = 0; g.mirror = 0; g.space_sty = 1;
  g.color = sco >= 0 ? sco : geo_color; g.level = slv >= 0 ? slv : geo_level;
  double p[3] = { r->ox + 0.0, r->oy + clearance() / 10.0 + s->wid, depth };
  mcam_note_create(&F, &g, 1, p, nm);
}
/* ---- the trims (decoded 2026-11-05; formats/nesting_addin.md section 8 "Trim lines") */
/* the result's engine sheet L x W (+0x140 / +0x148 = +0x780 / +0x788): TrueShape the CNST_Result's; Rectangular the sheet's, + 2|c| when
   c < 0 (RNInqNestedSheet, as the Results dialog shows them, FUN_180084d30) */
static void res_LW(const NsResult *r, double *L, double *W) {
  if (r->ts) { *L = r->len; *W = r->wid; return; }
  *L = S[r->sheet].len; *W = S[r->sheet].wid;
  if (collar_c < 0) { *L += 2 * fabs(collar_c); *W += 2 * fabs(collar_c); }
}
/* EntityMinMax of an entity made here (points / lines: their ends; arcs: ends + the quadrant points inside the sweep - the rule of
   cui_xform.c sel_box_centre; the nest output's arcs are XY arcs) */
static void ent_minmax(int e, double lo[3], double hi[3]) {
  const Ent *en = &F.ents[e];
#define NS_ADD(P) do { V3 q_ = (P); if (q_.x < lo[0]) lo[0] = q_.x; if (q_.y < lo[1]) lo[1] = q_.y; if (q_.z < lo[2]) lo[2] = q_.z; \
                       if (q_.x > hi[0]) hi[0] = q_.x; if (q_.y > hi[1]) hi[1] = q_.y; if (q_.z > hi[2]) hi[2] = q_.z; } while (0)
  NS_ADD(en->a); if (en->type != E_POINT) NS_ADD(en->b);
  if (en->type == E_ARC) for (int q = 0; q < 4; q++) {
    double a0 = en->sw < 0 ? en->a0 + en->sw : en->a0, sw = fabs(en->sw), t = q * M_PI / 2, d = fmod(t - a0, 2 * M_PI); if (d < 0) d += 2 * M_PI;
    if (d <= sw + 1e-12) NS_ADD(mcam_arc_pt(en, t)); }
#undef NS_ADD
}
/* FUN_180087530 L90334: the extent +0x698 (min) / +0x6b0 (max) grown by a box (min point, max point) when it is valid */
static void ext_grow(NsResult *r, const double b[6], int ok) {
  if (!ok) return;
  for (int k = 0; k < 3; k++) { if (b[k] < r->lo[k]) r->lo[k] = b[k]; if (b[3 + k] < r->lo[k]) r->lo[k] = b[3 + k];
                                if (b[k] > r->hi[k]) r->hi[k] = b[k]; if (b[3 + k] > r->hi[k]) r->hi[k] = b[3 + k]; }
}
/* FUN_18008c160 L93638 (the sheet box): the first contour of type 0 (FUN_1800c3180's L x W rectangle added for good when there is
   none) - a line (0, 0, depth) - (L, W, depth) (sheet +0x230 / +0x238) through CSheet::XformEnt with the result origin (param_5 0: not
   a chain copy - the records' transform of ns_output); its two end points are the box (as they come) */
static int sheet_box(const NsResult *r, double b[6]) {
  NsSheet *s = &S[r->sheet];
  if (s->nct == 0) rect_ctour(&s->ct, &s->nct, s->len, s->wid);
  NsXf ts = { 0, 0, 1, 0, r->ox, r->oy, 0 };
  for (int k = 0; k < s->nct; k++) if (s->ct[k].type == 0) {
    V3 p = xf_pt(&ts, (V3){ 0, 0, s->ct[k].depth }), q = xf_pt(&ts, (V3){ s->len, s->wid, s->ct[k].depth });
    b[0] = p.x; b[1] = p.y; b[2] = p.z; b[3] = q.x; b[4] = q.y; b[5] = q.z; return 1;
  }
  return 0;
}
/* FUN_180085c80 L89333-89690 in the active view (GetActiveViewNo(Planes, 0): Top - ns_run refuses another Cplane). k = the clearance
   FUN_1800cad50 (P.p2p), c = the collar DAT_180136e58, corner = STARTINGCORNER P+0xb0. L, W, the part area pa (< tol: the util not
   computed, the recount passes -1.0), the sheet box b (valid ok). Fills the result's trim values; the lines (p1 -> p2) in hl / vl */
static void calc_trims(NsResult *r, double L, double W, double pa, int ok, const double b[6], int *mkh, V3 hl[2], int *mkv, V3 vl[2]) {
  double k = clearance(), c = collar_c, tol = mc_systol.tol, dh = 0, dv = 0; int corner = P.corner, v;
  *mkh = *mkv = 0;
  /* horizontal (L89371-89470) */
  v = 0;
  if (corner == 1 || corner == 2) { r->hcut = k + r->hi[1] + c; if (c < 0) r->hcut = r->hi[1]; v = 1; }
  if (corner == 3 || corner == 4) { r->hcut = r->lo[1] - c - k; if (c < 0) r->hcut = r->lo[1]; v = 1; }
  if (!ok) v = 0;
  else if (v) {
    double y = r->hcut;
    if (corner == 1 || corner == 2) { v = k * 0.1 + y < b[4]; dh = fabs(fabs(y) - fabs(b[1])); }
    if (corner == 3 || corner == 4) { v = b[1] < y - k * 0.1; dh = fabs(fabs(y) - fabs(b[4])); }
    if (v) {                                                  /* the line across the box at y (min x -> max x for corners 1 / 4) */
      V3 a = { b[0], y, b[2] }, e = { b[3], y, b[5] };
      if (corner == 1 || corner == 4) { hl[0] = a; hl[1] = e; } else { hl[0] = e; hl[1] = a; }
      *mkh = 1;
    }
  }
  r->hval = v;
  if (v) { r->ht[0] = L; r->ht[1] = dh; r->ht[2] = dh * L; if (pa > tol) r->ht[3] = pa / (dh * L) * 100.0; }
  /* vertical (L89482-89580) */
  v = 0;
  if (corner == 1 || corner == 4) { r->vcut = k + r->hi[0] + c; if (c < 0) r->vcut = r->hi[0]; v = 1; }
  if (corner == 2 || corner == 3) { r->vcut = r->lo[0] - c - k; if (c < 0) r->vcut = r->lo[0]; v = 1; }
  if (!ok) v = 0;
  else if (v) {
    double x = r->vcut;
    if (corner == 1 || corner == 4) { v = k * 0.1 + x < b[3]; dv = fabs(fabs(x) - fabs(b[0])); }
    if (corner == 2 || corner == 3) { v = b[0] < x - k * 0.1; dv = fabs(fabs(x) - fabs(b[3])); }
    if (v) {                                                  /* the line across the box at x (min y -> max y for corners 1 / 2) */
      V3 a = { x, b[1], b[2] }, e = { x, b[4], b[5] };
      if (corner == 1 || corner == 2) { vl[0] = a; vl[1] = e; } else { vl[0] = e; vl[1] = a; }
      *mkv = 1;
    }
  }
  r->vval = v;
  if (v) { r->vt[0] = dv; r->vt[1] = W; r->vt[2] = dv * W; if (pa > tol) r->vt[3] = pa / (dv * W) * 100.0; }
  if (r->hval && r->vval) { r->bt[0] = dv; r->bt[1] = dh; r->bt[2] = dh * dv; if (pa > tol) r->bt[3] = pa / (dh * dv) * 100.0; }
  /* geometry mode (L89665-89685): the cutoffs = the lines' distances from the box's min y / min x (the line's first point) */
  if (r->hval) r->hcut = fabs(hl[0].y - b[1]);
  if (r->vval) r->vcut = fabs(vl[0].x - b[0]);
}
/* a trim line: FUN_180006e70 (a line entity, attributes DAT_1801305e0: width 1.0), line style 2 (ent +0x18), store_ent(.., 1, the
   sheet colour, the sheet level) (asm @180009011-18000910c); blanked unless active (XfBlankEntity / XfUnblankEntity, L6283-6309) */
static int trim_line(const V3 l[2], int co, int lv, int active) {
  V3 z0 = { 0, 0, 0 };
  int k = mcam_new_ent(&F, E_LINE, l[0], l[1], z0, 0, 0, 0, lv, co);
  if (k < 0) return -1;
  ent_set_attrs(k, 2, 1.0, 1);
  mcam_ent_blank(&F, k, !active);
  return k;
}
static void ns_output(NsResult *r) {
  NsSheet *s = &S[r->sheet];
  if (P.cyclelevels) next_levels();                                         /* L5764-5765 */
  double ox, oy;                                                            /* §7.1 (L5731-5747) */
  if (!P.autoorigins) { ox = s->ox; oy = s->oy; }
  else { double v = (P.s2s + s->len) * S[r->sheet].outputs; if (fabs(v) < mc_systol.tol) v = 0; ox = v + s->ax; oy = s->ay; }
  r->ox = ox; r->oy = oy; r->copy = S[r->sheet].outputs;
  r->lv_sheet = out_lv_sheet; r->lv_parts = out_lv_parts;
  r->ent_from = F.nents;
  for (int k = 0; k < 3; k++) { r->lo[k] = 1e38; r->hi[k] = -1e38; }      /* the CResult ctor's extent */
  r->hval = r->vval = 0; r->hent = r->vent = -1;
  memset(r->ht, 0, sizeof r->ht); memset(r->vt, 0, sizeof r->vt); memset(r->bt, 0, sizeof r->bt);
  /* +0x5a4 / +0x5a0: the sheet's per-output arrays +0x2f0 / +0x318 at index outputs - 1 when present, else TrimActive (P+0xc,
     L5888-5911). The arrays are filled only by FUN_180094550 (L99324: SetAtGrow from the MCLink job records +0x114 / +0x115), reached
     from FUN_18003bbc0's MCLink message 0x6c item 0x1f (L41340-41366) - never in this program: TrimActive. A blank result keeps the
     ctor's 0 (L5881-5885) */
  r->hact = r->vact = r->parts ? P.cp[0xc] != 0 : 0;
  /* the sheet (FUN_18008d820): colour CycleColors -> sheet+0x354, else the entity's own; level CycleLevels -> DAT_180130604 */
  int sco = P.cyclecolors ? S[r->sheet].color : -1, slv = P.cyclelevels ? out_lv_sheet : -1;
  if (s->nct == 0) rect_ctour(&s->ct, &s->nct, s->len, s->wid);
  S[r->sheet].outputs++;                                                /* +0x344++ (L94711) */
  NsXf ts = { 0, 0, 1, 0, ox, oy, 0 };
  int named = 0;
  for (int k = 0; k < s->nct; k++) {
    NsCtour *c = &s->ct[k];
    if (is_chained(c)) { NsXf t = ts; t.bx = s->basex; t.by = s->basey; for (int i = 0; i < c->nsrc; i++) if (F.ents[c->src[i]].type) xf_copy(c->src[i], &t, sco, slv, c->view); }
    else ct_ents(c, &ts, sco >= 0 ? sco : c->color, slv >= 0 ? slv : c->level);
    if (c->type == 0 && !named) { sheet_name_note(r, s, c->depth, sco, slv); named = 1; }
  }
  r->sheet_to = F.nents;
  double sbox[6]; int sok = sheet_box(r, sbox);                            /* FUN_18008d820's box (FUN_18008c160, L94604+446) */
  if (!r->parts) ext_grow(r, sbox, sok);                                   /* a blank result: the sheet box (L5881-5885) */
  /* the parts (FUN_18005a260): the instance position (L6034-6085), the collar offset (L5704-5729) */
  double off = collar_c < 0 ? collar_c : 0;
  /* the CNST_Parts (NestOutInst.entry: Rectangular the types numbered by first appearance in the RNInqNestedPart list,
     FUN_1800056d0 L3884; TrueShape one per NInqNestedPartType) in order, then each one's instances in list order (FUN_180005aa0 /
     FUN_180005980, L4465-4501; the loop over +0x198, L5926-5935) */
  int *ord = malloc(sizeof(int) * (size_t)(r->ninst ? r->ninst : 1)), no = 0;
  for (int i = 0; i < r->ninst; i++) {
    int first = 1; for (int j = 0; j < i; j++) if (r->inst[j].entry == r->inst[i].entry) first = 0;
    if (first) for (int j = i; j < r->ninst; j++) if (r->inst[j].entry == r->inst[i].entry) ord[no++] = j;
  }
  free(r->pq); r->pq = malloc(sizeof(int) * (size_t)(nP ? nP : 1)); for (int q = 0; q < nP; q++) r->pq[q] = -1;
  int ordn = 0;                                                             /* part +0x4c4 */
  for (int oi = 0; oi < no; oi++) {
    NestOutInst *in = &r->inst[ord[oi]];
    if (oi == 0 || in->entry != r->inst[ord[oi - 1]].entry) {               /* a new CNST_Part entry: +0x4c4 = 0 (L5952) */
      ordn = 0;
      /* FUN_180085bb0 (L6238-6250, once per part): the result's copy of the part with +0x250 = the entry's instance count */
      int cnt = 0; for (int j = oi; j < no && r->inst[ord[j]].entry == in->entry; j++) cnt++;
      for (int q = 0, n = 0; q < nP; q++) if (!Pt[q].skip) { if (n == in->part) { if (r->pq[q] < 0) r->pq[q] = cnt; break; } n++; }
    }
    if (in->part < 0) continue;
    NsPart *p = NULL; int pi = -1;
    for (int q = 0, n = 0; q < nP; q++) if (!Pt[q].skip) { if (n == in->part) { p = &Pt[q]; pi = q; break; } n++; }
    if (!p) continue;
    ordn++;                                                                 /* +0x4c4++ before the instance (L6121) */
    int from = F.nents;
    double x = fabs(in->x) < mc_systol.tol ? 0 : in->x, y = fabs(in->y) < mc_systol.tol ? 0 : in->y;
    double px = ox + x, py = oy + y;
    if (!P.trueshape && in->angle > 1.0) px += p->wid;                      /* L6075-6077 */
    px += off; py += off;
    if (fabs(px) < mc_systol.tol) px = 0; if (fabs(py) < mc_systol.tol) py = 0;
    int pco = P.cyclecolors ? Pt[pi].color : -1, plv = P.cyclelevels ? out_lv_parts : -1;
    if (p->nct == 0) { rect_ctour(&p->ct, &p->nct, p->len, p->wid); p->mmx = p->len; p->mmy = p->wid; }
    double a = in->angle * M_PI / 180.0;
    NsXf t = { 0, 0, cos(a), sin(a), px, py, a };
    for (int k = 0; k < p->nct; k++) {
      NsCtour *c = &p->ct[k];
      if (is_chained(c)) { NsXf tc = t; tc.bx = p->basex; tc.by = p->basey; for (int e = 0; e < c->nsrc; e++) if (F.ents[c->src[e]].type) xf_copy(c->src[e], &tc, pco, plv, c->view); }
      else ct_ents(c, &t, pco >= 0 ? pco : c->color, plv >= 0 ? plv : c->level);
    }
    /* the PPI (CPart::Place L62998-63065): +0xa4 part id, +0xac the ordinal, +0x34 the element, +0xb4 the result (L6405-6418) */
    if (nPPI == capPPI) { capPPI = capPPI ? capPPI * 2 : 64; PPI = realloc(PPI, sizeof(NsPPI) * (size_t)capPPI); }
    NsPPI *pp = &PPI[nPPI++]; pp->rid = r->rid; pp->part_id = p->id; pp->ord = ordn; pp->elem = in->elem < 0 ? 0 : in->elem;
    pp->from = from; pp->to = F.nents;
    /* CPart::Place's box (param_9, L62817-62822): part +0x520..+0x548 from 1e38 / -1e38 (L62101-62106) grown by EntityMinMax of every
       entity XformEnt made (FUN_18005f810 L65593, called at L62705) - valid (param_8 = 1, L62823) when it made them; ns_output grows the
       result's extent by it (FUN_180087530, L6191) */
    double ib[6] = { 1e38, 1e38, 1e38, -1e38, -1e38, -1e38 };
    for (int e = from; e < F.nents; e++) if (F.ents[e].type) ent_minmax(e, ib, ib + 3);
    ext_grow(r, ib, F.nents > from);
  }
  free(ord);
  /* the trims (L6268-6310): FUN_180085c80(result, L +0x140, W +0x148, part area +0x158, the sheet box), then each line made stored
     with the sheet's colour / level and blanked unless active */
  { double L, W; res_LW(r, &L, &W); V3 hl[2], vl[2]; int mh, mv;
    calc_trims(r, L, W, r->parts ? r->partArea : 0, sok, sbox, &mh, hl, &mv, vl);
    int co = sco >= 0 ? sco : geo_color, lv = slv >= 0 ? slv : geo_level;   /* local_6a70 / local_6a6c: the sheet's, else main (L5772-5777) */
    if (mh) r->hent = trim_line(hl, co, lv, r->hact);
    if (mv) r->vent = trim_line(vl, co, lv, r->vact); }
  r->ent_to = F.nents;
}
/* the shown result's trim lines blanked / unblanked by +0x5a4 / +0x5a0 (FUN_18007d8d0 L84455-84470, through FUN_180081d00 from the check
   box handlers FUN_180080a70 L86318 / FUN_180080b90 L86371) */
static void trim_show(NsResult *r) {
  if (r->hent >= 0 && F.ents[r->hent].type) mcam_ent_blank(&F, r->hent, !r->hact);
  if (r->vent >= 0 && F.ents[r->vent].type) mcam_ent_blank(&F, r->vent, !r->vact);
  scene_reupload(); overlay_dirty = 1;
}
/* FUN_1800868c0 L89733 (param 0 at OK, L42985): a line deleted when it is not valid or not active (param 1: every line) */
static void trim_delete(NsResult *r, int all) {
  int *e[2] = { &r->hent, &r->vent }, val[2] = { r->hval && r->hact, r->vval && r->vact };
  for (int i = 0; i < 2; i++) if (*e[i] >= 0 && (all || !val[i])) {
    int x = *e[i]; if (F.ents[x].type) { if (mod_from >= 0 && x >= mod_from) ent_off(x); else mod_del(x); }
    *e[i] = -1;
  }
}

/* ------------------------------------------------------------------------------------------------ the session driver (§2) */
static NsParams Pc;                        /* the Configuration dialog's copy (CCfgDlg +0x1550) */
static int cfg_rc;                         /* 0x3de: the Rectangular / TrueShape radio changed */
static void session_clear(void) {
  for (int i = 0; i < nS; i++) ct_free(S[i].ct, S[i].nct);
  for (int i = 0; i < nP; i++) ct_free(Pt[i].ct, Pt[i].nct);
  nS = nP = 0; results_free(); sel_sheet = sel_part = -1;
}
/* FUN_18003b120 L40786: a copy of the default sheet named "Sheet #" + id (10084) */
static void add_default_sheet(void) {
  NsSheet *s = sheet_push(); *s = Sdef; s->ct = NULL; s->nct = 0;
  s->id = sheet_counter++; snprintf(s->name, sizeof s->name, "Sheet #%d", s->id);
}
/* CO do_nest_g -> NE m_nest_g -> FUN_18003eac0(2) / FUN_18003ce30: the session starts (FUN_180043fd0 resets it, the counter
   DAT_180136e84 = 1, the defaults loaded FUN_18003b250(0)), Load default sheet (L42468-42470), the sheet shown */
void ns_start(void) {
  if (ns_open || ns_res_open || ns_chaining) { ns_msg("Warning - nesting already running."); return; }   /* 10101 (L43327) */
  if (geo_cmd) geo_end();
  if (!loaded && cui_hooks.new_part) cui_hooks.new_part();   /* nothing open: a new empty part (as geo_start; Mastercam always has one) */
  if (!loaded) return;
  ns_metric = mcam_is_metric(&F) == 1;
  session_clear(); sheet_counter = 1; part_counter = 1;
  ns_load_defaults();                              /* FUN_18003b250(0) */
  if (P.loaddefsheet && nS < 1) add_default_sheet();
  sel_sheet = nS ? 0 : -1;                    /* ours: the first sheet selected (the list's initial selection NOT DECODED) */
  ns_open = 1; ns_cfg_open = 0; ns_res_open = 0;
  if (!ns_inited) { ns_page = 0; ns_inited = 1; }   /* DAT_1801356ac: Sheets the first time, then the last page (L69959-69966) */
}
static void ns_close(void) { ns_open = ns_cfg_open = ns_res_open = 0; session_clear(); n_alloc_levels = 0; }   /* FUN_18003e8d0 -> FUN_180043e60 (L47141) */
/* UndoTransTerminate (L42676-42678): the run's entities go */
static void undo_terminate(void) {
  if (run_from < 0) return;
  int had = F.nents > mod_from && mod_from >= 0;
  mod_end();
  if (had) { do_undo(); if (nredo) urec_free(&redo_st[--nredo]); }
  run_from = -1;
}
/* OK in the sheet: the pre-checks, UndoTransStart, the run FUN_18003fb40(0) with ns_output for every result (FUN_180007ac0),
   then the Results dialog (DISPLAYRESULTSDLG) or the success path */
static void ns_commit(void);
static int ns_esc_pressed(void) { return cui_hooks.key_press && cui_hooks.key_press(0x1b); }   /* KeyPress(0x1b) */
static void ns_run(void) {
  if (!run_prechecks()) return;
  /* ours: CPart / CSheet::XformEnt end in the active Cplane's view (GetActiveViewNo(Planes, 0), L67340-67345 / L97610) - this program
     outputs in Top only: another Cplane (a project's ACTPLANE) is NOT PORTED (the sheet-name note and arcs in a view) */
  if (plane_active[0] > 1) { ns_msg("The Cplane is not Top: nesting output in another Cplane is NOT PORTED."); return; }
  NestEngineFn eng = P.trueshape ? nest_engine_ts : nest_engine_rect;   /* FUN_180043040: NOpenNestLib / RNInitRectangleNest */
  results_free();
  for (int i = 0; i < nS; i++) S[i].outputs = 0;                           /* +0x344 = 0 (L44076) */
  if (P.autoorigins && nS) {                                                /* FUN_180037d60 L38418 */
    for (int i = 0; i < nS; i++) S[i].ax = S[0].ox;
    S[0].ay = S[0].oy;
    for (int i = 1; i < nS; i++) S[i].ay = S[i - 1].ay - (P.s2s + S[i].wid);
  }
  cycle_colors();                                                           /* FUN_1800412e0 (L42579) */
  n_db_levels = 0; if (P.cyclelevels && P.cycleunused) db_levels_collect();  /* FUN_1800413e0 (L42580) */
  out_lv_sheet = out_lv_parts = 0;                                          /* DAT_180130604 / 600 = 0 (L42582-42583) */
  mod_begin(); run_from = F.nents;                                          /* UndoTransStart (L42574-42577) */
  NestJob job; job_build(&job);
  /* ours: the Rectangular engine port stops the program past 10000 placed rectangles on one sheet (nest_rect.c rn_die: the
     original silently drops them) - a run that could reach that many is refused before the engine starts */
  if (eng == nest_engine_rect) {
    double cap = 0;
    for (int sj = 0; sj < job.nsheets; sj++) {
      double c = 0, sa = job.sheets[sj].len * job.sheets[sj].wid;
      for (int pj = 0; pj < job.nparts; pj++) { double pa = (job.parts[pj].len + job.clearance) * (job.parts[pj].wid + job.clearance), k = pa > 0 ? sa / pa : 1e9; c += k < job.parts[pj].maxq ? k : job.parts[pj].maxq; }
      if (c > cap) cap = c;
    }
    if (cap > 9990) {
      job_free(&job); undo_terminate();
      ns_msg("Too many part instances for one sheet (more than 10000 can fit):\nthe rectangular nesting engine of this program stops there. Lower the quantities.");
      return;
    }
  }
  if (getenv("NEST_JOB_DUMP")) nest_job_write(getenv("NEST_JOB_DUMP"), &job);   /* ours: the job for the engine oracle (tests) */
  NestOut out = { 0 }; char err[256] = "";
  /* the TrueShape progress callback FUN_180006700 asks KeyPress(0x1b) first and stops the engine (returns 0) on Esc (L4613-4616;
     viewer/nest/nest_ts_job.c cb_progress, hook nest_ts_cancel). The Rectangular engine never calls its progress callback
     (formats/nesting_engine.md section 2: RN's progress setter stores it, nothing calls it), so Esc does not stop it */
  nest_ts_cancel = ns_esc_pressed;
  int ok = eng(&job, &out, err, sizeof err);
  nest_ts_cancel = NULL;
  if (cui_hooks.key_press_end) cui_hooks.key_press_end();                /* the input left in the queue: dispatched after the run */
  if (ok) for (int i = 0; i < out.ns; i++) {                                /* FUN_180005f90: one result per copy */
    NestOutSheet *os = &out.s[i]; if (os->sheet < 0) continue;
    for (int k = 0; k < os->copies; k++) {
      R = realloc(R, sizeof(NsResult) * (size_t)(nR + 1)); NsResult *r = &R[nR++]; memset(r, 0, sizeof *r);
      r->sheet = os->sheet; r->copies = os->copies; r->util = os->util; r->parts = 1;   /* ns_output(.., param_3 = 1) */
      if (eng != nest_engine_rect) {               /* FUN_180007b60 L6330-6370 (param_3 = 1, DAT_1801352ac != 0): the result's own values */
        r->ts = 1; r->len = os->len; r->wid = os->wid; r->area = os->area; r->cutArea = os->cutArea; r->partArea = os->partArea;
        r->nestLen = os->nestLen; r->nestWid = os->nestWid;
      }
      r->inst = malloc(sizeof(NestOutInst) * (size_t)(os->ninst ? os->ninst : 1)); memcpy(r->inst, os->inst, sizeof(NestOutInst) * (size_t)os->ninst); r->ninst = os->ninst;
    }
  }
  /* Rectangular: DAT_180134638, the last sheet-result callback's (§1.4, L6362-6363); TrueShape: +0x158 / +0x150 * 100 per result (L6366) */
  double lastUtil = out.ns ? out.s[out.ns - 1].util : 0;
  if (eng == nest_engine_rect) for (int i = 0; i < nR; i++) R[i].util = lastUtil;
  /* the engine's sheet L x W as RNInqNestedSheet gives it back (+0x140 / +0x148) */
  for (int i = 0; ok && i < nR; i++) { R[i].rid = i; ns_output(&R[i]); }   /* +0x8a0 = the count at its append (L6396-6418) */
  nest_out_free(&out); job_free(&job);
  scene_reupload(); overlay_dirty = 1;
  if (!ok || nR < 1) {                                                      /* 10102 + 10103 (L42647), back to the dialog */
    undo_terminate(); results_free();
    if (!strncmp(err, "nest_ts: ", 9)) ns_msg("Warning - Nesting has encountered a problem.\n(returning to the nesting dialog)\n\n%s", err + 9);   /* ours: the engine port stopped (NOT PORTED) */
    else ns_msg("Warning - Nesting has encountered a problem.\n(returning to the nesting dialog)");
    return;
  }
  ns_open = 0;
  if (P.dispresults) { ns_res_open = 1; res_sel = 0; res_main_level = geo_level; return; }   /* CResultDlg (L42636-42646); FUN_1800892c0 at its init */
  ns_commit();
}
/* the success path (L42809-42870): Delete original chains (FUN_1800577b0 per part; sheets FUN_18008ca40), UndoTransCommit */
static void ns_commit(void) {
  for (int i = 0; i < nP; i++) {
    NsPart *p = &Pt[i]; if (!(P.deletechains || p->delgeo)) continue;
    for (int k = 0; k < p->nct; k++) for (int e = 0; e < p->ct[k].nsrc; e++) mod_del(p->ct[k].src[e]);
  }
  for (int i = 0; i < nS; i++) {
    NsSheet *s = &S[i]; if (!(P.deletechains || s->delgeo)) continue;
    for (int k = 0; k < s->nct; k++) for (int e = 0; e < s->ct[k].nsrc; e++) mod_del(s->ct[k].src[e]);
  }
  for (int i = 0; i < nR; i++) trim_delete(&R[i], 0);                       /* FUN_1800868c0(result, 0) per result (L42985) */
  mod_end(); run_from = -1;
  /* FUN_1800414f0 L45253 (L42811, CycleLevels): per result its sheet level named after the result (+0x730 = the output name
     sheet+0x10c, L6320), its parts level 10168 "Parts on " + the name; set_levname (the part's level table, mcam_load.h) */
  if (P.cyclelevels) for (int i = 0; i < nR; i++) {
    char nm[256], pn[256]; snprintf(nm, sizeof nm, "%s - Result %d", S[R[i].sheet].name, R[i].copy + 1);   /* strcpy_s 0x100 */
    mcam_set_levname(&F, R[i].lv_sheet, nm);
    snprintf(pn, sizeof pn, "Parts on %s", nm); mcam_set_levname(&F, R[i].lv_parts, pn);
  }
  scene_reupload(); overlay_dirty = 1;
  ns_close();
}
/* the Results dialog's end: OK commits; Cancel / Redo: UndoTransTerminate, the results freed; Redo reopens the sheet with
   the session kept (L42648-42720) */
static void ns_results_end(int how) {        /* 1 OK, 2 Cancel, 3 Redo */
  ns_del_end();                               /* (the dialog is suspended while Delete's loop runs: never here) */
  ns_res_open = 0;
  if (how == 1) { ns_commit(); return; }
  undo_terminate(); results_free(); scene_reupload(); overlay_dirty = 1;
  if (how == 3) { ns_open = 1; return; }
  ns_close();
}

/* Copy (40032) / Copy Blank (40033) of the Results dialog, FUN_18007e290 L84825: a result made without parts (+0x880) copies
   blank; a modified one (+0x881: instances deleted / dragged - neither is ported, so never here) is refused with 13325-13327
   for Copy. ns_output again on the stored CNST_Result (FUN_180086cf0 -> FUN_180007b60(result, .., with parts), its copy count
   +0x120 forced to 1): a new output of the sheet (the next "Result n", AutoOrigins position, CycleColors / CycleLevels go on),
   inside the run's undo transaction; a blank one has no parts and zero nested size / area / utilisation (L5881, L6346-6352).
   The new result moves to just after the last result of the same sheet id (+0x718) following the selected one and becomes
   the selection (L84871-84942) */
static void ns_output(NsResult *r);
static void ns_results_copy(int with_parts) {
  if (res_sel < 0 || res_sel >= nR) return;
  if (!R[res_sel].parts) with_parts = 0;                                    /* L84853-84855 */
  if (R[res_sel].modified && with_parts) {                                  /* L84857-84858: 13325-13327 (FUN_180038ea0) */
    ns_msg("Warning - can not make copy of modified result.\nYou have added/removed parts from this result.\nResult can not be copied.");
    return;
  }
  NsResult n = R[res_sel];
  n.pq = NULL; n.modified = 0; n.rid = nR;                                  /* a new CResult (ctor FUN_1800853e0; +0x881 = 0, L5874), appended last (+0x8a0) */
  n.inst = malloc(sizeof(NestOutInst) * (size_t)(n.ninst ? n.ninst : 1)); memcpy(n.inst, R[res_sel].inst, sizeof(NestOutInst) * (size_t)n.ninst);
  n.copies = 1; n.parts = with_parts;
  if (!with_parts) { n.ninst = 0; n.util = 0; n.cutArea = 0; n.nestLen = n.nestWid = 0; }   /* +0xf5..+0xf9 = 0 */
  ns_output(&n);
  R = realloc(R, sizeof(NsResult) * (size_t)(nR + 1));
  int at = res_sel;                                                          /* the last sibling after the selection */
  for (int j = res_sel + 1; j < nR; j++) if (R[j].sheet == R[res_sel].sheet) at = j;
  at++;
  memmove(&R[at + 1], &R[at], sizeof(NsResult) * (size_t)(nR - at)); R[at] = n; nR++;
  res_sel = at;
  scene_reupload(); overlay_dirty = 1;
}

/* ------------------------------------------------------------------------------------------------ Results Delete (40031) */
/* FUN_18007e660 L84975 (formats/nesting_addin.md §8 "Results Delete"): on the selected result FUN_180089ed0 (isolate it on a free
   level), then the pick loop FUN_180087fb0(.., param_4 0), then FUN_180089860 (everything back), FUN_180081d00 (the dialog
   redrawn), the preview timer again. The CResultUtils state (0x180137fb0): */
int ns_deleting;                          /* the loop runs (get_event): the dialog is suspended (AfxSuspendWindow, FUN_180089890) */
static int ns_free_lv;                    /* +0xb0: 0 from the ctor FUN_1800876f0 (L302: a static object, never reset) */
static int del_res;                       /* the result the loop deletes from (FUN_180087fb0 param_2) */
typedef struct { int e, level, color; } NsIso;     /* the list +0x80 with the levels +0x58 / colours +0x30 it had */
static NsIso *iso; static int niso;
static uint8_t *iso_vis; static int niso_vis;      /* save_vis_level */
static void ent_color_set(int e, int co) {   /* write_ent with a new colour (ent +6) */
  Ent *en = &F.ents[e]; en->color = (uint8_t)co;
  for (int q = en->first_poly; q < en->first_poly + en->npoly && q < F.npolys; q++) F.polys[q].color = (uint8_t)co;
}
static void ent_level_set(int e, int lv) {   /* write_ent with a new level (ent +8) */
  Ent *en = &F.ents[e]; en->level = lv;
  for (int q = en->first_poly; q < en->first_poly + en->npoly && q < F.npolys; q++) F.polys[q].level = lv;
}
/* FUN_1800c90d0 L138614 (param 10000): the database-levels list rebuilt (FUN_1800c9df0), the first level from 10000 on in neither
   list (FUN_1800c9a00), made visible in the level table when it is not there (LevelData::setLevel(lv, 1, 1)), appended to the
   allocated list */
static int free_level(int lv) {
  db_levels_collect();
  while (level_used(lv)) lv++;
  if (!mcam_level(&F, lv)) { mcam_level_set(&F, lv, 1); F.lvl_dirty = 1; }
  alloc_levels = realloc(alloc_levels, sizeof(int) * (size_t)(n_alloc_levels + 1)); alloc_levels[n_alloc_levels++] = lv;
  return lv;
}
static void iso_add(int e) {
  if (e < 0 || e >= F.nents || !F.ents[e].type) return;                  /* FUN_180087820: get_ent_from_eptr == 0 */
  iso = realloc(iso, sizeof(NsIso) * (size_t)(niso + 1)); iso[niso].e = e; iso[niso].level = 0; iso[niso].color = F.ents[e].color; niso++;
}
static int ppi_of_ent(int e) {             /* FUN_1800b4220 L123460: the first PPI whose entity list holds it */
  for (int k = 0; k < nPPI; k++) if (e >= PPI[k].from && e < PPI[k].to) return k;
  return -1;
}
static int ppis_of(int rid, int part_id, int ord, int *out) {   /* FUN_1800891f0 L91505 (< 0 = any) */
  int n = 0;
  for (int k = 0; k < nPPI; k++)
    if ((PPI[k].rid == rid || rid < 0) && (PPI[k].part_id == part_id || part_id < 0) && (PPI[k].ord == ord || ord < 0)) { if (out) out[n] = k; n++; }
  return n;
}
static int res_has_ppi(int i) { return i >= 0 && i < nR && ppis_of(R[i].rid, -1, -1, NULL) > 0; }   /* FUN_180081d00 L87117-87125 */
/* gview_proc(GetActiveViewNo(Planes, 0)) (FUN_180089890 L91870-91873 and FUN_1800899a0 L91945-91948): the graphics view = the
   active Cplane's view (PlaneType 0 = Cplane, formats/bbox_toolplane.md). Both the isolation and the restore set it; neither puts
   back the view saved in +0xa8 (get_gr_view_n, L91869: stored, never read here). The Cplane: cui_planes.c plane_active[0] */
static void gview_cplane(void) { view_set_matrix(cam_plane_matrix(plane_active[0] > 0 ? plane_active[0] : 1)); }
static void ns_del_restore(void);
static void ns_delete_begin(void) {
  if (res_sel < 0 || res_sel >= nR) return;
  del_res = res_sel;
  /* the list +0x80 as FUN_18007d8d0 L84415 builds it for the shown result: the result's sheet entities (+0x678, L84476-84484), then
     for each part id in rising order (FUN_1800c8980) the entities of every PPI of the result - all of them, +0x668 being -1 (ctor
     FUN_1800853e0; only the preview's part click sets it, L84802); the trim lines: none (NOT PORTED).
     Ours, approved NS-2 (formats/user_decisions.md): the PPIs are taken by the selected result's id (+0x8a0 = PPI +0xb4, the key the
     pick loop FUN_1800891f0 deletes by) where Mastercam compares PPI +0xb4 with the dialog's selection index +0xb198 (L84498-84530):
     after a Copy inserted a result before the end the index is another result's id, so Mastercam isolates (and lets the user pick)
     another result's parts while the selected result's counts are lowered. */
  niso = 0;
  NsResult *r = &R[res_sel];
  for (int e = r->ent_from; e < r->sheet_to; e++) iso_add(e);
  int nsheet = niso;
  int last = 0;
  for (;;) {
    int m = 1000000; for (int k = 0; k < nPPI; k++) if (PPI[k].rid == r->rid && PPI[k].part_id > last && PPI[k].part_id < m) m = PPI[k].part_id;
    if (m == 1000000) break;
    last = m;
    for (int k = 0; k < nPPI; k++) if (PPI[k].rid == r->rid && PPI[k].part_id == m) for (int e = PPI[k].from; e < PPI[k].to; e++) iso_add(e);
  }
  int ntrim0 = niso;                                                        /* then the active trim lines (FUN_18007d8d0 L84455-84470) */
  if (r->hact && r->hent >= 0) iso_add(r->hent);
  if (r->vact && r->vent >= 0) iso_add(r->vent);
  /* FUN_180089890 L91845 (param_3 0): the free level once (FUN_1800c90d0(10000)); none -> FUN_180089ed0 fails and the loop is skipped */
  if (ns_free_lv < 1) ns_free_lv = free_level(10000);
  if (ns_free_lv < 1) return;
  ns_deleting = 1;                                                          /* AfxSuspendWindow(dlg) */
  free(iso_vis); niso_vis = F.nlvls; iso_vis = malloc((size_t)(niso_vis ? niso_vis : 1));   /* save_vis_level */
  for (int i = 0; i < F.nlvls; i++) iso_vis[i] = F.lvls[i].vis;
  geo_level = ns_free_lv;                                                   /* main_level = +0xb0 */
  gview_cplane();                                                           /* gview_proc(GetActiveViewNo(Planes, 0)) */
  for (int i = 0; i < F.nlvls; i++) F.lvls[i].vis = 0;                      /* LevelData::setAll(0) */
  mcam_level_set(&F, ns_free_lv, 1);                                        /* setLevel(+0xb0, 1, 1): only the free level is drawn and
                                                                               picked (LevelData::isVisible, mcam_load.h mcam_level_visible) */
  /* FUN_180089bb0 L91995 (param_6 0): the marks 2 / 4 / 8 off everywhere (set_clr_sel_bits(0, 0xe, ..)), then every listed
     entity onto the free level: the sheet's (+0x670) and the trim lines in the Group colour with mark 2, the rest in the Result
     colour with mark 8; the level / colour each had kept (+0x58 / +0x30); fit (fit_bits 10) */
  xf_clear_colors();
  for (int i = 0; i < niso; i++) {
    int e = iso[i].e, grp = i < nsheet || i >= ntrim0;   /* the sheet's and the trim lines: the group colour, mark 2 */
    iso[i].level = F.ents[e].level;
    xf_mark_ent(e, grp ? 2 : 8);
    ent_color_set(e, grp ? cfg_group_color : cfg_result_color);
    ent_level_set(e, ns_free_lv);
  }
  scene_reupload(); overlay_dirty = 1;
  view_fit(0);                                                              /* the fit walks the display list: the free level */
  /* FUN_180087fb0 L90774: no PPI at all (DAT_180140d60 < 1) -> nothing; else (geometry mode) the prompt 0x340a = 13322 and
     get_event(mask 0x122) until it returns 0 */
  if (nPPI < 1) { ns_del_restore(); return; }
  snprintf(cam_msg, sizeof cam_msg, "Select part profile to delete...");
  /* get_event(prompt, ev, 0x122, 0x1f1fff, 8, &flag) (L90889): the fifth argument 8 = the sel bit the picks and the window need -
     only the Result-coloured instance entities (mark 8, FUN_180089bb0), not the sheet's (mark 2) */
  pick_req_bits = 8;
}
/* the events of FUN_180087fb0 (L90924-91100): event 2 = the picked entity's PPI (FUN_1800b4220); event 0x20 = the window's entity list
   (ev +0xe8, already filtered to sel bit 8 by get_event), each entity's PPI once (VArray::Exists); the point event 4 is not in the
   mask 0x122. Then (L90990-91090): the PPI indices bubble-sorted, their (+0xa4 part id, +0xac ordinal) pairs taken once each in that
   order; per pair every PPI of this result (+0x8a0) with that pair (FUN_1800891f0) deleted - result +0x881 = 1 / +0x660 = 0,
   FUN_1800b0500 "DeletePPI" (its entities deleted FUN_180042f50 -> delete_ent inside the run's undo transaction; FUN_1800b0850 drops
   its XML instance record - the reports' data, NOT PORTED; FUN_1800b8010 / FUN_1800b2180 / FUN_1800354d0: Bump Nesting / toolpath /
   label structures, none here) - then the result's part copy count lowered once (FUN_180087450). Every event ends with the recount
   FUN_180086d20 L89932: the PPIs of this result left (its extent +0x698.. and the trim lines FUN_180085c80 / FUN_18008c160 feed only
   the trims, NOT PORTED); none left ends the loop */
/* FUN_180086d20 L89932 after a deletion: the extent = EntityMinMax over every entity of the result's PPIs left (FUN_1800891f0), none ->
   the sheet box (FUN_18008c160, min / max of its points); FUN_180085c80(result, +0x780, +0x788, -1.0: the utilisations kept, the sheet
   box); a line made: the stored one rewritten in place (write_ent; it keeps its level / colour / blank state), else stored new with
   the sheet colour (+0x724) and blanked unless active (L90175-90215). A line no longer valid stays until OK deletes it */
static void trims_recount(NsResult *r) {
  for (int k = 0; k < 3; k++) { r->lo[k] = 1e38; r->hi[k] = -1e38; }
  int any = 0;
  for (int k = 0; k < nPPI; k++) if (PPI[k].rid == r->rid) for (int e = PPI[k].from; e < PPI[k].to; e++) if (F.ents[e].type) { ent_minmax(e, r->lo, r->hi); any = 1; }
  double b[6]; int ok = sheet_box(r, b);
  if (!any && ok) for (int k = 0; k < 3; k++) { r->lo[k] = fmin(b[k], b[3 + k]); r->hi[k] = fmax(b[k], b[3 + k]); }
  double L, W; res_LW(r, &L, &W); V3 hl[2], vl[2]; int mh, mv;
  calc_trims(r, L, W, -1.0, ok, b, &mh, hl, &mv, vl);
  int *pe[2] = { &r->hent, &r->vent }, mk[2] = { mh, mv }, act[2] = { r->hact, r->vact }; V3 *ln[2] = { hl, vl };
  int co = P.cyclecolors ? S[r->sheet].color : geo_color, lv = P.cyclelevels ? r->lv_sheet : geo_level;
  for (int i = 0; i < 2; i++) if (mk[i]) {
    int x = *pe[i];
    if (x >= 0 && F.ents[x].type == E_LINE) {
      Ent *en = &F.ents[x]; en->a = ln[i][0]; en->b = ln[i][1];
      for (int q = en->first_poly; q < en->first_poly + en->npoly && q < F.npolys; q++) if (F.polys[q].count == 2) { F.pts[F.polys[q].first] = ln[i][0]; F.pts[F.polys[q].first + 1] = ln[i][1]; }
    } else *pe[i] = trim_line(ln[i], co, lv, act[i]);
  }
  scene_reupload(); overlay_dirty = 1;
}
static void ns_del_ppis(int *ls, int n) {
  NsResult *r = &R[del_res];
  for (int i = 0; i + 1 < n; i++) if (ls[i + 1] < ls[i]) { int t = ls[i]; ls[i] = ls[i + 1]; ls[i + 1] = t; i = -1; }   /* L90994-91010 */
  int *pid = malloc(sizeof(int) * (size_t)(n ? n : 1)), *pord = malloc(sizeof(int) * (size_t)(n ? n : 1)), np = 0;
  for (int i = 0; i < n; i++) {                                             /* the pairs once each (L91013-91050) */
    int a = PPI[ls[i]].part_id, b = PPI[ls[i]].ord, dup = 0;
    for (int j = 0; j < np && !dup; j++) dup = pid[j] == a && pord[j] == b;
    if (!dup) { pid[np] = a; pord[np] = b; np++; }
  }
  for (int i = 0; i < np; i++) {
    int *dl = malloc(sizeof(int) * (size_t)(nPPI ? nPPI : 1)), m = ppis_of(r->rid, pid[i], pord[i], dl);
    for (int j = 0; j < m; j++) {
      int q = dl[j] - j;                                                    /* the list shifts as PPIs go (L91080) */
      r->modified = 1;                                                      /* +0x881 = 1, +0x660 = 0 */
      /* delete_ent inside the run's transaction: an entity the run made goes with it (ours: switched off, not recorded as an
         original to restore - the transaction's undo removes the run's entities, so it must not bring this one back) */
      for (int x = PPI[q].from; x < PPI[q].to; x++) { if (mod_from >= 0 && x >= mod_from) { if (F.ents[x].type) ent_off(x); } else mod_del(x); }
      memmove(&PPI[q], &PPI[q + 1], sizeof(NsPPI) * (size_t)(nPPI - q - 1)); nPPI--;   /* RemoveAt (L120486) */
    }
    free(dl);
    for (int q = 0; q < nP; q++) if (Pt[q].id == pid[i]) { if (r->pq && r->pq[q] > 0) r->pq[q]--; break; }   /* FUN_180087450 (DAT_1801352b8 == 0) */
  }
  free(pid); free(pord);
  if (n) { scene_reupload(); overlay_dirty = 1; }                         /* repaint_graphics */
  trims_recount(r);
  if (ppis_of(r->rid, -1, -1, NULL) < 1) ns_del_end();                      /* FUN_180086d20 < 1 -> LAB_180088819 */
}
void ns_del_pick(int e) {                    /* event 2 */
  if (!ns_deleting) return;
  int k = e >= 0 ? ppi_of_ent(e) : -1;
  ns_del_ppis(&k, k >= 0);
}
/* event 0x20: get_event's window (mccore FUN_1804e6520, decomp/getevent/mccore_getevent_win.c 41-56, called from the button-up
   handler FUN_1804e3b90 when the mask has 0x20 / 0x10 and a rubber-band window was dragged, mccore_getevent_msgs.c 614-659):
   mark_window_sm over the two corners with GetWindowSelectMode() (cui_sel.c get_window_select_mode: 1, Inside, outside chaining)
   and the current selection mask, MCEvent::SetupWindowEvent + FillWithBITS(0x400): the marked entities; get_event then
   FilterEptrArray(ev, 0x1f1fff, 8) (decomp/mccore_getevent.c 189-191). Here: window_select (its display-list rule) with
   pick_req_bits = 8 */
void ns_del_window(float x0, float y0, float x1, float y1) {
  if (!ns_deleting) return;
  char *w = window_select(x0, y0, x1, y1, get_window_select_mode());
  int *ls = malloc(sizeof(int) * (size_t)(nPPI ? nPPI : 1)), n = 0;
  for (int e = 0; e < F.nents && w; e++) if (w[e]) {
    int k = ppi_of_ent(e), have = 0;
    for (int j = 0; j < n && !have; j++) have = ls[j] == k;
    if (k >= 0 && !have) ls[n++] = k;
  }
  free(w);
  ns_del_ppis(ls, n);
  free(ls);
}
/* FUN_180089860 L91827: FUN_180089a30 (the marks 2 / 4 / 8 off everywhere; each listed entity still there back on its level in its
   colour) and FUN_1800899a0 (main level = +0xac, the active plane's view, restore_vis_level, rebuild_graphics, AfxResumeWindow) */
static void ns_del_restore(void) {
  xf_clear_colors();
  for (int i = 0; i < niso; i++) {
    int e = iso[i].e; if (!F.ents[e].type || !iso[i].level) continue;
    ent_level_set(e, iso[i].level); ent_color_set(e, iso[i].color);
  }
  niso = 0;
  geo_level = res_main_level;
  gview_cplane();                                                           /* gview_proc(GetActiveViewNo(Planes, 0)) */
  for (int i = 0; i < F.nlvls && i < niso_vis; i++) F.lvls[i].vis = iso_vis[i];   /* restore_vis_level */
  cam_msg[0] = 0;                                                           /* ClosePrompt */
  ns_deleting = 0; pick_req_bits = 0;
  scene_reupload(); overlay_dirty = 1;
}
void ns_del_end(void) { if (ns_deleting) ns_del_restore(); }

/* ------------------------------------------------------------------------------------------------ Details (8016) */
/* FUN_180080a20 L86299 -> FUN_180084380 L88353: FUN_1800ccf40 (the images S%d / P%d / R%d.bmp - drawn through mastercam.dll's
   FEDC_Bitmap_Setup draw-to-bitmap: NOT DECODED, not made), FUN_1800deb10 L151600 (Nesting.xml in the XML folder), then
   SetNestingReportsInRegistry (mccore @1803c9c30: <reports key>\NESTING SHT-Source = report_nesting) and RunActiveReportsViewer
   (mccore @180641790: activereports_viewer.exe in <shared>\COMMON\REPORTS\ on the XML; missing -> 0x15e5 and the text report in
   notepad, FUN_180084440). The viewer (activereports_viewer.exe, decomp: ~/mastercam-re/dotnet/activereports_viewer) lays out the
   SHT-Source template: Add_Prefix_Suffix(lower(name), reports folder, ".rpx") (MainForm.cs 1612-1640), FileNotFound ->
   "NAME\n(file not found)" (MainForm.cs 503-507). report_nesting = config default "DEFAULT NESTING REPORT" in the NST folder
   (mccore toolmgr/tm2/a.c 4166: FUN_1801c6000(report_nesting, "NST", ..); its config key NOT DECODED, the default here).
   Ours: the viewer is this program's report window (core_ui tlr, viewer/rpx.c); Nesting.xml is not written to the XML folder (the
   tree is built in memory with the tags the template binds - the other tags NOT PORTED); the images are none (NOT DECODED). */
static RNode *xml_s(RNode *p, const char *tag, const char *v, int upper) {   /* FUN_1800dae10: a CString, empty -> 13424 "Unknown" */
  char b[512]; snprintf(b, sizeof b, "%s", v && v[0] ? v : "Unknown");
  if (upper) for (char *q = b; *q; q++) *q = (char)toupper((unsigned char)*q);   /* MakeUpper (param_5) */
  return rnode_new(p, tag, b);
}
static void xml_d(RNode *p, const char *tag, double v) { char b[64]; make_rstr(v, b, sizeof b, 5); rnode_new(p, tag, b); }   /* FUN_1800dc490 */
static void xml_dd(RNode *p, const char *tag, double a, double c) {     /* FUN_1800daba0: "%s x %s" of make_rstr(.., 5) */
  char x[64], y[64], b[160]; make_rstr(a, x, sizeof x, 5); make_rstr(c, y, sizeof y, 5); snprintf(b, sizeof b, "%s x %s", x, y); rnode_new(p, tag, b);
}
static void xml_i(RNode *p, const char *tag, int v) { char b[32]; snprintf(b, sizeof b, "%d", v); rnode_new(p, tag, b); }   /* FUN_1800db7e0 */
/* the SHEET record CResult +0x710 (FUN_1800da5e0 L149420, filled by ns_output L6318-6370) */
static void xml_sheet(RNode *sh, int i) {
  NsResult *r = &R[i]; NsSheet *rs = &S[r->sheet];
  double L = rs->len, W = rs->wid, nl = 0, nw = 0, area, cut = 0;      /* +0x780.. as the Results dialog shows them (FUN_180084d30) */
  if (collar_c < 0) { L += 2 * fabs(collar_c); W += 2 * fabs(collar_c); }
  area = L * W;
  if (r->ts) { L = r->len; W = r->wid; area = r->area; cut = r->cutArea; nl = r->nestLen; nw = r->nestWid; }
  if (!r->parts) { cut = 0; nl = nw = 0; }                                 /* +0xf5..+0xf9 = 0 (L6346-6352) */
  xml_i(sh, "ID", S[r->sheet].id);                                         /* +0x718 = sheet +8 */
  xml_i(sh, "INSTANCE", r->copy + 1);                                      /* +0x71c = sheet +0x344 at its output */
  xml_i(sh, "NUMBER", i + 1);                                              /* param_4 + 1 */
  char nm[300]; snprintf(nm, sizeof nm, "%s - Result %d", rs->name, r->copy + 1);   /* +0x730 = the output name sheet +0x10c (10161) */
  xml_s(sh, "NAME", nm, 1);
  xml_i(sh, "COPIES", r->parts ? r->copies : 1);                           /* +0x720 */
  char g[2] = { P.grain, 0 }; xml_s(sh, "GRAIN", g, 0);                    /* +0x748 = P+0xd8 */
  xml_d(sh, "SPACING", P.s2s);                                             /* +0x750 = P+0x38 SHEETTOSHEETDIST */
  { char x[64], y[64], z[64], b[200]; make_rstr(r->ox, x, sizeof x, 5); make_rstr(r->oy, y, sizeof y, 5); make_rstr(0, z, sizeof z, 5);
    snprintf(b, sizeof b, "%s, %s, %s", x, y, z); rnode_new(sh, "ORIGIN", b); }   /* FUN_1800dacb0; z: the port's origins are 2-D */
  xml_dd(sh, "DIMENSION", L, W);                                           /* +0x780 / +0x788 */
  double c2 = collar_c < mc_systol.tol ? fabs(collar_c) * 2.0 : 0;        /* +0x790 / +0x798: less 2|c| when c < tol (L6334-6341) */
  xml_dd(sh, "SIZE", L - c2, W - c2);
  xml_d(sh, "AREA", area);                                                 /* +0x7a0 */
  xml_d(sh, "CUT-AREA", cut);                                              /* +0x7a8 */
  xml_dd(sh, "NESTED-DIMENSION", nl, nw);                                  /* +0x7b0 / +0x7b8 */
  xml_d(sh, "OFFSET", 0);                                                  /* +0x7c0 = 0 */
  xml_d(sh, "UTILIZATION", r->parts ? r->util : 0);                        /* +0x7c8 */
  xml_d(sh, "HORIZ-CUTOFF", r->hcut); xml_d(sh, "VERT-CUTOFF", r->vcut);   /* +0x7d0 / +0x7d8 (FUN_180085c80, geometry mode: the distances) */
  xml_s(sh, "SHEET-MATERIAL", rs->material, 0);                            /* +0x740 = sheet +0x210 */
  /* PARTS (FUN_1800dc110 L150204): per part id in rising order (FUN_1800c8980) the PPIs of this result with element +0x34 < 2; a
     part with one or more -> a PART (FUN_1800daf30 L149649). Ours, approved NS-2: the PPIs are taken by the result's id (+0x8a0 =
     PPI +0xb4) where FUN_1800dc110 compares +0xb4 with the SHEET's index in the results (param_3 = uVar6, L150418-150424) - after a
     Copy inserted a result before the end the index names another result's parts */
  RNode *ps = rnode_new(sh, "PARTS", NULL);
  int last = 0;
  for (;;) {
    int m = 1000000; for (int q = 0; q < nP; q++) if (Pt[q].id > last && Pt[q].id < m) m = Pt[q].id;
    if (m == 1000000) break;
    last = m;
    int n = 0; for (int k = 0; k < nPPI; k++) if (PPI[k].rid == r->rid && PPI[k].part_id == m && PPI[k].elem < 2) n++;
    if (n < 1) continue;
    const NsPart *pp = NULL; for (int q = 0; q < nP; q++) if (Pt[q].id == m) pp = &Pt[q];
    if (!pp) continue;
    RNode *pt = rnode_new(ps, "PART", NULL);
    rnode_new(pt, "PART-NAME", pp->name);                                  /* part +8 (FUN_1800dc800: as it is) */
    char lab[256]; snprintf(lab, sizeof lab, "%s", pp->label);              /* part +0x108; NameAsLabel (P+0xa): get_just_name(name) */
    if (P.nameaslabel) { const char *b = strrchr(pp->name, '\\'); snprintf(lab, sizeof lab, "%s", b ? b + 1 : pp->name); char *d = strrchr(lab, '.'); if (d) *d = 0; }
    rnode_new(pt, "PART-LABEL", lab);
    xml_i(pt, "PART-ID", pp->id);                                          /* part +0x230 */
    xml_i(pt, "PART-NESTED", n);                                           /* param_7 */
    xml_s(pt, "PART-MATERIAL", "", 0);                                     /* param_10 = 13424 "Unknown" (L150255) */
  }
}
static void ns_details(void) {
  /* FUN_1800deb10: per SHEETRESULT the results of one sheet (+0x1d8 = the CSheet copy's id; first unwritten result's, +0x891 marks),
     QUANTITY = their count; SHEETS / SHEET per result (FUN_1800dc500 L150366) */
  char rel[700], path[800];
  snprintf(rel, sizeof rel, "%sNST\\%s.rpx", cfg_rpt_dir, "default nesting report");
  if (!cfg_rpt_dir[0] || !ci_path(rel, path, sizeof path)) {
    char up[700]; snprintf(up, sizeof up, "%s", rel); for (char *q = up; *q; q++) *q = (char)toupper((unsigned char)*q);
    cui_message("Warning", "%s\n(file not found)", up);                   /* the viewer's FileNotFound */
    return;
  }
  int *done = calloc((size_t)(nR ? nR : 1), sizeof(int));
  RNode *rec[256]; int nrec = 0;
  for (int i = 0; i < nR && nrec < 256; i++) {
    if (done[i]) continue;
    int sid = S[R[i].sheet].id;
    RNode *sr = rnode_new(NULL, "SHEETS", NULL);
    for (int j = 0; j < nR; j++) if (!done[j] && S[R[j].sheet].id == sid) { done[j] = 1; xml_sheet(rnode_new(sr, "SHEET", NULL), j); }
    rec[nrec++] = sr;
  }
  free(done);
  rpx_layout_free(&tlr);
  if (!rpx_layout(path, rec, nrec, &tlr)) cui_message("Warning", "%s", tlr.err);
  else { snprintf(tlr_title, sizeof tlr_title, "%s", "DEFAULT NESTING REPORT"); show_tlr = 1; tlr_new = 1; }
  for (int i = 0; i < nrec; i++) rnode_free(rec[i]);
}

/* chain_it for Add chained parts (10030) / sheets (10032): the sheet is hidden while the Chaining dialog runs */
static void ns_begin_chaining(int kind) {
  ns_chaining = kind; ns_open = 0;
  geo_chaining = 1;                            /* the shared chaining (cui_chain.c chain_begin_geo); cui_geo.c hands the OK / Esc back */
  chain_begin_geo(kind == 1 ? "Add Part: select chain" : "Add Sheet: select chain");
}
void ns_chains_done(void) {                    /* geo_chains_done while ns_chaining */
  int kind = ns_chaining;
  chain_keep_last();
  Chain ch[256]; int n = 0;
  for (int i = 0; i < ncchains && n < 256; i++) if (!cchains[i].sld && cchains[i].n > 0) ch[n++] = cchains[i];
  if (n) { if (kind == 1) parts_from_chains(ch, n); else sheets_from_chains(ch, n); }
  cam_clear_selection();
  geo_chaining = 0; ns_chaining = 0; ns_open = 1; cam_msg[0] = 0; overlay_dirty = 1;
}
void ns_chain_end(void) { ns_chaining = 0; ns_open = 1; cam_msg[0] = 0; }   /* chain_it returned without chains */

/* ------------------------------------------------------------------------------------------------ the windows' model */
/* the property sheet itself (CNestPropSht FUN_180065d80): ours [inferred] - the standard PropertySheet frame (7 DLU margins,
   tabs above the 402 x 270 pages, 50 x 14 OK / Cancel / Help at the right; Apply hidden FUN_1800661c0 L70067); the bitmap
   buttons (FUN_180066310 L70120-70246): squares of the OK button's height at OK's top, from x = 6 px stepping height + 3 px */
static const NsCtl NS_SHT[] = {
  { 0u, NSR_OWNER, 7, 20, 402, 270, "" },
  { 40014u, NSR_BMPBTN, 4, 297, 14, 14, "|&New" }, { 40016u, NSR_BMPBTN, 20, 297, 14, 14, "|&Open" },
  { 40018u, NSR_BMPBTN, 36, 297, 14, 14, "|&Save" }, { 40020u, NSR_BMPBTN, 52, 297, 14, 14, "|Save &As" },
  { 40022u, NSR_BMPBTN, 68, 297, 14, 14, "|&Restore" }, { 40023u, NSR_BMPBTN, 84, 297, 14, 14, "|&Config" },
  { 1u, NSR_BMPBTN, 251, 297, 50, 14, "|OK" }, { 2u, NSR_BMPBTN, 305, 297, 50, 14, "|Cancel" }, { 9u, NSR_BMPBTN, 359, 297, 50, 14, "|&Help" },
};
const NsCtl *ns_table(int tpl, int *n, int *w, int *h) {
#define T(a, W_, H_) do { *n = (int)(sizeof a / sizeof a[0]); *w = W_; *h = H_; return a; } while (0)
  switch (tpl) {
  case NS_SHEET: T(NS_SHT, 416, 318);
  case 101: T(NS_101, 402, 270);
  case 105: T(NS_105, 402, 270);
  case 201: T(NS_201, 379, 350);
  case 205: T(NS_205, 636, 430);
  }
  *n = 0; *w = *h = 0; return NULL;
#undef T
}
static const NsCtl *ctl_of(int tpl, unsigned id) { int n, w, h; const NsCtl *t = ns_table(tpl, &n, &w, &h); for (int i = 0; i < n; i++) if (t[i].id == id) return &t[i]; return NULL; }
const char *ns_tip(int tpl, unsigned id) { const NsCtl *c = ctl_of(tpl, id); return c ? (c->text[0] == '|' ? c->text + 1 : c->text) : ""; }

static NsSheet *cur_sheet(void) { return sel_sheet >= 0 && sel_sheet < nS ? &S[sel_sheet] : NULL; }
static NsPart *cur_part(void) { return sel_part >= 0 && sel_part < nP ? &Pt[sel_part] : NULL; }
static int any_cluster4(void) { for (int i = 0; i < nP; i++) if (Pt[i].cluster == 4) return 1; return 0; }

int ns_shown(int tpl, unsigned id) {
  if (tpl == 101 && id == 3005) return 0;          /* the in-place rename edit (double-click: not ported) */
  if (tpl == 105 && id == 4005) return 0;
  if (tpl == 105 && id >= 1084 && id <= 1089) return 0;   /* the colour legend: toolpath mode only (L76118-76131) */
  return 1;
}
int ns_enabled(int tpl, unsigned id) {
  int ts = P.trueshape;
  if (tpl == NS_SHEET) return id != 9;            /* Help: not ported */
  if (tpl == 101) {                                /* FUN_1800999e0 L102998 */
    NsSheet *s = cur_sheet();
    if (id == 3001 || id == 3003 || id == 3019 || id == 2008) return 1;
    if (id == 3002) return s != NULL;              /* shown only with one selected (L103292-103302) */
    if (id == 3020 || id == 3021 || id == 7003 || id == 7004 || id == 3022) return 1;   /* always / not in Customize */
    if (id == 7001 || id == 7002) return P.autoorigins;   /* L103383-103398 */
    if (id == 7028 || id == 7029 || id == 3025 || id == 3026 || id == 7033) return ts;   /* L103375-103379 */
    if (!s) return 0;                              /* no selection: the sheet's controls off (L103111-103180) */
    if (id == 3006 || id == 3007 || id == 3009) return s->added_rect;   /* L103400-103432 */
    if (id == 3018) return nS == 1;                /* L103191-103192 */
    if (id == 3016 || id == 3017) return !(S[0].amar && nS == 1);   /* L103193-103205 */
    if (id == 1129 || id == 1130) return ts;       /* L103362-103374 */
    if (id == 3010 || id == 3011 || id == 3013 || id == 3012 || id == 3014) return !P.autoorigins || sel_sheet == 0;   /* L103206-103232 */
    if (id == 3015) return 0;                      /* Select (point pick FUN_180099420): NOT DECODED */
    if (id == 1117 || id == 1118) return 0;        /* Material: not ported (and Rectangular greys it, L103335-103360) */
    return 1;
  }
  if (tpl == 105) {                                /* FUN_180076c40 L80329 */
    NsPart *p = cur_part();
    if (id == 4001 || id == 4003 || id == 2038 || id == 4039 || id == 0xffffffffu) return 1;
    if (id == 4002) return p != NULL;
    if (id == 4023 || id == 4024 || id == 4034 || id == 7005 || id == 1013) return 1;   /* always (L81013-81043) */
    if (id == 7006) return 1;
    if (id == 4035 || id == 4037) return P.addlabels;   /* L81024-81038 */
    if (id == 4036) return P.addlabels && !P.labelauto;   /* L81043 */
    if (id == 7027) return ts;                     /* Rectangular greys it (L80528-80533) */
    if (!p) return 0;
    if (id == 4006 || id == 4007 || id == 4009) return ts ? part_is_rect(p) : p->added_rect;   /* L80677-80800 */
    if (id == 4014 || id == 4015) return p->grain == 'N';   /* L80648-80676 */
    if (id == 4010 || id == 4011 || id == 4016 || id == 4017) return 1;   /* (a part in a group greys them: no groups here) */
    if (id == 4012 || id == 4013) return !P.fillall;   /* L80715 */
    if (id == 4032 || id == 4033) return !P.nameaslabel && P.addlabels;   /* L81013-81021 */
    if (id == 4019) return ts && p->nct > 0 && p->cluster != 4 && !p->added_rect;   /* L80746-80920 */
    if (id == 1100) return ts;
    if (id == 4021 || id == 4022) return ts && p->cluster != 4;
    if (id == 1116 || id == 1115) return 0;        /* Material: not ported */
    return 1;
  }
  if (tpl == 201) {                                /* FUN_18000ceb0 L8767 (Pc = the dialog's copy) */
    if (id == 1 || id == 2) return 1;
    if (id == 9) return 0;                         /* Help: not ported */
    if (id == 7016) return 0;                      /* Restore last session (registry RestoreLast + nesting.nstx): not ported */
    if (id == 7019 || id == 7020 || id == 7028) return Pc.savescrap;
    if (id == 7008 || id == 7010) return Pc.cyclecolors;
    if (id == 7013 || id == 7014 || id == 7017) return Pc.cyclelevels;
    if (id == 2030 || id == 7025) return Pc.trueshape;
    if (id == 7026) return Pc.trueshape && Pc.fitpip;
    if (id == 7024) return Pc.rescbox > 2;
    if (id == 2031 || (id >= 9107 && id <= 9115) || id == 2035 || id == 2036 || id == 2039 || id == 1098 || id == 1099 || id == 1131) return 0;   /* toolpath only (L8821-8851) */
    return 1;
  }
  if (tpl == 205) {                                /* FUN_18007f060 / L87102-87127 */
    if (id == 1 || id == 2 || id == 8017 || id == 8002 || id == 40032 || id == 40033 || id == 8018) return 1;
    if (id == 9268 || id == 9275) {                /* FUN_180081d00: enabled while the result's line is valid (+0x7e0 / +0x808) */
      NsResult *r = res_sel >= 0 && res_sel < nR ? &R[res_sel] : NULL; return r && (id == 9268 ? r->hval : r->vval); }
    if (id == 40031) return res_has_ppi(res_sel);  /* Delete (+0x6128): the result has PPIs (FUN_180081d00 L87117-87124) */
    if (id >= 8001 && id <= 8016) return 1;        /* 8016 Details (FUN_180080a20) */
    return 0;                                      /* Sort / Customize / Drag / Help: not ported */
  }
  return 1;
}
int ns_checked(int tpl, unsigned id) {
  NsSheet *s = cur_sheet(); NsPart *p = cur_part();
  if (tpl == 205 && (id == 9268 || id == 9275)) {  /* the check boxes show +0x5a4 / +0x5a0 (FUN_180081d00) */
    NsResult *r = res_sel >= 0 && res_sel < nR ? &R[res_sel] : NULL; return r && (id == 9268 ? r->hact : r->vact); }
  if (tpl == 101) switch (id) {
    case 3018: return nS ? S[0].amar : Sdef.amar;
    case 7033: return P.trueshape ? P.guillotine : 0;   /* Rectangular shows unticked (L103447-103450) */
    case 3022: return P.autoorigins;
  }
  if (tpl == 105) switch (id) {
    case 4019: return p ? p->user_rect : 0;
    case 1100: return P.trueshape && p ? p->commontool : 0;
    case 4034: return P.addlabels;
    case 4035: return !P.labelauto; case 4037: return P.labelauto;
    case 7027: return P.trueshape ? P.autopairs : 0;
    case 1013: return P.fillall;
  }
  if (tpl == 201) switch (id) {
    case 2032: return Pc.loaddefsheet; case 2034: return Pc.dispresults; case 2033: return Pc.dispgroups;
    case 7021: return Pc.deletechains; case 7027: return Pc.nameaslabel; case 7016: return 0; case 2040: return Pc.compress;
    case 7007: return Pc.cyclecolors; case 7012: return Pc.cyclelevels; case 7017: return Pc.cycleunused; case 7200: return Pc.highres;
    case 7018: return Pc.savescrap; case 7028: return Pc.checkdup; case 7031: return !Pc.trueshape; case 7032: return Pc.trueshape;
    case 7025: return Pc.fitpip; case 7026: return Pc.prefhole; case 1004: return cfg_rc & 0x100;
  }
  (void)s;
  return 0;
}
static const char *grain_items[3] = { "Horizontal", "Vertical", "Ignore" };                   /* 10050 / 10051 / 10052 */
static const char *corner_items[4] = { "Lower Left", "Lower Right", "Upper Right", "Upper Left" };   /* 10171-10174 */
static const char *fill_items[3] = { "Horizontal", "Vertical", "Calculate" };                 /* 10050 / 10051 / 10053 */
static const char *mirror_items[2] = { "Original Only (do not mirror)", "Original or Mirror" }; /* 10501 / 10502 */
static const char *acc_items[4] = { "Fine", "Medium", "Coarse", "User-Defined" };             /* 10191-10194 */
static char txtbuf[8][512]; static int txtk;
static char *tb(void) { txtk = (txtk + 1) & 7; return txtbuf[txtk]; }
static const char *num(double v) { char *b = tb(); ns_fmt(v, b, 512); return b; }   /* ours: CSkRCmb's display format NOT DECODED */
static const char *inum(int v) { char *b = tb(); snprintf(b, 512, "%d", v); return b; }
static double step_shown(const NsPart *p) {        /* L81067-81093 */
  if (P.trueshape) return p->grain != 'N' ? 90 : p->step;
  if (p->grain != 'N') return 90;
  double v = p->step, best = fabs(v), r; int k = 0; static const double ang[4] = { 0, 90, 180, 270 };
  for (int j = 1; j < 4; j++) if (fabs(v - ang[j]) < best) { best = fabs(v - ang[j]); k = j; }
  r = best <= fabs(v - 360.0) && k >= 1 ? ang[k] : 360.0;
  return r >= 360 ? 0 : r;
}
const char *ns_text(int tpl, unsigned id) {
  if (id == 0xffffffffu) {                         /* the window titles: 10010 / the templates' captions */
    if (tpl == NS_SHEET) return "Nesting 2017 V2013R2";
    return tpl == 101 ? "Sheets" : tpl == 105 ? "Parts" : tpl == 201 ? "Nesting Configuration" : tpl == 205 ? "Nesting Results" : "";
  }
  NsSheet *s = cur_sheet(); NsPart *p = cur_part();
  const NsSheet *sv = s ? s : &Sdef; const NsPart *pv = p ? p : &Pdpart;   /* no selection: the defaults shown */
  if (tpl == 101) switch (id) {
    case 3002: if (!s) return " "; { char *b = tb(); snprintf(b, 512, "%d of %d", sel_sheet + 1, nS); return b; }   /* 10035 */
    case 3007: return num(sv->len); case 3009: return num(sv->wid);
    case 3017: return inum(sv->qty);
    case 3012: return num(sv->ox); case 3014: return num(sv->oy);
    case 7002: return num(P.s2s); case 7004: return num(P.s2p);
    case 1118: return "";
    case 0xffffffffu: return "";
  }
  if (tpl == 105) switch (id) {
    case 4002: if (!p) return " "; { char *b = tb(); snprintf(b, 512, "%d of %d", sel_part + 1, nP); return b; }
    case 4007: return num(pv->len); case 4009: return num(pv->wid);
    case 4011: return inum(pv->minq); case 4013: return inum(pv->fillq);
    case 4015: return num(step_shown(pv));
    case 4033: return pv->label;
    case 1115: return "";
    case 7006: return num(P.p2p); case 4036: return num(P.labelh);
  }
  if (tpl == 201) switch (id) {
    case 7008: return inum(Pc.resultcolor); case 7013: return inum(Pc.resultlevel); case 7019: return Pc.scrapname;
    case 7024: if (Pc.rescbox > 2) return num(Pc.resuser);
      { NsParams sv_ = P; P = Pc; double r = resolution_auto(); P = sv_; return num(r); }   /* the computed FUN_180037e40() greyed (L8855-8861) */
  }
  if (tpl == 205) {                                /* FUN_180084d30 L88791 */
    NsResult *r = res_sel >= 0 && res_sel < nR ? &R[res_sel] : NULL; char *b = tb();
    if (!r) return id == 8015 || id == 8007 || id == 8009 || id == 8011 || id == 8013 ? "" : NULL;
    NsSheet *rs = &S[r->sheet];
    double L = rs->len, W = rs->wid;               /* RNInqNestedSheet's L / W = the engine sheet (+2|c| when c < 0) */
    if (collar_c < 0) { L += 2 * fabs(collar_c); W += 2 * fabs(collar_c); }
    if (r->hact || r->vact) {                      /* L88805-88830 (asm: the jumps to the shared tail 0x18008501c): +0x5a0 and +0x5a4 ->
                                                      +0x830..; +0x5a0 -> +0x810..; +0x5a4 -> +0x7e8..; Nested size / area as before */
      const double *t = r->vact ? (r->hact ? r->bt : r->vt) : r->ht;
      switch (id) {
      case 8007: snprintf(b, 512, "%.2lf x %.2lf", t[0], t[1]); return b;
      case 8011: snprintf(b, 512, "%.2lf", t[2]); return b;
      case 8015: snprintf(b, 512, "%.2lf%%", t[3]); return b;
      }
    }
    if (r->ts) switch (id) {                       /* TrueShape: the CNST_Result values (FUN_180007b60 L6330-6370) */
    case 8007: snprintf(b, 512, "%.2lf x %.2lf", r->len, r->wid); return b;           /* +0x780 x +0x788 = +0x140 / +0x148 */
    case 8009: snprintf(b, 512, "%.2lf x %.2lf", r->nestLen, r->nestWid); return b;   /* +0x7b0 x +0x7b8 = +0x168 / +0x170 */
    case 8011: snprintf(b, 512, "%.2lf", r->area); return b;                          /* +0x7a0 = +0x150 */
    case 8013: snprintf(b, 512, "%.2lf", r->cutArea); return b;                       /* +0x7a8 = +0x160 */
    case 8015: snprintf(b, 512, "%.2lf%%", r->util); return b;                        /* +0x7c8 */
    }
    switch (id) {
    case 8007: snprintf(b, 512, "%.2lf x %.2lf", L, W); return b;           /* +0x780 x +0x788 */
    case 8009: snprintf(b, 512, "%.2lf x %.2lf", 0.0, 0.0); return b;       /* +0x7b0 x +0x7b8: zero in rect mode (L4387-4395) */
    case 8011: snprintf(b, 512, "%.2lf", L * W); return b;                  /* +0x7a0 = +0x150 */
    case 8013: snprintf(b, 512, "%.2lf", 0.0); return b;                    /* +0x7a8 = +0x160 (zero) */
    case 8015: snprintf(b, 512, "%.2lf%%", r->util); return b;              /* +0x7c8 = DAT_180134638 */
    }
  }
  if (tpl == 205 || tpl == 101 || tpl == 105) { const NsCtl *c = ctl_of(tpl, id); if (c && c->role == NSR_VALUE) return c->text; }
  return NULL;
}
static const char **listbuf; static int listcap;
static const char **lb(int n) { if (n > listcap) { listcap = n + 16; listbuf = realloc(listbuf, sizeof(char *) * (size_t)listcap); } return listbuf; }
static char prio_txt[64][8];
int ns_items(int tpl, unsigned id, const char ***items) {
  if (tpl == 101 && id == 3003) { const char **v = lb(nS); for (int i = 0; i < nS; i++) v[i] = S[i].name; *items = v; return nS; }   /* the sheet name (L103166-103169) */
  if (tpl == 105 && id == 4003) { const char **v = lb(nP); for (int i = 0; i < nP; i++) v[i] = Pt[i].name; *items = v; return nP; }   /* the part name (L81243-81247) */
  if ((tpl == 101 && id == 3021) || (tpl == 105 && id == 4024)) { *items = grain_items; return 3; }
  if (tpl == 101 && id == 7029) { *items = corner_items; return 4; }
  if (tpl == 101 && id == 3026) { *items = fill_items; return 3; }
  if (tpl == 105 && id == 4022) { *items = mirror_items; return 2; }
  if (tpl == 201 && id == 7023) { *items = acc_items; return 4; }
  if ((tpl == 101 && id == 1130) || (tpl == 105 && id == 4017)) {          /* "1".."N" (FUN_1800394a0 / FUN_1800393f0) */
    int m;
    if (tpl == 101) { m = nS; for (int i = 0; i < nS; i++) if (S[i].prio > m) m = S[i].prio; }
    else { m = nP; for (int i = 0; i < nP; i++) if (Pt[i].prio > m) m = Pt[i].prio; }
    m += 1; if (m > 64) m = 64;
    const char **v = lb(m); for (int i = 0; i < m; i++) { snprintf(prio_txt[i], 8, "%d", i + 1); v[i] = prio_txt[i]; }
    *items = v; return m;
  }
  if (tpl == 205 && id == 8002) {                  /* "%s - Result %d" (10161, L5749-5752) */
    static char (*nm)[300]; static int cap; if (nR > cap) { cap = nR + 8; nm = realloc(nm, sizeof *nm * (size_t)cap); }
    const char **v = lb(nR);
    for (int i = 0; i < nR; i++) { snprintf(nm[i], 300, "%s - Result %d", S[R[i].sheet].name, R[i].copy + 1); v[i] = nm[i]; }
    *items = v; return nR;
  }
  *items = NULL; return 0;
}
int ns_cursel(int tpl, unsigned id) {
  const NsSheet *s = cur_sheet() ? cur_sheet() : &Sdef; const NsPart *p = cur_part() ? cur_part() : &Pdpart;
  if (tpl == 101) switch (id) {
    case 3003: return sel_sheet;
    case 3021: return P.grain == 'X' ? 0 : P.grain == 'Y' ? 1 : 2;
    case 7029: return P.trueshape ? P.corner - 1 : 0;   /* Rectangular shows Lower Left */
    case 3026: return P.filldir == 'X' ? 0 : P.filldir == 'Y' ? 1 : 2;
    case 1130: return s->prio - 1;
  }
  if (tpl == 105) switch (id) {
    case 4003: return sel_part;
    case 4024: return p->grain == 'X' ? 0 : p->grain == 'Y' ? 1 : 2;
    case 4022: return P.trueshape ? (p->mirror > 1 ? 1 : p->mirror) : 0;  /* clamped 0 / 1 (L80473); Rectangular item 0 */
    case 4017: return p->prio - 1;
  }
  if (tpl == 201 && id == 7023) return Pc.rescbox;
  if (tpl == 205 && id == 8002) return res_sel;
  return -1;
}
/* the edits (KILLFOCUS appliers); values out of range are refused (the field shows the old value again). The bounds are the
   decode's (tol .. 30000 sizes, 1 .. 10000 quantity, 0 .. 10000 Minimum / Filler Qty, -30000 .. 30000 origins, 0 .. 30000
   distances; the buddy bounds' exact meaning NOT DECODED) */
static int parse_d(const char *v, double *d) { char *e; double x = strtod(v, &e); if (e == v) return 0; while (*e == ' ') e++; if (*e) return 0; *d = x; return 1; }
static int parse_i(const char *v, int *d) { char *e; long x = strtol(v, &e, 10); if (e == v) return 0; while (*e == ' ') e++; if (*e) return 0; *d = (int)x; return 1; }
void ns_edit(int tpl, unsigned id, const char *v) {
  double d; int k; double tol = mc_systol.tol; NsSheet *s = cur_sheet(); NsPart *p = cur_part();
  if (tpl == 101) {
    if (id == 7002 && parse_d(v, &d) && d >= 0 && d <= 30000) P.s2s = d;     /* L100828 */
    if (id == 7004 && parse_d(v, &d) && d >= 0 && d <= 30000) P.s2p = d;     /* L100815 */
    if (!s) return;
    if ((id == 3007 || id == 3009) && parse_d(v, &d) && d >= tol && d <= 30000) { if (id == 3007) s->len = d; else s->wid = d; }   /* FUN_180096b60 / FUN_1800972c0 */
    if (id == 3017 && parse_i(v, &k) && k >= 1 && k <= 10000) s->qty = k;     /* FUN_180097200 */
    if (id == 3012 && parse_d(v, &d) && fabs(d) <= 30000) s->ox = d;
    if (id == 3014 && parse_d(v, &d) && fabs(d) <= 30000) s->oy = d;
  }
  if (tpl == 105) {
    if (id == 7006 && parse_d(v, &d) && d >= 0 && d <= 30000) P.p2p = d;     /* FUN_1800706e0 */
    if (id == 4036 && parse_d(v, &d) && d >= tol && d <= 30000) P.labelh = d;   /* FUN_180070c30 */
    if (!p) return;
    if ((id == 4007 || id == 4009) && parse_d(v, &d) && d >= tol && d <= 30000) { if (id == 4007) p->len = d; else p->wid = d; }   /* FUN_180070d10 / FUN_1800712a0 */
    if (id == 4011 && parse_i(v, &k) && k >= 0 && k <= (P.fillall ? 50000 : 10000)) p->minq = k;   /* FUN_1800710b0 (max: L76272 / L78294-78302) */
    if (id == 4013 && parse_i(v, &k) && k >= 0 && k <= 10000) p->fillq = k;   /* FUN_180070940 */
    if (id == 4015 && parse_d(v, &d) && d >= 0 && d <= 360) {                 /* FUN_180070770 L76298 */
      if (d > 360 - tol) d = 360; if (d < 1 - tol) d = 0;
      if (!P.trueshape) { double best = fabs(d); int kk = 0; static const double ang[4] = { 0, 90, 180, 270 };
        for (int j = 1; j < 4; j++) if (fabs(d - ang[j]) < best) { best = fabs(d - ang[j]); kk = j; }
        d = best <= fabs(d - 360.0) && kk >= 1 ? ang[kk] : 360.0; if (d >= 360) d = 0; }
      p->step = d;
    }
    if (id == 4033) {                                                         /* FUN_1800709f0: a duplicate -> 10104 (L76420-76440) */
      for (int i = 0; i < nP; i++) if (i != sel_part && !strcmp(Pt[i].label, v)) { ns_msg("Warning - duplicate label"); return; }
      snprintf(p->label, sizeof p->label, "%.254s", v);
    }
    /* the caps of the refresh (L80456-80475) */
    if (p->minq > 50000) p->minq = 50000; if (p->fillq > 50000) p->fillq = 50000; if (p->minq + p->fillq > 50000) p->fillq = 50000 - p->minq;
  }
  if (tpl == 201) {
    if (id == 7008 && parse_i(v, &k) && k >= 0 && k <= 255) Pc.resultcolor = k;   /* MCInt 0..255 (L8074-8075) */
    if (id == 7013 && parse_i(v, &k) && k >= 1 && k <= 10000) Pc.resultlevel = k;   /* 1..10000 (L8076-8077) */
    if (id == 7019) snprintf(Pc.scrapname, sizeof Pc.scrapname, "%.1000s", v);
    if (id == 7024 && parse_d(v, &d) && d >= (ns_metric ? 0.05 : 0.003) && d <= (ns_metric ? 2500 : 100)) Pc.resuser = d;   /* L8090-8097 */
  }
}
void ns_select(int tpl, unsigned id, int i) {
  NsSheet *s = cur_sheet(); NsPart *p = cur_part();
  if (tpl == 101) {
    if (id == 3003) { sel_sheet = i >= 0 && i < nS ? i : -1; return; }
    if (id == 3021 && i >= 0 && i < 3) { P.grain = "XYN"[i]; return; }      /* L101728 */
    if (id == 7029 && i >= 0 && i < 4) { P.corner = i + 1; return; }        /* L101664 */
    if (id == 3026 && i >= 0 && i < 3) { P.filldir = "XY "[i]; return; }    /* L101695 */
    if (id == 1130 && s && i >= 0) { s->prio = i + 1; return; }             /* L101791 */
  }
  if (tpl == 105) {
    if (id == 4003) { sel_part = i >= 0 && i < nP ? i : -1; return; }
    if (id == 4024 && p && i >= 0 && i < 3) { p->grain = "XYN"[i]; return; }   /* L78630 */
    if (id == 4022 && p && i >= 0 && i < 2) { p->mirror = i; return; }
    if (id == 4017 && p && i >= 0) { p->prio = i + 1; return; }
  }
  if (tpl == 201 && id == 7023 && i >= 0 && i < 4) { Pc.rescbox = i; return; }   /* FUN_18000ccf0 */
  if (tpl == 205 && id == 8002 && i >= 0 && i < nR) { res_sel = i; return; }   /* FUN_180080e30 */
}
/* ---- the property sheet's file buttons and the defaults menu (§4) */
static char ns_script_file[1100];             /* ours: a script's answer to the next file dialog (step nestfile:PATH) */
/* FUN_1800d6590 L147413: the file dialog on add_prefix_suffix(name, data folder, the first extension) - PromptForFileToOpen /
   ToSave, title 10046 "Specify file to read..." / 10047 "Specify file to write..." (mode with / without 'r'); the filter
   label "Nesting Sessions" (10048) for .nstx / .nst [inferred: FUN_1800d94c0 / the prompt functions' filter text NOT DECODED] */
static int ns_ask_file(char *name, size_t n, int save) {
  ns_add_prefix_suffix(name, n, ns_data_dir(), ".nstx");
  if (ns_script_file[0]) { snprintf(name, n, "%s", ns_script_file); ns_script_file[0] = 0; return 1; }
  if (!cui_hooks.ask_file) return 0;
  return cui_hooks.ask_file(name, n, save, save ? "Specify file to write..." : "Specify file to read...", "Nesting Sessions (*.nstx *.nst)", ".nstx");
}
static void ns_new(void) {                    /* FUN_1800441d0 L47307: FUN_18003b250(1) = clear + defaults; the default sheet */
  results_free(); ns_session_reset(); ns_load_defaults();
  if (P.loaddefsheet) add_default_sheet();       /* DAT_1801378d8 && DAT_1801305da (geometry: 1) */
  sel_sheet = nS ? 0 : -1;
}
static void ns_open_file(void) {              /* FUN_180044200 L47323 */
  char name[1100]; snprintf(name, sizeof name, "%s", ns_session_name);
  if (!ns_ask_file(name, sizeof name, 0)) return;
  results_free(); ns_session_reset();            /* FUN_180040180 */
  int ok = ns_session_read(name, sizeof name, 0);
  snprintf(ns_session_name, sizeof ns_session_name, "%s", name);
  if (ok) ns_session_named = 1;
  sel_sheet = nS ? 0 : -1; sel_part = nP ? 0 : -1;   /* ours: the lists' selection after a load NOT DECODED */
}
static void ns_save_file(int as) {            /* FUN_180044350 L47409 / FUN_180044380 L47428 */
  if (!as && ns_session_named) { ns_session_named = ns_session_save(ns_session_name, sizeof ns_session_name); return; }
  char name[1100]; snprintf(name, sizeof name, "%s", ns_session_name);
  if (!ns_ask_file(name, sizeof name, 1)) return;
  snprintf(ns_session_name, sizeof ns_session_name, "%s", name);
  ns_session_named = ns_session_save(ns_session_name, sizeof ns_session_name);
}
static NsCtour *ct_dup(const NsCtour *c, int n) {   /* FUN_18001ada0: a contour copy (the chain link not kept) */
  NsCtour *d = calloc((size_t)(n ? n : 1), sizeof(NsCtour));
  for (int i = 0; i < n; i++) { d[i] = c[i]; d[i].src = NULL; d[i].nsrc = 0; d[i].r = malloc(sizeof(NsRec) * (size_t)(c[i].n ? c[i].n : 1)); memcpy(d[i].r, c[i].r, sizeof(NsRec) * (size_t)c[i].n); }
  return d;
}
/* Save defaults 40002, FUN_180040990 L44623: the selected sheet (with its contours, FUN_18008c900 / FUN_18008c560) becomes the
   default sheet, the selected part's parameters the default part (ClusterMethod 7), the live parameters the defaults; the .defx
   written */
static void ns_save_defaults(void) {
  if (sel_sheet >= 0 && sel_sheet < nS) { ct_free(Sdef.ct, Sdef.nct); Sdef = S[sel_sheet]; Sdef.ct = ct_dup(S[sel_sheet].ct, S[sel_sheet].nct); }
  if (sel_part >= 0 && sel_part < nP) { NsCtour *c = Pdpart.ct; int nc = Pdpart.nct; Pdpart = Pt[sel_part]; Pdpart.ct = c; Pdpart.nct = nc; Pdpart.cluster = 7; }
  params_copy(&Pdef, &P);
  ns_write_defaults();
}
static void ns_load_defaults_cmd(void) { ns_load_defaults(); }   /* Load defaults 40003: FUN_18003b250(0) */
/* Restore 40022, FUN_18003ffa0 L44240: factory values - RestoreLast = No (the registry: not kept here), live tolerance 0.01 / 0.1,
   live label height = the note text height (InitNoteFA) with Automatic on; the default sheet / part / group and the defaults
   block from the ctors (metric scaled), its label height / Automatic likewise; the .defx written */
static void ns_restore(void) {
  DnGlobals g; dn_globals_config(&g, mcam_is_metric(&F));
  P.ptol = ns_metric ? 0.1 : 0.01; P.labelh = g.height; P.labelauto = 1;
  ct_free(Sdef.ct, Sdef.nct); sheet_ctor(&Sdef); ct_free(Pdpart.ct, Pdpart.nct); part_ctor(&Pdpart); group_ctor(&Gdef);
  NsParams c; params_ctor(&c); params_copy(&Pdef, &c); params_metric(&Pdef);
  Pdef.labelh = g.height; Pdef.labelauto = 1;
  ns_write_defaults();
}
void ns_click(int tpl, unsigned id) {
  NsPart *p = cur_part();
  if (tpl == NS_SHEET) {
    if (id == 2) { ns_close(); return; }           /* Cancel: DoModal != 1 */
    if (id == 1) { ns_run(); return; }
    if (id == 40023) { Pc = P; cfg_rc = 0; ns_cfg_open = 1; return; }   /* Config FUN_180066990 -> CCfgDlg */
    if (id == 40014) { ns_new(); return; }         /* New FUN_1800441d0 */
    if (id == 40016) { ns_open_file(); return; }   /* Open FUN_180044200 */
    if (id == 40018) { ns_save_file(0); return; }  /* Save FUN_180044350 */
    if (id == 40020) { ns_save_file(1); return; }  /* Save As FUN_180044380 */
    if (id == 40022) { ns_restore(); return; }     /* Restore FUN_18003ffa0 */
    return;
  }
  if (tpl == 101) switch (id) {
    case 3018: if (nS) S[0].amar = !S[0].amar; for (int i = 1; i < nS; i++) S[i].amar = S[0].amar; return;   /* L102221, L103092-103103 */
    case 7033: P.guillotine = !P.guillotine; return;
    case 3022: P.autoorigins = !P.autoorigins; return;
  }
  if (tpl == 105) switch (id) {
    case 4019: if (p) { p->user_rect = !p->user_rect; if (!p->user_rect) { p->len = p->mmx; p->wid = p->mmy; } } return;   /* L78449-78454 */
    case 1100: if (p) p->commontool = !p->commontool; return;
    case 4034: P.addlabels = !P.addlabels; return;
    case 4035: P.labelauto = 0; return;
    case 4037: P.labelauto = 1; return;
    case 7027: P.autopairs = !P.autopairs; return;
    case 1013: P.fillall = !P.fillall; P.fillers = !P.fillall; return;    /* FUN_180073930 L78283 */
  }
  if (tpl == 201) {
    switch (id) {
    case 2032: Pc.loaddefsheet = !Pc.loaddefsheet; return; case 2034: Pc.dispresults = !Pc.dispresults; return;
    case 2033: Pc.dispgroups = !Pc.dispgroups; return; case 7021: Pc.deletechains = !Pc.deletechains; return;
    case 7027: Pc.nameaslabel = !Pc.nameaslabel; return; case 2040: Pc.compress = !Pc.compress; return;
    case 7007: Pc.cyclecolors = !Pc.cyclecolors; return; case 7012: Pc.cyclelevels = !Pc.cyclelevels; return;
    case 7017: Pc.cycleunused = !Pc.cycleunused; return; case 7200: Pc.highres = !Pc.highres; return;
    case 7018: Pc.savescrap = !Pc.savescrap; return; case 7028: Pc.checkdup = !Pc.checkdup; return;
    case 7031: Pc.trueshape = 0; return; case 7032: Pc.trueshape = 1; return;
    case 7025: Pc.fitpip = !Pc.fitpip; return; case 7026: Pc.prefhole = !Pc.prefhole; return;
    case 1004: cfg_rc ^= 0x100; return;
    case 1:                                        /* OnOK FUN_18000c4b0 L8239 */
      cfg_rc = (cfg_rc & 0x100) | (Pc.trueshape != P.trueshape ? 0x3de : 0);
      P = Pc;
      if (cfg_rc & 0x100) { params_copy(&Pdef, &Pc); ns_write_defaults(); }   /* Save as Defaults: the defaults block + the .defx (L8306-8309) */
      ns_cfg_open = 0; return;
    case 2: ns_cfg_open = 0; return;
    }
  }
  if (tpl == 205) {
    if (id == 40032 || id == 40033) { ns_results_copy(id == 40032); return; }
    if (id == 40031) { if (res_has_ppi(res_sel)) ns_delete_begin(); return; }   /* FUN_18007e660 L84975 (enabled only with PPIs) */
    if (id == 8016) { ns_details(); return; }      /* Details FUN_180080a20 L86299 */
    if ((id == 9268 || id == 9275) && res_sel >= 0 && res_sel < nR) {   /* FUN_180080a70 / FUN_180080b90: +0x5a4 / +0x5a0 = the box */
      NsResult *r = &R[res_sel];
      if (id == 9268 ? r->hval : r->vval) { if (id == 9268) r->hact = !r->hact; else r->vact = !r->vact; trim_show(r); }
      return; }
    if (id == 1) ns_results_end(1);
    else if (id == 2) ns_results_end(2);
    else if (id == 8017) ns_results_end(3);        /* Redo FUN_180080b10: +0xb1a1 and closes */
    else if (id == 8018) ns_save_file(1);          /* Save FUN_180080b50 L86357: FUN_180044380 L47428 (Save As the session) between KillTimer / SetTimer of the preview timer */
  }
}
/* the list menus: 601 (parts) / 609 (sheets) (tools/pe_menus.py mcnestingres.dll); ">" opens a popup, "<" closes it */
static const char *const MENU609[] = { "Add rectangular", "Add chained...", "", "Add sheets from files...", "", "Import from library...", "Save to library...", "", "Move up", "Move down", "", "Delete All" };
static const int MENU609_ID[] = { 40008, 40011, 0, 40013, 0, 40015, 40017, 0, 40021, 40024, 0, 40019 };
static const char *const MENU601[] = { "Add rectangular", "Add chained parts...", "Add chained cluster...", "Disassemble solids...", "Add solids...", "",
  "Add parts from files...", "Add clusters from files...", "Add clusters from folder...", "Add parts from folder...", "",
  ">Add options", "Automatically attach geometry (search)", "Align with longest edge when disassembling solids", "Create a group when adding multiple parts",
  "Ignore holes when adding parts", "Scan for notes", "<", "", "Import from library...", "Save to library...", "", "Attach geometry...", "", "Delete all" };
static const int MENU601_ID[] = { 40005, 40006, 40007, 40033, 40010, 0, 40004, 40027, 40042, 40041, 0, 0, 40404, 40403, 40401, 40402, 40405, 0, 0, 40001, 40002, 0, 40009, 0, 40003 };
static const char *const MENU610[] = { "Save defaults", "Load defaults" };   /* the page background's (the property sheet's commands, §4) */
static const int MENU610_ID[] = { 40002, 40003 };
const char *const *ns_menu(int tpl, unsigned id, const int **cmds, int *n) {
  if (tpl == NS_SHEET && id == 0) { *cmds = MENU610_ID; *n = 2; return MENU610; }
  if (tpl == 101 && id == 3003) { *cmds = MENU609_ID; *n = (int)(sizeof MENU609_ID / sizeof MENU609_ID[0]); return MENU609; }
  if (tpl == 105 && id == 4003) { *cmds = MENU601_ID; *n = (int)(sizeof MENU601_ID / sizeof MENU601_ID[0]); return MENU601; }
  return NULL;
}
int ns_menu_enabled(int tpl, int cmd) {
  if (tpl == NS_SHEET) return cmd == 40002 || cmd == 40003;
  if (tpl == 101) return cmd == 40008 || cmd == 40011 || ((cmd == 40021 || cmd == 40024 || cmd == 40019) && nS > 0);
  if (tpl == 105) return cmd == 40005 || cmd == 40006 || cmd == 40003 || (cmd == 40402 && P.trueshape) || cmd == 40401 || cmd == 40405 || cmd == 40404;   /* 40402 only with TrueShape (§4.2) */
  return 0;
}
void ns_menu_cmd(int tpl, int cmd) {
  if (tpl == NS_SHEET) { if (cmd == 40002) ns_save_defaults(); else if (cmd == 40003) ns_load_defaults_cmd(); return; }
  if (tpl == 101) switch (cmd) {
    case 40008: {                                  /* FUN_180098710 L102147: a copy of the default sheet, "Sheet #"+id */
      NsSheet *s = sheet_push(); *s = Sdef; s->ct = NULL; s->nct = 0; s->id = sheet_counter++;
      snprintf(s->name, sizeof s->name, "Sheet #%d", s->id); s->added_rect = 1; sel_sheet = nS - 1; return; }
    case 40011: ns_begin_chaining(2); return;     /* FUN_180097f50 */
    case 40019: for (int i = 0; i < nS; i++) ct_free(S[i].ct, S[i].nct); nS = 0; sel_sheet = -1; return;
    case 40021: case 40024: {                      /* Move up / down */
      int i = sel_sheet, j = cmd == 40021 ? i - 1 : i + 1; if (i < 0 || j < 0 || j >= nS) return;
      NsSheet t = S[i]; S[i] = S[j]; S[j] = t; sel_sheet = j; return; }
  }
  if (tpl == 105) switch (cmd) {
    case 40005: {                                  /* FUN_180072b30 L77711 */
      int id = part_next_id();                     /* FUN_180039610 over the list without the new part */
      NsPart *p = part_push(); NsPart d = Pdpart; d.ct = NULL; d.nct = 0; *p = d;
      p->id = id; snprintf(p->name, sizeof p->name, "Part #%d", p->id);   /* 10085 + id */
      part_label(p, Pdpart.label, nP - 1);         /* FUN_18005f430(part, default label, the count before the Add) */
      p->cluster = 7; p->added_rect = 1; p->user_rect = 1; sel_part = nP - 1; return; }
    case 40006: ns_begin_chaining(1); return;     /* FUN_180071a20(0) -> FUN_1800745e0 */
    case 40003: for (int i = 0; i < nP; i++) ct_free(Pt[i].ct, Pt[i].nct); nP = 0; sel_part = -1; return;
    case 40401: P.addasgroup = !P.addasgroup; return;   /* toggles of the live params (L75741-75870) */
    case 40402: P.ignoreholes = !P.ignoreholes; return;
    case 40405: P.scannotes = !P.scannotes; return;
    case 40404: P.autoattach = !P.autoattach; return;
  }
}

/* ------------------------------------------------------------------------------------------------ tests (ours) */
void ns_dump(void) {
  char a[64], b[64];
  printf("    nest open %d page %d cfg %d results %d chaining %d trueshape %d\n", ns_open, ns_page, ns_cfg_open, ns_res_open, ns_chaining, P.trueshape);
  if (ns_last_msg[0]) { char m[512]; snprintf(m, sizeof m, "%s", ns_last_msg); for (char *q = m; *q; q++) if (*q == '\n') *q = '|'; printf("    nest msg '%s'\n", m); ns_last_msg[0] = 0; }
  for (int i = 0; i < nS; i++) { NsSheet *s = &S[i]; ns_fmt(s->len, a, 64); ns_fmt(s->wid, b, 64);
    printf("    nest sheet %d id %d '%s' %s x %s qty %d prio %d rect %d origin %.4f,%.4f contours %d\n", i, s->id, s->name, a, b, s->qty, s->prio, s->added_rect, s->ox, s->oy, s->nct); }
  for (int i = 0; i < nP; i++) { NsPart *p = &Pt[i]; ns_fmt(p->len, a, 64); ns_fmt(p->wid, b, 64);
    printf("    nest part %d id %d '%s' label '%s' %s x %s min %d fill %d step %g prio %d grain %c rect %d/%d base %.4f,%.4f contours %d", i, p->id, p->name, p->label, a, b, p->minq, p->fillq, p->step, p->prio, p->grain, p->added_rect, p->user_rect, p->basex, p->basey, p->nct);
    for (int k = 0; k < p->nct; k++) printf(" %d:%d", p->ct[k].type, p->ct[k].n);
    printf("\n"); }
  for (int i = 0; i < nR; i++) { NsResult *r = &R[i];
    printf("    nest result %d sheet %d copy %d/%d util %.2f origin %.4f,%.4f ents %d..%d inst %d:", i, r->sheet, r->copy, r->copies, r->util, r->ox, r->oy, r->ent_from, r->ent_to, r->ninst);
    for (int k = 0; k < r->ninst; k++) printf(" %d@%.4f,%.4f/%g", r->inst[k].part, r->inst[k].x, r->inst[k].y, r->inst[k].angle);
    printf("\n");
    for (int k = 0; k < F.nnotes; k++) {                 /* the result's notes (the sheet-name note) */
      const Note *nt = &F.notes[k]; const uint8_t *E = nt->E; int e = nt->ent; if (e < r->ent_from || e >= r->ent_to) continue;
      double p[3], h, sp; uint16_t w58, w5a; memcpy(p, E + 0x70, 24); memcpy(&h, E + 0x98 + 0x28, 8); memcpy(&sp, E + 0x98 + 0x38, 8);
      memcpy(&w58, E + 0x98 + 0x58, 2); memcpy(&w5a, E + 0x98 + 0x5a, 2);
      uint32_t n; const char *t = mcam_note_text(&F, k, &n);
      printf("    nest note ent %d pos %.4f,%.4f,%.4f h %.4f sp %.4f haln %d valn %d w5a %x font %d color %d level %d '%.*s'\n", e, p[0], p[1], p[2], h, sp,
             (w58 >> 8) & 3, (w58 >> 10) & 7, w5a, E[0x98 + 0x5c], F.ents[e].color, F.ents[e].level, (int)n, t);
    } }
}
/* ours: the script steps - nest (Transform > Geometry Nesting), nestdump (the session) */
int cui_ns_script(const char *st, const char *arg) {
  (void)arg;
  if (!strcmp(st, "nest")) { ns_start(); return 1; }
  if (!strcmp(st, "nestdump")) { ns_dump(); return 1; }
  if (!strcmp(st, "nestlevels")) {                /* the part's level table (mcam_load.h) */
    for (int i = 0; i < F.nlvls; i++) printf("    nest level %d vis %d name '%s'%s\n", F.lvls[i].level, F.lvls[i].vis, F.lvls[i].name, F.lvl_dirty ? " (changed)" : "");
    return 1; }
  if (!strcmp(st, "nestdelesc")) { ns_del_end(); return 1; }
  if (!strcmp(st, "nesttrim")) {                  /* ours: the results' trims (FUN_180085c80) */
    for (int i = 0; i < nR; i++) { NsResult *r = &R[i];
      printf("    nest trim %d ext %.4f,%.4f..%.4f,%.4f h %d/%d cut %.4f [%.4f %.4f %.4f %.4f] v %d/%d cut %.4f [%.4f %.4f %.4f %.4f] both [%.4f %.4f %.4f %.4f] lines %d %d\n",
             i, r->lo[0], r->lo[1], r->hi[0], r->hi[1], r->hval, r->hact, r->hcut, r->ht[0], r->ht[1], r->ht[2], r->ht[3],
             r->vval, r->vact, r->vcut, r->vt[0], r->vt[1], r->vt[2], r->vt[3], r->bt[0], r->bt[1], r->bt[2], r->bt[3], r->hent, r->vent);
      for (int k = 0; k < 2; k++) { int e = k ? r->vent : r->hent; if (e < 0 || !F.ents[e].type) continue; const Ent *en = &F.ents[e];
        printf("    nest trimline %d (%.4f,%.4f)-(%.4f,%.4f) style %d level %d color %d blanked %d\n", e, en->a.x, en->a.y, en->b.x, en->b.y, en->ln_style, en->level, en->color, en->blanked); } }
    return 1; }   /* Esc in the Results Delete loop (get_event returns 0) */
  if (!strcmp(st, "nestppi")) {                   /* the results' Delete state and the placed instances (PPIs) */
    { int nv = 0; for (int i = 0; i < F.nlvls; i++) nv += F.lvls[i].vis != 0;
      printf("    nest deleting %d free level %d main level %d visible levels %d prompt '%s'\n", ns_deleting, ns_free_lv, geo_level, nv, ns_deleting ? cam_msg : ""); }
    for (int i = 0; i < nR; i++) {
      printf("    nest res %d rid %d modified %d delete %d sheet ents %d..%d counts", i, R[i].rid, R[i].modified, res_has_ppi(i), R[i].ent_from, R[i].sheet_to);
      for (int q = 0; q < nP; q++) printf(" %d", R[i].pq ? R[i].pq[q] : -1);
      printf("\n");
    }
    for (int k = 0; k < nPPI; k++) printf("    nest ppi %d rid %d part %d ord %d elem %d ents %d..%d\n", k, PPI[k].rid, PPI[k].part_id, PPI[k].ord, PPI[k].elem, PPI[k].from, PPI[k].to);
    return 1; }
  if (!strcmp(st, "nestfile")) { snprintf(ns_script_file, sizeof ns_script_file, "%s", arg ? arg : ""); return 1; }   /* the next file dialog's answer */
  if (!strcmp(st, "nestcat") && arg) {             /* a file's lines (the session / defaults files in tests) */
    char path[1200]; snprintf(path, sizeof path, "%s", arg); ns_add_prefix_suffix(path, sizeof path, ns_data_dir(), "");
    FILE *fp = fopen(path, "rb"); if (!fp) { printf("    nestfile %s: missing\n", arg); return 1; }
    char line[2200]; while (fgets(line, sizeof line, fp)) { size_t k = strlen(line); int cr = k >= 2 && line[k - 2] == '\r'; while (k && (line[k - 1] == '\n' || line[k - 1] == '\r')) line[--k] = 0;
      printf("    nestfile %s%s\n", line, cr ? "" : " (no CR)"); }
    fclose(fp); return 1; }
  return 0;
}
