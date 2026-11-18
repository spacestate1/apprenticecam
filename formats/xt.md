# Parasolid XT inside Mastercam part files (reader: `tools/xt_parse.py`, `viewer/xt_mesh.c`)

Solids are SOLIDXMT records (class 0x20000): +0x49 version, +0x68 name (`sld_N`), +0x168 u32 size. The XT bytes
follow the record. They are a Parasolid **binary transmit** file (`FORMAT=binary`, `GUISE=transmit`), in **metres**.

Node layouts come from Parasolid's schema files, which Mastercam installs (`sch_<key>.s_t`; mastercam-2017 `files/`,
or `mcamX9/common/PSLDDATA`). The header line `SCH=SCH_2700142_26105` = written by Parasolid 27.0.142 using base
schema **26105**.

Schema text: node line `code NAME; description; xmt nfields variable`, then one line per field:
`name; type; xmt ptr_class n`. Only fields with xmt = 1 are written. A variable-length node repeats its last field.

## Binary body (after `**END_OF_HEADER***…\n`)
```
'B'  u32 len ": TRANSMIT FILE created by modeller version 2700142"  u32 len "SCH_2700142_26105"  u32 USFLD (1)
node*: u16 type, [u32 count if variable], u16 index, fields, USFLD x int32 user field
u16 1 = end
```
Field encodings: `d` int32, `f` double, `p` u16 node index (1 = null), `c`/`l`/`u` 1 byte, `n` int16, `v` 3 doubles,
`b` 6 doubles, `h` 4 doubles, `i` 2 doubles, `t` int32. All little-endian.

Validation:
- **118 of 127 solids** across the user's parts and Mastercam's samples parse to the exact end marker (every solid in
  the user's own files).
- The 9 failures are in Mastercam's Mill/MillTurn samples: node types or encodings not handled yet.

## Topology used for meshing
`FACE(loop, surface, sense, next)` → `LOOP(halfedge, next)` → `HALFEDGE(forward, backward, vertex, edge, sense)`
→ `EDGE(curve)`; `VERTEX(point)` → `POINT(pvec)`.

- **A fin's vertex is at its forward end.** The fin runs from the previous fin's vertex to its own. Verified: all 208
  arc endpoints lie on their circles; the opposite assumption put 184/200 off.
- Surfaces: PLANE(pvec, normal, x_axis), CYLINDER(pvec, axis, radius, x_axis). Curves: LINE(pvec, direction),
  CIRCLE(centre, normal, x_axis, radius). Ring edges (no vertex) are full circles.
- The user's parts use only plane/cylinder/line/circle. All faces mesh: BRACKET4 34/34, buckle-v10 162/162, NAKTO 20/20.

Mesher (`viewer/xt_mesh.c`):
- planes are projected to 2D, holes are bridged in, then ear-clipped;
- cylinders are unwrapped to (θ, h): a band between two full rings becomes a quad strip, partial faces are
  triangulated in parameter space;
- circles are sampled every 5°.

Units are chosen by matching the part's geometry and toolpath extents: ×1000 (mm) or ×39.37 (inch).

Not yet: cones, spheres, tori, B-surfaces / NURBS, offset and blend surfaces; the 9 failing sample solids.

## Edges (2026-10-21, the AutoCursor on the part's solids: `viewer/xt_mesh.c xt_brep_edges`)
Records read (schema 26105 field names): `EDGE(curve, halfedge)`, `HALFEDGE(vertex, other, sense, curve)`,
`VERTEX(point)`, `POINT(pvec)`, `LINE(pvec, direction)`, `CIRCLE(centre, normal, x_axis, radius, sense)`.
- An edge runs along its fin `EDGE.halfedge` from the partner fin's (`other`) vertex to the fin's own vertex (a fin's
  vertex is at its forward end, as above); `HALFEDGE.sense` '-' = the fin runs against its curve.
- LINE edge: the two vertex points. CIRCLE edge: the circle's parameter t runs from `x_axis` about `normal`
  (P(t) = centre + r (cos t x_axis + sin t (normal x x_axis))); the arc covered is from the start to the end vertex
  in the fin's sense; t0 = its start restricted to [0, 2 pi), t1 = t0 + the arc's angle (the same rule as the edges
  of bodies made here, formats/extrude.md 4.9). A **ring edge** (no vertex: both fins' vertex pointers null) is a
  full circle, 0 .. 2 pi. Display points every <= 5 deg (the mesher's sampling) for the hit tests.
- Seen in the user's parts: BRACKET4 74 edges = 56 LINE + 18 CIRCLE (10 ring edges), NAKTO-BRAKE-BRACKET 44 edges
  (2 solids) = 36 LINE + 8 CIRCLE (rings); every circle has sense '+'; most have normal +Z / x_axis +X, BRACKET4's
  two hole bottoms at z -13 have normal -Z / x_axis -X.
- **Left out** (counted, `ac_part_skipped`): edges on other curve classes (ELLIPSE, B-curves, intersection / SP
  curves - none in the user's parts), CIRCLE nodes with sense '-' (how the parameter runs then is NOT DECODED),
  edges whose fins / vertices are missing on one side only.
- Units: x `solid_scale` (scene.c mesh_solids: 1000 for mm, 39.37 for inch by the part's extents), so the edges
  coincide with the displayed mesh. IsPosVisible for every solid candidate now also tests the part's display mesh
  (core_ui cui_snap.c `part_hit`, the nearest triangle along the view ray).
