# Mastercam 2017 tool-library data layer (`.tooldb`), from the .NET code

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

This spec covers the tool-library data layer only. It is decoded from the ILSpy output of Mastercam 2017's managed
assemblies and checked against the shipped `.tooldb` files. It extends `formats/tooldb.md` (container, `OpToolInfo`
offsets) and `formats/mcam.md` (the part file's `TlMgr`).

**Rule (no improvisation).** Every statement cites its source. Anything without a source is marked **NOT DECODED**.

## 0. Sources and abbreviations

| Abbrev. | Path (under `~/mastercam-re/dotnet/` unless absolute) | What it is |
|---|---|---|
| `SQL:create` | `toolmanager.database/ToolManager.Database.Utilities.SqlScripts.CreateToolDatabase.sql` | embedded resource: the DDL Mastercam runs to create a new `.tooldb` |
| `SQL:base` | `toolmanager.database/ToolManager.Database.Utilities.SqlScripts.InsertBaseData.sql` | embedded resource: seed rows for a new library |
| `SQL:defaults` | `toolmanager.database/ToolManager.Database.Utilities.SqlScripts.InsertDefaultsData.sql` | seed rows for `databasedefaults.tooldb` |
| `CSDL` / `MSL` / `SSDL` | `toolmanager.database/ToolDatabase.ToolDatabase.{csdl,msl,ssdl}` | Entity Framework model: entity inheritance, associations, column mapping |
| `ENT/<T>` | `toolmanager.database/ToolManager.Database.ToolDatabase/<T>.cs` | EF entity classes (one per table) |
| `TDB` | `toolmanager.database/ToolManager.Database.Services/ToolDatabase.cs` | the Tool Manager's database service (load / add / delete / commit) |
| `XLAT` | `toolmanager.toolwizardlaunch/-Module-.cs`, functions `TlDataTranslation.Convert*` (lines 6781-12060) | C++/CLI bridge between the Tool Manager entities and Mastercam's native `Cnc::Tool::*` objects. `ConvertExtensions*` = DB entity → native; `ConvertMastercam*` = native → DB entity |
| `PU` | `tlcoreapi/TlCore.Interop/ProfileUtilities.cs` | managed entry points into native `TlCore.dll` profile code |
| `PG/<T>` | `toolmanager.database/ToolManager.Database.ProfileUtilities/<T>.cs` | profile and assembly generators |
| `IOP/<T>` | `toolnetapi/Cnc.Tool.Interop/<T>.cs` | managed wrappers and enums for `Cnc::Tool` |
| `VC` | `toolmanager.database/ToolManager.Database.ValidityChecking/ToolValidityChecker.cs` | tool validity rules |
| `DATA:<lib>` | shipped libraries: `<windows>/Users/Public/Documents/shared Mcam2017/{mill,router}/Tools/*.tooldb`, `~/mastercam-re/files/databasedefaults.tooldb` and `databasecatalog.tooldb` (all opened read-only) | verification |

Kept in the repo from the session scratch folder `toollib_agent/`: the mccore SQL strings `decomp/mccore_tooldb_sql.txt`
and the profile-row queries `tools/tests/tooldb_profile_q.py`, `tools/tests/tooldb_profile_q2.py` (were `prof/q.py`,
`prof/q2.py`). The other verification scripts cited below (`dump.py`, `cmp.py`, `defs2.py`, `corr.py`, `corr2.py`) were
not kept; their results are recorded in this spec.

### Architecture: who touches the file

The architecture determines what can be decoded:

- **The Tool Manager (WPF, `toolmanager.*.dll`)** reads and writes `.tooldb` through Entity Framework over
  System.Data.SQLite (`CSDL`/`MSL`/`SSDL`; `toolmanager.database/ToolManager.Database.Services/DatabaseContext.cs` L47-49
  `new ToolDatabaseEntities(connectionString)`).
- **Mastercam's native tool objects (`Cnc::Tool::TlTool` and others, in mccore / `TlCore.dll`)** hold the same data. `XLAT`
  copies field by field between the two.
- **The `OpToolInfo` blob** (648 bytes) is produced and consumed **only by native code**:
  - Write (native → DB): `XLAT ConvertMastercamTool` (L7380-7428) calls native vtable slot +168 and copies its 648 bytes
    into `TlTool.OpToolInfo`.
  - Read: `XLAT ConvertExtensionsTool` (L7305-7378) hands the bytes to vtable slot +176, but only if
    `OpToolInfo.LongLength == 648`. Otherwise the blob is ignored.
  - No managed code names the fields inside `op_tool_info`. The type is opaque in .NET
    (`toolnetapi/op_tool_info.cs`, `toolmanager.toolwizardlaunch/op_tool_info.cs`: `StructLayout Size = 648`, no members).
  - The field offsets known so far come from correlating the blob with SQL columns (`formats/tooldb.md`, and §3.5 below).
- **Parametric tool outlines** are computed in native `TlCore.dll` (`PU`, export `RetrieveParametricToolProfile`).
  The math is not in .NET (§4); it was decoded from tlcore.dll in `formats/tool_profiles.md` (port: `viewer/toolprofile.c`).

## 1. Tool types and class hierarchy

### 1.1 `MCToolType`
Source: `IOP/MCToolType.cs`. It is declared without explicit values, so value = position. The Tool Manager's twin
`toolmanager.database/ToolManager.Database.Resources/ToolTypeList.cs` uses explicit values identical to these, but
omits 20. UI labels come from `ToolManager.Database.Resources.DatabaseEnumStrings.resx`.

| # | Name | Entity class / deepest table (`toolmanager.database/.../Services/ToolTypeFactory.cs` ctor L14-38) | Op-params subtype (constructors; verified in `DATA`) |
|---|---|---|---|
| 0 | Undefined ("CustomTool") | TlToolMill (no subtype row) | plain TlOpParams |
| 1 | CenterDrill | TlToolCenterDrill (→ TlToolDrill) | Holemaking |
| 2 | SpotDrill | TlToolDrill | Holemaking |
| 3 | StandardDrill | TlToolDrill | Holemaking |
| 4 | RightHandedTap | TlToolThreading | Holemaking, CannedCycleType 3 |
| 5 | LeftHandedTap | TlToolThreading; SpindleDir = 1 (`ENT/TlToolThreading` L188-228) | Holemaking, CannedCycleType 3 |
| 6 | Reamer | TlToolReamer | Holemaking |
| 7 | FineBoringTool | TlToolBoring (→ TlToolDrill) | Holemaking |
| 8 | CounterBore | TlToolCounterBore (→ TlToolDrill) | Holemaking |
| 9 | CounterSink | TlToolDrill | Holemaking |
| 10 | FlatEndmill | TlToolEndmill | Milling |
| 11 | BallNosedEndmill | TlToolEndmill | Milling |
| 12 | ChamferMill | TlToolEndmill | Milling |
| 13 | FaceMill ("Face Mill / Shell Mill") | TlToolEndmill | Milling |
| 14 | SlotMill ("Slotting Tool or Saw", i.e. slot / keyseat) | TlToolSlotMill (→ TlToolEndmill) | Milling |
| 15 | RadiusMill ("Roundover Tool") | TlToolEndmill | Milling |
| 16 | DoveMill | TlToolEndmill | Milling |
| 17 | TaperMill | TlToolEndmill | Milling |
| 18 | LollipopMill | TlToolEndmill | Milling |
| 19 | BullNosedEndmill | TlToolEndmill | Milling |
| 20 | BlockDrill | **no factory entry**; no-op branches in `ENT/TlToolEndmill` L292-294 and `VC` L127-128. No row in any shipped DB. **NOT DECODED** | — |
| 21 | EngraveTool | TlToolEndmill | Milling |
| 22 | BradpointDrill | TlToolDrill | Holemaking |
| 23 | FeedMill ("High Feed Mill") | TlToolFeedMill | Milling, IsFinish = false (`ENT/TlMillingOpParams` L131-140) |
| 24 | ThreadMill | TlToolThreading | Milling |
| 25 | BarrelMill | TlToolBarrelMill | Milling |

- **Data check:** class = deepest table, and the mapping agrees with every row of mill_mm, mill_inch, router_mm,
  router_inch, databasecatalog and databasedefaults (row counts in §8).
- **Router:** there are no router-specific types or tables. A grep for "router" in toolmanager.* and the interop code
  finds nothing, and the router libraries use the same MCToolTypes and tables (`DATA:router_*`).
- **Lathe** tools use separate tables (`TlToolLathe*`, `TlInsert*`, `TlHolderLathe*`). They are out of the mill scope
  and the converters are **NOT DECODED** (§2.4).
- **`TlToolType` table vs MCToolType:** no code maps one to the other. Every shipped tool has `TlToolTypeID` =
  Guid.Empty (`DATA`). **The tool kind is MCToolType plus the deepest table.**
- **View models:** `AssemblyItemViewModelFactory.GenerateViewModel` (L54-66) picks by class name, except
  TlToolThreading, which picks by MCToolType: 4/5 → `TlToolTapViewModel`, 24 → `TlToolThreadMillViewModel`.

### 1.2 Property → table per type
§2.4 has the column lists. The class decides which leaf columns exist:

| Class (table) | Own columns |
|---|---|
| TlToolMill | OverallLength, ShoulderLength, CuttingDepth (flute length), OverallDiameter, ArborDiameter (shank), ShoulderDiameter, NeckDiameter, TaperLength, TlShankType, FluteCount, TlSpindleDir, TlHelixType, HelixAngle, RequiredPilotDiameter, IsVariablePitch, IsScalable, MfgToolCode, Chuck, ToolFileName, DiameterOffsetNum, LengthOffsetNum, MCToolType, TlGradeID, TlOpParamsID |
| TlToolEndmill | TipDiameter, TaperAngle, TlRadiusType (= TlCornerType), CornerRadius, ChamferLength |
| TlToolSlotMill | SecondaryRadiusType, SecondaryCornerRadius, SecondaryChamferDistance, SecondaryChamferAngle |
| TlToolDrill | TipAngle, ShoulderAngle, CornerRadius |
| TlToolBoring | TlRadiusType, TaperAngle, Shift |
| TlToolCenterDrill | DrillLength, DrillDiameter |
| TlToolCounterBore | PilotLength, PilotDiameter |
| TlToolReamer | TipAngle, ChamferLength |
| TlToolThreading | TipDiameter, TipAngle, TlRadiusType, CornerRadius, ThreadPitch, TlThreadShape, RootDiameter, ThreadAngle, NStarts |
| TlToolBarrelMill | BarrelLength, CornerRadius, ProfileRadius |
| TlToolFeedMill | TipDiameter, LowerRadius, UpperRadius, LowerTaperAngle, UpperTaperAngle, ReliefTaperAngle |

**Angle conventions seen in the defaults and data.** These are data observations, not code:
- `TlToolEndmill.TaperAngle` = 180 for flat/ball/bull/roundover/slot (meaning "no taper"); 45 for chamfer and face
  mills; 30 dovetail; 10 taper mill; 15 engrave.
- `TlToolDrill.TipAngle` = 118 standard drill, 90 spot, 82 countersink, 180 counterbore and brad point.
- In the blob these share offset 0x20 (§3.5).

### 1.3 Enums (all from `IOP/<Name>.cs`; declared without explicit values, so value = position)

| Enum | Values |
|---|---|
| TlCornerType (`TlRadiusType` columns, `SecondaryRadiusType`) | 0 NONE, 1 CHAMFER, 2 RADIUS, 3 FULL_RADIUS, 4 ROUNDOVER |
| TlRadiusType | 0 NONE, 1 CORNER, 2 FULL. No user found; the DB columns named `TlRadiusType` are cast to **TlCornerType** in `XLAT` |
| TlShankType | 0 Straight, 1 Tapered, 2 Reduced |
| TlSpindleDir | 0 Clockwise, 1 Counterclockwise, 2 Static |
| TlHelixType | 0 None, 1 Up, 2 Down, 3 Compression |
| TlThreadShape | 0 Unified, 1 Metric, 2 Acme, 3 Acme_Stub, 4 Buttress_Pull, 5 Buttress_Push, 6 NPT |
| TlDisplayType (`CustomDisplayType`) | 0 AUTO_TOOL, 1 CUSTOM_FILE, 2 CUSTOM_LEVEL, 3 CUSTOM_STORED |
| TlCutQuality | 0 ROUGH, 1 SEMI_FINISH, 2 FINISH |
| TlCutSource | 0 MANUFACTURER, 1 USER |
| TlMaterialHardness (`TlWorkMaterial.HardnessUnit`) | 0 BRINELL, 1 ROCKWELL_A, 2 ROCKWELL_B, 3 ROCKWELL_C |
| TlMaterialISOValue (`TlWorkMaterial.ISOGroup`) | 0 UNDEFINED, 1 P, 2 M, 3 K, 4 N, 5 S, 6 H |
| UpperConnectionType (`TlHolder`) | 0 BT, 1 DV, 2 CAT, 3 ISO, 4 HSK, 5 CAPTO, 6 OD, 7 KM. Data: mill_mm BT holders = 0, CAT = 2, HSK = 4 |
| LowerConnectionType (native) | 0 ID, 1 OD, 2 CHUCK, 3 ER, 4 DA, 5 SYOZ, 6 EOC. **Conflict:** the UI enum `ToolManager.Database.Resources/LowerConnectionTypeList` has TG at 4 (ID, OD, Chuck, ER, TG, DA, SYOZ, EOC), and `TlHolderViewModel.cs` L165-169 casts the same int to it. Values ≥ 4 are ambiguous; shipped data uses only 0 |
| CannedCycleType (`ToolManager.Database.Resources/CannedCycleTypeList`) | 0 Drill/Counterbore, 1 PeckDrill, 2 ChipBreak, 3 Tap, 4 Bore #1 (feed out), 5 Bore #2 (rapid out), 6 Fine Bore (shift), 7 RigidTapping, 8-19 Custom9..Custom20 |
| TlCompensationType, TlFeedSpeedUnits, TlMaterialGroup, TlActiveSpindle, TlActiveTurret | empty native enums in `toolnetapi/*.cs`: **NOT DECODED** |

## 2. READ: schema and mapping

### 2.1 Container
- SQLite 3, `page_size=1024`, `encoding UTF-8`, `auto_vacuum=0` (`SQL:create` header pragmas). All shipped libraries match:
  page_size 1024, journal_mode `delete`, user_version 0 (`DATA`, `dump.py`).
- **51 tables.** `SQL:create` creates exactly the tables found in mill_mm / databasedefaults, with identical column
  name, type, notnull and pk (`cmp.py`: no differences).
  - Older files that were migrated (router_mm, BT40-mm) differ only in `TlToolFeedMill` column order
    (`LowerTaperAngle` second).
  - router_mm also has a stray `Users(User TEXT, LastModified DateTime)` table. This is the Tool Manager's undo
    bookkeeping (`toolmanager.database/ToolManager.Database.Models/UndoRedo.cs` SetupTables L721-754).
  - Readers must therefore address columns **by name**. EF does this: `SSDL` declares columns by name.
- Undo bookkeeping that the Tool Manager adds while a library is open for editing:
  `UndoLog_<Doc>`, `UndoTransaction_<Doc>`, `UndoTableType`, `Users`, plus AFTER triggers (`UndoRedo.cs` L721-1227).
  These are dropped again in `FinalizeUndoRedo` (L177-278) unless another user was active in the last 2 days.
  A reader must ignore them; a writer should not create them.
- **GUIDs:** every `ID` / `*ID` column is a 16-byte BLOB in .NET `Guid.ToByteArray()` order: Data1, Data2, Data3
  little-endian, then 8 bytes as-is.
  - Code: `XLAT ConvertToGUID` L6781 copies `ToByteArray()`; `ConvertFromGUID` L6792.
  - Cross-check: `ENT/TlToolMill` L785 `new Guid("F9C5C3CC-570B-410D-A827-8DAB6BBF50E1")` is the row
    `x'CCC3C5F90B570D41A8278DAB6BBF50E1'` in `SQL:base`.
  - "No reference" is stored as `Guid.Empty`, i.e. 16 zero bytes, not NULL (`ENT/TlAssemblyItem` ctor L580-602;
    `DATA:databasedefaults` `TlToolTypeID` / `TlSourceID` all zero).
- **Schema version:** `_Header(id GUID, version INT, DataVersion INT)`.
  - The Tool Manager requires `version == 20`: >20 gives error 27, any other mismatch 28. If the column `version` is
    missing it retries `SchemaVersion` (`toolmanager.database/.../DatabaseConnectionManagement/DatabaseConnectionManager.cs`
    L14-18, L147-197).
  - Defaults and catalog DBs additionally need `DataVersion == 0`.
  - All shipped files: `version 20, DataVersion 0`; each file has its own `_Header.id` (`DATA`).
- `_UpdateLog(Update, Date, PreviousSchemaVersion, PreviousDataVersion, NewSchemaVersion, NewDataVersion,
  MCMajorVersion, MCMinorVersion)` is written only by native code. The string
  `INSERT INTO _UpdateLog VALUES (%d, '%s', %d, %d, %d, %d, %d, %d)` and `SELECT COALESCE(MAX("Update")+1, 0)` are in
  mccore.dll (`decomp/mccore_tooldb_sql.txt` L83-84).
  - Data, mill_inch: `(0,'Tue Nov 11 15:07:54 2014\n',17,0,18,0,18,65)`, `(1,…,18,0,19,0,18,65)`,
    `(2,'Wed Oct 07 15:36:11 2015\n',19,0,20,0,19,68)`.
  - The date is ctime format with a trailing `\n`.

### 2.2 Entity inheritance = table-per-type
`CSDL` declares these base types. `MSL` maps each type to its own table with `IsTypeOf(...)` and no discriminator
conditions. A row's class is therefore the **deepest table containing its ID**.

```
TlAssemblyItem ─┬─ TlTool ─┬─ TlToolMill ─┬─ TlToolEndmill ── TlToolSlotMill
                │          │              ├─ TlToolDrill ─┬─ TlToolBoring
                │          │              │               ├─ TlToolCenterDrill
                │          │              │               └─ TlToolCounterBore
                │          │              ├─ TlToolReamer
                │          │              ├─ TlToolThreading
                │          │              ├─ TlToolBarrelMill
                │          │              └─ TlToolFeedMill
                │          └─ TlToolLathe ─┬─ TlToolLatheCustom
                │                          └─ TlToolLatheISO
                ├─ TlHolder
                ├─ TlHolderLathe ── {Boring, Drill, Grooving, Threading, Turning}
                └─ TlInsert ── {Grooving, LatheDrill, Threading, TurningBoring}
TlOpParams ─┬─ TlHolemakingOpParams
            └─ TlMillingOpParams
TlMaterial ─┬─ TlToolMaterial
            └─ TlWorkMaterial
```

Every subtype table is `ID GUID PRIMARY KEY REFERENCES <parent>(ID)` (`SQL:create`). The same order is used
natively: `XLAT ConvertMastercamToolToExtensionsTool` (L9327-) tests `dynamic_pointer_cast` from the most derived
class down: SlotMill, Endmill, Boring, CounterBore, CenterDrill, Drill, Reamer, Threading, BarrelMill, FeedMill, then
plain TlToolMill. Note that `TlHolderLatheLiveCenter` exists natively and in `XLAT` (L8929-9110) but has **no table**
in `SQL:create`.

### 2.3 Joins (`CSDL` associations; FK columns from `SQL:create`)

| From | Column | To | Multiplicity |
|---|---|---|---|
| TlAssemblyItem | TlToolMaterialID | TlToolMaterial.ID (→ TlMaterial.ID) | 0..1 |
| TlAssemblyItem | TlToolTypeID | TlToolType.ID (→ TlToolGroup via TlToolGroupID) | 0..1 |
| TlAssemblyItem | TlManufacturerID | TlManufacturer.ID | 0..1 |
| TlAssemblyItem | TlSourceID | TlAssemblyItem.ID (self) | 0..1 |
| TlToolMill | TlOpParamsID | TlOpParams.ID | 0..1 (code needs it, §3) |
| TlToolMill | TlGradeID | TlToolGrade.ID (→ TlManufacturer via ManufacturerID) | 0..1 |
| TlOpParams | TlCoolantID | TlCoolant.ID | 0..1 (code needs it, §3) |
| TlProfileData | ItemID | TlAssemblyItem.ID; PK (ItemID, Segment) | * |
| TlRelations | ParentID | TlAssemblyItem.ID; PK (ParentID, ChildID); ChildID has no FK | * |
| TlAssembly | MainTool / MainHolder | TlTool.ID / TlHolder.ID | 0..1 |
| TlAssemblyComponent | TlAssemblyID, TlAssemblyItemID, ParentID, M/U/CTransformID | TlAssembly, TlAssemblyItem, TlAssemblyItem, TlMatrix4x4 ×3 | PK (TlAssemblyID, TlAssemblyItemID) |
| TlCutParam | TlToolGradeID, TlToolTypeID, TlOpTypeID, TlManufacturerID, TlWorkMaterialID | grade, tool type, op type, manufacturer, work material | 0..1 each |
| TlOpType | TlOpGroupID | TlOpGroup | 0..1 |
| TlToolLathe | HolderID / InsertID | TlHolderLathe / TlInsert | 0..1 |
| TlToolToTlOpType | (TlToolID, TlOpTypeID) | TlTool / TlOpType, ON DELETE CASCADE | **not in the EF model** (0 hits in csdl/ssdl/msl); no .NET reader |
| TlAuxData | PK (ID, Class, Tag) | none | not in the EF model; no .NET reader. Empty in all shipped files. Native DDL string in mccore (`mccore_tooldb_sql.txt` L85) |

Shipped data:
- Every TlToolMill has its **own** TlOpParams row, never shared and never equal to the tool ID; likewise every TlOpParams
  has its own TlCoolant (`DATA` all mill/router/catalog/defaults libraries).
- Holder libraries (`BT*`, `CT*`, `HSK*`) contain only TlAssemblyItem + TlHolder + TlProfileData.

### 2.4 Column → tool property (READ = `XLAT ConvertExtensions*`, which is what Mastercam loads into the native tool)
The default is the value used when the column is NULL (`GetValueOrDefault(x)` in `XLAT`).

**TlAssemblyItem**: `XLAT ConvertExtensionsAssemblyItem` L6797-7106 / `ConvertMastercamAssemblyItem` L7108-7250.

| Column | Native setter | Default |
|---|---|---|
| ID | TlObject vslot +48 (SetID) | — |
| CatalogID | SetCatalogID | — |
| GeometryFile | SetGeometryFile (also written from `TlToolMill.ToolFileName`, below) | — |
| ImageFile | SetImageFile | — |
| IsMetric | SetIsMetric (`.Value`, so NULL throws) | none |
| Location | SetLocation | — |
| TlToolMaterialID | SetMaterialID | Guid.Empty |
| TlToolTypeID | SetToolTypeID | Guid.Empty |
| Name / Description | TlObject.SetName / SetDescription | — |
| Quantity | SetQuantityOnHand | 0 |
| ThruCoolant | SetThruCoolant | false |
| TlSourceID | SetSourceID | Guid.Empty |
| GaugeDiameter / GaugeLength | SetGaugeDiameter / SetGaugeLength | 0.0 |
| TlProfileData rows | sorted by `Segment` (std::map) → `TlProfileFactory.MakeProfile` → SetStoredProfile | empty profile |
| TlManufacturerID (navigation) | SetManufacturer(ID, Name) if present (`ConvertExtensionsManufacturer` L9111) | — |
| `ImagePath`, `IsCatalogItem` | not passed to native (absent from XLAT) | — |

On write, profile segments are renumbered 0..n-1 from the vector index (`ConvertMastercamAssemblyItem`).

**TlTool**: `XLAT ConvertExtensionsTool` L7305-7378.

| Column | Native | Default |
|---|---|---|
| OpToolInfo | vslot +176, applied only if length == 648, **before** the column setters | ignored if NULL or another length |
| IsCustom | SetIsCustom | false |
| CustomDisplayType | SetDisplayType (`TlDisplayType`) | 0 |
| ToolStation | SetToolStation | **−1** |
| ToolNumber | SetToolNumber | 0 |

**TlToolMill**: `XLAT ConvertExtensionsMillTool` L7919-8073; reverse `ConvertMastercamMillTool` L8075-8168.

| Column | Native | Default / note |
|---|---|---|
| ShoulderLength, CuttingDepth, OverallDiameter, ArborDiameter, ShoulderDiameter, TaperLength, RequiredPilotDiameter, NeckDiameter, HelixAngle | same-named setters | 0.0 |
| OverallLength | SetTruePhysicalLength **and** SetOverallLength, unless the native tool `IsAdjustedForStickout` and `TruePhysicalLength` already equals the value. Write-back stores `GetTruePhysicalLength` | 0.0 |
| MCToolType | SetMCToolType | 0 |
| TlSpindleDir / TlHelixType / TlShankType | enum setters | 0 |
| MfgToolCode | TlTool.SetMfgToolCode | — |
| Chuck | SetChuck | — |
| DiameterOffsetNum / LengthOffsetNum | setters | 0 |
| FluteCount | SetFluteCount (short) | 0 |
| ToolFileName | **TlAssemblyItem.SetGeometryFile**; the same native field as GeometryFile | — |
| IsVariablePitch / IsScalable | setters | false |
| TlGradeID (navigation) | SetToolGrade(ID, Name) (`ConvertExtensionsGrade` L9219) | — |
| TlToolMaterialID | `SetToolMaterial(make_shared<TlToolMaterial>(MaterialID))` | — |
| TlOpParamsID | `ConvertExtensionsOpParams` (below) | — |
| (not a column) `LevelNumber` | TlTool.SetGeometryLevel | DTO only |

**Leaf tables.** Each converter first calls its parent's converter.

| Table | Columns → native | XLAT lines |
|---|---|---|
| TlToolEndmill | CornerRadius → vslot +264 (virtual SetCornerRadius); TaperAngle; TipDiameter; TlRadiusType → SetRadiusType(`TlCornerType`); ChamferLength | 8170-8244 |
| TlToolSlotMill | SecondaryRadiusType (TlCornerType), SecondaryCornerRadius, SecondaryChamferDistance, SecondaryChamferAngle | 8480-8545 |
| TlToolDrill | CornerRadius → vslot +264; ShoulderAngle; TipAngle | 8246-8308 |
| TlToolBoring | TaperAngle; TlRadiusType (TlCornerType; read back via vslot +288); Shift | 8310-8370 |
| TlToolCenterDrill | DrillDiameter, DrillLength | 8426-8478 |
| TlToolCounterBore | PilotDiameter, PilotLength | 8372-8424 |
| TlToolReamer | TipAngle, ChamferLength | 8546-8598 |
| TlToolThreading | TipAngle; CornerRadius (vslot +264); ThreadPitch; TlRadiusType; RootDiameter; ThreadAngle; TipDiameter; TlThreadShape (**default 1**); NStarts (**default 1**) | 8600-8690 |
| TlToolBarrelMill | BarrelLength; CornerRadius (vslot +264); ProfileRadius | 8692-8754 |
| TlToolFeedMill | TipDiameter, LowerRadius, UpperRadius, UpperTaperAngle, LowerTaperAngle, ReliefTaperAngle | 8756-8825 |

**TlOpParams** family: `XLAT ConvertExtensionsOpParams` L7555-7755.
- The subtype decides the native class: TlMillingOpParams → `Cnc::Tool::TlMillingOpParams`;
  TlHolemakingOpParams → `TlHolemakingOpParams`; otherwise a plain `TlOpParams`.
- Base columns: MaterialSFMAdjust, MaterialFPTAdjust, FeedRate, PlungeRate, RetractRate (double, default 0) and
  SpindleSpeed (int, default 0).
- Milling: IsRough, IsFinish (bool, default false), StepoverRoughXY/RoughZ/FinishXY/FinishZ (default 0).
- Holemaking: CannedCycleType (default 0), Peck1, Peck2, PeckClear, ChipBreak, Dwell (default 0).
- Coolant: if `TlOpParams.TlCoolant != null`, `ConvertExtensionsCoolant` (L7430-7490):
  - OldStyle → SetIsOldStyle, default **8**.
  - Flood/Mist/Tool/Custom1..7 → Set*State, default **2**.
  - FloodPos/MistPos/ToolPos/Custom1..7Pos → Set*Time, default **0**.

**TlHolder**: `XLAT ConvertExtensionsHolder` L8826-8892. Columns: UpperConnectionSize (string),
UpperConnectionType (enum), LowerConnectionSize, LowerConnectionType, HolderType (int, default 0), LibraryName.
`TlHolder.CustomDisplayType` is not passed.

**Lathe tables** (`TlToolLathe*`, `TlHolderLathe*`, `TlInsert*`):
- The schema is in `SQL:create` and the EF entities exist, but `XLAT` has no converter for them, and all shipped
  mill/router libraries leave them empty (`DATA`).
- **NOT DECODED** beyond the column list in `SQL:create`.

### 2.5 Seed / reference tables (`SQL:base`)
- `TlManufacturer`:
  - `82FA6C09…539F` 'Mastercam' / 'Mastercam Imported Data'
  - `76DB2132…2085` 'ISCAR'
- `TlMaterial` + `TlToolMaterial`: `E0E11375…429A` 'Carbide'.
- `TlToolGroup`: `58B8CC04…9509` 'Mastercam Tool Types'.
- `TlToolType`: 20 'Legacy … tool type' rows. Names: Block Drill, CBore, Endmill3 Bull, Engrave Tool, Reamer,
  Chamfer Mill, Bore, Spot Drill, Undefined, Lollipop Mill, Dove Mill, Center Drill, Endmill2 Sphere, Drill,
  Radius Mill, Bradpt Drill, Slot Mill, Endmill1 Flat, Csink, Taper Mill. GUIDs are in the script.
  - There is no Tap RH / Tap LH / Face Mill / Feed / Thread / Barrel row.
  - Tools in `DATA:databasedefaults` reference `TlToolTypeID = Guid.Empty`. See §1 for how the type is really encoded.
- `TlOpGroup`: 'Milling' `C2807149…9593`, 'Drilling' `74AB975F…713A`.
- `TlOpType` (10 rows): Surface, Facing, Shouldering, Chamfering, Plunging, Slotting, Profiling, Pocketing,
  Contouring (Milling group) and Drilling (Drilling group).
- `TlToolGrade`: `CCC3C5F9…50E1` 'Mastercam Default Grade' (manufacturer Mastercam).
- `_Header`: `(x'D158777D475B2E4B8CFD85DBF124ED5F', 20, 0)`.
- The script runs with `pragma foreign_keys = off` … `on`.

## 3. WRITE

### 3.1 Two writers
1. **Native Mastercam** (mccore, `Cnc::Tool::ToolLibrarySystem`). Managed wrapper: `IOP/ToolLibrarySystem.cs` with
   `OpenLibrary(path, updateLibrary, createBackup=true)` L83-107, `CreateNewLibrary` L137, `UpdateLibrary`,
   `BackupLibrary`, `Add/Update/Delete`, `PerformBatchChanges` L957.
   - All of these are `DllImport` stubs, so the logic is **NOT DECODED**.
   - String evidence in mccore.dll (`decomp/mccore_tooldb_sql.txt`): `INSERT INTO %s (%s) VALUES (%s)`,
     `UPDATE %s SET %s=? WHERE ID=?`, `DELETE FROM %s WHERE ID=?`, `BEGIN TRANSACTION`,
     `PRAGMA journal_mode = MEMORY|DELETE`, `PRAGMA synchronous = OFF|ON`, `DELETE FROM _Header;`,
     `INSERT INTO _Header VALUES(?, %d, %d)`, the `_UpdateLog` insert, and migration DDL.
   - `TlMgr.SaveAsTooldb(path)` (`IOP/TlMgr.cs` L848) is native too.
2. **The Tool Manager** (EF): `TDB`, `.../DatabaseConnectionManagement/WritableDatabaseConnection.cs`. Detailed below.

### 3.2 Creating an empty library (Tool Manager)
`ToolManager.Database.Utilities/ToolDatabaseCreator.CreateDatabase` → `DatabaseCreator.CreateDatabaseFile`
(`DatabaseCreator.cs` L73-150):
1. Delete any existing file, then `SQLiteConnection.CreateFile`. The connection string is `Data Source=<file>; Version=3`.
2. In one transaction, run `SQL:create` as a single command.
3. In a second transaction, run `SQL:base` line by line, **skipping every line that contains `/*`**.

Result: the 51-table schema, the seed rows of §2.5, and `_Header = (x'D158777D475B2E4B8CFD85DBF124ED5F', 20, 0)`.
That header GUID is a constant in the script. Shipped files carry per-file header GUIDs; their generator (native) is
**NOT DECODED**.

**Implementation (viewer/toollib.c `tm_create`, 2026-10-16):** a new library is made exactly this way. The two scripts are
read from Mastercam 2017's `ToolManager.Database.dll` (manifest resources, each a u32 length + the bytes; found by their
header comments `/* CreateDatabase.sql */` and `/* CreateDatabaseBaseData.sql */`, which occur once in the DLL), looked
for in `$MCAMUI_TOOLDB_DLL`, the paths.conf `schema` folders and `C:\Program Files\mcam2017\Extensions`. New tools in
it are then added as before (GetDefaultTool rows from DatabaseDefaults.tooldb). Checked: `pragma integrity_check` ok,
`_Header` = version 20, 20 TlToolType rows, the tools read back.

### 3.3 Connections and transactions (Tool Manager)
- Connection string: `Data Source=<path>` through EF metadata
  `res://ToolManager.Database/ToolDatabase.ToolDatabase.{csdl,ssdl,msl}` (`DatabaseConnectionBase` ctor L57-67).
  No pragmas are set.
- Each write action runs `context.SaveChanges()`, i.e. one EF transaction per save (`DatabaseConnectionBase`
  L193-212). Order inside a save is decided by EF, so the insert order is **not visible in .NET**.
- Most FKs are `DEFERRABLE INITIALLY DEFERRED`. These are **not** deferrable (`SQL:create`):
  `TlToolMaterial.ID`, `TlWorkMaterial.ID`, `TlOpParams.TlCoolantID`, `TlInsert.TlGradeID`, and all
  `TlHolderLathe*` / `TlInsert*` / `TlToolLathe*` FKs.
- `pragma foreign_keys = on` at the end of the scripts is per-connection only.
- A safe insert order for an external writer that turns `foreign_keys` on:
  1. TlCoolant before TlOpParams (non-deferred FK).
  2. Within a transaction, the deferred ones may come in any order.

  Not observed from Mastercam; derived from the DDL alone.
- Version gate on open: §2.1. A file not at `_Header.version = 20` is rejected by the Tool Manager.

### 3.4 Rows for one new mill tool
Sources: `TDB AddTool` L1530-1548; `WritableDatabaseConnection.Add` L18-26 (`context.TlAssemblyItems.AddObject`,
EF TPT); entity constructors.

| Table | Content | Source of values |
|---|---|---|
| TlAssemblyItem | ID = `Guid.NewGuid()` | `ENT/TlAssemblyItem` ctor L580-625 |
| | IsCatalogItem = 1; strings ""; FK GUIDs Guid.Empty | same ctor |
| | TlToolMaterialID, TlManufacturerID | via VerifyReference*: rows copied from the catalog DB if missing (`TDB` L1636-1763; `WritableDatabaseConnection.Add(...)` skips existing IDs L325/383/427/441/455/469) |
| TlTool | ToolNumber = `GetNextToolNumber(true)` unless given | `TDB` L384-392: `SELECT ToolNumber FROM TlTool t join TlAssemblyItem a on t.[ID]=a.ID where a.IsCatalogItem = 1 Order By ToolNumber Desc LIMIT 1`, +1, capped at int.MaxValue; 1 if empty |
| | ToolStation = 0, IsCustom = 0, CustomDisplayType = 0, **OpToolInfo = NULL** | `ENT/TlTool` L215-223 |
| TlToolMill (+ leaf table chain) | as §1.2 | constructor defaults (below), or a copy of the defaults DB row |
| TlOpParams + TlMillingOpParams or TlHolemakingOpParams | own ID | `ENT/TlOpParams` L292-309 |
| TlCoolant | own ID; all states 2, Pos 0, OldStyle 8 | `ENT/TlCoolant` L431-455 |
| TlProfileData | only for custom / stored profiles | — |

**Where a new tool's values come from.**
- `DefaultDatabaseManager.GetDefaultTool(type, IsMetric)` (`toolmanager.database/.../Services/DefaultDatabaseManager.cs`
  L60-67) copies the first tool in `%Shared%\common\ToolData\DatabaseDefaults.tooldb` with that MCToolType and IsMetric.
  - That copy **keeps** the defaults DB's `OpToolInfo` (`ENT/TlTool` copy ctor L228).
  - Values: §8.2.
- Only if no row exists does it fall back to `ToolTypeFactory.GenerateModel` with the constructor defaults
  (`ENT/TlToolMill` L777-810, `TlToolEndmill` L149-295, `TlToolDrill` L178-235, …). These equal the defaults DB values
  in §8.2 for every type checked.

**ID quirk.** `TlOpParams()` and `TlOpParams(type, metric)` never set `ID` (`ENT/TlOpParams` L282-309), so a
factory-built op-params row would get the all-zero GUID. Only the copy constructor (L313) calls `Guid.NewGuid()`.
Shipped files contain no zero op-params ID. An external writer must generate a fresh GUID for every
TlOpParams / TlCoolant row (§2.3: never shared).

**`CommitChanges(row)`** (`TDB` L806) dereferences `TlOpParams.TlCoolant` unconditionally. Every TlToolMill therefore
needs a TlOpParams row with a TlCoolant row, or the Tool Manager throws.

**Holders:** `new TlHolder(isMetric)` (`ENT/TlHolder` L172-187):
- Connection sizes "0", types 0, `CustomDisplayType = 3` (CUSTOM_STORED).
- One default TlProfileData line (0, 10|1) → (10|1, 10|1) (`ENT/TlProfileData` L255-266).

**Assemblies:** `TDB AddAssembly` L1406-1479:
- `ToolNumber = DiameterOffsetNum = LengthOffsetNum = GetNextAssemblyToolNumber()` (same query on TlAssembly).
- Its tool and holder get `IsCatalogItem = 0`.
- Components: §5.

### 3.5 `OpToolInfo` (648 bytes)

**Producer.** Native only (§0). The Tool Manager never builds or edits it:
- Tools created from constructors have NULL.
- Edits made in the Tool Manager leave an existing blob unchanged, so it goes stale.

Data shows stale blobs: in mill_inch, `FeedRate` matches blob 0x30 in only 393 of 427 rows, and in databasedefaults in
19 of 44, while matching 100 % in mill_mm, router_*, catalog (`corr2.py`).

**On load,** Mastercam applies the blob first and then overwrites with the DB columns (`XLAT ConvertExtensionsTool`
L7305-7378, then the subclass converters). **The SQL columns are authoritative for the fields they carry.** Whether
native setters refresh the blob is **NOT DECODED**.

**Field offsets.** The only offsets .NET names are in `toolmanager.toolwizardlaunch/-Module-.cs`
`ExecuteServiceRequest` L4132-4182: +628 i32 machine-group id (`TlRequestMgr.GetMachineGrpIDN`), +600 i8 IsMetric,
and "if i16 @+4 == 0 and f64 @+32 == 0 then f64 @+32 = 180.0". All other offsets below are **data-derived**: a column
is listed only where it matches the blob in every row of every library where the blob is not stale (`corr.py`,
`corr2.py`, 2542 blobs).

| Off | Type | Field | Evidence |
|---|---|---|---|
| 0x00 | i32 | tool number (`TlTool.ToolNumber`) | all 2542 rows (935 distinct) |
| 0x04 | i16 | `MCToolType` | all rows; also named by ExecuteServiceRequest |
| 0x08 | f64 | `OverallDiameter` | all rows (611 distinct) |
| 0x10 | f64 | corner radius (`TlToolEndmill.CornerRadius`) | all 555 endmills |
| 0x18 | f64 | thread pitch | metric: `ThreadPitch` (mill_mm 32/32, router_mm 40/40). **Inch: threads per inch ≈ 1/ThreadPitch** (router_inch 50/50, mill_inch 32/50 within 1e-4, e.g. pitch 0.0909 ↔ 11.00001) |
| 0x20 | f64 | angle: `TlToolDrill.TipAngle` (1332/1332), `TlToolEndmill.TaperAngle` (555/555), `TlToolBoring.TaperAngle`, `TlToolThreading.TipAngle` (640/640) | ExecuteServiceRequest default 180 for type 0 |
| 0x28 | i32 | `DiameterOffsetNum` | router_inch 266/266 (distinct from length offset) |
| 0x2c | i32 | `LengthOffsetNum` | router_inch 266/266 |
| 0x30 | f64 | `TlOpParams.FeedRate` | 100 % except stale libs |
| 0x38 | f64 | `PlungeRate` | same |
| 0x40 | f64 | `RetractRate` | same |
| 0x48 | i32 | `SpindleSpeed` | same |
| 0x50 | i16 | coolant `TlCoolant.OldStyle` (8/16/64/1/0) | mill_mm 280/280, mill_inch 427/427, catalog 1210/1210 |
| 0x52 | i16 | `FluteCount` | all 2542 rows |
| 0x56 | char[] | tool name | `formats/tooldb.md`, `formats/mcam.md` |
| 0x258 (600) | i32 / i8 | `IsMetric` | all rows; named in ExecuteServiceRequest |
| 0x25c (604) | i32 | `ToolStation` | all rows (2 distinct values) |
| 0x274 (628) | i32 | machine-group id | ExecuteServiceRequest only |

**Decoded from mccore (2026-10-16, `decomp/mccore_tooladd.c`, formats/mcam_write.md §8):** `TlTool / TlToolMill /
subclass ::GetOpToolInfo` build the blob from the stored block plus: +0x00 ToolNumber, +0x04 MCToolType, +0x06 radius
type (Endmill TlRadiusType 0,1 -> 0, 2,4 -> 1, 3 -> 2; Boring / Threading 0..2; FeedMill 1), +0x08 diameter, +0x10 corner
radius (Endmill +0x988, Drill +0x978, Threading +0x9a0), +0x18 pitch (inch: 1 / pitch), +0x20 taper / tip angle, +0x28 /
+0x2c D / H offsets, +0x30 / +0x38 / +0x40 / +0x48 feeds and speed, +0x50 coolant OldStyle, +0x52 flutes, +0x54 tool
material code, +0x56 name, +0xce geometry file, +0x258 IsMetric, +0x25c ToolStation, +0x264 slot, +0x26c display type,
+0x270 geometry level, +0x274 machine group, +0x278 IsScalable. Rebuilt from the parts' tools this reproduces all 62
TL_ID bodies of the user parts.

**Not found in the blob:** OverallLength, ShoulderLength, CuttingDepth, ArborDiameter, ShoulderDiameter, NeckDiameter,
TaperLength, HelixAngle, the CenterDrill / CounterBore / Reamer leaf columns, and the holemaking pecks. None match at
any single offset across a library (`corr2.py`). Their storage in `op_tool_info` (if any) is **NOT DECODED**.

**For a writer:** copy a blob from a same-type tool in `databasedefaults.tooldb` and patch the offsets above, or leave
it NULL (the Tool Manager itself saves NULL blobs, and the reader ignores them). Types 23-25 in the shipped defaults DB
have NULL blobs (`DATA:databasedefaults`). How Mastercam behaves with a NULL blob beyond `ConvertExtensionsTool`
skipping it is **NOT DECODED**.

### 3.6 Delete (Tool Manager, `WritableDatabaseConnection.cs`)

| Item | What is deleted |
|---|---|
| Tool (L41-56) | its TlCoolant, its TlOpParams (with subtype row), then the tool. EF TPT removes TlAssemblyItem … leaf |
| Holder (L104-112) | the holder only |
| Assembly (L149-177) | for each component: the tool (with op params and coolant) or the holder, then the component row, then the TlAssembly |
| Work material (L239-256) | first sets referencing `TlCutParam.TlWorkMaterialID = Guid.Empty`. Manufacturer and grade deletes null their references the same way (L339-370, L397-421) |

- TlProfileData rows of a deleted item: no explicit delete and no cascade on that FK. Whether EF removes them is
  **NOT DECODED**.
- `TlToolToTlOpType` is the only `ON DELETE CASCADE` table, and it is outside the EF model.

### 3.7 Commit order (Tool Manager, `TDB CommitChanges()` L576-685)
1. The assembly context first, propagating changed tool numbers between TlAssembly and the TlToolMill via components.
2. Then holders.
3. Then tools.

`CommitChanges(row)` (L687-830) copies the tool's ToolNumber, LengthOffsetNum and DiameterOffsetNum into its TlAssembly
before saving.

### 3.8 Import / export
`ToolManager.Database.ImportExport/ToolDatabaseFileFormat` (L8-12) registers only the extension `tooldb`.
`ToolDatabaseFileImporter.Import(path)` (L45-57) just opens it as a ToolDatabase. No other format is decoded.

## 4. Profiles (outline geometry)

### 4.1 Coordinate system and `TlProfileData`
Sources: `ENT/TlProfileData`; `PG/HolderSegmentConverter`; `PG/ToolProfileGenerator`; `DATA`.
- **Axes:** x = radius (≥ 0) and y = axial height. For tools, y = 0 is the tip and +y runs toward the shank:
  `ToolProfileGenerator.FillProfile` L74-86 splits at y = CuttingDepth and y = ShoulderLength. For holders, y = 0 is
  the tool end (data, below).
- **Type 2 = line** `(x0,y0) → (x1,y1)` (`IsALineSegment` L288-291).
- **Type 4 = arc** (`IsAnArcSegment` L293-296): centre (x0,y0), `radius`, `StartAngle`, `SweepAngle`. Every reader
  treats any type ≠ 2 as an arc.
- **Arc angles are radians:**
  - `GetEndAngle` wraps start + sweep into [0, 2π) (L416-426).
  - `GetMaxYOfArc` tests π/2 (L354) and `GetMinYOfArc` tests 3π/2 = 4.71238898038469 (L370).
  - `IncludesAngle` uses tolerance 5e-5 and follows the sweep's sign (L428-441).
  - Point on arc: `GetArcPoint` L443-448.
- **`Color`:** int, passed through to native byte +4 of `prof_ent`.
- **`Segment`:** order key, 0..n-1, primary key with ItemID.
- **Native layouts:**
  - `prof_ent` (48 bytes, `XLAT ConvertExtensionsToolProfileSegment` L7252-7303): i32 type @0, u8 colour @4; line
    x0,y0,x1,y1 @8,16,24,32; arc x0,y0,r,start,sweep @8,16,24,32,40.
  - `TlProfileEnt` (120 bytes, `PU ConvertToExtensionsProfileSegments` L661-717 / `ConvertFromExtensionsProfileSegments`
    L719-800): char 'L' / 'A' @0 (→ Type 2 / 4), colour @1; line x0 @16, y0 @24, x1 @40, y1 @48; arc centre @64/72,
    r @88, start @96, sweep @104.
- **Data:**
  - Every shipped TlProfileData row is Type 2, Color 0. There are no arcs, so arc units come from code only.
  - No tool row has profile data in any shipped library. All 14846 rows in mill_mm / mill_inch belong to the 921 holders.
  - Every holder chain is contiguous: segments 0..n-1, each end equals the next start, it starts at (0,0), y never
    decreases, and it ends with a line back to x = 0 at the top.
  - Example: holder C6E4-0875 (mm) starts (0,0)→(14.2875,0)→(23.8125,9.525)… and ends (48.4505,92.075)→(0,92.075).
  - Scripts: `tools/tests/tooldb_profile_q.py`, `tools/tests/tooldb_profile_q2.py`.

### 4.2 Tool outline generation
`PG/ToolProfileGenerator`:
- `Generate` (L88-107): if `IsCustom`, use the stored profile (render as imported); else the parametric profile.
  Then `FillProfile` splits it into cutting / shoulder / non-cutting parts at CuttingDepth and ShoulderLength.
- Profile height = OverallLength (`InitializeProfile` L37-49).
- **Parametric** (L51-55): `ToolValidityChecker.CheckToolValidity` (§7), then `PU.BuildParametricToolProfile`
  (L54-226). That calls native **`TlCore.dll!RetrieveParametricToolProfile`** (GetProcAddress L205; export ordinal 1574
  per `objdump -p ~/mastercam-re/files/tlcore.dll`).
  - The per-type outline math is native (not in .NET); it is decoded from tlcore.dll in `formats/tool_profiles.md`.
  - The native error code is thrown as `InvalidToolException((InvalidToolCode)err)` (L222-223). See
    `IOP/TlProfileResultCode.cs`: 1-47 parameter errors, 50-65 custom/DXF errors, 99 unknown.
- **Custom** (L57-72):
  - Requires profile rows and `ValidateQuadrant` (native `TlProfileValidateQuadrant`).
  - If `IsScalable`, applies `ScaleProfile(rows, OverallDiameter/2)` (native `TlProfileScale`).
  - Then `TrimOrExtendProfile(rows, OverallLength)` (native `TlProfileTrimOrExtendToYValue`).
- **`GenerateWithOutline`** (L109-138): a non-custom MCToolType 0 tool without stored rows gets a gauge cylinder of
  radius OverallDiameter/2.

**Arguments to `RetrieveParametricToolProfile`, in order** (`PU` L205-221). Unset arguments default to 0, except angle A
= 180, angle C = 45, corner type 0:

1. out `vector<TlProfileEnt>*`
2. `int*` error
3. MCToolType
4. byte 9
5. IsMetric
6. 0.0001
7. 5e-5
8. corner type
9. OverallLength
10. OverallDiameter
11. corner radius
12. angle A
13. ArborDiameter
14. ShoulderLength
15. angle B
16. CuttingDepth
17. tip diameter
18. ShoulderDiameter
19. TaperLength
20. chamfer length
21. length P
22. diameter P
23. radius 2
24. secondary chamfer distance
25. angle C
26. ThreadPitch
27. TlThreadShape
28. TlShankType
29. NeckDiameter

**Per class** (`PU` L68-176; tested most-derived first):

| Class | Columns → argument # |
|---|---|
| SlotMill | CornerRadius→11, ChamferLength→20, TaperAngle→12, TlRadiusType→8, SecondaryChamferDistance→24, SecondaryCornerRadius→23 |
| Endmill | CornerRadius→11, ChamferLength→20, TipDiameter→17, TaperAngle→12, TlRadiusType→8 |
| CenterDrill | TipAngle→12, ShoulderAngle→15, DrillDiameter→22, DrillLength→21 |
| CounterBore | PilotDiameter→22, PilotLength→21 |
| Threading | TipAngle→12, CornerRadius→11, TlRadiusType→8, TlThreadShape→27, ThreadPitch→26 |
| Boring | TaperAngle→12, ShoulderAngle→15, CornerRadius→11, TlRadiusType→8 |
| Drill | TipAngle→12, ShoulderAngle→15, CornerRadius→11 |
| Reamer | ChamferLength→20 |
| BarrelMill | CornerRadius→11, ProfileRadius→23 |
| FeedMill | LowerRadius→11, UpperRadius→23, TipDiameter→17, LowerTaperAngle→12, UpperTaperAngle→25, ReliefTaperAngle→15 |

Not passed: Reamer TipAngle, BarrelMill BarrelLength, CounterBore TipAngle, HelixAngle, FluteCount. The second native
path `IOP/TlProfileBuilderParametric.CalcProfile` (L10-73) uses the 200-byte `MillToolProfileParams`, whose fields
are named in `toolnetapi/-Module-.cs` L69117-69146. The column → field mapping for that path is **NOT DECODED**.

**Custom profile sources:**
- `.dxf` (native `RetrieveDXFToolProfile` / `RetrieveDXFHolderProfile`).
- A level of the open part (`toolmanager.interop/.../MastercamToolProfileLoader.cs` `UpdateToolData` L33-36 sets
  `CustomLevel`).
- Stored rows.

The shipped `Tools/*.mcam` files (FLATMILL.mcam, …) are not referenced by any decoded code path. Their role is
**NOT DECODED**.

### 4.3 Holder outline
- `PG/HolderProfileGenerator.Generate` (L13-67):
  - Walks rows in `Segment` order.
  - Each segment with |Δy| > 0.0005 becomes a sub-profile. Flat steps still add their |Δy|.
  - Holder height = Σ|y_end − y_start|.
- `LiveCenterProfileGenerator` is identical.
- **Holder editor ↔ rows** (`PG/HolderSegmentConverter`):
  - rows → editor (L18-45): walk from the highest segment down and skip horizontal lines (|Δy| ≤ 5e-5).
    - Line → {Height = y1 − y0, TopWidth = 2·x1, BottomWidth = 2·x0}.
    - Arc → {Height = maxY − minY, widths from its end points}.
    - Values are rounded to 6 decimals.
  - editor → rows (L48-96): y starts at 0 and the previous width at 0. For each segment:
    1. If the widths differ by > 0.0005, a horizontal line prevW/2 → BottomW/2 at the current y.
    2. Then a line (BottomW/2, y) → (TopW/2, y + Height).
    3. After the last segment, a closing line (TopW/2, H) → (0, H).
    - Rows are numbered from 0 and are all Type 2.
  - Shipped holders follow exactly this shape (§4.1).

### 4.4 `GaugeLength` / `GaugeDiameter`
These are columns on TlAssemblyItem, passed to native SetGaugeLength / SetGaugeDiameter (`XLAT`). No profile or
assembly code in .NET reads them, and they are 0.0 in every shipped row. **Meaning NOT DECODED.**

## 5. Assemblies (holder + tool stacking)

Tables: `TlAssembly` (MainTool, MainHolder, ToolNumber, MachineGroup, DiameterOffsetNum, LengthOffsetNum, MaxRamp,
RelationshipHierarchyXML) and `TlAssemblyComponent` (TlAssemblyID, TlAssemblyItemID, ParentID, M/U/CTransformID,
CScalar). Empty in every shipped file (`DATA`).

**Naming trap** (`ENT/TlAssemblyComponent` L280-340 + `CSDL`): the navigation property `ParentItem` is the row's own
`TlAssemblyItemID` (FK_TlAssemblyComponent_4_0), and the property `TlAssemblyItem` is `ParentID` (FK_3_0).

**Component rows** (`TDB`):

| Method | Row written |
|---|---|
| `AddAssemblyTool` (L928-948) | {TlAssemblyID, TlAssemblyItemID = tool, ParentID, CScalar = 0} |
| `AddAssemblyHolder` (L950-980) | CScalar = holder stick-out if ≥ 0, else `Profile.StickOut` |
| `SaveAssemblyComponent` (L832-881) | updates CScalar (tolerance 1e-5 tool / 5e-5 holder) and ParentID |

**Stacking** (`PG/AssemblyProfileGenerator`):
- `BuildComponentsList` (L22-41): start at the component with `TlAssemblyItemID == MainHolder`, then follow `ParentID`.
  The main holder is at the tool end, and the chain runs toward the spindle.
- `BuildHolderComponents` (L43-80):
  - StickOut = 0 for the main holder, `CScalar` for the others.
  - Each next holder is attached on a +Y axis with Max m = min(parentHeight, node.Height),
    `ModelingTransformation.M42 = −m`, Position = m (if StickOut < 0) else m − StickOut (`AppendMachineComponent`
    L147-201).
- `AssembleComponents` (L82-145): the holder rides on the tool along +Y.
  - Min = ShoulderLength, Max = tool height (OverallLength).
  - Position = tool height − CScalar(main-holder component), capped at the tool height.
  - If there is no main-holder component: Position = tool height.
  - If Position < ShoulderLength: Position = Max.
- **Tool stick-out from the holder** = round(tool profile height − CScalar(main-holder component), 6)
  (`TDB GetAssemblies` L1984-1989, `GetEndHolderComponent` L2072-2084).
- The scene-graph classes (`MachineBuilder.Models`, `~/mastercam-re/files/machinebuilder.models.dll`, not decompiled)
  turn Position / M42 into world coordinates: **NOT DECODED**.
- `M/U/CTransformID` (TlMatrix4x4): no reader in toolmanager.*: **NOT DECODED**.
- `RelationshipHierarchyXML`: only passed through (`TlAssemblyViewModel.cs` L237-246): format **NOT DECODED**.
- `TlRelations`: an EF navigation property only, with no users: **NOT DECODED**.

**Part file `TlMgr`** (`formats/mcam.md`, ASSOC subtype 33):
- The managed wrapper `IOP/TlMgr.cs` exposes:
  - Add / Update / Delete by GUID or slot.
  - `Find(mainToolId[, mainHolderId])` and `Find(toolNumber, machineGroup)`.
  - `IDBySlot` / `SlotByID` (L893-957), `SetupNewAssembly(tool, asDefault)` (L980), `SaveAsTooldb(path)` (L848).
- All are native stubs: **logic NOT DECODED**.
- Native tools enter the part's TlMgr via `Cnc.Tool.TlMgr.Add` from the wizard path (`toolmanager.toolwizardlaunch/-Module-.cs`
  `ExecuteServiceRequest` L4185).
- The objects are the same classes as the DB entities. `formats/mcam.md` confirms the TlTool level carries the same
  0x288-byte `op_tool_info` block.
- `TlAssemblyItem.TlSourceID` is copied only (`ENT/TlAssemblyItem` L645); its use as a library back-reference is
  **NOT DECODED**.

## 6. Feeds, speeds, materials, coolant

**Stored per tool:** `TlOpParams` (FeedRate, PlungeRate, RetractRate, SpindleSpeed, MaterialSFMAdjust,
MaterialFPTAdjust), plus `TlMillingOpParams` (IsRough, IsFinish, Stepover Rough/Finish XY/Z) or `TlHolemakingOpParams`
(CannedCycleType §1.3, Peck1, Peck2, PeckClear, ChipBreak, Dwell), plus `TlCoolant`.

**Formulas** (Tool Manager UI, `toolmanager.database/ToolManager.Database.ViewModels/TlToolViewModel.cs`):

| Quantity | Formula | Lines |
|---|---|---|
| Feed | \|rpm\| × fpt × FluteCount | L1013-1016 |
| rpm | round(K · SFM / Ø), K = **318.3 metric / 3.82 inch**; 0 if Ø < 5e-5 | L1018-1027 |
| fpt | Feed / (FluteCount · rpm) | L1028-1037 |
| SFM | rpm · Ø / K | L1039-1043 |

- SpindleSpeed is stored **negative** when TlSpindleDir = Counterclockwise (`SetSpindleSpeedValue` L1045-1049). No
  shipped row is negative.
- Changing Ø keeps SFM and fpt and recomputes rpm and feed, except for face mills (L503-517).
- **Taps** (`TlToolTapViewModel.cs`):
  - flutes = 1 (L150-153), fpt = pitch (L170-173).
  - Feed = |rpm · p| with p = pitch / 25.4 when metric, then × 25.4 when metric (L155-168).
  - Setting the feed sets rpm = feed / fpt (`TlToolViewModel.cs` L404).
- `MaterialSFMAdjust` / `MaterialFPTAdjust` are only edited and stored (L311-345); no formula uses them.

**`TlCutParam`** (cut data library):
- Columns are in `SQL:create`. Constructor defaults (`ENT/TlCutParam` L737-767): ToolLifeMinutes 15, TlCutSource 1
  (USER), TlFeedFactor 1, the rest 0 / empty, ID = `Guid.NewGuid()` (`TDB` L1302).
- Only CutSpeed and FPT reach native (`toolmanager.shared/ToolManager.Shared.Models/CutParamServiceReturn.cs` L17-24;
  `-Module-.cs` L11825-11908 → `TlCutParam.SetCutSpeed` / `SetFPT`).
- Selection is native `ToolSystem.SelectFromLibrary(ref TlCutParam, short isoGroup, double toolDia)` (`IOP/ToolSystem.cs`
  L1989): **NOT DECODED**. Min/MaxDiameter use and TlFeedFactor meaning are also **NOT DECODED**.

**Materials:**
- `TlMaterial(ID, Name, Description)` with subtypes `TlToolMaterial` (cutter material) and `TlWorkMaterial` (ISOGroup,
  Density, HardnessUnit, HardnessValue).
- Adding cut params reuses a work material equal on Name, Density, HardnessUnit, ISOGroup and HardnessValue
  (`TDB` L1299-1404).
- Data:
  - databasecatalog: 47 TlMaterial, 41 TlWorkMaterial, 6 TlToolMaterial, 0 TlCutParam.
  - databasedefaults: 2 TlCutParam (inch / mm, all zeros, life 15, op type Shouldering), 1 TlWorkMaterial (the empty
    material `83E53A23…`).
  - Mill/router libraries: no cut params or work materials.

**Coolant:**
- `TlCoolant.OldStyle` (`ToolManager.Database.ViewModels/TlCoolantViewModel.cs` L12-20, L24-112): **8 Off, 16 Flood,
  32 Mist, 64 Thru-tool**. It is stored in the blob too (0x50).
  - Data: mill_mm 8 / 16 / 64. mill_inch has 260 rows of **1**; catalog has 1174 rows of **0**. The meanings of 1 and
    0 are **NOT DECODED**.
- Flood / Mist / Tool / Custom1-7 states and their `*Pos`:
  - Default 2 / 0 (`ENT/TlCoolant` L431-455; `XLAT` defaults).
  - 3 = "differs across a multi-selection" in the UI.
  - Setting a state to 2 resets its Pos to 0 (`TlCoolantViewModel.cs` L574-580).
  - The combo items in `toolmanager.database/controls/coolantview.baml` are ordered FloodOn, FloodOff, FloodIgnore and
    Before, With, After, but the binding is not decoded. Every shipped row is state 2 / Pos 0.
  - **The state value meanings are NOT DECODED.** (`formats/tooldb.md` once said "1 = on, 2 = off" without a source;
    it now agrees with this section.)
- **This program** (`viewer/toollib.c`): reads OldStyle into `Tool.coolant_style` (flood flag `coolant` = OldStyle == 16)
  and writes the value read back unchanged while it still agrees with the flood flag, else 16 (flood) / 8 (off)
  (`tool_coolant_style`; the same value goes to blob +0x50). The wizard's Coolant dialog (`viewer/mcamui.c`
  `draw_coolant_dialog`, from CoolantView.baml / TlCoolantViewModel) sets Flood / Mist / Thru-tool On = 16 / 32 / 64,
  Off = 8. The Flood..Custom7 state / Pos columns are not kept.

## 7. Units and validation

**Units:**
- `IsMetric` (bool) is on TlAssemblyItem (every item) and TlCutParam. All lengths in a row are in that row's units:
  mm or inch.
- No mm/inch conversion of geometry exists in the managed data layer. The only 25.4 is the tap feed above, and
  `IsScalable` is copied but no scaling code reads it except the custom-profile `ScaleProfile`.
- Units choose constructor defaults and the rpm constant K.
- What Mastercam does with an inch tool in a metric part / library: native, **NOT DECODED**.
- Shipped libraries are uniform per file (mill_mm all 1, mill_inch all 0). The defaults and catalog DBs mix both.
- The blob pitch is stored per unit (§3.5).
- Display rounding (`ToolManagerConstants.cs`): FloatTolerance 5e-5, ToolValidityTol 5e-6, InchRounding 6,
  MetricRounding 5, AngleRounding 4.

**Validation:** `VC`. Runs from `ToolProfileGenerator` L53 and from `TDB GetTools` L1881 for tools without a stored
profile. A failure marks the tool invalid and logs 6100+code (L1883-1887). Tolerance is 5e-5 (L14-37). Codes are in
`ToolManager.Database.ValidityChecking/InvalidToolCode.cs`.

**All mill tools** (L39-96):

| Condition | Code |
|---|---|
| OAL ≤ 0 | 2 |
| Ø ≤ 0 | 1 |
| flute length ≤ 0 | 6 |
| flute length > OAL | 4 |
| shoulder > OAL | 3 |
| flute length > shoulder | 6 |
| arbor ≤ 0 | 5 |
| reduced shank: neck ≤ 0 | 33 |
| reduced shank: a = shoulderØ − neck or b = arbor − neck < 0 | 34 |
| reduced shank: (shoulder + TaperLength) − b − (shoulder + a) < 0 | 35 |
| reduced shank: OAL − (shoulder + TaperLength) ≤ 0 | 28 |

**Endmills by type** (L98-130, L328-507):

| Type | Rules |
|---|---|
| 10 / 11 / 19 | shoulderØ = 0 → 30; chamfer > Ø/2 → 39; R < 0 → 10; R > Ø/2 → 9; R > OAL → 7; R > shoulder → 8 |
| 15 | R ≤ 0 → 10; R ≥ shoulder → 8 |
| 16 | t = tan(90° − taper). R < 0 → 10; R > flute → 14; R > Ø/2 → 9; arbor > Ø → 11; arbor = Ø → 21; Ø/2 − flute/t ≤ 0 and R = 0 → 25 |
| 18 (L398-433) | shoulderØ = 0 → 30; shank-shape checks → 11, 27, 28, 37; shoulder < √((Ø/2)² − (shoulderØ/2)²) + Ø/2 → 18 |
| 17 | R < 0 → 10 |
| 13 | a = taper if < 90 else 0; v = (tipØ/2 − Ø/2)·tan(90° − a), with v = 0 if a = 0. OAL < Ø/8 → 24; Ø > tipØ → 19; v < 0 or v > shoulder → 12; R < 0 → 10; R > flute → 14; Ø = tipØ and a ≠ 0 → 12; Ø = tipØ and a = 0 and R > 0 → 12; a = 0 and R > shoulder → 12 |
| 12 / 21 | v = (Ø/2 − tipØ/2)·tan(90° − taper). OAL < Ø/8 → 24; tipØ ≥ Ø → 19; v < 0 or v > shoulder → 12; R < 0 → 10 |
| 14 (L133-154) | p = R if R > 0 else chamfer; s = secondary R if > 0 else secondary chamfer. R or secondary R < 0 → 10; p or s > flute/2 → 14; flute < p + s → 14; arbor/2 > Ø/2 − s → 13 |

**Other classes:**

| Class | Rules |
|---|---|
| Barrel (L156-205) | shoulderØ = 0 → 30; cornerR ≥ ProfileRadius → 36; Ø/2 < shoulderØ/2 → 37; flute > 2·ProfileRadius → 40; Ø/2 − shoulderØ/2 > ProfileRadius → 40; then a sqrt fit check → 40 / 41 / 36 (see source) |
| Feed mill (L207-238) | shoulderØ = 0 → 30; relief > 0 and Ø ≤ shoulderØ → 42; relief ∉ [0,90] → 47; Ø ≤ tipØ → 19; lower taper > 90 → 45; upper ∉ [0,90] → 46; lower < upper → 45 |
| Counterbore (L240-243) | base checks only |
| Bore (L245-266) | R < 0 → 10; arbor > Ø → 11; arbor = Ø → 21; Ø/2 − flute/tan(90° − tip) ≤ 0 → 25 |
| Center drill (L281-296) | DrillØ/2·tan(90° − tip/2) ≥ DrillLength → 15; (Ø − DrillØ)/2·tan(90° − shoulderAngle/2) + DrillLength > shoulder → 16 |
| Drill, not countersink (L537-546) | Ø/2·tan(90° − tip/2) ∉ [0, shoulder] → 12 |
| Countersink (L509-535) | R = 0: drill rule; else R < 0 → 10, R > Ø/2 → 9 |
| Reamer (L298-313) | OAL < Ø/8 → 17; Ø ≠ arbor and shoulder < Ø/8 → 18; Ø < 2·chamfer → 39 |
| Tap (L548-574) | as countersink, with tolerance 5e-6 for R = 0 |
| Thread mill (L576-610) | shoulderØ = 0 → 30. Root check → 38 if ≤ 0, with r = Ø/2: Unified/Metric r − 0.866p; Acme r − 0.5p; Acme stub r − 0.3p; Buttress r + 0.0826p − 0.89064p; NPT r − flute/32 + 0.8657p |

**Edit-time auto-corrections** (`ValidityChecking/ToolConverters.cs`; UI behaviour, not load rules):
- Changing the corner type rewrites MCToolType and R (L34-310): chamfer → type 10 with chamfer Ø/8; radius → 19 with
  R Ø/4; full → 11 with R Ø/2; none → 10; roundover → 15 with R Ø/4.
- R is clamped to floor(Ø/2·1e4)/1e4 (L312-381).

**Required fields:** the schema has `NOT NULL` only on `TlTool.ID`, the subtype `ID`s, `TlOpParams.ID`,
`TlHolemaking/MillingOpParams.ID`, `TlAuxData.ID` and the lathe IDs (`SQL:create`). Beyond that, the code needs:
- `TlAssemblyItem.IsMetric` non-NULL (`XLAT` uses `.Value`).
- A TlOpParams + TlCoolant per mill tool (§3.4).

## 8. Verification data

### 8.1 Row counts (`dump.py`)

| Library | Tools | Holders | Profile rows | Rows per leaf table (a parent table also counts its subtypes' rows, e.g. Drill includes CenterDrill) |
|---|---|---|---|---|
| mill_mm | 280 | 921 | 14846 | Drill 181, Endmill 67, Threading 32 |
| mill_inch | 427 | 921 | 14846 | CenterDrill 20, CounterBore 11, Drill 217, Endmill 149, Reamer 11, Threading 50 |
| router_mm | 315 | 0 | 0 | CenterDrill 14, Drill 81, Endmill 194, SlotMill 2, Threading 40 |
| router_inch | 266 | 0 | 0 | CenterDrill 12, Drill 93, Endmill 123, SlotMill 1, Threading 50 |
| databasedefaults | 50 (2 per type, none for 20) | 2 | 6 | every leaf table, 2 rows each except Drill 14, Endmill 22, Threading 6 |
| databasecatalog | 1210 | 36 | 393 | CenterDrill 36, Drill 746, Threading 464 |
| BT40-mm (holder lib) | 0 | 142 | 2249 | — |

- `OpToolInfo` is 648 bytes for every tool, except 6 NULL in databasedefaults (types 23, 24, 25).
- `IsCatalogItem` = 1 on every shipped item.

### 8.2 Per-type defaults (`databasedefaults.tooldb`; this is what `GetDefaultTool` copies)
Generated by `defs2.py`. Columns: type | units | leaf tables | TlToolMill geometry | leaf values | op params.
Coolant is always OldStyle 8 with states 2 / Pos 0. Material Carbide, grade 'Mastercam Default Grade'. MfgToolCode,
Chuck and ToolFileName are empty. Spindle dir, helix, shank and IsScalable are 0.

| MC | u | tables | TlToolMill | leaf | op params |
|---|---|---|---|---|---|
| 0 | in | - | D 0.5, OAL 4, SL 1.2, CD 1, arbor 0.5, shoulderD 0.5, neckD 0.5, taperL 0, Z 4 |  | plain; F 5 plunge 25 retr 50 S 1000 |
| 0 | mm | - | D 10, OAL 75, SL 25, CD 25, arbor 10, shoulderD 10, neckD 10, taperL 0, Z 4 |  | plain; F 120 plunge 600 retr 1200 S 1000 |
| 1 | in | TlToolDrill > TlToolCenterDrill | D 0.25, OAL 2, SL 0.2362, CD 0.2312, arbor 0.25, shoulderD 0.25, neckD 0.25, taperL 0, Z 2 | TlToolDrill: TipAngle=118.0, ShoulderAngle=60.0, CornerRadius=0.0; TlToolCenterDrill: DrillLength=0.109375, DrillDiameter=0.109375 | Holemaking; F 5 plunge 25 retr 50 S 1000 |
| 1 | mm | TlToolDrill > TlToolCenterDrill | D 7.94, OAL 54, SL 7.30728, CD 7.30228, arbor 7.94, shoulderD 7.94, neckD 7.94, taperL 0, Z 2 | TlToolDrill: TipAngle=118.0, ShoulderAngle=60.0, CornerRadius=0.0; TlToolCenterDrill: DrillLength=3.18, DrillDiameter=3.18 | Holemaking; F 120 plunge 600 retr 1200 S 1000 |
| 2 | in | TlToolDrill | D 0.25, OAL 2, SL 1.5, CD 1, arbor 0.25, shoulderD 0.25, neckD 0.25, taperL 0, Z 2 | TlToolDrill: TipAngle=90.0, ShoulderAngle=180.0, CornerRadius=0.0 | Holemaking; F 5 plunge 25 retr 50 S 1000 |
| 2 | mm | TlToolDrill | D 6, OAL 50, SL 37, CD 25, arbor 6, shoulderD 6, neckD 6, taperL 0, Z 2 | TlToolDrill: TipAngle=90.0, ShoulderAngle=180.0, CornerRadius=0.0 | Holemaking; F 120 plunge 600 retr 1200 S 1000 |
| 3 | in | TlToolDrill | D 0.25, OAL 2, SL 1.5, CD 1, arbor 0.25, shoulderD 0.25, neckD 0.25, taperL 0, Z 2 | TlToolDrill: TipAngle=118.0, ShoulderAngle=180.0, CornerRadius=0.0 | Holemaking; F 5 plunge 25 retr 50 S 1000 |
| 3 | mm | TlToolDrill | D 6, OAL 50, SL 40, CD 25, arbor 6, shoulderD 6, neckD 6, taperL 0, Z 2 | TlToolDrill: TipAngle=118.0, ShoulderAngle=180.0, CornerRadius=0.0 | Holemaking; F 120 plunge 600 retr 1200 S 1000 |
| 4 | in | TlToolThreading | D 0.25, OAL 2, SL 1.5, CD 1, arbor 0.25, shoulderD 1, neckD 0.25, taperL 0, Z 2 | TlToolThreading: TipDiameter=0.0, TipAngle=180.0, TlRadiusType=0, CornerRadius=0.0, ThreadPitch=0.05, TlThreadShape=0, RootDiameter=0.0, ThreadAngle=0.0, NStarts=1 | Holemaking; F 50 plunge 25 retr 50 S 1000 |
| 4 | mm | TlToolThreading | D 6, OAL 50, SL 37.5, CD 25, arbor 6, shoulderD 20, neckD 6, taperL 0, Z 2 | TlToolThreading: TipDiameter=0.0, TipAngle=180.0, TlRadiusType=0, CornerRadius=0.0, ThreadPitch=2.5, TlThreadShape=1, RootDiameter=0.0, ThreadAngle=0.0, NStarts=1 | Holemaking; F 2500 plunge 600 retr 1200 S 1000 |
| 5 | in | TlToolThreading | D 0.25, OAL 2, SL 1.5, CD 1, arbor 0.25, shoulderD 1, neckD 0.25, taperL 0, Z 2 | TlToolThreading: TipDiameter=0.0, TipAngle=180.0, TlRadiusType=0, CornerRadius=0.0, ThreadPitch=0.05, TlThreadShape=0, RootDiameter=0.0, ThreadAngle=0.0, NStarts=1 | Holemaking; F 50 plunge 25 retr 50 S 1000 |
| 5 | mm | TlToolThreading | D 6, OAL 50, SL 37.5, CD 25, arbor 6, shoulderD 20, neckD 6, taperL 0, Z 2 | TlToolThreading: TipDiameter=0.0, TipAngle=180.0, TlRadiusType=0, CornerRadius=0.0, ThreadPitch=2.5, TlThreadShape=1, RootDiameter=0.0, ThreadAngle=0.0, NStarts=1 | Holemaking; F 2500 plunge 600 retr 1200 S 1000 |
| 6 | in | TlToolReamer | D 0.25, OAL 6, SL 1, CD 1, arbor 0.225, shoulderD 0.25, neckD 0.225, taperL 0, Z 6 | TlToolReamer: TipAngle=45.0, ChamferLength=0.03125 | Holemaking; F 5 plunge 25 retr 50 S 1000 |
| 6 | mm | TlToolReamer | D 6, OAL 150, SL 25, CD 25, arbor 5.4, shoulderD 6, neckD 5.4, taperL 0, Z 6 | TlToolReamer: TipAngle=45.0, ChamferLength=0.75 | Holemaking; F 120 plunge 600 retr 1200 S 1000 |
| 7 | in | TlToolDrill > TlToolBoring | D 1, OAL 2, SL 1.5, CD 0.25, arbor 0.5, shoulderD 0.5, neckD 0.5, taperL 0, Z 1 | TlToolDrill: TipAngle=45.0, ShoulderAngle=180.0, CornerRadius=0.0; TlToolBoring: TlRadiusType=0, TaperAngle=45.0, Shift=0.1 | Holemaking; F 5 plunge 25 retr 50 S 1000 |
| 7 | mm | TlToolDrill > TlToolBoring | D 25, OAL 50, SL 37.5, CD 6.5, arbor 12, shoulderD 12, neckD 12, taperL 0, Z 1 | TlToolDrill: TipAngle=45.0, ShoulderAngle=180.0, CornerRadius=0.0; TlToolBoring: TlRadiusType=0, TaperAngle=45.0, Shift=0.25 | Holemaking; F 120 plunge 600 retr 1200 S 1000 |
| 8 | in | TlToolDrill > TlToolCounterBore | D 0.781, OAL 5, SL 1.5, CD 1, arbor 0.5, shoulderD 0.781, neckD 0.5, taperL 0, Z 4 | TlToolDrill: TipAngle=180.0, ShoulderAngle=180.0, CornerRadius=0.0; TlToolCounterBore: PilotLength=0.25, PilotDiameter=0.25 | Holemaking; F 5 plunge 25 retr 50 S 1000 |
| 8 | mm | TlToolDrill > TlToolCounterBore | D 25, OAL 100, SL 24, CD 18, arbor 12, shoulderD 25, neckD 12, taperL 0, Z 4 | TlToolDrill: TipAngle=180.0, ShoulderAngle=180.0, CornerRadius=0.0; TlToolCounterBore: PilotLength=6.0, PilotDiameter=6.0 | Holemaking; F 120 plunge 600 retr 1200 S 1000 |
| 9 | in | TlToolDrill | D 0.75, OAL 2, SL 1, CD 0.5, arbor 0.5, shoulderD 0.75, neckD 0.5, taperL 0, Z 4 | TlToolDrill: TipAngle=82.0, ShoulderAngle=0.0, CornerRadius=0.0 | Holemaking; F 5 plunge 25 retr 50 S 1000 |
| 9 | mm | TlToolDrill | D 25, OAL 75, SL 25, CD 16, arbor 12, shoulderD 25, neckD 12, taperL 0, Z 4 | TlToolDrill: TipAngle=82.0, ShoulderAngle=0.0, CornerRadius=0.0 | Holemaking; F 120 plunge 600 retr 1200 S 1000 |
| 10 | in | TlToolEndmill | D 0.5, OAL 4, SL 1.2, CD 1, arbor 0.5, shoulderD 0.5, neckD 0.5, taperL 0, Z 4 | TlToolEndmill: TipDiameter=0.0, TaperAngle=180.0, TlRadiusType=0, CornerRadius=0.0, ChamferLength=0.0 | Milling; F 5 plunge 25 retr 50 S 1000 |
| 10 | mm | TlToolEndmill | D 10, OAL 75, SL 25, CD 25, arbor 10, shoulderD 10, neckD 10, taperL 0, Z 4 | TlToolEndmill: TipDiameter=0.0, TaperAngle=180.0, TlRadiusType=0, CornerRadius=0.0, ChamferLength=0.0 | Milling; F 120 plunge 600 retr 1200 S 1000 |
| 11 | in | TlToolEndmill | D 0.5, OAL 4, SL 1.2, CD 1, arbor 0.5, shoulderD 0.5, neckD 0.5, taperL 0, Z 4 | TlToolEndmill: TipDiameter=0.0, TaperAngle=180.0, TlRadiusType=3, CornerRadius=0.25, ChamferLength=0.0 | Milling; F 5 plunge 25 retr 50 S 1000 |
| 11 | mm | TlToolEndmill | D 12, OAL 100, SL 30, CD 25, arbor 12, shoulderD 12, neckD 12, taperL 0, Z 4 | TlToolEndmill: TipDiameter=0.0, TaperAngle=180.0, TlRadiusType=3, CornerRadius=6.0, ChamferLength=0.0 | Milling; F 120 plunge 600 retr 1200 S 1000 |
| 12 | in | TlToolEndmill | D 1, OAL 3, SL 1.2, CD 1, arbor 0.5, shoulderD 1, neckD 0.5, taperL 0, Z 4 | TlToolEndmill: TipDiameter=0.1, TaperAngle=45.0, TlRadiusType=0, CornerRadius=0.0, ChamferLength=0.0 | Milling; F 5 plunge 25 retr 50 S 1000 |
| 12 | mm | TlToolEndmill | D 25, OAL 75, SL 30, CD 25, arbor 12, shoulderD 25, neckD 12, taperL 0, Z 4 | TlToolEndmill: TipDiameter=2.5, TaperAngle=45.0, TlRadiusType=0, CornerRadius=0.0, ChamferLength=0.0 | Milling; F 120 plunge 600 retr 1200 S 1000 |
| 13 | in | TlToolEndmill | D 1.5, OAL 2, SL 0.75, CD 0.25, arbor 0.5, shoulderD 2, neckD 0.5, taperL 0, Z 4 | TlToolEndmill: TipDiameter=2.0, TaperAngle=45.0, TlRadiusType=1, CornerRadius=0.0, ChamferLength=0.0 | Milling; F 5 plunge 25 retr 50 S 1000 |
| 13 | mm | TlToolEndmill | D 40, OAL 50, SL 18, CD 5, arbor 12, shoulderD 50, neckD 12, taperL 0, Z 4 | TlToolEndmill: TipDiameter=50.0, TaperAngle=45.0, TlRadiusType=1, CornerRadius=0.0, ChamferLength=0.0 | Milling; F 120 plunge 600 retr 1200 S 1000 |
| 14 | in | TlToolEndmill > TlToolSlotMill | D 2, OAL 2, SL 0.25, CD 0.25, arbor 0.5, shoulderD 2, neckD 0.5, taperL 0, Z 4 | TlToolEndmill: TipDiameter=0.0, TaperAngle=180.0, TlRadiusType=0, CornerRadius=0.0, ChamferLength=0.0; TlToolSlotMill: SecondaryRadiusType=0, SecondaryCornerRadius=0.0, SecondaryChamferDistance=0.0, SecondaryChamferAngle=0.0 | Milling; F 5 plunge 25 retr 50 S 1000 |
| 14 | mm | TlToolEndmill > TlToolSlotMill | D 50, OAL 50, SL 6, CD 6, arbor 12, shoulderD 50, neckD 12, taperL 0, Z 4 | TlToolEndmill: TipDiameter=0.0, TaperAngle=180.0, TlRadiusType=0, CornerRadius=0.0, ChamferLength=0.0; TlToolSlotMill: SecondaryRadiusType=0, SecondaryCornerRadius=0.0, SecondaryChamferDistance=0.0, SecondaryChamferAngle=0.0 | Milling; F 120 plunge 600 retr 1200 S 1000 |
| 15 | in | TlToolEndmill | D 0.25, OAL 2, SL 0.625, CD 0.25, arbor 0.375, shoulderD 0.75, neckD 0.375, taperL 0, Z 4 | TlToolEndmill: TipDiameter=0.0, TaperAngle=180.0, TlRadiusType=4, CornerRadius=0.25, ChamferLength=0.0 | Milling; F 5 plunge 25 retr 50 S 1000 |
| 15 | mm | TlToolEndmill | D 6, OAL 75, SL 18, CD 6, arbor 10, shoulderD 20, neckD 10, taperL 0, Z 4 | TlToolEndmill: TipDiameter=0.0, TaperAngle=180.0, TlRadiusType=4, CornerRadius=6.0, ChamferLength=0.0 | Milling; F 120 plunge 600 retr 1200 S 1000 |
| 16 | in | TlToolEndmill | D 0.75, OAL 2.125, SL 0.5, CD 0.375, arbor 0.375, shoulderD 0.317, neckD 0.375, taperL 0, Z 4 | TlToolEndmill: TipDiameter=0.0, TaperAngle=30.0, TlRadiusType=0, CornerRadius=0.0, ChamferLength=0.0 | Milling; F 5 plunge 25 retr 50 S 1000 |
| 16 | mm | TlToolEndmill | D 20, OAL 60, SL 15, CD 10, arbor 10, shoulderD 8.453, neckD 10, taperL 0, Z 4 | TlToolEndmill: TipDiameter=0.0, TaperAngle=30.0, TlRadiusType=0, CornerRadius=0.0, ChamferLength=0.0 | Milling; F 120 plunge 600 retr 1200 S 1000 |
| 17 | in | TlToolEndmill | D 0.218, OAL 6, SL 1.5, CD 1.5, arbor 0.75, shoulderD 0.75, neckD 0.75, taperL 0, Z 4 | TlToolEndmill: TipDiameter=0.0, TaperAngle=10.0, TlRadiusType=0, CornerRadius=0.0, ChamferLength=0.0 | Milling; F 5 plunge 25 retr 50 S 1000 |
| 17 | mm | TlToolEndmill | D 6, OAL 200, SL 40, CD 40, arbor 20, shoulderD 20, neckD 20, taperL 0, Z 4 | TlToolEndmill: TipDiameter=0.0, TaperAngle=10.0, TlRadiusType=0, CornerRadius=0.0, ChamferLength=0.0 | Milling; F 120 plunge 600 retr 1200 S 1000 |
| 18 | in | TlToolEndmill | D 1, OAL 5, SL 1.25, CD 0.75, arbor 0.75, shoulderD 0.5, neckD 0.5, taperL 1.4288, Z 4 | TlToolEndmill: TipDiameter=0.0, TaperAngle=45.0, TlRadiusType=3, CornerRadius=0.5, ChamferLength=0.0 | Milling; F 5 plunge 25 retr 50 S 1000 |
| 18 | mm | TlToolEndmill | D 25, OAL 200, SL 75, CD 18, arbor 25, shoulderD 12, neckD 12, taperL 71.438, Z 4 | TlToolEndmill: TipDiameter=0.0, TaperAngle=45.0, TlRadiusType=3, CornerRadius=12.5, ChamferLength=0.0 | Milling; F 120 plunge 600 retr 1200 S 1000 |
| 19 | in | TlToolEndmill | D 0.5, OAL 3, SL 1.2, CD 1, arbor 0.5, shoulderD 0.5, neckD 0.5, taperL 0, Z 4 | TlToolEndmill: TipDiameter=0.0, TaperAngle=180.0, TlRadiusType=2, CornerRadius=0.125, ChamferLength=0.0 | Milling; F 5 plunge 25 retr 50 S 1000 |
| 19 | mm | TlToolEndmill | D 12, OAL 100, SL 25, CD 25, arbor 12, shoulderD 12, neckD 12, taperL 0, Z 4 | TlToolEndmill: TipDiameter=0.0, TaperAngle=180.0, TlRadiusType=2, CornerRadius=3.0, ChamferLength=0.0 | Milling; F 120 plunge 600 retr 1200 S 1000 |
| 21 | in | TlToolEndmill | D 0.25, OAL 2, SL 1.2, CD 0.437, arbor 0.25, shoulderD 0.25, neckD 0.25, taperL 0, Z 1 | TlToolEndmill: TipDiameter=0.002, TaperAngle=15.0, TlRadiusType=0, CornerRadius=0.0, ChamferLength=0.0 | Milling; F 5 plunge 25 retr 50 S 1000 |
| 21 | mm | TlToolEndmill | D 6, OAL 50, SL 50, CD 10, arbor 6, shoulderD 6, neckD 6, taperL 0, Z 1 | TlToolEndmill: TipDiameter=0.2, TaperAngle=15.0, TlRadiusType=0, CornerRadius=0.0, ChamferLength=0.0 | Milling; F 120 plunge 600 retr 1200 S 1000 |
| 22 | in | TlToolDrill | D 0.25, OAL 2, SL 1.5, CD 1, arbor 0.25, shoulderD 0.25, neckD 0.25, taperL 0, Z 2 | TlToolDrill: TipAngle=180.0, ShoulderAngle=180.0, CornerRadius=0.0 | Holemaking; F 5 plunge 25 retr 50 S 1000 |
| 22 | mm | TlToolDrill | D 6, OAL 50, SL 30, CD 25, arbor 6, shoulderD 6, neckD 6, taperL 0, Z 2 | TlToolDrill: TipAngle=180.0, ShoulderAngle=180.0, CornerRadius=0.0 | Holemaking; F 120 plunge 600 retr 1200 S 1000 |
| 23 | in | TlToolFeedMill | D 1, OAL 4, SL 1.2, CD 0.08, arbor 0.75, shoulderD 0.75, neckD 0.75, taperL 0, Z 4 | TlToolFeedMill: TipDiameter=0.5, LowerRadius=0.031, UpperRadius=0.031, LowerTaperAngle=90.0, UpperTaperAngle=76.0, ReliefTaperAngle=7.0 | Milling; F 5 plunge 25 retr 50 S 1000 |
| 23 | mm | TlToolFeedMill | D 25, OAL 100, SL 30, CD 2, arbor 20, shoulderD 20, neckD 20, taperL 0, Z 4 | TlToolFeedMill: TipDiameter=12.0, LowerRadius=1.0, UpperRadius=1.0, LowerTaperAngle=90.0, UpperTaperAngle=76.0, ReliefTaperAngle=7.0 | Milling; F 120 plunge 600 retr 1200 S 1000 |
| 24 | in | TlToolThreading | D 0.25, OAL 2.5, SL 1.5, CD 1, arbor 0.25, shoulderD 0.2, neckD 0.2, taperL 0.025, Z 4 | TlToolThreading: TipDiameter=0.0, TipAngle=180.0, TlRadiusType=0, CornerRadius=0.0, ThreadPitch=0.05, TlThreadShape=0, RootDiameter=0.0, ThreadAngle=0.0, NStarts=1 | Milling; F 5 plunge 25 retr 50 S 1000 |
| 24 | mm | TlToolThreading | D 6, OAL 60, SL 40, CD 25, arbor 6, shoulderD 5, neckD 5, taperL 0.5, Z 4 | TlToolThreading: TipDiameter=0.0, TipAngle=180.0, TlRadiusType=0, CornerRadius=0.0, ThreadPitch=1.0, TlThreadShape=1, RootDiameter=0.0, ThreadAngle=0.0, NStarts=1 | Milling; F 120 plunge 600 retr 1200 S 1000 |
| 25 | in | TlToolBarrelMill | D 0.5, OAL 3, SL 1.5, CD 0.875, arbor 0.375, shoulderD 0.25, neckD 0.25, taperL 0.354455, Z 4 | TlToolBarrelMill: BarrelLength=0.875, CornerRadius=0.05, ProfileRadius=0.75 | Milling; F 5 plunge 25 retr 50 S 1000 |
| 25 | mm | TlToolBarrelMill | D 12, OAL 75, SL 40, CD 22, arbor 10, shoulderD 6, neckD 6, taperL 11.3426, Z 4 | TlToolBarrelMill: BarrelLength=22.0, CornerRadius=1.0, ProfileRadius=19.0 | Milling; F 120 plunge 600 retr 1200 S 1000 |

## 9. Status summary

**Decoded (with sources above):**
- Full schema and creation script, seed rows, version gate.
- GUID encoding.
- TPT class rule and the MCToolType → table mapping.
- Every column → native property mapping with NULL defaults (READ).
- Tool Manager insert / delete / commit behaviour, new-item defaults, tool numbering.
- `OpToolInfo` producer and consumer rules, and 17 blob offsets (data-verified).
- The `TlProfileData` format (lines / arcs, radians, axes), the holder outline and editor algorithm, assembly stacking
  via CScalar.
- Validation rules, the feed/speed formulas, coolant OldStyle codes.

**NOT DECODED:**
- (Decoded since, outside .NET: the per-type parametric tool outline math of native
  `TlCore.dll!RetrieveParametricToolProfile`, in `formats/tool_profiles.md`.)
- DXF / level / quadrant / trim profile helpers (native).
- Native `ToolLibrarySystem` / `TlMgr` logic: native insert order, `_Header.id` generation, migrations 17 → 20.
- `op_tool_info` offsets for OAL, shoulder / flute length, arbor, pecks, etc. (the mccore `GetOpToolInfo` builders of
  §3.5 write none of them).
- Coolant state values (Flood / Mist / Tool 0/1/2) and OldStyle 0 / 1.
- `GaugeLength` / `GaugeDiameter`, `TlMatrix4x4` transforms, `RelationshipHierarchyXML`, `TlRelations`, `TlAuxData`,
  `TlToolToTlOpType`, `TlSourceID` semantics.
- Cut-param selection.
- Block drill (type 20).
- Lathe tables.
- LowerConnectionType values ≥ 4 (conflicting enums).
- Role of the shipped `Tools/*.mcam` files.
- Unit conversion in native code.
