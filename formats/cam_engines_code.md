# CAM engine code map: cam2d.c / cam2d.h, cam2d_pock.c, pgeo.c / pgeo.h, offset.cpp / offset.h, cam_fileop.c / cam_fileop.h

Code map of the 2D toolpath core that has no other home spec (state 2026-10-16). The engines with their own specs:
Slot / Helix Bore / Circle Mill / Thread Mill (cam2d_ops.c) -> mcmill_2d_engines.md 7; Dynamic glue (cam2d_dyn.c) ->
dynamic.md "Glue code"; mcce port (mcce_dyn.c) -> mcce_port_plan.md; Area Mill (area_mill.c, cam2d_area.c) ->
area_mill.md 9 / 11.5; chaining (cam2d.c chaining section) -> chaining.md "Code"; posting -> post.md "Code"; Reverse
toolpath -> reverse_toolpath.md 7; feeds / speeds -> feeds_speeds.md "Code". Provenance classes (DECODED / CORRELATED /
SUBSTITUTE / IMPROVISED) are those of improvisation_review.md.

## 1. cam2d.h: data model
- `Chain` (chaining.md "Code"). Tolerances: `CAM_CHAIN_TOL` 0.002 / `CAM_CHAIN_TOL_INCH` 0.0001 = config 1001
  chain_tol, `CAM_SYS_TOL` 5e-5 = config 1000 tol (mccore FUN_1803bc7d0 / FUN_1801bab70, decomp/mccore_systol.c),
  `cam_chain_tol(metric)`.
- Op types `CAM_CONTOUR` .. `CAM_PEEL` (13) with gallery labels (`cam_type_label`, `cam_type_from_label`; "Engrave" /
  "Chamfer" map to Contour), `cam_type_uses_holes` (drill / helix / circle / thread pick points or circles).
- `CamParams`: absolute heights (`top_z`, `final_z`, `clearance`, `retract_z`, `feed_plane` incremental above the top),
  feeds / speeds, per-type fields, and blocks copied from the stored or default op record: `mc_p` (record
  0x1228..0x1347, Slot / Helix / Circle / Thread engines), `pk_p` (record 0x1228..0x1447, pocket), `am_*` (Area Mill
  macro values), `mc_*` multipass / depth-cut / clearance / retract-rate fields, `rpatch[]` (Edit Common Parameters
  record patches, opmanres dialog 243). Field comments give the record offsets.
- `CamOp` = params + chains + holes + generated `Move`s (+ `error`). Public functions: `cam_defaults` (base values,
  then `cam_defaults_from_file`), `cam_params_from_tool`, `cam_drill_z`, `chamfer_preview`, `cam_generate`,
  `cam_write_gcode`, sidecar `cam_save_ops` / `cam_write_ops` / `cam_load_ops(_ord)` / `cam_set_param`, `cam_op_free`.

## 2. cam2d.c: generation core
| code | source / class | role |
|---|---|---|
| `MB`, `mv`, `lastp` | ours | move buffer (rapid flag, xyz, feed) |
| `mb_tag_arc` | improvisation_review.md CAM item 18 | marks moves [from, n) as one engine arc (centre, cw) for the post |
| `zlevels` | ours | even depth levels (top - depth x i / n), contour / pocket zigzag / face |
| `hst_depths` | DECODED: FUN_1802ca5f0 / FUN_1802ca490 / FUN_1802ca590 = mccore calc_depth_cuts 0x18089e520 (dynamic.md "Depth cuts") | 2D High Speed and High Speed pocket levels + rough count / step for the depth copies |
| `reverse_path`, `rotate_to`, `pip`, `in_region`, `seg_inside`, `cut_loop`, `ramp_loop` | ours | polygon helpers; `seg_inside` samples every 0.05 |
| `cam_defaults_base` | IMPROVISED fallback (CAM item 19) | built-in values used only when the Mill_*.mcam-defaults file cannot be read, plus our own post options (arcs, cleanup) |
| `cam_params_from_tool` | CToolProp::Recalc mcmill 0x180244700 (mm l.395670-395678) | tool number / dia / shape / feeds; drill cycle + peck from the tool; leads as % of D when record 0x940 |
| `chain_paths` | ours | machining (1) / avoidance (2) / all (0) chains as polylines |
| `offset_open`, `offset_open_trim`, `fix_open_ends`, `chain_offset`, `rolls` | SUBSTITUTE (Clipper) + CORRELATED roll rule (op +0x5d4: turn >= 45 deg rolls, NAKTO op 5) | contour offsets of open / closed chains; mitre wedges when corners are not rolled |
| `LeadS`, `lead_sides`, `lead_in` / `lead_out` / `lead_start` / `lead_fits` / `pass` | DECODED pieces (FUN_1801c24a0 / FUN_180051990, 2026-10-23) + CORRELATED fit (measured; ours for two sides) | contour leads per side (arc on the free side, line tangent or Perpendicular, a line alone, ramp / helix heights), the fit when the gouge check 0x991 is on (shrinking the arc 1 deg at a time when it does not fit); staying down between passes (FUN_180045770's nc_tol rule) |
| `adj_path`, `pl_*` | DECODED: FUN_180050a10 / FUN_180054420 / FUN_1800566e0 | overlap 0x988, adjust start / end of contour Extend / Shorten (0x1320..) |
| `ct_tapered`, `ct_taper_stock` | DECODED: FUN_180056a70 / FUN_1800569a0 | tapered walls: the per-depth wall stock |
| `exact_end_dirs`, `ent_tangent` | CAM stream 2 item 0 (4) | open-chain lead directions from the exact end-entity tangents (mc-test01 op 1) |
| `retract_up` | mccore retract_move / clear_move (CAM stream 2 item 0 (3)) | rapid retract off (0x1a9 = 0): feed at 0x388 to the feed plane, then rapid |
| `chamfer_geometry` | ours (geometry of a cone) | 2D chamfer tool-centre offset and tip Z |
| `gen_contour` | DECODED XY multipasses: mcmill contour loop decomp/mcmill_ctour.c l.1506-1700; depth finish cuts (calc_depth_cuts), taper, By depth order (FUN_1800447e0) | passes per depth = rough count 0x8aa (+ finish 0x8b8 at the last depth or every depth with 0x1230), offsets = remaining finish x 0x8c0 + remaining rough x 0x8b0 + R + stock, warning-0x93b rule; types 2D / 2D chamfer / engrave; tabs |
| `gen_pocket` | dispatch | zigzag (cutting method 0, `pocket_style` 1) with roughing on -> `gen_pocket_zigzag`; everything else -> `gen_pocket_v2` (section 3). The contour-parallel code after these two returns is unreachable (old IMPROVISED engine, item 9) |
| `gen_pocket_zigzag` | CORRELATED (A001) + DECODED helix entry (`pk_entry_helix`, section 3) | passes across the region shrunk by R + stock + finish passes, floor(range / step) + 1 intervals, linked at depth, then the wall finish passes like a contour |
| `gen_drill` | ours; order = stored point records (tp_pt_mgr sorts NOT DECODED) | rapid / feed / rapid per hole (canned cycles are written by the posts) |
| `cam_generate` | | per-part `g_nc_tol` / `g_min_arc` (config 1008 / 1003: 0.002 / 0.02 mm, 0.0001 / 0.0002 in), checks (finite depths, <= 200 levels), dispatch, Clipper error check (`cl_error_take`), no non-finite / > 1e6 output |
| `fmtn`, `words`, `fit_arc`, `arc_words`, `emit_moves`, `cam_write_gcode` | post.md output rules | G-code of ops made here in the user's post style; tagged engine arcs with their own centre in <= 120 deg pieces, other feed runs refitted (<= 120 deg pieces); units from the part's NCI (refused when ops disagree) |
| `PKEYS`, `cam_set_param`, `cam_write_ops`, `cam_load_ops_ord` | ours | the `.mcamui` sidecar: `OP` / `P key value` / `CHAIN closed [A] [S<idn>] ent:rev ...` / `HOLE` / `TAB` / `END` blocks, ops regenerated on load |

Includes at the end of the generators: cam2d_ops.c, cam2d_dyn.c, cam2d_area.c, pgeo.c, cam2d_pock.c (one translation
unit; they use the static helpers above).

## 3. cam2d_pock.c: Pocket engine port
Batch 6 (2026-10-23, formats/opdlg_port_audit.md 10, decode notes formats/pocket_engine_b6.md): the files included by cam2d_pock.c
(one translation unit with cam2d.c):
| file | content | decomp |
|---|---|---|
| cam2d_pock_tspiral.c | method 5 True Spiral `pk_ts_loops` | FUN_1801ef3d0 |
| cam2d_pock_entry.c | the shared roughing entry `pk_entry_plan` / `pk_entry_emit` (PkEntry: plunge / helix / path; skip): helix, follow boundary, ramp, chained-point refusal; `pk_patched` (gates) | _enter_pocket FUN_1801bc830, FUN_1801c1d80, FUN_1801c14b0, FUN_1801c30d0 / c0ad0 / c0e80 |
| cam2d_pock_pspiral.c | methods 2 / 3 `pk_ps_loops_reg`, `pk_ps_cut` (clean corners) | FUN_1801ea750, FUN_1801e9a90, FUN_1801e9420 |
| cam2d_pock_hsx.c | High Speed extra loops (stepover between R and D) `pk_hs_extras`, `pk_hs_cut` | FUN_1801d1740 family, FUN_1801cf8b0 |
| cam2d_pock_fin.c | finish settings `pk_fin_init`, pass bounds `pk_fin_bounds`, lead geometry / checks `pk_fin_leads`, section order `pk_sec_cmp` | FUN_1801cc170, FUN_1801cd200, FUN_1801bdc00, wr_pock_toolpath comparator |
| cam2d_pock_zz.c | methods 6 / 0 (Pockzigr) `pk_zz_path`, `pk_zz_plan`, `pk_zz_depth` | FUN_1801ef830 family |
gen_pocket_v2 (cam2d_pock.c) plans per pocket (loops or zz pieces + entry), then emits roughing / finishing sections in
wr_pock_toolpath's order (`pk_rough_depth`, `pk_fin_section`). pgeo.c gained `gls_merge` (ours: merge_pock_bounds substitute).
Refused (NOT DECODED): methods 1 / 4, burial for 0 / 2 / 3, trochoidal modes, pocket types 1-4, keep tool down, spring passes. (2026-11-12: keep tool down 0x126a / 0x891 ported - pocket_engine_b6.md "Keep tool down 0x126a"; Remachining still refuses it.)
The pre-batch-6 description follows (still right for High Speed loops, helix centre, links).

Status (before batch 6): newest file (2026-10-16 12:59), being written while this map was made; improvisation_review.md item 9 still
reads "NOT DONE" and has no status line for it. Documented as found.
- Source: mcmill GeneratePocketToolpath FUN_1801c5250 (decomp/mcmill_all.c l.313461), hs_parallel_spiral_pocket
  FUN_1801d00d0 (l.320359), find_helix_center FUN_1801c04d0 (l.310452), helix FUN_1801c1d80 (l.311342), finish
  FUN_1801cc170, depth copies wr_pock_depth_cuts (l.315072); mccore pieces in decomp/mccore_pock.c / mccore_pock2.c.
  Parameters: pock_prm = record 0x1228 (`pk_p`): 0x1238 roughing, 0x1239 finishing, 0x123c cutting method, 0x1248
  stepover %, 0x1258 inside -> outside, 0x125a / 0x1260 finish count / spacing, 0x12a0 / 0x12a2 entry on / type,
  0x12f8 / 0x1300 helix min / max radius % D, 0x12b8 helix Z clearance, 0x12c0 plunge angle, 0x12c8 XY clearance,
  0x12d0 helix CCW, 0x12d1 skip pocket on entry failure, 0x1400 corner smoothing length, 0x1408 channel mode, 0x1230 /
  0x1436 finish at every depth; op +0x56b = record 0x5d3 min fillet angle (set_min_fil_angle: 0 -> pi, 1 -> 0.785398,
  else 0.01).
- `gen_pocket_v2`: roughing High Speed (method 8, channel mode 0) or none (record 0x1238 = 0: finish only). Region =
  closed chains as exact loops (`pk_chain_gl`, `pk_region`: nesting depth odd = island); tool-centre bounds = region
  offset by -(R + wall stock) (FUN_1801d4620), one pocket per outer loop with its islands; rough boundary = that by
  finish count x spacing. Loops (`pk_hs_loops`): smoothing (`pk_smooth` = FUN_1801d2b80 / FUN_1801d29b0 opening),
  repeated inward offsets by the stepover (FUN_1801d1850, halved once when empty), next loop chosen by the nearest
  entity start (FUN_1801ce870), merge with islands (`pk_merge`), pending loops, cut order reversed unless 0x1258 = 0,
  start points propagated (FUN_1801d19d0). Loops computed once, cut at every `hst_depths` level. Links between loops:
  a channel / S-shape of two half circles when the gap equals the stepover or half of it (FUN_1801ce310,
  FUN_1801d1010 / FUN_1801d13a0, accepted when `pk_inside_all` = FUN_1801d2c60), else a straight feed when it stays
  inside (`pk_line_inside` = FUN_1801e0fd0), else retract / rapid / plunge. Entry: helix (`pk_helix_center` 11 radii
  max -> min; height top - first depth + Z clearance, sweep from tan(plunge angle)), line to the start must stay inside,
  else plunge at the start (or pocket skipped with 0x12d1). Finish: pass j on the tool-centre bounds offset by
  (n - j - 1) x spacing, at every depth with 0x1230 else the final depth, started at the first chain entity's midpoint
  / the point nearest the last position, lead line + arc on the pocket side when inside the tool-centre region (lead
  geometry MEASURED: Mill.mcam ops 5 / 9, mc-test01 op 2, A001 op 13). Arcs are emitted as tagged true arcs (`pk_gt`,
  `pk_arc_moves`), full turns split.
- `pk_entry_helix`: the same helix entry for the zigzag engine's first pass.
- Refused with NOT DECODED errors: cutting methods other than 8 (0 is the zigzag engine), channel mode != 0, stepover
  between R and D (FUN_1801d1740 extra loops), ramp entry (0x12a2 != 0). NOT DECODED inside: sort_pock_bounds (pocket
  order = chain order), find_path_around_island (link / entry line around islands), FUN_1801cd200 lead checks.
- `PK_DEBUG=1|2` prints region / bound / link diagnostics.

## 3a. cam2d_face.c: Facing engine port (batch 7, 2026-10-23)
Included by cam2d.c after cam2d_pock.c; decode formats/mcmill_2d_engines.md 8, audit formats/opdlg_port_audit.md 11. Entry
`gen_face_mc` (bnci_face 0x180097230), reached from cam2d_ops.c `gen_face` through `face_decoded` (every op with its record since
decisions batch dec1; the measured engine only with FACE_DECODED=0 or without a record). `FW` = the mccore move state (down, first approach, top / init /
retract / clear depth): `fw_set_depths` (set_depths), `fw_adjust` (adjust_depths), `fw_plunge` / `fw_retract` / `fw_clear`
(plunge_move / retract_move / clear_move), `fw_lin` (FUN_1801e32a0), `fw_rapid` (ours: a descending rapid XY first);
`face_auto_angle` (FUN_180096d20), `face_bounds` (chains_to_pock_bounds / FUN_180098f90 / islands 0xa5e), `face_sharpen` (ours:
sharp corners of the along offset by end tangents), `face_span` (pock_bound_line_inters), `face_depths` / `face_z`
(FUN_180098e80), `face_dynamic` (MCCECalcDynPock with bnci_face's CDynPockParm, written by cam2d_dyn.c mcce_moves),
`cam_face_link_feed_on` (MCAM_FACE_LINK_FEED=1). CamParams.ht_inc (cam2d.h) = the heights' incremental flags adjust_depths needs.

## 4. pgeo.c / pgeo.h: exact line / arc loops
- Model: `Gt` = mccore "gt" (type 'L' / 'A' / 'P', line +0x10 / +0x20, arc centre +0x10, radius +0x20, start +0x28,
  signed sweep +0x30; FUN_1801ce540 l.319158, FUN_1801c04d0 l.310452), `Gl` = a closed pock_bound loop (island flag),
  `Gls` = a set. Evaluation as eval_gt_pt / eval_gt_tan / length_gt / t_on_gt (`gt_pt`, `gt_tan`, `gt_len`,
  `gt_t_near`), reorder_pock_bound (`gl_rotate`), `gl_split_start`, exact signed area `gl_area`, `gl_closest`,
  sampling `gl_sample` / `gls_sample`, even-odd `gls_inside`, `gl_cross` (sampled), `gt_cross` (transversal
  intersection, tangency excluded).
- `gls_offset(in, d, min_fil, tol)`: SUBSTITUTE for mccore ccomp_pock / offset_pock_bounds / mcce MCCECalcOffset
  (internals NOT DECODED): Clipper offset of finely sampled loops (step 0.02 tol, at least 2e-5) for the topology; each
  output edge assigned to an exact candidate element (offset line, concentric arc, corner arc about an input vertex);
  loops rebuilt from those elements with exact intersections (`rebuild`); corner arcs turning less than min_fil are
  replaced by a sharp corner (ccomp's minimum fillet angle).
- Used by cam2d_pock.c only.

## 5. offset.cpp / offset.h: Clipper wrapper
C API over Clipper 6.2.0 (vendor/clipper6, Boost licence), coordinates scaled by 1e5. SUBSTITUTE class: standard
polygon offset / boolean operations with the decoded semantics stated by each caller.
`CPath` / `CPaths` (closed polygons, > 0 area = CCW); `cl_offset` (even-odd normalise, round joins; + grows outers),
`cl_offset_nz` (no normalising, non-zero input), `cl_offset_join` (round / sharp / square), `cl_offset_lines` (disc
sweep of polylines), `cl_union_evenodd`, `cl_difference`, `cl_bool` (union / intersect / a - b / xor, non-zero),
`cl_clip_open` (polylines inside / outside a region), `cl_group` (PolyTree: each outer followed by its holes, owner
index), `cl_area`, `cl_total_area`, `cl_free`. Non-finite or > 1e10-unit input points are never passed to Clipper and
any exception sets an error flag read by `cl_error_take` (cam_generate refuses the toolpath).

## 6. cam_fileop.c / cam_fileop.h: stored operations as CamOps
- `cam_fileop_type`: opcode -> type (1 Contour, 3 Pocket, 2 Drill, 100 Thread Mill, 102 / 4 Face, 105 Slot, 106 Helix
  Bore, 18 Circle Mill, 134 by style S16 0x1228: 0 Dynamic, 1 Area, 2 Dynamic Contour, 3 Peel).
- `cam_op_from_file`: the OP_ID record (>= 0x1600 bytes) -> CamParams (`params_from_record`) and the TP records ->
  chains (kind 6 header, kind 7 entities, role byte), tabs (kind 30), holes (kinds 1 / 27; the point record's own xyz
  +0x98; kind 27 on a solid = the smallest coaxial cylinder `part_cylinders`); nothing dropped quietly (limits 256
  chains / 16 tabs -> the op stays as stored); 2D High Speed taper with depth cuts refused. `op_tool` finds the tool by
  slot (op +0x5ac = TL_ID +0x2cc) then number. Solid chains: `cam_solid_chain_ents` (outer loop of the solid's top
  face via `xt_top_loop` (xt_mesh.h, mm), scaled to part units by `xt_part_scale`; edges appended to F.ents). `cam_chain_from_idns` builds chains with `cam_chain_tol`.
- `params_from_record`: common heights / feeds / tool (P10005 +0x350 dia, P10027 / 10028 top, P10029 depth ...), leads
  (0x8f0; entry 0x8f8.. / exit 0x940.. each side its own, abs from % x D per side as bnci_ctour; overlap / midpoint / gouge
  0x988-0x991; contour adjust start / end 0x1320.., taper 0x130a / 0x1310, order 0x1318, finish cuts 0x878 / 0x880), contour multipasses / tabs / chamfer / compensation side, pocket
  (`pk_p` block, 0x1238 roughing, 0x1a9 / 0x388), drill (cycle 0x1228, peck 0x1230, tip comp 0x1268, R plane 0x190 /
  0x198), face (0x1228 style, 0x1240 / 0x1250 / 0x1260 / 0x1290, automatic angle 0x1288), slot / helix / circle /
  thread fields (mcmill_2d_engines.md), Dynamic (LoadDynPockParm subset; stepover 0x1730 when 0x1700 and 0x1701,
  decomp/mcmill_hst2d_dyn.c l.1612-1624), Area (`area_mill_macro`), 2D High Speed depth cuts 0x868-0x880. Record offset
  = operation offset + 0x68.
- Defaults: `cam_defaults_from_file` / `cam_default_op_record` = the first op of the type in the shared Mill_MM /
  Mill_Inch.mcam-defaults (CAM item 19; every built-in default a type uses is overwritten).
- Edit Common Parameters (`CamCommonEdit`, `cam_common_apply` = mastercam.dll CEditCommonDlg apply FUN_180145b50,
  `cam_common_drop_heights`): writes the dialog's fields as record patches (op +X = record +0x68+X; field offsets in the
  code comment) and maps the heights back with params_from_record's formulas.
- Regression: `tools/tests/fileop_all.sh OUTDIR` (every stored op of every user part regenerated with cam_cli into
  OUTDIR/<part>_<id>.m; the review runs compare that with ~/mastercam-re/regress/base), `tools/tests/safety.py DIR`
  (no rapid below the top of stock), `tools/tests/orfiles.py` (oracle metrics of such a directory).

## 7. NOT DECODED / open (this file's scope)
- Contour / pocket / Area Clearance arcs are Clipper polygons: an arc-exact MCCECalcOffset / ccomp_pock (only the
  pocket engine's pgeo rebuild is exact).
- Pocket: cutting methods 1-6, channel modes, stepover between R and D, ramp entry, sort_pock_bounds,
  find_path_around_island, FUN_1801cd200 lead checks; open pockets (process_open_pocket).
- Drill point order (tp_pt_mgr sorts); set_depths per chain (heights are evaluated once at the first chain's Z).
- Face: no decompile cited (CORRELATED only).
