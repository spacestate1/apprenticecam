# OCCT accuracy check: the user's Parasolid solids rebuilt in OpenCASCADE (2026-10-23)

Question: can every solid in the user's parts (`<user-parts>/*.mcx-9`) be reproduced *exactly* in OpenCASCADE
(OCCT 7.9.3, the approved stand-in for Parasolid), with any deviation > 0.001 mm flagged?

**Answer: yes. All 108 solids in the 17 parts translate. Every solid is valid (`BRepCheck_Analyzer`) and passes
`BRepAlgoAPI_Check` (so it can go into booleans), and none has free edges. The largest deviation of any kind is
2.8e-6 mm, so nothing is flagged.** Volume, area and bounding box match values computed straight from the XT data to
1e-9 mm or better (ΔV/area ≤ 2.5e-9 mm). OCCT uses the same analytic geometry as Parasolid (plane, cylinder, line,
circle), so the translation itself adds no approximation. The only representation change is a seam edge on
cylinder faces that wrap all the way round (144 seams); it adds no geometry.

Code (standalone; nothing in the main build changed): `viewer/occt/` -
`xt_occt.h/.cpp` (translator), `xt_occt_check.cpp` (this check), `xt_extract.py` (dumps the XT blobs from the parts),
`Makefile` (`make` / `make check` / `make mutate`; binaries and results in `~/mastercam-re/work/occt/`:
`accuracy.tsv`, `accuracy_v.tsv` (with notes), `mutant_*.tsv`). Run time: 52 s for all 108 solids, < 200 MB.

## 1. XT entity coverage in the user's parts

Counts over the 108 solids (all 17 parts; XT schema 26105, formats/xt.md):

| XT node | count | OCCT mapping | status |
|---|--:|---|---|
| BODY | 108 | `TopoDS_Compound` of solids (one per solid region) | mapped (body_type 1, res_linear 1e-8 m on every body) |
| REGION | 216 | type 'S' -> `TopoDS_Solid`; type 'V' (the outside) not built | mapped (one solid + one void region per body) |
| SHELL | 216 | `TopoDS_Shell` (the solid region's shell) | mapped |
| FACE | 1445 | `TopoDS_Face` (`sense` -> orientation) | mapped |
| LOOP | 1787 | `TopoDS_Wire` | mapped |
| HALFEDGE (fin) | 6730 | oriented edge in its wire | mapped |
| EDGE | 3365 | `TopoDS_Edge` (shared by its two faces) | mapped |
| VERTEX / POINT | 2052 / 2052 | `TopoDS_Vertex` | mapped |
| LINE | 2426 | `Geom_Line` | mapped exactly |
| CIRCLE | 936 | `Geom_Circle` | mapped exactly |
| PLANE | 979 | `Geom_Plane` | mapped exactly |
| CYLINDER | 466 | `Geom_CylindricalSurface` | mapped exactly |
| ATTRIBUTE / ATTRIB_DEF / ATT_DEF_ID / LIST / POINTER_LIS_BLOCK | 4810 / 108 / 108 / 108 / 108 | not translated (Parasolid attributes, e.g. colour / Mastercam ids) | ignored (no geometry) |

No other entity types appear. The same holds for the user's two Windows parts and the Mastercam sample parts that
parse (basic-mc01.mcam, Mill.mcam: also plane/cylinder/line/circle only).
**Not mapped** (none of them occur, and there is no sample to verify their parametrisation or sense conventions
against): ELLIPSE, PARABOLA, HYPERBOLA, B_CURVE / NURBS_CURVE, INTERSECTION, SP_CURVE / PCURVE, TRIMMED_CURVE,
OFFSET_CURVE, CONE, SPHERE, TORUS, B_SURFACE / NURBS_SURF, OFFSET_SURF, SWEPT_SURF, SPUN_SURF, blend surfaces. The
translator refuses a body containing any of them and names the type (`Result::unmapped`); it never approximates.
These will be needed once OCCT-made solids (fillets -> torus / B-surface, revolve -> cone / sphere / torus / spun) have
to round-trip through XT. To decode them we need either an XT file that contains them (one written by Mastercam with
fillets / cones) or the Parasolid XT format reference.

## 2. Translation rules (sources)

- File and node layout: formats/xt.md, tools/xt_parse.py, viewer/xt_mesh.c `parse` (driven by Parasolid's own schema
  `sch_26105.s_t`). The C++ reader in `xt_occt.cpp` follows xt_mesh.c line for line. Units: XT metres x 1000 = mm.
- Each edge is built once and shared by both of its faces. EDGE.halfedge runs from the partner fin's vertex to its own
  vertex; HALFEDGE.sense '-' means the fin runs against its curve (formats/xt.md "Edges"). In OCCT this is the edge's
  orientation in that face's wire.
- CIRCLE.sense '-' = the circle runs about -normal (viewer/xt_mesh.c `sample_edge`). Every '-' circle in the user's
  parts is a ring edge on a hole cylinder (54 nodes in 6 solids). The rule is confirmed by the mutation test in
  section 5.
- LINE.sense '-' (6 nodes, BRACKET4): line edges are built through their two vertices, so the sense only orders
  the parameters. The check that the end parameter is greater than the start held for every line.
- Surface and face sense: the face normal is the surface's natural normal x surface sense x face sense. A face whose
  normal opposes the natural normal is used REVERSED in OCCT, with its fins reversed inside the face. CYLINDER.sense
  '-' occurs on 67 hole cylinders.
- Loops run with the face on their left, seen along the face's outward normal (viewer/btopo.h). This is OCCT's own
  rule, so the wires carry over unchanged.
- FACE.shell is the shell behind the face and FACE.front_shell the shell in front. SHELL.face / FACE.next walk the
  faces behind a shell; SHELL.front_face / FACE.next_front walk the faces in front, which are reversed for that shell.
  The data confirms this: every solid region's faces have the region behind them.
- Tolerance: BODY.res_linear (Parasolid's linear resolution, 1e-8 m = 1e-5 mm on every body). It is used for
  vertices, edges and faces, and no tolerance was enlarged (max tolerance after building = 1e-5 mm).

Our choices (labelled "ours" in the code):
- **pcurves are built analytically.** Line or circle on a plane: a 2D line or circle in the plane's frame. On a
  cylinder: axial lines become u = const and coaxial circles become v = const, shifted by 2πk so each wire is
  continuous. A pcurve is accepted only if it stays within the tolerance of the 3D curve.
  Measured: ≤ 9.2e-8 mm (buckle-v12 sld_401 has a Parasolid line tilted 8.5e-9 rad out of its plane, i.e. Parasolid's
  own data is exact only to its resolution).
- **Seams.** Parasolid lets a face wrap all the way round a cylinder without a seam edge; OCCT requires one. On a band
  between two wrapping loops, an axial seam edge is added through a vertex shared by both loops:
  - between two rings, the cylinder's u = 0 line;
  - between a ring and a loop with vertices, the loop vertex nearest the ring.

  A ring edge (no vertex in XT) gets its one OCCT vertex on that seam. 144 seams were added. All were built this way,
  so `ShapeFix_Face::FixMissingSeam` (the fallback, which may split edges) was never needed. Seams add no geometry;
  OCCT simply has 144 more edges than XT (3509 vs 3365).

## 3. Method of the check (`viewer/occt/xt_occt_check.cpp`)

Per solid:
1. **OCCT validity:** `BRepCheck_Analyzer`, `BRepAlgoAPI_Check` (self-intersection, small edges, ...: the input check
   that booleans use), free edges (`ShapeAnalysis_FreeBounds`).
2. **Properties computed from the XT data alone, without OCCT (ours):**
   - Exact area of each face by Green's theorem over its XT loops:
     - plane: ½∮(u dv − v du) in the plane frame, with line and arc terms in closed form
       (arc: c×(p1−p0) + R²(e1×e2)Δφ);
     - cylinder: ∮ −h·r dθ over the coaxial arcs (axial lines contribute 0).
   - Exact volume by the divergence theorem, V = ⅓ Σ∫ x·n dA:
     - plane: ⅓ (p0·n) A;
     - cylinder: ⅓ ∮ −h r (P0·u(θ) + r) dθ, integrated in closed form.
   - Exact bounding box from the edges: the fin end points plus the arc extremes inside each arc. For planes and
     cylinders the extremes lie on edges.

   These use the same decoded topology rules but none of the OCCT geometry. Each face's exact area must also come
   out positive, which tests the loop-orientation rule.
3. **Compared with OCCT:**
   - `BRepGProp` volume and area, adaptive to 1e-10;
   - area per face;
   - `BRepBndLib::AddOptimal` box on the exact geometry.

   Reported as ΔV / total area and ΔA_face / face perimeter, i.e. the equivalent normal shift in mm.
4. **Deviation in both directions:**
   - (a) **XT → OCCT faces:** points taken on each XT face from its exact surface on a 15x15 (plane) or 36x9
     (cylinder, θ x h) grid, kept only inside the XT trim (even-odd test against the XT loops). Distance to the
     *trimmed* OCCT face: projection plus `BRepClass_FaceClassifier`, falling back to `BRepExtrema`.
   - (b) **XT → OCCT edges:** 9 points per fin on the exact XT curve, distance to the OCCT edge
     (`ShapeAnalysis_Curve::Project`, limited to the edge's range).
   - (c) **OCCT → XT faces:** nodes of OCCT's own triangulation (`BRepMesh`, deflection 0.005 mm / 0.05 rad), distance
     to the XT face: exact surface distance if inside the XT trim, otherwise the distance to the XT boundary (sampled
     every 0.05°, which bounds this column at ≈ 1e-7·r: the 2.8e-6 mm maximum is that sampling, not a deviation).
   - (d) **Cross-check against viewer/xt_mesh.c:**
     - its mesh vertices against the OCCT faces (`BRepExtrema`);
     - its mesh area and volume against OCCT's (its 5° chords alone give a relative error of up to 1.3e-3 on
       cylinders).

   In total 527,978 sample points were measured.
5. **Flags:** any deviation > 0.001 mm, ΔV/area > 0.001 mm, ΔA_face/perimeter > 0.001 mm, bbox Δ > 0.001 mm,
   invalid, failed bool-check, free edges, negative volume, or XT-exact not computable.

## 4. Results

### Per part (maximum over its solids; all values in mm; "XT gap" = Parasolid's own vertex-to-curve and curve-to-surface gaps)
| part | solids | faces | seams added | valid + bool-check OK | max ΔV/area | max face ΔA/perim | max bbox Δ | max XT→OCCT face | max XT→OCCT edge | max OCCT→XT | max XT gap | **max deviation** | flag |
|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|---|
| A001 | 1 | 11 | 0 | 1/1 | 6.7e-16 | 1.1e-15 | 0.0e+00 | 0.0e+00 | 1.8e-15 | 0.0e+00 | 1.8e-15 | **1.8e-15** | - |
| bracket4-dashed | 1 | 34 | 8 | 1/1 | 3.7e-16 | 2.7e-15 | 2.2e-16 | 7.2e-15 | 1.3e-14 | 4.9e-07 | 1.3e-14 | **4.9e-07** | - |
| BRACKET4 | 1 | 34 | 8 | 1/1 | 3.7e-16 | 2.7e-15 | 2.2e-16 | 7.2e-15 | 1.3e-14 | 4.9e-07 | 1.3e-14 | **4.9e-07** | - |
| buckle1 | 12 | 105 | 9 | 12/12 | 2.4e-15 | 3.9e-15 | 7.1e-15 | 3.6e-15 | 3.0e-14 | 6.6e-07 | 3.0e-14 | **6.6e-07** | - |
| buckle-v10-back | 1 | 27 | 6 | 1/1 | 0.0e+00 | 1.9e-15 | 0.0e+00 | 1.8e-15 | 1.6e-14 | 1.9e-07 | 1.6e-14 | **1.9e-07** | - |
| buckle-v10 | 12 | 162 | 16 | 12/12 | 8.6e-10 | 1.1e-09 | 7.1e-15 | 7.1e-15 | 5.2e-14 | 8.1e-07 | 8.1e-09 | **8.1e-07** | - |
| buckle-v11 | 11 | 166 | 15 | 11/11 | 7.1e-10 | 1.0e-09 | 3.6e-15 | 7.1e-15 | 4.5e-14 | 2.8e-06 | 8.1e-09 | **2.8e-06** | - |
| buckle-v12 | 11 | 169 | 15 | 11/11 | 2.5e-09 | 1.4e-08 | 3.6e-15 | 7.1e-15 | 9.2e-08 | 2.8e-06 | 9.2e-08 | **2.8e-06** | - |
| buckle-v6 | 15 | 174 | 14 | 15/15 | 3.6e-11 | 9.6e-10 | 6.1e-15 | 3.6e-15 | 1.6e-11 | 8.2e-07 | 8.1e-09 | **8.2e-07** | - |
| buckle-v7 | 15 | 174 | 14 | 15/15 | 3.6e-11 | 9.6e-10 | 6.1e-15 | 3.6e-15 | 1.6e-11 | 8.2e-07 | 8.1e-09 | **8.2e-07** | - |
| buckle-v8 | 12 | 162 | 12 | 12/12 | 4.3e-11 | 9.6e-10 | 5.8e-15 | 3.6e-15 | 1.6e-11 | 8.2e-07 | 8.1e-09 | **8.2e-07** | - |
| buckle-v9-back | 1 | 29 | 6 | 1/1 | 4.0e-16 | 5.9e-16 | 0.0e+00 | 1.8e-15 | 1.6e-14 | 1.7e-07 | 1.5e-14 | **1.7e-07** | - |
| buckle-v9 | 10 | 153 | 14 | 10/10 | 6.5e-10 | 1.0e-09 | 1.1e-15 | 3.6e-15 | 1.6e-11 | 8.1e-07 | 8.1e-09 | **8.1e-07** | - |
| NAKTO-BRAKE-BRACKET | 2 | 20 | 4 | 2/2 | 3.9e-16 | 2.7e-15 | 2.2e-15 | 3.6e-15 | 1.4e-14 | 5.6e-07 | 1.4e-14 | **5.6e-07** | - |
| test-cube-10mm | 1 | 7 | 1 | 1/1 | 0.0e+00 | 3.8e-16 | 0.0e+00 | 5.0e-16 | 5.0e-16 | 1.4e-07 | 0.0e+00 | **1.4e-07** | - |
| thread-test | 1 | 8 | 2 | 1/1 | 7.1e-16 | 2.7e-15 | 0.0e+00 | 3.6e-15 | 5.3e-15 | 3.3e-07 | 0.0e+00 | **3.3e-07** | - |
| tramming-rod | 1 | 10 | 0 | 1/1 | 9.9e-16 | 6.9e-16 | 3.6e-15 | 7.1e-15 | 7.1e-15 | 2.7e-15 | 4.0e-15 | **7.1e-15** | - |

**Overall: 108/108 solids built, 108 valid, 108 pass the boolean argument check, 0 free edges.
The maximum deviation of every kind is ≤ 2.8e-6 mm, so nothing is above 0.001 mm and nothing is flagged.**

Where the small non-zero values come from:
- 1e-14 .. 1e-15 mm: floating-point round-off.
- 8e-9 .. 9e-8 mm (buckle-v10/v11/v12/v6..v9): Parasolid's own data is exact only to its 1e-5 mm resolution. The
  tilted line in buckle-v12 sld_401 sits 9.2e-8 mm off its plane at its far end, and some vertices sit 8e-9 mm off
  their curves.
- 1e-7 .. 2.8e-6 mm (OCCT → XT column): the 0.05° sampling of the XT boundary used for that distance (section 3.4c).

### Per solid

All 108 rows, mm. "faces" = XT faces (the same in OCCT). "XT gaps" = Parasolid's own vertex-to-curve and
curve-to-surface gaps. "xt_mesh ΔA rel" is the viewer mesher's area against OCCT's (section 6).

| part | solid | faces | OCCT edges (incl. seams) | seams | valid | bool check | volume mm³ (OCCT) | ΔV/area | max face ΔA/perim | bbox Δ | XT→OCCT face | XT→OCCT edge | OCCT→XT | XT gaps | xt_mesh vtx | xt_mesh ΔA rel |
|---|---|--:|--:|--:|---|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| A001 | sld_7 | 11 | 24 | 0 | yes | yes | 235896.8236 | 6.7e-16 | 1.1e-15 | 0.0e+00 | 0.0e+00 | 1.8e-15 | 0.0e+00 | 1.8e-15 | 2.8e-06 | +2.6e-08 |
| bracket4-dashed | sld_46 | 34 | 82 | 8 | yes | yes | 10717.1626 | 3.7e-16 | 2.7e-15 | 2.2e-16 | 7.2e-15 | 1.3e-14 | 4.9e-07 | 1.3e-14 | 1.8e-06 | -9.7e-05 |
| BRACKET4 | sld_46 | 34 | 82 | 8 | yes | yes | 10717.1626 | 3.7e-16 | 2.7e-15 | 2.2e-16 | 7.2e-15 | 1.3e-14 | 4.9e-07 | 1.3e-14 | 1.8e-06 | -9.7e-05 |
| buckle1 | sld_178 | 3 | 3 | 1 | yes | yes | 25.6590 | 7.4e-17 | 8.6e-16 | 0.0e+00 | 1.8e-15 | 0.0e+00 | 1.5e-07 | 0.0e+00 | 9.1e-07 | -6.5e-04 |
| buckle1 | sld_175 | 3 | 3 | 1 | yes | yes | 3.1416 | 3.5e-17 | 2.8e-16 | 0.0e+00 | 0.0e+00 | 0.0e+00 | 9.3e-08 | 0.0e+00 | 1.3e-06 | -7.9e-04 |
| buckle1 | sld_174 | 3 | 3 | 1 | yes | yes | 3.1416 | 1.4e-16 | 2.8e-16 | 0.0e+00 | 8.9e-16 | 8.9e-16 | 9.3e-08 | 0.0e+00 | 9.0e-07 | -7.9e-04 |
| buckle1 | sld_150 | 3 | 3 | 1 | yes | yes | 28.8634 | 5.4e-16 | 3.2e-16 | 0.0e+00 | 3.6e-15 | 3.6e-15 | 1.6e-07 | 0.0e+00 | 3.5e-06 | -6.7e-04 |
| buckle1 | sld_149 | 3 | 3 | 1 | yes | yes | 28.8634 | 4.8e-16 | 3.2e-16 | 0.0e+00 | 3.6e-15 | 3.6e-15 | 1.6e-07 | 0.0e+00 | 3.5e-06 | -6.7e-04 |
| buckle1 | sld_145 | 10 | 24 | 0 | yes | yes | 1758.0000 | 9.4e-16 | 1.2e-15 | 3.6e-15 | 3.6e-15 | 1.6e-14 | 1.0e-14 | 1.0e-14 | 3.5e-06 | -1.5e-08 |
| buckle1 | sld_131 | 24 | 66 | 0 | yes | yes | 500.1458 | 2.4e-15 | 3.9e-15 | 0.0e+00 | 3.6e-15 | 3.0e-14 | 2.7e-14 | 3.0e-14 | 2.7e-06 | +9.8e-08 |
| buckle1 | sld_115 | 3 | 3 | 1 | yes | yes | 28.8634 | 1.4e-16 | 3.2e-16 | 0.0e+00 | 1.8e-15 | 0.0e+00 | 1.6e-07 | 0.0e+00 | 8.9e-07 | -6.7e-04 |
| buckle1 | sld_114 | 3 | 3 | 1 | yes | yes | 28.8634 | 1.4e-16 | 3.2e-16 | 0.0e+00 | 1.3e-15 | 8.9e-16 | 1.6e-07 | 0.0e+00 | 6.2e-07 | -6.7e-04 |
| buckle1 | sld_77 | 20 | 51 | 1 | yes | yes | 307.1690 | 1.1e-15 | 2.6e-15 | 3.6e-15 | 3.6e-15 | 1.5e-14 | 6.6e-07 | 7.9e-15 | 1.6e-06 | +7.4e-03 |
| buckle1 | sld_76 | 20 | 51 | 1 | yes | yes | 307.1690 | 1.2e-15 | 2.7e-15 | 7.1e-15 | 3.6e-15 | 1.1e-14 | 6.6e-07 | 7.9e-15 | 1.8e-06 | +7.2e-03 |
| buckle1 | sld_75 | 10 | 24 | 0 | yes | yes | 1758.0000 | 6.3e-16 | 3.7e-15 | 1.8e-15 | 8.9e-16 | 4.1e-15 | 3.6e-15 | 3.8e-15 | 2.5e-07 | -7.1e-09 |
| buckle-v10-back | sld_375 | 27 | 60 | 6 | yes | yes | 1636.0403 | 0.0e+00 | 1.9e-15 | 0.0e+00 | 1.8e-15 | 1.6e-14 | 1.9e-07 | 1.6e-14 | 8.8e-07 | +6.4e-04 |
| buckle-v10 | sld_374 | 37 | 105 | 0 | yes | yes | 932.9437 | 4.5e-16 | 4.0e-15 | 0.0e+00 | 3.6e-15 | 1.4e-14 | 2.9e-07 | 1.4e-14 | 1.7e-06 | +4.8e-03 |
| buckle-v10 | sld_373 | 34 | 96 | 1 | yes | yes | 407.4766 | 8.6e-10 | 1.1e-09 | 3.6e-15 | 3.6e-15 | 5.2e-14 | 8.1e-07 | 8.1e-09 | 1.2e-06 | +9.2e-03 |
| buckle-v10 | sld_372 | 34 | 96 | 1 | yes | yes | 407.4766 | 1.3e-10 | 1.1e-09 | 0.0e+00 | 2.0e-15 | 4.7e-14 | 8.1e-07 | 8.1e-09 | 8.8e-07 | +9.2e-03 |
| buckle-v10 | sld_355 | 27 | 60 | 6 | yes | yes | 1636.0403 | 3.1e-16 | 1.3e-15 | 0.0e+00 | 7.1e-15 | 1.4e-14 | 1.9e-07 | 1.4e-14 | 3.7e-06 | +6.4e-04 |
| buckle-v10 | sld_342 | 3 | 3 | 1 | yes | yes | 39.3437 | 3.3e-16 | 5.1e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.5e-07 | 0.0e+00 | 3.6e-06 | -5.7e-04 |
| buckle-v10 | sld_339 | 3 | 3 | 1 | yes | yes | 44.2572 | 5.1e-16 | 3.2e-16 | 0.0e+00 | 3.6e-15 | 3.6e-15 | 1.6e-07 | 0.0e+00 | 3.7e-06 | -5.8e-04 |
| buckle-v10 | sld_338 | 3 | 3 | 1 | yes | yes | 32.5155 | 1.1e-15 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 3.6e-15 | 1.4e-07 | 0.0e+00 | 3.2e-06 | -5.5e-04 |
| buckle-v10 | sld_337 | 3 | 3 | 1 | yes | yes | 32.5155 | 6.2e-16 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.4e-07 | 0.0e+00 | 1.9e-06 | -5.5e-04 |
| buckle-v10 | sld_336 | 3 | 3 | 1 | yes | yes | 44.2572 | 5.1e-16 | 3.2e-16 | 0.0e+00 | 1.8e-15 | 1.8e-15 | 1.6e-07 | 0.0e+00 | 1.6e-06 | -5.8e-04 |
| buckle-v10 | sld_330 | 9 | 21 | 1 | yes | yes | 815.7269 | 2.6e-16 | 2.1e-15 | 7.1e-15 | 4.0e-15 | 1.6e-14 | 1.9e-07 | 1.4e-14 | 3.6e-06 | +1.0e-03 |
| buckle-v10 | sld_246 | 3 | 3 | 1 | yes | yes | 31.8086 | 5.7e-16 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.4e-07 | 0.0e+00 | 1.9e-06 | -5.6e-04 |
| buckle-v10 | sld_245 | 3 | 3 | 1 | yes | yes | 43.2951 | 5.2e-16 | 3.2e-16 | 0.0e+00 | 1.8e-15 | 1.8e-15 | 1.6e-07 | 0.0e+00 | 1.7e-06 | -5.8e-04 |
| buckle-v11 | sld_394 | 34 | 96 | 1 | yes | yes | 378.2661 | 7.1e-10 | 1.0e-09 | 0.0e+00 | 3.6e-15 | 4.3e-14 | 2.8e-06 | 8.1e-09 | 1.2e-06 | +1.2e-02 |
| buckle-v11 | sld_393 | 35 | 99 | 1 | yes | yes | 378.3339 | 1.1e-10 | 1.0e-09 | 0.0e+00 | 2.0e-15 | 4.5e-14 | 2.8e-06 | 8.1e-09 | 8.8e-07 | +1.1e-02 |
| buckle-v11 | sld_374 | 41 | 117 | 0 | yes | yes | 868.7324 | 1.9e-16 | 3.8e-15 | 0.0e+00 | 3.6e-15 | 1.4e-14 | 2.9e-07 | 1.4e-14 | 1.7e-06 | +8.6e-03 |
| buckle-v11 | sld_355 | 27 | 60 | 6 | yes | yes | 1636.0403 | 3.1e-16 | 1.3e-15 | 0.0e+00 | 7.1e-15 | 1.4e-14 | 1.9e-07 | 1.4e-14 | 3.7e-06 | +6.4e-04 |
| buckle-v11 | sld_342 | 3 | 3 | 1 | yes | yes | 39.3437 | 3.3e-16 | 5.1e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.5e-07 | 0.0e+00 | 3.6e-06 | -5.7e-04 |
| buckle-v11 | sld_339 | 3 | 3 | 1 | yes | yes | 44.2572 | 5.1e-16 | 3.2e-16 | 0.0e+00 | 3.6e-15 | 3.6e-15 | 1.6e-07 | 0.0e+00 | 3.7e-06 | -5.8e-04 |
| buckle-v11 | sld_338 | 3 | 3 | 1 | yes | yes | 32.5155 | 1.1e-15 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 3.6e-15 | 1.4e-07 | 0.0e+00 | 3.2e-06 | -5.5e-04 |
| buckle-v11 | sld_337 | 3 | 3 | 1 | yes | yes | 32.5155 | 6.2e-16 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.4e-07 | 0.0e+00 | 1.9e-06 | -5.5e-04 |
| buckle-v11 | sld_336 | 3 | 3 | 1 | yes | yes | 44.2572 | 5.1e-16 | 3.2e-16 | 0.0e+00 | 1.8e-15 | 1.8e-15 | 1.6e-07 | 0.0e+00 | 1.6e-06 | -5.8e-04 |
| buckle-v11 | sld_330 | 11 | 27 | 1 | yes | yes | 814.3269 | 0.0e+00 | 4.3e-15 | 3.6e-15 | 4.0e-15 | 1.8e-14 | 1.9e-07 | 1.8e-14 | 3.6e-06 | +1.0e-03 |
| buckle-v11 | sld_246 | 3 | 3 | 1 | yes | yes | 31.8086 | 5.7e-16 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.4e-07 | 0.0e+00 | 1.9e-06 | -5.6e-04 |
| buckle-v12 | sld_401 | 38 | 106 | 1 | yes | yes | 391.6041 | 2.5e-09 | 1.4e-08 | 0.0e+00 | 2.0e-15 | 9.2e-08 | 2.8e-06 | 9.2e-08 | 9.2e-07 | +1.0e-02 |
| buckle-v12 | sld_394 | 34 | 96 | 1 | yes | yes | 378.2661 | 7.1e-10 | 1.0e-09 | 0.0e+00 | 3.6e-15 | 4.3e-14 | 2.8e-06 | 8.1e-09 | 1.2e-06 | +1.2e-02 |
| buckle-v12 | sld_374 | 41 | 117 | 0 | yes | yes | 868.7324 | 1.9e-16 | 3.8e-15 | 0.0e+00 | 3.6e-15 | 1.4e-14 | 2.9e-07 | 1.4e-14 | 1.7e-06 | +8.6e-03 |
| buckle-v12 | sld_355 | 27 | 60 | 6 | yes | yes | 1636.0403 | 3.1e-16 | 1.3e-15 | 0.0e+00 | 7.1e-15 | 1.4e-14 | 1.9e-07 | 1.4e-14 | 3.7e-06 | +6.4e-04 |
| buckle-v12 | sld_342 | 3 | 3 | 1 | yes | yes | 39.3437 | 3.3e-16 | 5.1e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.5e-07 | 0.0e+00 | 3.6e-06 | -5.7e-04 |
| buckle-v12 | sld_339 | 3 | 3 | 1 | yes | yes | 44.2572 | 5.1e-16 | 3.2e-16 | 0.0e+00 | 3.6e-15 | 3.6e-15 | 1.6e-07 | 0.0e+00 | 3.7e-06 | -5.8e-04 |
| buckle-v12 | sld_338 | 3 | 3 | 1 | yes | yes | 32.5155 | 1.1e-15 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 3.6e-15 | 1.4e-07 | 0.0e+00 | 3.2e-06 | -5.5e-04 |
| buckle-v12 | sld_337 | 3 | 3 | 1 | yes | yes | 32.5155 | 6.2e-16 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.4e-07 | 0.0e+00 | 1.9e-06 | -5.5e-04 |
| buckle-v12 | sld_336 | 3 | 3 | 1 | yes | yes | 44.2572 | 5.1e-16 | 3.2e-16 | 0.0e+00 | 1.8e-15 | 1.8e-15 | 1.6e-07 | 0.0e+00 | 1.6e-06 | -5.8e-04 |
| buckle-v12 | sld_330 | 11 | 27 | 1 | yes | yes | 814.3269 | 0.0e+00 | 4.3e-15 | 3.6e-15 | 4.0e-15 | 1.8e-14 | 1.9e-07 | 1.8e-14 | 3.6e-06 | +1.0e-03 |
| buckle-v12 | sld_246 | 3 | 3 | 1 | yes | yes | 31.8086 | 5.7e-16 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.4e-07 | 0.0e+00 | 1.9e-06 | -5.6e-04 |
| buckle-v6 | sld_230 | 3 | 3 | 1 | yes | yes | 33.6739 | 3.7e-16 | 3.2e-16 | 0.0e+00 | 3.6e-15 | 3.6e-15 | 1.6e-07 | 0.0e+00 | 3.7e-06 | -6.3e-04 |
| buckle-v6 | sld_229 | 3 | 3 | 1 | yes | yes | 24.7400 | 9.8e-16 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 3.6e-15 | 1.4e-07 | 0.0e+00 | 3.2e-06 | -6.0e-04 |
| buckle-v6 | sld_228 | 3 | 3 | 1 | yes | yes | 34.2119 | 3.6e-16 | 5.1e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.5e-07 | 0.0e+00 | 3.6e-06 | -5.9e-04 |
| buckle-v6 | sld_227 | 3 | 3 | 1 | yes | yes | 24.7400 | 5.3e-16 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.4e-07 | 0.0e+00 | 1.9e-06 | -6.0e-04 |
| buckle-v6 | sld_226 | 3 | 3 | 1 | yes | yes | 33.6739 | 4.9e-16 | 3.2e-16 | 0.0e+00 | 1.8e-15 | 1.8e-15 | 1.6e-07 | 0.0e+00 | 1.6e-06 | -6.3e-04 |
| buckle-v6 | sld_218 | 6 | 12 | 0 | yes | yes | 23.6866 | 2.1e-14 | 4.0e-15 | 0.0e+00 | 3.6e-15 | 2.8e-14 | 2.1e-07 | 1.4e-14 | 3.6e-06 | +2.3e-01 |
| buckle-v6 | sld_217 | 6 | 12 | 0 | yes | yes | 23.6865 | 2.2e-14 | 2.5e-16 | 0.0e+00 | 3.6e-15 | 7.1e-15 | 2.1e-07 | 7.1e-15 | 1.8e-06 | +2.3e-01 |
| buckle-v6 | sld_213 | 17 | 45 | 0 | yes | yes | 1151.0000 | 5.0e-16 | 2.4e-15 | 6.1e-15 | 3.6e-15 | 1.4e-14 | 7.1e-15 | 1.4e-14 | 2.9e-06 | -7.4e-08 |
| buckle-v6 | sld_200 | 32 | 90 | 1 | yes | yes | 304.6175 | 3.6e-11 | 1.2e-10 | 3.6e-15 | 3.6e-15 | 1.6e-11 | 8.2e-07 | 1.6e-09 | 1.3e-06 | +1.3e-02 |
| buckle-v6 | sld_178 | 3 | 3 | 1 | yes | yes | 28.2743 | 6.2e-16 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.4e-07 | 0.0e+00 | 1.9e-06 | -5.8e-04 |
| buckle-v6 | sld_130 | 3 | 3 | 1 | yes | yes | 34.2119 | 3.6e-16 | 5.1e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.5e-07 | 0.0e+00 | 3.6e-06 | -5.9e-04 |
| buckle-v6 | sld_116 | 6 | 12 | 0 | yes | yes | 37.5000 | 6.7e-17 | 4.6e-16 | 0.0e+00 | 0.0e+00 | 0.0e+00 | 0.0e+00 | 0.0e+00 | 8.6e-07 | -2.2e-07 |
| buckle-v6 | sld_112 | 20 | 39 | 5 | yes | yes | 1713.1401 | 6.0e-16 | 1.2e-15 | 0.0e+00 | 3.6e-15 | 1.6e-14 | 1.7e-07 | 1.4e-14 | 3.7e-06 | +2.5e-01 |
| buckle-v6 | sld_110 | 34 | 96 | 0 | yes | yes | 687.5794 | 4.6e-16 | 9.5e-16 | 0.0e+00 | 3.6e-15 | 3.0e-14 | 2.9e-07 | 8.6e-14 | 1.7e-06 | +6.1e-03 |
| buckle-v6 | sld_108 | 32 | 90 | 1 | yes | yes | 304.6035 | 1.8e-11 | 9.6e-10 | 3.6e-15 | 8.9e-16 | 4.5e-14 | 8.1e-07 | 8.1e-09 | 8.7e-07 | +1.3e-02 |
| buckle-v7 | sld_213 | 17 | 45 | 0 | yes | yes | 1151.0000 | 5.0e-16 | 2.4e-15 | 6.1e-15 | 3.6e-15 | 1.4e-14 | 7.1e-15 | 1.4e-14 | 2.9e-06 | -7.4e-08 |
| buckle-v7 | sld_230 | 3 | 3 | 1 | yes | yes | 33.6739 | 3.7e-16 | 3.2e-16 | 0.0e+00 | 3.6e-15 | 3.6e-15 | 1.6e-07 | 0.0e+00 | 3.7e-06 | -6.3e-04 |
| buckle-v7 | sld_229 | 3 | 3 | 1 | yes | yes | 24.7400 | 9.8e-16 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 3.6e-15 | 1.4e-07 | 0.0e+00 | 3.2e-06 | -6.0e-04 |
| buckle-v7 | sld_228 | 3 | 3 | 1 | yes | yes | 34.2119 | 3.6e-16 | 5.1e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.5e-07 | 0.0e+00 | 3.6e-06 | -5.9e-04 |
| buckle-v7 | sld_227 | 3 | 3 | 1 | yes | yes | 24.7400 | 5.3e-16 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.4e-07 | 0.0e+00 | 1.9e-06 | -6.0e-04 |
| buckle-v7 | sld_226 | 3 | 3 | 1 | yes | yes | 33.6739 | 4.9e-16 | 3.2e-16 | 0.0e+00 | 1.8e-15 | 1.8e-15 | 1.6e-07 | 0.0e+00 | 1.6e-06 | -6.3e-04 |
| buckle-v7 | sld_218 | 6 | 12 | 0 | yes | yes | 23.6866 | 2.1e-14 | 4.0e-15 | 0.0e+00 | 3.6e-15 | 2.8e-14 | 2.1e-07 | 1.4e-14 | 3.6e-06 | +2.3e-01 |
| buckle-v7 | sld_217 | 6 | 12 | 0 | yes | yes | 23.6865 | 2.2e-14 | 2.5e-16 | 0.0e+00 | 3.6e-15 | 7.1e-15 | 2.1e-07 | 7.1e-15 | 1.8e-06 | +2.3e-01 |
| buckle-v7 | sld_200 | 32 | 90 | 1 | yes | yes | 304.6175 | 3.6e-11 | 1.2e-10 | 3.6e-15 | 3.6e-15 | 1.6e-11 | 8.2e-07 | 1.6e-09 | 1.3e-06 | +1.3e-02 |
| buckle-v7 | sld_178 | 3 | 3 | 1 | yes | yes | 28.2743 | 6.2e-16 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.4e-07 | 0.0e+00 | 1.9e-06 | -5.8e-04 |
| buckle-v7 | sld_130 | 3 | 3 | 1 | yes | yes | 34.2119 | 3.6e-16 | 5.1e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.5e-07 | 0.0e+00 | 3.6e-06 | -5.9e-04 |
| buckle-v7 | sld_116 | 6 | 12 | 0 | yes | yes | 18.7500 | 0.0e+00 | 4.6e-16 | 0.0e+00 | 0.0e+00 | 0.0e+00 | 0.0e+00 | 0.0e+00 | 8.6e-07 | -2.8e-07 |
| buckle-v7 | sld_112 | 20 | 39 | 5 | yes | yes | 1681.8687 | 7.4e-16 | 1.2e-15 | 0.0e+00 | 3.6e-15 | 1.6e-14 | 1.7e-07 | 1.4e-14 | 3.7e-06 | +2.4e-01 |
| buckle-v7 | sld_110 | 34 | 96 | 0 | yes | yes | 687.5794 | 4.6e-16 | 9.5e-16 | 0.0e+00 | 3.6e-15 | 3.0e-14 | 2.9e-07 | 8.6e-14 | 1.7e-06 | +6.1e-03 |
| buckle-v7 | sld_108 | 32 | 90 | 1 | yes | yes | 304.6035 | 1.8e-11 | 9.6e-10 | 3.6e-15 | 8.9e-16 | 4.5e-14 | 8.1e-07 | 8.1e-09 | 8.7e-07 | +1.3e-02 |
| buckle-v8 | sld_254 | 6 | 12 | 0 | yes | yes | 168.7500 | 2.7e-16 | 4.6e-16 | 0.0e+00 | 0.0e+00 | 4.4e-16 | 4.4e-16 | 4.4e-16 | 8.6e-07 | -1.3e-07 |
| buckle-v8 | sld_250 | 3 | 3 | 1 | yes | yes | 38.4884 | 3.3e-16 | 5.1e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.5e-07 | 0.0e+00 | 3.6e-06 | -5.7e-04 |
| buckle-v8 | sld_248 | 3 | 3 | 1 | yes | yes | 43.2951 | 5.2e-16 | 3.2e-16 | 0.0e+00 | 3.6e-15 | 3.6e-15 | 1.6e-07 | 0.0e+00 | 3.7e-06 | -5.8e-04 |
| buckle-v8 | sld_247 | 3 | 3 | 1 | yes | yes | 31.8086 | 1.1e-15 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 3.6e-15 | 1.4e-07 | 0.0e+00 | 3.2e-06 | -5.6e-04 |
| buckle-v8 | sld_246 | 3 | 3 | 1 | yes | yes | 31.8086 | 5.7e-16 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.4e-07 | 0.0e+00 | 1.9e-06 | -5.6e-04 |
| buckle-v8 | sld_245 | 3 | 3 | 1 | yes | yes | 43.2951 | 5.2e-16 | 3.2e-16 | 0.0e+00 | 1.8e-15 | 1.8e-15 | 1.6e-07 | 0.0e+00 | 1.7e-06 | -5.8e-04 |
| buckle-v8 | sld_239 | 17 | 45 | 0 | yes | yes | 837.0000 | 2.6e-16 | 1.9e-15 | 5.8e-15 | 3.6e-15 | 1.5e-14 | 9.0e-15 | 1.5e-14 | 2.9e-06 | -3.9e-08 |
| buckle-v8 | sld_200 | 32 | 90 | 1 | yes | yes | 426.4645 | 4.3e-11 | 1.2e-10 | 3.6e-15 | 3.6e-15 | 1.6e-11 | 8.2e-07 | 1.6e-09 | 1.3e-06 | +8.4e-03 |
| buckle-v8 | sld_116 | 6 | 12 | 0 | yes | yes | 18.7500 | 0.0e+00 | 4.6e-16 | 0.0e+00 | 0.0e+00 | 0.0e+00 | 0.0e+00 | 0.0e+00 | 8.6e-07 | -2.8e-07 |
| buckle-v8 | sld_112 | 20 | 39 | 5 | yes | yes | 1713.1401 | 6.0e-16 | 1.2e-15 | 0.0e+00 | 3.6e-15 | 1.6e-14 | 1.7e-07 | 1.4e-14 | 3.7e-06 | +2.5e-01 |
| buckle-v8 | sld_110 | 34 | 96 | 0 | yes | yes | 962.6111 | 3.9e-16 | 9.5e-16 | 0.0e+00 | 3.6e-15 | 3.0e-14 | 2.9e-07 | 8.6e-14 | 1.7e-06 | +3.9e-03 |
| buckle-v8 | sld_108 | 32 | 90 | 1 | yes | yes | 426.4449 | 2.2e-11 | 9.6e-10 | 3.6e-15 | 8.9e-16 | 4.5e-14 | 8.1e-07 | 8.1e-09 | 8.6e-07 | +8.4e-03 |
| buckle-v9-back | sld_322 | 29 | 64 | 6 | yes | yes | 2175.1335 | 4.0e-16 | 5.9e-16 | 0.0e+00 | 1.8e-15 | 1.6e-14 | 1.7e-07 | 1.5e-14 | 8.1e-07 | -4.8e-06 |
| buckle-v9 | sld_321 | 29 | 64 | 6 | yes | yes | 2197.4702 | 0.0e+00 | 2.7e-15 | 0.0e+00 | 3.6e-15 | 1.6e-14 | 1.7e-07 | 1.6e-14 | 3.7e-06 | -1.2e-06 |
| buckle-v9 | sld_297 | 35 | 99 | 0 | yes | yes | 957.9234 | 9.1e-17 | 2.5e-15 | 0.0e+00 | 3.6e-15 | 1.8e-14 | 2.9e-07 | 7.9e-15 | 2.0e-06 | +3.7e-03 |
| buckle-v9 | sld_287 | 31 | 87 | 1 | yes | yes | 426.1110 | 9.2e-11 | 9.7e-10 | 0.0e+00 | 2.0e-15 | 4.1e-14 | 8.1e-07 | 8.1e-09 | 8.8e-07 | +8.4e-03 |
| buckle-v9 | sld_282 | 32 | 90 | 1 | yes | yes | 426.4466 | 6.5e-10 | 1.0e-09 | 1.1e-15 | 1.8e-15 | 1.6e-11 | 8.1e-07 | 8.1e-09 | 9.8e-07 | +8.4e-03 |
| buckle-v9 | sld_250 | 3 | 3 | 1 | yes | yes | 38.4884 | 3.3e-16 | 5.1e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.5e-07 | 0.0e+00 | 3.6e-06 | -5.7e-04 |
| buckle-v9 | sld_248 | 3 | 3 | 1 | yes | yes | 43.2951 | 5.2e-16 | 3.2e-16 | 0.0e+00 | 3.6e-15 | 3.6e-15 | 1.6e-07 | 0.0e+00 | 3.7e-06 | -5.8e-04 |
| buckle-v9 | sld_247 | 3 | 3 | 1 | yes | yes | 31.8086 | 1.1e-15 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 3.6e-15 | 1.4e-07 | 0.0e+00 | 3.2e-06 | -5.6e-04 |
| buckle-v9 | sld_246 | 3 | 3 | 1 | yes | yes | 31.8086 | 5.7e-16 | 1.9e-16 | 0.0e+00 | 3.6e-15 | 0.0e+00 | 1.4e-07 | 0.0e+00 | 1.9e-06 | -5.6e-04 |
| buckle-v9 | sld_245 | 3 | 3 | 1 | yes | yes | 43.2951 | 5.2e-16 | 3.2e-16 | 0.0e+00 | 1.8e-15 | 1.8e-15 | 1.6e-07 | 0.0e+00 | 1.7e-06 | -5.8e-04 |
| buckle-v9 | sld_239 | 11 | 27 | 1 | yes | yes | 864.5683 | 3.5e-16 | 1.1e-15 | 2.8e-17 | 3.6e-15 | 1.4e-14 | 1.6e-07 | 7.1e-15 | 3.5e-06 | +1.5e-05 |
| NAKTO-BRAKE-BRACKET | sld_22 | 4 | 6 | 2 | yes | yes | 1215.0110 | 0.0e+00 | 2.7e-15 | 0.0e+00 | 3.6e-15 | 1.8e-15 | 5.6e-07 | 0.0e+00 | 1.8e-06 | -5.2e-04 |
| NAKTO-BRAKE-BRACKET | sld_16 | 16 | 42 | 2 | yes | yes | 7217.9066 | 3.9e-16 | 1.8e-15 | 2.2e-15 | 3.6e-15 | 1.4e-14 | 2.3e-07 | 1.4e-14 | 1.8e-06 | +1.3e-05 |
| test-cube-10mm | sld_378 | 7 | 15 | 1 | yes | yes | 557.5885 | 0.0e+00 | 3.8e-16 | 0.0e+00 | 5.0e-16 | 5.0e-16 | 1.4e-07 | 0.0e+00 | 1.2e-07 | -3.5e-08 |
| thread-test | sld_114 | 8 | 18 | 2 | yes | yes | 36526.6903 | 7.1e-16 | 2.7e-15 | 0.0e+00 | 3.6e-15 | 5.3e-15 | 3.3e-07 | 0.0e+00 | 3.7e-06 | +2.7e-06 |
| tramming-rod | sld_50 | 10 | 24 | 0 | yes | yes | 77012.8000 | 9.9e-16 | 6.9e-16 | 3.6e-15 | 7.1e-15 | 7.1e-15 | 2.7e-15 | 4.0e-15 | 3.0e-06 | +1.7e-08 |

## 5. Is the check sharp enough? (mutation test, `make mutate`)

The translator was deliberately broken with test hooks (`XTOCC_MUTATE_*`, compiled out by default) and rerun on all
108 solids:

| mutant | effect | result |
|---|---|---|
| CIRCLE_SENSE: ignore CIRCLE.sense '-' | ring edges on the 6 solids with '-' circles turn the wrong way | all 6 refused ("wrapping loops do not bound a band"); the 102 solids without '-' circles are unchanged and correctly pass |
| SURFACE_SENSE: ignore CYLINDER.sense '-' | hole cylinders face the wrong way | the same 6 solids refused (the '-' cylinders are the same holes) |
| RADIUS: every cylinder +0.002 mm | geometry off by 0.002 mm | 96 refused at build (edge leaves its face by 0.002 > 1e-5); the 12 solids without cylinders pass |
| RADIUS + loose tolerance (0.01 mm) so the build goes through | the same error reaches the metrics | flagged DEV_FACE (0.002 mm) + FACE_AREA (1.6e-3 .. 5.2e-3 mm); BRepCheck still calls these solids *valid* |

So a 0.002 mm geometric error is caught twice: by the translator's tolerance guard and by the deviation metrics. A
wrong sense rule is refused outright. The last row shows that BRepCheck alone would not have caught it.

Limitation: check (c), from the OCCT triangulation, did not see the radius mutant. On short hole cylinders BRepMesh
puts nodes only on the boundary circles, which are the unchanged XT edges, so (c) mainly tests trimming. Check (a),
from points on the XT surface, catches surface errors.

## 6. Cross-check with the viewer's mesher (viewer/xt_mesh.c), finding

- **Its vertices are correct.** Every sampled xt_mesh vertex lies within 3.7e-6 mm of the OCCT faces, which is the
  limit of its single-precision floats.
- **Its area agrees within its 5° chord error** (≤ 1.3e-3 relative) on most solids, but not on 30 of them (|ΔA| > 2e-3).
  OCCT and the exact XT values agree with each other to 1e-9 there, so the error is in the mesher's triangulation.
  It is a display-mesh issue, not a geometry one:
  - buckle-v6/v7/v8 `sld_112`: plane z = -3 is meshed with 917.7 mm² against an exact 541.3 mm². This points to the
    hole bridging / ear clipping covering holes.
  - buckle-v6/v7 `sld_217` / `sld_218`: +23 % area on cylinder faces (no plane disagrees), so partial cylinder faces
    are over-covered.
  - buckle1 `sld_77`: a 1.26 mm² planar face (normal (-0.776, 0.631, 0)) is missing from the mesh. Several buckle
    solids also miss sliver planar faces of 0.007 .. 0.01 mm² (these may partly be the per-plane grouping of the
    check).
  - Its triangles are not wound consistently outward (36 solids have triangles wound against their stored normal),
    so it cannot give a volume unless each triangle is oriented by its normal (done in the check).

  Not fixed: viewer/xt_mesh.c is outside this task. Once OCCT is integrated, `BRepMesh` on the translated shape could
  replace it for display.

## 7. What this does not cover yet

- Entity types that do not occur in the user's parts (section 1): they are refused, not approximated.
- XT attributes (colours, Mastercam face ids) are not carried over to OCCT.
- The way back, OCCT -> XT (writing OCCT-made solids into .mcam), is not written. It is needed if OCCT-made solids have
  to open in real Mastercam (see the questions in formats/occt_3d_plan.md).
- Bodies with several solid regions (lumps) or inner void shells are handled by the code, but none occur in the
  user's parts, so they are untested.

## 8. Reproduce

```
cd viewer/occt && make && make check     # -> ~/mastercam-re/work/occt/accuracy.tsv
make mutate                               # -> ~/mastercam-re/work/occt/mutant_*.tsv
~/mastercam-re/work/occt/xt_occt_check ~/mastercam-re/files/sch_26105.s_t ~/mastercam-re/work/occt/xb/manifest.tsv -v   # with notes
```
