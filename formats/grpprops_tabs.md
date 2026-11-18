# Machine Group Properties: Files, Tool Settings, Safety Zone (decoded 2026-10-22)

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

## Status: PORTED 2026-10-23 (Qt + core_ui)

- **Safety Zone removed** (cui_stock.c ST_TABS = Files / Tool Settings / Stock Setup; cui_optree.c Doc file and viewer-qt
  managers.cpp tree: three Properties children; `stocksetup:3` opens Stock Setup). The group bytes +0x19a0..+0x19e8 are
  never written (the save patches single fields).
- **Loader** (viewer/mcam_load.c resolve_machine, mcam_load.h): active group +0x1170 -> machine record (+0x34) -> its
  +0xe94 -> control (sub 23 +0x34) -> pages (sub 24, ent id = control + 1 + page, +0x68 = page). matlib / toollib /
  mach_type / ctl_lofs / ctl_dofs / ctl_head follow the chain; with no group / no match the first machine record / first
  page 9 (ours, the old rule). New fields: mach_comment / ctrl / post / minarc, ctl_ncpath / oplib / opdef / seq / ncout /
  arcfeed, grp_name / mdef / comment / prog / ovr / tcfg / grp_names. Effect per part: only the parts with two machines
  change, and only toollib[1] (metric): BRACKET4, bracket4-dashed, NAKTO-BRAKE-BRACKET, tramming-rod, thread-test
  `%MCAMDIR%Mill\Tools\Mill_mm.tooldb` -> `%MCAMDIR%mill\Tools\user-tools.TOOLDB` (the active "3 - AXIS VMC" machine; that
  file does not exist in the shared folder - test-cube-10mm already names it). matlib, mach_type, ctl_lofs / dofs / head are
  equal in both machines / controls of every part. Post 0/16, fileop 0/189 unchanged.
- **Files** (core_ui/cui_grpprops.c GP_815, every template control at its position): group name (':' -> '-', empty ->
  "Invalid group name", CheckGroupName: blank -> kept, "Undefined" 4159, a name of another group 4161) and comment (1999,
  CRLF) editable; toolpath directory, description, from file, control, post, the three library paths and the four comment
  switches shown read-only (greyed: machine / control records); 1255 / 467 / 463 / 1258 / 1266 / 1260 / 1264 / 1257 / 1262 /
  635 greyed NOT PORTED, 751 greyed (MultiTaskingIsValid NOT DECODED).
- **Tool Settings** (GP_1589): Program #, Feed Calculation + user values (enabled for User defined), the override switches
  (FUN_1801eb2b0's 499 rules at OK; 501 / 502 / 504 enabled = 499), the four toolpath configuration switches editable;
  Adjust feed on arc, minimum arc feed, sequence start / increment shown read-only; material name read-only (the part's
  material list is not loaded); Material Edit / Select greyed NOT PORTED; 580 / 554 hidden. From material with material NONE /
  empty: Select is not ported (as cancelled), the radio stays (FUN_1801ebd40's UpdateData(TRUE) reads it back).
- **OK** (only the visited pages, MFC PSN_APPLY): the group record fields; `F.grp_dirty` -> mcam_saveops.c grp_patch
  writes them into the active group (strings only when changed): an untouched part saves byte-identically. Project files:
  GRPSET / GRPNAME / GRPCMT lines (ours). Side effects ported: new ops' record +0x160 / +0x164 / +0x168 = program # / (int)
  StartSeqNum / (int)SeqNumInc (OMsetOperationToMachine; written with the new op's record), the override-with-modal-values
  copy into a new op (InitOperation; cui_ops.c heights_from_default_op -> grp_modal_record), sequential tool numbers
  (+0x11b8 -> part_add_tool: lowest unused number from 1).
- **Ours (2026-10-23, user decision P2):** a check box "Post with Mastercam post (.pst) (ours)" on the Files page (id 0x7a01, the
  free right half of the Edit / Replace row; Mastercam has no such control - the post is a read-only label, 604): Post then runs
  the machine's .pst through viewer/mp instead of the LinuxCNC post (formats/mp_engine.md "Integration (app)"). Not a group
  record field (project line GRPMPPOST); dumps list it only when on.
- **Not wired (by request):** the post keeps N100 step 2 (nc_post.c); the stored increment is not read (test-cube-10mm stores
  10). No opt-in was added: it would need per-op values plus the wrap rule against +0x10 (NOT DECODED), not trivial.
- Scenes: tools/tests/stockplanes_scenes.txt `gp_*` (19). Tree / sheet entry: `tpmprops:K` (Qt: a click on Properties child K),
  `stpage:N`, `gpdump`, `stdlgset:tab=K`.

Scope: the tabs of EditGroupPropertiesDlg (mastercam.dll @1801e1f50, caption opmanres 3851 "Machine Group Properties")
that cui_stock.c does not port yet. Stock Setup (CGroupPg3, dialog 1590) is in formats/mcam.md "Stock Setup".
Decoded 2026-10-22 without code changes; ported 2026-10-23 (Status above). Every statement cites a decompile or measured data. Unknown = **NOT DECODED**.

New dumps (this session):
- `decomp/mastercam_grppg1_files.c` - CGroupPg1 (Files page), DumpRange 1801e28f0..1801e7220
- `decomp/mastercam_grppg2_toolset.c` - CGroupPg2 (Tool Settings page) + CGroupPg2Turning, DumpRange 1801eac00..1801ecb90
- `decomp/mastercam_grpsheet.c` - sheet CGroupPg0: ctor FUN_1801df8d0, group->pages FUN_1801e1590, SetMachinePtr
  FUN_1801e17e0, FUN_1801e1950 (SetActivePage), FUN_1801e1990, OMgetGrpMachine / OMsetGrpMachine
- `decomp/mastercam_grpsheet_helpers.c` - FUN_1801e1b40 (CGroupPg0::UpdateGroupAndDialogMachines), FUN_1801e1a20
- `decomp/mastercam_grppg12_msgmaps.txt` - vtables, both message maps (decoded from .rdata), disassembly of the tiny handlers
- `decomp/mastercam_safetyzone_editgroup.c` - RunningSafetyZone, COpMgrTree::EditGroup
- `decomp/mastercam_opsettomachine.c` - OMsetOperationToMachine (op program # / sequence numbers)
- `decomp/mccore_grpsettings.c` - InitGroup, JobSetupToMachineGroup, FUN_180346db0 (safety zone -> ops),
  TpGrpList::WriteFileGroups, default_ops_get, ...
- `decomp/mccontrol_ncoutput.c` - CCNCControl::AddNcoutput / GetNcoutput / AddNcPath (mccontrol.dll, ghidra_mdef)
- `decomp/mdef_grpprops_accessors.c` - CCNCMachine accessors (mcmachinedef.dll, ghidra_mdef)
Already present: `decomp/mastercam_grpprops.c` (EditGroupPropertiesDlg, CGroupPg0::OnOK FUN_1801e0a40),
`decomp/opman_tree_all.c` (tree items / drop handlers), `decomp/mccore_bnci_str.c`, `decomp/mccore_tooladd.c`,
`decomp/mccore_ops.c` (InitOperation).

## 0. Corrections to the task brief / existing notes

1. **For a mill group the Tool Settings page is template 0x635 = 1589 ("Group PG2 Dlg"), not 0x669.**
   EditGroupPropertiesDlg (decomp/mastercam_grpprops.c l.831-866) picks the templates by op_group +0x19e8:
   | +0x19e8 | page 1 | page 2 | page 3 | page 4 |
   |---|---|---|---|---|
   | 1 (lathe) | 0x358 | 0x359 | 0x35b | 0x637 (not built) |
   | 6 (mill-turn) | 0x1966 | 0x359 | 0x195b | 0x637 (not built) |
   | 2 (wire), Agie machine | 0x32f | 0x669 | 0x1958 | 0x1959 |
   | 2 (wire), other | 0x32f | 0x669 | 0x636 | 0x637 (not built) |
   | anything else (mill = 0; all user parts) | **0x32f = 815** | **0x635 = 1589** | **0x636 = 1590** | 0x637 (not built) |
   0x669 = 1641 "Wire Group PG2 Dlg" (same controls as 1589, wire layout; tab caption 3854 "Program Settings",
   FUN_1801df8d0 `0xf0e` when +0x19e8 == 2).
2. **Safety Zone does not exist for a mill group in Mastercam 2017.** Three independent proofs:
   - The sheet ctor FUN_1801df8d0 (mastercam_grpsheet.c l.27-290) adds page 4 only when DAT_1809a048c == 0x1959 (Agie
     wire "Piece Details", caption 0x1007 = 4103, class CGroupPg4Agie FUN_1801fa880). Template 0x637 is stored in
     DAT_1809a048c but no constructor ever uses it; there is no CGroupPg4 vtable in RTTI (VtableDump 'GroupP': only
     CGroupPg0/1/1MT/2/2Turning/3/3Lathe/3MT/4Agie).
   - The tree builds the 4th Properties child (-0x13) only for +0x19e8 == 2 with an Agie (or no) machine
     (opman_tree_all.c l.29239-29262); its "Safety zone" label and the drop handler DropOnGroupPropertiesSafetyZone are
     gated by `RunningSafetyZone()`, which is `return false;` (mastercam.dll @180442a40,
     decomp/mastercam_safetyzone_editgroup.c; callers opman_tree_all.c l.2995, l.6424).
   - So a mill Machine Group shows **Files / Tool settings / Stock setup** (tree items -0x10 / -0x11 / -0x12, captions
     3852 / 3853 / 3855) and the sheet has **three tabs**.
   **Consequence for our apps:** cui_optree.c l.56, mcamui.c (tree rows), viewer-qt managers.cpp l.176 and
   cui_stock.c ST_TABS (4 entries) show a "Safety zone" row / tab that Mastercam does not have -> remove them (rule
   "follow Mastercam over mcamui"), do not grey them.
3. The tab order in the sheet is the AddPage order: Files, Tool Settings, Stock Setup (FUN_1801df8d0). Initial page =
   EditGroup's page argument (tree child -0x10..-0x12 -> 0..2, opman_tree_all.c l.41730ff) via FUN_1801e1950 (index <= 3).

## 1. Common machinery (sheet CGroupPg0)

- EditGroupPropertiesDlg works on a **copy** of the group (0x1ae0 bytes at sheet +0xadf0, FUN_1801e1590); every page
  holds `+0x4d8` = that copy, `+0x4e0` = the group's CCNCMachine (OMgetGrpMachine, FUN_1801e17e0), `+0x4e8` = the sheet.
  The CCNCMachine is the **live machine object**: pages write control / machine fields into it directly.
- Page vtable slots (CGroupPg1 @1805eccf0, CGroupPg2 @1805ee608; same layout as CGroupPg3): 66 DoDataExchange, 95
  OnInitDialog, 97 OnOK, 102 OnSetActive, 103 OnKillActive, 114 (+0x390) group->page, 115 (+0x398) page->group,
  116 (+0x3a0) enable rules, 117 (+0x3a8) op-defaults/library lockout (decomp/mastercam_grppg12_msgmaps.txt).
- **CGroupPg0::OnOK** FUN_1801e0a40 (mastercam_grpprops.c l.331), run from each page's OnOK:
  1. CCNCMachine::SaveControlToDb / SaveParamsToDb / SaveGroupsToDb(1) / SaveComponentsToDb / DeleteUnusedEnts /
     SaveEntInds - the machine + control records in the part are rewritten (this is how Tool Settings' sequence
     numbers / arc feed and Files' comment switches / NC path reach the file).
  2. Group name: if the copy's +4 differs from the sheet's original name (+0xc8f0) and TpGrpList::CheckGroupName
     accepts it -> COpMgrTree::ChangeGroupNameInTree; a rejected name is replaced by the original.
  3. The copy is written back over the group, **except +0x110 (i32) and +0x118 (8 bytes)**, which are preserved.
  4. System file settings (CSystemProductFile::SetValue for toolLib, matlLib, opLib, opDflts, postProc, setupSheet,
     postExe, miscValsChook, ctrlDef, nciPrefix, ncPrefix; insert/holder catalogs for machine Type 2, wire aux chook
     for Type 5) are set from the machine - global "current file" settings, not part data.
  5. If the copy's +0x45a differs from the group's (_stricmp of sheet +0xb24a vs group +0x45a, l.404) and the group has operations (not mill-turn):
     every op of the group gets its NCI name (op +0x170 = record +0x1d8) rebuilt as `sprintf("%s%s%s", nciPrefix
     FullPath, ...)` (remaining sprintf arguments NOT DECODED - the decompiler dropped them), an empty op +0x170 first
     becomes "T"; TpPartOpList::UpdateListAndDB. Which page edits the copy's +0x45a: NOT DECODED (the Files page does
     not; see 2.3).
- After DoModal (EditGroupPropertiesDlg l.941-1001): deletes the hidden machine-def temp file, OMsetGrpMachine;
  mill: stock_ok(1) + the stock-changed regen (formats/mcam.md); LoadMachineTypeMenu if the machine def is not the
  hidden default; repaint_graphics, OMupdateGroupName, OMsetGroupToolbars; result = (DoModal == IDOK). These run on
  Cancel as well. On Cancel CGroupPg0::OnOK (the Save*ToDb calls) does not run, but values a page already pushed into the
  live CCNCMachine (OnKillActive of Files / Tool Settings, the library buttons) stay in memory - whether anything
  restores them is NOT DECODED; the group copy is discarded.

## 2. Files tab (CGroupPg1, template 0x32f = 815 "Group Page 1 Dlg", 339x366 DLU)

Constructor FUN_1801e28f0; DDX FUN_1801e3940; OnInitDialog FUN_1801e4410; load (PrmToDlg) FUN_1801e61b0;
dialog->group FUN_1801e3850; comment switches load / save FUN_1801e3240 / FUN_1801e3790; OnOK FUN_1801e5590;
OnKillActive FUN_1801e48d0; message map @1805ed080. Units index for every library path: page +0x550 = IsMetric() at
construction (0 inch, 1 metric).

### 2.1 Controls

| id | kind | label | rect (x,y,w,h DLU) | maps to | handler / rule |
|---|---|---|---|---|---|
| 705 / 292 | Static / Edit (UIEditString) | Group name | 14,6,60,8 / 82,4,220,12 | group +4 char[0x105] | EN_KILLFOCUS FUN_1801e4910: ':' -> '-'. OnOK check FUN_1801e2f70: empty -> warning 3857 "Invalid group name" (focus 292); OnOK continues anyway |
| 672 / 296 | Static / Edit | Toolpath directory | 14,22,60,8 / 82,20,220,12 | CCNCMachine::SetNCPrefix -> control NC path (control page 2 ent +0x94, measured) | load = NCPrefix(1); EN_KILLFOCUS FUN_1801e4990: kept only if non-empty and dir_exists(path,1), else the previous text (or NCPrefix); Enter in this edit runs the same check (PreTranslateMessage FUN_1801e6130) |
| 1255 | Button (bitmap 0x3a1) | (browse) | 307,20,16,14 | 296 | FUN_1801e5410 BrowseDirDialog, title string 7588 (mccoreres 7588 = "Toolpath output folder"; the resource module at that moment NOT DECODED); a non-existing pick keeps the old text |
| -1 / 25126 | Static / Edit (multiline) | Group comment | 14,36,60,8 / 82,36,220,28 | group +0x980 char[2000] | DDX_Text max 1999 (Ordinal_3528) |
| 673 | Group | Machine - Toolpath Copy | 14,71,310,87 | | |
| 684 / 467 | Static / Button (bitmap 0xd92) | Edit | 19,88,28,8 / 51,85,16,14 | | FUN_1801e5080: MachineDefinitionMgr(group, 0, sheet) with editingOpDefaults = 1; then UpdateGroupAndDialogMachines, Stock page +0x4f0 = 1, lathe component / WCS copies (Type 2), reload labels. Enabled only when the machine def is not the hidden default (FUN_1801e4ab0 tail) |
| 685 / 463 | Static / Button (bitmap 0x370) | Replace | 72,88,49,8 / 126,85,16,14 | group +0x355 (machine def path) | FUN_1801e4ab0: SelectMachineDefFile, ReplaceTpGroupMachineDef(..., m_TryToReplacePost = 1); failure restores machDefFile |
| 675 / 601 | Static / Static | Description | 17,103,50,8 / 72,103,245,8 | CCNCMachine::Comment = machine +0xe35 = machine record +0xdc5 | read-only |
| 676 / 607 | Static / Static | From file | 17,114,50,8 / 72,114,245,8 | file name of machDefFile = group +0x355 (char[0x105]); mill-turn product bit: +0x55f | PathFindFileName, truncateFileNameString(40); hidden default -> opmanres 3168 "System Default" |
| -1 | Static (etched line) | | 19,127,300,1 | | |
| 677 / 602 | Static / Static | Control | 17,133,50,8 / 72,133,245,8 | CCNCMachine::CtrlDefFile = machine +0x91c = record +0x8ac | file name, 40 chars; "System Default" as above |
| 686 / 604 | Static / CColorStaticST | Post | 17,144,50,8 / 72,144,245,8 | CCNCMachine::PostProcFile = machine +0xa21 = record +0x9b1 | FUN_1801e6720: FileNameIsNone -> text DAT_1805d5528 (NOT DECODED) in red bold |
| 678 | Group | Tool Library | 14,161,310,47 | | |
| 625 | Static (UILabel) | (path) | 30,177,220,22 | ToolLibFile(units) = machine +0xb26 inch / +0xc2b metric = record +0xab6 / +0xbbb | FUN_1801e3f20 (Type 5 wire: PowerLibFile) |
| 1258 | Button (bitmap 0x370) | (browse) | 256,180,16,14 | 625 | FUN_1801e5e90: file dialog FUN_1801e6520, title opmanres 1174 "Select Tool Library File", filter TlToolLibraryX::GetFilterString(mccoreres 2794 "Mastercam Tool Files") + 173 "All Files"; result copied into the machine's ToolLibFile buffer |
| 1266 | Button (bitmap 0xd92; lathe 0x37f) | (tool manager) | 275,180,16,14 | | FUN_1801e5c80: RunToolLibrariesMgr(1,1,sheet) (wire: edit_power_settings); hidden when MachTypeToCtrlType == 2 |
| 751 | Button (bitmap 0xbe0) | (load tools) | 294,180,16,14 | | FUN_1801e5e10: GetLoadToolsOptions -> LoadToolsUsedInOps -> ShowNumToolsLoadedOnMachine. Hidden unless MultiTaskingIsValid(); disabled when NumToolsUsedInOps == 0 |
| 680 | Group | Operation Library | 14,212,310,47 | | |
| 627 | Static (UILabel) | (path) | 30,228,220,22 | OpLibFile(units) = control page 2 ent +0x4a4 inch / +0x5a8 metric (file_ctrl +0x416 / +0x51a, measured) | FUN_1801e3ea0 |
| 1260 | Button (bitmap 0x370) | (browse) | 256,225,16,14 | 627 | FUN_1801e5890: title opmanres 1172 "Select Operation Library File", filter mccoreres 2798 "Mastercam Operation Files (*.mcam-operations;*.operations-9..-5;*.operations)" + 173 |
| 1264 | Button (bitmap 0xd92) | (edit) | 275,225,16,14 | | FUN_1801e5630: creates the file if missing (create_mc), CCNCMachine::SaveToDb, editingOpLibrary = 1, COpMgrDefaults dialog (template 0x330 = 816 "Operations Defaults Dlg"); OK with an existing file -> SetOpLibFile. Disabled when the open file itself is a defaults / operations file (FUN_1801e3b80) |
| 687 | Group | Operation Defaults | 14,263,310,47 | | |
| 623 | Static (UILabel) | (path) | 30,279,220,22 | OpDfltsFile(units) = control page 2 ent +0x6ac inch / +0x7b0 metric (file_ctrl +0x61e / +0x722, measured) | FUN_1801e3db0 |
| 1257 | Button (bitmap 0x370) | (browse) | 256,276,16,14 | 623 | FUN_1801e4240: title opmanres 1170 "Select Operation Defaults File", filter mccoreres 2800 "Mastercam Default Files (*.mcam-defaults;*.defaults-9..-5;*.defaults)" + 173 |
| 1262 | Button (bitmap 0xd92) | (edit) | 275,276,16,14 | | FUN_1801e3fe0: as 1264 with editingOpDefaults; OK -> SetOpDfltsFile. Disabled like 1264 |
| 683 | Group | Output comments to NC file | 14,315,310,46 | | |
| 466 | Check | Output operation comments to NC | 38,328,124,10 | control nc_output_ctrl +0x129 (control page 8 ent +0x1a9) | no handler (read at kill-active / OK, FUN_1801e3790) |
| 476 | Check | Output machine name to NC | 38,344,110,10 | nc_output_ctrl +0x12d (page 8 ent +0x1ad) | same |
| 468 | Check | Output group name to NC | 172,328,110,10 | nc_output_ctrl +0x12b (page 8 ent +0x1ab) | same |
| 465 | Check | Output group comments to NC | 172,342,124,10 | nc_output_ctrl +0x12a (page 8 ent +0x1aa) | same |
| 635 | Button (bitmap 0x37e) | | 307,52,6,6 | | FUN_1801e5b90: LatheToolManager, then SetMachinePtr / machDefFile refresh |

Not in the template but in the message map: 464 -> FUN_1801e3b80 (the 1262 / 1264 enable rule).
Editing op defaults / op library (editingOpDefaults / editingOpLibrary set) disables most controls (FUN_1801e3350
list) - not reachable from a part's tree, so not needed.

### 2.2 Data sources and where they live in the part file
- Group record (op_group, 0x1ae0 bytes, formats/mcam.md): +4 name, +0x355 machine def path, +0x55f mill-turn machine
  def path, +0x980 comment, **+0x1170 = the machine record's ent id** (CGroupPg0::UpdateGroupAndDialogMachines copies it
  with +0x978 = runtime CCNCMachine pointer; measured: +0x1170 equals the CNC_MACHINE_ID record's +0x34 in BRACKET4
  189 / 1054, tramming-rod 72 / 111, thread-test 37 / 83, NAKTO 337 / 666).
- Machine record (ASSOC 0x4000 sub 22, n 5000; record = CCNCMachine - 0x70, mcam_load.h): comment +0xdc5, ctrl def
  +0x8ac, post +0x9b1, matlib +0xcc0, tool libs +0xab6 / +0xbbb, Type +0x68, **control ent id +0xe94**
  (CCNCMachine::Ctrl_ent_idn = machine +0xf04, mdef_grpprops_accessors.c; measured equal to the CNC_CONTROL_ID
  (sub 23) record id in every part with two machines).
- Control: CNC_CONTROL_ID (sub 23, name at +0x78: "Fanuc", "Generic Default") followed by its pages (sub 24, page
  no. at +0x68); measured: page p has ent id = control id + 1 + p (the stored page -> control link field: NOT
  DECODED). Page 2 = files (NC path ent +0x94, NCI path +0x198, op libraries, op defaults, tool libraries at a
  0x104 stride - file_ctrl at ent +0x8e by the accessor offsets of CCNCMachine::NCPrefix / NCIPrefix / OpLibFile /
  OpDfltsFile; only verified by matching strings); page 8 = nc_output_ctrl at ent +0x80 (CCNCControl::AddNcoutput,
  mccontrol_ncoutput.c); page 9 = NC path (AddNcPath, control +0x238 -> ent +0x80).
- **Loader caveat (viewer/mcam_load.c):** `matlib` / `toollib` / `mach_type` come from the first machine record and
  `ctl_*` from the first page 9 in the file. In tramming-rod and thread-test the first machine / control belong to
  Machine Group-1 ("Mill Default MM" / "Generic Default", seq. increment 10) while the active (last) group uses the
  second ("3 - AXIS VMC" / "Fanuc", increment 2). The correct chain is active group +0x1170 -> machine record whose
  +0x34 matches -> its +0xe94 -> control record -> pages id + 1 + p.

### 2.3 OnOK / dirty / NC effect
- dialog->group (FUN_1801e3850): +4 name, +0x980 comment, SetNCPrefix(296), the four comment switches -> SetCtrlNCOutput.
  The Files page never writes group +0x45a (a path, measured "C:\MCAMX\MILL\NCI\" / "...\my mcamx9\Mill\NCI\"), so the
  NCI-rename branch of CGroupPg0::OnOK (section 1, step 5) is not triggered from this page; the toolpath directory
  edit changes only the control's NC path.
- Then CGroupPg0::OnOK (section 1). The library browse / edit buttons change the machine object immediately.
- **Posted NC:** the four "Output ... to NC" switches change post output in Mastercam (they are the control's NC output
  options passed with the NCI; how a .pst uses them: NOT DECODED). Measured in all 17 user parts: +0x129 = 1 (op
  comments on), +0x12a / +0x12b / +0x12d = 0. Our LinuxCNC post (viewer/nc_post.c) always prints the op comment and
  never the machine / group name or group comment - it matches the stored values, but it does not read them. If they
  are wired to nc_post.c the default output stays identical **only while the stored values are those defaults**; keep
  nc_post.c as is unless the user asks.
- Group name / comment / toolpath directory: no G-code change in our post (the header prints the file name, not the
  group name; output folder is chosen by the user).

## 3. Tool Settings tab (CGroupPg2, template 0x635 = 1589 "Group PG2 Dlg", 338x366 DLU)

Constructor FUN_1801eac00; DDX FUN_1801eb5e0; OnInitDialog FUN_1801ebe30; group->page FUN_1801ec2d0 (slot 114);
page->group FUN_1801eb2b0 (slot 115); enable rules FUN_1801eb790 (116); defaults/library lockout FUN_1801eb130 (117);
control values -> page FUN_1801eb070; page -> control FUN_1801eb210; OnSetActive FUN_1801ec1f0 (re-reads min arc feed +
the control values); OnKillActive FUN_1801ebe60 (writes min arc feed + control values); OnOK FUN_1801ec1b0
(page->group, then CGroupPg0::OnOK); message map @1805ee9c0. All decompiles: decomp/mastercam_grppg2_toolset.c.

### 3.1 Controls

| id | kind | label | rect (DLU) | maps to | type / range | rule |
|---|---|---|---|---|---|---|
| 688 / 293 | Static / Edit (MCLong) | Program # | 24,26,56,8 / 84,24,58,12 | group +0x1178 | i32, MCLong::Init(v, 0, 0x7fffffff) | |
| -1 | Group | Feed Calculation | 24,44,144,190 | | | |
| 469 | Radio (group start, DDX_Radio) | From tool | 32,60,62,10 | group +0x119c = 0 | i32 0..3 in radio order | click -> enable rules |
| 470 | Radio | From material | 32,74,64,10 | +0x119c = 1 | | FUN_1801ebd40: material "NONE" or empty -> Select material (479); still not in the part's material list -> back to 0 |
| 471 | Radio | From defaults | 32,90,64,10 | +0x119c = 2 | | enable rules |
| 475 | Radio | User defined | 32,106,64,10 | +0x119c = 3 | | enable rules |
| 695 / 312 | Static / Edit (MCReal) | Spindle speed | 48,123,64,8 / 114,120,40,12 | group +0x11b0 | f64 | enabled (label 695 too) only for 3 |
| 694 / 311 | Static / Edit (MCReal) | Feed rate | 48,141,64,8 / 114,138,40,12 | group +0x11a0 | f64 | label 694 + edit enabled only for 3 |
| 700 / 314 | Static / Edit (MCReal) | Retract rate | 48,159,64,8 / 114,156,40,12 | group +0x1188 | f64 | edit enabled only for 3 (label not toggled) |
| 702 / 316 | Static / Edit (MCReal) | Plunge rate | 48,177,64,8 / 114,174,40,12 | group +0x1180 | f64 | edit enabled only for 3 (label not toggled) |
| 473 | Check | Adjust feed on arc move | 30,198,114,10 | **control** NC path feed ctrl +0xd (CtrlFeedInfo(1) = GetFeedMT; mill slot control +0xbe0 = page 9 ent +0xa35) | u8 | load AdjustArcFeedrates(1); save SetCtrlFeedInfo(fi, 2) |
| 692 / 310 | Static / Edit (MCReal) | Minimum arc feed | 46,214,64,8 / 112,212,40,12 | **machine** Dynamics +0x30 (inch) / +0x34 (metric, by IsMetric) = machine +0x168 / +0x16c = record +0xf8 / +0xfc | f32 stored, Init(v, 0, FLT_MAX) | edit enabled = 473 checked |
| -1 | Group | Advanced options | 176,136,142,78 | | | |
| 499 | Check | Override defaults with modal values | 186,152,130,10 | group +0x115c | i32 | none of 501/502/504 checked at save -> 499 cleared; 499 cleared -> all three cleared (FUN_1801eb2b0) |
| 501 | Check | Clearance height | 196,167,84,10 | group +0x1160 | i32 | enabled = 499 |
| 502 | Check | Retract height | 196,182,84,10 | group +0x1164 | i32 | enabled = 499 |
| 504 | Check | Feed plane | 196,198,84,10 | group +0x1168 | i32 | enabled = 499 |
| 1213 | Group | Toolpath Configuration | 176,44,142,83 | | | |
| 1239 | Check | Assign tool numbers sequentially | 186,58,122,10 | group +0x11b8 | i32 | |
| 1384 | Check | Warn of duplicate tool numbers | 186,74,122,10 | group +0x11bc | i32 | |
| 1290 | Check | Use tool's step, peck, coolant | 186,90,122,10 | group +0x11c0 | i32 | |
| 1291 | Check | Search tool library when entering a tool number | 186,103,122,18 | group +0x11c4 | i32 | |
| 689 | Group | Sequence # | 176,226,142,50 | | | |
| 690 / 297 | Static / Edit (MCReal) | Start | 196,241,32,8 / 240,239,40,12 | **control** nc_output_ctrl +0 = page 8 ent +0x80 | f64, Init(v, 0, nc +0x10) | |
| 691 / 304 | Static / Edit (MCReal) | Increment | 196,260,40,8 / 240,257,40,12 | nc_output_ctrl +8 = page 8 ent +0x88 | f64, Init(v, tol, nc +0x10 * 0.5) | |
| -1 | Group | Material | 24,284,294,33 | | | |
| 477 | Edit | (material name) | 29,297,172,12 | group +0x11d0 char[0x51] | DDX_Text CString | FUN_1801eb000: a name not in the part's material list is cleared |
| 481 | Button | Edit... | 205,297,50,14 | | | FUN_1801ebc70: edit_material(m, 1, &changed), cur_matl_type 0; enabled = name not empty |
| 479 | Button | Select... | 259,297,50,14 | | | FUN_1801ebf50 -> FUN_1801eb980: list_matl_select; "NONE" -> 0 (radio forced to From tool, name ""); in part list -> 1; else imported from the library (TpPartMatList::DatabaseAdd) |
| 580 | Button | | 327,247,6,14 | | | hidden ("Leave these hidden controls alone", 554); FUN_1801ebbb0 = edit_material with cur_matl_type 1; enabled = name not empty |
| 554 | Check | Leave these hidden controls alone | 329,270,10,10 | | | hidden |

nc_output_ctrl +0x10 = the sequence-number maximum (9999.0 in every user part).

### 3.2 Defaults and consumers
- New group (InitGroup, decomp/mccore_grpsettings.c l.392-397): +0x1178 = 0, +0x119c = 0, +0x11b8 = 1 / +0x11bc = 0
  (one 8-byte store of 1), +0x11c0 = +0x11c4 = 0, +0x11cb = 0, material "NONE". From an old Job Setup
  (JobSetupToMachineGroup l.977-982): +0x119c / +0x11b8 / +0x11bc / +0x11c0 / +0x11c4 / material from assoc_setup
  +0x39 / +0x3c / +0x411 / +0x358 / +0x359 / +0x2f2.
- **Program #**: OMsetOperationToMachine (mastercam.dll @1803259d0, decomp/mastercam_opsettomachine.c), called by
  InitOperation (mccore_ops.c l.2437) for every new op: op +0xf8 = group +0x1178, op +0xfc = (int)StartSeqNum,
  op +0x100 = (int)SeqNumInc (= nc_output_ctrl +0 / +8, mdef_grpprops_accessors.c). Op record = op + 0x68, so record
  +0x160 / +0x164 / +0x168. **Existing ops keep their numbers** (the sheet's OnOK does not touch them; per-op change =
  Operations Manager 3714 Change Program #, formats/opmanager_menu.md).
- **Sequence numbers in posting**: the NCI writer's 1000 / 1001 / 1002 records take seq start / increment from the op's
  machine group's machine CtrlNCOutput at write time (decomp/mccore_bnci_str.c l.1069-1087).
- Override with modal values (+0x115c..+0x1168): InitOperation (mccore_ops.c l.2088-2122): for a new milling op whose
  predecessor (last op in the list) is milling: +0x1160 copies the clearance (op +0x108 value, +0x110, +0x111 flags),
  +0x1164 the retract (+0x118 / +0x120 / +0x121), +0x1168 the feed plane (+0x128 / +0x130).
- Assign tool numbers sequentially / warn: TpPartToolList::PrepareNewToolByMachine / PrepareNewAssemblyByMachine
  (mccore_tooladd.c l.6819, l.6942): +0x11b8 set -> tool number = OpenToolNumber starting at 1 (mill / +0x19e8 5),
  else +0x11bc set -> WarnOfDuplicateToolNumbers.
- +0x11c0 goes to init_op_tool_info as an argument (mccore_grpsettings.c FUN_1802e7b30 l.4143); its exact effect and
  every consumer of +0x11c4: NOT DECODED.
- Feed Calculation / material / user values: formats/feeds_speeds.md (already ported: cui_ops.c calc via F.grp_*).

### 3.3 Measured values (all 17 parts in <user-parts>, every machine group)
+0x1178 = 0; +0x115c..+0x1168 = 0; +0x119c = 0; plunge 25, retract 150, feed 100, rpm 5000; +0x11b8 = +0x11bc = +0x11c0
= +0x11c4 = 0; material "ALUMINUM mm - 2024". Every op record: program # 0. Ops: seq start 100 / increment 2, except
test-cube-10mm (100 / 10). Controls: page 8 start 100, max 9999, increment 2 ("Fanuc") or 10 ("Generic Default");
page 9 arc-feed flag 0; machine min arc feed 5.0 in / 125.0 mm. (Scratch dumper: session scratchpad grptabs/py/grpdump.py,
opdump.py, link.py - read-only.)

### 3.4 Posted-NC impact
- Sequence start / increment: **would change G-code.** viewer/nc_post.c hard-codes N100 step 2, wrap 9998 -> 100
  (`blk`, l.203), which equals the stored "Fanuc" control values of the user's VMC groups. test-cube-10mm's ops and
  control say increment 10: honoring the stored values would change that part's output (N100, N110, ...). Default
  must stay the current behaviour; an opt-in can read op record +0x164 / +0x168 (the values Mastercam froze into the
  op) - and the wrap rule against +0x10 (9999) is NOT DECODED (post.md only observed 9998 -> 100).
- Program #: the user's post prints no program number (post.md header rules) -> no G-code change. Store only.
- Adjust feed on arc / minimum arc feed: off in every part; how the post applies them: NOT DECODED -> no change.
- Override with modal values, tool numbering, feed calculation: affect only newly created ops / tools (their stored
  parameters, hence their G-code), never existing ones. Feed calculation is already used by cui_ops.c.

## 4. Safety Zone (template 0x637 = 1591 "Group PG4 Dlg") - not present in Mastercam 2017

No page class, no tree item for mill (section 0). The template is kept for reference only:

| id | kind | label | rect (DLU) |
|---|---|---|---|
| -1 / 4291 | Group / ComboBox | Shape | 7,6,91,40 / 16,23,77,39 |
| -1 / 960 / 651 | Group / Button / Edit | Safety Zone Plane / Select... | 108,6,220,40 / 116,20,22,18 / 140,23,178,12 |
| 1500 | Static | Bitmap goes here | 17,54,300,200 |
| 1499 / 1289 / 25149 / 25148 | Static / Edits | X | 7,268,10,8 / 22,265,40,12 / 77,265,40,12 / 79,266,40,12 |
| 25258 / 1456 / 25150 | Static / Edits | Y | 7,283,10,8 / 22,281,40,12 / 77,281,40,12 |
| 25259 / 25147 | Static / Edit | Z | 7,299,10,8 / 22,297,40,12 |
| 1501 / 25261 | Static | Radius / Length | 40,268,32,8 / 40,283,32,8 |
| 4290 / 4289 | Group / ComboBox | Axis | 140,263,97,34 / 154,276,48,30 |
| 1507 / 1509 / 1511 | Check | Display Safety Zone / Fit Screen to Safety Zone / Extend Z Negative | 7,327,100,10 / 113,327,98,10 / 238,326,90,10 |

Group data that remains: +0x19a0..+0x19e8 (copied by DropOnGroupPropertiesSafetyZone, itself gated off). The only
reader found, mccore FUN_180346db0 (mccore_grpsettings.c l.1392; no code references - RefsTo finds only unwind data),
uses: +0x19a0 u8 on, +0x19a4 i32 shape (0: box, centre Z lowered by +0x19b8 / 2; 2: centre along axis +0x19d8
(0 X / 1 Y / 2 Z) lowered by +0x19c8 / 2), +0x19a8 / b0 / b8 and +0x19c0 / c8 / d0 doubles. Field names beyond that:
NOT DECODED. Measured (all parts): off, shape 0, 300 / 200 / 100, 100 / 100 / 50, axis 0, byte +0x19e1 = 1.
Our apps must **preserve these bytes** (the save already copies the group record) and show nothing.

## 5. Porting plan

Cheap (group record only; existing stock_patch-style save of the active group):
1. Remove the "Safety zone" tree row and tab (cui_optree.c, mcamui.c, managers.cpp, ST_TABS) - Mastercam has three tabs.
2. Files: group name (+4, ':' -> '-', empty -> 3857 warning, CheckGroupName rule: NOT DECODED, keep "non-empty and
   unique among groups" out until decoded - grey rename if unsure), group comment (+0x980, 1999 chars). Read-only labels:
   Description, From file (+0x355 file name / 3168), Control, Post (+ red "none" text NOT DECODED -> plain), Tool /
   Operation library / Operation defaults paths - **fix the loader chain first** (2.2 caveat).
3. Tool Settings: program # (+0x1178, affects only new ops' record +0x160 - port into our op creation), Feed
   Calculation radios + user values + material (fields already loaded; add save), the four Toolpath Configuration
   switches and the Override switches (store; consumers in our op / tool creation: InitOperation modal copy and
   OpenToolNumber are small follow-ups), enable rules exactly as FUN_1801eb790.

Needs more work / decode:
4. Control / machine fields (sequence start / increment, arc feed flag, minimum arc feed, the four comment switches,
   tool / op library / op defaults paths, NC path): our save must patch the right control's page 8 / 9 / 2 records and
   the machine record (+0xf8 / +0xfc) instead of the group - and both copies? (each control is stored once per machine;
   parts with two machines have two controls). Mastercam rewrites them via SaveControlToDb etc.; the record layout of
   page 2 beyond the measured strings is NOT DECODED. Until then show these values read-only.
5. Tool library browse: we can honor it (tooldb support exists, cui_toolpage.c tl6410_libfile reads toollib) once the
   machine record save exists. Operation defaults path: cam_fileop.c / cui_ops.c hard-code shared Mill_MM / Mill_Inch
   .mcam-defaults; the stored paths are "...\MILL_MM.defaults-9" (GetCurVerExtFromPrvVerExt maps .defaults-9 ->
   .mcam-defaults; the %MCAMDIR% resolution is NOT DECODED) - honoring them changes nothing for the user's parts.

Must stay greyed (NOT PORTED): machine Edit (467, Machine Definition Manager) and Replace (463), tool manager (1266),
load tools to machine (751, multitasking only), lathe tool manager (635), operation library / defaults Edit (1264 /
1262, COpMgrDefaults), toolpath-directory browse (output folder is ours), Material Edit (481, material editor),
the Post path (our post is built in), Apply / Help (NOT DECODED, as for Stock Setup).

**Do not change posted NC by default**: N numbering (100 / 2), comment output and G43 H0 stay as nc_post.c has them;
any use of the stored sequence / comment settings must be opt-in and validated against the user's posted programs.

## Review fixes (2026-10-23, after the independent review; backup ~/mastercam-re/backups/pre_grptabsfix_2026-10-23.tgz)
1. **Modal copy** (cui_grpprops.c grp_modal_record): value and flags are copied as InitOperation does (record +0x170..+0x179,
   +0x180..+0x189, +0x190 / +0x198). A stored op: its record. An op made / edited here (also an edited stored op): the bytes the
   save writes for it - mcam_saveops.c mcam_op_record_heights = its own stored record (full OP_ID) or the defaults op, through
   the save's height rule (heights_to_record, now shared by the save) and then its record patches (written last, as the save
   does) - so clearance off, incremental values and Drill's R-plane flag are kept.
2. **Comment limit**: the Qt edit refuses input past 1999 ANSI characters at the cursor, a line end counted as CR LF = 2
   (DDV_MaxChars / EM_LIMITTEXT, FUN_1801e3940); the box and the stored comment always agree.
3. **Tree name = F.grp_name** (one source): scene.c calls scene_part_changed after load / new / unload ->
   grp_name_sync (the active group's +4; a new part: the defaults library's group, "Machine Group-1"); the Files page OK and
   the Toolpaths manager Rename (cui_misc.c -> grp_set_name) both write F.grp_name (+ grp_dirty: saved into the part and the
   project's GRPNAME line). The tree now shows the part's own name (tramming-rod / thread-test / BRACKET4 / NAKTO "Machine
   Group-2").
4. **Control pages**: when the id rule (control + 1 + page) finds no page, the page records following the control record in
   file order are taken by their +0x68 (ours; ctl_pgfb, reported by gpdump). No user part needs it.
5. **tp_tool_offset** (mccore 0x1800dc270; formats/toolmanager_ui.md "D/H offsets"): CCNCMachine::AddToToolRegister(1) decoded
   (mcmachinedef @180071e30 = CtrlToolInfo(1) +0 u16, decomp/mdef_addtotoolregister.c; = control page 9 ent +0x29e, 0 in
   every user part and both defaults libraries) -> a renumbered tool gets D / H = DOfsAmt / LOfsAmt + number (part_add_tool).
   Applied only when the number changed; the GetProdEnableBits() & 6 gate NOT DECODED (taken clear). Posting unaffected (H0).
6. **CP1252**: the Qt Files / Tool Settings texts and the tree's machine group label convert with Windows-1252 both ways
   (viewer-qt/src/cp1252.h, '?' for unmappable); gpdump escapes bytes >= 0x80.
7. The group-name list is bounded by the records the file holds.
8. **FUN_1801e2f70's warning** (Files page set-up = PrmToDlg, and its OnOK): empty name -> "Invalid group name" + "\n\n"
   (DAT_1805d89a8, decomp/mastercam_grppg1_sep.txt) + name; else OpDfltsFile(units) failing file_exists -> "Invalid file
   name\n\n" + that path; else (not Type 5) ToolLibFile(units) failing -> "Invalid file name\n\n" + the stored tool library.
   Which host file is tested: ours (%MCAMDIR% -> the shared folder as tl6410_machine_lib; .defaults-N -> .mcam-defaults as
   GetCurVerExtFromPrvVerExt), since Mastercam's expansion is NOT DECODED. Effect: the five parts naming user-tools.TOOLDB
   (BRACKET4, bracket4-dashed, NAKTO, tramming-rod, thread-test) and test-cube-10mm warn when the Files page opens and at
   OK. DAT_1805d5528 (the Post's red text) = "NONE"; FileNameIsNone is still NOT DECODED (the stored post is shown).
