# Surfaces > Create > Loft (RuledLoftedSurfaces) - decode notes (work in progress, 2026-10-28)

Dumps (gitignored): decomp/surfloft/loft.c (CreateLoftOrRuledSurface + depth 1), decomp/surfloft/loft2.c (the callees).

## Flow
- mccad RuledLoftedSurfaces 0x1802b31f0 -> FUN_1802b2190 (ISurfaceRuledLoftedManager panel: Type Ruled / Lofted (IsRuled), Chains
  Reselect; chain_zig_zag pushed; a sketcher-like chain command FUN_18008b560.. as Revolve's) -> mccore CreateLoftOrRuledSurface
  (surface_type clamp 0..2, set for the call) -> loft_ruled_maker(lofted, chains, nchains, list):
  - lofted == 0 and nchains == 2 -> lofted = 1 (two chains: the same either way); lofted and nchains > 2 with sync-by-pairs flag
    (uVar8) - ruled across consecutive pairs (local_278 = n - 1 passes) [the flag's source: param_3 > 2 in the else branch].
  - surface_type 0 -> FUN_1806fc800 (not read).
  - all_chains_alike (0x1806fc220): no more than 1 sync, FUN_1806fc690 = same entity counts; each chain's k-th curve: ruled (or
    surface_type 1): a line, or a parametric spline of order <= 4; lofted + NURBS: parametric spline (not 3D-flag 2, t1 = 0,
    t2 = its segments) / NURBS curve (not 2D, knot range = curve range) / others pass; the k-th curves of all chains the same order /
    rationality. -> lofted && NURBS: FUN_180701c20, else FUN_1807012f0.
  - not alike: break_ctour_smart(chains, boundary.dat, n, 0, 1, surf_step_chord_dev, 1, 0) -> filter_ctour_dat(boundary.dat ->
    cleanup.dat) -> FUN_180702fc0(lofted, pairs, file).
## break_ctour_smart (0x1806fcdc0) + FUN_1806ff040 (sections)
- Sections = the chain parts between sync markers (chain entries of type 2). Per chain and section: the total length; per entity
  FUN_1806ff5f0(ent, chord dev) -> a point count, the smallest length / count over the section's entities = the step; the section's
  count = total / step (1 when total <= tol), the largest over the chains; the counts + 1, at least 2 (3 lofted).
- Per chain, per section: FUN_1807002a0 = the section start point + unit tangent (eval_curve mode 1, reversed with the chain
  entity), then count - 1 more points FUN_1806fe0f0 (creep by length total / (count - 1)), each 2 p_3d (point, tangent) to the file.
## FUN_1807012f0 (alike chains, the parametric pipeline)
- Each chain -> a parametric spline (lines -> cubic segments FUN_1806fdc50(3, ..); reversed entities FUN_1806fd3a0 / FUN_180702f20),
  put_spline_in_db; across the chains per knot: ruled = linear segments (FUN_1806fd9b0(3, p_i, p_i+1)), lofted =
  make_spline_coefs_open (open cubic through the chains' points); build_surf_from_splines -> a parametric (kind 1) surface; the
  normal reversed when (Su x Sv at (0.5, 0.5)) . Cplane z < 0 (mcViews[cplane] +0x60 row); surface_type 2: flags |= 0x40,
  create_nurbsurf_from_surf (kind 1 -> NURBS).
## Correction: loft_ruled_maker's char = RULED (not lofted): ruled == 0 with 2 chains -> ruled. all_chains_alike(chains, ruled):
   ruled (or surface_type 1) needs lines / parametric splines of order <= 4; ruled && NURBS -> FUN_180701c20, else FUN_1807012f0.
## General path, continued
- FUN_1806ff5f0 (points per entity): lines 1; arcs |sweep| / (2 acos((r - dev) / r)) + 1; splines (branch empty in the decompile:
  to check in the disassembly).
- FUN_1806fe0f0: the next point at length + step (creep_along_ent; moving to the next entity of the section when past its length;
  splines' creep step +0x10 cut by 1/3 while > 0.75 step), the section end exact for the last; tangent = eval_curve mode 1, unit,
  reversed with the entity.
- FUN_180702fc0(ruled, pairs, file): per chain FUN_180702860: the points + tangents read back, per segment FUN_180702b30 ->
  pt_tan_to_sp_coefs(p0, p1, t0, t1, chord, coefs) (chord < tol: a constant segment), a parametric spline (put_spline_in_db; a chain
  shorter than tol in total: a point - store_point, e.g. a cone's apex); ruled: per chain pair per segment make_ruled_patch(NURBS,
  a, b, out) (Hermite matrices DAT_181307bf0 / DAT_181307c70 via mul_matrix; v order 2 for NURBS, else 4); lofted: per point index
  make_spline_from_pts through the chains' points (open), build_surf_from_splines; kind 1 surface (u order 4, v 2 / 4, rows =
  points - 1, cols = chains - 1), +0x6e |= 0x40, the normal reversed when (Su x Sv) at the middle . Cplane z < 0;
  create_nurbsurf_from_surf (kind 1 -> NURBS) for surface_type 2; put_surf_in_db(main colour, main level).
- make_ruled_patch = linear interpolation: the geometry matrix [A(0) A(1) A'(0) A'(1); B..; B-A ..; B-A ..] through the Hermite
  matrix M = DAT_181307bf0 (rows 1 0 0 0 / 0 0 1 0 / -3 3 -2 -1 / 2 -2 1 1) and its transpose DAT_181307c70: S = A(u) + v (B(u) - A(u));
  NURBS keeps the 8 v^0 / v^1 coefficients (v order 2). File records 0x30 bytes (DAT_181307be8 = point + tangent).
## create_nurbsurf_from_surf, kind 1 (FUN_180266ca0 case 1 -> FUN_180265e70)
- orders ou / ov (surf +0x98+8 / +9), patches nu / nv: degrees ou - 1 / ov - 1; ncu = nu (ou - 1) + 1, ncv = nv (ov - 1) + 1;
  knots: 0, then each i = 0 .. nu: (ou - 1) copies of i, then nu (and v the same) - i.e. full-multiplicity integer knots (Bezier
  segments), non-rational (+0x13 |= 1), 3 values per point; each patch's power coefficients -> Bezier by FUN_18026c490 (u) and its
  transpose (v). The record's range = the knot range 0 .. nu, 0 .. nv; the closed / reversed bits copied from the kind 1 surface.
- pt_tan_to_sp_coefs(p0, p1, t0, t1, L): the Hermite cubic on [0, L] with the unit tangents, rescaled to [0, 1] -> tangents t0 L,
  t1 L (Bezier: p0, p0 + t0 L / 3, p1 - t1 L / 3, p1). FUN_18026c490(n): the power -> Bezier matrix (n = order).
- FUN_180701c20 (alike, ruled, NURBS; loft_ruled_maker always passes ruled = 1): each chain's curves -> NURBS (lines FUN_1806fda60,
  arcs convert_arc_to_nb + nbrat_3d_to_4d, splines convert_spline_to_nb), reversed ones FUN_1802633a0 (points reversed, knots
  k -> kmax - k), joined (knot union FUN_1806fd910); v degree 1 (ruled) / 2 with middle points P_i + t_i |P_i+1 - P_i| / 2 (t_i from
  make_spline_coefs_open, unit) when not ruled; v knots integer, full multiplicity; +0x6e |= 0x40, normal reversed when (Su x Sv)
  at (0.5, 0.5) . Cplane z < 0; put_surf_in_db(main colour, main level).
## filter_ctour_dat (0x1806fe6c0; loft_ruled_maker: (boundary.dat, 0, &fp, cleanup.dat, .., surf_step_chord_dev, 0, ..))
- Per section, its interior points j = 1 .. n - 2 in order, run = the points dropped just before: the anchor a = j - 1 - run, the
  end c = j + 1; j is dropped when, for every chain, every point a + 1 .. j lies within dev of the Hermite segment a -> c
  (FUN_180702b30 = pt_tan_to_sp_coefs with the chord; proj_pt_on_coeff distance < dev); dropped: run + 1, else run = 0. The kept
  points (the same indices on every chain) are written to cleanup.dat with the new counts; FUN_180702fc0 then reads that.
## Section counts / chains (FUN_1806ff040, break_ctour_smart)
- chains used: index i (from 0) with i % step == 0, i / step < n (step = 1 here); FUN_1806ff6d0: a tangent break (dot < 0.99)
  between entities only flags (allowed: param_7 1); the counts' minimum 2 (param_12 0).
## Ported (2026-10-28): Ruled
- cui_surf.c srf_loft_exec / lf_general_ruled (the general path: counts, resampling, filter, Hermite spans, Bezier net) and
  mcsurf_build.c mcsb_loft_alike / mcsb_loft_alike_ruled (FUN_180701c20); cui_geo.c G_SLOFT (Type Ruled / Lofted, Reselect; IsRuled
  default 1, FUN_1802b40d0). Ours / NOT DECODED: proj_pt_on_coeff (sampling + Newton), CurveLength of splines (chords), the spline
  creep step (chords), FUN_1806ff960's second phase (bVar3), chain syncs (chain_it_sync: none here), chain_sync_msg in the prompt.
## Ported (2026-10-28): Lofted (more than 2 chains)
- cui_surf.c: lf_alike_lofted / lf_alike_lofted_build (all_chains_alike, lofted -> FUN_1807012f0: a line one segment a0 = start,
  a1 = end - start; a parametric spline of order <= 4 its own segments; reversed: t -> 1 - t, the segments in reverse order);
  lf_general(.., lofted = 1) (FUN_180702fc0's lofted branch: the kept points' Hermite spans, pt_tan_to_sp_coefs with the chord; the
  counts' minimum 3, FUN_1806ff040 param_12); lf_lofted (per node the cross spline through the chains' points, make_spline_coefs_open
  free ends, lf_natural; make_cubic_patch per (chain, segment), lf_patch; Bezier net degree 3 x 3, integer knots of full multiplicity,
  normal by (Su x Sv) . Cplane z at the middle, mcsb_normal_by).
- Checked offscreen (srf_scenes srf_loft_lofted): 3 circles r 10 / 20 / 30 at z 0 / 30 / 60 (general path: the sections at
  v = 0 / 1 / 2 within 0.015 of the circles, the resampling's Hermite spans) and 3 squares 40 / 50 / 60 at z 0 / 40 / 0 (alike path:
  the edges exact, the cross section a natural cubic through z 0 / 40 / 0).
- NOT PORTED: make_spline_coefs_closed (the first and last chains' points meet: a message), the surface types 0 / 1 (config 10014).
