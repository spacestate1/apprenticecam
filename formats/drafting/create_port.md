# Drafting - port of the LINEAR dimension creation core (calc_linear_dimension) and its corpus oracle

## 1. Ported, from the 2017 decompile (step 1 "creation core" of the Drafting tab)

Code: `viewer/draft_create.{h,c}` (`dc_calc_linear`, `dc_linear_angle`, `dc_fonts_load`); oracle: `tools/tests/draft_create_test.c` +
`tools/tests/draft_create_test.sh` (`RUN=1` runs the corpus, `VERBOSE=-v` lists every difference). Sources are mccore.dll
(image base 0x180000000), decompiles in `decomp/draft/` (`core_createport.c` is new: FUN_18023dad0, FUN_18023e0b0,
df_angle_between_lines_rads, find_orient_angle, FUN_18023de60, df_calc_orient_ang, df_angle_hv), constants and call arguments read in
`objdump -d mccore.dll` where Ghidra truncated the call. Every rule in the code cites its source next to it.

| step | function (address, decomp file:line) | port |
|---|---|---|
| validation, header, P1/P2 order, text angle, value / outside test, arrows-out force, centring snap | calc_linear_dimension @18024c480 (core_calcdim.c 797-1232; asm 0x18024cb9e-0x18024d239) | `dc_calc_linear` |
| value text + extents (tolerance mode 0) | FUN_18030f410 (core_calcdim.c 5805-6412) | `f410` |
| text extents, stroke fonts | get_ext @18031d040 -> FUN_180316750 (text1.c 238-449), FUN_18031dd60 / FUN_18031ca90 (text1.c 2470 / 6408) | `get_ext`, `cw` |
| half widths for the one-sided placement | FUN_18024fc30 (core_calcdim.c 5185), FUN_1802571c0 (core_dimlayout2.c 297) | `fc30` |
| centring, one-sided placement, P order by angle sign | FUN_180250410 (core_calcdim.c 5314; asm 0x1802505f7-0x18025086e) | `layout` |
| text, box, halves, witness copy | FUN_180252850 (core_dimlayout.c 2131; stack args from asm 0x180252850-0x180252cb1) | `layout` |
| text box | FUN_18024c160 + box_limits @18024b680 (core_dimgeo.c 3, 396) | `box_limits`, `layout` |
| dimension-line halves | FUN_180255b80 (core_dimgeo.c 40; control flow from asm 0x180255b80-0x180256449) | `f255b80` |
| text between the witness lines / leader style 1 / extra segment | FUN_18024b250 (499), FUN_180257270 (612), FUN_18024b570 (470) | `f24b250`, `f257270`, in `f255b80` |
| line x box edges | FUN_180254bc0 (core_dimlayout2.c 207; edge table 0x1812d7944 = 0,1,3,2), l_ints @1804b3480 (core_dimgeo.c 826), l_hv_ints (exported; body from decomp/pocket_b6/zigzag_decomp_zz_sys.c 68) | `f254bc0`, `l_ints`, `l_hv_ints` |
| witness lines | FUN_180256450 (core_dimgeo.c 310) | `f256450` |
| record assembly, wl slots | FUN_180252e50 (core_dimlayout.c 2245), make_wl_single @180254950 (core_calcdim.c 2526), aPointTestTol | `layout`, `make_wl_single` |
| text slot 0 | FUN_180251460 (core_calcdim.c 6585), FUN_180252d60 (core_dimlayout2.c 165), FUN_180252790 (asm: angle + 2 pi if negative, align bits `& 0xe9ff | 0x900`) | `layout`, `slot_font` |
| post-check, orientation re-derived | FUN_180251290 (core_calcdim.c 5640), df_angle_of_line_rads @180241aa0 and AbsAngleTheSame @18023e3a0 (asm), find_orient_angle / df_calc_orient_ang / DfCheckAngle | end of `dc_calc_linear` |
| placement point, text offsets, anchor | FUN_18023d590 case 0 (core_calcdim.c 3618), FUN_18023dad0 / FUN_18023e0b0 (core_createport.c 3 / 79), calc_linear_angle @18024c280 (asm) | `placement`, `offsets`, `dc_linear_angle` |

Decoded on the way (not in create.md / draw.md before):
* **FUN_18030f410 runs twice.** calc_linear_dimension formats the *view distance* |P1P2| (asm 0x18024cc0a-0x18024cd10: value pointer =
  local_2f8 before it is reassigned) and its extents feed FUN_18024fc30; FUN_180252850 formats the measured length (dx / dy / distance)
  into the same global buffer DAT_1813d5690 (or, when `DAT_1813d6212` = dd_global+0x1a is set, measures the buffer as it is).
* **Parallel dimensions use the raw view angle**: for orientation 2 the text-angle local (-0x70(%rbp)) is overwritten with
  atan2(P2 - P1) in [0, 2 pi) of the (swapped) view points, without DfHvpAngle (asm 0x18024cd41-0x18024cd8b); this angle drives
  FUN_18024fc30 and the whole layout.
* **The record keeps the pick order** of the aPoints (+0x70 / +0x108 copied before the swap); only calc's view copies and the
  caller's aPoints are swapped (the caller swaps them back, cad_linear.c 197-207).
* **One-sided placement** (FUN_180250410, asm 0x1802505f7-0x18025086e): unless Ctrl is held, when the witness mode or the leader
  mode includes the side nearer the *original* cursor, the text is moved to `lo` or `hi` around that witness (`lo/hi = W -/+
  out9..out11` from FUN_18024fc30 by arrows in/out), nearest to the cursor, or forced to the side when 2.1 x arrow + text width > length.
* **FUN_180255b80**: leader style (dd_global+0xa) != 0 always takes FUN_180257270 with the line under the text box (min box y - pad);
  style 0 takes FUN_18024b250 at the text y, and adds a 2-point segment to the text box (FUN_18024b570) when the text is outside and
  the text is horizontal (`DAT_1812d980c` set), on leader 1 or 2 by side / leader mode / arrows. The "perpendicular through A"
  shift (constant param_8 = pi/2 plus -pi/2 at 0x180d33538 = angle 0) is always 0 in practice.
* **record <- globals is 1:1**: DraftGlobalsFromDim @180308cc0 (core_draftwf.c 540-700) sets every global calc_linear_dimension
  reads from the record: `DAT_1812d980c` (text orientation) = |text angle +0x5e8| < tol, arrow size / style from leader 1 if on with
  an arrow else leader 2 else leader 1, witness / leader modes = the u32 at +0x7ac (df_linear_format is 1 for linear), arrows in/out
  +0x7b0, leader style +0x7b4, digits / zeros / comma / formats, font_attr fields. The oracle uses exactly this mapping.
* **Regeneration** FUN_1803049e0 (core_dimlayout.c 2980) calls calc_linear_dimension with DfHvpAngle = +0x1a0, orientation +0x68,
  value mode +0x1a9 and the cursor = anchor point (+0x1a8) + text offsets (+0x360/+0x368, depth +0x370) rotated by calc_linear_angle
  (or the placement +0x310 under DAT_1813d8cd2); the oracle drives the port this way.
* Older records: +0x7ac/+0x7ae = 0xffff (271 linear records) would make every witness and leader invisible if regenerated by this
  code (`((m - 1) & 0xfffd) == 0` is false for 0xffff), so they were laid out by an older Mastercam and never regenerated.

## 2. Oracle results (corpus `<mastercam-samples>/net`, fonts `~/mastercam-re/files/font0..7.fnt`)

```
linear dimensions: 985 in 155 files; skipped: view >= 9 8 (the oracle reads only views 1..8), NOT PORTED 0
2017-written records: compared 706, fully equal 533; anchor tie 16, equal with Ctrl held 46; remaining 111
older records (0xffff modes, measured with 3 / 3): compared 271, fully equal 0, equal with Ctrl held 126; remaining 145
```
"Fully equal" = valid flag, value +0x338 (1e-6 rel), text string +0x650, text position +0x638 and font angle +0x5e8, orientation
+0x68 and +0x1a0, the four v8_wit_ldr (on, kind, visible, has-arrow, arrow type, count, arrow h / w, every point within 1e-6),
placement +0x310, offsets +0x360..+0x370, anchor +0x1a8. Corpus coverage: witness / leader modes 3/3 (2 records 0/3), leader style
0 only, DfHvpAngle 0 only, text orientation horizontal in all but 6, tolerance mode 0, fixed and proportional Stick font - so
FUN_180257270 (leader style 1), the rotated-H/V branches and the one-sided witness modes are ported but not exercised.

Categories of the 2017-written differences (measurements, each with a temporary run; nothing of it is in the port):
1. **Anchor tie (16)**: text exactly between the definition points; FUN_18023d590 keeps P1 when d1 <= d2 and the last bit decides
   (e.g. 3D WEDGE.mcam rec1: offsets (-2, 2.244) ours vs (2, 2.244) stored, anchor 2 / 1).
2. **No one-sided placement (46 + 126 older)**: equal only when Ctrl is taken as held (FUN_180250410 skips the placement). E.g.
   CREATE_ARC.mcam (Lathe courseware) rec1: text centred at x -9.125 stored; the decompile moves it to -9.166 (`lo` = 2nd witness -
   (text/2 + pad + arrow)). The other way also occurs: 10 records are equal only *with* the placement (Router CREATE_ARC rec0/2,
   CREATE_RECTANGLE rec3 ...). Why these 46 were made without it (Ctrl, another edit path, an older version - all 126 matching older
   records behave the same): NOT DECODED.
3. **Text 1/255 cell wider (64 of the remaining 111)**: the text box is wider by exactly h x aspect / 255 (e.g. BREAK.mcam rec0
   '0.50': ldr1 (1.34504, -5.04661) ours vs (1.34528, -5.04661) stored; CHAMFER.mcam rec0 '0.250'). Adding 1/255 cell to every
   proportional string reproduces 64 of them (courseware files of X9 / 2017 / 2018) and breaks 152 others (Handbook files): two
   populations, consistent with a different stroke-font file version; NOT DECODED.
4. **Definition points do not give the stored value (8, SURFACING_7/8)**: free `K` points, value 1 / 2 / 6 stored vs 0.563 / 1.126 /
   3.378 measured (records.md 5 already lists them); orientation +0x68 differs in 2 more of those files.
5. **Record not self-consistent (3, CONICAL HELIX DRAWING rec4)**: stored text (13.56, 3.99) is not anchor + offsets (-3.06, 3.99).
6. The rest are combinations of 2 and 3 (e.g. DRAWING20.mcam rec1: centred text + a wider box), and one where the original cursor
   decides the side of the one-sided placement (Router CREATE_ARC rec1: the regenerated cursor is already the moved text).

Older records: with the modes read as 3/3 and Ctrl held, 126 of 271 are equal; the rest differ mostly in the text (125: trailing
zeros made under another session setting, as draft.c's value check found) - not a 2017 layout.

## 2b. The creation path (2026-10-26): `dc_init_dim` and `-f`
`dc_init_dim` ports InitDIM / FUN_180249820 / InitDimWL / InitDT / InitDimFA / InitDM (create.md 7.4) - the empty record the drag
entity starts from at creation; `dc_attribs_dim` = init_attribs_dim; `dc_apoint_world` / `dc_apoint_ent` = aPointInitWorld (without
the 'O' branch) / aPointSaveEntSel; `dc_line_hv` = determine_h_or_v. `draft_create_test -f` runs the oracle from dc_init_dim (the
record's globals as DraftGlobalsFromDim sets them, + the tolerance height / tolerances / DIN / units InitDIM reads) instead of a copy
of the stored record: **533 / 706 fully equal, the same records as the regeneration**, and lists the bytes still differing in those 533
(create.md 7.4). The app (viewer/core_ui/cui_draft.c) uses exactly this path.

## 3. NOT PORTED / NOT DECODED
* Tolerance modes 1-3: FUN_18030f410's tolerance strings and FUN_180251460 slots 1-2 are decompiled but not ported (`not_ported`);
  corpus has none. Coordinate formats 2-4 (engineering, fractional, architectural) not ported (draft.c has the same limit).
* TrueType text extents (FUN_180316d40), the demo-edition two_sig rounding (GetProdEnableBits), DraftIsAssoc / DimIsAssoc (the
  associativity byte), FUN_1802579d0's graphics-view fallback (view 0), InitDIM / FUN_180249820 defaults for bytes calc does not
  write (the regeneration starts from the old record; the port writes into the caller's buffer as is).
* `DAT_1813d6212` (dd_global+0x1a) name / config item: NOT DECODED (ported as `keep_text`; no record needed it). `sel_dist` (the
  centring snap radius) is a session value; the oracle uses 0.
* Stack bytes Mastercam leaves in the wl slots (points beyond the count, +0x1c..+0x1f, +0x50..+0x83) are zero in the port.
* Open: category 2 (when the one-sided placement is skipped) and category 3 (the 1/255 cell). A GetAsyncKeyState-free regeneration
  path, or the font file of the courseware's Mastercam version, would settle them.

## Ordinate (viewer/draft_ord.c, 2026-10-27)
| Mastercam (mccore) | cite | here |
|---|---|---|
| calc_ordinate_dimension @18024d3d0 | core_calcdim.c 1234-2070; decomp/draft/ord_core_calc_asm.txt (the decompile drops the FUN_18030f410 / get_ext / FUN_180251460 / make_wl_single arguments and several stores) | `do_calc_ordinate` |
| Point2Plane, dist_pt_l, l_offset, Equal, proj_pt_ln (systypes) | core_calcdim.c 6567 / 6414 / 6520, ord_core_helpers.c 1634, mccore_filter.c 4912 | static helpers |
| FUN_18030f410 kind 5, get_ext, box_limits, make_wl_single, FUN_180252790 | the linear port's (draft_create.c, exported as `dc_value_text`, `dc_text_ext`, `dc_box_limits`, `dc_make_wl`, `dc_slot_font`) | shared |
| FUN_180251460 (tolerance mode 0), FUN_18023d590 case 3 (FUN_18023dad0, FUN_18023e0b0 with angle 0) | core_calcdim.c 6585, 3618-3720; core_createport.c 3-117 | inline / `placement` |
| DfOrdType, DraftGlobalsFromDim, FUN_180255830, the regeneration FUN_1803053a0 | core_draft_rec.c 715; core_draftwf.c 540-716; ord_core_helpers.c 1288; core_dimlayout.c 3068-3160 | `do_ord_type`, `do_globals_from_dim`, `do_text_ends`; the oracle's inputs |
Oracle `tools/tests/draft_ord_test.sh` (RUN=1; FLAGS=-f / -v): the 2017 / 2018-written ordinates 102 / 102 fully equal (value, offset,
text, text slot, angle, leader 1, other slots off, placement, offsets, parent eptr, +0x122 / 124 / 125); X9 24 / 51. `-f` (InitDIM's
empty record): the non-orphans equal (90 / 90), differing bytes create.md 8.6. NOT PORTED (refused with `not_ported`): tolerance modes
1-3, a base with %v tokens or tolerance strings, view 0, TrueType extents. Corpus coverage: create.md 8.6 (leader style 1, signed
values, arrowhd 0 and the parallel orientation are not in the corpus).

### Align (pass 2)
| Mastercam (mccore) | cite | here |
|---|---|---|
| df_load_oc @1802434d0 (the entries) | ord_core_helpers.c 499 | cui_draft_ord.c `al_pick` |
| FUN_1802423f0 (order, the dragged box) | decomp/draft/ord_core_oc.c | `do_oc_order` |
| df_adjust_oc @180240fd0 | ord_core_helpers.c 203; its asm ord_core_oc_asm.txt (the parallel row direction is (+dy, -dx): asm 0x1802411db-0x18024120f) | `do_oc_adjust` |
| move_ordinate_dimension @180254d40 | ord_core_helpers.c 948 | `do_move_ordinate` |
Oracle `draft_ord_test -a`: 20 / 60 corpus entries re-placed equal (create.md 9.3).
## 4. Circular (2026-10-27): calc_circular_dimension -> `viewer/draft_create_circ.{h,c}`

| step | function (decomp / asm) | port |
|---|---|---|
| style switch, quadrant style (quad_to_eps + calc_linear_dimension, the centre aPoint over the linear ones) | calc_circular_dimension @18024b810 (core_calcdim.c 672-795), quad_to_eps (5009), FUN_180245600 / FUN_180245a40 (circ_core.c 948 / 891) | `dc_calc_circular`, `quad_to_eps`, `quads` |
| style choice | determine_cl @18023ff80 (circ_core.c 755), pt_on_arc (decomp/pocket_b6/zigzag_decomp_zz_sys.c 3), df_angle_of_line (asm 0x180241a00) | `dc_determine_cl`, `pt_on_arc` |
| radial header, centring snaps, forced arrows | FUN_18024f3c0 (core_calcdim.c 4816-4970) | `dc_calc_circular` |
| layout | FUN_180250f60 (core_dimlayout2.c; asm 0x180250f60-0x18025124c), FUN_18024a3c0 (core_dimlayout.c 1318; asm circ_asm_18024a3c0.s) | `f24a3c0` |
| attachment / angle | FUN_180251840 (asm circ_asm_180251840.s) | `f251840` |
| leaders | FUN_180256700 (circ_core.c 2; asm circ_asm_180256700.s), FUN_180254bc0 (draft_create.c) | `f256700` |
| text box | FUN_18024c160 (core_dimgeo.c 3) | `box4` |
| record | FUN_180253540 (core_dimlayout.c; args asm 0x180251183-0x18025124c), FUN_1802579a0, FUN_180251460 slot 0 | `f253540` |
| placement / offsets | FUN_18023d590 case 2, FUN_18023dad0 / FUN_18023e0b0, FUN_18023de60 (0 for type 2) | `placement`, `offsets` (draft_create.c, now type-aware) |

The helpers shared with the linear port are exported from draft_create.c through `viewer/draft_create_int.h` (dcx_* names): the linear
oracle is unchanged by the split (533 / 706).

### 4.2 Oracle `tools/tests/draft_circ_test.{c,sh}` (RUN=1, VERBOSE=-v)
Inputs as the regeneration FUN_180303e20 (core_dimlayout.c 2852-2950) takes them: the arc by DfGetCircularArc (the aPoint's eptr ->
the file's arc, else rebuilt from +0x70 / +0x320 / +0x118..+0x128), the cursor = centre + the offsets (+0x360 / +0x368; angle +
DfHvpAngle +0x108, FUN_180306330 taken as a [0, 360) normalisation - NOT DECODED; radius grown by the arc's change), depth +0x370; style
+0x100, diameter +0x101; globals as draft_create_test (DraftGlobalsFromDim), the leader mode = the stored +0x7ae.
```
circular dimensions: 393 in 112 files; skipped 3 (view >= 9)
2017-written records: compared 164, fully equal 132; with the creation cursor (leader style 1: text - pad) 1; remaining 31
older records (+0x7ae / +0x7b0 = 0xffff): compared 226, fully equal 0
determine_cl at the text point of the 2017-written records: radial 156, quadrant style 8
```
The 31: 15 equal when every proportional string is 1/255 cell wider (temporary run; the same run breaks 101 others - linear's
category 3), ~10 of the same family with 2/255 (one glyph), 6 SURFACING_7/8 (not self-consistent: value 0.75 vs geometry 0.422, a
leader 2 that is on with 0 points). Older: a 3-point leader 1 where this code makes 4 (an older layout). Coverage: leader style 0 (163)
and 1 (1), horizontal text only, tolerance mode 0, the radial style only (the quadrant style has no corpus record) - the aligned-text
and leader-style-1 branches of FUN_180251840 / FUN_180256700 are ported from the decompile but not corpus-checked (the user's metric
config has both: 12004 = 0, 14005 = 1).

## 5. Angular (2026-10-27): calc_ang_dimension -> `viewer/draft_create_ang.{h,c}` (in the app since pass 2: create.md 10)
Port of calc_ang_dimension @180021a40 -> FUN_1800226b0 -> FUN_1800206f0 (its arc branch table 0x180020d94-0x180021353 incl. the pigtail
branches from asm `decomp/draft/ang_asm_1800206f0.s`) -> FUN_180022d40 (record) -> FUN_18023d590 case 1 -> FUN_180020470; helpers
FUN_180022470, FUN_180023440, FUN_180021f00, l_ints with its t, la_ints, proj_pt_ln, extend_vlines; df_angular_set_ep_angles @180241fa0
(`dc_ang_set_ep`), determine_ang_quad (`dc_ang_quad`), FUN_18023f360 / ang_code2quad, the regeneration cursor FUN_180020320
(`dc_ang_regen_cursor`); the angle text (FUN_18030ec10) as viewer/draft.c's fmt_ang. Decompiles `decomp/draft/ang_core_a1.c`,
`ang_core_a2.c`, `ang_cad_a1.c`, `ang_cad_a2.c`, `ang_asm_1800226b0.s`, `ang_asm_1800206f0.s`. Inputs not in DcSettings (DcAngInput):
the angle format (config 11000), num_digits_ang (11019), the angular tolerance mode / values (11007 / 11020 / 11016), pigtail (14011).

Oracle `tools/tests/draft_ang_test.{c,sh}` (as FUN_180303b70 regenerates; 172 records, 4 skipped view >= 9):
```
2017-written records: compared 144, fully equal 85; layout = 25.4 x the aPoints 9 (TUTORIAL 4 METRIC: a unit conversion that scaled the
  layout, not the aPoints / +0x2e0), equal with the text point about the stored vertex 6 (the regeneration's vertex comes from the
  unextended lines, calc's from the extended ones: ~tol apart); remaining 44 (27 the n/255 text width, 3 stored ratio -0.5 that
  FUN_180020470 cannot give, 2 edited texts, the rest combinations + a stale stored vertex)
older records (0xffff modes, read as 3 / 3): compared 24, fully equal 14
```
Coverage: leader style 0, horizontal text, no pigtail in every 2017 record. The button flow (5.3) is wired (create.md 10; the two
pieces left open, message 0x6987 and get_angle, decoded there; decompiles decomp/draft/dim2_core.c, dim2_mastercam.c).

### 5.3 The Angular button (mccad; ang_cad_a1.c / a2.c, cad_linear.c)
FUN_180040ed0 type 1; pick 1 (FUN_180049c60: flags 0xb, mask 7; prompt 27720 "Create dimension, angular:" + 27721 "Select an arc
(non-circle) to create the angular dimension" + 27722 "Select a line or the pivot point"): a line ('1' / '2' with t 0 / 1) -> flags 0xb
mask 3, 27723 "Select another line (non-parallel) ..." + 27728 "Select base point for relative method"; a second line (FUN_18004b070)
-> l_ints, else 27832 "Warning - intersection not found" -> FUN_180042a20(slots, 0); a point instead -> 27833 "Enter angle in degrees"
(`get_angle`: NOT DECODED) builds the second line; a point first = the 3-point method (vertex, 27710 "Select the second point", 27724
"Select the final point ...", flags 9) -> FUN_1800426f0 -> FUN_180042a20(slots, 1); an arc (FUN_18004a620 case 4): |sweep| >= 2 pi or
~pi -> 27834 "Warning - cannot dimension a circle", 'C' / '1' / '2' slots, FUN_1800422a0 (sweep > pi, chord middle). FUN_180042a20 /
FUN_180042750: InitDIM, vlines off, DfHvpAngle 0, +0x2e8 = 0xc, FUN_180042bf0 orders the aPoints, df_angular_set_ep_angles (27258
"Warning - cannot calculate angular orientation"), quadrant ang_code2quad(0xc) (an arc: lock on, arrows inside, flip = sweep > pi,
quadrant of the chord middle). Placement: 27720 + 27055 "Sketch the text position for the angular dimension" + 27070; each move (FUN_180041c20)
determine_ang_quad(vertex, cursor) unless locked, calc_ang_dimension(..., flip, df_ang_add_pigtail); 27010 when invalid. Accept FUN_180041f30(1)
= df_save_drag_ent in one UndoTrans. Panel (FUN_180042620): 7 lock, 0x10 arrows + flip, the rest FUN_18012af20. Record: type 1, aPoints
+0x68 / +0x100 / +0x198 / +0x230, vertex / depth / radius +0x2c8..+0x2e0, quadrant code +0x2e8, flip +0x2e9, cursor quadrant +0x2ea,
ratio +0x2f0, pigtail +0x2f8, arcs (kind 2) +0x4a8 / +0x540 (the second reversed), witnesses +0x378 / +0x410. NOT DECODED: FUN_180242fa0
(message 0x6987, raised by ang_code2quad(0xc) at every start), `get_angle`, the 'C' aPoint's t for arcs (stack garbage in Mastercam).
