/* draft_pick - Mastercam 2017's pick of a drafting entity (dimension D_ID, note / label N_ID, witness / leader W_ID) under the cursor:
 * mastercam.dll select_ent_univ -> FUN_18020f4e0 (decomp/draft/ord_mc_seldim.c), its drafting cases and their distance functions
 * (ord_mc_seldim2.c, ord_mc_pick.c; mccore distance_to_string / calc_arrow_vec / dist_to_arc / proj_iso_pt, ord_core_pick.c). Shared by
 * the Drafting commands that pick an existing drafting entity (core_ui cui_draft_ord.c; Smart Dimension later). Spec:
 * ../formats/drafting/create.md 9. */
#ifndef DRAFT_PICK_H
#define DRAFT_PICK_H
#include <stdint.h>
#include "draft.h"

typedef struct {
    const uint8_t *E; uint32_t n;       /* the record (ent bytes) */
    const DraftDoc *doc;                /* its part's memory table (note text, leader point arrays, chained leaders); NULL: none */
} DpRec;

/* the graphics view: proj_iso_pt's axes (rows 0 / 1 of the view matrix: screen right / up), its number (get_gr_view_n) and the config
   15007 rule (iso_view_dimensions: drafting of every view is pickable) */
typedef struct { double gm[9]; int gview; int iso; } DpView;

/* the distance select_ent_univ takes for one record: the cursor C (the graphics view's 2-D point, proj_iso_pt) to the nearest of the
   record's drawn text strokes, witness / leader polylines (arcs) and arrowheads; 1 = a distance below LIM in *D */
int dp_distance(const DpRec *r, const DraftFonts *F, const DpView *V, const double C[2], double lim, double *d);
/* the pick: among R (display-list order) the records of MASK (0x40 dimensions, 0x10 notes, 0x20 witness / leaders) in the graphics view,
   the strictly nearest with a distance below LIM (sel_dist; an exact tie keeps the first); its index or -1 */
int dp_pick(const DpRec *R, int nr, int mask, const DraftFonts *F, const DpView *V, const double C[2], double lim, double *d);
#endif
