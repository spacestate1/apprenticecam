/* draft_ord - Mastercam 2017's ORDINATE dimension record (mccore calc_ordinate_dimension @18024d3d0), ported for the Drafting tab's
 * Ordinate group (viewer/core_ui/cui_draft_ord.c). The function fills the 0x810-byte D_ID record image the way Mastercam fills the
 * entity on each call (the creation in DraftApp::doOrdinate, each mouse move of the ordinate drag, the regeneration FUN_1803053a0).
 * Spec: ../formats/drafting/create.md 8 (the commands) and draw.md 4.5 (the record); the corpus oracle tools/tests/draft_ord_test.c. */
#ifndef DRAFT_ORD_H
#define DRAFT_ORD_H
#include <stdint.h>
#include "draft_create.h"

/* the session globals calc_ordinate_dimension reads beyond the linear ones (DcSettings, draft_create.h; DraftGlobalsFromDim
   core_draftwf.c 713-716 sets them from an ordinate record +0x122 / +0x124 / +0x125) */
typedef struct {
    DcSettings S;                       /* dimension font (S.fa: height, aspect = chr_exp, extra_ln_sp), arrow, leader style (DAT_1813d6202),
                                           gap (witness_gap), format, tolerance mode (DAT_1813d6210: only 0 ported), auto_alignment, sel_dist */
    int h_aln;                          /* h_txt_aln (short @1812d9808): 0 / 2 the text starts at the cursor (0: grows away, 2: towards), 1 centred */
    int neg_sign;                       /* ord_neg_sign (@1813d5a3a): signed offsets */
    int arrowhd;                        /* ord_arrowhd (@1812d986d): the leader starts witness_gap off the point */
} DoSettings;

typedef struct {
    uint8_t ap[0x98];                   /* param_2: the ordinate's aPoint (world position at +8) */
    double text[3];                     /* param_3: the text point, WORLD (the cursor) */
    double p4[3], p5[3], p6[3];         /* param_4 / 5 / 6 WORLD: the zero line (p4 -> p5) and the text-row origin p6 when there is no base;
                                           written back (the leader's first two points and the text point) when type > 0 */
    int view; double vm[9];             /* param_7 (FUN_1802579d0: 0 -> the graphics view, NOT PORTED - give one) and its matrix rows */
    double depth;                       /* param_8: the construction depth (view z) */
    int type;                           /* param_9 = DfOrdType: 0 a child of a base, 1 a zero (base) ordinate, 2 a child whose base is gone */
    int orient;                         /* param_10 in / out: 0 horizontal (measures x, text vertical), 1 vertical, 2 parallel */
    int has_base;                       /* param_11: the base ordinate gives the zero line and the text row */
    const uint8_t *base;                /* param_12: the base record (0x810) */
    uint64_t base_eptr;                 /* its ent+0x20 (the record's +0x108 when type 0) */
    int ctrl;                           /* GetAsyncKeyState(VK_CONTROL) held */
} DoInput;

typedef struct {
    int valid;                          /* *param_13 */
    int not_ported;                     /* a branch that is NOT PORTED was needed (tolerance mode, template base, TrueType); E unchanged */
    const char *why;
} DoResult;

/* calc_ordinate_dimension into E (the caller's record: InitDIM'd at creation, the stored record when dragging / regenerating) */
DoResult do_calc_ordinate(uint8_t E[0x810], DoInput *in, const DoSettings *O, const DcFonts *F);
/* DfOrdType @1802f1710 (core_draft_rec.c 715-757) on a record whose parent (+0x108) is PARENT (NULL: not found / not alive): 0 child,
   1 zero, 2 orphan; E +0x100 / +0x338 are written as DfOrdType writes them */
int do_ord_type(uint8_t E[0x810], const uint8_t *parent);
/* the orientation the drag set-up FUN_180046920 (mccad) / the regeneration FUN_1803053a0 derive from a record's text angle (+0x5e8):
   pi/2 -> 0, 0 -> 1, else 2 */
int do_orient_of(const uint8_t E[0x810]);
/* FUN_180255830 (mccore, decomp/draft/ord_core_helpers.c 1288) with param_2 = 1: the text start before (h_txt_aln 0) / after (2) the text centre
   T (view) along the text angle, by the box half width */
void do_text_ends(const uint8_t E[0x810], const DcFonts *F, const double T[3], double before[3], double after[3]);
/* DraftGlobalsFromDim @180308cc0 (core_draftwf.c 540-716): the globals the ordinate computation reads, from a dimension record */
void do_globals_from_dim(const uint8_t *E, DoSettings *O);
/* the oc - the ordinates of a set moved along with the one dragged (Align, panel id 17): df_load_oc's entries (mccore, decomp/draft/
   ord_core_helpers.c 499) - a record copy each, its order index (10001.. one side, 9999.. the other), its distance from the dragged one's
   leader, its text box half sizes and their components along the text */
typedef struct { uint8_t E[0x810]; int16_t idx; double d, bx, by, cbx, sbx; uint64_t eptr; int src; } DoOc;
typedef struct { double ang, bx, by, pad, ddx, ddy, lx0, ly0, lx1, ly1; } DoOcGlob;   /* DAT_1813c79f0 / 7a18 / 7a20 / 79e8 / 7a28 / 7a30 / 79f8.. */
/* FUN_1802423f0 (ord_core_oc.c): the order of the entries around the dragged record DRAG; 0 = the oc is not used */
int do_oc_order(DoOc *oc, int n, const uint8_t *drag, int orient, const DcFonts *F, DoOcGlob *g);
/* df_adjust_oc @180240fd0 (ord_core_helpers.c 203): every entry's text placed along the dragged text's row, staggered by their boxes */
void do_oc_adjust(DoOc *oc, int n, const uint8_t *drag, int orient, const DoOcGlob *g, const DcFonts *F);
/* move_ordinate_dimension @180254d40 (ord_core_helpers.c 948): the text to P4 (CODE 0: its start), P5 (1: its centre) or P6 (2: its end),
   the leader rebuilt; 1 = done */
int do_move_ordinate(uint8_t E[0x810], int code, double p4[3], double p5[3], double p6[3], const DcFonts *F);
#endif
