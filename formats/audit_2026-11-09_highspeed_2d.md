# Audit 2026-11-09: 2D High Speed provenance and gaps (N9-15)

Scope: Dynamic Mill, Area Mill (Core Roughing refused), Peel Mill, Dynamic Contour. Code: viewer/cam2d_dyn.c, cam2d_area.c, area_mill.c, mcce_dyn.c. Decompile files under decomp/. Citations: "hst2d l.N" = decomp/mcmill_hst2d.c, "dyn l.N" = decomp/mcmill_hst2d_dyn.c, "mm l.N" = decomp/mcmill_all.c.

Corpus (41 stored 2D HST ops, user parts): 37 Dynamic Mill (32 from outside; 4 stay inside with record 0x17b8 = 1: BRACKET4 and bracket4-dashed ops 14 / 16; 1 stay inside with 0x17b8 = 0: NAKTO 2), 3 Area Clearance (buckle-v10/11/12 op 2), 1 Core Roughing (NAKTO 3). Zero Peel, Dynamic Contour, finish passes, open chains; only chain roles 0 and 4; no chains at different Z. No internet corpus part holds a usable 2D HST op (`HS Dynamic.mcam`'s records parse as garbage and were not analysed).

**Framing.** Port p95 0.194 against mcce.dll on the same input 0.070 (before this work) is engine-port difference, not input. Only what stays inside the DLL's own figures can be input or the X9-versus-2017 version difference.

## A. Input and engine gaps found (status after this work)

| # | Gap | Decompile | Status |
|---|---|---|---|
| 1 | S2-S4 / tool-centre region sets are Clipper polygons (mcce_dyn.c region_sets) | FUN_180062a70 | Left opt-in: see section E, step 3 |
| 2 | Stay inside improvised: even-odd union, islands subtracted, Clipper inset, groups | PackageChains hst2d l.5005-5080 (each role-0 bound alone; HasOuterPocket l.5068 has no effect on the region - corrected 2026-11-13: it sets a nested region's top of stock, dynamic.md "Nested regions"), FUN_1801ac7e0 l.17281-17321, l.17329-17416, l.17477-17522, every island in every work item l.5787, S1 = islands then region dyn l.80-99 | Fixed |
| 3 | 0x17b8 rest material | PackageChains l.5302-5321, FUN_1802b77a0 hst2d l.14854, FUN_1802cb260 mm l.486926, FUN_1802a94f0, FUN_1802a6280, FUN_1802a70d0 dyn l.483-500 | Ported for source 0 (every earlier op), section G; sources 1 / 2 and the group filter refused |
| 4 | Chain roles 5, 6, 7 cut as machining regions | PrepareChains hst2d l.6152-6156, PackageChains l.4739-4835 | Refused for styles 0 / 1 |
| 5 | Open chains silently dropped | PrepareChainsReview l.6295-6325 (open avoidance: warning 4 and stop), PrepareOpenMachPockBounds l.6337, OpenClosedMachPockBounds l.4062, CloseOpenMachPockBounds l.1946 | Refused |
| 6 | Island depths silently ignored | SetParams l.9303 (+0x56 = (0x868 && 0x892) \|\| 0x1740), PackageSomething l.5669-5702, per-level FUN_1801b33b0 | Refused |
| 7 | One depth list for all regions | FUN_1802ca5f0(dc, bound z) PackageChains l.5080, FUN_1802ca790 mm l.486527 | Fixed |
| 8 | Negative wall stock keeps wrong pieces | FUN_1801ac7e0 l.17329-17369 (stay inside: largest), l.17371-17416 (from outside: all) | Fixed |
| 9 | Bound filters (length below tolerance, record 0x1288) absent | ConvertChainsToPockBounds l.2337 | Fixed (machining and avoidance) |
| 10 | Self-overlap error (LogError 1) absent | CheckPockBoundsForSelfOverlap hst2d l.1469, mcce::IsBndSelfCoin -> FUN_180128670 (221 lines, kernels FUN_18011f2e0, FUN_180123bc0) | Left (kernels unported) |
| 11 | Entry method 5 | SetOperationID hst2d l.9197 (+0x57 for style 0, record 0x16bb = 5), PrepareChainsReview l.6315-6323 LogError(4), PrepareChains l.6160-6163 stops; message mcmillres 0x13c | Refused with Mastercam's error (corrected in review: an earlier version cleared the flag and generated) |
| 12 | Feed override and tool-type radius refinement not applied | FUN_18029cb90 dyn l.112-125, FUN_1801afa30 dyn l.4424 | Refused (override body is an import; tool types 0x17 / 0x19 / 0xc / 0x15 need the tool record) |
| 13 | Area Mill: several machining chains, trochoid 0x15ce, profile smoothing 0x1566 | PackageChains; FUN_1802a1670 mm l.460139; hmm-vorlib FUN_180557f60 | Refused |
| 14 | Peel / Dynamic Contour wall stock by Clipper | FUN_1801ac7e0 l.17477-17522 (Peel, 0x1567 forced 0), l.17528-17545 (style 2: MCCECalcContourOffset 0x105) | Fixed |
| 15 | Roles other than 0 on Dynamic Contour / Peel | FUN_1801b58f0 reads chain set 0 only (hst2d l.11331); PrepareChains guards the rest by style < 2 | Fixed: skipped, not refused |

Ruled out by experiment: the region's direction (the forced CCW is gone, chain direction kept) and S1 order / loop start. DLL on input variants (tramming-rod 1, v12 23 / 17, v6 35 / 32, v8 42, NAKTO 2): p95 unchanged except `rev_island` on NAKTO 2 (0.416 to 0.466, worse).

## B. Refused or not ported (for the user)

| Feature | Decompiled, size | Dependencies | Verification available |
|---|---|---|---|
| Rest material sources 1 / 2 and the group filter (records 0x16fc = 1 / 2, 0x16fe) | FUN_1802cb260 source 1, FUN_1802cbdd0 (301 lines, MCCECalcFinToolBurial) | | none stored |
| Area Mill trim to rest (0x17b8, mode 4) | kernel plan Edit To Boundaries 0x1416 | McHmm | none stored |
| Core Roughing | passes = Area Clearance evaluator; expansion helpers read (134 lines, links only) | Pass region and FUN_1802ef450 trim NOT DECODED (hst3d_port_plan ar 4); McHmm linker unported (hmm_linker.c, 37k lines) | NAKTO 3 NCI, one op |
| Peel style bnci_peel | yes, 1280 lines, unread | contour writer FUN_180056be0 (ported), FUN_1801ba1c0 / ba2c0 | none |
| Finish passes FUN_180094540 | yes, 367 lines | FUN_180095000 (116, unread), FUN_180094fb0, FUN_180053e70 and FUN_1800518b0 (in cam2d.c) | none; gouge checks only |
| Tapered walls | FUN_180050910 (26), FUN_18029f110 (54), FUN_1801b33b0 (1096, unread) | | none |
| Dynamic Contour open chains | FUN_1800930c0 (60), 918c0 (147), 914a0 (130), band kernels decoded in mcce_engines.md 6.5 | contour offset port (modes 0x125 / 0x100 untested) | DLL on synthetic only |
| Open machining / air / containment / entry chains | CloseOpen*, TraceOpenPockBound, BuildAirEntryPockBound: about 2000 lines, unread | | none |
| Area linker, Dynamic entry methods 4 / 5 | as before | | |

## C. Labelled inferences replaced

Done (comments corrected to the decompile): Dynamic Contour stock sign (l.17528-17545), Peel stock (l.17477-17522), mcmill island filter (l.5669-5702 now cited and refused), LinkStart source (FUN_1802b8d50 l.18218), expansion helpers (mm l.464506-464640), tool refinement (dyn l.4424), method 5 clean-up message, stale cam2d_area.c header, "kind 1" Core note (kind 1 is OptiRough).

Left as labelled ours: `peel_side` (ccomp_pock is decomp/mccore_pock.c l.485, core FUN_1805d8690 not sized), grid medial axis area_mill.c, SAFE rule in mcce_moves and Area links.

## D. Proposed order after this work

1. Done (section G): rest material S2.
2. Exact S2-S4 default once the op-10 stall is found (see E, step 3).
3. Residual DLL-versus-NCI on v8/v9 42, v10-12 10, NAKTO 2.
4. Dynamic Contour open chains; Core Roughing region decode; Peel style, finish passes, tapered walls are the user's choice (no oracle).

UNSETTLED: NAKTO 2 cause (DLL 0.416, first move about 0.96 mm off); the X9-versus-2017 difference; tool types of the stored ops (no stored op refused by the tool-type check).

## E. What was done (branch hs2d)

Dynamic oracle (`ORACLE_FILEOP=1 tools/oracle.py --type dynamic`, 37 ops, p95 of the worse direction; 0x17b8 ops included with the comparison switch `MCCE_TRIM_NOS2=1`, ours, tests only):

| Step | median | worst | notes |
|---|---|---|---|
| base (toolpaths2 a14b689) | 0.194 | 0.643 | |
| step 1 refusals | 0.194 | 0.643 | all 41 byte-identical with the switch |
| step 2 decoded input | 0.111 | 0.643 | 13 ops better by more than 0.005, 6 worse, see below |
| step 3 MCCE_EXSETS=1 (opt-in) | 0.075 | 10.053 | v11/v12 op 10 stalls |
| Area (3 ops) | 0.914 | 0.928 | unchanged |

Step 1: refusals tested on copies with the role byte patched and `--rec` patches (roles 5 / 6 / 7, 0x1740, 0x17b8, 0x1700, 0x15ce, 0x1566) and open chain / several chains on Area through `cam_cli`: each prints its NOT DECODED / refusal line. Without the switch the 4 BRACKET4 ops refuse; `op->error` keeps the stored NCI.

Default state (without the switch: BRACKET4 family refused): 33 ops, median 0.202, worst 0.643; the 4 refused ops are among the best, so the median of the remaining ops is higher, the ops are not worse.

Step 2 per-op p95 changes above 0.005 (before -> after): v10 1 0.107 -> 0.069, v11 1 0.100 -> 0.054, v12 23 0.117 -> 0.055, v6/7/8 33 0.204 -> 0.057, v6/7 36 0.169 -> 0.111, v9 42 / v8 42 0.47 -> 0.437, v11/12 10 0.431 -> 0.332; worse: v10 10 0.233 -> 0.437, v6/7 31 0.059 -> 0.111, v8 31 0.059 -> 0.069. The exact offset of the region and the chain direction drive these.

DLL oracle (`tools/tests/mcce_dll_oracle.sh -n`, `MCCE_REG=bUseCtourGuide=1`, 37 ops, after step 2): port median 0.111 worst 0.643; DLL on the new input median 0.062 worst 0.435 (before: 0.070 / 0.434); DLL better than the port by more than 0.02 on 22 ops, port better on 1. The worst DLL figure stays at v8/v9 42: input not explained.

Step 3: `MCCE_EXSETS=1` improves the median to 0.075 and is better on v11/12 17, v6/7/8 31, v10 1; the stall is v11/v12 op 10: 87 % missed, the engine loops on a zero-length step to one target and leaves the region at `iProg + 32 < i` (the no-progress test of the region loop in viewer/mcce_dyn.c): `state01 -> 2: step L len 0.000 flags 8`, the same 5-entity link repeated, the target at (52.038, 10.864) unchanged. Dropping one-entity slivers in S2 made no difference. Root cause not reached: needs a per-step DLL comparison at that commit. Left opt-in; `MCCE_EXTCR` was not re-measured (run timed out).

Step 4: Core Roughing stays refused: the pass region and the FUN_1802ef450 trim are NOT DECODED (ar 4); the 3D port's box is labelled ours and measured on surfaces, which is not this input.

Connector unit oracle (`tools/tests/mcce_ct_oracle.sh`, buckle-v12 3 / 22, v6 32 / 36, v9 51, v10 10, v8 42): 146 connector calls, connector + cross equal 146, final link identical 108 (previously 130 / 131 and 95 / 131 on a different set of calls: the input changed).

Checks: contour oracle (149 ops, default sampling) median 0.047 worst 0.065 (no Contour code changed); units_test ALL PASS; op scenes (Dynamic / Peel / Area subset, 49 outputs) 10 differ before bless: 8 blessed (below), 2 are the pre-existing `PROJECT-FORMAT 2` header of gen_area_mill / gen_peel_mill and left; chain scenes 0/5 differ.

Blessed references (intended: the exact wall offset or the contour offset port changes the moves):

| Scene | before -> after points |
|---|---|
| dlg_dynamic (.txt, .nc) | 7594 -> 7595 |
| rt_dctour_b9_switch | 930 -> 874 |
| rt_peel_hst | 6099 -> 6159 |
| rt_peel_dec2_ext (.txt, .nc) | 6593 -> 6605 |
| rt_peel_dec2_v (.txt, .nc) | 7030 -> 6990 |

## F. Review round on hs2d

Decompile confirmed for each item below unless noted.

1. Entry method 5: refused, table item 11. `cam_cli ... --rec 16bb=b:5` prints Mastercam's text.
2. `avoid` versus `role`: `hst_role` (cam2d_dyn.c) reads both everywhere; project files write `R<n>` for roles 5 / 6 / 7 and read it back; `cam_cli a:` sets role 4; mcam_saveops.c writes the stored role.
3. Per-bound Z: depth and top on `Zr + (value - dep_cz)` as cam2d.c gen_contour3d; feed plane absolute when record 0x198 is off; clearance and retract incremental on that top; the two feed-plane clamps (FUN_1802ca790 mm l.486563-486578); the final retract uses the last region's clearance. No stored op has differing Z.
4. Peel: one engine call per wall-stock piece, then the level copies.
5. `mcce_wall_region`: pock_ccw's signed value compared as decompiled (mccore_pock.c l.2424: negative for counter-clockwise). A DLL test of FUN_1801ac7e0's choice was not possible (mcmill not under peloader; only the orientation of MCCECalcOffset is checked).
6. Label: the wall offsets run on chords (`bnds_to_sl`), Mastercam's on lines and arcs: comment at `wall_dummy`, and this note.
7. Settled: override predicate = (b3 && b2) || (b0 && b1) at record 0x1700 (mccore 0x1806c85b0); Area is exempt from the tool-type refusal (FUN_18029d3e0 does not call FUN_1801af380); no tool found leaves tool_type 0; a chain with trimmed ends is refused by the wall stock offset; the 0x105 sign matches the DLL on a CCW rectangle, d = +1 inset, -1 outset, entities identical to the port (`peloader --coff`).

Engine (v10 op 10): the per-step comparison finds the first divergence at the entry (commit 0, 0.039 mm). With `MCCE_EXSETS=1` the entry is identical to the DLL and the first divergence moves to record 450 / commit about 440: the DLL commits an arc step r 1.5, the port the line of `choose_refined` (FUN_1800167d0 mcce_all.c l.17744-17893, whose conditions match the port); the committed widths differ by 0.0007 mm. Inferred (not proven): the difference is in the trial measures, not the decision. Not fixed.

Exact sets stall (v11 / v12 op 10): same first divergence (commit 440, L against A), then a 5-entity link repeats until the no-progress test (`iProg + 32 < i`). Still opt-in.

Rest material note: per the review, the DLL with no S2 matches the NCI on all 4 BRACKET4 ops; S2 equal to the whole region gives byte-identical DLL output; a small square S2 changes it. The 4 refusals stand pending the user's decision.

Checks after this round: stored ops byte-identical to the previous round (0 of 41 differ, with the switch); Dynamic oracle 37 ops 0.111 / 0.643; DLL oracle port 0.111 / 0.643, DLL 0.062 / 0.435; connector oracle unchanged in form (7 ops, connector + cross equal on every call); units_test ALL PASS; Contour oracle at ORACLE_STEP=0.01: 149 ops, median 0.004, worst 0.010, ok 149 / 149; op scenes 2 / 49 differ (the existing `PROJECT-FORMAT 2` header lines only, not blessed).

## G. Rest material S2 (N9-18)

Ported in viewer/cam2d_dyn.c (`hst_rest_level`, per depth, reusing the pbmap port cam2d_remach.c and the earlier-op replay of Remachining): FUN_1802cb260 for source 0, the outside band with this+0x378 = 2 D (from outside), the allowance this+0x380 (0x16f0 signed by 0x16fd) as verify_2d_operation's radius adjustment, cell 0.045 mm / 0.00177 in, map y band +- (tol + p7) grown by 4/7, the trace with D/2, the loops read back as lines and sorted with 0.025 / 0.001. FUN_1802b77a0 level logic: the deepest level, then level 1; same loops (FUN_1801b1440, tolerance 0.0025 mm / 0.0001 in) means one S2 and the depth-cut copies; different means an S2 per level and an engine run per level (PackageChains l.5314-5316). FUN_1802a70d0 added to the wall offset when from outside. The N9-18 warning is gone. Refused: source 2 (FUN_1802cbdd0), source 1 and the group filter (no stored op uses them). Area Mill with 0x17b8 is refused with its own message (mode 4, plan 0x1416: not the Core Roughing message). Ours: the region is the chain's polyline; +0x49 (air chains only) unused.

**Why the 4 stored ops show no S2 effect.** Rest = region minus what earlier ops swept at that level. The list is Operations Manager order (op record +0xe6): BRACKET4 and bracket4-dashed run 14, 2, 16, 17, ... So op 14 has no earlier op at all (rest = whole region, 268.10 mm2). Op 16 has ops 14 and 2 before it, but the region of op 16 is x 32.66..55 and those ops cut only inside x 0..22 (the region of op 14; cut area 255 mm2 elsewhere): rest = the whole region again. Both S2 loops equal the region, so the engine's result is the same as with no S2.

Check on the 4 ops: DLL with the ported S2 against the NCI, op 14 / 16 p95 0.038 / 0.046; DLL with no S2: the same, records byte-identical. A copy of BRACKET4 with ops 2 and 14 swapped in the order: S2 = 7 loops, 74.9 mm2 (268.1 less the 193 cut by op 2), the DLL output changes (S2 versus no S2 differs). The feature is exercised; no stored op confirms it (the NCI cannot show an S2 equal to the region).

Checks after G (toolpaths2 merged, no comparison switch any more): Dynamic oracle 37 ops median 0.111 worst 0.643, unchanged per op and every one of the 41 op outputs byte-identical to the round before; Area 3 ops 0.914 / 0.928; DLL oracle: port 0.111 / 0.643, DLL 0.062 / 0.435, no DLL failure; units_test ALL PASS; op scenes 2 / 49 differ (the existing header lines), no references blessed.

## H. Review of the rest material port (N9-18)

Each confirmed in the decompile:

1. Empty rest: `FUN_1802cb260` sets the "no bounds" flag for an empty trace, `FUN_1802b77a0` (hst2d l.15131) sets its `param_7` when the deepest level, level 1 or a bisected interior level has no rest, and `PackageChains` (0x1802b0882-0x1802b088b, 0x1802b0adc-0x1802b0adf) skips the region. At the end `PackageChains` reports error 10 (mcmillres 0x187 "All machining regions have no remaining material.") or warning 0xb (0x17f "At least one machining region has no remaining material."). Ported; an empty S2 no longer means "cut the whole region".
2. The stop for a previous Pocket is opcode 3 and op +0x1340 = 1 with no finish test (mm l.487203, 0x1802cb757-0x1802cb765). `CamPrevOp.pocket_1340` carries it for stored and session ops; the test no longer depends on `opcode` being a Mastercam opcode.
3. The outside band: `D / 2 + p7` for both the offset and the sweep (0x1802cb82a-0x1802cb88a).
4. `rm_tol`, `rm_nc_tol`, `rm_chain_tol` set by `hst_rest_level`, as cam2d_pock.c.
5. Inch cell 0.0017716535433070866 = 0x3F5D06DA81D06DA8.
6. Settled. Interior levels: `FUN_1802a82c0` picks the middle of the first run of unknown levels; a level whose rest equals the level below or above within 0.0025 mm / 0.0001 in (`FUN_1801b1440`) shares that level's data through `FUN_1802ae390` over the whole interval. Ported as that bisection. The `dia / 2 + p8` move radius uses the guard of the 0x3f5 record (`0 <= radius + adj`). `P.top` per level is the previous level (PackageSomething l.5611 / l.6073), the first level's the op's top.

The op dialog has no control for 0x17b8, so the cleared-pocket case cannot be an op_scenes.txt scene: `tools/tests/hst_rest_test.sh` covers it through `cam_cli --rec 17b8=b:1`: on NAKTO-BRAKE-BRACKET a new Dynamic op whose earlier stored ops clear chain 1 gives the error text, and chains 1 + 5 the warning text.

Checks after H: Dynamic oracle 37 ops 0.111 / 0.643 and every one of the 41 op outputs byte-identical to the round before (no stored op has an empty or a partial rest); Area 3 ops 0.914 / 0.928; units_test ALL PASS; `hst_rest_test.sh` ALL PASS; op scenes 2 / 49 differ (the existing header lines), no references blessed; DLL oracle port 0.111 / 0.643, DLL 0.062 / 0.435.
