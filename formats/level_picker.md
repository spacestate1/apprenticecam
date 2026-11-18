# Level picker (`sel_level_dialog`, class `CSelLevel`, IDD 563 "Levels")

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

Source: mastercam.dll (Mastercam 2017), Ghidra project `mc2017`. Every claim cites a
function address and a line in the decomp dumps written for this spec:

| tag | file | functions |
|-----|------|-----------|
| d1 | `decomp/level_picker/d1.txt` | sel_level_dialog 18047d5b0, ctor 180474670 |
| d2 | `decomp/level_picker/d2.txt` | DDX, OnInitDialog, OnOK, message-map handlers |
| d3 | `decomp/level_picker/d3.txt` | helpers (list fill, filters, number edit, registry) |
| d4 | `decomp/level_picker/d4.txt` | LevelListCtl (columns, custom draw, registry widths, sort) |
| d5 | `decomp/level_picker/d5.txt` | default widths, sort comparator, LevelListCtl ctor + message map |
| d6 | `decomp/level_picker/d6.txt` | LevelListCtl WM_KEYDOWN 180248540 |

Also in `decomp/level_picker/`: `vt.txt`, `vt2.txt` (vtables) and `refs.txt` (references). (Written first in the
session scratchpad `.../scratchpad/lvl/`, copied into the repo.)

The `CSelLevel` class (source file `Sellevel.cpp`, from asserts at 1804763e0 d2 L1123) is
shared by two dialogs. A global flag `DAT_1809af313` picks the mode:
- **picker mode** (`af313 = 1`): set only by `sel_level_dialog` (d1 L74). It uses IDD 0x233 = 563.
- **Level Manager mode** (`af313 = 0`): a different IDD. In picker mode, DDX does not bind its
  extra controls 0x561/0x61de/0x61df/0x551/0x2ea/0x1035/0x523 (FUN_180475350 d2 L7-15). None of those IDs is in dialog 563.

This spec covers **picker mode only**. Branches guarded by `af313 == 0` are left out.

Control → member binding (DDX FUN_180475350, d2 L16-26): 1371 list → `LevelListCtl` @+0x2bd0;
1359 Number → MCLong @+0x4df0 (value @+0x59e8); 1313 Name → MCString @+0x3308 (text @+0x3f00);
radio group 1363..1366 → int @+0x4de8 (DDX_Radio, Ordinal_3627); 744 → MCLong @+0x5b58 (low,
int @+0x6750); 301 → MCLong @+0x68c0 (high, int @+0x74b8); 1327 → UIBitmapButton @+0x1988;
groups 1375/1376 → CButton.

## 1. `char sel_level_dialog(CWnd* parent, char p2, char p3, int* level)` @18047d5b0

- **p2 and p3 are unused.** The prologue keeps only rcx (into rsi) and r9 (into r14) before the
  first call (objdump 18047d5e2/18047d5e5). The decomp never reads them (d1 L7-123).
- Entry state: it saves `main_level` and `mask_level` into DAT_1808a2d80/84 (d1 L57-58). The
  working pick `DAT_1808a2d88 = *level`, or **1 if `*level == 0`** (d1 L59-62). So the initial
  value is the caller's `*level`, not the main level. The parent is stored in DAT_1809af430 (d1 L63).
- **Single instance:** if `DAT_1809af318` (the instance pointer) is already non-null, it returns 0
  at once (d1 L64, L114-115).
- If the Level Manager is open (`DAT_1809af428 != 0`), it is sent the registered message
  "OnUserClose" (DAT_1809af438, registered at 18009bc24) with wParam 0 (d1 L69-72). That handler
  (FUN_180478860 d2 L1409-1419) saves the manager's registry state and destroys it.
- **Modeless window with a private event loop.** It allocates 0x7798 bytes, runs the ctor with
  IDD 0x233 (d1 L65-67), and calls vtable +0x2d0 = slot 90 = `CBCGPDialog::Create(0x233, NULL)`
  (d1 L75, vtable dump slot 90). If Create fails it deletes the object and **returns 3** (d1 L76-81).
  Otherwise it loops on `get_event(<empty prompt>, ev, 0x200, 0, 0, &flag)` until
  `get_event` returns 0, `ev+8 == 0x200` and `ev+0x158 == 0` (d1 L85-89). Then it deletes the
  dialog and clears the instance pointer (d1 L90-93). In picker mode, closing the window posts
  thread message 0x8028 (DestroyWindow override FUN_180474f20 d2 L360-362). How
  `get_event` turns 0x8028 into the exit condition is NOT DETERMINED, because get_event is external.
- Exit: the flags are cleared (d1 L95-96). If `DAT_1809af311 != 0`, the return value is 1 and
  `*level = DAT_1808a2d88` (d1 L97-100). Otherwise it returns 0 and `*level` is unchanged.
  `main_level` and `mask_level` are always restored to their entry values (d1 L101-102). If a
  Level Manager was open, it posts the FT command ("Mastercam", "OnScreenLevels") to reopen it
  (d1 L104-110).
- **Decomp-level observation:** `DAT_1809af311` has exactly one writer in the whole DLL:
  `movb $1` at 180477799 in OnOK (FUN_180477690 d2 L139). It is never cleared (objdump grep for
  `0x1809af311` / `0x1809af310`). So after the first OK in a session (in either mode), later
  calls return 1 and write `*level` even when the user cancels. Whether something outside
  mastercam.dll resets it is NOT DETERMINED.

## 2. List view (1371, `LevelListCtl`, owner-data list)

**Columns (picker)**: LevelListCtl::PreSubclassWindow FUN_1802491d0 (d4 L441-456) inserts two
columns with LVCF mask 0xF and fmt 0 (left):

| col | header | string id (opmanres) | insert cx |
|-----|--------|----------------------|-----------|
| 0 | "Number" | 1979 (0x7bb) | 75 |
| 1 | "Name" | 1948 (0x79c) | 75 |

The widths are then overwritten from the registry (FUN_180249630 d4 L653-687 + objdump
1802497bd..180249845). Column 0 is read from `ColumnWidth0` and column 1 from **`ColumnWidth2`**,
which is the Level Manager's Name width. If a value is missing, the default is
`FUN_180247e90(i)`: `w = listWidthPx/40`, col 0 = `7w`, col 1 = `6w` (d5 L19-36). On save (d4
L781-794 + objdump 180249b85..180249be5), `ColumnWidth0 = width(col 0)` and
`ColumnWidth3 = LVM_GETCOLUMNWIDTH(2)`. Column 2 does not exist in the picker, so the Name width
is never saved from the picker. Registry key: `<get_REG_KEY>\GUI\Settings\Levels Mgr`, with
"Software\" replaced by "SOFTWARE\" (FUN_180475f30 d4 L1046-1049). Extended style 0x21
(full-row select + gridlines) (d4 L475). "Contrasting rows" (registry, default off: ctor d5 L104)
paints odd rows with COLOR_BTNFACE (d4 L141-148).

**Row content (picker)** FUN_18047d890 (d4 L849-872): col 0 = level number (`"%ld"`), col 1 =
`LevelData::getName(level)`, the raw stored name. The picker does not split off "~~~" like the
manager does (d4 L855-863). Entity count and visibility are not shown. Col 0 also has a small
icon: image 1 (bitmap 0x65bc) for the main level, image 0 (bitmap 0x65bd) for others
(LVN_GETDISPINFO/custom draw FUN_1802488a0 d4 L159-165; image list d4 L459-473). In picker
mode only the main-level row is custom painted (d4 L204). It is filled with COLORREF 0x00FFFF,
or 0x00FF00 when selected (d4 L232-239).

**Filters** (the radio index is stored in the global `list_level` (list_level_exref) by each
radio handler, d2 L404/419/434/454). FUN_18047c950 dispatches on it (d3 L902-917):

| radio | id | index | rows shown (iterate level_list order) | cite |
|-------|----|-------|-----------------------------------------|------|
| Used | 1363 | 0 | entityCount != 0 OR level == main_level | FUN_18047cb60 d3 L698-703 |
| Named | 1364 | 1 | level == main_level OR trimmed name non-empty | FUN_18047c9c0 d3 L747-757 |
| Used or named | 1365 | 2 | entityCount != 0 OR main OR name non-empty | FUN_18047cc90 d3 L802-813 |
| Range | 1366 | 3 | first every number in [low..high] that has **no** level yet (col 0 = "%d", empty name), then every existing level whose number is in [GetDlgItemInt(744), GetDlgItemInt(301)] | FUN_18047c7a0 d3 L856-881, FUN_180475eb0 d4 L928-933 |

Each filter also has a picker-only clause, "list index == row index of the current pick". The
row index is looked up in the list before the rebuild (d3 L694, L700-701). The radio handlers
clear the rows first (d2 L407/422/437/458), so that lookup returns -1 there.
After filling a list, the current pick's row is selected and scrolled into view
(FUN_18047b5a0 d3 L950-963). Then the list is sorted (FUN_180249fe0).
- Range edits 744/301 are enabled only for Range (d2 L405-406 vs L456-457; d3 L915-916). They are
  spin buddies with range 1..2,000,000,000 (d3 L269-270). Defaults come from the registry
  "Level list low range" = 1 and "Level high low range" = 1000 (sic) (FUN_18047ab60 d3 L61-72).
  They are saved on destroy (FUN_18047aed0 d3 L404-406). Spin: low must be >= 1 (FUN_1804786a0
  d2 L797-809). High must be <= 2,000,000,000 (FUN_180478750 d2 L828-845). A spin, a kill-focus
  with a changed value (FUN_180476e20 d2 L931-943), or Enter (PreTranslate d2 L232-246) rebuilds the list.
- **Default radio** = the current value of the global `list_level` (FUN_18047b6b0 d3 L242-244).
  Its initial value and whether it is persisted to config are NOT DETERMINED (the global is
  imported).
- On open, `FUN_18047b6b0` **deletes every level that has an empty name and 0 entities and is not
  the main level** from `level_list` (d3 L216-224).

**Sort**: comparator FUN_180247850 (d5 L44-89). Columns 0 and 3 compare numerically (atol after
removing spaces). Other columns use a string compare (Ordinal_2863), and ties fall back to
column 0. A non-zero direction negates the result. Picker settings: registry "Mini sort column" /
"Mini sort direction" (d4 L655-657), default 0/0 = Number ascending (ctor d5 L106). A header
click (HDN_ITEMCLICK A/W, FUN_1804768c0 d2 L1439-1459) sets the column. The direction is
`(clicked == current column) && (LVCOLUMN.iImage == 0)`. Only LVCF_SUBITEM is requested, so
literally this gives 1 when the current column is re-clicked and 0 for any other column. Real
toggle behaviour is NOT DETERMINED (the BCGP header may intervene). HDN_BEGINTRACK
(FUN_180476890) blocks resizing of hidden columns.

**Selection**: NM_CLICK on a valid item/subitem without Ctrl or Shift (OnNotify override
FUN_1804775c0 d2 L176-190) calls FUN_180479960. Picker branch (d3 L665-672):
UpdateData(TRUE), Number = col 0, Name = col 1, `DAT_1808a2d88` = level, UpdateData(FALSE).
This happens only if col 0 is non-empty (d3 L624-625). **Double-click is not OK**:
LevelListCtl::OnNMDblclk FUN_180248f50 (d4 L1905-1908, message map d5 L127-131) posts
"OnDoubleClick" for subitem 0. CSelLevel's handler (FUN_1804763b0 → FUN_180479c20 d3 L1278)
does nothing in picker mode. Subitem 1 is excluded from in-place edit (d4 L1910). Right-click
(FUN_180479d90 d3 L1367-1368) and the header right-click menu (FUN_180476970 d2 L1482-1488) do
nothing in picker mode. The Delete key (PreTranslate d2 L250-258) reads entity counts from
column 3 (objdump 1804756e4). Column 3 is never filled in picker mode, so nothing is deleted.
In the list, Ctrl+C/X/V call parent routines (FUN_180248540 d6 L33-58). Their effect in picker
mode is NOT DETERMINED.

## 3. Number (1359) / Name (1313) edits

- Init: Number = `DAT_1808a2d88` (MCLong range 1..2e9). Name = the level's name. Focus goes to
  Number with all text selected (FUN_18047b6b0 d3 L262-276).
- **Number**: on EN_KILLFOCUS (FUN_180476d10 d2 L388-393; skipped after Cancel, flag +0x7638),
  or on Enter (PreTranslate d2 L212-220, Enter is consumed), FUN_180477e30 runs if the edit was
  modified (d3 L478-592). Text that starts with '-' or gives n <= 0 is ignored. **If level n does
  not exist, it is created at once** with `LevelData::setLevel(n,1,1)` and a row is appended
  (d3 L485-512). Then `DAT_1808a2d88 = n`. The row is selected (and the list is rebuilt if it is
  not shown), and Name is set to the level's name (d3 L566-588). No error message is shown.
- **Name**: EN_UPDATE (every keystroke, FUN_180477b20 d2 L646-694) finds the row whose col 0
  equals the Number text, trims the new text, and writes it to row col 1 (picker) (d2 L669-672).
  It calls `LevelData::setName(n, new + ("~~~"+levelSet if any))`, so **the level is renamed
  immediately**, and refreshes the main-frame level UI (d2 L674-690). EN_KILLFOCUS/Enter
  (FUN_180476bb0 d2 L869-890) also calls setName for level `+0x59e8`. It keeps the old name's
  suffix after '~' (Ordinal_12380; exact tilde handling NOT DETERMINED).
  Cancel does not undo renames or created levels: sel_level_dialog restores only main and mask
  level (d1 L101-102).
- Validation strings: none fire in picker mode. Message 215 "Cannot renumber level %ld to level
  %ld..." belongs to the manager's in-place renumber (FUN_180476600 d2 L1317-1319).

## 4. Button 1327 (bitmap button)

- Bitmap resource **0x33F (831)** from the Mastercam resource handle, transparency 0xC0C0C0
  (FUN_18047b610 d3 L321-322).
- Click (FUN_180477120 d2 L1018-1074): BeginProcess, prompt string **opmanres 214 "Select
  entity"** (objdump 18047720c `mov $0xd6,%edx`). It suspends the dialog
  (`AfxSuspendThisWindow(this,0,6)`), sets cursor 0x6231, and calls
  `select_ent(prompt, …, 1, 0x1f1fff, …)`. Then it restores the cursor and resumes the window
  (L1026-1037). On success: the range is widened to include the entity's level (FUN_18047bf80
  d3 L927-936), the list is rebuilt, Number/Name show that level, `DAT_1808a2d88` = the
  entity's level, and the row is selected (d2 L1040-1072). It does **not** close the dialog and
  does not set the main level in picker mode (L1042-1043).

## 5. OK / Cancel / X / Help, title, persistence

- **OK (1)** → vtable slot 97 (thunk 180474b6c `jmp *0x308(rax)`) = FUN_180477690. It returns
  early if the dialog is pinned (+0x1449 && +0x1444==1) (d2 L116-118). Then: UpdateData(TRUE),
  apply the visibility diff (FUN_18047c2f0), `db_has_changed()`, **`DAT_1809af311 = 1`**, and
  DestroyWindow (d2 L120-143). The result is `DAT_1808a2d88`, the last
  clicked/typed/picked level. OK does not re-read the Number edit text (it goes through
  UpdateData), so a Number value that was never committed via kill-focus/Enter is NOT DETERMINED.
- **Cancel / X**: OnCancel slot 98 (FUN_1804762f0 d2 L153-159) and WM_CLOSE (thunk 180474b78
  `jmp *0x380`) both reach slot 112, FUN_180476310. It sets flag +0x7638 (which suppresses Number
  kill-focus) and calls DestroyWindow (d3 L10-14). It does not set `af311`. Alt+Z posts WM_CLOSE
  (PreTranslate d2 L283-285).
- **Help (9)**: there is no handler in the CSelLevel message map (map rebuilt from 1804757c0, entries
  at 1808a34f0..1808a3970). Behaviour comes from the base class MCDialog: NOT DETERMINED.
- **Title**: the resource caption "Levels" (opmanres_dialogs.tsv). No SetWindowText was seen in the
  decompiled CSelLevel picker paths.
- **Persisted (registry, key above)**: "Levels Selection Dialog Window Position" as
  "top,left,bottom,right" (save FUN_18047aed0 d3 L391-403; load FUN_18047ab60 d3 L49-59 + FUN_180479350
  d3 L112-154, only if it intersects the screen), the range low/high, "Mini sort column/direction"
  and the column widths described above. Saved when the window is destroyed (FUN_180474f20 d2 L359).
  The radio is kept only in the `list_level` global (NOT DETERMINED whether it is written to config).

## 6. Code: viewer/core_ui/cui_analyze.c (`level_dialog`, `ldlg_*`; moved out of viewer/mcamui.c in Qt-port phase 8 - mcamui.c draw_level_dialog / viewer-qt dialogs8.cpp LevelPicker draw it)
`level_dialog(cur, done)` opens the picker (start level = the caller's, 1 if 0); caller: the Analyze Level button 25261
(formats/analyze.md; also the scripted "azlevel" step). Drawn from template 563: list 1371 (Number / Name, opmanres 1979 /
1948; main level row yellow, green when selected), Selected Level 1375 (Number / Name), List Levels 1376 (Used /
Named / Used or named / Range with the range edits 744 / 301, filters as FUN_18047c950, Number ascending except for
the Range radio), button 1327 (pick an entity, prompt opmanres 214 "Select entity"; its bitmap 0x33f not drawn), OK /
Help; X = Cancel. The radio and the range come from the user's CONFIG/Mastercam.Workspace (LevelListDisplay /
Levellistlowrange / Levelhighlowrange). Click = Number / Name + result; typing a number selects (creates) that level.
NOT DECODED here: level names (Name column empty, "Named" = the main level only, name edit greyed); the registry
persistence of sec. 5 (window position, sort, widths) is not reproduced.

## Level visibility in this program (2026-11-05, decision NS-2 follow-up)
- **Drawing and picking**: LevelData::isVisible @180245fc0 gates draw_ent and the display-list walks (formats/nesting_addin.md section 8,
  "Isolation by level visibility"; viewer/mcam_load.h mcam_level_visible).
- **On load** (viewer/mcam_load.c lvl_record): a File > Open that is no merge runs dbinit before reading (mccore FUN_1803509a0: `if
  (!merge) dbinit()` before get_db, decomp/levels/mccore_dbinit_open.c; dbinit @1802c4770 -> LevelData::resetLevels @180246980: list
  emptied, main_level = 1, mask_level = 0, level 1 visible - decomp/mastercam_filenew.c 437-461). The level record then goes through
  FUN_1802d5220 (decomp/levels/core_levels.c 1-105): its main level not 1 -> level 1 hidden; GetOnlyMainVisible (mccore DAT_1813c43fc,
  zero-initialised .bss, set only by the Level Manager's check box handlers, decomp/level_picker/d2.txt 135 / 485) is off, so the
  record's visible levels are made visible; the named levels made (hidden when new) and named; set_main_level(record main) - the
  program's main level (geo_level) after the open (core_ui cui_doc.c); the main level hidden and main_level_vis = 0 when it is not
  in the visible list. Levels of later records missing from the list are made hidden while the file is read (FUN_1802c4010). No
  rule here is inferred any more.
- **A level written in place** (Change Level, Analyze's level): FUN_1802c4010 outside a load makes a missing level, visible
  (mcam_load.h mcam_level_store).
- **Levels manager Visible column** (viewer-qt managers.cpp, core_bridge.c qc_level_vis_click): the mark = isVisible
  (FUN_180249e00(row, 1, ..), decomp/level_picker/d4.txt 864-865; its glyph NOT DECODED: a check state); NM_CLICK on an item without
  Ctrl / Shift (FUN_1804775c0, d2.txt 164-192) -> FUN_180479960 column 1 (d3.txt 602-680): the new state = not the mark; FUN_18047bba0
  (d3.txt 1630-1660): the main level stays visible while main_level_vis is on (mccore export main_level_vis, .data initial 1, the
  "main level visible" check box +0x2988), else setLevel(level, new state); the graphics redrawn (FUN_18047bcc0). Not ported: the
  check boxes themselves ("only main visible", "main level visible"), the Number column click (make main), the other columns.
  Scene wf levels_vis_toggle (step levelvis:N, ours).
- **Corpus check** (<user-parts>, 17 parts, the scratchpad tool lvlscan over mcam_load): no part shows fewer entities -
  8 parts have main level 1 and one level; the 9 buckle parts have main level 6 with levels 1..6 all in the record's visible list
  (level 1 is hidden by the main-level rule, then made visible again by the list). Every entity is on a visible level.
