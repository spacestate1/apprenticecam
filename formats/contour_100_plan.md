# Contour to 100 % - plan (started 2026-10-28, branch toolpaths2)

User request 2026-10-28: "lets get the contours all working 100 percent" (after "in contours ... the tabs menus do not fully
come up"). Rules as always: decoded code with citations, ours labelled, NOT DECODED left out.

## 0. Where Contour stands (tools/tests/golden/op/gen_contour.txt, dialog dump)

**Greyed whole pages:**
- Toolpath Type, Holder, Axis Control, Rotary Axis Control.

**Disabled until their check box is on** (as Mastercam):
- Depth Cuts, Multi Passes, Tabs.

**Greyed controls per page** (106 in all; "ND" = greyed because the effect is NOT DECODED, `pf_engine_nd`):

| Page | Greyed | Controls |
|---|---|---|
| Cut Parameters | 3 | Optimize cutter comp in control, Infinite look ahead (ND: off refused), Max. depth variance |
| Depth Cuts | 11 | Max rough step / finish cuts / finish step edits, Keep tool down (ND), Subprogram + Absolute / Incremental, Tapered walls + angle, By contour / By depth |
| Lead In/Out | 24 | Use entry / exit point (+ point depth), Enter on first depth cut only (ND), Plunge after first move (ND), Exit on last depth cut only (ND), Retract before last move (ND), Override feed rate (in / out), Adjust start / end of contour (Length %, Extend / Shorten), the -> / <- copy buttons |
| Arc Filter / Tolerance | 27 | Total tolerance + its trackbars, line / arc filter ratio, XZ / YZ arcs (post opt-in), One way filtering, min / max arc radius, the tightening radios, Smoothing (ND), Output 3D arcs |
| Coolant | 19 | Every coolant combo after Flood (Mist, Thru-tool, Aux 1-9 ...) |
| Multi Passes | 11 | Number / spacing of roughing and finishing passes, Final depth / All depths, Keep tool down (ND), Feed rate / Spindle speed overrides |
| Tabs | 24 | All tab controls but Width / Thickness / Ramp angle (most ND) - 2026-11-04: all live; square point on splines refused |

The Contour type combo offers only 2D / 2D chamfer. Mastercam's list (record 0x1308 = ctour_prm +0xe0, the combo's data, field
table: 0 2D, 1 3D, 2 2D chamfer, 3 3D chamfer, 4 Ramp, 5 Remachining, 6 Oscillate): the others' engines are NOT DECODED. Since
2026-11-04 a stored op of another type is refused ("contour type X: its engine is NOT DECODED") instead of being cut as 2D (audit
2026-10-26 cam row), the combo shows its type and saving keeps it. Corpus (163 readable contour ops): 145 2D, 16 2D chamfer, 2 3D
(a Mastercam sample, refused already for its tool plane); none Ramp / Remachining / Oscillate. Ramp: ported 2026-11-04 (below).
The Compensation type combo offers Computer / Off (Control / Wear / Reverse wear are not offered).

## 1. Batches (proposed order)

1. **Tabs** (section 2; the user's report): automatic placement, the shape-size filter, the start offset rule, Full /
   Partial, Use feed plane for full height tabs, square point (make_geo_tabs), Manual (Position...), the page's enables,
   saving the tabs as TP_ID 30 records.
2. **Multi Passes**: roughing / finishing passes and spacing, Final depth / All depths, Keep tool down, feed / spindle
   overrides.
3. **Depth Cuts**: the rest of the page (finish cuts and step, Keep tool down, Subprogram, Tapered walls, By contour / By
   depth).
4. **Lead In/Out**: entry / exit points, first / last depth only, plunge after first move, retract before last move, feed
   overrides, adjust start / end of contour, the copy buttons.
5. **Cut Parameters**: compensation types (Control / Wear / Reverse wear), Optimize comp in control, Infinite look ahead,
   Max depth variance, the contour types (Ramp / Remachining / Oscillate if Mastercam 2017 has them for 2D contour).
6. **Arc Filter / Tolerance**: the filter's remaining controls and Smoothing (bnci_point_distribution).
7. **Coolant**: the other coolant lines (post side).
8. **Holder page** (with the Simulator holder work), **Toolpath Type** page, **Axis Control / Rotary Axis Control**.

Each batch: decode -> port -> dialog live -> the op scenes / oracle parts -> TODO.md.

## 2. Tabs - decoded so far (2026-10-28)

The tab settings block is record 0x1380 (op+0x1318). Its fields come from the field table (formats/opdlg_fields.tsv, page
2834 HSTTabPage).

**Where tabs are made.** mcmill `bnci_ctour` (decomp/mcmill_ctour.c l.343-349), after `chain_regen`, when Tabs
(op+0x1318) is on:
- with "Use square point for tab position" (op+0x1352) on: mccore `make_geo_tabs`;
- with Automatic (op+0x1319) on: mccore `add_auto_tabs`.

The tabs are saved with the chain (`save_chain` with a `tab_list_rec` list) as the TP_ID kind 30 records. Those are the
records this program already reads from Mastercam's parts (formats/mcam.md "Toolpath geometry references").

**`add_auto_tabs`** @18021afe0 (decomp/mccore_tabs.c). For Contour, opcode 1 (opcodes 0x4a / 0x53 read a second block at
op+0x14d1.., NOT PORTED). For every chain of the op (`tp_get_chains`, `chains_from_db`), the tab count n:
- By number (op+0x13d8 = 0): n = Number of tabs (op+0x131a).
- By max distance (op+0x13d8 = 1): n = chain length / (width op+0x1320 + max distance op+0x13e0). It is rounded up: a
  fraction above tol adds 1, and n is at least 1.

Then FUN_180223720(chain, n).

**FUN_180223720** @180223720:
- **Shape filter.** Unless "Tab all" (op+0x1388) is on, the chain becomes a pocket boundary (`chain_to_pock_bound`, tol x
  10). If an X x Y rectangle (op+0x1378 / op+0x1380) fits inside it (`is_rectangle_in_boundary`, 0.01), the chain gets no
  tabs (`unselect_chain`). So tabs are made only on shapes smaller than X x Y. A failed boundary also gives no tabs.
- **Spacing.** step = chain length / n. The first tab is at step / 2. It is at 0 when Lead In/Out (op+0x888, record 0x8f0)
  and "Enter/exit at midpoint in closed contours" (op+0x928, record 0x990) are both on.
- **Walk.** The chain's entities are walked in order with the running length. A tab goes at every position closer than tol
  to the entity's span. Positions within tol of the chain end are skipped. Point entities get none.
- **Record** (FUN_18021ae10, a 0x98-byte `tab_list_rec`):

  | Offset | Value |
  |---|---|
  | +0x10 | the point (`creep_along_ent` by the distance on the entity, reversed when the chain entity is) |
  | +0x30 | t = distance / entity length (1 - t when reversed) |
  | +0x38 | width (op+0x1320) |
  | +0x40 | thickness (op+0x1328) |
  | +0x48 | op+0x1330 (byte) |
  | +0x4c | op+0x1334 (short) |
  | +0x50 | op+0x1338 |
  | +0x60 | op+0x1348 (ramp angle) |

  Router products (GetProdEnableBits & 0x10) take only op+0x1478.

**`make_geo_tabs`** @1802248a0 (square point): for each chain, the point entities (`get_ent` kind 6 on the chain,
FUN_180222000) give tab positions (FUN_1802256c0 picks the chain entity by the Start / Midpoint / End choice op+0x1332).
The chain is then saved again with those tabs. The exact picking: NOT READ yet.

**Not read yet:**
- `position_tab` (Manual, the Position... button);
- the Full / Partial and the feed-plane option in the tab writer FUN_180056be0 (the partial case is ported in cam2d.c
  cut_path);
- the enable rules of the page (FUN_180190d80, read: the Manual / Automatic / number / distance / shape enables);
- the TP_ID 30 write in `save_chain`.

**Ported 2026-10-28** (cam2d.c ct_auto_tabs / ct_rect_in_boundary, cam_fileop.c tab fields + cam_tab_mirror, cui_opdlg.c):
- **Automatic tabs** are made from the chains at every regeneration: count by number or by max distance, the shape filter
  (is_rectangle_in_boundary), the start offset, the walk over the entities. Ours: the boundary is the chain's polyline at
  tol x 10, where Mastercam uses the pock_bound arcs.
- **Checked** against the tabs Mastercam stored. BRACKET4 op 15 (by distance, 3 tabs) and NAKTO op 5 (by number, 2 tabs) give
  the same 5 points to 4 decimals.
- **Manual** cuts the stored tabs; with none placed there are no tabs (it was "spread evenly" before).
- **Square point** keeps cutting the stored tabs (make_geo_tabs not ported; ours). A new op with it on is refused.
- **Full tabs** are refused: the rise is decoded (retract if on, else clearance if on, else the feed plane; always the feed plane
  with 0x13b9), the move at the tab end is not yet.
- **Dialog:** live are Manual / Automatic, Number / Max. distance and their edits, Shapes less than / Tab all, the shape size,
  with the page's enables (FUN_180190d80 / FUN_180191050). Still greyed: Position..., square point + Start / Mid / End,
  Full / Partial, the feed plane check, Overwrite edited tabs.

**Ported 2026-10-28 (2): Position...** (HSTTabPage FUN_180190070 + mccore position_tab, cui_opdlg.c tb_*): Manual with square
point off makes the button live; the dialog is suspended; "Select chain to add tabs to (press <Enter> when done)" (mcmillres
2837) takes an entity of the op's chains (another entity ends the command); "Place tab at desired position" (2838): the tab
follows the cursor on the nearest chain entity within sel_dist and a click places it; Enter ends; Esc while placing drops that
tab; Esc at the chain prompt (get_event 1, [inferred]) takes the last tab of the run out and ends. Ours: tabs are centre
points in the pending op (Cancel drops them; Mastercam's save_chain keeps them), the click point itself is snapped, no
spline snap, the markers are fast-marker squares at the centres. Scene tb_manual.

**Ported 2026-11-04 (3): Full tabs** (cam2d.c cut_path_full_tabs; the tab writer FUN_180056be0, decomp/mcmill_ctour.c
l.8979-9165, with the arguments of its calls read from the disassembly - Ghidra drops 9 of FUN_18005ab70's 13 and 14 of
FUN_1800518b0's 18):
- applies at every depth (tab +0x48 set, l.8981); the span is the partial tabs' (checked: BRACKET4 op 15 / NAKTO op 5);
- tab start: "Use feed plane for full height tabs" (0x13b9) -> to the feed plane (init_depth) at the cut feed, a rapid with
  rapid retract (op +0x141 = 0x1a9); else mccore retract_move (decomp/mccore_moves.c l.201-270: feed to the feed plane without
  rapid retract, then a rapid to the retract height if 0x189, else the clearance height if 0x179, else the feed plane);
- tab end (mcmill.dll 0x180059a40-0x180059b51): without leads (op +0x888 off, or +0x890 and +0x8d8 off) a rapid at the raised
  height to the end point, down to the cut depth (the cut's motion type and feed); with them FUN_18005ab70(file, end point,
  ctour record, t, sVar26, NStack_12b84 motion type, uVar46 flags, param_6 arc mode, the entity, feed, cut depth dStack_12b78,
  raised height dStack_12af0, ...): the path = [end point, the entity at t + 0.06] (no +0.06 on a full circle), FUN_180051990
  through the thunk FUN_1800518b0 (0x1800518b0: rcx / rdx = the record array, flags param_8 = param_10 = 1 -> the entry always,
  also with "first depth cut only"; param_9 = (global 0x180303310 == 1), param_11 = 1; overlap off), then a rapid at the raised
  height to the entry's start, down to the cut depth, the entry's lines / arcs at the cut depth, onto the end point;
- (Full with Ramp moves: the writer's ramp branch, ported 2026-11-04 - see "Ramp tab moves" below) refused: a Full tab over the path's
  start (what the writer does there is NOT DECODED); adjusted paths with tabs stay refused (as before);
- dialog: Full / Partial and the feed plane check live (the Tabs group's enable; FUN_180190d80 does not touch them);
- unverified: no stored op with Full tabs in the corpora (scan 2026-11-04: the two hits are 2018 records this reader does not
  parse). Scenes rt_tabs_full / rt_tabs_full_fp.

**Ported 2026-11-04 (4): the TP_ID 30 write** (mcam_saveops.c): a new op's tabs (Manual or Automatic) are saved as Mastercam's
save_chain saves them (BRACKET4 op 15 as the reference): after the chain header and before its entities, one TP kind 30 record
per tab: +0x6c / +0x80 the entity the tab is on, +0x88 the op, +0x98 chain number, +0x9c the entity's index in the chain as
written, +0xa0 the chain header's eptr, from +0x98 the tab_list_rec of FUN_18021ae10 (+0xa8 point, +0xc8 t from the entity's
start, +0xd0 width, +0xd8 thickness, +0xe0 Full, +0xe4 Ramp, +0xe8 op +0x1338, +0xf8 ramp angle); the op's TP count +0xf0
includes them; Automatic 0x1381 is the op's (it was always written on before, so Manual tabs came back as automatic ones).
Scene tb_manual_rt: place manual tabs, export .mcx-9, reopen, regenerate - the NC equals tb_manual's.

**Ported 2026-11-04 (5): square-point tabs** (cam2d.c ct_square_tabs; make_geo_tabs @1802248a0 with the calls' arguments from the
disassembly, mccore.dll 0x180224a30-0x180224ea8): per chain every point entity is tested by FUN_180222000 (the chain's entities in
order, the first with 3D distance <= chain_tol and t in [-tol, 1 + tol]); unless Midpoint (0x139a = 1) FUN_1802256c0 moves it by half
the tab width, forward for Start (0) / back for End (2), reversed entities the other way along t; past the entity's end the rest
(W - the distance walked, the FULL width as coded) is walked over the next entities (wrapping; an entity <= tol ends the walk) and
the tab lands (W / L) / 2 from the near end of the entity reached (the rest is used only by the spline branch, not ported: refused).
A chain with points gets exactly those tabs, one without keeps its stored ones; add_auto_tabs runs after it when Automatic is on
(bnci_ctour l.343-349). The 25 user ops that were refused (buckle-v6..v12; square point on, no point on their chains) now generate
with no tabs, as Mastercam's NCI has none: p95 0.000-0.075 / 0.100.

**Chain order (2026-11-04):** an op's chains are taken in chain-number order (TP header +0x98), not the records' file order: mccore
tp_get_chains walks the op's association list; in the user parts 24 of 93 multi-chain ops store their chains out of number order,
and their NCI follows the numbers in 16 (file order 2, the rest Dynamic / pocket regions). buckle-v6/v7/v8 op 18 0.167 -> 0.057.

**Accuracy pass 2026-11-04** (oracle at 0.01 mm sampling, `ORACLE_STEP=0.01`: 0.1 mm sampling alone reads up to ~0.05):
the paths matched to ~0.005; the deviations were at the plunge, i.e. the start point of closed contours.
- At a corner that is not rolled (concave, or convex left sharp: roll none, or "sharp" under 45 deg) the contour starts where the
  offsets of the chain's last and first entities meet (cam2d.c concave_start, exact line / circle carriers): Mastercam offsets the
  chain entity by entity, so its loop starts at that point; our Clipper loop draws the corner with chords. buckle-v6 op 28 0.075 ->
  0.007. The old chord-based mitre shift (d tan(turn / 2), right for two lines) fired on smooth curves (the chords of a full
  circle make a small turn): NAKTO op 1 / buckle-v9 op 40 / test-cube 0.06 -> 0.006; it is now used only without the part.
- The corner's last entity skips lines that make no contour record (chain_to_ctour_array_break: a line within the contour tol
  record 0x1228 of the last record in x and y; buckle-v11 op 18: a 0.003 mm closing line).
- Result: 146 / 149 contour ops within 0.05 (was 128), median 0.005, no op worse. Left: BRACKET4 op 7 / thread-test op 3 (0.105)
  and BRACKET4 op 9 (0.049) - the lead gouge check, below.
- Lead gouge check ported (cam2d.c lead_gouge_side, 2026-11-04): each side on its own; a piece is cut where it comes back
  within dVar6 of the path (the multipass finish spacing, else 1 % of D - BRACKET4 op 2 / op 9: Mastercam's cut lead ends 0.03 /
  0.049 before the path), counted once the lead has left that band; the arc cut drops the line; a piece whose middle is not in the
  machinable area (ours: on the free side, or on the path within nc_tol) is dropped - the arc drops the side. thread-test op 3
  (no leads), BRACKET4 op 2 / 7 / 9 to 0.007-0.010. All 149 contour ops within 0.05 (worst 0.040, median 0.005) at 0.01 sampling.
  Scenes rebaselined: rt_shorten_end (its entry now kept - it never crosses the path), op_sld_part_face (a hole's lead arc cut at
  the band). Was (decode notes): FUN_1801c46f0 (l.313097) -> FUN_1801c3d70 (l.312726-313097): the lead pieces are
  intersected with the offset bounds and trimmed at the crossing nearest the path (FUN_1801c1b60, trim_gt_to_pt), pieces whose
  middle / end lie outside the machinable area flagged; one piece flagged -> the side has none, two -> the arc flagged drops both,
  else the line flagged drops the line; a side still crossing the bounds grown by 1 % of D (op +0x2e8 x 0.01, or the multipass
  spacing) is dropped; only an error of FUN_1801c3d70 leaves without leads (LAB_180052fe1). Ours meanwhile: the measured fit
  (BRACKET4 op 2: Mastercam's arc is trimmed to ~48 deg and its line dropped - our 1 deg shrink lands within 0.012).

**Leads by depth / keep tool down (2026-11-04):**
- "Enter on first depth cut only" / "Exit on last depth cut only" (0x928 / 0x970): the contour loop FUN_18005b340 calls the lead
  application with param_8 = (depth == 1), param_9 = (depth == count), param_10 = (pass == 1), param_11 = (pass == count)
  (mcmill.dll 0x18005ccb7-0x18005cd41); the entry only on the first depth's first pass, the exit (and the overlap, local_197) only
  on the last depth's last pass (cam2d.c lead_skip). Dialog: live with the Entry / Exit group (FUN_18016fb00 / FUN_18016fcd0).
- Keep tool down, multi passes 0x8c8 / depth cuts 0x891 (l.1685-1700): into pass >= 2 / depth >= 2, next start > nc_tol away, and
  FUN_180053e70: the straight link (+- D/2 with computer comp) crosses the chain nowhere inside -> no retract: a G1 at the depth
  just cut to the next start, then down (BLOCK op 1 / 2, X9 handbook - both 0.003 in, were 0.353 / 0.336). Pocket's are separate
  (NOT DECODED).

**Feed overrides, plunge / retract, copy buttons (2026-11-04):**
- Entry / exit "Override feed rate" (0x1358 / 0x1350, 0x1368 / 0x1360 = ctour_prm +0x130 / +0x128, +0x140 / +0x138): the writer feeds
  the records before the path's first (entry) / after the exit start at them (l.8139-8160); cam2d.c lead_in / lead_out.
- Multi Passes feed override (0x1464 / 0x1458 = ctour_prm +0x23c / +0x230): the contour loop swaps op +0x310 on finish passes (the
  pass index > the rough count, mcmill.dll 0x18005cfe7) and the writer feeds them so (param_14 == 1, l.7717 / l.8113). The spindle
  override (0x1465 / 0x1460) swaps op +0x328 the same way: refused here (a spindle change inside an op).
- Plunge after first move / Retract before last move (0x992 / 0x993): the depth copies' writer FUN_180045770 - the first entry
  record (flag 0x4000) at the approach height, plunge_move at its end (l.6880-6910); at the last record (flag 0x8000) clear_move on
  the last depth else retract_move, the record at that height (l.7287-7310). cam2d.c pass(). No stored op uses them (unverified).
- Copy buttons: the lead "->" / "<-" (25502 / 25504, FUN_180172f40 / FUN_180172d70) and the adjust "->" / "<-" (1391 / 1392,
  FUN_18015a580 / FUN_18015a4e0), found through the pages' run-time message maps; the copied members mapped to controls by the
  DDX (cui_opdlg.c lead_copy / adj_copy).
- Use entry / exit point (0x929 / 0x971, + point depth 0x92a / 0x972): the point is a point entity chained at the chain's start /
  end (chain_to_ctour_array_break's point record); not done - chaining support for it first.

**Overwrite edited tabs, Infinite look ahead (2026-11-04):**
- Each stored tab carries its own values (TP kind 30 +0xd0 width, +0xd8 thickness, +0xe0 Full, +0xe4 Ramp, +0xe8 op +0x1338, +0xf8
  ramp angle; +0x110 = the tab's "edited" flag); the tab writer FUN_180056be0 cuts each tab with its own. mccore edit_operation
  (decomp/mccore_edit_operation.c l.2463-2466, 3021-3046): for Contour (opcode 1 / 0x85), when the edit changed the tab block (op
  +0x1318, 0x78 bytes = record 0x1380..0x13f7, memcmp before / after), tabs on and Manual, every tab with +0x110 = 0 - or every tab
  with "Overwrite edited tabs" 0x13f1 on - takes the op's +0x1320 / +0x1328 / +0x1330 / +0x1334 / +0x1338 / +0x1348
  (rewrite_ent_assoc; the edited flag stays). Ported: mcam_load.c reads the per-tab values, cam_fileop.c cam_tabprm_edited (the
  change test against the stored record), mcam_saveops.c rewrites the copied tab records, cam2d.c gen_contour refuses a tab that
  would be cut with values other than the op's (cut_path has one set). Dialog 1106 live (enabled with the Tabs group; FUN_180190d80
  does not touch it). Corpus: 90 stored tabs, none edited, none different from its op. Tested on patched copies of buckle-v12
  op 19 (scenes ow_*: Overwrite on -> generates and the export carries the op's values; off -> the edited tab keeps its own and is
  refused, the unedited tab takes the new thickness).
- Infinite look ahead 0x1238 (ctour_prm +0x10): ccomp_pock_from_ctour (decomp/mccore_ccomp_pock.c) -> UseOldOffset / ccomp_pock
  (decomp/mccore_ccomp_pock2.c): with the registry's new offset, MCCECalcContourOffset gets flags 0x40027 (on) or 0x60027 (off);
  bit 0x20000 also clears 0x20 (mcce_all.c l.8695) and is tested in the offset internals (l.107991, 108092, 108131, 108934,
  114635) - NOT DECODED, and our contour offset is not MCCE's: an op with it off and a compensation side is refused (no user op).
- Square-point tabs on splines stay refused: FUN_1802256c0's spline branch uses length_ent's spline length and FUN_180222000
  t_from_pt_2d / proj_pt_on_ent's spline projection, both NOT DECODED (formats/spline_chain.md 7); no user op reaches it.

**Ramp tab moves (2026-11-04)** (cam2d.c cut_path_ramp_tabs): the ramp branch of the tab writer FUN_180056be0 (decomp/mcmill_ctour.c
l.9256-9640, the tests read from the disassembly mcmill.dll 0x180058d81-0x18005a5ad), for Partial and Full tabs (record 0x139c = 1):
- the ramp branch is taken when tab +0x4c = 1 (and param_14 != 2); a partial tab only while the pass is under its top
  (z + tol < ctour_depth + break through + thickness, l.8981), a Full one at every depth;
- R (dStack_12b60): Partial = break through + thickness (measured from ctour_depth = the chain's FINAL depth, not the pass's);
  bVar17 = Full, feed plane option 0x13b9 off and retract (op +0x121 = 0x189) or clearance (+0x111 = 0x179) on -> R = that
  height - z; other Full -> R = the feed plane (init_depth) - z. run = R / tan(tab +0x60 = 0x13b0); span = width + D;
- per entity, with t on it (t0 = the tab's start; 0 on an entity the tab carries over to, the distance walked kept in
  dStack_12b50): rise (LAB_180058f8b) - the entity's move (line / arc, feed) to t0 + run (bVar17: the feed-plane run
  (init - z) / tan, at the feed plane), at R + ctour_depth; when the span's middle comes first, to the middle at z + walked x
  R / run (the peak, bVar21); bVar17 (LAB_1800591c6): a straight rapid (gr_lin_mill 0) to the CURRENT t + run at R +
  ctour_depth, or the peak; top (LAB_180059386, skipped after a peak): to span - run at R + ctour_depth (bVar17: straight rapid);
  bVar17: a straight rapid down to span - feed-plane run at the feed plane (after a peak only when the last point is above the
  feed plane + tol); descent: the entity's move to the span's end at z;
- a move reaching the entity's end (t >= 1 - tol, the decisions in t: 1 + tol) stops there at the carried height (rising z +
  walked x R / run, falling R + ctour_depth - (walked - (span - run)) x R / run) and the next entity continues. At the final
  depth (all stored ops) those lie on the moves' lines except on bVar17's rapid rise; at a higher pass they do not (Mastercam
  quirks, ported as coded: the partial run is from the final depth, the Full top is init - (z - final));
- the ctour's entity ends (2026-11-04): taken from the chain (cam2d.c rt_chain_ends: a path vertex at an entity's end normal, or
  on the perpendiculars of two entities - a concave corner; a rolled corner's chord vertices are neither); without the chain the
  profile is worked out with every path vertex an entity end and with none, refused where they differ.
- a tab over the path's start (2026-11-04): two pieces as in the NCI of buckle-v6/7/8 op 16, v11/v12 op 19 - the tab carried over
  the path's end, and the pass starting in the tab (the writer's record-1 case, l.9786-9817: the plunge to the tab's top, no rise,
  top, descent) with span H + the tab point's signed distance from the chain's start along the chain. Full tabs over the start
  stay refused. Tabs are taken from the chain they lie on only (buckle-v10 op 13).
- Also refused: overlapping spans, a tab finishing just after carrying over (LAB_180059a04 keeps the tab
  in carry mode), an angle outside (0, 90).
- checked: the contour oracle is identical for all 149 stored ops (the 90 stored partial-ramp tabs, 39 ops, of BRACKET4 / bracket4-dashed /
  NAKTO / buckle-v6..v12 at 45 deg; the measured ramps before gave the same moves at the final depth). Full + Ramp: no stored
  op (unverified against Mastercam); scenes rt_tabs_full_ramp / _fp / _30.

**Tab Cutoff (2026-11-04)** (page 3022 HSTTabSettingsPage; cam2d.c cut_tab_cutoff / ct_cutoff, cui_opdlg.c K(3022, ..)):
- record 0x13f2 (ctour_prm +0x1ca): 0 off, 1 After all contour chains, 2 After each contour chain, 3 Separate operation; 0x13f4 (+0x1cc)
  Cut tabs with first finish pass. Page: MapToDlg FUN_180191570 (group = mode != 0, enabled with 0x1380; radio index = mode - 1),
  UpdateLocalOp FUN_180191660 (off -> 0, on -> index + 1), IsPageDisabled FUN_180191550 (mode == 0), 1150 enabled by FUN_1801918c0 when
  the radio member is 2 and the group checked - the member is the index at MapToDlg but 1 / 2 / 3 after a radio click (handlers
  FUN_180191710 / 180191740 / 1801917b0): on open "Separate" enables it, after a click "After each" (ported as such).
- engine (bnci FUN_18005b340): mode 2 after each chain's passes (l.1955-2010, while local_125d): retract_move at the path's end, then
  the tab writer FUN_180056be0 with param_14 = 2 (mcmill.dll 0x18005d859); mode 1 after all chains (l.2025-2582: every chain made
  again, the writer with 2 at 0x18005ed49); mode 3: the op of opcode 0x82 the writer also serves - making it NOT DECODED, refused.
  The path is local_100 after the passes = the last pass at the last depth (with control compensation, bVar22 = comp type 1 and
  D > tol, the chain itself: refused). 0x13f4 (l.1705-1723): mode 2, Multi Passes with rough (0x842 > 0, 0x848 > tol) or finish
  (0x850 > 0, 0x858 > tol) passes: in the finish passes (no rough pass left, finish count below the op's) the tab list is freed and
  local_125d = 0 - the finish passes cut through the tabs and no cutoff follows for this or any later chain.
- the writer with param_14 = 2 (bVar50, l.9081-9220): every tab (no Partial / depth test), only the spans: at the start a rapid at
  dStack_12af0 (retract 0x189 / clearance 0x179 / feed plane = tab_rise_z) then down at the feed, or with leads (op +0x888 and +0x890
  or +0x8d8) FUN_18005ab70 (as at a Full tab's end); the span at the cut depth; at the end retract_move, or with the exit (+0x888 and
  +0x8d8) FUN_18005afc0 (0x18005afc0, read 2026-11-04: the lead application on [the entity at t - 0.06 forced to a line, the end] with
  FUN_18005ab70's flags 0x7fff / 1 / (0x180303310 == 1) / 1 / 1 / 0, the exit pieces at the depth, retract_move from the last; no piece:
  no retract). Ours: the exit direction only where the tab ends on the offset of a chain line (else refused); by depth with several
  chains refused (the sort FUN_1800447e0 not read for it); spans over the path's start or overlapping refused.
- scenes rt_tabcut_each / _all / _sep / _ffp; no stored op uses Tab Cutoff (scan 2026-11-04) - unverified against Mastercam.

**Ramp contour (2026-11-04)** (contour type 4; cam2d.c ct_ramp_chain, cui_opdlg.c K(2997, ..)):
- dialog: the type combo's Ramp (mcmillres 1240) shows the child page 2997 (FUN_18015ae90, 0xbb5; DDX FUN_18015b000, MapToDlg
  FUN_18015b220, MapToOp FUN_18015b2c0): record 0x12c0 ramp motion (radio 2068 / 2069 / 2070 = 0 Angle / 1 Depth / 2 Plunge), 0x12c8
  ramp angle (deg, min 0), 0x12d0 ramp depth (min 0), 0x12d8 one way ramping for open contours, 0x12d9 linearize helixes, 0x12e0 its
  tolerance (min tol), 0x12e8 make pass at final depth (dialog offset - 0x2088 = record). No enable rule in the page's methods.
- engine: bnci -> FUN_1801fe810 -> FUN_1801ff170 (decomp/mcmill_all.c l.350715): per chain the offset (as the 2D contour) and laps of
  create_ramped_ctour FUN_1801fdcb0 (l.349922, Rmpctour.cpp) until state 5, each lap written by FUN_1801fe8d0 (plunge-rate feed to the
  lap's first point, then lines / helical arcs at the feed). The lap state machine is in the ct_ramp_chain comment (slope tan(angle)
  or depth / perimeter; down from the top of stock to ctour_depth; then the rest of perimeter + overlap 0x988 + run at the bottom;
  Depth without the final pass stops at the end of the lap that reached the bottom).
- leads (2026-11-04): add_lead_in_out FUN_1801fcc30 (l.349035-349410), Ramp and Oscillate alike (call sites mcmill.dll 0x1801ffcfb /
  0x180200183 / 0x1801ab299 / 0x1801ab6b8: param_5 = side byte, param_6 = entry op +0x888 && +0x890, param_7 = exit +0x888 && +0x8d8).
  The pieces are FUN_1801c24a0's (as the 2D contour), tested > 1e-12 (not nc_tol): entry line +0x898, arc +0x8a8 / +0x8b0; exit
  line +0x8e0, arc +0x8f0 / +0x8f8. z: entry pieces = next record + height, exit = previous + height. FUN_1801fe8d0 feeds to record
  1 (the lead start) at the plunge rate, the pieces at the lead feed overrides (ctour_prm +0x130 / +0x140). Gouge check (op +0x929):
  FUN_1801c46f0(NULL -> the pass path, the path offset by +0x858 or 1 % D (FUN_1801fe310), closed chains only) = lead_gouge_side.
  Closed: entry on the first lap, exit on the lap ending in state 5; open (Oscillate): both on its one lap. No adjust start / end or
  lead overlap here (FUN_180051990 only). Ours: cam2d.c rmp_entry / rmp_exit; scenes rt_ramp_ctour_lead / rt_osc_ctour_lead.
- open chains, Plunge, One way (2026-11-04): the driver calls FUN_1801fd530 (l.349471) for Plunge (param_5 = 1: passes = max(1,
  (int)((top - bottom) / |0x12d0| + 0.999999)), z - (top - bottom) / passes per lap, a full lap at z, state 5 after the last) and
  with One way 0x12d8 while the lap is reversed (param_5 = 0: the reversed lap at the current z, state 3 -> 5); open chains alternate
  the direction every lap (byte +3), state 1 -> 3 at the bottom, a lap started and ended in 3 -> 5; the side flips per lap and leads
  go on every lap (cam2d.c ct_ramp_chain / rmp_lap_pts). Scenes rt_ramp_open_angle / _1way / _plunge / _lead, rt_ramp_plunge.
- Multi Passes (2026-11-04): the drivers' first loop (rough 0x8aa at 0x8b0, + the finish passes with "All depths" 0x1230 = 1) - each
  a whole ramp / oscillation from the top on its offset, the finish feed 0x1464 / 0x1458 once no rough pass is left, marker without
  keep down; then ("Final depth") the finish loop - closed Ramp: create_ramped_ctour state -2 / -3 (one lap at the bottom, + the
  overlap on the last), open Ramp: FUN_1801fd530 at the current z (direction and side going on), Oscillate: the whole oscillation
  again (open: reversed); keep tool down 0x8c8 via FUN_1801fe3e0 (closed Ramp: the link vs the chain offset by the finish spacing
  or 1 % D; open: always) or the flag itself (Oscillate). The link with keep down as the contour's marker (cam2d.c pass). Ours:
  refused with compensation off (the passes' side). Scenes rt_ramp_mp / _mp_kd / _mp_fin, rt_ramp_open_mp, rt_osc_mp.
- refused (not ported): (the -2 / -3 finish laps), control compensation, several offset loops. Linearize
  helixes: this program writes chords either way (the post fits arcs).
- scenes rt_ramp_ctour_angle / _depth / _depth_nofin; no stored op is a Ramp contour (unverified against Mastercam).

**Oscillate contour (2026-11-04)** (type 6; cam2d.c ct_osc_chain, cui_opdlg.c K(2998, ..)): child page 2998 HSTContourOscillatePage
(0xbb6, l.242342-242690): record 0x1408 Linear (0) / High speed (1), 0x1410 distance along contour, 0x1418 maximum depth. Engine bnci
l.1199 -> FUN_1801aa5b0 -> FUN_1801aa650 (outer loop as the Ramp contour's) -> create_linear_or_highspeed_ctour FUN_1801a9f50
(Oscillatectour.cpp): state 5 at once (one lap); z from ctour_depth, first down, by the max depth per distance, turning every
distance, entities split at the turns (rules in the ct_osc_chain comment). High speed (2026-11-04): FUN_1801a9ab0 (l.295979) after
each appended piece - the end z mapped onto (hi + lo) / 2 - cos(f pi) (hi - lo) / 2, then split every distance / 9 from its start
with the mapped z of the linear ramp there (cam2d.c osc_piece). Leads: FUN_1801fcc30 (above). Refused: Multi Passes, control
compensation. Scenes rt_osc_ctour / _lead / _hs; no stored op (unverified).

**3D / 3D chamfer types, 3D chains (2026-11-04):** the contour loop picks 3D by the chain (check_chains_3d -> ctour_3d), not by the type:
bnci_ctour l.354 only promotes 0 -> 1 for 3D chains; the loop tests the type for 4 / 6 only (l.1191-1199); FUN_180053480 (chamfer)
is type 2 or 3; FUN_180045770 l.6672 (3D types 1 / 3: the link height raised to the last point) is the one other use. So on flat
chains 1 cuts as 2D and 3 as 2D chamfer (cam_fileop.c), refused where l.6672 can act (tabs, lead heights); a chain not flat in Z
(cam2d.c cam_chain_3d, ours: entity heights within tol) is refused - the 3D contour is NOT DECODED.

**Next for Tabs:**
- make_geo_tabs: the spline branch of FUN_1802256c0 (needs length_ent / proj_pt_on_ent for splines); FUN_1802256c0 (read: moves a tab's t along the chain by a distance, forward / back over the
  entities, half-width with Start / End) with FUN_180222000 (read 2026-11-04: a point entity on a chain entity within
  chain_tol, t in [0, 1] +- tol) / FUN_180223c20 / FUN_18021b3c0; the chain's kind 7 association walk;
- Ramp tabs refused where a ramp crosses an entity end with a carried height off the move's line (the offset path would need the
  ctour entities: an arc / corner list kept through chain_offset).

**Ours before 2026-10-28** (replaced):
- cam2d.c cut_path spreads `p->tabs` tabs evenly over the tool path's own length. Mastercam measures them on the chain
  geometry with the rules above.
- The dialog binds only Number / Width / Thickness.

**Use entry / exit point, point chains (2026-11-04)** (cam2d.c gen_contour / pass; cui_opdlg.c K(3013, 25169 / 25170 / 25183 / 25184)):
- a point chain in a contour op: FUN_1800534a0 -> mccore rapid_xy_move (0x18089f250, decompiled 2026-11-04): a rapid to the point at
  the clearance height when on (op +0x111), else the retract height when on (+0x121), else the feed plane (init_depth);
- with leads and Use entry / exit point on, the point is kept for the next contour (FUN_18005b340 local_1108 / local_1178, a kept
  point first gets its rapid) and passed to FUN_180051990 (param_12 kept, param_13 the point, param_14 its depth; the arguments
  from mcmill.dll 0x18005ccb7-0x18005cd5d through the wrapper FUN_1800518b0): entry (local_196 = kept && 0x890 && 0x8c1 && (first
  depth || !0x8c0)) - record 1 = the point (z = its depth with 0x92a, else the lead start's), the lead's start a line after it,
  dropped within nc_tol at the same z; exit (bVar21 = kept && 0x8d8 && 0x909 && (last depth || !0x908)) - a line appended to the
  point (z = its depth with 0x972, else the last z). Both use the same kept point (the one chained before the contour). Used up after
  the contour's last pass and depth when 0x888 && 0x8c1; one left at the end gets its rapid.
- the page: the controls enabled with the side on and +0x11ad8 = FUN_18016fea0 != 4 (the contour type is not Ramp; OnSetActive
  l.258128); Use point depth also with its Use point checked. No stored op uses it (unverified against Mastercam); scenes
  rt_ctour_ptchain / _entrypt / _entryexitpt. Ours: refused with depth cuts by depth, Plunge after first move, Retract before last
  move or keep tool down.

**Heights between passes, ramp tabs on open chains (2026-11-04):**
- mccore set_heights (decompiled, 0x18089fa80): feed plane = incremental ? inc + top : abs (not below the min), retract = on ?
  (inc ? inc + top : abs) : feed plane, clearance = on ? (inc ? inc + top : abs) : retract. The depth copies' writer FUN_180045770
  calls set_heights(depth just cut, same) before each retract between passes (l.6589-6598): retract_move when the next pass is the
  same chain's (approached at those heights), clear_move + set_heights(depth, top) when it is another chain's; the op's end clears at
  the op's heights (l.6983-6995). cam2d.c ht_rel / pass_retract (only with incremental heights; absolute ones are unchanged), and the
  approach rises before it crosses. buckle-v12 op 13 rapids now equal the NCI (11.333 / 6.333, 9.967 / 4.967, 8.6, 12.7 / 7.7);
  contour oracle worst p95 0.040 -> 0.022 (the feed retracts to the relative feed plane), post regression 0/16.
- ramp tabs on open chains: the tab writer walks the ctour entities whatever the chain (buckle-v12 op 13 chain 1, NCI); a tab over
  an open chain's end stays refused (not seen). Other tab kinds on open chains: still not cut (unverified).


## 3. "Full contour options" pass (2026-11-07, branch worktree-agent-ac4378a7719d5f4b0)

User 2026-11-07: "lets get the full contour options tool path working". Corpus scan first (every .mcam / .mcx-* under
<user-parts> and <mastercam-samples>, 1381 parts, contour opcodes 1 / 0x82): the options still missing are
used by almost no stored op - Wear compensation 12 ops (cut already: computer path, the codes posted only with MCAM_CUTTER_COMP), 3D type
2 ops (TOOL AXIS CONTROL sample: computer compensation + leads on a rotary tool plane), Infinite look ahead off 4 ops; none with Multi
Passes spindle override, Subprogram, Tab Cutoff, opcode 0x82 or Remachining. So everything below is ported from the decompile only.

**2D chamfer with Control / Wear / Reverse wear (done).** FUN_18005b340 (decomp/mcmill_ctour.c l.1643-1655): the pass offset is
(cc_in_computer ? D/2 : 0) + dVar39, plus the chamfer's dVar39 when FUN_180053480 (type 2 / 3), dVar39 = tip offset x tan(half angle) +
tip diameter / 2 - D/2 (l.1101-1115) = chamfer_geometry's R - D/2. Wear / Reverse wear keep R (computer path + G41 / G42), Control cuts at
R - D/2 (the path lies on the other side of the chain; the control adds the radius back); the cc codes as the 2D contour (pass_cc_tag).
Optimize cutter comp in control 2144 is live for 2D chamfer too (FUN_180158ac0: Control and type 2D / 2D chamfer / Ramp / Remachining).
cam2d.c gen_contour, cui_opdlg.c pf_cc_ok. Behind MCAM_CUTTER_COMP=1 as the 2D contour (user decisions Q29 / B7-4); the LinuxCNC post
keeps refusing Wear / Reverse wear (Q29). Scenes rt_comp_chamfer / _wear / _opt. (Not touched: 2D chamfer with compensation Off cuts on
the chain here, where FUN_18005b340 l.1643-1655 would still add dVar39 - no stored op has it, left as it was.)

**Multi Passes spindle speed override (done: decoded to write nothing).** The contour loop swaps op +0x328 for the finish passes
(FUN_18005b340 l.1744-1748, restored l.1843-1845; the depth writer FUN_180045770 l.6968-6973 / 6193-6197). In the pass writer
FUN_180056be0 op +0x328 / the override 0x13f8 is read only into iStack_12ae4 (l.8122-8127), and that is written (bnci_wr_4, l.9779-9781)
only when a chain point carries its own speed change (record flag 0x20, l.8551-8554 / 8909-8912 - "change at point" data, not made
here). The motion writers gr_lin_mill / ct_lin_mill / ll_lin_move (mccore, decompiled 2026-11-07 into
~/mastercam-re/work/ctour100/mc_grlin.c / mc_ll.c) carry no speed, bnci_wr_ctour_marker (mc_marker.c: marker -96 with the chain / pass /
depth numbers and the finish flag) neither, and bnci_to_nci skips the markers (formats/nci_writer.md). The only bnci_wr_4 callers in
mcmill are the circle (FUN_180042070) and the thread mill (FUN_180236810). So Mastercam 2017 writes no spindle change for a contour's
finish passes: the refusal is gone, 2183 / 1415 are live (1415 iff 2183, OnSsOverrideCb 0x180178060) and saved. Scene rt_mp_spin
(export + reopen keeps 5000; the NC has the op's S only).

**Ramp / Oscillate with Control / Wear / Reverse wear (done).** The drivers offset by (cc_in_computer ? D/2 : 0) + stock (Ramp
FUN_1801ff170 l.350841-350848, Oscillate FUN_1801aa650 the same) - Control on the chain, as gen_contour's R = 0 already gave. The lap
writer FUN_1801fe8d0 (l.350524-350535) puts cc_control_gcode on record 2 when its "first lap" argument is set and 140 on the lap's last
record when its "last lap" argument is; Ghidra drops those stack arguments, so they were read from the calls (objdump of
~/mastercam-re/files/mcmill.dll, ~/mastercam-re/work/ctour100/ramp_drv.s / osc_drv.s): Ramp 0x1801ffd65 (rsp+0x40 = r13b, rsp+0x48 =
0x71(rsp)) and 0x18020027a (0x70(rsp) / 0x71(rsp)): first = set before each pass, cleared after a closed chain's lap (0x1801ffd98,
0x1802002bb), set on every lap of an open chain; last = set when the lap ends in state 5 (closed) or on every lap (open, 0x1801ffc91-
0x1801ffca2, 0x180200117-0x18020012f). Oscillate 0x1801ab2f9 / 0x1801ab78d: its one lap is both (0x1801ab22f-0x1801ab243). The code is
the side's (op +0x56a, flipped by the Ramp driver per open lap) - so an open chain's laps alternate G41 / G42 with a G40 at each lap's
end. cam2d.c rmp_cc_tag (record 2 = the first move after the plunge-rate feed to record 1, the lead line's repeated start skipped as
ct_lin_mill drops it). The refusal "control compensation is not ported" is gone. Scenes rt_ramp_cc / rt_ramp_open_cc / rt_osc_cc
(MCAM_CUTTER_COMP=1, entry line 8 mm: LinuxCNC needs the G41 move longer than the radius). Unverified (no stored Ramp / Oscillate op).

**Tab Cutoff with Wear / Reverse wear: now refused.** The pass writer gives the cutoff pass (param_14 = 2) cc codes by record (decomp/
mcmill_ctour.c l.8164-8177: record 2, the last record, and record 3 with a wear type and the entry lead), but the cutoff writes only the
spans' moves; which written move carries them is not worked out, so such an op is refused (it was cut without codes before). Control
stays refused (bVar22: the cutoff then runs on the chain's own records local_1240, l.1970-1995). Scene rt_tabcut_wear.

**Still NOT DECODED after this pass (left greyed / refused), with what was found:**
- **Remachining (type 5).** bnci_ctour calls FUN_1800550c0 (decomp/mcmill_all.c l.58166-58800) instead of the contour loop: per chain
  boundary (chains_to_pock_bounds) and depth, ccomp_pock by D/2, then the rest-material pixel maps - FUN_180053a70 / FUN_180054f70
  (the stock from the previous ops or a roughing tool, ctour_prm +0x22 / +0x28 / +0x50), pbmap_diff, pbmap_to_pbmap_bnds,
  verify_pbmap_bounds, filter_pbmap_bnds, pbmap_bnds_to_pock_bounds, then per pass ccomp_pock + pock_trim_toolpath +
  remach_merge_adj_segs + add_remachine_bound and find_remachine_group to write the pieces (FUN_180056be0). It needs mccore's pbmap
  engine (~4000 decompiled lines, decomp_tspiral_pbmap.c, not analysed - formats/pocket_engine_b6.md) and the rest-material source: not
  ported. Refused as before.
- **3D contour (3D chains).** check_chains_3d sets ctour_3d for a chain not flat in the tool plane; bnci_ctour (decomp/mcmill_ctour.c
  l.413-421) sends a 3D chain with incremental depth (op +0x140 = record 0x1a8), compensation Off, no leads and no wall stock to
  FUN_18005ef90 (decomp/mcmill_all.c l.63754-64210: the chain's entities followed in 3D with the depth offset 0x820 + 0x138 added in the
  tool view, lines / arcs in their own plane (FUN_18005fee0 otherwise) / splines (FUN_1800601f0), one copy per depth via
  adjust_depths_3d (mccore, decompiled: *z -= step; adjust_depths), NCI 1018 / 1019 for a subprogram, then FUN_180045770); every other
  3D case goes through FUN_18005b340 with ctour_3d, where FUN_180051990 calls mccore connect_3d_moves (l.56159-56163, with Max. depth
  variance ctour_prm +0x18 = record 0x1240) after the 2D ccomp. connect_3d_moves and the depth writer's 3D links are not decompiled /
  read, and no stored op exercises the FUN_18005ef90 case (the corpus' two 3D ops use computer compensation + leads on a rotary tool
  plane). Refused as before; Max. depth variance stays greyed (its decoded enable: 3D contours only).
- **Infinite look ahead off.** Unchanged: MCCECalcContourOffset's flag 0x20000 (mcce_all.c l.8695, 107991, 108092, 108131, 108934,
  114635) is NOT DECODED and the contour offset here is not MCCE's. 4 corpus ops (sample parts) have it off, none of the user's.
- **Subprogram + Absolute / Incremental (Depth Cuts).** Engine side read: subpgm_set (op_is_subpgm), set_n1018 (op, op +0x83b = record
  0x8a3 absolute / incremental, n), bnci_wr_1018 / 1019 (NCI records 1018 = 0x3fa with the 0xa8-byte nci_mill_1018 block, 1019 = 0x3fb)
  around each pass (FUN_18005b340 l.933-934, 1584-1601, 2204-2224; the writer l.8958-8960 / 9742-9744) - mccore decompiled 2026-11-07
  into ~/mastercam-re/work/ctour100/mc_marker.c. Not read: how FUN_180045770 forms the depth copies when a subprogram is on, and the
  post's subprogram output (Generic Fanuc 3X Mill.pst). The controls now carry a NOT DECODED reason (cui_opdlg.c pf_engine_nd).
  Read 2026-11-08, still NOT PORTED (controls stay greyed): FUN_180045770 (l.5908-7025) has no subprogram code at all, so the depth copies have no "sub form" of their own. The brackets are the pass
  writer's (FUN_180056be0): 1018 before record 2, or the entry lead's first record when the lead is on (op +0x888, +0x890, +0x8c0; l.7870-7877, written l.8958-8960); 1019 after the last record, or
  the exit lead's (+0x8d8, +0x908; l.7878-7882, written l.9742-9744). The motion is unchanged. The block's +0xc = depth number per chain (l.1186, 1585, 2139, 2151, 2205), +0x10 = the pass number
  (l.1601, 2224). Left: the MP engine does not act on 1018 / 1019 (viewer/mp only has the variable table), the stock post's side is psub_call_s$ / psub_st_s$ / psub_end_s$ (generic_fanuc_3x_mill.pst
  l.1384-1398: M98 + sub number + 1000, O header, M99, written to a sub file), and viewer/nc_post.c (LinuxCNC) has no subprogram form: **needs a user decision** (M98 / M99 as the stock post, or
  LinuxCNC o-word subroutines, or expanded moves). Corpus: 0 of 194 Contour-family ops have Subprogram on; 2 of 1380 files hold 1018 / 1019 records, both in non-Contour ops (tien.mcam, TUTORIAL 9.mcam).
- **Tab Cutoff as a separate operation (opcode 0x82).** Read: FUN_180044170 (decomp/mcmill_all.c l.47830-48000) deletes the op's child
  ops of opcode 0x82 (CDependentOperations) and, with tabs on and mode 3, builds a new op (init_operation, a copy of the contour with
  opcode 0x82, its chains copied) that runs the same contour loop, where FUN_180056be0 is skipped for 0x82 (FUN_18005b340 l.1757) and
  only the cutoff writer runs (l.1957). Making / saving / listing a dependent op is not ported here: refused as before.
- **Home / Ref. Points (approach / retract points, home position).** Not touched: user decision Q16 (2026-10-24, "not now - only if the
  user uses them"); every user op has them off. Still greyed with their reasons (rapid_approach / FUN_1800a0d70 not decoded).
- **Smoothing (Arc Filter / Tolerance 10382-10401).** Located: BNCISection::PostProcess (decomp/mccore_filter.c l.3588-3601) runs
  bnci_point_distribution_op when the op's flag +0xcd0 is set; bnci_point_distribution (mccore 0x1809450f0, decompiled into
  ~/mastercam-re/work/ctour100/mc_ptdist.c: srand(0x1786), the NCI section re-read, records collected by FUN_180945d50 and flushed by
  FUN_18094adc0) hands the points to PointDistribution_3D FUN_18093bcf0 (CFilter.cpp; segment length, the four option bytes +0xca9..
  +0xcac, the arc ratio; mc_ptdist2.c, 45 functions / ~2900 lines) - not ported.
- **Tabs leftovers.** Unchanged: a Full tab over the path's start (the writer's record-1 branch is read - l.9786-9817: a Full tab sets
  dStack_12af0 = init_depth and writes no record 1 - but how FUN_180045770 then approaches the pass is not); ramp tabs whose carried
  height leaves the move's line at an entity end; square-point tabs on splines (length_ent / proj_pt_on_ent for splines).
- **Coolant after Flood.** Unchanged (post side done 2026-11-04 behind MCAM_COOLANT, user decision Q11 - the combos follow that switch).
- **Pages Toolpath Type / Holder / Axis Control / Rotary Axis Control.** Not touched (lowest priority).

**Verification (2026-11-07).** Contour oracle (`ORACLE_FILEOP=1 ORACLE_STEP=0.01 tools/oracle.py <user-parts>/*.mcx-9 --type
contour`, fresh cam_cli): 149 ops, p95 median 0.005, worst 0.022, ok 149/149 - byte-identical to the run before this pass. Post
regression (postreg.sh): 0/16. Op scenes (op_compare.sh, 270 scenes / 490 files): the only differences are the intended ones (re-blessed:
the spindle override and Subprogram labels, the new scenes) and pre-existing ones not from this work, left unblessed: 52 saved projects
(*.mcamui) whose goldens predate the "PROJECT-FORMAT 2" line (PJ-1, 2026-11-05) and gen_blend / gen_sfradial / gen_srplunge (msg
"Translate" vs ""), as on the branch before.

**Remachining decode (2026-11-08):** driver, page 2999, mccore helpers and pbmap engine decoded, not ported (still refused) - formats/contour_remachining.md.
2026-11-08: 3D contour (FUN_18005ef90 case), Tab Cutoff op 0x82, tab edge cases - see formats/contour_3d_tabcutoff.md.
**Smoothing / Infinite look ahead (2026-11-08):** see formats/contour_smoothing.md (Smoothing ported for Contour, look-ahead off read, NOT DECODED for our offset).
