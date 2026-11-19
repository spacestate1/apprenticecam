# Solids > Modify > Constant Fillet (Mastercam 2017) - decode and spec (phase 1: no port yet)

Decoded 2026-11-18 from `mcgeomsld.dll` (Ghidra project `~/mastercam-re/ghidra_sld`, name `sld`, headless `-readOnly`
under `flock ~/mastercam-re/ghidra.lock`), the WPF panel in `mcgeomsld.ui.dll` (BAML
`solids/fillet/solidsfilletmanagerview.baml`, tooltips `MCGeomSld.UI.Solids.Fillet.SolidsFilletManagerViewToolTips.resx`),
the C++/CLI view model in `mcgeomsld.interop.dll` (`MCGeomSld.Interop.SolidsFilletManagerViewModel`, ilspycmd), the
string table of `mcgeomsldres.dll` (`decomp/extrude/sldres_str.tsv`, "sld N" = string N), the dialog resources of
`mcgeomsldres.dll` (tools/pe_dialogs.py) and the option-structure printers of Parasolid's `pskernel.dll` (the
journal / debug printers that print every option field by name). Port: phase 2, after the solid save-path branch
(SOL-1..SOL-4, formats/user_decisions.md).

Rule 1 applies: every statement cites its source. **NOT DECODED** = not found; **[inferred]** = a reading that is not
proven. No Parasolid headers exist here, so PK token *values* (0x47ea ...) have no names; the option *field* names
are decoded from pskernel.dll (section 5).

Work files (not in the repo, like formats/occt_3d_plan.md's): `~/mastercam-re/work/occt/fillet/`:
`d1.c` .. `d7.c`, `d3b.c`, `d6b.c` (DecompAt / DumpDecomp), `dis1.txt` (FUN_180036580 0x180036c73..e60), `dis2.txt`
(the panel loop 0x1801bc5c0..8c0, the blocks Ghidra's decompiler dropped), `dis3.txt`, `vt*.txt` (VtableDump),
`syms.txt`, `strrefs*.txt`, `refs1.txt`, `mem1.txt` / `mem2.txt` (MemDump), `callers.txt`,
`SolidsFilletManagerViewModel.cs` (ilspycmd), `psk_*.s` (pskernel printers, annotated by `psk_dis.py`; string xrefs by
`psk_xref.py`), `corpus_fillet_ops.txt` (`tools/sld_fillet.py -v` output), `gh.sh` (the headless wrapper).
Earlier dumps reused: `~/mastercam-re/work/occt/plan/sld_core.c` (ps_fillet, sld_fillet, sld_new_const_fillet,
sld_do_fillet), `sld_apps.c` (FUN_1801bc100 / FUN_1801bc2d0), `sld_help.c` (FUN_180036310, FUN_180036580,
FUN_18004b9b0, FUN_18004f490, FUN_180050bd0); `decomp/solid_history/h8_mceptr_entries.c` (FUN_1802656b0, FUN_180265240).
Shared machinery documented elsewhere and only referenced here: the SolidsAppBase / function-panel loop and its
messages (formats/extrude.md 1.3 / 1.4 / 1.6), McOpsFile / McOperation / McEptr records (formats/solid_history.md 2, 3).

## 0. Classes and the parameter block

| object | where | role |
|---|---|---|
| `sld_do_fillet` 0x1800e3670 | FT `FUNCTION CPP "sld_do_fillet"` (mastercam.ft l.5292, FNAME "Fillet", TIP 32979); ribbon Solids > Modify 53594 / split part 53595 "Constant Fillet" (formats/ui_ribbon.json) | `sld_solids_ok(1)`, app on the stack, `FUN_180165170` (run), dtor |
| `SolidsConstantFilletApp` | vtable 0x1802f8fd0; ctor `FUN_1801bc100`, dtor `FUN_1801bc2d0` (sld_apps.c l.273-360) | the command |
| `SolidsFilletManagerDialog` | ctor `FUN_180128900` (d2.c), vtables 0x1802ded88 / 0x1802df148 | function-panel host, MFC resource 0x587 |
| `SolidsFilletManager` | vtable 0x1802de8c8 (d1.c l.348-1007) | observable model the WPF view binds to |
| `SolidsFilletManagerValidator` | vtable 0x180302228; `FUN_180216e00` | panel validation |
| `SolidsFilletManagerViewModel` | mcgeomsld.interop.dll | WPF property -> model vtable offset |
| `SldFilletOp` | ctor `FUN_1802805d0` (0x128 bytes), vtable 0x18030b510 | the history op (type 9), shared with Variable Fillet |
| `SolidConstantFilletWithUI` 0x1800e1780 | export | the same app for editing an existing op (called by SldFilletOp slot 27 `FUN_180280ab0`) |
| `sld_new_const_fillet` 0x1800e2a10 | export | programmatic entry: `sld_fillet(bodies, faces, edges, P, name, NULL, 0x1c, false)` |

App vtable (0x1802f8fd0): [0] `FUN_1801bc470` dtor, [1] `FUN_180165120` load edit op (base), [2] `FUN_180165290` set
event masks (app+0x220 / +0x224), [3] `FUN_1801bc8c0` start, [4] `FUN_1801bc5c0` panel loop, [5] `FUN_1801bc4a0`
end, [6] `FUN_1801bcc90` panel -> P (dialog `FUN_180129160`), [7] `FUN_1801bd850` P -> panel (dialog `FUN_180129660`),
[8] `FUN_1801bc930` per-event tail (`sld_dirty` of every body of the selection, `FUN_180165040`).

App fields: P at app+0x228 (0x18 bytes); name char[0x40] at app+0x61 (base ctor `FUN_180164c60`); selection arrays
`CArray<int>` of Parasolid tags: bodies app+0x240 (data +0x248, count +0x250), faces app+0x268 (+0x270, +0x278), edges
app+0x290 (+0x298, +0x2a0) (`FUN_1801bc930`, `FUN_1801bd610`); dialog app+0x2b8; edit flag app+0x20; edited op app+0x128.

### SLD_FILLET_PARAMS ("P+x", 0x18 bytes; app+0x228, op+0xf0)

| P+ | type | meaning | panel -> P (`FUN_180129160`) | P -> panel (`FUN_180129660`) | default |
|---|---|---|---|---|---|
| 0x00 | int | fillet type: 0 constant, != 0 variable radius (`FUN_180281450`: `P+0 == 0` -> `sld_fillet`, else `sld_var_fillet`; `FUN_180280ab0` the same for editing) | always written 0 | - | 0 |
| 0x04 | int | padding: never written by the panel; copied with the block | - | - | 0 in the static block; stale values in old ops (6.3) |
| 0x08 | double | Radius, part units | model get 0x58 (Radius) | model set 0x60 | inch 0.1, metric 1.0 (below) |
| 0x10 | int | Overflow: 0 Default, 1 Maintain blend, 2 Maintain edges (combo index) | model get 0x88 | set 0x90 | 0 |
| 0x14 | byte | Mitered corners | model get 0x78 | set 0x80 | 0 |
| 0x15 | byte | Propagate along tangencies | model get 0x68 | set 0x70 | 1 |
| 0x16..0x17 | | padding (copied, never written) | | | 0 |

Defaults: the static block DAT_1803b9708 (`mem1.txt`: `00000000 00000000 | 0.1 (9a99..b93f) | 00000000 00 01 0000`)
is copied to the session block DAT_1803e9660 by a CRT initializer (refs1.txt: reads of DAT_1803b9708 / writes of
DAT_1803e9660 at 0x180004d10..0x180004d26, outside a Ghidra function) and to every new SldFilletOp (`FUN_1802805d0`).
The app ctor (`FUN_1801bc100`) calls `RegisterVariable(&DAT_1803e9668, 1.0, 0.1)` on the radius (argument order
metric, inch: formats/extrude.md 2.4) and copies the 0x18-byte session block into P. **Verified by data**: the only
fillet op made by Mastercam 2017 in the corpus (op version 0x1c, "TUTORIAL #7.mcam" sld 185 op 1, an inch part) has
exactly the defaults: type 0, radius 0.1, overflow 0, mitered 0, propagate 1, padding 0, name "Constant Radius Fillet".

## 1. Command flow

### 1.1 Start (slot 3 `FUN_1801bc8c0`)
1. Ctor `FUN_1801bc100` (new op, param_2 == 0): P from the session block (above), name = sld 823 "Constant Radius
   Fillet" (0x337; the name is never persisted), `set_clr_sel_bits(0,4,0,4,0x1f1fff,0,0)`, `ps_reset_all_hilights`,
   FT name "sld_do_fillet" (app+0x18, used by OK-New).
2. `FUN_1801bd610` (new op only): the selection (1.2). Esc, or ending it with nothing selected -> the command ends,
   no panel.
3. `FUN_180164f40` (BeginProcess / HistDlg_enable, as extrude.md 1.1 step 3).
4. `FUN_1801bce60`: model via `FUN_18011b5f0(ISolidsFilletManager)`, dialog `FUN_180128900` (MFC id 0x587; it
   deletes `%TEMP%fillets.tmp`, the preview message log), selection list filled (`FUN_180129600` -> `FUN_180129280`),
   slot 7 (P -> panel), `CreateInFunctionPnl(dialog, sld 823)`, Preview = DAT_1803bac10 (dialog +0x3a0
   `FUN_180129720`). Failure to create -> `slot 5` (end).
5. `FUN_180165220`: Parasolid mark at app+0x114 (`FUN_18004f970`), app+0x110 = 1; app+0x131 = 1.

### 1.2 The selection (`FUN_1801bd610`; Add / Reselect `FUN_1801bd2d0`)
- Modeless "Solid Selection" dialog (mcgeomsldres dialog 1413 = 0x585, class `SldSelectionDlg`, ctor
  `FUN_1801332b0(dlg, 1, 1, 1, &DAT_1803bac30, &DAT_1803bac31, &DAT_1803bac32, 1, mainframe)`): toggle buttons
  "Edge" (2953), "Face" (2954), "Body" (2960), "From back" (2959), "Last" (2952), "Verify Selection" (2955),
  "UnSelect All" (2961), "OK" (1), "Help" (9) (tools/pe_dialogs.py on mcgeomsldres.dll). The ctor saves the
  Selection Bar's Body / Face / Edge states and sets them from DAT_1803bac30 / 31 / 32 (bodies / faces / edges; all
  three = 1 in the image, `mem2.txt`), `SetSolidSelectionDialogActive(1)`, `SetSolidSelectionShowDone(1)`,
  `SetSolidSelectionShowLast(1)`. Who writes the three flags back: **NOT DECODED** (dialog dtor not dumped).
- Prompt sld 438 (0x1b6) "Select entities to fillet". `sld_array_pick(bodies, faces, edges, 0, prompt, prompt, &esc,
  1, 0)` (d3b.c): pick mask = 1 bodies | 4 faces | 2 edges (all three arrays are given); `sld_pick_one_ent` loop;
  a click toggles the entity in its array (`FUN_1800a92f0`); **Shift + edge** = the edge's tangent chain
  (`sld_add_tangent_edge_sel` -> `ps_edge_find_tangents`), **Shift + face** = tangent faces
  (`sld_add_tangent_face_sel`); "UnSelect All" clears the three arrays; "Last" re-selects the previous selection
  (`FUN_1800be140`; messages sld 274 "The last selection is no longer valid." / 669 "There are no previous
  selections."); a third flag calls `sld_add_all_color_sel(faces)` (which control sets it: **NOT DECODED**).
  A body hit vs a face / edge hit follows the Selection Bar mask rules already decoded in formats/surface_selection.md
  (N13-8) and formats/ui.md ("Solid selection hover highlight").
- Add Selection (model slot 28, message 0x322 lParam 0) and Reselect All (slot 29: clears the list, 0x322 lParam 1)
  re-run the same pick with the panel suspended (`AfxSuspendThisWindow`); Reselect All first empties the three
  arrays; Esc restores the arrays as they were. In edit mode the pick is `sld_array_pick_in_body(op's solid, faces,
  edges, &whole_body, ...)` (picks only on the op's own body; picking the body sets the whole-body entry).
- Remove (list context menu / Delete key, model slot 27 -> 0x323 per selected entry, lParam = tag): `FUN_1801bd110`
  removes the tag from its array (body / face / edge by `ps_is_body` / `ps_is_face` / `ps_is_edge`), unhighlights.
- List entries: "Body N", "Face N", "Edge N" (`FUN_180128f30`: `"%s %d"`, sld 686 / 687 / 688, N counts per kind).
  Selecting entries highlights them (slot 30 posts 0x321 = undo the preview, then highlight; slot 31 unhighlights).
- After any change: dialog +0x390 (`FUN_180129590`) -> 0x2bf (preview) when Automatically preview is on.

### 1.3 The panel loop (slot 4 `FUN_1801bc5c0`, disassembly dis2.txt)
- Event mask 0x200 (panel messages) plus keys (slot 2 with 0x200, `FUN_180164fb0` adds 8): **no graphics picks while
  the panel is open** - entities are added only through Add Selection / Reselect All.
- Prompt: sld 504 (0x1f8) "Use dialog to modify fillet settings" (new op), empty in edit mode (`FUN_1801bcb10`).
- `get_event` returning 1 / 2 or no event (Esc, right click) = Cancel. Enter: dialog +0x3a8 `FUN_180156ec0` (OK when
  ev+0x150 != 0, else OK-New), as extrude.md 1.3.
- Messages (0x8028): 0x2bd OK, 0x2be Cancel, 0x2bf Preview, 0x2c0 OK-New / OK-Regen (`FUN_180156e70`, the same table
  as extrude.md 1.4); 0x321 / 0x322 / 0x323 (`FUN_1801bd270`, 1.2).
- After every event: slot 6 (panel -> P); then `FUN_1801bd800(app, 0)` undoes the previous preview (if app+0x118:
  `FUN_180112220`, `PK_MARK_goto(app+0x114)` via `FUN_18004f660`). Then, if the event asks for a build:
  - **final** (0x2bd / 0x2c0), new op: `sld_fillet(bodies, faces, edges, P, name, NULL, 0x1c, false)`
    (0x1801bc7cf); result -> app+0x118.
  - final in edit mode: no build in the loop (app+0x20 set skips both calls). The OK counts as a change only when
    `FUN_1801290f0` finds the P different from the P at panel start (type, Mitered, Overflow, Propagate exactly;
    Radius within `tol`), or the selection changed (Add / Reselect / Remove set that flag, `FUN_1801bd2d0` /
    `FUN_1801bd110`), or app+0x132 is set; the rebuild is then the history replay done by the caller (1.5, 6.4).
  - **preview** (0x2bf or any other building event): `FUN_180156d90` (log messages to `%TEMP%fillets.tmp`,
    `set_msg_log(2)`), `sld_fillet(..., preview = true)` (0x1801bc81c), `FUN_180156d60`, `FUN_180156bc0` (lines of the
    log longer than 4 characters are shown with `DisplayWarning`, i.e. preview errors are warnings, not boxes); on
    failure `PK_MARK_goto(app+0x114)`.
- Preview mode (`sld_fillet` param_8): no op, no records, no undo item; the bodies are changed in place and undone by
  the next event (mark app+0x114).

### 1.4 End (slot 5 `FUN_1801bc4a0`)
If accepted (app+0x130): P -> session block DAT_1803e9660 (all 0x18 bytes). `ClosePrompt`, delete the start mark,
`set_clr_sel_bits`, `ps_reset_all_hilights`, `HistDlg_enable(0)`, Preview flag -> DAT_1803bac10 (dialog +0x398 ->
`FUN_180165260`; stored on Cancel too), destroy the panel. New op, accepted, restart flag app+0xb0 (set from the
0x2c0 OK-New message, `FUN_180165250` / `FUN_180165090`) -> `post_FT_command("sld_do_fillet")` (the command restarts
with the stored P). Then `FUN_180165050`: `update_graphics`, `HistDlg_enable(0)`, `UpdateLevelsManager`, `EndProcess`.
Cancel stores nothing (except Preview). No registry / config key is involved (all persistence is these session
globals).

### 1.5 Editing (Solids manager -> the op's slot 27 `FUN_180280ab0`)
Type 0: the op's records are resolved to tags (`FUN_180269310`; entries that no longer resolve are dropped,
`FUN_180276390`), whole-body -> the solid's body tag; `SolidConstantFilletWithUI(bodies, faces, edges, &op P, name,
&changed, op, ...)`: the same app with app+0x20 = 1 (ModifyParams = 1: OK-Regen instead of OK-New, BAML), P / name /
arrays from the op (`FUN_1801bd8d0`, `FUN_1801bd870`, `FUN_180165270`). After OK the op gets the new name
(`FUN_180265610`, `HistDlg_setOpName`), P (`FUN_1801bcd10`), the edge list (`FUN_1802757d0`), the face list
(`FUN_180275970`) and op+0x90 = (bodies > 0); the regen itself is the history replay (solid_history.md 4).

## 2. The panel (WPF `SolidsFilletManagerView`, title sld 823 "Constant Radius Fillet")

FunctionPanel buttons: OK, Cancel, Help, OK-New (shown when !ModifyParams), OK-Regen (shown when ModifyParams) (BAML).
Error state (BooleanOR of `Validation.HasError`) when NameTextBox, ChainsListBox (the selection list) or
RadiusEvaluatorBox has an error. Two tabs: Basic, Advanced. View model: `MCGeomSld.Interop.SolidsFilletManagerViewModel`
(vtable byte offsets into ISolidsFilletManager; = SolidsFilletManager slots: offset / 8).

| control (BAML) | label | binding | model field / getter / setter (d1.c) | P | default | limits / validation | persistence |
|---|---|---|---|---|---|---|---|
| Operation expander, Basic: NameTextBox | "_Name:" | Name | +0x30 std::string; get 72 / set 80 | name app+0x61 | sld 823 "Constant Radius Fillet" | blank -> sld 2001 "Name cannot be blank" (validator `FUN_180216e00`, string 0x7d1) | none (ctor resets) |
| PropagateAlongTangenciesCheckBox | "_Propagate along tangencies" | PropagateAlongTangencies | +0x58 bool; 104 / 112 ("PropagateAlongTangencies") | P+0x15 | on | - | session (DAT_1803e9675) on OK |
| MiteredCornersCheckBox | "_Mitered corners" | MiteredCorners | +0x59 bool; 120 / 128 | P+0x14 | off | - | session on OK |
| Operation expander, **Advanced content**: OverflowComboBox | "_Overflow:" | SelectedIndex = Overflow | +0x5c int; 136 / 144 | P+0x10 | 0 Default | items "Default" / "Maintain blend" / "Maintain edges" | session on OK |
| Selection expander: ChainsListBox | (tooltip "Selection") | SolidEntityReferences (item text = Name, IsSelected two-way) | collection +0x60; get 152 | arrays app+0x240/268/290 | from the pre-panel pick | empty -> sld 2029 "Constant radius fillet requires at least one solid edge, face, or body" (0x7ed) | - |
| context menu / Delete key | Remove / Add / Reselect all | DeleteSolidEntityCommand / AddSelectionCommand / ReselectAllCommand | slots 27 / 28 / 29 (216 / 224 / 232) | | | the list's own "Remove" item is IsEnabled=False; the per-item menu's is live | |
| buttons under the list | (icons ImageAddSelection16 / ImageSelect16) | AddSelectionCommand / ReselectAllCommand | 224 / 232 | | | | |
| Radius expander ("Radi_us"): RadiusEvaluatorBox | | Radius (TwoWay, PropertyChanged) | +0x50 double; 88 / 96 (setter `FUN_180126710` -> `FUN_1801264b0` with `tol`) | P+0x08 | 0.1 in / 1.0 mm | `radius < tol` -> sld 2030 "Radius must be greater than 0" (`FUN_180217130`); no upper limit in the code | session on OK |
| Advanced tab, Preview expander: PreviewCheckBox | "_Automatically preview results" | Preview | +0x29; 56 / 64 | - | on (DAT_1803bac10 = 1, `mem2.txt`) | - | session, shared by all solid commands, stored at end (also on Cancel) |
| Preview button | "_Preview" | PreviewCommand, IsEnabled = !Preview (BooleanNegationConverter) | slot 21 (168) posts 0x2bf | | | | |

Other model members: ModifyParams +0x28 (get 40 / set 48; = edit mode, set by the dialog ctor), commands OK 192,
OKNew 176, OKRegen 184 (all three via the generic FunctionPanelDialog handlers, extrude.md 1.4), Cancel 200, Help 208
(`+0x380`; help topic NOT DECODED), ListGotFocus 240 / ListLostFocus 248. View-model statics s_OperationExpanded /
s_ItemsToFilletExpanded / s_OptionsExpanded = true (session) - the BAML binds no IsExpanded, so they are unused by
this view [as dumped]. The open / closed state of the Operation expander's Advanced part (where Overflow sits):
**NOT DECODED**. Access keys: underscores in the labels.

Property changes that rebuild (dialog observer `FUN_180129390`): Preview, Radius, Overflow, PropagateAlongTangencies,
MiteredCorners -> dialog +0x390 `FUN_180129590(0)` -> post 0x2bf when Preview is on. Name / IsSelected: nothing.
Selection changes (1.2) do the same.

Tooltips (resx, verbatim meaning): Propagate "Extends the fillet along all tangent edges until a non-tangent edge is
reached."; Mitered "... miter the corners in places where three or more filleted edges meet at a vertex. Mastercam
extends each fillet to the extent of the edge. Clear to create a smooth face at the vertex where the fillets meet.";
Overflow: Default "uses Maintain Blend or Maintain Edges based on which method best handles the overflow condition.
Recommended"; Maintain blend "attempts to maintain the fillet surfaces and/or the prior tangency condition between the
fillet and the overflow faces ... The overflow faces may be extended or trimmed"; Maintain edges "attempts to maintain
the edges of the faces in the overflow region ... the resulting fillet is not tangent to the overflow faces".

## 3. From the selection to the edges to fillet (`sld_fillet` 0x1800e2540, `FUN_180096570`)

`FUN_180096570(bodies, faces, edges)` builds one psBodyEdgeStruct (0xa8 bytes) per body: +0 body tag, +4 whole-body
flag, +0x08 picked bodies, +0x30 picked faces, +0x58 picked edges, +0x80 **edges to fillet**:
- a picked **body** -> all its edges (`PK_BODY_ask_edges`), whole-body flag 1;
- a picked **face** -> all edges of the face (`PK_FACE_ask_edges`), each once per body; nothing added when the body is
  already whole;
- a picked **edge** -> itself, once.
Order: body edges in PK_BODY_ask_edges order, then face edges, then picked edges, as picked (duplicates skipped).

Then **per body** (the struct list walked from the last to the first): new op only - a new SldFilletOp on the body's
ops file (`sld_get_opfile`; a body without ops file fails, `local_res8 = 0`), op id = next (`FUN_180268fd0`), the
whole-body flag -> op+0x90, else list A = picked faces, list B = picked edges (6.2); a Parasolid mark
(`FUN_18004f970`); `FUN_18004a8f0` remembers every face / edge record of the body;
`ps_fillet(op id, body, P+0x08, P+0x10, P+0x14, P+0x15, n, edges, version)`. **Each body is its own op; a failure on
one body does not stop the others** (the loop continues; the result is false).

`ps_fillet` 0x180046d30 (sld_core.c l.418): a Parasolid bulletin board (`FUN_1800524b0`) for tracking, then
- Mitered corners **off**: one `FUN_180036310(all edges)` + one `FUN_180036580` (fix) + `FUN_18004b9b0` (naming of split
  faces / edges, 6.3).
- Mitered corners **on**: **one edge at a time**: for each edge that is still a tag: `FUN_180036310(that edge)`, fix,
  `FUN_18004b9b0`, then `FUN_18004b210` maps the remaining input edges to their successors (bulletin-board events 0x3844)
  so a later edge that was split / replaced by an earlier fillet is still found; the first failure stops the rest.
  This sequential filleting *is* Mastercam's mitre (no Parasolid mitre option is set; section 5).

`FUN_180036310` (sld_help.c l.1517): **drops every smooth edge** (`PK_EDGE_is_smooth` true) from the list; with no
edge left it returns success and nothing is filleted (no message). Radius to metres: / 1000 (metric) or / 39.37007874015748
(inch). `PK_EDGE_set_blend_constant(n, edges, r_m, &opts)` -> the blended edges (fix input).

## 4. Preview and the result body
Preview draws the real `sld_fillet(..., preview = true)` body (1.3). Its colour / drawing: as Extrude's preview
(extrude.md 3, `sld_draw_solid` while previewing) [inferred: same SolidsAppBase path; not traced for fillet].

## 5. What reaches Parasolid (field names from pskernel.dll's option printers)

### 5.1 PK_EDGE_set_blend_constant options (`FUN_180036310`; printer pskernel 0x101cd940, `psk_set_blend_constant_opts.s`)

| off | field (pskernel) | Mastercam value |
|---|---|---|
| +0x00 | o_t_version | 2 |
| +0x04 | cliff_edge (EDGE) | 0 (none) |
| +0x08 | properties (PK_blend_properties_t; printer pskernel 0x108646f0, `psk_blend_props_checks.s`): | |
| +0x08 | .propagate | 0x47ea; **0x47eb when Propagate along tangencies (P+0x15) is on** |
| +0x0c | .vary | 0x47f4 |
| +0x10 | .render_rib | 0x47fe |
| +0x14 | .draw_fix (logical) | 1 |
| +0x18 | .ov_smooth | Overflow 0: 0x480a, 1: 0x4809, 2: 0x4808 |
| +0x1c | .ov_cliff | 0: 0x4813, 1: 0x4812, 2: 0x4814 |
| +0x20 | .ov_cliff_end | 0: 0x481c, 1: 0x481c, 2: 0x481d |
| +0x24 | .ov_notch | 0: 0x4827, 1: 0x4827, 2: 0x4826 |
| +0x28 | .tolerance | 1e-5 (0x3ee4f8b588e368f1) |
| +0x30 | .ribspace | 0.0 |
| +0x38 | xs_shape | 0x56b9 |

So the panel options map: **Propagate along tangencies = the blend property `propagate`**; **Overflow = the four
overflow properties ov_smooth / ov_cliff / ov_cliff_end / ov_notch** (Default and Maintain blend differ in ov_smooth and
ov_cliff only; Maintain edges changes all four); **Mitered corners is not a Parasolid option** (3, sequential fillets).
Token value names (which of 0x4808 / 0x4809 / 0x480a is "yes" / "no" / "default"): **NOT DECODED** (no headers).
Errors: `FUN_18004f490(1, err, sld 566 "Failure to attached blends to body.")`.

### 5.2 PK_BODY_fix_blends options (`FUN_180036580`; printer pskernel 0x100f9660, `psk_fix_blends_opts.s`)
Arguments: (body, &opts, &n_blends, &blends, &unders, &topols, &fault, &fault_edge, &fault_topol) (pskernel string list
"PK_BODY_fix_blends ... *n_blends, blends, unders, topols, *fault, *fault_edge, *fault_topol"; decompile order).

| off | field | value | off | field | value |
|---|---|---|---|---|---|
| +0x00 | o_t_version | 0x1e | +0x90 | set_tol | 0x576c |
| +0x04 / +0x05 | local_check / vx_twin | 0 / 0 | +0x94 | repair_su_X | 0x58b6 |
| +0x08 | checks {check_su_X, check_fa, check_fa_fa} | 0x5230, 0x523a, 0x5244 | +0x98 | output_sheet | 0x58c0 |
| +0x18 | setback_data | zeros | +0x9c | inside_tight | 0x59cf |
| +0x38 | transfer | 0x550a | +0xa0 | vx_order_data | 0x5a54, 0, 0 |
| +0x40 | propagate {propagate, tolerance, function, data} (printer 0x10864220) | 1, 0.0872664626 rad (5 deg), NULL, NULL | +0xb8 | setback_shape_data | 0x5a5a, 0, 0 |
| +0x60 / +0x70 | vx_blend_data / y_blend_data | zeros | +0xd0 / +0xd4 / +0xd8 | ov_smooth / ov_notch / ov_order | 0x4808 / 0x4827 / 0x5a53 (fixed, independent of the panel) |
| +0x80 | preserve_notch | 0 | +0xdc | repair_fa_X | 0x5b2c |
| +0x84 | update | by op version (below) | +0xe0..+0xf8 | explicit cliff edges / partition | none |
| +0x88 | tolerance | 1e-5 | +0x100 / +0x118 / +0x11c / +0x120 | cap_data / improve_tolerance / tracking_type / laminar_trim | zeros / 0x5e42 / 0x5e92 / 0x60a4 |
| | | | +0x128.. / +0x188 / +0x18c / +0x190 | limit_data / suggest_limit / report / extension_shape | zeros / 0x60cc / 0x61c7 / 0x58de |

`update` by the op version (`param_5`): >= 0x1c 0x56b0 (new ops: 0x1c), 0x18..0x1b 0x5d0f, 0x16..0x17 0x5d0d,
0x13..0x15 0x5d0b, 0x12 0x5d09, 0x11 0x5d07, 0x10 0x5d05, 0xf 0x5d04, 0xe 0x5d02, 0xd 0x56b6, 0xb..0xc 0x56b5,
9..0xa 0x56b4, < 9 0x56ae. Versions with 0x56b4 / 0x56b5 retry once with the other token when fix_blends fails with
fault 0x3ec1 or error 0x14a. A replayed op keeps its own version (op+0x3c), so old ops regenerate with old blend
behaviour.

### 5.3 Naming the result (`FUN_180036580` tail, dis1.txt; `FUN_18004b9b0`)
- every blend face (fix_blends' `blends`): entinfo kind **0x2e**, op id, ref1 = the McEptr record of its underlying
  edge (`topols[i]`, looked up first in the fix input edges, then - op version > 0x15 - among all edges of the body
  before the fix), ref2 0 (`FUN_180050bd0(face, 0x2e, op, rec, 0)` at 0x180036d46);
- every edge of a blend face without a record: kind **0x8000017** with ref1 / ref2 = its two faces (tags; bit
  0x8000000 = refs still tags, resolved to records by `FUN_180265240` -> `FUN_1802652b0`, case 0x17), then every other
  record-less edge of the body likewise;
- split pieces (bulletin board, `FUN_18004b9b0`): face -> kind **0x33**, edge -> **0x2d**, ref1 = the record of the
  entity it came from, ref2 0.
Then (new op) `FUN_18004a4b0`: the op's record list = every face / edge of the body without a record or with a kind
| 0x10000000 record (`FUN_1802656b0`), the op's u64 list (op+0x80) = the records that existed before and are gone.

## 6. The history record (op type 9 "Fillet", SldFilletOp)

### 6.1 Layout (read `FUN_180281170`, write `FUN_180281c70`, size `FUN_1802808c0`; McSldEntOp read `FUN_180274c80`,
size `FUN_180274ad0`). 64-bit file, ops version 0x1a / 0x1c, SysFile version > 9:

| bytes | field | rule (code) | corpus (589 ops, 6.3) |
|---|---|---|---|
| McOperation base | solid_history.md 2.3 | | |
| i32 n1 + n1 x 38-byte McEptr | op+0x44 records: the faces / edges this op created (5.3) | kinds 0x2e / 0x17 / 0x2d / 0x33 | kinds 0x17 10666, 0x2e 3358, 0x2d 136, 0x33 58; op id = this op in all 14218; x28/b2c/b2d (0,0,0) in 14101 |
| i32 n2 + records | op+0x60 | always empty for fillet | 589 / 589 empty |
| i32 n3 + n3 x u64 | op+0x80: ids of records of earlier ops that this op consumed (`FUN_18004a4b0`) | | 3556 / 3556 are records of an earlier op of the solid |
| u16 len + name | op+0x20 | panel name | "Fillet" 508, user names 80, "Constant Radius Fillet" 1 (the only 2017 op) |
| u8 x4, u32 | op+9 / +0xa / +0xb / +0xc, op+0x10 | NOT DECODED (solid_history.md 2.3) | (0,0,0,0,0) 565, (0,0,1,0,0) 21, (1,0,0,1,1) 2, (1,0,1,1,1) 1 |
| u64 / u32 / u32 / u32 | solid eptr, op id, type 9, op version | | op id = index 589/589; type 9; versions 8:123, 9:2, 10:412, 12:16, 15:24, 16:2, 17:1, 19:8, 28:1 |
| u8 u8 | op+0x40 / +0x41 | NOT DECODED | (0, 12) in all |
| u8 | op+0x90 **whole body** | 1 = a body was picked | 1 in 3 ops; 1 <=> both lists empty (3 / 586) |
| i32 nA + nA x u64 + nA x 0x18 | list A op+0x94/98/a0: picked **faces**: record id (mcEptrPtr) + bbox centre (3 doubles, part units) (`FUN_180275980`, `ps_get_bbox_center`) | ids = records of earlier ops | 25 ops with 1 face; kinds 0x1f / 0x38 / 0x33 / 0x34 |
| i32 nB + nB x u64 + nB x 0x18 | list B op+0xa8/b0/b8: picked **edges**: record id + bbox centre (`FUN_1802757e0`) | | 1..10+ edges; every id = an earlier record or 0 (33: record-less edges) |
| u8 (+ nB bytes if set) | op+0xc0 | cleared by `FUN_1802757e0` | 0 in all |
| i32 n3' + n3' x 0x48 | op+0xd0 (ops version > 2) | not used by fillet | 0 in all |
| i32 n4 + n4 x 0x60 | op+0xe0 (ops version > 3) | not used by fillet | 0 in all |
| 0x18 bytes | P (op+0xf0); 0x12 bytes packed (int, double, int, u8, u8) when the SysFile version is 1..9 | 0 section | type 0: 571, 1: 18; radius > 0 in all; overflow 0: 569, 1: 4, 2: 16; mitered 0 in all; propagate 1: 398, 0: 191 |
| i32 n_vr | op+0x108 variable-radius items (`SLD_FILLET_VRDATA`, 0x78 bytes in memory) | 0 for type 0 | type 0 <=> n_vr 0 (571 / 571); type 1: n_vr 1 (16) / 8 (2) |
| per item: i32 k, k x 8 bytes (item+0x08), k x 8 bytes (item+0x30) | variable fillet only (`FUN_180281450` builds `sld_var_fillet` input from them) | | |

Size check: `FUN_1802808c0` = McSldEntOp size + 0x18 (0x12) + 4 + per item (4 + 16 k); every Read compares the bytes
read with it (sld 259 on a mismatch). The **P padding** (P+0x04, +0x16, +0x17) is whatever the 0x18 bytes held:
non-zero in 364 of 589 old ops (e.g. P+0x04 = 1767887765) and zero in the 2017 op: write zeros.

### 6.2 What a new op holds (`sld_fillet`, new op path)
op type 9, name = the panel name, op+0x18 = the solid's eptr, P copied whole, whole-body flag, list A = picked faces
of this body, list B = picked edges of this body (record id = the face's / edge's mcEptrPtr, 0 when it has none),
records + consumed list (5.3), then `FUN_180262b50` appends it (`FUN_180267170`, Solids manager entry).

### 6.3 Corpus proof (`tools/sld_fillet.py`)
`tools/sld_fillet.py <user-parts> @~/mastercam-re/work/occt/netx/tags.tsv` (1381 parts: the 17 user parts
- no fillet ops - and the 1364 internet sample parts; Mastercam's install templates contain no solids, solid_history.md
header): 77 parts / 116 solids hold **858 fillet ops**; 589 in solids of solid version 0x1a / 0x1c (what 2017 reads)
**all parse to their exact byte size** with the layout above, and the other 269 (solid version 0x1f, written by later
Mastercam, not readable by 2017: sld 678) also parse with it. All rules in the table hold for the 589 except where
counts are given. Cross-references checked: every list-A / list-B / consumed id is the id of a record of an earlier op
of the same solid; every 0x2e record's ref1 is an earlier edge-type record (3353; 0 in 5 ops of op version 10, where
the edge had no record) and is in the op's consumed list; every 0x17 record's refs are this op's 0x2e faces (13297)
or earlier records (7975) or this op's 0x33 faces (60); 0x2d / 0x33 records refer to earlier records (ref2 0) and
occur only in op versions 8 / 10.
One whole-body example: X9 METRIC "TUTORIAL #7 EXERCISE" sld 30 op 4 (r 1.5, whole body): the stored XT has PLANE 11,
CYLINDER 23, TORUS 24 faces (tools/xt_parse.py) - Parasolid made exact cylinders / tori, no blend surface type.

### 6.4 Replay (`FUN_180281450`, slot 25)
op+0xa = 0; not whole-body: list A / B ids -> tags (`FUN_180269310`, with the bbox-centre points as the fallback when
an xform is active); **any id that does not resolve -> sld 291 "Fillet input edge(s)/face(s) not found."** (box,
caption 201), op failed (op+0xa = 1), return false; whole-body: the solid's body tag. Type 0: `sld_fillet(body list,
faces, edges, P, NULL, op, op version, false)`; failure -> op+0xa = 1, `FUN_18026c590(ops, 1, 0)` (dirty); success
-> op+9 = 0. Replay failure handling of the whole history: solid_history.md 4 (sld 207 / 255, rollback to the last
good op).

## 7. Failure handling
- Selection empty at OK: validator sld 2029 (panel error state). Radius < tol: sld 2030.
- `PK_EDGE_set_blend_constant` error: box caption sld 566 "Failure to attached blends to body.", text "Parasolid(R)
  Kernel Interface Error:" (sld 203) / "PK Error Code: N -" (183) / sld (10000 + N) (`ps_report_error` 0x18004f0e0,
  kind 1; N >= 5000: 15000 + N - 5000).
- `PK_BODY_fix_blends`: fault 0x3ec1 or error 0x14a (330, sld 10330 "could not fix blends in body") -> box caption
  sld 538 "Blend creation failed.", text sld 538 + newline + sld 665 "Probable cause: Blend overflow removes adjacent
  face or edge."; another PK error -> the kind-1 box with caption 538; a fault other than 0x47d6 / 0x47d7 ->
  `FUN_18004f490(3, fault, 538)`: "Blend Error:" (sld 174) + sld (20100 + fault - 0x3eb2) (the BLCC* table, sld
  20101..20124; for faults outside that range the string id has no entry: what is shown is **NOT DECODED**).
  `ps_report_error` also logs the text (`LogMastercamEvent`) and, under the testing manager, does not show the box.
- In a preview the same messages go to `%TEMP%fillets.tmp` and are shown as a warning (1.3).
- A failed body: its op is deleted, the body goes back to its mark (`FUN_18004f660`), nothing is kept in history; other
  bodies of the same command keep their fillets (3). Mitered: all-or-nothing per body (the per-body mark).
- Only smooth edges selected: success, nothing changes, no op content beyond an empty fillet [inferred: an op is still
  created with no new records - `FUN_180036310` returns 1 with no edges; not run].
- Replay: sld 291 (6.4), then the history replay messages.
- `sld_do_fillet` returns 3 without a panel when `sld_solids_ok(1)` fails (sld_core.c l.1035; what `sld_solids_ok`
  checks is not traced here).

## 8. OpenCASCADE mapping (OCCT 7.9.3, `/usr/include/opencascade/BRepFilletAPI_MakeFillet.hxx`)

| Mastercam | OCCT | exact? |
|---|---|---|
| per body, edges collected as in 3 (body -> all edges, face -> its edges, smooth edges dropped) | our code: TopExp::MapShapes(body/face, TopAbs_EDGE); smooth = BRep_Tool::Continuity(edge, f1, f2) >= GeomAbs_G1 [inferred equivalent of PK_EDGE_is_smooth; Parasolid's tolerance unknown] | collection yes; the smoothness test's tolerance NOT DECODED |
| radius in metres | `BRepFilletAPI_MakeFillet(shape, ChFi3d_Rational)`; `Add(r, edge)` per edge, r in the shape's units (metres, occt_xt.md) | yes |
| Propagate along tangencies **on** | `Add` "builds a contour of tangent edges" (header l.80-88) | behaviour equal in intent; Parasolid's tangency tolerance vs OCCT's NOT DECODED |
| Propagate **off** (0x47ea) | none: OCCT always extends a contour over tangent edges | **no** |
| Overflow Default / Maintain blend / Maintain edges (ov_smooth, ov_cliff, ov_cliff_end, ov_notch) | none (ChFi3d has its own fixed overflow handling) | **no** |
| Mitered corners on = one edge at a time | one `BRepFilletAPI_MakeFillet` per edge in sequence, the remaining edges mapped through `Modified()` / `BRepTools_History` (as `FUN_18004b210`) | flow yes; geometry of the end caps differs |
| Mitered off = all edges at once (Parasolid vertex blends) | one MakeFillet with all edges (OCCT corner / vertex blends) | corner patches differ |
| xs_shape 0x56b9, fix_blends options (5.2) | `ChFi3d_Rational` circular section; `SetParams` tolerances | token meanings NOT DECODED |
| fix_blends fault / error | `IsDone()`, `NbFaultyContours`, `StripeStatus`, `HasResult` / `BadShape` | messages: map any failure to sld 538 (+665?) - see Q-F4 |
| result surfaces: Parasolid made PLANE / CYLINDER / TORUS for the sample (6.3) | OCCT: the ChFiKPart special cases (headers `ChFiKPart_ComputeData_FilPlnPln / FilPlnCyl / FilPlnCon ...`) give analytic faces - occt_xt.md 4.1: a box with all 12 edges filleted gave cylinders + spheres, a fillet round a hole a torus; general cases give B-splines | analytic cases match in type; B-spline cases need the XT writer's B_SURFACE (occt_accuracy.md: not mapped yet) |
| naming (0x2e / 0x17 / 0x2d / 0x33, consumed list) | `Generated(edge)` -> blend faces (0x2e, ref1 = edge record), `Modified()` -> split pieces (0x33 / 0x2d), new edges of blend faces (0x17 with their two faces) | data model yes; record order (Parasolid's) no (solid_history.md 3.2) |

## 9. NOT DECODED
- PK token value names (0x47ea / 0x47eb, the ov_* tokens, 0x56b9, the fix_blends constants): no Parasolid headers.
- Which selection-dialog control sets `sld_array_pick`'s third flag (`sld_add_all_color_sel`); who writes
  DAT_1803bac30..32 back; the Selection Bar / Solid Selection dialog's own drawing.
- The Operation expander's Advanced-part open state (Overflow is in it); the Help topic.
- `FUN_1801264b0`'s exact radius-change rule (compare with `tol`).
- Preview drawing of the fillet body (assumed as Extrude's).
- App+0x132 (edit-mode "rebuild anyway" flag) - who sets it.
- Variable-radius item contents (k x 8 bytes at item+0x08 / +0x30): only that `FUN_180281450` passes them to
  `sld_var_fillet` (Variable Fillet, S17).
- Base op bytes op+0xb / +0xc / +0x10 / +0x40 / +0x41 (also open in solid_history.md); McEptr x28 = 1 in 35 records.
- What `FUN_18004f490(3, fault)` shows for fix_blends faults outside the BLCC* range.

## 10. Open questions for the user (deviations need approval)
- **Q-F1 Propagate along tangencies off.** OCCT cannot leave tangent continuations unfilleted (BRepFilletAPI always
  builds the tangent contour). Options: (a) keep the checkbox and store it (history / XT round-trip exact), build
  always-propagated in OCCT and mark the result as ours; (b) refuse the command with Propagate off (message ours).
- **Q-F2 Overflow.** OCCT has no overflow choice: all three settings would give OCCT's result. Same options as Q-F1
  (store as decoded, build with OCCT's single behaviour, marked) or refuse non-Default values.
- **Q-F3 Corner shape.** Non-mitered corners (Parasolid vertex blends) and the end caps of mitered fillets will not
  match Parasolid's geometry; where OCCT fails and Parasolid succeeds the command fails. Accept as a known difference
  (checked by the regression only for success / failure and face counts)?
- **Q-F4 Failure messages.** OCCT has no PK error codes / BLCC faults, so Mastercam's box text (PK code + sld 10000+N,
  or "Blend Error:" + BLCC text) cannot be produced. Proposal: on any OCCT failure show the box Mastercam shows for
  fault 0x3ec1 / error 330 (caption and first line sld 538 "Blend creation failed.", second line sld 665 "Probable
  cause: Blend overflow removes adjacent face or edge."), or the same without the 665 line? Either is a deviation.
- **Q-F5 Record order and names.** The op's record list will follow OCCT's face / edge order, not Parasolid's (the
  same open point as Extrude, solid_history.md 3.2). Accept, with the dirty byte or without (solid_history.md 5.1)?
- **Q-F6 Smooth-edge test.** Mastercam drops edges for which `PK_EDGE_is_smooth` is true; OCCT's equivalent needs an
  angular tolerance (Parasolid's is unknown). Use OCCT's `BRep_Tool::Continuity` (set by `BRepLib::EncodeRegularity`,
  default angle 1e-10 rad) - ours, marked?

## 11. Port (phase 2, 2026-11-18; user decisions SOL-F1..F5, formats/user_decisions.md)

Built on the solid save path (SOL-1..4: XT writer, McOpsFile writer, Extrude on OpenCASCADE). A fillet is one feature per body
(`ExFeat.fl`, viewer/core_ui/core_ui.h `FilFeat`), replayed with the Extrude features (cui_feat.c `feat_apply` -> `fil_apply`) and written as
a SldFilletOp by the part save.

| piece | file | from |
|---|---|---|
| kernel `ocs_fillet` (all edges in one build / mitered: one build per edge, the rest carried through the history), the names (0x2e / 0x17 / 0x2d / 0x33 / 0x34, n3 = records gone), `ocs_edge_smooth`, `ocs_face_edges`, `ocs_tangent_edges` / `_faces`, `ocs_pick`, `ocs_sub_center`, `ocs_find_rec`, `ocs_sub_lines` | viewer/occt_solid.cpp / .h | 3, 5.3 |
| the command: pick (Body / Face / Edge buttons, toggle, Shift tangents, UnSelect All, End Selection), panel model, validator, preview, OK / OK-New / OK-Regen / Cancel / Esc, Add Selection / Reselect All / Remove, edit, session, `fil_apply` (body / face expansion, smooth-edge drop, sld 291 when a pick no longer resolves), script steps | viewer/core_ui/cui_fillet.c | 1, 2, 3, 6.4, 7 |
| the history record: McSldEntOp + SldFilletOp read / write | viewer/sld_ops.c / .h (`SLD_CLS_FILLET`, `sld_filp_get` / `_put`) | 6.1 |
| the op built from the feature (lists A / B = record ids + bbox centres, whole-body byte, P with zero padding, n_vr 0) | viewer/core_ui/cui_sldsave.c `build_op` | 6.2 |
| project lines `FILLET` + `FREC` / `EREC` (ours) | viewer/core_ui/cui_feat.c | - |
| the panel and the Solid Selection page | viewer-qt/src/filpanel.cpp / .h; mainwindow.cpp (ribbon FT sld_do_fillet, clicks, Esc / Enter / Delete, overlay) | 1.2, 2 |
| messages sld 291 / 538 / 665 (+ the SOL-F1 refusal, ours) | viewer/core_ui/cui_extrude.c `ex_sld` / `ex_report` / `ex_err_text` | 7 |

Ours (marked in the code): OCCT's geometry and overflow handling (SOL-F2 / F3); the smooth-edge test (SOL-F5); the tangent-chain test
of Shift picks (angle 1e-6 rad; ps_edge_find_tangents NOT DECODED); the pick aperture (6 px) and nearest-edge rule; a trimmed neighbour
edge / face that OCCT rebuilds without history is matched to its old name by its curve / surface handle (Parasolid keeps the entity);
a pick without a record resolved by its bbox centre; the Solid Selection dialog drawn as a page of the panel's dock; the Overflow combo
always shown; the selection highlight colour; the preview's failure text shown in the panel (FUN_180156bc0's DisplayWarning).

Refused: Propagate along tangencies off (SOL-F1: message, not built; the panel stays). **Not ported: a fillet on a solid read from the
part** (a message says so): its faces / edges have no McEptr names after the load (solid_history.md 3.3), so lists A / B, the 0x2e refs
and the consumed list could not be written as decoded - it needs the part's history replayed with names first (question to the user).
Variable-radius ops (P+0 != 0) are read / written byte for byte but not built here.

Tests: tools/tests/sld_ops_test.sh - **858 / 858 corpus fillet ops re-serialise byte for byte** (and every ops block holding them);
tools/tests/solid_scenes.txt fl_* / sv_fillet_* scenes (edge / face / body picks, Shift tangent chain on and off, mitered, overflow,
the SOL-F1 refusal, the 538 + 665 failure, validator errors, list Remove, Add Selection + Esc, Reselect All, Esc / empty End Selection,
real clicks with the Body / Face / Edge buttons, edit + undo, edit unchanged, OK-New, session, undo / redo, two bodies = two ops, the
project round trip, a fillet on a cut body, the base Extrude edited under a fillet, a part solid refused); the written parts' histories
(`*.sldhist`, tools/sld_hist.py) and tools/sld_fillet.py on them follow every corpus rule of 6.3 (lists A / B / consumed ids = earlier
records, 0x2e ref1 = the replaced edge's record and consumed, 0x17 refs = own 0x2e faces or earlier records, whole-body <=> empty lists,
type 0 <=> n_vr 0, P padding 0, +0x40 / +0x41 = 0 / 12).
