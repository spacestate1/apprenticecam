# Surface Finish Blend (classic op 109 = 0x6d) - decode, port and oracle (2026-10-26)

Status: **the pass layout is decoded and ported; the projection onto the drive is a substitute** (checked against every corpus op
with a stored toolpath, section 6). Port: `viewer/cam3d_blend.c/.h`; oracle: `tools/tests/blend_test.c` + `blend_test.sh`.
Registration, ribbon item and dialog pages: formats/surf_classic3.md 0.1 / 3.3 (not repeated here).

Rule for this file: every statement cites decompiled code or Mastercam's own output. **measured** = read from the stored NCI of
the corpus ops (section 6), not from code. **ours** = a stand-in, labelled where used. NOT DECODED = not read from code.

## 0. Sources and citation keys
- **l.N**: `decomp/mcmsurf_all.c` line N (mcmsurf.dll).
- **core**: mccore.dll `break_chain_to_points` @1801ea480 and callees `FUN_1801ea9c0` / `FUN_1801ea980` / `creep_along_ent`,
  decompiled for this file (DumpDecomp "^(make_loft_surf|break_chain_to_points)$", depth 2, on a scratch copy of
  ~/mastercam-re/ghidra_core) into `decomp/blend/loft_core.c` (gitignored). `make_loft_surf` is not in mccore: mcmsurf imports it
  from **MCMill.dll** (objdump import table: `?make_loft_surf@@YAHDPEAUent@@FFNPEAFDPEAD@Z`).
- **mill l.N**: `decomp/mcmill_all.c` line N (make_loft_surf @180198770, l.283297-283757, "LoftSurf.cpp").
- Record = the .mcam op record = op struct + 0x68 (cur_op_exref + X = record X + 0x68).

## 1. What Blend does
Two chains (any geometry: lines, arcs, splines, a single point) are each broken into the same number of equally spaced points;
point j of chain 1 joined to point j of chain 2 by a straight line gives the **rulings**. In 3D mode each ruling is projected onto the
drive surfaces and measured in 3D; the passes are the curves through the points at the same fraction i / m of every ruling's length
- so pass 0 is chain 1, pass m is chain 2, and the passes morph from one chain to the other with a constant 3D spacing along each
ruling. The passes are then projected onto the drive (the classic projection family) and linked.

## 2. Op record -> the "lm" parameter block (FUN_1800caff0 l.133416-133494)
The lm context (FUN_1800cc7b0(0x6d) l.134118): +0xa8 tool block, +0xb0 surface block, +0xc0 the Blend block (calloc 0x78,
l.134278-134292: +8 = 0.001 at creation, +0x4c = 0).

| record | lm | meaning | source |
|---|---|---|---|
| 0x1420 (op +0x13b8) | +0xc0 +0x10 | max stepover; <= 1e-6 (lm +0x88) -> 0.05 (both units) | l.133470-133473 |
| 0x1438 (+0x13d0) | +0xc0 +0x20 | "% of stepover" along the chains; <= the system tolerance -> 1.0 | l.133458-133461 |
| 0x1428 (+0x13c0, short) | +0xc0 +0x48 | cutting method: 0 Zigzag, 1 One way, 2 Spiral (row index of combo 993) | l.133455; order measured 5.2 |
| 0x142a (+0x13c2) | +0xc0 +0x40 | 1 = Along (rulings across, passes along the chains), 0 = Across | l.133453; FUN_1800307f0 l.33246 |
| 0x142b (+0x13c3) | +0xc0 +0x44 | 1 = 2D (XY lengths, FUN_180030a90), 0 = 3D (projected 3D lengths, FUN_180030fb0) | l.133454; l.33256-33270 |
| 0x1440 (+0x13d8) | +0xc0 +0x50 | 3D projection Quick (1) / Thorough (0) | l.133462 |
| 0x1240 (+0x11d8) | +0xc0 +8 | total tolerance | l.133456 |
| - | +0xc0 +0x3c | chain sync mode = 0 (copied to g_chain_sync_mode, l.34179) | l.133452 |
| 0x2e0 / 0x1239 | +0xb0 +0x40 / +0x34 | stock to leave / compensate to tip (with tip: lm +0xa0 = corner radius) | l.133426, l.133436, l.133490 |
| 0x350 / 0x358 | +0xa8 +0x48 / +0x50 | tool diameter / corner radius | l.133447-133448 |
| 0x1288 / 0x1290 / 0x1298 / 0x12a0 | (msurf_map) | short gap: % flag / distance / % / type ((v % 100) % 10, 1 = Broken) | l.340021-340026 |

Max short gap for op 0x6d with the % flag: **0x1298 x D** (the raw value times the tool diameter, msurf_map l.340443-340448; the
corpus value 3.0 gives 3 D). msurf_map case 0x6d (l.340400-340412) also sets DAT_1804d7260 = 3 (the lm path) and msurf_slice_dist
(used only by the EMM pre-pass, 3.1).

## 3. Dispatch
### 3.1 Stage 1 (FUN_18012a960 case 0x6d, l.197756-197780)
Along + 3D + Thorough (0x142a = 1, 0x142b = 0, 0x1440 = 0) and no check surfaces (FUN_180268e10 = !DAT_1805034a1): first
**FUN_180133f10(0xb, 1, D, r)** and **FUN_180133f10(10, 1, D, r)** (l.202699-203677), the raster engine shared with ops 0x27-0x2b:
each builds a set of parallel sections of the drive (FUN_1801cfb60(kind, ..) l.309504 + FUN_1801d4df0 l.311890) spaced
msurf_slice_dist (0x6d: FUN_1802465b0(D, chord, 0.05 in / 1.27 mm) = 2 sqrt((D - chord) chord), else the fallback, l.383345; surf_classic3.md 9.1) - kind 0xb in the tool
plane turned 90 deg (bit 0xb of the mask 0xad0, the "tmptc90" files, l.202867-202898), kind 10 at 0 deg. These are the "EMM" model
that stage 2's FUN_180278890 projects onto (l.206376-206379). Then, always, FUN_18013a370 (l.197779), which for DAT_1804d7260 = 3
calls **FUN_18002fd90(3)** (l.206603-206609): the lm pipeline below. Its curves go to the "mfolds" file (FUN_1800c8730 stage 2 / 3,
l.131673; writer FUN_1800c8af0 l.131843) for the msurf projection.

### 3.2 The lm pipeline (FUN_18002fd90 l.32739)
FUN_1800cc7b0(0x6d) context -> FUN_1800caff0 (section 2) -> FUN_1800c0e10 (chains) -> FUN_1800c8730(2) -> **FUN_1800307f0**
(l.33213) -> FUN_180030080 (-> FUN_18028e8b0, the projection) -> FUN_1800bbef0 -> FUN_1800c8730(3) -> FUN_1800c18c0.
FUN_1800307f0: FUN_180031990 (the rulings, 4.1-4.3), then by lm +0x40 / +0x44:
- Across (+0x40 = 0): **FUN_180031610** - NOT DECODED (refused).
- Along 2D (+0x44 = 1): **FUN_180030a90** (4.5).
- Along 3D (+0x44 = 0): **FUN_180030fb0** (4.4-4.5).
Then with cutting method +0x48 = 2 (Spiral) FUN_1800c53a0 joins consecutive passes (l.33318-33336) - NOT DECODED (refused).

## 4. The algorithm (decoded)
### 4.1 FUN_180031990 (l.34050-34237): chains and spacing
- Reads the op back by id, chains_from_db; **exactly two chains** or warning 0x171 and error (l.34172-34177).
- step = stepover (lm +0x10); with Along and (3D with lm +0x4c = 0, or 2D) and fraction > 1e-6: **step = stepover x fraction**
  (l.34191-34198).
- `break_chain_to_points(chains, 0, 1, 2, step, "boundary.dat", &npts, ..)` (l.34208), then `make_loft_surf(0, NULL, 2, npts, step,
  &nseg, 0, ..)` (l.34211), then FUN_180031ed0(nseg + 1, npts, ..) (l.34219).

### 4.2 break_chain_to_points (core 1801ea480)
- FUN_1801ea9c0 (core): per chain, the segments between sync entries (chain entry type 2); sync mode 0 -> one segment per chain.
  Each segment's length is the sum of its entities' creep lengths (init_creep_info, 3D), a type-4 entry scales it (default 1.0).
  The segment counts must agree on all chains (else warning 0x1066). Per segment k: **n_k = (int)(factor / step x max over the
  chains of the segment length + 2.0)**, clamped 2 .. 0x7fff.
- Points: each chain's segment at spacing d = its own length / (n_k - 1), from the segment start, d accumulated (dVar1 += dVar2)
  and placed with creep_along_ent; later segments skip their first point. Points per chain = 1 + sum(n_k - 1). A single point
  entity gives n copies of the point (length 0).
- Corpus check: TUTORIAL 2 SWARF chain lengths 21.244 / 16.934 in, step 0.025 -> n = 851; the stored NCI vertices on the first pass
  sit at exact multiples of 21.244 / 850 along chain 1 (measured).

### 4.3 make_loft_surf (mill l.283297) and the rulings (FUN_180031ed0 l.34266-34318)
make_loft_surf reads the chains' points by column j and writes, per column, nseg + 1 points of a cubic through the chain points
(2 chains: one span, end tangents from points extrapolated by 0.25 of the span, chord-length weights local_238 / dVar30,
nseg = floor(distance between the chains' first points / step), 0 -> 1). The cubic interpolates its end points. FUN_180031ed0
keeps **only the first and the last row** of each column (dcGetSurfPt(.., row 1) and (.., row nseg + 1)): ruling j = the 2-point
straight line chain1[j] -> chain2[j].

### 4.4 FUN_180030fb0 (Along 3D, l.33586-33876)
- The rulings through the lm frame (param_1[0..8]), then **projected onto the drive** FUN_18028e8b0 (l.33671; NOT DECODED), each
  projected ruling filtered FUN_1800c7390(tol / 10) (l.33695; NOT DECODED).
- len_j = 3D polyline length (FUN_1800c16c0 l.33723), max = the largest.
- **m = (int)(max / stepover), m >= 1; if max / m > max(1.05 stepover, 2 tol) then m += 1** (l.33733-33745). len_j /= m.
- m + 1 passes: **pass i point j = the point at 3D length i x len_j / m along projected ruling j** (FUN_1800c7b60(1, ..) l.33843).
- With Thorough and no check surfaces (lm +0x50 = 0, FUN_180268e10) the curve set is doubled (local_98 = 2 (m + 1)) and
  FUN_1800317d0 / FUN_180030280 (l.33958, l.30280) fill the second half from consecutive passes (differences prev / next): the EMM
  path's companion curves; their use is NOT DECODED (the pass geometry above does not depend on them).
- lm +0x4c = 1 on exit.

### 4.5 FUN_180030a90 (Along 2D, l.33358-33585)
The same count rule on the unprojected straight rulings with **XY lengths** (FUN_1800c1520 l.33414), points at XY length
i x len_j / m (FUN_1800c7860 l.33501), then the lm frame; projection happens later (stage 2).

### 4.6 Not decoded
FUN_18028e8b0 / FUN_180290fb0 (the projection), the EMM projection FUN_180278890 and the raster model of FUN_180133f10, the cut
order / direction (where lm +0x48 is applied in stage 2 was not found), the links (gap motion geometry), Across, Spiral.

## 5. Measured (stored NCI, section 6)
1. The NCI vertices of each pass include every decoded pass point that the output filter keeps, plus extra points between them
   (the projection follows the drive between pass points).
2. **One way** (method 1): every pass runs in chain 1's direction, pass 0 (chain 1) first, pass m (chain 2) last. **Zigzag**
   (method 0, CONVERT TO 5 AXIS op 10): odd passes reversed. Checked by comparing points at equal fractions of the path length
   (p95 0.08 / 0.05 / 0.07 in, i.e. only the length drift; first and last positions equal).
3. Short gap (<= max short gap), gap type Broken: from the end of a pass straight up to the next pass's start Z, then across
   (the next start higher - every corpus link); else (ours, no corpus case) across at the end's Z, then down.
4. Approach: rapid to the clearance over the first point, rapid to its Z + feed plane, plunge feed down. Exit: rapid up the feed
   plane, rapid to the clearance.

## 6. Port and oracle
### 6.1 Port (viewer/cam3d_blend.c)
Decoded and ported: 2 (record), 4.1-4.5. Substitutes (**ours**): the projection = an exact drop of the ball (radius R + stock)
along -Z onto the drive triangles (mcsurf.c mcs_mesh at tol / 10), highest contact (vertex / edge / facet), tip = centre - R with
compensate-to-tip; the projected ruling = that drop sampled every 4 tol in XY; between pass points, midpoint subdivision where the
drop leaves the chord by more than tol / 2; output filter: points within tol / 2 of the chord dropped; long-gap link (retract over
the feed plane). Measured: 5.2-5.4. Refused: Across, Spiral, gap types other than Broken, tools other than ball; the caller
refuses check surfaces and non-Top planes (cam3d_blend.h). S7 (formats/surface_selection.md 8): blend_drive_from_tp refuses check / gouge
references and role 2 (containment) chains - their use by the lm engine is NOT DECODED.

**Wired into the app (2026-10-28):** CAM_BLEND (cam2d.h; the Mill / Router 3D gallery's "Blend", the right-click menu's Surface finish >
Blend; cam_cli "blend"); default record = the defaults file's first op 109 (Mill_MM op 38 / Mill_Inch op 44); cam2d.c gen_blend: not
exactly two chains -> "Finish/Blend needs exactly two chains" (FUN_180031990 l.34169-34177, mcmsurfres 369), Top tool plane only, the
chosen tool's D / r over the record's 0x350 / 0x358, the chains (blend_chain_from_ents, chord stepover x 0.02 as blend_test), the drive
mesh (cam3d_rough.c c3_mesh_op at tol / 10). Selection: the drive pick, then the chains with mcmill blend_tp_title's prompt
(mcmillres 2631 "Blend: define " + 2446 "chain " + n; chain_sync_msg left out) - FUN_18001c4d0 chain_it_sync (l.20273); Mastercam asks
for the chains after the op dialog (msurf_assoc l.16038 / l.16140-16158), here before it (ours). Dialog: Tool parameters, Surface
parameters 999, Finish blend parameters 1040 (opdlg_fields.tsv s1040; the radio orders Across / Along 1199 / 1200 and 2D / 3D 1138 / 1139
are the template's, mcmsurfres dialog 1040); the Blend... 1007 / Depth limits / Gap / Advanced sub-dialogs are not shown (greyed). Scene
gen_blend (a ruled surface between two lines, 10 mm ball). The engine's static type PL became BlPL (a name clash inside cam2d.c's unit).

### 6.2 Oracle (tools/tests/blend_test.sh, 2026-10-26)
Each op regenerated from its own record, chains (TP kind 7) and drive surfaces (TP kind 4); distances sampled every stepover / 5
(query) against stepover / 50 (target), both ways.

| part (op) | units | pts / passes (ours) | ref->ours p50 / p95 / max | ours->ref p50 / p95 / max | cut length ref / ours |
|---|---|---|---|---|---|
| 2017 TUTORIAL 2 SWARF (2), also X9 | in | 851 / 75 | 0.0003 / 0.0006 / 0.0036 | 0.0003 / 0.0006 / 0.0018 | 1397.1 / 1397.0 |
| 2017 TUTORIAL 2 SWARF_METRIC (2), also X9 | mm | 1081 / 95 | 0.0069 / 0.0149 / 0.3201 | 0.0069 / 0.0149 / 0.0395 | 44791.5 / 44770.6 |
| X9 MOLD BOTTLE (3) | in | 426 / 38 | 0.0004 / 0.0006 / 0.0045 | 0.0004 / 0.0006 / 0.0018 | 709.4 / 709.4 |
| X9 CONVERT TO 5 AXIS (10), Zigzag, NURBS curve to a point | in | 344 / 72 | 0.0005 / 0.0010 / 0.0073 | 0.0005 / 0.0010 / 0.0052 | 696.0 / 696.0 |

Pass count: 75 confirmed directly on SWARF (pass 1 starts at chain1[0] + (chain2[0] - chain1[0]) / 74, measured); on the others
the cut lengths agree within 0.05 % (one pass is ~1 %). p95 is within the op tolerance everywhere. The metric max 0.32 mm
(ref -> ours) is a few stored points near the inner chain (x 49..62, y -43) that leave the straight ruling in XY by ~0.3 mm: the
real projection is not a pure Z drop there (NOT DECODED). The 2018 copies of SWARF / MOLD BOTTLE store no toolpath.

## 7. Open questions
- FUN_18028e8b0 (and the EMM projection FUN_180278890 with the FUN_180133f10 raster model): why it moves points off the ruling in
  XY near walls; FUN_1802465b0 (the EMM slice distance).
- Where stage 2 applies the cutting method and builds the links (Smooth / Direct / Follow surface gap types).
- Across (FUN_180031610), Spiral (FUN_1800c53a0 / FUN_1800317d0), sync modes other than 0 (lm +0x3c is always 0 for op 0x6d).
- The lm frame param_1[0..8] (treated as identity: right for the Top-plane corpus).

## 8. N9-15 update (2026-11-09; audit formats/audit_2026-11-09_highspeed_surface2.md)
- **Across decoded (FUN_180031610 l.33877-33957)**: the passes are the rulings, one per chain point, in chain order, through the lm frame (identity). Step = stepover (no fraction, l.34191-34198); the Zigzag reversal of FUN_1800307f0 applies; Spiral does not (FUN_1800307f0 l.33312 tests Along).
- **Spiral decoded (FUN_1800c53a0 l.129412-129641)**: for each pass k < m, point i moves to (1 - s / L) P_k[i] + (s / L) P_{k+1}[i], s = the length of P_k up to the old point i. The last pass is unchanged.
- **Zigzag decoded (FUN_1800307f0 l.33278-33310)**: every second pass reversed (FUN_1800c4a50), not measured any more. FUN_1800bd990 (l.124184) applies the method again on the lm curves of the later stages (mode 5, FUN_1800bbfc0 l.123100-123125); the path that reaches it was not traced (the oracle's direction matches the FUN_1800307f0 rule).
- **Filter decoded (FUN_1800c7390 l.130729-130944)**: applied to each projected ruling on the Along 3D path with angle = tol / 10 (radians, l.33695): from each kept point, the following points are dropped while the direction from it to P[j] and P[j] -> P[j + 1] differ by less than that angle.
- **Gap types**: FUN_180140d80 decides (type 4 or longer than max short gap -> retract, else tags 0x10005 / 0x10006 / 0x10008 with the segment FUN_1801417c0); the tags' geometry is built later (NOT DECODED): types other than Broken stay refused. A long gap (no corpus case): FUN_180140d80 l.209853 chooses retract (0x189) unless retract is off and clearance on (0x179), FUN_180142ee0 / FUN_180142420: feed height f (absolute 0x190 or point + increment 0x198; with msurf_long_gapchk 0x12a3 an absolute plane below the point is the point), plane = max(height, f); up (feed at the retract rate 0x388 when rapid retract 0x1a9 is off), over, down to f, plunge.
- **Projection**: FUN_18028e8b0 -> FUN_18028be40 -> FUN_18028ca50 (968 lines) / FUN_18028e990, FUN_18028eef0, FUN_18028f420; default path FUN_180278890 over FUN_180133f10: not ported (surface / EMM model needed), the labelled drop stays.
