# Drafting (dimensions, notes, leaders) - status

Specs (2026-10-26, from mccore decompiles in decomp/draft/): `records.md` (the D_ID / N_ID / W_ID / hatch records and the memory
table), `draw.md` (GetDraftWireframe: the stored witness / leader records replayed, arrowheads, text placement, the value format),
`text.md` (the stroke fonts fontN.fnt, font_attr, layout). Readers: `tools/draft_dump.py` (records), `tools/mcfont_dump.py` (fonts).

## Phase 1 - display (DONE 2026-10-26, branch ncidrill)
- `viewer/draft.{h,c}`: reads the drafting records + memory table of a part and draws them as draw_dimension / draw_note do (the
  stored layout; nothing is recomputed): witness / leader polylines and arcs (kinds 0 / 1 / 2, the arrow-end shortening, fold-backs),
  the 11 arrowhead types, the dimension value text + tolerance lines, note text (multi-line), leader chains (wl +0x84). Text: the
  stroke fonts read from the user's install (`setup_from_install.py` copies font0..7.fnt to <config>/fonts; paths.conf `fonts`
  overrides). `%v` texts: calc_dim_note -> FUN_18030f410 without tolerances (decimal / scientific numbers, decimal degrees / radians
  / grads; R / Ø / D / Dia. prefixes; leading / trailing zeros; the CRT's half-away rounding).
- `viewer/scene.c` draft_build / draft_draw: drawn with the geometry (show_geom) in the entity colour; filled arrowheads filled
  (ours: Mastercam fills them through OpenGL).
- Tests: `tools/tests/draft_svg PART OUT.svg FONTDIR` (SVG of the part + its drafting), `draft_svg PART --values` (the value oracle).
  Corpus (625 files with drafting): 1696 dimensions, 1700 notes / labels, 194 leaders drawn, 184 entities skipped (below), no crash.
  Value oracle: 1179 / 1567 stored plain-value texts reproduced; the rest are 246 trailing-zero cases (E[0x7cf] = 0 texts made under
  another session setting: with E[0x7cf] = 1 the zeros are kept in 469 / 469), 113 texts edited by the user, 29 hand-typed values.
  Screenshots: screenshots/2026-10-26_drafting_display_*.png (local).
- NOT PORTED (skipped and counted, `draft_msg`): views other than the reserved 1..8 (the part's view table), TrueType text,
  text paths Left / Up / Down / arc and mirrored text, the text box / underline bits (text drawn without them), the alternate font
  (0x0E / 0x0F), Bezier glyph commands (unused by the shipped fonts), number formats
  engineering / fractional / architectural and degrees-minutes-seconds, tolerance strings rebuilt from %v, cross-hatch (3 in the corpus).
- Not checked against Mastercam's screen: no screenshot of a corpus part in Mastercam yet (arrow fill, text weight).

## Phase 2a - Horizontal / Vertical / Parallel in the app (2026-10-26, branch drafting)
- The ribbon's Drafting > Dimension > Horizontal, Horizontal > Vertical and Parallel run Mastercam's dimension loop (create.md 2, the
  app side in create.md 7): AutoCursor / line / free-point picks, the placement drag (calc_linear_dimension port, draft_create.c),
  accept, the live entity, OK / OK-New / Cancel / Esc, Undo / Redo; a function panel (DraftingSmartDimensionsManager's controls, the
  decoded ones live: Mode Horizontal / Vertical, Arrows Inside / Outside, Centered text, Decimal places; the rest greyed).
  Code: viewer/core_ui/cui_draft.c (in the cui_geo.c command frame, G_DIMH / G_DIMV / G_DIMP), the empty record dc_init_dim (InitDIM /
  FUN_180249820 / InitDimWL / InitDT / InitDimFA / InitDM), session records drawn by scene.c (scene_draft_session), saved as D_ID
  records by mcam_saveops.c (the picked lines' eptrs), kept in the project file (DIM lines, ours). Every other Drafting button stays
  greyed. The globals come from the user's config (create.md 5).
- Tests: tools/tests/draft_ui_test.sh (self-checking): the app flow (3 dimensions, values / orientations / aPoint kinds, Undo / Redo,
  export + reopen = the session's records, the exported part through the corpus oracle: 3 / 3 fully equal, draft_dump --check clean,
  project reopen) and corpus dimensions recreated through the click path (draft_ui_corpus.py; numbers in create.md 7.5);
  draft_create_test.sh `-f` = the oracle from dc_init_dim instead of the stored record (533 / 706, the same as the regeneration).

## Phase 2b - the Ordinate group (2026-10-27, branch draftord)
- Ordinate > Horizontal / Vertical / Parallel live (create.md 8): DraftApp::doOrdinate's loop (zero ordinate, children, the parallel
  orientation point), each new ordinate stored and its text dragged (the ordinate drag state), Esc / panel buttons, Undo / Redo, saved
  with the base links (+0x108), kept in the project. The record: calc_ordinate_dimension port (viewer/draft_ord.c; draw.md 4.5).
  Code: viewer/core_ui/cui_draft_ord.c, viewer/draft_ord.{h,c}; display = the existing replay (draft.c).
- Tests: tools/tests/draft_ord_test.sh (oracle 102 / 102 2017-2018 records), tools/tests/draft_ord_ui_test.sh (app flow + 90 corpus
  ordinates through the click path: 24 EQUAL, 66 EQUAL-LAYOUT, 0 DIFFER).
- Pass 2: the pick of existing drafting (viewer/draft_pick.c, select_ent_univ's drafting cases; core_ui cui_draft_pick.c), so the base
  pick in every ordinate button and **Add To Existing** are live; **Align** live (df_load_oc / FUN_1802423f0 / df_adjust_oc /
  move_ordinate_dimension ported; part records rewritten through session replacements, U_DRAFTEDIT undo). Greyed: **Window** (dialog 2533 +
  its set builders not decoded). Not ported: the Mode change event, the '~' pick (decoded, no key state), the oc of a plain drag
  (panel id 17). create.md 9.
## Phase 2b - Circular in the app; Angular's calc ported (2026-10-27, branch draftdim)
- Drafting > Dimension > **Circular** live (create.md 8): the arc pick, the placement drag with determine_cl (radial / quadrant style), the
  leader-mode bookkeeping, accept, the panel (Leaders, Centered text, Decimal places, Lock, Arc Symbol Radius / Diameter). Code:
  viewer/draft_create_circ.c (calc_circular_dimension port), viewer/core_ui/cui_draft_dim.c (the command; dispatched from cui_draft.c),
  viewer/draft_create_int.h (the linear port's helpers shared), mcam_saveops.c (the centre aPoint's eptr). Oracle tools/tests/
  draft_circ_test.sh: 132 / 164 2017-written circular records fully equal (create_port.md 4); draft_ui_test.sh C: the app flow + 18 / 22
  corpus records recreated EQUAL through the click path.
- **Angular**: calc_ang_dimension and its layout (pigtail / arc-split table) ported in viewer/draft_create_ang.c, oracle
  tools/tests/draft_ang_test.sh 85 / 144 (create_port.md 5); the button flow decoded (create_port.md 5.3) but not wired - greyed.
- Still greyed (create.md 9): Smart Dimension, Point, Angular, Perpendicular, Baseline, Chained, Tangent.

## Phase 2c - Angular, Baseline, Chained in the app (2026-10-27, pass 2)
- Angular live (create.md 10, viewer/core_ui/cui_draft_ang.c): two lines, a line + a point + a typed angle (get_angle -> RequestRealInputModal
  -> cui_hooks.ask_real), three points, an arc; draft_ui_test.sh D: 4 / 4 exported records regenerate fully equal, corpus click path 29 / 38.
- Baseline / Chained live (create.md 11, cui_draft_base.c): BaselineChainProc ported over calc_linear_dimension; baseline without
  increments (config 15000 = n) refused (its drag-edit end NOT DECODED); the base pick waits for the Ordinate branch's draft_pick hit-test
  (tests: basepick). draft_ui_test.sh E: 5 / 5 records regenerate fully equal.
- Still greyed: Smart Dimension, Point, Perpendicular, Tangent (create.md 9).

## Phase 2d - Tangent, Perpendicular, Point (2026-10-27, pass 3)
- Tangent / Perpendicular live (create.md 12; viewer/draft_create_tan.c, cui_draft_tan.c; oracle tools/tests/draft_tan_test.sh: the 8
  readable corpus tangent records equal, 4 of them with trailing zeroes on); Point live (create.md 13; draft_create_pt.c, cui_draft_pt.c,
  draft.c now draws point dimensions; tools/tests/draft_pt_test.sh). Baseline without increments: the drag-edit of the new dimension
  ported (create.md 11). Only Smart Dimension is still greyed in the Dimension group.

## Notes (2026-11-05, notes.md)
- Part notes / labels / leaders are entities (E_NOTE): read with their memory-table entries, drawn from them, picked / selected /
  deleted / undone / recoloured, saved back byte for byte (deleted ones removed with their entries). Drafting > Note: Single / Multiple
  Notes (dialog 452, the note made at the first mouse move, dragged, stored by the click). The creation API `viewer/draft_note.h`
  (`mcam_note_create`) for other commands (Geometry Nesting's labels). Labels / leader creation, editing a note: not ported.
  Tests tools/tests/note_compare.sh.

## Next
- Phase 2: the Drafting tab commands (Smart, Horizontal / Vertical / Parallel, Circular, Angular, Note, Leader, Witness; then
  Ordinate, Baseline / Chained, Point, Tangent, Perpendicular, Cross Hatch) - creation geometry is in draw.md 4 (linear mostly decoded;
  angular / radial / leader attachment in outline; ordinate decoded (4.5); point / baseline / chain NOT DECODED), the drafting globals (records.md 6).
- Phase 3: save drafting entities into .mcam (unique eptr ids, the memory table), Regenerate (associative points), Multi-Edit.
