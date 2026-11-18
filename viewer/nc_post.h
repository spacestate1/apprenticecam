/* nc_post - the LinuxCNC post: a whole part (Mastercam's stored toolpaths + operations made here) to G-code in the
 * format of the user's proven LinuxCNC (4020 router) programs. See nc_post.c and ../formats/post.md. */
#ifndef NC_POST_H
#define NC_POST_H
#include <stdio.h>
#include "cam2d.h"
typedef struct {
    const char *style;        /* "r180" (2024+ programs: R arcs, pieces <= 180 deg) or "ij-quad" (I/J, quadrant pieces) */
    const char *progname;     /* program name in the header (default: the part file name) */
    const char *only;         /* NULL = all ops; else only[i] != 0 posts F.ops[i] */
} NcPostOpts;
/* 1 = written; 0 + err otherwise. Ops in F.ops order; camops[j]->op_index says which ones were made / edited here. */
/* cutter compensation in control (G41 / G42 D / G40, batch 5): only with the environment switch MCAM_CUTTER_COMP=1 (ours, like
   MCAM_H_OFFSET; formats/post.md "Cutter compensation in control"); off = the dialog offers Computer / Off and the post refuses
   any op carrying compensation codes */
int nc_cutter_comp_on(void);
/* batch 8: MCAM_DRILL_CYCLES=1 (ours, formats/post.md "Drill cycles") posts dwell (G82 P), Tap (G84 / G74) and Bore #1 (G85 / G89 P);
   off = today's output (G81 / G83 / G73; other cycles refused) */
int nc_drill_cycles_on(void);
/* batch 11: MCAM_COOLANT=1 (ours, formats/post.md "Coolant") posts each operation's coolant (the Coolant page: Flood M8 / Mist M7 / off
   M9, before / with / after as the stock Generic Fanuc 3X Mill.pst places them); off = today's output (no coolant words) */
int nc_coolant_on(void);
int nc_force_tc_on(void);           /* batch 12: MCAM_FORCE_TOOL_CHANGE=1 (formats/post.md "Force tool change") */
/* the drill page's post text (page 0x27, entry cycle x 12 + k): the label, *on = 0 when the post disables the control */
const char *nc_drill_text(int cyc, int k, int *on);
int nc_post_part(FILE *out, McamFile *f, CamOp **camops, int ncamops, const NcPostOpts *o, char *err, size_t errn);
/* Backplot motion list (formats/backplot.md): the post's own motions, in program order */
enum { NCM_RAPID = 0, NCM_LINE = 1, NCM_CW = 2, NCM_CCW = 3, NCM_DRILL = 4, NCM_TOOL = 5 };
typedef struct {
    int kind, op_index, tool, rpm, plane, cyc;
    double a[3], b[3], c[3];          /* start, end, arc centre */
    double feed, rplane, peck;        /* feed < 0: rapid */
    double r, a0, sweep, len;         /* arcs: radius, start angle, signed sweep in the plane; length */
} NcMotion;
int nc_motions(const McamFile *f, CamOp **camops, int ncamops, const char *only, NcMotion **out);   /* ONLY: NULL or per F.ops */   /* count; *out malloc'd */
void nc_motion_point(const NcMotion *m, double t, double *p);
/* Stock Setup "NCI extents" (mastercam.dll FUN_1801ef250 -> mccore get_bnci_extents -> FUN_1800fda60): the box of the
   active (first) machine group's toolpaths. top_inc[j] = camops[j]'s top of stock is incremental (record +0x2f0); tol =
   config 1000, chord = curve_chord_dev (config 1006). Returns FUN_1800fda60's count (0 = nothing: no tool change seen);
   *undecoded = 1 when a record needed a rule that is NOT DECODED (it was left out). */
int nc_extents(const McamFile *f, CamOp **camops, int ncamops, const int *top_inc, double tol, double chord, double box[6], int *undecoded);
#endif
