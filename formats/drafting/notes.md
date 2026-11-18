# Notes (N_ID 0x10 note / label, W_ID 0x20 leader) - entities, file, Drafting > Note, creation API

Mastercam 2017 (mccore.dll / mccad.dll, Ghidra VAs). Decompiles under `decomp/draft/` (gitignored); "file:line" as in the sibling docs.
The record layout is `records.md` 3 (with the header of records.md 1), the drawing `draw.md` 1.3 / 3.3 / 4.6, the text `text.md`, the
command flow `create.md` 4 - this file is what the program does with them (2026-11-05) and what was added to the decoding.
Code: `viewer/draft_note.{h,c}` (record + API), `viewer/mcam_load.c` (read), `viewer/mcam_saveops.c` (write), `viewer/scene.c`
(display), `viewer/core_ui/cui_note.c` (pick / highlight), `viewer/core_ui/cui_draft_note.c` (the command, its dialog's model, project
lines, script steps), `viewer-qt/src/mainwindow.cpp` hook_note_dialog (the dialog). Tests: `tools/tests/note_compare.sh` (section 9).

## 1. Status

| | |
|---|---|
| part notes / labels / leaders | read (every N_ID / W_ID record of 0x1c8 bytes with its memory-table entries), drawn, picked, selected, deleted, undone, colour / level / width changed (Home), saved back: unchanged records and table entries byte for byte, deleted ones removed with their entries |
| Drafting > Note | Single Note and Multiple Notes as Mastercam (dialog 452, the note made at the first mouse move, dragged, stored by the click, Esc) |
| creation API | `mcam_note_create` / `mcam_note_add` (section 7) - for Geometry Nesting's labels / sheet note (not wired: another agent) |
| NOT PORTED | labels / leaders creation (radios 3890-3895), Load File / Add Symbol / Properties, window selection of notes, Transform / Copy / Paste / Analyze / AutoCursor / Bounding Box of notes, editing an existing note (double-click / drag-edit), TrueType note text, text box / path / mirror drawing (draw.md) |

## 2. The entity (E_NOTE)

* Every N_ID (0x10) and W_ID (0x20) record whose length is 0x1c8 (bytes_per_ent: the v8_note body is fixed, records.md 0; the corpus's one
  corrupt 861-byte N_ID is left to the file and not loaded) becomes a `Note` (`McamFile.notes`: the record copy + its memory-table
  entries) and an Ent of type `E_NOTE` (0x10), added after the surfaces so earlier entity indices (project files) do not move.
  (Ours: the Ent order is this program's index space, not a Mastercam order - Mastercam keeps one display list in file order, and the
  corpus N_ID records follow the geometry; the only effect of the order here is that a project saved before 2026-11-05, on a part with
  notes, whose operations chain geometry drawn in the app, would point at shifted indices. Left as is: user decision 2026-11-05.)
  Ent identity / colour / level / width / style come from the record header (+0x34 / +6 / +8 / +0x10 / +0x18); `blanked` = sel bit
  0x100 (corpus: 105 N_ID records carry 0x101).
* The entries a note references are the ones `mTableDeleteNote` @1802fd620 frees (core_draft_rec2.c 2310-2370): the text (+0xf8) when the
  kind (+0x68) > 1 (only it when > 2), then the leader chain from +0x100 while its on byte is set: the aPoint array id (wl +0x20) when wl
  +0x19, the next wl's id (wl +0x84). `mcam_load.c note_collect`.
* Drawn by the drafting layer (scene.c draft_notes: viewer/draft.c draw_note over a doc of F.notes, `mcam_notes_doc`), not from the part
  file: the part doc's N_ID / W_ID records are skipped (core_ui cui_draft_pick.c skip_rec). A deleted or blanked note is not drawn
  (mastercam.dll draw_ent draws an entity only with sel bit 0x100 off, mcam_load.h Ent.blanked) - before this the display layer drew
  blanked notes too. The record's colour / level are the Ent's (Home > Attributes).
* Pick (core_ui cui_note.c note_pick): select_ent_univ's N_ID / W_ID case = viewer/draft_pick.c dp_distance (the drawn text strokes, the
  leader chain, arrowheads; the graphics view or iso_view_dimensions, config 15007), the aperture sel_dist; a note competes with the
  wireframe by screen distance in pick_entity, where surfaces are candidates (the general selection; the commands keep their masks).
  Highlight: the note's drawn strokes (note_segs) through the selection / auto-highlight pattern (cui_overlay.c).
* Delete = Home > Delete entities (delete_all_by_mask's mask 0x1f1fff holds 0x10 / 0x20), undo / redo as the wireframe (U_ENTS_DEL /
  U_ENTS_ADD). Attributes: colour (0x1f1fff), width (0x1f1ffe), point style (0x1f1fff: writes +0x18 and +0x1c) apply; line style does not
  (mask 0x1f078e has no 0x10 / 0x20; cui_attr.c set_lstyle).
* Not a candidate of: the wireframe commands' selection (sel_list), window / polygon selection (no decoded test for drafting),
  chaining, AutoCursor (dl_search_fast's drafting cases NOT PORTED), Transform, Copy / Paste, Analyze, Bounding Box.

## 3. On disk

### 3.1 The record
records.md 3. Header as written by this program for a note made here: `head()` of mcam_saveops.c - class, sel bits 0x81 (corpus N_ID:
870 x 0x81, 726 x 0x1, 105 x 0x101; W_ID 114 x 0x81, 89 x 0x1), colour / level of the Ent, a new eptr / ent_idn / time, the base part's
version stamp; +0x10..+0x1f = main_attrib as InitNOTE set it (or the Ent's width / style after Home > Attributes).

### 3.2 The memory table (DB_ID sub 0xE + its out-of-band block)
* Every part has the marker record DB_ID sub 0xE followed by the table: entries `[0x28 header][size bytes]`, then a 0x28 header with id 0
  (the user's parts without drafting: just the terminator, all zero). save_db writes it right after the toolpath groups
  (decomp/mccore_fileio.c 1137-1141: a zeroed record, class 0x200, +0x68 = 0xE, +0x4a = DAT_18143d54c, write_ent_to_disk; the raw writer
  FUN_1802c72c0 then writes the table, FUN_180302ad0). Parts that seemed to lack it (16 of the 2017 corpus) had toolpath groups with
  member entries the readers skipped wrongly (mcam_write.md 2, viewer/mcam_grp.h); the 2018+ files (major 0x16 / 0x17 / 0x18) have a
  different group record and are not read here. Header: +0 i32 id, +0x10 u64 size; +8 / +0x18 / +0x20 are pointers
  Mastercam leaves from memory (the terminator's +4 is stack garbage, e.g. 0x7ffd).
* `mTableRead` @1803025a0 (core_draft_rec2.c 2416-2497) reads only id and size (`SysFile::Read(.., 0x28)`; the 0x14-byte form for
  another MCFile mode) and gives every entry a NEW id (FUN_1802fcff0), keeping old -> new in DAT_1813d8be8 / DAT_1813d8c10: the stored
  ids are file-local tokens. So a writer needs ids unique in the file only, and zero pointers.
* A note's text entry: `mTableAlloc(strlen + 1)` + strcpy (calc_note core_draftwf.c 425-440, CalcLabel core_dimlayout.c 1151-1175): the
  bytes with their NUL; lines end "\r\n" (the edit control's).
* Order: the corpus's N_ID / W_ID records all follow the table. The read-time fix-up of a note looks its text up (mTableFindData(+0xf8),
  decomp/mccore_dbread.c 3085; a failure counts the entity (+0x3e90) and returns before its view is mapped, l.3086-3090), so this program writes new notes after
  the table (mcam_saveops.c: at the geometry slot, or right after the table when the base part has geometry slots before it - the
  defaults library Mill_mm has an ASSOC record before its system records).

### 3.3 The save (mcam_saveops.c)
* Part notes not touched: their records and entries stay where they are, byte for byte (the base-part copy).
* A deleted part note: its record is left out (the deleted-curve rule, by ent_idn) and its entries are left out of the table.
* Colour / level / width / style changed: the header fields are rewritten in place (as the curves': +6, +8, +0x10, +0x18, +0x1c).
* A note made here: its record (ids renumbered) after the table; its entries get ids above the base table's highest and are appended
  before the terminator. The table segment is rebuilt only when a note was made or deleted; the terminator is the base part's.

## 4. Defaults (the note globals)
`dn_globals_config` (draft_note.c): config items by the writer FUN_1801d6530 / FUN_1801d86a0 / FUN_1801d44c0 (core_cfgdraft.c 740, 845-907;
the reader is taken as the writer's mirror, create.md 5); the user's values (mcamxm.config metric / mcamx.config inch):

| item | global | font_attr / record | user (metric) |
|---|---|---|---|
| 13000 Extra line spacing | extra_ln_sp | fa +0x40 | 0.2157 |
| 13001 Extra char spacing | note_extra_sp | fa +0x38 | 0.2 |
| 13002 Horizontal | h_txt_aln | fa +0x58 bits 8-9 | 0 (Left) |
| 13003 Mirror | mirror | fa +0x5a bits 2-3 | 0 |
| 13004 Aspect ratio | note_chr_exp | fa +0x30 | 0.5 |
| 13005 Rotation | note_chr_rot | fa +0x20 | 0 |
| 13006 Font | note_font | fa +0x5c | 0 (13016 name "Stick") |
| 13007 Lines | note_text_bx | fa +0x5e | 0 |
| 13008 Path | note_text_path | fa +0x58 bits 13-15 | 0 |
| 13009 Slant | note_text_slant_angle | fa +0x18 | 0 |
| 13010 Spacing | note_space_sty | fa +0x5a bit 0 | 1 |
| 13011 Text height | note_text_height | fa +0x28 | 5.0 (inch 0.25) |
| 13012 Vertical | v_txt_aln | fa +0x58 bits 10-12 | 3 (Base) |
| 13019 Angle | note_text_angle | fa +0x10 | 0 |
| 14006 Style | DAT_1813d61fc (dd_global+4) | wl +0x18 | 0 |
| 14008 / 14009 Height / Width | note_arrow_head_height / _width | wl +8 / +0x10 | 5.0 / 1.665 |
| 11010 Scale | draft_scale | body +0x28 | 1.0 |

alt_height / alt_chr_exp (1.0), alt_font / alt_color / fill / fill_color (0) have no config item: their .data initializers. main_color /
main_level are the wireframe commands' (geo_color / geo_level), main_attrib this program's (width 1.0, styles 1).

## 5. Drafting > Note (doDraft_NOTE 0x9486, mccad FUN_18003d410 cad_rundraft.c 722-1440)

1. `DisplayNoteDlg` -> `doNoteDlg(text, &mode, 0)` (core_create.c 1417-1540). The command empties its text first, so the dialog is
   "creating": it shows the text and the radio of the last OK (DAT_18142cb18 / DAT_18142cb10). The radio's start value is **0 = Single
   Note** (decoded 2026-11-05, user decision NT-1): RVA 0x142cb10 lies past the file-backed part of mccore's .data (.data RVA 0x12ab000,
   raw size 0xc6c00, virtual size 0x199a38), so the loader zero-fills it; in the whole of mccore's code the only store is OnOK
   (FUN_1805c0370, 0x1805c03a2) and the reads are OnInitDialog (0x1805bfdbf) and doNoteDlg (0x1805c0a2f / 0x1805c0a6f / 0x1805c0aa0);
   it is not in the export table (no other DLL can set it), no config writer (core_cfgdraft.c) uses it, and the user's registry
   (NTUSER.DAT, `SOFTWARE\CNC Software, Inc.\Mastercam 2017` and below, tools/regf.py) holds no note / drafting value. So it is a
   per-session value starting at Single Note; the text (DAT_18142cb18) likewise starts empty.
   * Dialog 452 "Note Dialog" (mccoreres, 288 x 174 du, core_ui NOTE_452): edit 25300 (multi-line; font Arial unless the note font is
     TrueType, 1.5 x the dialog font height, OnInitDialog FUN_1805bfc00 l.257-279), group 2307 "Create" with radios 3888..3895 (index
     0..7, FUN_1805c0620 / c06c0), buttons 1358 Load File, 1352 Add Symbol, 1369 Properties, OK, Cancel, Help.
   * Enable rules (creating): a note / label radio (index < 5) enables the edit, Load File, Add Symbol; OK only with a text (OnInitDialog,
     FUN_1805bf9c0 radio, FUN_1805bfb60 EN_CHANGE); a leader radio (5..7) disables the edit, Load File, Add Symbol and enables OK
     (FUN_1805bf950). OnOK (FUN_1805c0370) keeps the text and the radio.
   * Result: mode 0 for radios 0 / 1 (stay_in_create_note = radio 1), 1 for 2 / 5, 2 for 3 / 6, 3 for 4 / 7; radios > 4 give an empty
     text. An empty note / label text gives message 40 and Cancel (FUN_180242fa0(0x28) = LoadString 40 of mccoreres, which reads
     "Computing the scallops..." - unreachable in practice: OK is disabled without a text).
   * Here: radios 0 / 1, the edit, OK / Cancel live; the label / leader radios, Load File (m_win_open_file on DAT_18142cb40, default
     `default_geo_filename` - whether a file chooser opens is NOT DECODED), Add Symbol (doCharMap), Properties (CfgDimX_dialog 0x67 /
     0x6b / 0x68), Help and the line / character count tooltip (FUN_1805c0760, mccoreres 27901-27904) are greyed.
2. Empty text with mode != 0: the leader command FUN_18003caa0 (or the witness command when the note arrow style is 3) - NOT PORTED.
3. The function panel: DraftingSmartDimensionsManager with every control disabled (FUN_180258d60 clears the enable flags, FUN_180257bc0
   disables each, cad_dimcmd.c 933-967).
4. Mode 0 loop (cad_rundraft.c 1163-1193; asm 0x18003d910-0x18003da00):
   * FUN_1800389a0 (cad_note.c 67-104): `get_event("*", ev, 0x100)` - mode bit 0x100 makes the mouse-move handler FUN_1804e5280 return a
     drag event (SetupDragEvent, decomp/draft/ev_handlers.c 866-985), so **the note is created at the first mouse move**:
     UndoTransStart, `DfCreateLabel(text, cursor, gr_back_color, 0, NULL, 0)` (core_draft_rec2.c 184-298): df_calc_sketched_point ->
     proj_pt_cp -> world_to_view(active view) -> CalcLabel (core_dimlayout.c 1036-1180: InitNOTE, view, position, kind 3 without leader
     points, the text into the memory table) -> store_ent (store_ent_nodraw with the background colour). A click before any move is no
     event of these flags (FUN_1804e3b90 takes none).
   * FUN_180038c50 -> RunDraftCommand(99) -> the note drag (FUN_1800457f0 copies the entity to drag_ent, its colour from the background
     to main_color): each mouse move (FUN_180045310 -> FUN_180046110, cad_note.c 259-347, 628-655) puts the sketched point (onto the
     construction plane, world_to_view of the note's view) at +0x70 and resets the note to the globals (main_attrib, init_attribs_note,
     df_reset_ldr_arrows - labels only -, main colour / level; constr_view_3d -> ResetLabelDepth, df_adjust_ldrs - labels). Prompt
     FUN_180045a00: mccadres 27709 "Create note: " + 27704 "Indicate text position" (27070, the '~' hint: left out, '~' NOT PORTED).
   * The click: the drag's get_event flags are 0xd (FUN_180045a00: points / sketch, auto-highlight off): an AutoCursor point is a point
     event and the note is stored there (FUN_180045180 event 1 -> FUN_180045eb0); else the sketch event (4) stores it where the drag left
     it (FUN_180045670(1) -> df_save_drag_ent). Then DraftGlobalsFromNote, UndoTransCommit (one undo step); with Multiple Notes
     (stay_in_create_note) the loop waits for the next first move with the same text, else the command ends.
   * Esc: before the first move FUN_1800389a0 fails and the command ends; in the drag FUN_180038c50 reports no success,
     UndoTransTerminate (decomp/align/core_undo.c 145-172: a transaction with records is rolled back) drops the note, and the event
     result 2 ends the command.
   * Panel buttons in the drag (decoded 2026-11-05): event 8 -> FUN_180045e70 -> FUN_18012b9f0 (key code - 10000 when 10001..10027, else
     a keyboard key, FUN_18012ba20) -> FUN_180045d00 (asm 0x180045d00-0x180045e0a) -> FUN_18012af20 (cad_linear2.c 244-320): panel id
     **23 -> code 3 = store the note** where the drag left it (FUN_180045180: init_attribs_note, FUN_180046110 of the last point,
     FUN_180045670(1), success), **24 / 25 -> codes 4 / 5 = abandon** (FUN_180045130 -> FUN_180045670(0), no success ->
     UndoTransTerminate); ids 1 / 2 / 3 / 9 / 10 / 11 / 15 / 21 / 22 / 26 / 27 are the common format controls (disabled for notes), 19 /
     20 the label leader Add / Delete (FUN_180045d00: DAT_180693004 / FUN_1800461a0). Which FunctionPanel button (OK / OK-New / Cancel)
     posts 23 / 24 / 25 is NOT DECODED (create.md 2.5: it lives in the managed view model) - so the app greys the panel's buttons for the
     whole command (user decision NT-2; cui_geo.c geo_panel_enabled) and ignores a button code that still arrives (dn_button).
   * Not decoded: the drag's keyboard keys (FUN_18012ba20 / FUN_180046070), DfRemoveMTID's work in DfCreateLabel / FUN_180045670.
   * The AutoCursor during the drag (decoded 2026-11-05): the drag's flags 0xd hold the point bit 1; the mouse-move handler
     FUN_1804e5280 (ev_handlers.c 866-985) calls FUN_1804e55b0 (dumped to decomp/draft/ev_move.c 97-277), which sets DAT_1813f5f4e = mode
     & 1 and runs FUN_1804df190, the fast-cursor search (use_fast_cursor && DAT_1813f5f4e, decomp/mccore_autocursor2.c 88): the
     AutoCursor and its marker are live while the note follows the raw sketched point; the click then takes the AutoCursor point.
5. The app: geo_cmd G_NOTE through the dimension loop's entry points (cui_draft.c dr_* -> cui_draft_note.c dn_*); every mouse move of the
   drag moves the note to the sketched point and runs the live AutoCursor (dn_need 's', cui_geo.c geo_cursor_update); the click snaps.

## 6. Project file (ours)
`NOTE HEX(record) HEX(text)` for a note made here, written among the GEO lines in entity order (cui_doc.c; a note made and deleted keeps
its place with the `GEO 0` placeholder line of any deleted new entity); a deleted part note is a `DEL idn` line. dn_read_line re-adds it
with mcam_note_add (a new memory-table id).

## 7. The creation API (viewer/draft_note.h)

```c
DnGlobals g; dn_globals_config(&g, mcam_is_metric(&F));   /* the user's note settings (section 4); dn_globals_init = no config */
g.height = h; g.angle = a;                                 /* radians */
g.h_aln = 1; g.v_aln = 2;                                  /* 0 Left 1 Center 2 Right / 0 Top 1 Cap 2 Half 3 Base 4 Bottom */
g.mirror = m;                                              /* fa +0x5a bits 2-3 (text.md: drawing of mirrored text NOT PORTED) */
g.color = colour; g.level = level;                         /* main_color / main_level at creation */
int e = mcam_note_create(&F, &g, 1, world_xyz, "P1");      /* view 1..8 (reserved views); the Ent index, -1 = empty text / bad view */
```
* `mcam_note_create` = calc_note (InitNOTE + view + position in view coordinates + text) + `mcam_note_add` (the text as a memory-table
  entry with a new id, the record, an E_NOTE Ent marked created). Lower level: `dn_calc_note` / `dn_init_note` / `dn_attribs_note` on a
  record image, then `mcam_note_add(F, E, text)`.
* The caller pushes the undo step (core_ui: `URec { U_ENTS_ADD, ents = { e }, types = { E_NOTE } }`) and redraws the notes
  (`scene_notes_rebuild()`); the save writes the note (section 3.3) and the project keeps it (section 6).
* Geometry Nesting (formats/wireframe.md "Geometry Nesting"): its labels set text height (param_12, else DAT_180137908, else 0.25),
  angle a and mirrored justification bits through calc_note (NE "CPart::XformEnt"): the fields above. Script step for tests:
  `noteapi:X,Y,H,ANG,HALN,VALN,COLOR,LEVEL|TEXT`.

## 8. Rendering (existing, draft.c)
draw_note (draw.md 1.3 / 3.3): the stroke fonts fontN.fnt of the user's install, multi-line text, h / v justification, slant,
character rotation, proportional spacing; leaders and arrowheads (draw.md 2 / 4.6). NOT PORTED there: TrueType fonts, text paths other
than Right, mirrored text, the text box / underline bits, the alternate font switches. Checked on the corpus (CREATE_LINES.mcam,
FIXTURE PLATE.mcam: screenshots in the scratch folder of 2026-11-05); no Mastercam screenshot of these parts in the repository.

## 9. Tests
`tools/tests/note_compare.sh OUTDIR` (note_scenes.txt; golden tools/tests/golden/note): load (13 notes / 6 leaders, 2 labels), pick +
delete + undo / redo + save (record and entry gone), pick of a label, Single Note (dialog, first move, drag, click, undo / redo, save:
record after the table, the entry appended), Multiple Notes + Esc, Cancel / Esc before the first move, a note added to and one deleted
from a part (the table rebuilt, the other entries byte-identical), the project file, the API, Home colour / width / style on a note,
the greyed panel (geodone / geoapply ignored) with the live AutoCursor during the drag (note_panel_greyed). Expected 0/18 differ.
Script steps (cui_draft_note.c): `notedlg:RADIO|TEXT` / `notedlg:cancel` (the dialog's answer; `\n` = a line break), `notemove:X,Y` (a mouse move through
cui_geo.c geo_cursor_update; prints the AutoCursor point),
`notedump`, `noteapi:...`.

## 10. Open
* Labels and leader-only notes (CalcLabel's leader branch, add_a_leader, df_adjust_ldrs, ResetLabelDepth; FUN_18003caa0 / FUN_1800408f0).
* Editing an existing note (the dialog with DAT_18142cb14 = 1, the drag-edit of a picked note FUN_1800457f0, Multi-Edit).
* The window / polygon test of drafting entities; Transform / Copy / Paste of notes (xform of drafting, merge_db2).
