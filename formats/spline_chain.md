# Spline curves in chains: chain_to_ctour_array's spline breaking (mccore.dll) - and the app's spline entities

Decompiles: `decomp/mccore_spline_brk.c` (Ghidra project `~/mastercam-re/ghidra_core`, mccore.dll; the header lists the regexes,
the constants read with MemDump and the call arguments checked in the disassembly). Port: `viewer/mcsurf.c` (spline evaluation +
brk_spline_const_err), `viewer/cam2d.c` (`cam_chain_polyline`, chaining), `viewer/mcam_load.c` (spline entities). Oracle:
`APP=1 tools/tests/engrave_test.sh` (section 5).

## 1. Where splines meet the toolpath code

`chain_to_ctour_array` (mccore 0x180283000, export ordinal 10080) only swaps the resource handle and calls
`chain_to_ctour_array_break(chain, ctour, &depth, view, max, start, &count, &planar, splines_ok, tol, arcs_3d, 0, &ok, 0)`
(0x180283120; `decomp/mccore_spline_brk.c` l.9-48). Engrave (engrave.dll FUN_18001e400, `decomp/engrave/contour_build.c` l.196-198)
passes view = the active view, max = 0x7fff, start = 0, `splines_ok = 1`, `tol = S+0x28 / 10`, `arcs_3d = 0`.

Per chain entity (`chain_to_ctour_array_break`, l.236-618; the type test `(type & 6) == 0` at l.276 splits points / splines from lines / arcs) the entity is read (`get_ent_from_eptr`), `FUN_180284b40` trims its
parameter range only for a Dynamic start / end (param_12 = 0 here: unchanged, l.267-274, l.849-943), and by class:

| class | ctour records | source |
|---|---|---|
| point 1 | one point record at the chain start | l.359-372 |
| line 2 | its end as an 'L' record (0x4c at +0x21) when it differs from the last record by more than `tol` (config 1000) in x or y | l.541-567 |
| arc 4 | an 'A' record (0x41): centre in the view, direction, the +0x48 full-circle flag; an arc whose view is perpendicular to the chaining view (|n . n_view| < 2e-7) is taken as a line | l.467-540 |
| **spline 8 / NURBS 0x80** | `splines_ok == 0`: post_warning 0x149d (unless quiet), failure (l.277-283). Else: the chain's first record is the spline's start (end when traversed backwards) in the view (`world_to_2d`, l.285-311); then **FUN_180282bd0** breaks it into 'L' records (l.313-355) | below |

So a spline is **never kept as a spline and never turned into arcs** here: it becomes lines at the chord tolerance `tol` the
caller passes. (Arcs from splines would need `break_spline_arcs`; chain_to_ctour_array does not call it.) The record limit
`max` (Engrave 0x7fff) gives post_warning 0x149c and failure (l.608 / l.823).

## 2. FUN_180282bd0: one spline into line records (l.688-848)

Arguments: the entity, `rev` (CHAIN_ENT +0x18 == 1), the ctour array, &count, start index, max, view, **tol** (param_8, the chord
tolerance), the CHAIN_ENT +0x10 id, the Dynamic mode / parameter (0 here), quiet, &ok.

1. `t = rev ? t2 : t1` (ent +0xa0 / +0x98); `Pprev = eval(t)` (`pt_on_ent_from_t` = eval_curve, 0 derivatives).
2. Loop (l.778-835): `brk_spline_const_err(ent, curve_min_step, curve_max_step, tol, rev, &t)`; `P = eval(t)`;
   when `|P - Pprev| >= tol_sys` (3D; `tol` = config 1000 "system tolerance", the global at l.786): P in the view (`world_to_2d`);
   an 'L' record is appended when `|x - x_last| > tol_sys` or `|y - y_last| > tol_sys` against the last record of the array
   (l.793-818; +0x28 the entity's +0x20, +0x30 the parameter t, +0x20 rev); `Pprev = P` (whether appended or not).
3. Until `t > t2 - 2e-7` (forward) / `t < t1 + 2e-7` (rev) (l.828-835).
4. The last record's x, y become the stored far end point (ent +0x80, or +0x68 when rev) in the view (l.838-847).

## 3. brk_spline_const_err (0x180162540, export 9930; l.944-1226)

`brk_spline_const_err(ent, min_step, max_step, tol, rev, &t)` - from t the next break parameter. min_step / max_step =
`curve_min_step` / `curve_max_step` (config 1004 / 1005; defaults FUN_1803bc7d0, `decomp/mccore_systol.c`: inch 0.001 / 100,
mm 0.02 / 100). `dim` = S_ID ent +0xb5, NB_ID ent +0xd1 less 1 when rational (ent +0xd2 bit 2 clear) - 2 means planar (x, y) math.

1. S_ID with dim 2 and `testMoldPlusDataHidingInSpline` (four marks at the start of the coefficients, 0x1802bf5c0): t = the far
   end, done (l.1012-1018).
2. The span of t: S_ID `floor(t) + 1` (rev with an integral t: `floor(t)`), clamped to 1 .. nseg; NURBS `nurb_interval(t) + 1`
   (l.1019-1036).
3. `s = |C'(t)|` (FUN_180162c70; x, y only when dim 2); `dmin = min_step / s`, `dmax = max_step / s`, or 0.001 / 100 when
   `|s| < 1e-12` or `> 1e12` (l.1037-1046).
4. Step `dt = FUN_180162cf0(t)`: `k = |C' x C''| / |C'|` (FUN_180162d60; dim 2: `|x' y'' - y' x''| / |C'|_xy`; 0 when `|C'| < 1e-8`),
   `dt = sqrt(8 tol / k)` (1 when `k <= 1e-8`), clamped to [dmin, dmax]; at most the rest of the range (l.1047-1056).
5. When `dt > 0.1`: probe `u = 0.1, 0.2, ...` (while `u < dt`): if the estimate at `t +- u` is below u, `dt` = the previous u (or
   that estimate when u = 0.1) (l.1057-1079).
6. `t' = t +- dt` (the far end if past it); if t' lies in another span, t' = the span boundary passed: S_ID the integer `span`
   (rev: `span - 1`); NURBS knot[span] (rev: walking down over repeated knots / the start knot, l.1115-1150), when it is more than
   1e-8 from t and inside [t1, t2] (l.1080-1152).
7. Halving (l.1160-1213): `tm = (t' + t) / 2`; `d` = the distance of `C(tm)` to the chord `C(t)`-`C(t')` (dim 2:
   `dist_to_finite_line` with limit 1e38 - perpendicular when the foot is on the chord, else to the nearer end; else
   `dist_pt_l_3d`, the infinite line), `L` = the chord length. `d < 1.1 tol` and `L <= 1.1 max_step` twice in a row -> the first of
   the two; otherwise the count restarts. Stop when `|t' - t| < 1e-6` or `L < min_step` (t' kept); else `t' = tm`.
8. t' clamped to [ent +0x98, ent +0xa0] (l.1214-1225).

Constants (MemDump of mccore): 1e38, 1e-6, 1.1, 0.5, 1e-12, 1e-8, 0.1, 0.001, 100, 1e12, -1 (header of the decompile file).
`nurb_interval` (0x1805b0630, l.1340-1387): the last knot <= t + 1e-10 among indices deg+1 .. nk-deg-1, less one; a zero-length span
-> from the end the first index whose knot is below t - 1e-10.

## 4. The port

- **Evaluation** (`mcspline.c mcs_spline_eval_d`): eval_curve with 1 / 2 derivatives. NURBS: eval_nurbs (0x1805ae2d0,
  `decomp/mccore_curves.c`, param_3 1, param_6 0) with the DLL's arithmetic, `mcs_eval_nurbs` (since 2026-11-12: the transliteration
  mcspline_chain.c had, moved; before, the Piegl / Tiller basis derivatives - equal up to rounding, and the Curve Surface oracle showed
  that one ulp in brk_spline_const_err's steps decides some runs, formats/wireframe.md "Curve Surface"). Parametric spline: eval_spline's
  segment rule (`decomp/surfkinds/evalspline.c`) and the power-basis derivatives.
- `mcs_brk_spline_const_err` = section 3; `cam_chain_polyline` (cam2d.c) = section 2 for E_SPLINE entities, with tol = the caller's
  chord tolerance (Engrave: S+0x28 / 10, `cam2d_engrave.c`), tol_sys = CAM_SYS_TOL 5e-5, curve_min_step by the part's units.
- **Entities** (`mcam_load.c`): records of class 8 / 0x80 that evaluate become `Ent` type **E_SPLINE** (= 8, `cls` 8 or 0x80),
  a / b = the stored start / end points (ent +0x68 / +0x80), `crv` = the CurveRec index, `planar` = every control point
  (NURBS z / w) or every segment's z polynomial at the start's z (ours, the counterpart of the lines' 1e-6 test), in record order
  (database order, which chaining walks). One polyline each for display and picks: the points of `brk_spline_const_err` at
  curve_chord_dev (0.001 in / 0.02 mm) - **ours: Mastercam's spline drawing (mastercam.dll) is NOT DECODED**.
- **Saving**: the records are never rewritten (no spline is created or edited here); `tools/mcam_walk.py` check on a part
  exported after regenerating coaster op 2 in the app: the 22 spline records byte-identical; the op's TP records name the NURBS
  (cls 0x80) like any entity and the exported op regenerates to the same moves.
- **Chaining** (cam2d.c): `chainable()` takes E_SPLINE (planar, not blanked) only with `cam_chain_splines` set, i.e. while the UI
  chains for an **Engrave** op (core_ui `cui_spline_chaining`, set around chain_pick_at / chain_click_pre / window_chains /
  vector_end / chain_act). Joints use a / b like lines and arcs (chain_next = FUN_1801e3de0). Rules applied to splines:
  `ep_n_from_t_sel`'s spline end rule (`|t - t2| <= |t - t1|` -> end 1, chaining.md) via the pick's parameter fraction; a single
  spline whose ends meet is closed (`set_chain_closed` 0x1801e8590: chain start / end closer than chain_tol; lines / arcs keep the
  old rule); the short-entity rule's length (length_ent) = the break-point polyline length at chain_tol (**ours**, length_ent's
  spline branch NOT DECODED). `cam_chain_from_idns` (stored ops) needs nothing new: the TP references now find the spline entities.
- **Ours / NOT DECODED**: the pick's parameter on a spline (nearest point of the break points at chain_tol, interpolated); a
  Dynamic start / end inside a spline (FUN_180284b40 / param_10-11 path) - refused; the AutoCursor points, EntityMinMax, transforms,
  Analyze of splines - splines are left out of them as before (no change).

## 5. Refused / left out (explicit)

| where | behaviour |
|---|---|
| `cam_generate` | a chain holding a spline: refused unless the op is Engrave ("this operation type does not take spline curves here (NOT PORTED; Engrave does)"), the Tplane is Top, the spline is parallel to XY, and no Dynamic cut lies in a spline. Contour / Pocket / Facing / Slot / 2D HS / 3D boundaries: their own spline handling (e.g. chains_to_pock_bounds' linearisation, mcmill_2d_engines.md) is NOT PORTED |
| interactive chaining | since CS-7 (formats/chaining.md "Curves outside the C-plane, splines and NURBS"): splines / NURBS in any plane chainable for every operation (Mastercam's 0x8f mask), the op's generator refusing what it does not take; Extrude and the wireframe chain functions leave them out with a message |
| general selection (pick, window, polygon, Select All, sel:) | splines are not selected (core_ui cui_sel.c `spl_skip`); they are drawn |
| AutoCursor (cui_snap.c), Transform window (cui_xform.c), Blend made here (cam3d_blend.c blend_chain_from_ents) | splines skipped / refused ("a spline curve - not taken here (NOT PORTED)") |
| Dynamic chaining on a spline | message "A Dynamic start / end inside a spline curve is not supported here (NOT PORTED)" |

## 6. Oracle: the app path against the stored NCI (`APP=1 tools/tests/engrave_test.sh`, 2026-10-26)

`cam_cli PART fileop ID --moves-arcs F` (chains from the part's entities, splines broken as above, the op's Line/Arc filter
applied, filtered arcs sampled at 0.0005 for the distances) vs the stored NCI of the X9 `engraved coaster.mcx-9`, inch:

| | op 2 (Spiegel 2D3D) | op 4 (Grund 2D) |
|---|---|---|
| passes / rapids | 16 / 48 (stored 17 / 51) | 6 / 18 (= stored) |
| stored feed points -> app path | p50 0.00001, p95 0.00005, max 0.00180 | p50 0.00001, p95 0.00006, max 0.00021 |
| app feed points -> stored path | p50 0.00001, p95 0.00009, max 0.07673 | p50 0.00001, p95 0.00006, max 0.00013 |
| G0 / G1 / G2 / G3 (app's filter / stored) | 48 / 2295 / 15 / 10 vs 51 / 2294 / 14 / 8 | 18 / 184 / 6 / 4 vs 18 / 186 / 5 / 4 |
| moves per finish pass [G1, arcs] | [25,2] [21,4] [40,8] [66,6] [27,1] [21,4] vs stored [25,2] [19,4] [40,7] [71,5] [24,1] [20,3] | [9,4] [20,3] [7,3] vs [9,4] [22,2] [7,3] |

The finish passes follow the contour, so their move counts check the breaking vertex by vertex: they match to a move or two (the
SUBSTITUTE sampler at 0.0001 gave thousands). The op 2 rough stage misses one short rough pass (stored 8 moves at (-0.166,
-0.828)) and one sharpening descent ends 0.077 off: **an engine-port effect, not the breaking** - the same polylines with their
long straight segments subdivided (no geometric change, max 0.002) give 17 / 51 passes / rapids and max 0.0025 / 0.0019 in the
standalone port; the engine port (`viewer/engrave.c`) depends on vertex density where estamp7 does not (open, engrave_engine.md).

## 7. Open questions

- The engine port's sensitivity to long segments (section 6): which estamp7 step (eg_load_layer / the offset precompute
  FUN_00501550, NOT DECODED) is density-independent.
- Mastercam's own spline drawing and picking (mastercam.dll draw_ent / proj_pt_on_ent for splines), length_ent's spline branch.
- The users' config 1004 / 1005 (curve_min_step / curve_max_step) are taken at their defaults (as chain_tol is).
- The other ops' spline handling (Contour's ctour path with splines_ok = 0 + their own conversion, pocket bounds).
