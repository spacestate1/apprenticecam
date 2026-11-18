# Classic Surface Rough toolpaths (mcmsurf.dll): survey and port (srough, 2026-10-24)

Mastercam 2017's classic **Surface Rough** ops: Parallel 5, Radial 6, Project 7, Flowline 8, Contour 9, Pocket 10, Plunge 44
(0x2c), Restmill 107 (0x6b), Multisurface Pocket 131 (0x83). Correction to formats/hst3d_port_plan.md 1.2: the engine is **in
mcmsurf.dll itself** (sources Rough.cpp, RsurfPly.cpp, ZSurfPly.cpp, MSurfPly.cpp, MSurfPlm.cpp, ZRest.cpp ... by the asserts /
m_fopen_hl names), not in mccore. mccore provides the pocket clearing (`process_pocket` path via mcmill), the tessellation and
`run_mctutl_zcut`.

Citation key: **l.N** = `decomp/mcmsurf_all.c` line N; "op+X" = the operation struct (cur_op_exref) offset X, **record = op + 0x68**
(the .mcam op record; e.g. op+0x2e8 tool diameter = record 0x350). Surface parameter block `rpock_prm` = op+0x11c0 = record 0x1228
(set_rpock_prm, decomp/mccore_pock.c l.4392); the op-10 pocket block `pock_prm` = op+0x13e0 = **record 0x1448** (set_pock_prm,
mccore_pock.c l.1437-1441) = the 2D Pocket's `prm_pocket` (record 0x1228, formats/pocket_engine_b6.md) shifted by **+0x220**.
Scratch dumps of the survey: `~/mastercam-re/work/srough/` (copied from the session scratchpad: pocket/, parallel/, dlg/, surf/).

## 1. Common pipeline

1. **bnci_surface @1800110b0** (l.11781) - the regen entry. `read_appmch_cfg` (l.18391) defaults: msurf_use_fzt = 1, msurf_use_emm
   = 1, msurf_use_emm_pocket / _parallel = 0, msurf_use_3rd_party_pocket = 0, read from `<apps>tp.cfg` questions 0x21.. when the
   file exists (none in the user's install). Per op type, l.11964-12045 / 12204 / 12513:
   - **op 10**: fzt stays 1 unless a ball end with r + stock < tol, op+0x1268 == 1, or tool types 0xe / 0x10 / 0x12 (0x19 with
     FUN_18021b330); emm is turned off unless stock < 0 with |stock| > r or msurf_use_emm_pocket (l.11985-11996). **Default path:
     fzt = 1, emm = 0, 3rd party = 0.**
   - **op 5**: fzt is always cleared (op 5 is not 9/10/0xf/0x6b/0x83, l.11964-11968); emm is cleared by default (l.11977-11979,
     the long test l.11999-12025 with msurf_use_emm_parallel = 0).
2. **FUN_1800135a0** (l.13212) runs, in order (l.13311-13356): FUN_18029e890 (memory set-up), FUN_180012e70 (restmill only),
   FUN_18023b510 (HST max stock engagement only), the containment chains (FUN_18012f750 writes tmptc.tmp and sets DAT_1805039d4 =
   "containment present", or FUN_18012b7c0 removes the files), the approximate start point -> DAT_1805138b0, **FUN_18012a310** (the
   machine model), **FUN_18012a960** (stage 1: the per-type slicer, switch on ms_tp_type l.197504), **FUN_18012e440** (stage 2:
   msurf_map, then the per-type toolpath / linking / NCI, l.199437-199484).
3. **msurf_map @180205a90** (l.339935) - op fields -> engine globals (common block l.340009-340048; per type below).
4. Output: FUN_180136670 @203958 (2267 lines, shared by every classic surface op) writes the NCI: feed op+0x310, plunge op+0x320,
   spindle op+0x328, clearance op+0x108/0x110/0x111, retract op+0x118/0x120/0x121, feed plane op+0x128/0x130 (l.204686-204704),
   markers -4 / -5 / -6 / -7. Its record cases and the move writer `thunk_FUN_180289380` are NOT DECODED.

Stage 1 per type (FUN_18012a960 l.197504-197756): Parallel 5/0xb -> FUN_180133860; Radial 6/0xc -> FUN_18013ae60; Project 7/0xd and
Plunge 0x2c -> FUN_18013a370; Flowline 8/0xe/0x2d/0x6e/0x70 -> FUN_180044c70; Contour 9/0xf and Restmill 0x6b -> FUN_18013b2f0 +
FUN_1802a14e0; **Pocket 10/0x83 -> FUN_1802a14e0(1, ..)**.

### Code sharing (closures of the stage-1 + stage-2 entries, ~/mastercam-re/work/srough/parallel/share.py)
| op | entries | closure | shared with op 5 |
|---|---|---|---|
| Parallel 5 | FUN_180133860 + FUN_180132430 | 1291 funcs / 110 736 lines | - |
| Radial 6 | FUN_18013ae60 + FUN_180132430 | 1237 / 103 610 | 1197 / 95 648 (stage 2 is the same function; its levels are FUN_1802b1a20 / FUN_1802b1180) |
| Project 7, Plunge 44 | FUN_18013a370 + FUN_180139c80 | 1548 / 176 064 | 457 / 43 265 (link / check model / NCI writer) |
| Restmill 107 | FUN_18013b2f0 + FUN_1802a14e0 + FUN_1802a0710 | 980 / 135 159 | 337 / 35 348 (it is the Contour / Pocket family) |
| Pocket 10 | FUN_1802a14e0 + FUN_1802a0710 | 358 + 784 funcs | shares the Z-cut family with Restmill / Contour |

So Project, Plunge and Restmill share only the generic layers (links, check model, drop / projection engine FUN_180290fb0, NCI
writer) with Parallel: **not ported in this batch** (next items, section 6). Project = the projection of a source op's NCI / curves /
points through FUN_180290fb0(1, ..) (l.206614-206776); Plunge = a point grid (FUN_18012c430) or a source op's points, each through
FUN_180290fb0 (l.206733-206762); Restmill = the contour Z-cut against a rest-material reference (FUN_18013b2f0, op+0x9b9, op+0x13a0..
0x13b0).

## 2. Surface Rough Pocket (op 10)

### 2.1 Drive model (fzt)
FUN_18012a310 (l.197313-197416) on fzt calls FUN_180224190 (l.197409; build_machine_model l.197413 otherwise): a **bare mesh, no
tool offset, no stock**; FUN_180229ba0 with param_4 = 1 tessellates at 0.5 x msurf_chord_dev (l.364012-364014; works at 0.9 x that,
l.364100). FUN_180223410 / FUN_18022d0d0 write tmpmm4.tmp (drive) / tmpmn4.tmp (check), the bounding boxes tmpmmlu.tmp (FUN_18025bde0
l.398094-398104). Mesh file (FUN_1802dc5c0 l.471337-471379): header, nVerts, nTris, xyz doubles, {3 int, 1 byte flag}; an extra
flag-1 triangle at the box's min-Z corner unless mutlPreprocessEMMDropCloth(). The tessellator itself (mccore) is NOT DECODED.

### 2.2 Z levels: FUN_1802a0d50 @1802a0d50 (l.438183) + FUN_18029cee0 @18029cee0 (l.435887)
Globals (msurf_map l.340018, l.340386-340391): rough_zdist = op+0x11e0 (record **0x1248** Maximum stepdown; < 1e-10 -> 0.05,
l.340498), rough_depth_is_inc = record 0x12e8, add corner radius 0x12e9, top adjustment 0x1300, bottom adjustment 0x1308, minimum /
maximum depth 0x1310 / 0x1318.
- Absolute (0x12e8 = 0, l.438245-438281): top = max(min, max depth), bottom = min(..). With record 0x1432 (Adjust for stock to leave)
  the stock is added to top, bottom and the user depth list.
- Incremental (l.438284-438401): min / max Z of the drive surfaces (tmpmm.tmp records with flag != 1); top = maxZ - top adjustment
  (or maxZ - stepdown when record 0x1440 "Keep top cut at max. stepdown", l.438380-438382); bottom = minZ + bottom adjustment + stock;
  top = max(top + stock, bottom) (l.438387-438391).
- 0x12e9 (Relative to): + corner radius on top, bottom and every list entry (l.438406-438417).
- **FUN_18029cee0**: the range [top .. user depths .. bottom] in segments; a segment with dz <= chord is skipped; n = ceil((dz -
  1e-12) / zdist), step = dz / n (equal steps), levels segTop - k step, k = 0..n-1; the last segment gets one more (the bottom
  itself); **the first level is the top value**; no segments -> one level at the top (l.435931-435964).
- Auto-detect flats (record 0x1441, FUN_1802e2040 l.474262): every planar face with |n . Z| > 0.9999 becomes an exact level and the
  intervals are re-split (l.474311-474456). Ported? see 2.6.
- The user depth lists (prm_srf_depths, FUN_1802a1b20 l.438855-438877): their place in the record is NOT DECODED.

### 2.3 Per-level region: FUN_180305ab0 @180305ab0 (ZSurfPly.cpp, l.492476)
- Tolerances (l.492960-492968): D1f8 = 0.9 chord, D200 = 0.05 chord, D208 = 0.009 chord, D210 = corner tolerance.
- **Tool model** (l.493058-493094): stock' = max(stock, -r); tube D218 = r + stock' (0 when <= D208), flat radius D220 = D/2 - r;
  ball end (D220 <= D208): D218 = D/2 + stock', D220 = 0. **One stock for walls and floors.** Check surfaces use record 0x1230.
- fzt (l.493266..): `run_mctutl_zcut(tmpmm4 -> tmpmm5, mem, D + 2 stock', D218, taper, .., chord, 0.0, box, nGroups, start[],
  step[], count[], &ok)` (l.494153-494157; the mccore export, exports/mccore.dll.tsv l.12148): the **tool-centre loops at each level
  Z** (records {n, z, n x (x, y)}, FUN_1802e3e60 l.475289), read back and matched by Z by FUN_180311fd0 (l.494234 / 494251,
  l.498868-498999). **The body of run_mctutl_zcut is NOT DECODED** (mccore, no dump).
- Containment: FUN_18029d130 (l.436072-436366) cleans the boundary (points closer than curve_chord_dev dropped, CW) and frames it;
  each level is re-seeded from containment record 0 (FUN_180386240 + FUN_180357c40, l.493647-493653) - the containment clips the
  level region (the boolean itself, FUN_180357f90 / FUN_180358800, is NOT DECODED). Containment offset FUN_180128390
  (l.196444-196457): off = record 0x12c0, + D/2 when record 0x12b8 == 0; mode = record 0x12c8 (double: Compensate to 0 Inside /
  1 Center / 2 Outside, dialog 999); mode != 1 and |off| > 1e-4 -> offsetWriteTcb(mode, off, ..), negated for mode 2.
- Output: the regions per level in a .rgn file with a trailer (levels, Z, view, tolerances, D220, D218, ...; l.493814-493922).

### 2.4 Clearing per level: FUN_1802a0710 -> FUN_180302350 -> FUN_18035b9d0 -> FUN_180373650 -> FUN_1803769e0
- msurf_map case 10 / 0x83 (l.340115-340206): forced op+0x1422 (keep tool down) = 1, 0x13e8 = 1, 0x1421 = 1, 0x1428 = 5.0; the HS
  cell size op+0x1430 = 5 % D (>= 0.5 chord, <= 0.3 stepover when roughing); finish passes / spacing forced 0 when Finish is off;
  minimise burial forced 0 unless method 0 or 7 (l.340150-340153); cutter_comp = 'N' (l.340443).
- Level order (FUN_180302350 l.491569-491578): top first unless the bottom-to-top flag (record 0x140a, Gap settings 817 "Bottom to
  top").
- **FUN_180373650** method choice (l.553279-553610): rough off -> FUN_1803769e0 finish only; **method 7 (High Speed)** with Spiral
  inside to outside (record 0x1478) or channel record 0x1628 < 1 -> mcmsurf's own HS engines FUN_180376fd0 / FUN_180374bb0 (by record
  0x13d0) - NOT DECODED; method 0 with quick zigzag (record 0x1420) and no leads -> FUN_180375780 - NOT DECODED; **everything else ->
  FUN_1803769e0**.
- **FUN_1803769e0** (l.554653-554779): the level region as pock_bound (loops flagged 1 = islands), pock_depth = Z,
  bnci_wr_marker(-4), method 7 -> 8, op+0x15d8 = 1, op+0x15ee = 0, op+0x15b2 = 0, tolerance op+0x13e0 = 0.001 in / 0.025 mm, then
  **mcmill `process_pocket`(bounds, a, b, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0)** (l.554779) - the 2D Pocket engine this program already
  ports (viewer/cam2d_pock*.c, reading pock_prm). So the cutting methods Zigzag / Parallel Spiral / PS Clean Corners / True Spiral /
  One Way, stepover, angle, inside to outside, minimise burial, finish passes and the helix / ramp entry are the 2D port's.
- Thin wall FUN_180372a00 (l.552905-553043), spring passes (record 0x1640, l.492206-492331), optimise cut order FUN_180377a60, the
  pre-pass record 0x1434 (l.491748-492013): see 2.6.
- Links / gaps: after clearing FUN_180226c30 builds the exact model and FUN_180290fb0(2, ..) projects the gap motion (l.438153 /
  438168); then FUN_180136670.

### 2.5 Op 10 record fields (dialogs: section 4)
Common: 0x2e0 stock to leave on drive, 0x34c tool type, 0x350 D, 0x358 corner r, 0x1230 stock on check, 0x1239 tip comp, 0x1240 cut
tolerance, 0x1248 max stepdown, gap block 0x1288-0x12b0, 0x12b8 / 0x12c0 / 0x12c8 containment offset, depths 0x12e8-0x1318,
0x1432 / 0x1440 / 0x1441 depth options; pocket block (2D +0x220): 0x1458 rough, 0x1459 finish, 0x145a climb, 0x145c method, 0x1460
stepover distance, 0x1468 stepover %, 0x1470 angle, 0x1478 inside to outside, 0x1479 minimise burial, 0x147a finish passes, 0x1480
spacing, 0x1488 finish containment boundary, 0x14c0 entry on, 0x14c2 helix / ramp, 0x1420 quick zigzag, 0x15c8 cutter comp, 0x15ca
of flat, 0x161a facing, 0x1640 spring passes, 0x1656 thin wall, 0x13d0.. High Speed dialog; heights 0x170-0x198.

### 2.6 Port (viewer/cam3d_srough.c, `gen_srough_pocket`)
Decoded and followed: the Z levels (2.2, absolute / incremental / relative-to / keep-top / adjust-for-stock), the tool model (2.3,
one stock, ball-end rule), the containment offset rule, the per-level clearing through the 2D Pocket port with the record's pocket
block at 0x1448 (2.4: method 7 -> 8 for the 2D engine, the tolerance override, keep tool down), the level order, the refusals.
**Ours** (labelled in the code):
- the drive mesh: OCCT BRepMesh of the selected solid (as OptiRough, decision 3D-2) or, for the oracle, the sampled Mastercam
  surfaces (section 5);
- the stand-in for run_mctutl_zcut: the slice of cam3d_rough.c (`c3_slice`: the tool inflated by the stock, the part of the mesh that
  collides with it at tip height Z, as forbidden tool-centre region) - free region = containment (or the drive's XY box, ours) minus
  the slice;
- the containment boolean (Clipper);
- the links between levels / regions: the 2D Pocket port's own retracts (FUN_180290fb0's gap projection is NOT DECODED).
Finish passes (2026-11-12): the keep-tool-down link msurf_map forces (0x148a = the pocket block's 0x126a) is ported for op code 0xe
(formats/pocket_engine_b6.md "Keep tool down 0x126a"): per level, machine_pocket's order (each pocket's roughing, then its finish passes),
the link inside the pocket's tool-centre bounds; 6 of the 7 corpus ops with finish passes generate (srough_oracle.sh now keeps the finish).
**NOT DECODED -> refused**: High Speed (method 7 on mcmsurf's own HS engine: the Mastercam default op holds it), Constant Overlap
Spiral / Morph (as 2D), quick zigzag, facing (0x161a), auto-detect flats (0x1441: refused, the planar-face scan needs surface normals
of the drive, not done here), the user depth lists, thin wall, spring passes, optimise cut order, the pre-pass 0x1434, check surfaces.

## 3. Surface Rough Parallel (op 5)

### 3.1 Parameters (msurf_map l.340069-340075, l.340386-340398, l.340441-340534)
| record | meaning | source |
|---|---|---|
| 0x2e0 | stock to leave (drive) | l.340009 |
| 0x1230 | stock on check | l.340015 |
| 0x1239 | tip comp | l.340016 |
| 0x1240 | cut tolerance (msurf_chord_dev; < 1e-10 -> nc_tol) | l.340017 / 340492 |
| 0x1248 | max stepdown (rough_zdist, min 0.05) | l.340018 / 340498 |
| 0x13d8 | max stepover (msurf_slice_dist, stored abs, min 0.05) | l.340070 / 340495 |
| 0x13e0 | machining angle (radians, used in sin / cos: FUN_180129b40 l.197055) | l.340072 |
| 0x13e8 | cutting method short (0 Zigzag mcmsurfres 313, 1 One way 314) | l.340073, dialog l.150334 |
| 0x13ea | use approximate start point | l.340074 |
| 0x13d0 / 0x13d1 / 0x13d2 | allow negative Z / allow positive Z / plunge control (0 multiple plunges, 1 cut from one side, 2 both sides) | l.340395-340397 |
| 0x1288 / 0x1290 / 0x1298 / 0x12a0 | gap: % on / distance / % of stepover / type ((v % 100) % 10: 0 Direct, 1 Broken, 2 Smooth, 3 Follow surface) | l.340020-340027 |
| 0x12e8.. 0x1318 | cut depths (as op 10, 2.2) | l.340386-340391 |

### 3.2 Model and line layout (stage 1, FUN_180133860 @202358)
- Machine model (emm = 0): build_machine_model(.., kind 1, stock, chk) -> FUN_180229ba0 -> **make_surf_model** per surface with
  offset = r + stock and flat radius R - r (l.364700-364705), ridge rule check_for_ridge(ent, 5.0) (l.364683-364694): the offset
  surface model; make_surf_model / check_for_ridge are mccore imports, NOT DECODED.
- Frame FUN_180129b40 (l.197048-197169): X' = cos a r0 + sin a r1 (the cut direction), Y' = -sin a r0 + cos a r1 (the step
  direction), Z' = the tool plane normal.
- Extents FUN_18012f9c0 (l.200261-200452): the model extent across the lines widened by |stock| + D/2; straight boundary edges
  parallel to the cut (spread < chord, length > 10 chord) give the first / last line positions, else the edge extremes with half a
  stepover outward.
- **Lines** (l.202481-202507): n = ceil(span / stepover); n < 2 -> one line at the middle; else the stepover is equalised to span /
  n (n + 1 lines), then lines are added at that spacing until the widened extent is covered. Containment: lines outside its box are
  dropped (FUN_18013ebb0, l.202508-202612), crossings per line FUN_180267000.
- Per line (FUN_1801fd1e0 l.335793 -> FUN_1801ff1a0 / FUN_1801dd550 plane sections of the model, FUN_1801f9e10 merge): one 2D
  profile (along-line coordinate, tool tip Z). **The section algorithm is NOT DECODED** (generic geometry).

### 3.3 Levels and the per-line rough (stage 2, FUN_180132430 @201702)
- Levels FUN_1802022d0 @337910 (absolute) / FUN_180204d90 (incremental: profile max - top adjustment, profile min + bottom
  adjustment): step >= max(2 x 0.9 chord, tol, 1e-10); n = int(range / step), +1 when range / n > step; step = range / n
  (l.337929-337965). FUN_180201660 (l.337865-337885): Z from the top, while Z >= bottom - 0.4 step; the previous level on the first
  pass = profile min + 100 (FUN_180204ce0 l.339456-339476); plunge control 2 + cutting method 1 runs each level twice
  (DAT_180502fe2).
- Per line FUN_1801fbbd0 (l.334929): line order 0.. (reversed by the approximate start point FUN_1801ee220 l.328186-328231); zigzag
  flips the direction after each line (l.335185-335187); profile clipped to containment (FUN_180205770).
- **FUN_1801fc390** (l.335240): each profile segment split at Z = level and Z = previous level; below-level parts marked level - 100
  - f78, above-previous parts previous + 100 + f78.
- **FUN_1801f8870** (l.332981-333274): the posz / negz / plunge-control state machine (0 not cutting, 1 cutting, 2 stopped): parts
  below the level are flattened to it; rising / falling in-band segments are kept, flattened or lifted ("+100" = skip) by
  allow positive / negative Z (1 = no, 2 = yes as stored +1, l.337920-337927); with plunge control != 0 everything after the first
  stop is skipped (one plunge per cut); the second pass lifts everything after the last in-band non-flat segment.
- Moves FUN_1801ee8c0 (l.328313-328731): segments in [level - f80, previous + f80] are cut; links FUN_180140d80 (l.209786-209957):
  gap > the max short gap (record 0x1290, or 0x1298 x stepover) -> retract to the clearance / retract plane and plunge; else the
  short-gap motion type (its exact mapping to the dialog labels and the gouge check FUN_180290fb0(2) NOT DECODED).

### 3.4 Port (viewer/cam3d_srough.c, `gen_srough_parallel`) - **unverified (no stored op 5 with NCI in any corpus)**
Decoded and followed: the frame, the line layout and stepover equalisation (3.2), the levels (3.3, absolute / incremental), the band
split FUN_1801fc390 and the state machine FUN_1801f8870 (ported line by line), the zigzag / one-way direction, the long-gap rule
(retract when the XY gap exceeds the max short gap). **Ours**: the per-line profile (a drop cutter of the tool inflated by the stock
on the drive mesh, sampled every cut tolerance - stands in for the offset model + plane sections, NOT DECODED); the short-gap motion
(a straight feed move at the higher of the two ends, then down - stand-in for the Broken / Direct / Smooth gap types); the straight-edge
snapping of the first / last line (FUN_18012f9c0 scans the model's boundary edges: here the mesh box). Refused: Follow surface
gap type, the approximate start point, check surfaces.

## 4. Dialog pages (CSurfaceSheet ctor FUN_1802b6550, l.450036-450530)
Every op: tab 1 Tool parameters (CToolProp), tab 2 **Surface parameters** (CMsSurfacePg, dialog 999). Op 10: tab 3 **Rough
parameters** (CAlt2, dialog 1310), tab 4 **Pocket parameters** (CMsPocPg, dialog 242; 1312 / CMsPocPgLev2 is op 0x83's). Op 5: tab 3
**Rough parallel parameters** (CMsParRoughPg, dialog 1039). Sub-dialogs: Cut depths 177 (CCutDpths, FUN_180029b00 / FUN_180027b40),
Gap settings 182 (CGapSetng, FUN_180082340 / FUN_1800834b0; not 1051), Advanced settings 183 (FUN_180033800 / FUN_180033b00),
Total tolerance 490, Facing 494, High speed 101 (CHspckdlg), Helix / ramp (mcmill HelixRampProp), Lead in / out 123, Thin wall
(mcmill ThinWallDlg). Control -> record tables: formats/opdlg_port_audit.md section "srough".

Dialog 999 (init FUN_1800dc360 l.143533-751, OK FUN_1800ddba0 l.144183-243): clearance 716 / 712 = 0x179 / 0x170 (713 / 714 abs /
inc 0x178), retract 25376 / 732 = 0x189 / 0x180 (564 / 567 0x188), feed plane 733 = 0x190 (730 / 731 0x198), top of stock 717 / 878 =
0x1431 (op 10) / 0x2e8 (869 / 872 0x2f0), tip comp 692 = 0x1239 (0 Tip, 1 Center), stock on drive 602 = 0x2e0, on check 831 =
0x1230 (disabled for op 10), compensate to 1105 / 1106 / 1107 = 0x12c8 double 0 / 1 / 2 (Center disables 1109), 1175 total /
additional = 0x12b8, offset 1109 = 0x12c0, direction 758 = 0x1250 (op 10: off, disabled).

Dialog 1310 (init FUN_1800f0650 l.154723-778, OK FUN_1800f1fa0 l.155314-353): total tolerance 25458 = 0x1240 (+ the filter
tolerance 0x320 when 0x33a), max stepdown 920 = 0x1248, Climb / Conventional 25113 / 25127 = 0x145a (1 = Climb), helix entry 739 =
0x14c0 (enables 736, caption by 0x14c2: mcmsurfres 364 "Entry - Helix" / 365 "Entry - Ramp"), use entry point 836 = 0x1408, plunge
outside containment 811 = 0x1409, align plunge entries 996 = 0x1434, facing 740 = 0x161a (enables 738, disables 664).

Dialog 242 (init FUN_1800f08a0 l.154779-971, OK FUN_1800f21a0 l.155355-455): Rough 719 = 0x1458, method list 741 = 0x145c (list
index = value with 4 <-> 7 swapped, l.154040-46 / 155390-399; names mcmsurfres 436-443), stepover % 25329 = 0x1468 (master; distance =
% x D / 100, or x (D - 2r) with "of flat"), stepover 722 = 0x1460, of diam. / of flat 1173 / 1174 = 0x15ca, roughing angle 723 =
0x1470, minimise burial 724 = 0x1479 (Zigzag only), inside to outside 721 = 0x1478 (not Zigzag / One Way), quick zigzag 1029 =
0x1420 (Zigzag only), Finish 720 = 0x1459, passes 725 = 0x147a, spacing 726 = 0x1480, spring passes 728 = 0x1640, cutter comp 680 =
0x15c8, finish containment 1031 = 0x1488, leads 734 = 0x8f0, thin wall 1345 = 0x1656, override feed 735 / 727 = 0x1654 / 0x1648,
spindle 737 / 729 = 0x1655 / 0x1650.

Dialog 1039 (op->page FUN_1800e8580 l.150308, page->op FUN_1800e9550 l.150910): total tolerance 25458 = 0x1240, max stepover 845 =
0x13d8, machining angle 723 = 0x13e0, cutting method 833 = 0x13e8, approximate start point 836 = 0x13ea, plunge control 879 / 883 /
888 = 0x13d2, negative Z 894 = 0x13d0, positive Z 900 = 0x13d1, max stepdown 920 = 0x1248.

Saving into .mcam: refused (the drive surface references of a classic surface op - TP records kind 4 class 0x100 - are entities this
program does not write; the drive here is a solid, whose surface-op reference is NOT DECODED), as OptiRough.

## 5. Oracle (corpus ops 10 with NCI on Mastercam surfaces)
See section 5 of the port notes in hst3d_port_plan.md 7 and tools/tests/srough_oracle.sh. The drive surfaces are sampled into an STL
by tools/surfs2stl.py (kinds 0 NURBS / 1 parametric / 10 trimmed, formats/mcam.md "Surface entities").

## 6. Next items
1. run_mctutl_zcut (mccore) - dump it and replace the c3_slice stand-in.
2. mcmsurf's High Speed engines (FUN_180376fd0 / FUN_180374bb0, ~30k lines each) - the default op's method.
3. The parallel section functions FUN_1801ff1a0 / FUN_1801dd550 and make_surf_model; the gap motion types; FUN_180290fb0.
4. Project 7 / Plunge 44 (FUN_18013a370 + FUN_180290fb0), Restmill 107 (FUN_18013b2f0 + the Z-cut family), Radial 6 (stage 2 shared
   with Parallel).
5. The classic drive selection (surfaces / faces, the "Toolpath/surface selection" dialog 1314) and saving the ops.

## 7. Port status (2026-10-25, on master)
- **Pocket (op 10)**: viewer/cam3d_srough.c gen_srough_pocket, as 2.6: levels (FUN_1802a0d50 / FUN_18029cee0), tool model, containment
  offset, per-level clearing through the 2D Pocket port (the op's pocket block 0x1448 moved to 0x1228; method 7 -> 8), the op's
  heights / feeds; record = the stored op or the default op 10 with every dialog patch (sr_record). Refused (NOT DECODED): High Speed
  with Spiral inside to outside or no channel (mcmsurf's own HS engine - **the Mill_MM default op 10**), quick zigzag, facing, auto-detect
  flats, thin wall (**the Mill_Inch default op 10 has it on**), spring passes, align plunge entries; Constant Overlap / Morph (as 2D).
  Ours: the slice (run_mctutl_zcut NOT DECODED), the drive's XY box as containment when no chain (the classic containment pick is NOT
  PORTED), the links (the 2D Pocket port's). S7 (surface_selection.md 8): containment = every chain of the op, open ones closed by their start
  (FUN_180128390); check surfaces -> mcmsurfres 249 "This operation cannot have check surfaces" (bnci_surface l.12645-12700); Parallel
  trims its lines to it (FUN_180133860 / FUN_180205770, surface_selection.md 8.2); Radial / Project / Plunge with a containment refused
  (NOT DECODED), their check surfaces refused.
- **Dialog**: the sheet's tabs as pages of this program's tree (ours: the container) - Tool parameters (this program's Tool page),
  Surface parameters 999, Rough parameters 1310, Pocket parameters 242 (op 10) / Rough parallel parameters 1039 (op 5); rows in
  formats/opdlg_fields.tsv (s999 / s1310 / s242 / s1039, from the init / OK functions of section 4); the cutting method list 741:
  rows 0..7 = strings 436 437 438 439 441 443 442 440, stored value = row with 4 <-> 7 swapped (l.153975-154036 / 155385-155402).
  Method-dependent enabling (Zigzag only etc.) is not ported (always enabled).
- BRACKET4 solid, D6 flat, Zigzag: 8 levels of 1.8 mm (-0.2 .. -12.8), 1844 moves.
- **Oracle** (tools/tests/srough_oracle.sh): PARALLELSTEEP.mcx-9 op 4 (op 10 Zigzag, D 0.5 flat, 11 surfaces + containment, inch): the 15
  levels equal Mastercam's (1.33 .. 0.01); ref->ours p50 0.011 / p95 0.079 in, ours->ref p50 0.012 / p95 0.049 in, cut length 2412 /
  2491 (ours without the finish passes: the forced keep-tool-down finish link is NOT DECODED). The op's tool must be given (--dia 0.5):
  cam_cli otherwise takes another tool of the part.
- **Parallel (op 5)**: viewer/cam3d_srough.c gen_srough_parallel, as section 8: the frame, line layout (stepover equalised, lines extended
  past the widened model), levels (FUN_1802022d0 / FUN_180204d90, per level all lines, the first at the top), per-line chains (split at
  gaps), the band split FUN_1801fc390, the state machine FUN_1801f8870, zigzag flips per line (empty ones too), plunge control 2 (Zigzag:
  every line twice; One way: the level twice), the moves of FUN_1801ee8c0 (in-band nodes cut, a restart links), the long-gap retract of
  FUN_180140d80. Refused: the approximate start point, plunge into previous cut 0x12a5, optimize cut order 0x12a4, absolute depths with tip
  comp Tip (the f00 offset), Center with a corner radius. **Ours**: the per-line profile (the drive rasterised to a height field at max(4
  chord, R/16) <= R/4, the stock-inflated tool's tip over it; off the model the model's lowest Z - stands in for the offset-surface model and
  its plane sections, NOT DECODED); the short-gap motion (a straight feed at the higher end; FUN_180141460 / FUN_180143630 NOT DECODED); the
  approach / retract geometry (clearance or retract, rapid to the feed plane distance, feed down); the short-gap % read as percent (NOT
  DETERMINED). No stored op 5 with NCI exists in any corpus: unverified against Mastercam.
- BRACKET4, default op 5 (D3, stepover 12, One way, +Z allowed / -Z not, both sides): 10 levels -0.2 .. -12.8 (+ the top 0.0), 4299 moves.

## 8. Op 5 stage 2 spec (decoded 2026-10-25; l.N = decomp/mcmsurf_all.c)
- **Globals** (msurf_map case 5 l.340069-340075, LAB_180206ec4 l.340383-340398): comp_to_tip 0x1239, msurf_chord_dev 0x1240 (< 1e-10 nc_tol),
  rough_zdist 0x1248 (< 1e-10 0.05), short gap: % flag 0x1288, distance 0x1290, % 0x1298, type 0x12a0 ((v % 100) % 10), check 0x12a2;
  opt cut order 0x12a4, plunge prev 0x12a5, entry r / a 0x12a8 / 0x12b0, depths 0x12e8 inc / 0x1300 top clearance / 0x1308 bottom stock /
  0x1310 / 0x1318 min / max; negz 0x13d0, posz 0x13d1, plunge ctrl 0x13d2, slice dist 0x13d8 (< 1e-10 0.05), angle 0x13e0, method
  0x13e8 (0 Zigzag, 1 One way), approx start 0x13ea, lead length 0x1400; op 5: max short gap = slice dist x % (l.340450); levels on always.
- **Frame** FUN_180129b40: A = cos a r0 + sin a r1 (cut), B = -sin a r0 + cos a r1; profile frame u = -B, a = A, z (l.336161-336169,
  FUN_1801fd010 l.335746).
- **Layout** FUN_180133860 l.202481-202507: s = slice dist; lo = first - (flag8 ? s/2 : 0), hi = last + (flag9 ? s/2 : 0); n = ceil((hi - lo)
  / s); n < 2: one line at the middle; else s = (hi - lo) / n, count n + 1 from hi down, extended while the widened model (|stock| + D/2,
  l.200337) reaches past; first / last from straight boundary edges (FUN_18012f9c0, tmpedg0). Containment drops / clips lines (l.202508).
- **Levels** FUN_1802022d0 l.337910: posz / negz coded 2 / 1 (allowed / not); absolute: st = max(step, 1.8 chord, tol, 1e-10), n =
  max(1, int(R / st)) (+1 when R / n > st), step = R / n; incremental FUN_180204d90 l.339482 inside the level loop: profile zmax - top
  clearance / zmin + bottom stock (top >= bottom), same step rule. Absolute + comp_to_tip: + f00 (NOT DETERMINED). Loop FUN_180201660
  l.337862: L = top; while bottom - 0.4 step <= L (L clamped to bottom): all lines; plunge ctrl 2 + One way: all lines again (second pass);
  P = L; L -= step. P on the first level per chain (FUN_180204ce0 l.339449): min z + 100 (>= L + 100 + f78).
- **Lines** FUN_1801fbbd0 l.334929: order 0..count-1 (reversed by the approximate start point); Zigzag + plunge ctrl 2: each line forward
  then reversed (second pass); Zigzag flips the direction after each line, op 5 also after empty ones (l.335185-335187); each level starts
  in the same direction.
- **Band split** FUN_1801fc390 l.335240 and **state machine** FUN_1801f8870 l.332981 (e = min(tol, 0.00009 chord), d = 0.009 chord): as
  ported in sr_band_split / sr_state (comments there).
- **Moves** FUN_1801ee8c0 l.328313: nodes with L - e <= z0 <= P + e are cut; others restart; the first move of a level approaches
  (FUN_1801408c0), a restart links (FUN_180140d80: gap > max short gap or type 4 -> retract (clearance when retract is off) and down; else
  the short-gap types 0x10005 / 0x10006 / smooth / 0x10008 - geometry NOT DECODED); a z change > d feeds.
- **Stream** cut.tmp: markers + 0x48-byte records {from, to (u, a, z), feed, msproj index, flags}; the NCI writer FUN_180136670 is NOT
  DECODED (here the moves go straight to the toolpath).

## 9. Surface Finish Parallel (op 11)
msurf_map case 0xb (l.340207-340212): stepover op +0x1298 (record 0x1300), angle +0x12a0 (0x1308), cutting method +0x12a8 (0x1310), approximate start
point +0x12aa (0x1312); levels stay off (no LAB_180206ec4: DAT_1805034a2 and the posz / negz / plunge block unset), so FUN_180201660 runs the lines
once (l.337851) and FUN_1801ee8c0 cuts every node but the gaps (l.328398 / 328456); with Z limits (msurf_use_zlimits op +0x1280 = record 0x12e8,
limits 0x12f0 / 0x12f8, l.340427-340429) the band split between them (l.328399-328418). Port: cam3d_srough.c sr_parallel(finish = 1) - the line
layout / profile / links of op 5 (section 8), no levels. Not in Mastercam 2017's 3D ribbon gallery (viewer/ui/layout.txt): reached by cam_cli
sfparallel and for stored ops. **Oracle** (tools/tests/finish_oracle.sh "SF"): PARALLELSTEEP.mcx-9 op 1 (D 0.25 ball, 0.02 in, zigzag): ref->ours p50
0.018 / p95 0.046 in, ours->ref p50 0.022 / p95 0.057 in; the floor matches (missed 0.07 %, extra 0.09 %); cut length 2373 / 3186.
