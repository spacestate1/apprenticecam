# NESTING.DLL — Geometry Nesting add-in, port spec (2026-11-05)

User decisions N-1 / N-2 (formats/user_decisions.md l.218-222): port Transform > Geometry Nesting exactly as Mastercam 2017
does, engine included. The engine (NestLib, n64dll.dll) is in formats/nesting_engine.md. This file covers the Mastercam
side between the user and the engine. It supersedes the call-path and NOT DECODED parts of formats/wireframe.md
"Geometry Nesting — scope / port spec (2026-11-04)"; the corrections are listed in §10.

## Sources
- Binary `~/mastercam-re/files/nesting.dll` (image base 0x180000000). Ghidra project `~/mastercam-re/ghidra_nesting`
  ("nesting", auto-analysed 2026-11-04).
- Dumps in `decomp/nest/` (gitignored), made 2026-11-05:
  - `nesting_all.c`: every function, DumpDecomp ".", 4190 functions, 171 763 lines. **"L<n>" = line in this file.**
  - `nesting_index.txt` (`addr name startline nlines`), `nesting_syms.txt` (SymList; RTTI class vtables).
  - `nesting_exports.txt` (11 exports), `nesting_pe.txt` (objdump -p), `nesting_strings.txt` / `nesting_strings_w.txt`.
  - `mcnestingres_dialogs.tsv` (tools/pe_dialogs.py) and `mcnestingres_strings.txt` (tools/pe_strings.py).
- Where Ghidra lost call arguments they were read from `objdump -d nesting.dll`; those are cited "(asm @addr)".
  Message maps and vtables were read from the binary's .data.
- Defaults `~/mastercam-re/files/nesting.defx` (inch) / `nesting_mm.defx`. Help: `~/mastercam-re/chm/mcnesting`.
- Tags: `[inferred]` = reasoned, not read. `NOT DECODED` = not read or not understood. "P+x" = parameter block CParam
  field x; the live copy is at 0x180137840 + x (§3.1).

## 1. Entry path (mode 2 = geometry)
1. CO `do_nest_g` -> MC `run_nesting_func("m_nest_g")` -> NE export `m_nest_g` @1800450b0 L48031.
2. `FUN_18003eac0(2)` L43316: 10101 "nesting already running" when `nestingMODE != 0` (L43327); Ctrl held toggles
   RecordBoundaries (L43332-43338); `nesting_check_product` else 10003 (L43339, L43354); `FUN_180043fd0(2)` L47225 resets
   the session state, nestingMODE = 2 (L47231); saves the OM insert location / selected ops, suspends repaint (L43344-43350).
3. `FUN_180043c20` L47031: data dir `DAT_180136280` = `<GetApp()->vtbl+0x268(path id 3)>\Nesting\` (`_mkdir` if missing,
   failure 10012, L47073-47080); report folders `reports_prefix + "IMG\"` / `"XML\"` (L47083-47086). Path id 3: NOT DECODED.
4. Licence `FUN_180038350` L38691: `ShouldNestingRun()` else router-on-SIM level > 1, else "Warning - invalid nesting
   license."; `DAT_18013785f` (TrueShape flag) = the ShouldNestingRun result (L38719). The port drops the licence (the
   TrueShape default then comes from the .defx TRUESHAPE=YES - user to confirm).
5. `DAT_1801305d9 = 1` (geometry, L48048) -> session driver `FUN_18003ce30` L42112 -> cleanup `FUN_18003e8d0` L43211
   (nestingMODE = 0). `m_nest_g` returns 0 on success else 3 (`FUN_18003cc20` L41990).

## 2. Session driver FUN_18003ce30 (L42112-42954), mode 2 path
1. Init (L42179-42231): SaveRestoreLevelsRefresh; status bar / user-app notify off; session name "nesting.nstx";
   profile tolerance `DAT_180137988` = 0.01 inch / 0.1 mm (L42214-42223). Mode 2: toolpath flag `DAT_180135a9b = 0`,
   `DAT_1801305da = !FUN_180044440()` (L42232-42243). Title 10010 "Nesting 2017 V2013R2" (L42365); `FUN_18003b250(0)` loads
   the defaults (§3).
2. **Restore last session** (L42374-42377): registry `<REG_KEY>\Nesting\RestoreLast` starting with 'Y' (FUN_180079570
   L81903) -> `FUN_18003b270("nesting.nstx", 1)` loads it silently from the data dir.
3. Dialog loop (L42378-42792): build `CNestPropSht` (FUN_180065d80, §4); Load default sheet when LoadDefaultSheet (P+0x98),
   no sheets and nothing restored (`FUN_18003b120` L40790: copy of the default sheet named "Sheet #"+id, 10084;
   L42468-42470); `DoModal` (vtbl+0x2d8, L42481): 0x3de (TrueShape/Rectangular changed in Configuration) or 0x3df (Groups
   page must appear / disappear) rebuild and re-show the sheet (L42498-42503); not 1 = cancel.
4. OK: with RestoreLast, save `FUN_180040bc0("nesting.nstx")` (L42505-42507); pre-checks (§6.1); `UndoTransStart`
   (`FUN_18003edc0`: always for geometry, L42574-42577); `FUN_1800412e0` colour cycling, `FUN_1800413e0` level
   pre-assignment, `FUN_180041d00(1)` label positions (§7.4), `FUN_180037d60` automatic origins (§7.1) (L42579-42587).
5. **Perform-Nesting** `FUN_18003fb40(0)` (L42613; body L43997) runs the engine (§6) and **writes the nested geometry into
   the database** through ns_output (§7), inside the undo transaction. Failure: 10102 + 10103, back to the dialog (L42654).
6. Display Results dialog on (P+0x94): `CResultDlg` modal (FUN_18007aea0, L42638-42647; §8). Not OK: delete all result
   entities (FUN_1800867c0 / FUN_1800868c0, L42704-42709), save part notes to `<data>\p<name><n>.tmp` (FUN_180060080
   L42671), `UndoTransTerminate`, restore the notes (FUN_18005bfa0 L42689); Redo (`FUN_180081cf0` = dialog+0xb1a1) loops
   back to the property sheet with the session kept, Cancel returns 0.
7. Success (L42810-42869): `FUN_1800414f0` names the result levels; `FUN_180043870` appends the scrap (§7.6); reports (§9)
   when `DAT_1801305dc` (L42626-42633); per sheet `FUN_18008ca40` / per part `FUN_1800577b0` delete original chains (§7.5);
   `UndoTransCommit` (L42947); `rebuild_graphics`.

## 3. Settings files (.defx / .nstx / .nslx / .nplx)
- **One format.** All four are Windows INI files read and written with `Get/WritePrivateProfileStringA`.
  - The writer upper-cases section, key and value (FUN_1800d3f00 L146109).
  - Booleans are written YES/NO; the reader accepts True/Yes/Y/1 and False/No/N/0 (FUN_1800d12c0 L144332).
  - Char values map '_' to ' ' (FUN_1800d1100 L144266).
  - Points are written as KEY-X / KEY-Y (FUN_1800d2d50 / FUN_1800d2c80).
  - Writers force the extension to end in 'x' (FUN_1800d97f0).
  - Loaders pick INI when the name ends in 'x' (FUN_18003b270 L40985-41079 -> FUN_18003b6b0). The legacy binary
    .nst/.nsl/.npl readers (FUN_18004f760, FUN_1800cb8c0; record types 1 sheet, 2 part, 3 group) are NOT DECODED.
- **Defaults**: `<data>\nesting.defx`, or `nesting_mm.defx` when metric.
  - Load FUN_18003ad10 L40636, in order: [PARAMETERS] -> defaults copy 0x180137650; [SHEET1] -> default sheet 0x180138090;
    [PART1] -> default part 0x180137a30; [GROUP1] -> default group 0x180136eb0. Then the defaults are copied to the live
    block 0x180137840 (L40777).
  - Write FUN_1800407a0 L44512.
  - In metric the CParam ctor defaults are multiplied by 25.4 before the file is read (FUN_18004f6d0 L55423).
- **Session** (.nstx):
  - Write FUN_180040c50 L44857-44920: delete the file, then [PARAMETERS] (live), [SHEETn], [PARTn], [GROUPn].
  - Read FUN_18003b6b0 L41164-41301: sheets and parts stop after 5 consecutive missing sections; groups stop at the first gap.
    Ids are renumbered and PREVID reset to -1 (L41058).
- **Libraries**: .nslx = [SHEETn] only, .nplx = [PARTn] only. Save writes the next free index (FUN_1800d21d0, n <= 10000).
- `VERSION` is read and then overwritten with the constant 14.0 (`DAT_1801305f0`); writers emit 14.0 (the shipped .defx says 10.002).
- Files with Version < 10 get SHEETTOPARTDIST += PARTTOPARTDIST on read (L34097).

### 3.1 [PARAMETERS] (geometry-relevant keys)
Reader FUN_18004f8a0 L55501, writer FUN_180052240 L56323, ctor FUN_18004e070 L54652-54720.

| key | P+off / global | ctor default | UI | feeds |
|---|---|---|---|---|
| TRUESHAPE | 0x1f `18013785f` | licence result | Cfg 7031/7032 | engine choice (§6.2) |
| ACCUNEST / EXACTMODE | 0x1d / 0x1c | 0 / 0 | – | NSetAccuNest is always 0; ExactMode is used only by toolpath common edge |
| RESCBOX / RESUSER / RESOLUTION | 0x28 `180137868` / 0x30 `180137870` / 0x20 `180137860` | 0 / 1.0 / 1.0 | Cfg 7023 / 7024 | NSetResolution (§6.3); RESOLUTION is overwritten by the computed value (L39950) |
| PARTTOPARTDIST | 0x48 `180137888` | 0.1 | Parts 7006 | NSetClearance via FUN_1800cad50 (0 only in toolpath common edge, L139900) |
| SHEETTOPARTDIST | 0x40 `180137880` | 0 | Sheets 7004 "Sheet Margin" | engine collar = SHEETTOPARTDIST - clearance (`DAT_180136e58`, FUN_1800cddf0 L142120-142140) |
| SHEETTOSHEETDIST | 0x38 `180137878` | 1.0 | Sheets 7002 | result-sheet layout (§7.1) |
| STARTINGCORNER | 0xb0 `1801378f0` | 1 | Sheets 7029 (1 LL, 2 LR, 3 UR, 4 UL) | NAddSheetInfo corner; forced 1 when not TrueShape (L34208) |
| SHEETFILLDIRECTION | 0xd9 `180137919` | ' ' | Sheets 3026 (X / Y / Calculate) | NAddSheetInfo direction |
| GUILLOTINECUT | 0x54 `180137894` | 0 | Sheets 7033 | NSetGuillotineCut; forced 0 when not TrueShape, message 10123 + "[GUILLOTINE]" (L34204) |
| AUTOORIGINS | 0xd4 `180137914` | 1 | Sheets 3022 | §7.1 |
| FILLALLSHEETS | 0x58 `180137898` | 0, **forced 0 on read** (L55588) | Parts 1013 | max quantity = 50000 (FUN_18005be90 L63182) |
| NESTFILLERSTONESTHEIGHT | 0x60 `1801378a0` | 1 | = !FillAllSheets (L78294) | NSetNestFillersUptoNestedHeight |
| AUTOPAIRS | 0x5c `18013789c` | 1 | Parts 7027 "Optimized pairs" | auto-pair clusters (§6.4) |
| FITPARTINPART / PREFERHOLEFILLING | 0x50 `180137890` / 0x64 `1801378a4` | 0 / 0 | Cfg 7025 / 7026 | NSetFitPartInPart(fit, prefer) |
| ADDLABELS / LABELHEIGHT / LABELHEIGHTAUTO | 0xc0 `180137900` / 0xc8 `180137908` / 0xd0 `180137910` | 0 / 0.25 / 1 | Parts 4034 / 4036 / 4035-4037 | §7.4 |
| NAMEASLABEL | 0xa `18013784a` (byte) | 0 | Cfg 7027 | label text |
| DELETECHAINS | 0x68 `1801378a8` | 0 | Cfg 7021 | §7.5 |
| CYCLECOLORS / CYCLELEVELS / CYCLEUNUSED | 0x74 / 0x78 / 0x7c | 0 | Cfg 7007 / 7012 / 7017 | §7.3 |
| RESULTCOLOR / RESULTLEVEL | 0x80 / 0x84 | 10 / 1 | Cfg 7008 (0..255) / 7013 (1..10000) | cycle start values |
| USEMAINCOLOR / USEMAINLEVEL | 0x6c / 0x70 | 0 | – (not in the UI) | §7.3 |
| DISPLAYRESULTSDLG / DISPLAYGROUPPAGE / LOADDEFAULTSHEET | 0x94 / 0x90 / 0x98 | 1 / 0 / 0 | Cfg 2034 / 2033 / 2032 | driver |
| COMPRESSRESULTS | 0x1e `18013785e` | 0 | Cfg 2040 | identical results merged, "%s (%d copies)" (L4534, L5753) |
| SAVESCRAP / SCRAPNAME / CHECKFORDUPLICATESCRAP | 0xa0 / 0x130 / 0xb | 0 / "scrap" / 0 | Cfg 7018 / 7019 / 7028 | §7.6 |
| HIGHRESIMAGES | 0x9c | 0 | Cfg 7200 | report images (use NOT DECODED) |
| IGNOREHOLES / ADDPARTSASGROUP / SCANFORNOTES / AUTOATTACHSEARCH / ALIGNWITHLONGESTEDGE | 0xac / 0xa4 / 0xbc / 0x8c / 0xa8 | 0 / 0 / 0 / 0 / 1 | Parts menu 40402 / 40401 / 40405 / 40404 / 40403 | §5 |
| PROFILETOLERANCE | 0x148 `180137988` | 0.01 inch / 0.1 mm | – | part+0x410 |
| TRIM* | 0xc-0x18 (TRIMACTIVE `18013784c`) | | toolpath Additions page | default state of the trim switches (§8) |

- Every other key in nesting.defx (ONIONSKIN-*, SORT*, XFM*, OPSELECT_*, COMMONEDGE, CREATEBLOCKOPS, PARENTOPID,
  SEPARATEOPPERSHEET, STOPBETWEENSHEETS, CREATECHILDOPS, TOOLORIGINCORNER) is toolpath nesting only.
- **INNERHOLES is never read by 2017.**
- The common sheet grain (`DAT_180137918`, P+0xd8) is not stored in [PARAMETERS]: it is read from and written to each
  [SHEETn] GRAINDIRECTION (FUN_18008e610 L95447).
- "Restore last session" is the registry value RestoreLast, not a .defx key (FUN_18007a180 L82434).

### 3.2 [SHEETn], [PARTn], [GROUPn], contours, notes ("key field (default when missing)")
- **[SHEETn]** (reader FUN_18008e610 L95325, writer FUN_180091410 L97293; CSheet 0x398 bytes): SHEETID +8 (auto);
  SHEETNAME +0xc ("def_sheet"); FILENAME / FILEEXT +0x220 / +0x228; MATERIAL +0x210; LENGTH / WIDTH +0x230 / +0x238
  (96 / 48); ADDEDRECTANGULAR +0x240 (forced 1 without contours); ORIGINPOINT-X/Y +0x248 / +0x250; PRIORITY +0x268 (>= 1);
  QUANTITY +0x26c (1); GRAINDIRECTION -> global `DAT_180137918`; DELGEO +0x338; BASEPOINT-X/Y +0x270 / +0x278;
  MINMAX-X/Y +0x280; AMAR +0x290 ("Create necessary quantity"); CONTOURS-NUMBER (list +0x298); NOTES-NUMBER (+0x2c0).
- **[PARTn]** (reader FUN_18005c3e0 L63482, writer FUN_180060d20; CPart 0x558 bytes): PARTNAME +8 / PARTLABEL +0x108
  ("undefined"); PARTID / PREVID +0x230 / +0x234; FILENAME / FILEEXT / MATERIAL +0x220 / +0x228 / +0x210; LENGTH / WIDTH
  +0x238 / +0x240; ADDEDRECTANGULAR / USERRECTANGULAR ("Use bounding box") +0x248 / +0x24c; MINQUANTITY +0x250 (**10**;
  the ctor uses 20, L57454); FILLQUANTITY +0x254; STEPANGLE +0x258 (90); PRIORITY +0x260; COMMONTOOL +0x264; MIRROR
  +0x268; GRAINDIRECTION +0x278 ('N'); CLUSTERMETHOD +0x27c (7 normal, 4 user cluster); DELGEO +0x4b8; BASEPOINT +0x280;
  MINMAX +0x290; CONTOURS +0x468; NOTES +0x490. Debug `DAT_180135290` adds write-only keys (FUN_180061330 L66841).
- **[GROUPn]** (reader FUN_18001e0b0 L23219): GROUPID +8, GROUPNAME +0xc, GROUPSTATUS +0x10c, MINQUANTITY +0x110,
  FILLQUANTITY +0x114, PRIORITY +0x118; per member i <= 100: PARTSTATUSi +0x11c[], PARTQTYi +0x2ac[], PARTIDi +0x43c[].
- **Contours** (CCtour 0x90; reader FUN_18001b390 / FUN_1800d1670 L144455, writer FUN_1800d4550): CONTOURn-TYPE (+0x64:
  0 outer, 1 hole, 2 point, 3 ignore; §5.1), -NUM_PTS, -XPOS / -YPOS +0x20 / +0x28, -PART_NUM +0x40 (cluster element),
  -VIEW +0x44, -DEPTH +0x48, -COLOR / -LEVEL / -WIDTH +0x50 / +0x54 / +0x58, -STYLE / -PSTYLE +0x60 / +0x62, -M33 (only
  when view > 8). Entities `CONTOURn-OBJk-*` = 0x60-byte ctour_rec: -OBJECT +0x21 'L'/'A'; -POINT-X/Y +0 (end point;
  OBJ1 holds only the start point); 'A' only: -CENTER-X/Y +0x10 and -CIRCLE +0x48; -REVERSED +0x20 (CW); -DEPTH +0x40.
- **Notes** `NOTEn-*` (FUN_1800d29a0 L145155): COLOR, LEVEL, EPTR, V8NOTE (the note struct), PART_NUM, TEXTi.

## 4. The property sheet (CNestPropSht, FUN_180065d80 L69872-69968)
- **Pages in mode 2:** Sheets (CShtPage, 101); Parts (CPrtPage, template **105**: FUN_180069a80 L72337-72342 picks 0x69
  when geometry, 0x6b = 107 when toolpath; CPrtPageG is a secondary base inside CPrtPage, not a page); Groups (CGroupPage,
  122) only when DisplayGroupPage is on or a group exists (L69924-69935). Parameters (132) / Additions (133): toolpath only.
  Start page `DAT_1801356ac` (L69959-69966).
- **Buttons along the sheet** ('G' set, FUN_180066310 L70180-70185; message map FUN_180066750 L70331): 40014 New
  (FUN_1800441d0 L47307: clear, reload defaults + default sheet); 40016 Open (FUN_180044200 L47323, .nstx/.nst); 40018 Save
  (FUN_180044350 L47409, Save As when unnamed); 40020 Save As (FUN_180044380 L47428); 40022 Restore (FUN_18003ffa0 L44240:
  factory reset - RestoreLast = No, tolerance, label height = note default with Automatic on, default sheet / part / group /
  params from the ctors, .defx rewritten); 40023 Config (FUN_180066990 L70398: CCfgDlg, EndDialog 0x3de / 0x3df; TrueShape
  without a TrueShape licence -> 10098 and switched back off, L70409).
- **Page background right-click** (menu 610): 40002 Save defaults (FUN_180040990 L44623: current sheet / part / group /
  params become the defaults, ClusterMethod forced 7); 40003 Load defaults (FUN_18003b250(1)).
- String ids 13001-13140 are not referenced; the real menus are resources 601 (parts), 604-606 / 608 (groups), 609 (sheets).

### 4.1 Sheets page (101)
DDX FUN_180093090 L98491; init FUN_180096420 L100632; handlers below.

| ctrl | field | range / items |
|---|---|---|
| 3003 list | sheets `DAT_180140e18` (count `DAT_180140e28`) | tooltip 10036 |
| 3005 | in-place rename | |
| 3007 / 3009 Size | +0x230 / +0x238 (FUN_180096b60 L100880 / FUN_1800972c0 L101187) | tol .. 30000 |
| 3017 Quantity | +0x26c (FUN_180097200) | 1 .. 10000 |
| 3018 Create necessary quantity | +0x290 (FUN_180098880 L102229) | as decompiled, writes only sheet[0] |
| 1130 Priority | +0x268 = index+1 | "1".."10" |
| 3012 / 3014 Origin X / Y | +0x248 / +0x250 | ±30000 |
| 3015 Select | point pick FUN_180099420 | NOT DECODED |
| 1117 / 1118 Material | +0x210 | dialog NOT DECODED |
| 3021 Grain Direction | `DAT_180137918` = "XYN"[i] | 10050 Horizontal, 10051 Vertical, 10052 Ignore |
| 7029 Nesting Corner | StartingCorner = i+1 | 10171-10174 |
| 3026 Fill Direction | `DAT_180137919` = "XY "[i] | 10050, 10051, 10053 Calculate |
| 7033 / 3022 / 7002 / 7004 | GuillotineCut / AutoOrigins / SheetToSheetDist / SheetToPartDist | 0 .. 30000 |

**List right-click** (menu 609, FUN_180095bb0 L100264): 40008 Add rectangular (FUN_180098710 L102151, copy of the default
sheet); 40011 Add chained (FUN_180097f50 L101830, §5.2); 40013 Add from files (FUN_1800984e0); 40015 Import from library
(FUN_180098ef0 L102523: .nslx / .nsl / .nstx / .nst, else 10034); 40017 Save to library (FUN_180098cb0 L102397: .nslx);
40019 Delete all; 40021 / 40024 Move up / down.

### 4.2 Parts page (105)
DDX FUN_18006cfe0 L74351; init FUN_180070020 L76114.

| ctrl | field | range / items |
|---|---|---|
| 4003 list / 4005 rename | parts `DAT_180138428` (count `DAT_180138438`) | rename up to 80 chars |
| 4007 / 4009 Size | +0x238 / +0x240 | tol .. 30000 |
| 4011 Minimum Qty | +0x250 | 0..10000; max 50000 - filler when Fill all sheets is off (FUN_180073930 L78298) |
| 4013 Filler Qty | +0x254 | 0..10000 |
| 4015 Step Angle | +0x258 (FUN_180070770 L76352), normalised by FUN_1800c4be0 | |
| 4017 Priority | +0x260 = i+1 | 1..10 |
| 4033 Label | +0x108 | duplicate -> 10104 |
| 1116 / 1115 Material | +0x210 | |
| 4019 Use bounding box | +0x24c | unticking restores L/W from MINMAX (L78465) |
| 1100 Common Edge | +0x264 COMMONTOOL (L75644) | |
| 4024 Grain | +0x278 = "XYN"[i] | |
| 4022 Mirror | +0x268 = i | **two items only**: 10501 Original Only, 10502 Original or Mirror (L76146-76152) |
| 7006 / 4034 / 4035-4037 / 4036 / 7027 / 1013 | PartToPartDist / AddLabels / LabelHeightAuto / LabelHeight / AutoPairs / FillAllSheets | |

- The colour swatches 1084-1089 (Ignore / Profile / Hole) and progress bar 9210 are shown only in toolpath mode
  (L76118-76131). They are an owner-draw legend (FUN_18006fe40 L76042) and do not drive classification.

**List right-click** (menu 601, FUN_18006f3a0 L75664): 40005 Add rectangular (FUN_180072b30); 40006 Add chained parts
(FUN_180071a20(0), §5.1); 40007 Add chained cluster (FUN_180072640(0)); 40033 Disassemble solids (FUN_180071a20(4)); 40010
Add solids (FUN_180071a20(3)); 40004 / 40041 Add from files / folder (FUN_180071a20(1/2)); 40027 / 40042 Add clusters from
files / folder (FUN_180072640(1/2)); 40001 Import from library (.nplx / .npl / .nstx / .nst); 40002 Save to library (.nplx);
40009 Attach geometry (FUN_18006ad60); 40003 Delete all.

Add options are toggles of live params (L75741-75870): 40401 Create a group when adding multiple parts (AddPartsAsGroup);
40402 Ignore holes (IgnoreHoles; enabled only when TrueShape); 40403 Align with longest edge; 40404 Auto attach (search);
40405 Scan for notes.

### 4.3 Groups page (122) and Configuration (CCfgDlg, 201)
- **Groups:** 6007 Quantity 1..3500 -> group+0x110; 6011 Priority -> +0x118; 6017 member quantity 1..3500 -> +0x2ac[i]
  (FUN_1800246c0 / FUN_180024bd0 / FUN_180024770). Lists are drag-drop CDDListBox; at most 100 parts per group (10079).
  Menus FUN_180021ba0 L25289.
- **Configuration:** ctor FUN_18000a7a0 L7213, DDX FUN_18000ae60 L7483, OnOK FUN_18000c4b0 L8239, enable rules
  FUN_18000ceb0 L8767.
  - It edits a copy of CParam: the live block, or the defaults block in defaults mode.
  - Controls -> keys as in §3.1.
  - Accuracy 7023: 0 Fine, 1 Medium, 2 Coarse, 3 User-Defined (10191-10194). The 7024 value is editable only for
    User-Defined, between 0.003 inch / 0.05 mm and 100 inch / 2500 mm (L8073-8086).
  - Cycle-colour and Cycle-level edits are enabled only while their box is ticked.
  - 7026 needs TrueShape and 7025 ticked.
  - 1004 "Save as Defaults" also copies the block to the defaults and writes the .defx (L8306-8309).
  - Returns 0x3de when the Rectangular/TrueShape radio changed (FUN_18000d490 L8873).

## 5. Chains -> parts and sheets
### 5.1 Add chained parts (FUN_1800745e0 L78893, prompt 10030)
1. `chain_it_point` (L79261), then `FUN_1800c4d20` L135796:
   - every chain must lie in one view (FUN_1800c9150; parallel views are accepted with the depth flipped), else 10062;
   - Z within tol snaps to 0 (L138748);
   - in that plane `chains_to_pock_bounds(chains,1,0,1,nc_tol)` (L135835); failure = 10061.
2. One CPart per outer boundary:
   - `new CPart` (FUN_1800540f0 L57434), parameters copied from the default part;
   - **outer selection** FUN_1800c74f0 L137154: the first closed, non-point bound that contains every other bound
     (`IsContourNested(.., 0.1)` / `pt_in_ctour`). Bounds inside or overlapping it go to this part; the rest wait for
     the next part. No outer at all = 10060.
3. **Classification** FUN_180054930 L57821 (CCtour+0x64). The largest closed bound by |area| is tested first (L57870-57914).

   | code | meaning | rule |
   |---|---|---|
   | 0 | outer | the bound chosen in step 2 |
   | 1 | hole | closed, nested in the outer, not inside another inner closed bound, and IgnoreHoles = 0 (L58023-58027) |
   | 2 | point | |
   | 3 | carried along, not nested | open chain; closed but outside the outer; an island inside a hole; a hole when IgnoreHoles = 1 |

   A single open chain = 10063 "chain must be closed" (L58048-58050).
4. **CCtour build** FUN_180054eb0 L58077: `pock_bound_to_ctour` -> 0x60-byte ctour_rec entities (3D chains via
   `chain_in_view`, else 10062, L58270-58276); arcs re-validated with `constr_ns_arc` / `constr_circle` (2·nc_tol), a
   failure becomes a line, logged when |r_start - r_end| >= part+0x410 (L58337-58355); bbox includes arc quadrant points
   (L58361-58400); coordinates relative to the outer's first point (part+0x280 / +0x288, L58436-58465); each CCtour keeps the
   first entity's colour, level and attributes (L58468-58498).
5. **Finalise** FUN_180057b20 L60191: shift so the bbox min is (0,0); length +0x238 / width +0x240 = bbox size, min corner
   +0x280 / +0x288, MINMAX +0x290 / +0x298 (L60303-60308); Scan for notes (FUN_1800cd7e0) and auto-attach (FUN_180057f80):
   NOT DECODED.
6. Name "Part #"+id (10085), label via FUN_1800c49c0 (L79096-79149). With AddPartsAsGroup a "Group #" is created with
   member quantity 1 per part (L79156-79227).

### 5.2 Add chained sheets (FUN_180097f50 L101826)
- The same pipeline builds the contours with FUN_18008a340(sheet, **0**, chains) (L101978), i.e. ignore-holes = 0.
- Name "Sheet #"+id, +0x240 (rectangular) = 0 (L101995-102002).
- How menu 13010 "Ignore holes when adding sheets" applies: NOT DECODED.

### 5.3 Rectangular tests and clusters
- Part is rectangular (FUN_1800599b0 L61429) when: not TrueShape, or no contours, or +0x248, or +0x24c.
- Sheet is rectangular (FUN_18008d7d0 L94583) when: not TrueShape, or no contours, or +0x240.
- Shape helpers: FUN_180058910 (axis rectangle / square), FUN_180058650 (circle).
- User clusters are type 4: elements via FUN_180059aa0 / FUN_180056df0; fewer than 2 elements re-types to 7 (L37479).
  The handler FUN_180074fb0 is NOT DECODED.

## 6. The run (FUN_18003fb40 L43997)
### 6.1 Pre-checks (driver + run)

| check | where | result |
|---|---|---|
| no sheets / no parts | L42514 / L42522 | 10016 / 10017 |
| sheet grain 'N' while a part has grain | FUN_180039a40 L39716 | 10029 |
| part too big | FUN_18003f400 L43681 | 10021 "(%s x %s) > (%s x %s)", abort |
| rectangular mode: non-rect sheets / sheets with holes | FUN_18003fe50 L44177 | 10080 / 5, then 4 "Proceed with rectangular nesting?" |
| licence | L42551-42566 | 10098 / 10099 |

Part too big: usable sheet = L - 2c, W - 2c (c = collar; a negative collar adds instead). It is tested only for steps that
map to 0/90/180/270, with (l + clearance, w + clearance), swapped when the step allows 90.

Quantities (L44017-44094):
- min, filler and their sum are each capped at 50000;
- engine min = +0x250, max = +0x250 + +0x254, or 50000 with FillAllSheets (FUN_18005be90 L63160);
- on a redo (`DAT_1801352b8`) the count already nested is subtracted;
- parts with max < 1 are skipped; none left = 10017 (or 10140 on a redo).

### 6.2 Engine choice
- `DAT_1801352ac` = TrueShape flag, else FUN_1800c7960 (L42528-42535).
- **Rectangular mode is upgraded to the TrueShape engine** (FUN_1800c7960 L137375-137387) when multiple materials are
  used, a group exists, or the local-area mode is on. The parts are then sent as SHAPE R with mirror and common tool 0
  (L38139, L38174-38177).

### 6.3 Call sequence (L44096-44119)

| # | function | calls |
|---|---|---|
| 1 | FUN_180043040 L46407 | `NOpenNestLib(dataDir, 1, FUN_180006cf0)` / `RNInitRectangleNest()`; failure 10013 |
| 2 | FUN_1800411f0 L45083 | TrueShape: NSetErrorHandlerCB(FUN_180006cf0), NSetProgressFunctionCB(FUN_180006700), NSetNewPartGeomHandleCB(FUN_180006f90), NSetNewSheetGeomHandleCB(FUN_1800074b0), NSetNestedOrderResultCB(1800066f0), NSetNestedSheetResultCB(FUN_180005f90). Rectangular: RNSetErrorHandlerCB, RNSetProgressCB(FUN_180006700), RNSetPartNestedCB(FUN_180009da0), RNStartNewSheetCB(FUN_180009ca0), RNSetNestedSheetResultCB, RNSetNestedOrderResultCB |
| 3 | FUN_180039c90 L39838 (asm @180039d44-180039e51) | NStartSetParams; NSetDLLEnableKey(@1801301f0); NSetResolution(r); NSetPartDistType(0); NSetClearance(PartToPartDist); NSetThicknessAndMaterialSelection(0, multiMaterial FUN_18003e930); NSetNestFillersUptoNestedHeight(`1801378a0`); NSetFitPartInPart(FitPartInPart, PreferHoleFilling); NSetGrpMaxElmtSize(max(20, largest group)); NSetExactDistance(0 for geometry); NSetAccuNest(0); NSetGuillotineCut(GuillotineCut != 0); NSetClusterCutoffUtil(0); NSetCutterDiameter(0); NSetFitClusterFirst(0); NSetFitMaxOnLast(1); NEndSetParam. Rectangular: RNStartSetParams, RNSetResolution(r), RNSetClearance, RNSetNestFillersUptoNestedHeight, RNSetCutterDiameter(0), RNEndSetParam |
| 4 | FUN_18003a120 L40033 | NStartNewPartList; groups FUN_180036950, user clusters FUN_180035f90, parts FUN_180037400; NEndNewPartList |
| 5 | FUN_18003e870 L43177 | NStartNestOrder; sheets FUN_18003a3f0; NNestOrder (rectangular: sheets, RNNestOrder) |
| 6 | FUN_180039050 | NCloseNestLib / RNEndRectangleNest |
| 7 | FUN_180007ac0 L5430 | ns_output per stored result (§7) |

**Resolution r** (asm @180037e40-180038134):
- User-Defined: r = ResUser.
- Otherwise:
  - a = mean over all part elements of max(L, W);
  - size column: a < 10, < 100, < 1000, < 3000, else (thresholds not unit-scaled);
  - FUN_180038410 L38743 then enforces a minimum: m = max sheet dimension × 2e-5 + 0.001; if r <= m, message 10143 and
    r = m + 0.001.

| | a<10 | <100 | <1000 | <3000 | >=3000 |
|---|---|---|---|---|---|
| inch Fine / Medium / Coarse | 0.025 / 0.1 / 0.25 | 0.1 / 0.25 / 0.5 | 0.25 / 0.5 / 1.0 | 0.5 / 1.0 / 2.0 | 1.0 / 2.0 / 5.0 |
| mm Fine / Medium / Coarse | 0.5 / 1 / 2 | 1 / 2 / 5 | 2 / 5 / 7.5 | 5 / 7.5 / 10 | 7.5 / 10 / 20 |

### 6.4 Parts, clusters, groups
- **Engine priority** = P - UIpriority + 1, with P = max(#groups, max group priority, #parts, max part priority) + 1
  (FUN_1800393f0 L39308).
- **Plain part** (FUN_180037400 L38076; asm @180037650-1800376a1):
  - name `"%dp"` % id;
  - step: +0x258, or 360 when < 1;
  - not TrueShape: FUN_1800c4be0 snaps the step to 90/180/270 (rotate = 1) or 360 (rotate = 0) (L135724-135770);
  - grain != 'N' forces step 90 (L38171).

  Call: `NAddPartInfo(name, min>=1, max, rectShape, L, W, step, grain, enginePrio, 2, commonTool, mirror)`.
  - Mirror +0x268 is passed with 2 mapped to 3.
  - Rectangular engine: `RNAddPartInfo2(name, min, max, L, W, rotate, grain)` + `RNSetPartPriority`.
  - Multi-material: `NDefinePartMaterialList(name, 1, "M%d")`.
- **Optimized pairs** (L38228-38307): when TrueShape, AutoPairs, the part is not rectangular, not a rectangle, square or
  circle, and max != 1: `NStartCluster(name2, 7, 1, 0, ct)`, `NAddClusterElement("%dp", 2, 0, 0, 360.0)`, `NEndCluster`.
- **User cluster** (FUN_180035f90 L37371):
  - per element: `NAddPartInfo(elem, ..., mirror 0)`;
  - `NStartCluster(name, 4, 0, 0, ct)`, `NAddClusterInfo(name, step, grain, prio, 2, ct)`;
  - per element: `NAddClusterElement(elem, 2, x_i, y_i, 360.0)` (FUN_180057ee0).
- **Group** (FUN_180036950 L37682):
  - qty capped at 50000 / largest member quantity (10025 + 10026);
  - `NStartGroup(name, +0x10c, qty)`;
  - per member: `NAddPartInfo(..., memberQty*qty, memberQty*maxQty, ...)` + `NAddGroupMember(name, memberQty)`;
  - `NEndGroup`;
  - also writes `<data>\<group>.grp`.

### 6.5 Sheets (FUN_18003a3f0 L40201; asm @18003a6e3-18003a730)
- Name `"%ds%dq"` (or `"%ds%di%dq"`).
- qty = +0x26c, or 10000 when there is one sheet type with "Create necessary quantity".
- Collar c = SheetToPartDist - clearance. When c < 0, collar 0 is passed; for a rectangular sheet or engine L and W are
  also enlarged by 2|c|.

`NAddSheetInfo(name, qty, 1, isRect, L, W, 0, 0, 0, 0, 0, corner, dir, grain, 1, collar, 0, 0)`:
- dir = FillDirection, or 'X' if L > W else 'Y' for Calculate;
- grain = the sheet grain if any part has a grain, else 'N';
- then `NAddSheetPriority(name, S - prio + 1)` and `NDefineSheetMaterialList`.
- Rectangular: `RNAddSheetInfo2(name, qty, L, W, collar, grain)`.

### 6.6 Geometry callbacks
- **Part** FUN_180006f90 L4917. Rectangular parts -> FUN_1800073c0 L5120: one closed profile of 4 lines
  (0,0)-(L,0)-(L,W)-(0,W). Otherwise NDwgGeomStart, NDwgProfsStart(0); per CCtour with > 1 entity: code 0 ->
  NDwgClosedProfStart(1), code 1 -> NDwgClosedProfStart(0), codes 2/3 skipped; cluster elements translated by their
  position. Lines `NDwgAddLine(prev, end)` only when |dx| or |dy| > tol; arcs `NDwgAddArc(cx, cy, prevx, prevy, sweep)`,
  sweep = atan2 difference normalised to (0, 2pi], minus 2pi when CW (+0x20) (asm @180007277-1800072fb); full circle
  `NDwgAddCircle(c, |end - c|)`; `NDwgChangeCutterDia(+0x270)` per profile.
- **Sheet** FUN_1800074b0 L5154: NDwgProfsStart(1), outer contours (code 0) then holes (code 1). Negative collar: the outer
  is offset by |c| (FUN_1800ca890), min corner -> sheet+0x380/+0x388, contours shifted by -min; offset failure -> 10054 and
  SheetToPartDist := clearance (L5264-5293).
- **Progress** FUN_180006700: Esc stops; at most 50000 instances per part type. Local-area mode `DAT_180135e89` =
  FUN_1800cf400 L142901 needs part+0x51a (front/back zone), 0 in the CPart ctor (L57506), so geometry nesting never
  restricts placement [inferred: only two-sided toolpath nesting sets it].

### 6.7 The .nip log (`<data>\nesting.nip`)
It is written in pieces by the steps above; FUN_180035970 is only a path builder (L37088):

| block | writer | mode |
|---|---|---|
| `(SETTINGS (NEST_TYPE NORMAL)(LANGUAGE ENGLISH)(DIRECTORIES ..)(FILES (OUTPUT (FORMAT UNIFIED)(RESULT nesting.nop)(ERROR nesting.err))(MESSAGE c:\nestlib\mesgfile.inp)))` | FUN_180043040 L46502 | "w" |
| `(PARAMETERS RESOLUTION, PART_DIST_TYPE, CLEARANCE, NEST_FILLERS_UPTO_NESTED_HT, FIT_PART_IN_PART, EXACT_NESTING, INTELL_EX, ACCU_NESTING NO, PREF_HOLE_FILL, IF_GUILLOTIN_CUT, FILL_LAST_SHEET YES, CLUSTER_CUTOFF_UTIL, DEFAULT_CUTTER_DIAMETER, NEST_CLUSTERS_FIRST NO)` | L39981-40021 | append |
| `(PARTLIST (PART NAME MINIMUM MAXIMUM IF_MIRROR [MIRROR_TYPE] GRAIN ROTATION_ANGLE_METHOD BY_STEP_ANGLE STEP_ANGLE [PART_THICKNESS_LIST 1.0 PART_MATERIAL_LIST] FIT_TYPE PRIORITY (CLUSTER TYPE COMMON_TOOL) COMMON_TOOL SHAPE R/X LENGTH WIDTH)...)` | L40156 / L40194 | append |
| `(SHEETLIST (SHEET NAME QUANTITY SHAPE COLLAR LENGTH WIDTH DIRECTION PRIORITY CORNER LL/LR/UR/UL GRAIN [..MATERIAL_LIST] MULTI_CRN_TYPE)...)` | L40437-40491 | append |

- Tabs and parentheses are exact (literal strings in the cited lines).
- The fprintf values are [inferred] to be the same values passed to the API (confirmed by asm for
  NEST_FILLERS @180039f90).
- No NEST_ORDER block is written.
- This is the oracle input format of N-2.

## 7. Write-back: ns_output FUN_180007b60 L5455 (during the run)
### 7.1 Result read-back and sheet placement
- **Read-back** FUN_180005f90 L4303 builds CNST_Result (0x1c0) / CNST_Part (0x138) / CNST_Inst (0x30); argument order from
  asm @1800060a4-18000612c and @180006434-18000647c.
  - `NInqNestedInstance` outputs: d1/d2 (XLL/YLL) discarded; **d3 XMATRIX -> +0x08 x, d4 YMATRIX -> +0x10 y**; d5/d6
    discarded (NOT DECODED); **d7 ANGLE (deg) -> +0x20**; **i1 MIRROR -> +0x1c**; i2 IDENTIFIER discarded.
  - Sheet result fields: `NInqNestedSheet` L / W / area / part area / CUT_AREA ("Nested Area") / nested rectangle
    ("Nested Size") at +0x140..+0x170; `NInqNestedParameters` at +0x108..+0x130.
  - Rectangular (FUN_180005980 L3995, CRectPrtItem via FUN_180005830): x/y from RNInqNestedPart; angle 90 if rotated, else
    0; mirror 0. When rotated, x += part width (L6075-6077).
  - Engine names are parsed back: `<id>p`, `<id>p<e>u|c` (cluster element), `c<id>u|a` (FUN_180009850 L6564).
- **Sheet origin of each output sheet** (L5687-5747):
  - AutoOrigins off: the user origin +0x248 / +0x250.
  - AutoOrigins on: x = (SheetToSheetDist + L) × instance index + sheet+0x258; y = sheet+0x260.
  - FUN_180037d60 L38420 stacks the sheet types: x = the first sheet's x, y_i = y_{i-1} - (SheetToSheetDist + W_i).
  - Result: copies of one sheet run along +X, and sheet types stack down in -Y.
- Instance position = XMATRIX/YMATRIX + sheet origin (L6050-6087).

### 7.2 What is drawn
- **The sheet outline is drawn** (FUN_18008d820 L94604): an L×W rectangle when there are no contours (L94713); chained
  sheets copy the original entities (`chain_to_ents` / `ctour_to_ents`, L94813-94843); a sheet-name label note at
  sheet+0x360/+0x368 (+0x368 = W + clearance/10, L45680; L94891-94917); the sheet's notes (`mTableCopyNote`); all through
  CSheet::XformEnt FUN_180091b90 (L95016).
- **Per part instance** (FUN_18005a260 L61824): every contour, codes 0-3, so open chains and points ride along
  (L62865-62909; no contours -> an L×W rectangle, L62052); the part's notes (L62454-62559); the optional label (§7.4); all
  through **CPart::XformEnt FUN_1800621b0** (L62690) with position, angle, mirror, colour, level (transform order as in
  wireframe.md: view -> Top, -(base+min), mirror X, rotate Z, translate, back to view); then `store_ent`, failure 10014
  (L67768-67780).
- **Trim lines** (FUN_180085c80 L89335): horizontal / vertical trim at the nested extent + clearance + collar on the
  corner's side, valid when it saves > 0.1·clearance; a line entity is stored and blanked unless the trim is on
  (L6279-6310; default TrimActive).
- Results go into CResult (0x8b0). Utilisation = part area / sheet area × 100 (rectangular `DAT_180134638`: NOT DECODED).

### 7.3 Colour and level
- **Colour**, per sheet type (+0x354) and part type (+0x4cc), not per result (L94738-94760, L62077-62099), first match:
  a colour passed in by the caller; CycleColors (FUN_1800412e0 L45130 numbers sheets then parts from ResultColor, skipping
  the background colour, wrapping 255 -> 0, or -> 1 in FUN_18005a260 / FUN_18008d820); UseMainColor; else the entity's own
  colour (the main colour if it equals the background, L94789-94793).
- **Level**: CycleLevels gives each output sheet two consecutive levels from ResultLevel (FUN_180039560 L39421, called
  L5764): sheet level `DAT_180130604`, parts level `DAT_180130600`; "Only unused levels" skips used ones (FUN_1800c9a00
  L138968). Else UseMainLevel; else the entity's own level if visible, otherwise the main level (L94796-94803). On OK
  FUN_1800414f0 L45253 names them "<result name>" and "Parts on " (10168) + name. Per-type part level +0x4d0: NOT DECODED.

### 7.4 Labels (FUN_180041d00 L45585)
- Text: PARTLABEL, or with NameAsLabel the part name without path / extension (`get_just_name`, L62107-62123); cluster
  elements "%s-%d" (L62436).
- **The fit test uses NestLib itself** (status 10056): per part the text extents box (`get_ext` L45843) is nested as a 1×1
  "null_part" on a sheet made of the part's own outline, in a separate NestLib session (L45868-45914); if it fits, the
  height goes to part+0x208 and the position to +0x4e0/+0x4e8 (L45919-45958). Height = LabelHeight, or Automatic =
  min(L, W) / k for k = 2, 3, ... down to 0.1 inch / 2.5 mm or 0.25·min (L45804-45842). The inner resolution loop
  (<= 50 tries, FUN_180038140) is NOT DECODED.
- Note settings (L67711-67738): height = part+0x208, else LabelHeight, else 0.25; angle = instance angle; +0xd0 = 0.1;
  justification 1, or 9 when mirrored.

### 7.5 Delete original chains
On OK only (FUN_18008ca40 L94070 / FUN_1800577b0 L60017): when DeleteChains or the object's DELGEO flag is set, and the
object came from chains [inferred +0x33c / +0x4bc]. The chain entities and notes are deleted with `put_special_undo(2)`.

### 7.6 Sheet scrap
When SaveScrap is on (L5420-5424):
- a "CSheet-Scrap" copy "<sheet>-I<n>" is made per output sheet, with each placed instance's outline added
  (FUN_18005db10, [inferred] as holes);
- on OK FUN_180043870 L46791 appends the scraps to `<data>\<ScrapName>.nslx`, skipping duplicates when
  CheckForDuplicateScrap is on (FUN_1800c6dc0).

## 8. Results dialog (CResultDlg, template 205; message map @1801314d0)

| ctrl | id | handler | behaviour |
|---|---|---|---|
| Sheet (Result) combo | 8002 | FUN_180080e30 | selects the result |
| fields | 8007-8015 | FUN_180084d30 L88793 | Total / Nested Size, Total / Nested Area, Utilization ("%.2lf%%") |
| Horizontal / Vertical trim | 9268 / 9275 | FUN_180080a70 L86318 / FUN_180080b90 L86371 | UpdateData, result+0x5a4 / +0x5a0 = the check box; the fields then show the trimmed size / area (+0x830.. / +0x810.., FUN_180084d30 L88810-88830) and the trim line entities result+0x590 / +0x598 are drawn (FUN_1800869c0) / blanked (FUN_1800868c0) when the result has a trim (+0x7e0 / +0x808); the trim lines and values: FUN_180085c80 (decoded 2026-11-05, see the trim lines under **Results Delete**; ported) |
| Details | 8016 | FUN_180080a20 L86299 | images + Nesting.xml, `RunActiveReportsViewer`; on failure nesting.out in notepad |
| **Redo** | 8017 | FUN_180080b10 L86345 | sets +0xb1a1 and closes; the driver deletes the results and reopens the property sheet (§2.8) |
| Save | 8018 | FUN_180080b50 L86357 | FUN_180044380 (Save As the session .nstx) between KillTimer / SetTimer of the preview timer - ported |
| Copy / Copy Blank | 40032 / 40033 | FUN_18007e290 L84825 | re-runs ns_output on the stored CNST_Result with / without parts, inserted after its siblings; a modified result cannot be copied (13325-13327) |
| Delete | 40031 | FUN_18007e660 L84975 | see **Results Delete** below (ported 2026-11-05; enabled while the result has PPIs, FUN_180081d00 L87117-87124) |
| **Sort** | 9233 | FUN_180080f00 L86517 | operation ordering (13309, Placement dialog) = toolpath; whether it does anything in mode 2 is NOT DECODED |
| **Customize...** | 1102 | FUN_18007f9e0 L85656 | re-nests only this result: makes a CSheet from it, turns off group page / default sheet / auto origins, reopens CNestPropSht, then FUN_18003fb40(1); a single new result replaces the old one |
| Preview | 8019 | FUN_180080c50 | zoom view (CZoomDlg, template 211 [inferred]) |
| **Drag** | 1101 | FUN_18007e730 L85006 | Bump Nesting |

**Results Delete** (2026-11-05 decode; NOT PORTED, greyed). FUN_18007e660 L84975: KillTimer, then on the selected result
FUN_180089ed0 L92111 = FUN_180089890 L91845 (an unused level from FUN_1800c90d0(10000) into +0xb0; save_vis_level; main level =
it; the graphics view = the active plane; all levels hidden but that one) + FUN_180089bb0 L91995 (every entity of the result
moved to that level and redrawn: the trim lines and the result's group entities (+0x670) in the group colour with sel bit 2,
the rest in the result colour with sel bit 8; fit). Then FUN_180087fb0 L90774 (param_4 0 from the Results button): with no
placed-part instances (DAT_180140d60 < 1) nothing; geometry mode (DAT_180135a9b = 0) prompts string 0x340a and loops on
get_event(mask 0x122): a picked entity (event 2) / a point (4, FUN_1800b42d0) / a window (0x20, every entity) gives the placed
instance (PPI, DAT_180140d58[]: FUN_1800b4220 L123460 finds the one whose entity list +0x1e0 holds it); the indices are sorted
and grouped by the PPI's +0xa4 / +0xac (FUN_1800891f0 L91505 collects every PPI of this result +0x8a0 = PPI +0xb4 with the
same pair), each one deleted by FUN_1800b0500 L120440 ("DeletePPI": its entities deleted FUN_180042f50, PPI removed) and the
result marked modified (+0x881 = 1, +0x660 = 0 - so Copy refuses it, 13325-13327) and its part's count +0x250 decremented
(FUN_180087450 L90270); the loop ends when FUN_180086d20 L89932 (recomputes the result's nested size / area) finds no
instance left. Then FUN_180089860 (entities back to their own level / colour, FUN_180089a30; main level + view restored,
FUN_1800899a0), FUN_180081d00 redraws the dialog, SetTimer. Prompt 0x340a = 13322 "Select part profile to delete..." (0x3409 =
13321 "Select operation profile to delete...", toolpath mode).
- **PPI construction** (2026-11-05): CPart::Place FUN_18005a260 L61819 (called per instance from ns_output, L6121-6130) makes
  one PPI per placed instance and appends it to DAT_180140d50 (L63065, count DAT_180140d60): +0xa4 = the part's id (part
  +0x230), +0xac = part +0x4c4, +0x34 = part +0x4c8 (the element number parsed from the engine's part name, L5948-5950).
  ns_output sets part +0x4c4 = 0 for every part-type entry of the result (L5952) and increments it before each instance
  (L6121): +0xac = the instance's ordinal (1..n) within its part-type entry. So FUN_1800891f0's (+0xb4 result, +0xa4, +0xac)
  match = the instances of the same part with the same ordinal in the result's entries - one per entry of that part (an
  Optimized-pair cluster's elements are separate entries of the same part, so a pair is deleted together).
- **Helpers**: FUN_1800b0850 L120560 removes the instance from the stored result (FUN_1800b1880 finds result / entry /
  instance; the instance record dropped from the entry's list +0x60; entries left without instances dropped from +0x858);
  FUN_1800b8010 L126381 drops the entity from the bump structure's list (+0x82f8, filled only by Bump Nesting / toolpath
  mode); FUN_1800b2180 L121818 frees the PPI's cached chain (+0x28 when +0x68); FUN_1800354d0 L36858 releases an id in the
  label table only when DAT_180135292 (labels) is on. FUN_180086d20 L89932 (recount): the result's extent +0x698..+0x6c0 =
  the min / max of EntityMinMax over every remaining instance entity (FUN_1800891f0 with this result), the sheet's extent
  (FUN_18008c160) when none is left; then FUN_180085c80 recomputes the trim lines (+0x590 / +0x598, rewritten or stored,
  blanked by +0x5a4 / +0x5a0); returns the number of remaining instances.
- **FUN_1800b1880 L121400** (2026-11-05, the instance lookup of FUN_1800b0850; its param = PPI + 8): the result whose +0x718
  (sheet id) / +0x71c (the sheet's output count) equal PPI +0xb8 / +0xbc, in it the entry of +0x858 (the CXML_Part records
  ns_output appends, L6262: +0x28 part id, +0x60 the CXML_Instance list) whose +0x28 = PPI +0xa4, in that entry the
  instance whose +8 = PPI +0x120 (the instance number uVar30 = local_6a38 + FUN_180058440(part), L6122-6125, also
  CXML_Instance +8, L6201). So FUN_1800b0850 edits the **report records** (+0x858), not the CNST_Result (+0x888, kept for
  Copy, which refuses a modified result anyway).
- **Other data Delete touches**: FUN_180087450 lowers +0x250 of the result's own CPart copy (+0x568, FUN_180085bb0 L89302:
  one copy per part, made after the part's first CNST_Part entry, L6238-6250, with +0x250 = that entry's instance count);
  in Customize mode (DAT_1801352b8 != 0) the session part's +0x250 instead.
- **Where the shown result's entities come from** (FUN_18007d8d0 L84415, run when the selection changes): the list
  0x180138030 (CResultUtils +0x80) = the result's +0x670 (every entity the sheet output stored: FUN_18008d820 param_3,
  L94436-94441), then per part id in rising order (FUN_1800c8980) the entities of the PPIs whose +0xb4 equals the dialog's
  **selection index** +0xb198 (not the result's +0x8a0 - they differ after a Copy inserted a result before the end) and,
  when result +0x668 >= 0 (only the preview's part click sets it, L84802; ctor -1), only that part; then the active trim lines.
  FUN_180089bb0 moves exactly this list.
- **Trim lines** (2026-11-05). **FUN_18008c160 L93638** (the sheet extent): for the first contour of type 0 (sheet+0x298 list;
  an L x W rectangle FUN_1800c3180 added for good when there is none) a line (0, 0, depth) - (L, W, depth) in the contour's
  view (+0x44), through CSheet::XformEnt FUN_180091b90 (param_5 0 - not a chain copy - and the result origin
  result+0x758): its end points (ent +0x68 / +0x80) are the extent box; param_3 = 1 when it worked. **FUN_180087530 L90332**
  grows the result extent +0x698 (min) / +0x6b0 (max) by a box. **FUN_180085c80 L89333** (result, L, W, part area, box valid,
  box, &h, h line, &v, v line), in the active view (geometry mode: GetActiveViewNo; toolpath mode 1); c = the collar
  DAT_180136e58, k = the clearance FUN_1800cad50, corner = STARTINGCORNER P+0xb0 (1 LL, 2 LR, 3 UR, 4 UL):
  - horizontal: y = max.y + k + c for corners 1 / 2, min.y - c - k for 3 / 4 (c < 0: the extent edge itself) -> +0x7d0
    (asm: `param_1 + 2000`); valid only with a sheet box and when the line saves more than 0.1 k (1 / 2: y + 0.1 k < box max
    y; 3 / 4: box min y < y - 0.1 k); trimmed height = | |y| - |box min y| | (1 / 2) or | |y| - |box max y| | (3 / 4), the
    offcut +0x650 the other one. The line runs across the box at y (from min x to max x for corners 1 / 4, the other way for
    2 / 3), made by FUN_180006e70 L4855 (a type-2 line ent, main colour / level, attributes DAT_1801305e0: width 1.0, then
    ent +0x18 line style = 2); +0x5a8 = its midpoint with y + 0.1 (1 / 2) or - 0.1 (3 / 4). +0x7e0 = valid; then +0x7e8 = L,
    +0x7f0 = trimmed height, +0x7f8 = their product, +0x800 = part area / it x 100 when part area > tol.
  - vertical: x = max.x + k + c for corners 1 / 4, min.x - c - k for 2 / 3 -> +0x7d8; valid when x + 0.1 k < box max x (1 / 4)
    or box min x < x - 0.1 k (2 / 3); trimmed width +0x810, the offcut +0x658; the line from the box's min y to max y (corners
    1 / 2), else downwards; +0x5c0 midpoint x +- 0.1. +0x808 valid; +0x810 = trimmed width, +0x818 = W, +0x820 = the area,
    +0x828 = utilisation.
  - both valid: +0x830 = trimmed width, +0x838 = trimmed height, +0x840 = area, +0x848 = utilisation; the corner points
    +0x5d8 / +0x5f0 / +0x608 / +0x620 / +0x638 (the trimmed rectangle; "(CalcTrimLines) (m_TrimPointAtIntersection does not
    equal m_iPt)" is a debug message only). Geometry mode then turns +0x7d0 / +0x7d8 into the distances of the stored lines
    from the box's min y / min x (|line y - box min y|, |line x - box min x|).
  - ns_output (L6263-6310, asm @180009011-18000910c): FUN_180085c80(result, +0x140 L, +0x148 W, +0x158 part area, the box of
    the sheet output, ..), each line stored by `store_ent(ent, &eptr, 1, sheet colour, sheet level, ..)` (asm: r8d = 1,
    r9b = the sheet colour local_6a70, [rsp+0x20] = the sheet level local_6a6c) into +0x590 / +0x598 and **blanked** unless
    +0x5a4 / +0x5a0. +0x5a4 / +0x5a0 = the sheet's per-output byte arrays +0x2f0 / +0x318 at index outputs - 1 when present,
    else TrimActive (P+0xc = DAT_18013784c, [PARAMETERS] TRIMACTIVE, shipped NO). A blank result (Copy Blank) only takes the
    sheet box as its extent and +0x5a0 = 0.
  - The result extent during the output: ctor 1e38 / -1e38, grown per placed instance by FUN_180087530 with the box CPart::Place
    returns (local_68b0 = Place's param_9, L62817-62822): part +0x520..+0x548 set to 1e38 / -1e38 at Place's start (L62101-62106),
    grown by FUN_18005f810 (L65593-65680: EntityMinMax of the entity, min / max per axis) for every entity XformEnt made (L62705);
    *param_8 = 1 then (L62823) - the union of the placed instance's entity boxes.
  - Results dialog: FUN_180081d00 enables 9268 / 9275 when +0x7e0 / +0x808 and shows +0x5a4 / +0x5a0; the handlers store the
    box (L86318 / L86371); FUN_18007d8d0 blanks / unblanks the lines per +0x5a4 / +0x5a0 and lists the active ones. OK:
    FUN_1800868c0(result, 0) per result (L42985) deletes a line that is not valid or not active (unblanked first); L42709 /
    L86040 delete them all (param 1).
  - The per-output arrays sheet+0x2f0 / +0x318 (CByteArray objects at +0x2e8 / +0x310): filled only by FUN_180094550 L99324
    (SetAtGrow of the MCLink job records' bytes +0x114 / +0x115, L99731-99739), called from FUN_18003bbc0 L41364 on the MCLink
    message 0x6c item 0x1f - an external program's job. Interactively they stay empty: +0x5a4 / +0x5a0 = TrimActive. The CResult ctor
    FUN_1800853e0 zeroes +0x5a0..+0x5a7 (one 8-byte store), which a blank result keeps (L5881-5885).
  - Ported 2026-11-05 (cui_nest.c "the trims": res_LW, ent_minmax, ext_grow = FUN_180087530, sheet_box = FUN_18008c160, calc_trims =
    FUN_180085c80 L89333-89690 read in full, trim_line, trim_show = FUN_18007d8d0's part, trim_delete = FUN_1800868c0 L89733,
    trims_recount = FUN_180086d20's trim step L90150-90215): the lines are made in ns_output after the parts, stored with the sheet's
    colour / level, line style 2, blanked unless active; the check boxes 9268 / 9275 (enabled when the line is valid) toggle
    +0x5a4 / +0x5a0 and blank / unblank the shown result's lines; the fields 8007 / 8011 / 8015 show the trimmed values
    (FUN_180084d30 L88805-88830; asm: three jumps to the shared tail 0x18008501c - both / vertical / horizontal); OK deletes the lines
    not valid or not active; the Delete recount rewrites a line in place (it keeps the isolation's level / colour) or stores a new
    one; the Delete isolation lists the active lines in the group style (FUN_18007d8d0 L84455-84470); the report's HORIZ- /
    VERT-CUTOFF are +0x7d0 / +0x7d8. Blanked wireframe (mcam_load.h mcam_ent_blank): Ent.blanked + Poly.blanked, skipped by draw
    and picks through the level filter (GEOM_SHOWN / ENT_LV_SHOWN; draw_ent decomp/surfdraw/drawent.c 52-54, select_ent_univ
    decomp/mastercam_seluniv.c 360-368), written as sel bit 0x100 (mcam_saveops.c, as the surfaces) and read as before. Scene
    xf_nest_trim (step nesttrim, ours). Not ported: the trimmed rectangle's corner points +0x5d8..+0x638 (stored, no reader in the
    geometry flow found), the midpoints +0x5a8 / +0x5c0 (their readers: NOT DECODED), the
    Rectangular back-end's part area +0x158 (0 here: the trimmed utilisations show 0 in rectangular mode - the Rectangular
    callback FUN_180005f90's +0x158: NOT DECODED for this port), EntityMinMax of a view arc / spline (the nest output has neither).
- **Port (2026-11-05)**: Results Delete is ported (cui_nest.c "Results Delete (40031)", scenes xf_nest_delete /
  xf_nest_delete_cancel). Model: NsResult.rid (+0x8a0), .sheet_to (+0x670 as an entity range), .modified (+0x881), .pq (the
  part copies' +0x250); NsPPI {rid, part id, ordinal, element, entity range}; NestOutInst.entry = the CNST_Part entry (TrueShape:
  the part type i; Rectangular: FUN_1800056d0's numbering), which ns_output now walks in order (it walked first appearances of
  the part before - the same order unless one part has several entries: Optimized-pair elements). Not ported with it: the trim
  recount (ported since: the trims above), the report records (FUN_1800b0850). Undo: an instance entity the run made is switched off
  without being recorded as an original (cui_nest.c; cui_undo.c's redo leaves a recorded type-0 entity off), so Cancel / Redo /
  Ctrl+Z remove it with the run and Ctrl+Y does not bring it back.
- **Port, decision NS-2 (2026-11-05)**:
  - **Isolation by level visibility.** FUN_180089890 hides every level but the free one (LevelData::setAll(0) + setLevel(+0xb0,
    1, 1)); the program now draws and picks as Mastercam does by level: LevelData::isVisible @180245fc0 (the node's flag, 0 for a
    level not in the list; decomp/mastercam_hidezoom.c 913-926) gates draw_ent @1803312e0 (decomp/surfdraw/drawent.c 116 / 144,
    also the display-list store), surfaces (FUN_18021e020, decomp/surfdraw/srfdraw.c 16) and drafting / notes
    (decomp/draft/core_draft_rec.c 1324); the picks (select_ent_univ FUN_18020f4e0, decomp/mastercam_seluniv.c 300-353), the window
    selection (decomp/xform/mastercam_dbselwin.c 317-330), AutoCursor (dl_search_fast, decomp/mastercam_dlsearch.c 132) and the
    fit (FUN_180320770, decomp/mastercam_fit2.c 455-470) walk the display list, so only visible-level entities. Port: viewer/mcam_load.c
    mcam_level_visible; scene.c (polylines, wide lines, points, surfaces, the fit, the mean display depth), draft_note.c (notes),
    cui_overlay.c (dashed entities), cui_sel.c (pick, nearest point, window, polygon), cui_note.c, cui_snap.c (AutoCursor), cui_chain.c
    (vector / point chaining), cui_draft*.c (the dimension picks). Solids are drawn whatever their level: draw_ent's solid branch
    (0x10000) does not ask isVisible and sld_draw_solid's own test is NOT DECODED. Select All (sel_all) keeps its rule (its level
    test is NOT DECODED). The stopgap (entities taken off the screen) is gone.
  - **The graphics view.** FUN_180089890 (L91870-91873) and FUN_1800899a0 (L91945-91948) both call gview_proc(GetActiveViewNo(Planes,
    0)) = the active Cplane's view (PlaneType 0 = Cplane, formats/bbox_toolplane.md); the view saved in +0xa8 (get_gr_view_n, L91869)
    is never read back. So Mastercam does change the user's view, to the Cplane's - Top whenever the Cplane is Top (this program's
    Cplane stays Top, cui_planes.c). Ported so (cui_nest.c gview_cplane: the Cplane's matrix, plane_active[0]); the earlier comment
    "Top because the nesting takes Top chains" was wrong. Settled: NS-2's text corrected (the Mastercam way is
    ported).
  - **The result's id (ours, approved NS-2).** The isolated list takes the PPIs whose +0xb4 is the selected result's +0x8a0, not
    the dialog's selection index +0xb198 (FUN_18007d8d0 L84498-84530); the pick loop deletes by +0x8a0 anyway (FUN_1800891f0). Scene
    xf_nest_delete_copy (a Copy inserted before the end: the selected copy's 6 instances are isolated, not the 3 of the result the
    index names).
  - **The pick's sel bits.** get_event(prompt, ev, 0x122, 0x1f1fff, 8, &flag) (L90889): the 5th argument is the sel-bit mask an
    entity must hold (select_ent_univ local_5884 = param_7, decomp/mastercam_seluniv.c 290 / 353; MCEvent::FilterEptrArray for a
    window, decomp/mccore_getevent.c 189-191). 8 = the Result-coloured instance entities of FUN_180089bb0, so the sheet's entities
    (mark 2) are neither picked nor windowed (cui_sel.c pick_req_bits).
  - **The window event 0x20.** mccore's button-up handler FUN_1804e3b90 (decomp/getevent/mccore_getevent_msgs.c 614-659) with mask
    0x20 / 0x10 and a dragged rubber band calls FUN_1804e6520 (decomp/getevent/mccore_getevent_win.c 41-56): mark_window_sm(corners,
    0x400, current selection mask, GetWindowSelectMode()), SetupWindowEvent + FillWithBITS(0x400). GetWindowSelectMode (mastercam.dll
    @180263c10, decomp/getevent/mastercam_winselmode.c) maps the GraphicsOverlayManager's SelectionContainmentBy (+0x90, mccore
    FUN_180b3be50 / FUN_180b3e860, decomp/getevent/mccore_overlaymgr.c 122-145) 0 -> 1, 1 -> 5, 2 -> 2, 3 -> 4, 4 -> 3; the
    manager's ctor FUN_180b356d0 zeroes it (mccore_overlaymgr.c 376) and outside chaining only the General Selection panel (not
    ported) changes it - the Chaining dialog puts its previous value back (decomp/mccore_chain_range.c 16323 / 16773). So the
    window is Inside (mode 1). The loop then takes each windowed entity's PPI once (L90960-90985); the PPIs are bubble-sorted and
    their (+0xa4, +0xac) pairs deleted once each (L90990-91090), as for a pick. Port: cui_nest.c ns_del_window / ns_del_ppis; the Qt
    view draws the window while the left button is dragged in the Delete loop (viewwidget.cpp nsWin; the drag / click threshold is
    the program's < 4 px test, Mastercam's NOT DECODED). Scene xf_nest_delete_win (step nestdelwin:X0,Y0,X1,Y1, ours).
  - Docs / decompiles of this pass: decomp/getevent/ (mccore get_event internals, the overlay manager, mastercam.dll
    Get/SetWindowSelectMode and KeyPress), decomp/nest/mastercam_chainview.c (get_view_from_chain / chain_in_view / FUN_1801aee30).

**Details** (decoded and ported 2026-11-05, decision NS-2; cui_nest.c ns_details): FUN_180080a20 L86299 = KillTimer;
FUN_180084380 L88353 (FUN_1800ccf40 the images, FUN_1800deb10 Nesting.xml; when `<xml folder>Nesting.xml` exists:
SetNestingReportsInRegistry + `RunActiveReportsViewer` on it); only when that returns false FUN_180084440: the text report
FUN_1800dd340 L150938 then `run_command("notepad " + its path)`; SetTimer.
- mccore SetNestingReportsInRegistry @1803c9c30: `<reports key>\NESTING` value `SHT-Source` = report_nesting, whose config default
  is "DEFAULT NESTING REPORT" in folder NST (decomp/toolmgr/tm2/a.c 4166, FUN_1801c6000(report_nesting, "NST", ..); its
  configuration key: NOT DECODED, the default is used). RunActiveReportsViewer @180641790 runs `<shared>\COMMON\REPORTS\`
  activereports_viewer.exe on the XML (missing -> message 0x15e5, false -> the notepad fallback). Decompile:
  decomp/getevent/mccore_nestreport.c.
- The viewer (activereports_viewer.exe, .NET; ilspycmd output ~/mastercam-re/dotnet/activereports_viewer): the template is
  Add_Prefix_Suffix(lower(SHT-Source), the reports folder, ".rpx") (MainForm.cs 1612-1640); missing -> FileNotFound "NAME\n(file
  not found)", caption "Warning" (MainForm.cs 503-507, 512-513). Images: a section with IMAGE_FILENAME (TextBox), IMAGE_HOLDER
  (Picture) and IMAGE_NOT_FOUND (Label): the file named by IMAGE_FILENAME (".BMP" added without an extension) is loaded into the
  holder and the label hidden; no file -> the label stays (MainForm.cs 760-970).
- The template (Mastercam's files: NST\Default Nesting Report.rpx and -Default Nesting Report (SR1) / (SR2) / (SR3).rpx): data
  source //NESTING/SHEETRESULT/SHEETS; Detail = Subreport SHEET -> SR1 (per SHEET: Data_NAME (a Label bound to NAME, bottom
  border), two AR.Shape boxes, the result image holder, "SHEET INFORMATION", Material (SHEET-MATERIAL), Grain Direction (GRAIN),
  Origin (ORIGIN), Dimension (SIZE), Nested Dimension (NESTED-DIMENSION), Total Area (AREA), Nested Area (CUT-AREA), Utilization
  (UTILIZATION), Horizontal / Vertical Cutoff (HORIZ- / VERT-CUTOFF), "PART INFORMATION", Subreport PARTS -> SR2 -> Subreport
  PART -> SR3 (Name PART-NAME, Quantity Nested PART-NESTED, Label PART-LABEL, Material PART-MATERIAL, the part image holder); the
  "Nesting Report" title label and an embedded picture (Picture1: the element's text is hex - a 9-byte header 0D 0D 00 04 02 + the
  u32 length 7605 of the rest - then a 300 x 52 PNG; SizeMode 2 = zoom). The scripts only add bookmarks (the viewer's tree).
- The values (FUN_1800deb10 L151600 / FUN_1800dc500 L150366 / FUN_1800da5e0 L149420 / FUN_1800dc110 L150204 / FUN_1800daf30
  L149649): a SHEETRESULT per sheet id (+0x1d8) in the order of the first unwritten result (+0x891), QUANTITY its results; a SHEET
  per result from the record CResult +0x710, filled by ns_output L6318-6370: ID +0x718 (sheet id), INSTANCE +0x71c (sheet +0x344),
  NUMBER = index + 1, NAME +0x730 (the output name, upper-cased), REFERENCE +0x738 (the engine sheet name), COPIES +0x720, GRAIN
  +0x748 (P+0xd8), SPACING +0x750 (P+0x38), ORIGIN +0x758 "x, y, z", DIMENSION +0x780 (the engine sheet), SIZE +0x790 (DIMENSION -
  2|c| when c < tol), AREA +0x7a0, CUT-AREA +0x7a8, NESTED-DIMENSION +0x7b0, OFFSET +0x7c0 = 0, UTILIZATION +0x7c8, the cutoffs
  +0x7d0 / +0x7d8 (the trims), SHEET-MATERIAL +0x740 (sheet +0x210); doubles make_rstr(v, 5), "%s x %s" pairs, an empty string ->
  13424 "Unknown" (FUN_1800dae10). PARTS: per part id in rising order the PPIs whose +0xb4 is the SHEET's **index** in the results
  (FUN_1800dc110's param_3 = uVar6, L150418-150424) with element +0x34 < 2; one or more -> a PART: PART-NAME part +8, PART-LABEL
  part +0x108 (NameAsLabel: get_just_name of the name), PART-ID +0x230, PART-NESTED the count, PART-MATERIAL 13424 "Unknown".
- Port: rpx.c reads AR.Shape (its outline), AR.Image (an embedded picture drawn, aspect kept for SizeMode 2), label borders (BorderXStyle -> a styled line), Visible="0" and labels bound
  to a DataField; the report opens in this program's report window (core_ui tlr). Scene xf_nest_details (step tlrdump).
  Not done (greyed / NOT DECODED): the images (FUN_1800ccf40 draws through mastercam.dll FEDC_Bitmap_Setup: NOT DECODED - no
  image, so the template's "IMAGE NOT AVAILABLE" label shows, as the viewer shows it without a file), Nesting.xml is not written to the XML folder (the tree is built in memory with the
  tags the template binds), the viewer's
  bookmark tree / toolbar / zoom / print, AR.Shape's ~10-twip corner rounding (square corners).
  Ours, approved NS-2: PARTS takes the PPIs by the result's id (+0x8a0), not the SHEET's index as FUN_1800dc110 does (after a Copy
  inserted a result before the end the index lists another result's parts).

**Customize** (2026-11-05 read in part, NOT PORTED): FUN_18007f9e0 L85656 (645 lines) - a CSheet from the result
(FUN_180089340), DAT_1801352b8 = the result, the live CParam block replaced by the result's copy (+8) with +0x90 / +0x98 /
+0xd4 cleared (group page, default sheet, auto origins), the session's results / sheets / groups / parts moved aside into
local arrays and the result's parts made the session's (FUN_180086c10), PPI part ids remapped (L85818-85860), 10016 (no sheet) / 10017 (no part, FUN_180038e70)
when there is no sheet / part, the property sheet reopened (title 10130, page 1), on OK FUN_18003fb40(1) and, with exactly one
new result, it replaces the old one (+0x8a0 = the selection, +0x881 = 1, L85966-86000); the rest (the arrays put back, PPI
renumbering, FUN_180089340 / FUN_180086c10 bodies) NOT READ.

**Bump Nesting** (CSktchBarNestDrag, template 1621): each button posts message 0x802b with a key (Sheet 'S', Drag 'G',
Delete 'D', Copy 'C', Move 'M', Rotate '+' / '-', Mirror 'R', Display holes 'H', Apply 'P', OK 'E'). Loop FUN_180081f80
(L87420-87600): '-' (also VK 0x6d / 0xbd) rotates by +step, '+' by -step; with drag+0x468 the step is 1 deg [inferred Ctrl];
angle wrapped to 0..360; the outline is a pocket boundary (FUN_180055af0). Collision / edge-snap ("tuning", 11011-11020)
maths: NOT DECODED.

## 9. Reports
- Images (FUN_1800ccf40 L141478): S%d.bmp / P%d.bmp / R%d.bmp.
- **Nesting.xml** (FUN_1800deb10 L151604) in the XML folder. Tree:
  - `NESTING`: VERSION, SOURCE=NESTING, SHEETRESULT;
  - `SHEETRESULT`: QUANTITY, PROCESS-DATE / TIME, HEADING-1..5, ...; SHEETS;
  - `SHEETS/SHEET`: INSTANCE, IMAGE, DIMENSION, CUT-AREA, NESTED-DIMENSION, UTILIZATION, the trim tags, MATL-*;
    PARTS / LABELS with PART-NAME, -LABEL, -QUANTITY, -NESTED, -LENGTH / -WIDTH, -ROTATE, ...
  - Writers: FUN_1800dc500 / FUN_1800da5e0 / FUN_1800dbe90 / FUN_1800daf30.
  - Which value fills each tag: decoded for the SHEET / PART tags the Default Nesting Report binds (§8 Details); the others NOT DECODED.
- **Text report** `<data>\nesting.out` or `<session>.out` (FUN_1800dd340 L150938): strings 12001-12081 and 12211-12244
  ([PARAMETERS], [SHEET INFO], [PART INFO], [QUANTITIES], "MINIMUM QUANTITY NOT ACHIEVED").
- Message log 10121 / 10122: MCLink only. NESTING.DLL reads and writes no .nop.

## 10. Corrections to earlier notes
- wireframe.md 2026-11-04: the geometry Parts page is template **105** (107 = toolpath); the Minimum Qty / Filler / Step /
  Priority / Label edits are 4011 / 4013 / 4015 / 4017 / 4033 (the listed ids were the static labels); the nested copies
  are written **during the run** (OK commits the undo transaction, Cancel / Redo delete them), not after Results OK;
  Mirror offers 2 choices in the UI; Sheet Margin = SHEETTOPARTDIST and the engine collar = margin - part distance.
- nesting_engine.md: FUN_180035970 is a path builder, not the .nip writer (§6.7). (The NAddPartInfo argument-4 correction
  is already in that file.)

## 11. NOT DECODED
GetApp path id 3; legacy binary .nst/.nsl/.npl; material dialog, origin Select 3015, sheet preview 3023, group-menu handler
bodies; Ignore holes for sheets (13010); cluster-add handler FUN_180074fb0; notes scan / auto-attach internals; the full
angle mapping of FUN_1800c4be0 / FUN_1800ccbf0; NAddSheetInfo args 15 / 17 / 18 and NStartGroup arg 2 (+0x10c);
NInqNestedInstance d5 / d6, CUT_AREA meaning, rectangular utilisation `DAT_180134638`; Sort in mode 2, the 2-second timer,
Delete-instance and bump collision / snap internals; the label placement (structure decoded in §12: get_ext + a TrueShape run per part; the callbacks FUN_18000a290 /
FUN_18000a330 / FUN_180009ed0 not read), part level +0x4d0, UseMainColor /
UseMainLevel UI (none found); XML tag values, HighResImages, scrap geometry details, the solids path (FUN_180063130 /
FUN_1800638e0); FUN_1800cc620 / FUN_18003cc30 on rebuild.

## 12. Port (2026-11-05): viewer/core_ui/cui_nest.c + viewer-qt/src/nestdlg.cpp
Transform > Geometry Nesting (button 53673, FT do_nest_g) drives the Rectangular engine port (viewer/nest/nest_rect.c) through
the back-end interface viewer/nest/nest_job.h (NestJob / NestOut, NestEngineFn; the Rectangular back-end is
viewer/nest/nest_rect_job.c; TrueShape plugs in by defining `nest_engine_ts` (a weak reference until nest_ts.c defines it) - a TrueShape run
is refused with a message, ours). Scenes: tools/tests/wf_scenes.txt xf_nest_* (steps `nest`, `nestdump`, `nestset:ID=V`,
`nestdlg`, ours). Ours: defaults = the shipped .defx text (cui_nest_defx.inc), TrueShape default from TRUESHAPE=YES (licence
dropped); the bitmap buttons show a letter; previews are empty frames; list boxes single-select; CSkRCmb number format =
FUN_1800c9fb0's (the combo's own NOT DECODED); a run that could place > 10000 rectangles on one sheet is refused (the engine
port exits there). Not ported: groups, clusters, solids, files / libraries / .nstx / .defx writing, Material, origin Select,
New / Open / Save / Restore, results Details / Save / Copy / Delete / Sort / Customize / Drag / trims (Save, Copy and Delete ported since), scrap, reports, labels
and the sheet-name note (no notes in this program), level naming on OK (FUN_1800414f0), chains in views other than Top (ported
since: "Chains in any view" below).

**Status (2026-11-05, later; worktree branch, commits 62cba2c / 64011c4 / 5b8bb77).** The TrueShape back-end is
viewer/nest/nest_ts_job.c (the paragraph above predates it). Ported since:
- **Optimized pairs** (TrueShape AutoPairs): NestJobPart.pair = FUN_180037400 L38267-38275 - AutoPairs (P+0x5c), TrueShape,
  the part not sent as a rectangle (NAddPartInfo arg 4), FUN_180058910(part, 0) == 0 (not a rectangle / square: every code-0
  contour only 'L' records, axis-parallel on the bbox FUN_180056c30 / FUN_1800c5260; 2 = square when every edge is the part's
  L / W; CLUSTERMETHOD 4 -> 0; no contours -> 1), FUN_180058650 == 0 (not a circle: the first arc record of the outer contours
  has the circle flag +0x48 and every arc shares its centre / radius within tol, no line after a contour's start record) and
  max != 1. After the part's NAddPartInfo: `NStartCluster("%dc%da" (FUN_180055ad0, id twice), 7, 1, 0, commonTool)`,
  `NAddClusterElement("%dp", 2, 0.0, 0.0, 0x4076800000000000 = 360.0 as raw bits)`, `NEndCluster` (L38278-38307; a failed
  element call still ends the cluster after the loop, L38318-38320). Engine part names come back through ns_parse_prtname
  FUN_180009850 L6564 (lower-cased; "<id>p" type 0, "<id>p<e>u|c" type 2 element e, "c<id>u" type 1, "c<id>a" type 0; the id
  then GetPartPtr FUN_180039750) in the sheet-result callback (FUN_180005f90 L4471-4509: element -> CNST_Inst +0x28, < 0 -> 0)
  and the part-geometry callback (FUN_180006f90 L4934). The engine port needed 69 more functions (coverage of the pair cases)
  and the bracketing of six right-nested y sums in FUN_1800cda00 (cluster element placement, asm 0x1800cdde3 .. 0x1800cf376).
  Oracle: nest_ts_job_oracle.sh families p (fixed) / z (random, pairs) - 93 / 153 / 153 / 207 bit-identical (seeds 1, 7, 11, 5).
- **Sheet-name note** of every output sheet: FUN_18008d820 L94890-94916 appends, after the outer contour's entities, an entity
  at (sheet+0x360, sheet+0x368) = (0, width + clearance / 10) (+0x360 = 0 from the ctor FUN_180089fa0 L92181; +0x368 set for
  every sheet at the run start by FUN_180041d00 L45676-45682) when the output name sheet+0x10c (10161 "%s - Result %d") is not
  empty; CSheet::XformEnt FUN_180091b90 L97806-97822 makes it calc_note(name) with height (SheetToSheetDist - clearance * 0.02)
  * 0.8 (1.0 below tol), +0xd0 (extra spacing) 0.1, +0xa8 / +0xb0 / +0xb8 (angle / slant / rotation) 0, +0xf4 font 0, +0xf6 0,
  +0xf0 = low byte | 0xc00 (Left, Base, path Right), +0xf2 = & 0xfff3 | 1; colour / level = the sheet's (L94720-94770). Through
  draft_note.h mcam_note_create.
- **CycleUnused**: FUN_1800c9a00 L138968 tests the database-levels list (FUN_1800c9df0 L139170: every entity's level, once),
  filled at the run start only (FUN_1800413e0 L45197-45201, CycleLevels + CycleUnused, called at L42580) - not the entities the
  run makes. (FUN_1800413e0 also sets sheet+0x358 / part+0x4d0; no reader of those in the geometry path was found.)
  The per-run fields live in the sheets / parts as in Mastercam (sheet+0x344 outputs, sheet+0x354 / part+0x4cc colours;
  no fixed-size tables).
- **Engine errors** (TrueShape, formats/nesting_engine.md §9): entities the engine refuses are reported with NestLib's own
  message (the error callback FUN_180006cf0 keeps the last one) and the run goes on with the other parts, as the DLL's catch
  blocks do ("Line is too small. ( x, y ) to ( x, y ).", "Arc is too small. ...", "Part : 2p. Can not get the part
  drawing."). A port failure (NOT PORTED) stops only that run, with the function's name; the engine is reloaded for the next.
- **Results Copy / Copy Blank** (40032 / 40033, FUN_18007e290 L84825): ns_output again on the stored CNST_Result with / without
  parts (copy count forced 1); a blank result copies blank; the new result goes after the last same-sheet result following the
  selection (cui_nest.c ns_results_copy). The modified-result refusal (13325-13327) cannot occur (Delete / Drag not ported).

**Chains in any view** (2026-11-05, decision NS-2 task: the Top-only limit was ours). NESTING.DLL takes chains in any one view:
- FUN_1800c9150 L138637 (from FUN_1800c4d20 L135817, param_2 0): per chain that is not a point chain (FUN_1800c4850 L135557: one node,
  a point entity) mastercam.dll get_view_from_chain @1801af9d0 (decomp/nest/mastercam_chainview.c 48-450): the first view 1..n_views
  the chain is flat in - FUN_1801aee30 (chainview.c 452-626: points / lines at one view z within tol, arcs in a parallel view with
  their centre at that z; splines through FUN_1801ae690 / make_view_k: NOT PORTED) - with its depth; flat in none: the plane from
  the geometry (an arc's view, two lines' plane through Plane::GetMatrixFrom2lines -> Matrix33::GetViewNo(.., true), which can add
  a view): NOT PORTED (AddMCView), refused with a message (ours). The view then becomes the lowest-numbered parallel one, the depth
  negated for an antiparallel view (L138700-138716); every chain must give the same view, else -10062 "Warning - all chains must
  lie in same view" (the first chain's depth kept; a chain the view cannot be read from keeps the view so far, L138697-138698);
  |depth| <= tol -> 0. Only point chains: view 1 and the point's Top z (L138765-138780).
- chains_to_pock_bounds runs under ActivePlaneSaveRestore(mcViews[view]) (L135820-135836): the records are in the view's
  coordinates (in-plane x / y, depth z); the contour's +0x44 = the view, +0x48 the depth. An arc's CW flag also flips in a view
  antiparallel to its own (the [inferred] CW rule of §5, extended the same way).
- Output: CPart::XformEnt FUN_1800621b0 (L67400-67560) / CSheet::XformEnt FUN_180091b90 (L97640-97700) first xform_ent the copy by
  mcViews[1] x mcViews[view] with a translation that keeps the base point (it cancels): the copy lands in Top at its view
  coordinates (arcs: centre / start in the view, the sweep negated in an antiparallel view, as xform_ent's XfStdView leaves a Top
  arc); then the in-plane steps (§7); then Top -> GetActiveViewNo(Planes, 0) (L67340-67345 / L97610, L67640-67700). ctour_to_ents
  (decomp/nest/mastercam_ctour.c 69 / 194-196) writes a non-chained contour through view_to_world(its view), so the same steps leave
  its records in Top.
- Port: cui_nest.c chains_view / chain_view_get / ent_view_z, bound_make(.., view rows), ctours_build (.view), xf_copy(.., view);
  view helpers mc_view_rows / mc_view_count / mc_views_parallel (cui_xform.c). Ours: the output in a Cplane other than Top (a
  project's ACTPLANE) is NOT PORTED - the run is refused with a message. The geometry chaining of this program does not chain arcs
  outside the Cplane (cui_chain.c cam_chain_views is for operations only, an earlier "ours"), so a non-Top part has lines only
  until that is decoded. Scenes xf_nest_front (a Front-plane outline; its instances equal xf_nest_front_topref's, the same outline
  drawn in Top) and xf_nest_mixedviews (Top + Front chains: 10062).

**Sessions and defaults** (ported; viewer/core_ui/cui_nest_ini.inc; user decision NS-1 for the folder):
- Data folder FUN_180043c20 L47031 = paths.conf `nesting` (MCAMUI_NESTING), default <config dir>/Nesting/ with IMG/ and XML/.
  The shipped nesting(_mm).defx text (tools/gen_nest_defx.py -> cui_nest_defx.inc) is written there when missing (what the
  installation provides); FUN_18003ad10 then reads it (ctors first; [SHEET1] else [SHEET]; the default part's contours dropped;
  [GROUP1]); no file -> the ctor values.
- The CParam block is kept whole (NsParams.cp, Mastercam's offsets): ctor FUN_18004e070 (+ COnion FUN_18004e000, duplicate-tool
  mode FUN_18004e910 / FUN_18004e990), metric FUN_18004f6d0, the partial copy FUN_18004e650 + FUN_18004e3e0 (+0xd8 not copied;
  +0x165..+0x16b forced), WriteParamsINI FUN_180052240 (98 keys incl. the 13 DupToolChk-* of FUN_180053a10, writer order) and
  ReadParamsINI FUN_18004f8a0 (Version missing -> -1 and nothing read; CheckForDuplicateScrap / CompressResults / NameAsLabel /
  TrimActive / TrimCombined missing -> 0; TrimMinWidth missing or < 0 -> 0; TrimMode missing -> TrimActive 0; FillAllSheets
  forced 0; sortGroupByOpIdn missing -> the live block's +0x171 = 0; the xfm* resets; xfmMethod outside 1..2 -> 2; the
  DupToolChk block all-or-nothing FUN_180050fe0 / FUN_18004e820). The licence byte +0x1f: a licensed Mastercam's 1 (the
  licence check is dropped).
- Value formats: Yes / No (FUN_1800d3f00), ints through "%d" then 0 / 1 -> NO / YES (FUN_1800d4fc0 + FUN_1800d41c0), "%d",
  "%hd", "%lu", char or "_" for ' ', doubles FUN_1800c9ed0(v, 5) (n_places_an + 5 places, trailing zeros to "x.0"), strings as
  they are; section / key / typed value upper-cased; readers: GetPrivateProfileString (emulated: case-insensitive section / key,
  trimmed value, 255 characters), yes / no / true / false normalised (FUN_1800d12c0), doubles below tol -> 0, chars '_' -> ' '.
- [SHEETn] WriteSheetINI FUN_180091410 / ReadSheetINI FUN_18008e610 (the section counts when SheetName or FileName is there;
  defaults for missing keys as decompiled; GRAINDIRECTION is the common grain); [PARTn] FUN_180060d20 + FUN_180061b90 /
  FUN_1800611d0 / FUN_180061990 and FUN_18005c3e0 (PartName / PartLabel "undefined"); contours FUN_18001c6a0 / FUN_18001cdd0
  and FUN_18001b390 / FUN_18001b890 (TYPE, NUM_PTS, XPOS / YPOS, PART_NUM, COLOR, LEVEL, WIDTH, STYLE, PSTYLE, DEPTH, VIEW,
  OBJk OBJECT / POINT / CENTER / CIRCLE / REVERSED / DEPTH); [GROUP1] FUN_18001e6b0 / FUN_18001e0b0 (the default group only).
- Session write FUN_180040c50 (file removed first; [PARAMETERS] live, sheets, parts, groups), Save FUN_180044350 (named ->
  FUN_180040bc0: ".nstx", the name must end in 'x'), Save As FUN_180044380, Open FUN_180044200 (FUN_180040180 clears; FUN_18003b270
  -> FUN_18003b6b0: sheets / parts appended until 5 missing sections in a row, new sheet ids, PREVID = the file's PartID then -1,
  new PartIDs), New FUN_1800441d0, Restore FUN_18003ffa0, Save defaults 40002 FUN_180040990 (selected sheet with contours,
  selected part's parameters, ClusterMethod 7, live -> defaults), Load defaults 40003 FUN_18003b250(0), the Configuration's Save
  as Defaults (L8306-8309), the defaults writer FUN_1800407a0 (file removed first). Dialog FUN_1800d6590: 10046 / 10047 titles,
  the file name add_prefix_suffix(name, data folder, ".nstx") (mccore add_prefix_suffix_s @1806f1ca0 from the asm), the initial
  session name "t" (the literal @1800f3b7c of FUN_180040180). Status prompts FUN_180039080 / FUN_180039230 (10057-10059,
  10131 / 10132, 10074).
- ctour_to_ents (mastercam.dll @18013f0a0, decompiled 2026-11-05 into decomp/nest/mastercam_ctour.c): a loaded part's contours are
  written back as points / lines / arcs (CW arcs as CCW from the end point).
- Ours: '/' counts as a directory separator in add_prefix_suffix; the upper-cased name of the prompt is not used to open files;
  the prompt's "\n" is a space. Not ported: Restore last session (registry RestoreLast, FUN_180079570 / FUN_18007a180), the
  legacy binary .nst (FUN_18004f760, refused with 10034), attached notes (NOTES-NUMBER read as 0 notes; written 0), M33 contours
  (views > 8), libraries .nslx / .nplx (item 6).
- Engine: the X3 example session ExampleParts\BUMP.NSTX opens and nests; its job (tools/tests/nest_jobs/j_bump_x3.job, made with
  NEST_JOB_DUMP) is bit-identical to the DLL after one more engine function (FUN_18015ed30).

Decoded, not ported (what each needs):
- **Part labels** (AddLabels): SetPartLabelPositions FUN_180041d00 L45585 (run start, L42584): per part with a label text
  (part+0x108, or the part name's just-name with NameAsLabel) and part+0x4d4 == 0: the label note's font_attr (InitNoteFA,
  height LabelHeight or, with LabelHeightAuto, min(L, W) / k shrinking until the text fits, floor min(0.1 inch | 2.5 mm,
  0.25 min(L, W))), the text extents by mastercam.dll `get_ext`, then a TrueShape engine run with the label box as the part
  "null_part" (NAddPartInfo(.., 1, 1)) and the part itself as the sheet ("%dp" / "%dp%dc", NAddSheetInfo(.., 1, 1)) with its own
  callbacks FUN_18000a290 / FUN_18000a330 / FUN_180009ed0 (up to 50 attempts); the position found goes to part+0x208 (cluster
  elements: contour +0x18). The note is then made per instance by CPart::XformEnt FUN_1800621b0 L67715-67741 (calc_note,
  height param_12 / LabelHeight / 0.25, angle = the instance angle, mirrored justification bits). Needs: get_ext (the note
  text extents), the three label callbacks and their engine paths (oracle cases), the per-part label-note record (+0x508).
- **Level names on OK** (FUN_1800414f0 L45253, CycleLevels): set_levname(result level +0x6c8, the result name) and
  (+0x6cc, 10168 "Parts on " + the name), each copied through a 0x100 buffer (strcpy_s / strcat_s). Ported 2026-11-05 with
  the part's level table (viewer/mcam_load.h McLevel: read from the level record DB_ID 0x200 subtype 0x15 as mccore
  FUN_1802d5220 does, written back by the save when changed as save_db @1802bc3d0 builds it - length bytes_per_ent(1) + bytes_per_ent(2) as
  FUN_1802c72c0 writes it, the variable part visible * 4 + named * 0x134 zero-padded to a multiple of 8 (bytes_per_ent @1802e5230); names shown in the Levels
  manager); scene xf_nest_levels.
- (ported later, see "Sessions and defaults" below)
- Groups, user clusters (CLUSTERMETHOD 4, FUN_180035f90: per element NAddPartInfo + NStartCluster(name, 4, 0, 0, ct) /
  NAddClusterInfo / NAddClusterElement(elem, 2, x, y, 360)), the Groups page (122): the cluster-add handler FUN_180074fb0 and
  the group menu handlers are NOT DECODED, and the engine's group / type-4 cluster paths have no oracle cases yet.

Decode results of 2026-11-05 (sub-reads for the port; they correct §5, §7, §8):
- §5.1 outer selection FUN_1800c74f0: the first closed non-point bound **not nested (0.1) in any other**; bounds inside **or
  crossing** it (FUN_180063a70) go to the part, then one forward pass adds bounds inside / crossing a marked one; several
  outers in one chaining = one part each. Classification FUN_180054930: largest |area| closed bound tested first; a closed
  bound crossing the outer = type 0; a nested closed bound with another closed bound inside it = 3 (so outer > hole > island
  gives hole 3, island 1, asm @180054d6f); else 1 (3 with IgnoreHoles). Bbox from break_arc_n points (FUN_1800d0bb0), records
  = absolute - bbox min, BASEPOINT = bbox min, MINMAX = L / W. FUN_1800c49c0 (unique names) is a no-op (FUN_1800c7090 = -1).
  Label FUN_18005f430: the template's label for the first part, else its prefix before the first digit + id ("P"+id). A new
  chained part copies the default part, then the selected part. ALIGNWITHLONGESTEDGE is solids only. Sheets (FUN_18008a340):
  no largest-area step, ignore-holes 0, a bound inside / crossing another closed bound = 3, crossing the outer = 3.
- §7 rectangular read-back: LAB_180009c80 = `movsd xmm1 -> DAT_180134638; jmp FUN_180005f90` (the utilisation = the last
  sheet-result callback's, shown for every result); FUN_180009da0 / FUN_180009ca0 only print progress; one CNST_Result per copy
  (CompressResults: one with "(%d copies)"); x += part WIDTH (+0x240) when rotated; the collar offset (c, c) when c < 0. Origins:
  AutoOrigins x = snap((SheetToSheetDist + L) k) + sheet+0x258 (k = sheet+0x344, the output count), y = +0x260; FUN_180037d60
  stacks the types in -Y. A sheet / part without contours gets FUN_1800c3180's clockwise L x W contour **for good**; ctour
  entities get the main colour / level, chained ones are copies of the original entities (chain_to_ents) with their own
  attributes; CycleColors (FUN_1800412e0) / CycleLevels (FUN_180039560) as decoded. CPart::XformEnt: translate -(base) (not
  -(base + min)), mirror, rotate CCW by the angle, translate P (z = 0), then to the **active** view. Results fields
  (FUN_180084d30): "%.2lf x %.2lf" of the engine sheet L x W (enlarged by 2|c|), Nested size / area 0 in rectangular mode, the
  combo "%s - Result %d" (10161). OK commits (delete original chains with put_special_undo), Cancel / Redo = UndoTransTerminate.
- §8 / §4: lists are multi-select (edits apply to every selected item); controls are greyed, not hidden; the property sheet's
  bitmap buttons are 40014 / 40016 / 40018 / 40020 / 40022 / 40023 (handlers FUN_180066d20 / d90 / e90 / e70 / e00 / 990).
