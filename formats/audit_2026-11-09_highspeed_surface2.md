# Audit and fixes: Surface High Speed, part 2 (Pencil, Blend, Horizontal Area, Project, Hybrid) - 2026-11-09, decision N9-15

Citation keys as the specs: mk = decomp/mckernel_all.c, ms = decomp/mcmsurf_all.c, hz = decomp/hmm_zplan.c, pn / pp = decomp/pencil/pnet.c / pplan.c, gA = decomp/horizarea/d1.c,
cs = decomp/scallop/cs.c, hw = decomp/hst3d_weave_drop.c, h3 = decomp/proj/h3.c. DLL addresses are mchmm.dll / mcmsurf.dll image addresses (objdump / .pdata / DecompAt on a scratch copy of ghidra_hmm).

## A. Accuracy and behaviour gaps

| # | Where | What | Decompile | Status |
|---|---|---|---|---|
| A1 | viewer/cam3d_pencil.c (the plan defaults) | mm constants (MinHorizontalChange 0.003, Cell min(2.5, D), Vstep 0.5 / 2.0, MinFragLength 0.1) used raw on inch parts | job +0x80 = 25.4 inch (FUN_18007bb48 mk l.91625-91633) through FUN_1800edd8c: McHmm in mm | FIXED: scaled by part units per mm |
| A2 | cam3d_pencil.c:524-ish | pass Z left out ToolThicknessZ | FUN_180150e90 pn l.1189-1346 adds (float)Eva+0x140; Eva+0x140 = 0x1d20 ToolThicknessZ (FUN_1802f4c80, stored at 1802f2cff, FUN_1802f2340) | FIXED |
| A3 | cam3d_horiz.c (flats, weaves, cut height) | Z of flats / slices / passes ignored c | hz l.15327, l.15470-15510, l.14945, l.14957, l.15209, FUN_180406b00 param_7 -> pass object +0x5c0 (hz l.8985) | FIXED, stock scene added |
| A4 | cam3d_horiz.c depth cuts | level by level | ByOffset true by default (gA l.404-414, mckernel writes only NumberOfOffsets / Stepdown, mk l.93456-93500): per pass (gA l.880-890); up: FUN_18031f160 -> FUN_1804a7c60 h3 l.584 / l.723 | FIXED |
| A5 | cam3d_pencil.c midpoint guard, no-contact points | an unlabelled 4 h^2 guard | FUN_180150b70 pn l.1046-1188: the cons test alone | FIXED (no-contact point filter labelled ours); hst_grid_pass PosZ clamp left to the agent fixing it |
| A6 | blend.md, comments | Across "NOT DECODED", cut-method location "not found" | FUN_180031610 l.33877-33957; FUN_1800307f0 l.33213; FUN_1800bd990 l.124184 | FIXED (B) |

Checked and matching: the Pencil step tests d20 / f30 / cons (st), cell split FUN_180161570, thinning, convex filter FUN_18015c020 pn l.9086-9222; the Blend record block, pass count, chain step, short-gap value.

## B. Refused or not ported, status after this pass

| Feature | Status |
|---|---|
| Blend Across | PORTED (FUN_180031610). Hand check SWARF op 2 (no stored Across op): 851 passes = rulings, ref->ours p50 0.0039 (Mastercam's Along passes lie on the same drive). Unverified against Mastercam |
| Blend Spiral | PORTED (FUN_1800c53a0): hand check SWARF op 2. Unverified |
| Blend Zigzag | now decoded, FUN_1800307f0 (no behaviour change) |
| Blend filter FUN_1800c7390 | PORTED on the Along 3D rulings, tol / 10 rad (l.33695) |
| Blend long gap link | FUN_180140d80 l.209853 picks the plane (retract 0x189, or clearance 0x179 when retract is off, or neither); FUN_180142ee0 / FUN_180142420: plane = max(height, feed plane), absolute / incremental heights (0x178 / 0x188 / 0x198), rapid retract 0x1a9 and rate 0x388, gap check 0x12a3 (from the record, so app and blend_test agree); no corpus case, ten unit cases in blend_test --gap-cases |
| Blend short-gap types other than Broken | REFUSED: FUN_180140d80 only writes tags 0x10005 / 0x10006 / 0x10008 + FUN_1801417c0's segment; the geometry is built by a later stage, NOT DECODED |
| Blend projection FUN_18028e8b0 -> FUN_18028be40 (137) -> FUN_18028ca50 (968), FUN_18028e990 (163), FUN_18028eef0 (133), FUN_18028f420 (260); default path FUN_180278890 (116) over FUN_180133f10 (974) | NOT PORTED: needs the classic surface / EMM model; the labelled ball drop stays |
| Pencil pass object FUN_180118ee0 / FUN_1801194d0 / FUN_180121900 | decoded: straight moves are Mastercam's (they record points, fragments split where FUN_18015e160's flag changes); the "ours" label is removed |
| Pencil cell types | LEFT labelled: FUN_180131810 -> FUN_180289d90 = MeasureRadSegs2D of the fibre against Eva+0x118's segments; which set that is (Surfaces / boundaries) NOT DECODED |
| Pencil Overthickness | REFUSED: FUN_180162eb0 (about 400 lines) + FUN_18015e210 + FUN_18013e330 and the refined marks: not small, no stored op with it |
| Pencil trim FUN_1802ed680 (520 lines, FUN_180275a80, FUN_1802780e0, FUN_1803581a0, FUN_180276d70) | LEFT: the containment is still the point test (labelled) |
| Pencil offsets 0x140d | NOT PORTED, by instruction: cs FUN_180337970 (1206 bytes) + FUN_180337e30 (4720 bytes) + the Scallop offset loop FUN_1803343e0 (7438 bytes, cs l.794-1881) and its weave distance field (scallop/off.c); oracle: PENCILSEAMS op 1 as stored |
| Pencil linker 0x1442 | NOT PORTED, by instruction: the general linker, decomp/hmm_linker.c 38 870 lines, hmm_link.c 23 174 |
| Horizontal zA window FUN_1804a1e10 | LEFT: FUN_18005ef10 / FUN_18005f7e0 not read, the window is 1e-4 wide |
| Horizontal seven extra pockets | NOT DECODED: FUN_18040b6f0's keep test is FUN_1801344c0 (the contact's tool segment > Eva+0x6e0) + the contact-angle window; not traced to a region drop. They remain cut |
| Project Points | PORTED: FUN_18001a470 (DecompAt). Hand check on RASTER.mcam's solid: tip 1.40000 at (0.0017, -0.0654) on the boss top (the decoded plane Z), + stock 0.02 = 1.42000, 2 cuts sd 0.05 = 1.45 then 1.40, a point off the drive refused. Unverified against Mastercam |
| Project NCI plan 0x1430 | decompiled (scratch): FUN_180328c80 (1764 bytes) reads the tessellations and 0x1a9b, creates through FUN_180492bc0 / FUN_180491ab0; not ported further |
| Hybrid | NOT PORTED, by instruction: ~204 wrapper functions (mk l.94990-103300) + the Kc engine (7 classes); oracle TUTORIAL 7 op 6 |

## C. Inferences replaced

Eva+0x140 ("c", port plan 4.4, ha D3, pencil 5.5): ToolThicknessZ. cam3d_horiz.c "up [inferred]": decoded (h3 l.584 / l.723). Pencil straight moves: decoded. Zigzag (Blend): decoded.

## D. Checks (before -> after)

| Check | Before | After |
|---|---|---|
| Pencil oracle (test sampling 0.01): ours->ref p50 / p95 / max | 0.0032 / 0.0071 / 0.0097 | 0.0030 / 0.0077 / 0.0097 |
| Pencil against ref sampled every 0.002 (pass vertices): p50 / p95 | 0.0011 / 0.0027 | 0.0006 / 0.0010 |
| Pencil oracle gouge (--dump) | 0.0079 | 0.0056 |
| Pencil stock 0.01 (--rec 13c0, 13c8): lowest Z | - | 0.0100 |
| Horizontal oracle (8 levels), median p50 (stock 0) | 0.0017 | 0.0017 (identical per level) |
| Horizontal stock 0.01: level Z | - | every level + 0.01 |
| Blend oracle (6 ops): ref->ours p95 SWARF / metric / BOTTLE / 5 AXIS | 0.0006 / 0.0149 / 0.0006 / 0.0010 | identical |

The Pencil p95 at the test's 0.01 sampling rose 0.0006; the ref sampled 5x finer shows p95 0.0026 -> 0.0017 and every vertex closer: the 0.01 sampling of Mastercam's own moves is the part that is coarse.

| Scene (op_compare subset, offscreen) | Before | After |
|---|---|---|
| gen_pencil (BRACKET4, metric, default 1 mm stock) | golden | identical (only point counts are in the golden; Z moved by the stock) |
| gen_hstproject | golden | identical |
| gen_horizarea (default stock 1 mm) | 2332 points | 2660 points: BLESSED, intended (A3: flats at model + stock, weaves at the weave Z) |
| gen_blend | golden diff, msg "Translate" vs "" in opstate lines (a UI message from the scene's Translate step, not the Blend engine; not compared with a baseline build, so whether it predates this branch is not checked) | unchanged, not blessed |
| gen_blend_across / gen_blend_spiral (new) | - | 430 / 321 points (Blend 164), blessed as new scenes, unverified against Mastercam |

units_test: ALL PASS (33).

## Unsettled

- ToolThicknessZ, depth order and the units fix have no oracle with stock / inch stock variation other than the hand checks above and RASTER / PENCILSEAMS at stock 0.
- c3_slice's other callers (cam3d_rough.c, cam3d_finish.c waterline) pass the tip Z as the weave Z (ztip), so a triangle wholly below the tip plane but within the stock's reach is skipped; the same correction (z - c) likely applies there (for the agent fixing those files).

## Review of 2026-11-09 (second pass, after `git merge toolpaths2`)

| Item | Result |
|---|---|
| Long-gap plane (FUN_180140d80 l.209853, FUN_180142420, FUN_180142ee0) | CONFIRMED and fixed: the plane is retract (0x189) unless retract is off and clearance on (0x179); plane = max(height, feed height); absolute / incremental heights, gap check 0x12a3, retract rate. Heights read from the record (sr_record's patches included), so app and blend_test agree and the -1e30 sentinel of CamParams is not used; the uncited clamp at cam2d.c is removed. blend_test --gap-cases: 10 cases, one per input |
| Project Points `noff` | CONFIRMED and fixed (validated before use) |
| Spiral gate FUN_1800c53a0 l.129494 | CONFIRMED and fixed: L >= tol_exref |
| Pencil dump gouge with stock | CONFIRMED and fixed: both sides compared at the tip with ToolThicknessZ |
| FUN_1801194d0 | CONFIRMED: counts changes of the flag (hw l.5940), does not end a fragment; comment and pencil.md corrected; duplicate skip FUN_18002d030 (exact equality, mchmm; the copy in decomp/surfcreate/prims.c is another DLL) added: gen_pencil 163 -> 151 points (12 duplicates on BRACKET4) |
| Project Points tests | SETTLED: the first (z > floor) is the toolbox drop floor (FUN_180222920 at toolbox +0x450); the second [PosZLo, PosZHi] ported in gen_hstproject; FUN_1802f3a80's Z and contact windows hold at the limits hst_grid_open accepts, its segment filter +0x6e0 and holder checks not ported (labelled) |
| FUN_180150e90 midpoint gate net+0x360 == 0 | LABELLED: PencilOverthickness above 1e-9 is refused |
| local_868 | PORTED (Rc < StepMachTolerance): added to the thicknesses, and PencilOverthickness now uses job +0xa0 (mk l.87625-87655, l.87400-87407) |
