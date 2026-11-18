# Surface entities (SURF_ID, class 0x100): kinds, evaluation, display

Decoded 2026-10-25 from mccore.dll / mastercam.dll (headless Ghidra; dumps in decomp/surfkinds/, decomp/trimsrf/, decomp/surfdraw/,
gitignored). Port: viewer/mcsurf.c (positions, wireframe), viewer/occt_mesh.cpp (meshing), viewer/scene.c (display).
Record layouts of kinds 0 / 10 and the blank bit: formats/mcam.md.

## 1. Header (surf_type at ent +0x68, 0x98 bytes)

| ent | meaning | source |
|---|---|---|
| +0x68 u8 | kind: 0 NURBS, 1 parametric, 2 revolved, 4 lofted, 6 draft, 7 arc sweep, 8 offset, 10 trimmed | eval_surf (mccore 0x18047ba50) |
| +0x69 u8 | flowline density (put_surf_in_db stores the global surf_density, mccore 0x1802db160); > 1 divides the spacing | surf_division |
| +0x6a u8 | shaded (put_surf_in_db: 1 when shading is on and shade-all) - draws the surface shaded when not shade-all | FUN_18032ff50 (mastercam) |
| +0x6e u16 | flags: 0x40 = evaluable (eval_surf returns -1 without it), 4 = normal reversed, 1 / 2 = flowlines also in render_cfg 4 | eval_surf, MaFace, FUN_180238320 |
| +0x70..+0x88 | u0, u1, v0, v1: the parameter range (= the knot range of kind 0, 0..nu / 0..nv of kind 1, the angles of kind 2) | MaFace (UVTransform), measured |
| +0xd8 | a size (compared with the view height) | surf_division |
| +0xe0 / +0xe8 | u / v curvature (<= tol: flat in that direction) | surf_division |

Fixed data size per kind (the variable tail follows): 0 +0x18, 1 +0x10, 2 +0x50, 4 +0x10, 6 / 8 +0x30, 7 +0x20, 10 +0x38.

## 2. Positions (eval_surf, decomp/surfkinds/evalsurf.c; D = ent +0x100)

- **0 NURBS**: formats/mcam.md (u fastest, homogeneous points).
- **1 parametric** (FUN_1805eb9c0 l.1124): D+0 nu, +4 nv patches, +8 / +9 u / v order; coefficients at ent +0x110, per patch (u
  fastest) [v power][u power][xyz] power basis; patch = floor(u) / floor(v) clamped to the last, local = the fraction.
- **2 revolved** (eval_revsurf l.3287): D+0 axis direction, +0x18 axis point, +0x30 / +0x38 start / end angle, +0x48 profile curve
  (eptr); P = foot + r cos v + (d x r) sin v, C = eval_curve(profile, u).
- **4 lofted** (FUN_18042cad0 l.2737): D+0 i16 mode (1 linear, 2 Hermite), +4 curves, +8 u slots, +0xc records; tail: curves x slots
  i32 indices then 0x20-byte records {u0, s, eptr, -}; t = (s > 0 ? t1 : t2) + (u - u0) s; blends of l.2957-3080. (No visible kind 4
  in the corpus; index-array layout inferred from the size formula.)
- **6 draft** (FUN_180412490 l.329): D+0 vector N, +0x18 d, +0x20 curve; P = C + v (N - d |N| Dir), Dir = unit(C' x N) (|d| >= 1e12:
  C + v sign(d) |N| Dir).
- **7 arc sweep** (FUN_18041c730 l.1311, FUN_1803fca60 l.4645): curves C0 (centre), C1, C2 at u; P = C0 + Rot(C1 - C0, n, theta v).
- **8 offset** (FUN_180460cd0 l.1911, find_base_surf l.5262): D+0x18 offset, +0x20 base; nested offsets add; P = base + total N / |N|,
  N = Su x Sv.
- **10 trimmed**: the base's position (eval_surf follows D+0).
- **eval_curve** (l.4311): point; line (1 - t) p1 + t p2; arc start + t sweep in its view; parametric spline (eval_spline 0x180662eb0:
  segment floor(t), [power][coord] coefficients at ent +0xc0); NURBS curve (eval_nurbs). curve_t1t2: lines / arcs 0..1, splines /
  NURBS ent +0x98 / +0xa0.

Port notes (ours): derivatives are central differences (draft C', offset Su / Sv); kinds other than 0 are meshed through a degree 1
B-spline sampled at Mastercam's (u, v) until the cell midpoints are within the deflection. Corpus check (61 parts with the rare kinds +
120 random): 3968 / 3972 visible surfaces mesh (1 degenerate kind 1, 1 trimmed of TURBO_IMPELLER fail).
Bit-exact eval_surf (2026-11-14, N13-33; viewer/mcpartline_mesh.c plm_eval / plm_norm, checked against mccore.dll through make_surf_model):
kind 0, kind 1 (FUN_1805eb9c0 / FUN_1805ec000 + eval_poly_deriv 0x1805eb630, param_6 = 0), kind 8 (FUN_180460cd0 + find_base_surf) and
kind 10 (its base); calc_surf_norm reads the domain / size of the surface it is given (formats/hst_linker.md 21.5).

## 3. Display (mastercam.dll)

- **draw_ent** (0x1803312e0): an entity is drawn only with sel bit 0x100 off (blanked) unless Unblanking, and on a visible level.
- **draw_ent_nolist** surface branch: FUN_18032ff50 (wire?) -> FUN_18021e020 (wireframe at the entity's width ent +0x10, colour by
  find_color) else FUN_180229d90 (shaded display list; colour: gl_shade_color 2 = gl_all_color, 3 = material gl_all_material, else the
  entity's). FUN_18032ff50: wire when shading is off; with shading on and gl_shade_all: wire (= the outline) when render_cfg == 4;
  without shade-all: wire unless ent +0x6a == 1.
- **Defaults** (FUN_1801b4070): gl_shade_color 1, gl_all_color 7, gl_shade_all 1, gl_ambient 0.15, gl_all_material 6, gl_tolerance
  0.025 mm / 0.001 inch (the shading tessellation tolerance).
- **Wireframe of an untrimmed surface** (FUN_180238320): u = const lines at surf_division(pass 1) spacing from u0 (the last at u1),
  sampled along v at its step; then v = const lines (pass 0). In render_cfg 4 without +0x6e bits 0 / 1: spacing = the whole range
  (boundary only); density 0: half the range; kind 7: u sample step <= 2.
- **Wireframe of a trimmed surface** (FUN_180238ee0): the loops, then per pass iso lines from min(u0, base u0) one spacing in (density 0
  or spacing > 0.6 range: the middle line only), cut by the loops (hit_polygon / clip_uv_to_minmax / in_polygon for odd counts) and
  drawn between crossing pairs - in every mode (no render_cfg test).
- **surf_division** (mccore 0x1806f9530): line counts by the view height wk against the size: > 20 size 1 / 3, > 12 size 2 / 6, > 4 size
  4 / 12, else 8 / 36 (12 / 36 when size >= 4 wk); flat directions range / n1 x 2 (lines) or x 0.5 (samples); curved sqrt(3.2 /
  (curvature x pixels per unit)) <= half the range, x 4 for lines, >= range / count, whole divisions; kind 1 > 20 patches x 0.25,
  kind 4 v sample 1 / u 1 / ceil(1 / s), kind 7 pass 0 v 0.5; density > 1 divides.

Ours / NOT DECODED: when Mastercam rebuilds the flowlines (here: on a 1.5 x view height change or a mode change); FUN_1802389d0's loop
drawing (here the stored uv points through the base); the back-side colouring (surf_back_draw); the config keys behind the gl_* settings
(their defaults are used); the lighting of the shaded faces (the solids' headlight shader).
