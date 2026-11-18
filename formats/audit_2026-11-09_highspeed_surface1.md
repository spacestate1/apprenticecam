# Audit and fixes: Surface High Speed (op 132), 2026-11-09 (N9-15)

Styles audited: OptiRough, Area Roughing, Waterline, Raster, Scallop, Spiral, Radial. Code: viewer/cam3d_rough.c, viewer/cam3d_finish.c. Citation keys: mk = decomp/mckernel_all.c, hz = decomp/hmm_zplan.c, hp = decomp/hmm_pass0.c, ms = decomp/mcmsurf_all.c, K+x = TPHSTToolPath offset as the kernel reads it (K = TP m - 0x60, record = op + 0x68; the whole map is the mccore copy FUN_180923f60, decomp/proj/d4.c l.60-255, so the record offsets below are no longer "[inferred]").

(Numbers and the commit are filled in section E after the final run.)

## A. Accuracy and behaviour gaps found

| # | where | what we did | what the decompile does | status |
|---|---|---|---|---|
| A1 | cam3d_rough.c gen_optirough | `if (conventional) flags \|= 1` | FUN_180108f1c mk l.205392: K+0x47c == 0 -> \| 1; K+0x47c = record 0x16e4 (1 = Conventional, ms l.96657-96664); flags & 1 = climb in the engine (mcce_dyn.c `p->climb`) | fixed |
| A2 | gen_optirough | order_mode never set (0) | FUN_180108f1c mk l.205385-205391: CDynPockParm +0x20 from K+0x4ea (s16); K+0x4ea = record 0x1660 by FUN_180923f60 (TP m 0x54a = op 0x15f8): 0 -> 1, 1 -> 2, else 0 | fixed |
| Core flag | gen_optirough | 0x50 | mk l.205380: (Core ? 0x30 : 0) + 0x10 = 0x40 | fixed |
| Zigzag | gen_optirough | refused, flag 5 read from byte 0x16f0 | K+0x487 = record 0x1728 (FUN_180923f60) and FUN_180108f1c mk l.205395 sets \| 5; the refusal was stale | fixed |
| A3 | gen_optirough | stock XY as stored | FUN_180071314 mk l.87577-87611: XY = max(XY, Z) in every style but Horizontal (3) | fixed |
| A7 | c3_levels (arrays 400 / 2000) | silent truncation | no cap in FUN_18040da70 | fixed: the level array grows |
| A4 | levels | nominal levels only; a comment said inflexions do not apply to OptiRough | FUN_18040cf60 hz l.12945-12975, FUN_180413d90 hz l.16565-16590 call CZInflexions whenever 0x1df6 (default true, hp l.517-521) | ported, flat facets only (see D) |
| A5 | cam3d_rough / finish containment | offset taken from the help pages | ms l.196433-196458: mcmsurf offsets tmptc.tmp itself, offsetWriteTcb(position, d, tol) with d = 0x12b0 + (0x16c8 ? D / 2 : 0), applied when position != Center and \|d\| > 0.0001; Inside -d, Outside +d | ported as c3_tcb_offset; Raster / Scallop / Spiral / Radial now apply it too |
| A5b | Waterline / Raster / Scallop / Spiral / Radial | "Tool contact point" ignored | K+0x499 = record 0x1730: these styles (mask 0x196) go through Cutter Contact Detection 0x1434 (FUN_18007d7c8 mk l.92875-93074): not decoded | refused with NOT DECODED |
| A6 | OptiRough K+0x485 (0x16ee) | flag only | FUN_180106624 mk l.203234-203236 also grows the job containment +0xe8 by D / 2 and hands it to MCCECalcDynPock as a second boundary list (FUN_1800f7330 mk l.190834) | refused: list not traced; 0 in all defaults and stored ops |
| A8 | Spiral / Radial / Scallop | no PosZ limits, no Flute contact | same evaluator as Raster (18035ec70); Raster's rule ported | fixed (hst_grid_open2 / hst_grid_pass) |
| A9 | Waterline pass order | unlabelled | LinkByZLevel = (0x16c0 == 0); LinkWaterlineDirectionStyle FUN_180075b48 mk l.90215-90240 | labelled: the linker decides (NOT DECODED) |
| A10 | hcell drop (height field) | ours | CToolDropPos: vertex FUN_180112d30, facet FUN_180113220, edge FUN_180113ce0 (hst3d_weave_drop.c) | see E |

## B. Refused or left for later

| item | state of the decompile | size / dependencies | verification available |
|---|---|---|---|
| The linker (all styles; every C*LinkingPlan shares EvaGeneralLinkerPlan FUN_1803a0c50, styles differ only in defaults and macro parameters; FUN_18007d028 mk l.92328, parameters FUN_180075b48 mk l.89806+) | decoded to the call chain only: FUN_1801929d0, 192f30, 193bf0, 194070 (hmm_linker.c) | 132 functions, 25.8k decompiled lines, 18 not dumped (CLinkMovementConnect 1801c8000-1801e2000: 7 of about 349 dumped). Whole reach 310 functions / 35k lines, 84 not dumped. | stored NCI only: rapid counts (TUTORIAL 7 op 5 4, Waterline op 3 114, Scallop op 4 57, PARALLELSTEEP op 5 3) |
| Corner rounding FUN_180557f60 (hmm_vorlib.c l.16248) | decompiled, 675 lines | 32 functions, 2.8k lines closure, all dumped; needs the Voronoi graph FUN_180407c10 (hmm_vorlib.c is 21.9k lines, not ported; area_mill.c is a grid) | TUTORIAL 2 SWARF_METRIC ops 3 and 1 only |
| Scallop 3D path 0x1445 + Shallow Area Detection 0x140a (the default op) | area detect 180316000-18031ea00 not dumped (71 functions, 33 KB) + the 3D boundary first offset | large | TUTORIAL 2 inch op 5, MOLD BOTTLE op 6, FALSE FLOOR op 6 |
| Add cuts (AdaptiveStepdown) | outline FUN_18040cf60 hz l.13030-13140 + FUN_18012cc60 (78 lines, FUN_18012c520 / c880); no Voronoi needed | small port, but it compares two CWeave objects: our slice keeps no fibres, flat tips none at all | Area T2 inch op 1, WATERLINE op 2, MOLD BOTTLE op 1 - left, not done in this pass |
| Stepup | FUN_180411bd0 / FUN_180414620 decoded (plan 4.4), CZInflexionsHorizontal partly | small | none: no stored op has 0x16ec on - left refused |
| Skip pockets | reader found: FUN_1801073c4 mk l.203900 drops a region when K+0x330 (record 0x13e8) > the region's measure at node +0x58; what that measure is (set in the executioner's region table) was not traced | small | TUTORIAL 7 op 5 (22) - left, not applied |
| OptiRough K+0x485 / containment list +0xe8 | see A6 | small | none (0 everywhere) - refused |
| OptiRough Core region | flags 0x40, approach 0.65 D decoded; the executioner's Core region not | see E | TUTORIAL 7 inch op 5 |
| Tool contact containment 0x1730 | plan 0x1434 not decoded | - | none - refused |
| Inflexion vertex heights | ported code path, disabled for our tessellator | needs Mastercam's tessellator | Area op 1 levels |
| mchmm.dll under tools/peloader | **No** today: the loader stops at "delay imports present - not supported"; beyond that mchmm imports 668 mfc120u.dll ordinals (mcce needed few), tbb.dll, COMCTL / COMDLG / OPENGL / GLU. Feasible only as a project of its own (delay imports, an MFC ordinal stub set, tbb) | - | - |

## C. Labelled inferences that the decompile now settles
- Every "[inferred]" record offset of op 132 now has a decompiled source: FUN_180923f60 (decomp/proj/d4.c), K = m - 0x60, record = op + 0x68. 0x1660 = K+0x4ea; 0x1728 = K+0x487; 0x16c1 = K+0x49a (Bottom to top: LinkHeadNorthwards = false in FUN_180075b48); 0x1730 = K+0x499; 0x12f8 = K+0x270 (PassExtension); 0x13e8 = K+0x330.
- OptiRough's depth limits: Use Z depths = K+0x464 with 0x1238 / 0x1240 (mk l.87321-87344), now applied to OptiRough.

## D. Inflexion levels
CZInflexions per level: triangle collection (FUN_1804a2270, FUN_1804a20b0, FUN_1804a2040), interval merge FUN_18005fb40, then FUN_18005ef10 (clear of the set and intervals by Avoidance) or the farthest point FUN_18005f7e0, ported in c3_levels_infl / c3_infl_level; c = Eva +0x140 = ToolThicknessZ (FUN_1802f4c80); the 0.01 cap is in McHmm's mm (the macro's lengths are scaled by job +0x80). Avoidance is the default 0.5 x 0x1bbe for every style: the 0.0001 at mk l.205595 belongs to FUN_18010921c (the Stock Model macro, plan 0x1436); FUN_180109910 does not set it. Applied to flat and near-flat facets only: the vertex and edge steps depend on the tessellator, and with our OCCT mesh they moved levels that Mastercam leaves exact (TUTORIAL 2 SWARF_METRIC op 3: with vertices 39.629 for 39.626 and p50 0.034 -> 0.066; with mesh edges alone 28.829 for 28.826). With flats only the 24 levels are exact again. The remaining Area Roughing drift (Mastercam 36.401, ours 36.394) needs Mastercam's own tessellation and stays open.

## E. Fixes and numbers (per step; oracle = ref->ours p95 / ours->ref p95 / cut length, ref length in brackets)

| step | OptiRough TUTORIAL 7 METRIC op 5 (mm) | inch op 5 (in) | Waterline op 3 cut-only (mm) | Raster PARALLELSTEEP op 5 (in) | Raster RESTPASSES op 18 cut-only (in) | Scallop op 4 z <= 12 (mm) | Area op 1, level 20.8 (mm) |
|---|---|---|---|---|---|---|---|
| baseline a14b689 | 1.050 / 1.009 / 23179 (21522) | 0.066 / 0.071 / 797 (778) | 0.478 / 10.880 / 11120 (10239) | 0.015 / 0.036 / 3148 (2543) | 0.012 / 0.050 / 971 (978) | 0.539 / 0.490 / 8203 (8728) | 1.608 / 1.613 |
| 1: A1, A2, A3, A7, Core flag | 1.028 / 1.038 / 21652 | 0.066 / 0.070 / 792 | same | same | same | same | same |
| 2: inflexions, flats only (vertex version not kept: p50 0.034 -> 0.066) | same | same | same | same | same | same | same |
| 3: containment, Flute / Z limits on grid ops | same | same | 0.481 / 10.864 / 11120 | same | same | same | same |
| 4: exact drop (CToolDropPos) | - | - | - | 0.015 / 0.037 / 3137 | 0.015 / 0.050 / 966 (with MachTrimZTolerance below: 0.010 / 0.050 / 977) | 0.497 / 0.492 / 8319 | - |
| 4b: step rule FUN_1803612d0: tried, not kept (Raster p95 0.015 -> 0.019, ours->ref 0.036 -> 0.058) | | | | | | | |
| 5: OptiRough Use Z depths; Core flag tried, not kept (inch p95 0.090 / 0.400, length 463); Pocket stand-in kept | | 0.043 / 0.043 / 766 | | | | | |
| weave Z = level - c (FUN_18040cf60 hz l.13009, FUN_180413d90 hz l.16662, FUN_1801295b0) | 1.030 / 1.050 / 21364 | 0.043 / 0.043 / 786 | 0.481 / 13.295 / 11507 | - | - | - | 1.608 / 1.613 |
| **final** | **1.030 / 1.050 / 21364** | **0.043 / 0.043 / 786** | **0.478 / 13.356 / 11507** | **0.013 / 0.037 / 3154** | **0.010 / 0.050 / 977** | **0.497 / 0.492 / 8319** | **1.608 / 1.613** |

Direction test on TUTORIAL 7 METRIC op 5 (tangent agreement within 1 mm, same / opposite): baseline 236 / 556, after A1 543 / 208.

Worse numbers, explained:
- OptiRough ours->ref p95 1.009 -> 1.038 (A1): the path now runs the right way; ref->ours and the cut length improve and the direction test proves it.
- Waterline ours->ref p95 10.88 -> 13.36, cut length 11120 -> 11507 (weave Z = level - c): only level 12.05 changes (per level: ref 239, before 377, after 789; every level above is identical): the floor within the 1 mm stock is now in reach, as the decompile says, and adds the shallow-floor rings Mastercam leaves out by the rule that is still NOT DECODED (hst3d_port_plan.md 7, Investigation 2026-11-08). To keep the old figure, the three `z - sz` in c3_slice's callers are the only change to undo.
- Raster RESTPASSES: 0.012 -> 0.015 with the drop alone, because the height field's Z limit allowance had been removed; the decoded MachTrimZTolerance (mk l.88128-88134: job +0xa0 when below 0.1 mm / 0.00394 in) restores it, 0.010.
- The stored Scallop op with its own Use Z depths (no longer refused): cut-only 0.500 / 0.480, length 8319 (ref 8625); before: refused.

Raster's 3148 vs 2543 was not the height field's Z steps: with the exact drop it stays 3154. The excess is the SAFE links' plunges at feed (the cut-only length is 2524-2543). A10's remaining sampling constant (hcell) stays ours: the decoded split rule made the oracle worse.

Scenes (OP_SCENES = the 10 gen_* 3D scenes): 7 / 12 differ. 6 blessed, intended, BRACKET4 point counts only: Area Roughing 21127 -> 20813 (weave Z, Use Z depths unaffected), Waterline 2742 -> 3266 (weave Z), Raster 16011 -> 14379 (exact drop, Flute contact limits), Scallop 62294 -> 32169, Spiral 2511 -> 1034, Radial 4759 -> 2530 (Flute contact removes the off-model positions, exact drop). Not blessed: gen_optirough.gen_optirough.mcamui differs only by a "PROJECT-FORMAT 2" line, one of the known *.mcamui differences of the base branch (oracle_history.tsv: op_compare known 46), not from this work. units_test: ALL PASS (33).

## F. Independent review (after 67be3a4), each item checked in the decompile

1. **Vanished containment.** offsetWriteTcb returns 0 < count (mcmill l.322700+), FUN_180128390 then posts warning 0x127 (ms l.196459-196461) and returns an error (FUN_180268e20(0xfffffff0)); its caller at ms l.196578 sees sVar1 != 0, so no toolpath is made. Ported: c3_tcb_offset returns 0 and the op is refused with mcmsurfres 295, "Containment offset failed; it may be too large.  Try <center> containment option" (OptiRough, Waterline, Area Roughing, Raster, hst_grid_open2 / hst_grid_open). The offset tolerance is record 0x12a0 (offsetWriteTcb's third argument = op +0x1238). Inward by at least half the box's smaller side is tested first: Clipper's integers overflow on absurd distances. Test: tools/tests/hst3d_tcb_vanish.sh.
2. **Avoidance.** Correct: the 0.0001 at mk l.205595 belongs to FUN_18010921c (Stock Model macro, plan 0x1436). FUN_180109910 does not set it; OptiRough keeps the default 0.5 x 0x1bbe (hp l.533-577). Fixed in the code and section D.
3. **c3_levels_infl.** Cancellable (mcce_abort_get per level). A level that does not descend is refused with a message: the decompile's loop would not end on it, and no cap was added.
4. **Z limits on the grid passes.** Decoded: FUN_180361bd0 (raster/grid.c l.2266) with the predicate FUN_1802f3a80. A position counts when its z + Eva +0x140 (our tip Z) lies in [PosZLo, PosZHi] (+0x6c0 / +0x6c8), or in the window widened by MachTrimZTolerance (+0x6d0 / +0x6d8, set in FUN_1802f2340) when a stretch has at least one position in the tight one; other positions are removed, never clamped. So the clamp, and the Flute-only removal, were wrong: `hst_z_filter`, used by Raster and hst_grid_pass (Spiral, Radial, Scallop). The contact-layer test +0x6e0 concerns the tool's upper layers only. The end-point refinement (the second loop of FUN_180361bd0) is not ported. Use Z depths is justified by this decode. Flute contact off (0x16cf = 0) is now refused for these styles: the height a drop falls to off the model is the toolbox floor (+0x450), not decoded (every default op has 0x16cf = 1, both libraries). Scallop's `tz` field is not clamped: Mastercam's contours are cut at PosZHi / PosZLo - c inside the weave (scallop/off.c l.4339-4342); here the passes go through hst_z_filter.
5. **OptiRough Core.** Stated plainly in the code: `v.core` is forced to 0 before P.flags, so the 0x40 flag and the approach cannot run.
6. Settled or left:
   - FUN_1800a4ce0(plan) == 7 (LinkerToolThickness for Eva +0x140): **not settled**. The plan-to-class map is filled at run time, and I could not find its registration. The parameter names point to linking plans; 0x1b70 was not traced for the passes plans. Not changed.
   - `+0x90 != 0` tip branch of FUN_180112d30 / FUN_180113220: **not applicable to hd_tip**. Its setter FUN_180141690 is called only from FUN_180151a20, FUN_18031b210, FUN_18031e170, FUN_18035ce60 (pencil / area detect / edit-passes ranges by address), none on the grid-plan path.
   - The CWeave box: **not the containment box**: FUN_180271070 (hz l.12944) sets it from the weave's fibre interval extents grown by the tool radius at the top height. Our inflexion step uses the containment box, labelled ours; only flats outside it would differ.
   - Vertex and edge inflexion steps: **left off, not ported**. They depend on the tessellator; with our mesh they move levels Mastercam leaves exact (section D). Refusing them would refuse every op: a decision for the user.
   - 0x16cf: **on in every default op** (Mill_MM and Mill_Inch, every style).
   - Waterline's extra rings at level 12.05: **not traced**. Unchanged: ref 239, ours 789.
7. **Shared code.** The containment offset of c3_tcb_offset also applies to Pencil and Project in NCI mode, through hst_grid_open (a non-Center tool containment now offsets their region too, or refuses their op with the message above); both are otherwise unchanged, by the scenes.

Checks after these fixes (mm unless noted): OptiRough metric 1.030 / 1.050 / 21364; inch 0.043 / 0.043 / 786 (in); Waterline op 3 cut-only 0.478 / 13.356 / 11507, levels as Mastercam's; Raster PARALLELSTEEP 0.013 / 0.037 / 3154 (in); RESTPASSES op 18 cut-only 0.010 / 0.050 / 977 (in); Scallop 0.497 / 0.492 / 8319; Area 1.608 / 1.613; Pencil (PENCILSEAMS op 1, offsets off) ours->ref p95 0.0077 in. Project: gen_hstproject unchanged (5225 pts). hst3d_tcb_vanish.sh: 4 / 4 PASS. units_test ALL PASS (33). 3D scenes: 1 / 12 differ (gen_optirough .mcamui, the base branch's known line), nothing blessed.
