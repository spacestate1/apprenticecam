# Mastercam 2017 Wireframe tab: command behaviour (native side)

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

Sources: Ghidra decompiles of MCCAD.DLL (`decomp/mccad_sketch.c` plus the extra dumps `decomp/mccad_points_extra*.c`, `mccad_lines_helpers.c`, `mccad_arcs_helpers.c`, `mccad_shapes_*.c`, `mccad_fillet_chamfer*.c`) and MCCADUTIL.DLL (`decomp/mccadutil_modify.c`, `mccad_util_modify2.c`, `mccad_util_project.c`). The ribbon-to-function map is `viewer/ui/mastercam_ui.txt` (Wireframe tab; that file is no longer in the repo - the command index is `formats/ui_commands.tsv`, the ribbon `formats/ui_ribbon.json`).

Tags: **[code]** means read directly from the decompile or DLL data. **[inferred]** means a naming, meaning or semantic guess. Most function panels (field labels, some defaults) live in the .NET `mccad.ui.dll` / `mccadutil.ui.dll`; their BAML outlines were dumped later (2026-10-16, `formats/ui_wireframe_panels.txt` by `tools/baml_panel.py`) and the managers' registered keys decompiled (`decomp/mccad_panel_keys.c`, `decomp/mccadutil_panel_keys.c`); see "Code" at the end. Native defaults below are the values the native side seeds or registers. `RegisterVariable(var, metric, inch)` pairs are given as inch / mm. Hex doubles are converted to decimal.

Entity type ids: point 0x1, line 0x2, arc 0x4, parametric spline 0x8, NURBS 0x80, surface 0x100 (see `formats/mcam.md`).

Section order: Points (incl. Bolt Circle, Bounding Box), Lines, Arcs, Shapes, Fillet/Chamfer, Modify (MCCADUTIL).

## Points: conventions
Entity type ids used below (from formats/mcam.md): P_ID 0x1, L_ID 0x2, A_ID 0x4, S_ID 0x8 (parametric spline),
NB_ID 0x80 (NURBS), SURF_ID 0x100, CONS_ID 0x400, CD_ID 0x800. Extra decompiles produced for this part:
`decomp/mccad_points_extra.c`, `decomp/mccad_points_extra2.c`, `decomp/mccad_points_extra3.c`.

## Point Position

- Body: `sketcher_point_1` @180197e90 -> `FUN_180196d20` (class `WireframePointPositionApp`, panel `IWireframePointPositionManager`). [code]
- Input: repeated position picks (`get_event` mask 0x10008009). Each pick creates one point (`createPoint(p_3d)`), wrapped in an undo transaction; Apply (panel button 1) commits; OK = 0, Cancel = 2. [code]
- Panel struct (`FUN_1800e5540`): `+0 bool` = "edit last point" flag (panel vfunc 0x38 == 0); `+4 int` = point-style option 0/1/2. [code]
  - When the edit flag is set and a point exists, the next pick moves the last-created point instead of creating a new one (`FUN_180196c60`: rewrite P_ID with new xyz). [code]
  - Style option: 0 -> main attribute point style (main_attrib+0xc) reset to 1 if it was 7/8; 1 -> style 7; 2 -> style 8. `sketcher_point_1(1)` / `(2)` force style 7 / 8 for the session and restore afterwards. [code] Meaning of styles 7/8 not determined. [unknown]
- Output: P_ID points at picked position (snap/autocursor result). [code]

## Point Dynamic

- Body: `sketcher_point_2` @18034d540 -> `FUN_18034aeb0` (class `WireframePointDynamicApp`). [code]
- Input: pick one entity, mask 0x18e | 0x400000 = line, arc, spline, NURBS, surface (+ solid face). Then cursor slides along it; each click creates output. [code]
- Distance along: measured with `creep_along_ent` from the end **nearest the original pick**: if pick parameter > mid-parameter, distance is taken from the far end (`dist = length - d`) (`FUN_18034d080` sets flag +0x2990, `FUN_18034d310`). Tangent at that point is computed with `eval_curve` and normalised. [code]
- Offset (`FUN_18034ce00`, param_4 = offset distance, param_3 = offset vector): if |offset| <= tol one result at the on-curve point; otherwise two results at `p + v` and `p - v` (both sides). [code]
- Output mode int at panel +4: 0 = points only; 1 = lines only (line from on-curve point to offset point, `FUN_18034d030` -> `store_the_line`); 2 = points, plus lines when offset != 0. [code]
- The offset / output-mode code above is the shared `FUN_18034ce00`; Point Dynamic calls it with offset 0, mode 0: one point. [code]
- **The Along box** (decoded 2026-11-05; the Ghidra decompile of `FUN_18034aeb0` drops these branches - from the disassembly
  18034aeb0-18034c290, kept in ~/mastercam-re/work/softlock_gh/pd_dis.txt): panel struct (reader `FUN_180352370` :888, every
  call GetLocked(false); writer `FUN_180352830` :2305 SetValue then SetLocked) `+0` CurrentlyEditing == Point, `+8` Along
  (manager [8], key WireframePointDynamic-AlongDistance, default 0), `+0x10` its lock (hard or soft). IsEnabled: off at the
  start (:293) and after Apply, `!surface` at each entity pick (`FUN_18034c290` :1120, which also sets the limits: 0 .. the
  length rounded to the display precision for a closed curve / spline, else +-1e38).
  - A mouse move: the parameter follows the cursor (`FUN_1801c90d0` :1292; lines / arcs not clamped), the Along written (an
    unlocked box only). A click: the same, then the point at the click's distance (a locked box does not move it, :571);
    each point live (`sktcher_live_color`), the previous one committed; the entity stays picked.
  - Code 4 (Along Value `FUN_180351bf0` / IsHardLocked `FUN_180351b00`): with the live point - UndoTransTerminate, the point
    again at the box's value from the measuring end, whatever the lock; without - locked: a point at the value at once.
  - Code 1 (Apply): locked and a curve picked - the live point discarded, a point at the value made and committed; else the
    live point committed, the box disabled; back to the entity selection. OK / Cancel / Esc: the loop exit commits.
  - The soft lock: `FUN_18034ae30` (:850, not hard-locked -> 0, write with 0) after every point and the code 4 rebuild.
  - A locked box at the entity pick makes a point at once (disasm 18034b491-18034b51c) - whether at the locked distance or the
    pick position (the pick event's +0xe0 point) is NOT DECODED: not ported.
  - Viewer: `geo_ptdyn_panel`, `geo_ptdyn_far`, the Apply branch of `geo_panel_button`; scene `soft_ptdyn`. Surfaces: not here
    (the viewer's Point Dynamic picks curves only). The Reselect button (CurrentlyEditing, code 7) is not ported.
- Surface picks: point evaluated at surface (u,v) with `eval_surf`; solid face converted to a surface first (`sld_face_to_temp_file`). [code]

## Point Segment

- Body: `sketcher_point_4` @18020ca70 -> `FUN_18020b240` (class `WireframePointSegmentApp`). [code]
- Input: pick one entity (mask 0x40008e: line, arc, spline, NURBS, + solid edge). [code]
- Panel struct (`FUN_1800ebaa0`): `+0 bool distanceLocked`, `+8 double distance`, `+0x10 bool countLocked`, `+0x14 int count`. On a new pick both are reset to 0 (`FUN_18020c8f0`). Distance limited to [0, entity length rounded up to display precision], count limited to [0, 30000] (`FUN_18020c960`). [code]
- Mode rule (`FUN_18020b240`): radio/edit event 3 = distance mode, 4 = count mode. If distance <= tol or (count mode and same entity) -> `createPointsAlongAnEntity(ent, count, ...)`; afterwards distance shown = length/(count-1) (0 if count<2) => **count includes both endpoints**. Else -> `createPointsAlongAnEntitySpaced(ent, distance, pickParam, ...)`; count shown = round(length/distance). Spaced points start from the end nearest the pick (pick parameter passed). [code / start-end: inferred]
- Output: P_ID points. [code]

## Point Endpoints

- `create_point_endpoints` @180008070 (no dialog). param_1 = 0 -> preview only (temp points, wait for space); nonzero -> create. [code]
- Scans all **visible** entities (display list or `get_vis_ent`, mask 0xc8e = line, arc, spline, NURBS, CONS, CD). If anything is selected, only selected entities are used. [code]
- Filters: S_ID skipped if byte ent+0xb5 < 3, NB_ID skipped if byte ent+0xd1 < 3 (field meaning unknown); CD_ID only processed if body+0 != 0 or subtype (+0xc) == 0x6f or >= 200. Lines/arcs always. [code]
- For each entity: `curve_endpoints` -> both endpoints, each projected to the construction plane (`proj_pt_cp`) and stored as P_ID with main attributes. No duplicate removal seen (shared endpoints give coincident points). [code]

## Point Nodes

- `create_spline_points` @180008500: select one spline (mask 0x88). [code]
- S_ID (parametric) -> `createPointsNodeOnParaSpline` (points at the spline nodes); NB_ID -> `createPointsCtrlOnNURBSpline` (points at NURBS **control points**). [code]
- Ported 2026-11-09 (N9-16; viewer/core_ui/cui_wfmisc.c `wm_ptnodes`, no panel: one pick, the command ends). S_ID: t = 0 .. segments (segments - 1 when closed, ent +0xb7), `pt_on_ent_from_t`; NB_ID: the control points (+0xcc count, +0xd1 doubles per point 3 / 4, +0xd2 bit 2 = non rational; a homogeneous point is w x y z). `proj_pt_cp` per point: the depth in 2D construction.

## Point Small Arcs

- Body: `sketcher_point_6` @1801da910 -> `FUN_1801d92a0` (class `WireframePointSmallArcsApp`). [code]
- Input: window/selection of arcs (mask 4); preselection honoured; event 0x3b = "select all visible arcs" (`FUN_1801da820`). [code]
- Params (`FUN_1801d92a0` init, `FUN_1800f1c00` read): `+8 double maxRadius = 0.1` (same constant for inch and metric in native code; panel may override) ; `+0x10 bool partialArcs = false`; `+0x11 bool deleteArcs = false`; `+0x12` radius-locked. [code]
- Rule (`FUN_1801da5f0`): for each selected A_ID with `radius <= maxRadius + 1e-8` and (partialArcs or |sweep| == 2*pi within 1e-8): create P_ID at arc centre (view_to_world of arc centre in its view, then `proj_pt_cp`); if deleteArcs, delete the arc. [code]

## Bolt Circle

- `OnBoltCircle` @1800654d0: MFC modeless dialog `BoltCircleDlg` (res 0x281a) with params object `BoltCircleParams` (`FUN_18005f660` ctor). Params persist to `%TEMP%\BoltCircleParams.prm` (load `FUN_180062870`, save `FUN_180064300`); on load the axis mode is clamped to 0 if > 2 and removal lists cleared. [code]
- BoltCircleParams fields and defaults (`FUN_18005f660`): [code]
  - `+0x10 p_3d center` = (0,0,0) (picked by position, or from a picked arc's centre: event 2 copies arc centre).
  - `+0x28 double boltRadius` = **5.0 in / 100.0 mm**.
  - `+0x30 bool fullCircle` = **true** (evenly spaced over 360).
  - `+0x34 int instances` = **8**.
  - `+0x38 double startAngle` = **0 deg**.
  - `+0x40 double angleBetween` = **45 deg** (used as the step in all modes; UI keeps it = 360/n for full circle [inferred]).
  - `+0x48 bool createPoints` = false; `+0x49 bool createCircles` = **true**; `+0x4a bool createCenterPoint` = false; `+0x4b bool keepBoltCircle` = false.
  - `+0x50 double holeRadius` = **0.25 in / 5.0 mm** (stored as arc radius).
  - `+0x90 bool reverse (clockwise)` = false (negates angles).
  - `+0x08 bool useAxis` = false; `+0x58 int axisMode` = 0 (0 = X, 1 = Y, 2 = Z of active view; 3/4 = vector from +0x60 to +0x78 points; default +0x78 = (1,0,0)) — defines the plane via `find_view_k` (`FUN_180061bd0`). Otherwise active Cplane.
  - `+0x98 / +0xd0` RemovalLists: instance indices removed from points / circles (user clicks an instance, `FUN_180063fe0`).
- Geometry (`FUN_180062970` -> `FUN_180061900`, `FUN_180060e60`, `FUN_180060700`, `FUN_180060c60`): [code]
  - Instance i position (view coords) = center + R*(cos(start + i*step), sin(start + i*step)), step = +/-angleBetween; i = 0..n-1, skipping removed indices.
  - Points: P_ID at each position (if createPoints). Circles: full A_ID (start 0, sweep 2*pi) radius holeRadius at each position (if createCircles). Centre point: P_ID at center (if createCenterPoint).
  - Bolt (reference) circle: A_ID radius R, start = startAngle, sweep = 360 (fullCircle) or (n-1)*angleBetween capped to 360; created in color 15 and purged unless keepBoltCircle. Fails with warning (msg 0x8145) if sweep <= tol.
  - Requires n >= 1 and (createPoints or createCircles).

## Bounding Box

Decoded 2026-10-22 (full spec incl. stock mode; decomp/mccad_bbox_stock.c, mccad_bbox_panel.c, mccad_bbox_manager*.c,
mccad_bbox_pushpull.c, mccad_bcyl*.c, mccad_points_extra*.c; managed side mccad.interop `BoundingBoxManagerViewModel`, BAML
mccad.ui `boundingbox/boundingboxmanagerview.baml`, tooltips `MCCAD.UI.BoundingBox.BoundingBoxManagerViewToolTips.resx`).
Ported in viewer/core_ui/cui_bbox.c (both apps). [code]

- Entry points: `create_bbox` @180349300 -> `FUN_180347380` -> driver `FUN_180343ae0(app, s, box[6], cyl[8], allowCyl, adoptPlane, stl, stlEnt)`
  (wireframe: `(…, 1, 1, 0, 0)`); Stock Setup "Bounding box" (25451) mc:`FUN_1801f1150` -> `create_boundingshape_either_method`
  @18005ecb0 (`(…, 1, 0, 0, 0)`, +0x60 stock mode); `create_bbox_stock(box, int)` @18005e0b0; the `*_no_dlg` variants run
  the compute without a panel (cui_stock.c). Driver result 0 = OK, 3 = Cancel / panel failed.
- Settings struct (0x68 bytes, copy `FUN_18005d4f0`, panel -> struct `FUN_18015c3d0`, struct -> panel `FUN_18015d2c0`):
  `+0 AllEntities` (All shown), `+1 Lines and arcs`, `+2 Points`, `+3 Center point`, `+4 Solids`, `+8/+0x10/+0x18 SizeX/Y/Z`
  and `+0x20 Radius`, `+0x28 Height` (**sizes**, not expansions; negatives clamped to 0), `+0x30 rectangular`,
  `+0x34 RectangularOrigin` 0..10 (out of range -> 9), `+0x38 CylindricalOrigin` (0 Top, 1 Bottom, 2 Center),
  `+0x3c` cylinder axis code **0 = Z (the plane normal), 1 = Y, 2 = X** (VM Axis X -> 2, Y -> 1, Z -> 0; `FUN_18005bab0`:
  0 = active view, 1 = RotateRelative 3 (Back), 2 = RotateRelative 5 (Right)), `+0x40 CenterOnAxis`, `+0x41 Incremental`,
  `+0x42 BothDirections`, `+0x43 CreateToolPlane`, `+0x48 ToolPlaneName`, `+0x50 / +0x51` set as tool plane / WCS,
  `+0x52 / +0x58` STL mode, `+0x60` stock mode. (Earlier notes here had the axis codes backwards.)
- Defaults: `enable_dialog_modality` (mc: .data 0x18087b836) = 1 -> the modal statics are used and copied back on OK:
  wireframe `DAT_18064fc20` = Lines on, rect, origin 9 (BottomCenter), cyl origin 1 (Bottom), axis 2 (**X**), Incremental,
  all else 0; stock `DAT_180649fc0` = the same with Lines off. Non-modal hard defaults (`FUN_180347380` / `FUN_18015cd40`) are
  not used (the modality writers are NOT DECODED). Stock-mode overrides: Manual, no lines / points / centre / solid,
  rect = the page's shape, axis from the page (1 -> 1, 2 -> 0, else 2), centre from the page, +0x43 = 0, +0x60 = 1.
- Driver rules: Solids forced off without `sld_solids_ok`; not stock and all four create flags off -> Lines on; stock ->
  EnableApply false (Apply greyed), EnableCreateToolPlane false; the Create Geometry checks stay enabled.
- RectangularOrigin enum (= the BAML CommandParameter): 0 TopBackLeft, 1 TopBackRight, 2 TopFrontLeft, 3 TopFrontRight,
  4 TopCenter, 5 BottomBackLeft, 6 BottomBackRight, 7 BottomFrontLeft, 8 BottomFrontRight, 9 BottomCenter, 10 TrueCenter
  (Back = ymax, Front = ymin, Left = xmin, Top = zmax).
- Panel (BAML order): Basic tab - Entities (Select: _Manual + Select Entities button (code 7) / _All shown), Shape
  (_Rectangular / _Cylindrical), Rectangular Settings (_Origin: 11 image radios in the order TopBackLeft, TopBackRight,
  BottomBackLeft, BottomBackRight, TopFrontLeft, TopFrontRight, BottomFrontLeft, BottomFrontRight, TopCenter, TrueCenter,
  BottomCenter; Size: _X _Y _Z, keys BoundingBox-SizeX/-SizeY/-SizeZ), Cylindrical Settings (Ori_gin: Top / Center / Bottom;
  Radi_us, Heigh_t (keys BoundingBox-Radius / -Height); Axis: _X _Y _Z; Ce_nter on axis), Push Pull (Ab_solute /
  _Incremental, _Both directions), Create Geometry (_Lines and arcs, _Points, C_enter point, Soli_ds); Advanced tab -
  _Create Tool Plane (header check, _Name:, _Set as current tool plane, Set _WCS from new tool plane), Volume (read-only, F4).
- Panel codes (`FUN_18015cb60` + ctor lambdas via `FUN_18015ca20`): 4 size / Incremental change -> compute without
  recompute (resize); 5 Shape / Axis / CenterOnAxis -> recompute; 7 AllEntities / Select button -> the entity step again;
  8 origin change -> only in 2D and only when the Z side (Top 0..4 / Bottom 5..9 / TrueCenter) or the cylinder origin
  changes; 0 OK, 1 Apply (then code 7), 2 Cancel.
- Entities: mask 0x1905ff. Manual (`FUN_1803470e0`): clear the selection, `select_ents` with mccadres 81 "Select entities,
  use Ctrl-A to select all"; it needs >= 1 entity, else the command ends as Cancel. All shown: `set_clr_sel_bits(4,0,4,1,mask)`.
- Compute `FUN_180346eb0(…, recompute, final)`: not recomputing -> `FUN_180346490` applies the sizes keeping the anchor
  (X: Left 0,2,5,7 keep xmin, Right 1,3,6,8 keep xmax, else centred; Y: Back 0,1,5,6 keep ymax, Front 2,3,7,8 keep ymin;
  Z: Top 0..4 keep zmax, Bottom 5..9 keep zmin, 10 centred; cylinder: Top c += dir (len - H), Center c += dir (len - H) / 2,
  Bottom c stays). Rect `FUN_180345930`: create flags 0 and not stock -> fail; recompute -> `make_bounding_box(box, true)`;
  2D (`FUN_180347840`): Z = the construction depth, then (not recomputing) SizeZ below (origin < 5), above (5..9) or both
  halves (10); stock write `FUN_1803481e0`; valid = all three spans >= tol. Cylinder `FUN_180345da0`: recompute ->
  `FUN_18005bab0`; 2D `FUN_180347960` (centre to the depth, len 0, then Height by the origin); stock write `FUN_1803483b0`;
  valid = r > 0. Recompute: no live geometry -> sizes and box 0, else sizes := extents (`FUN_180345f80`). Failure ->
  Solids off; Volume (`FUN_180348590`) = |dx dy dz| or pi r^2 len, 0 on failure.
- Output (final compute, OK / Apply; bits from `FUN_180345540`: 1 = Points, 2 = Lines and arcs, 4 = Center point, 8 = Solids):
  rect `FUN_18005a2e0` with corners P0 (xmin,ymin,zmin) P1 (xmax,..) P2 (..,ymax,..) P3, P4..P7 at zmax: bit 1 the corners
  P0, P1 if dx, P2 if dy, P3 if dy and dx, P4..P7 likewise if dz (degenerate axes deduplicated); bit 4 the centre (average of
  the 8 corners), then the face centres xmin (if dy or dz), xmax (dx and (dy or dz)), ymin (dx or dz), ymax (dy and (dx or
  dz)), zmax and zmin (dz and (dx or dy)); bit 2 the edges in the order P0P1, P2P3, P4P5, P6P7, P0P2, P1P3, P4P6, P5P7,
  P0P4, P1P5, P2P6, P3P7, each only when its span(s) > tol (4 lines for a flat box); bit 8 `sld_basic_primitive` 0xf.
  Cylinder `FUN_18005b220(c, dir, r, len, flags)` (r >= tol; the view = `find_view_k(dir)`): bit 1 base centre and (len > tol)
  top centre, bit 4 the axis mid-point, bit 2 full circles (start 0, sweep 2 pi) at the base and (len > tol) the top - no
  lines; bit 8 a solid cylinder (0x11) when len > tol. Main colour / level.
- Stock mode: every compute writes the active machine group's stock (rect: Display 1, shape 0, sizes = spans, origin =
  (xmin, ymin, zmin), corner 8; cylinder: shape 1, axis code -> page axis (0 -> 2, 1 -> 1, 2 -> 0), centre, +0x1230 = 2 r,
  +0x1238 = len, +0x1240 = c.z, origin = c). On success the page gets the result (rect -> `FUN_1801edd80` with the page's
  corner + Display / Fit screen; cylinder -> axis, centre, origin c, diameter 2 r, length len); afterwards group +0x1228 is
  overwritten from the `*_no_dlg` cache `DAT_1806a1790`.
- Ported here: all of the above except as listed. Greyed / NOT PORTED: the live solid and the push-pull arrows (Push Pull
  expander greyed), Solids (check greyed, +4 forced off), Create Tool Plane (planes not ported; the Name TextBox left out),
  STL mode, plane-change events, the circles of an X / Y axis cylinder (this program's arcs lie in the XY plane), Help.
  NOT DECODED: the panel caption, MCEvent 0x8000 sub-ids, `set_clr_sel_bits`' 4th argument, the in-place live-solid update
  `FUN_180347b00` / `FUN_180347f70`, `FUN_180346160`, who sets CenterOnAxis.IsEnabled (enabled here), model vt+0x18.
- Deviations (ours): the preview is the Lines-and-arcs outline (flag 2 of the output functions) drawn in the live colour as
  an overlay instead of the live solid; the Manual selection is this program's entity selection ended with Enter (panel
  greyed meanwhile); solid bodies only via All shown; the Select Entities button shows its tooltip header as text and the
  origin image radios their BAML CommandParameter; the construction depth is this program's grid Z; after a stock-mode run
  the machine group's stock is restored to its state at the start (not the `*_no_dlg` cache), so the page's own OK / Cancel
  decides what is stored; test steps `bbenter`, `bbdump`, `constr3d:N` (cui_geo.c, ours).

### Rest of the panel (2026-10-22): live solid, Solids, Push Pull, Create Tool Plane, X / Y circles
Specs: formats/bbox_pushpull.md (live solid FUN_180345b10 / FUN_180345c50, SLD_PRIMITIVE_PARAMS fill, face ids, arrow
FUN_180344b30, drag FUN_1803450b0 / FUN_180345180, commit FUN_180346a60; correction: code 6 = a CANCELLED drag, FUN_180346850)
and formats/bbox_toolplane.md (find_view_k, FUN_180345fd0 / FUN_180346160 plane origin + Cplane matrix, MastercamPlanes::Add
naming "Plane" / "%s-%ld", SetActivePlane types 0 Cplane 1 Tplane 2 Gview 3 WCS, sld_basic_primitive layout). The cylinder
circles (FUN_18005b220 bit 2) are A_ID arcs in the find_view_k(dir) view: X -> view 5 Right, Y -> view 2 Front, Z -> Top
(formats/mcam.md "Arc views"). Port: viewer/core_ui/cui_bbox.c (header lists ours / NOT DECODED).

## Live results (per command)

Read from each command's main loop in `decomp/mccad_cmds/<entry>.c` (line numbers are in those files). The **live** pattern:
the result is created at once inside an open undo transaction (`UndoTransStart`) and coloured with `sktcher_live_color`
(config 5026), or with `store_ent(..., live_entity_color)` / `rect_set_rents_color(live_entity_color)`. It is committed with
`sktcher_main_color` + `UndoTransCommit` (rectangles: `rect_set_rents_color(main_color)` + `rect_free_rents`). Panel codes
(event 0x10000000, code at +0x158): 0 = OK, 1 = Apply, 2 = Cancel. Shape dialogs post their own codes (0x65-0x72, 100). [code]

| Command | Live? | Rebuild on panel change | Commit | Esc / Cancel |
|---|---|---|---|---|
| Point Position (FUN_180196d20) | yes, one point per click (:303-310) | none (the "edit last point" flag moves it, :315) | next click :303-306, Apply :263-268, exit :324-327 | commits (code 2 / Esc break, exit commit) |
| Point Dynamic (FUN_18034aeb0) | yes (`FUN_18034ce00`, :582-583) | code 4 recomputes (:539-547) | **not found** in the function | unclear |
| Point Segment (FUN_18020b240) | yes, the point list (:438-460) | Terminate + `delete_ent` list + create again (:426-475) | next entity pick :288-292, Apply :391-418, exit :512-515 | commits |
| Point Endpoints, Point Small Arcs | no: create + `UndoTransCommit` at once | - | - | - |
| Bolt Circle (FUN_180062970) | yes, `store_ent(live_entity_color)` | `FUN_1800611c0`: Terminate + create, on a dialog change **or a new centre pick** | 0x66 Apply / 0x68 OK (`FUN_180061750`) | **removes** (100 / Esc: `FUN_1800642b0` UndoTransTerminate, :236-239) |
| Bounding Box (FUN_180343ae0) | yes (:538-539) | `FUN_180347310` Terminate + Start | Apply / OK (:664-678, :785) | removes (:781-782) - ported with its own preview / commit (cui_bbox.c, "## Bounding Box") |
| Line Endpoints (FUN_1801913b0) | yes (:634-647) | in place, `replace_ent` (`FUN_180190f30`, `FUN_1801904e0`) | next pick :1589-1596, codes 0 / 1 `FUN_18018ea40`, exit :2113-2116; **multi-line: each line at once** (:742-753) | commits |
| Line Parallel (FUN_18022a9b0) | yes, both lines (:1085-1086) | Terminate + Start + create (:1057-1086) | next pick :487-495, codes 0 / 1, exit :1130-1134 | commits |
| Line Perpendicular (FUN_180203280) | yes (:782) | code 4 `FUN_18018a6d0` rescale, code 5 `FUN_180204d10` | next pick :490-495, codes 1 / 6 :378-383, exit :902-906 | commits |
| Line Closest | no (`UndoTransStart; createLineClosest; UndoTransCommit`) | - | - | - |
| Line Bisect (FUN_1801cbb30) | yes (:1724-1731) | code 5: delete + Terminate + Start + create (:523-544); code 4 rescale | next pick :328-338, code 1 :463-470, exit :593-594 | **removes** (code 2 -> `LAB_1801cc96b`: `delete_ent` + `UndoTransTerminate`, :473-476, :580-590) |
| Line Tangent Through Point (FUN_180272630) | yes (:485-490) | rescale :600-628, `replace_ent`, Terminate + create :417-419 | next pick :361-363, code 1 :690-696, exit :722-731 | commits (main loop) |
| Circle Center Point (FUN_1801bf090) | yes (:325-330) | codes 5 / 6: `replace_ent` keeping the centre (`FUN_1801999a0`) | next pick :306-309, codes 0 / 1 :570-573, exit :600-605 | commits |
| Arc 3 Points (FUN_1801b90b0) | yes (:1541-1548) | none from the panel (codes 4-8: prompts only) | next pick :801-806, code 1 :432-434, exit :597-602 | commits |
| Arc Tangent (FUN_1801b4b60) | modes 0, 1, 2, 6 yes; **modes 3, 4, 5 commit at once** (:570-617) | codes 4 / 5: Terminate + Start + create from the picks and the kept candidate index (:413-419, :819-830) | next pick :363-367, codes 0 / 1 :833-843, exit :851-856 | commits |
| Circle Edge Point (FUN_1801c51b0) | yes (:765-771, :887-894) | only the tangent types' radius (:1250-1252) and edit-point mode | next pick :712-731, codes 0 / 1 :1404-1434, exit :1051-1056 | commits |
| Arc Endpoints (FUN_1801a7970) | yes (:401-403) | codes 8 / 9: `replace_ent` (`FUN_1801a9280`) | next pick :353-357, codes 0 / 1 :719-721, exit :866-871 | commits |
| Arc Polar (FUN_1801afae0) | yes (`FUN_1801ae880` :3173-3180) | codes 5-9: `replace_ent` keeping the centre | next centre pick :2589-2592, codes 0 / 1, exit :906-911 | commits |
| Arc Polar Endpoints (FUN_1801abb30) | yes (:377-407) | codes 7-10: Terminate + Start + create (:305-307, :373-407) | next pick :338-343, codes 0 / 1 :310-323, exit :508-512 | commits |
| Rectangle (FUN_1801e3ca0) | yes, `rect_set_rents_color(live)` (:558-559, :630-631) | `FUN_1801e3af0`: delete + Terminate + `create_rectangle1(base)` | next corner click (`FUN_1801e34a0` :590-600), codes 0 / 1, exit :862-863 | commits (Esc :399 -> exit block) |
| Rectangular Shapes (create_rectangle) | yes (:381-382, :441-442) | `FUN_180082950` (:659-683) | next base click :393-394, 0x67, exit :485-493 | commits |
| Polygon (create_polygon) | yes (`FUN_18007e7f0` :772) | `FUN_18007e960` from the centre | next centre click :249-252, 0x6a, exit :293-301 | commits |
| Ellipse (create_ellipse) | yes (`FUN_18007b380` :1165-1200) | `FUN_18007b760` | next centre click :204-209, 0x6b, exit :351-361 | commits |
| Helix / Spiral | yes (`FUN_180088e00`, `FUN_180089040`) | 0x6f / 0x65: Terminate + create | next base click, 0x72 / 0x68, exit | commits |
| Fillet Entities (FUN_18026c370) | yes, arc (+ relief line) (:524-541) | codes 4-7: Terminate + Start + `make_fillet` (:459) | next pick :318-326, codes 0 / 2 :428-436, code 1 :445-454, exit :608-611 | commits (main loop) |
| Fillet Chains (FUN_18027ce30) | yes, made in the **preview colour 0xf** (8 on a 0xf background), not `sktcher_live_color` (`FUN_18027d7d0` :453-506) | codes 5-9: delete + Terminate + create (:286-291) | `LAB_18027d44e` (:247-268) on codes 0 / 1 / 2 and `get_event` != 0 (:237) | commits |
| Chamfer Entities (FUN_180218910) | yes (:516) | Terminate + Start + `make_chamfer` | next pick :295-297, code 1, exit :541-544 | commits (codes 0 / 2, see below) |
| Chamfer Chains (FUN_180210bb0) | yes, `main_color` = 0xf / 8 while previewing (`FUN_1802114b0` :460-507) | codes 5-7 (:286-291) | `LAB_1802111df` :250-273 (rebuilt, committed) | commits |
| Offset, Offset Chains, Project | Xform preview (`XfPreview`, `UndoXformTransStart`); preview colours not decoded | 0x66 (Offset Chains: also after a chain is reversed) | 0x65 / next pick | 100 deletes the preview |
| Trim Break Extend, Break Two Pieces, Break at Intersection, Break at Points, Break Circles, Close Arc, Join, Trim Many | no: each operation `UndoTransStart` ... `UndoTransCommit` at once (Trim Break Extend has a hover preview only, :407-435) | - | - | - |
| Break Many (FUN_1800b0330) | yes, the pieces (`FUN_1800afcc0` :1254) | codes 4-9 (:276-292) | code < 2 (:302-310), exit (:343-350) | commits (code 2 sets local_46c and breaks; the exit block commits) - ported 2026-11-07 |

**The panel check (vtable +0x18).** Almost every sketcher command calls a helper before its main switch (chamfer
`FUN_1802186d0`, fillet `FUN_18026c0d0`, line `FUN_18018eb40`, arc `FUN_1801bed50` ...). While panel vf+0x18 returns true
the helper takes the live result away (`delete_ent`, or `UndoTransCommit` + `UndoTransEvent(true)`), and puts it back
(`undelete_ent` / `RedoTransEvent`) when it turns false; a code 2 (Cancel) while it is true makes the removal permanent
(`UndoTransTerminate` or `RedoStackTruncate`). With `EnableInlineUndoRedo` and `SetDisableUndoRedoUI` around it, this looks
like the panel's own Undo / Redo buttons [inferred]. Outside that state the main loops send code 2 (and `get_event` = 2, Esc)
to the loop exit, which commits: Chamfer Entities included (`(code & 0xfffffffd) == 0` commits for codes 0 and 2, :385-398).
The viewer commits Chamfer Entities on Esc / Cancel as decoded (`geo_live_kind` G_CHAMF has no LV_ESC_DROP, 2026-10-16;
the earlier "Esc removes the chamfer" behaviour is gone). The panel's own Undo / Redo state is not ported. [code /
inferred as marked]

**Picked entities.** Entity picks go through `get_event(prompt, ev, mask, type mask, sel bits, ...)`; chamfer / fillet pass
0x40 and set 0x40 on all lines / arcs at the start (`set_clr_sel_bits(0x40,0,0x40,1,6)`), clear it around the creation and
call `UnSelectEptr` on both picks before `make_chamfer` / `make_fillet`; the committed result gets `|= 0x40`
(`write_ent_sel`). Line Bisect / Perpendicular / Tangent and the arcs' SelectArcToKeep select the candidates
(`write_ent_sel |= 4`, `select_ent`) while the user picks one; Point Small Arcs, Bounding Box, Trim Many and the break
commands keep sel bit 4 on their input selection until the command ends (`set_clr_sel_bits(0,4,...)`). The meaning of each
bit for drawing (selected colour or not) is not decoded; the viewer draws the picked entities selected until the command uses
them. [code / meaning unclear]

Viewer (`viewer/core_ui/cui_geo.c` `geo_live_kind`): the live commands above are implemented with a list of live entities; a
rebuild re-runs the command from the same picks (or, for Circle Center Point, Rectangle, Rectangular Shapes and Polygon, from
the first pick when a typed size now replaces the other picks - that result stays their live result: the next pick, OK and
Esc commit it, as before the change; only Arc Tangent's candidates are dropped by the next pick, 2026-11-05); a rebuild that
would need other picks is not done. Line Endpoints and Arc Polar change the live entity itself from the panel code instead
(`geo_live_panel_code`, "Line Endpoints: panel changes" and "Arc Polar" below). Not changed:
Point Dynamic (commit not found), Bounding Box and Break Many (selection + Apply here; Cancel unclear for Break Many), Offset,
Offset Chains, Project (preview colours not decoded), and the commands that commit at once.

## Lines: common pattern

Common pattern (all line commands): `sketcher_line_N` is a 3-call wrapper (ctor / Run / dtor of a
`Wireframe*App` object). The Run function creates a .NET function panel through an
`IWireframeLine*Manager` interface, then loops on `get_event()`. Panel values are copied into a small
native POD struct by a "read panel" function and written back by a "write panel" function; field
offsets below refer to that struct. Lines are created with `store_the_line(l_3d*, ...)` (entity type
line) in world coordinates after `view_to_world()` from the active construction view
(`MastercamPlanes::GetActiveViewNo`). Preview entities are created immediately (live colour,
`sktcher_live_color`) inside an undo transaction and committed (`sktcher_main_color` +
`UndoTransCommit`) on OK/Apply/next pick. "Length change" events (panel event code 4 / 7) re-scale the
already created preview line with `FUN_18018a6d0` (keeps the start point, moves the end point along
the existing direction to the new length; with flag 0 keeps the end point instead) [code].

Helper dumps for this section were produced into `decomp/mccad_lines_helpers.c` (DecompAt).

## Line Endpoints  (sketcher_line_1 @180195390 -> FUN_1801913b0)
- Manager: `IWireframeLineEndpointsManager`; saved/restored params class
  `WireframeLineEndpointsApp::SaveRestoreLastParams` (FUN_18018e3c0) -> panel remembers last values [code].
- Param struct (read FUN_1800c3e10, write FUN_1800c4ed0) [code]:
  - `+0x00/+0x01` bools encode a 3-state value from panel vf+0x78 (0 -> +0, 1 -> +1, 2 -> neither).
    Used as "which endpoint is being re-picked/edited" on the live line (0 = write pick into point 1,
    1 = point 2, 2 = normal creation) [inferred from use at FUN_1801913b0 ~line +1287: `(char)+0==0 ? p2 : p1`].
  - `+0x08` length (double), `+0x10` length locked.
  - `+0x18` angle (radians; panel shows degrees, conversion *0.0174532925.. / *57.29577951..), `+0x20` angle locked.
  - `+0x28` axis-offset value for horizontal/vertical lines (Y of a horizontal line / X of a vertical
    line, in view coords; doubled when `GetCoordinateDisplay()==2` i.e. lathe diameter display) [code FUN_1801913b0].
  - `+0x30/+0x31` from panel vf+0x48: 1 -> `+0x31` = Horizontal, 2 -> `+0x30` = Vertical, 0 -> Free form [code + inferred names: `FUN_18018f050` tests +0x31 and drives the Y/"horizontal" prompt; `FUN_18018f100` tests +0x30 and picks X].
    `+0x35` (FUN_1800c51d0) swaps the H/V sense (inverts the test) [code; meaning not resolved, likely rotated/lathe plane].
  - `+0x32` from panel vf+0x38 == 1: Multi-line. When set, after each line the end point becomes the next start point and the line is committed immediately (`bVar30` branch: `local_8490 = local_8478`) [code].
  - `+0x33` editable bool (panel ctrl [7]): enables entity picking (event mask 0x1000c90b + 0x8c instead of 0x10008909) for tangent construction (only in Free form) [code FUN_18018f700]; native init default = 1 (local_8c90 = 0x01000000) [code]. Label "Tangent" is [inferred].
- Native fallback defaults before panel read: length = `2*tol` (tol_exref), angle 0, axis value 0, Free form, multi-line off, `+0x33`=1 [code FUN_1801913b0 init block].
- Inputs: point 1, point 2 (points via autocursor). Second point computation (FUN_18018f480) [code]:
  - length & angle locked: `p2 = p1 + L*(cos a, sin a)`, z of p1.
  - Horizontal + length locked: `p2.x = p1.x ± L` (sign = side of cursor); Vertical: `p2.y = p1.y ± L`.
  - otherwise p2 = picked point. Points are view coords, then `view_to_world` -> `store_the_line`.
- Angle snapping (FUN_18018f1b0) [code]: free-form, angle not locked, Ctrl NOT held and two global
  flags set (fast_data+0x24 && fast_data+0x42): angle rounded to multiples of `fast_data+0x52f0`
  degrees (the system angle increment), length preserved; only when both points have equal Z.
- Tangent variants (picking arcs instead of points) [code, external CADUtil/core functions]:
  - point -> arc: `createLineTangentToArcEndAtPoint(arc, t, pt, len, ...)`; len = `CalcTanLength(arc,t,pt)` unless length locked.
  - arc -> arc: `createLineTangentTo2Arcs(arc1, arc2, pick, out, 1)`.
  - arc with angle+length locked: `createLineTangentToArcAtAngle(arc, t, angle, len, 0, out[2], ...)`;
    if 2 solutions both are shown and the user picks one (prompt 0x1a0b, `select_ent`), the other is deleted.
  - Rubber band while dragging tangent: type 0x20 with params (t, len or 0, ±1 side) (FUN_18018fa50) /
    `tang_ent_at_angle` when angle locked.
- Output: one LINE entity per click pair (several with multi-line).

### Line Endpoints: panel changes, locks (decoded 2026-11-05)
- **Panel codes** (event 0x10000000, code at local_82c0), posted by the observers `WireframeLineEndpointsManager` registers in
  its ctor `FUN_1800bfd20` (`FUN_1800c33d0(observer, field, property, lambda)`; the lambdas' `_Do_call` -> `FUN_1800c44f0(mgr, code)`)
  [code, ghidra_cad DecompAt / VtableDump]:
  - Length (`param_1[9]`) `Value` -> **10** (`FUN_1800c24a0`); `IsHardLocked` -> **0xb** (`FUN_1800c2420`: when the hard lock is
    now off, the main frame gets the focus first, Ordinal_12994);
  - Angle (`param_1[0xb]`) `Value` -> **0xc** (`FUN_1800c24b0`); `IsHardLocked` -> **0xd** (`FUN_1800c2300`, same focus rule);
  - AxisOffset (`param_1[0xe]`) `Value` -> **0xe**; Tangent (`param_1[7]`) `Value` -> **6**; CurrentlyEditing -> **7** / **8** / **9**
    (endpoint 1 / 2 / neither, `FUN_1800c24c0`); the two other observers post **5** and **4** (EntityType / EntityMode by the
    order of the panel's commands [inferred]).
  - The soft lock (`IsSoftLocked`) has no observer: it posts nothing.
- **Soft lock.** The EvaluatorBox style sets `ValueLockBehavior.HasLocks = HasLock` and binds `ValueLockBehavior.IsSoftLocked`
  (cncsoftware.controls.dll themes/evaluatorbox.baml Setter_20 / _21); `ValueLockBehavior.OnGotKeyboardFocus` sets
  `IsSoftLocked = true` (cncsoftware.controls.dll, ilspycmd). So clicking into the Length or Angle box soft-locks it (the lock
  image shows `IsLocked` = the hard lock only). `FunctionPanelEditableDouble::GetLocked(false)` = hard (vf+0x50) || soft (vf+0x60)
  (mastercam.dll @180191fd0); `SetLocked(l, true)` = vf+0x58 (hard), `SetLocked(l, false)` = vf+0x68 (soft) (@180192fa0);
  `SetValue(v, false)` changes the field only when neither lock byte (+0x31 / +0x32) is set (@1801930f0). [code]
- **Locks in the loop.** The panel is read every event (`FUN_1800c3e10(mgr, &local_8cc0, 0)`, sketcher_line_1.c:571):
  `local_8cb0` (+0x10) = Length `GetLocked(false)`, `local_8ca0` (+0x20) = Angle `GetLocked(false)`, `local_8498` / `local_8496`
  = point 1 / point 2 picked (low byte) and picked on an entity (high byte: the tangent picks; 0x101 at :1549 / :1557).
  `bVar31` = no live line (`local_8cd8 == 0`, :574). Codes 0xb / 0xd flip the local copy of the lock (:1716-1721) and only
  repaint / re-prompt.
- **Codes 10 / 0xc** (:1943-2026): `bVar32` = (code 0xc and angle locked) or (code 10 and length locked). With a live line
  and `bVar32`: no tangent end -> `FUN_1801904e0` (:2773) then, Horizontal (`FUN_18018f050`) or Vertical (`FUN_18018f100`),
  `FUN_180190f30` (:3728) + prompt 0x1a07; a tangent end -> `FUN_180190d10` (not ported); then `FUN_18018e9c0` (the fields'
  autocomplete lists) and `FUN_180191320` (locks). Without a live line and with point 1 picked only: the rubber band again.
  - `FUN_1801904e0` with CurrentlyEditing = neither (state 2): p1 stays; L = the panel length if locked, else the line's;
    a = the panel angle if locked, else the line's (atan2, `restrict_angle(0, 2pi)`); p2 = p1 + L (cos a, sin a), z of p1;
    nothing when the line is shorter than tol. `replace_ent` on the live line; L and a written back (`FUN_1800c4ed0`:
    only into an unlocked field). With CurrentlyEditing 0 / 1 the edited end goes to the cursor point (and the other end
    follows L / a when both are locked) - the Endpoints buttons are not ported.
  - `FUN_180190f30`: both ends at the active origin + the axis offset (Y for Horizontal, X for Vertical; halved with the
    diameter coordinate display) - the axis offset is p1's Y / X when the line is made (:1405-1453).
  - `FUN_180191320` (:4503): a field whose hard lock is off gets struct lock 0; the write `FUN_1800c4ed0` then sets each soft
    lock to the struct lock (`SetLocked(l, false)`): a hard-locked field stays soft-locked too, the others are unlocked.
    Also called after every line made (`FUN_18018f480` :5003) and in `FUN_18018ff60`.
- Code 0xe (Axis Offset, :2059-2105): with the live line and H / V, `FUN_180190f30` - both ends at the active origin + the
  offset (Y for Horizontal, X for Vertical); then the autocomplete lists; no soft-lock clearing. The value is written at the
  creation of an H / V line: p1's Y / X minus the active origin, x2 for Horizontal with the diameter coordinate display
  (:1395-1453). `AxisOffset.IsEnabled` = EnableApply and EntityMode 1 / 2 (manager vf+0x30 `FUN_1800c4940`, decompiled
  2026-11-05 with ghidra_cad DecompAt). EnableApply (mgr +0x28): 0 at the start (:506) and at a commit (:1595, `FUN_18018ea40`
  :3616), 1 with the live line (:784 ...), = (CurrentlyEditing None) on codes 7 / 8 with the live line (:1937-1940).
- **Endpoints 1 / 2 (CurrentlyEditing, codes 7 / 8 / 9)** (decoded 2026-11-05; the view model from mccad.interop.dll with
  ilspycmd: `WireframeLineEndpointsManagerViewModel`): the ToggleButtons `_1` / `_2` are checked by CurrentlyEditing (PointToEdit
  One 0 / Two 1 / None 2; manager +0x80, 2 at the start, `FUN_1800bfd20`); a click sets that value, or None when it is the
  current one (`OnSetCurrentlyEditingCommand`); `EnableEndpoint1` = NumberSelected > 0, `EnableEndpoint2` = EntityType !=
  Multi-line and NumberSelected > 1. NumberSelected (+0x84, vf+0x90 `FUN_1800c4e80`) = 1 / 2 when point 1 / 2 is picked (disasm
  1801923a6-1801923bf), 0 at the OK / Apply commit (`FUN_18018ea40` :3617).
  - Code 7 / 8: `FUN_180190200` (:4299; its hidden arguments from the disassembly at 180192ad8: struct, pick flags, prompt,
    rubber-band anchor): with the live line the rubber band from the other end (code 7: the angle + pi), prompt 0x1a07 / 0x1a08.
  - Code 9: `FUN_18018fe00` (:3504) -> `FUN_18018ff60` (:3885: `FUN_180191320` soft locks, prompt 0x1a07), or with point 1
    picked and point 2 not `FUN_180190070` (the soft locks, the rubber band from point 1, prompt 0x1a08).
  - A click while editing (:1240-1466): the click point, or the rubber band's point when one lock is on or the mode is H / V;
    stored into point 1 / 2; `FUN_18018fe00` (param_8 = no live line, disasm 180191fb1) clears the soft locks; with the live line
    `FUN_1801904e0` (:2768) with CurrentlyEditing One: p1 = the point and, both locks on, p2 = p1 + L (cos a, sin a); Two: p2 =
    the point and p1 = p2 - L (cos a, sin a); the line's L / a written back; H / V: the axis offset from p1; `FUN_1801912c0`
    (CurrentlyEditing None, then code 9), EnableApply 1. Without the live line: point 1 re-picked, CurrentlyEditing None.
  - OK / Apply (`FUN_18018ea40`) commit only when not editing; OK still leaves the loop (exit commit).
  - Codes 10 / 0xc while editing: `FUN_1801904e0` moves the edited end to the last click point (local_8c18).
- **The rubber band of the second point** (:1240-1290, `FUN_18018f8c0` :2954): with one of length / angle locked (hard or soft)
  in Freeform the click is the rubber band's point - type 0x11 (the length: anchor + L toward the cursor) or 0x10 (the angle:
  decomp/rubber/rb.c :951, the projection on the ray; no point -> the click is not taken); both locked: the click itself
  (`FUN_18018f480` computes p2); H / V: types 3 / 4.
- Tangent ends (`local_8498` / `local_8496` high bytes) -> `FUN_180190d10` (:2002, :1585): NOT PORTED - the Tangent mode itself
  (the arc picks, mccore `createLineTangentToArcEndAtPoint` / `createLineTangentTo2Arcs` / `createLineTangentToArcAtAngle`) is
  not ported, so there is no tangent end to rebuild.
- Viewer: lk_* bit 1 = the hard lock (the lock box), bit 2 = the soft lock (the Qt panel sets it when a lockable box gets the
  focus - `geo_soft_lock_cmd`, "Soft locks per command" below); a nonzero lk_* is "locked". `geo_live_panel_code` handles
  codes 7 / 8 / 9 / 10 / 0xc / 0xe as above (the changed field = the value that differs from the one the live line was made /
  last changed with); `geo_line_edit_click` the edit clicks; `geo_line_rb` the rubber band points; `geo_line_rb_seg` the rubber
  band drawn (the overlay; the colour is ours, the GrTemp drawing is not decoded) and dumped (`georb:x,y`). The lathe diameter
  display (GetCoordinateDisplay 2) has its factors in `geo_axoff_note` / `geo_axoff_world`, but the app has no lathe display
  (0, as cui_draft_pt.c). Scenes tools/tests/wf_scenes.txt `line_live_*`, `soft_line_*`.

### Soft locks per command (decoded 2026-11-05)
A lockable size soft-locks when it gets the keyboard focus, so a typed value is used at once; the lock button is the hard lock.
WPF panels: `ValueLockBehavior.OnGotKeyboardFocus` (above). MFC dialogs (Rectangular Shapes, Polygon, Ellipse, the Simple
surfaces): every lockable size is a `CSkRLCombo` (mastercam.dll, dumped read-only from its Ghidra project): CBN_SETFOCUS -> `onSetFocus`
@1804839c0 -> `setLock(1)` (the current lock +0xfc0 and the dialog's lock byte; the hard lock +0xfc1 untouched); the lock
button `onBnClick` @180483820 toggles the hard lock; `resetLock` @1804841b0 clears a lock whose hard byte is 0. Typing posts
nothing; leaving the combo with a changed value posts the dialog's size code (CBN_KILLFOCUS); the focus alone posts nothing
(the viewer's change test `geo_param_hash` leaves the soft locks out). Each command reads its locks with GetLocked(false) /
the current lock (hard or soft); where it clears the soft locks (decomp/mccad_cmds unless noted):

| Command | Cleared | Panel code with a pick pending |
|---|---|---|
| Line Endpoints | made, rebuilt, code 9, an edit click: `FUN_180191320` (hard -> soft too, else off) | 10 / 0xc: the rubber band only |
| Arc Polar | made, rebuilt: `FUN_1801af9c0` | `FUN_1801af0b0` / `FUN_1801aeed0` (above) |
| Circle Center Point | made (sketcher_arc_2.c :350), codes 5 / 6 rebuild (:528): `FUN_1801bef70`; the next pick (:319), codes 0 / 1 (:583): `FUN_1801bf000` - only when neither radius nor diameter is hard-locked | codes 5 / 6 with radius or diameter locked: `createCircleRadiusPt` at once (:439-475) |
| Arc Endpoints | made (sketcher_arc_4.c :423), make failed (:435), codes 8 / 9 rebuild (:651), the next first pick (:367), OK / Apply (:731): no hard test | codes 8 / 9 with two endpoints and the radius locked: `FUN_1801a7330` - the createArcEndpts candidates (below) |
| Arc Tangent | every creation (sketcher_arc_6.c :466 / :516 / :566 / :596 / :639 / :672): `FUN_1801b4af0`; the loop never tests the locks | - |
| Circle Edge Point | each live circle and the code 7 / 8 rebuild: `FUN_1801c5080` (sketcher_arc_1.c :3316), no hard lock only; the sizes matter only to the tangent modes (greyed here) | - |
| Rectangle | the commit `FUN_1801e34a0` only (sketcher_rect.c :600 next corner, :815 codes 0 / 1): `FUN_1801e3c30`, no test | codes 8 / 9 with the first corner, both locked and nonzero: made at once (:753-803) |
| Line Parallel | made / rebuilt (sketcher_line_5.c :1116): `FUN_18022a930`, when the hard lock is off | - (a line made at the distance keeps it in the other codes' rebuilds, :1062-1083) |
| Line Perpendicular | code 4 rebuild only (sketcher_line_4.c :425 `FUN_180203210`); after a line is made the write is with 1 (:866-870, the hard lock only): the soft lock stays | - |
| Line Bisect | :369 / :428 made, :517 / :566 rebuilt: `FUN_1801cbac0` (the lock is never read) | - |
| Line Tangent Through Point | :348 / :512 made, :622 rebuilt: `FUN_1802725c0` | - |
| Point Segment | after every build (sketcher_point_4.c :495): `FUN_18020b1d0` (the locks unused by the build here) | - |
| Point Small Arcs | never (`FUN_1801d91f0` :545 zeroes the struct byte, no write) | - |
| Polygon | made (create_polygon.c :282), 0x69 rebuild (:191), Apply 0x6a (:209): `FUN_18007ee10` | 0x69 with the centre and lock and r > tol: made (:187-200, :265) |
| Rectangular Shapes | base click (create_rectangle.c :417 / :397), made (:446 / :310), 0x67 / 0x6d (:262): `FUN_1800829e0` | 0x6a with the base and both done: made (:198-215, :292-314) |
| Ellipse | the centre click (:211), Apply 0x6b (:302) | 0x6a: done raised, made when both are (above) |
| Block / Cylinder / Sphere / Cone / Torus | the base click and Apply (decomp/surfcreate/prims.c `FUN_180013430` :2301 / :2510, `FUN_18002a820` :3255 / :3398, `FUN_18000bf90` :1568 / :1670, `FUN_180033af0` :4574 / :4700, `FUN_18002ee70` :3929 / :4053) | the dialog change: done |= lock and value; the base and every size done -> the preview |

Simple base click: the done flags from the struct read before resetLock; nothing live and everything known -> the preview built
and made at once (prims.c :3207-3239: FUN_180028b60 then FUN_1800270e0), scene `soft_cyl_once`; else resetLock, the struct read
again: only hard-locked sizes skip their picks. Block's lock buttons (0x2f3 / 0x2f5 / 0x2e8) run FUN_18000f6b0 (every size's lock
byte = its hard lock: a soft lock dropped) and post 0x6e: that size's done flag = lock and |value| > tol (prims.c :2544-2570,
:2377-2389), scene `soft_block_lockbtn` (ours: a size already picked keeps its pick). The MFC lock button itself (onBnClick
@180483820): a soft-locked box becomes hard-locked, a hard-locked one fully unlocked (`geo_lock_button`), scene
`soft_ellipse_unlock`. Values zeroed at a commit: Circle Center Point `FUN_1801bf000` (no hard lock) and Arc Endpoints
`FUN_1801a7900` write a zero radius with SetValue(v, false), which changes an unlocked box only - scenes `soft_circle_zero`,
`soft_aend_zero` (the port's circle never writes a picked radius into the box: the box only holds typed values).
Arc Endpoints' candidates (decoded 2026-11-05, sketcher_arc_4.c, all mccad): the radius locked and > tol, after the second endpoint
(:382-400) or on codes 8 / 9 with both endpoints (:664-665): createArcEndpts (:3072-3147) - findArcCenters / aa_ints give c0 (left
of p1 -> p2) and c1; FUN_18017bc70 (decomp/mccad_arctan_helpers.c:398-557) stores for each centre the CCW arc p1 -> p2 and its
complement p2 -> p1 (slots [0] c0 minor, [1] c0 major, [2] c1 major, [3] c1 minor; only when |sweep| r >= tol) in the main colour
with selection bit 4; |p1p2| - 2r > tol -> "Cannot create the arc" (5793). SelectArcToKeep (decomp/mccad_arctan_helpers.c:12-135,
prompt 5784 "Select an arc") keeps the arc picked, delete_ent on the others; the kept one goes live. Viewer: `geo_cand_show`
(the same SelectArcToKeep model as Arc Tangent: the click keeps the candidate nearest to it; Arc Tangent's unchosen candidates
are now deleted too - they were committed with the chosen arc before), scenes `soft_aend_cand`, `soft_aend_cand_typed`.
Viewer (`viewer/core_ui/cui_geo.c`): `geo_soft_lock_cmd` (the Qt panel's FocusIn), `geo_soft_clear_at` (made / rebuilt / next pick /
Apply / first pick), `geo_soft_strip` at the command's end (the managers' SaveRestore reads GetLocked(true)), `geo_pending_update`
(a typed value completing pending picks), `geo_sz_done` / `geo_sz_picked` (Ellipse, Simple), LV_VALUES (Ellipse / Simple rebuild
from the base point and the values), `geo_lock_button` (the lock buttons). Scenes `soft_*` (wf), `soft_cyl_radius`, `soft_cyl_once`,
`soft_block_lockbtn` (srf). Point Dynamic's Along box: "## Point Dynamic".

## Line Parallel  (sketcher_line_5 @18022d1f0 -> FUN_18022a9b0)
- Param struct (read FUN_1800daf60, write FUN_1800db530) [code]:
  - `+0x00` bool (panel bool ctrl [9]): when set, after the line pick the prompt becomes 0x164d and an
    ARC is picked -> tangent method. => Method: 0 = Point, 1 = Tangent [names inferred].
  - `+0x04` int from panel vf+0x68: 0, 1, 2. 2 = create a second line on the other side as well
    ("Both sides"); 1 flips side (point arg swapped / `createLineParallelAndTanToArc(..., side != 1, ...)`) [code; names Side1/Side2/Both inferred].
  - `+0x08` offset distance, `+0x11` distance locked; `+0x10` = (panel vf+0x40 == 0) [code; meaning not resolved].
  - Native init distance 0.0 before panel read [code].
- Inputs: pick a LINE (entity type 2), then either a point (mask 0x10008009, prompts 0x164a/0x164c) or,
  in tangent mode, an ARC (type 4).
- Geometry (external functions) [code]:
  - through point (distance not locked): `createLineParallelThroughPoint(line, pt)`;
  - distance locked: `createLineParallelSideOffsetDist(line, side_pt, dist)` (side from cursor point);
  - tangent: `createLineParallelAndTanToArc(line, arc, pick_pt_on_arc, side_flag, out, &dist_out)`;
  - Both sides: the same call repeated with the mirrored point / opposite flag -> second line.
- Output: 1 or 2 LINE entities, same length as the original (core functions; length rule [inferred]).
- Live preview: two temp lines (GrTemp_AddLinePtr, dashed style 1, colour 15 or 8 on white bg) [code].

## Line Perpendicular  (sketcher_line_4 @1802058f0 -> FUN_180203280)
- Param struct (read FUN_1800d3ef0, write FUN_1800d4480) [code]:
  - `+0x19` bool (panel bool ctrl [0xe]), `+0x18` = !`+0x19`: Method. `+0x19`==0 -> second input is a
    POINT (prompt 0x165b, mask 0x10008109); `+0x19`!=0 -> Tangent: if first entity is a line the second
    pick must be an ARC (prompt 0x165d) and vice versa (0x165f); splines (type 8/0x80) always use a point.
  - `+0x20` length, `+0x2c` length locked.
  - `+0x28` int 0/1/2 (panel vf+0x68) handled by FUN_180204d10 [code]:
    0 = single line (second copy deleted), 1 = flip: line reflected through its base point
    (p_end' = 2*p_base - p_end), 2 = both: an additional line from the base point in the opposite direction.
    Names "Side 1 / Side 2 / Both" [inferred].
- Inputs: entity (line, arc, spline; mask 0x1000c00a), then point or tangent arc/line.
- Geometry [code FUN_180202210 / FUN_180202670]:
  - Point method: `calcLinePerpToAnEntity(ent, t, pt, len_or_0, out, 1, 0, ...)`. If length not locked
    the line goes from the point to the foot on the entity (length = that distance and is written back to
    the panel). Foot computation: line -> `proj_pt_ln` in view 2D (or 3D if `constr_view_3d`), arc ->
    `proj_pt_ar` choosing the nearer of the two radial intersections (closest t to pick), spline ->
    `proj_pt_on_ent`. Z set to construction depth (+active origin). Fallback length 1.0 if projection fails.
  - Tangent method: `CalcLinePerpAndTanToArc(line, arc, &len, !locked, out1, out2)` -> two candidates
    (tangent on either side of the arc). Unless the arc is a full circle (|sweep| >= 2π-1e-6) the candidate
    whose endpoint lies on the actual arc span nearest the pick is shown; both may be shown (GrTemp 0xc8/0xd0).
- Output: LINE(s) (`store_the_line`).

## Line Closest  (create_a_line_Closest @180007d90)
- No function panel. Two `get_event` entity picks (prompt string 0x1663, masks 0x4002 / 0x40008e) [code].
- Calls `createLineClosest(ent1_eptr, ent2_eptr)` inside an undo transaction [code]; creates the
  shortest (minimum-distance) LINE between the two entities [inferred from name; core function not dumped].
- FUN_180007f90 is only a progress-percent status-bar updater ("%s...%.1f %%" every 20 steps) [code].

## Line Bisect  (sketcher_line_3 @1801cde80 -> FUN_1801cbb30)
- Manager `IWireframeLineBisectManager`. Param struct (read FUN_1800cde20) [code]:
  `+0x00` bool (panel bool ctrl @+0x30), `+0x01` = !`+0x00`; `+0x02` length locked; `+0x08` length.
  Native init: `+0x00` = 1, length = 1.0 [code: `local_c78 = 1; local_c70 = 1.0`].
- Inputs: two LINES (states 0 -> 1 -> 2) [code].
- Geometry: `createLineBisect(line1, line2, length, pick_pts, out[4])` (external) returns up to 4
  candidate bisector lines (both bisectors x both directions from the intersection) [code FUN_1801cd170; "4 directions" inferred from array size].
  - If `+0x00` set and >1 candidates: auto-select. For each candidate compute a measure against
    line1/pick1 and line2/pick2 (FUN_1801ccc40, not dumped); keep the candidate whose two measures are
    equal within tol and smallest -> the bisector lying in the angle sector where the two lines were
    picked [code + interpretation inferred]. Others deleted.
  - Otherwise (or if auto fails) all candidates are shown, prompt 0x1a0a "select bisector to keep";
    the picked one stays, the other three are deleted [code].
  - Length edits rescale all candidates from their start (intersection) point (FUN_18018a6d0 flag 1).
- Output: one LINE starting at the intersection, given length.

## Line Tangent Through Point  (sketcher_line_6 @1802749d0 -> FUN_180272630)
- Manager `IWireframeLineTangentThroughPointManager`. Param struct (read FUN_1802778b0, write FUN_180277d90) [code]:
  `+0/+1` 3-state from panel vf+0x40 (re-pick state, like Line Endpoints) [inferred]; `+0x02` length locked; `+0x08` length.
  Native init length = 0.1 [code `local_3228 = 0.1`].
- Inputs: 1) entity (arc/spline; mask 0x1000c00a), 2) point ON the entity (projected with `proj_pt_on_ent`),
  3) unless length locked, a second point that sets the length (rubber band type 0x22 along tangent).
- Geometry [code FUN_180273ac0 / FUN_180273e20 / FUN_180273c00]: unit tangent T = `tan_on_ent_from_t(ent,t)` at
  the projected point P. Length locked: two lines are built, P->P+L·T and P->P−L·T; user picks which one
  to keep (prompt 0x1a0a), the other is deleted. Unlocked: line from P along ±T to the projected cursor
  distance (the length is written back to the panel). Panel "flip" (event 6) reverses direction.
- Output: one LINE starting at the tangent point.

## Line Normal  (sketcher_line_normal @18035a920 -> FUN_1803584a0, decisions N9-16 / N9-23)

Ported: viewer/mcsurf_nrm.c (eval_surf, calc_surf_norm, the projection), viewer/core_ui/cui_wfmisc.c (`ln_*`: the command), cui_geo.c (plumbing).

Panel (IWireframeLineNormalManager, mccad.ui `wireframelinenormalmanagerview.baml`, manager FUN_18035ea60) [code]: check boxes Contact point (manager [10], off) and End point ([0xc], off);
Reselect toggle (CurrentlyEditing, enabled = NumberSelected != 0); Length (manager [8], key `WireframeLineNormal-Length`, 1.0 inch / 25.0 metric; limits +-1e38 at the pick, FUN_180359a20);
Direction radios Normal / Reverse Normal / Both (+0x70 OffsetDirection 0, 1, 2, default Normal). Reader FUN_180360010: +0 reselect armed, +8 Length, +0x10 Direction, +4 point flags
(Contact only 0, both 1, End only 2, neither 3).

Loop (disassembly 180358600-180359880, the decompile drops the panel-code blocks) [code]:
- Prompt 6706 "Select a surface or face" (get_event 0x1000200a, entity mask 0x100, a solid face 0x2000). FUN_18035a670 starts the location: the pick's (u, v) for a surface; the middle of the domain for a
  solid face (l.7317-7322). No arrow yet (+0x29e0 = -1): the first event after the pick only evaluates at that (u, v).
- Prompt 6707 "Select locations to create normal lines. Press Enter to select another surface or face.": get_event 0x10008109: a mouse move (0x100) moves the location (FUN_1801c90d0 with param_6 = fast_data[9]
  (AutoCursor snapped) or the event-derived r13b), a click (1, not a keyboard point 'K') does the same with param_6 = 1, then commits the previous live line, UndoTransStart, FUN_18035a3b0 (live, sktcher_live_color),
  NumberSelected 1, Apply enabled, Reselect off.
- Panel codes: 4 Length, 6 Direction, 8 points - the live line rebuilt (LV_REBUILD); 7 Reselect - armed with a live line: UndoTransTerminate; 1 Apply (Enter: [inferred] from the prompt) - not while Reselect armed; a live line: committed,
  back to the selection (NumberSelected 0, Apply off), no live line: nothing; 0 OK / 2 Cancel / Esc - the exit commits (977c).
- FUN_1801c90d0's surface branch (shared with Point Dynamic): the delta of the view-plane point |dx|+|dy| <= tol: nothing. With the arrow: proj: project_pt_onto_surf(surf, Cplane view, allow outside, P, uv, uv', S) (not found: fallback below);
  else (or not found): v_lincoeffs(Su2d, Sv2d, delta) (Su2d = proj_iso_pt(Su): the graphics view); periodic wrap (flags +0x8e bits 1/2); a trimmed surface: in_polygon, else hold_uv_to_trim (hit_polygon: the nearest crossing); else clamp to the domain.
  Then eval_surf(2), calc_surf_unit_norm(Su, Sv) (failure: N = +Z), reversed with +0x8e bit 4 (the surface's normal flag); proj_pt_cp(S) (2D construction: the point onto the Cplane view at ConstrDepth + the active origin's view z, mastercam.dll 18033c200), arrow (GrTemp_AddArrow(S, N, 0, 0.15, -2): drawn here as a segment, its look NOT DECODED).
- The panel-code rebuilds (4, 6, 8) use the point and normal of the line made (locals saved at the click, disassembly 180358e39-180358ec1), not the arrow's current location. Code 7 armed with a live line: UndoTransTerminate and EnableApply(0) (180358d8a-180358dad). Code 1's branch at 180358fca (the stored event an entity pick of a non-surface: Terminate, build again) cannot be reached: the pick mask is 0x100; a keyboard click ('K', local_5d68) does not move the location before the line.
- FUN_18035a3b0 (P = S, v = N L): |L| <= tol: nothing; Contact point at P (+4 < 2); the Normal side ((+0x10 & ~2) == 0): End point at P + v (+4 in 1..2), line P -> P + v; the Reverse side (+0x10 in 1..2): End point at P - v, line P -> P - v.

Read line by line against the port (2026-11-09): FUN_1805ed4a0 (view >= 0 branch) and calc_surf_norm 0x1801637e0 (the limits, the cross products of both directions, the order 2 / 3 terms, the 50-step exit) agree with viewer/mcsurf_nrm.c.

Project_pt_onto_surf (mccore 0x1805edee0 -> FUN_1805ed4a0, view >= 0: Newton along the view's Z): det3(Su*, Sv*, Z), Su* = Su |Su_k| / |Su_(k-1)| (from step 1), stop: |P - S| < tol (metric: tol * 25.4) or view-plane distance < tol (found), a step larger than the domain or det < 1e-12 or 56 steps (not found), |du|+|dv| < 1e-6 after step 10 (found);
eval_surf_extrap outside the domain (clamped + first derivative). calc_surf_norm (0x1801637e0): N = Su x Sv, limit min(metric ? 1e-4 : 4e-6, size^2) (size = ent +0xd8); |N| > limit: N stands; else (a pole / degenerate point) steps of 1 / 100 of the range along the longer-second-derivative direction, with the order 2 / 3 Taylor correction of the normal (l.158-284).

Surface kinds: eval_surf order <= 3 for NURBS (kind 0, FUN_1805b1f10), parametric (kind 1, FUN_1805ec000), revolved (kind 2, eval_revsurf) and trimmed on them; order <= 1 for draft (6, FUN_180412490), arc sweep (7, FUN_18041c730 / FUN_18041ced0: its Su is that function's, the blend of the rotated end derivatives, not the true derivative), offset (8, FUN_180460cd0: base order 2, Weingarten); kind 4 (lofted): none in the 1359 sample parts (0 of 55 227 surfaces; 32119 kind 0, 2217 kind 1, 502 kind 2, 23 kind 6, 28 kind 7, 19 kind 8, 20319 kind 10): NOT DECODED, the pick says so. A degenerate point (poles) on kinds 6 / 7 / 8 asks for order 2 / 3 there: NOT DECODED, the location refuses with a message.

Solid faces (N9-23, OCCT stands in for Parasolid; ln_start_face): the face as a NURBS (BRepBuilderAPI_NurbsConvert) and its trim loops (occt_xt_faces / occt_brep_faces, mcsb_prim_nurbs / loops / trim as From Solid), a temporary surface. Face picks: sld_pick_part_face (the part's solids F.solids; faces of solids made in the session are refused NOT PORTED).
Where a solid-face result can differ from Mastercam's Parasolid one: (1) the (u, v) start position of the arrow = the middle of OCCT's domain (only the preview's first position: the line is made at a click); (2) the NURBS of a face: exact for analytic surfaces, for the XT b-surface / offset / swept faces OCCT's conversion replaces Parasolid's fit (a position / normal difference within the conversion tolerance); (3) the trim polygon (the loops are OCCT's linearization of the edges): a click near a face boundary can fall on the other side; (4) the face order for the pick (ours: the mesh triangles' face). The normal direction and its sense come from the same flag (+0x8e bit 4, face sense reversed).
The projection direction: the Cplane's Z (GetActiveViewNo(Planes, 0)), per the code; in a non-Top Cplane the first click's Top-view seeds (ours, on `ln_seed`) use the graphics view.

Checks: real mccore.dll under tools/peloader (`--lnorm`, tools/lnorm_cmp.py, cases from the app hook lnoracle:PREFIX): eval_surf order 3, calc_surf_unit_norm, project_pt_onto_surf in view 1 on kinds 0 and 1 of 7 parts + Surfaces-tab sphere and cylinder: (8 runs, 2026-11-09: PARALLELSTEEP, Surfaces-tab sphere and cylinder, DRILL 5 AXIS, TURBO_IMPELLER, CARTEMP, SURFACING_11, TUTORIAL #7): calc_surf_unit_norm 2400 of 2400 agree (max difference 2.3e-12, poles of the sphere included), eval_surf order 3 2369 of 2400 lines (the rest: order-3 values of rational NURBS, |difference| <= 1.1e-4 on values up to the order of 1e4), project_pt_onto_surf 854 of 864 agree to 2e-14, the 10 others (surfaces 7 and 8 of DRILL 5 AXIS) differ by 1.4e-5 in (u, v) and <= 3.5e-4 in S: there Su and Sv are parallel in the Cplane view to 4 digits (a nearly vertical face under Z), the Newton determinant is ill-conditioned and both answers are within the view-plane tol (5e-5) of the point. Kinds 2, 6, 7, 8 were not run under the DLL (their eval needs get_ent_from_eptr / the curve records in memory); checked instead against central differences of their positions (test hook lnfd, relative to |Su| / |Sv|, 6 parts): kind 2 <= 1.5e-7 (264 surfaces), kind 6 <= 5e-9 (18), kind 7 <= 4.3e-10 (4), kind 8 <= 3.9e-6 (3: mcsurf.c eval_offset's own central differences are the limit). Hand-worked: sphere r 10 at (6, 0, 8): S (5.999997392, 0, 8.000001956) (the projection stops within tol), N (0.599999739, 0, 0.800000196) (exact (0.6, 0, 0.8)), scene lnorm_sphere; cylinder r 12 at the wall (15, 4, 12.5): N (1, 0, 0), lnorm_cyl; Top view on a vertical wall: the location cannot move (the Newton det is 0 and Sv projects to a point: the decoded fallback gives no step) - lnorm_cyl stays at the pick.

Not ported (preview only, no effect on the line): the mouse-move re-entry state machine (r13b / cVar6, the location taken from the stored event when the cursor leaves the surface and comes back; find_an_entity); the arrow's drawing (GrTemp).

## Arcs: common pattern

- Each `sketcher_arc_N` export builds a `Wireframe*App` object, runs its main loop, then destroys it. The .NET panel is an `IWireframe*Manager` (in mccad.ui.dll), found via RTTI in `FUN_1800b33f0`. A per-command "panel -> struct" reader copies the panel fields into a native struct before each geometry step. **[code]**
- Default field values live in the .NET panel. The native side only seeds the struct, and the panel reader then overwrites it. Seeds are listed below where they are non-zero. **[code]**
- Angles in the panel are degrees. The readers multiply them by `0.01745329251994329` to get radians. Diameter is always `2*radius` (`FUN_1801c0180`, `FUN_1801214c0`). **[code]**
- The final arc is created by Mastercam core helpers imported by name: `createCircleRadiusPt`, `createCircle2pt`, `createCircle3pt`, `createArc3pt`, `createArcEndpts`, `createArcPolar`, `createArc`, `createArcTangentToAnEntity`, `createArcTangentThruPoint`, `createArcTangentUsingCenterLine`, `createArcTangentDynamnic` (sic), `createArcTangentTo3Eptrs`, `createArcTangentTo3Entities`, `CreateArcTangentTwoEntities`, and `make_fillet`. Their bodies are outside mccad.dll. The output is always one arc entity (full circle = sweep 2*pi). No splines are produced. **[code]**
- When a constructor returns several candidate arcs, `FUN_1801997f0` calls `SelectArcToKeep(list, prompt 0x1698, 4, &idx, -1, wnd)`, and the user clicks the arc to keep. The candidate list is a CArray filled by the creator. **[code]**
- Created arcs stay "live" (editable from the panel) until the next pick or OK. Re-editing a live arc deletes it and recreates it (`delete_ent` + create). **[code]**
- Tooltip text in mccad.ui.dll gives the UI names. You can read it with `strings -n 25 ~/mastercam-re/files/mccad.ui.dll`. **[code: resource strings]**
- Extra helpers decompiled for this section: `decomp/mccad_arcs_helpers.c`.

## Circle Center Point (sketcher_arc_2 @1801c0c70 -> FUN_1801bf090; panel IWireframeCircleCenterpointManager)

- Panel struct (reader `FUN_180118ca0`) **[code]**:
  - +0 bool (panel mode==0)
  - +8 radius, +0x10 radius locked
  - +0x18 diameter, +0x20 diameter locked
  - +0x21 Tangent toggle
- Native seed: radius 0. There is no native default; the radius comes from the panel. **[code]**
- Click 1 sets the center.
  - If radius or diameter is locked (`local_5b8`/`local_5a8`), `createCircleRadiusPt(center, radius)` makes the circle immediately.
  - Otherwise a circle rubber band (`init_rubber_band(8, center)`) runs, and click 2 sets the radius to the distance from the center to the cursor (`rubber_band+0x88`). **[code]**
- **Tangent** (+0x21): this enables entity or solid-edge picking (event mask 0x1000c10a, fill-drag 0x4002). `FUN_1801c0200` projects the center onto the picked curve in its own view plane (`proj_pt_gt`) and sets radius = 2D distance from the center to that foot point. The result is a full circle centered at the click and tangent to the entity. **[code]**
- Output: one full circle (arc entity, 360 degrees). **[code]**

## Arc 3 Points (sketcher_arc_5 @1801baca0 -> FUN_1801b90b0; IWireframeArcThreePointsManager)

- Panel struct (reader `FUN_1800fd350`) **[code]**:
  - bytes 0..2 = which point (1/2/3) is being reselected (panel buttons "1", "2", "3")
  - byte 3 = **Tangent** toggle (UI: toggle between Point and Tangent before each pick)
- Three picks; each one is either a point (event 1) or an entity (event 2/0x4000). Handled by `FUN_1801b88d0`, `FUN_1801b8a30`, `FUN_1801b8b60` (one per pick). **[code]**
- The arc is built after pick 3 in `FUN_1801b8760`. **[code]**
  - No entity among the picks: `createArc3pt(p1, p2, p3)`, an arc from p1 through p2 to p3.
  - At least one entity: each slot is either a picked entity or a point stored as a type-1 (point) ent. `createArcTangentTo3Entities(ents[3], pickpos[3])` then gives an arc tangent to the entities and passing through the point slots.
- Output: an arc, not a full circle. **[code]**

## Arc Tangent (sketcher_arc_6 @1801b6f10 -> FUN_1801b4b60; IWireframeArcTangentManager)

- Panel struct (reader `FUN_1801129c0`) **[code]**:
  - +0 int mode
  - +8 radius, +0x10 radius locked
  - +0x18 diameter, +0x20 diameter locked
- Native seeds **[code]**:
  - inch: radius `0x3ff0...` = 1.0, diameter `0x4000...` = 2.0
  - metric: radius `0x4039...` = 25.0, diameter `0x4049...` = 50.0
  - The panel overwrites these, but they are probably the panel defaults too. **[inferred]**

Modes (enum read from code; names from the .NET tooltip list, which uses the same order):

| Mode | UI name | Picks | Geometry (code) |
|---|---|---|---|
| 0 | Arc one entity | 1 entity, then 1 point | `createArcTangentToAnEntity(ent, radius, pt, list, idx)`. Gives several candidates; user picks one via `SelectArcToKeep`. In Mastercam this gives 180-degree arcs tangent at the point. **[inferred from UI]** |
| 1 | Arc one point | 1 entity, then 1 point | `createArcTangentThruPoint(ent, pt, radius, list, idx)`: tangent to the entity and through the point. Choose among candidates. |
| 2 | Arc centerline | 2 lines (tangent line, then centerline) | `createArcTangentUsingCenterLine(e1, e2, radius, list, idx)`: center lies on the 2nd line. Choose among candidates. Selection mask = lines only (`FUN_1801b6540`: mask 2, prompt 0x16b4). |
| 3 | Arc dynamic | 1 entity, then cursor point | `createArcTangentDynamnic(pt_on_entity, cursor_pt, ...)`. Updates while dragging once the cursor is more than 2*tol away. |
| 4 | Arc three entities | 3 entities | `createArcTangentTo3Eptrs(e1, e2, e3, pickpos)` gives an arc. |
| 5 | Circle three entities | 3 entities | Same as mode 4, then sets sweep to 2*pi (`0x401921fb54442d18`) via `arc_ep_3d` + `replace_ent`, giving a full circle. |
| 6 | Arc two entities | 2 entities | `set_fil_trim(0)`, `set_fil_dir(2)`, `set_fil_small_sweep(0)`, `set_lollipop_corner(0)`, then `make_fillet(e1, e2, radius, 0, picks, ...)`. It is a fillet arc without trimming. On failure it shows warning 0x16b2. |

- Selection masks (`FUN_1801b6540`) **[code]**: modes 0,1,4,5 use mask 6; mode 2 uses 2; modes 3 and 6 use 0x8e.
  - Probably these are bit (1<<entity type): 2 = line, 4 = arc, 0x80 = spline/NURBS. **[inferred]**

## Circle Edge Point (sketcher_arc_1 @1801c8f00 -> FUN_1801c51b0; IWireframeCircleEdgePointManager)

- Modes (UI): 2 Points, 2 Points tangent, 3 Points, 3 Points tangent.
  - `FUN_1801c7a50` maps panel mode 1->0 and 3->2, i.e. it converts the tangent variant to the plain one.
  - That suggests 0 = 2 pts, 1 = 2 pts tangent, 2 = 3 pts, 3 = 3 pts tangent. **[inferred]**
- Native init sets the step counter to 1 and selection mask 0x8e (`local_3864=1`, `local_3868=0x8e`). Radius and diameter start at 0. **[code]**
- 2 points: `createCircle2pt(p1, p2)`. The two points are diametrically opposite (center = midpoint). The rubber band is type 5. **[code + inferred meaning]**
- 3 points: `createCircle3pt(p1, p2, p3)`, a circumscribed circle. The rubber band is type 6. **[code]**
- 3 points tangent: `createArcTangentTo3Eptrs(e1, e2, e3, pick)`, then forced to a full circle (sweep 2*pi, `arc_ep_3d` / `replace_ent`). **[code]**
- 2 points tangent: `FUN_1801c7c20` calls `CreateArcTangentTwoEntities(e1, e2, &r, picks, ...)`. The radius comes from the panel; if radius <= tol it uses diameter*0.5. **[code]**
- 2 points tangent, automatic radius when none is entered (`FUN_1801c7d20`) **[code]**:
  - Two parallel lines (`l_ints` fails): r = half the distance between the lines.
  - Otherwise, d = half the distance between the two pick points evaluated on the curves.
  - It tries `make_fillet` (trim off, small sweep 2) with radii starting at 1.1*d. The step is 0.1 for inch (`0x3fb999999999999a`) or 10.0 for metric.
  - It keeps the smallest radius that succeeds. When the step overshoots it refines the step x0.1, for up to 5 refinements.
  - It stops early once a found radius is within +/-10% of d.
- Output: a full circle (Mastercam builds circles counter-clockwise; from the CHM help). **[code / help]**

## Arc Endpoints (sketcher_arc_4 @1801a9fa0 -> FUN_1801a7970; IWireframeArcEndpointsManager)

- Panel struct starts at `local_d18`: +8 radius, +0x10 radius-entered/locked flag. The reader is `FUN_180103510` (not dumped). The radius seed is 0. **[code]**
- Click 1 and click 2 are the endpoints. The UI also offers "Tangent".
- Edge point mode (+0x10 == 0): a 3rd click gives the edge point, and `createArc3pt(p1, edge, p2)` (`FUN_1801a7330`) makes an arc from p1 through the edge point to p2. The rubber band is type 0x12. **[code]**
- Radius given (+0x10 != 0): `createArcEndpts(p1, p2, radius, list, idx)` returns up to 4 candidates (minor/major on each side), and the user picks one via `SelectArcToKeep`. **[code]** (slot order and geometry: "Soft locks per command" above).
- Tangent (`FUN_1801a7010`) **[code]**:
  - Requires that one endpoint lies on the picked entity (2D distance < tol).
  - It evaluates the entity tangent at that point (`t_on_gt`, `eval_gt_tan`).
  - `cr_a_ppt(pt, tangent, other_pt)` builds the arc tangent to the entity there and ending at the other endpoint.
  - The arc's mid-parameter point (t = 0.5) is then used as the edge point.

## Arc Polar (sketcher_arc_3 @1801b2540 -> FUN_1801afae0; IWireframeArcPolarManager)

- Panel struct (reader `FUN_1801214c0`) **[code]**:
  - +0 bool, +1 tangent toggle
  - +8 radius, +0x10 radius locked, +0x18 diameter = 2r
  - +0x28 start angle (rad), +0x30 locked
  - +0x38 end angle (rad), +0x40 locked
  - +0x41 Defined/Inverted arc flag (param_1[6])
- Native seeds: radius 0.1, diameter 0.2, start 0. The panel then overwrites them. **[code]**
- UI: Defined Arc vs Inverted Arc ("reverses the start point and end angle"); start/end angle; "same start and end angle = full circle". **[UI strings]**
- Picks **[code]**:
  - Click 1 = center.
  - Click 2 = start point: radius = distance from the center, start angle = atan2 in view coords, normalised to [0, 2*pi) with `restrict_angle`.
  - Click 3 = end point: end angle = atan2.
- Locked values skip their click: if radius and start angle are locked, the start point = center + r*(cos a0, sin a0) (`FUN_1801ae880`).
- Create: `createArcPolar(center, r, startPt, endPt, cwFlag)`. **[code]**
  - When everything is typed in, cwFlag = (end - start < 0).
  - When dragging, cwFlag = (rubber-band sweep < 0) (`local_7e6`).
- Tangent: when picking the start point on an entity (`FUN_1801b1800`), the point is projected onto the entity. For an arc entity (gt type 0x41) it takes the near side relative to the arc center; the radius is then the distance from the center. **[code]**
- Panel changes with the live arc and no pick pending (`local_778 == 0`, sketcher_arc_3.c:722-836): codes 6 / 7 when the radius
  lock (+0x10 / +0x20) is on, 8 when the start-angle lock (+0x30) is on, 9 when the end-angle lock (+0x40) is on ->
  `FUN_1801b1a50` (:2137): the centre stays; radius = the panel's if locked; with a start or end lock: end = the panel end angle
  if locked else the arc's end, start = the panel start angle if locked, sweep = end - start, `restrict_angle(0, 2pi)`
  (`(-2pi, 0)` for a clockwise arc); `replace_ent`. Then `FUN_1801ae800` (autocomplete) and `FUN_1801af9c0` (:1174): every
  struct lock 0 and the write `FUN_180121a50` clears the soft locks (the hard ones stay); the same after an arc is made
  (`FUN_1801ae880` :3203). With a pick pending (`local_778` = the picks: 1 centre, 2 start point) the codes give the rubber
  band (`FUN_1801af4b0`) or: codes 6 / 7 with the radius locked and > tol and the start known (the start angle locked or
  `local_778` >= 2), code 8 with the start angle locked and the radius known (locked and > tol, or `local_778` >= 2) ->
  `FUN_1801af0b0` (:1578): the end angle locked -> the arc (`FUN_1801ae880`), else state 2 (prompt 0x168b, the end point next);
  code 9 with the end angle locked and (`local_778` > 1 or radius + start angle locked) -> `FUN_1801aeed0` (:1209): the arc.
  **[code]** Viewer: `geo_live_panel_code` (scene `apolar_live`); the codes' fields are read from the lock each one tests;
  pending: `geo_pending_update` with `geo_steps` (a start point once taken stays in the steps: `geo_apolar_st`), scene
  `soft_apolar_pending` (2026-11-05).

## Arc Polar Endpoints (sketcher_arc_7 @1801acfe0 -> FUN_1801abb30; IWireframeArcPolarEndpointsManager)

- Panel struct (reader `FUN_18010c370`) **[code]**:
  - +0 bool
  - +1 "Start point" (1) / "End point" (0) radio; native seed 0x100 means Start point
  - +8 radius, +0x10 diameter
  - +0x18 start angle (rad), +0x20 end angle (rad)
- The prompt is 0x1692 or 0x1693 depending on that radio. **[code]**
- One click gives P; the panel gives r, a0 (start) and a1 (end). **[code]**
  - sweep = a1 - a0, wrapped into (1e-6, 2*pi] by `restrict_angle(1e-6, 2pi)`.
  - Start-point mode: center = P - r*(cos a0, sin a0).
  - End-point mode: center = P - r*(cos a1, sin a1).
  - Then `createArc(center2d, r, a0, sweep, z=P.z)`.
- The arc always runs counter-clockwise from a0. **[code]**
- The arc is recreated live while the radius is > tol. **[code]**

## Rectangle (sketcher_rect @1801e5d50 -> FUN_1801e3ca0)

- UI is a .NET function panel (`IWireframeRectangleManager`), read into a small struct by FUN_1800e1a40 [code]:
  `+0 bool mode==0, +1 bool mode==1, +8 width, +0x10 widthLocked, +0x18 height, +0x20 heightLocked, +0x21 anchorToCenter, +0x22 createSurface`.
- Width/height start at 0.0 (locals cleared in FUN_1801e3ca0) [code]; panel-side defaults are in .NET (unknown).
- FUN_1801e37f0 pushes it to the MCCore rectangle globals [code]: `rotate_rect=0, rect_rotate_angle=0, make_rect_fillets=0, rect_fillet_rad=0, rect_type=0 (plain rectangle), make_rect_center_pt=0, make_rect_surfs=createSurface (forced 0 if GetProdEnableBits()&2), rect_surf_type = cfg_createNurbs?2:1, onept_rect_type = anchorToCenter ? 5 : 1, rect_width/height`. So the simple Rectangle has no fillets/rotation/center point.
- Inputs [code]: two clicks = opposite corners, signed width/height via `rect_width_and_height_two_way` (mccore @180270f10: w=x2-x1, h=y2-y1 in view coords; if anchor==5 then w=2|dx|, h=2|dy|, i.e. 1st click is the centre). If both width and height are locked (>tol) one click places it (`create_rectangle1(base)`), otherwise rubber band; two-point path calls `create_rectangle2(p1,p2,...)`.
- Output: 4 lines (rect_type 0, see Rectangular Shapes) + optional planar surface [code].

## Rectangular Shapes (create_rectangle @1800830d0; dialog CRectShapes FUN_18007fad0, res 0x190e/0x190c)

- Param struct (DAT_1806a46a0, persistent defaults at DAT_18064c670, all zero except anchor) [code: FUN_1800827e0/FUN_180082d60, static data read from mccad.dll]:

  | off | field | default |
  |---|---|---|
  | +0x00 bool | 2-point mode (radio 0x1550==1); false = 1-point/base-point mode | 0 (1-point) |
  | +0x04 int | `rect_type` 0..3 (clamped, >3 -> 0) | 0 |
  | +0x08 double | corner fillet radius (`make_rect_fillets` iff > tol) | 0 |
  | +0x10 double | rotation angle deg (UI range -360..360) | 0 |
  | +0x18 byte | create surface | 0 |
  | +0x19 byte | create centre point | 0 |
  | +0x20/+0x28 | width / width lock | 0 / 0 |
  | +0x30/+0x38 | height / height lock | 0 / 0 |
  | +0x3c int | `onept_rect_type` anchor 1..9 (dialog stores value-1) | 1 |

- FUN_180082440 copies it into MCCore globals `rotate_rect, rect_rotate_angle, make_rect_fillets, rect_fillet_rad, rect_type, make_rect_surfs, rect_surf_type(cfg_createNurbs?2:1), make_rect_center_pt, onept_rect_type, rect_width, rect_height` [code].
- `rect_type` dispatch in mccore FUN_180270510 [code]: 0 -> FUN_180272040, 1 -> FUN_180271b50, 2 -> FUN_1802720d0, 3 -> FUN_1802713f0 (4 = unused). Geometry [code, names inferred from geometry]:
  - 0 Rectangle: 4 lines through 4 corners.
  - 1 Obround: 2 lines + 2 half-circle arcs, arc radius = h/2, arc centres at cx +- (w/2 - h/2); FUN_180272530 flag rotates the construction 90deg (inferred: when h > w, so radius = min/2).
  - 2 Single D: one flat line at y = cy - h/2 + one arc of radius w/2 whose top is at cy + h/2; three cases h>w/2, h==w/2 (180deg semicircle), h<w/2 computed with asin/sqrt.
  - 3 Double D: circle radius w/2 cut by two flats at cy +- h/2: 2 lines + 2 arcs (chord half-length sqrt((w/2)^2-(h/2)^2)).
  - Rotation: all points rotated by `rect_rotate_angle` about the shape centre (rotate_pt) [code].
- Anchor (`onept_rect_type`) -> offset centre-from-base point, mccore FUN_180270fc0 [code, case fall-through partly lost in decompile -> grid layout inferred]: 1..9 = 3x3 grid, column = (n-1)/3 (left/centre/right), row = (n-1)%3 (bottom/middle/top); 5 = centre. e.g. 1: centre = base + (w/2, h/2) (base is lower-left), 9: base + (-w/2,-h/2), 5: base. Rotation applied to the offset unless anchor==5. Lathe coordinate display flips X/Y signs per view 4/0x11/0x12.
- Inputs [code]: 1-point mode: click base point; if width or height unlocked, rubber band then second click sets size via `rect_width_and_height_from_1pt` (cursor delta rotated by -angle; for left/right anchors size = +-dx, centre column 2|dx|). 2-point mode: two clicks -> `create_rectangle2`: centre = midpoint, diagonal length & angle -> width/height.
- Fillets (FUN_18026fce0) [code]: chain-fillet all corners of the closed chain with `rect_fillet_rad`, only if radius < |w|/2 - tol and < |h|/2 - tol; otherwise warning 0x16d1/0x16d2 and no fillets (create_rectangle2).
- Centre point: point entity at shape centre (FUN_180270110) [code]. Surface: `create_nurbsurf_from_4_points` (rect_surf_type 2, NURBS) or FUN_1804284b0 (type 1) then trimmed to the boundary if shape is not a plain rectangle or has fillets (FUN_1802702b0) [code].

## Polygon (create_polygon @18007f1f0; dialog CPolygonDlg FUN_18007cde0, res 0x191a/0x191b)

- `polygonOp_params_tag` (DAT_18064c3c0 static defaults) [code: FUN_18007ec90/FUN_18007f030 + data]:

  | off | field | default |
  |---|---|---|
  | +0x00 int | number of sides (UI range 3..360) | 6 |
  | +0x08 double | radius | 0 (user drags) |
  | +0x10 byte | radius locked | 0 |
  | +0x18 double | corner fillet radius | 0 |
  | +0x20 double | rotation angle deg (range 0..360) | 0 |
  | +0x28 bool | radius measured to corner (radio index 0); false = to flat (index 1) | 0 -> **flat** |
  | +0x29 byte | create surface | 0 |
  | +0x2a byte | create centre point | 0 |
  | +0x2b byte | dialog expanded state | 0 |

- Vertex math, mccore `create_polygon_ents` @1805ea9b0 + FUN_1805ea870 [code]: step = 360/n deg; point count = n+1 (closed); circumradius R = (corner ? r : r / cos(step/2)); first vertex angle = step/2 - 90deg + rotation, then += step; points in view plane, view_to_world, offset by centre.
  => rotation 0: a flat edge is horizontal at the bottom (edge midpoint at -90deg) [derived].
- Entities, mccore FUN_1805eb0a0 [code]: n lines (consecutive vertices); if fillet radius > tol, `make_fillet` between each pair (i,i+1) and (n-1,0) with set_fil_trim(1), set_fil_dir(2), small sweep off -> n lines + n fillet arcs, interleaved.
- Inputs: click centre; radius from rubber-band drag unless locked; also a dashed construction circle of radius r is drawn (FUN_18007e7f0, temp). Optional centre point (createPoint) and surface (`make_polygon_surface`: 4-point NURBS/plane trimmed by the chain) [code].

## Ellipse (create_ellipse @18007c2a0; dialog CEllipseDlg FUN_180079970, res 0x191c/0x191d)

- Param struct (9 qwords, DAT_18064c0b0) [code: FUN_18007bb20/FUN_18007c090 + data]:

  | off | field | default |
  |---|---|---|
  | +0x00 double | radius B (Y) | 0 |
  | +0x08 byte | B locked | 0 |
  | +0x10 double | radius A (X) | 0 |
  | +0x18 byte | A locked | 0 |
  | +0x1c int | output type 0=NURBS spline, 1=arcs, 2=lines | 0 |
  | +0x20 double | arc/line conversion tolerance; set from `min_arc_length` config on every launch | min_arc_length (static 0.001) |
  | +0x28 double | start angle deg | 0 |
  | +0x30 double | end angle deg | 360 |
  | +0x38 double | rotation angle deg | 0 |
  | +0x40 byte | create surface | 0 |
  | +0x41 byte | create centre point | 0 |
  | +0x42 byte | dialog expanded | 0 |

  UI ranges: start 0..360-tol, end tol..360, rotation 0..360, tolerance chain_tol..10000 [code].
- Geometry FUN_18007b380 -> mccore `CalculateEllipse(A, B, start, end, rot, centre, ent)` @18023bfe0 [code]: `ellipse_to_nurbs(A,B,...,start,end)` builds an exact rational NURBS (rational weights, nbrat_3d_to_4d); |start|<=tol -> 0, |end|<=tol -> 360; closed flag when |end-start-360| < 1e-6 rad; control points rotated by `rot` in view plane, view_to_world, translated to centre (projected to construction plane via proj_pt_cp). Stored with put_nurbs_in_db => **one NURBS spline entity**.
- Output type 1: `break_spline_arcs(0, tol, ...)` -> arcs; type 2: `break_spline_const_error(..., tol)` -> lines; then the NURBS is deleted [code].
- Inputs: click centre; drag A (X) then B (Y) unless locked [code: rubber band 0x88/0x1b8 -> +0x10 then +0x00].
- Locks and picks (decoded 2026-11-05, create_ellipse.c; registers checked in the disassembly): the struct is read from the
  dialog every event (`FUN_18007bb20` :143). The centre click (:198-230) commits the previous ellipse, sets A done = lock A
  (+0x18) and A > tol, B done = lock B (+0x08) and B > tol - from the struct read before the resetLock `FUN_18007bcb0` (:211),
  so a soft lock counts. Then (LAB_18007cb47 :309-345): A not done -> rubber band 8 (A); else B not done -> rubber band 0xf (B);
  else `FUN_18007b2f0` makes it. A size click (:186-197) takes the size from the rubber band and sets it done (> tol). 0x6a (a
  size combo left with a changed value, CBN_KILLFOCUS FUN_18007aaf0 / FUN_18007ab60) raises the done flags (lock and > tol)
  and goes to LAB_18007cb47 - the ellipse made at once when both are known; with the live ellipse it rebuilds from the values
  (`FUN_18007b760`, no lock test). resetLock also on Apply 0x6b (:302); not after it is made. Viewer: `geo_sz_done` /
  `geo_sz_picked`, the A / B locks `lk_ra` / `lk_rb`; the picked sizes are written into the fields; scenes
  `soft_ellipse_pending`, `soft_ellipse_picked`. The B rubber band 0xf's measure is NOT DECODED (the distance from the centre
  is kept).

## Helix (create_helix @18008a290; dialog FUN_180083e00 res 0x1920; create_helix_ent @180089f10/f70)

- HELIX_PARAMS (FUN_180085750) and defaults via `RegisterVariable(var, metric, inch)` (mccore @1805fa3a0 confirms arg order metric, english) [code]:

  | idx | field | default (inch / mm) | UI range |
  |---|---|---|---|
  | [0] | radius | 1.0 / 20 | tol..1e38 |
  | [1] | revolutions (double) | 1 / 1 | tol..1000 |
  | [2] | pitch (per revolution) | 0.1 / 2 | |
  | [3] | taper angle deg | 0 | -90..90 |
  | [4] | initial (start) angle deg | 0 | -360..360 |
  | +0x28 bool | direction flag (radio index 0); true negates the Y axis -> clockwise [inferred CW] | false (CCW) |

  Dialog also shows height = pitch*revs (FUN_1800855f0); editing height sets pitch = height/revs (FUN_180085a00).
- Input: click base (centre) point; option "drag" (FUN_180088e00 at rubber-band size) [code].
- Math, mccore `MaHelix::MaHelix` @180a79540 + `EvaluateCrv` @180a79d30 [code]: frame = active view X,Y,Z (Y negated if CW); initial angle rotates X,Y about Z. For t in [0,1]: theta = 2*pi*revs*t; r = R + theta*(pitch/2pi)*tan(taper); P = C + r cos(theta) X + r sin(theta) Y + (pitch/2pi)*theta Z.
- Output [code]: `MakeMCSplineEntities` @180a7a7c0: if revs <= 50 one curve, else split into ceil(revs/50) pieces of <=50 revs; each piece `ConvertToNURB` (@180a79b90: 1+8*revs sample params, cubic fit with tolerance `surf_step_chord_dev`) -> **NURBS spline entities** (not arcs).

## Spiral (create_spiral @18008ab80; dialog FUN_180085c90 res 0x191f; create_spiral_ent @180089fb0 -> FUN_180089040)

- SPIRAL_PARAMS (FUN_180087890) [code; defaults from RegisterVariable + static data]:

  | off | field | default (inch / mm) |
  |---|---|---|
  | [0] | radius (start) | 1.0 / 20 |
  | +0x08 int | revolutions (1..1000) | 1 |
  | [2] | vertical pitch initial | 0 |
  | [3] | vertical pitch final | 0 |
  | [4] | horizontal pitch initial | 0.1 / 2 |
  | [5] | horizontal pitch final | 0.1 / 2 |
  | +0x30 bool | direction (as helix) | false |

  Height shown = sum of vertical pitches (FUN_180087700); editing height sets both vertical pitches = height/revs (FUN_180087b10). [2],[3] are vertical because they multiply the Z axis in MaSpiral::EvaluateCrv and advance the origin along view-Z in FUN_180089040 [code].
- If initial==final for both pitches: single `MaSpiral(centre, R, v0, v1, h0, h1, revs, initAngle=0, dir)` -> MakeMCSplineEntities (split per 50 revs) [code].
  Else: loop per revolution i: one 1-rev MaSpiral with constant pitches (v_i, h_i), then origin += v_i*Z, R += h_i, v += (v1-v0)/(revs-1), h += (h1-h0)/(revs-1); each converted with MakeMCEnt -> **one NURBS spline per revolution** [code].
- Curve: theta = 2*pi*revs*t; vertical/horizontal pitch per radian vary linearly in theta; r = R + h(theta)*theta; P = C + r cos X + r sin Y + v(theta)*theta Z (MaSpiral::EvaluateCrv @180a7aec0) [code].

## Fillet Entities

Entry `sketcher_fillet_1` @18026e3a0 → body `FUN_18026c370` (mccad_sketch.c). The panel is a .NET `WireframeFilletEntitiesManager`, constructed natively in `FUN_1801631f0` (dump: decomp/mccad_fillet_chamfer_mgr2.c). The core geometry is mccore `make_fillet` @180259270 (scratch dump of mccore).

- **Parameter struct** [code], filled from the panel by `FUN_1801641b0`:
  `{+0 int style; +8 double radius; +0x10 u8 trim; +0x11 u8 =0; +0x18 double clearance}`.
- **Defaults**:
  - [code] native init: style 0, radius 0.25 in / 5.0 mm (`0x3fd0…`/`0x4014…`, chosen by `IsMetric()`), trim 1, clearance 0.
  - [code] The panel model keeps these as persisted keys:
    - `"WireframeFilletEntities-Radius"`: default 0.25/5.0, lower limit `tol`.
    - `"WireframeFilletEntities-ClearanceDistance"`: default 0.
    - The trim bool defaults to true (`FUN_1800bd490(…,1)`).
  - The struct is re-read from the panel before every use, so in practice the persisted values win.
- **Style enum** (0..4):
  - [code] The `set_fil_small_sweep` and `set_lollipop_corner` mapping (in `FUN_18026c370`):
    - 0: sweep 0
    - 1: sweep 1
    - 2: sweep 2
    - 3: sweep 0 with the lollipop corner on
    - 4: `SetReliefFilletActive(true)`
  - [code/strings] The names, from the mccad.ui.dll radio buttons and tooltips:
    - 0 Normal: rounded corner.
    - 1 Inverse: inverted arc.
    - 2 Circle: a full circle in the corner.
    - 3 Clearance: "fillets on inside corners so the tool reaches into the corner". It uses the clearance distance, "max 95% of radius, measured along the fillet radius from the intersection".
    - 4 Relief: "puts the clearance on only one entity; only lines".
  - [inferred] The order is taken from the sweep mapping plus the tooltips.
- **Picking** [code]:
  - Styles 0–3: prompts 5863/5864, "Fillet: Select an entity" then "…another entity". The selection mask is 0xc8e, which covers lines, arcs and splines; `make_fillet` handles spline types 8 and 0x80 through `SplineIsFlat`.
  - Style 4 (Relief): prompts 5865/5866, "Select first line as tangent edge for fillet" then "Select second line".
  - The pick parameter `t` of each selected entity (event double at +0x18) is saved and passed to `make_fillet` as `double picks[2]`.
- **Side selection**:
  - [code] `make_fillet` passes the pick `t` values to `find_fil_gt` / `find_fil_ent`. Those functions are not in the dumped DLLs.
  - [inferred] The fillet goes in the sector of the corner that holds the picked portions of both entities; this is the usual Mastercam behaviour.
  - [code] With trim on, each entity is cut with `trim_ent_to_t(ent, 1, pick_t, fillet_t, …)`, so the picked side is kept.
- **Trim behaviour** (`make_fillet`):
  - [code] Trim on: both entities are trimmed to the tangent points. An entity whose length drops below `tol` (line length, or arc `|r·sweep|`) is deleted.
  - [code] Trim off with `param_8 = 1`: for each entity whose fillet tangent `t` falls outside [0,1], a new extension segment is stored, running from the nearer end (t=0 or 1) to the tangent point. The originals are left untouched.
  - [code] Relief: an extra line entity is stored (fetched with `GetReliefFilletExtensionEptr`).
- **Plane** [code]: the plane is found from the two entities (`CalcTwoLineView` / `find_view_from_ents`), falling back to the active construction plane and depth.
- **Other inputs** [code]: `set_fil_dir(2)` is used for entities. `make_fillet(e1, e2, radius, clearance, picks, out_arc, 1,0,1,1, …)`.
- **Output** [code]: one arc (`a_3d`) entity, stored in the main colour. Live preview: the user can re-pick, and the result is committed on the next selection, Apply or OK.

## Fillet Chains

Entry `sketcher_fillet_2` @18027e330 → body `FUN_18027ce30`. The worker is `FUN_18027d7d0`, which calls the native `create_chain_fillets` (decomp/mccad_fillet_chamfer.c).

- **Parameter struct** [code], filled by `FUN_18016a8d0`:
  `{+0 double radius; +8 int direction; +0xc u8 trim; +0x10 int style; +0x14 u8 =0; +0x18 double clearance}`.
- **Defaults**:
  - [code] native: radius 0.25 in / 5 mm, direction 0, trim 1, style 0, clearance 0.
  - [code] Persisted keys: `"WireframeFilletChains-Radius"` (0.25/5.0, minimum `tol`) and `"WireframeFilletChains-ClearanceDistance"` (0). Trim defaults to true.
- **Style** (0..3) [code]: the same mapping as Fillet Entities: 0 Normal, 1 Inverse, 2 Circle, 3 Clearance (lollipop). There is no Relief style for chains (no such tooltip).
- **Direction**:
  - [code] The UI int maps to `create_chain_fillets` dir as 0→2, 1→0, 2→1.
  - [strings] The three options are "All corners" (fillet every corner whatever the chaining direction), "+ Sweeps" (CCW corners relative to the chain direction) and "- Sweeps" (CW corners).
  - [inferred] UI 0 = All corners, which is dir 2, the same value Fillet Entities uses. Which of +/- Sweeps is UI 1 and which is UI 2 is unconfirmed.
- **Call** [code]: `create_chain_fillets(chain, radius, clearance, dir, trim, colour/0xffff, previewColour, main_level, out[], &flag)`.
  - The temporary `set_use_fil_dir(1)` is restored afterwards.
  - The preview colour is 0xf, or 8 when the background colour is 0xf.
- **Picking** [code]: `chain_it(0x1089, …)` (mccadres 4233 "Select chain"), the panel's own prompt `DAT_18040a2d8` = the literal `"*"` (its meaning NOT DECODED). Several chains are allowed. The preview is regenerated whenever a parameter changes (`FUN_18027d920` deletes the preview and the call is redone).
- **Loop** [code] (FUN_18027ce30, decomp/mccad_cmds/sketcher_fillet_2.c:196-299; Chamfer Chains is the same loop, sketcher_chamfer_2.c:197-305):
  - The panel is created first, then `chain_it` runs with the panel disabled (`Ordinal_4282` = EnableWindow(0) / (1) around it). chain_it = `run_chain_dlg(prompt, &chains, NULL, 0, 0, 0, 0, false)` (no mode argument: the dialog mode is the config's); `init_chaining` starts from an empty list.
  - chain_it returns 2 (cancelled, `cancel_chaining`) -> the loop ends (the command ends).
  - After chaining: `EnableApply(chains != NULL)` (manager vtable +0x30: FUN_18016adf0 / FUN_180176f50 set the "EnableApply" property) and, with chains, the worker on the whole list (`FUN_18027d7d0` -> `create_chain_fillets(list, ...)`, one UndoTransStart).
  - Code 1 (Apply) with chains: commit, `free_chains`, EnableApply(0), chain_it again. Codes 0 / 2 (OK / Cancel): commit, end.
  - Code 4 = the panel's **Reselect** button (`ReselectAllChainsCommand`, mccad.interop WireframeFilletChainsManagerViewModel -> native manager vtable +0x90 = FUN_18016aa80: `PostMessage(0x8038, 4)`; Chamfer Chains vtable +0x80 = FUN_180176cc0, the same): delete the preview, UndoTransTerminate, chain_it again (from no chains); 2 ends the command.
  - Codes 5-9 (Chamfer 5-7) with chains: delete the preview, UndoTransTerminate, the worker again.
- **Output** [code]: arc entities, one per qualifying corner, with the chain entities trimmed when trim is on. The trimming happens inside `create_chain_fillets`, which is not dumped.

## Chamfer Entities

Entry `sketcher_chamfer_1` @18021a770 → body `FUN_180218910`. The core is mccore `make_chamfer` @18023b080 → `chamfer_2_ents` (`FUN_18023a3c0`, `CrChamfer.cpp`) → `FUN_18023a860`, which computes the distances.

- **Parameter struct** [code], filled by `FUN_180170fa0`:
  `{+0 int style; +8 dist1; +0x10 dist2; +0x18 angle; +0x20 width; +0x28 u8 trim}`.
- **Defaults** [code]: style 0, dist1 = dist2 = width = 0.25 in / 5 mm, angle 45.0, trim 1.
  - The persisted keys are `"WireframeChamferEntities-Distance1"`, `"-Distance2"`, `"-Angle"` and `"-Width"`.
  - Angle has default 45, minimum 0 and maximum 360 (`0x4046800…`, `0`, `0x4076800…`).
  - Distances have minimum `tol`.
- **Style enum** [code + tooltips]:
  - `FUN_1802186a0` maps style s to `set_chamfer_method(s+1)` (checked in the disassembly).
  - The array passed to `set_chamfer_dist(double[4])`:
    - 0 "1 Distance": `d[0]=dist1`; both legs equal dist1.
    - 1 "2 Distances": `d[1]=dist1`, `d[2]=dist2`. dist1 applies to the entity selected first.
    - 2 "Distance and angle": `d[3]=dist1`, plus `set_chamfer_angle(angle)`.
    - 3 "Width": `d[3]=width`.
- **Geometry** [code] (`FUN_18023a860`):
  - Method 1: d1 = d2 = d[0].
  - Method 2: (d1, d2) = (d[1], d[2]).
  - Method 3: the leg on entity 1 is d[3]. The chamfer line goes through that point. Its direction is entity 1's direction rotated by 180°±angle, with the sign chosen by the cross product of the two lines. It is intersected with line 2 to get d2. So the angle is measured between the chamfer and the first entity. Warnings 0x16f6/0x16f7 (mccore strings) are raised when the lines are near-collinear or there is no intersection.
  - Method 4 (width W, θ = angle between the lines at the intersection): each leg d = W·sin((π−θ)/2)/sin θ = W/(2·sin(θ/2)). The chamfer is symmetric with chord length W.
  - Legs are converted to entity parameters as `t = t_int ± d/length`, signed toward the pick side (`pt_on_ent_from_t`).
- **Picking** [code]:
  - The two entities must be lines (type 2) or arcs (type 4); `chamfer_2_ents` rejects anything else.
  - Mask 6 (lines + arcs) for styles 0–1, prompt 5880 "Select line or arc".
  - Mask 2 (lines only) for styles 2–3, prompt 5881 "Select line".
  - The pick `t` values pick the intersection and the side kept (`intersect_ents` with the pick params).
- **Trim** [code]: `set_chamfer_trim(trim)`. With trim on, the entities are trimmed to the chamfer endpoints (`FUN_18023b800`).
- **Output** [code]: one line entity.

## Chamfer Chains

Entry `sketcher_chamfer_2` @180211fc0 → body `FUN_180210bb0`. The worker is `FUN_1802114b0`, which calls mccore `make_chamfer_chain` @18023b1b0.

- **Parameter struct** [code], filled by `FUN_180176b30`:
  `{+0 int style; +8 double value; +0x10 u8 trim; +0x11 =0}`.
  - value comes from the Distance field when style is 0, and from the Width field otherwise.
- **Defaults** [code]: style 0, Distance = Width = 0.25 in / 5 mm, trim 1. The persisted keys are `"WireframeChamferChains-Distance"` and `"WireframeChamferChains-Width"`.
- **Styles**:
  - [code] 0 "Distance": method 1, `d[0]=value`.
  - [code] 1 "Width": method 4, `d[3]=value`.
  - [strings] These are the only two styles for chains.
- **Algorithm** [code] (`make_chamfer_chain`):
  - For each chain, every pair of consecutive curves is passed to `chamfer_2_ents`.
  - If the chain is closed (`FUN_18023be90`), the last→first corner is chamfered too.
  - Entities must be lines or arcs. Method 3 would require lines only.
  - The chain must be planar and parallel to the construction plane (`get_view_from_chain`/`views_parallel`); otherwise a warning is raised.
  - A chain with fewer than 2 curves is skipped.
- **Picking** [code]: `chain_it(0x1089)` (mccadres 4233 "Select chain"), panel prompt 0x166c = 5740 "Adjust values, OK, Apply, or Chain". Several chains. Preview entities are drawn in colour 0xf (8 on a white background) and recomputed when a parameter changes. Loop, Apply / OK / Cancel / Reselect (code 4) and EnableApply as Fillet Chains above.
- **Output**: one line per corner, with the chain trimmed when trim is on.

## Combine Views (OnScreenCombineViews @1804703a0, CombineViews @180341cc0 -> combine_views @18032f0e0, mastercam.dll)

- No panel; status bar 1986 "Combining planes . . ."; `get_ent(mask 1, A_ID)` walks the arcs; the view map starts empty, views 1, 2, 5 and left-handed views (`LeftHandView` @180322230) = -1 (never moved). An unmapped view v: k from 1 up (`sVar21 == n_views` or `sVar21 < n_views && != v`, so the last view of the table can find itself), first with right-handed k and `views_parallel(k, v)` (@1803418c0, |dot| > 0.9999996) maps v -> k. A mapped arc: `put_special_undo`, centre to view k through world, the start angle from the stored world start point in view k (atan2, 0 .. 2 pi), sweep negated when the view normals are opposed, `pos_sa_sw`, `replace_ent` (same record / id), counted; one undo transaction, Terminate when none. Message box title 1988 "Combine planes", body "%d%s" of the count and 1987 " arcs moved to a new view". [code]
- Viewer: `wm_combine_views`; an arc loaded with its view normal along +-Z is stored here as a plain XY arc (Ent.vw 0, mcam_load.c:891) and is not walked: the count differs from Mastercam's for arcs in stored views parallel to Top / Bottom (their geometry does not change either way).

## Relief Groove (MakeReliefGrooves @180076e90; dialog CDINGrv MCCADRES 2516, table CGrvTable 2517; panel in viewer/core_ui/cui_wfmisc.c)

- The product check: `if (GetProdEnableBits() & 0x120) { if (!lathe_product_level_ok(1)) return; }` (mastercam.dll 18010a480 / 1801103b0): the bits come from the SIM; this program has no SIM, i.e. no product bits: the check is not taken and the command runs.
- Parameters P (dialog +0x7380), FUN_180077f80 defaults, then `rginch.prb` / `rgmetric.prb` from the configuration folder (240 bytes: the per-shape table dialog +0x72e0 4 x 5 doubles, P 0x48 bytes at +0xa0: +0 shape, +8 Z position, +0x10 geometry in 0 +X / 1 -X / 2 both, +0x11 radios 1509 / 1511 (bitmaps only in the dialog; static bitmaps 3182 / 3184 (Shoulder on right 3183 / 3185) from MCCADRES, FUN_180078480: the cross marks the Z position: radio 1509 = the left end of the groove at Z, 1511 = the right end (confirmed against FUN_180074c80 :660); the panel shows them from the icon pack (`#img:` labels; tools/extract_icons.py now includes mccadres.dll)), +0x12 Shoulder on right, +0x13 trim 0 None / 1 Break / 2 Trim; then +0xe8 Select Z only, +0xec Preview groove at selected position); written back on OK (FUN_180078880), a wrong file size: post_warning 632. Viewer: the file is in the config folder (config_file; MCAMUI_PRB_DIR overrides, tests only).
- The per-shape row P+0x18 .. +0x38 maps to the dialog through FUN_180078720 / FUN_1800787c0: Shape A / C: d = +0x20, dg = +0x28, g = +0x30, r = +0x38 (P+0x18 = the pitch, table only); Shape E / F: d = +0x18, t1 = +0x20, f1 = +0x28, g = +0x30, r1 = +0x38. Labels FUN_1800785d0: dg / g / r (4029 / 4031 / 4034), shapes E F: t1 / f1 / r1 (4030 / 4032 / 4033) with edit 1500 (g:) shown.
- Table (FUN_180078c60): rginch.tbl / rgmetric.tbl from the shared folder, name line + "shape d P dg g r" or "shape d r1 t1 f1 g"; Shape C: d - P * 1.407. Viewer: Select From Table is a combo in the panel (the dialog's list box dialog 2517). The order of the columns FUN_180078c60 writes: shape 0: +0x20 = d, +0x18 = P, +0x28 dg, +0x30 g, +0x38 r; shape 2 / 3: +0x18 d, +0x38 r1, +0x20 t1, +0x28 f1, +0x30 g.
- Geometry (FUN_180074c80 A, FUN_1800751c0 C, FUN_180075700 E, FUN_180075c60 F): Shape A/C: h = (dg - d) / 2; L0 (0,0)-(h / +-0.5773502691896, h); L1 to (-g, h); L2 to (-g, 0). E/F: L0 (0,0)-(-t1 / 0.2679491924311, -t1); L1 to (-f1, -t1) (F: -f1 - g x 0.1405408347024); L2 to (-f1, -t1 + g). `fillet_line_line_2d` r / r1 at both corners. Z = P+8, adjusted: not Shoulder on right and Left: + g (f1 for E / F); Shoulder on right and Right: - that; translate (Z, d / 2); Shoulder mirrors x before the translation, the -X run mirrors y after it (FUN_1800762e0). Failures (A: d, dg, g > 0, d > dg, d - dg >= r, g >= r; C: the mirror; E F: all > 0, g >= r): Preview post_warning 630, OK silently nothing (FUN_180076420 / FUN_1800765e0). [code]
- Created: `store_ctour_wcs(plane, ctour, 0.0, n, 0x40, main_color)` (mclathe.dll 18000f930): the plane origin in view coordinates + (x, y, 0), view_to_world; lines stored, arcs in the plane's view (AddMCView when it is not in the table, mcam_add_view: written by the save). The plane is the active **WCS**: `FUN_180074370` and the Select handler call `CombineActivePlaneAndOrigin` with PlaneType 3 (disassembly 1800743ac / 180077cc1 `MOV R8D,3`; the WCS string in mastercam.dll 1802c4e20), and `point_proc_2d` (mclathe.dll 1800267a0) returns the point in `GetActiveViewNo(3)`; the Cplane plays no part. The line search of the trim (FUN_1800749a0) stays literal on the world x, y, z = 0. A part's saved active planes and origins are applied on open since decision N9-27 (formats/planes_archive.md section 7). The origin used is the **active origin** of the WCS (`CombineActivePlaneAndOrigin` -> `GetActiveOrigin(3)` 0x1802c5830: the plane whose id is SystemPlanes +0x120[WCS], `plane_active_origin(3)`), which may be another plane's than the WCS's own; making a plane active (`SetActivePlane(type, plane)` 0x1802c9be0 -> SET_ACTIVE_ORIGIN 1) makes its origin the active one too (`plane_set_active`, 2026-11-12: the merge of N9-27 had left the test step `wmuplane` setting only the plane, so wm_rg_wcs_user lost its (1, 2, 3) origin).
- `fillet_line_line_2d` mcmachinedef.dll 1800eb0a0 (l_ints, reverse_lines, l_offset, proj_pt_ln, constr_ns_arc): ported as is; compared with the real DLL under tools/peloader (`--fillet`, mcfil.c, mcmachinedef + mccore + systypes): 8 / 8 cases equal to 1e-6, the failures included.
- Trim / Break (FUN_180074510, FUN_1800749a0, FUN_180076970): for ctour[1], ctour[n] (a line along x through that point, z = 0, the point interior) and ctour[n] again (a line along y), each line is cut: param_3 = (keep) piece, param_4 = other piece; FUN_180076730 stores the second piece unless a kept piece has the same ends; Trim deletes the second pieces at the very end (delete_all_by_mask(0x44, 2), so the three points cut them again). [code]
- Preview Groove / the Select button with Preview checked: built live, then `wait_for_space` (mccore 1804ebbe0): prompt 25539 "Press <ENTER> to continue", the panel greyed (AfxSuspendWindow), Enter removes the preview (delete_all_by_mask / FUN_1800764b0); Esc ends the wait too (RunPump's return paths are not fully read: [inferred]). Select (FUN_180077be0): point_proc_2d 631; Z = x - plane origin (Top: x), the diameter (unless Select Z only) = 2 |y|.
- Checked by hand (no real part with a groove in the samples): metric Shape A default d=16 dg=13 g=7 r=1 at Z=1: L0 ends (8,8)-(5.634,6.634) tangent-trimmed from (5.402,6.5), arc r 1 centre (5.134,7.5) 30 deg, L1 y = 6.5, arc centre (2,7.5), L2 x = 1 to y = 8 (scene wm_rg_a); Shape E / F / C likewise (scenes wm_rg_e/f/c); Trim of y = 8 line x 0..12: pieces (0,8)-(1,8) and (8,8)-(12,8) (wm_rg_trim), Break of the same line and the vertical x = 1: four pieces (wm_rg_break). [hand-worked, unverified against Mastercam]

## Create Letters (create_letters @18006ea40, dialog CLetDlg MCCADRES 434; viewer/core_ui/cui_wfmisc.c `wm_let_*`)

- letters_data: +0 Mode (1..5 Mastercam Box / Block / Roman / Slant / Other, 6..14 Drafting fonts, 15 TrueType; combo strings 5888..5902, FUN_180068550), +2 text, +0x130 alignment (0 Horizontal, 1 Vertical, 2 Top of Arc, 3 Bottom of Arc), +0x138 Height, +0x140 the folder (Other), +0x270 Arc Radius, +0x278 Spacing; defaults (:36-62) 20 / 1, 5 / 0.2, 20 / 1, mode 1. [code]
- Placement FUN_18006c0b0 + FUN_18006e7f0: scale = height / 1.4; letter `LETTERS.DAT` widths (first line the default, FUN_18006db00); letter file `check_letter_geometry_file_exist` @18006b620: CHAR%03hu, then the letter's name (upper case, the named punctuation QUOTE PERCENT ... BACKSLSH), A%03hu otherwise, trying .mcam .mcx-8 -7 -6 -5 (FUN_18006bb70). Point / line / arc / NURBS records (FUN_18006e2a0): points scaled and offset, the arc radius scaled, start angle + the rotation in the arc alignments, control points through FUN_18006e7f0, stored in the main colour / level; one undo step per placement; each pick places the text again (Mastercam). OURS, user decision LET-1 (approved deviation, formats/user_decisions.md): the placement is live until OK - Apply rebuilds it at the same placement with the settings now, the next click / arc / chain moves it, OK (Enter) commits it as one undo step, Esc drops it (cui_geo.c geo_let_*; scenes wm_let_l1_*).
- Horizontal: advance scale * width + spacing; Vertical: height + spacing. Arc: FUN_18006b440 (the line's width), the first rotation (half width - first glyph half) / radius, negative for Bottom; each next: ((w + w next) * scale * 0.5 + spacing) / radius; lines reversed when Top; y: Top +radius, Bottom -(height + radius), rotate_pt about the pick point; next line radius += height + spacing. [code] Default spacing when <= tol: 0.2 height (:1883).
- A missing `letters.dat`: `get_b_real` (mccoreres 5480 "Enter default letter width", start 0.8, tol .. 1e38; RequestRealInputModal, the dialog's range check NOT DECODED): every entry that width; Cancel ends the placement loop (FUN_18006c0b0 returns 0). Top of Chain prompts per letter (FUN_18006b0d0 runs FUN_18006db00 each time); a cancelled prompt drops that letter. The list of missing part files (FUN_180069e90 / FUN_18006a0e0): text 13010 " %s part file not found" of folder + name, lines "%s)\n" (the format stored at 0x180412d7c) or "%s (%d)\n" for repeats. Greyed, NOT PORTED: Load File..., Add Symbol..., Drafting Options..., TrueType(R)... (dialog 434 buttons 1358, 1352, 3763, 3726). Spacing is enabled for the Mastercam fonts or Top of Chain, Arc Radius for the Mastercam fonts, an arc alignment and no chain (FUN_180068100; 0x3ed8 / 0x4cd0 = the MCReal objects whose values are +0x4ad8 / +0x58d0, value at object + 0xc00).
- OURS: `.mcx-9` is also tried: the only letter files on this machine are X9's (paths.conf fonts = common/Fonts of mcamX9), not in Mastercam 2017's list. A line break in the text box is typed as the two characters \\n (the panel's single-line text; Mastercam's Letters box is multi-line). 
- Drafting fonts 6..14: FUN_18006bfa0 / FUN_18006beb0: drw_string(.., 1, text) with the note font attributes (init_attribs, is_dimen 0), font = mode - 6 (14: note_font), height +0x138 when > tol; here dn_globals_config + dn_calc_note + draft_render_text (the port of FUN_18031a950), each stroke segment a line. Reserved views only (a user plane: NOT PORTED message).
- Top of Chain (alignment 4, FUN_18006cce0, FUN_18006a9b0, FUN_18006b540): decoded and ported: glyphs built at the origin (spacing and height as they are, no 0.2 x H / 1.0 fallback; the drafting fonts with h_aln Center, v_aln Base: FUN_18006a9b0 sets font_attr +0x58 & 0xedff | 0xd00), width = view bbox, Mastercam fonts centred on x, advance by average width + spacing (space) or half widths, one step to the next chain entity, a newline = next chain, chain_it loops (0x1089); lines and arcs (ge_at / ge_dir) only.
- Arc pick for the arc alignments (FUN_18006d450, mask 3): arc radius, side from the pick against centre +- r / 2; an arc in another view: not taken (SetActivePlane branch NOT PORTED).
- LETTERS.PRM (FUN_180069100 / FUN_180067fb0): INI section LETTERS, sorted keys Alignment Height Mode Radius Spacing UserSpacing; REAL "%.15f" trailing zeros stripped (wr_param_REAL: mccore 1806013bd MOV R8D,0xf), BOOL Yes / No (DAT_180d84bfc / bf0), read at every start, written with each placement here (Mastercam: at the dialog's OK; the panel stays up here). UserSpacing: Height change clears it and sets spacing = 0.2 height (FUN_1800674b0 / FUN_180066380); a Spacing change sets it.
- Record filter (FUN_18006d910: +0x3c > 0 and sel bit 1 clear skipped) via Ent.rec3c / rec_sel. Scan of the 253 letter files of this install: 1935 records have +0x3c > 0, none is filtered; none is a NURBS.
- Non-Top Cplane: start point world_to_view, FUN_18006e7f0 ends with view_to_world: no plane origin anywhere in the letter path (decoded).
- dbmmess 5605: string 5605 of MCCADRES / MCCORERES resolves empty (parsed from the resource table); the box shows the list of files only.
- Not ported: TrueType 15 (decision N9-21: greyed).
- Checked: Box "AB" height 14 spacing 2 at (10,20): scale 10; A's point (0.1125, -0.4) -> (11.125, 23), B starts at x = 10 + 10 x 1.05 + 2 = 22.5, its arc r = 10 x 0.35 = 3.5 (LETTERS.DAT 65 = 1.05; scenes in tools/tests/wf_letters_scenes.txt, run with WF_SCENES=; their expected outputs hold the outlines of Mastercam's letter files, so they stay out of the repository: ~/mastercam-re/regress/golden/wf_letters).

## Commands left greyed (N9-16; decode status)

- **Line Normal**: ported (N9-23): section "Line Normal" above.
- **Curve Slice**, **Curve at Intersection**: merged 2026-11-11 (sections below); **Curve Parting Line** (parting_line 0x180566740) and **Curve Surface** (project_chains_onto_surfs): ported 2026-11-11 (overnight O1 / O2, sections "Curve Parting Line" and "Curve Surface" below).
- **Silhouette Boundary** and **Turn Profile** (FUN_180072d50 -> FUN_18006f480 -> FUN_1800704b0 / FUN_180070060 / FUN_18006f2a0, shared with Silhouette): the mcmsurf closure (115 functions, 6453 lines) is the driver; the kernel is TPSilhouette / TPSpinSilhouette in mckernel.dll -> ComputeMCSilhouette in tpiftools.dll (.text 3.4 MB), reached through an STL / IFF temp-file pipeline. Not tractable as a port of the 6450 lines alone.
- **Raster to Vector** (`m_main` in chooks/rast2vec.dll): a separate product (`APPLICATION "MWrast2vec"`, build path `chookdev\mcamAdapter`): .text 5.7 MB, .rdata 2.3 MB, .data 0.2 MB; imports mfc120, BCG, MCCore, MCMill, MCLathe, MCVerify, MCGeomSld, NLib, TlCore, UICtrls, VCOMP120 ...: not self-contained, so not runnable under tools/peloader without most of Mastercam; embeds an image library (libpng strings) and a vectoriser with `rast2vec.dat` data (1 MB). A port is a project of its own (order of the 5.7 MB of code).

## Conventions (MCCADUTIL.DLL modify commands)

- Entity type ids used as selection masks [code]: `1`=point, `2`=line, `4`=arc, `8`=parametric spline, `0x80`=NURBS spline. Mask `0x8e` = all curves (line|arc|spline|NURBS); `0x8f` adds points (e.g. FUN_18004f9f0 switches on 2/4/8/0x80).
- `tol` = global system tolerance (`tol_exref`); `curve_chord_dev` = global chord deviation (`curve_chord_dev_exref`).
- `RegisterVariable(&v, metricValue, inchValue)` registers a length that gets unit conversion. The `.data` initial values read from the DLL equal the 2nd argument (e.g. offset distance initialised to 1.0 = inch), so argument order is (metric, inch) [inferred from .data bytes + magnitudes].
- Static `.data` defaults below were read straight from `~/mastercam-re/files/mccadutil.dll` (file bytes at the VA). They persist for the session (globals).
- New dumps: `decomp/mccad_util_project.c` (do_xform_project), `decomp/mccad_util_modify2.c` (31 callees used below).

## Trim Break Extend (sketcher_trim_1 @1800d23d0 -> FUN_1800ce760)

- Panel object `WireframeTrimBreakExtendApp` / `IWireframeTrimBreakExtendManager` (.NET). Panel parameter struct, initialised in FUN_1800ce760 [code]:
  - `+0 int mode` = 0; `+4 bool trim` = 1 (Trim; 0 = Break); `+8 double length` = 1.0 (literal, no metric switch); `+0x10 int` = 0. Read back from panel by FUN_1800d5bd0 (length, trim/break bool, mode).
- Mode enum (FUN_1800ce550 @1800ce550 sets pick count + event mask; FUN_1800cdd80 @1800cdd80 executes) [code; names inferred from behaviour + Mastercam UI]:
  | mode | name | picks | execute (Trim=1) | execute (Break=0) |
  |---|---|---|---|---|
  | 0 | Automatic | 2 (event mask 0xc90a, allows finishing with 1) | `trim_123(1,...)` like mode 1 | FUN_180049ae0 (break at intersection), else trim_123(1, ..., break=1) |
  | 1 | Trim 1 entity | 2 (entity to trim, then trimming entity) | `trim_123(1, ents, ..)` | same as above |
  | 2 | Trim 2 entities | 2 | `trim_123(2, ..)` (both trimmed to their intersection) | FUN_180049560 then trim_123(2,.., keep) |
  | 3 | Trim 3 entities | 3 | `trim_123(3, ..)` (first two trimmed to the third) | trim_123(3, .., keep) |
  | 4 | Divide/delete | 1 | FUN_18004e810(ent, params, 1) = delete the span between the two nearest intersections | FUN_18004e810(.., 0) = break at those intersections |
  | 5 | Trim to point | 2 (entity, then point) | FUN_1800503b0 (trim/extend to the picked point) | FUN_18004f9f0 (break at point; same routine as Break Two Pieces) |
  | 6 | Modify length | 1 | `extend_curve(length, ent, t_pick)` — extends (+) / shortens (−) the end nearest the pick | FUN_18004c290(ent, t_pick, .., −length): see "Break mode" below [code] |
- Mode 6 is skipped if |length| <= tol [code].
- **Break mode** (Trim.Value false, FUN_1800cdd80's first switch; dumps 2026-11-07 from ghidra_cad mccadutil: trim_123 @180060cb0,
  FUN_180049560, FUN_18004e810, FUN_18004c290 / FUN_18004c400 / FUN_18004bca0, FUN_18005fa20, FUN_18005bfd0 - not saved in decomp/,
  regenerate with DumpDecomp on mccadutil.dll) [code]:
  - `trim_123`'s 4th argument is the **break flag** (0 from the Trim switch, 1 from the Break switch and Trim Many's Break). With it,
    each entity whose intersection point is >= tol from both its ends goes through FUN_18005fa20(ent, n, pick, t...), which stores the
    part on the far side of the pick - or, for t past an end, the piece from that end to the point - as NEW entities (store_ent, the
    entity's attributes); trim_ent_to_t then runs only when t is inside [min_t, max_t] (the original keeps the pick side). Net: a
    split at an inside point; for a point past an end the original is untouched and the gap piece is added. Mode 3's third entity:
    FUN_18005fa20(ent, 2, pick, t0, t1), then trimmed to the inside one(s) (FUN_18005bfd0 = t inside [min_t, max_t]).
  - FUN_180049ae0 (modes 0 / 1; Trim Many): FUN_18004a480 records the carrier intersections inside both entities (ranges widened by
    tol / length; seeds 1x1 for two lines, 5x5 when an arc / spline is involved; intersect_ents returns first the hit nearer the
    midpoint of the seed points, the first seed being both starts); FUN_1800492d0 records a hit for an entity only >= tol from its
    nearer end, first entity first. If the first record is on the entity to break, only that record is kept (DAT_1802555d8 = 1) and
    FUN_180049680 breaks it there; otherwise 0 -> trim_123(1, .., break).
  - FUN_180049560 (mode 2): the same records; any -> FUN_180049680(1) breaks both entities at all of theirs; none -> trim_123(2, .., break).
  - FUN_18004e810(ent, t_pick, 0) (mode 4): the nearest intersection on each side of the pick with the displayed curves (and solid
    edges), each only inside the entity (tol margins; an arc piece under tol dropped); break (FUN_18004bb90) at each found one.
  - FUN_18004f9f0 (mode 5): Break Two Pieces' break at the point.
  - FUN_18004c290(ent, t_pick, pt, -length) (mode 6): DAT_180244600 = -length; FUN_18004c400 (lines) / FUN_18004bca0 (arcs) mode 2
    creep_along_ent(d) with d = -length from the start, or L + length when the pick is nearer the end: inside [0, 1] -> broken there;
    outside -> a new entity from the nearer end to that point (store_ent). So + length adds a separate piece past the picked end,
    - length breaks off |length| at it.
  - FUN_18005fa20 in detail (call-site args from the disassembly @18006146c / 180061722 / 180061978: (ent, n, pick, t0, t1, p6 = t0)):
    lines - b = (t0 < pick && t0 < max_t) || t0 < min_t; per point b -> the piece start..P, else P..end, b flipping for the next point;
    arcs - n 2: the points sorted, b = pick between them; n 1: b = pick > t (t outside [-0.01, 1.01]: pick < 0.5); per point A = sa +
    t sw: p6 outside [0, 1] -> b: A..sa, else end..A; inside -> b: sa..A, else A..end; the sweep takes the arc's sign; kept when
    |sweep| r >= tol. Mode 3's third entity: FUN_18005fa20(e, 2, ...) then nothing when both points are outside (FUN_18005bfd0), else
    trim_ent_to_t at t0 when inside, then at t1 when inside (recomputed on the trimmed entity; an arc's t1 within 1e-6 of 0 / 1 skipped).
  - Which piece keeps the original record (replace_ent rewrites it in place; ours: mod_pair - a new record that takes the id and the
    toolpath chains, as a Move): trim_ent_to_t + replace_ent (every one-piece trim / extend / close) - that piece; trim_123's break -
    the trimmed pick side; FUN_18004c400 / FUN_18004bca0 mode 1 (Divide's and Modify length's breaks) - the original rewritten up to the
    point (the start piece; Divide breaks the right point first, so the start piece keeps it); Break Two Pieces / Trim to point
    (FUN_18004fec0 -> FUN_1800509e0) - the side of the entity pick (pick t <= point t -> start..P); break_circles - the first piece;
    FUN_180049680 / FUN_180049bf0 (Break at Intersection, the recorded breaks) and Break Many - every piece new, the original deleted.
  - Port: cui_geo.c fa20_pieces / brk123 / brk123_3 / brk_len / inter_recs / break_first, replace_keep / split_at_keep (scenes wf
    trim_brk_*, trim_keepid, break2_keepid, trim_brk_lenmid_keepid). Ours: the two-hit ordering uses our carrier intersections
    ordered by distance to the midpoint of the starts (= the first seed; later seeds only re-find the same carrier hits of a line /
    arc pair); a full circle as the entity to break with one point / as mode 3's third entity is left as it is (trim_ent_to_t on a
    full circle NOT DECODED).
- The picked location (pick point / parameter) on each entity decides which side is kept — standard "click the part to keep" (trim_123 receives pick params) [inferred from data flow].
- Solid edges can be used as trimming entities: `SetSolidSelectionEdgesCheckBoxEnabled(mode==4)`, `ps_edge_to_mc_curve_struct` temp curves are created and deleted afterwards [code]. Live preview gated by config `enableTrimPreview` (clears event bit 0x100) [code].
- Event 0x100 (mouse move) when picks == required−1 -> dynamic preview FUN_1800cf6e0 [code].
- Config hook "SetupMastercamConfiguration"(isMetric, tol) passed to .NET manager (FUN_1800c91d0) [code].

## Trim Many (sketcher_trim_2 @1800dda00 -> FUN_1800db540)

- Panel `WireframeTrimManyApp`. State: byte `trim` (default 1, read from panel bool by FUN_1800df0e0), byte `flip` (toggled by panel command 4) [code].
- Flow (FUN_1800dbf20 @1800dbf20) [code]:
  1. `select_ents(mask 0x8e)` – the curves to trim (window/multi select).
  2. Pick the trimming entity (event 2 = entity, or 0x4000 = solid edge projected by `proj_2d_pt_to_curve`).
  3. If Trim: pick a point for the side to keep (event 4). Side = sign of 2D cross product of the trimming entity tangent at the projected point with (pick − foot point): `keepLeft = cross(dir_to_pick, tangent) >= -tol`.
     If Break: no side pick.
- Execute (FUN_1800db2d0 @1800db2d0) [code]: Trim -> FUN_1800dc870(selected, trimmer, sideFlag^flip) (dumped 2026-11-07, ghidra_cad mccadutil). Break -> for every selected curve, last first, FUN_180049ae0([curve, trimmer], 1) (the first recorded hit, see Trim Break Extend "Break mode"); if that fails, `trim_123(1, [curve, trimmer], picks 0.0, break=1)` (the seed at both starts). No side pick in Break (FUN_1800dbf20). Opposite side = the panel's IsDirection1 (flip): the kept side is the other one. Port: cui_geo.c G_TRIMMANY (scenes wf trimmany_brk, trimmany_opp).

## Break Many (sketcher_trim_3 @1800b19b0 -> FUN_1800b0330 "Break Many Pieces")

- Panel `WireframeBreakManyPiecesApp`. Param struct initialised in FUN_1800b0330, read via FUN_1800ba8d0 [code]:
  - `+0 byte curvesAsLinesArcs` = 0; `+4 int method` = 0; `+8 int segments` = **5**; `+0x10 double tolerance` = `curve_chord_dev`; `+0x18 double exactLength` = **0.1 in / 0.5 mm** (IsMetric); `+0x20 double fullLength` = 0.1 in / 0.5 mm; `+0x28 int original` = 0.
- `+0` is the panel's **CreateCurves** (FUN_1800ba8d0 reads FunctionPanelEditableBool param_1[6]; BAML `IsChecked={CreateCurves.Value}`): 1 = Create curves, 0 = Create lines.
- Method (FUN_1800afcc0 @1800afcc0) [code]:
  - 0 Number of segments: piece length = L / n (n<1 forced to 1), each piece >= tol*1.01.
  - 1 Tolerance (chord error): lines untouched; arcs: angle step = sqrt(8·tol/R) -> piece length = L / (|sweep|/step), broken into chords (FUN_1800af2a0's param_5 = 0) - with CreateCurves set, arcs are left as-is; splines: `break_spline_const_error(tol)` or, with CreateCurves, `break_spline_arcs(tol)` + `FilterEntities` (warning 0x1746 on failure). Tolerance clamped to >= curve_chord_dev. Untouched curves are not deleted (FUN_1800af8d0 skips lines / arcs-as-curves in this method).
  - 2 Exact distance: piece length = exactLength (clamped >= tol, then >= tol*1.01); FUN_1800af2a0 makes ceil(L/len) pieces from the start, the last the remainder [code].
  - 3 Rounded distance: n = round(L/len) (>=1), equal pieces of L/n.
- FUN_1800af2a0(ent, len, param_4 = 0.0, param_5, list) [code; disassembly @1800afeaf / aff7b / b00de / b01a8 for the stack args]: param_5 = CreateCurves for methods 0 / 2 / 3, 0 for method 1. n = ceil(L/len), minus one if the last piece would be under tol; the pieces run from the end nearer param 0.0 (the start); arcs stay arcs (sweep / (L/len) each) only when param_5, everything else becomes lines between creep_along_ent points.
- Panel feedback (FUN_1800bb8f0 pushes the struct back after each run) [code]: method 0 first sets exact / rounded = 1e38 and lowers them to each curve's piece length; methods 2 / 3 first set Number = 0x7fffffff and lower it to each curve's piece count.
- Splines (S_ID / NB_ID) [code, dumps 2026-11-07 ghidra_core / ghidra_cad]: FUN_1800af2a0 makes lines between creep points (init_creep_info
  @1802581b0, creep_along_ent -> FUN_180257c10: a walk in steps of sqrt(8 tol / ent +0xa8) (FUN_180013db0 S_ID, <= 1; FUN_180013d50 NB_ID,
  (t2 - t1) / (8 x +0xcc control points) when +0xa8 <= tol) summing chords, the last step bisected (20 rounds) to the distance; the
  last piece ends at the stored end point ent +0x80); method 1 + Create lines -> break_spline_const_error (mccadutil @180062630): from
  t1, brk_spline_const_err(curve_min_step, curve_max_step, tol) until t2 - 1e-4, a step > tol_sys a line, or the last line extended
  when check_colinear(last start, last end, P). ent +0xa8 is a stored length (inch / mm copies differ by 25.4; pieces of one curve
  keep the parent's value) - only read. Splines are in the command's selection (select_ents mask 0x8e; cui_sel.c sel_list).
- Live (FUN_1800b0330): the pieces in an open undo step in the live colour; codes 4-9 (the panel's values) UndoTransTerminate + run
  again on the same selection; codes 0 / 1 and the loop exit - code 2 / Esc too, local_46c only records it - commit
  (sktcher_main_color + FUN_1800af8d0 + UndoTransCommit); code 1 then select_ents again.
- Port (2026-11-07): cui_geo.c brkmany / brkmany_ent / brkmany_spline / brk_spline_lines, brkmany_run (live, geo_live_kind LV_LIVE |
  LV_REBUILD); Blank = mod_blank (cui_undo.c: undoable, saved as sel bit 0x100); scenes wf breakn_*. Ours: a spline's length_ent =
  CurveLength (MaCurve vtable +0xc0, NOT DECODED) by Gauss-Legendre. NOT PORTED: method 1 + Create curves on a spline
  (break_spline_arcs' biarcs FUN_180161910 / FUN_180160ce0 / FUN_180160ef0 + FilterEntities -> mccore FilterChains): left as it is.
- Original curves (`+0x28`, FUN_1800af8d0) [code]: 0 = delete (default), 1 = keep, 2 = blank. Results recoloured via `sktcher_main_color`.
- Input: select curves (0x8e) first, then the panel stays open for re-apply / re-select [code].

## Break Two Pieces (Break2Pieces @18004b150)

- Pick entity (mask 0x8e, prompt 0x178b), then pick a point (prompt 0x178c); curve broken at the point projected onto it (FUN_18004f9f0 @18004f9f0) [code]. Loops until Esc.
- FUN_18004f9f0: maps 2D pick to construction depth in active view; lines -> FUN_18004fec0/FUN_1800509e0, arcs -> FUN_18004f180/FUN_1800505d0, spline/NURBS -> FUN_18004f5c0/FUN_180050a60. Original entity rewritten as one piece + new second entity; associativity re-linked (`DfReAssocOE`) [code]. Break refused if the point is within `tol` of an endpoint (when called with endpoint-check flag) [code].

## Join Entities (join_selected_entities @180053510 -> JoinEptrs @180050b50)

- Select curves (mask 0x8e, prompt 0x1cf4) then join all; one undo transaction [code].
- Conditions (JoinEptrs + helpers) [code]:
  - Lines: `check_colinear` on endpoints; FUN_180052cf0 replaces the pair by the longest segment among all endpoint pairs (so overlapping/abutting collinear lines merge). Endpoint coincidence uses `tol`.
  - Arcs: FUN_1800532a0 – same centre (dist < tol), same radius (|ΔR| < tol), same view/plane id (ushort @ent+200). FUN_1800529e0 merges: both normalised to positive sweep, combined start = start of the "leading" arc, sweep = span; if span <= 1e-6 it becomes 2π (full circle); result rewritten as one arc, other deleted.
  - Splines/NURBS: FUN_180053130 merges two pieces of the same underlying spline by unioning their parameter ranges (t0=min, t1=max) — i.e. only re-joins splines that were broken from one spline [code]; a separate branch compares chord distance to `curve_chord_dev` (JoinEptrs line ~1103) before merging [code, purpose inferred].
  - Zero-length entities (< tol) are skipped [code].
- When >2 entities would be joined in some cases a yes/no box (strings 0x1cf1/…) is shown once [code; text unknown]. Status text 0x1cf5 "%d joined" / 0x1cf8 "none" [inferred].

## Break at Intersection (break_at_inters @18004a760)

- Select curves (mask 0x8e) and optionally solid edges/bodies (sld pick, window) [code].
- For each pair of selected curves (bbox prefilter FUN_180049470, bboxes from FUN_180049f80) and each curve vs each picked solid edge: FUN_18004a480 intersects with seeding grid 1×1 (line/line) or 5×5 parameter starts (if either is arc/spline/NURBS, mask 0x8c); intersections inside the open parameter range (ends shrunk by tol/length) are recorded (FUN_1800492d0) [code].
- Then all curves broken at all recorded params (FUN_180049680); solid edges are only cutters [code]. Esc aborts; warning 0x1cf0 on error.

## Break at Points (BreakAtPoints @18004b480)

- Select points and curves together (mask 0x8f) [code].
- For every selected curve (0x8e) × every selected point (1): FUN_18004f9f0(curve, point, endpointCheck=1) — break the curve at the point (projected); points within tol of an existing endpoint are ignored. Repeats passes until no new break (new pieces re-tested) [code]. Points are kept. Warning 0x1792 if nothing broken.

## Offset Entity (do_xform_offset @18001a420, dialog CXfOffsetDlg)

- Param struct `DAT_180242810` (static defaults from .data) [code]:
  - `+0 int mode` = **1**: 0 = Move (original blanked/deleted, refs reassigned), 1 = Copy, 3 = Join (copy + connecting lines between original and offset ends when copies==1, FUN_1800204c0) [code; 2 unused/unknown].
  - `+4 int copies` = **1** (MCInt range 1..30000).
  - `+8 double distance` = **1.0 in / 25.0 mm** (RegisterVariable(…,25.0,1.0)).
  - `+0x10 int direction` = 0: 0 = picked side, 1 = opposite side, 2 = both sides (copies doubled) [code: FUN_180018230 pass 0 skipped if dir==1, pass 1 (−distance) skipped if dir==0].
  - `+0x14 bool` use result colour/level, `+0x15 bool` increment per copy (only when copies>=2), `+0x18 level` (−1 = main), `+0x1c color` (−1 = main) [code].
- Input: pick one entity (line/arc/spline/NURBS, mask 0x8e family), then a point indicating the side (`read_cursor_pos`), unless direction==2 [code FUN_180019db0].
- Geometry (FUN_180018230) [code]: copy i offset by i·distance. Lines/arcs: `gt_offset` in the entity's plane (arcs: error if radius would become <= 0, message shows the limit value). Splines: FUN_180019320. Arcs require the construction plane parallel to the arc plane (`views_parallel`), else error 0xf5f. In 2D construction mode the result is squashed to construction depth. Spline side picked via tangent × view normal (FUN_180017f80).

## Offset Chains (do_xform_offset_ctour @18001cd70, dialog CXfOffsetContourDlg)

- Param struct `DAT_1802429e0` [code; .data + init]:
  - `+0 mode` = **1** (res 6460 radios 100-103: 0 Move -> chains blanked, 1 Copy, 2 Join, 3 Slot when copies==1).
  - `+4 copies` = **1** (1..30000).
  - `+8 distance` = **0.5 in / 10.0 mm**.
  - depth (`DAT_180254e08` incremental / `DAT_180254e00` absolute, both 0.0; `+0x40`/`DAT_180242a20` = 1 selects which) [code; abs/incr naming inferred].
  - `+0x10 double angle` = 0.0 (dialog limit 89.99999°) [inferred from 0x40567fff0faf200d max].
  - `+0x18 int direction` = 0 (0 picked side 'L', 1 opposite 'R', 2 both) [code FUN_18001bc50].
  - `+0x1c short corners` = **2**, dialog combo index = value−1 -> 1..3 = None / **Sharp** / All [names inferred; values code].
  - `+0x28 byte` = 1 (unknown checkbox) [code].
  - `+0x30 linearization tol` = **0.001 in / 0.02 mm**; `+0x38 max depth variance` = **0.005 in / 0.1 mm** [values code; names inferred].
- Input: `chain_it(0xf64)` (one or more chains; the mccadutil resources are current and mccadutilres 3940 is empty, so the prompt is mccoreres 3940 "Offset: select chain" through MFC's string search [inferred]); no chains -> the command ends. Then `get_chain_depth` and the dialog with the preview of every chain (FUN_18001bc50). There is **no side point**: each chain is offset to its 'L' side (direction 0; 'R' for 1, both for 2) - FUN_18001c220(chain, 'L' 0x4c / 'R' 0x52) -> ccomp_pock, the compensation side of the chain direction (the contour toolpath passes the same 'L' / 'R' for compensation left / right, mcmill_ctour.c:943-946). While the preview is valid get_event waits for an entity (mask 0x8e, prompt mccadutilres 1523 "Modify dialog values or select chain to reverse it."); an entity of a chain reverses that chain (FUN_18001ca60, the first chain in list order holding it) and the preview is rebuilt (0x66) [code].
- After the dialog the outer loop runs chain_it again while the dialog's +0x5091 is set (do_xform_offset_ctour.c:144 / :237; init 0 at
  l.1152). DECODED 2026-11-05 (see "Transform > Common machinery: the dialog flags" below): Apply 12321 = FUN_18000b7d0 = +0x5091 := 1 then
  OK (FUN_18000b230); enabled by FUN_18001db60 -> FUN_18000b030 (+0x5090 && +0x50a4). This dialog's OnInitDialog FUN_18001e550 (vt slot 95,
  decomp/xform/gaps/oninit.c) sets +0x5090 and posts 0x66 in every round, so OK / Esc commit and Apply is enabled from the start. PORTED
  (cui_geo.c geo_xmod; scene wf offsetc_apply). Esc = the forced 0x65 (bVar1, l.150-160): OK while +0x5090 is set - i.e. always here
  (scene offsetc_esc re-blessed: Esc now commits).
- Geometry: per chain and side FUN_18001c220(chain, 'L'|'R', copies, distance) for planar chains; 3D chains (ctour_3d) use FUN_18001c4c0 with depth/angle. `min_fil_ang` saved/restored around the call [code].

## Close Arc (close_arc @180062370)

- Select arcs (mask 4). For each arc with sweep < 2π: sweep set to +2π (or −2π if sweep <= 0); start angle kept; entity replaced in place [code]. Already-full circles untouched.

## Break Circles (break_circles @180064c60)

- Select arcs (mask 4), then prompt integer pieces: **default 2**, range 2..32767 (`get_b_int`) [code].
- Only full circles (||sweep| − 2π| < 1e-5) processed; others deselected. Circle replaced by first piece (start unchanged, sweep 2π/n, positive), then n−1 new arcs each starting sweep later (`restrict_angle` 0..2π) [code].

## Project (do_xform_project @180028a70; dump decomp/mccad_util_project.c)

- `XF_PROJECT_PARAMS` at `DAT_180242ea0` [code; .data + init]:
  - `+0 short mode` = **1** (0 Move -> originals blanked; 1 Copy; 2 Join). Join is changed to Copy when projecting to surfaces [code].
  - `+2 short target` = 0: 0 = construction depth, 1 = plane, 2 = surfaces/solids [names inferred; 2 -> XfProject_Surfaces, <2 -> XfProject_Squash, code FUN_180027360].
  - `+8 double depth` = 1e38 sentinel -> uses current construction depth.
  - `+0x10 short plane/view` = active view (validated 1..n_views); `+0x18 p_3d origin` = active origin.
  - `+0x30 byte` = 1, `+0x31` = 0 (dialog checkboxes, unknown), `+0x32` result attr, `+0x34 level`/`+0x38 color` = −1.
  - `DAT_180242e90` = **0.1 in / 2.0 mm**, `DAT_180242e98` = **1.0 in / 25.0 mm** — passed to XfProject_Surfaces (likely max-distance / limit values) [inferred].
  - Surface options via globals: `msf_proj_cview` (along cplane vs normal to surface), `msf_find_multi`, `msf_join_em` + `msf_join_tol`, `surf_step_chord_dev` [code]; forced `msf_make_spline=1`, `msf_trim_em=0`, `msf_make_cons=0` -> surface projections produce splines [code].
- Input: `SelectEntsOrdered(mask 0x8f)` = points + curves; then (target 2) surfaces/solids; target auto-set to surfaces when FUN_180027a40 reports surface selection [code].

## Transform (decoded 2026-10-23; code viewer/core_ui/cui_xform.c; dumps decomp/xform/)

Ribbon tab Transform (ui/layout.txt): Position = Dynamic (DynamicXform), Translate (do_xform_trans) / Translate to Plane
(do_xform_trans3d), Rotate (do_xform_rotate), Project, Move to Origin (do_xform_movetoorigin), Mirror (do_xform_mirror), Roll
(do_xform_roll); Offset = Entity, Chains; Layout = Geometry Nesting (MCCORE do_nest_g), Rectangular Array (do_xform_array),
Distribute (TransformDistribute); Size = Stretch (do_xform_stretch), Scale (do_xform_scale). All in MCCADUTIL.DLL
(mastercam.ft "Xform" category), dialogs in mccadutilres.dll, the work in mccore's Xf* machinery.

### Common machinery (mccore; decomp/xform/mccore_xform.c, mccore_xf_colors_join.c)
- Flow of each do_xform_* [code, do_xform_trans @180047570 as the model]: params reset (see each command), select_ents(prompt,
  mask 0x1f1fff, MC_PRESEL 2 = a preselection is taken at once), DfXformCheck, the MFC dialog (CXf*Dlg); its loop:
  panel code 0x66 = rebuild the preview (XfDeletePreview + XfCreatePreview), 0x65 = OK / Apply (XfDoFinalAction, then
  UndoXformTransCommit), 100 = Cancel (XfDeletePreview); Apply sets dlg +0x5091 and the outer loop runs select_ents again
  (the params are NOT reset between those rounds: the reset block is before the loop). Button 1250 "Add/Remove Geometry"
  re-runs select_ents.
- XfCreatePreview(type, MoveCopyJoin, ...) -> FUN_1808d8820: MoveCopyJoin 0 Move, 1 Copy, 2 Join (the join flag) [code
  XfCreatePreview: cVar1 = (mode == 2) + 2, mode 99 -> 1]. One XfInfo entry (0x80 bytes: 3x3 + translation, copy index
  +0x70/+0x72, reverse flag +0x74) per copy; every pass transforms the selected originals (sel bit 4) by that entry's
  matrix (the commands build Mⁱ by repeated mul_xmatrix).
- Copies keep the original's level / colour / attributes (FUN_1808d8820 param_7 = 1 -> DAT_18143d6e8 -> xform_store_ent
  l.1260-1268). Use New Attributes (check 1665 "Assign results new level and color"): XfApplyColorLevelToResult_Index
  (colour, level, IncrLevels): the results of entry k get the colour and level (+ k with "Increment level per copy" 1688);
  Level < 1 = main level, Color < 0 = main colour (FUN_180045f00).
- Marks: originals get sel bit 2 (group), results bit 8 (result) (xform_store_ent l.1350-1352, set_clr_sel_bits_all(4, 2,
  ...)); find_color (0x1802e72d0) draws bit 2 in Group color (config 5010, mastercam.dll .data 12) and bit 8 in Result
  color (5031, .data 13) while config 5044 "Use Group and Result color in Xform?" is on (use_group_color /
  use_result_color .data 1). XfClearBITS(0, 2, 8) clears them at each new preview. Home > Clear Colors (mastercam.dll
  ClearColors 0x180272880) = set_clr_sel_bits_all(0, 0xe, ...): bits 2 / 4 / 8 off everywhere.
- Join (FUN_1808e48b0): a line from each point / line end / arc end (start, and end unless the arc is closed: |end -
  start| >= tol) to its image; del_duplicate over the results afterwards.
- Preview check box 1774 = XfPreview, from config 10026 "Preview?" (mccore FUN_1801b7d30 case 0x272a; XfCheckVirgin;
  10027 "Maintain?" keeps the last session's values); Regen 2973 is enabled while it is off; Fit 1778 = XfAutoFit (config
  10028 "Fit?").
- **The dialog flags (decoded 2026-11-05; dumps decomp/xform/gaps/: base1.c, base_msgmap.txt, apply_bytes.txt, oninit.c, vt_all.txt).** Every
  CXf*Dlg derives from the Xform dialog base (ctor FUN_18000a8e0 MX l.9081: +0x5090 = 0, +0x5091 = 0 / +0x5092 = 1 (u16 0x100), +0x509c /
  +0x509d = 0 / 1, +0x50a4 = 1 via FUN_18000c080). +0x5090 = "the dialog was used": set by FUN_18000bd80 (every 0x66 post), by the base
  OnCommand FUN_18000b900 (vt slot 63) on any WM_COMMAND with notify 0 (a button / radio / check click) and by PreTranslateMessage
  FUN_18000bfd0 on Tab / Enter. Each OnInitDialog (vt slot 95: Rotate FUN_180033010, Mirror FUN_180014d60, Scale FUN_180036760, Translate /
  Stretch FUN_1800411c0, Translate to Plane FUN_1800410c0, Array FUN_180040f40, Distribute FUN_18000fb90, Roll FUN_18002b020) clears it once
  (+0x509d); Offset Chains' (FUN_18001e550) sets it and posts 0x66. Base message map @1802417a0: 1774 Preview FUN_18000bba0, 2973 Regen
  FUN_18000bb70, 1778 Fit, 1665 / 1683 / 1685 / 1688 / 1714 / 1717 the attributes, **12321 Apply = FUN_18000b7d0: `mov byte [rcx+0x5091], 1;
  jmp FUN_18000b230`** (OK: posts 0x65). Apply is enabled by FUN_18000b030 only while +0x5090 && FUN_18000b380 (+0x50a4) (Roll:
  FUN_180029da0 +0x5090 && |diameter| >= tol). Each do_xform_* loop: 0x65 with +0x5090 clear is handled as 100 (Cancel; MX l.479 / l.968 /
  ...); Esc in get_event (returns 2) calls OnCancel and sets bVar1, whose next pass forces 0x65 (and clears cVar11 / +0x5092): Esc = OK
  while +0x5090 is set, else Cancel; the outer loop runs again (select_ents / chain_it, the parameters kept: the reset blocks precede the
  loops) while +0x5091 was set, and posts 0x66 at the new dialog then (cVar11; cVar11 = 0 before the first round, so the first round posts
  none). PORTED (cui_xform.c xf_mod / xf_round2 / xf_escape, cui_geo.c geo_xmod / geo_escape): OK unused = Cancel, Apply enabled only when
  used, Esc = OK when used, and no preview in the first round until the dialog is used (no 0x66; 2026-11-05: 33 wf scenes
  re-blessed - each checked to differ only in entity numbers, besides the Array order below). Scenes wf xf_rot_ok_unmod, xf_esc_unmod, xf_rot_apply, xf_esc (re-blessed: Esc after
  an edit now commits).
- xform_del_duplicate after OK runs del_duplicate only when xform_check_dup is set ([inferred] config 10019 "Check for
  duplicate entities (slower)?", n in the user's config): not ported.
- Arcs under a scale / a translation (xform types 2 / 3): FUN_1808d5e90 -> FUN_1808d2330 (decomp/xform/gaps/core_arc.c): the centre
  through the matrix in the arc's own view, a scale's radius x |DAT_18143d668|, the view and the angles kept - PORTED 2026-11-05 (an arc in
  a user view is no longer refused by these).
- Arcs: the transform keeps an arc when the linear part is a similarity in the arc's plane; a non-uniform scale (the
  diagonals differ by > 1e-8, XfScaleFromMatrix) converts the arc to NURBS (convert_arc_to_nb, put_nurbs_in_db). PORTED 2026-11-05
  (mcsurf_build.c mcsb_arc_nurbs_rec, then the NURBS case below; scenes wf xf_scale_arc_nurbs, xf_scale_xyz re-blessed - the circle was
  refused before): convert_arc_to_nb (decomp/surfrev/util_conv.c l.63-140) writes nb +0 / +0x18 the ends, +0x30 = 0, +0x38 = the last
  knot, +0x40 = the radius, +0x68 u16 0x402, +0x6a flags 0x11 (| 2 closed: ||sweep| - 2 pi| < tol); put_nurbs_in_db (mccore_xform.c
  l.5747) re-reads the ends from the end control points (t1 / t2 are the end knots), +0x40 := max nb_parm_curvature (gaps/core2b.c:
  |d1 x d2| / |d1| with the derivatives scaled to a unit range; 0 for |d1| <= 1e-6) over t1 + i (t2 - t1) / (2 ncp - 2) below 1e9, at
  least 1e-9, check_nurb_knots (no change for clamped knots). NOT DECODED: eval_nurbs' two char arguments in nb_parm_curvature (ours:
  derivatives on the raw parameter); the nb_type bytes +0x48..+0x5f (left 0).
- Spline curves (decoded 2026-11-05, mccore_xform.c xform_store_ent l.1182 / l.1309 / l.1319; PORTED mcsurf_build.c mcsb_xform_curve +
  mcam_load.c mcam_new_curve + the saver's class 8 / 0x80 records, mcam_saveops.c): NURBS FUN_1808d7f00 (l.3689): only when the values per
  point are 4 for a rational / 3 for a polynomial curve (else the copy stays as it was: the 0 return is not checked); the stored ends and
  every control point through xform_pt (rational points divided by w and multiplied back); a scale (type 2) multiplies +0xa8 (nb +0x40) by
  |DAT_18143d668|. Parametric FUN_1808da030 (l.4109): per segment the constant coefficient through the whole matrix, the higher ones
  through its linear part; +0xa8 likewise; xform_pt reads / writes 3 values at the record's stride, so only dimension 3 is taken (2: NOT
  PORTED). The ends: the decompile transforms them in place and then writes back the copy taken before (NOT DECODED whether the stored
  ends move; ours: they do). DAT_18143d668 = FUN_1808d8820's param_11 (XfCreatePreview: the p_3d argument's x - the Scale dialog's factor
  +0x16d10, MX l.6564-6587; Percentage / 100; fmu_scale_db: the ratio) times itself at every pass (l.2612 / l.2908). Splines are selectable
  in the general selection (2026-11-05: mccore's idle pick / auto-highlight FUN_1804df930, decomp/xform/gaps/idle.c l.136-144, uses
  get_current_sel_mask - 0x1f1fff when none is set, & 0x8f in the standard mode: points, lines, arcs, parametric splines 8, NURBS
  0x80 - with select_ent_univ's pick rule; cui_sel.c spl_skip) and in a Transform's select_ents; the other wireframe commands here
  neither pick them nor take them from the selection (sel_list; their spline cases are not ported); their box for Scale's Auto center
  is their drawn polyline (EntityMinMax of a spline NOT DECODED). Scene wf xf_nurbs_xform (rotate / mirror / translate, saved and
  reopened).
- A rotated / mirrored arc (types 0 / 1): DECODED / PORTED 2026-11-05 (cui_xform.c xf_arc_views; decomp/xform/gaps/core_arc.c FUN_1808d20f0 /
  FUN_1808d2070 / FUN_1808e07d0, core2.c FUN_1808d1300, core_removed.c XfGetArcPoints, mccore_xform.c XfKeepView / XfStdView, mc_views.c
  views_parallel / get_view_matrix / AddMCView). FUN_1808d1300(strict 1, M, view): R = the view's rows through the linear part, each
  normalised; R equal (within tol) to the view -> the same view; else over the views 1..n_views the first one equal to R wins (offset 0),
  and the first whose normal . R.z is 1 within 1e-10 (the arc's own view first) is the candidate: offset = atan2(R.x . its Y, R.x . its X)
  in [0, 2 pi), flip = (R.x x R.y) . its normal < 0; none -> AddMCView(R, 2e-7) adds a view (MastercamPlanes::Add): NOT PORTED (refused
  with the message, as before). The centre through M; start = start + offset, or offset - start with the sweep negated on a flip;
  restrict_angle 0..2 pi. Then XfKeepView(arc, the old view, 1e38): a parallel old view in which start / middle / end
  (XfGetArcPoints: a closed arc's points at 1/3 and 2/3) lie at one depth (within tol) takes it back; else XfStdView(arc, 1e38, false):
  the first view 1 .. max(view, 9) - 1 parallel to it with the points flat (views_parallel: |normal . normal| > 0.9999996, antiparallel
  when < -0.9999996). FUN_1808e07d0 re-expresses the arc in that view: start angle from the start point, the sweep negated for an
  antiparallel view - so a negative sweep is kept in a view (e.g. a Right-view arc rotated 90 deg lands in Back exactly, then in Front, the
  first parallel view, with sweep -180). Views here: 1..8 (mcam_std_view) and the part's stored ones (F.fviews); XY results (views 1 / 4)
  are stored as mccore leaves them: XfStdView takes Top (view 1) before Bottom, so an XY result is a Top arc with its signed sweep - a
  mirrored arc runs clockwise (negative sweep), as in Mastercam's own files (the loader keeps their sign; 2026-11-05: xf_mirror_x /
  _2pts / _y_move / xf_save_reopen re-blessed - same arcs, reversed direction). The wireframe chain functions (Fillet / Chamfer /
  Offset Chains: cui_geo.c chain_ges) now flip the chain's direction flag for a clockwise arc, whose GE (geo2d.c ge_arc) is counter-
  clockwise - before, a clockwise arc from a file was offset to the wrong side (scene wf xf_mirror_cw_offset). Not ported: FUN_1808d2070's 3-point refit branch (FUN_1808d1e90: its flag
  comes from xform_store_ent's 4-argument call of FUN_1808d5e90 - NOT DECODED). Scene wf xf_arc_views (saved and reopened).
- Solids: mask 0x1f1fff includes solids (sld_xform_solid; non-uniform scale: "Solid History will be lost" 5317 / 5318).
  Not ported: this program's solids are Extrude features rebuilt from their chains.
- Units change "Scale current part" (fmu_scale_db, formats/units.md 4): the FUN_1808d8820 call is xform type 2 with param_11 = the ratio
  (disassembly decomp/xform/gaps/fmu_call.txt / fmu_call2.txt: [RSP+0x48] = 2, [RSP+0x50] = XMM6 = the diagonal's value). PORTED
  2026-11-05 for splines and surfaces (kinds 0 / 1 / 10; a trimmed surface's blanked base goes with it) besides points / lines / arcs
  (cui_xform.c xf_scale_all); solids (and sld_scale_sldBodies on "No") stay NOT PORTED. Scene srf srf_units_scale.

### Translate (do_xform_trans @180047570; CXfTransParams / CXfTransDlg, templates 6433 / 6434)
- Params (ctor FUN_18003ac00(&p, 1); XFORM.PRM keys FUN_180046310 / FUN_1800452b0): +0x20 MoveCopyJoin (1, NOT reset per
  run), +0x24 Copies 1 (1..30000), +0x28 ByTotal 0 ("Distance between" 1396 / "Total distance" 1399, enabled copies > 1),
  +0x30 Delta 0, +0x48 Direction 0, +0x4c Stretch 0, +0x4d UseAttrs, +0x4e IncrLevels, +0x50 Level -1, +0x54 Color -1. Every
  run resets all but MoveCopyJoin.
- Dialog: Delta X / Y / Z 2485 / 2488 / 2492; Polar angle 1540 (+-360) / length 1581; From / To 1469 (FROM; the first time
  then TO, mccoreres 3929 "Select the point to translate from" / 3930 NOT in the string tables), 1476 TO (never set ->
  the FROM flow), 1472 a line (mccoreres 1538 "Define the fit vector"; the picked end = FROM: parameter >= 0.5 -> end 2);
  a successful pick = From/To mode, Direction := 0, delta = TO - FROM in the Cplane (2D construction: TO.z := FROM.z when
  the XY differ). Delta edit -> polar refreshed (L = |XY|, A = atan2 in [0, 360)); polar edit -> X = L cos A, Y = L sin A,
  Z = 0 (L < tol: all 0). 2D construction mode: a Z alone zeroes X / Y, an X / Y zeroes Z. Direction 1623 (MCButton 3
  states): 0 one way, 1 reverse, 2 both.
- Matrix FUN_18003d210: v = delta (/ copies with ByTotal and copies > 1); forward entries T = i v, reverse T = -i v,
  i = 1..copies; xform_type 3.
- Selection prompt mccadutilres 3948 "Translate: select entities to translate".

### Translate to Plane (do_xform_trans3d @180047d80; CXfTrans3dParams / CXfTrans3dDlg, 6435 / 6437)
- Params (FUN_18003a530): MoveCopy (1, kept), Copies 1, FromView / ToView = the active view, FromPoint / ToPoint 0, UseAttrs,
  IncrLevels, Level / Color -1. No Join, no Direction.
- Dialog: Source / Destination plane combos 10101 / 10100 (every plane; "UNNAMED" mccoreres 273 for an unmatched view),
  plane dialogs 1469 / 1476 (mccoreres 152 / 153), origin points 716 / 717 (mccoreres 3929 / 3930), by points 1500 / 1472 /
  1512 (mccadutilres 1557-1567, three-point frames).
- Matrix FUN_18003cc10: same view -> T = To - From (type 3); else p' = To + Mdst^T Msrc (p - From) (type 1); copy i = Mⁱ.

### Move to Origin (do_xform_movetoorigin @180046b70)
- No dialog: every shown entity (set_clr_sel_bits(4, ...)), point_proc(mccoreres 3929), destination = the active plane's
  origin (2D construction: the picked point's view Z kept), translate_silent = a move, one undo step.

### Rotate (do_xform_rotate @180034490; CXfRotateParams / CXfRotateDlg, 6441 / 6443)
- Params (FUN_180031320; keys FUN_180034390 / FUN_180034060): MoveCopyJoin (kept), Copies 1, ByTotal 0 ("Angle between" /
  "Total sweep"), Origin (0,0,0 each run), Angle 0 (each run), Translate 0 (711 Rotate / 712 Translate; Translate + Join ->
  Copy, Join enabled only with Rotate), Direction 0, UseAttrs / IncrLevels / Level / Color.
- Centre 716: mccoreres 3932 "Select the point to rotate about". Prompt mccadutilres 3946.
- Matrix FUN_180031900: step = Angle (/ copies with Total sweep and copies > 1); |angle| < 5.7296e-5 deg -> identity
  copies; rotation about the centre in the Cplane (CCW +), forward Rⁱ, reverse R(-step)ⁱ. Translate (FUN_180032010): T =
  rotate_pt(ref, centre, +-i step) - ref, ref = XfBBox_GetParams output 3 ([inferred] the selection box centre).
- Remove Item / Reset Items (725 / 726): DECODED / PORTED 2026-11-05 (also Rectangular Array's; dumps decomp/xform/gaps/core_removed.c,
  core2.c, core3.c; rotate.c FUN_1800335f0 / FUN_180033840, trans_dlg_b.c l.1240-1307). Remove = FUN_1800335f0: XfWfMgrSelectCopy(mccoreres
  1534 "Select copy to remove or Press <ENTER>") loops get_event(mask 10, 0x1f1fff, 8): an entity whose ent +0x50 (set by FUN_1808eb180 =
  XfInfoIndex + 1 while XfInfoApplyPartNumber, which Rotate / Array set) is 1..XfInfoSize names its XfInfo entry; XfInfoRemovedAdd stores
  that entry's +0x70 / +0x72 shorts; XfDeletePreview and the preview again (XfPreview or XfTempPreview 1); the pick repeats until Enter
  (key 0xd -> 3, back to the dialog); Esc (2) posts 100 = the command's Cancel. The entry keys: Rotate FUN_180031900 / FUN_180032010 forward
  +0x70 = i, reverse -i, +0x72 0; Array FUN_18003c160 / FUN_18003c4e0: base-row columns (c + 1, 1) / (-1 - c, 1), row k (1, k + 1) /
  (1, -1 - k), that row's columns (c + 1, row) / (-1 - c, row). The entry loops FUN_1808eb340 / FUN_1808eb720 skip an entry whose pair is in
  the list (FUN_1808eb7b0), so a removal survives parameter changes. Reset = FUN_180033840: XfInfoRemovedFree + 0x66. The list is also
  freed by Add/Remove Geometry (FUN_180032ed0) and at every round's end (MX l.1034 / l.1529). Enables: Rotate FUN_1800315e0 (copies >= 2
  and XfPreviewValid / XfRemovedYes), Array FUN_18003b890 (#1 or #2 >= 2, the same). Left out: the WfMgr hover highlight of the nearest
  copy (FindClosestToCursor, event 0x100); a zero angle's XfInfoFillWithI33 copies (their keys NOT DECODED: Remove refused with a message).
  Scenes wf xf_rot_rmitem, xf_rot_rmreset, xf_arr_rmitem.

### Mirror (do_xform_mirror @180017700; CXfMirrorParams / CXfMirrorDlg, 6439 / 6440)
- Params (FUN_180013c10): MoveCopyJoin (kept), Mode -1 each run (no axis), ReferencePoint = active origin, Angle 45,
  From / To points, UseAttrs / Level / Color.
- Axis radios 1428 / 1433 / 1437 / 1442 / 1445 = 0 X axis (value Y 2488), 1 Y axis (X 2485), 2 Polar (A 1540, through the
  reference point), 3 Line (1460, mccoreres 3931; enabled once picked), 4 Two points (1463, mccoreres 3936 / 3937; equal
  points -> X axis). Point buttons 1450 / 1453 / 1456 (mccoreres 1489) set the reference point and the mode; editing a
  value switches to its mode; 360 -> 0. A line clicked while the dialog is up = the Line button. "Mirror note and label
  text" 3018 (DfMirrorText; notes / labels only).
- Matrix FUN_180014230 from the axis line (FUN_180016f80: ref +- (2, 0) / (0, 2) / 2 (cos A, sin A)): k = 2 / L, m00 =
  k dx² - 1, m11 = k dy² - 1, m01 = m10 = k dx dy, t from the first point; zero length -> mccadutilres 3912 "Zero length
  line!". One copy, xform_type 0. Prompt mccadutilres 3945.

### Rectangular Array (do_xform_array @1800463f0; CXfArrayParams / CXfArrayDlg, 6457 / 6458)
- Params (FUN_18003a210, all reset per run): Copies(1) / (2) = 2, Length(1) / (2) = RegisterVariable(25 mm, 1 in),
  Angle(1) 0 (+-359.99994), Angle(2) 90 (relative to Angle(1), 5.7296e-5..179.99994), Direction(1) / (2) 0 (enabled when
  that count > 1), UseAttrs / IncrLevels / Level / Color. Always a copy (XfDoFinalAction(3, 1, ...)).
- Copy order (FUN_18003c160): all the forward columns, then all the reverse ones (two loops); rows FUN_18003c4e0 the same - this
  sets "Increment level per copy" and the entity order (ported 2026-11-05; xf_array_dirs re-blessed).
- Matrix FUN_18003c8b0 / FUN_18003c160 / FUN_18003c4e0: v1 = L1 (cos A1, sin A1), v2 = L2 (cos (A1 + A2), sin ...); the
  original is instance (1, 1); rows / columns by the directions (forward / reverse / both).

### Scale (do_xform_scale @180038080; CXfScaleParams / CXfScaleDlg, 6445 / 6447 / 6449 / 6451, calculator 6452)
- Params (FUN_1800350d0): MoveCopyJoin (kept), Copies 1, Origin 0, XYZ 0 (718 Uniform / 720 XYZ), Percentages 0 (1301
  Factor(s) / 3024 Percentage(s); switching converts the values x100 / /100), Scale 1.0, Scale X / Y / Z 1.0 (minimum
  tol), UseAttrs / IncrLevels (only with copies >= 2) / Level / Color. "Auto center" 3025 = global DAT_180243a50 (.data 1,
  kept for the session); 716 "Define scaling reference point" (mccoreres 3921) sets the point and Auto center off.
- Centre: Auto center = the centre of the union of the selected entities' world boxes (CalcAutoCursorTemporaryCenterPoint
  -> FUN_18001a7e0; nothing -> 0,0,0), else the picked point.
- Matrix FUN_180035820: diag(s) (XYZ: diag(sx, sy, sz) in the Cplane), translation c (1 - s); copies Mⁱ (cumulative
  factor); all factors within tol of 1 -> identities. Prompt mccadutilres 3947.
- Scale Calculator (CXfScaleCompDlg, template 6452): DECODED / PORTED 2026-11-05 (decomp/xform/distribute_b.c, stretch_distribute.c
  FUN_180038ca0 (OnInitDialog), gaps/scalecalc.c, message map @18017bf20 gaps/maps1.txt). Opened by the Calculator buttons (FUN_180035fb0 the
  Uniform Scale, FUN_1800360f0 / 180036230 / 180036370 X / Y / Z) as a modal dialog; ctor FUN_1800387c0: Base 1005 (+0x4310) = Result 1006
  (+0x5108) = Scale factor 1007 (+0x5f00) = 1.0, minimums tol; combos 1001 / 1002 = mccoreres 12135 "Line", 523 "Points", 5127 "Diameter",
  6025 "Radius", both 0. A combo change (CBN_SELCHANGE, FUN_1800390b0 / 180039060) or Select 1003 / 1004 (FUN_180039100 / 180038b90)
  measures with FUN_1800392c0: 0 a line (FUN_180039610, get_event 0x4002 / 0x400002, type 2), 1 two points (FUN_180039920, prompts 1878 /
  1879), 2 / 3 an arc's radius x 2 / x 1 (FUN_180039310, 0x6002 / 0x400004; a solid face's radius via sld_face_radius: no counterpart here);
  lengths in the Cplane's 2D unless 3D construction, kept only >= tol; prompts "%s\n%s" of mccadutilres 458 / 459 "Define the base / result
  length" and the method's string. The value is kept >= tol (and <= the MCReal maximum: MCReal is mastercam.dll, NOT DECODED - not
  clamped), factor = result / base; the base / result edits (EN_KILLFOCUS, a change >= tol) recompute it. OK: the factor (x 100 with
  Percentage(s)) into the field, kept within that combo's bounds, 0x66. Ours: the dialog shows in the Scale panel's place (a modal dialog
  there). Scenes wf xf_scale_calc, xf_scale_calc_xyz.

### Stretch (do_xform_stretch @180046df0; CXfTransParams via CXfStretchDlg, 6558 / 6559)
- Params: CXfTransParams ctor (&p, 0) -> MoveCopyJoin 0 Move (kept); Stretch +0x4c = 1; Join disabled; no Direction.
  Dialog = Translate's controls (delta, From / To, Polar, copies, ByTotal).
- Selection: window mode In + Intersect with dbselwin_stretching (mastercam.dll FUN_18020e440 / FUN_18020d670): wholly
  inside -> the entity moves whole; a crossing line with its start inside -> sel bit 0x200, end inside -> 0x40 (several
  windows add up); other crossing entities are not selected. No line with exactly one end inside -> the command ends
  without the dialog. Prompt mccadutilres 3949 "Stretch:  Window intersect entities to stretch".
- xform_store_ent with the stretch flag (type 3 only): a flagged line moves only that end; shorter than tol -> deleted.

### Not ported (decoded where noted)
- Dynamic (DynamicXform @1800eea50): decoded 2026-11-04 except the gnomon's drawing / handle hit areas and the ruler / ring drag
  maths and increments (section "Dynamic - port spec" below; dumps decomp/xform/dynamic/) - greyed: a port would need our own gnomon
  look and drag rules (a deviation: the user decides).
- Roll: PORTED 2026-11-04 (viewer/core_ui/cui_xroll.c, a chain command of cui_geo.c; spec "Roll - port spec" below). Apply PORTED
  2026-11-05 (the dialog flags in "Common machinery"; scene wf roll_apply). Not ported: Type Splines (no wireframe splines are made here:
  refused with a message), the "Select an arc" button (greyed: see "Roll - port spec" 4 - the Cplane / origin handling and Matrix33
  AlignAxis / IsValid / Plane::CalcAndTransform it depends on are not ported or decoded), the cylinder outline (temp graphics), the Ctrl seam
  split (the seam lines NOT DECODED). "Select two points" (829) PORTED 2026-11-05 (scene wf roll_twopts). Scenes wf roll_x /
  roll_un (no Mastercam output to compare: unverified).
- Distribute: PORTED 2026-11-04 (cui_xform.c build_distribute / G_XDIST; spec "Distribute - port spec" below; fv = the frame of
  get_unique_view_from_2L, mastercam.dll 0x18033a4e0, decompiled: GetMatrixFrom2lines(P1->P2, P1->left normal) + GetViewNo). Not
  ported: the box reference lines / margin marker lines (not drawn), so the edge pick 1251 is greyed (Align geometry uses edge 1);
  the fit vector's point pick is the raw click when it misses a line (ours); the 2nd point's prompt 3930 NOT DECODED (empty: see
  "Roll - port spec" 4 for the candidates). proj_pt_ln / dist_pt_l decoded 2026-11-05 and made literal (no output change).
  Scenes wf xf_dist / _spec / _align (no Mastercam output to compare: unverified).
- Geometry Nesting (MCCORE do_nest_g -> mastercam.dll run_nesting_func -> NESTING.DLL m_nest_g -> the third-party NestLib n64dll.dll):
  PORTED (2026-11-05): NestLib itself was decompiled and ported bit-exact (formats/nesting_engine.md, user decision N-1); the
  NESTING.DLL side is viewer/core_ui/cui_nest.c + viewer-qt/src/nestdlg.cpp; status and what is left out: formats/nesting_addin.md
  §12 (Optimized pairs, the sheet-name note, Results Copy / Copy Blank ported; part labels, sessions, groups / clusters, trims,
  reports, Customize / Drag not). The 2026-11-04 scope below is superseded where it says the engine cannot be ported.
- Toolpaths are not transformed by these commands (Toolpaths > Transform is a separate operation type).

### Distribute — port spec (2026-11-04)
Sources: MX = `decomp/xform/mccadutil_xform.c`, DM = `decomp/xform/distribute_math.c` (FUN_18000e090, also MX l.3457), SD =
`decomp/xform/stretch_distribute.c` (the CXfFitDlg handlers). Decompiled 2026-11-04 with Ghidra (project ghidra/mc2017,
mccadutil.dll; not saved as dumps): CXfFitDlg vftable @180177428; DoDataExchange FUN_18000dec0 (+ base FUN_18000b110);
OnInitDialog FUN_18000fb90; message map FUN_18000f450 + static entries @180241980 (MemDump); margin-end spin FUN_180010820;
template switch FUN_180012720 (vt +0x380). mccore (ghidra_core) mul_xmatrix @1808eb830. Strings: tools/pe_strings.py on
mccadutilres.dll / mccoreres.dll; template rows: exports/mccadutilres_dialogs.tsv.

**Parameters** — struct DAT_180254930, copied to / from the dialog by FUN_1800127f0 (MX l.3310) / FUN_1800125e0 (MX l.3415).
Reset at every command start (MX l.92-105; only when no CXfFitDlg is up), NOT between Apply rounds (the reset is before the
loop, MX l.106). Dialog offsets are bytes into CXfFitDlg (ctor FUN_18000cc80, MX l.2656).

| struct | dlg | type | reset | meaning / control (DDX FUN_18000dec0, template 25527 / 25528) |
|---|---|---|---|---|
| +0x00 | +0x18a28 | p_3d | 0 | copied both ways, not used by the math: NOT DECODED |
| +0x18 | +0x189f8 | double | 0 | start margin, combo 10314 (CSkRCmb +0x70f8) |
| +0x20 | +0x18a00 | double | 0 | end margin, combo 10315 (CSkRCmb +0xac50) |
| +0x28 | +0x18a1d | byte | 1 | 1 = "Number of instances equally spaced" (radio 10324, FUN_180010a60), 0 = "Specific instance spacing" (10325, FUN_180010470); spacing combo enabled only at 0 |
| +0x2c | +0xf3a8 | int | 1 | instances N, edit 1375 (MCInt +0xe7b0; ctor min +0xf3a0 = 1, max +0xf3a4 = 30000, MX l.2726-2728; EnableBuddyCtl(1, 30000)) |
| +0x30 | +0x18a08 | double | 0 | spacing s, combo 1581 (CSkRCmb +0xf518); rewritten by the math in equal mode |
| +0x38 | +0x18a54 | int | 1 | justification, trackbar 10318 (range 0..2, TBM 0x407 / 0x408 in FUN_18000fb90; read on SB_ENDSCROLL, FUN_18000fb10): 0 1st endpoint, 1 center, 2 2nd endpoint (tooltip mccadutilres 398) |
| +0x3c | +0x12188 | byte/int | 0 | "Adjust instance offset" check 10319 (FUN_180010ac0 enables 1582 + 716) |
| +0x40 | +0x18a10 | double | 0 | vector offset, combo 1582 (CSkRCmb +0x13198); written by the math while 10319 is off |
| +0x48 | +0x16d38 | byte/int | 0 | 1 = "Align Box"; combo 10329 sel (+0x18a58) = (+0x16d38 != 0) on load |
| +0x49 | +0x16d3c | byte/int | 0 | 1 = Move (radio 100, FUN_180010530), 0 = Copy (101, FUN_18000f8f0); DDX radio int +0x60e8 = !Move |
| +0x4a | +0x16d40 | byte/int | 0 | 0 = Contain, 1 = Center instances on the endpoints: bitmap button 285 toggles it (FUN_1800104d0) [names inferred from the math below + tooltip "Contain/Center instances on fit vector endpoints"] |
| +0x4b | +0x34dc | byte/int | 0 | Use New Attributes 1665 (FUN_18000b110) |
| +0x4c | +0x4248 | byte/int | 0 | Increment level per copy 1688 |
| +0x50 | +0x40d8 | int | -1 | Level 1685 (< 1 -> main level, FUN_1800127f0) |
| +0x54 | +0x4e48 | int | -1 | Color 1717 (< 0 -> main colour) |

Dialog-only state: +0x18a60 / +0x18a78 = fit points P1 / P2 (world; FUN_1800129c0 MX l.2807, FUN_18000f7f0); +0x18a1c = "no
vector yet" (ctor 1, cleared when the vector is set; FUN_18000e090 returns at once while set, DM l.152); +0x18a20 = edge
alignment (ctor -1 = none; 0..3); +0x189f0 = xform type for XfCreatePreview / XfDoFinalAction (ctor 3; 1 while "Align
geometry", FUN_180010d10 / FUN_180010de0); +0x18a90 = vector-offset point, +0x18ada = it was picked (ctor 0; cleared by any
edit / spin of 1582, FUN_180010360 / FUN_180010950; toggling 10319 sets it to the check's state,
FUN_180010ac0, so with no point picked the offset is measured from +0x18a90 = its ctor value 0,0,0); +0x18aa8 / +0x18ac0 = box lo / hi (world), +0x18ad8 = their view;
+0x18ae0 (+0x18ae8 data, +0x18af0 count) = the box reference lines; temp ids +0x18a40 / 44 / 48 (vector), +0x18a4c / 50
(margin lines); +0x34d0 = XfPreview, +0x34d8 = XfAutoFit. Combo 10329 items mccadutilres 399 "Original", 400 "Align Box",
401 "Align geometry" (FUN_18000fb90). FUN_180010d10 (CBN_SELCHANGE 10329): returns if +0x18a54 == -1 (sic); sel 1 ->
+0x16d38 = 1; sel 2 -> +0x16d38 = 0, +0x18a20 = 1, +0x189f0 = 1, button 1251 enabled; sel 0 / 1 -> +0x18a20 = -1, +0x189f0 = 3,
1251 disabled.

Edits (message map): margins on CBN_KILLFOCUS (FUN_180010200 / 1800102b0) and spin (FUN_1800106f0 / 180010820): while the
lock bitmap check 10317 (+0x9c90, checked at init by BM_SETCHECK in FUN_18000fb90) is checked, the other margin := this one;
clicking 10317 checked copies start -> end (FUN_18000f960). Spacing / offset / count on kill focus or Enter (FUN_180011280).
A change < tol is ignored (the old value restored for spacing, FUN_180010160). Spin step = deltaPos x spin_metric_float /
spin_inch_float, clamped to the CSkRCmb's own +0x2988 / +0x2990 bounds (FUN_1800106f0 / 1800105f0 / 180010950 / 180010820);
those bounds are set by `UICombobox::EnableBuddyCtl(.., 0, 30000)` for all four combos (FUN_18000fb90) [inferred: the
uictrls function was not decompiled]. Typed values: no clamp in mccadutil (DDX_ProcessSkRCmb, uictrls: NOT DECODED).

**Flow** (TransformDistribute MX l.3-301)
1. select_ents(mccadutilres 4109 "Select the geometry to be fit along the vector", mask 0x1f1fff, MC_PRESEL 2) (l.108-115),
   DfXformCheck.
2. Fit vector FUN_180012b80 (MX l.3115): prompt mccoreres 1538 "Define the fit vector", get_event(.., 3, 2, 1): a line ->
   P1 / P2 = its ends, reversed when the pick parameter >= 0.5 (P1 = the end picked near); temp points at both. A point ->
   P1, then a rubber band and a second point (prompt GetCoreString(0xf5a = 3930): not in mccoreres' string table - NOT
   DECODED; 3929 / 3931 exist) -> P2; temp line P1-P2. Both are moved to the construction depth: view Z := constr_depth (+ the
   active origin's view Z when ActiveOriginIsOn). Kept only if |P1 P2| > tol (else unchanged: 0,0,0 at the start). Fail /
   Esc -> command ends. Button 1472 re-runs it (FUN_18000f7f0) and clears +0x18a1c.
3. CXfFitDlg (template 25527, 25528 when Use New Attributes: FUN_180012720), FUN_1800127f0, FUN_1800129c0 (P1 / P2 -> dlg,
   +0x18a1c = 0, posts 0x66). Loop get_event(0x8200) (l.152): 0x66 = XfDeletePreview, delete the box lines (FUN_1800113b0);
   if XfPreview (or XfTempPreview 1): FUN_18000e090, XfCreatePreview(+0x189f0, MoveCopyJoin, 0,0,0, 0.0, 0, 0, NULL),
   FUN_18000d940 (box lines) (l.199-226). 0x65 (OK / Apply): no valid preview -> FUN_18000e090 again; delete box lines;
   XfDoFinalAction(+0x189f0, MoveCopyJoin, ..); FUN_180011a40 (Use New Attributes: XfApplyColorLevelToResult_ExOpIdn(colour,
   level, incr = +0x4248 && N >= 2)), UndoXformTransCommit, xform_del_duplicate (l.171-197). 0x65 with dlg +0x5090 clear is
   handled as 100 (l.161-164). 100 = XfDeletePreview + delete box lines. View change (0x8000 / 0x15) -> 0x66; 0x14 -> XfFit
   with Fit on. MoveCopyJoin = (Move flag == 0): 0 Move, 1 Copy, never Join (l.182 / l.218). Apply (+0x5091) -> back to 1.
4. Buttons: 1250 re-select (FUN_18000f9e0, sets +0x189f4); 716 vector-offset point (FUN_180010b40: prompt id 0x14c9 =
   5321 - not in mccadutilres; mccoreres 5321 = "Select point" [inferred: MFC string lookup falls back to the module chain],
   get_event point -> +0x18a90, +0x18ada = 1); 1251 edge (FUN_180010de0: prompt mccadutilres 4110 "Select an edge of the
   result bounding box to be aligned with the fit vector"; picks one of the box reference lines; e = index mod 4; if
   +0x18a20 is 0 or 2: +0x18a20 = {1, 0, 3, 2}[e], else +0x18a20 = e; then +0x189f0 = 1, +0x16d38 = 0) (SD l.1478-1592).

**Box** FUN_180012090(dlg, view, lo, hi) (MX l.10027): the union of GetEntityBoundingBox over the dl_db list 4 (else list 2)
with mask 0x1f1fff, evaluated under ActivePlaneSaveRestore(view's matrix) = a box in that view's coordinates [inferred];
per-axis min / max then swapped if lo > hi; nothing -> lo = hi = 0. Which entities lists 4 / 2 hold: NOT DECODED [inferred:
the selected originals].

**Math** FUN_18000e090 (DM; x_matrix p' = p·M + t with rows m[0..2] = image of X: cui's XM.m = transpose; mul_xmatrix(a, b,
out) = a after b = xm_mul(a, b), mccore 1808eb830). av = active view, fv = view from get_unique_view_from_2L.
```
if dlg.novector: return                                        # l.152
A = w2v(av, P1); B = w2v(av, P2); v = B - A (2D)
ang = |v|² >= 1e-12 ? atan2(v.y, v.x) (+2π if < 0) : 0           # l.163-176
ang += {0: π/2, 2: 3π/2, 3: π}[edge] (edge 1 / -1: +0); while ang > 2π + tol: ang -= 2π     # l.177-189
L1 = line P1->P2; L2 = line P1 -> P1 + v2w(av, (-v.y, v.x, 0)); fv = get_unique_view_from_2L(L1, L2)   # l.190-213
  [inferred: fv's X along the fit vector, Y = its left normal; the function is mccore, NOT DECODED]
if !alignBox:                                                   # l.214-412
   lo, hi = box(av); store v2w(av, lo / hi), view av
   c1 = (lo.x, hi.y, lo.z), c2 = (hi.x, lo.y, lo.z); lo, hi = v2v(av -> fv, lo / hi)
   if edge < 0: per-axis sort lo / hi, then grow by v2v(c1), v2v(c2)  (AABB in fv of those 4 corners)
else: lo, hi = box(fv); store, view fv                          # l.413-418
A = w2v(fv, P1); B = w2v(fv, P2); c = (lo + hi) / 2 (3D)          # l.419-425
F = proj_pt_ln(c.xy, line A B); (dx, dy) = ok ? c - F : (0, 0)  # l.434-438
if !adjustOffset: offset = dy                                  # l.439-441 (shown in 1582)
else: if offsetPicked: offset = dist_pt_l(w2v(fv, Poff), line A B), negated if Poff.y < A.y   # l.443-449
      dy = offset
if offset < 0: ang += π (reduced as above)                       # l.452-456
R = FUN_180012a60(v2v(fv -> av, c), ang)  # CCW rotation by ang about that point in av, then view_xm_to_world (MX l.9966):
      m = [cos, sin, 0; -sin, cos, 0; 0, 0, 1], t = (cx(1-cos) + cy sin, cy(1-cos) - cx sin, 0)
if edge >= 0: lo, hi = w2v(fv, R(v2w(fv, lo / hi))) then per-axis sort; view = fv   # l.460-521 (two corners only)
w = hi.x - lo.x; Lx = (B.x - A.x) + mS + mE (+ w if Center); Vy = B.y - A.y   # l.522-529
if !equal:                                   # count by fit, l.535-554
   if N < 1: return
   len = sqrt(Lx² + Vy²); acc = 0; n = 0
   do { acc += w; if acc >= len + tol: break; acc += s; n++ } while n < N
else n = N
if n < 1: return                             # both returns precede XfInfoFree: the last XfInfo stays (preview NOT rebuilt)
if !equal: step = (w + s) / len * (Lx, Vy, 0)
else: step = (Lx - w, Vy, 0); if n > 2: step /= (n - 1)
      s = (n < 2) ? 0 : |step| - w           # written back to 1581, l.574-581
UpdateData(FALSE); if just == 2: step = -step                       # l.583-588
S = translation v2w(fv, step) [the view_to_world of a vector: whether an origin is added NOT DECODED]
h = (hi.y - lo.y) * 1.1; hh = |dy| < h ? h : dy                     # l.600-604 (dy signed)
temp line id +0x18a4c: x = A.x - mS, y = A.y ± hh, z = A.z; id +0x18a50: x = B.x + mE, y = B.y ± hh, z = B.z (fv -> world)
just 0: tx = A.x - lo.x - mS (- w/2 if Center);            ty = A.y - c.y     # l.619-629
just 1: a0 = A.x - mS; tx = ((B.x + mE) - a0)/2 + a0 - c.x
        - (equal ? (n-1) step.x / 2 : (w + s)(n-1) / 2);   ty = A.y - c.y     # l.632-642
just 2: tx = B.x - hi.x + mE (+ w/2 if Center);            ty = B.y - c.y     # l.644-650
T0 = translation v2w(fv, (dx + tx, dy + ty, 0)); M0 = edge >= 0 ? T0 after R : T0   # l.652-669
XfInfoFree; XfInfoALLOC(1, n, 0, 0); entry 0 = M0; entry i = S^i after M0 (i = 1..n-1); +0x74 = 0   # l.670-740
```
The originals are not an instance: n copies (Copy) of the selection; Move per the common machinery.

**Box reference lines** FUN_18000d940 (MX l.2893): per XfInfo entry k, lo / hi (world, +0x18aa8 / +0x18ac0) through entry
k's matrix, to view +0x18ad8; four XfSaveReferenceLine(.., 8, 2, 1, 2.0) (colour 8 [the other args NOT DECODED]), skipped
when < tol, appended to +0x18ae0 in order (lo.x,lo.y)-(lo.x,hi.y), (lo.x,lo.y)-(hi.x,lo.y), (hi.x,hi.y)-(hi.x,lo.y),
(hi.x,hi.y)-(lo.x,hi.y) at Z = lo.z (index mod 4 = the edge e of FUN_180010de0; a skipped line shifts the indices).

**Port mapping** (cui_xform.c): a `build_distribute()` in xf_build_list pushing the n XMs with xi_add (XM.m = transpose of
the x_matrix rows; T0 / S with xm_trans; "T0 after R" = xm_mul(&T0, &R)); Top Cplane: av = world, w2v / v2w via
cam_plane_matrix; fv needs a view from the vector (X = P2 - P1 projected into av, Z = av's Z) [inferred]; the box from the
originals (sel_box_centre's loop gives lo / hi; Mastercam: GetEntityBoundingBox); attributes as xf_attrs (incr only with
N >= 2); XfDoFinalAction type +0x189f0 (3 / 1) only matters to mccore's arc handling.

**Decoded 2026-11-05**: proj_pt_ln (systypes.dll 0x180002780, decomp/xform/gaps/projptln.c): t = ((c - A) . e) / |e|^2, F = (1 - t) A
+ t B, not ok when |e|^2 == 0; dist_pt_l (mccore 0x1804b32a0, gaps/distpt.c): |(q - A) x e| / |e|, |q - A| when |e| <= tol; view_to_world
of a vector: world = M^T v (mastercam.dll 0x180341520, formats/mcam.md "Arc views": a view has no origin) - no origin added. The port
uses them literally (cui_xform.c build_distribute).
**Not decoded**: prompt 3930 (the fit vector's 2nd point: GetCoreString(3930) is empty in mccoreres, MFC's string search then takes the
first module of the resource chain with a non-empty 3930 - mccadutilres "Select the point to translate to", opmanres, _axui_res,
mwexceptions_res have one; the chain's order is not decoded); struct +0x00 p_3d; dl_db lists 4 / 2; FUN_18000f3e0's role (+0x189f4,
XfBBox_CalcParams - no effect on the math); typed-value clamps (DDX_ProcessSkRCmb) and the EnableBuddyCtl bound slots
[inferred 0..30000]; the bitmap statics #10303 / #10304 / #10305 (tooltips 397 / 398 / 395) and the button bitmaps
0x283b-0x2841; the +0x18a54 == -1 test in FUN_180010d10; XfInfoALLOC's other args. (The Esc path, l.233-236, is decoded with the
dialog flags - "Common machinery": Esc = OK while +0x5090 is set; ported 2026-11-05.)

### Roll — port spec (2026-11-04)
Sources: `decomp/xform/mccadutil_xform.c` (MC = do_xform_roll l.579-815 and callees), `decomp/xform/roll.c` (R),
`roll_b.c` (RB), `roll_c.c` (RC), `rot_mirror_vt.c` (VT: the CXfRollDlg vftable functions), `mccore_roll.c` (CR). Decompiled
2026-11-04 with Ghidra, not saved as dumps: mccadutil `break_spline_const_error_lines` @180062aa0 (export 15),
`convert_entity_to_nurbs` @180059150 (export 25); mccore `FUN_1805faf10`, `n_segs_to_unwrap_arc` @1805fce20; MemDump of
mccadutil .data 0x180243380 / 0x1802433a0 / 0x180243438. Help: chm cadfields/MCCore/Xform_fields/*.htm (Roll, Unroll,
Angle_tolerance, Rotary_diameter, Roll_unroll_angle, Roll_unroll_between_points, Type_(Roll), Rotate_about_X_or_Y_axis).

**Parameters** (struct DAT_1802433a0 = "P"; dialog <-> P: FUN_18002ede0 MC l.5848 / FUN_18002e3a0 MC l.5461; .data initial
values = MemDump; "reset" = the per-run block MC l.642-653, byte writes checked in the disassembly at 0x180030a1d-0x180030a72):

| P | meaning | dialog | .data | reset |
|---|---|---|---|---|
| +0x00 s16 | 0 Move / 1 Copy (OK deletes the originals when 0, MC l.5374) | radios 100 "Delete original geometry" / 101 "Keep" -> dlg +0x61c0 | 1 | kept |
| +0x02 u8 | 1 Roll / 0 Unroll (= dlg value == 0) | radios 817 / 818 (bitmaps 815 #1210 / 816 #1211) -> +0x61c4 | 1 | 1 |
| +0x03 u8 | 1 = axis X, 0 = axis Y (= dlg value == 0) | "Rotate about" 819 X axis / 821 Y axis -> +0x61c8 | 1 | kept |
| +0x04 u8 | 1 = CW, 0 = CCW (= dlg value == 0) | "Direction" 790 CW / 793 CCW -> +0x61cc | 1 | kept |
| +0x08 f64 | radius r | combo 825 = diameter (dlg +0x104b0 = 2 r) | 1.0 | 0 |
| +0x10 f64 | angle tolerance (help: "how far to roll ... before creating a point") | combo 827 (bitmap 717 #1212) | 0.5 | kept |
| +0x18 f64 | angle, degrees (help: positions the result on the cylinder); >= 1e38 -> 0 | combo 804 (bitmap 719 #25201) | 1e38 | 1e38 |
| +0x20 u8 | Positioning: 0 angle / 1 two points (= dlg value == 1) | radios 797 / 801 -> +0xc908 | 0 | 0 |
| +0x28 / +0x40 | P1 (from) / P2 (to) points | 829 "Select two points" | 0 | 0 |
| +0x58 s32 | Type: 0 Lines / Arcs, 1 Points, 2 Splines | combo 3036 (strings mccadutilres 147 / 157 / 158) | 0 | 0 |
| +0x5c u8 | Use New Attributes (template 6468 when set, 6467 when not: FUN_18002ebc0 VT l.559) | 1665 | 0 | 0 |
| +0x60 / +0x64 s32 | Level (< 1: main level) / Colour (< 0: main colour) (FUN_18002ede0) | 1685 / 1717 | -1 / -1 | -1 / -1 |

- DDX FUN_180029e70 (VT l.414). Dialog ctor FUN_180029690 (MC l.5046): template 6468, its own defaults (diameter 1.0, angle
  tolerance 0.01, min 5.7296e-5 = param[0x18df]) are overwritten by FUN_18002ede0 before the dialog shows. Spins (FUN_18002b020
  VT l.494, R l.785-893): diameter / tolerance -100..100 steps of spin_metric_float / spin_inch_float, diameter >= 0, tolerance
  >= 5.7296e-5; angle -360..360 steps of spin_*_ang. The upper clamps (param[0x136d] / [0x18e0] / [0x1e54]) are NOT DECODED.
- Enables (FUN_180029da0 VT l.596): 801 only after two points were picked once (dlg +0x10500); 804 only in angle mode; Apply
  12321 only with dlg +0x5090 set and |diameter| >= tol (+0x5090 = the dialog was used: "Common machinery: the dialog flags"; Apply ported).
- Change handlers rebuild the preview (FUN_18000bd80(dlg, 0x66)): Roll/Unroll R l.80/98, axis R l.116/135, direction R l.154/172,
  diameter R l.212, tolerance R l.275, angle R l.599, type R l.639. Move/Copy (R l.40/60) only blanks / unblanks the originals
  (XfBlankUnblankGroup). Diameter / axis changes also mark the cylinder outline dirty (DAT_180254fdc = 1).

**User flow** (MC l.579-815)
1. Reset (above); origin O = the active plane origin (GetActiveOrigin), working view V = the active view (DAT_180243380).
2. `chain_it_point(3941)` (mccoreres "Roll: select chains"): chains and points; none -> exit. Selected entities get sel bit 4.
   FUN_18002de90 (MC l.5432) appends chain c2 to c1 when c1's end equals c2's start or end (c2 reversed then; FUN_18002cc70
   MC l.10961). The selection box (FUN_18002d880 MC l.5499): EntityMinMax in V over the bit-4 entities (bit 2 when none);
   |v| < tol -> 0; extents < 0.1 -> 0.1; its centre C.
3. The modeless dialog; get_event loop with prompt mccadutilres 1523 "Modify dialog values or select chain to reverse it."
   (mask 0x8202 / 0x8e). Clicking an entity of a selected chain reverses that chain (FUN_18002cbf0 MC l.5306) and rebuilds.
   Cplane change event (0x8000 / 0x15): O, V := the new active ones, box recomputed, rebuild. Escape = OK when dlg +0x5090 is
   set, else Cancel (MC l.699-712). Panel codes: 100 Cancel (FUN_18002c860: preview deleted, chains freed), 0x65 OK / Apply
   (FUN_18002c8a0 MC l.5333), 0x66 rebuild (FUN_18002cbc0 -> FUN_18002bf70), 0x67 blank / unblank chains + fit (FUN_18002cb80;
   its sender NOT DECODED). Apply (dlg +0x5091, MC l.698) loops to step 2; the reset block runs once before that loop
   (MC l.641), so Apply keeps every parameter.
4. Buttons: 1250 Add/Remove = chain_it_point(3941) again, replacing the chains (FUN_18002aec0 R l.1 [inferred: by its prompt]).
   721 "Select an arc to define the parameters" (FUN_18002a3b0 R l.315, prompt mccadutilres 3942 "Select an arc..."):
   diameter := 2 x the arc's radius, O := the arc centre (view_to_world, written to DAT_180255148), axis := X when the arc's
   normal is the Cplane X axis, Y when it is the Cplane Y axis (Matrix33 from (arc normal, Cplane Y, Cplane Z) resp. (Cplane X,
   arc normal, Cplane Z) + AlignAxis: [inferred] from the row order), V := the view parallel to that frame (SearchParallel);
   the third fallback (Plane::CalcAndTransform with views 1 / 5) is NOT DECODED. 829 (FUN_18002ab10 R l.663-760): two get_event
   points, prompts GetCoreString 3929 "Select the point to translate from" / 3930 (empty in mccoreres; GetCoreString = mccore 0x18036ca10:
   mccoreres made the current resource handle, then CString::LoadString, whose MFC string search skips an empty entry and goes on along
   the resource chain - mccadutilres "Select the point to translate to", opmanres "Getting EXE path...", _axui_res "!!! NOT USED !!!" and
   mwexceptions_res have a 3930 (tools/pe_strings.py): which one wins depends on the chain's order, NOT DECODED - left empty), P1 / P2 :=
   them (dlg +0x104d0 / +0x104e8), +0x10500 = 1 (enables 801), positioning := two points (+0xc908 = 1), FUN_18000bd80(0x66); an Esc
   (get_event 2) posts 100 = Cancel. PORTED 2026-11-05 (cui_geo.c / cui_xroll.c xrl_pick, panel code 9).
5. OK (FUN_18002c8a0): build the preview if it is not valid; Move: when a chain has toolpath references, ask 1502 / 1513 /
   1519 ("Original chains have toolpath associations that will be lost." / "Do you still want to MOVE?" / "[no=copy]"), then
   delete the originals (yes) or keep them (no); UndoXformTransCommit; Use New Attributes -> XfApplyColorLevelToResult_ExOpIdn
   (results, colour, level); CheckOperationPlanes + validate_toolpath when references were found; xform_del_duplicate.

**Preview build** (FUN_18002bf70 MC l.10747): nothing when Preview is off (and not a Regen), r < tol or V < 1. Offsets
(FUN_18002ef50 RB l.375); target type T (P+0x58: 0 -> 2 lines, 1 -> 1 points, 2 -> 8 spline, 0x80 NURBS when config
cfg_createNurbs); per chain FUN_18002df50 (RB l.1): the unroll angle memory A := 1e38, then per chain entity (with its
chain-reversed flag) FUN_18002e070 (VT l.917): a copy taken to the roll frame by FUN_180030290 (RB l.55: world -> V's
coordinates relative to O, expressed in the Top view; Top: a translation by -O), then points -> FUN_18002e190 (R l.1355),
lines / arcs / splines / NURBS -> FUN_18002f170 (R l.949; other types: "(trav) (unsupported entity type)"). Originals: Move ->
blanked + bit 2; Copy -> bit 2, drawn. Status "Rolling..." (mccadutilres 3914) with a percentage per chain. Results: bit 8,
bits 2 / 4 off, the source entity's attributes / colour / level; every result is mapped back by FUN_1800305b0 (RB l.241: Top
-> V, + O).

**The point map** M(p) = FUN_180030150 (RB l.200), a = P+0x18 x pi/180 (0 when within 1e-6 of 2 pi, R l.1388 / RC l.115),
s_roll = +1 for CCW, -1 for CW; s_un = -s_roll. All in the roll frame (Top view coordinates, so no world_to_view change):
- Roll: q = p + D (offsets below), then axis X: roll_y (CR l.342): y' = q.y + r a, t = s_roll y' / r,
  M = (q.x, (q.z + r) cos t, (q.z + r) sin t); axis Y: roll_x (CR l.292): x' = q.x + r a, t = s_roll x' / r,
  M = ((q.z + r) cos t, q.y, (q.z + r) sin t). r < 1e-12 -> M = q.
- Unroll: axis X: unroll_y (CR l.504): rho = sqrt(y^2 + z^2), f = atan2(y, z) - pi/2, f moved by +-2 pi into (A - pi, A + pi]
  when A < 1e38, A := f; M = (x, s_un r f - r a, rho - r); axis Y: unroll_x (CR l.392): rho = sqrt(x^2 + z^2),
  f = atan2(x, z) - pi/2 (same continuity), M = (s_un r f - r a, y, rho - r); components |v| < 1e-12 -> 0 before the -r a;
  then M += D. (A is shared by the whole chain, RB l.23.)
- D (FUN_18002ef50, only in two-point mode, else 0; it also sets the angle P+0x18 := 0): unroll X: D = (P2.x - P1.x,
  (atan2(P1.z, P1.y) - pi/2) r + P2.y, 0); unroll Y: D = ((atan2(P1.z, P1.x) - pi/2) r + P2.x, P2.y - P1.y, 0); roll X:
  D = (P2.x - P1.x, -((atan2(P2.z, P2.y) - pi/2) r + P1.y), 0); roll Y: D = (-((atan2(P2.z, P2.x) - pi/2) r + P1.x),
  P2.y - P1.y, 0). P1 / P2 are used as the event returned them (world) - whether they should be in V's frame relative to O
  is NOT DECODED (they are not converted, RB l.375-425).

**Per entity** (FUN_18002f170):
- Point: M(p), always a point entity whatever T (FUN_18002e190 pushes a node + a type-1 target).
- Line: one segment (ends swapped when the chain runs it reversed).
- Arc: convert_entity_to_nurbs -> convert_arc_to_nb (+ nbrat_3d_to_4d) (ported: mcsurf_build.c l.368), stored temporarily,
  then as a spline; the chain direction is flipped when the NURBS starts at the arc's end. n_segs_to_wrap_arc /
  n_segs_to_unwrap_arc are called (R l.1131-1135) but their count is never used.
- Spline / NURBS (and the arc's NURBS): `break_spline_const_error_lines(ent, curve_chord_dev x 0.1, segs)` (@180062aa0):
  t from ent+0x98 to +0xa0 by `brk_spline_const_err(ent, curve_min_step, curve_max_step, tol, 0, &t)` (decoded / ported:
  formats/spline_chain.md 3, mcsurf.h `mcs_brk_spline_const_err`); a segment is kept when longer than tol; a segment
  colinear (check_colinear) with the previous one extends it. curve_chord_dev = config 1006 (0.001 in / 0.02 mm), min / max
  step = config 1004 / 1005 (formats/units.md). Reversed chain: the segment list reversed (R l.1224-1268).
- Each segment (R l.1271-1340): Roll + T = lines + check_wrap_line_as_arc(a, b, axis X ? 2 : 1, view 1) (CR l.3: the
  segment runs along the wrap direction - axis X: |dx| < tol, |dy| > tol; axis Y: |dy| < tol, |dx| > tol - and |dz| < tol)
  -> an exact arc by wrap_line_as_arc (CR l.616) on the segment + D: axis X: the view whose normal is (1, 0, 0), centre
  (x1, 0, 0), radius z1 + r, start s_roll (y1 / r + a), sweep s_roll (y2 - y1) / r; axis Y: the view with normal (0, -1, 0),
  centre (0, y1, 0), start s_roll (x1 / r + a), sweep s_roll (x2 - x1) / r (find_view_k; that these views' angle 0 matches
  M's t = 0 is [inferred]; a negative sweep's storage is NOT DECODED). Pending nodes are flushed before it. Failure -> the
  sampled path ("(trav) (wrap-as-arc has failed)").
- Otherwise break2lines FUN_18002d040 (RC l.1): n samples (below), points a + i (b - a) / n, i = 0..n (DAT_180243382 is only
  ever set to 1, R l.1076 / l.1396), each mapped by M; a point equal (pt_equal) to the last node is skipped; for T != points
  three colinear nodes merge (the last node replaced, RC l.257-272); a run reaching 1000 nodes (T lines / points) or 32762
  (splines) is flushed and restarted at the current point. Each source entity ends its run (FUN_18002ce80 RC l.309): T lines
  -> a line per consecutive node pair, T points -> a point per node, T spline -> make_spline_from_pts, NURBS ->
  create_nurbs_from_points (FUN_18002e530 R l.1433), at the chain's end (FUN_18002eaa0 VT l.1026); nodes go back to V / O
  through FUN_180030890 (R l.1636). Shared chain vertices give coincident points with T = points (each run starts fresh).
- Ctrl held while the preview builds (GetKeyState(VK_CONTROL), MC l.10846) + Unroll + T != points: the unrolled run is split
  where a step jumps more than 3 pi r / 2 (RC l.160-235: l_ints with the seam lines PTR_DAT_180243390 / 398, made from the
  outline lines). The seam lines' contents are NOT DECODED - a port leaves this out.

**Sample counts** (the step comes from the Angle tolerance P+0x10, b):
- Roll: n_segs_to_wrap_line (CR l.192) on the raw segment (no D): d = its component along the wrap direction (axis X: dy,
  axis Y: dx, in view 1); |dx| and |dy| < tol, or |d| < tol -> n = 1; else step = b r pi / 180 (b in degrees: the arc length
  per tolerance step), n = |trunc(d / step)|, at least 1.
- Unroll: n_segs_to_unwrap_line (CR l.69): w = FUN_1805faf10(z, x) (axis Y) / (z, y) (axis X) of each end = atan2 in
  [0, 2 pi) (0 or pi when |z| < 1e-12, pi / 2 or 3 pi / 2 when the 2nd argument is ~0); w2 += 2 pi when |w2 - w1| > pi;
  n = |trunc((w2 - w1) / b)|, at least 1 - b is used as radians here (no degree conversion, unlike the wrap: literal code).
- b <= 0: step = sqrt((1.5 / get_x_scr_scl()) 3.2 r) (both helpers); get_x_scr_scl (mastercam.dll, the screen scale) is
  NOT DECODED; the spin keeps b >= 5.7296e-5 (whether typed values are clamped is NOT DECODED).

**Exact vs sampled** (from the above): points: exact. Roll, Lines / Arcs: a segment along the wrap direction at constant Z ->
exact arc; a segment with no component along the wrap direction (|d| < tol) -> n = 1 -> exact line (t constant); every other
segment -> a polyline of n chords (colinear ones merged). Unroll: a segment at constant angle -> n = 1 -> exact line; others
-> n chords. Arcs and splines are chorded first (curve_chord_dev / 10), so in Lines / Arcs they give polylines; a chord can
still become an arc when it lies along the wrap direction at constant Z (an arc on the cylinder unrolled with chords shorter
than b rad maps chord ends to colinear nodes -> one line: [inferred]). T = Points: a point per node. T = Splines: one spline
(or NURBS) per source entity through its nodes.

**Cylinder outline** (FUN_18002c850 MC l.4787, temp graphics while the dialog is up; r >= tol and V >= 1): h = the box
extent along the axis / 2 x 1.1 (axis Y: Y extent; axis X: X extent); four lines parallel to the axis from -h to +h at
(+-r, 0) and (0, +-r) across it (the fourth, (0, -r), in line style 1, width 2, colour 15) and two full circles of radius r in
view 2 (axis Y) / 5 (axis X) at the ends, all about O moved to C along the axis; h <= tol: one circle.

**Port mapping** (cui_xform.c): a new G_XROLL beside the others with its own state (P above; Move / Copy / Use New Attributes
as xf_attrs / xf_make), selection by chains (cui_chain.c, chain_it_point's role) instead of select_ents; the preview / undo /
group-result marks / Apply loop as xf_make / xf_commit. Exactly portable: points; lines; the exact arcs (mcam_new_arc_view_w
with views 2 / 5 for the Top Cplane; another Cplane rotates them as xf_ent does); arcs and E_SPLINE input through
convert_arc_to_nb + mcs_brk_spline_const_err. T = Splines needs make_spline_from_pts / create_nurbs_from_points (not in this
program: NOT PORTED - grey the entry or refuse). Also left out: the Ctrl seam split, the arc pick's third fallback, the b <= 0
screen-scale step, the toolpath-association question (no chain toolpath references by entity id here: as the other moves).

### Geometry Nesting — scope / port spec (2026-11-04)
> **Superseded in part (2026-11-05):** full NESTING.DLL decode in formats/nesting_addin.md (template 105 = geometry Parts page;
> nested copies are written during the run, not after Results OK; corrections listed there in §10).
Sources (decompiled 2026-11-04, Ghidra headless, not saved as dumps): CO = mccore.dll (project ghidra_core); MC = mastercam.dll
(project ghidra/mc2017); NE = nesting.dll (NEW project ~/mastercam-re/ghidra_nesting, imported + auto-analysed 2026-11-04; no
earlier notes). Files: ~/mastercam-re/files/{nesting.dll, n64dll.dll, mcnestingres.dll, nesting.defx, nesting_mm.defx,
nesting.xml, default_nesting_report.rpx, mcnesting.chm}. Help = chm/mcnesting (Geometry_nesting/*.htm, General/*.htm).
Dialog rows: tools/pe_dialogs.py mcnestingres.dll; strings: tools/pe_strings.py mcnestingres.dll.

**Where the engine is (the key answer).** The optimisation is NOT in Mastercam's own code. It is Geometric Ltd's **NestLib**
("http://nestlib.geometricglobal.com", "Nestlib Development Version" - strings in n64dll.dll), shipped as `n64dll.dll`
(2.9 MB, .text 0x25fdc6; imports only USER32/KERNEL32/GDI32; exports a C API `N*` (TrueShape) + `RN*` (rectangular) and some
C++ classes: shakeNest, partToBeNested, trialSheet, negCollar, optStruct...). It is licensed: NE calls
`NSetDLLEnableKey(<key string @1801301f0>)` after `NStartSetParams` (NE FUN_180039c90). Both modes (Rectangular and TrueShape)
run inside n64dll.dll (RNInitRectangleNest vs NOpenNestLib, NE FUN_180043040). Decompiling n64dll.dll is possible in principle
but it is a third-party licensed library: NOT DECODED, and not proposed for porting.
The call chain (all decoded):
- CO `do_nest_g` @1805bec10: CString "m_nest_g" (0x180d7d460); `nesting_enabled(0,0)` (@1805be860: 1 unless
  `OperationInProcess`) else returns 3; then FUN_1805be640 = MC `run_nesting_func(name, 0)` -> `UpdateLevelsManager()`.
  (do_nest_cfg / do_nest_m / do_nest_t pass "m_nest_cfg" / "m_nest_m" (MCLink) / "m_nest_t" (toolpath); do_nest_r opens
  the ActiveReports viewer.) Ribbon enable: CO `OnUpdateNestingToolbar` @1805be9c0, do_nest_g = id 0x948e -> nesting_enabled(0,0).
- MC `run_nesting_func` @18045fa10: `loadMCApp("NESTING.DLL")`, `GetProcAddress(name)`, call(param), success = return 0.
- NE `m_nest_g` @1800450b0: FUN_18003eac0(2) (mode 2 = geometry; refuses with 10101 "Warning - nesting already running." when
  nestingMODE != 0; product gate `nesting_check_product(mode not in 1..2, ..)` CO @1805be7a0 -> always passes for mode 2;
  saves OM insert location / selected ops; Ctrl held toggles RecordBoundaries debug) -> FUN_180043c20 (data folder
  "<app data>\Nesting\" + "IMG\" + "XML\" [inferred: the app path getter is not decoded]) -> FUN_180038350 (license;
  "Warning - invalid nesting license.") -> the session driver FUN_18003ce30 -> FUN_18003e8d0 / FUN_18003cc20 (cleanup).
- Same driver for m_nest_t (mode 3) and m_regen (mode 5, regenerates a nesting op).

**Size of the Mastercam-side code.** nesting.dll is a 1.3 MB MFC add-in (.text 0xe8c5c ~ 950 KB of code): property sheet +
pages, libraries / defaults / session files, the NestLib driver and callbacks (source file name "callback.cpp" in asserts),
the result dialogs, the geometry write-back, and the whole toolpath-nesting side. All of it is decompilable; only the
pieces below were read.

**User flow** (Help: About_geometry_nesting, Parts_tab_geometry_nesting, Sheets_tab_geo_toolpath_nesting; dialogs from
mcnestingres.dll):
1. Transform > Geometry Nesting opens the Nesting property sheet (MCPropertySheet, NE FUN_18003ce30). With "Restore last
   session" the previous `nesting.nstx` is loaded (NE FUN_18003b270(local "nesting.nstx", 1) [inferred: the condition
   FUN_180079570 is not decoded]).
2. **Sheets** page (template 101): Sheet List (right-click: Add rectangular / Add chained / Add sheets from files / Import from
   library (.NSL/.NSLX/.NST/.NSTX) / Save to library (.NSLX) / Move up/down / Delete all; dialog right-click Save / Load
   defaults), Size (combos 3007/3009), Create necessary quantity 3018, Quantity 3017, Priority 1130, Origin X/Y 3012/3014 +
   Select 3015, Material... 1117/1118; Common Parameters (all sheets): Grain Direction 3021, Nesting Corner 7029, Fill
   Direction 3026, Guillotine cut 7033, Automatic sheet origins 3022, Sheet-Sheet Distance 7002, Sheet Margin 7004.
   Sheet = closed chain (inner closed chains are holes, open chains ride along, notes ignored; Help About_sheet_geometry).
3. **Parts** page (template 105 or 107 - which one is the geometry page is NOT DECODED; 105 has "Common Edge" 1100, 107 has
   "Optimize" 1100 + a progress bar 9210): Part List (right-click: Add rectangular, Add chained parts, Add chained cluster,
   Disassemble solids, Add solids, Add parts / clusters from files or folder, Add options (Align with longest edge, Create a
   group when adding multiple parts, Ignore holes, Scan for notes), Import from library (.NPL/.NPLX/.NST/.NSTX), Save to
   library, Attach geometry, Delete all); per part: Size, Minimum Qty 4010, Filler Qty 4012, Step Angle 4014, Priority 4016,
   Label 4032, Material... 1116, Use bounding box 4019, Profile / Hole / Ignore colour swatches 1086-1089/1084-1085, Grain
   Direction 4023, Mirror 4021; Common: Part-Part Distance 7005, Add part labels 4034 + Height 4035 / Automatic 4037,
   Optimized pairs 7027, Fill all sheets 1013 (max 3500 instances per sheet: strings 10008/10009).
   Part = closed outer boundary; inner closed = holes; open chains / points / notes are copied with it but not nested
   (Help About_part_geometry). "The parts can lie at different Z depths. Nesting generates the session at the current Z".
4. **Groups** page (template 122, shown when Display Group page): Group List, Quantity 6006, Priority 6010, Available Parts,
   Parts in Group, "Quantity of this part within the group" 6016.
5. The Configuration dialog (template 201, also do_nest_cfg): Load default sheet, Display Results dialog, Display Group page,
   Delete original chains, Use Name as Label, Restore last session, Compress results, Cycle colors starting with / Cycle levels
   starting with (+ Select), Only unused levels, Higher resolution images in reports, Save sheet scrap, Check for duplicates
   during save, Nesting Mode Rectangular / TrueShape + Accuracy, TrueShape Options: Nest smaller parts in the holes of larger
   parts, Nest in holes before nesting on sheet; Save as Defaults. (Toolpath Attributes group: toolpath nesting only.)
6. OK runs NestLib (below); the **Nesting Results** dialog (template 205): Sheet (Result) list, Utilization, Horizontal /
   Vertical trim, Total / Nested Size, Total / Nested Area, Details (report: nesting.xml + ActiveReports .rpx), Redo, Save,
   Copy / Copy Blank / Delete, Sort, Customize..., Drag (**Bump Nesting**, template 1621: Sheet, Drag / Delete, Copy / Move,
   Rotate CW / CCW by the step angle (+/- keys, Ctrl = 1 deg), Mirror, Display holes, OK / Apply).
7. OK in Results writes the geometry (below), Save offers .NSTX sessions; Save sheet scrap writes the leftover to SCRAP.NSLX.

**Defaults** (nesting.defx, inch; nesting_mm.defx metric): [PARAMETERS] TRUESHAPE=YES, PARTTOPARTDIST=0.1, SHEETTOPARTDIST=0.0,
SHEETTOSHEETDIST=1.0, RESOLUTION=1.0, PROFILETOLERANCE=0.01, FITPARTINPART=NO, PREFERHOLEFILLING=NO,
NESTFILLERSTONESTHEIGHT=YES, GUILLOTINECUT=NO, ACCUNEST=0, EXACTMODE=0, INNERHOLES=YES, IGNOREHOLES=NO, ADDLABELS=NO,
LABELHEIGHT=2.0, RESULTCOLOR=10, RESULTLEVEL=1, CYCLECOLORS/CYCLELEVELS=NO, DELETECHAINS=NO, DISPLAYRESULTSDLG=YES,
LOADDEFAULTSHEET=YES, AUTOORIGINS=YES, SHEETFILLDIRECTION=X, STARTINGCORNER=1, SAVESCRAP=NO, SCANFORNOTES=YES (+ toolpath
keys). [SHEET1] 96 x 48, QUANTITY=1, GRAINDIRECTION=X, origin 0,0. [PART1] 1 x 1 rectangular, MINQUANTITY=1, FILLQUANTITY=0,
STEPANGLE=90, PRIORITY=1, MIRROR=NO, GRAINDIRECTION=N, CLUSTERMETHOD=7. [GROUP1] DEF_GROUP, MINQUANTITY=20. The defx key ->
NE global mapping is NOT DECODED (the reader was not located). A profile tolerance default is set in FUN_18003ce30:
0.01 (inch) / 0.1 (metric) into DAT_180137988.

**Data passed to the engine** (NE; each step also logged in NestLib's text format to `<data>\nesting.nip`, FUN_180035970):
- Open: TrueShape `NOpenNestLib(dataDir, 1, errCB FUN_180006cf0)` / rectangular `RNInitRectangleNest()` (FUN_180043040);
  failure -> 10013 "Warning - unable to open NESTLIB". The .nip header: NEST_TYPE NORMAL, FORMAT UNIFIED, RESULT nesting.nop.
- Callbacks (FUN_1800411f0): error, progress FUN_180006700 (also `NCommitNestedOrientation`, `NSetCurrentSheetLocalArea`),
  new-part-geometry FUN_180006f90, new-sheet-geometry FUN_1800074b0, nested-sheet-result FUN_180005f90; rectangular:
  RNSetPartNestedCB FUN_180009da0, RNStartNewSheetCB FUN_180009ca0.
- Parameters (FUN_180039c90): TrueShape: NSetResolution(r) (r = DAT_180137870, or FUN_180037e40() when DAT_180137868 < 3,
  then FUN_180038410), NSetPartDistType(0), NSetClearance(FUN_1800cad50()), NSetThicknessAndMaterialSelection(0, multiple
  materials), NSetNestFillersUptoNestedHeight, NSetFitPartInPart(fitPartInPart, preferHoleFill), NSetGrpMaxElmtSize,
  NSetExactDistance(0/1), NSetAccuNest(0), NSetGuillotineCut, NSetClusterCutoffUtil(0), NSetCutterDiameter(0),
  NSetFitClusterFirst(0), NSetFitMaxOnLast(1). Rectangular: RNSetResolution, RNSetClearance, RNSetNestFillersUptoNestedHeight,
  RNSetCutterDiameter(0). Which dialog field feeds each global: NOT DECODED beyond the .nip key names.
- Parts (FUN_18003a120 -> groups FUN_180036950 / clusters FUN_180035f90 / parts FUN_180037400): `NAddPartInfo(name, q1,
  q2, ...)` (q1 / q2 from FUN_18005be90 [inferred: min / max quantity, as the .nip MINIMUM / MAXIMUM]; rect:
  `RNAddPartInfo2(name, q1, q2, part+0x238, ..)` + `RNSetPartPriority`), `NDefinePartMaterialList`,
  clusters `NStartCluster(name, 7, 1, 0, ..)` / `NAddClusterElement(.., 2, 0, 0, 360.0)` / `NEndCluster`, groups
  NStartGroup / NAddGroupMember / NEndGroup. .nip PART keys: NAME, MINIMUM, MAXIMUM, IF_MIRROR, MIRROR_TYPE, GRAIN,
  ROTATION_ANGLE_METHOD BY_STEP_ANGLE, STEP_ANGLE, PART_MATERIAL_LIST, FIT_TYPE, PRIORITY, CLUSTER, COMMON_TOOL, LENGTH, WIDTH.
  Part geometry is pulled by the engine through the callback (FUN_180006f90): NDwgGeomStart / NDwgProfsStart /
  NDwgClosedProfStart, then per entity NDwgAddLine(x1,y1,x2,y2) (only when longer than `tol`), NDwgAddArc(cx,cy,..),
  NDwgAddCircle(cx,cy,r), NDwgChangeCutterDia(part+0x270), ...End. Rectangle-only parts: FUN_1800073c0 (four lines).
- Sheets (FUN_18003a3f0, called from the nest-order step FUN_18003e870): `NAddSheetInfo(name, qty, 1, irregular)` +
  `NAddSheetPriority(name, i+1)` + `NDefineSheetMaterialList` (rect: `RNAddSheetInfo2`); .nip SHEET keys NAME, QUANTITY,
  COLLAR, LENGTH, WIDTH, DIRECTION, PRIORITY, GRAIN, SHEET_MATERIAL_LIST, MULTI_CRN_TYPE. Sheet geometry callback
  FUN_1800074b0 (outer profile `NDwgProfsStart(1)` + hole profiles; an offset by a negative distance is applied to them,
  its source field NOT DECODED).
- Run: `NStartNestOrder` + `NNestOrder` (TrueShape) / `RNNestOrder` (FUN_18003e870); `NCloseNestLib` / `RNEndRectangleNest`.

**Data returned**: FUN_180005f90 (CNST_Result / CNST_Part / CNST_Inst records): per sheet `NInqNestedSheet` +
`NInqNestedParameters`, per part type `NInqNestedPartType`, per instance `NInqNestedInstance(sheet, part, out)` (position,
rotation angle, mirror flag [inferred from how CPart::XformEnt uses them]); rectangular: `RNInqNestedSheet` /
`RNInqNestedPart` (FUN_180005830). The record layouts are NOT DECODED.

**Write-back** (NE FUN_1800621b0 = "CPart::XformEnt", per entity of each nested instance): with parameters angle a (deg),
mirror m, result position P, part min corner + base point (part+0x280/0x288): (1) if the part's view v >= 2: map view v -> Top
(xform_ent mode 1 with the view matrices); (2) translate by -(base + min) (when param_15); (3) if m: scale X by -1 (mode 0);
(4) if a != 0: rotate about Z by a; (5) translate by P; (6) back to the part's view when it was >= 2. Then attributes: colour
param_18 / level param_19 (fall back to the entity's own), `store_ent` (a new entity: copies, the original is kept unless
Delete original chains). Labels: a note entity gets text height (param_12, else DAT_180137908, else 0.25), angle a, mirrored
justification bits (calc_note). Error -> 10014 "Transformation Error!". Sheet geometry: FUN_180091b90 ("CSheet::XformEnt",
same pattern with the sheet origin part+0x270/0x278). Cycling colours / levels per sheet, scrap and report XML: NOT DECODED.

**Toolpath nesting (context only)**: do_nest_t -> m_nest_t (mode 3), same driver and same NestLib; it creates a nesting
operation of type 0x6c (CO FUN_1805be670 redraws / regenerates op type 0x6c after the run; nesting_enabled skips 0x6c and 0x85;
`ParentIsNest`, `nestingLOCKop`). Its pages: Parameters (132: sorting, stop condition, work offsets), Additions (133), Common
Edge (MC run_nesting_common_edge -> NE m_common_edge FUN_180010d50). Requires the machine product bits (10003 "Select a machine
before running toolpath nesting.").

**NOT DECODED**: NestLib's algorithm (n64dll.dll, third-party); the property sheet page classes / DDX and the defx reader; the
geometry-page template (105 vs 107); results-dialog sorting / Redo / Bump internals; .NSTX / .NPLX / .NSLX formats; report
XML writer; colour / level cycling; how chains become part profiles (holes, Ignore holes, clusters) before the callback.

**Effort, honestly**: the Mastercam glue is decodable but large (about 950 KB of code; the pages, libraries and results
dialogs alone are weeks). The engine cannot be ported from Mastercam: any port needs a substitute nesting engine (user
decision; e.g. an NFP / bottom-left-fill engine of our own or an existing library), and its layouts will never match
Mastercam's, so no oracle comparison is possible beyond the transform step (CPart::XformEnt, which is exact). Rough sizes:
substitute engine + minimal Sheets / Parts dialog + write-back: 2-4 weeks; full Mastercam parity (groups, clusters,
libraries, sessions, results / bump, reports, labels): 2-3 months. Recommendation: keep greyed until the user decides.

### Dynamic — port spec (2026-11-04)
Sources: MX = `decomp/xform/mccadutil_xform.c` (DynamicXform l.2493, ctor FUN_1800e6fc0 l.8321, main FUN_1800eb210 l.8431, round
FUN_1800eb6c0 l.12987; the same three also in `distribute_b.c` l.279-600). Decompiled 2026-11-04 with Ghidra (ghidra/mc2017
mccadutil.dll, ghidra_gafgr gafgr.dll; not saved as dumps), cited by address below. Panel: BAML
`mccadutil.ui/transform/xformdynamicmanagerview.baml` (tools/baml_panel.py), tooltips
`MCCADUtil.UI.Transform.XformDynamicManagerViewToolTips.resx`, view model `MCCADUtil.Interop.XformDynamicManagerViewModel` (ilspycmd on mccadutil.interop.dll: it maps each
binding to a native IXformDynamicManager vtable slot). Strings: tools/pe_strings.py mccadutilres.dll (decimal ids).

**Two layers.** (a) DATA: a 4x3 matrix T applied to the selection by `xform_silent` (move / copy, n instances), the panel's five
values, the commit / preview rules below — portable onto cui_xform.c. (b) PURE UI: the gaf gnomon (`CncGaf::GrRefGnomon`,
gafgr.dll, wrapped by mccore `SelXformGnomon` / `GnomonApp`): its handles, hit-testing, ruler / compass drag, snapping. Decoded
2026-11-05 in "The gnomon widget" below (meshes, sizes, colours, hit test, drags, settings).

**Panel** (BAML; manager XformDynamicManager ctor FUN_1800f1440; field -> vtable getter from the view model):

| control (BAML) | manager | default (FUN_1800f1440 / FUN_1800f29c0) | on change (observer lambdas, FUN_1800f1de0..f1f90) |
|---|---|---|---|
| Mode: _Move / _Copy (IsCopy) | +0x50 bool, vt +0x48 | Move (0) | posts 0x8038 code 10 Move / 11 Copy; Move -> IsSingle := true |
| Type: Si_ngle / M_ultiple (IsSingle) | +0x60 bool, vt +0x50 | Single (1) | posts 12 Single / 13 Multiple; Multiple -> IsCopy := true |
| _Instances (CopyCount, int box, enabled = IsSingle) | +0x90 int, vt +0x78 | 1 (FUN_1800b5f50(1,1): [inferred] value 1, min 1) | posts 14 |
| _Only apply last transform | +0x70 bool, vt +0x58 | off | posts 7 |
| Advanced tab: _Set to geometry when placed | +0x80 bool, vt +0x60 | on | posts 7 |
| Selection: _Reselect | vt +0xd0 FUN_1800f2860 | | posts 6 |
| Origin: C_plane / Wor_ld / _Tplane / _WCS | vt +0xd8..+0xf0 | | posts 0x1a / 0x1b / 0x1c / 0x1d |
| Axes: Cpl_ane / Worl_d / Tplan_e / WC_S | vt +0xf8..+0x110 | | posts 0x1e / 0x1f / 0x20 / 0x21 |

OK / OK-New(Apply) / Cancel / Help (PANEL HasOKButton HasOKNewButton HasCancelButton HasHelpButton); manager vt +0xb0 / +0xb8 /
+0xc0 post 0 / 1 / 3 (FUN_1800f2850 / f27d0 / f27e0 -> FUN_1800f28c0 = PostMessage(0, 0x8038, code)); which button is +0xc0 is
[inferred] Cancel. Enable flags (manager bytes +0x48..+0x4d, setters FUN_1800ef1e0 "EnableApply" / "EnableOK" / "EnableSelection"
/ "EnableOrigin" / "EnableAxes" / "EnableSettings"). The resx also has X / Y / Z / Length / Angle / Delta / Polar / Absolute /
Incremental / Direction 1/2 / Add Selection tips, and the params struct (FUN_1800f25b0 / FUN_1800f2cd0, FUN_1800ed1d0) carries
X/Y/Z, length, angle doubles, but the 2017 BAML has **no typed-value fields**: no typed offsets in this version [code]. Params live
in statics DAT_1802447d0 for the session (SaveRestoreLastParams FUN_1800e6f20 / FUN_1800e7440 -> FUN_1800f29c0 restores them when
`enable_dialog_modality` is set, else the defaults; FUN_1800f2a90 saves them when manager vt +0x18 (FUN_1800f26a0, an error
check [inferred]) is false). Not in the registry. No Use New Attributes in the panel. Group / result marks:
see xform_silent below (results get the Result colour).

**App state** (XformDynamicApp, FUN_1800e6fc0): +0x20 manager, +0x30 SelXformGnomon, +0x40 its GrRefGnomon, +0x3a0 phantom-copy
list, +0x3c8 the selection (DB_LIST_ENT list), +0x3f0 WfMgr (preview geometry), flags +0x389 **mode** (1 = gnomon / "Manipulate
Axes", 0 = geometry / "Manipulate geometry"; SetMoveGeom(+0x389 == 0); cursor resource 0x6a gnomon / 0x6b geometry), +0x38f a
transform is pending, +0x394 a banked transform exists, +0x38d dragging. Matrices (MtTmat3d, row-vector convention: x_matrix =
GetEl(r, c) r 0..3, c 0..2, translation in row 3, FUN_1800ed800):

| off | name | set by |
|---|---|---|
| +0x58 | B, the gnomon frame the transform is measured from | FUN_1800eab50(1): B := C; also +0x1f0 := B⁻¹ |
| +0x168 | C, the current gnomon frame (GrGnomon::GetDefMat) | FUN_1800eab50 (any), after every drag / button |
| +0x1f0 | B⁻¹ | with B |
| +0xe0 | E, the gnomon frame when the current drag started | before each drag (MX l.13438; l.13386 for the ruler) |
| +0x278 | A, transforms committed by Ctrl-Apply / Multiple this round (preview base) | identity at round start; A := A·T on keep-commit |
| +0x300 | P, the banked transform (gnomon button) | P := P·T (path 7); identity after each commit / reset |

T = B⁻¹·C (operands from the disassembly: 0x1800e9307 / 0x1800e8aa8 / 0x1800e8d2f, `[+0x1f0] * [+0x168]`); with a bank, T := P·T.
Tlast = E⁻¹·C (0x1800e934b), used only by Only-apply-last.

**Flow** (FUN_1800eb210 -> loop FUN_1800eb6c0(app, cplaneMat, mgr, again) while it returns 1):
1. `*graphics_support != 1` -> message 433 "OpenGL graphics must be enabled to use this function", end (l.13210).
2. Gnomon created (CreateSelXformGnomon, EnableButton, SetMoveGeom(1)); paint notify FUN_1800eab10: 2 = fit extents with every
   instance (FUN_1800e85b0), 0x18 = draw the preview (FUN_1800e9260), 0x19 = bounds (FUN_1800ea270).
3. Round: mode := gnomon (FUN_1800ecda0(1)), panel disabled (FUN_1800e8540), Apply off; unless a bank exists, `select_ents(440
   "Select entities to move/copy", mask 0x171fff, MC_PRESEL 2 first time, then 0)` (l.13203); nothing / Esc -> end. FUN_1800ea720
   copies every selected entity (bit 4, mask 0x1f1fff) into WfMgr and +0x3c8, clears sel bits 2|8; none -> select again.
4. The gnomon is put on the Cplane (vt +0x188 [inferred SetDefMat] with cplaneMat⁻¹, 0x1800eba94); A := I; B := C; panel enabled;
   IsSingle / CopyCount restored from +0x391 / +0x398.
5. **Placement**: EmSetSelObjEdit(gnomon, 2) and a drag (FUN_1800e9ef0 -> FUN_1800e9540): the gnomon follows the cursor until the
   click (prompt 455 "Pick gnomon origin position or Ctrl-right-click for gnomon settings"); Esc here -> back to step 3. Then, if
   Set-to-geometry-when-placed, mode := geometry (l.13510). On later passes (`again` = 1, after the gnomon button) the placement
   is skipped.
6. **Idle** (FUN_1800ec560; prompt by mode and the gnomon's current edit mode: gnomon 441 / 442 / 443 / 445 / 444, geometry
   446 / 447 / 448 / 454 / 452, for none / origin / axis-translate / align / rotate): get_event(mask 0x156c0808) -> handle picked
   (0x400000 / 0x1000000, EmGetSelItem; l.13450) -> drag (step 7); gnomon button (0x4000000, FUN_1800ea880) -> path 7 below if +0x38a
   (else MessageBeep); key event 8 / 0x80000 -> FUN_1800eb0a0: **Ctrl (0x11) toggles gnomon <-> geometry mode** (not while
   dragging; needs gnomon mode or +0x38a); panel codes (0x10000000; Enter = event 8 key 0xd is turned into a panel code by
   FUN_1800e58f0, see step 7): 0 OK -> end 4, 1 Apply -> end 5, 3 / 4 -> end 6, 2 ignored here, others FUN_1800eac50 (6 Reselect -> end 3);
   event 0x800 -> end 5 when EnableApply ([inferred] the double-click of prompt 446); Esc / right-click (get_event 1) -> end 6;
   0x40000 -> PostMessage WM_CONTEXTMENU (the gnomon settings menu; NOT DECODED).
7. **Drag** (FUN_1800e9540; E := C first; prompts gnomon / geometry: origin 455 / 457, axis-translate 464 / 465, align 469 / 471,
   rotate 472 / 474): mask 0x13840809 (0x13840808 for RotType 1), entity mask 0x1f1fff (AutoCursor / entity picks). Moves
   (0x800000) redraw the live preview; the release (0x2000000, or edit status 2): gnomon mode or Single -> drag done; geometry
   mode + **Multiple** -> commit a copy at once (FUN_1800e89e0(copy 1, count ESI [inferred] 1, keep 1), 0x1800e9b32) and keep
   dragging (each release drops one copy). Panel codes: 1 Apply -> end 5, 0 OK -> end 4, 3 -> end 6, 2 -> drag done, 4 -> as Esc;
   Enter becomes code 1 (code 0 when event +0x150 is set, FUN_1800e58f0(ev, 0, 1); that field NOT DECODED). Esc / right-click
   -> the drag is cancelled (EmCancelSelObjEdit; [inferred] C restored by it), +0x38a := 1.
   After a drag in gnomon mode B := C (FUN_1800eab50(+0x389)), so moving the gnomon alone does not move geometry; in geometry
   mode T changes and +0x38f := 1.
8. Path 7 (gnomon button while a transform is pending, FUN_1800e8df0 + l.13303): the pending T (with P) is baked into phantom
   copies (xform_silent copy, CopyCount instances, colour df_phantom_color, list +0x3a0), P := P·T, IsCopy / IsSingle / CopyCount
   saved, the round returns 1 and FUN_1800eb6c0 runs again with cplaneMat := C⁻¹ (the gnomon stays) and the bank flag set: the
   gnomon can be re-placed, further edits compose onto P.
9. **End codes** (LAB_1800ec0e3): 4 OK / 5 Apply commit when +0x38f (FUN_1800e89e0(IsCopy, CopyCount, keep 0)); 0 / 3 / 6 drop
   the transform (FUN_1800ed100 hides / deletes the phantoms, P := I). Then 0 / 4 end the command; 3 / 5 / 6 go back to step 3
   (Apply = commit + new selection; Esc = discard + new selection, where Esc ends). **Ctrl+Apply / Ctrl+Enter** (Ctrl down per
   GetAsyncKeyState in FUN_1800ec560; block 0x1800ebc92-0x1800ebcd8, dropped from the decompile as unreachable): commit with
   keep 1 and stay on the same geometry (no reselect).

**Commit** FUN_1800e89e0(copy, n, keep) [code]: phantoms removed; M = T (P·T with a bank); UndoXformTransStart; if Only-apply-last
and n >= 2: FUN_1800ed800(M, copy, 1) then FUN_1800ed800(E⁻¹·C, copy 1, n - 1) on the results of the first (the originals' sel
bits saved and restored around it), else FUN_1800ed800(M, copy, n); UndoXformTransCommit(copy == 0), or UndoTransTerminate when
nothing changed. keep: A := A·T, B := C, the selection +0x3c8 := the results. Always P := I. FUN_1800ed800 = `xform_silent(list,
copy, x_matrix(M), xform_type 1, &ok, n, 1)` (mccore 0x1808d9da0; it sets sel bit 4 on the input and calls FUN_1808d8820 with
keep-attributes 1), then marks each result (visible level, not bit 0x100) with bit 4. Instance k
= M^k, k = 1..n (xform_silent, see "The gnomon widget" below).

**Preview** (FUN_1800e9260, notify 0x18): WfMgr::DrawInstances(A, T, T2, k, phantom colour Wd_colors[df_phantom_color]); k =
CopyCount when Single and not dragging, else 1; T2 = Tlast when Only-apply-last and k >= 2, else T; with a bank T := P·T, T2 :=
P·T2. Instance i (FUN_1800e85b0 / ea270 show the walk): m := A; m := m·T (i = 1), m := m·T2 (i > 1). Visibility (FUN_1800eb120,
FUN_1800e7d70 writes the entity's display-list state): phantoms state = (geometry mode), originals state = (+0x38f and Move);
[inferred] state 1 = hidden (the moved preview replaces the originals).

**Origin / Axes buttons** (FUN_1800eac50) — data, decoded: Pl = Cplane (UtGetCPlaneMat) / World (identity) / Tplane
(UtGetTPlaneMat) / WCS (UtGetWcsMat). Origin (FUN_1800ea940): C' = C with translation := (0,0,0)·Pl⁻¹ (the plane origin in world).
Axes (FUN_1800e7c50): C' = Pl⁻¹ with C's translation (the gnomon's axes take the plane's, origin kept). Then gnomon := C'
(vt +0x188); geometry mode: +0x38f := 1 (the geometry follows: T = B⁻¹·C'); gnomon mode: B := C' (only the gnomon moves).
EmLockSelObjEdit(true). Code 0x12 (C with translation 0) and 0x22 / 0x23 (Ctrl toggle / re-assert) have no source in this panel
(NOT DECODED where they come from).

**Gnomon edit modes** (gafgr GrGnomon, decoded): EditMode 1 = origin (IsOriginEdit); 2-4 = translate along X / Y / Z
(IsTranslationEdit; GrRuler::DragRuler, then ApplyDrag C := Translation(+0x500)·C); 5-10 = axis (IsAxisEdit; 5-7 align
IsAlignEdit, 8-10 face IsFaceEdit; DragAxis: with a snapped 3D point the X axis is aimed at it (MtTmat3d::SetAlignedXAxis), modes
5/8 and 6/9 permute the axes by fixed matrices DAT_110d8648 / s_PlaneDef; without a point it falls back to DragOrigin);
0xb-0x16 = rotate (IsRotationEdit; RotType 0 = 0xb-0x10, 1 = 0x11-0x16; GrCompass::Drag; ApplyDrag C := CompassRot·C; GetRotBias
90° for 0xb, 0xe, 0xf, 0x11, 0x14, 0x15, else 0); 0x17 = the GrRefGnomon ruler (sel item 0x15; the app suspends the panel and
drags without committing, l.13382). DragOrigin (0x10ee2210): a snapped 3D point (AutoCursor) is used as is (Is3dSnap -> app
+0x388); otherwise the screen point is projected onto the gnomon plane (MtVector2d::Project, tol 5e-5). Snap state: Get3dSnap
+0x318, GetHVSnap +0x31c (both 2 in the ctor), rotation snap value +0x900 / enabled +0x8f8 (= the compass's +0x100 5.0° / +0xf8 on,
then the settings), IsAngleSnap +0x9d2 / +0x9d4 (cursor 0x2ca while angle-snapped, SelXformGnomon::Drag). Free move: only the origin handle (mode 1, in the gnomon plane or to a snap point); no free rotation.

#### The gnomon widget — decoded 2026-11-05
Dumps in `decomp/xform/dynamic/` (gitignored): GD = `gnomon_draw.c` (GrGnomon ctor / Tessellate / DrawAxes / DrawItems /
DrawPlane / DrawLabel / PrepDrawMasks / SetProps / SetSize / SetColor), GS = `gnomon_select.c` (Select x2, GrRefGnomon,
GetEdit/SetEditTranslation), RG = `refgnomon.c` (GrRefGnomon ctor / Tessellate / SetState / Highlight, ResetEditMap), RC =
`ruler_compass.c` (GrRuler, GrCompass), EV = `gnomon_editvalue.c`, MESH = `gnomon_mesh.txt` (every handle's triangles, dumped by
the scratch script DumpTri.java: GrTriItem = {int type 0 list / 1 strip @0, float *v @8, int n @0x10}, vertex = 6 floats x y z
nx ny nz), DATA = `gnomon_data.txt`, MC = `mccore_gnomon.c` / `mccore_gnomon2.c` / `mccore_gnomon_settings.c` / `mccore_vt.txt`,
XS = `xform_silent_dis.txt` + `mccore_xfinfo.c`. All gafgr addresses are gafgr.dll (ghidra_gafgr), all 0x180a… mccore.dll.

**Construction.** GnomonApp::CreateSelXformGnomon (MC 0x180a07f90) = SelXformGnomon(size 0x78, item mask 0x1e7f) -> SelRefGnomon
-> GrRefGnomon(size, mask, false) -> GrGnomon (GD 0x10edc090). Item-mask bits (gafgr exports @0x10fa8e08..): ORIGIN 1, X/Y/Z_AXIS
2/4/8, X/Y/Z_ARC 0x10/0x20/0x40, X/Y/Z_PLANE 0x80/0x100/0x200, X/Y/Z_LABEL 0x400/0x800/0x1000. 0x1e7f = origin, 3 axes, 3 arcs,
the **Z plane only** and the 3 labels. The data units follow IsMetric (mm 3 / inch 7, SelRefGnomon ctor MC 0x180a52a90). The
SelXformGnomon also owns a GrButton (states 0x28f6 / 0x28f7, MC 0x180a534a0): the "gnomon button" of the round's path 7.
Settings are then applied from SelGnomonBase statics (UpdateSettings 0x180a659b0): size **s_GnomonSize** (pixels), rotation snap
s_AngularInc / s_AngularActive, ruler s_RulerActive / s_RulerSize (below).

**Handles (SelItem table DAT_110c8fd0, 19 x {id, hit bit, mesh, count}, DATA; default edit map ResetEditMap RG l.1228):**

| SelItem | enabled by | mesh (MESH array) | extent in mesh units | EditMode |
|---|---|---|---|---|
| 2 origin ball | ORIGIN | 110c9f80 x6 | sphere r ~0.022 | 1 origin |
| 3 / 4 / 5 axis shafts | X/Y/Z_AXIS | 110c9870 / 98c0 / 9ca0 x3 | 0 .. 0.3048 along the axis, r ~0.0095 | 2 / 3 / 4 translate along X/Y/Z |
| 6 / 7 / 8 arrow heads | X/Y/Z_AXIS | 110ca010 / 9910 / 9a20 x11 | 0.3048 .. 0.3759 | 5 / 6 / 7 align that axis (IsAlignEdit) |
| 9, 10, 11 X-rotation arc (end, middle, end) | X_ARC | 110c9cf0 / 9b30 / 9bf0 | quarter ring r ~0.21-0.26 in the YZ quadrant | 0xd, 0x13, 0xf |
| 12, 13, 14 Y-rotation arc | Y_ARC | 110c9400 / 9520 / 9250 | XZ quadrant | 0xb, 0x16, 0x10 |
| 15, 16, 17 Z-rotation arc | Z_ARC | 110c9310 / 9630 / 9750 | XY quadrant | 0xc, 0x12, 0xe |
| 0x12 / 0x13 / 0x14 plane quadrants | X/Y/Z_PLANE | 110c9198 / 95e0 / 91b0 | quarter disc 0.254 | 8 / 9 / 10 face edit |

Rotation modes 0xb-0x10 = RotType 0 (the arc ends), 0x11-0x16 = RotType 1 (the middles) (IsRotationEdit). GrRefGnomon adds
SelItem 0x15 (its sphere / ruler, EditMode 0x17) and 0x16-0x18 (the ruler for X/Y/Z, EditMode 2-4) (GrRefGnomon::Select GS l.482,
GetEditMode). Labels "X" "Y" "Z" (GD l.148) are text, not hit.

**Size and zoom** (GD Tessellate l.448-475, GetBounds l.2829, Select GS l.124): mesh -> model scale s = size x 2.65 x k / ppu, ppu =
PvcToDc(0,0) x pipe+0xc0 (model units -> pixels), k = +0x310 (1.0; SetSize clamps it to 1). So the gnomon has a constant screen
size: the arrow tip 0.3759 x 2.65 ~ 1.0 x size pixels (size 120 -> ~120 px), the shaft 0.3048 x 2.65 = 0.81 x size. Frame = the
gnomon matrix C (+0x328; during origin / axis edits the drag matrix +0x3b0), translation edits add the ruler vector (+0x500 / s),
rotation edits pre-multiply the compass rotation. SetSize: size < 40 -> 40 (GD l.2077); the dialog limits 50-250.

**Colours** (GrGnomon ctor GD l.97-143, ColorIdx = +0x100 + 0x18 i; SetProps GD l.1998 uses rgb + alpha for 6-9 else the
caller's alpha, render type 8 lit; no SetColor call was found in the decoded mccore / mccadutil paths):
0 inactive gray 66,66,66 · 1 highlight yellow 255,255,0 · 2 origin gray 150,150,150 · 3 X red 255,0,0 · 4 Y green 0,170,0 · 5 Z blue
26,26,255 · 6/7/8 planes white alpha 50 · 9 highlighted plane yellow alpha 80 · 10 yellow · 11/12 red · 13 0,180,0 · 14 edit text
black · 15 edit background white (GrEdit). DrawAxes (GD l.717): the state +0x304 == 0 (not selectable) draws everything in 0;
otherwise the origin 2, X shaft / head / X-arc 3, Y 4, Z 5, planes 6/7/8, the hovered item 1 (planes 9) (GetHighlightMask; a
hovered arc end also lights the axis it lies next to: items 0xc / 0xf -> X, 9 / 0x11 -> Y, 0xb / 0xe -> Z). Items drawn as lit triangle meshes (GrPolyTriFace /
GrTriStrip, DrawItems GD l.1131); plane quadrants as a 2-D pie of radius s_PlaneRadius (0.254) in the plane (DrawPlane);
labels in Tahoma (bold when size < 41), height 0.3 x the shaft box (s_LabelHeight), at the arrow tip + (len x 0.5 + 0.4) x height
(DrawLabel). On the full-alpha pass each shown arc also gets two small circles (radius 0.007747, grey 0.75) at 30° and 60° on
radius 0.254 (DrawAxes l.818-893, 960-1098).
During a drag the rest of the gnomon is drawn at alpha 0.1 (+0xf98, DrawAxes l.812). Draw order by depth (GetAxesDrawOrder).

**View-dependent hiding and picking** (PrepDrawMasks GD l.1691, Select GS l.95-119): for each gnomon axis a = |axis . view Z|:
a > 0.995 (pointing at the viewer) -> that axis's shaft / head / label are not drawn or picked; a <= 0.1 (axis in the screen
plane) -> the arc *about* that axis (edge-on) is hidden. **Hit test** = the screen point's view ray (z 0 .. -1) into the gnomon's
mesh space ((C x s)⁻¹), intersected with every enabled, visible handle's triangles (GrPolyTriFace / GrTriangleData::Intersect);
the nearest hit wins (SelItem 0x12 only when nothing else was hit) (GS l.155-173). Hover highlights it (Highlight).
GrRefGnomon also tests its sphere first (SelItem 0x15) and maps translation items to 0x16-0x18 when asked (GS l.609-660).

**Drags** (SelXformGnomon::Drag MC 0x180a538c0 -> GrRefGnomon::Drag -> GrGnomon::Drag, decoded in the first part):
- Origin (mode 1, DragOrigin 0x10ee2210): an AutoCursor 3-D point is taken as is (3-D snap, +0x322); else the screen point is
  projected (MtVector2d::Project, tol 5e-5) onto the gnomon's XY plane. A floating X,Y,Z edit (UIFloatingCoordEdit, 31 chars;
  SelGnomonBase::ProcessEditOriginKEYDOWN MC 0x180a64e90, enabled by EnableOriginFloatingEdit) takes **typed coordinates** when a
  key is pressed during the edit; Enter applies them (SetEditOrigin -> SetEditTranslation).
- Translate along an axis (modes 2-4, GrRuler::DragRuler RC l.1473): the ruler plane is the s_PlaneDef plane containing the axis
  that faces the view most; the mouse is projected onto it; value d = signed distance from the ruler start minus the zero offset
  (+0x1c0). **Snapping**: while the cursor is inside the ruler band (perpendicular distance between 0.03429/q and 0.10287/q +
  the ruler label width, q = ppu / (ruler size x 8.44)), d is rounded to the ruler's minor step (floor((d + i/2)/i) x i, ceil for
  d < 0); outside it d is free; an AutoCursor 3-D point projects exactly; snap-to-zero within 0.02/q; limits / zero exclusion
  (+0x1a8 / +0x1b0 / +0x1b8, unset here). The step i (+0x1d0) = the ruler's tick unit (+0x1c8, ScaleRuler RC l.2313: metric
  0.1 of the decade unit chosen by zoom (mm / cm / m / km by MtCnvLength thresholds 0.5 / 500 / 50000 / 5e6); inch 1/64 .. 1/4 in,
  1/12 ft, ft) then multiplied by the subdivision (+0x1dc: 10 metric, 2 inch) while the step is under p pixels, or divided by it
  while over p x subdivision, p = (ruler size / 29, min 1) x (+0x1d8: 5 metric, 8 inch) (DrawRuler RC l.620-647, l.3007). Result: C := Translation(axis x d) C.
- Align (5-7) / face (8-10) (DragAxis 0x10ee24c0): needs a picked entity (SetEventFlags MC 0x180a65870: the pick filter from
  s_SelMask, align: lines, arcs, splines, solid edges; face: solid faces, surfaces); the axis is aimed along it
  (SetAlignedXAxis).
- Rotate (0xb-0x16, GrCompass::DragRotation RC l.3965): the mouse is projected onto the plane normal to the axis; angle =
  atan2 in that plane - start angle (+0x1b0), normalised. **Snapping**: when the cursor is inside the compass's inner radius
  (+0x4a0 / pixel scale x +0x4a8; where those two are set is NOT DECODED) and rotation snap is on, round to s_AngularInc (default 5°): floor((a + inc/2)/inc) x inc; with
  the H/V snap and the view along the axis, snap to 0/90/180/270 (table @0x10fa39f0) within 10 px; an AutoCursor 3-D point
  sets the angle exactly. Typed value: a key opens a floating number edit (UIFloatingDoubleEdit, 9 chars, SelObj::ProcessKEYDOWN
  0x180a24ff0, mccore_gnomon_settings.c l.814) for the edit value = the angle (GrGnomon::GetEditValue EV: rotation -> MtNormalizeAngle(+0x9b8); translation ->
  the ruler distance); SetEditValue (EV) applies it (translation: SetEditTranslation; rotation: GrCompass::SetRotationAngles).
- On-screen read-out (Tessellate GD l.658-699): during a drag a GrEdit label at the cursor + (24, -24) px shows the ruler text,
  the angle "%.2lf°" or the origin "%.3lf, %.3lf, %.3lf" (black on white). The ref gnomon draws a dashed line from the
  drag start to the origin and a sphere of radius size x 0.065 / ppu there (RG Tessellate l.120-405).

**Gnomon settings** (SelGnomonBase::RunSettingsDlg MC 0x180a65570, dialog GnomonSettingsDlg template mccoreres 25610 "Gnomon
settings"; values in the SelGnomonBase statics, initial .data @0x181325d70; read once from / written to the registry
`<REG_KEY>\Settings\Gnomon` by ReadFromRegistry 0x180a653a0 / WriteToRegistry 0x180a65a40):

| control (id) | value | default | limits | registry |
|---|---|---|---|---|
| Axis alignment: Line (along) 10519 / Arc (tangent/normal) 10520 / Spline (tangent) 10521 / Solid edge (along) 10522 | s_SelMask bits 1/2/4/8 | 0x3f (all) | | "Sel mask" |
| Face alignment: Solid face (normal) 10523 / Surface (normal) 10524 | bits 0x10 / 0x20 | on | | |
| Activate angular snap 10526 | s_AngularActive | on | | "Angular active" |
| Angular increment (deg) 10525 | s_AngularInc | 5.0 | 1-90 | "Rot inc" |
| Activate ruler 10518 | s_RulerActive | on | | "Ruler active" |
| Ruler size (pixels) 10528 | s_RulerSize | 35 | 20-70 | "Ruler size" |
| Gnomon size (pixels) 10527 | s_GnomonSize | 120 | 50-250 | "Gnomon size" |

OK applies them to the gnomon (SetSize, SetRotSnap, SetRotSnapEnabled, ruler enable vt +0x190, SetRulerSize) and writes the
registry when anything changed. The path from Ctrl-right-click to RunSettingsDlg (vtable slot 20 of SelRefGnomon) is NOT DECODED;
the prompts 455-474 name it.

**xform_silent** (mccore 0x1808d9da0, XS): `xform_silent(DB_LIST_ENT **list, char copy, x_matrix *M, xform_type t, char *ok, int n,
char flag)`; it marks the start entity (bit 4), checks DfXformCheck, then calls FUN_1808d8820(0, flag, 0, 1, 0, zero vec, keep
attrs 1, move = !copy, M, t, scale = M[0] for t == 2 else a constant, n, 0) (XS 0x1808d9f15-0x1808d9fc6). FUN_1808d8820 loops k
= 1..n: transform the originals by the current matrix, then current := M x current (mul_xmatrix, mccore_xfinfo.c l.542) —
**instance k = M^k** [code] (how a Move with n > 1 places the originals is NOT DECODED). Results get sel bit 8 (xform_store_ent, mccore_xform.c
l.1350-1352) and FUN_1808d8820 first runs set_clr_sel_bits_all(2, 8, 10, 4, …) (mccore_xfinfo.c l.197). Dynamic leaves bit 8
(its end clears only bit 4: set_clr_sel_bits_all(0, 4, 0, 4, …), MX l.13580 area) and clears 2|8 at the next selection
(FUN_1800ea720): **results show in the Result colour** (config 5044) like the other transforms [code]; the group mark of the
originals in this path [inferred] as the common machinery.

**NOT DECODED** (after 2026-11-05): the Ctrl-right-click -> RunSettingsDlg call path; where the compass inner radius (+0x4a0 /
+0x4a8) and ruler limits are set; the exact triangle shading (UtRender type 8 parameters beyond the HSL colour); the event-mask
bits of get_event; the display-list state meaning; Ordinal_12994 on the main frame (FUN_1800ecdc0); strings 435-439 (not
referenced by the decoded code; XformDynamicManagerDialog @1801a9640 is the MFC host [inferred]); the source of panel codes 0x12 /
0x22 / 0x23. The handle meshes are data (MESH): a port draws / hit-tests them as dumped.

**Port mapping** (cui_xform.c): data side = a new G_XDYN using XM (T = C ∘ B⁻¹ in xm_mul's column order; instance k = T2^(k-1) ∘
T ∘ A), xf_make / xf_commit for undo and preview, IsCopy as Move/Copy, CopyCount as copies; Origin / Axes buttons and
Single / Multiple / Only-last / Ctrl-Apply are pure data and portable now. UI side = a gnomon widget in the viewer (origin drag
via cui_snap.c AutoCursor, axis-translate drags, rotate rings): its look and snapping must be ours (labelled) or wait for the
NOT DECODED items.

**Ported 2026-11-05** (viewer/core_ui/cui_xform.c G_XDYN = the round / data side, cui_xdyn.c = the gnomon, cui_xdyn_mesh.inc
generated by tools/gen_gnomon_mesh.py from MESH / DATA; scene.c scene_set_gnomon; Qt viewwidget.cpp routes the view's mouse to
xdyn_press / xdyn_move / xdyn_release). Note: colour 0 (inactive) is 0x66 = 102,102,102 (GD l.97), not 66. The align permutations
are computable (ctor GD l.284-289): DAT_110d8648 = (Rz(90) Ry(90))^T puts the aligned axis into X (modes 5 / 8), s_PlaneDef =
(Rz(-90) Rx(-90))^T into Y (6 / 9), none for Z (7 / 10) - SetAlignedXAxis's frame has the entity direction as its Z.
Ours / left out: listed in the headers of cui_xdyn.c and the Dynamic block of cui_xform.c (shading, ruler / compass drawing,
the gnomon button as a panel button, the settings dialog as a panel group, rotation snap everywhere, ruler band width 0,
align to lines only, phantoms not drawn apart, typed values). Tests: wf scenes xf_dyn_* (verbs dynplace / dyndrag / dynctrl /
dynbtn / dyndump, xfset copy / single / n / last / setgeom / gsize / ainc / aon / ron / rsize, xfbtn 0x1a-0x21 / 15 / 90 / 101).

### The port (viewer/core_ui/cui_xform.c, 2026-10-23) - ours / deviations
- Ported: Translate, Translate to Plane (plane combos + origin picks), Rotate, Move to Origin, Mirror, Rectangular Array,
  Stretch, Scale, Home > Clear Colors (+ the right-click mini toolbar's), the group / result colours, Use New Attributes,
  Move / Copy / Join, copies, directions, preview / Regen, Apply loop, undo (one step per OK / Apply).
- The dialog is the function panel (geo_panel_body -> xf_panel_body): template texts / tooltips as labels; the bitmap
  statics' labels (#1219.., #25200..) are NOT DECODED - the decoded parameter names are shown (Delta X, Angle, Length ...);
  the Direction MCButton is three radios ("One way" / "Opposite" / "Both": ours, the states are decoded); the panel keeps
  one layout (the 2nd templates' Attributes fields / Scale's Uniform and XYZ groups are always listed, enabled by their
  switch).
- select_ents: this program's entity selection, ended by Enter (xf_enter); Stretch's windows are two picked corners in
  world XY (ours; Mastercam drags a screen window); points, lines and arcs only.
- Copies are new entities (new ids); Move deletes the originals (Mastercam rewrites them in place, keeping their ids and
  the operations' references - here the operations keep the old entity until rechained, the existing edit warning).
- Arcs whose image is not in XY or a system view's plane, and arcs under a non-uniform scale (Mastercam: splines), are
  refused with a message; solids and toolpaths are not transformed.
- The parameters live for the session (XFORM.PRM is not written; each command resets as decoded); the marks are not
  saved with the part; Fit, Help, Remove / Reset Items, the on-screen handles (SelObjHelper), the plane dialogs, the
  3-point plane definition, the Scale Calculator and the Level / Color pickers are greyed.
- Tests: tools/tests/wf_scenes.txt xf_* (36 scenes; xfset / xfbtn / xfenter / xfdump / xfents / clearcolors steps).

## Code: viewer/mcamui.c wireframe commands, viewer/geopanel_ui.inc, viewer/geo2d.c

**Commands** (mcamui.c "wireframe" section, `gcmd[]`): 42 commands - Point Position / Dynamic / Segment / Endpoints /
Small Arcs, Bolt Circle, Line Endpoints / Parallel / Perpendicular / Closest / Bisect / Tangent Through Point, Circle
Center Point, Arc 3 Points, Arc Tangent, Circle Edge Point, Arc Endpoints, Arc Polar, Arc Polar Endpoints, Rectangle,
Rectangular Shapes, Polygon, Ellipse, Helix, Spiral, Bounding Box, Trim Break Extend, Break Two Pieces, Join, Break at
Intersection, Break Many, Trim Many, Break at Points, Fillet / Chamfer Entities and Chains, Offset, Offset Chains,
Project, Close Arc, Break Circles. Not available (`geo_unsupported[]`, need splines / surfaces / solid edges): Point
Nodes, Line Normal, the Spline and Curve commands, Create Letters, Silhouette Boundary, Turn Profile, Relief Groove,
Raster to Vector, Combine Views. A command is a list of picks (point / entity) run when complete
(`geo_run` -> `geo_exec`, one undo step), then the live pattern of "Live results" (`geo_live_kind` flags LV_LIVE /
LV_REBUILD / LV_PREFIX / LV_ESC_DROP / LV_PICK_MOVES / LV_PREVIEW; `geo_live_start` / `geo_live_update` /
`geo_live_commit` / `geo_live_drop`; `geo_cancel` = Esc / Cancel). One parameter set per command (`gps[]`, `GeoParams`;
`gp` = the running one; panel values `v_*` and locks `lk_*`). Point picks snap to the grid and to endpoints / centres
/ points within 8 px (AutoCursor-like; Fast Point typed coordinates NOT DONE).

**Chain commands** (since 2026-10-21, both apps; formats/qt_port_plan.md "Wireframe chain commands -> Mastercam
chaining"): Fillet Chains, Chamfer Chains and Offset Chains start the real Chaining dialog (core_ui cui_chain.c
`chain_begin_geo`, every dialog mode) instead of a single chain pick. `geo_chaining` = chain_it running (the panel is
greyed, `geo_panel_enabled`); the dialog's OK hands its chains over (`geo_chains_done`, through the UI's
`cui_hooks.chain_ok`; Last keeps them, as cleanup_chaining's lastchn.tmp); the command works on every chain of
`geo_chains` in one undo step (`corner_chain` / `offset_chain` per chain inside one mod_begin / mod_end); a panel change
rebuilds from the same list. Panel codes (`geo_chain_button`): Apply = commit + chain again (Fillet / Chamfer only with
chains: `geo_panel_apply_enabled` = EnableApply); OK = commit + end; Cancel = `geo_cancel`; Reselect (code 4, a live
button now) = drop the preview + chain again. Esc / the dialog's close box while chaining ends the command. Offset
Chains: no side point any more - 'L' of each chain (`offset_chain(c, +1, d)`), a click on a chain entity reverses that
chain and rebuilds; live in the main colour (`LV_MAINCOL`: the Xform preview colours are NOT DECODED), Esc / Cancel
drop it (`LV_ESC_DROP`, "100 deletes the preview"). An entity in two chains of one list: the later chain is skipped
(NOT DECODED).

**geopanel_ui.inc** (the function panels; formats/improvisation_review.md UI items 6 + 11):

| part | behaviour | source |
|---|---|---|
| `PKEYS[]`, `pk_read_all` / `pk_write_all` / `pk_note` | 35 persisted EvaluatorBox keys (e.g. WireframeLineEndpoints-Length, WireframeFilletEntities-Radius, WireframeTrimBreakExtend-ExtensionLength) per unit system; changed values written at once | key names: decomp/mccad_panel_keys.c, decomp/mccadutil_panel_keys.c; file = ours |
| `geo_defaults(c, metric)` | first-use defaults per command and unit (e.g. Bolt circle 100 mm / 5 in, hole radius 5 / 0.25; Arc Tangent radius 25 / 1; Fillet / Chamfer 5 / 0.25; Trim extension 1.0 / 0.1; Break Many 0.5 / 0.1, tol 0.002; Offset 25 / 1; Offset Chains 10 / 0.5; Helix / Spiral radius 20 / 1, pitch 2 / 0.1; Line Endpoints length 2 x tol) then the stored keys | managers' registered keys, FUN_1800bd490 bools, BoltCircleParams FUN_18005f660, polygonOp_params_tag DAT_18064c3c0, RegisterVariable pairs (this spec) |
| `geo_panel_sync` | panel -> geometry values each frame; lock rules: Line Endpoints length + angle locked or H / V + length (FUN_18018f480); Line Parallel / Perpendicular / Tangent, Circle, Arc Endpoints, Rectangle / Shapes, Polygon use the value only when locked (lk_* nonzero: the hard or the soft lock - GetLocked(false); "Soft locks per command") | decomps cited per line |
| `fp_*` widgets | `fp_txt` drops access-key underscores; `fp_exp` = AdvancedExpander (open by default); `fp_radio` / `fp_check` / `fp_num` / `fp_int` (EvaluatorBox + lock); `fp_toggle` = Reselect / 1 / 2 / 3 ToggleButtons, greyed except the chain commands' Reselect (posts code 4); `tbtn` = ToggleButtons bound to a value (Line Endpoints' Endpoints 1 / 2 = CurrentlyEditing) | formats/ui_wireframe_panels.txt |
| `geo_panel_body` | each command's expanders, controls and texts in BAML order; MFC-dialog commands (Bolt Circle 10266, Polygon, Rect Shapes, Ellipse, Helix, Spiral, Offset 6453, Offset Contour 6460, Project 6462) use the template group / check / radio texts, value labels = decoded parameter names | BAML outlines, mccadres / mccadutilres templates |
| `geo_panel_content` | FunctionPanel: OK (code 0: apply if picks complete, end), Apply = OK-New (code 1: commit, the command goes on), Cancel (code 2: `geo_cancel`), Help greyed; "Basic" tab; docked as a tab of the left managers' dock bar, inserted between "Levels" and "Recent Functions", active while the command runs (a manager tab is reachable mid-command; the tab goes when the command ends) | "Live results" panel codes; the docking = Mastercam.Workspace `<BCGPDockManager-128>` `UIDialogbarTabbedControlBar` (tabs Toolpaths, Solids, Planes, Levels, "Circle Center Point" 0xbe8, Recent Functions 0xee8 - the function panel lives in the same tabbed dock bar) |

Greyed (not ported, from the code): every Reselect / 1 / 2 / 3 toggle except Fillet / Chamfer Chains' Reselect, Tangent modes, Thread / Cut Point, Arc
Tangent mode 3 (dynamic), Line Bisect Multiple, Fillet styles other than Normal and Fillet Chains +/- Sweeps, Chamfer
Chains Trim entities, Bounding Box All shown / Cylindrical / Lines and arcs / Solids, Rectangle Create surface.
Deviations (code header): the keyed values persist in `<config dir>/panel_keys.tsv` (Mastercam does NOT store the values
there: it keeps an autocomplete history under the key, see "Panel key storage" below - the cross-session value file is ours; scripted runs with MCAMUI_SCRIPT neither read nor write it); the panel title is the command name
(FunctionPanel caption source NOT DECODED); the EvaluatorBox lock is a check box. NOT DECODED: Help topic, the MFC
dialogs' bitmap-button tooltips, inch defaults that have no registered key.

**geo2d.c / geo2d.h** - own 2D construction math (SUBSTITUTE for the MCCAD / MCCADUTIL geometry kernels; the
behaviour rules come from the sections above, the formulas are ours). `GE` = point / line / arc (centre, radius,
start, sweep stored counter-clockwise) at one Z.

| function | behaviour |
|---|---|
| `ge_point` / `ge_line` / `ge_arc`, `ge_is_circle`, `ge_at`, `ge_dir`, `ge_len` | construction and evaluation (t 0..1, outside = extension) |
| `ge_param`, `ge_nearest`, `ge_dist`, `ge_on`, `ge_side` | projection on the carrier (infinite line / full circle), distance, side (+1 left of a line / outside a circle) |
| `ge_sub`, `ge_trim_keep` | piece between two parameters; trim / extend at tx keeping the side of tk (Trim Break Extend, fillet trim) |
| `ge_intersect` | carrier intersections, up to 2 |
| `ge_offset` | offset to the left of travel (Offset, Line Parallel) |
| `ge_fillet` | fillet arc between two entities on the side of each where the other's picked part is (second pass: other side for arcs); of the candidates, the centre nearest both picks |
| `ge_circles_tan2`, `ge_circles_tan_pt`, `ge_tangent_points` | tangent circle centres (Arc Tangent modes, Circle Edge Point), tangent points from a point |
| `ge_closest` | shortest line between two bounded entities (Line Closest) |
| `ge_circle3`, `ge_arc3` | circle through 3 points, arc a -> m -> b (Arc 3 Points, Circle Edge Point 3 points) |

## Panel key storage (decoded 2026-10-22)
The EvaluatorBox key names (decomp/mccad_panel_keys.c, `FUN_1800df740(model, "WireframeRectangle-Width", ...)`) name a
`FunctionPanelEditableDouble` (mastercam.dll exports; decomp/mastercam_autocomplete.c, decomp/mastercam_autocomplete_mru.c).
The key is the field's **autocomplete history** (an MRU list for the field's drop-down), not a stored value. [code]
- Constructor `FunctionPanelEditableDouble(char *key, double, double, double)` @18018ffb0: key at +0xa8, "DecimalPrecision" =
  n_places_an, "HasAutoComplete" = 1.
- `LoadAutoCompleteList` @180192170: opens `REG_KEY\Gui\Panel AutoComplete History\<key>` (s_Gui, s_PanelAutoCompleteHistory,
  separator "\"); DWORD `NumValues`; for i < NumValues the string value `Input%d` (s_InputValue, string @0x1805bfa38),
  `atof`, added through vtable +0xd0 with cap `numValuesInMRUList`.
- `SaveAutoCompleteList` @180192a50: opens `REG_KEY\Gui`, creates `Panel AutoComplete History`, DELETES the field's key and
  re-creates it, writes `NumValues` = list size and each entry as `Input%d` = `"%.*f"` with 15 places (format @0x1805e0464).
- `AddToAutoCompleteList` @180191b10: only when `GetLocked(false)` (byte +0x31 or +0x32: the field's lock flags, the same
  bytes that stop `SetValue(v, false)`): adds the current value (vtable +0x28) with cap `numValuesInMRUList`.
- the add (vtable +0xd0 = FUN_1801919c0): an equal entry already in the list (std::find over +0x80) is removed, the value is
  appended at the END, and when the count exceeds the cap the entry at index 0 (the oldest) is removed. Newest = last.
- `numValuesInMRUList` (exported u8 @0x18087b850, .data) = **4**.
- When (Rectangle, decomp/mccad_cmds/sketcher_rect.c FUN_1801e3ca0): Load right after `CreateFunctionPanelDialog`
  (FUN_1801e3780, both fields), Add after each committed rectangle (FUN_1801e3430, l.641 / 790), Save just before
  `DestroyFunctionPanelDialog` (FUN_1801e53d0). The other commands call the same three helpers (mccad_lines_helpers.c).
- Evidence: the user's hive (`<windows>/Users/<user>/NTUSER.DAT`, `Mastercam 2017\GUI\Panel AutoComplete History`) has
  10 such keys, e.g. WireframeRectangle-Width NumValues 4, Input0..3 = 5.740000000000000, 6.750000000000000, 5.750000000000000,
  5.000000000000000; keys whose history is empty have NumValues 0. No other value of these keys exists in the hive, so
  Mastercam does not carry a field's current value over to the next session through the registry.
- NOT DECODED: the drop-down's look (WPF EvaluatorBox), whether picking an entry locks the field, the lifetime of the
  command's panel model within a session (whether the value survives to the next run of the command).


## Curves on surfaces (Wireframe > Curves: sketcher_curve_1 .. _9, MCCAD.DLL; decoded 2026-11-09, N9-16)

Dumps: decomp/curves/ (core1 / core2 / core3 / ma_classes: mccore; cad1..3: mccad; `~/mastercam-re/ghidra_core` / `ghidra_cad`). The nine commands are thin panels
around mccore: the surface kernel's MaSurface / MaCurve classes make the curves. Resource strings below are mccadres numbers unless noted.

| ribbon | FT | panel manager | loop | core function |
|---|---|---|---|---|
| Curve One Edge | sketcher_curve_1 | IWireframeCurveOnOneEdgeManager, keys `WireframeCurveOnOneEdge-BreakAngle` (FUN_18021f380) | FUN_1802a2940 | MakeSurfaceEdgeCurve 0x1802735c0 (nearest edge to the dyno pick), sld_convert_tag_to_edges for solid edges (event 0x4000) |
| Curve All Edges | sketcher_curve_9 | IWireframeCurveOnAllEdgesManager (OpenEdgesTolerance, BreakAngle) | FUN_1802c3f10 | MakeAllSurfaceEdgeCurves 0x1802732f0 per surface, surface_open_edges when Only Open Edges (FUN_1802c4a70), solids sld_convert_tag_to_edges |
| Curve Constant Parameter | sketcher_curve_2 | IWireframeCurveConstantParameterManager (ChordHeightTolerance; Direction U / V / Both) | FUN_1802980f0 | CreateConstantParameterCurves 0x18023c8e0 |
| Curve Flowline | sketcher_curve_3 | IWireframeCurveFlowlineManager (ChordHeightTolerance, ChordHeight, Distance; Number; U / V) | FUN_180287a40 -> FUN_180287870 | CreateSurfaceFlowLines 0x18023cc20 (GenerateFlowlinesByChordHeight / ByDistance / ByEqualParametericIntervals) |
| Curve Slice | sketcher_curve_5 | IWireframeCurveSliceManager (Spacing, Offset, Plane, Join, Find multiple) | FUN_18028d0d0 | slice_sel_surfs_with_a_plane, slice_sel_curves_with_a_plane |
| Curve at Intersection | sketcher_curve_8 | IWireframeCurveIntersectionManager (ChordHeightTolerance, Set 1 / 2 offsets, Join, Multiple) | FUN_1802b8960 | intersect_multiple_surfs 0x180562730 -> MaSurfSurfIntersect (not int_2_surfs); see "Curve at Intersection" below |
| Curve Parting Line | sketcher_curve_7 | IWireframeCurvePartingLineManager (ChordHeight, Distance, Angle) | FUN_1802be7b0 | parting_line 0x180566740 |
| Curve Surface | sketcher_curve_6 (no panel) | - | - | project_chains_onto_surfs: converts selected curves lying on surfaces to surface curves (mask 0x8e); see "Curve Surface" below |
| Curve Dynamic | sketcher_curve_4 | IWireframeCurveDynamicManager (ChordHeightTolerance) | FUN_180282390 | dyno_surf + make_spline_from_pts / create_nurbs_from_points |

Every loop [code]: panel values read through a FUN_ reader, `get_event` with the mask, `UndoTransStart` + `sktcher_live_color` on a build, the next pick / Apply / OK `UndoTransCommit` + `sktcher_main_color`, panel changes rebuild (no dyno, the same (u, v)). The loop exit (OK, Cancel code 2, Esc) commits as well (FUN_1802980f0 LAB_180298acf); only the inline undo events (0x8000, codes 9 / 0x2d) `UndoTransTerminate`. Solid faces come through event 0x2000 `sld_face_to_temp_file` + `read_next_solid_surf_from_temp_file` (the face's Mastercam surface; `sld_convert_tag_to_edges` for edges).

### dyno_surf and Curve Dynamic (viewer/core_ui/cui_dsurf.c)

`dyno_surf` (mccore 0x1803210c0, decomp/curves/core1.c): the arrow on the surface. Modes 3 (one position; Constant Parameter, Curve One Edge) and 5 (point collection; Curve Dynamic; param_5[0] 1 = xyz, [1] = 1500 doubles, so at most 499 points). Ported: the screen delta -> (du, dv) through the projected tangents (`v_lincoeffs` 0x1804b8900, `proj_iso_pt`), wrap on a closed direction (surf_type +6 bits 0 / 1), the trim hold (`in_polygon` 0x18046c960, `hit_polygon` 0x18046be80 + `hold_uv_to_trim` 0x1804c41d0), clamp on an untrimmed surface, the mode-5 points (S(u, v) at each click), the Enter / Cancel exit (code 0xb, with the points already collected), a click on the AutoCursor's point projected onto the surface (`FUN_1803204b0`, `project_pt_onto_surf`). Not ported: the fill-drag branch (`dist_to_surf` of a surface hover, event 0x2000 of a solid face): it needs local_2c8c (a solid hit); the callers here pass param_10 = param_11 = 0, which keeps cVar7 at 0 without a solid under the cursor. Inferred: a click with an AutoCursor point is event 1 (code 0xf), a plain click event 4 (code 0xe): `get_event` (mastercam.dll) was not decoded; `project_pt_onto_surf`'s core FUN_1805ed4a0 was not read, ours is `mcsc_uv_of_point`; the key 'A' accepting a position in mode 3 / 5 (dyno_get_event: event 8 key char) is not implemented (the key char's meaning for a plain letter is not decoded); the arrow is drawn in the srf_ov style (GrTemp_AddArrow size 0.1).

Curve Dynamic (G_CDYN, sketcher_curve_4, FUN_180282390): get_event mask 0x100 (a surface; prompt 5968 "Select a surface"), dyno_surf mode 5 with param_4 = 1 (prompt 5867 "Select points. Press <Enter> when done"), Enter or Cancel ends it with the points already taken; more than one point: `make_spline_from_pts` (cfg_createNurbs off) or `create_nurbs_from_points`, natural ends, main colour and level (`sp_fit_points`, the mcspline_fit.c ports); the check is "more than one point", no minimum of 4 for a closed point list. Fewer than two points: nothing and no message. Prompt 5969 after a build, 5968 again after OK / Apply. The panel (`WireframeCurveDynamic-ChordHeightTolerance`, FUN_18023d6c0: the value, surf_step_chord_dev when below tol) does not change the fit; a panel change rebuilds from the same points (cVar6 stays set, dyno_surf runs once per surface). The event's start (u, v) is ours: `mcsc_uv_of_point` of the point the click returned. Not ported: a solid face (event 0x2000, read_next_solid_surf_from_temp_file): the picked solid is not offered here, the surface mask only.

Checks (scenes cdyn_cyl, cdyn_plane, cdyn_one, cdyn_esc, cdyn_trim; unverified against Mastercam): the cylinder r 12 through (3, 4), height 25: three clicks on the AutoCursor at the end points and (3, 16, 12.5): a NURBS of degree 3 through (15, 4, 0), (3, 16, 12.5), (-9, 4, 25) - chord knots 0 .. 42.1545 with the interior knot at 21.0772, the spline from `convert_points_to_nurbs`, checked by the spline oracle (mcspline_fit.c, within 7e-13 of mccore). Planar NURBS ent 47 (Top): cursor moves (0.2, 0.1) and (0.4, 0.15) in view coordinates map to exact (1.4575, -4.7375, 0) and (1.6575, -4.6875, 0), so S(u, v) moved by the screen delta. Esc after two clicks builds the same; one click builds nothing. Trimmed ent 46 (kind 10, Front view after the view animation): a drag of 5 inches stays on the loop (in_polygon returns 0 for the final uv); the further drag back keeps the state "outside" and stays (the decoded local_2ff8 latch).

### Curve One Edge and Curve All Edges (untrimmed NURBS; viewer/mcsrfcrv.c, cui_geo.c; 2026-11-09)

Curve One Edge (G_CEDGE1, sketcher_curve_1, FUN_1802a2940): get_event mask 0x100 (a surface; prompt 5968), dyno_surf mode 3, param_4 0 (prompt mccoreres 5445 "Move arrow to Desired Edge of Surface"); a click makes the edge curve at the arrow's (u, v); Enter / Cancel nothing and the prompt 5968 again (`local_2f27`, the "goto LAB_1802a32c8" branch not taken), else 5969. `MakeSurfaceEdgeCurve` (0x1802735c0): `BoundaryClosestToUVPoint` (0x1804800a0): the nearest non degenerate boundary in the (u, v) plane (U0, U1, V0, V1, first of equal distances), `MaSurface::Flowline` at it (FindClosestEdgeToUVPoint 0x180483d20), each through Simplify (line / arc) or `MakeSplineOrNURBCurve`. Fit arcs and lines (`FitArcsAndLines`, FUN_1802a2940 `local_2e78`) changes only piecewise linear NURBS, which skip Simplify in both branches: no effect here. Break Angle (`WireframeCurveOnOneEdge-BreakAngle`, FUN_18021f380 default 1.0, enabled only for a trimmed surface): the trimmed path only (NOT PORTED). The solid edge event (0x4000, sld_convert_tag_to_edges) is NOT PORTED. Panel: `wireframecurveononeedgemanagerview.baml`.

Curve All Edges (G_CEDGEA, sketcher_curve_9, FUN_1802c3f10 / FUN_1802c4a70): the selection (select_ents mask 0x10100, prompt 6015), then per selected surface `MakeAllSurfaceEdgeCurves` (0x1802732f0): `MaSurface::GetEdgeCrvs` (0x180485930): the boundaries v0, v1, u0, u1, a degenerate one skipped and v1 / u1 skipped when v / u is periodic, each iso through Simplify / `MakeSplineOrNURBCurve`; panel `WireframeCurveOnAllEdges-BreakAngle` 30.0, `-OpenEdgesTolerance` (FUN_180224880: the larger of tol and sld_stitch_tol; ours tol, `sld_stitch_tol` not read), `Only Open Edges` greyed (surface_open_edges not dumped: NOT DECODED), Fit arcs and lines (no effect here, see above). Ours: the panel's Apply / OK builds from the current selection (the Break Many pattern), the result stays live until the next Apply / OK; the build right after select_ents (FUN_1802c4a70 at once in Mastercam) and the Reselect button are not ported. The surfaces of the build are kept for the rebuild after a panel change.

Trimmed surfaces (2026-11-09): `MaFace::MaFace` (0x180413ba0) -> `MaLoop(surface, polyline, surf_step_chord_dev, break angle)` (0x18042eb50): the stored uv loop (first point repeated; `UVTransform` to the normalised domain), `RefineByChordHeightToSurface` (1/4, 1/3, 1/2, 2/3, 3/4 of each span; a span over the chord deviation halved), `BreakAtAngleAndSrfBoundary` (the 3D chord angle against Break Angle, an edge-on-boundary change, a corner; a closed loop's last and first pieces joined within 20 degrees unless linear), `MaParameterCurve(.., double)` -> `GenPiecewiseLinearPCurve`, `ConcatenateAllSmoothlyJoinedPCurves` (5 degrees, not at corners), `CheckForClosure` (a last end within 0.001 of the first start in (u, v), and nearer than the piece's middle, is moved onto it). `GetEdgeCrvs(false)` / `GetEdgeSplineNoFit` (0x180465b80): the surface points at the pcurve's knots as an order 2 NURBS, `RegenNonUniformKnotsFromDerivRatios` (normalised cumulative chord). `IsPiecewiseLinearNURBCurve` (order 2, ncp > 2) skips Simplify; ncp 2 is a line. So `BinarySplineFit`, `GenPCurve` (cubic), `DuvDtEvaluate` and `BreakAtLinearSpans` are not on this path: they belong to the other `MaLoop` constructors. A NURBS base with a knot of full multiplicity (`GetMaxKnotMultiplicity >= degree`, and non rational or degree < 3: `this[0x1c]`) is loaded regenerated (`LoadUntrimmedSurf(.., true)`, `RegenNonUniformU/VKnotsFromDerivRatios` from the Bezier legs of each span, averaged over the isos; `ConvertUVParams`): ported as a copy of the base with the new knots. One Edge picks the nearest piece in (u, v) (`FindClosestEdgeToUVPoint`, `MaLoop::PointMinDist`: last of equal distances, an inner loop only when strictly nearer). The Break Angle default of One Edge is 1.0 as `FUN_18021f380` writes it (All Edges 30.0); stored settings override it.

Curve All Edges' flow (FUN_1802c3f10): select_ents (prompt 6015; ours: the current selection and Enter), FUN_1802c4a70 at once (live), prompt 6016; a panel change rebuilds, Apply commits and selects again, Reselect (code 4) selects again and the next Enter replaces the last build; a further Enter is OK. dyno_surf's key 'A' (modes 3 and 5) is taken as the letter key A.

**The DLL oracle** (`tools/peloader --edg`, tools/peloader/mccore_edg.c: runs the real `UVTransform`, `ConvertUVParams`, `RegenNonUniform..`, `MaLoop(surface, polyline, chord, angle)`, `GetEdgeCrvs`, and the shape tests `IsLinear` / `IsCircular` on a MaNURBSurf built from the same numbers; hook `edgbatch` / `edgcase` writes the cases and our result): at break angle 30 degrees on 181 trimmed surfaces of 171 sampled corpus parts (every 8th of the 1364 parts; 35 of them regenerated bases): 238 loops, 840 curves, piece counts all equal, control-point counts all equal, knots within 5.8e-13, points within 1.0e-13 (the DLL's shape tests on them: 325 neither, 344 line, 171 arc; every curve is order 2). Bases that are not NURBS (176 of the 1140 trimmed surfaces in the sample) are computed from the same polyline logic and evaluators; the oracle does not cover them.

**Stored curves** (hook `edgscan`, the same 171 parts): 59 of the 438 stored order-2 NURBS curves (7 parts) equal an edge piece computed here, within 1.4e-6, all at break angle 30. They are not all necessarily made by this command.

Solid edges and Only Open Edges: the section "Curve One Edge / All Edges on solids" below. Non-NURBS untrimmed surfaces (Flowline) are refused.

### Curve One Edge / All Edges on solids, Curve Dynamic / Constant Parameter on solid faces (2026-11-09, N9-16; viewer/occt_edges.cpp, viewer/core_ui/cui_cedge.c)

The conversion (decompiled from mcgeomsld 2017, `ghidra_sld`, hooks `cedump` / `sldscan`, Curve One Edge event 0x4000 and FUN_1802c4a70's lists):
- `sld_convert_tag_to_edges` (0x1800b1890): body / face / edge through `sld_convert_sel_to_mc_ents` -> FUN_180096570 (edges of bodies, faces deduplicated) -> `ps_edge_to_mc_curve` (0x1800434d0); with `open_only` the laminar edges of a sheet (`PK_BODY_find_laminar_edges`), a non-sheet gives nothing.
- `ps_edge_to_mc_curve`: FUN_180043850 (mask 0x1f1fff): `PK_EDGE_ask_geometry(edge, want_interval)`. A LINE (class 0xbb9): start / end in the edge's sense (`local_227`), length below tol: not made (param_4). A CIRCLE (0xbba): view = `make_view_k(axis)` + `find_view_m(create)`, centre / angles in that view, sweep positive when sense else negative (`restrict_angle` windows 0..2 pi around the start), below tol of arc length: not made. Otherwise FUN_180043e40 -> FUN_180044820: `PK_CURVE_make_bcurve` + `PK_BCURVE_ask`: degree 1 two points of a non-rational curve is a line, else a NURBS via `put_nurbs_in_db`, no `MaCurve::Simplify`. FUN_180044010 (`OUBSED` / `OUBSCU`, a B-spline fit of the edge at tol) when the bcurve fails or a periodic bcurve has a zero start derivative: no OCCT counterpart, not reached by any of the sampled edges.
- Curve One Edge: `get_event` 0x1000c00a mask 0x100; event 0x4000 -> `sld_convert_tag_to_edges` with UndoTransStart, then prompt 0x1751, no dyno. Curve All Edges (FUN_1802c4a70, `*param_4` = the Only Open Edges check): surfaces: `MakeAllSurfaceEdgeCurves` per surface unless open: `surface_open_edges`, bodies then the temp-file faces (a face with `local_29c3`==0: its edges; `local_29c3` set = its whole body, deduplicated) through `sld_convert_tag_to_edges(tag, list, open)`; warning 751 (0x2ef) when nothing was made.
- `surface_open_edges` (0x1800d0a30): `StitchSurfaces` (each surface `ps_surf_to_ps_sheet`, `bs_body_heal_and_sew` at the Only Open Edges tolerance) then per stitched sheet `sld_convert_tag_to_edges(.., 1)`. `sld_stitch_tol` (mcgeomsld .data = 0.005, config 8003) is the default of OpenEdgesTolerance when larger than tol (FUN_180224880).

The port: every edge from the OCCT model of the Parasolid XT (`occt_xt_edges`: one per XT EDGE node, ascending; `occt_brep_edge_list` for imported BRep solids, TopExp map order) -> `occt_fill_curve` (GeomConvert for non line / circle) -> `mcsc_emit_edge` (mcsrfcrv.c, the FUN_180043850 rules on tol, view, sweep sign; a NURBS through `emit` with Simplify off). The pick (`ce_pick_edge`, 10 px, the sampled polyline of every edge of every part solid, nearest visible): surfaces first, `get_event` order NOT DECODED, as Line Normal. Solids made in this session (solid25 Body): refused. Curve All Edges: a click on a solid adds its face (0x2000 temp-file faces, the decoded `local_29c3` == 0 path); a whole-body pick (mask 0x10000, an E_SOLID selection in select_ents) is NOT DECODED here: only the test hook `cesel:S` adds one.

Differences from Parasolid (measured):
- Direction and record of edges: `PK_EDGE_ask_geometry`'s sense relates the edge to the curve **as stored in the XT**, ours to the OCCT curve built with `CURVE.sense` applied; the arc rule therefore uses the stored normal (the opposite of OCCT's for a '-' sense circle) and the sense for the sweep sign (occt_edges.cpp `pk_along`).
- Number and place of edge breaks: for XT solids one curve per XT EDGE, exactly Parasolid's edges (OCCT's ShapeFix seams are not in `edge_of`). A ring edge (no vertex) starts at the ring's stored position (`ring_at`, ours, at the seam of its face) where Parasolid starts at the interval's start: the start of a full circle may differ. For imported BRep solids the edges are OCCT's own (seams on periodic faces add edges Parasolid does not have).
- Only Open Edges: `BRepBuilderAPI_Sewing` stands in for `bs_body_heal_and_sew`, untrimmed NURBS surfaces only. **Decision N9-28**: a selection holding a trimmed surface is refused with a NOT PORTED message until true trim curves exist (`ps_surf_to_ps_sheet` sews the real trims; the stored uv polygon broke the free edges at every point); the whole selection, because stitching the rest alone would report the edges shared with the trimmed surface as open. Kinds other than 0 and 10: skipped with a message. Decision N9-29: solids drawn in the app (solid25 Body) stay refused until the Solids work.
- NURBS: `GeomConvert` of the OCCT edge against `PK_CURVE_make_bcurve` of the Parasolid curve; see the corpus check.

Corpus check (hook `sldscan`; stored curves whose two ends equal a solid edge's, every 5th of the 1364 sample parts; 51 parts with solids scanned; these curves are not necessarily made by this command): lines 1675 / 5296 solid edges have a stored line at the same ends (1600 in the same order, 75 reversed in the first run; see the arc rule); arcs 451 / 1253 (all same geometry; 92 stored in a view opposite to the circle's axis, opposite sign of sweep: same arc, different record, unreproduced: `find_view_m` not decoded); NURBS 489 / 1389: 423 with the same degree, knots and control count, of which 422 have the same control net within 1e-6 (median 8.9e-16) and one differs (0.37 on a different parametrisation); 66 have a different degree or count (for example 3 / 2: Parasolid's bcurve one degree higher).

Solid faces on Curve Dynamic / Constant Parameter (event 0x2000 of FUN_180282390 / FUN_1802980f0: `sld_face_to_temp_file` + `read_next_solid_surf_from_temp_file`, ps_face_to_mc_surf): a face converted as Line Normal's (`occt_xt_faces` / `occt_brep_faces`, `mcsb_prim_nurbs`). Only a face whose NURBS is untrimmed (`mcsb_prim_untrimmed`) is used: a trimmed solid face is refused with a message, the dyno and the iso curves need the trim as a stored kind 10 surface: NOT PORTED. Cited tests: scenes cedge_sld_arc, cedge_sld_nurbs, cedge_sld_body, cedge_sld_face, cedge_open_surf, cface_const, cface_dyn (unverified against Mastercam).

### Common machinery (mccore)

- `MaSurface::LoadSurf` (0x1804879d0): kind 10 → `LoadTrimmedSurf` → MaFace (loops, base); others `LoadUntrimmedSurf`: 0 MaNURBSurf, 1 with bicubic orders MaBicubicSurf else MaGenericSurf, 8 MaOffsetSurf, others MaGenericSurf.
- `MaSurface::IsoCurve` (0x1804876f0): untrimmed face → `Flowline(dir, par, curve)` (vtable +0x20: dir 1 = at constant u along v, 2 = at constant v along u); trimmed → `MaFace::IsoCurveTrimPoints` (the loops' polylines `GetLoopPolyLine` cut by the iso line, points deduplicated at 1e-10, `Sort2DPoints` along the free parameter, odd count drops one, `count / 2` pieces) then `MaCurve::TrimMultiple` on the Flowline curve.
- `MaNURBSurf::Flowline` (0x18045b9c0): the curve along v (or u) with the surface's knots in that direction; each control point = the de Boor point of a row of the other direction at the parameter, in homogeneous space (`SubdivisionPoint`); range SetMinMaxParams = the surface's domain. Exact.
- `MaBicubicSurf::Flowline`, `MaSurface::Flowline` (generic surfaces: `BinaryCurveFitParams` at `surf_step_chord_dev`, a cubic spline through the points with tangents, then converted to NURBS): decoded in ma_classes.c, NOT PORTED.
- `MaCurve::Simplify` (0x180408f80): a curve that is not a line or arc in class: `IsLinear` (MaNURBSpline: order 2 with 2 points; else per unique-knot span the end derivatives parallel to 1e-6 and along the chord, cos >= 0.999999) → `ConvertToLine` (start, end); else `IsCircular` (0x180403940: IsPlanar(1e-6); `BinaryPointsAtTol(0, 10 deg, 1, 1500)` (tangent-angle bisection, list of parameters), < 20 points of a non-polyline: 20 uniform points; radial and min-distance checks to the arc through start/mid/end at `surf_step_chord_dev`; a closed curve: both halves) → `ConvertToArc` (closed: start / quarter / middle, delta 2 pi).
- `MaCurve::MakeSplineOrNURBCurve` (0x1804059a0): `cfg_createNurbs` (surface type config 10014 == 2, the default) → `MakeMCEnt` of the NURBS (`MaNURBSpline::MakenbsSplineType` → put_nurbs_in_db); otherwise `ConvertToCubicSpline` (a parametric spline entity; NOT PORTED).
- `MaCurve::IsDegenerate(tol)` (0x180404570): the derivative at the middle parameter <= tol.

### The port

Done (viewer/mcsrfcrv.c; panels / flow in cui_geo.c, cui_surf.c):
- **Curve Constant Parameter** (G_CCONST): the panel, prompt 5968, pick a surface (mask 0x100), then the position (prompt mccoreres 5444, `geopick` / a click), the iso curve(s) Direction U / V / Both, kinds 0 and 10-on-0. Panel changes rebuild; live colour; next pick / OK commit (LV_LIVE | LV_REBUILD | LV_MAINCOL).
Greyed (the commands in `geo_unsupported`, cui_geo.c), 2026-11-11: Silhouette Boundary, Turn Profile (Interfacial, formats/interfacial.md), Raster to Vector (N9-21). Curve Slice, Curve at Intersection, Curve Parting Line and Curve Surface are live (each with its refusals in its section).

Closest point (stage 1): `mcsc_uv_of_point` is the iteration of `MaSurface::PointMinDist` (0x180488080): tangent-plane step and Newton step, the smaller per parameter (their mean while the surface has a pole; Newton only after 20 iterations there), limited to the range (a quarter of it with a pole), the secant correction from the second iteration, 50 iterations, the stops (foot within 1e-9 inch, |d x N| < 1e-6, no movement after 30 iterations); `ExtEvaluate` wraps a periodic parameter; poles and seams as `GetEdgeInfo` (1e-4 inch / 0.0025 mm, cos 5 deg). The earlier 1e-2 error was seen with a 48 x 48 uniform grid and a clamped Newton; its exact cause was not isolated (ent 42 has a pole at u0 and a near-zero Sv). Three parts are ours and stated: the hunting grid is `TessellateUntrimmed` (0x18048d4c0) ported (mode 0 chord, mode 1 tangents and normals, the five iso lines merged) over the range `AdjustRangeForPolesAndSeams` leaves (the seam branch not read); derivatives analytic on NURBS (numeric only for the other kinds, not reached yet); a guard (damped Gauss-Newton from a 129 x 129 grid when the result is more than the edge tolerance away) - on ent 42 18 of 403 samples left the surface with the ported iteration alone, near-zero Sv and the secant step. A guard therefore stays; see the oracle below.

**Knot regeneration, AutoCursor in dyno_surf, the views (review fixes).** All the curve commands load through `LoadSurf(.., true, ..)`, so when `MaNURBSurf`'s constructor regenerates the knots (maximum multiplicity >= degree, not rational or a degree below 3: ma_classes.c:62390-62400, 62438, 62485) the iso curves, the Flowline parameters and the arrow's (u, v) are on the regenerated surface (mcsrfcrv.c `fe_init`, `fe_arrow`, `iso_pieces`). `RegenNonUniform?KnotsFromDerivRatios` uses `GetUniqueKnots` (the distinct knots of the range only: `uniq_dom`) and writes order zeros, the interior knots by multiplicity, order ones. In `dyno_surf` a click on a locked AutoCursor point is event 1 (SetupPointEvent), any other click event 4 (SetupSketchEvent), decomp/getevent/mccore_getevent_msgs.c:668-740; a move on the locked point projects it. Views: `n_views` starts at 18 (built in 1 .. 18, decomp/mastercam_stdviews_init.c:28); a new view takes the next free number above 18; 89 of the 1053 corpus parts with views store numbers out of order or with gaps, and reading goes through `DbProcess::ProcessAssocViews` -> `MastercamPlanes::CheckViewEnt` (callee not read: how Mastercam numbers them on reading is UNSETTLED), 2809 of 2809 view records come before the first arc using them; +0x4a of the record holds 0, 1, 3 or 5 and is NOT DECODED (we write 0, as 2766 of 12797 stored records). All Edges (mccad.dll disassembly): Reselect (code 4) `UndoTransTerminate` at 0x1802c4508, Apply (1) `UndoTransCommit` at 0x1802c45b2, the loop exit (OK, Cancel, Esc) `UndoTransCommit` at 0x1802c4781.

The oracle (tools/peloader --pmd, tools/peloader/mccore_pmd.c, runs the real `MaSurface::PointMinDist` on a MaNURBSurf built from the same numbers; note the DB constructor 0x180455dd0 (:150-230) normalises each knot vector to 0 .. 1 and maps the parameter range with it, so the iso curve's knots and range in a record are normalised, and iso_pieces does the same): 399 interior points per surface, (dist > 1e-4): PARALLELSTEEP ent 43: the DLL 0, ours 0, the same (u, v) within 1e-5 on 399 of 399. Ent 42 (pole at u0, near-zero Sv): the DLL fails on 12 (return 1, the last iterate kept), ours without the guard on 16, both on 7; 343 of 399 the same (u, v). So the DLL itself fails on 12 of these 399 points (a real Mastercam click there gives a wrong (u, v)). The guard fires on ours' 16 failures: 9 are points the DLL gets right, 7 are points the DLL also misses (ours then lands on the surface where the DLL does not). The other 5 DLL failures ours solves without the guard. The guard is kept (ours) and is the single known deviation of the (u, v) search from the DLL on ent 42.

**DEVIATION approved by the user, N9-25 in formats/user_decisions.md** (viewer/mcsrfcrv.c `mcsc_uv_of_point`, the block after the port of the iteration): the numeric Gauss-Newton fallback is ours, not Mastercam's code. Reason: the DLL itself misses 12 of 399 points on ent 42 (tools/peloader --pmd, a pole and a near-zero Sv); the port of the iteration without the fallback misses 16 (7 shared with the DLL); with the fallback picks near poles stay on the surface where Mastercam's would not. `MCSC_NOGUARD=1` switches it off for the comparison with the DLL.

Callees read (stage 3): `MaCurve::IsClosed(-1)` -> `GetClosedInfo` 0x1804022b0 (tol 1e-4 inch / 0.0025 mm, closed when the ends are within tol and the start is farther from the middle point than from the end, tangents within cos 10 deg give the record's bit 3); `MaLoop::GetLoopPolyLine` -> `MakeLoopPolyLine` 0x1804331e0 (the parameter curves' polylines joined; each polyline is the stored uv points: the MaParameterCurve constructor's resampling was not read); `MaPolyLine::PlaneIntersection` 0x180471cd0 (segments with d0 d1 <= 0, a closed polyline's first segment skipped when its start is on the line, the segment after one ending on the line skipped); `IsPlanar`'s constant = n . P0 of the first control point (`dStack_c0`); `put_nurbs_in_db` 0x1802daaf0 (+0xa8: `nb_parm_curvature` at 2 ncp - 2 steps, under 1e9, degree < 2 or below 1e-9: 1e-9; the end points: the end control point's stored values when the end is at the knot, else evaluated); `MaNURBSpline::Break` 0x1804435c0 cuts by knot insertion (`SubdivideSegment`) and keeps the parameters: the same curve as the standard refinement here.

The arc's view (stage 2): `MaArc(p, p, p)` = `MakeArcFrom3Points` 0x1803d1d80: k = unit((p1 - p2) x (p2 - p3)), `find_view_k` (mastercam.dll 0x180337960: views 1.. in order, either sense, | |k . z| - 1 | <= 1e-12 at once, else the best within 2e-7; no match with create 1: `make_view_k` + `AddMCView`), then `cr_a_3p` in that view (start angle, `find_sweep_angle`, which can be negative); `store_the_arc` (mccad 0x17c150) is `store_ent` with the given a_3d. Ported: the search over the reserved and the part's stored views. A plane with no view: `AddMCView` (mastercam.dll 0x18031df40: `mx33_match` within 2e-7 reuses a view, else the next number; `mcam_add_view`) and a DB_ID 0x200 subtype 0 record (0xb8 bytes) written first in the geometry block at every save; a File > New part first takes the operation defaults library's stored views (numbers 9..18) into its list (`mcam_fresh_views`), as Mastercam's n_views has them. The sweep is stored signed as `cr_a_3p` / `find_sweep_angle` give it (`store_the_arc` passes the a_3d through; stored arcs do have negative sweeps, PARALLELSTEEP ent 38 -4.46 deg in view 5), a closed curve's delta 2 pi.

Curve Flowline (G_CFLOW, FUN_180287a40 -> FUN_180287870 -> CreateSurfaceFlowLines 0x18023cc20): panel keys and defaults FUN_180237560; Number (`GenerateFlowlinesByEqualParametericIntervals` 0x1804855e0), Chord height (`GenerateFlowlinesByChordHeight` + `TessellateUntrimmed` mode 0) and Distance (`GenerateFlowlinesByDistance` + `GenUVListByArcLength`, `MaCurve::Length` 16-point Gauss nodes from the DLL's .rdata, `ParamAtArcLength`) are all ported. A trimmed surface's range comes from the stored loop extents (MaFace +0x10..+0x28 taken as those [inferred]).

Other deviations: the (u, v) of the click is `mcsc_uv_of_point` of the point the click returned; `dyno_surf` (arrow, drag) is not ported - the position is a second click. Non-NURBS surface kinds (bicubic and the fitted path), solid faces and cfg_createNurbs off are refused with a message.
Constant Parameter now runs through dyno_surf (mode 3, param_4 1, see "dyno_surf and Curve Dynamic" above): after the surface pick the arrow follows the cursor, a click (or a click on the AutoCursor: the point is projected, code 0xf) builds; Enter / Cancel leave no curve (FUN_1802980f0 `local_2ee7`) and the prompt 5969. The (u, v) start of the arrow is `mcsc_uv_of_point` of the point the click returned (the event's (u, v) is not available here). Non-NURBS surface kinds (bicubic and the fitted path), solid faces and cfg_createNurbs off are refused with a message.

Checks (Curve Constant Parameter; scenes ccurve_cyl, ccurve_nurbs, ccurve_rebuild, ccurve_trimmed, ccurve_refused in tools/tests/wf_scenes.txt; the real DLL was not run under tools/peloader; the stored-curve scan below is the only check against data Mastercam wrote):
- Hand-worked, cylinder r 12, axis through (3, 4), height 25 (Surfaces > Simple): the position at (2.5385, 15.9911, 10) gives the line x = 2.5385, y = 15.9911, z 0..25 (distance from the axis 12.000) and the arc c(3, 4, 10) r 12 sweep 360 - unchanged after Save / reopen.
- The iso curve's control net is the DLL's surface-derived net only where shown above; with the search's stop (|d x N| < 1e-6) the picked parameter itself differs from the given one by up to 1e-5, seen in crviso's 6.0e-6 (ent 42 constant v).
- The curve against the surface, two separate evaluators (`mcs_curve_eval` of the written record, `mcs_surf_eval` of the source, 201 samples over the curve's range, `crviso`), PARALLELSTEEP.mcx-9 rational NURBS surface ent 42: constant u 1.7e-10, constant v 9.3e-10; surface ent 43 constant v 5.2e-12 (units: inch; parameter given to 9 digits).
- Planar NURBS ent 47: both iso curves simplified to lines ending at the domain corners, 2e-15 from the surface. Trimmed ent 46 (kind 10 on NURBS 45): the curves stop at the loop (v 0.0833..0.8868 at u 0.3917; u 0.1704..0.8296 at v 0.5917), 2e-15 from the surface.
- Stored curves against their surfaces (the test hook `crvscan` over 180 corpus parts, every third of the sorted list; 176 loaded): 74 stored NURBS curves in 14 parts lie along a constant u or v of a NURBS or trimmed NURBS surface (within 1e-4 at 11 samples, the largest 8.9e-5). For 34 of them the degree and control count equal the iso curve computed here: 33 agree on the control net within 2.5e-4 (forward or reversed); the other one (surface ent 58, iso v 0, 10 control points) has the same knot count but different knots (normalised difference 3.4e-2, 6 of 12 interior knots in common), so the same geometry within 9.4e-7 at 11 samples on a different parametrisation: a different construction, not a defect of the iso. The 40 with other counts: 9 cover the whole surface range with every interior knot among the iso curve's (section curves with the surface refined from them), 14 cover the range with other knots, 17 cover only part of it (at most trimmed pieces; 0.40 of the range in one case); none of the 40 was compared further. These curves were not necessarily produced by the Curve commands - the surface can equally have been built from them - so this checks the iso extraction against real surface data, not Mastercam's command output. Curve Flowline's Number results were checked by hand only (cylinder: 2 lines, the last value skipped on the periodic u; 3 circles z 0, 12.5, 25).

## Wireframe spline commands (N9-16, 2026-11-09)

Ports: `viewer/core_ui/cui_spline.c` (the commands), `viewer/mcspline_fit.c` (mccore's fitting, records, parametric -> NURBS), `viewer/mcspline_chain.c` (Spline From Curves), `viewer/mcspline_refit.c` (Refit), `viewer/mcspline_simplify.c` (Simplify), `viewer/mcsurf_tonurbs.c` (Convert of surfaces). Sources: decompiles of mccad.dll, mccadutil.dll, mccore.dll, mastercam.dll (functions cited in the code; mccore exports unless noted), the MCCAD.UI / MCCADUtil.UI BAML panels, resource strings (cited by id).

### Common
- `cfg_createNurbs` (decomp/surfrev/surftype_fns.c): config 10014 `surface_type` (factory 2) 2 -> NURBS, 1 -> parametric, 0 -> config 10013 (nurbs_as_backup, factory 1).
- Nodes: CSplineNodeMgr starts at 4000 and grows on demand (FUN_18027c5e0, steps of 1000): no fixed limit.
- Records: `put_spline_in_db` / `put_nurbs_in_db`. **+0xa8 of a parametric spline**: DAT_1812d9064 is 1 in .data; its only setter FUN_1802db420 is called by FUN_180429de0 with 0 around one make_spline_from_pts and back to 1. So the limit is the largest `sp_parm_curvature` (0x180666210; eval_spline's flag 1 multiplies the derivatives by nseg, then by 1 / (t2 - t1)) over t = 0 .. nseg at steps 1 / order, 1.0 when below 1e-9, at most 1 / tol. Check on stored records: 55 of the 74 class 8 records of the sample parts without the exact value 1.0 match within 1e-6 relative; the other 19 are trimmed copies keeping the parent's value or unit-converted parts (ratio 25.4). recalc_spline (Modify) uses step 0.1 / order.
- Fit: `make_spline_coefs_open` / `_closed`, `natural_tan`, `tan_3p_arc`, `pt_tan_to_sp_coefs`, `convert_points_to_nurbs` (knots: 4 zeros, chord lengths, last value 3 more times; tridiagonal FUN_18026b8e0, Thomas FUN_18026b510). Oracle `tools/tests/spline_fit_oracle.sh`.
- proj_pt_on_ent (0x1800184e0, view 0): a parametric spline: every segment by proj_pt_on_coeff from 0.5 (FUN_180018af0), the nearest (stop within tol); others Newton on t from the seed, a NURBS clamped / wrapped per the closed test, damping after 30 iterations. select_ent's returned t (mastercam.dll) is NOT DECODED: the seed for a click on a spline is the nearest point of the drawn polyline, interpolated in t (as spline_chain.md).

### Spline Manual (FUN_180260ef0, base FUN_180227990 / FUN_180227cd0 / FUN_180228600)
Panel (BAML): Position (Back up one point, code 4), Edit End Conditions, the Start / End condition views. Prompts 5868 / 5869. Rubber band type 9 (rb.c l.627): the spline through the nodes and the cursor, natural ends, closed when the cursor is on the first node. Edit End Conditions: combo -> code (FUN_1801d3830: Natural 1, 3 Point 0, To Entity 2, To End 3, Angle 4). Panel codes: 100 / 101 the combos (FUN_1801d3a60: property EndpointCondition, 0x65 - isStart), 0x66 / 0x67 the angle (AddToAutoCompleteList in the handlers), 0x68 / 0x69 the direction (handler tests the condition code 2: flips the tangent). To Entity: select_ent, proj_pt_on_ent(the end node, seed = pick t), tan_on_ent_from_t; To End: t2, or t1 when 2 x pick t < t1 + t2; oriented along the end chord, unit. Angle: (cos, sin, 0) through the active Cplane (FUN_180228600). Live colour during the loop. Esc: a pending To Entity pick only cancels the pick (select_ent); otherwise it leaves the loop keeping the spline and the command goes on (FUN_180227cd0 break, cVar4 = 1). Apply also resets the panel (FUN_1802278b0). OK / Cancel end the command. End-tangent arrows (FUN_180227be0, conditions 0 / 2 / 3, natural_tan at the start): drawn as a tick; GrTemp_AddArrow's size 0.1 and style int are NOT DECODED. Enter in the loop acts as Apply from prompt 5869 only: mask 0x10008008 has no 0x800 and get_event's Enter path there is NOT DECODED. Panels are new objects per command (make_shared, the exits only SaveAutoCompleteList): values do not carry over between runs.

### Spline Automatic (FUN_180266560, get_a_points_process)
Three picks (5871 - 5873). The first two flagged, the last pick not (the walk must reach it). Capacity CountVisiblePoints + 2; at the limit FUN_1806621f0 stops and the 5875 question (Yes: a spline of the nodes so far and on from its last two nodes). In 2D construction points go through proj_a_point (0x180662d00): along the Cplane z to the depth.

### Spline Blended (FUN_1801ee790, FUN_1801ee220, FUN_1801efa00, dyno_curve 0x180320530)
Pick, slide (mouse moves: t += (dmouse . T2d) / |T2d|^2, wrapped on a closed curve else clamped), click: proj_pt_on_ent(the event point, seed = running t). Live colour; panel values rebuild (codes 6 - 8: roll back and create again); Reselect (4 / 5: the other entity kept); next first pick / OK / Apply / Cancel / Esc commit. Trim radio (FUN_1801e8c30): None 0, Both 1, Entity 1 2, Entity 2 3 (default Both). Trim rewrites in place: here a new record taking the id (mod_pair); Reselect or a rebuild rolls the record and the chains back (mod_unpair). One entity picked twice gets both trims on one record (FUN_1801efa00: param_5 = the updated param_4). Reselect enabled only with a spline made (BAML EnableEntity1 / 2). A parametric spline in 2D construction: **DEVIATION approved by the user, N9-26**: projected correctly (mcsf_squash_spline; Mastercam's squash_spline stores stack garbage).

### Spline From Curves (sketcher_spline_3 FUN_1801fc2a0)
A chain command (chain_it 4233). `make_nurbs_from_chains` / `make_spline_from_chain` per kind (mcspline_chain.c, oracle `spline_chain_oracle.sh`); a parametric spline in 2D construction: **DEVIATION approved by the user, N9-26**, as Blended. Conversion enum from mccad.interop.dll IWireframeSplineCurvesManager.PostConversionAction: Keep 0, Blank 1, Delete 2, Move 3. Panel (FUN_1801f58e0): Tolerance 0.02 / 0.001, Conversion 0 Keep / 1 Blank / 2 Delete (blank when used by toolpaths) / else Move to level, Level, Sharp Corner Smoothing (FUN_1801fc020: MaCompositeCurve::Refit of each new spline, Detection angle 10, Blend 2.0 / 0.1).

### Refit Spline (RefitSpline FUN_180134ca0, FUN_1801347c0)
Selected splines (mask 0x88) rebuilt live on each panel change; a new selection resets the fair distance limit to 1e38 (bVar4). OK / Apply: Delete mode removes the originals (blanked if used by toolpaths: FUN_180134020), commit; Cancel / Reselect (5) terminate. Panel (FUN_1801305f0): chord deviation (curve_chord_dev), angle 10, blend 0.1, Allow, Original Spline 0 Delete / 1 Keep (default Keep), Sharp handling 0 Blend / 1 Break. FUN_1801343f0 (toolpath groups) not ported.

### Simplify Spline (FUN_180292940, FUN_1802926a0, convert_ent_to_line_or_arc mccadutil 0x1800539c0)
OK / Apply over the selection: a line or an arc on the main attributes; Delete / Keep / Blank; status 8926 / 8927. Oracle `spline_simp_oracle.sh`. ent +0x28 (> 0 blanks instead of deleting) taken from the toolpath references (sp_used_by_ops for From Curves / Refit; Simplify takes it as 0).

### Spline Convert to NURBS (x_2_nurbs, convert_entity_to_nurbs)
Lines (convert_line_to_nb), arcs, parametric splines (convert_ppspline_to_nbspline + FUN_180059f40: chord knots, C1 / C2 joints with 2e-7; oracle includes it); a trimmed one: t1 / t2 = the end knots, then proj_pt_on_ent on the NURBS of the stored ends. Surfaces (`viewer/mcsurf_tonurbs.c`, convert_to_nurbs / create_nurbsurf_from_surf / make_new_trim): kind 0 and trims on a NURBS base are only redrawn; kinds 1 / 2 / 6 / 7 become kind 0 on the source's colour / level; kinds 8 / 10 on a non-NURBS base get a new blanked NURBS base and the rebuilt trim. Refused: kind 4, a trim on an offset of a non-NURBS base, a kind 10 on a kind 8 / 10 base, a chunked-flush loop. For every converted type: the original deleted, or blanked when used by toolpaths (delete_ent blank = ent +0x28 >= 1; ours: sp_used_by_ops), tp_substitute_ent + DfReAssocNE (mod_pair, a new id). One run per command: x_2_nurbs has one select_ents and no loop. convert_to_nurbs's Group Manager part (TpGrpMgr, TpGrpList, list_grp_append_ents) is NOT PORTED: this app has no entity groups.

### Untrim Spline, Modify Spline
Untrim (UntrimSpline): t1 / t2 full, ends evaluated; a new record taking the id (mod_pair: Mastercam rewrites in place); one run per command. Modify a NURBS (modify_nurbspline_process): control point moved, FUN_180056e10, passReferences (mod_pair, a new id); picks by view distance (FUN_1800554d0 / FUN_18029d650: proj_iso_pt); surfaces (modify_nurbsurf_process) are NOT PORTED and refused with a message; Enter ends the command; a parametric spline (sketcher_spline_5): pick the nearest node (FUN_18029e120), drag (rubber band 0x21, FUN_18033d520: the node keeps its depth in the Cplane), recalc_spline (mccadutil 0x180057080), Tangency Endpoints 1 / 2 keep the end tangents, a new entity with the references (translate_silent / passReferences).

### Tests
Scenes wf spl_*. Oracles: tools/tests/spline_fit_oracle.sh (+ pt_tan_to_sp_coefs, convert_ppspline + FUN_180059f40), spline_chain_oracle.sh, spline_refit_oracle.sh, spline_simp_oracle.sh. A parametric spline in 2D construction (Blended, From Curves): **DEVIATION approved by the user, N9-26** (formats/user_decisions.md): squash_spline reads uninitialised data in the real DLL (disassembly 0x180288d80 - 0x180288dc5), so it is projected correctly here; the chain oracle checks the result lies in the plane. The parametric path through the UI: scenes spl_par_* (config from tools/tests/data/spl_param, MCAMUI_USER).

### Curve Slice (sketcher_curve_5, FUN_18028d0d0, mccad): ported 2026-11-09 for untrimmed NURBS surfaces and curves (viewer/mcsrfint.c, core_ui/cui_slice.c)

Panel and loop [code]: model `WireframeCurveSliceManager`, values read by FUN_180242280 (mccad; model fields +0x50, +0x60, +0x70, +0x80, matched to Spacing, Offset, Join, Find multiple by the order of the call's arguments [inferred; the BAML bindings not read]: `local_548` spacing, `dStack_540` offset, `local_538` byte 0 Join (param_5 of `slice_sel_surfs_with_a_plane`), byte 1 Find multiple (param_6)); the session defaults FUN_1802425c0 / the loop's initial values: Spacing 0, Offset 0, `local_538 = 0x100` = Join off, Find multiple on (only Join and Find multiple have a `SaveRestoreLastParams` value; Spacing and Offset carry the panel's autocomplete list, FUN_18028d060 / FUN_18028cff0 / FUN_18028dc80). The panel fields in the BAML (`wireframecurveslicemanagerview`, MCCAD.UI.Curve): SpacingEvaluatorBox, OffsetEvaluatorBox [labels not read: resource table of the BAML].
Plane: the active Cplane origin (`view_to_world` of 0 + `AddActiveOrigin`) and normal (`view_to_world` of (0, 0, 1), FUN_180009920 unit) on entry; event code 4 (the Plane button) opens `get_plane_info` and replaces both. Prompts mccadres 5970 (0x1752, selecting) and 5971 (0x1753, after a build). get_event mask 399 (0x18f: 0x100 surfaces, 0x8f curves) with the solid-selection bits while `uVar13 & 2`. Codes 0 (OK) and 1 (Apply) with no build yet (`!bVar6`) go straight to the build (`bVar6 = true`; after a code 0 build the loop leaves); with a build present, both commit (`UndoTransCommit`, `sktcher_main_color`, selection cleared, `bVar6 = false`), code 0 then leaves and code 1 returns to selecting; Cancel is code 2 (`local_536 = 1`, the loop ends, the transaction committed at the exit like the other commands). Enter (event 8, key 0xd) is turned into a panel event by FUN_18028bba0 [the code it maps to depends on `*(i*)(+0x150)` and the elided arguments of the call: not established]. Every build: `erase_plane`, the previous undo transaction `UndoTransTerminate`d, `UndoTransStart`, `slice_sel_surfs_with_a_plane(point, normal, spacing, offset, join, multiple, list, 0)` then `slice_sel_curves_with_a_plane(point, normal, spacing, list, 0)`, `sktcher_live_color`, the plane redrawn, prompt 5971; panel changes (codes 5..8) rebuild. msf_nurbs_splines takes `cfg_createNurbs` for the build.

`slice_sel_surfs_with_a_plane` (mccore 0x18056e000): `GetProdEnableBits() & 2` returns at once. `MaSurfPlaneIntersect(NULL, zero plane, chord, chord, 1e-4, 0.5, multiple == 1)` (chord = `surf_step_chord_dev`; metric 0.0025 and 12.0) built once. Per surface: offset surface first when |Offset| > tol (`offset_a_surface`), `MaSurface::LoadSurf`, trimmed base surfaces `SubsetToFace(0.05)` when `this[0x11] == 1` and `+0x70 != 0`, then FUN_18056ac50 (kernel call below); `join`: `MaCurve::JoinCurves(msf_join_tol)` (halved when spacing <= msf_join_tol); every curve `Simplify` (to a line / arc when it can) else `MakeSplineOrNURBCurve` (the NURBS, or the cubic spline when cfg_createNurbs is off). No curves at all: `post_warning(0x1575)`.

FUN_18056ac50 (mccore, the driver per surface piece; the decompile's call shows only two arguments, so param_5 = Spacing is [inferred] from `DAT_1813fbe78 = param_3` and its use as the plane step): `BreakAtSlopeDiscontinuities(2.5)` on non-smooth bases (vtable +0x10 with 2.5: taken as "smooth at 2.5 deg" [inferred from its use, the method not identified]), each smooth piece: the normal normalised, `d = n . P`, SetPartSrf + SetDrivePlane, `ComputeIntersections(false)` (vtable +8) repeated: with Spacing 0 the loop ends after one plane; otherwise it steps plane offsets, first in the +n direction until nothing is found, then -n from the start, and with the hunting-grid `DistToPlane` it jumps directly to the first spacing multiple inside the surface's extent when the first plane misses it.

The intersection (MaSurfSurfIntersect / MaSurfPlaneIntersect, ma_classes.c; the plane path read, the surface-surface variants SurfSurfInitIntersection and MaSurfSurfIntersect::InitCutData not read; a first reading, to be re-checked line by line when ported): `ComputeIntersectionsMain`: `SurfPlaneInitIntersection` seeds on iso lines (first the 2 domain boundaries of each direction, then 15 interior iso lines at 1/16 steps; `(bool)this[0x10]` false = stop after the first iso with seeds; `MaCurve::PlaneIntersect` of the iso curve at 2.5% steps with Newton / secant refinement, tolerances 1e-6 / 1e-9 inch, 2.5e-5 / 2.5e-8 mm), then per seed `MakeIntersection` = `InitTolerances`, InitCutData (plane version: the start point, tangent = unit(n x surface normal), direction by a test step of `+0x50` through `PointMinDist`, return -1 reverses it), `MarchIntersection` (on code 2: chord, chord and max step halved and InitCutData again, at most 3 retries; `ReverseCutDirectionData` for a second run from the seed under conditions on the stop state, [the exact condition: read at MaSurfSurfIntersect::MarchIntersection, not restated]) around `SurfSurfMarchIntersection` (1385 lines): step along the tangent by a curvature-based length sqrt(dTol*radius*...) bounded by +0x228 (min) / +0x230 (max), corrector through `SurfSurfTangentPlane` + the plane equation at each iteration, boundary exit through `SurfSurfFinalIntersection` (iso of the hit boundary against the plane, `BoundaryClosestToUVDirection`, `DuvDtEvaluate`), a stop when the tangent turns back by more than 135 deg (`dVar32 + 0.707 <= 0`) and a loop closure test (the step crossing the plane through the start point with the start tangent, and the distance tests against the first point), step doubling / halving, 10000 points (`this+8`), results in `this+0x58` points, `+0x60` tangents, `+0x78` (u, v). `BuildTrimmedModelSpaceIntersectionCurves` (plane): `MaCubicSpline(n, chord, points, tangents)` + its NURB conversion (`ConvertToC1NURB` / `ConvertToG0G1NURB`), then `CheckUVEndPointsClose` and `BuildTrimmedModelSpacePartCurves` + `GenPartSrfUnionedLoop` on trimmed faces (else the whole curve, clone at vtable +0x128). Also needed for a port and not read in detail: `MaCurveHuntingPoints` (`GetHuntingPoints`), `MaSurfaceIsoCurve::EvaluateCrv`, `UnitNormalEvaluate` (239), `BoundaryClosestToUVDirection` (361), the cubic conversions (ConvertToC1NURB 185, ConvertToG0G1NURB 270), `JoinCurves` (135), `CheckUVEndPointsClose` (113), `BuildTrimmedModelSpacePartCurves` (197), `GenPartSrfUnionedLoop` (248), `BreakAtSlopeDiscontinuities` (96).

Oracle (`tools/peloader --spint`, tools/peloader/mccore_spint.c; KeyPress answered "no key" for WasInterrupted): the DLL's `ComputeIntersections` on a NURBS surface built like `--pmd`. Result on a 6 x 6 bicubic patch and the plane x = 7.3 (chord 0.002 inch): one curve (a NURBS, vtable 0xd5d710), 201 samples with x in 7.2999999999999989 .. 7.300000000078648. Planes y = 7.7, z = 3.3, and n = (1,1,1) through (10,10,3) each returned one curve. So the real kernel runs here and is available to check a port against point by point.

`slice_sel_curves_with_a_plane` (mccore 0x18056db40) -> FUN_18056d090(ent, normal, point, spacing, 0.0009 (0.001 x 0.9, disassembly at 0x18056dec5), store = 1, param_7 = 0, &interrupted, list): per curve (arcs, lines, NURBS, class 8, surface curves class 0x400): `ent_above_below`; with Spacing > tol up to 1000 planes stepped along the normal toward the curve (`local_6128 = 1000`, the sign of the curve's side), each plane: lines `lp_ints`, 0 <= t <= 1 (the end touches both pieces); arcs in chords of `pow(3.84e-10 / (5 r), 0.25)` of the sweep; class 8 and NURBS stepped by `brk_spline_const_err(ent, 0.0009, 1.0 or 25.0 mm, 0.0009, 0, &t)`; surface curves through their stored (u, v) polyline (`fetch_cons_n_and_pts`); each crossing of a chord: a point entity (`store_point`, main attributes) stored at the chord point, not refined to the curve, and the `1e-12` test avoiding the shared node; stops once a plane no longer touches on either side. brk_spline_const_err (mccore 0x180162540, decomp/mccore_spline_brk.c, chord + knot-span limited) was read.

#### The port (viewer/mcsrfint.c: a transliteration by the DLL's offsets; viewer/core_ui/cui_slice.c: the command)
Ported: MaNURBSurf evaluation (FindUKnotSpan: t at an interior knot takes the span on its left), GetEdgeInfo, PointMinDist, EstimateUV + hunting grid (mcsrfcrv's TessellateUntrimmed), MaCurveHuntingPoints (BinaryPointsAtTol mode 1, 10 deg, 5000), MaMinMaxBox (hunting points + interval middles, Expand 1e-4 / 0.0025) and operator&&, PlaneIntersect, SurfPlaneInitIntersection, SurfSurfTangentPlane, SurfSurfMarchIntersection, SurfSurfFinalIntersection (plane side), BoundaryClosestToUVPoint / UVDirection, DuvDtEvaluate, UnitNormalEvaluate, InitCutData, ReverseCutDirectionData, MarchIntersection, WeedOutDuplicateStartPoints, IsSmooth + BreakAtSlopeDiscontinuities (patch boundary flow lines, GetTListAtSlopeDiscontinuities, SetMinMaxParams inset 1e-12), MaCubicSpline::CurveFit (BestFitDerivs, MaCurve::PointMinDist) + GenUList + ConvertToC1NURB; FUN_18056ac50 (Spacing, DistToPlane jump); FUN_18056d090 (curves) with ent_above_below per class (lines, arcs by centre distance, class 8 by samples, NURBS by control points) and brk_spline_const_err; panel labels from wireframecurveslicemanagerview.baml (Plane / _Reselect, _Spacing, _Offset, Settings: _Join curves into entity, _Find multiple solutions).

Greyed or refused with a message: the Plane button (get_plane_info, mccore.dll 0x180a2aea0, 1600 decompiled lines: NOT PORTED; the active Cplane is used), Offset (offset_a_surface NOT PORTED), trimmed surfaces and other kinds (MaLoop::TrimToFace 623, FindAllLoopIntersections, TrimToLoop 301, MaParameterCurve: NOT PORTED; refused for the whole selection with a message), ConvertToG0G1NURB (not reached: the march's tangents give a smooth spline). Arcs in other views are sliced through mcam_arc_pt; the centre for ent_above_below is taken as the mean of the points at 0 and 180 degrees [ours].

Checks against the real DLL (tools/peloader --spint / tools/spint_port / tools/spint_spread.py, tools/spint_cmp.py): 60 kind-0 NURBS surfaces from 23 sample parts x 11 planes (3 per axis, 2 oblique; chord 2e-4 inch / 0.0025 mm) = 660 planes. "Find multiple" off: curve counts equal on all 660 planes, 778 curves, ncp equal; control point differences within 1e-6 on 774 curves, 4 curves within 8.3e-4 (knots 7.8e-6), all four in the part SURFACING_2 (the DLL's last march point differs by 4e-5 in parameter at a boundary end where the plane meets the boundary almost tangentially). "Find multiple" on: equal curve counts on all 660 planes, 871 curves; 821 within 1e-6, 50 differ (up to 14.7 in position, at a surface of that same part where several curves meet at one point: the seeds' branch choice differs); the rest of the spread equal. (Superseded: see "Curve Slice: accuracy pass" below.) Hand: cylinder r 12 at z 10 gives an arc r 12 (centre 3.0, 4.0181: the radius is exact; the fitted curve deviates within the chord tolerance, and the ConvertToArc three points follow it), Spacing 6: arcs at z 4, 10, 16, 22. Curves: line and arc points on the plane; a line with Spacing 4: y 3, 7, 11, 15, 19; spline at x 15: y 2.4884 (Manual spline). Scenes cslice_* in tools/tests/wf_scenes.txt.

Curve at Intersection, Curve Parting Line, Curve Surface: nothing new beyond the table at the top of this section; the panels FUN_1802b8960 and FUN_1802be7b0 are in the dump of the same run (not committed, Ghidra `DumpDecomp` of those FUN_ names); `int_2_surfs`, `parting_line`'s workers (FUN_18054be80, FUN_180565bc0) and `project_chains_onto_surfs` were not read beyond their entry lines.

#### Curve Slice: accuracy pass and the remaining refusals (2026-11-09, this branch)

Kernel additions (viewer/mcsrfint.c), all from the decompile cited at each function: `MaSurface::VerifyNormal` (0x180490ad0; the branch order from the disassembly at 0x180490c1x, the decompiler's text is garbled there): a normal shorter than 1e-6 is re-evaluated, at most 6 times, 1e-6 towards the middle of the range (u unless |Su| is small, v unless |Sv| is), and `MaNURBSurf::EvaluateSrf`'s normal flip (this[0x10], set by the DB constructor from ent +0x6e bit 2). `MaCubicSpline::GetSharpNodes` / `ConvertToG0G1NURB` (ConvertToNURB 0x1803f3dd0: any interior node with a tangent break above 0.1 deg keeps the joint point: three-fold knot; else ConvertToC1NURB). `MaCurve::JoinCurves` with `FindConnectedCurveSets`, `FindLastCrvInChain`, `FindAllCrvsInChain`, `FindNeighbor`, `SharesBothEndPoints`, `Reverse`, `JoinSplines`, `Concatenate`, `RegenNonUniformKnotsFromDerivRatios` (the Bezier legs by knot insertion to full multiplicity, the same points as `ConvertSegmentToBezier`); Join after the last surface, `msf_join_tol` 0.2 mm / 0.01 inch (mccore_systol), halved to Spacing / 2 when 0 < Spacing <= that (slice_sel_surfs_with_a_plane 0x18056e000).

Command (viewer/core_ui/cui_slice.c): Apply / OK with a build present now commit as FUN_18028d0d0 does (before: the first Apply committed); panel changes rebuild (`sl_panel_sync`: UndoTransTerminate as `geo_live_drop`, then the build again; codes 5..8); Join is enabled; `slset:nurbs=0` (tests) and `cfg_createNurbs` off give a parametric spline: `MaNURBSpline::ConvertToCubicSpline` (0x180445850: per unique knot span the Bezier points, P0 = B0, P1 = B3, T0 = 3 (B1 - B0), T1 = 3 (B3 - B2), `pt_tan_to_sp_coefs`) stored with `put_spline_in_db`'s class 8 record; a degree other than 3 or a rational curve would need `MaCurve::FitCubicApproxCurve` (slot 14, 0x180401e20, not read): those stay refused with the message.

Not ported, cited: `CheckSurfaceToSubsetDeltaUVRatios` (0x1804804e0, called by LoadSurf for untrimmed surfaces): it reparameterises only when `DeltaUAllowable / (u1 - u0) >= 1000` (or the v equivalent); across the 272 kind-0 surfaces of the spread the largest ratio is 2.35 (19 subset surfaces, all below 2.35), so the branch is not reached; the spint oracle builds surfaces through the constructor and never reaches it either.

Checks against the real DLL (tools/peloader --spint / tools/spint_port on each case; `SPINT_MULTI`, `SPINT_JOIN=tol` runs `MaCurve::JoinCurves`, exported; chord 2e-4 inch / 0.0025 mm; planes as tools/spint_spread.py, 11 per surface):

| run | surfaces | curves | control points within 1e-6 | control-net differences | notes |
|---|---|---|---|---|---|
| the 2017 Advanced courseware parts, Find multiple off | 272 | 3477 | 3434 | 43 (19 of them a different number of cubic segments) | counts equal on all 2992 planes |
| SURFACING_2, Find multiple on (the panel default) | 16 | 406 | 360 by order; 277 of 277 on planes not nearly coincident with the surface | see below | |
| the courseware parts, Find multiple on (the panel default) | 272 | 3751 by plane; 3733 on planes not nearly coincident with the surface | 3680 (of 3733) | 53 (29 with a different number of curve segments or unmatched as a set) | equal curve counts except on 5 planes: 51 vs 57 pieces on SWEEP1 ent 21 and SWEEP2 ent 81 plane 3, 3 vs 1 on SWEEP1 ent 26, SWEEP2 ent 86, SWEEP3 ent 90 plane 6: not traced |
| SURFACING_2, Find multiple on, Join (tol 0.01) | 16 | 338 | 270 of 270 on the same planes | | |

Where the differences come from (traced by comparing the march point by point, seed per seed, `SPINT_SEED` / `SPINT_SEEDALL`, tools/peloader/mccore_spint.c and viewer/mcsrfint.c under SPINT_TRACE): (1) the plane nearly contains the surface (the surface's control net within 2.7e-4 of the plane: 23 curves off / 129 curves with Find multiple on, SURFACING_2): the seed lists are identical and the march starts identical, but the intersection is numerically ill-conditioned, the DLL's Bezier-patch arithmetic (`ConvertSegmentToBezier` / `EvaluateRationalBezierCurve`, vectorised) and the port's B-spline basis differ in the last digits and the march drifts, up to 14.7 on the garbage end points of such cuts (the DLL's own output there has end points 270 inches outside the surface). (2) A few long curves where the march points agree to 1e-6 but `CurveFit` chooses a different segment count (43 vs 42 cubic segments on SWEEP3 ent 92 plane 8; 19 curves): the fit's split decision compares a point distance with the tolerance, so a point at the threshold falls either way. The curves' geometric difference (DLL sample points against the port's curve) on all mismatching planes is at most 5.5e-4 for a 2e-4 chord tolerance (SURFACING_4 ent 182), that is both fits inside the same tolerance band. (3) With Find multiple on, 5 planes give different curve counts (SWEEP1 ent 21 and SWEEP2 ent 81 plane 3: 51 vs 57 pieces; SWEEP1 ent 26, SWEEP2 ent 86, SWEEP3 ent 90 plane 6: 3 vs 1). Traced on SWEEP1 ent 26 plane 6 by logging every `MaNURBSurf::EvaluateSrf` of the DLL (vtable slot 26 replaced under `SPINT_EVTR`, tools/peloader/mccore_spint.c) against the port's `is_eval` (`SPINT_TRACE`): the first 58 order-2 evaluations of the march agree to 3e-15 in (u, v); the 59th differs by 1.5e-10 and the 60th by 5.7e-6: a Newton step of the point projection amplifies a difference of the last digits by about 1e4 per step (that the iteration is close to singular there is inferred from that growth; the Jacobian was not measured). Everything after that is a different branch of the same code (the DLL closes the loop in 89 points, the port's first run returns 2 and is retried at half the chord: 277 points). No difference in the step-control logic showed in this trace; the other four planes and the 19 segment-count differences of (2) were not traced individually and are taken to be the same cause as (1) [inferred].

**Known difference, left as it is (decided 2026-11-09):** the DLL evaluates through `ConvertSegmentToBezier` / `EvaluateRationalBezierCurve` (vectorised), the port through B-spline basis functions; the results differ in the last digits. Where the problem is ill-conditioned this is amplified: planes lying along the surface (control net within 2.7e-4 of the plane: 23 curves with Find multiple off, 129 with it on, SURFACING_2; differences up to 14.7, and there the DLL's own curves end 270 inches outside the surface) and the singular projection steps of (3). Porting the DLL's arithmetic bit for bit is not worth it.

Still refused with the message: Offset (`offset_a_surface` 0x18026c860 + `MaOffsetSurf::EvaluateSrf` 288 lines; needs a third derivative of the base and `UnitNormalEvaluate` order 2); trimmed surfaces and other kinds (`MaLoop::TrimToFace` 623, `FindAllLoopIntersections` 282, `TrimToLoop` 301, `MaLoop::Trim` 161, the `MaParameterCurve` constructors, `IntersectCrvCrv`, the loop relations); the Plane button (`get_plane_info` is in mccore.dll at 0x180a2aea0, 1600 decompiled lines: the earlier note said mastercam.dll).

#### Curve Slice: trimmed surfaces, what was read and the oracle (2026-11-09; the port itself NOT done, the refusal stays)

The chain for a trimmed surface, read from decomp/curves/ma_classes.c. `slice_sel_surfs_with_a_plane`: `LoadSurf(ent, true, 30.0)` -> `MaFace::MaFace(DB_LIST_ENT, true, 30)` (0x180413ba0); `SubsetToFace(0.05)` (0x1804895e0) when `BaseSurfType != 0`, which includes NURBS (`MaNURBSurf::SurfType` 0x180264170 returns 1): the march's domain is the face's u/v range widened by 5% and clamped to the surface, so the port must march on that domain. `MaSurfPlaneIntersect::ComputeIntersectionsMain` calls vtable +0x20 = `BuildTrimmedModelSpaceIntersectionCurves` (0x180495a90) per march with more than 2 points: the whole NURBS (MaCubicSpline(n, chord, P, T) -> ConvertToNURB) is kept at +0x218; when `surface[0x11] == 1` and `+0x70 != 0`: `CheckUVEndPointsClose`, `BuildTrimmedModelSpacePartCurves`, and per part curve `AddIntersectionCurve` + `GenPartSrfUnionedLoop`. The march runs on the smooth piece (+0xb8), the trim on the whole surface (+0xa8).

`BuildTrimmedModelSpacePartCurves` (0x1804a2bd0): `MaParameterCurve(surface, P, T, UV, 0.0, n, chord)`; loop of the one pcurve; `MaLoop::SnapEndPoints(loop, face outer loop)`; a copy `TrimToFace`d (result 2: nothing kept; non zero otherwise: the whole loop). For each resulting pcurve: the two ends evaluated on the surface, `MaCurve::PointMinDist(hunt = true)` on a copy of the NURBS gives t0 and t1, the closed-curve end rule (t within 1e-6 of the far end, 0x1804a2bd0), then vtable +0x80 `Trim(t0, t1)`. So an output curve is the WHOLE fitted NURBS with a trimmed range: the fit is the untrimmed one.

The pcurve: `GenPCurve` (0x180464da0) with param_4 = 0: tangents through `ExtEvaluate(2)` + `DuvDtEvaluate(.., true, ..)`, `MaCubicSpline(n, uv, tangents)` = `BuildPtVtSpline` (0x1803f1fa0: unit tangents, `BestFitDerivs` per span, `ConvertToAlgebraic`, `GenUList`); `GenUVPolyLine`: the spline's nodes with the spline's u list. No BinarySplineFit on this path. A face's loops are `GenPiecewiseLinearPCurve` (a degree 1 MaNURBSpline, the polyline from its vertices and unique knots).

`MaLoop::TrimToFace` (0x1804369e0): `IsCoincidentWithLoop` (outer) -> the whole loop; one pcurve and `IsPartiallyCoincidentWithLoop` -> copies trimmed to the overlap ranges; else `FindAllLoopIntersections`: none: `UVPointRelative` of the start (0 inside: whole; 2 on, closed or not as cited; else return 2); one crossing with ends on opposite sides returns 2; a closed loop with an odd count `operator--`; `TrimToLoop(.., false, ..)`. Then each result against every inner loop (same tests), the crossings sorted by pcurve then parameter (`Sort3DPoints`, `SortDoubleArray`), `MaLoopTrimData` with the ends kept when `IsUVActive`, `Trim(.., true)`. `TrimToLoop` (0x180437750), `Trim` (0x180435eb0, pairs of (index, t)), `ConcatentateLoops`, `UVPointRelative` (0x180438770: 1e-3 polyline pre-test, 1e-6 on the curve -> 2; else `TestUVPtInPolyLineWrtX` and `WrtY`, disagreement -> outside), `SampleStart/EndDirectionRelativeToLoop` (vtable +0xb8 with a distance whose low 32 bits are 0xd2f1a9fc, the low word of 0.001 = 0x3F50624DD2F1A9FC [inferred from the low word only; the decompile drops the high word and the slot's method was not identified]), `FindAllLoopIntersections` (0x180431a30), `FindAllPolyLineIntersections` (0x18046eeb0: the end tolerances 1e-6 / 1e-9, parallel cos(1e-2 deg)), `IntersectCrvCrv` (0x1804030c0: 50 iterations, 1e-9), `MaPolyLine::PlaneIntersection` (0x180471cd0; in the 2D degenerate-span branch it reads x into both coordinates: keep as read), `PointMinDist` (polyline 0x1804725e0, curve 0x180407490), `SnapEndPoints` (0x180434ad0), `IsCoincidentWithLoop` (0x1804328c0) + `Travel` (0x180435720), `IsPartiallyCoincidentWithLoop` (0x180466a70) + `OverlapRange` (0x180405f40), `IsFullyCoincidentWith` (0x1804662c0), `MaCubicSpline::EvaluateCrv`, `LimitTToRange`, `MaPolyLine::Trim`, `MaLine::LineIntersect` were read.

Not read yet, needed by the port: `MaCurveOnSurface` (not in decomp/curves/ma_classes.c; `DumpDecomp` with the regex `MaCurveOnSurface::|MaCurveHuntingPoints::MaCurveHuntingPoints|MaCurve::BinaryPointsAtTol` on the read-only ghidra_core project gives 2654 lines, of them `EvaluateCrv` 0x18040d580 207, `BinaryPointsAtTol` 0x1803fe780 about 1000, the four `MaCurveHuntingPoints` constructors 96; `BinarySplineFit` and `BinaryUVPointsAtTol` are not on this path; `IsPartiallyCoincidentWithLoop` runs `OverlapRange` on two of them for EVERY trimmed cut, so this cannot be skipped), `AlignStartPointWithCurve` (0x1803fdff0, read only up to its first PointMinDist calls), `GetStartParamOffset` / `TPeriod` (periodic curves in `LimitTToRange`), `MaCubicSpline::MoveStartPoint` / `MoveEndPoint` (0x1803fb480 / 0x1803fb2f0), `MaNURBSpline::MoveStartPoint` / `MoveEndPoint` (0x180452810 / 0x180452780), `MaNURBSpline(MaPolyLine*)`, `MaPolyLine::MoveStartPoint`, `MaPolyLine::IsClosed`, `Sort3DPoints`, `MaLoopIntersectData::Add` / `operator--`, `GenPartSrfUnionedLoop` (247 lines; to confirm it leaves the output curves alone), the Trim path of `MakeSplineOrNURBCurve` / `Simplify` on a trimmed NURBS.

The oracle: `tools/peloader --sptrim` (tools/peloader/mccore_sptrim.c), driven by tools/sptrim_spread.py: the --edg case plus a plane list; the real MaNURBSurf (the regenerated copy when the case says so), MaLoops, `MaFace(surface, outer, inners, n)` (0x180414280: `InitFaceData` sets surface +0x70 / +0x11 through `SetFace`), `CheckFaceToSurfaceDeltaUVRatios`, `SubsetToFace(0.05)`, then the same MaSurfPlaneIntersect as --spint. It prints t0, t1 with the whole NURBS and 201 samples over the kept range. The case carries rev = 0 for now (the surface normal flag; the real value is the trimmed record's +0x6e bit 2: MaFace::MaFace 0x180413ba0).

Runs of the oracle (tools/sptrim_spread.py, the first 100 entities of each part, 11 planes per surface, Find multiple off, chord as the part's surf_step_chord_dev): PARALLELSTEEP 1 surface, 8 curves; FILLHOLES 7 surfaces, 37 curves; CELLPHONE_REMOVEBNDY 15 surfaces, 313 curves. Total 23 trimmed surfaces on a NURBS base, 253 planes (190 with curves), 358 curves, 331 of them with a kept range narrower than the whole NURBS, no DLL failure. The kept ranges are typically about 0.05 .. 0.95 of the NURBS, in line with the march running on the domain widened by 5% (`SubsetToFace(0.05)`) [inferred from the ranges, not measured per surface]. SURFACING_2, SURFACING_4 and SWEEP1 gave no case within their first 100 entities (why was not checked: no trimmed surface on a NURBS base among them, or the part did not open in the test run). There is no port to compare with yet.

Offset (`offset_a_surface`, `MaOffsetSurf::EvaluateSrf`), other surface kinds and the Plane button (`get_plane_info`): see "the port" below.

Curve at Intersection, Curve Parting Line: not started (sizes in the list of the final report). Curve Surface: ported 2026-11-11 (below).

#### Curve Slice: trimmed surfaces, the port (2026-11-09, this branch)

viewer/mcsrfint.c "Curve Slice on a trimmed surface" (MiFace; `mi_face_build`, `mi_face_subset`, `mi_surf_plane_face`, `mi_plane_piece_face`), viewer/core_ui/cui_slice.c `sl_build`, viewer/mcsrfcrv.c `mcsc_face_data`, `mcsc_emit_mi_range`, `mcsc_truncate_mi`. Each function carries the mccore address it was read from. Curve kinds in the port (`ICv`): the cubic spline of a march's pcurve (BuildPtVtSpline), the degree-1 NURBS of a face edge, the whole fitted NURBS (a cut), `MaCurveOnSurface` and `MaParameterCurve`.

What settled the points the first reading left open: `MaLoop::SampleStart/EndDirectionRelativeToLoop` step +0.001 (0x180cf9cb0) and -0.001 (0x180d5d020), read from the DLL's .rdata (the earlier note had inferred only the low word). `MaCurve::ParamAtArcLength` 0x180406910, `MaCurve::Length(a, b, ..)` 0x180405570 (16-point Gauss, the nodes of mcsrfcrv.c iso_length). The MaLoopIntersectData sorting (`SortDoubleArray`) sorts only the parameters. `TrimToLoop`'s shift at LAB_180437a8e is kept as read (ascending). `BinaryPointsAtTol`'s closed-curve retry (3 points, ends within 1e-12: half the angle, at most 3 times) and the piecewise-linear branch (`IsPiecewiseLinearNURBCurve`, `GetUniqueKnots`). `GenPartSrfUnionedLoop` (247) reads the curve only (`GetStartPoint`, `GetEndPoint`); it is not ported.

Output: `MaCurve::Simplify` (0x180408f80) tests the whole NURBS for a line (`MaNURBSpline::IsLinear` 0x1804500a0: unique knots of the whole curve) and the trim range for an arc (`IsCircular`); `MaNURBSpline::MakeMCEnt` (0x180451bd0) and `ConvertToCubicSpline` (0x180445850) `Truncate` a trimmed curve before storing: `mcsc_emit_mi_range`. Join: `JoinSplines` (0x180450940) truncates, and `FindConnectedCurveSets` sees the trimmed ends: `sl_emit_list` truncates a trimmed cut first (`mcsc_truncate_mi`).

Refused: a face under 1/1000 of the surface (`MaFace::CheckFaceToSurfaceDeltaUVRatios` 0x1804148a0: `MaSurface::Truncate` + `MaLoop::UVInverseTransform`; `mi_face_reparam`, the message says so; never reached in the spread). The IsSubseted branch of the same function is not reached for a trimmed record (the second branch of 0x1804148a0: `MaSurface::IsSubseted` is for subset surfaces). Inner-loop validity (`CheckInnerLoopsValid`: `MaLoop::Area`, `WindingNumber`) is ported; `MaFace::CheckOuterLoopValid`'s swap is ported.

Checks against the real kernel (tools/peloader --sptrim, tools/sptrim_port, tools/sptrim_spread.py; chord of the part's surf_step_chord_dev, 11 planes per surface; 2017 sample parts):

| run | surfaces | planes | curves (of them with a trimmed range) | compare within 1e-6 | worst difference |
|---|---|---|---|---|---|
| PARALLELSTEEP, FILLHOLES, CELLPHONE_REMOVEBNDY, Find multiple off | 23 | 253 | 358 (331) | all | knot 8.8e-15, control point 1.9e-14, range 8.4e-13 |
| 75 parts (every 6th of the 2017 sample list), first 120 entities, Find multiple off | 91 | 1001 | 1019 (826) | 1015 of 1019 | 4 curves: control 6.4e-5, knot 1.3e-5, range 5.9e-5 (TUTORIAL_5 ent 21 plane 3,4,5 and CONTOURFINISH1 ent 49 plane 0) |
| the same parts, first 60 entities, Find multiple on (the panel's default) | 56 | 616 | 836 (650) | 729 of 836 | 107 curves from 5 marches (CONTOURFINISH1 ent 49, TUTORIAL_5 ent 21): knots up to 0.26, control 1.5e-2 |

Counts equal on every plane of all three runs. The differing curves are the full-range fits of the march, the same class as "Curve Slice: accuracy pass" (the DLL's Bezier-patch arithmetic vs the port's basis evaluation at an ill-conditioned point); the trim results (the counts, the t ranges to 1.6e-4) agree. That these 5 marches are that same class is inferred; they were not traced point by point. Scenes: cslice_trim_plane, cslice_trim_holes (FILLHOLES ent 33: inner loops), cslice_trim_spacing, cslice_trim_join. wf_compare 0/346, units_test ALL PASS.

#### Curve Slice: Offset: kernel read and checked, the command still refused (2026-11-09, this branch)

`slice_sel_surfs_with_a_plane` (0x18056e000): tol < |Offset|: `offset_a_surface` (0x18026c860, param_1 = 1) writes a temporary kind 8 record, `LoadSurf` makes it a `MaOffsetSurf`, the record is deleted afterwards. viewer/mcsrfint.c has the evaluator: `ISrf.obase` / `odist`, `offset_ev` = `MaOffsetSurf::EvaluateSrf` (0x180463740; the base one order higher through `ExtEvaluate`), `unit_normal2` = `MaSurface::UnitNormalEvaluate` order 2 (0x18048f160), `is_eval` order 3 (`MaNURBSurf::EvaluateSrf` 0x180458ec0: E[21..32] Suuu, Svvv, Suuv, Suvv), `is_smooth` / `slope_list` on the bottom base (`MaOffsetSurf::IsSmooth` 0x180463ea0, `GetU/VListAtSlopeDiscontinuities`), `mi_pieces` passing the range to the base (`SetUMinMaxParams` 0x1804641c0).

Checks (tools/peloader --spint with SPINT_OFFSET = d: the real `MaOffsetSurf(surface, d)` 0x1804633d0): the evaluator against the DLL's vtable slot 26 at 400 points and across a knot line (SPINT_EVPTS, SPINT_EVLINE), orders 0..2: position 7e-15, first derivatives 4.4e-12, second 2.0e-9 (SURFACING_4 ent 178). SURFACING_2 (16 surfaces, 275 curves, d = 0.05): all equal to 2.9e-10. Six courseware parts (SURFACING_2, SURFACING_4, SWEEP1..3, HAIRDRYER; 47 surfaces, 517 planes): d = 0.05 and 106 of 679 curves differ (up to 0.025 in position on a curve, 10 planes with a different count), d = -0.1: 80 of 683; without an offset 40 of 769 on the same parts (the "Known difference" class).

Traced on SURFACING_4 ent 178 plane 0: the first differing evaluation of the march is a hunting start point: the DLL's hunting-grid line at v = 0.4444 (4/9, the 10-point uniform fallback of `MaSurfHuntingGrid::GenGrid` 0x180495000 when `TessellateUntrimmed` fails), the port's at 0.4375 (7/16, a successful subdivision). So on this offset surface the DLL's `TessellateUntrimmed` (0x18048d4c0, 972 lines, `MaSurfaceIsoCurve` based) gives up and the port's `tess` does not; why was not settled [that it fails is inferred from the 4/9 grid value]. Until that is read the Offset option stays refused with the message; the evaluator is unused by the command.

Not read: the kind 10 path of `offset_a_surface` (the trimmed base), the record's flags, other base kinds. The ExtEvaluate subset branch (`ExtensionEvaluate` 0x1804838c0, read) applies only when `IsSubseted` and not `this[0x1b]` (`IsSmooth(2.5)` from `GetEdgeInfo`): ported surfaces are smooth pieces or unsubset, so it was not needed.

`--spint`'s degree print (mccore_spint.c read +0x128 instead of the order at +0x134) is fixed; it never influenced a comparison (spint_spread.py and spint_cmp.py compare knots and points only).
### Curve at Intersection (sketcher_curve_8, FUN_1802b8960, mccad): ported 2026-11-09 for untrimmed NURBS surfaces (viewer/mcsrfint.c, core_ui/cui_srfint.c)

**The core function is `intersect_multiple_surfs`** (mccore 0x180562730, called from FUN_1802b8960), not `int_2_surfs`: `int_2_surfs` (0x1805622a0, the old kernel, FUN_18053e230 / FUN_180544020 / FUN_1805636d0) is called from `project_chains_onto_surfs` (Curve Surface). `intersect_multiple_surfs` builds one `MaSurfSurfIntersect(NULL, NULL, chord, chord, 1e-4, 0.5, msf_find_multi)` (metric 0.0025, 12.0; the constants are the hex literals of its body), then per outer surface / inner surface `SetPartSrf` / `SetDriveSrf`, `ComputeIntersections` (vtable +8), the curves collected, `JoinCurves(msf_join_tol)` when `msf_join_em`, then each curve `Simplify` else `MakeSplineOrNURBCurve`; none at all: `post_warning(0x1760)` (mccoreres text "No Intersections found", read from mccoreres.dll's string table).

Panel (wireframecurveintersectionmanagerview.baml, the order of the elements): expanders Surface Set 1 (_Reselect, code 7), Surface Set 2 (Reselec_t, code 9), _Chord Height Tolerance, Offset (Set _1:, Set _2:), Settings (_Join curves into entity, _Find multiple solutions). Values FUN_18024cb60 (model +0x70 chord, +0x80 / +0x90 offsets, +0xa0 join, +0xb0 multiple; matched to the fields by the call order, as Curve Slice [inferred: the BAML bindings' field offsets not read]); initial values FUN_1802b8960: chord `surf_step_chord_dev`, offsets 0, `local_490 = 0x100` Join off, Find multiple on. FUN_1802b9990 puts them in the globals for the build (`msf_nurbs_splines = cfg_createNurbs`, `surf_step_chord_dev` = the panel chord, `msf_join_em`, `msf_find_multi`), restored after.
Loop FUN_1802b8960: select_ents set 1 (FUN_1802b94e0, prompt mccadres 5974 "Select first set of surfaces"; nothing selected: the command ends, local_538 = 0), set 2 (FUN_1802b9730, 5975), then the first pass builds at once (local_524 = 3, bVar7). cVar5 (one set) = (count 1 >= 2 and count 2 == 0) or (count 1 == 0 and count 2 >= 2): `intersect_multiple_surfs(cVar5, ...)`; with cVar5 the inner loop is the same set from the entity after the outer one (local_a618 = local_a640), and the outer loop's get_ent mask is 0x14 (bit 4 with bit 0x10): with count 1 == 0 it finds nothing and the warning is posted [the AND meaning of the mask is inferred from the set_clr_sel_bits calls of FUN_1802b94e0 / FUN_1802b9730]. Prompts after: 5986 "Set options, press <ENTER>, or OK", after Apply 5987 "Select new sets or OK". Panel codes: 0 OK and 2 Cancel leave (the live result is committed at the exit in both, as in Curve Slice), 1 Apply commits and clears the sets, 7 / 9 reselect a set (Enter ends it; the rebuild waits for the next Enter or panel change: bVar7 is only set), 4 5 6 8 10 (the panel values) rebuild. Enter while the panel is up: bVar7 true: rebuild (get_event's return code path; the exact code get_event returns for Enter is not established, but no other path builds there). Ours: a set = the current selection and Enter, as Curve All Edges.

The kernel (all of it in mcsrfint.c, by the DLL's offsets, shared with the Curve Slice march): `MaSurfSurfIntersect::ComputeIntersections` 0x1804a37a0 (the smooth pieces of both surfaces, `IsSmooth` / `BreakAtSlopeDiscontinuities` at 2.5 deg), `ComputeIntersectionsMain` 0x1804a3ad0 (IsUVNearSeam clears the boundary flag of a seed; a seed with status 2 is marked -1, -1), `SurfSurfInitIntersection` 0x18048a460 (the box test, then passes 0 boundaries / 1 the 15 interior iso lines at 1/16: iso curves of the drive surface against the part surface, then the part surface's against the drive surface, u then v; IsHighSeam skipped; beyond-range tests; `multi` false stops after the first iso with seeds), `AllCurveIntersections` 0x18047d950 (46 equal pieces, Subdivide; the signed distance to the tangent plane at the closest point changing sign or <= 1e-12; `CurveIntersect` from the middle of the piece; duplicates within 1e-6 / 2.5e-5 dropped; `AdjustUVToRange`), `MaSurface::CurveIntersect` 0x180481730 (Newton on t, u, v; the steps clamped to the allowable ranges; the secant scale; stop at |d|^2 <= 1e-18 inch / 2.5e-17 metric), `Invert3X3Matrix` 0x1808ec1d0, `InitCutData` 0x1804a5290, `ReverseCutDirectionData` 0x1804a5e20 (+ the drive side), the drive-surface branches of `SurfSurfMarchIntersection` and `SurfSurfFinalIntersection` (side 1 / the other surface: CurveIntersect of the iso line), `MarchIntersection`'s drive-surface pole cleanup, `WeedOutDuplicateStartPoints` (both seed lists), curve building as Slice (`MaCubicSpline(n, chord, P, T)`, ToNURB). `MaCurve::TMid` is not in the dumps: the mean of the piece range is [inferred]. `MaMinMaxBox::operator&&` 0x180439040 (box against box).

Refused with a message: trimmed or non-NURBS surfaces ("MaLoop::TrimToFace NOT PORTED", the other Curve Slice agent's work: BuildTrimmedModelSpacePartCurves, DriveCurves, UnionModelSpaceIntersectionCurves), Offset ("offset_a_surface NOT PORTED"), solid faces (the temp-file sets of FUN_1802b94e0 / FUN_1802b9730: `sld_*`). Not done: the live colour of the result (sktcher_live_color; the result is on the main colour immediately, as Curve Slice), the undo-exit codes 9 and 0x2d.

Oracle: `tools/peloader --ssint` (tools/peloader/mccore_ssint.c: the real `MaSurfSurfIntersect` on two NURBS surfaces given by numbers; `SSINT_INIT` dumps the seeds, `SSINT_SEEDS` runs each seed through `MakeIntersection`, `SSINT_CI` runs `CurveIntersect`), `tools/ssint_port.c` (mi_surf_surf on the same case), `tools/ssint_spread.py` (the pairs of kind-0 surfaces of the parts whose control-point boxes overlap, at most 40 per part), `tools/ssint_geom.py` (the two sets of curves as point sets), `tools/ssint_stored.py` (stored NURBS against the results). Chord 2e-4 inch / 0.0025 mm, 16 parts of the 2017 Advanced geometry folder (SURFACING_1..9, 11, 12, HAIRDRYER, NETSURF1, NETSURF3, RUL2, FILLET_BLEND):

| run | pairs | no curves either side | curve count differs | curves compared by order | control nets within 1e-6 | as point sets, pairs with curves |
|---|---|---|---|---|---|---|
| Find multiple on (the panel default) | 189 | 52 | 30 | 389 | 243 | 137: 92 within 1e-5, 6 within 1e-4, 5 within 1e-3, 5 within 1e-2, 21 above, 8 with curves on one side only |
| Find multiple off | 189 | 59 | 25 | 204 | 164 | 130: 94, 6, 5, 2, 14 above, 9 on one side only |

Traced on the worst cases (seed by seed, `SSINT_SEEDS`, `SSINT_CI`): the seed lists agree (92 seeds identical to 1e-15 on SURFACING_3 ent 30 / 31, 57 seeds to 2e-11 on HAIRDRYER 61 / 62) or differ by a few seeds (HAIRDRYER 62 / 71: 67 against 66, HAIRDRYER 65 / 73: 26 against 25, SURFACING_1 25 / 26: 84 against 86); on HAIRDRYER 63 / 73 the seed-by-seed run shows the port's list shifted by one from seed 11 on. The differences start at (a) `CurveIntersect` from the same seed giving different stopping points on nearly tangent contacts (FILLET_BLEND 25 / 37: the iso line along a shared boundary; port 11 Newton steps and t 0.99999991, the DLL 7 steps and t 0.99999958, both inside the distance tolerance; one seed fewer or more shifts the later list), (b) seeds running into a degenerate point (SURFACING_3 30 / 31: most of the 92 seeds end at (-6.6987, 0, -0.5861) with 1 to 4 points and status 0 or 2 either way), and (c) a difference of 2e-6 at the first step after the start point on SURFACING_12 51 / 55 seed 2 (53 against 26 points), not traced further. The traced cases are neighbours sharing an edge or running into a degenerate point; that this is what the other mismatching pairs are is [inferred], they were not counted. **The 34 pairs above 1e-3 are not all explained**: only the pairs named here were traced. The stored curves: of the 47 stored NURBS curves in 8 of those parts none has a control net equal to a result (2 start at a result's first point): no intersection curve could be identified among them.

Scenes cint_* (tools/tests/wf_scenes.txt): pair, one set (all pairs), two sets, none found, empty set, trimmed refused, offset refused, join, panel rebuild, undo, cancel, reselect, Apply and again.

### Curve Parting Line (sketcher_curve_7, FUN_1802be7b0, mccad): ported 2026-11-11 for untrimmed NURBS surfaces (viewer/mcpartline.c, mcpartline_mesh.c, core_ui/cui_partline.c)

Dumps: decomp/partline/ (core_pl.c, core_post.c, core_tess.c, mesh_*.c: mccore; cad_*.c: mccad; *_asm.txt: disassembly, needed throughout: Ghidra drops most call arguments of this old "msf" engine).

**Panel** (wireframecurvepartinglinemanagerview.baml) [code]: expander Curve Quality: radio "Chord heigh_t:" + EvaluatorBox ChordHeight, radio "_Distance:" + EvaluatorBox Distance (each box enabled with its radio: the manager's SetEnabled calls), expander "_Angle": EvaluatorBox Angle; buttons OK, OK + New (Apply), Cancel, Help. Tooltips (resx): Chord Height "maximum distance that a curve can be separated ... from the surface", Distance "fixed distance between defining points", Angle "between the normal direction of the surface ... which represents zero degrees, and the XY plane of the current construction plane". Manager `WireframeCurvePartingLineManager` (ctor FUN_180246410): keys `WireframeCurvePartingLine-ChordHeight` (0.001 in / 0.02 mm, minimum tol), `-Distance` (0.1 / 2.0, minimum tol, disabled), `-Angle` (0, limits -90 .. 90) - the keys are the autocomplete histories (section "Panel key storage"). Values read by FUN_1802473f0: method (vt +0x38 == 1 -> 1 Distance, else 0 Chord height), chord (model +0x50), distance (+0x60), angle (+0x70). **Session values** `WireframeCurvePartingLineApp::SaveRestoreLastParams`: FUN_1802476c0 at the start (enable_dialog_modality = 1: the statics DAT_1806a53d0 method, 53d8 chord, 53e0 distance, 53e8 angle; RegisterVariable'd chord 0.02 / 0.001 and distance 2.0 / 0.1; the statics start, by the CRT initializer 0x180003950, as method 0, chord = surf_step_chord_dev, distance 0.1, angle 0), FUN_180247770 at the end saves them (unless manager vt +0x18; ours: always [inferred]).

**Loop** FUN_1802be7b0 [code]: prompt mccadres 5972 "Set Cplane, select surfaces, press Apply when done", get_event mask 0x1000823a (selection; bit 2 also solid faces), entity mask 0x100. Selection events (2, 0x10, 0x20) only enable the panel (vt +0x30). Panel codes: 0 OK and 2 Cancel end the loop (an active transaction is committed after the loop in both: OK without a prior Apply makes nothing); 1 Apply: the first time bVar5 = 1 and the build, the next time UndoTransCommit, bVar5 = 0, the selection cleared, prompt 5972, selection again; 4 .. 7 (the panel values) and get_event's nonzero return (<Enter>) rebuild when bVar5. A build: msf_nurbs_splines = cfg_createNurbs; Chord height: surf_step_chord_dev = the panel value, surf_use_chord_dev = 1; Distance: surf_step_dist = the panel value, surf_use_chord_dev = 0; surf_use_const_num = 0; a previous build is UndoTransTerminate'd; UndoTransStart; parting_line(Angle, list); sktcher_live_color; prompt 5973 "Set options, press <ENTER>, or OK"; the selection mask off (0x10008008). The pushed globals are restored after the build. The Distance value is not read by parting_line or any callee in the dumps [code: surf_step_dist does not occur in them]: Distance mode changes only the curve building (below); the mesh and the chains then use the system surf_step_chord_dev.

**parting_line** (mccore 0x180566740) [code]: V = the third row of the view GetActiveViewNo(Planes, 0) (the Cplane: prompt 5972, tooltip), angle in radians (x pi / 180, [0x180d018a8]); msf_trim_em = msf_make_cons = msf_join_em = 0; DAT_1812f396c = -1; init_poly_space(0). For every selected surface (get_ent mask 4 / 0x100, database order) and then every selected solid face (read_next_solid_surf_from_temp_file): prompt mccoreres 5449 "Generating Parting Line on Surface... " n 5639 " out of " total (5450 for a solid face); FUN_18054be80(1, ent, surf_step_chord_dev * 0.5, ..) = make_surf_polygons(1, 1, ent, step, 0, 0, ..) and the triangles' boxes; with triangles FUN_180565bc0 (the nodes and the curves of this surface into the shared sp_structure list; "found" when it listed a curve). After all: none found: post_warning mccoreres 5451 "No Parting Lines found in this view"; Esc during the run: FUN_180542010 (message box 5354 "Change color of entity(s) being processed when <Esc> pressed?", NOT PORTED: no Esc during a build here); else FUN_180538d30 (below) and the listed curves into the command's list.

- **make_surf_polygons** (0x1806fbda0 -> FUN_18058de20 -> FUN_18057a980; viewer/mcpartline_mesh.c, the full description in its header comments) [code]: per direction the spans (get_evaluation_grid: (ncp - 1) / degree equal parameter spans, the knot spans only when non-uniform: (min + max) 0.005 + 1e-12 < max - min; at least 2); per span the length / step ratio from FUN_180581f00 (an offset surface of distance 0 built on the stack, the inset corners evaluated to order 2, FUN_180585d10's curvature step 2 sin(acos(1 - k step)) / k (normal curvature surf_curv_dir or |S' x S''| / |S'|^3; FUN_180589880's march when the chord and the length disagree), the diagonal FUN_180585fd0, the flatness check FUN_18059a450 (the middle sub-cell within step of its two triangles)), the merge pass of single-segment spans, n = ceil(L / S) per span, FUN_18058e2a0, the parameters by FUN_18057e440 / FUN_180599580 (values within 2e-10 dropped), the vertices by eval_surf_const (its own Cox-de Boor FUN_1805b06f0 with basis values below 1e-8 zeroed), u outer / v inner, two triangles per cell (b, b + nv + 1, b + nv + 2), (b, b + nv + 2, b + 1). The arena retry loop of make_surf_polygons (FUN_1806fb740) only grows memory: not ported.
- **FUN_180566220** (nodes) [code]: every triangle's edges 0-1, 1-2, 2-0: g = N . V at both ends (eval_surf order 1, calc_surf_unit_norm, negated with surf +6 bit 4; FUN_180564740: with |angle| > 1e-4, g = cos(acos(N . V) - angle)); FUN_1805647a0: an end with |g| < 1e-12 (the other not) is the node, else on a sign change regula falsi in (u, v) from the g > 0 end (at most 100 steps, stop |g| < 1e-12); a node within tol of an earlier one is dropped (FUN_180554e40); at 4000 nodes (CSplineNodeMgr::get_max_n_spline_pts) it returns at once. The first vertex's normal is computed with the previous triangle's last v (1805663ae .. 1805663dc: a stale stack value; ours 0 before the first triangle [inferred]; only a degenerate normal reads it).
- **FUN_180564b70** (the boundary, untrimmed branch) [code]: edges (u0 v0 -> u1 v0), (-> u1 v1), (-> u0 v1, skipped on a v-closed surface), (-> u0 v0, skipped on a u-closed one), each in FUN_180565990 steps (min of the iso count FUN_180541730 / FUN_1805419b0 - 1 and the end distance / (metric ? 1.0 : 0.04) + 1; doubled through the middle of a closed edge); a crossing not within tol of a boundary node replaces the nearest node (FUN_180535430) and becomes a boundary node; on edge 0 of a v-closed surface (edge 1 of a u-closed one) a copy at v1 (u0) is added; at most 100.
- **FUN_180551e00** (chains) [code; parting_line passes no second surface]: starts the boundary nodes first, then every node; a chain grows by the nearest unused node beyond tol (past chord / 2 x 50 the two nodes' middle must evaluate within chord / 2 x 10 of the surface), a candidate turning back (cos < 0) refused, a forward one (cos >= best - 0.1) preferred within 3 x the nearest distance, a second node nearer the start than half the distance to the best makes the chain restart there, a chain over 3 nodes closes on its start, an interior start is then grown the other way (from the node now at the start's old index: 1805530a7, as the code does). FUN_180534ce0 moves a chain end on a closed seam to the side its neighbour is nearer.
- **FUN_180565bc0 -> msf_create_spline** (0x18054db20) per chain of 2 nodes or more (main colour / level: DAT_1812f3978 = 1, never written) [code]: 2 nodes: a line when tol long (its ok flag stays 0: stored but not listed); Chord height: FUN_18054d160 drops nodes within tol of the previous one in place but the count used after stays the old one (18054decc: R13); closed when the last node is within tol of the first (make_spline_coefs_closed) else make_spline_coefs_open with natural ends; remove_nodes(chord x 0.9) (a node goes when the Hermite cubic of its run passes within that of the removed nodes and the neighbouring segments' middles); one segment left with its 0 / 0.5 / 1 points colinear (check_colinear: 0.99985, 2 tol): the line; else put_spline_in_db. Distance: make_spline_from_pts (natural ends), no node removal.
- **FUN_180538d30 -> FUN_18054d250** (Chord height only, each listed curve) [code]: GetProdEnableBits & 0xf002 taken clear (Mill [inferred], as align_ops.md's & 2); a spline (class 8): convert_ent_to_line_or_arc(surf_step_chord_dev) (mccadutil, mcspline_simplify.c) -> the line or the arc; else with msf_nurbs_splines convert_entity_to_nurbs (mcsf_spline_to_nurbs) + put_nurbs_in_db; the spline killed (FUN_180551960: sel bit 1 cleared).

**The DLL oracle** (`tools/peloader --partline CASE`, tools/peloader/mccore_pl.c): the real FUN_18054be80 (mesh), FUN_180566220 (nodes, boundary, chains, seams) and msf_create_spline's steps on the DLL's exports (FUN_18054d160, make_spline_coefs_*, remove_nodes, eval_curve, check_colinear) for one stored surface record; no database: get_ent_from_eptr is patched to serve the record (FUN_180581f00's offset surface evaluates its base through it), _MCAssert is patched to print. Cases: `tools/partline_cases.sh` (the app's lnoracle hook: every kind 0 record, the bases of trimmed ones included), `tools/partline_dll.sh`; port side `tools/partline_port.sh` + `tools/partline_cmp.sh` (`PL_DLLMESH=1`: the DLL's mesh into the port's later stages). The conversion step (mccadutil) is not in the oracle: its ports were checked on their own (Simplify Spline, Convert to NURBS).

Results (205 surface records of 20 parts: PARALLELSTEEP, SURFACING_1 .. 5, 7 .. 9, 11, 12, HAIRDRYER, FILLET_BLEND, TEA POT, RUL2, 2 SURFACE BLEND, SWEEP1, FENCESURFACE; chord 0.001 inch, the mesh step 0.0005):
- the mesh: all 205 bit-identical (triangles, vertex order, xyz, uv); and 22 synthetic records (non-uniform knots, sub-domains, a rational cylinder, flat multi-span planes).
- with the bit-exact evaluators (below), Top view angle 0: 205 / 205 (nodes bit-identical, chains equal, 211 curves: the fitted coefficients within 3.2e-11); view (0.6, 0, 0.8) angle 15: 205 / 205 (141 curves, 2.1e-12); view (0, 0.7071, 0.7071) angle -20 with chord 0.0005: 205 / 205 (227 curves, 6.4e-12).
- eval_surf and calc_surf_unit_norm for kind 0 (plm_eval / plm_unit_norm in mcpartline_mesh.c: FUN_1805b08d0 / FUN_1805aeb50 / nurb_interval for order 0, FUN_1805b1f10 -> FUN_1805b0fb0, FUN_1805aeff0, FUN_1804b7f40 for orders 1 .. 3, calc_surf_norm 0x1801637e0 with its 50-step pole walk): bit-identical to the DLL on 169,586 points of 182 records (orders 0 .. 3 and the normal; `tools/peloader --lnorm` commands "e" / "n" printing %a) and on 6 degenerate records. viewer/mcsurf_nrm.c's mcsn_eval / mcsn_unit_norm (agreeing to ~1e-12) were not enough: N . V = 1 + 2^-52 makes acos NaN, and FUN_1805647a0 (disassembly 18056482d ..: COMISD / JBE) treats a NaN g as negative and not near 0, unlike a C reading of its decompile; with mcsn_* 6 of 410 tilted cases differed by a node.

Not ported, refused with a message: trimmed surfaces (make_surf_polygons' trim loops, FUN_180564b70's loop branch, the in_polygon tests of FUN_180551e00 / FUN_180565bc0), surface kinds other than 0 (make_surf_polygons on them; FUN_180541730 / FUN_1805419b0's other cases), solid faces (sld_face_to_temp_file / read_next_solid_surf_from_temp_file). Not done: the live colour of the result (sktcher_live_color; the result is on the main colour at once, as the other curve commands), the Esc colour prompt, the undo-exit codes 9 / 0x2d. The per-surface prompts 5449 / 5450 are not shown (the build does not yield to the UI).

Scenes cpart_* (tools/tests/wf_scenes.txt): all 16 surfaces of SURFACING_2, Angle 15, Distance mode (parametric splines), none found, OK without Apply (nothing), panel rebuilds, Apply twice and again, Cancel (kept), undo, <Enter>, trimmed refused, the session values, a rotated Cplane (its 9 entities are the 9 curves the oracle builds for the 16 records in view (0, -1, 0)), HAIRDRYER.
### Curve Surface (sketcher_curve_6, mccad 0x180007a50 -> mccore project_chains_onto_surfs 0x180569380): ported 2026-11-11 (viewer/mccsurf.c, core_ui/cui_csurf.c; O2)

Dumps: decomp/mccad_sketch.c l.3-120 (the command), decomp/curves/core1.c (project_chains_onto_surfs and depth 1), decomp/curvesurf/cs1.c .. cs6.c
(the msf kernel, the storing, the joins), asm1.txt (the stack arguments Ghidra dropped: DisasmAt of the kernel), mc_drawcons.c (mastercam.dll
FUN_18021c2c0 / FUN_180237990, the drawing), mc_selbits.c (FUN_180211740, set_clr_sel_bits), symw.txt / mem1.txt (the globals' writers and static
values). [code] unless marked.

**The entity: a surface curve (CONS_ID, record class 0x400).** Record 0xf0 bytes (0x68 header + cons_type 0x88, bytes_per_ent: no tail):
`+0x68 / +0x80` the 3D start / end (the uv curve's ends on the surface), `+0x98 / +0xa0` t1 / t2 (the uv curve's range: 0, n - 1), `+0xa8` the
largest |C' x C''| / |C'| sampled from t1 in steps of sqrt(t2 - t1) / pi (put_cons_in_db with DAT_1812d9064, static 1), `+0xb0 .. +0xc8` the uv
ends (u0, v0, u1, v1), `+0xd0` the surface's eptr (put_cons_in_db replaces a trimmed surface by its base: the CONS always names an untrimmed
surface), `+0xd8` the parameter curve's eptr, `+0xe0` 0, `+0xe8` closed (3D ends within tol), `+0xe9` 1 (FUN_18054d4f0 local_28ef; FUN_180237990
draws nothing when it is 0). The parameter curve: a 2D NURBS (class 0x80, 2 values per point, degree 1, flags 0x14, knots 0, 0, 1, .., n - 1,
n - 1, `+0x98 / +0xa0` 0 / n - 1, `+0xa8` 1e-9, `+0xc0` 1.0: array_2d_to_nurbs 0x18023d330 + put_nurbs_in_db, checked byte for byte against the
DLL's stored record, `CSURF_RAW`), stored first, ent +0x28 refs 1 (inc_refs_eptr); the surface's ent +0x28 gets 1 more per surface curve
(put_cons_in_db: put_special_undo + rewrite_ent). Corpus: 291 CONS records in 1364 parts, every 2D NURBS (291) is one's parameter curve, all of
degree 1; the uv curves have selection 0x1 (not 0x81: [data] they are never drawn as curves). Drawing (FUN_180237990): an order-2 uv curve
segment by segment, each in (int)(3D chord / surf_type +0x70 x 20) equal uv steps (at least 1); higher orders take curvature steps (NOT PORTED:
no corpus uv curve has them). eval_curve of a CONS (FUN_1803ec480): the uv curve at t, then the surface, the chain rule (the second derivative
adds u'' (Su + Sv), as the DLL).

Here (viewer/mcam_load.c): E_SCRV entities (record kept as a CurveRec class 0x400), the 2D NURBS kept without an Ent, the polyline by
`mcam_cons_poly` (mccsurf.c `cs_cons_polyline`, NURBS and trimmed-on-NURBS bases only: others are not drawn); picked / selected / deleted /
undone / saved like the wireframe; the save writes a new one's uv curve (selection 0x01, refs 1) then its CONS, remaps +0xd0 to a surface made
here, keeps the surfaces' +0x28 = as read + made here - deleted here (`cons_ref_delta`; the decrement on a delete is [inferred]: Mastercam's
delete of a surface curve was not decoded), and drops a deleted part CONS with its uv curve. Not ported for surface curves: chaining,
Transform, AutoCursor snaps, Analyze.

**The command.** select_ents(mccadres 5967 "Select curves to convert to surface curves", mask 0x8e: lines, arcs, parametric splines, NURBS
curves); set_clr_sel_bits(4, 0, 4, 1, 0x100): bit 4 on the surfaces FUN_180211740 finds walking the display list (not UnBlanking): displayed
surfaces only - blanked ones (a trimmed surface's base) and hidden levels are not candidates; UndoTransStart; per selected curve one chain
(RecordBoundaries_Add_Chain + add_curve_to_chain(ent, 0, 1, 0, ..)) and project_chains_onto_surfs(chain, 0, surf_step_dist, 1,
surf_step_chord_dev, surf_step_chord_dev, 0, 0, 1, 0, &n, sp); n >= 1: the curve deleted (delete_ent(eptr, 1, 0, 0, .., 0)), else its selection
bit cleared and redrawn; UndoTransCommit; the surfaces' bit 4 cleared; the command returns (no loop, no panel). Colour / level of the made
curves: main (FUN_18054d4f0: DAT_1812f3978 static 1). Ours: the current selection at Enter (as Convert to NURBS); entities outside the mask left
alone.

**project_chains_onto_surfs with these arguments.** Globals for the run: surf_step_chord_dev = the chord (config 1007), msf_join_tol = the chord,
_DAT_1812f39c8 (the projection distance) = surf_step_dist = 0.1 (static; its only writer is this function's save / restore, so 0.1 in inch and
mm), msf_find_multi 1 (0 inside FUN_180568ca0), surf_use_chord_dev 1. Per chain entity: the curve grid (FUN_180533170: arcs |sweep| / 15 deg + 2,
splines order x segments + 1, NURBS (degree + 1)(knots - 1) + 1, lines 2; more points when the length per step exceeds curve_min_step x 100 -
curve_min_step is a Mastercam.dll data import of mccore, config 1004; the length is length_ent -> CurveLength -> the MaCurve's vtable +0xc0
MaCurve::Length(void) -> +0xc8 Length(t1, t2, ..): for a NURBS MaNURBSpline::Length 0x180450e70, the 16-point Gauss rule (.data 0x1812f0880 /
0x1812f0900) on each span between GetUniqueKnots 0x18044eaf0 (K[degree], the knots K[order] .. K[nk - order - 1] that differ from the one
before, K[nk - order]) clipped to t1 .. t2 - one rule over the whole range gave 7.3 for a 6.06 long closed polyline of 34 spans, and so more
grid points (NURBS fixed 2026-11-12); for a parametric spline (class 8) MaCubicSpline::Length 0x1803fa380 measures segment by segment
through MaCubicCurve (NOT PORTED: the port still takes one rule over the whole range, no oracle run differs on it)); per candidate surface
the surface grid (build_grid(1): u / v counts FUN_180541730 / FUN_1805419b0 = (degree + 1)(control points - 1), x 4 in v for rational,
clamped 10 .. 50, a trimmed surface's counts scaled to its range; the base evaluated over the surface's +8 .. +0x20 range), its box grown by
the distance (FUN_18053aed0), and when the boxes meet FUN_180568ca0:
- the start (FUN_18053e300): the grid's triangles (two per cell) whose box (grown by the distance) meets a curve-grid segment's box; the
  segment's middle parameter (from the last end reached) projected from the triangle's uv centroid (x 0.33333) onto the surface (a trimmed
  one's base; project_pt_onto_surf -> FUN_1805ed4a0: Newton on the normal with a curvature correction (surf_curv_dir, x 1.25), 56 steps,
  eval_surf_extrap beyond the range); accepted within the distance and inside the trim loops (in_polygon, the boundary counts) / the range.
  Later searches skip the cells hit and (FUN_1805344b0) points near a piece's end; with pieces already made it calls FUN_180531890 (NOT
  DECODED: refused).
- the march (FUN_18054e8e0, 805 lines, transliterated with its labels): steps of chord x 0.5 x 10 first, x 1.1 after each accepted point,
  halved on a failed projection (beyond the distance), a chord deviation above surf_step_chord_dev or a surface off the 3D chord
  (FUN_180534200 at 1/4, 1/2, 3/4), a turn back (cos 0.93969); lines / arcs step by length, splines by brk_spline_const_err(tol, step, 100);
  the curve extended along its end tangents beyond t1 / t2; periodic seams wrapped (FUN_180556520 / FUN_180551630); the face left: bisection
  onto the boundary (FUN_180554400); boundary snapping within 0.01 (FUN_18053a9f0); a closed curve meeting its start; both directions from
  the start; pieces of more than one point.
- FUN_1805636d0 / FUN_18054d4f0: each piece's uv points thinned (a point dropped while its surface point is within 0.9 chord of the 3D chord
  and its uv within 1e-8 of the uv chord; points outside the range left out), array_2d_to_nurbs, put_cons_in_db; the sp entry's links -3 at
  an end on a periodic seam of the base (asm 180563ced: within 1e-8 of u0 / u1 with +6 bit 0, v0 / v1 with bit 1), else -1. One piece run
  per surface (param_7 = 0): the loop goes on from the reached parameter only when nothing was stored.
- after the surfaces (LAB_180569df8, decomp/curves/core1.c 1646-1800): the piece whose end is nearest the chain's start (2D in the view of
  the surface normal there, 3D within the distance, the end's direction more than 0.25 along the start tangent; a tie by the tangents) is
  swapped to the front (FUN_180554d40). The 2D distance, as built since 2026-11-12 (decision N12-2; until then |d - (d.n) n|, the same up
  to rounding): per piece end (l.1726-1740, msf_proj_cview 0) calc_surf_unit_norm(surface, Su, Sv, u, v, n, 0) (0x180164290 ->
  calc_surf_norm 0x1801637e0, decomp/partline/ev_dec.c: N = Su x Sv; within min(metric ? 1e-4 : 4e-6, (surf +0x70)^2) the stepping
  branch - u or v by a hundredth of the range, up to 50 steps, eval_surf order 3, N + (a + b) h + (2 b' + a' + c') h^2 / 2 - ported, three
  corpus runs reach it; its return is not read), then find_view_k(n, &view, 1, &found) (mastercam.dll 0x180337960: views 1 .. n_views in
  order, | |k_v . n| - 1 | <= 1e-12 at once, else the least below 2e-7; none: make_view_k (mccore 0x180017080: rows x, y, k; y = unit(k x
  e), x = unit(y x k), e = v_z_3d with e[idx] = 1, idx the smaller |k_i| of i = 0, 1) and AddMCView(m, 2e-7) (mastercam.dll 0x18031df40:
  the first view 1 .. n_views that mx33_match 0x180017880 takes - every element within 2e-7 - else a new view n_views + 1), then
  world_to_view (0x180341b30: view < 2 a copy, else the rows times the point) of the surface point and of the chain's start, the distance
  of their x, y. v_z_3d (mccore `?v_z_3d@@3Vp_3di@@A`, 0x181371ca8) is (0, 0, 0) here: it lies past the file bytes of .data (raw size
  0xc6c00 from 0x1812ab000: zero filled at load) and no instruction of mccore's .text stores to it (every RIP-relative reference scanned:
  loads, and 20 lea that pass it as a const p_3d & argument; mastercam.dll's uses of the import read it); so e is the x or y axis.
  `found` 0 (n = 0, only from a failed calc_surf_norm) keeps the last view; at the first end the DLL would read an uninitialised local
  (NOT DECODED: refused). The view table is the session's: the 18 built-in views (n_views 18, decomp/mastercam_stdviews_init.c), the
  part's stored views, those added earlier; a view this makes stays in the part (the app: mcam_add_view, written by the save as a DB_ID
  0x200 record, as Mastercam's AddMCView adds it to mcViews; whether Mastercam's undo removes it: NOT DECODED, ours keeps it).
  Ours: viewer/mccsurf.c surf_norm / surf_unit_norm / make_view_k / find_view_k / world_to_view through a CsViews table
  (core_ui/cui_csurf.c: the app's views; tools/csurf_port.c: the case's, as the oracle keeps them).
- FUN_1805474b0 -> FUN_1805474e0 with two or more pieces: FUN_1805358b0(n, sp, 1) links pieces with the same surface and base only
  (FUN_180535ea0: the four end pairs within msf_join_tol on the base, a uv distance over a tenth of the u range not linked, a nearer link
  kept); each linked chain joined (a piece taken reversed when its start link is the way on; a turn back of the tangents, dot < -0.995,
  stops; the shared point once; a closed result's ends averaged), FUN_18054d4f0 on the base (the new surface curve replaces the first
  piece), the joined pieces deleted (silent_delete). Pieces on different surfaces are never joined.

**What it makes, examples (scenes csurf_* in tools/tests/wf_scenes.txt).** A curve within 0.1 of a displayed NURBS / trimmed NURBS surface
becomes one surface curve per surface it lies on (more than one piece on one surface: joined when the ends meet, else kept apart), the
original is deleted; a curve on no surface stays. A curve leaving the surface: only the part on it is kept (the original still deleted). A
line across a hole of a trimmed plane gives only the first piece (one piece run per surface). A full circle on a cylinder (seam crossed):
two march pieces joined into one closed surface curve (85 uv points, csurf_cyl). A line along the cylinder's iso line at the picked point:
no surface curve in the DLL either (the oracle).

**Refused with a message (the curve kept):** displayed surfaces of kinds other than NURBS / trimmed NURBS (eval_surf of kinds 1, 2, 4, 6, 7,
8 not ported in this kernel); a curve used by an operation (delete_ent of a referenced entity NOT DECODED); a second piece search with pieces
made (FUN_180531890 NOT DECODED); no view for the surface normal at the first piece end (find_view_k finding and making none: n = 0 after a
failed calc_surf_norm; the DLL then reads an uninitialised view number, NOT DECODED; no corpus run). Solid faces (read_next_solid_surf_from_temp_file) are not offered (no solid surfaces temp file here).

**Oracle** (`tools/peloader --csurf`, tools/peloader/mccore_csurf.c: the DLL's own project_chains_onto_surfs on part records given as hex,
eptrs = the addresses of the images, the database / UI calls answered in the loader - get_raw_ent lists the displayed (not blanked) surface
records, store_ent / silent_delete tracked, the mastercam.dll view calls answered from a table of the 18 built-in views (n_views 18,
decomp/mastercam_stdviews_init.c; until 2026-11-12 only 1 .. 8), the case's views numbered above 8 and those find_view_k adds (make_view_k
of the DLL, AddMCView's mx33_match within 2e-7); tools/csurf_case.py makes the cases from a part, tools/csurf_port.c runs viewer/mccsurf.c
on them, tools/tests/csurf_cmp.py compares, tools/tests/csurf_oracle.sh runs a corpus sample): 86 parts (every 8th of the corpus parts with
NURBS / trimmed surfaces), up to 40 curves each, 2575 runs (surface-curve count, uv point counts, uv within 1e-9, CONS bodies within 1e-7):
**2575 the same** (2026-11-12; 2318 bit for bit, the largest difference 1.7e-12), 0 refused. Before: 2513 (the 2026-11-11 port), with the
view built 2513 (no run's order changed), then:
- 2539: eval_surf kind 0 as the DLL (FUN_1805b08d0 for order 0, FUN_1805b1f10 -> FUN_1805b0fb0 + FUN_1804b7f40 for derivatives, with
  nurb_interval 0x1805b0630, the transliteration of viewer/mcpartline_mesh.c): the port's own NURBS evaluation took the span left of a
  knot reached from 1e-14 below (u 8.9999999999999929), nurb_interval counts a knot from 1e-10 below, so the DLL evaluates the right
  span: at a C0 knot (triple knots) another derivative, another curvature +0xa8, another march.
- 2559: the arithmetic order of the projection (FUN_1805ed4a0) read from the disassembly where the decompile prints a + b + c left to
  right: det3 0x1804b7a80 adds b0 a2 c1 + (b2 a1 c0 + a0 b1 c2) (1804b7acc / 1804b7af3), surf_curv_dir 0x18047ce10 b Nn b + (M 2 a b + a
  L a) (18047ce96 / 18047ceb7), the curvature correction 1 - k (dot x 1.25) (1805edb03 / 1805edb0b; the decompile reads (k dot) 1.25),
  the |d| and |Su| sums (x^2 + y^2) + z^2 (1805ed76b, 1805ed8da). The Newton steps of a projection near C0 knots wander (sc_u 0.5 .. 1.0
  from one step to the next) and one ulp decided whether a run converged.
- 2572: eval_curve of a NURBS curve = eval_nurbs (0x1805ae2d0, param_3 1, param_6 0) with the DLL's arithmetic: mcspline_chain.c's
  transliteration moved into viewer/mcspline.c as mcs_eval_nurbs and used by mcs_spline_eval_d (so by brk_spline_const_err's steps too),
  replacing the Piegl / Tiller derivatives (equal up to rounding).
- 2575: MaNURBSpline::Length per knot span (above): the curve grid of a 34-span closed polyline (PENCILSEAMS) had 88 points, the DLL 73.

Tracing (tests only): `CSURF_PROJ_TRACE=1` prints every projection (P, the start uv, the result, ok / inu / inv) in the oracle (FUN_1805ed4a0
hooked through a trampoline of its first 18 position-independent bytes) and in csurf_port; `CSURF_PROJ_U0=u` (oracle) / `CSURF_IT=u` (port)
print each Newton step's FUN_1806faa70 / eval output of the projection starting at u.

Not checked against Mastercam itself (no Windows run): the drawing, the save / reload in Mastercam, the delete of a surface curve.
