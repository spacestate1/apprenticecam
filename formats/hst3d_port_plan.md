# 3D toolpaths (HST / "Mastercam HMM") — decode plan and first decode (OptiRough)

Status 2026-10-24 (3drough): Dynamic OptiRough is ported end to end for new ops (section 6). Status 2026-10-23: plan + first decode only.
Builds on `formats/hst3d.md`, `formats/hst_plans.md`, `formats/area_mill.md` section 11 (McHmm Area Clearance,
the only McHmm algorithm ported so far) and `formats/dynamic.md` / `viewer/mcce_dyn.c` (the mcce engine).

Citation keys used below:
- **mk l.N**: `decomp/mckernel_all.c`;
- **hmm-FILE l.N**: `decomp/hmm_FILE.c`;
- **hst3d-FILE l.N**: the new dumps `decomp/hst3d_FILE.c`;
- **msurf l.N**: `decomp/mcmsurf_all.c`.

The new dumps come from Ghidra `~/mastercam-re/ghidra_hmm`, made read-only with DumpRange (section 4.0).

Scratch files and scripts are in `~/mastercam-re/work/hst3d/`:
- `scan.tsv` (op scan of every part);
- `eva_vt.txt` / `all_vt.txt` (RTTI vtables);
- `class_addr_map.txt` (class -> code address);
- `scripts/rtti_vt.py`, `cg_size.py`, `scan_ops.py`, `wstr.py`.

## 0. Summary

- Mastercam 2017 has **11 HST 3D toolpaths** (mcmsurf `msurf_hmm_*` commands):
  - Area Roughing ("Core Rough"), Dynamic OptiRough;
  - Waterline, Scallop, Horizontal Area, Raster, Pencil, Hybrid, Spiral, Radial, Project.

  Blend, Equal Scallop and Pocket-HST do not exist in 2017. "Blend" is only the classic Surface Finish Blend op 109.
  Mastercam also has the classic surface rough / finish ops (op codes 5-15, 39-44, 107, 109, 131), computed in mccore.
- Every HST op is one TPHSTToolPath job in mckernel. It writes a macro that McHmm (mchmm.dll) runs. McHmm evaluates a
  graph of **plans** (classes `C*Plan` + evaluators `Eva*Plan`).
- The geometry is computed by a few shared kernels in McHmm, all working on **triangles** (the tessellated model):
  - **Weave** (Z-slices: X/Y fibres + push-cutter): used by Waterline / Area Clearance / Core Roughing / OptiRough /
    Horizontal Area.
  - **Drop-cutter / ToolBox**: Raster / Radial / Spiral / Project / Scallop.
  - **Pencil net**.
  - **Voronoi offsets** (already partly ported for 2D Area Mill).
  - **General linker**.
- **Oracle problem.** None of the user's or sample parts holds a stored HST op. 774 part files were scanned (section 1.3).
  The only 3D op on disk is one classic Surface Finish Parallel on a flat face (BRACKET3 op 7).
  So no 3D step can be validated against Mastercam today. A decision is needed (section 5).
- **First decode (section 4): OptiRough.** Decoded so far:
  - Z levels, including the adaptive and "even last steps" rules;
  - flat-level (step-up) detection;
  - the slice at a Z level (weave resolution formula, fibre layout, tool radius-at-height, per-triangle push onto
    fibres, the push-cutter shove setup);
  - the region boundary output;
  - the hand-off to the mcce Dynamic engine, which is already ported.

  Still open:
  - the push-cutter interval maths per tool band (FUN_180125b70 / 180125dd0 / 180126340);
  - the weave -> loop traversal;
  - the general linker.

## 1. Inventory

### 1.1 HST toolpaths (op 132 "Surface High Speed", style byte = op struct +0x1654 = record +0x16bc)

How each column was found:
- **UI style**: set by the command handlers (msurf l.211706-211900). The type code is from FUN at msurf l.376405.
- **Kernel style**: the byte `TP+0x45c` that mckernel's Machining Passes macro builder switches on
  (FUN_18007d7c8, mk l.92564-94100; branches at mk l.93280-93957). The parameter setter
  FUN_180071314 (mk l.86968; branches at mk l.87623-88358) switches on it too.
- **UI -> kernel mapping**: the copy happens in mccore and was **NOT DECODED**. The columns are paired by plan name: the
  kernel branch that builds the "Waterline Passes Plan" is taken to be Waterline. Confirm this in mccore.

| toolpath | UI style | type code | kernel style | passes plan (type id) | mchmm evaluator (vftable, Evaluate slot [1]) | dialog pages (ui_fields_mcmsurf.tsv) |
|---|---|---|---|---|---|---|
| Area Roughing (Core Rough) | 0 | 0x19 | 0 | Core Roughing 0x1460 / Area Clearance 0x1435 (+ Horizontal Area 0x143a when +0x475 = 2; Axial Offset 0x143b) | EvaCoreRoughingPassesPlan 1807a18e8 / EvaAreaClearancePlan 1807a1078, both [1] 18040eaf0 | 1326, 25624, 1332 |
| Dynamic OptiRough | 1 | 0x26 | 9 | Optimized Roughing Z Profile 0x1478 (Pocket / Core) | EvaOptRoughProfilePassesPlan 1807a1910, [1] **180413b40** | 1465, 1430 |
| Waterline | 2 | 0x1b | 1 | Waterline 0x1436 | EvaWaterlinePassesPlan 1807a08f0, [1] 18040eaf0 | 1324, 1333 |
| Scallop | 3 | 0x1c | 2 | Constant Stepover from 2D Boundaries 0x140c / from 3D Boundaries 0x1445 | EvaConstantStepoverPassesFrom2D/3DBoundariesPlan 18078c058 / 18078c4e8, [1] 180339890 / 18033b3f0 | 1328 |
| Horizontal Area | 4 | 0x1d | 3 | Horizontal Area 0x143a (Axial Offset 0x143b when NumberOfOffsets > 0) | EvaHorizontalAreaClearancePlan 1807a1160, [1] 18040fb90 | 1327 |
| Raster | 5 | 0x1e | 4 | Raster 0x1404 (+ "Raster Passes Plan 2" perpendicular) | EvaRasterPassesPlan 1807908f0, [1] 18035ec70 (EvaGridPassesPlan) | 1330 |
| Pencil | 6 | 0x1f | 5 | Pencil 0x140f (+ Constant Stepover From Passes 0x140d for offsets) | EvaPencilPassesPlan 18079a960, [1] 1803ca8f0 | 1329 |
| Hybrid | 7 | 0x27 | (separate job) | TPHybridWSToolPath::Compute -> FUN_18008584c / FUN_1800882e0 (mk) | NOT surveyed | 25623, 25628 |
| Spiral | 9 | 0x22 | 7 | Spiral 0x1418 | EvaSpiralPassesPlan 180790d00, [1] 18035ec70, [2] 18037dc90 | 1338 |
| Radial | 10 | 0x25 | 8 | Radial 0x1417 | EvaRadialPassesPlan 1807910f0, [1] 18035ec70 | 1339 |
| Project | 15 | 0x23 | 10 | Project Point 0x147a / Boundary (Project Curve / NCI) 0x1412, via vtable +0x18 (mk l.93957) | EvaPointPassesPlan 180760338 / EvaBoundaryPassesPlan 180791898 | 1517 |

Notes on the table:
- **Kernel style 6** has no passes-plan branch in FUN_18007d7c8: it is **Hybrid** (a separate job). The UI -> kernel style map is decoded
  (mccore FUN_1809239b0: 0->0, 1->9, 2->1, 3->2, 4->3, 5->4, 6->5, 7->6, 9->7, 10->8, 15->10) and the record -> kernel offset map is a plain
  field copy (mccore FUN_180923f60: kernel K = m - 0x60, record = op + 0x68); formats/hst_project_hybrid.md 0.1 / 0.2.
- **Pages shared by all HST ops**:
  - 1325 (Steep / Shallow), 1340 (Rest material), 25627 (Toolpath fillet), 1353 (Arc filter / tolerance);
  - 1333 (Finish transitions), 1332 (Rough transitions).
- **Finishing chain.** After the passes plan, finishing ops add, by option:
  - Edit Passes to Surfaces 0x141f (check surfaces / rest material);
  - Edit Passes to Toolholder 0x1459;
  - Convert To Primary Output 0x1458;
  - a Plan of Plans 0x1437.
- **Roughing chain.** Area Roughing and Horizontal Area add a 2D Stock Model 0x145b (mk l.93387-93566).
- **Linking.** A separate macro, FUN_18007d028 (mk l.92328), adds the linking plan of each style:
  Core Roughing 0x1461, Area Clearance 0x141d, Horizontal 0x144c, Raster 0x142b, Waterline / Spiral / Radial / Pencil /
  Constant Surface Stepover / Project linkers.
- **Plan type ids found here, missing from hst_plans.md**:
  - 0x1404 Raster Passes;
  - 0x140d Constant Stepover Passes From Passes;
  - 0x1427 Drive Boundaries;
  - 0x1478 Optimized Roughing Z Profile Passes;
  - 0x145b 2D Stock Model.
- **Correction to formats/area_mill.md 11.2 / 11.7.** FUN_180413b40 is slot [1] of **EvaOptRoughProfilePassesPlan**
  (RTTI vftable 1807a1910, `~/mastercam-re/work/hst3d/eva_vt.txt`). It is not Core Roughing's. Core Roughing,
  Area Clearance and Waterline share slot [1] = FUN_18040eaf0 -> FUN_1803fabf0. That function passes kind = 2 when
  Eva+0xff0 is set and 0 otherwise (hmm-zplan l.843). So "pass kind 1" = OptiRough, and Core Roughing's passes are
  still NOT DECODED. area_mill.md was not edited (docs-only task, new files only).

### 1.2 Classic surface toolpaths

These are the Surface Rough and Surface Finish ops:
- Rough Parallel 5, Radial 6, Project 7, Flowline 8, Contour 9, Pocket 10, Plunge 44, Restmill 107;
- Finish Parallel 11, Radial 12, Project 13, Flowline 14, Contour 15, Pencil 39, Leftover 40, Parallel Steep 41,
  Shallow 42, Scallop 43, Blend 109;
- Multisurface Rough Pocket 131.

Where they are computed:
- mcmsurf.dll only holds their dialogs (pages 99x-131x in ui_fields_mcmsurf.tsv). It imports nothing from mckernel /
  mcce except `TP::Utilities::sChekTcbStretchEmm` and `CDynPockParm::GetMinRadK` (objdump -p).
- The motion is computed in **mccore** (`bnci_surface`, assert "Assocsrf.cpp" msurf l.12235).
- mccore imports `TPZCut` from mckernel (objdump -p mccore.dll).
- The classic engine was **not surveyed**. It is a separate project, and it does not share the McHmm kernels.

### 1.3 Stored 3D ops (oracle data)

How the scan was run:
- `~/mastercam-re/work/hst3d/scripts/scan_ops.py` (tools/mcam_ops.load) went over every .mcam / .mcx-9 / .MCX file
  on `<data-disk>`, `<windows>` and `~/mastercam-re`. Fonts, tool and template files were left out: 105 parts.
- Ops found: 211 Contour, 73 2D HST, 16 drill, ... and **one 3D op**: BRACKET3.mcx-9 op 7 "Surface Finish Parallel"
  (flat 3 mm endmill, 48 moves, a zig-zag on a flat face at Z -0.5).
- **No HST op (132) exists anywhere.**
- The Mastercam X example parts (`<windows>/mcamx/documentation/ExampleParts/*.MCX`) are not read by
  tools/mcam_walk.py: the X1 header differs (0x88 vs 0x98) and only 6 records come back. By their strings they hold
  5-axis Multi Surface ops (ELECTRODE, IMPELLER, CAMSHAFT ...), not 3-axis HST.

### 1.3b Internet corpus rescan (3drough, 2026-10-24)

`~/mastercam-re/work/hst3d/scripts/scan_ops.py` over `<mastercam-samples>/net` (1460 part files,
19 unreadable), then `scan3d.py` (style byte = record 0x16bc, entity classes, XT blobs) on the 47 files with 3D ops.
Scratch: `~/mastercam-re/work/hst3d/netscan/scan.tsv`, `scan3d.tsv`. The same parts appear in X9 / 2017 / 2018 copies;
the 2018 copies carry no NCI (0 moves).

| part (emastercam-training, 2017 / X9 copies) | op | style | NCI moves | model | usable as oracle |
|---|---|---|---|---|---|
| Multiaxis Essentials TUTORIAL 7 METRIC | 5 | **1 Dynamic OptiRough** | 30008 | 1 solid (XT: B_SURFACE, TRIMMED_CURVE, SP_CURVE, ELLIPSE) | **yes** once xt_occt maps B-surfaces (section 6.4) |
| Multiaxis Essentials TUTORIAL 7 (inch) | 5 | 1 Dynamic OptiRough | 27137 | same solid | same |
| TUTORIAL 7 (both) | 6 | 7 Hybrid | 24328 / 17346 | same | no (Hybrid) |
| Multiaxis Advanced TUTORIAL 2 SWARF (+ _METRIC) | 1 | 0 Area Roughing | 11617 / 38629 | Mastercam surfaces (no XT) | needs the surface entities (not read here) |
| TUTORIAL 2 SWARF | 3 / 4 / 5 | 2 Waterline / 3 Scallop | 2586-17837 | surfaces | later steps |
| Multiaxis Professional MOLD BOTTLE (X9) | 1 / 4 / 6 | 0 / 2 / 3 | 12061 / 4812 / 16959 | surfaces | Area Roughing, surfaces |
| Handbook 3 MULTIAXIS ROUGHING FALSE FLOOR | 6 | 3 | 7317 | 2 solids (SWEPT_SURF, B_SURFACE) | no (style 3) |
| MCX Files WATERLINE / PARALLELSTEEP / STEEPSHALLOW / RESTPASSES / PENCILSEAMS | 2 / 5 / 5 / 18-19 / 1 | 0 / 5 / 5 / 5 / 6 | 1581-57770 | surfaces (classes 0x80 / 0x100) | finishing ops |
| classic Surface Rough Pocket (op 10) | CURVE 5 AXIS REVIEW op 1, CONTOURFINISH(1), FINISHSCALLOP, LEFTOVER, PROJECTBLEND, PARALLELSTEEP, STEEPSHALLOW, RESTPASSES | - | 292-4174 | surfaces | classic engine (mccore), not surveyed |

So **one** stored OptiRough op with NCI and a solid exists (TUTORIAL 7 op 5, metric and inch). Its NCI cuts **a single Z
level** (-16.975 mm metric, 24280 moves at that Z; the other Z values are helix entries / micro lifts), tool 12 mm flat,
record: stepdown 0x1228 = 18, stepover 0x1230 = 2.4, Stepup off (0x16ec = 0), Pocket (0x16ef = 0), climb (0x16e4 = 0),
helix radius 0x1328 = 10, Z clearance 0x1330 = 3, plunge angle 0x1338 = 2 deg, gap 0x1308 = 60 (% 0x1310 = 500),
micro lift 0x1488 = 0.25, back feed 0x1490 = 2500, min toolpath radius 0x1480 = 2, skip pockets 0x13e8 = 22.
Decision 3D-3 therefore stays on OptiRough (the corpus has no classic-rough op on a solid; Area Roughing only on
Mastercam surfaces, which this program does not read).

## 2. Where each algorithm lives (mchmm.dll)

Sizes are the sums of the .pdata function extents over the address range of the classes involved. Classes are placed
by their RTTI vftable slots (`class_addr_map.txt`). Ranges come from the class layout, so a range can include
neighbouring helpers: treat the sizes as estimates.

Whole .text: 71 439 functions, 7.1 MB.

The direct-call closure of a plan (`cg_size.py`) is not useful as a size. The evaluators reach the kernels through
vtables, and every plan's closure is 0.3-0.8 MB of shared infrastructure.

| kernel / module | address range | functions / KB | used by | state |
|---|---|---|---|---|
| Z-profile plans (Waterline, Area Clearance, Core Roughing, Horizontal Area, OptRough; CZInflexions*) | 1803f9000-180416000 | 224 / 112 | rough + waterline | dumped (hmm_zplan.c, hmm_zprofile.c). Z levels, flat levels, OptRough flow decoded (4.2-4.4) |
| Weave / fibres / push-cutter / drop / pipe-thin (CWeave, CWeaveFibresConcurrent, CToolDropPos, CPipeThinToolpath) | 180110000-18013a000 | 630 / 162 | all Z-slice ops; drop positions | dumped (hst3d_weave_drop.c). Weave setup + per-triangle push decoded (4.5); interval maths and loop traversal open |
| CFibre / CFibreConcurrent / CWeaveConcurrent | 18000f000-180014000 | 153 / 18 | weave | dumped (hst3d_fibre.c) |
| Weave tool profile + grid | 18013a000-180142000 | 82 / 39 | weave | dumped (hst3d_grid.c). r(h) decoded |
| BoxSet / ToolBox (BoxSetFuncs.cpp, ToolBoxSet, EvaToolBox*) | 1801e2000-180254000 | 1423 / 450 | slice feeding (decoded), drop-cutter for raster-type passes (EvaToolBoxLinearBase 180230b20), stock-model trims | 1801e2000-18022c000 dumped (hst3d_slice/boxset/boxset2.c); ToolBox part not dumped |
| Grid pass plans (Raster, Radial, Spiral, Linear, Morphed, Boundary) | 18035e000-180384000 | 307 / 146 | raster / radial / spiral / project | not dumped |
| Constant-stepover plans (Scallop) | 180333000-18033c000 | 66 / 36 | scallop, pencil offsets | not dumped (they also use the Voronoi lib) |
| Pencil plan + FullPencilNet / PencilQuad | 1803c8000-1803cc290, 18014f000-18016a000 | 49 / 16 + 317 / 104 | pencil | not dumped |
| Area detect (cutter contact, shallow, rest, theoretical rest) | 180316000-18031ea00 | 71 / 33 | steep / shallow limits, rest | not dumped |
| Trim passes to surface / holder | 18034e000-18035ce00 | 119 / 58 | finishing chains | not dumped |
| Linking plans (per style) | 180396000-1803c8000 | 339 / 197 | all | dumped (hmm_link.c); Area Clearance params decoded (area_mill.md 11.4) |
| General linker / FragOrderer / Weave links | 180190000-1801c8000 | 359 / 219 | all | dumped (hmm_linker.c); NOT DECODED |
| Link connect (CLinkMovementConnect) | 1801c8000-1801e2000 | 349 / 100 | all | partly dumped (hmm_linkconnect.c) |
| Voronoi library (xoi*) | 180540000-180560000 | 352 / 123 | Area Clearance offsets, smoothing, constant stepover | dumped (hmm_vorlib.c); positions decoded (area_mill.md 11.3) |
| Tessellation (plans + EvaTess* / CTess*) | 1803e4000-1803f3000, 180470000-1804f0000 | 148 / 61 + 1376 / 502 | surface -> triangles | not dumped. Not needed if we feed our own mesh (4.8) |
| CToolChopPos / CSChopL, CTrianglePlaneIntersect | 18016e000-180172000, 18003c000-18003e000 | 54 / 15, 31 / 8 | chop / slice helpers | dumped (hst3d_chop.c, hst3d_triplane.c) |

### Black boxes and how to treat them

- **Macro interpreter.** The yywc lexer and macro AST are in "Source\yywc*.cpp" (location in .text not established).
  It is only a parameter carrier: mckernel writes `CreatePlan(type, inputs, names, values)` and McHmm builds the plan
  objects from it. The port should call the plans directly with the same parameter values.
- **Default expressions.** McHmm parameter defaults are formula strings evaluated by that interpreter
  (`FUN_18038b890(this, ID, type, flag, L"expr", ...)`, hmm-pass0 l.679-1278). They read like source; see 4.5 for
  MachWeaveResolution. Each one has to be transcribed. Not a black box, just work.
- **tbb.** Two loops are parallel:
  - FUN_1801e2fd0 runs the tessellation areas in parallel when `vtable+8` says so (hmm-plans1 l.2973, the
    FUN_1801e5f20 branch);
  - CWeaveFibresConcurrent / CFibreConcurrent run the fibres in parallel.

  Each fibre's interval set is a union of per-triangle intervals, so the result should not depend on the order (this
  is an inference: the union is commutative). A single-threaded port is fine. Where threads could change the result
  (ordering, FragOrderer) is NOT DECODED.
- **Virtual dispatch.** The evaluators reach the kernels through vtables, so call-graph tools miss them. Decode by
  vtable slot (RTTI dumps in `eva_vt.txt` / `all_vt.txt`).
- **Surface tessellation.** It is replaced by our mesh (4.8). Its exact triangles cannot be reproduced without porting
  1803e4000 + the EvaTess* classes (about 560 KB).

## 3. Recommended order for a 3-axis router shop

Each step names its oracle. Every oracle depends on the decision in section 5: today there is no stored HST op. The
"self-checks" below are ours: analytic cases, not Mastercam data.

| # | step | why first | reuses | new decode | oracle | effort (agent sessions) |
|---|---|---|---|---|---|---|
| 1 | **OptiRough** (Dynamic OptiRough) | biggest gain for roughing 3D parts; the in-plane engine is already ported | mcce_dyn.c (CDynPockParm mapping FUN_180108f1c, hst3d.md), area_mill.c loop output | Z levels + step-up (done, 4.2-4.4); weave push-cutter interval maths + weave loops (open); OptRough executioner per region (mk FUN_180107864 chain); Imported Linking Plan | stored OptiRough NCI (needs 5.1); self-check: a box / cylinder / hemisphere boss whose Z-slices are analytic | 6-9 |
| 2 | **Waterline** | same weave + Z levels, kind 0 (profile only); finishing of steep walls | step 1 | ProfileSmoothing on the loop (Voronoi, FUN_180557f60 already read), Waterline Linking Plan, steep/shallow limits (Area detect) | stored Waterline NCI | 2-3 |
| 3 | **Raster** (parallel finish) | the standard router finish | step 1 tool model | drop-cutter (ToolBoxSet / EvaToolBoxLinearBase, CToolDropPos), grid passes 18035e000, Raster Linker 0x142b | stored Raster NCI; BRACKET3 op 7 only as a smoke test of the classic op (different engine) | 4-6 |
| 4 | **Area Roughing** (Core / Area Clearance in 3D) | the alternative roughing | step 1 weave + 2D Area Clearance port (area_mill.c) | Core Roughing passes (still open, area_mill.md 11.7), Horizontal Area | stored NCI; the 2D Area Mill oracle (buckle) covers the Voronoi part | 3-5 |
| 5 | **Horizontal Area** | flats after roughing | step 1 flat-level detection (FUN_180411bd0 decoded) | EvaHorizontalAreaClearancePlan FUN_18040fb90 (kind 4), Axial Offset | stored NCI | 2 |
| 6 | **Scallop** | constant-cusp finish | steps 3 + Voronoi | constant-stepover from 2D/3D boundaries (180333000, 36 KB + Voronoi) | stored NCI | 4-6 |
| 7 | **Pencil** | corner clean-up | step 3 drop-cutter | FullPencilNet / PencilQuad (120 KB), bitangency | stored NCI | 4-6 |
| 8 | Spiral / Radial / Project | niche | step 3 | grid variants, Project point/curve/NCI | stored NCI | 1-2 each |
| 9 | Hybrid | niche | steps 2 + 6 | TPHybridWSToolPath job (mk FUN_18008584c) | stored NCI | 3+ |

All steps need the general linker for "Mastercam-identical" links. Until it is decoded, the links should use the
neutral SAFE rule already used by 2D Area Clearance (improvisation_review.md CAM item 8), labelled ours.
Decoding the linker (EvaGeneralLinkerPlan FUN_1803a0c50, 219 + 100 KB) is a separate 5-8 session item. It also helps
the 2D Area Mill port.

## 4. First decode: Dynamic OptiRough

### 4.0 New dumps (this session)

`analyzeHeadless ~/mastercam-re/ghidra_hmm hmm -process mchmm.dll -noanalysis -readOnly -postScript DumpRange.java`,
GHIDRA_MAXMEM=2500M, run when no other Ghidra was running:

| file | range | functions |
|---|---|---|
| decomp/hst3d_weave_drop.c | 180110000-18013a000 | 392 |
| decomp/hst3d_fibre.c | 18000f000-180014000 | 143 |
| decomp/hst3d_grid.c | 18013a000-180142000 | 82 |
| decomp/hst3d_slice.c | 1801e2000-1801e8000 | 61 |
| decomp/hst3d_boxset.c | 1801e8000-180200000 | 111 |
| decomp/hst3d_boxset2.c | 180200000-18022c000 | 294 |
| decomp/hst3d_chop.c | 18016e000-180172000 | 36 |
| decomp/hst3d_triplane.c | 18003c000-18003e000 | 25 |

### 4.1 Chain

```
mcmsurf msurf_hmm_opti_core_toolbar: op+0x1654 = 1 (msurf l.211780)
mccore -> TPOptRoughToolPath::Compute (mk l.226794) -> job OptRoughToolPath (vtable 180155f00, hst3d.md)
  1. Machining Passes macro FUN_180109910 (mk l.205628): Get Surface Tessellation + Get Optimized Roughing Z Profile
     Passes (0x1478, built by FUN_18007d7c8 style 9 at mk l.93437-93453, "(Pocket)" or "(Core)" by TP+0x486),
     PosZLo / PosZHi / LinkConnectPairs = false
  2. 2D Stock Model macro FUN_18010921c (mk l.205485): Bounding Box + Waterline Passes
  3. per region: OptRoughDynPockExecutioner FUN_180107864 -> ... -> mcce::MCCECalcDynPock with CDynPockParm from
     FUN_180108f1c (mk l.205368; field list in formats/hst3d.md)
  4. Toolpath Linking Passes macro FUN_180109b58 (mk l.205690): Imported Passes + Imported Linking Plan 0x1475
     (PlanarPatchZHeight)
McHmm: EvaOptRoughProfilePassesPlan::Evaluate = FUN_180413b40 (hmm-zplan l.16465)
```

### 4.2 Inputs (parameters for kernel style 9, FUN_180071314 mk l.88358-88419)

The macro sets these parameters:
- **Stepdown** (local_840);
- **StepUp** (local_738) and **StepUpEnable** (TP+0x483);
- **MinOffset = MaxOffset = TP+0x1b8**;
- **PosZHi** / **PosZLo** (= local_870 + param_5);
- **ThinArcEnable** = 1, **ThinArcMaxRadius** = 9999.99;
- **ProfileSmoothing** = 0;
- **StepDownEvenLastSteps** = 1;
- **RadPushUp** = 0.0002, **RadPushUpLimit** = 0.002.

Before the style switch the same function sets the tool:
- ToolCornerRadius = TP+0x218, ToolCutterDiameter = TP+0x210;
- ToolFluteLength / ToolBodyLength;
- ToolTaperAngle = 0;
- ToolShankDiameter (mk l.87457-87493).

It also sets the stock: ToolThicknessXY / ToolThicknessZ (mk l.87500-87610) and StepMachTolerance (mk l.87612).

The model comes in as the tessellation of the selected surfaces / solids, through the plans Database Loader 0x1421 ->
Detect Top Surfaces 0x1423 -> Surface Tessellation.

### 4.3 Z levels (EvaZProfilePlan slots [2] / [3], shared by every Z-profile plan)

- **First level / adaptive** (FUN_18040cf60, hmm-zplan l.12890). Let z_prev = the current level (+0x2c8) and
  z_nom = slot[3](z_prev), the nominal next level.
  - **AdaptiveStepdown off** (0x1bb8), or the gap z_prev − z_nom ≤ StepDownInflexionTolerance + AdaptiveStepDownMin:
    the slice is built at z_nom, and the log says "non-adaptive Stepdown <gap> to <z>" (l.13010-13025).
  - **Otherwise:**
    - the slice at z_nom is compared with the current one (measure FUN_18012cc60 with
      AdaptiveStepDownBetweenDistanceOffset, l.13037-13043);
    - if they differ by more than **AdaptiveStepDownDiff**, z_prev − AdaptiveStepDownMin is tried (l.13045-13070);
    - then the interval is bisected until it is ≤ **AdaptiveStepDownPrecision** (loop l.13075-13140).

    This is an outline. The exact choice of the accepted level inside the loop has to be checked before porting.
  - The StepDownInflexion* parameters (0x1bbd-0x1bc0, 0x1df6) snap levels to model inflexion heights through
    CZInflexions (FUN_1804a1b80 / FUN_1804a1e10).
- **Next level** (FUN_18040da70, hmm-zplan l.13173). When no pending inflexion level is queued:
  - n = ceil((z − PosZLo) / Stepdown − StepDownInflexionTolerance), at least 1;
  - if n > **StepDownEvenLastSteps** (OptiRough: 1) and n ≠ 1: next z = z − Stepdown;
  - otherwise the remaining depth is split evenly: z' = (1 − 1/n)·z + (1/n)·PosZLo (l.13227-13238).

  So the last k = StepDownEvenLastSteps steps are made equal. For OptiRough (k = 1) the last step goes straight to
  PosZLo; it is the remainder, at most Stepdown × (1 + StepDownInflexionTolerance).
- OptiRough calls FUN_1803fb4f0 with adaptive = 0 (hmm-zplan l.16489). Its own level loop is FUN_18040e430
  (hmm-zplan l.13531), which also uses **AreaClearStockZStepdown** (0x1cc9) through FUN_18040dc80.
  The details of FUN_18040e430 are NOT DECODED.

### 4.4 Step-up on flats (StepUpEnable, FUN_180414620 hmm-zplan l.16821)

When StepUpEnable (0x1ec6) is set, FUN_180411bd0 (hmm-zplan l.15258) runs first:
1. It builds CZInflexionsHorizontal over the model, with a box = model box ± 10 and the Z range [PosZLo − c, PosZHi − c]
   where c = Eva+0x140.
2. This gives a list of horizontal bands (z_lo, z_hi) with an area each.
3. Neighbouring bands closer than **HorizontalZEpsilonMerge** (0x1c6e) are merged and their areas added.
4. Bands with area ≥ **HorizontalFlatAreaSize**² (0x1c69) are logged "HozLevel detected between ... with area ..." and
   kept as step-up levels.

For each step-up level:
- the slice is built with FUN_1804109a0(kind 1);
- the pockets with FUN_1803faee0(kind 1);
- a region that is non-empty records the level (FUN_180415170).

### 4.5 The slice at Z: cutter contact against the mesh ("weave")

All of this is in McHmm `machining\Weave.cpp` / `BoxSetFuncs.cpp`. The model is triangles only (4.8).

**Resolution.** Defaults from FUN_180310790 (hmm-pass0 l.723-799, expression strings resolved with `wstr.py`):
```
WeaveFlatTip        = (ToolCornerRadius + ToolThicknessZ <= 0.0)
VorProfileFlatRad   = (WeaveFlatTip ? 0.0 : max(0.0, ToolShaftRadius - ToolCornerRadius))
WeaveCutterRadius   = ToolShaftRadius + ToolThicknessZ - VorProfileFlatRad
MachWeaveResolution = 1.414 * sqrt(square(WeaveCutterRadius) - square(max(WeaveCutterRadius - StepMachTolerance, 0.0)))
```
In words: the fibre spacing is √2 × the half-chord of a circle of radius WeaveCutterRadius at sagitta
StepMachTolerance. For a flat endmill without Z stock this is the shaft radius. For a bull or ball tool it is the
corner radius + Z stock.

**Fibres** (CWeave ctor FUN_18012b990 hst3d-weave_drop l.18719; FUN_18012be70 l.18891; FUN_180131690 l.23479):
- Each CWeave holds two fibre sets: X fibres at +0x10 and Y fibres at +0x58.
- The region is the weave box, grown by (spacing + extra):
  - X range +0x198 / +0x1a0;
  - Y range +0x1a8 / +0x1b0.
- A set with range [lo, hi] gets n = max(3, int((hi − lo) / spacing) + 2) **+ 1** fibres (FUN_180131690 stores the max + 1 as the count, set +0x28; checked
  2026-11-14 on the DLL: 272 x fibres on MOLD BOTTLE op 4 where the formula without the + 1 gives 271 - hst_linker.md 28.3; ported for Waterline's weave, the
  other styles' weaves keep their own box and the old count) at evenly spaced positions, t = i / (n - 1), position (1 − t) lo + t hi:
  t_i = lo + i (hi − lo) / (n − 1).
- The weave Z is at +0x158; FUN_1801298a0 sets it (hmm-plans1 l.2871).
- **MachWeaveMaxFibres** (0x1e40) caps the count. Where it applies is NOT DECODED.

**Feeding the model** (FUN_1801e2fd0 hmm-plans1 l.2973):
1. For every tessellated area whose box meets the weave box, FUN_1801e5d50 (hst3d-slice l.2322) dispatches on the
   area type:
   - untrimmed, trimmed, raw and triangulated surfaces;
   - the stock model;
   - FUN_1801ea6f0 (hst3d-boxset l.1613) walks triangle lists / strips / fans, via FUN_1801fcfe0 l.13774.
2. Each triangle is transformed to the work plane when one is set (FUN_1802082a0 hst3d-boxset2 l.4958).
3. FUN_1801295b0 (hst3d-weave_drop l.17072) then handles the triangle:
   - cull it against the weave XY box (+0x168..+0x180);
   - h = z_max(triangle) − z_weave; skip the triangle if h ≤ 0 (all below the slice);
   - r = **tool radius at height h** (FUN_18013fff0, below);
   - keep the triangle if r ≤ the first field of the tool profile (weave+0x98; its meaning is NOT DECODED), or if its
    XY box comes within r of the region;
   - push it into the X fibres and the Y fibres (FUN_180130b40 twice, with the axes swapped).

**Tool radius at height h above the tip** (FUN_18013fff0 hst3d-grid l.4060, tool profile object at weave+0x98):
- r(h) = F + sqrt(R² − max(R − h, 0)²) for 0 < h below the first table entry. R = +8 (the corner / ball radius),
  F = +0x18 (the flat radius). This is the torus cross-section, so a flat endmill (R = 0) gives r = F.
- Above the first entry: linear interpolation in the (r, h) table at +0x40. That is the side profile: taper, shank and
  holder, the SSideProfileSegment classes.
- h ≤ 0 gives F.

**Push onto the fibres** (FUN_180130b40 hst3d-weave_drop l.22900). For every fibre at cross-coordinate c within
[x_min − r − 0.003, x_max + r + 0.003]:
1. d = the distance from c to the triangle's cross range. Skip the fibre if r² − d² < 0.
2. Skip the fibre if it is already covered there (interval lookup, vtable +0x10).
3. Otherwise move the triangle into fibre coordinates: along-fibre axis first, then cross offset − c, then z − z_weave.
4. FUN_180128800 (l.16375) splits the tool into its profile bands up to the triangle's z_max. For each band it pushes:
   - the three vertices (FUN_180125dd0);
   - the three edges (FUN_180126340);
   - the facet.
5. The resulting interval [lo, hi] along the fibre is the tool-centre positions where the tool at z_weave collides with
   the triangle (FUN_180125b70 l.15072).
6. The interval is added to the fibre (CFibre vtable +0x18 = FUN_180010ac0 -> FUN_18005fd30, hst3d-fibre l.1375):
   interval list, 0x40-byte records.

This is the classic push-cutter: vertex, edge and facet tests per tool band. The interval formulas inside
FUN_180125dd0 / FUN_180126340 / FUN_180125b70 are NOT DECODED yet; they are the next item.

**Weave -> loops.** FUN_18012d020 (l.19677):
- it closes the fibres at the region border (FUN_18012cde0 on the first and last fibre of each set);
- the CWeave graph is then traversed into closed loops.

The loops are the boundary of the forbidden tool-centre region at z_weave. Their complement inside the stock /
boundary is the region the tool may clear. Still NOT DECODED: FUN_180123be0 / FUN_180123ea0, log "Failed to remove intersection ...".

**Weave -> loops (decoded 2026-11-13; wd = decomp/hst3d_weave_drop.c; ported for Waterline: viewer/cam3d_rough.c wv_loops).** The Z-profile level
evaluation FUN_18040e430 (hz l.13643) calls FUN_180271330 (hmm-plans1 l.5247) on the slice's weave (Eva +0x1430 +0x10) with param_2 = Eva +0x498 (the
model) when Eva +0x1000 = WeaveResolveAmbiguous 0x1bef (EvaZProfilePlan ctor hz l.355), else 0, and param_4 = 0. WeaveResolveAmbiguous default
"(VorProfileFlatRad < 1.5 * MachWeaveResolution)" (FUN_180310790 hp l.1112-1120). Set +0x10 = fibres at x (intervals along y), set +0x58 = fibres at
y (intervals along x); a set: [0] fibres (0x38 each, +0x28 position, +8 intervals of 0x40: lo, hi, contacts, +0x30 c0, +0x34 c1, +0x38 / +0x3c end flags),
+0x18 / +0x20 position range, +0x28 count, +0x30 / +0x38 the extent along the fibres.
1. FUN_180129b00 -> FUN_180128f30 (wd l.16694, flag = (param_2 == 0 && param_4 == 0)): FUN_18012f730 (wd l.21919) on set +0x10 against +0x58, then
   the reverse, alternately, until two rounds in a row change nothing (round 11 still changing: assert "Machining\Weave.cpp" 0x21). FUN_18012f730:
   the other set's interval ends as events (value, lo before hi, fibre; FUN_180133a30) swept along this set's positions give for each fibre the
   set of crossing fibres that are forbidden there (lo events at the position count, hi events at it do not); then per fibre (tbb lambda ->
   FUN_180130300 wd l.22500): c0 / c1 of each interval = the first crossing at or after lo / the last at or before hi, + 1 (FUN_1801314e0: from the
   even spacing, one step corrected); for each crossing j, the interval nearest it (advance while pos - hi > next lo - pos); a forbidden crossing
   not covered: c0 - 1 == j -> lo = pos, c0 = j; c1 == j -> hi = pos, c1 = j + 1; else a point interval [pos, pos] (" add new point ... twist ")
   inserted before / after; then an interval with c0 == c1 joins the previous when that ends in the same cell, then the next when that starts
   there; with the flag any two consecutive intervals ending / starting in one cell join. Counters: extensions, twists, joins (changed = any).
2. FUN_1801312c0 (wd l.23293): set +0x10 then +0x58, per interval a node (0x1c: +0 set byte (1 for +0x10), +1 end (0 lo / 1 hi), +4 fibre,
   +8 interval, +0xc cell = c0 / c1, +0x10 next = -1, +0x18 keep flag) when c0 != 0 / c1 != the crossing count.
3. FUN_18012d7a0 (std::sort, insertion sort FUN_18012e9e0): by the cell before the node, key t = fibre + (end != set byte), (cell, t) for set byte 0,
   (t, cell) for 1. Linking: for each node in that order, its cell after (t = fibre + (end == set byte)); the first node (binary search) whose
   cell before is >= it; if taken, the next one when free (and that first index goes to the ambiguous list), else the first free after; that
   node's next = this node.
4. FUN_18012a140 (wd l.17669) per ambiguous index: FUN_180129df0 lists the cell's nodes (the run with that cell before) and their nexts with a side
   code (0 top, 1 right, 2 bottom, 3 left) and is sorted round the cell (FUN_18012efa0: side, then along it - ascending interval index on sides 0 / 3,
   descending on 1 / 2 - a node before its next at a tie). Without the model (param_5 = 0): starting at the first listed node of the run, each one's
   next = the entry before it (param_7 = 0) round the list. With the model: the list starts at a run node; 4 entries: the line through the
   midpoints of entries 0-1 and 2-3 (a point when the two are equal) is clipped to the cell (FUN_1801127c0 / FUN_1801121d0) and the model dropped on it
   with the weave's tool (FUN_180225ba0; extra objects of param_6 via FUN_1802364d0 - not modelled): one interval over the whole clipped line ->
   0 -> 3 and 2 -> 1 joined, else 0 -> 1, 2 -> 3; 6 entries: three lines between the pair midpoints (01-23, 01-45, 23-45), all full -> 0-5, 2-1,
   4-3; only 01-23 -> 0-3, 2-1, 4-5; only 23-45 -> 0-1, 2-5, 4-3; only 01-45 -> 0-5, 4-1, 2-3; else 0-1, 2-3, 4-5 (a -> b: a's next is b); other counts
   pair 0-1, 2-3 ....
5. FUN_18012b1e0 (wd l.18363) walks the next rings into loops (and nests them); FUN_1804031a0 (hz l.6173) takes each node's point: set byte 0 ->
   (end value, fibre y), 1 -> (fibre x, end value). FUN_18012d020 (wd l.19677, the border closing: when every fibre of one set covers its far
   extent end the last fibre of the other set gets the whole extent; FUN_18012cde0 makes each border crossing agree by a point interval;
   FUN_18012cf20 joins intervals that touch) is called from FUN_18012bf60 / FUN_180233ca0 / the Add cuts and OptiRough paths; its call on the
   plain Z-profile level weave is not traced (ported as part of wv_loops).
Port limits (ours): the probe's drop is push_tri in the line's frame. Waterline (since 2026-11-13): the weave's fibre ranges and the boundary fill
are McHmm's (section 7 "The weave box and the boundary fill"); the other styles still use the drive's / containment's XY box grown by the spacing +
the ball radius (they do not trace loops).

**Thin output.** The WeaveThin* parameters (0x1c85-0x1c8b) turn the fibre points into lines and arcs; ThinArcEnable /
ThinArcMaxRadius are set for OptiRough. NOT DECODED.

### 4.6 Regions and their boundary passes (kind 1)

FUN_1803fae40(kind 1) -> FUN_1803faee0 "DoPockets" (hmm-zplan l.921):
1. per pocket slice: FUN_18040b6f0;
2. the Voronoi diagram of the slice: FUN_180407c10 (hmm-zprofile);
3. per region: FUN_180406b00.

For kind 1, MinOffset = RadExtra = 0, so only the region boundary loops (t = 0) are output (area_mill.md 11.3 rule 1,
hmm-pass0 l.1811). Then FUN_1803ffdb0 orders the slices (NOT DECODED) and FUN_180402f40 writes the passes.

"Skip pockets smaller than" (page 1430, record 0x13e8) and "Removing over offset pocket" (hmm-zplan strings) filter the
regions. The exact rule is NOT DECODED.

### 4.7 Per-region clearing

OptiRough does not use McHmm passes inside the region. mckernel feeds each region boundary to
**mcce::MCCECalcDynPock** with the CDynPockParm of FUN_180108f1c (formats/hst3d.md). That is the engine ported in
viewer/mcce_dyn.c and validated on 2D Dynamic Mill (formats/dynamic.md). Only the executioner chain is new mckernel
work:
- FUN_180107864 -> FUN_18010687c -> FUN_180106624 -> FUN_180108510;
- reading the McHmm region output;
- per-level Z;
- step-up order.

### 4.8 Drive model: mesh or OCCT

McHmm never touches exact surfaces in the weave. It slices the **tessellation** (EvaTessArea triangles; FUN_1801e5d50
accepts surface tessellations, raw / triangulated meshes and the stock model).

So the port's weave takes a triangle list. Two sources are possible:
- today: viewer/xt_mesh.c;
- later: the OCCT layer, with BRepMesh at a deflection tied to the tolerance.

Mastercam's own tessellator (EvaSurfaceTessellationPlan 1803eb3a0, NewTessellation* parameters, about 560 KB) gives
different triangles. The choice of tessellator is therefore ours. The deviation it causes is bounded by the
tessellation tolerance, plus StepMachTolerance in the weave.

### 4.9 Next decode items for OptiRough (in order)

1. The push-cutter interval maths, FUN_180125b70 / 180125dd0 / 180126340 (all in hst3d_weave_drop.c): vertex, edge
   and facet tests for the cylinder, torus and cone bands.
2. The weave graph -> loops (FUN_18012d020 onwards, CWeave vtable, FUN_180123be0 / 180123ea0), and WeaveThin
   (lines / arcs).
3. FUN_1803fb4f0 / FUN_18040e430 / FUN_1804109a0, the per-level slice management and the stock-Z rule.
4. The mckernel OptRoughDynPockExecutioner chain and the region filter (Skip pockets smaller than).
5. The Imported Linking Plan 0x1475 and the general linker (shared with every op).

## 5. Decisions needed from the user

1. **Oracle source.** No stored 3D HST op exists, so nothing can be checked against Mastercam. Options:
   - (a) The user makes a small set of test parts in Mastercam 2017 (Windows) and saves them with NCI. For example a
     boss / pocket / dome block with OptiRough, Waterline, Raster, Area Roughing, Scallop and Pencil on 3 tools: flat,
     bull and ball. Then tools/oracle.py gets a 3D mode.
   - (b) A Wine harness that loads the original mchmm.dll and calls its 12 exported CNCGDCInterface members (Open /
     RunMacro / ReadTag) on macros we generate. Wine is not installed. mchmm needs mfc120u, msvcr120 / msvcp120 and
     tbb from the install.
   - (c) Port without an oracle, using analytic self-checks only (labelled ours).
2. **Drive mesh.** Feed the weave from xt_mesh now, or wait for the OCCT BRepMesh layer? Either way the tessellation
   is ours (4.8).
3. **Scope.** HST only, or also the classic surface ops? Their engine is in mccore and has not been surveyed.
4. **Links before the linker is decoded.** Allow the neutral SAFE link rule (ours, as in 2D Area Clearance) so 3D ops
   can be generated before the general linker is decoded? Or keep 3D ops "NOT DECODED, not generated" until it is?

## 6. Port: Dynamic OptiRough (3drough, 2026-10-24)

Work tree `~/mastercam-re/work/tree_3drough` (base `tree_3drough_base`), patch `~/mastercam-re/work/3drough.patch`.
Decisions 3D-1..4 were chosen by the assistant pending the user's confirmation (formats/user_decisions.md).

### 6.1 Sources
- `viewer/cam3d_rough.c` (included by cam2d.c): mesh -> Z levels -> slice -> regions -> mcce per region -> moves.
  Header comment = the decode map + every "ours" / NOT DECODED item.
- `viewer/occt_mesh.cpp/.h`: XT -> OCCT (viewer/occt/xt_occt.cpp) -> BRepMesh triangles (decision 3D-2). Built when
  OCCT headers exist (`HAVE_OCCT`, viewer/Makefile `OCC_INC`, viewer-qt/CMakeLists.txt); else a stub that refuses.
- `viewer/xt_mesh.c`: `xt_schema_find` (the schema file's path for the OCCT reader).
- `CAM_OPTIROUGH` = 13 (cam2d.h), label "OptiRough" (= the 3D gallery item, viewer/ui/layout.txt), CamOp.drive[] (drive
  solids, sidecar `DRIVE <index> <idn>`), cam_cli `optirough` with `--solid N` / `--stl FILE` (ours, testing).
- Default op = the first op 132 with style 1 in Mill_MM / Mill_Inch.mcam-defaults (cam_fileop.c default_op); stored op
  132 is NOT regenerated (cam_fileop_type unchanged: fileop stays 0/189).

### 6.1b What was decoded this session (sources)
- **Op record map** (pages 1465 / 1430): HSTOptRoughParamPage UpdateLocalOp FUN_180093c30 / MapToDlg FUN_180091f20 / stepover check
  FUN_180091cd0 and HSTOptRoughTransitionsPage UpdateLocalOp FUN_180095d60 / MapToDlg FUN_180094e80 (decomp/mcmsurf_all.c). The
  page's local op is the record - 0x2088 (tool diameter +0x23d8 = record 0x350, opcode +0x20f4 = 0x6c). Fields: stepdown 0x1228,
  stepover 0x1230 (clamped to 95 % of the flat diameter), Stepup 0x16ec / 0x1478, Mill vertical walls = the sign bit of 0x14a0,
  min toolpath radius 0x1480, micro lift 0x1488, back feed 0x1490, conventional feed 0x1508, cut method 0x16e4 (Conventional) /
  0x1728 (Zigzag), Optimize cut order 0x16c0, stepup ordering 0x16c0 + 0x172d, stepdown ordering 0x1660 (s16), tip compensation
  0x2f8, gap retract combo 0x16eb, gap radio 0x16cd (1 = %), gap distance / % 0x1308 / 0x1310; Transitions: entry method 0x16e8
  (Helix only / ... medial), helix radius 0x1328, loop radius 0x1450, slot width 0x1458, Z clearance 0x1330, plunge / pitch radio
  0x16e9, plunge angle 0x1338 (rad), entry pitch 0x1460, skip pockets 0x13e8, entry feeds group 0x16ea, ramp feed 0x1468, ramp
  spindle 0x1578, dwell spindle 0x1470. The TP fields of FUN_180108f1c line up with these by block shifts (TP = record - 0x1078 /
  0x1088 / 0x10b8 / 0x1269), consistent over 2-6 anchors per block (the copy itself is mccore's, NOT DECODED).
- **CDynPockParm for OptiRough** (FUN_180108f1c): flags 0x12 (0x40 instead of 0x10 for Core), 1 = climb (set when K+0x47c = record 0x16e4 = 0: mk l.205392; correct since 2026-11-09, formats/audit_2026-11-09_highspeed_surface1.md A1), order_mode from record 0x1660 (mk l.205385-205391), 0x100..0x1000 entry method, 0x2000 =
  plunge-angle radio, 0x10000..0x80000 retract mode, 0x40000000 entry feeds, 8 metric; engagement acos(1 - stepover / R) capped at
  170 deg; approach 0.65 D (Core); chain tolerance + sliver.
- **Region walls**: the executioner FUN_180106624 calls FUN_180107dcc(D x 0.5) on every region before MCCECalcDynPock (the region =
  tool-centre boundary, offset by R to the pocket wall mcce expects); FUN_180107e34 offsets the containment by R when TP+0x485.
- **Z levels**: FUN_18040da70 verified line by line (StepDownInflexionTolerance = min(0.01, Stepdown x 0.01), 1/n in single
  precision); PosZHi / PosZLo = job +0x40 / +0x38 or the depth limits (FUN_180071314 l.87321-87348): their job values are NOT DECODED.
- **XT B-surfaces** (viewer/occt/xt_occt.cpp): B_SURFACE / NURBS_SURF (vertex order v fastest: the u-fastest order leaves the edges
  0.0194 mm off their faces, v-fastest 0.0006), B_CURVE / NURBS_CURVE, SP_CURVE, TRIMMED_CURVE (parm_1 / parm_2 are parameters of
  the basis as defined, not sensed; lines in metres), ELLIPSE; tolerant edges (EDGE.tolerance). TUTORIAL 7's solid now builds
  (26 faces, BRepCheck valid before healing) and meshes.

### 6.1c Not decoded / ours (the engine refuses or labels them)
- NOT DECODED, refused: Stepup (flat-level step-ups), Zigzag, entry methods 1-4 (medial axis, as B9-3), Tplanes other than Top.
- NOT DECODED, stand-in with a warning: Core (from outside) is cut as Pocket (decision B6-1's rule).
- NOT DECODED, not applied (greyed on the pages): Skip pockets smaller than, Optimize cut order / stepups / stepdowns, tip
  compensation, entry feeds / speeds (air dwell), the 2D stock model between levels (every level clears its whole region), the
  stock to leave for op 132 (CamParams.stock_xy is used for walls and floors), the cut tolerance's record field (CamParams.tol).
- Ours: the tessellator (OCCT BRepMesh, deflection = tol / 10); the fibre intervals' polygon (quads between overlapping intervals of
  neighbouring X / Y fibres, united - the CWeave traversal FUN_18012d020 on is NOT DECODED); the closed-form push-cutter for the
  ball + cylinder weave shape (same set as FUN_180125dd0 / 180126340 / 180126130 / 180126e90 / 180127d50); the containment default
  (the model's XY box); the first level at PosZHi cut only when the top of stock is above it; the SAFE links; the first record's start
  Z = its own Z (the decoded OutputMotionToBnci rule, qt_port_plan.md B9-5 - applied to this new op type only).
- The dialog: title "High Speed - Dynamic OptiRough" (mcmsurfres 441 / 159, joined as 2D's - ours), the page tree (Cut Parameters +
  Transitions + the common pages; mcmsurf's page factory is NOT DECODED), the drive pick = whole part solids (mcmsurf's surface /
  face selection and its "Toolpath/surface selection" dialog are NOT DECODED), saving into .mcam refused (the drive references are
  NOT DECODED; the op stays in the sidecar, DRIVE lines).
- MCP: the scripted steps chainmode:OptiRough; drvsolid:N; drvdone; opok make the op through the MCP "steps" request.

### 6.2 Self-checks (ours; tools: ~/mastercam-re/work/hst3d/scripts/mkstl.py, gouge.py)
- boss block (pocket 60x50 to -20 with a 20x20 boss to -5), D10 flat: one level at -20 (stepdown 20 from the default
  op), free region area 1121.5 = analytic (2000 - 878.5); worst radial intrusion 0.013 mm (Clipper round-join chords).
- dome block (spherical cap R 40 on a 1 mm heightfield, stepdown 6), D10: flat - worst radial intrusion 0.020 mm; ball (Rc 5) -
  0.050 mm; bull (Rc 1) - 0.031 mm; no rapid below the model top. The intrusions are the fibres' chords at the cap edges of the
  stepped test surface and Clipper's round-join chords (ours, both bounded by the cut tolerance's order). `tools/tests/hst3d_check.sh`
  runs these four cases (limit 0.06 mm).
- BRACKET4 solid 1 (OCCT mesh 8136 triangles), D3 / D6: the slices find the 2 through holes, the 4 box corners and the lower
  shelves; screenshots/2026-10-24_optirough_backplot_bracket4.png (stepdown 3).

### 6.3 Oracle: TUTORIAL 7 METRIC op 5 (the only stored OptiRough op with NCI, section 1.3b)
- Its drive is two Mastercam NURBS surface entities (class 0x100 kind 0, ids 2135572447 / 2135572899; formats/mcam.md "NURBS
  surface" layout confirmed by the sizes: 47 + 59 + 3 x 43 x 55 = 7201 doubles; the control net runs **u fastest**). The part's
  solid is the machine group's stock (stock shape 2), not the drive. This program does not read surface entities, so the oracle
  samples them into an STL (tools/surf2stl.py, ours, 1 mm grid) and runs `cam_cli optirough --stl` with the op's own record
  (`--set file_op_id=5`), its containment chain (role 2) and D 12 flat, tolerance 0.025 (record 0x12a0 = 0.025, inferred as the
  cut tolerance).
- Levels: Mastercam cuts one level, Z -16.975 = 1.025 - 18. With PosZHi = 1.025 (MCAM_3D_ZHI, the job +0x40 value is NOT
  DECODED: the drive top is 0) the decoded level rule gives exactly 1.025 -> -16.975 -> -25.403 (the last one has no free area).
- `tools/oracle3d.py` (moves vs the stored NCI, 0.1 mm samples): ref->ours p50 0.435 / p95 1.045, ours->ref p50 0.532 / p95 7.861,
  cut length ref 20093 / ours 23595, swept floor at -16.975: missed 0.00 %, extra 17.25 %. Mastercam's path stays **8.0 mm inside
  ours on every side** (x +-126.91 vs +-134.96, y -53.29 / 47.16 vs -61.30 / 55.17): our region is the drive slice with R; the
  source of Mastercam's extra 8.0 (= R + 2.0?) is NOT DECODED - candidates: the drive stock to leave of op 132 (the Model
  Geometry / surface-group stock, FUN_18006f2e8 / FUN_18006f3a4, not mapped), the McHmm region output at MinOffset = stepover
  (macro: MinOffset = MaxOffset = TP+0x1b8), the inner lambda of FUN_180107dcc. A diagnostic run with 2 mm wall stock (ours) gives
  ours->ref p95 5.6 / extra 13.6 %, so it is not a plain stock offset either. Inside the common area the trochoidal passes agree
  (ref->ours p95 1.0 mm).
- Run time: slice 0.8 s (123k triangles), MCCECalcDynPock 85 s for the 31 177 mm2 region.

### 6.4 Next items
1. The region boundary offset above (decode FUN_180107dcc's lambda FUN_180104538 and the McHmm kind-1 region output of
   FUN_1803faee0 with MinOffset = stepover) - then re-run the oracle (expected: the outer 8 mm band disappears).
2. PosZHi / PosZLo (job +0x38 / +0x40, FUN_18006f158) and the 2D stock model (FUN_18010921c) between levels.
3. Stepup (FUN_180411bd0 / FUN_180414620), Core (from outside) boundaries, the region filters (Skip pockets smaller than).
4. A reader for Mastercam surface entities (class 0x100) as drive geometry (the corpus' Area Roughing / finishing ops all drive
   surfaces), and the HST surface / face selection + "Toolpath/surface selection" dialog.
5. The CWeave traversal (FUN_18012d020 on) and WeaveThin, to replace the quad polygon (ours).

### 6.5 Regression (~/mastercam-re/regress/run_3drough, blessed.txt)
fileop 0/189, post 0/16 (+ the four opt-in post runs 0/16, arc-filter fileop 0/189), dlg_fileop_cmp 1/205 (NAKTO, known),
pf_roundtrip same, parity OK, mcp OK, mpint OK, mp oracle c947f0c2, the 2D oracles unchanged. Every existing UI output equals the
pristine base tree's (run_3drough_base): wf 0/123, chain 0/63, solid 0/80, op 0/329, tool 0/226, misc 0/75, stockplanes 0/122,
planes 0/38. New op scenes gen_optirough / gen_optirough_refuse (blessed, 4 files). 29 scene shots differ only by the enabled
OptiRough gallery icon (29 x 29 px at 317, 70) - to be blessed at merge (the shot goldens are shared). New: tools/tests/hst3d_check.sh
(HST3D CHECK OK), tools/tests/hst3d_oracle.sh (6.3). tools/tests/fileop_all.sh honours CAM_CLI (a work tree's cam_cli; default
unchanged).

### 6.6 Stock to leave + tool containment (2026-10-24, on master)
- **The 8 mm band of 6.3 is the wall stock.** ToolThicknessXY = TP+0x318, ToolThicknessZ = TP+0x320 (macro builder FUN_180070b1c,
  mk l.86744-86770; FUN_18006f2e8 / FUN_18006f3a4 read the same pair). Record 0x13c0 / 0x13c8 (block shift TP = record - 0x10a8)
  [inferred] from the corpus: walls / floors 1 / 1 in every op 132 of Mill_MM.mcam-defaults, 8 / 1 in the TUTORIAL 7 OptiRough, 4 / 0 in
  its Hybrid, 1 / 1 in TUTORIAL 2 SWARF's roughing and 1 / 0 in its Scallop, 0.05 / 0.05 in MOLD BOTTLE's roughing, 0 / 0 in its
  Scallop. cam3d_rough.c reads them (was CamParams.stock_xy for both). Not ported: the page that edits them (Model Geometry),
  FUN_180070b1c's bVar3 branch.
- Oracle (tools/tests/hst3d_oracle.sh): ref->ours p50 0.386 / p95 1.024, ours->ref p50 0.387 / p95 1.040 (was 7.861), cut length
  ref 20093 / ours 19981 (was 23595), swept floor at -16.975 missed 0.01 % / extra 0.00 % (was 17.25 %). The max 17.2 mm stays
  (entries / links).
- **Tool containment** (dialog 25624, DDX FUN_18039af70, MapToDlg FUN_18039b4d0 mcmsurf l.573978): record 0x16c7 Inside (0) /
  Center (1) / Outside (2), 0x12b0 Offset distance, 0x16c8 Add offset distance to tool radius, 0x1730 Tool tip / contact point.
  Applied per the help pages (sr5axfields Fields_HMM): the containment moved by Offset (+ R when added) inward for Inside, outward for
  Outside, not for Center. No effect on the oracle (its free region is bounded by the drive wall, not the containment). Where
  Mastercam applies it (FUN_180107e34, TP+0x485) is NOT DECODED.
- Side effect (user decision R-1, option 1): new ops take the defaults' 1 / 1 mm stock; BRACKET4's default OptiRough (stepdown 20:
  one level at the part's bottom face) now finds nothing to cut, because that level lies in the 1 mm floor stock. Mastercam's lowest
  level (PosZLo = job +0x38 or the depth limits TP+0x1c0 / 0x1c8 + job +0x28, FUN_180071314 l.87321-87348) is NOT DECODED - next.
  gen_optirough re-blessed with the failure. tools/tests/hst3d_check.sh sets the stock to 0 (`--rec 13c0=d:0 --rec 13c8=d:0`): the
  four cases give the documented intrusions (0.013 / 0.020 / 0.050 / 0.031).

### 6.7 PosZHi / PosZLo (2026-10-25)
- **Chain**: mcmsurf process_3rd_party_algorithm FUN_1800199d0 (mcmsurf_all.c l.18230) -> FUN_180258310 (l.395462) writes the drive's
  tessellation to tmphardmillraw.stl and returns its lowest / highest vertex Z into hstJobIdDat +0x28 / +0x30 -> mccore
  run_mchardmill_macro -> FUN_1809251c0 copies hstJobIdDat into the TPHSTToolPath at +0x20 (decomp/poszlo/: setup.c, hardmill.c,
  core_hst.c, callers*.txt) -> mckernel FUN_18007bb48 (l.91572): job +0x38 / +0x40 = that range (job flag +0x50 = 1, FUN_18006dd68).
- **Op 132** (l.91783-91790): floor stock ToolThicknessZ (TP+0x320) > 0 is added to PosZHi, < 0 to PosZLo; PosZHi += TP+0x220,
  PosZLo -= TP+0x220; kernel styles in the mask 0x5b6 (not 9 = OptiRough) also lower PosZLo by the corner radius (TP+0x70 +0x10,
  GetHstCornerRadius).
- **TP+0x220 = record 0x12a0** [inferred]: the "x 0.01" tolerance use of op 132 matches mcmsurf's op +0x1238 (= record 0x12a0) in
  FUN_180258310; the default 0.001 / 0.025 (FUN_18006dd68); 0.025 in the corpus. (A new block shift, 0x1080.)
- **Check**: TUTORIAL 7 op 5 without the MCAM_3D_ZHI override: PosZHi 0 + 1 + 0.025 = 1.025 = Mastercam's; the levels and the oracle
  metrics are unchanged (ref->ours p95 1.024, ours->ref p95 1.040).
- **Not ported** (record offsets NOT DECODED): the stock model range (FUN_18007d7c8, TP+0x463), the depth limits (FUN_180071314, TP+0x464,
  + job +0x28), the rest stock range (FUN_18006f158). Stock below -(corner radius + tolerance) takes FUN_1800f1860 (NOT DECODED, refused).
- BRACKET4: the part spans -12..0, so the levels are 1.025 / -13.025 (stepdown 20, scene gen_optirough: still nothing to cut - its one level is
  under the part; re-blessed: 5 regions, 4 after 6.8's decoded tessellation tolerance). Stepdown 3: 6 levels, 2292 moves.

### 6.8 Mastercam surfaces as the drive (2026-10-25)
- Read: SURF_ID records (mcam_load.c `Surf`: idn, eptr, level, colour, kind, blanked, the record). Meshed by OCCT (occt_mesh.cpp
  occt_mesh_surf): kind 0 NURBS and kind 10 trimmed on a NURBS base (formats/mcam.md "NURBS surface" / "Trimmed surface"); other
  kinds refused. A 120-part sample: 567 / 567 NURBS and 385 / 385 trimmed mesh; 22 kind 1 refused; 467 blanked skipped.
- Drive: CamOp.dsurf (sidecar DSURF lines); a stored op's own drive = its TP kind 4 references (cam3d_rough.c c3_mesh_op);
  cam_cli `--surf N`; the drive pick (cui_chain.c) takes solids or surfaces (st_pick_drive_at, ours); script step drvsurf:N.
- **Tessellation tolerance decoded:** msurf_map (mcmsurf l.340012) sets msurf_chord_dev = op +0x1238 = record 0x12a0 for op 132, the
  value handed to the drive tessellation (FUN_1803b94d0) - the same field as TP+0x220 of 6.7, which supports that mapping. Used as
  the OCCT deflection (the tessellator stays ours).
- Oracle (tools/tests/hst3d_oracle.sh) now on the part's own two NURBS surfaces (no surf2stl): ref->ours p50 0.389 p95 1.024, ours->ref
  p50 0.406 p95 1.038, cut length ref 20093 ours 19884, floor at -16.975 missed 0.00 % extra 0.00 %; PosZLo -25.443 (exact surfaces).
- Display (scene.c mesh_surfs, ours): shaded in the entity colour in the shaded / translucent modes, boundary edges always,
  blanked surfaces not drawn; a unit nearer depth bias than solids. Mastercam's surface display settings are NOT DECODED.

### 6.9 Check surfaces and containment (S7, 2026-11-12; formats/surface_selection.md 8)
- Check surfaces: bnci_surface sets record 0x172c (K+0x495) = 0 for styles 0 / 1 / 3 / 4 / 6 -> the checks join the drive STL
  (FUN_180258310(2)), warning 282 / 287: ported (c3_mesh_op). Other styles: "HST check trimming method" (registry, 1 when absent) -> the
  kernel's Edit Passes to Surfaces 0x141f, NOT DECODED: refused.
- Containment: every chain of the op (Project: role 2 only), open chains closed by their start (FUN_180128390): c3_containment.

## 7. Waterline (UI style 2, kernel style 1) - decode and port (2026-10-25)
**Decode** (mk = decomp/mckernel_all.c, hz = decomp/hmm_zplan.c, hp = decomp/hmm_pass0.c, hl = decomp/hmm_link.c, ms = decomp/mcmsurf_all.c):
- Parameters (FUN_180071314 style-1 branch mk l.87822-87911; record offsets [inferred], TP = record - 0x1078 to 0x1258, - 0x1080 from 0x1268):
  Stepdown TP+0x1b0 = 0x1228 (<= 1e-12: 2.5 metric / 0.1 inch); PosZHi / PosZLo as 6.7, or with Use Z depths (TP+0x464 = 0x16c9) max / min of
  TP+0x1c0 / 0x1c8 (0x1238 / 0x1240) + job +0x28; ContactAngleLo / Hi = 0x1248 / 0x1250 (radians, x 57.2958, clamped 0..pi/2); AdaptiveStepdown
  only with Add cuts 0x16c2 (Min stepdown 0x1258, profile stepover 0x1268); ProfileSmoothing only with Corner rounding 0x16c3 (tolerance
  max(0x1270, 2 x job +0xa0), max radius 0x1278); PosContactOnly = (0x16cf == 0) false. Common: **ToolThicknessXY = max(XY, Z stock)**,
  ToolThicknessZ = Z stock (mk l.87577-87611). Plan default StepDownEvenLastSteps = **5** (hp l.596-601).
- Passes (EvaWaterlinePassesPlan, slots [2] / [3] = OptiRough's Z-level rule; FUN_1803fabf0 kind 0, hz l.880): FUN_180406b00 outputs only the
  boundary loops of each level's tool-centre region (hz l.8711-9057); the angle filter's reader is FUN_18040b6f0 (see **Contact angle filter** below); no shallow area plan.
- Chain after the passes (mk l.93567-93629): Waterline alone by default; rest / check surfaces add Edit Passes to Surfaces / Convert To Primary.
- Linking: Waterline Linking Plan 0x141c (mk l.92434); LinkByZLevel = (0x16c0 == 0); direction style from 0x16bd / 0x16be: Climb one way,
  Conventional one way reversed, Zigzag bidirectional (hl l.18604-18640); transitions 0x16d2 / angle 0x1358; the linker is NOT DECODED
  (decoded parts and the missing functions: formats/hst_linker.md, 2026-11-13).
- Pages: 1324 (HSTWaterlineParamPage) and **1341** (transitions, not 1333): formats/opdlg_fields.tsv s1324 / s1341.

**Port** (viewer/cam3d_finish.c gen_waterline): c3_levels_even(.., 5), c3_slice per level, the forbidden region's loops inside the containment
as passes (split where they leave it), stock XY = max, the depth limits, the cut method direction, the tool containment offset as OptiRough.
Refused: Add cuts, Corner rounding, Bottom to top, contact angle limits with a flat tool (the normal at the tool's edge); since 2026-11-12 contact angle limits with
a ball / bull tool are generated (N9-2's condition met, "Contact angle filter inputs" below). Ours: the default containment (the drive box widened),
the loop direction sense (Climb = material on the right), the loop start (nearest the tool), SAFE links, Optimize = By depth.

**StepMachTolerance (2026-11-08)**: job +0xa0 = 0.5 x record 0x12a0 (FUN_18006dd68 mk l.84701-84718, the HSTToolPath constructor used by every HST style; default 0.025 mm / 0.001 in when <= 1e-12) - cam3d_rough.c c3_step_tol, used now by Waterline, Area Roughing, Raster, the grid ops (Scallop, Spiral, Radial) and OptiRough (it was CamParams.tol = the 2D default 0.005 before, whatever the unit; for the inch ops 10 x too coarse, for the metric 2.5 x too fine). TP+0x220 itself (the full 0x12a0) stays where the kernel uses it: the Bounding Box offset, the PosZHi / PosZLo tolerance.

**Contact angle filter (2026-11-08, Waterline: FUN_18040b6f0 hz l.11835-12215)**: EvaMachLocBasePlan's constructor FUN_1802f2340 (mchmm.dll, decompile l.504-546 of that function; constructor of the Z-profile evaluators) sets Eva +0x670 = 1 and the cosine window from ContactAngleLo / Hi / Tol (0x1ac4 / 0x1ac5 / 0x1ac6; mckernel passes degrees, the plan reads radians): Eva +0x678 = cos(Hi) when Hi <= pi/2 - Tol, else -0.1; +0x680 = cos(Lo) when Lo >= Tol, else 1.1; +0x688 / +0x690 = the same with Hi + Tol / Lo - Tol. ContactAngleTol default (mchmm FUN_18030a730, the default expression of 0x1ac6): min(Lo > 0 ? min(Lo / 10, 1) : 1, Hi < 90 ? min((90 - Hi) / 10, 1) : 1) degrees, so 1 deg for 50..90. FUN_18040b6f0, called per level from FUN_1803faee0 (hz l.1005) on the level's loops before the passes are built: a vertex is kept unless its contact word has the clipped-end flag (top byte < 0), or FUN_1801344c0 (a contact on an upper tool layer, Eva +0x6e0 / +0x6e4: PosContactOnly + tool layers) is true, or (Eva +0x670) the contact normal's Z (FUN_1802538d0: the contact parameter on the tool's weave ball / profile layer, i.e. the tool's surface normal at the contact) is outside [+0x678, +0x680]. Then each run of dropped vertices between two kept ones is restored when none of its vertices is outside the wider window [+0x688, +0x690] and otherwise when its path length (kept vertex to kept vertex, measured along the loop) is below PassesJoinGaps (0x1ad6: default 1.0, FUN_180310790 hp l.725-731 - the same mm default as ha 2.3's +1.0 tip push). Runs left dropped split the loop into open passes.
Port (cam3d_finish.c wl_angle_keep, gen_waterline): the same flags and restore rules; ours: the contact normal is found on the mesh (the point of the near triangles closest to the tool's ball-centre circle; a point above the ball centre lies on the flute, normal horizontal, as the profile's cylinder layer in FUN_1802538d0). Not modelled: FUN_1801344c0 (no tool assembly here). A flat tip with limits is refused (the edge contact's normal is not decoded). Oracle: TUTORIAL 2 SWARF.mcam op 4 and MOLD BOTTLE.mcx-9 op 4 (both 50..90, previously refused; tools/tests/waterline_oracle.sh second metrics line, numbers in formats/oracle_results.md).
**2026-11-14 (formats/hst_linker.md 28, N13-34)**: the contact the filter reads is the weave's contact word (FUN_180253420 / FUN_1802538d0, carried from the drop to the loop vertex); ported - the weave's own loops (ball tools) now use the word's normal and FUN_1801344c0, = the DLL vertex by vertex on MOLD BOTTLE op 4 (38846 / 38846, 98 / 98 passes) and on SWARF op 4's weave (34620 / 34620). The window takes the degrees as mckernel writes them ("%19.9f": 90 / 50 exactly; the record's 89.99999999999997 had made the low end cos(Hi) > 0 and dropped every vertical contact). The mesh normal (wl_contact_nz) stays for the Clipper offset polygons of bull tools (no words: the flags' way through the Voronoi offset is W3 stage 3).
The earlier statement (below) that the angle parameters are read only by the dialog and the default expressions was wrong: FindImm had missed the double getter calls in FUN_1802f2340.

**Investigation 2026-11-08 (the lowest levels)**: the extra length at Z 12.05 .. 8.49 does not look like corner rounding (whole straight sides of the rings are absent, not corners; not proven: FUN_180557f60 is not ported, so the oracle cannot run with it on, and the earlier note in this section naming it as the cause was never tested). Per level (TUTORIAL 2 SWARF_METRIC op 3, 94 drive surfaces, stock 1 / 1): from Z 40.8 to 13.2 ours matches the stored levels (ref->ours p95 0.07-0.27); at Z 12.05 / 10.87 / 9.67 / 8.49 Mastercam keeps only the rings on the steep walls (contact normal about 88 deg from vertical; these angles come from a crude sampled-triangle contact search, scratch analysis, not from the engine) and omits the rings along the shallow pocket floors (normal 20-35 deg from vertical, floors 1378-1446 at Z 6.9-11.3) that ours cuts: the 28 levels are right, the regions are not. The rule that removes them is NOT DECODED. Traced and ruled out: the weave contact flag FUN_1804039a0 (hz l.6565) is 0 only for an interval end clipped to the fibre window (index | 0x80000000, FUN_180125b70 hst3d_weave_drop l.15072; FUN_18012cde0 l.19542), not for floor contacts; ContactAngleLo / Hi 0x1ac4 / 0x1ac5 are read only by the dialog FUN_18043f440 and default expressions (FindImm over mchmm.dll; the stored op has 0..90 anyway); PosContactOnly 0x1ae1 reaches the pass builder (+0x604 -> +0x114, FUN_180120e60), its removal condition on these floors not established; FUN_1803fb4f0 hmm-plans1 l.4398: Eva+0x15c0 = PosContactOnly and a tool holder present. (Superseded 2026-11-08: the contact angle filter above is that rule, but the stored op's record says 0..90 and it is not dirty, so it does not explain this op: TUTORIAL 2 SWARF_METRIC op 3's shallow-floor rings stay NOT DECODED - it is the only op of the three with Corner rounding on, and FUN_180557f60 is not ported.) A rule that would fit (drop the shallow contacts by angle) has no constant in the code, so it is NOT implemented. Corner rounding: FUN_180555f60 passes CornerTolerance as [3] of the boundary loop spec even at t = 0, so FUN_180557f60's corner block runs on the Waterline loops (hmm-vorlib l.15164-15182 / l.16360-16560); it needs the Voronoi graph (not in the port).

**Oracle** (tools/tests/waterline_oracle.sh): TUTORIAL 2 SWARF_METRIC op 3 (D12 r3, stepdown 1.2, 11 drive surfaces, containment, depth limits)
with Corner rounding switched off: the 28 levels equal Mastercam's (the first 24 exactly, the last 4 within 0.012); ref->ours p50 0.037 / p95
0.56 mm; ours has the thin channel loops of the lowest levels that Mastercam's corner rounding removes (the expected difference).

**Contact angle filter inputs (decoded 2026-11-12, mchmm.dll in ~/mastercam-re/ghidra_hmm; the three items of user decision N9-2, so the refusal of limits other
than 0..90 is lifted):**
- The evaluator: EvaWaterlinePassesPlan's constructor FUN_18040ea00 (the only writer of its vftable 1807a08f0) calls FUN_1803f9c20 (EvaZProfilePlan), which calls
  FUN_1802eb170 (EvaMachiningBasePlan), which calls FUN_1802f2340 (EvaMachLocBasePlan, the window stores) - so the Waterline evaluator has the window.
  FUN_18040ea00 then sets Eva +0x6e0 = PosContactOnly (0x1ae1) ? 1 : -1, the layer argument of FUN_1801344c0 (-1 returns false at once).
- DAT_1809bf1a0 = pi / 2: a dynamic initializer (180002320) stores DAT_1809bf190 x 0.5 (constant 1807bb628), and 1800022d0 stores DAT_1809bf190 =
  acos(-1.0) (the import at IAT 180746730 is MSVCR120 acos; -1.0 at 1807bba88). DAT_1809bf198 = DAT_1809bf190 x 2 (180002300).
- The angle getter FUN_180389560 returns the parameter x DAT_1809bf198 / 360 (degrees -> radians); mckernel passes the angles in degrees with scale 1.0.
- PassesJoinGaps 0x1ad6: default 1.0 (FUN_180310790 hp l.725-731), not written by the Waterline macro; McHmm's lengths are mm: mckernel writes every length
  x job +0x80 (FUN_1800edd8c), job +0x80 = 25.4 for an inch part, 1.0 for metric (FUN_18007bb48 mk l.91625-91633). So 1.0 mm = 1 / 25.4 in.
- ContactAngleTol 0x1ac6 default re-read (FUN_18030a730): "min(Lo > 0 ? min(Lo / 10.0, 1.0) : 1.0, Hi < 90 ? min((90 - Hi) / 10.0, 1.0) : 1.0)", DDV 0..180.
- PosContactOnly's default (FUN_180303660): "false" for one caller context (its third argument 7), else NOT (plan 0x1435 / 0x143a / 0x1460 / 0x1478), i.e. true for
  Waterline; mckernel writes false only when Flute contact is off (TP+0x46a = record 0x16cf = 0, mk l.87892-87896). So with Flute contact on, Eva +0x6e0 = 1 and
  FUN_1801344c0 drops contacts whose tool layer (contact word bits 25-30) is above 1, or is 1 when Eva +0x6e4 = 0 and the contact's byte +0x1b bit 0 is set. Which
  profile band gets which layer number is NOT DECODED; ours keeps every contact (no tool layers in our weave), as before.

**Recess filled by our weave polygons (found 2026-11-12; fixed 2026-11-13 by porting McHmm's loop traversal, 4.5 "Weave -> loops"):** our slice
turned the fibres' intervals into polygons by uniting quads between overlapping intervals of neighbouring fibres of each direction
(cam3d_rough.c fibres_quads, ours, still used by the other styles). A quad between a split interval and a whole one of the next fibre crosses the
gap, so a recess narrower than the fibre spacing in that direction was filled over its whole length. TUTORIAL 2 SWARF.mcam op 4: a planar face
(surface 42, 2 deg draft) set back 0.017 in from its neighbours over 1 in; Mastercam enters it at every level (x = -2.5016 at Z 1.356 ...), ours
stayed 0.014 out at 17 of 26 levels. gen_waterline now builds the loops with wv_loops (cam3d_rough.c: McHmm's consistency rounds, nodes, cell
linking, ambiguous cells - with WeaveResolveAmbiguous by the probe line through the cell, without it by the order round the cell): the face is
matched at every level. Results (cutting moves): TUTORIAL 2 SWARF op 4 ref->ours p95 0.131 -> 0.087 in (max 0.726 -> 0.377), ours->ref p95
0.007 -> 0.002 (max 0.089 -> 0.524: the groove sides below), cut 390 -> 404 vs 425; MOLD BOTTLE op 4 (WeaveResolveAmbiguous on, ambiguous cells
by the probe) ref->ours 0.199 -> 0.194, ours->ref 0.011 -> 0.002 (max 0.021 -> 0.003), cut 534 -> 541 vs 603.
**BRACKET4 (2026-11-13):** the default Waterline (scene gen_waterline) was refused at Z -10.595 ("a weave node without a free node in its cell"): our
triangle push (push_tri, ours - McHmm's interval formulas FUN_180125dd0 / FUN_180126340 / FUN_180125b70 are NOT DECODED, 4.5) gives long intervals
for the sliver faces of vertical walls nearly along a fibre, which reached the grid's border fibres, where a loop cannot close. With the weave box
and the boundary fill below (ported 2026-11-13) every border fibre is wholly forbidden, and the op generates (push_tri unchanged).
**The weave box and the boundary fill (decoded and ported 2026-11-13: cam3d_rough.c wv_fill / c3_weave_rng, cam3d_finish.c gen_waterline):**
FUN_1803fb4f0 (hz l.1098-1225, the Z-profile set-up): the slice weave is a CZProfileWeave built by FUN_180270d10 (mchmm 180270d10, Ghidra) on the
boundary collection Eva +0x118 (the constraint boundaries: the containment / the Bounding Box Plan), with A = BoundaryOutwardsOffsetRad 0x1bf0
(x 1.1 when PosContactOnly and it equals the shifted tool profile's first value: FUN_18013e330(profile Eva +0x208, 0, 0, -VorProfileFlatRad), decoded
and ported 2026-11-13: Eva +0x208 is built by FUN_1802f5270 from EvaMachLocBasePlan's constructor FUN_1802f2340 - FUN_18013e200(ToolShaftRadius 0x1abe,
max(ToolCornerRadius 0x1abd, -ToolThicknessZ)) = { [0] shaft, [1] corner, [3] = [0] - [1] }, then FUN_18013e330(., ToolHolderThickness, ToolThicknessZ, 0) adds
ToolThicknessZ to [0] and [1]; the shift by -VorProfileFlatRad gives [0] = corner + ToolThicknessZ when VorProfileFlatRad = [3] > 0, else shaft +
ToolThicknessZ - VorProfileFlatRad; compared exactly in McHmm mm) + 1.0 (+ a stock-shaft term when Eva +0x1030, CheckAreaClearance-
StockShaft 0x1cc0, off for Waterline) and MachWeaveResolution: FUN_18012bc80 -> FUN_18012be70 (wd l.18891): the fibre ranges (+0x198..+0x1b0) =
the boundary collection's box (+0x18..+0x30) grown by resolution + A; FUN_180270d10 then sets the cull box +0x168..+0x180 = the ranges grown by the
tool profile's radius (weave +0x98). BoundaryOutwardsOffsetRad default (FUN_180310790 hp l.1127-1137): "VorProfileFlatRad + (PosContactOnly ?
1.0 + MachWeaveResolution : 0.0)" (mm; PosContactOnly is true for Waterline unless Flute contact is off). FUN_18012bf60(slice weave, boundaries,
r = that BoundaryOutwardsOffsetRad, r != 0, 1) (hz l.1214; the third argument is the offset, not the Z): FUN_180131810 (wd l.23535) on both fibre sets.
FUN_180289d90 (mchmm.dll, read 2026-11-13 with its callees): per fibre P0 -> P1 (set +0x10: (x, extent lo) -> (x, extent hi)),
- FUN_180289390 extends it past the collection's box: s = max(t, 1) + 1.01, t = the largest (corner - P0) . d / |d|^2 over the box corners, P1' = P0 + s d;
- MeasureRadSegs2D (FUN_18023ada0 over the collection's ToolBoxSet grid, FUN_180231900 "BuildToolBoxSetRoundEverything"): for each line / arc element
  (FUN_18028a970 / FUN_18028ab70) whose box meets the box of P0 P1' grown by 1.1 r: FUN_180057040 records a crossing with the fibre's line when the
  element's ends lie on different sides (side = cross > 0; parameter = the projection of the intersection point onto P0 P1', with the side), and,
  when r != 0, FUN_180055b50 the parameter range within r of the element (clamped to [0, 1], dropped when [1, 1]; the switch +0x128 of the
  collection selects end discs or end perpendiculars - not traced, the port uses discs);
- FUN_180289520: the crossings sorted, those at t >= 0 counted (odd = P0 inside); the inside ranges between successive crossings, joined with the
  within-r ranges (r != 0), each x s, hi capped at 1, ranges starting at 1 or later dropped.
FUN_180131810 with fill = 1 puts on the fibre the complement of those ranges: [extent lo, first lo] unless that lo is 0.0, [hi, next lo] between,
[last hi, extent hi] unless that hi is 1.0, the whole fibre when there is none, mapped t -> (1 - t) lo + t hi, every one with both contact words
0x80000000 (the clipped flag; then FUN_18012d020). FUN_18040b6f0 drops a loop vertex with that flag (before the angle window), and a run of dropped
vertices holding one is put back only when shorter than PassesJoinGaps: that is how the containment's outline drops out of the passes and how a
loop that runs into the fill splits into open passes. The contact words travel with the interval ends: the union FUN_18005fd30 (a lower lo / higher
hi replaces the end with its contact, joined intervals give their hi contact), FUN_180130300's extensions keep the old contact, its point intervals
(" twist ") and FUN_18012cde0's are 0x80000000, its joins and FUN_18012cf20's carry the joined end's contact; FUN_180125b70 flags an end clipped to
the fibre window. Before every level FUN_180271070 (hmm-plans1 l.2411, from hz l.12944 / 14895 / 16626) re-fits +0x198..+0x1b0 to the weave's
free interior (fibres with two or more intervals: from the first interval's hi to the last one's lo, and their positions) grown by +0x2d8
(MachWeaveResolution) and the cull box +0x168..+0x180 by max(the tool's radius at the model top above the level, FUN_18013fff0; the weave profile's
first field). It is NOT only a speed-up: CZInflexions (FUN_1804a1b80, hz l.12956) takes that cull box as the XY box of the inflexion levels. The weave
then holds the template (the fill only): FUN_18040e430 copies Eva +0x1090 back after each level (FUN_18012c0f0 hz l.14423). Ported 2026-11-13
(gen_waterline ibx): the fill-only fibres' free box + MachWeaveResolution + the ball radius (corner + ToolThicknessZ; the shank / holder table of
FUN_18013fff0 is not modelled, as in the slice) is the inflexion box of ball / bull Waterline (before: the containment box, ours); flat tips keep the
containment box (no fibres in our slice). The oracle levels did not move (SWARF op 4, MOLD BOTTLE op 4, gen_waterline unchanged).
Port (2026-11-13): gen_waterline sets the fibre ranges (c3_weave_rng) and the fill (WvFill: the containment polygons after c3_tcb_offset, or the
Bounding Box Plan's box; r as above, 1.0 mm = 1 / 25.4 in) for ball / bull tools; wv_loops fills each fibre first, then adds the model's intervals
with the window flags, and carries the flags through the union, the consistency rounds and the border closing to each loop vertex.
- No flat-radius offset (Fo = VorProfileFlatRad + ToolThicknessXY - ToolThicknessZ = 0, e.g. a ball tool with equal stock): the passes are the weave's own
  loops and FUN_18040b6f0 drops their clipped vertices (wl_angle_keep with the flags) - the decoded rule, no containment clipping.
- With an offset (bull tools, or XY stock above Z stock): NOT PORTED - the flags reach the offset spans through McHmm's Voronoi offset (stage 3 of W3
  below); our passes are the Clipper-offset polygons, which carry no flags, so they are clipped to the containment by wl_inside as on master (the
  fill's outline lies BoundaryOutwardsOffsetRad - Fo outside the containment and drops out); when that distance is not positive (Flute contact off,
  or ToolThicknessXY - ToolThicknessZ >= 1 mm + MachWeaveResolution) the op is refused: "NOT PORTED: the boundary fill's clipped passes need the
  mchmm Voronoi offset (hst3d_port_plan.md 7 stage 3)". (A nearest-site transfer of the flags was tried and removed: a stand-in, user 2026-11-13.)
- A flat tip has no fibres in our slice (the projected triangles): clipped with wl_inside as before.
The grid's outline is added as a loop round the free loops for our union (McHmm nests the loops, FUN_18012b1e0); in the direct case it is all
clipped. Results (cutting moves, tools/tests/waterline_oracle.sh and the MOLD BOTTLE op 4 run of formats/oracle_results.md): TUTORIAL 2 SWARF op 4
(bull, wl_inside) ref->ours p95 0.087 / ours->ref 0.002 (max 0.524 -> 0.528), 404 vs 425; MOLD BOTTLE op 4 (ball, Fo = 0: the flags) 0.194 / 0.002
(max 0.003 -> 0.002), 541 vs 603. BRACKET4's default Waterline generates (above).
Open: McHmm's angle filter tests the weave loop's vertices (before the flat-radius offset), ours tests our polygon after the union and the
Clipper offset by the flat radius, whose straight sides have only their end points (TUTORIAL 2 SWARF op 4 at Z 0.405: the 2.4 in groove sides,
now in the loops, are kept; Mastercam keeps only the slot-end arcs - the source of the ours->ref max 0.524). The fix is to run the filter on the
wv_loops vertices and offset what stays (the Voronoi offset, W3).

**Cut length (2026-11-12):** the "about 10 % short" inch cut length is Mastercam's links: its feed moves off the levels (vertical lead arcs, helical
transitions, ramps) are 8.1 % of the cut length of TUTORIAL 2 SWARF op 4 (34.6 of 425 in), 10.0 % of MOLD BOTTLE op 4 (60.4 of 603), 7.2 % of TUTORIAL 2
SWARF_METRIC op 3 (735 of 10239 mm); ours are SAFE rapids. On the levels (measured with the unapproved cell polygons): op 4 ours 392 vs 390, MOLD BOTTLE 541 vs 543. The rest of the missing samples
at the levels are horizontal lead arcs (points out of contact, e.g. (2.45, 1.35) at Z 1.806 is 0.22 from the model with Rb 0.175) - the Waterline Linking
Plan 0x141c, NOT DECODED.

**Corner rounding (FUN_180557f60) - decode 2026-11-12, NOT PORTED** (hp = hmm_pass0.c, hv = hmm_vorlib.c):
- Inputs. FUN_180406b00 with kind 0 (hp l.1811-2050): MinOffset = RadExtra = 0; CornerTolerance 0x1bd0 when ProfileSmoothing 0x1bcf, else 0;
  FUN_180555f60(graph, specs, 0, 0, 0, 0.0, 0.0, 0, CornerTolerance) takes its t = 0 branch (hv l.15164-15182): one spec per boundary loop {loop, loop +0x48,
  t = 0, CornerTolerance}. Per spec FUN_180557f60(graph, Eva +0x4f0, spec, MinDeviation = Eva +0x1010, MaxRadius 0x1bd2, MinRadius = Eva +0x1018,
  MinArcChordLength = Eva +0x1008, 0x1f or 0x1e, flag) (hp l.2030 / 2048). Eva +0x1008 / +0x1010 / +0x1018 = ProfileSmoothingMinArcChordLength 0x1bd3 /
  MinDeviation 0x1bd4 / MinRadius 0x1c00, read by EvaZProfilePlan's constructor FUN_1803f9c20 (hz l.355-380). Defaults (FUN_180310790 hp l.960-1035, mm):
  MinRadius 0.0001, MinArcChordLength 0.00015, MinDeviation 0.001, MaxRadius "ToolCutterDiameterAdjusted * 0.05", CornerTolerance "... * 0.01" (mckernel
  writes MaxRadius 0x1278 and CornerTolerance max(0x1270, 2 x job +0xa0), wl 1).
- FUN_180557f60 (hv l.16253-16923): FUN_180554c70 lists the Voronoi edges of the loop's chain that cross clearance t; each gives an element (FUN_180556de0
  hv l.15717): centre = the edge's point at clearance t (FUN_180555130), radius 0, its two sites (edge +8 / +0x10) with the foot parameters (FUN_180553580).
  Edge kinds (FUN_180554f60): 1 / 2 / 3 / 4 / 5 -> types 1 / 3 / 2 / 4-5 (by side) / 6; type 2 = the bisector of two segments meeting at a convex vertex of
  the region (foot parameters 1 / 0).
  When CornerTolerance - t != 0: type 1 / 4 / 5 elements -> FUN_180557c80: radius = min(FUN_180556ec0 (a limit from the neighbouring elements' ends;
  -1 = none -> the edge's far radius), FUN_180557520 (the clearance at which the arc lies CornerTolerance off the corner)), then FUN_1805579c0 caps it at
  t + MaxRadius, moves the centre along the edge to it and puts the arc's ends on the two sites (FUN_180553720); type 2 elements: the arc grows to the edge's
  far Voronoi node (radius = its clearance) or, when that is more than MaxRadius above t, to the edge point at t + MaxRadius. Neighbouring elements whose
  arcs end at the same type-1 node at its clearance are merged into one arc about that node (FUN_180557d20), repeatedly. An arc is kept only when
  radius - t >= MinRadius, its deviation (the largest distance of the spanned crossings' points from the centre, minus the radius) >= MinDeviation and its
  chord >= MinArcChordLength; otherwise the element is replaced by the plain crossings again. The loop is then written as site spans and arcs
  (FUN_1801192a0 / FUN_1801194d0 / FUN_1801195f0; an arc of 180 degrees or more is written as a line, log "XoiDiagram::MakeCyclicRaceCurve : Write line
  rather than major arc").
- FUN_180557520's result (read in its disassembly, 180557520-1805579c0; the decompile drops it): with P the corner point, (t, tau) = the spec's t and
  CornerTolerance, B the edge's far node (+0x20: x, y, clearance rB): when |B - P| <= (rB - t) + (tau - t) it returns rB; otherwise, for a type-1 edge,
  the clearance r of the edge point C(s) (the clearance linear from the near node +0x18 to B) solving |C(s) - P| = (r(s) - t) + (tau - t) - the arc
  whose distance from the corner is tau; the other edge types (segment-point parabolas, 18055771c on) are not read yet.
- W3 (user 2026-11-13: port mchmm.dll's own Voronoi library, option (a)) - stage 1 decode STARTED 2026-11-13 (below), no code; stages, each committed when consistent:
  (1) the site input and construction FUN_1805475b0 (hv l.4736) / FUN_18054a440 (hv l.7069) with the Voronoi* tolerances (0x1bc3-0x1bc9, 0x1bce)
  from the wv_loops loops (FUN_180407c10 hz-zprofile l.180-258), its node / edge / site records (edge 0x28 at graph +0xe0, kinds via FUN_180554f60);
  (2) the copy with radii shifted FUN_18054ec00 (hv l.10416) and FUN_180555130 (point at a clearance); (3) the t = 0 boundary offset by the flat
  radius (replacing the Clipper offset of c3_slice for Waterline) and the angle filter on the wv_loops vertices with its keep flags carried to the
  spans (FUN_1801192a0 / FUN_1801194d0 flags); (4) FUN_180557f60's corner block (FUN_180557c80 / FUN_180557d20 / FUN_1805579c0, the disassembly of
  FUN_180557520 / FUN_180556ec0) and the keep test; (5) the TUTORIAL 2 SWARF_METRIC op 3 oracle (it also needs the check-surface plan 0x141f, S7).
- W3 stage 1, decode started 2026-11-13 (no code yet; where it stopped). The call from the level (hmm_zprofile.c FUN_180407c10 l.180-258):
  FUN_1805475b0(graph, 1.0 / VoronoiParallelVanishingDistanceReciprocal 0x1bc7, VoronoiSmallBisectorLength 0x1bc8, VoronoiSmallBisectorSplit 0x1bc9)
  constructs the empty graph; FUN_180409f40 turns the level's loops into the input polygons (0x48-byte records), FUN_180406660 checks them (log
  "AllPolygonsChecked failed" -> no graph); FUN_18054a440(graph, ..., polygons, VoronoiPerturbationTolerance 0x1bc6) builds it.
  Defaults (FUN_180310790 hp l.836-900, the value strings in the order of the FUN_18038b890 calls): 0x1bc3 VoronoiInputCurveTolerance
  "max(0.0001, (min(0.002, 0.25 * <0x1acc>) / 3.0))", 0x1bc4 VoronoiInputCurveAngleTolerance 85.0, 0x1bc5 VoronoiInputCurveMinSegmentLength
  0.001, 0x1bc6 VoronoiPerturbationTolerance 0.002, 0x1bc7 VoronoiParallelVanishingDistanceReciprocal "1.0 / 1000000.0", 0x1bc8
  VoronoiSmallBisectorLength 2e-5, 0x1bc9 VoronoiSmallBisectorSplit 2e-5 (mm; DDV_MinDistanceEx(%1, 0) each).
  FUN_18054a440 (hv l.7069): FUN_180549ff0(graph, ., polygons, 0) and, when it fails, up to 7 retries after FUN_1805476e0 (clear) with the
  perturbation +-min(|DAT_1807ba3f0[k] x tolerance|, FUN_18054e170(.) / 2.8284270763397217) (k = 0..6, the sign of the table entry; log "Perturb
  times %d"; 7 failures -> 0); then FUN_18054b290 and FUN_18054b150. FUN_180549ff0 (hv l.6893-7065): graph +0x98 = the perturbation, +0x18 = the
  polygon count; the input box -> graph +0x78 = 2 (w + h); per polygon FUN_180546680 (hv l.4110; the sites: segments and, at reflex vertices whose turn
  passes the 1 / param_5 test, point sites FUN_180546380 / FUN_180546490, normals at +0x98 / +0x13; vertex kinds from the input's flag bits
  0xf0000000), FUN_1805459d0 (one polygon) or FUN_18054c120 (several: "Failed link boundaries, perturb: "); the boundary elements into the list
  graph +0x68 (FUN_180547500); FUN_1805494d0 the incremental build ("Failed incrementally build, perturb: "); FUN_180549d10 (several polygons);
  FUN_1805478a0 ("Failed detect bottlenecks, perturb: "); FUN_180547f10. Next to read: DAT_1807ba3f0 (the 7 factors), FUN_18054e170,
  FUN_180546680 in full, FUN_18054c120, FUN_1805494d0 (the incremental insertion - the core, FUN_18054d4b0 708 lines / FUN_18054b480 612 lines
  are its largest callees by size), FUN_1805478a0, FUN_180547f10, then the node / edge / site records (edge 0x28 at graph +0xe0, kinds FUN_180554f60).
  Read 2026-11-13 (second pass): DAT_1807ba3f0 (mchmm.dll .rdata, 7 doubles) = -0.07568000257015228, 0.7567999958992004, -0.352400004863739,
  0.8971999883651733, -0.35923, 2.299999952316284, -1.887665033340454 (the perturbation factors of the 7 retries, x VoronoiPerturbationTolerance, sign
  kept); FUN_18054e170 (hv l.~9970-10415) returns a distance over the input polygons' points (a spatial sort FUN_180551950, then per candidate triple
  the exact-equality tests FUN_1800082c0 and FUN_18005ab30, sqrt of the smallest) - its exact quantity is not read yet; it caps the perturbation at
  that / 2 sqrt 2.
- **W3 stage 1 DONE 2026-11-13: the construction is ported (viewer/hmm_voronoi.c / .h, not yet called by any engine) and identical to the DLL record
  for record.** Oracle: tools/peloader `--hmmvor` (hmm_vor.c: mchmm.dll mapped with the real msvcr120 / msvcp120, mfc120u and the DLL's DllMain not run,
  tbb's allocator = calloc, the log stream stubbed; FUN_1805475b0 + FUN_18054a440 called on a points object + FGridC vector built as FUN_180407c10 does)
  and tools/tests/hmm_voronoi_oracle.sh (cases tools/tests/hmm_voronoi_cases.py: analytic shapes, random star polygons with islands, rectilinear grid
  blobs with holes - every grid vertex kept, as weave loops; dump tools/peloader/hmm_vor_dump.h): seeds 1-5, 395 cases, every vertex / site / node /
  edge / bottleneck identical in %a, including the perturbed retries (the grid cases mostly need the first one) and the islands' bridges. Self-test
  tools/tests/hmm_voronoi_test.c (square, rectangle, 17-gon, L, square ring: nodes on their sites, empty circles, the known medial-axis nodes): ALL PASS.
  What the construction is (decoded from hv; the record layouts are in hmm_voronoi.c's header comment): a wavefront build. FUN_180546680 turns each
  input loop into vertex records (against the input order) and sites (a segment per edge; at a reflex turn - sine below -1e-6 - the vertex is doubled
  with a point site between the copies; a straight-ish turn gets kind 8 with the mean normal; the FGridC vertex-kind bits 0x40000000 set the sites'
  +0x5c flag); FUN_180547070 perturbs every other vertex (index parity by the perturbation's sign) along its turned edge sum by |p| x a hash fraction
  (the hash reads the float encodings of two sums - Ghidra shows a conversion; disassembly 1805471fb). Several loops are first joined into one by
  bridges (FUN_18054c120: a direction from the widest gap of the sites' pseudo-angles FUN_18054fd50, each loop's extreme point FUN_180550600, a ray to
  the nearest site of the loops above it, the two sites split there FUN_180546b90, two kind-9 bridge vertices FUN_180546df0). Every vertex gets its
  bisector edge (FUN_180545ce0); FUN_1805487a0 seeds the event queue with the nodes of neighbouring vertex edges (FUN_18054d4b0, sorted by
  clearance FUN_180549430); FUN_180548c00 pops events: a live node N (sites A / B / C) starts the edge A-C (FUN_1805459f0), whose end is the first node
  met walking from A's start vertex up the left edges or from C's end vertex up the right edges (FUN_18054d4b0 with the side tests FUN_18054d1f0 /
  FUN_180548a00, the lower clearance of the two wins), new nodes retire the later nodes of their edges (FUN_1805477e0); the last node closes the loop.
  FUN_18054d4b0 dispatches the equidistant-point solvers by edge / site kind (disassembly 18054d69c-18054d8c5: FUN_180545eb0 edge line vs site,
  FUN_180550f40 parallel segments + third, FUN_180550790 segment + two arcs with the quadratic FUN_1802e67b0, FUN_180550cb0 three points) and keeps
  the node when its feet lie between the edges' end nodes (FUN_18054d0d0). Then FUN_180549d10 / FUN_1805496d0 remove the bridges (kind-4 nodes,
  merged sites FUN_180546f40, the relabelling walks), FUN_1805478a0 finds the bottlenecks (FUN_18054f480: the narrowest point inside a kind 2 / 4 / 5
  edge, VoronoiSmallBisectorSplit from its ends) and turns edges to rise in clearance (3 passes, VoronoiSmallBisectorLength), FUN_180547f10 splits the
  sites at each bottleneck (kind-10 then 8 vertices, two edges replacing the bottleneck edge, node kind 5), FUN_18054b290 orders each node's edges
  (+0x60 = the leaving one; kind 2 when none leaves) and marks kind-4 edges' side, FUN_18054b150 gives each site its first / last node. On failure
  FUN_18054a440 retries 7 times with the DAT_1807ba3f0 factors x VoronoiPerturbationTolerance, capped by FUN_18054e170 / 2 sqrt 2 = the smallest
  distance of an input vertex from an input edge it is not an end of (x-sorted scan, MSVC std::sort of the indices, started from the first two
  vertices' distance; FUN_18005ab30 point-segment distance). The input contract (FUN_180406660): the first loop clockwise in input order, the others
  counter-clockwise; the CCW square fails all 8 attempts in the DLL too. The DLL's asserts (FUN_1802e24f0 "voronoi\VoronoiFunctions.cpp" 0x375 in the
  walks) end the attempt in the port.
  Not ported yet (stage 1 is the construction only): FUN_180409f40's input from the level's loops and which vertex-kind bits FUN_180407c10 sets (the
  angle filter's keep flags, stage 3), FUN_18054b480 / FUN_18054a640 (callers not traced), the copy FUN_18054ec00.
- **W3 stage 2a DONE 2026-11-13** (hmm_voronoi.c hv_push_radius / hv_regions, oracle step `OFF r pushup limit`, identical on seeds 1-5 plus fixed
  radii at the critical clearances). What FUN_180407c10 does after the build (hz l.586-660): when VorProfileFlatRad (0x1bee, Eva +0xff8) != 0,
  r' = FUN_18054a640(graph, it, RadPushUp 0x1bca, RadPushUpLimit 0x1bcb) (log "Inflexion offset radius moved in from ... to ..." when moved), else
  r' = 0; n = FUN_18054b480(graph, r'); per region i < n FUN_180406b00(Eva, passes, graph, i, r', kind, ...) (kind 0 for Waterline / Area
  Clearance without Eva +0xff0). FUN_18054a640 (read in the disassembly 18054a640-18054b150; return xmm6): S = the sorted clearances of every
  bottleneck node (its edge is dead after FUN_180547f10, so the range branch for kind 2 / 5 edges does not occur), of kind 2 and kind 3 nodes; R =
  merged ranges (FUN_18005fb40, lower_bound by hi FUN_180060480) [start r, node r] of a kind-2 node's kind 2 / 5 edges not starting at a kind-5 node,
  and [end r, start r] of kind 1 / 4 edges falling in clearance; from d = r', alternately past every S value within pushup (d = s + pushup) and
  every R range within pushup (d = hi + pushup), until a round moves nothing; at r' + limit: R += S as points + [r', r'], the middle of the widest gap
  between successive ranges within [r', r' + limit] ("Find Best range gap for vroffset revised to"), else the same search up to max S + 1 +
  pushup ("Offset displaced to ... beyond ..."). FUN_18054b480: each unlabelled kind-2 node above r' opens a region; its loops are walked from any
  site above r' whose last node carries the label (site +0x58; a kind-5 node passes the label on); then every live edge rising above r' takes its
  left site's label (edge +0x58, its end node +0x84), and an edge starting at or below r' is its sites' crossing (left +0x50, right +0x48).
  NEXT (stage 2b): FUN_18054ec00 (hv1 l.613, read: the region's compact copy - site records 0x50 at copy +0x18 indexed by site +0x40, vertex
  nodes 0x78 at +0, top nodes kind 2 at +0x98, bottleneck nodes at +0xb0, kind-3 nodes at +0xc8, edges 0x28 at +0xe0, every clearance minus r';
  the shifted site geometry FUN_18054cd70, the edge kinds FUN_180554f60), then its finishing FUN_1805538a0 / FUN_1805540e0 / FUN_180554e80, then
  stage 3 (FUN_180406b00 kind 0: FUN_180555f60's boundary loop specs and FUN_180557f60 writing them through the pass writer at Eva +0x4f0,
  FUN_1801192a0 / FUN_1801194d0 / FUN_1801195f0). The oracle can call FUN_18054ec00 / FUN_180555f60 directly (no Eva); FUN_180557f60 needs a
  stand-in for the writer object.
- **W4 stage 2b DONE 2026-11-13: the region's compact copy** (hmm_voronoi.c hv_copy; the layout is in its comment): FUN_18054ec00 (hv1 l.617)
  numbers the region's sites (+0x30), edges (+0x48) and top / ordinary / bottleneck nodes (+0x80) in the graph, then fills the copy: per site a
  vertex node at the crossing of r' (FUN_18054cd70: the graph edge's point at a clearance; kinds 1 / 3 linear, 4 segment-point or two arcs by
  the edge side byte +0x5c - disassembly 18054cd70-18054d010 for the dropped arguments and the operand order) and the site moved in by r'
  (+0x10 = graph +0x28 + r'), the nodes with clearance - r' (graph kind 3 -> copy kind 1, 2 -> 2, bottlenecks -> 3, vertex nodes 4), the edges
  (FUN_180554f60: graph kind 1 / 2 / 3 / 5 -> 1 / 3 / 2 / 6, kind 4 -> 4 or 5 by the side of the end node; the start is the left site's vertex
  node when the graph start lies at or below r'). FUN_1805538a0 gives each site its node chain (+0x38 / +0x30), FUN_1805540e0 per loop of
  crossings the diameter (three farthest-point passes), its far vertex node (copy +0x30 vector, diameters at +0x48, the largest at +0x60) and
  the loop's lowest bottleneck or top node (FUN_1805539c0 "below": lower clearance, ties by bottleneck index - disassembly 1805539c0), the
  bottlenecks' parents / children and the clearance ranges (FUN_180553fd0, FUN_180553a70), FUN_180554e80 the sites' positions in their loops.
  Oracle (`OFF` runs now dump every region's copy, hmm_vor_dump.h hvd_copy): seeds 1-5, 373 copies / 38581 records identical to mchmm.dll (358 copies on the current cases, which add vertex-kind bits).
- **W4 stage 3 DONE 2026-11-13 (not wired): the offset loops without Corner rounding** (hv_offset_loop / hv_offset_region, HvWriter).
  FUN_180555f60 with t = 0 (hv l.15164-15182) only makes one spec {loop's far vertex node, its +0x48 node, t = 0, CornerTolerance} per loop;
  FUN_180406b00 (hp l.2000-2050, kind 0) calls FUN_180557f60(copy, writer Eva +0x4f0, spec, MinDeviation Eva +0x1010, MaxRadius 0x1bd2,
  MinRadius Eva +0x1018, MinArcChordLength Eva +0x1008, 0x1e for the loop with the largest diameter else 0x1f, param_9). FUN_180557f60 (hv
  l.16253): FUN_180554c70 / FUN_180554a20 list the loop's crossing edges (from the +0x48 node's edge on this loop, across the bottlenecks not
  above it; r8 = the vertex node, r9 = the node: disassembly 180554a4a), FUN_180556de0 makes one element per crossing (centre = the copy edge
  at t, FUN_180555130 - disassembly 1805551f0-180555490; radius 0; the two sites with their foot parameters FUN_180553580 / FUN_1805532e0 /
  FUN_180553430 / FUN_18005acc0 - disassembly 1805535d0, 18005acc0). With CornerTolerance == t the corner block is skipped; the keep test
  (radius >= MinRadius, deviation >= MinDeviation, chord >= MinArcChordLength) replaces every radius-0 element by itself. The writer
  (disassembly 180558600-180558cdc for the arguments): FUN_1801192a0(start = the last element's point, flag, kind), then per element a line
  FUN_1801194d0 to its point along a segment site, or an arc FUN_1801195f0(end, the point site's centre, site radius + t, 0, 0, flag) - a
  line when the arc spans 180 degrees or more (log "Write line rather than major arc"), then FUN_180121900. The flag is 0 when param_9 is 0 and
  the site's byte +0x44 (graph site +0x5c, from the FGridC vertex-kind bits 0x40000000) is set, else 1. Oracle: `VOFF` lines (the DLL's
  FUN_180555f60 + FUN_180557f60 with the writer's four methods patched to recorders; cases with random vertex-kind bits and param_9 0 / 1):
  seeds 1-5, 401 offset loops, 7767 writer calls (1470 arcs, 3149 with flag 0) identical to the DLL. The angle filter's keep flags (which
  FGridC bits FUN_180407c10 / FUN_180409f40 set from the weave loop vertices) are NOT traced yet.
- **W5 stage 4 DONE 2026-11-13 (not wired): Corner rounding** (hv_offset_loop's corner block, the element arcs and their flag split).
  All from the disassembly where Ghidra drops values (dis = objdump -d -M intel of mchmm.dll at the addresses): the corner block of
  FUN_180557f60 (dis 1805580e0-180558219) runs when CornerTolerance - t != 0: per element on a kind 1 / 4 / 5 edge FUN_180557c80 (dis
  180557c80: r = min(FUN_180556ec0(edge, next element's second point, previous element's first point, copy +0x78 = VoronoiPolyEpsilon) or the
  element's t when that is -1, FUN_180557520(edge, centre, {t, tau}, t)) then FUN_1805579c0); on a kind-2 edge the centre goes to the edge's
  end node, at most MaxRadius above t (MaxRadius -1 = no cap). Then the merge (dis 180558224-1805583ec): successive elements whose edges end
  at the same kind-1 node, both at its clearance, become FUN_180557d20's element (dis 180557d20: on the node's leaving edge +0x38 from its
  start; the next element's first side, the current one's second side; r = min of FUN_180556ec0 and FUN_180557520 over every non-kind-2
  crossing it spans, v = the edge's start clearance) inserted before the element after them; the walk resumes one element before it.
  FUN_1805579c0 (dis 1805579c0-180557c73): r capped at t + MaxRadius, never below the element's t; the centre the edge's end / start node or
  FUN_180555130 at that clearance; radius = clearance - t; the feet FUN_180553580, snapped to 0 / 1 where the centre is a node of a kind-2 edge
  of the site's vertex nodes, clamped to 0..1, the arc ends FUN_180553720 (dis 180553720: segment lerp(f) + t n; arc: the pseudo-angle sweep x
  f, FUN_18005bd20 = the direction of a pseudo-angle, radius site + t). FUN_180556ec0 (dis 180556ec0-18055750d, return xmm0): kinds 1 / 3 the
  end clearance unless p / q pass the end node's tangential coordinates by tol, then the clearance at min(u1, u2) along the edge; other kinds
  (unless both feet pass the end by tol: the end clearance) two arcs: the circle through the midpoint's normal reaching p and q (-1 when
  degenerate), arc + segment: the tangent circle through p / q; the result clamped to [start, end] clearance. FUN_180557520 (dis
  180557520-1805579bc): the end clearance when p is within (end clearance - t) + (tau - t) of the end node; kind 1 the quadratic along the
  edge; arc + segment the quadratic with W = tau - 2t (dis 1805777e1, literal), the larger root when both are >= 0; two arcs or no root: v when
  v == t, else the start clearance. The writer's element arc (dis 180558ce8-180558fe3): arc(p1, centre, radius, 1, 1, flag of the first site);
  with param_9 = 0 and the two sites' flags differing first arc(split, ..., second site's flag == 0), split = mchmm's arc library on the arc
  {centre, |p2 - c|, p2 -> p1, dir 1}: FUN_180052110 (dis 180052110-1800522b6: atan2 of the point's direction against the start, or + 2 pi
  whichever is nearer [0, sweep], / sweep; zero sweep 2 / 0.5 / -1) at the direction of the first spanned crossing whose sites carry the
  first / second flags, clamped to 0.1 .. 0.9 (0x1807bbc10), the point FUN_180051e00 (dis 180051e00: rotate the start by sweep x f with
  sin / cos), the sweep FUN_180051d20 (atan2, [0, 2 pi), 2 pi = 2 acos(-1): the static at 0x1809bf198 set at 180002300). libm: msvcr120's
  sin / cos / atan2 (imports 0x180746758 / 38, 0x1807465a8); hv_set_libm. Oracle mode `corner` (hmm_voronoi_cases.py: CornerTolerance a
  random fraction of the shape or 0, MaxRadius 0.5 / -1, VoronoiPolyEpsilon 2e-5 / 0 as VOFF's 7th field into copy +0x78): seeds 1-5, 470
  cases, 358 copies, 401 offset loops, 9060 writer calls (3336 arcs; 578 merges, 91 split arcs in the port's coverage) identical to mchmm.dll
  with msvcr120's libm (`peloader --hmmvor CASES --port OUT` runs the port inside peloader with those functions); the native glibc build
  differs in the last bit of 3 split points (sin / cos), counted apart by the oracle.
- **Wiring decode (W5, 2026-11-13, partial; not wired):**
  - FUN_180409f40 (hz l.10718) is only the copy of the TGridC records (0x40) into FGridC (0x48); the records come from FUN_18011cbc0(Eva +0x1440,
    points, records) (hz l.80, hst3d_weave_drop l.8091): per stored curve of the writer object at Eva +0x1440 (param_1 + 0x288 in hz's
    pointer arithmetic) FUN_180123330 (hw l.13138) appends its vertices: the curve's start, then each element's end, with the element's +0x14
    as the vertex-kind bits: 1 -> 0, -1 -> 0x40000000 (the site flag +0x5c / copy +0x44), anything else 0x20000000; arcs 0x4000000 /
    0x5000000 (+ the centre). FUN_180407c10 then drops the records with exactly 3 entries (hz l.158-178) before the build.
  - The writer at Eva +0x1440 is fed by the angle filter FUN_18040b6f0 itself (hz l.12268-12345): per weave loop of the level (Eva +0x1430 +0x240
    [Eva +0x1438]) FUN_180118ee0(start point, keep byte +0x18 of the start vertex, kind 0xf for the first loop else 0xe), FUN_1801194d0(next
    vertex, its keep byte) for each vertex except one whose following vertex lies exactly at the current one's point (a spike out and back;
    with Eva +0x1001, a vertex +0x14 == -1 and either keep byte set, FUN_18040af10 first - not read), FUN_180121900 at the end; writer +0x112 = !Eva
    +0x15c0, +0x113 = Eva +0x15c0 (Eva +0x15c0 = PosContactOnly with a tool holder, hz l.1235-1257). FUN_1801194d0 (hw l.5922) gives the line
    element +0x14 = v ? 1 : -1 with v = the new flag when +0x112 and the flags differ, else (+0x113 ? previous && new : previous || new).
    So a span gets the Voronoi site flag when it is not kept (both / either end clipped by the angle filter or the boundary fill).
  - param_9 for Waterline: FUN_180406b00's uVar22 (hp l.1998-2003) is 1 for kinds 2..4 or kind 1 with PosContactOnly 0x1ae1 off, else 0;
    Waterline runs kind 0 (stage 2a note above), so param_9 = 0: the not-kept sites' spans are written with flag 0.
  - r0 = Eva +0xff8 (param_1[0x1ff], VorProfileFlatRad) pushed by FUN_18054a640 only when != 0 (hz l.592-602; logged when moved), the regions
    FUN_18054b480 at r', FUN_180406b00(Eva, passes, graph, region, r', kind, Eva +0x286 [0x5b], param_3) per region (hz l.635-654).
  (W5 stopped at the output side; W6 below.)
- **W6 2026-11-13 (not wired): the pass writer CPipeThinToolpath and its flush into TGridC records** (viewer/hmm_pipethin.c; hw =
  decomp/hst3d_weave_drop.c, dis = objdump of mchmm.dll). The writer at Eva +0x4f0 (and +0x1440) is a CPipeThinToolpath (ctor
  FUN_180118410): +8 vector of curves (0x58: the 0x48-byte template + a std::list of composites), +0x20 the minimum arc chord, +0x28 /
  +0x70 the curve templates, +0xb8..+0xd0 the points header (+0xd0 = the level z, FUN_180406b00's param_7), +0xd8 the start kinds,
  +0xf8 the pen, +0x110 / +0x111 start / last flag, +0x112 / +0x113 the flag rule, +0x114 PosContactOnly, +0x170 flag changes, +0x178
  the near-full-arc flag. FUN_180406b00 (hz l.8943-8985) sets +0x20 = 0x3f33a92a30553261, +0xb8 = 1, +0xd0 = z, +0xf0 = -1, +0x168 = 0.01,
  +0x170 / +0x178 = 0, +0x112 = 1, +0x114 = PosContactOnly, the templates from Eva +0x4a8 (EvaMachLocBasePlan's ctor FUN_1802f2340, dis
  1802f2402-1802f278f): mode = ThinCurveTolerance 0x1ad0 != 0 ? (ThinArcEnable 0x1ad4 ? 2 : 1) : 0 (FUN_1802eaec0), ThinCurveTolerance,
  cos(ThinAngleTolerance 0x1ad3), ThinSegmentLengthMin 0x1ad2, -Max 0x1ad1, ThinArcMaxRadius 0x1ec7, StraightLineEnable 0x1c7f, cos / sin
  (StraightLineAngleTolerance 0x1c7e) (imports 0x180746738 = cos, 0x180746758 = sin); the +0x70 template's tolerance is 0x1e70 (kind 0x20
  only). **+0x113 does not matter here**: with +0x112 set a change of flag takes the new one and equal flags give it under both rules
  (FUN_1801194d0 dis 18011953b-18011956c; +0x113 = Eva +0x603 is set to 1 at 1803355c0 / 1803cb6ef), so a span's element flag is its
  writer call's flag (1 -> +1, 0 -> -1).
  - Writer calls: FUN_1801192a0 start (a new curve, its first composite kind 5 flag 1 FUN_180115e20, pen = p with z +0xd0; kind 0x1e
    records its index at +0xf0); FUN_1801194d0 line (nothing when p equals the pen exactly - FUN_18002d030, dis 18002d030: x, y, z ==;
    the flag counter +0x170 still counts); FUN_1801195f0 arc (dis 1801195f0-1801198c5: a line when the chord < +0x20; the pseudo-angle
    FUN_18004d7e0 (dis 18004d7e0) of (dot, cross) with the direction byte; >= 3.9 without byte 6 sets +0x178; <= 1.9 one arc element, else
    two through the point at radius r (-1: |p - c|) on the chord's perpendicular, z the ends' mean when they differ); FUN_18011a730 /
    FUN_180115fb0 the arc element (a line when either end is within sqrt(5e-5) of the centre in float arithmetic, dis 1801160bb-180116132;
    sweep FUN_180051d20); FUN_180121820 adds the element to the last composite (mode 0: it becomes the composite and an empty kind-0 one
    opens); FUN_180121900(curve, pen, +0x110, +0x111, +0x115) (dis 180558ff4-18055901f) drops the open composite, gives an empty curve a
    zero line, closes the last composite (one element: itself; more: the chord with the last flag) and appends a kind-5 marker unless the
    curve closes exactly (mode 0 also: more than one composite or param_5); mode != 0 with differing end flags FUN_180121eb0 only adds an
    attribute point (FUN_1801166d0, not read by the records).
  - FUN_18011ca00(writer, passes +0xa8, passes +0xe8) (dis 18040747e-180407496): +0x178 set -> message 0x4c4 and every curve dropped
    (FUN_180118930); the points header; FUN_18011d020 when the template mode != 0: per curve **FUN_180120e60 = a straight-line split**
    (not a span drop: a composite is split where a sub-element turns from the first one's direction - negative dot or dot^2 < |d|^2 |d0|^2
    sin^2 - or, with PosContactOnly, changes flag) then FUN_180124f40 (mode 1 FUN_180121f80, modes 0 / 2 FUN_180122380; then every
    composite's flag = its sub-elements' common one, 0 when mixed) with p4 = +0x170 > 0, p5 = +0x174 > 0 (dis 18011d048-18011d0cf);
    FUN_18011ae20 / FUN_180123330 (args dis 18011af8a-18011afd5) write per curve a TGridC (kind = the start kind) of the first
    composite's start and every composite's end (an arc first its centre, bits 0x4000000 / 0x5000000 by the direction byte; ends' bits
    flag 1 -> 0, -1 -> 0x40000000, else 0x20000000), closed when the last composite is not a marker (its last point popped, the count
    kept).
  - Mode 1 thinning (FUN_180121f80): FUN_18011e8f0 picks the split of a composite: none for one sub-element (an arc over 170 deg -
    FUN_1802e73a0(170.0) = 2 pi / 360 x 170 - halved by length), else the first flag change (p4), arcs isolated (mode 1), at
    ThinSegmentLengthMax by length (FUN_18011edd0, tolerance x 1e-4), at the farthest point from the chord beyond ThinCurveTolerance or the
    smallest cos below cos(ThinAngleTolerance) (FUN_18011f370 with FUN_180116e10 / FUN_180116fa0; StraightLineEnable rounds the parameter to
    the sub-element's end, FUN_1801208d0 the straight-line run test); the part after the split becomes a new composite processed in turn
    (FUN_18011e550; sub-element split FUN_180116b40 / FUN_180116770).
  - Oracle: `PIPE` lines (peloader --hmmvor runs the DLL's real writer, ctor FUN_180118410 set up as FUN_180406b00, and FUN_18011ca00;
    hvd_pipe dumps the records; mchmm's static initialisers 1800022d0 / 180002300 for pi / 2 pi now run in the harness - without them
    FUN_1802e73a0 halved every arc until the DLL crashed). hmm_voronoi_oracle.sh seeds 1-5: pipe0 (mode 0) 357 flushes / 401 records /
    11816 vertices (280 flagged, 3138 arc centres), pipe1 (mode 1) 357 / 401 / 10543 (266 flagged, 3140 arc centres) identical to the
    DLL; build / off / corner unchanged. Not covered by the cases: open curves, +0x178, duplicate points. Debug dumps: HMMVOR_CURVES /
    HMMVOR_STEP (DLL) and HPT_CURVES (port) print the composites after each thinning stage.
  - **NOT DECODED (W6 stopped here; mode 2 done in W7 below):** mode 2 (ThinArcEnable): FUN_180122380 with the arc fit FUN_18011f6a0 (hw l.10143: counts the
    lines / arcs / directions, FUN_18011fbc0, FUN_18011ff30 / FUN_1801202f0 the circle through the sub-elements, ThinArcMaxRadius, the
    0.97 test) and the arc-chord deviations FUN_1801172b0 / FUN_180117550 / FUN_180117780; the port refuses it. Which mode Waterline runs
    is not traced: mckernel writes ThinArcEnable = 1 only in the OptiRough branch (mk l.88396); the default expression of 0x1ad4 was not
    read (ThinCurveTolerance's default, hp l.1594-1609: ThinArcEnable ? min(0x1a9d, 0x1acc) : min(0.002, 0.25 x 0x1acc), not 0).
  - **Where the flagged spans are dropped is not in the writer chain**: the records leave FUN_180406b00 with the 0x40000000 vertex bits
    (an element's flag on its end vertex). A consumer that skips a span whose end vertex carries exactly 0x40000000 (when its param_4 is 0)
    is FUN_180249cc0 (hmm_plans1 l.4560-4600; callers 180240f18 / 1802410ac / 180241283 / 180241409 / 180288cbd / 180410517), which of
    these runs on the Waterline passes (the Waterline Linking Plan 0x141c) is NOT traced. The direct path's rule (wl_angle_keep: a
    vertex not kept is dropped with its spans) is master's; it is not the DLL's span rule (a span is cut when its end vertex is flagged).
  - Also open (not read this session): the fallback when AllPolygonsChecked / the build fails (hz l.263-586); FUN_18040af10 (hz l.11453,
    ~380 lines), called from FUN_18040b6f0 for a span with vertex +0x14 == -1 and a kept end when Eva +0x1001 = **WeaveSubdivision
    0x1bc2** (hz l.361-365; default for the Waterline plan 0x1436 "(VorProfileFlatRad < 1.5 * MachWeaveResolution)", hp l.811-822, else
    false) - the direct path lacks it too; whether wv_loops' loop order / start vertex equal FUN_18040b6f0's Eva +0x240 list.
- **W7 2026-11-13 (not wired): which thinning mode Waterline runs, the arc thinning (mode 2), the span drop rule** (hmm_pipethin.c; dis = objdump
  of mchmm.dll; Ghidra decompiles of the functions named below made from ~/mastercam-re/ghidra_hmm, not kept).
  - **Waterline runs template mode 2.** ThinArcEnable 0x1ad4 is registered per plan by FUN_1802f5dc0 (edx = the plan id): default "true"
    (0x180765b08) for the plans 0x1435 / **0x1436 (Waterline Passes)** / 0x143a / 0x1460 / 0x1478, else "false" (0x1807624f0) (dis
    1802f6cd2-1802f6d5b: r15 - 0x1435 in the bit set 0x80000000023, or 0x1478). mckernel writes ThinArcEnable only for OptiRough (1, mk
    l.88396) and in the 2D stock model macro FUN_18010921c (0, mk l.205600), not for Waterline; ThinCurveTolerance 0x1ad0 defaults to
    "ThinArcEnable ? min(Tolerance 0x1a9d, StepMachTolerance 0x1acc) : min(0.002, 0.25 x StepMachTolerance)" (hp l.1594-1609), not 0.
    So FUN_1802eaec0 gives mode 2 for Waterline's writer templates.
  - **Mode 2 ported, DLL-identical** (FUN_180124f40 -> FUN_180122380 for modes 0 / 2): per composite the split of FUN_18011e8f0; when there is
    one, the arc fit FUN_18011f6a0; when that succeeds FUN_18011e8f0 again on the arc: no split -> the composite stays an arc, else the
    composite is split at the **first** split (dis 180122437-180122497: the second result at [rbp+0x110] is only tested), the part after it a
    new composite processed in turn. FUN_18011f6a0 (hw l.10143): sub-elements without kind 3, planar (the min / max z of their ends equal,
    FUN_1800082c0 = exact ==), more than two lines or any arcs, all arcs of one direction; one arc alone is copied; else FUN_18011fbc0 (every
    end but the last ahead of the start and on the side of the first end off the start, cos < 1) and the centre: all arcs FUN_18011ff30 (dis
    18011ff30-1801202eb: the sub-arcs' mean centre's foot on the chord's perpendicular bisector, computed as ((b + d) - b)), else FUN_1801202f0
    (dis 1801202f0-1801208c2: per sub-element of non-zero length the circle FUN_180051610(start, its end, end, 0.3 = 0x1807bb598) - dis
    180051610-1800519fb, three points >= 0.3 apart, the turn at the middle point within cos(pi - 0.5 deg) .. cos(pi - 87.5 deg) (MSVCR120 cos),
    bisectors meeting by FUN_180054820 (dis 180054820-180054c18), diameter >= 0.3 - gives a radius range so the circle passes within
    ThinCurveTolerance of that end; the ranges intersected, empty -> no arc; centre at the middle radius on the first circle's turn side); the
    arc kept when dot(end - c, start - c) > -0.97 r^2, r <= ThinArcMaxRadius (when > 0) and its turn matches the sub-arcs'. An arc composite's
    deviations (FUN_18011f370): FUN_1801172b0 (ends by FUN_1800522c0 dis 1800522c0: the circle distance within the arc's parameter range, else
    the nearer arc end; the circle's distance at the centre's foot on the sub-chord) and FUN_180117550 (cos^2 against the arc's ends, 0 when
    degenerate); a line composite's arc sub-element FUN_180116e10 -> FUN_180117780 (the arc points on the chord's normal, FUN_180058620 dis
    180058620; parameters within 3e-5 of an end snap to it). Oracle `pipe2` (hmm_voronoi_oracle.sh, now in the default modes) seeds 1-5:
    358 flushes / 401 records / 9927 vertices (222 flagged, 2683 arc centres) identical to the DLL (msvcr120 libm; the glibc build differs in
    64 lines through cos / atan2); coverage (hvcli with HPT_STATS=1): 1186 arc fits, 1313 empty radius ranges, 245 side refusals, 163 mean
    centres, 203 arcs refused by the 0.97 / radius / turn test, 1 single-arc copy. pipe0 / pipe1 / build / off / corner unchanged, identical.
  - **The span drop rule (decoded; the Waterline consumer itself NOT TRACED).** Every reader of TGridC records that drops spans uses one rule: the
    span from vertex i-1 to vertex i (an arc: from the vertex before its centre to the vertex after it) is skipped when vertex i's bits &
    0xfffffffff0000000 == 0x40000000 (exactly the -1 element flag; 0x20000000 "mixed" spans are kept). Found in: FUN_180249cc0 (hmm_plans1
    l.4560-4600) and FUN_180017470 (dis 18001751f-180017555), both only when their param_4 == 0 - their callers are the ToolBoxSet builders
    FUN_180240dc0 (param_4 = ToolBoxSet +0x129, from FUN_18023b780) and FUN_180252910 (+0x129 = its param_3: 0 from FUN_1803343e0 - the
    constant stepover / area clearance plan - and FUN_1803520f0, 1 from 180373bff / 18039f1a0), FUN_180288940 / FUN_18040fb90 (param_4 = 1);
    FUN_180318bf0 (always: the passes drawn into the CWeaveConcurrent Eva +0x1260 by FUN_180412ea0 hz l.16054); FUN_18047d1f0 (always; the
    next kept span starts a new piece) and its relatives FUN_18047d5a0 / FUN_18047da30, the CurveTrimmer FUN_180467420 / FUN_1804652e0
    (TrimGridCToBUsingCyclidOrderedGaps) and about 25 templated ToolBoxSet fillers 180243800-18024bfe0. FUN_18040b6f0 calls FUN_1801194d0 with
    the writer at Eva +0x1440 set +0x112 = !Eva +0x15c0, +0x113 = Eva +0x15c0 (hz l.12275-12277), and **Eva +0x15c0 = !WeaveSubdivision &&
    PosContactOnly 0x1ae1 && ToolHolderDefinition 0x1b38 not empty** (hz l.1232-1250; mckernel writes ToolHolderDefinition in FUN_180079d34
    mk l.90741-90770: FUN_180079bfc when its param_4 and FUN_18006f480, or the tool type 0x84 / byte-6 branch - which of these hold for an op is
    NOT DECODED). So: without a holder definition a span's flag is its END vertex's keep byte (a non-kept vertex removes only the span ending
    at it; the span leaving it is kept); with one (Flute contact on, no WeaveSubdivision) a span is kept only when both its ends are kept -
    master's direct path rule (wl_angle_keep + runs of kept vertices). The direction for the end-vertex rule is the node ring's next direction
    (FUN_18040b6f0 follows node +0x10 from the Eva +0x240 entry's node, hz l.12279-12341; the last span closes to the start node; a vertex
    whose next's next lies exactly at the current point is skipped), which wv_loops follows (cam3d_rough.c: nd[p].next). **The direct path is
    unchanged**: the holder condition of Eva +0x15c0 is not decoded for our ops, and the Waterline output's own consumer (the Waterline Linking
    Plan 0x141c's pass reader) is not identified.
  - Item 3 (partial): wv_loops' direction = McHmm's (above); the start node: McHmm's is the high 32 bits of the Eva +0x240 entry made by
    FUN_18012b1e0 (wd l.18363: it takes nodes whose interval end slot +0x38 / +0x3c is -1, in node order - not read to the end), ours the
    lowest unvisited node index: NOT verified equal. The fallback (hz l.263-586) and FUN_18040af10 (WeaveSubdivision) were not read.
  - **W7 stopped here.** Nothing is wired: gen_waterline keeps both refusals (the chain is not decoded end to end: the Waterline consumer of
    the flagged spans, Eva +0x15c0's holder condition, the fallback, FUN_18040af10, the start node).
- (W7 state, superseded by W8 below) Missing for a port: the span drop's Waterline consumer and Eva +0x15c0, then the wiring.
- **W8 2026-11-14 (N13-35): WIRED - bull-tool Waterline runs mchmm's own chain** (gen_waterline, cam3d_finish.c wlvor_pocket; branch
  worktree-agent-a722b6e517f8bf72d, which merges W7 = worktree-agent-a82f3c31073064da1, the weave contact = worktree-agent-aa8e60cca3ef18e5b and
  master fb90997). hz = decomp/hmm_zplan.c, hzp = decomp/hmm_zprofile.c, hw / wd = decomp/hst3d_weave_drop.c, dis = objdump of mchmm.dll.
  - **The pockets** (FUN_18012b1e0 wd l.18359-18719, ported in cam3d_rough.c wv_loops -> WvPockets): Eva +0x1430 +0x240 is a vector of pockets
    (FUN_1803faee0 runs the filter + FUN_180407c10 once per pocket, Eva +0x1438 = the pocket index, not the level). Phase 1: in node order, a node
    whose interval end slot (+0x38 lo / +0x3c hi) is still -1 starts loop L = {start node, pocket -1} and its ring gets L in every end slot (so a
    loop starts at its lowest node index - what wv_loops already did). Phase 2: per loop L in order whose pocket is -1, the fibre of L's start
    node: per gap between intervals j - 1 and j, A = the loop of interval j - 1's hi end, B = the loop of interval j's lo end; A without a pocket ->
    a new pocket [A, B] (B only when != A), pocket(A) = pocket(B) = A; else B without one -> B appended to the pocket whose first loop is
    pocket(A) (searched in all pockets but the last, none -> the last). Each entry = (loop, start node); FUN_18040b6f0 writes from the start node.
    The grid outline loop of c3_slice (ours, for the union) is not a weave loop and takes no part.
  - **The filter's writer** (FUN_18040b6f0 hz l.12262-12345): FUN_180118870 (hw l.5350) installs the template FUN_1803faee0 built (hz l.960-1000:
    mode 1, VoronoiInputCurveTolerance 0x1bc3, cos(VoronoiInputCurveAngleTolerance 0x1bc4 = 85 deg), VoronoiInputCurveMinSegmentLength 0x1bc5,
    0, -1, no straight lines, cos / sin(0.0001)), +0xb8 = 0, z 0, +0x112 = !Eva +0x15c0, +0x113 = Eva +0x15c0, +0x114 = 1 (Eva +0x1554); per loop
    FUN_180118ee0(start node, its keep byte, 0xf for the pocket's first loop else 0xe, 0) (= FUN_1801192a0 without the 0x1e index) and
    FUN_1801194d0(next node, its keep byte) for every node except one whose next node lies exactly on the current point (FUN_1800082c0 on x and y;
    a two-node ring is always written), FUN_180121900 at the end. FUN_180407c10 flushes it with FUN_18011cbc0(.., 1) (hzp l.135, dis 180407cd1:
    r9b = 1 -> FUN_18011d160 -> the thinning FUN_18011d020, then FUN_180123330 per curve - hpt_flush; no +0x178 test, which lines never set), drops
    the records after the first with exactly 3 entries (hzp l.153-172), copies them (FUN_180409f40) and checks them (FUN_180406660: polygon 0
    clockwise, the others counter-clockwise, by FUN_18005c800 - the port reads it as the area sign).
  - **The offset** (hzp l.586-680): r' = FUN_18054a640(Eva +0xff8 = VorProfileFlatRad, RadPushUp 0x1bca, RadPushUpLimit 0x1bcb) when Eva +0xff8 !=
    0, FUN_18054b480 regions, per region FUN_180406b00 kind 0 (the copy, copy +0x78 = VoronoiPolyEpsilon 0x1bce, FUN_180555f60 + FUN_180557f60 per
    loop with param_9 = 0 into the writer Eva +0x4f0 (template Eva +0x4a8, mode 2), FUN_18011ca00) - hv_* / hpt_* as W3-W7.
  - **The passes**: each record of FUN_18011ca00 is a pass loop (closed) with its vertex bits; a span whose end vertex carries exactly 0x40000000 is
    not cut (the W7 rule; an arc's span is judged by the vertex after its centre). A closed record with every span kept is one closed pass starting
    at the record's first point; otherwise the runs of kept spans, each an open pass (the record is its toolpath item, hst_linker.md 28.2).
  - **Traced on TUTORIAL 2 SWARF op 4** (peloader --hmm, HMM_TRACE `18040b6f0:W` - its new "eva" line - and `180557f60:X:s5:o78:O`): Eva +0xff8 =
    3.175 mm, +0x1001 (WeaveSubdivision) 0, **+0x15c0 = 0** (so a span's flag is its end vertex's keep byte: +0x112 = 1), +0x1008 / +0x1010 /
    +0x1018 = 0.00015 / 0.001 / 0.0001, Eva +0x4a8 = {2, 0.0127 (= StepMachTolerance), cos 45 deg, 0.0005, -1, -1, 1, cos / sin 0.1 deg};
    FUN_180557f60: MaxRadius 0.635 (= 0.05 D: "ToolCutterDiameterAdjusted * 0.05" with ProfileSmoothing off), MinRadius 0.0001, MinArcChordLength
    0.00015, MinDeviation 0.001 (xmm3), kind 0x1e, param_9 0, copy +0x78 = 2e-5, spec t = tau = 0. Every pocket of op 4 has one loop.
  - **Checked against the DLL** (MCAM_HST_MESH=1, the port at the DLL's levels - MCAM_WL_LEVELS from the passes' Z; cmp_passes: every element
    end point of the DLL's passes plan output, tag 1, matched to the port's pass vertices, and the port's vertices measured to the DLL's path):
    SWARF op 4 **65 / 65 passes, 2705 / 2705 end points equal (max 1.4e-10 mm), the port's vertices on the DLL's path (max 7e-12 mm), every
    pass's start, end and direction equal** (the closed ones too, after the start rule above). Not equal: the order of the open pieces of one
    record (e.g. z 9.065: the DLL gives (4 -> 5) before (1 -> 3) on one record and (1 -> 2) before (3 -> 4) on another) - the consumer that orders
    them is NOT DECODED; the records' order and each piece are the DLL's. TUTORIAL 2 SWARF_METRIC op 3 as stored (Corner rounding on, the DLL
    macro without its check plans): 28 / 28 passes, 27 vertex-identical; the lowest level's pass has DLL splits after FUN_18011ca00 that the port
    lacks (hst_linker.md 32.1) - the offset writer's calls are the DLL's on every level. Measurements: hst_linker.md 32.2.
  - Refused (NOT DECODED): AllPolygonsChecked failing or the construction failing - FUN_180407c10's fallback (hzp l.253-586), and with it
    FUN_1803faee0's retry with a larger tolerance (dis 1803fb39b: tolerance += its first value while <= StepMachTolerance) [both PORTED in W9 below]; WeaveSubdivision
    0x1bc2 (default "VorProfileFlatRad < 1.5 MachWeaveResolution": FUN_18040af10, hz l.11453, not read); K+0x472 (record
    0x16d7: mckernel's FUN_180079bfc may write ToolHolderDefinition, which sets Eva +0x15c0 when PosContactOnly and no WeaveSubdivision); XY stock
    other than the Z stock (Eva +0xff8 = VorProfileFlatRad vs our Fo: the term ToolThicknessXY - ToolThicknessZ in McHmm's offset is not traced).
    Not modelled: mckernel's ToolHolderDefinition for a tool of type 0x84 with byte 6 (mk l.90741-90770; no corpus op met). Rings of one or two
    nodes keep their clipped test only (not seen on the traced ops).
  - Corner rounding (ProfileSmoothing) now runs on this path (CornerTolerance = max(record 0x1270, 2 StepMachTolerance), MaxRadius = record 0x1278,
    mk l.87279-87283); without a flat-radius offset (ball tools) it stays refused: the DLL runs the same chain with r' = 0 there (and thins / arc-fits
    the passes), which the port's direct path does not (ours, unchanged).
  - Testing switches: MCAM_WL_PASSDUMP=FILE (the passes as made, mm), MCAM_WL_VORDUMP (the pockets' loops with keep bytes, the records with span
    flags), MCAM_WL_CLIPPER (the old Clipper offset, for comparison). Scratch tools: cmp_passes.py / dllcmp.sh (session scratchpad vor/).
- **W9 2026-11-14 (N13-37): FUN_180407c10's fallback DECODED + PORTED, checked against mchmm.dll; a correction to N13-35b / N13-37** (cam3d_finish.c
  wlvor_try / wlvor_pocket, hmm_pipethin.c hpt_records / hpt_fix_isect; hzp = decomp/hmm_zprofile.c, hz = decomp/hmm_zplan.c, hw = decomp/hst3d_weave_drop.c,
  dis = objdump of mchmm.dll).
  - **The retry** (FUN_1803faee0 hz l.960-1090): per pocket, FUN_18040b6f0 writes the loops into the writer at Eva +0x1440 with the template's +8 = the
    tolerance (first VoronoiInputCurveTolerance 0x1bc3), then FUN_180407c10; while that returns 0 ("DoPockets : Trapped Voronoi failure with tolerance")
    the tolerance grows by its first value (dis 1803fb39b-1803fb3af: addsd xmm6, xmm7) and the pocket is written again, until it exceeds
    StepMachTolerance 0x1acc (then message 0x4c3 with the level, and the last try's state stays).
  - **FUN_180407c10 when AllPolygonsChecked (FUN_180406660) or the construction FUN_18054a440 fails** (hzp l.263-590): FGridC views of the records
    after the first (local_960[1..], after the 3-entry drop), a ToolBoxSetRaw of them (FUN_180231f50, vftable 0x180775648; the points header at +0x430,
    +0x108 = [0x1807bba88], +0x126 = 0x1000100, +0x118 = 0: dis 1804084b4-180408574; FUN_180017470(.., 1) per record: every span, the closing one
    included since FUN_18004f610 maps a closed record's last vertex to its first, dis 18004f626-18004f638), then **FUN_18011ce60(writer, toolbox)**
    (hw l.8201): the sum of FUN_180123be0 over every curve plus FUN_180123ea0(curve 0, toolbox); true when > 0.
    - FUN_180123be0 (hw l.13471): while FUN_180122a00 finds crossing chords in the curve (hw l.12701: c from the second composite, b before c's
      predecessor, the pair last / first left out; FUN_180122710 = the DLL's own box test - not a plain overlap test, dis 180122861-18012288b - then
      FUN_1800566e0: b's ends strictly on different sides of c's line (dis 1800566e0-180056867), the crossing projected onto c within [0, 1]; the first c
      with a crossing is the candidate unless c's successor crosses the same b (return 2, P = c's end), a second b for the candidate returns 2 with P =
      b's start, else 1): 1 -> FUN_180123060 (counted, split or not), 2 -> FUN_180122bc0 (counted when the split moves the chord across P, hw
      l.13001-13005); no split ends it ("Failed to remove intersection. Could not split.").
    - The split (FUN_180123060 / FUN_180122bc0): FUN_18011e8f0 with the template {1, ctol 0, cos(DAT_1809c0558 x 180) = -1, 0, 0, -1.0, StraightLineEnable
      1, cos / sin 0.0001} - the farthest sub-element point from the chord when any lies off it - the sub-element cut there (FUN_180116b40 /
      FUN_180116770), the rest a new composite (FUN_18011df10 + FUN_18011e550) - the port's thinning split (hmm_pipethin.c comp_split_fix).
    - FUN_180123ea0 (hw l.13614): per composite of curve 0 a MeasureRadSegs2D (vftable 0x1807759d0; r = 0.0001, the chord's box grown by 0.00011, dis
      180123f06-1801240de) over the toolbox; FUN_18028a970 per line element (dis 18028a970-18028ab5d) records the crossing FUN_180057040 (dis
      180057040-1800571fa) and the within-r range FUN_180055b50; a composite whose chord has a crossing inside a range is split (FUN_180123060), repeated
      while any split happened. Coded as "a crossing at 0 <= v <= 1" (it lies on its element, so inside that element's range by r / |chord|; the
      ranges are clamped to [0, 1]); FUN_180055b50 itself not ported.
    - True: the records again (FUN_18011cbc0(.., 1): the thinning runs again on the split curves; every record kept, no orientation check) and a second
      construction; it fails -> "GenerateVoronoi failed second level", marks (Eva +0x1430 +0x278 = 1, the pocket's +0x280 bit set, its +0x258 bit
      cleared, its passes an empty set) and FUN_180407c10 returns 0 (a retry). False: the pocket's +0x258 bit set and FUN_180407c10 returns !bVar32 - 1
      after AllPolygonsChecked failed (no retry), 0 after the construction failed (a retry).
  - **Checked against the DLL** (tools/tests/hmm_vorfix_oracle.sh, new: `peloader --hmmvor` FIX cases run the DLL's own filter writer - ctor
    FUN_180118410, FUN_180118870, FUN_180118ee0 / FUN_1801194d0 / FUN_180121900 with the port's calls (MCAM_WL_FIXCASE) - then FUN_18011cbc0, the
    ToolBoxSetRaw + FUN_180017470, FUN_18011ce60 and FUN_18011cbc0 again; tbb.dll mapped for the toolbox's concurrent_vector): gen_waterline's 27 fallback
    tries (Z -0.975 and -6.975, every tolerance) **byte-identical** (first records, FUN_18011ce60's result 27 / 27 true, second records); its 100
    constructions (MCAM_WL_VORCASE, the second-level ones included; 46 built, 54 failed) identical with their offsets. Not covered by these cases: a
    split by FUN_180123ea0 (curve 0 against the other polygons) was not counted separately.
  - **Still refused**: a pocket that fails at every tolerance (or is killed): the DLL leaves it without passes, but its marks steer FUN_1803fc9b0 (hz
    l.1881-2240: "part N killed", "Removing over offset pocket", "Spawning weave into N parts", the +0x278 branch with FUN_180400fc0), the per-pocket
    weave bookkeeping that decides which weaves the next levels slice - NOT DECODED.
  - **Correction (N13-35b / N13-37 assumed "the DLL fails there too")**: mchmm.dll run on gen_waterline's own dump (peloader --hmm, MCAM_HMM_DUMP of the
    scene; the drive is the port's OCCT mesh in both) never fails a construction (16 levels, every FUN_180407c10 returns 1, FUN_18011ce60 never called)
    and makes 33 passes; the failure came from the port's polygon, which mchmm's construction also rejects (--hmmvor). The port's pockets differ from
    the DLL's from the second Voronoi level on (HMM_TRACE 18040b6f0:W vs MCAM_WL_VORDUMP, weave Z = level - ToolThicknessZ): at the pass level -0.975 the
    DLL has [[932, 600], [90]] (loops per pocket, vertex counts), the port [[932, 608], [54], [90], [54], [54], [4]]; the three 54-vertex pockets that
    both have at 0.025 are gone in the DLL below it (as at every lower level), and the DLL's inner loop near (10.1 .. 10.6, 2.9 .. 5.2) mm differs from
    the port's (a run along the fibre x = 10.114553 with clipped words 0x80000000 in the port). This matches FUN_1803fc9b0 dropping the pockets that gave
    no passes ("part N killed") from the weaves of the next levels - the port slices every level from the whole model (ours) - NOT DECODED, the next item
    for gen_waterline. With the fallback the port now passes Z -0.975 (success at the 10th tolerance, 0.00667 mm) and stops at Z -6.975 (18 tries).
  - Testing switches: MCAM_WL_FIXCASE=FILE (the FIX cases), MCAM_WL_FIBDUMP="S,A,B" (fibres A..B of a set after the consistency rounds; peloader
    :W with HMM_WFIB="S,A,B" prints the DLL's), MCAM_3D_STL=FILE (any 3D op's drive from an STL, in the app as cam_cli --stl); MCAM_WL_VORDUMP also
    prints each try ("wlvor try z tol rc").

## 8. Raster (UI style 5, kernel style 4) - decode and port (2026-10-25)
Decompiles of the grid-plan range (18035e000-180384000) and the tool box / drop: decomp/raster/ (grid.c "G", tb*.c, gdef.c; gitignored).
- Parameters (FUN_180071314 style-4 branch mk l.88074-88155): MachStepover = TP+0x1b8 (record 0x1230; <= 1e-12 2.5 / 0.1), RasterAngle = deg(TP+0x268)
  + 180 (record 0x12f0 radians), PassExtension TP+0x270 (name not in the McHmm registry: NOT DETERMINED), RasterStepoverMode TP+0x467 (Perpendicular
  fill 0x16cc), the cutoff 1.4 x stepover; contact angles / PosZ / stock (XY = max) / StepMachTolerance (0.5 x total tolerance) as Waterline; style 4
  in the mask 0x5b6 (PosZLo - corner radius).
- Lines (FUN_18037ca90 G l.20060-20397): u1 = (cos A, sin A), u2 = (sin A, -cos A); [min1, max1] / [min2, max2] of the measured objects (which: NOT
  DETERMINED); line from min1 - (e + 1) to max1 + e + 1; q = range2 / step, k + 1 lines at min2 + i step + (q - k) step / 2, i ascending, A -> B.
  Trim to boundaries / extend / JoinGaps (G l.1164 / 3833 / 920); the drop along a line FUN_1803612d0 (CToolDropPos, step / refine rules - the
  per-triangle contact maths, the sag formula and the step defaults NOT DETERMINED); a post-filter splits passes (FUN_180361bd0, its parameters NOT
  DETERMINED).
- Chain: Raster [+ Raster 2 = A + 90 deg, mode 2, with Perpendicular fill] by default; rest / check surfaces add Edit Passes / Convert.
- Linking (FUN_180075b48 mk l.89806-89963): Zigzag 0x16bd -> LinkDirectionStyle 1 (bidirectional); Down / Up Mill 0x16d4 1 / 2 -> 2 / 3; else 0
  with LinkReverseDirection = Other Way 0x16d5. The Raster Linker 0x142b is NOT DECODED.
- Page 1330: formats/opdlg_fields.tsv s1330 (cut method combo 0 One Way, 1 Other Way, 2 Zigzag, 3 Down Mill, 4 Up Mill).
**Port** (cam3d_finish.c gen_raster): the line layout, the directions, Use Z depths (2026-11-08 above); refused: Down / Up Mill, Perpendicular fill, the approximate start point,
angle limits other than 0..90. Ours: the measured objects (containment, else the drive box), e = 0, the tool positions (the height
field drop of cam3d_srough.c), off the drive its lowest Z, the containment trimming, the SAFE links.
**Use Z depths and the drive's edge (2026-11-08, cam3d_finish.c gen_raster)**: with no containment chain the constraint is the Bounding Box Plan 0x1425 of the surfaces with OffsetDistance |0x13c0| + FUN_18006fee0 / 2 + 2 x 0x12a0 (mk l.92848-92875; the styles not in the mask 0x209; FUN_18006fee0 = D for an untapered tool with Flute contact: FUN_1800969b4 mk l.111092; with Tool assembly contact, 0x16cf = 0, it takes the max with the shank / arbor diameters, not ported) - Waterline and Raster now use it; Scallop did. Use Z depths = PosZHi / PosZLo (mk l.87321-87344). EnableTrimToZRange 0x1b60 is NOT the mechanism (default false, FUN_180303880 / FUN_180303a20, not set by the Raster macro). The help (Using_Z_limits: the tool "continues at the maximum Z depth and falls no further"; Using_contact_areas: Flute contact cuts only where the tool position would contact) + the stored op (the lines end at the drive's edge, X -0.5 / 4.25, Y 0.5 / -8.25, Z >= 0) give the rule ported: a position below PosZLo (or above PosZHi) stops at the limit out of contact, and is removed when Flute contact (0x16cf) is on. Off the model the height field has no surface under Flute contact (sr_hf_build_fill). CToolDropPos' own handling of the limits NOT DECODED; the links (the 0.0106 loops beyond the edge, the arc entries) remain the SAFE rule.

**Oracle** (tools/tests/finish_oracle.sh "RA", since 2026-11-08 with the stored Use Z depths): PARALLELSTEEP.mcam (2017) op 5 (D 0.25 ball, 45 deg, 0.02 in, zigzag): ref->ours p50 0.007 / p95 0.029 in (every Mastercam pass reproduced), ours->ref p95 0.101 (was 3.458 before the Z limits; the rest: steep walls, Z noise of our height-field drop); XY feed length 2066 vs 2100 (3D 3128 vs 2543 true, our drop's Z steps).

## 9. Area Roughing (UI style 0, kernel style 0) - decode and port (2026-10-25)
**Decode** (mk = decomp/mckernel_all.c, hp = decomp/hmm_pass0.c, hz = decomp/hmm_zplan.c; McHmm functions not in decomp/ were decompiled with
`tools/ghidra_scripts/DecompAt.java` on the ghidra_hmm project, program mchmm.dll):
1. **Plan choice** (FUN_18007d7c8 mk l.93390-93432): FUN_18006fb24 (style 0, TP+0x463 = 0, TP+0x486 != 0) -> Core Roughing Passes 0x1460;
   FUN_18006fb04 (TP+0x486 = 0) -> Area Clearance Passes 0x1435; TP+0x463 set -> ekPlan4a with the same choice, + Horizontal Area 0x143a when
   TP+0x475 = 2. TP+0x486 = record 0x16ef ("Core", as OptiRough, 6.6). Every stored Area Roughing op of the corpus is Core (TUTORIAL 2 SWARF /
   _METRIC op 1, MOLD BOTTLE op 1, WATERLINE.mcam op 2); the defaults' first style-0 op (Mill_MM op 27 / Mill_Inch op 24) is Area Clearance.
2. **Both evaluators are Area Clearance**: EvaCoreRoughingPassesPlan (vftable 1807a18e8, ctor FUN_1800c7b80) and EvaAreaClearancePlan (ctor
   FUN_18040fa80) set Eva +0xff0 = 1 and share Evaluate FUN_18040eaf0 -> FUN_1803fabf0 -> FUN_1803fae40 with pass kind 2 (hz l.880): the
   passes of a level are the Area Clearance positions of formats/area_mill.md 11.3. (So "Core Roughing passes NOT DECODED", area_mill.md 11.7
   item 1, reduces to the region / trim below.)
3. **Inputs** (FUN_180071314 style 0, mk l.87696-87820): Stepdown 0x1228; MinOffset / MaxOffset = min / max(TP+0x230, TP+0x238) = record 0x12b8 /
   0x12c0 (page 1326 "Minimum" / "Maximum", ui_fields_mcmsurf.tsv - the block shift here is 0x1088); PosZHi + half the tolerance when PosZHi -
   PosZLo is below the system tolerance; AdaptiveStepdown (Add cuts 0x16c2); ProfileSmoothing (Corner rounding 0x16c3): CornerTolerance =
   max(TP+0x1f0 = 0x1270, 2 tol), MaxRadius TP+0x1f8 = 0x1278, **CornerOffsetTolerance = max(TP+0x240 = 0x12c8, 2 tol)** (mk l.87382-87389);
   RadPushUp 0.0002, RadPushUpLimit 0.002. StepDownEvenLastSteps: the plan default 5 (FUN_18030b160 hp l.596-601, no style-0 override).
   Stock: the non-Horizontal branch, ToolThicknessXY = max(XY, Z) (mk l.87571-87596). Cut method: page 1326 control 1239 Climb / Conventional
   = record 0x16e4 (MapToDlg FUN_180087460 decomp/mcmsurf_all.c l.88380-88390).
4. **Core** (ar 3): FUN_180071120 (mk l.86896) - with Core on, d4 = D/2 - MaxOffset (when > eps), c = max(0, Max - (D/2 - Rc)) - max(0, Max -
   ((D + 2 sxy)/2 - (Rc + sz))); the constraint boundaries get OffsetDistance max(d4, c) outward (ekPlanTcb3x OffsetDirection 1 / the Bounding
   Box Plan, mk l.92851-92990); TUTORIAL 2 METRIC: 0.6. CoreRoughingTrimOffset default `max(0.0, (MaxOffset - ToolFlatRadius))` (FUN_1803132a0
   hp l.1497-1508; string pieces McHmm 180784900 " - ", 180784a84 "))") - 2.4 there; FUN_1802ef450 trims to CoreRoughingTrimBoundaries (input
   0x1d65, optional - CCoreRoughingPassesPlan check FUN_180413a60) offset by it (FUN_1802efdd0 weave offset); mckernel's 3D macro gives the Core
   plan only "ConstraintBoundaries", "Surfaces" (mk l.93229-93243). CoreRoughingExpansionX / Y (default 1.0, hp l.1522-1530) widen the box of
   the boundary collection by its size on each side in FUN_1802e9fb0 (hp l.2995-3020) when the plan behind MachiningPassesIn is 0x1460 - the
   boundary of a plan that reads the passes (the linker's: Mastercam's Core links run outside the containment).
   NOT DECODED: Core's pass region and what FUN_1802ef450 trims to without trim boundaries.
5. **Measured** (TUTORIAL 2 SWARF_METRIC op 1, D 12 r 3, Min 2.97 / Max 5.4, stock 1 / 1, Center containment +-95.25 x +-73.02, Corner rounding
   on): the passes outside the pocket are rings around the part, the first where the tool + stock touches it, then MaxOffset - CornerOffset
   tolerance = 4.8 apart (the open-side step of 11.3 minus tau); a ring 0.9 inside the containment is kept and the next, 3.9 beyond, dropped;
   MOLD BOTTLE op 1 (inch, Core offset 0.0375, trim offset 0.0875) drops a ring 0.07 beyond the containment - so the passes are kept inside
   the constraint boundary grown by the Core offset (0.6 / 0.0375), not the containment + the trim offset; the arcs outside the containment at
   its corners, out to a box at containment + 7.2 (0.6 D), are links (the top level's crossings beyond the containment too).

**Port** (viewer/cam3d_finish.c gen_arearough, CAM_AREAROUGH, cam_cli `arearough`, gallery "Area Roughing", page 1326 = formats/opdlg_fields.tsv
s1326): levels c3_levels_even(.., 5) from PosZHi / PosZLo (6.7; style 0 not in the mask 0x5b6), the first level (PosZHi) skipped as OptiRough;
per level the slice and the tool-centre region T, the passes area_mill_passes(T, Min, Min, Max - Min, tau); Area Clearance: T = containment -
slice; Core: the containment grown by the Core offset, T = a box around it - slice, the passes cut to that grown boundary.
Refused: Add cuts, Corner rounding (positions with tau are ported - MCAM_AR_SMOOTH_DIAG=1 runs them for the oracle - but not the loop
smoothing FUN_180557f60). Ours: Core's pass region (the constraint box widened by 0.2 x its size: 1.0, FUN_1802e9fb0's rule, is 7x slower on
the 2e6-cell clearance grid and coarser), keeping Core's passes inside the grown boundary (measured), the slice only over it, inner passes
first, the entries
(am_ramp of cam2d_area.c on closed passes; an open Core pass starting outside the containment plunges there, else ramps along itself), the
SAFE links. NOT DETERMINED (not generated): the Horizontal Area plan for TP+0x475 = 2 (record field not mapped).

**Fix in the shared grid code** (viewer/area_mill.c area_mill_passes, also the 2D Area Clearance port): the component scan took cells with
clearance > 0 while am_flood needs >= 1e-9; a cell in between left `best` unset (a crash, or hundreds of bogus passes - BRACKET4's default
Area Roughing went from 1.3 M points to 22 k). The scan now uses the flood's threshold.

**Oracle** (tools/tests/arearough_oracle.sh + arearough_cmp.py, in the regression): TUTORIAL 2 SWARF_METRIC op 1 at Z 20.831 (two levels, Corner
rounding positions via MCAM_AR_SMOOTH_DIAG): the pocket's 9 passes and the two outer rings are Mastercam's (the outermost exact, the others
0.2-0.6 off - the clearance grid and the missing smoothing); level p50 0.17 / 0.27 mm. MOLD BOTTLE op 1 at Z 1.193 (Add cuts off): p50
0.021 / 0.017 in, the crossings within 0.01 in. NOT DECODED: Mastercam's levels drift up to 0.08 below Stepdown multiples from Z 36.4 down
(the inflexion levels, CZInflexions*); the extra flat levels (9.176, 46.695).

## 10. Spiral (UI style 9, kernel style 7) and Radial (UI style 10, kernel style 8) - decode and port (2026-10-26)
G = decomp/raster/grid.c, mk = decomp/mckernel_all.c, ms = decomp/mcmsurf_all.c. Both are grid plans sharing Raster's evaluator slot [1] 18035ec70;
the line generators are EvaRadialPassesPlan slot [7] FUN_180380f90 (Raster's is FUN_18037ca90) and the spiral CSpiralPassesPlan FUN_18037eaf0
(EvaSpiralPassesPlan slot [3]; slot [2] FUN_18037dc90 replaces the grid's pass joining - not ported).
- **Parameters** (FUN_180071314 mk l.88158-88340): MachStepover 0x1230; RadialFocalPointX / Y = TP+0x338 / 0x340 (+ the WCS origin in the view when
  TP+0x50, mk l.87305-87319); RadialCloseRad TP+0x348; RadialFarRad TP+0x350 only when TP+0x47b = 0 (and, Spiral, far - close > 10 x TP+0x220);
  Radial start / end TP+0x358 / 0x360 (radians, passed in degrees); PassExtension TP+0x270 (the plan reads "PassesExtension" 0x1ad7: as Raster,
  e = 0 ours); SpiralClockwise = TP+0x477, inverted for Other Way without Zigzag / Down-Up (mk l.88166-88177); SpiralLinearisationTolerance = the
  cut tolerance (job +0xa0) when < 0.01 mm / 0.000393 in, else the plan default 0.01 (G l.22432-22452; unit [inferred] mm).
- **Record offsets** (dialog pages 1338 / 1339; local op = record + 0x2088): X / Y 0x13f0 / 0x13f8, inner / outer radius 0x1400 / 0x1408 (page
  members via FUN_1800864c0 ms l.87611 and InitPage FUN_1800865c0 l.87693-87700: sheet +0x3478..0x3490); Outer radius check 710 = (0x16e3 == 0)
  (l.87698-87700 / OK FUN_180086ee0 l.88101); Spiral clockwise 678 = 0x16df (FUN_1800a0a80 l.105467 / 105512 / 105572); Radial start / end 679 /
  681 = 0x1410 / 0x1418 (FUN_180097bd0). So TP+0x338..0x360 = record - 0x10b8, TP+0x477 / 0x47b = record - 0x1268 (both [inferred] from the
  dialog fields lining up with the kernel's uses). The rest of the page is Raster's base page (FUN_180098940 -> FUN_180088180; 1209 = 0x16c0).
- **Radial lines** (FUN_180380f90 + its disassembly 180381198-1803812ae): da = 2 atan(step / 2 / (far + e)); end < start or end = start -> a
  full turn (start -= 2 pi when start >= 0, else end += 2 pi); k = int(span / da), when not exact k + 1 with da = span / k; one fewer for span = 2 pi;
  line i at start + i da from max(close - e, 0) to far + e (inner -> outer).
- **Spiral** (FUN_18037eaf0 G l.21262-22290): r(t) = close + t step / (2 pi), P(t) = c + (+-cos t r, sin t r) (x negated when clockwise); N = 4
  per turn, doubled until |chord midpoint - P(half angle)| <= the linearisation tolerance (start G l.21470-21540, each point l.21880-21990: on a
  miss N doubles, index -> 2 i - 1); points until one at or past the far radius (l.21991). Each turn's grid starts at the previous last point.
- **Links** (FUN_180075b48 mk l.89846-89875): Zigzag -> bidirectional; else one way, Other Way -> LinkReverseDirection (+ LinkReverseOrder for
  Spiral). Spiral / Radial Linkers 0x142c / 0x142d NOT DECODED.
- **Defaults**: Mill_MM ops 72 / 73, Mill_Inch ops 74 / 75 (centre 0,0, inner 0, outer 12 mm / 0.5 in, angles 0 / 0 = full turn; the inch Radial is Zigzag).
**Port** (cam3d_finish.c gen_spiral / gen_radial, shared hst_grid_*): the above; refused as Raster (Down / Up Mill, approximate start point,
contact angles, Use Z depths) plus no outer radius (the plan default NOT DECODED) and Spiral Zigzag. Ours as Raster: the drop (height field), off
the drive its lowest Z, the containment trimming, the SAFE links, Zigzag on Radial alternating the lines. **No stored op 132 style 9 / 10 exists
in any corpus: unverified against Mastercam.** Scenes gen_spiral / gen_radial (BRACKET4).
