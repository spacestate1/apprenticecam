# Mastercam 2017 tool libraries (`.tooldb`) and part-file tool records

## Container
A `.tooldb` file is a plain SQLite 3 database (magic `SQLite format 3`). Its schema is in `tooldb_schema.sql`
(49 tables, `_Header.version` 20). Primary keys are 16-byte GUID blobs, and tables join on them. Rows use class
inheritance, one table per level:
`TlAssemblyItem` (name, catalog id, IsMetric, type id) → `TlTool` (tool number, `OpToolInfo` blob) → `TlToolMill`
(geometry, `MCToolType`) → `TlToolEndmill` / `TlToolDrill` / `TlToolThreading` / `TlToolReamer` / ... (shape
specifics). Holders: `TlAssemblyItem` → `TlHolder`. Their outline is in `TlProfileData`.

Files shipped with Mastercam 2017 (`Users/Public/Documents/shared Mcam2017/mill/Tools`):

| Library | Tools | Holders | Units |
|---|---|---|---|
| `mill_mm` | 280 | 921 | mm |
| `mill_inch` | 427 | 921 | inch |
| `router_mm` | 315 | 0 | mm |
| `router_inch` | 266 | 0 | inch |
| `BT30`..`BT50`, `CT30`..`CT50`, `HSK63A/F`, `HSK1000A` | 0 | 8 to 172 each | `-mm` / `-in` |

## Columns used (all verified against the 280 tools of mill_mm)
| Field | Source |
|---|---|
| name | `TlAssemblyItem.Name` |
| units | `TlAssemblyItem.IsMetric` |
| type | `TlToolMill.MCToolType` (table `tooltypes_mill.tsv`: 3 Drill, 10 Flat, 11 Ball, 19 Bull, ...) |
| diameter, flute length, shoulder length, overall length, shank (arbor) diameter, flutes | `TlToolMill.OverallDiameter, CuttingDepth, ShoulderLength, OverallLength, ArborDiameter, FluteCount` |
| D / H offsets | `TlToolMill.DiameterOffsetNum, LengthOffsetNum` |
| corner radius | `TlToolEndmill.CornerRadius` (also Threading / Drill) |
| tip angle | `TlToolDrill.TipAngle` (also Threading / Reamer) |
| pitch | `TlToolThreading.ThreadPitch` |
| rpm, feed, plunge, retract | `TlOpParams.SpindleSpeed, FeedRate, PlungeRate, RetractRate` via `TlToolMill.TlOpParamsID` |
| coolant | `TlCoolant.OldStyle` via `TlOpParams.TlCoolantID`: 8 Off, 16 Flood, 32 Mist, 64 Thru-tool (toollib_net.md §6, TlCoolantViewModel); also op_tool_info +0x50. OldStyle 1 (mill_inch) and 0 (catalog) are NOT DECODED. The Flood / Mist / Tool state values are NOT DECODED (the earlier "1 = on, 2 = off" had no source; every shipped row is 2). toollib.c keeps the raw OldStyle and writes it back unless the flood flag was changed (then 16 / 8), see the code section below |
| canned cycle, pecks, chip break, dwell | `TlHolemakingOpParams` (same ID as TlOpParams) |
| holder outline | `TlProfileData` rows per holder, ordered by `Segment`. Type 2 = line `(x0,y0)-(x1,y1)`: x = radius, y = height from the gauge face. Arcs use radius / StartAngle / SweepAngle. |

`TlCutParam`, `TlAssembly` and `TlRelations` are empty in the shipped libraries.

## `OpToolInfo` = part-file `TL_ID` body (0x288 bytes)
The blob in `TlTool.OpToolInfo` has the same layout as a part file's tool record (class 0x4000 subtype 3, body at
record +0x68). Offsets found by correlating all 280 blobs with the SQL columns (every row matched):

| Offset | Type | Field |
|---|---|---|
| 0x00 | i32 | tool number |
| 0x04 | i16 | MCToolType |
| 0x08 | f64 | diameter |
| 0x10 | f64 | corner radius |
| 0x18 | f64 | thread pitch |
| 0x20 | f64 | tip angle: drills = included angle (118, 140, 90); chamfer mills = angle per side (45); flat mills 180 |
| 0x28 | i32 | diameter offset |
| 0x2c | i32 | length offset |
| 0x30 | f64 | feed |
| 0x38 | f64 | plunge rate |
| 0x40 | f64 | retract rate |
| 0x48 | i32 | spindle rpm |
| 0x56 | char[] | name |

Checked on the user's parts: BRACKET4 shows T12 flat D3, T20 drill 118° D10, a chamfer mill 45°, and thread mill T8 with
pitch 1.5. buckle-v12, NAKTO-BRAKE-BRACKET and thread-test also decode sensibly.

## Code: `viewer/toollib.c` / `toollib.h`
- Opens `.tooldb` read-only with SQLite (works straight off the read-only Windows mount).
- Our own library format is text, `.tools`: `MCAMUI-TOOLS 1` / `UNITS mm|inch` / `TOOL k=v ... name=...` /
  `THOLDER n r z ... name` / `HOLDER n r z ... name`. Writes are atomic (tmp + rename). Numbers are written with
  `%.17g`; a `.tooldb` → `.tools` → `.tools` round trip is byte-identical. Details in "The `.tools` text format" below.
- Writing `.tooldb` (2026-10-15): `toollib_save_tooldb` - existing tools updated in place by GUID (columns of
  formats/toollib_net.md 2.4, OpToolInfo patched at its decoded offsets), new tools = copies of DatabaseDefaults.tooldb's tool
  of that type and units with new GUIDs (the Tool Manager's AddTool), removed tools deleted (3.6), Save As starts from the source
  library (holders kept). Checked: edit / add / delete round trip on
  mill_mm, integrity_check ok, foreign_key_check pattern identical to Mastercam's own files (zero-GUID references).
- A new library (2026-10-16, `tm_create`): Mastercam's own SQL:create + SQL:base scripts, read from 2017's
  `ToolManager.Database.dll`, run as `DatabaseCreator.CreateDatabaseFile` does (toollib_net.md 3.2); no
  DatabaseDefaults copy any more. (The comment on `toollib_save_tooldb` in `toollib.h` still says "a copy of
  DatabaseDefaults.tooldb without its tools": stale, the .c is authoritative.)

Code map (all in `viewer/toollib.c` unless noted):

| Function / struct | Behaviour | Source cited by the code |
|---|---|---|
| `ToolLib` (toollib.h) | path, units (`metric` 1 mm / 0 inch / -1 unknown), tools `Tool *t`, holders `Holder *h` (name + (radius, height) outline from the gauge line up), `del` = GUIDs of `.tooldb` tools removed since loading | - |
| `toollib_load` | `SQLite format 3` magic → `load_tooldb`, else `load_text` (.tools) | - |
| `load_tooldb` / `load_extras` | one query for the common columns (table above, plus the GUID, IsMetric, canned-cycle fields), `OpToolInfo` kept when exactly 648 bytes; blob +0x10 / +0x18 / +0x20 / +0x48 fill columns the tables leave at 0, +0x06 radius type, +0x54 cutter material; `load_extras` = the wizard's per-type columns. Holders: TlProfileData lines; arcs linearised into 100 lines | toollib_net.md 3.5 (`ConvertExtensionsTool` 648-byte rule), 4.1; tlcore `TlProfileArcSegment::LinearizeArc` |
| library units | majority of the tools' `IsMetric`, else of all TlAssemblyItem rows | none: ours (Mastercam keeps units per item) |
| ball mill (type 11) with corner radius 0 on load | corner radius = dia / 2 | `tool_new` comment: both DatabaseDefaults ball mills have r = dia / 2 |
| `tool_type_name` / `tool_is_mill` / `tool_is_drill` | mill-product names from `Cnc::Tool::tl_string_chooser` (mccoreres 5014..5412); `is_mill_tool` mask 0x3affc00, drill = not mill | `decomp/mccore_toolfilt.c` (tl_string_chooser 0x1800dbef0), mccore 0x1800daa80 |
| `tool_new` | default tool of a type and units = the matching DatabaseDefaults.tooldb item; a given diameter replaces only the diameter | mccore TlAssemblyItemFactory FUN_180abb8f0 / FUN_180abba80 |
| `tool_op_info` / `tool_tp_tool` | the 0x288-byte op_tool_info (GetOpToolInfo per class, starting from the stored block) and the 0x698-byte tp_tool = TL_ID body (SetToOldTool); +0x50 = `coolant_style` when set (not re-checked against the flood flag, unlike `tool_coolant_style`), else 16 / 8 | `decomp/mccore_tooladd.c`; toollib_net.md 3.5 |
| `tool_outline` | display outline: `toolprofile.c` (formats/tool_profiles.md); on an error a single line (0, OAL) - (D/2, OAL); a holder stacked with its face at OAL | backplotbnci FUN_67d16db0 / FUN_67d18a90 (tool_profiles.md 5.1 / 5.2); tool_len = OAL: how it relates to the stick-out is NOT DECODED |
| `toollib_save_tooldb` | works on `<path>.tmp` (copy of the file, of the source library on Save As, or `tm_create`), attaches DatabaseDefaults.tooldb as `d`; deletes `del` (`delete_tool`: coolant, op params, then the TPT chain), updates existing rows (`write_cols`), copies a defaults tool with new v4 GUIDs for new ones (`copy_row`, references via `ensure_ref`); commit, then replaces the file | toollib_net.md 3.2-3.6 |
| `tool_coolant_style` | OldStyle written = the value read while (value == 16) agrees with the flood flag, else 16 / 8 | toollib_net.md 6 |
| `toollib_vacuum` | Compress: `VACUUM;` | TlToolLibrarySQL vtable slot 28, FUN_1807e8f80 (toolmanager_ui.md 1.5.8) |
| `toollib_find` | `.tooldb` / `.tools` files in `$MCAM_TOOLLIB_DIR`, `./tools`, the config dir (sorted together, case-insensitive), then the paths.conf `shared` folder's `mill/Tools`, `router/Tools` (sorted) | none: ours |
| `toollib_catalog_materials` / `toollib_catalog_sizes` | wizard Material list and standard-size grid from DatabaseCatalog.tooldb | toolmanager_ui.md 2.9 / 2.6.y |

CLI harness `viewer/tool_cli.c` (built as `tool_cli`): `tool_cli find` lists `toollib_find`'s paths; `tool_cli LIB
[--holders] [--save OUT.tools]` loads a `.tooldb` / `.tools`, prints one line per tool (number, type name, dia, corner
radius, tip angle, flute length, OAL, flutes, rpm, feed, plunge, H, coolant, holder, name), optionally the holders
(point count, top height) and writes the library as `.tools`; `tool_cli PART.mcx-9 --part` prints a part's tools as
`mcam_load` reads them.

### The `.tools` text format (INTENTIONAL, ours - not a Mastercam format)
Written by `toollib_save` / `tool_write`, read by `load_text` / `tool_parse` / `tool_read_holder`:
- line 1 `MCAMUI-TOOLS 1` (a file not starting with `MCAMUI-TOOLS` is refused); `UNITS inch` or `UNITS mm` (anything
  but `inch` = mm; a missing line = mm);
- per tool one `TOOL` line: ` key=value` for every field of the `TOOL_FIELDS` list (the `Tool` members of
  `viewer/mcam_load.h`: integers `%d`, doubles `%.17g`), then the text fields `desc`, `mfg_code`, `chuck`, `mat_name`
  (only when non-empty; bytes <= space, `%`, `=` and >= 0x7f written as `%XX`), then ` opinfo=` + 1296 hex digits when the
  tool has an op_tool_info block, and last ` name=` + the name (may contain spaces). Unknown keys are ignored; a value
  that is not finite or above 1e9 in magnitude is skipped; a line without a diameter > 0 is dropped; `is_metric`
  missing = the file's UNITS;
- `THOLDER n r0 z0 ... r(n-1) z(n-1) name` right after a TOOL line = that tool's holder outline (n <= 100000);
- `HOLDER n r z ... name` = a library holder.
- Files are written to `<path>.tmp` and renamed over the target.
