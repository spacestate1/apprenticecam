# Pocket engine port, op dialog batch 6 (2026-10-23): per-engine decode notes

The notes the six engine ports were written with (work trees ~/mastercam-re/work/tree_b6_*, merged into viewer/ by the
coordinator). Line numbers = decomp/mcmill_all.c unless stated; the extra Ghidra dumps they cite are in decomp/pocket_b6/
(names prefixed by the work tree: finish_, pspiral_, tspiral_, zigzag_ ...). Summary and regression: formats/opdlg_port_audit.md 10.
Merge notes: the engines share gen_pocket_v2's section loop (cam2d_pock.c, the finish agent's structure); One Way / Zigzag
and every loop engine use the shared entry (cam2d_pock_entry.c) - the 'pk_zz_rough' of the zigzag note became pk_zz_plan +
pk_zz_depth; file names in the notes = viewer/ files.

---

## Batch 6 "entry": pocket roughing entry (Pocket Entry page 3005, children 3010 Helix / 3011 Ramp)

Work tree: ~/mastercam-re/work/tree_b6_entry. All line numbers: decomp/mcmill_all.c.

### Files changed
- viewer/cam2d_pock_entry.c (NEW, included from cam2d_pock.c): the shared entry. Interface:
  `pk_entry_plan(f, op, &rb, S, z_first, &e)` (0 = refused, op->error set), `e.skip`, per depth
  `pk_entry_emit(b, p, &e, z, z_approach, ctol)` then (if `e.kind != PKE_PLUNGE`) the caller feeds to S, `pk_entry_free(&e)`.
- viewer/cam2d_pock.c: `#include "cam2d_pock_entry.c"`; PkPocket keeps a PkEntry (was hc / hr / helix); gen_pocket_v2's
  ramp refusal and its helix code replaced by plan / emit (same moves); pk_entry_helix (the zigzag engine's hook, same
  signature) rewritten over the shared entry; gen_pocket_v2 comment.
- viewer/Makefile: cam2d_pock_entry.c added to the cam2d.o / cam_cli dependencies.
- viewer/cam_cli.c (test aid, fileop path only): `--rp OFF:HEX` record patch (= a sidecar `RP OFF 0 HEX` line, i.e. an
  op-dialog edit; goes through cam_params_from_patches) and `--verify` (the chain path's verify) for `fileop`.
- tools/tests/fileop_all_entry.sh: fileop_all.sh with this tree's cam_cli (test only, not to merge).

### Where Mastercam chooses (the brief's candidates mapped)
| function | what |
|---|---|
| FUN_1801bc830 l.307784 | `_enter_pocket` (Pockentr.cpp asserts l.308087): THE dispatch, called per pocket cut start (param_1 rough boundary, param_2 cut start, param_3 = roughing) |
| FUN_1801bfcb0 l.310115 | wrapper: _enter_pocket with ignore_wire_islands |
| FUN_1801c38a0 l.312509 | finds a point chained with the pocket = user entry point (pock_start, user_defined_entry); called from spiral rough l.339447 / zigzag l.340350 with param_2 = 0 (only when entry off, helix + 0x12f0, ramp + 0x1358 / 0x1359) and FUN_1801bdc00 l.308736 with 1 |
| FUN_1801c0190 l.310341, FUN_1801bfd60 / FUN_1801bfde0 l.310158 / 310185, FUN_1801c03c0 l.310414, FUN_1801c1fc0 l.311434 | facing pocket types 1 / 2 (pock_facing_islands) entry - not this scope |
| FUN_1801bfce0 l.310131 | entry XY clearance: ramp 0x1350 / 2 + 0x1338, helix 0x12c8 |
| FUN_1801c1f80 l.311412 | entry height: top of stock - depth + Z clearance (ramp 0x1320, helix 0x12b8) |
| FUN_1801c1d80 l.311345 | helix (find_helix_center FUN_1801c04d0; writer FUN_1801c4a60 arcs when 0x12e1, FUN_1801c5020 linearized otherwise) |
| FUN_1801c14b0 l.311010 | helix "Follow boundary" |
| FUN_1801c30d0 l.312161 | ramp; line by FUN_1801c0ad0 l.310645 -> FUN_1801c0e80 l.310797 (scan at one angle) |
| FUN_1801c2440 l.311640 | offset_pock_bounds + delete_nested + merge (-> gls_offset, our documented substitute) |
| FUN_1801c4e10 l.313344 | one entry point: first = plunge_move / gr_lin_mill at the plunge rate, else FUN_1801e32a0 at feed (0x1308 helix / 0x1370 ramp ? feed : plunge) |
| FUN_1801c2770 l.311782 | make_entry_exit_envelope (used by FUN_1801c03c0, facing) - not used here |

_enter_pocket decision (l.307923-307990), ported in pk_entry_plan:
1. entry off (0x12a0 = 0), or open pocket (0x13fa = 4) with 0x13f0, or ramp with 0x1359 Ramp from entry point -> plain plunge at the start (with a chained point: plunge at the point, refused here).
2. helix (0x12a2 = 0): follow boundary 0x12d2 and not "on failure only" 0x12d3 -> FUN_1801c14b0; else FUN_1801c1d80 and, if it fails and 0x12d2, FUN_1801c14b0. (0x12d3 is read only when 0x12d2 = 1, so the Mill_Inch default 0x12d3 = 1 changes nothing while 0x12d2 = 0 - no gate needed.)
3. ramp (0x12a2 = 1) -> FUN_1801c30d0. Another type with entry on -> the failure path.
4. failure: skip byte = ramp ? 0x134a : 0x12d1; set -> the pocket is skipped (return -1; save_bound if 0x12e0 - note the engine reads 0x12e0 for the ramp too, never 0x134b; warning 0x955); else plunge at the start.
5. success -> link from the entry end to the start (FUN_1801e23c0 / FUN_1801e0df0).

### Ported steps
- Helix: unchanged geometry / moves (the existing port, now in pk_entry_plan / pk_entry_emit). New: H < nc_tol fails (l.311360);
  0x12e1 = 0 -> linearized helix FUN_1801c5020 (n = trunc(|sweep| / sqrt(8 tol / r)) + 1 points, >= 2, z linear, tol 0x12e8);
  entry feed 0x1308 (FUN_1801c4a60: feed rate if set, else plunge rate) GATED (see below).
- Follow boundary FUN_1801c14b0: rough boundary offset by 0x12c8, first outer loop, direction by 0x12d0, perimeter < 0x12d8 fails,
  start at the entity whose start is nearest S, slope tan(0x12c0) (< 1e-12 fails), H / tan = laps x perimeter + rest, start
  `rest` before the loop start walking back from the last entity, z drops entity length x tan (clamped at the depth),
  lines at the entry feed, arcs helical (0x12e1) or linearized at 0x12e8; end = the loop start.
- Ramp FUN_1801c30d0 + FUN_1801c0ad0 + FUN_1801c0e80: all as in the comments of cam2d_pock_entry.c (offset by slot / 2 +
  0x1338; scan rows ymin + nc_tol .. ymax - nc_tol, max(10, trunc(2 span / stepover) + 2) rows; pairs with inside midpoints;
  >= min length (D % 0x1360, recompute_tool_dia_dependent_fields l.314905); closest to S; XY angle 0x1340 or Auto 0x1348
  (0..170 deg by 10, then 10 halvings); cut to max length (D % 0x1368) from the end nearest S; zig / zag 0x1328 / 0x1330
  backward walk; slot 0x1350 with half circles, direction 0x1349; feed 0x1370).
- Skip on failure: HS engine skips the pocket (as before for the helix, now also 0x134a for the ramp); the zigzag engine
  (one region for all pockets) refuses as before.
- Center on entry point 0x12f0 / Align 0x1358 / Ramp from entry point 0x1359: only act with a point chained with the pocket
  (FUN_1801c38a0); without one 0x12f0 / 0x1358 do nothing and 0x1359 means a plain plunge (l.307929).

### NOT DECODED / refused / not done
- Entry from a chained entry point (user_defined_entry: find_helix_center's user branch l.310569, FUN_1801c0e80's single row
  l.310914, the LAB_1801bce44 path): refused "pocket entry from a chained entry point: NOT DECODED" when a point is chained and
  the entry is off, or helix + 0x12f0, or ramp + 0x1358 / 0x1359.
- The link from the entry end to the start around islands (FUN_1801e23c0 / find_path_around_island): as before, a straight
  feed; when it would leave the rough boundary the entry counts as failed (the existing rule of this port, now applied to the
  ramp and follow boundary too - it is NOT Mastercam's behaviour, which goes around; documented at the code).
- Save skipped boundary 0x12e0 / 0x134b (save_bound creates geometry) and warning 0x955: not done - leave the controls greyed.
- Facing pocket types 1 / 2 entry (FUN_1801bfd60 etc.): not ported (not reachable: gen_pocket_v2 has no facing).
- The user-entry rotations of the zigzag rough angle: everything is in world coordinates (the decomp's rotations cancel).

### Ours (labelled in the code)
- Row / entity intersection kernel for the ramp scan (pock_bound_line_inters is mccore): lines half-open, arcs by asin,
  tangency excluded; the midpoint inside test (gls_inside) filters as Mastercam's does.
- Zero-length entry moves are not written (FUN_1801c5020's first point = the current position; a start exactly at an entity end).
- Ramp guard: both zig / zag drops <= 1e-12 or > 100000 passes -> the entry fails (the decomp's loop would not end; dialog min 0.1 deg).
- Linearized helix / boundary with tolerance <= 0 refused.

### Stored values / gating (rules item 3)
| field | A001 op13 | Mill_MM | Mill_Inch | Mill.mcam 5/9, mc-test01 2 | handling |
|---|---|---|---|---|---|
| 0x12a2 type | 0 | 0 | 0 | 0 | ramp honoured (no op holds 1) |
| 0x12d2 follow bnd | 0 | 0 | 0 | 0 | honoured |
| 0x12d3 on failure only | 0 | 0 | 1 | 1 | honoured (only read when 0x12d2 = 1: no output change) |
| 0x12e1 output 3D arcs | 1 | 1 | 1 | 1 | honoured (0 = linearized) |
| 0x12e8 tolerance | 0.05 | 0.05 | 0.005 | 0.005 | used only when 0x12e1 = 0 |
| 0x12f0 / 0x1358 / 0x1359 | 0 | 0 | 0 | 0 | honoured as above |
| 0x1308 helix entry feed | 0 | 0 | **1** | 1 | **GATED**: feed rate only when the record byte was edited (rpatch) and is 1 |
| 0x1370 ramp feed | 0 | 0 | 0 | 0 | honoured |
| 0x12d8 if length exceeds | 100 | 100 | 1 | 1 | used by follow boundary only |
Ramp fields 0x1310.. hold the defaults everywhere (no op uses the ramp).

### Oracle / regression
- No corpus op uses the ramp (pockets.tsv: "entry 1 1" none; 6 ops off, 3 helix). Mastercam reference for ramp: none.
- fileop: 0/189 differ. Reference ops moves + NC byte-identical to the main tree's cam_cli: Mill.mcam 5 / 9, mc-test01 2,
  A001 13. oracle_refs --type pocket output identical: A001 13 0.031 p95 / 0.057 max, Mill 5 0.008 / 0.011, Mill 9 0.007 /
  0.011, mc-test01 2 0.005 / 0.005.
- Patched runs (cam_cli fileop --rp ... --verify; verify = final-depth replay; plus a check for horizontal rapids below the top):

| part / op (engine) | patch | result |
|---|---|---|
| mc-test01 2 (HS) | ramp | ramp passes along y=-0.058 trimmed to max 0.5, no gouge, no low rapid |
| mc-test01 2 | ramp + auto angle | tilted line, no gouge |
| mc-test01 2 | ramp + slot 0.1 | half circles r 0.05, no gouge |
| mc-test01 2 | ramp + 0x1359 | plain plunge at start |
| mc-test01 2 | follow boundary (direct) / + linearized | boundary ramp, no gouge |
| mc-test01 2 | follow boundary + on failure only | = helix (unchanged) |
| mc-test01 2 | linearized helix (0x12e1 0) | chords at tol 0.005 |
| mc-test01 2 | radii 500% + skip | roughing skipped, finish only |
| A001 13 (zigzag) | ramp / auto / slot 4 / slot CCW | no gouge |
| A001 13 | follow boundary (0x12d8 100 > perimeter) | fails -> plunge (as decoded); with 0x12d8 10: boundary ramp |
| A001 13 | radii 500% + skip | refused (zigzag engine cannot skip, as before) |
| A001 13 | 0x1308 = 1 edited | helix at F1000 (feed) instead of F600; unedited: F600 as today |
| CHGREC 2 / BLOCK 3 patched to method 8 | helix / ramp / auto / follow bnd | no gouge |
- Pre-existing (not from this change): A001 13 zigzag finish starts with a diagonal rapid from the last cut at depth to the
  clearance (gen_pocket_zigzag) - in the base output too.

---

## Batch 6 "tspiral": pocket roughing methods 5 (True Spiral), 1 (Constant Overlap Spiral), 4 (Morph Spiral)

Files: viewer/cam2d_pock_tspiral.c (NEW, included from cam2d_pock.c before the entry section),
viewer/cam2d_pock.c (gen_pocket_v2 hook only). Ghidra dumps: decomp_tspiral_core.c (mccore center_gravity_pock_bound,
intersect_pock_bound, restrict_angle, pock_bound_line_inters), decomp_tspiral_disasm.txt (mcmill 1801eee80..1801ef030
and the qsort comparators 1801ef2e0 / 1801ef300), decomp_tspiral_pbmap.c (mccore pbmap engine, 59 functions, for method 1).

### gen_pocket_v2 changes (cam2d_pock.c)
- method 5 accepted; method 1 and 4 get specific NOT DECODED errors (below); others unchanged.
- channel-mode (0x1408) and "stepover between R and D" refusals now gated on method == 8 (High Speed only:
  FUN_1801ede50 l.339401 tests method 8 for the channel; FUN_1801d1740 is called only from FUN_1801d00d0).
- roughing loops: method 5 -> pk_ts_loops, else pk_hs_loops (unchanged).
- the channel / S-shape link (FUN_1801ce310) is now applied for method 8 only. Method 8 output is bit-identical.

### Method 5 True Spiral (Pocktrue.cpp) - PORTED
| step | source |
|---|---|
| per pocket (one outer rough bound + its islands) | FUN_1801ede50 l.339337 case 5 -> FUN_1801ef3d0 l.340034 |
| centre = center_gravity_pock_bound of the (last) outer bound: length-weighted centroid of the boundary curve, arcs in floor(|sw|/0.1745)+1 pieces at mid-angles | mccore 0x1805e28f0 (decomp_tspiral_core.c) |
| half circle i: d = (i+0.25) step/2, r = d + step/4 = step (i/2 + 0.375); centre.x -/+ step/4 (even / odd, sign flipped when conventional), start angle / sweep by climb (0x123a) | build_arc_pock FUN_1801ee880 l.339597-339690 |
| intersections with every bound (deduped within tol per bound), kept when on the half's side (even dy>0, odd dy<0), plus the arc's ends; angles restricted to [-pi/2, 3pi/2) / [pi/2, 5pi/2), sorted ascending (climb) / descending | l.339700-339800, restrict_angle, comparators 1801ef2e0 / 1801ef300 (disasm) |
| a piece between consecutive points is a segment when its mid-angle point is in the machinable area | l.339800-339850 (pt_in_machinable_area) |
| stop: two half circles in a row with no segment whose last sorted point is not strictly inside an outer bound (after the first segment) | l.339860-339880 + disasm 1801eef4e (FUN_1801ef330 arg = last sorted point), FUN_1801ef330 l.340010 (pt_in_ctour == 1) |
| in2out (0x1258): segments in order, then one pass around the outer bound from the projection of the last position | FUN_1801ef550 l.340090, FUN_1801ef030 l.339903 |
| out2in: pass around the REVERSED outer bound from the projection of the last segment's end, then segments in reverse order, reversed | FUN_1801ef3d0 l.340060-340075, FUN_1801ef550 |
| links between pieces: straight feed when inside | FUN_1801e0f80 -> FUN_1801e23c0 l.332360 |
| entry at the first piece's start: the existing helix (FUN_1801bfcb0 -> FUN_1801bc830 -> FUN_1801c1d80) | cam2d_pock.c unchanged |
REFUSED (NOT DECODED): a link with a section outside the machinable area (find_path_around_island / FUN_1801e12b0);
in2out with no segment at all (FUN_1801ef030 would start from the caller's uninitialised position).
OURS / substitutes: intersections = exact line/circle carriers kept within nc_tol of both entities (role of mccore
ints_on_gt_new, not decoded); pt_in_machinable_area = existing pk_pt_ok; a rough boundary with several outer loops is
cut loop by loop in list order (sort_pock_bounds order NOT DECODED, as for High Speed); outer bound orientation
(CCW outer, as pk_region) verified only for climb (the coaster's final pass is G3); conventional untested (no corpus op);
FUN_1801e1cb0's min_arc_length arc->line conversion not applied; default climb = 1 when the op has no record (ours).

### Method 1 Constant Overlap Spiral (POCKOVLP.cpp, FUN_1801e8650 l.335846) - NOT PORTED, refused
Decoded structure: refuses when stepover <= tolerance (0x1278 abs, pock_prm+0x50; warning 0x964); orients the bounds
by climb xor in2out; first cuts the outer bound (in2out = 0) or an island (in2out = 1) with FUN_1801e9a90
(FUN_1801e9ee0 when Minimize tool burial 0x1259 is on, on the medial axis); then a stock bitmap loop: init_pbmap
(cell = 0x1278), verify_pock_bounds, repeat { pbmap_area (stop when unchanged twice), pbmap_to_pbmap_bnds (contours of
the uncut cells), FUN_1801e85c0, reverse when conventional, FUN_1801e8470 / FUN_1801e8540 (start), pbmap_init_tool +
pbmap_bnd_to_tp (tool path from the contour), filter_pbmap_tp (0x1278 / 2), out2in: FUN_1801e9020 writes directly,
in2out: FUN_1801e9180 to the temp file pockovlp.tmp read back by FUN_1801e9250; FUN_1801e8f90 updates the map },
then the remaining bounds via FUN_1801cbd20 / reorder_pock_bound / FUN_1801e9a90.
What is missing: the mccore pbmap engine (~4000 decompiled lines: pbmap_to_pbmap_bnds_ext, pbmap_bnd_to_tp,
pbmap_verify_arc_ext / _line_ext, pbmap_diff_ext, FUN_1805cf180 ...; dumped in decomp_tspiral_pbmap.c, not analysed)
and, for both oracle ops (BLOCK op 3 2017 / X9 hold 0x1259 = 1), the medial axis (machine_pocket l.322548:
FUN_1801d9f60 tree ~2900 lines, used by FUN_1801e9ee0 / FUN_1801e9020 / FUN_1801e9250). The refusal names both.
0x1278 (pock_prm+0x50) = the map cell size / tolerance; 0x1270 (pock_prm+0x48, %) is read once: when 100 - 0x1270 <
stepover % (0x1248, +0x20) the pbmap tool radius is stepover - R - 0x1278, else stepover - R (l.335990-336000).
0x1281 (display stock) is display only (filter_pbmap / draw_filtered_pbmap, +0x59).

### Method 4 Morph Spiral (FUN_1801de750 l.329829) - NOT PORTED (stretch, refused)
Runs on the medial-axis family (FUN_1801d8aa0, FUN_1801d9110, FUN_1801d7830, FUN_1801da3c0, FUN_1801db4b0 ...; tree
3189 lines at depth 3). Needs the medial axis FUN_1801d9f60 decoded first.

### Oracle (part units, inch): p50 / p95 / max
| op | before | after ref->ours | after ours->ref |
|---|---|---|---|
| engraved coaster (X9) op 1, method 5 | FAILED (method 5 NOT DECODED) | 0.004 / 0.005 / 0.005 | 0.004 / 0.005 / 0.105 |
| BLOCK.mcam (2017) op 3, method 1 | FAILED | FAILED: Constant Overlap Spiral NOT DECODED (pbmap engine; burial medial axis) | - |
| BLOCK.mcx-9 (X9) op 3, method 1 | FAILED | same | - |
Coaster: helix entry, every spiral arc and the clipped last arc end (-1.6011, -1.1284) match the NCI exactly; the
0.004 p50 is chord sampling. The 0.105 max ours->ref is the retract after the roughing: Mastercam feeds up to
0.075 then rapids, we feed to the feed plane 0.2 (shared retract code pk_retract / cam2d.c retract_up, not method 5).
Regression: fileop 0/189 differ; High Speed refs unchanged (A001 op 13 0.031/0.057, Mill op 5 0.008/0.011,
op 9 0.007/0.011, mc-test01 op 2 0.005/0.005 - identical to the main tree's cam_cli).

---

## Pocket roughing methods 2 Parallel Spiral / 3 Parallel Spiral Clean Corners (agent "pspiral", batch 6)

Changed files (vs main tree): `viewer/cam2d_pock_pspiral.c` (new), `viewer/cam2d_pock.c` (include + dispatch in
gen_pocket_v2, header comment), `viewer/Makefile` (dependency lines). Dumps: `decomp_merge.c` (mccore merge_pock_bounds
internals, Ghidra), `cc_ea750.txt` / `dis_ede50.txt` (call-site disassembly). `tools/tests/fileop_all_ps.sh` = fileop_all.sh
with C = this tree's cam_cli.

### Ported steps (decomp/mcmill_all.c line numbers)
| step | source | ours |
|---|---|---|
| dispatch case 2/3, pock_clean_corners = (method == 3), copy of the bounds for links | FUN_1801ede50 l.339480 | gen_pocket_v2 |
| ea750 extra args: point + "loop cut" flag reset to 0 per pocket | disasm 1801ee1a0 / call 1801ee21c (Ghidra decomp dropped R8/R9) | first loop starts at entity 0 |
| clear +0x45 on the start loops | FUN_1801ea750 l.337214 | tag = 0 |
| loop generation: cut/write all non-island loops; offset by stepover 0x1248, halved once; min fillet min(min_fil, 0.785398); drop loops larger than parent; first new loop current, others at head of waiting list; merge with islands | FUN_1801ea750, FUN_1801ea5d0 l.337118, FUN_1801ea3b0 l.337009, link_pock_bound mccore_pock2.c l.137 | pk_ps_loops |
| waiting loop pick: entity start nearest the last start (get_ep(..,1) = start, mccore_pock.c l.4273), strict <, reorder | FUN_1801ea450 l.337034 | pk_ps_loops |
| +0x45 survives merge_pock_bounds (outer piece's flag) | mccore FUN_1805de250 / FUN_1805dfc50 (decomp_merge.c l.607 / 617 / 863 / 941) | tag kept on merged loop |
| in2out 0x1258: temp file is a backward-linked list, read from the last | FUN_1801eb050 l.337489, FUN_1801eaca0 l.337348 | order reversed |
| loop cut: start entity nearest current position (no split), first loop entity 0; entry for the first loop | FUN_1801e9a90 l.336573, FUN_1801c3b20 l.312596 -> FUN_1801bfcb0 -> FUN_1801bc830 | existing helix / plunge of gen_pocket_v2 |
| link between loops | FUN_1801e0f80 (pock_move_avoid_islands) | existing High Speed code path: straight feed when FUN_1801e0fd0 holds, else the HS port's retract / re-plunge (find_path_around_island NOT DECODED, as HS). HS-only channel link (FUN_1801ce310) skipped for 2/3 |
| clean corners | FUN_1801e9420 l.336373, FUN_1801eada0 l.337401 (pock_bound_line_inters = infinite line, bound_line_inters mccore_pock.c l.4094; pt_in_machinable_area l.1334), constr_ns_arc | pk_ps_corner / pk_ps_clip / pk_ps_cut |

Clean corners detail: turn sharper than 30 deg (dot <= 0.866), d = unit(t_in - t_out), s = cross(t_in, d),
Q = P + d (1/|s| - 1) D/2 (cur_op+0x2e8 = tool dia), Q clipped to the nearest bound crossing, needs Q and mid(PQ) in the
machinable area; skipped after the loop's last entity and when |s| <= 1e-4; up to 10 tries, dist x 0.8 when the corner
loop crosses the bounds; loop = line P->A, arc A->B about C (clockwise when cross(t_in,t_out) > 0), line B->P.
Only loops with +0x45 (made by the stepover offset) get it; the rough boundary loop itself not.

### NOT DECODED -> refused
- **0x1259 Minimize tool burial = 1** (FUN_1801e9ee0 l.336785 -> FUN_1801dae10 / FUN_1801daeb0: links walk the medial
  axis built by FUN_1801d9f60 / POCKMDL.cpp, ~6000 decomp lines): refused "pocket Parallel Spiral with Minimize tool burial
  (medial axis links): NOT DECODED". Every corpus method-2 op has it on (and Mill_Inch defaults). Diagnostic switch
  `PK_PS_MEDIAL_FALLBACK=1` ("ours", off by default) instead takes e9ee0's own fallback branch FUN_1801e0f80 for every link
  (what Mastercam does when FUN_1801dae10 fails). Used only to measure the oracle below.
- unchanged from HS: ramp entry, find_path_around_island, user-defined entry point (pock_start), sort_pock_bounds.

### "ours"
- Start entity of offset loops (pk_ps_restart / pk_ps_rb_start): ccomp_pock internals are not decoded (gls_offset is the
  documented substitute and starts anywhere). Measured: Mastercam's offset loops start with the offset of the source loop's
  entity 0 (CHGREC op 2 2017/X9: the innermost loop - cut from its entity 0 - starts with the offset of chain entity 0,
  the bottom line from its left end). Each offset loop is rotated to the entity start nearest source start + |d| x left
  normal. Applied only on the 2/3 path (HS untouched).
- Order of several pieces of one ccomp / merge result: gls_offset's order (substitute).
- PK_PS_MEDIAL_FALLBACK (diagnostic, above).

### Oracle (ORACLE_FILEOP=1, ref->ours / ours->ref p50 / p95 / max, inch)
| op | before | after (default) | after, PK_PS_MEDIAL_FALLBACK=1 |
|---|---|---|---|
| CHGREC_EX1_REV-A.mcam op 2 | FAILED method 2 NOT DECODED | FAILED burial NOT DECODED | 0.000 0.010 0.010 / 0.000 0.139 0.350 |
| CHGREC_EX1_REV-A.mcx-9 op 2 | same | same | 0.000 0.010 0.010 / 0.000 0.139 0.350 |
| PROJECTLETTERS .mcam / .mcx-9 op 2 | FAILED chain rebuild | FAILED chain rebuild | FAILED chain rebuild |
| ROUGHPROJNCI .mcam / .mcx-9 op 1 | FAILED chain rebuild | FAILED chain rebuild | FAILED chain rebuild |

CHGREC: the roughing matches Mastercam move for move (both loops, starts, link). The 0.35 ours->ref is the finish pass
entry, shared code outside this task: our finish starts at the first chain entity's midpoint (1.5, 0.8125) (the HS
port's measured model), Mastercam at the start of the finish loop's entity 0 (1.0625, 0.8125) - this op has leads off
and 0x1269 (pock_prm+0x41, FUN_1801cc170 l.318169: reorder to the closest point) = 0. Experiment (not delivered): starting
the finish at entity 0 gives 0.000 0.004 0.004 / 0.000 0.004 0.011 on both files. Before changing the finish start the
lead / 0x1269 dependence in FUN_1801cc170 / FUN_1801bdc00 must be decoded (HS refs have leads on and start at the mid).

PROJECTLETTERS / ROUGHPROJNCI: fail before the engine. Their TP chain records reference entity ids that are not in the
file (ROUGHPROJNCI: ids 8676, 8596, 8597, ... missing of 1 chain; PROJECTLETTERS 6 of 18 chains); cam_fileop.c refuses
such ops by design ("missing geometry", l.562) - mcam_ops.py silently drops those ids. Limit not changed. (Also
burial = 1 on both, so they would be refused by the engine anyway.)

Method 3 and 0x1258 = 0 have no corpus op; smoke-tested only (forced method 3 on A001 chain 0, sharp rectangle: corner
loops emitted at the inner loops' corners into the corner, none on the outer boundary loop).

### Must-not-change
- fileop: 0/189 differ.
- High Speed / zigzag references unchanged: A001 op 13 0.000 0.031 0.057 / 0.000 0.027 0.050; Mill.mcam op 5
  0.000 0.008 0.011 / same; op 9 0.000 0.007 0.011 / same; mc-test01 op 2 0.003 0.005 0.005 / same.

---

## Batch 6 "zigzag": Pocket roughing, cutting methods 6 One Way and 0 Zigzag

Files (work tree ~/mastercam-re/work/tree_b6_zigzag):
- `viewer/cam2d_pock_zz.c` (NEW, included from cam2d_pock.c just before gen_pocket_v2): the engine, `pk_zz_rough`.
- `viewer/cam2d_pock.c`: `#include "cam2d_pock_zz.c"` after pk_retract; gen_pocket_v2 accepts methods 6 / 0 (`zz`),
  HS-only refusals (channel mode 0x1408, stepover between R and D, stepover <= 0) now apply to method 8 only; zigzag with
  Minimize tool burial (0x1259) refused; roughing dispatch `if (zz) ... pk_zz_rough(...)`.
- `viewer/cam2d.c` gen_pocket: `PK_ZIGZAG_DECODED=1` (env, testing only, ours) sends method 0 to gen_pocket_v2;
  default stays the correlated `gen_pocket_zigzag` (stored ops unchanged).
- `viewer/Makefile`: cam2d_pock_zz.c added to the two dependency lines.
- `tools/tests/fileop_all_zz.sh`: fileop_all.sh pointed at this tree's cam_cli (test helper only).
- Ghidra dumps used (this tree, not decomp/): `decomp_zz_core.c` (find_path_around_island, next_p_index,
  FUN_1805e35d0, get_ep, FUN_1805e5310), `decomp_zz_core2.c` (FUN_1805e1cc0, l_ints, la_ints, rotate_pock_bounds),
  `decomp_zz_core3.c` (t_on_arc, t_on_line, MCCEPtInBnd = jump into mcce.dll), `decomp_zz_core4.c`
  (bnci_wr_pock_marker), `decomp_zz_sys.c` (systypes pt_on_arc, l_hv_ints). Lost arguments: objdump of
  ~/mastercam-re/files/mcmill.dll ("dis ADDR" in the code).

### Ported steps (decomp/mcmill_all.c unless noted)
| step | source |
|---|---|
| rough boundary = tool-centre bounds offset by finish passes x spacing; per pocket | FUN_1801ef830 l.340234 (dVar19); pockets of rb = each outer + its islands (extract_pocket role) |
| roughing angle = record 0x1250 = pock_prm+0x28, **radians** (cos/sin applied directly; process_pocket l.323238 copies it unconverted to rough_angle; pock_prm points into cur_op +0x1228, set_pock_prm decomp/mccore_pock.c l.1411) | the dialog sheet's "deg/rad unverified" is answered: radians. NB the correlated engine (cam2d.c gen_pocket_zigzag) treats pocket_angle as degrees - harmless while every stored op has 0 |
| rotate bounds by -angle, output rotated back (rot_rpt) | FUN_1801f3700 l.343263, rotate_pock_bounds, FUN_1801e32a0 l.332872 |
| scan lines: y = ymin+2tol + i*range/(n-1), n = max(2, floor(range/step)+2), range = ymax-ymin-4tol of the first bound; line (0,y)-(100,y) | FUN_1801f23e0 l.342243 |
| intersections: retries shifted by ((k+1)/2)*0.1 nc_tol until even (else no segment); horizontal line filters entities by y-range +- nc_tol; lines t_ent in (-2e-7,1.0000002); arcs both la_ints points if pt_on_arc(nc_tol); sorted insert by t, same-bound duplicate within tol dropped | pock_bound_line_inters / bound_line_inters / FUN_1805e33c0 (mccore_pock.c l.1102, 4094, 4229), FUN_1805e1cc0, l_ints, l_hv_ints, la_ints, pt_on_arc |
| piece classes I/O/B by samples 1/2, 1/4..., 1/16 (pt_in_machinable_area tol,tol), B->I, merge equal, drop leading O | FUN_1801f0200 l.340493, FUN_1801f03c0 l.340593, pt_in_machinable_area / pt_in_ctour (mccore_pock.c l.1334, 2893) |
| record links to the previous line by unique x-overlap (-1 on conflict) | FUN_1801f23e0 second half |
| regions: chains along links with the same (boundA, boundB) as the first record; +1 flag | FUN_1801f1fe0 l.341981 |
| zigzag region orientation (vertical lines through 3 points, below/above nc_tol), reversal | FUN_1801f3b10 l.343399, FUN_1801f3150 l.342964, FUN_1801f3450 l.343121 (dis 1801f1bbc: ends not swapped) |
| zigzag linking, burial off: cost classes 0/1/2 x (1,2,3), first region lowest y, 4 candidate ends, side alternation, along-bound link with direction (side, ccw flag +3, island flag +2, reversed region: dis 1801f1c54-1801f1df9), straight cut if FUN_1801e0fd0 | FUN_1801f15a0 l.341514, FUN_1801e0fd0 l.331634 |
| inter-region / non-straight link: crossings strictly inside, pieces straight if midpoint strictly inside (FUN_1801f0c80) else around the bound by find_path_around_island (shorter way; same entity -> direction 0) + FUN_1801f29c0 | FUN_1801f2cc0 l.342711 (dir arg dis 1801f30b6), find_path_around_island (mccore 0x1805e39c0), FUN_1801f29c0 l.342510, FUN_1801e1cb0 / FUN_1801e2270 (min_arc_length -> line; < tol skipped) |
| one way: climb (record 0x123a != 0) swaps every record's ends (dis 1801f0e00); region choice (first: lowest y; else nearest, preferring regions adjacent (line +-1) to a cut one); reversal swaps ends too (dis 1801f10c5); per record: back to the previous start, along the bound (direction from the first link of the region, dis 1801f11aa), cut A->B; into later regions: via point FUN_1801f07b0 then FUN_1801f2cc0 | FUN_1801f0d60 l.341115, FUN_1801f07b0 l.340818 |
| "back to start" / "to via point" = FUN_1801f35c0: pock marker 3 (NCI -95) + a line; wr_pock_toolpath replaces that line by retract_move + plunge_move | FUN_1801f35c0 l.343236, bnci_wr_pock_marker (decomp_zz_core4.c), wr_pock_toolpath l.315766 / l.316476; emitted like gen_pocket_v2's re-entry (pk_retract, feed plane, plunge feed) |
| entry: helix at the path start / plunge, per depth; pockets skipped by 0x12d1 | as gen_pocket_v2 (FUN_1801bfcb0 -> FUN_1801bc830, FUN_1801c1d80) |
| depths: path computed once, cut at every depth | as gen_pocket_v2 (wr_pock_depth_cuts) |

### NOT DECODED (refused with an error)
- A link between two different bounds (FUN_1801f29c0 -> FUN_1801e0f80 -> FUN_1801e23c0 pock_move_avoid_islands, 428
  lines): "link between two bounds ... NOT DECODED". Not hit in any test below.
- ~~Zigzag with Minimize tool burial 0x1259 = 1~~: ported in the medax batch (section "Medial axis batch" at the end).
- Inherited from gen_pocket_v2: sort_pock_bounds order, user-defined entry points (point chains), ramp entry,
  product bit 0x10 / pock_op_code 0xe paths (not applicable to Mill pockets: op code 4).

### Ours (substitutes, labelled in the code)
- MCCEPtInBnd (mcce.dll): "on" = gl_closest <= tol, inside = even-odd on the loop sampled within tol/2.
- constr_arc_new: arc about the given centre from the start, valid when the end is within nc_tol of that circle.
- Entity boxes of build_pock_bound_info: exact line / arc extents.
- gls_offset (existing substitute for MCCECalcOffset): its loop start points matter for One Way on a full-circle
  bound: a single-entity first link gives direction 0, decided per link by t on the entity, so a link that crosses the
  entity's start goes the long way round (seen on BLOCK op 3 / mc-test01 op 2 patched to One Way). This is the decoded
  behaviour, but where Mastercam's offset loop starts is not known.
- PK_ZIGZAG_DECODED env switch (testing only).

### Oracle / tests
Stored references: the corpus scan (pockets.tsv) has NO method 0 or 6 pocket with stored moves (2017/X9 ops are methods
1, 2, 5; 2018 layouts ignored); Mill.mcam ops 5 / 9 are method 0 but roughing off (finish only). The only reference is
A001 op 13 (zigzag, angle 0, burial 0):

| A001 op 13 | ref->ours p50 / p95 / max | ours->ref p50 / p95 / max |
|---|---|---|
| correlated gen_pocket_zigzag (default) | 0.000 / 0.031 / 0.057 | 0.000 / 0.027 / 0.050 |
| decoded (PK_ZIGZAG_DECODED=1) | 0.000 / 0.028 / 0.057 | 0.000 / 0.022 / 0.050 |

The decoded roughing reproduces the stored NCI's lines exactly (first pass -5.481,-5.799 -> 15.804; 10 lines 1.3863
apart; links along the wall); the max is the shared helix / finish part. Decision for the user: switching method 0 to
the decoded engine (drop the env gate).

One Way sanity (no reference exists): A001 op 13 patched to method 6 (climb / conventional, angle 0 / 30 deg), BLOCK op 3,
CHGREC op 2, engraved coaster op 1, Mill ops 5 / 9 (rough on), mc-test01 op 2, and an island pocket built on A001
geometry (chains 31 + 17,18,23,26,30, angle 0 / 30 deg), same set for decoded zigzag: no gouge (tool centre never
closer than R + wall stock to the chains), no rapid below the top, nothing left uncut beyond corners / the stock band.
Patched copies were made in the scratchpad only (patch.py writes record bytes in a copy).

Regression: fileop_cmp 0/189 differ (this tree's cam_cli, base ~/mastercam-re/regress/base). oracle_refs.sh --type pocket
identical to the main tree: A001 13 0.031/0.027, Mill 5 0.008/0.008, Mill 9 0.007/0.007, mc-test01 2 0.005/0.005 (p95).
Debug: ZZ_DEBUG=1 (bounds, regions, links), =2 also every record.

---

## Batch 6 "hsptype": High Speed extras / trochoidal modes / pocket types

Changed files (tree ~/mastercam-re/work/tree_b6_hsptype):
- `viewer/cam2d_pock_hsx.c` (NEW, included from cam2d_pock.c before gen_pocket_v2)
- `viewer/cam2d_pock.c`: PkXs type + forward decls, pk_hs_loops (extras + owner bookkeeping, new params `dia`, `xo`),
  PkPocket.xs, gen_pocket_v2 (stepover R..D refusal removed, pocket-type refusal added, cut hook, frees, comment)
- `viewer/pgeo.c` / `pgeo.h`: `gls_merge` (gls_offset refactored into gls_offset_mode; gls_offset unchanged)
- `viewer/Makefile`: cam2d_pock_hsx.c in the cam2d.o / cam_cli dependency lists

### 1. Stepover between R and D (PORTED)
All lines are from decomp/mcmill_all.c.
| step | source |
|---|---|
| condition: stepover *used* (local_190, after FUN_1801d1850 halving) strictly in (D/2, D), next loops non-empty | FUN_1801d00d0 l.320598-320603 |
| current non-island loops offset inward D/2 -> outer; next loops offset outward D/2 -> islands | FUN_1801d1740 l.321132 (side 'L'/'R' by +3 vs param_5, +2 = (param_5 == 0)) |
| delete loops that cross no loop of the other kind (sequential) | FUN_1801d2280 l.321620 |
| merge -> uncut regions | merge_pock_bounds; **ours**: `gls_merge` = union(outer) - union(islands), rebuilt exactly as gls_offset at 0 (substitute, mccore internals not decoded) |
| every loop +2 = 0; bbox larger side <= D/2 -> +2 = 1 ("small") | l.320627-320683 |
| smoothing when 0x1400 > nc_tol | l.320686 FUN_1801d2b80 (pk_smooth) |
| owner: in->out = current loop; out->in = nearest next loop (FUN_1801cea40 l.319397 entity-start distance, single loop owns all) | FUN_1801d28b0 l.321845 |
| out->in carry (local_1c0) / waiting (local_198) lists, re-own after merge, pending pick | l.320735-320808, l.320670-320681 |
| written after the owner, read back as a group (cut order reversal keeps groups) | l.320718-320732, FUN_1801d19d0 l.321231 |
| cutting: attachments FUN_1801cede0 (in->out) / FUN_1801ceb50 (out->in) l.319440/319570; per entity min-t walk, t > 1-tol -> 1; partial entity moves FUN_1801e2270 / FUN_1801e1cb0 | FUN_1801cf8b0 l.319995 |
| small extra: circle through cur and P (two half arcs about the midpoint, cw iff cross < 0) | FUN_1801ce710 l.319237 |
| other extra: S link out (dir in->out cross>=0, out->in cross<0), extra loop from its attach entity all round, S link back (!dir) | FUN_1801d26e0 l.321778 (FUN_1801d13a0 + FUN_1801d2c60, else FUN_1801e0f80) |

Read-as-decoded oddities kept: out->in, when the merged next loop splits in pieces, carried extras of the *picked* piece
are unlinked and not relinked (l.320797-320808) -> dropped; several attachments at t ~ 1 on one entity: only the first
is cut (loop ends at t == 1.0). The recursive FUN_1801cf8b0's link from the previous point is zero-length here (nothing
emitted). FUN_1801e0f80's path around islands remains NOT DECODED (same fallback as gen_pocket_v2: retract / re-plunge).

Oracle: no stored method-8 pocket has a stepover in 50..100 % (pockets.tsv has no method 8 at all; the only High Speed ref, mc-test01 op 2, is at exactly 50 % = D/2, outside the strict range; Mill 5 / 9 are zigzag), so no
stored NCI exists. Tests (cam_cli --verify, chains only so pk_p defaults = method 8):
| part / chains, D 0.5 | step % | extras (small) | gouge | uncut | rapids below top |
|---|---|---|---|---|---|
| mc-test01 chain 0 | 60 / 75 | 0 (geometry: cusp only when s > 0.854 D at 90 deg) | 0 | 3.79 % | 0 |
| mc-test01 chain 0 | 90 | 12 (12) | 0 | 3.79 % | 0 |
| PROJECTLETTERS chain 1 | 70 / 90 | 8 (4) / 16 (10) | 0 | 2.53 / 2.55 % | 0 |
| PROJECTLETTERS chain 0 | 70 / 90 | 16 (13) / 32 (15) | 0 | 2.08 % | 0 |
| PROJECTLETTERS chain 75 | 90 | 40 (40) | 0 | 1.64 % | 0 |
| out->in (temporary override, reverted) chains 1 / 0 | 90 | 16 / 32 | 0 | 2.54 / 2.07 % | - |
No p50/p95 table: no oracle. Pre-existing (not mine, main cam_cli the same): `cam_cli mc-test01.mcam pocket 0,1` (and
any island) on the chains path does not finish in 60 s.

### 2. Trochoidal channel modes 1 / 2 (record 0x1408): still REFUSED (NOT DECODED)
Decode map for whoever continues: rough boundary widening FUN_1801ede50 l.339390 confirmed:
`dVar18 += (2r - (2r - spacing/2pi) pi) / 2`, r = pock_prm+0x1f0 (0x1418 loop radius), spacing +0x1e8 (0x1410), only
method 8 and mode 1/2. FUN_1801d00d0: mode 2 disables smoothing (l.320410); mode 1 flags the start loops +0x2d and
builds a pbmap (init_pbmap with pock_prm+0x50, bbox +- D/2, l.320660); writer FUN_1801cf080 (same attachment walk) calls
per entity segment FUN_1801e41e0 (mode 2, POCKNCI.cpp troc_gt_move, 461 lines, globals roll_rad / loop_rad / l_rad /
s_rad not yet traced to the record) or FUN_1801e3b20 (mode 1, pbmap material test, 213 lines + FUN_1801e34c0); links
FUN_1801d2f40 / FUN_1801e55a0 (-> FUN_1801e23c0, FUN_1801dfb90 path finding). ~2000 decomp lines, no stored oracle.

### 3. Pocket types (record 0x13fa): non-Standard now REFUSED (NOT DECODED)
Before: gen_pocket_v2 ignored 0x13fa (a Facing / Remachining / Open op silently cut a Standard pocket). Now
`pocket type <name>: NOT DECODED`. Gate check (rules item 3): the only user pocket (A001 op 13) and the defaults op
(MM=0 Inch=0, formats/opdlg_fields.tsv 2101) hold 0 -> honoured directly. Not touched: cam2d.c gen_pocket dispatch
sends zigzag (method 0) to gen_pocket_zigzag, which still ignores 0x13fa (other agent's code). UI: 0x13fa is not in
cui_opdlg.c pf_honoured POCK (not my file).
Decode pointers: process_pocket l.323151 (type 1 skips the finish offset flag; type 3 -> FUN_1801ec3c0); FUN_1801d4620 /
FUN_1801d4850 l.322990 (type 1 outer offset = pock_prm+0x160 (rec 0x1388) - R + finish n x spacing, outward; islands by
param_4); GeneratePocketToolpath l.313461: type 2 -> per depth FUN_1801c7970 + FUN_1801c68e0, finish_with_rough; types
3/4 skip the closed-chain check; open pocket via FUN_1801e5840/FUN_1801e57f0 -> process_open_pocket l.334844.
Remachining needs the rest material of previous ops (FUN_1801ec3c0): not decodable from the pocket code alone -> refused.

### Regression
- fileop: 0/189 differ (own cam_cli, baseline ~/mastercam-re/regress/base)
- oracle_refs --type pocket: identical to the main cam_cli (A001 op 13 0.031/0.027, Mill 5 0.008, Mill 9 0.007, mc-test01 op 2 0.005)

---

## Batch 6 "finish": Pocket Finishing page 3004 in gen_pocket_v2 (2026-10-23)

Tree: ~/mastercam-re/work/tree_b6_finish. Changed files (diff against the main tree):
- `viewer/cam2d_pock_fin.c` (NEW, included from cam2d_pock.c): settings / refusals, pass bounds, lead geometry + lead
  checks, section comparator.
- `viewer/cam2d_pock.c`: `#include "cam2d_pock_fin.c"`; PkPocket gained finish caches; the roughing per depth moved
  unchanged into `pk_rough_depth`; new `pk_fin_inner`, `pk_fin_section`, `pk_free`; gen_pocket_v2 builds the sections,
  sorts them (`pk_sec_cmp`) and emits them. The zigzag engine (cam2d.c gen_pocket_zigzag) is untouched.
- `viewer/cam2d.h`: `CamParams.lead_xflags` (0x928 / 0x929 / 0x970 / 0x971 / 0x992 / 0x993).
- `viewer/cam_fileop.c`: LEADS reads lead_xflags; pocket block reads `ct_ccopt = B(0x5d4)`; patches for both.
- `viewer/cam_cli.c`: `--rec OFF=b:V|s:V|d:V` (ours, test tooling: a record patch as the dialog writes it).
- `viewer/Makefile`: cam2d_pock_fin.c in the dependency lists.
- Ghidra dumps (mccore, not in the main decomp/): `decomp_finish.c` (reverse_pock_bound @1805dda60),
  `decomp_finish2.c` (bnci_wr_pock_marker @1800958f0).

All line numbers are decomp/mcmill_all.c.

### Ported (decoded)

| field / step | source | port |
|---|---|---|
| pass bounds: computer = tool-centre bounds offset (n-j-1) x spacing | FUN_1801cc170 l.318050 (dVar20), offset_pock_bounds | `pk_fin_bounds` (was there) |
| 0x13a8 = 1 Control: base = chain bounds, distance + wall stock (R not in it) | l.317960-317968 (local_168 = param_1, uVar21/22 = op +0x278) | `pk_fin_bounds`, `q->cb` |
| 0x5d4 Optimize (control only, D > tol): out by D x 0.505, merge, back with angle pi; nothing left -> pass skipped | l.318068-318086 | `pk_fin_bounds` |
| High Speed corner smoothing of the finish bounds (rough on, method 8, type != 4 or +0x1c8 = 0, 0x1400 > nc_tol) | l.318058-318062 FUN_1801d2b80 | `pk_smooth` on every pass; GATED like 0x126b (stored ops / 0x1400 edited): see question 1 |
| start of the first bound without 0x1269: the bound's own start (offset_pock_bounds carries the chain start); FUN_1801d5870 (l.323745) and FUN_1801cc170 (l.318202) reverse keeping entity 0, so the start is the chain's first entity start when the final direction is the chain's, else its end; then FUN_1801bf950 splits that entity at its midpoint only when leads are on - without leads the pass starts at the entity's end point, not its midpoint | l.323721-323752, l.318202, Ghidra reverse_pock_bound, FUN_1801bdc00 early return | `pk_fin_section` (fixes CHGREC: 0.35 -> 0) |
| 0x13a8 tags: first entity of the pass (incl. leads) 41 / 42 (Reverse wear swapped), last 140; Wear / Reverse wear keep the computer path | FUN_1801cd200 l.318636-318656; cc_in_control / cc_in_control_only | `pk_fin_section` (Move.cc); posted only with MCAM_CUTTER_COMP=1 (nc_post.c unchanged) |
| roughing with control compensation | GeneratePocketToolpath l.313553 dVar3 = D/2 + stock always | unchanged: roughing is always computer |
| direction: climb flag = 0x1436 ? 0x143a : 0x123a; != 0 -> side 'L', outer CCW / islands CW, else reversed | l.317946-317951, l.318195-318203; [3] = pock_ccw (mccore build_pock_bound_info) | `pk_fin_section` gl_reverse; leads mirrored ('R') |
| reverse_pock_bound(b, !0x1269): keep entity 0 / plain reversal | Ghidra mccore reverse_pock_bound | both keep the start point here |
| 0x1269 closest: bound + entity nearest prev_nc_pt, entity evaluated at t 0.5 with leads on (op +0x888) else 0; reorder_pock_bound; each bound once | FUN_1801cbd20 l.317673, reorder_pock_bound (mccore_pock2.c l.214), l.318171-318190 | `pk_fin_section` |
| 0x1268 Finish outer boundary | l.318185: the outer bound is skipped only when 0x1268 = 0 AND param_2 +0x50 = 0; +0x50 = the bound it was offset from, set for every tool-centre bound by FUN_1801d4850 (l.323115) | NO EFFECT on closed pockets (always finished) - nothing to port |
| 0x1230 only at final depth | pock_finish_all = 0x1230 or 0x1436 (l.313514); wr_pock_toolpath skips other finish depths unless finish_all | verified, unchanged |
| leads: split of the first entity at its midpoint | FUN_1801bf950 | `pk_fin_leads` (see "ours" for the start) |
| leads: start entity rule: no arc of radius <= a lead radius turning to the lead side (ccw ^ island ^ cw arc); none -> no leads | FUN_1801bdc00 l.308810-308990 | `pk_fin_leads` |
| lead arc / line geometry (side L/R, tangent / perpendicular 0x8f9 / 0x941, line from the arc) | FUN_1801c2b30 | `pk_lead_ent` |
| lead flags: arc = sweep > 1e-6 and r > nc_tol, line = len > nc_tol | FUN_1801bdc00 l.308620-308700 | `pk_fin_init` |
| overlap 0x988 (exit on, > nc_tol): along the pass from its start, whole entities while <= rest + tol, the next one cut; exit leads from its end | FUN_1801bd7c0, FUN_1801bfa70 | `pk_fin_leads` |
| lead check only when entry count > 0x992 and exit count > 0x993 | l.309323-309333 | `pk_fin_leads` |
| lead check: inner bounds = each pass bound offset by the spacing (no merge, +0x50 parent); lead / inner intersections sorted from the pass outwards (max 8); trim at the first one on another bound's offset, or whose section midpoint (CalcSectMidPtViaM) is outside the inner bounds, or (first one) whose piece towards the pass is outside the pass bounds | FUN_1801c4860, FUN_1801c3d70, FUN_1801c37b0, FUN_1801c1b60, FUN_1801bbc70, mccore offset_pock_bounds l.666 | `pk_lead_check`, `pk_fin_inner` |
| per lead entity: < nc_tol dropped; midpoint and t 0.001 (entry) / 0.999 (exit) in the machinable area of the pass bounds; overlap: length test only | FUN_1801c3d70; mccore pt_in_machinable_area l.1334 (order-dependent) | `pk_lead_check`, `pk_machinable` |
| a kept lead crossing the first pass bound away from its ends drops its side; a dropped entry entity drops those before it, exit those after | FUN_1801c3d70 tail; intersect_pock_bound (single pair) | `pk_lead_check` |
| section order: 0x126b, 0x13f8 By depth, thin wall | wr_pock_toolpath qsort FUN_1801c7a90 / comparator FUN_1801c6520 (12 keys + write position), section fields FUN_1801c74a0 + bnci_wr_pock_marker | `pk_sec_cmp` |
| 0x126b = 0 (checked): every roughing section before every finish section; = 1: per pocket and depth roughing then finishing | comparator key 1 vs key 6 | honoured for stored ops and after a dialog edit only (Mill_MM defaults hold 1, the old order was the 0 one) |
| thin wall 0x1436 / 0x1438 nZ: the finish sections of each rough depth again for t = 1 .. nZ-1, raised (nZ - t) x step / nZ (step: pock_rgh_step; depth cuts off: top - depth, the call at 0x1801c5ba4 xmm6 - xmm7); the originals t = nZ; copies only at rough depths (wr_pock_depth_cuts); sorted thin index first; finishing at every depth | FUN_1801cad80 l.316904, wr_pock_depth_cuts, GeneratePocketToolpath l.313729 / l.313746 | sections in gen_pocket_v2 |

### Refused (NOT DECODED / ours)

- 0x126a Keep tool down = 1: the link is written by wr_pock_toolpath from keep-tool-down sections (FUN_1801cd200 l.318461,
  pock_ktd_bnd markers) - not decoded. (FUN_1801e3150 = retract does something only for facing, so 0 = retract, as today.)
- 0x1420 spring passes > 0: FUN_1801ca670 decoded (copies of the last pass at the last depth, entry only on the first copy,
  exit only on the last, sorted after everything by the +0x44 / +0x46 keys) but the link between its sections is
  wr_pock_toolpath's - refused.
- Lead Use entry / exit point 0x929 / 0x971 (FUN_1801c38a0 / FUN_1801c2ab0); lead ramp / helix heights 0x908 / 0x920 /
  0x950 / 0x968 (the switch at the end of FUN_1801bdc00 is decoded, the moves of the raised plunge are not);
  0x928 / 0x970 with depth cuts (they switch GeneratePocketToolpath to per-depth processing, FUN_1801bfd20).
- FUN_1801cc070: with several bounds an island longer than the longest outer bound + is_ctour_nested_or_intersecting skip a
  pass; the test is not decoded -> refused when an island is longer.
- Control on a chain pocket the tool splits into sub-pockets (machine_pocket would finish the chain bounds once per
  sub-pocket) - refused.
- Closest (0x1269) when the finish is the op's first motion: prev_nc_pt's initial value not decoded - refused.
- ours (safety, as the contour of batch 5): Control (1) needs a kept exit lead line (the G40 move).

### "ours" / stand-ins kept

- The start of the first bound: the loop point nearest the chain start (stands in for offset_pock_bounds' start
  propagation). Every other bound / pass without 0x1269: the point nearest the end of the cut before (the measured model,
  no midpoint split).
- The crossing check of the leads uses the first loop of the pass list; our list order (gls_offset) is not Mastercam's
  merge order (outer first in practice).
- The pocket / sub-pocket keys of the section order use our pocket index k (tool-centre outer loops in gls_offset order).
- Lead sweep 0 for ops without a record: a quarter (as before).
- gls_offset stays the substitute for ccomp_pock / merge.

### Feed / spindle overrides (not implemented, for the docs)

FUN_1801cd200 l.318540-318555: with finishing on (pock_prm +0x11) and param_9 = 0, 0x1434 (+0x20c) sets
init_feedrate = 0x1428 (+0x200) and 0x1435 (+0x20d) spindle_speed = 0x1430 (+0x208) for the finish pass; both are restored
after it (l.318705-318712). The lead feed overrides (+0x218 / +0x220 entry, +0x228 / +0x230 exit) are applied per lead
entity in the same loop (l.318598-318680). Spring passes with leads on and an overlap warn 0x96e; with tapered walls 0x96b
(GeneratePocketToolpath l.313752-313768).

### Stored values

A001 op 13 (zigzag: not this engine), Mill.mcam 5 / 9, mc-test01 2, Mill_MM / Mill_Inch defaults: outer 1, closest 0,
ktd 0, spring 0, thin 0, cc 0, optimize 1, overrides 0, lead Use point / first-last / plunge-retract flags 0, ramp heights 0.
0x126b: Mill 5 / 9, mc-test01, Mill_Inch 0; A001, Mill_MM 1 -> gated (see above). 0x143a only acts with thin wall.
Overlap 0x988: Mill 5 / 9 0.0075 (now cut: improvement), others 0.

### Oracle (p50 / p95 / max, part units; ref->ours | ours->ref)

| op | before | after |
|---|---|---|
| mc-test01 op 2 (HS, finish every depth, leads) | 0.003 0.005 0.005 \| 0.003 0.005 0.005 | identical moves (bit-identical) |
| Mill.mcam op 5 (finish only, overlap 0.0075) | 0.000 0.008 0.011 \| 0.000 0.008 0.011 | 0.000 0.004 0.005 \| 0.000 0.004 0.005 (overlap; the rest = arc chords) |
| Mill.mcam op 9 | 0.000 0.007 0.011 \| 0.000 0.007 0.011 | 0.000 0.004 0.005 \| 0.000 0.004 0.005 |
| A001 op 13 (zigzag, other engine) | 0.000 0.031 0.057 \| 0.000 0.027 0.050 | unchanged |
| corpus (BLOCK, CHGREC, PROJECTLETTERS, ROUGHPROJNCI, coaster; 2017 + X9) | FAILED (roughing method / chain limit) | unchanged FAILED (roughing is not this engine) |

Finish-only probe (`cam_cli PART fileop ID --rec 1238=b:0`, our finish moves vs Mastercam's whole toolpath, ours->ref):
CHGREC_EX1_REV-A.mcam / .mcx-9 op 2 (leads off): before 0.000 / 0.350 / 0.350 (started at the midpoint), after
0 / 0 / 0. BLOCK.mcam op 3 (Wear compensation, keep tool down patched to 0 for the probe): 0 / 0 / 0, and the G41 / 140
tags land on the same moves as Mastercam's NCI cc field (1.3424,-1.4424 / 1.4826,-1.8548).

fileop (own cam_cli): 0/189 differ. New ops from the defaults (cam_cli PART pocket ...: A001 chain 0 D10 metric,
mc-test01 chain 0 and Mill chain 82 inch): bit-identical to the main tree's cam_cli. (A pocket with islands, A001 chains
0,17,18 D10 / Mill 72,73, runs > 2 min in both trees - the High Speed roughing, not this batch.)

Synthetic checks (mc-test01 op 2 with --rec): thin wall (0x1436 1, nZ 2) adds the raised copies at the rough depths,
thin index first; 0x126b = 1 interleaves roughing / finishing per depth; Control: the pass on the chain, G41 on the lead
line, 140 on the exit line; Reverse wear + Conventional: computer path mirrored, G41.

### Questions for the user / coordinator

1. Finish corner smoothing (0x1400 on the finish passes, decoded l.318060) is gated: a new metric op (Mill_MM 2 mm)
   would get its finish corners rounded where today's are sharp. Ungate (Mastercam's behaviour) or keep?
2. 0x126b gated as asked (Mill_MM holds 1 = finish per depth after its roughing).

## Decisions batch dec1 (2026-10-23, formats/user_decisions.md B6-1..B6-5)
- B6-1: Parallel Spiral / Clean Corners with 0x1259 Minimize tool burial is no longer refused: every link takes FUN_1801e9ee0's own
  fallback FUN_1801e0f80 (what PK_PS_MEDIAL_FALLBACK=1 did; the switch is removed) and the op carries CamOp.warning. The medial axis
  (CalcMedAx / FUN_1801d9f60) stays NOT DECODED. Zigzag with burial is a different branch (FUN_1801f0540 region choice +
  find_path_around_island's burial branch, no medial axis): still refused, NOT DECODED.
- B6-2: method 0 Zigzag goes to the decoded engine by default; `PK_ZIGZAG_DECODED=0` (testing) keeps the correlated one. A001 op 13
  regenerates p95 0.028 / ours->ref 0.022 vs its NCI (was 0.031 / 0.027).
- B6-3 / B6-4 / B6-5: finish corner smoothing 0x1400, 0x126b finish after roughing all pockets and the entry feed 0x1308 are honoured
  for every op (batch 6 gated them to stored ops / after an edit). The user's A001 op 13 holds 0x1308 = 0, method 0 (no smoothing),
  0x126b already honoured: no change from these three.

## Medial axis batch (2026-10-24, formats/user_decisions.md Q3; formats/opdlg_port_audit.md "Medial axis")
### Zigzag with Minimize tool burial (ported; decode decomp/medax/zzburial/zz_burial_spec.txt)
- No medial axis for method 0: machine_pocket (l.322515-322530) builds it for methods 1-3 only with product bit 0x10 clear;
  FUN_1801f2cc0's medial-axis link (FUN_1801dae10) also needs that bit.
- Region choice in FUN_1801f15a0 (l.341682-341840, disassembly 1801f1787-1801f1b62) with burial on: FUN_1801f0540 (l.340654)
  classes a region 0 when it lies next to an already cut one, else 2; the "below the last point" / "same bound" class tests are
  skipped; a region / end whose class is worse than the best so far is skipped; the link cost adds 10 x the record's uncovered
  length (10.0 read from the DLL).
- FUN_1801f3940 (l.343328): the bounds' lowest / highest point flags (+0x44 / +0x45), set before every move of a record (reset per
  pocket, l.341658); the points from build_pock_bound_info (mccore_pock.c l.2079-2096) + FUN_1805e41e0 (objdump).
- find_path_around_island's burial branch + FUN_1805e35d0 (decomp/pocket_b6/zigzag_decomp_zz_core.c): the way round an island is
  forced when the link's ends lie on different sides of its lowest / highest points. Zigzag passes burial to FUN_1801f2cc0
  (dis 1801f19b8 / 1801f1e82); One Way passes 0 (1801f1429, 1801f11c7) - unchanged.
- Code: viewer/cam2d_pock_zz.c (zz_extreme, zz_bound_extremes, zz_f3940, zz_e35d0, the burial branch of the island path, the
  region choice); the refusal in cam2d_pock.c removed. Ours: an island's winding from its area sign; FUN_1805e35d0's two side
  markers start at -1 (uninitialised in Mastercam, never observable); `ZZ_DEBUG` trace.
- Tests: A001 op 13 (burial 0) byte-identical; with `--rec 0x1259=b:1` identical too (one region, no island: nothing to
  change, as decoded); PROJECTLETTERS chain sets with islands change (forced direction 1 / 10 / 365 times); 18 burial runs
  (3 chain sets x angle 0 / 30 / 90 x stepover 50 / 30 %) 0.00 mm2 gouge, no rapid below the top. No corpus zigzag op with
  burial on and stored NCI exists (no oracle).

### Parallel Spiral links with Minimize tool burial (ported; decode decomp/medax/pockmdl/pockmdl_spec.txt)
- **Build** (machine_pocket l.322547, per pocket on the tool-centre bounds, methods 1-3, product bits 0): FUN_1801d9f60(pocket,
  'L', nc_tol, medTol = max(nc_tol x 50, D x 0.1), nc_tol, &medial_axis) = find_medial_axis FUN_1801da3c0 then FUN_1801d9f20
  (polyline simplification at nc_tol). The bounds are oriented (outer ccw, islands cw), full circles split in two, a zero-radius
  arc inserted at each reflex corner (insert_zero_radius_arcs); every entity is lifted to a 45 deg "roof" (line -> plane, arc ->
  cone, z = -distance); nodes = points equidistant from 3 entities, found by rays along the bisector of two entities to the
  first hit on a third roof, refined by Newton on the 3 surfaces; edges join nodes sharing an entity pair (straight, or
  polylines of (x, y, r)); at most 4 node passes. Ghidra had lost the stack / float arguments: every parameter list was
  recovered from the disassembly (pockmdl_redecomp_sig.c).
- **Walk** FUN_1801dae10 (8 tries, tolerance doubled) -> FUN_1801daeb0: copy the axis (CMdlPocks), lines -> 2-point polylines,
  split where edges cross a bound (flagged), drop pieces outside the machinable area; the edges nearest 'from' and 'to'; a
  depth-first search over unflagged edges whose ends meet within tol, through a sorted end-point table (comparator
  LAB_1801d7d10, equal within 1e-12; MSVC CRT qsort / bsearch ported - [inferred] same CRT); moves: from -> its projection on the
  first edge (FUN_1801e0f80), feed along the edges, -> the projection of 'to' -> to (FUN_1801e0f80); both nearest the same
  edge: one FUN_1801e0f80(from, to). All 8 tries failing: FUN_1801e9ee0's fallback FUN_1801e0f80 (the dec1 behaviour).
- Code viewer/cam2d_pock_medax.c (~1250 lines; md_build, the walk), wired in cam2d_pock.c (the axis built once per pocket) and
  cam2d_pock_pspiral.c; the former fallback link is cam2d_pock.c pk_e0f80 (unchanged). The op warning is gone.
- Ours (mccore helpers known by name only, standard meaning): proj_pt_ar, ints_on_gt (1e-6), CalcIntersectRect, det3, dist_pt_l,
  pock_ccw (signed area), exact entity boxes for build_pock_bound_info, eval_gt_* / length_gt (pgeo), zero-length feed moves
  skipped, arc start angles normalised in the private copy.
- NOT DECODED: FUN_1801e0f80's path around islands (find_path_around_island in pock_move_avoid_islands - the retract stand-in
  stays); moves written by a failed try stay in the output (literal reading, unverified). Methods 1 (Constant Overlap, also uses
  this walk via FUN_1801e9020 / FUN_1801e9250) and 4 stay refused (pbmap / Morph Spiral). FUN_1801ddf20 (rotate the axis by
  -angle, methods 0 / 6) is unreachable with product bits 0.
- Oracle: CHGREC_EX1_REV-A op 2 (.mcam and .mcx-9) 0.000 0.004 0.004 | 0.000 0.004 0.011, byte-identical to the fallback: its one
  link (G1 (1.0625, 0.91625) -> (1.0625, 0.8225)) has both ends nearest the same axis edge, where Mastercam's walk itself takes
  the straight link. PROJECTLETTERS op 2 / ROUGHPROJNCI op 1 (the other corpus method 2 / 3 + burial ops) cannot be rebuilt
  (missing entity ids), BLOCK op 3 is method 1. So no corpus op exercises a multi-edge walk; checked on a 100 x 50 rectangle with
  a circle island (12 axis edges incl. the line-circle curves; test links walked several edges round the island).


## Constant Overlap / Facing (2026-11-08)

Corpus scan (all 1381 parts, `op opcode 3`): 19 parsed Pocket ops, Pocket type 0 Standard in all 19; methods 0 Zigzag 10 (A001 + door parts), 1 Constant Overlap 2 (BLOCK X9 + 2017, one op), 2 Parallel Spiral 6 (3 parts), 5 True Spiral 1. Pocket types 1-4 (Facing, Island facing, Remachining, Open pocket): 0 stored ops in the parsed corpus (RESTMATERIAL.mcam and 15 other parts do not parse: `unknown node type` / a broken record in `tools/mcam_walk.py`).

- Method 1 Constant Overlap Spiral: viewer/cam2d_pock_ovlp.c = FUN_1801e8650 (l.335846) on the mccore pbmap port (viewer/cam2d_remach.c); verified parts: pbmap_init_tool / pbmap_bnd_to_tp / filter_pbmap_tp / reverse_pbmap_bnds against the real mccore.dll (tools/tests/remach_oracle.sh TOOL / TP / FTP / REVB, 120 new cases). The whole op against BLOCK op 3's NCI: the 31 cut records of the first depth all lie on our path (0.00000 in) with the one exception that the NCI has no "ring-closing" record while pbmap_bnd_to_tp (checked in the DLL) adds one: dropped on that NCI evidence, flagged for the user. Left: BLOCK's entry helix centre (find_helix_center FUN_1801c04d0 picks (2.4825, -1.575) where pk_helix_center picks near the start, unverified), the op can't generate in the app because Keep tool down finishing (0x126a) is refused.
- Pocket type 1 Facing: pk_face_tc (FUN_1801d4850 l.323068-323100: outer bound grown by 0x1388 - R, islands by R + stock), entry FUN_1801bfd60 / FUN_1801bfde0 (approach distance 0x1390: outside the offset outer bound, closest point for spiral methods, else line search at 20 angles, islands clear by a capsule test), exit FUN_1801c0190 (exit distance 0x13a0). No stored op: checked against the decompile only (patched op). Refused: finish passes with Facing (FUN_1801cc170 on the chain bound NOT DECODED for this case), a roughing angle with Zigzag / One Way (entry search runs in the rotated frame), chained entry points.
- Types 2 Island facing, 3 Remachining, 4 Open pocket: see their sections below. GUI: the Pocket type combo (2101) selects Facing and the engine runs, but the child panels (3002 Facing overlap / approach / exit, 3001 Open, 3146 Remachining) are not wired: Facing uses the values already in the op record.

## Remachining (2026-11-09, decision N9-3; supersedes the "still refused" note for type 3 above)

Pocket type 3: `viewer/cam2d_pock_rem.c` = POCKRMCH.cpp FUN_1801ec3c0 (l.338353-338921, called by process_pocket l.323274) on the mccore pbmap port (`viewer/cam2d_remach.c`). All lines decomp/mcmill_all.c; arguments the decompile dropped come from the disassembly of 0x1801ec3c0 (objdump of mcmill.dll).

Parameters (HSTPocketRemachPage, dialog 3146, class FUN_1801827b0; DDX FUN_1801826b0, MapToDlg FUN_1801828b0, MapToOp FUN_1801829b0, l.268824-268966; pock_prm +X = record 0x1228 + X):

| control | record | pock_prm | meaning |
|---|---|---|---|
| radios 1953 / 1954 / 2060 | 0x13b2 s16 | +0x18a | rest source: 0 all previous ops, 1 the previous op, 2 roughing tool diameter |
| 1958 (static 1956) | 0x13b8 f64 | +0x190 | roughing tool diameter; enabled iff source 2 (FUN_180182dc0); MapToDlg shows 25 / 1 when stored < nc_tol, and MapToOp stores that |
| 1984 | 0x13c0 f64 | +0x198 | clearance % of D |
| 1716 | 0x13c8 f64 | +0x1a0 | clearance distance: MapToDlg always recomputes % x 0.01 x D (l.268928), MapToOp stores it: the app's dialog OK does the same (cui_opdlg.c pf_apply) |
| 1988 | 0x13d0 u8 | +0x1a8 | Apply entry/exit curves to rough passes |
| 1963 | 0x13d1 u8 | +0x1a9 | Machine complete finish passes |
| 1759 | 0x1280 u8 | +0x58 | Display stock (display only) |

Cell = +0x50 = D x 0.01 x 0x1270 (recompute_tool_dia_dependent_fields l.314920); the panel has no tolerance field. The method is forced to 2 (l.338502).

Flow, one call per chain pocket and (with depth cuts: FUN_1801c5250 l.313630-313636) once at the top depth, wr_pock_depth_cuts copying it down:
1. Y range of the chains +- (ymax - ymin) x 0.5714285714285714 (l.338524).
2. Cut map, source 0 / 1: FUN_1801ebc60(source, ymin, ymax, cell, 1, -1, -1, depth, 0.0, map) (disassembly 0x1801ec5dc-0x1801ec61c; l.337954): verify_2d_operation of the earlier ops; refused (warning 0x519, mcmillres 1305) by a prior Pocket with finish on (op +0x11d1) and finish compensation 1 (op +0x1340), a different test from the Contour's FUN_180053a70. Source 2 and the reach map: FUN_1801ec130 (l.338251): comp_pock_bnds(r + stock), pbmap_fill_pock_bounds, verify_pock_bounds(r). Unlike the Contour, the inside is filled.
3. pbmap_diff(reach, cut, cell); trace with R; copy; verify_pbmap_bounds(R + clearance); trace; filter(cell x 0.5); to bounds; sort_pock_bounds; bbox (l.338567-338615). The band local_128 (l.338594-338609) only reaches the Display stock copy, so it is not computed.
4. Per tool-centre pocket, passes k = 0, 1, ...: offset d_0 = 0; d_k = d_(k-1) + spacing (+0x38) for k <= nf, then + stepover (+0x18); after the nf finish passes roughing off or stepover < 1e-12 ends the loop. offset_pock_bounds(pocket, d); no bound left ends the loop. Direction: l.338702-338711. pock_trim_toolpath against the rest, remach_merge_adj_segs; the first pass without a real piece ends the loop (l.338723-338729): a pass that misses the rest stops every later pass.
5. Leads on finish pieces, and on rough pieces with +0x1a8: FUN_1801bdc00 for an open piece (no split, no overlap: param_4 = 0, disassembly 0x1801ecd02; lead check on: param_5 = 0x62(%rsp) = the pocket's closed flag, disassembly 0x1801ecb34 / 0x1801ecdf0; FUN_1801c4860 on the pocket).
6. add_remachine_bound(piece, k); find_remachine_group; the marker bnci_wr_pock_marker(pocket, finish = k < nf, pass nf - k - 1, depth index, sub-pocket local_232, group local_236) goes into the section order FUN_1801c6520 (`pk_sec_cmp`, now with the two extra keys: section +0x10 and +0x1a, read off FUN_1801c74a0 l.314756-314765).
7. Writer FUN_1801cd200: the first piece of a group (param_9 = 0, r15b at 0x1801ed01f): plain plunge at its start (FUN_1801bfcb0 -> _enter_pocket with param_3 = 0: no helix / ramp entry); a later piece: FUN_1801e0f80 from the end of the piece written before it to its start, inside the writer bounds (finish: tool-centre bounds, rough: offset by nf x spacing; disassembly 0x1801ed1c9-0x1801ed1d2). Either way the section's first record is the piece start.
8. Section start, wr_pock_toolpath l.315990-316049 (every piece has a marker, so every piece is a section; this branch is for type 3 only): tool down and the first record more than nc_tol from prev_nc_pt: retract_move at the last point, or clear_move when the pocket changes (retract_move again when clearance is on with clearance only at start / end, op +0x111 and +0x2b1). Then plunge_move to the first record: first = the op's first section, or the pocket changes (unless clearance only at start / end). So the feed link FUN_1801e0f80 writes inside a group does not survive: the tool retracts and plunges at its end, except when the two pieces meet within nc_tol (no retract, feed to the depth only). decomp/mccore_moves.c: retract_move l.206 (retract, else clearance, else feed plane), clear_move l.8 (clearance, else retract, else feed plane), plunge_move l.85 (first: rapid at clearance, or retract with clearance off; rapid to the point at the feed plane in one record, the plunge at op +0x318; tool down: feed to the point, feed to the depth). The end of the op: clear_move l.316554.
9. Pocket order: extract_pocket(list, -2) (decomp/mccore_pock.c l.140-146): the first bound in the list not yet taken gives the group, then its outer bound and its islands in list order (l.152-182).

Refused, op->error, NOT DECODED: chained entry point; Keep tool down 0x126a; Machine complete finish passes 0x13d1 (the +0x50 parents from sort_pock_bounds' p1 / p2 branches); Use island depths 0x892 and tapered walls (the per-depth branch); compensation in control on finish pieces (FUN_1801cbef0); thin wall 0x1436; finish feed / spindle override 0x1434 / 0x1435; lead ramp / helix heights and Use entry point (pk_fin_lead_init); a prior op that triggers 0x519.

comp_pock_bnds (FUN_1801ec130 l.338299; mccore 0x1805d8bf0, exported; comp_pock_bnd is already in decomp/mccore_pock2.c l.24, comp_pock_bnds dumped with DumpDecomp on 2026-11-09: decomp/pocket_b6/decomp_comp_pock_bnds.c): per bound in list order, ccomp_pock(bound, d, side, min_fil_ang, 1, 1, 0), side = outer: ccw ? 'L' : 'R', island: ccw ? 'R' : 'L'; ported with rm_ccomp_pock, then delete_intersecting and delete_nested (l.338305-338306).

INFERRED, not decoded: rings that cross each other (merge_pock_bounds, FUN_1805e02e0 not ported) and any offset the MCCE port refuses fall back to `gls_offset`. The offsets of passes and of the tool-centre bounds stay on `gls_offset` (the engine's substitute for offset_pock_bounds / MCCECalcOffset), so the list order of tool-centre bounds that extract_pocket walks is `gls_offset`'s, not MCCE's. A link that FUN_1801e0f80 would route around an island: its first record would be a waypoint (find_path_around_island NOT DECODED); the port plunges at the piece start.

Checks (no stored op of this type exists; **unverified against Mastercam**): pbmap_fill_pock_bounds against the real mccore.dll (remach_oracle.sh FILLB, 20 new cases; 490 cases, 0 differences). Hand-worked on 100 x 50, D 10, stock 1, roughing tool 30, clearance 110 %: reach - cut = four crescents, 4 x (1 - pi/4)(15^2 - 5^2) = 171.7; the map gives 170.97 (cell 0.625). The last pass starts at x 72.265: first map row above the crescent tip y = 1.4286, arc x = 87.56, swept 16 at y = 6 gives 72.23. The scene with a previous 20 mm op: the rest is four corners only; each is 25.9 by a 0.05 grid simulation of that op's posted cuts (103.66 total) against 100.40 in the map (row sampling), larger than a sharp-cornered estimate (64.4) because the op's finish pass has R2 corners from the High Speed corner smoothing (cam2d_pock_fin.c F.smooth): r = 12 around (13, 13), (1 - pi/4)(12^2 - 5^2) = 25.5 per corner.

## Open pocket (2026-11-09)

Pocket type 4 (record 0x13fa = 4), decision N9-3. Engine: `viewer/cam2d_pock_open.c` (`pko_open`), called from gen_pocket_v2. All lines are decomp/mcmill_all.c unless stated. No stored op of this type in the corpus: nothing below is checked against Mastercam's NCI; the kernels marked DLL are checked against the real mcce.dll under tools/peloader.

**Panel 3001** (HSTPocketOpenPage, FUN_180180bf0 DDX, FUN_180180e00 l.267827 MapToDlg, FUN_180180e70 l.267850, kill focus FUN_180180ee0 / FUN_180180fc0): 1975 Overlap % = 0x13e0 (pock_prm +0x1b8), 1718 Overlap distance = 0x13e8 (+0x1c0; shown = % x D x 0.01 on load, each edit updates the other), 2100 Use open pocket cutting method = 0x13f0 (+0x1c8), 2101 Use Standard pocket for closed chains = NOT 0x13d8 (+0x1b0). The panel is wired in `cui_opdlg.c` pf_emit at static 2990 (228, 52) of page 2940 when the type is 4 (control 2101 is shown as 3101: the host's Pocket type combo has the same id).

**Dispatch** (FUN_1801c5250 l.313685-313711, process_open_pocket l.334850, FUN_1801e57f0 l.334017):
- all chains closed, not (0x13d8 && !0x13f0): the ordinary pocket (process_pocket).
- closed chains, 0x13d8 on, 0x13f0 off: MCCECalcClosedOpen (mcce PockClosedOpen.cpp FUN_1800e53d0, l.124175 of mcce_all.c): **refused**.
- an open chain, 0x13f0 off: FUN_1801e5c70 close_pocket, then process_pocket. **Generates.**
- an open chain, 0x13f0 on: FUN_1801e78a0, FUN_1801e6970, FUN_1801d36c0 with param_3 = 0, FUN_1801e7040. **Generates (roughing only).**

**close_pocket FUN_1801e5c70 l.334314.** Side: pock_ccw of the chain with its closing line (FUN_1801e63e0 l.334564, FUN_1801e57a0): ccw 'L', else 'R'. FUN_1801e6220 l.334523: the line through the chain's start and end, offset by overlap % x D away from the normal n (n = right of p0-p1 for 'L', left for 'R'; disassembly 0x1801e6220 keeps the two points intact, the decompiler merged them into the direction). When overlap > 0: FUN_1801e7f30 l.335561 (param_3 = 0, param_4 = 0, param_5 = 1) extends the chain's two ends to the line (an end entity trimmed at the intersection when it lies on it, else a line added from the projection of the end on the line, FUN_1801e5900 l.334062). The bound is closed with a line (FUN_1801e5900 at the end). The result is the ordinary closed bound of process_pocket (`reg`), its first entity the first of fe.

**FUN_1801e7040 l.334991** (cutting method, entry a plain plunge l.307927 since 0x13f0 is on). FUN_1801e78a0 l.335219: the chain reversed when its ccw flag differs from 0x123a (final: ccw iff climb). FUN_1801e6970 l.334785: ccomp_pock of the chain by R + wall stock, side 'L' when climb (FUN_1801e6600 l.334661: both chain ends at least d - 2 nc_tol from the result). With no finish passes dVar20 = 0 and dVar19 = -0.01 D. FUN_1801e6720 l.334689: the chain doubled up (double_up_pock_bound mccore 0x1805dd710 disassembly: the bound + a reversed copy), offset 'L' by R + stock + dVar19 with ccomp_pock(.., 0.785398, 1, 1, 0): the gouge capsule. FUN_1801e5f00 l.334386: the trim line (FUN_1801e6220 with overlap - 50) cut to its extent over the chain (pock_bound_line_inters nc_tol x 0.01, largest t < 0.5, smallest t >= 0.5, defaults -10 / 10). Passes d = 0, step, 2 step, ...: ccomp_pock of the tool-centre path by d; the loop ends when the offset is empty or FUN_1801e6600 fails, or (d > nc_tol, no intersection with the path) FUN_1801e7930 l.335249 returns nothing. FUN_1801e7930: the bound cut at the segment (pock_bound_line_inters nc_tol x 0.01; a point within the segment or nc_tol of its ends; comparator LAB_1801e5da0 entity then t), a piece with midpoint behind n (and its foot on the segment) dropped; when more than one is in front, pieces under nc_tol dropped; each piece not closed extended to the line by FUN_1801e7f30 (param_3 = the capsule, on-line and gouge checks on). A pass whose offset crosses the path (intersect_pock_bound count > 0) is kept unclipped. The cut order: FUN_1801eb050 stacks each pass last piece to first, FUN_1801eaca0 reads the last written first, so passes last to first and the pieces of a pass in order; each cut from entity 0 by FUN_1801e9a90 l.336573 (param_5 = 0, disassembly of the call: first piece entry FUN_1801c3b20, the others linked). The links FUN_1801e0f80 l.331618 on the bounds of l.335115-335127: FUN_1801e67b0 l.334708 (the path's box grown 10x) and the capsule of R + stock + 0.01 D as an island. FUN_1801e23c0 l.332355 is ported for these links (`pko_link`): a straight feed, or along a bound by find_path_around_island (zz_fpai / zz_walk from cam2d_pock_zz.c).

**Refused (op->error says NOT DECODED / NOT PORTED):** closed chains with 0x13d8 on (MCCECalcClosedOpen); several chains with an open one (sort_pock_bounds / extract_pocket of an open bound); tapered walls with 0x13f0 (FUN_1801e6970 + FUN_1801e5de0); finish passes with 0x13f0 (FUN_1801cc170 on an open bound, l.318090-318110); no roughing with 0x13f0; keep tool down between depth cuts with the entry off with 0x13f0 (FUN_1801e7040 l.335153-335194); a pass offset that splits into several bounds (FUN_1801e7930 is used on one bound here); offsets the MCCECalcContourOffset port refuses (a self-intersecting input with several closed pieces); passes that do not end at the trim line (a chain crossing its own chord; FUN_1801e7040's loop l.335076 has no limit in Mastercam, the guard at the chain's diagonal + R is ours and only refuses).

**N9-11 audit fixes (2026-11-09).** Tapered walls: FUN_180056a70 l.59008-59017 reads record 0x1288 and the angles 0x1290 / 0x1298 for pockets (dc_taper is Contour only). The cutting-method path is ported (per depth: FUN_1801e6970 twice with FUN_1801e5de0 l.334347, the first by the wall offset less R, then by R, l.334935-334939; chain Z = the bound's +0x18); on the close and process_pocket paths the taper stays refused (FUN_1801d4620 with its taper arguments). The finish flag 0x1239 decides the capsule offsets and the first pass (l.335064-335071, l.335117-335122), with n x spacing from 0x125a / 0x1260. No roughing or an empty offset: machine_pocket writes nothing, no error (l.322563, l.334833-334835, l.335131): the op is empty. Open chain with type 3: refused with its own message (l.334927, l.334948 run FUN_1801ec3c0 on the open bound); types 0-2 with an open chain stop at warning 2367 (l.313523-313527, l.313789), not process_open_pocket: that part of O-U5 was wrong. Section start / retract: wr_pock_toolpath l.316154-316210 (the same logic as the Remachining branch l.315990-316049; retract_move, clear_move when the pocket changes, plunge_move) replaces the fixed retract, and the end of the op is clear_move l.316554. Box of an entity: bbox_gt (mccore 0x180012bc0). Several open chains: separate pockets when their boxes are disjoint (disjoint_pock_bound_bbox 0x1805e3070, sort_pock_bounds l.247-251, l.303-315); chains overlapping are refused (IsContourNested on an open bound), and with 0x13f0 an island gives warning 2405 (0x965) and no cut (l.334912-334919). FUN_1801e5840 (chain_tol) replaces ch->closed for the dispatch. A pass offset of several bounds follows FUN_1801e7930 l.335488-335534 (the head; the others copied whole); a record on a later bound indexes the head's entities in Mastercam (undefined): refused. O-U4: over 999 waypoints return -1 (l.332561, l.332585, l.332615), no move; FUN_1801e9a90 ignores the value.

**Review V-9 .. V-14 (2026-11-09).** FUN_1801e6970 appends the whole result of a ccomp_pock (l.334829-334834): a tapered offset of several bounds is refused instead of the depth being dropped (FUN_1801e7040 would take a list, which the offset port cannot). A closed chain among open ones with 0x13d8 on takes MCCECalcClosedOpen per pocket (l.313699-313700, l.334976): refused. FUN_1801e6600 measures against the whole list (proj_pt_to_pock_bounds walks `+0x60` at mccore 0x1805e6c47). The section start uses split rapids (pk_rapid, as pk_rem_plunge). The cutting method is not read on this path (l.322571-322573, FUN_1801e7040): gen_pocket_v2 routes the open cutting method before the method refusals. Taper enables: enable_taper_controls l.247981-248026 and enable_depthcut_controls: 1252 needs the Depth cuts group and the check; 1381 is the group's buddy (l.247447); control +0x3ba0 is Subprogram 1463 (DDX 0x5b7 at +0x3b60, HWND +0x40), warning 0x95b = mcmillres 2395. Waypoints on different pocket numbers (l.332621-332641) cannot occur: the bound +8 of the rectangle is 0 (RecordBoundaries_Add_PockBnd: 0x1805dd4c0 calloc, not written later; FUN_1801e67b0 does not set it), of the capsule 0 (CopyShArToPockBnds: +8 = n >> 16 for n < 65536; copy_pock_bound_properties does not touch +8).

**Size of what stays refused.** Finish passes with 0x13f0: FUN_1801cc170 param_5 = 0 (l.318088-318130: offset_pock_bounds open, FUN_1801e5f00 + FUN_1801e6720 + FUN_1801e7930 per pass) then FUN_1801bdc00 (1380 lines) and FUN_1801cd200 (390) with leads on an open bound: not attempted. Keep tool down with the entry off: FUN_1801e7040 l.335153-335194 writes a marker pair and a feed back to the start, and wr_pock_toolpath walks the ktd records (l.315650-315900, l.316052-316150). Closed chains with 0x13d8: MCCECalcClosedOpen = FUN_1800e53d0 and PockClosedOpen.cpp (about 3100 decompiled lines, l.124153-127300 of mcce_all.c), plus MCCECalcOffset and SoPockUnionAr, neither ported: refused.

**Decisions N9-9 / N9-10 (2026-11-09): decoded in place of the first version's inferences.** A pass offset collapsing to a run under 1e-4: FUN_1800cd350 l.110788 (with FUN_18013d010 / FUN_18013d280 nearest point, GetNearestEntPt, FUN_18006f1c0 / FUN_18006f4f0 the 0.001 step along the source) and the tail's FUN_1800d6180 l.115609 + FUN_1800d8b40 l.117209 are ported in `viewer/mcce_ctour.c` (tiny_side_cull, tiny_cull); the marching of the passes ends only on the real empty result. `pock_bound_intersect` (mccore_pock.c l.2187) / `intersect_pock_bound` (l.892, only its count > 0 is read here) replace gt_cross: `pko_bounds_x`, with ints_on_gt_new (l.4040) and aa_ints_new (systypes 0x180001fb0, from the disassembly). The constant of trim_gt_to_pt is mccore's exported `?nc_tol@@3NA` (0x1812b5b68); the arc also accepts both sweeps below 6.195918844579867, the sweep normalisation is FUN_1804b4440. IsBndClosed: strictly within tol (0x1805ddcf0). The doubled chain's direction byte: FUN_1800ce9a0 l.111497 + FUN_1801230c0 / FUN_180123320 (a shape of reversed twins takes its direction from the sign of d) are in the port, so the bound's own byte is passed. FUN_1801dfb90 l.330667 (a link piece between different bounds) is ported; "no pair found" writes nothing of the link (l.332695-332700).

**Stays inferred:** the line snapping in `pko_chain_gl` (chain_to_pock_bound -> FUN_1805d3f70 / 0x1805d3960 is not decompiled here). Everything else labelled as inferred above is decoded by the N9-11 paragraph.

**Checks.** `tools/tests/mcce_coff_oracle.sh` (mcce.dll): corpus 781 exact, var 4800 exact, synthetic 18000: 17976 exact, 0 mismatch, 24 refused (SoPockUnionAr); the 12 open-chain pieces within 1e-4 are now exact. This path's 13 COFF cases (chain 100 x 50 U, D 10, nc_tol 0.0025, chain_tol 0.002: tool-centre offset, passes d = 0, 5, 30, 50, the capsule with both direction bytes, d 43 / 44.99 / 45 / 45.01): 13 exact. `tools/tests/open_pocket_oracle.sh` (mccore.dll, random and contrived near-tangent, concentric, end-touching): trim_gt_to_pt 1800 / 1800, ints_on_gt_new 4000 / 4000 (1418 hits), pock_bound_intersect 1500 / 1500 (876 hits, the DLL's own boxes). The capsule walk of `pko_link`: scene `rt_pock_open_walk` (a kink in the chain, overlap 51.097%): the link into the last pass runs along two offset entities of the capsule through their corner (94.5518, -0.1117), which agrees with a separate computation of that corner; a single-line and a single-arc walk were reached in scratch scenes. There is no DLL oracle of the whole move list (FUN_1801e23c0 is in mcmill, not run under peloader). Hand-worked (U 100 x 50, D 10, stepover 50%, overlap 125%, climb): trim line y = -7.5 = chord offset (125 - 50) % D; tool-centre U at x 5 / 95, y 45; passes d = 40 .. 0 with X 45-55 .. 5-95, each extended to y = -7.5, cut inner to outer, links along y = -7.5; rt_pock_open_cut (`tools/tests/op_scenes.txt`) matches. close: rt_pock_open_close, chain extended to y = -12.5 (125% D), tool-centre bound y from -6.5 to 44 with wall stock 1. Tests: unverified against Mastercam.


## Island facing, Pocket type 2 (2026-11-09, user decision N9-3)

Line numbers: decomp/mcmill_all.c unless noted. Code: viewer/cam2d_pock_isl.c, hooks in cam2d_pock.c (`isl_f`), cam2d_pock_entry.c, cam2d_pock_fin.c (section keys), pgeo.c (`gls_face_merge`). No stored op of this type exists in the corpus, so the op as a whole is **unverified against Mastercam**.

### Decoded behaviour
- **Validity**: valid_pocket_options l.315055: roughing off posts 0x969 (mcmillres 2409 "Roughing must be enabled when Island facing is selected").
- **By depth** (FUN_1801c5250 l.313630-313642): depth cuts on and type 2: each depth generated on its own (pock_proc_by_depth, no wr_pock_depth_cuts copies), so each depth has its own bounds, entry and heights. Depth cuts off: one pass at the final depth and no island is removed.
- **Islands per depth** (FUN_1801c7970 l.314958, depth cuts on only): an island whose top (chain Z + 0x1398 stock above islands) is below depth + tol is unlinked.
- **Which tops are faced** (FUN_1801c68e0 l.314347, after the depth's pocket): depth + tol < top < previous depth (top of stock at the first depth), bounds in list order, per chain pocket.
- **Top bound**: comp_pock_bnd(island, R - 0x1388 overlap, inward) (mccore_pock2.c l.29); only the first loop of the result is kept (l.314352).
- **Check bounds** (FUN_1801c7350 l.314652): the chain pocket's outer bound and the islands strictly higher in Z, offset by R + wall stock + finish passes x spacing.
- **Test** (FUN_1801c6870 l.314283, is_ctour_nested_or_intersecting mccore 1805e4b40): the top bound nested in a check outer bound, or crossing it with pock_bound_overlap (mccore 1805e58f0) true at 2 x record 0x1228. pock_bound_overlap only advances the first bound's chords: a top bound beside the *second* chord of a check bound on every scan line gives false and the island is **not faced** (seen on a real layout, confirmed in the DLL, case `two_chords`).
- **Region**: merge_pock_bounds_facing_islands (FUN_1805e02e0 param 1; FUN_1805dec50 param 1 keeps a piece only when inside every outer bound it shares a box with and in no island), then islands and loops not meeting the top bound are unlinked (l.314375-314380).
- **Cut**: process_pocket(list, 0, 0, ...) roughing only (param_11 = 0), so no finish passes on tops and no finish allowance (FUN_1801ede50 l.339387, FUN_1801ef830 l.340290), at Z = top.
- **Entry** (_enter_pocket l.307880 -> FUN_1801bfd60 -> FUN_1801c1fc0 l.311434): the first outer bound offset outward by 0x1390; spiral methods the closest point (proj_pt_to_pock_bound), others the nearest hit of the +X line in the roughing-angle frame; it must be in the check bounds (pt_in_any_machinable_area), else the nearest entity start of the offset bound that is; none: the normal entry (LAB_1801bcbc8). Plunge there (FUN_1801c4e10), link FUN_1801e0f80 with the check bounds.
- **Exit** (FUN_1801c0190 l.310361): 0x13a0 > 1e-12, the same search from the tool position, link FUN_1801e0f80.
- **Order and heights**: top sections carry the facing flag and sort depth = the depth (bnci_wr_pock_marker, decomp/nciw/core_wr.c l.2489-2497; FUN_1801c6520 key 8), so they follow the depth they belong to. Section heights are set_heights(depth, section top) (wr_pock_toolpath l.315582, mccore 18089fa80): by depth the top is the previous depth; a top section uses last_top_of_stock (l.314394).
- **UI**: combo 2101 item 2 shows HSTPocketFacePage, dialog 3002 (FUN_18017b220 l.264027, third child, flag +0x5da0 = 1 for control 1284 Stock above islands; FUN_18017b8b0 l.264316) in static 2990 of page 2940. Fields: 25324 / 1372 overlap % / distance (0x1380 / 0x1388), 25165 approach (0x1390), 1649 exit (0x13a0), 1284 stock above islands (0x1398); DDX l.265734-265738, FUN_18017da90 l.265865, FUN_18017db70 l.265897. Wired for type 2 only; type 1 Facing uses the same dialog and is still unwired.

### Refused (op->error)
Roughing off; a chained entry point (NOT DECODED, as Facing); thin wall finishing (FUN_1801cad80 by depth, NOT DECODED); tapered walls 0x1288 (FUN_1801d4620 param_5, NOT DECODED); a chain not flat in Z (check_chains_3d l.313530); a top offset that splits in several loops (ccomp_pock's list order NOT DECODED); an entry or exit link leaving the check bounds (find_path_around_island NOT DECODED, no stand-in here). The Standard engine's own refusals also apply to the depth passes (e.g. True Spiral link around an island, Morph Spiral, High Speed channel modes).

### Audit round (formats/audit_2026-11-09_pocket.md, N9-11 / N9-12 / N9-14)
- **Merge** (I-C1, I-U5, gls_face_merge removed): merge_pock_bounds / merge_pock_bounds_facing_islands are now ported: `rm_merge_pock` in viewer/cam2d_remach.c = FUN_1805e02e0 with FUN_1805df8c0, FUN_1805de0f0, FUN_1805df740, FUN_1805dec50, FUN_1805dfc50, FUN_1805defc0, FUN_1805dede0, FUN_1805de9b0, FUN_1805dea90, FUN_1805de720 (decomp/pocket_b6/pspiral_decomp_merge.c), IsContourATrace, CalcIntersectRect, IsEqualRect, pt_to_pock_bound_dist, reverse_pock_bound, disjoint_pock_bound_bbox (scratch dump of mccore via tools/ghidra_scripts/DumpDecomp.java). aa_ints_new (SysTypes.dll 0x180001fb0) added to ints_gt, so pock_bound_intersect = ints_on_gt_new at last on arc-arc pairs (`pk_isl_gt_int`, the fitted rule, removed).
- **Standard hang N9-12**, root cause: `pk_merge` was a Clipper union at distance 0 (gls_offset): with an island reaching outside the outer loop its part outside the loop has winding -1, non-zero filled: a phantom loop, which offset inward by a stepover became the same loop again forever. Tangent geometry made this reach (rb outer bound touching the r16.5 island at the top and bottom). Fixed by the merge port: Mastercam's FUN_1805dec50 never keeps it. No loop limit added (the existing 100000 guard is from the earlier port; the decompile has none). Same root cause for methods 8 and 2 (HS / Parallel Spiral both use pk_merge).
- **Links** (found after the hang: a straight link crossed the island): `pk_line_inside` was wrong when one entity was crossed twice with one crossing at a move end. Rewritten from FUN_1801e0fd0 l.331634 (every pock_bound_line_inters record, t in (-2e-7, 1.0000002), farther than nc_tol from both ends fails; then the midpoint). The pocket entry keeps the old rule (`pk_line_inside_ent`): with the decoded test an entry link that clips the rough boundary near a zigzag start would turn a helix into a plunge (scene rt_pock_oneway), because the real Mastercam link goes around the island (find_path_around_island, NOT DECODED).
- **I-U2**: section key: sVar16 + param_7 (machine_pocket passes r15 - 1, D:1801d3963, the sub-pocket index) in the sub field of the marker; the part is the chain pocket index.
- **I-C3**: clearance incremental only when clearance is on (set_heights 18089fa80).
- **I-C2**: flatness with chain_plane_tol (check_chains_3d_view mccore_chain_range.c l.6215-6220). Arcs compare the centre Z and need a parallel view (e->planar).
- **I-U1**: an open chain with type other than 3 / 4: warning 0x93f (mcmillres 2367) and no toolpath (l.313523-313527, l.313789), Standard too.
- **I-U4**: the depth and top of stock of each pocket come from that pocket's first non-island chain's Z (set_depths l.313612), not chain 0's; only matters with incremental depth / top. The depth list per chain pocket (PkCp in gen_pocket_v2).
- **Top bound**: comp_pock_bnd (and the outward approach offset of FUN_1801c1fc0) through `rm_ccomp_pock` (ccomp_pock port, MCCE offset), head of its list as l.314352-314353; the old refusal for several loops is gone.
- **Retract between sections** (Island facing only): set_heights(previous Z, previous Z), retract_move for the same pocket else clear_move (wr_pock_toolpath l.316156-316179, mccore_moves.c).
- **Section top for spirals**: FUN_1801ede50 calls adjust_depths at D:1801ee708 when no finish runs at this pocket and it is the last (`param_4` and index == count - 1), as FUN_1801ef830 l.340447 and FUN_1801cc170 l.317982 otherwise. The section top = previous depth is decoded.
- **finish_with_rough** l.313559: only pock_rapid_finish (the keep-tool-down link in the entry, l.308142) reads it, and Keep tool down finishing is refused for every Pocket; no effect here.
- **Ordinal_4282** = CWnd::EnableWindow (formats/renumber_tools.md): 0x504 is greyed (not hidden) for Facing. Panel 3002 wired for types 1 and 2; overlap distance recomputed as % x D / 100 on page init and activation (FUN_18017da90 l.265876-265881, l.266032-266036).

### Independent review (V-*, 2026-11-09)
- V-3: Facing tool-centre bounds per chain pocket (pk_face_tc on the pocket's region). V-4: per-chain heights: an incremental plane follows the pocket's top, an absolute one stays (set_heights). V-2: the section's "new pocket" is the marker's chain pocket (l.313710, FUN_1801d36c0, l.339443), every PkPocket carries it (`sk`). V-5: 0x2309 (mccoreres 8969) for types 0 / 1 / 2 and for a closed 3D chain the Clipper path rejects; Z in the op's view.
- V-14: Island facing now takes the shared section start `pk_sec_start` (retract / clear_move with set_heights(prev, prev), the distance test, the tool-down plunge) instead of its own retract: the stale flag and the ht3 clamp are gone with it. A single open chain gives 2367. Point chains: set_chain_closed (mccore_chain_range.c l.12427) sets the closed flag when the two ends are within tol, so a point chain is closed for l.313523.
- Scenes: rt_pock_facing_2z (two chains at Z0 / Z-3, depth incremental, top absolute: -5 and -8, planes unshifted) and rt_pock_facing_2z_inc (top incremental as well: the second chain's feed plane Z7, retract Z22). Both set Finish passes 0 (page 3004, control 1369): Facing with finish passes stays refused NOT DECODED, and that is the dialog's default state.
- `q->sub` / `q->sk`: sort_pock_bounds numbers the pockets in list order (mccore_pock.c l.308-326), so the order is that of the list MCCECalcOffset returned. Island facing uses gls_offset's order: not settled.

### Still inferred / unsettled
- Check bounds offset: gls_offset for MCCECalcOffset (the documented substitute for every Pocket bound).
- process_pocket at radius 0 (FUN_1801d4620 -> MCCECalcOffset(1, -0.0, ...)): the zero-distance MCCE offset is not run, the loops are used as merged. Not settled (no DLL case at distance 0 through this wrapper).
- check_chains_3d_view for arcs: the second point local_29a8 is read as the 3 doubles after the arc's first point; the arc layout of `ent` is not known here. Arcs use the centre Z and e->planar.
- Retract before an entry after the heights: pk_entry_emit's approach heights are the section heights; plunge_move's exact sequence (first / part change, l.316188-316200) applies to Island facing only through those heights.

### Checks
- `tools/tests/isl_face_oracle.sh` (real mccore.dll under tools/peloader, against the port): 161 cases, 0 differences: pock_bound_intersect 12, pock_bound_overlap 12, is_ctour_nested_or_intersecting 12, pt_in_any_machinable_area 72, merge_pock_bounds_facing_islands (+ filter) and merge_pock_bounds 53, compared entity by entity (6 significant decimals): several disjoint outers, islands reaching outside the outer loop, the tangent geometry of the hang.
- Hand-made parts, clearance from every island whose top is above the cutting Z: 0 violations in 9465-22489 samples per run (cutting methods 8, 0, 2, 6 on a part with a crossing top bound; 8, 0, 2 on a simple part), minimum clearance 1.0 = the wall stock. One finish pass was on in these runs; finish off was not exercised (the `--rec 1239=b:0` patch does not change finish_n in cam_cli).
- `tools/tests/remach_oracle.sh` 0 differences (the peloader gained CRT bsearch). `tools/oracle.py --type pocket` on every part with a stored Pocket op: 7 generated ops ok 7/7, p95 median 0.004, worst 0.028 (the others refuse as before). tools/tests/pocket_regress.py: 22 stored Pocket ops (18 parts): moves identical to a cam_cli built from toolpaths2 before this round.
- Op scene rt_pock_islface blessed; 18 pocket scenes unchanged except the 5 *.mcamui files, which differ only by the known PROJECT-FORMAT line.

## Remachining: audit follow-up (N9-11, N9-12, N9-13; formats/audit_2026-11-09_pocket.md)

Decoded and ported since the first version:
- Heights per section: `set_heights(floor, ref)` (mccore 0x18089fa80; `pk_h`): section `set_heights(depth, top)` l.315582, retract `set_heights(prev depth, prev depth)` l.315998 / l.316160, the end l.316546-316554 (feed plane l.316548). The NCI of the stored ops confirms it: A001 op 13 retracts to 9.5 (incremental 10 on the depth -0.5), not 10.
- `pk_rapid`: the stored NCI's rapids are never diagonal: up first when rising, across first when going down.
- 0x992 / 0x993 (Plunge after first move / Retract before last move) l.315505-315511, l.315782-315860, l.315914-315931: the record heights in `pk_rem_cut`.
- merge_pock_bounds FUN_1805e02e0(list, 0): `rm_merge_bounds` (FUN_1805df8c0, FUN_1805de0f0, FUN_1805de9b0, FUN_1805dec50, FUN_1805dfc50 + FUN_1805defc0, the MSVC qsort order of equal keys, FUN_1805dede0, FUN_1805dea90). Not only crossing rings change: FUN_1805dede0 drops slivers and open pieces, FUN_1805dea90 nesting.
- Pass offsets: offset_pock_bounds, move_split_off, merge, delete_intersecting by tool-centre bounds / chains, sort_pock_bounds with the p2 test of the chains (l.338673-338698). The lead check's inner bounds: offset_pock_bounds per bound (FUN_1801c4860 l.313160).
- delete_nested by is_ctour_nested (the split-offs aside, l.338300); a later piece's first record is the first waypoint of FUN_1801e0f80 (`pko_link`); point pieces keep their section. The Z gate of `pk_rem_cut`: the DLL always writes the record, a zero-length one is no motion.

Still inferred: the fallback to `gls_offset` when the MCCE port refuses a call, and FUN_1801edcd0's writer bounds (MCCECalcOffset, not ported), which also set the list order extract_pocket walks.

## Standard Pocket retract (N9-12)

Types 0 / 1: the section start in wr_pock_toolpath (l.316156-316197) through `pk_sec_start` / `pk_sec_plunge` / `pk_rapid`, the end l.316546-316554. Against the stored NCI (19 parts with stored Pocket ops, 24 ops; `pocket_regress.py` base toolpaths2 35ec3ea): 20 identical, 4 changed (A001 op 13, CHGREC op 2 x 2, engraved coaster op 1). The rapid Z sequences of all 9 ops that generate now equal the NCI (base: 5 of 9); CHGREC ours -> ref p95 0.137 -> 0.038, the cut length 9 -> 8 = the NCI's.

## Independent review fixes (V-1, V-6, V-7, V-8, V-14; formats/audit_2026-11-09_pocket.md)

- V-1: the reference of the incremental heights in a section is the marker's top: top of stock for the first depth, the previous cut's Z for the copies of wr_pock_depth_cuts (l.315181: top = source Z less the shift of the previous copy), used by set_heights l.315582. Standard (`g_pk_ref`, `pk_sec_start`), Remachining (`pk_rem_cut`) and Open (`pko_section`) use it. No stored op has depth cuts with incremental planes: checked by `tools/tests/pocket_heights_test.py`, hand-derived.
- V-6: Plunge after first move / Retract before last move, in `pk_rem_cut`: the first entity ends at the retract height (not the clearance in the first section: bVar17 is already clear), and a plunge at its end at the plunge rate follows (l.316463-316467), not a ramp along the next entity; the retract or clear_move comes before the last record (l.316403-316428; local_920 drops the op's end clear_move, l.316422 / l.316552), not after it; rough pieces without entry curves carry no lead flags and are lifted too (l.338740-338753, l.338862-338870).
- V-7: delete_intersecting / delete_nested compute the boxes of their second list as well (pock_bounds_bbox(param_2)).
- V-8: Remachining's depths per chain pocket (l.313602-313612), as gen_pocket_v2.
- V-14: the -95 marker re-entry of One Way (zz) is a real wr_pock_toolpath path (l.316471-316487) and now uses retract_move / plunge_move on the section's heights. `pk_e0f80` and `pk_x_straight` stand in for the undecoded walk around islands, so they are not section starts: their comments now say so.

## Final review of the merged Pocket code (2026-11-09)

- Open pocket depths per chain pocket: set_depths per chain pocket l.313612 with process_open_pocket inside the loop l.313694; `pko_open` applies `pk_cp_for_z` per chain, and on the path closed by the line `pk_isl_zs(.., all = 1)` gives the Z of open chains (reg follows the non-avoided chains).
- Open pocket on the ordinary path: `g_pk_sec_on` for every type but 3 (l.315990 branches on == 3, l.316156-316197).
- Remachining: `pk_cp_for_z` shifts the heights with the top per chain, as gen_pocket_v2 (set_heights mccore 18089fa80).
- plunge_move takes the Z of the section's first record (l.316198; helix top, FUN_1801c4a60 l.313310-313316), so `pk_entry_emit` rapids to max(feed plane, that Z) (mccore_moves.c l.148-153) and the test of `pk_sec_start` uses it.
- `q->sk`: a pocket nested in an island is its own chain pocket (sort_pock_bounds about l.308-318): the innermost outer bound holding the tool-centre bound.
- Thin wall copies at the first depth: from the second copy on the marker's top is the Z of the copy before it (FUN_1801cad80 l.317014-317016); copy 1 and the original keep the marker's top.
- check_chains_3d (l.313530) for types 3 and 4 on all chains (l.313523-313527). Remachining refuses an open chain (FUN_1801e5c70 l.338282, not decoded) instead of dropping it.
- Remachining's end of op: start_top_of_stock is the last set_depths call (l.316545, mccore_depths.c l.461), the last chain pocket.
- Facing with several chains: per chain pocket (process_pocket l.313707), not one offset of the merged list. A chain `pk_region` drops is no longer counted in the Z list, which had turned the per-chain depths off.

## Tapered walls: process_pocket's taper arguments (N9-19)

Unverified against Mastercam: no stored Pocket op has 0x1288 on, so only the hand-worked formulas below check it. viewer/cam2d_pock_taper.c.

- FUN_180056a70 l.59008-59017: on when depth cuts are on (op +0x800 = record 0x868), 0x1288 set and |0x1290| or |0x1298| > 1e-12 (`pko_taper_on`). FUN_1801c5250 cVar4: by depth (l.313636-313641), `pock_proc_by_depth`, each depth through process_pocket (l.313707; open pocket l.313694 -> process_open_pocket l.334972, the same call for a pocket closed by the line).
- Per depth, process_pocket l.323256-323264: FUN_1801d4620(chains, 1, .., param_5 = 1) -> FUN_1801d4850 for each bound in list order (`pk_taper_region`):
  FUN_1801d34b0(bound Z +0x18, angle, first = param_2 = 1): `d = tan(a) x (z_bound - depth - (0x1231 ? cr : 0)) + (stock + cr) / cos(a) - cr` (the `- D/2` of the first pass cancels the `+ D/2`), angle 0x1290 outer / 0x1298 island (disassembly 1801d48f7 xmm9, 1801d4992 xmm6), FUN_1801d5870 direction, comp_pock_bnd side: outer inward, island outward, Facing's outer outward (flag 0, l.323064-323073). The outer stock (FUN_1801d34b0 uses op +0x278) applies to Facing's outward offset too, as decoded.
- Split-off pieces (`+1`): not appended with param_5 (l.323141); FUN_1801d35f0 / FUN_1801d3ca0 (l.322423 / l.322655) drop the split-off outers nested in another source's island, then islands nested in or crossing a kept split-off and shorter than it (`pk_isl_nested`, gl_len).
- The result `local_98` (`reg_t`, `q->tr`): second FUN_1801d4620 by D/2 (`tc_off_g = R`; the stock is in the first offset), machine_pocket and FUN_1801cc170's control-only base (local_168 = param_1, l.317964) use it. Empty at a depth: no cut there, not "tool does not fit".
- By depth, the top of stock of a depth is the previous depth (adjust_depths), the same as Island facing, so `isl_f` plumbing is `isl_f || taper`: sections keep the pocket's chain number and sub-pocket, `pk_isl_sec_params` and `g_pk_ref`.
- Island facing follows: FUN_1801c68e0 (top pockets, l.314403: process_pocket param_4 = 0) is untapered, the depth pockets tapered.
- Spring passes: warning 0x96b, no spring passes (FUN_1801c5250 l.313752-313768; mcmillres "Spring passes not output on tapered wall pockets").
- Dialog (cui_opdlg.c): 1381 / 1252 / 1466 live for every Pocket type but Remachining, group + check (HSTDepthCutsPage::enable_taper_controls l.247981, FUN_18017c550 l.265140-265143); 0x1288 / 0x1290 / 0x1298 honoured.
- Refused with their reasons: thin wall with taper (FUN_1801cad80 per depth); the MCCE offset port's refusals; a bound without its chain Z; Remachining with taper (no decoded taper path here, cam2d_pock_rem.c); Open pocket on closed chains through MCCECalcClosedOpen; several bounds on Open's cutting-method taper.

## Keep tool down 0x126a (overnight 2026-11-12, agent K)

Decoded from decomp/mcmill_all.c (l.N) and decomp/mcmsurf_all.c (ms l.N); ported in viewer/cam2d_pock.c (`pk_ktd_link`, `pk_ktd_take`,
`pk_ktd_pend`, the marker branch of `pk_fin_section`, the rough branch of gen_pocket_v2's section loop) and cam2d_pock_fin.c (`PkFin.ktd`,
`fwr`, `finall`); record 0x891 is now read for Pocket ops (cam_fileop.c).

**Where the decision is made.** FUN_1801cd200 (the writer of one finish bound, l.318365) at l.318461: the link is wanted when
`pock_tool_down` (the tool stayed down after what was generated before: the pocket's roughing, an earlier bound / pass), cVar19 (= 0x126a,
pock_prm +0x42, when param_9 = 0 - always from FUN_1801cc170, whose call passes `uVar26 & 0xff00`) and (0x126b, or depth cuts off (op
+0x800 = record 0x868), or not `pock_finish_all` (0x1230 or 0x1436, l.313514-313521), or not param_3). param_3 = FUN_1801cc170's bVar18 =
`!finish_with_rough && closed` (l.317975) on the first bound of the first pass only (cleared after each bound, l.318279; a later pass sets
it only with 0x126a off, l.318011-318015). `finish_with_rough` (l.313559-313567) = 0x126b, Use island depths 0x892 (op +0x82a), tapered
walls (FUN_180056a70) or Island facing; msurf_map sets it to 1 (ms l.339989) for the Surface rough levels, which never run
GeneratePocketToolpath. With 0x126a off the passes after the first retract in FUN_1801cc170 (FUN_1801e3150, l.318011) - as before.

**Op code 4 (2D Pocket: GeneratePocketToolpath -> wr_pock_toolpath).** FUN_1801cd200 copies the pass's bound list (param_2 = local_1a8,
the pass bounds; op code 0xe passes cc170's param_2 instead, l.318254-318257) into pock_ktd_bnd and writes marker 1 (-97, record +0x1e =
the bound's index), a feed to the start, marker 2 (-98), prev_nc_pt = 1e38 and the feed to the start again (l.318470-318488)
(bnci_wr_pock_marker: decomp/nciw/core_wr.c l.2464-2475). wr_pock_toolpath, types other than 3:
- -97 in a finish section (l.315658-315760 via LAB_1801c88a2): with 0x126a on and the same marker pocket (local_93e = local_942) and
  sub-pocket (local_91e = local_90c, marker +0x28 = FUN_1801cc170 param_8 = machine_pocket's extract count - 1, disassembly
  0x1801d3bb4-0x1801d3bb8) as the section before: local_944; else (LAB_1801c8b0b) lVar19 = 1 with the bound (`g_pk_kp`). The records up to
  -98 are skipped (l.316510-316518).
- local_944, at the first linear record (l.315875-315905): FUN_1801e0f80(the bound, prev_nc_pt, the record) at pock_depth = the section
  depth; lVar19 = 0 (l.315890). Then the section start (l.316101-316150) finds the tool within nc_tol: no retract, plunge_move with the tool down
  (the NCI's two zero-length plunge-feed records, BLOCK op 3 records 122 / 123).
- lVar19 > 0 at a section's first record, 0x891 on (op +0x829) and the same pocket / sub-pocket as the section before (l.316099-316154):
  the same link with the pending bound at its depth (dVar10); lVar19 = 0. lVar19 is not cleared at a section start, so a pending bound
  can wait for a later section.
- The roughing's own markers (FUN_1801ede50 l.339478-339504, FUN_1801ef830 l.340363-340394): tool down, depth cuts on, 0x891, entry off
  (0x12a0, pock_prm +0x78) and not facing islands: a pair at the end of the roughing with the rough bounds (Zigzag: rotated back,
  l.340386) -> pending (`pk_ktd_pend` in the section loop); the next section of the pocket links to its first record at that depth and
  plunges with the tool down (FUN_1801c4e10 l.313342: the plunge entry's one record is the plunge at depth).
- pock_ktd_bnd is emptied at the end (l.316574-316587).

**Op code 0xe (Surface Rough Pocket levels).** FUN_1803769e0 calls process_pocket(.., param_9 = 1) (ms l.554779) -> pock_op_code = 0xe
(l.323217): no markers are written (core_wr.c l.2446), wr_pock_toolpath is not run, and machine_pocket's order stands: per pocket its
roughing then its finish passes (FUN_1801d36c0 l.322583-322595, LAB_1801d3b83 l.322565-322568) - the port no longer sorts these sections
(`pk_sec_cmp` is wr_pock_toolpath's). FUN_1801cd200 then runs FUN_1801e0f80(cc170's param_2 = the pocket's tool-centre bounds,
prev_nc_pt, the start) at once (l.318493-318497), then FUN_1801c3b20 (l.318536: 0x126a and pock_rapid_finish = 0) and FUN_1801bfcb0 ->
FUN_1801bf680 (l.309886-309930): both run FUN_1801e0f80 again from the tool through ppVar20 (FUN_1801cc170 param_7 on the last pass =
FUN_1801edcd0(the tool-centre bounds, the spacing 0x1260): the bounds offset inward by the spacing (MCCECalcOffset) and sorted; else
param_2), then plunge_move with the tool down (decomp/mccore_moves.c l.157-165): a feed at the current Z to the start when still away,
the plunge feed to the depth. msurf_map forces 0x126a, 0x1230 and 0x1269 on and 0x1270 = 5 for op 10 / 131 (ms l.340117-340120); every
stored op 10 holds them (20 / 20 in the 2017 / X9 corpus).

**FUN_1801e0f80** = FUN_1801e23c0 (pock_move_avoid_islands) with ignore_wire_islands: the existing port `pko_link` (cam2d_pock_open.c);
when it finds no pair of bound points for a piece it writes nothing and returns 0 (l.332695-332700), which the callers above take as done.

**Still refused (NOT DECODED / NOT PORTED):** Remachining with 0x126a (its own writer branch l.315663-315730 and FUN_1801ec3c0's
FUN_1801cd200 call l.338895 with param_9 set); the Surface rough levels with depth cuts + 0x891 + entry off (FUN_1801ede50 l.339505 links
back to pock_rough_start_pt, not decoded); a link of over 999 waypoints (-1, l.332561: the callers' FUN_1801e3150 path); compensation in
control on the Surface rough levels' retry. Ours, as everywhere in the port: the pocket / sub-pocket of a section is our pocket index k
(PkSec.sub = 0 outside Island facing / tapered walls); `pock_tool_down` at the first finish bound of a pocket (generation order) is
"the pocket was roughed, or an earlier pocket of the same chain pocket exists"; the FUN_1801edcd0 offset is gls_offset.

**Checks (2026-11-12).** BLOCK op 3 (2017 .mcam and X9 .mcx-9; Constant Overlap, depth cuts with 0x891 and the helix entry, 0x126b on,
one finish pass, 0x126a on): refused before; now generated, `ORACLE_FILEOP=1 tools/oracle.py --op 3` ref->ours 0.004 / 0.193 / 0.380,
ours->ref 0.004 / 0.202 / 0.381, cut length 48 / 48. The finish link is Mastercam's: the roughing ends at (2.7025, -1.5750), a straight
feed at depth to (1.4750, -1.3098) (NCI record 121, then the two zero-length plunge-feed records 122 / 123), the lead line to
(1.3424, -1.4424); the deviation left is the roughing's helix centre (see "Constant Overlap / Facing"). The other stored Pocket ops
(CHGREC, PROJECTLETTERS, ROUGHPROJNCI 2017 / X9, engraved coaster): moves identical to f02a6d9 (tools/tests/pocket_regress.py).
Surface Rough Pocket corpus ops (cam_cli srpocket with the record's tool and chains, tools/oracle3d.py, cut moves; p50 / p95 / max,
inch), all refused on 0x126a before:

| op | ref->ours | ours->ref | cut length ref / ours |
|---|---|---|---|
| CONTOURFINISH1 2 | 0.000 / 0.031 / 0.049 | 0.007 / 0.052 / 0.113 | 273 / 260 |
| FINISHSCALLOP 1 | 0.032 / 0.078 / 0.144 | 0.039 / 0.074 / 0.599 | 623 / 536 |
| LEFTOVER 16 | 0.000 / 0.081 / 0.250 | 0.020 / 0.102 / 0.228 | 506 / 446 |
| RESTPASSES 16 | 0.000 / 0.081 / 0.250 | 0.020 / 0.102 / 0.228 | 506 / 446 |
| STEEPSHALLOW 4 | 0.000 / 0.030 / 0.400 | 0.006 / 0.053 / 0.131 | 2471 / 2456 |
| CURVE 5 AXIS REVIEW FILE 1 | 0.027 / 0.125 / 0.655 | 0.034 / 0.060 / 1.025 | 1782 / 1739 |
| PROJECTBLEND 7 | refused: the rough boundary of a level region came out larger than the region (gls_offset, ours; without this check the op hung in the zigzag plan) | | |

In the NCI the link is the same kind of move as ours (FINISHSCALLOP level -1.4950, records 41-42: from the roughing's end a straight feed
at the level to the finish pass's closest corner (-1.4096, -1.6646), then the pass); no level of ours matches one of Mastercam's exactly
(FINISHSCALLOP: -1.51 / -1.6071 ... against -1.4950 / -1.5813 ...; the op 10 levels and regions are sr_levels / c3_slice,
formats/surf_rough.md 2.6), so the remaining deviations are not measured per link.
