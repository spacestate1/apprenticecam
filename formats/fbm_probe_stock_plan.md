# FBM Drill, Probing, Stock Model - plan and survey (started 2026-10-28, branch toolpaths2)

User request 2026-10-28: "lets get the FBM drill stuff done and translated" + "and probing and stock model". Order: FBM Drill, then
Probing, then Stock Model. Rules as always: decoded code with citations, NOT DECODED / ours labelled.

## 1. FBM Drill (opcode 0x88 = 136)
Ghidra: mcmill.dll in ~/mastercam-re/ghidra (project mc2017); mcgeomsld.dll in ~/mastercam-re/ghidra_sld (project sld). Dumps (gitignored):
decomp/fbm/mcmill_fbm_entry.c (BnciFbmDrill, FBM_DrillAssoc / Dialog / SetDefaultParameters, OkayToRunFbm, RedetectFbmDrill, ...),
decomp/fbm/sld_detect.c (solidDetectDrillSuspects & the sdetect_* family, depth 2). decomp/mcmill_all.c holds every mcmill function.

### 1.1 Structure (survey)
- **Start**: ribbon / gallery "FBM Drill" -> mcmill FBM_DrillAssoc @1800a37a0: refused without the 3D product bits (GetProdEnableBits & 2 /
  & 4) or with no visible solid (sld_get_num_solids_visible < 1); product_level_ok(1); OkayToRunFbm(1, 1) @1800b0750; InitOperation(op,
  0x88); the parameters dialog (edit_operation -> FBM_DrillDialog @1800a3b70, CFBM_DrillDlg FUN_1800c7900, OnOK_Page FUN_1800ca3d0);
  then OMregenSelectedOperations -> BnciFbmDrill @1800a0650 (the generation; ~2500 lines).
- **Detection** (mcgeomsld, named symbols): solidDetectDrillSuspects @18006f350 / sld_detect_drill_suspects @18006e340 (the hole faces of
  the solid: sdetect_test_face_cyl / sdetect_test_face_cone / IsSolidFaceCone, sdetect_test_drill_face @18006d1c0, sdetect_combine_drill_data
  @18006c2c0, sdetect_create_view_info @18006ce20 / sdetect_create_min_max_info, the clash tests sdetect_clash_test_drill_faces /
  _spot_drill_clearance), DetectSolidHoleFeatures, default_drill_params (SDETECT_DRILL_PARAMS); results as sDetectDrill objects
  (AllocSdetectDrill) / SDETECT_DRILL_GEOM; stored with the op (sld_detect_create_drill_tpOp, sld_detect_read_drill_tpOp, *_begin_edit /
  _end_edit, sld_redetect_drill_suspects).
- **Operations** (mcmill, methods identified by their error strings - the functions are FUN_ addresses in mcmill_all.c):
  CFBM_Drill: AlignViewAxis FUN_18009ec60, AssignToolToHoleMap FUN_18009fcc0, AssignViewNoToArc FUN_1800a0030, AssignViewToArc
  FUN_1800a0460, CalculateDrillDepths FUN_1800a1080, CalculateOperationsDrillDepths FUN_1800a15e0, CheckArcThreadValue FUN_1800a1700,
  CombineCoaxialArcs FUN_1800a1ed0, CreateDefaultHoleMapFile FUN_1800a3340, FindDbLine FUN_1800a6010, FindDbPoint FUN_1800a6520,
  GetArcBelowChamfer FUN_1800a71a0, InitArc FUN_1800aa410, InitChamferArc FUN_1800ab8e0, InitCoaxialHighZ FUN_1800ac320,
  InitDeepDrillingArcs FUN_1800ac5d0, InitNewOperation FUN_1800ae130, InitOperationsFromArcList FUN_1800ae730, InitSpotOperations
  FUN_1800af1f0, ReadHoleMapFile FUN_1800b0cf0, RemoveSuppressedAndConflictingArcs FUN_1800b21f0 (two passes, the first only arcs with a
  parent: drops arcs FUN_18010d2e0 calls suppressed or FUN_1800fbaa0 >= 0 calls conflicting; refuses the main list), ReverseArc FUN_1800b3430,
  SortArcsForSpotDrilling FUN_1800b71e0, SortDepthPairsArray FUN_1800b75a0, SortPreviewOperationsAscending FUN_1800b7980,
  WriteFbmDrillParentOperation FUN_1800b80c0, Save / WriteFbmDrillToolpathOperation FUN_1800b8740, LoadTreeCtrlByPlane / ByTool
  FUN_1800e6f30 / FUN_1800e7c20. CFBM_DrillDetectUtils (BuildSarcListFromDetectedData FUN_1800c1160, CleanDetectedData FUN_1800c1830,
  LoadFromOperation FUN_1800c2a50, ReloadGeom FUN_1800c3ff0, ToolpathEntMatchesDetectedData FUN_1800c5cf0, UpdateSolidOperation
  FUN_1800c7360, ...). CFBM_Tools (AssignToolToArc FUN_1800fbd30, CalcTipLength FUN_1800fe930, CheckPreferredTools FUN_1800fed70,
  CheckTolerances FUN_1801001b0, InitMastercamToolIDs FUN_180100500, CreateTool FUN_180101730, ...). CFBM_DrillSolidWorksImport (SolidWorks
  hole wizard data: out of scope - no SolidWorks here).
- **Data files** (the install): fbm_drill_hole_map.map (the hole map: diameter ranges -> tools / cycles), fbmtooltable.csv.
- **Output**: a parent op 0x88 and child drill ops (spot / drill / deep drill per hole group), WriteFbmDrillParentOperation /
  WriteFbmDrillToolpathOperation; delete rule: the parent with children (opmanres 4041, formats/align_ops.md).

### 1.2 Mapping to this program (proposed)
- Detection on the part solid's B-rep = this program's XT -> OCCT model (viewer/occt/xt_occt.cpp), the PK_ face / edge queries of the
  sdetect_* code mapped to OCCT (as user decision S-1 does for the surfaces).
- The child ops = this program's Drill ops (CAM_DRILL, cam2d.c), so they post and simulate like the rest.
- Oracles: no stored FBM Drill op found in the local / internet scans (~/mastercam-re/work/hst3d/*scan.tsv): unverified unless a part turns up.

### 1.2a Detection, read so far (decomp/fbm/sld_detect.c)
- solidDetectDrillSuspects @18006f350: clears the output list; no params -> -1; no visible solid -> 0; more than one visible solid or no
  given solid -> sld_select_one_solid (prompt mcgeomsldres 0x1c9); the solid's tag -> FUN_18006a400(tag, list, params); empty entries
  removed; returns the count.
- FUN_18006a400 @18006a400: PK_BODY_ask_faces; the body box diagonal (PK_TOPOL_find_box); every face through sdetect_test_face_cyl;
  each cylinder -> sdetect_test_drill_face(face, body, diag, params, new sDetectDrill (0xf8 bytes, FUN_180069500)) - kept when it
  passes; then sdetect_clash_test_drill_faces(list, cyl faces, params, body, diag) -> FUN_180061a80 -> sdetect_create_view_info ->
  sdetect_create_min_max_info -> sdetect_combine_drill_data(list, params, 0).
- sdetect_test_face_cyl @1800671b0: PK_FACE_ask_oriented_surf is a cylinder (PK_CYL_ask) and, with param_2 = 1 (as called), the face's
  orientation flag is 0 (the face against its surface: a hole wall); keeps the face tag (+0) and the PK_CYL_sf (FUN_180066150 -> +8:
  location +0x20.., axis +0x38.., ref +.., radius +0x50).
- **SDETECT_DRILL_PARAMS** (0xa8 bytes) = the op's FBM block at op+0x1300 (record +0x1368): the detect call passes
  (CFBM_Drill+0x448)+0x140, and +0x448 is set by FUN_1800b4ee0 to the operation (op+0x11c0.. = the FBM block that
  FBM_DrillSetDefaultParameters @1800a3e20 clears, 0x1170 bytes). Confirmed on the shipped Mill_MM.mcam-defaults op 136 (record
  0x2938 bytes; its "Hole.map" path at record +0x1ea0 = op+0x1e38 as the defaults code writes). Fields (defaults code / file value):
  Named from the Hole Detection page (dialog 2795, class ctor FUN_1800df990, DoDataExchange FUN_1800dfca0, UpdateLocalOp
  FUN_1800e09f0, which writes the dialog's local op copy at (page+0x1560) + op offset + 0x20f0):
  | params | op | page control -> stored | use (decoded) | default (code) | Mill_MM op 136 |
  |---|---|---|---|---|---|
  | +0x00 dbl | 0x1300 | 664 Minimum diameter x **0.5** | r + tol < it / k -> no (k = 1000 metric, 39.37 inch) | tol x 1 (inch) / x 25 (metric) | 0.25 (= 0.5 dia) |
  | +0x08 dbl | 0x1308 | 665 Maximum diameter x 0.5 | r - tol > it / k -> no | 5e37 | 12.5 (= 25 dia) |
  | +0x10 byte | 0x1310 | 662 Include blind holes | blind holes kept (clash test) | 1 | 1 |
  | +0x18 dbl | 0x1318 | 0 | NOT READ | 0 | 0 |
  | +0x20 byte | 0x1320 | the main color | NOT READ | main color | 0 |
  | +0x21 byte | 0x1321 | 663 Limit search to plane (cleared when the plane has no view) | the axis must equal +0x22's Z (either sign; -Z = reversed), every drill gets view +0x22 | 0 | 1 |
  | +0x22 short | 0x1322 | 882 the plane's view number | that view | 1 (Top) | 1 |
  | +0x24 byte | 0x1324 | 2934 Include split holes | accept partial cylinders (no 0x1526 loop) | 0 | 0 |
  | +0x28 int | 0x1328 | Sweep angle 2935 Ignore / 667 Minimum / 668 Maximum (0 / 1 / 2) | 1: the smallest section angle >= +0x30; 2: partial faces only, the largest >= +0x30 | 0 | 0 |
  | +0x30 dbl | 0x1330 | 670 Angle | degrees | 0 | 190 |
  | +0x38 dbl | 0x1338 | 672 Step | the section sampling step | 0 | 0.5 |
  | +0x40 dbl | 0x1340 | Co-axial holes 673 (combo, mcmillres 2725..2727: 0 "From 1 plane" -> 1e38, 1 "From 2 planes" -> tol, 2 "From 1 plane if the gap is <=" -> 674 "times the drill diameter") | combine gap factor: same-radius pieces further apart than 2 x R x it stay separate (>= 1e38 - tol: no limit) | 1.0 | 1e38 |
  | +0x48 byte | 0x1348 | 2933 Include chamfers | (the arc builder's chamfer pass reads block +0x188 = op+0x1348) | 1 | 1 |
  Also on the page: 2894 Set normals toward tool plane on detect -> op+0x1d54 (int); 2895 Delete existing features on detect ->
  op+0x22f4 (int; = the FBM block +0x1134 the dialog's detect checks). 
- sdetect_test_drill_face @18006d1c0 (all read): the radius test above; FUN_180066b70(face) = 1 when **no** loop of the face is
  of PK_LOOP type 0x1526 (PK_FACE_ask_loops, FUN_180066c10 = PK_LOOP_ask_type == 0x1526; a full cylinder's end circles are such
  loops - the name of the type is not in our dumps) = the "partial" flag (+0x84); partial and +0x24 off -> no. The section test
  (+0x28 1 / 2): FUN_1800661a0(cyl, step, &min, &max): PK_FACE_find_uvbox; step = max(+0x38, curve_min_step) / k, at most half
  the v span; for v = v0 + step .. v1 - step: the surface point at (u0, v), a plane there with the cylinder's axis / ref,
  PK_FACE_intersect_surf(face, plane) -> each curve's length / radius = its angle (restrict_angle to (1e-6, 2 pi], in degrees),
  min / max over all; a PK failure -> 0 / 0 and the face fails. Then +0x21 (the view). Then the two ends: PK_FACE_find_extreme
  along +axis (ties: ref, axis x ref) and along -axis (ties: ref, the cross product), each point projected onto the axis line
  through the cylinder's location; the topology found at each extreme: an edge -> the face's loop on it (FUN_18006abe0: the
  edge's fins, the fin whose face is this face, its loop), kept when its type is 0x1526; a vertex -> its oriented edges, the
  first such loop. (Mastercam bug, kept: the -axis end's vertex case asks the +axis end's vertex.) The record: FUN_180065fd0
  adds the face to its face list; +0x28..0x38 = the +axis end and +0x40..0x50 = the -axis end, swapped when reversed (x k);
  +0x58 0; +0x60 = r x k (the **radius** in model units: 1000 or 39.37 x the metre radius; the arc code doubles it); +0x68.. the axis (negated when reversed); +0x80 reversed; +0x82 -1 (no view yet);
  +0x84 partial; +0xe8 / +0xec the two end loops (swapped when reversed); +0x98..+0xb0 0.
- sdetect_clash_test_drill_faces @18006b0d0 (param_5 = the body box diagonal): per drill its cylinder record (no record -> the
  whole test fails); two probe rods made with PK_BODY_create_solid_cyl(r', diag, basis): r' = r - 2 t when r > 2 t, r - t when
  t < r <= 2 t, else r (t = nc_tol / k); rod 1 based at the -axis end (+0x40) moved t along +axis, pointing +axis; rod 2 based at the
  +axis end (+0x28) moved t along -axis, pointing -axis (axis and ref negated) - each rod fills the hole and runs a body diagonal
  past the far end. Their 3 faces each go into one tool list (6 per drill: rod 2 at 6i, rod 1 at 6i + 3); PK_TOPOL_clash(the body,
  the rods); every clash of type 7, 1 or 2 clears a flag: tool index / 6 = the drill, (index / 3) % 2 = 0 -> A (rod 2: something
  past the -axis end), 1 -> B (rod 1: past the +axis end). Then per drill: A and B both clear (open at both ends) -> kept, +0x58 = 0;
  exactly one blocked -> +0x58 = 1 (blind): deleted when params +0x10 is off; with +0x21 on it must be open on the view side (not
  reversed: B, reversed: A); both blocked -> deleted. A kept drill whose +axis side is blocked (B) and not reversed is reversed
  (FUN_18006afa0) and +0x80 = 1: a blind hole points out of its open end. The rods are deleted after.
- FUN_180060500(face, faces, loops): for each of the face's 0x1526 loops, the edges of its fins, every face on those edges other than
  this one (each once) with that loop. FUN_180063b60(face, hole, cone_sf): PK_FACE_ask_oriented_surf + PK_CONE_ask; with hole = 1
  only a face against its surface (as for the cylinders).
- FUN_180061a80 (the cone pass): for each drill, its cylinder record; FUN_180060500(face, faces, loops) (NOT DUMPED: presumably
  the faces next to it with the loop they share) and each through sdetect_test_face_cone(face, 1, cyl, &half, &len, &same):
  FUN_180063b60 = the cone's PK_CONE_sf; with a cylinder given: the cone axis parallel to it (|dot| >= 0.9999996) and coaxial
  (the location's offset across the axis < tol / k) -> the half angle, `same` = 0 when antiparallel, then (the decompiler drops
  this jump; the disassembly at 180066f1a..180066f59 shows it) the cone face's extremes along its axis -> len = their distance
  along the cylinder axis x k. A cone found: +0xf0 = 1; on end loop +0xe8: +0x98 = the included angle 2 x half (deg, x 114.59),
  +0xa8 = len, negative angle when `same` (after flipping it when the cone axis is opposite the drill axis); on +0xec: +0xa0 /
  +0xb0, negative when not `same`.
- sdetect_create_view_info @18006ce20: +0x21 on -> every drill's view = +0x22. Else per drill: the first view 1..n_views whose Z
  equals the axis; failing that (not +0x58, not reversed) the first whose Z equals -axis -> that view and FUN_18006afa0 (reverse
  the drill: swap the ends +0x28 / +0x40, +0xb8 / +0xd0, the loops +0xe8 / +0xec, the cone data +0x98 / +0xa0, +0xa8 / +0xb0,
  negate the axis, +0x88 / +0x90 swapped and negated); none -> make_view_k(axis) + find_view_m(.., create 1) = a new view.
- sdetect_create_min_max_info @18006c9f0: per drill with a view, the solid's extent along that view's Z (FUN_180066690: every
  face's PK_FACE_find_extreme along +Z and -Z, world_to_view, min / max) -> +0x90 min, +0x88 max (cached per view in an
  SDETECT_VIEW_MIN_MAX list {view, min, max}, 0x18 bytes); a failure -> 0 / 0.
- sdetect_combine_drill_data @18006c2c0 (read, the branch detail to port from the source): copies the ends to +0xb8 / +0xd0
  (the overall extent); for each pair (i < j): FUN_18006a2b0 = coaxial (axes equal or opposite unless both +0x58; j's +axis end
  within tol of i's axis line); diameters differing by >= tol -> a "stepped" pair (FUN_18006aca0(larger-or-equal, other, 0):
  only the overall extents +0xb8 / +0xd0 widen); same diameter -> the gap between them in i's view (+0x40 limit above, and no
  other coaxial piece in between) and, unless called with param_3, FUN_18006aca0(i, j, 1) (the ends, loops and cone data of
  the outer pieces taken), j's faces appended to i, partial only when both are, j deleted; when the pieces' order says the
  merged hole points the other way: FUN_18006afa0 + +0x80 = 1 and its view looked up again as in the view pass.
### 1.2c The dialog pages -> op fields (UpdateLocalOp of each page: stores to (page+0x1560) + op + 0x20f0; controls from each
page class's DoDataExchange; an MCReal member's value is at member + 0xc00; labels from mcmillres dialogs)
- Hole Detection (2795): the params table above.
- Setup (369; UpdateLocalOp FUN_1800ea270, DDX FUN_1800e8d80): op+0x11d0 635 Automatic initial hole detection; +0x11dc the
  Sorting combo 650 (also op+0x22f0); +0x11e4 654 Subprograms (also op+0x22ec); +0x11e8 655 Absolute (vs 656 Incremental); +0x11e0
  the 4th-axis rotation axis (combo 652 / 0 when off); +0x1ca8..+0x1cab 3138 / 3139 / 3140 / 3142 (the create-points options, one
  check each - order NOT CHECKED); +0x1cac 3141 Level; +0x1348 2933 Include chamfers (the same field as the detection page's);
  +0x1d60..+0x1d70 the 2D-sort settings (NOT READ).
- Spot Drilling (2797; FUN_1800f1100, DDX FUN_1800ef960): op+0x13c8 683 Spot drilling; +0x13d0 686 Max. percentage of finished
  hole (default 80); +0x13d8 687 Max. depth (default 0.25 in / 6.25 mm); +0x13e8 688 Combine spot drill operations that share the
  same depth and retract values; +0x13e4 691 Use this tool for all spot drill operations (the tool: 692 / 693 / 697).
- Pre-Drilling (376; FUN_1800e6950, DDX FUN_1800e6180): op+0x1a88 718 Pre-drilling; +0x1a89 3026 Pre-drill pilot holes only;
  +0x1a90 699 Minimum diameter (default 0.25 in / 6.25 mm); +0x1a98 722 Increment (same default); +0x1aa0 723 Stock to leave (on)
  / +0x1aa8 721 its value (0.04 in / 1 mm).
- Tools (443; FUN_1800f3510, DDX FUN_1800f1c90): op+0x1bd8 726 Diameter matching tolerance (0.001 in / 0.025 mm); +0x1e30 3030 Use
  both inch and metric tools; +0x1be0 742 Use tools in part file; +0x1be4 744 Use tool library (its path op+0x1ad0); +0x1be8 2931
  Create as needed; +0x1bec 746 Create only standard sizes; +0x1bf0 747 Consider flute length; +0x1bf4 the 734 / 736 / 738 radio
  (one tool for all depths / one for each depth / length increments of); +0x1bf8 740 that % (default 25); +0x1c00 a combo (748 "If
  hole exceeds flute length"?, NOT CHECKED); +0x1c08 730 Tool tip match tolerance (degrees); +0x1c10 733 Allow flat end mill;
  +0x22e8 3334 Use Block Drill.
- Depths (455; FUN_1800c0650, DDX FUN_1800bead0): op+0x1c18 the clearance method (combo 760), +0x1c20 its value (762), +0x1c28
  clearance incremental (= method != 2, or 764 Incremental); +0x1c7c the retract method (761), +0x1c30 its value (767), +0x1c80 and
  +0x1c74 retract incremental (method != 2 or 775); +0x1c70 = +0x1c78 = 1; +0x1c2c 765 Use clearance only at the start and end of an
  operation; through holes: +0x1c38 768 (group), +0x1c3c 712 Additional break through, +0x1c40 the 714 Distance / 715 % of tool
  diameter radio (index: 0 Distance, 1 %), +0x1c48 769 the distance, +0x1c50 2930 the %; +0x1c58 753 "Avoid tool bottoming out by" combo, +0x1c5c 757 Distance /
  758 %, +0x1c60 759, +0x1c68 2929; blind holes: +0x1c88 770 (group), +0x1c8c 713 Additional break through, +0x1c90 716 Distance /
  717 %, +0x1c98 772, +0x1ca0 2931. The clearance / retract method combos (760 / 761): 0 "Solid height plus" (mcmillres 205), 1 "Stock model plus" (206), 2
  "Manual" (207), 3 "Top of hole plus" (144), 4 "Top of coaxial holes plus" (145); 753: 0 "No adjustment" (208), 1 "Adjust tap /
  ream up" (209), 2 "Adjust pre-drill down" (210).
- Deep Drilling (2796; FUN_1800be550, DDX FUN_1800bdc50): op+0x13a8 1000 Deep drilling; +0x13b0 684 "Use Deep Drilling when hole
  depth to diameter ratio is greater than" L/D (default 10); +0x13ac combo 677 the mode (0..3, see InitDeepDrillingArcs); +0x13b8 678 Primary face depth percentage (default
  50); +0x13c0 combo 679 Deep drilling cycle (default 2; the cycle InitNewOperation gives an op whose arcs are all deep-drill arcs).
- Circle mill / Helix bore / Hole milling pages (InitPage FUN_1800bc830 / FUN_1800dec20 / FUN_1800e5720): they do not store
  through (page+0x1560) like the others (the op+0x1f88..+0x2140 block FBM_DrillSetDefaultParameters fills is theirs; op+0x1fc8 / op+0x2108
  are the depth offsets the writer adds for circle mill / helix bore, op+0x1fd0 / op+0x2130 their pre-drill switches): field by field
  NOT MAPPED. (The stores found at op+0x1fc0..0x2270 by the same scan are FBM Pocket's pages, 0x11b.. classes.)
- Not mapped yet: Features (2799 = the 0xb590 manager), the circle mill / helix bore / hole
  milling pages, Hole Mapping.

### 1.2b Generation (mcmill, read so far)
- BnciFbmDrill @1800a0650: OkayToRunFbm(1, 1); a CFBM_DrillDlg work object (0xf8a8 bytes, FUN_1800c7900, not shown) and a
  0xb590-byte manager (FUN_1800cc810; +0x1560 = the dialog, +0xb230 = dialog + 0x9c50 = the CFBM_Drill); the op loaded
  (FUN_180114e60); its solid entity must be a SOLID_ID (0x10000) ("BnciFbmDrill() - invalid solid entity ID!"); NCI 1005 "FBM
  drilling toolpath" + eos; FUN_1800d4380 -> FUN_1800d3ca0(mgr, op, 1) (below); when it made ops: CFBM_DrillDetectUtils::
  UpdateSolidOperation FUN_1800c7360, WriteFbmDrillToolpathOperation FUN_1800b8740 (the child ops, with the file naming rules
  saved / zeroed / restored around it), the children with tool paths appended to the Toolpaths tree's list (+0x3660); when the
  root group's +0x11b8 is set, FUN_1800b2340(op).
- FUN_1800d3ca0 (regenerate from the stored detection; 272 lines): FUN_1800d1270; the op must be 0x88; the FBM tool library
  (CFBM_Tools::Load FUN_18010b9e0, 664 lines; "tool library does not exist!" when the op's library file is missing);
  CFBM_DrillDetectUtils::LoadFromOperation FUN_1800c2a50 (931 lines: the stored detection back from the solid op) - the stored
  counts must agree; the op's FBM block (0x1d0 x 8 bytes) copied to (CFBM_Drill+0x448); BuildSarcListFromDetectedData
  FUN_1800c1160; per stored hole (0x388-byte records: +0xea / +0x200 / +0xe8 shorts, +0xe0 / +0xdc / +0xe4 ids, the tool at +4
  through FUN_180109090) a CFBM_ReloadInfo; CheckCounts FUN_1800a1880; FUN_1800aa090(CFBM_Drill, op+0x29c, sarcs, n) (the
  operations from the arcs - presumably InitOperationsFromArcList's caller); FUN_1800a6ed0 -> the preview ops (0x318 bytes
  each), FUN_1800d00c0 per op ("%ld:%ld  %s..." progress), FUN_1800d43d0.
- The first run (detection) is the dialog's: FUN_1800d0940 / FUN_1800d4780 -> FUN_1800c1e90 -> FUN_1800c1b90 (copies the 0xa8
  params into (CFBM_Drill+0x448)+0x140, solidDetectDrillSuspects, CFBM_DrillDetectUtils FUN_1800c2270 / FUN_1800c5910 /
  FUN_1800c4890 / FUN_1800c7760; no holes -> warning mcmillres 0xea; a detect failure -> 0x12d) -> BuildSarcListFromDetectedData.

- The operations from the holes: FUN_1800aa090(CFBM_Drill, op, sarcs, n, reload_tools, make_ops, reload_info, params) (both
  paths): (CFBM_Drill+0x448) = the FBM block = **op+0x11c0** (0x1d0 x 8 bytes copied in; block +0x140 = the detection params, +0x910
  = the tool library path = op+0x1ad0, +0xa20 = op+0x1be0, +0xc70 = op+0x1e30 are CFBM_Tools::Load's arguments); the op must be
  0x88; CFBM_Tools::Load FUN_18010b9e0 when asked or not loaded; ReadHoleMapFile FUN_1800b0cf0 (339 lines); FUN_1800aab60 (the
  arcs: 666 lines - per detected hole InitArc FUN_1800aa410 (329) + FUN_18009e280, then per hole the hole-map class FUN_1800a8e20
  (1..11; < 5: a value 2 x radius (sarc +0x38) - (block +0xa18 + tol), 8 / 10 / 11 -> cycle kinds 5 / 7 / 8), the chamfers
  (GetArcBelowChamfer FUN_1800a71a0, InitChamferArc FUN_1800ab8e0 516), InitDeepDrillingArcs FUN_1800ac5d0 (1435),
  InitCoaxialHighZ FUN_1800ac320, CombineCoaxialArcs FUN_1800a1ed0, FUN_18010d880 (699), ...); then InitOperationsFromArcList
  FUN_1800ae730 (205: RemoveSuppressedAndConflictingArcs, InitNewOperation FUN_1800ae130, InitSpotOperations FUN_1800af1f0 324,
  CalculateOperationsDrillDepths, SortPreviewOperationsAscending); nothing made -> FUN_1800a1e10(.., 0). Roughly 8000 lines of
  mcmill in all, the tool choice (CFBM_Tools: GetClosestMatchingTool FUN_180105e20 299, ...) included: NOT READ YET.

- **The sarc** (0xe0 bytes, BuildSarcListFromDetectedData FUN_1800c1160: calloc n x 0xe0; detections with a view < 1 are
  dropped, "bad view_n"): sarc +0x00..+0xbf = sDetectDrill +0x28..+0xe7, sarc +0xc8 = (long) +0xe8, which CleanDetectedData FUN_1800c1830 has set to the hole's index (+0xec = -1; NULL entries and
  views < 1 dropped). After the detection FUN_1800c7760 turns +0x28 / +0x40 / +0xb8 / +0xd0 into **view coordinates** of the hole's
  view (world_to_view in place). So: +0x00 end 1,
  +0x18 end 2, +0x30 blind, +0x38 radius, +0x40 axis, +0x58 reversed, +0x5a view, +0x5c partial, +0x60 the solid's max / +0x68 min in
  the view, +0x70 / +0x78 the cone included angles at end 1 / end 2, +0x80 / +0x88 the cone lengths, +0x90 / +0xa8 the overall
  extent ends.
- **The hole map file** (ReadHoleMapFile FUN_1800b0cf0; CreateDefaultHoleMapFile FUN_1800a3340 makes an empty <fbm folder>\Hole.map
  when missing; the op's file is block +0xc78 = op+0x1e38, replaced by that default when it does not exist). Per line: the text
  from "//" on is dropped; empty lines skipped; split at "," (DAT_18031af68) into
  at most 10 fields, each parsed with atoi / atof into a 0x50-byte entry (defaults from CFBM_Drill +0x1d58..):
  | field | entry | meaning (the shipped file's header) |
  |---|---|---|
  | 0 | +0x48 byte | units: first char 'M' -> metric (empty: block flag) |
  | 1 | +0x08 short | hole type (1..11 kept) |
  | 2 | +0x10 dbl | diameter (> tol kept) |
  | 3 | +0x18 dbl | threads (pitch); types 2 / 3 must have >= tol, else "invalid thread value in 4th field" and the line is skipped |
  | 4 | +0x20 dbl | tip angle |
  | 5 | +0x28 dbl | length |
  | 6 | +0x30 byte | left hand |
  | 7 | +0x38 long | FBM tool ID |
  | 8 | +0x44 int | Mastercam tool ID |
  | 9 | +0x40 int | source |
  Exactly 10 fields needed (else "invalid format"). The tool: by the Mastercam tool ID in the FBM tool list (FUN_180109090(.., id, 1))
  checked by FUN_1800a9e70 -> source 1 and that tool's FBM id; else by the FBM tool ID (FUN_180108fa0) or AssignToolToHoleMap
  FUN_18009fcc0: the tool type for the hole type (Tap LH 5 -> the left-hand byte), GetClosestMatchingTool for D within +- the diameter
  matching tolerance (op+0x1bd8); none and a tap: threads needed ("invalid thread value"; FUN_1800fcd40 finds them), the tap drill
  size FUN_180108870 and a search for it (then FUN_18010a2f0's size); still none -> no tool; FUN_18009fc40 stores the result. +0x00 = the entry number (1-based); a second entry with the same diameter (within tol): "duplicate
  diameter entries"; FUN_1800b77a0 after (presumably the sort). Shipped data: ~/mastercam-re/files/fbm_drill_hole_map.map (12
  lines), fbmtooltable.csv (the FBM tool table: kind D / M / T..., diameter, number size, name, ..., units, ...).
- **The arc** (CFBM_Drill::InitArc FUN_1800aa410(CFBM_Drill, arc, sarc, parent, kind, plane, type, p8, p9, p10, p11, p12, p13);
  starts as a copy of the template at CFBM_Drill +0x46f8, **0x318** bytes, the stride of the arc list CFBM_Drill +0x4a18 / count +0x4a20): +0x00..+0xbf = the sarc (+0x38 radius, +0x5a view);
  +0xc0 the arc's own id; +0xc4 the parent arc id (p4, checked by FUN_1800a7720: "invalid parent arc ID!"); +0xc8 the tool id
  (p11); +0xcc the detection index (p5); +0xd0 short the **cycle kind** (p8); +0xd4 flags; +0xe0 short the hole type (p7); +0xe2 short the plane; +0xe4 short the view (from the sarc); +0xe8 the diameter 2 r; +0xf0 p12 when > 0; +0xf8 p9;
  +0x108 end 1 (view coords) / +0x120 the same in world (view_to_world), +0x138 / +0x150 end 2, +0x168 / +0x180 and +0x198 / +0x1b0 the
  extent ends; +0x100 = |z1 - z2| (the depth in the view); +0x258..+0x2d7 cleared. Ordered so end 1 is the higher (view Z): the
  view / world / extent pairs and the two cone (angle, length) pairs are swapped when end 2 is higher, and the same for the sarc
  copy at +0x00. The plane: p6 > 0 -> FUN_18009ffd0(arc, p6); else the arc's +0xe2 plane or the Toolpaths' selected plane when its
  view is compatible (FUN_180119570), else AssignViewNoToArc FUN_1800a0030; the rotary axis (GetRotaryAxisFromParameters >= 0 ->
  FUN_18009eaf0); p11 > 0 -> +0xc8 (after FUN_1800a8b20 / FUN_180111030 checks); the tool: CFBM_Tools::AssignToolToArc
  FUN_1800fbd30(tools, arc, kind = FUN_18010a4d0(type), kind == 5, p10, p13 or CFBM_Drill +0x4a10); CheckArcThreadValue
  FUN_1800a1700; FUN_1800b4ba0 (adds it).
- **FUN_1800aab60** (the arcs from the sarcs, 666 lines; param_4 = the reload info on a regenerate, 0 on a fresh run):
  1. every sarc -> InitArc(.., kind 1, type 0) -> a reference list (local_eb8, FUN_18009e280);
  2. per sarc ("%d-%d %s" progress, mcmillres 0xe4): the hole-map entry template (CFBM_Drill +0x1d58); the type = FUN_1800a8e20
     (fresh; a hole-map entry with an FBM tool > 0 gives the tool id) or the stored one; FUN_1800fba60 (a cone at end 2: > 0 =
     FUN_1800fea30(0, 2 r), else -1) > 0 -> the chamfer case: GetArcBelowChamfer FUN_1800a71a0, a trial InitArc, FUN_1800a59c0 ...
     (decides whether the hole keeps its tip: p9 = the end-2 cone length sarc +0x88 or 0; NOT FULLY READ); a reload tool id > 0 ->
     FUN_1800a8b20 and p9 = its +0x6c0; kind: type 1..4 -> 1 with p10 = 2 r - (block +0xa18 + tol), type 8 -> 5, 10 -> 7, 11 -> 8,
     else 1 (p10 = -1); p12 = the hole-map entry's +0x18 (threads) when > tol; then InitArc(arc, sarc, 0, kind, 0, type, view?, p9,
     p10, tool, p12, refs) and FUN_18009e280(arc, ...) adds it;
  3. chamfers: block +0x188 on and a chamfer length at either end (FUN_18010eb30 / FUN_18010eac0 > 0) -> the arc also into a
     chamfer list; each through InitChamferArc FUN_1800ab8e0 (two chamfer arcs; on a reload matched to the stored toolpath with
     ToolpathEntMatchesDetectedData); chamfers that fail -> a warning list (mcmillres 0x7d, FUN_1800a3490) unless suppressed;
  4. fresh: FUN_1800b0960; then FUN_1800aec80, InitDeepDrillingArcs FUN_1800ac5d0, InitCoaxialHighZ FUN_1800ac320,
     CombineCoaxialArcs FUN_1800a1ed0 (both over CFBM_Drill +0x4a10 = the arc list); reload: the stored extra arcs back
     (FUN_1800b4580, FUN_1800afc50); block +0xa28 > 0 -> FUN_18009e5e0 (when deep drilling made arcs) and CFBM_Tools FUN_18010d880.
     Returns the arc count (CFBM_Drill +0x4a20).
- Arc helpers: FUN_18009e280(CFBM_Drill, arc, new id, check, list = main list +0x4a10) adds an arc (a new id from FUN_1800a9030 when
  it has none, else found by FUN_1800a7710); FUN_1800b4ba0 registers it. GetArcBelowChamfer FUN_1800a71a0: a trial arc and
  FUN_1800a6ef0 = the arc directly under a chamfer (more than one: "this should not happen!").
- **InitCoaxialHighZ** FUN_1800ac320: per group of arcs at the same view X / Y (tol) in parallel same-direction views, every member's
  +0x210 = the group's highest top (+0x118).
- **CombineCoaxialArcs** FUN_1800a1ed0: clears +0x2d8..+0x317; per group of same-diameter (tol), same X / Y, same-direction arcs with
  more than one member: the deepest one (lowest bottom +0x148) gets +0x2d8 = 1, +0x2e0.. = the top member's top point (+0x108..),
  +0x2f8.. its extent point (+0x168..), +0x310 its +0x1c8; the top member (highest +0x118), when it is a pre-drill (kind 2) and not
  suppressed, is removed; a kind-1 top over a kind-2 bottom makes the bottom kind 1.
- FUN_1800b0960 (fresh runs): for the circle mill / helix bore arcs (types 8, 10) with tools: one common tool = the smallest-diameter
  tool of the same type among theirs (with consider flute length op+0x1bf0: long enough); every such arc whose diameter range
  (FUN_180105810) admits it takes it (+0xc8).
- **InitChamferArc** FUN_1800ab8e0(CFBM_Drill, hole arc, out top, out bottom): with a chamfer at the top (FUN_18010eb30 > 0): angle A =
  arc +0x1d8, the outer diameter Do = D (+0xe8) + 2 x the cone length (+0x1e8) x tan(-A / 2); the tool diameter FUN_1801054d0 (>= Do -
  tol, else "unable to get chamfer dia for top chamfer"); a chamfer arc: the hole's data with the top raised by the cone length, the
  bottom = top - the tip length of Do at A, radius Do / 2 (or the tool's / 2 when larger, the top raised by that tip length), not
  blind; InitArc(parent = arc +0xc0, index, type **7**, kind **4**, tip -A) twice around FUN_1800a5b00 (an existing chamfer arc of that
  size); +0x200 = Do; none -> +0xc0 = -1. The bottom chamfer (only when the top one is not blind - local flag) the same from
  ReverseArc(arc) with +0x1e0 / +0x1f0 ("unable to get chamfer dia for bottom chamfer"). Returns the count.
- **InitDeepDrillingArcs** FUN_1800ac5d0 (1435 lines; read at the flow level): candidates = arcs of hole type 1 and kind 1 or 2 whose
  length / diameter (+0x100 / +0xe8) >= the L/D limit op+0x13b0, with a tool (AssignToolToArc when none); kept when the mode op+0x13ac
  (combo 677) != 0 or the primary percentage op+0x13b8 is in (tol, 100). Per candidate, with the longer tools of its size
  (FUN_1801066e0 / FUN_180110470, stored to the part by StoreToolsInArrayToDb):
  (the mode = combo 677, items mcmillres 2733..2736 from the page's OnInitDialog FUN_1800bde20; the cycle combo 679 lists the
  machine's drill cycles, FUN_1800a8990)
  - mode 0 "Split through holes between faces": a primary arc from the top to op+0x13b8 (Primary face depth percentage) % of the
    length with the shortest tool whose flute length (+0x6d0) covers it, and a second arc **from the other face** (ReverseArc of a copy,
    flag +0x1fc = 1) for the rest with the shortest tool covering that; both added;
  - mode 1 "Drill to maximum and finish with a long tool" (with Create as needed): tools in length steps - one arc per tool, each to its flute length (capped by the hole), tools
    created when needed ("unable to create tool long enough!"); op+0x1bf0 = 1 / op+0x1bf4 = 0 forced;
  - mode 2 "Drill to maximum and warn user": a tool shorter than the hole -> the arc cut to the tool's length; else dropped from the candidates;
  - mode 3 "Cut entire hole with long drill": the first tool long enough replaces the arc's tool; else dropped;
  then per location the largest-diameter member is made kind 1; arcs that got no deep-drill treatment -> the warning mcmillres 0x7b.
  (The exact length bookkeeping of each mode: NOT READ.)
- **Cycle kinds** (arc +0xd0; FUN_1800b0940: 1..4 -> 1 = drilled, 5..8 -> 0 = milled, else -1): 1 the hole itself (types 1..4 and
  the rest), 2 pre-drill (FUN_1800aec80), 4 chamfer (InitChamferArc: type 7), 5 circle mill (type 8), 7 helix bore (type 10), 8
  block drill (type 11); 3 and 6: NOT SEEN YET (spot drilling / deep drilling are the candidates).
- **Pre-drilling** FUN_1800aec80 (block +0x8c8 = op+0x1a88 Pre-drilling on): for each arc with flags +0xd4 = 0: FUN_1800fda70(tools,
  arc, pilot-only op+0x1a89, ...) -> the pre-drill diameters (below); for a blind tapped hole (tool FUN_18010b9d0, or tool kind
  6) the bottom Z moves by op+0x1c5c mode 1: op+0x1c60, 2: op+0x1c68 % of the tool value, negated when op+0x1c58 = 2; per diameter a
  new arc: the parent's data with radius = d / 2, InitArc(parent = arc +0xc0, index arc +0xcc, plane arc +0xe2, type 1, kind 2,
  tip 118 deg) and added ("%ld %s", mcmillres 0xd3); the depth of the 2nd and later pieces when the previous diameter + tol < the
  hole's: NOT READ (lost in the decompile).
- **InitOperationsFromArcList** FUN_1800ae730 (progress mcmillres 0xd6): FUN_180103b50(tools); the arc list reordered: FUN_1800b66d0 by
  diameter (+0xe8) ascending, FUN_1800b6cc0 grouped by tool (+0xc8, in first-seen order), FUN_1800b6f50 grouped by plane (+0xe2)
  (< 1 -> fail); FUN_1800b41b0 clears every +0x1fa; then for each arc not yet used (+0x1fa = 0): skipped when
  FUN_18010d2e0(tools, arc) or FUN_1800fbaa0(tools, arc) >= 0 (both NOT READ; the latter -> FUN_18009f410 warning); the arc's tool =
  FUN_1800a8b20(arc +0xc8); no tool and a parent -> flag bit 0 off; with a tool (its +8 bit 0, or local flag) and flags 0:
  FUN_1800a7fe0 = the arcs with that tool (+0xc8) and plane (+0xe2); split by kind in the order **8, 1, 2, 3, 4, 5, 6, 7**
  (FUN_1800a7c10: +0xd0 = kind); in each, FUN_1800a7360 = the distinct (top Z +0x118, bottom Z +0x148, blind +0x30) triples (tol),
  sorted by SortDepthPairsArray FUN_1800b75a0; per triple FUN_1800a7830 = its arcs, RemoveSuppressedAndConflictingArcs FUN_1800b21f0 (two passes, the first only arcs with a
  parent: drops arcs FUN_18010d2e0 calls suppressed or FUN_1800fbaa0 >= 0 calls conflicting; refuses the main list),
  and **InitNewOperation** -> a preview op appended to CFBM_Drill +0x1bf0 ("%ld %s"). Each arc used: +0x1fa = 1, FUN_18009f410; flags
  != 0 -> FUN_1800fb230. After: InitSpotOperations FUN_1800af1f0, CalculateOperationsDrillDepths FUN_1800a15e0,
  SortPreviewOperationsAscending FUN_1800b7980 (the ops ordered by kind **8, 3, 2, 4, 1, 5, 6, 7** - block drill, spot,
  pre-drill, chamfer, drill, circle mill, -, helix bore -, each kind's ops through FUN_1800b7b50, NOT READ; a count mismatch -> "sort
  failed!"). So **one child op per (tool, plane, kind, top Z, bottom Z, blind)**.
- **The preview op** (0x130 bytes, ctor FUN_18009cf20; InitNewOperation FUN_1800ae130(CFBM_Drill, kind, plane, tool, arcs)): +0 id
  (FUN_1800a91d0: 1 + the largest id); +0x08 its arcs; +0x30 the views (FUN_1801148c0(plane, the first arc's view); none -> "no views
  defined!"); +0x58 the tools; +0x80 short the drill cycle: -1 for a milled kind, 0 for a drilled one; when every arc has +0x1fc or
  +0x25c set: op+0x13c0 (block +0x200, default 2); the tool: kinds 4 / 6 with a tool value > tol use it; kind 4 or milled ->
  FUN_180106570 (FUN_1801066e0 with the chamfer diameter +0x200 for types 7 / 9, minus the tools that interfere), else FUN_1801066e0
  (the matching tools list, 349 lines, NOT READ in detail); a drill tool (Cnc::Tool::is_drill_tool) -> its own cycle; kind 8 -> cycle 0; a tap
  (FUN_18010b9d0) -> cycle 3; +0x82 kind; +0x88 the description: FUN_1800ae4c0 builds a temporary operation (FUN_1800af8a0(.., op,
  2, 0), the active plane) and op_make_description.
- **InitSpotOperations** FUN_1800af1f0 (op+0x13c8 Spot drilling on; FUN_1800a1e10 first): old kind-3 preview ops removed; from the
  ops of kinds 1, 2, 5 (not 6) the root arcs (FUN_1800a81b0: the arc, or its parent when it has one and is not kind 1; each once);
  those where FUN_1800fbaa0(tools, arc, 0x20) >= 0 (the spot tool conflicts) are set aside and get flag 0x20 + the warning at the
  end; SortArcsForSpotDrilling FUN_1800b71e0: per plane, per hole location (view X / Y +0x108 / +0x110 within tol) the arc with the
  highest top (+0x118); per plane: its arcs (FUN_1800a8190), the spot tool CFBM_Tools::GetSpotDrill FUN_180109170 (none: "no tool
  defined!"), the depth groups (FUN_1800a7360 / FUN_1800a7830), in each the distinct diameters (FUN_1800a7560, +0xe8, sorted) and
  per diameter (FUN_1800a79f0) InitNewOperation(kind **3**, plane, spot tool) inserted at the front of the op list. So spot ops
  come first, one per (plane, depth group, hole diameter). (Combine spot ops op+0x13e8 / one tool op+0x13e4: read by GetSpotDrill /
  later - NOT CHECKED.)
- **CalculateOperationsDrillDepths** FUN_1800a15e0: empty preview ops dropped (FUN_1800b2160); per arc **CalculateDrillDepths**
  FUN_1800a1080(CFBM_Drill, preview op, arc) -> arc +0x238 clearance, +0x240 retract, +0x248 the stock top in the plane's view
  (FUN_1800a9c20), +0x250 depth (all view Z; start -1e38): the plane (arc +0xe2; none -> "unable to get named view!");
  - clearance by method op+0x1c18 with value op+0x1c20: 0 Solid height plus -> value + arc +0x1c8; 1 Stock model plus -> value + the
    stock top; 2 Manual -> value (absolute; with op+0x1c28 set it falls to method 0); 3 Top of hole plus -> value + the larger of
    FUN_1800a9cb0's two values (capped by the stock top); 4 Top of coaxial holes plus -> value + the smaller of FUN_1800a8c60(arc, 1)
    and the stock top; else method reset to 0 + "invalid clearance method!". Retract the same
    with op+0x1c7c / op+0x1c30 / op+0x1c80 ("invalid retract method!"); same incremental flags and clearance < retract -> warning
    "clearance adusted to retract level" (sic);
  - depth by kind: 1, 2, 5, 7, 8 -> the arc bottom (+0x148), FUN_18009e350; for the drilled ones (not 5 / 7) unless the parent arc is
    a chamfer-type: the tool's tip data (FUN_18010a360) on -> depth -= CalcTipLength FUN_1800fe930, and for a through hole (or arc
    +0x1f0 >= tol) with break-through on: depth -= the distance (data +0x10) or % x D (data +0x18); 3 (spot) -> the smaller top -
    FUN_1800fe690's spot depth, not below the bottom; 4 (chamfer) -> the bottom; others -> "unknown operation type!".
  FUN_1800b4c50 then collects each op's distinct clearance / retract / stock-top / depth values (+0x90 / +0xb8 / +0xe0 / +0x108 lists;
  SortRealArray, depths in the reverse order); FUN_1800a9260(.., 1, list) = the largest (-1: the smallest).
- **Tools** (CFBM_Tools = CFBM_Drill +0x450; its +0x10 = op+0x1ad0, so tools+0x108 = op+0x1bd8 diameter matching tolerance,
  +0x118 = op+0x1be8 Create as needed, +0x11c = op+0x1bec standard sizes, +0x120 = op+0x1bf0 consider flute length, +0x124 = op+0x1bf4
  the length radio, +0x138 = op+0x1c08 tip match tolerance). The kinds passed around are **Mastercam tool types** (viewer/toollib.c's
  names: 3 Drill, 4 Tap RH, 5 Tap LH, 6 Reamer, 7 Bore, 8 CBore, 9 CSink, 17 Taper mill, 19 Endmill3 Bull, 20 Block Drill, 22 Bradpt
  Drill): FUN_18010a4d0(tools, hole type) = the first tool type for it in the (hole type, tool type) table at tools +0xc8 / count +0xd0,
  filled by the CFBM_Tools constructor FUN_1800fa020 in this order: 1 -> 3 Drill, 22 Bradpt, 2 Spot, 1 Center; 2 -> 4; 3 -> 5; 4 -> 6;
  5 -> 7; 6 -> 10 Flat endmill, 8 CBore; 7 -> 9; 8 -> 10; 9 -> 12 Chamfer mill; 10 -> 10; 11 -> 3 (so the lookup gives 1 -> Drill,
  6 -> Flat endmill); FUN_18010b9d0(t) = t is 4 or 5 (a tap); FUN_18010b780(t) = t is 7 or 9.
  - Conflicts / suppression: FUN_1800fbaa0(tools, arc, mask) = the index of the arc's root (GetParentArc) in the warning list (tools
    +0x118 / +0x120, 0x318 entries, matched by +0xc0) with (mask = 0 or its +0xd4 & mask), else -1; FUN_18010d2e0 = the arc or its root
    has +0xdc bit 8 (suppressed).
  - **CFBM_Tools::Load** FUN_18010b9e0(tools, use part tools = op+0x1be0, 1, both units = op+0x1e30, library path = op+0x1ad0, op
    id): SetParentFbmOpId (the op must be FBM: "invalid FBM opcode"); (1) the part file's tools (the internal tool system's
    assemblies with a main tool; kept when both units is on or the tool's units match; checked by FUN_1800fcd90 / FUN_1800fcf90 (bad
    -> the warning text) / FUN_180103ff0 / FUN_18010b6a0 (already listed)) added with source 1 (3 for this op's own tools) when use
    part tools is on; (2) the tool library file (Cnc::Tool::TpLibToolList, when the file exists) added with source 2; (3) the
    nominal sizes table (CNominalSizes FUN_1800f7f40: <fbm folder>\FbmToolTable.xlsx through Excel when installed, else
    FbmToolTable.csv - FUN_1800f68d0): each size looked up with GetClosestMatchingTool and added with source 4 (FUN_1800fb6e0;
    unmatched sizes get GetDefaultToolTipAngle); (4) with Use Block Drill (op+0x22e8) the block drill tools (machine tool number
    9999) with source 1. Then FUN_1801073f0 / FUN_180100480 / FUN_180103730.
  - **FbmToolTable.csv** (FUN_1800f68d0; same reader as the hole map: "//" comments, "," fields, at most 10): 0 the type key: M ->
    10 (Flat endmill), D -> 3 (Drill), R -> 6 (Reamer), CS -> 9 (CSink), T -> 4 (Tap RH), else "unknown tool type" (0; with threads ->
    tap); 1 the outside diameter; 2 the size name; 3 the description; 4 threads; 5 the minor / tap drill diameter; 6 'Y' -> left hand
    (a tap becomes 5 Tap LH); 7 'M' -> metric; 8, 9 two values (NOT NAMED). Taps need 0 < tap drill < outside and threads >= tol
    ("BAD TAP DATA ..."); other non-endmill sizes with a minor >= the outside: "BAD DATA"; sizes in the other units converted; the
    largest diameter kept. Shipped: ~/mastercam-re/files/fbmtooltable.csv (no header; e.g. "D,0.0059,97,Drill,,,,I,0.02,0.005",
    "T,0.0118,,30-318 UNM,318,0.0083,,I,0.04,0.0125").
  - **AssignToolToArc** FUN_1800fbd30(tools, arc, tool type, type == 5, min dia, ...): arc tool +0xc8 = 0, flags +0xd4 = 0; threads (arc
    +0xf0 > 0) with a non-tap type -> warning; min dia > the hole -> warning, ignored; the needed length L = FUN_1800fd180 when consider
    flute length (or a CFBM flag) is on; hole types 8 / 10 (circle mill / helix bore): threads 0 and FUN_180105810(L) -> the target
    and min diameters; Create as needed + standard sizes: the target = FUN_180107fa0 (kind 4 chamfer: the standard size <= D - tol) or
    FUN_180108570 (the standard size >= D + tol, with the threads), dropped for a kind-1 tap when it differs from D by more than the
    diameter matching tolerance; else the target = D (arc +0xe8). The search record (target, min, max, L, threads, tip / angle
    tolerances, source mask, ...) -> GetClosestMatchingTool (hole type 11: FUN_180105a10); a pre-drill arc (kind 2) takes the found
    tool's diameter; not found -> one retry without the length (and the tip check only for types 7 / 9); found within the diameter
    tolerance -> the arc's tool (Create as needed: FUN_180111030 / FUN_180104cc0 first); else with Create as needed: the tip angle (arc
    +0xf8, > 180 -> 0), the length, ToolDiaInterferes FUN_1801119c0, **CreateTool** FUN_180101730 (below) -> the arc's tool; FUN_1800fc380 records the result / warnings.
  - Standard sizes: FUN_180108570(tools, type, D, threads) = the closest size **below** D from the part tools (FUN_180108800 source
    1), the library (source 2) and the nominal table (FUN_1800f60a0), with which one won (1 / 2 / 4); FUN_180107fa0 the same for the
    closest size **above**. Tip length FUN_1800fea30(D, angle) = tan(90 - angle / 2) x D / 2 (0 when either is ~0); CalcTipLength
    FUN_1800fe930(tool) by Mastercam tool type (+4): center drill 1 uses the larger of its two angles (+0x20 / +0x400); 2, 3, 4, 5, 9,
    12, 13, 21, 22 use |tool +0x3f8 - D|; the rest (0, 6, 7, 8, 10, 11, 14..20) 0; unknown types -> "unknown tool type!" (the exact
    case fall-through: NOT CHECKED). The tip / break-through data of an arc (FUN_18010a360): op+0x1c38.. for through holes, op+0x1c88..
    for blind ones ([0] on, [1] additional break through, [2] 0 distance / 1 %, +0x10 distance, +0x18 %).
  - The needed length FUN_1800fd180(tools, arc, list): from the arc top (the higher of +0x1c8 / +0x118) to its bottom (+0x148); with
    a list, extended by the coaxial arcs at the same location (FUN_180104980, other views through view_to_view) and - consider flute
    length on with "one tool for all depths" (op+0x1bf4 = 0) - by every arc of the same type and diameter (within the diameter
    tolerance; threads equal) (FUN_1801047b0); + the tip length for the arc's tip angle (arc +0xf8, > 180 -> 0; none: 90 deg, or 0 for
    hole types 8..10); + FUN_180107d30 (> tol); rounded up to a multiple of the length increment and at least a minimum (FUN_18010a200: the minimum from the nominal
    sizes table FUN_1800f5f70; the increment = op+0x1bf8 % x D when consider flute length is on with "Create tools in length increments
    of" (op+0x1bf4 = 2)).
  - **Pre-drill diameters** FUN_1800fda70(tools, arc, pilot only, out tool, out list): only kind 1, kind 5 with op+0x1fd0, kind 7 with
    op+0x2130; the arc's tool (none -> -1); its pilot diameter (tool +0x2a0; <= the diameter tolerance -> 0; with op+0x1be8 and
    op+0x1bec FUN_180108c90 computes one): taps (types 4, 5) need one above op+0x1bd8... (else the warning "%s\n\n" + mcmillres 0x103
    "Pilot hole diameter not defined in tool definition!" and -1); types 6..8 without one get 0.9 x D (warning 0x103 + 0x105 "Pilot
    hole diameter set at 90% of tool diameter"); the largest pre-drill = the pilot, or with Stock to leave (op+0x1aa0) D - 2 x
    op+0x1aa8; then from op+0x1a90 (Minimum diameter) in steps of op+0x1a98 (Increment, at least the diameter tolerance + tol) up to
    it, plus the largest itself; sizes >= the tool's D (or within the tolerance of it; not for flat endmills) dropped; sorted
    descending; without Create as needed (or a minimum <= tol) only sizes up to the largest existing Drill (FUN_180108220 type 3)
    stay; pilot only (op+0x1a89): only the pilot size stays; sizes above the root arc's diameter - tolerance dropped.
  - **GetSpotDrill** FUN_180109170(tools, out, arcs): spot drilling off -> none; "Use this tool for all spot drill operations"
    (op+0x13e4) with the tool op+0x1654 (it must be a spot type FUN_180111c80: type 2, or 1 when op+0x13f4 = 1) - else every spot-type
    tool; no candidates and not Create as needed -> "no spot drills exist!"; the spot depth per arc FUN_1800fe690(arc, angle) = min(
    the spot max depth op+0x13d8, the tip length of the spot diameter (when the max % op+0x13d0 <= 100), the tip length of op+0x13d0 %
    of it, the hole's length) (the diameter: the arc's, or a chamfer tool's when larger); the needed diameter per arc FUN_1801054d0 =
    the largest that does not interfere (ToolDiaInterferes; from 1.1 x D down in steps of 1/32 in or 1 mm), then snapped to standard
    sizes of CSink 9 / Drill 3 / Flat 10 / any (op+0x1bec); the tool: the smallest spot tool >= the needed diameter, or a created
    one (CreateTool type 2, 90 deg tip... the angle literal 0x4056800000000000 = 90); each hole checked with mcgeomsld
    sdetect_clash_test_spot_drill_clearance (the spot cone against the solid, NOT READ) - an interfering spot tool is replaced by a
    smaller one (FUN_180108800) or, failing, the arcs get flag 0x20 (mcmillres 247 "TOOL: spot drilling tool interferes with
    solid"); "cannot determine spot drill dia!" / "cannot create spot drill!".
  - **CreateTool** FUN_180101730(tools, type, D, threads, flute length, tip angle, left hand, out): tip <= tol -> the default
    (GetDefaultToolTipAngle FUN_180107090), > 180 -> 180; a tap needs threads >= tol ("invalid thread value"); a matching existing
    tool of that type is looked for first (within the diameter tolerance; chamfer mill 13: any size); a Mastercam tool entity (init_tl_ent,
    mill or drill kind) numbered by InitMastercamToolIDs FUN_18010a860: D, threads (<= tol -> 0), left hand, the flute length (<= tol ->
    2 x D for mill tools, 10 x D for drills), shoulder = 1.1 x flute, overall = 1.1 x shoulder; percentages 100 / 100; the default
    feed / speed block by FUN_1801072e0's class (NOT CHECKED in detail); chamfer mill 13: tip D x 1.1 ... flute D / 8, 45 deg; taper mill
    17 without an angle: 45 deg; then the holder (AdjustLengthForStickout), CFBM_Tools::StoreEntAssoc (into the part), added with
    source 3 (FUN_1800fb180) and listed in tools +0x58; a created tool whose pilot exists -> the warning mcmillres 0xe6 / 0xf9.
  - After the arcs (op+0x1be8 Create as needed): FUN_18009e5e0 lengthens each created tool (list CFBM_Drill +0x4a8) used by arcs to
    the longest need among them (flute = that, shoulder 1.1 x, overall 1.1 x, written back to the tool system); FUN_18010d880 then
    finalises the created tools' lengths (FUN_18010b400) rounded up to whole units (FUN_18010d770: up when the fraction >= 0.5, or
    any fraction when forced).
  - **GetClosestMatchingTool** FUN_180105e20(tools, out, search, arc, list = tools +0xe8): FBM tool records 0x6e8 bytes (+0 id, +8
    source bits 1 / 2 / 4, +0x10 short Mastercam tool type, +0x6b0 diameter, +0x6b8 thread, +0x6c0 tip angle, +0x6c8 the 2nd angle,
    +0x6d0 flute length, +0x2d8, +0x481 preferred); the window: max = min(search max, target... + tolerance tools +0xb60), none when
    max <= min - tol; taps without a thread value take FUN_1800fcd40's ("unable to determine thread value for %lf diameter"); per tool:
    the type must match (with tools +0x174c = 3, Drill 3 and Bradpt 22 count as one), the source mask must hit, D >= min, the
    length >= L, a tap's thread within tol, a bull mill's corner radius >= search +0x28, the tip / 2nd angle within tolerance
    (FUN_180111ba0; 180 / 90 count as 0), and D <= max + tol; the closest |target - D| wins; with several exact (tol) matches: for
    a through hole the first Drill (3), for a blind hole the first Bradpt Drill (22) (both via FUN_180108fa0), else the first
    preferred (+0x481).
- **The child operations** (WriteFbmDrillToolpathOperation FUN_1800b8740, 1827 lines; stack names read as op offsets, op = the
  0x2938 operation copy; op offsets as in viewer/cam_fileop.c's table: record = op + 0x68): WriteFbmDrillParentOperation
  FUN_1800b80c0 first (the parent op 0x88; < 0 -> fail); SetMachineGroupPtr; per preview op ("%ld - %ld"; no arcs / no views ->
  error): the first arc (and its parent arc via CFBM_Tools::GetParentArc when it has one; views must be parallel); the opcode from the
  kind: **kinds 1..4 -> 2 (Drill), 5..6 -> 0x12 (18, Circle mill), 7 -> 0x6a (106, Helix bore), 8 -> 0x132 (306, Block drill)**; the
  op made by FUN_1800af8a0 = InitOperation(op, opcode) with the plane active (so the **defaults-file op** of that type), op+0x1e28 =
  10.1, op+0xc94 = param; kind 3 ops listed (spot); the tool (FUN_18010ac40, or for block drill FUN_1800a4b80 / FUN_1800a4e50 /
  FUN_18010acb0) and its holder; the plane. For opcode 2 (or a milled kind via FUN_1800affd0 / FUN_1800b0490): the arc (reversed by
  ReverseArc when arc +0x1fa > 0 - NOT CHECKED), the stock (FUN_1800c2610, the stock box) for the top:
  - top of stock op+0x280 = (the arc top - the stock / 0 when the hole is not under the stock (bVar9)) + the hole top Z, op+0x288
    incremental = op+0x1c70; clearance op+0x108 = FUN_1800a9260(.., 1, preview +0x90), op+0x110 incremental = op+0x1c28 (then
    relative to the top), op+0x111 = 1, op+0x2b1 = op+0x1c2c; fails (<= -1e38) -> top + 2 in / 50 mm ("error in calculating
    clearance plane!"); retract op+0x118 = feed plane op+0x128 = FUN_1800a9260(.., 1, preview +0xb8), op+0x120 = op+0x130
    incremental = op+0x1c74, op+0x121 = 0 ("error in calculating feed plane!" -> arc +0x1c8 + 2 in / 50 mm); depth op+0x138 = the
    preview's first depth (+0x110[0]) (circle mill / helix bore: + op+0x1fc8 / op+0x2108 when not ...; helix: - a value from
    FUN_1800b0490), op+0x140 incremental = op+0x1c78; a tap (tool kind 3, FUN_18010a360 data): + the tip length FUN_1800fea30 and an
    extra (fixed or % of D), and for Drill op+0x1200 = 1 / op+0x11f8 = the extra (NOT CHECKED which drill field);
  - op+0xd0 = op+0xc90 = the parent op id; op+0x95 = 1; op+0xd8 = 1; op+0xe9 = not "independent of solid"; op+0x289 = op+0x28b = 0;
  - RewriteOperation FUN_180117730; then per arc of the preview op whose depth equals the op's: a drill point at its top (arc +0x120,
    world): init_tp_ent 'C' with a solid-feature toolpath header (init_tp_ent_header(.., 4, 0x1c, op id): view, the arc's
    +0xcc / +0x58 / +0x30 / +0x60 / +0x68 / +0x38 / +0x5c / +0x5a / +0x148, the top in view coords), or with "Create operations
    independent of solid" a DB point (FindDbPoint) and init_tp_ent 'P'; named "%s %d" (mcmillres 0x9d1, 1-based); store_ent_assoc;
    op+0x88 / op+0xd4 counted; op+0x94 = 1; RewriteOperation; op+0x11e0 > 0 with > 1 point -> sorted (FUN_1800711b0 /
    FUN_180075850, the sort method = op+0x11e0);
  - after all: the create-points geometry (FindDbPoint / FindDbLine for the 3138 / 3139 / 3140 options, coaxial points merged);
    FUN_1800a24a0; GroupOperations FUN_180115180; FUN_18010e4c0.
- **Hole types** (the names list built in the CFBM_Drill constructor FUN_18009c710, index = type): 0 Undefined (mccoreres 5014),
  1 Drill (mcmillres 195), 2 Tap RH (197), 3 Tap LH (199), 4 Ream (200), 5 Bore (201), 6 Counter bore (202), 7 Chamfer (203),
  8 Circle mill (265), 9 Circle Mill - Chamfer (268), 10 Helix bore (266), 11 Block Drill (338).
- The hole type of an arc, FUN_1800a8e20(CFBM_Drill, sarc, .., .., out entry): 1 by default; block +0x1128 on and FUN_1800a41e0(
  2 r, view) true with view 1 -> 11; FUN_1800aa020(block +0xd80) true -> 10 (block +0xd84 on) or 8, when the matching size limit
  (blind: block +0xd88 / +0xd90 with FUN_1800a86b0 >= 0, +0xda8 / +0xdb0 when < 0; through: +0xd98 / +0xda0) is on and <= 2 r + tol;
  still 1, blind, cone length at end 2 ~0 and FUN_1800a86b0 >= 0 -> 6; finally the hole map entry for 2 r (FUN_1800a1bd0) wins when
  its type > 0. Helpers NOT READ.
### 1.3 Next
Read (2026-10-28): detection, the arcs, the hole map, tools (load / match / create / standard sizes / spot / pre-drill / lengths),
the op building, depths, the child op writer, the dialog pages except the milled-hole pages and Features. Still open:
- FUN_1801066e0 (the matching-tools list) and FUN_1800b7b50 (the order within a kind) in detail; the exact length bookkeeping of
  the deep-drilling modes; sdetect_clash_test_spot_drill_clearance; the circle mill / helix bore child ops (FUN_1800affd0 /
  FUN_1800b0490) and their pages; the Features page (the 0xb590 manager: the list, hole-type reassignment); WriteFbmDrillParentOperation.
- **Port plan** (next step): detection on the OCCT model (each PK_ call above mapped: PK_FACE_find_extreme -> the face's extreme
  along a direction; the probe rods + PK_TOPOL_clash -> BRepAlgoAPI_Common of the rod with the body; loop type 0x1526 -> a closed
  circular edge loop on a full cylinder), labelled the S-1 stand-in; the hole map and FbmToolTable.csv read from the install copy;
  tools from this program's tool library; children as CAM_DRILL ops (opcode 2 from the defaults file), spot / pre-drill / chamfer /
  drill kinds in Mastercam's order; the milled kinds (circle mill 18, helix bore 106, block drill 306) NOT PORTED at first.

## 2. Probing (op 150)
mcprobe.dll (336 KB) + mcproberes.dll (6 MB resources), Renishaw Productivity+ files (PP_pdbProbeDatabase, probecatalogue.renpc1).
12 stored "Probe" ops in the local scan (oracles). Not surveyed yet.

## 3. Stock Model
Toolpaths > Stock Model = mcmill stock_model_dialog (layout.txt 53842 / 53915 / 53992). The stock model computation is probably the
verify engine (mcverify / the Simulator); not surveyed yet. No stored Stock Model op in the scans.
