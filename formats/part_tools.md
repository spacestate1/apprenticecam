# Part-file tool definitions: the `TlMgr` archive (plan item T4)

Where a Mastercam part file (.mcx-9 / .mcam) keeps each tool's full geometry, its holder and its assembly. This
extends the 0x288-byte TL_ID record that `viewer/mcam_load.c` reads today (formats/tooldb.md). Every field below cites
its evidence. Anything without evidence is listed under **NOT DECODED** (§9).

Evidence sources:
- **mccore.dll** (Ghidra `~/mastercam-re/ghidra_core`, program `mccore.dll`):
  - readers in `decomp/mccore_deserialize.c`;
  - writers in `decomp/mccore_tlserialize.c`;
  - tlcore's writers / readers (TlObject, TlAssemblyItem, TlProfile and segments, op params, coolant, TlManufacturer,
    TlToolGrade), TlID / TlObject / segment constructors and `TlID::operator<`: `decomp/tlcore_tlserialize.c`
    (persisted 2026-10-16 from the full tlcore decompile);
  - getters/setters dumped to `decomp/part_tools/getters.c` (map `getter_map.txt`); the other dumps of that work are in
    `decomp/part_tools/` too: `binders.c` (+ `binders.txt`), `classes.c`, `lambdas.c`, `sel_deser.c`, `tl_ser.c`, and the
    text maps `dbs.txt`, `hashes.txt`, `tl_hashes.txt`, `others.txt`, `report_all.txt`.
- **tlcore.dll** (Ghidra `~/mastercam-re/ghidra_tl`, program `tlcore.dll`): full decompile in `decomp/native_agent/tl_all.c`.
  Addresses are the export-thunk addresses printed there.
- **Reads through setter lambdas.** The newer readers do not store fields directly. They call
  `FUN_18070e1d0 / e260 / e160 / 1807(1)cbd0 / cb60` (read f64 / bool / long / CString / int) with a `std::function` whose
  `_Func_impl<lambda_…>` `_Do_call` slot (vftable +0x10) writes the member.
  - Every lambda used by the readers in §3 was resolved to its member offset by the Ghidra scripts
    `decomp/part_tools/gscripts/LambdaCalls.java` and `LambdaFrom.java`. Outputs: `decomp/part_tools/lambda_calls.c`,
    `lambda_tail.c`, `tl_lambda_calls.c`; summary in `decomp/part_tools/lambda_map.txt`.
  - Offsets were then named with the exported getters (`TlToolMill::GetOverallLength` returns `this+0x7f0`, and so on).
- **Data:** the 16 user parts `<user-parts>/*.mcx-9`, plus `test-exports/*` (7 .mcx-9 and the 2017-format
  `new-plate.mcam`).
- Scripts (copied from the session scratch folder into `tools/tests/`):
  - `tools/tests/tlmgr_parse.py`: the reference reader;
  - `tools/tests/tlmgr_report.py` (was `report.py`): the per-part dump;
  - `tools/tests/tlmgr_nci_corr.py` (was `nci_corr.py`): the NCI cross-check.
  Below, `tlmgr_parse.py`, `report.py` and `nci_corr.py` mean these files.

## 1. Where the data lives

- **Record:** one ASSOC_ID (0x4000) entity with subtype `+0x2c` = **33**, ClassEnt flag `ent+0x56 != 0` (formats/mcam.md
  "Serialized objects").
  - The body is an MFC `CArchive` stream. In all 24 files it starts at **ent+0x378** with the class tag
    `FF FF 05 00 05 00 "TlMgr"` (schema 5).
  - Locate it by searching for that tag; do not hard-code +0x378.
  - ent+0x68..+0x378 is not decoded.
- **Root:** `TlMgr::DeserializeV5` (mccore 0x18073dc60), level tag only:

  ```
  i32 n;  n × { GUID key[16]; ReadObject() }      // key == the object's TlObject ID (asserted on all 24 files)
  i32 m;  m × { i32 slot; GUID assembly_id }       // slot table -> TlMgr+0x30
  i32 k;  k × { i32 slot; GUID id }                // lathe slot table -> TlMgr+0x40 (empty in every file)
  ```

- **Objects in the list:** `TlAssembly`, `TlHolder` and the tool classes (`TlToolEndmill`, `TlToolDrill`,
  `TlToolThreading`, …).
  - An assembly links one tool and one holder by GUID.
  - **Every TlAssembly's slot equals the slot of its tool and its entry in the slot table** (62/62 assemblies).

### 1.1 Linking to the TL_ID records and the operations

- **TL_ID ↔ tool:** the TlTool level carries a raw 0x288-byte block (the legacy `op_tool_info`).
  - It is **byte-identical to TL_ID entity +0x68..+0x2f0** for 62/62 tools in the 16 parts.
  - Each matches exactly one TL_ID (ASSOC subtype 3, 0x700-byte record).
- **New field: `op_tool_info +0x264` = i32 TlMgr slot.** At TL_ID that is entity +0x2cc; in an operation, OP_ID +0x5ac
  (embedded tool at +0x348).
  - Equal to the TlTool slot in 62/62 tools.
  - In 189/189 OP_ID records (subtype 1), +0x5ac is the only offset whose value names an assembly with the op's tool#
    (+0x348) and tool name (+0x39e).
  - **Use the slot, not the tool number, to find the assembly:** tool numbers repeat across assemblies in two parts
    (BRACKET4 T2 and T8; NAKTO T2 in slots 6 and 7).
  - The op's embedded 0x288 copy is *not* byte-identical to the TlTool block: 189/189 differ in ~103 bytes, which are
    not decoded.

## 2. Archive grammar (MFC CArchive, as Mastercam uses it)

- **Tags:**
  - `FFFF u16 schema u16 len name` = new class.
  - `0x8000|i` = existing class i.
  - `i < 0x7FFF` = back-reference to object i.
  - `0x0000` = null.
  - `0x7FFF u32` = big tag.
- **Map indices** count classes and objects together, starting at 1.
  - `ReadObject` of a new class assigns 2 indices (class, then object).
  - A new object of a known class assigns 1.
  - A level tag of a new class (`ReadClass`) assigns 1.
- **Levels.** Every `X::Serialize` calls `Base::Serialize`, then `ar.SerializeClass(X)` (one level tag, class only), then
  X's own fields (mcam.md "Writer side").
  - Embedded members (`stored profile`, `coolant`) call `Serialize` directly: level tags only, no object index.
  - Pointer members use `ReadObject` (op params, profile segments).
- **CString:** `u8 len` (0xFF → `u16`; 0xFFFF → `u32`). Unicode marker 0xFFFE never seen.
- **Inheritance chains**, from each Serialize's base call (mccore_tlserialize.c / tl_all.c):

| leaf | levels (base first) |
|---|---|
| TlAssembly | TlObject, TlAssembly |
| TlHolder | TlObject, TlAssemblyItem, TlHolder |
| TlToolEndmill / TlToolDrill / TlToolThreading / TlToolReamer / TlToolFeedMill / TlToolBarrelMill | TlObject, TlAssemblyItem, TlTool, TlToolMill, *leaf* |
| TlToolSlotMill | … TlToolMill, TlToolEndmill, TlToolSlotMill |
| TlToolBoring / TlToolCenterDrill / TlToolCounterBore | … TlToolMill, TlToolDrill, *leaf* |
| TlProfileLineSegment / ArcSegment | TlObject, TlProfileSegment, *leaf* |
| TlMillingOpParams / TlHolemakingOpParams | TlObject, TlOpParams, *leaf* |
| (embedded) TlProfile | TlObject, TlProfile |
| (embedded) TlCoolant | TlObject, TlCoolant |

**Verification of the grammar.** `tlmgr_parse.py` consumes every TlMgr stream to the exact last byte, in all 16 user
parts and 8 test exports, with every class tag where the chain expects it. That includes the 2017 class versions in
`new-plate.mcam`.

## 3. Level layouts (versions in the files)

Seen in the user's parts (X9 writer): TlMgr 5, TlObject 1, TlAssemblyItem 7, TlTool 13, TlProfile 2, TlProfileSegment 1,
TlProfileLineSegment 1, TlToolMill 10, TlToolEndmill 5, TlToolDrill 3, TlToolThreading 7, TlHolder 4, TlAssembly 8,
TlOpParams 1, TlMillingOpParams 1, TlHolemakingOpParams 1, TlCoolant 1.

`new-plate.mcam` (2017 writer): TlObject 2, TlAssemblyItem 9, TlTool 15, TlToolMill 12, TlHolder 5, TlAssembly 9; the rest
is unchanged. "→ +0xNN" is the member the read lands in; the name comes from the getter at the cited address.

### 3.1 TlObject (v1: tlcore `TlObject::DeserializeV1` 0x180005ad3; v2: writer `TlObject::Serialize` 0x18000cf9f)
`GUID[16]` → +0x30 ID · `CString` → +0x18 Name (`GetName`) · `CString` → +0x20 Description (`GetDescription`) ·
**v2 only:** `i32` slot (`GetSlot`).

### 3.2 TlAssemblyItem (v7 = DeserializeV7 0x1800027f2 → V6 0x18000443a → V5 0x180006faf → V4 0x180006e1f → V3 0x180003fcb → V2 0x18000f40c)

| read | member | getter |
|---|---|---|
| CString | +0x48 | GetCatalogID |
| CString | +0x50 | GetLocation |
| CString | +0x58 | GetImageFile |
| CString | +0x60 | GetGeometryFile |
| i32 | +0x80 | GetQuantityOnHand |
| bool | +0x84 | **GetIsMetric** |
| GUID | +0x88 | GetMaterialID |
| GUID | +0x118 | NOT DECODED (TlToolMill V10 calls `SetToolGrade` when it is non-null) |
| GUID | +0xa0 | GetToolTypeID |
| GUID | manufacturer: when non-null, creates the object at +0xf0 and sets its ID (V2 tail) | |
| i32 n, n × GUID | list at +0x100 (name NOT DECODED) | |
| bool (V3) | +0x85 | GetThruCoolant |
| GUID (V4) | +0xb8 | GetSourceID |
| bool (V5) | +0x86 | GetIsCatalogItem |
| f64, f64 (V6) | +0xd0, +0xd8 | GetGaugeDiameter, GetGaugeLength |
| i32 n, n bytes (V7) | +0x68 | GetModelData |

**v9** (2017 writer `TlAssemblyItem::Serialize` 0x18000d8aa) writes, in order:
1. 4 CStrings, u32 +0x80, bool +0x84, GUID +0x88, GUID +0xa0;
2. the list (+0x100), bool +0x85, GUID +0xb8, bool +0x86, f64 +0xd0, f64 +0xd8, the blob (+0x68);
3. **the embedded stored TlProfile (+0xe0)**;
4. `bool`, then, if it is true, `WriteObject(+0xf0 manufacturer)`.

### 3.3 TlTool (v13: mccore `TlTool::DeserializeV13` 0x1807511b0; v15: writer `TlTool::Serialize` 0x1807547b0)
- **v13** reads, in order:
  1. Embedded **stored profile** (`GetStoredProfile` → vfunc +0x10 Serialize → `SetStoredProfile`): a TlProfile
     (§3.7). It is empty for every parametric tool in the parts.
  2. `bytes[0x288]` legacy op_tool_info (= TL_ID body, §1.1).
  3. `long` slot (`TlObject::SetSlot`).
  4. `long` +0x130 ToolNumber (`GetToolNumber` 0x18012d0f0).
  5. `long` +0x134 ToolStation (`GetToolStation` 0x1806c7240).
  6. `long` +0x138 MachineGroup (0x180326400).
  7. `CString` +0x140 MfgToolCode (0x18064c060; lambda 94e67063 → `CString::operator=(this+0x140)`).
  8. `bool` +0x148 IsCustom (0x1806c6840).
  9. `raw4` +0x150 DisplayType (0x1807544b0; 0 = parametric, 1/2/3 = import from file / layer / stored profile, per
     tool_profiles.md §4.2).
- **v15:** `bytes[0x288]`, u32 +0x130, +0x134, +0x138, CString +0x140, bool +0x148, raw4 +0x150, u32 +0x14c
  GeometryLevel. No stored profile (it moved to TlAssemblyItem v9) and no slot (it moved to TlObject v2).

### 3.4 TlToolMill (v10: mccore `TlToolMill::DeserializeV10` 0x1808351b0; v12: writer `TlToolMill::Serialize` 0x1808410f0)
v10 read order:

| # | read | member | getter / name |
|---|---|---|---|
| 1 | ReadObject | +0x888 | op params (TlMillingOpParams or TlHolemakingOpParams, §3.8) |
| 2 | f64 | +0x878 | GetLegacyHolderLength 0x18039bd60 |
| 3 | f64 | +0x880 | GetLegacyHolderDiameter 0x18039bd50 |
| 4 | f64 | **+0x7f0** | **GetOverallLength** 0x18012d050 |
| 5 | f64 | **+0x7f8** | **GetShoulderLength** 0x18012d080 |
| 6 | f64 | **+0x800** | **GetCuttingDepth** (flute length) 0x1800e8290 |
| 7 | f64 | **+0x808** | **GetOverallDiameter** 0x1802e8040 |
| 8 | f64 | **+0x810** | **GetArborDiameter** 0x18039bc50 |
| 9 | f64 | **+0x818** | **GetShoulderDiameter** 0x18039bda0 |
| 10 | raw4 | **+0x820** | **GetShankType** (TlShankType) 0x18007ca60 |
| 11 | f64 | **+0x828** | **GetTaperLength** 0x18039bde0 |
| 12 | f64 | **+0x830** | **GetNeckDiameter** 0x18007bc70 |
| 13 | raw4 | +0x840 | GetSpindleDir (TlSpindleDir) 0x1800d8b90 |
| 14 | bool | +0x86c | GetIsAdjustedForStickout 0x1803a59a0 |
| 15 | f64 | +0x870 | **GetTruePhysicalLength** 0x1803a5bd0 |
| 16 | f64 | +0x838 | GetRequiredPilotDiameter 0x18064c100 |
| 17 | CString | – | `TlAssemblyItem::SetGeometryFile` (lambda c677650c) |
| 18 | i32 | +0x14c | TlTool::GetGeometryLevel 0x180280650 |
| 19 | bool | +0x86b | GetIsScalable 0x180280660 |
| 20 | raw4 | **+0x8b8** | **GetMCToolType** 0x18083ff10 (formats/tooltypes_mill.tsv) |
| 21 | long | +0x850 | GetDiameterOffsetNum 0x1806c64e0 |
| 22 | long | +0x854 | GetLengthOffsetNum 0x1806c68b0 (lambda 356428a0) |
| 23 | i16 | +0x858 | GetFluteCount 0x180508090 (lambda 5e97eff5) |
| 24 | raw4 | +0x85c | GetHelixType 0x1806c6560 (lambda d42a69dc) |
| 25 | f64 | +0x860 | GetHelixAngle 0x1806c6550 (lambda b98566a2) |
| 26 | CString | +0x848 | GetChuck 0x18064bff0 (lambda 78169c0e) |
| 27-29 | bool ×3 | +0x86a, +0x868, +0x869 | GetIsVariablePitch, GetIsFBMTool, GetIsAggregate |
| 30 | bytes[0x10] | discarded (read into a local and never used) | NOT DECODED |
| 31 | bytes[0x78] | +0x8f0 | NOT DECODED |
| 32 | i32 n, n × GUID | list at +0x8c0 | NOT DECODED |

**v12** (2017 writer) is the same through #16. Then:
- #17 (CString) and #18 (i32) are gone;
- #19 bool +0x86b and #20-29 follow unchanged;
- there is no discarded 16 bytes;
- after the GUID list, `bool` and, if it is true, `WriteObject(+0x8a8 tool grade)`.

### 3.5 Tool subclasses
Read order (address); "≡" = the getter that names the member:
- **TlToolEndmill v5** (`DeserializeV5` 0x180766b00; writer 0x180767a30):
  - f64 +0x988 ≡ GetCornerRadius 0x180767710;
  - f64 +0x968 ≡ GetTaperAngle 0x18007caa0;
  - f64 +0x970 ≡ GetTipDiameter 0x18007cb00;
  - raw4 +0x978 ≡ GetCornerType / GetRadiusType 0x18007baa0;
  - f64 +0x980 ≡ GetChamferLength 0x18007ba70.
- **TlToolDrill v3** (0x18075e3a0; writer 0x18075eee0):
  - f64 +0x978 ≡ GetCornerRadius 0x18075ec40;
  - f64 +0x968 ≡ GetTipAngle 0x18007cae0;
  - f64 +0x970 ≡ GetShoulderAngle 0x18007ca70.
- **TlToolThreading v7** (0x18085dfc0; writer 0x18085f450):
  - f64 +0x968 ≡ GetTipAngle 0x18085f440;
  - f64 +0x9a0 ≡ GetCornerRadius 0x18085f290;
  - f64 +0x970 ≡ GetThreadPitch 0x18007cac0;
  - raw4 +0x990 ≡ GetRadiusType 0x18007bcf0;
  - f64 +0x978 ≡ GetRootDiameter 0x18007ca10;
  - f64 +0x980 ≡ GetThreadAngle 0x18007cab0;
  - f64 +0x988 ≡ GetTipDiameter 0x18007cb20;
  - i32 +0x994 ≡ GetNumThreadStarts 0x18007bc80;
  - raw4 +0x998 ≡ GetThreadShape 0x18007cad0.
- **No sample in any file**: the layout is from the 2017 writer store branch and the matching highest `DeserializeVn` in
  formats/carchive_layouts.md. The schema an X9 file would carry is unknown. **UNVERIFIED on data:**
  - TlToolSlotMill v2 (writer 0x180a34000, base Endmill):
    raw4 +0x990 SecondaryCornerType, f64 +0x998 SecondaryCornerRadius, f64 +0x9a0 SecondaryChamferDist,
    f64 +0x9a8 SecondaryChamferAngle.
  - TlToolCenterDrill v1 (0x180a2ff50, base Drill): f64 +0x980 DrillDiameter, f64 +0x988 DrillLength.
  - TlToolCounterBore v1 (0x180a31870, base Drill): f64 +0x980 PilotDiameter, f64 +0x988 PilotLength.
  - TlToolBoring v2 (0x18075a850, base Drill): f64 +0x980 TaperAngle, raw4 +0x990 RadiusType, f64 +0x988 Shift.
  - TlToolReamer v5 (0x180855250): f64 +0x968 TipAngle, f64 +0x970 ChamferLength.
  - TlToolFeedMill v1 (0x180a37a40): f64 +0x968 TipDiameter, +0x970 LowerRadius, +0x978 UpperRadius,
    +0x990 LowerTaperAngle, +0x980 UpperTaperAngle, +0x988 ReliefTaperAngle.
  - TlToolBarrelMill v1 (0x180a3a3f0): f64 +0x968 BarrelLength, +0x970 ProfileRadius, +0x978 CornerRadius.

### 3.6 TlHolder (v4: `TlHolder::DeserializeV4` 0x1807281f0; v5: writer 0x18072add0)
- **v4**, in order:
  1. embedded **stored profile** (TlProfile, §3.7) = the holder outline;
  2. CString +0x130 LibraryName (0x1803a59c0; lambda 9d49f2c5);
  3. i32 +0x138 HolderType (0x1806c6620);
  4. CString +0x140 UpperConnectionSize (lambda 3f58050c);
  5. CString +0x150 LowerConnectionSize (lambda 49df786a);
  6. raw4 +0x148 UpperConnectionType;
  7. raw4 +0x158 LowerConnectionType;
  8. `raw4` into a local plus one `i32` skipped (8 bytes, discarded, NOT DECODED);
  9. CString → `SetGeometryFile`.
- **v5:** CString +0x130, u32 +0x138, CString +0x140, CString +0x150, raw4 +0x148, raw4 +0x158. The outline is in
  TlAssemblyItem v9.

### 3.7 TlProfile (v2: tlcore `DeserializeV2` 0x1800027d9) and segments
- **TlProfile:** `u32 n`, then n × `ReadObject(TlProfileSegment)`.
- **TlProfileSegment v1** (0x180003f21): `u8` → +0x48 Color (`GetColor`).
- **TlProfileLineSegment v1** (0x18000c351): 4 × f64 → +0x58, +0x60, +0x68, +0x70.
  - `GetEndpoint1` = (+0x58, +0x60) and `GetEndpoint2` = (+0x68, +0x70).
  - `GetBottomWidth` +0x58, `GetTopWidth` +0x68, `GetHeight` uses +0x60/+0x70.
  - So each line is **(x0 = radius, y0 = height, x1, y1)**.
- **TlProfileArcSegment v1** (0x1800011d6): 5 × f64 → +0x70, +0x78, +0x68, +0x58, +0x60.
  - `GetPointAtAngle` uses centre (+0x58, +0x60) and radius +0x68.
  - `GetLength` uses +0x68 and +0x78 (radius, sweep).
  - **+0x70 = start angle**, +0x78 = sweep: `GetArc` (tlcore 0x180002af9) returns the `a_2d` at +0x58 = {cx, cy, r, start,
    sweep}, and `LinearizeArc` (0x180005461) evaluates c + r (cos, sin)(start + i sweep / 100), i = 0..100 (100 lines).
    No arcs in any file; part_tools.c turns an arc into those 100 lines (2026-10-16).

### 3.8 Op params and coolant (tlcore)
- **TlOpParams v1** (0x18000d797; writer 0x1800063ed):
  1. embedded **TlCoolant**;
  2. f64 +0x48 MaterialSFMAdjust;
  3. f64 +0x50 MaterialFPTAdjust;
  4. f64 +0x58 FeedRate;
  5. f64 +0x68 PlungeRate;
  6. f64 +0x60 RetractRate;
  7. long +0x70 SpindleSpeed.

  Names are from the `TlOpParams::Get*` getters in tl_all.c.
- **TlMillingOpParams v1** (0x18000d5ad): bool +0x88 IsRoughTool, bool +0x89 IsFinishTool, f64 +0x90 StepoverRoughXY,
  +0x98 StepoverRoughZ, +0xa0 StepoverFinishXY, +0xa8 StepoverFinishZ.
- **TlHolemakingOpParams v1** (0x18000d25b): i32 +0x88 CannedCycleType, f64 +0x90 Peck1, +0x98 Peck2, +0xa0 PeckClear,
  +0xa8 ChipBreak, +0xb0 Dwell.
- **TlCoolant v1** (0x18000a380): 21 × i32 → +0x48 … +0x98.
  - Getters: +0x48 IsOldStyle; +0x4c / +0x50 Flood state / time; +0x54 / +0x58 Mist; +0x5c / +0x60 Tool (through-tool);
    then Custom1..7 state / time pairs at +0x64 … +0x98.
  - The value meanings are NOT DECODED. The parts hold 8, 2, 0, 2, 0, …

### 3.9 TlAssembly (v8: `TlAssembly::DeserializeV8` 0x180714060; v9: writer 0x1807179d0)
1. u32 +0x48 ToolNumber;
2. GUID → +0x78 **MainHolderID** (0x1803a59d0);
3. GUID → +0x60 **MainToolID** (0x1806c6b80);
4. u32 +0x4c MachineGroup;
5. u32 +0x50 DiameterOffsetNum;
6. u32 +0x54 LengthOffsetNum;
7. f64 +0x90 **Stickout** (`GetStickout` 0x1806c6e10; `GetToolProjection` 0x180716cd0 returns it when a main tool
   exists);
8. bool +0x58 IsDefaultAssembly;
9. long → `TlObject::SetSlot` (v8 only; in v9 the slot comes from TlObject v2);
10. f64 +0xa0 MaxRamp.

## 4. Field map to `Tool` (viewer/mcam_load.h)

"TM" = TlToolMill level and "EM" = TlToolEndmill; offsets are object members (§3).

| Tool field | source | notes |
|---|---|---|
| num, dia, name, type, … | legacy 0x288 block (already read) | also TM +0x808 = dia; TlTool +0x130 = num; TM +0x8b8 = type |
| oal | TM +0x7f0 OverallLength | **stick-out adjusted** (§5) |
| flute_len | TM +0x800 CuttingDepth | |
| shoulder_len | TM +0x7f8 | |
| shoulder_dia | TM +0x818 | |
| arbor_dia | TM +0x810 | |
| shank_type | TM +0x820 | 0 straight, 1 tapered, 2 reduced neck (tool_profiles.md §2) |
| taper_len | TM +0x828 | |
| neck_dia | TM +0x830 | |
| flutes | TM +0x858 (i16) | |
| helix_type, helix_angle, spindle_dir, req_pilot_dia | TM +0x85c, +0x860, +0x840, +0x838 | |
| chuck | TM +0x848 | |
| mfg_code | TlTool +0x140 | |
| head_num | TlTool +0x134 ToolStation | −1 / 0 in the parts |
| is_metric | TlAssemblyItem +0x84 | |
| guid, has_guid | TlObject ID | the part's own GUID: no part GUID matches a library ID (0 of 101 found in 131 .tooldb files) |
| desc | TlObject +0x20 | |
| corner_r | EM +0x988 / Drill +0x978 / Threading +0x9a0 / BarrelMill +0x978 | |
| radius_type | EM +0x978 / Threading +0x990 / Boring +0x990 | |
| taper_angle | EM +0x968 / Boring +0x980 | equals legacy block +0x20 (tip angle) for all 55 endmills |
| tip_dia | EM +0x970 / Threading +0x988 / FeedMill +0x968 | |
| chamfer_len | EM +0x980 / Reamer +0x970 | |
| tip_angle | Drill / Reamer / Threading +0x968 | |
| shoulder_angle | Drill +0x970 | |
| pitch, root_dia, thread_angle, nstarts, thread_shape | Threading +0x970, +0x978, +0x980, +0x994, +0x998 | |
| sec_radius_type, sec_corner_r, sec_chamfer, sec_chamfer_angle | SlotMill +0x990, +0x998, +0x9a0, +0x9a8 | unverified (§3.5) |
| drill_dia, drill_len | CenterDrill +0x980, +0x988 | unverified |
| pilot_dia, pilot_len | CounterBore +0x980, +0x988 | unverified |
| shift | Boring +0x988 | unverified |
| barrel_len, profile_r | BarrelMill +0x968, +0x970 | unverified |
| lower_r, upper_r, lower_taper, upper_taper, relief_taper | FeedMill +0x970, +0x978, +0x990, +0x980, +0x988 | unverified |
| feed, plunge, retract, rpm | TlOpParams +0x58, +0x68, +0x60, +0x70 | equal to the legacy block +0x30/+0x38/+0x40/+0x48 for 62/62 tools |
| sfm_adj, fpt_adj | TlOpParams +0x48, +0x50 | |
| is_rough, is_finish, step_rxy, step_rz, step_fxy, step_fz | TlMillingOpParams | |
| cycle, peck1, peck2, peck_clear, chipbreak, dwell | TlHolemakingOpParams | |
| holder | holder TlObject Name | e.g. "B2C3-0020", "Default Holder" |
| holder_rz, holder_n | holder stored profile, (x0, y0) per line plus the last (x1, y1) | (radius, height) |
| coolant, tool_mat, mat_name | NOT DECODED (§9) | |

Not in the struct yet, but needed:
- **true physical length** (TM +0x870);
- **assembly stickout** (TlAssembly +0x90);
- **slot** (TlTool / legacy +0x264).

## 5. Stick-out, OAL and holder placement

- **`TlMgr::CheckStickoutsAreLessThanPhysicalLength` (0x18073c180)** fails the TlMgr unless, for every assembly:
  - `|tool+0x7f0 (OAL) − assembly+0x90 (Stickout)| ≤ tol`;
  - `tool+0x870 (TruePhysicalLength) − OAL ≥ −tol`.

  **So the stored OAL is the stick-out (gauge length below the holder), and the real tool length is +0x870.**
- Data: OAL == stickout for 62/62 assemblies, and +0x870 ≥ OAL for 62/62. Three tools have +0x870 > OAL, all with
  +0x86c IsAdjustedForStickout = 1:
  - BRACKET4 thread mill: OAL 30, true 50;
  - "4mm Flat Endmill": 30 / 55;
  - buckle1 "1.5 Flat Endmill": 30 / 50.
- **Holder outline:** the holder's stored profile, x = radius, y = height.
  - y = 0 is the face toward the tool (every holder chain starts at (0,0) and ends with a line back to x = 0 at the
    top; toollib_net.md §4.1).
  - The holder is drawn from **z = OAL (= stickout)** above the tip:
    - the legacy backplot rule puts the holder face at z = `tool_len` (tool_profiles.md §5.2);
    - Mastercam's NCI tool block carries OAL at 20007[2] and the stickout at 20007[11] (§7);
    - and the TlMgr rule above makes the two equal.
  - The exact transform on the tool-system backplot path (offset = `GetHeight() − GetLength()`, tool_profiles.md §5.3)
    is NOT DECODED.
- **A tool added from a legacy record** (TL_ID without a TlMgr entry, formats/mcam_write.md 8): `TlMgr::SetupNewAssembly`
  (mccore 0x180745760) - when the legacy holder length / diameter (+0x878 / +0x880) are <= tol it calls
  `SetLegacyHolderData(50, 25)` mm or `(2, 1)` inch (+0x880 = first, +0x878 = second) and uses the holder made by
  FUN_180323600(IsMetric) (its shape NOT DECODED); otherwise a holder named by mccoreres string 0x1dc is built from them
  (vfunc +0xa8 with diameter and length).
- **"Default Holder"** (decoded 2026-10-16, `decomp/mccore_tooledit.c`): `FUN_180323600` = `make_shared<TlHolder>(IsMetric)`;
  `TlHolder::TlHolder(bool)` (0x180723f80): IsMetric, name = mccoreres 0x1dc (476 "Default Holder"), LibraryName "",
  HolderType / connection types 0, connection sizes "0" (DAT_180cf7cd4), outline = `MakeProfileFromHolderSegments`
  (0x18072a3d0) of one `TlHolderSegment` {top dia, bottom dia, height} = {50, 50, 25} mm / {2, 2, 1} inch
  (`MakeSegment` 0x18072ad00: line (bottom/2, y) -> (top/2, y + h), a line from x = 0 before it and one back to x = 0
  at the top) = (0,0)-(r,0), (r,0)-(r,h), (r,h)-(0,h). SetupNewAssembly (0x180745760): when the legacy holder diameter
  (+0x880) and length (+0x878) are both > tol, the holder is renamed (0x1dc again), `DeleteAllSegments` (vfunc +0xa0,
  0x180727150) and `AddSegment(dia, dia, len)` (vfunc +0xa8, 0x180726be0; registers xmm1 = xmm2 = +0x880, xmm3 =
  +0x878 at 0x180745867-0x180745882): a rectangle r = dia / 2, h = len; otherwise the 50 / 25 holder and
  `SetLegacyHolderData(50, 25)` (2, 1 inch). Data: every adj = 1 "Default Holder" of the 16 parts fits (r 3 h 25 for
  (len 25, dia 6), r 10 h 10 for (10, 20), r 25 h 25 for dia 0); the X9 writer did not store the 50 / 25 legacy data
  (A001 keeps dia 0: an X9 difference). Holders with adj = 0 come from other routes (CreateNewFromOp etc.).
- An assembly whose holder (MainHolderID) is not found is deleted by `SetupAssemblies` (mccore_tooladd.c l.~4410,
  FUN_18039e6e0 fails -> Delete); the TL_ID route then re-adds the slot with the default holder.
- `TlAssembly::Initialize` (0x180716da0) does not set MaxRamp (+0xa0): the files hold 0 or uninitialised denormals.

## 6. C sketch of the reader

```c
typedef struct { const uint8_t *b; size_t p, n; int err;
                 struct { char name[32]; int schema; } cls[512]; void *obj[512]; int map; } Ar;
/* primitives: u8/u16/i32/f64/guid/cstr with bounds checks; set ar->err on overrun */

static int ar_class_tag(Ar *a, char *name, int *schema) {      /* ReadClass, for level tags */
    uint32_t t = rd_u16(a);
    if (t == 0x7fff) t = rd_u32(a) | 0x80000000u;               /* big tag: bit 31 = class */
    if (t == 0xffff) { *schema = rd_u16(a); int l = rd_u16(a); rd_bytes(a, name, l);
                       add_class(a, name, *schema); return a->map++; }
    int i = t & 0x7fff;  if (!(t & 0x8000) || !is_class(a, i)) return a->err = -1;
    copy_class(a, i, name, schema); return i;
}
static void level(Ar *a, const char *want, int *schema) {
    char n[32]; ar_class_tag(a, n, schema); if (strcmp(n, want)) a->err = -1;
}
static void *read_object(Ar *a) {                                /* ReadObject */
    uint32_t t = rd_u16(a);  if (t == 0) return NULL;
    char name[32]; int schema;
    if (t == 0xffff) { schema = rd_u16(a); int l = rd_u16(a); rd_bytes(a, name, l); add_class(a, name, schema); a->map++; }
    else if (t & 0x8000) copy_class(a, t & 0x7fff, name, &schema);
    else return a->obj[t];                                       /* back-reference */
    void *o = new_obj(name); a->obj[a->map++] = o;
    serialize_chain(a, o, name);                                 /* §2 table: level() + L_xxx() per level */
    return o;
}
/* levels, X9 versions (§3): */
static void L_TlToolMill_v10(Ar *a, MillObj *m) {
    m->op = read_object(a);
    m->legacy_h_len = rd_f64(a); m->legacy_h_dia = rd_f64(a);
    m->oal = rd_f64(a); m->shoulder_len = rd_f64(a); m->flute_len = rd_f64(a); m->dia = rd_f64(a);
    m->arbor_dia = rd_f64(a); m->shoulder_dia = rd_f64(a);
    m->shank = rd_i32(a); m->taper_len = rd_f64(a); m->neck_dia = rd_f64(a); m->spindle_dir = rd_i32(a);
    m->adj_stickout = rd_u8(a); m->true_len = rd_f64(a); m->req_pilot = rd_f64(a);
    rd_cstr(a, m->geom_file); m->geom_level = rd_i32(a); m->scalable = rd_u8(a);
    m->mc_type = rd_i32(a); m->d_off = rd_i32(a); m->h_off = rd_i32(a); m->flutes = rd_i16(a);
    m->helix_type = rd_i32(a); m->helix_angle = rd_f64(a); rd_cstr(a, m->chuck);
    a->p += 3 /* var pitch, FBM, aggregate */ + 16 /* discarded */ + 0x78 /* +0x8f0 */;
    a->p += 16 * rd_i32(a);                                      /* GUID list +0x8c0 */
}
/* top level */
int tlmgr_read(const uint8_t *rec, size_t n, TlMgrOut *out) {
    const uint8_t *s = memmem(rec, n, "\xff\xff\x05\x00\x05\x00TlMgr", 11);  if (!s) return -1;
    Ar a = { s, 0, rec + n - s, .map = 1 };  int sch;  level(&a, "TlMgr", &sch);  /* sch must be 5 */
    for (int i = 0, k = rd_i32(&a); i < k; i++) { rd_guid(&a, key); add_item(out, key, read_object(&a)); }
    for (int i = 0, k = rd_i32(&a); i < k; i++) { int slot = rd_i32(&a); rd_guid(&a, id); add_slot(out, slot, id); }
    for (int i = 0, k = rd_i32(&a); i < k; i++) { rd_i32(&a); rd_guid(&a, id); }       /* lathe slots */
    return a.err ? -1 : (a.p == a.n ? 0 : -2);                  /* all files consume exactly n */
}
```

Then, for each TL_ID tool (legacy +0x264 = slot):
1. slot → assembly (slot table) → MainToolID / MainHolderID → objects;
2. fill the `Tool` fields per §4;
3. set `holder_rz` from the holder profile lines.

Reject unknown schemas instead of guessing. The reader in `tlmgr_parse.py` asserts on every version.

## 7. Verification

**Structure** (all 24 files): every TlMgr stream is consumed to the exact byte (A001 4227 B, BRACKET4 27757,
buckle1 8140, NAKTO 16032, thread-test 8051, test-cube 6114, tramming-rod 3163, the 7 buckle-v* files,
new-plate.mcam 7751, the 7 test-exports tram-*), and:
- every TlMgr key equals its object's ID;
- every slot-table entry names an assembly whose slot, and whose tool's slot, equal it (62/62);
- every TlTool legacy block equals exactly one TL_ID body (62/62; new-plate.mcam has no TL_ID records).

**Field order, independent check 1: Mastercam's own NCI.** Mastercam writes a tool block (NCI 20004 / 20007 / 20008)
before each tool change; the binary NCI is embedded in the part (`tools/nci_decode.py`).
- Pairs: 35 distinct (part, tool) pairs over 845 tool changes, matched by tool name and number, 16 parts.
- Every decoded field with more than one distinct value equals exactly one NCI slot in **every** pair (`nci_corr.py`):

| field | distinct values | NCI |
|---|---|---|
| OAL +0x7f0 | 4 | 20007[2] (and [11] = stickout, equal by §5) |
| shoulder len +0x7f8 | 5 | 20007[3] |
| flute len +0x800 | 6 | 20007[1] |
| dia +0x808 | 6 | 20004[2] |
| arbor dia +0x810 | 4 | 20007[4] |
| shoulder dia +0x818 | 6 | 20004[11] |
| taper len +0x828 | 2 | 20004[12] |
| neck dia +0x830 | 4 | 20004[20] |
| taper angle (EM +0x968) | 2 | 20004[5] |
| holder profile height | 2 | 20007[12] |

Each field lands in a different slot, so the 8 TlToolMill doubles cannot be permuted.

**Check 2: feeds.** TlOpParams feed / plunge / retract / rpm equal the TL_ID legacy feed / plunge / retract / rpm in 62/62
tools. This confirms that the 4th double is plunge (+0x68) and the 5th is retract (+0x60).

**Check 3: shipped libraries** (sqlite, read-only):
- **"3. FLAT ENDMILL"** (NAKTO T463) matches X9 `shared mcamx9/mill/Tools/mill_mm.tooldb` "3. FLAT ENDMILL" in all 19
  compared columns. Among them: OverallLength 75, ShoulderLength 60, CuttingDepth 50, ArborDiameter 3, ShoulderDiameter 3,
  TlShankType 0, TaperLength 0, NeckDiameter 3, FluteCount 4, TaperAngle 180, TipDiameter 0, TlRadiusType 0,
  CornerRadius 0, ChamferLength 0, IsMetric 1. The distinct 75 / 60 / 50 pin OAL / shoulder / flute.
- The old-style "   N. …" tools (geometry-file paths `C:\MCAMX\MILL\TOOLS\…`) were edited in the parts (e.g.
  "   10. DRILL" OAL 20). They match the X9 library "N. …" rows only in their unedited fields, so they are not used as
  evidence.
- **Holder "B2C3-0020"**: all 45 part copies match, segment for segment (18 lines, exact doubles), one of the two
  "B2C3-0020" items in `BT30-mm.tooldb` (22 + 23). Connection sizes "30" / "0" and types 0 / 0 match the library
  TlHolder row.

**Per-part spot results** (`report.py`):

| part | assemblies | tools / holders |
|---|---|---|
| NAKTO-BRAKE-BRACKET | 7 | 3. DRILL, 6 Flat Endmill (OAL 40, SL 25, FL 20, Dn 10, B2C3-0020), 3. FLAT ENDMILL, 3mm-3flute (OAL 25, SL 15, FL 15), 3./5. FLAT ENDMILL, 10. DRILL (OAL 20, SL 12, FL 10, tip 118, shoulder angle 90) |
| thread-test | 3 | Thread Mill 4.8 ×2 (OAL 50, SL 15, FL 1, Da 6, Dsh 2.9, shank 1 tapered, TL 3.5, Dn 5, pitch 1.5, shape 1, 4 flutes), 3mm-3flute |
| BRACKET4 | 11 | 4 Chamfer Mill (type 12, D 4, taper angle 45, Dsh 25, Dn 12), 10. CHAMFER MILL HSS (tip dia 0.1), 1mm-2-flute (D 0.8, Da 3.18, Dsh 1.18, shank 1), 4mm Flat (OAL 30, true 55), thread mill (OAL 30, true 50), … |
| buckle1 | 3 | 1.5 Flat Endmill (OAL 30, true 50, Da 4, shank 1), 10. DRILL, … |
| A001, test-cube-10mm, tramming-rod, buckle-v6…v12 | 2 / 2 / 1 / 3-4 | endmills with B2C3-0020 or Default Holder |
| new-plate.mcam (2017 format) | 4 | 2" FACE MILL (type 13, inch: OAL 2.1, FL 1.575, Da 0.75, taper 45, tip 2.492), 1/2 FLAT ENDMILL, 1/4 SPOTDRILL, 1/2 DRILL |

## 8. Notes for the implementer
- The TlMgr can hold assemblies whose tools no operation uses, and several assemblies per tool number. Key by slot
  (§1.1).
- Back-references: an object can be referenced again by index (e.g. a shared op-params object). Keep the map.
- One NAKTO assembly holds MaxRamp 3.6e-311 (a denormal, apparently uninitialised). Treat MaxRamp as data only.
- 2017-format files (`.mcam`) use TlObject v2, TlAssemblyItem v9, TlTool v15, TlToolMill v12, TlHolder v5 and
  TlAssembly v9 (§3). `tlmgr_parse.py` reads both version sets.

## 9. NOT DECODED
- ent+0x68..+0x378 of the subtype-33 record (before the archive). It holds no length of the archive (searched in all
  16 parts and new-plate.mcam); the writer keeps it.
- TlAssemblyItem:
  - +0x118 GUID: its name; TlToolMill V10 calls `SetToolGrade` when it is non-null;
  - the +0x100 GUID list;
  - (decoded 2026-10-16, §10.1: the v9 manufacturer object is a `TlManufacturer` v1 with no fields of its own.)
- TlToolMill:
  - the 16 bytes read and discarded after IsAggregate (v10);
  - the 0x78-byte block at +0x8f0;
  - the GUID list at +0x8c0;
  - (decoded 2026-10-16, §10.1: the v12 tool grade is a `TlToolGrade` v1, GUID +0x48.)
- TlHolder v4: the two discarded 4-byte values before GeometryFile.
- TlCoolant: the meaning of the state / time values, so `Tool.coolant` is not mapped; `Tool.tool_mat` / `mat_name`
  (TlToolMill +0x898 material object; not serialized in v10).
- The arc units (radians assumed from the .tooldb side, toollib_net.md 4.1). No arc segments in any file.
- Subclass layouts without samples: SlotMill, CenterDrill, CounterBore, Boring, Reamer, FeedMill, BarrelMill.
  The order is from the writers; X9 schema numbers unknown.
- The holder's exact placement transform in Mastercam's tool-system backplot; NCI 20007[5] / [6] (31.877 / 2.7178
  for B2C3-0020 = twice the radius and the height of its second vertex: meaning unconfirmed).
- (decoded 2026-10-16: the "Default Holder" rule, §5.)
- The ~103 bytes in which an operation's embedded op_tool_info differs from the TL_ID / TlTool block.
- Lathe classes (TlToolLathe, TlInsert, TlHolderLathe*) and the lathe slot table: none in the files.

## 10. The writer (`part_tools_write`, 2026-10-16)

`viewer/part_tools.c` rebuilds the subtype-33 record: ent +0 .. the archive kept, then the archive as Mastercam's store
branches write it, with the version of each class that the archive already has (X9 layouts for an .mcx-9, 2017 ones for
an .mcam). The exporter (`viewer/mcam_saveops.c`) calls it for the part tools edited here (§10.2).

### 10.1 Decode of the store side
- **`TlMgr::Serialize`** (mccore 0x180744620, `decomp/mccore_tlserialize.c` l.9445-9588), store branch:
  1. `SerializeClass(TlMgr)` (l.9445), schema 5;
  2. `PrepareObjectsForStoring()` (l.9470; 0x180743640, l.8393) returns a `std::map<TlID, shared_ptr<TlObject>>`: every
     object of the manager under its ID; an assembly as a clone with `RemoveAssemblyComponents`, plus `TlTool::Clone`
     of its main tool under its +0x60 (MainToolID) and `TlHolder::Clone` of its main holder under +0x78 (MainHolderID);
  3. i32 map size (l.9480), then per entry `Write(GUID, 16)` (l.9485) and `WriteObject` (l.9486);
  4. i32 slot-map size (+0x38, l.9511), per entry (`std::map<long, TlID>` at +0x30, so in slot order) i32 slot and the
     GUID (l.9537);
  5. i32 lathe slot-map size (+0x48, l.9562) and its entries (+0x40, l.9588).
- **Item order** = `std::less<TlID>` = `TlID::operator<` (tlcore 0x180004741, `tlcore_tlserialize.c` l.3234): Data1 (u32),
  Data2 (u16), Data3 (u16), then the 8 Data4 bytes. The item lists of all 24 files are in this order (not in memcmp order).
- **CArchive store rules** (mfc140; `WriteObject` = Ordinal_14063, `SerializeClass` = Ordinal_12658, `Write` =
  Ordinal_14002, as named in formats/mcam.md "Writer side"): null pointer = tag 0x0000; an object already written = its
  map index (u16; from 0x7FFF: 0x7FFF + u32); a new object = `WriteClass(its class)`, a new map index, then its
  `Serialize`. `WriteClass`: a class already written = 0x8000 | index (from 0x7FFF: 0x7FFF + u32 with bit 31), else
  0xFFFF, u16 schema, u16 name length, name, and a new index. One map counts classes and objects from 1. CString =
  `FUN_18035ee30` (`decomp/mccore_deserialize.c` l.43175): `AfxWriteStringLength` (Ordinal_2330: u8 length; 0xFF + u16
  from 255; 0xFF, 0xFFFF + u32 from 0xFFFE) and the bytes. bool = 1 byte `x != 0`.
- **Layouts written** (`FD_*` tables in part_tools.c; member offsets as §3):

| class | X9 (as the 2017 dll reads it) | 2017 (store branch) |
|---|---|---|
| TlObject | v1 tlcore DeserializeV1 0x180005ad3 (l.509): GUID +0x30, CString +0x18, +0x20 | v2 Serialize 0x18000cf9f l.1931-1938: + i32 GetSlot (+0x28) |
| TlAssemblyItem | v7 DeserializeV7 l.79 → V6 l.364 → V5 l.687 → V4 l.662 → V3 l.302 → V2 l.2864 (§3.2) | v9 Serialize 0x18000d8aa l.2696-2752 (§3.2) |
| TlTool | v13 DeserializeV13 (mccore_deserialize.c l.10352-10581) | v15 Serialize l.11124-11172; the block = GetOpToolInfo (vfunc +0xa8, l.11124) |
| TlToolMill | v10 DeserializeV10 l.18219-18728 (the CString after +0x838 = TlAssemblyItem +0x60 via SetGeometryFile; 16 bytes at l.18721 into a local) | v12 Serialize l.12025-12293 |
| Endmill 5 / Drill 3 / Threading 7 | Serialize l.11344-11373 / l.11271-11288 / l.12405-12478 (same schemas in X9 files) | same |
| Boring 2, CenterDrill 1, CounterBore 1, SlotMill 2, Reamer 5, FeedMill 1, BarrelMill 1 | no X9 sample | Serialize l.11216-11223, 13995-14001, 14042-14048, 14092-14115, 12317-12323, 14156-14202, 14247-14260 |
| plain TlToolMill (types 0 / 20, CreateNewFromOldTool 0x180834f20) | – | TlToolMill is the leaf (Router_MM / Router_Inch "Drill Block") |
| TlHolder | v4 DeserializeV4 l.8518-8780 (8 bytes at l.8749 dropped; last CString = +0x60) | v5 Serialize l.188-196 |
| TlAssembly | v8 DeserializeV8 l.7250-7424 (i32 slot before MaxRamp) | v9 Serialize l.59-124 |
| TlProfile 2 / Segment 1 / Line 1 / Arc 1 | tlcore l.32 / l.240 / l.1383 / l.6 | Serialize l.3223-3229 (u32 n, n × WriteObject) / l.230 / l.1304-1315 / l.191-206 |
| TlOpParams 1 / Milling 1 / Holemaking 1 / TlCoolant 1 | l.2409 / l.2176 / l.1943 / l.814 | l.634-655 (embedded coolant +0x78 first) / l.1357-1376 / l.788-807 / l.2778-2857 |
| **TlManufacturer 1** (AssemblyItem v9 +0xf0) | – | tlcore Serialize 0x18000a0b0 (l.3573): TlObject level, own level empty (DeserializeV1 0x180008b4d) |
| **TlToolGrade 1** (TlToolMill v12 +0x8a8) | – | tlcore Serialize 0x18000fc6d (l.3610): TlObject level, GUID +0x48 (DeserializeV1 0x180006244) |

- The readers' discarded bytes (TlToolMill v10 16 bytes, TlHolder v4 8 bytes), the +0x8f0 block, the GUID lists, the
  model-data blob and every other undecoded value are written back as read.
- New objects (only profile line segments, §10.2): `TlProfileLineSegment(x0, y0, x1, y1, colour)` (tlcore 0x18000bd7f,
  l.3465) on `TlProfileSegment(colour)` (l.3422; the colour of all 873 segments in the files is 0 = the default
  constructor's, l.3403) on `TlObject()` (0x180006bd6, l.3357): ID = CoCreateGuid (a random version-4 GUID: all
  segment IDs in the files have version nibble 4), empty name and description, slot -1 (+0x28 = 0xffffffff; GetSlot
  0x18000cd33 l.3543; the 2017 segments in new-plate.mcam hold -1).

### 10.2 What the exporter writes for a tool edited here
`mcam_save_part`: the part as loaded (mcam_load of the source) is the base; each of F's tools is paired with the base
tool of its slot (the first of F's tools with that slot owns it; a copy made here is a new tool, TL_ID route of
mcam_write.md 8). For each pair, only the fields that differ from the loaded tool are written:
- the objects: TlObject name / description, IsMetric, TlTool +0x130 / +0x134 / +0x140, the 19 TlToolMill fields of §4,
  the subclass fields (§3.5; the endmill / boring TaperAngle also takes the Tool's tip angle, as fill() maps it), the op
  params (§3.8), TlCoolant IsOldStyle (+0x48) from the block's +0x50 when the coolant changed (TlToolMill::GetOpToolInfo
  0x18083ff20 maps one to the other, mcam_write.md 8), TlAssembly ToolNumber / D / H offsets;
- an OAL change follows Mastercam's tool edit (decoded 2026-10-16, `decomp/mccore_tooledit.c`): the wizard's "Overall
  length" is TruePhysicalLength (toolmanager.toolwizardlaunch `TlDataInterop::ConvertMastercamMillTool` l.19980); on OK
  `ConvertExtensionsMillTool` (l.19837-19840): if !IsAdjustedForStickout (+0x86c) or TruePhysicalLength != v, then
  TruePhysicalLength = OverallLength = v. `ToolPageBase::EditTool` (0x180883900) puts the tool back with tool-system
  vfunc +0x1a8 = FUN_180a9d130 (clone of the assembly, main tool = clone of the edited tool, `TlAssembly::OnSetMainTool`
  0x180717710): adjusted -> Stickout (+0x90) = OverallLength, a TruePhysicalLength below it raised to it; not adjusted ->
  Stickout = OverallLength only when Stickout is 0. `CheckStickoutsAreLessThanPhysicalLength` only reports (its result
  is unused by SetupAssemblies). The op dialog's Holder page "overhang" (`HolderPageBase::OnKillFocusOverhangEdit`
  0x1803a8d60: shoulder <= s <= true length, OverallLength = s, Stickout = s) is a different edit, not reachable here.
  Limitation: when adjusted and v equals the true length, Mastercam changes nothing; our TL_ID body still carries v;
- the TlTool block, the TL_ID body (tp_tool) and the op_tool_info of operations written here: the stored bytes, except
  the bytes in which toollib.c's builders (`tool_op_info` = GetOpToolInfo, `tool_tp_tool` = SetToOldTool) give
  different results for the edited and the loaded tool. Measured on the 62 TL_IDs: tool_tp_tool of the loaded tool
  differs from the record only at +0x2b0..+0x2bf (legacy holder size, 62), +0x3c8 (2) and +0x408 (1) - those stay as
  stored. The archive block and the TL_ID body stay equal (§1.1);
- the holder: a changed name -> its TlObject name; a changed outline -> its stored profile: the line segments keep
  their objects (`SetLine`) when the count is the same and the profile has no arcs, else the profile gets new line
  segments (§10.1); line k = outline point k -> k + 1 (§4 holder_rz);
- a holder removed here (empty name or outline) is not written (an assembly without a holder: NOT DECODED);
- not written (the export message says so): a type of another tool class (`CreateNewFromOldTool`'s class table: a new
  object of that class - its defaults NOT DECODED), and every edit when the archive cannot be read (lathe classes,
  pre-X9 schemas).
- New tools stay on the TL_ID route (mcam_write.md 8): Mastercam's load (`TlMgr::Add(tp_tool)`, `SetupNewAssembly`)
  builds their tool, holder and assembly. Building them here would need SetFromOldTool, the class constructors and the
  default holder (FUN_180323600, shape NOT DECODED).

### 10.3 Verification (2026-10-16)
The harness was the session scratch folder `tlmgr_w/`; it is kept as `tools/tests/tlmgr_roundtrip.c` (was `rt.c`),
`tools/tests/tlmgr_edit_test.c` (was `edit_test.c`; modes none / geom / holder / holdersame / num / all / type / mat),
`tools/tests/tlmgr_edit_batch.sh` (every part × mode: export, reload, `tlmgr_verify.py` archive check, TL_ID == block),
`tools/tests/tlmgr_rt_all.sh` (export round trip over the user parts) and `tools/tests/tlmgr_verify.py` (archive check
on `tlmgr_parse.py`). The scripts expect the scratch layout (`parts/`, `out/`, `../part_tools`): adjust paths to rerun.
- **Round trip** (`rt.c`, no edits): byte-identical record for all 16 user parts (copies of `<user-parts>/*.mcx-9`), the 8 test
  exports (new-plate.mcam 2017), and 128 of the 141 archives in 144 Mastercam shared / sample files (mill / router
  defaults libraries with TlManufacturer / TlToolGrade objects and plain TlToolMill tools, Samples, tool .mcam files). The
  13 others are refused (lathe classes; X7/X8 schemas TlTool 12 / 14, TlAssemblyItem 6 / 8).
- **No false edits:** exporting each of the 17 parts unchanged finds no tool to rewrite ("nothing to add").
- **Edits** (`edit_test.c` × 17 parts × geometry / number / all tools / holder name + outline / outline only / material
  code only): 102 / 102 exports reload in mcam_load with the new values (OAL, flute and shoulder length, diameter, corner
  radius, name, number, holder name and outline), the archive parses to its last byte in the independent
  `tlmgr_parse.py`, and every TL_ID body equals its TlTool block. `tools/mcam_export.py` reads the new name / diameter
  from the TL_ID in 16 / 16 X9 parts (it does not read the TlMgr archive, so it lists no tools for new-plate.mcam, which
  has no TL_ID records; mcam_load reads the new values there). A type of another class is refused with the note. An edited stored op exported with its
  edited tool (tramming-rod op 1) carries the new diameter / corner radius; the rest of its block is its own.
- Regressions: fileop 0 / 189, post 0 / 16, export round trip 188 / 188 (0 new tools, 0 rewritten).

## 11. Code: `viewer/part_tools.c` / `part_tools.h`

| Function / struct | Behaviour | Spec |
|---|---|---|
| `Ar`, `need` / `u8` / `u16` / `u32` / `f64` / `raw` / `cstr` / `count` / `guid_list` | bounds-checked archive primitives; any overrun sets `err` and rejects the whole archive | §2 |
| `tag` / `new_class` / `class_ref` / `level` / `read_object` / `add_map` | CArchive tags: one map for classes and objects counted from 1, back-references, big tags; nesting limited to `MAX_DEPTH` 8 (ours: a recursion guard) | §2 |
| `lev` | one case per class level and schema (X9 readers and 2017 writer layouts); an unknown class or schema rejects the archive | §3 |
| `parse` | finds the `FF FF 05 00 05 00 "TlMgr"` class tag in the record, reads n × {GUID, object}, the slot table and the lathe slot table; must end on the record's last byte | §1, §2 |
| `resolve` | slot → slot table → TlAssembly (its slot must match) → MainToolID / MainHolderID (the tool's slot must match) | §1.1 |
| `fill` | the §4 field map into `Tool`, sets `has_extras` | §4, §5 |
| `part_tools_apply` | for each TL_ID tool: `resolve`, then `fill` only when the TlTool block equals the TL_ID body byte for byte | §1.1 |
| `part_tools_list` | every slot-table entry as a Tool (used for 2017 parts without TL_IDs) | §1.1 |
| `WM` / `WO` / `WI`, `w_parse`, `w_levels` | the writer's model: the same grammar, every item kept as its bytes | §10.1 |
| `ww_*`, `w_emit`, `guid_less` | store side: WriteObject / WriteClass / AfxWriteStringLength, items in `TlID::operator<` order, slot map in slot order | §10.1 |
| `apply_edit` / `tf_apply` (`TF_*` field tables) | writes only the fields where NOW differs from BASE; class change → `PT_CLASS`, unknown slot → `PT_NOSLOT`; coolant from block +0x50; OAL → stickout / true length; number / D / H → TlAssembly; block bytes that differ | §10.2 |
| `new_guid` / `new_line` / `set_outline` | new holder line segments (TlObject(): v4 GUID, empty strings, slot -1; colour 0); `/dev/urandom`, else a splitmix64 fallback (ours: no CoCreateGuid on Linux) | §10.1, §10.2 |
| `part_tools_write` | ent +0 .. archive start copied, then the rebuilt archive | §10 |

Callers: `viewer/mcam_load.c` (apply / list), `viewer/mcam_saveops.c` (write). Tests: `tools/tests/tlmgr_roundtrip.c`,
`tlmgr_edit_test.c` and the scripts of §10.3.

### 10.4 Decoded 2026-10-16, NOT yet implemented (`decomp/mccore_tooledit.c`)
- **Deleting a part tool** (Tool Manager delete FUN_180872b80 -> tool system vfunc +0x68 FUN_180a99210 -> PartToolMgr
  vfunc +0x48 FUN_180ab2a80 -> lambda FUN_180aae6a0): `TlMgr::IDBySlot`, `TlMgr::Delete(ID)` (0x18073d100: the assembly
  with its tool and holder leaves the object map and the slot map); then FUN_180ab1c90(slot, delete ops): unless an op
  uses the tool and the user said no, every op using it (`list_op_by_tl`) gets ent +0xfc = 1, +0x1200..+0x1214 = 0 and,
  when EnableFeedRateLock is off, its op_tool_info (+0x348, 0x288 bytes) zeroed, else only +0x5ac (slot) = 0;
  EnableFeedRateLock = config 17027 (0x4283, FUN_1801bac10; default true FUN_1803bc8e0; the user's mcamx9 mcamxm.config
  line 644 "17027. ... n"). Then the TL_ID record is deleted (`delete_ent`). In the archive: drop the assembly, its tool
  and holder objects (unless another assembly names them) and the slot entry; drop the TL_ID record (otherwise the
  load-time TL_ID route re-adds the tool). Not done: the exporter still keeps a deleted tool.
- **Removing a holder**: `SetMainHolder(null)` -> `OnSetMainHolder` MainHolderID = GUID_NULL (DAT_180e8efe0 = 16 zero
  bytes); the holder object is not stored; on load SetupAssemblies deletes the assembly and the TL_ID route rebuilds it
  with the default holder (§5). Not done.
- **A library holder** (`TlAssembly::SetMainHolder` 0x1803abf30 = `TlHolder::Clone` of the library holder; the .tooldb
  holder comes in via toolwizardlaunch `ConvertExtensionsHolder` l.20688-20755: UpperConnectionSize / Type,
  LowerConnectionSize / Type, HolderType, LibraryName, plus the TlAssemblyItem fields). Not done: toollib.c's Holder
  keeps only name + outline, and Tool (mcam_load.h, another stream's file) has no field for the rest.
- **New tools built in the archive**: the constructor chain is partly decoded (TlAssembly() / Initialize, TlHolder(bool),
  TlTool() / Initialize 0x1807546e0, TlToolMill() / Initialize 0x180840c40 (OAL 2, shoulder 1.2, flute / dia / arbor /
  shoulder dia / pilot 1, true length 2, +0x8f0 zeroed, op params FUN_180762b60, tool material from MaterialID or the
  block's +0x54), TlToolEndmill / Drill Initialize, SetupNewAssembly, SetFromOldTool). NOT DECODED: TlAssemblyItem() and
  the op params / coolant constructors (tlcore), the TlToolMaterial ID mapping, and no 2017 sample of the TL_ID route
  to verify against (the X9 parts differ: legacy holder data). New tools stay on the TL_ID route.
