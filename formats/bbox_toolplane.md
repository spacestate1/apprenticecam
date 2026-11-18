# Bounding Box panel: Create Tool Plane, find_view_k, sld_basic_primitive (spec)

Addresses: `mc:` = mastercam.dll, `cad:` = mccad.dll, `sld:` = mcgeomsld.dll, `core:` = mccore.dll.
New dumps (all invocations in ~/mastercam-re/regress/ghidra_bboxrest_tp.sh, runs a..n):
decomp/mastercam_findviewk.c (find_view_k, get_view_matrix, Matrix33::GetViewNo / FindView / IsEqual / GetTol /
IsValid / operator==, AddMCView), decomp/mastercam_planeadd.c (Plane ctors, MastercamPlanes::Add x3, Get/SetActivePlane
overloads, EnsureUniqueName, GetNextID, Plane::SetName / SetType, ...), decomp/mastercam_planetype.c (MapAdd,
reallocate_max_n_views, Plane::GetTypeDescription / TypeToInt, SystemPlanes::SetPlaneID), decomp/mastercam_mcview.c
(MCView ctor, LeftHandView), decomp/mastercam_stdviews_init.c (CMastercamApp::InitializeStandardViews),
decomp/mastercam_stdviews_static.c (FUN_18009ab20, the static view matrices), decomp/mastercam_stdviews_static_refs.txt,
decomp/mastercam_mcviews_refs.txt, decomp/mastercam_stdmatrix_callers.txt, decomp/mastercam_opmanres_load.c
(CMastercamApp::LoadResourceDLL), decomp/mccore_make_view_k.c (make_view_k, mx33_match),
decomp/mcgeomsld_basic_primitive.c (sld_basic_primitive + depth 2), decomp/mcgeomsld_basic_cyl.c (FUN_180033b60 ...),
decomp/mcgeomsld_primop.c (McOperation base ctor FUN_180261b90, op insertion FUN_180267170).
Disassembly checked with objdump: cad:FUN_180345fd0 (argument registers), mc:find_view_k (constants), core:make_view_k
(loop bound). Resource strings: opmanres.dll via tools/pe_strings.py.

## A. View table, find_view_k, get_view_matrix

**View table** `mcViews` (mc: CArray, data ptr DAT_1809ac028, size DAT_1809ac030). Entry = `MCView` (0x68 bytes,
mc:MCView::MCView 0x1802eb2d0): +0 vtable, +8 matrix_33 (9 f64, rows x / y / z), +0x50 / +0x58 / +0x60 = pointers to row
0 / 1 / 2 (`+0x60` -> +0x38 = the view's **k = Z row**).
Init (mc:CMastercamApp::InitializeStandardViews 0x18028e430): `n_views := 18`; entries 0..8 copied from the static array
filled by mc:FUN_18009ab20 (entry i at 0x1809aba50 − i·0x48, evaluated):
0 identity, **1 Top identity, 2 Front (1,0,0)(0,0,1)(0,−1,0), 3 Back (−1,0,0)(0,0,1)(0,1,0), 4 Bottom (−1,0,0)(0,1,0)(0,0,−1),
5 Right (0,1,0)(0,0,1)(1,0,0), 6 Left (0,−1,0)(0,0,1)(−1,0,0), 7 Iso (z row (.5774,−.5774,.5774)), 8 Axo (z row (.7071,.5,.5))**
(= ReservedPlanes::BuildStandardMatrices order). Entries 9..18 are written explicitly in the same function (not needed here).

**find_view_k(dir, short *view, char create, char *found)** (mc:0x180337960):
1. `*found = 0`. For v = 1 .. n_views (stop early if the entry pointer is NULL):
   `d = dir · k_v` (k_v = mcViews[v]+0x60 row; dir is used as given, not normalised);
   `e = | |d| − 1 |`.
   - `e <= 1e-12` (0x1805cece0) -> `*view = v, *found = 1`, return at once (first exact match in index order).
   - else if `e < 2e-7` (0x1805ec048) and (no candidate yet or e < candidate's e) -> candidate = v.
2. After the loop: candidate -> `*view = candidate, *found = 1`, return.
3. None: if `create != 0`: core:make_view_k(dir, m, found); if found: `v = AddMCView(m, 2e-7)`, `*found = (v > 0)`,
   `*view = v` if v > 0. If `create == 0` nothing more (*view untouched).
- **The opposite direction matches** (|d| is used) regardless of the `create` arg; `create` only means "add a new view
  when nothing matches". `*view` is NOT written when nothing is found and nothing is created — callers pre-load it
  (cad:FUN_18005b220 preloads the active Cplane view and, if found==0, re-reads it; then uses sign(k·dir) to place the
  second circle: `z ± len` in that view; decomp/mccad_bcyl.c l.800-825).

| dir | view returned (found = 1) |
|---|---|
| (1,0,0) | **5** Right (exact; views 1-4 give d = 0) |
| (0,1,0) | **2** Front (k = (0,−1,0), |d| = 1) — not 3 Back, index order wins |
| (0,−1,0) | **2** Front |
| (0,0,1) | **1** Top |
| (0,0,−1) | **1** Top |
| (−1,0,0) | **5** Right (not 6 Left) |

**make_view_k(dir, m, found)** (core:0x180017080): k = dir/|dir| (0 if |dir| = 0); if |k| < 0.5 -> found = 0. Else
idx = index of the smaller |k_i| among **i = 0, 1 only** (loop `cmp ax,1; jle`, z never chosen; ties keep 0);
e = v_z_3d (core export at 0x181371ca8, runtime value NOT DECODED) with e[idx] := 1.0; row1 y = norm(k × e);
row0 x = norm(y × k); row2 = k; found = 1.

**AddMCView(matrix_33 m, tol)** (mc:0x18031df40): (DbViewMap::GetAddViewsToPlanes -> also builds a PlaneBase from m
with tol, added via MastercamPlanes::Add(PlaneBase&) with the resulting view number); tol <= 0 asserts. Builds Matrix33
from m; if !IsValid -> FixMatrix (debug strings), m := fixed. Then for v = 1..n_views: `mx33_match(view_v, m, tol)`
(core: every one of the 9 elements |a−b| <= tol) -> m := stored matrix, return v. Else (n_views == max_n_views ->
max += 100, reallocate_max_n_views; failure -> max −= 100, return −1) `n_views++`, mcViews[n_views] := m,
LeftHandView(…, 0) (check only, no fix), return n_views. **Yes: a non-standard direction creates a new view number
n_views+1 (first free = 19 on a fresh session).**

**get_view_matrix(short v, matrix_33 &m)** (mc:0x18033a4f0): returns 0 if v < 0 or (v > n_views and not in
[max_n_views, max_n_views+6] — the scratch views); v = 0 is read as 1; m := mcViews[v] +8 (9 f64), return 1.

**Matrix33::GetViewNo(bool add)** (mc:0x1804295d0): !IsValid -> 0; else FindView(this, add, GetTol()).
GetTol = min of the three rows' tol fields (+0x28, +0x58, +0x88).
**Matrix33::FindView(m, add, tol)** (mc:0x180428d00): copy with SetTol(tol); v = 1..min(n_views, size−1): v == 1
compares with identity via IsEqual(tol); every v compares `operator==(mcViews[v]+8)` (P3D::operator!= per row, row tol);
first match -> v. None: `add` -> AddMCView(m, tol); else 0.
PlaneBase::FindViewNo(m, b) = Matrix33::GetViewNo(b) (mc:0x1803e0b10). PlaneBase ctors call FindViewNo(…, **false**)
(no view creation); PlaneBase::GetViewNo(b) / GetShortViewNo(b) recheck the cached +0x11c and else FindViewNo(b).
MastercamPlanes::SetActivePlane calls `PlaneBase::GetViewNo(plane, type != Gview)` -> **a non-standard plane gets its
new view number when it is made active as Cplane / Tplane / WCS** (AddMCView), not in Add.

## B. Create Tool Plane (cad:FUN_180345fd0 / cad:FUN_180346160)

Signature (disassembly): `FUN_180345fd0(app, s, char p3, double box[6], double cyl[8])`; p3 is passed through
unchanged to FUN_180346160. Callers: rect FUN_180345930 (box = real, cyl = zeroed local), cyl FUN_180345da0
(decomp/mccad_bcyl.c l.64: box = 6 zeroed doubles, cyl = real); both only when `final && s+0x43`. The panel's compute
passes **p3 = 1** (decomp/mccad_points_extra.c l.1031/1034, FUN_180346dc0/cd0(…, 1, …)).

**Origin** `FUN_180346160(app, s, p3, box, cyl, out P)`:
- Rect (s+0x30 != 0), view coords from box (x0,x1,y0,y1,z0,z1) by s+0x34:
  0 (x0,y1,z1) · 1 (x1,y1,z1) · 2 (x0,y0,z1) · 3 (x1,y0,z1) · 4 (xm,ym,z1) · 5 (x0,y1,z0) · 6 (x1,y1,z0) · 7 (x0,y0,z0) ·
  8 (x1,y0,z0) · 10 (xm,ym,zm) · default incl. 9 (xm,ym,z0); xm = (x1−x0)/2 + x0 etc.
- Cyl (s+0x30 == 0), world: s+0x38 == 0 (Top) -> c + dir·len; == 2 (Center) -> c + dir·len/2; else (Bottom) -> c;
  then `world_to_view(p, GetActiveViewNo(0))`.
- Then if p3 && !constr_view_3d(): view z := ConstrDepth + (ActiveOriginIsOn(0) ? GetActiveOriginInViewCoordinates(0).z : 0).
- `P = view_to_world(v, GetActiveViewNo(0))` (mc:view_to_world = rows-transpose rotation only, no origin added).
  PlaneType 0 = **Cplane** (below), so "active view" = the active Cplane's view.

**Orientation**: `GetActivePlane(Planes, 0)` (= Cplane, r8d = 0 in disassembly) -> its Matrix33 at Plane+0x40 (Plane
layout: +0x10 PlaneID (+0x38 class, +0x3c id), +0x40 Matrix33, +0x110 name CString, +0x11c view no, +0x130 PlanePoint
origin, +0x368 PlaneType). **New plane matrix = the active Cplane matrix, unchanged** — for a cylinder too (the cylinder
axis is not used).

**Construction**: `Plane::Plane(const Matrix33&, const P3D&, PlaneType 1)` (mc:0x1803d6590): PlaneBase() + Plane::Init
(PlaneID class 2 = named plane, type −1, work offset −1, colour defaultPlaneColor, ...), +0x374 := 1, SetType(1) ->
+0x368 = 1 (**Tplane**; only if ValidPlaneType), SetMatrix33(m), origin PlanePoint(P) at +0x130, ConstructorCheck.
Meaning of PlanePoint(P3D) (world vs view) NOT DECODED; the value passed is the world point P.
PlaneType values (mc:Plane::GetTypeDescription 0x1803dade0, opmanres strings): **0 Cplane (3280), 1 Tplane (3059),
2 Gview (3278), 3 WCS (3058)**, other "Undefined" (4016). TypeToInt = identity for < 4.

**Name**: if `CString(s+0x48).GetLength() > 0` -> Plane::SetName(name) (refused when the plane is reserved). Then
`MastercamPlanes::Add(plane, false, true)` (mc:0x1802ae620, args: r9 = false, stack = true):
1. Plane class (+0x38) 1 -> converted to PlaneBase add; not 2 -> assert "must be of class PlaneClass::NamedPlane", ID 0.
2. ID already used -> debug string, returns ID 0. Parent ID invalid -> 0.
3. Temporary flag from MastercamPlanes+0x14e5. Reserved planes checked (CheckReservedPlane / ValidateReservedPlane).
4. ID invalid -> `GetNextID(class)` = GetHighID(class) + 1 (if < 0x8000; else first unused from there; > MAXLONG -> error box).
5. Type invalid -> SetType(1).
6. Name: copy; if plane not reserved or name empty -> `EnsureUniqueName(name, 0)` (mc:0x1802b4ea0): with the Mastercam
   resource handle (= Resources\OpmanRes.dll, mc:CMastercamApp::LoadResourceDLL sets app+0x7e0): empty -> LoadString
   **174 = "Plane"**; PlaneBase::CorrectName (NOT DECODED); base := name; while IsReservedName || GetPlanePtrByName(name,
   0) || IsStatusBarPlaneLabel: name := **"%s-%ld" (base, n), n = 1, 2, ...**. So: empty -> "Plane", then "Plane-1",
   "Plane-2"…; a typed name that is taken -> "Name-1"… Changed name -> SetName.
7. ValidatePlane, MapAdd (matrix IsValid else FixMatrix; with GetAddViewsToPlanes also adds a PlaneBase for the matrix;
   inserts into the map at this+0x13e0). Failure -> "unable to add plane", ID 0.
8. If MastercamPlanes+0x18: db_has_changed; arg2 (here false) would open EditPlaneDialog; SetEntityRefs(true) unless in
   GetDb. Recent-ID array this+0x3b8 (max 100). Caller's Plane := stored copy (gets the ID / name).
9. arg3 (true) && not temporary && !IsStandardSevenReservedID(id) -> UpdatePlanesMgrBar(3).
No view number is assigned in Add (PlaneBase ctor FindViewNo(false) -> 0 for a non-standard matrix).

**Set as current / WCS**: s+0x50 -> `SetActivePlane(1 = Tplane, plane, SET_ACTIVE_ORIGIN 1)`; s+0x51 -> `(3 = WCS, …, 1)`
(mc:0x1802c9cc0). That function changes only the given type: ValidPlaneType / locks; plane already active with same
matrix (+ origin) -> only refresh; else Plane::Validate, RegenIfDirty, SetPreviousPlane, `view = GetViewNo(plane, true)`
(creates the view), SystemPlanes::SetPlaneID(type, id, view) (+ NotifyUserApp 0x16 Tplane / 0x13 WCS / 0x15 Cplane /
0x14 Gview), origin arg 1 -> SetActiveOrigin(type, plane id); cur_op op_views (+0x570 Tplane / +0x620 Cplane / +0x6d0 WCS)
whose plane id equals this plane are refreshed (ConvertNoWorkOffset); active machine group's group_view at
+0x708 + type·0x68 := plane (Plane::Convert); UpdatePlanesMgrBar(1), RepaintActivePlaneInfo. **No call sets the Cplane
when the Tplane or WCS is set** in this function (a Cplane-follows link elsewhere: NOT DECODED).
SET_ACTIVE_ORIGIN enum names NOT DECODED (value 1 = "use the plane's origin" by the code path above).

## C. sld_basic_primitive (sld:0x1800ac350)

`char sld_basic_primitive(SLD_PRIMITIVE_PARAMS *p, DB_LIST_ENT **out, bool b)`:
tag = sld_primitive_tag(p, 0, 0x1c) (partition FUN_1800414f0; switch on `int p+0`: 0xf ps_create_block(p+8),
0x10 cone, 0x11 ps_create_cylinder(p+8), 0x12 sphere, 0x13 torus) -> collect edges + faces (FUN_18004a8f0) ->
`sld_solid_from_tag(tag, b)` (sld_init_solid_ent, sld_compute_bbox, **dbput_solid(ent, b)** — external, colour / level
NOT DECODED here) -> history op `SldPrimitiveOp` (0x120 bytes, McOperation base FUN_180261b90, ctor FUN_180285cd0):
op+0x90..+0x11f := the 0x90-byte params, **op type +0x30 := p+0 (0xf / 0x11)**, op+0x18 := the solid entity, op
index := FUN_180268fd0 (opfile count, stepping back over trailing toolpath ops when `sldAddBeforeToolp`), face/edge
ids FUN_1802656b0, inserted into the opfile + HistDlg_addOp (FUN_180267170). Opfile alloc failure -> message 0x109.
History display name for types 0xf / 0x11: NOT DECODED (not set in these functions).
All lengths are divided by 1000 (IsMetric) or 39.37007874015748 before Parasolid.

**SLD_PRIMITIVE_PARAMS** (0x90 bytes; offsets from p):
| off | block 0xf (ps_create_block 0x1800402a0) | cylinder 0x11 (ps_create_cylinder 0x180041280) |
|---|---|---|
| +0x00 | i32 type 0xf | i32 type 0x11 |
| +0x08 | origin p_3d | origin p_3d |
| +0x20 | vector A (axis: block local Z / 3rd size) | axis vector |
| +0x38 | vector B (block local X / 1st size) | reference vector (not read by the full-cylinder path) |
| +0x50 | vector C (decides handedness, see below) | — |
| +0x68 | size 1 (along B) | **radius** (CRCYSO 3rd arg) |
| +0x70 | size 2 (along A×B or mirrored) | **height** (CRCYSO 4th arg) |
| +0x78 | size 3 (along A) | start angle, 0..360 |
| +0x80 | — | end angle, 0..360 |
Block checks: all sizes >= tol, |A|,|B|,|C| >= tol, A/B not parallel (| |A·B| − 1 | >= 0.01), else message 0x143.
Build: PK_BODY_create_solid_block(s1, s2, s3, basis{origin, axis (0,0,1), ref (1,0,0)}); rotate so (0,0,1) -> A
(axis z×A, or B when A = −Z; angle acos(A·z)); rotate about A so the rotated X -> B; if `A · (B × C) <= 0` reflect in the
plane through origin with normal = rotated Y. Must have 6 faces (attr 0x3c, index 1..6) and 12 edges (attr 0x800001e).
Placement of the block relative to its basis is Parasolid's (not decompiled).
Cylinder checks: radius, height >= tol, |axis| >= tol, angles in [0,360], else message 0x147. start < tol and
end > 360 − tol -> FUN_180033b60: `CRCYSO(origin, axis, radius, height)`, 3 faces (attr 0x3c, 1..3), 2 edges; otherwise
FUN_180033e60 (partial; not needed here).

**What the Bounding Box passes** (cad, already in the decomp files):
- Block (cad:FUN_18005a2e0 bit 8, decomp/mccad_points_extra3.c l.290-360): m = get_view_matrix(active Cplane view);
  centre = mean of the 8 corners, view_to_world (Top: unchanged); requires all three spans > tol;
  origin = centre − |dz|/2 · m.row2 (= centre of the zmin face, world); A = m.row2 (view Z), B = m.row0 (view X),
  C = norm(row2 × row0) (view Y); sizes |dx|, |dy|, |dz|. Type 0xf, `sld_basic_primitive(&p, &ent, false)`.
- Cylinder (cad:FUN_18005b220 bit 8, decomp/mccad_bcyl.c l.911-959, only if len > tol): origin = c (low end centre),
  axis = dir, ref = norm((dir × u) × dir) with u = (1,0,0) when |dir.y| > tol or |dir.z| > tol, else u = (0,0,1)
  (cad .rdata 0x18040a6b0 / 0x18040a6c0), radius = r, height = len, angles 0 / 360. Type 0x11, b = false.

## NOT DECODED
- core v_z_3d runtime value (seed of make_view_k); PlaneBase::CorrectName; IsReservedName / IsStatusBarPlaneLabel rules.
- PlanePoint(P3D) semantics (world vs plane coords); SET_ACTIVE_ORIGIN enum names; any Cplane-follows-Tplane link.
- dbput_solid (colour / level of the new solid; external to mcgeomsld); SldPrimitiveOp history name for 0xf / 0x11;
  Parasolid block/cylinder placement relative to the basis (PK / KI semantics, not in the binaries).
- mccad functions not re-dumped here (ghidra_cad not used): none needed beyond decomp/mccad_bcyl.c,
  mccad_points_extra*.c; the live-solid in-place updates FUN_180347b00 / FUN_180347f70 remain NOT DECODED.
