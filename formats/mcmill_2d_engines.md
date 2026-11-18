# mcmill.dll 2D engines: Slot, Helix Bore, Circle Mill, Thread Mill, Drill cycles

Decoded from `mcmill.dll`. The full decompile is `decomp/mcmill_all.c` (Ghidra project `~/mastercam-re/ghidra` /
`mc2017`), and functions are cited by name and address. The NCI drill record writers are in `mccore.dll` (Ghidra
`~/mastercam-re/ghidra_core`). Anything without a source is marked **NOT DECODED**.

## 0. Conventions

### 0.1 Offsets
- The engines read `cur_op_exref`, an `operation` struct. Its offset **o** is part-file record offset **o + 0x68**,
  because the 0x68-byte entity header comes first (`formats/mcam.md` "Operations").
- Evidence for the 0x68 shift:
  - `bnci_slotmill` copies `local_2a40` = record + 0x68 into `cur_op`.
  - The tool diameter `cur_op+0x2e8` is record 0x350, which is parameter 10005.
- Every offset below is the **record offset**, the one `viewer/cam_fileop.c params_from_record` uses, followed by the
  MP parameter number and description from `formats/opparams/` (`param_reference.tsv`, the per-opcode case in
  `op_params_by_case.tsv`).
- The MP descriptions are generic (several say "Circle toolpaths"). Where `cam_fileop.c` already has a dialog label for
  the same offset, both are given.

### 0.2 Common inputs (all engines)
| record offset | param | meaning |
|---|---|---|
| 0x350 | 10005 | tool diameter D |
| 0x358 | 10006 | tool corner radius |
| 0x2e0 | 10010 | XY stock to leave |
| 0x2f8 | 10007 | compensate to tip |
| 0x378 / 0x380 | | feed / plunge rate |
| 0x390 | | rpm |
| 0x5d0 | 15346 | compensation type (`cc_in_computer` / `cc_in_control` / `cc_in_control_only`) |
| 0x5d2 | 15347 | compensation direction (0 left, 1 right) |
| 0x868 | 15211 | depth cuts on |
| 0x870 | 10065 | max rough step |
| 0x878 | 10066 | number of finish cuts |
| 0x880 | 10067 | finish step |
| 0x891 | 10069 | keep tool down |
| 0x179 | 15100 | clearance plane on |
| 0x189 | 15101 | retract plane on |
| 0x319 | 15339 | use clearance only at start / end |

### 0.3 NCI output primitives (mccore imports)
| function | writes |
|---|---|
| `gr_lin_mill(file, g, pt, feed, z, cc, …)` | g = 0 rapid (feed −2.0, or `init_feedrate` when product bit 0x10 is set) or g = 1 linear |
| `gr_arc_mill` / `gr_circle_mill` | arc records |
| `plunge_move` / `retract_move` / `clear_move` | the op's plunge, retract and clearance moves |
| `bnci_wr_1018` / `bnci_wr_1019` | subprogram start / end |
| `bnci_wr_81` / `100` / `80` | canned cycle first point / further points / end |
| `bnci_wr_4` | NCI 4 |

The record layouts are in `formats/mcam.md` "Binary NCI tail". The internals of the writers are **NOT DECODED**
here, except `plunge_move` 0x18089ed00 / `retract_move` 0x18089f390 / `clear_move` 0x18089e6a0 and `set_depths`'
init_depth (decomp/mccore_moves.c, decomp/mccore_depths.c; improvisation_review.md CAM small items part 2 (h)): their
rules are stated with the port (`mw_plunge` / `mw_retract` / `mw_clear` / `mc_init`) in section 7.

### 0.3a Compensation-type tests (mccore.dll)
Read from the disassembly of `~/mastercam-re/files/mccore.dll` (exports by name):
| function | address | true for 0x5d0 = |
|---|---|---|
| `cc_in_computer(t)` | 0x18009de10 | `(t & 0xfffc) == 0 && t != 1`: 0 computer, 2 / 3 wear |
| `cc_in_control(t)` | 0x18009de30 | `(u16)(t − 1) <= 2`: 1 control, 2 / 3 wear |
| `cc_in_control_only(t)` | 0x18009de40 | `t == 1` |

Type 4 (off) is none of them.

### 0.3b Where lost arguments come from
Where Ghidra dropped a stack argument or an XMM argument, it was read with `objdump -d` from
`~/mastercam-re/files/mcmill.dll` (the binary of the Ghidra project). Such facts are cited below as **dis
<address>**.

The port (`viewer/cam2d_ops.c`) uses these values for the system tolerances the engines compare against (mccore
globals tol_exref / nc_tol_exref / min_arc_length_exref = config 1000 / 1008 / 1003, defaults of mccore FUN_1803bc7d0,
decomp/mccore_systol.c; improvisation_review.md CAM small items part 1 (a)):
- tol = 5e-5 in both units (`MC_TOL` = cam2d.h `CAM_SYS_TOL`);
- nc_tol = 0.002 metric / 0.0001 inch (`MC_NC_TOL` = cam2d.c `g_nc_tol`, set per part by `cam_generate`);
- min_arc_length = 0.02 metric / 0.0002 inch (`MC_MIN_ARC` = `g_min_arc`).
(Before 2026-10-16 the inch values 1e-4 / 0.001 were used for every part.)

### 0.4 Depth passes: `set_depths` and the depth-cut table
Used by Slot (`FUN_180218e00`) and Circle Mill (`bnci_circmill`).
1. `set_depths(op, z_geom, &zf)` gives the final depth zf. It is a mccore import, since decoded as set_depths
   0x18089f680 (decomp/mccore_depths.c; abs / inc heights relative to the chain Z, see improvisation_review.md UI item
   5). The port uses CamParams `final_z` / `top_z` evaluated once (at the first chain's Z).
2. T = top_of_stock, with −1e38 meaning 0.
3. If depth cuts (0x868) are on:
   - nf = max(0, S16 0x878) and fs = 0x880.
   - dz = (T − zf) − nf·fs.
   - If dz > 0: nr = max(1, floor(dz/0x870 + 0.999999)) and rough = dz/nr. Otherwise nr = 0.
   - If nf = 0 and nr = 0: nf = 1 and fs = 0.
4. If depth cuts are off: nr = 0, nf = 1, fs = 0.
5. The pass count is nr + nf.
6. **Pass i** (0 = top) is at z = zf + off(i), from `FUN_180218340` (slot) or `FUN_1800412d0` (circle mill, identical):
   - k = nr + nf − 1 − i
   - a = max(0, k − nf)
   - off = (k − a)·fs + a·rough

## 1. Slot Mill (opcode 105) — `bnci_slotmill` 0x180216f80

### 1.1 Entry
- `bnci_slotmill` requires `product_level_ok(4)`, then calls `set_slotmill_prm` (0x1802183a0). That sets
  `P = cur_op+0x11c0`, i.e. **P[k] = record 0x1228 + k**.
- **External engine hook.** If `<apps_prefix>slot9.dll` loads (`load_special_app`), the op is handed to it and nothing
  below runs.
- It reloads the op (opcode must be 0x69) and calls `subpgm_set` and `set_n1018`.
- It sets **ramp stepover 0x1270 = 0x1268 [%] · 0.01 · D**.
  - 0x1268 = 12387 "Ramp stepover as a percentage of the tool diameter"; the `cam_fileop` label is "Stepover [%]".
- It loads the chains (`init_chaining` 0x44, `chains_from_db`).
- If `prm_hem::OverrideFeedsAndSpeeds(op+0x12e8)`, the feed and rpm are temporarily replaced by 0x1308 and
  round(0x1310), and restored at the end.
- Each chain goes to **`FUN_180218e00(chain, last_chain)`**.
- If any chain was not a slot: warning 0xa5f.

### 1.2 Per chain — `FUN_180218e00`
1. **Plunge-angle check.** If ramp is enabled (P+0x3a = 0x1262, 12386 "Enable ramp entry") and the plunge angle
   0x1278 (12389) < 2e-7: warning 0xa60 and return.
2. **Boundary.** `chain_to_pock_bound(chain, max(0x1228, tol))`, then `FUN_180217ff0` merges consecutive collinear lines
   (`FUN_180216d60`: both `'L'`, |cos| − 1 ≤ 2e-7, offset ≤ tol).
3. **Slot test** (`FUN_180216990`). Take the two longest lines. They must be parallel (||cos| − 1| ≤ 2e-7).
   - The slot width **W** = `dist_pt_l` between them.
   - Otherwise the chain is drawn in colour 12 and skipped (not a slot).
4. **Start point** (`FUN_180217520`, "SLOTMILL.cpp" `break_slot_boundary`). The boundary is rotated to start and end
   at the **midpoint of the first long line**.
5. **Direction.** side = 'L' when 0x5d2 = 0, else 'R'. If `pock_ccw` disagrees with the side, the boundary is reversed.
   It is then converted to a ctour (`pock_bound_to_ctour`).
6. **Depths**, per §0.4.
7. **XY passes.** c = D/2 if `cc_in_computer`, else 0. s = 0x2e0.
   - Finish allowance F = n_f·f_s, with n_f = S16 0x1290 (12392 finish passes) and f_s = 0x1298 (12393 finish stepover).
   - A boundary offset by (f_s, or D·0.01 when n_f < 1 or f_s ≤ nc_tol) + c + s is made via `FUN_180217f20`.
     That is `ccomp_pock` with 0.785398 rad. Its output parameter is lost in the decompile.
   - Rough base offset **b = D/2 + s + F**. Remaining width **Wr = W − 2b**. If Wr < 0 the chain fails.
   - Rough loop count **n = floor((Wr/0x1270 + 1)/2 + 0.999999)**. Actual stepover **st = Wr/(2n − 1)**.
8. **For each depth pass i** (z from §0.4):
   1. **Ramp.** If 0x1262 is set and n > 0:
      1. d = (n − 1)·st + b. It must be < W/2 − nc_tol.
      2. The ctour is offset inward by d (`ccomp_pock_from_ctour`).
      3. `FUN_1802178c0` ("SLOTMILL.cpp" `create_ramped_ctour`):
         - The ramp's top is T for the first depth pass and **the previous pass's z after that**: FUN_180218e00 passes
           `dVar28` (= top of stock at first), and sets `dVar28 = dVar27` (the pass z) at the end of every pass. It
           returns −1 (nothing output) when top − z ≤ 1e-12. The innermost loop is skipped either way.
         - Laps = floor((top − z)/(perimeter·tan(0x1278°))) + 3.
         - Walk the reversed loop from z upward; each segment climbs length·tan(angle).
         - The segment crossing T is cut at T (lines interpolated, arcs shortened).
         - Reverse again so the tool descends from T, then append one full loop at z.
      4. Output with `FUN_180218410`. The innermost loop is then not repeated.
   2. **Rough loops**, j = n − 1 … 0 (innermost first, working outward):
      1. Offset = j·st + b. It must be < W/2 − nc_tol.
      2. After the first loop, the step from the previous loop's end E is **`FUN_180215d90`**. It inserts a line to E,
         then two half-circle arcs: E → Q = E + u·d (centre at the midpoint), then Q → S, the new start (centre (Q+S)/2),
         where u = unit(E − S). **d = the actual stepover st = Wr/(2n − 1)**: param_5 is passed in `xmm9`, which is set by
         `movaps %xmm8,%xmm9; divsd` = Wr/(2n − 1) (dis 0x1802192ff..0x18021932d). The finish-pass call site
         compares the same register with 0 (`0 < st`) just before the call (dis 0x1802199c4..0x1802199ec). The arc
         direction byte is `param_6 == 0`, with param_6 = [rsp+0x72] = (0x5d2 == 0) (dis 0x180218fff). That gives
         CCW arcs for left compensation and CW arcs for right (1 = CW, as in `FUN_1802189e0` and `FUN_180218130`).
      3. **Last rough loop** when there are no finish passes (j = 0 and n_f = 0). The lead is set up by writing op
         fields:

         | record | value |
         |---|---|
         | 0x991 | 0 (gouge check off) |
         | 0x900 | 0 |
         | 0x941 / 0x948 | 1 / r_l when 0x1261 (12209 "perpendicular entry line") is set, else 0x948 = 0 |
         | 0x910 / 0x918 | 0 |
         | 0x958 | r_l (exit arc radius) |
         | 0x960 | a (exit sweep) |

         - a = 0x1248 (12206 "Entry/exit arc sweep", degrees → radians).
         - **r_l = Wr·(0.5 if a ≤ 1.5807963 rad, else 0.25)**.
         - The 0x941 flag is the op +0x8d9 exit-perpendicular flag. 0x948 is the exit line length.
         - Then `FUN_180216080` builds the lead (§1.4). Its param_5 is the side char 'L'/'R' (`dil`), and **param_6 =
           param_7 = (1e-12 < sweep 0x1248)** = [rsp+0x78], set by `seta` (dis 0x18021920f, 0x18021976b). With
           sweep 0 the builder therefore only does its 0x1250 extension.
      4. Every ctour record gets z, then `FUN_180218410` outputs it.
   3. **Finish passes**, k = 1 … n_f:
      1. Offset = (n_f − k)·f_s + c + s. Used if it is > 1e-12 and < W/2 − nc_tol; otherwise the un-offset ctour is used.
      2. If 0x1248 ≤ 1e-12: connect with `FUN_180215d90` (when st > 0), then output.
      3. Else: lead fields as above, but with r_l = (W − 2·offset)·(0.5 / 0.25), entry and exit both set
         (0x8f9 = 1, 0x900 = r_l, 0x910 = r_l, 0x918 = a, 0x958 = r_l, 0x960 = a), then `FUN_180216080`.
         With 0x1261 set, the entry line is turned on as well: op +0x891 = 1 (record 0x8f9) and op +0x898 = r_l
         (record 0x900), plus 0x941 = 1 / 0x948 = r_l. Without 0x1261 both line lengths are 0 (dis 0x180219a3d..).
         param_6 = param_7 = (1e-12 < sweep) again (dis 0x1802199a3).
         **Correction:** `FUN_180216080` returns 0 on success, *after* adding the leads to the ctour, and the pass is
         then output with them (`if (sVar8 == 0) goto LAB_180219b0f`). A nonzero return (gouge check) skips the
         output. There is no link (`FUN_180215d90`) from the previous loop in this branch: the pass starts with
         `FUN_180218410`'s G1 to its first record at the plunge feed.
9. After the chain: `clear_move` if 0x179 is set and (0x319 = 0 or this is the last chain).

### 1.3 Pass output — `FUN_180218410(ctour, n, …)`
ctour record (0x60 bytes): +0 end point x,y; +0x10 centre; +0x20 dir; +0x21 `'L'`/`'A'`; +0x38 entity; +0x40 z.
1. **First move of the chain.**
   - Rapid G0 to record 0 at the current height, unless already down.
   - Then `plunge_move` to z.
   - Then G1 to record 0 at the **plunge feed** (0x380).
2. **Each record:** G1 at the feed rate (0x378), or an arc via `FUN_1802189e0`.
   - If 0x1280 (12390 "Output helixes as arcs") is set: `gr_arc_mill`.
   - Otherwise the arc is linearised with angle step √(8·0x1288/r) (12391 "Helix linearization tolerance").
3. **Control compensation.** Record 2 carries `cc_control_gcode` (G41/G42) and the last record carries 140 (G40) when
   requested.
   **Requested by** (batch 7, dis): param_11 / param_12 = 0 for the ramp (0x180219521) and the rough loops (0x180219805);
   `cc_in_control(0x5d0)` for the finish passes ([rsp+0x7c], set at 0x180218fe8 from the call at 0x180218fcb, passed at
   0x180219b76..0x180219b82). Ported in slot_chain behind MCAM_CUTTER_COMP=1 (formats/post.md); FUN_1802189e0 writes flat arcs
   as arcs always and helical ones as arcs when 0x1280 is set, else as lines at an angle step sqrt(8 x 0x1288 / r). Ported for the
   ramp in decisions batch dec1 (viewer/cam2d_slot_arc.c: create_ramped_ctour on the exact line / arc loop - an exact convex offset,
   ours, else gls_offset - with the collinear-line merge, the cut at the top (line interpolated / arc sweep x f, kept > nc_tol), and
   FUN_1802189e0's n = floor(|sweep| / step) + 1 equal-angle lines). The rough and finish loops still walk slot_loop's polygon
   (the post fits their arcs); SLOT_RAMP_POLY=1 (testing) = the old polygon ramp.
4. **Subprograms.** 1018 / 1019 around the marked records.
5. **End.** `retract_move` at the end when asked; otherwise the z is kept.

### 1.4 Lead builder — `FUN_180216080` ("SLOTMILL.cpp" `add_lead_in_out`)
Arguments: ctour, &count, the `FUN_180217f20` boundary (param_3), the slot boundary (param_4), side 'L'/'R' (param_5),
lead-in on (param_6), lead-out on (param_7). Lead parameters are operation fields (record offset = op + 0x68):

| piece | length / radius | sweep | perpendicular flag | ramp height (+0x78) |
|---|---|---|---|---|
| entry line | op +0x898 (rec 0x900) | | op +0x891 (rec 0x8f9) | op +0x8a0 (rec 0x908) |
| entry arc | op +0x8a8 (rec 0x910) | op +0x8b0 (rec 0x918), rad | | op +0x8b8 (rec 0x920) |
| exit line | op +0x8e0 (rec 0x948) | | op +0x8d9 (rec 0x941) | op +0x8e8 (rec 0x950) |
| exit arc | op +0x8f0 (rec 0x958) | op +0x8f8 (rec 0x960), rad | | op +0x900 (rec 0x968) |

Steps:
1. If P+0x28 (record 0x1250) > 1e-12 and the last ctour record is a line, its end is moved that much further along it.
2. Pieces on: the entry line if its length > 1e-12 and param_6; the entry arc if radius and sweep > 1e-12 and param_6.
   The exit pieces work the same way with param_7.
3. Each piece is made by **`FUN_1801c24a0(gt, exit, line, side, out)`** (0x1801c24a0):
   - P and T are the gt's start (entry) or end (exit) point and tangent (`eval_gt_pt` / `eval_gt_tan`).
   - For an entry, T is negated and the side flipped.
   - N is the left normal (−T.y, T.x), negated for side 'R'.
   - **Arc:** centre P + r·N, radius r. The start angle is the angle of P seen from the centre (atan2, [0, 2π)). The
     sweep is sw, negated for 'R'. An entry arc then gets start + sweep and −sweep, so that it ends at P.
   - **Line:** the direction is N when the perpendicular flag is set, else T. An entry line runs from P + len·dir to
     P; an exit line runs from P to P + len·dir.
   - The piece's +0x78 gets its ramp-height field.
4. Order:
   - Entry: the arc is built on the ctour's first gt, then the line on that arc.
   - Exit: the arc is built on the last gt, then the line on that arc.
   - The new ctour is: start record, entry line, entry arc, the old records, exit arc, exit line.
5. z values:
   - The record at the loop start keeps its z.
   - Going backwards, each entry record is the next one's z plus that piece's ramp height.
   - Each exit record is the previous z plus its ramp height.
6. The gouge check `FUN_1801c46f0` runs only when op +0x929 (record 0x991) is set. FUN_180218e00 clears it before
   both calls. Returns 0.

## 2. Helix Bore (opcode 106) — `bnci_helix_bore` 0x180142c80

### 2.1 Per point (`tp_get_all`)
1. `FUN_180143390` ("HlxBore.cpp get_pt_and_radius") gives the centre and the hole radius rh:
   - an arc picked at its centre ('C'): the arc radius;
   - a solid hole (tp kind 0x1b): the solid cylinder radius;
   - kind 0x1c: the stored value;
   - otherwise 0x1240/2 (12207 circle diameter).
   It also writes 0x1240 = 2·rh.
2. If rh ≤ D/2: string 0xa56 warning, and the point is skipped.
3. zf from `set_depths`. **zb = zf + (0x358 if 0x2f8 = 0, else 0)** (the corner radius when not compensating to the tip).
   T = top_of_stock (−1e38 → 0).
4. **Rough radius.** rr = rh − 0x2e0 − (D/2 if `cc_in_computer`) − (0x1288 if finish 0x1278 is on).
   - 0x1278 = 12403 "Output a finish pass".
   - 0x1288 = 12393 "Finish stepover" (`cam_fileop` label).
5. **Rough passes.** n = S16 0x1258 (12399), at least 1. If n > 1 and the rough stepover 0x1260 (12400) > tol:
   n = min(n, floor(rr/0x1260) + 1).
   For k = 1 … n: r = rr − (n − k)·0x1260; if r > tol, run **`FUN_180144ff0`(centre, r, T, zb)**.
6. **Finish**, if 0x1278 is set:
   - If 0x12c1 is set: `FUN_1801448f0`.
   - Else if 0x12c0 is set: `FUN_1801435a0`. These are params 41137 and 41131, with no description.
   - Otherwise **`FUN_180143fb0`(centre, rr before the finish subtraction, T, zf)**.
7. `clear_move` if 0x179, 0x189 and (0x319 = 0 or the last point).

### 2.2 Rough helix — `FUN_180144ff0(c, r, T, z)`
**Inputs.**
- dir = −1 (CW) if 0x5d2 = 1, else +1.
- Feed = 0x378. Bottom feed = 0x1270 (12402 "Feed rate at final depth").
- **Total angle Θ = (T − z)·2π / 0x1250**, where 0x1250 is the rough pitch (`cam_fileop`), param 12398. Θ = 0 when
  the pitch ≤ 1e-12.

**Point count.**
- If 0x12b0 (10393 "Output arc moves") = 0: m = |Θ|/√(8·0x12b8/r) + 1, at least 2 (0x12b8 = 12391 tolerance).
- Otherwise m = round(|Θ|/2π) + 1, at least 2.

**Start angle.** a0 = 0x1228° (12107 "start angle") − Θ, kept in [0, 2π).

**Entry.**
1. If start-at-centre (0x1248 = 12208) and |0x1230 − 180| ≥ tol: a point at the centre at z = T.
   (0x1230 = 12206 entry/exit arc sweep.)
2. If start-at-centre, control compensation, or 0x1249 (perpendicular entry) is set: a lead arc into (r, a0).
   - Lead radius = r/2, or 0.55·D when the compensation is control-only and D > r − tol.
   - Start angle a0 − dir·0x1230°.
   - The lead centre is Lc = c + (r − rl) at a0 (dis 0x180145329..0x180145346: `FUN_180145b40(c, r − rl, a0)`, then
     `FUN_180145b40(Lc, rl, a0 − dir·π/180·sweep)`).
   - The records are: a line to the lead start, then the arc to (r, a0) about Lc. **Correction:** with 0x1249 there is
     one more line *to Lc*, before the line to the lead start (dis 0x180145363..0x18014538a).

**Helix.**
- No arcs: m points, t = i/(m − 1), z = (1 − t)·T + t·z (the last point exactly z), angle a0 + t·dir·Θ, all G1.
- Arcs: full 2π arcs, each 0x1250 lower, then the remainder. A piece shorter than nc_tol / min_arc_length becomes a line.

**Bottom.**
1. A full circle at z at the bottom feed.
2. If the overlap 0x1238 (10407) > min_arc_length: an extra arc of angle dir·overlap/r.

**Exit.**
1. An arc **about** M = (end + c)/2, radius r/2, from the end to M + r/2 at **(end angle + dir·0x1230°)**. The end
   angle is atan2(end − c), plus 2π if negative (dis 0x1801458bf..0x1801459d0; π/180 = 0x18030a7d8, 0.5 =
   0x18030a800). The feed is the bottom feed 0x1270.
   In the control-only case with D > r − tol, the centre is c + (r − 0.55·D) at an angle held in [rbp−0x80]; that
   angle is **NOT DECODED**.
2. A line to the centre, at the bottom feed, if that is farther than nc_tol away.

**Output.** `FUN_180145ba0` (writer; its flags are **NOT DECODED**).

### 2.3 Finish helix (default mode) — `FUN_180143fb0`
- It uses the finish feed 0x12a8 (12408) and the **Z step per revolution 0x1280** (12404).
- Lead (dis 0x1801441c7..0x180144254):
  - The lead radius rl is r/2, or 0.55·D in the control-only case.
  - Lc = c + (r − rl) at a = 0x1228° (`restrict_angle(−2π, 2π)`). The start is Lc + rl at **a − dir·0x1230°**.
  - Records:
    - with 0x1249: a line to Lc, then a line to the start;
    - otherwise: a line to the start, only if the start is more than nc_tol from c;
    - then the arc to (r, a) about Lc.
- Exit, only when `cc_in_control` or 0x1248 (dis 0x18014469c..0x180144719):
  - Lc' = c + (r − rl) at the end angle (atan2, +2π if negative).
  - An arc about Lc' to Lc' + rl at (end angle + dir·0x1230°).
  - With 0x1249, a line to Lc'.
  - A line to c.
  - All of it at the top z.
- The finish spindle speed is op +0x1230 (rec 0x1298), swapped into op +0x328 for the writer.
- Order:
  1. Lead in at z (zf, not the corner-raised zb).
  2. A full circle at the bottom.
  3. The overlap.
  4. Then the helix **upward** from z to T: z = (1 − t)·z + t·T (arcs climb +0x1280 per turn).
- The other two finish modes (`FUN_1801435a0`, `FUN_1801448f0`) are **NOT DECODED**.

## 3. Circle Mill (opcode 18) — `bnci_circmill` 0x18003f980

### 3.1 Parameters
`set_circmill_prm` (0x180042050) sets **P = cur_op+0x11c0 = record 0x1228** (case 17).

| record | param | use |
|---|---|---|
| 0x1238 | 12004 | tip comp: when 0, every z is raised by the corner radius 0x358 |
| 0x1240 | 12107 | start angle (degrees) |
| 0x1248 | 12206 | entry/exit arc sweep (degrees) |
| 0x1250 | 10407 | overlap |
| 0x1258 | 12207 | circle diameter for point input (`FUN_180040830`) |
| 0x1260 | 12208 | start at centre |
| 0x1261 | 12209 | perpendicular entry line |
| 0x1262 | 12210 | roughing on |
| 0x1268 | 12055 | stepover % → 0x1270 = %·0.01·D |
| 0x1278 | 12211 | helical entry |
| 0x1280 | 10389 | min radius (= 0x12d0 %·0.01·D) |
| 0x1288 | 10381 | max radius (= 0x12d8 %·0.01·D) |
| 0x1290 | 10386 | Z clearance |
| 0x1298 | | plunge angle (degrees as used by `tan(·°)`; the MP text says radians) |
| 0x12a0 | 10385 | XY clearance |
| 0x12a9 | 10397 | entry fail: skip |
| 0x12b9 | 10393 | output arcs |
| 0x12c0 | | helix tolerance |
| 0x12e8 | 12265 | finish at all depths |
| 0x12e9 / 0x12ea | 40282 / 40283 | rough / finish multipass flags |

The multipass counts come from the common block: 0x8a8 on, S16 0x8aa rough count, 0x8b0 rough spacing, S16 0x8b8 finish
count, 0x8c0 finish spacing. The 0x8a8 / 0x8b8 / 0x8c0 values match `cam_fileop.c`.

### 3.2 Per point
1. Radius R = the arc radius, or 0x1258/2.
2. Depth passes per §0.4, each at z = zf + off + (corner radius when 0x1238 = 0).
3. XY multipasses (pass table `local_5528` with the same `FUN_1800412d0` formula):
   - rough n_r = 0x8aa if 0x12e9, else 0;
   - finish n_f = 0x8b8 if 0x12ea;
   - the default is a single pass.
4. Warning 0x93b (the passes are reduced) when there are several depth passes, finish passes only, and
   0x12e8 = 0 and 0x1262 = 0.
5. For each depth pass:
   1. **Outer pass** `FUN_1800409f0`, at radius R − (XY offset of pass 0).
   2. If roughing (0x1262) is on and r = (R − off0) − D/2 − rough spacing > 1e-12: **`FUN_180041530`** (§3.4).
   3. Then each XY pass j, at radius R − off(j): `FUN_1800409f0`, then output with `FUN_180042070`. Its param_6 (plunge
   with `plunge_move`) is **j == 0** (dis 0x18004048f..0x180040538); other circles start with a G1.
   - The roughing spiral is output before circle 0 (it writes its own records), although circle 0 was computed
     first.
   - A failing circle 0 (warning 0xa56) ends that point only (`break`). A failing later circle ends the operation
     (`goto LAB_18004076e`).
   - The roughing helix's top (`dVar2`) is the top of stock, then the previous pass's z: it is set after
     `adjust_depths` whenever roughing is on or the pass had circles.
   4. `adjust_depths` between depth passes.
   5. `retract_move` unless keep-tool-down (0x891) is set.
6. `clear_move` at the end, per 0x179 / 0x319.

### 3.3 One circle — `FUN_1800409f0(c, R, z, cw)`
**Setup.**
- Error 0xa56 if R < 1e-12 or R ≤ D/2.
- s = −1 if `cw` (0x5d2 = 1), else +1.
- a = 0x1240°, or the angle carried over from the previous `MCCECalcArcLnArcBetCir` transition.
- Lead fields are used only when 0x8f0 (lead in/out on) is set; otherwise sweep = 0 and overlap = 0.
- Overlap angle ov = 0x1250/R. End angle ae = a + s·ov.
- Extra full turns = min(floor(|ov|/2π), param_7), with **param_7 = 50** (`mov $0x32` → [rsp+0x30], dis 0x18004028a
  and 0x18004044a).
- Radii:
  - `cc_in_computer`: Rp = R − D/2 and lead radius rl = Rp/2.
  - Otherwise: Rp = R and rl = D/4 + R/2.

**Points.**
| name | definition |
|---|---|
| Q1 | (Rp − rl) at a — entry-arc centre |
| Q2 | (Rp − rl) at ae — exit-arc centre |
| S | Rp at a |
| E | Rp at ae |
| Pin | Q1 + rl at (a − s·sweep°) |
| Pout | Q2 + rl at (ae + s·sweep°) |

**Moves.**
1. **Entry** (first circle, or no transition):
   1. line to the centre (0x1260);
   2. line to Q1 (0x1261);
   3. line to Pin if it is > nc_tol away;
   4. arc Pin → S about Q1, if > nc_tol.
2. **Circle:** full arc S → S about c, plus the extra full turns.
3. **Overlap:** arc S → E if it is > nc_tol.
4. **Transition** to the next circle: if not the last circle (param_14) and P+0x110 (0x1338) is set, call mcce.dll
   **`MCCECalcArcLnArcBetCir`**(c, out point, ccw = (0x5d2 == 0), 0x1330, Rp + next, next/4) (§3.5).
   - `next` = param_12 = the spacing to the next XY pass: the finish spacing (op +0x858 = rec 0x8c0) when
     `FUN_1800412d0(j + 1)` flags that pass a finish pass, else the rough spacing (op +0x848 = rec 0x8b0).
     The registers are xmm9 / xmm8 (dis 0x18004021b..0x18004022c, 0x1800403ec..0x1800403fd, loaded at 0x180040040 /
     0x180040076).
   - The pieces are appended by `FUN_18003f7c0`: end point, cw = sweep < 0, full = |sweep| > 6.2831851.
   - If there is at least one piece:
     - DAT_1804f125c = 1;
     - the next start angle = atan2(last record − c), stored in DAT_1804f1278;
     - **the next circle has no entry** (entry runs when param_13 or not DAT_1804f125c).
   - **Call arguments:**
     - param_13 (first) = 1 for the circle computed before roughing (j = 0), 0 for the others;
     - param_14 (last) = (pass count < 2) for j = 0, else (j = count − 1) (dis 0x180040247..0x180040254,
       0x180040401..0x180040414).
   - DAT_1804f125c is cleared before each depth pass's first circle.
5. **Exit** (lead on and (last, or no transition into or out of this circle)):
   1. arc E → Pout about Q2;
   2. line to Q2 (0x1261);
   3. line to the centre (0x1260).

### 3.4 Roughing spiral — `FUN_180041530(c, Rr, T, z, …)`
Call (dis 0x18004031a..0x18004036d):
- Rr = (R − off(0)) − D/2 − rough spacing. The rough spacing is rec 0x8b0 when there are rough passes, else rec
  0x8c0 (xmm15, dis 0x1800400d9).
- **param_5 = 1**: the output starts with `plunge_move`.
- **param_6 = the out point of circle 0** (`FUN_1800409f0` param_9: its S, or E when the overlap arc was added).
  This is the rotation target. FUN_180041530 reads it at [rbp+0x298] → [rbp+8] (dis 0x180041596..0x1800415aa).
- param_11 = (R − off(0)) − D/2, the radius used by its transition.

The stepover, min and max radii are recomputed in bnci_circmill:
- 0x1270 = 0x1268 %·0.01·D;
- 0x1280 = 0x12d0 %·0.01·D;
- 0x1288 = 0x12d8 %·0.01·D.

Arc records: z = the pass z, cw = (0x5d2 = 1). They are full only for whole helix turns and the circle at z (dis
0x18004190d, 0x1800419ea, 0x180041bcc, 0x180041c55, 0x180041cd0, 0x180041e36).
**Helical entry** (0x1278):
1. d = Rr − 0x12a0. If max ≤ d: rh = max. Otherwise, if min ≤ d: rh = d.
2. Otherwise the entry fails.
   - If 0x12a9 is set: return with the error flag cleared. bnci_circmill then breaks out of this point's depth
     loop and goes on with the next point.
   - Otherwise: no helix (rh = 0), a line to the centre at z.
3. Height H = (T + 0x1290) − z. Angle Θ = H/(rh·tan 0x1298°), or 0 if tan or rh ≤ 0.
4. **Lines** (0x12b9 = 0):
   - m = |Θ|/√(8·0x12c0/rh) + 1, at least 2.
   - Point i: t = i/(m − 1), z = (1 − t)(z + H) + t·z (the last point exactly z), angle t·Θ·dir (from 0).
5. **Arcs** (0x12b9 = 1):
   - The first record is a line to (rh, 0) at z + H.
   - Then, while Θ remains:
     - more than 2π: a whole turn (angle unchanged), z − rh·tan·2π;
     - more than π: a half turn, angle + dir·π, z − rh·tan·π;
     - otherwise: the rest, angle + dir·rest, z.
   - The angle is kept in [−2π, 2π].
6. Then a whole circle at z.

Without the helical entry: a line to the centre at z.

**Spiral out.**
Only when Rr > rh + nc_tol.
1. Half circles, sweep dir·π, each starting at the current point.
   - The radius grows by g: first 0x1270/4, then 0x1270/2.
   - The centre moves by −g at the current angle: C' = C − g@angle, radius r + g.
   - After each half circle the angle grows by π (not dir·π).
2. `ints_on_gt(bound circle (c, Rr), half circle)` (tolerance nc_tol). If they meet, the half circle ends at the
   crossing and it is the last one. Otherwise it ends at its end.
3. A half circle ending more than Rr + nc_tol from c, or the record capacity ((round((Rr − rh)/0x1270) + 5)·2 + m), ends
   the loop without being added.
4. Closing pair about c: to the end mirrored through c, then back to the end.
5. Leads off (op +0x888 = rec 0x8f0 off), with 0x1338 set: `MCCECalcArcLnArcBetCir`(c, last, ccw, 0x1330, param_11,
   (param_11 − Rr)/4). The end point becomes the last record.
6. Leads on: an arc about M = (end + c)/2, radius Rr/2, to the angle (atan2(last − c), +2π if negative) + dir·0x1248°
   (dis 0x180041d9d..0x180041e56). The end point is not updated.
7. **`FUN_180041340`** rotates every record (ends and arc centres, `rotate_pt`) about c by the angle from (end − c) to
   (param_6 − c), i.e. so that the spiral's end direction is circle 0's out point direction.

The circle output (`FUN_180042070`) writes NCI 4 (`bnci_wr_4(0, rpm)`), G1 lines and `gr_circle_mill` arcs at feed
0x378, with 1018 / 1019 around subprograms.

### 3.5 `MCCECalcArcLnArcBetCir` (mcce.dll 0x180002d29, decomp/mcce_all.c)
Arguments: c, E, ccw, sw, R5, r → up to three gts and a count.
1. Arc 1:
   - centre C1 = c + (|E − c| + r) at angle(E − c), radius r;
   - start angle angle(E − c) + π (in [−π, π]), so it starts at E;
   - sweep = −|sw| when ccw, else +|sw|;
   - its end is A.
   - If E = c, or A = C1 (both within 1e-12), there is no transition.
2. A line from A along u (the tangent at A): u = (v.y, −v.x) when ccw, else (−v.y, v.x), with v = A − C1. The line's
   reference point is L0 = C1 + 2v.
3. If R5 − r < 0: no transition. `FUN_1800a9000` (0x1800a9000) intersects L0 + t·u with the circle (c, R5 − r):
   - foot point by `CalcLnLnIntersectPt`;
   - two intersections when inside (t0 < t1), one in the band ((r − 1e-14)², (r + 1e-6)²), none outside.
   - Take t1 when there are two and t1 > t0, else t0.
   - C2 = L0 + t·u. If t < 0 and |C2 − L0| ≥ 1e-6, or C2 = c: no transition.
4. The line ends at B = C2 + r at (angle(v) + π).
5. Arc 2:
   - centre C2, radius r, from that angle;
   - sweep = angle(C2 − c) − start, in [−π, π];
   - if its sign disagrees with ccw (negative when ccw, positive when cw) and |sweep·r| ≥ 1e-6, it is shifted by
     ∓2π.
   - It ends on the R5 circle.
6. `FUN_1800ac800(1e-6)` drops pieces shorter than 1e-6. If all of them are dropped, the line stays.

## 4. Thread Mill (opcode 100) — `bnci_threadmill` 0x180234c80
Ported function by function (2026-10-16, completed 2026-10-26 "threadmill": taper, compensation in control, contour flags, the
point diameter rule; viewer/cam2d_ops.c, code section 7). Decompiles decomp/mcmill_all.c l.385021-386600; the arguments the
decompile lost are read from the disassembly (`objdump -d -M intel --start-address=0x180234b80 --stop-address=0x180237b70
mcmill.dll`). Oracle: tools/tests/thread_oracle.py + nciw_oracle.py (4.8).

### 4.1 Function map (Mastercam -> port)
| Mastercam | decompile | role | port |
|---|---|---|---|
| bnci_threadmill 0x180234c80 | l.385061 | points (tp_get_all), each point's diameter, first / last point | `gen_thread` |
| FUN_1802374d0 | l.386306 | one point: passes, feed override, heights, teeth sub-passes, the cw / side bytes | `tm_point` |
| FUN_180236810 | l.385946 | one pass | `tm_pass` |
| FUN_180235a80 | l.385542 | entry / exit arc | `tm_arc` |
| FUN_180235e90 | l.385662 | lead point | `tm_lead` |
| FUN_1802360c0 | l.385749 | radius at Z (taper) | `tm_rt` |
| FUN_180236170 | l.385772 | helix pieces (arcs / lines) | `tm_helix` |
| FUN_1802355d0 | l.385403 | tapered arc step | `tm_step` |
| FUN_180235120 | l.385274 | tapered piece: arc through 3 points | `tm_arc3` |
| FUN_1802350f0 | l.385262 | quarter pitch when 0x1281 | `tm_q` |
| FUN_180234b80 | l.385021 | angle between vectors | `tm_vang` |
| mccore center_3pa 0x1804b5200 | mccore_filter.c l.4717 | circle through 3 points | `tm_center3` |
| mccore proj_pt_ar 0x1804b41b0 | dis only | point onto a circle along the ray from its centre; nothing when nearer than 5e-5 (const 0x1812b5b58) | inline in `tm_step` |
| gr_lin_mill / gr_arc_mill / gr_circle_mill | mccore | records | `mv` / `arc_moves` (+ `Move.cc`, `Move.ctf`) |

### 4.2 bnci_threadmill (points)
- Every tp in order. Kind 27 (solid point): sld_get_toolp_point_data gives a radius; kind 1: tp_calc_pt with the point's mode byte
  (TP +0xb0). The diameter is 2 r only when the mode is 0x43 ('C', centre) **and** the entity is an arc (or the solid point);
  otherwise rec 0x1298 (l.385180-385213). Port: cam_fileop.c sets Hole.d = 0 for a thread op's point whose mode is not 0x43
  (mcam_load.c TpRef.pmode); gen_thread uses Hole.d > 0 else 0x1298. (Thread Milling.mcam op 1: mode 0x50 -> 0x1298 = 1.0.)
- A point is milled when its diameter > tol. first = the first point milled; last = the op's last point index (even when skipped).

### 4.3 FUN_1802374d0 (one point)
- local_118 (S +0x40 / +0x41): +0x40 cw = (0x12a2 = 1 ? 0x12a0 == 1 : 0x12a0 != 1); +0x41 = !cw for ID, cw for OD (the side byte).
- comp = rec 0x350 (tool D) when cc_in_computer(0x5d0), else 0.
- Passes: multipasses 0x8a8 off -> 1 rough pass; on -> rough 0x8aa + finish 0x8b8 + spring S16 0x12b0 (0 passes -> warning 0x966,
  nothing). Per pass s16 = rough left; s16 < 1 -> finish-- else rough--. Offset = rough_left x 0x8b0 + comp + finish_left x 0x8c0.
  Radius S +0x18 = (dia + 0x1268 - offset) / 2 (ID), (dia - 0x1268 + offset) / 2 (OD).
- Finish / spring passes (s16 == 0): feed op +0x310 = 0x12b8 when 0x12c4; spindle op +0x328 = S32 0x12c0 when 0x12c5 (bnci_wr_4 at
  the pass start and back at the retract) - **refused here** (no spindle change inside an op).
- Heights (+ base Z = the point's Z when S16 0x1250 = 1, else the active origin's): ztop = max(top 0x1240, bottom 0x1248) (S +0x30),
  zbot = bottom (S +0x38); teeth S16 0x1228 > 1: top -= (teeth - 1) x pitch; q = FUN_1802350f0 = pitch / 4 when 0x1281; top >= pitch +
  q + bottom; helix zs = q + bottom (S +0x20), ze = top (S +0x28).
- Teeth >= 2: nsub = trunc(((ze - zs) + teeth x pitch - 2 pitch) / (teeth x pitch)) + 1 sub-passes of one pitch each, spaced teeth x
  pitch, from the bottom (up) or the top; the last (up) / first (down) gets one more pitch when short of ze.
- first (+0x42) = first sub-pass of the first pass, last (+0x43) = last sub-pass of the last pass.

### 4.4 FUN_180236810 (one pass)
- bVar2 = first || OD, bVar3 = last || OD. zA / zB = helix start / end (bottom up 0x12a2: zs / ze, else ze / zs). Start angle 0x1260.
- P1 (helix start) = o + tm_rt(r, zA) at the start angle. Entry arc E = FUN_180235a80(1, ...) at zA.
- Entry points: ID - the centre when 0x12a1 = 0 and |E.P0 - o| > tol; a lead point when 0x12a3 or (0x12a1 and 0x1278 > nc_tol).
  OD - a lead point when 0x1278 > tol. Then E.P0.
- Approach (bVar2): rapid to clearance 0x1230 unless 0x319 and not the op's first point; rapid to the feed plane 0x1238 (+ base)
  when E.z < it < clearance. Then to the first point at E.z: feed (plunge 0x380) when E.z <= feed plane, else rapid.
- Entry lines at the feed 0x378; entry arc helical (FUN_180236170, taper 0) when |zA - E.z| >= tol, else a flat arc to P1.
- Helix FUN_180236170(o, zA, zB, taper = |0x12a8| > 1e-6, radius S +0x18, angle 0x1260, sweep |zA - zB| / pitch x 2 pi).
- Exit arc X = FUN_180235a80(0, ...) at zB from the helix's end angle; exit points: ID - a lead point (as entry), then the centre
  (> nc_tol); OD - a lead point. Exit arc helical (FUN_180236170 starting at the helix's angle, taper 0) or flat to X.P0.
- Retract (bVar3): to the feed plane when below it (rapid if 0x1a9, else the retract feed 0x388); rapid to the clearance unless 0x319
  and not the op's last point.
- **Compensation in control** (cc_in_control 0x5d0 = 1 / 2 / 3, dis 0x180236b1e..0x180236b5c): code 0x2a (G42, op +0x56a 'R') when the
  side byte is 0, 0x29 (G41, 'L') when 1; Reverse wear (3) swapped. It goes on the last entry line (else the entry arc / its first
  piece); 140 on the first exit line (else the exit arc's pieces). Port: with `MCAM_CUTTER_COMP=1` only (formats/post.md); the post
  refuses a code on an arc.
- **Contour flags** (gr_lin_mill / gr_arc_mill last argument -> NCI line +0x40 / arc +0x58): 1 on the first entry line when bVar2;
  with no entry line the entry arc gets bVar2 (its first piece); 2 on the last exit line when the pass is the point's last (+0x43);
  with no exit line the exit arc gets 2 when bVar3 (its last piece). Port: `Move.ctf`, written by nci_write.c.

### 4.5 FUN_180235a80 (entry / exit arc)
d = pi / 2 (sign: cw, entry); P0 at angle a + d, radius re = r - 0x1270 (ID, >= 0) / r + 0x1270 (OD); both radii through tm_rt at
the pass Z (args dis 0x1802369b0..0x180236a0b / 0x180236fd3..0x180237016). Centre = the bisector of P0 P1 meets the ray o -> P1
(l_ints); sweep = acos (pi when |cos| >= 1 - 1e-12). Z of P0 = z +/- d x pitch / 2 pi when the helical flag (entry: up ? 0x1281 :
0x1280; exit: up ? 0x1280 : 0x1281), signs by entry / up.

### 4.6 FUN_180236170 (helix pieces) and the taper
- Args per call (dis 0x180236ec2..0x180236f28 entry, 0x180236f37..0x180236fcc helix, 0x1802371b7..0x18023720f exit): centre, z0,
  z1, taper flag, ztop, zbot, radius, &angle, sweep, cw, feed, cc code, flags.
- Arcs (0x1282 off): step 2 pi, or tapered FUN_1802355d0(r, taper rad, pitch, tol 0x1288). Pieces of one step; within 1.01 steps
  the rest (two halves when more than one step). Untapered piece = arc about the centre; tapered piece = FUN_180235120: the arc
  through the helix points at the previous, middle and new angle (radii tm_rt at their Z; args dis 0x1802365f5..0x180236635).
- Lines (0x1282 on): n = (short) floor(|sweep / sqrt(8 x 0x1288 / r)|) + 1 equal steps; tapered radius tm_rt(r, z) (dis
  0x18023631c..0x18023633c). cc 41 / 42 on the first line, 140 on the last; flags 1 first / 2 last.
- FUN_1802360c0: |0x12a8| >= 1e-6: ID r - tan(t) x (ztop - z), OD r - tan(t) x (z - zbot) (t in degrees, x 0.01745329251994329).
- FUN_1802355d0: step = pi; up to 8 halvings until the max over 13 samples (i / 12 of the step) of the 3D distance between the
  conical helix point and its projection on the 3-point arc (Z of the projection proportional to its arc angle) is < tol.

### 4.7 Dialog (2824 / 2825 / 2969 / 2837) - every control
| page / control | record | decoded | port |
|---|---|---|---|
| 2824 1647 teeth, 1659 pitch, 1660 start angle, 1661 allowance | 0x1228 / 0x1258 / 0x1260 / 0x1268 | yes | honoured |
| 2824 1723 taper angle (deg) | 0x12a8 | yes (FUN_1802360c0) | honoured (was refused) |
| 2824 1665 compensation | 0x5d0 | yes | Computer / Off; Control / Wear / Reverse Wear with MCAM_CUTTER_COMP=1 (post: Control only) |
| 2824 1677 / 1678 ID / OD, 1690 / 1691 hand, 1697 / 1698 direction | 0x1290 / 0x12a0 / 0x12a2 | yes | honoured |
| 2824 1682 diameter (label 1412 Major / Minor) | 0x1298 | yes; enabled when a point is not an arc picked at its centre (FUN_180192050 / FUN_180191ee0) | honoured (was ours: CamParams.thread_major) |
| 2824 1672 linearize, 1674 tolerance (enabled iff linearize or taper) | 0x1282 / 0x1288 | yes | honoured |
| 2825 2945 group, 1264 / 1267 rough, 1271 / 1275 finish, 580 spring | 0x8a8.. / 0x12b0 | yes | honoured |
| 2825 586 / 571 finish feed (571 iff 2945 && 586, FUN_180194910 dis 0x18019492c..0x18019495e) | 0x12c4 / 0x12b8 | yes | honoured |
| 2825 588 / 573 finish spindle (573 iff 2945 && 588) | 0x12c5 / 0x12c0 | yes | greyed; a stored op with it is refused (no in-op spindle change) |
| 2969 1668 arc clearance, 1670 line length, 1714 start at centre, 1711 perpendicular, 1645 / 1671 helical top / bottom | 0x1270 / 0x1278 / 0x12a1 / 0x12a3 / 0x1280 / 0x1281 | yes | honoured |
| 2837 1650 clearance, 1059 only at start / end, 1651 feed plane, 1653 top, 1654 depth, 1655 / 1656 abs / inc | 0x1230 / 0x319 / 0x1238 / 0x1240 / 0x1248 / 0x1250 | yes | honoured; a new op keeps the defaults op's 0x1230 (was ours: the Linking flags' clearance) |
| 2824 "Thread depth", "Full-profile tool", the "Radial passes" row (ours) | - | - | removed (never read by the engine) |

### 4.8 Oracle (2026-10-26)
All 7 corpus thread-mill ops with stored NCI (BRACKET4 / bracket4-dashed 18 / 19, thread-test 2 / 4, Thread Milling.mcam 1; all ID,
1 tooth, computer compensation, no taper): **every NCI motion record equal** to Mastercam's (nciw_oracle.py: 26 / 26, 30 / 30, 18 / 18,
100 / 100, 15 / 15, contour flags included), posted NC through Mastercam's posts equal (nciw_nc_cmp.sh). Path (thread_oracle.py, 3D
sampling 0.01 mm): max deviation both ways 0.0082 mm on the mm parts, 0.0005 in on Thread Milling.mcam (= the sampling / our chords), length ratio 0.9997-0.9998 (the chords). B8-6 (Thread
Milling.mcam "2.2x longer") was the oracle's sampling: orfiles.py / oracle.py used STEP 0.1 for an inch part and counted each of our
0.04 in chords as a full step (49 vs 22 "in"); the real lengths are 22.442 vs 22.449 in.
No corpus op has a taper, OD, several teeth, control compensation, linearize or a spindle override: those are ported from the
decompile without an oracle.

## 5. Drill / peck / chip break (opcode 2) — `bnci_drill` 0x180067cf0 → mccore `bnci_wr_81`

### 5.1 No peck motions
mcmill does not generate peck motions. Its only record writers here are:
- `bnci_wr_81` (first point);
- `bnci_wr_100` (further points);
- `bnci_wr_80` (end);
- `bnci_wr_lin` / `bnci_wr_0` (rapids between holes, feed −2.0);
- `bnci_wr_1018` / `1019` (subprograms), `bnci_wr_1025`, `bnci_wr_sof`.

The peck, chip-break and dwell values travel inside the cycle record. The G83 / G73 expansion belongs to the post or
the backplot, not to mcmill.

### 5.2 Heights per point (`bnci_drill`, record offsets)
| height | source |
|---|---|
| top | 0x2e8 (−1e38 → 0), plus the point z if 0x2f0 (incremental) |
| R plane | the per-point override (tp +0x148, if not 0 or ±1e38), else 0x190; plus top if 0x198 |
| clearance (0x179 on) | per-point +0x140, else 0x170; plus top if 0x178 |
| depth | `bnci_drill_depth(op, tp, top)` (0x1800694d0) |
| initial | per-point +0xd8 if set; else the R plane, or the clearance when 0x179 is on (0x319 also considered) |

### 5.3 NCI 81 record — `bnci_wr_81` (mccore 0x1800911d0)
Its `param_1` is the record itself, so these are record offsets. 0x370 bytes, preceded by `bnci_wr_82(extra)`:

| NCI off | value |
|---|---|
| +0 | 0x51 (81) |
| +4 | op id (rec +0x68) |
| +8 | tp id |
| +0x18 | i16 **cycle = rec 0x1228** (10100) |
| +0x20 | hole bottom point x, y, depth |
| +0x38 | dwell = per-point override or rec 0x1250 (10112) |
| +0x40 | feed = per-point override (when not ~0) or rec 0x378 |
| +0x48 | rec 0x1230 first peck (10108) |
| +0x50 | rec 0x1238 subsequent peck (10109) |
| +0x58 | rec 0x1240 peck clearance (10110) |
| +0x60 | rec 0x1248 chip-break retract (10111) |
| +0x68 | initial − point z |
| +0x70 | R plane − point z |
| +0x78 | top − point z |
| +0x80 | rec 0x1258 bore shift (10118) |
| +0x88 | initial point x, y, z |
| +0xa0 | flags 0x4001 \| per-point flags |
| +0xa8 | param_18 |

- `bnci_wr_82` receives rec 0x1270 when rec 0x12c0 is set.
- **Note:** `formats/post.md` reads +0x68 as "R plane". By this code it is the initial height *relative to the point Z*.
  It equals the R plane only when clearance is off and the point Z is 0.

### 5.4 NCI 100 record — `bnci_wr_100` (mccore 0x18008df60)
| NCI off | value |
|---|---|
| +0 | 100 |
| +0x20 | point x, y |
| +0x30 | depth |
| +0x38 | initial − z |
| +0x40 | R − z |
| +0x48 | dwell (or rec 0x1250) |
| +0x50 | feed (or rec 0x378) |
| +0x58 | initial point |
| +0x70 | flags |
| +0x78 | top − z |
| +0x80… | the op's view matrix (rec 0x5e0–0x620) |

The cycle number is **not** repeated in it.

## 6. NOT DECODED / next steps
- **Slot:**
  - the lost output argument of `FUN_180217f20` (used only by the lead builder's gouge check, which the slot turns
    off);
  - `slot9.dll`.
- **Helix bore:**
  - finish modes `FUN_1801435a0` / `FUN_1801448f0` (rec 0x12c0 / 0x12c1);
  - writer flags of `FUN_180145ba0`;
  - the control-only exit lead angle.
- **Circle mill:**
  - the MP meaning of rec 0x1330 / 0x1338 (40296 / 40297): 0x1338 turns transitions on, 0x1330 is arc 1's sweep in
    radians (§3.5);
  - `adjust_depths`;
  - `FUN_180042070`'s feed / speed switching (op +0x1294 / +0x1295 / +0x12a4 / +0x12a5).
- **Thread mill:** the finish-pass spindle override 0x12c5 / 0x12c0 is decoded (bnci_wr_4) but refused (no spindle change inside an
  op here); the per-point base Z when 0x1250 = 1 uses the first point's Z for every point (Hole has no Z); solid points (kind 27):
  the diameter is this program's smallest coaxial cylinder, not sld_get_toolp_point_data (not decoded).
- **Drill:** custom / longhand cycles; the NCI 82 extra record. (`bnci_drill_depth` decoded in batch 8: with tip comp 0x1268 the depth
  goes down by the breakthrough 0x1260 plus, for a drill tool other than types 6-8, mccore tool_tip_length(D, angle) = tan(90 -
  angle / 2) x D / 2 (0x1800a5440); cam2d.c cam_drill_z - BLOCK op 5 / CONTROL HORN op 4 equal their NCI depths.)

## 7. Code: viewer/cam2d_ops.c (Face, Slot Mill, Helix Bore, Circle Mill, Thread Mill)
Included by cam2d.c (uses its MB / mv / mb_tag_arc helpers and CamParams). Every engine reads its parameter block
P = record 0x1228.. from `CamParams.mc_p` (copied by cam_fileop.c `params_from_record` from the stored op, or from the
Mill_MM / Mill_Inch default op for a new op; `mc_ok` = 0 -> `mc_common_ok` refuses). A branch this spec marks NOT
DECODED fails the op with `op->error` naming it.

| code | source | role |
|---|---|---|
| `mcd` / `mcs` / `mcb` | §0.1 | read a double / S16 / byte of P by record offset |
| `cc_in_computer` / `cc_in_control` / `cc_in_control_only` | §0.3a | compensation tests; control-only compensation is refused (no G41 / G42 output) |
| `mc_depths`, `MPass`, `mpass_off` | §0.4, FUN_180218340 / FUN_1800412d0 | depth (and XY) pass table and pass offsets |
| `MRec` / `MList`, `ml_line`, `ml_arc` | ctour_rec / 0x40-byte helix records, FUN_180142b00 | engine motion records; an arc ending at the last point (nc_tol, same z) is dropped |
| `mc_init` | set_depths init_depth (decomp/mccore_depths.c) | feed plane, at least the final depth; equal to it -> retract (when on) else clearance (when on) |
| `mc_feed_up`, `mw_retract`, `mw_clear`, `mw_plunge` | retract_move 0x18089f390, clear_move 0x18089e6a0, plunge_move 0x18089ed00 (decomp/mccore_moves.c) | feed at the retract rate 0x388 to init_depth unless rapid retract 0x1a9, then rapid by the clearance / retract on flags; plunge param_5 = 0 only with clearance on + "only at start and end" (0x319) after the first approach |
| `mw_line`, `mw_arc`, `mw_out` | FUN_1802189e0 chord step | records -> moves; arcs as chords of step sqrt(8 tol / r) (<= 10 deg), z linear; an arc whose radius is constant within nc_tol is tagged with `mb_tag_arc` so the post writes the true G2 / G3 (improvisation_review.md CAM item 18) |
| `bent_*`, `slot_loop`, `loop_to_ml`, `slot_connect`, `slot_ramp`, `slot_lead`, `slot_chain`, `gen_slot` | §1: FUN_180216d60 merge test, ccomp_pock_from_ctour (Clipper substitute), FUN_180215d90, FUN_1802178c0 create_ramped_ctour, FUN_180216080 add_lead_in_out, FUN_180218e00 per chain | Slot Mill; a chain that is not a slot (closed, two longest lines parallel) is skipped (Mastercam warning 0xa5f) |
| `slot_offset_convex`, `slot_arc_rec`, `slot_ramp_exact` (cam2d_slot_arc.c, dec1) | FUN_1802178c0 create_ramped_ctour, FUN_1802189e0 | the ramp on the exact loop: helical arcs (0x1280) or equal-angle lines at sqrt(8 x 0x1288 / r); exact convex offset (ours) else gls_offset |
| `arc_moves`, `hb_lead_r`, `hb_rough`, `hb_finish`, `gen_helixbore` | §2: FUN_180144ff0 rough, FUN_180143fb0 finish, FUN_180145b40 point | Helix Bore; finish modes 0x12c0 / 0x12c1 (FUN_1801435a0 / FUN_1801448f0) refused |
| `mcce_arc_ln_arc`, `cm_circle`, `cm_rough`, `gen_circlemill` | §3.3 FUN_1800409f0, §3.4 FUN_180041530, §3.5 MCCECalcArcLnArcBetCir | Circle Mill; XY multipasses from 0x8a8 with rough / finish switches 0x12e9 / 0x12ea (dis 0x18004021b..0x18004022c) |
| `gen_helix` | | Helix Bore or Circle Mill by type |
| `tm_arc`, `tm_lead`, `tm_rt`, `tm_helix`, `tm_step`, `tm_arc3`, `tm_pass`, `tm_point`, `gen_thread` | §4.1 (every function mapped) | Thread Mill: radial passes, teeth sub-passes, entry / exit arcs and leads, untapered and tapered helices (arcs or lines), compensation in control codes (MCAM_CUTTER_COMP=1), NCI contour flags (Move.ctf); refused: finish spindle override 0x12c5 |
| `gen_face` | measured (buckle-v8 op 41 and others, comments in the code), not from a decompile | Face, **only with FACE_DECODED=0 or an op without its record** (since decisions batch dec1; batch 7: stored ops as read): this program's style codes 0 zigzag / 1 one way / 2 one pass / 4 = its dynamic, passes spaced floor(range / step) + 1, along / across overlaps; without chains the stock box. `face_decoded` sends every other op to cam2d_face.c (§8) |

Drill (`gen_drill`) is in cam2d.c: rapid to the clearance (first hole) / R plane, feed to `cam_drill_z` (final depth,
minus the tip length when tip compensation is on), rapid back to the R plane; holes in the op's stored order
(tp_pt_mgr sorts NOT DECODED). The canned-cycle words are the post's (formats/post.md).
Comment drift: the file header of cam2d_ops.c still says "arcs are written as chords"; since CAM item 18 the true arc
records are tagged for the post (above).

## 8. Facing (opcode 102 = 0x66) — `bnci_face` 0x180097230 (batch 7, 2026-10-23)
decomp/mcmill_all.c l.101309-102230; port viewer/cam2d_face.c (`gen_face_mc`). Record offsets (op + 0x68); P = record 0x1228..
(`CamParams.mc_p`). Oracle: the user's BRACKET4 / bracket4-dashed op 8 (Zigzag) regenerate Mastercam's NCI to 1.3e-5 (every
move, rapids included) and buckle-v8 op 41 (One way, reverse last pass, auto angle) move for move (formats/opdlg_port_audit.md 11).

### 8.1 Inputs
| record | meaning |
|---|---|
| S16 0x1228 | style: 0 Zigzag, 4 One way, 3 One pass, 5 Dynamic (the dialog's codes, FUN_18016ba50); others take the general path |
| S16 0x122a | move between cuts (Zigzag): 0 high speed loops, 2 rapid, else (1, the defaults' -1) linear |
| 0x1230 | linearization tolerance (chains_to_pock_bounds; < 1e-12 -> tol) |
| 0x1240 / 0x1250 / 0x1260 / 0x1270 / 0x1280 | stepover / along / across / approach / exit in % of D; bnci_face recomputes the distances 0x1238 / 0x1248 / 0x1258 / 0x1268 / 0x1278 = % x D x 0.01 (l.101600-101609) |
| u8 0x1288, 0x1290 | auto angle; roughing angle (radians) |
| u8 0x1298, 0x12a0 | feed rate between cuts on, value (Zigzag links unless Rapid; else the feed rate 0x378) |
| u8 0x12c0, 0x12c1, 0x12c2 | climb (angle + pi, direction -1); Zigzag: even number of passes / One way: reverse direction of last pass; Dynamic start location |
| 0x12c8.. | prm_hem::OverrideFeedsAndSpeeds (feed 0x12e8 / rpm 0x12f0 while generating) - not ported (no control on the page) |
| 0x868 / 0x870 / S16 0x878 / 0x880, 0x891 | depth cuts (FUN_180098e80 = calc_depth_cuts), keep tool down |

### 8.2 Per boundary
1. Boundaries: chains_to_pock_bounds (3D chains: warning 0x2309 / 0x270b) or, without chains, FUN_180098f90
   make_facing_pock_bound (the machine group stock's rectangle from job_setup_stock_corners, or the cylinder's circle when it
   stands along the Tplane Z; else warning 0xa59 "Stock boundary is not defined"). sort_pock_bounds; a boundary with an island:
   warning 0xa5e "Facing does not support islands".
2. set_depths (init / retract / clear depth), rough angle = auto ? FUN_180096d20 : 0x1290, + pi with 0x12c0.
3. FUN_180098d60: the boundary offset outward by along - D/2 (ccomp_pock, side from pock_ccw, min fillet pi = sharp corners;
   failure: 0xa5c). Both boundaries rotated by -angle.
4. Across: y range of the boundary's bbox +- (across - D/2) (< 0: 0xa5c). One pass: width - nc_tol > D -> 0xa5a, y = middle,
   n = 1. Else y0 = ymin + nc_tol, range = ymax - nc_tol - y0, n = floor(range / stepover + 0.999999999999) + 1, at least 2;
   Zigzag with 0x12c1 and n odd: n + 1; step = range / (n - 1).
5. Zigzag with Rapid and along <= 100 %: 0xa5b. Keep-tool-down mode (local_4385) = depth cuts && (Zigzag || One pass) && 0x891.
6. Depth k = 1..n_d at z = final + (nr - k) rs + (n_d - nr) fs (k <= nr), else final + (n_d - k) fs. Each depth starts at y0 with
   the initial direction, except keep-tool-down depths after the first: step and direction reversed, y kept.
7. Pass i: the line y meets the offset boundary (pock_bound_line_inters, tol nc_tol x 0.01; none: 0xa5d) at [u0, u1]. One way with
   0x12c1: the last pass reversed. Approach on the start side for the first pass (every pass unless Zigzag), exit on the end
   side for the last pass (every pass unless Zigzag). Then:
   - the first pass of a depth (state 1 / 2): plunge_move to its start (param_5 = the op's very first approach only); keep-tool-
     down depths (state 3): G1 at the plunge rate (0x380) down at the previous end;
   - Zigzag links: High speed loops = G1 along the old pass to the outer of the two ends, then gr_arc_mill half circle (centre
     (u, y - step / 2), cw = direction > 0, swapped when step < 0) at the link feed; Linear / Rapid: one move to the start;
   - every other style: plunge_move at every pass (the tool down: across at the feed rate).
   The pass: G1 to its end at the feed rate (0x378). adjust_depths(z) (mccore 0x18089e310: top of stock = z, incremental
   heights re-based). After the pass: last depth, last pass: retract_move if clearance on + only at start / end + another
   boundary follows, else clear_move; otherwise retract_move when (One way and not (the pass before a reversed last pass)) or
   (the last pass and not keep-tool-down).
8. Dynamic (5): per depth MCCECalcDynPock on the boundary with CDynPockParm = flags 0x40 | 0x8000002 | climb | metric 8
   ("Face CE option" registry 1: 0x10 instead of 0x40), start location 0x12c2, +0x20 = 1, R = D/2, r = corner radius
   (FUN_1801afa30), engagement CalcLnEngAng(D/2, stepover), micro lift 0.01 D (CE option: 0.1 D), approach, sliver = 20 x
   chain tol, top = top of stock (re-based by adjust_depths), depth, trochoid radius D/2 + stepover, chain tol; plunge_move to
   the first entity's start (FUN_180096be0), FUN_1801b07d0 writes the file (NOT DECODED: the port uses cam2d_dyn.c
   mcce_moves, as Dynamic Mill), adjust_depths, retract_move / clear_move as 7.

### 8.3 FUN_180096d20 automatic angle
The longest line longer than a quarter of the perimeter gives the angle (atan2, [0, 2 pi)); without one, 16 directions
-15 pi / 16 + k pi / 16 collect the midpoint tangent x length of every entity within tan(pi / 16) of them (signs folded), the
direction with the largest sum wins and its summed vector rotated back gives the angle. restrict_angle(-pi / 2, 3 pi / 2) (mccore
0x1804b4440: bounds inclusive), > pi / 2 + 1e-6: - pi, |a| < 1e-6: 0, |a - pi| < 1e-6: pi.

### 8.4 Not decoded / ours in the port
- ccomp_pock's sharp corner (min fillet pi): ours = the corner arcs gls_offset builds replaced by the two end tangents meeting at the
  apex (BRACKET4 op 8 confirms it on a line-arc corner, 1e-4 vs 0.018 along the circle).
- A rapid that descends while moving in XY: written XY first, then the descent (Mastercam's NCI writer ct_lin_mill, NOT DECODED;
  buckle-v8 op 41 NCI records 5-6).
- sort_pock_bounds' order of several boundaries (chain order here); chains_to_pock_bounds' spline linearisation (chords at 0x1230
  here); open chains refused; subprograms 1018 / 1019, prm_hem feed / speed override, face9.dll, the "Face CE option" registry
  value (0 assumed), FUN_1801afa30's tool-manager radius refinement, FUN_1801b07d0 (Dynamic's NCI writer).

## 7. Peel Mill (2D HST opcode 134 style 3) — CHST2DProcess path (HST engines pass, 2026-10-23)

Full spec with line citations: `decomp/hst2/peel_mcmill_spec.txt` (decomp/mcmill_hst2d.c unless noted; record = op + 0x68).
Port: viewer/cam2d_dyn.c `gen_peel_mcce` (+ mcce_dyn.c `mcce_peel_aesthetics`, `prep_material`).

- **Dispatch** (bnci_2d_hmm l.117-125): style 3 with record 0x17d8 != 0 (2784 3330 "Cutting style" = Peel) → `bnci_peel`
  @1801b68d0 (**NOT DECODED**, refused); 0x17d8 = 0 ("Dynamic peel", the defaults' value) → run_hst2d_toolpath → CHST2DProcess.
- **Chains** (PrepareChains l.6120-6248, PreparePockBoundsPeel l.6619-6628): chain set 0 only, **open chains only** (a closed
  one: LogError 8), at most two.
- **One chain** (l.6630-6657): w = record 0x1258 (≤ tol → 4·D, written back); side record 0x1270 (s16: 1 Left → R 0 / L w,
  2 Right → R w / L 0, else w/2 each); A = ccomp_pock(chain, dR, 'R'), B = ccomp_pock(chain, dL, 'L'), fillet angle
  set_min_fil_angle(2) = 0.01 rad. (ccomp_pock internals NOT DECODED: the port uses cam2d.c offset_open / offset_open_trim.)
- **Two chains** (l.6658-6836): order test with the cross products of the start points / start directions (swap when c1 > 0
  and c2 < 0, else keep; degenerate: LogWarning 8); a negative wall stock offsets them R / L by |stock| and the stock becomes 0.
- **Forced op writes** (l.6849-6853): record 0x1567 = 0, 0x1608 = 10.0 (gap %), 0x16bf = 1, 0x16bb = 0 (Helix only), 0x16b9 = 0.
- **Extensions** (l.6859-7062): entry record 0x1240 ? 0x1278 : 0, exit 0x1272 ? 0x1280 : 0, each capped by the distance to the
  intersection of the two start (end) rays.
- **Material +0x348** (l.7060-7635): [entry ext of A, only when > tolP (record 0x1288; < 1e-12 → 0.025 mm / 0.001 in)] A
  [exit ext] connector [exit ext of B] REV(B) [entry ext of B] closing line; **op 0x14e0 (record 0x1548) = the start of A or of
  its entry extension** = the engine's explicit entry point (CDynPockParm +0x0c = 8).
- **Motion +0x328** (l.7709-8242): the same with eE = 3·max(extE, D) (+ stock when > tol; + approach record 0x1660 when eE is
  below it - added, as decompiled) and eX = 2·max(extX, D), both capped by the ray distances, no tolP test.
- **+0x340** (l.8244-8714): the two side chains with the user extensions - only for the finish passes (FUN_180094540
  "DynCtourFinPasses.cpp", decoded 2026-10-28 below; record 0x1241 still refused by the port).
- **FUN_180094540(bound, .., z, level, last level, plunge/retract, &count)** (decomp/mcmill_hst2d_dyn.c l.4515-4882 = mcmill_all.c
  l.99557-99924), the Dynamic Contour / Peel finish passes, read 2026-10-28:
  - runs only when the 2D HST style op+0x11c0 (record 0x1228) is 2..4, finish op+0x11d9 (record 0x1241) is on, and op+0x1228 (record
    0x1290, "all depths") is set or this is the last level (param_4 == param_5);
  - the bound is copied; when the lead in/out flags op+0x888 / op+0x928 (records 0x8f0 / 0x990) are both on and the bound is closed, or
    the bound's start parameter (+0x10 -> +0x68) lies strictly inside (tol, 1 - tol), FUN_180095000 re-starts it (not read);
    pock_bound_to_ctour -> the contour records;
  - per pass i = 0 .. n - 1, n = op+0x1718 (record 0x1780): offset d = (D / 2 when compensation op+0x568 is in computer, else 0) +
    (n - i - 1) x op+0x1720 (record 0x1788, the spacing), + the wall stock op+0x278 (record 0x2e0) for style 4, or style 3 with stock
    > 0; FUN_1800518b0 offsets the records by d (the 2D Contour offset routine; side 'L' 0x4c, or 'R' 0x52 for styles 3 / 4 with
    op+0x1206 (record 0x126e) set, or for other styles when op+0x56a (record 0x5d2) == 1); the passes' Z = z + the returned value;
    closed = start and end within nc_tol;
  - op+0x145c / op+0x145d (records 0x14c4 / 0x14c5): the feed (op+0x310 <- op+0x1450, record 0x14b8) / spindle (op+0x328 <-
    op+0x1458, record 0x14c0) overrides, restored after the pass;
  - approach: with op+0x860 (record 0x8c8) and the previous NCI point farther than nc_tol from the pass start, FUN_180053e70 then
    FUN_1801afb70(0, ..) (the link); with param_6 plunge_move; bnci_wr_ctour_marker; FUN_1801afb60(0x40 | 8 when the link ran, ..)
    (entry); FUN_180056be0 (the contour NCI writer, as 2D Contour: leads, arcs); with param_6 retract_move; FUN_1801afb70(0x10 when
    FUN_180094fb0(op, i), 0x80 on the last pass of the last level, ..) the exit.
  Not read: FUN_180095000, FUN_180094fb0, FUN_180053e70.
- **PeelAesthetics(R = D/2, mr = D · record 0x1658 %, P = op 0x14e0, &Motion, &Material)** (l.8716-8720), then per depth
  level the Motion copy is offset by the wall stock (PackageSomething l.5703-5733, FUN_1801ac7e0 - its sign rule NOT DECODED;
  the port shrinks it, as for Dynamic Mill) and `MCCECalcDynPock(S1 = Motion, S2 = Material)` runs with flags 0x12 | climb |
  0x100 | 0x4000000 (FUN_18029cb90 l.136-138), +0x0c = 8, +0x10/+0x18 = op 0x14e0/0x14e8.
