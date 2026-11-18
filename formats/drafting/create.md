# Drafting - how Mastercam 2017 CREATES dimensions and notes (first command set)

Scope: ribbon Drafting tab -> **Horizontal, Vertical, Parallel, Smart Dimension, Note (note / label / leader-only)**; **Circular** (sec. 8,
2026-10-27); the state of the other Dimension-group buttons (sec. 9).
Display and record layouts are in `draw.md` / `records.md` / `text.md` (same folder); this file is the interaction flow,
the defaults and what ends up in the record. Rule: every statement cites a decompile line, a resource id, the user's config
or the help; **NOT DECODED** = not found, **hypothesis** = labelled guess (never to be implemented without confirmation).

Sources (all new, gitignored, under `<repo>/decomp/draft/`; "file:line"):

| file | DLL | contents |
|---|---|---|
| `cad_rundraft.c` | mccad | RunDraftCommand, RunDraftDragging, OnUpdateDraftingToolbar, FUN_1800392d0 (command switch), FUN_18003f7f0 (dimension command loop), FUN_18003d410 (Note command), FUN_180038c50 (drag-edit a fresh entity) |
| `cad_dimcmd.c` | mccad | FUN_180040ed0 (per-command start), FUN_180048b10 / FUN_180048940 / FUN_180048be0 (smart state machine dispatch), function-panel accessors FUN_180038ad0/af0/b70, FUN_180258090/FUN_180258d80/FUN_180259080 (DraftingSmartDimensionsManager get/set) |
| `cad_linear.c`, `cad_linear2.c`, `cad_linear3.c` | mccad | first/second pick (FUN_180049ee0, FUN_18004a620, FUN_18004b070), prompts (FUN_180049560, FUN_180043660), linear drag state (FUN_180042d30 events, FUN_180042e50 mouse move, FUN_1800442d0 entity pick during placement), store (FUN_180043280), panel keys (FUN_18012ba20, FUN_18012af20, FUN_180043cc0), angle entry (FUN_180044090), Height (FUN_18012b4b0) |
| `cad_note.c` | mccad | note/label placement (FUN_1800389a0, FUN_1800387f0), note drag (FUN_180045180/310, FUN_180046110), prompt FUN_180045a00 |
| `core_create.c`, `core_create2.c` | mccore | determine_hvp, determine_h_or_v, df_angle_hv, df_calc_sketched_point, df_save_drag_ent, init_attribs_dim/note, DraftGlobalsSave/Restore/..., ToDim/ToNoteGlobalsFromDraft, DisplayNoteDlg, doNoteDlg, DfCheckAngle, FUN_180241810 |
| `core_notedlg.c` | mccore | CNoteDlg (ctor FUN_1805bf460, DoDataExchange FUN_1805bf7d0, OnInitDialog FUN_1805bfc00, radio handlers, OnOK FUN_1805c0370) |
| `core_cfgdraft.c` | mccore | the config **writers** of the five drafting sections (FUN_1801d44c0 / d49c0 / d6530 / d86a0 / d4850): exact item number -> global |
| existing | mccore | `core_calcdim.c` (calc_linear_dimension 797, AddLeaders 203, FUN_180250410 5314, FUN_180251290 5640), `core_dimlayout.c` (CalcLabel 1036), `core_draft_rec2.c` (DfCreateLabel 184), `core_draftwf.c` (InitNOTE 234, calc_note 394, add_a_leader 6394) |

Strings: `regress/str/all.tsv` (mccadres / mccoreres ids). Dialog templates: `tools/pe_dialogs.py mccoreres.dll`.
Function panel UI: `~/mastercam-re/dotnet/mccad.ui/drafting/draftingsmartdimensionsmanagerview.baml` (WPF; strings only) +
`MCCAD.UI.Drafting.DraftingSmartDimensionsManagerViewToolTips.resx`. Help: `~/mastercam-re/chm/mccore/Drafting/*.htm`.

---------------------------------------------------------------------------------------------------------------------------

## 1. Command dispatch (common to every Drafting button)

* Ribbon exports are 16-byte thunks `mov dl,1; mov ecx,ID; jmp RunDraftCommand` (objdump mccad.dll 0x180041960-0x180041b07):
  `doDraft_DIMENSION_ANGULAR 0x9472, _BASELINE 0x9473, _CHAINED 0x9474, _CIRCULAR 0x9475, _HORIZONTAL 0x9476, _ORD_ALIGN 0x9477,
  _ORD_EXIST 0x9478, _ORD_HORIZ 0x9479, _ORD_PARA 0x947a, _ORD_VERT 0x947b, _ORD_WINDOW 0x947c, _PARALLEL 0x947d,
  _PERPENDICULAR 0x947e, _POINT 0x947f, _TANGENT 0x9480, _VERTICAL 0x9481, GLOBALS 0x9482, HATCH 0x9483, LEADER 0x9484,
  MULTI_EDIT 0x9485, NOTE 0x9486, REGENERATE_ALL 0x9487, _AUTOMATIC 0x9488, _SELECTION 0x9489, _VALIDATE 0x948a, SMART 0x948b,
  WITNESS 0x948c` (same table by name in OnUpdateDraftingToolbar, cad_rundraft.c:87-270). Second arg (`dl=1`) = "allow
  modifying the just-created entity" (stored at app+0x5a, FUN_1800375a0 cad_rundraft.c:379; used §2.6).
* RunDraftCommand -> FUN_1800392d0 (cad_rundraft.c:290): **0x9476 H, 0x9481 V, 0x947d P, 0x948b Smart** (and 0x9472, 0x9475,
  0x9477, 0x947e, 0x947f, 0x9480, id 99) all run **FUN_18003f7f0** (the dimension loop); 0x9486 Note runs FUN_18003d410;
  0x9484 Leader FUN_18003caa0; 0x948c Witness FUN_1800408f0. On exit: ClosePrompt; if result 2: df_erase_drag_ent,
  df_erase_phantom, repaint.
* Toolbar enable (OnUpdateDraftingToolbar cad_rundraft.c:242-262): the creation buttons are enabled unconditionally;
  globals/regenerate only when no drafting command is active (`DfActive`).

## 2. Horizontal / Vertical / Parallel / Smart - the dimension loop (FUN_18003f7f0, cad_rundraft.c:2964)

### 2.1 Set-up
* Refuses to start if `DfActive` (l.3110). Loads `MCCAD.Interop.dll` (FUN_1800b0d50 cad_dimcmd.c:657), gets the
  **`IDraftingSmartDimensionsManager`** view controller (FUN_1800b33f0 "ViewControllerFactory") and creates the
  **FunctionPanelDialog** (`FunctionPanelDialog::CreateFunctionPanelDialog`) unless one is already open (nested call),
  then shows it (Ordinal_4282(panel,1)). The same panel is used by all dimension commands and by Note (§4).
* Per-command start = **FUN_180040ed0** (cad_dimcmd.c:441) -> `FUN_180048b10(type, sub)` (cad_dimcmd.c:1361) which stores
  `DAT_1806496bc = type`, `DAT_1806496c0 = sub`, then FUN_18004b820 (cad_linear.c:904): `InitDIM(drag_ent)`,
  `DfHvpAngle = 0`, pick counter `DAT_1806960e8 = 0`, then FUN_180049c60 (selection mask + prompt).

  | button | type (bc) | sub (c0) | panel call | cite |
  |---|---|---|---|---|
  | Horizontal 0x9476 | 0 linear | 0 | FUN_180259080(mgr, 5) = Mode "Horizontal" | cad_dimcmd.c:470-473, 503-507 |
  | Vertical 0x9481 | 0 | 1 | FUN_180259080(mgr, 6) = Mode "Vertical" | l.503-507 |
  | Parallel 0x947d | 0 | 2 | - | l.485-487 |
  | Smart 0x948b | -1 (0xffff) | -2 (0xfffe) | - | l.510-512 |

  After a successful start all selection bits are cleared `set_clr_sel_bits(0,4,0,4,0x1f1fff,1,0)` (l.519).

### 2.2 Picks before the text placement (smart state machine, `DAT_1806496b8 = 0xffff`)
Event dispatch FUN_180048940 (cad_dimcmd.c:569), default branch. MCEvent+8 = event kind (decoded by use): **1 = point**
(aPoint at event+0x40, e.g. an AutoCursor snap; cad_dimcmd.c:589-606), **2 / 0x4000 = entity picked** (eptr at event+0x20), **4 = accept/finish**,
**8 = key / panel command** (code at event+0x140, modifier flag at event+0x150), **0x100 = mouse move**. Exact get_event
semantics (mccore get_event, flags `9/0xb/0xc/0xf/0x100`): **NOT DECODED**.

Selection mask set by FUN_180049c60 (cad_linear.c:2685), mask word = sel_mask+0xc, bits by entity class
(1 point, 2 line, 4 arc, 0x10 note, 0x20 witness/leader, 0x40 dimension; FUN_180040dd0 cad_dimcmd.c:390):

| state | H / V / P (bc 0) | Smart (bc -1) |
|---|---|---|
| 0 picks | get_event flags 0xb, mask 3 (point, line) | flags 0xb, mask 0x77 (point, line, arc, drafting) |
| 1 pick | flags 9, mask 0 (position only) | flags 0xb, mask 7 (0xf if `DfDragPtDim`) |

Prompts (FUN_180049560 cad_linear.c:359; lines joined with "\n" = mccad 0x18040e028; ids = mccadres / mccoreres):

| state | H / V / P | Smart |
|---|---|---|
| 0 picks | 27726 "Create dimension, linear:" / 27701 "Specify the first endpoint" | 27725 "Create dimension, smart:" / [27064 "Select a point to create a point dimension" if DfDragPtDim] / 27061 "Select first point for linear dimension" / 27065 "Select a line to create a linear dimension" / 27066 "Select an arc to create a circular dimension" / 27067 "Select a dimension to edit (drag)" or 27068 "...(text)" (by panel Edit Mode, `DAT_1806a4c21`) |
| 1 pick | 27726 / 27702 "Specify the second endpoint" | 27725 / [27051 "Sketch the text position for the point dimension" if DfDragPtDim] / 27062 "Select second point for linear dimension" / 27069 "Select a line to create a perpendicular dimension" / 27074 "Select an arc to create a tangent dimension" |

Pick handling:
* **Point event (kind 1)** - FUN_180049ee0 (cad_linear.c:615): `aPointTestTol`, copy the event aPoint into slot
  `DAT_180698a30 + 0x98·n` (l.740-741); first point -> n = 1, prompt refresh. Second point (l.711-735): both points
  are projected to the active view (`world_to_view`); equal points -> rejected (-1). Smart only: if both aPoints carry the
  same arc entity and lie on it (FUN_18004b970 cad_linear.c:2557: same eptr, no `I`, A_ID, views parallel, each point at
  distance r from the centre, chord H/V-aligned) -> **circular** dimension (FUN_18004d250, state 2; out of scope here).
  Otherwise **linear**: `FUN_180044d20(points, fromLine = 0, sub)` -> state 0 (cad_linear.c:729).
* **Point entity picked (P_ID)**: aPoint kind `P` (`K` when `DfAssoc` is off) at the entity's position, then as a point
  event (FUN_18004a620 cad_linear.c:2926-2932, FUN_18004b070 for the 2nd pick).
* **Line picked as the 1st pick** (FUN_18004a620 cad_linear.c:2934-2964): rejected with 27013 "Line is parallel to Z
  axis." if its ends coincide in the view (FUN_18012be10). aPoints: slot 0 = line start kind `1`, slot 1 = line end kind
  `2`, each with `aPointSaveEntSel(aP, 0, line, t)` t = 0.0 / 1.0 (eptr = line ent+0x20); kinds `K`, no eptr, when
  `DfAssoc` is off. Then (types other than angular/perp, `(bc-1)&0xfffb != 0`) `FUN_180044d20(points, fromLine = 1, sub)`
  -> straight to text placement (state 0).
* Smart, 1st pick = arc -> circular (FUN_18004d250 cad_linear.c:3130-3164); 1st pick = existing dimension/note -> drag-edit it
  (FUN_1800490a0) or open its text editor when Edit Mode = Edit (FUN_180049500); 2nd pick = line -> angular/perpendicular,
  2nd pick = arc -> tangent (FUN_18004b070 cad_linear.c:3202). Out of scope (next command sets).

### 2.3 Entering the placement state - FUN_180044d20 (cad_linear.c:2531)
```
smart      = (sub == 0xfffe)            -> DAT_180649640
orient     = sub > 2 ? 2 : sub          -> DAT_180649648  (0 H, 1 V, 2 P)
locked     = !(sub > 2)                 -> DAT_1806a4c20  (H/V/P buttons: orientation locked; Smart: free)
linearMode = !(sub > 2)                 -> DAT_180692ce9  (selects the prompt "linear:" vs "smart:")
fromLine   = arg                        -> DAT_180692cf0
DfHvpAngle = 0; P1, P2 aPoints -> DAT_180692d30 / DAT_180692dc8
```
then FUN_180044c50 (cad_linear2.c:198): `InitDIM(drag_ent)`, clears the drag flags, sets "re-test H/V" `DAT_180649643 = 1`,
FUN_180044e70 (cad_linear3.c:178: if P1 and P2 lie on the same arc and |P1P2| = 2r -> panel Arc Symbol "Diameter" + "Apply
to linear dimension" on; = r -> "Radius" on), `aPointTestTol` both, FUN_180043f20 (cad_linear3.c:86): P1/P2 to view coords
(DAT_180692fb0 / fc8), prompt, `df_phantom_on(1, P1)`.

Placement prompt FUN_180043660 (cad_linear.c:1721): `"Create dimension, linear:"` (H/V/P) or `"Create dimension, smart:"`
+ (smart only) 27057 "Select a non-parallel line to create an angular dimension", 27058 "Select a parallel line to create a
perpendicular dimension", 27075 "Select a point to create an angular dimension" (27059 "...perpendicular..." if fromLine),
27074 "Select an arc to create a tangent dimension"; + mccoreres 5886 **"Sketch the text position for the linear
dimension"**; + 27070 "Hold '~' to use another dimension's formatting". While `~` is held: 27071 "Select another dimension
to use its formatting", mask 0x70. Get_event flags: H/V/P 0xc with auto-highlight off (position only); smart 0xf, mask 7.

### 2.4 Dragging (each mouse move) - FUN_180042e50 (cad_linear.c:99)
Called from the loop on event 0x100 through FUN_180040da0 -> FUN_180048be0 with `param_4 = GetAsyncKeyState(0xC0) < 0`
(VK_OEM_3 = the `~` key; cad_rundraft.c:3310).
1. Cursor -> `df_calc_sketched_point(screen, aP, ok)` (core_create.c:447): grid snap (`sel_grid_search`) if the grid
   snap is on, else `map_cursor_to_3d`; result is a `K` aPoint (world). Text point **T = world_to_view(aP, active view)**
   (cad_linear.c:170-173).
2. Orientation (cad_linear.c:174-191):
   * on the first move after (re)start, `determine_h_or_v(P1v, P2v, T, &orient)` (core_create.c:3): rotate the three
     points by -DfHvpAngle; if the P1->P2 line is itself horizontal or vertical (`df_angle_hv`: within AbsAngleTheSame of
     0/180 -> 0, 90/270 -> 1) the orientation is set to it and locked (`DAT_180692ce7`, `DAT_180692ce8 = 1`);
     otherwise `ce8 = DAT_1806a4c20` (locked for H/V/P buttons).
   * not locked (Smart): `orient = determine_hvp(P1v, P2v, T, orient, 1)` (core_create.c:49). Decoded: rotate by
     -DfHvpAngle; a1 = angle between lines P1->P2 and P1->T, a2 = angle between P2->P1 and P2->T
     (FUN_180241810 core_create2.c:50: difference of the two line angles, wrapped to (-pi, pi]); if |a1| < 90 deg and
     |a2| < 90 deg (T projects strictly inside the segment) -> **2 parallel**. Else for k = x then y: if T[k] is strictly
     between P1[k] and P2[k] -> return k (**0 horizontal** when T.x is between the x's, **1 vertical** when T.y is
     between the y's). Else keep the previous orientation (and 2 -> 1 when param_5 = 0, unused here).
3. `calc_linear_dimension(drag_ent, P1, P2, T_world_cursor, activeView, &orient, valueMode, &swapped, &ok)`
   (cad_linear.c:197; mccore core_calcdim.c:797), `valueMode = panel Arc Symbol + 1` when "Apply to linear dimension"
   is checked, else 0 (`DAT_180692ceb/cec`). If `swapped`, the caller swaps its P1/P2 copies too (l.203-207).
4. Valid -> `df_draw_drag_ent` (preview drawn as a phantom / live entity); invalid -> 27010 "Warning - cannot calculate
   dimension" (l.216) and the placement is abandoned (FUN_180042ca0).

### 2.5 Keys and function panel during placement
Function panel = `DraftingSmartDimensionsManager` (mccad, shared_ptr `DAT_1806926e0`); WPF view
`draftingsmartdimensionsmanagerview.baml` (labels below are its strings). Panel commands come back as event 8 with code
`10000 + id` (FUN_18012ba20 posts `PostMessage(main, 0x802b, 10000+id)`, cad_linear2.c:327) and are handled by
FUN_18012af20 (common, cad_linear2.c:244) and FUN_180043cc0 (linear, cad_linear2.c:120).

| id | panel control (BAML label / tooltip) | key | effect (cite) |
|---|---|---|---|
| 1 | Leaders - Arrows: _Inside / _Outside (Location) | A | `dd_global.+0` (`_dd_global`, 0 inside / 1 outside) (FUN_18012af20 case 1) |
| 2 | _Witness lines (checkbox) + _Both / _Right / _Left | W (cycles 0,1,2) | `dd_global+6` = `DAT_1813d61fe` "Visible witness lines" 0..3 (case 2). Panel mapping (FUN_180258090 / FUN_180259080 case 2): unchecked = 0; 3 = radio index 0, 2 = index 1, 1 = index 2. XAML order Both, Right, Left -> **hypothesis** 3 Both, 2 Right, 1 Left |
| 3 | Dimension Text - _Centered text (by elimination: bool slot [0x28]) | N | `auto_alignment` (case 3) |
| 4/5/6 | Mode: _Auto / _Horizontal / _Vertical (radio; set value 0 -> Horizontal, 1 -> Vertical, other -> Auto, FUN_180259080 case 4-6) | H (5), V (6) | H: orient 0, V: orient 1; both set `DfHvpAngle = 0`, lock (FUN_180043cc0 case 5/6). Ignored when the points are H/V-aligned or a rotation angle is active |
| 7 | Loc_k | K | lock on/off; unlocking sets orient 2 (case 7) |
| 8 | Orientation an_gle (EvaluatorBox, -360..360) | - | FUN_180044090 (cad_linear3.c:123): 90/270 -> vertical, locked; 0/180 -> horizontal (locked only for 180); equal (mod 180) to the P1P2 angle -> parallel; else `df_calc_orient_ang`, rotated horizontal with `DfHvpAngle` = angle (degrees, normalised by DfCheckAngle core_create2.c:3) |
| 9 | Format - _Font | F | FUN_18012bd50(drag_ent) (font dialog; not decoded) |
| 10 | Format - _Edit text (by elimination) | J | `df_toggle_text(drag_ent, 1)` NOT DECODED |
| 11 | Format - Heigh_t | T | FUN_18012b4b0 (cad_linear3.c:253): DisplayDraftingTextHeightDialog; new height -> `dim_text_height`; with "use factors": tol height / witness gap / extension / arrow h,w = height x the factors; else, optionally, all scaled by new/old |
| 12 | Format - De_cimal places | - | `num_digits` (panel int) |
| 13 | Arc Symbol - Radi_us / _Diameter | D, R | value mode for linear dims (cec 0 radius / 1 diameter) |
| 14 | Arc Symbol - A_pply to linear dimension | (D/R turn it on) | `DAT_180692ceb` |
| 15 | Dimension Box - _Show when dragging | B | `greek_drag_ent` |
| 21 | Settings - _Options | S | `CfgDimX_dialog(main, 100)` = Drafting Options (case 0x15) |
| 22 | Settings - _Update | - | `DraftGlobalsSaveOverLast` (tooltip: "Copies the current drafting settings to the Drafting Options configuration file") |
| 23 / 24 / 25 | panel OK / (?) / OK-and-new | Enter = 25, or 23 when event+0x150 is set | return codes 3 / 4 / 5 to the loop (§2.6). Which FunctionPanel button (OKCommand / ApplyCommand / CancelCommand) owns 23/24/25: **NOT DECODED** |
| 26 | Maintain perpendicular to arc center | - | `DfMaintainTang` |
| 27 | Dimension Text - Edit Mode: P_lace / _Edit | - | `DAT_1806a4c21` (what picking an existing dimension does) |
| 16-20 | (Flip sweep, Flip direction, Display group, Note leader Add / Delete: candidates) | L = 16 | **NOT DECODED** (vtable slots 0x150-0x1b8 of the manager) |

Panel values are refreshed from the globals on every prompt refresh (FUN_180043b60 cad_linear2.c:88: fields 13, 14, 1,
2, 3, 15, 12; fields 4/5/6/7 from orient/lock unless rotated).
`~` held + click on an existing dimension/note -> `ToDimGlobalsFromDraft(ent)` copies its formatting into the globals
(core_create.c:1257; dims via DraftGlobalsFromDim) + `DraftGlobalsSaveOverLast`, and the preview is recomputed
(FUN_1800442d0 case 0x10/0x20/0x40, cad_linear.c:1620-1628). Help agrees (chm mccore/Drafting/Smart_Drafting_Dimensions.htm).

During H/V/P placement, picking geometry is not offered (mask 0). During Smart placement: point -> 3-point angular or
point-line perpendicular, line -> angular/perpendicular, arc -> tangent (FUN_1800442d0 / FUN_180044240) - out of scope.

### 2.6 Accepting, live edit, repeat, exit (loop FUN_18003f7f0)
* **Click (event 4)** in placement: FUN_180044030 -> FUN_180043280(1) (cad_linear.c:989): `UndoTransStart;
  df_save_drag_ent; UndoTransCommit`. df_save_drag_ent (core_create.c:548): `store_ent(&drag_ent, ...)` (the drag entity
  as last computed), then if the entity is not associative (FUN_1802efd70) `DfTurnOffApAssoc`, `DfRemoveMTID(ent,1)`,
  rewrite; failure -> mccoreres message 0x6996 (not looked up).
* The text position is therefore **the cursor position of the last mouse move** (§2.4); there is no separate "text point"
  pick. The value/geometry are those of the last preview.
* After a store with app+0x5a set (all ribbon calls), the new dimension becomes the **live** entity (`DAT_1806925f1 = 1`,
  cad_rundraft.c:3477): drawn in `live_entity_color`, prompt mccoreres 5887 "Modify current dimension or begin creating a
  new dimension" (+ 27067/27068 when idle in Smart, l.3503-3517); panel changes re-run the calc on it (FUN_180038ed0 ->
  FUN_1800386f0). Picking new geometry or Enter commits it (write_ent, colour restored, `DraftGlobalsRestore` unless
  `DAT_1806a4c21`, `DraftGlobalsSave`) and starts the next dimension of the **same** command.
* Esc while a dimension is in progress (state 2) cancels it and restarts; Esc with nothing in progress, or panel codes
  3/4, ends the command; code 5 commits and restarts (cad_rundraft.c:3291-3306, 3314-3320, 3442-3452).
* `DraftGlobalsSave` / `DraftGlobalsRestore` (core_create.c:958 / 802): a stack (`DAT_1813d8cf0`, 0x238 bytes per entry) of
  all drafting globals + main attributes/colour/level, active plane, construction depth, `DfAssoc` - panel changes made
  while a dimension is live are rolled back afterwards unless Update was used.

## 3. The dimension record produced (H / V / P)
`calc_linear_dimension` (core_calcdim.c:797-1232) on `drag_ent`; offsets = ent+X (records.md §2; body = ent+0x68).

1. **Validation**: P1, P2 to view coords; |P1P2| < tol -> invalid (core_calcdim.c:951-954). Orientation 2 collapses to 1/0 when the
   rotated points share x / y (l.955-964).
2. **Header**: ent+0 = 0x40, +0x20 = 0, colour = `main_color`, level = `main_level`, attrib +0x10/+0x18 = `main_attrib`,
   +4 = 1 (l.966-972).
3. **Body**: +0x68 orientation; +0x70 aPoint P1, +0x108 aPoint P2 (aPointCopy, kinds per §2.2); +0x1a0 `DfHvpAngle`
   (degrees); +0x1a9 value mode 0 linear / 1 radius / 2 diameter (valueMode, clamped > 2 -> 0); +0x308 type 0; +0x328 view
   (FUN_1802579d0(activeView)); +0x7ca 0; +0x330 `draft_scale`; +0x7cb `comma_period`, +0x7cc `leading_zeroes`, +0x7cf
   `trailing_zeroes`, +0x7cd `num_digits`; +0x7d0 `constr_view_3d()` (l.973-989); dimension font from
   `init_attribs_dim(&dim font_attr)` (l.990; core_create.c:607: height `dim_text_height`, aspect `chr_exp`, rotation
   `chr_rot`, slant `dim_text_slant_angle`, angle `dim_text_angle`, spacing `extra_sp` / `extra_ln_sp`, `font`, box
   `text_bx`, path `text_path`, alt font/height/aspect, fill, h-align 1, v-align 2).
4. **Dimension angle** phi = 0 (H), 90 (V) or atan2(P1P2) (P), + DfHvpAngle; when `DAT_1812d980c` ("Text orientation",
   config 12004) = 0 the text angle is normalised into (-90, 90] (l.996-1039), when != 0 the text angle is 0
   (`DAT_1813d5640 = 0`, l.1040-1043).
5. **P1/P2 order**: swapped so that P1 is left (H: x2 < x1), bottom (V: y2 < y1), and for P by the rule at l.1056-1064;
   the aPoints are swapped too and `*swapped = 1` (l.1050-1092).
6. **Value / text**: `FUN_18030f410(T, kind = 1 linear | 3 radius | 4 diameter, coordinate_format, fraction_units, ...)`
   (l.1094-1105; formatting = draw.md §5). Measured value: H |dx|, V |dy|, P distance (l.1106-1167) - also the corpus check
   in records.md §5. `uVar16` = text outside the witness span (T beyond the P1..P2 range along the measured axis).
7. **Arrows inside/outside**: from `_dd_global == 1`; forced outside when the dimension length < 2.1 x
   `arrow_head_height` and the dimension arrow style != 3 (l.1171-1175).
8. **Centring snap**: when `auto_alignment` is off and Ctrl (VK 0x11) is not held, the text is treated as centred
   (`auto_alignment = 1` for this computation) if T is within `sel_dist/2` of the dimension midpoint (H: |T.x - mid.x|,
   V: |T.y - mid.y|, P: projection inside and distance to the midpoint) (l.1176-1208); `auto_alignment` is restored and
   stored in +0x7c9 at the end (l.1223-1224).
9. **Layout**: `FUN_18024fc30(textAngle, DAT_1812d980c, outside, arrowsOutside)` (unless text-orientation != 0 and Ctrl held; l.1209-1211),
   then **`FUN_180250410`** with (disassembly calc_linear 0x18024d140-0x18024d20f): param_8 = `auto_alignment`, param_9 =
   `DAT_1812d980c`, **param_10 = `DAT_1813d61fe` visible witness lines**, **param_11 = `DAT_1813d6200` visible leaders**,
   param_17 = `DAT_1813d6202` leader style; inside it (core_calcdim.c:5448-5452, 5559-5561, 5594) these reach
   FUN_180252e50 (record assembly: witness 1 -> E+0x378, leader 1 -> E+0x4a8, text, leader 2 -> E+0x540, witness 2 ->
   E+0x410; gap/extension/value/format fields) = draw.md §4.1-4.2 (witness start = Pi + gap·u, end = Di + extension·u;
   dimension-line halves / arrows; text box).
10. **Post-check** FUN_180251290 (core_calcdim.c:5640, called l.1213): leader-1 start to leader-2 start must be non-degenerate; the
    **stored orientation is re-derived** from the finished dimension line: parallel to P1P2 -> +0x68 = 2 (even for an H
    button pick on a horizontal line), else `find_orient_angle` -> 0/1, or 0 with the rotation angle written to +0x1a0
    (l.5672-5687). Then FUN_18023d590 (anchor byte body+0x140, records.md 2.1) and `DraftIsAssoc`.
11. +0x7b0 = `(char)dd_global` (arrows in/out) (l.1221).

**Memory table**: none for linear dims - the four v8_wit_ldr use inline points (make_wl_single sets +0x19 = 0, draw.md 4.0).
**Associativity**: only through the two aPoints (`1`/`2` + line eptr, `P` + point eptr, or AutoCursor kinds); with
`DfAssoc` off all are `K`. **Not stored**: the text position separately from the layout (records.md 2.2 +0x2a8 = text pos).

## 4. Note (0x9486) - FUN_18003d410 (cad_rundraft.c:722)

(The app's port of this command, the first-move / drag / Esc decoding and what is left out: notes.md 5.)

### 4.1 Note dialog (mccore `DisplayNoteDlg` -> `doNoteDlg(text, &mode, 0)` core_create.c:1417/1436)
Dialog **452 "Note Dialog"** (mccoreres, `MCDialog(0x1c4)` core_notedlg.c:7): edit **25300** (multi-line text; font =
the note font if TrueType else Arial, 1.5 x UI height, core_notedlg.c:259-279), group 2307 "Create" with radios
3888 Single Note, 3889 Multiple Notes, 3890 Label with Single Leader, 3891 Label with Segmented Leader, 3892 Label with
Multiple Leaders, 3893 Single Leader, 3894 Segmented Leader, 3895 Multiple Leaders; buttons 1358 "&Load File..." (reads an
ASCII file into the edit, FUN_1805c0070), 1352 "&Add Symbol..." (`doCharMap` with `note_font`, FUN_1805bfa90),
1369 "&Properties..." (NOT DECODED), OK / Cancel / Help.
* Radio index (dialog+0x2c38) -> button (FUN_1805c0620/c06c0 core_notedlg.c:682/724): 0 3888, 1 3889, 2 3890, 3 3891,
  4 3892, 5 3893, 6 3894, 7 3895. Initial = last used (`DAT_18142cb10`) when creating; radios disabled when editing.
  Indexes 5-7 (leaders only) disable the text, Load File and Add Symbol and enable OK (FUN_1805bf950).
* Result (doNoteDlg core_create.c:1484-1505): text kept for the next call; `mode` = 0 for indexes 0/1 (index 1 also sets
  **`stay_in_create_note`** = 1, index 0 clears it), **1** for 2/5, **2** for 3/6, **3** for 4/7. Indexes > 4 force an empty
  text. Empty text with a note/label index -> message 0x28 and the dialog result is "cancel".

### 4.2 Flow after OK (FUN_18003d410)
* Empty text and mode != 0 (leader-only radios) -> leader command FUN_18003caa0 (app+0x58 = 2 / 1 / 3 for mode 1 / 2 / 3),
  or witness command FUN_1800408f0 when `dd_global+4` (`DAT_1813d61fc`, note arrow style) == 3. Not decoded here.
* Otherwise the function panel is opened (same `IDraftingSmartDimensionsManager`, cad_rundraft.c:962-1160).
* **mode 0 (note)**: loop { FUN_1800389a0 (cad_note.c:67): `get_event("*", ev, 0x100, ...)` - first mouse event (flag
  semantics NOT DECODED) - then `DfCreateLabel(text, screenPos, gr_back_color, kind 0, NULL, 0, &eptr)`; then
  `FUN_180038c50(app, &eptr)` = drag-edit the new note: RunDraftCommand(99) -> note drag state 0x62 (§4.4); on success
  `DraftGlobalsFromNote(note)`; UndoTransCommit } while `stay_in_create_note` (Multiple Notes) (cad_rundraft.c:1163-1193).
* **mode 1/2/3 (label)**: leader points first. Prompt (lines joined by "\n"): mccadres 27003 "Drafting: " / 27711 "Create
  label: " / 27712 " Indicate leader arrowhead position"; `get_event(prompt, ev, 9, ...)`; key events are skipped; the
  event aPoint (`aPointTestForSolid`; AutoCursor kind, e.g. `C` on an arc) is appended (FUN_180039530). Mode 1 stops after
  one point. Mode 2 (segmented): rubber band (`init_rubber_band(0xb)`), prompt 27003 / 27711 / 27715 " Indicate leader
  tail position " + "%ld" (points so far) / mccoreres 27703 "Press <ESC> when done". Mode 3 (multiple): 27712 + " %ld"
  (count + 1) / 27703. Esc ends the list (cad_rundraft.c:1199-1335). Then FUN_1800387f0 (cad_note.c:3): copies the points,
  `get_event("*", ev, 0x100)`, `DfCreateLabel(text, screenPos, back, mode - 1, points, n, &eptr)`, then drag-edit as for
  notes (cad_rundraft.c:1338-1350).

### 4.3 The record - DfCreateLabel (core_draft_rec2.c:184) -> CalcLabel (core_dimlayout.c:1036)
* Position: `df_calc_sketched_point(screen)` -> `proj_pt_cp` (onto the construction plane) -> `world_to_view(active view)`
  = ent+0x70 (x, y, depth) (core_draft_rec2.c:252-256).
* `InitNOTE` (core_draftwf.c:234; font_attr from `init_attribs_note` core_create.c:649: `note_text_height`, `note_chr_exp`,
  `note_chr_rot`, `note_text_slant_angle`, `note_text_angle`, `note_extra_sp`, `extra_ln_sp`, `note_font`, `note_text_bx`,
  `note_text_path`, `note_space_sty`, `h_txt_aln` -> bits 8-9, `v_txt_aln` -> bits 10-12, `mirror`, alt font/height, fill);
  view +0x88 = active view (core_dimlayout.c:1131-1139).
* **Kind** ent+0x68: no leader points -> 3 note; points + text -> 2 label; points, empty text -> 1 (core_dimlayout.c:1140-1150).
* **Text** -> memory table: `mTableAlloc(strlen+1)`, id at ent+0xf8 (core_dimlayout.c:1151-1175; note path also `calc_note`
  core_draftwf.c:394 for the same layout). Line breaks are the edit control's `\r\n` (records.md §0).
* **Leaders** (kind <= 2), arrow = `make_wl_single(kind 1, arrow on, style DAT_1813d61fc, note_arrow_head_height/width)`:
  * single / segmented (DfCreateLabel passes `param_9 = (mode-1 != 2)`): one wl at ent+0x100, visible (+0x103 = 1), point
    array flag +0x119 = 1, **npts +0x11a = n + 2**, leader count +0x1c0 = 1; aPoints in **one memory-table entry**
    ((n+2) x 0x98, id at ent+0x120): the n picked aPoints (or `K` view points when none given), then two copies of point 0
    (core_dimlayout.c:1251-1300).
  * multiple: for each picked point i one 3-aPoint array (mTableAlloc(0x1c8), all three = point i), wl npts 3, flag 1,
    then `add_a_leader(note, tmp, i+1)` (core_draftwf.c:6394): leader 1 is copied to ent+0x100, each further leader into a
    new 0x98 memory-table wl linked from the previous wl+0x84; count ent+0x1c0 (core_dimlayout.c:1176-1249).
  * kind 2: `ResetLabelDepth` when the construction view is 3-D, then **`df_adjust_ldrs`** replaces the last two points of
    every leader by the shoulder points left or right of the text box (draw.md 4.6) (core_dimlayout.c:1301-1307).
* Store: `DfRemoveMTID(ent,1)`; `store_ent(ent, ..., 1, colour)` with `main_attrib` / `main_level` (or `store_ent_nodraw`
  when the colour equals the background) (core_draft_rec2.c:265-290); failure -> `mTableDeleteNote`.

### 4.4 Note text placement (drag) - FUN_180045310 / FUN_180046110 (cad_note.c:259 / 628)
Each mouse move: cursor -> `df_calc_sketched_point` -> `proj_pt_cp` -> view coords -> ent+0x70; `init_attribs_note`
(current globals, so panel/Height changes apply), `df_reset_ldr_arrows(ent,0)`, colour/level from main, 3-D ->
ResetLabelDepth, `df_adjust_ldrs` (shoulders follow the text). Prompt FUN_180045a00 (cad_note.c:745): 27709 "Create note: "
or 27711 "Create label: " + 27704 "Indicate text position", + 27070 / 27071 for `~`. Click (event 4) -> FUN_180045670(1)
store; `~` + pick drafting entity -> `ToNoteGlobalsFromDraft` (core_create.c:1311). Key 'Enter'-type code 3 resets the
note to the current globals and stores it (cad_note.c:197-226).

## 5. Defaults - globals, config items, the user's values
Config writers (core_cfgdraft.c:718-916) give the **exact item number -> global** (reader assumed symmetric, as for
backplot.md). User values from `<windows>/Users/<user>/Documents/my mcam2017/CONFIG/mcamxm.config` (metric,
"File is metric? y"); `mcamx.config` (inch) differences in brackets.

| item | label (file) | global | user value |
|---|---|---|---|
| 11004 | Center text between arrowheads? | `auto_alignment` | n |
| 11005 | Comma? | `comma_period` | n |
| 11006 | Format? | `coordinate_format` | 0 (decimal) |
| 11008 | Tolerance? | `DAT_1813d6210` linear tolerance mode | 0 |
| 11009 | Diameter? | `DAT_1813d5628` diameter format | 0 |
| 11010 | Scale? | `draft_scale` | 1.0 |
| 11011 | Fractional units? | `fraction_units` | 0 |
| 11012 / 11023 | Leading / Trailing zeroes? | `leading_zeroes` / `trailing_zeroes` | y / y |
| 11017 / 11021 | Lower? / Upper? (linear) | `neg_/pos_linear_tolerance` | -0.05 / 0.05 [-0.0005 / 0.0005] |
| 11018 | Decimal places? | `num_digits` | 2 [3] |
| 11019 | Decimal places? | `num_digits_ang` | 1 [2] |
| 11022 | Radius? | `radius_format` | 0 |
| 12000 | Aspect ratio? | `chr_exp` | 0.5 |
| 12002 | Display in SmartMode? | `DfDragPtDim` | n |
| 12004 | Text orientation? | `DAT_1812d980c` | 0 [1] |
| 12005 | Text height? | `dim_text_height` | 5.0 [0.25] |
| 12006 | Extra char spacing? | `extra_sp` | 0.2 |
| 12007 | Font? | `font` | 0 (12024 name "Stick") |
| 12013 / 12014 | Lines? / Path? | `text_bx` / `text_path` | 0 / 0 |
| 12016 | Tolerance height? | `tol_text_height` | 3.75 [0.125] |
| 12017-12021 | Factors? | `arr_ht_factor` 1.0, `arr_wd_factor` 0.333, `tol_ht_factor` 0.75 [0.5], `wit_gap_factor` 0.2 [0.375], `wit_ext_factor` 0.5 | |
| 12022 | Spacing? | `space_sty` | 1 |
| 12027 | Use factors? | `DAT_1812d9898` | 1 |
| 13000 | Extra line spacing? | `extra_ln_sp` | 0.2157 |
| 13001 | Extra char spacing? | `note_extra_sp` | 0.2 |
| 13002 / 13012 | Horizontal? / Vertical? | `h_txt_aln` / `v_txt_aln` | 0 / 3 |
| 13003 | Mirror? | `mirror` | 0 |
| 13004 | Aspect ratio? | `note_chr_exp` | 0.5 |
| 13006 | Font? | `note_font` | 0 (13016 "Stick") |
| 13011 | Text height? | `note_text_height` | 5.0 [0.25] |
| 13019 | Angle? | `note_text_angle` | 0 |
| 14000 / 14001 | Height? / Width? | `arrow_head_height` / `_width` | 5.0 / 1.665 [0.25 / 0.08325] |
| 14002 | Arrow direction? | `_dd_global` (inside 0 / outside 1) | 0 |
| 14003 | Style? | `DAT_1813d61fa` dimension arrow style | 0 |
| 14004 | Visible leaders? | `DAT_1813d6200` | 3 |
| 14005 | Leader style? | `DAT_1813d6202` | 1 [0] |
| 14006 | Style? | `DAT_1813d61fc` note arrow style | 0 |
| 14007 | Visible witness lines? | `DAT_1813d61fe` | 3 |
| 14008 / 14009 | Height? / Width? | `note_arrow_head_height` / `_width` | 5.0 / 1.665 |
| 14012 / 14013 | Witness extension? / gap? | `witness_extension` / `witness_gap` | 2.5 / 1.0 [0.125 / 0.09375] |
| 15003 | Associate dimensions...? | `DfAssoc` | y |
| 15005 | Automatic? | `DfRegenAuto` | y |

`dd_global` = 0x1813d61f8 (mccore export ordinal 10428) is a struct of shorts: +0 arrows in/out, +2 `DAT_1813d61fa`
dim arrow style, +4 `DAT_1813d61fc` note arrow style, +6 `DAT_1813d61fe` witness mode, +8 `DAT_1813d6200` leaders mode,
+0xa `DAT_1813d6202` leader style (clamps core_draftwf.c:3403-3425; writer core_cfgdraft.c:899-904). The user's values
are self-consistent with "use factors": arrow 5 = 1.0 x 5, width 1.665 = 0.333 x 5, gap 1 = 0.2 x 5, extension 2.5 =
0.5 x 5, tolerance height 3.75 = 0.75 x 5 (the factor rule is FUN_18012b4b0, cad_linear3.c:300-312). Which of
mcamx/mcamxm is loaded at start-up: NOT DECODED here (unit setting).

## 6. NOT DECODED (callees / addresses)
* mccore `get_event` flag words (9, 0xb, 0xc, 0xf, 0x100) and the AutoCursor aPoint kinds it returns; MCEvent layout
  beyond +8, +0x20, +0x40, +0x140, +0x150.
* Panel ids 16-20 (vtable slots 0x150-0x1b8 of DraftingSmartDimensionsManager), 23/24/25 <-> panel buttons; 10
  (`df_toggle_text`); 9 FUN_18012bd50 (font); the Both/Right/Left enum order (`SetFormatCommand`, needs the
  MCCAD.Interop / C# view-model).
* `FUN_18024fc30`, FUN_18023d590 (anchor), `find_orient_angle`, `df_calc_orient_ang`, `FUN_1802efd70`, `DfTurnOffApAssoc`,
  `DfRemoveMTID` (contents), store_ent arguments `DAT_1813d6274/6276/6278/6280` in df_save_drag_ent.
* FUN_180250410 / FUN_180255b80 / FUN_18024b250 / FUN_180257270 branch tables (draw.md §4.2, partially decoded).
* Note: Properties button (1369), FUN_180045670 (note store), FUN_180045e70/FUN_180046070 (note keys), ResetLabelDepth,
  `proj_pt_cp`, message 0x28 / 0x6996 texts (mccoreres, not looked up), DfCreateLabel when the colour = background.
* Leader-only / witness commands FUN_18003caa0 / FUN_1800408f0 (decompiled in cad_rundraft.c, not analysed).
* Smart: circular / angular / perpendicular / tangent / point branches (FUN_18004d250, FUN_180042a20, FUN_180048330,
  FUN_18004f420) - the Circular button's path is decoded (sec. 8), angular's calc is ported (sec. 9); the Smart state machine itself not.
* The config **reader** (only the writer is decoded); start-up choice between mcamx.config and mcamxm.config.

## 7. In the app (2026-10-26): Horizontal / Vertical / Parallel

Code: `viewer/core_ui/cui_draft.c` (the loop's states inside the wireframe command frame `cui_geo.c`: `geo_cmd` = G_DIMH / G_DIMV /
G_DIMP), `viewer/draft_create.c` (`dc_init_dim`, `dc_attribs_dim`, `dc_apoint_*`, `dc_line_hv`, `dc_calc_linear`), `viewer/scene.c`
(`scene_draft_session`), `viewer/mcam_saveops.c` (the D_ID records written), `viewer-qt/src/mainwindow.cpp` (`localRun`: the
doDraft_ FT routing). Tests: `tools/tests/draft_ui_test.sh`, `draft_create_test.sh -f`.

### 7.1 Dispatch
The three buttons start by their FT function (`doDraft_DIMENSION_HORIZONTAL / _VERTICAL / _PARALLEL`, layout.txt l.293-301): their
labels are also the Ordinate group's (`doDraft_DIMENSION_ORD_*`) and Wireframe labels, so the label is never matched. Every other
doDraft_ function returns "not ported" (greyed). The set-up refuses to start without the stroke fonts (text extents, get_ext).

### 7.2 Click events - decoded this session (mccore; Ghidra dumps kept in the session scratchpad, to be copied to decomp/draft/:
`core_evdisp.c` = FUN_1804e4d60 + FUN_1804e11e0, `core_evclick.c` = FUN_1804e3b90 + FUN_1804e2f30 .. FUN_1804e5280, `core_evac.c` =
FUN_1804e27d0 + FUN_1804dff70)
* FUN_1804e4d60 (the get_event pump's dispatcher, called by FUN_1804e8dd0 decomp/mccore_evloop.c) sends the view messages
  0x8028 .. 0x8038 to their handlers; FUN_1804e3b90 is the left click.
* **FUN_1804e3b90, the event a click makes** (mode = get_event's flag word, `mfMODE`): (1) FUN_1804e27d0 (get_AC_point: the
  AutoCursor point under the cursor) found a point, the mode has bit 1 (point) or 4 (sketch), and neither Alt nor Ctrl is held ->
  **point event** (MCEvent::SetupPointEvent); (2) else, mode bit 2 / 0x10 / 0x400 and FUN_1804dff70 finds an entity (the
  auto-highlighted one, else find_an_entity with the selection mask) -> **entity event** (SetupSelectEvent); (3) else mode bit 4 ->
  a 'K' aPoint sketch event, mode bit 1 -> map_cursor_to_3d -> a 'K' point event. So **the AutoCursor wins over the entity**: the
  open "event order of mask 0x4003" (formats/ui.md) is this rule. Consequence for the first pick (flags 0xb): with the Nearest
  AutoCursor mode on, a click on a line's span is a Nearest point, not the line; the line pick (2.2) happens where no AutoCursor
  point is found. `check_AC_active` (which modes the AutoCursor serves) is NOT DECODED: taken as always active for point modes.
* **The AutoCursor point's aPoint**: FUN_1804e1bb0 (decomp/mccore_autocursor2.c:651-791) gives the snap code a kind and, unless the
  kind is K / O / V, `aPointSaveEntSel(slot 0, the snapped entity, t)`. Codes with their t decoded in dl_search_fast (decomp/
  mastercam_dlsearch.c:826-886): **point entity 'P' t 0.0, line start '1' t 0.0, line end '2' t 1.0** (display-list kind 1). The
  corpus agrees: 110 linear dimensions have '1' / '1' on two different lines, 79 'P' / '1', 77 'P' / 'P' (all t 0 / 1) - point
  events, a line pick gives one line. How the aPoint reaches the event (MCEvent::SetupPointEvent / save_AC_fast_point: FUN_1804e27d0
  itself builds a 'K' aPoint) is NOT DECODED - the corpus is the evidence that the kinds arrive. **Ported**: 'P', '1', '2' with
  the entity's eptr; **every other snap** (arc ends - t is the display list's float parameter -, midpoints 'M', centres 'C', quadrants,
  intersections 'I', origin 'O', grid, Nearest ...) gives a **free 'K' point** at the snapped position (NOT DECODED t / slots): the
  dimension is right but not associative to that entity.
* Snap ties: geo_snap keeps the first entity in list order on an exact tie (formats/ui.md). At a corner shared by two lines the corpus
  records mostly reference the other line (e.g. ours '2' of line A, stored '1' of line B); Mastercam's display-list order / tie rule
  there is NOT DECODED (7.5 "EQUAL-LAYOUT").

### 7.3 The loop as ported
| step | Mastercam | here |
|---|---|---|
| start | FUN_180040ed0 / FUN_18004b820: InitDIM(drag), DfHvpAngle 0, no picks; DraftGlobalsSave | dr_begin / dr_restart |
| pick 1 (flags 0xb, mask 3) | point event -> slot 0 (FUN_180049ee0); line -> FUN_18004a620 ('1' / '2', 27013 "Line is parallel to Z axis."), straight to placement | dr_click -> dr_point_event / dr_line_event |
| pick 2 (flags 9) | point event; equal view points refused; FUN_180044d20 | same |
| placement | FUN_180044d20 / FUN_180044c50 / FUN_180043f20, prompt 27726 + 5886 | dr_enter_placement (27070 '~' line left out: not ported) |
| mouse move | FUN_180042e50: df_calc_sketched_point (grid snap / map_cursor_to_3d), determine_h_or_v on the first move (locks H / V when P1P2 is itself H / V), calc_linear_dimension, invalid -> 27010 + abandon | dr_move -> dr_compute (no AutoCursor during placement, flags 0xc) |
| accept (event 4) | FUN_180043280(1): store_ent(drag) in one UndoTrans; the new entity is live (live_entity_color), prompt 5887 | dr_store: McamFile.dnew + U_DRAFT undo record, live |
| next pick / OK-New | live committed: main colour, DraftGlobalsRestore + Save | dr_commit_live |
| Esc | dimension in placement: cancelled, restart; else the command ends | dr_esc / geo_end |
| panel 23 / 24 / 25 | codes 3 / 4 end, 5 commits and restarts; button mapping NOT DECODED | OK, Cancel = end (3 / 4); OK-New = 5 |
Not ported: Smart Dimension (its state machine: circular / angular / perpendicular / tangent / point branches), the '~' formatting
pick, the panel keys (A, W, N, H, V, K, F, J, T, D, R, B, S, L), Edit Mode Edit, the live entity's drag-edit, picking an existing
dimension, DfTurnOffApAssoc / DfRemoveMTID / DraftIsAssoc (create.md 6), `sel_dist` = the AutoCursor aperture of the current view.

### 7.4 The record, the save
* The drag entity starts from **dc_init_dim** = InitDIM @180249730 + FUN_180249820 (core_draftwf.c 3-49, 1542-1596) + InitDimWL /
  InitDT / InitDimFA (core_draftwf.c 61-231) + InitDM (core_draft_rec2.c 581-647); the dimension font_attr = init_attribs_dim
  (core_create.c 607-645). Checked: `draft_create_test.sh -f` (the corpus oracle from this empty record instead of the stored one):
  533 / 706 fully equal, the same as the regeneration; the bytes left differing in those 533 records are header ids / selection bits,
  text bytes past the NUL, the tolerance font (its aspect / spacing differ from the main font in ~100 records: InitDT uses the main
  font's globals - those records' tolerance font was set otherwise), the template flag +0x7a0 (user-edited texts), dm +0xa / +0x24 /
  units (NOT DECODED), and **+0x7ce = 0x21 in every 2017 record (714 / 714): its writer NOT DECODED** (no reader either:
  DraftGlobalsFromDim does not read it) - new records carry 0.
* Globals (dr_globals_load): the user's config items of create.md 5 + 12001 chr_rot, 12012 dim_text_angle, 12015
  dim_text_slant_angle, 11013-11015 DIN, 11017 / 11021 tolerances, 12016 tolerance height (writer core_cfgdraft.c:731-911);
  alt_height / alt_chr_exp = 1.0, alt_font / alt_color / fill / fill_color = 0 (their .data initializers in mccore.dll; no config
  item). Loaded once per units, changed by the panel, rolled back after each live entity (DraftGlobalsRestore).
* Saved (mcam_saveops.c save_impl) after the new geometry, as the geometry's header (head(): selection bits 0x81 - most 2017 corpus
  D_ID records -, new eptr / ent_idn / time, the base part's stamp), the aPoints' slot-0 eptr = the picked entity's (a new line gets
  its eptr in the same save); a dimension whose entity was deleted is not written (Mastercam's handling of a deleted associative
  entity NOT DECODED). Linear dimensions need no memory-table entry (3, "Memory table"). The project file keeps them as DIM lines (ours).

### 7.5 Verification (tools/tests/draft_ui_test.sh)
* **App flow** (a new part, three lines; ribbon buttons, AutoCursor clicks, a line pick with Nearest off, placement drags, accept,
  OK / Esc, Undo / Redo): 3 dimensions, values 30 (|dx|) / 60 (|dy|) / 143.178 (distance), orientations 0 / 1 / 2, aPoint kinds
  '2' '2' / '1' '2' (the line pick) / '1' '2'; exported as a .mcam and reopened: the file's records dump identically to the session's,
  the part draws 3 dimensions; the exported part through the corpus oracle: **3 / 3 fully equal**; draft_dump.py --check: every
  aPoint eptr resolves to the picked line end; the project file reopened with the 3 dimensions. Parity: the 4 Horizontal / Vertical /
  Parallel ribbon entries on, every other Drafting entry greyed.
* **Corpus recreation through the click path** (draft_ui_corpus.py: one linear dimension per part, 2017/2018-written, view 1,
  DfHvpAngle 0, kinds '1' / '2' / 'P', no edited text, regenerated fully equal by the oracle - 76 parts; its format made the session
  globals (`dimglobals`, = DraftGlobalsFromDim), its button, clicks at its definition points, the placement mouse move at the
  regeneration cursor (`dimredo`), accept, `dimcmp`): **45 EQUAL, 29 EQUAL-LAYOUT, 2 DIFFER**. EQUAL-LAYOUT = every layout field
  equal, only the aPoint references differ at the same point: 18 a corner shared by two lines (the other line's end - 7.2 "Snap
  ties"), 11 a 'K' where the stored aPoint is '1' / '2' / 'P' (another entity at the same Top-view point, or a snap kind ported as 'K').
  The 2 DIFFER (CREATE_RECTANGLE.mcam, 2017 and 2018 Lathe courseware): the centring snap (calc_linear_dimension l.1176-1208) fires
  with this view's `sel_dist` - the stored text was placed just off the midpoint (-8.782 vs -8.75); `sel_dist` at Mastercam's creation
  time is not stored.

## 8. The Ordinate group (2026-10-27, branch draftord): Horizontal / Vertical / Parallel / Add To Existing / Align live; Window greyed (section 9)

Sources (new dumps, gitignored, `decomp/draft/ord_*`): `ord_cad_asm.txt` (DraftApp::doOrdinate FUN_18003e640 disassembled, mccad),
`ord_cad_drag.c` / `ord_cad_drag2.c` (the ordinate drag state: FUN_1800466a0, FUN_180046920, FUN_180046430, FUN_180046380 and its
event handlers, FUN_180046740, FUN_180046c50 / FUN_180046f50, FUN_180039860 base check, FUN_1800395f0 view, FUN_180038ff0 depth,
FUN_18003e2f0 Window + FUN_18003bb00 / dialog CAdimDlg FUN_180034950), `ord_cad_prompt.c` (FUN_180046e80), `ord_core_calc_asm.txt`
(calc_ordinate_dimension disassembled, mccore), `ord_core_helpers.c` / `_helpers2.c` (align_ordinate_quiet(_1), df_adjust_oc,
df_load_oc, df_manage_oc, set/get_oc_on, move_ordinate_dimension, FUN_180255830, DfOrdNumberOfChildren, DfTurnOffOrdAssoc,
DirtyOrdChildren, DfCheckForRegen, FUN_180303a80, Equal, df_calc_sketched_point, calc_dim_note), `ord_mc_event.c`
(MCEvent::SetupPointEvent, mastercam.dll), `ord_mc_seldim.c` / `_seldim2.c` (select_ent_univ's dimension distance, mastercam.dll).
The record computation is `calc_ordinate_dimension` (core_calcdim.c 1234 + the asm): draw.md 4.5.

### 8.1 Dispatch
* FUN_1800392d0 (cad_rundraft.c:323-329): 0x9478 _ORD_EXIST, 0x9479 _ORD_HORIZ, 0x947a _ORD_PARA, 0x947b _ORD_VERT -> **FUN_18003e640
  (DraftApp::doOrdinate**, the `_MCAssert("DraftApp.cpp",0x722,"DraftApp::doOrdinate")`); 0x947c _ORD_WINDOW -> FUN_18003e2f0;
  0x9477 _ORD_ALIGN -> the dimension loop FUN_18003f7f0 with FUN_180048b10(3, -1) (cad_dimcmd.c:474-478, app+0x5a = 0).
* doOrdinate's orientation `local_82f4` (asm 0x18003e9aa-0x18003e9df): 1 for 0x947b, 2 for 0x947a, else 0 (0x9479 **and 0x9478**).
  **Add To Existing is the same loop in horizontal mode**; what it is for is the pick of an existing base ordinate, which every
  ordinate button accepts (8.2).

### 8.2 doOrdinate's loop (FUN_18003e640; asm cited)
Refuses when DfActive; opens the IDraftingSmartDimensionsManager function panel (as 2.1). Then (LAB_18003e9f0, the (re)start):
SI = 1 (the next pick makes a **zero** ordinate), BL = 0 (no base), view = FUN_1800395f0(GetActiveViewNo) (0 -> the graphics view if
parallel to Top, else 1; `ord_cad_drag.c` 354), depth = ConstrDepth, InitDIM(drag), `DraftGlobalsSave`.
| state (R12) | what | cite |
|---|---|---|
| prompt | mccoreres 27730 / 27731 / 27732 "Create dimension, ordinate horizontal: / vertical: / parallel: " (27736 "Create dimension, ordinate:" otherwise) + "\n" + 27733 "Indicate initial ordinate location" (SI = 1) / 27734 "Indicate position for next ordinate element" + "\n" + 27450 "Select an ordinate dimension" | asm 0x18003eafa-0x18003f182 |
| 1 pick | `get_event(prompt, ev, 0x1000000b, mask 0x40, 1)`. Point event (kind 1) -> state 2. Entity event (kind 2, a dimension): FUN_180039860 - must be type 3 (else 27451 "You must select an ordinate dimension.") and DfOrdType = 1 (else 0x699e = 27038 "Select a BASE(0) ordinate dimension"); a dirty one asks (0x69c0 / 0x69c1) and is regenerated; then BL = 1, SI = 0, view / depth = the base's (+0x328, +0x320) - the next point makes its child. Event 8 (panel / key): FUN_180038ed0 - codes 23 / 24 end the command, 25 restarts it (LAB_18003e9f0). Event 0x10000000 with value <= 2: orientation = value, BL = 0, SI = 1 | asm 0x18003f190-0x18003f302, `ord_cad_drag.c` 276 |
| 2 (point) | SI = 1: view = FUN_1800395f0(active view), depth = the view z of `df_calc_sketched_point` of the click (construction view not 3-D; else of the event point; failing: FUN_180038ff0). aPoint = the event's (+0x40), `local_8288` = event +0x110 = the aPoint's world point projected on the construction plane (**MCEvent::SetupPointEvent** `ord_mc_event.c` 74: +0xe0 world, +0x110 = proj_pt_cp(it)); p4 = p5 = p6 = the aPoint's point | asm 0x18003f323-0x18003f433 |
| 2 -> 3 | SI = 1 and parallel: rubber band from p4, prompt + 27039 "Specify orientation point", get_event(9, mask 0) until a point event: local_8288 = its +0x110, p5 = p6 = it. Otherwise local_8288 to the view, z = depth, back to world; p5 = p6 = it | asm 0x18003ee1b-0x18003f06d |
| 3 create | InitDIM(drag); text point = local_8288 + (10 tol, 10 tol) in world x / y; **calc_ordinate_dimension**(drag, aPoint, text, p4, p5, p6, view, depth, param_9 = SI, &orientation, param_11 = BL, base, &ok). Not ok: the command ends. DfAssoc off: DfTurnOffOrdAssoc (+0x108 = 0, +0x123 = 0). UndoTransStart, store_ent; SI = 1: the new entity is the base (BL = 1) | asm 0x18003ec9a-0x18003ee16 |
| 4 drag | FUN_180038c50(app, &eptr, 0, .., 1, ..): RunDraftCommand(99) drags the new entity's text (8.3). Success: DraftGlobalsFromDim(stored), UndoTransCommit. Failure: UndoTransTerminate (the new ordinate removed); the get_event result 2 ends the command; FUN_180038c50's param_6 (DAT_1806925f3 = panel code 5) restarts it; SI = 1: back to picking a zero (BL = 0). Then the base is reloaded and SI = 0: the next points make children | asm 0x18003eb5f-0x18003ec95 |
Esc while picking (get_event not ok) ends the command (LAB_18003f45c); every exit runs `DraftGlobalsRestore`.

### 8.3 The text drag (state 3 of the dimension loop for an ordinate)
FUN_1800490a0(ent, 0) -> **FUN_1800466a0** (`ord_cad_drag.c` 1): InitDIM(drag), `set_oc_on(align flag)` (0 here), DfHvpAngle 0,
**FUN_180046920** (`ord_cad_drag2.c` 1): the stored record as the drag entity; `DfOrdType` (its base into DAT_180693020); a zero
ordinate without children or an orphan: oc off; `DraftGlobalsSave`, `DraftGlobalsFromDim(record)`; oc on: auto_alignment 0,
h_txt_aln 1; **zero ordinate: auto_alignment 0**; the calc inputs: param_11 = (type 0), view +0x328, depth +0x320, the aPoint, p4 /
p5 / p6 = world(leader 1 points 0 / 1, the placement +0x310), the orientation from the text angle +0x5e8 (pi/2 -> 0, 0 -> 1, else 2);
the entity erased (sel bit 0x100), `df_load_oc`. FUN_180047020 -> FUN_180046c50: prompt mccadres 27054 "Sketch the text position for
the dimension" + 27070 "Hold '~' to use another dimension's formatting"; get_event flags 0xc, no auto-highlight (0x2 / mask 0x70 while
'~' is held).
* **Mouse move FUN_180046430** (`ord_cad_drag.c` 28): `df_calc_sketched_point(cursor)` -> calc_ordinate_dimension(drag, the aPoint,
  the sketched point, p4, p5, p6, ..) - p4 / p5 / p6 are rewritten by the call for a zero ordinate; valid: df_adjust_oc, drawn;
  invalid: 27010 "Warning - cannot calculate dimension", FUN_180046320 (the drag ends unsuccessful; the loop restarts it from the
  stored record).
* **Click** (event 4, FUN_180047080 -> FUN_180046740(1), `ord_cad_drag.c` 214): the drag entity (its eptr restored) written over the
  stored one; a zero ordinate whose leader start moved (oc off): DirtyOrdChildren + DfCheckForRegen (its children regenerated);
  `DraftGlobalsRestore`; df_manage_oc / df_free_oc.
* Esc / panel 23 / 24 / 25 (FUN_18012af20 codes 3 / 4 / 5): FUN_180046320 -> FUN_180046740(0): the stored record kept, the drag
  unsuccessful (8.2 state 4 failure); code 5 also restarts doOrdinate.
* Keys (FUN_180046f50, `ord_cad_drag2.c` 200): panel id 17 (0x11) toggles the oc for a base (df_manage_oc); otherwise FUN_18012af20
  (the common keys of 2.5) and a re-run at the last cursor (FUN_180047170). The panel shows 17, 3 (Centered text), 15, 12 (Decimal
  places), 22, 21, 11, 9, 10 (FUN_180046e80, `ord_cad_prompt.c`).

### 8.4 Defaults (config items, the writer core_cfgdraft.c)
| item | label (mcamxm.config) | global | user value |
|---|---|---|---|
| 12008 | Display negative sign? | `ord_neg_sign` (l.811) | n |
| 13002 | Horizontal? | `h_txt_aln` (l.860; the note alignment global, which DraftGlobalsFromDim sets to an ordinate's +0x122) | 0 |
| 14010 | Apply to ordinate dimensions? | `ord_arrowhd` (l.908): the leader starts witness_gap off the point | y |
| 14005 | Leader style? | `DAT_1813d6202` (create.md 5): 1 = the leader runs along its text (draw.md 4.5) | 1 [0] |

### 8.5 In the app (cui_draft_ord.c)
Code: `viewer/draft_ord.c` (`do_calc_ordinate` = calc_ordinate_dimension, `do_ord_type` = DfOrdType, `do_text_ends` = FUN_180255830,
`do_globals_from_dim` = DraftGlobalsFromDim), `viewer/core_ui/cui_draft_ord.c` (the loop of 8.2 / 8.3 in the cui_geo.c command frame:
G_ORDH / G_ORDV / G_ORDP, FT functions doDraft_DIMENSION_ORD_HORIZ / _VERT / _PARA), the session's records in McamFile.dnew (cui_draft.c;
a child keeps its base as `ent[1] = -2 - the base's index`), `viewer/mcam_saveops.c` (an ordinate's aPoint eptr at +0x68 + 0x28 and its
base's new eptr at +0x108). Tests: `tools/tests/draft_ord_test.sh` (oracle), `tools/tests/draft_ord_ui_test.sh` (app flow + corpus).
| Mastercam | here |
|---|---|
| picks: AutoCursor point / map_cursor_to_3d 'K' (Ctrl) | `do_point_event` (the aPoint kinds of 7.2) |
| the entity event: a base ordinate picked (mask 0x40) | ported (pass 2, section 9): `cui_draft_pick` over viewer/draft_pick.c, then FUN_180039860's checks (`do_take_base`) |
| state 3 / 4 | `do_create` / `do_drag_setup`, `do_move`, `do_drag_accept`, `do_drag_drop` |
| the drag's '~' formatting pick, panel id 17 (oc), Mode change event 0x10000000 | not ported (the Mode radios are shown, greyed) |
| store_ent + UndoTrans around the drag | the record appended at state 3, its U_DRAFT undo record pushed when the drag is accepted (dropped otherwise) |
| panel 23 / 24 / 25 | as 7.3: OK / Cancel = 23 / 24, OK-New = 25 (mapping NOT DECODED) |
| rubber band of the orientation point (init_rubber_band(0, p4)) | not drawn |
Greyed: **Add To Existing** (its purpose is the base pick, NOT DECODED), **Window** (FUN_18003e2f0: dialog CAdimDlg 0x9e5 = 2533,
`get_window`, FUN_18003af90 / FUN_180041160 / FUN_18003bed0 build the sets - decompiled, not analysed), **Align** (0x9477: picks an
ordinate (FUN_18004a620 case 0x40, 27450 / 27451) - the same NOT DECODED pick - then the drag with the oc on: df_load_oc /
df_adjust_oc / align_ordinate_quiet, decompiled in `ord_core_helpers.c`, not analysed).

### 8.6 Verification
* `draft_ord_test.sh` (RUN=1): every corpus ordinate recreated by `do_calc_ordinate` from its own inputs, as the regeneration
  FUN_1803053a0 / the drag set-up FUN_180046920 build them: **2017 / 2018-written records 102 / 102 fully equal** (zero 16, child
  74, orphan 12); X9 records 24 / 51 (X9's own code: trailing zeros, text extents - as the older linear records). `-f` (from InitDIM's
  empty record): 90 / 90 non-orphan equal; the bytes still differing are the second aPoint init / anchor of InitDIM (+0x180..+0x18f,
  +0x1a8: 0 in every stored ordinate, NOT DECODED), the off witness / leader 2 slots, text bytes past the NUL, the tolerance font,
  +0x7ce (= create.md 7.4).
* `draft_ord_ui_test.sh`: the app flow (H: a zero + 2 children, one jogged, an Esc'd child dropped; V: zero + child; P: zero with its
  orientation point + child; Undo / Redo; export / reopen: the file's records = the session's; the exported records through the
  oracle 7 / 7 fully equal; each child's +0x108 = its zero's eptr; draft_dump --check clean; the project reopened; paritydump: H / V
  / P on, Add To Existing / Window / Align off) and the corpus through the click path (draft_ord_corpus.py, 2017 / 2018 parts, the
  orphans left out): **90 recreated: 24 EQUAL, 66 EQUAL-LAYOUT (the aPoint reference: 'C' arc centres etc. are 'K' here), 0 DIFFER**.
* Not covered by the corpus (all its ordinates: leader style 0, ord_neg_sign 0, ord_arrowhd 1, view 1, orientations 0 / 1): the
  leader style 1 branch, signed values, arrowhd 0, the parallel orientation - ported from the asm, checked only by the app flow's
  self-consistency (the user's metric config has leader style 1).

## 9. Pass 2 (2026-10-27): picking existing drafting, Add To Existing, Align; Window not done

### 9.1 The pick of an existing drafting entity (viewer/draft_pick.{c,h}, core_ui cui_draft_pick.c)
A click with a mask that holds drafting classes (doOrdinate's get_event 0x1000000b mask 0x40, Align's flags 10 mask 0x40, later Smart
Dimension's 0x77) becomes an entity event when no AutoCursor point is found (FUN_1804e3b90, 7.2): FUN_1804dff70 (decomp/draft/ev_ac.c 59)
takes the auto-highlighted entity, else `find_an_entity` -> `select_ent_univ` -> FUN_18020f4e0 (ord_mc_seldim.c 63-951):
* every display-list entity that matches the mask gets a distance in proj_iso_pt's 2-D (the graphics view's x / y axes,
  ord_core_pick2.c 50); the winner is the **strictly nearest below sel_dist** (l.1802101c9: `dist < best && dist < sel_dist`; ties keep
  the first in display-list order). FUN_180207a70 (the surface / solid pre-pass) does nothing for the drafting masks (ord_mc_pick.c).
* D_ID (case 0x40, l.608-660): the dimension's view (+0x328) must be the graphics view (+0x3e18 of the graphics window, FUN_1801c0ba0) or
  iso_view_dimensions (config 15007); mask 0x40 / 0x10: the texts - FUN_18020ae30 -> FUN_18020b380 (ord_mc_seldim2.c 193): the value
  text (DfCheckForTokens) and the tolerance texts 1 / 2 through mccore `distance_to_string` (ord_core_pick.c 174): CDrawString drawn in
  its distance mode 2, every emitted segment `proj_iso_pt` + `dist_to_finite_line` (text1.c 559-570, 3154) - TrueType FUN_18020ae80 not
  ported; mask 0x40 / 0x20: the slots +0x378, +0x410, +0x4a8, +0x540 when on: FUN_180208d20 (the +0x84 chain) -> FUN_18020b5c0: on and
  visible; kind < 2 the polyline (inline view points or memory-table aPoints, aPoint2Plane at the depth, FUN_18020b870 per segment),
  kind 2 the arc (dist_to_arc, same view only: dist_to_arc_by_pieces not ported); then `calc_arrow_vec` (ord_core_pick.c 3: the first
  segment, the point arrow-length along it) and FUN_18020a1a0 (ord_mc_pick.c): styles 0 / 1 / 2 / 10 the two sides (half angle
  atan(w/2 / h), length h / cos), 4 / 5 a circle of radius h/2 about the tip, 8 a slash; 6 / 7 / 9 and the arc leaders' arrows not ported.
* N_ID / W_ID (case 0x10 / 0x20): view +0x88; the text when kind > 1, the leader chain +0x100 when kind < 3 and on.
Here the candidates are the open part's drafting records then the session's; the distances are computed on the segments draft.c draws
(the text strokes: `draft_render_text`). Checked: on FIXTURE PLATE every ordinate probed on its leader is picked (18 / 18); the app
flows of 9.2 / 9.3. No Mastercam pick log exists to compare with.

### 9.2 The base pick (every ordinate button) and Add To Existing
doOrdinate's entity event (8.2): FUN_180039860 (ord_cad_drag.c 276) - not an ordinate: mccadres 27451; DfOrdType != 1: 27038 "Select a
BASE(0) ordinate dimension"; DraftIsDirty (core_calcdim.c 9237: an associative aPoint off its entity) asks to regenerate - NOT PORTED:
here a part base whose referenced entity was changed in this session is refused with a message. Then BL = 1, SI = 0, view / depth = the
base's: the next points make its children (a part record's base: +0x108 = its eptr, kept by the save). **Add To Existing** = the same
loop in horizontal mode (8.1) - live. The prompt now shows the third line 27450 "Select an ordinate dimension".

### 9.3 Align (0x9477)
The dimension loop with type 3 (cad_dimcmd.c:474): pick an ordinate (flags 10, mask 0x40; 27451 otherwise), FUN_1800490a0(ent, 1) ->
the ordinate drag with the oc on: FUN_180046920 (a zero without children / an orphan: oc off; else auto_alignment 0, h_txt_aln 1 - the
dragged text centred), `df_load_oc(type, orientation, base, 1)` (ord_core_helpers.c 499: a child -> its base + the base's other
children; a zero -> its children; each copy +0x7c9 = 1, +0x20 = 0), FUN_1802423f0 (ord_core_oc.c: the dragged leader line, the dragged
text box half sizes 7a18 / 7a20, pad 79e8 = |height x extra_ln_sp|, each entry's distance from the dragged leader (x / y / signed
perpendicular) and its box; order 10001.. upward by increasing distance, 9999.. downward), each mouse move calc_ordinate_dimension then
`df_adjust_oc` (ord_core_helpers.c 203 + its asm, decomp/draft/ord_core_oc_asm.txt): three rows through the dragged text's placement
(its start / centre / end), the entries placed alternately in index order at max(own position, previous + pad + box) along the row,
then `move_ordinate_dimension` (ord_core_helpers.c 948: the text start / end moved there, the leader rebuilt, +0x122 = 0 / 2). The click:
the dragged record and every entry written (one undo step). Ported: viewer/draft_ord.c `do_oc_order`, `do_oc_adjust`,
`do_move_ordinate`; core_ui cui_draft_ord.c `al_*`; existing records rewritten by `cui_draft_edit_*` (cui_draft_pick.c: a session record
in place, a part record by a session replacement carrying its eptr, written over the part's record by mcam_saveops.c; U_DRAFTEDIT undo).
Checked: the app flows (a session set and FIXTURE PLATE's X set: the texts moved to the dragged row, every rewritten record regenerating
equal through the ordinate oracle, one undo step); `draft_ord_test -a` - the corpus sets whose records show a past Align (the dragged
one h_txt_aln 1, the others +0x7c9 = 1: CAVITY's two sets, 2017 / 2018 / X9) re-placed by our oc from the dragged record as stored:
**20 / 60 entries equal**; the rest differ in their position along the row (e.g. CAVITY '1.6' stored left of its own point, which the
max rule never gives: those texts were dragged again after the Align, or a rule is missing - NOT RESOLVED). Not ported: the oc in a plain
ordinate drag (panel id 17), the GrTemp preview colour.

### 9.4 Window (0x947c) - greyed
FUN_18003e2f0 (ord_cad_drag.c 412): dialog CAdimDlg 2533 (FUN_180034950; its settings struct DAT_180692610, 0xd0 bytes, copied in / out
by FUN_180035450 / FUN_180034e00), `get_window(prompt, mask 0x48f, ...)`, FUN_18003bb00 (ord_cad_drag2.c 256): FUN_18003af90 (ord_cad_win.c:
the base point - the struct's point / the given coordinates / select_origin -, the windowed points: point entities 'P', curve ends
'1' / '2', arc centres 'C' by the struct's options, their extents and the four ordinate rows beyond them by text size + struct +0xb8),
FUN_180041160 (which sides have points), FUN_18003bed0 per side (calc_ordinate_dimension for each point, store_ent, then
align_ordinate_quiet). Decompiled; NOT DECODED: the dialog's controls / DDX / defaults (the struct's fields), get_window's selection,
FUN_18003bdd0 / FUN_18003aaa0 / FUN_18003ae40 (point list helpers), align_ordinate_quiet. Stays greyed.

### 9.5 Mode change and the '~' pick
* doOrdinate's event 0x10000000 (value <= 2: the orientation, BL = 0, SI = 1; cad_rundraft.c:2210) comes from the panel's Mode radios
  through the .NET view model; its sender is not in the dumps: NOT DECODED (the radios stay greyed).
* '~' during the drag (FUN_180046c50: get_event 2, mask 0x70; FUN_1800470f0): the picked dimension / note's format
  (`ToDimGlobalsFromDraft`, ord_core_pick2.c 187: DraftGlobalsFromDim, or a note's font / arrow globals) + DraftGlobalsSaveOverLast + the
  drag re-run. Decoded; not wired (the app has no '~' key state): left out.
## 10. Circular (0x9475) - decoded and in the app (2026-10-27)

Sources (new, gitignored, `decomp/draft/`): `circ_cad.c` (mccad: FUN_18004bd60 drag, FUN_18004d300 / FUN_18004d750 / FUN_18004d840
set-up, FUN_18004bc60 events, FUN_18004bbf0 abandon, FUN_18004c430 prompt, FUN_18004cb70, FUN_18004c290 (drag-edit entry),
FUN_18004c170 store, FUN_18004cd20 / cd40 / cd80 / ce00, FUN_18004c840 panel refresh, FUN_18004c950 panel keys, ...), `circ_core.c`
(mccore: FUN_180256700, determine_cl, FUN_180245a40, FUN_180245600, FUN_180310b30 / b20, BaselineChainProc), `circ_asm_18024a3c0.s`,
`circ_asm_180251840.s`, `circ_asm_180256700.s` (the calls Ghidra truncated). Port: `viewer/draft_create_circ.c` (create_port.md 4);
app: `viewer/core_ui/cui_draft_dim.c`.

### 8.1 The command
* FUN_180040ed0 case 0x9475 -> FUN_180048b10(2, -1): type 2; FUN_18004b820 InitDIM(drag), DfHvpAngle 0. FUN_180049c60 case 2: get_event
  flags 10, mask **4 (arcs only)**. Prompt FUN_180049560 (cad_linear.c:399-483): mccadres 27706 "Create dimension, circular: " +
  27066 "Select an arc to create a circular dimension".
* Arc picked (FUN_18004a620 case 4, cad_linear.c:2964-3083): `views_parallel(arc view, active view)`, else (construction view not 3-D)
  27012 "Warning - arc is not parallel to construction plane."; type 2 -> style argument 8, angle 0 -> FUN_18004d250 (cad_linear.c:2819):
  **diameter flag `DAT_180698cc4` = AbsAngleTheSame(sweep, 2 pi)** (a full circle gets a diameter, an arc a radius), `DAT_180698cc7` = 1,
  `DAT_1806496ce` = 0 -> FUN_18004d300: FUN_18004d750 (leader bookkeeping flags dd / de / df / e0 = 1, e4 / e8 = 3), InitDIM(drag),
  `e8` = dd_global+8 (the leader mode), df = 0, `DAT_1806a4c20` (lock) = 0, style `DAT_1806496cc` = 2, quadrant `cc8` = 0, first-move
  flag `dc` = 1, DfHvpAngle 0; FUN_18004c290 (drag-edit only); the 'C' aPoint, the arc copied; FUN_18004cb70 (df_phantom_on of the arc,
  FUN_18004c430 prompt). State 2.
* Placement prompt FUN_18004c430: 27054 "Sketch the text position for the dimension" + 27070 "Hold '~' ..." (flags 0xc, mask 0: no
  entity picks for the Circular button; Smart (`ce` = 1) gets mask 7 and 27049 / 27069 for a tangent dimension).
* **Each mouse move FUN_18004bd60**: df_calc_sketched_point -> T (view of the arc); unless locked (`DAT_180698cc6` = the panel's Lock)
  after the first move: **`style = determine_cl(arc, T, diameter, &quad)`**; the leader mode dd_global+8: radial style -> mode < 1: 0,
  arrows outside: dd ? 1 : e0, inside: de ? 3 : e4; quadrant style -> df ? 3 : e8; then `calc_circular_dimension(drag, arc, quad, &style,
  diameter, T world, active view, &ok)`; invalid -> 27010 and FUN_18004bbf0 (abandon; the leader mode is NOT restored there).
* Click (event 4): FUN_18004cd20 -> FUN_18004c170(1): dd_global+8 = e8, UndoTransStart, df_save_drag_ent, UndoTransCommit. Point event
  in placement (Smart only): FUN_18004cd80 -> FUN_18004f420(2, ...) a tangent dimension (state 6). Keys / panel: FUN_18004cd40 ->
  FUN_18004c950.
* Panel (FUN_18004c840 refresh): ids 1 arrows, 3 centred, 0xf box, 0xc digits, **7 Lock (`DAT_1806a4c20`)**, **0xd Arc Symbol = the
  diameter flag**; quadrant style also 2 witness / 8 angle. FUN_18004c950: after FUN_18012af20 (common) id 1 / 0x15 with the radial style:
  arrows changed and the mode not -> outside: dd = 0, mode = e0, e4 = old mode; inside: de = 0, mode = e4, e0 = old mode; a changed
  mode -> < 1: all four 0, outside: dd = 0, e0 = mode, inside: de = 0, e4 = mode; quadrant style: df = 0, e8 = mode. id 7 lock, id 8 the
  angle (ignored for the radial style), 0xc digits (FUN_18012bca0), 0xd diameter.

### 8.2 determine_cl @18023ff80 (circ_core.c 755)
T farther than r from the centre and inside the band of the four quadrant points E / N / W / S (FUN_180245a40: centre +- r, rotated by
DfHvpAngle, each `pt_on_arc` tol): diameter: T.x between W and E and E, W on the arc -> **quadrant style** (return 0), quad = T.y >= cy ?
2 : 4; else T.y between S and N and N, S on the arc -> quad = T.x >= cx ? 1 : 3. Radius: by `df_angle_of_line(centre, T)` (degrees =
df_angle_of_line_rads x 57.29.., asm 0x180241a00) 0-90 / 90-180 / 180-270 / 270-360 with the two quadrant points of that quarter on the
arc -> quad 1/2, 3/4, 5/6, 7/8 (the half by 45 / 135 / 225 / 315). Else 2 (radial). `DAT_1813c79b0` (would accept any quadrant) is 0:
.bss, only read (8 reads in mccore, no writer).

### 8.3 calc_circular_dimension @18024b810 (core_calcdim.c 672-795)
* Quadrant style (`*param_4 == 0`): **quad_to_eps** (core_calcdim.c 5009) + FUN_180245600 (circ_core.c 948): two quadrant aPoints, kind
  **'A' = the arc at angle t (degrees, 0 / 90 / 180 / 270 + DfHvpAngle)** ('K' when the arc has no eptr) - records.md 2.5's 'A' is this -,
  diameter: E-W horizontal (quad 2 / 4) or N-S vertical (1 / 3); radius: E-N, N-W, W-S, S-E, orientation by the half. Not on the arc ->
  `*param_4 = 2` (radial). Else `calc_linear_dimension(E, ap1, ap2, cursor, view, &orient, diameter + 1, ...)`, then type 2, the centre
  aPoint 'C' at +0x68 (over the linear aPoints), +0x101 diameter, +0x100 0, +0x110 quad, +0x108 DfHvpAngle, +0x118.. r / start / sweep,
  FUN_18023d590 (case 2), DfTurnOffApAssoc when the arc has no eptr, DraftIsAssoc.
* Radial style: **FUN_18024f3c0** (core_calcdim.c 4816): header; +0x100 = 2, +0x101 = diameter, aPoint 'C' (centre, t 0; 'K' when
  DfAssoc is off), +0x110 = 1, +0x108 = 0, arc r / start / sweep, view, scale, zeros / digits; the **centring snaps** (auto_alignment off,
  T within r, Ctrl not held; m = min(dim_text_height, sel_dist)): radius: r/2 - m/2 < |T-C| < r/2 + m/2 -> centred; |T-C| < m/2 -> T = the
  centre; diameter: |T-C| < m/2 -> centred; **arrows forced outside** when inside, arrow style != 3 and r < arrow_head_height
  (+0x7b0 = 1 with the leader mode unchanged: the corpus' "arrows 1, mode 3" records); FUN_180250f60 -> FUN_18024a3c0 / FUN_180253540;
  FUN_18023d590 case 2 (offsets from the centre, angle +0x108; FUN_18023de60 gives 0 for type 2); +0x7c9 = the original auto_alignment.
* **FUN_18024a3c0** (layout; args asm 0x180250f60-0x18025117e): leader mode 2 -> 1; inside the circle: arrows inside for a radius, a mode
  > 0 -> 3; **FUN_180251840**: the value text (kind 3 radius / 4 diameter, value r / 2r) and its box (FUN_18024c160 at the text angle 0),
  side = the box's left-bottom corner nearer the centre; outside: horizontal text (+ leader style 0) puts both corners at the box's middle
  height; the attachment point = the nearer corner (horizontal text: moved out by 2 x text height + pad) and the angle centre -> it;
  inside + leader style 1: the angle to the corner on the centre's side. Then the arc's angular range (wrapped when beyond 0 / 2 pi): an
  angle off the arc -> the opposite angle when that is on the arc (radius with arrows outside: never), else the nearer arc end;
  Q+/- = centre +- r (cos a, sin a); auto_alignment + inside: radius -> T to the middle of the radius (Q+ side unless Q- is nearer and
  not opposite), diameter -> T = centre; the text angle (aligned text) = a - pi if a > pi, + pi if then > pi/2 (1e-6); the leader
  angle (opposite: a -+ pi into [pi/2, 3pi/2]); the text and box again (at the text angle); **FUN_180256700** the two leaders (A -> +0x4a8,
  B -> +0x540): A = [Q+, centre or Q+ + 2h dir (arrows outside), centre (leader style 1)], B the same from Q-; inside with arrows inside
  and leader style 0: both to the box edges (FUN_180254bc0 -+ pad); radius: B off; outside: A gets the attachment point (aligned text:
  the box edge along the dimension line -+ pad, by side / leader style; horizontal text + leader style 0: a horizontal shoulder 2h
  long; leader style 1: under the text, B to Q+); leader style 1 moves the text by (centre.y - box bottom + pad) across the line, or by
  pad when horizontal and outside; leader visibility by the mode (1 A, 2 B, else both) unless leader style 1.
* **FUN_180253540** (record; args asm 0x180251183-0x18025124c): value r / 2r at +0x338, +0x7b2 symbol format, +0x7a6 = 0x10001, +0x7ae the
  leader mode as passed, +0x7a2 = 0, **+0x7ac = 0**, +0x7c0 / +0x7b0 / +0x7c2 / +0x7b4 / +0x7b6, tolerances + DIN (FUN_1802579a0),
  +0x7b8; **witnesses off** (+0x378 / +0x410 on = 0); A kind 1 arrow (leader style 1: by mode 1 / 2 the other leader's arrow 3), the text
  slot (FUN_180251460), B kind 1 (radius: no arrow); radius with B < 2 points: +0x7a8 = 0 and B off.

### 8.4 In the app (cui_draft_dim.c)
The ribbon's Circular (FT doDraft_DIMENSION_CIRCULAR, G_DIMC) runs in cui_draft.c's loop (the same live entity, Undo, OK / OK-New /
Esc, save, project DIM lines): arc pick (pick_entity's rule over the XY arcs; arcs of other views refused with 27012: map_arc_to_parallel_view
/ constr_view_3d NOT PORTED), the placement drag as 8.1 (determine_cl, the leader-mode bookkeeping, both styles), accept. Panel live: Leaders
Inside / Outside (+ FUN_18004c950's bookkeeping), Centered text, Decimal places, Lock, Arc Symbol Radius / Diameter; the rest greyed. The
saved record's 'C' aPoint gets the arc's eptr (mcam_saveops.c: the aPoint at +0x68 for type 2). Not ported: the arc phantom
(df_phantom_on), the '~' formatting pick, the 27070 prompt line, the drag-edit of the live entity (FUN_180038790; panel changes recompute
it from its inputs, as cui_draft.c does for linear).

### 8.5 Verification
* Oracle `tools/tests/draft_circ_test.sh` (regeneration FUN_180303e20's inputs; create_port.md 4.2): 393 circular records, 3 skipped (view
  >= 9); **2017-written: 164 compared, 132 fully equal**, 1 more with the creation cursor (leader style 1 moves the text: the stored
  offsets include the shift, so a regeneration is not an identity - in Mastercam either), 31 left = 15 "text box n/255 cell wider"
  (font version, create_port.md 2 cat. 3) + 10 more of that family (2/255: one glyph wider) + 6 SURFACING_7/8 records that are not
  self-consistent (records.md 5). Older records (+0x7ae / +0x7b0 = 0xffff, 226): 0 equal - a 3-point leader where 2017 makes 4, not this
  layout.
* determine_cl at the stored text point: radial for 156 of 164, quadrant style for 8 (those keep the radial style because FUN_18004c290,
  the drag-edit entry, locks the style).
* App (`tools/tests/draft_ui_test.sh` C): a circle and a 90-degree arc dimensioned (diameter 40 / radius 15, 'C' aPoints), Undo / Redo,
  export + reopen = the session's records, draft_dump --check clean; **corpus recreation through the click path (draft_ui_corpus_circ.py,
  22 parts): 18 EQUAL, 4 DIFFER** = 2 where determine_cl gives the quadrant style at the stored text point (8.2: a dragged record), 2
  stored with arrows outside + leader mode 3 (what the drag-edit's FUN_18004d840 keeps; the command's bookkeeping gives mode 1).

## 11. The other Dimension-group buttons (state 2026-10-27, pass 3)
Live: Horizontal / Vertical / Parallel (7), Circular (8), Angular (10), Baseline / Chained (11), Tangent / Perpendicular (12), Point (13).
Still greyed:
| button | what is decoded | why greyed |
|---|---|---|
| Smart Dimension 0x948b | the linear branch (create.md 2: picks, determine_hvp, placement prompts), the branches it starts are the other commands' set-ups (FUN_18004d250 circular from an arc or two points on one arc - FUN_18004b970 -, FUN_180042a20 angular, FUN_180048330 perpendicular, FUN_18004f420 tangent, calc_point_dimension under DfDragPtDim) | not ported: its own state machine (type -1: masks 0x77 / 7 / 0xf, the arc style argument 9 / 1 / 0 with an angle for 90 / 180 / 270-degree arcs, FUN_18004a620 l.3060-3090), the unlocked linear orientation (determine_hvp in cui_draft.c's drag), the placement-time picks FUN_1800442d0 / FUN_180044240 (3-D construction-plane logic: Plane::GetMatrixFrom2lines ...), the edit of a picked dimension / note (FUN_1800490a0 / FUN_180049500; the hit-test = the Ordinate branch's viewer/draft_pick.c) |

## 12. Angular (0x9472) - in the app (2026-10-27, pass 2)
The flow is create_port.md 5.3 (picks, prompts, set-up, drag, accept); code `viewer/core_ui/cui_draft_ang.c` over `viewer/draft_create_ang.c`.
The two pieces left open in pass 1, decoded:
* **FUN_180242fa0 (message 0x6987)**: `FUN_180243370` loads mccoreres **27015 "Warning - invalid quadrant or code."**, FUN_180242ff0 shows it
  with `dbmmess_setup` + `dbmmess_mbok(0x2ee)` (an OK message box) (decomp/draft/circ_core.c is not it: dumps in the session scratch,
  mccore 0x180242fa0 / 0x180242ff0). It is raised by FUN_18023f360 / ang_code2quad only when NO quadrant has the code (FUN_18023f360:
  the code of quadrant q = the two endpoint codes +1, x10 / +, accepted when in {12, 14, 23, 34}, bitmask 0x400805000) - the start's
  ang_code2quad(0xc) normally finds the quadrant between the two lines' first ends; not an every-start message.
* **"Enter angle in degrees" (the line + point method)**: `get_angle` @1804e9620 (mccore) -> FUN_1804ea180 -> FUN_1804e9730 ->
  **mastercam.dll RequestRealInputModal @180327430** (min / max -1e38 / 1e38 = 0xc7d2ced32a16a1b1 / 0x47d2ced32a16a1b1, default = the last
  angle `DAT_180698cb0` in degrees): dialog **286 of OpmanRes.dll** (mastercam.dll's resource DLL, `Resources\OpmanRes.dll`): a 154 x 16 du
  box, one ComboBox 254 (1, 2, 152, 61); the title (template "Enter coordinates:") replaced by the prompt (FUN_180235f20); DoModal !=
  IDCANCEL -> `EvalToDouble` of the text (FUN_1802358f0). App: `cui_hooks.ask_real` (Qt: a dialog titled with the prompt, an editable
  combo, Enter = OK); **the expression evaluator EvalToDouble is NOT PORTED (numbers only; anything else = Cancel); the combo's history
  NOT DECODED (empty)**. Then (cad_linear.c:765-830): restrict_angle, slot 3 = slot 2 relative (+0x58 = 1, base +0x60, view offset +0x78
  of 100 x tol along the angle; records.md 2.5), l_ints with the first line (27832 when none), all 'K' with DfAssoc off, FUN_180042a20(0).
* App specifics: the arc's 'C' slot t (Mastercam: a stack value) is 0; panel live: Leaders Inside / Outside, Centered text, Lock; greyed:
  the digits (id 0xc -> FUN_18012bca0, whose body Ghidra reduced: NOT DECODED for angular), flip (id 0x10: which control posts it NOT
  DECODED), witness. The angular globals from config 11000 / 11001-11003 / 11007 / 11016 / 11019 / 11020 / 14011 (core_cfgdraft.c:730-750,
  909). New geometry's eptr: the aPoints carry a session token the save replaces (mcam_saveops.c MCAM_EPTR_TOKEN, ours).
* Verified: draft_ui_test.sh D - two lines 45, an arc 120, three points 56.3, line + point + typed 30 -> 150; exported records 4 / 4
  regenerate fully equal (draft_ang_test), aPoints resolve; corpus recreation through the click path (draft_ui_corpus_ang.py, two-line
  records, every AutoCursor mode off): **29 EQUAL / 38**, 9 DIFFER = 6 stored with the quadrant +0x2e8 != the cursor's +0x2ea (made with
  Lock), 3 aPoints on another coincident line.

## 13. Baseline (0x9473) / Chained (0x9474) - in the app (2026-10-27, pass 2)
FUN_18003c3d0(app, 0 / 1) (cad_rundraft.c:540-720) + BaselineChainProc @180247c60 (circ_core.c 305-669); code
`viewer/core_ui/cui_draft_base.c`.
* Prompts: mccadres 27003 "Drafting: " + 27717 "Create dimension, baseline: " / 27718 "Create dimension, chained: " + 27719 "Select a
  linear dimension: " (get_event flags 2, mask 0x40) then 27702 "Specify the second endpoint" (flags 1: a point event; aPointTestForSolid).
  No function panel. Esc ends the command.
* Phase 0 (the base): df_linear_format (linear, circular quadrant style, perpendicular, tangent) else mccoreres 27070 "BASE is not a linear
  dimension"; DraftIsDirty -> 0x69c0 + DfRegenEnt (NOT PORTED: the base is taken as clean); the base's text point +0x638, its line angle
  (leader 1 -> leader 2 first points), the increments = config **15000 "Baseline increments?" / 15001 X / 15002 Y** (DAT_1812d97fa /
  9850 / 9858, core_cfgdraft.c:768-770; the user's metric config: y, 5, 5), the "text not along the line" flag; the ends: linear
  +0x70 / +0x108, circular quadrant style quad_to_eps (perpendicular / tangent bases: NOT PORTED, refused).
* Each point (phase 1 / 2): the end nearer the point along the base line (first point, or every chained point); **chained**: from that end
  to the point, the end moves there, text = the base's text point, centred (auto_alignment 1); **baseline**: from the far end (fixed by
  the first point), increments on: the text point moves by (+-X, +-Y) per point, the signs by the quadrant of (base text - its projection
  on the base line); DfHvpAngle = the line angle through df_calc_orient_ang, DAT_1812d980c = the flag; calc_linear_dimension; store_ent in
  one UndoTrans. **Baseline with increments off** (text point = DAT_1813c7cd8 as it was) hands the new dimension, its UndoTrans still open,
  to the **drag-edit FUN_180038c50**(app, &eptr, no undo, &ok, 1, &oknew, 0) (asm 0x18003c8c9-0x18003c8f2): RunDraftCommand(99) ->
  FUN_180040ed0 case 99 -> FUN_1800490a0 -> FUN_180043260 / FUN_180044c50 / FUN_180043390 (the record into the drag entity,
  DraftGlobalsSave + DraftGlobalsFromDim, DfHvpAngle +0x1a0, P1 / P2, orientation +0x68 (0 rotated), locked) and the linear drag; a click
  = FUN_180043280(1) (write back, DraftGlobalsRestore) -> UndoTransCommit, the next point; Esc / panel OK / Cancel / OK-New (codes 3-5
  in mode 99: FUN_180048800 + success 0) -> **UndoTransTerminate = the Undo command's reversal** (both call FUN_1802dfa50: UndoTransEvent
  @1802dffd0 at 0x1802e0027, UndoTransTerminate at 0x1802e04c1): the new dimension removed; OK-New restarts at the base pick, the others
  end the command (cad_rundraft.c:647-662). Chained never drag-edits.
* The base pick (select_ent_univ of a dimension under the cursor) is the hit-test the Ordinate branch decodes (viewer/draft_pick.c): left
  as a stub here (db_pick_dim), the test step `basepick:s:N / f:N` chooses the base; the merge joins them.
* Verified: draft_ui_test.sh E - a base 30, baseline 60 / 100 with the texts stepped by 5 / 5, chained 30 / 40; the 5 records regenerate
  fully equal (linear oracle); with increments off (test step baseincr:0) the drag-edit places the new dimension and Esc removes it.

## 14. Tangent (0x9480, type 6) / Perpendicular (0x947e, type 5) - in the app (2026-10-27, pass 3)
Code `viewer/draft_create_tan.{c,h}` (calc_tangent_dimension @18024f830 / calc_perp_dimension @18024eb60 over calc_linear_dimension,
calc_tangent_eps @18023f9c0, calc_tangent_rbox @18023fc80, FUN_18023f7a0 / FUN_18023f400 / FUN_18023ee40, DfGetTangentArc @18023e6a0,
FUN_18023f890, FUN_18023de60 cases 5 / 6, FUN_18023d590 cases 5 / 6) and `viewer/core_ui/cui_draft_tan.c`; decompiles `decomp/draft/
tan_core.c`, `tan_core2.c`, `tan_cad.c`, `perp_cad.c`. Every rule cites its source in the code.
* **Tangent**: FUN_180048b10(6, -1); pick 1 (flags 10, mask 4: an arc only; arcs of other views 27012) prompt mccadres 27708 "Create
  dimension, tangent: " (type 6 has no second line: the 0-pick table covers -1..5, asm 0x18004977f-0x180049791); pick 2 (flags 0xb, mask 7)
  27264 "Select an arc, line or point": a point -> mode 2, a line -> mode 4, another arc -> mode 1 (the same arc: refused). FUN_18004f420 /
  FUN_18004ee50 set-up (DfMaintainTang 0 at start, text4.c:150); placement prompt 27708 + 27054. Each move FUN_18004d9e0: the extremes
  (calc_tangent_rbox), the orientation from the cursor against both entities' box unless locked (FUN_18012ac50), the side of entity 1
  (FUN_18004e3b0, switched only within sel_dist), calc_tangent_eps (a failure flips the 2nd side once, FUN_18004f910, then 27010);
  calc_tangent_dimension. Accept FUN_18004ec40 -> FUN_18004df60(1). Record: +0x70 'C' (t 0), +0x108 / +0x1a0 by mode, +0x68 mode,
  +0x238 input orientation, +0x240 hvp, +0x248 maintain, +0x249 anchor, +0x24a / b sides, +0x250.. r / start / sweep.
* **Perpendicular**: pick 1 (flags 10, mask 2: a line; 27013 parallel to Z) prompt 27727 "Create dimension, perpendicular:" + mccoreres
  1892 "Select a line"; pick 2 (flags 0xb, mask 3) 27729 "Select a parallel line or a point": a point -> mode 1, a parallel line (no l_ints
  intersection) -> mode 2, an intersecting line: refused. FUN_180048270 / FUN_180048460 (the four aPoints) / FUN_1800480c0 (DfHvpAngle =
  find_orient_angle of the first line); placement prompt 27053 only. Each move FUN_180047320: the pair nearest the cursor (FUN_180047aa0),
  locked when H / V (FUN_180047ca0), else determine_h_or_v + determine_hvp(param_5 0); calc_perp_dimension. Record: +0x68 mode, aPoints
  +0x70 / +0x108 / +0x1a0 / +0x238 (mode 1: a 'K' at the cursor), +0x2d0 / +0x2d1 pair, +0x2d2 orientation, +0x2d8 hvp, +0x2e0 anchor.
* NOT DECODED / refused: DfAssoc off for the tangent (DfTurnOffApAssoc FUN_1802f20a0) - refused with a message; arcs of other views;
  mccadres message 12 of FUN_18004ee50 (not in the string table); panel: digits (FUN_18012bca0), id 18 (second-side flip), Maintain (26)
  greyed; angle (id 8, FUN_18004eca0) decoded, not wired. Phantom and '~' not ported.
* Verified: oracle `tools/tests/draft_tan_test.sh` - tangent corpus 8 readable of 10 (2 in views 19 / 21): 2017-written 1 / 1 fully equal;
  older 3 equal + 4 equal with trailing zeroes on (the older-records category of the linear oracle). Self-test (creation vs regeneration
  on synthetic geometry): tangent 40 / 40; perpendicular 13 / 24 - 10 differ by exactly 180 degrees in the offsets (calc_linear_dimension's
  re-derived angle at creation vs calc_linear_angle(+0x2d2) at regeneration: faithful to the decompile, Mastercam's regeneration would flip
  the text side too - no corpus record to confirm), 1 only in the 'K' 4th aPoint. App (draft_ui_test.sh F): arc + point 55 / 50,
  perpendicular 20; exported tangent records 2 / 2 regenerate fully equal; the perpendicular one equal with Ctrl held (the one-sided
  placement, linear category 2).

## 15. Point (0x947f, type 4) - in the app (2026-10-27, pass 3)
Code `viewer/draft_create_pt.{c,h}` (calc_point_dimension @18024ed10, FUN_18030f410 kind 7 -> FUN_180310700, FUN_180251460 slot 0,
FUN_180250bb0 leader, FUN_18023d590 case 4, the regeneration FUN_1803042b0 case 4 -> FUN_180305b40), `viewer/core_ui/cui_draft_pt.c`,
display `viewer/draft.c` (pt_value = calc_dim_note case 4); asm / decompiles `decomp/draft/pt_calc.s`, `pt_f410.s`, `pt_250bb0.s`,
`pt_310700.s`, `pt_regen.c`.
* FUN_180048b10(4, -1); get_event flags 9 (a point) then 0xc, mask 0; prompts mccadres 27713 "Create dimension, point: " + 27064 "Select a
  point to create a point dimension" / 27051 "Sketch the text position for the point dimension"; the drag = FUN_180048be0's default branch
  (calc_point_dimension(drag, slot 0, the sketched point, view); 27010 + abandon on failure); accept FUN_180049e80 -> FUN_180049250(1)
  (store, the live entity). Panel: Leaders Inside / Outside, Centered text live; digits (FUN_18012bca0) greyed.
* Config 12009 "Absolute?" ptdim_abs, 12010 "3D label?" ptdim_label, 12011 "3D?" ptdim_use3d (core_cfgdraft.c:812-814).
* Record: +0x68 the aPoint, +0x100 / +0x101 / +0x102 use3D / labels / absolute, +0x308 = 4, +0x310 the cursor (view), **+0x338 = the point's
  view x only**, the four wl slots off then leader 1 (4 points: the point with the arrow, a bend at half way - horizontal within pi/4 of
  horizontal, else vertical -, the two shoulder points at the text edge), +0x7a6 u64 = 1, +0x7ae = 1, tolerance mode +0x7b6 / +0x7b8.
* The value (FUN_180310700): the point's view coordinates (minus the active origin when on; world with Absolute) x draft_scale,
  `X 1.50, Y 2` with labels (+ `, Z ..` with 3D; the creation's Z = the cursor's depth, the display's = the point's own), lathe forms
  `X r, Z x` / `D 2r, Z x` (ported, not reachable: mill planes), digits 0 forces leading / trailing zeros.
* NOT PORTED / refused: tolerance modes 1-3, coordinate formats 2-4, view 0; DraftGlobalsLastOrigin; the drag-edit FUN_180049070 (decoded);
  Smart's DfDragPtDim branch; demo two_sig rounding.
* Verified (no corpus record of type 4): tools/tests/draft_pt_test.sh - 16 / 16 hand-derived value strings; 122,880 synthetic records,
  121,206 regenerate byte-equal, the rest explained (880 cursor-on-the-point side ties, 794 views 7 / 8 matrix round trips); draft.c draws
  all of them; the corpus display counts / SVGs byte-identical before and after. App (draft_ui_test.sh G): `X 40.00, Y 20.00`, regenerates
  equal, drawn after export.
