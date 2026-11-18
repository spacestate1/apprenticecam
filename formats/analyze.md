# Mastercam 2017 "Analyze Entity Properties": Point / Line / Arc dialogs (spec)

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

Sources (all mccadutil.dll unless stated). The decomp files are in `decomp/analyze/`:
`first.c` (CAnalyzeFirstDlg), `base.c` (CAnalyzeDlg + SecondDialog), `pt.c`, `ln.c`, `arc.c`, `helpers.c`,
`ctors.c`, `misc.c`, `b600.c` (factory), `realan.c` (mccore.dll MCRealAn), `bmpcombo.c` (mastercam.dll), plus the
vtable dumps `vt.txt`, `v_First.txt`, `v_Dlg.txt`, `v_Sec.txt`, `v_Point.txt`, `v_Line.txt`, `v_Arc.txt`.
The command loop is `decomp/mccad_cmds/AnalyzeStart.c` (cited as `AS.c:L`).
Bitmaps were extracted and viewed (`bmp/sheet.png`, `bmp/sheet2.png`) and dialog styles were read from the RC template
(`pe_dlg_style.py`): those two are scratch-only (session scratchpad, not kept in the repo).

## 0. Class hierarchy and vtable slots

RTTI: `CAnalyzeDlg` -> `CAnalyzeFirstDlg` -> {`CAnalyzePoint`; `CAnalyzeSecondDialog` -> {`CAnalyzeLine`, `CAnalyzeArc`}}.
Vtables: Dlg 180184868, First 1801858e8, Second 180188630, Point 180187c50, Line 1801871c0, Arc 180180c38.

| slot (+off) | role (from use) | Dlg | First | Second | Point | Line | Arc |
|---|---|---|---|---|---|---|---|
| 66 (+0x210) | DoDataExchange | MCDialog | 180081d50 | 180086b80 | 180086390 | 180084f80 | 1800738f0 |
| 95 | OnInitDialog | 18007ea60 | 1800824d0 | 180086c90 | 180086560 | 180085470 | 180074180 |
| 97 (+0x308) | OnOK / finish | 18007ee50 | 1800825a0 | = | = | = | = |
| 98 (+0x310) | OnCancel | 18007e950 | 1800823a0 | = | = | = | = |
| 112 (+0x380) | "end from command loop" | 18007df90 | 180081c70 | = | = | = | = |
| 113 (+0x388) | Apply (live) | 18007f130 | 180082ee0 | = | = | = | = |
| 114 (+0x390) | init control bitmaps | 18007e940 | 180081ef0 | 180086bf0 | 180086440 | 1800850f0 | 180073a80 |
| 115 (+0x398) | entity -> dialog | 18007e8c0 | 180081d00 | 180086b20 | 180086310 | 180084d70 | 1800737b0 |
| 116 (+0x3a0) | dialog -> entity | 18007f5c0 | 180083000 | 180086cb0 | 180086580 | 180085940 | 180074b50 |
| 117 (+0x3a8) | WM_CLOSE target | 18007e960 | 180082420 | = | = | = | = |
| 118 (+0x3b0) | compact single-entity layout | - | 180082140 | = | = | = | = |

(VtableDump output; "=" means the slot is inherited from First.) Slot names are my labels, inferred from what the code does.

Message maps (AFX_MSGMAP entries; code 0x200 = EN_KILLFOCUS, 1 = CBN_SELCHANGE):
- CAnalyzeDlg (180184da0): WM_CLOSE -> 18007db4c = `jmp [vt+0x3a8]` (slot 117 -> 180082420 -> slot 98 = Cancel); IDOK -> 18007db40 = `jmp [vt+0x308]`.
- CAnalyzeFirstDlg (180185dc0): 875 Next -> 180082580; 877 Previous -> 1800829b0; 25261 Level -> 180082500; 25114 Color -> 180082430; 891 Propagate -> 1800829f0; EN_KILLFOCUS 430 (level), 25117 (color) -> 180073358; IDOK -> 180081c34 (`jmp [vt+0x308]`).
- CAnalyzeSecondDialog (180188a50): CBN_SELCHANGE 895, 896 -> 180073358.
- CAnalyzePoint (1801880d0): 867 -> 180086510; EN_KILLFOCUS 413, 419, 869 and CBN_SELCHANGE 864 -> 180073358.
- CAnalyzeLine (180187700): EN_KILLFOCUS 892, 893, 894, 437, 441, 873, 25056, 897, 899 -> 180073358; 901 -> 180084a90; 870 -> 180084c00.
- CAnalyzeArc (180181190): EN_KILLFOCUS 437, 441, 873, 25367, 415, 25056, 904, 420, 897 -> 180073358; 867 -> 180073400; 870 -> 1800734d0; 843 -> 180073fd0.
- 180073358 is `mov rax,[rcx]; jmp [rax+0x388]` (disasm 180073358), so every listed edit/combo calls slot 113 (Apply) when it loses focus or its selection changes.

## 1. Opening, the selection loop, and closing

- `AnalyzeStart` (18008d050) calls `FUN_180089150(FUN_18008b600)` (AS.c:30).
- Pre-selection: `FUN_18008c8b0` reads `eptrs_from_sel(4,0x1f1fff,...)` into the CAnalyzeInfo list. If the list is not empty, the dialog opens for index 0 (AS.c:118-123).
- Prompt: `Ordinal_8010(..,0x18bd)` = mccadutilres 6333 "Select entities to analyze" (AS.c:124).
- While the list is empty, `get_event(prompt, ev, 0x4012, 0x5f1fff, ..)` runs (AS.c:129). Once a dialog exists the mask is 0x4212 (AS.c:195).
  - Single pick (ev 2) or solid pick (0x4000): the list is reset to that one entity and the dialog opens for index 0 (AS.c:131-153).
  - Window pick (0x10): the list becomes the window set and the dialog opens for index 0 (AS.c:156-186).
- Picks while a dialog is open:
  - Single pick (ev 2 / 0x4000): the current dialog is ended through slot 112 (AS.c:215).
    - If the picked entity is already in the list, it becomes current (AS.c:228-262).
    - Otherwise it is appended and a dialog opens for the new last index (AS.c:269-322).
  - Window pick (0x10): `FUN_18008c530` merges the window set into the list (AS.c:335; AS.c:977-1060).
    - Entities that were already in the list are removed from both sets and their colours are restored.
    - The rest are passed to `FUN_18008b4e0`, which was not decompiled; that they are appended is NOT DETERMINED.
    - The dialog then opens for the last index (AS.c:336-337).
- Highlighting (`FUN_18008c780`, AS.c:722-782): every list entity is rewritten in `auto_highlight_color`, and the current one in `select_color`. The real colours are kept in each list entry at +0xc and are written back by `FUN_18008ca30` (AS.c:879-924).
- Factory `FUN_18008b600` (b600.c). Entity type is the first int of the ent:
  - type 1: new(0x17f38), `CAnalyzePoint` ctor 1800861e0, IDD 0x431 = 1073 (b600.c:141-146).
  - type 2: new(0x22a78), `CAnalyzeLine` ctor 180084730, IDD 0x438 = 1080 (b600.c:147-153).
  - type 4: new(0x21dd0), `CAnalyzeArc` ctor 180072e30, IDD 0x1946 = 6470 (b600.c:157-162).
  - Other types get other dialogs. Examples: type 8 / 0x80 -> 0x194d (6477 Parametric/NURBS Spline); 0x40 -> dimension dialogs; 0x100 -> surface dialogs; 0x10000 -> 0x195e; 0x100000 -> 0x196f.
  - Any unhandled type returns NULL, and no dialog opens (b600.c:430-460, 467-468).
  - After construction: `vt+0x2d0(IDD, 0)` (Create, modeless), then `Ordinal_13643(dlg,5)` (ShowWindow SW_SHOW) (b600.c:462-466).
- Title: the RC caption ("Point Properties" / "Line Properties" / "Arc Properties").
  - If the picked line/arc is a solid edge (list entry +8 != 0; Line ctor ln.c:38-39, Arc ctor arc.c:46-47), OnInitDialog sets the title to mccadutilres 33218 "Linear Edge Properties" (ln.c:156-159) or 33219 "Arc Edge Properties" (arc.c:165-168).
  - In edge mode every value control, AutoCursor button, combo and Propagate is disabled. Level, Color, Style/Width and their labels are hidden, OK/Cancel/Help move up, and the window shrinks (ln.c:113-205, arc.c:124-214).
- Single-entity layout: if the list count is 1, `FUN_1800824d0` calls slot 118 (first.c:73-75). Slot 118 hides Propagate (891), Next (875), Previous (877) and separators 2961/2962. It moves OK/Cancel/Help and all children up by 0x20 and resizes the window (first.c:691-738).
- Constructor `FUN_180081860` (helpers.c:308-497):
  - Copies the entity into the working ent (+0xac98) and the original (+0xd5d0), and a snapshot into +0xff08.
  - Saves every list colour (+0x4f10 array).
  - Sets the OK flag `+0x4ec0 = 1` (helpers.c:366).
  - Starts an undo transaction if none is active and remembers that it did (+0x4f0a) (helpers.c:484-491).
  - CAnalyzeDlg ctor sets the default post code `+0x4ea8 = 0x930` (disasm 18007d9b9).
- Closing (`FUN_18007e960`, base.c:341-353): SaveWindowPosition. If +0x4ea8 != 0 it posts `PostMessage(0,0x8028,+0x4ea8)`. Then reset highlights, repaint, DestroyWindow.
- Command-loop handling of wParam (AS.c:340-418):
  - 0x930: restore list colours and end the command.
  - 0x931 / 0x932: Previous / Next (see §5).
  - 0x934: second phase of the Level button.
  - 0x933: NURBS spline only.
- Escape / right-click (get_event returns non-zero): slot 112 runs `FUN_180081c70`, which sets +0x4ea8=0 and calls slot 97 with the OK flag still 1, so pending changes are committed. Then the list colours are restored and the command ends (AS.c:196-206; first.c:39-47).
- The dialog window's X button (WM_CLOSE) behaves as Cancel (see above).

## 2. Common controls (CAnalyzeFirstDlg)

- DDX (first.c:52-64):
  - 430 Level edit: MCInt at +0x7ea8, value +0x8aa0. Range set to 1..2000000000 (first.c:439-440).
  - 25117 Color edit: MCByte at +0x8c10, value +0x8cec. Its range is set by `*(short*)(+0x8ced)=0xff00` (first.c:441); the exact byte semantics are NOT DETERMINED.
  - Subclassed buttons: 875 Next, 891 Propagate, 877 Previous, 25114 Color, 25261 Level.
- Button bitmaps (first.c:371-448):

  | control | bitmap | DLL | image |
  |---|---|---|---|
  | Color (25114) | 0x482 = 1154 | mccoreres | colour grid |
  | Level (25261) | 0x481 = 1153 | mccoreres | layer grid with tick |
  | Propagate (891) | 0x484 = 1156 | mccadutilres | blue asterisk circle |
  | Next (875) | 0x47b = 1147 | mccoreres | blue ▶ |
  | Previous (877) | 0x473 = 1139 | mccoreres | blue ◀ |

- XYZ labels (CAnalyzeDlg OnInitDialog 18007ea60, base.c:129-250): if +0x4ec4 is set, statics 2963/2965/2966 are relabelled from mccoreres 2197 "X", 2198 "Y", 2200 "Z", 2202 "D". If +0x4ec8 is also set, the delta statics 2967/2969/2971 are relabelled too. The labels depend on `MastercamPlanes::GetCoordinateDisplay`:

  | mode | 2963 / 2965 / 2966 | 2967 / 2969 / 2971 |
  |---|---|---|
  | 0 | X / Y / Z | X / Y / Z |
  | 1 | X / Z / Y | X / Z / Y |
  | 2 | D / Z / Y | D / Z / Y |

  - Point, Line and Arc ctors set +0x4ec4=1 (pt.c:13, ln.c:40, arc.c:48).
  - Only Line also sets +0x4ec8 (param[0x9d9]=1, ln.c:41).
- Coordinate conversion. `ConvertPoint` = `FUN_18007dfb0` (helpers.c:77-118):
  - Subtracts the active origin (`SubtractActiveOrigin(Planes,0)`), then `world_to_view` into `GetActiveViewNo(Planes,0)`.
  - Mode 1 swaps the first two components to (vy, vx).
  - Mode 2 gives (2*vy, vx); "D" is a diameter.
  - The inverse is `UnConvertPoint` = `FUN_18007f4f0` (helpers.c:123-158).
  - Which plane index 0 is (Cplane/WCS) is NOT DETERMINED.
  - No unit conversion appears anywhere: values are DB units, and angles are degrees.
- Number formatting: every numeric field is an `MCRealAn` (mccore.dll). `DDX_ProcessEditRealAn` (realan.c, 1804dc080):
  - Displays `make_mcreal_str(value, .., *n_places_an, +0xd88, +0xd9c)`.
  - The default ctor sets +0xd88 (sign flag) = 0 and +0xd9c = 1, so the format is `"%.*f"` with `n_places_an` decimals, trailing zeros stripped but at least one digit kept after '.'. |v| >= 1e38 shows "$$$$$$$$$$$$$$$" (make_mcreal_str).
  - Input goes through `SelectionCue`, then `UIEdit::Evaluate` (expressions allowed).
  - If the value is outside [+0xbf0 min, +0xbf8 max], the old value is restored and `InvalidRealField` is called. Default min/max are ±1e38 (MCReal::MCReal).
  - Typing "?" posts a WM_RBUTTONDOWN.
  - `CheckChanges` returns 1 if the text changed since the last check (realan.c MCRealAn::CheckChanges).
  - Which config setting feeds `n_places_an` is NOT DETERMINED.
- Style / Width combos (CAnalyzeSecondDialog, Line+Arc). DDX 896 Width = CBmpCombo2 at +0x12840; 895 Style = CBmpCombo2 at +0x14578 (base.c:28-35).
  - Filled by `CBmpCombo2::AddLineStyleImages` / `AddLineWidthImages` (base.c:71-72).
  - Items are bitmaps, not strings (mastercam.dll 1801035a0 / 180103670, operand disasm at 1801035dc..180103630, 1801036ac..180103700):
    - Style: 0x61b7..0x61bb = 25015..25019, which in mccoreres show solid, hidden (dash), center, phantom, zigzag.
    - Width: 0x61bd..0x61c1 = 25021..25025, five line thicknesses.
  - Entity -> dialog: `CB_SETCURSEL(style-1)` from ent+0x18 (short), and `CB_SETCURSEL((int)width-1)` from ent+0x10 (double) (base.c:83-91).
  - Dialog -> entity: style = sel+1; width = (double)(sel+1) (base.c:96-107).
  - Point style combo 864 (Point dialog): CBmpCombo2 at +0x12840 with `AddPointStyleImages` (pt.c:69). Bitmaps 0x61c9..0x61d0 = 25033..25040 (8 items; disasm 18010377c..18010380f; star, dot, plus, X, circle, square, two further glyphs).
    - Entity -> dialog: sel = ent+0x1c (short) - 1 (pt.c:91).
    - Dialog -> entity: ent+0x1c = ent+0x18 = sel+1 (pt.c:122-125).
  - That GetMastercamResourceHandle resolves to mccoreres.dll is NOT DETERMINED; images with these IDs exist there.
- Base dialog -> entity `FUN_180083000` (first.c:474-672):
  - ent level (+0xaca0) = Level edit. A missing level is created with `LevelData::setLevel(..,1,1)`, then UpdateLevelsManager.
  - The Color edit value is stored in the list entry colour (not in the ent).
  - If the working ent differs from the last snapshot (`FUN_180082f70`), the entity is rewritten in the DB (`rewrite_ent(ent,0)` plus drafting regeneration, `FUN_180082f30` first.c:653-667).
  - Change bits are ORed into +0x4f38 (and +0x4f3c): 1 = point style; 4 = line style (types 2/4/8/0x80); 8 = width; 2 = types 0x10/0x20 only. Then the snapshot is updated.
- Apply (slot 113, `FUN_180082ee0`, first.c:357-366), skipped while closing (+0x1550):
  1. `draw_ent_erase`.
  2. `FUN_18007f130`: `UpdateData(TRUE)`, then slot 116 (dialog->entity), then slot 115 (entity->dialog, recomputing every derived field) (base.c:296-308).
  3. `draw_ent`.
  - This is LIVE: it fires on EN_KILLFOCUS / CBN_SELCHANGE.
- Level button (`FUN_180082500`, base.c:358-380).
  1. The first click sets a static flag and posts 0x934.
  2. The command loop calls the handler again, which opens `sel_level_dialog(current level)`.
  3. On OK: Level edit = result, UpdateData(FALSE), Apply.
- Color button (`FUN_180082430`, base.c:385-406):
  1. Temporarily restores the real list colours, then opens `sel_color_dialog(current list colour)`.
  2. On OK: +0x8cec = colour, UpdateData(FALSE), Apply.
  3. Re-highlights the list.
- Propagate (`FUN_1800829f0`, base.c:415-621). The sequence:
  1. Apply.
  2. For every other entity in the list: if the colour changed since open or the last propagate, copy the current list colour into that entity's list entry (`FUN_180082dc0`). If its level differs, set it to the dialog level (`FUN_180082e20`) and `rebuild_graphics` later.
  3. Apply the change-bit attributes:
     - bit 1, point style (`FUN_180082eb0`, points only);
     - bit 4, line style (`FUN_180082e30`, types 2/4/8/0x80);
     - bit 8, width (`FUN_180082e60`);
     - bit 2 (`FUN_1800829d0`, types 0x10/0x20).
  4. `rewrite_ent`, with `put_special_undo(3,..)` when this dialog owns the undo transaction (misc.c).
  5. Copy the current working ent to original and snapshot, and clear the bits.
  - Scope: Propagate affects only entities in the analyze list.
- OK (IDOK -> slot 97 `FUN_1800825a0`, first.c:85-312). The sequence:
  1. Dialog -> entity.
  2. "Changed" means: the real colour differs from the original colour (+0x4ef1), or the accumulated bits (+0x4f3c) are non-zero, or the ent differs from the original.
  3. With the flag set (OK) and changes present:
     - `put_special_undo(3, original)`;
     - set the ent colour to the real list colour;
     - `rewrite_ent`, UpdateLevelsManager;
     - for non-solids, `list_op_check_ent` + `mc_op_test_and_modify_entity` (operation associativity);
     - `draw_ent` (first.c:281-293).
  4. Undo: `UndoTransCommit` if changed or propagated, else `UndoTransTerminate` (first.c:298-306).
  5. Then close. With the default code 0x930 the command ends.
- Cancel (slot 98 `FUN_1800823a0`, first.c:317-352). The sequence:
  1. OK flag = 0.
  2. Restore all list colours from the saved array, then call slot 97.
  3. With the flag 0 and changes present:
     - if the entity pointer changed, delete the new entity and restore the list pointer; otherwise erase it;
     - rewrite the ORIGINAL ent copy, draw it, and copy it back to the working ent (first.c:204-279);
     - `UndoTransTerminate` (first.c:299-301).
  4. Close; the command ends (0x930).
  - Whether `UndoTransTerminate` also reverts level/style/width already propagated to OTHER entities is NOT DETERMINED. Propagated colours are reverted, because they live in the list.
- Help (id 9): no handler in these maps. Its behaviour is NOT DETERMINED (MCDialog/MFC default).

## 3. Point Properties (1073)

- Constructor and DDX: ctor 1800861e0; DDX 180086390 (pt.c:20-30).
- Fields (value = MCRealAn object + 0xc00), ent -> dialog 180086310 (pt.c:80-94):
  - 413 = X, 419 = Y, 869 = Z: `ConvertPoint(ent point +0xad00)`. Editable (style 0x50810080, no ES_READONLY).
  - 864 = point style combo.
  - Level/Color as in §2.
- Dialog -> entity `FUN_180086580` (pt.c:99-128): if any of 413/419/869 changed, the displayed triple is put through `UnConvertPoint` into the point (+0xad00) (disasm 1800865cc..1800865f8). Then style, then base apply.
- AutoCursor 867 (bitmap 0x15a1 = 5537, 4-way move cross, pt.c:66-68), `FUN_180086510` (pt.c:133-147):
  1. `FUN_18007dc20` suspends the dialog and runs `get_event` with mccadutilres 5342 "Select New Point Position", mask 1 (helpers.c:5-72). The picked world point is written directly into the point.
  2. Entity -> dialog, Apply.
  3. Post 0x8028,0.
- Statics #1082: the 145x1 separator line bitmap (mccoreres 1082).

## 4. Line Properties (1080)

DDX `FUN_180084f80` (ln.c:48-67). Entity -> dialog `FUN_180084d70` (ln.c:340-401). Start P1 = ent+0xad00, end P2 = ent+0xad18.

| ctrl | value | editable (RC style) |
|---|---|---|
| 892 / 893 / 894 | start X / Y / Z = ConvertPoint(P1) | yes |
| 437 / 441 / 873 | end X / Y / Z = ConvertPoint(P2) | yes |
| 25056 | angle (deg) = atan2 of the view-projected delta P2-P1 in the active view, normalised to [0,360), 0 if the 2D length² < 1e-12 | yes |
| 897 | 3D length = world \|P2-P1\| | yes |
| 899 | 2D length = \|delta\| projected into the active view | NO (0x50810880 has ES_READONLY) |
| 874 / 878 / 880 | \|dX\| / \|dY\| / \|dZ\| of the converted end-start | NO (ES_READONLY, no handlers) |

- `FUN_18007f220` places GrMarker3d markers at P1/P2, or a zero-length marker (helpers.c:163-262).
- Dialog -> entity `FUN_180085940` (ln.c:406-633). The first changed field in this else-if order wins:
  1. 899 2D length (unreachable from the UI because the field is read-only): scales the converted delta (including Z) by L2D/old.
  2. 897 3D length: if new L > tol, P2 = P1 + (P2-P1)*L/old. P1 is kept. If L <= tol, AfxMessageBox mccoreres 5011 "Warning - zero length line" and no change (ln.c:458-512, 570-573).
  3. 25056 angle: P2 = view_to_world(P1v.xy + L2D*(cos a, sin a), z = P1v.z) in the active view. P1 and the 2D length are kept; P2's view depth is set to P1's (ln.c:514-530).
  4. 892, 893, 894, 437, 441, 873: the edited component goes into the converted point, which is put through `UnConvertPoint` into P1 or P2 (ln.c:532-625).
  - Then `FUN_180086cb0` (style/width + base apply).
- AutoCursor buttons:
  - 901 (bitmap 0x4a2 = 1186 "+1"), `FUN_180084a90`: rubber band from P2, picks a new P1 (prompt 5342).
  - 870 (0x4a3 = 1187 "+2"), `FUN_180084c00`: rubber band from P1, picks a new P2.
  - If the resulting length < tol, the old point is restored and message 5011 is shown. Then entity -> dialog and Apply (ln.c:638-751).
- Statics (runtime images, ln.c:216-335):

  | static | image |
  |---|---|
  | 905 | 0x4a5 = 1189 (green "+" -> red "+": start/end header) |
  | 908 / 909 / 910 | 0x439 = 1081 "Δ" (delta labels) |
  | 906 / 907 | 0x6272 = 25202 (arrow over ruler: 3D/2D length icons; the RC "#1178" is replaced) |
  | 25057 | 0x6271 = 25201 (angle icon; RC "#1172" replaced) |
  | #1082 | separator |

## 5. Previous / Next

- Next (875, disasm 180082580): +0x4ec0=1, +0x4ea8=0x932, then slot 97. Previous (877, 1800829b0): +0x4ec0=1, +0x4ea8=0x931, then slot 97. Both commit like OK.
- The command loop then steps the index through the analyze list with wrap: Next goes last -> 0, Previous goes 0 -> last. It re-highlights and opens the dialog for the new entity (AS.c:370-412).
- The list is the pre-selection, picks and window picks of this Analyze session (§1).
- Both buttons are hidden when the list has one entity (§1).

## 6. Arc Properties (6470)

- DDX `FUN_1800738f0` (arc.c:55-73). Entity -> dialog `FUN_1800737b0` (arc.c:412-441).
- a_3d at ent+0xad00: ep1 +0xad00, ep2 +0xad18, centre (view coords) +0xad30, z +0xad40, r +0xad48, sa +0xad50, sweep +0xad58 (radians), view +0xad60.

| ctrl | value | editable |
|---|---|---|
| 902 | arc view number (MCInt) | NO (0x50010881 read-only) |
| 437 / 441 / 873 | centre X / Y / Z = ConvertPoint(view_to_world(centre, view)) | yes |
| 25367 | radius r | yes |
| 415 | diameter 2r | yes |
| 25056 | sweep (deg) | yes |
| 897 | 3D arc length \|sweep\|*r | yes |
| 904 | start angle (deg) = sa | yes |
| 420 | end angle (deg) = sa+sweep, not normalised | yes |

- Markers at the endpoints and centre (`FUN_18007f220`, `FUN_18007f170`).
- Radius and diameter minimum = tol (+0xbf0 set in arc.c:121-123).
- Dialog -> entity `FUN_180074b50` (arc.c:446-528). After style/width/base apply, the first changed field wins:
  1. 415: r = D/2.
  2. 25367: r.
  3. 25056: sweep = deg.
  4. 904: sa = new, sweep -= (new - old), so the end angle stays fixed.
  5. 420: sweep = end - sa.
  6. 897: if \|L\| < tol, L is reset to the MCRealAn previous value (+0xd78); sweep = L/r (sign of the old sweep cancels).
  7. 437 / 441 / 873: UnConvertPoint, then `world_to_view(arc view)` into the centre (including depth).
  - Then, if \|sweep\| > 2π: sweep = 2π, end field = 360.0000000000001, length = 2πr.
  - Endpoints are recomputed with `arc_ep_3d`, followed by style/width/base apply again.
- AutoCursor 867 "Use the AutoCursor to move arc" (bitmap 5537), `FUN_180073400` (arc.c:533-558): rubber band 0xc (r, sa, sa+sw) in the arc's view, prompt 5342. The picked point becomes the centre (world_to_view in the arc's view).
- AutoCursor 870 "Use the AutoCursor to resize arc" (5537), `FUN_1800734d0` (arc.c:565-658):
  1. The dialog is suspended. The active plane is temporarily the arc's view matrix and the construction depth is the arc z.
  2. Rubber band 8 from the centre, prompt 5342.
  3. r = rubber-band radius if > tol.
  4. Plane and depth are restored.
- Flip view 843 (bitmap 0x463 = 1123, two arrows across a line), `FUN_180073fd0` (arc.c:665-721):
  1. Builds the matrix with the arc view's rows +0x50 and +0x60 negated (row +0x58 kept).
  2. `find_view_m(..,1,..)`; if found, `map_arc_to_parallel_view(ent, view)` + `rewrite_ent`.
  3. Entity -> dialog.
  - Whether `find_view_m` creates a missing view is NOT DETERMINED.
- Statics (runtime images, arc.c:227-407):

  | static | image | meaning |
  |---|---|---|
  | 911 | 0x6270 = 25200 (quartered circle) | centre icon; RC "#1119" replaced |
  | 914 | 0x468 = 1128 | radius |
  | 915 | 0x4bd = 1213 | diameter |
  | 25057 | 0x6271 = 25201 | sweep/angle |
  | 916 | 0x6272 = 25202 | 3D length |
  | 917 | 0x469 = 1129 | start angle |
  | 918 | 0x46a = 1130 | end angle |
  | #1082 | | separators |

  - `FUN_18007dc10(dlg, 0x395/0x388, 0x22d8, 1)` and `(0x396/0x1a4, 0x22d9)` are empty stubs (`ret`, helpers.c:502-506). No tooltip is set.

## 7. Code: viewer/core_ui/cui_analyze.c (Analyze, `az_*`; moved out of viewer/mcamui.c in Qt-port phase 8 - mcamui.c draw_analyze / viewer-qt dialogs8.cpp AnalyzeDialog draw it)
Templates 1073 / 1080 / 6470 drawn at their dialog-unit positions (exports/mccadutilres_dialogs.tsv). Entry: command
AnalyzeStart (F4) -> `az_start` (the pre-selection, else pick an entity, prompt mccadutilres 6333).

| function | behaviour | spec |
|---|---|---|
| `az_fill` | entity -> dialog (slot 115); values "%.*f" with config 29003 places, trailing zeros cut | sec. 2 |
| `az_apply` | dialog -> entity (slot 116) on kill-focus; the first changed field wins (line / arc rules), then refill | sec. 4, 6 |
| `az_rewrite` | rewrites the entity as a new record (mod_del + new) in one mod transaction | sec. 2 |
| `az_close(commit)` | OK / Esc commit (one undo step); Cancel / X restore the original | sec. 1 |
| `az_step` | Previous / Next: commit, next list entry with wrap; hidden for one entity | sec. 5 |
| `az_propagate` | level (and a changed colour) to the other listed entities | sec. 2 |
| `az_autocursor_pick` | AutoCursor 867 / 901: new point position (prompt 5342) | sec. 3, 6 |
| `az_level_done` / `az_color_done` | Level button 25261 -> level picker (formats/level_picker.md), Color 25114 -> Colors picker | sec. 2 |

Here: values relative to Top with origin 0 (the only active plane), the rewritten entity keeps the Top view number.
Not done (greyed, per the code comment): Style / Width combos (line style and width are not modelled), arc resize
AutoCursor 870 (FUN_1800734d0), Flip view 843, Help, the dialog position. (The code comment still lists the Level
picker as not done, but `draw_analyze` opens it for button 25261.)

## 9. Double-click and the solid dialog (2026-10-24)
- **Double-click** in the graphics window with no command running opens the properties dialog of the point / line / arc under the
  cursor (as AnalyzeStart with it picked: the pre-selection rule applies), else of the part solid under it (core_ui
  `analyze_dblclick`, Qt MainWindow::viewClickIn). Source: the user's report of Mastercam's behaviour. Decoded part of the path
  (decomp/dblclk/README.txt): UiView OnLButtonDblClk -> message 0x802f -> mccore FUN_1804e3a10 (a dblclk event, MCEvent type 0x800,
  outside general selection / chaining) or FUN_1804e3af0; the idle consumer of the event is NOT DECODED.
- **Solid dialog** = the factory's class 0x10000 case: CAnalyzeSolid (FUN_180086d20, base CAnalyzeFirstDlg), mccadutilres dialog
  6494 (`AZ_6494`, height 154). OnInitDialog FUN_180086f30: title = opmanres 27601 + mc_op_getType(solid, 0) (the first op's type
  name, e.g. "Extrude"; ours: "Solid" when the history is unreadable); 1807 = sld_get_description ("%s", editable, DDX FUN_180086e40);
  416 "# of Operations" = sld_get_num_operations ("%d"); 1811 = mccadutilres 6317 "Closed Solid Body" / 6318 "Open Sheet Body" by
  is_solid_a_sheet (PK_BODY_ask_type != solid; here the XT's BODY.body_type, a byte field, != 1: occt_xt_body_type); Level / Color
  as the base dialog; one entity -> Previous / Next / Propagate hidden (slot 118). OK / Apply (slot 117 FUN_180086eb0):
  sld_set_description (at most 0x100 bytes) and the Solids manager row re-added (the tree shows the new description). 416 / 1811
  read-only [inferred: they are filled by SetWindowText]. Cancel restores the values at open; no undo step (NOT PORTED).
- Saved: mcam_saveops.c `solid_patch` writes the description into the SOLIDXMT trailer's name field (0x40 / 0x100 bytes) and the
  SOLID_ID record's colour (+0x06) / level (+0x08) in place (test-cube: 33 bytes change - those fields only); project line
  `SOLIDATTR sld_N level colour description` (ours). The solid's display colour is not taken from the record here (NOT PORTED).
- Tests: misc scenes az_solid_open, az_solid_edit, az_dbl_solid, az_dbl_line (MCAMUI_EVENTS `dblclick` / `dblclickw`, ours);
  script step `azsolid:N` (ours).
