# Provenance review: decoded vs improvised (2026-10-16)

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

Three read-only audits of every code file (CAM engines, UI, tools / data / solids). Classes: DECODED (cites a decompile
address / resource / spec section that matches), CORRELATED (measured against Mastercam data with stated evidence),
SUBSTITUTE (our implementation of a decoded standard operation, e.g. Clipper offset), IMPROVISED (no source), STUB
(NOT DECODED, neutral / greyed). Citations were spot-checked against decomp/*.c, the C# and formats/*.md: all checked
ones exist and match.

| area | blocks | DECODED | CORRELATED | SUBSTITUTE | STUB | IMPROVISED |
|---|---|---|---|---|---|---|
| CAM engines (cam2d*, mcce_dyn, area_mill, cam_fileop, nc_post, nci_reverse) | 120 | 30 | 24 | 10 | 9 | 47 |
| UI (mcamui.c, backplot_ui.inc, toolmgr_ui.inc) | 192 | 57 | 13 | – | 17 | 105 |
| Tools / data / solids (toollib, toolwiz, toolprofile, part_tools, feedspeed, mcam_load/write/saveops, solid25, scene, xt_mesh, rpx) | 152 | 62 | 13 | 17 | 9 | 51 |
| **total** | **464** | **149** | **50** | **27** | **35** | **203** |

Well sourced: mcce Dynamic port, Slot / Helix Bore / Circle Mill, Reverse toolpath, tool wizard logic, tool profiles,
part tools reader, feeds / speeds, backplot state, Tool Manager behaviour, Extrude flow / panel, wireframe live pattern.

## Fixed during the review (2026-10-16)
1. Dynamic Mill rapid plunge into stock (cam2d_dyn.c mcce_moves): every one of the 35 Dynamic ops had 1-3 rapids
   below Z0 (e.g. G0 to Z-10 at the entry). Now rapid only to the feed plane, then plunge feed. (Mastercam's
   FUN_1801afb70 / FUN_1801afb60 still NOT DECODED.) Baseline refreshed.
2. .tooldb coolant overwritten on save (toollib.c): Mist / Thru / undecoded values (mill_inch: 260 rows of value 1)
   were rewritten as Flood / Off. The raw TlCoolant.OldStyle is now kept unless the flood flag is changed.

## Highest-impact IMPROVISED items still in the code (to replace next)
Safety / data integrity first, then what the user sees most.

| # | where | what is improvised | needed |
|---|---|---|---|
| 1 | cam_fileop.c:185, cam2d.c:605 | 2D High Speed ops ignore depth cuts (one level at final depth) | decode CHST2DProcess::ComputeOffsetsAtDepth (mcmill) |
| 2 | mcam_saveops.c:301-308 | exported op's tool copy only partly patched (retract, coolant, flutes, material, metric stale); no tool records written | write the full decoded 0x288 block; decode TlMgr::Add / FUN_1802cfb60 |
| 3 | mcam_saveops.c:79-162 | new ops copied from the user's other parts; new part = an emptied user part | use config 2303 defaults library only; port init_db (decomp/mastercam_filenew.c) |
| 4 | mcam_saveops.c:294-332, 376-384 | exported constants / fallbacks (retract top+fp+3, Z stock 0, peck2 = peck1, +0x878 / +0x8aa / +0x1381, lead sweep 90°) and our climb chain-reversal rule | map params 10066 / 15560 / 16003 / 10068; decode chain direction from compensation |
| 5 | mcamui.c:4496-4504 | new ops get invented heights (depth top-3, retract +2, clearance +25), undoing the 2026-10-14 defaults fix | keep the Mill_MM default op values; decode abs/inc height init |
| 6 | mcamui.c:2120-2144 | wireframe panel defaults always metric (inch part: Bolt radius 100 in) | inch values are already in wireframe.md (RegisterVariable pairs) |
| 7 | cam2d_dyn.c (old gen_dynamic) | still the default for Dynamic Contour, Peel, Area Core Roughing: invented pattern and constants | mcce FUN_1800930c0 + band kernels; PeelMill.cpp roles; McHmm plans 0x1460 / 0x1461 |
| 8 | cam2d_area.c:102-110 | Area Clearance links = one straight feed at depth (can cross islands / stock) | McHmm Area Clearance Linking Plan 0x141d |
| 9 | cam2d.c:1208-1359 | contour-parallel pocket engine invented | mccore pock_assoc / ccomp_pock path for opcode 3 |
| 10 | mcamui.c:6984-7161 | op parameter dialog = one invented window; Linking page lacks abs/inc and Clearance on/off; Retract only for Drill | decode the mcmill page tree + page 226 controls |
| 11 | mcamui.c:3222-3295 | wireframe function panel = invented floating window, labels not from BAML; one shared gp struct | dump IWireframe*Manager BAML (mccad.ui.dll / mccadutil.ui.dll); per-panel keys |
| 12 | mcamui.c:1046-1059 | Solids manager list invented ("made here", "Delete last feature", stock line) | decode the Solids manager (mcgeomsld.ui) |
| 13 | mcamui.c:615-627, 4946, 5365 | reverse-engineering notes visible in the UI (Command window, "Coolant: flood (TlCoolant.OldStyle 16)", "(default library: NOT DECODED)") | remove; decode the Coolant dialog |
| 14 | mcamui.c:1694-1770 | own file browser, invented titles / filters / places | decode RunFileOpenDlg / RunFileSaveDlg titles and filters |
| 15 | mcamui.c:6834-6906 | Chaining dialog / Chaining Options drawn as invented windows | render templates 1058 / 405 with their bitmaps |
| 16 | mcamui.c:4382-4389 | drill point selection by click-toggle with green crosses | Drill Point Manager (tp_pt_mgr) |
| 17 | ~150 message texts (mcamui.c) | texts with no string-table source | decode Mastercam's behaviour per case (list in the UI audit) |
| 18 | nc_post.c:102-127, cam2d_ops.c:172 | chord refit of our ops' arcs | output the engines' true arc records |
| 19 | cam2d.c:588-645 | built-in defaults (rpm 8000, feed 600, leads 0.5 D / 0.25 D, wall stock 3) when the defaults file maps nothing | map the remaining record fields from Mill_*.mcam-defaults |
| 20 | solid25.c | slab kernel: draft approximated by 24 slabs, draft taper sense and thin-wall side unknown, non-XY planes refused | Parasolid sld_extrude sense; a real solid kernel |

Lower-impact items (look-and-feel metrics, safety caps, unit guesses, tolerance constants, report page defaults,
display fallbacks) are listed in the three audit tables; summary by audit:
- CAM: unit-blind thresholds (tiny-arc 0.01, CAM_CHAIN_TOL 0.0025, area grid floor), conflicting tol_exref values
  (5e-5 vs 1e-7), stale comments (Clipper2 vs 6.2, 1.3 vs 1.5 width limit), stepover 0x16c8 branch dropped
  (cam2d_dyn.c:476), mcce numerics (g_chord, eqtol, sliver threshold).
- UI: ribbon / tab / backstage metrics, title "apprenticeCAM" and About / risk dialog (intentional), keyboard-shortcuts
  window lists removed behaviours, Print saves a BMP, Grid Settings window invented, Tool Manager type names.
- Data: tool type names (toollib.c:14-21, Mastercam's are ids 5014..5412), tool_is_drill filter, new .tooldb from
  DatabaseDefaults instead of SQL:create + SQL:base, header bytes taken from samples, missing-tool display fallback,
  point marker size, simulation tool wireframe / alpha.

## Spec / code inconsistencies found
- mcam_write.md:350 vs toollib_net.md 3.5 (coolant / flute bytes) - toollib_net is decoded, mcam_write.md is stale.
- tooldb.md "1 = on, 2 = off" coolant vs toollib_net.md 6.
- mcam_load.h:138 says machine record "sub 1"; code and feeds_speeds.md use sub 22.
- provenance_audit.md claims removed: "op reversed" message, Shift fine rotation / Ctrl+Up stepping (still in code).
- extrude.md "Differences" list is out of date (most items now implemented); wireframe.md:151 says Chamfer Esc removes
  (code commits, as decoded).
- area_mill.md:401 bottleneck margin formula vs fixed 0.00022 in area_mill.c:388.
- FUN_180337bb0 (CalcFpt) decompile now in decomp/mccore_calc_fs.c (was scratch only).

Resolution (documentation pass 2026-10-16, each checked against the code): mcam_write.md:350 marked superseded by its
section 8; tooldb.md coolant now agrees with toollib_net.md 6 and toollib.c (raw OldStyle kept unless the flood flag
changes); mcam_load.h now says sub 22; the provenance_audit.md removals are now true in the code (view_key has no Shift
/ Ctrl stepping, no "reversed" message; see its "Corrections" section); extrude.md "Differences" and wireframe.md
Chamfer Esc updated by the UI documentation pass; area_mill.md: the code already uses min((hi - lo) / 3, 0.00022 x
unit) (CAM small items part 1 (b)), so spec and code agree - explained in area_mill.md 11.5.

## Replacement run (started 2026-10-16)
Streams (one agent per stream at a time; file ownership avoids edit conflicts):
- CAM (cam2d*.c, cam_fileop.c, mcce_dyn.c, area_mill.c, cam2d_area.c, nc_post.c): items 1, 19, then 7, 8, 9, 18, CAM small items.
- EXPORT (mcam_saveops.c, mcam_write.c, toollib.c new-library path): items 2, 3, 4, data small items.
- UI (mcamui.c, *.inc, solid25.c): items 5, 6, 13 first, then 10, 11, 12, 14, 15, 16, 17, 20, UI small items.
Status lines are appended below as items finish.
- UI item 5 DONE (2026-10-16): open_params' invented heights (top = chain / stock top, depth top-3 / -5 / -0.5, R plane
  top+2, clearance top+25) removed. heights_from_default_op (mcamui.c) evaluates the Mill_MM / Mill_Inch default op's
  height fields with mccore set_depths' rule (decomp/mccore_depths.c @18089f680): inc depth / top relative to the first
  chain's (or point's) Z, feed plane / retract / clearance inc relative to top, floor stock +0x820 except Facing, the
  per-opcode below-tol offset. Mastercam does not change heights at op creation; the flags stay in the op and are
  evaluated per chain at generation. Deviation: our CamParams are absolute, evaluated once at the first chain's Z
  (multi-Z chains get one depth); clearance off -> rapids at retract / feed plane (no "no clearance" motion here).
- CAM item 1 DONE (2026-10-16): 2D High Speed depth cuts decoded (formats/dynamic.md "Depth cuts": CHST2DProcess
  SetOperationID / SetParams, CHST2DDepthCuts FUN_1802ca5f0 / FUN_1802ca490 / FUN_1802ca590 = mccore calc_depth_cuts
  0x18089e520, DCS copy work item FUN_1802b11f0 / FUN_1802bb260; decompiles decomp/mcmill_hst2d.c, decomp/mccore_depths.c).
  ComputeOffsetsAtDepth is the taper wall offset, not the level list. Port: cam2d.c hst_depths (record 0x868 / 0x870 /
  0x878 / 0x880, cam_fileop.c), Dynamic Mill and Area Clearance run the engine once per region and append Z-shifted
  copies (FUN_1802bb260); Dynamic Contour / Peel / Core Roughing (old generator) use the decoded levels. NOT DECODED:
  island-depth island filter (islands kept), taper + depth cuts (op refused, stays as stored). Oracle: unchanged - no
  stored op has more than one level (BRACKET4 ops 14 / 16: 0x868 on with max 0 -> one level, decoded). Fileop regression
  378/378 identical, post 0/16. Multi-level checked by hand (buckle-v10 ops 1 / 2 with --set stepdown, finish cut).
- UI item 13 DONE (2026-10-16): the "Command" reverse-engineering window is gone; ribbon / menu / gallery commands this
  program has not ported are drawn greyed and do nothing (run_local_x probe; wireframe names only for MCCAD*.DLL
  commands, so Toolpaths > Trim is greyed). The invented "... is not available here (no splines ...)" message removed.
  Tool wizard: Coolant button -> Coolant dialog from toolmanager.database CoolantView.baml / CoolantViewModel /
  TlCoolantViewModel / CoolantParameters() / CoolantStrings.resx (Standard Coolant Flood / Mist / Thru-tool On / Off on
  TlCoolant.OldStyle 16 / 32 / 64, Off = 8, the enable rules, OK / Cancel / Esc); Custom Coolant rows and Help greyed
  (TlCoolant Flood..Custom7Pos not kept in our tool record - needs toollib.c / mcam_load.h fields, EXPORT stream).
  "(default library: NOT DECODED)" gone: material not found -> nothing changed, silently (calc_op_fs); mccoreres 4038
  "Your default work piece material does not exist!" belongs to calc_op_fs's show flag, not traced for these callers.
  The wizard's "Metric" checkbox is NOT invented: toolpropertiesview.baml row 6 CheckBox ToolMetric "Metric"
  (IsChecked = EntitySelected.IsMetric) - kept, with its tooltip. Screenshots 2026-10-16_coolant_dialog.png,
  2026-10-16_ribbon_greyed_commands.png.
- CAM item 19 DONE (2026-10-16): every built-in default that a type uses is now overwritten by its Mill_MM / Mill_Inch
  default op (all 12 types have one in both files; cam_fileop.c params_from_record). New mappings: contour chamfer
  width / tip 0x1288 / 0x1290 and tab width / height 0x1388 / 0x1390 always (were only when chamfer / tabs on); 2D High
  Speed depth cuts 0x868-0x880 (item 1). Leads: the invented 0.5 D / 0.25 D are gone; with leads on (0x8f0) and 0x940
  the line / arc follow the tool as 0x978 / 0x980 % of D (CToolProp::Recalc mcmill 0x180244700, mm l.395670-395678;
  bnci_ctour l.47474-47480 does the same at generation; holds on all 12 stored ops with leads), also when the tool
  changes and for new ops (Mill_MM contour / pocket: 100 % / 100 %). The "pointed tool switches a contour to 2D
  chamfer" rule is removed (no source; the type is the op's 0x1308; the UI's own variant already overrides it). What is
  left in cam_defaults_base is only the fallback when the defaults file cannot be read, plus our own post options
  (arcs, cleanup). Fileop regression 378/378 identical, post 0/16.
- CAM small items, part 1 (2026-10-16): (a) tolerances decoded - mccore FUN_1803bc7d0 (defaults) / FUN_1801bab70
  (config key -> global), decomp/mccore_systol.c: config 1000 tol = 5e-5 in both units (the mcce port's 1e-7 replaced),
  1001 chain_tol metric 0.002 / inch 0.0001 (CAM_CHAIN_TOL was 0.0025 for every unit; now cam_chain_tol(metric)),
  1003 min_arc_length 0.02 / 0.0002 and 1008 nc_tol 0.002 / 0.0001 (cam2d_ops.c used the inch values 0.001 / 1e-4 for
  every part; now per unit, g_nc_tol / g_min_arc). The user's mcamxm.config holds the same metric values. (b) unit-blind
  thresholds: post tiny-arc 0.01 mm and the Area grid floor 0.01 mm now scale for inch parts; RadAvoidBottleneck
  0.00022 mm scaled and used as min((hi - lo) / 3, 0.00022) (hmm-vorlib l.14994; fixes the area_mill.md:401 vs code
  inconsistency). (c) stepover record 0x1730 when 0x1700 and 0x1701 (LoadDynPockParm decomp/mcmill_hst2d_dyn.c
  l.1612-1624) restored in cam_fileop.c (no stored op uses it). (d) a new Dynamic op's engine record is Mastercam's
  default op record (Mill_MM / Mill_Inch.mcam-defaults, cam_default_op_record) with the op's tool, not zeros.
  (e) stale comments fixed (Clipper 6.2; 1.5 x stepover). (f) drill points: the invented nearest-neighbour re-sort is
  removed - the op's point order is kept (stored ops: Mastercam's point records = its NCI order); tp_pt_mgr sorts NOT
  DECODED. Oracle dynamic (35 ops, chain_tol sliver 0.05 -> 0.04): p95 median 0.461 -> 0.462, worst 1.496 -> 1.496,
  material missed / extra unchanged = holds; per op +-0.2 both ways. Fileop: the 35 Dynamic ops changed (accepted, copied
  into ~/mastercam-re/regress/base), all others identical; post 0/16; no rapid below the stock top (189 outputs checked).
- UI items 6 + 11 DONE (2026-10-16): wireframe function panels rebuilt from the WPF views (viewer/geopanel_ui.inc;
  outlines in formats/ui_wireframe_panels.txt by the new tools/baml_panel.py from mccad.ui / mccadutil.ui BAML, now
  decompiled in ~/mastercam-re/dotnet/mccad.ui etc.): FunctionPanel OK / Apply (OK-New) / Cancel / Help (greyed),
  "Basic" tab, AdvancedExpander headers, radio / check / EvaluatorBox (+ lock) controls in BAML order and text. One
  parameter set per command (gps[]), defaults per unit system from the native managers' registered keys
  (decomp/mccad_panel_keys.c, decomp/mccadutil_panel_keys.c via tools/ghidra_scripts/StringRefsDecomp.java: e.g. Arc
  Tangent radius 1 in / 25 mm, Line Bisect / Perpendicular 1 / 25, Fillet / Chamfer 0.25 / 5, Trim extension 0.1 / 1,
  Break Many exact / rounded 0.1 / 0.5, Arc Polar radius 0.1, Point Segment distance 0 / number 0) and BoltCircleParams
  (5 in / 100 mm, hole radius 0.25 / 5), HELIX / SPIRAL RegisterVariable pairs, offset 1 / 25, offset chains 0.5 / 10.
  Keyed values persist in <config dir>/panel_keys.tsv (Mastercam's own storage NOT DECODED). Locks now decide like the
  decomp (Line Endpoints: length + angle locked or H / V + length; Line Parallel / Perpendicular distance / length;
  Circle / Arc Endpoints radius; Rectangle width / height). Implemented from decoded rules on the way: Line Parallel
  and Perpendicular Direction (selected / opposite / both), Line Endpoints H / V +-L by cursor side, Arc Polar Endpoints
  "End point", Point Segment distance mode, Bolt Circle Full Circle off (angle between) + Reference Circle, Bounding Box
  corner points. Removed (invented): cursor read-out, typed X / Y "Add point", Level / Colour fields, "Grid snap" and
  "Restart" / "Done" buttons, "N entities selected", Bounding Box "Expand", Line Bisect's 50 mm fallback length.
  Greyed (not ported): every Reselect / 1 / 2 / 3 toggle, Tangent modes, Thread / Cut Point, Arc dynamic, Line Bisect
  Multiple, Fillet styles other than Normal, Fillet Chains +/- Sweeps, Break Many tolerance / distance methods and
  Keep / Blank, Trim / Trim Many Break and Opposite side, Bounding Box cylinder / all shown / solids, Axis Offset.
  MFC-dialog commands (Bolt Circle, Polygon, Rect Shapes, Ellipse, Helix, Spiral, Offset, Offset Contour, Project):
  group titles / check / radio texts from the templates, value labels = decoded parameter names (their bitmap tooltips
  NOT DECODED). AutoCursor Fast Point (typed coordinates) is NOT DONE. Screenshot 2026-10-16_wireframe_function_panels.png.
- EXPORT item 2 (2026-10-16): DONE. Decoded (decomp/mccore_tooladd.c, formats/mcam_write.md 8): an op names its tool by
  TlMgr slot (op +0x5ac) and carries the tool's op_tool_info (+0x348..+0x5cf); on load FUN_1802cf2a0 collects TL_ID
  bodies and FUN_1802ce370 runs TlMgr::Add(tp_tool) (CreateNewFromOldTool + SetupNewAssembly) for each slot the TlMgr
  lacks; free slot = TpToolList::OpenSlot + VerifyOpenSlot; the block = TlTool / TlToolMill / subclass GetOpToolInfo,
  the TL_ID body = SetToOldTool. mcam_saveops.c writes the whole block (toollib.c tool_op_info: rebuilds all 62 user
  TL_ID bodies byte for byte; coolant +0x50 / flutes +0x52 included; mcam_write.md:350 corrected) and, for a tool the
  part lacks, a TL_ID record (tool_tp_tool, slot by the OpenSlot rule). Tested: 189/189 user ops exported unedited
  regenerate identically, their tool blocks unchanged; new ops with a part tool (block = TL body + op feeds) and with a
  library tool (new TL_ID, slot 2). NOT DECODED: legacy holder size (+0x2b0 / +0x2b8; Mastercam gives such a tool its
  default holder), TlToolMill +0x8f0 block, canned-text coolant, tp_tool +0x3c8, op +0x5a8.
- EXPORT item 3 (2026-10-16): DONE. New ops are copies of config 2303's defaults library op (user CONFIG mcamxm.config /
  mcamx.config by units); the user's other parts are no longer read. New part = that library (its machine group is
  config 25000's machine, checked) emptied: geometry, solids, OP / TP / TL / NCI_ID, the ops' holder records (ASSOC 31)
  and NCI dropped, TlMgr archive emptied (TlMgr::DeserializeV5 layout). Op +0x1210 (holder record idn of the template's
  file) cleared on new ops. Tested: new part with a contour + drill on new geometry reads back (cam_cli, mcam_walk.py,
  mcam_export.py). NOT DECODED: thumbnail / description / history records of a new part (library's kept).
- EXPORT item 4 (2026-10-16): DONE. P10066 +0x878 (contour / pocket 0 = cam2d zlevels has no finish cuts; slot / 2D HS
  the op's), P15560 +0x8aa = 1 (mcmill contour loop decomp/mcmill_ctour.c l.1506-1580: one rough pass per depth),
  P16003 +0x1381 auto unless tab records are copied, P10068 +0x888 kept with depth = final - stock (cam_fileop
  reader), retract off keeps the stored value, P10109 subsequent peck / P10110 clearance kept; heights / depth cuts /
  multipass / leads / tabs / pecks keep their stored form when it already gives our value (unedited ops now differ only
  in NCI position and the dirty flag); Thread Mill heights written to its own fields (0x1230..0x1250). Chain direction:
  Mastercam keeps chains as chained (config 6001 / 6002) and cuts along them with comp left / right: side_rel ops as
  chained; outside / inside contours given cam2d's cut direction with comp left; other op types (pockets, 2D HS) no
  longer reversed.
- UI item 10 DONE (2026-10-16): the op parameter dialog is now Mastercam's ToolpathDlg layout (viewer/opparams_ui.inc):
  title "2D Toolpaths - Contour" etc. (FUN_18026dc70 + GetToolpathTypeName overrides FUN_18026d220 / FUN_180270050 /
  FUN_18026ea30; drill = "2D Toolpaths - Drill/Circles <cycle>"), the page tree of ToolpathTree builder FUN_18014a700
  with each op type's pages from MakeSpecificPages (ChainBased FUN_18026d480, PointBased FUN_1802701e0, HST2D
  FUN_18026eb40; vtable slots resolved with VtSlot.java) x the page factories' NULL cases (FUN_18026c800 /
  FUN_18026f760 / FUN_18026e210); decompiles kept in decomp/mccore_tpdlg.c, decomp/mccore_tptree.c. Linking Parameters
  = mcmillres dialog 25596 (drill 25593): Clearance check + value + Absolute / Incremental + "Use clearance only at the
  start and end of operation", Retract check + value + Abs / Inc (every 2D op; drill: the feed plane field, no switch),
  Feed plane, Top of stock, Depth with Abs / Inc; incremental shown relative to top / chain (set_depths). The flags come
  from the default op / stored record / the op's own height bytes and are written with the op at OK as height
  record patches, so an exported new op now carries Mastercam's flags and values (verified: exported Contour / Drill =
  Mill_MM default op 3 / 6 heights). Pages not ported are listed greyed ("not available in this program"): Toolpath
  Type, Holder, Stock, Break Through, Home / Ref. Points, Planes, Canned Text, Misc Values, Axis Control, Tab Cutoff, and
  for HST Trochoidal / Transitions / HST Leads / Contour Wall. NOT DECODED: the HST tree builder FUN_18014c4a0 nesting
  (the chain builder's order is used), the post text for the drill title, Clearance / Retract / ... pick buttons, the
  page bitmap (static 624), Help. Screenshot 2026-10-16_op_dialog_linking.png. Makefile: *.inc dependencies added.
- CAM small items, part 2 (2026-10-16): (g) Thread Mill ported from bnci_threadmill 0x180234c80 -> FUN_1802374d0 ->
  FUN_180236810 / FUN_180235a80 / FUN_180235e90 / FUN_180236170 (decomp/mcmill_all.c l.385055-386575, lost arguments
  from objdump, cited in cam2d_ops.c): radial passes (rough / finish / spring, offsets on the diameter), active teeth
  Z passes, start angle, entry / exit arcs with inset 0x1270 and quarter-pitch helical ends 0x1280 / 0x1281, lead
  points, the thread's own heights 0x1230 / 0x1238, top / bottom from the point Z with 0x1250 (was: top + depth),
  retract rate / rapid retract, finish feed 0x12b8, points without a circle use 0x1298. NOT ported (refused): taper
  0x12a8, control compensation; the UI's "thread depth" / "full-profile tool" fields are not Mastercam's and are
  unused (UI stream). Oracle thread (4 ops): unchanged p95 median 0.067 / worst 0.082 = the oracle's 0.1 mm sampling
  of 5-degree chords (the paths coincide with the NCI; first rapid now at the 0x1230 clearance as the NCI). Accepted:
  BRACKET4_18 / _19, thread-test_2 / _4 copied into base. (h) mccore plunge_move 0x18089ed00 / retract_move 0x18089f390
  / clear_move 0x18089e6a0 and set_depths' init_depth (decomp/mccore_moves.c, mccore_depths.c) ported for the Slot /
  Helix Bore / Circle Mill engines (mw_* in cam2d_ops.c; param_5 from the callers FUN_180218410 / FUN_180145ba0 /
  FUN_180042070): retract-rate feed up to the feed plane unless rapid retract, then retract / clearance / feed plane by
  their on flags. No stored op of these types exists (fileop unchanged); post 0/16; no rapid below the stock top.
- CAM small items, part 3 (2026-10-16): (i) mcce glue FUN_1801afb70 / FUN_1801afb60 DECODED: they write no motion
  (set_heights / return 1, decomp/mcmill_hst2d_dyn.c l.6044-6070); OutputMotionToBnci writes each engine record as G0
  (record flag 0) or G1 to its own XYZ (decomp/mcmill_hst2d.c l.4445 / l.4534). Mastercam's NCI has vertical G0 retracts
  from the floor and, on from-outside ops, G0 descents into air (buckle-v6 op 31: Z10 -> Z-0.5). Our engine's rapid
  records are not proven to lie in air, so the SAFE interim rule (no rapid below the feed plane; plunge feed down) is
  kept, now documented in cam2d_dyn.c mcce_moves. All CAM small items are done except: tp_pt_mgr sorts (NOT DECODED;
  stored order kept), mcce numerics (g_chord / eqtol / sliver are the port's substitutes, mcce_port_plan.md).
- CAM item 8 (2026-10-16): Area Clearance links still NOT DECODED, made SAFE. Findings: CAreaClearanceLinkingPlan
  0x141d (vftable 1807987b0) only registers parameters (hmm-link l.12318 FUN_1803a9720 etc.); the links are made by the
  shared EvaGeneralLinkerPlan (vftable 180795ba8, evaluate FUN_1803a0c50 hmm-link l.7153) - a model-based 3D linker
  (CLinkMovementConnect vftable 180774f60; its FUN_1801d4990 "WanderedPosition" bisects link positions against the
  tool-box model, decomp/hmm_linkconnect.c; CWeave / CFragAlgorithms / EvaFragAdjacency order the fragments). Reference
  (buckle-v10 op 2 NCI): stay-down S links of straight lines with ProfileLinkRadius (0.371) fillets tangent to both
  passes. Porting it needs the tool-box model collision and the weave ordering: not done. Port (cam2d_area.c): the
  straight feed link is kept only where it stays inside the tool-centre region (cannot touch a wall, island or the
  stock to leave); otherwise feed-plane retract, rapid, plunge feed. Area ops unchanged on the 3 stored Area Clearance
  ops (their links are inside), fileop / post unchanged.
- UI item 12 DONE (2026-10-16): Solids manager rebuilt from mastercam.dll's history tree (viewer/solidsmgr_ui.inc;
  decompiles decomp/mastercam_histdlg.c, decomp/mastercam_solidsmgr.c): solid rows "Solid" (opmanres 27600 when the
  description is empty) with image 0 of bitmap 749, operation rows under the body they made / changed, named by the
  feature name or the type name (27601-27603 Extrude Cut / Boss / Extrude), images type * 4 + 8 / + 10 from the
  per-type strips (730 / 736 / 733, table 0x18087e520); the right-click menu of FUN_180230a10 (submenus "Solids" /
  "Model Prep" with every item and separator, item commands Delete / Suppress / Highlight / Rename, Delete / Rename /
  Duplicate Solid / Attributes / Check solid... / Remove solid history, Regen Solid / Regen All Solids / Expand all /
  Collapse all / Auto-Highlight / Edit Parameters / Recreate trimmed bodies). Working: Solids > Extrude..., Delete on
  the last feature, Regen All Solids, Expand / Collapse all; the rest greyed. Removed: "(made here, volume ...)", the
  "Delete last feature" button, the solid-mesh diagnostics line, the "Stock (solid box)" line and "Show stock box"
  (the stock box was the solids' bounding box - invented; now not drawn: the machine group stock origin / display
  flag are NOT DECODED). NOT DECODED: the part solids' description and history operations (no children shown), which
  menu group belongs to solid vs op rows (inferred from the item kinds), op flags / dirty images, suppress. Note:
  Facing without chains still faces the solids' box (open_params) - Mastercam uses the defined stock, whose origin is
  NOT DECODED. Screenshot 2026-10-16_solids_manager.png.
- UI item 14 DONE (2026-10-16): file dialogs take Mastercam's titles and OPENFILENAME filters (decomp/
  mastercam_filedlg.c: mccore OpenFile / FUN_1803552e0, SaveFile / FUN_180355810, PromptForFileToOpen /
  PromptForFileToSave, mastercam.dll RunFileSaveDlg -> FUN_18029fb80): Open = mccoreres 7283 "Open" with "Mastercam
  Files (*.mcam)", "Mastercam X Files (*.mcx*)", "All Mastercam Files (*.mc*)" (default, remembered like
  DAT_1812dcd98), "All Files (*.*)"; Save As = 7284 "Save As" with "Mastercam 2017 Files (*.mcam)" and "All Files
  (*.*)"; tool libraries "TOOLDB Files (*.tooldb)" (PromptForFileToOpen's "<EXT> Files" rule). The project type
  "apprenticeCAM Project (*.mcamui)" is ours (intentional). Removed (invented): the Home / Part folder / CAD parts /
  Posts / Shared tools place buttons, "Show all files", the descriptive lines under the list ("Workbench project ...
  the part file is not modified", "New MCAM part: a copy ..."). The window itself is a host file browser (path field,
  Up / Go, list with sizes) - the Windows common dialog and its "Import" / "Export" filter lists (FUN_18034c200) are
  NOT DECODED / not reproduced; initial folder: GetDefaultPath(".mcx") NOT DECODED (the part's folder is used).
  Screenshot 2026-10-16_file_open_dialog.png.
- CAM item 7 (2026-10-16): the invented gen_dynamic pattern is REMOVED (cam2d_dyn.c; old file kept in the session
  scratchpad only). Dynamic Mill keeps the mcce port. Dynamic Contour: the mcmill driver FUN_1801b58f0 is decoded
  (decomp/mcmill_hst2d.c l.11126-11653: one MCCECalcDynPock per chain bound and level, wall stock by FUN_1801ac7e0
  dis 0x1801b611d-0x1801b6170, level copies FUN_1800451f0 mm l.48656) and ported as gen_dyncontour_mcce, but over the
  ported prep_ctour it cuts nothing on a plain square with the Mill_MM default op (band = min material thickness
  0.025 without finish passes) - the band semantics are unverified and no Dynamic Contour op exists anywhere on disk,
  so it is NOT generated (MCCE_DCTOUR=1 runs it for decoding work) [2026-10-23 HST engines pass: the cause was a swapped
  reading of FUN_1800922e0's outputs, not the band semantics; Dynamic Contour and Peel Mill are generated now -
  opdlg_port_audit.md "HST engines (post-batch 9)"]; open chains (FUN_1800930c0), avoidance chains,
  finish pass FUN_180094540 (0x1241) refused. Peel Mill (PreparePockBoundsPeel 0x1802b4090 / bnci_peel / PeelAesthetics
  FUN_1800df710 roles) and Area Mill Core Roughing (McHmm kind 1 FUN_180414620 / FUN_1804109a0, mckernel expansion
  FUN_1802a9c60 / 9e80 / 9d80) are NOT DECODED and not generated: stored ops stay as Mastercam made them (postable from
  their NCI), new ops report it. Oracle: NAKTO op 3 (Core Roughing, the only affected stored op, p95 8.677 with the
  invented pattern) is now not regenerated - accepted under the no-improvisation rule, base updated (FAIL entry); all
  other fileop outputs identical; post 0/16; no rapid below the stock top. New reference found: shared Mcam2017
  common/reports/SST/Samples/Mill/Mill.mcam (inch) has 2 Area Clearance ops (oracle p95 0.389 / 0.245 in, part units)
  plus contour / pocket ops; basic examples cad/mc-test01.mcam has contour / pocket / drill.
- EXPORT data small items (2026-10-16):
  - new .tooldb: DONE - toollib.c tm_create runs SQL:create + SQL:base exactly as DatabaseCreator (scripts read from
    Mastercam 2017's ToolManager.Database.dll manifest resources); no DatabaseDefaults copy. integrity_check ok,
    _Header version 20, tools read back.
  - tool type names: DONE - mccoreres 5014..5412 via Cnc::Tool::tl_string_chooser (decomp/mccore_toolfilt.c).
  - tool_is_drill: DONE - mccore Cnc::Tool::is_mill_tool (mask 0x3affc00) / is_drill_tool = !is_mill_tool; added
    tool_is_mill. For the UI stream: InitOperation's default tool = the first assembly of the op's machine group whose
    tool fits: op_is_mill(opcode) needs is_mill_tool, op_is_drill(opcode) needs is_drill_tool (for drill opcodes, which
    op_is_mill also accepts, no tool passes both - decoded as is); the tool-selection filter CToolFilt is the user's
    common/PRM/toolfilt(m).prm per opcode (TestToolOk), all types when absent.
  - head() bytes: DONE - +0x48/+0x49/+0x4b..+0x4e from FUN_1802c72c0's global stamp (taken from the base's header),
    +0x4a = 0 (mccore tests only for 2 = wire), selection bits cited (ASSOC 0x0101 save_db; geometry 0x81 correlated).
  - new-point record: DONE - init_tp_ent (mccore 0x18004d5a0, decomp/mccore_tptools.c): +0x150 / +0x158 -1e38 and
    +0x160 -1 added; 'C' type char and "Point n" name correlated (all 14 user points).
  - solid-chain restore: NOT DECODED (needs a modelling kernel) - Mastercam keeps the chain as an SldToolPOp (ops-history
    type 0x18) and rebuilds it by rolling the solid back and reading Parasolid tags (formats/mcam.md "Solid chains",
    decomp/mcgeomsld_toolp.c); xt_top_loop / xt_cylinders stay CORRELATED.
  - holder arcs: DONE - TlProfileData type != 2 (centre, radius, start / sweep rad) and TlProfileArcSegment (+0x70 start,
    +0x78 sweep: GetArc a_2d) linearised into 100 lines as tlcore LinearizeArc.
  - other: part units from the header +0x6f (FUN_1802ae550 ChangeConfiguration) instead of only the NCI; 2017 parts'
    tools from the TlMgr when there are no TL_IDs; TL_ID tail (OAL / flute / shoulder / arbor ...) read (SetFromOldTool);
    inch thread pitch from TPI (TlToolThreading::SetOpToolInfo) and feedspeed's tap fpt adjusted; feedspeed TOL = config
    1000 5e-5; mcam_load.h machine record comment (sub 22); tooldb.md coolant "1 = on, 2 = off" corrected.
  - not in EXPORT files (left to their streams): point marker style (scene.c), backplot missing-tool fallback
    (backplotbnci FUN_67d1a2e0, tool_profiles.md), simulation tool wireframe / alpha.
  - known gaps: a part tool edited here is not written back to the part's TL_ID / TlMgr (needs a TlMgr archive writer);
    a library tool's holder is not carried (TL_ID route gives the default holder).
- UI item 15 DONE (2026-10-16): Chaining (mccoreres 1058) and Chaining Options (405) drawn from their templates at
  dialog-unit positions (viewer/chaindlg_ui.inc). Button images decoded from mccore CChainDlg: bitmaps loaded into CBitmap
  members (0x1801eb240..), SetImage on the CMFCButtons in OnInitDialog (0x1801ed2ef..), members tied to control ids by
  DoDataExchange (0x1801ec139..): Wireframe 1133, Solids 1142, Chain 1134, Point 1143, Window 1135, Area 1144, Single
  1136, Polygon 1145, Vector 1137, Partial 1146, Chain Feature 10095 / Options 10098, Unselect All 10205, End chain 7004,
  Back 1139, Forward 1147, Dynamic 1140, Unselect 1216, Reverse 1123, Last 1138 (verified by the images' content).
  Group boxes Start / End / Mask / Closed / Open / Nested chains from the templates; behaviour unchanged. Greyed: Solids,
  C-plane / 3D, Point, Chain Feature (+ Options), Help, and in 405 Entity mask, Section angle, Ignore depths, Start chain
  at point, surface edges, Break entities, Sorting, Reverse inner chains, Sync mode, Plane tolerance. NOT DECODED: the
  alternative image set chosen by flag +0x1f2b5 (second DDX branch: lathe variant), the pressed / hot look of the
  CMFCButtons. Screenshot 2026-10-16_chaining_dialogs.png.
- UI item 16 DONE (2026-10-16): point picking for Drill / Helix Bore / Circle Mill / Thread Mill shows tp_pt_mgr's
  CDrillPtSelection dialog, mcmillres 2300 "Drill Point Selection", from its template (viewer/chaindlg_ui.inc
  drill_sel_body; DDX FUN_18008b700): manual picks add a point each (an arc gives its centre) - the invented
  click-again-to-remove is gone - Unselect (last) / Unselect all / OK live; Automatic, Entities, Window Points, Mask on
  Arc (+ Diameter / Tolerance), Subprograms..., Last, Sorting..., Edit..., Pattern / Create points, Grid / Bolt Circle
  and their fields, Help greyed (not ported). Picked points drawn in palette colour 14 (FUN_18006c980(op, 0xe)) instead
  of our green; marker shape NOT DECODED. The tree dialog 2074 "Drill Point Manager" (Sort state / Re-Sort) belongs to
  the Sorting / Edit path and is not shown. NOT DECODED: the manual button's image (2954), sort order (none applied: pick
  order). Screenshot 2026-10-16_drill_point_selection.png.
- CAM item 18 (2026-10-16), partly: Move (mcam_load.h, additive fields arc / cw / cx / cy) now carries the engines'
  true arc records - Thread Mill helix / entry / exit arcs, Slot / Helix Bore / Circle Mill arc records (mw_arc, exact
  circles only), mcce Dynamic Mill 'A' records - tagged by cam2d.c mb_tag_arc. cam_write_gcode and nc_post write a tagged
  run as G2 / G3 with the record's own centre (exact points at <= 120 degree splits for R words) instead of refitting
  chords; untagged moves keep the refit. Not done: contour / pocket / Area Clearance arcs (their offsets come from
  Clipper polygons - an arc-exact MCCECalcOffset / ccomp_pock is NOT DECODED). Check: no geometrically impossible R arc
  in the posted buckle-v11 op 1, tramming-rod op 1, BRACKET4 op 19; fileop / post regressions unchanged.
- CAM item 9 (2026-10-16): NOT DONE. The pocket engine is mccore's (bnci_pock 0x1801c5fa0 -> FUN_1801c5250 ->
  process_pocket / process_open_pocket / ccomp_pock / pock_spiral_cutting in mccore, decomp/mcmill_all.c l.313461-
  313900): a large engine not decoded in this run; gen_pocket stays the invented contour-parallel / zigzag engine.
  References for it: user parts (A001 op 13 ...), shared Samples Mill.mcam ops 5 / 9, cad/mc-test01.mcam op 2.
  Map for the next run (decomp/mcmill_all.c): bnci_pock l.313900 -> FUN_1801c5250 l.313461 (chains_to_pock_bounds,
  sort_pock_bounds, extract_pocket, calc_depth_cuts per pocket, process_pocket / process_open_pocket, depth copies
  wr_pock_depth_cuts) -> process_pocket l.323151 (FUN_1801d4620 boundary offsets; method pock_prm+0x1d2 = 3 ->
  FUN_1801ec3c0; else machine_pocket FUN_1801d36c0 "POCKMAIN.cpp" l.322459) -> per pocket finish FUN_1801cc170, rough
  FUN_1801ef830 (pock_spiral_cutting) or FUN_1801ede50; the zigzag / one-way core is "Pockzigr.cpp" FUN_1801f3700
  l.343263 (FUN_1801f23e0 / FUN_1801f1fe0 scan lines, FUN_1801f15a0 zigzag when pock_prm+0x14 = record 0x123c = 0,
  else FUN_1801f0d60). pock_prm = record 0x1228 (+0x14 = 0x123c cutting method). mc-test01 op 2 (method 8, round
  pocket) = concentric full circles joined by half circles after a helix.
- EXPORT regressions (2026-10-16, final): fileop into regress/new_exp 0/189 differ from base; post 0/16; export round
  trip 188/188 regenerable ops identical (NAKTO op 3 is not generated here either way), unedited ops' records differ only
  in NCI position (+0xe8) / TP count (+0xf0) / dirty flag (+0xfc); cam_cli, mcamui, tool_cli build.
- UI item 17 (2026-10-16, messages): every remaining message site in mcamui.c / *.inc classified. DECODED: Backplot with
  nothing selected = COpMgrTree::BackplotSelected post_warning(opmanres 3964 "No operations were selected.  Please select
  at least one operation."); duplicate tool number = mccore TpToolList::WarnOfDuplicateToolNumbers (title 5583, text
  "%s%ld\n\n%s" = 5583 + number + CreateToolDescription; decomp/mccore_msg17.c; the edit stays open = deviation, ops
  find tools by number); part-tool delete already = FUN_180872b80 (3840 / 5585 / 3841, decomp/mccore_tooldel.c) - the
  dead invented tm_delete_part_tool removed; a tool edit that leaves stored toolpaths stale now marks them dirty in the
  tree (InitTvItem -2 images 0x13 / 0x18) instead of an invented box. REMOVED (Mastercam silent / command greyed):
  "<cmd>: not available in this program" (QAT, status bar display buttons, keyboard, ribbon; the status-bar 3D button is
  drawn greyed), "%s is not available here" (gallery), tool library loaded / saved lines, project loaded / saved lines,
  Recalculate feeds summary, Reverse toolpath summary, "<op> cut / copied", Doc file "wrote", Backplot "no toolpath".
  KEPT as our own (listed in provenance_audit Messages): .mcamui project open / save errors, file dialog errors, LinuxCNC
  post results, tool library load / write errors, op generation failures (CAM engine text), "Only operations created
  here can be deleted", "Too many operations", delete of geometry used by an op (Mastercam asks 4421 and lets it go -
  refused here), Print file result, unit-mismatch / 256-tool limits in part_add_tool.
- UI item 20a (2026-10-16, Toolpaths manager texts, viewer/optree_text.inc): op rows = InitTvOpText with the OpMgrDisplay
  defaults FUN_180343d50 ("N - <op_make_description> - [WCS: <plane>] - [Tplane: <plane>] - <comment>"); op_make_description
  (mccore @1800c7720): contour " (2D / 3D / 2D chamfer / ...)" (FUN_1800b0010 -> 4561..4571), pocket subtype
  (FUN_1800cb1c0 -> 4792..4800), drill = cycle text (FUN_1800b0530; post text page not read - cycles 0-7 take mccoreres
  7645 + 11 n like the op dialog, an inference; else 4877.. / 5218 / 4974); children (InitTvItem -3..-6): "Parameters"
  (+ Work offset #), tool = tl_make_description upper-cased, "Geometry -  (N) chain(s) / point(s)" (op_is_chained /
  op_is_point / op_is_msurf decompiled), "Toolpath - %.1fK - Program # N" (NCI size from OP_ID +0xe8/+0xec, op +0xf8).
  Op struct offset = OP_ID record offset - 0x68. Doc file = DocFile / DumpItemArray / CStringArrayToFile: "<part> Operations
  Manager.txt" in $TMPDIR, header path + "    " + "%A, %B %d, %Y   %H:%M:%S", 4 spaces per depth, opened with xdg-open
  (Mastercam's editor) outside scripted runs. NOT DECODED: plane names other than id 1 ("?"), the NC file name part of the
  Toolpath row (NCSuffix = control definition text 0x15: left out), facing_op_uses_stock (mcmill; facing rows show chains),
  the first op class test inside op_is_chained. Screenshot screenshots/2026-10-16_toolpaths_tree_texts.png.
- UI handed over by EXPORT (2026-10-16): (1) point markers DONE - mastercam.dll draw_3d_point_style @1803301c0 /
  draw_3d_point (decomp/mastercam_pointstyle.c): per-point style from the record (ent +0x1c; user parts use 1 and 6),
  drawn per view by scene.c draw_point_markers (style 1 = world X/Y/Z lines +-vht/72; 3 plus, 4 X, 5 circle (21 x 18 deg),
  6 square at +-vht*0.01 in the screen plane; width 1, entity colour); new points take mcamxm.config 10002 "Point
  style?"; the invented fixed +-0.5 cross in mcam_load.c is gone (point polylines are the bare point). NOT DECODED: the
  dot size of style 2 (FUN_18021b780, drawn 1 px) and the thread / cut glyphs of styles 7 / 8. Screenshot
  screenshots/2026-10-16_point_styles.png. (2) backplot missing tool: FUN_67d1a2e0 (decomp/backplotbnci_tooldata.c)
  builds the tool from the op's NCI tool attributes; its constant branch (type 10, len 1.0, flute 0.99, holder D*1.1 x
  0.25) runs only with caller flag +0x19 (forced tool vector) - not plain backplot. The invented ext*0.02 tool is gone:
  with no part tool the op's own tool (camop params, or OP_ID +0x34c type / +0x350 dia) is drawn, else none. (3) tool
  type names: tl_string_chooser(type, 8) (Tool Manager column) = the same mill map, so the UI's names are right; the
  0x40 (router) map would be 3970.. (4) default tool = InitOperation rule: first part tool with is_mill_tool when
  op_is_mill(opcode) and is_drill_tool when op_is_drill(opcode) - so Drill gets no default tool (Tool page shows none;
  OK then says mccoreres 3837 "Unable to calculate toolpath.  Tool assigned to operation does not exist!" - its caller
  is not traced). The invented "last tool used / pointed tool for chamfer / thread mill" picks are gone. CToolFilt prm
  (toolfilt(m).prm) is absent in the 2017 user folder (present only in <windows>/mcamx), so not applied.
- UI item 20 keyboard window (2026-10-16): labels now cmd_label (FT resource strings) for each keymap entry; the
  invented names ("(n/a)", "Help (this list)") and the "Also" key list removed. INTENTIONAL: OnHelp shows this list
  because Mastercam's CHM help is not ported. Screenshot screenshots/2026-10-16_keymap_window.png. Op dialog: a
  not-ported page is drawn empty (the "(not available in this program)" text is gone).
- UI item 20 Grid Settings (2026-10-16): the invented "Grid Settings" window replaced by mccoreres dialog 1589
  "Configuration - Screen Grid" (Spacing X / Y, Origin X / Y + Select (greyed: picking NOT PORTED), Snap Near / Always,
  Size) = mcamxm.config [GRID SETTINGS] 23002-23007; the style / height / colour / snap switch / preset buttons are gone
  (grid.style 0 lines and at_floor stay unreachable at their defaults). ConfigureX opens this page only (the rest of the
  Configuration dialog NOT PORTED). Screenshot screenshots/2026-10-16_grid_settings_page.png.
- UI item 20 unsaved prompt (2026-10-16): mccoreres 7291 verbatim, "Mastercam" -> program name; caption 5175 -> program
  name (intentional, as the title bar).
- CAM stream 2, item 0 DONE (2026-10-16): new references Mill.mcam (shared SST Samples, inch) and cad/mc-test01.mcam
  (inch) are in the oracle runs (tools/oracle_refs.sh copies them to a scratch dir; oracle.py now samples in part units:
  0.1 mm / 20 mm cap scaled by 1/25.4 for inch parts, header +0x6f). Why contour was poor there - not inch units, four
  causes, each fixed from the decode: (1) arc views: A_ID +0xc8 u16 view number, the centre is in that view's
  coordinates (mastercam view_to_world @180341520, decomp/mastercam_keyrot.c l.673: views < 2 identity, else M^T p;
  views = DB_ID subtype 0 records +0x6a number / +0x70 matrix_33, they may follow the geometry) - Mill.mcam's arc 180
  has view 19, read as a garbage centre (mcam_load.c arc_finish + pending view arcs, tools/mcam_ops.py; built-in views
  2..8 NOT DECODED, such arcs stay as stored); every user arc has view 1. (2) contour XY multipasses (mcmill contour
  loop decomp/mcmill_ctour.c l.1506-1700): passes per depth = rough count 0x8aa (+ finish count 0x8b8 at the last depth
  or every depth with 0x1230), offsets = remaining finish x 0x8c0 + remaining rough x 0x8b0, warning-0x93b rule; our
  "always one rough pass + finish_n" cut an extra pass on Mill.mcam ops 2 / 6 (rough 0, finish 1). (3) rapid retract
  off (0x1a9 = 0, the Mill_Inch default; every user op has it on): mccore retract_move / clear_move feed up at the
  retract rate 0x388 to the feed plane, then rapid (cam2d.c retract_up, contour + pocket). (4) open-chain lead
  directions: exact end-entity tangents instead of polyline chords (mc-test01 op 1). Also from the references: pocket
  "Roughing enabled" 0x1238 = 0 -> finish passes only (machine_pocket FUN_1801d36c0 l.322563, finish FUN_1801cc170;
  Mill.mcam ops 5 / 9), and drill points picked on a circle are drilled at the point record's xyz (+0x98), e.g. a
  quadrant (mc-test01 op 4). Oracle p95 (part units) before -> after: Mill op 2 contour 0.122 -> 0.004, op 6 0.066 ->
  0.002, pocket op 5 0.533 -> 0.008, op 9 0.480 -> 0.007; mc-test01 contour op 1 0.764 -> 0.004, drill op 4 0.250 ->
  0.000; user ops unchanged (contour 141 ops median 0.049). Fileop regression: 377/378 identical, tramming-rod_3 (drill
  point y 18.8938 -> 18.893801 = the NCI's 18.8938007, record xyz) accepted into base. Post 0/16. No rapid below the
  top (188 outputs). Left: mc-test01 op 2 = cutting method 8 = High Speed pocket (item 9); Area Clearance ops 3 / 7
  (item 8). Pocket cutting method values decoded (mcmill FUN_180183c20 list table l.269721 + strings 1862-1872):
  0 Zigzag, 1 Constant Overlap Spiral, 2 Parallel Spiral, 3 Parallel Spiral Clean Corners, 4 Morph Spiral, 8 High
  Speed, 6 One Way, 5 True Spiral; the Mill_MM / Mill_Inch default pocket is 8 (High Speed).
- UI item 20 manager tabs (2026-10-16, viewer/levels_ui.inc): Levels = opmanres dialog 459 + list columns FUN_1802491d0
  (Number / Visible / Name / Entities / Level Set, default widths FUN_180247e90: 7u / 6u / W-32u / 8u / 7u, u = W/40),
  rows = levels with entities (entity count, not polylines); form (Number / Name / Level set / Display Used / Named /
  Used or named / Range) greyed. Planes = dialog 457 + columns FUN_18040b1f0 (Name, G, WCS, C, T, Display, Offset,
  Coordinate), rows = the SystemPlanes of the part's MastercamPlanes archive (1 Top, 2 Front, 3 Back, 4 Bottom, 5 Right
  side, 6 Left side, 7 Iso - "Isometric" was wrong); the Toolpaths rows use these names for WCS / Tplane. Recent
  Functions = MCCore.UI RecentFunctionsManagerView (flat buttons, padding 8, small icon + text, click runs the command),
  up to mcamxm.config 4023 entries (the 16 was invented). NOT DECODED: Levels row fill / visibility marks / names / sets,
  Planes row fill (marks, Display, Offset, Coordinate), plane column widths, column chooser and sorting, editing; the
  Planes rows no longer set the view (that click was invented). Screenshots screenshots/2026-10-16_levels_tab.png,
  screenshots/2026-10-16_planes_tab.png.
- EXPORT TlMgr archive writer (2026-10-16): DONE. Decoded the store side (formats/part_tools.md 10): TlMgr::Serialize
  0x180744620 (decomp/mccore_tlserialize.c l.9445-9588) over PrepareObjectsForStoring's std::map<TlID> (TlID::operator<
  tlcore 0x180004741: Data1, Data2, Data3, Data4 - the order of all 24 files), slot map in slot order, CArchive
  WriteObject / WriteClass / AfxWriteStringLength rules; every class layout for X9 (DeserializeVn readers) and 2017
  (Serialize store branches) with function + line per table (part_tools.c FD_*); new: TlManufacturer v1 (no fields),
  TlToolGrade v1 (GUID +0x48), plain TlToolMill leaf; tlcore writers persisted to decomp/tlcore_tlserialize.c.
  part_tools_write (viewer/part_tools.c/.h) rebuilds the subtype-33 record: byte-identical for 16/16 user parts, 8/8
  test exports and 128/141 archives of Mastercam shared / sample files (13 refused: lathe, pre-X9 schemas). Exporter
  (mcam_saveops.c): tools edited here (paired with the reloaded source by slot) get their changed fields written to the
  archive objects, the TlTool block, the TL_ID body and edited ops' op_tool_info (stored bytes kept except those the
  edit changes); OAL edit -> assembly stickout and true length per CheckStickoutsAreLessThanPhysicalLength; holder name
  / outline -> holder object (new line segments = TlProfileLineSegment / TlObject() constructors, CoCreateGuid v4 IDs).
  Tested: 102/102 edit exports reload with the new values, archive parses to its last byte (tlmgr_parse.py), TL_ID ==
  block; mcam_export.py shows edited tools 16/16 X9. Regressions: fileop 0/189, post 0/16, export round trip 188/188.
  NOT DECODED / not done: a type change to another tool class (new object defaults), new tools' assemblies / holders
  (TL_ID route kept: SetupNewAssembly's default holder FUN_180323600 shape unknown), library holder fields (LibraryName,
  connection sizes) on a holder change, Mastercam's own OAL / stickout dialog rule, lathe and pre-X9 archives, whether
  Mastercam refreshes stored ops' op_tool_info from the TlMgr.
- UI item 20 Tool Manager menu (2026-10-16): menu 1225 now with its real cascades - View > (Large Icons / Small Icons /
  Details), Arrange tools > (by Tool Number / Type / Name / Diameter), Import/export tools > (Convert a library to /
  from text, Import from Machining Cloud (greyed: not ported), Compress, Report..., Detailed report...) - drawn as
  top-level windows beside the menu; the missing items Edit projection... and Load tool(s) on machine... added greyed.
  Rows under the open menu no longer take its clicks. Screenshot screenshots/2026-10-16_tool_manager_menu_cascades.png.
- UI item 20 op Tool page (2026-10-16): the invented tool combo / "Custom flat endmill" / "Tools..." / Tool Manager
  "Use for operation" button replaced by mccoreres dialog 25524 "Tool": the part tool list (Tool Manager part-list
  columns and cell texts - the same CMillToolListCtrl column set on this page is assumed), Tool diameter / Corner radius /
  Tool name / Tool number / Head number / Length offset / Diameter offset from the picked tool (read-only), Feed rate /
  Plunge rate / Spindle speed live; FPT, SFM, Retract rate, Spindle direction, Force tool change, Rapid Retract, Comment,
  Recalculate values, Filter, Tool inspection, To batch, Iscar HEM / RCTF greyed. "Select library tool..." opens Tool List
  6410 (library list, OK adds the tool to the part and picks it; filter / display mode / New Library greyed). The
  dialog width now fits the 435-du page (ToolpathDlg size NOT DECODED). Screenshot
  screenshots/2026-10-16_op_tool_page.png.
- UI item 20 extrude kernel (2026-10-16): thin-wall side of OPEN chains decoded - mcgeomsld ps_sweep_wire takes the
  sheet face nearest a point on the chain's first entity, compares its oriented normal with v = d x t from FUN_180097650
  (d = the chain's extrusion vector, t = the first entity's own tangent at mid parameter, negated by the per-chain flag)
  and thickens Direction 1 away from v / Direction 2 toward it (PK_BODY_thicken_3 front = along the face normal - the
  Parasolid front/back convention is the one assumption). feat_groups (mcamui.c) now orients each open polyline so
  open_band's left side is Direction 1 (reversed when cdir * (first entity reversed ? -1 : 1) > 0). Closed chains were
  already Direction 1 inward. Projects with thin open-chain extrudes may rebuild with the wall on the other side.
  Draft sense stays NOT DECODED: ps_draft_extrude_faces calls PK_FACE_install_surfs_isocline(faces, ref edges,
  unit direction (negated with the angle when the angle < 0), |angle|, tol) - which way Parasolid tapers for a positive
  angle relative to that direction is not in the code (decomp/mcgeomsld_draft_sweep.c).
- Extrude sheet grouping (2026-10-19): DECODED from decomp/extrude/b7.c - FUN_180039370 converts every CLOSED chain
  (CHAIN+8) with chain_to_pock_bound(tol*10), nests the bounds with sort_pock_bounds(0.025 mm / 0.001 in), and
  FUN_18003a650 makes a group per top-level loop: direct children (holes) join it, even-depth loops (islands) become
  new top-level groups. mcamui.c ex_grouping implements this (feat_groups / ex_accept use it; Create now makes one
  body per disjoint loop and per island; Cut / Boss union to the same even-odd region as before). chn_get_normal's
  flip rule decoded too: closed chains flip only when the turning-angle sum is -2 pi within tol, open chains keep the
  plane normal. Remaining OURS / NOT DECODED there: the containment test and group emission order (sort_pock_bounds'
  algorithm), the thin path's wire grouping (ps_sort_chains_for_wire), the hole-children dedup FUN_1800325c0, and the
  turning sum is taken on the flattened polyline. Verified headless (MCAMUI_SCRIPT): disjoint loops 2 bodies, 3-deep
  nesting outer-hole body + island body, nested Cut unchanged; fileop 0/189, post 0/16.
- UI stream FINAL (2026-10-16, session wound down): build ok (make mcamui cam_cli), offscreen screenshot of B4 ok,
  fileop regression 189 files 0 diffs, post regression 0/16 (after the point-marker change in mcam_load.c too).
  DONE: items 5, 6, 10, 11, 12, 13, 14, 15, 16, 17; item 20 parts - Toolpaths manager row texts + Doc file, Levels /
  Planes / Recent Functions tabs, keymap window, Grid Settings page, unsaved prompt text, Tool Manager menu cascades,
  op Tool page + Tool List 6410 (replaces "Use for operation"), extrude open-chain thin-wall side; EXPORT hand-overs
  (point markers, backplot missing tool, type names, default tool rule).
  REMAINING / NOT DONE: AutoCursor Fast Point (item 6); ribbon / backstage look metrics; extrude draft sense (Parasolid
  isocline convention); Levels / Planes row fill, visibility, editing; Toolpath row NC file name (control-definition
  suffix); facing_op_uses_stock; plane names beyond the 7 system planes; op Tool page FPT / SFM / retract rate /
  spindle direction / filter; Help topics; MFC field tooltips; HST tree builder FUN_18014c4a0; stock origin for Facing.
  Stopped after the extrude thin-wall item; no work in progress.
- CAM stream 2, item 9 PARTLY DONE (2026-10-16, session stopped here): the pocket engine is ported for High Speed
  roughing (cutting method 8, the Mill_MM / Mill_Inch default) and finish-only pockets, in viewer/cam2d_pock.c on a new
  exact line / arc loop kernel viewer/pgeo.c (pock_bound / gt model; offset = SUBSTITUTE for mccore ccomp_pock /
  MCCECalcOffset: Clipper topology on finely sampled loops, each vertex run reassigned to its exact offset line /
  concentric arc / corner arc, corners turning less than set_min_fil_angle's angle made sharp). Decoded and ported
  (decomp/mcmill_all.c, decomp/mccore_pock.c, decomp/mccore_pock2.c = new mccore decompiles): GeneratePocketToolpath /
  machine_pocket flow (roughing / finishing flags 0x1238 / 0x1239), tool-centre bounds R + wall stock, rough boundary
  inset by finish passes x spacing, hs_parallel_spiral_pocket FUN_1801d00d0 (stepover offsets halved once when empty,
  corner smoothing 0x1400 = opening FUN_1801d2b80, nearest-start loop picking FUN_1801ce870, islands merged each level,
  waiting branches, inside-out via the temp file's backward links unless 0x1258 = 0, start propagation FUN_1801d19d0),
  the loop transitions FUN_1801ce310 (channel loop / S shape FUN_1801d1010 / FUN_1801d13a0 when the step is the
  stepover or half, checked by FUN_1801d2c60 with transversal intersections), find_helix_center FUN_1801c04d0 and the
  helix FUN_1801c1d80 (height, sweep, direction 0x12d0, ending towards the start), depth cuts computed once and
  Z-shifted (wr_pock_depth_cuts), finish passes FUN_1801cc170 (pass offsets, every depth with 0x1230), cutting-method
  values (FUN_180183c20 table: 0 Zigzag, 1 Constant Overlap Spiral, 2 Parallel Spiral, 3 Clean Corners, 4 Morph,
  8 High Speed, 6 One Way, 5 True Spiral). Arcs are output as true arc records (item 18 for pockets). The zigzag
  engine (method 0) keeps its CORRELATED pass pattern but now uses the decoded helix (find_helix_center + FUN_1801c1d80)
  and calc_depth_cuts. Measured (not decoded): the finish entry at the first chain entity's midpoint with the op's
  lead line / arc (4 references agree). Oracle p95 (part units): mc-test01 op 2 (High Speed) 0.169 -> 0.005, A001 op 13
  (zigzag) 0.140 -> 0.031, Mill.mcam ops 5 / 9 0.008 / 0.007; pocket 4/4 ok. NAKTO outer + 4 islands (D6, stepover
  25-200 %): no gouge (cam_cli --verify). Fileop regression: 377/378 identical, A001_13 changed (oracle-justified,
  copied into base); post 0/16; no rapid below the top (188 outputs). REFUSED (NOT DECODED, generation refused with a
  message, no invented fallback): cutting methods 1-6 (Constant Overlap / Parallel / Clean Corners / Morph / True Spiral
  / One Way; the old invented contour-parallel engine is no longer used), High Speed channel modes 1 / 2, stepover
  between R and D (the extra loops FUN_1801d1740), ramp entry (FUN_1801c30d0 / FUN_1801c14b0), open pockets, skip-pocket
  on entry failure. Approximations kept: the link around islands (find_path_around_island) -> retract / plunge; lead
  fitting of the finish passes (FUN_1801cd200) -> leads dropped when they leave the region; pocket order = chain order
  (sort_pock_bounds not ported). NEXT: Parallel Spiral FUN_1801ea750 / FUN_1801e9a90, the Pockzigr zigzag / one way
  (FUN_1801f23e0 scan lines, FUN_1801f1fe0 regions, FUN_1801f15a0 / FUN_1801f0d60 linking), then items 2-6 (Peel / Core
  Roughing, Dynamic Contour, Area linker, true arcs for contour / Area via pgeo.c, tp_pt_mgr sorts) - not started.
- EXPORT tool items 1-5 (2026-10-16, stopped at the session end): (1) DONE - OAL edit = Mastercam's tool edit
  (toolwizardlaunch ConvertExtensionsMillTool l.19837-19840: TruePhysicalLength = OverallLength = v unless adjusted
  and already v; EditTool 0x180883900 -> FUN_180a9d130 -> TlAssembly::OnSetMainTool 0x180717710: Stickout = OAL when
  adjusted, else only when 0); the invented "raise the true length" rule is gone (CheckStickouts only reports).
  part_tools.c apply_edit, part_tools.md 10.2. (2) DECODED - "Default Holder" = TlHolder(bool) 0x180723f80 (mccoreres
  476, sizes "0", 50 x 25 mm / 2 x 1 in) or, with legacy dia and len > tol, AddSegment(dia, dia, len) (objdump
  0x180745867-882): rectangle r = dia / 2, h = len; fits all adj=1 default holders (part_tools.md 5). (3) NOT DONE -
  constructor chain partly decoded, TlAssemblyItem / op params ctors, material ID map not; TL_ID route kept (10.4).
  (4) DECODED, NOT IMPLEMENTED - delete = TlMgr::Delete + op fixes (config 17027) + TL_ID delete_ent; holder removal =
  GUID_NULL holder ID, re-added on load (10.4). (5) DECODED, NOT IMPLEMENTED - library holder = TlHolder::Clone with
  ConvertExtensionsHolder fields; needs Tool / Holder fields (mcam_load.h is another stream's). Decompiles:
  decomp/mccore_tooledit.c; tools/ghidra_scripts/MemDump.java new. Tests: round trip ALL IDENTICAL (16 parts + test
  exports), edit batch 102/102 archive OK, TL_ID == block; fileop 377/378 identical (A001_13.log pocket move count:
  base changed by another stream at 13:07, not tool related); post 0/16. Next: implement 10.4 delete / holder removal
  in mcam_saveops.c (archive object + slot removal, TL_ID record drop, op fixes per config 17027).
- 13:20 SESSION END: all streams stopped cleanly. Final: build ok (cam_cli, mcamui, tool_cli); fileop 0/189 vs base; post 0/16 (~/mastercam-re/regress/postreg.sh, base post_base); mcamui offscreen smoke test ok. Next session: CAM pocket methods (Parallel Spiral first) + items 2-6, EXPORT tool delete / library holder fields / new-tool assemblies, wizard OAL = true length, UI leftovers (see status lines).
- CAM item 9, op dialog batch 6 (2026-10-23): pocket cutting methods 2 / 3 Parallel Spiral (+ Clean Corners), 5 True Spiral,
  6 One Way (0 Zigzag decoded too; since decisions batch dec1 the decoded engine is the default, the correlated one only with PK_ZIGZAG_DECODED=0),
  High Speed stepover between R and D, ramp entry and the helix options, the finish page (leads FUN_1801bdc00 replace the
  measured lead model) - DONE (formats/opdlg_port_audit.md 10). Still refused: methods 1 / 4, Minimize tool burial (medial axis),
  trochoidal modes 1 / 2, pocket types 1-4, keep tool down, spring passes; links around islands (find_path_around_island).
