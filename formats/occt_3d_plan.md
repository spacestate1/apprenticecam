# 3D commands on OpenCASCADE: decode plan (Solids / Surfaces / Model Prep / Transform)

Status 2026-10-23: **plan only, nothing implemented.** OCCT 7.9.3 (headers `/usr/include/opencascade`,
`Standard_Version.hxx` OCC_VERSION_COMPLETE "7.9.3") is the approved stand-in for Parasolid. This file lists, per
greyed ribbon command, where Mastercam implements it, what is decoded, which Parasolid (PK_*) calls it makes, the
OCCT equivalent, where OCCT will behave differently, and what must still be decoded. Model for the depth of a full
decode: formats/extrude.md (Solids > Extrude, the only 3D command live today, built on our own 2.5D kernel
viewer/solid25.c, not on OCCT).

Rule 1 applies: everything below cites its source. **NOT DECODED** = not found; **NOT TRACED** = the code exists but
the call path from the command to it was not followed; **[inferred]** = a reading of the code that is not proven.
PK option tokens are given as raw numbers: the Parasolid headers (enum names of the tokens) are not available here, so
token -> name mapping is NOT DECODED.

## Sources used

| what | where |
|---|---|
| ribbon (tab / group / id / label / FT function / DLL) | formats/ui_ribbon.json (tabs "Solids", "Surfaces", "Model Prep", "Transform") |
| FT command index (FT name, tooltip / icon ids, export status) | formats/ui_commands.tsv |
| FT aliases (`CALL_FUNC` + `PARAM`) for entries that are not DLL exports | ~/mastercam-re/files/mastercam.ft (e.g. `create_block_solid` = `create_block` PARAM 1) |
| which commands are live | viewer-qt/src/mainwindow.cpp `localRun` / `cmdOk` (lines ~150-200), viewer/core_ui/cui_geo.c `gcmd[]` / `geo_find` |
| Parasolid imports of mcgeomsld.dll | `x86_64-w64-mingw32-objdump -p ~/mastercam-re/files/mcgeomsld.dll` (PSKERNEL.dll: 303 names; psbodyshop.dll: 14 PSB_* names). Copies: ~/mastercam-re/work/occt/plan/sld_pk.txt, sld_bodyshop.txt |
| mccad.dll / mccadutil.dll imports | objdump -p: **no PSKERNEL import in either** (mccad imports `sld_basic_primitive`, `sld_primitive_tag`, `sldModifyBBoxSolidPrim`, ... from MCGeomSld; mccadutil imports no PK and no `sld_xform_solid`) |
| decompiles made for this plan (Ghidra headless, `-readOnly`, projects not modified) | ~/mastercam-re/work/occt/plan/: `sld_core.c` (mcgeomsld: ps_boolean_*, ps_chamfer, ps_create_cone/sphere/torus, ps_fillet, sld_fillet, sld_new_const_fillet, sld_do_fillet, sld_new_revolve, sld_revolve, sld_revolve_from_chain, sld_newRevolve, sld_chamfer, sld_new_chamfer, sld_boolean, sld_new_boolean, sld_do_boolean, sld_xform_solid), `sld_help.c` (FUN_180036310, FUN_180036580, FUN_180032890, FUN_18003e630, primitive builders, ps_swing_sheet / ps_swing_wire + callees), `sld_light.c` (shell, thicken, var / face-face fillet, loft, sweep, draft, trim, pattern + callees), `sld_apps.c` (sldChamfer, app ctors FUN_18014e490 / FUN_1801bc100 / FUN_1801b9b50 / FUN_1801a1980 ...), `cad_prim.c` (mccad create_block / cone / cylinder / sphere / torus), `xf.c` (mccadutil do_xform_* entry points), `sld_mem.txt` / `prim_mem.txt` (MemDump of default globals), `baml/*.txt` (tools/baml_panel.py outlines of every mcgeomsld.ui panel), `mccadres_dialogs.tsv` (tools/pe_dialogs.py on mccadres.dll), `pk.py` (PK-per-function summariser), `run.sh` / `mem.sh` (the headless commands) |
| WPF panels | ~/mastercam-re/dotnet/mcgeomsld.ui/solids/*/*.baml (+ `MCGeomSld.UI.Solids.*ToolTips.resx`), ~/mastercam-re/dotnet/mccad.ui (Surface* views), ~/mastercam-re/dotnet/mccadutil.ui (XformDynamic, SurfaceNormal*) |
| MFC dialogs | exports/mccadutilres_dialogs.tsv (Transform), mccadres.dll dialogs 6415-6424 (primitives; work copy above) |
| strings | decomp/extrude/sldres_str.tsv ("sld N" = mcgeomsldres string N), exports/mccoreres_strings.tsv, formats/mccadres_strings.tsv |
| earlier decodes reused | formats/extrude.md, formats/bbox_toolplane.md sec. C (sld_basic_primitive, block / cylinder), formats/bbox_pushpull.md 1.2 (SLD_PRIMITIVE_PARAMS fill), formats/xt.md (XT reader), formats/mcam.md (SOLIDXMT / SURF_ID records), formats/mcam_write.md (solids are written back as stored XT) |
| parallel work (not mine, read only) | the Transform decode under ~/mastercam-re/work/tree_transform (decomp/mccadutil_xform.c, decomp/mccore_xform.c) - its worked example: Translate params `CXfTransParams` ctor FUN_18003ac00, keys in "XFORM.PRM" |

Common to every mcgeomsld command (from the code read here): lengths go to Parasolid divided by 1000 (IsMetric) or
39.37007874015748 (inch), i.e. Parasolid works in metres (FUN_180036310 radius / dVar8; formats/bbox_toolplane.md C);
failures are reported through `FUN_18004f490(1, pk_error, sld_msg)` and `message_box_ok(msg, 201, 3)`, caption sld 201
"Error Processing Solid"; every command app is a SolidsAppBase (ctor FUN_180164c60; extrude.md 1.1) with a PK mark for
undo of the preview (PK_MARK_create / PK_MARK_goto).

## Overall table (in proposed porting order)

Live today: only Solids > Extrude (`sld_newExtrude`, own kernel solid25.c) and, on the Transform tab, Offset Entity
("Entity" / `do_xform_offset`), Offset Chains ("Chains" / `do_xform_offset_ctour`) and Project (`do_xform_project`) -
all three are the wireframe implementations in viewer/core_ui/cui_geo.c (`gcmd[]` names "Offset"/"Entity",
"Chains"/"Offset Chains", "Project"), shared with the Wireframe tab, and act on wireframe only. Everything else on the
four tabs is greyed.

**Finding (not fixed - outside my scope):** Surfaces > Create > **Offset** (id 53557, FT `OffsetSurfaces`, MCCAD.DLL)
has the label "Offset", its DLL starts with "MCCAD", so `MainWindow::cmdOk` sets rlNoGeo_ = false and
`geo_find("Offset")` returns G_OFFSET: the surface command is shown live and starts the wireframe Offset Entity.
The Surfaces > Extrude collision is already guarded by the FT check (mainwindow.cpp line ~194); Offset needs the same.

| # | tab > group | ribbon label (id) | FT -> implementation | Parasolid core | OCCT | status / blocker |
|---|---|---|---|---|---|---|
| T1 | Transform > Position | Translate (53663/53664) | MCCADUTIL `do_xform_trans` 0x180047570 | solids only: `sld_xform_solid` (history) or brick copy | gp_Trsf::SetTranslation + BRepBuilderAPI_Transform | wireframe part needs no OCCT; solid part blocked on Q1/Q2 (history) |
| T2 | Transform > Position | Translate to Plane (53665) | `do_xform_trans3d` 0x180047d80 | same | gp_Trsf (plane-to-plane) | as T1 |
| T3 | Transform > Position | Rotate (53666) | `do_xform_rotate` 0x180034490 | same | gp_Trsf::SetRotation | as T1 |
| T4 | Transform > Position | Mirror (53669) | `do_xform_mirror` 0x180017700 | same | gp_Trsf::SetMirror (orientation flips) | as T1 |
| T5 | Transform > Size | Scale (53677) | `do_xform_scale` 0x180038080 | same + `sld_xform_changing_scale` | uniform: gp_Trsf::SetScale; XYZ: BRepBuilderAPI_GTransform | XYZ on solids differs (sec. T5) |
| T6 | Transform > Position | Move to Origin (53668) | `do_xform_movetoorigin` 0x180046b70 (`translate_silent`) | same | gp_Trsf | as T1 |
| T7 | Transform > Layout | Rectangular Array (53674) | `do_xform_array` 0x1800463f0 | copies | gp_Trsf per copy | as T1 |
| T8 | Transform > Offset | Entity / Chains (53671/53672) | `do_xform_offset` / `do_xform_offset_ctour` | - | - | **live** (wireframe) |
| T9 | Transform > Position | Project (53667) | `do_xform_project` 0x180028a70 | - | - | **live** (points only per cui_geo.c G_PROJECT) |
| T10 | Transform | Dynamic (53662), Roll (53670), Stretch (53676), Distribute (53675), Geometry Nesting (53673, MCCORE `do_nest_g`) | mccadutil `DynamicXform` 0x1800eea50 (WPF XformDynamicManagerView), `do_xform_roll` 0x180030940, `do_xform_stretch` 0x180046df0, `TransformDistribute` 0x180013060 | wireframe (+ solids via the same xform engine) | gp_Trsf | wireframe work first; not an OCCT task |
| S1 | Solids > Simple | Block (53580) | mastercam.ft `create_block_solid` = mccad `create_block` PARAM 1 -> `create_sz_block` 0x180013ae0 -> mcgeomsld `sld_basic_primitive` type 0xf | PK_BODY_create_solid_block (+ PK_BODY_transform) | BRepPrimAPI_MakeBox(gp_Ax2, dx, dy, dz) | geometry decoded; panel defaults NOT DECODED |
| S2 | Solids > Simple | Cylinder (53579) | `create_cylinder` PARAM 1 -> type 0x11 | CRCYSO (full) / sheet + spin (partial) | BRepPrimAPI_MakeCylinder(ax2, r, h[, angle]) | as S1 |
| S3 | Solids > Simple | Cone (53582) | `create_cone` 0x180034370 PARAM 1 -> type 0x10 | CRCOSO / PK_BODY_create_sheet_planar (partial) | BRepPrimAPI_MakeCone(ax2, r1, r2, h[, angle]) | as S1 |
| S4 | Solids > Simple | Sphere (53581) | `create_sphere` PARAM 1 -> type 0x12 | PK_BODY_create_solid_sphere / partial via circle + imprint | BRepPrimAPI_MakeSphere | as S1 |
| S5 | Solids > Simple | Torus (53583) | `create_torus` 0x18002f330 PARAM 1 -> type 0x13 | PK_BODY_create_solid_torus / sheet circle + spin | BRepPrimAPI_MakeTorus | minor <= major enforced |
| S6 | Solids > Create | Revolve (53585) | MCGEOMSLD `sld_newRevolve` 0x1800eaf70 -> app FUN_18014e490 -> `sld_revolve` 0x1800e8fc0 | PK_BODY_spin + PK_TRANSF_create_rotation + PK_BODY_boolean_2 | BRepPrimAPI_MakeRevol + BRepBuilderAPI_Transform + BRepAlgoAPI_Fuse/Cut | panel decoded (BAML); defaults of angles NOT DECODED |
| S7 | Solids > Create | Boolean (53588) | `sld_do_boolean` 0x18010e5c0 -> app FUN_1801a1980 -> `sld_boolean` 0x18010d590 | PK_BODY_boolean_2 / PK_FACE_boolean_2 (tol 1e-5 m) | BRepAlgoAPI_Fuse/Cut/Common (+ SetFuzzyValue) | panel decoded; tolerance / disjoint rules differ |
| S8 | Solids > Modify | Constant Fillet (53594/53595) | `sld_do_fillet` 0x1800e3670 -> app FUN_1801bc100 -> `sld_fillet` 0x1800e2540 -> `ps_fillet` 0x180046d30 | PK_EDGE_set_blend_constant + PK_BODY_fix_blends | BRepFilletAPI_MakeFillet | overflow / propagation tokens NOT DECODED |
| S9 | Solids > Modify | One Distance / Two Distance / Distance and Angle Chamfer (53598-53601) | `sldChamfer` 0x18010ab80 (FT PARAM 0/1/2) -> app FUN_1801b9b50 -> `sld_chamfer` 0x18010a260 -> `ps_chamfer` 0x18003ff90 | PK_EDGE_set_blend_chamfer + PK_BODY_fix_blends | BRepFilletAPI_MakeChamfer (Add(d,e) / Add(d1,d2,e,f) / AddDA) | reference-face convention to decode |
| S10 | Solids > Create | Loft (53586) | `sld_newLoft` -> `sld_loft` / `ps_loft_chains` | PK_BCURVE_make_matched, PK_BCURVE_combine, PK_BODY_sew_bodies (+ imports PK_BCURVE_make_bsurf_lofted) | BRepOffsetAPI_ThruSections | parametrisation differs; light decode only |
| S11 | Solids > Create | Sweep (53587) | `sld_newSweepV8` -> `sld_sweep_v8` | PK_BODY_sweep, PK_BODY_spin, PK_CIRCLE_ask, PK_TRCURVE_ask (no PK_BODY_make_swept_body import) | BRepOffsetAPI_MakePipeShell / MakePipe | Mastercam's path decomposition NOT DECODED |
| S12 | Solids > Modify | Shell (53602) | `sld_do_shell` -> `sld_shell` -> `ps_shell_body` | PK_BODY_hollow_2 | BRepOffsetAPI_MakeThickSolid | light |
| S13 | Solids > Modify | Thicken (53603) | `sld_do_thicken` -> `sld_thicken` -> `ps_thicken_sheet(_op)` | PK_BODY_thicken_3 | BRepOffsetAPI_MakeThickSolid::MakeThickSolidBySimple / MakeOffsetShape | light |
| S14 | Solids > Modify | Draft Faces / Plane / Edge / Extrude (53604-53608) | `sld_newDraftFace` PARAM 0 / 1 / 2 / 3 -> `sld_draftface` -> `ps_draft_faces` | PK_FACE_install_surfs_isocline | BRepOffsetAPI_DraftAngle | angle sign convention NOT DECODED (also open in extrude.md 4.3) |
| S15 | Solids > Modify | Trim by Plane / Trim to Surface/Sheet (53609-53611) | `sld_newTrimByPlane`, `sld_newTrimToSurfSheet` -> `sld_trim` -> `ps_trim_to_sheet` / `ps_trim_to_surf` | PK_BODY_section_with_sheet_2, PK_BODY_section_with_surf | BRepAlgoAPI_Splitter / BRepPrimAPI_MakeHalfSpace + Cut | light |
| S16 | Solids > Create | Rectangular / Circular / Manual Pattern (53590-53592) | `sld_do_pattern` PARAM 0/1/2 -> `sld_pattern` | PK_FACE_instance_tools, PK_TRANSF_create_translation / _rotation, PK_TOPOL_clash | copies + BRepAlgoAPI_Fuse/Cut (no feature-instancing API) | semantics differ (sec. S16) |
| S17 | Solids > Modify | Face to Face Fillet (53596), Variable Fillet (53597) | `sld_do_face_face_fillet` -> `ps_face_face_fillet`; `sld_do_variable_fillet` -> `sld_var_fillet` -> `ps_var_rad_fillet` | PK_FACE_make_blend; KI BLECVR + PK_BODY_fix_blends | BRepFilletAPI_MakeFillet (SetRadius(law)) / ChFi3d | light |
| S18 | Solids > Create | Impression (53589), From Surfaces (53593); Drawing > Layout (53612) | `SldImpression`, `sld_do_stitching`, `do_solid_layout` | NOT TRACED (imports PK_BODY_sew_bodies, PSB_BODY_heal_and_sew) | BRepOffsetAPI_Sewing + ShapeFix / HLRBRep for Layout | low value for the shop |
| M1 | Model Prep (all 20 buttons) | sec. M | MCGEOMSLD `sldPushPull*`, `sldSplitFace`, `sld_modify_*`, `sld_remove_*`, `sld_simplify`, `SldOptimize`, ... | NOT TRACED (candidates from the import list, sec. M) | BRepAlgoAPI_Defeaturing, ShapeUpgrade_UnifySameDomain, BRepFeat_SplitShape, ... | direct editing has no OCCT equal |
| F1 | Surfaces (all 37 buttons) | sec. F | MCCAD / MCCADUTIL native surface code | **none** (mccad has no PSKERNEL import) | GeomFill / BRepOffsetAPI_* / BRepFill_Filling | results are Mastercam SURF_ID entities, not Parasolid (Q6) |

## Questions for the user (decisions needed; nothing below is guessed)

1. **Where do OCCT-made solids live?** Mastercam stores a solid as SOLIDXMT (XT bytes) + its ops history
   (formats/mcam.md line 26, 245; formats/mcam_write.md row 13 writes the loaded XT back unchanged). A solid built
   with OCCT can only go into a .mcam if we write XT (an OCCT -> XT writer, the reverse of the translator being built)
   and SldXxxOp history records. Options: (a) keep OCCT solids in our .mcamui project only, (b) write XT bricks (no
   history), (c) write XT + history ops (needs every op's serialisation decoded). Which one?
2. **Solid history.** Mastercam regenerates history solids from their op list, and transforming one transforms every
   op's parameters and regenerates (`sld_xform_solid`, sec. T). Do you want parametric history (edit / regen of Revolve,
   Fillet, ...) or bodies only ("brick" solids, Mastercam's own term: `mc_is_entity_a_brick`, `sld_make_brick_copy`)?
3. **Extrude kernel.** Extrude runs on our own solid25.c (formats/extrude.md). Move it to OCCT (so bodies can be filleted
   / booleaned with the new commands; exact output of existing projects may change) or keep solid25.c?
4. **Version-dependent Parasolid options.** Fillet (FUN_180036580) and Boolean (FUN_18003e630) choose PK option tokens by
   the op's version number (thresholds 0x12, 0x13, 0x16, 0x18, 0x1c; and 0xe..0x15 for blends). OCCT has one behaviour.
   Accept that, or do you need old parts' solids to regenerate identically (then they must be kept as stored XT)?
5. **Tolerance.** Parasolid calls use 1e-5 m (0x3ee4f8b588e368f1) as the boolean / blend tolerance; Mastercam's own
   `tol` global gates inputs. OCCT's default is Precision::Confusion() = 1e-7 in model units. Which value should our
   boolean fuzzy value / healing use (it is an "ours" choice either way)?
6. **Surfaces tab.** Mastercam surfaces are native NURBS / loft / parametric entities (SURF_ID 0x100 kinds 0/1/4/10...,
   formats/mcam.md line 46, 85), built by mccad itself, not Parasolid. Should we build them with OCCT and store them as
   SURF_ID kind 0 NURBS entities (layouts in mcam.md are "unverified against data"), or leave the Surfaces tab greyed for
   now? Is the tab needed at all for 2.5D/3D router work beyond Flat Boundary / From Solid?
7. **Model Prep.** Any of these needed soon (Push/Pull, Remove Faces / Fillets, Hole Axis)? They are the least
   portable to OCCT.
8. **Non-uniform (XYZ) scale of solids**: OCCT turns analytic faces into B-splines (BRepBuilderAPI_GTransform). OK?

## T. Transform tab

### T.0 What is shared
- Every do_xform_* entry (`xf.c`) uses the mccore Xform machinery: `ActivateXform`, `select_ents`, `XfCreatePreview`,
  `XfDeletePreview`, `XfDoFinalAction`, `UndoXformTransCommit`, `xform_del_duplicate`, `XfFit`, `DfXformCheck`
  (call lists from `xf.c`; `do_xform_movetoorigin` instead calls `translate_silent` + `UndoXformTransStart`). Dialog
  templates (exports/mccadutilres_dialogs.tsv): Translate 6433/6434, Translate 3D 6435/6437, Mirror 6439/6440, Rotate
  6441/6443, Scale 6445/6447/6449/6451 (+ Scale Calculator 6452), Rectangular Array 6457/6458, Roll 6467/6468, Stretch
  6558/6559, Distribute 25527/25528. Common controls on these templates: Move / Copy / Join radios (100 "Delete original
  geometry", 101 "Keep original geometry", 102 "Keep original geometry and Join"), copies edit 1375, Preview (Regen
  2973, Fit 1778), Attributes (1665 "Use New Attributes", Level 1683, "Increment level per copy" 1688, Color 1714), Add /
  Remove Geometry 1250, OK / Apply / Help.
- Parameters, defaults and dialog mapping: **being decoded by the parallel Transform task** (its brief: Translate =
  `CXfTransParams` ctor FUN_18003ac00, file "XFORM.PRM", +0x20 MoveCopyJoin, +0x24 Copies = 1, +0x28 ByTotal = 0, +0x30
  Delta, +0x48 Direction, +0x4c Stretch, +0x4d UseAttrs, +0x4e IncrLevels, +0x50 Level = -1, +0x54 Color = -1; the
  other commands still open there). Not duplicated here.
- Wireframe results need **no OCCT**: points / lines / arcs / splines are transformed by the matrix (mccore
  `xform_ent`). OCCT is only needed for solids.

### T.1 Solids under Transform (mccore `xform_store_ent` 0x1808da2e0, tree_transform/decomp/mccore_xform.c ~l.1070-1150)
- Entity class 0x10000 (solid). One branch calls `sld_xform_solid(ent, DAT_18143d5d0 /*xform_type*/, &matrix)`
  directly; the other (when the mode global DAT_18143d604 == 1, [inferred] the Copy mode) tests `mc_is_entity_a_brick`:
  a history solid asks `message_box_yes_no` mccoreres 5318 "The Copy of the Solid will have no History. Continue?" or
  5317 "Solid History will be lost. Continue?" (chosen by DAT_18143d6a8; caption mccoreres 5261) and, on Yes, makes
  `sld_make_brick_copy` and transforms that. Which xform types / modes take which branch: NOT DECODED here.
- `sld_xform_solid` (mcgeomsld 0x1800d04c0, `sld_core.c`): only for a solid with an op file (ent+0xb0) and flag
  ent+0x70 bit 4 clear. It copies the solid (`sld_copy_solid`), then **transforms every history op's parameters**
  (`FUN_1800d0240`: per op the vtable slot 0x100 or 0x108 = the op's own transform method, recursing into referenced
  entities), then **regenerates** the solid (`sld_regen_solid`), restores name / colour / level (main colour / level
  when not keeping attributes) and re-adds it to the Solids manager (`HistDlg_removeSolid`, `mc_readd_solid_to_dialog`).
  `sld_xform_changing_scale` toggles units (`SetUnits`) around the copy. The Parasolid body itself is never moved by a
  PK_BODY_transform here.
- Brick solids: mcgeomsld imports PK_BODY_transform / PK_BODY_transform_2 / PK_TRANSF_create* and has sld 550 "Failed to
  create PS transform." / 551 "Failed to apply transform to body."; the call site for the brick path is NOT TRACED.
- OCCT: brick -> `BRepBuilderAPI_Transform(shape, gp_Trsf, copy)`; history -> transform each feature's stored
  parameters (chains, vectors, planes) and rebuild (needs Q2). Mirror flips orientation: BRepBuilderAPI_Transform keeps
  a valid solid (it reverses as needed) - check that our face-normal / tessellation code follows `TopoDS_Shape::Location`.
- **T5 Scale XYZ** (templates 6449/6451 with "Uniform" / "XYZ" radios 718 / 720): non-uniform scale of a brick needs
  BRepBuilderAPI_GTransform, which converts planes / cylinders to B-spline surfaces; Mastercam's history path scales
  the op parameters instead (results stay analytic). Q8.
- To decode for solids: the `xform_type` enum (DAT_18143d5d0), DAT_18143d604 / DAT_18143d6a8 meanings, the brick
  transform call, and the op vtable slots 0x100 / 0x108 for each SldXxxOp (needed only with history, Q2).

## S1-S5. Simple solids (Block, Cylinder, Cone, Sphere, Torus)

### Entry and panel
- FT: `create_<shape>_solid` / `create_<shape>_surface` are not DLL exports (formats/ui_commands.tsv "NO"); mastercam.ft
  maps them to mccad `create_block` / `create_cylinder` / `create_cone` / `create_sphere` / `create_torus` with PARAM 1
  (solid) / 0 (surface). Tooltips: mccadres 5994 / 5962 / 5993 / 5995 / 5996 ("Create solid block" ...), surface
  variants 5916 / 5903 / 5912 / 5920 / 5923 (formats/mccadres_strings.tsv).
- `create_block` (0x180013ab0) = `create_sz_block(PARAM, 0, 0, 0, 0, 1)`; `create_sz_block` (0x180013ae0, `cad_prim.c`):
  copies the static block DAT_180648370 (DAT_1806919e0 when `enable_dialog_modality`), sets the three axis vectors
  from the active Cplane (`view_to_world` of GetActiveViewNo(Planes,0)), byte 0 = 1 for PARAM 0 (surfaces) / 0 for
  PARAM 1 (solids), forced to surfaces when `sld_solids_ok(0)` fails, then runs `FUN_180011b50` (the panel loop, not
  decoded) and `update_graphics`. Cylinder / sphere follow the same pattern (`create_sz_cylinder` DAT_180648980,
  `create_sz_sphere` DAT_180648030); torus / cone are full functions (`create_torus` DAT_180648d70, `create_cone`
  DAT_180649140).
- Static images of those blocks (`prim_mem.txt`): first int = 1, all sizes 0.0, a double 360.0 (0x4076800000000000) in
  each round shape (the sweep end angle [inferred]). The size defaults are therefore set at run time: **NOT DECODED**.
- MFC panels (mccadres.dll dialogs, `mccadres_dialogs.tsv`), each in a short and a long variant: Cylinder 6415 / 6416,
  Cone 6417 / 6418, Block 6420 / 6419, Sphere 6422 / 6421, Torus 6424 / 6423. Controls: radio 343 "&Solids" / 585
  "S&urfaces"; 544 "Base point"; Block: 755 "&Length" + combo 759 + 611 "Length", 757 "&Width" + 761 + 570, 744
  "H&eight" + 750 + 574, 575 "Flip direction", 411 combo, "Anchor" group (9 buttons 1308..1366, static 613 "Anchor
  position"); Cylinder: 738 "&Radius" + 740 + 25368, 744 / 750 / 574 height, 575; Cone: Base (radius 738/740, height
  744/750), Top (1533 / 1528 radio pair, combos 608 / 606, 598 "Top radius"); Torus: 738 radius, 746 "&Minor radius" +
  751 + 617; long variants add "Sweep" (edits 595, 435) and "Axis" (25074 X / 25075 Y / 25076 Z, 1518, 25069, 579
  "Line", 582 "Two points"); OK 1 / Apply 12321 / Help 9. Which variant is shown when, the meaning of the unlabeled
  radio pairs and combo 411, and the combo-box (value + history?) behaviour: NOT DECODED.

### Geometry (mcgeomsld `sld_basic_primitive` 0x1800ac350, formats/bbox_toolplane.md C; `sld_core.c`, `sld_help.c`)
SLD_PRIMITIVE_PARAMS (0x90 bytes): +0 type, +8 origin, +0x20 axis, +0x38 reference / X vector, +0x50 3rd vector, then
sizes / angles; the per-shape builders get `p+8`.

| shape | builder | checks (message, caption sld 201) | Parasolid | OCCT |
|---|---|---|---|---|
| Block 0xf | ps_create_block 0x1800402a0 | sizes >= tol, vectors >= tol, A/B not parallel; sld 323 "Invalid geometry, failed to create to block primitive." | PK_BODY_create_solid_block(s1,s2,s3, basis) then rotations / reflection (bbox_toolplane.md C) | BRepPrimAPI_MakeBox(gp_Ax2(origin, A, B), s1, s2, s3); Parasolid's placement of the block relative to its basis is NOT DECODED (bbox_toolplane.md) - verify against an XT block from a real part before trusting the corner |
| Cylinder 0x11 | ps_create_cylinder 0x180041280 | r, h >= tol, axis >= tol, angles in [0,360]; sld 327 | full: KI CRCYSO(origin, axis, r, h); partial (start >= tol or end <= 360 - tol): FUN_180033e60 | BRepPrimAPI_MakeCylinder(ax2, r, h) / (.., angle); start angle -> rotate about axis (gp_Trsf) |
| Cone 0x10 | ps_create_cone 0x1800410b0 | p+0x60 >= tol, (p+0x60 > tol or p+0x68 > tol), p+0x70 >= tol, axis >= tol, angles p+0x78 / p+0x80 in [0,360]; sld 325 (field meaning NOT DECODED; [inferred] base radius, top radius, height) | full: FUN_180032e30 -> KI CRCOSO; partial: FUN_1800331f0 (PK_BODY_create_sheet_planar ...) | BRepPrimAPI_MakeCone(ax2, r1, r2, h[, angle]) - OCCT rejects r1 == r2 (use a cylinder) |
| Sphere 0x12 | ps_create_sphere 0x180041570 | p+0x18 > tol else sld 297; angles p+0x68 / p+0x70 | full: FUN_180034510 PK_BODY_create_solid_sphere; partial: FUN_180034720 (PK_CIRCLE_create, PK_POINT_make_minimum_body, PK_BODY_imprint_curve, PK_FACE_attach_surf_fitting, CRTRCU) | BRepPrimAPI_MakeSphere(ax2, r[, angle]) |
| Torus 0x13 | ps_create_torus 0x1800416b0 | p+0x60 >= tol, p+0x68 >= tol, **p+0x68 <= p+0x60** (no spindle torus), axis >= tol; sld 329 | full: FUN_180034f90 PK_BODY_create_solid_torus; partial (angles p+0x70 / p+0x78): FUN_180035190 (PK_BODY_create_sheet_circle + spin) | BRepPrimAPI_MakeTorus(ax2, R, r[, angle]) |

History: SldPrimitiveOp (formats/bbox_toolplane.md C). Surfaces variant (PARAM 0): what is built (Mastercam surface
entities, not Parasolid) is NOT DECODED.
**Porting status:** OCCT builders straightforward; blockers = panel loop FUN_180011b50 (+ the per-shape loops
FUN_18002dc80 / FUN_180032a00 seen in `cad_prim.c`), runtime defaults, Q1 / Q2.

## S6. Revolve

- Ribbon Solids > Create > Revolve (53585), FT `sld_newRevolve` (MCGEOMSLD export), tooltip 5774 (ui_commands.tsv).
- `sld_newRevolve` 0x1800eaf70 -> `sld_solids_ok`, app ctor FUN_18014e490 (+ FUN_18014e7f0), FUN_180165170 (run).
  `sld_new_revolve` 0x1800e8ec0 / `sld_revolve_from_chain` 0x1800eae40 are the programmatic entries; the op is
  `SldRevolveOp`.
- App ctor FUN_18014e490 (`sld_apps.c` l.45-140): name sld 711 "Revolve" (0x2c7); P (app+0x230..) copied from the
  session globals DAT_1803e8f60..0x1803e8fb7; `RegisterVariable(&DAT_1803e8fa0, 1.0, 0.1)` and `(&DAT_1803e8fa8, 1.0,
  0.1)` (metric 1.0 / inch 0.1; argument order per formats/wireframe.md) copied to app+0x270 / +0x278 = the thin-wall
  thicknesses [inferred from the extrude pattern: same values as extrude's Direction 1 / 2]; type DAT_1803e8f60 forced
  0 when `sld_get_num_solids_visible() < 1`; DAT_1803e8f70 / f78 zeroed at every start; app+0x2b0 = DAT_1803bab71
  (initial 1, `sld_mem.txt`; which control it backs NOT DECODED). The image of DAT_1803e8f60.. is all zero: start / end
  angle defaults are set elsewhere, **NOT DECODED**. Other default names: sld 712 "Revolve Boss", 713 "Revolve Cut"
  (string table; selection code NOT TRACED). Panel title sld 853 "Solid Revolve", prompt sld 854 "Use dialog to select
  chain(s), select axis of rotation, and to modify revolve settings", chain prompt sld 473 "Select chain(s) to
  revolved", axis prompt sld 917 "Select axis of rotation (edge, face, line, arc, or spline)" (strings; call sites NOT
  TRACED).
- Panel (BAML solids/revolve/solidsrevolvemanagerview.baml, `baml/solidsrevolvemanagerview.txt`): tabs Basic /
  Advanced. Operation: "_Name:", "Type:" radios "_Create body" / "C_ut body" / "Add _boss" (OperationType; Cut / Boss
  enabled by EnableCutBoss), "_Target:" + Select Target Body, "Create a single _operation". Chains: list + Reverse Axis
  / Add Chains / Rechain All. "A_xis of Rotation": text + Select Axis. Angle: "_Start angle:" (StartAngle), "_End
  angle:" (EndAngle). Advanced: "Thin _Wall" checkbox expander with Direction 1 / 2 / Both and "Direction _1:" /
  "Direction _2:" (ThinWallInwardDistance / OutwardDistance), Preview ("_Automatically preview results", "_Preview").
  Validator strings present: sld 2057 "Revolve requires at least one chain", 2058 / 2059 "Direction 1 / 2 must be
  greater than 0", 2060 "Axis of rotation required for revolve", 2000 "Angle must be between -360 and 360 degrees"
  (validator code NOT TRACED).
- Geometry (`sld_revolve` 0x1800e8fc0): closed chains -> sheets (`ps_sort_chains_for_sheets`,
  `ps_chains_to_sheet_bodies`) and `ps_swing_sheet`; thin wall -> wires (`ps_sort_chains_for_wire`,
  `ps_chains_to_wire_bodies`) and `ps_swing_wire`; then Create (`sld_solid_from_tag`) or Boss / Cut with
  `ps_boolean_union` / `ps_boolean_cut`. Messages: sld 682 "Chains must be closed to revolve into a solid body." (0x2aa),
  sld 657 "Operation requires planar chain set." (0x291).
- Spin (`ps_swing_sheet` 0x1800549d0 -> FUN_180039730, `sld_help.c`): |end - start| <= tol -> sld 310 "Invalid swing
  angle."; |axis| <= tol -> sld 311 "Invalid swing axis."; the angle is normalised by +-2 pi (a flag reverses it:
  end - start or start - end), a full turn is detected within tol; `PK_BODY_spin(body, axis, angle, 1)`; then, when
  |start| > tol, the result is rotated by the start angle (`PK_TRANSF_create_rotation`, sign flipped by the reverse
  flag) and `PK_BODY_transform_2`; redundant topology removed (`PK_TOPOL_delete_redundant`).
- OCCT: `BRepPrimAPI_MakeRevol(face, gp_Ax1, angle)` (full turn: the 2-argument form, which closes the seam), then
  `BRepBuilderAPI_Transform(gp_Trsf::SetRotation(ax1, start))`, `ShapeUpgrade_UnifySameDomain` for the redundant
  topology, `BRepAlgoAPI_Fuse` / `Cut` for Boss / Cut. Thin wall: revolve the wire to a shell, then thicken (as extrude
  4.6, `PK_BODY_thicken_3` <-> `BRepOffsetAPI_MakeThickSolid::MakeThickSolidBySimple`).
- Differences: a profile touching the axis (degenerate edge) is accepted by PK_BODY_spin; MakeRevol gives a degenerated
  edge that can upset later fillets - test. Which side "Reverse Axis" flips and how a picked edge / arc / spline becomes
  the axis: NOT DECODED.
- **To decode before porting:** the app's panel <-> P mapping (like extrude's FUN_1801308a0 / FUN_180132310), the
  axis-pick code, the angle defaults, the Cut / Boss target rules (probably the extrude ones, not assumed).

## S7. Boolean

- Ribbon Solids > Create > Boolean (53588), FT `sld_do_boolean` 0x18010e5c0 -> app FUN_1801a1980 (name 0x3a2 = sld 930
  "Boolean Add"; others sld 931 "Boolean Remove", 932 "Boolean Common") + FUN_1801a1b80; op `SldBooleanOp`;
  programmatic `sld_new_boolean` 0x18010e480; also exported `sld_do_union_from_eptrs`, `sld_do_subtraction_from_eptrs`.
- Panel (BAML solids/boolean/solidsbooleanmanagerview.baml): Operation: "_Name:" (enabled by NonAssociative), "Type:"
  radios "_Add" / "_Remove" / "_Common" (OperationType), "_Target:" + Select Target Body (enabled by ModifyParams). "Tool
  _Bodies" list + Add Selection / Reselect All. "N_on-associative" checkbox expander: "_Keep original target solid",
  "Kee_p original tool solid". Advanced: Preview. Prompts sld 442 "Select target body for boolean operation", 454 "Select
  tool body(s) for boolean operation"; sld 727 "Create non-associative boolean?"; validator string sld 2037 "Boolean
  requires at least one tool body". Defaults: NOT DECODED.
- Kernel (`sld_boolean` 0x18010d590 -> `ps_boolean_union` / `_cut` / `_isect` -> FUN_18003e630 with PK op tokens 0x3e1f /
  0x3e1e / 0x3e1d): bodies -> `PK_BODY_boolean_2(target, n, tools, &opts, ...)`; face sets -> `PK_FACE_boolean_2`. Options:
  struct version 0x11, tolerance 1e-5 (twice), a version-dependent token 0x616c..0x6174 chosen by the op version
  (< 0x12, < 0x13, < 0x16, < 0x18, < 0x1c, else 0x616d), other tokens 0x4724, 0x58fc, 0x5930, 0x598a, 0x5a78, 0x5bae,
  0x5bf4, 0x5f17, 0x5528 (names NOT DECODED). Errors: add / remove / common sld 539 / 553 / 554, "would result in a
  disjoint body" sld 668 / 671 / 672 (checked from the boolean report, PK_boolean_r_f), general sld 559, sld 658
  "Boolean operation would destroy target" (in sld_boolean). Highlighting of the failing edges on error
  (`ps_hilight_edges`).
- OCCT: `BRepAlgoAPI_Fuse` / `Cut` / `Common` with arguments = target, tools = tool bodies; `SetFuzzyValue` (Q5),
  `SetNonDestructive(true)` for "Keep original", `ShapeUpgrade_UnifySameDomain` afterwards. Disjoint result: OCCT
  returns a compound of several solids without error - the 668 / 671 / 672 checks must be done by us (count
  `TopAbs_SOLID` in the result) once their exact rule (error or warning, which ops) is decoded from FUN_18003e630's
  report handling (`local_160 == 0x5495`, `*local_148 < 0x5461`).

## S8. Constant Fillet

- Ribbon Solids > Modify > Constant Fillet (53594, split button 53595; Face to Face / Variable are S17). FT
  `sld_do_fillet` 0x1800e3670 -> app ctor FUN_1801bc100 (name sld 823 "Constant Radius Fillet" 0x337) + FUN_1801bc2d0;
  `sld_new_const_fillet` 0x1800e2a10 programmatic; op `SldFilletOp`.
- Defaults (FUN_1801bc100, `sld_apps.c` l.290-330): P = app+0x228 (SLD_FILLET_PARAMS) copied from DAT_1803e9660..0x1803e9677;
  **radius P+0x08 = RegisterVariable(&DAT_1803e9668, 1.0, 0.1)** (metric 1.0 / inch 0.1). The image of DAT_1803e9660 /
  DAT_1803e9670 is zero (`sld_mem.txt`); the other fields' defaults: NOT DECODED.
- Panel (BAML solids/fillet/solidsfilletmanagerview.baml): Operation: "_Name:", "_Propagate along tangencies"
  (PropagateAlongTangencies), "_Mitered corners" (MiteredCorners), "_Overflow:" combo items "Default" / "Maintain blend"
  / "Maintain edges" (Overflow = index). Selection list + Add Selection / Reselect All. "Radi_us" (Radius). Advanced:
  Preview. Prompts sld 438 "Select entities to fillet", 504 "Use dialog to modify fillet settings"; validator strings
  2029 "Constant radius fillet requires at least one solid edge, face, or body", 2030 "Radius must be greater than 0".
- Kernel: `sld_fillet` 0x1800e2540 -> `ps_fillet(body, ..., radius P+0x08, P+0x10 int, P+0x14 byte, P+0x15 byte, n,
  edges, version)` (call at `sld_core.c` l.868). `ps_fillet` 0x180046d30: with P+0x14 == 0 all edges in one call,
  otherwise one call per edge; FUN_180036310 drops smooth (tangent) edges (`PK_EDGE_is_smooth`), then
  `PK_EDGE_set_blend_constant(n, edges, radius_m, &opts)`: opts version 2, a token 0x47ea or **0x47eb when P+0x15 != 0**,
  0x47f4, 0x47fe, byte 1, tokens 0x4809/0x4813/0x481c/0x4827 replaced by **P+0x10**: 1 -> 0x4812, 2 -> 0x4808 / 0x4814 /
  0x481d / 0x4826, other -> 0x480a; tolerance 1e-5; 0x56b9. Then FUN_180036580 -> `PK_BODY_fix_blends` with a version
  table (op version thresholds 0xe..0x1c -> tokens 0x56b6, 0x5d02..0x5d0f, else 0x56b0). Errors sld 566 "Failure to
  attached blends to body.", 538 "Blend creation failed.", 665 "Probable cause: Blend overflow removes adjacent face or
  edge.", 291 "Fillet input edge(s)/face(s) not found." (strings; exact call sites partly traced).
- Panel <-> P mapping is NOT DECODED; [inferred]: P+0x10 = Overflow index (3 values match the 3 branches), P+0x15 =
  Propagate along tangencies, P+0x14 = Mitered corners. Must be confirmed from the dialog's panel -> params function.
- OCCT: `BRepFilletAPI_MakeFillet(shape, ChFi3d_Rational)`, `Add(r, edge)` per edge; propagation along tangent edges
  is automatic in OCCT (contours are built over G1 chains) - "Propagate off" has no direct equal; selecting a face or
  body = all its edges. Overflow: OCCT has no "maintain blend / maintain edges" option. Mitered corners: no option
  (OCCT always builds its own vertex blends). Expect different corner patches and failures where Parasolid succeeds
  (cliff edges, blend running off a face). `IsDone()` false -> report sld 538.

## S9. Chamfers (One Distance / Two Distance / Distance and Angle)

- FT `sldChamferOneDist` / `TwoDist` / `DistAngle` (not exports) = mastercam.ft `CALL_FUNC "sldChamfer"` PARAM 0 / 1 / 2.
  `sldChamfer` 0x18010ab80 sets DAT_1803b9720 = PARAM and runs app FUN_1801b9b50 (`SolidsChamferApp`). Names by type:
  sld 912 "One Distance Chamfer", 913 "Two Distance Chamfer", 914 "Distance and Angle Chamfer" (0x390-0x392).
- Defaults (FUN_1801b9b50, `sld_apps.c` l.455-530): P = app+0x228 (SLD_CHAMFER_PARAMS); P+0 = type (DAT_1803b9720);
  **Distance 1 P+0x08 = RegisterVariable(&DAT_1803e9640, 2.0, 0.1)**, **Distance 2 P+0x10 = RegisterVariable(&DAT_1803e9648,
  2.0, 0.1)** (metric 2.0 / inch 0.1); for type 2 P+0x10 = DAT_1803bac28 = **45.0** (the angle; `sld_mem.txt`). P+0x18
  byte (per-edge mode in ps_chamfer, as P+0x14 in fillet) default NOT DECODED.
- Panels (BAML solids/chamfer/*.baml): all: "_Name:", "_Propagate along tangencies", "_Mitered corners", "_Selection"
  list + Add Selection / Reselect All; One: "_Distance" (Distance); Two: + "Switch All Reference Faces", "Distance _1:" /
  "Distance _2:"; Dist-Angle: + "Switch All Reference Faces", "_Distance:" / "_Angle:". Validator strings sld
  2038-2045 (2045 "Angle must be greater than 0 and less than 90 degrees"); prompts sld 449, 506; errors sld 299, 317
  "Angle between faces varies, cannot chamfer", 321 "Chamfer ill-defined", 344.
- Kernel: `sld_chamfer` 0x18010a260 -> `ps_chamfer` 0x18003ff90 -> FUN_180032890: per edge skip smooth edges, find the
  reference face side (`PK_EDGE_ask_faces`, `PK_FACE_ask_oriented_surf`, `PK_SURF_eval_with_normal`,
  `PK_SURF_parameterise_vector`), `PK_EDGE_set_blend_chamfer`; then FUN_180036580 (fix blends, as fillet).
- OCCT: `BRepFilletAPI_MakeChamfer`: `Add(d, edge)`; `Add(d1, d2, edge, face)`; `AddDA(d, angle, edge, face)`. The
  reference face per edge and how "Switch reference face" flips it must be decoded from FUN_180032890 / the selection
  list (the per-edge byte array `param_6` of ps_chamfer). OCCT's angle is measured on the given face; Parasolid's
  convention for the distance-angle pair is NOT DECODED -> verify with a test part.

## S10-S18. Remaining Solids commands (light: location, kernel, NOT DECODED list)

All panels: `baml/<view>.txt`. Kernel call lists: `pk.py` over `sld_light.c` (one level of callees).

| cmd | panel fields (BAML) | kernel | OCCT | differences / to decode |
|---|---|---|---|---|
| S10 Loft (`sld_newLoft`; `sld_loft`, `sld_loft_from_chains`, `ps_analyse_loft_chains`, `ps_loft_chains`; op SldLoftOp) | Name, Type Create / Cut / Boss, Target, "C_reate as ruled", Chains, Preview; sld 2050 "Loft requires at least two chains" | PK_BCURVE_make_matched, PK_BCURVE_combine, PK_CURVE_make_bcurve, PK_BODY_sew_bodies, PK_BODY_unite_bodies (imports also PK_BCURVE_make_bsurf_lofted) | BRepOffsetAPI_ThruSections(isSolid, ruled) | Mastercam matches the section curves itself (make_matched) then lofts a B-surface: ThruSections' own compatibility / parametrisation (start points, directions) will twist differently. Decode chain start / direction matching (ps_analyse_loft_chains) |
| S11 Sweep (`sld_newSweepV8`; `sld_sweep_v8`, `sld_sweep_from_chains`, `ps_analyse_sweep_chains`; op SldSweepOpV8) | Name, Type, Target, single op, "Chains to Sweep", "_Along Chain"; sld 2032 | PK_BODY_sweep, PK_BODY_spin, PK_CIRCLE_ask, PK_TRCURVE_ask, PK_FACE_make_sheet_body (no PK_BODY_make_swept_body import) | BRepOffsetAPI_MakePipeShell (SetMode: Frenet / CorrectedFrenet / fixed) | [inferred] the path is processed per segment (lines swept, arcs spun); profile orientation law NOT DECODED |
| S12 Shell (`sld_do_shell`; `sld_shell`, `ps_shell_body`; SldShellOp; `sld_reshell`) | Name, Direction 1 / 2 / Both, Selection (faces to open), Direction 1 / 2 thickness; sld 2061-2063 | PK_BODY_hollow_2 | BRepOffsetAPI_MakeThickSolid::MakeThickSolidByJoin(faces, -t, tol, BRepOffset_Skin, intersection, GeomAbs_Arc / Intersection) | Both directions = offset in and out: OCCT needs two runs or MakeOffsetShape; self-intersection handling differs; uniform thickness only on both sides (no per-face thickness in the panel) |
| S13 Thicken (`sld_do_thicken`; `sld_thicken`, `ps_thicken_sheet`, `ps_thicken_sheet_op`; SldThickenOp) | Name, Direction 1 / 2 / Both, Target, Direction 1 / 2 thickness | PK_BODY_thicken_3 | BRepOffsetAPI_MakeThickSolid::MakeThickSolidBySimple / MakeOffsetShape + sewing | side convention vs sheet normal NOT DECODED (extrude.md 4.6 has the extrude case) |
| S14 Draft x4 (`sld_newDraftFace` PARAM 0 Faces / 1 Plane / 2 Edge / 3 Extrude; `sld_draftface`, `ps_draft_faces`, `ps_draft_extrude_faces`; SldDraftFaceOp; `sld_redraft`) | Faces: Name, Propagate, Faces to Draft, Reference Face, Angle; Plane: "Make associative to plane", Target Bodies, Plane, Angle; Edge / Extrude: Faces to Draft, Angle; sld 2006-2013 (0..90 / -90..90) | PK_FACE_install_surfs_isocline (+ PK_PLANE_create for the plane case) | BRepOffsetAPI_DraftAngle::Add(face, dir, angle, neutralPlane) | isocline (Parasolid) vs neutral plane + direction (OCCT): sign of the angle and which side is fixed must be decoded (same open item as extrude.md 4.3); OCCT fails on faces adjacent to non-drafted fillets more often |
| S15 Trim by Plane / to Surface/Sheet (`sld_newTrimByPlane`, `sld_newTrimToSurfSheet`; `sld_trim`, `ps_trim_to_sheet`, `ps_trim_to_surf`; SldTrimOp) | Name, "_Split solid", "Make associative to plane" / "Use _trimmed surface" / "Use _parent surface", Target Bodies, Plane or Surface/Sheet; sld 2064-2066 | PK_BODY_section_with_sheet_2, PK_BODY_section_with_surf, PK_PLANE_create, PK_SURF_make_sheet_body, PK_BODY_reverse_orientation | BRepAlgoAPI_Splitter (split) / BRepPrimAPI_MakeHalfSpace + BRepAlgoAPI_Cut (trim); untrimmed parent surface -> BRepBuilderAPI_MakeFace(surface) | which side is kept (flip arrow) NOT DECODED; trimming by a Mastercam surface needs the SURF_ID -> Geom_Surface conversion (Q6) |
| S16 Patterns (`sld_do_pattern` PARAM 0 rect / 1 circ / 2 manual; `sld_pattern`; SldPatternOp) | Rect: Name, "Solid bod_y boss" / "Solid body c_ut", Target, Selection, Results, Direction 1 / 2 (Instances, Distance, Angle, Reverse, Both, "Relative to direction _1"); Circ: Instances, Center point, Full circle / Arc, Angle, "Limit to total sweep angle", Reverse; Manual: Selection, Results; sld 2002-2005, 2051-2055 | PK_FACE_instance_tools (+ PK_FACE_inst_tools_r_f), PK_TRANSF_create_translation / _rotation, PK_TOPOL_clash, PK_TOPOL_find_box | per instance BRepBuilderAPI_Transform of the tool body (or of the feature's faces made into a solid) + BRepAlgoAPI_Fuse / Cut | Parasolid instances *faces of a feature* into the body; OCCT has no feature instancing - we must rebuild a tool solid per feature (needs history, Q2). Instances falling off the body / clashing: Parasolid reports (clash), OCCT just fuses |
| S17 Face-Face Fillet (`sld_do_face_face_fillet`; `sld_face_face_fillet`, `ps_face_face_fillet`) | Name, Type Radius / Width / "H_old line", Selection 1 / 2, Radius, Width + Ratio, Hold Line (Edges, Single / Double sided, Propagate, Continuous curvature, Help point); sld 2021-2026 | PK_FACE_make_blend, PK_blend_rib_r_f | no direct API (BRepFilletAPI works on edges); ChFi3d / BRepFill_Filling by hand | major gap - defer |
| S17 Variable Fillet (`sld_do_variable_fillet`; `sld_var_fillet`, `ps_var_rad_fillet`) | Name, Propagate, Flow Linear / Smooth, Overflow (3 items), Selection, Vertices, Radius Default + "Set _all" / "_Cycle"; sld 2027, 2028, prompts 852 / 855 | KI BLECVR + PK_BODY_fix_blends (no PK_EDGE_set_blend_variable import) | BRepFilletAPI_MakeFillet::Add(r1, r2, edge) / SetRadius(TColgp_Array1OfPnt2d law) | linear vs smooth law: OCCT law functions are not Parasolid's - shape between markers will differ |
| S18 Impression, From Surfaces (stitch), Layout | Impression: (no BAML found in mcgeomsld.ui); From Surfaces: "Original surface:" Keep / Blank / Delete, "_Tolerance:" (EdgeTolerance), "_Create curves on open edges", curve color, sld 2046 / 2047; also MFC dialog 137 "Stitch Surfaces into Solid(s)" (exports/mcgeomsldres_dialogs.tsv) and globals `sld_stitch_tol`, `sld_stitch_*`; Layout: large drawing panel (solidslayoutmanagerview) | NOT TRACED (imports: PK_BODY_sew_bodies, PSB_BODY_heal_and_sew, PK_BODY_imprint_*) | BRepOffsetAPI_Sewing(tol) + ShapeFix_Shell / BOPAlgo_MakerVolume; Layout = HLRBRep_Algo | needs Mastercam surfaces -> OCCT faces (Q6) |

## M. Model Prep tab (all greyed; location + candidate kernel calls only)

FT entries (formats/ui_commands.tsv / ui_ribbon.json): Hole Axis `sld_do_hole_axis` (53613), Push/Pull
`sldPushPullOffset` (53614), Move `sldPushPullMove` (53615), Split Solid Faces `sldSplitFace` (53616), Modify Feature
`sld_modify_feature` (53617), Modify Fillet `sld_modify_blend` (53618), Remove Faces `sld_do_face_removal` (53619), Remove
Fillets `sld_remove_blend` (53620), Remove History `sld_remove_history_task` (53621), Simplify Solid `sld_simplify`
(53622/53623), Optimize `SldOptimize` (53624), Repair Small Faces `SldRepairSmallFaces` (53625), Find Features
`sld_do_feature_detect` (53626), Disassemble `sld_do_solid_disassemble` (53627), Position `sld_do_solid_position`
(53628), Colors: Clear All `sld_clearAllColor` = `sld_pick_and_clear_all_color`, Change Face `sld_changeFaceColor` =
`SldChangeFaceColor`, Clear Feature `sld_clearOpColor` = `sld_pick_and_clear_operation_color`, Set Feature
`sld_setOpColor` = `sld_pick_and_set_operation_color` (mastercam.ft CALL_FUNC), all in MCGEOMSLD.DLL.

Panels: `baml/solidspushpullmanagerview.txt`, `solidsmovemanagerview.txt` (Move / Copy, Maintain fillet radius, Snap to
AutoCursor, Repair faces, Revert model, Grow: automatic / moving / non-moving / none, Step: None / All boundary edges /
If cannot extend or merge / Pierce), `solidssplitmanagerview.txt`, `solidsmodifysolidfeaturemanagerview.txt` (Create
body / Remove / Remove and create body), `solidsmodifysolidfilletsmanagerview.txt` (Radius), `solidsremovefacesmanagerview.txt`,
`solidsremovesolidfilletsmanagerview.txt` ("Cap ends of fillets being removed"), `solidsrepairsmallfacesmanagerview.txt`
(Small faces / edges / spikes / sliver / gashes tolerances, aspect ratio; sld 2067-2072), `solidsfindfeaturesmanagerview.txt`
(Create operations / single op / Remove features; Fillets, Holes, blind holes; Minimum / Maximum radius), `solidsholeaxismanagerview.txt`,
`solidsdisassemblemanagerview.txt`, `solidschangefacecolormanagerview.txt`. Simplify: no BAML (MFC; not located).

| command | candidate PK / PSB imports (call sites NOT TRACED) | OCCT candidate | note |
|---|---|---|---|
| Remove Faces | PK_FACE_delete_2, PK_FACE_delete_from_sheet_body | BRepAlgoAPI_Defeaturing | heal-over behaviour differs |
| Remove Fillets / Modify Fillet / Find Features | PK_FACE_identify_blends(_r_f), PK_FACE_delete_blends, PK_FACE_change, PK_EDGE_ask_blend | BRepAlgoAPI_Defeaturing; blend recognition is ours to write | OCCT has no blend identification |
| Push/Pull, Move | PK_FACE_change, PK_BODY_offset_2, PK_OFFSET_ask | BRepOffsetAPI_MakeOffsetShape (single face offset is limited), BRepFeat | Parasolid local operations (grow / step / pierce) have no OCCT equal |
| Split Solid Faces | PK_FACE_imprint_curve, PK_FACE_split_at_param, PK_BODY_imprint_* | BRepFeat_SplitShape, BRepAlgoAPI_Splitter | |
| Simplify / Optimize / Repair Small Faces | PK_BODY_simplify_geom, PK_FACE_simplify_geom, PK_TOPOL_delete_redundant(_2), PSB_* (heal, clean, simplify, remove_small_entity, optimise) | ShapeUpgrade_UnifySameDomain, ShapeFix_Shape, ShapeFix_FixSmallFace, ShapeUpgrade_RemoveInternalWires | results will differ |
| Hole Axis | exported DetectSolidHoleFeatures (exports/mcgeomsld.dll.tsv) | ours: cylinder-face analysis on TopoDS | creates wireframe only |
| Disassemble / Position | PK_TOPOL_find_box, PK_BODY_transform_2 | Bnd_Box + gp_Trsf | |
| Colors | exported SldSetFaceColor / SldGetFaceColor / SldClearFaceColor, mc_op_setOpColor (attributes) | per-face colour map (TDocStd / our own) | needs face identity across regen |
| Remove History | sld_remove_history_task | drop the feature list, keep the shape | trivial once Q2 is decided |

## F. Surfaces tab (37 buttons; light)

Mastercam builds surfaces in MCCAD.DLL / MCCADUTIL.DLL as its own entities (SURF_ID 0x100, kinds per
formats/mcam.md line 46 / 85); neither DLL imports PSKERNEL (objdump). WPF panels exist in mccad.ui for most
(`MCCAD.UI.Surface.*ManagerView`: Extend, Fence, FillHoles, FlatBoundary, FromSolids, Net, Offset, Revolved,
RuledLofted, Split, Swept, TrimmedEdgeExtend, TrimToCurves, TrimToPlane, TrimToSurfaces, Untrim) and in mccadutil.ui
(SurfaceNormalOrient, SurfaceNormalSet); Offset has MFC dialogs 6531-6542 (exports/mccadutilres_dialogs.tsv). None
decoded.

| button (id) | FT (DLL) | OCCT candidate |
|---|---|---|
| Simple: Cylinder / Block / Sphere / Cone / Torus (53542-53546) | `create_*_surface` = mccad `create_*` PARAM 0 | the S1-S5 primitives, faces only |
| Create: From Solid (53547) | `sld_convert_to_surfs` (MCCAD, imported from mcgeomsld) | TopExp faces -> Geom surfaces |
| Flat Boundary (53548) | `FlatBoundarySurfaces` | BRepBuilderAPI_MakeFace(plane, wire) |
| Loft (53549) | `RuledLoftedSurfaces` | BRepOffsetAPI_ThruSections(isSolid = false) / GeomFill_NSections |
| Extrude (53550) | `ExtrudeSurface` | BRepPrimAPI_MakePrism(wire) |
| Sweep (53551) | `SweptSurfaces` | BRepOffsetAPI_MakePipeShell |
| Revolve (53552) | `RevolvedSurfaces` | BRepPrimAPI_MakeRevol(wire) |
| Draft (53553) | `DraftSurface` | BRepOffsetAPI_MakeDraft |
| Net / Fence (53554-53556) | `NetSurfCreation`, `FenceSurfCreation` | GeomFill_Coons / BRepFill_Filling; fence: ours |
| Offset (53557) | `OffsetSurfaces` (**currently mis-enabled, see top**) | BRepOffsetAPI_MakeOffsetShape / Geom_OffsetSurface |
| Modify: Trim to Curves / Surfaces / Plane (53558-53561) | `trim_surface_2` / `_1` / `_3` | BRepFeat_SplitShape, BRepAlgoAPI_Splitter |
| Fill Holes, Extend, Extend Trimmed Edges (53562-53565) | `FillHoles`, `SurfaceExtend`, `ExtendTrimmedEdge` | BRepFill_Filling; GeomLib::ExtendSurfByLength |
| Fillet to Surfaces / Plane / Curves (53566-53569) | `fillet_surface_1` / `_3` / `_2` | BRepFilletAPI on sewn shells / ChFi3d (weak) |
| Two / Three Surface Blend, Three Fillet Blend (53570-53572) | `blend_2_surfs`, `blend_3_surfs`, `blend_3_fils` | BRepFill_Filling / GeomFill (no direct equal) |
| Untrim, Remove Boundary, Split (53573-53575) | `UnTrimSurfaces`, `RemoveBoundary`, `SplitSurface` | BRepBuilderAPI_MakeFace(surface) / GeomTrimmedSurface |
| Normals: Change / Set / Orient (53576-53578) | MCCADUTIL `change_normals`, `set_normals`, `orient_normals` | TopoDS_Shape::Reverse / ShapeFix_Shell orientation |

Blocker for the whole tab: Q6 (entity storage + SURF_ID read / write, which today only has unverified layouts).

## Where OCCT will differ from Parasolid (summary of the per-command notes)

- **Loft / sweep parametrisation**: Mastercam matches section curves itself (PK_BCURVE_make_matched / combine) and
  sweeps per segment [inferred]; ThruSections / MakePipeShell parametrise differently (twist, seam position).
- **Variable-radius fillet**: KI BLECVR law vs OCCT law functions; Linear / Smooth flow mapping NOT DECODED.
- **Draft**: Parasolid isocline (`PK_FACE_install_surfs_isocline`) vs OCCT neutral plane + pull direction; sign
  convention NOT DECODED (also extrude.md 4.3).
- **Blend overflow / cliff edges / mitered corners**: Parasolid options (Overflow combo, Mitered corners) have no OCCT
  options; failures and corner patches will differ.
- **Shell**: PK_BODY_hollow_2 handles Both directions in one call; MakeThickSolid is one-sided per call.
- **Boolean tolerance**: Parasolid opts tolerance 1e-5 m; OCCT exact + optional fuzzy value; disjoint-result errors
  (sld 668 / 671 / 672) must be re-implemented.
- **Patterns**: Parasolid face instancing vs OCCT tool-body copies + booleans.
- **Version-dependent behaviour**: Mastercam selects PK option tokens by op version; OCCT cannot reproduce old
  versions.
- **History**: transforms / edits regenerate history in Mastercam; with OCCT we either keep our own feature list or
  work on bricks (Q2).
- **Units**: Parasolid runs in metres with lengths divided by 1000 / 39.37007874015748; OCCT can run in part units
  directly (tolerances must then be scaled - an "ours" choice, Q5).

## Proposed order (router shop value) and next decode steps

1. **Transform on solids** (T1-T7) once Q1 / Q2 are answered; wireframe transforms first (no OCCT; parallel task).
   Decode: xform_type enum, brick transform call, Copy-mode prompts (sec. T.1).
2. **Block, Cylinder** (S1, S2: geometry already decoded) then Cone / Sphere / Torus. Decode: the panel loops
   (mccad FUN_180011b50, FUN_18002dc80, FUN_180032a00), runtime defaults, which dialog variant shows, base-point /
   anchor rules.
3. **Boolean** (S7). Decode: SolidsBooleanManager panel <-> P, defaults, non-associative flow (sld 727), the disjoint
   rule in FUN_18003e630.
4. **Revolve** (S6). Decode: panel <-> P, angle defaults, axis pick, Reverse Axis.
5. **Constant Fillet, Chamfers** (S8, S9). Decode: panel <-> P (Overflow / Propagate / Mitered bytes), selection of faces
   / bodies -> edges (`sld_fillet` edge collection), chamfer reference face.
6. Then Shell, Draft, Trim by Plane, Thicken, Loft, Sweep, Patterns, Variable / Face-Face fillet, From Surfaces;
   Model Prep last; Surfaces depending on Q6.

For each step the decode should follow formats/extrude.md's structure (classes + parameter block, command flow, panel,
defaults / persistence, preview, geometry, messages, operation tree) and dump into decomp/<cmd>/ like
decomp/extrude/.
