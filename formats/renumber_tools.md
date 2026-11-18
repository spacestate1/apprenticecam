# Renumber tools (cmd 3580 / 0xdfc, CRenumbTl, IDD 805): specification

Source: `decomp/opman_dialogs.c` (= D below). Line numbers are D lines unless prefixed `T:` (= `decomp/opman_tree_all.c`).
Inferred MFC/Win32 meanings: `SendMessage 0xf0/0xf1` = BM_GETCHECK/BM_SETCHECK, `Ordinal_13906(dlg,1|0)` =
UpdateData(TRUE|FALSE), `Ordinal_4282` = EnableWindow, `Ordinal_5128` = GetDlgItem, msg 0x28 = WM_NEXTDLGCTL.
`Ordinal_10537` is unresolved (see section 7).

## 0. Invocation
- T:22145-22157: `case 0xdfc`: construct dialog (FUN_180350be0, not decompiled), DoModal (vtbl+0x2f0), destroy, then
  **`RefreshAll(this,false,0)` always** (OK or Cancel).
- Menu item disabled when the right-clicked op is a wire op (T:21169-21171) and on a wire machine group (group type 2,
  T:21310-21316).

## 1. Dialog fields, defaults, persistence, enable logic, validation

DDX map (FUN_1804570e0, D:30-43) and storage. MCLong value lives at control+0xbf8 (inferred from the four identical deltas).

| Control | DDX member | Value / HWND | Static (persisted) | .data initial value |
|---|---|---|---|---|
| 1357 Starting tool number (edit) | +0x4010 | value +0x4c08 | DAT_1808a1908 | 1 |
| 1312 Tool number increment (edit) | +0x2540 | value +0x3138 | DAT_1808a190c | 1 |
| 1406 Length offset check | +0x1550 | HWND +0x1590 | DAT_1808a1a14 | 1 |
| 1114 length amount (edit) | +0x32a8 | value +0x3ea0 | none (machine) | - |
| 1408 Diameter offset check | +0x1628 | HWND +0x1668 | DAT_1808a1a18 | 1 |
| 1383 diameter amount (edit) | +0x17d8 | value +0x23d0 | none (machine) | - |
| 1248 Also renumber unused tools | +0x1700 | HWND +0x1740 | DAT_1808a1a1c (read only) | 1 |
| 1468 Renumber head number | +0x4d78 | HWND +0x4db8 | none (machine) | - |

The initial values are read from the .data section of `~/mastercam-re/files/mastercam.dll`. A dynamic initialiser
that overwrites them was not checked.

OnInitDialog (FUN_1804572a0, D:48-117):
- Machine = `OMgetActiveMachine()` (= `OMgetGrpMachine(NULL,0)`, D:3177-3205). A null machine asserts
  "RENUMBTL.CPP" 0x11d (D:82-85). `bVar9 = MachTypeToCtrlType(machine) != 2` (D:86-87). Its meaning as an argument is NOT DETERMINED.
- Start = DAT_1808a1908, Increment = DAT_1808a190c (D:88-89).
- Check 1248 = DAT_1808a1a1c, check 1406 = DAT_1808a1a14, check 1408 = DAT_1808a1a18 (D:90-92).
- Length amount = `CCNCMachine::LOfsAmt(m,bVar9)` (D:93-94). Diameter amount = `DOfsAmt(m,bVar9)` (D:95-96).
  These are re-read from the machine every time and never persisted.
- Check 1468 = `CCNCMachine::UseHeadForTool(m,bVar9)` (D:97-98). It is never persisted.
- If `GetProdEnableBits() & 0x120` (lathe product), the label of 1468 is set to string 0xf70 = opmanres 3952
  **"Renumber station number to tool number"** (D:99-109; all.tsv).
- UpdateData(FALSE) (D:110), then FUN_1804571e0 (D:111).
- Enable logic, FUN_1804571e0 (D:3210-3227): edit 1114 enabled = check 1406 state, and edit 1383 enabled = check 1408 state.
  No other enable dependencies. In this decomp it is called only from OnInitDialog. The BN_CLICKED wiring that
  presumably calls it again is NOT DETERMINED, because the message map is not decompiled.
- PreTranslateMessage (FUN_180459910, D:2223-2234): Enter (WM_KEYDOWN 0x0d) posts WM_NEXTDLGCTL. Enter moves focus
  to the next control and does not press OK.

OnOK (FUN_1804574e0) persistence (D:281-287): UpdateData(TRUE). DAT_1808a1908 = Start, DAT_1808a190c = Inc,
DAT_1808a1a14/1a18 = the two offset checks. **The "unused tools" check (DAT_1808a1a1c) is never written back.** It
therefore always opens checked, whatever the last choice was. Cancel persists nothing.
- **Quirk:** DAT_1808a1908 is also used as the running counter (D:308, 367, 1929, 2128). After OK, the value persisted
  as "Start" is the next free number of the **last root machine group processed**. Start is reset to the
  dialog value at D:308 for every root group. A last group with no in-scope tools leaves the persisted value equal to the
  entered Start.
- Validation: **none** in OnOK (no message box or string id). Range checks inside `DDX_ProcessEditLong`/MCLong
  are NOT DETERMINED. Negative or zero Start and Increment are accepted by the logic as written.

## 2. Scope and order

Step A: selection expansion, FUN_180459950 (D:3297-3540), called first in OnOK (D:279):
1. `ids = OMgetSelectedOpIds()`. If the list is empty, nothing happens here.
2. For every op in the main op list with `op+0x95 != 0` (selected flag, inferred) and `op_is_xform(type)`:
   `CDependentOperations::Detect(xformId, 4, ...)`. Each returned op with `op+0x95 == 0` is added (D:3349-3371).
   That these are the transform's source ops is NOT DETERMINED; the code only shows "dependents, mode 4".
3. A ToolID list is built from `ids` with the same filter as step B (D:3373-3428).
4. Every op sharing a tool is added. Mill: `list_op_by_tl(slot)` adds ops whose `op+0x544 == slot` (D:3460-3484).
   Lathe (prod bits 0x120): `Cnc::Tool::list_op_by_ltl(slot)` adds all returned ops (D:3487-3510).
5. **`OMselectOpIds(ids)`** (D:3511). The tree selection is permanently changed to the expanded set.

Step B: main pass, FUN_1804574e0:
- Loop over every group of the main group list (D:289-303). Only groups where `RetrieveRootMachineGroup(g)->id == g->id`
  are processed (root machine groups, in group-list order) (D:304-307). `local_a460` = group id.
- Per root group: `counter = Start` (D:308). `ops = OMgetOpIdsInTreeOrder()` lists all ops in tree order, globally
  (D:309, D:3622-3633). If that count is < 1, **break out of the whole group loop** (D:310).
- Filter (D:313-335), in order:
  - Missing op: removed.
  - **Type 4 (Transform): kept in the list whatever its selection or machine group, but contributes no tool.**
    Its NCI is still scanned and patched in step D (D:329).
  - Removed if any of the following holds: not selected (`op+0x95==0`), `op_is_no_nci`, `OMgetOpMachineGroup(op,1) != this root group`,
    lathe op without prod bits 0x120, `op_is_notool`, type 0x132 (Router Block Drill).

## 3. Tool identity and numbering
- ToolID (CArray<CRenumbTl::ToolID>, 16 bytes; D:3567-3578 ctor, D:3544-3562 SetAtGrow): `+0 u8 isLathe`, `+4 i32 slot` (=
  `op+0x544`), `+8 i32 old tool#` (`op+0x2e0`), `+0xc i32 new tool#`.
- For each surviving non-transform op in tree order, look up (slot, isLathe) in the array (D:342-355). If it is not
  found, append {isLathe, slot, op+0x2e0, counter} and do `counter += Increment` (D:356-368). Later ops with the same
  tool reuse the entry.
- **The ToolID array is created once (D:288) and is NOT cleared between machine groups.** Entries from earlier groups
  persist. A tool already seen keeps its earlier number, and the per-group arrays built at D:381-407 contain all
  entries accumulated so far. Consequence for section 6c: a later group rewrites the library entries of earlier groups' mill tools
  with len/dia = 0. That this happens in practice is NOT DETERMINED, because the arguments of vtbl+0x168 are lost.
- The entries are split into a mill set {slots local_a388, old# local_9e88, new# local_a0b8} and a lathe set
  {local_a2b8, local_9ef8, local_a128} (D:381-407). Old numbers are never used afterwards.
- If both sets are empty, `Ordinal_10537(dlg)` runs and the group is skipped, including the unused-tools pass (D:408-418).
- Per-index result arrays: local_a208 = mill length offset, local_9f68 = mill diameter offset, local_9fd8 = lathe length,
  local_a048 = lathe diameter. All are initialised to 0 (D:420-442).

### 3a. NCI patch (per op in `ops`, D:672-1732)
`bnci_read_sos(opId)` positions the NCI file at the op's section (D:682). It then reads records until a read failure or -3
(D:686-691), remembering `pos` = file position before each record (uVar10). Held buffers:
- 1016 (0x3f8) is copied to H1016 and its position remembered (D:692-727).
- 20004 (0x4e24, mill) is copied to H20004 and its position remembered (D:1629-1664).
- 20100 (0x4e84, lathe) is copied to H20100 and its position remembered (D:1666-1700).
- 1011 (0x3f3) clears all held buffers and positions (D:732-880). Other codes are ignored.
- 1000/1001/1002 tool change (TC): if no H1016 is held, the held buffers are cleared (D:1480ff). Otherwise:
  - `flag = TC+0x0c`. flag 0 (mill) with no H20004, or flag 1 (lathe) with no H20100: clear and do not patch (D:939-1232).
  - `idx = IdInArray(H1016+0x60 /*tool slot*/, flag ? lathe slots : mill slots)` (D:1234-1239). Not found: no patch.
  - **Mill** (D:1240-1259): `n = new#`. `TC+0x24 = n`. If the length check is on: `TC+0x2c = H20004+0x4c = lenAmt + n`.
    If the diameter check is on: `TC+0x28 = H20004+0x48 = diaAmt + n`. `H20004+0x18 = n`. If the head check is on:
    `H1016+0x58 = n`. Then `local_a208[idx] = TC+0x2c` and `local_9f68[idx] = TC+0x28`. These are the values after
    patching; an unchecked field keeps the original NCI value.
  - **Lathe** (prod bits 0x120 required; any other flag value skips) (D:1261-1299): `n = new#`. `H20100+0x28 = n`.
    - If `H1016+0x1e` (i16) is 0x3e Lathe Groove or 0xd0 Lathe Canned Groove and either offset check is on: **live
      `op+0x94 = 1`** (dirty), and the DB copy is retrieved with `+0x94 = 1` and saved via `UpdateListAndDB` (D:1269-1282).
    - Otherwise, if the **diameter** check is on: `TC+0x28 = n + diaAmt` (D:1284-1286). Lathe TC+0x2c is never patched.
    - Head check on: `H1016+0x58 = n`.
    - `local_9fd8[idx] = lenAmt+n` and `local_a048[idx] = diaAmt+n`, **unconditionally** (D:1291-1298).
  - Write-back, in place, same record sizes (D:1301-1340): seek to TC and write it. Only if that succeeds: seek to
    H1016 and write it, then write H20004 if its position is known, else H20100. Finally seek back.
  - **Op DB update**, once per op (flag local_a477) and only if `!op_is_notool` (D:1341-1381):
    `DatabaseRetrieve(op)`. Offsets below are op-relative (ent = op + 0x68: tool#, dia, len, head are 0x348/0x370/0x374/0x5a4).
    - Mill: `op+0x2e0 = n`, `op+0x30c = TC+0x2c`, `op+0x308 = TC+0x28`, and if the head check is on `op+0x53c = n`.
    - Lathe: `op+0x2e0 = n`. **If the diameter check is on:** `op+0x30c = n + lenAmt`. **If the length check is on:** type 0x3e/0xd0
      gets `op+0x1450 = n + diaAmt`, type 0x3d (Lathe Finish) gets `op+0x14e0 = n + diaAmt`. If the head check is on: `op+0x53c = n`.
      The crossed checkbox gating is literal in the decomp.
    - Then `UpdateListAndDB(op,false,true)`.
  - All held buffers are cleared after a patch (D:1385ff). Each later TC in the same op needs a fresh 1016 and a fresh 20004/20100 record.
- **Consequence:** an op's tool#, offsets and head are updated only through this NCI path. Ops whose NCI lacks a
  1016 and 20004/20100 record before a TC keep their old op fields.

### 3b. Tool library, tools in use (D:1734-1879)
- Mill, for each slot in the mill set: tool system vtbl+0x168 (lookup; the arguments are lost in the decomp). If it
  succeeds, fill a TL-ent buffer (local_52b8; ent offsets): `+0x68 = new#`, `+0x94 = local_a208[idx]` (len),
  `+0x90 = local_9f68[idx]` (dia), and if the head check is on `+0x2c4 = new#`. Then vtbl+0x1a0 (save; arguments lost)
  (D:1738-1805). Unpatched tools get len/dia = 0.
- Lathe (prod bits 0x120): vtbl+0x168(slot). Buffer P = local_2910: `P+0x76c = new#`, `P+0x774 = lenAmt+n`,
  `P+0x770 = diaAmt+n` (both unconditional). If the head check is on, `P+0x9d8 = n`. Then vtbl+0x198(P) (D:1808-1879).

## 4. Unused tools (check 1248)
Runs per root group, after the used tools, continuing the same counter:
- Check off: the pass is skipped. Unused tools keep their numbers.
- **Mill** (D:1880-2083): `tool system vtbl+0x40` returns all assemblies, in tool-system order. For each assembly that
  passes every test below:
  - Tests: `HasMainTool`, `GetMachineGroup == group id`, and not (MCToolType 3 Drill or 22 Bradpt Drill with legacy field
    +0x3d0 == 3). The meaning of that field is NOT DETERMINED. The slot must be absent from the in-scope mill set, and
    `list_op_by_tl(slot)` must return no op at all (no op in the file uses it).
  - Action: `Clone`, `SetToolNumber(counter, group)`, `counter += Inc`. If the length check is on:
    `SetLengthOffsetNum(lenAmt+num)`. If the diameter check is on: `SetDiameterOffsetNum(diaAmt+num)`. If the head check
    is on: `main tool SetToolStation(num)` then `SetMainTool`. Finally vtbl+0x58(clone), i.e. replace/save
    (D:1924-1988).
- **Lathe** (check on and prod bits 0x120; D:2085-2170): `vtbl+0x48` returns all lathe tools. Each tool needs
  `GetMachineGroup == group id`, a slot that is not in the lathe set, and `list_op_by_ltl(slot)` returning none. Then
  vtbl+0xa0 fills buffer B: `B+0x76c = counter`. If the length check is on: `B+0x774 = lenAmt+counter`. If the diameter
  check is on: `B+0x770 = diaAmt+counter`. Then `counter += Inc`. If the head check is on: `B+0x9d8 = num`. Save via vtbl+0x198.
- Order: mill unused first, then lathe unused, after all used tools of the group.

## 5. Conflicts
**No duplicate or conflict check of any kind.** No lookup of existing numbers and no message. Tools out of scope
(unselected ops, unused tools with the box off, other machine groups) keep their numbers, so duplicates are possible.

## 6. Side effects
- (a) **The selection is changed** to the expanded set (D:3511).
- (b) NCI records are patched in place: TC 1000/1001/1002 (+0x24/+0x28/+0x2c), 1016 (+0x58), 20004 (+0x18/+0x48/+0x4c),
  20100 (+0x28). There is no regeneration and no dirty flag, except for lathe groove ops (op+0x94 = 1).
- (c) Op DB records are rewritten via `UpdateListAndDB`, and tool library entries via the tool-system vtbl 0x1a0/0x198/0x58.
- (d) **No undo.** There is no BackupOpsAdd or similar call anywhere in OnOK.
- (e) RefreshAll comes from the caller (T:22156).
- (f) The static Start ends up holding the advanced counter (section 1).

## 7. NOT DETERMINED
- The identity of `Ordinal_10537` (called when a group, or the whole selection, has no tools; D:411, D:3446).
  Execution continues after it and `UIDialog::OnOK` still runs at D:2214.
- The arguments and semantics of tool-system vtbl 0x168/0x1a0/0x198/0x58/0x40/0x48/0xa0.
- The arguments of LOfsAmt, DOfsAmt and UseHeadForTool.
- The legacy field +0x3d0 (value 3) that excludes Drill and Bradpt-drill assemblies.
- `CDependentOperations::Detect` mode 4 semantics.
- The BN_CLICKED handlers.
- Any MCLong range limits.
