# Mastercam UI (2017)

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

## Command index — `formats/ui_commands.tsv` (`tools/ft_index.py`)
All 19 function-table files (`*.ft`) parsed: **787 UI commands** -> category, name, DLL, entry point, icon/tooltip
resource ids. 385 entry points verified against DLL exports (35 not found by plain name, the rest are EXE/.NET).
Examples: "Dynamic Area" (the 2017 UI name for Dynamic Mill) = `MCMILL.DLL!hmm_2d_DynamicArea`; Dynamic Contour, Area,
Peel, Blend = `hmm_2d_*`; 3D HST (OptiCore, Core Rough, Waterline, Scallop, Raster, Hybrid, ...) = `MCMSURF.DLL!msurf_hmm_*`.
Key bindings: `mastercam.kmp` (`KEYMAP Mastercam <command> "<keys>"`).

## Toolpath dialogs -> operation fields — `formats/ui_fields_mcmill.tsv` (`tools/ui_map.py`)
Chain: dialog control id -> label (mcmillres RT_DIALOG, `tools/pe_dialogs.py` + `dialog_labels.py`) -> page member
(`DoDataExchange`: `DDX_ProcessEditReal` for MCReal edits, MFC DDX ordinals 3547 DDX_Check (binds checkboxes) / 3548 DDX_Control (binds UICombobox/Static objects; no value) / 3627 DDX_Radio (bound to the first radio of a group). Corrected 2026-10-13 from the control types: earlier output had these three mislabelled)
-> operation entity offset (load/save copies `page.member <-> *(page.opptr) + A`, pointer delta calibrated against the
MP-parameter offsets) -> MP parameter number(s) (`param@case`; the case depends on the opcode, 97 = 2D High Speed).

mcmill: 140 DDX pages, 1436 bound controls, 463 mapped to operation offsets; 175 dialogs with captions
(`exports/mcmillres_dialogs.tsv`). Fully mapped HST pages: 3205 "Dynamic Mill Cut Parameters", 3219 "Dynamic Pocketing
Entry Motion" (see `formats/dynamic.md`). Checkbox/radio pages that copy whole structs (e.g. 1337 "HST 2D Rest Material")
and custom pages without DDX (3299 "Dynamic Mill Contour Parameters") are not mapped by this method.
Combos bound by DDX_Control (kind `control`) are read and written through UICombobox virtual calls, so they are not mapped
automatically. Mapped by hand: page 3205 combo 3202 "Motion > Gap size, retract" → entity 0x16b9 (see dynamic.md).
Radio 3203/3204 (gap size Distance / % of tool diameter) → page int 0x13558.

## mclathe (2026-10-14)
- Full decompile `decomp/mclathe_all.c` (17,829 functions); dialogs `exports/mclatheres_dialogs.tsv` (160 dialogs);
  mapping `formats/ui_fields_mclathe.tsv`: 38 DDX pages, 419 controls, 40 mapped.
- Lathe pages do not copy page members to the op with `*(T*)(param_1+S) = *(T*)(op+A)`. Each MCReal is initialised
  by a virtual call: `(*(vtbl(param_1+M) + 0x348))(param_1+M, *(cur_op_exref + X), ...)`, where `cur_op_exref` is
  the global current operation (= op entity + 0x68). `ui_map.py` step 5 handles this.
- **Lathe Dynamic Rough cut page** = `CDynamicRoughCutPg`, dialog **6497 (0x1961)** "Cut Parameters"
  (page caption string "Dynamic rough parameters"):

| control | label | entity | engine use (`InitDynPockParameters`) |
|---|---|---|---|
| 6492 | Stepover [%] | 0x1238 | |
| 6491 | Stepover [abs] | 0x1230 | stepover distance → engagement acos(1 − s/R) |
| 6493 | Toolpath radius [%] | 0x1248 | |
| 1712 | Toolpath radius [abs] | 0x1240 | min toolpath radius (+0x40) |
| 818 / 819 | Stock to leave in X / Z | 0x1250 / 0x1258 | |
| 1048 | Linearization tolerance | 0x1260 | |
| 231/232 | Cutting method One-way / Zig-zag | | |
| 5387 | Rough Direction (combo) | | short 0x1228 = cut direction |
| 852 / 846 / 908 | Compensation type / direction / Roll cutter around corners | | |
| 1381 | Stock Recognition (combo) | | |

## Label pairing (`dialog_labels.py`, improved 2026-10-14)
- Combobox height is capped at 14, since the resource height includes the drop-down list.
- A left label must end within 30 units of the control.
- Stacked layouts take the label (possibly two lines) directly above the control.
- Paired edits around a unit static are tagged `[%]` (before the unit) and `[abs]` (after it). Other second controls
  in a row take the row label plus `[2]`.
- Otherwise the label falls back to the enclosing group-box caption.

## Keyboard view control (2026-10-14)
- Arrow keys are not in `mastercam.kmp`; the graphics window handles them in code. The step sizes are
  configuration items in `mcamxm.config`: **4026 Zoom Factor 0.0625, 4027 Unzoom Factor 0.0625, 4028 Pan Factor
  0.0625, 4029 Rotate Factor 5.0** (degrees). A press pans or zooms by that fraction of the screen, or rotates 5°.
- "Screen → View settings" page (mccoreres dialog 2558, handlers in mccore `FUN_1801a8960` / `FUN_1801b6200`, items
  0x80e9-0x80f2): max entities for dynamic rotation, free orbit, view snapping + snap tolerance, spaceball
  attraction, view / fit animation + delay, **useShfToSpin** (exported from mastercam.dll; Shift to spin),
  ReverseMouseWheel.
- `mastercam.dll` (the real main module; `mastercam.exe` is an 89 KB launcher) is now in the Ghidra project `mc2017`.
  `OnScreenPan` / `OnViewRotate` / `OnScreenZoom` are its view commands (`decomp/mastercam_viewcmds.c`). The WM_KEYDOWN
  message-map handlers found are edit / list controls, not the graphics window (`decomp/mastercam_keydown.c`).
- mcamui follows this: one step per press using these factors (read from the user's config when present), and a held
  key repeats smoothly at 30 steps/s after 0.25 s.

## Grid (mastercam.dll, decompiled 2026-10-14: `decomp/mastercam_grid.c`)
Settings struct `sel_grid` (exported, 0x1809a0430):

| offset | field | default (`set_default_grid(metric, colour)`) | config item (mcamxm.config) |
|---|---|---|---|
| +0x00 | spacing X (double) | 5 mm / 0.25 in | 23003 |
| +0x08 | spacing Y | 5 / 0.25 | 23004 |
| +0x10 | origin X (construction plane) | 0 | 23005 |
| +0x18 | origin Y | 0 | 23006 |
| +0x20 | size (total width) | 500 / 25 | 23007 |
| +0x28 | show (char) | off | (ShowGridOn/Off, OnToggleShowGrid) |
| +0x29 | snap enabled | off | (SnapToGridOn/Off, OnToggleSnapToGrid) |
| +0x2a | colour index | 8 | 5032 Grid color |
| +0x2b | snap mode: 1 Near, 2 Always | 2 | 23002 Snap |

- **Drawing** (`display_sel_grid`): nx = (short)(size/2 / dx), ny = (short)(size/2 / dy). The centre is the origin at the
  construction depth, mapped from the active construction plane (`view_to_world`, then `AddActiveOrigin`); the axes
  are the plane's X/Y scaled by the spacing. Six lines are drawn in GL style 2 (dashed), width 1: the X line through
  the origin, the bottom and top edges, the Y line through the origin, and the left and right edges. Then
  `glBegin(GL_POINTS)` puts one point at every node (2nx+1) x (2ny+1), in every viewport unless "active view only".
  Grid colour, or the background colour to erase.
- **Snapping** (`sel_grid_search`): the cursor is mapped to 3D (`map_cursor_to_3d`), then into plane coordinates. The
  offset from the origin is rounded to the nearest multiple of the spacing; a remainder of exactly half rounds away
  from zero. The node is projected back to the screen. It is used when it is within half the selection distance
  (`sel_dist`) of the cursor, or always when snap mode = 2. The grid size does not limit snapping.
- Settings dialog: mccoreres 1589 "Configuration - Screen Grid" (Spacing X/Y, Origin X/Y + Select, Snap Near/Always,
  Size).
- mcamui draws and snaps the same way (construction plane Top, depth 0), and reads 23002-23007 / 5032 from the user's
  config. Status bar: GRID / SNAP toggles and Grid... (2026-10-16: opens only the 1589 page "Configuration - Screen
  Grid" - Spacing X / Y 25213 / 25217, Origin X / Y 25211 / 25215 + Select 25203 greyed (picking NOT PORTED), Snap Near /
  Always 25208 / 25206, Size 25204; the rest of the Configuration dialog NOT PORTED; the earlier invented "Grid
  Settings" window with style / height / colour / presets is gone, `grid.style` / `at_floor` stay at their defaults,
  mcamui.c `draw_gridset`); the user's keymap (`CONFIG/mastercam.kmp`) binds **F9 = OnDisplayInfo** (info + axis lines through the origin), **Alt+F9 = OnWCSAxesToggle**, **Alt+G = OnScreenGrid** (grid on / off; off at start). Screen colours come from the config (`mcamxm.config` [COLORS]: 5007 gradient start = 111, 5009 end = 15, 5001 system origin, 5032 grid) through Mastercam's 256-colour palette (mastercam.dll @0x8a6440, 4-byte RGB entries; `formats/mastercam_palette.tsv`).

## File > New (mastercam.dll `init_db` @0x18033ae30, decompiled 2026-10-14: `decomp/mastercam_filenew.c`)
File > New is MFC command 4019 → `OnNewDocument` → `init_db`. It does the following:
1. `SaveDbIfChanged` (prompt).
2. `dbinit` (empties the database), then erases the graphics.
3. Viewports go to their default views (`vp_views_dflt`) and the graphics view to view 1, Top.
4. Active level 1, mask 0, `resetLevels`. Main attributes: width 1.0, style 1.
5. Construction depth 0, saved as the "previous" view/depth.
6. `init_assoc_globals`. With param_1, the product / machine defaults are reloaded (`CSystemProductFile::InitDefaults`,
   `ReloadConfiguration`).
7. File name empty, suffix ".mcam" (".emcam" for HLE), title cleared.
8. `OMinitOpManTree`, `BuildReservedPlanes`, `set_db_unchanged`.

The new part has no geometry, no operations and no machine group until one is chosen (config 25000 "Mill Machine
Definition" = Mill Default mm). New operations take their settings from the operation defaults library (config 2303:
`shared Mcam2017/Mill/Ops/Mill_MM.mcam-defaults`, a part-format file with one default op per type, including the five
2D High Speed styles at op +0x1228).

mcamui follows this:
- File > New gives an empty part, Top view, level 1, depth 0.
- Wireframe commands draw geometry (42 commands, formats/wireframe.md "Code"), plus Delete.
- The project stores it (`NEWPART`, `GEO`, `DEL` lines).
- Save as Mastercam part (2026-10-16, mcam_saveops.c; formats/improvisation_review.md EXPORT item 3) builds the file
  from Mastercam's operation defaults library (config 2303, by units; its machine group is config 25000's machine)
  emptied: geometry, solids, OP / TP / TL / NCI records, the ops' holder records and NCI dropped, TlMgr archive emptied.
  Then the geometry records (line 0x98, arc 0xd0, point 0x80, built from the decoded layout) and the operations are
  added. The user's other parts are no longer used as a base. NOT DECODED: thumbnail / description / history records
  of a new part (the library's are kept).

## File tab Info page (ribbonui.dll, decoded 2026-10-15 from disassembly)
- DDX (0x18006bbb0): Name 1171 = member +0x6920, Type 1172 +0x6b00, Folder Path 1177 +0x6ce0, Size 1170 +0x6ec0,
  Units 1174 +0x70a0, Descriptor edit 1173 +0x7280, Last Modified 1175 +0x7d18, Created 1176 +0x7ef8.
- Update routine 0x18006cda0: Name / Folder Path / Size start as ribbonuires 193 "Not saved yet". A host callback
  (global object 0x18012f510, vtable +0x30) fills {bool saved; CString name, type, folder; u32 size; u8 metric;
  CString descriptor; time modified; time created}. Saved: Name / Folder / Size (FUN_18006d480) / Descriptor /
  Last Modified. Not saved but named: Name / Folder / Size = 201 "Not saved in current version". Always: Type, Units =
  195 "Inch" / 196 "Metric", Created. Descriptor cue banner = 194 "Add descriptor" (saved) else 193. Name, Folder,
  Size, Descriptor, Modified, Created enabled only when saved; Type and Units always.
- Size (FUN_18006d480): < 0x400 -> 198 "%1!d! KB" with 1; < 1 MB -> KB = size >> 10; < 1 GB -> 199 MB (>> 20); else
  200 GB (>> 30). Dates (FUN_18006dba0): localtime + strftime(buf, 0x80, 197 "%a %b %d %H:%M:%S %Y").
- Host callback = mastercam.dll BackstagePageInfoManager (vtable 1805c1d80) slot 6 FUN_1800fa5b0, decoded 2026-10-15.
  CFileFind (MFC140 ordinals, sorted-name order: 491 ctor, 4507 FindFile, 4524 FindNextFile, 5270 GetFileName,
  6101 GetRoot, 5560 GetLength, 5547 GetLastWriteTime, 5026 GetCreationTime, 1117 dtor) on get_geo_filename.
  Found: saved = 1, name = GetFileName, type = FUN_1800fa440 = SHGetFileInfoA(path, 0, .., 0x410 TYPENAME |
  USEFILEATTRIBUTES).szTypeName (Windows shell association; empty path -> default_geo_filename + ".mcam", or
  ".emcam" when GetProdEnableBits & 1 or user type 5 / 1), folder = GetRoot, size = GetLength, metric = IsMetric,
  descriptor = FUN_1800fa390 (get_descr_info; copied when its pointer field is non-zero), modified = GetLastWriteTime,
  created = GetCreationTime. Not found: saved = 0, name = file name unless == default_geo_filename (then ""), type as
  above, folder "", size 0, modified 0, created = CMastercamInterface +0x8d8.
  apprenticeCAM: name / folder / size / units / dates done; type (no Windows shell), descriptor (get_descr_info record
  not read) and the unsaved created time (+0x8d8 meaning) NOT DECODED -> blank.

## UI chrome in mcamui (2026-10-16, formats/improvisation_review.md UI items 13, 14, 17, 20)
- **Toolpath galleries** (2026-10-19): the palette dropdown matches the original (ribbonuires.dll id 53925 & co:
  ENABLE_LABELS TRUE = the item name centred below the 64 x 32 icon, INITIAL_COLUMNS columns, borderless, opens under
  the expand arrow, closes on pick / Esc / outside click). Item tooltips show the name plus the description from
  `<config dir>/gallery_tips.tsv` (Mastercam text, not shipped; written by setup_from_install.py from the user's
  install, tools/gen_gallery_tips.py). Repeated item names within a gallery key icons / tips as `gallery|item#2`
  (2D Mill Drill x 2, 3D Mill Project x 2, lathe Rough / Finish / Groove). The 320 px tooltip wrap and the on-screen
  clamping of the palette are ours.
- **Commands not ported** (ribbon, menus, gallery, QAT, status-bar display buttons, keyboard): drawn greyed and do
  nothing (`run_local_x(t, 1)` probe); the old "Command" reverse-engineering window and the "<cmd>: not available in
  this program" / "%s is not available here" messages are gone. Wireframe names only match MCCAD*.DLL commands.
- **File dialogs** (mcamui.c `draw_file_dialog`, `FDT_*`; decomp/mastercam_filedlg.c): titles mccoreres 7283 "Open",
  7284 "Save As" (others 25549, 992, 653, 25550); Open filters (mccore OpenFile / FUN_1803552e0) 2860 "Mastercam Files
  (*.mcam)", 2875 "Mastercam X Files (*.mcx*)", 2857 "All Mastercam Files (*.mc*)" (default, remembered like
  DAT_1812dcd98), 2858 "All Files (*.*)"; Save As (SaveFile / FUN_180355810) 33207 "Mastercam 2017 Files (*.mcam)" +
  "All Files (*.*)"; tool libraries "TOOLDB Files (*.tooldb)" (PromptForFileToOpen "<EXT> Files" rule). Ours
  (intentional): "apprenticeCAM Project (*.mcamui)"; the window is a host file browser (path field, Up / Go, list
  with sizes). NOT DECODED / not reproduced: the Windows common dialog, the Import / Export filter lists
  (FUN_18034c200), the initial folder GetDefaultPath(".mcx") (the part's folder is used).
- **Unsaved changes prompt**: mccore save_on_exit (decomp/mccore_save_on_exit.c), mccoreres 7291 verbatim with
  "Mastercam" replaced by the program name; caption 5175 -> program name (intentional, as the title bar).
- **Keyboard window** (OnHelp, Alt+H): lists the user's keymap with each command's FT resource label (`cmd_label`).
  INTENTIONAL: Mastercam's OnHelp opens its CHM help, which is not ported.
- **Messages**: decoded ones use Mastercam's strings (e.g. opmanres 3964 "No operations were selected.  Please select
  at least one operation." for Backplot with no selection; duplicate tool number = TpToolList::WarnOfDuplicateToolNumbers
  title 5583, decomp/mccore_msg17.c); messages kept as this program's own are listed in formats/provenance_audit.md
  "Messages" (project / file errors, LinuxCNC post results, op generation failures, ...). Message box captions are the
  program name.

## Selection and AutoCursor in mcamui (2026-10-19)
(2026-10-21, Qt port phase 2: the code named below moved out of mcamui.c into `viewer/core_ui/` with the same names -
`pick_entity` / `pick_point_under` / selection / auto-highlight rule in cui_sel.c, `geo_snap` / `geo_ang_snap` /
Temporary Midpoints in cui_snap.c, `build_overlay` / `ov_entity` / stipples / `ov_fast_marker` (draw_fast_marker) in
cui_overlay.c - and both UIs call it; formats/qt_port_plan.md "Phase 2". Behaviour unchanged.)
Decoded from mastercam.dll / mccore.dll (decomp/mastercam_dlsearch.c, mccore_autocursor.c, mccore_autocursor2.c;
new dumps: decomp/mastercam_seluniv.c, mastercam_selpre.c, mastercam_acdlg.c (draw_fast_marker, doAutocursorDlg,
set_gl_style), mastercam_acok.c, mastercam_acinit.c, mastercam_acpk.c, mastercam_acbar.c, mastercam_accursor.c,
mastercam_acrclick.c, mastercam_acsave.c, mastercam_hlpaint.c, mastercam_hldraw.c, mastercam_hilite.c, mccore_dlg292.c,
mccore_wfhighlight.c, and for the Linking page pick mccore_tp_select_depth.c + mccore_tpsd_helpers.c +
mccore_getevent.c; the second round: decomp/mastercam_dlsearch_tpn.c (dl_search_fast_tpn + the tangent /
perpendicular helpers FUN_180131c50 / FUN_180131620), mccore_inters.c (intersect_ents, dataentr_inters, dataentr_ep),
mccore_tempmid.c + mccore_tempmid_at.c (Temporary Center Point / Temporary Midpoints)):
- **Pick rule (click selection)** = select_ent_univ (mastercam.dll 0x18020f4a0): aperture = the full sel_dist
  (view height / 30 at 522 px = 17.4 px, `SEL_APERTURE_PX`); the winner is the strictly nearest candidate, exact ties
  go to the first entity in display-list order; there is NO depth tie-break and NO click-to-cycle through coincident
  entities (nothing of the sort exists in the code). mcamui.c (now core_ui/cui_sel.c) `pick_entity` already implements exactly this (nearest
  2D screen distance, entity order = draw order). AutoCursor's own search (dl_search_fast) uses sel_dist * 0.5.
- **Auto-highlight (the dotted / thick hover line)**: on mouse moves mccore FUN_1804df930 -> mastercam.dll
  DrawAutoHighlight (0x18031f290) redraws the entity under the cursor in the auto-highlight colour (config 5000,
  default 15) with the wireframe line width raised to at least WfHighlightWidth (default 2.0, config 8033, clamp
  [1, 5]); dashed-style entities get a two-pass dash / inverse-dash (config 4056). It runs while an entity-selection
  prompt is active or for general pre-selection (FUN_1804e11e0 / TurnOnAutoHighlight; config 4036 and 4001, factory
  defaults both ON, .data initializers), and is off during point entry / geometry creation. mcamui.c:
  `autohl_active` (chaining, entity picks, idle), `hover_ent` tracked on mouse moves, drawn in `build_overlay`
  (overlay width 2.5 covers the 2.0 bump; a selected / chained entity keeps the select colour - ours).
- **The striped highlight** (2026-10-20; draw_ent_nolist 0x180331350, decomp/mastercam_hldraw.c:277-320): a
  highlighted OR selected wireframe entity with a non-solid line style (ent +0x18, loaded as `Ent.ln_style`; 1 =
  solid) and config 4056 "Use dashed on wireframe?" on is drawn in two passes: the entity's own stipple pattern in
  the highlight / select colour over the INVERSE pattern in the contrast colour (white 15, or black 0 when the
  colour's luma 0.30R + 0.59G + 0.11B > 0x7f; style 5: 0xfa on both passes). 4056 off = one pass, the highlight
  colour with the entity's own pattern. The stipple patterns come from set_gl_style (0x180226bc0): 2 = 0x3333,
  3 = 0x33ff, 4 = 0x333f, 5 = 0xffff, 6 = 0xaffa, 7 = 0xaaaa; the inverse bytes map to the 0xf8-0xfe specials (the
  exact bit-inverses). The pattern runs continuously along the entity like GL_LINE_STIPPLE in one begin; the bit
  factor (2026-10-20 decoded): (int)(x_pt_size / 2.9 + 0.5) px per bit, x_pt_size = the graphics viewport's pixel
  width / 90 integer-divided (view +0x1098, written by FUN_1801ceb50 / FUN_1803260f0 = resize / fit; the
  GetDeviceCaps ASPECTX value FUN_1801c0eb0 writes there first in FUN_1801d0a70 is overwritten; the hardcopy
  scale branch FUN_1801c0a50 x DAT_1809a6058 / 100 is not ported - no printing). mcamui: `ov_entity` /
  `ov_entity_pat` / `ln_stipple` / `stipple_px` in build_overlay (selection AND hover); solid entities are
  striped too, by FUN_180135ad0 - see "Stripes on a hovered / selected SOLID wireframe line" below. The BASE drawing honours line styles too
  (2026-10-20): draw_ent_nolist calls set_gl_style(ent +0x18) unconditionally (decomp/mastercam_hldraw.c:255-256),
  so a dashed entity is dashed in the plain view, highlighted or not. Core GL has no GL_LINE_STIPPLE: each polyline
  carries the style (`Poly.ln_style`), scene.c leaves the stippled ones out of the static draw and mcamui bakes
  their dash segments in screen px into the dash buffer (`scene_set_dash`, rebuilt by build_overlay on every view
  change, drawn depth-tested at the geometry width). The factory default of 4056 is ON (2026-10-20: the .data
  initializer of the flag DAT_18088f3e6 is 1 and only SetDashedWireHightlight writes it, from the config binding
  - no config key, no override; the user's config has y).
- **Stripes on a hovered / selected SOLID wireframe line** (2026-10-21; evidence: the user's X9 screenshot
  ~/Pictures/mc-highlite.png - a selected style-1 line = select-yellow dashes over near-black, period 16 px).
  DECODED: the per-entity draw FUN_180135ad0 (mastercam.dll 2017) - used by the auto-highlight (call 0x18031f4fd,
  highlighted flag 1, auto-highlight colour), the selected-list repaint (0x1801d54d1) and the display-list draw
  (0x18032b724, taken when the list node's +0x10 flag is set) - is NOT the draw_ent_nolist branch above:
  - two passes when highlighted / selected AND config 4056 on (GetDashedWireHightlight 0x180320760 =
    DAT_18088f3e6; pass start `neg; sbb; add 2` at 0x180135baa), else one pass, colour + own style;
  - pass 1 = the contrast colour (15, or 0 when luma > 0x7f - same formula), style s if s is 1 or 5
    (`(s - 1) & 0xfffb == 0`, 0x180135c6c), else ~s;
  - pass 2 = the highlight / select colour, ~s if s is 1 or 5 (0x180135c7c), else s.
  So a solid line = a solid contrast line under 0x3333 dashes (~1 = 0xfe -> set_gl_style 0x3333) at the
  set_gl_style factor; hover (colour 15) and select (14) both get black gaps (both lumas > 0x7f), only the dash
  colour changes. Style 5: solid contrast under 0xfa = 0x3333; styles 2-4, 6, 7 unchanged from above (inverse in
  contrast, own pattern in colour). X9 Mastercam.exe gives the same picture: its draw_ent_nolist (0x14027acbd) and
  the callers of its FUN_180135ad0 twin 0x1400fd650 (0x140167608, 0x14026c138) replace a highlighted entity's
  style with the word at 0x1407643c4 (.data init 2, only read) and the width with 0x1407643c8 (2.0); its two-pass
  (s != 1) is pass 1 contrast SOLID, pass 2 colour with that style 2 = 0x3333. mcamui: `ov_entity` follows
  FUN_180135ad0 (selection and hover); screenshots/2026-10-21_highlight_stripes_hover.png / _sel.png.
  Side notes from the search: `StippleHighlight` (data 0x180876679, init 1) is config 4053 "Use stipple on
  solids/surfaces?" (mccore binding 0x1801ba3c9, key 0xfd5) and only drives the GAF face highlight (UtRender
  props 0xb / 0xc stipple index -> glPolygonStipple in gafgr GrOGLPipe 0x10e964eb; masks from InitStippleData
  0x180322020 -> FUN_1801c8440) - decoded 2026-11-18, next item.
- **Shaded face highlight (GAF stipple)** (2026-11-18; dumps decomp/gafhl/: gafgr_stipple.c, gafgr_passes.c, gafgr_um2.c,
  mc_1801c8440.asm, mc_sequence.c / .asm, mc_229d90.asm, mc_dldraw.c, mc_ff40.c, mc_glow.c). A shaded surface is drawn by gafgr's
  GrOGLPipe from its GrRender (the dl node +0xa0, UseGAFSolids = mcgeomsld 0x1803baa7f, .data 1, never written - every
  reference in mastercam.dll reads it).
  - **The look = a stipple list** (GrOGLPipe::StippleData, pipe +0x650 masks / +0x668 lists, SetStippleData 0x10e83400). The
    pass loop (DrawTriangles 0x10e94c50 and the other Draw(GrTri*) / Draw(GrFace)) draws the same triangles once per pass
    while PrepTriangleState(pass, ...) 0x10e959e0 returns true. The list: GetStippleIdx 0x10e926d0 = the lowest set highlight
    flag bit k -> material k+1 (GetMaterialIdx 0x10e927c0: bit 1 = 1 auto-highlighted, bit 2 = 2 selected), its byte
    Material +0x94 = the stipple index of the UtRender prop 0xb + k; -1 or out of range = no list (a plain Materialize with
    the flags: the face lit in that material's colour). FUN_1801dbc80's prop set (mastercam_hlpaint.c:550-590): 0xb =
    auto_highlight_color, stipple `StippleHighlight ? 0 : -1`; 0xc = select_color, `? 1 : -1`; 0xd colour 7, 0xe / 0xf the
    check / drive colours, 0x10 result_color, 0x11 group_color, all -1.
  - **A pass = 6 bytes** r g b a, [4] = mask index (0xff none), [5] flags: 0x100 = the face's own material (the flag bit
    stripped: GetMaterialIdx again), else the entry's rgba; 0x200 = lit (Materialize, glMaterialfv), else a flat glColor4ubv
    with GL_LIGHTING off (bit 8 never set in the state word); 0x400 = glow (Glow type 1 / 2 only). A mask index with the
    pipe's user mask bit 0x20 (+0x254 = the display-list item's mask, GrOGLPipe::Draw 0x10e88190) sets the state bit 0x400 -> PrepOpenGLState 0x10e96380: glPolygonStipple(masks + idx * 0x80) +
    glEnable(GL_POLYGON_STIPPLE). An alpha != 0xff turns blending on (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, 0x10e96418).
    Every pass keeps depth test + depth writes + glPolygonOffset(1, 1) (0x43 & the allowed mask +0x6c0); the scene's depth
    function is GL_LEQUAL (GrOGLPipe::Draw 0x10e88147), so the passes over the same triangles land.
  - **The draw items**: CallPipeOnList FUN_1801bcb90 draws the shaded scene through a GrSequence of display-list items
    (OglFactory::DspListItem(Mode, uint, ushort user mask, ...), built once by FUN_1801c11d0): render_cfg 3 -> the list
    {DAT_18099fec8, fee8, fde8, fe28}, render_cfg 4 -> {DAT_18099fea8, ff08, fe08, fe48}, indexed by the view +0x45a4.
    DAT_18099fea8 = four Mode 0 items with the masks 0xc, 0xa, 0x34, 0x32 (0x1801c5da6..0x1801c61b2). The mask bits
    (Draw(GrFace) 0x10e8a950, DrawTriangles, Draw 0x10e8ad00): 4 = triangles, 2 = lines, 8 = only faces with no
    highlight flag (vtable +0x188 & 3), 0x10 = only flagged faces, 0x20 = stipple allowed. So flagged faces are drawn after
    every other face and line, stippled; the other faces never are.
  - **InitStippleData** (0x180322020, a jump to FUN_1801c8440; called by DoShadeShaded / Outline / Wire / WireDimmed /
    WireHidden) with Glow type 0 (UsingGlowHighlighting 0x180329dc0 = StippleHighlight && DAT_18099fd9c && render_cfg 3 / 4;
    DAT_18099fd9c = registry GUI\Diagnostics "Glow type" 0..2, FUN_1801ce090, .bss 0; the user's hive has no such key):
    - mask 0 (0x1801c84a9..0x1801c8532) = the dwords {0, 0x22222222, 0, 0x88888888} x 8: row y mod 4 = 1 -> x mod 4 = 2,
      row 3 -> x mod 4 = 0, rows 0 / 2 empty (one pixel in 8); mask 1 (0x1801c8539..0x1801c85fc) = 0x3ff00ffc 0x3ec0037c
      0x3e80017c 0x1e000078 0x0e000070 0x030000e0 0 0 0 0 0x00c00300 0x00f00f00 0x00f81f00 0x00fc3f00 0x01cc3380 0x033c3cc0
      0x0ffc3ff0 0x0f7c3ef0 0x037c3ec0 0x01781e80 0x00700e00 0x00e00300 0 0 0 0 0x030000c0 0x0f0000f0 0x1f0000f8 0x3f8001fc
      0x33c003cc 0x3cf00f3c - no pass reads it;
    - list 0 (auto-highlight): {0,0,0,0, ff, 3} (the face lit in its own colour), {0,0,0,0x40, ff, 0} (flat black, alpha
      0x40), {auto_highlight_color rgb, 0x80, 0, 0} (flat, alpha 0x80, mask 0); glow: one pass {ahl rgb, ff, 0, 7};
    - list 1 (selected): {select_color rgb, ff, ff, 2} (lit in the select colour), {0,0,0,0xc4, 0, 0} (flat black, alpha
      0xc4, mask 0); glow: {select rgb, ff, 1, 7}. The colours are read when InitStippleData runs (Wd_colors).
  - **Which faces get the flags**: MaintainGrRenderSurface (0x180322560): visibility, render mode (render_cfg 1 -> 6, 2 -> 8 /
    0x17, 3 -> 1, 4 -> 3), SetGrRenderSelected(arg) (vtable +0x180, bit 2), SetGrRenderAutoHighlighted(the render is the
    auto-highlighted node DAT_1809a0c38's) (bit 1), SetGrRenderColor(colour; with glow the entity's own). Callers:
    FUN_18031f620 (the repaint's per-entity loop: the real selected bit `ent+4 >> 2 & 1`, colour = find_color(own) or
    auto_highlight_color for the hovered node) - but NOT for surfaces in render_cfg 4 (bVar13 = class 0x100 and render_cfg 4);
    FUN_180229d90 (dl_draw FUN_18032b420 -> here in render_cfg 3 / 4: FUN_18032ff40 returns 1) with selected =
    `(ent+4 & 4) == 1` (0x180229f0f: AND AL,4; CMP AL,1; SETZ - never true) and the drawing colour. So in render_cfg 4 (Alt+S
    Outline Shaded = this program's solid_mode 1): hover = FUN_180205690 draw_ent_forced(ent, auto_highlight_color, true) ->
    colour ahl + flag 1 (the repaint FUN_1801dbc80 sets the flag again before the GAF draw) -> list 0; selection =
    draw_ent(ent, select_color, false) -> colour select, no flag -> the face lit in the select colour. In render_cfg 3
    (Shaded) the repaint sets Selected from the entity -> list 1.
  - **Ported** (scene.c `scene_srf_look` + the mesh shader's `flatc` / `stip` / `smask`, core_ui cui_overlay.c `cui_ent_hl`,
    config 4053 `cfg_stipple_hl`): solid_mode 1 only; a hovered surface = lit in config 5000's colour, then flat black alpha
    0x40, then flat config 5000 alpha 0x80 through mask 0 (gl_FragCoord-indexed, the glPolygonStipple bit order), GL_LEQUAL
    for the passes; config 4053 n = the lit face only; a selected (or picked) surface = lit in config 5033's colour. Test:
    srf_scenes.txt srf_hl_* (step `srfhl`).
  - NOT DECODED / not ported: the translucent display (the pipe's alpha scaling +0x2f4 / +0x2f8), Glow type 1 / 2 (the glow
    shaders OglGlow1 / 2ShaderOp), solids' faces / bodies (sld_draw paths, FUN_1800bc970; the GAF solid's flags are the same
    pipe), meshes (GrPolygonalMesh), render_cfg 3 as its own mode (this program has one shaded mode = 4: the Shaded button's
    list 1 look is decoded above but has no mode to show in), the colour of a hovered surface after the hover ends while it is
    selected (FUN_180204f00 not read; ours: the selection look again), what the hovered + selected face looks like in
    render_cfg 3 (pass 0 = material 2, the select colour, by the flag stripping - decoded, not reachable here).
- **AutoCursor snap** (`geo_snap`, core_ui/cui_snap.c, moved from mcamui.c): the decoded pass order of FUN_1804e1bb0 (decomp/mccore_autocursor2.c:381-800):
  1. **Tangent** (dl_search_fast_tpn, decomp/mastercam_dlsearch_tpn.c): needs the rubber-band anchor; with a line-like
     rubber band (types {0,17,35}) lines are refused (FUN_180131c50:604-609); never on the anchor's own entity
     (dlsearch_tpn:204-205). Pre-check: each angle (anchor->cursor drag, the tangent at the eval point) is folded to
     [0,pi] (exact 0 / pi -> 0, strictly within 1e-6) and the plain difference must be under sel_dist / radius
     (FUN_180131c50:651-699; for line entities with an arc-like rubber band the tolerance is 1 degree - not reachable
     here); the snap point is the exact tangent point from the anchor (tang_ent_thru_pt - not dumped; the tangent
     point nearer the cursor's closest point here). No distance re-test.
  2. **Feature points** (dl_search_fast, decomp/mastercam_dlsearch.c): point entities -> endpoints -> midpoints (arc:
     the mid-angle point) -> quadrants -> arc centres, nearest wins, aperture sel_dist * 0.5 (dVar6). (2026-10-21:
     "nearest wins" is ONE running distance for every kind - local_d078, each candidate `d < dVar6 && d < local_d078 &&
     IsPosVisible(1)`, mastercam_dlsearch.c:718-790 / 866-935; geo_snap ranked them by kind first until then - fixed,
     see "AutoCursor on solids".) ONLY WHEN NO
     feature hit (dlsearch:963,1256-1258): **intersections** of the two entities closest to the cursor
     (intersect_ents, decomp/mccore_inters.c: carrier intersection, no segment / sweep clamping in the search phase,
     the hit nearer the midpoint of the two closest-approach points; conics / solids excluded). Checkbox Intersect 337.
  3. **Origin**: the C-plane origin at the FULL sel_dist.
  4. **Temporary Center Point** (FUN_1807083e0 -> CalcAutoCursorTemporaryCenterPoint, decomp/mccore_tempmid.c:138-242):
     the centre of the bounding box of all displayed geometry (EntityMinMax per entity; arc extrema via the in-sweep
     quadrant points here); registry-persisted (ACTmpCtrPntActive) and force-enabled during point entry
     (mccore_getevent.c:100-104); always on here during picks (the registry default is NOT DECODED).
  5. **Temporary Midpoints** (mode 0x1A, checkbox 360): two captured slots, offered in slot order, first within
     sel_dist*0.5 wins (mccore_autocursor2.c:610-640). Capture = the **N** key during point entry (opmanres string 360
     "Temporary Midpoi&nts" -> the letter after '&', FUN_1800f44f0 decomp/mastercam_acpk.c), with the toggle / promote
     semantics of FUN_18001baa0 (decomp/mccore_tempmid.c:705-765: capturing a stored point removes it, a new one takes
     a free slot else overwrites the first); cleared when a point-entry command ends (FUN_180708450 via
     mccore_getevent.c:105-107). The original's third "live" slot and its hover tracking are NOT DECODED (not ported);
     here the capture point is the midpoint of the entity under the cursor via pick_entity (ours).
  6. **Perpendicular** (dl_search_fast_tpn do_p, FUN_180131620): the drag direction within sel_dist /
     |anchor->cursor| radians of the in-plane normal (CplaneZ x curve_tangent, :812-816) at the eval point - the
     anchor's own entity is evaluated AT THE ANCHOR (dlsearch_tpn:214-218), any other at the cursor's closest
     point; same [0,pi] fold + plain difference as the tangent pass (:792-844). Then, by param_6 (disasm
     0x180135264: the 6th stack arg = the anchor-entity compare; the solids path passes 1): on the anchor's OWN
     entity the snap is anchor + normal x the drag DISTANCE on the side nearer the cursor (:845-876; the side
     test compares the DIRECTED normal and anchor->cursor angles with a plain difference, no wraparound - the
     original's quirk), so dragging at ~90 deg to the previous segment snaps to exactly perpendicular and keeps
     the length; on any other entity it is the foot of the perpendicular from the anchor
     (createLinePerpToAnEntity + proj_pt_on_ent, :878-889). No distance re-test either way.
  7. **Grid** (sel_grid_search).
  8. **Horizontal / Vertical** (mode 0x19, checkbox 359; FUN_1804e1bb0:558-606): rubber-band types {0,17,32,35}, view
     coordinates at the anchor's depth, H = (Cx, Ay) preferred when |Cy - Ay| <= sel_dist, else V = (Ax, Cy); C-plane
     not edge-on (|dot(CplaneZ, viewZ)| > 1e-10; tangent / perpendicular use the SIGNED dot).
  9. **Nearest** (dl_search_fast_tpn do_n): the closest point on a line / arc within sel_dist * 0.5 (no helper, no
     anchor interaction, dlsearch_tpn:233).
  The tpn passes share one search (dl_search_fast_tpn @ 0x180134ba0): DL mask 0xC8E (no point entities), eligibility =
  cursor within sel_dist*0.5 of the closest point OR the entity is the anchor's (find_an_entity with mask 0x8E; via
  pick_entity here); the winner is the eligible candidate whose SNAP POINT is nearest the cursor in 2D (ties keep the
  earlier entity; the first eligible candidate is taken unconditionally - the running best starts at ~1e38,
  dlsearch_tpn:104-105,235-260). The anchor modes are gated on the command's rubber-band type (the init_rubber_band
  argument; masks: tpn {0,17,35} = 0x800020001, H-V {0,17,32,35} = 0x900020001, tangent also allows the arc-like
  {5,6,8,18,20} = 0x800160161). Per command (2026-10-20): Line Endpoints = 0 freeform / multi-line, 35 the +0x33
  mode, 17 a locked length, 16 a locked angle, 32 the arc-tangent drag (sketcher_line_1.c:2996-3028, 2321-2341; only
  0 is mapped here); the arc commands' types (Circle Edge Point 5 / 6, Circle Center Point 8 / 12, Arc Polar 20 / 8,
  Arc Endpoints / Arc 3 Points 18, Arc Tangent 10 - the init_rubber_band calls in sketcher_arc_*.c) enable only the
  tangent pass with the arc-rubber-band mechanic (anchor at rubber_band + 0x70, the reference direction rotated 90
  deg, perpendicular-foot result - FUN_180131c50:611-718), whose anchor semantics are NOT DECODED, so they get no
  anchor modes; Line Parallel / Perpendicular / Bisect set no rubber-band type in their decompiles. **Angular** (mode 0x18, checkbox 352 + degrees edit 289) is
  NOT in the pass function: the rubber-band consumers apply it (decomp/mccad_lines_helpers.c:1240-1300,
  FUN_18018f1b0): with an anchor and no snap hit, round the anchor->cursor angle to the step (half away from zero),
  keep the length; Ctrl suppresses; planar-in-Z only (|dz| < tol_exref - its value is NOT DECODED, 1e-6 here). mcamui:
  `geo_ang_snap`, gated on the Line command. The step is clamped to [1e-6, 90] degrees and made absolute by the dialog
  (mastercam_acok.c:30); the original's default step is NOT DECODED (1.0 here - ours).
  A snapped point gets `draw_fast_marker` (0x180331460): an axis-aligned square, half-size sel_dist * 0.15 view units,
  colour 15 (8 on a white background), width 1 - mcamui draws it in view space from vmat rows (plain colours, not XOR
  - ours); the grid-only snap keeps this program's cross marker (ours). Per-snap cursor glyphs (opmanres cursors
  261-714) are not ported (the OS cursor stays).
- **AutoCursor Settings** (FT command 3340 -> doAutocursorDlg -> opmanres dialog 779, layout from
  exports/opmanres_dialogs.tsv): two checkbox columns (Origin 333 / Center 334 / Endpoint 336 / Intersect 337 /
  Midpoint 339 / Quadrant 343 / Point 341; Angular 352 + edit 289 / Tangent 345 / Perpendicular 347 / Nearest 350 /
  Horizontal-Vertical 359 / Temporary Midpoints 360), Enable All 537 / Disable All 539, "Default to Fast Point mode"
  357, "Enable power keys" 362, OK (FUN_1800da800 commits) / Cancel / Help. Mode index <-> checkbox (fast_data arrays
  at +0x2A, decomp/mastercam_acinit.c / mastercam_acok.c): Origin = 1, Center = 2, Endpoint = 3, Intersect = 4,
  Midpoint = 5, Point = 6, Quadrant = 9, Tangent = 0x15, Perpendicular = 0x16, Nearest = 0x17, Angular = 0x18,
  H-V = 0x19, Temp Midpoints = 0x1A. mcamui: `show_acset` in draw_key_windows; all but Fast Point 357 / power keys 362
  are enabled (those two stay greyed); Enable / Disable All act on the ported ones (ours); the flags are session-only
  (the original's persistence is NOT DECODED - the flags live in BSS fast_data with no config writer found). The
  original's DEFAULT enabled set is NOT DECODED (BSS zero + only the dialog writes them; the chm pages don't say) -
  all modes default on here. Entry point here: the keymap (doAutocursorDlg); the original's Selection Bar
  (CAutoCursorBar) and the Screen config page button 9017 are not ported.
- **Power keys** (FUN_1800f73c0, decomp/mastercam_acinit.c:180-367): with Fast Point off / on the typed-coordinate
  handling differs (not ported - no AutoCursor bar). The key letters come from the opmanres strings after a '&'
  (FUN_1800f44f0, decomp/mastercam_acpk.c): 3329-3335 + 3341 = "&Origin" O / "Arc &Center" C / "&Endpoint" E /
  "&Intersection" I / "&Midpoint" M / "&Point" P / "&Quadrant" Q / "Alon&g" G (command ids 0xA042-0xA04A, 0xA04E at
  DAT_180864980; 3336-3339 "Nearest" / "Relative" / "Tangent" / "Perpendicular" have no '&' = no key), and the 14th
  (string 360) is the Temporary Midpoints capture key **N** (ported, see above). NOT PORTED: the one-shot override /
  lock machinery behind the keys (FUN_1800ecc80, the AutoCursor_*_Lock commands 0xA042 lock variants, the override
  drop-down commands 3329-3374), Fast Point mode (357 / SetFastPointDefault, FUN_1807083e0's pass is the Temporary
  Center Point instead).
- **AutoCursor on solids** (2026-10-21; decomp/mcgeomsld_snap.c = mcgeomsld.dll sld_search_fast / ps_select /
  sld_select / sld_identify_from_cursor / sld_pick_*_from_cursor + sld_solid_extents etc., decomp/mccore_proj2d.c =
  mccore proj_2d_pt_to_curve / dist_to_finite_line / proj_iso_pt; with mastercam_dlsearch.c, mastercam_dlsearch_tpn.c,
  mccore_autocursor.c / mccore_autocursor2.c). There is NO separate solid pass: solids are display-list nodes (type
  0xe) inside the same passes as wireframe, and the snap point is returned like any other (FUN_1804e1bb0 writes the
  p_3d at param_3 + 0x5390, mccore_autocursor2.c:476-490 - so a solid vertex gives its own Z to the Linking page's
  Depth / Top of stock pick, point_proc_assoc in mccore_tp_select_depth.c:79).
  - **Where it is not**: sld_identify_from_cursor with psHit types body 1 / edge 2 / face 4 (corrected 2026-10-21, see
    "Solid selection hover highlight") and the mfMODE bits 0x1000 / 0x2000 / 0x4000 (mccore_autocursor.c:51-63,
    FUN_1804df930) is the mouse-move solid HOVER highlight of entity selection (it ends in EraseAutoHighlight /
    sld_search_fast_hilite), not the point snap; the dataentr_ep
    branch for ent type 0x10000 (sld_pick_endpoint_from_cursor / sld_pick_point_along_edge, mccore_inters.c:322-334)
    is the manual "Endpoint" data entry, not the AutoCursor.
  - **The edge pick** (ps_select 0x18004fbd0, mcgeomsld_snap.c:3-430): PK_BODY_pick_topols along the cursor ray (the
    cursor mapped at view depth 0 and +-10000 m, map_cursor_to_3d), maximum edge distance 10 / get_x_scr_scl = **10 px**
    (:229; get_x_scr_scl = view +0x768 = pixels per unit, mastercam_acsave.c:2347 / mastercam_viewrot.c:332), edges kept
    when their distance <= that (:349), at most 30 (0x1e); per hit psHit +4 = the edge tag, +0x10 = the edge's point
    nearest the ray, +0x28 = its distance from the ray's start in front of the view = the depth order. A body is only
    tried when the cursor lies in its view-space box +-10 px (FUN_1800cd000 / sld_solid_extents = the 8 corners of the
    body box through GetGView) - a pure cull, nothing within 10 px is lost. Back-faces button
    (GetSolidSelectionBackButtonChecked, flips the ray) not ported.
  - **Feature pass** (dl_search_fast -> sld_search_fast 0x1800c7bf0 for each solid node, mastercam_dlsearch.c:213-224;
    mcgeomsld_snap.c:1163-1650): the body's edge hits ordered front to back (:1316-1370), and per edge (the loop ends at
    the first edge that gives a hit, :1391-1392): PK_EDGE_ask_geometry (:1393: curve, class, the two vertex positions,
    the interval) -> **Endpoint** (fast_data[3]: the start vertex, then the end vertex when strictly nearer, :1403-1460),
    **Midpoint** (fast_data[5]: PK_CURVE_eval at (t0 + t1) / 2, :1461-1492), **Quadrant** (fast_data[9], circles - class
    0xbba - only: t = 0, pi/2, pi, 3pi/2 inside the interval, :1495-1550), every candidate IsPosVisible(item 2); the
    edge's nearest one is accepted when < sel_dist * 0.5 AND < the pass's running best (shared with the wireframe
    candidates, param_9 = local_d078) AND its mode is on (:1553-1556, 1601-1616: snap kind 3 / 5 / 9, the point in
    model units = Parasolid metres x 1000 or x 39.37). Else, for a circle edge, **Center** (fast_data[2]): when the
    edge's ray point (psHit +0x10) is within sel_dist * 0.5, nearer than the best and visible, the snap is the circle
    centre (PK_CIRCLE_ask, kind 2, :1557-1600). Then the next edge. The two front-most distinct edges (param_11) are the
    body's **intersection** candidates when it gave no feature hit and Intersect is on (mastercam_dlsearch.c:230-260:
    ps_edge_to_mc_curve_struct, proj_2d_pt_to_curve, < sel_dist * 0.5, IsPosVisible(2)); intersect_ents then takes the
    two closest curves (wireframe or edge) exactly as for wireframe, a solid's curve copied in for its ent
    (mastercam_dlsearch.c:1263-1400, kind 4).
  - **Tangent / Perpendicular / Nearest** (dl_search_fast_tpn, mastercam_dlsearch_tpn.c:296-367): only when the pass
    found nothing on wireframe: sld_select(cursor, 2 = edges, hits, 1) (0x1800c93a0, mcgeomsld_snap.c:1654-1990: every
    visible body, ps_select, edges whose point nearest the cursor - dist_to_finite_line + t_on_line, t NOT clamped - is
    hidden (MayBeHidden(2) + IsPosVisible(2)) dropped, then sorted by depth, edges first) and ONLY ITS FIRST EDGE:
    ps_edge_to_mc_curve_struct, proj_2d_pt_to_curve (mccore_proj2d.c: a line = the distance to the FINITE segment,
    the carrier parameter unclamped) < sel_dist * 0.5, eval_curve at that parameter, then FUN_180131c50 (tangent: a
    line with a line-like rubber band is refused, :604-609), FUN_180131620 with param_6 = 1 (the solids path, disasm
    0x180135264: anchor + normal x the drag distance), else Nearest (the point itself); the snap point IsPosVisible(2).
  - **IsPosVisible** (mastercam.dll 0x1802064a0, dlsearch_tpn.c:899-962) on every candidate (item 1 wireframe, 2
    solid): when MayBeHidden (0x180206ad0: render_cfg 3 / 4 = shaded and not (translucent selection + full-time
    translucent), or render_cfg 2 without bit 0 of +8) the face under the point's screen position (FUN_180207a70 ->
    sld_pick_solid_from_cursor) hides it when it is in front by more than |z * 1e-5 * 150| (z = the point's view depth).
  - Marker: the same draw_fast_marker square as any snap (FUN_1804ea8a0 also draws the snapped entity in the select
    colour - draw_ent_forced for wireframe, sld_search_fast_hilite for a solid edge; neither is ported, as before).
  - **Ported** (core_ui cui_snap.c geo_snap; edges from solid25.c `body_brep_edges`, kept by `ac_solids_set` from
    bodies_show): this kernel's bodies are Z slabs of polygon regions, so the Parasolid edges are rebuilt (ours): the
    outlines of the up / down cap faces at each slab height plus the vertical edges where the wall normals turn by more
    than EDGE_DEG (= what body_edges draws), merged through vertices where exactly two segments continue in line (one
    edge across stacked slabs / collinear splits), runs through horizontal vertices turning by < EDGE_DEG = a faceted
    curve (`curved`). Feature pass: Endpoint / Midpoint of the edges within 10 px, front to back, first edge with a hit
    per body, after the wireframe (display-list order), one running distance with the wireframe features; intersection
    candidates (straight edges in an XY plane); the tpn fallback (Perpendicular param_6 = 1 / Nearest) on the first
    visible edge; IsPosVisible for solid_mode 1 (shaded, opaque) against the slab bodies (body_hit). Changed for
    wireframe too (decoded, both apps): the feature pass is nearest-wins over all kinds (was: kind first), intersection
    candidates need sel_dist * 0.5 + visibility, and IsPosVisible(1) applies to the feature / tpn / intersection
    candidates when solids are shown shaded.
  - **Point picks and the cursor** (fix 2026-10-22, user report: the Linking "..." and the Extrude direction picks did not
    snap): the AutoCursor searches around the CURSOR (FUN_1804e1bb0 / dl_search_fast take the cursor position, ps_select
    maps the cursor at view depth 0 and +-10000 m - map_cursor_to_3d); geo_snap here re-projected the world point it was
    handed, the cursor on the construction plane (scene_pick), which is NOT the cursor when the view looks along that plane
    (Front / Back / Side views: the ray never meets it, so the search ran at the bottom of the view) - every click / hover
    now searches around the mouse (geo_snap_ptr: mcamui handle_click, Qt viewClick, geo_cursor_update, geo_click). And
    the AutoCursor marker (draw_fast_marker) ran only for the wireframe commands: the point picks that run point_proc /
    get_event with the AutoCursor (Linking "..." tp_select_depth -> point_proc_assoc, Analyze's new position, Stock Setup
    Select origin / corners, the Extrude panel's AutoCursor / direction picks) now snap and draw the marker on every mouse
    move (cui_snap.c pp_hover). A missed click still gives the construction depth. Not changed: the unsnapped
    construction-plane point in a view along the plane (NOT DECODED for this Top-only Cplane), the Alt+F12 rotation
    centre's hover (its click snaps the same way), a click where a wireframe line lies under a solid vertex in the Extrude
    direction pick takes the line (the event order of mask 0x4003 is NOT DECODED). Tests: solid_scenes.txt ss_lk_*,
    ss_ex_dir_*, ss_ex_autocursor_front, ss_wire_pt_front; stockplanes_scenes.txt st_corner_*_snap_front.
  - **Round edges** (2026-10-21, formats/extrude.md 4.9): the bodies made here now carry their chains' arcs, so
    body_brep_edges returns circle edges (`BEdge.arc` 1 = arc between two vertices, 2 = ring edge without vertex; centre,
    axis +Z, parameter 0 along +X, interval t0 .. t1 with t0 in [0, 2 pi)). geo_snap gives them the decoded circle-edge
    passes: feature pass Endpoint (the vertices; none on a ring edge - PK's ring-edge ends NOT DECODED), Midpoint at
    (t0 + t1) / 2, Quadrants at t = 0, pi/2, pi, 3pi/2 inside the interval, else the Centre fallback (the edge's
    point nearest the cursor within sel_dist * 0.5, nearer than the best, visible -> the centre, snap kind 2); the
    intersection candidates use the true arc (ge_arc); the tpn fallback evaluates the true arc (nearest point on the
    bounded arc in the view, dist_to_arc not dumped), with Tangent (FUN_180131c50 on a circle edge: no own-entity
    exclusion on this path), Perpendicular (param_6 = 1) and Nearest. Visibility of a point of a round edge is tested
    where the facet polygon lies at the same angle (the faces are faceted here, ours). Tests: tools/tests/solid_scenes.txt
    ss_cyl_top / ss_cyl_link / ss_cyl_click_quad / ss_cut_hole / ss_cut_through.
  - **The part file's solids** (2026-10-21, formats/xt.md "Edges"): their LINE / CIRCLE edges are read from the XT
    (xt_brep_edges) into the same BEdge form (cui_snap.c `part_edges_sync`: one edge set per solid after the Extrude
    bodies) and take every pass above; IsPosVisible also tests the part's display mesh (`part_hit`). Note (decoded
    rule, visible on parts whose wireframe lies on the solid's edges): a solid candidate must be STRICTLY nearer than
    the running best of the pass (shared with the wireframe, sld_search_fast :1553-1556), so on a wireframe arc lying
    on a circle edge the equal-distance solid Quadrant / Midpoint is refused and the circle's Centre fallback (the
    edge's ray point, nearer than the wireframe point) wins - solids are searched after the wireframe here (the
    display-list position of a part's solid among its entities is not tracked). Tests: ss_part_bracket4 /
    ss_part_nakto / ss_part_link (copies of the user's parts).
  - NOT PORTED / NOT DECODED on solids: faceted runs of unknown geometry (drafted bodies: end vertices only), Tangent on
    straight edges (refused anyway), Perpendicular on a vertical edge (CplaneZ x
    tangent = 0; FUN_180130980 on a zero vector NOT DECODED), intersections with vertical edges or between curves in
    different Z planes (intersect_ents' 3D path not dumped), PK_BODY_pick_topols' own hit order and its 30-hit cap
    (all hits kept, sorted front to back), the back-faces button, MayBeHidden with translucent solids
    (gl_translucent_selection's default NOT DECODED: no hiding in solid_mode 2), the snapped solid edge's highlight
    (sld_search_fast_hilite / FUN_1804ea8a0), part-solid edges on curve classes other than LINE / CIRCLE.
- **Solid selection hover highlight** (2026-10-21; decomp/mccore_autocursor.c = mccore FUN_1804df930, decomp/
  mcgeomsld_snap.c sld_identify_from_cursor, new dump decomp/mcgeomsld_hover.c: FUN_180099aa0 / FUN_180098b20 /
  FUN_180259770 / FUN_1800bc970 / FUN_180042a00 / FUN_18004e540, the selection-bar handlers, FUN_1800a44c0,
  sld_pick_one_ent, mccore FUN_1804e1340 / FUN_1804e15b0):
  - **Hit kinds** (psHit +0): 1 = body, 2 = edge, 4 = face. get_event's mode bits map to the solid pick mask in
    FUN_1804e1340: 0x1000 bodies -> mask 1, 0x2000 faces -> mask 4, 0x4000 edges -> mask 2 (+ the edge entity mask
    when 0x400000 is in the second word); sld_identify_from_cursor turns an edge whose curve fails the edge mask into
    its body when mask & 1, a face into its body when !(mask & 4); cursors 8 / 0x10 / 0x20 (body / edge / face).
    (Earlier notes said "face 1 / edge 2 / vertex 4": there is no vertex kind.)
  - **When**: FUN_1804df930 (every mouse move of point / entity input): (a) mfMODE & 0x7000 and a hit of an enabled
    kind (0x4000 edge, 0x2000 face (hit 4), 0x1000 body (hit 1)) -> EraseAutoHighlight (the wireframe hover goes), the
    solid hit stays highlighted; (b) else render_cfg == 3 (not full-time translucent) and a hit -> the same; (c) the
    wireframe auto-highlight (DrawAutoHighlight) as before; then, only when no wireframe entity is highlighted,
    sld_identify_from_cursor(.., highlight = true): a hit becomes the highlighted tag (FUN_180099aa0 + a repaint),
    no hit clears it. Every one of these needs the solid pick mask (GetSolidSelectionPickMask, mcgeomsld data
    0x1803e80ec = 0 in the image): it is set only by get_event with mode bits 0x7000 (FUN_1804e1340, restored after),
    sld_pick_one_ent (restored after each event), SelectEntsOrdered / DragOnEntity and the Selection Bar's solid
    buttons (OnSolidsSelectBody / Face / Edge, enabled only while such a selection is active - OnUpdateSolidsSelect*).
    So in **plain idle, point entry and chaining** (no solid selection; the chaining dialog's Solids button 351 is not
    ported) the mask is 0 and **nothing on a solid is highlighted** - what both apps already do. The one solid
    selection this program has is the Extrude panel's **Select Target Body** (sld 857, sld_pick_one_ent with mask 1 =
    bodies; FUN_1800a44c0 checks the Bodies button): hovering a body highlights it (hit type 1).
  - **Drawing** (sld_repaint_hilite_ents -> FUN_180098b20 for the highlighted tag): type != 4 -> FUN_1800bc970(tag,
    auto-highlight colour) fills a FACE in the auto-highlight colour (shaded render_cfg 3 / 4 only; a body is no face:
    nothing) and FUN_180042a00 draws the topol as wire (FUN_18004e540 render mode 1 -> PK_TOPOL_render_line) in the
    **select colour** (config 5033): a body = its edges in the select colour. No set_gl_style in the path - so not the
    FUN_180135ad0 stripes; no set_gl_width (the current width - NOT DECODED). Faces / bodies also get
    FUN_180259770 -> SetGrRenderAutoHighlighted on the GAF render object (its look = gafgr's stipple list 0, decoded under
    "Shaded face highlight (GAF stipple)"; for solids not ported).
  - **Ported** (core_ui, both apps): `sld_hover_body` (cui_sel.c cui_hover_update: while ex_mode && ex_pick == 2 the
    front body along the cursor ray, ex_pick_body = sld_select of bodies), drawn by build_overlay (cui_overlay.c) as
    the body's edges (body_edges) in config 5033's colour at the overlay's width. Test: tools/tests/solid_scenes.txt
    ss_hover_target (+ ss_hover_idle / ss_hover_chain: no solid highlight), `sldhover` dump step.
  - Solid chaining (2026-10-22, formats/chaining.md "Solid chaining"): FUN_1800c2da0 sets the pick mask (6 / 4 / 2)
    around read_cursor_pos, so while the Chaining dialog is in Solids mode the edge under the cursor is drawn in the
    select colour (core_ui cui_sldchain.c sld_hover_update / sld_overlay; the wireframe hover is off there).
  - NOT DECODED / left out: the GAF AutoHighlighted face / body look, the highlight width, face picks outside solid
    chaining (Trim to faces is not ported), the cursors 8 / 0x10 / 0x20, the
    part file's solids as targets (this kernel cannot modify them: ex_nvis), FUN_180073f60's list mode.
- NOT DECODED / not done: Fast Point mode, the power-key overrides + locks (only the N capture key is ported), the
  AutoCursor override drop-down, the Selection Bar (only its Select body / Select face buttons are ported, in the status bar during
  a 3D op's drive / check prompt: formats/surface_selection.md 6 "N13-8 / N13-9"), solid face / edge hover picks (sld_identify_from_cursor), the solid
  items listed under "AutoCursor on solids", splines / conics (no such entities here), arcs in non-XY planes for the
  tpn / intersection passes.

## Esc with no function running (decoded 2026-11-14; user report "in mastercam when I hit escape does it unselect everything")
Ghidra dumps of this pass (scratch, not committed): mastercam.dll FUN_18026ad90, COpMgrTree::PreTranslateMessage, CMastercamApp::Run /
PreTranslateMessage, GSUnSelectAll / OnUpdateGSUnSelectAll / FUN_1801a7d70; mccore.dll PumpPreSelection with its callees.
- **Where the key goes.** The main frame's PreTranslateMessage (mastercam.dll FUN_18026ad90) takes every WM_KEYDOWN that is not
  kept by its exclusions (frame flag +0x5a79, focus inside the frame children +0xa130 / +0x9bf8 [identity NOT DECODED], the ribbon
  controller, a manager editing a name, a UIEdit with FUN_1800f73c0, FunctionPanelDialog::ProcessAccessKey) and posts it as the
  thread message `0x802a` (Esc) / `0x802b` (any other key, wParam = the key). With the Toolpaths manager tree focused,
  COpMgrTree::PreTranslateMessage (@180364f70) posts Esc the same way (0x802a, 0x1b) unless its label edit is open. Nothing in
  mastercam.kmp binds Esc.
- **A function running** (a command, a prompt, a pick, chaining, a dialog's graphics pick): it reads the event in its get_event
  loop (mccore get_event's Esc case FUN_1804e34f0: a window / polygon selection being drawn is cancelled first when the event mask
  allows it, else Esc is returned to the function = its cancel). Modal dialogs have their own loop (Esc = IDCANCEL). The selection
  is NOT cleared by the key itself there (what each function does at its end is its own code).
- **No function running.** CMastercamApp::Run (@180294550, the idle message pump) passes the messages 0x8028..0x8038 to mccore
  `PumpPreSelection` (@1804e6890) only while `allow_preselection` is set (config 4001 "Allow pre-selection", mccore FUN_1801ba2f0
  case 0xfa1; ours cfg_presel, factory default on). PumpPreSelection -> FUN_1804e50a0 case 0x802a -> FUN_1804e3560:
  - a polygon selection being drawn (state DAT_1813f5f40, byte +1): its rubber band erased (FUN_1804e14d0) and reset
    (FUN_1804e2a40) - consumed;
  - a selection window being dragged (DAT_1813f5f38, count > 0): its focus rectangle erased (FUN_1804e1500 -> FUN_1804e0880) and
    reset (FUN_1804e2aa0) - consumed;
  - otherwise PumpPreSelection posts `0x8028` with wParam `0x8104` -> FUN_1804e6250 case 0x8104: FUN_1804e0710(&DAT_1813fb410) (the
    preselection event state: flags, its chains freed, its two arrays emptied) and FUN_1804e07a0 = **`set_clr_sel_bits_all(0, 4, 0,
    4, 0x1f1fff, 0, 0)`** (selection bit 4 off on every entity class of the general selection), `repaint_graphics()`,
    `gl_check_flush(false, false)`. The Selection Bar's buttons follow through their OnUpdate callbacks (FUN_1801a7d70).
  - Enter (0x802b, 0xd) in the idle pump posts 0x8028 / 0x8105 only in the selection lock modes 3 / 6 (end of a polygon).
- **Not touched by Esc**: the Toolpaths manager's selected operations (no op call in the path). The Selection Bar's own "Clear selection" button (GSUnSelectAll @1801aa180 -> WM_COMMAND 0xa012) is a
  separate route (its handler NOT DECODED here).
- **Ported** (2026-11-14): core_ui cui_sel.c `sel_escape_idle` (cfg_presel off -> nothing; else sel_clear + overlay redraw); Qt
  mainwindow.cpp eventFilter: Esc after the whole existing Esc chain, when `escIdle()` (no command / prompt / pick / drag / dialog
  state of ours, OK's computation not running, the main window active; a text field keeps the key as Mastercam's UIEdit
  exclusion) -> sel_escape_idle + refreshAll (the redraw and the ribbon's enabled states). Every earlier Esc case is unchanged.
  The window / polygon cancel branches have no counterpart (there is no idle window / polygon selection here) and the
  preselection event state has nothing to reset. Esc in a floating pane / non-modal window of ours: nothing (its routing NOT
  DECODED). Tests: tools/tests/misc_scenes.txt esc_unselect / esc_zoom_prompt / esc_presel_off / esc_chain_prompt (test steps
  `keyev:KEY` = the key through the window's whole key path, `presel:0|1`).


## Code: viewer/scene.c / scene.h (the graphics view)
Shared 3D view of mcamui (and mcamgl), OpenGL 3.3. Holds the loaded part `F`, its centre / extent (vertices are stored
relative to the centre), the Mastercam view state and the drawing.

| part | behaviour | source |
|---|---|---|
| `vmat`, `vcx` / `vcy`, `vht`, `ViewState`, `view_get` / `view_put`, `view_set_matrix` | CGraphicsView state: view matrix rows (screen right, up, to the viewer), screen centre (+0x6b8 / +0x6c0), view height (+0x760); parallel projection | decomp/mastercam_viewrot*.c, set_parms / FUN_1801dd950 |
| `STD_VIEW`, `view_std`, `view_number` | reserved views 1 Top .. 7 Isometric, 8 Axonometric; Matrix33::GetViewNo | ReservedPlanes::BuildStandardMatrices, decomp/mastercam_stdviews.c |
| `view_std_animated`, `view_anim_step`, `trans_box_mid`, `angle_diff`, `min_angle_diff` | animated change to a standard view (config 33006), StoredViews per reserved view, anim_delay x 12 steps of >= 17 ms eased 1 - cos | CGraphicsView FUN_1801ccd70 (decomp/mastercam_viewtrans.c), FUN_1801bdd00, MtTmat3d::GetAngleDiff / GetMinAngleDiff (gafmt) |
| `view_fit` | GetFitExtents: visible entities (+ toolpaths while fit_toolpath) in view coordinates, x 1.05, aspect; empty -> 250 mm / 10 in | FUN_180320770 |
| `zoom_to`, `view_zoom_push`, `view_unzoom_previous`, `view_unzoom`, `view_zoom_steps`, `view_wheel_run_end` | animated zoom (config 33007 / 33008, sin^2 easing), zoom history +0x38b0, Unzoom previous / 0.8, wheel notches x config 4026 | FUN_1801cc890, FUN_1801c92b0, FUN_180329a10, FUN_1803297c0, FUN_18032a080 |
| `view_rotate_free`, `view_rotate_begin` / `view_rotate_turntable`, `view_axis_rotate_begin` / `view_axis_rotate`, `view_rotate_keys`, `view_mean_depth` | free / turntable (config 33002) rotation, Ctrl / X / Y / Z axis rotation, Alt + arrow keys about the mean displayed point | FUN_1801b0bd0, decomp/mastercam_axisrot.c FUN_1801cf1c0, FUN_1803278f0 (decomp/mastercam_keyrot.c), FUN_18013dbd0 |
| `view_pan_px`, `view_pan_steps` | cursor pan; key pan steps x config 4028 x height | FUN_1801b0bd0 mode 1, FUN_180325ea0 |
| `GridSet grid`, `grid_defaults`, `grid_round`, `grid_z` | sel_grid (see "Grid" above); `style` / `at_floor` are ours and stay at their defaults | set_default_grid, sel_grid_search |
| `MC_PALETTE`, `mc_col` | Mastercam's 256-colour palette | mastercam.dll @0x8a6440, formats/mastercam_palette.tsv |
| `upload` | one vertex buffer: geometry in entity colour, toolpaths in config 5045 feed / 5046 rapid colour; solids mesh | config [COLORS] |
| `mesh_solids` | part solids meshed from Parasolid XT (xt_mesh); XT metres scaled x1000 or x1000/25.4 by matching the part's extents (ours, formats/robustness_review.md notes it); `solid_msg` summary | ours |
| `scene_set_user_solids`, `user_stock` | the Extrude bodies' triangles; `stock_lo` / `stock_hi` = bounding box of all solids (used only by Facing without chains; not drawn) | ours |
| `draw_point_markers` | point styles per entity: 1 world X/Y/Z lines +-vht/72, 3 plus, 4 X, 5 circle 21 x 18 deg, 6 square +-vht*0.01; width 1, entity colour; style 2 a 1 px point (size FUN_18021b780 NOT DECODED), 7 / 8 glyphs NOT DECODED | draw_3d_point_style @1803301c0, decomp/mastercam_pointstyle.c |
| `bp_draw_tool`, `bp_draw_endpoints`, `bp_lv` / `bp_ln` | Backplot tool / holder, endpoints, motions | formats/backplot.md 13.3 / 13.4 / 14 |
| `draw_scene` | gradient background (config 5007 / 5009), F9 info axes, origin axes, grid (config 5032, dashed drop line to a floor grid), geometry + toolpaths, solids (config 5052; translucent mode alpha 0.38 NOT DECODED), backplot, simulation tool, overlay, corner view indicator | config [COLORS], formats/backplot.md |
| `scene_pick`, `scene_pixel_to_world`, `scene_to_screen`, `view_px_to_view`, `view_to_world3`, `world_to_view3` | cursor <-> world (on the grid / Z plane), V orthonormal | ours |
| `load_file`, `new_file`, `unload_file`, `scene_reupload`, `scene_set_center`, `scene_set_overlay` | part load (a failed open keeps the current part), File > New, rotation centre (Alt+F12), overlay lines | ours |

Ours / not from Mastercam: shader lighting of solids (headlight), the 48-segment simulation tool, the solid unit
guess, the corner view indicator letters. The fixed "+-0.5 cross" point marker of mcam_load.c is gone (point
polylines are the bare point).
