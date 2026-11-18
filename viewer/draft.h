/* draft - Mastercam 2017's drafting entities (dimensions D_ID 0x40, notes / labels N_ID 0x10, witness / leader W_ID 0x20) read from a
 * part file and drawn as Mastercam draws them (mccore GetDraftWireframe -> draw_dimension / draw_note: the stored witness / leader
 * records, arrowheads, stroke-font text). Specs: ../formats/drafting/{records,draw,text}.md. The stroke fonts (fontN.fnt) are read
 * from the user's Mastercam install, never shipped. */
#ifndef DRAFT_H
#define DRAFT_H
#include <stddef.h>
#include <stdint.h>

typedef struct DraftDoc DraftDoc;        /* the part's drafting records + memory table */
typedef struct DraftFonts DraftFonts;    /* stroke fonts 0..7 */

/* output: world-space polylines (lines, tessellated arcs, text strokes); CLOSED: last point joins the first; FILL: a filled arrowhead */
typedef struct {
    void *ctx;
    void (*poly)(void *ctx, const double *xyz, int n, int closed, int fill, int color, int view);   /* VIEW: the entity's view number */
} DraftOut;

typedef struct {
    int dims, notes, leaders, hatches;   /* entities drawn */
    int skipped;                         /* entities not drawn (a case NOT PORTED: see note) */
    char note[400];
} DraftStats;

DraftDoc *draft_load(const char *path);                 /* NULL: unreadable; a part without drafting gives an empty doc */
void draft_free(DraftDoc *d);
int draft_count(const DraftDoc *d);                     /* drafting entities in the part */
const uint8_t *draft_record(const DraftDoc *d, int i, uint32_t *n);   /* drafting record I (file order; ent bytes), NULL past the end */
/* fonts from DIR (fontN.fnt / FONTN.FNT, N = 0..7, as mccore FUN_180365a90 probes "font%d.fnt"); NULL dir or no font: NULL */
DraftFonts *draft_fonts_load(const char *dir);
void draft_fonts_free(DraftFonts *f);
/* draws every drafting entity of every view (the caller applies draw_dimension's view test: entity view == graphics view unless
   iso_view_dimensions, config 15007); text needs FONTS (NULL: text skipped, counted) */
void draft_render(const DraftDoc *d, const DraftFonts *fonts, const DraftOut *out, DraftStats *st);
/* drafting records held in memory (N records of LEN bytes each, back to back: the drafting entities made in this session, core_ui
   cui_draft.c), drawn as draft_render draws a part's; no memory table (linear dimensions have none) */
void draft_render_records(const uint8_t *recs, int n, uint32_t len, const DraftFonts *fonts, const DraftOut *out, DraftStats *st);
/* tests: dimensions whose stored text is a plain value, formatted again from the stored value: N compared, EQ equal, DIFF examples */
void draft_value_check(const DraftDoc *d, int *n, int *eq, char *diff, size_t dn);
/* the drafting pick (viewer/draft_pick.c): the text strokes of one record (dimension value / tolerance texts, note text; D: its memory
   table, may be NULL) through OUT; a memory-table entry (mTableFindData), NULL if none */
void draft_render_text(const DraftDoc *d, const uint8_t *E, uint32_t n, const DraftFonts *fonts, const DraftOut *out);
const uint8_t *draft_mtable(const DraftDoc *d, int32_t id, size_t *n);
/* ours: an in-memory doc (the notes of McamFile.notes: core_ui / scene.c) - records and memory-table entries are referenced, not
   copied (they must outlive the doc); draft_set_skip does not apply to it */
DraftDoc *draft_doc_new(void);
void draft_doc_add_rec(DraftDoc *d, const uint8_t *E, uint32_t n);
void draft_doc_add_mt(DraftDoc *d, int32_t id, const uint8_t *p, size_t n);
/* ours: draft_render leaves out a part record for which SKIP returns 1 (a record the session edited: drawn from the session) */
void draft_set_skip(int (*skip)(const uint8_t *E, uint32_t n));
#endif
