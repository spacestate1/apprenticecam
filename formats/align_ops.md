# Toolpaths manager: Delete / Delete key / Cut-Copy-Paste / Undo-Undelete / Engrave (Mastercam 2017)

Status: research spec (2026-10-26), read-only. Every behaviour cites its source; "NOT DECODED" = not found, do not implement
from guesses. Addresses are mastercam.dll unless marked (core = mccore.dll).

Decompiles used (repo <repo>):
- existing: `decomp/opman_tree_all.c` (all COpMgrTree), `decomp/opman_opsarray.c` (COperationsArray), `formats/opmanager_menu.md`.
- new (gitignored, `decomp/align/`): `mc_keys.c` (CMastercamApp::CheckForKeymap, OMOnCut/Copy/Paste/DeleteKey/UnDelete/SelectAll,
  COperationsArray::CopySelected/CutSelected/Paste, COpMgrTree::CopyOperations), `mc_keys2.c` (CMastercamApp::ExecuteKeymap
  @18028a400, main-frame Cut/Copy FUN_180261310, Paste FUN_180267ca0, FUN_1803199b0), `mc_strrefs.c` (main-frame
  PreTranslateMessage FUN_18026ad90, `Delete` @180272ab0), `callers_mc*.tsv`, core: `core_undo.c` (UndoEvent, UndoTrans*,
  undeleteUndo, OnUpdateUndoEvent, FUN_1803509a0), `core_oplist.c` (TpPartOpList::DeleteListAndDB @180907f50, list_op_copy
  @1800b8700), `core_undo_syms.txt`, `core_callers.tsv`. Message-box style mapper core FUN_18001d7f0 is in `decomp/align/geo_core_b.c`.
- strings: `~/mastercam-re/regress/str/all.tsv`; dialogs: `tools/pe_dialogs.py`; help: `~/mastercam-re/chm/mastercam/Toolpath_Manager/*`,
  `~/mastercam-re/chm/mcengrave/Engrave/*`.

Data model used below (all [code], opman_opsarray.c): `COperationsArray` at COpMgrTree+0x7278 keeps three buffers:
copy buffer (VArray at +8: id/name references only), cut buffer (+0x1b8 tree entries, +0x1d8 full `operation` copies),
delete buffer (+0x200 tree entries with the original list index at entry+0x14, +0x220 full `operation` copies).
`DeleteOperationsFromDb(mask)` = **purge** (bit 1 cut buffer, bit 2 delete buffer); `DeleteBufferedOperations(n)` @180378910 =
purge with selection kept.

---------------------------------------------------------------------------------------------------------------------------
## 1. DELETE of operations

### 1.1 Entry points (all end in `COpMgrTree::OnDeleteKey` @180360de0)
| entry | path | source |
|---|---|---|
| right-click menu 3566 "Delete" (opmanres 3566 tip "Delete selected operations") | `OnRclick` case 0xdee -> `OnDeleteKey` | opman_tree_all.c OnRclick (switch at l.~22007) |
| Delete key with keyboard focus in the tree | `COpMgrTree::ExecuteKeymap` case 0x2e -> post FT `("Mastercam","OMOnDeleteKey")` -> `OMOnDeleteKey` @18036a140 -> `OnDeleteKey` + `EnsureOpOrGroupSelected` | opman_tree_all.c l.17304-17480; align/mc_keys.c |
| FT command `OMOnDeleteKey` (HIDDEN, FCAT "Operations Manager", FNAME "Delete") | mastercam.ft l.6922 | files/mastercam.ft |
| toolbar | **no Delete button**: the manager toolbar's delete is 33350 = "Delete all operations, groups and tools" (OMdeleteAllOperationsAndTools, Yes/No core 7617 "Permanently delete all operations and tools?", title 7616) - permanent, no Undelete | formats/opmanager_menu.md §3; opmanres 33350 |

Menu enabling (OnRclick, formats/opmanager_menu.md §2): 3566 greyed when ops<1 or nothing selected, or any selected op is in a
locked group (`group+0x1ab8`). The keyboard path has no greying; OnDeleteKey filters itself (below).

### 1.2 OnDeleteKey algorithm [code, opman_tree_all.c l.19974-20190]
1. Return if `this[0x7255]` (busy flag) or product bits &2 (`GetProdEnableBits`).
2. `GetSelectedOperations`. For each selected op: if `OpInLockedGroup` -> dropped silently; else if `OperationInProcess(id,1,warn)`
   (op being processed) -> dropped, `warn` true only for the first one (its message text NOT DECODED). Dropped ops are deselected.
3. `groupHit` = the tree's current item (`**(this+0x3628)`) has lParam -1 (a group).
4. If neither ops remain nor `groupHit` -> nothing.
5. **Confirmation** only when ops remain AND `this+0x75a4 != 0`:
   `message_box_yes_no(core 8979, core 7616, 2)` - title mccoreres 7616 "Toolpath Manager", text mccoreres 8979:
   "You are about to delete the selected operations.\nUndelete is limited and may not be able to undo this action.\n\nDo you want
   to proceed?\n\nThis message can be disabled in the Display Options found\non the right-click menu inside of the Toolpath Manager."
   Buttons: style = MB_YESNO | (2 -> 0x20 MB_ICONQUESTION) (core FUN_18001d7f0); no MB_DEFBUTTONn flag -> **Yes is the default
   button** (Win32 default; MCMessageBox internals not read). Result = IDYES (core message_box_yes_no @18001e9f0). No -> stop.
   Note: when the core message log destination is 0 or 2 (`get_msg_log_dest`) the box is not shown and the answer is No.
   - `+0x75a4` = Display Options (dialog opmanres 821) check box **4061 "Prompt for toolpath deletion"** (COpMgrTree::DisplayOptions
     @18035a2c0 copies it to/from dialog +0x15bc). Help Display_Options_dialog_box.htm: "you can also enable or disable the prompt
     to confirm toolpath deletions"; options persist across sessions (same page). **Default value and storage key: NOT DECODED.**
6. `DeleteBufferedOperations(3)` - **the previous delete (and any pending cut) is purged first: only the last delete is undoable.**
7. If `groupHit`: ops of that group (`GetOpIdsFromGroup`) - if none, the group and its direct child groups are deleted
   (`DeleteGroup(id,0)`, failure aborts); else each selected op of the group goes through `DeleteOperation(id)`.
8. If ops were selected: `DeleteSelectedOperations` @1803589d0 (below).
9. If anything was deleted: `RefreshAll`; if `NestingSelected()` returned 1 -> `DeleteBufferedOperations(3)` again (nesting
   deletes are immediately permanent, no Undelete); `repaint_graphics`.
No message after the delete (none in the code).

### 1.3 DeleteSelectedOperations @1803589d0 [code]
- `SelectMatchingPinchOp(1)`: pinch-turn partner ops are selected and deleted too.
- Solid-control ops (`op_is_solid_ctrl`, only when `this[0x7428]`): spawned ops (`CDependentOperations::DetectSpawnedOperationIDs`)
  are appended to the list and deleted too; for an FBM drill (opcode 0x88 with `op[0x1ca8]==0`) whose spawned children are not all
  selected -> `post_warning(4041)` opmanres 4041 "FBM Drill operation can not be deleted because it has dependent child drill
  operations." and **nothing** is deleted.
- Each op -> `DeleteOperation(long)` @180378b10: ops whose parent is a Nest op (`ParentIsNest`) are refused silently (-1);
  `mark_op_dependent_stock_models_dirty(id)`; tree items removed; `COperationsArray::DeleteOperation`.

### 1.4 What a delete does to the op (soft delete) [code, COperationsArray::DeleteOperation @180308840]
- The op's list index is stored (entry+0x14) and a full copy of the `operation` struct goes to the **delete buffer** (`DelOperationsAdd`).
- Lathe/mill-turn products (bits 0x120): `UpdateStockFlipOp`, `UpdatePinchTurnOps`, `UpdateCannedFinish`, `UpdateQuickFinish`,
  `mark_ops_for_regen(id,1,0)`.
- `TpPartOpList::DeleteListAndDB(id, runningInSolidWorks, false)` (core @180907f50): with param_2=false (normal Mastercam) the op
  leaves the **in-memory list and its display** (`remove_opengl_list` / `DeleteToolpath`) but is **not removed from the DB**, its
  tool is not touched, the NCI is not touched, dependents are not marked.
- `OMsetOperationSyncs(0, [-op+0x28c])` (op+0x28c = its group) - sync effect NOT DECODED.

### 1.5 Purge (permanent delete) [code]
`DeleteOperationsFromDb(buffer)` @180308b50: `UnbufferOperations` re-adds the buffered ops (`AddListAndInit`), then for each
`TpPartOpList::DeleteListAndDB(id, true, true)`:
- param_3 (true): if op+0xd0 points to an op-id parent, `DeleteToolpathEntitiesPointingToSpawnedOps`; **`mark_op_dependents(id, op_is_bi, 0, 1)`**
  = dependent ops (e.g. Transform ops referencing it) are marked for regeneration (dirty).
- param_2 (true): `DatabaseRetrieve` + `DatabaseRemove` (record removed); if the op has a tool and its tool is used by < 2 ops
  (`list_op_by_tl`) and a flag in the record (+0x2d00 of the 0x2938 buffer) is set, the internal tool system's vtable slot 0x68 is
  called with that tool number (presumably removes the now-unused tool; slot meaning NOT DECODED); `list_op_set_by_nci(op NC
  name +0x170)` and `MTMgr::DeleteOperationData(id)` (exact NCI effect NOT DECODED).
- Geometry: the part geometry is never deleted. Toolpath entity records (`GetToolpathEntities`, ent_tp +0x90 kind 9 / 0x2b, or a
  reference whose associated entity is gone - `tp_get_assoc_ent` fails) are deleted by `DeleteToolpathEntities` in
  UnbufferOperations (kind 9 = TP_ENT_REF, 43 = TP_EPTRS by tools/mcam_export.py TP_KINDS - the +0x90 = kind mapping is inferred).

**When the buffer is purged** (callers of DeleteBufferedOperations, align/callers_mc.tsv): OnDeleteKey (next delete),
`CreateNewOperation` (any new toolpath from the menu), `DeleteGroup`, `DeleteEmptyGroups`, `EditGroup`, `OMdeleteAllOperationsAndTools`,
`StartUp`, three unidentified FUN_1804093d0 / FUN_1804a0ed0 / FUN_1804a5470, and the export `OMdeleteBufferedOperations` (FUN_1803199b0;
skipped while a tree menu command is executing) called by core FUN_1803509a0 (file open; mask 3), mastercam_filenew.c:301 (3),
mastercam_grpprops.c:753 (3), mclathe_all.c:396612 (2). **Cut / Copy / Paste do not purge the delete buffer** (CutSelected /
CopySelected purge only mask 1 = the cut buffer; OnRclick cases 0xdeb/0xdec call nothing else) - note formats/opmanager_menu.md
l.200 ("Copy/Cut commits the buffer") is wrong for the delete buffer. Help Guidelines_for_working_in_Toolpath_Manager.htm says
"You can only undelete an operation in Toolpaths Manager immediately after deletion, before executing another command." - the
code commits only at the points listed (a generic per-command commit was not found).

### 1.6 Locked ops / groups
Locked group (`op_group+0x1ab8`): menu greys Delete/Cut/Sort; OnDeleteKey drops such ops silently [code]. The tree's L key
(`OMOnLKey`) "Toggles toolpath locking" (help Toolpath_Manager_shortcut_keys.htm) - that is NCI lock, not a delete guard (no
check of it in OnDeleteKey). Help: "Machine Groups cannot be deleted if they are active. Only the operations in them will be
deleted." (Guidelines page) - code path: DeleteGroup refusals msgs 3823 / 3824 (formats/opmanager_menu.md §4 Groups).

### 1.7 Undelete
- Menu 3567 "Undelete" (tip opmanres 3567 "Undelete previously deleted operations"), **never greyed** (OnRclick has no
  EnableMenuItem for 0xdef) [code]; Ctrl+Z in the tree (§4); FT `OMOnUnDelete` (FNAME "Un-Delete").
- `UndeleteOperations` @1803682b0 -> `UnbufferOperations(ids, 2)` @18030d620: every op of the delete buffer is re-added with
  `AddListAndInit` at its stored index (index + 1 + i), op+0xcb8 cleared (meaning NOT DECODED), toolpath-entity references to
  geometry that no longer exists are deleted (as §1.5); buffer emptied; `LoadTree`; the restored ops become the selection. Empty
  buffer -> nothing (no message).
- Stack depth: **one** delete (all ops of it). Help right-click page: "Undelete - Undoes the last operation/group deleted from the list."

---------------------------------------------------------------------------------------------------------------------------
## 2. The Delete key: routing by focus [code]

MFC pre-translates a key from the focused window up its parents. Order:
1. **Tree focused** -> `COpMgrTree::PreTranslateMessage` @180364f70: an in-place label edit gets the key; Enter / Esc are posted as
   0x802b / 0x802a; otherwise `CMastercamApp::CheckForKeymap` @180288890 -> `CMastercamApp::ExecuteKeymap` @18028a400:
   if the focus window is the Op Mgr tree (`GetOpMgrTreeCtrl() == CWnd::FromHandle(GetFocus())`), `OMexecuteKeymap` ->
   `COpMgrTree::ExecuteKeymap` @18035d350 handles: Del -> OMOnDeleteKey, Ctrl+Z -> OMOnUnDelete, Ctrl+X -> OMOnCut, Ctrl+C ->
   OMOnCopy, Ctrl+V -> OMOnPaste, Ctrl+A -> OMSelectAll, E/L/P/T -> OMOnE/L/P/TKey, arrows -> OMarrow*. (Floating pane case: if the
   active window is a floating frame, the message's hwnd is tested for COpMgrTree instead of the focus.) Handled -> the key is
   consumed: with tree focus, **Delete never reaches geometry**, even with nothing selected in the tree (OnDeleteKey then does nothing).
   Ctrl/Shift for X/C: Shift held -> `set_FT_item_param` passes 1 = **append** to the existing cut/copy buffer (CutSelected /
   CopySelected skip the clear when param=1).
2. Otherwise the keymap table (mastercam.kmp; it has no "Del" entry - files/mastercam.kmp) and then the **main frame**
   `PreTranslateMessage` FUN_18026ad90: on WM_KEYDOWN, unless the focus is inside frame child windows +0xa130 / +0x9bf8
   (identity NOT DECODED), the ribbon takes it, a manager is editing a name (`EditingAName`), a UIEdit has focus, or the focus is
   inside an MCDialogBar with +0x8a4 set: **VK_DELETE -> post FT `("Mastercam","delete_entities")`** (the graphics-side delete;
   mastercam.ft "Delete Entities", keymap F5 too).
- Result: **the focused window decides**: tree focus = operations; graphics / anything else = selected entities (delete_entities).
  There is no rule combining "geometry selected AND ops selected" - nothing checks the other selection.
- What `delete_entities` does with entities used by operations (associativity, dirty ops): NOT DECODED here.
- Help Guidelines page: "Move your mouse over the Toolpaths Manager to make it active. When you move your mouse back over the
  graphics window, then that becomes active" - focus-follows-mouse code NOT DECODED (COpMgrTree::OnMouseMove only does
  drag work; no SetFocus in opman_tree_all.c).
- The same focus routing for Ctrl+X / Ctrl+C / Ctrl+V when they come through the global keymap (`OnEditCut/Copy/Paste`,
  FUN_180261310 / FUN_180267ca0): Edit control -> WM_CUT/COPY/PASTE; Op Mgr tree -> Cut/CopyOperations(Shift) / PasteOperations;
  graphics view (DAT_1809a8578) -> entities to the clipboard as format "EntFormat" (Cut: `UndoDeleteTransStart`,
  `delete_all_by_mask`, `UndoTransCommit` - an undoable entity delete).

---------------------------------------------------------------------------------------------------------------------------
## 3. CUT / COPY / PASTE of operations [code]

- **Copy** (menu 3564, Ctrl+C in tree): `CopyOperations(shift)` -> `SelectMatchingPinchOp(0)`, `CopySelected(shift)`: unless Shift,
  the pending **cut buffer is purged** (permanent delete of cut ops, §1.5) and the copy buffer cleared; then the selected ops'
  references (id, name) are appended. Nothing is duplicated yet.
- **Cut** (3563, Ctrl+X): `CutOperations(shift)`: selected ops + pinch partners (`CutMatchingPinchOp`); unless Shift the previous
  cut buffer is purged; each op copied into the cut buffer (`CutOperationsAdd`) with its index, removed from the list and
  `DeleteListAndDB(id,false,false)` (soft, like a delete); tree items removed. Cut = soft delete into its own buffer (not the
  delete buffer; not undeletable; recovered only by Paste). Cut does not test locks itself (the menu greys it for locked groups).
- **Paste** (3565, Ctrl+V): `PasteOperations` @180380890: needs a copy or cut buffer; `ForceArrowIntoToolpathGroup`; target =
  insert arrow (group = arrow's toolpath group +0x37b4, after the op above the arrow +0x37b8; 0 = top of group).
  `COperationsArray::Paste` @18030b350:
  - cut buffer (wins when present): ops re-added with **the same ids** (`AddListAndInit`), moved to the target group, tool copied
    into the target machine group's tool list when needed (`list_ltl_copy`, `Make_op_tool`), `ValidatePastedOps` +
    `CMachineOpCompatibility` (help Moving_and_copying_items_in_Toolpath_Manager.htm: incompatible ops modified or dropped after a
    prompt; fully compatible "status will remain clean"); moved after the arrow op (`list_op_move`); cut buffer cleared (paste once).
  - copy buffer: per op `list_op_copy(index, dest+1, 1, 1, targetGroup)` (core @1800b8700): new id (`OMgetNextOpId`), geometry
    associations re-stored for the copy (`store_ent_assoc` / `rewrite_ent_assoc` on the same geometry - geometry not duplicated
    [inferred from the callee set]), pinch links / some sync fields reset (opcodes 0x3c/0xd2/0xdc), Dynamic (0x86) copies across
    machine groups re-saved; then compatibility as above. The copy buffer is saved before and restored after `Paste`
    (PasteOperations: FUN_180308030 around the call) -> **repeatable**.
  - Afterwards `LoadTree`, `SelectAll(0)`, insert arrow moved after the last pasted op, pasted ops selected.
  - Renumbering: cut keeps ids; copies get next ids. Dirty state of a pasted copy: NOT DECODED (no explicit +0x94 write in
    list_op_copy).
- Menu: Paste greyed when both buffers are empty; Cut/Copy greyed without a selection (formats/opmanager_menu.md §2).

---------------------------------------------------------------------------------------------------------------------------
## 4. UNDO vs operations [code]

- **Ctrl+Z with focus in the Toolpaths manager = Undelete**, not Undo: `COpMgrTree::ExecuteKeymap` case 'Z' with Ctrl -> FT
  `OMOnUnDelete` -> `UndeleteOperations` (§1.7). Help Toolpath_Manager_shortcut_keys.htm: "Ctrl+Z - Undeletes deleted operation(s)".
- Elsewhere Ctrl+Z = keymap `UndoEvent` (mastercam.kmp; core `UndoEvent` @1802e3dd0 -> `UndoTransEvent(true)` @1802dffd0).
  The core undo (DbUndo.cpp) stacks **entity-DB transactions** (`UndoTransStart` records `db_end`; transaction counters
  +0x3c/+0x4c/+0x5c over DB_LIST_ENT changes; `undeleteUndo(DB_LIST_ENT*)`; solid / xform variants `UndoSolidTransStart`,
  `UndoXformTransStart`); undo applies them, `ValidatePlanes`, `update_graphics`, `DfRegenerateAsap`, `NotifyUserApp(9)`.
  `OnUpdateUndoEvent` enables the button when a transaction exists and none is open. Limits: config symbols `UndoMaxTransactions`
  / `UndoMaxMemory` / `UndoLimited` exist (core_undo_syms.txt); values not read here.
- **Operations are not recorded**: no Undo* / undo_* call anywhere in COpMgrTree / COperationsArray (grep of opman_tree_all.c,
  opman_opsarray.c, opman_handlers_d1.c, opman_helpers.c = 0) and mcmill toolpath creation calls only `undo_disable/enable` inside
  `TPEdit::RunPosDlg` (mcmill_all.c l.409309). So op add / delete / edit are not on Ctrl+Z's stack.
- Op-level history instead: right-click **Undo > time #n** (per-op parameter snapshots `BackupOpsAdd`, formats/opmanager_menu.md §4
  "Undo > Time #n"; help: "Allows you to undo changes made to an operation during a session").
- "Undelete all" / a multi-level undelete: **does not exist** (menu 780 has only 3567; the buffer holds one delete).

---------------------------------------------------------------------------------------------------------------------------
## 5. ENGRAVE

- Menu 3628 "Engrave" posts FT `MWENGRAVE` / `m_main` (decomp/opman_create_map.tsv l.12). `files/engrave.ft`: APPLICATION
  "MWENGRAVE", FUNC_DLL "chooks\engrave.dll", RES_DLL "Resources\engraveRes.dll", CATEGORY "Router C-Hooks", FNAME "Engrave".
  It is a **separate C-hook (ModuleWorks "mwEngrave": classes mwEngraveCoreParams / mwEngraveToolpathImpl ... in engrave.dll)**,
  whose core runs as an **external process** (engraveres strings 161 / 170 "Can't create external process. ::CreateProcess
  failed", 163 "Cannot write parameter file for external process", 58/224 "THE engine of engrave"; engrave.dll names
  `estamp.exe` / `estamp7.exe`; engine module errors HRKORRD / HSCHRAFF / KONTBILD, strings 34-57).
- Stored opcode **416 (0x1a0)**, name mccoreres 277 "Engrave" (formats/opcodes.tsv; corpus: "engraved coaster.mcx-9" ops 2 / 4).
  Mastercam's Contour (opcode 1) has no Engrave contour type (its 2064 combo: 2D / 2D chamfer / Ramp / Remachining / Oscillate,
  formats/opdlg_port_audit.md l.166, l.1739).
- Its own parameters (engraveres dialogs 1802 "Engraving parameters", 1803 "Roughing/Finishing", 1804 "Depth cuts", 1805 "Ramp",
  1806 "Filter settings", 1807 "Engrave remachining", 10502 "Wrap toolpath"): V-groove / chamfer tool (string 12 "Conical part of
  tool is too short...", 15 "The tip diameter can not be zero"); closed chains only (4 "Open chains are ignored"); roughing types
  Zigzag / One way / Parallel Spiral / Clean Corners (21-24); sorting Selection order / Top to bottom / Left to right (26-28); roll
  cutter at sharp edges None / Sharp (102-103); depth cuts equal-depth or constant-volume (help Engraving_depth_cuts.htm); plunge
  angle 10-90 (179); wrapping on surfaces / between curves (105-137). Help Creating_an_engraving_toolpath.htm: "Sharp inside
  corners, created by the Z-axis climbing in the corners", "Inner boundaries are islands".
- **2026-10-26**: now wired as Mastercam's own: CAM_ENGRAVE = opcode 416, the "Engraving" sheet (pages 1802 / 1803), the EStamp7
  engine port (viewer/engrave.c via cam2d_engrave.c). The verdict below is history.
- **Verdict** (before): our "Engrave = Contour with comp off" (viewer/core_ui/cui_ops.c l.56, cui_opdlg.c l.1498, cui_chain.c l.132) matches
  **no Mastercam behaviour**: the real Engrave is opcode 416, a different dialog and a V-bit engine (corner climbing, roughing,
  depth-cut volume rules) that is **NOT DECODED** (external estamp engine, not in any Ghidra project here). Options: drop/grey
  the menu item as "not ported", or keep it explicitly labelled as ours; do not write it as opcode 416.

---------------------------------------------------------------------------------------------------------------------------
## 6. Differences in the app today (audit items; no change made)

| app (file) | Mastercam (section) |
|---|---|
| Delete refuses stored ops: "Only operations created here can be deleted" (cui_ops.c l.1043, l.149) | any op deletes, except locked group / in-process / Nest child / FBM with children (1.2-1.3) |
| no confirmation | Yes/No core 8979 / 7616 when "Prompt for toolpath deletion" (821/4061) is on (1.2 step 5) |
| one op (`selected_camop`) | all selected ops + pinch partners + spawned solid ops (1.3) |
| Delete key: op made here wins over selected geometry unless the op dialog is up (mainwindow.cpp l.1259-1260) | keyboard focus decides: tree -> ops, else delete_entities (2) |
| Ctrl+Z undoes op add/delete/edit on the shared stack (cui_undo.c l.106-130) | Ctrl+Z in the tree = Undelete; global Undo = entities only (4) |
| Copy/Cut drop the Undelete buffer (`op_copy`: `cam_op_free(op_deleted)`) | Copy/Cut purge only the cut buffer; Copy (no Shift) permanently deletes pending cut ops (3, 1.5) |
| Cut goes through delete_selected_op (undo stack) | Cut = soft delete into the cut buffer, same ids on paste (3) |
| Paste inserts after the selected op | inserts at the insert arrow, arrow moves after the pasted ops, pasted ops selected (3) |
| Undelete restores one op | restores every op of the last delete at its index (1.7) |
| Engrave = Contour, comp off | separate C-hook, opcode 416, engine NOT DECODED (5) |

Open questions (NOT DECODED): default + persistence key of 821/4061 (+0x75a4); the two frame windows (+0xa130 / +0x9bf8) that
keep Delete from becoming delete_entities; what delete_entities does to ops using deleted geometry; dirty state of pasted copies;
`OperationInProcess` warning text; the tool-system slot 0x68 / record flag +0x2d00 on purge; `list_op_set_by_nci` effect on the
NCI; the unidentified purge callers FUN_1804093d0 / FUN_1804a0ed0 / FUN_1804a5470; the whole Engrave engine.
