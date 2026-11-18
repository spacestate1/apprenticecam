# Lathe toolpaths: where the code is (survey 2026-11-16)

Status: survey (1-4), Lathe Rough decoded (5), Lathe Face decoded and ported (6, 2026-11-16: viewer/lathe_face.c), a new
Face op (7, L-6 stage 1: decode + engine - the face from the stock, the lathe stock: viewer/lathe_stock.c). Decision
P4 ("Lathe / wire: skip for now", user_decisions.md): lathe posts still skipped (the LinuxCNC post refuses lathe ops).
Stage 2 (the UI: menu, Face dialog, lathe tool pick, saving) is on branch worktree-agent-ae2f66970f48c6949 / int-l3, under
regress3 on 2026-11-17 (its section 8 arrives with that merge). Session status: formats/TODO.md "Where we are (2026-11-17)".

## 1 turnkernel.dll is NOT Mastercam's lathe engine
`~/mastercam-re/files/turnkernel.dll` (x86, 9.8 MB, MSVCR80): HSMWorks ApS `com::hsmworks::camkernel` (C++, RTTI names) +
a statically linked **Python 2.4.2** interpreter (847 exports, all `Py*` except `entry`, `getBuild`, `getDate`, `getProduct`,
`getVendor`, `getVersion`, `setNativeExceptionHandler`, `setStdErrPath`, `setStdOutPath`; exports/turnkernel.dll.tsv).
- Loaded only by `cnccalc.dll` (x86, string `\TurnKernel.dll`), which is loaded only by CIMCO components
  (`cimcoedit.exe`, `cimcodnc.exe`, `cyclemacro.dll`, `ncbaseclient.dll`, `editorconfig.dll`, `inspectdll.dll`): it is the
  turning engine of CIMCO Edit's cycle macros (the bundled NC editor), not of Mastercam's lathe operations.
  mclathe.dll (x64) does not import it and cannot load an x86 DLL in-process.
- Embedded Python: a 114-module `PyImport_FrozenModules` table (file offset 0x912f68). Extracted by
  `tools/frozen_extract.py DLL hsmkernel.LatheRoughingOp OUTDIR` -> `~/mastercam-re/work/turnk/pyc` (+ index.tsv);
  decompiled with pycdc (Decompyle++, built in `~/mastercam-re/oss/pycdc`) -> `work/turnk/py` (78 / 114 complete; the
  other 36 incomplete, try/except) and disassembled -> `work/turnk/das` (all 114).
  Packages: hsmkernel (50: Lathe{Roughing,Grooving,Cutoff}Op, Lathe*Parameters*, the HSM 3D ops), roughsteel (12),
  freesteel (8), exsteel (5), stdlib.
- The lathe modules are thin wrappers: LatheRoughingOp maps ~60 parameters onto `camkernel.TurningProfileParameters` and
  calls the C++ `camkernel.TurningProfile(...).generate()`. Useful only as a parameter list for HSMWorks turning.

## 2 mclathe.dll: Mastercam's lathe operations
`~/mastercam-re/files/mclathe.dll` (x64, 5 MB, MFC 120; imports MCCore, MCMill, MCCE, MCPost, MCMachineDef, TlCore ...),
6986 exports with full C++ names (exports/mclathe.dll.tsv). Exported methods per toolpath class:
CLRoughTp 70, CLGrooveTp 69, CPlungeTurnRoughTp 64, CLMiscOpsTp 52, CLFinishTp 46, CPlungeTurnFinishTp 41, CLCutoffTp 30,
CLPointTp 24, CDynamicRoughTp 22, CLathePinchTurnTp 16, CLFaceTp 14, ContourRoughTp 13, CPlungeTurnTp 10,
CMtSpindleGripTp 10, CLCanRoughTp 10, CLFinGrvTp 9, CMtTailstockTp 8, CLCanPatternTp 8, CLThreadTp 6, CLCanFinishTp 5,
CLDrillTp 4. Parameter structs in the signatures: `prm_lrough`, `ctour_rec`, `l_inters_rec`, `RoughPassType`.
Resources: mclatheres.dll. Template part: lathe.mcam_8; machines lathe_default.lmd, lathe_*_slant_bed*.lmd; posts
generic_fanuc_2x_lathe.pst, generic_haas_tl_2x_lathe.pst.

## 3 Oracle data on hand (tools/list_ops.py)
| file | lathe ops with stored NCI |
|---|---|
| `<windows>/Users/Public/Documents/shared Mcam2017/common/reports/SST/Samples/Lathe/Lathe.mcam` (same bytes as `~/mastercam-re/files/lathe.mcam_8`, major 0x12) | op 1, 2 Lathe Rough (60); 267 NCI records |
| `~/mastercam-re/files/millturn.mcam_8` | op 1 Face (66), 2 / 7 Rough (60), 3 / 14 Lathe Drill (64), 4 / 5 / 8 Finish (61); + C-axis drills, 2 Contours; 685 NCI records |
| `.../shared Mcam2017/lathe/Ops/LATHE_INCH.mcam-defaults`, `LATHE_MM.mcam-defaults` | default records (no NCI) of every lathe op: 60-67, 71, 72, 201-219, 224 |
| `.../lathe/Ops/Lathe_Inch.mcam-operations`, `Lathe_MM.mcam-operations` | operations library |
Still wanted from Mastercam (Windows): Groove, Thread, Cutoff and more Rough / Finish variants with NCI.

## 4 Ghidra (L1, 2026-11-16)
Project `~/mastercam-re/work/lathe/gh` (name `lathe`), full auto-analysis in 127 s (work/lathe/import.sh, import.log).
- `work/lathe/dump/lrough_finish_face.c`: CLRoughTp / CLFinishTp / CLFaceTp and their callees (depth 1), 288 functions,
  32.7k lines. Drivers (the op's NCI generation, as bnci_* in mcmill): `CLRoughTp::bnci_lrough` 0x180126a80,
  `CLFinishTp::bnci_lfinish` 0x18005b3e0, `CLFaceTp::bnci_lface` 0x180045c80. Rough core: RoughArea 0x1802016b0 ->
  RoughOneSection 0x180201ff0 -> RoughOnePass 0x1801f4720, CompBoundaries 0x1801fa550, SemiFinish 0x1802029e0;
  chip break RoughChipBreak::DoChipBreak 0x18029c090. Face: CompRough / GenerateRough, CompFinish / GenerateFinish.
- `work/lathe/dump/mclathe_all.c`: every function (DumpDecomp ".").
Re-dump: `analyzeHeadless work/lathe/gh lathe -process mclathe.dll -noanalysis -readOnly -scriptPath tools/ghidra_scripts
-postScript DumpDecomp.java OUT.c REGEX [max] [calleeDepth]`.

## 5 Lathe Rough (opcode 60): decoded
Status: DECODE (L2, 2026-11-16), not ported. Sources: `work/lathe/dump/lrough_finish_face.c` (DumpDecomp, callees depth 1)
and `mclathe_all.c`; functions cited as `Name 0xaddr`. Tools: `tools/decomp_fn.py DUMP REGEX` (print functions),
`tools/lathe_prm.py FILE..` (prm_lrough fields of every opcode-60 record), `tools/lathe_passes.py FILE` (pass levels in a
part's stored NCI).

### 5.1 Call path (`CLRoughTp::bnci_lrough 0x180126a80`)
| step | what (as read) |
|---|---|
| load | `DatabaseRetrieve` the op ent into a 0x2938 stack copy (`local_2a38`); op = ent+0x68, **prm_lrough = ent+0x1228 = op+0x11c0** (`local_1810`), tool `ltool_rec` = op+0x4b8 (`this+0x88` = `local_2518`). Asserts RoughTp.cpp 0x151 / 0x165 / 0x16c. |
| flags | `this[0x120]` ("remaining stock" mode, name not traced) = 1 when opcode in {0x3c, 0xd2, 0xdc} and op+0xb68 == 1 and prm+0x2e8 == 1. If prm+0x30 (cut method) == 2: prm+0x10, +0x20, +0x2e0 forced 0. `this[0x111]` = turret-top XOR (prm+0x200 == 1). |
| geometry | `CRoughTpGeometry::GetAllGeometry 0x1801ed430`: chains -> `build_lctour` (part ctour + stock ctour, entry / exit point lists geom+0x80 / +0x6c8, counts +0x6c0 / +0xd08); `SetAbsRoughingAngle 0x1801edd40` -> absolute angle geom+0xd10 and side char geom+0xd0c ('D','U','L','R'); cos/sin(-a) at +0xd18/+0xd20, cos/sin(a) at +0xd28/+0xd30 (copied by `InitVariables 0x1801264f0` to this+0x170..0x188, this+0x108, this[0x112]). No sections -> warning 0x6410. |
| init | `InitVariables`: this[0x91] = abs(prm+0x2e0) > 0.01; this[0x110] = rough angle prm+0x28 not in {0,90,180,270,360} (compared as degrees here, but `SetAbsRoughingAngle` adds +-pi/2 / pi to it as radians: both readings agree only at 0, the value in every sample); clearance this+0x520 = op+0xb78 if op+0xb69 and > tol, else machine group +0x16a0, min 2*tol; tool-inspection (prm+0x210) and chip-break (prm+0x318) helpers set up with op feed op+0x310 / spindle op+0x328. |
| start | `CLNciFmt::lwr_sof`; G1 through the geometry's entry points (geom+0x80, count +0x6c0). |
| per section i of geom (stride 0x28: +0 / +4 part ctour first / last index, +8 / +0xc stock first / last) | `copy_ctour` part -> `local_c8`, stock -> `local_c0` (rec 1 type forced 'L'); stock range found in the rotated frame (`rotate_lctour` by this+0x170/0x178, `find_ctour_range`, rotate back); **`CompBoundaries 0x1801fa550`** (assert 0x286), part must keep >= 2 recs (0x28d); **`RoughArea 0x1802016b0`**; **`SemiFinish 0x1802029e0`**. |
| end | G1 through exit points (geom+0x6c8, count +0xd08); `SendLToolHome`; linked op (prm+0x1f8) not semi-finished -> `bnci_wr_2600(0x7d01)`; `AddCtourFlagsLathe`. Pinch-turn link (prm+0x1f8, op id prm+0x1fc): spindle direction must match the linked op (else warning 0x1069); if linked op+0x11d2 == 1 and its +0x11e8 != 0, op feed op+0x310 and plunge feed prm+0x50 are doubled for the run. |

`RoughArea 0x1802016b0`: zigzag flag this[0x90] = prm+0x30 != 0; `set_overlap_angle 0x1801fa230` -> this+0x100;
`FindCtourSections 0x1801f21e0`; implied-boundary start insert (5.3); top = max y of part / stock -> this+200; records the
indexes of horizontal 'L' recs (dy <= tol, length > tol) in this+0x98; then `RoughOneSection(s)` for s = 1 and every later
section whose flag (+0) is 0. Between sections in zigzag a `RetractMove` + G1 (feed from `FindFeedrate`). After the last:
`add_end_cut_position(prm[0x151], prm+0xf8)`; if no part defined, a rapid to (prev x, max(top, prev y) + 0.25*doc).

`RoughOneSection 0x180201ff0`: `FindSectionCutDepths` -> CutDepthsType list (0x20 bytes: +0 entry height, +8 cut depth,
+0x10 exit height, +0x18 char "critical depth" flag); for each pass k: `RoughOnePass(k, n, entry, cut, overlap-depth,
exit, ...)` (overlap-depth = 1e38 except cut method 2); Esc (`KeyPress(0x1b)`) aborts. With this[0x91] (prm+0x2e0) a pass
that tilted (RoughOnePass `*param_8`) moves the next entry / exit height to that pass's end + overlap (prm+0x18).

`RoughOnePass 0x1801f4720` (930 lines; structure only): `find_inters_4_boundary` at entry height, cut depth, exit height
-> `filter_by_section_entr / _retr`, `FindCutPoints` (start / end `l_inters_rec` of the cut); prm+0x2e0 tilt: angle =
atan2(prm[0x2e0] * 0.02 * doc, pass length), used only if 0.001 deg < |angle| < 20 deg and length > 3 * doc (boundaries
rotated about the pass mid point, `RotateAllBoundaries`); `make_entry_moves` / `make_cut_moves` / `make_retract_moves`
fill the pass buffer (this+0x198, 0x28-byte entries, count this+0x1b8); duplicate / collinear buffer points dropped;
sections whose end point lies on this pass are marked done (+0x38); `WriteCutList 0x1801f6c60`. In zigzag the direction
flag this[0x218] toggles after each pass.

`WriteCutList`: zigzag back pass -> `InvertCutList`; cc mode 0 -> `xlate_lctour` by the tool offset; `BufferPass` ->
`ShortenBufferedPass` -> `ExtendBufferedPass` (start by prm+0x1b0, end by prm+0x2b8, both skipped for zigzag; `calc_ext_pt`
along the tangent, fallback `calc_ext_pt_cutdir` with direction prm+0 / angle prm+0x28) -> tool inspection (`FUN_1801efc80`,
prm+0x210) -> `RoughChipBreak::PreprocessBufferedPass 0x18029c910` (prm+0x318); then `CLEntryExit::enter_contour` with
lead-in prm+0x58 (negated for the zigzag back pass), the buffered 'L' / 'A' moves, 'T' = `DoToolInspectionRetract`,
'C' = `RoughChipBreak::DoChipBreak 0x18029c090`, `exit_contour` with lead-out prm+0xf8.

`SemiFinish 0x1802029e0` (only if prm+0x1d8): prm+0x1da passes (>= 1) of `CLFinishTp::OffsetContour(step prm+0x1e0,
stock X prm+0x1e8, Z prm+0x1f0 (minus prm+0x38 / +0x40 in this[0x120] mode))` on the uncompensated part copy made in
CompBoundaries; `enter_contour` (prm+0x58), vtable slot +0x30 (cut, NOT DECODED which function), `exit_contour` (prm+0xf8);
pinch-turn link: `bnci_wr_2600` + `wr_dwell_ss` per pass.

`DoChipBreak 0x18029c090` (opcodes 0x3c / 0x48 only): `ChipBreakDwell`; cb+0x20 == 0: retract `cb+0x28` via
`GetRetractPoint` and back (two G1); else `exit_contour` with lead-out then `enter_contour` with lead-in (prm+0x1b0
zeroed meanwhile). `prm_lathe_chip_break` = prm+0x318 (op+0x14d8).

### 5.2 prm_lrough (ent+0x1228, op+0x11c0)
NCI ids: `bnci_wr_lrough_prm 0x1800c7190` writes `[lrough]` params id -> op offset; the MP parameter reference
(`formats/opparams/param_reference.tsv`, id + 10000) names them. Lead in/out ids 11001-11030: `FUN_1800c5560` (op+0x1218 for
opcodes 0x3c / 0x42). Other names are **by use** (function cited). Values: `tools/lathe_prm.py` on LATHE_INCH.mcam-defaults
op 45, Lathe.mcam ops 1 / 2, millturn.mcam_8 ops 2 / 7.

| prm+ | type | NCI id | meaning (source) | defaults | Lathe 1 | Lathe 2 | mt 2 | mt 7 |
|---|---|---|---|---|---|---|---|---|
| 0x000 | i16 | 10214 | rough direction: 0 OD, 1 ID, 2 face (angle - 90 deg, side 'L'), 3 back face (+90 deg, 'R') (`SetAbsRoughingAngle`; MP ref text says "0 = ID, 1 = OD" but code + samples: op "OD ROUGH RIGHT" is 0, the bore op mt 7 is 1) | 0 | 0 | 0 | 0 | 1 |
| 0x008 | f64 | 10200 | depth of cut (`set_steps_between_depths`) | 0.1 | 0.1 | 0.1 | 0.05 | 0.05 |
| 0x010 | u8 | 10215 | equal steps | 1 | 1 | 1 | 1 | 1 |
| 0x018 | f64 | 10201 | overlap amount (added to entry / exit heights, `add_cut_depths`) | 0.01 | 0.01 | 0.01 | 0.01 | 0.01 |
| 0x020 | u8 | 10216 | use overlap | 1 | 1 | 1 | 1 | 1 |
| 0x021 | u8 | 10220 | MP: "use advanced parameters" (not read by the rough code) | 0 | 0 | 0 | 0 | 0 |
| 0x028 | f64 | 10204 | rough angle (`SetAbsRoughingAngle`, `InitVariables`) | 0 | 0 | 0 | 0 | 0 |
| 0x030 | u8 | 10213 | cut method: 0 one way, !=0 zigzag (`GetSectionCriticalDepths`); value 2 has its own pass pairing in RoughOneSection, name NOT DECODED | 0 | 0 | 0 | 0 | 0 |
| 0x038 / 0x040 | f64 | 10202 / 10203 | stock to leave X / Z (`lthcc0` in CompBoundaries; 0 in this[0x120] mode) | .01/.01 | .01/.01 | .01/.01 | .01/.005 | .01/.005 |
| 0x048 | f64 | 10205 | passed to `build_lctour` / `OuterBdryCtour` (name not traced) | 0.001 | | | | |
| 0x050 | f64 | 10221 | plunge feed (neg = per rev; `FindFeedrate`) | -0.005 | -0.005 | -0.005 | -0.00787 | -0.00591 |
| 0x058 | prm_lathe_ee | 11001.. | lead in: +0 / +8 entry vector (Z, X) delta (11001 / 11002) | (-0.1,0) | (-0.1,0) | (0.1,0) | (-0.1,0) | (-.0354,.0354) |
| 0x0f8 | (ee+0xa0) | 11011 / 11012 | lead out vector (Z, X) delta | (.0707,.0707) | same | (-.0707,.0707) | (.0707,.0707) | (.0354,-.0354) |
| 0x151 | u8 (ee+0xf9) | 11030 | flag passed to `add_end_cut_position` | 0 | 0 | 0 | 0 | 0 |
| 0x198 | u8 | 10222 | implied boundary: needs 0x198 && 0x1a9 && 0x1a8 in {1,2} (`SetImpliedBoundary 0x1801fb750`); set to 1 in this[0x120] mode | 0 | 1 | 1 | 1 | 1 |
| 0x1a0 | f64 | 10223 | (3 deg in rad; reader not in the dump, NOT DECODED) | 0.05236 | = | = | = | = |
| 0x1a8 / 0x1a9 | u8 | 10224 / 10225 | implied boundary mode / enable | 0/0 | 0/0 | 0/0 | 0/0 | 1/0 |
| 0x1b0 | f64 | | extend pass start (`ExtendBufferedPassStart`; also zeroed during re-entries) | 0.1 | 0.1 | 0.1 | 0 | 0 |
| 0x1b8 | u8 | 10226 | 1 = never use plunge feed (`FindFeedrate` op[0x1378]) | 0 | 0 | 0 | 0 | 0 |
| 0x1b9 / 0x1c0 / 0x1c8 | u8 / f64 deg / u8 | | overlap angle on / angle / ignore insert angles (`set_overlap_angle`) | 0/10/0 | 0/10/0 | | | |
| 0x1d0 | f64 | | minimum cut depth (`set_steps_between_depths`, `AdjustFinalDepth`, critical depths) | 0.001 | 0.001 | 0.001 | 0.001 | 0.001 |
| 0x1d8 / 0x1da | u8 / i16 | | semi finish on / passes | 0/1 | 0/1 | 0/1 | 0/1 | 0/1 |
| 0x1e0 / 0x1e8 / 0x1f0 | f64 | | semi finish step / stock X / stock Z | .1/0/0 | .1/0/0 | .1/0/0 | .1/.01/.01 | .1/.01/.01 |
| 0x1f8 / 0x1fc / 0x200 | u8 / i32 / u8 | | linked (pinch turn) op on / op id / turret flag (bnci_lrough, `PinchTurnStartOfPass`) | 0 | 0 | 0 | 0 | 0 |
| 0x210 / 0x298 | u8 / f64 | | tool inspection on / interval (`InitVariables`) | 0/0.02 | 0/0 | | | |
| 0x2b8 | f64 | | extend pass end (`ExtendBufferedPassEnd`) | 0 | 0 | 0 | 0 | 0 |
| 0x2e0 | f64 | | pass tilt percent (RoughOnePass atan2 rule; this[0x91]) | 0 | 0 | 0 | 0 | 0 |
| 0x2e8 | u8 | | with op+0xb68 -> this[0x120] | 0 | 0 | 0 | 0 | 1 |
| 0x2e9 / 0x2f0 / 0x2f8 / 0x300 / 0x308 | u8 / f64 / f64 / u8 / f64 | | variable depth on / depth at range top / at range bottom / incremental mode / increment (`SetPecksBetweenDepths`, `FindPeckDepths`, `FindIncrementalDepths`) | 0/0/0/0/0 | 0/.05/.1/0/.005 | = | = | = |
| 0x318 / 0x338 / 0x340 | u8 / u8 / f64 | | chip break on / retract mode / retract amount (`DoChipBreak`) | 0/0/0.1 | 0/0/0 | | | |
Op fields read: feed op+0x310 (neg = per rev), spindle op+0x328 (+0x32d dir), feed-type chars op+0x32e / +0x32f, cc
op+0x568 / +0x56a / +0x56c, view op+0x6d0 (WCS for `RemoveWCSOriginFromY`).

### 5.3 Inputs and coordinates
- **ctour_rec** (0x60 bytes, 1-based, rec 1 = start point; formats/nesting_addin.md): +0 / +8 end point, +0x10 centre,
  +0x20 CW, +0x21 'L' / 'A'. Lathe: **x = Z, y = X** (lead-in (-0.1, 0) in Lathe op 1 = NCI move dZ -0.1; lead-out
  (0.0707, 0.0707) = dZ +0.0707, dX +0.0707; mt 7 (-0.0354, 0.0354) = dZ -0.0354, dX +0.0354). Whether NCI X is radius
  or diameter is not established here; depths and NCI X share units (5.5).
- **Cut frame**: every contour is rotated by -a (a = absolute rough angle, `SetAbsRoughingAngle`): dir 2 / 3 -> a =
  angle -+ pi/2; dir 0 / 1: a = angle when (dir 0 and the part's |y max| > |y min|, i.e. above the axis) or (dir 1 and
  below), else angle + pi ('U'). In the cut frame passes are horizontal lines y = depth stepped downwards.
- **CompBoundaries 0x1801fa550**: stock ctour reversed if its ends meet the part's the other way round, `remove_stock_undercuts`
  in the cut frame, `AdjustStockCtour`, `lthcc1` (side -1 for dir 0/1, +1 for 2/3); prm+0x1d8 -> uncompensated part
  copy for SemiFinish; part `lthcc0(..., stock X prm+0x38, Z prm+0x40, ...)`; stock `lthcc0` likewise; `lthcc2_rough`;
  part trimmed at start / end down to max(boundary point y, stock top) + tool radius (`TrimCtourStart/EndDownToHeight`);
  stock ctour shifted by r = tool +8 (`xlate_lctour`; part range after `RemoveWCSOriginFromY`): dir 0: +r in y above the
  axis (|ymax| > |ymin|), -r otherwise; dir 1: the opposite (-r above, +r below); dir 2: +r in x; dir 3: -r in x; implied boundary
  (`SetImpliedBoundary` -> `HandleImpliedStockBoundaries 0x1801fb050`, not decoded); cut-direction sign this+0x220 = +1 if
  part end x > start x else -1; arcs broken at quadrants; cc-in-control `OptimizeCompInControl`.
- **Sections** (`FindCtourSections 0x1801f21e0`, array this+0xd8, 0x40 bytes, 1-based, count this+0xf8): a new section
  starts where dy changes sign; +0 u8 1 = rising section (skipped by RoughArea), +8 min y, +0x10 max y, +0x18 end point,
  +0x28 'P', +0x2c ctour index, +0x30 section no., +0x38 done flag. rec -> section map this+0x1c8 (i16).
- **l_inters_rec** (0x20; `add_inters 0x1801feea0`): +0 point, +0x10 type 'P' part / 'C' stock / 'I' implied boundary,
  +0x14 ctour index, +0x18 section. `find_inters_4_boundary 0x1801ffb00`: intersections of y = depth with implied
  boundary, part and stock; retried at y -1e-5, +1e-5, -1e-4, +1e-4 until the count is even; sorted along the cut
  direction; duplicate / extra stock / extra inters removed.

### 5.4 Depth rule
`FindSectionCutDepths 0x1801fc800` (doc = prm+8, min = prm+0x1d0):
- |doc| < tol: one pass at the section min.
- Critical depths (`GetSectionCriticalDepthsOneWay 0x1801fd140`): section min, then the y of every horizontal 'L' rec of
  the section (`GetCtourRoughSection`) that is > previous + min + tol and < section max - tol, then section max; list
  reversed (top first). ZigZag (`0x1801fd650`): both directions of the section, 'L' recs within 5 deg of horizontal, sorted
  and de-duplicated (detail NOT DECODED).
- Which list is stepped: **equal steps on and no tilt (prm+0x2e0 = 0)**: if overlap off and > 2 critical depths ->
  per segment: n = floor(span / doc); if span - n*doc < min (min < tol -> 2 tol) steps = span / (n+1) else doc, last step
  to the critical depth; **otherwise (overlap on, or <= 2 depths) one `set_steps_between_depths(top, bottom)`** (critical
  depths ignored). Equal steps off: overlap off and variable depth off -> `set_steps` between each pair of critical
  depths; else top -> bottom.
- `set_steps_between_depths 0x1801fe970` (span = top - bottom > doc; n = floor(span / doc); m = min(prm+0x1d0, doc - tol),
  2 tol if prm+0x1d0 < tol): **equal steps: n += 1 if span - n*doc > m; n passes of span / n.** Not equal: rest =
  span - n*doc; rest < tol -> n passes of doc; rest <= m -> n+1 passes of span/(n+1); else n passes of doc + one of rest.
  Variable depth (prm+0x2e9): `SetPecksBetweenDepths` -> `FindPeckDepths` (pass depth interpolated linearly between
  prm+0x2f0 at range top and prm+0x2f8 at range bottom) or `FindIncrementalDepths` (prm+0x300: start at 0x2f0, change by
  prm+0x308 per pass toward 0x2f8), then `AdjustFinalDepth` (last step < min -> spread).
- `add_cut_depths 0x1801fe700` one way: entry = exit = previous depth + overlap (prm+0x18) if use overlap, else entry =
  previous depth, exit = new depth. Zigzag: odd / even passes swap entry and exit sides.

### 5.5 Output and oracle check
- Moves: `BufferMoveLinear` / `BufferPass` buffer 'L' / 'A' records; `CLNciFmt::wr_lin_move 0x1800c27c0` /
  `wr_arc_move 0x1800c1910` write them (NCI 0 rapid, 1 feed, 2/3 arcs plane 2, as tools/nci_decode.py shows).
- **Feed (`FindFeedrate 0x1801f3620`)**: G0 -> 0; a feed move going "into" the cut side (side char 'D' and dy < -tol, 'U'
  dy > tol, 'L' dx < -tol, 'R' dx > tol, world coords) gets the plunge feed prm+0x50 / type op+0x32f, unless prm+0x1b8
  or forced (WriteCutList's lead-in calls force); else op feed op+0x310 / type op+0x32e. Check: in mt 7 (ID, 'U') the only
  moves at -0.0059 (= prm+0x50) are two in-pass moves with dX > 0; every other feed move is -0.0118 = op+0x310.
- **Pass levels** (`tools/lathe_passes.py`):

| op | doc | passes | step (NCI X) | span = n * step | floor(span/doc)+1 | top check |
|---|---|---|---|---|---|---|
| Lathe 1 (OD) | 0.1 | 17 | -0.09455 const | 1.60735 | 16+1 = 17 | pass 1 exit climbs to X 2.3430 = 2.24842 + 0.09455 |
| Lathe 2 (OD) | 0.1 | 14 | -0.09715 const | 1.3601 | 13+1 = 14 | pass 1 exit to 2.1640 = 2.06685 + 0.09715 |
| mt 2 (OD) | 0.05 | 9 | -0.04578 const | 0.4120 | 8+1 = 9 | pass 1 exit to 0.8365 = 0.79072 + 0.04578 |
| mt 7 (ID) | 0.05 | 7 (+ a later section) | +0.04915 const | 0.3441 | 6+1 = 7 | pass 1 returns to X 0.1200 = 0.1691 - 0.04915 |
All four agree with the equal-steps + overlap rule (one top -> bottom split, remainder > min cut 0.001 -> n+1 equal
steps), with ctour y = NCI X up to a constant tool offset. They do not separate floor+1 from ceil (no sample has a
remainder <= min cut).
- **Overlap**: each pass k >= 2 ends by following the contour up to previous level + 0.01 (Lathe 1: exits 2.2584 =
  2.24842+0.01, 2.1639 = 2.15387+0.01; Lathe 2: 2.0768 = 2.06685+0.01); pass 1 stops at the stock top, not top + 0.01
  (clip location NOT DECODED).
- **Pass skeleton** (Lathe 1): rapid to (entry start Z 0.4916, X level), feed lead-in (-0.1, 0) to Z 0.3916, feed cut,
  feed up the contour to the exit height, feed lead-out (+0.0707, +0.0707), rapid along Z back to 0.4916, rapid in X to
  the next level. Lead-in / lead-out vectors = prm+0x58 / prm+0xf8 exactly in all four ops.

### 5.6 NOT DECODED (next to read)
- `FindCutPoints 0x1801f2a90`, `filter_by_section_entr/_retr/_cut`, `make_entry_moves 0x1801f8990`,
  `make_retract_moves 0x1801f8d90`, `FindEntryDirection` / `FindRetractDirection`: exact entry / exit point choice and
  the contour-following climb (5.5 overlap); where pass 1's exit is clipped to the stock top.
- `BufferPass 0x1801f1100` / `BufferMoveLinear 0x1801f0b80` / `ShortenBufferedPass*` / `InvertCutList`; the rapid pair
  between passes (`CLEntryExit::enter_contour 0x18003f7d0` / `exit_contour 0x180040080`, `CLatheToolpath::RetractMove`).
- `CRoughTpGeometry`: `build_lctour`, vtable +8 / +0x10 (`CompPart` / `CompStock`), `DefineStockCtour`,
  `FindRemainingStock`, `OuterBdryCtour`; stock from op+0xb68 / this[0x120] mode; `HandleImpliedStockBoundaries`.
- Tool comp `CLToolComp::lthcc0 0x1800bc0b0`, `lthcc1 0x1800b5480`, `lthcc2_rough 0x1800b1060`, `remove_stock_undercuts`.
- Zigzag details (`GetSectionCriticalDepthsZigZag 0x1801fd650`, cut method 2), tilt (prm+0x2e0) pass geometry,
  `RotateAllBoundaries`, `PinchTurnStartOfPass 0x1801f42c0`, SemiFinish cut (vtable +0x30), tool inspection
  (`CRoughToolInspection::DoToolInspectionRetract 0x1801238f0`, `FUN_1801efc80`), chip break `AddBufferedChipBreak` /
  `AdjustChipBreak` / `ChipBreakDwell`.
- Readers of prm+0x1a0 (id 10223), prm+0x48 (id 10205), prm+0x21; contour flags (`AddCtourFlagsLathe 0x1800bc7b0`);
  NCI X radius vs diameter; prm+0x28 units.

## 6 Lathe Face (opcode 66): decoded and ported
Status: DECODE + PORT (L1 Face, 2026-11-16). Engine `viewer/lathe_face.c` (pure; glue `gen_lathe_face` in viewer/cam2d.c,
`cam_op_from_file` case `CAM_LFACE` in viewer/cam_fileop.c). Oracle `tools/tests/lathe_face_test.sh` (6.7). Sources:
`work/lathe/dump/lrough_finish_face.c` / `mclathe_all.c` (mclathe.dll) and `work/lathe/dump/core_lathe.c` (mccore.dll:
`is_turret_top 0x1800344b0`, `cc_in_computer 0x18009de10`, `cc_in_control 0x18009de30`, `ccomp_pock_from_ctour 0x1805d5e50`;
DumpDecomp on `~/mastercam-re/ghidra_core`, -readOnly). Field dump: `tools/lathe_face_prm.py FILE..`. (A tlcore.dll project,
`work/tlcore/gh`, was made looking for `LATHETOOLCLASS::TopTurret`: it is in mclathe.dll, 0x18015cd80.)

### 6.1 Call path (`CLFaceTp::bnci_lface 0x180045c80`)
| step | what |
|---|---|
| load | `DatabaseRetrieve` -> ent (0x2938), op = ent+0x68, tool `ltool_rec` = op+0x4b8. Assert FaceTp.cpp 0x707 when rough (op+0x11c0) and finish (op+0x11d0) are both off. cc in control (op+0x568 1..3) with no retract: warning 0x14aa unless the op+0x1630.. conditions hold (not needed: refused) |
| geometry | `CFaceTpGeometry::GetAllGeometry 0x1801e8990` (6.3) |
| init | `CLFaceTp::InitVariables 0x1800445e0`: signs this+0x98 / +0xa0 (6.3), this[0x93] = `GetOpStockBoundary`, this+0xb0 = retract gcode (0xfffd = -3 when prm+0x40, else 0xfffb = -5) |
| rough | `GenerateRough 0x180204c50`: prm+0 off -> nothing; else `lwr_sof` (tool change), fmt+0x2940 = 1; corner off -> `RoughStrategy0 0x180204e60`, on -> RoughStrategy1..5 by op+0x1398 (NOT DECODED) |
| finish | `GenerateFinish 0x1802046b0`: prm+0x10 off -> nothing; finish feed override op+0x139a (op+0x310 = op+0x13a0, negated when op+0x13a8 = 'R'; op+0x32e = (op+0x13a8 == 'S')); spindle override op+0x13a9 (`wr_dwell_ss`); `lwr_sof` when rough is off; corner off -> `FinishStrategy0 0x180203090`, on -> FinishStrategy1..3 by op+0x1399 (NOT DECODED); overrides restored after |
| end | no pass written (this[0x92]) -> warning 0x1281; `SendLToolHome 0x180114f90`; `AddCtourFlagsLathe` |

Corner "on" (GenerateRough / GenerateFinish / MakeContour): op+0x1358 and not (op+0x135a == 0 and op+0x1360 < tol) and not
(op+0x135a == 1 and (op+0x1378 deg outside (tol, pi/2 - tol) rad or op+0x1388 < tol)). `MakeContour 0x1801ea1e0` builds the
corner contour (type 0: L, arc, L; type 1: angled line + optional radii op+0x1368 / op+0x1370), used only by the corner
strategies.

### 6.2 prm_lathe_face (op+0x11c0 = ent+0x1228)
NCI ids from `bnci_wr_lface_prm 0x1800c6550` (`[lface]`; +10000 = the MP names of formats/opparams/param_reference.tsv); lead
in / out by `FUN_1800c5560` (ids 11001..). Values: millturn op 1 / LATHE_INCH.mcam-defaults op 56 / LATHE_MM op 2.

| prm+ | type | id | meaning (MP name; use) | mt 1 | inch | mm |
|---|---|---|---|---|---|---|
| 0x00 | u8 | 10603 | rough on (GenerateRough) | 0 | 0 | 0 |
| 0x08 | f64 | 10604 | rough max stepover (RoughStrategy0) | 0.1 | 0.1 | 2 |
| 0x10 | u8 | 10606 | finish on (GenerateFinish) | 1 | 1 | 1 |
| 0x18 | f64 | 10608 | finish stepover ("amount of each cut") | 0.1 | 0.01 | 0.25 |
| 0x20 | i16 | 10607 | finish passes | 1 | 1 | 1 |
| 0x28 | f64 | 10609 | stock to leave | 0 | 0 | 0 |
| 0x30 | f64 | 10614 | lead-in amount: pass start y += -this[0xa0] * it | 0.1 | 0.1 | 2 |
| 0x38 | f64 | 10612 | retract amount: after each pass, x -= this[0x98] * it | 0.1 | 0.1 | 2 |
| 0x40 | u8 | 10613 | retract rapid (1: gcode -3 = G0; 0: -5 = G1 at op+0x310) | 1 | 1 | 1 |
| 0x48 | f64 | 10601 | overcut: pass end y += this[0xa0] * it | 0 | 0 | 0 |
| 0x50 | u8 | 10615 | cut away from the centre line (= op+0x1210: swaps geom +0xb0 / +0xc0, the signs, the stock face) | 0 | 0 | 0 |
| 0x58 / 0xf8 | prm_lathe_ee_vec | 11001.. | lead in / out vectors: used only with op+0x888 (6.5) | (0,-0.1) / (0.1,0) | same | (0,-2) / (2,0) |

Op fields: op+0x88 face geometry source (2 = the op's points), op+0xb68 face from stock, op+0x888 entry / exit on, op+0x540
turret (= ltool_rec+0x88), op+0x568 / +0x56a / +0x56b compensation type / direction / `set_min_fil_angle` mode, op+0x310 feed
(neg = per rev), op+0x328 rpm, op+0x32e surface feed, op+0x292 (SendLToolHome: a `calc_lathe_ref_pt` move), op+0x720 view
origin, op+0x6d0 / +0x6d2 view (op_view, ent 0x738) and op+0x570 tool plane (ent 0x5d8).
`ltool_rec` (op+0x4b8; `LATHETOOLCLASS::Get_ltool_rec 0x18014f920`): +0 orientation (quadrant), +8 nose radius, +0x10 / +0x18
width / height, +0x20 / +0x28 angles (rad), +0x30 / +0x38 tool centre -> reference point (OutputOnePass), +0x40 / +0x48 front
edge, +0x50 / +0x58 back edge (`FUN_1801c20e0` / `FUN_1801c2290`, not read), +0x61 back edge defined, +0x88 turret.

### 6.3 Geometry and signs
- `GetAllGeometry`: op+0xb68 == 1 with a stock polygon, or op+0x88 != 2 -> `SetMaxMinPtsFromOp` / `GetFaceBoundaryFromStock`
  (BOUNDARYCLASS stock, `GetOpXStockLimits`; makes the two point entities): **NOT DECODED, refused**. Else `tp_get_all`: the op's
  two point associations (TP kind 1; millturn op 1: (1.007, 0.8365, 0) and (0.982, 0, 0), mode 'P'), minus op+0x720,
  `world_to_view(op+0x6d2)`: geom+0xa8 = the larger x, +0xb8 = the smaller, swapped when `GetOpStockFace` == 1.0; geom+0xb0 =
  the y of larger |y|, +0xc0 the other, swapped with op+0x1210. Then `MakeContour`.
- `GetOpStockFace 0x1801e9e10`: top = TopTurret (`LATHETOOLCLASS::TopTurret 0x18015cd80` = `is_turret_top(pclsLTool+0x761)`,
  mccore: value in {1, 3, 4}). top: op+0x56a == 0 -> (op+0x1210 ? 1 : -1), else (op+0x1210 ? -1 : 1); not top: op+0x56a != 0 ->
  (op+0x1210 ? 1 : -1), else (op+0x1210 ? -1 : 1). **pclsLTool's turret is taken as the op's op+0x540**: `SetActiveTurret`
  writes +0x761 (tool dialogs, `MakeFromMillTool` from tool +0x260); what loads the op's tool into pclsLTool before bnci_lface
  is NOT TRACED - **CORRELATED** (6.7a): millturn op 1 has op+0x540 = 1 (top) -> -1 -> the face at x 0.982 = the stored NCI Z.
- `InitVariables` signs: geom+0xc0 + geom+0xb0 <= 0: this+0x98 = op+0x56a == 0 ? (op+0x1210 ? -1 : 1) : (op+0x1210 ? 1 : -1),
  this+0xa0 = op+0x1210 ? -1 : 1; else this+0x98 = op+0x56a == 0 ? (op+0x1210 ? 1 : -1) : (op+0x1210 ? -1 : 1), this+0xa0 =
  op+0x1210 ? 1 : -1 (mt 1: -1 / -1).
- `GetToolEdgeComp 0x180044440` + `TestFrontBackEdge 0x180044a10` on P3 = (b8, b0), P4 = (b8, c0) give the front / back edge
  flags (orientation masks 0xb4 / 0x14a; ported literally); a flag adds ltool_rec+0x40 (front) / +0x50 (back) to the pass x and
  +0x48 / +0x58 to the end y. mt 1: both 0.

### 6.4 Passes
- Contour of one pass: rec 1 = (b8, b0), rec 2 = (b8, c0) (FinishStrategy0 / RoughStrategy0), + edge x. CompFinish
  0x180202f60 / CompRough 0x1802049d0 (offset): op+0x568 == 4 -> unchanged; else `CLToolComp::lthcc0 0x1800bc0b0` ->
  `LatheCutterComp0 0x1800b74e0` ('F'): coincident records deleted (one left: fails), `ccomp_pock_from_ctour(|r| + min(stock X,
  stock Z))` to side 'L' (op+0x56a 0) / 'R' (1), r = ltool_rec+8 (0 when op+0x568 == 1); **CORRELATED** (6.7a): ccomp_pock is not decoded -
  for the 2-record line the offset is the line moved to the left (L) / right (R) of its direction (millturn op 1: the line
  (0.982, 0.8365) -> (0.982, 0) moved +0.03125 in x: NCI Z 0.982 = 1.01325 - 0.03125). `ShiftCtourInfo` only when stock X !=
  stock Z (never: both = the offset). CompRough with op+0x56b and offset > tol: lthcc0(r 0, offset) then lthcc0(r, 0) - the
  same total for one line.
- Ends: rec 1 y += -this[0xa0] * r (twice when op+0x568 == 1), += -this[0xa0] * prm+0x30; rec n y += this[0xa0] * prm+0x48
  (+ the edge y).
- `FinishStrategy0`: k = 1..prm+0x20 at offset (n - k) * prm+0x18 + prm+0x28.
- `RoughStrategy0`: allowance A = prm+0x28 (+ prm+0x20 * prm+0x18 when finish is on); span = |b8 - a8| - A; span < -tol -> no
  rough pass; < tol -> 0; step = prm+8: span >= 0.9 step -> n = trunc(span / step), +1 when span - n step > tol, at least 1,
  passes of span / n; else one pass. Pass k at offset (n - k) * (span / n) + A.
- Stock clipping `GetStockStartEndHeight 0x180044240` returns 0 when op+0xb68 == 0 (nothing changes); the stock case is refused.

### 6.5 NCI written per pass
- `OutputOnePass 0x180044760`: the contour moved by -(ltool_rec+0x30, +0x38) when op+0x568 is computer / wear / reverse wear
  (`(cc & ~3) == 0 && cc != 1`), plus `world_to_view(op+0x720)`; `enter_contour 0x18003f7d0` (op+0x888 == 0: no vectors -
  `add_rapid_from_prev_pt`, then `wr_lin_move(gcode fmt+0x293e or 0)`: G0 to rec 1); `wr_lin_move(1, rec i, op+0x310, flags 1 for
  rec 2 / 2 for the last)` for i = 1..n (rec 1 again: shorter than tol, not written); `exit_contour 0x180040080` writes nothing
  without op+0x888. Then the retract: |prm+0x38| > tol -> `wr_lin_move(this+0xb0, (prev.x - this[0x98] * prm+0x38, prev.y),
  op+0x310)`.
- `CLNciFmt::wr_lin_move 0x1800c27c0`: not written when shorter than tol unless the last record is the tool change (fmt+0x292e in
  1000..1002); gcode 0 / -1 / -3 / -4 -> G0 (RapidFeedrate), others G1; gcode not 1 / -1, fmt+0x293c clear and both coordinates
  changing by tol or more -> two moves: via (prev.x, p.y) for -3 / -5, or when `GetPlungeDirection 0x1800bcb30` (opcode 0x42:
  stock face != -1 ? 3 : 2) is 2 and p.x <= prev.x / 3 and prev.x < p.x; else via (p.x, prev.y). `gr_lin_move 0x1800bf810`:
  coordinates within tol / 10 of the previous point's snap to it; shorter than tol not written (unless after the tool change).
- NCI point (`XY_to_lathe`, mccore, not decoded): measured on millturn op 1 (view Top, tool plane rows (0,1,0) (0,0,1) (1,0,0) at
  the origin, plane id 9 "LATHE UPPER LEFT 1"): NCI (X, Y, Z) = (y, 0, x) - the tool plane's rows applied to world (x, y, 0).
  X is the radius here (stock diameter 1.673: 0.8365 + lead-in 0.1 = X 0.9365). Other views / planes refused.
- NOT DECODED, taken as measured (**CORRELATED**, 6.7a: the first move): the first entry's `add_rapid_from_prev_pt` home branch (fmt+0x2940 set by lwr_sof: home
  position `get_lhome_pos_sys`, `calc_lathe_ref_pt`, `add_rapid_pts`, `WriteHomePosPts`, components op+0xb3c) - millturn op 1
  writes nothing there (the G0 to rec 1 follows the tool change, whose first position is rec 1); fmt+0x293c (prev point = home)
  taken as never true for the later entries; `SendLToolHome`'s components branch (`ComponentsDefined` -> `WriteHomePosPts`) -
  millturn op 1 ends at the retract. The tool change / -2 / 20000 records (`lwr_sof`, `LATHETOOLCLASS::SetBNciInfo 0x180156570`)
  are not written here (moves only).

### 6.6 Port (viewer/lathe_face.c) and refusals
Generates for: exactly two face points, or op+0xb68 with a left-spindle stock (section 7.3, since L-6); op+0x568 0 or 4; op+0x888 0; corner off; op+0x32e 0;
no finish spindle override; finish feed override plain or 'R'; op+0x292 0; Top view at the origin and the measured tool plane;
the 2017 record (0x2938). Everything else refuses with a NOT DECODED / NOT PORTED message. Moves are world (Z, X, 0) with
CamParams.tplane = 5 (the same rows) so `cam_cli fileop --moves` / the app show them in the lathe XZ plane; the G1 feed = the
NCI value (negative = per rev). No arc filter / smoothing (BNCISection::PostProcess for lathe NOT DECODED). The app: Regenerate
(cui_dirty.c) takes it over like any stored op; its parameter dialog, edit-all, the tool manager's regeneration and the save
(mcam_opcode_for: no opcode, "type not exported yet") leave it alone / refuse; the LinuxCNC post refuses any lathe op (nc_post.c,
the `op_is_lathe` rule @1800c6060), stored or regenerated.

### 6.7 Oracle (tools/tests/lathe_face_test.sh)
- millturn.mcam_8 op 1 (the only stored Face): 3 moves G0 (X 0.9365, Z 0.982), G1 (X -0.03125, Z 0.982) F-0.012, G0 (X -0.03125,
  Z 1.082); ours the same 3 moves and kinds, max deviation 2.2e-15 (p95 the same).
- Self-consistency (record edits, NOT oracle matches): 3 finish passes of 0.01, rough on with 2 passes + finish, overcut / stock /
  lead-in / retract at feed, compensation off, no retract - the moves follow 6.4 / 6.5 as written out in the script.

### 6.7a CORRELATED (decision L-3: kept, matched to millturn op 1, NOT traced)
Three behaviours of the port rest on the one stored Face, not on traced code. They are marked CORRELATED at their code sites
in viewer/lathe_face.c and stay until traced or confirmed by more Face NCI.

| behaviour | port | what is not traced | trace next |
|---|---|---|---|
| face side (GetOpStockFace's TopTurret) | the op's own turret byte op+0x540 (ltool_rec+0x88) stands for pclsLTool+0x761 | what loads the op's tool into pclsLTool before bnci_lface | `FixLatheToolOrigin` (mccore, bnci_lface's first call) and the callers of `LATHETOOLCLASS::SetActiveTurret 0x180155790` |
| first move | straight G0 to the first pass start | the rapid from the home position add_rapid_from_prev_pt can write after lwr_sof (fmt+0x2940) | `CLNciFmt::add_rapid_from_prev_pt 0x1800bdc90` home branch: `get_lhome_pos_sys`, `add_rapid_pts`, `TOOLPATHOPCLASS::WriteHomePosPts` |
| compensation of the face line | the 2-record line moved sideways by the distance, to the left ('L') / right ('R') of its direction, ends not extended | mccore `ccomp_pock` (the pocket offset ccomp_pock_from_ctour calls) | `ccomp_pock` (mccore, after `ctour_to_pock_bound` in `ccomp_pock_from_ctour 0x1805d5e50`) |

### 6.8 NOT DECODED (Face)
- (The face from the stock: decoded and ported since, section 7.)
- Corner strategies (RoughStrategy1..5 0x180205430.., FinishStrategy1..3, `CompToolCenterRad 0x180044060`, MakeContour use),
  entry / exit vectors (`rotate_tp_vec`, `calc_arc_pts`, `calc_manual_ee_vector`, `CalcEEPoints`), cc in control / wear codes,
  `feedrate_from_surface_finish`, the spindle override record, the home / reference-point moves (6.5), `XY_to_lathe` for other
  planes, `ccomp_pock` beyond a single line, the parameter pages (CLFaceProp), the lathe NCI records and a lathe post.

## 7 New Lathe Face: what Mastercam does (L-6 stage 1, 2026-11-16)
Status: DECODE (7.1-7.5) + engine PORT (7.6: face from the stock, the lathe stock, a new op from the defaults record through
cam_cli). No UI (stage 2). Sources: `work/lathe/dump/mclathe_all.c` (mclathe.dll), `work/lathe/mdef/mdef_all.c` (mcmachinedef.dll,
every function: DumpDecomp of `~/mastercam-re/ghidra_mdef`, -readOnly), `decomp/mccore_ops.c` (InitOperation), disassembly
`work/lathe/dump/fs0_dis.txt` / `rs0_dis.txt` (FinishStrategy0 / RoughStrategy0). Record offset = op offset + 0x68.

### 7.1 The command: a new Face op
- `lathe_face 0x1801a3290` (the ribbon / menu command): `IsLatheOpSupported(1, 0x42)`, `MtJobSetupComplete(1)`,
  `CheckInsertInLockedGroup`; `newOp = 1`; `init_operation(op, 0x42, 1, ok)` (mccore InitOperation); `lathe_toolpaths_start(op id)`;
  `FUN_180045f90` = CLFaceTp ctor + `CLFaceTp::lface 0x180046000` -> `edit_lathe_operation` (the dialog, 7.4) -> on OK
  `UpdateListAndDB` + `bnci_section` (the NCI: `bnci_lface`, section 6); cancel / failure -> `DeleteListAndDB`. **No chaining**: the
  face geometry is the stock (op +0xb68 "Use stock") or two picked points (op +0x88 = 2, "Select points").
- InitOperation (decomp/mccore_ops.c): memset 0x28d0, `set_op_prm_defaults`; with an empty op list (or "read defaults")
  `default_ops_get(0x42, op, group)` = the first Face of the machine group's defaults file (the lathe control's OpDfltsFile;
  lathe.mcam_8 names `%MCAMDIR%LATHE\OPS\LATHE_INCH.defaults-9` = `shared Mcam2017/lathe/Ops/LATHE_INCH.mcam-defaults`, op 56;
  LATHE_MM op 2); else `list_op_get_prev` (the newest op of the same opcode in the active machine, kept when config 17028 "Get
  defaults from previous operation?" = y; contour_3d_tabcutoff.md) - its lathe branch NOT DECODED. Lathe-only steps:
  `FUN_1800b6f00(0, &previous_op_idn)`, `reset_lathe_op_prm_defaults 0x1801d3170` (op +0xb70 / +0xb78 = group +0x1698 / +0x16a0,
  op +0xb3c = op +0xb3a = 1; for 0x42 op +0x1218..+0x1357 (the entry / exit vectors) := mclathe .data 0x1804b3550, 0x140 bytes),
  `AddWCSOriginToPt3D(op +0x870)`, the active planes (`InitOperationViewsToActivePlanes`); `op_is_no_refpt` clears the reference
  points. `lathe_toolpaths_start 0x18019fe10`: `LATHEPROJECTCLASS::GetPrvOpBdrys(op id)` (the stock the op sees, 7.2),
  `set_prev_tool_info`, `BackupComponents`.
- The defaults record (LATHE_INCH op 56, measured): op +0xb68 = 1 (Use stock), op +0x88 = 2, no points, Finish Z op +0x13b8 = 0,
  finish on (1 pass of 0.01), rough off, its own tool T5 (ltool_rec r 0.03125, reference point (0.03125, 0.113379)), turret 1, the
  planes of millturn op 1 (Top view, the lathe tool plane; plane id 54 in the defaults file).

### 7.2 The lathe stock (machine group components, LBDRY records)
- **Part file**: the machine group record (op_group, mcam_grp.h) holds six component boundary ids at +0x16ac, +0x16b4, +0x16bc,
  +0x16c4, +0x16cc, +0x16d4 (stride 8; `LATHEPROJECTCLASS::GetComponentsFromDb 0x18010d120` local_78[0..5]); each names an LBDRY_ID
  record (ASSOC subtype 10) by ent id. lathe.mcam_8 group 1 (+0x19e8 = 1): 75 / 76 (left / right stock: 4-line rectangles), 77 / 78
  (left / right chuck); millturn.mcam_8 group 2: 1816 (left stock), 1817 (left chuck). k0 / k1 = the left / right stock:
  `StockDefined 0x1801114c0` = PolygonDefined(0 / 2) and GetFrom_lbdry's stock rule (index 0 -> LeftSpindle, 2 -> RightSpindle); the
  index table `bCompBdrys` itself (mcmachinedef .bss, set at run time; its initialiser was not found) is read from those as {0, 2,
  ..}, with `local_60` = {1, 3, 5, 7, 9, 11} the mirror indices. k2 / k3 = the chucks (record shapes); k4 / k5 empty here.
- **Every lathe op keeps its own copies** at op +0xb40, +0xb44, +0xb48, +0xb4c, +0xb50, +0xb84 (GetComponentsFromDb from an op): the
  stock AFTER the op (measured: millturn op 1 Face 2120 = the group's 1816 with the face moved from Z 1.007 to 0.982; lathe.mcam_8
  op 1 / op 2 Rough: 170 / 174 = the turned profiles). The LBDRY header's +8 = the owner op id (0x7fffffff = the group).
- `lathe_bdry` (LBDRY ent +0x68; `BOUNDARYCLASS::GetFrom_lbdry 0x1800346d0`): +0 i32 entity count, +4 u8 boundary index (0 / 2),
  +5 u8 valid, +8 i32 owner op, +0x28 f64 1000, +0x90 the entities (`tp_ent`, 0x78 each: +0 i32 type 2 line / 4 arc, +0x10 p_3d start,
  +0x28 p_3d end; arcs (a_3d): +0x40 centre, +0x58 radius, +0x60 start angle, +0x68 sweep (rad), view +0x70 = 1). x = lathe Z, y =
  lathe X (radius), times 1000: GetComponentsFromDb scales polygon k into k + 0x1b by 1 / MathScale (`Scale 0x18003b060`,
  `ScaleBdryEnt 0x180046c20`; MathScale = BOUNDARYCLASS +0x18 = 1000, `InitTolerances 0x180037620`) and mirrors it (about y = 0) into
  its mirror index + 0x1b. So **polygon 0x1b = the left stock, 0x1c its mirror, 0x1d / 0x1e the right stock**.
- **Which stock a new op sees** (`GetPrvOpBdrys 0x18010dc40` -> `GetPrvOpBdrysOpIdn 0x18010deb0`): the active lathe group's ops
  (GetOpsInGroup) from the last to the first; after the op itself (a new op is already in the list, at its insert place) the first
  op with lathe boundaries (`OpHasLatheBoundaries 0x18010f890`: op +0xb40 / +0xb44) -> its stored copies; a Stock Model op with lathe
  stock goes to a second slot (`IsStockModelOpWithLatheStock`); none -> the group's (0x7fffffff). lathe.mcam_8: a new Face after op 2
  faces op 2's stock 174. `GetCurOpBdrys 0x18010d770` (error_return, edit_lathe_operation) does the same from the insert location.
- `GetOpStockBoundary 0x1801e9d60`: `OpActiveSpindle(op)` (mcmachinedef 0x1800f7b50: the axis combination op +0x11bc -> its part
  group's ActiveSpindle; the machine definition NOT DECODED) 0 -> YStatus(0x1b) with TopTurret: top -> 0x1b + (status != 1), bottom
  -> 0x1b + (status != -1); 1 -> the same on 0x1d; `PolygonDefined` 0 -> 0xff (no stock).
- BOUNDARYCLASS queries (mcmachinedef): `MinX 0x180039150` / `MaxX 0x180038cf0` / `MinY` / `MaxY` (raw: last argument 1) = the extents
  `SetExtents 0x18003b710` keeps (BdryEntMinX / MinY / MaxX / MaxY over the entities; arcs through break_arc_3d); `YStatus
  0x18003d790` (the entities' min / max y, each beyond tol votes its sign; mixed -> 0); `GetMaxY 0x180034fb0` / `GetMinY 0x180035db0`
  (bdry, x1, x2, limit, out, raw): the greatest (least) y below (above) the limit of the boundary's intersections with the vertical
  lines x = x1 and x = x2 (`IntersectLine 0x180037730` -> `IntersectLineBdry 0x180045e80`, the line from MinY - 1 to MaxY + 1; only
  x1 when |x1 - x2| <= tol), plus (two lines only) each entity's max (min) y point (`BdryEntMaxYPt 0x180040110` / `BdryEntMinYPt
  0x180040750`: a line's end with the greater (lesser) y, the second end on a tie; an arc's break_arc_3d point) with x in [x1, x2].

### 7.3 Face from the stock (`CFaceTpGeometry`, op +0xb68 = 1)
- `GetAllGeometry 0x1801e8990`: op +0xb68 == 1 and GetOpStockBoundary != 0xff -> `SetMaxMinPtsFromOp`, `GetFaceBoundaryFromStock(op,
  1, 1)` (it rewrites the op's two point entities), re-read; op +0x88 != 2 -> the same pair again (it returns at once when op +0xb68
  is 0); then the point path of 6.3. No stock and no points: the FaceGeometry.cpp 0x4ff assert (fails).
- `GetOpXStockLimits 0x1801e9e70` (polygon 0x1b..0x1e): stock face (GetOpStockFace) 1 -> [MinX, MaxX - 0.002], else [MinX + 0.002,
  MaxX] (0.002 in either unit); no polygon -> (-1e30, 1e30).
- `SetMaxMinPtsFromOp 0x1801eac20` (geom +0x88 / +0x90 / +0x98 / +0xa0; the ctor 0x1801e8450 sets +0x88 = +0x90 = 0, +0x98 = +0xa0 =
  1e30): with fewer than 2 op points, +0x88 is kept when inside the limits, else the near end (op +0x56a 0: hi) / the far end (1: lo);
  +0x98 = hi (0x1b / 0x1c; 0x1d / 0x1e: lo); +0x90 / +0xa0 = MinY / MaxY (0x1b, 0x1d) or MaxY / MinY. With points: +0x88 = the
  smaller x (stock face -1) / the larger (1), +0x98 the other; +0x90 = the y of smaller |y|, +0xa0 the other. Then +0x88 clamped.
- `GetFaceBoundaryFromStock 0x1801e9410` (op, param_2, param_3): param_2 and op +0xb68 == 0 -> return. op +0xb68 = 0 (copy); no
  polygon -> with param_2 and op points: op +0x88 = +0xd4 = 2, the points kept; else fail. param_3 0 -> +0x88 clamped to the limits;
  1 -> SetMaxMinPtsFromOp. +0x98 = MaxX (stock face -1) / MinX (1). Over [min, max] of (+0x88, +0x98): top turret -> +0xa0 =
  GetMaxY(.., 1e30), +0x90 = GetMinY(.., -1e30); bottom -> the other way. Points A = (+0x88, +0x90), B = (+0x98, +0xa0) ->
  view_to_world(op +0x6d2) + op +0x720; when they differ from the op's (`GetFaceTpPoints`, `FaceTpPointsEqual`): `DeleteOpPts`, two
  new point entities (`create_point_ent`, `init_tp_ent(.., 1, 1, 'P', ..)`, `store_ent_assoc`); op +0xb68 = 1, op +0x88 = +0xd4 = 2
  (op +0x94 set when replaced), `UpdateListAndDB`. So the face runs at x = +0x88 (the finish Z) from the stock's outer y to its
  inner y there; +0x98 = the stock face (rough passes span the two).
- **Stock clipping per pass** (`GetStockStartEndHeight 0x180044240`, from FinishStrategy0 / RoughStrategy0): op +0xb68 on; YStatus of
  this[0x93] 1 -> start = GetMaxY, end = GetMinY over [x, xprev] (swapped with cut away from the centre, prm +0x50); -1 -> the other
  way; 0 -> nothing; start not found -> no change. The pass's first / last record y += (start - rec1.y) / (end - recn.y), after
  CompFinish / CompRough, with the lead-in / overcut terms of 6.4. Probe x: finish `((n - k) * step + prm+0x28 - tol) * -this[0x98] +
  rec1.x`; rough `((n - k) * step - tol + prm+0x28) * -this[0x98] + rec1.x` (the finish allowance NOT included); xprev = the previous
  pass's probe x, the first pass's own for the first - from the disassembly (XMM10 / XMM12 reloaded after each call at 0x1802032b6 /
  0x180205183; the decompiler merged that register with the y delta).
- NOT TRACED (taken as the geometric result within tol, lathe_stock.c): mccore `ln_ln_ints2` / `line_arc_ints` / `AddIntPt` (the
  intersections) and `break_arc_3d` (an arc's ends + quadrant points). CORRELATED (not traced): OpActiveSpindle taken as 0 (left) -
  millturn op 1's own stock copy (2120) is component k0 with its face moved; the TopTurret of pclsLTool = op +0x540 (6.7a).

### 7.4 Face dialog (decode only; control -> field table: formats/face_dialog_fields.tsv)
Source: `work/lathe/dump/mclathe_all.c` (mclathe.dll), templates `exports/mclatheres_dialogs.tsv`, strings
`tools/pe_strings.py mclatheres.dll`. op +X = record +X+0x68.

**Sheet.** `edit_lathe_operation 0x1801c7480` builds a `CLTpSheet` (1801e13b0). For opcode 0x42 the tool page template is
`toolpg_IDD` 0xf47 = **3911** "Tool parameters" (0x193a = 6458 when `GetProdEnableBits() & 0x100`, the Mill-Turn product;
edit_lathe_operation switch). `CLTpSheet::InitTabPages 0x1801e4170`: pages in order (1) the tool page (`CLToolProp
0x1801a62d0`, tab title string 0x82d = 2093 "Toolpath parameters"; `CLToolPropMt` in Mill-Turn), (2) `CLFaceProp
0x180041170` = template 0x14f1 = **5361** "Face Parameters"; the sheet opens on the Face page when op+0xf4 == 1, else on
the tool page. No other pages (coolant, planes, reference points, misc values, canned text are buttons of the tool page).
For a new op (`newOp`) the sheet ends with `UpdateCtrlData(true)`.

**Face page controls** (`CLFaceProp::DoDataExchange 0x180041810`, `OnInitDialog 0x1800426d0`, `MapToOp 0x180042070`; edit
values are the MCReal / MCInt member +0xc00 / +0xbf8): Entry amount 795 -> op+0x11f0; Rough stepover check 809 -> op+0x11c0,
edit 787 -> op+0x11c8; Finish stepover check 811 -> op+0x11d0, edit 796 -> op+0x11d8, Max finish passes 793 -> op+0x11e0
(i16, 1..32000, < 1 forced 1); Overcut 800 -> op+0x1208; Retract 802 -> op+0x11f8; Rapid retract 929 -> op+0x1200; Stock to
leave 790 -> op+0x11e8; Cut away from center line 1018 -> op+0x1210; Comp type combo 852 -> op+0x568 (init: 0..3 kept, other
-> 4); Comp direction combo 846 (788 Left / 791 Right) -> op+0x56a; Roll cutter combo 908 (813 None / 814 Sharp / 815 All) ->
op+0x56b (init min(v, 2)); Lead In/Out check 816 -> op+0x888 (button 820 -> dialog 2425); Filter check 1413 -> op+0x2d2
(button 1414); Corner check 5383 -> op+0x1358 (button 5368 -> CFaceCornerDlg). Edit minimums (MCReal init, OnInitDialog):
0 for the stepovers, retract and entry; -1e30 for stock to leave and overcut. Mutual rule (`OnRoughStepCb 0x180042d40` /
`OnFinishStepCb 0x1800425e0`): clearing one of rough / finish while the other is off turns the other on.

**Face geometry source.** Radios 1339 "Select points" (index 0) / 1340 "Use stock" (index 1) -> page+0x7f68 -> **op+0xb68 =
(index != 0)** (MapToOp, OnOK 0x180042c40). `SetRestCtrls 0x180043710` (called by OnSetActive and OnOK):
- new op (`newOp`), Z not yet edited (page+0x847c == 0) and `FindPartFace 0x180041ba0` finds a part face (machine group ->
  `MTPg3Params::GetPartFaceZ`, != 1e30; mill-turn machines from MC extensions only): Finish Z = wcsX_to_latheZ(face) when inside
  the edit range, op+0xb68 = 1;
- "Use stock" (1340) is enabled only when `CFaceTpGeometry::GetOpStockBoundary` != 0xff (a stock polygon 0x1b..0x1e exists);
  op+0xb68 == 0 with no op points and a stock polygon -> op+0xb68 = 1; no stock polygon -> op+0xb68 = 0;
- op+0xb68 1: Select Points (1338) off, Finish Z button 1343 and edit 1342 on, edit range = the op's stock X limits
  (`GetOpXStockLimits`) converted with wcsX_to_latheZ, +-tol, the value snapped to an end within 1e-7 and rounded to n_places.
**Finish Z** (edit 1342) = **op+0x13b8** (lathe Z in tool-plane coordinates, MapToOp); button 1343 (`OnSelectZPb 0x180042ea0`):
prompt string 6473 "Select finish Z position.", point_proc_2d, `XY_to_lathe(op+0x6d0, op+0x570)`, clamped into the range.
`InitZFinEd 0x180041d90`: SetMaxMinPtsFromOp + GetOpXStockLimits, value = wcsX_to_latheZ(geom+0x88) (or the given Z).
**OnOK 0x180042c40**: op+0xb68 from the radio, SetRestCtrls, MapToOp; when op+0xb68: geom+0x88 = latheZ_to_wcsX(Finish Z);
then `GetFaceBoundaryFromStock(op, PointsDefined, 0)` - param_3 0: geom+0x88 is clamped to GetOpXStockLimits (not reset by
SetMaxMinPtsFromOp) and the two face point entities are (re)made from the stock at that X (section 7.3). So the face of a
"Use stock" Face is **Finish Z (op+0x13b8)**, defaulting to the machine group's part face (new op) - the defaults records
hold 0 there (LATHE_INCH op 56, LATHE_MM op 2, millturn op 1: op+0x13b8 = 0.0, measured).
`FacePgPrm::UpdateCtrlData 0x1801b5d10` (runs on every tool change through CLTpSheet::UpdateCtrlData): new op with Z not
edited -> Finish Z := 1e30, `DeleteOpPts`, geom +0x88..+0xa0 := 1e30, InitZFinEd(op+0x13b8), SetRestCtrls.
`FacePgPrm::UpdateCutParams 0x1801b6750`: for tool types 0 / 3 / 50 / 53 (`CutParamToolType`, mask 0x24000000000009) the
insert's (`GTBINSERTCLASS` at LATHETOOLCLASS+0xad8) RghDOC -> op+0x11c8, FinDOC -> op+0x11d8, XOvercut -> op+0x1208, Retr ->
op+0x11f8 (unless the op is canned). `FacePgPrm::SetDefaultCC 0x1801b5460` (on a tool orientation change): comp direction
op+0x56a = (op+0x1210 != 0) for a right-hand tool on the top turret or a left-hand tool not on it, else (op+0x1210 == 0);
then `SetEEVectors(op+0x1218, op+0x1288)`.
NOT DECODED: the comp type combo's item texts (CCutterCompComboBox vfunc +0x300), the Lead In/Out (2425), Filter and Corner
sub-dialogs, `MTPg3Params::GetPartFaceZ` (mccore/mcmachinedef: which group field is the part face), SetEEVectors,
`ToolPrmBase::ToolOrientChanged`, the GTBINSERTCLASS DOC fields of the lathe tool records.

### 7.5 Lathe tools (decode only)
- **Tool page** `CLToolProp` (template 3911) on `CLToolPropBase` / `CLtlPgCmn` (page+0x2a980). The list (830) is the part's
  lathe tools from the tool system (`TlMgr::GetLatheTools`, `CLtlPgCmn::InitToolList 0x1801ac400` / FillListCtrl); with "Show
  library tools" (2229, page+0x22370) the group's lathe tool library is read too (`ToolLibrary(group)` ->
  `crib_list_ltool_list`, tools not in the part added with `list_ltl_add`). Lathe tools in the part: LTL_ID (assoc subtype 8,
  LATHETOOL 0xa48) records (lathe.mcam_8: 2, idn 90 / 120; millturn.mcam_8: 6) loaded through `TlMgr::Add(LATHETOOL)`
  (formats/mcam_write.md "How a tool gets into the part's TlMgr"). The library named in lathe.mcam_8 / millturn.mcam_8:
  `%MCAMDIR%Lathe\Tools\Lathe_Inch.tooldb` (file offset 0x711a9 / strings) = `shared Mcam2017/lathe/Tools/lathe_inch.tooldb`
  (SQLite: TlToolLathe 76 rows, TlToolLatheISO 76, TlToolLatheCustom 0, inserts in TlInsert* tables; lathe_mm.tooldb, plus
  legacy Lathe_Inch/MM .LIC / .LHC catalogs). The lathe tables are not read by viewer/toollib.c (formats/tooldb.md is mill only).
- **Default tool of a new Face** (`CLToolPropBase::OnInitDialog 0x180263a50` -> `CLtlPgCmn::InitLTool 0x1801abdc0`): newOp ->
  `FUN_1801c9700`: the last lathe op of the **same opcode (0x42)** before the new op in the op list; its tool (op+0x544 slot)
  is selected. None -> list row 0 (`this+0x2b724 = 0`, the first row in the list's sort order; GetSortOrder default NOT
  DECODED). Empty list -> no tool (fLToolDefined 0); what OK does then is NOT DECODED.
  `MapToDlg` -> `CLtlPgCmn::GetToolProperty 0x1801ab7f0` (only when newOp): `LATHETOOLCLASS::SetInfo(tool)` (pclsLTool),
  `SetLToolAngles`, **`Make_op_tool(op+0x2e0)`**, op+0x520 / +0x528 / +0x530 / +0x531 = pclsLTool +0xbc8 / +0xbd0 / +0xbd8 /
  +0xbd9; the axis combination from the tool (`AxisComboFromLatheToolInfo`), op+0x11bc = its id, `SetOpDefaultTplane`,
  active spindle / turret / mode / stream from the axis combination (mcmachinedef, NOT DECODED); op+0x868 == 2 -> home
  op+0x870 = `GetHomePos`. So the defaults record's embedded tool (LATHE_INCH op 56: T5) is replaced by the selected part tool.
- **`LATHETOOLCLASS::Make_op_tool 0x180152060`** (op_tool_info = op+0x2e0, 0x288 bytes, cleared except +0x268): +0 tool number
  (op+0x2e0), +4 tool type (op+0x2e4), +0x10 nose radius (= ltool +8; drills: dia / 2 or insert radius), +0x2c offset
  (op+0x30c), +0x30 fast feed (op+0x310, negated for 'R' per rev), +0x38 slow feed (op+0x318), +0x48 rpm (op+0x328, negated
  when not CCW), +0x4d CSS (op+0x32d), +0x50 coolant (op+0x330), +0x56 name (op+0x336, 0x78), +0x258 metric (op+0x538),
  +0x25c station (op+0x53c), +0x260 turret (**op+0x540**), +0x261 spindle (op+0x541), +0x264 slot (op+0x544), +0x270 = 0x100,
  +0x274 = active machine group id (op+0x554); **+0x1d8 ltool_rec (op+0x4b8)** = `Get_ltool_rec 0x18014f920` (orientation,
  corner radius, width, height, angles deg->rad from +0x818 / +0x820, tool centre) then `Rotate_ltool_rec` and
  `Xlate_t_center` (NOT DECODED: they set the ltool_rec +0x30.. reference point the engine uses).
- Tool page -> op on OK (`CLToolPropBase::MapToOp 0x1802624a0`, CLToolProp::MapToOp 0x1801a6de0): Make_op_tool again for the
  selected tool, then the edits: tool # 182 -> op+0x2e0, offset 188 -> op+0x30c, station 1417 -> op+0x53c, rpm 231 -> op+0x328
  (sign rules in the TSV), CSS radio -> op+0x32d, max rpm 237 -> op+0x548, feed 224 + units radio -> op+0x310 (in/rev
  negative) / op+0x32e (micro-in), Face finish feed 5190 / 5191 / radio -> op+0x139a / +0x13a0 / +0x13a8 ('R' or 'M'), finish
  spindle 5195 / 232 / radio -> op+0x13a9 / +0x13ac / +0x13b0, comment 260 -> op+6, home combo 1372 -> op+0x868, force tool
  change -> op+0x289, to batch -> op+0x291, stock update -> op+0xb3c, tool display -> op+0x930.
- NOT DECODED: LATHETOOL (0xa48) field layout / `LATHETOOLCLASS::SetInfo`, Rotate_ltool_rec / Xlate_t_center, the list sort
  order, `ToolLibrary(group)` field, "Select library tool..." and the tool-definition dialogs, axis-combination rules.

### 7.6 Port (stage 1, engine only) and checks
- `viewer/lathe_stock.c` (new): LBDRY -> polygon 0x1b (`ls_from_lbdry`: GetFrom_lbdry + Scale), extents, YStatus, GetMaxY / GetMinY,
  and `lathe_op_stock` (GetPrvOpBdrysOpIdn + GetComponentsFromDb component 0). `viewer/mcam_load.c` keeps the LBDRY records
  (`McamFile.lbdry`) and every group's component ids (`McamFile.lgrp`).
- `viewer/lathe_face.c`: GetAllGeometry's stock branch (SetMaxMinPtsFromOp, GetOpXStockLimits, GetFaceBoundaryFromStock: the bnci path
  and the Face page's OK path), GetStockStartEndHeight in both strategies; `LfIn.made` returns the two face points it made (for the
  save, stage 2). Refused: the bottom turret / mirrored polygon 0x1c, the right spindle, a stock arc shorter than tol (ScaleBdryEnt's
  min_arc_length rule), Stock Model ops with lathe stock, boundary entities other than lines / arcs.
- `viewer/cam2d.c gen_lathe_face`: a new op (no file op) = the lathe defaults Face record (`cam_lathe_default_record`, cam_fileop.c:
  shared lathe/Ops/LATHE_INCH|MM.mcam-defaults, or cam_cli `--lathe-defaults FILE`) + record patches; the stock of `lathe_op_stock`;
  the Face page's OK path with Finish Z = op +0x13b8 (latheZ_to_wcsX from the measured plane, 6.5: wcs x = Z). Refused: a part that
  already has a Face (InitOperation's previous-op copy; lathe branch not decoded). Kept from the record, NOT as Mastercam (stage 2):
  the tool (Mastercam: Make_op_tool from the part's lathe tool, 7.5), the planes (Mastercam: the active planes), the bytes of
  reset_lathe_op_prm_defaults (7.1), the default Finish Z (Mastercam: FacePgPrm::UpdateCtrlData sets 1e30 for a new op, then
  InitZFinEd; FindPartFace only on Mill-Turn machines - the value the page shows on a plain lathe is NOT DECODED; the record's 0 is used).
- cam_cli: `cam_cli PART lface - [--lathe-defaults FILE] [--rec OFF=TYPE:VALUE ..] --moves OUT` makes a new Face in the active lathe
  group; `cam_cli PART fileop ID [--rp ..]` regenerates a stored one (op +0xb68 now from the stock).
- Checks (tools/tests/lathe_face_test.sh): **SELF-CONSISTENCY, not Mastercam oracles** - no Face-from-stock NCI exists here.
  millturn op 1 with op +0xb68 = 1 gives its stored NCI again (the stock rebuilds the op's own two points: max dev as section 6.7);
  a new Face in lathe.mcam_8 (stock 174 after op 2): G0 X0.95066 Z0 / G1 X0.38662 Z0 / G0 X0.38662 Z0.1 (the face at Z 0 from X
  0.93276 down to the 0.5 bore, in T5's reference point); Finish Z -0.25 + rough on: 3 rough passes of 0.08 + the finish, each start
  X on op 2's 0.647 slope. Refusals: a new Face in millturn (an earlier Face exists), a bottom-turret tool. The Mastercam-made
  reference to ask for: formats/windows_checklist.md 7.
- Stage 2 (UI): the Lathe toolpaths > Face command (7.1: no chaining; the active machine group must be a lathe group), the two-page
  dialog (tool page 3911, Face page 5361; control -> field map formats/face_dialog_fields.tsv, 51 rows), the lathe tool list (LTL_ID
  records, Lathe_Inch.tooldb TlToolLathe) and Make_op_tool, saving the new op with its two point entities (`LfIn.made`) and its own
  stock copies (op +0xb40.., the stock after the op: LATHEPROJECTCLASS::UpdateOpStock - NOT DECODED).

## 8 New Lathe Face: the UI path (L-6 stage 2, L-8, 2026-11-16)
Status: PORT (the menu / ribbon command, the two-page dialog, the tool pick, the planes, Finish Z, the save) - SELF-CONSISTENCY,
no Mastercam-made new Face to compare with (windows_checklist.md 7). Code: viewer/lathe_newop.c (the record), viewer/lathe_tool.c
(Make_op_tool), viewer/lathe_axis.c (axis combination + default Tplane), viewer/core_ui/cui_lface.c (command + dialog),
viewer-qt/src/lfacedlg.cpp (Qt sheet), cam2d.c gen_lathe_face (`CamParams.lf_new`), mcam_saveops.c (the save).

### 8.1 The command
- Toolpaths manager MENU 780 (opmanres) position 1 "Lathe toolpaths" = Rough 3676 .. Face 3680 .. C Axis (pe_menus.py); OnRclick
  greys the popup unless the arrow's machine group type (+0x19e8) is 1 (lathe) (opmanager_menu.md 2). Face 3680 -> FT
  `lathe_face` (decomp/opman_create_map.tsv) = mclathe `lathe_face 0x1801a3290`; the Turning ribbon tab's General gallery "Face"
  is the same command (cmd_labels.tsv `lathe_face` "Create Lathe Face Toolpath..."). Here: Face enabled on a lathe group; the
  other lathe items greyed (not made here); Mill-Turn groups (type 6, menu 1119) refused.
- Refused at the command (lfn_can_start): an earlier Face (InitOperation's list_op_get_prev, lathe branch NOT DECODED); machine
  group +0x11c0 != 0 (ToolPrmBase::CutParamToolType 0x1801b3450 -> FacePgPrm::UpdateCutParams 0x1801b6750 takes the insert's
  GTBINSERTCLASS RghDOC / FinDOC / XOvercut / Retr = this +0x998 block: NOT DECODED); no lathe defaults file.

### 8.2 The record (lfn_build)
InitOperation(0x42): the defaults file's Face (default_ops_get), op +6 cleared (L-7); the defaults file's lathe stock ids op
+0xb40..+0xb50 / +0xb84 cleared (they name LBDRY records of the defaults file); `reset_lathe_op_prm_defaults 0x1801d3170`: op
+0xb70 / +0xb78 = group +0x1698 / +0x16a0, op +0xb3a = op +0xb3c = 1, op +0x1218..+0x1357 := the block
`init_lathe_op_prm_defaults 0x1801d2630` fills at 0x1804b3550 (.bss, zero in the file; disassembly 0x1801d2ab5..0x1801d2b64):
+0x08 -0.1 in / -2.0 mm, +0x38 = 1, +0x58 'S', +0x60 0.02 / 0.5, +0xa0 0.1 / 2.0, +0xd8 = 1, +0xf8 'S', +0x100 0.02 / 0.5, the rest 0;
`op_is_no_refpt(0x42)` (mccore 0x1800c6970) false; `InitOperationViewsToActivePlanes 0x1802b8830` (Cplane +0x6d0, Tplane
+0x570, WCS +0x620 := the active planes, work offset -1); `AddWCSOriginToPt3D(op +0x870)`.

### 8.3 The tool page and the tool pick (CLtlPgCmn::GetToolProperty 0x1801ab7f0)
- List (InitToolList 0x1801ac400 / FillListCtrl 0x1801ab1a0): the part's LTL_ID tools of group 0 or the op's group (body
  +0x9dc), sorted by SortLItems 0x1801b0d20 case 0 (tool number * 10000 + offset); columns 721 Number "T%.2d%.2d", 722 Tool Name,
  723 Insert Info (4701 "R%s"), 724 Turret (4704 Top / 4705 Bottom); widths 56 / 135 / 62 / 44 (ReadToolPageDisplayInfo
  0x180177900 defaults, no registry key). No earlier Face -> row 0 (InitLTool 0x1801abdc0 / FUN_1801c9700).
- Make_op_tool (lathe_tool.c; oracle: lathe.mcam_8 ops 1 / 2 byte-identical 0x288 bytes, millturn ops 1 2 4 5 7 8 equal but
  for the tool page's later edits; tools/tests/lathe_tool_test.sh): SetOpAngles (not B axis: op +0x520 .. +0x531 = 0), Get_ltool_rec,
  Rotate_ltool_rec, Xlate_t_center / GetXlateVector, every getter cited in lathe_tool.c.
- Slot changed -> `AxisComboFromLatheToolInfo 0x180066d60` (lathe_axis.c; the machine = ASSOC 22, combinations ASSOC 26 in the
  ASSOC 15 list, groups ASSOC 13 / params 21 / components 12): op +0x11bc; oracle 11 / 12 stored ops (lathe.mcam_8 ops 1 / 2:
  42 / 48; millturn op 3's drill differs - open); op +0x541 / +0x540 / +0x11b8 from the combination; no rotary 3 / 9 -> op
  +0x520 / +0x528 / +0x530 = 0; id changed -> `SetOpDefaultTplane 0x1801af890`: `PrevTurningOpSpindleReference 0x1802509c0`
  (the nearest earlier lathe op with the same combination: its Tplane id) else the combination's first ACT plane
  (`GetDefaultACTplaneId 0x1802b3ee0`, ACT list = the part planes whose matrix is the combination's AC matrix), `GetACTplanePtr
  0x1802b3e50` -> Plane::Convert into op +0x570. lathe.mcam_8: plane 9 "LATHE UPPER LEFT [TOP] 1" (= both stored ops').
- `FacePgPrm::UpdateCtrlData 0x1801b5d10`: OKToUpdate (op +0x531 == 0), ToolOrientChanged 0x1801b5960 (first call: changed) ->
  SetDefaultCC 0x1801b5460 (op +0x56a) + IntelliSetEEVector 0x1801b4860 (op +0x1218 / +0x1288).
- Tool page -> op (CLToolPropBase::MapToOp 0x1802624a0): tool # / offset / station / feed (|value|, in/rev = negative, micro-in =
  op +0x32e) / finish feed + units ('R' / 'M') / rpm (|value|, the sign Make_op_tool gave) / CSS / finish spindle / max spindle /
  force tool change / to batch / stock update / tool display / comment (op +6) - the bytes of formats/face_dialog_fields.tsv.

### 8.4 The Face page and Finish Z
- Made on its first activation (the sheet opens on the tool page): OnInitDialog 0x1800426d0 -> `InitZFinEd(1e30)` 0x180041d90 on
  a fresh CFaceTpGeometry (+0x88 = 0): SetMaxMinPtsFromOp keeps 0 inside GetOpXStockLimits, else the near / far end (op +0x56a);
  the value snapped to an end within 1e-7; the MCReal range = the limits +- tol (vfunc +0x348(value, min - tol, max + tol, places),
  disassembly 0x180041e09..0x180041fb0 - the decompile drops the value). `SetRestCtrls 0x180043710`: FindPartFace only for MC
  extension machines (FromMCExtensions = machine +0x1c8, InitFromMCExtensionsFile 0x180058790); "Use stock" enabled with a stock
  polygon. So the default Finish Z on lathe.mcam_8 = 0.0 (range -3.998 .. 0 for T0101).
- OK: lf_new 2 (the page was opened: CLFaceProp::OnOK -> GetFaceBoundaryFromStock(op, PointsDefined, 0) at Finish Z) / 1 (never
  opened: no OnOK, the bnci path GetAllGeometry). Point picks (Select Points 1338: GetFaceBoundary; Finish Z... 1343:
  point_proc_2d + XY_to_lathe) are refused with a NOT DECODED message; the sub-dialogs (2425 Lead In/Out, Filter, Corner, Coolant,
  Misc values, Tool display, Stock update, Ref points, Canned text, Library origin, Tool angle, Axis combination, Define home,
  Select library tool, Tool filter) and the comp type / home combos' texts are greyed.
- Refused at OK (gen): the tool's combination on the right spindle (op +0x541 = 1: polygon 0x1d NOT PORTED), plus stage 1's.

### 8.5 The op made here (CamParams.lf_new, lf_slot; project lines `P lf_new` / `P lf_slot`)
The op's record = lfn_build(tool slot) + its planes + record patches (the dialog's bytes that differ from the tool pick's record);
the generator (cam2d.c gen_lathe_face) and the save rebuild it from those, so a project reopens to the same record.

### 8.6 The save (mcam_saveops.c)
The record as built (new op id / ent id / eptr / seq, the comment, OMsetOperationToMachine's +0x160..+0x168, dirty op +0x94, op
+0x11a8 = 0, op +0x88 = op +0xd4 = 2, op +0xb68 = 1, the base's groups), two point entities (create_point_ent) and two TP kind 1
records 'P' (init_tp_ent(.., 1, 1, 'P'), store_ent_assoc; name "unnamed drill point" as millturn op 1's - measured). No NCI (the
op is dirty, as every op saved here). NOT DECODED, not written: the op's stock-after-op copies (op +0xb40.. LBDRY records;
`LATHEPROJECTCLASS::UpdateOpStock 0x180112400` stores the BOUNDARYCLASS polygons 0 / 2 that the stock update computes - that
computation is not decoded); the save's note says so.

### 8.7 Checks (op_compare.sh scenes lface_*)
lface_new (menu, dialog dump, OK, moves, export .mcam, reopen, regenerate: same moves G0 X1.03279 Z0 / G1 X0.46875 Z0 / G0
X0.46875 Z0.1 with T0101), lface_project (project round trip), lface_edits (Finish Z -0.25 + rough 0.08; out-of-range Finish Z
and Select Points refused), lface_nopage (OK without the Face page: the bnci path, same moves), lface_rspindle (T0202's
combination 48 is on the right spindle: refused), lface_mill_grey (BRACKET4: the item greyed, the command refused).

### 8.8 Open questions (deviations to approve)
- "Show library tools" (2229): Mastercam's default is on (ReadToolPageDisplayInfo, no registry key) - the library's lathe tools
  (Lathe_Inch.tooldb TlToolLathe, not read here) would join the list and may tie with T0101 on the sort key (the tool system's
  "already in part" check vfunc +0x170 NOT DECODED). Here: part tools only, the check greyed.
- The stock-after-op copies are not written (8.6); Mastercam regenerating the op would make them.
- Max spindle speed / ValidateSpindleSpeeds: DECODED and PORTED 2026-11-17 (L-12, section 9).

## 9 The tool page's machine checks: spindle speed clamp, the spindle-change check, feed limits (L-12, 2026-11-17)
Status: DECODED + PORTED for a new Lathe Face (viewer/lathe_axis.c la_lathe_rpm_limits / la_combo_spindle / la_machine_feed_limits,
viewer/lathe_newop.c lfn_build_st / lfn_machine_checks / lfn_repick_checks, core_ui/cui_lface.c). Sources: mclathe.dll
(work/lathe/dump/mclathe_all.c; vtables and CToolModWarning from the Ghidra project `lathe`), mcmachinedef.dll
(work/lathe/mdef/mdef_all.c). SELF-CONSISTENCY: no Mastercam-made new Face to compare with; lathe.mcam_8's two chucks give 0 ..
10000 rpm, so a plain new Face is unchanged (lface_new / lface_project / lface_edits / lface_nopage identical).

### 9.1 The machine's limits
- Spindle: `ToolPrmBase::GetSpeedMax(true, 1) 0x1801b36f0` = `CCNCMachine::MaxLatheSpindleRPM(cur_op, NULL) 0x18006b6c0` on
  `ToolPrmBase::Machine 0x1801b4b10` = OMgetActiveMachine; `GetSpeedMin(mode) 0x1801b3760`: mode 0 (CSS) 0, mode 1 (RPM)
  `MinLatheSpindleRPM(cur_op, NULL) 0x18006bb80`. The combination = `AxisCombinationFromID(op +0x11bc, 0) 0x180061250` (an id not
  in the machine -> its combination 0; no combination -> 0x7fffffff / 0). `CAxisCombination::PartComps 0x180017160` (the part
  list, CompListIsPart 0 then 1) -> `MaxLatheSpindleRPM(CObArray *) 0x18006b790`: the first chuck (component Type 0x6a) whose
  `CChuckComponent::MaxSpindleSpeed 0x180092a50` (UParams +4 = component body +0x4a4; UParams 0x180092f20 = Params +0x4a0) is not
  0x7fffffff, none -> 0x7fffffff; Min `0x18006bc40`: `MinSpindleSpeed 0x180092a80` (body +0x4a0), the first other than -1, none
  -> 0. No part list: `PartGroup 0x1800171b0` = GroupByClass(1) for an unmapped combination = NULL -> 0x7fffffff / 0
  (`0x18006b770` / `0x18006bc30`). lathe.mcam_8 / millturn.mcam_8: both chucks 0 .. 10000, every combination 0 .. 10000.
- Feed: `GetFeedMin / GetFeedMax(mode) 0x1801b3630 / 0x1801b3570` (IsMetric): mode 0 per rev `CCNCMachine::OpFeedPerRev
  0x180077520`, 1 per minute `OpFeedPerMin 0x180077500`, 2 -> 0 / 1e30. `OpFeedRates(metric) 0x180077550` = machine this +0x218
  (inch) / +600 (metric) = machine record body +0x140 / +0x180: +0 per-minute (min, max), +0x10 per-rev (min, max). Both parts:
  inch per minute 0.0001 .. 500, per rev 0.0001 .. 100; metric 0.001 .. 12500 / 0.001 .. 1000.

### 9.2 The checks
- `ToolPrmBase::ValidateSpindleSpeed(MCLong &, mode, max, reset, &changed) 0x1801b7a10`: ToolPgPrm +0x34 := max; value <
  GetSpeedMin(mode) -> the min; else value > max in mode 1 -> max; a change -> the field's font := the italic one (ToolPrmBase
  +0x48, made by `InitFonts 0x1801b3af0` with lfItalic 1) and changed; no change with reset -> the plain font (+0x38) if italic.
- `ToolPrmBase::ValidateFeedrate(MCReal &, mode, &changed) 0x1801b77a0`: outside [GetFeedMin, GetFeedMax] -> the bound (italic,
  changed), inside -> the plain font.
- `CLToolPropBase::ValidateSpindleSpeeds(force, reset, &changed) 0x180266830`: force or GetSpeedMax < max -> max := GetSpeedMax
  (field +0x1f9b8, italic font +0x2b780; the plain one +0x2b770), else with reset the plain font; then (opcode not 0xe0 / 0xe1)
  ValidateSpindleSpeed of the rpm (+0x1ec50, mode +0x22364 = (op +0x32d == 0)); case 0x3e / 0x42 / 0x44 / 0xd0 also the finish rpm
  (+0x28960, mode +0x28ad0 = (op +0x13b0 != 1)) - for Face even with "Finish spindle speed" off. `ValidateSpindleSpeeds()
  0x180266810` = (true, true).
- `CLToolPropBase::ValidateFeedrates 0x1802665e0`: the feed (+0x1d068, mode +0x22360: op +0x32e -> 2, else 1 for feed >= 0,
  0 below) and, case 0x42, the finish feed (+0x23ea8, mode +0x26e30 = the radio index; MapToDlg: op +0x13a8 != 'R').
- `CLToolPropBase::RunMachineDefChecks(reset) 0x180265a60`: nothing while page +0x2b790 is set (CLToolProp ctor .. the end of
  OnInitDialog) or re-entered (DAT_1804b641c); MapToOp, ValidateSpindleSpeeds(false, reset), ValidateFeedrates; a change ->
  UpdateData(FALSE) and, unless DAT_1804b641d, `FUN_1801af210` = CToolModWarning (`FUN_1801a9100`: MCDialog 0xccb = **3275**
  "Tool Settings Modified"; OnInitDialog `FUN_1801ad7a0` puts string **2424** (0x978) in static 503; DoDataExchange `FUN_1801aa3c0`:
  check 500 "Turn off warning for session..." -> dialog +0x1550 -> DAT_1804b641d).

### 9.3 When they run
- **Opening** (new op): CLToolProp ctor 0x1801a62d0: page combination +0x2b768 = AxisCombinationFromID(op +0x11bc, 0) (the
  defaults file's id; else AxisCombinationFromOpType), ToolPgPrm ctor: +0x34 = MaxLatheSpindleRPM. `CLTpSheet::InitTabPages
  0x1801e4170` ends with UpdateCtrlData(true) (before the pages exist). `CLToolPropBase::OnInitDialog 0x180263a50` -> MapToDlg
  (`CLToolProp::MapToDlg 0x1801a6dd0`): feed and (Face) finish feed clamped to their mode's limits, max = op +0x548, 0 or >
  GetSpeedMax -> GetSpeedMax (plain clamps, no font); GetToolProperty -> `CLtlPgCmn::GetToolProperty 0x1801ab7f0`: Make_op_tool;
  the tool's combination id != op +0x11bc -> op +0x11bc := it, ActiveSpindle(new) != ActiveSpindle(+0x2b768) -> ValidateSpindleSpeeds();
  +0x2b768 := new; ... `CLTpSheet::UpdateCtrlData 0x1801e61b0` -> list[0] = ToolPgPrm (`CLTpSheet::SpindleSpeed 0x1801e6150` calls
  list[0]'s vfunc +0x18 = ToolPgPrm::SpindleSpeed) -> `ToolPgPrm::UpdateCtrlData 0x1801b6320`: (+0x2928 EnableFeedRateLock off or
  +0x2929 "edited" off) -> vfunc +0x10 = `ToolPgPrm::UpdateToolFeedInfo 0x1801b76b0`: `GetToolFeedInfo 0x1801b37b0` (group +0x119c
  0: the tool's SpdlSpeed / CSS / GetFastFR = Make_op_tool's values) into the fields, ValidateSpindleSpeed(rpm, mode, +0x34, true),
  ValidateFeedrate(feed). MapToDlg's closing RunMachineDefChecks is a no-op (+0x2b790 still 1). So the rpm is checked against
  +0x34 = the max after the spindle check, or MaxLatheSpindleRPM of the defaults id's combination when there was none.
  lathe.mcam_8: the defaults id 3455 is not in the machine -> combination 42 (index 0, left spindle); T0101 -> 42: no check; T0202
  -> 48 (right spindle): forced, max 10000 shown in italics.
- **Another tool picked** (`OnClickToolbmpsLb 0x180263460`): GetToolProperty (the spindle check against the previous combination),
  UpdateToolFeedInfo, UpdateData(FALSE), SetFeedrateFromSS (thread only), ValidateFeedrates - no warning.
- **Edits**: every feed / speed handler ends with RunMachineDefChecks(true): OnKillfocusFeedrateEd 0x1802641c0, OnInRevRb /
  OnInMinRb / OnSurfaceFinishRb 0x180265470, OnFinishFeedCb 0x180263740 (when checked), OnFinishFeedEd 0x180263790,
  OnFinishFeedPerRevRb / PerMinRb / SurfaceFinishRb 0x180263820, OnKillfocusSpindleSpeedEd 0x180264260, OnCSSRb 0x1802632a0,
  OnRPMRb 0x180264980, OnFinishSSCb 0x180263880 (when checked), OnFinishSSEd 0x1802638d0, OnFinishCssRb / OnFinishRpmRb 0x180263850,
  OnKillfocusMaxSSEd 0x180264200 (also the list arrows ListUp / Down / Left / Right, not ported here).
- **OK**: `CLToolProp::OnOK 0x1801a7a10` = MapToOp only (no check): the record keeps what the fields hold.

### 9.4 Port
lfn_build_st: the opening sequence on the record (the record = the page's fields: rpm |op +0x328| with Make_op_tool's sign, max op
+0x548, feed |op +0x310| with MapToOp's sign rule, finish feed op +0x13a0, finish rpm op +0x13ac); lfn_repick_checks: the pick of
another row (max / finish fields kept from the dialog); lfn_machine_checks: RunMachineDefChecks after the controls above
(cui_lface.c runs_checks), the warning as a message box with string 2424. The italic fields: LfnSpeedSt.italic, shown by the Qt
sheet (italic font) and the dump (" (italic)"). lfn_can_start / lfn_build refuse a group +0x119c of 1 (9.5, 9.6).
Scenes: lface_rspindle (the max field italic after T0202), lface_mclamp (max 20000 -> 10000, RPM 15000 -> 10000, in/min 600 ->
500, finish rpm 12000 -> 10000, max 8000 -> rpm and finish rpm 8000; five warnings; OK, export, reopen, regenerate).

### 9.5 L-13 .. L-15 (2026-11-17)
- Feed calculation (group +0x119c, `GetToolFeedInfo 0x1801b37b0`), PORTED for 0 / 2 / 3: 2 From defaults = `set_op_prm_defaults` +
  `default_ops_get(opcode, op, group)` (the defaults file's op, InitOperation's source): rpm = its +0x328 as stored, RPM mode = (its
  +0x32d == 0), feed = |its +0x310|, per-minute mode = (its +0x310 >= 0); 3 User defined: feed = group +0x11a0, rpm = (long) group
  +0x11b0, RPM mode, the feed's mode not written (the field keeps MapToDlg's / the dialog's). The group values are the live ones
  (Group Properties' Tool Settings page, cui_grpprops.c F.grp_fs_mode / grp_feed / grp_rpm, for F.grp_id). Note: lathe.mcam_8's
  group holds +0x11a0 = 3600, +0x11b0 = 0.01 (as stored; the code reads +0x11a0 as the feed) - followed as read (L-16, the
  Mastercam-default rule). 1 From material: section 11.
- "Turn off warning for session..." PORTED: CToolModWarning's check box (DoDataExchange `FUN_1801aa3c0` DDX_Check 500 -> +0x1550,
  OK `FUN_1801adf00` = UpdateData(TRUE) + OnOK) -> DAT_1804b641d, a static never reset: no further warning in the session. Here
  cui_hooks.message_check (Qt: QMessageBox + QCheckBox; scripts: step `lfwarncheck:0|1`).
- EnableFeedRateLock DECODED + PORTED: mccore global (export `?EnableFeedRateLock@@3_NA` @ 0x181376eda) = config item 17027 "Lock
  Feedrates?" (ReadCfgFile setter `FUN_1801bac10` case 0x4283; default true in `FUN_1803bc8e0`; the config dialog `FUN_1801b14f0`;
  the user's mcamxm.config: y). ToolPgPrm ctor 0x1801b2320: +0x2928 = it, +0x2929 = (newOp == 0); +0x2929 = 1 in OnKillfocusFeedrateEd /
  OnFinishFeedEd / OnKillfocusSpindleSpeedEd / OnFinishSSEd / OnKillfocusPlungeFREd. ToolPgPrm::UpdateCtrlData skips
  UpdateToolFeedInfo when both are set: a re-pick keeps the edited rpm / feed (the spindle check then validates the kept rpm).
  Tests: MCAMUI_TEST_CFG17027 (ours) stands in for the file's value.
- The part closing with the sheet up (crash fixed): Mastercam's sheet is modal (`edit_lathe_operation 0x1801c7480`: a stack
  CLTpSheet run by MFC Ordinal_3916 = DoModal) so no part closes under it; here doc_clear_ops cancels it (lface_cancel, as its Cancel).
- Scenes: lface_fs_defaults, lface_fs_user, lface_warnoff, lface_lock, lface_nolock, lface_close.

### 9.6 NOT DECODED / NOT PORTED
- Group +0x119c = 1 From material: `LATHETOOLCLASS::CalcFeedSpeed 0x18014c540` - section 11; refused until ported.
- A mapped combination without a part list, a chuck max of -1: `CComponentGroup::MaxLatheSpindleRPM 0x1800a1370` /
  FindComponentsByType (the group's component tree) NOT DECODED, refused.
- The list arrow keys (ListUp / ListDown / ListLeft / ListRight: GetToolProperty + RunMachineDefChecks): no key handling here.
- MapToOp's rpm sign rules beyond Make_op_tool's CCW sign (OpActiveSpindle != the tool's spindle, |op +0x528| >= 90, op +0x2b3):
  kept as before (OpActiveSpindle not traced; with lathe.mcam_8's T0101 on combination 42 the extra rules would not apply).
- Mode 2's rpm as stored may be negative (the record keeps |rpm| with the tool's sign): not met in the defaults files here.

## 10 The stock update: a lathe op's stock-after copies (op +0xb40..) - DECODE STARTED (2026-11-17), nothing ported
What writes them: `LATHEPROJECTCLASS::SaveComponentsToDb(group, op id) 0x180110b60`: for the six component boundaries of
`bCompBdrys` (the BOUNDARYCLASS polygons; `PolygonDefined`), the op's old LBDRY record (get_lbdry_ent) is deleted, a new one made
by `BOUNDARYCLASS::FillInDbEnt` and stored (store_ent_assoc, sub 10); the ids go to op +0xb40 / +0xb44 / +0xb48 / +0xb4c / +0xb50 /
+0xb84 (0 where the polygon is not defined), op +0xb3b = 1 unless a stored record had its +0x67 set; UpdateListAndDB. With op id
0x7fffffff the same six go to the group's +0x16ac / +0x16b4 / +0x16bc / +0x16c4 / +0x16cc / +0x16d4 (the group stock, 7.2).
`UpdateOpStock 0x180112400` writes polygons 0 / 2 into op +0xb40 / +0xb44 of every lathe op of a group. Callers inside mclathe:
only the Mill-Turn misc ops (`MtChuckPrm::MakeOperation 0x18021f150`, `MtPointPrm::MakeOperation 0x18023c610`, `FUN_180240a40`)
and ValidFlagSetupComponents (group stock); the import name `?SaveComponentsToDb@LATHEPROJECTCLASS` is in mccore.dll,
mastercam.dll and mcmachinedef.dll - the call after a lathe toolpath is made is in one of them: NOT TRACED (next).
What computes the polygons (per NCI move, while the toolpath is written, only with op +0xb3c "Stock update" on):
`update_lathe_boundaries 0x1800c1870` -> `CLNciFmt::update_boundaries_linear 0x1800c14d0` / `_arc 0x1800c1280` /
`CLPointTp::update_boundaries_point 0x18010aa80`: (components defined, op +0xb3c) a rapid's start moved 0.001 in / 0.05 mm
along the move; `TOOLPATHOPCLASS::BackupGrComponents 0x1801d7800`, `ToolDiffRapid 0x1801e0bf0` / `ToolDiffLin 0x1801e0300` /
`ToolDiffArc 0x1801e0220` -> `ToolDiffLinArc 0x1801e03a0`: the tool's boundary (polygon 0xc top / 0xd bottom turret) translated to
the move start into 0x12, `BOUNDARYCLASS::Stretch` (line) / `ArcStretch` (arc) along the move into 0x13, CopyGeo 0x13 -> 0x14;
for each stock boundary of `bCompBdrys2`: `StartBdryAtXYLimit`, `DiffB1B2(stock, 0x13)` (the stock minus the swept tool),
`B2CutB1` (a crash: `PostToolCrashPrompt 0x1801ddd60`). The polygon engine is mcmachinedef.dll's BOUNDARYCLASS
(work/lathe/mdef/mdef_all.c: DiffB1B2 0x180032460 282 lines + FUN_180153d00 / FUN_180157090 / FUN_180157a10 (383) / FUN_180158b70..;
Stretch 0x18003c960 + FUN_180155ea0 / FUN_180156090; ArcStretch 0x18002f150 + FUN_180155070 (600) / FUN_180155f80; StartBdryAtXYLimit
0x18003c4e0; Translate 0x18003d080; YStatus 0x18003d790; MathScale = 1000) - a few thousand lines, arcs included.
Oracle: lathe.mcam_8 op 2's stored stock copy (LBDRY 174, Z -4 .. 0) after ops 1 / 2 from the group stock; millturn.mcam_8's ops.
Next: (1) the mccore / mastercam caller of SaveComponentsToDb after a lathe toolpath (and whether the BOUNDARYCLASS polygons it
saves are the ones ToolDiffLinArc left); (2) where polygons 0xc / 0xd (the tool's boundary, holder included?) are built from
LATHETOOLCLASS; (3) BOUNDARYCLASS::DiffB1B2 and its helpers, Stretch / ArcStretch; (4) port + check against LBDRY 174.

## 11 Feed calculation "From material" (group +0x119c = 1, L-13) - DECODED except the part material's record layout; NOT PORTED (refused)
`GetToolFeedInfo 0x1801b37b0` mode 1: `LATHETOOLCLASS::CalcFeedSpeed 0x18014c540(opcode, group +0x11d0 material name, group id)` changes
the picked tool (pclsLTool), then the mode-0 reads (SpdlSpeed(IsMetric), CSS, GetFastFR(IsMetric)).
- **Material** (loop while not found): `TpMatList::MaterialByName` of the group's name upper-cased (CString MakeUpper); the part's
  list = its ASSOC sub 4 records (store_ent_assoc(.., 4, ..) in get_setup_matl; now loaded into McamFile.mdrec); the compare
  `IndexByName 0x1808fa5f0` = MFC120 ordinal 2863 = a tail call of MSVCR120 `_mbsicmp` (mfc120.dll export RVA 0x37440, IAT
  0x2aad88): case-insensitive. Not found -> mccore `get_setup_matl 0x1800d35b0` (GetProdEnableBits() & 6 -> none; the group's
  name, "NONE" / empty -> the material picker mlib_select (UI); in the part list -> it; else `import_matl_ent 0x1800704b0` ->
  `import_ent(name, 4, MatlLibrary(group), ..)`: the library's material records of sub 4 with selection bit 1 whose name
  `_stricmp`-matches (FUN_180067b50), stored into the part; else mlib_select), then MaterialByName again. None -> group +0x119c := 0
  (written to the group) and the tool is left as it is (mode 0 values). The group's material name := the found one's.
- **Tool fields** (LATHETOOL body = LATHETOOLCLASS this + 8): rpm +0x7e8 (SetSpdlSpeed 0x18015a890: metric tool with CSS -> / 0.3048,
  dRound, kept if >= 0), CSS flag +0x7ec (SetCSS 0x1801570a0), fast feed +0x788 / 'R' +0x790 (SetFastFR 0x180157880: metric tool ->
  / 25.4), Metric +0x808, PctMatlCSS +0x7f0, PctMatlFdRev +0x7f8; insert MatlIndex = INSERTCLASS +0x144 (0x1801529c0, > 5 -> 0) =
  body +0x1e4 (INSERTCLASS at this +0xb0, lathe_tool.c B_INS convention).
- **Formula** (material struct offsets, 0x3a0 layout): metric material = +0xc8 != 0; tool type t (ToolType(1, 0)); insert index i:
  t 1 thread: flag +0x2d0+i -> pct = +0x2e0+8i, feed pct 100; t 4 drill: flag +0x2d6+i -> +0x340+8i, feed pct +0x370+8i (not for drill
  types 7 / 8); other: flag +0x2d0+i -> speed pct +0x2e0+8i, feed pct +0x310+8i; flag off -> 100 / 100. Per opcode: Face 0x42 / 0xcc /
  0xcd: css% = +0x250, feed% = +0x2a0; rough 0x3c / 0xca / 0xcb / 0xd2: +0x248 and (type 3: +0x288, slow% +0x290; else +0x270, slow%
  +0x278); groove 0x3e / 0x43 / 0x44 / 0xd0 / 0xd1 / 0xd4: +0x258, +0x2a8; thread 0x3f: +0x260; drill 0x40: +0x240 and per drill
  type +0x2b8 / +0x2c8 / +0x2c0 / 1 / +0x2b0; default: +0x250 and (type 3 / '5': +0x298, else +0x280). Not drill-RPM: SetCSS,
  css = +0x238 * speed%/100 * css%/100 * PctMatlCSS/100 -> metric material, inch tool: / 0.3048; inch material, metric tool: *
  0.3048; (long) -> SetSpdlSpeed. Feed = +0x268 * feed%/100 * opfeed%/100 * PctMatlFdRev/100, mode 'R'; a metric material with an
  inch tool / 25.4 - the decompile also divides for an inch material with a metric tool (LAB_18014cd0a reached from both
  branches): the disassembly is not read (NOT DECODED). Rough ops also set the slow feed (+0x268 * feed pct * slow% * PctMatlFdRev).
- **NOT DECODED, so not ported**: the record layout of a part's material saved at an older minor version - millturn.mcam_8's material
  806 "STEEL meter - 4140 - 400 BHN" has a 0x388-byte body (mcprvverfile FUN_180063b10: body 0x388 when record +0x48 < 0x12 or
  +0x49 < 0x40, else 0x3a0) and the reader that turns it into the 0x3a0 struct is not traced. Measured, not decoded: the body
  matches the 0x3a0 struct with 0x18 bytes missing between +0x100 and +0x1b8 (23 instead of 26 tool-type feed percentages; the six
  cutter-material flags at +0x1b8 instead of +0x1d0); read that way, CalcFeedSpeed gives millturn ops 7 / 8 (Rough / Finish with
  slot 29, a type-3 tool) exactly their stored rpm 295 (CSS 90 m/min / 0.3048) and feed 0.01181 in/rev (0.4 mm * 75 %), and op 2's
  feed 0.01575; ops 1 / 2 / 4 / 5 have rpm 400 / 600 (edited or made otherwise). Also not decoded: the "/ 25.4" branch above,
  GetProdEnableBits, the material picker (UI). Until decided: group +0x119c = 1 stays refused (lfn_can_start).

### 11.1 L-16 follow-up (2026-11-17): the converter trace and the feed branch
- **The feed branch, from the disassembly** (mclathe 0x18014ccf6..0x18014cd0e, the slow feed 0x18014cd76..0x18014cd8f; divisor
  .rdata 0x1802f7858 = 25.4): sil = metric material; `test sil; je` -> inch material: tool Metric() != 0 -> divide; metric
  material: tool Metric() == 0 -> divide. So the feed is divided by 25.4 whenever the material's and the tool's units differ
  (both directions), as the decompile showed - DECODED. (Then SetFastFR / SetSlowFR divide again for a metric tool.)
- **The 0x388-byte material record - converter traced, none found.** millturn.mcam_8 / lathe.mcam_8 are X9 files (header +0x6a /
  +0x6b = 0x12 / 0x3f); the material record 806 has record version +0x48 / +0x49 = 0x12 / 0x3f. Read path: mccore read_ent_from_disk
  -> mcprvverfile `ReadPrevVerEntFromDisk 0x180003560` (major 0x12) -> `FUN_180065720` (ReadVX9EntFromDisk): the whole record read
  into a 0x2938 buffer, `FUN_180063b10(rec, 1)` = fixed size 0x388 for class 0x4000 sub 4 when +0x48 < 0x12 or +0x49 < 0x40 (no
  variable part: the record is 0x68 + 0x388), then `FUN_18005e7f0` = the X9 -> 2017 ent update: a copy of all 0x2938 bytes, then
  changes only for class 0x200 sub 0x13, class 0x4000 sub 1 (operations by opcode) and sub 0x18, class 0x10000 - nothing for sub 4.
  mccore's own reader `FUN_1802c62d0` (2017 files) also reads the record bytes as they are; `bytes_per_ent` / `assoc_sizeof` give
  0x3a0 for sub 4 with no version case. Also read without a material conversion: TpPartMatList::DatabaseRetrieve 0x1808fb070,
  TpMatList::CreateCopy, init_material 0x18004bac0, get_setup_matl, import_matl_ent / import_ent (FindImm 0x388 / 0x3a0 over
  mccore: no other material user). So, as far as traced, Mastercam 2017 keeps an X9 material's 0x388 bytes in place and
  CalcFeedSpeed reads them at the 2017 offsets: for millturn's material that gives a Face CSS% of +0x250 = 0.4 (the X9 base
  feed), i.e. a few rpm - while the stored ops (made by X9, whose struct is the 0x388 layout) carry X9's results. Not ported:
  porting that reading would be "as traced" but its result is implausible, and a converter elsewhere (mastercam.dll / mclathe
  file-open passes not searched) is not ruled out. Question for the user (L-16a). Group +0x119c = 1 stays refused.
