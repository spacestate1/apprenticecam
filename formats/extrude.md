# Solids > Extrude (Mastercam 2017) - implementation spec

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

Decoded 2026-10-15 from `mcgeomsld.dll` (Ghidra project `~/mastercam-re/ghidra_sld`, name `sld`), the WPF panel
`mcgeomsld.ui.dll` (BAML `solids/extrude/solidsextrudemanagerview.baml`, tooltips resx
`MCGeomSld.UI.Solids.Extrude.SolidsExtrudeManagerViewToolTips.resx`), the C++/CLI bridge `mcgeomsld.interop.dll`
(`MCGeomSld.Interop.SolidsExtrudeManagerViewModel`) and the string table of `mcgeomsldres.dll`
(`~/mastercam-re/regress/str/sldres.tsv`, copy in `decomp/extrude/sldres_str.tsv`; ids below are decimal, "sld 470" =
mcgeomsldres string 470).

Rule: everything below is read from code or data, with its citation. Items marked **NOT DECODED** are unknown; do not
invent them. Decompiles (for re-checking) are in `decomp/extrude/`: `b1.c`..`b8.c` (Ghidra DumpDecomp; `b7.c` =
`FUN_180039370` + `chn_get_normal` and their callees, `b8.c` = the panel loop `FUN_1800d8620` / dispatcher at
depth 0),
`model_slots.c` / `model_slots.txt` (the model vtable bodies), `baml.txt` (the BAML dump), `core1.c` (mccore
`get_event`, `set_chain_closed`), `dlgslots.txt`, `vt.txt`, `refs.txt`, `refs2.txt`, `sldres_str.tsv`. Not kept in the
repo (session scratchpad only, temporary): `d8620.s` (disassembly of the panel loop), the Ghidra logs, the `interop/` and
`ui/` dump folders, `fx.py`, `mcgeomsld.dll.str`, and the earlier `../extr/app.c`, `../extr/app2.c`.

## 0. Classes and the parameter block

| Object | Where | Role |
|---|---|---|
| `SolidsExtrudeApp` | vtable 0x1802d99c0; ctor `FUN_1800d73b0`; FT command name `"sld_newExtrude"` (ctor last line) | the command |
| `SolidsExtrudeManagerDialog` | ctor `FUN_18012fe70` (0x16f0 bytes), at app+0x2dc0 | MFC function-panel host of the WPF view |
| `SolidsExtrudeManager` | ctor `FUN_18012bef0`, vtable 0x1802dfef8, at dialog+0x1698 | the observable model the WPF view binds to |
| `SolidsExtrudeManagerValidator` | vtable 0x180301af0, `FUN_18020fbd0` | panel validation |
| `SolidsExtrudeManagerViewModel` | mcgeomsld.interop.dll | WPF properties -> model vtable offsets |
| `SldExtrudeOp` | ctor `FUN_18027abc0` (op type 2 at +0x30) | the history operation |

SolidsExtrudeApp vtable (0x1802d99c0): [0] `d8200` dtor, [1] `dec40` load edit op, [2] `df180` set event mask,
[3] `d8b90` start, [4] `d8620` panel loop, [5] `d83b0` end, [6] `dc330` panel -> params, [7] `df0c0` params -> panel,
[8] `db930` per-event tail (sld_dirty target, update_graphics).

### SLD_EXTRUDE_PARAMS (app+0x2a8, 0x70 bytes; "P+x" below)

Mapping from `FUN_1801308a0` (panel -> P, called by slot 6) and `FUN_180132310` (P -> panel, "SolidsExtrudeManagerDialog::SetParams",
see its _MCAssert string), model offsets from the setters in `model_slots.c`:

| P+ | type | meaning | model field / property name (setter string) | model vtable get/set |
|---|---|---|---|---|
| 0x00 | int | operation type 0 Create body, 1 Cut body, 2 Add boss (BAML keys OperationTypeCreateBody/Cut/Boss = 0/1/2) | +0x50 "OperationType" | 0x78 / 0x80 |
| 0x08 | DB_LIST_ENT* | target body | +0x58 "TargetBody" (shared ptr) | 0x88 / 0x90 |
| 0x10 | int | distance method 0 Distance, 1 Through all (BAML DistanceMethodDistance/ThroughAll) | +0x8c "DistanceMethod" | 0xc8 / 0xd0 |
| 0x18 | double | Distance | +0x90 "Distance" | 0xe8 / 0xf0 |
| 0x20,0x28,0x30 | 3 double | extrusion vector (copy of the first chain's direction, see sec. 4.1) | (shown as string "Vector" +0xe0) | 0x1c8 / 0x1d0 |
| 0x38 | bool | Trim to faces | +0xba "TrimToFaces" | 0x118 / 0x120 |
| 0x39 | bool | reverse direction (toggled by the "Reverse all" button) | +0xb8 "ReverseDirection" | 0xf8 / 0x100 |
| 0x3a | bool | Both directions | +0xb9 "BothDirections" | 0x108 / 0x110 |
| 0x3b | bool | Split (draft) | +0xc9 "Split" | 0x168 / 0x170 |
| 0x40 | bool | Draft | +0xbc "Draft" | 0x138 / 0x140 |
| 0x44 | int | draft Reverse (native name "DraftOutward"; WPF property `ReverseDraft`) | +0xc8 "DraftOutward" | 0x158 / 0x160 |
| 0x48 | double | draft angle, **radians** in P, degrees on the panel (x 0.01745329251994329 / x 57.29577951308233) | +0xc0 "DraftAngle" | 0x148 / 0x150 |
| 0x50 | bool | Thin wall | +0xcb "ThinWall" | 0x188 / 0x190 |
| 0x54 | int | thin wall direction 0 Direction 1, 1 Direction 2, 2 Both (BAML ThinWallDirection1/2/Both) | +0xcc "ThinWallDirection" | 0x1b8 / 0x1c0 |
| 0x58 | double | Direction 1 thickness | +0xd0 "ThinWallInwardDistance" | 0x198 / 0x1a0 |
| 0x60 | double | Direction 2 thickness | +0xd8 "ThinWallOutwardDistance" | 0x1a8 / 0x1b0 |
| 0x68 | bool | Draft ends | +0xca "DraftEnds" | 0x178 / 0x180 |

Other app fields: name char[0x40] at app+0x61 (model +0x30 "Name", get 0x68 / set 0x70); "Create a single operation"
bool at app+0x340 (model +0x89, get 0xb8 / set 0xc0); per-chain data `CArray<SLD_EXT_CHAIN_DATA>` (0x60 bytes each:
direction 3 doubles + PLANE_sf) at app+0x230; trim faces `CArray<int>` at app+0x318; chain list manager at app+0x2dc8;
target tag app+0x2db8 / eptr app+0x2b0 (= P+0x08); edit flag app+0x20; arrow-drag state (short) app+0x3c8.

## 1. Command flow

### 1.1 Start (slot 3 `FUN_1800d8b90`)
1. Ctor `FUN_1800d73b0` (new op, param_2 == 0): `RegisterVariable` the unit-dependent defaults (sec. 2.4), copy the
   session globals DAT_1803e89b0.. into P (if `sld_get_num_solids_visible() < 1` the stored type is forced to 0
   Create), single-op flag = DAT_1803bab19, name = sld 707 "Extrude", then `FUN_1800db220(type)` renames it to the
   default name of the type (sec. 6). Ends with `ps_reset_all_hilights`, `set_clr_sel_bits(...)`.
2. `FUN_1801575e0`: if no chains are held, the chain-manager callback `FUN_1800dcca0 -> FUN_1800dc600` runs chain
   selection (1.2). If that fails or is cancelled the command ends (slot 5) without a panel.
3. `FUN_180164f40`: `BeginProcess(get_FT_command(..) + 0xa06b)`, `HistDlg_enable(1)`.
4. `FUN_1800dccc0`: creates the panel (title sld 846 "Solid Extrude", `CreateInFunctionPnl`), `SetParams(P, name,
   chain data, single-op)` (`FUN_180132310`), and sets the model's Preview to DAT_1803bac10 (dialog vtable +0x3a0 ->
   `FUN_1801328c0`).
5. `FUN_180157140` (new op only): if the first chain has an entity (`FUN_18000b800`) and **not every chain is closed**
   (`FUN_18000aef0` tests CHAIN+8; CHAIN+8 is the closed flag written by mccore `set_chain_closed`, core1.c), Thin Wall
   is switched on (callback slot +0x38 -> `FUN_1800df980` -> `FUN_180132910` -> model ThinWall setter) and the panel
   is read back. Then `FUN_180157090`: if exactly **one** solid is visible it becomes the target body (tag + eptr);
   otherwise target = none.
   If Thin Wall went from off to on here, app+0x2e78 = 1 ("thin wall was switched on automatically"; d8b90).
6. `FUN_1800d8d70` (new op, no explicitly chosen target): looks for a body the chains belong to (`FUN_1800da210`,
   `FUN_1800da960`), sets it as target and negates the direction of chains flagged by that search. Skipped in edit mode and
   once Select Target Body was used (app+0x478). Also runs after every Add / Rechain (b1.c:2418). **Ported 2026-11-08**
   (`ex_target_search`, `solid_target_search`). Rule, from `FUN_1800da960`: the test op is the panel's with method Distance and
   Distance = the diagonal of the box around the visible bodies and the chains (`FUN_1800da210`). One tool per chain group, tested
   through the group's first chain. A body stays when every group's tool clashes with it, or (Both directions off) the reversed tool
   does, and such a group is flagged. Of several bodies: smallest mean of min(|far side - chain plane|, |near side - chain plane|)
   (`FUN_1800d9d40`), within tol the fewest flags. `PsClashTest` (0x1800308d0) returns 1 for no clash or a type-0 clash and 3 for any
   other clash. **Inference, not decoded:** touching without shared volume is treated as no clash, on the assumption that this is
   Parasolid's type 0 (no Parasolid header in the files). No real Mastercam run compared. `FUN_1800da050` (Automatically determine
   operation type, run right after) is not ported.
7. `FUN_180165220`: Parasolid mark at app+0x114 (`FUN_18004f970` -> `PK_MARK_create`), app+0x131 = 1.

### 1.2 Chain selection (`FUN_1800dc600`)
- Prompt sld 470 "Select chain(s) to extrude"; `chain_it(prompt, &chain)` (mccore chaining dialog with its own
  defaults - this call passes no mode/mask; which entity kinds the dialog allows is **NOT DECODED** here).
- chain_it non-zero (cancel) or no chain while no panel exists -> return 0 (command ends).
- Every chain must be planar: `FUN_18000b140` (per chain `get_chain_plane` + every entity in that plane within
  nc_tol). Failure -> `message_box_ok(657, 201, 3)`: sld 657 "Operation requires planar chain set.", caption sld 201
  "Error Processing Solid"; chains unselected; returns 0. Chains in *different* planes are allowed (they are grouped
  per plane later, `ps_sort_multiplane_chains_for_sheets`).
- Initial direction of each chain (stored in its SLD_EXT_CHAIN_DATA): `chn_get_normal`, normalised; reference =
  a vector from the active view-0 matrix (`MastercamPlanes::GetActiveViewNo(Planes,0)`, `get_view_matrix`,
  `FUN_18000ec80`; the "Set C-Plane +" button documents it as the C-plane +Z, tooltip SetCPlanePlusButton_ToolTip).
  If normal . ref < 0 the **chain itself is reversed** (`reverse_chain`) and the normal recomputed; if it still points
  away it is negated. Chains without a normal get the reference vector. Plane = `get_chain_plane_sf`.
- `chn_get_normal` itself (b7.c): the chain plane's normal from `get_chain_plane` (an arc's own view; else the cross
  product of the first three non-collinear entity points matched against the existing views within 1e-10, else a new
  view via `make_view_k` / `AddMCView`) + `get_view_matrix`. Only for a CLOSED chain (CHAIN+8) the signed
  turning-angle sum over the chain (`FUN_18000bf30`: the angle between the incoming and outgoing tangents at every
  vertex, plus each arc's own sweep, splines sampled at 2n-1 points, lines contribute nothing, all measured in the
  chain plane) is compared with -2 pi: when |sum + 2 pi| < tol (strictly) the chain winds clockwise and the normal
  is flipped. Open chains keep the plane normal unflipped (no winding test).
- Open chains are accepted here (they switch Thin Wall on, 1.1 step 5).

### 1.3 Panel loop (slot 4 `FUN_1800d8620`, dispatcher `FUN_1800dba00`)
Event mask (slot 2 `FUN_1800df180`): app+0x220 = 0x8202, +0x224 = 0x8f; 0x1400000 added when app+0x342
(= !sldPrototypingEnabled()). Prompt while waiting: sld 851 "Use dialog to select chain(s) and to modify extrude
settings" (new op, no drag), empty in edit mode / during a drag, "*" while an arrow is being dragged.
`get_event(prompt, ev, mask, mask2, 1, &got)` (mccore, core1.c); `got` = an event was delivered.

| get_event / event | handler | effect |
|---|---|---|
| returns 1 | `FUN_1800dd1d0` | arrow drag active: cancel the drag, restore the pre-drag values (`FUN_1800dc500(1)`); otherwise leave the loop **not accepted** (= Cancel). analyze.md: non-zero return = Escape / right-click. |
| returns 2, or no event | - | leave the loop, not accepted |
| 0x400000 (GrArrow picked) | `FUN_1800ddc70` | start dragging an arrow; OK/Apply/Regen/tabs disabled; prompt sld 505 "Select new position or Esc to cancel" |
| 0x800000 (drag move) | `FUN_1800dd7c0` | live value: distance arrow -> Distance = abs(offset) (negative offset flips all chain directions); Direction 1 arrow -> P+0x58 and ThinWallDirection 0 -> 2 (Both); Direction 2 arrow -> P+0x60 and 1 -> 2 |
| 0x1000000 / 0x2000000 | `FUN_1800dd960` / `FUN_1800ddab0` | end the drag, commit, re-enable the buttons |
| 0x200 (panel message) | `FUN_1800dcf50` -> `FUN_1800dd4a0`, else `FUN_180156e70` | see 1.4 |
| 8 with key 0x0d (Enter) | dialog +0x3a8 `FUN_180156ec0` | ev+0x150 != 0 -> OK, else Apply (meaning of ev+0x150 **NOT DECODED**) |
| 0x8000 with ev+0x158 == 0x36 | `FUN_1800dd2a0` | solids changed: re-validate the target; if the target is gone and no body remains: name "Extrude", type Create, target none |
| 2 (entity pick) | `FUN_1800dde10` | if the picked entity belongs to a selected chain: negate that chain's direction (click a chain = reverse it) and redraw its arrows |
| 1 | CleanEvent | rebuild |

After every event (d8620 0x1800d87e7..0x1800d8b07): slot 6 (panel -> P), `FUN_1800db3c0` (per-chain thin-wall side
flags, sec. 4.6), then if the "rebuild" flag is set:
- final (message 0x2bd OK or 0x2c0 Apply), new op, >= 1 chain: if `app+0x8 > abs(Distance)` ->
  `message_box_ok(1101, 201, 3)` sld 1101 "Below minimum distance." (0x1800d892f; app+0x8 = tol * 10, SolidsAppBase ctor
  `FUN_180164c60`, decomp/extrude/b5.c:146);
  else `sld_extrude(chains, target tag, faces, sideflags, P, chaindata, name, op=0, single, list, bodies, 0x1c,
  preview=0)` (0x1800d89a1).
- final in edit mode: `FUN_18027c5a0(op, faces)` only (0x1800d89f8); the regen itself is done elsewhere, **NOT DECODED**.
- not final (a preview request): `FUN_180156d90` (log to `%TEMP%extrude.tmp`), the same min-distance check, then
  `sld_extrude(..., preview=1)` (0x1800d8ab8), `FUN_180156d60`, `FUN_180156bc0` (the log's lines longer than 4 chars are
  shown with `DisplayWarning`), and on failure `PK_MARK_goto(app+0x114)` (`FUN_18004f660`).

### 1.4 Panel messages (WM 0x8028, posted by the model / dialog)

| wParam | source | handler |
|---|---|---|
| 0x2bd | OK button (dialog +0x308 `FUN_180156e40`) | accept, leave loop |
| 0x2be | Cancel (+0x310 `FUN_180156df0`) | not accepted, leave loop |
| 0x2bf | Preview button (model 0x258) and every auto-preview change (`FUN_180131d10`) | rebuild preview, stay |
| 0x2c0 | OK-New (= Apply) and OK-Regen (+0x3b0 `FUN_180156dc0`) | accept, leave loop, app+0xb0 = 1 (restart) |
| 0x2c1 lParam=flags | `FUN_180131d10` | `FUN_1800dea90` -> `FUN_1800dfb70(flags)` redraw arrows (1 distance, 2 Direction 1, 4 Direction 2) |
| 0x321 lParam 1/0 | Add chains / Rechain all (model 0x2a0 / 0x2a8) | `FUN_1800de090` (1.5) |
| 0x322 | AutoCursor distance button (model 0x2b0, also sets DistanceMethod = 0) | `FUN_1800de680` (4.2) |
| 0x323 lParam 0/1 | Add selection / Reselect all faces (0x2c8 / 0x2d0) | `FUN_1800de480` |
| 0x324 | Select target body (0x288) | `FUN_1800ddf50` |
| 0x325 idx | Remove chain (0x290 -> `FUN_1801306a0`) | `FUN_1800dcfb0` -> `FUN_1801572d0` (unlink + free chain, drop its chain data) |
| 0x326 idx | Reverse chain (0x298 -> `FUN_180131d90`) | `FUN_1800dd6a0`: negate that chain's direction |
| 0x327 tag | Remove face (0x2b8) | `FUN_1800dd010` |
| 0x328 flags | `FUN_180131d10` when `FUN_1801307a0` != 0 | `FUN_1800dced0` -> `FUN_1800d96f0` (auto operation type, 4.8), then redraw |
| 0x329 | ThinWall property changed | app+0x2e78 = 0 |

`FUN_180156e70`: 0x2bd -> loop=0, inner=0, return 1; 0x2be -> loop=0, inner=0, return 0; 0x2bf -> inner=0, return 1;
0x2c0 -> loop=0, inner=0, restart=1, return 1. The return is the "rebuild/accepted" flag.

### 1.5 Add chains / Rechain all (`FUN_1800de090`)
Re-runs 1.2 with the panel suspended. Add (lParam 1): `append_chains`, new chain data appended. Rechain all (0): old
chains unselected and freed, replaced. In edit mode, Rechain all with no new chain -> MessageBox(sld 644 "Base chain
cannot be deleted", caption sld 745 "Delete Chain", MB_ICONINFORMATION) (`FUN_180157710`). If the new set has an open
chain, Thin Wall is switched on (`FUN_180132910(dlg,1)`). Then `FUN_1800d8d70` (target search) and, if
CalculateMinimumDistance (model 0x128), `FUN_1800da050` (4.4). Remove chain in edit mode on the first chain shows the
same 644/745 box (model 0x290 `FUN_18012d280`).

### 1.6 End (slot 5 `FUN_1800d83b0`)
- If accepted (app+0x130, set from the loop's flag): copy P to the session globals DAT_1803e89b0..0x1803e8a1f
  except: Thin Wall is stored off if it was switched on automatically and never touched (app+0x2e78), the vector is
  the first chain's direction, the target (DAT_1803e89b8) is cleared; single-op -> DAT_1803bab19. Cancel stores
  nothing.
- Remove arrows, `ClosePrompt`, delete the start mark (`FUN_1801650e0` -> `FUN_18004e410` = `PK_MARK_delete` + drop
  extra partitions), `set_clr_sel_bits`, `ps_reset_all_hilights`, `HistDlg_enable(0)`, store the panel's Preview flag
  into DAT_1803bac10 (dialog +0x398), destroy the panel.
- New op: chains unselected and freed. **Apply**: if accepted and app+0xb0, `post_FT_command("sld_newExtrude")` -
  the command starts again from 1.1 (new chain selection) with the just-stored parameters.
- `EndProcess`, `UpdateLevelsManager`, `update_graphics`.
- Auto operation type checkbox -> DAT_1803e89a0 (new op only, 0x1800d8b1f).

### 1.7 Editing an existing extrude
Ctor with an op (param_2 != 0): app+0x20 = 1, ModifyParams = 1. Slot 1 `FUN_1800dec40` loads the op: for Cut/Boss
the target comes from the op (`sld_get_tag_from_solid`), faces from the op (`FUN_18027af50`) into the face list
(`FUN_1801313e0`, names "Face N"); Create clears target. Panel differences: OK-Regen instead of OK-New
(BAML `HasOKRegenButton="{Binding ModifyParams}"`), Create body radio and "Create a single operation" disabled
(`IsEnabled = !ModifyParams`), Cut/Boss enabled iff type != Create (`EnableCutBoss`), AutoOpType disabled,
CalculateMinimumDistance reads false (model 0x128). In edit mode OK with unchanged parameters (`FUN_180130490`
compares P, vectors, thin wall) does not rebuild (0x1800d8866). The regeneration pipeline is **NOT DECODED**.

## 2. The panel (WPF `SolidsExtrudeManagerView`, title sld 846 "Solid Extrude")

FunctionPanel buttons: OK, Cancel, Help, OK-New (shown when !ModifyParams, enabled by EnableApply), OK-Regen (shown
when ModifyParams, EnableRegen) (BAML FunctionPanel element). OK/Apply/Regen and both tabs are disabled during an
arrow drag (`FUN_180130720/760/780/740/700(0)`). The panel shows an error state when any of NameTextBox,
ChainsListBox, ExtrudeDistanceEvaluatorBox, TrimFacesListBox, DraftAngleEvaluatorBox, Direction1/2EvaluatorBox has
a validation error (BAML `HasError` MultiBinding). Underscore = access key.

Expander open states are static (kept for the session): Operation, Chains, Distance open; Trim to Faces, Draft,
Thin Wall closed (ViewModel `s_*Expanded`). Checking Draft / Thin Wall / Trim to Faces opens its expander (setters).

### 2.1 Tab "Basic" (enabled by EnableBasicTab)

**Operation** expander (icon ImageExtrude16)
| control | label | binding / rule |
|---|---|---|
| TextBox NameTextBox | "_Name:" | Name; validator: blank -> sld 2001 "Name cannot be blank" |
| RadioButton | "Cr_eate body" | OperationType == 0; enabled = !ModifyParams |
| RadioButton | "C_ut body" | OperationType == 1; enabled = EnableCutBoss |
| RadioButton | "Add bo_ss" | OperationType == 2; enabled = EnableCutBoss |
| read-only TextBox + Button (ImageSelect16) | "_Target" | TargetBodyName ("(None)" sld 858 when no target, else the body's name from `FUN_18012fa00`); text enabled = EnableCutBoss; button enabled = EnableSelectTargetBody = (new op and > 1 visible solid) (dialog ctor `FUN_18012fe70`); focus highlights the body's edges (model 0x2e8/0x2f0) |
| CheckBox | "Create a single _operation" | CreateASingleOperation, default on (DAT_1803bab19 = 1), enabled = !ModifyParams |
| CheckBox | "_Automatically determine operation type" | AutoOpTypeChecked (default off, DAT_1803e89a0 = 0, kept for the session), enabled = AutoOpTypeEnabled |

EnableCutBoss (ViewModel): new op -> a target body is set; edit -> OperationType != 0. AutoOpTypeEnabled / forced type
(`FUN_180132310` / `FUN_1801308a0` tail): if (new op and no visible solid) or (edit and type Create) or (new op and no
target) -> OperationType = 0 and AutoOpTypeEnabled = 0; else EnableModifySolidBody = 1 and AutoOpTypeEnabled = !edit.
On panel init with no visible solid: DistanceMethod 0, type 0 (`FUN_180131600`).

**Chains** expander ("_Chains", icon ImageChains16)
- ListBox ChainsListBox, extended selection; items "Chain  N" (sld 749 "Chain " + `"%s %d"`, N from 1 -
  note the double space), `FUN_1801311f0`. Focus = selected chains drawn highlighted (`draw_chain_sel`), lost focus =
  unselected. Delete key = Remove. Item context menu: Remove, Reverse, Add, Rechain all; background menu: Remove
  (disabled), Reverse (disabled), Add, Rechain all.
- Buttons (right aligned): Reverse all (ImageReverseDirection16) = toggle ReverseDirection (model 0x2c0
  `FUN_18012d770`); Add chains (ImageAddChain16) -> 0x321,1; Rechain all (ImageChains16) -> clears list, 0x321,0.
- Validator: no chain -> sld 2015 "Extrude requires at least one chain".

**Distance** expander (icon ImageMeasure16)
| control | label | rule |
|---|---|---|
| RadioButton + EvaluatorBox ExtrudeDistance + Button (ImageAutoCursor16) | "_Distance" | DistanceMethod == 0; Distance; AutoCursor button -> 0x322 |
| RadioButton | "Th_rough all" | DistanceMethod == 1; enabled = Cut body radio checked; setting it clears TrimToFaces (model 0xd0 `FUN_18012dd30`) |
| CheckBox | "_Both directions" | BothDirections |
SetParams forces DistanceMethod = 0 if method > 1, Trim to faces is on, or method 1 with type != Cut (`FUN_180132310`).
Validator: Distance < tol -> sld 2016 "Distance must be greater than 0" (`FUN_180210260`, checked whatever the method).

**Trim to _Faces** expander (checkbox header, icon ImageChains16)
- Checkbox = TrimToFaces, enabled when OperationType != 0; content enabled when DistanceMethod == 0 and type != 0 and
  checked.
- ListBox TrimFacesListBox (items "Face N", sld 687 "Face"), Delete = Remove; buttons Add selection
  (ImageAddSelection16, 0x323,0) and Reselect all (ImageSelect16, 0x323,1); context menu Remove / Add selection /
  Reselect all. Face picking (`FUN_1800df990`): prompt sld 443 "Select faces to extrude to. Press Enter when done.",
  `sld_array_pick`; the picked faces' body becomes the target.
- Advanced part: CheckBox "Calculate _minimum distance to face" = CalculateMinimumDistance (default on,
  DAT_1803bab70 = 1, stored on change when not editing; `FUN_18012dc70`).
- Validator: type != 0, TrimToFaces, no face -> sld 2017 "Trim to faces requires at least one face".

### 2.2 Tab "Advanced" (enabled by the same flag, ViewModel uses slot 0x238/0x240 for both tabs)

**Draf_t** expander (checkbox header = Draft)
| control | label | rule |
|---|---|---|
| EvaluatorBox (StepValue 1.0) | "Angl_e" | DraftAngle (degrees), enabled = Draft; validator: Draft and not 0 <= a <= 89 -> sld 2018 "Draft angle must be between 0 and 90 degrees" (`FUN_1802101f0`; the code limit is 89) |
| CheckBox | "Re_verse" | ReverseDraft = native DraftOutward (P+0x44), enabled = Draft |
| CheckBox (advanced) | "_Split" | enabled = Draft and BothDirections |
| CheckBox (advanced) | "Draft e_nds" | enabled = Draft and ThinWall |

**Thin _Wall** expander (checkbox header = ThinWall)
| control | label | rule |
|---|---|---|
| Label | "Direction:" | enabled = ThinWall |
| RadioButtons | "_Direction 1", "Di_rection 2", "_Both" | ThinWallDirection 0/1/2, enabled = ThinWall |
| EvaluatorBox | "Direction _1:" | ThinWallInwardDistance, enabled = EnableDirection1 = ThinWall and dir in {0,2}; validator < tol -> sld 2019 "Direction 1 distance must be greater than 0" |
| EvaluatorBox | "Direction _2:" | ThinWallOutwardDistance, enabled = EnableDirection2 = ThinWall and dir in {1,2}; validator < tol -> sld 2020 "Direction 2 distance must be greater than 0" |

**Plane _Orientation** expander (icon ImagePlaneOrientation16)
- MaskedTextBox, regex `\-{0,1}(\d*\.)?\d+[,]\-{0,1}(\d*\.)?\d+[,]\-{0,1}(\d*\.)?\d+` = Vector (model default string
  "0.0, 0.0, 1.0", ctor `FUN_18012bef0`). Displayed value (`FUN_180132ab0`): the first chain's direction converted to
  the active view (`world_to_view`), each component as `%ld` when within tol of an integer else `%.4lf`, joined with ",".
- Buttons: Set normal to chain (ImageChains16; each chain's direction = its `chn_get_normal`, `FUN_1801329f0`),
  Set C-Plane + (ImagePlane16; every direction = a view-matrix vector of the active view, `FUN_180132930`),
  Select vector (ImageSelect16; `sld_extrude_prompt_for_dir`: prompt sld 905 "Select line, edge or two points to
  specify extrusion direction." mask 0x4003; a point pick asks sld 485 "Set extrusion direction." with a rubber band).
- Typing a vector (`FUN_180131720` -> `FUN_180131fb0(dlg,0)`): `parse_pnt`, `SubtractActiveOrigin`, and **every**
  chain's direction = that vector (Distance unchanged because the call passes 0). Unparsable text: every chain's
  direction = its own `chn_get_normal`.

**Preview** expander (icon ImageAutoPreview16)
- CheckBox "_Automatically preview results" = Preview (model default 1, then DAT_1803bac10, initial 1, shared by the
  solids commands through SolidsAppBase and saved at the end).
- Button "_Preview", enabled = !Preview, posts 0x2bf.

### 2.3 Which property changes do what (`FUN_180131750`, the dialog's model observer)
| property | action (dialog +0x390 = `FUN_180131d10(flags)`) |
|---|---|
| Preview, Name, AutoOpTypeChecked, DistanceMethod, BothDirections, Draft, DraftAngle, DraftOutward, Split, DraftEnds, TrimToFaces | flags 0 |
| ReverseDirection, Distance | flags 1 |
| OperationType | default name for the type (`FUN_180132190`, only if the name is one of the 3 defaults); new op: AutoOpTypeChecked = 0; flags 0 |
| ThinWall | post 0x329, flags 6 |
| ThinWallDirection | flags 6 |
| ThinWallInwardDistance / OutwardDistance | flags 2 / 4 |
| Vector | parse (above), flags 7 |
| anything else | nothing |
`FUN_180131d10`: if `FUN_1801307a0` (auto op type due: new op, >= 1 visible solid, and dialog mode
(dialog+0x16e8) == 2 with AutoOpTypeEnabled and AutoOpTypeChecked, or mode == 1) -> post 0x328(flags); otherwise post
0x2bf when Preview is on and 0x2c1(flags) when flags != 0. Who sets dialog+0x16e8 to 1/2 is **NOT DECODED**.

### 2.4 Defaults and persistence
Static block 0x1803b97d0 copied to the globals by the CRT initializer at 0x180002340 (also the SldExtrudeOp defaults,
`FUN_18027abc0`). Inch values from the block, metric from `RegisterVariable(&v, metric, inch)` (argument order per
formats/wireframe.md, mccore @1805fa3a0):

| field | inch | metric | other |
|---|---|---|---|
| type | Create (0) | | forced 0 when no solid is visible |
| target | none | | never persisted |
| method | Distance (0) | | |
| Distance P+0x18 | 1.0 | 50.0 (RegisterVariable(&,50,1)) | |
| vector P+0x20..0x30 | 0,0,0 | P+0x30 registered (50, 1) - meaning for a direction **NOT DECODED** | |
| Trim / Reverse / Both / Split | off | | |
| Draft | off, angle 0.08726646259971645 rad = 5 deg, Reverse off | | |
| Thin Wall | off, Direction 1 (0) | | |
| Direction 1 / 2 thickness | 0.1 / 0.1 | 1.0 / 1.0 | |
| Draft ends | off (bss) | | |
| Create a single operation | on (DAT_1803bab19 = 1) | | |
| Auto operation type | off (DAT_1803e89a0) | | |
| Calculate minimum distance | on (DAT_1803bab70 = 1) | | |
| Automatically preview | on (DAT_1803bac10 = 1) | | |
| name | "Extrude" / "Extrude Cut" / "Extrude Boss" by type | | |
All persistence is in-process session globals (no registry / config key was found); they are written only on OK /
Apply (1.6).

## 3. Preview
- Rebuilt on: every panel change when "Automatically preview results" is on (2.3), the Preview button, chain / face /
  target / distance picks (they call dialog +0x390 with 7), arrow drags. Not rebuilt when the min-distance check fails.
- What is drawn: the real `sld_extrude(..., preview=1)` result. In preview mode no undo transaction, no history op
  and no op file are made (`FUN_1800d38f0`, `FUN_1800d2c80`: op creation only when !preview); a new Create body is
  made with `sld_solid_from_tag` while DAT_1803e80f3 = 1, which makes `sld_draw_solid`/`sld_draw_solids` skip drawing
  during that call (0x1800b3b98, 0x1800b4b40). The preview's colour / transparency and how the previous preview body
  is removed before the next one or on Cancel are **NOT DECODED**.
- Warnings produced while previewing go to the log `extrude.tmp` and are shown afterwards with `DisplayWarning`
  (`FUN_180156bc0`); errors that use `message_box_ok` still pop up.
- Arrows (`FUN_1800dfb70`): flag 1 = the distance arrow, a draggable `SelRefArrow` anchored at a chain start point in
  the extrusion direction, length 0.1 x viewport height in pixels, offset = Distance (`FUN_1800dfcb0`, `FUN_1800d9330`);
  flags 2 / 4 = Direction 1 / Direction 2 thickness arrows, only while Thin Wall is on and the direction includes that
  side (`FUN_1800e0110`). Dragging: 1.3. Chain-direction display / colours of the arrows **NOT DECODED**.
- Reverse one chain: click it in the graphics (event 2, `FUN_1800dde10`) or list menu Reverse (0x326). Reverse all:
  the button toggles P+0x39.

## 4. Geometry (`sld_extrude` 0x1800d5730)

### 4.1 Direction
Per chain: its SLD_EXT_CHAIN_DATA direction (1.2, changed by reverse chain, Set normal, Set C-plane +, Select vector,
typed vector, AutoCursor). Sweep (`ps_sweep_sheet` 0x1800530c0 / `ps_sweep_wire`): direction normalised, negated when
P+0x39 (Reverse all), length = distance converted to Parasolid units (/1000 metric, /39.37007874015748 inch). The
group's first chain's direction is used for the whole group (`FUN_1800d38f0` loop, chain data lookup).

### 4.2 Distance
`FUN_1800d4a60`: d1 = d2 = Distance, except Cut + Through all: from the target's bounding box (`sld_compute_bbox`)
projected on each chain direction and every chain curve point (`CalculatePointsOnCurve`): d1 = max(bbox max - point),
d2 = max(point - bbox min) (swapped when P+0x39), bbox extended by 1000e-5 (metric) / 39.37007874015748e-5 (inch);
a result <= tol becomes 1.0. AutoCursor (`FUN_1800de680`): prompt sld 459 "Select point to extrude to (in
construction view).", one point; per chain the signed projection of (point - chain start) on its direction; if
abs > app+0x8: negative flips that chain; the first chain's value (made positive) becomes Distance. Distance <= tol at
sweep time -> error sld 315 "Invalid extrude distance." (code 0x13b, `ps_sweep_sheet`).

### 4.3 Both directions / Split / Draft
- Both: the profile body is copied (`PK_ENTITY_copy`), swept d1 one way and d2 the opposite way (`!P+0x39`), then
  `ps_boolean_union` (FUN_1800d2c80 / FUN_1800d38f0).
- Draft: `ps_draft_extrude_faces` -> `PK_FACE_install_surfs_isocline` on the swept side faces with the sweep vector and
  the angle; P+0x44 (Reverse) negates the angle, which flips the vector used (0x180041f20). Which way a positive angle
  tapers (in or out) depends on Parasolid's isocline convention: **NOT DECODED**. Errors: sld 555 "Failure drafting
  faces." (0x22b), sld 556 "Failure allocating memory." (0x22c).
- Second half of Both: draft-reverse flag = Split ? P+0x44 : !P+0x44 (so without Split the taper continues along
  the extrusion vector; with Split both halves taper the same way from the chain plane - tooltip SplitCheckBox_ToolTip).

### 4.4 Calculate minimum distance (`FUN_1800da050`)
When on and faces are picked: for each chain, intervals to the faces (`FUN_1800dc090`); a chain whose interval is
entirely behind (both ends < -tol) is reversed; Distance = the largest absolute end value if it exceeds 10 x app+0x8.

### 4.5 Chain grouping, closed / open
- Not thin: `ps_sort_multiplane_chains_for_sheets` groups the chains by plane (`FUN_18000b940`) and into sheet groups
  (`FUN_180039370`, decoded in b7.c); each group (count, chain list) becomes one sheet (`ps_chains_to_sheet_bodies`).
  `FUN_180039370`: every CLOSED chain (CHAIN+8) becomes a 2D boundary (`chain_to_pock_bound(chain, tol*10)`); open
  chains are skipped and reported through an out flag - after the sweep attempt sld 680 "Chains must be closed for
  extrusion into a solid body." (no title, `message_box_ok(0x2a8,-1,-1)`) and the result is failure. The boundaries
  are nested by `sort_pock_bounds` (tolerance 0.025 metric / 0.001 inch, from IsMetric; pock_bound byte +2 = island,
  +0x50 = parent, +0x58 / +0x60 = prev / next - layout in formats/dynamic.md; its internal containment algorithm is
  NOT DECODED - mcamui.c `ex_grouping` uses its own winding-number test and its own emission order). Groups (`FUN_18003a650`, depth counter
  starting at 0): every top-level non-island boundary starts a group; the group takes its direct children (nesting
  depth 1 = the holes); a boundary at EVEN depth (an island, depth 2, 4, ...) becomes a NEW top-level group, its own
  depth+1 children join it, and so on. So the sheets of one plane union to exactly the even-odd region of all its
  loops (Cut / Boss remove / add the same volume as one even-odd group), but Create makes one body per disjoint loop
  AND per island-in-a-hole, and sec. 6's one-op-per-group follows these groups. Failure -> sld 337 "No valid closed
  chains selected for non-thinwall extrude." (title 201).
- Thin: `ps_sort_chains_for_wire`, `ps_chains_to_wire_bodies`, swept into a wall sheet, thickened (4.6). How the wire
  path groups the chains is **NOT DECODED**. Open chains are only usable this way, and Thin Wall is switched on for
  them automatically (1.1).

### 4.6 Thin wall (`ps_sweep_wire`)
The chain wire is swept, then `ps_thicken_sheet(sheet, front, back)`: Direction 1 thickness (P+0x58) is used when
dir != 1, Direction 2 (P+0x60) when dir != 0; front/back is chosen by the sheet normal against a per-chain side vector
from `FUN_180097650` (for closed chains `FUN_1800db3c0` tests a point offset by 100 x tol with `pt_in_ctour`). Which
geometric side "Direction 1" is for an open chain: **NOT DECODED**. Draft ends (P+0x68) drafts the end faces of open
chains (tooltip DraftEndsCheckBox_ToolTip; code passes it to ps_sweep_wire). Error codes as 4.2/4.3.

### 4.7 Bodies and booleans
- Path choice (sld_extrude): Create body, or new op without "Create a single operation", or Trim to faces ->
  `FUN_1800d38f0` (one result per group); otherwise (Cut/Boss with single op, or edit) -> `FUN_1800d2c80` (all groups'
  tools in one boolean).
- Create body: **each group becomes its own new body** in a new empty partition (`FUN_1800414f0`
  `PK_PARTITION_create_empty`, `FUN_180050b70` set current), `sld_solid_from_tag`, **one SldExtrudeOp per body**.
- Cut: `ps_boolean_cut(target, tools)`; Add boss: `ps_boolean_union(target(s), tools)`. With Trim to faces the tool is
  first trimmed to the faces (`FUN_180057460`; failure sld 558 "Failure trimming tool to selected faces." exists in
  the table, its call site was not traced). Boolean producing no body -> `message_box_ok(559, 553)`: sld 559 "Failure
  generating boolean operation.", caption sld 553 "Boolean remove operation failed." (FUN_1800d38f0).
- Target when several bodies exist: chosen by the Target button (sld 857 "Select target body to cut or add boss to.",
  `sld_pick_one_ent`; with exactly one visible solid it is taken without asking, `FUN_180157410`), by face picking, or
  by the automatic search (1.1 step 6). Choosing a target while the type is Create switches it to Cut and the name to
  "Extrude Cut" (`FUN_1800ddf50`).

### 4.8 Automatically determine operation type (`FUN_1800d96f0`)
Uses the target (or all visible bodies), `FUN_1800da960` picks a body; per chain interval pairs: one with start < -tol
and end > tol -> Cut (1); otherwise `FUN_1800d8ce0` classifies -> Cut or Boss (2); no body -> Create (0). Then the
default name for the type and the panel are refreshed. Exact classification **NOT DECODED**.

### 4.9 Round edges (2026-10-21, the AutoCursor's circle edges on bodies made here)
Mastercam's Extrude builds Parasolid curves from the chain (ps_chain_to_ps_curves -> ps_curve_to_ps_curve ->
ps_arc_to_ps_arc, mcgeomsld 0x18003e060, decomp/mcgeomsld_arcs.c): an arc entity becomes **PK_CIRCLE_create
with centre = the arc centre, axis = the view's Z row (negated for a negative sweep), ref_direction = the view's X row
projected normal to the axis**, trimmed (CRTRCU) to [a0, a0 + sw] (a negative sweep mirrored: 2 pi - a0 ...). For a
Top-plane chain: axis +Z (-Z for a clockwise arc), parameter 0 along +X. The swept body keeps circle edges (class
0xbba): in the user's part solids (BRACKET4, NAKTO-BRAKE-BRACKET, formats/xt.md "Edges") the circles have normal +Z
and x_axis +X, except two in BRACKET4 (the bottoms of two holes at z -13: normal -Z, x_axis -X - how Parasolid
re-parametrises curves in sweeps / booleans is not visible here); full circles are ring edges (no vertex), trimmed
arcs have two vertices. This kernel uses ps_arc_to_ps_arc's parametrisation for every cap edge of a chain arc.

This kernel facets arcs (cam_chain_polyline, chord tolerance `tol` = 0.002 + ext * 2e-6 in feat_groups (ours), step <=
min(2 acos(1 - tol / r), pi / 18)). The true arcs are recovered (`solid25.c`, `BArc` / `seg_carrier` / `arc_edge`):
- `feat_groups` puts every arc entity of the group's chains on the Profile as a carrier (centre, radius,
  counter-clockwise start / sweep, the chord tolerance); `group_tool` copies them to the tool (thin wall: also r +- each
  thickness, tolerance + t (1 / cos(pi / 36) - 1) for the sharp-join offset of the facets; drafted tools: none - the
  slabs approximate a taper); `body_bool` (Cut / Boss / union of groups) keeps both operands' carriers - a trimmed arc
  keeps its circle with a shorter sweep.
- `body_brep_edges`: a horizontal cap-outline segment lies on a carrier when both ends are within tol + 2e-5 (the
  Clipper 1e5 grid, offset.cpp) of the circle, it is no deeper inside than that (a chord's sagitta <= tol), it spans at
  most pi / 18 and both ends are inside the arc's sweep (nearest carrier wins). A vertex where a carrier ends is moved
  onto the true intersection with the neighbouring straight segment's line or the other circle, when within one facet
  step (r pi / 18) of it (Parasolid's vertex lies on both curves) - the straight and vertical edges there move with it.
  A run of one carrier between two vertices is an arc edge (`BEdge.arc` 1), a closed loop of one carrier a ring edge
  (2, no vertex, as Parasolid's - formats/xt.md); two carriers / a carrier and a straight segment meeting = a vertex.
- Interval: t from +X about the axis (+Z, or -Z for a clockwise entity: ps_arc_to_ps_arc); t0 = the start angle in that sense restricted to
  [0, 2 pi) - the same restriction FUN_180043850 (mcgeomsld, the PK edge -> mc arc conversion: atan2 of the end points,
  restrict_angle(0, 2 pi), decomp/mcgeomsld_arcs.c) applies to an edge's start angle - and t1 = t0 + the arc's angle; a ring edge 0 .. 2 pi.
  **Note:** the interval PK_EDGE_ask_geometry itself returns (its normalisation for arcs crossing +X, the start of a
  ring edge) is Parasolid internals, not in any dump - the rule above is the one Mastercam's own conversion of the same
  data uses. It decides the Quadrants (t = 0, pi/2, pi, 3pi/2 inside [t0, t1]: an arc crossing +X offers no quadrant
  on +X) and a ring edge's Midpoint (t = pi, the -X point).
- NOT DECODED / left out: the ends PK_EDGE_ask_geometry gives for a ring edge (no Endpoint on full circles); circle
  edges of drafted bodies (faceted runs, end vertices only); vertical (seam) edges of a cylinder wall stay as the
  sharp-edge rule gives them (none on a smooth wall: EDGE_DEG 20 deg, body_edges' rule); tangent line / arc joints get a
  vertex but no vertical edge (smooth).

## 5. Messages
| id (sld) | text | where / condition |
|---|---|---|
| 470 | Select chain(s) to extrude | chain_it prompt, `FUN_1800dc600` |
| 851 | Use dialog to select chain(s) and to modify extrude settings | panel prompt, `FUN_1800dba00` |
| 505 | Select new position or Esc to cancel | arrow drag start, `FUN_1800ddc70` |
| 459 | Select point to extrude to (in construction view). | AutoCursor, `FUN_1800de680` |
| 443 | Select faces to extrude to. Press Enter when done. | `FUN_1800df990` |
| 857 | Select target body to cut or add boss to. | `FUN_180157410` |
| 905 / 485 | Select line, edge or two points to specify extrusion direction. / Set extrusion direction. | `sld_extrude_prompt_for_dir` |
| 657 + caption 201 | Operation requires planar chain set. / Error Processing Solid | non-planar chain (`FUN_1800dc600`, `sld_extrude`) |
| 1101 + 201 | Below minimum distance. | abs(Distance) < app+0x8 on preview / OK (0x1800d892f, 0x1800d8a42) |
| 337 + 201 | No valid closed chains selected for non-thinwall extrude. | grouping failed (`sld_extrude`) |
| 680 | Chains must be closed for extrusion into a solid body. | open chain without thin wall (`sld_extrude`) |
| 315 + 201 | Invalid extrude distance. | distance <= tol in a sweep |
| 557 / 667 + 201 | Failure extruding sheet body. / Self-intersecting body produced by extruding sheet body. | `SWEENT` failure / code 0x345b (`ps_sweep_sheet`) |
| 555 / 556 + 201 | Failure drafting faces. / Failure allocating memory. | draft |
| 559 + caption 553 | Failure generating boolean operation. / Boolean remove operation failed. | boolean gave no body (`FUN_1800d38f0`) |
| 644 + caption 745 | Base chain cannot be deleted / Delete Chain | edit mode, removing the base chain or rechain to nothing |
| 2001, 2015-2020 | panel validation (sec. 2) | `FUN_18020fbd0` |

## 6. Operation tree
- One `SldExtrudeOp` per created body (Create) or per group (Cut/Boss without single op), one in total with single op.
- Its name = the panel Name verbatim (`FUN_180265610`); defaults sld 707 "Extrude", 706 "Extrude Cut", 705 "Extrude
  Boss" (`FUN_1800db220`; a name equal to one of those three follows the type). No counter is added in this code, so
  repeated extrudes are all called "Extrude". The op stores P (`FUN_18027c8b0`), the chains (+0x1c8), the chain data
  (+0x210), the faces, the target (+0x18), the partition (`FUN_1802654e0`). How the Solids manager renders the entry
  (icons, body rows) was decoded later from mastercam.dll's history tree (HistDlg_addSolid / HistDlg_addOp,
  decomp/mastercam_histdlg.c): see formats/mcamui_dialogs.md "Solids manager". mcgeomsldres 847-849 "Create Body / Cut Body / Boss Body" are
  not used by this panel (it uses the BAML labels).

## 7. The kernel on OpenCASCADE (2026-11-18, user decision SOL-3; viewer/occt_solid.cpp, core_ui cui_feat.c)

`feat_apply` builds the kernel input from the feature: the chain groups of `ex_grouping` (4.5, its own containment test stays ours), each
chain's lines / arcs **exact** (occt_solid_ent.h: ps_arc_to_ps_arc's circle - axis = the view's Z, negated for a negative sweep, parameter 0
along the view's X, the entity's interval), the group direction (4.1) and P, then `ocs_extrude` (sld_extrude): profile face (root loop +
holes), BRepPrimAPI_MakePrism along the direction x Distance (Through all: 4.2 from the target's box projected on the direction from
every chain point, + 1000e-5 metric / 39.37007874015748e-5 inch), Create = one body per group, Cut / Boss = BRepAlgoAPI_Cut / Fuse of the
target with the tools - all groups in one boolean with "Create a single operation" (FUN_1800d2c80), else one op per group in group order
(FUN_1800d38f0). Both directions = the second half swept d2 the other way and fused (4.3). Every op records Mastercam's face / edge names
(formats/solid_history.md 3.2 / 3.2.1) from the prism's and the boolean's history (Generated / Modified / IsDeleted); OCCT's seams and
degenerated edges get no name (Parasolid has none). The bodies are displayed, picked, snapped and chained through their XT (the writer
runs at every rebuild): `body_mesh` / `body_edges` / `body_hit` / `body_volume` on the OCCT shape, `body_brep_edges` / `body_brep_topo` =
xt_mesh.c's `xt_brep_edges` / `xt_brep_topo` of that XT, the same code as the part's own solids. The target search (1.1 step 6) clashes
the tools with each body by BRepAlgoAPI_Common (shared volume, solid25.c's reading of PsClashTest). Bounding Box solids: BRepPrimAPI box /
cylinder. Built without OpenCASCADE the Extrude makes no body (the log says "built without OpenCASCADE"); solid25.c's slab kernel is no longer used.

Kept from solid25.c because Mastercam's rule is NOT DECODED (ours, unchanged behaviour): the draft's taper sense (a positive angle grows the
section away from the chain plane; Reverse / Split per 4.3; BRepOffsetAPI_DraftAngle on each swept half's side faces, neutral plane = the
chain plane, OCCT's sign `OCS_DRAFT_SIGN` measured by tools/tests/sld_kernel_test.sh), the thin wall's sharp joins (BRepOffsetAPI_MakeOffset
GeomAbs_Intersection for closed chains; open chains: each line / arc offset exactly, joints at the offset curves' intersection, square
ends; Direction 1 on the side away from d x t as feat_groups orders the path), the chain-gap joints (formats/occt_xt.md 5), no face merging
after booleans (Parasolid's merge rule NOT DECODED: e.g. a boss flush with the block keeps its own side faces and the edge between them;
user decision SOL-5a). Parasolid's boolean tolerance 1e-5 m (SOL-5c, FUN_18003e630) as OCCT's fuzzy value is implemented but off by default
(MCAM_OCS_FUZZY=1): it merges Through all tools with the target (user_decisions SOL-5c, question SOL-6).
Refused (new): the draft of an open chain's thin wall without Draft ends (BRepOffsetAPI_DraftAngle would draft the end faces).

Changes against solid25.c in the regression (tools/tests/solid_compare.sh, `tools/tests/solid_kernel_diff.py` classifies them): the body
lines print the face count instead of the slab count; volumes are exact - arcs were facets (cylinder r 10 x 50: 15702.8 -> 15707.96 =
pi r^2 h; holes / cuts -1e-5 .. -4e-5 relative), drafts were 24 slabs per half (ex_both_draft 3.6e-7 relative); ss_boss_merged: the boss
flush with the block now has its own side faces, so the AutoCursor finds the edge at the block's top (z 50) and resolves the coincident
candidates on the axis line in the XT's edge order (kinds 3 / 13 swapped at the same points); ss_lk_click_vtx_top: of the two vertices that
coincide in the Top view the top one (z 50) is taken now (edge order). ex_save / ex_save_edit differ before this work (PROJECT-FORMAT line).

## Differences from viewer/mcamui.c

Status re-checked against the code on 2026-10-16 (viewer/mcamui.c "solids: Extrude" section `ex_*` / `feat_*`,
viewer/solid25.c). Each item: the original difference, then **implemented**, **partly** or **not done** with what
the code does now. (formats/overnight_plan.md 22:56 counted 25 done / 6 partly / 2 not done; the per-item reading
below gives 27 / 5 / 0 because 13 and 29 are counted partly here; 20 and 32 were implemented 2026-10-19.)

1. Panel structure (was: one window with a "Chains: N" line). **Implemented**: `draw_extrude_panel` has tabs Basic /
   Advanced (`tw_tabs`) with the expanders Operation, Chains, Distance, Trim to Faces | Draft, Thin Wall, Plane
   Orientation, Preview (`ex_page_basic` / `ex_page_advanced`); the chains line is gone. Initial open state of Plane
   Orientation / Preview NOT DECODED (open).
   2026-10-21: the panel docks in the left function-panel pane (CreateInFunctionPnl, like the wireframe panels;
   draw_manager yields the dock) - it used to float at the right with its Cancel under the Chaining dialog's OK, so a
   second click there cancelled the command. stderr `extrude:` lines trace the command (`ex_log`).
2. Name field. **Implemented**: Name edit (`ex_name`), default names sld 707 / 706 / 705 following the type while the
   name is a default (`ex_set_type`, FUN_1800db220 / FUN_180132190), validation sld 2001 (`ex_validate`).
3. Target body. **Partly**: read-only Target text with the body name or "(None)" (sld 858), "Select Target Body"
   button enabled with > 1 visible solid (prompt sld 857, `ex_pick_body` ray pick), auto-target when exactly one
   solid exists (`ex_chains_done`, FUN_180157090). Not done: the target search `FUN_1800d8d70` (and its direction
   flips) and target by face picking - NOT DECODED / no faces here.
4. Cut / Boss enabling. **Implemented**: enabled only with a target (EnableCutBoss), Create forced without one
   (`ex_force` / `ex_setparams`), choosing a target while Create switches to Cut and "Extrude Cut" (`ex_set_target`).
5. "Create a single operation" / "Automatically determine operation type". **Partly**: both check boxes exist with
   their session globals (DAT_1803bab19 = 1, DAT_1803e89a0 = 0); single op off splits the features (`ex_accept`). The
   automatic classification (sec. 4.8 `FUN_1800d96f0`) is not done.
6. Chains list. **Partly**: list "Chain  N" with Ctrl multi-select, context menu Remove / Reverse / Add / Rechain all,
   buttons Reverse All / Add Chains / Rechain All, Delete key removes the selected chains (main loop). Not done: the
   highlight of a focused / selected chain in the graphics.
7. Reverse all. **Implemented**: a button toggling P+0x39 (`ex_p.reverse`).
8. Per-chain directions. **Implemented**: `ex_cdir[]` per chain (SLD_EXT_CHAIN_DATA), chain normal oriented to +Z
   with the chain reversed when its normal points away (`ex_chains_done`, `ex_chain_normal`), reversed by clicking an
   entity of the chain (`ex_click`, event 2 FUN_1800dde10) or the list's Reverse. The winding rule is the decoded
   one (`FUN_18000bf30`, b7.c): the normal flips only when the signed turning-angle sum is -2 pi within tol; open
   chains keep the plane normal (the reference +Z here).
9. Default direction rule (was: Cut into / Boss away from the target, invented). **Implemented**: the group's first
   chain's direction x Reverse all (solid25.c `group_tool`, sec. 4.1); the into / away rule is only used once to convert
   project lines written before this spec (`feat_apply` `p.legacy`). The target-search flips of `FUN_1800d8d70` remain
   NOT DECODED (item 3).
10. Through all. **Implemented**: Cut only (radio enabled for Cut, clears Trim to faces), distances from the target's
    Z range extended by 1000e-5 metric / 39.37007874015748e-5 inch, <= tol -> 1.0 (solid25.c `group_tool`).
11. AutoCursor. **Implemented** (`ex_autocursor`, FUN_1800de680): Distance method = Distance, per-chain signed
    projection, negative flips that chain, the first chain's |value| = Distance; 2026-10-21 (Qt phase 5): the threshold is
    app+0x8 = tol * 10 (SolidsAppBase ctor FUN_180164c60, decomp/extrude/b5.c:146), core_ui cui_extrude.c.
12. Trim to Faces group. **Partly**: check-box header (enabled when type != Create), empty face list, Add Selection /
    Reselect All greyed, "Calculate minimum distance to face" (DAT_1803bab70), validation sld 2017. The bodies here
    have no pickable faces, so trimming is not done; Trim on forces Distance (`ex_setparams`) and one op per group.
13. Draft Reverse (was: invented "Outward"). **Partly**: "Reverse" = P+0x44 DraftOutward, default off, negates the
    angle; the second half of Both uses Split ? P+0x44 : !P+0x44 (solid25.c `group_tool`). Which way a positive angle
    tapers is NOT DECODED (Reverse off keeps the earlier geometry: the section grows away from the chain plane).
14. Split / Draft ends. **Implemented**: Split enabled with Draft + Both directions, Draft ends with Draft + Thin wall;
    Draft ends drafts the end faces of open-chain walls (solid25.c `group_section`).
15. Thin wall direction. **Implemented**: radios Direction 1 / Direction 2 / Both, default Direction 1, the two values
    enabled per direction (EnableDirection1 / 2).
16. Open chains. **Implemented**: open chains switch Thin Wall on at the first selection (app+0x2e78, not stored
    when untouched, `ex_chains_done` / `ex_end`); a non-thin open group reports sld 680 (solid25.c `group_tool`).
17. Open-chain wall sides. **Implemented**: Direction 1 on one side and Direction 2 on the other (solid25.c
    `open_band(path, t1, t2)`); the side is chosen as ps_sweep_wire + FUN_180097650 decode (mcamui.c `feat_groups`,
    decomp/mcgeomsld_draft_sweep.c: d x t of the first entity's own tangent).
18. Plane Orientation. **Partly**: vector text (FUN_180132ab0 format, typed vector FUN_180131fb0), Set Normal to
    chain, Set C-Plane +, Select Vector (sld 905 / 485) are present; chains may lie in several XY planes (grouped per
    Z and per nesting island, `feat_groups` / `ex_grouping`). Kernel limit: only +Z / -Z vectors are accepted (others marked in error, `ex_set_vector`) and a
    chain not parallel to XY gets sld 657 + 201 (Mastercam accepts any planar chain). Separate sheet groups within one
    plane: **implemented** per the FUN_180039370 / FUN_18003a650 decode (sec. 4.5: disjoint loops and even-depth
    islands are separate groups, holes join their root; `ex_grouping`) for non-thin - the containment test and the
    group order are ours (sort_pock_bounds' algorithm NOT DECODED); the thin path's wire grouping
    (ps_sort_chains_for_wire) stays NOT DECODED, so a thin plane is one group.
19. Preview. **Implemented**: "Automatically preview results" (DAT_1803bac10, stored also on Cancel) and a Preview
    button enabled only when it is off; the preview is the real extrude result (`ex_preview`). Preview colour and
    removal NOT DECODED (drawn like the bodies). The preview is drawn shaded also in Wireframe display
    (`scene_force_mesh`, scene.c): whether the original shades the preview in wireframe mode is NOT DECODED - ours,
    without it the command has no preview at all in that mode.
20. Distance / thickness drag arrows on the graphics (sec. 3). **Implemented**: a distance arrow per chain (anchor =
    chain start, along the chain direction x Reverse all, offset = Distance) and, with Thin Wall on, Direction 1 (red)
    / Direction 2 (green) arrows on the last chain at its first entity's mid point, along -/+ d x t (closed chains:
    side negated); dragging sets the live value (distance: abs, a negative offset flips all chain directions;
    thickness arrows switch the direction to Both), Esc restores the saved values, OK / Apply / Cancel and the tabs
    are disabled during the drag (mcamui.c `ex_arrow_list` / `ex_drag_*`). Ours: the shaft is world-length with a
    small head (the original's shaft is a screen-constant 0.1 x viewport height), the pick aperture, and the distance
    arrow's colour (the GrArrow default is NOT DECODED; Direction 1 / 2 colours are the decoded UtColor red / green).
21. Buttons. **Implemented**: OK (0x2bd), Apply = OK-New (0x2c0, restarts the command), Cancel (0x2be), Help greyed
    (topic NOT DECODED); Enter = Apply (main loop). OK-Regen belongs to editing (item 32).
22. Persistence. **Implemented** (`ex_end`): P stored only on OK / Apply, target cleared, an automatic Thin Wall stored
    off, Draft kept in P+0x40 for the session; the vector = the first chain's direction.
23. Error texts. **Implemented**: the sld texts of sec. 5 (`ex_sld`: 201, 315, 337, 553, 555, 556, 557, 559, 657,
    667, 680, 1101) and the validation texts 2001 / 2015-2020; the invented texts are gone. A feature whose chain
    geometry was deleted is skipped silently (regeneration NOT DECODED, sec. 1.7).
24. Error display. **Implemented**: validation marks the field (red border / label, text as tooltip), run-time
    failures are message boxes (`ex_report` -> `message`).
25. Prompt. **Implemented**: sld 851 while the panel is up (`ex_prompt_panel`), chain prompt sld 470; 2026-10-21: empty
    in edit mode (1.3).
33. Ribbon (2026-10-21, Qt phase 5): only FT sld_newExtrude starts the command; Draft Extrude (sld_newDraftFace_3) and
    Surfaces > Extrude (ExtrudeSurface) share the "Extrude" tooltip and had started it too - now greyed.
The command's code moved to viewer/core_ui/cui_extrude.c (formats/qt_port_plan.md phase 5); "mcamui.c ex_*" above = that file.
26. Create body with several groups. **Implemented**: one body and one feature per group (`ex_accept`, groups = chains
    in one Z plane within nc_tol).
27. Cut / Boss with "Create a single operation" off (or Trim to faces on). **Implemented**: one feature per group
    (`ex_accept`); with single op on, one feature for all chains.
28. Naming. **Implemented**: the feature (operation) has the panel Name; a new body takes that name (solid25.c
    `solid_extrude`); the invented "Extrude %d" counter and Solids list line are gone (Solids manager:
    formats/mcamui_dialogs.md). How Mastercam names bodies (FUN_18012fa00) is NOT DECODED.
29. Distance check. **Partly**: validation < tol (sld 2016) whatever the method, ps_sweep_sheet sld 315 in the kernel.
    2026-10-21 (Qt phase 5): the "Below minimum distance." box (sld 1101 / 201) on preview and on OK / Apply of a new op
    (no feature made), app+0x8 = tol * 10 (FUN_180164c60) - **implemented** (core_ui cui_extrude.c ex_accept).
30. Draft angle range. **Implemented**: validation 0..89 with the sld 2018 text ("0 and 90").
31. Thin wall defaults. **Implemented**: thick = thick2 = 1 mm / 0.1 in (inch values set at the first start in an inch
    part); the old thick2 = thick fallback is gone (Direction 2 < tol is a sld 2020 validation error).
32. Editing an existing extrude (Modify params, OK-Regen, base-chain protection sld 644). **Implemented**: the Solids
    manager's Edit Parameters loads the op into the command (`ex_edit`): Create body / "Create a single operation" /
    AutoOpType / the target button greyed, Cut / Boss enabled iff the type != Create, Calculate minimum distance reads
    false, Regen button instead of Apply (its label text is the shared FunctionPanel resource, NOT DECODED - "Regen"),
    removing the base chain or rechaining to nothing pops sld 644 / 745, OK with unchanged parameters does not rebuild
    (`ex_unchanged`, FUN_180130490), a change replaces the feature with undo (U_FEAT idx 2) and replays the history;
    the edit preview replays the history with the op replaced (the original's edit regen pipeline is NOT DECODED, so
    this is ours), and a failing replay keeps the edit open with the error box (also ours).

### Code: viewer/solid25.c / solid25.h (the extrude kernel)
Own slab kernel (formats/improvisation_review.md item 20: "slab kernel ... a real solid kernel" needed). Data:
`Body` = stack of Z `Slab`s, each a 2D region `CPaths` (outer CCW, holes CW); `ExtrudeParams` = SLD_EXTRUDE_PARAMS
(sec. 0: P+0x00 type, +0x08 target (here a body index), +0x10 method, +0x18 dist, +0x20 vec, +0x38 trim, +0x39
reverse, +0x3a both, +0x3b split, +0x40 draft, +0x44 draft_rev, +0x48 angle (degrees here), +0x50 thin, +0x54
thin_dir, +0x58 / +0x60 thicknesses, +0x68 draft_ends; `legacy` is ours); `Profile` = one chain group (closed loops,
open polylines, z, dir); `ExErr` = mcgeomsldres message id + caption (sec. 5).

| function | behaviour | source |
|---|---|---|
| `solid_extrude` | Create: one new body per group named by the op; Cut / Boss: union of the group tools, then difference / union with the target; empty result -> 559 / 553 | sec. 4.7 (ps_boolean_cut / union) |
| `group_tool` (static) | non-thin with open chains -> 680; empty section -> 337 (thin: 557); direction = group dir x Reverse all; Through all from the target's Z range + margin; d <= tol -> 315; draft halves (Split rule) | sec. 4.1-4.5 |
| `group_section` (static) | closed loops even-odd; thin: band between +Direction 2 / -Direction 1 offsets (Direction 1 inward); open chains `open_band`; Draft ends offsets the open walls too | sec. 4.6, WPF ThinWallInward / OutwardDistance |
| `open_band` (static) | band of an open polyline, a left / b right, square ends, mitre limit 4x (ours) | sec. 4.6 |
| `body_bool` (static) | union / difference per Z interval (Clipper `cl_bool`) | ours |
| `body_mesh`, `cap`, `walls`, `bridge`, `earclip` | triangles for display: walls per slab, caps only where not covered by the next slab, holes bridged, ear clipping | ours |
| `body_hit`, `reg_inside` | ray vs caps / walls (target picking) | ours |
| `body_volume`, `body_copy`, `body_free` | helpers (copy / free the arc carriers too) | ours |
| `body_brep_edges`, `seg_carrier`, `arc_edge`, `arcs_add` | the AutoCursor's edges: cap outlines + sharp vertical edges merged through in-line vertices; facets on a `BArc` carrier -> arc / ring edges with the true circle and interval, trimmed-arc vertices moved onto the true intersection | sec. 4.9; formats/ui.md "AutoCursor on solids" |
| `body_edges` | the edges drawn on the body: mesh sides used once or with normals > 20 deg apart (ours - no B-rep faces); drawn 1 px (no set_gl_width in the path), lighting off, depth-tested: wireframe = the solid colour (config 5052, ent +6), shaded = the _render_cfg 4 outline pass in RENDER_CFG +0x12 = config 8027 "Outlined edge color?" (default 0 black; mcgeomsld export render_cfg 0x1803b9c50, its mode field defaults to 4; read at 0x1800b3ea8), translucent = palette 0xc9 (0x1800b3e57); faces polygon-offset (1, 1) | sld_draw_solids 0x1800b4aa0 (an edge pass with _render_cfg 1 before the shaded pass), sld_draw_solid_wire FUN_1800b46b0 (ps_gen_disp_list edges, glDisable(GL_LIGHTING), colour = find_color(ent colour) or the select colour), FUN_1800a61f0 (_render_cfg < 3: set_gl_color(colour)), FUN_1800b4010 (_render_cfg 4: glPolygonOffset(1, 1)); decomp/extrude/sld_draw.c |

Superseded 2026-11-18 by section 7 (OpenCASCADE). Ours / approximations (improvisation_review item 20): draft by 24 slabs per drafted half (`EX_NSLAB`); taper sense of
a positive angle NOT DECODED; only Z-direction extrusion (non-XY planes refused with sld 657); bodies made here only
(Parasolid solids from the part cannot be cut / bossed: `ex_nvis` counts this program's bodies). The features are
replayed from the project's EXTRUDE / XCHAIN lines (`feats_write` / `feats_read`, `solids_rebuild`); undo records
`U_FEAT` (`feat_undo`).
