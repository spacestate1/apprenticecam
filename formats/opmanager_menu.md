# Toolpaths (Operations) Manager right-click menu — Mastercam 2017

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

Status: decoded from `mastercam.dll` (Ghidra project `mc2017`). Tags: **[code]** = read from
decompiled code (function @ address in mastercam.dll unless noted), **[inferred]** = deduced.

Decompile dumps (all in `decomp/`):
`opman_onrclick.c` (OnRclick), `opman_tree_all.c` (every `COpMgrTree::`/`COpMgrDrop::` method),
`opman_opsarray.c` (`COperationsArray::`), `opman_handlers_d1.c` (handlers + direct callees),
`opman_helpers.c` (OMdeleteAllOperationsAndTools, RunEditCommonParams, ...), `opman_toolbar.c`
(`COpMgrToolBar::OnButtonPress`), `opman_dialogs.c` (CRenumbTl / CRenWoff / CProgramNumber),
`opman_misc.c`, `opman_create_map.tsv` (toolpath command -> opcode / FT command table).

## 1. How the menu is built and shown

* Message map `COpMgrTree` (@0x180624500): no ON_COMMAND entries at all. `NM_RCLICK` (code 0xfffb)
  -> `COpMgrTree::OnRclick` @180361e80. Commands are dispatched **inside OnRclick** after
  `CMenu::TrackPopupMenu(TPM_RETURNCMD|TPM_NONOTIFY)` returns the chosen id. [code]
* Target item = `TVM_GETNEXTITEM(TVGN_DROPHILITE)` (the right-clicked item). Tree item lParam:
  `>0` op id, `-1` group, `-0xe` insert arrow, `-0xf` machine-group "Properties" child, `-6` a sub-item
  of an op (right-click on it = select parent op and run FT `OMverifySelectedOperations`, or the op's
  C-hook; no menu). [code]
  - clicked op  -> becomes "target op" (used by Undo submenu, Toolpath Editor item, Stock model items);
  - clicked group -> becomes "clicked group" (needed by Rename/Delete group/New toolpath group).
* No menu at all when: tree is a list-only tree (`ImJustaList`), the insert arrow is in a disabled
  Mill-Turn machine group, or product bits &6 (non-CAM product) and no group was clicked. [code]
* A tiny menu 952 ("Make machine group active" 3711 / Cancel) exists; 3711 -> `MoveInsertItem(-grpId,..)`.
* **Template selection** (resource in opmanres.dll; `GetProdEnableBits()`, insert-arrow group =
  `this+0x37b0`, its machine type byte `op_group+0x19e8`: 0 mill, 1 lathe, 2 wire, 5 router, 6 mill-turn) [code]:
  - wire product bit (0x10): Agie machine -> MENU 1385, else MENU 780.
  - else if no lathe/MT bits (0x120): router level==4 or mill level==4 or bit 0x100 -> MENU 911
    ("One surface ..." entry-level surface menus, uses `*_1_toolbar` FT variants), else MENU 780.
  - else (lathe/MT): arrow group type 6 -> MENU 1119 (Milling/Turning/Part Handling); otherwise 780
    (or 911 if a mill/router level-4 sim). 
  - Then `GetSubMenu(menu,0)`; the Undo popup is position 13.

### Top-level positions of MENU 780 (0-based)
0 Mill toolpaths, 1 Lathe toolpaths, 2 Wire toolpaths, 3 Router toolpaths, 4 Edit selected operations,
6 Groups, 8 Cut, 9 Copy, 10 Paste, 11 Delete, 12 Undelete, 13 Undo, 15 Expand, 16 Collapse, 17 Doc file,
18 Select, 19 Sort, 21 Import, 22 Export, 23 Batch, 25 Display options, 26 Collision check,
27 Setup sheet, 28 Tool list, 30 Lathe stock preview, 31 Lathe stock update, 33.. debug items.

## 2. Enable / grey / hide rules (all in OnRclick, [code])

Notation: "ops" = op count in tree (`(this+0x72d8-0x72d0)/0x18`), "sel" = `COperationsArray::GetSelectedCount`.

| Rule | Effect |
|---|---|
| always | appends item **3579** "Toolpath Editor" (string 4034; "Wirepath Editor" 4040 for wire ops) at the end, greyed; enabled iff target op exists and is mill/lathe/wire/multiaxis and opcode not 0x11 (manual entry) / 0x4d |
| target op is wire | grey 3580 Renumber tools, 3582 Reverse, 3583 Recalc |
| target op opcode 0x8b (Stock Model) | enables 4184 View/Compare stock model, 33451 Export STL, 33452 Convert to PMesh; otherwise these are greyed |
| insert info `this+0x37b4`==0 (no arrow group) | grey 3600 New Toolpath group; delete Lathe stock preview/update items |
| no group clicked, or clicked item disabled | grey 3585 Rename, 3586 Delete (group), 3600 New Toolpath group |
| defaults-file mode (`IsDefaultsFile`) | grey 3573 Batch, 3584 Setup sheet, 4199 Tool list, 3575 Collision check |
| ops<1 or sel<1 | grey popup 4 (Edit selected operations), 3563 Cut, 3564 Copy, 3566 Delete, 3584, 4199, 3575 |
| ops<1 | additionally grey 3568 Expand, 3569 Collapse, 3570 Doc file, 3611 Sort, 3612 Select, 3572 Export |
| copy buffer empty AND cut buffer empty | grey 3565 Paste |
| `!UnsortOK()` | grey 3615 Unsort (true only right after a Sort, counts unchanged) |
| `!nesting_enabled` | grey 3631 Nest |
| `!OkayToRunFbm(1)` / `(0)` | grey 3728 FBM Drill / 3730 FBM Mill |
| `!OkayToRunStockModel()` | grey then **delete** 4183/4184/33451/33452 |
| `!OkayToRunCollisionCheck()` | delete 3575 |
| sel>0 and any selected op in a locked group (`group+0x1ab8`) | grey 3563 Cut, 3566 Delete, 3611 Sort |
| insert arrow in locked group | grey 3571 Import |
| **machine type of arrow group** = 0 Mill | grey popups 1 Lathe, 2 Wire, 3 Router |
| = 1 Lathe | grey 2 Wire, 3 Router; 0 Mill toolpaths enabled only if (no machine or ctrl type 4 = mill-turn capable lathe) and mill product level ok; Lathe submenu pos 16 (C Axis) enabled only if ctrl type 4 and lathe level ok; stock-model rules repeated inside Lathe submenu |
| = 2 Wire | grey 0 Mill, 1 Lathe, 3 Router, and 3580, 3582, 3583, 3575 |
| = 5 Router | grey 0 Mill, 1 Lathe, 2 Wire |
| = 6 Mill-Turn (MENU 1119) | Part Handling submenu: Pickoff/Cutoff, Pickoff, Stock Pull, Tailstock enabled from group machine flags (+0x1938/+0x18a0/+0x194d/+0x18a1/+0x16cc) |
| arrow group type 1 or 6 | Lathe stock preview (3622) + update popup: greyed if no lathe stock components defined; update popup (3623/3624) enabled only if sel>0. Other types: items deleted |
| Undo | popup 13 greyed if no backup snapshots for the target op; else placeholder 33279 removed and one item per snapshot appended (newest first), label `"%2ld - %s"` (index, CTime `"%a %b %d %H:%M:%S %Y"`), ids `maxMenuId+10+i` |
| not (Ctrl and Shift held) | delete debug items 3601, 33543, 3613, 3614, 3712, 3713, 33481, 33515, 33546, 33608 |
| always | delete 33601 (toolpath statistics) |
| Ctrl held | append 3675 "Solid Drill" (FT `solid_drill_toolpath_dialog`) |

Note: the lathe-stock code greys/deletes by position 29/30 (0x1d/0x1e); in the shipped MENU 780 these are
the separator before and "Lathe stock preview", so the by-position calls are off-by-one relative
to the resource (the by-command calls on 3622 do the real work). [code+inferred]

## 3. Toolpath creation commands (Mill/Lathe/Wire/Router submenus)

Every returned id `< maxMenuId+10` is first passed to `COpMgrTree::CreateNewOperation(isMenu911, id, outIds)`
@1803532a0 [code]:
1. `IsDefaultsFile` -> instead of FT, `AddDefaultOperation(opcode, cur_op, variant)` (edits the default-ops file).
2. Remember next op id; for all toolpath ids: `DeleteBufferedOperations(3)` (commit cut+delete buffers => no Undelete
   afterwards) and `ForceArrowIntoToolpathGroup` (if arrow sits in a machine group, a toolpath group is created; msg 160).
3. Posts FT command `("Mastercam", "<name>")` (full table: `decomp/opman_create_map.tsv`), e.g. Contour 3577 `ctour_assoc`
   op 1; Drill 3625 `drill_assoc` 2; Pocket 3626 `pock_assoc` 3; Face 3627 `face_assoc` 0x66; Engrave 3628 module
   MWENGRAVE `m_main`; Dynamic Mill 4288 `hmm_2d_DynamicArea` 0x86 (Dynamic Contour/Area/Peel/Blend 4290/4292/4294/4295
   `hmm_2d_DynamicContour/AreaMill/PeelMill/BlendMill`, all opcode 0x86); Circle mill 3588 `circmill` 0x12; Thread mill
   3666 `thread_mill` 0x64; Auto drill 3589 `pro_drill`; Drill start holes 3667 `start_hole_dialog`; Slot 3590
   `slotmill` 0x69; Helix bore 3591 `helix_bore` 0x6a; Transform 3630 `xform_toolpath_dialog` 4; Nest 3631 `do_nest_t`
   0x6c; Manual entry 3632 `manual_entry_toolpath_dialog` 0x11; Point 3633 `point_toolpath_dialog` 0x13; Trim 3634
   `trim_toolpath_dialog` 0x67; Probe 33462 `probe_interface` 0x96; surface rough/finish `msurf_*_toolbar`
   (MENU 911 uses `msurf_*_1_toolbar`); HS surface `msurf_hmm_*_toolbar` 0x84; lathe `lathe_*`; wire `wctour_assoc`
   etc.; multiaxis `*Entry`; Stock Model 4183 `stock_model_dialog` 0x8b; 4184 `stock_model_compare_dialog`;
   33451 `stock_model_export_dialog`; 33452 `stock_model_convert_pmesh_dialog`.
4. Afterwards all ops with id >= the remembered next id are selected and made visible; returns the opcode.

**Mill toolpaths > New (3576)** = `COpMgrToolBar::OnButtonPress(33350)` -> FT `OMdeleteAllOperationsAndTools`
@18034ece0 [code]: if there are no ops/groups/tools, nothing; else Yes/No "Permanently delete all operations and
tools?" (core 7617, title 7616 "Toolpath Manager"); Yes -> `DeleteBufferedOperations(3)`, `toolpath_init(...)`
(clears all ops, groups, tools), re-init tree, create a fresh machine group of the current product + one toolpath
group (`OMcreateGroup`), `LoadTree`. It is "start over", not "new operation".

## 4. Per-entry actions

### Edit selected operations (popup greyed unless sel>0)
* **3578 Edit common parameters** [code OnRclick, RunEditCommonParams@180144bc0]: arrow moved after first selected
  op; wire/lathe/mill variant by product. Abort silently if a selected op is being processed. OK if any selected op
  is in the active machine group; else if all are in one machine group that group becomes active; else msg 3797
  "None of the selected operations are a part of the active machine group…". Dialog CEditCommonDlg (IDD 243):
  tool #, head #, feed/plunge, len/dia offset, spindle speed/dir, clearance/retract/feed plane/top of stock
  (abs/inc), Coolant/Canned text/Misc values/Home pos/Planes/Tool display/Ref point/NC name. IDOK -> RefreshAll.
  Decoded 2026-10-15 (mastercam.dll; vftable 1805cf688, message map 1805d0090): OnInitDialog FUN_180143a10 copies
  the selected ops (lathe / transform ops dropped; ops of another machine group dropped with 3790 / 3791), then zeroes
  cur_op (memset 0x28d0, views = active planes, machine group) - so every value starts at 0 ("0.0" / "0") and every
  check box off (FUN_180142f40). Combos (FUN_180141f80): Use = No change / Enable / Disable (3775, 3799, 3800); When =
  No change / Always use Clearance / Clearance at Start and End (3778, 3779); Abs/Inc = No change / Absolute /
  Incremental (3776, 3777); Rapid Retract = Rapid retract / Retract rate (3780, 3781); Force Tool Change = On / Off
  (3782, 3783); Batch = To batch / No batch (3784, 3785); Spindle direction = CW / CCW (3804, 3806); the last four
  start with no selection. OK FUN_180143d70 / Apply FUN_180143180: FUN_180140c00 (an enabled field empty or an enabled
  choice blank -> 3786 "A value must be specified..." / 3787 "Missing Parameter", focus there), then FUN_180145b50
  per op; ops clamped to the machine definition rpm / feed limits (FUN_1801451d0) are counted -> 3788 / 3789.
  FUN_180145b50 writes only the enabled settings: tool # +0x2e0, head # +0x53c, feed +0x310, plunge +0x318, len.
  offset +0x30c, dia. offset +0x308, retract rate +0x320, spindle speed +0x328 (keeps the op's sign; direction
  CCW = negative); clearance +0x108 with Use -> +0x111 (Enable), When -> +0x2b1 (Start and End), Abs/Inc -> +0x110;
  retract +0x118 / +0x121 / +0x120 (drill ops, opcode 2: +0x128 / +0x130); feed plane +0x128 / +0x130 (not on
  op_is_drill ops: opcodes 2, 28, 55-57, 306); top of stock +0x280 / +0x288; depth +0x138 / +0x140; rapid retract
  +0x141 = (Rapid retract); force tool change +0x289 = (On); batch +0x291 = (To batch); "No change" leaves a flag;
  Coolant / Misc values / Tool display (+0x930) / Planes / Ref point / NC name copy the sub-dialog results. A changed
  op: NCI patched by bnci_update, else op dirty (+0x94); BackupOpsAdd; UpdateListAndDB. Propagate FUN_180143eb0:
  the op picked in the left list (COpMgrList) -> all values and combos (check boxes unchanged). Enable / Disable All
  = FUN_180143060 / FUN_180142f40. Implemented in viewer/cam_fileop.c cam_common_apply + mcamui.c draw_edit_common.
  Tool list 395 (N13-32, decoded 2026-11-14; AFX_MSGMAP 0x1805d0090 = {base, entries 0x1805cfa10}): a CMillToolListCtrl at
  dlg +0x34d8, set up by FUN_180142840 (OnInitDialog): SetListObject(the part's tools, GetInternalToolSystem vtable +0x1c8),
  word +0x3b81 = 0 (list +0x6a9 = 0 -> ResetListCtrlColumns hides column 11 Rad. Type; +0x6aa = 0 part list), InitMachGroup,
  InitColumns, InitBitmaps, SetViewMode(1) Details, LVM_SETCOLUMNWIDTH LVSCW_AUTOSIZE_USEHEADER on columns 1..4 (not ported:
  default widths), FillList(-1); the ctor's display mode 2, column 2 ascending. LVN_COLUMNCLICK from 395 -> 0x180143b10 =
  SortList(iSubItem) (mccore import) - the Tool Manager's toggle and SortColumn order. mastercam.dll imports no
  Read/WriteRegistrySettings: the sort is not kept (each Edit Common Parameters starts at # ascending). Its right-click menu
  (0x180143388: view modes) is not ported. Ours: cui_ecp.c `ecp_cs` / `ecp_tool_rows` / `ecp_sortlist` (steps `ecpsort:C`,
  `ecprows`; Qt `ecpdlgset:sort=C`, `tool=ROW`); the list was a one-line-per-tool text list before (ours).
* **Multi-selection** (COpMgrTree::Selchanged @18039be00, GetSelectedInfo reads Ctrl / Shift with GetAsyncKeyState):
  a click = SelectOperation(id, toggle, clear others = !Ctrl) - Ctrl+click toggles one op; Shift+click =
  SelectOperationRange(anchor +0x7240, id, select, clear others = !Ctrl). [code]
* **3704 Change NC file name** (`SetNewNciPath`@180366fe0/`PromptNewNciPath`): selected ops with NCI; dialog
  "Enter new NC name" prefilled with first op's NC base name; invalid names (path chars `\/:*?"<>|`) silently keep old;
  writes `build_filename(name, NC dir, suffix)` into op+0x170 of every eligible op; no dirty. [code]
* **3714 Change Program #** (`SetNewProgramNumber`@180367270): int dialog defaulting to first op's program # (op+0xf8);
  set on each selected op with NCI, NCI patched with `bnci_update`, op dirty only if patch fails. [code]
* **3580 Renumber tools** (CRenumbTl, IDD 805): full specification in formats/renumber_tools.md (decoded
  2026-10-15 from FUN_1804572a0 / FUN_1804574e0, supersedes the earlier summary). Start / Increment / offset boxes are
  session statics (1 / 1 / on / on); "also renumber unused tools" always opens on; amounts = the control's mill tool
  registers (LOfsAmt / DOfsAmt, page 9 ent +0x29e +4 / +6 - formats/mcam.md) and the head box = UseHeadForTool (+0xc).
  Scope = selected ops + ops sharing their tool slots (becomes the selection); ops in tree order per root machine
  group, new slot -> counter, += increment; offsets = amount + number when checked; written through the NCI tool
  change records (ops without 1016 + 20004 records before the TC keep their numbers); unused tools of the group
  follow; no duplicate check, no messages, no undo; RefreshAll even on Cancel. [code]
* **3581 Renumber work offsets** (CRenWoff, IDD 808): Start 0, Increment 1, Match existing (on), Overwrite op-defined
  (off), Number per operation (off). Selected ops with a view; builds/matches work-offset map by plane matrix+origin;
  writes op+0x602 (and plane copies), patches NCI 1016 records; no dirty. [code FUN_18045a5a0]
* **3582 Reverse toolpath** (`ReversePath(1)`@180366890 -> mccore `bnci_reverse`): selected non-multiaxis ops (subprogram
  ops refused, msg core 2911); reverses the **NCI** (each pass reversed in place - passes keep their order -, 1040/1041 swapped, arcs flipped; formats/reverse_toolpath.md), not chains;
  sets op+0xe0 (NCI edited), clears dirty, marks dependents dirty, refresh. [code]
* **3583 Recalculate feeds/speeds** (`RecalcSpeedsAndFeeds`@180365330 -> `list_op_recalc_sf`): needs a material on the
  active machine group; for each selected op recomputes spindle/feed/plunge/retract from material + tool (flutes,
  diameter); drill-type ops plunge=retract=feed; changed ops marked dirty. No prompt. [code]

### Groups
* **3618/3619/3620/3621 New Machine group Mill/Lathe/Router/Wire** (types 0/1/5/2): `CreateGroup(type, machine)` creates
  root group (parent 0) at end of roots, loads default machine definition (`ReplaceTpGroupMachineDef`, external),
  `CreateNewMachine`, opens "Machine Group Properties"; then a child toolpath group (no rename). Arrow moves into the new
  group. Lathe also `SetWCSToLatheDefault`. [code]
* **3600 New Toolpath group** (needs a clicked group): child of the **clicked group** (nested if a toolpath group was
  clicked), appended as its last child, machine type of the root; arrow moves into it; in-place rename starts immediately.
  Name: naming rules (`OpMgrFileNamingRules`: base (file name / machine comment / custom) + separator + n, n stepping
  until unique, ':'->'-'), fallback `"Toolpath Group %d"`-style first unused. [code CreateGroup@1803949e0, InitFileName,
  CreateGroupName@180395a70]
* **3585 Rename**: `TVM_EDITLABEL` on the clicked group. Ops and groups are renamable (op label = comment, ≤120 chars).
  Group rules: text cut at first ':', ≤259 chars; empty -> revert; "Undefined" refused (msg 4159); duplicate refused
  (msg 4161 "Group already exists with the same name"). [code OnEndlabeledit@180361440, TpGrpList::CheckGroupName]
  **Operation rename** (decoded 2026-11-19, branch op-rename; core_ui cui_misc.c op_rename_begin / op_set_comment): menu 3585 is
  greyed unless a *group* item was right-clicked (local_9b98 is set only for lParam -1, opman_tree_all.c l.20981-20994 /
  l.21183-21187), and Mastercam sends TVM_EDITLABEL only from 3585 and CreateGroup - an op's edit is started by the tree control
  itself: the Toolpaths tree (opmanres dialog 145, control 197) has style 0x5091082f = TVS_EDITLABELS, so a click on the label of
  the already-selected item starts it (after the double-click time; the Windows tree control's rule). F2 / a keymap entry: NOT DECODED.
  OnBeginlabeledit@1803609f0: refused for a disabled item / list-only manager / Ctrl or Shift held / lParam < -1; for an op
  (lParam = op id > 0) the edit text = the op's **comment** (op +6), not the row text. OnEndlabeledit@180361440, op item: Esc
  (no text) = nothing; else DatabaseRetrieve, `strcpy0(record + 0x6e, text, 0x78)` (field cleared, at most 119 bytes, no trim,
  no refused text, **empty text clears the comment**), `UpdateListAndDB(rec, false, true)` (DatabaseUpdate@180907520: no time
  stamp, rewrite_ent -> db_has_changed = part modified; the op is **not** marked dirty, its toolpath / NCI stay), RefreshOperation
  (InitTvItem -> InitTvOpText: the row = "N - type - [WCS] - [Tplane]" + " - comment" when not empty), ReallySelectItem; no
  message, no undo. The comment is the op's +6 field the post / Doc file / saved part read (decision L-5).
  The same slow click on a **group** row edits its name (lParam -1: the item text; OnEndlabeledit's group rules above). Text is ANSI
  (Windows-1252, the `_mbsnbcpy_s` byte cut); the Qt front end converts (viewer-qt/src/cp1252.h). Ported 2026-11-19 (user Q1-Q3).
* **3586 Delete (group)**: `DeleteGroup(selectedGroup)`@180395fd0: commits undelete buffer; refuses deleting the last
  machine group / last group (msg 3824); group with ops -> msg 3823 "You cannot delete groups that contain operations…"
  (in one branch instead Yes/No 28049 "Delete group and all operations in the group?" which deletes ops permanently;
  branch condition uncertain); refuses if a direct child group has ops; arrow moved to parent / neighbouring machine group;
  deletes group and direct children. Tools/NCI untouched. [code]
* **3616 Delete all empty**: no prompt; deletes every group with no ops and no child groups (machine groups included;
  single pass, so a machine group whose only child is an empty toolpath group survives one run). [code DeleteEmptyGroups@180395db0]

### Clipboard / delete
* **3563 Cut** (`CutOperations`@180358730): selected ops + pinch-turn partners; op structs moved to cut buffer and
  removed from list+DB immediately (ids kept); tree items removed. Previous cut buffer purged. [code]
* **3564 Copy** (`CopySelected`): stores only references (id/name) of selected ops (+ pinch partner); commits any pending
  cut first; nothing duplicated yet. [code]
* **3565 Paste** (`PasteOperations`@180380890): forces arrow into a toolpath group; inserts after the op above the insert
  arrow (or top of arrow group). Cut buffer wins if both exist: cut ops re-added with **same ids**, moved to target
  group (tools copied into target machine's tool list if needed), cut buffer cleared (paste once). Copy: `list_op_copy`
  creates new ids, pinch links reset, copy buffer kept (repeatable). Then arrow after last pasted op, pasted ops selected. [code]
* **3566 Delete** (`OnDeleteKey`@180360de0): locked-group ops skipped; optional confirm (core 8979, toggled in Display
  options); purges previous delete buffer (only last delete undoable); if a group item highlighted and empty -> delete
  group; else `DeleteSelectedOperations` (with pinch partners; FBM drill with children refused, msg 4041); ops go to the
  delete buffer (soft delete in DB; dependents marked for regen). Final cleanup (toolpath entities, NCI) at purge time. [code]
* **3567 Undelete** (`UndeleteOperations`@1803682b0): restores **all** ops of the last delete at their original indices,
  reloads tree, selects them. Note: any toolpath creation, group delete, Copy/Cut commits the buffer. [code]
* **Undo > Time #n**: per-op parameter snapshots (`BackupOpsAdd`, taken when an op's parameters are edited, e.g. lathe
  edit). Choosing one opens the op's parameter dialog (`edit_operation`/`edit_lathe_operation`) loaded with that snapshot;
  on OK, the current state is pushed as a new snapshot, the snapshot params are written to the DB (`UpdateListAndDB`) and
  the op marked dirty; Cancel restores. [code OnRclick]

### View / misc
* **3568 Expand / 3569 Collapse**: scope = right-clicked item (or nearest op/group ancestor) and the following items at
  deeper level (its subtree); if a group was clicked only group items toggle, if an op was clicked only op items. [code ExpandItems@180378fb0]
* **3570 Doc file**: writes a text dump of the tree (file name + timestamp header, item texts indented by depth) and opens it. [code DocFile]
* **3612 Select...**: `COpSelect` dialog (IDD 0x32d); selects matching ops. [code]
* **3611 Sort...**: snapshots ops/groups then `COpSort` dialog (IDD 0x329, 8 sort keys asc/desc, e.g. NC file name);
  arrow kept after same op. **3615 Unsort**: restores the snapshot order (only right after Sort). [code]
* **3571 Import...** (`COpImport`, IDD 0x327): import ops from op library/file, select imported. **3572 Export...**
  (`COpExport`, IDD 0x326). [code]
* **3573 Batch...**: saves current file then `BatchDialog`. [code]
* **3574 Display options...**: `COpMgrDisplayDlg` (IDD 0x335) edits tree display settings (incl. delete confirmation). [code]
* **3575 Collision check / 3584 Setup sheet / 4199 Tool list**: need sel>0 (else msg 3964 "No operations were selected");
  FT `collision_check` / `setup_sheet` / `RunToolReportForSelectedOps()`. [code]
  Tool list decoded 2026-10-15 (mccore): selected ops with a tool (op_is_notool skipped), one entry per tool slot
  (+0x544) in op order, cloned with the op's tool number; machine group product 2 (lathe) bails. Report =
  GetToolMatlRpx(true, router ? 5 : 0, 3) = config 31017 TOOL LIST (ROUTER-OPMGR) / 31011 TOOL LIST (MILL-OPMGR) as
  "<31020 reports folder>MSC\<name>.rpx". LoadRpxInfo (FUN_1806395c0) scans the template (SortTools field: Yes /
  Ascending = 1, Descending = 2, else 0; per-kind codes 10000-40000); FUN_180638750 = 6 for multi-kind templates ->
  FUN_180616d00 / FUN_18061bdc0 (TOOLS root, TOOL / TOOL-R|TOOL-M), else FUN_1806168b0 / WriteXML_MillToolList
  (fields on TOOL). Root: LIBNAME core 5028 "Operations Manager", FILTER (AddNode_YN, core 5359 / 5360 upper-cased),
  AddNode_DATE (GetDateFormat long / short, GetTimeFormat no seconds, %Y), SORT (core 5360 / 10169 / 10170).
  Per tool (FUN_180649320): NUMBER "%ld", COMMENT = tool struct +0x56 (trimmed; empty -> left out), DIAMETER / RADIUS /
  CORNER-RADIUS by make_rstr(v, n_places_an = config 29003), DIAMETER-OFFSET / LENGTH-OFFSET "%ld", ASSY-NAME /
  HOLDER-NAME from the TlAssembly / holder objects. Shown by RunActiveReportsViewer(UsingRegistry). [code]
* **3579 Toolpath Editor**: drill ops / opcode 0x4b -> `tp_pt_mgr` (drill point manager); else FT `DoTPEdit` with op id. [code]
* **3622 Lathe stock preview** -> `LatheStockView(0)`; **3623/3624** set/clear op+0xb3c "update stock" on selected lathe
  ops and redraw lathe boundaries. [code]
* Debug (Ctrl+Shift): 3601 binary NCI dump (asks to include dead sections), 33543 same w/o line #, 3613 assoc dump,
  3614 mtable dump, 3712 malloc dump, 3713 aPoint dump, 33481 tool diag dump, 33515 Compare NCI, 33546 work-offset map,
  33608 SOS/EOS pairing check. [code]

## 5. Insert arrow / locks (shared state) [code]
* `OMinsertInfo` at tree+0x37a8: +8 machine-group id (0x37b0), +0xc toolpath group of arrow (0x37b4), +0x10 op just
  above the arrow (0x37b8; 0 = top of group).
* Lock = `op_group+0x1ab8`; locked groups refuse cut/delete/sort/import/drop.
* "Active machine group" = group holding the insert arrow (`UpdateSystemFromGroup`: active machine, toolbars, planes).

## 6. In mcamui (2026-10-14)
`viewer/mcamui.c` (cm_items / cm_leaf_ok / cm_usable) follows sections 2–4 where its model allows: Lathe / Wire / Router
popups greyed by machine type (Router = the mill list, live on a router machine); Stock Model items and Collision check
removed (they cannot run); Toolpath Editor appended (greyed); Solid Drill only with Ctrl; ops < 1 / no selection rules;
Paste needs a buffer; group items need a right-clicked group. Commands: Cut (paste once, same op) / Copy (paste
repeatedly) / Paste, both committing the delete buffer; Delete / Undelete; Doc file (2026-10-16: DocFile / DumpItemArray / CStringArrayToFile as decoded - "<part name> Operations
Manager.txt" in $TMPDIR (else /tmp), header = part path + "    " + "%A, %B %d, %Y   %H:%M:%S", then " ", then every
tree item indented 4 spaces per depth, opened with xdg-open outside scripted runs; item texts formats/mcamui_dialogs.md
"Toolpaths manager texts"); Renumber tools
(Start / Increment, scope = selected op + ops sharing its tool, tools still used by part ops are copied); Rename group
(':' cut, ≤ 259, empty keeps, "UNDEFINED" refused (mccoreres 4159), duplicate refused (4161)); Delete group refusals
(opmanres 3823 / 3824). Script steps for tests: `menu:NAME`, `menu:Groups>NAME`, `group:N`, `ask:V1|V2`, `selop:N`.

## 7. Code: viewer/rpx.c / rpx.h (report templates for Tool list...)
Used by Tool list... (mcamui.c, after the section 4 decode: report data `RNode` tree TOOLS / TOOL with the fields
above, template config 31017 / 31011 from `<reports folder>MSC\`; missing file -> mccore text "Warning - report file
does not exists!") and by the Tool Manager's reports (toolmgr_ui.inc). Mastercam shows the report in its ActiveReports
viewer; this program lays the template out itself.

| function / type | behaviour |
|---|---|
| `RNode`, `rnode_new`, `rnode_free` | report data as an XML-like element tree (name, text, children) |
| `rpx_layout(rpx, rec, nrec, out)` | reads the `<ActiveReportsLayout>` template (cached, up to 32), lays out its Detail sections once per record: Field = the record's child named by DataField (its Text when none), Label = Caption, Line, Subreport = its own report repeated over the record's children named by its DataField, growing (CanGrow, default on) and pushing the controls below down; KeepTogether sections move to the next page when they do not fit; output `RItem`s in twips (text or line, page, style: size, bold, italic, alignment) |
| `rpx_binds(rpx, field)` | 1 when the report or a subreport binds a subreport to `field` (multi-kind templates: TOOL-M / -L / -B / -R) |
| `rpx_layout_free` | frees the items |

Rules are ActiveReports 3's as stated in the rpx.c header; page = Letter 8.5 x 11 in with 1 in margins (PageSettings
empty). Only what Mastercam's shipped templates use is read (Detail sections; Label / Field / Line / Subreport). Not
measured against the real viewer; NOT DECODED: ASSY-NAME / HOLDER-NAME values, the viewer window (title, toolbar,
zoom); one UI font is used for every report font (twips -> px at 96 dpi, 1/15).
