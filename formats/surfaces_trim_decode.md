# Surfaces > Modify: Trim to Surfaces / Curves / Plane, Extend, Extend Trimmed Edges - decode (2026-11-17)

Session surftrim. Summary and status: formats/surfaces_tab_plan.md "E.". Dumps (gitignored) in decomp/surftrim/; strings from
mccadres.dll / mccoreres.dll (tools/pe_strings.py, decimal ids); panels from mccad.ui BAML (tools/baml_panel.py); the WPF property ->
native manager slot binding from mccad.interop.dll (`DOTNET_ROOT=~/.dotnet ~/.dotnet/tools/ilspycmd -t MCCAD.Interop.<Name>ViewModel`).
Sections 2 and 3 were written by two decode forks of the session (their notes, reviewed; section 2's binding note updated with ilspycmd).

## 1. Extend (SurfaceExtend) - ported for NURBS surfaces

Dumps: cmds.c (the FTs), apps.c (SurfaceExtendApp FUN_180337330, FUN_180338540), ext_mgr.c (SurfaceExtendManager 0x1802d9f00-0x1802db200),
core_ext.c (extend_surf, extend_trimmed_surf, FUN_18032d240 and helpers, dyno_surf, depth 3), ext_surf.asm (extend_surf), ext_nl.asm
(FUN_180331a30 / FUN_180332680), ext_chk.asm (FUN_18032cd00 / FUN_1803331a0), ext_geo.c (lp_ints, ll_ints_3d, alloc_surf_data_ptr).
Port: viewer/mcsrfext.c (kernel), viewer/core_ui/cui_srftrim.c (command), cui_geo.c G_SEXT (panel / picks). Oracle: tools/peloader
--ext (mccore_ext.c), tools/tests/srfext_oracle.sh.

### 1.1 Panel (surfaceextendmanagerview.baml, ISurfaceExtendManager)
- Expander Entity: label "Mode:", radio "By _distance" {ExtendByDistance} + EvaluatorBox {Distance} (enabled with it), radio "To _plane"
  (ExtendByDistance negated); label "Style:", radio "_Linear" {IsLinear} / "_Non-linear" (negated). Expander Settings: check "_Keep original
  surface" {KeepOriginalSurface}. OK, OK + New, Cancel, Help. Tooltips (resx): By distance "Extends the surface by a specified distance.",
  To plane "Opens the Plane Selection dialog box, and extends the surface to the selected plane. Clicking this button disables the By
  distance field.", Linear "Extends the surface in a linear direction.", Non-linear "Extends the surface following the curvature of the
  surface.", Keep original "Keeps the original surface once the extended surface is created."
- Binding (mccad.interop SurfaceExtendManagerViewModel): ExtendByDistance = native vt +0x38 -> FUN_1802da830 -> member +0x30; IsLinear
  vt +0x40 -> FUN_1802dab50 -> +0x40; KeepOriginalSurface vt +0x50 -> FUN_1802daba0 -> +0x60; Distance vt +0x48 -> FUN_1802da7e0 -> +0x50.
- FUN_1802daa10 (panel -> app struct): +0x10 = Keep (+0x60), +8 = Distance (+0x50), +0x11 = IsLinear (+0x40), +0 = !ExtendByDistance
  (+0x30) = To plane. FUN_1802dae40 writes back.
- Defaults FUN_1802dad50: RegisterVariable(&DAT_18064f468, 20.0, 1.0) (the distance, metric / inch); enable_dialog_modality (1) -> the
  session block DAT_18064f460: .data image To plane 0, Distance 1.0, Keep 0, IsLinear 0 (= By distance, Non-linear); FUN_1802dade0
  stores the panel back at the end. Manager vt slots 11 / 12 / 13 -> PostMessage(0x8038, 0 / 1 / 2) (OK / Apply / Cancel).

### 1.2 The loop (FUN_180337330)
- get_event(prompt, ev, 0x1000000a, mask 0x100, 1, &in_panel); prompt mccadres 7454 (0x1d1e) "Select a surface to extend".
- Event 2 (a surface): a built result committed first (UndoTransCommit, Terminate with no eptr), Apply off; get_ent_from_eptr; prompt
  7455 (0x1d1f) "Slide arrow to edge to extend from"; dyno_surf(NULL, surf, mode 3, AutoCursor 1, .., the pick's (u, v), &uv, ..)
  (viewer cui_dsurf.c; its done flag local_ab56 is not looked at: Enter / Esc in the arrow also build, at its last (u, v));
  FUN_180338540 = the edge (1.3); kinds 1 with an order > 4, 2, 4, 6, 7 (and a trimmed / offset surface on them,
  find_base_surf): message_box_yes_no(mccadres 7456 "Surface extension will be NURBS approximation.  Continue?").
- Build (a surface and edge known): To plane: get_plane_info(7457 "Select plane to extend to") first (the panel disabled around it); By
  distance: only when tol <= Distance. UndoTransStart; set_clr_sel_bits(0,4,0,4,0x100,1,0); colour mode = Keep ? 0 : (the surface's op
  references ent +0x38 > 0 ? 1 : 2); kind 10 -> extend_trimmed_surf, else extend_surf (mccore) with (dir, end, mode, Distance, plane
  point, plane normal, .., IsLinear, To plane, 0, &status); an offset surface (kind 8, or 10 over 8) To plane:
  trim_extended_offsetsurf_toplane (failure: post_warning 7459 "Extension to plane failed") [NOT DECODED further]. Failure:
  UndoTransTerminate. Success: Keep off -> delete_ent(the original, 1, ops > 0, ..); repaint; Apply on. The prompt string is emptied
  after each get_event (Ordinal_1502 "") and set again (7454) only by a commit or a failed edge: the prompt line is empty while a
  result is live.
- Panel codes: 0 / 1 / 2 commit (Terminate with nothing made), prompt 7454; 0 and 2 end. 4 / 5 (IsLinear) and 7 (Keep): a built result
  with a changed value -> UndoTransTerminate, then the build again on the same surface / edge; 6 / 8 (the mode / the distance):
  UndoTransTerminate when built; To plane -> get_plane_info, then the build. Esc: the result committed.

### 1.3 The edge (FUN_180338540: &dir, &end, the surface ent, the arrow's (u, v))
- Untrimmed: the header range u0 u1 v0 v1 (ent +0x70..); u at most the middle: fu = (u - u0) / (u1 - u0); v at most the middle: (v - v0) /
  (v1 - v0) <= fu -> v min (dir 1, end 0) else u min (0, 0); v above: (v1 - v) / (v1 - v0) <= fu -> v max (1, 1) else u min. u above the
  middle: gu = (u1 - u) / (u1 - u0); v above: gu < (v1 - v) / .. -> u max (0, 1) else v max; v below: gu < (v - v0) / .. -> u max else v min.
- Trimmed (kind 10): the nearest segment of the trim loops to (u, v) (dist_to_finite_line, t_on_line: the foot clamped at 0.0001 / 0.9999),
  stopping at a distance < 1e-6; the foot classified on the BASE's range (find_base_surf): v = v0 -> 1, v = v1 -> 4, u = u1 -> 2, u = u0 -> 8
  (1e-6); none -> post_warning 7460 "Cannot extend trimmed edges of trimmed surfaces". Then the loop is walked from the segment backwards
  and forwards (KeyPress(Esc) aborts) until a point's code differs: dir = code 1 or 4 ? 1 : 0, end = code 2 or 4 ? 1 : 0; no change: the
  untrimmed rule. [Ported: the untrimmed branch only.]

### 1.4 extend_surf (mccore 0x18032fb30; arguments from ext_surf.asm: src ent, dst ent, &eptr, dir, end, colour mode, Distance, &plane point,
&plane normal, l_3d, IsLinear, To plane, put_surf_in_db flag, &status)
- Colour / level: mode 0 = main_color / main_level, else the source's +6 / +8.
- By kind: 0 -> FUN_18032d240; 1 with orders <= 4 -> FUN_18032efb0 (parametric, NOT PORTED); 1 (higher) / 2 / 4 / 6 / 7 ->
  convert_entity_to_nurbs then FUN_18032d240 (NOT PORTED); 8 -> FUN_18032eae0 (NOT PORTED); others nothing. Each gets (src surf, dst surf,
  dir, end, Distance, plane point, plane normal, To plane, IsLinear, &uv8, &status).
- Then FUN_18032cd00(dst, uv8[0], uv8[4], uv8[6], uv8[2], &flag) (1.6); flag 0 or FUN_18032c670(dst, uv8[0], uv8[2], uv8[4], uv8[6], dir)
  == 1 -> message_box_yes_no(mccoreres 7458 "Surface extension has self-intersection - Keep surface anyway?", caption mccoreres 857
  "Warning"); No -> no result.
- dst +0 = 0x100; the normal bit (+0x6e & 4) from the source; put_surf_in_db(dst, colour, level, flag, &eptr); dst +0x20 = eptr.
- Op references (GetProdEnableBits & 6 == 0 and src +0x38 > 0): DAT_1812da318 (the session answer, -1 at start): mode 0 -> 0; mode 1 ->
  tp_blank_surf_yes_no (Yes -> 1 = substitute); mode 2 -> 2; > 0 -> add_undo_eptr_reference + tp_substitute_ent(src, dst). Solids
  depending on the source: mc_op_modify_entity. [NOT PORTED: the op substitution; the port refuses Keep off on a surface used by an
  operation, as the Untrim branch does.]

### 1.5 FUN_18032d240 (the NURBS extension; in-memory nbs at surf +0x98: counts, +0xa8 dim, +0xa9 / +0xaa degrees, +0xab flags,
+0xb0 knots (u then v), +0xb8 the net)
- One new knot span of length 1.0 at the edge; p = the degree across the edge; the control points across the edge + p.
- Knots: u min: U'[0..p] = U[0..p] - 1, then U[1..] (and V); u max: i0 = the last knot more than tol below the last (the run of the last
  value skipped), U[0..i0], p x last, (p + 1) x (last + 1), then V; v min: V'[0..q] = V[0..q] - 1, then V[1..]; v max: V[0..nkv-2], then
  (q + 1) x (V last + 1). The header range: the extended side -/+ 1. Header: kind 0, density / shaded copied, +6 &= ~3, &= ~4, |= 0x40;
  +0x113 bits 0..2 copied; surf_extent(3).
- uv8 (param_10) = the strip's corners: u min (u0, v0) (u0, v1) (u0 - 1, v0) (u0 - 1, v1); u max with u1 / u1 + 1; v min (u0, v0) (u1, v0)
  (u0, v0 - 1) (u1, v0 - 1); v max with v1 / v1 + 1.
- Per row (u edge) / column (v edge) of control points: Non-linear (IsLinear 0, count > 2 and degree > 1) FUN_180331a30 on the 3 end
  points, else Linear FUN_180332080 on the 2; at the minimum the p new points go before the row in reverse order, at the maximum after.
  To plane: per row the end leg (inner -> end point, Cartesian: FUN_180331530) against the plane (FUN_1803315e0 = lp_ints(normal, point,
  leg): t >= 0 required, else post_warning 7459 "Extension to plane failed" and no result); the length = |end - hit|.
- FUN_180332080 (Linear): direction unit(B - A) (A, B the end leg's points, Cartesian); k = 1..p: end + dir (k / p) L (before the first
  point: A - dir (k / p) L); rational: the end point's weight, the points multiplied by it.
- FUN_180331a30 (Non-linear): E = the end point, Q1 / Q2 its neighbours; a quadratic [E, E + (E - Q1), FUN_180331070(E, unit(E - Q1),
  Q2)] (rational: Cartesian, the new points weighted with the neighbours' weights); FUN_180331070: d = E - Q2, n = dir x unit(d); |n| >= 1e-6:
  in the view of n (find_view_k) the angle 2 a(dir) - a(d) at distance |d| (the circle through the three points continued), else E + d.
  The length L of the quadratic (FUN_1803316b0: 100 chords, t += 0.01); loop: |Distance - L| <= tol -> stop; FUN_180332680(Distance /
  L): the quadratic re-made over 0 .. r (end points C0 and C(r), the middle where the end tangents meet (ll_ints_3d), else the middle of
  the ends; rational: the weight sin(half angle) when the weights differ and the old quadratic is a circular arc (FUN_18032d020 /
  FUN_180331820, 1e-4), the weight at r when they differ and it is not, else 1); the new length; a change < 1e-8 -> stop; the start
  tangent turned back (dot < 0) -> FUN_180332680(Distance / previous L) and stop. Then degree elevation 2 -> p (FUN_18032cf50); the p
  points after E. FUN_18032d060 evaluates ((P0 - 2 P1) + P2) (t t) + ((P1 - P0) (2 t) + P0) (the disassembly's order, not the
  decompile's).
- The loop has no bound: a corpus case at a pole (L 4e-7 <-> 3800 alternating) loops forever in mccore.dll too (peloader); the port stops
  after 100 000 iterations (MSE_NONLIN_CAP) with no result and a message - user-approved deviation S-10.

### 1.6 The self-intersection test
- FUN_18032cd00: the uv path uv8[0] -> uv8[6] -> uv8[2] -> uv8[4] (3 segments); FUN_1803331a0 per segment: n = round(|d| / 0.2) (>= 1)
  steps (accumulated), the unit normals (FUN_18032d170: eval_surf 1 + calc_surf_unit_norm, reversed with +6 bit 2), a normal against the
  previous one of the same segment (dot < 0) -> flag 0.
- FUN_18032c670 (dir): n = round(|uv8[6] - uv8[4]| / 0.2); the unit edge tangents (Sv for a u edge, Su for a v edge) along uv8[4] ->
  uv8[6] and uv8[0] -> uv8[2] in n equal steps; the summed turning angles (acos of successive dots) more than pi apart -> 1.

### 1.7 extend_trimmed_surf (mccore 0x18032fff0) - decoded, NOT PORTED
- The base (ent +0x100; an offset base -> find_base_surf); a base of kind 1 with orders > 4 or 2 / 4 / 6 / 7: convert_entity_to_nurbs +
  put_surf_in_db (a temporary NURBS base, removed with ResetDbEnd on failure). extend_surf(base, new base, .., dir, end, mode, Distance,
  plane, .., IsLinear, To plane, 1, &status).
- The new trimmed record: the new base's header as kind 10, +0x100 = the new base, the old record's trim fields (+0x108 .. +0x130); the uv
  points copied, then per loop (count + 1 points; the closing duplicate skipped): a point on the old edge value (|x - edge| < 1e-6) moves
  to the new value; when its predecessor is off the edge (entering) a copy at the old value stays before it, when its successor is off
  (leaving) a copy at the old value follows (each insertion: the loop's count + 1, the later loops' starts + 1); points between two edge
  points move without insertion; off-edge points keep (u + 1 at a u-min edge over a parametric base); loop 0's closing point is set to
  its first when they differ by tol. store_new_trim_surf(rec, 1, 1, new base, &eptr) with the original's colour / level / attributes
  (main colour = the background when the offset-plane flag is set); Keep off: tp_substitute_ent (associative), else (flag) the new
  surface blanked with +0x38 / +0x54 = 0 and the original's colour. [The later loops' closing points keep the copied old array's values
  at their shifted positions - read literally; to check with an oracle before porting.]

### 1.8 Port status (2026-11-17)
- Ported: the panel, the picks (surface, dyno_surf arrow, the untrimmed edge rule), FUN_18032d240 (Linear,
  Non-linear), the self-intersection test and question, the header (put_surf_in_db = mcsb_finish), Keep / colour modes 0 / 2, the
  rebuild on panel changes, one undo step per result (the new surface + the deleted original).
- Oracle (tools/tests/srfext_oracle.sh: 2017 Advanced geometry SURFACING_* + PARALLELSTEEP, 6 surfaces per part, every edge x Linear /
  Non-linear / To plane): 606 cases; 24 left out (Non-linear does not converge: mccore loops forever, the port stops - E-1); of 582,
  580 identical within 1e-9 relative (knots, net, range, uv8, the self-intersection answer), 2 (one surface, v max) within 1.5e-8: the
  oracle's find_view_k stub reuses a frame whose z is within 1e-12 (dot) of the normal; run on fresh frames (one case per process) the
  other 23 such differences vanish.
- To plane (2026-11-17, integration int-s1): get_plane_info(7457 "Select plane to extend to") through the Plane Selection port (cui_plsel.c
  psel_begin; the function panel disabled while it runs): the app's plane local_aaa8 (origin 0, normal (0, 0, 1) at the start) and its ok
  flag local_aba5 (0 at the start) as decomp/surftrim/apps.c l.22116-22202 / 22440-22530 - a build in To plane mode asks only while no
  plane was accepted (Cancel: nothing built), the Mode change into To plane (code 6) always asks again, Style / Keep changes rebuild with
  the plane kept; extend_surf gets (origin, normal). Scenes srf_extend_plane (z = 25 above the top edge: the same surface bytes as By
  distance 5 in srf_extend_plane_dist), srf_extend_plane_codes, srf_extend_plane_cancel.
- NOT PORTED: trimmed surfaces (1.7 + the trimmed edge rule), kinds 1 / 2 / 4 /
  6 / 7 / 8 (FUN_18032efb0, convert_entity_to_nurbs, FUN_18032eae0), the op substitution (tp_substitute_ent, tp_blank_surf_yes_no).

## 2. Trim to Surfaces / Curves / Plane - decoded; the shared core ported (2026-11-17, "Port of the shared core" below)

Dumps (gitignored, decomp/surftrim/): cmds.c (the FTs, depth 2), apps.c (mccad 0x18031d000-0x18033a000: every App), trimwork.c
(mccad 0x18008e000-0x180091000: the TrimSurfaces* worker classes), trim_panels.c (panel <-> struct mappers, base ctor), trim_mgrs.c
(mccad 0x1802e2000-0x1802ee000: the three managers), trim_vt.txt / trim_core_vt*.txt (vtables), trim_dis_{srf,crv,pln}.asm (the app
loops' call sites: argument order recovered from the asm where Ghidra dropped stack arguments), core_k1.c / trim_core2.c (mccore
trim_surfs_to_surfs / _to_curves / _to_a_plane, select_multi_surfs, reselect_surfs, store_new_trim_surf_ext), core_trimss.c,
trim_kernel.c (MaTrimSurf* depth 2), trim_{plane,surf,crv}_closure.c (depth-6 call closures) + trim_*_missing.tsv (the closure minus
what viewer/*.c already cites, lines per function).

FT entry points (mccad exports): trim_surface_1 0x180320020 = Trim to Surfaces (SurfaceTrimToSurfacesApp, run FUN_18031ed90),
trim_surface_2 0x180324830 = Trim to Curves (SurfaceTrimToCurvesApp, FUN_180323340), trim_surface_3 0x1803299d0 = Trim to Plane
(SurfaceTrimToPlaneApp, FUN_1803287c0). Each: App ctor / run / dtor; run = function panel (FunctionPanelDialog) + a get_event loop.

### Shared worker (mccad, TrimSurfacesBase vftable 0x1804174b0; ToSurfaces 0x180417500, ToCurves 0x180417550, ToAPlane 0x1804175a0)
Slots: [1] select, [2] build, [3] commit (OK / OK-New / Cancel), [4] reselect surfaces, [5] keep-point pick, [6] exit, [7] toolpath
substitution (originals replaced), [8] toolpath refs of kept originals.
- Base ctor FUN_18008dfb0: +0x08 surface list (set 1), +0x30 result list (new surfaces), +0x58 result colours (bytes), +0xa0 assoc
  array (int64), +0x80 short count set 1, +0x82 short = 2 (keep-original mode, see below), +0x84 = 0, +0x85 = 0, +0x87 = 1, +0x88 = 1,
  +0xc8 = 1 (toolpath associativity: slot [7]/[8] at commit, FUN_18008f660 -> FUN_1800905f0).
- Field meanings (from the kernel calls): +0x82 = Keep original surface ? 0 : 2 (FUN_18008f890: `*p ? 0 : 2`); +0x84 Use current
  attributes; +0x85 Split model; +0x86 Keep multiple regions; +0x87 ToCurves: Interference check boundaries, others constant 1;
  +0x88 Extend curves to edge (ToPlane: 0). ToSurfaces: +0xd0 set 2 list, +0xf8 short count set 2, +0xfa "select both sets",
  +0xfb "reselect one set", +0xfc which (1 = set 1), +0x90 / +0x100 keep (u, v) on set 1 / set 2, +0x118 / +0x130 keep points (xyz),
  +0x110 / +0x111 keep point 1 / 2 taken, +0x148 Trim option (0 Both, 1 First set, 2 Second set). ToCurves: +0xd0 CHAIN*, +0xd8
  projection (1 = construction plane), +0xe0 max distance, +0xe8 the picked surface (ent copy), +0x90 keep (u, v). ToAPlane: +0xd0
  PLANE_sf (origin +0, normal +0x18), +0x118 Delete untrimmed surfaces.
- Build (slot [2]: ToSurfaces FUN_1800903e0, ToCurves FUN_180090230, ToAPlane FUN_1800900d0): store the flags, clear the result arrays,
  cleartextall, UndoTransStart, the mccore call, then FUN_180090020: each new surface's colour saved (+0x58), sktcher_live_color (the
  live colour), repaint, selection bits cleared; no result -> UndoTransTerminate. ToSurfaces also set_clr_sel_bits(0, 0x34 ...).
- A panel change after a build (FUN_18008f890 / 8f8e0 / 8f920 / 8fab0 / 8fb40 / 8f9b0): if a result exists, UndoTransTerminate (the
  live result discarded), result list cleared, then the app rebuilds = the live preview.
- Commit (slot [3]: FUN_18008f850 / 8f820 / 8f800 -> FUN_18008f660): no result -> UndoTransTerminate; else toolpath substitution
  (+0x82 == 2: slot [7] TpSubstituteSrfEnts per original, FUN_180090620 / 090d20 / 090920 (ToAPlane: with Delete untrimmed, originals
  left unreferenced -> DeleteTpEntsRefThisEnt); +0x82 == 0: slot [8] tp_inc_tpref), then Use current attributes off: every new
  surface rewritten + drawn in its own saved colour; on: sktcher_main_color(list); UndoTransCommit. ToCurves also frees the chains,
  ToAPlane erase_plane, ToSurfaces resets set 2 (+0xf8 = 0x10000: count 0, +0xfa = 1).
- Exit (slot [6] FUN_18008e550 / e6b0 / e700): set_clr_sel_bits off; same colour rule; UndoTransCommit when a result is pending
  (Esc after a build keeps it), else UndoTransTerminate.
- Keep-point pick (slot [5] FUN_18008eb40 / ed50 / eda0): the picked surface must be in the selected list; prompt mccadres 5431
  (0x1537) "Slide to a location to keep after trimming", dyno_surf(mode 3, the (u, v) into +0x90 / +0x100) = viewer/core_ui/cui_dsurf.c
  (ported); eval_surf(u, v) -> the keep point; ToSurfaces: set 1 when Trim option < 2 and point 1 missing, else set 2 (FUN_18008eda0
  returns "all needed points taken": Both = both, First = 1, Second = 2).

### Panels (BAML mccad.ui surface/surfacetrimto*managerview.baml; OK / OK-New / Cancel / Help; tab Basic)
Property <-> struct mapping: the managers' getters (FUN_1802ec9a0 / 1802e34f0 / 1802e83d0) and setters (FUN_1802ecf30 / 1802e3b20 /
1802e8850) read members in a fixed order; the member offsets follow the order the constructor creates them (ToSurfaces ctor in
trim_mgrs.c l.8330-8540: +0x13, +0x15, +0x17, +0x19, +0x1b), which is the BAML order. Names = that order, confirmed by each byte's use
in the kernel call (Use current attributes = the commit colour rule, Keep original = +0x82, Extend = MaTrimSurf +0x3c, ...).
[Checked with ilspycmd (mccad.interop SurfaceTrimTo*ManagerViewModel): the names exist with these native getter slots - Surfaces: ExtendCurvesToEdge vt +0x68 (member +0x98), SplitModel +0x70 (+0xa8), KeepMultipleRegions +0x78 (+0xb8), UseCurrentAttributes +0x80 (+0xc8), KeepOriginalSurface +0x88 (+0xd8); Curves: ProjectToConstructionPlane +0x58, InterferenceCheck +0x60, ExtendCurvesToEdge +0x68, MaxDistanceToSurface +0x70, SplitModel +0x78, KeepMultipleRegions +0x80, UseCurrentAttributes +0x88, KeepOriginalSurface +0x90; Plane: DeleteUntrimmedSurfaces +0x58, SplitModel +0x60, KeepMultipleRegions +0x68, UseCurrentAttributes +0x70, KeepOriginalSurface +0x78.]
Defaults = SaveRestoreLastParams (session statics, written back on exit FUN_1802eceb0 / 1802e3aa0 / 1802e87d0; read FUN_1802ece50 /
1802e3a10 / 1802e8770; .data values = the modality-off literals):
- Trim to Surfaces (ISurfaceTrimToSurfacesManager; struct: +0 Keep original, +4 int Trim option (vtable +0x58 / +0x60), +8 Split model,
  +9 Extend curves to edge, +0xa Use current attributes, +0xb Keep multiple regions; .data 0x18064f498): Trim Both (0) / First set /
  Second set; Surface Set 1 Reselect, Surface Set 2 Reselect; Extend curves to edge 0, Split model 0, Keep multiple regions 1, Use
  current attributes 0, Keep original surface 0.
- Trim to Curves (struct: +0 Keep original, +1 Project to construction plane, +8 double Normal distance, +0x10 Split, +0x11 Extend,
  +0x12 Use current, +0x13 Keep multiple, +0x14 Interference check; .data 0x18064f478): Surfaces Reselect, Chains Reselect; Project
  Curves To: Construction plane (1) / Normal: distance 0.1 in (RegisterVariable(0x18064f480, 2.0 mm, 0.1 in)), the box enabled with
  Normal; Interference check boundaries 1, Extend curves to edge 0, Split model 0, Keep multiple regions 1, Use current attributes 0,
  Keep original surface 0.
- Trim to Plane (struct: +0 Keep original, +1 Split, +2 Delete untrimmed surfaces, +3 Use current, +4 Keep multiple; .data
  0x18064f490): Surfaces Reselect, Plane Reselect; Delete untrimmed surfaces 1, Split model 0, Keep multiple regions 1, Use current 0,
  Keep original 0.
Tooltips (MCCAD.UI.Surface.*ToolTips.resx): Split model "Breaks the surfaces into multiple trimmed surfaces at their intersecting
curves ... it also blanks the original surfaces"; Keep multiple regions "allow multiple regions ... Deselect to create one region";
Delete untrimmed surfaces "Deletes the selected surfaces that are not trimmed by the slice plane and that lie on the side of the plane
you choose to trim"; Interference check "Deselect to speed up the Trim function when there are many islands ... none of the islands
will intersect"; Normal distance "the maximum distance that Mastercam looks for solutions from the trimming curves".
IsEnabled rules (the managers' validators, trim_mgrs.c) NOT DECODED.

### Trim to Surfaces (FUN_18031ed90)
- Selection (slot [1] FUN_18008f360): select_multi_surfs (mccore 0x180550fd0): prompt mccoreres 7439 (0x1d0f) "Select first set of
  surfaces and press <Enter> to continue" (select_ents mask 0x100, sel bit 0x10), then 7440 (0x1d10) "Select second set of surfaces
  and press <Enter> to continue" (bit 0x20); flag out = one set empty and the other >= 2. Reselect of one set: reselect_surfs
  (0x180550b80, same prompts, bits 0x10 / 0x20). Checks: flag -> post_warning mccadres 7449 (0x1d19) "Cannot trim - only one set of
  surfaces selected"; both counts sum < 2 -> 7450 (0x1d1a) "At least two surfaces must be selected"; both sets >= 2 -> 7451 (0x1d1b)
  "Cannot trim - one set of surfaces must have only one surface"; each failure clears the lists and selects again; Esc -> exit.
- Then the panel; prompt mccadres 4870 (0x1306) "Indicate area to keep - select a surface to be trimmed" while a needed keep point is
  missing (get_event flags |= 2, sel filter 0x10 for set 1 / 0x20 for set 2), get_event(prompt, ev, 0x10008008 | 2, mask 0x100, ...).
  Event 2 (a pick) -> slot [5]; ready -> build.
- Panel codes (event 0x10000000): 0 OK / 1 OK-New / 2 Cancel -> commit; 0 and 2 end, 1 starts again at the selection; 4 Reselect set 1
  (FUN_18008fa90), 5 Reselect set 2 (FUN_18008f9e0); 6 Keep original; 7 / 8 / 9 Trim option radios (FUN_18008fab0: a missing keep point
  -> the pick prompt again, else rebuild); 10..0xd the four check boxes (FUN_18008fb40). Event 0x8000 with codes 0xc / 0xe / 0x11 / 0x12
  (mask 0x65000) -> sktcher_updt_attrib on the live result (status-bar attribute changes; per-code meaning NOT DECODED). Enter (event 8
  key 0xd) -> FUN_18031d8e0 turns it into a panel event [its code: elided argument, as in wireframe.md, not established].
- trim_surfs_to_surfs (mccore 0x180564220) args: (+0x82, Trim option, Use current, Split, Keep multiple, Extend, 1, count1, count2, keep
  point 1, keep point 2, set 1, set 2, out list, assoc). It: GetProdEnableBits & 2 -> nothing; selection bits off; when set 1 has one
  surface and set 2 several, the sets swap (A = set 2, B = set 1, the points swap, Trim option 1 <-> 2); else A = set 1, B = set 2[0];
  a surface of A equal to B is skipped; MaSurface::LoadSurf(eptr, true, 30.0) each (trimmed: SubsetToFace(0.05));
  MaTrimSurfToSurf(A, nA, ptA, B, ptB, trimA = opt < 2, trimB = (opt & ~2) == 0, Split, Keep multiple, Extend, +0x82 == 0, Use current,
  1); [0x40] = 0; DoTrim; GetNewlyTrimmedSrfPtrs -> out; GetSrfAssocArr; no new surface -> post_warning mccoreres 5494 (0x1576)
  "Cannot Trim Surface, Selected curves do not form a closed boundary" (the same id for all three).

### Trim to Curves (FUN_180323340)
- Surfaces (FUN_18008f150): select_ents(mccadres 7494 (0x1d46) "Select surfaces and press <Enter> to continue", mask 0x100); none ->
  post_warning 7493 (0x1d45) "At least one surface must be selected" (returns 3). Then chains: chain_it(mccadres 7435 (0x1d0b) "Select
  Curves"); 1 / 2 = cancelled -> exit. Then prompt 4870 and the keep-point pick (slot [5] FUN_18008ed50: dyno_surf, the surface copied
  to +0xe8) -> build.
- Codes: 0 / 1 / 2 commit (1: surfaces and chains again), 4 Reselect surfaces, 5 Reselect chains (free_chains, chain_it, rebuild), 6
  Keep original, 7 / 8 / 9 / 10 / 0xd check boxes (7 always rebuilds), 0xb projection radio (FUN_18008f920), 0xc Normal distance (when
  |new - old| > tol, FUN_18008f8e0); event 0x8000 code 0x15 with construction-plane projection and a result -> rebuild (Cplane change).
- trim_surfs_to_curves (mccore 0x1805686b0): (surfaces, chains, count, keep (u, v), picked ent, projection, Split, Keep multiple,
  Extend, +0x82, Use current, Interference, distance, out, assoc). set_clr_sel_bits(.. 0x1f1fff ..); surfaces loaded as above; each
  chain -> MaCompositeCurve (IsCurveChain); keep point = eval_surf(picked surface, u, v); projection 0 -> MaTrimSurfToCurvesOnSurf(
  surfs, n, keep, curves, nc, distance, Split, Keep multiple, Extend, +0x82 == 0, Use current, Interference) (normal projection:
  MaSurfProjectedCurve); 1 -> MaTrimSurfToCurvesByView(..., the Cplane view matrix's third row (GetActiveViewNo(0), get_view_matrix),
  same flags) (MaTrimSurfToCurvesByView::GenTabcylSrfs: MaTabcylSurf extrusions of the curves along the view, then MaSurfSurfIntersect
  against them). this[0x180] set -> MCMessageBox mccoreres 489 "Some Curves could not be projected because they were vertical or beyond
  vertical with respect to the projection view"; no result -> 5494.

### Trim to Plane (FUN_1803287c0)
- ActivePlaneSaveRestore(Planes, 0, 1) for the command. Surfaces as Trim to Curves (7494 / 7493), then the plane: FUN_18008f0f0 =
  get_plane_info(0, PLANE_sf +0xd0) (the Plane Selection dialog, mccore 0x180a2aea0 - NOT PORTED here, see cui_slice.c); cancelled ->
  exit. Builds at once (no keep pick): get_event(empty prompt, mask 0x100, sel filter 4). Codes: 0 / 1 / 2 commit (+ erase_plane;
  1: surfaces and plane again), 4 Reselect surfaces, 5 Reselect plane (FUN_18008f9b0), 6 Keep original, 7..10 check boxes.
- trim_surfs_to_a_plane (mccore 0x18056ed00): (+0x82, Delete untrimmed, Split, Keep multiple, Use current, 1, surfaces, plane, out,
  assoc): n = unit(PLANE normal), d = origin . n, keep point = origin + n (the kept side = the normal's side);
  MaTrimSurfToPlane(surfs, n, keep, (n, d), Delete untrimmed, Split, Keep multiple, +0x82 == 0, Use current, 1); no result -> mccoreres
  5513 (0x1589) "Cannot Trim Surface, Plane does not intersect selected surface(s)".

### The kernel (mccore Ma library)
- MaTrimSurf (ctor 0x1804c6b60; vftable: [1] TrimSurfType, [2] DoTrim (= the +0x10 call), [3] GenUVKeepPoint, [4] GenKeepPtStatus):
  +0x10 surfaces, +0x18 count, +0x20 keep point, +0x39 trim part, +0x3a split, +0x3b keep multiple, +0x3c extend (copied into each
  face +8 before trimming), +0x3d keep original, +0x3e use current, +0x41 the last flag (interference / 1), +0x70 the new faces,
  +0x98 new eptrs, +0xc0 per-surface result counts. ToPlane ctor 0x1804caa00: MaTrimSurf(.., trim part = true, Split, Keep multiple,
  extend = true, keep original, use current, 1), +0xe8 plane, +0x108 = Delete untrimmed and not Split. ToSurf ctor 0x1804cba70: +0xe8
  surface B, +0xf0 point B, +0x108 trim B.
- MaTrimSurfToPlane::DoTrim 0x1804cacd0: MaSurfPlaneIntersect(NULL, plane, chord = surf_step_chord_dev twice, 1e-4 in / 0.0025 mm,
  0.5 in / 12 mm, multi true) (= mi_surf_plane_face's constants); status text mccoreres 207 (0xcf) "Trimming Surface %d of %d ..."; GenKeepPtStatus; per
  surface: SetPartSrf, ComputeIntersections (vtable +8); no intersection -> Delete untrimmed: CheckForSurfOnNegativeSideOfPlane
  0x1804caba0 (the face's first loop start or the parametric middle; signed distance < -1e-6 in / -2.5e-5 mm -> the surface goes on the
  delete list +0x48); else the part loops (GetIntersectionPartLoop 0x1804a4fe0: this+0x160[i], count +0x180) copied, >1 ->
  MaLoop::ReconstituteLoops (joins them, MaCompositeCurve::GenCompositeCurveArray); an untrimmed surface gets MaFace(surface);
  GenUVKeepPoint 0x1804cb210 (a point of an intersection loop inside the face, its uv normal, a uv line across 2 x the larger range,
  MaFace::UVLineTrimPoints, 10 samples per inside segment, the first with plane distance > 0.001 (this[0x3f] = 1 from GenKeepPtStatus; see "Port of the shared core"), nudged
  0.001 x the tangent at the segment ends); MaFace::TrimToLoopsNew 0x1804196d0(face, loops, keep uv, true, split, keep multiple, +0x41,
  &n, &faces) -> the new faces into +0x70; Delete untrimmed after a failed trim (not split) -> the negative-side test. Then
  AddNewTrimmedSrfsToDatabase 0x1804c6fb0 (each face MaFace::Make_MC_Ent(use current, keep original, &eptr, [0x40]); error ->
  mccoreres 377 (0x179) "Number of boundary points exceeds 32767") and RemoveDeletedSrfsFromDatabase 0x1804c7310 (put_special_undo(3) when referenced,
  delete_ent).
- MaTrimSurfToSurf::DoTrim 0x1804cbc30: MaSurfSurfIntersect(NULL, NULL, chord, chord, 1e-4 / 0.0025, 0.5 / 12, true) (= mi_surf_surf);
  B (trim B) and each A (trim part) get a MaFace when untrimmed; per A: SetPartSrf(A), SetDriveSrf(B), ComputeIntersections; trim B ->
  the drive loops (GetUnionedIntersectionDriveLoop 0x1804a5030, +0x210 count) collected over every A; trim A -> as the plane path
  (part loops, ReconstituteLoops, GenUVKeepPoint 0x1804cc5b0, TrimToLoopsNew); after the loop B is trimmed once with all its drive
  loops (ReconstituteLoops, GetDriveSrfKeepUV, TrimToLoopsNew); AddNewTrimmedSrfsToDatabase (no RemoveDeleted).
- MaTrimSurfToCurvesOnSurf::DoTrim 0x1804c9c80 (MaSurfProjectedCurve::GenCurve / GenUVSpline per drive curve, HandleSeamCrossings,
  ReconstituteLoops, TrimToLoopsNew) and ...ByView::DoTrim 0x1804c8e90 (GenTabcylSrfs, MaSurfSurfIntersect with SetMinMaxStepSize
  per tabcyl drive, part loops) - call lists only, NOT READ line by line.
- MaFace::Make_MC_Ent 0x180416580 (read once): loop counts > 0x7fffffff -> 1; attributes = the face's record or the surface's (colour,
  level, attrib, +0x18 / +0x1a); a subsetted / [0x1d] / record-less surface, or Keep original: a new base made (surface vtable +0x88 /
  +0x90), the base's attributes, delete_ent(new base, 1, 1) [= stored blanked], and without Keep original the original eptr deleted
  likewise; the record = kind 10, range 1/12 .. 11/12 both ways (as Flat Boundary), +0x10c loops + 1, the outer polyline (LimitUVs,
  UVInverseTransform when the surface's uv range was scaled) then the inner ones, loop table {start, count - 2};
  store_new_trim_surf_ext(rec, keep original ? 0 : 2, use current, base ent, flag, out) (0x1808ac5e0: mode 2 on a base that is not kind
  10 -> 1; mode != 0 erases the base; attributes main or the base's; the base's normal bit; +0xa0 = min(loops, 255); put_surf_in_db;
  toolpath substitution asked once (tp_blank_surf_yes_no); solids dependent -> mc_change_eptr; not use-current -> the new surface joins
  the base's groups; inc_refs_eptr(base); mode != 0 -> delete_ent(base, 0, mode == 1) [1 = blank the untrimmed base, 2 = delete the
  replaced trimmed record]).

### What is already ported (viewer/mcsrfint.c, mcsrfcrv.c, mcsurf_build.c, core_ui/cui_dsurf.c)
- Intersections: MaSurfPlaneIntersect (plane_init / march / plane_one, mi_surf_plane_face for trimmed faces), MaSurfSurfIntersect
  (ss_init / ss_main / mi_surf_surf, untrimmed branch only), the pcurve of a march (pc_of_march), loops (loop_of, loop_copy, loop_concat,
  loop_snap, loop_travel), MaLoop::Trim / TrimToLoop (trim_to_loop) / TrimToFace (trim_to_face) / FindAllLoopIntersections (loop_loop) /
  UVPointRelative (uv_rel) / PointMinDist / IsCoincidentWithLoop, MaFace from stored loops (mi_face_build), SubsetToFace
  (mi_face_subset), MaLoopIntersectData::Add, BuildTrimmedModelSpacePartCurves (part_curves).
- The record: trim_rec / mcsb_trim_rec (create_trim_from_uvs + store_new_trim_surf_ext's record part), put_surf_in_db header
  (mcsb_finish); dyno_surf mode 3 (cui_dsurf.c); select_ents-style surface picking (cui_surf.c / cui_srfedit.c on the untrim branch).
- Untrim branch (worktree-agent-aab335cc2a38c285b, cui_srfedit.c): untrim_a_trimsurf / remove boundary on records (ut_do, ut_range) -
  record-level only, no Ma library; reusable for the blank / delete of bases and the undo pattern, not for the trimming.

### NOT PORTED (the closures, decomp/surftrim/trim_*_missing.tsv; decompiled lines, depth-6 closure minus what viewer/ cites)
- Common to all three (the plane path, trim_plane_closure.c): 230 Ma* functions, ~11 900 lines. Largest: MaFace::SplitToLoop 590,
  MaFace::TrimToLoopsNew 559, MaCompositeCurve::SortCurvesHeadToTail 535, MaCurveOnSurface::BinaryUVPointsAtTol 529,
  MaLoop::TrimExtendToSurfEdges 363, MaFace::Make_MC_Ent 360, MaTrimSurfToPlane::GenUVKeepPoint 348, DoTrim 235,
  MaPolyLine::WeedOutDegenerateSegments 231, Sort2DPointsWrtVector 221, MaCurve::SnapEndPoints 211, MaLoop::Traverse 202,
  ExtendToSurfEdges 200, MaCompositeCurve::GenCompositeCurveArray 199, MaFace::FindNeighbors 168, UVLineTrimPoints 159,
  MaSurface::ExtensionEvaluate 158, MaCompositeCurve::GenUList 150, MaPolyLine::UVInverseTransform 133, MaMinMaxBox::GenFaceBox 125,
  MaLoop::Active 121, BuildUntrimmedSurfLoop 114, ..., MaLoop::ReconstituteLoops 67; plus store_new_trim_surf_ext's DB side (toolpath
  substitution, groups) and reduce_trimsurf_data 291.
- Trim to Plane also: get_plane_info (the Plane Selection dialog, ~1600 lines, not ported anywhere).
- Trim to Surfaces adds (trim_surf_closure.c) 23 functions / ~1 600 lines: GenUVKeepPoint 369, DoTrim 320,
  MaSurfSurfIntersect::GenPartSrfUnionedLoop 246, U/VBoundaryClosestToUVPoint 84 each, IsInSameRegion 65, GenKeepPtStatus 48,
  GetDriveSrfKeepUV 29, ...; and the trimmed-face branch of mi_surf_surf (part / drive loops on trimmed faces).
- Trim to Curves adds (trim_crv_closure.c) 134 functions / ~9 900 lines: MaSurfProjectedCurve (BinaryUVPointsAtTol 583, GenUVSpline 163,
  pole checks), MaCurveOnSurface::BinarySplineFit 432, MaSurface::VectorPiercePoint 373, DoTrim OnSurf 347 / ByView 267, MaCubicSpline
  ctor 345, MaLoop::TrimToSurfEdges 294, GenUVKeepPoint 269, MaCurve::PointsAndDerivsAtTol 253, BreakAtSlopeDiscontinuities 229,
  GenTabcylSrfs 213 (+ MaTabcylSurf), GenUVKeepFromPCurve 207, seam-crossing breaks 171 x 2.
- Not read at all (named above by call lists only): TrimToLoopsNew / SplitToLoop internals, the surf / curves GenUVKeepPoint, the
  projection classes, the managers' IsEnabled validators, the 0x8000 status-bar codes, the Enter mapping.

### Verdict
None of the three can be ported faithfully in one pass. The UI flow, panels, defaults, prompts, warnings and kernel arguments above
are decoded; the missing part is the Ma library's face trimming (TrimToLoopsNew -> SplitToLoop / TrimExtendToSurfEdges / Traverse /
ReconstituteLoops / GenUVKeepPoint / Make_MC_Ent), ~12 000 decompiled lines shared by all three, on top of which the intersections are
already there. The smallest first target is Trim to Plane (only plane-specific code beyond the shared core: DoTrim 235 + GenUVKeepPoint
348 + CheckForSurfOnNegativeSideOfPlane 54), blocked additionally by get_plane_info (or a user decision to take the plane another way).
Then Trim to Surfaces (+~1 600 lines and mi_surf_surf's trimmed-face branch), then Trim to Curves (+~9 900). Oracle route: peloader
mccore (as --ssint / --edg): MaTrimSurfToPlane needs LoadSurf from DB records, so a driver would have to fake DB_LIST_ENT / get_ent_from_
eptr - not tried.

### Port of the shared core (2026-11-17, session srftrimcore) - viewer/mcsrftrim.c
- Ported (transliterations, addresses at each function): MaTrimSurfToPlane::DoTrim 0x1804cacd0 / GenUVKeepPoint 0x1804cb210 /
  CheckForSurfOnNegativeSideOfPlane 0x1804caba0, MaFace::UVLineTrimPoints 0x18041a3c0, MaPolyLine::Sort2DPointsWrtVector 0x180473c20,
  MaFace::TrimToLoopsNew 0x1804196d0 / SplitToLoop 0x180417950 / FindNeighbors 0x180415510 / Copy, SetOuterLoop, AddInnerLoop, SetInnerLoops,
  IsValid, IsUVActive; MaLoop::TrimExtendToSurfEdges 0x180436220 / ExtendToSurfEdges 0x180431510 / Traverse 0x180435ac0 / SplitToLoop
  0x180434e70 / Active 0x18042efc0 / MergeInnerLoop 0x1804333c0 / Direction / Reverse / IntersectsWith / SharesPCurve / CheckForClosure /
  ReconstituteLoops 0x180433c60 (MaCompositeCurve::GenCompositeCurveArray 0x1803e71b0: MaCurve::SnapEndPoints 0x1804090b0,
  FindConnectedCurveSets 0x180401560, MaLoop(surface, composite) 0x18042e6c0); MaLoop(MaSurface *) 0x18042e630 + BuildUntrimmedSurfLoop
  0x18042ff80; MaCurveOnSurface::BinaryUVPointsAtTol 0x18040ca10; MaParameterCurve ctors 0x180464500 / 0x180464620 (+ MaCubicSpline(polyline)
  0x1803f0490 = BuildPolyPtsSpline: SetLinearTangents 0x1803fbd00 + FairSplineTangents + BuildPtVtSpline, viewer/mcspline_refit.c
  mcr_poly_pts_spline), FacetPCurve 0x180464c60, Break 0x180464a30 (MaCubicSpline / MaNURBSpline::Break: mcspline_refit.c), Reverse
  0x1804671a0 (MaCubicSpline::Flip 0x1803f6d60, MaNURBSpline::Flip 0x18044c770, MaPolyLine::Reverse 0x180473460), MaCurve::Truncate
  0x180409950 / 0x180409990, MaLoop::MoveEndPoint 0x180433520; MaCubicSpline::Length 0x1803fa380 (9-point Gauss, .data 0x1812f07e0 /
  0x1812f0830). mcsrfint.c's internals are shared through viewer/mcsrfint_p.h (every former static gets the mi_ prefix); the plane march
  gets a hook (mi_march_hook) for the loops mode.
- Facts read on the way (they correct or complete the decode above):
  - DoTrim (plane) calls ComputeIntersections(true) (asm 0x1804caeb8 `mov dl,1`): vtable +0x28 = BuildUnTrimmedIntersectionLoops 0x180495c00
    = CheckUVEndPointsClose + BuildUnTrimmedPartLoops 0x1804a3220 (the march's MaParameterCurve on the whole surface, untrimmed, one loop per
    march). MaTrimSurfToSurf::DoTrim calls it with false (asm 0x1804cbf12 `xor edx,edx`): the trimmed-face branch
    (BuildTrimmedModelSpaceIntersectionCurves 0x1804a2aa0) - DoTrim makes MaFace for B and each A first, so it always runs.
  - GenUVKeepPoint (plane): DoTrim calls GenKeepPtStatus (vtable +0x20, sets this[0x3f] = 1) before; with [0x3f] = 1 the first sample whose
    plane distance is MORE than 0.001 is kept (the [0x3f] = 0 variant: less than -0.001). The line through the loop point runs along the
    pcurve's (u, v) normal, 2 x the larger (u, v) range each way; the samples are of a MaCurveOnSurface over that MaLine (output [4] = the
    (u, v), [5] = d(u, v)/dt, asm 0x18040d74d / 0x18040d7xx).
  - MaFace +8 = extend allowed (InitFaceData sets +8 = +9 = 1; DoTrim sets +8 = MaTrimSurf +0x3c, true for the plane); +9 =
    IsOuterLoopTrimmed: MaLoop(MaSurface *) copies the face's outer loop only when +9 is 0 (MaFace(MaSurface *) 0x180414180 clears it; the
    DB constructor 0x180413ba0 sets it), else it builds the range's 4 edges. Such a loop has byte +0 = 1: UVPointRelative 0x180438770 then
    tests the surface's range (on within 1e-6; beyond a periodic direction counts inside).
  - MaLoop::MoveEndPoint / MoveStartPoint truncate a trimmed end pcurve first (MaCurve::Truncate -> MaParameterCurve::Break: the uv curve
    broken and re-faceted by BinaryUVPointsAtTol). MaLoop's CheckForClosure (an end within (0, 0.001) of the start) thus re-facets the last
    pcurve of a traversed loop.
  - Make_MC_Ent 0x180416580: MaNURBSurf::SurfType = 1 (0x180264170). A new base (MaNURBSurf::Make_nbs_type 0x18045e7f0: create_nurb_surf_new
    with the normalised knots and the surface's current (u, v) range) when the surface is subsetted (a trimmed surface after SubsetToFace),
    has regenerated knots (+0x1d), has no DB entity, or Keep original; it is stored blanked; then the loops stay in that range (no
    UVInverseTransform). Otherwise the original NURBS is the base and the loops are LimitUVs'd and UVInverseTransform'ed back to the record's
    range when it differs (IsUVRangeScaled 0x180487490 / GetOriginalUVRange 0x180485f80). The record = the base's struct as kind 10, range
    1/12 .. 11/12, ent +0x109 = +0x124 = 1 (= Flat Boundary's record), store_new_trim_surf_ext(mode Keep ? 0 : 2, Use current, ...).
- Oracle: tools/peloader --trim CASES (tools/peloader/mccore_trim.c): the case surface built as LoadSurf + SubsetToFace for a trimmed one,
  MaTrimSurfToPlane with trim_surfs_to_a_plane's arguments, +0x40 = 0, DoTrim with AddNewTrimmedSrfsToDatabase /
  RemoveDeletedSrfsFromDatabase returning, a hook on the GenUVKeepPoint slot (the keep (u, v) and the loops given to TrimToLoopsNew), the
  faces' loop polylines (MaLoop::GetLoopPolyLine = what Make_MC_Ent stores). The marching's static DAT_1813f59b0 is reset per case (and
  mi_lastn in the port) so a case does not depend on the ones before. tools/tests/srftrim_oracle.sh OUTDIR [MAX] [PART...] (srftrim_test.c
  gen: kind 0 and kind 10 surfaces, 11 planes each; env TRIM_OPTS="split keepmult delun"); srftrim_cmp.py compares (1e-9).
- Results (2026-11-17): SURFACING_* (3 surfaces per part, 209 cases, 33 trimmed): 203 identical, 6 differ; split mode: 202 / 209. Six
  Multiaxis / Handbook parts with holed trimmed surfaces and metric ones (242 cases): 241 identical; split + one region + no delete: 241 /
  242. The differences are all the same effect: the CheckForClosure gap of a traversed loop is exactly 0 in the port where the DLL finds a
  tiny one (or the reverse), so the DLL re-facets that loop's last boundary piece and the port keeps the original polyline points (the
  same geometry, other points); the cut polylines from the march already differ from the DLL's by ~1e-13 (mcsrfint.c's march), which
  decides it. Not chased further.
- Trim to Surfaces' kernel (2026-11-17, commit 1d95ba3): mi_trim_to_surfs = MaTrimSurfToSurf::DoTrim 0x1804cbc30, GenKeepPtStatus,
  GenUVKeepPoint 0x1804cc5b0, GetDriveSrfKeepUV, and MaSurfSurfIntersect's trimmed-face branch (CheckUVEndPointsClose,
  BuildTrimmedModelSpacePartCurves 0x1804a2bd0 / DriveCurves 0x1804a2560, UnionModelSpaceIntersectionCurves 0x1804a6320,
  GenDriveSrfUnionedLoop 0x1804a4350; GenPartSrfUnionedLoop is not run: DoTrim never reads the unioned part loops) on mcsrfint.c's
  surface / surface march (hook mi_ss_hook). A's are trimmed with their part loops (the untrimmed, snapped march pcurves), B once after
  all A's with the unioned drive loops. Refused: GenDriveSrfUnionedLoop on an offset drive surface. Oracle tools/peloader --trimss +
  tools/tests/srftrim_ss_oracle.sh (pairs of surfaces whose boxes meet, Both / First / Second, keep points at the boxes' middles):
  SURFACING_* 52 / 60 identical, six Multiaxis / Handbook parts 66 / 81, 0 refused; the differing ones are surface pairs meeting at an
  edge or nearly tangent where mcsrfint.c's surf / surf march already differs from the DLL (the cut points 5e-6 .. 1e-4 off, other march
  counts) - a separate job on the march - plus a few point-count differences from the closure re-facet above.
- The command (cui_srftrim.c G_STRIMS + cui_geo.c; MaFace::Make_MC_Ent on the UI side, ts_store): see surfaces_tab_plan.md "E. Trim".
  A base is loaded through mcsrfint.c mi_db_knots (MaNURBSurf's DB constructor exactly, below); a base whose knots the constructor
  regenerates (MaSurface +0x1d) always gets a new base (Make_MC_Ent's +0x1d condition). Unclamped knot vectors (the Simple Cylinder)
  are no longer refused.

### The surface / surface march matched to mccore (2026-11-17, session ssmarch)
- March-level oracle: `TRIMSS_TRACE=FILE` on tools/peloader --trimss / --trim (mccore_trim.c) hooks the real DLL's calls - MakeIntersection
  from MaSurfSurfIntersect / MaSurfPlaneIntersect::ComputeIntersectionsMain (0x1804a3c6d / 0x180495ef9: "MK" seed, "MKEND" result + points),
  SurfSurfInitIntersection / SurfPlaneInitIntersection (0x1804a3b30 / 0x180495e3a: "IN" .. "INEND", "SEED"), SurfSurfMarchIntersection in
  MarchIntersection (0x1804a59e6: "RUN" / "RUNEND"), SurfSurfTangentPlane (0x18048aea8 / 0x18048aedc: "TP" point, status, (u, v), plane, I0,
  I2), SurfSurfFinalIntersection (0x18048b3f3 / 0x18048b42e: "FI"), PointMinDist at the march / seed / InitCutData call sites (0x47da89,
  0x47dbb2, 0x47dcb4, 0x48bcb5, 0x48c946, 0x48c9ee, 0x48d249, 0x48d2b3, 0x4a5500, 0x4a5559, 0x4a560c, 0x4961c7: "PM" in / out), CurveIntersect
  (0x47dd38, 0x489dee: "CI") and MaNURBSurf::EvaluateSrf (vtable slot 26: "EV" order, (u, v), point; `TRIMSS_TRALL` every one, `TRIMSS_EVFULL`
  the derivatives); trampolines in the padding after .text (rva 0xce5700). The port prints the same lines from viewer/mcsrfint.c built with
  -DMI_TRACE (tools/tests/srftrim_test runs / run, env TRIMSS_TRACE). tools/tests/ssmarch_cmp.py PORT DLL [-noev]: per case bit-identical or
  the first differing line with its MK / RUN / step; -noev leaves the evaluations out (only the computed steps). TRIMSS_EVPTS: the
  evaluator alone (400 points + the knots per surface, orders 0 .. 3, all 33 values).
- What was wrong, found step by step and fixed (each from the decompile, the asm where noted):
  - MaNURBSurf::EvaluateSrf 0x180458ec0: the port evaluated by B-spline basis functions; the DLL converts the span's patch to Bezier form
    (ConvertNURBPatchToBezier 0x180457ad0: ConvertSegmentToBezier along u per row, then along v per column), evaluates it with
    EvaluateRationalBezierPatch 0x180458b10 (EvaluateRationalBezierCurve 0x180448b80 per row, then the rows along v) and applies the quotient
    rule with the products by 1 / w. Now mcsrfint.c nbs_eval; FindUKnotSpan 0x18045b860. Bit for bit on 501 192 evaluations (orders 0 .. 3,
    all 33 values) of the 141 SURFACING / Multiaxis surface pairs.
  - MaSurface::PointMinDist 0x180488080: the normal by its inverse length (products, not divisions); the steps times DeltaU/VAllowable
    (1 only on clamped knots); the metric terms g11, g22, g12 are (Su.Su) + (d.Suu) - two sums added last, asm 0x1804886c0 .. 0x180488830
    (the decompile prints one flat sum).
  - The hunting grid: MaSurfHuntingGrid::GenGrid 0x180495470 with AdjustRangeForPolesAndSeams 0x180494920 (a pole side moved in by |N| at
    most 1e-4 of the range, periodic sides 1e-4; SetMinMaxParams -> SetDirty -> InitEdgeInfo: the edge flags found again on the moved range;
    the range put back after) and TessellateUntrimmed 0x18048d4c0 (mode 1, 10 deg; a stack of end states, the end of a failing span replaced
    by the evaluation at (b - a) * 0.5 + a while the list gets (b + a) * 0.5; the degenerate flag sticky for the iso curve; iso parameters
    cumulative; the merge resuming at the last insertion), the 5- and 10-point fallbacks cumulative (x += range / 4, range / 9); the grid
    points kept (ISrf.gp) for EstimateUV 0x180482520, MaMinMaxBox::GenSrfBox 0x18043a1e0 and DistToPlane 0x180494d80. The port had a
    re-evaluating approximation (mcsrfcrv.c pm_tess, also not the DLL's: see "Open").
  - MaCurve::GetClosedInfo 0x1804022b0 (iso_closed): the gap test is inclusive (closed unless tol < gap), the ends Evaluate(1).
  - MaCurveHuntingPoints::GenCrvPoints 0x18040bc20: the 5-point fallback cumulative (x += range * 0.25).
- Results (the fixed case sets of the earlier sessions, scratch march/base/{ss1,ss2,o2,o4}; march-level = ssmarch_cmp):

  | set | cases | march identical before | march identical now (every step) | incl. every evaluation |
  |---|---|---|---|---|
  | Trim to Surfaces, SURFACING_* pairs | 60 | 0 | 60 | 60 |
  | Trim to Surfaces, Multiaxis / Handbook pairs | 81 | 0 | 81 | 81 |
  | Trim to Plane, SURFACING_* (11 planes each) | 209 | 0 | 209 | 142 |
  | Trim to Plane, Multiaxis / Handbook | 242 | 0 | 242 | 207 |

  The evaluations that still differ (plane sets only) are the iso curves' hunting points (MaCurve::BinaryPointsAtTol 0x1803fe780,
  ~950 lines, approximated by iso_hunt) and the edge info (MaSurface::GetEdgeInfo 0x180485a40's virtual checks, approximated by is_edges):
  other evaluation sequences with the same lists / flags on all cases (no PM / CI / TP / point differs).

  Trim oracles (srftrim_oracle.sh / srftrim_ss_oracle.sh with CASES= the same sets): differing cases before -> now: SURFACING ss 8 -> 5,
  Multiaxis ss 15 -> 6, SURFACING plane 7 -> 6 (the other option set: 6 -> 6), Multiaxis plane 1 -> 2 (the other option set: 1 -> 2). Every
  remaining one has a bit-identical march; the difference starts after it:
  - TrimToLoopsNew's input (keep point + loops, the "keep" / "inloop" lines) bit-identical, the faces differ: ss SURFACING 12, 13, 15 and
    14, 17 (B only), ss Multiaxis 45, 47, 62, plane SURFACING 144, 151, 162, 173, plane Multiaxis 219 -> MaFace::TrimToLoopsNew /
    SplitToLoop / the loop closure re-facet in mcsrftrim.c (the trim core; 151 / 162 / 173 are the CheckForClosure gap case of the earlier
    notes). Not traced inside.
  - the keep point or a loop point differs in the last bits (GenUVKeepPoint / the part-loop polylines: MaCurveOnSurface::
    BinaryUVPointsAtTol, MaParameterCurve facets - mcsrftrim.c, built on the identical march): ss Multiaxis 30, 31, 60, plane SURFACING 7,
    8, plane Multiaxis 241. Not traced.
  The new plane Multiaxis differences (219, 241; 127 fixed) are these trim-core cases surfacing once the evaluator is exact.
- MaNURBSurf's DB constructor 0x180455dd0 (mcsrfint.c mi_db_knots): each knot vector (K - K0) * (1 / (Kn - K0)) (a product, the port
  divided); the record's range likewise, clamped to the knots; when the first span is empty the knots K[ord] .. K[ord + deg - 1] within
  1e-10 of K[deg] move up 1e-6, at the end the ones within 1e-10 of K[nk - ord] down 1e-6; per direction the range "differs" when it is not
  [K[deg], K[nk - ord]]; GetMaxKnotMultiplicity 0x18045c510 (the longest run among K[deg] .. K[nk - deg - 1], the run reaching the end not
  counted, 0 with 2 unique knots); this[0x1c] when a direction's multiplicity reaches its degree and (not rational or a degree below 3);
  with param_3 (LoadSurf's true) and [0x1c]: per such direction (U first) GlobalUVToLocalUV 0x18045cc40 of both range corners when the
  range differs, RegenNonUniformU/VKnotsFromDerivRatios 0x18045ea10 / 0x18045edd0, LocalUVToGlobalUV 0x18045e330, this[0x1d]; +0xd0 ..
  +0xe8 = 0 / 1; SetMinMaxParams. RegenNonUniformUKnotsFromDerivRatios: Flowline 0x18045b9c0 (dir 2) at each unique V knot (GetUniqueVKnots
  0x18045cb20: K[deg], the distinct K[ord] .. K[nk - ord - 1], K[nk - ord]) = each u column's MaNURBSpline::SubdivisionPoint 0x180454d30
  (SegmentSubdivisionPoint 0x180453000: de Boor, (P[i] a + P[i-1] b) * (1 / (b + a))), MaNURBSpline::RegenNonUniformKnotsFromDerivRatios
  0x180452940 on it (per interior unique knot the last Bezier leg of the span before over the first leg of the span after, the points
  divided by their weights, legs below 1e-12 counted 1, the product cumulated, divided by the last; knots order zeros, each interior unique
  knot's value repeated GetMultiplicityOfKnot 0x18044e780 times (at most the order), order ones; SetKnots 0x180453740), the curves' unique
  knots summed and divided by their count, the surface's knots order zeros, each interior unique knot repeated GetMultiplicityOfUKnot
  0x18045c670 times (no cap), order ones (SetUKnots 0x18045f1a0). An unclamped knot vector comes out clamped: the Simple Cylinder (360 deg,
  degree 2, 7 points, KU -1/3 0 0 1/3 1/3 2/3 2/3 1 1 4/3) is normalised to 0 .2 .2 .4 .4 .6 .6 .8 .8 1, the range 0.2 .. 0.8 (= the knots'
  domain: no conversion) and regenerated to 0 0 0 1/3 1/3 2/3 2/3 1 1 1 with the same points and range: the DLL's surface after loading
  is not the stored cylinder [a property of mccore, reproduced; whether Mastercam shows it this way was not checked]. The old port
  (blossom-based legs, all distinct knots in the lists, a knot array of nk entries) overflowed on it.
- Oracle tools/peloader --nbsdb (the real constructor; get_ent_from_eptr answered at its call 0x180455ef5 with the case's flags / range,
  the nbs_type built as the record's +0x100 ..) + tools/tests/srftrim_test genk PART... / dbk CASES: 1 969 records of 400 corpus parts
  (407 regenerated) and the Simple Cylinder: knots, range, [0x1c], [0x1d] identical bit for bit. TRIM_DBCTOR=1 makes both trim oracles
  build their surfaces through the constructor (the DLL side by the constructor with param_3 false / true and MaPolyLine::ConvertUVParams
  between them; the port by mi_db_knots); TRIM_REP=1 lets the generators include regenerated surfaces.
- With TRIM_DBCTOR=1 the four fixed sets give the same counts (6 / 209, 2 / 242, 5 / 60, 6 / 81). New sets (TRIM_DBCTOR=1 TRIM_REP=1,
  SURFACING_* + a Simple Cylinder part made by the app, scratch cyl360.mcam): Trim to Plane 594 cases (275 on regenerated surfaces, 11 on the
  cylinder): 19 differ (cylinder 0 / 11, regenerated 11 / 275, others 8 / 308); march-level 576 / 594 identical in every step, the other 18
  all regenerated surfaces whose first difference is a plane seed (SurfPlaneInitIntersection: MaCurve::PlaneIntersect on the iso curves,
  their hunting points from iso_hunt, the approximated BinaryPointsAtTol) at 1e-11 .. 1e-10 (one a seed count 24 / 40) - not traced inside.
  Trim to Surfaces 102 pairs without the cylinder: 19 differ (regenerated 14 / 54, others 5 / 48). The cylinder's 24 pairs (each run alone,
  60 s limit): 7 time out in the DLL and / or the port (DLL and port traces identical up to the DLL's cut on the one traced: the cylinder
  against its cap marches very small steps); of the 16 that both finish the march is identical in all 16, the results differ in 12 (the
  keep loops / GenUVKeepPoint or the faces: the trim core), 3 identical.
- Open (not matched, cause known): the trim core after an identical march (above); MaCurve::BinaryPointsAtTol 0x1803fe780 (iso_hunt) and
  MaSurface::GetEdgeInfo 0x180485a40 (is_edges) are evaluated otherwise than in the DLL (same results on the fixed sets, 18 regenerated
  plane seeds); mcsrfcrv.c pm_tess (Curve One Edge''s own TessellateUntrimmed approximation) not changed; Curve Slice / Curve at Intersection
  (cui_slice.c sl_surf_build, cui_srfint.c si_srf) load untrimmed NURBS without the DB constructor (no regeneration, knots by division):
  a behaviour change there was left for a decision. [All five done in the next section.]

### The trim core matched to mccore (2026-11-17, session trimcore)
- Trimmer-level oracle: `TRIMSS_TRIM=1` with `TRIMSS_TRACE` (tools/peloader --trim / --trimss, mccore_trim.c) redirects every direct call
  (E8 rel32 in .text, one trampoline per function: hook_all) of MaLoop::FindAllLoopIntersections 0x180431a30 ("ZX": a b ta tb),
  SnapEndPoints 0x180434ad0 ("ZS"), TrimExtendToSurfEdges 0x180436220 ("ZE"), MaFace::SplitToLoop 0x180417950 ("ZF" / "ZFE": face and loop in,
  faces out), Traverse 0x180435ac0 ("ZV"), MaLoop::Trim 0x180435eb0 ("ZR" / "ZRE"), TrimToLoop 0x180437750 ("ZT"), CheckForClosure 0x180430210
  ("ZC"), MaLoop::PointMinDist 0x180433b00 ("ZP"), MaCurve::IntersectCrvCrv 0x1804030c0 ("ZI"), and MaSurface::PointMinDist at the trimmer's
  call sites ("ZQ": GenPart / GenDriveSrfUnionedLoop, MaTrimSurfToSurf GenKeepPtStatus / GenUVKeepPoint / GetDriveSrfKeepUV); a loop prints per
  pcurve the polyline's count, FNV hash of its points, t0 t1, first / last point and a hash of the uv curve (MaCubicSpline coefficients + u list,
  MaNURBSpline points + knots). Extra switches: TRIMSS_ZU (the polylines' u lists, the loop of every ZP), TRIMSS_ZM (MaLoop::PointMinDist's
  curve PMD, call 0x180433c2f), TRIMSS_ZN / ZNC / ZEC / ZECB (the model curves' PointMinDist in BuildTrimmedModelSpacePart / DriveCurves and
  UnionModelSpaceIntersectionCurves, their knots / points, their EvaluateCrv calls and Bezier points), TRIMSS_ZB (the faces built for the
  case and their input polylines), TRIMSS_ZF (MaCubicSpline::CurveFit's PointMinDist, call 0x1803f4195). The Z lines are printed only while
  DoTrim runs. The port prints the same lines from viewer/mcsrftrim.c / mcsrfint.c built with -DMI_TRACE; tools/tests/trimcore_cmp.py PORT DLL:
  per case identical or the first differing Z line with context.
- What was wrong (each found as the first differing Z line, fixed from the decompile, the asm where cited):
  - MaLoop::Trim builds its pieces with MaLoop(MaParameterCurve *) 0x18042e5c0 / MaLoop(MaParameterCurve **, int) 0x18042e570, both
    CheckForClosure when valid: the port's loop_trim used the bare constructor. The closure machinery (pcurve faceting, Break, Truncate,
    loop_new) moved from mcsrftrim.c to mcsrfint.c (mcsrfint.c needs mcspline_refit.c now); BuildTrimmedModelSpacePartCurves' MaLoop(pcurve)
    likewise.
  - GenPartSrfUnionedLoop 0x1804a4940 (the same code as GenDriveSrfUnionedLoop on the part side) runs before GenDriveSrfUnionedLoop for every
    union curve; its loops are unused by DoTrim but its steps are the DLL's (ported: gen_unioned(part)).
  - MaCubicSpline::EvaluateCrv 0x1803f47f0: the derivative's first term is C (s 1.5), not (C s) 1.5 (asm 0x1803f49ad).
  - MaNURBSpline::EvaluateCrv 0x1804487b0 by the span's Bezier form (FindKnotSpan, ConvertSegmentToBezier, EvaluateRationalBezierCurve, the
    quotient rule by 1 / w), as the surfaces were; the port evaluated by basis functions.
  - MaCubicSpline::Flip 0x1803f6d60 through ConvertToGeometric 0x1803f3be0 (P1 = c + d + b + a, D1 = b 2 + c + a 3) and ConvertToAlgebraic.
  - Face-edge pcurves (MaNURBSpline(MaPolyLine *) + RegenNonUniformKnotsFromDerivRatios): the legs as sqrt(dx dx + dy dy + dz dz), not hypot.
  - MaLoop::SnapEndPoints moves through MaLoop::MoveStart / EndPoint (a trimmed end pcurve truncated first) and deletes the loop polyline.
  - MaNURBSpline::Break keeps the vertices homogeneous: a non-rational curve's subdivided vertices carry the w the subdivision gives (not
    exactly 1) and GenUVPolyLineFromNURB / EvaluateCrv read x, y, w as stored (ICv.W; mcr_nurbs_break takes / returns x y z w).
  - MaLine::PointMinDist 0x18042ba60: the lengths sum y x z (asm 0x18042baee).
  - The stored loop points: MaFace's DB constructor 0x180413ba0 maps them with MaPolyLine::UVTransform 0x1804754e0 (when the record's range
    differs from the surface's: the translation u0 - H0 through BuildMatrixFromPoints / Invert4X3Matrix / MultiplyTwo4X3Matrices, then
    ScaleMatrix's scale about the range start with the offset -((s - 1) u0), each applied by MaPolyLine::Transform: x' = y m4 + x m0 + m12)
    to the range of LoadUntrimmedSurf(.., false) and then ConvertUVParams to the regenerated surface: mcsrfint.c mi_uv_transform, MiRegen.d0
    (the pre-regeneration range); used by Trim (cui_srftrim.c), Curve Slice's trimmed surfaces and the srftrim_test harness, which divided.
  - MaCubicSpline::CurveFit 0x1803f3f40 keeps one MaCubicCurve (local_298) whose coefficients MaCubicCurve::GenBestFitCoeff 0x1803ed9f0
    replaces without SetDirty: the hunting points made at the first hunting PointMinDist of a fit stay for the later ones (stale for the later
    segments - the DLL's behaviour, reproduced: mcsrfint.c g_cub_hunt, reset per curve_fit), and they are GenCrvPoints' BinaryPointsAtTol
    points (the port had a capped bisection). This decided the Simple Cylinder pairs' fitted curves (DLL 32 control points, port 30).
- MaCurve::BinaryPointsAtTol 0x1803fe780, the angle mode GenCrvPoints asks for (tolerance 0, param_3 1, no points), ported exactly
  (mcsrfint.c bin_pts): the end tangents carried (TS from the previous span's end, TE from a stack of pending ends), at 0.25 / 0.5 / 0.75:
  |TE.TS|, |TS.u|, |TE.u| and the chords start -> first sample (x y z length) and last sample -> end (y x z length, as the decompile) against
  cos(angle) with the radian factor .rdata 0x180d018a8 = 0.01745329251994329 (not the double nearest pi / 180); a failing span pushes its end,
  the new end is the 0.5 sample (or, when the first sample failed, the evaluation at (b - a) 0.5 + a) while the list gets (b + a) 0.5; 5000
  points at most (failure: GenCrvPoints' 20 points at range / 19); a 3-point result on a curve whose ends lie within 1e-12 is redone with half
  the angle (3 times at most). Used for the iso curves (iso_hunt), the pcurves / model curves (cv_hunt) and the fit's cubics (cub_pmd).
- MaSurface::GetEdgeInfo 0x180485a40's virtual checks exactly (MaNURBSurf vtable +0xa0 .. +0xc8 = MaSurface::CheckUClosedOrPeriodic
  0x180480940, CheckVClosedOrPeriodic 0x1804810b0, CheckU0 / U1 / V0 / V1Degenerate 0x180480620 / 0x1804807b0 / 0x180480d90 / 0x180480f20):
  degenerate = 5 points along the edge (cumulative quarter steps of the stored range width), every consecutive distance (y x z) within tol;
  closed = at the last of up to 5 stations the opposite edges' points within tol and nearer than the middle iso's point, periodic = closed
  and the smallest |N0.N1| of the unit normals at least cos 5 deg; the V check's middle point is taken on the iso u = (v0 + v1) / 2 at
  u = the station (asm 0x18048139d: edx 1, as in the U check) - the DLL's, reproduced. IsSmooth(2.5) (this[0x1b]) is not called: the DLL's
  evaluates flow-line NURBS (no surface point), the flag is not read by the ported code, and mcsrfint.c is_smooth is an approximation.
  The iso curve's GetClosedInfo is cached in the curve (Iso.ci / cl) as the DLL caches it (+0x23).
- MaNURBSpline / LoadSurf for Curve Slice and Curve at Intersection: slice_sel_surfs_with_a_plane uses LoadSurf(.., true, 30.0) and
  intersect_multiple_surfs LoadSurf(.., true, ..) (asm 0x180562c11 / 0x180562e8d: dl 1): both commands now load their NURBS through the DB
  constructor (mcsrfcrv.c mcsc_load_nbs -> mi_db_knots: the knots normalised by products, regenerated when this[0x1c]); the trimmed path of
  Curve Slice through mcsc_load_nbs + UVTransform + ConvertUVParams as Trim.
- mcsrfcrv.c's PointMinDist (mcsc_uv_of_point: Curve One Edge / Constant Parameter picks) takes its hunting grid of a NURBS from mcsrfint.c's
  GenGrid (mi_grid_lists: AdjustRangeForPolesAndSeams + TessellateUntrimmed transliterated) on the surface as LoadSurf builds it, the values
  taken back to the record's knots (the regeneration undone, x (Kn - K0) + K0); other surface kinds keep pm_tess (their evaluators are
  mcsrfcrv.c's). The flow lines by chord height (TessellateUntrimmed mode 0) still use pm_tess: mode 0 is not ported in mcsrfint.c.
- Results: see "Results (2026-11-17, session trimcore)" below.
## 3. Extend Trimmed Edges (ExtendTrimmedEdge) - decoded, NOT PORTED

Sources (all under <repo>/decomp/surftrim/, gitignored):
- mccad: cmds.c / apps.c (FUN_18032ccd0 = 750 lines, app ctor FUN_18032c640, dtor FUN_18032c880), tee_mgr.c (manager 0x180305000-0x180305d00, new),
  vt.txt (manager / app / validator vtables).
- mccore: core_tee.c (MaTrimmedEdgeExtend::* depth 3), core_k1.c (store_new_trim_surf_ext, dyno_surf, put_surf_in_db ...),
  tee_extpar.c (new: MaNURBSpline::ExtendByParameter, MaNURBSurf::SetVertices/SetUKnots/SetVKnots, MaParameterCurve ctors, UVInverseTransform,
  LimitUVs, IsUVRangeScaled, GetOriginalUVRange, GetEdgeInfo), tee_extu.asm (new: disassembly of ExtendUByParameter 0x18045a960-0x18045ac00),
  tee_vt.txt (new: Ma* vtables).
- Strings: mccadres.dll / mccoreres.dll string tables extracted to the scratchpad (mccadres.tsv, mccoreres.tsv: id, hex id, text).
- Panel: ~/mastercam-re/dotnet/mccad.ui/surface/surfacetrimmededgeextendmanagerview.baml (+ MCCAD.UI.Surface.SurfaceTrimmedEdgeExtendManagerViewToolTips.resx).

### 3.1 The command (mccad ExtendTrimmedEdge 0x18032e600 -> SurfaceTrimmedEdgeExtendApp::Run FUN_18032ccd0)

#### Panel (ISurfaceTrimmedEdgeExtendManager; BAML SurfaceTrimmedEdgeExtendManagerView)
- Buttons: OK, OK-New (Apply), Cancel, Help. Tab "Basic".
- Expander "Entity": label "Style:", radios "_Mitered" (IsChecked={Style}) / "_Rounded" ({Style} via BooleanNegationConverter). Style = true -> Mitered.
  Tooltips: "Specifies that the new surface will have mitered outer corners." / "... rounded outer corners." (images MiteredCorners.png / RoundedCorners.png).
- Expander "_Distance": EvaluatorBox Distance.Value (autocomplete list: LoadAutoCompleteList FUN_18032cc70 at start, AddToAutoCompleteList
  FUN_18032cc10 after each pick, SaveAutoCompleteList FUN_18032dc00 at exit). Tooltip "The distance to extend the selected edge."
- Expander "Direction": "Solution _1" (IsChecked={IsDirection1}, IsEnabled={EnableIsDirection1}) "Extends the edge excluding the edge between two
  selected points."; "Solution _2" (negated) "Extends the edge only between two selected points."
- Manager object (SurfaceTrimmedEdgeExtendManager vtable 0x1804b1118, fields): +0x28 EnableApply (getter slot 5 FUN_1803054f0, setter slot 6
  FUN_180305a20 "EnableApply"), +0x29 IsDirection1 (getter slot 7 FUN_180305690, setter slot 8 FUN_180305ac0), +0x2a EnableIsDirection1 (getter slot 9
  FUN_180305500, setter slot 10 FUN_180305a70), +0x2b Style (getter slot 11 FUN_1803056f0, setter slot 12 FUN_180305b90), +0x30 the Distance
  FunctionPanelEditableDouble (value = its vtable +0x28). Panel codes posted with PostMessage(0, 0x8038, code) (FUN_1803057e0): slot 14 -> 0 (OK),
  slot 15 -> 1 (Apply / OK-New), slot 16 -> 2 (Cancel), slot 17 Help callback (+0x80). Property-changed hook FUN_180305860: "IsDirection1" -> code 7,
  "Style" -> code 6. Codes 4 / 5: not posted by the manager itself (the Distance object's posts, NOT DECODED which is which; both handled alike).
  OK / Apply / Cancel = 0 / 1 / 2 as formats/wireframe.md l.198.
- Settings block (FUN_1803056a0 reads, FUN_180305b10 writes): +0 double distance, +8 byte Style, +9 byte IsDirection1.
- Defaults (SaveRestoreLastParams ctor FUN_18032c5a0 -> FUN_180305960): RegisterVariable(DAT_1806a5668, 20.0, 1.0) (the remembered block);
  when *enable_dialog_modality == 0: distance = 1.0 inch / 20.0 mm (IsMetric), IsDirection1 = 1, Style = the byte as found on the stack
  (uninitialised in the decompile: NOT DECODED - question for the user / a Windows check); otherwise the remembered DAT_1806a5668 block.
  The app itself also sets local distance 1.0 / 20.0 and IsDirection1 = 1 before reading the panel back. Saved back at exit (FUN_1803059f0) when the
  validator reports no error (slot 3 FUN_180305700).
- Validator SurfaceTrimmedEdgeExtendManagerValidator (vtable 0x1804bc0e8): FunctionPanelEditableDouble::Validate of Distance (FUN_18032ff80);
  the limits it checks (FUN_18032fca0 / FUN_180152220) NOT DECODED. The app builds only when tol <= distance.

#### Pick sequence (FUN_18032ccd0)
- UndoTransStart is per build (see below). get_event(prompt, ev, flags 0x1000000a (|2 after a panel OK/Apply/Cancel), mask 0x100 = surfaces, 1, &inpanel).
- Prompt mccadres 0x1d1e (7454) "Select a surface to extend".
- Event 2 (a surface pick, eptr = MCEvent +0x18): if a result is pending (local_59f4): commit it (list empty -> UndoTransTerminate, else
  sktcher_main_color(list) + UndoTransCommit), EnableApply(0), IsDirection1(1). get_ent_from_eptr; cleartextall;
  prompt mccadres 0xff (255) "Indicate the Surface Edge or the first point along the edge to be extended";
  dyno_surf(NULL, surf, mode 3, AutoCursor 1, &tmp, &uv, &uv, &esc, NULL, NULL, 0, true) (viewer/core_ui/cui_dsurf.c has dyno_surf mode 3).
  Esc -> back to "Select a surface". Else P1 = eval_surf(surf, 0, u, v).
  Prompt mccadres 0x101 (257) "Indicate the Second point on the Surface Edge (or press <Enter> to extend the entire Edge)"; dyno_surf again;
  Esc/Enter -> two_points = 0; else P2 = eval_surf(u, v), two_points = 1. Then build (flag bVar23).
- Panel code 0 / 1 / 2 (< 3): commit as above (sktcher_main_color + UndoTransCommit, or UndoTransTerminate when nothing was made), prompt back
  to 0x1d1e, EnableApply(0), IsDirection1(1); codes 0 and 2 end the command, 1 (Apply) continues. NB literal: Cancel (2) COMMITS a pending result.
- Codes 4..6 (distance / Style changes): UndoTransTerminate (drops the pending result); rebuild when a surface is picked and distance > tol.
- Code 7 (IsDirection1 toggled): UndoTransTerminate; P1 and P2 swapped; rebuild (Solution 2 = the other part of the loop between the points).
- After the loop: SaveAutoCompleteList, DestroyFunctionPanelDialog; last result: commit (sktcher_main_color) or UndoTransTerminate;
  set_clr_sel_bits(0, 4, 0, 4, 0x100, 1, 0).

#### Build (FUN_18032ccd0, l. ~290-400 of the function)
- Distance >= tol required. UndoTransStart; set_clr_sel_bits(0,4,0,4,0x100,1,0).
- The surface given to the kernel (ent kind at ent +0x68):
  - kind 0: MaSurface::LoadSurf(picked eptr, true, 30.0).
  - kind 8 or 10: find_base_surf; base kind 0 -> LoadSurf(picked eptr) (a trimmed / offset surface over a NURBS, loaded as such).
  - otherwise (kinds 1 2 4 6 7, or 8 / 10 over them): convert_entity_to_nurbs(ent, tmp) -> put_surf_in_db(tmp, main_color, main_level, 1, &e)
    -> delete_ent(e, 0, 0, 0, ..) -> LoadSurf(e). [convert_entity_to_nurbs on a kind 10 / 8 and delete_ent's flags: NOT READ]
- new MaTrimmedEdgeExtend(surface, distance, P1, P2, two_points, Style == 1 (mitered)) (0x1804c45d0: +0 surface, +0x18 distance, +0x20 P1,
  +0x38 P2 (only when two_points), +0x50 two_points, +0x51 mitered, +0x52 inner = 0, +8 loops, +0x10 count).
- DetermineLoopsToExtend() == 0 -> for each loop i: DoExtend(GetLoop(i), &eptr) == 0 -> eptr appended to the result list (FUN_1800563f0).
- Any result: pending = 1, repaint_graphics, sktcher_live_color(list) (shown in the live colour until committed), EnableApply(1),
  EnableIsDirection1(two_points).
- The picked surface is never changed, blanked or deleted: the result is a NEW trimmed surface (a strip) - see DoExtend.
- No warning strings in the app; the kernel's only message: mccoreres 0xc3 (195) "No Offset Solution found. (Try a smaller offset value)"
  (AfxMessageBox, Ordinal_2222, in DoExtend).

### 3.2 The kernel (mccore)

#### DetermineLoopsToExtend 0x1804c4750 (returns 0 ok, 1 fail)
- Face: the surface's own MaFace when surface +0x11 == 1 (trimmed) and +0x70 != 0; else, unless (HasSeams && HasDegenerateEdges) or
  (IsUPeriodic && IsVPeriodic) (-> return 1), new MaFace(surface) (0x180414180: outer = MaLoop(surface) = the boundary loop [MaLoop(MaSurface*) ctor
  NOT DUMPED]). MaFace::SetLoopDirections (0x180417230: MaLoop::Direction [NOT DUMPED] 1/0 against surface +0x10 (normal reversed): the outer loop
  reversed when (dir 1 and +0x10 == 0) or (dir 0 and +0x10 != 0); inner loops the opposite).
- Which loop: two_points or the face has inner loops (+0x30 > 0): FindClosestLoopTo3DPoint(P1) (0x180415390: MaSurface::PointMinDist(true, P1, ...,
  true) -> uv; MaLoop::PointMinDist on the outer, then each inner loop with a strictly smaller distance wins). Else the outer loop (inner = 0).
- Two points: the loop closest to P2 too; different loops -> two_points = 0 (whole loop of P1). Same loop: MaLoop::PointMinDist for both ->
  (pcurve index i, parameter t); t snapped to the pcurve's start (+0x28) / end (+0x30) when within 0.05; MaLoopTrimData (0x180438d80):
  i1 < i2, or i1 == i2 and t1 <= t2: 2 entries {i1,t1},{i2,t2}; otherwise 4 entries {i1,t1},{last, end of last pcurve},{0, start of pcurve 0},
  {i2,t2} (around the loop's start). inner = IsInnerLoop (0x180433050: the loop is one of the face's inner loops). MaLoop::Trim(loop, data, ..,
  &pieces, true) (ported: mcsrfint.c "MaLoop::Trim (0x180435eb0)"); fail -> return 1; the piece = pieces[0].
- One point: piece = MaLoop::Copy(loop), inner = IsInnerLoop.
- Seams / poles: if inner, or the surface has no seams and no degenerate edges, or MaLoop::BreakAtSeamsAndPoles(piece) fails -> 1 loop = piece.
  Else the broken pieces: two_points and > 1 pieces: keep the pieces within 1e-4 (MaLoop::PointMinDist) of P1's or P2's loop foot (uv);
  one point and > 1 pieces: the piece nearest P1's uv (MaSurface::PointMinDist, 1e31 start) plus the pieces IsConnectedTo it (0x180432ee0: any
  pair of ends within 1e-6 in uv); then two_points (+0x50) = 1 when >= 2 pieces or the first is not closed (MaLoop::IsClosed). Return 0.

#### FindDuDvForOffset 0x1804c5eb0 (loop, &du0, &du1, &dv0, &dv1, &duv)
- Every pcurve of the loop, every node t of its polyline (+0x138, MaPolyLine::UList): MaCurveOnSurface evaluate order 1 (output: +0 point, +0x18 the
  tangent T, +0x60 / +0x68 (u, v), +0x90 the surface normal N - layout read from use; MaCurveOnSurface::EvaluateCrv not re-read here);
  offset vector O = unit(T) x unit(N) * distance (unit of a zero vector = 0); MaSurface::Evaluate(2, u, v); DuvDtEvaluate(u, v, E, point, O, -, true,
  &du, &dv) (ported: mcsrfint.c duvdt, 0x180481e00, param_7 true = with the normal's derivative). duv = the MINIMUM over all nodes of |(du, dv)| > 0
  (start 1e31).
- Inner loop (+0x52): only duv; du0 = du1 = dv0 = dv1 = 0.
- Else: d = duv, negated when surface +0x10 == 0; MaPolyLine::Offset(loop polyline, d, mitered, +0x10 == 0, ..) -> the box of the result polylines
  (u min / max, v min / max). Failure or no result: the box of uv + duv * unit(du, dv) over all nodes. Then
  du0 = 2 |u0 - umin| when umin < u0 (surface +0x20) and not U-periodic; du1 = 2 (umax - u1) when umax > u1 (+0x28); dv0 / dv1 alike with +0x30 / +0x38;
  each zeroed on a periodic or degenerate side (IsU0Degenerate / IsU1Degenerate / IsV0 / IsV1, MaSurface +0x17.. via GetEdgeInfo).

#### DoExtend 0x1804c5140 (loop, &eptr; 0 ok)
- S = surface->Copy() (vtable +0x98); SetFace(NULL); SetNULLDBPtr (+0x88); type 4 (vtable +8; the offset surface): its base (+0xa0) SetNULLDBPtr.
- FindDuDvForOffset. Not inner and any of du0 du1 dv0 dv1 != 0: ExtendSurface(S, du0, du1, dv0, dv1), extended = 1.
- d = duv, negated when the ORIGINAL surface +0x10 == 0.
- box = extended ? (S+0x20 - du0, S+0x28 + du1, S+0x30 - dv0, S+0x38 + dv1) : (0, 1, 0, 1).
- MaLoop::Offset(loop, d, mitered, inner, extended ? S : NULL, box, &offs, &n) != 0 -> return 1.
- n == 0: inner -> MaFace(S, Copy(loop)), IsValid -> Make_MC_Ent(true, false, out), else 1; outer -> message mccoreres 0xc3, return 1.
- two_points (+0x50 == 1) (a piece of a loop): inner with n > 1: the first open offset moved to the front. L = Copy(loop); Reverse(offs[0]);
  line A = MaCubicCurve::MakeLinear(EndPoint(loop) -> StartPoint(offs[0])) -> MaCubicSpline -> MaParameterCurve(S, curve, FacetTol(loop))
  (0x180464500) -> MaLoop(pcurve); line B = EndPoint(offs[0]) -> StartPoint(loop) alike; L.Concatenate(A), (offs[0]), (B): a closed strip loop.
  Face = inner and n > 1 ? MaFace(S, L, offs[1..n-1], n - 1) : MaFace(S, L). IsValid -> Make_MC_Ent(true, false, out); else 1.
- Whole loop, outer: n < 2: MaFace(S, offs[0], {Copy(loop)}, 1); n >= 2: MaFace(S, offs[0], {Copy(loop), offs[1..]}, n) - the ring between the old
  boundary and the offset one. Inner (a hole): MaFace(S, Copy(loop), offs, n) - the ring inside the hole.
- MaFace::IsValid 0x180416200: u and v ranges (+0x10..+0x28) > 1e-6, a surface, MaLoop::IsValid of every loop [MaLoop::IsValid NOT DUMPED].

#### ExtendSurface 0x1804c5d10 (S, du0, du1, dv0, dv1)
- N = S if type 1 (NURBS), S+0xa0 if type 4 (offset), else NULL. DU = DeltaUAllowable (UMax - UMin allowable), DV alike, both BEFORE extending.
- du0 != 0, not U-periodic, not U0-degenerate: N->ExtendUByParameter(false, true, du0); du1: ExtendUByParameter(true, true, DU / (DU + du0) * du1);
  dv0 / dv1 alike with ExtendVByParameter and DV.

#### MaNURBSurf::ExtendUByParameter 0x18045a960 (atEnd, linear, amount); ExtendVByParameter 0x18045b430 the same over the other index
- Fields: +0x90 rational, +0x94 ncu, +0x98 ncv, +0x9c order u, +0xa0 order v, +0xa4 / +0xa8 degree u / v, +0xac / +0xb0 knot counts, +0xb8 vertices
  (wx wy wz w, u fastest), +0xc0 / +0xc8 knots u / v (normalised 0..1 by SetUKnots).
- Per row (v index): MaNURBSpline(ncu, order, rational), SetVerts(row), SetKnots(u knots); vtable +0x160 = ExtendByParameter(amount, atEnd, linear)
  (arguments verified in tee_extu.asm: XMM1 = amount, R8B = atEnd (param_1), R9B = linear (param_2)); the row's new vertices copied
  (ncu + degree - 1 each); the new knots summed over rows and divided by ncv (mean); then the first order knots = 0.0 and knots
  [n - order, n - 2] = 1.0 (n = new knot count; the last stays the mean); SetVertices(new, ncu + degu - 1, ncv); SetUKnots (renormalises to 0..1);
  +0xf0 = -1.

#### MaNURBSpline::ExtendByParameter 0x18044aa10 (amount, atEnd, linear) - the linear branch (the only one used here)
- !atEnd: Reverse (vtable +0xa8) before and after [MaNURBSpline::Reverse NOT READ].
- n = ncp (+0x12c), k = order (+0x134), K = knots (+0x148, count +0x130), P = vertices (+0x140, 4D).
  D = (P[n-1] - P[n-2]) * (k - 1) / (K[nk-k] - K[nk-k-1]) (4D, homogeneous); Q_i = P[n-1] + i * (amount / (k - 1)) * D, i = 1 .. k-1.
- New vertices: P[0 .. n-2], Q_1 .. Q_{k-1} (n + k - 2; the old last vertex is replaced).
- New knots: K[0 .. nk-k-1], then k - 2 times 1.0, then k times 1.0 + (K[nk-k] - K[nk-k-1]) * |Q_1 - P[n-1]| / |P[n-1] - P[n-2]| (4D lengths,
  0 when the squared length <= 1e-20); all renormalised (x - K0) / (Klast - K0), first = 0.0, last = 1.0. SetDirty, SetMinMaxParams.
- The non-linear branch (SubdivideSegment(k - 1, .., amount + 1.0, ..)) is not used by this command (read, not needed).

#### MaLoop::Offset 0x180433610 (d, mitered, inner, S or NULL, box, &loops, &n)
- side flag = inner ? (S0 +0x10 != 0) : (S0 +0x10 == 0) (S0 = the loop's surface); MaPolyLine::Offset(GetLoopPolyLine, d, mitered, flag).
- Not inner and > 1 results: the one with the largest box +0x18 (max u) moved to the front (others keep their order).
- Each polyline: S given -> MaPolyLine::UVInverseTransform(box -> S's +0x20..+0x38) (0x180475180: affine map of the box onto the range),
  MaLoop(S, polyline, FacetTol(loop), 30.0) and MaLoop::IsValid (fail -> 1); no S -> MaLoop(loop's surface, polyline, FacetTol, 30.0).
  Then (S given) the loop itself MaLoop::UVInverseTransform(box -> S range) (0x180438490, 112 lines, dumped, not read).

#### MaPolyLine::Offset 0x180470df0 (d, mitered, side, &out, &n)
- A pock_bound (0x68 bytes, +3 = side (the ccw byte), +0xc count, +0x10 entities of 0x88 bytes): one line 'L' per polyline span longer than tol.
- MCCECalcContourOffset(1, d, bound, &out, flags 0x2f, angle mitered ? 179.1 : 44.99999063801583 deg, 1e-6, 1e-6, 1e-6) (mcce.dll; ported PARTIAL in
  viewer/mcce_ctour.c, verified with flags 0x40027 / 0x60027 - flags 0x2f (bit 0x08 set, 0x40000 clear) are untested in the port).
- Each result bound -> a polyline: 'L' its start point (+ its end for the last entity); 'A' mcce::ApxArcWithLnSegs(1e-4, arc, pts, true) all points
  but the last (+ the last for the last entity) (ApxArcWithLnSegs is used in viewer/mcce_dyn.c; its 4th argument NOT DECODED there).
  free_pock_bounds. Return 0 / 1 (offset failure).

#### Storing (MaFace::Make_MC_Ent 0x180416d10 -> 0x180416580 (true = main attributes, false, out, true))
- The face's surface has no DB pointer (DoExtend nulled it) -> its vtable +0x88 SetNULLDBPtr, +0x90 MaNURBSurf::Make_MC_Ent (0x18045e7a0, NOT DUMPED)
  writes the (extended) NURBS base; its colour / level / attributes = main (no face / surface DB entity to copy from); rewrite_ent;
  delete_ent(base, 1, 1, 0, ..) [the blanking of a new base, delete_ent 0x180294860 NOT READ].
- The trimmed record (kind 10): range +8..+0x20 = 1/12, 11/12, 1/12, 11/12 (put_surf_in_db recomputes the header), +0x98 the base eptr, +0xa1 = 1,
  +0xa4 total points, +0xa8 the points (outer polyline LimitUVs'd, then each inner), +0xbc = 1, +0xc0 loops = inner + 1, the loop table after the
  points {start, count - 2} - the same record mcsurf_build.c mcsb_prim_trim / mcsb_flat_trim builds. IsUVRangeScaled (the base record's u/v range
  differs from the MaSurface's) -> the polylines mapped back to GetOriginalUVRange first.
- store_new_trim_surf_ext(rec, 2 -> 1 (base not kind 10), main attributes, base ent, true, out) (0x1808ac5e0): normal bit 4 from the base;
  +0xa0 = min(loops, 255); put_surf_in_db; toolpath substitution only for associative bases; inc_refs_eptr(base); mode 1 -> draw_ent_erase and
  delete_ent(base, 0, 1, 0, ..) (again the base's blanking, NOT READ).

### 3.3 Oracle entry points (tools/peloader, as mccore_edg.c builds a MaNURBSurf by its exported ctor)
- Pure numeric, easy: ?ExtendByParameter@MaNURBSpline@@UEAAXN_N0@Z (0x18044aa10); ?ExtendUByParameter@MaNURBSurf@@QEAAX_N0N@Z /
  ?ExtendVByParameter@... (0x18045a960 / 0x18045b430); ?DuvDtEvaluate@MaSurface@@... (already used).
- Loop level (needs a MaFace as in mccore_edg.c): ?FindDuDvForOffset@MaTrimmedEdgeExtend@@IEBAXPEAVMaLoop@@AEAN1111@Z (0x1804c5eb0),
  ?Offset@MaLoop@@QEAAHN_N0PEAVMaSurface@@QEANAEAPEAPEAV1@AEAH@Z (needs mcce.dll mapped, as the remach oracle does),
  ?DetermineLoopsToExtend@MaTrimmedEdgeExtend@@QEAAHXZ (set MaSurface +0x11 = 1, +0x70 = the face).
- End to end: ?DoExtend@MaTrimmedEdgeExtend@@QEAAHPEAVMaLoop@@PEAPEAUDB_LIST_ENT@@@Z with store_new_trim_surf_ext (0x1808ac5e0),
  MaNURBSurf::Make_MC_Ent (0x18045e7a0), delete_ent, rewrite_ent patched to record their records (peloader already patches mccore addresses).

### 3.4 Verdict
NOT portable in one pass as an exact port; feasible in a dedicated pass on top of mcsrfint.c. Already in the viewer: MaLoop::Trim / PointMinDist /
StartPoint / EndPoint / IsClosed / Concatenate / MaLoop(pcurve) / MaLoop(surface, polyline, chord, angle), MaSurface::PointMinDist, DuvDtEvaluate,
UnitNormalEvaluate, the edge flags (is_edges: degenerate / closed / periodic), MaParameterCurve from points, the trimmed record writer
(mcsb_prim_trim), dyno_surf mode 3 (cui_dsurf.c), MCCECalcContourOffset (partial, mcce_ctour.c).
To port (dump lines): FUN_18032ccd0 app 750; manager / panel (tee_mgr.c, ~45 small); DetermineLoopsToExtend 461; DoExtend 549; FindDuDvForOffset 476;
ExtendSurface 51; MaNURBSurf::ExtendU/V 152 + 167; MaNURBSpline::ExtendByParameter linear branch (~230 of 547); MaLoop::Offset 168;
MaPolyLine::Offset 682 (mostly vector plumbing; core = MCCECalcContourOffset with flags 0x2f); UVInverseTransform 133 (+ MaLoop's 112);
LimitUVs 32; FindClosestLoopTo3DPoint 67; SetLoopDirections 41; IsInnerLoop 36; IsConnectedTo 46; BreakAtSeamsAndPoles 109; MaLoop::Reverse 73;
MakeLinear 42 + MaParameterCurve(S, curve, tol) 23 (+ GenPCurve, dumped in tee_extpar.c); MaFace::IsValid 35; Make_MC_Ent 360; SetUKnots /
SetVKnots / SetVertices ~150.
NOT DECODED / not dumped: MaLoop(MaSurface*) (the boundary loop of an untrimmed surface), MaLoop::Direction, MaLoop::IsValid, MaFace::InitFaceData /
CheckForValidLoops, MaNURBSpline::Reverse, MaNURBSurf::Make_MC_Ent 0x18045e7a0, delete_ent's flags (blank vs delete of the new base),
convert_entity_to_nurbs on kinds 8 / 10 over non-NURBS, MaCurveOnSurface evaluate output layout (inferred), the Distance validator limits and
which of codes 4 / 5 the Distance box posts, the default Style byte (uninitialised in the decompile), MCCECalcContourOffset with flags 0x2f.
Behaviour notes for the user: the original surface is kept (a new strip surface is made on an extended copy of the base); Cancel commits a pending
result (literal reading of codes 0..2).
