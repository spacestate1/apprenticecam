/* draft_note - the note record and the note creation API (see draft_note.h; spec ../formats/drafting/notes.md).
 *
 * Sources (Mastercam 2017 x64 mccore.dll; decompiles decomp/draft/): InitNOTE @180249f70 (core_draftwf.c 234-286), FUN_18024a060
 * (core_draftwf.c 1616-1647), InitNoteFA @18024a120 (core_draftwf.c 292-354), InitNoteWL @18024a310 (core_draftwf.c 361-391) over
 * InitDimWL @180249e40 (core_draftwf.c 181-231), init_attribs_note @180252610 (core_create.c 649-686), calc_note @18024d2d0
 * (core_draftwf.c 394-447), CalcLabel @180248780 (core_dimlayout.c 1036-1180), mTableAlloc @1802fd100 (core_draftwf.c 1650-1704),
 * the config writer FUN_1801d6530 / FUN_1801d86a0 / FUN_1801d44c0 (core_cfgdraft.c 718-916: item number -> global). */
#include "draft_note.h"
#include "mcam_saveops.h"
#include "draft.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void P16(uint8_t *b, size_t o, uint16_t v) { memcpy(b + o, &v, 2); }
static void P32(uint8_t *b, size_t o, uint32_t v) { memcpy(b + o, &v, 4); }
static void PD(uint8_t *b, size_t o, double v) { memcpy(b + o, &v, 8); }
static uint16_t U16(const uint8_t *b, size_t o) { uint16_t v; memcpy(&v, b + o, 2); return v; }
static int32_t I32(const uint8_t *b, size_t o) { int32_t v; memcpy(&v, b + o, 4); return v; }

void dn_globals_init(DnGlobals *g) {
    memset(g, 0, sizeof *g);
    g->alt_h = 1.0; g->alt_aspect = 1.0;                           /* alt_height / alt_chr_exp .data initializers (cui_draft.c dr_globals_load) */
    g->color = 1; g->level = 1;
    double w = 1.0; int16_t one = 1;                               /* main_attrib as this program's records carry it (mcam_saveops.c head: width
                                                                      1.0, styles 1) */
    memcpy(g->attrib, &w, 8); memcpy(g->attrib + 8, &one, 2); memcpy(g->attrib + 10, &one, 2); memcpy(g->attrib + 12, &one, 2);
    g->scale = 1.0;
    g->ver[0] = 0x13; g->ver[1] = 0x69;                            /* DAT_18143d54a / b: Mastercam 2017's stamp (cui_draft.c dr_globals_load) */
}
static int cfg(int metric, int key, char *v, size_t n) { mcam_config_value(metric, key, v, n); return v[0] != 0; }
static void cfgd(int metric, int key, double *d) { char v[128]; if (cfg(metric, key, v, sizeof v)) *d = atof(v); }
static void cfgi(int metric, int key, int *d) {
    char v[128]; if (!cfg(metric, key, v, sizeof v)) return;
    *d = v[0] == 'y' || v[0] == 'Y' ? 1 : v[0] == 'n' || v[0] == 'N' ? 0 : atoi(v);
}
/* [note text] FUN_1801d6530 (core_cfgdraft.c 845-880): 13000 extra_ln_sp, 13001 note_extra_sp, 13002 h_txt_aln, 13003 mirror, 13004
   note_chr_exp, 13005 note_chr_rot, 13006 note_font, 13007 note_text_bx, 13008 note_text_path, 13009 note_text_slant_angle, 13010
   note_space_sty, 13011 note_text_height, 13012 v_txt_aln, 13019 note_text_angle; [leaders / witness] FUN_1801d86a0 (l.890-907): 14006
   DAT_1813d61fc, 14008 / 14009 note_arrow_head_height / _width; [dimension attributes] 11010 draft_scale (l.740). The reader is taken
   as the writer's mirror (create.md 5) */
void dn_globals_config(DnGlobals *g, int metric) {
    dn_globals_init(g);
    cfgd(metric, 13000, &g->extra_ln_sp); cfgd(metric, 13001, &g->extra_sp); cfgi(metric, 13002, &g->h_aln); cfgi(metric, 13003, &g->mirror);
    cfgd(metric, 13004, &g->aspect); cfgd(metric, 13005, &g->rot); cfgi(metric, 13006, &g->font); cfgi(metric, 13007, &g->text_bx);
    cfgi(metric, 13008, &g->text_path); cfgd(metric, 13009, &g->slant); cfgi(metric, 13010, &g->space_sty); cfgd(metric, 13011, &g->height);
    cfgi(metric, 13012, &g->v_aln); cfgd(metric, 13019, &g->angle);
    cfgi(metric, 14006, &g->arrow_type); cfgd(metric, 14008, &g->arrow_h); cfgd(metric, 14009, &g->arrow_w);
    cfgd(metric, 11010, &g->scale);
}

/* init_attribs_note (core_create.c 657-686) */
void dn_attribs_note(uint8_t fa[0x60], const DnGlobals *g) {
    PD(fa, 0x50, g->alt_aspect); fa[0x5d] = (uint8_t)g->alt_font; PD(fa, 0x48, g->alt_h);
    PD(fa, 0, 0); PD(fa, 8, 0);
    PD(fa, 0x30, g->aspect); PD(fa, 0x20, g->rot); PD(fa, 0x40, g->extra_ln_sp); PD(fa, 0x38, g->extra_sp);
    fa[0x5c] = (uint8_t)g->font; PD(fa, 0x28, g->height); PD(fa, 0x18, g->slant); PD(fa, 0x10, g->angle);
    fa[0x5e] = (uint8_t)g->text_bx;
    uint16_t w58 = U16(fa, 0x58), w5a = U16(fa, 0x5a);
    w58 = (uint16_t)((w58 & ~0xf) | (g->alt_color & 0xf));
    w5a = (uint16_t)(((g->fill & 1) << 1) | (w5a & 0xd));
    w58 = (uint16_t)((w58 & ~0xf0) | ((g->fill_color << 4) & 0xf0));
    w58 = (uint16_t)((w58 & ~0x300) | ((g->h_aln << 8) & 0x300));
    w5a = (uint16_t)((w5a & ~0xc) | ((g->mirror << 2) & 0xc));
    w58 = (uint16_t)((g->text_path << 13) | (w58 & 0x1fff));
    w5a = (uint16_t)((w5a & ~1) | (g->space_sty & 1));
    w58 = (uint16_t)((w58 & ~0x1c00) | ((g->v_aln << 10) & 0x1c00));
    P16(fa, 0x58, w58); P16(fa, 0x5a, w5a);
}

/* InitNOTE: memset, the version stamp, class 0x10, u16 +4 = 1, main colour / level / attrib, eptr 0; FUN_18024a060 on the body
   (memset 0x160, kind 3, position 0, view = GetActiveViewNo (0 -> 1), draft_scale, text id 0, InitNoteFA (= init_attribs_note on a zero
   font_attr, +0x5a & 0xf, +0x5f 0), InitNoteWL(+0x98, 1) = InitDimWL(1) with the note arrow style / size, the rest 0) */
void dn_init_note(uint8_t E[0x1c8], const DnGlobals *g, int view) {
    memset(E, 0, 0x1c8);
    E[0x48] = g->ver[0]; E[0x49] = g->ver[1];
    P32(E, 0, 0x10); P16(E, 4, 1); E[6] = (uint8_t)g->color; P32(E, 8, (uint32_t)g->level);
    memcpy(E + 0x10, g->attrib, 16);
    uint8_t *b = E + 0x68;
    b[0] = 3;
    P16(b, 0x20, (uint16_t)(view ? view : 1));
    PD(b, 0x28, g->scale);
    dn_attribs_note(b + 0x30, g); b[0x30 + 0x5f] = 0; P16(b + 0x30, 0x5a, U16(b + 0x30, 0x5a) & 0xf);
    uint8_t *wl = b + 0x98;                                        /* InitDimWL(1): on 0, kind 1, +2 u16 0x100 (visible), +4 1, 2 points */
    wl[1] = 1; wl[3] = 1; wl[4] = 1; P16(wl, 0x1a, 2);
    wl[0x18] = (uint8_t)g->arrow_type; PD(wl, 8, g->arrow_h); PD(wl, 0x10, g->arrow_w);   /* InitNoteWL */
}

/* calc_note: InitNOTE, +0x88 = the view, +0x70 / +0x78 / +0x80 = the point (CalcLabel: the same, kind 3 with no leader points) */
void dn_calc_note(uint8_t E[0x1c8], const DnGlobals *g, int view, const double pos_view[3]) {
    dn_init_note(E, g, view);
    P16(E, 0x88, (uint16_t)(view ? view : 1));
    PD(E, 0x70, pos_view[0]); PD(E, 0x78, pos_view[1]); PD(E, 0x80, pos_view[2]);
}

/* ------------------------------------------------------------------------------------------------ the part */
int mcam_note_add(McamFile *f, const uint8_t E[0x1c8], const char *text) {
    size_t L = text ? strlen(text) : 0;
    if (!L) return -1;                                             /* calc_note: strlen + 1 > 1, else 0 (core_draftwf.c 425-440) */
    if (f->mt_next < 1) f->mt_next = 1;
    f->notes = realloc(f->notes, sizeof *f->notes * (size_t)(f->nnotes + 1));
    Note *nt = &f->notes[f->nnotes]; memset(nt, 0, sizeof *nt);
    memcpy(nt->E, E, 0x1c8);
    nt->mt = calloc(1, sizeof *nt->mt); nt->nmt = 1;
    nt->mt[0].id = f->mt_next++; nt->mt[0].n = (uint32_t)(L + 1);   /* mTableAlloc(strlen + 1), strcpy_bb (the calloc'd NUL kept) */
    nt->mt[0].p = calloc(1, L + 1); memcpy(nt->mt[0].p, text, L);
    P32(nt->E, 0xf8, (uint32_t)nt->mt[0].id);
    f->nnotes++;
    return mcam_new_note_ent(f, f->nnotes - 1);
}
int mcam_note_create(McamFile *f, const DnGlobals *g, int view, const double pos_world[3], const char *text) {
    double m[9], v[3];
    if (!mcam_std_view(view ? view : 1, m)) return -1;             /* world_to_view: the reserved views only (the view table: NOT PORTED) */
    for (int i = 0; i < 3; i++) v[i] = m[3 * i] * pos_world[0] + m[3 * i + 1] * pos_world[1] + m[3 * i + 2] * pos_world[2];
    uint8_t E[0x1c8]; dn_calc_note(E, g, view, v);
    return mcam_note_add(f, E, text);
}
const NoteMt *mcam_note_mt(const McamFile *f, int k, int32_t id) {
    if (k < 0 || k >= f->nnotes || !id) return NULL;
    for (int i = 0; i < f->notes[k].nmt; i++) if (f->notes[k].mt[i].id == id) return &f->notes[k].mt[i];
    return NULL;
}
const char *mcam_note_text(const McamFile *f, int k, uint32_t *n) {
    if (n) *n = 0;
    if (k < 0 || k >= f->nnotes || f->notes[k].E[0x68] < 2) return "";
    const NoteMt *m = mcam_note_mt(f, k, I32(f->notes[k].E, 0xf8));
    if (!m || !m->n) return "";
    if (n) { uint32_t L = 0; while (L < m->n && m->p[L]) L++; *n = L; }
    return (const char *)m->p;
}

struct DraftDoc *mcam_notes_doc(McamFile *f, int k) {
    DraftDoc *d = draft_doc_new();
    for (int i = k < 0 ? 0 : k; i < f->nnotes && (k < 0 || i == k); i++) {
        Note *nt = &f->notes[i]; int e = nt->ent;
        if (e < 0 || e >= f->nents || f->ents[e].type != E_NOTE || (k < 0 && (f->ents[e].blanked || !mcam_level_visible(f, f->ents[e].level)))) continue;   /* drawn on a visible level
                                                                      (core_draft_rec.c 1324 LevelData::isVisible) */
        nt->E[6] = (uint8_t)f->ents[e].color; P32(nt->E, 8, (uint32_t)f->ents[e].level);
        draft_doc_add_rec(d, nt->E, 0x1c8);
        for (int q = 0; q < nt->nmt; q++) draft_doc_add_mt(d, nt->mt[q].id, nt->mt[q].p, nt->mt[q].n);
    }
    return d;
}
