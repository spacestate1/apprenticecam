# Feeds and speeds (decoded 2026-10-15)

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

Sources: mccore.dll `calc_op_fs` @180336700 (class CFeedCalc, FeedSpeedDlg.cpp / FeedSpeedCalculator.cpp),
FUN_180338050 (rpm), FUN_180337a40 (feed), FUN_180337df0 (plunge), FUN_180337f20 (retract), FUN_180337bb0
(CFeedCalc::CalcFpt), FUN_1803381f0 + `CalculateSFMFromMaterial` @180334390 (SFM), `GetSFMIndex` @180335580,
FUN_18033b660 (context: op + tool), FUN_180336e60 (CFeedCalc ctor), `GetMaterialFromName` @1803351c0,
`list_op_recalc_sf` @1800bf960 (Toolpaths > Recalculate feeds/speeds), `op_is_*` classifiers; mastercam.dll
`MatlLibrary` @18015b7d0 -> FUN_180157980 case 6, `OMgetGrpMaterialName` @180323240; mcmachinedef.dll
`CCNCMachine::MatlLibFile` @180076a10 (= this + 0xd30), `CCNCMachine::MaxToolSpindleRPM` @18006b850..;
mcmill.dll `CToolProp::SetFeedsAndSpeedsFromSource` @180245be0, `CToolProp::GetFeedsAndSpeedsFromMaterial` @18023d7a0;
opmanres dialog 1589 "Group PG2" (machine group Tool Settings page); mccore `GetCurVerExtFromPrvVerExt` @1803486a0.
Decompile in the repo: `decomp/mccore_calc_fs.c` (list_op_recalc_sf, calc_op_fs, FUN_180338050, FUN_180337a40,
FUN_180337df0, FUN_180337f20 and their callees to depth 2, incl. FUN_180337bb0 CalcFpt at l.2137); tolerance defaults
`decomp/mccore_systol.c`.

## Machine group (op_group record, 0x1ae0 bytes; tool settings block at +0x1178)
| Offset | Field |
|---|---|
| +0x119c (block +0x24) | Feed Calculation: 0 From tool, 1 From material, 2 From defaults, 3 User defined (dialog 1589 radios 469-475, DDX radio order) |
| +0x11d0 (block +0x58) | material name, char[0x51] (OMgetGrpMaterialName) |
| +0x1180 / +0x1188 / +0x11a0 / +0x11b0 (block +8 / +0x10 / +0x28 / +0x38) | User defined plunge / retract / feed / spindle (double; rpm = (int)) |
Seen in the user's parts: mode 0, material "ALUMINUM mm - 2024", user 5000 rpm / feed 100.

## Material library (`*.mcam-materials`, X9 `*.materials-9`)
Same record container as a part file. A material = record magic 0x4000, n = 1032, body (struct material, 0x3a0 bytes)
at record +0x68:
| Body offset | Field |
|---|---|
| 0x00 | name char[0xc8] ("ALUMINUM mm - 2024") |
| 0xc8 | i16 units: 0 inch (SFM ft/min, FPT in), 1 (or 2) metric (m/min, mm) |
| 0xd0 | base cutting speed |
| 0xd8 + 8 i | % per operation class i = GetSFMIndex: 0 drill, 1 contour, 2 pocket, 3 surface (shipped 60 / 100 / 100 / 80) |
| 0xf8 | base feed per tooth |
| 0x100 + 8 t | % per tool type t = MCToolType 0..25 |
| 0x1d0 + i | tool material i enabled (6: tool +0x54 = 1..6) |
| 0x1d8 + 8 i / 0x208 + 8 i | speed % / feed % for tool material i |
Shipped: shared Mcam2017/MATERIALS/Default.mcam-materials (138), ROUTER.mcam-materials (39).

Which library: the part's own material list first (TpMatList::MaterialByName; part record 0x200 sub 0xE table; the
user's parts have none), else `import_matl_ent(name, MatlLibrary())` = the machine definition's material library
(CCNCMachine +0xd30; in the part's machine record - class 0x4000, record +0x2c = 22, n 5000 - the path string is at record +0xce7,
measured on 6 parts: "\Materials\Default.materials-9"). Old extensions map to the current one
(GetCurVerExtFromPrvVerExt: .materials-9 .. -5 / .materials -> .mcam-materials). The leading "\" path is relative to
the shared data folder (resolution measured: shared Mcam2017/MATERIALS exists; the code that prefixes it is NOT
DECODED). Not found + "show" flag: message 0xfc6 and the material picker (mlib_select) - NOT DONE here.

## Tool fields used (op_tool_info = TL_ID body, 0x288 bytes)
type +0x04, diameter +0x08, pitch +0x18 (inch tools: threads per inch), feed +0x30, plunge +0x38, retract +0x40,
rpm +0x48 (i32), flutes +0x52 (i16, CheckNumberOfFlutes), tool material +0x54 (i16 1..6, < 1 -> 1), metric +0x258.

## Calculation (calc_op_fs)
Exit with nothing changed for: lathe ops (op_is_lathe), old-NCI ops (op_is_oldnci), no-tool ops (op_is_notool),
unless op_is_msurf. Mode 1 with a material that cannot be found: nothing changed.
- rpm (FUN_180338050):
  - mode 0: the op's own rpm (context = op tool info; with a tool system tool the tool's info is loaded and the op's
    copied over it except plunge / retract, FUN_18033b660)
  - mode 3: (int)group user rpm
  - else: SFM = CalculateSFMFromMaterial; D = tool dia (/25.4 if the tool is metric); D < tol -> fail;
    rpm = (int)(SFM * 3.82 / D); negative when the spindle is CCW
  - CalculateSFMFromMaterial: k = GetSFMIndex(opcode); SFM = base * (k >= 0 ? pct[k] / 100 : 1); mode 1: x op speed %
    (if > tol), x tool-material speed % when enabled and not 0 or 100; metric material: SFM /= 0.3048
  - clamp: |rpm| > machine max (CCNCMachine::MaxToolSpindleRPM: spindle component MaxRPM; 10000 with no machine;
    0x7fffffff without a spindle component) -> max
- feed (FUN_180337a40): mode 0 op feed; mode 3 group user feed; else |rpm| x z x FPT, z = 1 for tool types 4 / 5
  (taps), else flutes. FPT (CalcFpt) = base FPT x type % / 100; metric material /25.4; mode 1: x op feed % (if > tol),
  x tool-material feed % (same rule); taps with pitch p > tol: metric tool p / 25.4, inch tool 1 / p.
- plunge / retract: mode 0 tool's; mode 3 group user values; else feed x 0.5. Drill opcodes 2, 0x37-0x39:
  plunge = retract = feed.
- metric material: feed, plunge, retract x 25.4 - exactly when the material's units field == 1 (decomp/mccore_calc_fs.c
  l.299-302, the struct is loaded only for group mode 1); the / 0.3048 and / 25.4 steps above test units 1 or 2.
- Mode 3 with the op's "user" flag (+0xf0 > 0) keeps a non-zero op value (meaning of +0xf0 NOT DECODED; not used).

Op classes: op_is_drill = opcodes 2, 0x1c, 0x37-0x39, 0x132; op_is_contour = 1, 0x33-0x35, 0x4a, 0x53, 0x82,
0x85; op_is_point = drill or 0x12, 100, 106, 113; GetSFMIndex: drill 0; contour / point / 0x13 / 0x65 / 0x8a 1;
3 / 0x66 / 0x69 / 0x86 / 0x89 2; msurf and 0x15, 0x16, 0x18-0x1b 3; 0x68 / 0x88 0; else none (base speed only).
op_is_lathe = 0x3c-0x48, 0xc9-0xec.

## Where it runs
- Toolpaths > Recalculate feeds/speeds (list_op_recalc_sf): every selected non-lathe op, calc_op_fs(op, -, clamp 1,
  material = group's, group = active, feed % 0, speed % 0, CW); changed ops are marked dirty (regenerate).
- Toolpath dialog, tool chosen (CToolProp::SetFeedsAndSpeedsFromSource(group mode)): 0 GetFeedsAndSpeedsFromTool,
  1 GetFeedsAndSpeedsFromMaterial (calc_op_fs; then plunge = retract = feed x 0.5), 2 GetFeedsAndSpeedsFromDefaults
  (defaults file), 3 nothing. The dialog-state condition (+0x31834 / +0x31835, drill with +0x11c0 == 3) NOT DECODED.

## NOT DECODED
Machine max rpm from the machine definition components (CToolSpindleComponent::MaxRPM - not read here, so no clamp
is applied for parts with a machine; 10000 applies only with no machine at all); the shared-folder prefix rule; mode 2 (From defaults) values in the dialog;
the Feed & Speed Calculator dialog 882 and Material List / Definition dialogs 238 / 248 (UI).

## apprenticeCAM implementation (2026-10-15)
viewer/feedspeed.c (calc_op_fs, CalcFpt, CalculateSFMFromMaterial, GetSFMIndex, op_is_*, material library reader,
X9 extension mapping), mcam_load.c (group tool settings, machine material library path, tool flutes +0x52 / cutter
material +0x54 / metric +0x258), toollib.c (cutter material from the OpToolInfo blob), mcamui.c recalc_feeds
(Toolpaths > Recalculate feeds/speeds, on the selection via edit_selected - shared with Edit Common Parameters) and
fs_from_source (tool chosen in the toolpath dialog). Checked: ALUMINUM mm - 2024, D3 4-flute flat -> 12733 rpm,
5093.2 mm/min, plunge / retract 2546.6 (= 120 m/min / (pi x 3 mm), x 4 x 0.1 mm); drill class 60 %, surface 80 %,
drill tool type 120 % FPT; From tool / User defined pass through. Test hooks: fsmode:N, recalc.
Deliberate deviation (safety): a result is applied only when rpm != 0 and feed / plunge / retract are positive and
finite; Mastercam writes partial results (e.g. feed 0 when flutes = 0). Spindle clamp: not applied (machine spindle
max NOT DECODED).

### Code: viewer/feedspeed.c / feedspeed.h (state 2026-10-16)
| code | source | what it does |
|---|---|---|
| `TOL` = 5e-5 | config 1000 tol (decomp/mccore_systol.c) | every "> tol" test (was the same value before; now cited) |
| `op_is_lathe` / `_msurf` / `_drill` / `_contour` / `_point` / `_chook` / `_oldnci` / `_solid_ctrl` / `_notool`, `fs_op_skipped` | mccore op_is_* | op classes above; skipped = !msurf and (old NCI or no tool or lathe) |
| `fs_sfm_index` | GetSFMIndex @180335580 | 0 drill, 1 contour, 2 pocket, 3 surface, -1 none |
| `Material`, `MatLib`, `matlib_load`, `matlib_find` | material record (magic 0x4000, body 0x3a0 at +0x68) | reads every material record of a .mcam-materials file; skips the part-style material table payload (record 0x200 sub 0xE); exact-name lookup |
| `matlib_resolve` | GetCurVerExtFromPrvVerExt @1803486a0 | old extensions -> .mcam-materials; a leading separator -> `<APP_DIR_SHARED>` (the shared-folder rule itself is NOT DECODED, measured) |
| `calc_sfm` | CalculateSFMFromMaterial @180334390, FUN_1803381f0 | base x class %; From material: x op speed %, x cutter-material speed %; metric material (units 1 / 2) / 0.3048 |
| `calc_fpt` | CFeedCalc::CalcFpt FUN_180337bb0 | base FPT x tool-type %; metric material / 25.4; From material: x op feed %, x cutter-material feed %; taps (types 4 / 5): pitch (metric tool p / 25.4, inch tool: the tool's part-unit pitch) |
| `calc_feed` | FUN_180337a40 | From tool: op feed; User: group feed; else abs(rpm) x flutes (1 for taps) x FPT |
| `fs_calc` | calc_op_fs @180336700 with FUN_180338050 / FUN_180337df0 / FUN_180337f20 | rpm = (int)(SFM x 3.82 / D) (D in inch), negative for CCW, clamp only when the caller passes max_rpm > 0 (the UI passes 0); plunge / retract = 0.5 x the computed feed (drill opcodes 2, 0x37-0x39: = feed); From material with material units 1: feed / plunge / retract x 25.4; From defaults (mode 2) is computed like From material without the op / cutter-material % factors (the dialog values of mode 2 are NOT DECODED); `ok` = 1 only when all values are valid (safety deviation above) |

Callers: mcamui.c `recalc_feeds` (Toolpaths > Recalculate feeds/speeds) and `fs_from_source` (tool chosen in the
operation dialog), both with feed % / speed % 0 and max_rpm 0.
