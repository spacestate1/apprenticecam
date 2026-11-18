# MCCECalcContourOffset: decode notes, the DLL oracle, the partial port (2026-11-08)

Port: `viewer/mcce_ctour.c` / `.h` (`mcce_contour_offset`). Oracle: `tools/peloader --coff` (`tools/peloader/mcce_coff.c`), the case builder `tools/coff_cases.py`, the
driver `tools/coff_port.c`, `tools/coff_cmp.py`, `tools/tests/mcce_coff_oracle.sh`. Wiring: `viewer/cam2d.c` `ctoff_port` (default on, `MCCE_CTOFF=0` off; section 4). Line numbers are
`decomp/mcce_all.c` unless a file is named. Status: **partial port** (closed and open chains, several result rings and near-180-degree corners since 2026-11-08); what it does is proven against the real DLL, what it does not is refused (section 5).

## 1. The entry and its parameters

`ccomp_pock` (decomp/mccore_ccomp_pock2.c, reached from `ccomp_pock_from_ctour`, decomp/mccore_ccomp_pock.c): `MCCECalcContourOffset(1, dist (negated for 'R'), in, out, flags, angle, gap,
nc_tol, 0.75 nc_tol)`, through the `pock_bound` overload @0x180006839 (l.11661-11740: `CopyPockBndsToShAr`, the `CVe<CSh*>` overload, `CopyShArToPockBnds`, `FUN_1800d9960`, `FUN_1800d9830`).
- flags 0x40027 (Infinite look ahead on) / 0x60027 (off); angle 179.1 (roll None), 44.99999063801583 (Sharp), 0.5729577951308232 (All) degrees; gap = chain_tol (1e38 / -1 for the callers that
  ask for it, the contour gives chain_tol).
- Flags inside the export (l.8686-8689): `flags & ~0x50`; with 0x20000 also `& ~0x70` (so 0x20, the input self-intersection split, is off when look-ahead is off).
- Result: for each output shape, entities with `+0x60` = pointer to the input entity it comes from (`FUN_1800dbce0` copies it onto the raw line / arc pieces and `FUN_1800d9830` keeps it; the corner
  entities it inserts have no link). `FUN_1800d9960` copies `+0x18` of the bound and `+0x68` of the first entity from the source bound.

## 2. The pipeline (closed shape), as ported

```
tmp = copy of the input                        l.8704-8709
FUN_1800da1c0 (flags)                          l.117939-118048: CeFlags 0; bbox; FUN_1800d99e0 (bit 0x20000 off: a shape of identical entities reduced to one); bit 1 closedness (gap) -> dir 0 if not closed within gap; FUN_18012c2b0 tiny-entity removal (0.0001);
                                                 FUN_18012c740 gap closing (below); bit 0x20 FUN_180132ef0 self-intersection split (see 5); bit 8/0x10: not set here
e = FUN_1800cd040 (d, shape)                   l.110684-110781: closed: e = (dir == 1) == (d <= 0) ? +|d| : -|d| (open: see 5)
FUN_1800da180 (e > 0: FUN_1800d92f0 frame)      l.117926-117936, 117465-117547: shape made an island of a frame box 2.5·|e|·n wider
FUN_1800da5e0 orientation                       l.118050-118075: outers CW (-1), islands CCW (+1), RevSh with the 0x90 bit 1 mark if reversed
FUN_1800d50d0 per nest group                    l.114916-114998: FUN_1800dbce0 for the shape, FUN_1800d48b0 for the frame (never used in the result)
  FUN_1800dbce0 raw offset ring                 l.118764-119112: to the RIGHT of travel by |e|:
    FUN_1800db950 line (l.118671-118763), FUN_1800dadb0 arc (l.118311-118506: sw > 0 r + |e| else r - |e|), FUN_1800db4b0 a shape of one arc, the circle (l.118508-118670)
    FUN_1800c9d70 / FUN_1800ca830 corner (l.108916, 109263-109333; return values from the disassembly 0x1800ca8a1-0x1800ca9f7): tangents dot < cos(pi - 1e-4) reversal FUN_1800cb710 (see 5);
      inner corner -> trim to the intersection FUN_1800cb410 (l.109705-109912), a chord FUN_1800c9c60 (l.108881) or an arc FUN_1800ca220 (l.109073) by the 0x20000 flag and dot < 1e-4;
      outer corner -> 4 = FUN_1800ca460 mitre (l.109144-109262) if cos(turn) > cos(angle) and neither entity has ce & 0x10, else arc about the source point (FUN_1800ca220), radius |e|
    closing corner (l.119082-119112); FUN_1800da940 merges the mitre lines into the neighbouring lines (l.118175-118310); dir = -1
  FUN_1800d47b0 / FUN_1800c7bc0 (l.114618, 107904-108011): crossings FUN_18012f3b0 / FUN_18012f620 (l.163223, 163304-163463: pairs within boxes 1e-5, CalcEntIntersect 1e-7, a crossing not within 1e-6 along
    the chain of a joint - FUN_18012ff20 / FUN_18014b720; with 0x20000 only the next 3 entities of each); none and FUN_1800c8da0 (l.108468: flagged corner / inverted-arc length < the rest): the ring is the
    shape, FUN_1800c7f00 (l.108013: box under tol or area under tol^2 -> dropped, dir from the area FUN_180126ac0); with crossings: the loop removal below
  FUN_1800d4400 (l.114479-114564, look-ahead on only, FUN_1800d4660 / FUN_1800d4720): a result ring is dropped unless it has the region's direction, does not touch the source (FUN_180122a40 l.156181) and, for an outer source, lies inside it (FUN_18013e910 l.171277)
  FUN_1800d3d30 (l.114216-114389): FUN_1800d4170 sorts the rings by the source's island flag. No island-derived ring: the rings go straight to the output. One island-derived ring and the lone frame: both moved to the output. Anything else needs the boolean (see 5)
FUN_1800ce670 tail                              l.111406-111494: frame shapes dropped (e > 0); RevSh back if the source was reversed; FUN_1800da6a0 (l.118079) rotates the start to the entity with the lowest source index,
                                                 one earlier when that is an arc preceded by a line with ce & 2 (FUN_18014fd50 rotates)
FUN_18012c740 gap closing (l.161726-161766, joints in order, gap >= 1e-10; link threshold 1e-10 look-ahead on, 1e-7 off, l.118012-118020):
  FUN_18012a1b0 (l.160388-160573): the extended intersection FUN_1800aad10 (l.92865-92922, tol 1e-6, CalcArcLnSegIntersect / FUN_1800a4460 / CalcLnLnIntersectPt with bool true: FUN_1800a46e0 instead of the sweep test, no line range test) -> FUN_18012bcd0 picks the point
    nearer the gap midpoint (l.161428-161556): not beyond a line's far end, within 1.5 gap of the midpoint when beyond an entity, the extensions FUN_18012ac00 (l.160786-161062) must stay within max(2 gap, 1e-5) of the other entity (FUN_18013c4f0, l.169903-170077, 3 levels of halving)
    FUN_18012b390 (l.161105-161421): a joint not closed by that intersection, nearly in line (FUN_18012b2b0 with the tangent line, 1e-5): ends snapped (line - line the midpoint; line - arc the line's end to the arc's), not for two arcs
    otherwise both entities trimmed at the intersection (t > 1 on an arc = the backward parameter -|(2pi - |sw t|)/sw|)
  FUN_18012a720 (l.160580-160778): a joint still open >= link threshold: FUN_180129530 (l.160037-160381) moves back from the ends (t1 = 1 - i step, t2 = i step, step = max(gap, 1e-5)/len capped at 0.1, i <= 5) while the chord stays on the two entities (FUN_18013c4f0) and the two end cosines improve, stop when both > cos 10 deg; both entities trimmed; line ce 0x10, no source, inserted before the current entity
result tail FUN_1800d8d90 (l.117293-117338, bit 0x100 off, on each result shape, after the reversal and rotation of FUN_1800ce670):
  FUN_1800d62d0 (l.115651-115961): entities shorter than t1 (lines) / t2 (arcs), t1 = nc_tol, t2 = 0.75 nc_tol: replaced by a line from the previous end to their end (src cleared) or, when that line is within max(t1, t2), removed; each run joined to the next long entity by
    FUN_1800d6ad0 (l.115968-116230): the intersection FUN_1800aad10 / FUN_1800d8f00 (l.117345-117462, the point nearer the midpoint) or the feet FUN_1800d8900 (l.117002-117144: line-line perpendiculars / the arc's foot FUN_1800d8050 / FUN_1800d8260 / FUN_1800d7940), tested by FUN_1800d5330 (l.115006-115604: the trimmed pair and the removed run within max(t1, t2) of each other, FUN_18013c4f0), then the ends trimmed and the entities between removed
  FUN_1800d7100 (l.116238-116573): each run of corner entities (ce & 2) between two others: FUN_1800d6ad0 on the sub-chain with tol t1 (the near-tangent corners are absorbed here)
loop removal (FUN_1800c7bc0 l.107906-108010, ring with crossings):
  FUN_18012f3b0 (l.163225-163301): crossings records (FUN_18012f620 emission, sort FUN_18012e7d0, duplicate removal FUN_18012ed20, renumbering FUN_18012e930)
  FUN_1800c8030 (l.108057-108117): a section between consecutive records is dropped (state 3) if it holds a flagged corner / inverted arc longer than 1e-3 (FUN_1800c8370) or, look-ahead on, its midpoint is covered: FUN_1800c8920 (l.108345-108465, within |d| - 1e-5 of the source, or inside a mitre record FUN_1800c9e50 l.108953-109070)
  look-ahead on: FUN_1800c92b0 (l.108673-108738) spreads the dropped marks; FUN_1800c94b0 (l.108745-108799): kept sections with a kept partner dropped if their quarter point is covered or they cross the source (FUN_18014afd0); FUN_1800c7100 (l.107557-107738): walks over the kept records by decreasing section length (qsort_s comparator 0x1800c7b80), changing to the partner where kept; FUN_1800c8fa0 closes a walk; FUN_1800c6c90 (l.107412-107552) the ring from the sections of consecutive records (FUN_180146e20), FUN_1800c7f00 checks it
  look-ahead off (bit 0x20000): FUN_1800c7670 (l.107743-107901): a record's next partner seen earlier on the list cancels the loop between when FUN_1800c8750 (l.108285-108338) sees a dropped entry and fewer than 6 entities spanned; one ring from what remains
  results: DetNestLvlShAr (single ring: island flag 0), FUN_1800d4400 (l.114481-114564) drops by direction, contact with the source (FUN_180122a40) and, outer source, FUN_18013e910 (l.171277-171434: the ring must lie inside the source, PtInSh l.33-206 with FUN_180143460 / FUN_180144460 / FUN_180145690 / FUN_1801453d0, FUN_1801489a0); no-crossing rings take the same filter
```
### 2.1 Open chains and the self-crossing split (ported 2026-11-08)

An open source goes through the closed path as a zero-width shape, and the result is cut back into open pieces.

- **Which chain is open**: the input's ccw byte only (`CopyPockBndsToShAr` l.418-: dir = ccw ? 1 : -1, so an open bound's 0 cannot be expressed); `FUN_1800da1c0` l.117956-117971 sets dir 0 when the chain does not close within gap (`FUN_180123770`). So an "open" chain whose ends are within gap is closed; and `MCCE_CTOFF_DUMP` writes the dir as `a > 0 ? 1 : -1`.
- **Self-crossing split** (bit 0x20, look-ahead on only, l.8686-8689): `FUN_1800da1c0` l.118021 -> `FUN_180132ef0` (l.165098-165176) -> `ReconfigSelfIsectSh` (l.878-1163, flags 0x80020) -> `FUN_180133130` (l.165180-165300): records `FUN_18012f3b0` mode 0; each record in turn on a walk, and as soon as the partner of the record after the walk's last entry is on the walk (`FUN_180131740`, l.164328-164348) those entries form a closed shape (`FUN_1800c6c90`, already ported as `ring_from_walk`, `FUN_1800c7f00` filters it) and leave the walk. For an open shape only `n - 1` records are taken and what is left on the walk becomes the open remainder (`FUN_180131240`, l.164180-164312: chain start, the remaining records' sections, chain end). Bit 0x80000 (l.1018-1096): `+0x54 = 0`, open pieces apart, `SoPockUnionAr` over the closed ones (one closed piece: nothing to unite; two or more: NOT PORTED, refused), open ones appended after; the parent is replaced by the pieces only when something was found (acStack_108). `FUN_18012d040` + `DetNestLvlShAr` close `FUN_1800da1c0` when the count changed (closed shapes first, open last, l.118040-118045). The pieces are then offset one by one in the loop of the export (l.8730-8769), each appended to the result.
- **Retrace**: `FUN_1800cd040` (l.110684-110781) for dir 0: e = |d| (always the island side); d > 0 first `RevSh` (l.110756), `FUN_18011d800` (l.153634-) appends the reversed copies, `FUN_1800d98a0` (l.117684-) clears the source link +0x70 on the returning half, dir = 1. From here the shape takes the closed path as an island with a frame.
- **The raw ring** (`FUN_1800dbce0` l.118986-119005, export l.8737-8740 `+0x88` = the entity count of the open source): from the first entity of the returning half on, `+0x58` = 1 (no mitres: `FUN_1800ca830` then always gives the arc); at that entity everything already in the ring (the forward half and its corners) is marked ce 0x20. The end caps are ordinary corners: `FUN_1800ca830` -> `FUN_1800cb710` -> `FUN_1800caac0` (l.109339-109702) walks outward from the corner and finds each pair of entities mirror images, so the walk meets without a difference, returns 0, `FUN_1800cb710` returns 1 and `FUN_1800c9d70` makes the arc about the source point (radius |d|). A difference (a real near-180 corner) goes to the rest of `FUN_1800cb710`: NOT PORTED, refused.
- **Blunt inner corners**: `FUN_1800c9d70` l.108933: with a source of `+0x88 != 0` the chord is used even for a blunt corner. **Flagged sections**: `FUN_1800c8330` (l.108122-108135): look-ahead off with `+0x88 != 0`, only an entity marked 0x20 counts.
- **After the ring**: `FUN_1800ce670` for dir 0 (l.111425-111450): `RevSh` when d > 0; `FUN_1800da6a0` rotation; `FUN_1800ccf20` (l.110643-110680: first and last entities both marked: the start moves after the last unmarked); `FUN_1800cd7e0` (l.110936-111110): the runs of ce 0x20 entities become open shapes (dir 0; a run that is the whole ring keeps the ring's dir; it stops at once if the first entity is unmarked); `FUN_1800cd350` (l.110788-110930: a run within 1e-4 on the wrong side of the source is deleted: NOT PORTED, refused); `FUN_1800cead0` (l.111588-111648) sorts the shapes by their lowest source entity index (MSVC qsort, comparator 0x1800cd020: ascending ints), and `FUN_1800d8d90` the tail on each.

Sign and orientation: d > 0 left of the shape's travel, d < 0 right (mcce_engines.md 6.13.2); the core itself always offsets to the right of travel of a shape it has oriented outer-CW / island-CCW.

## 3. The DLL oracle

`tools/tests/mcce_coff_oracle.sh [WORK]`. The driver loads the real `mcce.dll` (tools/peloader), builds the same `pock_bound` input and calls the exported overload, and prints every output entity with its
`+0x60` link mapped to (input shape, entity). A hook in `viewer/cam2d.c` (`MCCE_CTOFF_DUMP=FILE`: every contour offset request of `gen_contour`: the chain's exact entities, the ccomp_pock parameters) provides the
user's chains. Comparison `tools/coff_cmp.py`: type, endpoints, centre, radius within 1e-6, source link, direction, per shape and entity. peloader `--cov-funcs FILE --cov-out HITS` adds per-case function coverage (one-shot
int3 at each decompiled function entry, re-armed between cases) - how the decode order below was chosen.

| Set | Cases | Exact before (a10c588) | Exact before open chains (6fe3907) | Exact now | Mismatch now | Refused now |
|---|---|---|---|---|---|---|
| The 149 contour ops' offset requests (`corpus`, as dumped; 780 in the a10c588 run, 781 in today's dump) | 781 | 609 | 758 | **778** | 0 | 3 |
| Those chains, de-duplicated, at x1 / x0.5 / x2 / x0.1 offset, both signs, look-ahead on / off, roll None / Sharp / All (`var`) | 4800 | 3288 | 4341 | **4767** | 0 | 33 |
| Synthetic (`coff_cases.py`: as before: hourglass, dumbbell, comb, finer offsets across the half widths, and 8 shapes with every joint opened by 17 gap sizes from 1e-11 to 0.06, around the 1e-10 threshold and chain_tol 0.05, in 3 directions) | 11472 | 1897 | 9835 | **10919** | 0 | 553 |
| Synthetic open chains (`open_*` in `coff_cases.py`, new: lines, polylines, arcs, line-arc mixes, tiny chains, chains coming back close to themselves, offsets larger than the features, both directions, all rolls, look-ahead on / off; all at input dir 0) | 6528 | - | - | **6216** | 0 | 312 |

**After several rings and near-180-degree corners (2026-11-08, section 5.1 / 5.2), exact / mismatch / refused: corpus 781: 781 / 0 / 0; var 4800: 4800 / 0 / 0; synthetic 11472: 11448 / 0 / 24; open synthetic 6528: 6516 / 0 / 12.** The table above is the state before.

Every non-refused case agrees entity by entity (dir: the DLL side prints the bound's +3 ccw byte, `FUN_180001c08`/`CopyShArToPockBnds` l.2948 (dir == 1), so an open shape reads as -1; `tools/coff_port.c` mirrors that, and dir 0 is only verified by the code reading of `FUN_1800cd7e0`). The refused reasons are in section 5. "Exact before" for the synthetic set is the a10c588 port run on the enlarged set; the old 1056-case set gave 530. DLL function coverage (`--cov-funcs`) on the synthetic set: FUN_180129530 (link line) 1764 cases, FUN_18012b390 8688, FUN_1800d6ad0 4573, FUN_1800d5330 3981, the feet FUN_1800d8900 1718 (arc-line 342, line-arc 72, arc-arc 612), FUN_1800c7100 1256, FUN_1800c7670 949, FUN_1800c6c90 2213.

Infinite look ahead off alone (flags 0x60027): `var` 2184 exact, 0 mismatch, 216 refused (all open chains); `syn` 4938 exact, 0 mismatch, 798 refused (744 open chains, 54 corners of almost 180 degrees); the corpus has no such op.

## 4. Wiring into the Contour op

`viewer/cam2d.c` `chain_offset`, closed chains on a side: with `MCCE_CTOFF=1` the loops come from `ctoff_port` (exact entities, ccomp_pock parameters, the same `left`-of-travel sign as `coff_cases`), else - and for any input the port refuses - the Clipper offset as before. The rest of the
pass (orientation, start point rule, leads) is unchanged.

**Default since 2026-11-08 (user decisions D-4, D-5): on; `MCCE_CTOFF=0` switches it off. Infinite look ahead off (flag 0x20000): closed and, since this round, open chains through the port; a chain the port refuses refuses the op (`g_nolook_refused`, `viewer/cam2d.c`).**

**Open chains (2026-11-08)**: `chain_offset`'s open branch asks `ctoff_port` and falls back to `offset_open` only for a refused chain (with look-ahead off that refuses the op, as above). The pass: `ccomp_pock_from_ctour` (decomp/mccore_ccomp_pock.c l.58-63) makes only the **first** result bound the contour (`pock_bound_to_ctour`, count +1), the head of `ccomp_pock`'s list, i.e. the first shape of the result (`CopyShArToPockBnds` l.2905-: built last to first): for an open chain the piece with the lowest source entity (`FUN_1800cead0`), a one-way open path along the chain's direction (forward halves, `RevSh` undone). `ctoff_port` therefore keeps `res.s[0]` only and appends the last end point. An empty result (nothing kept) is empty loops, what `FUN_180051990` l.56154 does with the failed flag.

Contour oracle (`ORACLE_FILEOP=1 ORACLE_STEP=0.01 tools/oracle.py --type contour`, all 149 ops, fresh cam_cli):
- **Open chains wired (this round, measured against 6fe3907 per op): on 149/149, median 0.004, worst 0.010; `MCCE_CTOFF=0` 149/149, median 0.005, worst 0.022; at the printed p95 (the worse direction) 5 better, 144 same, 0 worse = 149. The 5: buckle-v10 / v11 / v12 op 4 (0.005 -> 0.004, max 0.008 -> 0.005, our length 190 -> 180 against the stored 180) and buckle-v11 / v12 op 21 (0.005 -> 0.004, max 0.010 -> 0.006).**
  All 781 offset requests of the 149 ops: 778 through the port, 3 refused (several result rings, keep the old path); none skipped for spline chains or trimmed ends, none refused as "closed here, open for MCCE".
  Op scenes (`tools/tests/op_compare.sh`): 53/534 differ = the known 46 (43 `*.mcamui`, gen_blend / gen_sfradial / gen_srplunge `.txt`) + 7 new, none blessed: `gen_ct_nolook_open.txt` (intended: the op with look-ahead off and an open chain is now generated, 61 points, instead of refused) and `rt_smooth_{arcs,def,fixed,min,save,shift}.-mcamui.nc` (the open chain's exit lead: the arc leaves along the last line's tangent (0.970, 0.243), as FUN_180051990 l.56297-56310 builds it on the last element; the golden's exit corresponds to a last chord of (0.6, 0.8) from the old tessellated open offset (read off the NC arc, the old path's cause not traced); `rt_smooth_fixed` / `save` also move one point by 0.001 in the Smoothing resample). No Mastercam NCI exists for these scenes, so the exit is unverified against Mastercam.
- **Combined, b780ba0 (gap closing, tail, loop removal and the lead fix together): switch off 149/149, median 0.005, worst 0.022; switch on 149/149, median 0.004, worst 0.010; at the printed p95 89 ops better, 60 same, 0 worse = 149; op 9 of both parts 0.008 -> 0.005. Op scenes 55/530 differ (the known 55). The port-use and refusal tables below predate gap closing and were measured with the lead fix alone (7fefaa9).**
- switch off: 149/149, median 0.005, worst 0.022, every op line identical to the baseline (off the code path is untouched);
- switch on, before the fix (2467bbc): 147/149, worst 0.181; op 9 of BRACKET4 and bracket4-dashed worse (0.008 -> 0.181). At the oracle's printed 3 decimals that run is 64 better / 83 same / 2 worse = 149 (the "69 same" reported earlier was a miscount);
- switch on, after the fix (7fefaa9): **149/149, median 0.005, worst 0.022**; op 9 of both parts 0.005 (off 0.008). Printed 3 decimals: 66 better / 83 same / **0 worse** = 149.

The regression, confirmed from the data: `pass` took the lead directions from `path_dirs`, the ring's first and closing **chords**. BRACKET4 op 9, first chord of the port's ring (3.4, 7.5) -> (3.402193, 7.374362): 1.0 degree off the true tangent (0, -1), half of the 2 degree tessellation step (hence 5 degree chords gave 0.568). The lead turned with it about the attach point and the gouge check then cut it elsewhere: our lead start (8.780195, 10.594369), the stored NCI's (8.93517, 10.5), 0.18 apart. The Clipper ring hid this: it happens to have a chord along the true tangent at that point. The ring itself and the start point were right.
What Mastercam does: the leads are built on the path's own first and last elements, true lines and arcs: `FUN_180051990` (decomp/mcmill_all.c l.56236-56246 first element, l.56277 / l.56294 entry pieces, l.56297-56310 last element, l.56347 / l.56365 exit pieces) -> `FUN_1801c24a0` (l.311687-311688: `eval_gt_pt` / `eval_gt_tan` of that element at t = 0 / 1).
The fix, `viewer/cam2d.c`: `chain_offset` keeps the port's entities and `ctoff_set_dirs` records their tangents at the ring's start point; `pass` uses them on a closed path starting there (`ctoff_exact_dirs`). The chords only choose the sign (the ring may run backwards) and which of two entities meeting at the point (**ours**, not a Mastercam behaviour). Only a ring from the port has them, so the Clipper path is untouched. After the fix our lead start on op 9 is (8.934198, 10.5).

All 149 ops, switch on against off (`MCCE_DEBUG=1` per offset request: ported / refused):
| Port use | Ops | Which |
|---|---|---|
| every closed-chain request ported | 83 | |
| some ported, some refused (Clipper for those) | 9 | buckle-v10 12; buckle-v11 12, 18; buckle-v12 12, 18; buckle-v6 / v7 / v8 / v9 28 |
| every request refused (all Clipper) | 25 | buckle-v10 13; buckle-v11 13, 14, 19; buckle-v12 13, 14, 19; buckle-v6 / v7 16, 18, 19, 30, 37; buckle-v8 16, 18, 19, 30; buckle-v9 18, 19, 43, 48 |
| no closed-chain offset request (open chains, or on the line) | 32 | the port is not asked |

Refusal reasons over the 758 closed-chain requests: 609 ported; 127 refused for an entity gap of 1e-10 or more; 22 for a near-tangent corner (these 22 are in buckle-v6 / v7 op 37 and buckle-v11 / v12 op 14). Nothing was skipped for spline chains or trimmed ends.

Three measures of "better / same / worse", all adding to 149:
- printed 3 decimals (the oracle's own p95, worse direction): 66 / 83 / 0.
- the same sampled p95 at full precision: 83 / 63 / 3. The 3 are buckle-v6, v7 and v8 op 13, +0.000021. On the sampled max, 69 / 73 / 7: those 3, BRACKET4 and bracket4-dashed op 15 (+0.000001), and buckle-v11 / v12 op 11 (+0.0027). These are the sampling floor: at ORACLE_STEP 0.01 two identical paths can read up to 0.005 apart, and the numbers move when the sample phase moves.
- without the floor (ours: point to segment, both ways, the stored moves against our feed segments; equal within 2e-6, the moves are written to 6 decimals): p95 **77 better / 72 same / 0 worse**, max 77 / 72 / 0. Median p95 0.004369 off, 0.000327 on; worst 0.022329 for both (buckle-v11 / v12 op 18, where the worst chain is a refused one). The 72 same are the 32 not asked, the 25 all-Clipper, the 9 mixed (their p95 and max are set by a refused chain) and 6 fully ported ops.

**Several result rings and near-180-degree corners (2026-11-08, against a188398 per op): 149/149, median 0.004, worst 0.010; 1 better (buckle-v12 op 13, p95 0.005 -> 0.004, max 0.033 -> 0.007, the three chains that were refused and took the Clipper offset), 148 same, 0 worse.** The pass is still the first result shape only. Visible on an open chain whose offset falls apart into pieces: only the piece with the lowest source entity is cut (scene `gen_ct_rings`: a 6 mm piece of the spiral, equal to the DLL's shape 0 for that chain; no Mastercam NCI exists for it).

Not covered: the Ramp and Oscillate writers (`rmp_first_dir` / `rmp_last_dir`) still take lead directions from chords under the switch; no op of the corpus has those contour types (0x1308 is 0 or 2 on all 149), so nothing can check them and they are unchanged. The lead gouge check still runs on the tessellated ring (op 9's cut lead is 0.001 short of the NCI's at 2 degree chords).

So the switch stays **off by default** (rule: no worse on every op). Refused chains keep the Clipper offset, so "on" is a mix of the two.

## 5. NOT DECODED / NOT PORTED (the port refuses these, with the path in `McceResult.refused`)

Counts on the sets of section 3 (corpus 781, var 4800, synthetic closed 11472, open 6528).

- ~~**A self-intersecting input with several closed pieces** (synthetic 24)~~ **ported 2026-11-13 (M2 links step 3)**: `ReconfigSelfIsectSh` bit 0x80000 (l.1018-1096): +0x54 = 0 on every piece, the open ones apart, `SoPockUnionAr(tol, closed pieces)` = `mcce_sobool.inc` so_union_ar (the region boolean ported by M3, DLL-identical on its own unit oracle), the open ones appended after, every piece then takes the parent's +0x54. `tools/tests/mcce_coff_oracle.sh`: synthetic 18000 / 18000 exact (was 17976 + 24 refused), corpus 781 / var 4800 exact.
- **An open-chain result piece within 1e-4**: ported 2026-11-09 (decision N9-9): `FUN_1800cd350` (the side test: `FUN_18013d010` / `FUN_18013d280`, `FUN_18006f1c0` / `FUN_18006f4f0`) and the tail's `FUN_1800d6180` / `FUN_1800d8b40` (`mcce_ctour.c` tiny_side_cull, tiny_cull); `FUN_1800ce9a0` / `FUN_1801230c0` too (a doubled-up shape takes its direction from the sign of d). `tools/tests/mcce_coff_oracle.sh`: corpus 781 / var 4800 exact, synthetic 18000: 17976 exact, 0 mismatch, 24 refused (`SoPockUnionAr`); the 12 open cases are exact.
- **Several island-derived rings that touch, or several groups with holes**: ported 2026-11-13 (M2 links step 3), `FUN_1800d3d30` (l.114214-114389) as decoded: `FUN_1800d4170` splits island-derived / outer-derived rings; one island-derived ring is moved; two or more go through `SoPockUnionAr(t1)` (l.114289), `FUN_1800d42a0` (l.114432-114478) moves the shapes with +0x54 != 0 apart and `FUN_18015b1c0` (l.186989-187020: `FUN_1801638f0` for every frame x hole) replaces them; with the single frame (+0x90 bit 1) the output is union + frame + holes, `DetNestLvlShAr`. Reached by Dynamic Mill's link smoothing (FUN_180048840, flags 0x101): 15 of the 43063 passes of `tools/tests/coffct_oracle.sh` (now 43063 / 43063 exact). The frame is still the stand-in box (next item).
- `FUN_1800d48b0` beyond a single frame: the frame ring is not built. Where its place in the nesting order matters (several island-derived rings) a stand-in box around all rings takes it (ours: only its containing everything is used, and it is dropped as `FUN_1800ce670` drops the real one).
- More than 64 result rings: refused (ours, the size of the nesting tree). Reached by no case.
- Limits of the port: n = 1 pass, one shape (flag 0x40000 forces it for the contour), `+0x68..+0x80` carried fields not produced (the DLL also carries only `+0x60` on the offset entities).
- **Inference, not a line-for-line reading**: `FUN_18012fc00` (l.163470-163542) writes back the t values sorted by (entity, t). It is treated as a no-op because the list is already in that order after `FUN_18012e7d0`; records at the same point on one entity are ordered by their partners there, and a difference could only arise for those. Its comparator 0x18012e790 was only partly read.
- **Inference on the no-contact boolean**: `FUN_180162010` was not read to its end for shapes without contact; it is taken to give nothing back there (`FUN_180163bf0` / `FUN_180163580` then decide), which the 0 mismatches over the sets of section 3 support and do not prove.

### 5.1 Several result rings (ported 2026-11-08)

- `DetNestLvlShAr` (l.7733-7792): one shape: island flag 0. Otherwise `FUN_18013e3e0` (l.171120-171245): CRT qsort with comparator 0x18013e780 (closed before open, otherwise equal: the CRT's order of equal keys is kept, `ms_qsort`), each shape placed in the tree by `FUN_1801402a0` (l.172216-172300: a node containing it: descend; nodes it contains move under it, `FUN_18013fec0`); `FUN_18013fff0` (l.172022-172115) numbers the levels and lists them: even-level nodes after their subtree, odd-level ones after their whole chain.
- `FUN_18013e910` (l.171277-171434) in full: the list `FUN_180133c70` (l.165530-165671, comparators 0x180133630 / 0x180133680, `FUN_180133b30`, `FUN_180134800`, `FUN_1801349c0`), the sections on the other shape (`FUN_1801489a0`), the area compare (`FUN_180126ac0` tol 0.1), `FUN_18013f550`.
- `FUN_1800d4400` (l.114481-114564): the filter on every ring; with an island source an even ring dropped takes the odd rings before it (its children in that list's order).
- `FUN_1800d3d30` (l.114216-114389): outer source: the rings go to the output in the list's order. Island source, one ring: as before. Island source, two or more: `SoPockUnionAr` keeps shapes without contact, and its tail runs `DetNestLvlShAr` once more (l.2711, which fixes the order of equal keys: found from two synthetic mismatches); `FUN_1800d42a0` moves the nested rings aside, `FUN_18015b1c0` -> `FUN_1801638f0` copies each back (a hole inside the frame, `FUN_180163580`), and the list evens + frame + holes is nested again.
- The tail `FUN_1800ce670`: per shape, then `FUN_1800cead0` over all of them. An open source goes through the open branch for every shape, the pieces ordered together.

### 5.2 Corners of almost 180 degrees (ported 2026-11-08)

`FUN_1800cb710` (l.109917-110120) beyond the retrace: `FUN_1800caac0` (l.109339-109702) with the feet `FUN_18013d200` / `GetNearestEntPt` gives the section where the two sides part, `FUN_180146e20` copies it, and `FUN_18014a290` (l.177490-177675) walks both sides at distances 1e-5, 2e-5, 4e-5 ... until two points more than 1e-5 apart lie off the line through the source point by at least 1e-7: 3 (outer) or 2 (inner), the same codes as `FUN_1800ca830`. No difference found: 1, the arc.

The former refusals for gaps, near-tangent corners (a turn below 0.02 rad, the threshold was ours), zero-length trimmed entities and raw-ring crossings are gone: those paths are ported (section 2).

## 5.3 Dynamic Mill's link smoothing (2026-11-13)

`FUN_180048840` (l.43097-43249) calls `MCCECalcContourOffset(1, d, {shape}, out, 0x101, 0.0, -1.0, 1e-6, 1e-6)` (the CSh overload, l.43188-43191)
four times per smoothing distance. With gap -1 and bit 1, `FUN_1800da1c0` opens every shape (l.117956-117971); bit 0x100: the vanishing-arc limit
|d| + 1e-15 and no `FUN_1800d8d90` tail. `viewer/mcce_dyn.c` oc_offset now calls `mcce_contour_offset` with exactly these arguments (was an own
approximation). Unit oracle `tools/tests/coffct_oracle.sh PART OP ...`: the connector oracle with CT_COFF=1 records the DLL's own input and output of
every pass (hooked call 0x180048a11), `tools/coffct.py` turns them into COFF cases, `tools/coff_port` (COFF_RAWDIR=1) runs the port on the same
inputs, `tools/coff_cmp.py` compares. buckle-v12 3 / 22, v6 32 / 36, v10 10, v8 42, v9 51: 43063 passes, all exact.

## 6. Downstream features

What each can use now (partial, `mcce_contour_offset`):
- Each output entity has its type (true line or arc, with true radius) and a link to its source entity (`src_sh`, `src_ent`, `-1` for inserted corners / chords).
- **Parameter on the source entity**: MCCE does not store one (entity `+0x68..+0x80` are not copied to the offset pieces); the caller derives it from the point and the source entity's geometry (what `connect_3d_moves` needs, formats/contour_3d_tabcutoff.md, is then computable for the proven closed-chain class).
- General 3D contour (`connect_3d_moves`; still refused, see formats/contour_3d_tabcutoff.md 1.1), Smoothing with arcs / rolled corners, Remachining (`ccomp_pock -> MCCECalcContourOffset`), ramp tabs at entity ends: usable for chains the port does not refuse, micro-gaps and loop removal included (one result ring), open chains included.
- Infinite look ahead off (0x20000): crossing test, link threshold 1e-7, and the loop path `FUN_1800c7670` are ported. Unit oracle evidence, section 3. The "Infinite look ahead off" refusal in `gen_contour` (viewer/cam2d.c) **stays until the user decides**; no real op in the corpus uses the flag, so the evidence is the user's chains re-run with that flag plus the synthetic cases, and nothing compares the resulting toolpath with Mastercam's NCI in that mode.
