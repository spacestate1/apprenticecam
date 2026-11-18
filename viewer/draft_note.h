/* draft_note - Mastercam 2017's note entity (N_ID 0x10, v8_note body; formats/drafting/notes.md): the note globals, the empty record
 * (InitNOTE), the note record of a text at a position (calc_note / CalcLabel's note case) and the creation API other commands use
 * (the Drafting > Note command, core_ui cui_draft_note.c; Geometry Nesting's part labels / sheet-name note).
 *
 * Usage (a note with the user's note settings, then the caller's own attributes):
 *     DnGlobals g; dn_globals_config(&g, mcam_is_metric(&F));      // config items 13000-13019, 14006-14009, 11010 (or dn_globals_init)
 *     g.height = 2.0; g.angle = 0.5; g.h_aln = 1; g.v_aln = 2;      // any field: text height, angle (radians), justification, mirror ...
 *     g.color = 10; g.level = 3;                                    // main_color / main_level at creation
 *     int e = mcam_note_create(&F, &g, 1, world_xyz, "P1");          // the Ent index of the new E_NOTE (-1: refused)
 * The note is kept in F.notes / F.ents (drawn, picked, deleted, undone through the Ent like the wireframe) and written by the part
 * save (mcam_saveops.c: the N_ID record + its memory-table entry). An undo step is the caller's (core_ui: U_ENTS_ADD of the Ent). */
#ifndef DRAFT_NOTE_H
#define DRAFT_NOTE_H
#include <stdint.h>
#include "mcam_load.h"

/* the drafting globals a note record is made from (mccore exports; config item -> global by the writer FUN_1801d6530 / FUN_1801d86a0 /
   FUN_1801d44c0, decomp/draft/core_cfgdraft.c 718-916; create.md 5) */
typedef struct {
    double height;          /* note_text_height (13011): the text (cap) height, world units */
    double aspect;          /* note_chr_exp (13004): character width factor */
    double rot;             /* note_chr_rot (13005): rotation of each character (radians, text.md 1) */
    double slant;           /* note_text_slant_angle (13009, radians) */
    double angle;           /* note_text_angle (13019): the string angle (radians) */
    double extra_sp;        /* note_extra_sp (13001): extra character spacing */
    double extra_ln_sp;     /* extra_ln_sp (13000): extra line spacing */
    int font;               /* note_font (13006): < 8 stroke font fontN.fnt, else TrueType (text.md 2.2; not drawn here) */
    int text_bx;            /* note_text_bx (13007): text box / underline bits (+0x5e) */
    int text_path;          /* note_text_path (13008): 0 right ... (bits 13-15 of +0x58) */
    int space_sty;          /* note_space_sty (13010): 1 proportional */
    int h_aln;              /* h_txt_aln (13002): horizontal justification (bits 8-9 of +0x58): 0 Left, 1 Center, 2 Right (text.md 1) */
    int v_aln;              /* v_txt_aln (13012): vertical justification (bits 10-12 of +0x58): 0 Top, 1 Cap, 2 Half, 3 Base, 4 Bottom (text.md 1) */
    int mirror;             /* mirror (13003): bits 2-3 of +0x5a */
    double alt_h, alt_aspect; int alt_font, alt_color, fill, fill_color;   /* no config item (core_cfgdraft.c writes none): the .data
                               initializers (alt_height / alt_chr_exp 1.0, the rest 0) */
    int arrow_type;         /* DAT_1813d61fc = dd_global+4 (14006): the note leader's arrowhead style */
    double arrow_h, arrow_w;   /* note_arrow_head_height / _width (14008 / 14009) */
    double scale;           /* draft_scale (11010) */
    int color, level;       /* main_color / main_level */
    uint8_t attrib[16];     /* main_attrib (ent +0x10..+0x1f) */
    uint8_t ver[2];         /* DAT_18143d54a / b: the version stamp InitNOTE writes (+0x48 / +0x49; the save writes the base part's) */
} DnGlobals;

/* the .data / startup values with no configuration (zero, alt 1.0, attrib width 1.0 / styles 1, colour / level 1, Mastercam 2017's
   stamp 0x13 / 0x69): the base dn_globals_config fills from the user's config */
void dn_globals_init(DnGlobals *g);
/* the note globals from the user's config file of these units (mcam_config_value: mcamxm.config metric / mcamx.config inch); an item
   missing keeps dn_globals_init's value */
void dn_globals_config(DnGlobals *g, int metric);
/* init_attribs_note @180252610 (decomp/draft/core_create.c 649-686): FA's note fields from G (its other bits kept) */
void dn_attribs_note(uint8_t fa[0x60], const DnGlobals *g);
/* InitNOTE @180249f70 + FUN_18024a060 + InitNoteFA + InitNoteWL(1) (core_draftwf.c 234-391, 1616-1647): the empty note record (kind 3,
   view VIEW - 0 = 1 -, scale, the font, the leader slot off) */
void dn_init_note(uint8_t E[0x1c8], const DnGlobals *g, int view);
/* calc_note @18024d2d0 (core_draftwf.c 394-447) / CalcLabel's note case (no leader points, core_dimlayout.c 1036-1150): InitNOTE, the
   view, the text position POS (VIEW coordinates: x, y, depth) at +0x70; the text id at +0xf8 is mcam_note_add's */
void dn_calc_note(uint8_t E[0x1c8], const DnGlobals *g, int view, const double pos_view[3]);

/* ---- the part (McamFile) ---- */
/* a note made here: E (an N_ID record image) and TEXT (8-bit, "\r\n" line breaks; mTableAlloc(strlen + 1) + strcpy, calc_note) - the text
   becomes a memory-table entry of the note (a new id: F.mt_next) referenced at +0xf8. Returns the new Ent (E_NOTE, created), -1 for an
   empty text (calc_note returns 0 for it) */
int mcam_note_add(McamFile *f, const uint8_t E[0x1c8], const char *text);
/* the API: dn_calc_note at POS (WORLD; world_to_view of view VIEW - a reserved view 1..8) with G, then mcam_note_add */
int mcam_note_create(McamFile *f, const DnGlobals *g, int view, const double pos_world[3], const char *text);
/* the text of note K (its +0xf8 entry), "" when it has none; N = its byte count (NUL excluded) */
const char *mcam_note_text(const McamFile *f, int k, uint32_t *n);
/* the memory-table entry ID of note K, NULL if none */
const NoteMt *mcam_note_mt(const McamFile *f, int k, int32_t id);
/* the notes as a drafting doc (viewer/draft.h: draft_render / draft_render_text / dp_distance): every live note (the Ent not deleted and
   not blanked - draw_ent draws an entity only with sel bit 0x100 off) when K < 0, else note K alone; each record's colour / level are
   its Ent's first (Home > Attributes). The doc references F's notes: free it (draft_free) before they change */
struct DraftDoc *mcam_notes_doc(McamFile *f, int k);
#endif
