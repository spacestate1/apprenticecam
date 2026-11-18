# 2D High Speed Area Mill (op 134, style 0x1228 = 1): where it is computed and what is decoded

Sources:
- `decomp/mcmill_all.c` (cited **mm l.N**);
- `decomp/mckernel_all.c` (cited **mk l.N**);
- mccore.dll from Ghidra `~/mastercam-re/ghidra_core` / `mccore`, a DumpDecomp of `run_mchutl_coreareamill|MutlConvertRawToEMMWithToolOffset|MutlZrestAddTrochoidalMovesMill`, depth 1 (cited **core FUN_x**);
- mcmill vtables and references from Ghidra `~/mastercam-re/ghidra` / `mc2017` (VtableDump `CHST2D|HST2D`, RefsTo, StringRefs);
- the disassembly of `~/mastercam-re/files/mcmill.dll` (cited **dis ADDR**);
- export and import tables (`objdump -p`) of mccore.dll, mckernel.dll and mchmm.dll.

Offsets follow two conventions:
- "record 0xNNN" is the op record offset, the one `cam_fileop.c` uses: the op struct offset + 0x68 (formats/mcmill_2d_engines.md 0.1);
- names such as `S+x`, `WI+x`, `dat+x`, `T+x`, `P+x` are object offsets defined where they first appear.

## 0. Summary: the pattern is not in mcmill (and not in mcce)

The work item `CHST2DWorkItemStd` only:
- writes the machining region as **layered 2D boundaries**;
- turns them into a **triangle model**;
- asks Mastercam's NCG CAM kernel to rough that model.

The kernel is **McHmm.dll**, the `CNCGDCInterface` class. It gets a "Machining Passes" macro built by mckernel.dll. The pass pattern lives inside McHmm.dll, and so do the stepover logic, cut order, climb, entries, linking, smoothing and finish. McHmm.dll is now imported (section 11): the Area Clearance pass positions and the profile-ramp parameters are decoded there; Core Roughing passes and the linker are not. What is decoded on the Mastercam side (sections 2-8):
- the parameter plumbing from the op record to the macro;
- the plan graph;
- the region / layer / model preprocessing;
- the trochoidal post-step conditions;
- how the depth passes call the engine;
- how the kernel output is read back.

Call chain (each line cited):
```
CHST2DProcess::PackageSomething (mm 1802b1e60)                          per depth / island-top level
  -> CHST2DStandard vtable[1] = FUN_18029d3e0 (VtableDump 180377610 [1])    writes "areaclear2b<n>.tmp" layers
       FUN_1802bc670 per layer (mm l.456063, 456074); FUN_1802b30f0 creates CHST2DWorkItemStd (mm l.456104)
  -> CHST2DWorkItemStd vtable[1] = FUN_1802a5ca0 (VtableDump 1803774f0 [1])
       FUN_1802a7010 (l.461690), FUN_1802ba330 (l.461698), rename the layer file to "areaclear2b..." (l.461748)
       FUN_1802a1670 (l.461767): layers -> polygons -> triangle model "tmpCoreMill<s>.raw" -> hmmCoreAreaMillDat
            -> mccore run_mchutl_coreareamill / _ext (mm l.460117 / 460132)
                 core: MC2DHSTCoreAreaMill (TP2DHSTCoreAreaMill + input files), copies dat (core FUN_180917530),
                 TP2DHSTCoreAreaMill::Compute (mckernel export 0x47) -> output copied to "tmpCoreMill<s>.tmp"
                   mk TP2DHSTCoreAreaMill::Compute (mk l.225623) -> HSTCoreAreaMill FUN_18006ad24 / FUN_18006adf4:
                   FUN_18006bd90 builds the macro "// Mastercam X - Machining Passes Macro" (mk l.83366)
                   FUN_18006ae1c: CNCGDCInterface::Open / RunMacro / ReadTag (mk FUN_1800f14c0, FUN_1800f155c,
                   FUN_1800f0e10) -> "tmpCoreMillmco.tmp"
                   CNCGDCInterface is exported by McHmm.dll (objdump -p mchmm.dll: RunMacro, Open, ReadTag, ...);
                   mckernel delay-loads "McHmm.dll" (strings mckernel.dll)               <-- pattern computed here
            optional trochoidal post-step (section 6)
       FUN_1802abad0 (l.461796): kernel motion records -> toolpath file (section 8)
       FUN_1802a66c0 (l.461801): appends to the op (NOT DECODED)
```

The earlier note "NOT DECODED, separate mcmill-side engine" in formats/mcce_engines.md section 0 is resolved: it is this kernel chain. mckernel does import `MCCECalcOffset` and `MCCECalcDynPock` (objdump -p mckernel.dll), but HSTCoreAreaMill does not use them. It drives McHmm through `CNCGDCInterface` (mk l.82945-83000).

## 1. Op record -> CHST2DProcess -> CHST2DStandard -> work item

`CHST2DProcess::SetOperationID` (mm l.475267) reads the record into `local_2958`, so record offset = 0x2958 − local. Its call to `SetParams` (mm l.475366) maps:

| SetParams arg -> process field | record | used here as |
|---|---|---|
| p1 -> +0x48 | 0x1288 (12553) | **tolerance**; < 1e-12 -> 0.001 inch / 0.025 mm (mm l.475476-475483) |
| p2 -> +0x54 | 0x1228 | style (1 = Area Mill) |
| p8 -> +0x55 | 0x1567 (12984) for style < 2 (mm l.475501-475506: p8 = p47) | |
| p47 -> +1 | 0x1567, or the previous +1 when process byte +0 is set (mm l.475347-475349; where +0 is set: NOT DECODED) | |
| p48 -> +0x16a | 0x17b8 (41142) | trim-boundary option |
| +0x56 | 1 when ((0x868 depth cuts and 0x892) or 0x1740) and style != 2 (mm l.475537-475541) | island-top levels |

The rest of SetParams (p3-p46 -> record 0x868, 0x870, 0x878, 0x880, 0x16d8-0x16fe, 0x1760, 0x1778, heights 0x170-0x1a8, stock 0x2e0 / 0x2e8 / 0x2f0, ...) is listed in the call at mm l.475366. Those fields are used by the depth / offset code (section 7), not by the kernel macro.

`PackageMachiningRegion` (mm l.469713) creates the `CHST2DStandard` object S with `FUN_1802ba330` (mm l.475424):

| S field | value |
|---|---|
| S+8 | op id |
| S+0x60 | style |
| S+0x68 | tol (0x1288) |
| S+0x70 | process +0x56 |
| S+0x71 | process +0x55 (0x1567) |
| S+0x78 / +0x80 | process +0x38 / +0x40 (source **NOT DECODED**) |
| S+0x88 | process +1 (0x1567) |
| S+0x89 | +0x16a (0x17b8) |

The work item `FUN_1802b30f0` (mm l.470508; field stores l.470612-470629) receives these from FUN_18029d3e0 (mm l.456028-456104):

| WI field | value | FUN_1802a1670 parameter |
|---|---|---|
| +0xb0 | number of layers written | param_2 |
| +0xb8 | (in_stack_28 == 0) | param_3 = **horizontal** (mode 5) |
| +0xb9 | (S+0x60 == 1 && S+0x88); with S+0x89: S+0x71 | param_4 |
| +0xba | 1 if S+0x60 == 1 && S+0x89, else 0 | param_5 |
| +0xbb | S+0x71 | param_6 |
| +0x78 | S+0x68 = tol | param_8 |
| +0x80 / +0x88 / +0x90 | **0 / 0 / 0** (l.456098-456100) | param_9 / 10 / 11 |
| +0x98 | in_stack_30 − in_stack_38 (l.456103) | param_12 |
| +0x68 / +0x70 | S+0x78 / +0x80 | param_13 |
| +0xbc | S+0x70 | param_14 |
| +0xa0 | in_stack_38 | param_15 = floor Z of this pass |
| +0xb4 | in_stack_48 | pass index |
| +0xa8 | in_stack_30 | |

FUN_1802a5ca0 passes these to FUN_1802a1670 (mm l.461752-461769).

## 2. Kernel input record hmmCoreAreaMillDat (FUN_1802a1670 mm l.460030-460115)

The op is re-read into `local_3258` (`DatabaseRetrieve`) and copied to `local_5b48` (mm l.459218-459258), so record offset = 0x5bb0 − local. The dat is copied into TP2DHSTCoreAreaMill T by core FUN_180917530.

| dat | from | record / value | T |
|---|---|---|---|
| +0x00 | local_5860 | 0x350 tool diameter | +0x20 |
| +0x08 | local_4620 | 0x1590 (12973) | +0x30 = P[0] |
| +0x10 | local_4618 | 0x1598 (12974) | +0x38 = P[1] |
| +0x18 | local_4608 | 0x15a8 smoothing tolerance | +0x40 |
| +0x20 | uStack_4600 | 0x15b0 smoothing corner tolerance | +0x48 |
| +0x28 | local_4610 | 0x15a0 smoothing max radius | +0x50 |
| +0x30 | param_8 | tol (0x1288) | +0x28 |
| +0x38 | from param_9 / 10 (l.460036-460071) | **0** on this path (both 0) | +0x58 |
| +0x40 | local_45e0 | 0x15d0 helix radius | +0x60 |
| +0x48 | \|param_12\| + local_45d8 | \|in30 − in38\| + 0x15d8 | +0x68 |
| +0x50 / 58 / 60 / 68 | local_45d0 .. uStack_45b8 | 0x15e0 ramp angle / 0x15e8 / 0x15f0 / 0x15f8 | +0x70 .. +0x88 |
| +0x70 / +0x78 | 1.0 / 1.0 (l.460073); mode 0: FUN_1802a9c60 (l.460083) | | +0x90 / +0x98 |
| +0x80 / 81 / 82 | local_45e7 / e6 / e5 | 0x15c9 / **0x15ca entry style** / 0x15cb | +0xa0 / a1 / a2 |
| +0x83 | local_464a | 0x1566 smoothing on | +0xa3 |
| +0x84 (int) | **mode** (l.460074-460093) | see below | +0xa4 |
| +0x88 | param_6 in mode 4, else 0 | | +0xa8 |
| +0x89 | local_45e3 | 0x15cd gap by distance / % | +0xa9 |
| +0x90 / +0x98 | local_45b0 / uStack_45a8 | 0x1600 / 0x1608 | +0xb0 / +0xb8 |
| +0xa0 | local_464c | 0x1564 | +0xc0 |
| +0xa8 | local_5838 | 0x378 | +0xc8 |
| +0xb0 / +0xb8 | local_4570 / uStack_4568 | 0x1640 / 0x1648 | +0xd0 / +0xd8 |
| +0xc0 | local_44f6 | 0x16ba | +0xe0 |
| +0xc8 / +0xd0 | *param_13 | S+0x78 / +0x80 | +0xe8 / +0xf0 |
| +0xd8 / +0xe0 | local_4418 / uStack_4410 | 0x1798 / 0x17a0 | +0xf8 / +0x100 |

**Mode** (dat+0x84) is set as follows:
- param_3 (horizontal) -> **5**;
- else param_5 (record 0x17b8) -> **4**, with dat+0x88 = param_6 (0x1567);
- else param_4 (0x1567) == 0 -> **1**;
- else -> **0**.

For style 1, param_4 and param_6 are both record 0x1567. So:
- **"from outside" (0x1567 = 1) runs Core Roughing**;
- **"stay inside" (0x1567 = 0) runs Area Clearance** (section 3).

In the user's parts, NAKTO op 3 has 0x1567 = 1 (mode 0) and the buckle ops have 0x1567 = 0 (mode 1).

## 3. The NCG macro (mckernel FUN_18006bd90 mk l.83366-83580; parameters FUN_18006b750, FUN_18006af68)

HSTCoreAreaMill H (FUN_18006ad24 mk l.82894) has these fields:
- H+0x40 = system context. Its +8 = IsMetric and +0x10 = system `tol` (core MCSystemContext::MCSystemContext).
- H+0x48 = &T+0x20, which holds D, then T+0x28 = tol.
- H+0x50 = P = &T+0x30.
- **H+0x38 = unit scale**: 25.4 inch, 1.0 metric (mk l.83399-83405).

Every length is written as value × scale with "%19.9f" (FUN_1800edd8c), so the kernel works in mm.

Plans (`FUN_1800ef300(comment, name, type id, inputs, ...)`):
1. Mode 4 only: `ekPlanTcb3` = Boundaries Plan 0x1427 from `tmptc2D.tmp` (FUN_18006c4f8 mk l.83581).
2. `ekPlan0` = Database Loader Plan 0x1421.
3. `ekPlan1` = Detect Top Surfaces Plan 0x1423.
4. `ekPlan2` = Surface Tessellation Plan 0x1424.
5. Modes 0 / 4+core: OffsetDistance = 0.03937 inch / 1.0 metric (mk l.83444-83459).
6. `ekPlan3` = Bounding Box Plan 0x1425.
7. Modes 0 / 4+core: `ekPlan3s` = Silhouette Boundary Plan 0x140b.
8. Tool and pass parameters (FUN_18006b750, table below).
9. **Passes plan** `ekPlan4`:

   | mode | passes plan |
   |---|---|
   | 5 | Horizontal Passes 0x143a |
   | 0 | **Core Roughing Passes 0x1460** |
   | 1 | **Area Clearance Passes 0x1435** |
   | 4 | `ekPlan4a` = Area Clearance 0x1435 (or Core Roughing 0x1460 when dat+0x88), then OverThickness 0, EnableTrimToZRange false, TrimBoundaryThickness 0.005, then `ekPlan4` = Edit Passes to Boundaries 0x1416 |

10. Ramp and link parameters (FUN_18006af68).
11. **Linking plan** `ekPlan5`:

    | mode | linking plan |
    |---|---|
    | 5 | Horizontal Linking 0x144c |
    | 0 | Core Roughing Linking 0x1461 |
    | 1 | Area Clearance Linking 0x141d |
    | 4 | 0x141d or 0x1461 |

FUN_18006b750 (mk l.83223-83364):

| macro parameter | value | line |
|---|---|---|
| ToolCornerRadius | D × 0.05 | 83247 |
| ToolCutterDiameter | D | 83253 |
| ToolThicknessXY | max(0, T+0x58) = 0 on this path | 83258 |
| ToolThicknessZ, PosZLo, PosZHi | 0 | 83263-83283 |
| StepMachTolerance | tol (record 0x1288) | 83268 |
| ToolToolType | 1 | 83284 |
| **MinOffset / MaxOffset** | P[0] = 0x1590 / P[1] = 0x1598, written when P[1] > tol and P[0] ≤ P[1]; P[1] ≤ tol -> Max = 0.45 D; Min = 0.55 Max when replaced | 83286-83310 |
| ProfileSmoothing | 0x1566; on: CornerTolerance 0x15a8, MaxRadius 0x15a0, CornerOffsetTolerance 0x15b0 | 83311-83337 |
| CoreRoughingTrimOffset | D/2 − 10 tol, if > tol (core modes) | 83339-83349 |
| CoreRoughingExpansionX / Y | T+0x90 / +0x98 (core modes) | 83351-83360 |

In the user's parts, 0x1590 = 1.485 and 0x1598 = 2.7 (D 6, 0x1580 = 45 %, 0x1588 = 55 %). So 0x1598 is the maximum stepover and 0x1590 the minimum, contrary to the MP texts of 12973 / 12974.

FUN_18006af68 (mk l.83010-83221):

| macro parameter | value |
|---|---|
| ProfileRampStyle | P+0x71 = **0x15ca** (1 helix / 0 profile ramp) |
| helix: HelixDiameter | 2 × 0x15d0, else 0.3 inch / 6.0 metric |
| ramp: MinProfileRampDiameter | 0x15f0, else 0.3 / 6.0 |
| MaxRampAngle | 0x15e0 in radians -> degrees, else 3° |
| RampHeightOffset | \|in30 − in38\| + 0x15d8 |
| LinkRetractStyle | 2 |
| **LinkClimbMilling** | record **0x1564 == 0** |
| LinkMaxStayOnSurface | 0x15cd = 1: 0x1608 % × D; 0x15cd = 0: 0x1600; else 0.15 inch / 2.5 mm |
| ProfileLinkRadius | 0x1590 / 4, written when 0x1590 > tol and 0x1590 / 4 ≤ 0.5 mm (0.019685 inch) |
| MinProfileDiameter | 0x15f8 |
| EnableMaxCuttingDist / MaxCuttingDist | 0x16ba = 1: 0x1640; 0x16ba = 2: 0x1648 × 0x378 |
| LinkStartX / Y | T+0xe8 / +0xf0, skipped when both are −10000 |
| LinkVerticalLeadIn / OutRadius | 0x1798 / 0x17a0 |

**What the plans do with these (pass shape, stepover choice between MinOffset and MaxOffset, cut order, entry placement, link moves, smoothing): NOT DECODED.** That code is in McHmm.dll.

## 4. Region preprocessing: layer file and triangle model

**Layer file `areaclear2b<s>.tmp`** (FUN_1802bc670 mm l.476882-477045). Each layer is:
1. int32 count = entities + 1 (l.476952);
2. double Z;
3. count records of 0x60 bytes:
   - +0 end x, y;
   - +0x10 arc centre;
   - +0x20 cw flag (sweep < 0, l.476991);
   - +0x21 type 'L' / 'A';
   - +0x48 full circle (\|\|sweep\| − 2π\| < tol, l.476988).

The first record is an 'L' holding the start point.

The source is a pock_bound with:
- +0xc = count;
- +0x10 = entities of 0x88 bytes: +8 type; line start +0x18 / +0x20, end +0x28 / +0x30; arc centre +0x18 / +0x20, r +0x28, start angle +0x30, sweep +0x38;
- +0x18 = Z.

**Layers written per pass** (FUN_18029d3e0 mm l.456047-456074):
1. For each bound in S+0xa0 (count S+0xa8):
   - Z = S+0x68 × 2 + in38 when S+0x70 = 0;
   - Z = in40 + bound Z otherwise;
   - the bound is written when Z > in38 + S+0x68 (l.456057-456063).
2. Then the machining region S+0x90 is written at in38, always last.

The first write uses "wb", the others append. What S+0x90 / S+0xa0 hold is set by CHST2DBase vtable slots 2-6 (PackageSomething mm l.470180-470189). The bound sets come from ComputeOffsetsAtDepth / FUN_1801ac7e0 offsets in PackageSomething. Their exact contents are **NOT DECODED**: the stock to leave (0x2e0) is applied there, not in the macro, because ToolThicknessXY = 0.

**FUN_1802a1670** (mm l.458963-460320) processes the layer file as follows:
1. **Read** each layer (l.459357-459393).
2. **Skip** layers below the floor when param_14 is set (l.459373-459386).
3. **Arcs become chords.** Half-angle h = acos((r − tol) / r), with r = \|start − centre\| and tol = param_8. Then n = ceil(sweep / 2h) when 2h < sweep, else 1 (l.459488-459496).
4. **Sweep and direction** come from FUN_18029caf0 (mm l.455621): normalise both angles to [0, 2π]; ccw: end += 2π if end < start, dir +1; cw: end −= 2π if start < end, dir −1. A full circle is 2π, +1.
5. **Clean the polygon.** Points closer than tol to the previous point are dropped (l.459560-459580), and so is a closing point equal to the first (l.459590-459613).
6. **Bounding boxes** per layer and overall: FUN_1802a9bb0 (mm l.464457), read by FUN_1802a9b70 (mm l.464434).
7. **Last layer in mode 1** (l.459615-459650): an outer frame polygon is built by FUN_1801b28c0 (mm l.301692). Its margin is 50 mm (1.9685 inch) + \|param_11\| (0) + 2 × max(box width, box height) (l.459205, 459637-459641). The frame is written as a surface. **The exact frame shape is NOT DECODED.**
8. **Stock offsets.** With param_9 = param_10 = 0, the stock-offset helpers FUN_1801ae0e0 / FUN_1801afbb0 are never reached on this path (l.459617-459631, 459651-459680).
9. **Write the model.** Each layer polygon goes to "tmpCoreMill<s>.raw" through FUN_1801b4b80 (mm l.303151-303420):
   - "Surface %d" (l.303353);
   - the polygon is made ccw;
   - the top face is cut into triangles by `TessellatePolygon` (l.303314);
   - every edge becomes two wall triangles;
   - x, y are scaled by 25.4 for inch (l.303216).

   **The Z values of these triangles are XMM varargs that Ghidra dropped: NOT DECODED** (needs dis of FUN_1801b4b80).
10. **Out-parameters** (l.459687-459694): param_17 = the last (floor) layer Z; param_16 = the lowest island layer Z above the floor.

## 5. Mode-0 expansion (CoreRoughingExpansionX / Y)

FUN_1802a9c60 (mm l.464506) runs only for mode 0 (l.460083). It sets dat+0x70 / +0x78 to the maximum over layers of FUN_1802a9e80 / FUN_1802a9d80 ratios. Those two helpers are **NOT DECODED**.

## 6. Trochoidal post-step (FUN_1802a1670 mm l.460139-460275)

It runs when all of these hold:
- the kernel output exists;
- param_5 (0x17b8) = 0;
- record **0x15ce** ≠ 0.

What it does:
1. `MutlConvertRawToEMMWithToolOffset` (l.460234) gets tool offset (2·min(p9, p10) + D) / 2 = D/2 and an angle field of 90.0 (l.460229-460232).
2. A "RawInfo" file stores the overall box.
3. `MutlZrestAddTrochoidalMovesMill` (l.460260) runs with:
   - initial loop = 0x1618 % × D if 5-100, else 0.3 D;
   - minimum loop = 0x1620 % × D if 1-100, else 0.03 D; the initial loop is raised to the minimum;
   - retry = 0x1628 if 5-95, else 50;
   - 0x1598 × (0x1630 + 100) / 100 when 0x1630 is 0-100;
   - 0x358 × 0x1638 / 100 when 0x1638 is 5-100, else 0.5 × 0x358;
   - 0.01 inch / 0.25 mm;
   - climb = 0x1564 == 0;
   - flag 0x15cf when the style byte is non-zero.

The internals of both Mutl functions (mccore) are **NOT DECODED**. All 4 user Area Mill ops have 0x15ce = 0, so the step does not run.

## 7. Depth handling (PackageSomething mm l.469868-470508)

The outer loop runs over the CHST2DDepthCuts levels (count `*(short*)(dc+2)`); each depth Z is FUN_1802ca590(dc, i) (l.470048). For each level:
- ComputeOffsetsAtDepth gives the XY offsets (l.470091);
- the bound lists are rebuilt;
- vtable[1] (FUN_18029d3e0) is called (l.470190, **dis 1802b25f6-1802b262d**) with in_stack_28 = 1, so the pass is not horizontal;
- in30 = xmm11, in38 = xmm13, in40 = process +0x88 (record 0x1778), in48 = the pass number.

When process +0x56 is set, island-top levels are also machined (l.470300-470440). There, vtable[1] is called with in_stack_28 = 0, so **mode 5 Horizontal** passes (dis 1802b2e52-1802b2e80), with in38 = xmm8 and in40 = xmm12.

What xmm11 / xmm13 / xmm8 / xmm12 hold (top / floor of each step) is **NOT DECODED**. It needs the register flow of PackageSomething. So RampHeightOffset's \|in30 − in38\| is also NOT DECODED.

## 8. Kernel output -> toolpath (FUN_1802abad0 mm l.466022)

1. **Input.** It reads "tmpCoreMill<s>.tmp" records of 0x88 bytes (l.466373). The motion type is at +0x84: 1, 3, 4 or 6, each with its point at a different offset. It tracks the lowest Z.
2. **Arcs.** They are kept when `TP::Utilities::IsHMMArcMCCompatible` (l.466554) accepts them. `GetHMMArcCreationStyle()` (l.466344) controls this:
   - 1 = no arcs;
   - 2 = arc radius limits from op 0x328 / 0x330;
   - otherwise 0.0001 / 9999.99 (l.466232-466233).
3. **Feed.** The plunge feed 0x380 replaces the passed feed when 0x15c9 = 0 and 0x380 > 0 (l.466339).
4. **Output.** It appends to the toolpath file (l.466370) through FUN_1802bd100. The NCI written by FUN_1802a66c0 is **NOT DECODED**.

## 9. Port (viewer/area_mill.c)

- `area_mill_macro()`: everything in sections 1-3, the NCG plan choice and every macro parameter with its value. `cam_cli PART areaparams ID` prints it. For the user's ops:
  - NAKTO op 3: mode 0 Core Roughing, MinOffset 1.485, MaxOffset 2.7, ProfileRampStyle 0, LinkClimbMilling 1, LinkMaxStayOnSurface 6, ProfileLinkRadius 0.371, CoreRoughingTrimOffset 2.75.
  - buckle-v10 / v11 / v12 op 2: mode 1 Area Clearance, same offsets.
- `area_mill_arc_segments()` and `area_mill_arc_sweep()`: the boundary tessellation in section 4.
- **Generator (2026-10-15):** Area Clearance (0x1567 = 0, mode 1) now uses the McHmm pass rule of section 11
  (`area_mill_passes`, motion in `cam2d_area.c`); Core Roughing (mode 0) is NOT DECODED and, since 2026-10-16
  (improvisation_review.md CAM item 7), is not generated: cam2d_dyn.c `gen_dynamic` returns the error "Area Mill Core
  Roughing (from outside) is NOT DECODED here; not generated" and a stored op stays as Mastercam made it. Oracle in
  section 11.6.

## 10. NOT DECODED (to finish Area Mill)

1. **McHmm.dll** is now in Ghidra (`~/mastercam-re/ghidra_hmm`, section 11). Decoded: the Area Clearance pass
   positions and pass curves, the profile-ramp parameters. Still NOT DECODED: see section 11.7.
2. The Z values of the triangle model (FUN_1801b4b80 varargs), the frame shape (FUN_1801b28c0), and the mode-0 expansion helpers FUN_1802a9e80 / FUN_1802a9d80.
3. The source of process +0x38 / +0x40 (LinkStartX / Y), and where process byte +0 is set.
4. The XMM step heights at the vtable[1] call sites (section 7), and the bound-list builders (CHST2DBase vtable slots 2-6, ComputeOffsetsAtDepth, FUN_1801ac7e0).
5. The kernel output record layout beyond the motion types, and FUN_1802a66c0. The two mccore Mutl trochoid functions.

## 11. McHmm pattern (McHmm.dll, 2026-10-15)

### 11.0 Sources and import

- `~/mastercam-re/files/mchmm.dll` (11 213 824 bytes) imported into the new Ghidra project `~/mastercam-re/ghidra_hmm` /
  `hmm` with auto-analysis: **Analysis succeeded (228 s), Save succeeded**, no OOM (GHIDRA_MAXMEM=4000M, scope
  MemoryMax=5G; log `~/mastercam-re/logs/hmm_import.log`).
- McHmm exports only the 12 `CNCGDCInterface` members (objdump -p). The rest was found through RTTI names + VtableDump,
  wide-string references (`tools/ghidra_scripts/StringRefsW.java`, StringRefs.java changed to UTF-16LE), immediate
  operands (`tools/ghidra_scripts/FindImm.java`; the session's copy was a variant of it) and structure displacements
  (`tools/ghidra_scripts/FindDisp.java`); address / range dumps with `tools/ghidra_scripts/DumpAt.java` /
  `DumpRange.java`.
- Decompile dumps in `decomp/` (cited **hmm-FILE l.N**):
  `hmm_params_reg.c` (FUN_1800ab4b0, the NCG parameter registry: `Ordinal_286(tmp, L"Name")` then `FUN_1800b5690(this, ID, tmp)`;
  `hmm_params.tsv` = the 750 ID -> name pairs extracted from it), `hmm_plans1.c`, `hmm_pass0.c`, `hmm_zprofile.c`
  (FUN_180407c10), `hmm_zplan.c` (0x1803f9000-0x180416000, the Z-profile evaluators), `hmm_vorlib.c` (0x180540000-
  0x180560000, the Voronoi library), `hmm_ac1.c`, `hmm_vor1.c`, `hmm_link.c` (0x180396000-0x1803c8000, linking plans),
  `hmm_linker.c` (0x180190000-0x1801c8000, the general linker), `hmm_lk1.c`, `hmm_ramp1.c`, `hmm_meas.c`; vtables
  `hmm_vt_plans.txt`, `hmm_vt2.txt`, `hmm_vt3.txt`.
- Parameter defaults are expressions set by FUN_18038b890(this, ID, type, flag, default, validation, ...).

### 11.1 Parameter IDs used here (hmm_params_reg.c line)

| ID | name | | ID | name |
|---|---|---|---|---|
| 0x1a92 | MinOffset (1466) | | 0x1aaa | MaxOffset (1499) |
| 0x1c47 | RadExtra (2498) | | 0x1bcc | RadAvoidBottleneck (2462) |
| 0x1bcf | ProfileSmoothing (2471) | | 0x1bd7 | ProfileSmoothingCornerOffsetTolerance (2135) |
| 0x1bd0 | ProfileSmoothingCornerTolerance (2114) | | 0x1bd2 | ProfileSmoothingMaxRadius (2120) |
| 0x1e5e / 0x1e5f | AntiVibration / AntiVibrationStepin (2864 / 2867) | | 0x1bca / 0x1bcb | RadPushUp / RadPushUpLimit |
| 0x1a58 | ProfileRampStyle (2201) | | 0x1a4b | MinProfileRampDiameter (2288) |
| 0x1a4e | MaxRampAngle (2297) | | 0x1a54 | RampHeightOffset (2189) |
| 0x1a76 | ProfileRampScanForward (884) | | 0x1a79 | ProfileRampDropPerStep (890) |
| 0x1abe | ToolShaftRadius (1544) | | 0x1a7c | ProfileLinkRadius (896) |
| 0x1c9e | LinkClimbMilling (2618) | | 0x1b1f | LinkMaxStayOnSurface (1841) |

### 11.2 Plans and evaluators

- `CAreaClearancePassesPlan` (vftable 1807a1010), `CCoreRoughingPassesPlan` (1807a1880), `CAreaClearanceLinkingPlan`
  (1807987b0), `CCoreRoughingLinkingPlan` (180798bb0) (hmm_vt_plans.txt). The plans only hold parameters and their
  defaults; slot [10] checks the inputs (hmm-plans1 l.1-33).
- `EvaAreaClearancePlan` and `EvaCoreRoughingPassesPlan` share the slots of `EvaZProfilePlan`: [2] FUN_18040cf60 (Z levels:
  Stepdown / AdaptiveStepdown, log "non-adaptive Stepdown") and [3] FUN_18040da70 (hmm_vt3.txt, hmm-plans1 l.75-437).
- Evaluate: Area Clearance / waterline FUN_1803fabf0 calls FUN_1803fae40 with pass kind **2 when Eva +0xff0 is set,
  else 0** (hmm-zplan l.880); FUN_180413b40 (OptiRough, NOT Core Roughing - correction 2026-10-23, formats/hst3d_port_plan.md; Core Roughing passes still NOT DECODED) calls it with **kind 1** after FUN_180414620 (hmm-zplan l.16503).
  FUN_1803fae40 -> FUN_1803faee0 ("DoPockets", per pocket slice FUN_18040b6f0 then FUN_180407c10), then FUN_1803ffdb0
  (ordering of the slices, NOT DECODED).
- FUN_180407c10 (hmm_zprofile.c): Voronoi diagram of the slice (FUN_1805475b0 / FUN_18054a440 with the Voronoi*
  tolerances, l.180-258), offset radius moved in by RadPushUp / RadPushUpLimit (FUN_18054a640, l.592-602), regions at that
  radius counted (FUN_18054b480, l.635), and per region FUN_180406b00 (l.654).
- FUN_180406b00 (hmm_pass0.c l.1717-2277): `bVar3 = kind - 2 < 3` (l.1811). Only for kinds 2-4 it reads MinOffset,
  RadExtra (l.1861-1870), and when ProfileSmoothing CornerOffsetTolerance (l.1879) and CornerTolerance (l.1898-1900);
  AntiVibration replaces the first step by AntiVibrationStepin (l.1925-1947). FUN_180555f60 places the passes (l.1948),
  FUN_180557f60 turns each into a curve with ProfileSmoothingMaxRadius (l.2030 / 2048). **For kind 1 (Core Roughing)
  MinOffset = RadExtra = 0, so this step only outputs the boundary loops: the Core Roughing passes come from elsewhere
  (FUN_180414620 / FUN_1804109a0 loops): NOT DECODED.**
- RadExtra default = `MaxOffset - MinOffset` (FUN_180312970, hmm-pass0 l.1338-1344).
- Voronoi tuning defaults (FUN_180310790, hmm-pass0 l.679-1278): RadAvoidBottleneck 0.00022, RadRadialPrecision 0.0002,
  VoronoiPolyEpsilon 2e-5, RadPushUp 0.002, RadPushUpLimit 0.02. McHmm works in mm; the port uses RadAvoidBottleneck
  = 0.00022 mm x unit (unit = part units per mm, 1/25.4 for inch parts; area_mill.c area_mill_passes).

### 11.3 Area Clearance pass positions (DECODED) — `voronoi\xoipositionoffsets.cpp`

The tool region T = machining region shrunk by R + stock. FUN_18054ec00 (hmm-vorlib l.10416) copies the region's Voronoi
graph with every radius shifted by −R, so a **position t** is a distance from T's boundary, and its pass is the offset of
T by t (FUN_180557f60, hmm-vorlib l.16248-16923: the loop is built from FUN_180556de0 offset spans of the boundary sites at
the record radius, trimmed by FUN_180557c80 / FUN_180557d20).

Inputs (FUN_180406b00 -> FUN_180555f60 hmm-vorlib l.15123): S1 = first step (MinOffset, or AntiVibrationStepin), S =
MinOffset, E = RadExtra, τ = CornerOffsetTolerance when ProfileSmoothing else 0, m = min((hi − lo) / 3,
RadAvoidBottleneck) (hmm-vorlib l.14994). The port computes m exactly so per chain (area_mill.c am_chain:
`m = fmin((hi - lo) / 3, RadAvoidBottleneck)`, lo / hi = the grid component's lower threshold and its largest
clearance); the earlier review note "fixed 0.00022 in area_mill.c" is out of date.

1. MinOffset = RadExtra = 0: only the boundary loops (t = 0) are output (hmm-vorlib l.15164-15182).
2. Otherwise each boundary loop is output at t = 0 (FUN_180556530, l.15219), then FUN_180555960 (l.14866-15123) runs along
   the chain of Voronoi edges from the loop (radius lo) to its end node (radius hi: a maximum, or a node where loops merge or
   split):
   - start r = S1 for a boundary loop (l.14934), else the value stored at the start node;
   - r = max(r, lo + m); while r < hi − m (l.15004-15011):
     - for every Voronoi edge of the chain crossing radius r (FUN_180554c70 l.14394, FUN_180554a20 l.14276), take the point
       P at clearance r (FUN_180555130, xoiEdge evaluation), walk along the edges towards higher radius while the next vertex
       is closer to P than E and is not a node of type 2 / 3 (FUN_180555800 l.14828-14866), and bisect the radius where the
       distance to P is exactly E (FUN_180555490, tolerance RadRadialPrecision). A chain that reaches a node first gives no
       limit;
     - g = the smallest of these radii, starting from hi − m (l.15016-15030);
     - **pass at t = max(lo + m, g − τ)** (l.15034-15051);
     - **next r = t + S** (l.15070); from the second pass on E and τ are the regular values (l.15090).
   - At the end r = max(r, end-node radius) is stored at the node (l.15095-15110).
3. Nodes with two incoming chains (loops merging, e.g. an island loop meeting the outer one; node +0x60 ≠ 0) wait for both
   and continue with the smaller stored radius; split nodes continue both outgoing chains from the stored radius
   (l.15250-15260, l.14951-14960).
4. In words: each next pass sits where the medial axis, followed for RadExtra from the point MinOffset beyond the previous
   pass, has climbed to; the step is MinOffset + the clearance gained over that walk. Where the axis rises 1:1 (round
   islands, open sides) the step is MaxOffset; along a corner bisector of angle α it is MinOffset + RadExtra·sin(α/2).

Check on buckle-v10 op 2 (Area Clearance, D 6, stock 1, MinOffset 1.485, RadExtra 1.215; clearance from the chains on a
0.03 mm grid, `tools/tests/area_mat.py` / `tools/tests/area_walk.py`): outer pass clearance 4.000; predicted 5.485 -> 5.99-6.03, **Mastercam 6.0154**;
predicted 7.5004 -> 7.9985, **Mastercam ≈ 8.00**; next 9.485 > max clearance 8.28: no more. Mastercam has exactly these
three loops.

### 11.4 Profile ramp entry (partly DECODED)

- Linker parameters (FUN_18039d850 hmm-link l.5070-5191): +0x11ad = (ProfileRampStyle == 0), +0x11b4 =
  MinProfileRampDiameter, +0x11a4 = RampHeightOffset, +0x11b8 = ProfileRampDropPerStep, +0x11c0 = ProfileRampScanForward.
- Defaults (FUN_180037920, hmm-lk1 l.1-258): ProfileRampScanForward = 1.1 × ToolShaftRadius (l.122),
  ProfileRampDropPerStep = ScanForward × tan(MaxRampAngle) (l.128-134).
- FUN_1801a9740 (hmm-linker l.16624-16979): profile ramp (l.16783) -> FUN_1801af630 picks the fragment (l.16795):
  the target fragment, or when its **MeasureCurveDiameter** < MinProfileRampDiameter (hmm-ramp1 l.501) another candidate
  that reaches it (candidate order: NOT DECODED). MeasureCurveDiameter (FUN_18004cf60 / FUN_18004cb90, hmm-meas l.1-206,
  609-800) = the largest distance between antipodal points = the curve's diameter.
- **n = int((z_start − z_frag) / DropPerStep + 1)**, at least 1 (hmm-linker l.16904); FUN_180174910 (hmm-ramp1 l.1-325)
  places n positions ScanForward apart along the fragment before its start (FUN_18027a130 steps the fragment iterator).
- Check (buckle-v10 op 2): ramp on the second loop (the triangle's diameter ≈ 1.3 < 6); horizontal step 3.3 = 1.1 × 3;
  the drop per step 0.17037 = 4.6 / 27 with n = int(4.6 / (3.3 tan 3°) + 1) = 27, so the ramp starts 4.6 above the level
  (RampHeightOffset = |in30 − in38| + record 0x15d8, 0x15d8 = 0: in30 is NOT DECODED, measured: level + 4.6 = top + 0.1).
- NOT DECODED: how z is spread along the path (the reference has flat stretches at three places per lap), the plunge
  height (0.0379), the end point.

### 11.5 Port (viewer/area_mill.c, viewer/cam2d_area.c)

- `area_mill_passes` (area_mill.c): rule 11.3 on a clearance grid (spacing max(0.01 mm x unit, √(box area / 2e6)), the
  floor is OURS and scaled for inch parts since CAM small items part 1 (b)): exact distance
  to T's boundary per cell, medial-axis cells = cells whose neighbour's nearest boundary point is > 6 cells away, the E walk
  = breadth-first search along axis cells (upwards) to the first cells E away (our approximation of McHmm's exact
  Voronoi; buckle: t = 2.009 / 3.99 against 2.0154 / 4.00). Components are followed with flood fills: a split continues
  every part from r. **Islands: McHmm keeps separate radii per loop until loops merge (rule 3); the port treats a
  component (outer + islands) as one chain — NOT PORTED.** Passes = cl_offset of T by t (standard offset semantics).
- `area_mill_curve_diameter` = MeasureCurveDiameter.
- `gen_area_hmm` (cam2d_area.c), used only for mode 1 (Area Clearance):
  - region = closed chains − avoidance chains; T = region offset by −(R + stock);
  - order **inner passes first** and loops **CCW for climb** (reversed when LinkClimbMilling = 0): MEASURED (buckle NCI),
    McHmm's ordering (FUN_1803ffdb0 / FragOrderer) NOT DECODED;
  - entry: profile ramp on the first pass whose diameter ≥ MinProfileRampDiameter, n and drop per 11.4, from 0.1 above the
    previous level (measured), z linear along the path, ending at the ramp loop's point nearest the first pass start;
  - links between passes (improvisation_review.md CAM item 8, SAFE neutral link, McHmm's EvaGeneralLinkerPlan /
    CLinkMovementConnect NOT DECODED): a straight feed at the level only when `seg_inside` finds the segment inside the
    tool-centre region T grown by 0.002 (sampled every 0.05); otherwise up to the feed plane, across at rapid, and down
    at the plunge feed;
  - depth cuts (dynamic.md "Depth cuts"): the passes are computed once at the first level of `hst_depths`; every
    further level is the same motion shifted down by d (d += rough step for the first nr - 1 copies, += record 0x880
    after them, as FUN_1802b11f0 / FUN_1802bb260); between levels: rapid above the ramp start at the clearance, feed
    plane, then the copy; the final retract goes to the clearance;
  - tolerances: chord tol = max(op tol, 0.002 D) x 0.25 for the offsets; MinOffset / MaxOffset fall back to the
    FUN_18006b750 replacement rule (Max = 0.45 D, Min = 0.55 Max) when the record gives none; ramp angle 3 deg when
    MaxRampAngle is 0; the ramp start 0.1 above the top is a measured constant in part units (buckle is metric);
  - Core Roughing (mode 0) and the other modes: `gen_dynamic` refuses them (NOT DECODED, not generated).
- Dialog edits (batch 9, 2026-10-23): an op with record patches in the HST block gets its am_* re-derived at generation from the
  record with the patches (cam2d_dyn.c `hst_record`, cam_fileop.c `cam_area_macro_params`): 379 cutting method 0x1564, XY stepover
  % / Min / Max (0x1580 / 0x1590 / 0x1598, the page's handlers), corner rounding 0x1566 + offset tolerance 0x15b0, 2826 1007
  0x15f0. formats/opdlg_port_audit.md 13.
- Dispatch: in `cam_generate` (cam2d.c) `case CAM_AREA: ok = op->p.am_ok && op->p.am_mode == 1 ? gen_area_hmm(f, op,
  &b) : gen_dynamic(f, op, &b);`. CamParams `am_*` fields (cam2d.h) are filled by cam_fileop.c `params_from_record`
  from `area_mill_macro` (macro values / scale = part units; system tol 5e-5).

### 11.6 Oracle and regressions (2026-10-15)

`ORACLE_FILEOP=1 tools/oracle.py <the 4 parts> --type area` (p95 of the worse direction, mm):

| op | before | after |
|---|---|---|
| buckle-v10 op 2 | 2.616 | 0.928 |
| buckle-v11 op 2 | 2.669 | 0.914 |
| buckle-v12 op 2 | 2.669 | 0.914 |
| NAKTO op 3 (Core Roughing, unchanged generator) | 8.677 | 8.677 |
| **median / worst** | **2.669 / 8.677** | **0.928 / 8.677** |

Since 2026-10-16 NAKTO op 3 is not regenerated at all (Core Roughing NOT DECODED, improvisation_review.md CAM item 7;
the fileop base holds a FAIL entry for it). The inch Area Clearance ops 3 / 7 of the shared SST Samples Mill.mcam are
oracle references too (CAM item 7: p95 0.389 / 0.245 in). The remaining buckle deviation is the ramp's Z phase (11.4
NOT DECODED part) and the links. Fileop regression
(`regress/new_hmm` vs the refreshed `base`): 186/189 identical, the 3 differences are the buckle Area Clearance ops.
Post regression (16 parts, DATE lines removed): 0/16 differ.

### 11.7 NOT DECODED

1. Core Roughing passes (kind 1: FUN_180414620, FUN_1804109a0, FUN_180411bd0 horizontal levels): the NAKTO rings are about
   2.65-2.7 apart with a 3.25 last gap. **2026-10-25 (formats/hst3d_port_plan.md 9):** EvaCoreRoughingPassesPlan runs the same kind-2
   Area Clearance evaluator (its ctor FUN_1800c7b80 sets Eva +0xff0 = 1); kind 1 is OptiRough's. What differs is the region (open
   towards the outside) and the trim - the HST 3D port keeps its passes inside the constraint boundary grown by FUN_180071120's offset.
2. The general linker (EvaGeneralLinkerPlan FUN_1803a0c50, CLinkMovement*, FUN_1803ffdb0 weave / FragOrderer): pass order,
   start points, stay-down links with ProfileLinkRadius, the ramp's Z spreading and seam flats, retracts.
3. Islands (per-loop chains, rule 3) in the port; profile smoothing (FUN_180557f60 with MaxRadius) in the port.
4. AntiVibration, Horizontal Area (kind 4 / FUN_18040fb90), the material-model trims.
