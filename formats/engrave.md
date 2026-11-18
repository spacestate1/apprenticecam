# Engrave (Mastercam 2017 opcode 416) - decode for a port

Status 2026-10-26. Sources: `engrave.dll` (Ghidra project `~/mastercam-re/ghidra_engrave`, decompiles in
`decomp/engrave/*.c`), `engraveres.dll` (dialogs: `tools/pe_dialogs.py`; strings: `tools/pe_strings.py`, ids below are
engraveres RT_STRING ids), the engine `EStamp7.exe` from the install data folder (Ghidra project `~/mastercam-re/ghidra_estamp`),
the help `~/mastercam-re/chm/mcengrave/Engrave/*.htm`, the engine vendor's German help
`<windows>/Users/Public/Documents/shared Mcam2017/engrave/H4HELP.TXT` (cp1252; cited as "H4HELP #nnnn"), the shipped engine
I/O sample in the same folder (`EST_DAT.TXT`, `ENGCONT.LG1`, `ENGTOOLP.LG1`), the four `*.mcam-defaults` and the corpus
(measurement script `tools/engrave_dump.py`). Everything not traced to one of these is marked **NOT DECODED**.

Decompile files (all under `<repo>/decomp/engrave/`, gitignored):
`exports.c` (C-hook exports), `strrefs1.c` (EST_DAT writer, tool writer, CEngToolP ctor), `core1.c`..`core6.c`, `heights.c`,
`pages.c` (DoDataExchange of the 3 pages / filter dialog), `pagexfer.c` (op block <-> pages), `regen_disasm.txt` (call-site
argument order of the regen), `lg1.c` (LG1 reader / writer), `estamp_main.c` (EStamp7 EST_DAT parser), `syms.txt`, `vtables.txt`.

## 0. Summary

| part | state |
|---|---|
| Registration, op creation, chain rules | DECODED (sec. 1) |
| Dialog pages, every control <-> op record field, defaults | DECODED except "Do 3D Engraving moves" (1079), "rapid retract" (1075) storage (sec. 2) |
| Op record -> engine parameters (EST_DAT.TXT keys, units, derived values) | DECODED except the tool "calc type" `KorrekturVerf` value and `LFSchaft` source (sec. 3) |
| Engine I/O file formats (EST_DAT.TXT, LG1 contours / toolpath, FEHLER) | DECODED for what engrave.dll reads/writes; LG1 tool-header tags NOT DECODED (sec. 4) |
| The geometry engine (offset with V-tool "Ausspitzen" corners, roughing hatches, ordering) | DECODED for the paths the corpus uses (2D / 2D3D finishing, zigzag / one-way roughing, blocks, re-sort, depth cuts, LG1 output) and ported: **formats/engrave_engine.md**, `viewer/engrave.c`; the rest refused there |
| LG1 toolpath -> NCI (z mapping, retracts, feeds, arc filter) | DECODED (FUN_180041910, formats/engrave_engine.md 8) |
| Wrap (2 curves / surfaces), remachining | parameters DECODED, algorithms NOT DECODED |
| Oracle | 2 ops with NCI (engraved coaster ops 2 / 4) + 1 without, 4 defaults records; measured offsets and stepovers match the HCAM rules exactly (sec. 7) |
| App wiring (2026-10-26) | DONE: CAM_ENGRAVE (opcode 416) - gallery / menu "Engrave", chaining prompt engraveres 3, the "Engraving" sheet with pages 1802 / 1803 from engraveres (rows e1802 / e1803 of formats/opdlg_fields.tsv), generate `viewer/cam2d_engrave.c` -> engrave.c, arc filter, post, save as opcode 416 from the defaults library's Engrave op. Not yet: the sub-dialogs (1804 depth cuts, 1805 ramp, 1806 filter, 1807, 10502) behind the push buttons; spline chains since 2026-10-26 (formats/spline_chain.md: the coaster's NURBS chains regenerate; `APP=1 tools/tests/engrave_test.sh`); 1808 depth warning (refused); 2018 records (S at +0x12f8) in the dialog |

## 1. What Engrave is

- A C-hook, not a mcmill toolpath. `~/mastercam-re/files/engrave.ft`: `APPLICATION "MWENGRAVE"`, `FUNC_DLL "chooks\engrave.dll"`,
  `RES_DLL "Resources\engraveRes.dll"`, `CATEGORY "Router C-Hooks"`, `FUNCTION CPP "m_main"`, `FNAME "Engrave"`, `TIP 159`
  (engraveres 159 "Engraving"). The ribbon / gallery entry is 3628 "module MWENGRAVE m_main" (formats/opmanager_menu.md l.92).
- engrave.dll is a ModuleWorks wrapper ("mwEngrave", RTTI `engrave::mwEngrave*`, pdb path
  `H:\project_17\mastercam\quickpart\engrave\Release\engrave.pdb`, strings `mwEngrave_Exception`, `MODULEWORKS`). Exports
  (`syms.txt`): `m_main` 0x180215530, `m_open`, `m_close`, `m_notify`, `YourPrmFunction` 0x18025a550, `YourToolFunction`,
  `YourGeomFunction`, `YourNciFunctionL/R`, `YourRegenFunction` 0x18025a600, `V9ToVxChookOperation` 0x18025a290,
  `Adv5axBackplot4thAxis`. All dispatch through the app object `DAT_1806da9c8` / `DAT_1806e4d90` vtables (`exports.c`).
- Licence: `MASTERCAM.DLL::ShouldEngraveRun` import, class `mwEngraveLicChecker`, file `engrave.acc` (engraveres 139-150).
- The op is OPCODE 416 (formats/opcodes.tsv; mccoreres 277 "Engrave"). The op record carries a C-hook header at +0x1218
  (f64 19.1 in the coaster / 20 in the defaults and i32 at +0x1220 = 51 / 22 / 460: meaning NOT DECODED; then `"Engraving Operation"` (engraveres 19, default op name) at +0x1228, DLL name
  `"ENGRAVE.DLL"` at +0x1264, the four names `YourPrmFunction` / `YourToolFunction` / `YourGeomFunction` / `YourRegenFunction`
  at +0x128c.. ) followed by the Engrave block S (sec. 2.2). A second, stale copy of this header with a lowercase `engrave.dll`
  sits at +0xd38..+0xf3c in every record (corpus and defaults; never read by the decoded code - NOT DECODED purpose).
- The geometry is computed by an **external process**: engrave.dll writes `ENGCONT.LG1` (contours), `EST_DAT.TXT`
  (parameters) and runs `estamp7.exe` in `<shared Mcam2017>\ENGRAVE\` (FUN_180013490 `strrefs1.c` l.1098-1126 builds the folder
  with `"%s%s"` + `"ENGRAVE"`; FUN_180001b90 l.1049 `"estamp7.exe"`), waits for it (FUN_18004a4e0 `core2.c` l.98-211:
  `CreateProcessA(NULL, "<exe> <GetTickCount()^0x5aeca017>", ..., cwd = data folder)`, `WaitForSingleObject(INFINITE)`), then
  reads `FEHLER=` from EST_DAT (FUN_180049c30 `core3.c` l.280-484) and the toolpath `ENGTOOLP.LG1`. EStamp7.exe is a Delphi
  (FastMM Borland Edition) 32-bit PE: the German engraving CAM "HCAM"/"eStamp" by AHSoft (strings `AHSoft-Data LG1, H2R`,
  module names HRKORRD / HSCHRAFF / KONTBILD in its messages).
- Other tools in the same folder (not part of the toolpath): Txtchain (text on a chain), Rast2Vec, EPS reader (help TOC
  `MCEngrave.hhc`).

### 1.1 Operation lifecycle (engrave.dll)

1. Chain selection: prompt engraveres 3 "Engraving: Select chains"; open chains are dropped (4 "Open chains are ignored");
   closed boundaries required (help "Creating an engraving toolpath": "Engraving requires closed boundaries"; inner boundaries
   are islands). The chain height: FUN_180019800 (`core4.c` l.1337-1413) runs `chain_to_ctour_array(chain, ..., view =
   active Tplane, tol = S.tolerance*10, ...)` per chain (the contour build FUN_18001e400 uses S+0x28 / 10, formats/engrave_engine.md 8) and returns the first chain's z; a different z on another chain shows
   message 0x2a37 once (engraveres 10 "Chains on different heights are found. They will be forced to same height.").
2. Dialog (sec. 2), Geo manager for wrapping (dialogs 1800 / 1801, FUN_18001cc00 `core4.c` l.1164-1330: buttons Chains / Ref Pt /
   Upper / Lower chain / Start point / Surfaces / Surf curve).
3. Regen = FUN_180017d80 (`core4.c` l.36-784, the `mwMachToolPTempl<mwEngraveToolPage>` op object, `this` = CEngToolP):
   a. heights / tool / remachining tools: FUN_18001bb30 (`heights.c`, sec. 3.1);
   b. engine tools: tool 1 = `mwEngraveChamferMill` when the Mastercam tool type is 12 (Chamfer Mill) or 21 (Engrave Tool),
      else `mwEngraveEndMill` (l.217-366); remachining roughing tool (l.367-481);
   c. `mwEngraveParams` P built by FUN_18002e410 (`core4.c` l.860-913; argument order from `regen_disasm.txt`
      0x180018c6f-0x180018d96, sec. 3), containing `mwEngraveToolWriterParams` W (FUN_18002e880, depth cuts / ramp) and
      `mwEngraveToolpathParams` T (FUN_18002eb10, heights / feeds);
   d. compute FUN_180038230 (`core4.c` l.5-28): validate P (FUN_18004e4d0 `core5.c`), write ENGCONT.LG1 (FUN_180052120 ->
      FUN_18005a5c0), write EST_DAT.TXT (FUN_180052250 -> FUN_18004f870), run the exe (FUN_18004a7f0 -> FUN_18004a4e0);
   e. read ENGTOOLP.LG1 and write NCI (FUN_180041910 `core5.c` l.220-597, via `mwEngraveToolpathImpl` Write3dMove
      FUN_18001de90 / WriteEndRefMove FUN_18001d480);
   f. if the op's filter flag (op +0x2d2, record +0x33a) is set: `nci_manager(op, 10, ...)` = Mastercam's arc filter
      (l.639-646; failure text "No success by calling the arc filter").

## 2. Dialog and parameters

### 2.1 Pages (engraveres dialogs, `pe_dialogs.py`)

Property sheet "Engraving" (engraveres 8): page 1 = the standard Toolpath parameters page (`mwEngraveToolPage`, a
`mwMachToolPTempl`), page 2 = dialog 1802 "Engraving parameters", page 3 = dialog 1803 "Roughing/Finishing". Sub-dialogs:
1804 Depth cuts, 1805 Ramp, 1806 Filter settings, 1807 Engrave remachining, 1808 depth warning, 10502 Wrap toolpath,
1800 / 1801 Engrave Geo Manager.

- 1802: Machining direction (1055 Climb / 1057 Conventional); [x]1071 Clearance... 1047 + 1050 Absolute / 1052 Incremental;
  1078 "Use clearance only at the start and end of operation"; [x]1073 Retract plane... 1037 + 1039 / 1042; Feed plane... 1030 +
  1031 / 1032; 1075 "rapid retract"; Top of stock... 1026 + 1027 / 1028; Depth... 1022 + 1023 / 1024; 1143 "Roll cutter around
  corners" combo (engraveres 102 None, 103 Sharp); 1118 "XY Stock to leave"; 1119 "Angle for flat cutter offset"; [x]1087
  Depth cuts...; [x]1088 Filter...; [x]1102 Remachining...; [x]1149 Wrapping...; 1104 "Engrave Type" (static).
- 1803: [x]1059 Rough; 1038 list (engraveres 21 Zigzag, 22 One way, 23 Parallel Spiral, 24 Clean Corners); 1034 "First rough,
  then finish"; 1041 "Smooth contours"; 1079 "Do 3D Engraving moves"; 1082 Sorting combo (26 Selection order, 27 Top to bottom,
  28 Left to right); [x]1083 Entry-ramp...; 1053 Roughing angle; 1065 Stepover %; 1081 Stepover distance; 1049 Tolerance;
  Cut geometry 1061 at depth / 1063 on top; Enter On 1062 Interior Corners / 1064 Chain start point / 1067 Midpoint longest line;
  1036 "Try to keep tool down"; 1048 "Start at corners".
- 1804: 1086 # of cuts; 1089 equal depth cuts / 1090 constant volume depth cuts. 1805: 1151 "Plunge angle (90 = vertical)".
  1806: 1049 Cut tolerance, 1094 Look ahead, 1093 Create arcs, 1095 Minimum / 1096 Maximum arc radius.
  1807: 1105 Previous operation / 1106 Roughing tool; 1058 Major diameter, 1072 Angle, 1076 Tip diameter; 1144 "Finish after
  remachining". 10502: 10505 Between 2 curves / 10506 On surfaces, 10508 Grid distance. 1808: 1166 use max depth / 1167 return /
  1168 ignore (text "The maximum depth of cut for this tool is %s").

DoDataExchange (`pages.c`): 1802 = FUN_180009600 (page members +0xd48..+0xdc0), 1803 = FUN_18000d3d0 (+0xe30..+0xe80, rough
list selection +0xd48), filter dialog = FUN_180021de0. Enable rules FUN_18000e030: rough list / 1034 enabled only with Rough on
and not remachining; 1053 / 1054 (roughing angle) only for rough types 0 / 1; 1065 / 1066 / 1081 / 1080 / 1036 / 1083 only with
Rough on; 1060 (Entry-ramp button) with Rough and the ramp box; 1059 (Rough) disabled when remachining is on (remachining forces
roughing: `pages.c` l.79-82 sets Rough = 1, list = 0 Zigzag; help "remachining toolpaths always use the zigzag cutting pattern").
Stepover distance (display) = `d * stepover% / 100` with `d` = tool +0x408 (tip diameter) for tool types 12 / 21 when the
tool's +0x30 is nonzero, else tool +0x18 (diameter) (FUN_18000d3d0 l.104-119, FUN_18000d8d0).

### 2.2 The Engrave block S (op record +0x12f0, 0x1d6 bytes)

FUN_18001c3f0 (`core2.c` l.15-95) copies 0x1d6 bytes from operation +0x1288 (= OP_ID record +0x12f0, the record body starts
0x68 before the operation struct) into CEngToolP +0x3400 and back (FUN_1800174f0); `S+0x128` must be `'ENGR'` (0x52474e45),
else the block is reset to defaults (FUN_18001b900 / FUN_18001b920: magic, `S+0x12c = 1`, and `S+0x130 = 0` when the old block
had no flag). Pages <-> S: FUN_18001a690 (S -> pages) and FUN_18001a3e0 (pages -> S) in `pagexfer.c`; sanitising on load
FUN_18001ddc0 (`core4.c` l.1072-1104) and FUN_18001bb30 (`heights.c`). In the 2018 record (10560 bytes, `3D Door Machined.mcam`)
the whole C-hook header and S sit 8 bytes later (magic at +0x1420) - S offsets below are unchanged relative to S.

Defaults: columns MI / MM / RI / RM = Mill_Inch / Mill_MM / Router_Inch / Router_MM `.mcam-defaults` (their single Engrave op),
C = engrave.dll CEngToolP constructor FUN_180013490 (`strrefs1.c` l.1131-1283, inch / mm), coaster = `engraved coaster.mcx-9`
op 2 (op 4 differs only in S+0x20).

| S+ | type | control (page member) | meaning | MI | MM | RI | RM | coaster op 2 |
|---|---|---|---|---|---|---|---|---|
| 0x00 | u8 | 1055/1057 radio (1802 +0xd6c) | 0 climb, 1 conventional | 0 | 0 | 0 | 0 | 0 |
| 0x08 | f64 | 1065 (1803 +0xe50) | **overlap = 100 - stepover %** (l.19 / l.97 of `pagexfer.c`) | 20 | 20 | 20 | 20 | 25 |
| 0x10 | u8 | 1034 (+0xe38) | first rough, then finish | 0 | 0 | 0 | 0 | 0 |
| 0x11 | u8 | 1059 (+0xe48) | Rough on | 0 | 0 | 0 | 0 | 1 |
| 0x12 | u8 | 1036 (+0xe5c) | Try to keep tool down (not passed to the engine params; use NOT DECODED) | 0 | 0 | 0 | 0 | 0 |
| 0x13 | u8 | 1041 (+0xe58) | Smooth contours | 1 | 1 | 1 | 1 | 1 |
| 0x14 | i32 | list 1038 (+0xd48) | rough type 0 Zigzag, 1 One way, 2 Parallel Spiral, 3 Clean Corners (4 is read as 1, regen l.549-552) | 0 | 0 | 0 | 0 | 0 |
| 0x18 | f64 | 1053 (+0xe30) | roughing angle (deg) | 0 | 0 | 0 | 0 | 0 |
| 0x20 | u8 | 1061/1063 (+0xe4c) | Cut geometry: 0 at depth, 1 on top | 1 | 1 | 1 | 1 | 1 (op 4: 0) |
| 0x28 | f64 | 1049 (+0xe40) | tolerance (C: `curve_chord_dev * 0.5`) | 0.001 | 0.025 | 0.001 | 0.025 | 0.001 |
| 0x30 | i32 | 1023/1024 (1802 +0xd60) | depth: 0 absolute, 1 incremental | 1 | 1 | 1 | 1 | 0 |
| 0x34 | i32 | 1031/1032 (+0xd64) | feed plane mode | 1 | 1 | 1 | 1 | 1 |
| 0x38 | i32 | 1027/1028 (+0xd68) | top of stock mode | 0 | 0 | 1 | 0 | 0 |
| 0x3c | i32 | 1050/1052 (+0xd74) | clearance mode | 0 | 0 | 1 | 0 | 0 |
| 0x40 | i32 | 1039/1042 (+0xd78) | retract mode | 1 | 1 | 1 | 1 | 1 |
| 0x48 | f64 | 1047 (+0xd80) | clearance | 2 | 50 | 2 | 50 | 2 |
| 0x50 | f64 | 1037 (+0xd88) | retract | 0.25 | 25 | 0.25 | 25 | 0.25 |
| 0x58 | f64 | 1026 (+0xd58) | top of stock | 0 | 0 | 0 | 0 | 0 |
| 0x60 | f64 | 1022 (+0xd48) | depth | 0 | 0 | -0.05 | 0 | -0.135 |
| 0x68 | f64 | 1030 (+0xd50) | feed plane | 0.1 | 5 | 0.1 | 5 | 0.1 |
| 0x70 | i32 | [x]1071 (+0xd90) | clearance on | 0 | 0 | 0 | 0 | 0 |
| 0x74 | i32 | [x]1073 (+0xd94) | retract on | 1 | 1 | 1 | 1 | 1 |
| 0x78 / 0x80 / 0x88 | f64 | 1058 / 1072 / 1076 (+0xde0/de8/df0) | remachining roughing tool major dia / angle / tip dia | 0 | 0 | 0 | 0 | 0 |
| 0x90 | i32 | [x]1102 (+0xda0) | remachining on | 0 | 0 | 0 | 0 | 0 |
| 0x98 | i32 | (1803 +0xe70, no control in DDX) | NOT DECODED | 0 | 0 | 0 | 0 | 0 |
| 0x9c | i32 | 1082 (+0xe74) | sorting 0 Selection order, 1 Top to bottom, 2 Left to right | 2 | 2 | 2 | 2 | 2 |
| 0xa0 | i32 | [x]1087 (+0xda4) | depth cuts on | 0 | 0 | 0 | 0 | 0 |
| 0xa4 | i32 | [x]1083 (+0xe7c) | entry ramp on (stored only when Rough is on, `pagexfer.c` l.50-53) | 0 | 0 | 0 | 0 | 0 |
| 0xa8 | i32 | 1086 (+0xdfc) | # of depth cuts (C 3; >100000 -> 1) | 2 | 0 | 2 | 0 | 2 |
| 0xac | i32 | 1089/1090 (+0xe00) | radio index (0 equal, 1 constant volume; >10 -> 1) | 0 | 0 | 0 | 0 | 0 |
| 0xb0 | f64 | 1151 (1803 +0xd50) | plunge angle (deg; outside 10..90 -> 45) | 45 | 45 | 45 | 45 | 45 |
| 0xb8 | i32 | 1048 (+0xe78) | Start at corners (engine use NOT DECODED) | 0 | 0 | 0 | 0 | 0 |
| 0xbc | i32 | 1105/1106 (+0xdd8) | remachining stock from: 0 previous operation, 1 roughing tool | 0 | 0 | 0 | 0 | 0 |
| 0xc0 | i32 | 1078 (+0xd9c) | use clearance only at start and end | 0 | 0 | 0 | 0 | 0 |
| 0xc8 | f64 | 1118 (+0xda8) | XY stock to leave | 0 | 1 | 0 | 1 | 0 |
| 0xd0 | i32 | combo 1143 (+0xdb0) | roll: stored `2 - (sel != 0)`: 2 = None, 1 = Sharp (>2 -> 1) | 1 | 1 | 1 | 1 | 1 |
| 0xd8 | f64 | 1119 (+0xdb8) | angle for flat cutter offset (deg; outside (0, 90] -> 0) | 0 | 1 | 0 | 1 | 0 |
| 0xe0 | u8 | 1144 (+0xdf8) | finish after remachining | 0 | 0 | 0 | 0 | 0 |
| 0xe4 | i32 | [x]1149 (+0xdc0) | wrapping on (== 1) | 0 | 0 | 0 | 0 | 0 |
| 0xe8 | i32 | 10505/10506 (+0xe04) | wrap: 0 between 2 curves, 1 on surfaces | 0 | 0 | 0 | 0 | 0 |
| 0xf0 | f64 | 10508 (+0xe08) | wrap grid distance (<=1e-12 or >1e6 -> 0.001 in / 0.02 mm) | 0.001 | 0.02 | 0.001 | 0.02 | 0.001 |
| 0x128 | 4s | - | magic `ENGR` | | | | | |
| 0x12c | u8 | - | 1 = block has the Enter-On field | 1 | 1 | 1 | 1 | 1 |
| 0x130 | i32 | 1062/1064/1067 (+0xe80) | Enter On radio index: 0 interior corners, 1 chain start point, 2 midpoint longest line | 0 | 0 | 0 | 0 | 0 |
| 0x134..0x1d5 | | - | wrap geometry / other: NOT DECODED | | | | | |

Not in S: filter (1088 / dialog 1806) uses the standard operation filter fields: on = op +0x2d2 (record +0x33a), create arcs =
op +0x2d3 (+0x33b), look ahead = op +0x2d0 i16 (+0x338), tolerances op +0x2b8 / +0x2c0 / +0x2c8 f64 (record +0x320 / +0x328 /
+0x330) (`pagexfer.c` l.55-60, l.142-148). Feeds: T takes op +0x310 / +0x318 / +0x320 (record +0x378 feed, +0x380 plunge, +0x388
retract; `regen_disasm.txt` 0x1800188f3-0x180018901). **NOT DECODED storage**: "Do 3D Engraving moves" (1079, page +0xe64: only in
DDX, in neither transfer function), "rapid retract" (1075, page +0xd70: same), "Engrave Type" (1104 is a static).

### 2.3 Validation messages (engraveres)

mwEngraveParams::Validate FUN_18004e4d0 (`core5.c` l.655-745) throws code c -> engraveres string 168 + c: 8 metrics mismatch, 17
direction > 1, 18 sorting not 1..3, 19 rough type > 3, 20 roll >= 2, 21 depth-cut type > 1, 22..26 clearance / retract / feed /
top / depth mode, 27 "Plunge can be on only when roughing is on" (ramp on and rough off), 28 the Enter-On range (P+0x188 not
1..3; 168 + 28 is a translator header, the matching text is engraveres 262 "Invalid starting point parameter. Must be 1 ..." -
the code -> id map for 28 is NOT DECODED). Same table: 1 (169) "Some of the base contours are overlapping", 2 (170)
CreateProcess failed, 4 (172) write failed, 5 (173) read failed, 6 (174) coding error, 7 (175) external process error.
FUN_18004abf0 (`core6.c`): 13 (181) "This negative stock to leave value is not allowed, it invalidates one of the input tools"
(tool radius after the XY-stock adjustment <= 0), 14 "Conical part of tool is too short for the given cutting
depth" (when `(r_major - r_tip) / tan(angle) < depth` and not ignored), 15 depth <= 0 ("The current top of stock or depth values
are invalid"), 16 tip radius <= 1e-4 ("The tip diameter can not be zero"). FUN_18001bb30 (`heights.c`) shows dialog 1808 when
`angle != 0` and `(r_major - r_tip)/tan(angle) < depth`: choice 0 sets depth = that maximum (globals DAT_1806da109 = 1,
DAT_1806da110 = -max, inch / 25.4), 1 throws 14, 2 ignores (DAT_1806da108 = 1, P+0x6f).

## 3. Op record -> engine parameters

### 3.1 Heights and the engine tool table (FUN_18001bb30, `heights.c`)

`z0` = active plane origin z (`MastercamPlanes::GetActiveOrigin`), `zc` = chain z (FUN_180019800).
- top `Ztop` = (S+0x38 == 0 ? z0 : zc) + S+0x58;
- each of depth (S+0x60, mode S+0x30), feed plane (S+0x68, S+0x34), clearance (S+0x48, S+0x3c), retract (S+0x50, S+0x40):
  `mode == 0 ? z0 + value : Ztop + value` (CEngToolP +0x33c8 / +0x33d0 / +0x33d8 / +0x33e0);
- engine tool row 1: `+0x10 = Ztop - Zdepth` (cut depth, `LEintauchT`), `+0x18 = Zfeed - Ztop` (`LFlugHoehe`), both x25.4 in
  inch parts;
- tool geometry (CEngToolP +0x2c58 = the op's tool; +0x2c64 type, +0x2c68 diameter, +0x2c80 angle, +0x3058 tip diameter):
  types 12 / 21 (Chamfer Mill / Engrave Tool): tip radius = +0x3058 / 2, angle = +0x2c80, major radius = diameter / 2;
  every other type: tip radius = diameter / 2, and then type 13: angle +0x2c80, major radius +0x3058 / 2; type 17: angle
  +0x2c80, major radius = tan(angle) x +0x2ef0 + diameter / 2 (diameter / 2 when the angle is 90); otherwise, when S+0xd8
  (angle for flat cutter offset) is nonzero: angle = S+0xd8, major radius = 100 x diameter / 2 and row +0x5c = 0, else angle
  0 and major radius = diameter / 2 (radii x25.4 for inch);
- `+0x33e8 = (!S+0xc0 && S+0x70) || S+0x74`; `+0x33f0` = the rapid height: max(clearance, retract) when both on and clearance
  not "start/end only", else the one that is on;
- remachining (S+0x90): a second "BIGTOOL" row from S+0x78/0x80/0x88 (major dia, angle, tip dia); when `major_r / tip_r >= 2.5`
  a third row is inserted (3 tools), and without "finish after remachining" (S+0xe0) the first row is dropped.

### 3.2 mwEngraveParams P (FUN_18002e410) - what each field is fed from

From `regen_disasm.txt` 0x180018ae3-0x180018d96 (x64 argument order RCX, RDX, R8, R9, then [RSP+0x20..0xa8]):

| P+ | source | used as |
|---|---|---|
| 0x08 | IsMetric | 0 = inch: geometry / lengths x25.4 into the engine (FUN_180052250 l.105-111, FUN_180052120) |
| 0x58 | S+0x00 != 0 | `KGegenlauffraesen` = (P+0x58 == 1) |
| 0x60 | S+0x08 (overlap) | `KAbraeumueberlapp`; with XY stock to leave s > 0: `100 - ((100-ov)/100 * r * 100) / (r + s)`, r = tool +0x10 / 2 (FUN_18004aea0 `core6.c` l.307-330) |
| 0x68 | S+0x9c: 0 -> 1, 1 -> 2, 2 -> 3 | `KBlockweise` |
| 0x6c | S+0x10 | `KAbraeumZuerst` |
| 0x6d | S+0x13 | `Knachsortieren`, and CoreParams +0x30 = `Bahnoptimierung` (FUN_18002d970 l.1027) |
| 0x6e | S+0x11 | rough on (`AbraeumVerf` = `keine` when off) |
| 0x6f | DAT_1806da108 (depth warning "ignore") | skips error 14 |
| 0x70 | S+0x14 (4 -> 1) | `AbraeumVerf` 0 `zeilebi`, 1 `zeileuni`, 2 `Radius`, 3 `Durchm` |
| 0x78 | angle(S+0x18) | `Abraeumwinkel` |
| 0x88 / 0xa0 | engine tool lists | tools / remachining tool |
| 0x98 | S+0x90 != 0 | remachining |
| 0xb0 | S+0xe0 | finish after remachining (FUN_1800525b0 drops the first tool otherwise) |
| 0xb8.. | W = FUN_18002e880(on = S+0xa0 != 0, n = S+0xa8, type = (S+0xac == 1), ramp = S+0xa4 != 0, angle(S+0xb0), surface = S+0x20 != 0) | depth cuts, ramp, Spiegel/Grund |
| 0xe8 | S+0xd0 == 1 | roll: 1 Sharp -> `KAbrundungswinkel=20`, 0 None -> 150 |
| 0xf0.. | T = FUN_18002eb10 | heights (sec. 3.1), modes, feeds, XY stock to leave (S+0xc8, T+0x60), flat-cutter angle (S+0xd8, T+0x68), flags S+0x70 / S+0x74 / S+0xc0, op +0x141 (T+0x90, NOT DECODED meaning) |
| 0x188 | S+0x130: 0 -> 2, 1 -> 1, 2 -> 3 | `KStartpunkt` |
| 0x18c | 100 (constant) | `KParamNormGr` = size class from the contour box (FUN_180046f80: max(dx, dy) in mm: < 75 -> 1, < 150 -> 2, < 350 -> 3, else 4) |

### 3.3 EST_DAT.TXT (writer FUN_18004f870 `strrefs1.c` l.19-648, tools FUN_1800525b0 / FUN_180052380 / FUN_180050fd0)

Line format `key=value\n`; booleans via FUN_180051eb0 as `JA` / `Nein`; reals via FUN_180264940; lengths scaled by `f` = 25.4
(inch) or 1.0 (mm). The shipped `EST_DAT.TXT` in the data folder is exactly this layout.

| section / key | value |
|---|---|
| `[Datenaustausch]` `Quelle=` / `Ziel=` | upper-cased full paths of `engcont.lg1` / `engtoolp.lg1` in the data folder |
| `Grenze_x0/y0/x1/y1=` | f x CoreParams +0x10/+0x18/+0x20/+0x28 = 0, 0, 100, 100 (FUN_18002d970 l.1023-1026) -> 0 / 2540 in inch parts |
| `ZIEHWND=0` | constant |
| `[Parameter]` `Bahnoptimierung` | S+0x13 (smooth contours) |
| `BOMindestausd=` | 0.05 mm (written unscaled: 0.001969 in inch, 0.05 metric) |
| `BOMindStuetzp=4`, `BOBahnenSchl=JA`, `BOVektorAufl=10000`, `Kreisaufloesung=26`, `Splineaufloesung=10`, `DXFSkalFaktor=1` | constants (FUN_18002d970 l.1035-1040) |
| `KAbrundungswinkel=` | CoreParams +0xa0 == 0 (always, l.1046) -> roll: Sharp 20, None 150 |
| `KGegenlauffraesen` | conventional |
| `KAbraeumueberlapp=` | P+0x60 (overlap %) |
| `KDatencheck=JA`, `KStartInnen=JA`, `KZeilenVerbinden=JA`, `KAbrWegZusammen=Nein`, `KAbrZeilenRund=Nein` | constants (+0x70..+0x74) |
| `KBlockweise=`, `KAbraeumZuerst`, `Knachsortieren`, `KParamNormGr=`, `KStartpunkt=` | sec. 3.2 |
| `[Werkzeuge #1..#10]` per tool: `WerkzeugNr=` (1..), `LName=`, `LEintauchT=` f x depth, `LFlugHoehe=` f x fly height, `LFRadius=` f x tip radius, `LFWinkel=` angle (unscaled), `LFSchaft=` f x core tool +0x70, `LVorschub=`, `LEintVorschZ=`, `LSpindelDreh=`, `LWechselStat=` (+0x78..+0x90), `KorrekturVerf=` `2D`/`2D3D`/`3D`/`Ecke` = W.surface ? core tool +0x94 : 0, `SpantSchichtZ=` W.on ? n : 1, `SpantAufteil=` W.type, `SpantEintWi=` W.ramp ? angle : 90 | FUN_180050fd0 l.656-1040; core tool fields FUN_18002ddd0 (`core6.c`) |
| `[Berechnung]` `AbraeumVerf=`, `Abraeumwinkel=` | sec. 3.2 |
| `KonturenAuf=` | `Spiegel` when S+0x20 (on top), else `Grund` |
| `GravurTyp=` | `vertieft` (CoreParams +0x78 = 0, always) |
| `SpantZustellung=JA`, `SpantSpiegelt=0` | constants |
| `SpantAbrSchichtZ=`, `SpantAbrAufteil=` (0 when no remachining and CoreParams +0x110 = 0), `SpantAbrEintWi=` | from W |

**NOT DECODED**: the value of core tool +0x94 (calc type) - FUN_18004ee60 / FUN_18004ed10 build it from the Mastercam tool
(chamfer: flag 1 when the tool angle is 0), but the stack-argument order into FUN_18002ddd0 was not verified; the shipped sample
has `KorrekturVerf=2D3D` for a 15 deg tool. Also `LFSchaft` (+0x70) and `LWechselStat` sources.

## 4. Engine file formats

- `ENGCONT.LG1` (contours, written by FUN_18005a5c0 / FUN_18005a410 per contour, in mm): records `tag:u8, x:f32, y:f32, z:f32`;
  tag 0x0b starts a contour, 0x0a = next point. Sample in the data folder: a 76.2 mm square, `0b (-38.1, 38.1, 0)`, then 4 x `0a`,
  closed, clockwise.
- `ENGTOOLP.LG1` (toolpath): a tool header (tags 0x01, 0x05, 0x06, 0x07, 0x96..0x9d incl. the tool name `AUSSPITZEN` at 0x98 in
  the sample; **NOT DECODED**, parser FUN_18005ddf0) then motion records read by FUN_18005ccc0 (`lg1.c` l.183-405), values / f:
  0x0b move-to (new subpath), 0x0a line-to, 0x0c point, all `x, y, z:f32`; 0x0d arc `r, a, sweep:f32` (radians; the start is
  the current point, centre = current - r(cos a, sin a), end at angle a + sweep); any other tag -> error 12. Error 11 when f = 0.
- `FEHLER=n` in `EST_DAT.TXT` after the run: 0 ok, 53 -> exception 1 (engraveres 169 "Some of the base contours are
  overlapping"), else exception 7 with message 14000 + n through the
  `mwEngrave_ExternalProcessErrors` map (`core3.c` l.355-376; the texts are engraveres 196-231: HRKORRD offset errors, HSCHRAFF
  roughing errors, KONTBILD toolpath errors, "Roughing is not possible", "Clean corners strategy is not possible here").

## 5. The engine (EStamp7.exe)

Decoded and ported since: see **formats/engrave_engine.md** (this section is the earlier overview).


- EST_DAT parser: FUN_005229b0 (`estamp_main.c`, Delphi ShortString keys `\tGRENZE_Y0` .. `SPANTABREINTWI`, value strings
  `ZEILEBI ZEILEUNI RADIUS DURCHM KEINE`, `2D 2D3D 3D ECKE`, `SPIEGEL GRUND`, `ERHABEN VERTIEFT`) into globals around
  0x5369b0..0x536a01; FEHLER writer FUN_00522164; error texts FUN_0050c494 (HRKORRD / HSCHRAFF / KONTBILD).
- The algorithms are documented only by the vendor help (H4HELP.TXT, cp1252):
  - #2010 "AKorr2D/3D": 2D/3D toolpaths for up to 10 tools from closed, non-intersecting contours. `Korrekturverfahren`
    2D / 2D+3D: without / with "Ausspitzen" (sharpening): conical tools go around the contour in 3D, cylindrical (0 deg) in 2D;
    3D: only the 3D sharpening paths; Ecke: corner tool. `Abraeumverfahren`: contour only / line-wise (zeilenfoermig) / path-
    parallel with radius / diameter offset / none.
  - #2010 (cont.) `Spiegel/Grund`: Spiegel = contours describe the surface, offset with the resulting radius (tip + cone);
    Grund = contours describe the floor, offset with the tip radius. `erhaben/vertieft`: raised stamp / engraved.
  - #2004 "AKorr Parameter": `Abrundungswinkel 2D` for outside corners (>120 deg keeps sharp corners); `Gegenlauffraesen`
    JA = a surface is cut in the mathematically positive direction; `Ueberlapp. ZeilenAbr.` 0 % = line spacing of one cutter
    diameter; `Ueberlapp. optim. Abr.` 0 % = spacing of one radius; `Bahnoptimierung`.
  - #0083 "Spantiefenzustellung": depth layers (max 999; equal layers: total / count, e.g. 2.0 mm at 1.5 mm -> 2 x 1.0 mm),
    plunge angle 10..90 deg (90 vertical), ramp along the path.
  - #0645 BahnSort, #8800 BahnOp (point reduction), #0053 Abraeumen innen.
- To port the engine one must decompile EStamp7.exe's HRKORRD (offset / sharpening), HSCHRAFF (hatching), KONTBILD (path
  building) modules; their code was not located yet (entry: callers of FUN_005229b0's globals; error text table FUN_0050c494).

## 6. LG1 -> NCI (FUN_180041910, `core5.c` l.220-597) - partly decoded

Per motion record of each tool (a tool row named "BIGTOOL" is skipped when remachining is on, P+0x98; FUN_180055180 test):
x, y from the file; z by tag: 0x0c `-z`, 0x0a `-z` (or `-local_140` = the depth of the tool row from FUN_18004abf0, when z = 0),
0x0b `local_138 - z` (local_138 = a row value from FUN_18004abf0, NOT DECODED which); then `+ P+0x190` (`P+0x190` = top: absolute
T+0x48, or T+0x48 + chain z; FUN_18004aea0). Moves go through `mwEngraveToolpathImpl` (vtable +0x10 = Write3dMove
FUN_18001de90 -> FUN_18001e3c0) with feeds chosen by FUN_1800468e0 from T+0x78 / +0x80 / +0x88 (feed / plunge / retract) and the
rapid / retract levels P+0x198 (feed plane), P+0x1a0 (clearance), P+0x1a8 (retract), P+0x1b8 (FUN_18004aea0: the higher of
clearance / retract when both on, etc.). The exact link sequence (when G0 to retract vs feed plane, start-of-op clearance)
is **NOT DECODED** in detail; the coaster NCI shows it (sec. 7).

## 7. Measurement (corpus oracle)

Corpus scan (all 1460 .mcam/.mcx*/.emcam under <mastercam-samples>, opcode 416): 3 ops.
`python3 -I tools/engrave_dump.py PART [--moves] [--op N] [--prm-only]` prints S, chains, NCI passes.

| part / op | S summary | chains | NCI |
|---|---|---|---|
| X9 `engraved coaster.mcx-9` op 2 | on top (S+0x20 = 1), rough zigzag 0 deg, overlap 25 (stepover 75 %), finish first, smooth on, sorting L-R, roll Sharp, depth -0.135 abs, top 0 abs, feed plane 0.1 inc, retract 0.25 inc on, clearance off, filter on 0.0002 / look ahead 100 / arcs; tool 21 "1/4 INCH ENGRAVING TOOL 30 DEGREE X .010 TIP", feed 5 / plunge 25 | 6 closed NURBS chains (CW) | 2367 moves: G0 51, G1 2294, G2 14, G3 8; 6 finish passes 3D (z -0.135..0.000), then 11 rough passes planar at -0.135 |
| same, op 4 | as op 2 but at depth (S+0x20 = 0) | 3 chains | 213 moves: 3 planar finish passes at -0.135 with arcs, 3 zigzag rough passes |
| 2018 ATP `3D Door Machined.mcam` op 6 | on top, rough off, depth -0.140486 abs, feed plane 0.1 abs, retract 0.25 abs on, clearance 2 abs on, filter off | 1 chain (4 entities) | no NCI |

Measured rules (`engrave_dump.py PART --op N --offsets`: distance of every feed point at the deepest z to the chains sampled
400x per entity, and the most common y step of the G1 points):
- **Finish offset**, op 4 (Grund): every finish point at -0.135 lies 0.00493..0.00505 from the chain = tip radius 0.005
  (H4HELP #2010 "Grund ... Spitzenradius").
- **Finish offset**, op 2 (Spiegel): planar parts of the finish passes lie 0.04103..0.04180 from the chain;
  `r_tip + depth x tan(15 deg) = 0.005 + 0.135 x 0.26795 = 0.04117` (H4HELP #2010 "Spiegel ... resultierender Radius"); the passes
  climb to z = 0.000 (top of stock) in sharp corners ("Ausspitzen", 3D corner moves).
- **Rough stepover**: zigzag lines 0.0075 apart in both ops = tip diameter 0.010 x (100 - 25) / 100 (H4HELP #2004: 0 %
  overlap = one cutter diameter). Rough lines keep >= 0.0417 (op 2) / >= 0.0050 (op 4) from the chain.
- **Order**: finish before rough (S+0x10 = 0, `KAbraeumZuerst=Nein`); each pass: G0 to (x, y) at retract 0.25 (G0 at 0.1 first),
  G1 plunge at the plunge feed 25 to -0.135, cut at feed 5, G0 up to 0.25. Rapid z levels only 0.1 and 0.25 (clearance off).
- **Arcs** in the finish passes come from the Mastercam arc filter (record +0x33a on, 0.0002); formats/opdlg_port_audit.md
  sec. 14.1 already reproduces both coaster ops' arc filtering from their NCI (op 4 exact).

## 8. Port plan

1. **Op data** (DECODED): read/write S at record +0x12f0 (+0x12f8 in 10560-byte records), the C-hook header at +0x1218, defaults
   from the four `.mcam-defaults`; new op = defaults record copy. Dialog pages 1802 / 1803 / sub-dialogs from engraveres with the
   control <-> S table (sec. 2.2) and enable rules (FUN_18000e030). Leave 1079 / 1075 unbound (NOT DECODED storage).
2. **Driver** (DECODED): heights (sec. 3.1), validation messages (2.3), depth warning (1808), engine parameter derivation
   (3.2 / 3.3) - testable by writing EST_DAT.TXT and comparing with the shipped sample layout.
3. **Engine**: decoded and ported (formats/engrave_engine.md, viewer/engrave.c, oracle tools/tests/engrave_test.sh). Before that: Until then the oracle only fixes:
   Grund finish = offset by tip radius at depth; Spiegel finish = offset by `r_tip + depth tan(a)` at depth plus 3D corner
   climbs to the top; zigzag rough at `tip_dia x (100 - overlap)/100` spacing, angle S+0x18. Per the no-improvisation rule
   these must not be filled with our own geometry; the rough/corner algorithms stay unported.
4. **NCI** (PARTLY): z mapping per LG1 tag, feeds, retract levels; then the existing arc filter (`viewer/cam_filter.c`,
   `cam_filter_nodes`) with the op's filter fields.
5. Wrap (2 curves / surfaces) and remachining: parameters only.
6. **App** (2026-10-26, DONE for lines / arcs): `viewer/cam2d_engrave.c` (record = stored op or defaults library op + the dialog's
   record patches; chains = cam_chain_polyline at S+0x28 / 10, closed only, first chain's z; engrave_generate; then cam_arc_filter),
   op dialog pages 1802 / 1803 (cui_opdlg.c; stepover % shown as 100 - S+0x08), test scene `gen_engrave_depth`
   (tools/tests/op_scenes.txt).

### Undecoded callees (engrave.dll unless noted)

| address | role |
|---|---|
| FUN_18005ddf0 | ENGTOOLP.LG1 tool-header parser (tags 0x01, 0x05..0x07, 0x96..0x9d) |
| FUN_18004ee60 / FUN_18004ed10 -> FUN_18002ddd0 | engine tool rows: calc type (+0x94, `KorrekturVerf`), shank (+0x70) |
| FUN_180046920 | feed choice per move (via FUN_1800468e0) |
| FUN_18001e3c0 / FUN_180200ac0 | Write3dMove / end move into Mastercam NCI |
| FUN_18001e400 | contour list build from the op's chains (P+0x10) |
| FUN_1800173f0 | per-geometry-set handling (4 sets: chains, wrap curves / points) |
| FUN_18001ca20 / FUN_180017b80 | wrap geometry selection |
| FUN_1800569c0 | tool list for FUN_1800525b0 |
| EStamp7.exe FUN_005229b0 globals -> HRKORRD / HSCHRAFF / KONTBILD code | the engine |
