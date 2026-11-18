# Dynamic (2D High Speed) toolpaths — Mastercam 2017

Opcode 134 "2D High Speed". Sub-style = first byte of the opcode union, **entity offset 0x1228** (MP param 12713):

| style | name (mcmillres string) | engine path |
|---|---|---|
| 0 | Dynamic Mill (2777) | mcce `MCCECalcDynPock` |
| 1 | Area Mill (2773) | standard pocket (`CHST2DStandard` / `MCCECalcStdPock`) |
| 2 | Dynamic Contour (2818) | mcce dynamic, wall/contour flag 0x200000 |
| 3 | Peel Mill (2703) | mcce dynamic with peel region (`CPeelMotMatParm`, `PeelAesthetics`) |
| 4 | Blend Mill (2713) | separate path in mcmill (`bnci_2d_hmm` case 4) |

Style -> name from the switch at mcmill 0x18026d2f0. Both 2D HST ops in the bundled `mill` template are **Area Mill**;
no bundled file contains a Dynamic Mill toolpath, so engine behaviour below is from code only.

## One engine for every "Dynamic" toolpath
`mcce::MCCECalcDynPock` (mcce.dll) computes the in-plane motion for all three Dynamic families:
| family | caller | how the region is built |
|---|---|---|
| 2D Dynamic Mill / Dynamic Contour / Peel (opcode 134) | mcmill `CHST2DWorkItemDyn` -> `LoadDynPockParm` | chained 2D geometry, open/closed boundaries |
| 3D OptiRough | mckernel `OptRoughToolPath` -> `OptRoughDynPockExecutioner` -> `FUN_180108f1c` fills CDynPockParm | Z-level profile slices of the model (formats/hst3d.md) |
| Lathe Dynamic Rough | mclathe `CDynamicRoughTp::GenerateToolpath` -> mccore wrapper `MCCECalcDynPock(CDynPockParm const&, pock_bound* ...)` | XZ stock-minus-part turning profile (`CDynamicRoughGeometry::GetAllGeometry`, `FindRemainingStock`) |
All three convert their own settings to the same `CDynPockParm` block, with engagement = acos(1 - s/R).

## Pipeline
```
mcmill.dll  bnci_2d_hmm (opcode 134)
  CHST2DProcess: ImportChains -> PrepareChains -> ConvertChainsToPockBounds
     open/closed boundaries (CloseOpenMachPockBoundWithAir/Line/StockShape, Join*), ComputeOffsetsAtDepth,
     PackageChains / PackageMachiningRegion, SetupTpFiles (temp files tmppb, tmphdr, tmptpe, wrktpe, HstTpe, tmpce,
     tmprnd, dlctpe in GetWorkingDirectory)
  ExecuteChains: one CHST2DWorkItem{Dyn,Std,DCS} per region, run on the Windows thread pool
     (QueueUserWorkItem, srand(seed) per item), joined in CHST2DAccumulate::WaitForCompletion
  CHST2DWorkItemDyn -> LoadDynPockParm (op -> mcce::CDynPockParm) -> mcce::MCCECalcDynPock
  ProcessDynamicMillExt reads the engine output (mcce::MCCEReadDynPockHd + 0x88-byte entities tagged
     'L' line / 'A' arc / 'P' point) and converts to NCI -> OutputMotionToBnci
mcce.dll ("Mastercam Cutting Engine", CNC Software, MCCE.pdb)
```

## Operation -> engine parameters (`LoadDynPockParm`, mcmill 0x1801af380)
Labels come from the Dynamic Mill dialog (mcmillres dialog 3205, class `HSTDynPockParamsPage`): control -> `MCReal`
member (DoDataExchange 0x180165fa0) -> operation field (page methods, `page+0x1560` = op entity - 0x2088). Full join in
`formats/dynamic_ui_fields.tsv`. Offsets are **entity offsets**.

| CDynPockParm | operation field (entity offset, MP param) | UI label / meaning |
|---|---|---|
| +0x08 flags | style 0x1228 (12713), **0x1567 machining region: 1 stay inside -> 0x40**, 0x16bd plunge angle/pitch -> 0x2000, 0x16be center helix on point -> 0x100000, 0x16bc entry feeds/speeds -> 0x40000000; + option bytes 0x1564/0x1567/0x16b9/0x16bb/0x16bc/0x16bd/0x16be, metric, 0x1784 bit1 | mode bits: 0x12 or 0x42 base, 0x1, one-of 0x100..0x8000 (0x16bb), one-of 0x10000..0x80000 (0x16b9), 0x100000, 0x2000, 0x200000/0x600000 Dynamic Contour, 0x40000000 (0x16bc), 0x8 metric, 0x4 |
| +0x0c | 0x1790 (or 8 + region for Peel) | |
| +0x20 | 0x16bf (0/1/2) | 3-way option |
| +0x28, +0x30 | tool geometry (FUN_1801afa30) | tool radius R, corner radius r |
| **+0x38** | 0x1580 Stepover % (12971) [0x1730 alt] | **engagement angle = acos(1 - s/R)**, s = D x % / 100 |
| +0x40 | 0x1658 Min toolpath radius % (40006) [distance 0x1650 = 40005] | min toolpath radius = % x D (>= 5 % of R) |
| +0x48 | 0x16d0 Micro lift distance (40233) | |
| +0x50 | 0x1660 Approach distance (40007) | Dynamic styles only |
| +0x58 | 20 x chain tolerance | |
| +0x60 | 0x17c0 First pass offset (41143) | |
| +0x68 | 0x1678 **Plunge angle** (entry page) deg -> rad (default 1.5 deg) | helix/ramp plunge angle |
| +0x70 | 0x1680 **Entry pitch** (default 0.1 in / 2.54 mm) | helix pitch (alternative to plunge angle, radio 0x16bd) |
| +0x98 | 0x16c8 **Trochoidal loop radius** (default 0.1 in / 2.54 mm) | entry trochoid |
| +0x88 | 0x1688 **Z clearance** (entry) | |
| +0x90 | 0x16c0 **Helix radius** | entry helix radius |
| +0xa0 | 0x1690 **Additional slot width** | entry slot widening |
| +0xa8 | 0x17b0 First pass feed reduction % (41141) | clamped 0..99; styles 0/3 only |
| +0xb0 | 0x388 (tool retract rate) | |
| +0xb8 | 0x1248 Back feedrate (12718), else feed | |
| +0xc0 | 0x1668 **Ramp feed rate** when "Entry feeds / speeds" (0x16bc) is on, else feed | entry ramp feed |
| +0xc8 | 0x378 feed rate | base feed |
| +0xd0 | feed x (1 - first pass reduction %) | first-pass feed |
| +0xd8 | 0x17d0 Conventional feed rate (41259) when 0x1784 bit1, else feed | conventional-cut feed |
| +0xe0 | Gap size: 0x1600 distance (12994) or 0x1608 % of D (12995), selector 0x15cd | gap size (motion < gap -> micro lift, > gap -> retract) |
| +0xe8/+0xf0/+0xf8 | 0x1748/0x1750/0x1758 | min material thickness (>= 0.001) etc. |
| +0x100/+0x108 | 0x1780/0x1788 when 0x1241 set | |
| +0x110 | chain tolerance | |
Other Dynamic-page fields without an engine field yet: Stepover distance 0x1250 (12719).

Derived inside mcce (`FUN_180069560`): +0x1f8 = working stepover R(1 - cos theta) clamped between the stepovers for 10 deg
(+0x200) and 170 deg (+0x208) using R - r; +0x210 = stepover at 0.25 deg; +0x1b0 = flags & 1. `CCePock` keeps the block
at `this+8`.

Validated on the two Area Mill ops: D = 0.5, stepover 40 % -> engagement 78.46 deg (computed, Area Mill does not use it);
gap size stored as 0.5 (0x1600) / 100 % of D (0x1608) — consistent. Other entry fields seen in those ops (common to the 2D
HST family): **0x15d0 = helix radius** (param 12988; 0.225 = the helix radius measured in the NCI), 0x1620 = 3.0 (12998),
0x1628 = 50 (12999), 0x1638 = 50 (40002), 0x15b8 = 0.1 (12978), 0x15c0 = 0.05 (12979), 0x1610 = 0.05 (12996).
Measured Area Mill helix: 40 arcs of 117 deg at r 0.225, 0.0158 drop each -> 1.97 deg ramp (so 3.0 is not simply the ramp
angle for Area Mill's own entry code).

Parameter validation (`FUN_18006a3b0`, mcce): engagement angle clamped to [10 deg, 170 deg]; +0x30 <= 0.95 x +0x28;
min radius >= 0.05 x +0x28; tolerances >= 0.001 / 0.1 deg / 1e-4; +0xa8 in [0, 99] %; min material thickness 0.001.

## Engagement model (mcce exports)
- `CalcLnStepover(theta, R) = R (1 - cos theta)` — stepover of a straight cut with engagement angle theta.
- `CalcLnEngAng(R, s) = acos(1 - s / R)` clamped to [0, pi].
- `GetMinRadK(x) = 0.05 x`, `GetMinMatThick() = 0.001`.
- Tool-centre placement against an arc wall at the target engagement (`FUN_18001efb0`): uses `theta + pi/2`, asin/atan2 to
  position the next centre so the cut arc meets the wall at the engagement angle.

## Engine structure (mcce.dll, 4831 functions, full decompile in `decomp/mcce_all.c`)
Classes: `CCePock` (pocket driver), `CCetp` (toolpath writer, Cetp*.cpp), `CMatMod`/`CMmCut` (material model),
`CEnCut` (engagement-controlled cut), `CBands`, `CRail`, `CSeg`/`CSect`/`CSec`, `CTEnt`, `CFeedRateSect`, `CGbap`, `CHp`, `CBei`,
`CCutPassLen`, `CCeafc`; geometry kernel `CSh` (shape), `pock_bound`, `pock_chn`; `CRasDev`/`CPixVe` raster device
(present but not reachable from Dynamic Mill — material is tracked with vector shapes, not a pixel grid).
Source files: Ce/Cetp/CetpFile/CeTransIn/CeParm, MedAx*, MinQdMain, OffMain{Ctour,Encase,Pock}, Peel{MotMat,Aesthetics},
PockClosedOpen, Raster*, Sh* (area/dir, coincidence, self-intersection, nesting, point-in, smoothing), So* (union, intersect, A-B).

`MCCECalcDynPock` (0x180006d61): pock bounds -> `CSh` arrays (machining region, second set, third set), preprocessing
(`FUN_180090290`), then `CCePock` run (`FUN_180011710`):
1. prepare (`FUN_1800124d0`: offsets via `MCCECalcOffset`, nesting, unions)
2. open toolpath output (`CCetp`, `FUN_180085ed0`; temp file `~CE` when no path given)
3. `while (next region)` (`FUN_18002a2a0`) -> region machining (`FUN_18006e620`):
   - entry (`FUN_180070130`; reaches the medial-axis code — helix/entry placement)
   - loop while remaining-material shapes exist (list at `this+0x420`), state machine:
     state 0/1 -> `FUN_18006d170`, state 2 -> `FUN_18006d330`, state >= 3 -> `FUN_180067ce0`
     (with a runaway guard of 32 iterations without progress)
   - finishing / clean-up (`FUN_1800688e0`)
4. close output, progress 100 %.

## Algorithm summary (from code; matches the classic "adaptive clearing" scheme, done with exact vector geometry)
1. Uncut material is a set of vector shapes (`CSh`, list at `CCePock+0x410`; `CMatMod`/`CMmCut` material model).
2. The feasible tool-centre boundary is `MCCECalcOffset(1, R - r, material)` merged into `CCePock+0x340`
   (`FUN_180062a70`).
3. Each sweep's candidate path is a **section of that boundary** between two parametric positions
   (`FUN_18006caa0` -> `FUN_180146e20`, entity index + t in [0,1]).
4. The sweep is marched in steps (`FUN_18006f1c0`). The pocket object holds the CDynPockParm at **+8**, so pocket offset
   X = Parm offset X−8. Each step is measured twice by `FUN_180030de0`: with circle R − r (step record +8, result at
   +0x128) and with R (+0x140, result at +0x260). The measure is **swept-away material area / step length**, the mean
   radial width of cut. The step is committed to the material (`FUN_180035990`, flag 0x80) only if both widths are
   below the limits from `FUN_18006cf10` (otherwise it is corrected by `FUN_18006dfa0`). The limits:
   - Default: the working stepover s_work (Parm+0x1f8).
   - **Rate-of-engagement ramp**: active while the ramp flag is set and Parm+0x23b (`bUseRateOfEng`) is on. The flag
     (low byte of `local_3e8` in the region loop) is **set after every linking move** (state ≥3). States 0/1
     (`FUN_18006d170` → `FUN_18006db90`, one step per call) consume it, and state 2 (`FUN_18006d330`) clears it on entry.
     So only the **re-entry into material after a link** is ramped; constant-engagement marching is not.
     `limit = width of previous step + (s_work − s_10) · step / (10.666·R)`, where step is the march step Parm+0x1e0
     and the previous width comes from the last of a 16-entry ring of step records (0xc0 bytes, +0xa0 / +0xa8 for the
     two circles). The limit is clamped to [s_10, s_170] and to ≤ s_work. The width may therefore grow from s_10 to
     s_work no faster than over **10.666·R of travel** (`Parm+0x1b8`). The ramp flag clears once the limit is within
     Parm+0x210 (s_0.25) of s_work, unless byte +0x21f (bullnose) is set.
   Derived values (`FUN_180069560`; Parm offsets; R = +0x28, r = +0x30; `s_θ = CalcLnStepover(θ, d) = d(1 − cos θ)`):
   - +0x1f8 = s_work = R(1 − cos engagement), clamped to [s_10, s_170]. s_10, s_170 and s_0.25 use d = R − r
     (+0x200, +0x208, +0x210).
   - +0x1b8 = 10.666·R (ramp length). +0x1c0 = 0.01 inch-equivalent (0.01, or 0.254 if metric flag 0x8), but at most R/8.
   - +0x1d8 = max(0.45·R, min path radius) when variable radius (`bIsVarRadAvail`, +0x21e) is on, else the min path radius.
   - +0x1c8 = +0x1c0 + 0.25·(that radius). +0x1d0 = 2.5·(+0x1c0 + 0.25·min radius).
   - +0x1e0 = march step from `FUN_180069350`: a chord of the trochoid-loop radius (+0x228 `dMinCutRad`; default 70 %
     of R − r, clamped to 1–90 %) over `dRendAngDeg` (22.5 deg), capped by a function of (R − r, engagement).
   - +0x1e8 = 1.5·+0x1e0. +0x1f0 = π·min path radius (the sweep ends after 1.5×/3× this much non-cutting travel).
5. Accepted steps are emitted as cutting moves and subtracted from the material; the sweep ends after too much
   non-cutting travel (1.5x / 3x `CCePock+0x1f0`), then linking (micro lift if shorter than gap size, else retract)
   moves to the next sweep. Repeat until no material remains in the region.
There is no pixel/raster model in this path (CRasDev is unreachable from MCCECalcDynPock).

## Region loop states (inferred from which parameters each touches + code reading; not validated against output)
| state | function | evidence | likely role |
|---|---|---|---|
| entry | `FUN_180070130` | reaches medial-axis code; uses R, r, micro lift | entry move / helix placement in the largest open area |
| 0/1 | `FUN_18006d170` | first-pass offset + first-pass feed, approach distance, conventional feed, back feed | first cut along the material / open edge |
| 2 | `FUN_18006d330` | engagement angle (15 refs) and working stepover (9) | **constant-engagement marching**: steps along a candidate path (step length `this+0x1e8`, `FUN_18006f1c0`), measures engagement of each step against the material model (`CEnCut`, measured values at +0x128/+0x260 vs limits from `FUN_18006cf10`), emits accepted steps as cutting segments (flag 0x10, `FUN_1800870f0`), removes them from the material (`FUN_180035990`), and ends the sweep after more than 1.5x / 3x `this+0x1f0` of non-cutting travel |
| >=3 | `FUN_180067ce0` -> `FUN_180067d80` | approach distance, gap size, micro lift | linking / repositioning between sweeps (micro lift when the move is shorter than the gap size, retract otherwise) |
| finish | `FUN_1800688e0` | min radius, R, r | final clean-up pass |
A guard ends the loop after 32 iterations without progress.

## Data structures (field names from mcce's own text dumpers)
`pock_bound`: alive, sel, isl, ccw, merged, machined, pock_n, n_ents, z, min_pt, max_pt, i_min_y, i_max_y, bottom_cut, top_cut,
perimeter, parent, prev, next.
`pock_chn` entity (0x88 bytes, also the engine output record): e.id ('L' line / 'A' arc / 'P' point), color, level, lstyle,
width, CeFlags[3], union {pt | li.e1,e2 | ar.c, r, sa, sw}, bbox[2], e_ptr, t, tp_ptr, depth, bp_tag.
Dynamic-mill input dump file (`MCCEWriteDynMillInParm` / DiagDynIn.cpp): magic 0xb4b8, version 0xc, parameters, bounds, entities.

## Region semantics, measured against Mastercam's NCI (2026-10-14, `tools/oracle.py`) — supersedes the notes below
- **Op +0x1567 is "from outside" when 1** (the earlier "1 = stay inside" label was inverted). Minimum distance from
  Mastercam's tool centre to the chains over all 37 2D HSM ops: +0x1567 = 0 → R + wall stock *inside* (BRACKET4 1.5 =
  R + 0, NAKTO 4.0 = R + 1, fight-stick 3.5 = 2.5 + 1); +0x1567 = 1 → the centre runs up to **R + wall stock outside**
  the chains at cutting feed (3.99 for R 3 / stock 1, 2.49 for R 1.5) and touches the chains. Material is the chain
  interior in both cases.
- **Chain role = TP_CHN header byte +0xe0**: 0 machining region, 4 avoidance region (not cut; kept like an island).
  buckle-v10 op 3 (second region avoided) and Area op 2 (five circles + a 443 mm² region avoided) only match with it.
- **Solid chains** (TP_BND entity class 0x10000, header text "Solid Chain N", buckle-v6/7/8 op 31) carry no edge
  geometry; the region is the outer loop of the solid's top face (see mcam.md "Solid chains"): missed 0.0 %, extra 0.0 %.
- Result with these rules (`oracle.py --type dynamic|area`, material inside the machining regions, rasterised at R/10):
  33 Dynamic ops: material Mastercam cuts and we leave ≤ 0.1 % on every op, extra ≤ 6.1 % (median 0); 4 Area ops:
  missed ≤ 0.5 %, extra 0. Paths differ (our engine is not Mastercam's), so path-to-path deviation is not a measure here.

## Machining regions (decoded 2026-11-07; decomp/mcmill_hst2d.c = mcmill_hst2d.c, dyn = mcmill_hst2d_dyn.c)
How mcmill turns an op's chains into MCCECalcDynPock calls (Dynamic Mill, style 0; ported for from-outside ops in
viewer/cam2d_dyn.c `facing_regions` / `gen_dynamic_mcce`):
- **Process fields** (`SetOperationID` -> `SetParams`, mcmill_hst2d.c l.9211-9351, record = 0x2958 - local): +1 = 0x1567 (from
  outside), +0x54 style, +0x55 = 0x1567 (style < 2), +0x58 = style < 2 && 0x17b8, +0x16a = 0x17b8, +0x150 = wall stock 0x2e0,
  +0x60 = tool diameter 0x350, +0x48 = tolerance 0x1288, +0x16b = 1 for style 0, +0x380 = ±0x16f0 (0x16fd 1 / 2).
- **Chain lists** (`PrepareChains` l.6120-6205, `chains_from_db_op` by TP_CHN role): role 0 -> this+0x10 -> pock bounds this+0x328
  (machining regions); 4 -> +0x330 (avoidance); 5 -> +0x338 (entry chains); 6 -> +0x350 (air); 7 -> +0x358 (containment).
  No role-0 chain: FUN_1802c4530 builds +0x328 from the op (not read). This+0x340 / +0x348 are set by PreparePockBoundsPeel only.
- **`ConvertChainsToPockBounds`** (l.2276-2470): one `chain_to_pock_bound` per chain; each chain's bounds go in FRONT of the list
  built so far (LAB_1802a0417), so the list is in reverse chain order. (Full-circle single-arc chains are split in two halves.)
- **`PackageChains`** (l.4631-5398), no containment (+0x358) / air (+0x350) chains: every bound of +0x328 is flagged, then until
  none is left `FUN_1802a7cf0` (l.15138) takes the FIRST flagged bound with the HIGHEST z (+0x18; a later one must be higher by
  1e-12) and runs it alone through `PackageMachiningRegion` (l.5400) with the whole avoidance list +0x330 (l.5040-5047, restored
  after each region l.5280-5292). No nesting test between role-0 bounds on this path: a chain inside another is a region of its
  own, not an island. (With containment chains the bounds are intersected with them first, l.4740-4835 - not ported.)
- **`PackageSomething`** (l.5480-6118), level loop (once without island depths / taper): copies the avoidance bounds (islands)
  and the region, then `FUN_1801ac7e0(&region, &islands, d, s, 1e-12)` (l.5640-5660; call 0x1802b2396: xmm2 = d, xmm3 = s) with
  d = ComputeOffsetsAtDepth (l.2242: wall stock, sign flipped by +0x16c; taper FUN_180050910) + CHST2DDynamic vtable +0x38
  `FUN_1802a70d0` (dyn l.483-500: 2·tol + 1.5·D (+ |+0x380| when negative) only when style 0 && 0x17b8 && 0x1567, else 0), and
  s = the island stock. Each piece of the result is one work item (vtable +0x18 islands, +0x10 region, +0x20/+0x28/+0x30 the
  +0x338 / +0x340 / +0x348 lists, +8 creates it: call 0x1802b262d, param_5 = 1, param_6 = top of stock, param_7 = level z).
- **`FUN_1801ac7e0`** (l.17164-17553): islands, s > 0: each `MCCECalcOffset(1, s, island, 0x101)`, all pieces kept (s < 0:
  AddDummyOuter `FUN_1801abf80` box, offset, largest piece dropped). Region, d > 0: **from outside** (cur_op +0x14ff = record
  0x1567) `MCCECalcOffset(1, d, region, 0x101)` and only the largest piece (pock_ccw area) kept, the others freed (l.17426-17470);
  **stay inside**: the region is flagged an island inside a dummy box (`FUN_1801abf80` "AddDummyOuter"), offset by d, and the
  largest piece (the box) dropped: the region inset by d (l.17471-17510). Dynamic Contour (record 0x1228 = 2): MCCECalcContourOffset.
- **Work item** (`FUN_18029cb90` dyn l.3-262 -> `FUN_1802b1430` dyn l.1909-2110, call 0x18029cf93): S1 = the islands (flag [2] = 1)
  followed by the region ([2] = 0); S2 = the rest-material bounds (process +0x58 only, `FUN_1802a94f0`), else the first of the
  +0x348 list; S3 = the entry chains (+0x338). The parameter block = LoadDynPockParm (FUN_1801af380) with the tool-manager corner
  radius, flag 0x4000000 for Peel, 0x8000 cleared without entry chains, +0x78 = top of stock, +0x80 = level z; it is written
  to a file (WriteCeParm) that the executor `FUN_1802a5760` reads back (ReadCeParm) before MCCECalcDynPock(parm, S1, S2, S3).
- **Solid chains** (TP_BND class 0x10000): what Mastercam stores is NOT DECODED (formats/mcam.md). buckle-v6/7/8 op 31 (2 solid
  chains, from outside): the NCI has 6 regions - per solid the outer loop and the two holes of the solid's top face, in the
  order outer A, hole A1, hole A2, hole B2, outer B, hole B1 = the face's XT loop order per chain, chains [B, A], reversed as
  above. Reproduced exactly (the DLL oracle: 6 calls, record counts 622 / 109 / 148 / 148 / 622 / 109; p95 vs the NCI 1.473
  -> 0.06 mm) by taking every loop of the top face as a chain (CORRELATED). The hole regions' feed-in rapid goes down to
  z -0.5 where the outer regions' goes to 3.0: decoded 2026-11-13, "Nested regions" below (a region inside another region
  gets that region's z as its top of stock).
- **Nested regions: top of stock** (decoded 2026-11-13, M3; mcmill.dll disassembly, the decompile drops the arguments). In
  PackageChains, before each region's depth list (call 0x1802b0701 to the CHST2DDepthCuts setter `FUN_1802ca970`, l.5066-5077):
  `CHST2DProcess::HasOuterPocket(region, list)` (0x1802a9af0, l.3124-3146) walks the bound list +0x328 - the bounds not yet run
  plus every bound already run, each put back at the front after its run (l.5005-5020 unlink, l.5348-5355 relink) - and
  returns the innermost bound b with mccore `IsContourNested(region, b, tol +0x48 = record 0x1288)` (b replaces the best when
  `IsContourNested(b, best)`). Found: the setter's top-incremental byte (param_14 -> dc +0x80) is 0 (`xor dil,dil`
  0x1802b05d1) and its top (param_15 -> dc +0x88) = b +0x18 (b's z) minus the view z of the plane origin this+0xd0
  (0x1802b05d4-0x1802b0605); not found: this+0x110 / +0x118 (the op's top flag / value). `FUN_1802ca790` (mcmill_all.c
  l.486548) then makes top_of_stock = b's z, and from it: feed plane / retract / clearance when incremental (l.486553-486573),
  the depth list (`FUN_1802ca490` from `FUN_1802ca5f0`, l.15556 / 15565), the engine's P+0x78 (PackageSomething reads
  top_of_stock at 0x1802b201e into xmm11 = the work item's param_6, call 0x1802b262d) and the group header's top
  (`FUN_18029f830` -> `FUN_1802bd100` param_7 = *top_of_stock, 0x18029ffa4). Ported: viewer/cam2d_dyn.c `outer_pockets`
  (mccore IsContourNested = cam2d_remach.c `rm_contour_nested`, the bound polylines as line entities).
- **Region-to-region link** (decoded 2026-11-13). OutputMotionToBnci (l.4103-4630) writes each work item's records as one group
  behind a contour marker: `bnci_wr_ctour_marker(group counter, 1, header +0x20, header depth, header top, ..)` (l.4497; the
  header from `FUN_1802bd100`: depth = the group's lowest z (xmm7), top = top_of_stock, 0x18029ff9e-0x18029ffb5), and the depth
  writer `FUN_180045770` (l.13699-14820) links the groups: the counter changes at every group, so at a group's first move,
  tool down and farther than nc_tol (l.14450-14466): set_heights(previous depth, previous depth), clear_move when record
  0x179 (clearance) or 0x319 (only at start and end) is off, else retract_move; set_heights(depth, top); plunge_move(start,
  z, param_5 = 0x179 off or 0x319 off) (l.14533-14562). The mccore moves (decomp/mccore_moves.c) and set_heights 0x18089fa80
  as cam2d_ops.c mw_clear / mw_retract / mw_plunge. Ported for the transition from one region to the next (`hst_group_link`,
  record 0x1a9 rapid retract, 0x388 retract rate). NOT checked / not changed: the links between the depth copies of one
  region (CHST2DWorkItemDCS `FUN_1802b11f0`: whether each copy is its own group was not read) and the engine's own rapid
  records inside a region (the SAFE rule, M1); the 3D-contour branch of l.14469-14476 (ctour_prm +0xe0 = 1 / 3) is not taken
  by a 2D HST op [not read where ctour_prm is set for op 134].
  buckle-v6 / v7 / v8 op 31: ours->ref p95 0.111 / 0.111 / 0.069 -> 0.060 / 0.060 / 0.059, max 3.501 / 3.501 / 2.501 -> 0.226;
  the NCI's link moves (G0 to 10, across at 10, G0 down to 3.0 for the two outer regions and -0.5 for the four holes) are
  reproduced move for move (mcce.dll on our input, tools/tests/mcce_dll_oracle.sh -n: max 3.501 -> 0.102). The other Dynamic ops
  and the 3 Area ops score unchanged; fileop over all 205 stored ops of the user parts vs the base commit: only op 31 (x3) and
  buckle-v9 op 45 differ - op 45's two (not nested) regions are now linked across at the retract height 10, as its NCI
  (moves 622-624), where the SAFE rule crossed at the feed plane 3.0.

## Validation against real Dynamic Mill toolpaths (user files, `tools/dyn_validate.py`, `tools/dyn_batch.py`)
Method: rasterise the machining region (chains split at TP_CHN headers, even-odd), walk the stored NCI cutting moves at
the cut depth, measure engagement as the contact arc on the front half of the tool (calibrated: exactly 60.0 deg on a
synthetic straight cut with s = R/2), remove the tool disk, repeat.

Results over 35 Dynamic Mill ops in 13 X9 part files:
- **The engagement ceiling is acos(1 - s/R).** "Stay inside" ops (30): p99 engagement - prediction = -6.5..+5 deg
  for 29 of them (median -0.5 deg); e.g. tramming-rod op 1 (D 6, 25 %): predicted 60.0, p99 59.5, max 60.5.
  fight-stick op 1 (D 5, **80 %**): predicted 126.87, p95 124.0, p99 126.0, max 128.0 — the cap tracks the parameter.
  Median engagement sits below the cap (curved sweeps, entries).
- **Ramp-up after entering material** (`dyn_validate.py --samples`; a sweep starts each time engagement rises from 0 above 1 deg):
  - fight-stick op 1 has 75 entries. Median stepover vs distance since entry (mm → stepover in mm):
    0.25 → 0.12, 0.75 → 0.29, 1.25 → 0.89, 1.75 → 1.42, 2.25 → 1.96, 2.75 → 2.48, 3.5 → 3.10, 4.5 → 3.54, ≥5 → 3.6.
    That is about 1 mm of stepover per mm of travel. The p90 already reaches 3.3 at 1–1.5 mm, so many entries hit the
    cap almost immediately: the median "ramp" is mostly entry geometry (arcing into the stock), not a fixed rate limit.
  - tramming-rod op 1 leaves the material only 4 times in 7,665 moves (43k samples), so the cutter stays in contact
    almost continuously. The whole level is one unbroken path at the cut feed: no lifts and no back-feed moves at Z.
  - buckle-v12 op 23 has 14 entries. Its median plateaus at ~0.48 mm (33 deg) between 3 and 12 mm, which is
    curvature-limited (small pockets), with a p90 of 1.1–1.3 mm against a 1.5 mm cap.
- **Op entity 0x1567 = machining region strategy** (1 = stay inside, 0 = from outside; engine flag 0x40: 0x42 vs
  0x12). **Correction (2026-10-14):** the earlier "from outside = material outside the chain" fit was an artefact of
  the `chain_polyline` direction bug. With correct chains, both strategies use **material = chain interior**:
  - BRACKET4 op 14: median 57.5°, p90 65.0° vs predicted 66.4°;
  - fight-stick op 1: median 117°, p95 124° vs predicted 126.9°;
  - NAKTO op 2: p90 59.5° vs predicted 60°.

  Only the from-outside ops have a high-engagement tail (5–8 % of samples at 110–180°). It is **not** a heavier first
  pass: on BRACKET4 op 14 the first pass offset and the feed reduction are both 0, and every sample runs at the same
  feed (300). All 524 high samples fall in the first 5.6 % of the level's cutting sequence (65 short runs), so this is
  an **entry artefact**. The validator walks only moves at the final Z and ignores the material the entry ramp/helix
  removes just above it, so the first passes look like slotting. The strategy changes how the tool enters, not what
  is cut.
- Revalidated after the chain fix (all 36 ops): stay-inside ops (29 with chains) have p99 − predicted median −0.5°,
  and 25/29 are within [−6.5, +5]. Outliers with max 77–96° are buckle-v10 op 1 and the op-10/36/42/45 family (p99 OK,
  max ~82°). buckle-v6/7/8 op 31 chain solid edges (entity class 0x10000): see "Solid chains" above.
- **Plunge angle (0x1678 -> +0x68) confirmed**: BRACKET4 ops 14/16 ramp down from Z 3.0 to −4.5 in ~7,900 descending
  line moves with a median ramp angle of exactly 2.000 deg. These two ops have entry method 0x16bb = 3 = **Profile**.
  The descent follows the offset pocket shape (radius about the centroid 0.78–13.1 mm in a 14 × 18 mm region, no
  arcs), not a helix. The helix radius setting (10 mm) does not apply to them. (An earlier note here read the descent
  as a shrunken helix; that was wrong.) All other ops use 0x16bb = 0 (Helix only) or enter from open edges.
- **Feeds / micro lift confirmed**: Dynamic ops emit only the base feed (0x378) and the back feedrate (0x1248, 2500 in
  these files) for horizontal motion. Back-feed moves run at **cut Z + micro lift distance** (0x16d0 = 0.25: 1284 of 1318
  back-feed moves on buckle-v11 op 10, 50 of 73 on BRACKET4 op 14, and +0.25 above each depth level on fight-stick).
- Output is mostly line segments (arcs approximated). Across all 35 ops the p90 segment length is 5-5.8 % of D
  (0.15 mm for D 3, 0.30-0.35 mm for D 6) while medians range 1-5.5 %: segments are capped near 5 % of D (march step)
  and shorter where arcs are linearised. Min curvature 1.76 mm >= min toolpath radius 0.6 mm (10 % of D).

- **Re-entry descent (measured 2026-10-15, all stored Dynamic ops)**: after a link at the micro lift, the tool ramps down
  the micro lift (0.25) over a constant length = pi x min toolpath radius (mcce +0x1f0): 1.885 mm for D 6 and 0.942 mm
  for D 3 at 10 % (angles 7.56 / 14.86 deg in every op), along an arc (centre-path fit radius 1.6-3.3 mm; shape not
  decoded). Some descents of length 1.35 mm (r ~2.5, ~10.5 deg) also occur. The single Helix-only first entry in the
  data (NAKTO op 2, D 6, helix radius setting 10) ramps at exactly the plunge angle 2.00 deg on a centre-path radius of
  13.08 mm (one sample: not enough to tell how the 10 mm setting maps to it). Area Mill helices: 2.3-2.6 deg.
  (Was used by the old generator's dyn_link ramp length; that generator is removed since 2026-10-16. The mcce port makes
  its own descents, mcce_port_plan.md.)

## Not yet done
- Exact geometry of each state's moves (how the candidate path for a sweep is generated; how micro-lift height is applied).
- Still unnamed, with known sources:
  - +0x0c (byte) ← entity 0x1790 (MP 40307); fixed at 8 for Peel, which also copies 0x1548..0x1554 into +0x10..+0x1c.
  - +0x20 ← 0x16bf (MP 40230; 3-way; 1 in all user ops).
  - +0xe8/+0xf0/+0xf8 ← 0x1748/0x1750/0x1758 (MP 40253–40255; three doubles, probably an XYZ point; all 0 in user ops).
  - +0x100 (int) / +0x108 ← 0x1780/0x1788 (MP 40260/40261), only when byte 0x1241 (MP 12717) is set.
  - Flag 0x4 ← bit 1 of 0x1784.
  - Confirmed: +0x60 ← 0x17c0 first pass offset; +0xa8 ← 0x17b0 (styles 0 and 3 only, else 0).
- Observed vs decoded ramp: the decoded per-step increment (Δs·step0/(10.666R) ≈ 0.15 mm per mm of step0 for fight-stick) is slower than the measured median rise after material entry (~1 mm/mm). This would match if the state-0/1 steps (`FUN_180016c00`) are ~1/7 of step0 long; that is unverified.

## Hidden registry configuration (`FUN_180069840`, read when Parm+0x21c is set, which is the default)
Key: `<hive>\Software\CNC Software, Inc.\Mastercam 2017\Configurations\Variables\Toolpaths\Dynamic Mill`
(root from mccore.dll `get_REG_KEY()`, loaded dynamically by `FUN_1800691c0`; hive not verified). None of these are exposed in the UI.

| value | type | default | Parm field | role |
|---|---|---|---|---|
| bIsAssertOn | int | 0 | +0x21d | debug asserts |
| bIsVarRadAvail | int | 1 | +0x21e | variable min path radius (≥ 0.45 R) |
| bIsBullnoseAvailA | int | 1 | +0x21f | bullnose (corner radius) aware engagement; also caps the march step |
| bWriteMatMod | int | 0 | +0x220 | dump material model |
| dMinCutRad | double | −1 (auto) | +0x228 | trochoid loop radius used for the march step |
| dRendAngDeg | double | 22.5 | +0x230 | angular step for the march-step chord |
| bIsBandsAvail | int | 1 | +0x238 | |
| bIsCutOrdLenBas | int | 0 | +0x239 | cut order by length |
| bIsAirCutV3 | int | 0 | +0x23a | air-cut handling v3 |
| **bUseRateOfEng** | int | 1 | +0x23b | **rate-of-engagement ramp** (above) |
| bOverrideMinPathRad / dMinPathRadPctToolDia | int / double | 0 / 2.5 | +0x40 | override min path radius = pct·D/100 |
| bTurnOnMoatFirst | int | 0 | flag 0x10000000 | cut a moat (outline) first |
| dStkPlaceErr | double | −1 | | stock placement tolerance |
| dAirDwellPos | double | 0.01 | +0x240 | |
| iCallNumToMill | int | −1 | +0x248 | debug: stop after N calls |
| iNumRedPass | int | 1 | +0x24c | reduced passes |
| dDisTransOvr / dDisTransTot | double | 2·R / 4·R | +0x250 | transition distances |
| bUseOldGslCutOrd | int | 0 | +0x260 | old cut ordering |
| bUseCtourGuide | int | 0 | +0x261 | contour guide |
| bIsPeelAestheticsAvail | int | 1 | | Peel Mill (`FUN_1800df360`) |
| bUseOldBuildSoFx (Util) | int | 0 | | `FUN_180162a00` |

## Gap-size retract option (entity 0x16b9; UI combo 3202 "Motion > Gap size, retract")
`LoadDynPockParm` maps the byte to flag bits; `FUN_180064180` (link stage) decides whether a link retracts.
`len` is the link length, `cross` is how far the straight link crosses a boundary (chains, part, stock, or uncut
material), and gap = Parm+0xe0 (pocket+0xe8).

| 0x16b9 | flag | help-file option | retract when |
|---|---|---|---|
| 0 | — | Never | never (stay down / micro lift at back feed) |
| 1 | 0x10000 | Avoiding a boundary | cross > 1e-5 |
| 2 | 0x40000 | Avoiding a boundary OR exceeding a distance | cross > 1e-5 or len > gap |
| 3 | 0x80000 | Avoiding a boundary AND exceeding a distance | cross > 1e-5 and len > gap |
| 4 | 0x20000 | Exceeding a distance | len > gap |

The labels come from help `tpfields/Fields_dynamic_mill/Retract_dynamic_mill.htm`; the combo items are filled at runtime.
The value-to-label order follows the engine logic; the combo's item order is unverified. All 40 Dynamic ops in the
user's files have 0x16b9 = 0 (Never) and gap = 100 % of D. This matches the measured links: moves longer than the gap
stay down at back feed.

## Entry method (entity 0x16bb; Entry Motion page 3219)
| 0x16bb | flag | help-file option |
|---|---|---|
| 0 | 0x100 | Helix only (helix centred on the deepest point, unless "Center helix on point" 0x16be → 0x100000) |
| 1 | 0x200 | Helix followed by full medial burial |
| 2 | 0x400 | Helix followed by trochoidal medial |
| 3 | 0x800 | Profile: ramp following the offset pocket shape (**confirmed** on BRACKET4 ops 14/16) |
| 4 | 0x1000 | Medial: ramp along the medial axis |
| 5 | 0x8000 | Custom, use entry chain |
The order follows the help text (`Entry_method.htm`); only value 3 is confirmed against output. The entry state in the
engine (`FUN_180070130`) reaches the medial-axis code, which is used by options 1, 2 and 4.

## Lathe Dynamic Rough → CDynPockParm (`CDynamicRoughTp::InitDynPockParameters`, mclathe 0x18027acb0)
`GenerateToolpath` reads the op entity with `TpPartOpList::DatabaseRetrieve`. As in mill, `operation` = entity + 0x68.
The toolpath-specific block `prm_ldynamic` (0x248 bytes) starts at **entity 0x1228**, the same union that holds the
mill style byte.

| CDynPockParm | source (entity offset) | meaning |
|---|---|---|
| +0x08 flags | 0x2142 always (+0x40 stay inside, 0x100 helix-only entry, 0x2000 plunge, 0x2); \|4 if 0x122a; \|1 by caller flag; \|8 metric | |
| +0x20 | 1 | 3-way option fixed to 1 |
| +0x28 | op+0x4c0 (entity 0x528) | tool radius |
| +0x38 | `CalcLnEngAng(R, stepover)`, stepover = double at **0x1230** (distance, not %) | engagement = acos(1 − s/R), clamped |
| +0x40 | 0x1240 | min toolpath radius |
| +0x50, +0x70 | max(|(0x1270, 0x1278)|, |(0x1310, 0x1318)|) | approach distance = entry pitch = longer of two XZ vectors (probably lead-in/out) |
| +0x110 / +0x58 | 0.0001 in / 0.0025 mm; ×20 | chaining tolerance |
| +0xe0 | 10.0 (fixed) | gap size |
| +0x48 | 99.99 (fixed) | micro lift |
`InitVariables`: the short at 0x1228 is the cut direction (0 → 0, 1 → −π, 2 → −π/2, 3 → +π/2). The byte at
0x13c8 enables the stock-shape options, with a double at 0x1450.
Not validated: there are no lathe Dynamic Rough samples.

## Reference model vs Mastercam output (`tools/dyn_ref.py`, `tools/dyn_svg.py`)
`dyn_ref.py` is a numpy-only raster re-implementation of the *constraint* the engine enforces:
- material = region; feasible centres = region eroded by R;
- march step 5 % of D; heading = the highest front-half engagement ≤ θ within the min-radius turn limit, falling back
  to a sharp turn;
- a sweep ends after 1.5π·min radius of non-removing travel;
- straight links to the nearest point where an entry heading with 1° < e ≤ θ exists;
- it stops after 50 unproductive sweeps.
It does **not** reproduce the engine's sweep construction (sections of the offset boundary of remaining material).

**Reference numbers before 2026-10-14 01:00 are invalid.** The reference cut a disk of R + 0.75·res, which hid about
45° of contact arc next to the side normal, so it really ran at ~80° while reporting 60°. Fixed; see the rerun below.

**Loop spacing (strong confirmation).** On tramming-rod op 1, Mastercam's nested loops cross the slot midline every
**1.50 mm** (6.39, 7.89, 9.39, …, plus one 3.5 mm gap where the spiral changes), exactly s = D × 25 %. So on
straight walls the programmed stepover is the exact pass-to-pass distance, and engagement = acos(1 − s/R).

Rerun after the fix (tramming-rod op 1, overhang R):

| | reference | Mastercam |
|---|---|---|
| in-material path | 1,947 mm | 2,318 mm (1.19×) |
| links | 3 (121 mm) | 4 entries, 29 mm air at Z |
| median engagement | 59° | 55° |
| loop spacing at the midline | 1.70 mm (raster error ~2 cells) | 1.50 mm exact |

Findings:
- Both produce nested offset-like loops growing from the entry (`img/dyn_cmp_tramming_rod_op1.png`). This
  constant-engagement wall following reproduces Mastercam's topology on a slot, and its path is 19 % shorter
  mostly because its raster loops are 13 % wider.
- Mastercam starts its spiral at one end of the slot and grows it along the whole slot. The reference starts at the
  deepest point (centre) and splits into three loop systems, which costs it 3 links.
- **Stay-inside paths overhang the chain by about R.** On buckle-v12 ops 3/17/22/23 the tool centre goes 1.6–5 mm
  past the chain bounding box on every side, and tramming-rod shows the same outer loop. With "stay inside", the chain
  bounds the *material*, not the tool centre. The reference allows centres within R of the region (`--overhang`).
  (An earlier `mcam_ops.chain_polyline` bug that ignored entity direction is fixed.)
Next step for the reference: replace the greedy march with "offset remaining material by (R − s), follow the
contour", and compare loop spacing with Mastercam's.

## Linking moves (measured 2026-10-14 on fight-stick op 1, BRACKET4 op 14, buckle-v11 ops 3/10, tramming-rod op 1)
Moves at back feed (0x1248) grouped into links. The dominant pattern (132/141 fight-stick links, 18/30 BRACKET4,
25/68 buckle-v11 op 10) is symmetric:
1. **Exit arc.** The sweep ends on an arc of radius **0.45·R** (exactly: 1.125 for D 5, 0.675 for D 3, 1.35 for D 6;
   = Parm+0x1d8 = max(0.45R, min radius), `bIsVarRadAvail` on). It continues at cut feed (~80°), then on the same
   circle at back feed for another 11–31°.
2. **Straight sloped link** at back feed, rising from Z to **Z + micro lift** along its whole length. There is no
   separate lift, traverse and drop. Chord/length 0.97–1.0. On fight-stick these are ~116 mm long, far beyond the gap
   (5 mm), yet they stay down: the retract option is Never (0x16b9 = 0).
3. **Entry arc** at Z + micro lift, mirroring the exit: a short back-feed piece, then ~80° at cut feed, radius 0.45·R.
4. **Descent ramp** at cut feed back to Z: horizontal length **0.15·D** for the 0.25 drop (0.375 for D 5, 0.225 for
   D 3), then cutting continues.
Other patterns: a single straight back-feed line at Z ('L-'; 18 on buckle-v11 op 10, where the next start is close),
and line + arc. buckle-v11 op 3 also shows larger link arcs (3.59 / 6.59), probably fitted to the region.

## C port of the engine (2026-10-15)
`viewer/mcce_dyn.c` ports MCCECalcDynPock (formats/mcce_engines.md §0-§6, third-pass decode §6.10-§6.12); the plan,
the substitutions and every stubbed NOT DECODED piece are in `formats/mcce_port_plan.md`. It is the Dynamic Mill
generator (cam2d_dyn.c `gen_dynamic` -> `gen_dynamic_mcce`). Since 2026-10-16 the old invented generator is removed:
Area Clearance uses McHmm's pass rule (area_mill.md 11); Area Core Roughing is not generated (NOT DECODED,
improvisation_review.md item 7). Dynamic Contour (closed chains) and Peel Mill are generated since the HST engines pass
(2026-10-23, formats/opdlg_port_audit.md "HST engines (post-batch 9)"; no NCI exists for either). Oracle numbers before / after: mcce_port_plan.md §6.
Findings from the port that refine the notes above:
- Mastercam runs one engine call per machining region and depth (mcmill work items): separate chains are separate
  regions with their own entry; avoidance regions (role 4) are islands of the motion region (the tool runs round them
  at the micro lift), not air.
- The first approach of a from-outside op (0x1567 = 1 -> START_FACING) is chosen by the reference point of FUN_180076500
  (start selector op 0x1790, default 0 = the bottom-left of the region set) and the mode-2 cut-order filter; this
  reproduces Mastercam's first entry point to within 0.1 mm on 32 of the 35 stored Dynamic Mill ops.

Glue code (viewer/cam2d_dyn.c; mcce side in mcce_port_plan.md "Code map"):
- `gen_dynamic`: 2D High Speed dispatch. Dynamic Mill / Dynamic Contour / Peel Mill -> `gen_dynamic_mcce` (then
  `gen_dyncontour_mcce` / `gen_peel_mcce`, HST engines pass 2026-10-23); Area Mill modes that cam2d.c does not send to
  `gen_area_hmm` are refused with a "NOT DECODED here; not generated" error.
- `gen_peel_mcce`: CHST2DProcess::PreparePockBoundsPeel (Material / Motion loops round the side curves, extensions, start
  point op 0x14e0), mcce PeelAesthetics, MCCECalcDynPock(S1 = Motion, S2 = Material); formats/mcmill_2d_engines.md 7.
- `gen_dynamic_mcce`: McceParm from the stored record (`mcce_parm_from_record` = LoadDynPockParm), or for a new op from
  Mastercam's default op record (`cam_default_op_record`, Mill_MM / Mill_Inch.mcam-defaults) with the op's style,
  0x1567, tool diameter / corner radius patched in; then the dialog values (R, engagement acos(1 - s/R), min radius,
  micro lift, feeds, plunge angle, gap, top). Boundaries: region = even-odd union of the machining chains; stay inside:
  minus the avoidance regions, inset by the wall stock; from outside (flag 0x40): outers, islands and avoidance regions
  grown by the wall stock and passed as separate sets. One `mcce_calc_dyn_pock` per region (one outer boundary + its
  islands; from outside: + the islands whose points fall in its box; PackageMachiningRegion NOT DECODED) at the first
  level of `hst_depths`, then Z-shifted copies per level ("Depth cuts" below).
- `hst_record` (batch 9, 2026-10-23): the record LoadDynPockParm reads = the stored record (new op: the defaults op with style /
  0x1567 / tool) + the op dialog's record patches >= 0x1228 and 0x5d2 (cui_opdlg.c pf_apply). Without patches = the record as before.
  The dialog fields this makes live: formats/opdlg_port_audit.md 13.
- `gen_dyncontour_mcce`: FUN_1801b58f0 order - per chain, wall stock offset towards the band
  (sign rule of FUN_1801ac7e0 NOT DECODED), one engine call per resulting bound, level copies; refuses open chains,
  avoidance chains and finish passes (record 0x1241).
- `mcce_moves`: engine records -> moves; 'A' records linearised at max(op tol, 0.001) and tagged as true arcs
  (`mb_tag_arc`) for the post; Z interpolated along each record. SAFE rule for rapid-class records (CeFlags & 1): up to
  the feed plane, across, never rapid below the feed plane, plunge feed down (improvisation_review.md CAM small items
  part 3; Mastercam's own rapids are G0 to the record XYZ).

Test scripts: `tools/tests/dyn_cmp.py PART OPID OURS.m` (one op: our moves file vs the stored NCI - move counts,
rapids, entries, first entry, engagement), `tools/tests/dyn_batch_cmp.py CAM_CLI OUTDIR PART...` (every stored Dynamic
Mill op; its MCCE_DYN=0 "old generator" column no longer applies: the viewer has no such switch now),
`tools/tests/orfiles.py DIR TYPE [PART...]` (tools/oracle.py metrics over a directory of moves files),
`tools/tests/safety.py DIR` ("no rapid below the top of stock" check over a fileop output directory).

## Depth cuts (decoded 2026-10-16)
Sources: decomp/mcmill_hst2d.c (CHST2DProcess, CHST2DDepthCuts helpers), decomp/mcmill_all.c, decomp/mccore_depths.c.
- **Fields.** `CHST2DProcess::SetOperationID` (l.9101) -> `SetParams` (l.9211): record 0x868 depth cuts on -> process
  +0x59, 0x870 max rough step -> +0xb0, S16 0x878 finish cuts -> +0xb8, 0x880 finish step -> +0xc0, 0x892 (P10064 "Use
  island depths") -> +0x5a, 0x1740 -> +0x5b, 0x136a taper -> +0x98 (outer angle 0x1370 -> +0xa0, island angle 0x1738 ->
  +0xa8). +0x56 = ((+0x59 and +0x5a) or +0x5b) and style != 2; +0x98 is cleared unless 0x868 and not both angles < tol.
- **Count** (`FUN_1802ca5f0` l.15538 -> `FUN_1802ca490`, mm l.486414; same arithmetic as mccore `calc_depth_cuts`
  0x18089e520): dz = (T - zf) - nf fs; T - zf < tol or dz < tol -> nr = 0; else nr = (short)(dz/max), rough = max when
  |dz/max - nr| <= tol, else nr + 1 cuts of dz/(nr + 1); total = nr + nf (0 -> 1 rough). Off -> one cut at zf.
  zf / T from `FUN_1802ca790` (depth + stock 0x888, top of stock 0x2e8 / 0x2f0), as mccore `set_depths`.
- **Level i** (1-based, `FUN_1802ca590` l.15192): i > nr -> zf + (total - i) fs; else zf + (nr - i) rough +
  max(0, total - nr) fs.
- **Execution.** With +0x56 and +0x98 clear, `FUN_1802ca5f0` stores the level-loop count 1: `PackageSomething` (l.5480)
  runs the engine once per machining region at level 1, then `FUN_1802b11f0` (l.15759) queues a `CHST2DWorkItemDCS`
  (count total - 1, nr - 1, rough); its run `FUN_1802a5580` -> `FUN_1802bb260` (mm l.476064) copies the region's 0x88-byte
  entities total - 1 times, copy s shifted Z - d with d += rough for s < nr - 1, else += op +0x818 (record 0x880). Order:
  region by region, all levels of a region together. With +0x56 / +0x98 every level is regenerated; +0x56 drops the
  islands whose Z + 0x1778 is below level + tol (l.5669-5702); +0x98 offsets the walls per level with
  `ComputeOffsetsAtDepth` 0x18029f010 / `FUN_180050910` (tan(a)(z_top - z) + (stock + r)/cos(a) - r).
- **Port:** cam2d.c `hst_depths`, copies in cam2d_dyn.c `gen_dynamic_mcce` and cam2d_area.c `gen_area_hmm`. NOT DECODED:
  the island-depth filter's island list (islands kept: material left), the taper term (an op with taper + depth cuts is
  not regenerated). Dynamic Contour / Peel (bnci_peel l.9654 and FUN_1801b58f0 l.11126) use the same level formula
  (l.11405-11425) with their own per-level loop.
