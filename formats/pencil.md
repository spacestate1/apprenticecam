# Pencil (HST op 132, UI style 6, kernel style 5) - decode and porting spec (2026-10-26)

Status (2026-10-26, second pass): **the geometry is decoded and ported** for the default op without offsets: the net's drops,
the edge search, the PencilQuad cells, the chain walk with its thinning and junctions, the chain filter, the climb split and the
pieces' orientation, the pass points (section 6). Ported as `viewer/cam3d_pencil.c` (section 8), checked against Mastercam's stored
PENCILSEAMS op (section 7: our passes lie on Mastercam's centre passes, p95 0.0071 in). Still NOT DECODED (refused): the offsets plan
0x140d (so the one stored op regenerates only with its offsets switched off), Overthickness, non-ball tools; ours (labelled): the drop
on our tessellation, the cell types, the pass order and links (section 8).

Rule for this file: every statement cites decompiled code or Mastercam's own data. Anything taken from data and not from code is
marked [measured] or [inferred]. Anything not found is marked NOT DECODED.

## 0. Sources and citation keys

- **mk l.N**: `decomp/mckernel_all.c`. **ms l.N**: `decomp/mcmsurf_all.c`. **reg**: `decomp/hmm_params.tsv` (McHmm id -> name).
- **tb3 l.N**: `decomp/raster/tb3.c` (FUN_1802f5dc0, the shared machining-plan defaults).
- **hw l.N**: `decomp/hst3d_weave_drop.c` (CWeave).
- **pp l.N / pn l.N**: McHmm ranges 1803c8000-1803cc290 (CPencilPassesPlan / EvaPencilPassesPlan, 40 functions) and
  18014f000-18016a000 (FullPencilNet / PencilQuad, 263 functions), decompiled for this file with `tools/ghidra_scripts/DumpRange.java`
  on a private copy of `~/mastercam-re/ghidra_hmm` (program mchmm.dll). Scratch (temporary):
  `decomp/pencil/pplan.c`
  and `.../pencil/pnet.c`. To keep them, re-run DumpRange over those two ranges into `decomp/hmm_pencil.c`.
- **d2 l.N**: single functions (DecompAt.java), same scratch dir `d2.c`: FUN_180253300, FUN_180313c70, FUN_1802f5b70,
  FUN_1802ed680, FUN_18011b050, FUN_180073c00, FUN_1802ec5a0, FUN_180150e90.
- **st l.N**: `decomp/pencil/g/step.c` (DecompAt.java, this pass): FUN_180253d20 (l.1-85), FUN_180253f30 (l.86-161), FUN_180253690
  (l.162-231), FUN_1802541f0 (l.232-280), FUN_18005bd20, the tool box drop FUN_180222a90 (l.1982-2050), FUN_18013e330 (l.2051).
  The decompiles of this pass are in that gitignored scratch folder; copy them into decomp/ to keep them.
- **pe**: bytes of `~/mastercam-re/files/mchmm.dll` read directly (objdump / a PE section map) for the expression built-ins (1.1).
- **dis**: DisasmAt.java of the two call sites whose arguments the decompiler lost: `dis1.txt` (1803cb230-1803cb560), `dis2.txt`
  (1803cb557-1803cb5a8). **mem**: MemDump.java `mem1.txt` (expression string pieces).
- **res**: `exports/mcmsurfres_dialogs.tsv` rows 1492-1523 (dialog 1329). **ui**: `formats/ui_fields_mcmsurf.tsv` rows 208-213.
- **help**: `~/mastercam-re/chm/sr5axfields/Fields_HMM/` (Bitangency_angle.htm, Overthickness.htm, Reference_tool_diameter.htm).
- **Record offsets.** "TP+x" is the TPHSTToolPath field mckernel reads. The op -> TP copy (mccore) is NOT DECODED; record offsets
  use the block shifts of hst3d_port_plan.md 1.1 / 6.6 / 6.7 / 8 and scallop.md 0. New for Pencil: **TP+0x248 / 0x250 = record
  0x12d0 / 0x12d8** (shift 0x1088, as Area Roughing's 0x230 -> 0x12b8), confirmed by the dialog: page 1329 Overthickness /
  Bitangency angle write op +0x3358 / +0x3360 (ms l.98282-98283) and record = op - 0x2088 -> 0x12d0 / 0x12d8, the same offsets
  ui gives. **TP+0x466 = record 0x16cb** (offsets mode, shift 0x1265 as 0x46f -> 0x16d4) [inferred]; **TP+0x440 = record 0x1540**
  (number of offsets, int; page op +0x35c8 - 0x2088) [inferred: the kernel and the page are the only users and their meanings
  agree, the shift 0x1100 is new].

## 1. Op record -> kernel parameters (FUN_180071314, style-5 branch mk l.87623-87694, plus the common prologue)

Default op: **Mill_MM.mcam-defaults op 71 / Mill_Inch op 73** (the first op 132 with record 0x16bc = 6) [measured,
tools/mcam_ops.load(path)[1][id]['raw']].

| kernel parameter (McHmm id, reg) | value | record (MM / Inch default) | source |
|---|---|---|---|
| PencilOverthickness (0x1a86) | o = TP+0x248; when (corner radius + min(XY stock, Z stock)) < -2 tol: o += abs(corner radius + min(..)) + tol. **Written only when o > SystemContext +0x10** (a tolerance [inferred]); otherwise the plan default **0** (FUN_180313c70 d2) | 0x12d0 Overthickness (0 / 0) | mk l.87625-87655; corner radius = FUN_180096984(TP+0x70) = *(TP+0x80) (mk l.111038), GetHstCornerRadius of hst3d_port_plan.md 6.7; tol = job +0xa0 |
| PencilBiTangentMaxNormalChange (0x1a5e) | a = deg(TP+0x250); **180 - a** when a >= SystemContext +0x10, else **15** | 0x12d8 Bitangency angle, radians (2.96706 = 170 deg / same) -> **10 deg** | mk l.87656-87667 |
| PosZHi / PosZLo | as Waterline (hst3d_port_plan.md 6.7 / 7): job range, or Use Z depths TP+0x464 (0x16c9) with TP+0x1c0 / 0x1c8 + job +0x28; PosZLo + param_5. Style 5 is in the mask 0x5b6, so PosZLo is also lowered by the corner radius (6.7) | 0x16c9 = 0 | mk l.87668-87677, l.87321-87348 |
| ContactAngleLo / Hi | degrees of min / max(TP+0x1d0, TP+0x1d8), each clamped to 0..pi/2 | 0x1248 / 0x1250 (0 / 1.5708 rad = 0 / 90 deg) | mk l.87678-87687, l.87266-87302 |
| PosContactOnly (0x1ae1) | false, written only when TP+0x46a = 0 | 0x16cf = 1 -> not written (Waterline's reading of 0x16cf) | mk l.87688-87693 |
| StepMachTolerance (0x1acc) | tol + local_848. **Pencil only**: when the corner radius < tol, local_848 = -(tol - Rc) / 2, i.e. StepMachTolerance = (tol + Rc) / 2 | record of tol NOT DECODED (hst3d_port_plan.md 6.6) | mk l.87400-87407 (the `== '\x05'` test), l.87612-87617 |
| ToolThicknessXY / Z | the common non-Horizontal branch (XY = max(XY, Z) stock, Z = Z stock), **each + local_868**; Pencil only: local_868 = (tol - Rc) / 2 when Rc < tol | 0x13c0 / 0x13c8 (1 / 1 mm, 0.05 / 0.05 in) | mk l.87404-87407, l.87573-87611 |
| tool (ToolCornerRadius, ToolCutterDiameter, ...) | FUN_180074e18 with the tool at TP+0x70 (bVar18 is true for style 5) | tool: 0x350 / 0x358 (20 / 0 mm flat; 0.75 / 0 in) | mk l.87390-87397, l.87410-87458 |
| MachConstStepoverVertical / Horizontal (0x1bae / 0x1bad) | **offsets only** (FUN_18007b024): TP+0x1b8 if > 1e-12, else 0.1 (inch) / 2.5 (metric) | 0x1230 Stepover (1 / 0.05) | mk l.91255-91278 |
| StepMachTolerance on 0x140d | job +0xa0 | - | mk l.91279-91283 |
| (bool, name lost by the decompiler) + MachConstNumberOfOffsets (0x1baf) | offsets mode TP+0x466 = 1: the bool = true and NumberOfOffsets = TP+0x440; else **MachConstLimitOffsets (0x1a47) = false**. The lost name is MachConstLimitOffsets [inferred: the else branch] | 0x16cb = 0 / 0x1540 = 0 | mk l.91284-91297 |

**McHmm plan defaults** (parameters mckernel does not set for this op). Default expressions of FUN_1802f5dc0 for plan 0x140f
(the bit mask at tb3 l.498-499 selects 0x140f; string pieces "((" 1807846e4 and " + " 1807624c0 read with MemDump, mem):

| parameter | default expression | source |
|---|---|---|
| PencilCellResolution (0x1c77) | `min(2.5, ToolShaftRadius * 2)` | tb3 l.500-510 |
| PencilBigCellResolution (0x1a5d) | `PencilCellResolution * 7.0` | tb3 l.511-520 |
| PencilBiTangentMaxNormalChange (0x1a5e) | 20.0 (15.0 for plan 0x1432); **mckernel overrides it** (10 deg for the default op) | tb3 l.521-534 |
| PencilBiTangentMaxContactPointChordChange (0x1a5f) | `2.0 * sin(PencilBiTangentMaxNormalChange / 2)` | tb3 l.535-544 |
| PencilBiTangentMaxNormalChangeThreshold (0x1a84) | `PencilBiTangentMaxNormalChange + 5.0` (only plans 0x140f / 0x1439) | tb3 l.546-556 |
| PencilBiTangentMaxContactPointChordChangeThreshold (0x1a85) | `2.0 * sin(PencilBiTangentMaxNormalChangeThreshold / 2)` | tb3 l.557-566 |
| PencilBiTangentMinHorizontalChange (0x1a60) | `StepMinHorizontalChange` (0x1acf; its default is 0.003 per scallop.md 1, FUN_180305960 - not re-read here) | tb3 l.567-573 |
| PencilBiTangentMaxVerticalStepforward (0x1a67) | `((ToolCornerRadius + ToolThicknessZ) == 0.0) ? 0.5 : 2.0` | tb3 l.574-590; mem |
| PencilOverthickness (0x1a86) | 0 | FUN_180313c70 d2 l.60-62 |
| MinFragLength (0x1a87) | 0.1 (validator `DDV_MinDistance(%1, 0)`) | FUN_180313c70 d2 l.63-70 |

**1.1 The unit of `sin(...)` (decoded, pe).** The expression interpreter's built-ins are a table of (declaration string, function)
pairs at 1809a1670.. (e.g. 1809a16e8 -> L"real sin(const real& angle);" with 1809a16f0 -> 180297550; `radians` 1809a1688 / 180297530;
`cos` 1809a1748 / 1802975d0; `tan`, `asin`, `atan`, `acos` beside them). sin, cos and tan are thunks into 1802970a0 with the CRT
function in r8; 1802970a0 computes `x * (TWO_PI * (1 / 360.0))` (180297136-180297158: [1809bf198] x 0.0027777 (1807bb478)) and calls
the CRT function on that. TWO_PI = PI x 2.0 (static init 180002300, factor 2.0 at 1807bb7a0) and PI = f(-1.0) (1800022dd: the CRT call
at 180569982 on -1.0 at 1807bba88 - acos [inferred from the value use]). So **the interpreter's sin takes degrees**: for the default
op MaxNormalChange 10 -> chord 2 sin(5 deg) = 0.1743, threshold 15 -> 2 sin(7.5 deg) = 0.2611. (Measured support: with radians the
chord would be 1.92 and none of PENCILSEAMS's seams - 66 to 82 deg - would be marked; Mastercam machines them.)

The value of ToolShaftRadius for a Mastercam tool is NOT DECODED (FUN_180074e18, as for Scallop); the port uses D / 2 [inferred]
(PencilCellResolution = min(2.5, D)).

Other pencil parameter writers in mckernel are **not** this op: FUN_18006c944 (mk l.83919, PencilCellResolution = its +0x80) and
FUN_180078e44 (mk l.90320, = TP+0x438) feed the "//Fillet Surfaces plan" 0x1439; FUN_18007b240 / FUN_18007b604 (mk l.91305-91400,
LinkPencilDirectionStyle) belong to "// Mastercam X - Pencil Pass Macro" (mk l.94211-94280, plan ekPlan4 0x140f + "Pencil Linker
Plan" 0x1442), a separate job: the classic Surface Finish Pencil op 39 (decoded 2026-11-13, formats/surf_classic3.md 9.3 / 10; ported as gen_sfpencil on this net).

## 2. Algorithm

### 2.1 Plan chain (FUN_18007d7c8 mk l.93245-93360, l.93863-93890)

- Inputs of the Pencil Passes Plan **0x140f**: "ConstraintBoundaries", "Surfaces" (ekPlanTcb3 / ekPlan3 + ekPlan2 /
  ekPlanPrefillet2, as Scallop's 2D path) (mk l.93225-93243). The plan declares the inputs ConstraintBoundaries (0x1b41), Surfaces
  (0x1aaf), ProtectedSurfaces (0x1c78) (CPencilPassesPlan ctor FUN_1803cc1c0 pp l.2664-2680).
- **No offsets** (TP+0x466 = 0, or = 1 with TP+0x440 < 1): `ekPlan4 = Pencil 0x140f`, and nothing else is added (goto
  LAB_1800815ae, mk l.93278-93289, l.93299-93301). **This is the default op** (0x16cb = 0).
- **Offsets** (TP+0x466 = 2, or = 1 with TP+0x440 >= 1): ekPlan4a = Pencil, then FUN_18007b024 (section 1) and
  `ekPlan4 = Constant Stepover Passes From Passes 0x140d` with inputs "ConstraintBoundaries", "MachiningPassesIn" = ekPlan4a;
  with check surfaces present (FUN_18006f480: TP+0x472 set, the list at TP+0xf0 not empty, TP+0x49e = 0) an Edit Passes to
  Toolholder 0x1459 follows (mk l.93300-93335).
- With a stock model (TP+0x463 set and TP+0x475 != 2) the same choice is made under the names ekPlan4x / ekPlan4ax, followed by
  FUN_18007b97c and Edit Passes to Surfaces 0x141f (rest material) (mk l.93336-93382). Not the default.
- EvaConstantStepoverPassesFromPassesPlan (vftable 18078bc90, `~/mastercam-re/work/hst3d/eva_vt.txt` l.68) is NOT DECODED.

### 2.2 Evaluate (EvaPencilPassesPlan slot [1] FUN_1803ca8f0, pp l.1790-1849)

Order of the calls:
1. **Setup** FUN_1803cab20 (pp l.1851-2174), below.
2. **Pencil net** FUN_180151630(Eva+0xff0 = the FullPencilNet, Eva+0x13d0 = the step parameters, Eva+0xcb8, progress)
   (pp l.1811), section 2.3.
3. Log "pencil chains %d" (pp l.1814-1824).
4. **Chains -> passes** FUN_1803cb3f0 (pp l.2176-2535), section 2.4.
5. With **ShaftProfileEnable** (0x1c9f): FUN_1802ec5a0 (reads ToolShaftRadius 0x1abe; NOT DECODED) (pp l.1826-1838).
6. Hand the pass list at Eva+0xfb8 to the output (FUN_1800a2410) (pp l.1839-1847).

**Setup** FUN_1803cab20:
- Eva+0x2c8 = (PencilOverthickness == 0). Then Eva+0x2d0 = the tool Eva+0x208 itself, or (overthickness != 0) the tool built by
  FUN_18013e330(tool, 0, overthickness, 0) (pp l.1923-1952) - a tool grown by the overthickness [inferred from the call shape;
  help: "increase the apparent size of the cutter to force a cutting pass"].
- The net is initialised by FUN_180150120(net, (float)PencilBigCellResolution, 0.0, plan, PencilOverthickness, m) with
  m = StepMinHorizontalChange (0x1acf) when the overthickness is not 0, else -1.0 (DAT_1807bbd50 = -1, -1, mem) (pp l.1953-1973).
  FUN_180150120 (pn l.661-700) builds a **CWeave** (FUN_18012bc80, hw l.18830) over the box at plan +0x118 with fibre spacing
  PencilBigCellResolution (the fibre layout of hst3d_port_plan.md 4.5: X fibres and Y fibres, n = max(3, int(range / spacing)
  + 2) evenly spaced), copies its two fibre sets into the net (net +0x150), computes the fibres (FUN_18012bf60 with Z offset 0,
  hw l.18928) and runs FUN_180128f30 (hw l.16698, up to 11 alternating X / Y passes of FUN_18012f730 until two quiet passes).
- A tool box of the Surfaces with margin FUN_1802f5b70(plan, tool, 2 x PencilBigCellResolution) = 2 x SurfaceBoxWidth (0x1adb)
  + max(the tool's largest radius, ShaftProfileMaxShaftRadius when ShaftProfileEnable) (+ holder terms with RestPassesAllowed)
  + 2 x BigCellResolution + 0.1 (d2 l.75-170), built by FUN_180221480 into plan +0x498 (pp l.1974-1984).
- With **EnableTrimToBoundaries** (0x1b3f): the ConstraintBoundaries plan's "Boundaries" (0x1b44) -> an EvaToolBoxFrags at
  Eva+0x120 (pp l.1985-2133), as Scallop.
- The step parameters Eva+0x13d0 = FUN_180253300(MinHorizontalChange 0x1a60, -, PencilCellResolution 0x1c77,
  MaxContactPointChordChange 0x1a5f, MaxVerticalStepforward 0x1a67, R12, -1, -1) (argument registers from dis 1803cb324-1803cb359;
  FUN_180253300 d2 l.1-44): [0] = MinHorizontalChange, [1] = its square, [2] = -1, [3] = PencilCellResolution, [4] = chord change,
  [5] = vertical step-forward, [6] = (R12 +0x88 != 0), [7] = R12, [8..11] = -1 (the angle part is skipped for -1). Then
  Eva+0x1400 / 0x1404 are overwritten from Eva+0x6e0 / 0x6e4 (pp l.2152-2172).

### 2.3 FullPencilNet (FUN_180151630 pn l.1512-1560; source file "Machining\FullPencilNet.cpp", assert line 0x13c)

- s = 3 x the largest of |net+0xf0|, |+0xf8|, |+0x100|, |+0x108| (float); e = s + min(step[0], Eva+0xcb8[0]); the net asserts
  unless s < (s + e) / 2 < e (pn l.1530-1546). What the four fields are: NOT DECODED.
- Stage 1, **cells** FUN_180150520 (pn, 255 lines): (nx + 1) x (ny + 1) cells of 0x148 bytes (class PencilQuadSuper) on the fibre
  grid (nx / ny = the X / Y fibre counts at net +0x188 / +0x1d0). Each inner cell takes its bounds from the neighbouring fibre
  coordinates (+0x28 of each 0x38-byte fibre) and its triangles from the tool box (FUN_180222a90 on plan +0x498). A cell's type is
  2 when all four surrounding fibres have a first interval (vtable +0x10) starting at or below the cell's cross coordinate, else 3
  (`local_218 = 3 - bVar12`). Neighbour links: +0x30 / +0x38 / +0x40 from the left / lower cells.
- Stage 2, **edges** FUN_180151810 (pn l.1564-1600): for every cell of type 2, or whose neighbour at +0x50 / +0x58 is type 2,
  FUN_1801611d0 on its two edges (+0x30 with +0xb0 / +0xc8, and +0x40 with +0x90 / +0xa8), with the step parameters.
- Stage 3, **rows** FUN_180155740 (647 lines; logs "scan row", "step fore", "step back", "chain row", "Kill row", "unchain",
  "**** unkill").
- Stage 4, **pairs** FUN_1801549b0 (601 lines; logs "PC Pairs", "Mismatch pair", "Unlinked BI-path across 2-cell, length").
- Inside the cells the tool is chopped with CToolChopPos / CSChopL (FUN_1801522b0, FUN_1801537c0 via FUN_180151a20; these classes
  are dumped in decomp/hst3d_chop.c); FUN_1801522b0 also logs "Clearing folded fillet surface".
- The bitangency test, the per-edge search, the cells, the chain walk: **decoded in section 6**. The net's output is the chain list at
  Eva+0x1128.

### 2.4 Chains -> passes (FUN_1803cb3f0 pp l.2176-2535)

For every chain list in Eva+0x1128:
1. **Filter / split** FUN_18015c4c0(chains, out, MaxVerticalStepforward 0x1a67, MaxContactPointChordChangeThreshold 0x1a85,
   MinFragLength 0x1a87, Eva+0x218 == 1, Eva+0x1350 != 0, Eva+0xcb8) (arguments from dis2 1803cb557-1803cb583). It calls
   FUN_18015c020 twice (pn l.9318, l.9812) and logs "Inserting vertical climb-changing pioece at" (pn l.9863).
   FUN_18015c020 (pn, 137 lines), per piece: counts the segments whose two contact points (FUN_1802538d0 of each end's +0x18)
   are farther apart than the chord threshold, sums the XY length L and the "convex" length C (segments where both contact
   offsets lie on the non-positive side of the segment's normal, the side chosen by param_6), and with more than 2 such segments
   and (min length = -1 or L > min length) logs "Discarding convex pencil piece of length L, convex part length C". Return
   rule and the split: decoded in 6.7.
2. Per resulting piece: a drop object at Eva+0x4f0 is reset (tolerances 0.0003 at +0x510 and 0.01 at +0x658), FUN_180150e90(net,
   drop, piece, (float)Eva+0x140, Eva+0xcb8) (d2 l.2217-; calls FUN_180150b70, logs "Unexpected points required in pencilpath"),
   FUN_18011b050 writes the pass (entity type 0x13d4), its index pair is appended to the pass list at Eva+0xfb8.
3. **Trim** FUN_1802ed680(Eva, passes, Eva+0x120 frags when EnableTrimToBoundaries, progress) (d2 l.172-690): reads
   EnableTrimToSurfaces 0x1b4e (+ TrimToSurfaceSimultanious 0x1ce7 -> Eva+2000), EnableTrimToBoundaries 0x1b3f, EnableTrimToZRange
   0x1b60 with PosZLo / PosZHi (0x1ac7 / 0x1ac8), EnableTrimToParametricValues 0x1e61 (0x1e62 / 0x1e63). Its maths: NOT DECODED.

The order of the chains, their direction and where a chain starts: NOT DECODED (the chain list order comes from stage 3 / 4).

### 2.5 Offsets (Constant Stepover Passes From Passes 0x140d)

Not used by the default op. Inputs: the pencil passes as MachiningPassesIn, ConstraintBoundaries; parameters section 1
(FUN_18007b024). McHmm defaults for 0x140d: FUN_18030e0c0 (tb3 l.469), MachWeaveResolution = 0.7 x MaxHorizontalStepChange
(scallop.md 1). The evaluator is NOT DECODED.

## 3. Links

- Plan (FUN_18007d028 mk l.92394-92413): no offsets -> "//Get Pencil Passes Plan" ekPlan0 and **"//Pencil Linking Plan" 0x1442**
  (CPencilLinkingPlan, vftable referenced at decomp/hmm_link.c l.15645; dialog CPencilLinkingDlg l.15525); offsets -> "//Get
  Constant Surface Stepover Passes Plan" and the Constant Surface Stepover Linker 0x1420.
- Parameters (FUN_180075b48, style-5 branch mk l.89312-89452), no offsets:
  - Zigzag TP+0x45d (0x16bd) with TP+0x46f (0x16d4) = 0: LinkDirectionStyle 1;
  - TP+0x46f = 1 / 2 (Down / Up Mill): LinkDirectionStyle 2 / 3 (name lost, as Raster), LinkDMExtendStart = TP+0x2f0, LinkDMShallowSlope
    = deg(TP+0x2f8) (Overlap 0x1380 / Shallow angle 0x1388, shift 0x1090 [inferred], scallop.md 4);
  - else LinkDirectionStyle 0, LinkReverseDirection = true when Other Way TP+0x470 (0x16d5);
  - then LinkRampHeightOffset = TP+0x30 (mk l.89447-89452).
  - With offsets (mk l.89376-89445): Zigzag off -> direction 0 with LinkReverseDirection = Other Way (written as int, FUN_1800ed9bc);
    Zigzag on -> 1; Down / Up -> 2 / 3; then LinkDMExtendStart = 0 and LinkClimbMilling.
- Common: LinkMaxStayOnSurface (mk l.89205-89235): TP+0x468 = 1 -> tool diameter (FUN_18009698c(TP+0x70) = *(TP+0x78)) x TP+0x288 /
  100; = 0 -> TP+0x280; record 0x16cd / 0x1308 / 0x1310 [inferred: the same fields cam3d_rough.c c3_values reads as "Keep tool down"
  % / distance]; LinkRetractStyle TP+0x462, LinkTrimmingStyle, vertical lead in / out TP+0x2c0 / 0x2c8 (mk l.89237-89310).
- **Default op**: one way (0x16bd = 0, 0x16d4 = 0, 0x16d5 = 0) [measured]. The Pencil Linking Plan's evaluation is NOT DECODED.

## 4. Dialog page 1329 (HSTPencilParamPage, base HSTFinishPageBase)

Sources: ctor FUN_180095f90 ms l.97749 (resource 0x531 = 1329); DDX FUN_180096310 ms l.97902 + base FUN_180087d50 ms l.88717; InitPage
FUN_180096430 ms l.97948 (assert "HSTPencilParamPage::InitPage") + base FUN_180088180; MapToOp FUN_180096ae0 ms l.98274 + base
FUN_180088bd0; record = op - 0x2088. Layout: res rows 1492-1523.

| control | label (res) | record | notes |
|---|---|---|---|
| 1239 | Cut method (combo; strings 0xb8, 0xb9, 0xbb, 0xbc, 0xbe) | 0x16bd / 0x16d4 / 0x16d5 | base page, as scallop.md 5: 0 One Way, 1 Other Way (0x16d5 = 1), 2 Zigzag (0x16bd = 1), 3 Down Mill (0x16d4 = 1), 4 Up Mill (0x16d4 = 2). Default 0 |
| 692 | Tip compensation (combo) | NOT DECODED | not in these DDX functions |
| 1248 | Stepover | 0x1230 | base (op +0x32b8) |
| 1301 | Scallop height | not stored | base FUN_180088430 |
| 1251 | Limit # of offsets to (check) | 0x16cb | page +0xb840. Init: 0x16cb 0 -> checked, N = 0 (and op N cleared); 1 -> checked, N = 0x1540; 2 -> unchecked. OK (FUN_180096920): checked -> 0x16cb = (N != 0); unchecked -> 2 |
| 1250 | (number of offsets, int edit) | 0x1540 (int) | page MCInt +0xb848, value +0xc440 |
| 706 | Reference tool diameter | not stored | = 2 ((N + 1) stepover + D / 2) (FUN_180096200); editing it sets N = ceil((ref / 2 - D / 2) / stepover) (FUN_180096240), or, after N is edited with ref edited, stepover = (ref / 2 - D / 2) / N (FUN_1800962b0); enabled with the Limit check (Ordinal_4282, ms l.97984-97985) |
| 1299 | Overthickness | 0x12d0 | MCReal +0xc5b0; help Overthickness.htm |
| 1300 | Bitangency angle | 0x12d8 (radians) | MCReal +0xd3a8; page default when the lookup fails 2.87979 rad = 165 deg (FUN_180096950 ms l.98205-98230); help: "A typical recommended value is 165 degrees"; the record default is 170 deg [measured] |
| 1285 / 1286 | Overlap / Shallow angle (group "Down / up mill") | 0x1380 / 0x1388 (1 mm, 0.032 in / 5 deg) | base; enabled only for Down / Up Mill (FUN_180087e00) |
| 1205-1208 | Keep tool down within: Distance / % of tool diameter | NOT DECODED here | not in these DDX functions (scallop.md 5) |
| 1287 | Use approximate start point | 0x16bf | base |
| 1288 | Horizontal offset | 0x16ca | DDX FUN_180096310 control 0x508 -> page +0x55a0 -> base MapToOp op +0x3752; InitPage disables it (Ordinal_13643(GetDlgItem(0x508), 0), ms l.97990-97991). Kernel use NOT DECODED |
| 1508 | (static, picture) | - | page +0x5288, DDX int (Ordinal_3548) |

## 5. NOT DECODED (in order of need; after the second pass)

1. Offsets: EvaConstantStepoverPassesFromPassesPlan (vftable 18078bc90) and the Constant Surface Stepover Linker 0x1420 - the only
   stored Pencil op uses them, so Mastercam's output can only be compared with the offsets switched off (section 7).
2. The cell type (pn l.871-891): the net skips type-3 cells; the test reads the first interval of the four surrounding fibres of the
   net's CWeave, computed by FUN_180131810 (hw) with Z 0 and the complement flag (param_5 = 1), whose interval maths
   (FUN_180289d90) is not read. With it: what the net does where a drop has no contact (off the drive).
3. The tool box drop FUN_180222a90 (st l.1982) and its contact code (the int / 24-bit fraction / 6-bit profile segment that
   FUN_1802538d0 / FUN_180253690 decode, 6.1) for flat and bull tools; the tool type field tool +0x10 (Eva +0x218); the step's
   segment filter word step +0x30 / +0x34 (overwritten from Eva +0x6e0 / +0x6e4, pp l.2152-2172) - only shank contacts depend on it.
4. Overthickness: the grown tool FUN_18013e330, the refined marks of FUN_180162eb0 (pn l.14181, the bisection between the bracket
   drops on the offset contact points), the pre-split FUN_18015e210 (FUN_18015c4c0 param_7 = Eva +0x1350 = the net's +0x360 =
   PencilOverthickness != 0).
5. The pass drop object (Eva +0x4f0; FUN_180118ee0 / FUN_1801194d0 / FUN_180121900; tolerances 0.0003 at +0x510 and 0.01 at +0x658,
   pp l.2322 / 2325): how the straight moves between the pass points are dropped. Also the Z offset Eva +0x140 (FUN_180150e90 param_4;
   0 measured, 7) and the per-point flag FUN_18015e160.
6. The row scan bookkeeping (FUN_180155740: scan / chain / kill rows, "step back", "unchain") and the pairing of chains across rows
   (FUN_1801549b0: "PC Pairs", "Mismatch pair"): they assemble the same chains row by row; which end a chain starts from (the walk's
   first mark, FUN_18015d4d0) follows from them.
7. The Pencil Linking Plan 0x1442 (order of the passes, links); the trim FUN_1802ed680 (boundaries / Z range / surfaces).
8. ContactAngleLo / Hi on the passes; Horizontal offset 0x16ca, approximate start point 0x16bf, Tip compensation; ShaftProfileEnable
   FUN_1802ec5a0; the stock-model branch; check surfaces.
9. ToolShaftRadius for a Mastercam tool (so PencilCellResolution); StepMachTolerance's record field; the mccore op -> TP copy (the
   record offsets marked [inferred] in section 1).

## 6. The net's geometry (decoded, second pass 2026-10-26)

### 6.1 The drop and its contact
- A net drop is a 0x30-byte node (FUN_180151430 pool, filled by FUN_180151230 pn l.1347-1379 = the tool box drop FUN_180222a90 at an
  XY point with the plan's tool +0x2d0): [0] x, [1] y, [2] z (the tool tip), +0x18 the contact code (int + uint), +0x28 the pool link.
- The contact normal FUN_1802538d0 (decomp/raster/tb.c l.248-336): the uint holds a 24-bit fraction f (x 2^-24), a 6-bit tool profile
  segment s (bits 25-30) and a flag (bit 24). s = 0, flag set: the corner arc (torus / ball); s >= 1: the side profile segments (shank)
  from the profile table tool [8] / [9]; the (r, z) normal is turned about Z by the int (FUN_18005bd20). The contact point offset
  FUN_180253690 (st l.162-231): +0x10 = the contact's height above the tip; for the ball arc R (1 + (f - 1) / sqrt((f - 1)^2 + f^2)) =
  R (1 - n_z).
- Ported (ours): an exact ball drop on our triangles (facet, edge and vertex contacts), n = (centre - contact point) / R. Drops with
  no triangle under the tool: no contact (6.4).

### 6.2 The step parameters and the three tests
Step block S = Eva +0x13d0 (FUN_180253300 d2 l.1-44): S[0] = MinHorizontalChange h, S[1] = h^2, S[2] = -1, S[3] = CellResolution,
S[4] = MaxContactPointChordChange c, S[5] = MaxVerticalStepforward v, +0x30 int / +0x34 byte (segment filter, 5.3), S[7] = the tool,
S[8..11] = -1 (the angle part is off: param_8 = -1).
- **d20(a, b)** FUN_180253d20 (st l.1-85): with a segment filter set (+0x30 != -1) a contact on a profile segment other than 0 gives 0
  (unless +0x30 = 1 and segment 1); then 0 ("no discontinuity") when |n(a) - n(b)|^2 <= c^2 and (tool +0x10 != 1 or |z(a) - z(b)| <= v),
  else 1. (S[8] = -1 skips the rest.)
- **f30(a, b)** FUN_180253f30 (st l.86-161): 0 when XY |ab|^2 <= h^2; 1 when d20; (S[8] = -1 skips the arc test); 0 when (S[3] = -1 or
  |ab|^2 <= S[3]^2) and |z(a) - z(b)| <= v (and S[2] = -1); else 1.
- **cons(p3, p4, p5, p6)** FUN_1802541f0 (st l.232-280): 0 when XY |p3 p4|^2 <= h^2; when |z(p5) - z(p6)| <= v and |n(p5) - n(p6)|^2
  <= c^2: 0 when |p3 p4| <= S[3] and |z(p3) - z(p4)| <= v (S[2] = -1), else 1; otherwise 1. "0" = the two pairs continue one bitangent.

### 6.3 The grid (FUN_180150120 pn l.661-702, FUN_180150520 pn l.791-1045)
- The net's CWeave (FUN_18012bc80 / FUN_18012be70, hw) over the Surfaces' box grown by the fibre spacing PencilBigCellResolution:
  per axis n = max(3, int(range / spacing) + 2) fibres evenly spaced (hst3d_port_plan.md 4.5).
- (nx + 1) x (ny + 1) PencilQuadSuper cells of 0x148 bytes; cell (i, j) spans the fibres i - 1 .. i, j - 1 .. j (the border ones are
  degenerate) and owns the drop at fibre crossing (i, j) (local_res18, pn l.920-930). Its type is 3 or 2 (pn l.951-976, 5.2).
- Stage 2 FUN_180151810 (pn l.1577-1630): every cell of type 2 (or with a type-2 neighbour at +0x50 / +0x58) searches its two edges
  +0x30 -> +0x38 and +0x40 -> +0x38 with FUN_1801611d0.

### 6.4 The edge search (FUN_1801611d0 pn l.12770-12890, FUN_180160ad0 pn l.12439-12769)
- An edge is a doubly linked list of drops between its two corner drops. FUN_1801611d0: with no inner drops yet, f30 on the whole edge
  - false: d20 -> one mark (FUN_180162eb0); else split at the midpoint (a new drop, FUN_180151230) and search both halves.
- FUN_180160ad0: an explicit stack of intervals; per interval [a, b]: f30 -> drop at the XY midpoint (only the coordinate along the
  edge is averaged, param_10), insert it, push the halves that still need it; a leaf interval with d20 -> a mark.
- The mark (FUN_180162eb0 pn l.14181-14579) with PencilOverthickness = 0 (net +0x360) is the bare sentinel PTR_DAT_1809a1148 on the
  interval: the bitangent lies between the two bracket drops, which are at most h apart (a d20 interval is split until |ab| <= h).
  With overthickness it bisects between the drops' offset contact points (5.4).
- Ported. Ours: a drop with no contact never makes a mark (5.2 - with Mastercam's (0, 0, 1) normal there every rim of the drive would
  be marked; on PENCILSEAMS the type-3 cells evidently prevent that: Mastercam has no pass on the part's outline).

### 6.5 The cells (FUN_180161570 pn l.12891-13220, FUN_18015f530 pn l.11600-12026)
Per leaf quad (FUN_180161bf0 walks the cell's quad tree): s = |corner +0x28 - corner +0x38| (the diagonal), k = the marks on its four
sides (FUN_18015f350 pn l.11544-11599: FUN_18015e860 on the sides shared with the +0x50 / +0x58 neighbours, FUN_18015f0d0 on
+0x48 / +0x60; FUN_180161570 then reads the four lists so that each mark's drops come in boundary order, pn l.12935-13060).
- s >= h and (k > 2 or k = 1 or s >= CellResolution) -> split; s >= h and k = 2 -> split unless cons(Y1, X2, X1, Y2) = 0 and
  cons(Y2, X1, X2, Y1) = 0 (marks M1 = (X1, Y1), M2 = (X2, Y2), bracket drops in boundary order, so Y1 / X2 and X1 / Y2 are the same
  side of the bitangent); else a leaf. (With overthickness marks FUN_180252d90 is used instead.)
- The split (FUN_18015f530): a drop at the centre (x from corners [5] / [6], y from [5] / [8]); four children; each side's midpoint
  drop (made with its two halves searched when missing); the two new edges through the centre searched (FUN_1801611d0 with param_9 =
  1). Every cell therefore ends below CellResolution, and cells with odd or crossing marks go on down to h.

### 6.6 Chains (FUN_180162190 pn l.13542-13593, FUN_180161f40 pn l.13435-13541, FUN_18015d700 pn l.10014-10342, FUN_1801602a0 pn l.12027-12076)
- FUN_180161f40 starts a chain at every unused mark of a cell with marks; FUN_18015d700 walks: leave the cell through a mark, enter the
  neighbour; continue only through cells with exactly 2 marks (+0x88 == 2 -> -2 "used"); stop elsewhere.
- Chain ends: when the start / end cell has more than 2 marks, the chain gets that cell's corner drop with the lowest contact (tip z +
  contact height, FUN_1801602a0) as an end point (+0x30 / +0x38).
- An entry is (in, out, refined mark) = the bracket drops ordered by the walk (local_68 = flag equality, the side rule) - see 6.7 for
  which side "in" is. Appending entry E: when the chain has 2 or more entries and P = the one before the last satisfies cons(P.in,
  E.in, P.out, E.out) = 0 and cons(P.out, E.out, P.in, E.in) = 0, the last entry is removed (pn l.10210-10230). The kept entries are
  therefore about CellResolution apart on straight seams and closer where the normals turn by more than c.
- Ported as one pass over the final quad tree (ours: the row bookkeeping of 5.6 is not reproduced).

### 6.7 The filter, the climb split and the pieces' side (FUN_18015c4c0 pn l.9223-9898, FUN_18015c020 pn l.9086-9222, FUN_18015bcf0 pn l.8936-9085)
Called from FUN_1803cb3f0 (pp l.2266-2283) with v = MaxVerticalStepforward, the threshold chord, MinFragLength, param_6 = (Eva +0x218 =
tool +0x10 == 1), param_7 = (Eva +0x1350 != 0) = overthickness on.
- **FUN_18015c020(list, v, tool, chord, minlen, side, flat)** returns 1 = discard: strong = the entries with |n(in) - n(out)| > chord;
  for consecutive entries L += |cur.in - prev.out| (XY) and, when p . Dn(prev) <= 0 and p . Dn(cur) <= 0 with Dn = n(in) - n(out) and p
  the move's left normal (side 0) or right normal (side 1), C += the same length (the "convex" part); kept (0) only when strong > 2
  and (minlen = -1 or L > minlen) and C <= 0.9 L; "Discarding convex pencil piece" is logged when C > 0.9 L. (flat = tool type 1
  replaces the convex test by a vertical-step count.)
- The chain is tested with side 0 and minlen -1 first.
- **Split**: c = the climb of an entry = (z(in) + contact height(in)) - (z(out) + contact height(out)). Split entries S: the first
  entry with c > v or c < -v, then each later one where the sign beyond v changes. Each split S[i+1] is then moved back to the entry s in
  [S[i], S[i+1]) minimising the largest wrong-way climb (entries before s against the earlier piece's sign, from s on against the
  later one's; first minimum), when that is below v. S[0] := the chain's start; no split -> one piece with the sign of the largest |c|.
- **Pieces** FUN_18015bcf0: entries [S[i], S[i+1]); when the next entry already climbs the other way by v or more, a separate two-entry
  "vertical climb-changing piece" (a copy of the last entry, the next entry; in / out of the first swapped for an up piece, of the last
  for a down piece) is made after it, else the piece also takes the next entry. Then a **down piece (c < 0) is reversed and an up
  piece (c > 0) gets in / out swapped** - in every piece "in" is the side with the lower contact. Each piece: FUN_18015c020 with side 1
  and minlen = MinFragLength.
- The side of "in" in the walk: the walk's side rule was not followed through the PencilQuad lists, but only one choice keeps a
  concave seam (floor / wall) through both convex tests: "in" on the right of the walk (side 0 on the chain), which after the
  normalisation leaves the lower side on the left of each piece (side 1). Mastercam keeps such seams (PENCILSEAMS), so: in = right of
  the walk [decoded from the two tests + measured]. A ridge (normals turning towards the in side) is the "convex" case.
- Measured: the passes of the ported net follow the lower bracket drop as Mastercam's centre passes do (7); before this was decoded a
  "lower drop" substitute gave the same result.

### 6.8 The pass (FUN_180150e90 pn l.1189-1346, FUN_180150b70 pn l.1046-1188)
- Points = the entries' "in" drops (x, y, z + Eva +0x140 as a float, 0 here), preceded / followed by the junction drops of 6.6 when they
  differ from the first / last; a closed chain (start cell = end cell) repeats its first entry.
- FUN_180150b70: between two consecutive points a, b with cons(a, b, a, b) != 0 a drop at the XY midpoint is inserted (and the pair
  tested again); logged "Unexpected points required in pencilpath" for the main list.
- The points go to the drop object (5.5) and become the pass; then the trim FUN_1802ed680.

## 7. Oracle (tools/tests/pencil_test.sh; the only stored op 132 style 6 with NCI, [measured] with tools/mcam_ops.py)

Corpus search (2026-10-26): every corpus file was scanned for op 132 (`~/mastercam-re/work/hst3d/netscan/scan3d.tsv`, 1460 files)
and the files named *PENCIL* were listed again: PENCILSEAMS.mcam (2017) op 1 is the only HST Pencil with NCI (the 2018 copy has no
NCI; PENCILPART / SURFACE PLUNGE RASTER AND PENCIL hold no ops readable here; the X9 PENCILSEAMS is the classic Pencil op 39).

`<mastercam-samples>/net/emastercam-training/2017/Mastercam 2017 Handbook Volume 2 Files/MCX Files/PENCILSEAMS.mcam`
op 1 (the 2018 copy has no NCI; `~/mastercam-re/work/hst3d/netscan/scan3d.tsv`). Drive: Mastercam surfaces (entity classes 0x80 x 23,
0x100 x 61), no XT.

| field | value |
|---|---|
| tool 0x350 / 0x358 | D 0.25 / Rc 0.125 in (ball) |
| Bitangency 0x12d8 / Overthickness 0x12d0 | 2.96706 rad (170 deg -> MaxNormalChange 10) / 0 |
| **offsets 0x16cb / 0x1540** | **1 / 1** -> the offsets path (Pencil + Constant Stepover From Passes 0x140d, Constant Surface Stepover Linker 0x1420) |
| Stepover 0x1230 | 0.05 |
| angles 0x1248 / 0x1250 | 0 / 90 deg |
| stock 0x13c0 / 0x13c8 | 0 / 0 |
| tolerance 0x12a0 | 0.001 |
| Use Z depths 0x16c9 | 0 |
| cut method 0x16bd / 0x16d4 / 0x16d5 | 0 / 0 / 0 (one way) |
| Overlap / Shallow angle 0x1380 / 0x1388 | 0.032 / 5 deg |
| keep tool down 0x16cd / 0x1308 / 0x1310 | 1 / 0.5 / 100 % |
| other non-zero bytes 0x16b8-0x1740 | 16be 16c0 16c4(2) 16c7 16c8 16cd 16cf 16d0 16d1 16dd |

NCI: **1581 moves** (1572 G1, 9 G0), Z 0 .. 2, X -1.405 .. 1.405, Y -3.495 .. -0.147. Three cut runs between rapids: 548 moves
(13.95 in), 924 moves (25.93 in), 100 moves (2.09 in); 15 single G1 moves longer than 0.1 in (up to 1.86 in) inside the runs
(stay-down links [inferred]). Because the stored op uses one offset, it checks the pencil net only through the offset plan; a run
with 0x16cb = 0 would need Mastercam to regenerate (no such NCI on disk).

**Results** (tools/tests/pencil_test.sh: tools/tests/pencil_test regenerates the op from its record and its own drive surfaces; samples
every 0.01 in; Mastercam's plunges / long links are its own):
- As stored (offsets 1): refused - "offsets (Limit # of offsets): the Constant Stepover Passes From Passes plan 0x140d is NOT DECODED".
- Offsets switched off (`--rec 16cb=b:0`): 6 chains, all kept (no convex / short / few-mark drops), 167 points, 14.4 in of passes;
  **ours -> Mastercam p50 0.0032, p95 0.0071, max 0.0097 in; 100 % of ours within 0.01 in of Mastercam's passes**; 16.1 in of
  Mastercam's 51 in of cutting moves lie within 0.01 of ours (its centre passes - the other two thirds are the offsets at +-0.05,
  which ours does not make: ref -> ours p50 0.0497 = the stepover). The chains end where Mastercam's centre passes meet (the junction
  drops), the arc over the dome (z up to 0.56) is Mastercam's run 3.
- Differences: the pass direction - ours has the lower side on the left (6.7); Mastercam's stored passes run the other way (the offsets
  plan / its linker 0x1420 decide that, NOT DECODED); the deepest gouge of our straight moves between points 0.008 in (at a junction;
  Mastercam's pass drop is NOT DECODED, 5.5); where a seam runs within MinHorizontalChange of a grid line (y -2.804) our points stay
  on the line (0.002 off the seam) - Mastercam's grid lines are elsewhere (its Surfaces box is its tessellation's).
- History of this pass (same oracle): first port p95 0.0097 / max 0.0196 / 96 % on ref (chains through junctions, in = an arbitrary side);
  junction ends + lower side: p95 0.0076 / max 0.0097 / 100 %; decoded split / normalisation: p95 0.0071.
- Self-check (no Mastercam NCI): TUTORIAL 2 SWARF_METRIC op 4 turned into a Pencil (`--rec 16bc=b:6 --rec 16cb=b:0 --rec 16c9=b:0`, stock
  0.5 / 0.5, `--tool 10 5`): one closed pass around the part's foot, 4.4 s.

## 8. Port (viewer/cam3d_pencil.c, cam3d_pencil.h; tools/tests/pencil_test.c / .sh)

- Engine: `gen_pencil(f, op, MB *)` (static, for cam2d.c's dispatcher, included after cam3d_finish.c) and `cam3d_pencil(f, op)`
  (fills op->m / op->nm). **Wired 2026-10-28:** CAM_PENCIL (cam2d.h, cam_hst_style 6; the 3D gallery's "Pencil" and the right-click
  menu's Surface high speed > Pencil; cam_cli "pencil"); the default record = the defaults file's first op 132 with style 6 (Mill_MM
  op 71 / Mill_Inch op 73); the Cut Parameters page 1329 (section 4; opdlg_fields.tsv s1329, the Limit # of offsets check / number /
  reference diameter as InitPage / OK FUN_180096920 / FUN_180096240; Overlap / Shallow angle enabled for Down / Up Mill only, Horizontal
  offset disabled). Scene tools/tests/op_scenes.txt gen_pencil (BRACKET4, 10 mm ball: 163 points).
- Decoded and ported: section 1 parameters (MaxNormalChange = 180 - Bitangency angle, the degree sin of 1.1, threshold + 5, v =
  0.5 / 2.0, MinFragLength 0.1, BigCell = 7 Cell), 6.2 - 6.8.
- Ours (labelled in the code): the drop on our tessellation (OCCT; exact ball drop, facet / edge / vertex); the cell types (every cell
  used, no marks against a drop without contact); one global pass instead of the row bookkeeping; which end a chain starts from;
  ToolShaftRadius = D / 2; MinHorizontalChange = StepMinHorizontalChange's default 0.003 (scallop.md 1); the pass order (nearest start,
  each pass in the net's direction, Other Way reverses) and SAFE links; straight moves between pass points.
- Refused: offsets, Overthickness, non-ball tools, unequal XY / Z stock, Zigzag, and hst_grid_open's (Down / Up Mill, approximate start
  point, contact angles other than 0..90, Use Z depths). S7: check surfaces are meshed with the drive (style 6 -> 0x172c = 0, FUN_180258310(2),
  surface_selection.md 8.1; warning 287); containment = every chain of the op (8.2). Not read: rest surfaces, the trim to boundaries other than the
  containment (cut as hst_grid_pass does).
- Tried and rejected (ours): re-dropping the moves' midpoints to the cut tolerance - on the near-vertical walls of PENCILSEAMS it puts
  points 0.25 up the wall, which Mastercam's passes do not have (p95 0.061).

## 9. N9-15 update (2026-11-09; audit formats/audit_2026-11-09_highspeed_surface2.md)
- **Units decoded.** Job +0x80 = 25.4 for an inch part (FUN_18007bb48 mk l.91625-91633), every length through FUN_1800edd8c: McHmm's defaults are mm. The port now scales MinHorizontalChange 0.003, CellResolution min(2.5, 2 ToolShaftRadius), MaxVerticalStepforward 0.5 / 2.0 and MinFragLength 0.1 by part units per mm (they were used raw on inch parts).
- **Eva+0x140 decoded**: ToolThicknessZ 0x1d20 (FUN_1802f4c80, stored at 1802f2cff in FUN_1802f2340; 0 for plan 0x1439). FUN_180150e90 adds it to every pass Z: tip output = drop + ToolThicknessZ (5.5 closed for this point).
- **Pass object decoded**: FUN_180118ee0 / FUN_1801194d0 / FUN_180121900 (hst3d_weave_drop.c) record the points; the moves are straight. FUN_1801194d0 (l.5922-5975) counts changes of the per-point flag (+0x170, l.5940) and does not end a fragment; a point exactly equal to the last one is skipped (FUN_18002d030). FUN_18015e160 flags a point in range when tip z + Eva+0x140 lies in [Eva+0x6c0, +0x6c8] and the contact's nz in [Eva+0x678, +0x680].
- **local_868**: when the corner radius is below StepMachTolerance (mk l.87400-87407, Pencil only), (tol - Rc) / 2 is added to ToolThicknessXY and Z, ported (and -(tol - Rc) / 2 added to StepMachTolerance, not used by this port). The midpoint insertion FUN_180150b70 runs only when net+0x360 = PencilOverthickness = 0 (FUN_180150e90); Overthickness stays refused.
- **Offsets 0x140d**: the evaluator is decompiled in decomp/scallop/cs.c: FUN_180337970 (1206 bytes) / FUN_180337e30 (4720 bytes) on the Scallop offset loop FUN_1803343e0 (not ported; left for the user, see the audit).
- **Cell types**: FUN_180131810 -> FUN_180289d90 = a MeasureRadSegs2D of the fibre against the segment set of Eva+0x118 (the Surfaces tool box by its use at pp l.1981-1984); which set that is for this plan is NOT DECODED, so the labelled "every cell used" stays.
