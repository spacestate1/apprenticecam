# Bounding Box panel — live solid and PUSH PULL (mccad.dll)

Companion to bboxpanel_spec.md (§1 settings struct `s`, §4 compute). All addresses mccad.dll unless noted.
Decomp sources (in decomp/):
- `mccad_bbox_pushpull.c`: FUN_180343900, FUN_1803450b0, FUN_180345180, FUN_180347730, FUN_180347b00
- `mccad_bbox_panel.c`: FUN_180344b30, FUN_180345470, FUN_180346490, FUN_180346850, FUN_1803469a0, FUN_180346a60, FUN_180348590
- `mccad_bbox_stock.c`: driver FUN_180343ae0 (event loop ~l.1180-1420)
- `mccad_bcyl.c`: FUN_180345c50, FUN_180347f70, FUN_18005b220, FUN_1803483b0
- `mccad_points_extra2.c`: FUN_180345b10, FUN_1803481e0; `mccad_points_extra3.c`: FUN_18005a2e0; `mccad_points_extra.c`: FUN_180346eb0
- NEW `mccad_bbox_pp2.c`: FUN_18005d7b0 (settings copy-assign)
- NEW `mccad_bbox_pp2_*.txt`: call-site disassembly (CallContext) giving the register/stack arguments Ghidra dropped:
  `_drv_6850` (driver->FUN_180346850 @0x180343fd3), `_drv_69a0` (->FUN_1803469a0 @0x180344789, @0x1803447f6),
  `_drv_6a60` (->FUN_180346a60 @0x1803448b2), `_drv_4b30` (->FUN_180344b30 @0x180344327, @0x1803446b3),
  `_69a0_5180` (FUN_1803469a0 tail-jump ->FUN_180345180 @0x180346a50), `_6a60_5180` (@0x180346c02),
  `_6a60_4b30` (@0x180346c6f), `_5180_upd` (FUN_180345180 ->FUN_180347b00/7f70 @0x1803453aa/b5),
  `_7730_psbody` (FUN_180347730 PSBodyAsk* args), `_consts` (MemDump 0x18040a6b0).
  Invocation: ~/mastercam-re/regress/ghidra_bboxrest_pp.sh.

Notation: box[6] = xmin,xmax,ymin,ymax,zmin,zmax (active-plane view coords); cyl[8] = r, len, c[3], dir[3] (c = world
end point on the low side of dir). s = settings struct (bboxpanel_spec §1: +8 SizeX, +0x10 SizeY, +0x18 SizeZ,
+0x20 Radius, +0x28 Height, +0x30 rect, +0x34 RectangularOrigin, +0x38 CylindricalOrigin, +0x41 Incremental,
+0x42 BothDirections). "tol" = mccore tol_exref.

## 1. Live solid

### 1.1 Creation (preview, final = 0)
FUN_180345b10 (rect) / FUN_180345c50 (cyl): when no live solid exists and the shape is valid (rect FUN_180345830: all three
spans >= tol; cyl FUN_180345800), the output routine is called with flags = **8 only** (rect FUN_18005a2e0(box, 8, list, ..),
cyl FUN_18005b220(c, dir, r, len, 8, list)); if the list then holds >= 1 entity, its first entity becomes the live solid
(`*liveSolid = list[0]`), its faces+edges are listed (FUN_180347730) and the list is cleared (FUN_18000a890) — the solid
itself is not deleted. Invalid shape -> rect flags 2 (lines), cyl flags 2 (circles), no live solid.
If a live solid exists: valid shape -> update in place (§1.3); invalid -> FUN_180347310 deletes the live geometry; then if the
pointer became 0 a new one is created as above. (FUN_180345b10, FUN_180345c50.)

Solids are made by mcgeomsld `sld_basic_primitive(SLD_PRIMITIVE_PARAMS*, DB_LIST_ENT**, false)` (FUN_18005a2e0 bit 8;
FUN_18005b220 bit 8, only if len > tol).

### 1.2 SLD_PRIMITIVE_PARAMS as filled by mccad (byte offsets)
Identical fill in create (FUN_18005a2e0 / FUN_18005b220) and update (FUN_180347b00 / FUN_180347f70).
View matrix = `get_view_matrix(GetActiveViewNo(Planes,0))`; its rows are the view X/Y/Z axes in world
(world_to_view = rows·p, formats/mcam.md:455, mastercam.dll 0x180341b30/0x180341520).

| off | rect (type 0xf, "block") | cyl (type 0x11) |
|---|---|---|
| +0x00 int | 0xf | 0x11 |
| +0x08..+0x18 p3d origin | world centre of the box − Zv·(\|zmax−zmin\|/2) = centre of the **zmin face** (centre = average of the 8 corners in view coords, then view_to_world; view_to_world skipped when active view no. == 1) | c (cyl[2..4]) |
| +0x20..+0x30 v3d | Zv = view-matrix row 2 (box "up" axis) | dir (cyl[5..7]) |
| +0x38..+0x48 v3d | Xv = view-matrix row 0 | ref = normalize((dir × h) × dir), h = (1,0,0) if \|dir.y\|>tol or \|dir.z\|>tol, else h = (0,1,0) (update: literal 1.0/0.0; create: DAT_18040a6b0 = (1,0), DAT_18040a6c0 = (0,1), `_consts`) |
| +0x50..+0x60 v3d | normalize(Zv × Xv) (= Yv; zero vector if length 0) | not written by the caller |
| +0x68 f64 | \|xmax−xmin\| | r (cyl[0]) |
| +0x70 f64 | \|ymax−ymin\| | len (cyl[1]) |
| +0x78 f64 | \|zmax−zmin\| | 0.0 |
| +0x80 f64 | 0 (update: block memset 0, 0xc0 bytes) | 360.0 (0x4076800000000000) |
Field names of SLD_PRIMITIVE_PARAMS are NOT DECODED (only the values written).

### 1.3 In-place update
- Rect FUN_180347b00(app, box, liveSolid, liveTagList, int* faceTag): only if all three |spans| > tol (strict); fills the
  table above and calls mcgeomsld `sldModifyBBoxSolidPrim(params, liveSolid, faceTag)` (int& — may update the tag; NOT
  DECODED), then FUN_180347730 relists tags. Otherwise does nothing (solid left as is).
- Cyl FUN_180347f70(app, cyl, liveSolid, liveTagList, int* faceTag): only if r >= tol and len >= tol; same call.
  (Argument order from `_5180_upd`: RCX app, RDX box/cyl, R8 liveSolid, R9 list, [RSP+0x20] faceTag ptr.)

### 1.4 Face/edge tag list (FUN_180347730)
Clears the CArray<int>, then `tag = sld_get_tag_from_solid(liveSolid)`; `PSBodyAskFaces(tag, list)`;
`PSBodyAskEdges(tag, list)` — both into the same array (`_7730_psbody`: RDX = RDI for both; whether the second call
appends or replaces is mcgeomsld, NOT DECODED). Use: while an arrow drag is active `SetPsSelectNoPickTags(list)`
(FUN_180346850), i.e. the live solid's faces/edges are not pickable during a drag.

### 1.5 Face ids (sldAskPrimBBoxSolidFaceId(faceTag), mcgeomsld)
Meaning derived from what FUN_180345180 changes (anchor codes per FUN_180346490) and FUN_180343900:

| id | rect side | size changed | anchor during drag (keeps) | cyl face | changed |
|---|---|---|---|---|---|
| 1 | +Z (zmax, top) | SizeZ | 9 BottomCenter (keeps zmin) | top end (c + dir·len) | Height, CylOrigin 1 Bottom (keeps c) |
| 2 | −Y (ymin, front) | SizeY | 6 BottomBackRight (keeps ymax) | base end (c) | Height, CylOrigin 0 Top (keeps c+dir·len) |
| 3 | −X (xmin, left) | SizeX | 6 (keeps xmax) | lateral | Radius := value |
| 4 | +Y (ymax, back) | SizeY | 7 BottomFrontLeft (keeps ymin) | — | — |
| 5 | −Z (zmin, bottom) | SizeZ | 4 TopCenter (keeps zmax) | — | — |
| 6 | +X (xmax, right) | SizeX | 7 (keeps xmin) | — | — |
Both directions: rect anchor := 10 TrueCenter; cyl faces 1/2 CylOrigin := 2 Center. The id function itself is NOT DECODED.

## 2. Face pick -> arrow (FUN_180344b30)

Trigger: driver event **0x2000** (face pick) — face tag = MCEvent+0x17c (`_drv_4b30`: EDX = [RBP+0x2ec], event at
RBP+0x170); stored as the current face (driver local_f64), edit flag := 0. Picking is limited to the live solid:
FUN_180346850 calls `SetPsSelectOnlyPickTagsFromSolid(liveSolid)` and includes 0x2000 in the mask only when a live
solid exists (table §3.1). Also re-run after code 6 and after every committed drag (same face tag).

FUN_180344b30(app, faceTag, &arrow, &base, s):
1. base := 0; remove the previous arrow (FUN_180345470: EmRemoveSelObj, release).
2. `sldFaceNormalAtMidPt(faceTag, &P, &N)`; on false: nothing.
3. `isCyl = sldIsSldFaceCyl(faceTag, &rad, &convexity)`; rad := rad × (IsMetric() ? 1000.0 : 39.37007874015748).
   - isCyl: offset := rad; convexity == 0 -> P := P − N·rad; else P := P + N·rad and, if convexity == 1, N := −N.
     (Result: anchor on the axis, offset = radius.)
   - planar: offset := 0.
4. Absolute (s+0x41 == 0) and planar: size := FUN_180343900(faceTag, s); P := P − N·size; base := −size; offset := size.
   FUN_180343900: rect (s+0x30): id 1/5 -> SizeZ, 2/4 -> SizeY, 3/6 -> SizeX; cyl: id 1/2 -> Height, else 0.
   (Result: anchor on the opposite face, arrow displayed at the picked face, arrow value = total size.)
5. Arrow object (mccore classes, `new` 0x48 bytes):
   - isCyl: `SelArrow(convexity == 1)`; then `GrArrow::SetLimits(0.0, 1e38)`.
   - planar: `SelRefArrow()` (no limits set).
   GrArrow = object +0x40. `SetAnchorPnt(P)`, `SetDirection(N)`, `SetOffset(offset)`,
   `SetColor(0, UtColor(0x80,0,0,0xff,false))`, then `EmAddSelObj(arrow)`.

| face | class | anchor | direction | offset | base |
|---|---|---|---|---|---|
| planar, Incremental | SelRefArrow | face mid-point | face normal N | 0 | 0 |
| planar, Absolute | SelRefArrow | mid − N·size | N | size | −size |
| cylindrical (cyl lateral) | SelArrow(conv==1), limits [0,1e38] | mid ∓ N·rad (axis) | N (negated if conv==1) | rad | 0 |

## 3. Drag and commit

### 3.1 Event mask (FUN_180346850, args from `_drv_6850`: app, prompt, event, liveSolid, tagList, arrow, editActive, &accept)
| state | mask | pick filter |
|---|---|---|
| no live solid | 0x10008000 | SetPsSelectOnlyPickTagsFromSolid(NULL) |
| live solid, no arrow | 0x1000a000 | OnlyPickTagsFromSolid(liveSolid) |
| arrow, not editing | 0x1140a000 | OnlyPickTagsFromSolid(liveSolid) |
| arrow, editing | 0x13808001 | SetPsSelectNoPickTags(tagList) |
`get_event(prompt, ev, mask, 0, 1, &accept)`; afterwards the filter is cleared. If get_event returns **1 while editing**:
accept := 1, `EmCancelSelObjEdit(false)`, `PostMessageA(0,0x8038,6,0)` (= panel code 6). (formats/extrude.md §1.3 records
return 1 as Escape / right-click for the same get_event; not re-verified here.) The driver then continues the loop.

### 3.2 Arrow events in FUN_180343ae0 (names by analogy with formats/extrude.md §1.3; editActive = local_f68,
last = local_f28 [RBP−0x50], base = local_f38 [RBP−0x60])
| event | action |
|---|---|
| 0x400000 (arrow grabbed) | editActive := 1, last := 0 |
| 0x800000 (drag move) | FUN_1803469a0(active = editActive, force = 0) -> live preview |
| 0x1000000 | editActive := 1; FUN_1803469a0(active = 1, force = 1); if `EmGetSelEditStatus(ev) == 2` -> commit FUN_180346a60 |
| 0x2000000 (edit end) | commit FUN_180346a60 |
| code 6 | if an arrow exists: FUN_180346eb0 (no recompute, not final; removes the arrow, re-applies panel sizes from s via FUN_180346490 and updates the live solid back to s) then FUN_180344b30(current face); editActive := 0 |
Note: bboxpanel_spec §2 says code 6 is posted "when a push-pull drag ends" — per FUN_180346850 it is posted when the
drag is **cancelled** (get_event == 1 while editing); normal ends arrive as 0x1000000/0x2000000.

### 3.3 Value (FUN_1803450b0(app, ev, active, base, &value, &last, force))
If `EmGetSelItem(ev) != 2` and active: `value = base + ((GrArrow*)EmGetGrObj(ev))->+0x1a8` (dynamic_cast to
CncGaf::GrArrow); if force or |last − value| >= tol: last := value, return true (-> FUN_180345180(commit=0)).
Item 2, or no change: repaint_graphics, false. Field GrArrow+0x1a8 (in gafgr.dll) is NOT DECODED; if it is the offset set by
SetOffset (also NOT DECODED), then for planar faces value = signed distance moved along N in both modes (Absolute: arrow
shows the total size, base −size turns it back into a delta), for the cyl lateral face value = new radius.

### 3.4 Size update (FUN_180345180(app, value, s, box, cyl, liveSolid, tagList, &faceTag, commit))
Works on copies (s via FUN_18005d4f0, box, cyl). d = value, doubled if s+0x42 Both.
- Rect: id 1/5 SizeZ += d; 2/4 SizeY += d; 3/6 SizeX += d; anchor per §1.5 (Both -> 10).
- Cyl: id 1 Height += d, CylOrigin := 1; id 2 Height += d, CylOrigin := 0; Both and id 1/2 -> CylOrigin := 2;
  id 3 Radius := value (not doubled, CylOrigin unchanged).
- FUN_180346490(copy s, box, cyl) (bboxpanel_spec §4 resize; cyl: Top(0) c += dir·(len−H), Center(2) c += dir·(len−H)/2,
  Bottom(1) c unchanged; r := Radius, len := Height). No clamp: sizes may go negative here (FUN_18015c3d0 clamps to 0
  only when the panel is read back).
- Live solid: rect FUN_180347b00(box'), cyl FUN_180347f70(cyl').
- commit != 0: restore the original RectangularOrigin and CylindricalOrigin into the copy, `FUN_18005d7b0(s, copy)`
  (field-wise copy, mccad_bbox_pp2.c), box := box', cyl := cyl'. commit == 0 (drag preview): s/box/cyl untouched.

Examples (Incremental, Both off): drag +Y face (id 4) by 5 -> SizeY += 5, ymax += 5; Both on -> SizeY += 10, ymin −= 5,
ymax += 5 (anchor 10 also centres Z, a no-op when SizeZ is unchanged).

### 3.5 Commit (FUN_180346a60(app, ev, &last, &editActive, &base, s, box, cyl, liveSolid, tagList, &faceTag, model,
&arrow, startPlane); args from `_drv_6a60`)
editActive := 0. If `EmGetSelItem(ev) == 2` and the object is a GrRefArrow: base += `GrRefArrow::GetRefPntOffset()` (the
reference point was moved; nothing else). Otherwise: if |last| > tol: ActivePlaneSaveRestore + SetActivePlane(start plane);
FUN_180345180(last, ..., commit = 1); stock write (rect FUN_1803481e0 / cyl FUN_1803483b0 — both return unless s+0x60
stock mode); FUN_18015d2c0(model, s, 1) (push to panel; the bool goes to FunctionPanelEditable*::SetValue, meaning NOT
DECODED); Volume FUN_180348590 (rect |dx·dy·dz|, cyl π·r²·len). Then (always) FUN_180344b30(faceTag) re-creates the arrow
on the (possibly re-tagged) face with the new size; update_graphics(true,true).
The drag preview (FUN_1803469a0) does **not** switch to the start plane; only the commit does.

## 4. Typed value on the arrow
mccad never calls an edit-field API itself; it only reacts to events. The arrow classes are mccore.dll exports
(exports/mccore.dll.tsv): SelArrowBase::HasEditField / SetEditEnabled / RunSettingsDlg / ReadFromRegistry /
WriteToRegistry / UpdateSettings, statics s_ArrowSize, s_RulerActive, s_RulerSize, s_InitValues; SelArrow and SelRefArrow:
GetEditValue / SetEditValue(double,bool) / GetGrEdit (CncGaf::GrEdit) / SetEditMode(SelObj::EditMode,int) -> EditEvent /
Drag / ApplyDrag / Select / GetCursorPnt / IsValidItem / Highlight / GetSelType / GetArrow; SelRefArrow::GetGrRefArrowPtr;
SelArrow::GetGrArrowPtr. So the generic arrow has an edit-field mechanism; whether it is enabled here and that event
0x1000000 with SelObjEdit status 2 is the typed-value path are NOT DECODED (mccore internals, other project). Whatever the
source, the value used is base + GrArrow+0x1a8 (§3.3), i.e. distance along the arrow (radius for the cyl side).

## 5. External functions in the push-pull path
| function | DLL | use |
|---|---|---|
| sld_basic_primitive | mcgeomsld | create the live solid (type 0xf / 0x11) |
| sldModifyBBoxSolidPrim(params&, solid, int&) | mcgeomsld | update the live solid in place |
| sld_get_tag_from_solid, PSBodyAskFaces/Edges(tag, CArray&) | mcgeomsld | tag list for pick exclusion |
| sldAskPrimBBoxSolidFaceId(tag) | mcgeomsld | face id 1..6 / 1..3 |
| sldFaceNormalAtMidPt(tag, P&, N&) | mcgeomsld | arrow anchor/direction |
| sldIsSldFaceCyl(tag, r&, SldBlendConvexity&) | mcgeomsld | cylindrical face test, radius (scaled ×1000 / ×39.37) |
| SetPsSelectOnlyPickTagsFromSolid, SetPsSelectNoPickTags, ClearPsSelect* | mcgeomsld | pick filters |
| SelArrow(uchar), SelRefArrow() | mccore | arrow objects |
| EmAddSelObj, EmRemoveSelObj, EmCancelSelObjEdit, EmGetSelItem, EmGetGrObj, EmGetSelEditStatus, get_event | mccore | arrow registration / events |
| CncGaf::GrArrow SetAnchorPnt/SetDirection/SetOffset/SetColor/SetLimits, GrRefArrow::GetRefPntOffset, UtColor | gaf* (CncGaf) | arrow geometry |
| get_view_matrix, view_to_world, GetActiveViewNo, ActivePlaneSaveRestore, SetActivePlane | mastercam | frames |

## NOT DECODED
- sldAskPrimBBoxSolidFaceId, sldModifyBBoxSolidPrim (incl. whether faceTag is rewritten), PSBodyAskEdges append vs replace.
- SLD_PRIMITIVE_PARAMS field names; cyl +0x50 vector (not written by mccad).
- SldBlendConvexity enum values; SelArrow ctor bool meaning; units of sldFaceNormalAtMidPt vs sldIsSldFaceCyl.
- GrArrow+0x1a8 (value field) and SetOffset semantics; GrArrow default length/size; UtColor argument order (0x80,0,0,0xff).
- MCEvent bit 0x1 in the editing mask; SelObjEdit value 2; EmGetSelItem item 2 (ref point by context only); whether the
  arrow edit field is enabled for this panel.
- FUN_18015d2c0 third argument (FunctionPanelEditable*::SetValue bool).
