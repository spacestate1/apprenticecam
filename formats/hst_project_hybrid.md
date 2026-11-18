# HST Project (op 132, UI style 15, kernel style 10) and Hybrid (op 132, UI style 7, kernel style 6) - decode and porting spec (2026-10-26)

Rule: every statement cites its source (function address + file:line). Anything not found is marked **NOT DECODED**; anything taken from
part data instead of code is marked **measured** or **inferred**.

## 0. Sources and citation keys

| key | file | what |
|---|---|---|
| mk | decomp/mckernel_all.c | mckernel.dll (HST macro builders, Hybrid engine) |
| ms | decomp/mcmsurf_all.c | mcmsurf.dll (dialogs, file writers for the HST job) |
| mm | decomp/mcmill_all.c | mcmill.dll (offsetWriteTcb) |
| G | decomp/raster/grid.c | mchmm.dll grid pass plans |
| C1 | scratch `.../decomp/proj/core_proj.c` | mccore.dll: functions that reference tmpHmmProj* (FUN_180925280, FUN_180a17900, FUN_180a179f0, FUN_180a17a70) |
| C2 | scratch `.../decomp/proj/apt.c` | mccore.dll CNcgAptPasses (1805b2570-1805b3900) |
| C3 | scratch `.../decomp/proj/hla.c`, `d3.c`, `d3r.c` | mccore bnci_to_hla 1803909f0, FUN_180390820, FUN_180a17a90, run_mchardmill_macro 180597240 |
| C4 | scratch `.../decomp/proj/d4.c`, `d5.c` | mccore op -> TPHSTToolPath copy (FUN_1809232b0 tree), style map FUN_1809239b0 |
| H1..H5 | scratch `.../decomp/proj/h1.c` .. `h5.c`, `h3.c` | mchmm.dll: Axial Offset, Point, Imported, Boundary plans |
| hp | decomp/hmm_params.tsv | McHmm parameter id -> name |

Scratch dir = `decomp/proj/`
(decompiled with tools/ghidra_scripts DecompAt / DumpRange / DumpDecomp / VtableDump on copies of ~/mastercam-re/ghidra_core and ghidra_hmm).
`tpmap.tsv` there is the full op -> TP table generated from FUN_180923f60.

### 0.1 Record offset <-> kernel TP offset (now decoded, replaces the "[inferred]" block shifts of hst3d_port_plan.md 7-10)
- mccore FUN_1809232b0 (C4 d4.c, also decomp/poszlo/setup.c l.49) fills the TPHSTToolPath from the operation; for op 132 it calls
  FUN_180923f60 (C4 d4.c l.60-255), a plain field copy `TP[m] = op[o]`.
- The kernel reads the same object through `param_1[3]`: kernel offset **K = m - 0x60** (check: style byte K+0x45c = m 0x4bc =
  FUN_1809239b0(op+0x11c0), d4.c l.63-65; K+0x220 = m 0x280 = op 0x1238 = record 0x12a0, matching hst3d_port_plan.md 6.7).
- **record = op + 0x68** (formats/nci_writer.md "record = op + 0x68"; style op 0x1654 = record 0x16bc).
- The whole table is in scratch `tpmap.tsv`; the rows used below are quoted where used.

### 0.2 UI style -> kernel style (mccore FUN_1809239b0, C4 d5.c l.1-30; switch on op+0x1654)
0->0, 1->9, 2->1, 3->2, 4->3, 5->4, 6->5, **7->6**, 9->7, 10->8, **15->10**, others 99. So the "kernel style 6, meaning NOT DECODED" of
hst3d_port_plan.md 1.1 is **Hybrid**, and the UI->kernel mapping of that table is now decoded (not just paired by plan name).

---------------------------------------------------------------------------------------------------------------------------------

## 1. Project (UI style 15, kernel style 10)

### 1.1 Dialog page 1517 "HST Project Parameters" -> record -> kernel
DDX FUN_180097040 (ms l.98515-98530), InitPage FUN_180097130 (ms l.98551-98620), UpdateLocalOp FUN_180097920 (ms l.98840-98870);
local op = record + 0x2088.

| control | meaning | local op | record | op | kernel TP | notes |
|---|---|---|---|---|---|---|
| 834/837/841 radio | Projection type NCI / Curves / Points | +0x36e2 (short) | **0x165a** | 0x15f2 | K+0x49c | 0/1/2 (UpdateLocalOp l.98845). InitPage forces 1 (Curves) when the part has no source op (l.98571-98576) |
| 997 tree | Source operation (NCI mode) | +0x3604 (int) | **0x157c** | 0x1514 | - (read by mcmsurf, 1.3.1) | from the tree FUN_1800f5530; reset to 0 when the op does not exist (l.98848-98856) |
| 1489 int edit | Number of depth cuts N | +0x35e0 = N - 1 | **0x1558** (int) | 0x14f0 | K+0x44c NumberOfOffsets | UpdateLocalOp l.98860; InitPage shows value + 1 (l.98600) |
| 1490 real edit | Stepdown | +0x3440 | **0x13b8** | 0x1350 | K+0x310 Stepdown | InitPage clamps the shown value to >= system tol (l.98601-98605); edit enabled only when N > 1 (l.98606-98608) |
| 1493 check | Order by depth | +0x36e6 (short) | **0x165e** | 0x15f6 | K+0x4d8 -> ByOffset = (K+0x4d8 == 0) | FUN_18007c8a4 mk l.92137-92140 |
| 692 combo | Tip compensation | base page | NOT DECODED | | | |
| 1205-1208 | Keep tool down within | base page FUN_18002bc40 | NOT DECODED here | | | shared HST linking page |

Other kernel fields Project reads (via tpmap.tsv): contact angles K+0x1d0 / 0x1d8 = record 0x1248 / 0x1250 (FUN_180071314 mk l.87268-87300,
clamped to 0..pi/2), cut tolerance K+0x220 = 0x12a0, XY / Z stock K+0x318 / 0x320 = 0x13c0 / 0x13c8, Use Z depths K+0x464 = 0x16c9 with
limits K+0x1c0 / 0x1c8 = 0x1238 / 0x1240 (FUN_180070b1c mk l.86775-86795), Zigzag K+0x45d = 0x16bd, Down/Up K+0x46f = 0x16d4, Other way
K+0x470 = 0x16d5 (links, 1.6).

**Defaults** (tools/mcam_ops.py on the 2017 defaults files):

| file / op | 0x165a | 0x157c | 0x1558 | 0x13b8 | 0x165e | 0x16bd / 0x16be | 0x12a0 | 0x1248 / 0x1250 |
|---|---|---|---|---|---|---|---|---|
| Mill_MM op 80 | 0 NCI | 3 | 1 (2 depth cuts) | 1.0 | 0 (ByOffset true) | 0 / 1 | 0.025 | 0 / 1.5708 |
| Mill_Inch op 79 | 0 NCI | 5 | 0 (1 depth cut) | 0.05 | 0 | 0 / 1 | 0.001 | 0 / 1.5708 |

### 1.2 Chain (mccore -> mckernel)
1. mccore run_mchardmill_macro 180597240 (C3 d3.c l.89-170): when cur op is 132 with style 15 (`DAT_181377a14 == 0x84 && DAT_181379064 == 0xf`)
   it calls **FUN_180a17a90** (C3 d3r.c l.1-26), which switches on op+0x15f2 (record 0x165a): 0 -> FUN_180a179f0 (NCI), 1 -> FUN_180a17900
   (Curves), 2 -> FUN_180a17a70 (Points). Then **FUN_180a178e0** (C4 d4.c l.677-690) copies the first point (TP +0x5f0..0x600, set by
   the writers) into hstJobIdDat +0x10..0x20. Its use in the kernel: NOT DECODED.
2. FUN_1809251c0 (decomp/poszlo/setup.c l.3) -> FUN_180923f60 (field copy, 0.1).
3. **MCHSTToolPath::SetUpInputFiles FUN_180925280** (C1 l.12-137; assert "KernelInterface\MCHSTToolPath.CPP" 0x248) copies the temp files
   into the job: tmphardmill.raw, tmphardmillraw.stl, tmpmmlu, tmptc, tmptca, tmpmm2, tmpmm5, tmpmn5, tmpmm8.emm, **tmpHmmProjPts.tmp,
   tmpHmmProjCrv.tmp, tmpHmmProjNci.apt**, tmpmn35.stl, tmpMaxStkEng.emm, then tmphmmstk.stl / .emm / tmphmmstku.emm, tmpapt*.apt.
4. mckernel Project job **FUN_18011e7b0** (mk l.223907-223938): FUN_18011d62c (mk l.223407) copies the mode's file to the job's own name
   (NCI: FUN_18011f6ec mk l.224787 CopyFile tmpHmmProjNci.apt; Points: FUN_18011f784 l.224810 tmpHmmProjPts.tmp; Curves: FUN_18011d758
   l.223456 only checks the file and writes "// Number of Curves / Points" comments) and compares with the job's previous file
   (FUN_1800a385c) to decide whether job files 0xe / 8 / 10 are invalidated (l.223440-223449).
   - NCI: FUN_18011d588 (l.223381) -> **FUN_18011d914** (NCI passes macro, 1.4.1) -> FUN_18007d028 (linking).
   - Curves / Points: FUN_18007d000 (l.92310) = FUN_18007d7c8 (Machining Passes macro, 1.4.2) + FUN_18007d028.

### 1.3 What mccore / mcmsurf hand to the job (open question 1)

#### 1.3.1 NCI mode -> tmpHmmProjNci.apt
- mccore FUN_180a179f0 (C1 l.190-204) -> mcmsurf **MutlAnalyzeOperationApt**(op, "tmpHmmProjNci.apt", 2, &firstPt) (ms l.352012-352240).
- Source op: `param_3 == 2` -> op id = *(long *)(op + 0x1514) = **record 0x157c** (ms l.352114-352117). Its NCI is read from the
  op's start (`bnci_read_sos`, `bnci_read_from_disk`, l.352119-352126).
- Per NCI record (l.352127-352230):
  - 0x410: skipped (bnci_read_ignore_end); 0xfffd: stop; 0xfff9: pass break (count reset, l.352160-352163).
  - 1013 (0x3f5): when the origin field (record +0x38, three components, any |c| > 1e-12) is non-zero, the HF_TOOL_PLANE offset is set to it
    (byte +0 = 1, offset at +8); the view (record +0x32) is kept; view > 8 -> bp_scratch_view (l.352164-352181).
  - 1014 (0x3f6): when that view > 1, byte +1 = 1 and the matrix is copied (and into the scratch view when > 8) (l.352182-352225).
  - G codes 0..3 (`< 4`): **bnci_to_hla** (mccore 1803909f0, C3 hla.c) then **HlaToApt** FUN_180212140 (ms l.347968).
- bnci_to_hla (hla.c l.46-145): G0/G1 -> HLA line, start = previous end, xyz taken **as stored in the NCI** (no view transform);
  G2/G3 -> 2D arc in the arc's plane view (plane_to_view(record +0x18)), constr_ns_arc / constr_circle with nc_tol, arc_2d_to_3d; helix
  (end z != start z beyond tol) -> HLA kind 2 with z0 / z1. Then **FUN_180390820(hla, toolplane, 1)** (C3 d3.c l.1-60): when the 1013 offset
  is set, it is **added** to the line ends / arc centre (arc in a view >= 2: via view_to_world / world_to_view).
- HlaToApt (ms l.347968-348146, "MSurfUtl.cpp"):
  - **G0 ends the current pass** (CNcgAptPasses::End when it has moves, count = 0) - every rapid splits passes.
  - G1: first move of a pass -> FROM start (GOTO when param_5[1] == 0 and param_4 != 0), then GOTO end.
  - G2/G3: FROM / GOTO the arc start; when param_3 (= 1 here) a full circle (|sweep| = 2 pi +- 1e-6) is cut into 3 and a half circle (pi)
    into 2 CIRCLE records, the helix z interpolated per piece; each CIRCLE/ centre, axis (0,0,+1 for sweep > 0 else -1), radius, then GOTO end.
- APT writer CNcgAptPasses (mccore 1805b2570.., C2): ctor(IsMetric, 0): scale +0x40 = **1 metric, 25.4 inch** - the file is **always mm**
  (C2 l.1-26). Lines (C2 l.94-150, 501-588, 408-440, 276-406):
  - `FROM/ x,y,z`, `GOTO/ x,y,z`, `CIRCLE/ cx,cy,cz,ax,ay,az,r` followed by the GOTO of the end, `END`, `CUTTER/ d,r,h[,a]`; each line ends
    with "\n"; numbers `%19.13f` trimmed of trailing "0", leading " ", trailing "." (RealValString C2 l.784-806; strings at mccore 180cf7cd4 /
    180cf5440 / 180d1d898 / 180d7c3ec read from the binary).
  - Points are scaled by +0x40 and z += +0x48 (ScaleCompPt3D C2 l.850-868); the axis is not scaled; r is scaled.
  - Cutter(op) (C2 l.276-406): tool of **the Project op** (op +0x544 tool number, tool_manager); CUTTER/ = (diameter, corner radius,
    height terms; type 0xc taper / 0xd angle cases) x scale. SetTipComp: **z offset = 0 when OpIsTipComp(op), else - corner radius**
    (C2 l.392-397, 870-881).
- First pass point -> TP +0x5f0 (ms l.352158-352161) -> hstJobIdDat +0x10 (1.2 step 1).

#### 1.3.2 Curves mode -> tmpHmmProjCrv.tmp (+ tmpHmmProjCrvA.tmp)
- mccore FUN_180a17900 (C1 l.153-188): `surface_manager(op id, &chains, 8, ..)` (the op's geometry kind 8), then
  **GetAndConvertChainsToCurvs**(op type, (byte)op+0x1654 (long* + 0x595), 3, chains, "tmpHmmProjCrv.tmp", "tmpHmmProjCrvA.tmp")
  (ms l.196543-196588), then GetLinearCurvsStartPt2D(0, "tmpHmmProjCrv.tmp") -> TP +0x5f0 (x, y, z = 0).
- GetAndConvertChainsToCurvs: close flag uVar2 = 0 for (op 7 / 13 / 0x6d) or **(op 0x84 and style 0xf)**, else 1 (l.196562-196567) - so
  Project curves are written **without the closing point**; FUN_180129b40(msurf_cut_angle, u, v, ..) builds the 2D axes; FUN_180128390(3,
  chains, u, v, curve_chord_dev, 0, 1, files) (l.196575-196577).
- FUN_180129b40 (ms l.197025-197130): view = MastercamPlanes::GetActiveViewNo(Planes, 0); u / v = that view's rows rotated by the angle
  (sin / cos of msurf_cut_angle). **Which value msurf_cut_angle holds during an HST Project run: NOT DECODED** (it is a global set by msurf_map
  for the classic ops); which view "GetActiveViewNo(.., 0)" returns: NOT DECODED.
- FUN_180128390 (ms l.196075-196540) - file layout (main file):
  - short nChains; then nChains x (int fileOffset, int nPoints) (rewritten at the end, l.196444-196446); then per chain the points as
    **2 doubles (p.u, p.v)** (l.196365-196380).
  - Entities: FUN_180129590 (l.196799-196812) keeps types with mask 0xc8e (not 0x200), points (type 1) only when param_7 (= 1).
  - Linearisation per entity (l.196300-196400): span count k = 1 for entity types 1 / 2 / 4, (ent +0xb0 int) + 1 for type 8, the entity's int at
    +0xcc for type 0x80, 5 otherwise (type meanings per formats/mcam.md); each span sampled at FUN_180131710(ent, chord dev, t0, t1) + 1 equal parameter
    steps (eval_curve); a point is written only when |du| or |dv| to the last written point exceeds the chord deviation (`curve_chord_dev`).
  - Attribute file (tmpHmmProjCrvA): per entity ushort "first of chain" flag, two ints from the entity record (+0x34, +0x30: meaning
    NOT DECODED), int entity type, byte reversed; terminator ushort 2 (l.196327-196356, 196447-196450).
  - Failure: any chain with 0 points (or 1 when param_7 == 0) -> error -0x10 (l.196493-196505).
  - For op 132, when the containment tool position op[0x165f] (record 0x16c7) != 1 and |op+0x1248 (+ D/2 when op[0x1660])| > 1e-4,
    **offsetWriteTcb** runs (l.196452-196478); that function always rewrites **tmptc.tmp** (mm l.322700-322870, "POCKMAIN.cpp", strcat
    "tmptc.tmp" at l.322845) - so the project curve file itself is **not** offset. Whether this re-run changes the containment: NOT DECODED.
- Kernel read: FUN_18007cbc4 (mk l.92186-92245) opens the file with CToolContainmentBndy (FUN_18009d790 mk l.116997, the containment-boundary
  reader); per chain type +0x14: 0 -> resolved by FUN_1800a7928 (closedness test, NOT DECODED), **2 = closed (n - 1 points), 1 = open**; each
  chain -> `entity ekBndyCurvN = Create2DBoundary(x0 * k, y0 * k, ...; indices[, closing])` with k = job +0x80 (FUN_1800eec0c mk
  l.184042-184090, "// 2D Closed or Open Boundary"), collected as "ekBndyCurvCollection4"; then **Drive Boundaries Plan 0x1427**
  "ekPlanDrive3" (mk l.93251-93259). The curves are therefore **2D (XY in the u/v frame); their Z is discarded**.

#### 1.3.3 Points mode -> tmpHmmProjPts.tmp
- mccore FUN_180a17a70 (C1 l.140-150) -> **GetConvertProjectPts**(op, true, "tmpHmmProjPts.tmp", &firstPt) (ms l.196590-196726).
- Every database entity returned by get_ent(.., mask 0x40, 1, ..) (l.196666-196670; which entities mask 0x40 / flag 1 select - all points or
  only the op's - NOT DECODED), its point (ent +0x68, 3 doubles) -> world_to_view(op +0x622 view) when that view >= 2
  (l.196676-196690), scaled by 25.4 (inch part) or 1/25.4 (when the bool disagrees with IsMetric; with `true` and a metric part: 1)
  - i.e. **mm**, written as `"%14.8f %14.8f %14.8f \n"` (l.196692-196712). The fprintf arguments were dropped by the decompiler; the three
  FUN_1800062d0(pt, 2 / 1 / 0) calls before it suggest x y z [inferred].
- McHmm reads it with FUN_1802e3580 (H4 h4.c l.1307-1387): `swscanf(L"%f %f %f\n")` into **floats**, keeps **(x, y, 0)** - the point's Z is
  discarded, precision is single float.

### 1.4 Kernel macros

#### 1.4.1 NCI variant FUN_18011d914 (mk l.223516-223905)
- No stock-model branch (TP+0x463 = 0): Database Loader 0x1421 -> Detect Top Surfaces 0x1423 -> Surface Tessellation 0x1424 (drive);
  optional check surfaces (TP+0x495 = record 0x172c and FUN_18006e4d0) via FUN_18007ca68; **Bounding Box Plan** 0x1425 with OffsetDistance =
  |TP+0x318| + FUN_1800969b4(tool, dia) x 0.5 + 2 x TP+0x220 (l.223612-223622); optional "// Boundaries Plan" 0x1427 (containment,
  FUN_180081f6c) -> FUN_18007c528; optional prefillet (FUN_18007c3f4 / FUN_180081de8); FUN_180079d34.
- **FUN_180081708**(job, apt file, 1, ..) (mk l.94110-94148): `FileName` = the APT, "**Imported Passes Plan 1**" 0x1474 "ekPlanIP1" with inputs
  "Surfaces", "ConstraintBoundaries"; then "**Convert to Bounday Plan**" 0x1430 (input "Tessellations" = ekPlanIP1) -> ekPlan4
  (l.223660-223667). Convert-to-boundary internals: **NOT DECODED** (where the APT passes become 2D boundaries).
- FUN_180070b1c (mk l.86706-86800): ToolThicknessXY / Z (when r + min(sxy, sz) < -2 tol the larger minus the smaller, else as is), PosZHi /
  PosZLo = job +0x40 / +0x38, or with Use Z depths (K+0x464) max / min of K+0x1c0 / 0x1c8 + job +0x28. **FUN_180071314 is not called on this
  path** (so no ContactAngleLo / Hi or MachTrimZTolerance for NCI mode - as decoded; their plan defaults NOT DECODED).
- "// Boundary Project NCI Passes Plan" (or "// Boundary Passes Plan" / "Boundary Project Curve Passes Plan" in the rest / check variants)
  type **0x1412** with inputs "ConstraintBoundaries", "DriveBoundaries", "Surfaces" = (tcb or bbox, ekPlan4, drive).
- Depth cuts and toolholder: when NumberOfOffsets > 0 the passes plan is named "ekPlan5x" and FUN_18007c8a4 adds the **Axial Offset Passes
  Plan 0x143b** as "ekPlan5"; with the holder check (FUN_18006f480: K+0x472 = record 0x16d7, a non-empty holder list, K+0x49e = record
  0x165c == 0) FUN_1800820c8 adds "Edit Passes to Toolholder" 0x1459 (l.223672-223734). Rest / check variants add Edit Passes to Surfaces
  0x141f and Convert To Primary 0x1458 (l.223738-223830).

#### 1.4.2 Curves / Points: FUN_18007d7c8 (mk l.92564..) -> vtable +0x18 = FUN_18011e85c (mk l.223940-224330)
- FUN_180071314 style-10 branch (mk l.88421-88459): **PosZHi, PosZLo** (as hst3d_port_plan.md 6.7; 0x5b6 = styles 1, 2, 4, 5, 7, 8, 10, so
  style 10 PosZLo is also lowered by the corner radius), **MachTrimZTolerance** = cut tolerance when it is below 0.1
  (metric, job +0x20 +8 != 0) / 0.003937 (inch) (l.88438-88449; constants l.87498-87499 and 88439-88441), **ContactAngleLo / Hi** (K+0x1d0 /
  0x1d8 clamped, l.87268-87300). No Stepdown / stepover.
- The 5-byte flag block passed in (mk l.93935-93957): [0] containment boundary present ("ekPlanTcb3" vs "ekPlan3"), [1] prefillet
  ("ekPlanPrefillet2" vs "ekPlan2"), [2] check surfaces (K+0x495 && FUN_18006e4d0), [3] stock / rest (K+0x463 && K+0x475 != 2), [4] holder
  (FUN_18006f480) [the meaning of [0] / [1] is inferred from the plan names they select in FUN_18011e85c].
- **Curves** (l.224190-224330): "// Boundary Project Curve Passes Plan" **0x1412**, inputs "DriveBoundaries", "ConstraintBoundaries",
  "Surfaces" = ("ekPlanDrive3", tcb / bbox, drive); + Axial Offset / holder / rest / check as 1.4.1.
- **Points** (l.223965-224185): `PointProjLoadFromFile` = 1, `FileName` = tmpHmmProjPts.tmp (job copy); "// Project Point Passes Plan" (or
  "// Boundary Project Point Passes Plan" with check / rest) type **0x147a**, inputs "ConstraintBoundaries", "Surfaces"; same Axial Offset /
  holder / rest / check chain. File missing -> fail (l.223997-224002).

### 1.5 McHmm plans (open questions 2 and 3)

#### 1.5.1 Boundary passes (Curves, NCI): EvaBoundaryPassesPlan vftable 180791898
VtableDump (scratch vt.txt): [0] 180363f40, **[1] 18035ec70** (the grid evaluator shared with Raster / Spiral / Radial), [2] 18035fbb0,
**[3] FUN_180382210** (G l.22984-23372), [6] 180360120.
- FUN_180382210 reads `BoundaryLinearisationTolerance` 0x1a9b (hp; G l.23000-23004) and the plan behind input **DriveBoundaries** 0x1a93
  (l.23005-23009), takes its output `Boundaries` 0x1b44 (a CTessBoundaryCollection, l.23090-23140).
- Per boundary (l.23143-23215): an EvaToolpathFrag, its fragments -> per-pass lists (FUN_180383c90 / FUN_180383d00); then FUN_180383e80
  (G l.24354) runs **FUN_180382a30** (G l.23373-23709) per pass: each fragment vertex is appended as a **2D grid point** (16-byte (x, y)
  entries at Eva +0xff0, a CGridVal index per pass at Eva +0x1008); an arc fragment (move flag bit 1) is linearised by FUN_180383890 with the
  tolerance, direction from flag 0x4000000 (reversed when the fragment is reversed) (l.23590-23605).
- So **each drive boundary becomes one grid pass whose XY polyline is the boundary**; slot [1] then drops the tool along it onto the drive
  (the same path as Raster, hst3d_port_plan.md 8: FUN_1803612d0 drop, refine, the split post-filter FUN_180361bd0). This is exactly
  cam3d_finish.c `hst_grid_pass` on the boundary polyline.
- Plan default of BoundaryLinearisationTolerance: NOT DECODED (mckernel does not set it).

#### 1.5.2 Point passes: EvaPointPassesPlan vftable 180760338 ([1] FUN_180013ae0, [2] FUN_180013ab0)
- FUN_180013ae0 (H1 h1.c l.191-425): optional trim to ConstraintBoundaries 0x1b41 when `EnableTrimToBoundaries` 0x1b3f (tool box
  EvaToolBoxFrags, FUN_180231900(.., 1, -1.0, 1.0)); then **FUN_180014050** (H4 h4.c l.3-280); then ShaftProfileEnable 0x1c9f ->
  FUN_1802ec5a0 (holder / shaft, NOT DECODED).
- FUN_180014050: `PointProjLoadFromFile` 0x1ed6: true -> points from `FileName` 0x1a97 (FUN_1802e3580, 1.3.3); false -> `PointProjInputList`
  0x1ed7 as (x, y) pairs, z = 0 (l.30-75). No points -> nothing. Else PosZHi 0x1ac8 / PosZLo 0x1ac7 are read and a tbb parallel_for runs a
  lambda per point (FUN_1800172f0 l.367-417); each non-empty result is one pass (FUN_1802ed200, vtable +0x10, FUN_1802ed5f0, l.200-245);
  FUN_180013ab0 links consecutive passes (FUN_1802ed2c0).
- The lambda body (the per-point tool drop between PosZHi and PosZLo, and what a "point pass" contains): **NOT DECODED** (its address is in
  the start_for vftable built at h4.c l.395-397). By the class CTessToolpathPoint (vftable 1807633d0) a pass is a point toolpath [inferred].

#### 1.5.3 Imported passes (NCI): EvaImportedPassesPlan vftable 1807a77f0, [1] FUN_180451850 (H1 h1.c l.444-504)
`FileName` 0x1a97 empty -> fail; optional trim (0x1b3f -> FUN_1802ef450); file type FUN_1802e1500 == 0xd -> **FUN_180451990** (H5) reads
the APT; another type -> error. The APT tokenizer (FUN_1805591d0 / FUN_180452a80, "Token %s not supported in APT file"): token table
**NOT DECODED** (the writer's set FROM / GOTO / CIRCLE / END / CUTTER is known, 1.3.1).

#### 1.5.4 Depth cuts: Axial Offset Passes Plan 0x143b (EvaAxiallyOffsetPassesPlan vftable 180788360, [1] FUN_180320750)
- FUN_180320750 (H1 h1.c l.1-169): input `MachiningPassesIn` 0x1bb0; for a source plan of type 0x1404 / **0x1412** / 0x1417 / 0x1418 /
  0x1433 / 0x143a / 0x146f / 0x1474 / **0x147a** it runs FUN_18031f660(this, passes, adjacency = (source type != 0x143a)).
- **FUN_18031f660** (H2 h2.c l.3-664): Stepdown 0x1ada = s, NumberOfOffsets 0x1ac1 = n, ByOffset 0x1ee1; optional trim (0x1b3f).
  - With adjacency (all Project sources): the passes are first ordered through EvaFragAdjacency (FUN_180185f00 + FUN_180182e20) and each
    next pass is picked by FUN_18031f4b0 (the adjacency successor; when none, the first remaining) (l.140-180).
  - Each copy: FUN_18031f160(.., pass, k x s) -> CTessToolpath vtable +0x58; for a mill pass (CTessToolpathMill FUN_1804ac490 ->
    FUN_1804a7c60, H3 h3.c) **z' = z + k x s** (h3.c FUN_1804a7c60 l.256 / l.395) - the copies are **raised** by k stepdowns.
  - **ByOffset false (Order by depth checked)**: for k = n, n-1, .., 0: every pass at +k s (level by level, top first) (l.150-330).
  - **ByOffset true (default)**: for each pass: k = n .. 0 (the pass cut top to bottom before the next) (l.331-610).
  - Between consecutive copies FUN_1802ed2c0 adds the link record (l.302, 586); trim 0x1b3f -> FUN_1802ed680 per copy.
  - In the first branch the decompile drops the double argument of FUN_18031f160 (l.183); the second branch shows `(double)k * s` (l.420).
    That the first branch passes the same k x s is [inferred] from the shared loop variable.
- So "Number of depth cuts N" = the source passes plus N - 1 copies raised 1..N-1 stepdowns, cut from the highest down.

### 1.6 Links (FUN_18007d028 mk l.92328; style 10 l.92515-92540; parameters FUN_180075b48 mk l.88917, used l.89806-89935)
- Linker plan: NCI -> "//Get Project Boundary NCI Passes Plan" + "Project Boundary NCI Linker Plan" **0x142e**; Curves -> "Project Boundary
  Curve Linker Plan" 0x142e; Points -> "Project Point Linker Plan" **0x147b**.
- Direction (generic branch, l.89806-89880): Zigzag K+0x45d (0x16bd) with Down/Up K+0x46f (0x16d4) == 0 -> LinkDirectionStyle 1; K+0x46f 1 / 2
  -> style 2 / 3 (name decoded in hst3d_port_plan.md 8) with LinkDMExtendStart K+0x2f0 (0x1380) and LinkDMShallowSlope deg(K+0x2f8 = 0x1388);
  else LinkDirectionStyle 0 and LinkReverseDirection = K+0x470 (0x16d5).
- Styles in the mask 0x590 (4, 7, 8, 10): SurfaceRampStyle 0 when K+0x473 (0x16d8) == 1 else 1 (l.89903-89918); for style 10 a boolean whose
  name the decompile lost is set true (l.89925-89927, NOT DECODED), and `LinkRampDownToPasses` = false when NumberOfOffsets > 0 (l.89928-89932).
- Points mode only: LinkMaxStayOnSurface rule l.89237-89260 (style 10 with K+0x49c == 2 skips the min() test). The linkers 0x142e / 0x147b: NOT
  DECODED (as every linker).

### 1.7 Port recipe (what can be built on viewer/cam3d_finish.c)
1. Curves mode (fully decoded inputs): take the op's kind-8 chains, linearise each entity at the chord deviation (1.3.2), project to the
   tool-plane XY (u/v = the view rows; angle 0 [**ours** until msurf_cut_angle is decoded]), drop the closing point, open/closed per chain;
   each chain -> one pass: `hst_grid_pass` along the polyline (Raster's drop); clip to the containment as Raster.
2. NCI mode: walk the source op's moves (record 0x157c), split at every G0, lines as is, arcs linearised (our tolerance - the boundary
   linearisation default is NOT DECODED), add the 1013 origin, keep only XY (the boundary path drops Z); then as 1.
3. Points mode: each selected point (x, y) -> one drop of the tool at (x, y) between PosZHi / PosZLo [the per-point lambda is NOT DECODED, so
   this is **ours**: a single plunge position].
4. Depth cuts: N > 1 -> copies raised by k x stepdown, k = N-1 .. 0; order per 1.5.4 (ByOffset = !Order by depth).
5. Links: SAFE (ours, as Raster); direction per 1.6.
Refuse: Points mode (until the lambda is decoded), rest material / check surfaces / holder chains, Down / Up mill, Use Z depths (as Raster).

### 1.8 Oracle
No op 132 style 15 exists in the corpus (`~/mastercam-re/work/hst3d/netscan/scan3d.tsv`: op 132 styles 0 x24, 1 x4, 2 x5, 3 x6, 5 x4, 6 x1,
7 x4). **Unverified against Mastercam.** (scan3d's "style" column is record 0x16bc, the UI style.)

### 1.9 NOT DECODED (Project)
msurf_cut_angle during the HST run and the view of GetActiveViewNo(Planes, 0); the entity selection of GetConvertProjectPts (mask 0x40);
FUN_1800a7928 (closed test); Convert to Boundary Plan 0x1430; the APT tokenizer; BoundaryLinearisationTolerance default; the Point passes
per-point lambda; the base-page fields (tip compensation 692, keep tool down 1205-1208); the linkers 0x142e / 0x147b; the unnamed link bool
(mk l.89925); how hstJobIdDat +0x10 (first point) is used.

---------------------------------------------------------------------------------------------------------------------------------

## 2. Hybrid (UI style 7, kernel style 6)

### 2.1 Chain
- mccore MakeMCHSTToolPath FUN_180573610 (decomp/poszlo/hardmill.c l.3-54): style 7 -> FUN_1809257e0 (C4 d5.c) = MCHSTToolPath ctor +
  `MCHybridWSToolPath::vftable`; the op fields go through the same FUN_180923f60 copy (0.1); kernel style = **6** (0.2).
- mckernel TPHybridWSToolPath::Compute 18012217c (mk l.226595-226620) -> FUN_18008584c (ctor, mk l.97458) and **FUN_1800882e0** (mk
  l.99424-99460):
  1. FUN_18008dbc0(job "job%d_hybridWS.tmp") (mk l.103795); FUN_18008eaf4 (l.104502) true -> reuse (FUN_18007c4b8 / FUN_18007c234), else
     **FUN_18008684c** (compute, mk l.98128-98150).
  2. When Optimize cut order K+0x45f (record 0x16c0) is set: FUN_18008cbb4 / FUN_18008cdc8 (mk l.103024 / 103127; NOT DECODED).
  3. FUN_180088748 (mk l.99691) writes **tmpapt.apt**; FUN_18006e52c (mk l.84937) = FUN_18007bb48 (the HST job runner, hst3d_port_plan.md
     6.7) + FUN_18006fff0 (mk l.86188); then FUN_18008659c.
  4. Linking (FUN_18007d028): style 6 -> "//Get Imported Passes Plan" "ekPlan0" + "// Imported Linking Plan" **0x1475** (mk l.92490-92501);
     with check surfaces the linker also gets "LinkClampSurfaces" (l.92370-92390).
- FUN_18008684c: **FUN_180086730** (mk l.98089-98120) runs the mckernel **CKcKonstZcutEmm** engine (ctor FUN_18003c538 mk l.48748) through
  FUN_180041ef4(.., "tmpmmlu.tmp", "emmrigid.tmp", "emmzcuts.tmp") (mk l.53012) on **tmpmm5.tmp**; with Flat detection K+0x493 and Flat area
  K+0x494 == 2 (Flats only) its limit angle is forced to pi/2 (l.98110-98113). Then FUN_1800868ac (l.98155): thread count K+0x454 (record 0x1650)
  > 1 -> FUN_180086d80 (threaded), else **FUN_1800868e0** (l.98176-98383). Then FUN_18008c930 (l.102903, reads emmBothRegionsMap.tmp).
- FUN_1800868e0: opens **emmBothRegionsMap.tmp** (written by the Z-cut stage), counts regions with (kind 2, sub-kind != 3); per such region a
  **CKcScallopEmm** (ctor FUN_18004f2dc mk l.63778) on tmpmm5.tmp with the tool (TP+0x70), cut tolerance K+0x220, **stepover K+0x1b8**; then
  FUN_18008e004 (region), FUN_18005819c, FUN_1800581f4(Steep Climb K+0x45e), filters K+0x3f8 / K+0x400, Map-3D-filler (K+0x496 == 1),
  FUN_180058338(Shallow Offset K+0x492), stepdown K+0x1b0, Z limits (K+0x1c0 / 0x1c8) when the object's +0x299f flag, corner radius, flat
  machining FUN_180058144(K+0x493, K+0x494, K+0x408), then FUN_1800520dc (compute) (l.98290-98370).
- The remark strings "WATER LINE %d" / "SCALLOP %d" (FUN_180083af0 mk l.95943-95978) tag the two pass kinds.

**Dependency answer**: Hybrid uses neither McHmm's Waterline (EvaWaterlinePassesPlan, ported as gen_waterline) nor McHmm's Scallop
(constant-stepover plans, formats/scallop.md). Its passes come from mckernel's own **Kc "emm" engine** (CKcKonstZcutEmm, CKcScallopEmm,
CKcMultiKonstZcutEmm, CKcRigidEmm, CKcPbmap3, CKcZsurfUtl, CKcHstZrest - vftables in mk), reading the msurf-format model tmpmm5.tmp (format
NOT DECODED here), and McHmm only links the result (Imported Passes + Imported Linking Plan). The ported Waterline / Scallop are therefore
not the Hybrid algorithms; using them would be **ours**.

### 2.2 Parameters (page 25623 "HST Hybrid Parameters", 25628 "HST Hybrid Advanced")
DDX FUN_18008e150 (ms l.92885-92930), InitPage FUN_18008e640 (ms l.93080-93372; combo strings l.93125-93158 via exports/mcmsurfres_strings.tsv),
UpdateLocalOp **FUN_18008f450** (ms l.93689-93762; "HSTHybridWSParamPage.cpp"); stepover clamp FUN_18008e440 (ms l.92992-93068). Kernel names
from the macro comment dump **FUN_18008f704** (mk l.104907-105000).

| control | UI | record | kernel | kernel name (FUN_18008f704) | Mill_MM op 74 | Mill_Inch op 76 | T7 METRIC op 6 |
|---|---|---|---|---|---|---|---|
| 1484 combo | Closed contour direction: 0 Climb / 1 Conventional | 0x16bd / 0x16be: Climb = (0, 1), Conv = (0, 0); shown index = (0x16be == 0) | K+0x45d / 0x45e | Steep ZigZag / Steep Climb | 0 / 1 (Climb) | 0 / 1 | 0 / 1 |
| 1240 combo | Open contour direction: 0 One Way / 1 Zigzag; both also clear 0x16d4 / 0x16d5 | 0x172a | K+0x491 | Shallow ZigZag | 0 | 0 | 0 |
| - | (cleared by 1240) | 0x16d4 / 0x16d5 | K+0x46f / 0x470 | Shallow DownUpStyle / Shallow PreferReverse | 0 / 0 | 0 / 0 | 0 / 0 |
| 1475 check | Optimize cut order | 0x16c0 | K+0x45f | - | 1 | 1 | 1 |
| 1433 | Z stepdown | 0x1228 | K+0x1b0 | StepDown | 4 | 0.1 | 1 |
| 1434 | Limiting angle (rad) | 0x1498 | K+0x3e8 | LimitAngle | 0.6109 (35 deg) | 1.309 (75 deg) | 1.309 |
| 1435 | 3D stepover (clamped to 0.95 (D - 2r) when D - 2r >= 1.19e-7, else 0.95 D, with a warning) | 0x1230 | K+0x1b8 | StepOver | 1 | 0.025 | 0.5 |
| 1233 check | Preserve Z passes | 0x1652 (short) | K+0x456 | Preserve Z passes | 0 | 1 | 0 |
| 1241 combo | Offset method: 0 Closed offsets / 1 Upper to lower / 2 Lower to upper / 3 Automatic | 0x172b | K+0x492 | Shallow Offset | 0 | 3 | 3 |
| 1497 group check | Flat detection | 0x172e | K+0x493 | Flat area machining OnOff | 0 | 0 | 0 |
| 1242 combo | Flat area: 0 Include flats / 1 Ignore flats / 2 Flats only | 0x172f | K+0x494 | Flat area machining cut method | 0 | 0 | 0 |
| 1495 | Flat stepover (same clamp) | 0x1500 | K+0x408 | Flat area XY stepover | 0 | 0 | 0 |
| 25628 1477 | Remove tight passes | 0x14b0 | K+0x3f8 | Close pass filter factor | 0 | 0 | 0 |
| 25628 1478 | Remove small segments | 0x14b8 | K+0x400 | Small pass filter limit | 0 | 0 | 0 |
| - | (not on these pages) | 0x1658 (short) | K+0x496 | Map 3D offset filler passes | 0 | 0 | 0 |
| - | Use Z depths / limits | 0x16c9, 0x1238 / 0x1240 | K+0x464, 0x1c0 / 0x1c8 | Use Zdepths, Z depth limit 1 / 2 | 0 | 0 | 1 |
| - | cut tolerance | 0x12a0 | K+0x220 | Cut tolerance | 0.025 | 0.001 | 0.025 |

Tool of T7 METRIC op 6: D 10, corner radius 5 (record 0x350 / 0x358) - a ball. Defaults read with tools/mcam_ops.py.

### 2.3 What Hybrid's passes are
- Two families, by code: constant-Z cuts from CKcKonstZcutEmm (stepdown, limit angle) and per-region 3D-offset passes from CKcScallopEmm
  (stepover, offset method, flat options), tagged "WATER LINE" / "SCALLOP" (2.1). Which regions get which (the region map's kinds, the
  limit-angle test, Preserve Z passes, the filler mapping, the filters, the cut order): **NOT DECODED** - the Kc engine (mk l.48700-70000
  region, plus FUN_1800520dc l.65746 356 lines) was not read.
- **Measured** (TUTORIAL 7 METRIC op 6, 24328 moves): 3 rapids-separated runs; 23507 sloped feed moves, 812 horizontal; only two Z values
  carry >= 20 horizontal moves (-21.052, -0.5). So this part's output is nearly all 3D-offset passes; it does not show a 1 mm Z-stepdown
  family. No conclusion about the algorithm is drawn from this.

### 2.4 NOT DECODED (Hybrid) - effectively the whole algorithm
The CKcKonstZcutEmm / CKcScallopEmm engines and their region map (emmBothRegionsMap / emmzcuts / emmrigid files); tmpmm5.tmp (the
model they read: who writes it for op 132 - mcmsurf l.119250 / 127621 / 202025 write tmpmm5 for classic ops); FUN_18008c930 (merge),
FUN_18008cbb4 / FUN_18008cdc8 (optimize order), FUN_180088748 (APT output), the threaded path FUN_180086d80; the Imported Linking Plan
0x1475. Estimated size: the Hybrid wrapper is ~204 functions (mk l.94990-103300) plus the Kc engine; a port is a separate multi-session
project (hst3d_port_plan.md step 9 "3+" sessions is an underestimate).

### 2.5 Oracle
TUTORIAL 7 (METRIC 24328 moves / inch 17346 moves), op 6, in the 2017 and X9 copies (scan3d.tsv); fields in the 2.2 table.

## 3. Port (2026-10-26, viewer/cam3d_finish.c gen_hstproject)
- NCI and Curves modes per 1.7: NCI = the source op's cut polylines from this part's toolpath points (its rapids split them), XY only; Curves =
  the op's chains (cam_chain_polyline at the op's tolerance; closed ones back to their start), no containment; each one pass through
  hst_grid_pass (Raster's drop); depth cuts as 1.5.4 (NumberOfOffsets copies raised k x stepdown, k = n .. 0; ByOffset = !Order by depth),
  through HstGrid.zoff. Refused: Points, and Raster's refusals. Ours: as the header comment (frame angle 0, source order, SAFE links).
- Page 1517 (formats/opdlg_fields.tsv s1517): the depth-cut count shown as stored + 1; control 997 (Mastercam's op tree) as a list of the part's
  ops with "(none)" = 0; a record source id that is not an op of this part shows as none (the OK's GetOperationPtr test).
- Scene gen_hstproject (BRACKET4, source op 2): 5225 points. **Unverified against Mastercam** (no stored style 15 op).

## 4. N9-15 update (2026-11-09; audit formats/audit_2026-11-09_highspeed_surface2.md)
- **Points lambda decoded (FUN_18001a470, via start_for vftable 1807600a8 -> FUN_180019940 -> FUN_180019e80; DecompAt)**: per point (x, y): MeasureInterior2D over the ConstraintBoundaries (with EnableTrimToBoundaries) must be > 0; FUN_180222920 drops the tool; FUN_1802f3a80 must be >= 0 (contact); z' = drop z + Eva+0x140 (ToolThicknessZ) within [PosZLo, PosZHi]; FUN_180014810 makes the one-position pass. Ported in gen_hstproject (Points). The op's points (mask 0x40 of GetConvertProjectPts NOT DECODED) = the op's chained point entities (ours); links SAFE (ours). Hand check only: no stored style 15 op.
- EvaConvertToBoundaryPlan (0x1430, FUN_180328c80, vftable 180789248): decompiled (scratch, DecompAt): reads the tessellations (0x1394 / 0x1396 / 0x13df) and BoundaryLinearisationTolerance 0x1a9b, builds via FUN_180492bc0 / FUN_180491ab0, not read further; "XY only" stays the reading of 1.3.2.
