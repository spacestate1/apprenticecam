/* cui_draft_note.c - Drafting > Note (doDraft_NOTE 0x9486) as Mastercam 2017 runs it: mccad FUN_18003d410 (decomp/draft/cad_rundraft.c
 * 722-1440; formats/drafting/create.md 4, notes.md 5), the Note dialog (mccoreres 452, CNoteDlg / doNoteDlg - decomp/draft/core_notedlg.c,
 * core_create.c 1417-1540), the note's creation at the first mouse move (FUN_1800389a0 -> DfCreateLabel -> CalcLabel, cad_note.c 67-104,
 * core_draft_rec2.c 184-298, core_dimlayout.c 1036-1180) and its drag until the click that stores it (FUN_180038c50 -> RunDraftCommand(99)
 * -> the note drag state: FUN_180045180 / FUN_180045310 / FUN_180046110 / FUN_180045670 / FUN_180045eb0, cad_note.c 184-745). The note
 * is an E_NOTE entity (viewer/draft_note.h); one undo step per note (UndoTransStart in FUN_1800389a0 .. UndoTransCommit).
 *
 * Runs in the wireframe command frame through the dimension loop's entry points (cui_draft.c dr_*: geo_cmd = G_NOTE). Ported: the radios
 * Single Note / Multiple Notes (mode 0). NOT PORTED (greyed in the dialog): the label radios (CalcLabel's leader branch + df_adjust_ldrs),
 * the leader-only radios (FUN_18003caa0 / FUN_1800408f0), Load File (m_win_open_file's path NOT DECODED), Add Symbol (doCharMap),
 * Properties (CfgDimX_dialog), the edit control's line / character count tooltip (FUN_1805c0760), the '~' formatting pick during the drag
 * (ToNoteGlobalsFromDraft), the drag's key events (FUN_180045e70 / FUN_180046070: NOT DECODED). "ours" marks glue. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "paths.h"
#include "core_ui.h"
#include "cui_draft_int.h"
#include "draft_note.h"
#include "mcam_saveops.h"
#include "scene.h"

int dn_cmd(int c) { return c == G_NOTE; }
int dn_find_ft(const char *ft) { return ft && !strcmp(ft, "doDraft_NOTE") ? G_NOTE : 0; }   /* the ribbon's FT (layout.txt Drafting > Note) */

/* ------------------------------------------------------------------------------------------------ the Note dialog (mccoreres 452) */
/* the template (tools/pe_dialogs.py mccoreres.dll, dialog 452 "Note Dialog", 288 x 174 du); classes as core_ui.h BsCtl (1 button, 2 edit,
   5 radio, 8 group box) */
const BsCtl NOTE_452[] = {
    { 25300, 2, 8, 8, 196, 71, "" },
    { 2307, 8, 8, 87, 272, 53, "Create" },
    { 3888, 5, 13, 99, 60, 10, "Single Note" }, { 3889, 5, 13, 111, 60, 10, "Multiple Notes" },
    { 3890, 5, 81, 99, 105, 10, "Label with Single Leader" }, { 3891, 5, 81, 111, 105, 10, "Label with Segmented Leader" },
    { 3892, 5, 81, 123, 105, 10, "Label with Multiple Leaders" },
    { 3893, 5, 197, 99, 73, 10, "Single Leader" }, { 3894, 5, 197, 111, 73, 10, "Segmented Leader" }, { 3895, 5, 197, 123, 73, 10, "Multiple Leaders" },
    { 1358, 1, 212, 24, 68, 18, "&Load File..." }, { 1352, 1, 212, 46, 68, 18, "&Add Symbol..." }, { 1369, 1, 8, 148, 68, 18, "&Properties..." },
    { 1, 1, 166, 148, 36, 18, "&OK" }, { 2, 1, 205, 148, 36, 18, "&Cancel" }, { 9, 1, 244, 148, 36, 18, "&Help" },
    { 0, 0, 0, 0, 0, 0, NULL } };
/* the radio index (dialog +0x2c38) of each button: FUN_1805c0620 / FUN_1805c06c0 (core_notedlg.c 682-764) */
static const unsigned NOTE_RADIO[8] = { 3888, 3889, 3890, 3891, 3892, 3893, 3894, 3895 };
int dn_dlg_radio_index(unsigned id) { for (int i = 0; i < 8; i++) if (NOTE_RADIO[i] == id) return i; return -1; }
/* a control is live here: the radios 0 / 1, the edit, OK / Cancel; the rest is NOT PORTED (the header comment) */
int dn_dlg_ported(unsigned id) { return id == 3888 || id == 3889 || id == 25300 || id == 1 || id == 2; }
/* CNoteDlg's enable rules while creating (DAT_18142cb14 = 0): OnInitDialog FUN_1805bfc00 (core_notedlg.c 236-358) and the radio / edit
   handlers FUN_1805bf9c0 / FUN_1805bf950 / FUN_1805bfb60: a note / label radio (index < 5) enables the edit, Load File and Add Symbol, OK
   only with a text; a leader radio (5..7) disables the edit, Load File and Add Symbol and enables OK. 1 = enabled (before the NOT PORTED
   greying) */
int dn_dlg_enabled(unsigned id, int radio, int text_empty) {
    if (id == 1) return radio >= 5 || !text_empty;
    if (id == 25300 || id == 1358 || id == 1352) return radio < 5;
    return 1;
}
/* doNoteDlg's kept state: the last text (DAT_18142cb18, the dialog shows it again: the Note command empties its CString first, so the
   dialog is "creating", DAT_1812f8584 = 1) and the last radio (DAT_18142cb10, OnOK stores it while creating). Its start value is 0 =
   Single Note (decoded 2026-11-05, user decision NT-1): RVA 0x142cb10 lies past the file-backed part of mccore's .data (raw size 0xc6c00
   from RVA 0x12ab000; virtual 0x199a38), so the loader zero-fills it; the only store in mccore is OnOK (0x1805c03a2), the reads are
   OnInitDialog (0x1805bfdbf) and doNoteDlg (0x1805c0a2f / 0x1805c0a6f / 0x1805c0aa0); it is not exported, no config writer
   (core_cfgdraft.c) touches it and the user's registry (NTUSER.DAT, ...\Mastercam 2017) has no note key: a per-session value */
char dn_last_text[10000]; int dn_last_radio;
int stay_in_create_note;                                 /* mccore stay_in_create_note: Multiple Notes */
/* the script's dialog answer (no front end: cui_hooks.note_dialog NULL / a scripted run): OK with this radio and text, or Cancel */
int cui_script_note_ok = 1, cui_script_note_radio = 0; char cui_script_note_text[4096] = "NOTE";

/* doNoteDlg(text, &mode, 0) (core_create.c 1436-1540): the dialog on the kept text / radio; OK -> the text kept, mode 0 for radios 0 / 1
   (1 sets stay_in_create_note, 0 clears it), 1 for 2 / 5, 2 for 3 / 6, 3 for 4 / 7; radios > 4 return an empty text. 0 = Cancel */
static int dn_dialog(char *text, size_t n, int *mode) {
    char t[sizeof dn_last_text]; snprintf(t, sizeof t, "%s", dn_last_text); int radio = dn_last_radio;
    int ok = cui_hooks.note_dialog ? cui_hooks.note_dialog(t, sizeof t, &radio)
                                   : (cui_script_note_ok ? (snprintf(t, sizeof t, "%s", cui_script_note_text), radio = cui_script_note_radio, 1) : 0);
    if (!ok) return 0;
    if (radio < 0 || radio > 7) radio = 0;
    snprintf(dn_last_text, sizeof dn_last_text, "%s", t); dn_last_radio = radio;   /* FUN_1805c0370 (OnOK) + doNoteDlg l.1484-1490 */
    if (radio > 4) t[0] = 0;
    if (!t[0] && radio <= 4) return 0;                   /* (message 40 + Cancel; OK is disabled for an empty text, so not reached) */
    snprintf(text, n, "%s", t);
    if (radio == 4 || radio == 7) *mode = 3; else if (radio == 3 || radio == 6) *mode = 2; else if (radio == 2 || radio == 5) *mode = 1;
    else { *mode = 0; stay_in_create_note = radio == 1; }
    return 1;
}

/* ------------------------------------------------------------------------------------------------ the command */
static char dn_text[sizeof dn_last_text];               /* the note text (app +0x48) */
static int dn_phase;                                     /* 0: waiting for the first mouse move (get_event "*" flags 0x100), 1: the drag */
static int dn_ent = -1;                                  /* the note being placed (its E_NOTE Ent) */
static DnGlobals dn_G;                                   /* the note globals (create.md 5), loaded with the dimension globals */
static int dn_G_units = -1;

static void dn_prompt(void) {
    /* the drag (FUN_180045a00, cad_note.c 745-829): mccadres 27709 "Create note: " (27711 "Create label: " for kind 2) + 27704 "Indicate
       text position"; 27070 (the '~' formatting pick) left out (NOT PORTED). Before the first mouse move get_event's prompt is "*" (none) */
    if (dn_phase == 1) snprintf(cam_msg, sizeof cam_msg, "Create note: Indicate text position");
    else cam_msg[0] = 0;
}
static void dn_globals(void) {
    int metric = mcam_is_metric(&F);
    if (dn_G_units != metric) { dn_G_units = metric; dn_globals_config(&dn_G, metric); }
}
/* FUN_18003d410's set-up: DfActive, ClosePrompt, the dialog; a leader-only answer (empty text) goes to the leader / witness command (NOT
   PORTED: refused); a label mode is NOT PORTED (its radios are greyed). Then the function panel (DraftingSmartDimensionsManager with every
   control disabled: FUN_180258d60 + FUN_180257bc0, cad_dimcmd.c 933-967) and the loop of FUN_1800389a0 */
int dn_begin(void) {
    int mode = 0;
    if (!dn_dialog(dn_text, sizeof dn_text, &mode)) return 0;   /* Cancel: the command ends */
    if (mode != 0) { cui_message("Note", "Labels and leaders (Note dialog radios 3890 - 3895) are not ported yet."); return 0; }
    dn_globals();
    dn_phase = 0; dn_ent = -1; dn_prompt();
    return 1;
}
/* the note being placed is still this document's (ours): a made-here E_NOTE with its Note - a document change ends the command
   (cui_geo.c geo_part_closing), this guards the index anyway */
static int dn_valid(void) {
    if (dn_ent < 0 || dn_ent >= F.nents) return 0;
    const Ent *en = &F.ents[dn_ent];
    return en->type == E_NOTE && en->created && en->nt >= 0 && en->nt < F.nnotes && F.notes[en->nt].ent == dn_ent;
}
/* the note being placed is dropped (UndoTransTerminate of FUN_1800389a0's transaction: the store_ent of DfCreateLabel is rolled back,
   decomp/align/core_undo.c 145-172) */
static void dn_drop(void) {
    if (dn_valid()) { ent_off(dn_ent); scene_notes_rebuild(); }
    dn_ent = -1;
}
void dn_end(void) { if (dn_phase == 1) dn_drop(); dn_phase = 0; cam_msg[0] = 0; }

/* FUN_180046110 (cad_note.c 628-655): the point onto the construction plane (proj_pt_cp), world_to_view of the note's view -> +0x70; the
   drag first resets the note to the globals (FUN_180045310: main_attrib, init_attribs_note, df_reset_ldr_arrows - leaders only -, main
   colour / level) */
static void dn_position(const double w[3]) {
    if (!dn_valid()) return;
    Note *nt = &F.notes[F.ents[dn_ent].nt]; uint8_t *E = nt->E;
    int view = (int16_t)(E[0x88] | E[0x89] << 8); double m[9], v[3];
    if (!mcam_std_view(view ? view : 1, m)) return;
    double p[3] = { w[0], w[1], grid_z() };            /* proj_pt_cp: the construction plane (Top at the construction depth here) */
    for (int i = 0; i < 3; i++) v[i] = m[3 * i] * p[0] + m[3 * i + 1] * p[1] + m[3 * i + 2] * p[2];
    memcpy(E + 0x70, v, 24);
    memcpy(E + 0x10, dn_G.attrib, 16); dn_attribs_note(E + 0x98, &dn_G);
    E[6] = (uint8_t)geo_color; memcpy(E + 8, &geo_level, 4);
    F.ents[dn_ent].color = geo_color; F.ents[dn_ent].level = geo_level;
    scene_notes_rebuild(); overlay_dirty = 1;
}
/* a mouse move (WORLD = the sketched point, cui_geo.c geo_cursor_update's 't' path = df_calc_sketched_point): the first one is
   get_event 0x100's drag event (FUN_1804e5280: mode bit 0x100 -> SetupDragEvent, decomp/draft/ev_handlers.c 866-985) - DfCreateLabel
   there: InitNOTE with the globals, the active view, the point, kind 3, the text in the memory table (mcam_note_add); then the drag */
int dn_move(const double world[3], int ctrl) {
    (void)ctrl;
    if (dn_phase == 0) {
        dn_G.color = geo_color; dn_G.level = geo_level;
        double p[3] = { world[0], world[1], grid_z() };
        dn_ent = mcam_note_create(&F, &dn_G, dr_view_no(), p, dn_text);
        if (dn_ent < 0) return 1;
        dn_phase = 1; dn_prompt(); doc_dirty = 1;
    }
    dn_position(world);
    return 1;
}
/* a click in the drag: get_event flags 0xd (FUN_180045a00: points / sketch, auto-highlight off) - an AutoCursor point is a point event
   (FUN_1804e3b90) and the note is stored there (event 1: FUN_180045eb0 -> FUN_180046110(the event's point), FUN_180045670(1)); else the
   sketch event (4) stores it where the drag left it (FUN_180045670(1): df_save_drag_ent). Then DraftGlobalsFromNote (the globals come back
   unchanged: the note was made from them), UndoTransCommit; Multiple Notes (stay_in_create_note) waits for the next first move, else the
   command ends (cad_rundraft.c 1163-1193). Before the first move a click is no event of flags 0x100 (FUN_1804e3b90 takes none) */
int dn_click(float mx, float my, const double w[3], int ctrl) {
    if (dn_phase != 1) return 1;
    if (!dn_valid()) { dn_ent = -1; dn_phase = 0; return stay_in_create_note; }   /* (ours: no note of this document to store) */
    V3 p = { w[0], w[1], grid_z() };
    geo_snap_ptr(mx, my); int sn = !ctrl && geo_snap(&p, NULL, -1, 1); geo_snap_ptr_off();
    if (sn) { double q[3] = { p.x, p.y, p.z }; dn_position(q); }
    else { double c[3]; dr_sketched(mx, my, w, c); dn_position(c); }
    URec r; memset(&r, 0, sizeof r); r.kind = U_ENTS_ADD;
    r.ents = malloc(sizeof(int)); r.types = malloc(sizeof(int)); r.ents[0] = dn_ent; r.types[0] = E_NOTE; r.n = 1;
    undo_push(r); doc_dirty = 1;
    dn_ent = -1; dn_phase = 0;
    if (!stay_in_create_note) return 0;                  /* the command ends (geo_end) */
    dn_prompt();
    return 1;
}
/* every mouse move is the sketched point the note follows (FUN_180045310 -> df_calc_sketched_point of the event's cursor), and the
   AutoCursor runs live meanwhile: the drag's get_event flags 0xd hold the point bit 1, so the mouse-move handler FUN_1804e5280 ->
   FUN_1804e55b0 sets DAT_1813f5f4e = mode & 1 and runs FUN_1804df190's fast-cursor search (use_fast_cursor && DAT_1813f5f4e,
   decomp/mccore_autocursor2.c 88; FUN_1804e55b0: decomp/draft/ev_move.c 97-277, notes.md 5) - 's' = both (cui_geo.c) */
int dn_need(void) { return 's'; }
/* Esc: get_event fails - before the first move FUN_1800389a0 returns 0, in the drag FUN_180038c50 reports no success and
   UndoTransTerminate drops the note; the event result 2 ends the loop (cad_rundraft.c 1163-1193, asm 0x18003d9c2-0x18003d9fb): the
   command ends either way */
int dn_esc(void) { if (dn_phase == 1) dn_drop(); dn_phase = 0; return 0; }
/* the panel's buttons during the note: the drag state's key events (FUN_180045180 event 8 -> FUN_180045e70 -> FUN_18012b9f0 -> FUN_180045d00
   -> FUN_18012af20, cad_linear2.c 244-320 + asm 0x180045d00-0x180045e0a) take panel id 23 as "store the note where the drag left it"
   (code 3: init_attribs_note, FUN_180046110, FUN_180045670(1)) and ids 24 / 25 as an abandon (codes 4 / 5 -> FUN_180045130:
   FUN_180045670(0), no success -> UndoTransTerminate), ids 19 / 20 as the label leader Add / Delete; which FunctionPanel button posts
   23 / 24 / 25 is NOT DECODED (create.md 2.5), so the panel is greyed during the command (user decision NT-2; cui_geo.c
   geo_panel_enabled) and a button code that still arrives (a script step) is ignored */
int dn_button(int code) { (void)code; return 1; }

/* the panel (FUN_180258d60 + FUN_180257bc0: every control disabled): DraftingSmartDimensionsManagerView's layout as cui_draft.c draws it,
   all greyed */
void dn_panel_body(const CuiPanelOps *ops, void *u) {
    int t = -1, z = 0; double a = 0;
    if (!ops->tab || ops->tab(u, 0, "Basic")) {
        if (ops->exp(u, "Entity")) { ops->label(u, "Mode:"); ops->radio(u, "_Auto", &t, 0, 0); ops->radio(u, "_Horizontal", &t, 1, 0); ops->radio(u, "_Vertical", &t, 2, 0);
            ops->num(u, "Orientation an_gle:", "angle", &a, NULL, 0, -360, 360); ops->check(u, "_Maintain perpendicular to arc center", &z, 0); ops->check(u, "Loc_k", &z, 0); ops->end(u); }
        if (ops->exp(u, "Arc Symbol")) { ops->radio(u, "Radi_us", &t, 0, 0); ops->radio(u, "_Diameter", &t, 1, 0); ops->check(u, "A_pply to linear dimension", &z, 0); ops->end(u); }
        if (ops->exp(u, "_Witness lines")) { ops->radio(u, "_Both", &t, 0, 0); ops->radio(u, "_Right", &t, 1, 0); ops->radio(u, "_Left", &t, 2, 0); ops->end(u); }
        if (ops->exp(u, "Leaders")) { ops->label(u, "Arrows:"); ops->radio(u, "_Inside", &t, 0, 0); ops->radio(u, "_Outside", &t, 1, 0); ops->end(u); }
        if (ops->exp(u, "Format")) { ops->toggle(u, "_Font", 0, -1); ops->toggle(u, "_Edit text", 0, -1); ops->toggle(u, "Heigh_t", 0, -1);
            int d = 0; ops->inum(u, "De_cimal places:", "digits", &d, NULL, 0, 0, 8); ops->end(u); }
    }
    if (!ops->tab || ops->tab(u, 1, "Advanced")) {
        if (ops->exp(u, "Dimension Text")) { ops->check(u, "_Centered text", &z, 0); ops->label(u, "Edit Mode:"); ops->radio(u, "P_lace", &t, 0, 0); ops->radio(u, "_Edit", &t, 1, 0); ops->end(u); }
        if (ops->exp(u, "Dimension Box")) { ops->check(u, "_Show when dragging", &z, 0); ops->end(u); }
        if (ops->exp(u, "Settings")) { ops->toggle(u, "_Options", 0, -1); ops->toggle(u, "_Update", 0, -1); ops->end(u); }
    }
}

/* ------------------------------------------------------------------------------------------------ project file, tests (ours) */
static void hexs(FILE *f, const uint8_t *p, size_t n) { for (size_t k = 0; k < n; k++) fprintf(f, "%02x", p[k]); }
/* NOTE HEX(record) HEX(text): a note made here (cui_doc.c writes it in entity order, among the GEO lines) */
void dn_write_ent(FILE *f, int e) {
    const Ent *en = &F.ents[e]; if (en->type != E_NOTE || en->nt < 0 || en->nt >= F.nnotes) return;
    uint8_t E[0x1c8]; memcpy(E, F.notes[en->nt].E, 0x1c8); E[6] = (uint8_t)en->color; memcpy(E + 8, &en->level, 4);
    if (en->attr_edited) {                               /* Home > Attributes on it: width / the style fields (as mcam_saveops.c note_attr_patch) */
        if (en->width >= 1) memcpy(E + 0x10, &en->width, 8);
        uint16_t ls = (uint16_t)en->ln_style, ps = (uint16_t)en->pt_style; memcpy(E + 0x18, &ls, 2); memcpy(E + 0x1c, &ps, 2);
    }
    uint32_t n; const char *t = mcam_note_text(&F, en->nt, &n);
    fprintf(f, "NOTE "); hexs(f, E, 0x1c8); fputc(' ', f); hexs(f, (const uint8_t *)t, n); fputc('\n', f);
}
/* a NOTE line that cannot be read keeps its place: a deleted entity, as a GEO 0 line makes (cui_doc.c), so the indices of the entities
   after it stay as written (ours) */
static int dn_placeholder(void) { V3 z = { 0, 0, 0 }; int k = mcam_new_ent(&F, E_LINE, z, z, z, 0, 0, 0, 1, 1); if (k >= 0) mcam_delete_ent(&F, k); return 1; }
int dn_read_line(const char *ln) {
    if (strncmp(ln, "NOTE ", 5)) return 0;
    const char *h = ln + 5; if (strlen(h) < 2 * 0x1c8) return dn_placeholder();
    uint8_t E[0x1c8]; for (int k = 0; k < 0x1c8; k++) { unsigned v = 0; sscanf(h + 2 * k, "%2x", &v); E[k] = (uint8_t)v; }
    h += 2 * 0x1c8; while (*h == ' ') h++;
    size_t L = strspn(h, "0123456789abcdefABCDEF") / 2; char *t = calloc(1, L + 1);
    for (size_t k = 0; k < L; k++) { unsigned v = 0; sscanf(h + 2 * k, "%2x", &v); t[k] = (char)v; }
    int k = mcam_note_add(&F, E, t); free(t);
    if (k < 0) return dn_placeholder();
    return 1;
}
/* notedump: every note (Ent, kind, view, position, height, angle, justification, font, colour / level, text, state) */
static void dn_dump(void) {
    printf("    note cmd %d phase %d ent %d stay %d radio %d\n", geo_cmd == G_NOTE, dn_phase, dn_ent, stay_in_create_note, dn_last_radio);
    for (int k = 0; k < F.nnotes; k++) {
        const Note *nt = &F.notes[k]; const uint8_t *E = nt->E; int e = nt->ent; double p[3], h, a; uint16_t w58;
        memcpy(p, E + 0x70, 24); memcpy(&h, E + 0x98 + 0x28, 8); memcpy(&a, E + 0x98 + 0x10, 8); memcpy(&w58, E + 0x98 + 0x58, 2);
        uint32_t n; const char *t = mcam_note_text(&F, k, &n); char s[200]; size_t j = 0;
        for (uint32_t i = 0; i < n && j + 4 < sizeof s; i++) { unsigned char c = (unsigned char)t[i]; if (c == '\r') { s[j++] = '\\'; s[j++] = 'r'; } else if (c == '\n') { s[j++] = '\\'; s[j++] = 'n'; } else s[j++] = (char)c; }
        s[j] = 0;
        const Ent *en = e >= 0 && e < F.nents ? &F.ents[e] : NULL;
        printf("      note %d ent %d cls 0x%x kind %d view %d pos (%.4f,%.4f,%.4f) h %.4f ang %.4f haln %d valn %d font %d color %d level %d mt %d text \"%s\"%s%s%s\n", k, e,
               E[0], E[0x68], (int16_t)(E[0x88] | E[0x89] << 8), p[0], p[1], p[2], h, a, (w58 >> 8) & 3, (w58 >> 10) & 7, E[0x98 + 0x5c], en ? en->color : E[6],
               en ? en->level : (int)(E[8] | E[9] << 8), nt->nmt, s, en && !en->type ? " deleted" : "", en && en->created ? " new" : "", en && en->blanked ? " blanked" : "");
    }
}
/* script steps: notedlg:RADIO|TEXT (the dialog's scripted answer; "\n" in TEXT = a line break, stored "\r\n" as the edit control's) /
   notedlg:cancel, notemove:X,Y (a mouse move at world X,Y), notedump */
int dn_script(const char *st, const char *arg) {
    if (!strcmp(st, "notedlg") && arg) {
        if (!strcmp(arg, "cancel")) { cui_script_note_ok = 0; return 1; }
        cui_script_note_ok = 1; cui_script_note_radio = atoi(arg);
        const char *b = strchr(arg, '|'); b = b ? b + 1 : "";
        size_t j = 0; for (const char *q = b; *q && j + 3 < sizeof cui_script_note_text; q++) {
            if (q[0] == '\\' && q[1] == 'n') { cui_script_note_text[j++] = '\r'; cui_script_note_text[j++] = '\n'; q++; } else cui_script_note_text[j++] = *q; }
        cui_script_note_text[j] = 0;
        return 1;
    }
    if (!strcmp(st, "notemove") && arg) {               /* a mouse move through the view's path (cui_geo.c geo_cursor_update): the note and the
                                                           live AutoCursor (its snapped point printed) */
        double x = 0, y = 0; sscanf(arg, "%lf,%lf", &x, &y); double w[3] = { x, y, 0 };
        if (geo_cmd != G_NOTE) { printf("    (no Note command)\n"); return 1; }
        geo_cursor_update(w, 0, 0);
        printf("    autocursor %s (%.4f,%.4f)\n", geo_snap_ent ? "snapped" : "free", geo_cursor.x, geo_cursor.y);
        return 1;
    }
    if (!strcmp(st, "notedump")) { dn_dump(); return 1; }
    /* noteapi:X,Y,H,ANG,HALN,VALN,COLOR,LEVEL|TEXT - the creation API (draft_note.h mcam_note_create) as another command uses it: the user's
       note globals, then H / ANG (radians) / HALN / VALN / COLOR / LEVEL when > 0 (ANG: != 0), view 1 at world (X, Y, construction depth);
       one undo step (ours: the caller's U_ENTS_ADD) */
    if (!strcmp(st, "noteapi") && arg) {
        double x = 0, y = 0, h = 0, ang = 0; int ha = 0, va = 0, co = 0, lv = 0;
        sscanf(arg, "%lf,%lf,%lf,%lf,%d,%d,%d,%d", &x, &y, &h, &ang, &ha, &va, &co, &lv);
        const char *b = strchr(arg, '|'); b = b ? b + 1 : "";
        char t[1024]; size_t j = 0; for (const char *q = b; *q && j + 3 < sizeof t; q++) { if (q[0] == '\\' && q[1] == 'n') { t[j++] = '\r'; t[j++] = '\n'; q++; } else t[j++] = *q; }
        t[j] = 0;
        DnGlobals g; dn_globals_config(&g, mcam_is_metric(&F));
        g.color = co > 0 ? co : geo_color; g.level = lv > 0 ? lv : geo_level;
        if (h > 0) g.height = h; if (ang != 0) g.angle = ang; if (ha > 0) g.h_aln = ha; if (va > 0) g.v_aln = va;
        double p[3] = { x, y, grid_z() };
        int e = mcam_note_create(&F, &g, 1, p, t);
        if (e >= 0) { URec r; memset(&r, 0, sizeof r); r.kind = U_ENTS_ADD; r.ents = malloc(sizeof(int)); r.types = malloc(sizeof(int)); r.ents[0] = e; r.types[0] = E_NOTE; r.n = 1;
                      undo_push(r); doc_dirty = 1; scene_notes_rebuild(); }
        else printf("    (noteapi: refused)\n");
        return 1;
    }
    return 0;
}
