# XT -> OCCT translator: binary 'h' fields, cones / spheres / tori / offsets / rolling-ball blends / intersection curves (2026-10-26)

Code: `viewer/occt/xt_occt.cpp` / `xt_occt.h` (the translator the 3D toolpaths mesh their drive solids with, through
`viewer/occt_mesh.cpp`; formats/hst3d_port_plan.md 6), `viewer/xt_mesh.c`, `viewer/occt/occt_xt.cpp`, `tools/xt_parse.py` (the 'h' size).
Tests (ours): `tools/tests/xt_occt_acc.cpp` (accuracy report of one solid / part), `tools/tests/xt_occt_scan.sh` (+ `.c`: every solid of a
corpus through the app's drive-mesh path). Trigger: formats/horizontal_area.md 9.4 (the Horizontal Area oracle part, 2017 Handbook Vol 2
`RASTER.mcam`, could not be meshed); the scratch translator of that session (`decomp/horizarea/x2s/`, approximate blends, dropped faces) is
replaced by this.

Sources, cited in the code as "ref p.N": the **Parasolid XT Format Reference** (UGS, Oct 2006, printed page numbers;
`~/mastercam-re/work/occt/ref/Parasolid-XT-format-reference-2006.pdf` / `.txt`, see formats/occt_xt.md), the schema embedded with Mastercam
(`~/mastercam-re/files/sch_28002.s_t` for RASTER.mcam), `viewer/occt/xt_refgeom.h` (the reference formulas + the measured cone sign), and
measurements on the stored data (labelled "measured"). "Ours" = an OCCT representation choice. Rule 1: nothing else.

## 1. 'h' fields are 24 bytes in a binary transmit file

The schema types `hvec` fields (CHART.hvec, LIMIT.hvec, SP_CHART, HVEC_ARRAY) as `h`. The readers took 32 bytes (4 doubles). The reference
defines an hvec as pvec + u[2] + v[2] + tangent + t, and says (ref p.44) **"Note that only the pvec part of an hvec is actually
transmitted"**: 3 doubles = 24 bytes. Measured: with 32 the parse of RASTER.mcam's solid stops at 0xcdac ("unsupported node type 16305",
the first CHART); with 24 all 12403 nodes parse and the blob is consumed to its end marker (540876 / 540878 bytes).
Fixed in `viewer/occt/xt_occt.cpp` tsize, `viewer/xt_mesh.c` tsize (the C reader: solids with charts now parse there too),
`tools/xt_parse.py` (SIZES + Reader.field; `tools/mcam_walk.py` itself does not parse XT nodes - its `--extract-xt` was fine, the failure
was xt_parse.py), and the writer `viewer/occt/occt_xt.cpp` (doubles per 'h' element 3; it writes no hvec today).

## 2. Surfaces

| XT | OCCT | rule |
|---|---|---|
| CONE | `Geom_ConicalSurface(gp_Ax3(pvec, axis, x_axis), atan2(sin_half_angle, cos_half_angle), radius)` | R = P + vA + (X cos u + Y sin u)(r + v tan a), Y = A x X: ref p.55 field list; the sign "+ v A" is the measured one in `xt_refgeom.h` (ref p.55 prints "- v A"). Same u, OCCT v = XT v / cos a, same natural normal. Exact. Half angle 0 / >= 90 deg or radius < 0: refused |
| SPHERE | `Geom_SphericalSurface(gp_Ax3(centre, axis, x_axis), radius)` | ref p.56-57, the same parametrisation. Exact |
| TORUS | `Geom_ToroidalSurface(gp_Ax3(centre, axis, x_axis), major, minor)` | ref p.58-59, the same parametrisation; doughnut and apple (major <= minor, OCCT 7.9 accepts) exact; lemon (major < 0): refused |
| OFFSET_SURF | `Geom_OffsetSurface(basis, d)` | ref p.63: the underlying surface offset "along its normal, taking into account the surface sense"; d = offset x (-1 when the **underlying** surface's sense is '-'). Measured on RASTER.mcam (16 offsets of '-' B-surfaces, all spine-defining surfaces of blends): this sign puts the stored spine charts on the offset (max 2 um) and is the blend rule of ref p.61 (offset = natural normal x range x -1 for a '-' sense; the blends' own `range` and the offsets agree); the natural-normal sign leaves 6 blends / 5 curves unbuildable (fit deviations 1.6 mm) - `XTOCC_OFFSET_NATURAL=1` reproduces that. Same parametrisation as the basis (ref p.63) |
| BLENDED_EDGE | cubic B-spline interpolating the exact definition (3) | approximation, deviation checked (5) |

## 3. Rolling-ball blends (BLENDED_EDGE, ref p.59-62)

Definition used (ref p.59-61): R(u, v) = C(u) + r X(u) cos(v a(u)) + r Y(u) sin(v a(u)); C = the spine, r = |range|; the spine "will be the
intersection of the two surfaces obtained by offsetting the supporting surfaces by ... range[]", the offset vector "the natural unit
surface normal, times the range, times -1 if the sense is negative"; v = 0 on the boundary on surface[0], 1 on surface[1], "unless the sense
of the spine curve is negative, in which case it is the other way round"; cliff edge ('E', ref p.61): one support is a zero-range blend whose
spine is the cliff edge curve.
- **Boundary point on support k** for a ball centre c: the support point F with c = F + so_k n(F), so_k = range[k] x (-1 if the support's
  sense is '-'), n = its unit natural normal. Taken as the nearest point when it satisfies that, else the best of all the distance extrema
  (`Ctx::foot_k`; a ball can wrap a support thinner than itself - RASTER.mcam blend 4812 on a 0.508 mm torus, where the nearest point is
  0.25 mm away and the contact 1.27 mm). Cliff edge: the nearest point of the edge curve.
- **Section**: the arc of radius r about C(u) in its normal plane from the surface[0] boundary to the surface[1] one (swapped for a '-' spine),
  angle proportional to v; beyond [0, 1] the same circle continues (needed: a blend that defines another blend's spine is met by it
  outside its own boundaries). Where the boundaries meet (a terminator, a(u) = 0) the section is a point.
- **u / v intervals (ours)**: the stored points that must lie on the blend - its faces' vertices and analytic edges (sampled along the arc each
  edge takes), the charts / limits of every INTERSECTION with the blend (or its BLEND_BOUND) as a side, the LIMITs of the blend itself, and,
  for a blend that supports other blends, their ball centres (exact for this purpose: the centre lies in the support's section plane on the
  contact's radius, so it projects to the contact's u and v) - projected on the spine; + 5 % and 0.1 r, kept only where the ball touches
  both supports at |range| (a bounded support such as a B-surface may end first); a periodic spine: the arc complementary to the largest
  gap, or the whole period when a face of the blend has a ring loop.
- **Natural normal (measured)**: XT's is the opposite of du x dv of the construction above (u along the spine's parameter, v from the
  surface[0] boundary): the 13 blends of RASTER.mcam that support other blends put those blends' spines on their offsets (ref p.61) only with
  the reversed normal (13 / 13, ELLIPSE and INTERSECTION spines of both senses; the other sign misses by 2 x range). The fitted surface is
  therefore `VReverse`d; face senses then come out consistent (solid volume positive, 33.03 in^3).
- **Representation (ours)**: OCCT has no rolling-ball surface. A cubic B-spline (`GeomAPI_PointsToBSplineSurface::Interpolate`,
  iso-parametric) through Nu x Nv exact section points, Nu / Nv doubled until the exact points at all cell centres (u) and v-midpoints
  are within fit_tol = 1e-7 m of it (distance, `ShapeAnalysis_Surface`); when doubling stops helping (the spine's own noise near a
  terminator) the best round is kept; a blend whose best fit exceeds 1e-5 m is refused.
- Not mapped: blend types other than R / E, unequal |range| (variable / chamfer), blends with no stored point where the ball touches both
  supports - refused by name (`Result::unmapped`).

## 4. Curves

- **INTERSECTION** (ref p.42-46): exact = the branch of surface[0] x surface[1] through the chart; the chart is only "a piecewise-linear
  (chordal) approximation" (chordal_error up to 0.3 mm in RASTER.mcam), its hvecs lie on the curve. Ours: a C2 B-spline
  (`GeomAPI_Interpolate`, chord-length parameters) through the stored points (the 'T' limit's singular point hvec[0] first / last, ref
  p.45; a closed 'H' curve periodic), plus points on the exact intersection: Newton on the two tangent planes (`Ctx::refine`); a
  BLEND_BOUND side (ref p.62-63: the support blend.surface[1 - boundary], RASTER.mcam: 219 / 219 BLEND_BOUND sides pair with exactly that
  surface) maps a point to the blend's boundary on that support above its spine point (exact up to the spine). Refined at the quarter points
  of every interval until within fit_tol of the exact intersection; best round kept; refused above 1e-5 m. The parameter increases along
  the chart (ref p.42) - the XT parametrisation itself (base_parameter / base_scale, ref p.45-46) is not reproduced and not needed (edges
  are trimmed by their vertices).
  A blend's INTERSECTION spine is continued past its 'L' limits ("an artificial boundary of an intersection curve on an otherwise
  potentially infinite branch", ref p.45) by marching on the exact intersection (ours: 20 % of its length + 4 radii), so the ball reaches
  the blend's ends.
- **TRIMMED_CURVE** on an INTERSECTION basis: parm_1 / parm_2 are chart parameters; point_1 / point_2 "lie exactly on the basis curve"
  (ref p.46): their projections on the built basis are used (max 3.2e-6 in from it).
- **SP_CURVE** (existing 3drough path): its 2D B-spline is in the XT surface's parameters; the model is scaled by S, so lengths in the
  parametrisation scale - plane u, v (ref p.52), cylinder v (p.53), cone v / cos a (p.55, `xt_refgeom.h`); angles and B-surface parameters do
  not; OFFSET_SURF as its basis (p.63). Before: plane SP curves were placed 2 in off (RASTER.mcam edges 4097 / 11313 / 11320). An SP curve on a
  BLENDED_EDGE (whose parametrisation the fit does not reproduce) is not used: a tolerant edge (curves on its fins, ref p.46) takes its
  other fin's curve (4 edges in RASTER.mcam).
- **Ring edges** on a periodic curve (closed INTERSECTION, ELLIPSE): one period (was refused).
- **Edge interval on a curve** (`general_edge`, also the 3drough curves): the vertex parameter is the nearest of OCCT's interior extrema *and
  the curve's ends* (OCCT's extrema skip the ends: a vertex at an end got the first parameter, or a distance maximum inside); on a periodic
  curve a rounding tie between the two arcs keeps the forward one (the fin-sense curve order) - before, an ELLIPSE edge of RASTER.mcam took
  the long way round.

## 5. Faces (ours: OCCT representation)

- pcurves on the new surfaces: `interp_pcurve` - a 2D B-spline interpolating the surface parameters of curve points at the curve's own
  parameter (periodic unwrapped), refined until surface(pcurve(t)) is within tol / 4 of curve(t), accepted within 4 tol at 65 points
  (a GeomProjLib projection may be parametrised differently, which BRepCheck then rejects as self-intersecting wires). Also the fallback
  when a 3drough projection fails or is too coarse (an ellipse on a cylinder: 26 um).
- Edge tolerance for bodies with new geometry: max(EDGE.tolerance, 1 um + 2 x the largest accepted fit deviation).
- A face on a CONE / SPHERE / TORUS / OFFSET_SURF / BLENDED_EDGE that BRepCheck rejects: `ShapeFix_Face::Perform` (seams, degenerate pole
  edges, wire order / orientation); if still invalid, its pcurves dropped and recomputed by ShapeFix (`ShapeConstruct_ProjectCurveOnSurface`).
  Plane / cylinder / B-surface faces are untouched (the user parts' translation is byte-identical, 6).
- Feet on B-spline / offset surfaces start from the previous foot on that surface (`ShapeAnalysis_Surface::NextValueOfUV`), kept only if
  it is a true foot within the caller's distance bound, else OCCT's global search (RASTER.mcam: 104 s -> 31 s, same result).
- Time budget (ours): past `XTOCC_TIME_LIMIT` s (default 120) the fits stop and the body is refused ("time limit exceeded ..."), so a big
  body cannot hang the app (2017 / 2018 / X9 "IMPELLER WITH TILTED CURVE" sld_8: 55 blends on B-surfaces, refused after 120 s).
- `viewer/occt_mesh.cpp occt_mesh_xt`: in a body with new geometry, a face BRepMesh cannot triangulate is left out of the drive mesh with a
  warning in the error text ("N of M faces not meshed") instead of refusing the solid; bodies without new geometry: unchanged (refused).

## 6. Accuracy check (RASTER.mcam sld_0, inch model units: 1e-5 in = 0.25 um; `xt_occt_acc`)

| check (max over all samples) | samples | max deviation |
|---|--:|--:|
| CONE: OCCT point vs the XT formula | 735 | 7.9e-17 in |
| SPHERE: same | 784 | 0 |
| TORUS: same | 5831 | 5.0e-16 in |
| BLENDED_EDGE: B-spline fit vs the rolling-ball definition (194 blends) | 194 | 2.5e-5 in (0.64 um) |
| BLENDED_EDGE: ball centre to support distance - range | 211757 | 3.7e-6 in (0.09 um) |
| BLENDED_EDGE: ball centre to the support's offset (spine exactness) | 211757 | 3.9e-5 in (1.0 um, near terminators) |
| INTERSECTION: B-spline fit vs the exact intersection (357 curves) | 357 | 1.7e-4 in (4.3 um; near 'T' terminators, where the two spine-defining surfaces become tangent) |
| INTERSECTION: stored chart points (Parasolid's exact points) to our two surfaces | 2015 | 7.7e-5 in (2.0 um); 32 points beyond a bounded surface not evaluable |
| TRIMMED_CURVE on INTERSECTION: point_1 / point_2 to the basis | 14 | 3.2e-6 in |
| edges (stored curves) on their faces' surfaces: CONE / SPHERE / TORUS / CYLINDER / PLANE | 9 samples each | <= 3.5e-6 in (0.09 um) |
| edges on BLENDED_EDGE faces (CIRCLE / ELLIPSE / INTERSECTION / SP) | 602 edges | 1.1e-5 in (0.28 um) |
| edges (INTERSECTION) on B_SURFACE faces | 14 | 1.1e-5 in |
| vertices on their edge curves' ends | 1135 | 2.7e-5 in (0.69 um; EDGE.tolerance 2e-5 m on the tolerant edges) |

Topology: 491 XT faces -> 492 OCCT faces (one seam split), solid volume 33.03 in^3, 25192 triangles at deflection 0.01 in; 11 faces still fail BRepCheck after healing (5 blends,
1 B-surface (3drough path), 2 planes, 1 cylinder next to them, 2 tori - self-intersecting wires / BRepMesh polygons), 2 of them (blend faces
6096 / 8546, 0.026 and 0.0035 in^2, plane-torus blends whose fitted surface runs back on itself near a terminator) get no triangles and are
left out of the drive mesh. Natural normals of CONE / SPHERE / TORUS against the XT formula: max angle 2.5e-9 rad. Measured with
`XTOCC_OFFSET_NATURAL=1` (the other offset sign): 6 blends / 5 intersection curves unbuildable (fit deviation 0.06 in) - the sign rule of 2.
User parts (`<user-parts>`, 108 solids, `viewer/occt make check`): accuracy.tsv identical before / after.

## 6b. Regression: 3D oracles, corpus

3D oracles (tools/tests/*_oracle.sh, blend_test.sh, pencil_test.sh), unmodified build vs this one: **all identical** -
hst3d ref->ours p50 0.390 p95 1.036 | ours->ref 0.407 / 1.025; srough 0.011 / 0.079 | 0.012 / 0.049; waterline 0.037 / 0.563 | 0.040 / 20.0;
finish SF 0.018 / 0.046 | 0.022 / 0.057, RA 0.007 / 0.029 | 0.541 / 3.458; arearough 0.172 / 1.607 | 0.269 / 1.614; scallop 0.265 / 0.539 |
0.258 / 0.490; sfcontour 0.011 / 0.057 | 0.006 / 0.022; blend (CONVERT TO 5 AXIS op 10) 0.0005 / 0.0010 both ways; pencil (PENCILSEAMS op 1)
ours->ref 0.0032 / 0.0071 (pencil_test.sh did not link at the base commit - engrave.o missing from its object list, added).
tools/tests/solid_compare.sh 0/80 differ; scenes_compare.py dumps 0/46, shots 1/46 (tool_list_report, as in the last regression summary).

Corpus (`tools/tests/xt_occt_scan.sh`, every solid of <mastercam-samples> through occt_mesh_xt, 821 solids):

| result | before | after |
|---|--:|--:|
| meshed | 195 | 343 |
| no XT data in the part | 173 | 173 |
| no schema file (e.g. SCH_..._28101, Mastercam 2018) | 172 | 172 |
| XT parse failed (the 'h' size) | 93 | 0 |
| truncated / no header / unreadable part | 7 | 3 |
| CONE / TORUS / SPHERE not mapped | 83 | 0 |
| face / pcurve build (edge off its surface, line / circle not in its plane, ...) | 77 | 84 |
| SWEPT_SURF / SPUN_SURF | 12 | 20 |
| periodic B-surface / B-curve | 11 | 11 |
| blend / intersection fit refused (no contact region, fit > 10 um, its surfaces not built) | - | 14 |
| time limit (IMPELLER WITH TILTED CURVE sld_8, 3 copies) | - | 3 |

No solid that meshed before fails now. (An intermediate build crashed on 9 parts - a closed INTERSECTION ring taken for a cylinder's
circle ring; fixed: only circles count as rings there.) The new "face / pcurve" and swept / spun failures are solids that now parse.

## 7. Still refused / open

(As of b5384b5; superseded by 8.12 for SWEPT_SURF / SPUN_SURF, periodic NURBS, lemon tori, the time limit, schema 28101 and the
B-surface extension.)

- Lemon tori, cones outside OCCT's half-angle range, blend types other than R / E, variable-radius / chamfer blends (unequal ranges),
  SWEPT_SURF, SPUN_SURF, periodic B-surfaces / B-curves, BLENDED_VERTEX / BLEND_OVERLAP, PARASURF: refused by name.
- The XT parametrisation of blends / intersection curves is not reproduced (SP curves on blends are not used, 4.).
- Faces that still fail BRepCheck (6.); B-surface extension: Parasolid evaluates some spine-defining offsets of B-surfaces beyond the
  B-surface's knot range (32 stored chart points), NOT DECODED - those points are excluded from the checks.
- Horizontal Area oracle (RASTER.mcam op 2, horiz_test.sh, now without HORIZ_STL): flat levels 8 / 8 exact (+ the bottom -0.25 and a
  -1.0995 down-facing flat, both not cut by Mastercam). Full op with MCAM_HZ_OPEN=1.45 (stopped after 7 of 8 levels, ~1 h), Mastercam->ours
  p50 / p95 in: z 0 0.0226 / 0.048, z 0.125 0.0029 / 0.016 (scratch STL: 0.0023 / 0.014), 0.45 0.0071 / 0.015 (0.0064 / 0.038), 0.5 0.0018 /
  0.030, 0.55 0.0026 / 0.029, 0.65 0.0012 / 0.0026, 0.65744 0.0019 / 0.010; ours->Mastercam p50 0.02-2.4 (the region rule is NOT DECODED,
  horizontal_area.md 9.3: extra passes; z 0.125 0.82 vs 0.011 with the scratch STL - not analysed).
- Solids whose schema file is missing (e.g. SCH_2901131_28101, Mastercam 2018) cannot be read at all.

# xtfix2 (2026-10-27): the remaining failure groups

Same sources and rules as above (ref p.N = the Parasolid XT Format Reference 2006, printed page numbers; "measured" = on the stored
data; "ours" = an OCCT representation / speed choice). Code: `viewer/occt/xt_occt.cpp` (comments tagged xtfix2), `tools/tests/xt_occt_acc.cpp`
(volume computation guarded). Corpus table: 8.9.

## 8.1 Tolerant edges: each face takes its own fin's SP curve (B_SURFACE "edge curve leaves its face's surface", 60 solids)

Ref p.21: a tolerant edge has no curve; "every fin of that edge will reference a trimmed SP-curve. The underlying surface of the SP-curve
must be the same as that of the corresponding face. The curve must not deviate by more than the edge tolerance from curves on other fins
of the edge, and its ends must be within vertex tolerance of the corresponding vertices." The translator took one fin's curve as the edge
curve and *projected* it on the other face; the projection's own error pushed the gap 1-2 % past the tolerance (the TUTORIAL 4 IMPELLER
blade solids, 2017 / X9, metric and inch). Now (`Ctx::fin_pcurves`): every other fin's 2D curve (its TRIMMED_CURVE interval, oriented along
the edge curve by ends and quarter points, parameter mapped linearly onto the edge's [t0, t1]; BRepLib::SameParameter makes it exact in
parameter) is that face's pcurve. Checked: its ends against the edge's vertices within VERTEX.tolerance (ref p.21; null = half the
resolution) - else not used. Measured: the stored fin curves can be further apart than EDGE.tolerance (TUTORIAL 4 IMPELLER blades up to
1.9 x, zero at the ends and mid-edge - Parasolid apparently checks at sample points; HST DYNAMIC OPTIROUGH AND REST MATERIAL sheet up to
14.6 x near a tolerant vertex whose tolerance is larger): the OCCT edge takes the measured separation as its tolerance on that face. Ours:
a gross-error guard of 10 x the larger of the edge's and its vertices' tolerances. (ours: the factor 10 is not from the XT reference or Mastercam; kept by the user's decision 2026-10-27) Reported as `tolerant edges: ...` in Result::accuracy.

## 8.2 Sheet bodies (BODY.body_type 3)

Ref p.86 (SCH_sheet_body = 3), p.25 ("All the regions of a sheet body must be void"): the faces go into open shells, one per XT shell,
each face once (FACE.shell's list first). Was "no solid region" (behind the edge failures above). The impeller blades are sheets.
A laminar edge's EDGE.halfedge can be a dummy fin (ref p.92: "Dummy fins have fin->loop = ... = fin->curve = null"): the curve of the
edge's real fin is taken (was "edge without curve").

## 8.3 SWEPT_SURF, SPUN_SURF, lemon tori: exact OCCT surfaces

- SWEPT_SURF (ref p.71) R(u, v) = C(u) + v D -> `Geom_SurfaceOfLinearExtrusion(C, D)`, the same parametrisation (v x S) and natural normal.
- SPUN_SURF (ref p.72-73) R(u, v) = Z(u) + (C(u) - Z(u)) cos v + A x (C(u) - Z(u)) sin v -> `Geom_SurfaceOfRevolution(C, axis (base, -A))`:
  OCCT's U is the angle, V the curve parameter, and turning by U about -A is turning by -U about A, so OCCT (U, V) = XT (-v, u) and the
  natural normal is XT's. SP curves on it: (u, v) -> (-v, u) (`Ctx::uv_map`).
- C = the section / profile curve as defined (never an intersection or trimmed curve, ref), built without its sense; a LINE profile's
  parameter is a length (x S).
- Lemon torus (ref p.58: a < 0, |a| < b): the formula is the circle Q(v) = C + a X + b (X cos v + A sin v) turned by u about (C, A):
  `Geom_SurfaceOfRevolution(Q, (C, A))` - OCCT (U, V) = XT (u, v), exact. (None met in the corpus scan.)
- Checks (r.check): OCCT point vs the ref formula on 7 x 7 samples - SPUN_SURF / SWEPT_SURF max 2.8e-14 (8.10); the stored edges on them by
  the edge checks.

## 8.4 Periodic B-curves / B-surfaces, isolated loops, trimmed periodic SP curves

- Ref p.35: over its valid range a periodic B-curve is the B-spline of its knots and vertices; the period only repeats it (p.37: the same
  for surfaces). Built as the plain B-spline, then `SetPeriodic` (OCCT's periodic form drops the repeated end vertices) - accepted only
  when equal to the plain one at 33 (x 33) points within 1e-9 x S (ours), else refused. 3D curves, 2D SP curves, surfaces in u / v.
- Isolated loops (ref p.21 / p.91: a loop of a single vertex, "a 0-dimensional hole in the face", its fin has no edge - e.g. a B-surface's
  pole): skipped (OCCT has no point loop). Was "fin on an unbuilt edge".
- TRIMMED_CURVE on a periodic 2D basis (ref p.46: parm_1 in the base range, parm_2 up to a period away, below parm_1 for a '-' basis): the
  3D curve is built over the trim interval itself. A straight 2D segment (degree 1) trimmed beyond its range on a periodic surface
  (PORT 5 AXIS.mcam edge 10: 0 .. -1, point_1 = point_2, one period round the surface) is the line itself, checked: point_1 / point_2 on
  the result (1.4e-16).
- Faces on periodic B-surfaces get the ShapeFix_Face pass of 5. (they failed before, so nothing that worked changes).

## 8.5 Trimmed curves on periodic bases: one arc (ref p.46)

"If the basis curve has positive sense, parm_2 > parm_1. If ... negative sense, parm_2 < parm_1", at most a period apart: the trimmed
portion is the arc from parm_1 increasing (or decreasing) to parm_2. `general_edge` offered both arcs between the vertices and a rounding
tie could take the complementary one (IMPELLER WITH TILTED CURVE sld_1, edges 3797 / 3803 / 3820: 5.84 rad instead of 0.44, then
"wrapping loops do not bound a band"). Now the one arc of the ref (`XTOCC_TRIM_BOTH_ARCS=1` = before).

## 8.6 Analytic pcurve rules: measured gap instead of the fixed angle test

"line not in its face's plane" (12 solids), "circle not in its face's plane" (4), "circle on a cylinder not coaxial" (2): the analytic
rule's 1e-6 rad test rejected curves that lie on the face within its tolerance (WCS_EX 0.mcam sld_12 edge 5837: 1.13e-6 rad, 5e-9 m;
ref p.21: the curve lies in the surfaces "to within modeller resolution", or the edge is tolerant). Then the curve's own points'
surface parameters (`interp_pcurve`) and the existing curve-surface gap check against the edge tolerance decide. Only the failing cases
change (user parts byte-identical, 8.10).

## 8.7 Speed: the time-limit group (IMPELLER WITH TILTED CURVE sld_1, 2017 / 2018 / X9)

Profiled (XTOCC_PROF=1 prints Ctx::foot's calls / time by caller and surface type): 100 % of the 120 s went to point projections on
OFFSET_SURFs of B-surfaces (ShapeAnalysis_Surface ~1 ms a call), mostly in INTERSECTION refinement with points beyond the bounded
surface. Ours, results unchanged within the foot test:
- the feet on an OFFSET_SURF O = S + d n are searched on its basis S first: O and S have the same tangent plane at the same (u, v), so
  p - O(u, v) is normal to O exactly where p - S(u, v) is normal to S; the (u, v) is kept only when it passes the foot test on O. When the
  basis' nearest point is on its parameter boundary (p beyond the bounded surface) that point is the answer (as the global search on O
  would give a boundary point; the callers treat it as not evaluable by its residual);
- `refine` stops when its Newton step stalls without converging (a foot on a boundary);
- a blend's section at a spine parameter is computed once (the fit rounds re-sample the same u).
IMPELLER WITH TILTED CURVE sld_1 now builds its 55 blends in ~80 s (was refused at 120 s) - and then fails on 8.8's open item.
RASTER.mcam: 30 s -> 15 s, every maximum deviation of 6. unchanged (8.10).

## 8.8 B-surface extension: decoded by measurement, not yet built

Parasolid evaluates OFFSET_SURFs of B-surfaces beyond the basis' knot range (7 "NOT DECODED"). Measured on the stored chart points that lie
there (`side_dist`, check mode): they lie on **the offset of the basis' tangent (C1 linear) continuation** Eb(u, v) = Sb(ub, v) + (u - ub)
Sb_u(ub, v) (or in v) - RASTER.mcam 12 points max 1.1e-10 in, TUTORIAL #3 MULTIAXIS and Z AXIS LIMITS max 2e-10 (finite-difference noise);
the B-spline's polynomial continuation misses by up to 26 in, the linear continuation of the offset itself by up to 1.7e-5 (exact only
where both coincide). Reported as `INTERSECTION: stored chart points beyond an offset B-surface -> offset of its tangent-extended basis
(measured)`. Not yet used to build the surfaces: it needs an OCCT surface for the extended basis (a Geom_Surface subclass with its iso
curves, or an exact B-spline with the degree-1 extension pieces; corners - both directions beyond - are not measured). It is the cause
of: the Z AXIS LIMITS family (8 solids: BLENDED_EDGE 1175 "no stored point where the ball touches both supports" - its supporting
blend's ball runs onto an offset B-surface past its range), TUTORIAL #3 MULTIAXIS sld_6 (x2) and IMPELLER WITH TILTED CURVE sld_1 (x3):
"edge curve not on the built surface" (a blend's own boundary edge 0.33 mm off the fit where the support is past its range).
Bare B_SURFACE sides beyond their range (Z AXIS LIMITS) match neither rule (1e-4 .. 1e-3 in): NOT DECODED.

## 8.9 Corpus (`tools/tests/xt_occt_scan.sh`, 821 solids), before (fa49bd4) -> after

| result (failure cause) | before | after |
|---|--:|--:|
| meshed | 343 | **449** |
| no XT data in the part | 173 | 173 |
| no schema file (SCH_2901131_28101, Mastercam 2018; 8.11) | 172 | 172 |
| unreadable part / no XT header | 3 | 3 |
| B_SURFACE face: edge curve leaves its face's surface (tolerant edges, 8.1; then sheets 8.2) | 60 | 0 |
| PLANE: line / circle not in its face's plane; circle on a cylinder not coaxial (8.6) | 18 | 0 |
| SPUN_SURF / SWEPT_SURF (8.3) | 20 | 0 |
| periodic B-surface / B-curve (8.4; 3 more counted under SWEPT_SURF) | 11 | 0 |
| fin on an unbuilt edge (isolated loops 8.4), edge without curve (dummy fins 8.2) | 3 | 0 |
| time limit (IMPELLER WITH TILTED CURVE sld_1; 8.7) | 3 | 0 |
| blend: "no stored point where the ball touches both supports" (Z AXIS LIMITS family; B-surface extension 8.8) | 8 | 8 |
| blend face: edge curve not on the built surface (B-surface extension 8.8: TUTORIAL #3 x2, IMPELLER WITH TILTED CURVE x3) | 0 | 5 |
| blend / intersection fit > 10 um (8.12; the 2 SWARF solids were SWEPT_SURF refusals before) | 6 | 8 |
| blend face: edge curve leaves its face's surface (RIB CUT x2, HS AREA_REST MATERIAL_HYBRID) | 3 | 3 |
| BRepMesh left a face without triangles (PORT 5 AXIS x2: a closed SP-curve edge of its plane face gets a 20-unit tolerance from SameParameter - not analysed) | 0 | 2 |

No solid that meshed before fails now (joined per part / solid). The 5 + 2 + 2 "new" failures are solids that now get further (they
were refused earlier as SWEPT_SURF, periodic B-surface, time limit, fin / edge failures). The last code change after this scan only
touches a reported metric (the fin-curve ratio), not geometry.

## 8.10 Accuracy and regression

`xt_occt_acc` on the 106 newly meshed solids (35 parts; max over all, model units - inch or mm per part):

| check | solids | max deviation |
|---|--:|--:|
| SWEPT_SURF: OCCT point vs the ref formula | 7 | 2.2e-16 |
| SPUN_SURF: same | 9 | 4.3e-14 |
| CONE / TORUS: same (angle of natural normals) | 15 / 8 | 4.1e-11 / 0 (4.3e-10 / 3.7e-10 rad) |
| edges (B_CURVE / CIRCLE / LINE / INTERSECTION) on SWEPT_SURF / SPUN_SURF faces | 3-9 | <= 2.4e-5 (INTERSECTION), 2e-12 (others) |
| edges on B_SURFACE / PLANE / CYLINDER faces (B_CURVE, CIRCLE, LINE) | 16-85 | <= 5.9e-7 |
| tolerant edges: fin curve end to its vertex / VERTEX.tolerance | 66 | ratio 0.997 (all within, ref p.21) |
| tolerant edges: stored fin curves' separation / EDGE.tolerance | 66 | ratio 14.6 (8.1; the other face's SP curve "on" the edge's face up to 0.028 mm) |
| vertex on edge curve end | 88 | 0.0169 (= the tolerant vertices' VERTEX.tolerance) |
| BLENDED_EDGE fit vs the definition / ball centre to support - range | 4 | 9.3e-5 / 6.5e-6 (inch: 2.3 / 0.16 um) |
| INTERSECTION fit vs the exact intersection | 18 | 4.8e-5 (inch: 1.2 um) |
| BRepCheck invalid (whole body) | 2 of 106 | |

RASTER.mcam (6.): every maximum of 6.'s table unchanged; invalid XT faces 11 -> 8; volume 33.027 -> 33.0225 in^3 (the tolerant edges'
own fin curves), 25192 -> 25172 triangles; 30 s -> 15 s.
User parts (<user-parts>, 108 solids): `viewer/occt make check` accuracy.tsv byte-identical to the base build's, and the
`xt_occt_acc` reports of all 17 parts identical.

Regression (this build vs the baseline lines of ~/mastercam-re/regress/run_fix27/summary.txt and 6b): hst3d, srough, waterline, finish
SF / RA, arearough, scallop, sfcontour, blend (0.0005 / 0.0010), pencil (ours->ref 0.0032 / 0.0071): **all identical**.
solid_compare.sh 0/80 differ; scenes_compare.py dumps 0/46, shots 5/46 (tool_list_report as in the baseline + backstage_* pages; a second
run gave a different backstage set - info / open vs convert / help - clock / file-time pages, no XT involved).
Builds: `make -C viewer -j4 all`, `make -C viewer/mp`, Qt (viewer-qt/build, the main tree's cache options): OK.

## 8.11 Schema 28101 (Mastercam 2018 parts, 172 solids)

- The embedded data has no schema deltas: the header key is `SCH_2901131_28101` (modeller 29.01.131, schema 28101); ref p.5-6: a file
  carrying its schema differences has the base schema appended to the key ("SCH_1400068_14000_13006") and the per-node-type field edits
  inline - not these files.
- Schema 28002's layout does not read them: BODY already has two more pointer fields before res_size (measured: res_size 1000.0 /
  res_linear 1e-8 sit 4 bytes later), so the parse stops at the first node after BODY. Reconstructing 28101 field by field from the data
  would be invented - not done.
- The Mastercam 2018 install in ~/Downloads (mastercam2018.iso -> Mastercam/data/mastercam/Mastercam_Installer.msi -> Data1.cab /
  Data11.cab, listed / extracted to a scratch dir only, nothing run, deleted afterwards) ships sch_*.s_t up to **sch_28002** only - no
  28101. Parasolid needs schema files only to read *older* versions, so the 2018 install (Parasolid 29.1 writes 28101 itself) has no
  reason to ship it; a later install (Mastercam 2019+, Parasolid >= 30) should. <windows> has no Mastercam program install (only
  the shared data folders). Open: the user's decision where to get sch_28101.s_t (it is then read at runtime from MCAM_SCHEMA_DIR /
  paths.conf "schema", never committed).

## 8.12 Still refused / open (replaces the corresponding items of 7)

- B-surface extension (8.8): build the measured rule - 13 solids.
- PORT 5 AXIS (x2): BRepMesh leaves its plane face untriangulated (a closed SP-curve edge gets a 20-unit tolerance from SameParameter) - not analysed.
- Blend / intersection fits above 10 um, 8 solids (HOUSING COVER blends 447 / 479, SWARF OUTPUT FORMAT blends 225 / 251 / 277 x 2 files,
  FLOW 5X-AXIS LIMITS 779 x 2, PROJECTLETTERS 1209 / 1311 x 2, TOOL AXIS CONTROL ... 373 / 389): the SWARF blends' error sits in the first
  cells, where the spine's continuation past its 'L' limit turns fast (the section rotates 0.6 rad over 0.02 in) - the cubic fit does
  not converge there; not analysed further.
- RIB CUT (x2) / HS AREA_REST MATERIAL_HYBRID: a blend face's edge 0.06 / 1.6 mm off the fit - not analysed.
- Schema 28101 (8.11). Blend types other than R / E, unequal ranges, BLENDED_VERTEX / BLEND_OVERLAP, PARASURF: none met in the
  failing corpus solids (no refusal by those names in the scan), so nothing to measure against - left refused by name.
- RASTER.mcam's BRepCheck-invalid XT faces (ACC_XTFACES=1): 11 -> 8 (B-surface 10443 and tori 10995 / 11019 now valid - the tolerant
  edges' own fin curves, 8.1); left: blend fits 2461 / 2610 / 4190 / 6096 / 8546, planes 2578 / 3311, cylinder 9429. The 2 untriangulated
  blend faces 6096 / 8546 remain (plane-torus blends whose fit runs back on itself near a terminator, 6.).
