# Wireframe commands: audit against the decompile and fixes (N9-16, 2026-11-09)

Branch `wfacc`. New Ghidra dumps (git-ignored, in the shared tree): `decomp/wfacc/core_kernels.c` (make_fillet, FUN_18023a860, make_chamfer_chain,
createLineBisect, FUN_18026e060, FUN_180272530), `core_kernels2.c` (createLineClosest, createPointsAlongAnEntity(Spaced), calcLinePerpToAnEntity,
ellipse_to_nurbs, ccomp_pock, t_on_arc ...), `core_kernels3.c` (t_from_pt_2d, FUN_18026f6d0 / f710 / f950 / fea0, gt_offset), `mccad_kernels.c`
(createArc* ), `mccadutil_kern3.c` (create_chain_fillets, find_fil_ent, find_fil_gt, trim_ent_to_t), `offsetc_store.c`, `offsetc_ctx.txt`.
All paths below are under `decomp/` unless noted.

## 1. Findings fixed (code in `viewer/core_ui/cui_geo.c` and `viewer/core_ui/cui_geo_mc.inc`)

| # | Command | Was | Decoded | Settled by |
|---|---|---|---|---|
| 1 | Rectangular Shapes, D shape | flat above the circle centre, arc below, circle centred on the shape | flat at cy - h / 2 left to right, arc through the top, centre cy + h / 2 - w / 2; \|w\| < \|h\| swaps and rotates +90 | FUN_1802720d0 mccad_shapes_core2.c:472-503, :582-589; FUN_180272530 wfacc/core_kernels.c:2067 |
| 2 | Arc Polar direction | always counter-clockwise when dragged | the rubber band's latched sign (0.1 of arc length from the start) gives clockwise; start in [0, 2pi), sweep restricted, signed | sketcher_arc_3.c:632-633, :3175-3179; rubber/rb.c:1094-1100; createArcPolar wfacc/mccad_kernels.c:449 |
| 3 | Trim hit (modes 0 / 1 / 3, Break path) | hit nearest one pick | hit nearest the midpoint of both seed points | trim_123 mccad_cmds/sketcher_trim_2.c:2448; intersect_ents mccore_inters.c:149-153; gp_ints :564 |
| 4 | Ellipse tolerance and angles | 0.02 in every unit, 32-segment floor, parametric angles | min_arc_length of the unit system; start / end are polar angles of the point on the ellipse. Output is still lines (section 3) | create_ellipse.c:84; ellipse_to_nurbs wfacc/core_kernels2.c:2248 |
| 5 | Offset Chains corners | no corners value | corners = 2 (Sharp) -> set_min_fil_angle(1) = 0.785398 | do_xform_offset_ctour.c:76; mccore_pock2.c:7-20 |
| 6 | Offset Chains algorithm | own offset and extension | FUN_18001c220 -> ccomp_pock through `rm_ccomp_pock`; copies, L / R / both; stores lines and arcs over tol | do_xform_offset_ctour.c:996, :1299; wfacc/offsetc_ctx.txt; wfacc/offsetc_store.c:664 |
| 7 | Trim Many (Trim) | kept all pieces on the side as new records | one record per curve, trim_ent_to_t per hit, extension past an end, last curve first | FUN_1800dc870 sketcher_trim_2.c:3060-3370; side FUN_1800dbf20 |
| 8 | Break Two Pieces / Trim to point break, circle | one 360-degree arc | two arcs; a piece under tol = no break | FUN_18004f180 Break2Pieces.c:406-640, FUN_1800505d0 |
| 9 | Point Endpoints | de-duplicated, one point for a circle | both ends always, projected to the construction depth | create_point_endpoints.c:134-137, :184 |
| 10 | Rectangular Shapes fillets | radius clamped | radius < \|w\| / 2 - tol and < \|h\| / 2 - tol or no fillets (no warning on this 1-point path) | FUN_18026fce0 mccad_shapes_core2.c:817-819 |
| 11 | (Helix / Spiral) | not fixed, section 3 | | |
| 12 | Project default depth | 0 | construction depth | mccad_util_project.c:88, :504 |
| 13 | Join | tol = 10 x our gtol, two new records | tol, one record rewritten, one deleted | FUN_1800532a0 join_selected_entities.c:1983; JoinEptrs |
| 14 | Close Arc | one pick, sweep made positive | select_ents, last first, sign and start kept | close_arc.c:61, :70-89 |
| 15 | Chamfer Chains arcs | not fixed, section 3 | | |
| 16 | Tangent-corner 0.0175 | not fixed, section 3 | | |
| 17 | Rectangle and Rectangular Shapes order | bottom first | Rectangle: top L to R, right, bottom R to L, left. Obround and Double D: top, right arc, bottom, left arc | FUN_180272040, FUN_18026e060 wfacc/core_kernels.c:2002; FUN_180271b50 :378-394; FUN_1802713f0 |
| 18 | Tolerances | gtol in Break at Intersection / Points | tol | Break2Pieces.c:122-130 |
| - | Small Arcs | 1e-12 | 1e-8 | sketcher_point_6.c:765-768 |
| - | 3-entity arc | 5e-5 literal | tol | mccad_arctan_modes.c |
| - | Full circle as Trim / Break target | refused or two-hit | trim_ent_to_t treats it as any arc, to the seam | wfacc/mccadutil_kern3.c:1666 |
| - | Trim, no intersection | no message | "The selected objects do not intersect" (0x1743, param_7 0) | sketcher_trim_1.c:1798; trim_123 |
| - | Fillet candidate | nearest both picks by summed distance | nearest the seed midpoint | find_fil_gt wfacc/mccadutil_kern3.c:817 |

## 1b. Checks

- Offset Chains: `rm_ccomp_pock` against the real mccore.dll (`tools/peloader --remach`, script: a 20 x 20 square, clockwise and counter-clockwise, 'L' and 'R', distances 5 and 10, min_fil_ang 0.785398): 8 cases, 70 lines of output, 0 differences. On a square the 'L' of a clockwise chain at 10 gives straight offsets joined by 90-degree arcs of radius 10 (the 45-degree rounding); a chain whose offset collapses gives no result.
- wf scenes `acc_*` (expected numbers worked out from the decompile, listed in the commit message's report) and `acc_reload` (save and reload of a clockwise polar arc and a D shape).

- Golden changes blessed (75 wf files, of which 17 new; 23 chain files; all with the reason below; list in git history): 50 wf files are order / direction only (the decoded Rectangle and Rectangular Shapes order FUN_18026e060, and Trim Many's last-first store), confirmed by a normalised comparison of the entity sets; 8 change geometry: offsetc* and xf_mirror_cw_offset (ccomp_pock, the 'L' side of the chain, signed sweeps), pt_end (no duplicate removal), bbox_cyl_pushpull is NOT blessed (section 2b). 23 chain files: the chain order of the same rectangles.
- wf_compare 0/270 after blessing, chain_compare 0/63, units_test ALL PASS (its check of a scaled line edited for the new line direction).

## 2. Corrections to the first audit

- Rectangular Shapes filleting warnings 0x16d1 / 0x16d2 belong to `create_rectangle2`, the 2-point mode this panel does not have; the 1-point path is silent.
- `make_fillet`'s extension pieces need `param_8` != 0; `sketcher_fillet_1.c:529` passes 0, so Fillet Entities makes none.
- Break at Points already works on the selection; the "points anywhere" comment is misleading but the behaviour matched.

## 2b. Open: `bbox_cyl_pushpull` and the Rectangle order

- **Bounding cylinder.** The scene changes from r 70.0000 to r 70.0532 after Apply, when the entity step reruns on everything including the new r 70 solid. `bcyl_fit` agrees with the decompile where I read it side by side: FUN_180057e50, FUN_1800599a0, FUN_18005c4a0, FUN_180059b20 in `mccad_bcyl.c` / `mccad_bcyl_finish.c`. That algorithm depends on point order: `add_to_minmax` grows the first circle from the points in visiting order, and the refinement stops once the support count is anything but 2 (here 7, after the phase with one support), so it is not a true minimum. Neither value can be called Mastercam's, because Mastercam's points for a solid come from its own STL facets (FUN_1800587c0, TMP.STL) and ours from our mesh. The reference stays at the old value, so wf_compare shows this one difference until that is settled.
- **Rectangle order.** 255 stored four-line rectangles in 105 of 451 sample parts: 140 start TL going +x clockwise (FUN_18026e060), 74 start BL going +y clockwise (the same loop started one corner earlier), 41 other. Only 3 stored chains are made of exactly such a rectangle, too few to test a start point. Effect on other suites before any blessing: op 92 of 325 scenes differ (84 draw a Rectangle), solid 61, planes 18, stockplanes 18 (11), tool 68 (3), srf 3 (2), misc 5 (0); in op, 30 NC files change and a default Contour moves to the other side of the rectangle (`dirty_ours` starts at Y75 instead of Y25). The scenes that do not draw a Rectangle (8 op, 65 tool, 7 stockplanes, 5 misc, 1 srf, 1 planes) were not compared against a toolpaths2 baseline.

## 2c. N9-22 and later fixes

Decision N9-22 keeps the decoded Rectangle order. Against a toolpaths2 baseline built from the same base: tool 76 and misc 5 differences are pre-existing (identical output on the baseline); op 47, solid 2, planes 22, stockplanes 16 also differ on the baseline, of which op 41, solid 2, planes 8 and stockplanes 9 are changed further here and stay unblessed. References updated only for files that differ on this branch alone in scenes drawing a rectangle: op 52 (26 txt, 13 NC, 13 saved projects), solid 61, planes 1, stockplanes 2, srf 2. Not updated: planes 3 `.mcx-9` (shared store outside the repository), srf parts.srf_loft and parts.srf_lofted (flags 0x41 -> 0x45, unexplained).

Item 16: `check_tangency` |dot| > 0.99985 replaces the 0.0175 test. Item 15: Chamfer Chains take arcs (legs t_int +- d / length). Ellipse: one NURBS (FUN_1805ad6a0 / ellipse_to_nurbs, trimmed by Newton as FUN_1805adaf0, rotation and centre as CalculateEllipse); output types 1 and 2 NOT PORTED.

## 2d. Stage 1 (second pass): Helix, Spiral, Ellipse output

Item 11 fixed. `mcr_helix` in `viewer/mcspline_refit.c`: MaHelix / MaSpiral (decomp/mccad_shapes_core.c MaHelix::MaHelix :548, EvaluateCrv :842, ConvertToNURB :745, FitCubicApproxCurve :934, MakeMCSplineEntities :1271; MaSpiral :1561, :1665) on the spline branch's BinaryCurveFitParams / MaCubicSpline / ConvertToC1NURB port (merged, branch worktree-agent-af8e5e2e1856c181b); Spiral per revolution with final pitches FUN_180089040 (create_spiral.c:540-660). Panel: Helix Taper Angle and Height (FUN_180085a00 / FUN_1800855f0), Spiral Initial / Final Pitch (res 0x191f) and Height (FUN_180087700 / FUN_180087b10).

- Oracle `tools/tests/helix_oracle.sh` (real mccore.dll, `tools/peloader/mccore_spl_hx.c`: MaHelix and MaSpiral constructors, ConvertToNURB, TrimMultiple for > 50 revolutions): 90 cases (3 seeds x 30), 40 511 lines of knots and control points, 40 508 exact, max abs diff 0 (the non-exact lines are the seed's empty trailing lines); structure mismatches 0.
- Ellipse output 2 (Line segments): break_spline_const_error with the dialog's tolerance (create_ellipse.c:1181-1184) through the existing `brk_spline_lines`; the NURBS deleted. Output 1 (Arc segments): `break_spline_arcs` (mccore @180160440, dumped: FUN_180161910 biarcs) NOT PORTED, greyed in the panel.
- Scenes `acc_helix_*`, `acc_spiral_*`, `acc_ellipse_lines`: ends checked against the formulas: helix r 10, 3 turns, pitch 2, taper 5, initial 30 from (1,2): (9.6603, 7, 0)-(10.1149, 7.2625, 6); r + th p tan(t) = 10.525. Clockwise 2.5 turns: (10,0,0)-(-10,0,5). 120 turns: 3 NURBS. Spiral with pitches changing: per-revolution NURBS; R 20 and Z 20 at the end. Save and reload: `acc_helix_reload` matches.
- References: helix and spiral (lines to one NURBS, decoded above), spl_curv and spl_par_curv (the rectangle order N9-22 reached the spline-from-chain scenes of the merged branch: spline start (100,50,0), first line from the rectangle's new start). 

## 2e. Stages 2 to 5 (second pass)

Skipped commands audited against the decompile (read-only audit report, then fixes). Fixed, with the deciding code:

| Command | Was | Decoded | Settled by |
|---|---|---|---|
| Point Segment, spaced | k * d <= L, extra end point | ceil(L / d) points 0, d, ... (none at the far end on an exact multiple) | createPointsAlongAnEntitySpaced wfacc/core_kernels2.c:1675-1723 |
| Point Segment, count | n >= 2, circle n divisions | any n >= 1; step L / (n - 1) (a circle's last point on its first) | createPointsAlongAnEntity :1612-1650 |
| Point Segment, direction | circles exempt | near-end reversal for every curve (pick parameter against t1 / t2) | :1708-1714 |
| Point Segment | Z of the entity, no boxes written | construction depth in 2D (proj_pt_cp); after a run Distance := L / (n - 1) or Number := round(L / d) | sketcher_point_4.c:441-445, :461-468 |
| Prompts | | Point Position 6710, Point Segment 5834 after the pick, Arc 3 Points 5775-5777, Arc Polar Endpoints 5778 / 5779, Offset 3922 / 3923 | sketcher_point_1.c:169, sketcher_point_4.c:331, sketcher_arc_5.c:1098-1110, sketcher_arc_7.c:214 / :300 / :408, do_xform_offset.c:97 / :311 |
| Line Closest | no result closer than 1e-9; Z of the entity | a Point when closer than tol; construction depth in 2D | createLineClosest wfacc/core_kernels2.c:821-844 |
| Line Parallel | a pick on the line made a duplicate | the through-point pick must be >= tol away; the distance goes back to the panel | CalcLineParallelThroughPoint :4709-4770; sketcher_line_5.c:721-728 |
| Circle Center Point | r > 1e-9, picked radius not written | r > tol; picked radius written | sketcher_arc_2.c:382-384, FUN_1801c0180 :300 |
| Arc 3 Points, Arc Endpoints edge | counter-clockwise always, 1e-12 determinant | start p1, signed sweep through p2 (find_sweep_angle); any two points under tol fail with "Cannot create the arc" | createArc3pt / cr_a_3p / center_3pa core_kernels2.c:2065-2068, :6530-6595; mccad_kernels.c:302 |
| Arc Polar, Polar Endpoints | R < 1e-9, sweep fmod | R <= tol no arc; restrict_angle(1e-6, 2pi) | sketcher_arc_3.c, sketcher_arc_7.c:373, :380 |
| Break at Intersection, circle | n pieces around the seam | the seam stays a break: n hits -> n + 1 arcs | FUN_180049680 break_at_inters.c:1040-1056 |
| Break Circles | sw > 2pi - 1e-9 | \|\|sw\| - 2pi\| < 1e-5 | break_circles.c:82 |
| Offset Entity | zero distance = through the cursor | always the stored distance; direction 0 picked / 1 opposite / 2 both (copies doubled; picked side first); arc result radius failure message 3925 | do_xform_offset.c:1359, :1392-1431, :1569-1574 |
| Offset Chains panel | distance only | Copies, Direction (0 'L', 1 'R', 2 both), Corners None / Sharp / All | do_xform_offset_ctour.c:960, res 6460, mccore_cc.c:229 |
| Rectangular Shapes anchors | 1 and 5 | base point 1 to 9 | FUN_180270fc0 mccad_shapes_core.c:2015-2031 |

Checks: wf_compare 1/321 differ (bbox_cyl_pushpull only, unchanged and left for the main session); chain_compare 0/63; units_test ALL PASS. New scenes with the numbers worked out from the code above (`acc_ptseg_spaced` 0 / 30 / 60 / 90, `acc_ptseg_exact` 0 / 25 / 50 / 75, `acc_ptseg_rev` 100 / 70 / 40 / 10, `acc_ptseg_one`, `acc_ptseg_circle` 4 points the last on the first, `acc_lclose_touch` a Point, `acc_offset_both` +7 and -7, `acc_offset_arc_toosmall` no arc, `acc_rshape_anchor7` x -20..0, y 0..10, `acc_break_circ_hits` 2 arcs and 3 lines). References changed for the arc direction (arc3, aend, soft_aend_*, closearc, breakn_lines, spl_x2nurbs, xf_arc_views, xf_dyn_move, xf_mirror_cw_offset, xf_mirror_x, xf_mirror_y_move, xf_nest_cw_arc, xf_rot_copies, xf_save_reopen, xf_scale_arc_nurbs, xf_t3d_front, xf_trans_join: e.g. arc3 from 28.07 sweep 123.86 -> from 151.93 sweep -123.86, the same circle from its first point clockwise): all from createArc3pt's signed sweep; `xf_save_reopen` shows the sign survives save and reload.

Stage 5: `parts.srf_loft` and `parts.srf_lofted` (flags 0x41 -> 0x45, the +6 normal bit): both scenes also draw Rectangles (`draw:Rectangle|-30,-30|30,30` and three more) and loft through them; a control run drawing the same rectangle with the old bottom-first order gives 0x41 again. The change comes from N9-22's decoded Rectangle order (the chain runs the other way, so (Su x Sv) . cz < 0, the normal-bit rule at viewer/mcsurf_build.c:1293). Those two references are updated; the surfaces are otherwise unchanged (`srf_loft_ruled`, `srf_loft_lofted` scene dumps identical).

## 2f. Picture controls and the fillet kernel (third pass)

Panel pictures: Offset / Offset Chains Direction are the three bitmaps 0x463 / 0x464 / 0x465 (MCButton init, do_xform_offset.c:409-412, mccoreres); Spiral's pitch groups are told apart by the pictures 5548 / 5549 of statics 652 / 654 (mccadres, res 6431, `Initial Pitch` / `Final Pitch` as in the template); Rectangular Shapes Anchor is nine untexted radios in a 3 x 3 grid (controls 1359 ... 1328 of res 6414, value = tab index + 1, create_rectangle.c:612). Drawn by `#img:NAME` / `#cell:r,c` in `geopanel.cpp` from `icons.pack` (`tools/extract_icons.py` now also packs mccadres: the Spiral pictures need the pack rebuilt).

Signed arc direction, six suites (build of 68c487b with mc_arc3 reverted to counter-clockwise, against the same build with signed sweeps; both against a build of toolpaths2 4037987): op 0, solid 0, planes 0, stockplanes 0, tool 0, srf 0 files differ between the two. Against the toolpaths2 baseline the other changes since then remain (op 93 files, solid 63, planes 12, stockplanes 11, srf 4; tool 0). Only two op scenes draw an Arc 3 Points, both counter-clockwise; a variant with both arcs drawn clockwise (`rt_slot_dec1_lines_cw`, `_arcs_cw`, scratch only) gives identical NC (12 and 13 arc moves) with both builds.

`viewer/mcfillet.c`: find_fil_gt with offset_gt_by_dir, gt_offset, gp_ints, l_ints, la_ints, aa_ints, proj_pt_ln, proj_pt_ar, FUN_180060490, FUN_18005c600 (FUN_180060c90 complement: start at the second tangent point, confirmed by the DLL), the corner direction test (a rejected solution makes find_fil_gt return at once: 18005eb0b-18005ec2f). Oracle `tools/tests/fillet_oracle.sh`: the real mccadutil.dll (`tools/peloader`, FIL case): 1200 cases (3 seeds x 400; lines and arcs in all pairings, radii 1.5 to 45, arcs smaller than the radius, styles 0 / 1 / 2, corner direction on and off, both preset indices): max abs diff 3.2e-12, structure mismatches 0.

Fillet Entities: make_fillet's store (trim_ent_to_t(ent, 1, pick, tangent t, 0, intersection t), core_kernels.c:1283); Normal, Inverse and Circle styles enabled. Fillet Chains: create_chain_fillets / FUN_18005f380: pick 1.1 R from the joint on each curve, check_tangency, the corner direction control now enabled (`+ Sweeps`, `- Sweeps`; sketcher_fillet_2.c:477-480), trim_ent_to_t on the running curve. Line Bisect on parallel lines (createLineBisect core_kernels.c:1597-1727). Line Bisect / Line Closest depth 2D.

Not done from item 2: `find_fil_ent` (a spline in a fillet: the old carrier fillet stays, labelled), the Clearance style's lollipop branch and Relief (DAT_180255601 / FUN_1802591c0: greyed), `SelectArcToKeep` (several solutions: Mastercam's proposed one is kept), `make_chamfer_chain` / chamfer_2_ents and calcLinePerpToAnEntity not re-ported (read against the code: the chamfer legs and the perpendicular foot already match; the warnings 0x16f6 / 0x16f7 and the depth of the perpendicular in 2D are not done).

## 3. Not done
- Ellipse output type 1 (`break_spline_arcs`, `FilterEntities`, dumped by this pass) and Break Many Tolerance with Create curves: the panel options stay greyed.
- Break at Intersection / Break Circles on clockwise arcs still produce counter-clockwise pieces (`ge_sub`); association prompt 0x1577 and solid cutters not offered.
- Line Tangent Through Point: the tangent-point pick 6669, the both-lines live pick and the length write-back. Line Bisect parallel lines (createLineBisect :1597-1727) and Multiple (four candidates, FUN_1801cd170). Circle Edge tangent modes, Arc 3 Points tangent, Arc Endpoints tangent, Arc Tangent dynamic, Circle Tangent, Arc Polar Tangent, Inverted Arc (no native consumer found), Line Perpendicular Tangent, Line Endpoints Tangent, Bolt reverse / removal lists, Project plane (XfProject_Squash not dumped), Offset Join / Slot, Point Dynamic with Along locked and Reselect, Point Position Reselect: NOT DONE, remain greyed or missing with the decoded detail in the audit notes above and in the session report.
- A pick filter per command (Mastercam refuses wrong entity types at `get_event`, ours accepts then fails silently) NOT DONE.

## 2g. Independent review of the fixes (fourth pass)

Read-only review of 7e65c0b against the decompile, then the fixes below. Every number in the `rv_*` scenes is worked out from the code cited.

| Item | Was | Decoded | Settled by | Scene |
|---|---|---|---|---|
| Ellipse Line segments, undo | `mod_begin` without `mod_end`: the next edit's undo swallowed the lines and brought the NURBS back | one transaction, the NURBS made and deleted inside it | create_ellipse.c:1166-1205 | `rv_ell_lines_undo` |
| Fillet Chains, style | always Normal | set_fil_small_sweep from the style (Normal 0, Inverse 1, Circle 2) | sketcher_fillet_2.c:483-492 | `rv_filc_inverse`, `rv_filc_circle` (top right corner: from 90 sweep 270; 0 sweep 360) |
| Fillet Chains, - Sweeps | greyed; no flip | enabled; find_fil_gt filters the corner (DAT_180244634), FUN_18005f380 stores start + sweep, -sweep | mccadutil_kern3.c:1102-1106, :2179-2186 | `rv_filc_minus`, `rv_filc_plus` (a clockwise rectangle: no + Sweeps corner) |
| Fillet, entity used up | kept at full length | Fillet Entities: `delete_ent` under tol; Fillet Chains: `replace_ent` with no test, a zero-length line stays | core_kernels.c LAB_180259b50; mccadutil_kern3.c:2151-2169 | `rv_fillet_short`, `rv_filc_r10_full` |
| Offset, clockwise arc | stored counter-clockwise | the entity copied, only the radius replaced | do_xform_offset.c (`local_a6c8 = local_7e18`) | `rv_offset_cw` |
| Obround / D, w = h | a full circle | FUN_18026f6d0 fails on pt_equal ends and stops: nothing | mccad_shapes_core2.c:378-394, :582-589 | `rv_rs_obround_sq`, `rv_rs_d_sq` |
| Offset failure message | r + d | r - d in n_places_an digits, in the message box channel | do_xform_offset.c, 0xf55 | `rv_offset_inside` |
| Project depth | first use only | at every dialog init in 2D | mccad_util_project.c:504-506 | `rv_project_relaunch` |
| Near-tangent hits | geo2d's 1e-9 | gp_ints with la_ints' 2 r tol | mccore_inters.c:512, viewer/mcfillet.c | `rv_trim_tangent` |
| Line Bisect, near-parallel | geo2d's 1e-12 | l_ints' tol test or \|t\| > 1e9 | core_kernels.c:1592-1596 | `rv_bisect_nearpar` |
| Ellipse flag | bit 2 only when closed | 0x13 always: FUN_1805ad6a0 sets bit 2 after ellipse_to_nurbs clears it | core_kernels2.c ellipse_to_nurbs; core_spline.c:2126 | saved record |
| Spline fillet branch | an unreachable claim | refused: `find_fil_ent` is left to the spline branch | (message) | |
| Point Segment, spline | dead code | spline branch reached; pick t on the curve against curve_t1t2; creep from the far end | core_kernels2.c:1675-1723 | `rv_ptseg_spline` |
| Join | cross < 1e-6, overlaps | ends within tol, check_colinear, the six pairs of FUN_180052cf0 (A rewritten, B deleted); arcs FUN_1800529e0 | join_selected_entities.c:300-440, :1677, :1774; surfpick/core_dist.c | `rv_join_kink` |
| Offset Chains | no Mode, no 3D | Move (radio 100, UnBlankAndDelete), Infinite Look Ahead (ccomp_pock param_6) ported; Join, Slot, 3D chains (depth, angle, Absolute / Incremental, Tolerance, Max depth) greyed, NOT PORTED, refused with a message | res 6460 ids 100-103; do_xform_offset_ctour.c:1027, :727-734; check_chains_3d_view | `rv_ofc_move`, `rv_ofc_3d` |
| Point Endpoints | no duplicate test | duplicates of this run dropped by add_repaint_pt (param_3 = 0): pt_equal against the list (mastercam.dll @18032d160, disassembled) | create_point_endpoints.c:134-137, :184 | `pt_end`, `rv_ptend_dup` |

Failures and refusals of this pass go through a message box (`geo_msg`, as Mastercam's post_warning / MCMessageBox): `cam_msg` is overwritten by the next prompt, so `Cannot create the arc` and `The selected objects do not intersect` were never visible.

Settled by reading: anchors of res 6414 (ids 1359..1328 in tab order give values 1..9 at x 20 / 66 / 112, y 278 / 254 / 232: bottom first, as the panel maps). Line Parallel: the through point is `get_perp_pt_dir` tol test, as coded. Break at Points: FUN_18004f9f0 with param_7 = 1 (proj_pt_ln_3d, foot within tol, t inside (tol, 1 - tol)), ours `ge_on` at tol. SelectArcToKeep: Enter takes the proposal, a pick selects (the proposal is what Fillet keeps, labelled). Radius on D and double D shapes: no shape test (create_rectangle.c:572; FUN_18026dd90 returns true for rect_type 1 only): left greyed, message NOT PORTED because it needs `find_fil_ent`. Save and reload: `rv_reload` (a clockwise offset, reversed fillets, zero-length lines).

Correction: wireframe.md's "3 Join" is Slot: res 6460 radios are Move, Copy, Join, Slot = 0 .. 3.

## 2h. Fifth pass: biarcs, the spline fillet, Line Bisect Multiple

`viewer/mcbiarc.c` is `break_spline_arcs` (mccore @180160440): brk_spline_const_err, FUN_180161780 (hidden arguments from the disassembly 180160866-180160882: chord floor tol * 10, chord ceiling 1e+38, the tolerance in XMM3), FUN_180160d30 (normal), FUN_180161910 with FUN_180161410 / FUN_180160f40 (inflections), FUN_180160de0, FUN_180162360, FUN_180160c20, FUN_1801623a0, FUN_180160be0, solve_2x2. Ellipse output 1 (Arc segments) goes through it (create_ellipse.c:1175-1181, the NURBS deleted :1196-1199); only a spline in a plane parallel to Top is handled (find_view_from_ents, :90); otherwise the message 0x1746 "Spline does not lie in the construction plane" (mccadutilres 5958). Scene `acc_ellipse_arcs`: A 50, B 25 gives 136 arcs starting at centre (37.4999, 0), r 12.5001 (the vertex radius B^2 / A = 12.5).

Oracle `tools/tests/biarc_oracle.sh` (real mccore.dll, `tools/peloader/mccore_spl_bi.c`: brk_spline_const_err, FUN_180161780, FUN_180160d30, FUN_180161910 by RVA, world_to_2d hooked to Top): 900 cases (3 seeds x 300: rational ellipses and random planar NURBS of degree 2 / 3 with inflections), 52 844 output lines, structure mismatches 0, every arc / line the same piece; max abs diff 9.9e-06 on arc centres of large radii (relative 2e-07; cause not established; the 2x2 solve of the biarc radii is ill-conditioned for nearly equal normals, which would amplify small evaluator differences, but that was not tested).

`viewer/mcfillet_ent.c` is `find_fil_ent` (mccadutil @18005d780). Fillet Entities with a spline on either side uses it (make_fillet :1216, sketcher_fillet_1.c:529 passes the step limit 1) instead of the former polyline fillet. Trim: trim_ent_to_t's spline branch (mccadutil_kern3.c:1749-1775, the record's t1 / t2 and end points; `mc_trim_spline`). Inferred, marked in comments: the pick parameter on the spline (nearest of 400 samples; get_event is not decoded for splines) and intersect_ents_spl for the trim side (sampled intersection of the two curves; the tangent points when none). A spline in another plane: message "Fillet of a spline in a plane other than Top: not ported" (make_fillet's view branch :1113-1157 not ported).

Oracle FIE cases in `tools/tests/fillet_oracle.sh` (real find_fil_ent, world_to_2d / view_to_world hooked to Top): 600 cases (rational NURBS with lines, arcs, NURBS; radii 0.5 to 8, styles 0 / 1 / 2): 584 agree (return flag equal, t and arc within 3.2e-11). 16 do not (9 differ in the return flag, 7 converge on a different solution). **The cause is not established.** On the first 6 of them (180-case run): in 4 the DLL converged, and the port seeded at the DLL's answer returns it (t exact, arc within 2 ulp), so the DLL's solutions satisfy the port's equations; in 2 the DLL returned 0 where the port converges. A relative change of 1e-13 in a seed changed none of 180 port results, so seed rounding alone does not explain it. The iteration's path differs somewhere; this port is not proven identical on those cases. Scene `acc_fil_spline`: arc centre (25.9137, 6), r 2, line trimmed to x 25.9137, spline trimmed to (27.4559, 4.7265). FIL (lines / arcs) in the same run: 600 cases, 0 mismatches, max 4.5e-13.

`Line Bisect` Solution Multiple is enabled: createLineBisect's four lines (core_kernels.c:1830-1985: directions from the intersection towards each line's far end by the t <= 0.5 rule, a + b and a - b both signs), shown live, third pick "Select which line to keep" (sketcher_line_3.c:383, 6666; state 2 :391-407 deletes the other three). The third and fourth lines (a - b, both signs) are read from the 3D branch (:1947-1975); the 2D branch of the decompile is garbled there (local_a6e0 reused), so in 2D they are inferred from the 3D branch. The numbers of `acc_bisect_multi` are hand-worked from this, unverified against Mastercam. Single keeps its former code: by reading, FUN_1801cd170 keeps the candidate with equal angles to the two picked sides and the smallest angle (FUN_1801ccc40), which is the bisector between the picked sides; this equivalence was argued, not run against the DLL.

Not ported in this pass: FilterEntities for Break Many Create curves (mccad FilterEntities, FUN_18006f810, ChainsToEptrArray) needs FilterChains / FUN_1800e7c90 / filter_pock_boundaries (FUN_1800ee5d0, 360 lines) and the chain conversions; Create curves on a spline leaves it unchanged and now says so ("Create curves on a spline: FilterEntities NOT DECODED, spline left unchanged"; mccad_cmds/sketcher_trim_3.c:1172-1176). The Clearance and Relief styles and SelectArcToKeep's several-solution pick. Chamfer warnings 0x16f6 / 0x16f7 (core strings not in the resource tsv, not decoded), Line Perpendicular depth.

References updated in this pass: new `acc_ellipse_arcs`, `acc_fil_spline`, `acc_bisect_multi`, `acc_bisect_single`. Five scenes from the toolpaths2 spline work that the merge of wfacc changed (spl_par_curv_2d, spl_refit_maxfair, spl_x2n_chain_line, spl_blend_chain_rollback: the rectangle's lines start and run as N9-22 decodes, FUN_18026e060; spl_x2n_chain_arc: arc 180 sweep 180 -> from 180 sweep -180, createArc3pt's signed sweep). They already differed in the first run after the merge and do not reach the code of this pass; no separate baseline build was made. wf_compare: only bbox_cyl_pushpull differs (left as instructed).

## 2i. Sixth pass: the spline-fillet differences, FilterChains, Clearance

**The 16 of 600 spline fillet differences.** Traced with `FIE_TRACE` (every pt_on_ent_from_t / tan_on_ent_from_t / l_ints of the DLL's find_fil_ent, through its import table) and `-DMCF_TRACE` (the same calls from the port): in all 16 the first call already differs, same t, different point. All 16 seeds lie above the spline's t2 (`tools/tests/fie_cases.py` drew them from a fixed 0.2 to 1.8 range): the DLL's evaluator and ours disagree outside the domain. A pick cannot produce such a seed, and inside the iteration the parameter is clamped to [t1, t2] (mccadutil_kern3.c:766-800). With seeds inside the domain: 600 FIE + 600 FIL cases, 0 return-flag or structure mismatches, max abs diff 3.4e-10. Before: 584 of 600 FIE.

**The biarc residual (9.9e-06 on arc centres).** Last-digit amplification: moving every control value of the port's input by 2.3e-16 (relative, one ulp) moves the output arc centres by up to 4.6e-06 (no change of structure in 51 941 lines); of the 244 lines differing from the DLL by more than 1e-9, the same perturbation alone moves 227 by at least a tenth of that difference.

**Break Many, Create curves.** `viewer/mcfilter.c` is FUN_180948e60 with FUN_180947310 (extend_buffer) and FUN_18094bc40, called as FUN_1800ee5d0 calls it (1800ee91f-1800ee936: rmin 0.001, rmax 1500.0, limit 1000 FUN_1809495b0). Oracle `tools/tests/filter_oracle.sh`: real FUN_180948e60, 600 cases (chords, arcs, noisy runs, lines), 13 061 output lines (3 of them the empty line at each file end): every line identical. Wired: `brk_spline_arcs` with the filter: FilterEntities (only with more than one piece), pieces in chain order (reversed arcs: sa + sw, -sw; break_and_append_ent), output stored with the main colour and level (FUN_1800f0970; length <= tol dropped; FUN_1800f1330 reverses an entity that does not continue the previous end), on failure the chain stays (FilterChains :325). Inferred, in a comment: the chain's start and direction (chain_all_selected's FUN_1801dec60 is not ported; here the first stored piece from the spline's start, along the spline). Scene `acc_brk_createcurves`.

**Clearance style.** find_fil_gt's lollipop branch (mccadutil_kern3.c:1160-1370, set_lollipop_corner, sketcher_fillet_1.c:510-522) in `viewer/mcfillet.c`; the panel's Clearance radio and distance are enabled for Fillet Entities. Oracle: the FIL cases again with set_lollipop_corner(1) and clearance 0.5 and 2.25: 900 cases, 33 numeric lines differ by more than 1e-9 (plus a few return-flag differences). In the one run classified (clearance 2.25, seed 3; 4 differing cases), every differing case had an arc in the pair and `both` = 1; no line / line case differed in that run, the other runs were not classified. Attempted and not the cause: the seed moving between (u, v) passes (:1282-1285), the second gp_ints solution. **The residual is unexplained.** Scene `acc_fil_clearance`: R 5, clearance 2, corner at the origin: centre X + n (R - c) = (2.1213, 2.1213), tangent points (6.649, 0), (0, 6.649), hand-checked against the circle equation (unverified against Mastercam).
