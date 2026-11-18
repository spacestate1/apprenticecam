# Contour type 5 "Remachining" - decode notes and status

**Status (2026-11-08, ported):** `viewer/cam2d_remach.c` / `.h` (pbmap, pock_bound helpers, leads) + `viewer/cam2d_remach_gen.c` (the driver FUN_1800550c0, sources 2 / 0 / 1, the pass
writer), `gen_contour` dispatches type 5 to it. Dialog: Contour type combo offers Remachining (appended after Oscillate: the combo indexes are the test scripts' / sidecars'), page 2999 shows
as the Cut Parameters subdialog (mcmill l.240679), its fields `0x124a..0x1280` read / written (cam_fileop.c, cui_opdlg.c).
Verified against the **real mccore.dll** (`tools/tests/remach_oracle.sh`: `tools/peloader --remach`, `tools/remach_cases.py`, `tools/remach_port.c`, `tools/remach_cmp.py`; mccore mapped with DllMain not
run, every other import bound to an abort stub, systypes.dll and mcce.dll loaded next to it, GetRegOffFlag / post_warning / draw_pock_bound / _MCAssert patched to no-ops, UI-only): 350 cases, 0 mismatches to 1e-9 on
init / line / arc sweeps, area, diff, copy, trace (ring by ring), filter, to-bounds, bbox, build info, sort, ccomp_pock, trim, merge, add_remachine_bound, find_remachine_group, including the whole Rest flow (ccomp cut + reach, verify, diff, trace, filter,
to-bounds, sort, ccomp pass, trim, merge, group). Refusal list and what is not checked: `## Status: refusals, not verified` at the end.

**2026-11-12 (agent R):** the lead side is now checked against the **real mcmill.dll** too (`peloader --remach` maps mcmill.dll next to mccore.dll on demand):
the whole lead builder FUN_180050de0 (pieces, leads, heights, n_entry / n_exit, DAT_1804f16fc / 1700), the element builder FUN_1801c2b30 and the gouge check
FUN_1801c46f0 -> FUN_1801c3d70 (with the real mcce::CalcSectLen / CalcSectPt): 850 cases, 0 differences (section "mcmill oracle" below). Compensation Off and
Control + tapered walls + gouge check are ported (no longer refused). The corpus still has **no** type 5 op (scan below), so the driver itself has no Mastercam output to compare.

Pre-port notes (the decode), kept as written:

- Read the driver FUN_1800550c0 end to end. The garbled arguments of its rest-material, lead and writer calls were recovered from the
  disassembly.
- Decoded the child page (dialog 2999 HSTContourRemachPage) field by field; its rows are in formats/opdlg_fields.tsv, dialog 2999.
  They are documentation rows only: 2999 is not in tools/gen_opdlg_fields.py PAGES, and the page is not emitted.
- Decompiled every mccore function the driver needs (none had been decompiled before) and wrote their algorithms down (sections 3-4):
  - ~/mastercam-re/work/ctour100/remach_core.c: 15 functions.
  - remach_callees.c: 34 functions (those 15 plus their depth-1 callees).
  - remach_helpers.c: 20 functions (pbmap helpers and geometry helpers).
  - remach_drv.s: objdump of FUN_1800550c0, 0x1800550c0-0x180056a00.
  - The pbmap engine itself was already dumped in decomp/pocket_b6/decomp_tspiral_pbmap.c.
  - Ghidra ran on a private copy of the mccore project, ~/mastercam-re/work/remach_gh (2 GB heap), with
    tools/ghidra_scripts/DumpDecomp.java. Regenerate with `DumpDecomp.java <out.c> '^(name|...)$' 200 <depth>`.
- **Oracle: none.** The 2026-11-07 corpus scan (contour_100_plan.md 3: 1381 parts) found no contour op of type 5, so a port can only be
  checked step by step against the decompile.

Citations used below:

| prefix | file |
|---|---|
| mm | decomp/mcmill_all.c |
| drv | remach_drv.s addresses |
| core | remach_core.c |
| cal | remach_callees.c |
| hlp | remach_helpers.c |
| pb | decomp/pocket_b6/decomp_tspiral_pbmap.c |

## 1. Parameters (ctour_prm = op +0x11c0 = record 0x1228; dialog 2999, the Cut Parameters child shown for type 5)

The page is HSTContourRemachPage (class name FUN_18015bb30). Its handlers (mm l.243226-243700):
- DDX FUN_18015ba30;
- MapToDlg FUN_18015bc90;
- MapToOp FUN_18015bda0 / FUN_18015c370;
- enables FUN_18015c3a0.

The template (exports/mcmillres_dialogs.tsv, dialog 2999) has one group, "Compute remaining stock from:".

| control | record | ctour_prm | meaning | defaults MM / Inch (contour op of Mill_*.mcam-defaults) |
|---|---|---|---|---|
| radios 1953 / 1954 / 2060 | 0x124a s16 | +0x22 | rest source: 0 All previous operations, 1 The previous operation, 2 Roughing tool diameter (bitmap 3099: 0x6542 / 0x6540 / 0x6541) | 1 / 1 |
| 1958 (static 1956) | 0x1250 f64 | +0x28 | roughing tool diameter. Enabled iff source 2. MapToDlg shows 25 (metric) / 1 (inch) when the stored value < nc_tol (l.243349-243358) | 25 / 1 |
| 1984 | 0x1258 f64 | +0x30 | clearance as % of D (record 0x350) | 50 / 50 |
| 1716 | 0x1260 f64 | +0x38 | clearance distance. MapToDlg recomputes it = % x 0.01 x D; kill focus converts both ways (FUN_18015be50 / FUN_18015bf10) | |
| 2061 | 0x1270 f64 | +0x48 | remachining tolerance as % of D | 5 / 5 |
| 2062 | 0x1278 f64 | +0x50 | remachining tolerance distance (= % x D x 0.01; FUN_18015bfd0 / FUN_18015c090). Used as the pbmap cell size | 1 / 0.025 |
| 1759 | 0x1280 u8 | +0x58 | Display stock: draws the maps and waits for space; no toolpath effect | 0 / 0 |

FUN_1800550c0 also reads these fields, which are not on page 2999:
- **ctour_prm +0x40** (record 0x1268). No page that writes it was found; it is 0 in both default ops. When it is set, finish passes
  are not trimmed (section 2 step 6).
- **ctour_prm +8** (record 0x1230). It is 1 in both defaults. It works as "write the finish passes at every depth" (step 7).
- **ctour_prm +0x10**, Infinite look ahead: the ccomp_pock flag.
- **ctour_prm +0xe8**, the taper angle, through FUN_1800569a0.

Parent page 2938 (FUN_180158ac0): Optimize cutter comp in control 2144 is enabled for Remachining, and Internal corner rounding radius
1210 is disabled for it.

## 2. The driver FUN_1800550c0 (mm l.58166-58845)

bnci_ctour calls this driver instead of the contour loop when the type is 5.

**Locals.** The frame base is rbp = entry rsp - 0x238, so a decompile local_X sits at [rbp + 0x238 - X].

**Op fields.**
- op +0x810 / +0x818 are the finish depth-cut count and step.
- op +0x808 is the rough maximum step.
- calc_depth_cuts (decomp/mccore_depths.c l.256-300) returns the total number of depths, the rough count and the rough step.

### Step 1: Setup (l.58290-58342)
- Side DAT_1804f16f8 = op +0x56a ? 'R' : 'L'.
- minfil = set_min_fil_angle(op +0x56b).
- bVar7 = Control compensation (op +0x568 == 1) and D (op +0x2e8) > tol.
- leads = op +0x888 && (op +0x890 || op +0x8d8).
- chains_to_pock_bounds(chains, 0, 0, 1, ctour_prm +0, &bounds, 0) makes one bound per chain, in chain order.
- The NCI goes to CNTRNCI.TMP, as in the contour loop.
- dVar1 = op +0x858 (finish spacing) when Multi Passes (op +0x840) is on and that value is > nc_tol; otherwise D x 0.01.

### Step 2: Depths (l.58325-58380, drv 0x1800554b0-0x1800555df)
Per chain:
- set_depths gives the final depth.
- Without taper (FUN_180056a70 false):
  - Only one depth is processed: local_2b4 is forced to 1, and n_actual_depth_cuts gets the real count.
  - After the chain, FUN_1800451f0(n, n_rough, rough step, file pos) copies the NCI to the other depths (l.58800-58806).
  - With depth cuts off, n_actual is the previous chain's value (0 for the first chain): a quirk that feeds the marker.
- With taper, every depth is looped.
- The first depth is final + (finish count or count - 1) x op +0x818 + (rough count - 1 or 0) x rough step (drv 0x18005558f-0x1800555d5).
- Each depth ends with adjust_depths(op, &depth, rough step, op +0x818, depth# < n_rough, depth# == n_depths) (drv 0x180056409).
- FUN_180056a70 (mm l.58991) is true only when op +0x800 is set. For contour opcodes (1, 0x85, 0x87, 0x33-0x35) it also needs
  op +0x12a2 and op +0x12a8 > tol (tapered walls).
- Everything below is redone for each depth.

### Step 3: Boundary and map range
- With taper, the bound is offset by FUN_1800569a0(ctour_prm, depth, ctour_prm +0xe8), which already includes op +0x278 (wall stock),
  using ccomp_pock(..., minfil, 1, 0, 0). Then build_pock_bounds_info is run.
- Y range: ymin = min of bound +0x28, ymax = max of bound +0x38 (drv 0x1800556c1-0x1800556e7).
- Both ends are widened by D x 2.142857142857143 (15/7, constant at 0x180310cf0; drv 0x1800556f0-0x180055709).

### Step 4: Rest material (drv 0x18005570d-0x180055897; arguments from the registers and stack)
Values used: cell = ctour_prm +0x50, R = D/2, stock = op +0x278, and Rr = 0 (xmm14 is zeroed at drv 0x180055199 and only set for
source 2).

**Source 0 / 1.** FUN_180053a70(source, ymin, ymax, cell, depth, &cut) (mm l.57212-57488):
1. init_pbmap(ymin, ymax, cell, cut).
2. Source 0: every op in the main op list before the current op id goes through verify_2d_operation(op id, depth, 0.0, cut, 0, 0.0).
3. Source 1: only the last op before the current one goes through it.
4. An op with Control compensation (op +0x568 == 1) gives post_warning 0x519 and returns -1, which refuses the operation.

**Source 2.** Rr = ctour_prm +0x28 x 0.5. Then FUN_180054f70(bound, Rr, stock, side, bVar7 ? 0.785398 : minfil, 1, ymin, ymax, cell,
&cut) (mm l.58134-58160):
1. init_pbmap.
2. For each bound: ccomp_pock(bound, Rr + stock, side, angle, 1, 0, 0), then verify_pock_bounds(each offset loop, Rr, cut).
3. The result is the band swept by a roughing tool of that diameter running the same contour.

**Count and step** (drv 0x1800557c3-0x18005583d):
- If |R| > 1e-12: n = trunc((Rr - (stock + R)) / R), floor or ceil towards zero; count = n + 2; step = (Rr - (stock + R)) / (count - 1).
- Otherwise: count = 1, step = 0.
- For source 0 / 1, count is 1.

**Reach map.** FUN_180054db0(bound, R, stock, side, bVar7 ? pi : minfil, 1, count, step, ymin, ymax, cell, &reach) (mm l.58094-58128):
1. init_pbmap.
2. For each bound and k = 0..count-1: ccomp_pock(bound, R + stock + k x step, ...), then verify_pock_bounds(.., R, reach).

**Then:**
- With Display stock on, cut and reach are drawn (messages 0x500 / 0x501).
- pbmap_diff(reach, cut, cell) leaves the rest material this tool can remove (l.58507).

### Step 5: Rest regions (l.58521-58534)
1. copy_pbmap(rest2 <- reach).
2. pbmap_to_pbmap_bnds(reach, R, 0, &bnds).
3. verify_pbmap_bounds(bnds, R + ctour_prm +0x38, reach, rest2): every traced boundary is swept with R + clearance into rest2, which
   grows the rest areas by the clearance.
4. pbmap_to_pbmap_bnds(rest2, R, 0, &bnds).
5. filter_pbmap_bnds(rest2, bnds, cell x 0.5).
6. pbmap_bnds_to_pock_bounds(rest2, bnds, &restb). The result has line entities only.
7. sort_pock_bounds(0, 0, 1, tol, &restb), then pock_bounds_bbox.

### Step 6: Passes (l.58540-58635)
- Without Multi Passes there are 0 rough passes and 1 finish pass. With Multi Passes (op +0x840) there are op +0x842 rough and
  op +0x850 finish passes, rough first.
- A pass is "rough" while the rough counter is non-zero (bVar26).
- Offset = rough_left x op +0x848 + D/2 + finish_left x op +0x858, plus the stock unless tapered (drv 0x180055b38-0x180055b88).
- pass = ccomp_pock(bound, offset, side, bVar7 ? pi : minfil, bVar7 ? 1 : ctour_prm +0x10, 0, 0).
- ref = ccomp_pock(pass, dVar1, side, minfil, 1, 0, 0). It is used only as FUN_180050de0's gouge-check bound.
- **Trimmed** (the default; skipped only when ctour_prm +0x40 is set and the pass is a finish pass):
  1. pock_trim_toolpath(&pass, restb, &pieces), then remach_merge_adj_segs(&pieces).
  2. Each piece gets leads from FUN_180050de0 when leads are on and !bVar7.
  3. Each piece is added with add_remachine_bound(piece, idx, &list). idx counts down from total-1 on the first pass to 0 on the
     last: the number of passes after this one.
- **Untrimmed:** the whole pass, with +0x50 = 0. Leads are added without the !bVar7 test, so Control gets them twice. This may be a
  Mastercam quirk; check before copying it.

### Step 7: Output (l.58640-58790)
1. All list entries are marked active.
2. find_remachine_group(&cont, cur, list, &group) is called until it returns NULL; each group's entries are written in order.
3. Per entry, the marker: bnci_wr_ctour_marker(chain#, position in group (1-based), depth#, depth, top_of_stock, running piece counter,
   0, finish flag = op +0x840 && position > op +0x842, n_actual_depth_cuts, keep-down = (position > 1 && op +0x860) ||
   (n_actual > 1 && op +0x829), ok).
4. An entry is written only when one of these holds:
   - this is the last depth;
   - ctour_prm +8 is set;
   - op +0x850 <= its idx.

   So the last op +0x850 passes are written only at the last depth. op +0x850 is read even with Multi Passes off. Skipped entries
   still advance the counters.
5. With bVar7: ccomp_pock(piece, D/2, opposite side, pi, 1, 0, 0) brings the tool-centre path back to the programmed path, and the
   leads are rebuilt on it (FUN_180050de0 with no gouge bound). A NULL result ends the group.
6. DAT_1804f1708 = N + 1. pock_bound_to_ctour(piece, ctour, 1).
7. Depths are written into the 0x60-byte records' z (+0x40):
   - records 1..N get the depth;
   - entry k gets depth + entity k's +0x78;
   - exit records get depth + their +0x78.

   Record N + 1's z without an exit lead is pock_bound_to_ctour's.
8. FUN_180053e70 runs once (a gouge test of a stay-down line from the last NCI point, latched as local_2a8). It runs after the marker
   is written, so it does not change the marker; its net effect is not determined.
9. The writer FUN_180056be0 (the 2D contour's pass writer) is called with: nc_file, ctour, depth, count, 0, 0, 0, &local_28e, n_entry,
   n_exit, !(last depth && last pass) && op +0x908, depth, no tabs, finish flag, &local_138, ok.
   - DAT_1804f16fc / DAT_1804f1700 mark the first contour record and the exit start: entry / exit feeds ctour_prm +0x128 / +0x138, and
     the first move uses op +0x310.
   - Without leads they are 2 / count. With leads and Computer comp, DAT_1804f1700 keeps the value from the last FUN_180050de0 call of
     the pass loop (a quirk).
10. With bVar7, FUN_180054710 follows (display only).

### Step 8: End
- FUN_180045770, the depth writer.
- Cleanup.

### FUN_180050de0 (mm l.55322-55835, "add_remach_entry_ents"): leads on a piece
Real arguments: (piece, chain bound, gouge bound, side, unused, &out, &n_entry, &n_exit).

Which lead elements are built:

| element | built when | lead geometry fields | ramp height |
|---|---|---|---|
| entry arc | op +0x8b0 > 1e-6 and op +0x8a8 > nc_tol | +0x891 / +0x898 / +0x8a8 / +0x8b0 | op +0x8b8 |
| entry line | \|op +0x898\| > nc_tol | (as above) | op +0x8a0 |
| exit arc | op +0x8f8 > 1e-6 and op +0x8f0 > nc_tol | +0x8d9 / +0x8e0 / +0x8f0 / +0x8f8 | op +0x900 |
| exit line | \|op +0x8e0\| > nc_tol | (as above) | op +0x8e8 |

- The geometry comes from FUN_1801c2b30. The entry/exit "on" flags are not tested here.
- Output order: entry line, entry arc, the piece's records, exit arc, exit line.
- Entity +0x78 holds the heights: suffix sums for the entry, prefix sums for the exit.
- With op +0x929 set, FUN_1801c46f0 runs the gouge check; its effect is not decoded.
- Side outputs: DAT_1804f16fc = n_entry + 2, and DAT_1804f1700 = total - n_exit + 1.

## 3. mccore: the pock_bound / remachine_bound helpers

### Data layouts

**pock_bound.** FUN_1805dd4c0 (hlp l.1031-1046) allocates calloc(n x 0x88 + 0x68), all zeroed: a 0x68-byte header followed by the
entities inline.

| offset | meaning |
|---|---|
| +0 | valid |
| +1 | "opened / broken" (trim), nested (ccomp), used (sort) |
| +2 | island |
| +3 | pock_ccw |
| +4 | merge "done" |
| +8 | group id (sort) |
| +0xc | entity count |
| +0x10 | entities |
| +0x18 / +0x45 | copied properties, meaning unclear |
| +0x20..+0x38 | bbox minx miny maxx maxy (pock_bound_bbox core l.993-1056) |
| +0x48 | length |
| +0x50 | parent: the container (sort / ccomp); in trim, the rest bound that cut or contains the piece |
| +0x58 | prev |
| +0x60 | next |

**Entity (0x88 bytes).**

| offset | meaning |
|---|---|
| +0 | record index |
| +8 | type 'L' / 'A' / 'P' |
| +0x18..+0x30 | line: x0 y0 x1 y1 |
| +0x18 / +0x20 / +0x28 / +0x30 / +0x38 | arc: cx, cy, r, start, signed sweep (radians, < 0 = CW) |
| +0x40..+0x58 | entity bbox |
| +0x60..+0x80 | copied to the ctour record; +0x78 is the height added to the depth |

**remachine_bound (0x40 bytes).**

| offset | meaning |
|---|---|
| +0 | active |
| +8 | start point |
| +0x18 | end point |
| +0x28 | short pass index |
| +0x30 | deep copy of the piece |
| +0x38 | next |

### pock_trim_toolpath(&passes, rest, &out) (cal l.272-435)
1. One counter numbers every entity (+0), passes first, then rest. Each pass's +0x50 is set to 0.
2. For each rest bound R and pass P whose bboxes overlap within nc_tol, call intersect_pock_bound(P, R, 0, 500 - n, ...).
   - It tests non-'P' entity pairs with overlapping bboxes using ints_on_gt_new at nc_tol.
   - Points within tol of an earlier hit are dropped.
   - At most 500 intersections are kept in total.
3. For each intersection:
   - FUN_1805e4430 finds the piece holding that record index whose hit is nearest t = 0.5.
   - The piece's +0x50 is set to R.
   - break_pock_bound FUN_1805de250 (cal l.1302-1597) splits it with split_pock_chn at min(nc_tol, 1.1920928955078125e-7). The first
     cut of a closed loop only reopens it there (reorder_pock_bound). Later cuts, and cuts of open bounds, split it into two bounds.
     Both halves keep the record index (hlp l.970-1026).
4. Each piece that FUN_1805e5490 places inside the rest material is moved to *out. *out is built by prepending, so it ends up in
   reverse order. +0x50 is set to the containing rest bound if it is not set yet.
   - FUN_1805e5490 (cal l.1153-1178) samples entity midpoints at t = 0.5 with FUN_1805e7080. It stops when one side has 3 votes and
     keeps the piece when inside votes > outside votes.
   - FUN_1805e7080 (hlp l.1494-1527) returns 1 when the point is in an outer bound (+2 == 0; pt_in_ctour at nc_tol / tol) and in no
     island of the same group (+8). It returns the last such bound. Otherwise it returns -1.

### remach_merge_adj_segs(&pieces) (cal l.442-607)
1. Clear +4 on every piece.
2. Take the first piece A that is not done. Look at each later piece B that is not done: they match if end(A) == start(B), or
   end(B) == start(A), within nc_tol (strict).
3. On a match, make a new bound with the entities of the piece whose end matched (F) followed by those of the other (S). Only the gt
   bytes +8..+0x3f are copied, and properties come from F. Mark it done, unlink F and S, append the new bound, and restart from the
   head.
4. If A has no partner, mark A done.

A merged bound never merges again.

### add_remachine_bound and find_remachine_group
- **add_remachine_bound(piece, idx, &list)** (cal l.8-57): copies the piece into a new node and appends it at the tail
  (FUN_1805e20e0).
- **find_remachine_group(&cont, cur, list, &group)** (cal l.184-269), using FUN_1805e3d40 (cal l.88-175):
  1. The first pick is the active entry with the highest idx; ties go to the start nearest cur (when cont is set). The "selected" flag
     is not reset when a lower idx is seen (disassembly 0x1805e3e03-0x1805e3e0d); a port must keep it persistent.
  2. While the pick has the group's parent (+0x50): mark it inactive, append a shallow copy to the group, set cur = its end, and pick
     next the active entry with that parent whose start is nearest cur.
  3. Phase 3: among same-parent entries, a higher idx wins.
- FUN_1800550c0 writes each group in that order, then asks for the next group from the last end point.

### verify_2d_operation(op id, z, rad_adj, map, positioned, rad) (cal l.667-923)
- Does nothing when GetProdEnableBits() & 6. zlim = nc_tol + z.
- bnci_read_sos seeks to the op's NCI.
- If the op's byte +0x5d8 is set, every record is translated by the 3x3 at +0x5e0 times the vector at +0x628 (FUN_1805e8120). The
  meaning of these op fields is unclear.

Records:

| code | handling |
|---|---|
| -3 | stop |
| -0x60 (marker) | skip the next linear move |
| 0 (rapid) | set last point only; not swept |
| 1 | if both Z <= zlim and no marker: pbmap_verify_line(last, cur, rad) |
| 2 / 3 (plane 0 only) | constr_arc_new(last, cur, centre, CW = code 2), then pbmap_verify_arc |
| 0x51 / 100 (drill points) | a disc |
| 0x3f5 (1013 tool record) | rad = its +0x20 x 0.5, plus rad_adj when that stays >= 0 |

So each previous op is swept by its own NCI motion below the current depth, with its own tool radius. Swept is not the same as
simulated stock; rapids are not swept.

### ccomp_pock (core l.493-665)
- With UseOldOffset it calls the legacy FUN_1805d8690 (not read).
- Otherwise it calls MCCECalcContourOffset(1, dist (negated for 'R'), in, out, flags, angle, gap, nc_tol, 0.75 nc_tol). This is the
  same MCCE offset the contour uses; it is NOT DECODED, and cam2d.c uses a Clipper stand-in.
  - flags: p6 ? 0x40027 : 0x60027.
  - angle: 0.5729577951308232 deg for p5 = 0.01, 179.1 for pi, otherwise 44.99999063801583.
  - gap: 1e38 when p7, -1 when p8, otherwise chain_tol.

### Other helpers
- **pock_bound_to_ctour(pb, recs, start)** (core l.370-484):
  - Record start is an 'L' at entity 0's start.
  - Then one 0x60-byte record per 'L' / 'A' entity: +0 end point, +0x10 centre, +0x20 CW, +0x21 type, +0x28..+0x40 and +0x4c from
    entity +0x60..+0x80, +0x48 full circle, +0x49 'N'.
- **chains_to_pock_bounds** (core l.8-99): one bound per chain, through chain_to_pock_bound.
- **sort_pock_bounds** (core l.107-362):
  - nesting by IsContourNested, recorded in +0x50;
  - groups in +8;
  - islands in +2;
  - duplicates dropped with warning 0x12c8.

## 4. mccore: the pbmap engine

### Layout
**pbmap** (init_pbmap pb l.626-629, FUN_1805cd760):

| offset | field |
|---|---|
| +0 | cell |
| +8 | y0 = ymin |
| +0x10 | nrows = (int)((ymax - ymin) / cell) |
| +0x18 | row pointers (NULL until used) |

- Row i is sampled at i x cell + y0 (FUN_1805d2670), with no half-cell shift.
- A row is {count, capacity, 0x30-byte records} of sorted x endpoints; pairs (2k, 2k+1) are the intervals.
- Record fields: +0 flag, +1 used, +4 ring id, +8 x, +0x10 y, +0x18 index, +0x1c row, +0x20 prev, +0x28 next. Only
  pbmap_to_pbmap_bnds sets the last six.
- Memory is an arena of calloc'd blocks of at least 5000 bytes (FUN_1805d27c0 / FUN_1805cd6f0), freed only as a whole by
  free_pbmap_local_mem.
- pbmap_area = the sum of interval lengths x cell.

### Sweeps
**Edges** (0x38 bytes) come in two types:
- **line:** {type 1, ymin, ymax, x = a + b y} (FUN_1805cd650);
- **y-monotone arc piece:** {type 0, ymin, ymax, cx, side +-1, r^2 - cy^2, 2 cy}. FUN_1805cd2c0 splits an arc at pi/2 and 4.71238898
  (a truncated 3 pi/2), giving at most 3 pieces.

**pbmap_verify_line_ext** (pb l.2090-2186): the stadium of radius R around the segment. A zero-length segment gives a disc.

**pbmap_verify_arc_ext** (pb l.1831-2068), by |sweep|:
- |sweep| > 2 pi - 1e-6: the annulus r +- R (outer circle only when R >= r).
- |sweep| > 1.01 pi: split into two halves.
- Otherwise, when R < r: caps of radius R, plus the arc r + R, plus the arc r - R.
- Otherwise (R >= r): the caps meet at the intersection of the cap circles (aa_ints) that is farther from the arc's middle.

**verify_pock_bound(s)** (pb l.4014-4060, 2286-2315): each 'L' / 'A' entity is swept separately. The bound's inside is not filled.

### Fill FUN_1805d3340 (pb l.3450-3589)
1. Rows run from FUN_1805d2610(ymin) - 1 to FUN_1805d2610(ymax) + 1. FUN_1805d2610 maps y to a row as (y - y0) / cell, rounded half
   away from zero and clamped (hlp l.513-534).
2. Per row, FUN_1805d2bd0 (hlp l.573-699) collects the crossings:
   - an edge crosses when ymin <= y < ymax;
   - at most 200 crossings;
   - an odd count retries at y0 + k x tol and y0 alternately, 8 tries;
   - the result is sorted ascending.
3. A row with an odd count is skipped.
4. Each pair with hi - lo >= 1e-5 is merged into the row by FUN_1805cfe40 (hlp l.305-508). This is a union: an existing interval that
   overlaps, or whose ends come within the global tol, widens to cover the new one and swallows the intervals in between; otherwise
   the pair is inserted in sorted position.

### Map operations
- **pbmap_diff_ext(A, B, tol)** (pb l.1041-1215): A minus B, row by row. A leftover piece shorter than tol is dropped. The overlap test
  is strict: (a0 - b1)(a1 - b0) < 0.
- **copy_pbmap_ext** (pb l.23-111): a deep copy.
- **pbmap_to_pbmap_bnds_ext(map, dtol, inv, &bnds)** (pb l.1592-1809):
  - inv = 0 traces the filled intervals; inv = 1 traces the inner gaps.
  - Intervals of adjacent rows are matched (FUN_1805cedf0 / FUN_1805cfd00) in two passes: threshold 0, then -dtol (disassembly
    0x1805d1a36: the sign mask), so gaps smaller than dtol join.
  - Matched endpoints are linked into rings; between rows the boundary runs as straight diagonals, not a staircase. Unmatched
    intervals become 2-point rings.
  - **At most one ring is extracted per row** (disassembly 0x1805d1be3-0x1805d1c5e); a port must copy this.
  - pbmap_bnd = {1, first record, next}.
- **verify_pbmap_bounds_ext(bnds, R, src, dst)** (pb l.2337-2408): sweeps every ring segment with radius R into dst. src only has its
  used flags reset.
- **filter_pbmap_bnds(map, bnds, tol)** (pb l.339-371, FUN_1805ce6e0; hlp FUN_1805cce60 l.152-231, FUN_1805cd250 l.236-278):
  - Greedy chord simplification: from each kept node, keep the end of the longest run whose interior nodes all lie within tol of the
    chord (FUN_1805ccff0, binary search).
  - Above 0x7ffe nodes, tol is multiplied by 10 (up to 16 tries).
  - As read, the relinked ring comes out open; pbmap_bnds_to_pock_bounds adds the closing line anyway.
- **pbmap_bnds_to_pock_bounds** (pb l.895-1018): each ring of 2..0x7ffe nodes becomes a pock_bound of 'L' entities (node i to i + 1,
  plus a closing line), appended to the list.

## 5. What is left / how to port

**Still to decompile** (small, all mccore):
- FUN_1805e0b40 (nearest point on a segment: is it clamped?)
- FUN_1805e7040 (a bbox prefilter?)
- FUN_1805e2070 (is an angle on the arc?)
- arc_endpoints
- the meaning of get_ep's flag
- the side trim_gt_to_pt keeps
- ints_on_gt_new's details
- t_on_gt / eval_gt_pt (in hlp, not yet read)
- FUN_1801c2b30 / FUN_1801c46f0 (mcmill lead geometry and its gouge check)
- the internals of the pass writer FUN_180056be0 for a remachining piece (the 2D contour port already follows it on chains)

**Blocking:**
- ccomp_pock is MCCE's offset, NOT DECODED. The contour port uses a Clipper polyline stand-in (accepted for the 2D contour: oracle
  149/149 within 0.05). A remachining port would have to use the same stand-in for every ccomp_pock call: the pass offsets, the
  reach / cut sweeps, and the Control back-offset.
- Rest sources 0 / 1 need the previous operations' NCI in operation order. cam_generate makes one op at a time and has no "previous
  ops" input; stored ops' NCI and the app's generated moves (CamOp.m) would both have to be fed to a verify_2d_operation port. Source 2
  (roughing tool diameter) needs none of this.

**Suggested order:**
1. A pbmap module (viewer/cam2d_remach.c): init / sweep line / arc / fill / union / diff / copy / trace / filter / to-bounds, with a
   unit test of each against hand-worked cases from the formulas above.
2. Source 2 only: FUN_180054f70 + FUN_180054db0 + the trace, with polyline bounds.
3. pock_trim_toolpath / merge / group order on polylines.
4. Hook into gen_contour for ct_rtype == 5 && source == 2 (pieces written through the existing pass writer with the remachining leads).
5. Sources 0 / 1 (verify_2d_operation over earlier ops' moves).
6. Make the combo offer Remachining and page 2999 live.

Every step has to be verified step by step against the decompile; the corpus has no Mastercam output to compare.

## Status: refusals, not verified (2026-11-08)

Refused, with a message (2026-11-12: Control + taper + gouge check and compensation Off are ported, see the end; compensation Off is refused only when the chain records do not give the chain directions): a point or spline in the chains; a 3D chain; a chain with trimmed ends; any ccomp_pock the MCCE port refuses (several rings etc.); a Control previous op for sources 0 / 1 (post warning 0x519); sources 0 / 1 where no operation list is available.
Not checked against Mastercam (corpus has no type 5 op): the lead geometry (FUN_180050de0 + FUN_1801c2b30 ported from the decompile by hand; 2026-11-12: now checked against mcmill.dll, 0 differences), the pass writer use, `verify_2d_operation` on stored NCI and on CamOp moves (markers are not available for moves generated here: the first-feed-after-rapid plunge is not skipped; it sits above the depth limit unless the op was cut below its own plunge start; a keep-tool-down link is not skipped), sort_pock_bounds's point-bound and p1 / p2 branches (never reached: restb is line rings, p1 = NULL), arc-arc intersections (aa_ints_new, rest is lines only), l_ints inside bound_line_inters (sort overlap with line bounds: hand-derived from l_ints_tol, unverified). The mccore-side arc entities of `pock_trim_toolpath` (pass arcs against rest lines) are in the oracle.
Quirks copied: one ring per row; pbmap "selected" flag; DAT_1804f1700 from the last lead call (Computer comp); the untrimmed-pass leads doubled under Control (disassembly 0x180055c8a..: the leads call in the untrimmed branch has no !bVar7 test, confirmed); FUN_1800451f0 copies every written record to every depth (finish passes included).

## Lead In/Out gouge check (ported 2026-11-08)

`rm_add_leads` now runs FUN_1801c46f0 -> FUN_1801c3d70 (decomp/mcmill_all.c l.313097, l.312726) per side when record 0x991 is on, with its helpers: FUN_1801c37b0, FUN_1801c1b60, FUN_1801bbc70 (mcce::CalcSectLen / CalcSectPt), FUN_1801bc0d0, and from mccore pt_on_comped_side_of_open_ctour, GetNearestPockBndPt. It is a direct port, separate from `lead_gouge_side` in cam2d.c, which is the 2D contour's measured fit. The gouge bound is ccomp_pock(pass, dVar1, side, minfil, 1, 0, 0) (l.58567), built only with the check on.

Verified by hand-worked cases (`tools/tests/remach_lead_test.c`: nothing in the way, entry cut, exit cut, wrong side) and, since 2026-11-12, against mcmill.dll itself (next section). GetProdEnableBits & 0x10 (meaning NOT DECODED) is taken as off. mcce::CalcSectLen / CalcSectPt are done as a plain walk along the lead's entities (clamped t, as FUN_180149d80 / FUN_18014a290 begin).

## Clearance on the dialog's OK (2026-11-09, decision N9-8)

MapToDlg FUN_18015bc90 always derives 1716 = % x 0.01 x D (mm l.243346) and MapToOp FUN_18015bda0 stores it (record 0x1260). The app's dialog OK (cui_opdlg.c pf_apply) now does the same for type 5: without this a new op kept the default's stale distance. Decision N9-13 (2026-11-09): the same for the tolerance 0x1278 = 0x1270 % x D x 0.01 (l.243362-243363) and the roughing diameter fallback 25 / 1.

## 2026-11-12 (agent R): corpus scan, mcmill oracle, compensation Off, Control + taper + gouge

**Corpus scan.** Every contour op (opcodes 1, 0x85, 0x87, 0x33-0x35; record 0x1308) of the corpus <mastercam-samples>/net (1460 files, one 2020
file unreadable) and of the user parts <user-parts> (and the copies in <posts>): 194 contour ops, type 0 176, type 1 2, type 2 16, **type 5
none** (nor 4 / 6). `tools/oracle.py` has nothing to measure for Remachining; the only reference remains the DLLs themselves.

**mcmill oracle** (`tools/tests/remach_oracle.sh`, cases from `tools/remach_cases.py`; `tools/peloader/mccore_rem.c` vs `tools/remach_port.c`). On the first
LEADS / LEADGT / LGOUGE command mccore_rem.c maps mcmill.dll from mccore.dll's folder (DllMain not run): its MCCore.dll imports are bound to the mapped
mccore.dll (so `?cur_op@@3Uoperation@@A`, `?tol@@3NA`, ... are the same memory the script sets), MCCE.dll / SysTypes.dll as for mccore, Mastercam.dll!
GetProdEnableBits answers 0 (bit 0x10 off, as the port assumes; its meaning is still NOT DECODED), any other import aborts (none is reached).
- `LEADS`: FUN_180050de0(piece, chain, gouge bound, side, unused, &out, &n_entry, &n_exit) with the lead fields in cur_op (+0x891 / +0x898 / +0x8a0 / +0x8a8 /
  +0x8b0 / +0x8b8 entry, +0x8d9 / +0x8e0 / +0x8e8 / +0x8f0 / +0x8f8 / +0x900 exit, +0x929 gouge check) vs `rm_add_leads`: 120 cases (one- and two-line pieces,
  0-3 crossing gouge bounds, gouge on / off, arcs / lines / both / none, perpendicular lines, negative lengths, heights): 0 differences in the entities, their
  +0x78 heights, n_entry / n_exit and DAT_1804f16fc / DAT_1804f1700 (ctour_depth is 0 in the oracle: the records' +0x78 base is not exercised).
- `LEADGT`: FUN_1801c2b30(gt, ex, ln, side, out) vs `rm_lead_gt` on 320 random lines / arcs (both ends, both sides, arc / line, along / perpendicular, a zero
  length line): 0 differences.
- `LGOUGE`: FUN_1801c46f0(NULL, chain, gouge list, entry, 0, side, lead) - the call of FUN_180050de0 (disassembly 0x18005115d-0x18005117e entry,
  0x180051352-0x180051373 exit) - vs `rm_lead_gouge_side` on 320 leads of pieces of offset rectangles (CW / CCW), circles and open lines with the gouge bound
  at R + d or random crossing lines, and another parent (+0x50) on 30 %: 0 differences; 118 of the 320 leads were changed (57 dropped, 28 lost one element,
  33 trimmed), so the cut / drop paths are covered. The port's plain walk for mcce::CalcSectLen / CalcSectPt (FUN_1801bbc70) agrees with the real ones on these.

**Compensation Off** (record 0x5d0 = 4). FUN_1800550c0 has no Off test: the side is DAT_1804f16f8 = op +0x56a (record 0x5d2) ? 'R' : 'L' (l.58324-58327),
bVar7 is Control only (op +0x568 == 1, l.58328-58335), so with Off the passes are offset by D/2 to the 0x5d2 side exactly as with Computer, and the pass writer
FUN_180056be0 writes no cutter comp codes. Ported: `CamParams.ct_off_right` = record 0x5d2 (cam_fileop.c, read with the op and from a record patch),
`rm_side` takes it under Off, bVar7 is false under Off (ct_comp keeps the last non-Off type in the dialog), pass_cc stays off. 0x5d2 is relative to the chain's
direction; when the chain records do not give the directions (cam_fileop.c `side_rel` = 0) the op is refused (NOT PORTED). Under Off the dialog's direction combo
shows the record's 0x5d2, greyed (cui_opdlg.c K(2938, 1274)), which is the value used. Since 2026-11-12 (coordinator request, all contour types) the
dialog keeps the last direction set: UpdateLocalOp FUN_180157e30 (decomp/mcmill_all.c l.240808-240811) stores the type combo to op +0x568 and the direction
combo to op +0x56a (local op +0x2658 / +0x265a) whatever the type, so switching Left / Right -> Off keeps that direction in 0x5d2. cui_opdlg.c K(2938 / 3025,
1290): going to Off writes the side into record 0x5d2 (a record patch, only when it differs from the record's or a patch already holds it; the direction
combo keeps such a patch in step), and coming back from Off takes the direction from 0x5d2 (before: always Left).

**Control + tapered walls + gouge check.** At the Control write the leads are rebuilt by FUN_180050de0(piece, local_260, local_258, side) (l.58708);
local_258 (the gouge bound) was freed at the end of the pass loop (l.58622-58623), local_260 is the depth's offset chain with taper (ccomp_pock by
FUN_1800569a0's distance, l.58428-58434) and the chain bound without. `rm_compute` now hands the tapered bound back (`C.tb[depth]`) and the Control lead
call uses it; the refusal is gone.
