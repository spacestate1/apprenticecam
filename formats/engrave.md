# Engrave (Mastercam 2017 opcode 416) - decode for a port

Status 2026-11-19 (branch engrave-dll: sec. 2.1 hidden controls, 2.2 every S field, 2.3 MWMSGMAP, 2.4 sub-dialogs, 3.1 / 3.3 XY stock,
depth warning, KorrekturVerf, 3.4 remachining, 4 LG1 tool header, 5a wrap; full engrave.dll dumps in `decomp/engrave/full/`). Sources: `engrave.dll` (Ghidra project `~/mastercam-re/ghidra_engrave`, decompiles in
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
2026-11-19: `full/dll_0001_0062.c` (every function 0x180001000-0x180062000, cited "dll l.N"), `full/dll_0200_0262.c` (the
C-hook / wrap support code 0x180200000-0x180262000, "dll2 l.N"), `full/dll_01f0_0200.c` (dialog base classes, the wrap dialog),
`full/dll_0262_0266.c` (number formatting), `full/dis_tools.txt` (disassembly of FUN_18004ed10 / FUN_18004ee60), `full/vt_lg1.txt`
(vtables); made with tools/ghidra_scripts DumpRange.java / DisasmAt.java / VtableDump.java on `~/mastercam-re/ghidra_engrave`.
Resources: the control styles (WS_VISIBLE / WS_DISABLED) read from engraveres RT_DIALOG (pe_dialogs.py's parser, style printed),
the custom resource `MWMSGMAP` of engraveres (exception code -> string id, sec. 2.3).

## 0. Summary

| part | state |
|---|---|
| Registration, op creation, chain rules | DECODED (sec. 1) |
| Dialog pages, every control <-> op record field, defaults | DECODED (sec. 2): 1075 "rapid retract", 1079 "Do 3D Engraving moves", 1036, 1048, 1104 are hidden controls (no WS_VISIBLE in the template, never shown): 1075 / 1079 have no storage, 1036 / 1048 are stored only (2.1) |
| Push-button sub-dialogs 1804 / 1805 / 1806 / 1807 / 10502, depth warning 1808 | DECODED (2.4) and PORTED (cui_opdlg.c pf_sub_*, opdlg.cpp runSub / askDepthWarning) |
| Op record -> engine parameters (EST_DAT.TXT keys, units, derived values) | DECODED (sec. 3) incl. `KorrekturVerf` (core tool +0x94), `LFSchaft` (= the major radius), `LVorschub` .. `LWechselStat` (CoreTool defaults), XY stock to leave (3.1); PORTED |
| Engine I/O file formats (EST_DAT.TXT, LG1 contours / toolpath, FEHLER) | DECODED for what engrave.dll reads/writes, incl. the LG1 tool header (156 bytes, the name at 0x50) (sec. 4) |
| The geometry engine (offset with V-tool "Ausspitzen" corners, roughing hatches, ordering) | DECODED for the paths the corpus uses (2D / 2D3D finishing, zigzag / one-way roughing, blocks, re-sort, depth cuts, LG1 output) and ported: **formats/engrave_engine.md**, `viewer/engrave.c`; the rest refused there |
| LG1 toolpath -> NCI (z mapping, retracts, feeds, arc filter) | DECODED (FUN_180041910, formats/engrave_engine.md 8) |
| Wrap (2 curves / surfaces), remachining | parameters, dialogs, engrave.dll side DECODED (3.4, 5a); remachining's multi-tool engine run and the wrap transformation (engrave.dll 0x180203f50.. / 0x18020f0d0.., 4 / 5-axis output) NOT DECODED -> refused |
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
- The op is OPCODE 416 (formats/opcodes.tsv; mccoreres 277 "Engrave"). Record +0x1218 f64 = the operation's version (op +0x11b0,
  every op type: InitOperation sets 20.0, decomp/mccore_ops.c l.222; 19.1 in the X9 coaster, 20 in the defaults, 21 in the 2018 part),
  +0x1220 i32 = 0 in all 7 records (not read by engrave.dll), +0x1224 i32 = the op's axis combination id (op +0x11bc,
  `CCNCMachine::AxisCombinationFromID`, mastercam_opsettomachine.c l.173, mccore_ops.c l.686-694; formats/mp_engine.md 950; 51 / 420 /
  22 / 22 / 460 / 733 in coaster / door / MI / MM / RI / RM). The C-hook header starts at +0x1228: `"Engraving Operation"` (engraveres
  19, default op name), the DLL name `"ENGRAVE.DLL"` at +0x1264, the four names `YourPrmFunction` / `YourToolFunction` /
  `YourGeomFunction` / `YourRegenFunction` at +0x128c.. ; then the Engrave block S (sec. 2.2).
- Record +0xd38..+0xfe5: an older copy of the same layout in every record (corpus and defaults): f64 10.14 (a V9-era op version) at
  +0xd38, i32 0, i32 an axis combination id (22 / 330 / 733: not the one at +0x1224), the header with a lowercase `engrave.dll` from
  +0xd48, and a copy of S at +0xe10 ('ENGR' at +0xf38) that differs from S in a few fields (coaster: S+0x0e / +0x11 / +0x40 / +0x60..;
  RI: identical). No code of engrave.dll 0x180001000-0x180062000 / 0x180200000-0x180262000 addresses it (no CEngToolP / op offset
  in that range is read); kept byte-identical as part of the record (the app copies the stored / defaults record). Its writer (an
  old-version conversion, V9ToVxChookOperation?) is NOT DECODED.
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

Hidden / disabled controls (engraveres RT_DIALOG styles): 1802 1075 "rapid retract" (0x40030003), 1104 "Engrave Type" (0x40020000),
1803 1079 "Do 3D Engraving moves" (0x40010003), 1036 "Try to keep tool down" (0x40030003), 1048 "Start at corners" (0x40010003) have
no WS_VISIBLE and engrave.dll never shows them (no ShowWindow of these ids in dll 0x180001000-0x180062000): Mastercam 2017 does not show
them. 1075 (page +0xd70, ctor FUN_180008590 sets 0) and 1079 (page +0xe64) are in the DDX only, in neither transfer function and nowhere
else (dll l.5534 / 8716 are their only references): no storage. 1149 / 1148 (Wrapping) carry WS_DISABLED (0x58010003 / 0x58010000) in
the template but FUN_18000ab60 (dll l.6531-6575, run at init and by the check boxes) enables 1149 always and 1148 with it; the same
function enables 1085 Depth cuts... with +0xda4, 1062 Filter... with +0xd98, 1064 Remachining... with +0xda0, 1087 always, 1119 / 1146
(flat cutter angle) unless the tool type is 12 / 13 / 17 / 21 (mask 0x223000), and the clearance / retract groups with +0xd90 / +0xd94.
The real "Rapid retract" of an Engrave op is op +0x141 (record 0x1a9), the Toolpath parameters page's button 3305 (sec. 3.2 P+0x180).

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
| 0x12 | u8 | 1036 (+0xe5c, hidden) | Try to keep tool down: stored only - read by FUN_18001a690 (S -> page, dll l.18501) and nothing else (no 0x3412 / S+0x12 read in dll 0x180001000-0x180062000); C 1 | 0 | 0 | 0 | 0 | 0 |
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
| 0x98 | i32 | (1803 +0xe70, no control in DDX) | stored only: S <-> page +0xe70 (dll l.18416 / 18496), no other reader; C 1 (dll l.13087) | 0 | 0 | 0 | 0 | 0 |
| 0x9c | i32 | 1082 (+0xe74) | sorting 0 Selection order, 1 Top to bottom, 2 Left to right | 2 | 2 | 2 | 2 | 2 |
| 0xa0 | i32 | [x]1087 (+0xda4) | depth cuts on | 0 | 0 | 0 | 0 | 0 |
| 0xa4 | i32 | [x]1083 (+0xe7c) | entry ramp on (stored only when Rough is on, `pagexfer.c` l.50-53) | 0 | 0 | 0 | 0 | 0 |
| 0xa8 | i32 | 1086 (+0xdfc) | # of depth cuts (C 3; >100000 -> 1) | 2 | 0 | 2 | 0 | 2 |
| 0xac | i32 | 1089/1090 (+0xe00) | radio index (0 equal, 1 constant volume; >10 -> 1) | 0 | 0 | 0 | 0 | 0 |
| 0xb0 | f64 | 1151 (1803 +0xd50) | plunge angle (deg; outside 10..90 -> 45) | 45 | 45 | 45 | 45 | 45 |
| 0xb8 | i32 | 1048 (+0xe78, hidden) | Start at corners: stored only (dll l.18432 / 18511), no other reader, no EST_DAT key; C 1 | 0 | 0 | 0 | 0 | 0 |
| 0xbc | i32 | 1105/1106 (+0xdd8) | remachining stock from: 0 previous operation, 1 roughing tool | 0 | 0 | 0 | 0 | 0 |
| 0xc0 | i32 | 1078 (+0xd9c) | use clearance only at start and end | 0 | 0 | 0 | 0 | 0 |
| 0xc8 | f64 | 1118 (+0xda8) | XY stock to leave | 0 | 1 | 0 | 1 | 0 |
| 0xd0 | i32 | combo 1143 (+0xdb0) | roll: stored `2 - (sel != 0)`: 2 = None, 1 = Sharp (>2 -> 1) | 1 | 1 | 1 | 1 | 1 |
| 0xd8 | f64 | 1119 (+0xdb8) | angle for flat cutter offset (deg; outside (0, 90] -> 0) | 0 | 1 | 0 | 1 | 0 |
| 0xe0 | u8 | 1144 (+0xdf8) | finish after remachining | 0 | 0 | 0 | 0 | 0 |
| 0xe4 | i32 | [x]1149 (+0xdc0) | wrapping on (== 1) | 0 | 0 | 0 | 0 | 0 |
| 0xe8 | i32 | 10505/10506 (+0xe04) | wrap: 0 between 2 curves, 1 on surfaces | 0 | 0 | 0 | 0 | 0 |
| 0xf0 | f64 | 10508 (+0xe08) | wrap grid distance (<=1e-12 or >1e6 -> 0.001 in / 0.02 mm) | 0.001 | 0.02 | 0.001 | 0.02 | 0.001 |
| 0xf8 / 0x100 / 0x108 | p_3d (3 f64) | Geo manager point 1 | the wrap point picked by the Geo manager's case 4 (FUN_18001cc00 dll l.20071-20088: draw_3d_point, FUN_180203e80 pick, kept unless moved) - copied to CEngToolP +0x37d8.. by FUN_18001af20 and back by FUN_18001eca0 (the decompile splits it into 4 ints + 1 double) | 0 | 0 | 0 | 0 | 0 |
| 0x110 / 0x118 / 0x120 | p_3d | Geo manager point 2 | case 7 (l.20097-20113, FUN_180203dd0) -> +0x3808.. | 0 | 0 | 0 | 0 | 0 |
| 0x128 | 4s | - | magic `ENGR` | | | | | |
| 0x12c | u8 | - | 1 = block has the Enter-On field | 1 | 1 | 1 | 1 | 1 |
| 0x130 | i32 | 1062/1064/1067 (+0xe80) | Enter On radio index: 0 interior corners, 1 chain start point, 2 midpoint longest line | 0 | 0 | 0 | 0 | 0 |
| 0x134..0x1d5 | | - | never addressed: no CEngToolP +0x3534..+0x35d5 / op +0x13bc.. access in dll 0x180001000-0x180062000; copied whole by FUN_18001c3f0 / FUN_1800174f0; 0 in all 7 records - kept as stored | 0 | 0 | 0 | 0 | 0 |

Not in S: filter (1088 / dialog 1806) uses the standard operation filter fields: on = op +0x2d2 (record +0x33a), create arcs =
op +0x2d3 (+0x33b), look ahead = op +0x2d0 i16 (+0x338), tolerances op +0x2b8 / +0x2c0 / +0x2c8 f64 (record +0x320 / +0x328 /
+0x330) (`pagexfer.c` l.55-60, l.142-148). The page keeps them as floats (+0xdc8 / +0xdcc / +0xdd0) and both transfer functions run
at every op load (FUN_18001a690 ends with FUN_18001a3e0): the op holds `(double)(float)` values - measured: 0.00019999999494757503 for
0.0002 in the coaster and MI, 0.009999999776482582 in MM. Feeds: T takes op +0x310 / +0x318 / +0x320 (record +0x378 feed, +0x380 plunge, +0x388
retract; `regen_disasm.txt` 0x1800188f3-0x180018901). "Do 3D Engraving moves" (1079, page +0xe64) and "rapid retract" (1075, page +0xd70): hidden controls without storage (2.1); "Engrave
Type" (1104) is a hidden static.

### 2.3 Validation messages (engraveres)

mwEngraveParams::Validate FUN_18004e4d0 (`core5.c` l.655-745) throws code c -> engraveres string 168 + c: 8 metrics mismatch, 17
direction > 1, 18 sorting not 1..3, 19 rough type > 3, 20 roll >= 2, 21 depth-cut type > 1, 22..26 clearance / retract / feed /
top / depth mode, 27 "Plunge can be on only when roughing is on" (ramp on and rough off), 28 the Enter-On range (P+0x188 not
1..3) -> engraveres 262 "Invalid starting point parameter. Must be 1 (START_IN_CHAIN_START_POINT), 2 or 3". The code -> string map is
the custom resource `MWMSGMAP` of engraveres (one line per string id: `#<string id>#<domain>#<code>#`): `#169#mwEngrave_Exception#1#`
.. `#195#mwEngrave_Exception#27#`, `#262#mwEngrave_Exception#28#`, `#196#mwEngrave_ExternalProcessErrors#14000#` .., and the
MODULEWORKS message ids engrave.dll raises directly: 10807 -> 10 ("Chains on different heights..."), 10809 -> 11 ("Previous operation
for remachining is not found"), 10811 -> 13, 10812 -> 14 ("The current top of stock or depth values are invalid", heights 0x2a3c),
10813 -> 15 ("The tip diameter can not be zero", 0x2a3d). Same table: 1 (169) "Some of the base contours are overlapping", 2 (170)
CreateProcess failed, 4 (172) write failed, 5 (173) read failed, 6 (174) coding error, 7 (175) external process error.
FUN_18004abf0 (`core6.c`): 13 (181) "This negative stock to leave value is not allowed, it invalidates one of the input tools"
(tool radius after the XY-stock adjustment <= 0), 14 "Conical part of tool is too short for the given cutting
depth" (when `(r_major - r_tip) / tan(angle) < depth` and not ignored), 15 depth <= 0 ("The current top of stock or depth values
are invalid"), 16 tip radius <= 1e-4 ("The tip diameter can not be zero"). FUN_18001bb30 (`heights.c`) shows dialog 1808 when
`angle != 0` and `(r_major - r_tip)/tan(angle) < depth`: choice 0 sets depth = that maximum (globals DAT_1806da109 = 1,
DAT_1806da110 = -max, inch / 25.4), 1 throws 14, 2 ignores (DAT_1806da108 = 1, P+0x6f).

### 2.4 The push-button sub-dialogs and the depth warning (decoded 2026-11-19; ported: cui_opdlg.c pf_sub_*, opdlg.cpp)

Each button's handler builds the dialog, copies the page members in, `DoModal`, and copies them back unless the result is IDCANCEL (2)
(the page message maps at 0x1804d1a90.. / 0x1804d2e40):

| button (page) | handler | dialog (class, ctor) | DDX (dialog member <- page member) | DDV / enable | stored as |
|---|---|---|---|---|---|
| 1085 Depth cuts... (1802; enabled with 1087) | FUN_180009ea0 | 1804 CDepthCutsDlg FUN_18000cdf0 | FUN_18000cfe0: 1086 int +0x1d90 <- +0xdfc, radio 1089 +0x1d94 <- +0xe00 | none | S+0xa8, S+0xac |
| 1060 Entry-ramp... (1803; with 1083 and Rough) | FUN_18000df40 | 1805 CRampDlg FUN_1800246a0 | FUN_180024760: 1151 double +0x1d90 <- +0xd50 | DDV_MinMaxDouble 10 .. 90 | S+0xb0 |
| 1062 Filter... (1802; with 1088) | FUN_18000a140 | 1806 CFilterDlg FUN_180021d30 | FUN_180021de0: 1094 short +0x1d90 <- +0xdd4, 1096 float +0x1d94 <- +0xdd0, 1095 float +0x1d98 <- +0xdcc, 1049 float +0x1d9c <- +0xdc8, check 1093 +0x1da0 <- +0xdc4 | 1095 / 1096 enabled with 1093 (FUN_180021f00 at init; 1093's click 0x180021ea0 toggles +0x1da0 and runs it) | op +0x2d0 / +0x2c8 / +0x2c0 / +0x2b8 / +0x2d3 = record 0x338 / 0x330 / 0x328 / 0x320 / 0x33b, the reals as `(double)(float)` |
| 1064 Remachining... (1802; with 1102) | FUN_18000a550 | 1807 CRemachDlg FUN_180024820 | FUN_1800248e0: radio 1105 +0x1d90 <- +0xdd8, 1058 +0x1d98 <- +0xde0, 1072 +0x1da0 <- +0xde8, 1076 +0x1da8 <- +0xdf0, check 1144 +0x1db0 <- +0xdf8 (back: `== 1`) | 1056 / 1070 / 1074 / 1058 / 1072 / 1076 / 1108 enabled iff "Roughing tool" (FUN_180024a00 at init; the radio clicks 0x1800249a0 (0) / 0x1800249b0 (1) set +0x1d90 and run it) | S+0xbc, S+0x78 / +0x80 / +0x88, S+0xe0 |
| 1148 Wrapping... (1802; with 1149) | FUN_18000a980 | 10502, ctor FUN_1801fe960 (dialog base 0x1801ff220) | FUN_1801fea10: 10508 double +0x1d90 <- +0xe08, radio 10505 +0x1d98 <- +0xe04 | DDV_MinMaxDouble 1e-6 .. 99999 | S+0xf0, S+0xe8 |

A DDV failure shows MFC's AFX_IDP_PARSE_REAL_RANGE ("Please enter a number between %1 and %2.", bounds "%.*g") and keeps the dialog open.
Help (9) in each: not ported (greyed). The texts / positions are the templates' (pe_dialogs.py; rows of cui_opfields.inc dialogs 1804 ..
10502). Port: the dialog's edits are record edits like a page's; Cancel restores them (pf_sub_cancel); scripted runs drive them with
`pfset:ID=V` (the button opens, `pfset:1=1` OK, `pfset:2=1` Cancel); scenes `gen_engrave_sub` / `gen_engrave_dwarn` (op_scenes.txt).

**Depth warning 1808** (mwConicalWarningDlg, ctor FUN_180021f90, DDX FUN_180022070): shown by heights FUN_18001bb30 (heights.c l.163-201)
when the tool row's angle != 0 and `(major_r - tip_r) / tan(angle) < depth` (radii x25.4 for an inch tool, depth x25.4 for an inch part).
The text 1165 is "The maximum depth of cut for this tool is %s" with the float `(major_r - tip_r)/tan(angle)` in the tool's units printed
`%.4f` (FUN_1802647a0 -> `%%.%df` with 4); the radio 1166 / 1167 / 1168 member +0x1d90 is set to 0 before DoModal. Choice 0 (use the
maximum): row depth = max, `DAT_1806da109 = 1`, `DAT_1806da110 = -max` (/25.4 for an inch part) - the regen then takes the depth value
as `top + DAT_1806da110` (absolute) or `DAT_1806da110` (incremental) (dll l.17262-17275 / regen_disasm.txt 0x180018797-0x1800187e6):
the toolpath's depth is the maximum; choice 1: exception 14 (engraveres 182 "Conical part of tool is too short for the given cutting
depth"); choice 2: `DAT_1806da108 = 1` -> P+0x6f, FUN_18004abf0 skips error 14. Closed without OK (DoModal != IDOK): heights returns at
once (skipping the rest, incl. the remachining rows and the sanitising of S+0xb0 / +0xac / +0xa8 / +0x9c) with `DAT_1806da108 = 0`, so
FUN_18004abf0 throws 14 = as choice 1. The three globals are reset only when the dialog shows: after "use the maximum" every later
regeneration (of any op, while the dialog does not show) still takes `DAT_1806da110` as its depth (a Mastercam session state; NOT
ported - question E-DW2 in sec. 9). Port: engrave_setup (in->dwarn / CamParams.eg_dwarn), asked by the Qt OK (OpDialog::askDepthWarning)
between gen_begin and the computation; scripted runs answer with `egdwarn:N`; a regeneration without the dialog (Regenerate dirty /
regen-all) re-uses the op's last answer (Mastercam asks again: question E-DW1).

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
- these rows feed the depth warning (2.4) and the remachining rows only. The engine tool is built in the regen (dll l.16990-17210):
  types 12 / 21 -> `mwEngraveChamferMill(tip dia = 2 x row tip r, angle = row angle, major dia = 2 x row major r)`, every other type
  -> `mwEngraveEndMill(dia = 2 x row tip r)` (row values / 25.4 again for an inch part, FUN_18001c8c0 l.19789); the core tool
  (mwEngraveCoreTool FUN_18002ddd0, sec. 3.3) of an end mill takes the angle from S+0xd8 (T+0x68), not from the tool: type 13's / 17's
  own angle and major radius only reach the depth warning. Port: type 13 as decoded; type 17 refused (its row uses tool +0x2ef0);
- XY stock to leave s = T+0x60 = S+0xc8 (raw, part units; < -1e10 -> 0 in the T ctor FUN_18002eb10): the CoreParams ctor FUN_18002d970
  (dll l.32508-32580) replaces its first tool by FUN_18004abf0's copy (FUN_18004ab60): when |s| > 1e-12, tip radius (+0x58) += s and
  major radius (+0x70) += s (FUN_1800376a0); either <= 1e-12 -> error 13 (engraveres 181 "This negative stock to leave value is not
  allowed, it invalidates one of the input tools"). Then (FUN_18004abf0 l.52362-52398, with these radii) error 14 when the angle is not 0,
  not ignored and `(major - tip)/tan(angle) < depth - 1e-12`, error 15 when depth <= 1e-12, error 16 when the tip radius <= 0.000100000001
  (part units). The P ctor's FUN_18004aea0 (l.52413-52427) changes the overlap with the same s: `100 - ((100 - ov)/100 * r * 100) / (r + s)`,
  r = the mwEngraveTool's +0x10 / 2 (end mill: diameter, chamfer: tip diameter), before the stock. Ported (engrave_setup);
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
| 0xf0.. | T = FUN_18002eb10 | heights (sec. 3.1), modes, feeds, XY stock to leave (S+0xc8, T+0x60), flat-cutter angle (S+0xd8, T+0x68), flags S+0x70 / S+0x74 / S+0xc0, op +0x141 (T+0x90) = "Rapid Retract" of the Toolpath parameters page (button 3305, record 0x1a9; formats/mcamui_dialogs.md l.91): sec. 6 / engrave_engine.md 8 |
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

The engine tool = mwEngraveCoreTool (FUN_18002ddd0: +0x8 name, +0x48 depth, +0x50 fly, +0x58 tip radius, +0x60 angle, +0x70 major
radius, +0x78 / +0x80 / +0x88 feed / plunge / spindle, +0x90 change station, +0x94 calc type, +0x98 metric), made by the visitor
mwEngraveCoreToolCreator (vtable 0x1804e0020: slot 1 FUN_18004ed10 for mwEngraveChamferMill, slot 2 FUN_18004ee60 for mwEngraveEndMill;
the stack-argument order read from the disassembly, `decomp/engrave/full/dis_tools.txt`) over the defaults of FUN_18002dcb0 (name
"ausspitzen", +0x48 1.0 mm / 0.05 in, +0x50 1.0 / 0.05, +0x58 0.1 / 0.005, angle 25, +0x70 2.0 / 0.1, +0x78 1.0, +0x80 0.8, +0x88 30000,
+0x90 1, +0x94 1):
- chamfer mill: tip radius = tip dia / 2, angle, major radius = major dia / 2; +0x78..+0x94 the defaults -> **calc type 1**;
- end mill: tip radius = dia / 2; |S+0xd8| < 1e-12 -> angle 0, major radius = tip radius, **calc type 1**; else angle S+0xd8, major
  radius = 100 x tip radius, **calc type 0** (0x18004eecb-0x18004ef1d);
- depth / fly are set from T later (FUN_18004abf0, sec. 3.1).
So `KorrekturVerf=` (FUN_180050fd0 dll l.57005-57040: 0 `2D`, 1 `2D3D`, 2 `3D`, 3 `Ecke`, else exception 6 "Calc Type Unknown") is `2D`
at depth and, on top, `2D3D` except for an end mill with a flat-cutter angle (`2D`). `LFSchaft=` = f x the major radius (+0x70; the
shipped sample: 1.5875 = 1/16 in); `LVorschub=1`, `LEintVorschZ=0.8`, `LSpindelDreh=30000`, `LWechselStat=1` always (the defaults;
the shipped EST_DAT.TXT has exactly these): the op's feeds reach only the moves (sec. 6). Ported (engrave_setup).

### 3.4 Remachining (engrave.dll side decoded; the engine run NOT DECODED)

- Rows (heights.c l.218-260): with S+0x90 on and S+0xbc = 0 ("Previous operation") FUN_18001abd0 (dll l.18662-18749) takes the tool of
  the previous operation (FUN_180219960; none -> message 0x2a39 = engraveres 11 "Previous operation for remachining is not found", the
  regen goes on) into S+0x78 / +0x80 / +0x88 (major dia, angle, tip dia; by its type as the heights rows). The "BIGTOOL" row (major
  r = S+0x78 / 2, angle S+0x80, tip r = S+0x88 / 2, +0x40 1.0, +0x48 0.8, +0x50 30000, +0x58 = 2, +0x5c = 1) is the last row: two rows
  [tool, BIGTOOL], or three [tool, tool copy, BIGTOOL] when the op tool's tip r = 0 or BIGTOOL tip r / tool tip r >= 2.5; without
  "Finish after remachining" (S+0xe0) a three-row list drops its first row.
- The EST_DAT tool list (FUN_1800525b0, dll l.57598-57725; CoreParams +0xa8 = the op tool T with the XY stock, +0x148 = the
  remachining tool R = mwEngraveChamferMill of the BIGTOOL row, regen dll l.17195-17210: tip r = S+0x88 / 2, angle S+0x80, major r =
  S+0x78 / 2, no stock): T three times; T tip r == 0 or `R tip r / T tip r >= 2.499999999999` -> entry 1 rebuilt as T with calc type
  3 (`Ecke` on top, `2D` at depth), 3 entries; else the last copy dropped, 2 entries. The last entry becomes "BIGTOOL" (FUN_18002ddd0
  with T's depth / fly, R's tip radius / angle / major radius, +0x78 1.0, +0x80 0.8, +0x88 30000, +0x90 2 = LWechselStat, +0x94 1).
  Without "Finish after remachining" (P+0xb0 = S+0xe0) a 3-entry list drops entry 0. A size mismatch throws "noTools !=
  lTools.size()". So: [T, BIGTOOL], or [T, T-Ecke, BIGTOOL] / [T-Ecke, BIGTOOL]. LName of every non-BIGTOOL tool = "ausspitzen"
  (the CoreTool default, FUN_18002dcb0). PORTED (engrave_setup).
- Reading back (FUN_180041910): every tool block named "BIGTOOL" (the LG1 header name, sec. 4) is skipped when remachining - in the
  engine's output that is the BIGTOOL slot and also the roughing slot 10, whose header names the last row's tool (FUN_00517c20 /
  eg_tool_header). PORTED (engrave_generate names the blocks by slot, eg_lg1_to_moves skips them).
- "Previous operation" (S+0xbc = 0): FUN_18001abd0 above; FUN_180219960's choice of the operation is NOT DECODED -> refused.
- The engine (EStamp7) with 2 / 3 tools: "rest machining" with tools in ascending radius order (FUN_00529b50's check, engraveres 230
  "Tool tip radiuses or angles must be in ascending order"); the port's engine (viewer/engrave.c, the engine stream) loops over the slots,
  verified under Wine (below); before that it was gated by MCAM_ENGRAVE_REMACH=1 (2026-11-19: runs for the 2-tool lists and for 3 tools at depth; 3 tools on top stop in the engine port at
  the corner tool 'E', FUN_00528a1c / FUN_00528b10 NOT PORTED). Next: the engrave-core Wine oracle (EG-2, real estamp7.exe) over a case
  set (2 / 3 tools x on top / at depth x finish after on / off x roughing types), bit for bit on ENGTOOLP.LG1, then lift the gate;
  the corner tool's engine code goes into a new file (coordinated in formats/engrave_engine.md).
  **Wine check 2026-11-19** (tools/tests/engrave_remach.sh, real estamp7.exe, EG-2; 28 of 30 cases ran - the last 2 hit the
  7000 s limit): shapes e1 (star + notch) and the text "Rg8" in DejaVu Sans / Liberation Sans; Engrave Tool dia 3.175, 15 deg, tip r
  0.1; BIGTOOL tip r 0.2 / 0.249 (2 tools) and 0.25 / 0.4 (3 tools, the 2.5 threshold), finish after remachining off / on, at
  depth; 2 tools on top. Bit-identical: **0 / 28** in every class (2 tools at depth 0/12, 3 tools at depth 0/12, 2 tools on top 0/4).
  The one remachining-specific difference found - estamp7 writes the tool names upper-cased (its EST_DAT parser upper-cases each
  line, FUN_00497e08) - is fixed in the engine's name copy. What remains is the engine port's general difference, the same in
  engrave-core's single-tool cases (e1_Z0, e1_sharp_2d_K, f0/f1_Rg8_Z / grund_flat_Z: header box 0.0003, coordinates up to 0.013 on
  e1, record counts +3..+31 on the text): with e1 the remachining runs have exactly estamp7's record counts and tag sequence (all 10
  e1 cases), i.e. the 2 / 3-tool list and the BIGTOOL block are what estamp7 makes.
  **2026-11-20, branch engrave-remach (master with engrave-core f3fa7e5: corner tool, roughing C / D, path optimisation):
  36 / 36 bit-identical** - 2 tools at depth 12/12, 3 tools at depth 12/12, 2 tools on top 6/6, 3 tools on top 6/6 (3 with output;
  the 3 at the exact 2.5 ratio (BIGTOOL tip r 0.25 = 2.5 x 0.1) fail in estamp7 with FEHLER 906 and in the port with the same code:
  the corner-tool row check 0x529d03-0x529d1e compares the x87 product f32 rt x 2.5 unrounded - the port rounded it to f32, fixed).
  The MCAM_ENGRAVE_REMACH gate is removed; FEHLER codes now give Mastercam's texts (FUN_180049c30: 53 -> engraveres 169, else
  "External process error:" + MWMSGMAP 14000 + n, e.g. 906 "Clean corners strategy is not possible here."). Still refused:
  "Previous operation" (FUN_180219960 NOT DECODED).

## 4. Engine file formats

- `ENGCONT.LG1` (contours, written by FUN_18005a5c0 / FUN_18005a410 per contour, in mm): records `tag:u8, x:f32, y:f32, z:f32`;
  tag 0x0b starts a contour, 0x0a = next point. Sample in the data folder: a 76.2 mm square, `0b (-38.1, 38.1, 0)`, then 4 x `0a`,
  closed, clockwise.
- `ENGTOOLP.LG1` (toolpath): per tool a header then motion records. The reader mwLG1FileReader (ctor FUN_18005ddf0: `_wfopen_s(path,
  "rb")`, flag +0x30 = its 3rd argument; vtable 0x1804e3ba0) reads the header in slot 3 FUN_18005e350 -> FUN_18005e3a0 (dll l.65473-65511):
  with the flag set, 0x9c (156) bytes one by one (FUN_18005e270, fread 1), of which bytes 0x50..0x9b (76) are kept as the tool name (a C
  string; "BIGTOOL" for the remachining tool, "AUSSPITZEN" in the shipped sample); bytes 0..0x4f are skipped (their content is the engine's:
  12 records of 13 bytes, tags 0x01 / 0x05 / 0x06 / 0x07 / 0x96..0x9d, engrave_engine.md 7). Slot 1 FUN_18005e080: a tag 9 record ->
  3 floats then exception 13. Then motion records read by FUN_18005ccc0 (`lg1.c` l.183-405), values / f:
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

## 5a. Wrapping (dialog and data decoded; the transformation NOT DECODED)

- Enable: S+0xe4 == 1 (1149), type S+0xe8 (0 between 2 curves, 1 on surfaces), grid distance S+0xf0 (2.4; on load < 1e-12 or > 1e6 ->
  0.001 in / 0.02 mm, dll l.20927-20941). The note of 10502: "The wrapped toolpath requires 4 or 5 axis machining" (engraveres 10504).
- Geometry: the Engrave Geo Manager 1800 / 1801 (FUN_18001cc00, buttons Chains / Ref Pt / Upper / Lower chain / Start point / Surfaces /
  Surf curve) when the wrap flag or its type changed at the op dialog's OK (dll l.17654-17680: FUN_1802040d0 tests a geometry set, then
  FUN_18001cc00 or FUN_18001b530). Its menu loop (dll l.20040-20160): case 1 the chains (FUN_18001ca20), 4 / 7 the points S+0xf8 /
  S+0x110 (two p_3d, sec. 2.2), 5 / 6 / 9 the geometry sets CEngToolP +0x3728 / +0x3738 / +0x3748 (FUN_18001ca20 + FUN_180017b80; with
  +0x3718 the four sets), 8 the surfaces (selection, message 0x3aa3), 10 a check of the chains set; which button sends which case
  (dialogs 1800 / 1801) NOT DECODED.
- Computation: FUN_18001b530 (dll l.18992-19039): copies type / grid / tolerance (S+0x28) to +0x37c8 / +0x37b8 / +0x37b0, then
  FUN_180204420 (dll2 l.2917: the surfaces' bounding box over a linked list, error 0x3e8a for a type != 1), FUN_180203f50 / FUN_18001eca0
  (a grid of the surfaces / curves at the grid distance), FUN_180204130, FUN_180204a60 -> FUN_180203a50 / FUN_180211380 / FUN_180205460
  (dll2 l.3178..): the wrap mapping, then the moves are written with the flag +0x3710 (dll l.19755 / 19949 / 20053) - NOT DECODED (dozens
  of functions in 0x180203000-0x180212000), and its output needs 4 / 5-axis moves (tool axis per point), which the app's NCI / post path
  does not carry for 2D ops. **Refused** (engrave_setup); the dialog stores its values. Plan (formats/TODO.md "Engrave wrap"): decode
  FUN_180203a50 / FUN_180205460 / FUN_180211380 (the mapping of a 2D move onto the curves / surface grid and its tool axis), then how
  the wrapped moves reach the NCI (the writer behind +0x3710: 5-axis NCI codes; nci_write.c op_is_multiax / nci_reverse.c already know
  the multiaxis op codes - reuse them), then the app's Move needs a tool axis for this op family.

## 6. LG1 -> NCI (FUN_180041910, `core5.c` l.220-597) - partly decoded

Per motion record of each tool (a tool row named "BIGTOOL" is skipped when remachining is on, P+0x98; FUN_180055180 test):
x, y from the file; z by tag: 0x0c `-z`, 0x0a `-z` (or `-local_140` = the depth of the tool row from FUN_18004abf0, when z = 0),
0x0b `local_138 - z` (local_188 = FUN_18004abf0's core tool copy, local_140 / local_138 = its +0x48 depth / +0x50 fly: the fly
height above the top, dll l.46630-46654); then `+ P+0x190` (`P+0x190` = top: absolute
T+0x48, or T+0x48 + chain z; FUN_18004aea0). Moves go through `mwEngraveToolpathImpl` (vtable +0x10 = Write3dMove
FUN_18001de90 -> FUN_18001e3c0) with feeds chosen by FUN_1800468e0 from T+0x78 / +0x80 / +0x88 (feed / plunge / retract) and the
rapid / retract levels P+0x198 (feed plane), P+0x1a0 (clearance), P+0x1a8 (retract), P+0x1b8 (FUN_18004aea0: the higher of
clearance / retract when both on, etc.). The link sequence was decoded since (contour_build.c FUN_180041910): formats/engrave_engine.md 8.

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
   control <-> S table (sec. 2.2) and enable rules (FUN_18000e030). 1079 / 1075 / 1036 / 1048 / 1104 hidden as in Mastercam (2.1).
   Round trip: tools/tests/engrave_rt.sh (2026-11-19: 3 ok - coaster ops 2 / 4 edited through 1804 and a new mm op vs Mill_MM: the
   record's S / header / old copy byte-identical apart from the edit). Not yet: 2018 records: done (E-2018, the rows offset by the ENGR marker).
2. **Driver** (DECODED): heights (sec. 3.1), validation messages (2.3), depth warning (1808), engine parameter derivation
   (3.2 / 3.3) - testable by writing EST_DAT.TXT and comparing with the shipped sample layout.
3. **Engine**: decoded and ported (formats/engrave_engine.md, viewer/engrave.c, oracle tools/tests/engrave_test.sh). Before that: Until then the oracle only fixes:
   Grund finish = offset by tip radius at depth; Spiegel finish = offset by `r_tip + depth tan(a)` at depth plus 3D corner
   climbs to the top; zigzag rough at `tip_dia x (100 - overlap)/100` spacing, angle S+0x18. Per the no-improvisation rule
   these must not be filled with our own geometry; the rough/corner algorithms stay unported.
4. **NCI** (PARTLY): z mapping per LG1 tag, feeds, retract levels; then the existing arc filter (`viewer/cam_filter.c`,
   `cam_filter_nodes`) with the op's filter fields.
5. Wrap (2 curves / surfaces) and remachining: parameters and dialogs ported, generation refused (3.4, 5a).
6. **App** (2026-10-26, DONE for lines / arcs): `viewer/cam2d_engrave.c` (record = stored op or defaults library op + the dialog's
   record patches; chains = cam_chain_polyline at S+0x28 / 10, closed only, first chain's z; engrave_generate; then cam_arc_filter),
   op dialog pages 1802 / 1803 (cui_opdlg.c; stepover % shown as 100 - S+0x08), test scene `gen_engrave_depth`
   (tools/tests/op_scenes.txt).

### Undecoded callees (engrave.dll unless noted)

| address | role |
|---|---|
| FUN_1800525b0 (the remachining part, dll l.57600..) | the EST_DAT tool list for 2 / 3 tools |
| FUN_180203a50 .. FUN_180212000 (dll2) | the wrap mapping (5a) |
| (writer of record +0xd38..+0xfe5) | the old-format copy of the header and S (sec. 1) |
| FUN_180046920 | feed choice per move (via FUN_1800468e0) |
| FUN_18001e3c0 / FUN_180200ac0 | Write3dMove / end move into Mastercam NCI |
| FUN_18001e400 | contour list build from the op's chains (P+0x10) |
| FUN_1800173f0 | per-geometry-set handling (4 sets: chains, wrap curves / points) |
| FUN_18001ca20 / FUN_180017b80 | wrap geometry selection |
| FUN_1800569c0 | tool list for FUN_1800525b0 |
| EStamp7.exe FUN_005229b0 globals -> HRKORRD / HSCHRAFF / KONTBILD code | the engine |

## 9. Questions for the user (deviations / choices, 2026-11-19)

- **E-DW1** (decided 2026-11-19, user_decisions.md): asked before every regeneration that shows it, as Mastercam (pp_engrave_ask:
  op dialog OK, Regenerate selected / dirty, tool / chain regenerations; scripts: egdwarn:N). PORTED.
- **E-DW2** (decided: approved deviation): Mastercam's carry-over of "Use the maximum depth" into later regenerations is not
  reproduced (engrave.c, "ours, user decision E-DW2").
- **E-T17** (decided): tool type 17 stays refused until a test part holds such a tool (windows_checklist.md 12o); then read tool +0x2ef0
  (TL +0x290) and port its depth-warning row.
- **E-SUB** The sub-dialogs' Help buttons are greyed (no help port), as the other op dialogs.
- **E-2018** (decided, PORTED): the dialog's Engrave rows are offset by the record's 'ENGR' marker (cui_opdlg.c eg_s_shift; 0x1420 ->
  +8). Not testable on the corpus: the one 2018 Engrave op (3D Door Machined op 5) is not editable here (its Tplane is not a system
  plane at the origin).
