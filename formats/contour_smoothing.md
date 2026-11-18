# Contour: Smoothing (Arc Filter / Tolerance page) and Infinite look ahead (2026-11-08)

Decode notes for the two items left open by formats/contour_100_plan.md section 3. Code: viewer/cam_smooth.c (included by
viewer/cam2d.c after cam_filter.c), the recording hooks in viewer/cam2d.c (`g_ps_*`, `ps_mark`, `ps_arc_rec`, `ps_contour_arcs`),
the dialog in viewer/core_ui/cui_opdlg.c (`pf_engine_nd`, `pf_honoured`, `pf_smooth_group`).

Decompiles (mccore.dll, Mastercam 2017):
- ~/mastercam-re/work/ctour100/mc_ptdist.c - bnci_point_distribution_op 0x180087040, bnci_point_distribution 0x1809450f0, the
  collector FUN_180945d50, the flush FUN_18094adc0, init_gv (previous pass).
- ~/mastercam-re/work/ctour100/mc_ptdist2.c - PointDistribution_3D FUN_18093bcf0 (CFilter.cpp) and helpers (previous pass).
- ~/mastercam-re/work/ctsmooth/pd_more.c, pd_more2.c (this pass; DecompAt.java on a copy of the gh_core project, heap 2 GB):
  FUN_180947050 (the Bezier / arc-break callback), FUN_18094a170 fit_line, FUN_18093e0f0, FUN_180940b40, FUN_18094ce10 (the
  writer, shared with the Line/Arc filter), FUN_180944290 (append), FUN_18094b530 insert_nci_pt_before, FUN_180944210 (the
  node pool: calloc), FUN_18093d780 / FUN_18093d7e0.
- Disassembly of mccore.dll 0x18093bcf0-0x18093d107 (objdump): the stack arguments Ghidra drops at the calls of
  FUN_18093eda0 / FUN_180942380 / FUN_180942110 / FUN_180947160 / FUN_1809468c0 (below); the constants (0.1, 0.075, 0.7, 0.125,
  8, 10, 50, 0.15707963267948966, 1e-05, 0.95, 0.6, 0.4, 0.5, 1e-07) read from .rdata.
- decomp/opdlg_mccore_arcfilter.c - ArcFilterRefinePage (InitPage, DoDataExchange, OnBnClickedStaticSmoothing,
  OnBnClickedCheckLimitMaxSegmentLength, ValidateRftpDialogControls, KeepTolerancesConsistent, UpdateLocalOp).

## 1. Where it runs

BNCISection::PostProcess (decomp/mccore_filter.c l.3588-3601): after the Line/Arc filter (record 0x33a, bnci_filter_op), when
record 0xca8 is on: bnci_point_distribution_op (op_is_drill ops skipped) -> bnci_point_distribution. That one first tries the
special app "bfilter9" / "bfiltw9" (load_special_app); no such DLL ships with Mastercam 2017 (~/mastercam-re/files: none), so the
built-in path runs: srand(0x1786), the op's NCI section re-read record by record (CBnciReadWrite) through the collector, the
section rewritten.

Record fields (ent offsets; DDX of ArcFilterRefinePage, UpdateLocalOp l.77-88): 0xca8 on (group 10386), 0xc98 the smoothing
tolerance (10382), 0xca0 segment length (10401), 0xca9 Use Fixed Segment Length (10397), 0xcaa Shift points randomly (10398),
0xcab Minimize number of points (10399), 0xcac Present arcs as line segments (10400). FUN_18094adc0 passes them to
FUN_18093bcf0 in that order (tol, fixed, shift, minimize, arcs, seglen).

## 2. The collector FUN_180945d50 (mc_ptdist.c l.1025-1384)

A state record (code, last point, last feed) and the filter's node list (0xe8-byte nodes: +0 type, +2 cc, +8 point, +0x20
vertex tangent, +0x70 arc centre in world / phase 1 scratch, +0x88 feed, +0x90 flags, +0x94, +0xb5 the arc's full-circle byte,
+0xc0 mark, +0xd8 prev, +0xe0 next):
- 1000-1002 (the op's 1002 at the section start): state -2000 (init_gv: _prev_nc_pt / cur_depth "far away");
- a motion record 1 / 2 / 3: state -2000 -> flush, the record written as it is, state 0; state not -1000 and a feed different
  from the state's -> the same; else a node (arc centres view_to_world), and when the state is not -1000 or the list is empty
  the state's point first as the list's head (type 0), state -1000; the state takes the record's end point and feed;
- 0 (rapid, feed -2): state 0, flush, written as it is;
- -4 (0xfffc): state 0, written; -5 / -6 / -7: state 0, flush, written; 1011-1014 and every other record (the contour marker
  -96 = 0xffa0, the stop_cc 1004 ...): flush, written, state kept.
So the list is one run of feed moves between rapids and records; a feed change inside a run does not end it (fit_line refuses
to merge across it). The flush FUN_18094adc0 runs PointDistribution_3D on the list and writes every node after the head through
FUN_18094ce10 (lines ct_lin_mill - which drops a point within tol of the last written one, XY and depth, mc_grlin.c l.836-850 -,
arcs ct_arc_mill).

## 3. PointDistribution_3D FUN_18093bcf0 (mc_ptdist2.c l.3-740 + the disassembly)

T = the tolerance. fixed = 0xca9 (it clears shift and minimize). t1 = 0.1 T; t2 = fixed ? 0 : (T - t1) x (shift ? 0.1 : 0.075);
t3 = (T - t2 - t1) x 0.7; t4 = (T - t3 - t1 - t2) / 8 (asserted to sum to T); maxlen = fixed ? segment length : 10 T.
1. Eight passes over the nodes: a line whose next is a line, |P2 - P0| in [1e-7, 50 T], turning by at most 0.15707963 rad (9 deg):
   the vertex moves towards P0 + (P2 - P0) L01 / (L01 + L12), at most t4 a pass; the first pass keeps the original point
   (+0x70), the eighth stores the vertex tangent (1 - g)(P2 - P1) + g (P1 - P0), g = L12 / (L01 + L12), mark -1.
2. FUN_18093eda0 ExtractLinesUsingToleranceAndMaxCount(t1, max count 0, no shift): drops line ends that fit_line FUN_18094a170
   keeps within the tolerance (the largest of the distance to the chord and 2 x the overshoot beyond it; 1e6 when the end is
   not a line or its feed differs from the node before).
3. Each line: n = ceil(L / maxlen) (cvttsd2si, 16-bit compares; at most 30 unless fixed; < 2: left), the end tangents (mark -1)
   and their angles a0 / a1 to the chord (0 beyond 50 T); h = FUN_1809415c0(L, a0, a1) with both > 1e-5, t3 with one, 0 with none;
   h <= 1.1 t3: a line (h <= 1e-7) or a quadratic from the end with the larger angle (h / sin, the 0.95 cos clamp and the 0.6 /
   0.4 projection pulls with the other tangent missing), else a cubic with t3 / sin a along each tangent; FUN_180942110 evaluates
   it at i / n (de Casteljau) and the callback FUN_180947050 inserts each point as a line before the node (its cc / feed / flags).
4. Not fixed: shift off -> ExtractLines(t2, 0, 0); shift on -> ExtractLines(t2, 100, random) - or with minimize ExtractLines(t2 / 2,
   100, random) then FUN_180942380 RemoveClosePoints(t2 / 2, T). The random step back uses the CRT rand() (MSVC LCG, the srand above).
   (Read from the disassembly at 0x18093cf27-0x18093cf96: Ghidra shows neither the counts nor the shift argument.)
5. Present arcs as line segments: each arc with a node before it - FUN_180947160 (constr_ns_arc / constr_circle with nc_tol, the
   +0xb5 full-circle byte), n = max(FUN_180940ca0 (chord angle 2 asin(sqrt(r^2 - (r - T)^2) / r)), fixed ? FUN_180940c50 (length
   maxlen) : 0), at least 1; FUN_1809468c0 break_arc_3d_to_nsegs inserts the points (depth linear), the arc node becomes a line.
6. The arc centres back to their views (world_to_view).

## 4. What this program does (ported, cam_smooth.c)

The toolpath is this program's Move list, so the records the collector reacts to are rebuilt from it: a rapid = record 0 (feed
-2), a feed move = record 1, the chords of an engine arc (Move.arc) or of a recorded lead arc = one arc record; the op's 1002
before the first move; a contour marker -96 before each pass of gen_contour (bnci_wr_ctour_marker before the pass writer
FUN_180056be0, decomp/mcmill_ctour.c l.1750 / 2343; ps_mark, placed on the pass's first move - the stored X9 NCI has its pass
section markers -7 -5 -4 -6 at the same place, before the next depth's plunge, BRACKET4 op 7); the 1004 after a move with cc 140
(nci_write.c). Raw records are copied unchanged; distributed lines are new moves; arcs keep their chords (a recorded lead arc is
tagged as an arc, mb_tag_arc, so the post writes it as G2 / G3 with its centre); with smoothing the post does no arc fitting of
its own (CamOp.filtered = 1: Mastercam posts the distributed points as lines). A feed move repeating the last point (within
tol) is not a record (ct_lin_mill dropped it when Mastercam's engine wrote it): kept as it is between lists, dropped inside one.

Dialog (cui_opdlg.c): for Contour the group 10386, 10382, 10397-10401 are live and saved; the enables of
ValidateRftpDialogControls (segment length with Use Fixed Segment Length, Shift / Minimize without it, all with the group);
the group check redistributes the tolerances as OnBnClickedStaticSmoothing + KeepTolerancesConsistent (pf_smooth_group).

Scenes (tools/tests/op_scenes.txt rt_smooth_*): an open polyline with small turns, Smoothing on with each option; a rectangle
+ circle (chain arcs) refused; export / reopen keeps the values.

Verification: no stored op of the corpus (<user-parts>, the Mastercam samples; 879 ops read) has Smoothing on (the
2018 samples' bytes at 0xca8 belong to a different record layout), so there is no oracle - ported from the decompile only. With
Smoothing patched on (cam_cli fileop --rec ca8=b:1 --rec c98=d:0.02) the 149 user contour ops: 34 generate (line chains; the
collinear points of chains broken into pieces are merged, as ExtractLines does), 115 are refused (chain arcs / rolled corners).
The contour oracle with Smoothing off is unchanged (149/149, median 0.005, worst 0.022, every op's line identical); post
regression (postreg.sh) 0/16. Op scenes (op_compare.sh, 280 scenes): re-blessed only the intended changes (gen_contour,
pw_contour, rt_arcf_b10_contour: the Smoothing controls live for Contour; the new rt_smooth_* scenes); left unblessed, not from
this work: the 52 project goldens before "PROJECT-FORMAT 2", gen_blend / gen_sfradial / gen_srplunge ("Translate"), and the
Dynamic / Peel scenes (rt_dyn*, rt_dynamic, gen_dynamic_mill, rt_peel*, rt_dctour_hst, rt_arcf_b10_dyn / _planes,
regen_dup_toolnum, rt_b12_reload_stored: point counts / NC), whose goldens predate e97779a (Dynamic links D-3 default).

## 5. NOT DECODED / not ported (refused or greyed)

- **Other operation types**: the routine serves every non-drill op (PostProcess), but which records lie between their moves
  (where the list is flushed) is engine-specific and not mapped here: the Smoothing controls stay greyed with the NOT DECODED
  marker for every op but Contour; a stored non-contour op with 0xca8 on is refused.
- **Arcs this program writes as untagged chords**: a contour whose Mastercam NCI holds arc records other than the lead arcs - chain
  arcs, corners the compensation rolls (set_min_fil_angle: None pi, Sharp 0.785398, All 0.01 at an outside corner), corner
  rounding / break / optimize - is refused: the distribution would move their chords, where Mastercam leaves an arc record alone
  (ps_contour_arcs). Porting it needs the contour's arcs kept as arcs through the offset (Clipper polylines here).
- **Ramp / Oscillate contour, Tab Cutoff**: their markers (FUN_1801ff170 l.350924 / 351258, the cutoff loop l.2343) are not mapped
  onto the moves: refused.
- **Together with the Line/Arc filter**: the filter's output records (and how its run interacts with the markers) are not kept
  here: refused.
- **A lead arc rebuilt in a scratch list** (Plunge after first move / Retract before last move with an arc lead): refused (g_ps_arc).
- (A tolerance / segment length of 0 is not refused: the piece counts go through cvttsd2si as Mastercam's do - 0x80000000, 16-bit
  0 -> 1 piece.)
- InitPage's default segment length (0xca0 <= 1e-7 -> total tolerance x 20, written back by UpdateLocalOp) and the tolerance edits'
  KeepTolerancesConsistent (the % edits, sliders) are not ported (as for the filter, batch 10).
- The writer's NCI flags (+0x90) and wire / lathe / 5-axis branches (not for Contour).

## 6. Infinite look ahead off (Cut Parameters 1223, record 0x1238 = ctour_prm +0x10): NOT DECODED for this offset

ccomp_pock_from_ctour -> ccomp_pock passes MCCECalcContourOffset the flags 0x40027 (on) / 0x60027 (off) (formats/contour_100_plan.md
"Overwrite edited tabs, Infinite look ahead"). Read this pass (decomp/mcce_all.c):
- MCCECalcContourOffset (l.8571-8807, test at l.8695): bit 0x20000 also clears 0x20 of the flags it works with.
- FUN_1800c7bc0 (l.107904-107990): the raw offset's self-intersections come from FUN_18012f3b0 with the bit as its argument; with
  intersections and the bit clear the loops are resolved globally (FUN_1800c92b0, FUN_1800c94b0, FUN_1800c7100, DetNestLvlShAr),
  with it set FUN_1800c7670 (l.107741-107903) cuts them sequentially (a stack of intersection indices, each closing loop removed).
- FUN_18012f620 (l.163304-163460): with the argument 1 each entity is tested only against the next entities, at most 3 of them
  (local_144 > 3 ends the inner loop, l.163444-163451, wrapping to the start) - the "limited look ahead"; with 0 against all.
- FUN_1800c8030 (l.108053-108118): with the bit set a pair that FUN_1800c8370 does not accept is no longer tested at its middle
  point (FUN_1800c8920); FUN_1800c8330 (l.108120-108136), FUN_1800c9d70 (l.108916-), FUN_1800d47b0 (l.114618-): further tests,
  not read.
This program's contour offset is Clipper's (cam2d.c chain_offset), not MCCE's, so there is no raw offset / intersection list to
apply this to: an op with it off and a compensation side stays refused (cam2d.c gen_contour), the control greyed (pf_engine_nd).
Corpus: the four ops with 0x1238 = 0 (BLOCK.mcam ops 1 / 2, WCS_EX-0_PROGRAMMED.mcam ops 1 / 3, Mastercam 2018 Handbook Volume 1
samples) carry no NCI (0 motion records) and are 2018 records (another layout: 0x1238 there is not known to be the flag) - no
oracle.
