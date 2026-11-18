# Units: metric / inch (Mastercam 2017's rule and the port)

Decompiles (2026-10-27, mccore.dll, `decomp/units/`): `core_units.c` (ChangeConfiguration, IsMetric, SetUnits*,
RegisterVariable*, the configuration file name), `core_cfgread.c` / `core_cfgitem.c` (ReadCfgFile's item filter, the
CCfgChgDlg constructor), `core_cfgcallers.c` (every caller of ChangeConfiguration / SetUnits: the Configuration dialog's
OK FUN_180170520, its Current: combo FUN_1801756a0 / FUN_180175420, file open FUN_1802a0a90 / ProcessVersionEntity
FUN_1802d5f60, startup FUN_1803b9f90 / FUN_1801cc690), `core_cfgchange.c` (RegisterVariable, ResetRegisteredVariable,
ReloadConfiguration, ConfigRewriteDefaults), `core_scale.c` (fmu_scale_yn, fmu_scale_db), `core_stamp.c` (the record /
version-entity writers), `core_callers_*.txt` (call lists). Strings: mccoreres (tools/pe_strings.py); dialogs:
tools/pe_dialogs.py.

## 1. One units flag: IsMetric()

- `IsMetric()` (@1805fa070) returns one byte, `DAT_18142d400`; `IsNotMetric()` its negation. There is no per-part units
  value in memory: every units-dependent piece of Mastercam (655 call sites in mccore alone, core_callers_ismetric.txt)
  reads this flag.
- `SetUnits(units, regen)` (@1805fa8c0) is the only writer. When the units change it
  1. stores the flag;
  2. converts every registered session variable (FUN_1805f9da0): the table built by `RegisterVariable(&v, metric, inch)`
     (@1805fa3a0; entries {double *v, char units, double ratio = metric / inch}) - to metric `*v *= ratio`, to inch
     `*v /= ratio`;
  3. stamps the save units (FUN_1808c4ce0: `DAT_18143d54f = IsMetric()`), which every record writer copies - record
     `+0x4d` (FUN_1802c4010 / FUN_1802c72c0 / CCloset::StoreEnt) and the version entity's `+0x6f` (FUN_1802c5a00). **A part
     is saved in the system units.**
  4. with `regen`, `sld_regen_solids`.
- The configuration file carries its units in item 1 `File is metric? y|n` ([UNITS]); `FUN_1801c5500(units)` names the
  default file `mcamxm.config` (metric) / `mcamx.config` (inch). Reading a configuration (FUN_1801bb2c0) sets the units from
  item 1 (`SetUnits(DAT_1813c467c)`).

## 2. Opening a part

`ProcessVersionEntity` (FUN_1802d5f60) and FUN_1802ae550: version entity `+0x6f` < 2 and != IsMetric() (and the read is an
open, `param_1+0x20`) -> `ChangeConfiguration(units, 0, 1)` + `ResetEA`. So **opening a part switches the system to the
part's units**.

`ChangeConfiguration(units, mode, regen)` (@1805f9a70), same units -> nothing; else:
- mode 0 (open): unless the registry has `GUI\Settings\EnglishMetricPrompt` = 0, the "System Configuration" prompt CCfgChgDlg
  (mccoreres dialog 1112: radio **Units** / **All settings**, "Don't ask me again" -> registry `LoadAllSettings`). Mode 1 =
  units only, mode 2 (import / export directory, FUN_180521ec0 / FUN_1805228e0) = all settings.
- `SetUnits(units)`; the current configuration = the last one used in those units (`DAT_18142d410` inch / `DAT_18142d510`
  metric) or the default `mcamx(m).config`.
- **Units only** (FUN_1801b6990): ReadCfgFile then reads only items < 1000, 11000-15999 and the list 29000-29003 (analyze
  precision), 1000-1008 (tolerances), 7004, 8003, 10012, 19024, 19035, 20018, 21004, 21008, 21009 (FUN_1801b62d0 skips the
  rest). **All settings**: the whole file.
- The user's registry (NTUSER.DAT, `Mastercam 2017\GUI\Settings`) has neither value: Mastercam asks every time.
- Merge (FUN_1802a0a90, dialog 209 "File-Merge"): "Maintain current system units" / "Reset units to file being merged" +
  "Scale geometry being merged" / "Scale geometry in current database" -> ChangeConfiguration + fmu_scale_db. Merge is not in
  this program.

## 3. File > New

`init_db` (mastercam.dll, decomp/mastercam_filenew.c) does not touch the units: **a new part has the current system units**
(it is stamped with them when saved).

## 4. Switching the units: File > Configuration

System Configuration (mccoreres 282) "Current:" combo (2056) lists the configuration files with their units, `mcamx.config
(English)` / `mcamxm.config (Metric)` (strings 4962 / 4963). Selecting one (FUN_180175420 -> FUN_1801756a0): ReadCfgFile of
that file, SetCurrentCfgFilename, `SetUnits(item 1)`. OK (FUN_180170520), when the units differ from those the dialog opened
with (dialog `+0x851`):
- an empty part (`db_end == 0`) -> `init_db`;
- else `fmu_scale_yn` asks **"Scale current part to Metric?"** (5304) / **"Scale current part to English?"** (5303)
  (dbmmess_setup + dbmmess_mbyn; the caption is not set by this code: NOT DECODED). Yes -> `fmu_scale_db`: every entity of
  mask 0x1f1fff through the xform FUN_1808d8820 with diag(25.4) / diag(1 / 25.4), no translation (a scale about the world
  origin, originals replaced). No -> `sld_scale_sldBodies` (the solids keep their size in the new units - NOT PORTED);
- `sld_regen_solids`, then `RefreshStatusBar`, `init_autosave`.

**Not converted** on this path (no call found): operations' parameters, their toolpaths (NCI: written with `1016 +0x44 =
IsMetric()` when computed), tools, the stock / machine group; outside mccore neither (section 9: only the dirty mark of the
scaled geometry's users, through the xform's associativity).

Messages: 5476-5479 ("Switching / System units switched from Metric to English" ...), 5472-5475 ("Loading Metric
configuration file..."). 

## 5. The port

| what | where | rule |
|---|---|---|
| system units | `mc_units_metric` (mcam_load.c), starts 1 | IsMetric(); metric start = the user's startup configuration mcamxm.config (ours: no startup config selection) |
| "is this part metric" | `mcam_is_metric(f)` (mcam_load.c) | the part's header units (0 / 1), else (-1 unknown, 2 mixed NCI without a header) the system units; used by every site (table 7) |
| open | scene.c load_file | header units 0 / 1 -> system units (ChangeConfiguration). The prompt "Units / All settings" is not shown: the app always reads the current units' whole file ("All settings") |
| new | scene.c new_file | F.metric = system units (init_db) |
| switch | core_ui `cui_units_change` / `cui_units_set` (cui_misc.c); Qt: File > Configuration (backstage), a dialog with the Current: combo only; script step `units:0|1` (askyes answers the scale question), `unitsdump` | 1. the scale question when the part has entities (none for an empty part); 2. Yes: points / lines / arcs scaled about the origin (xf_scale_all, one undo step; splines / surfaces / solids NOT PORTED - counted in the message); 3. system units, F.metric, doc dirty (not for an empty part); 4. RegisterVariable'd session values converted (Extrude: Distance 50 / 1, Thickness 1 / 0.1); 5. the front end reads the new current configuration again (cui_hooks.config_reload: cui_config_item items, view factors, colours, grid = set_default_grid(units) + [GRID SETTINGS]); 6. message 5479 / 5478 (+ the scale count) |
| config values | mcam_config_value(mcam_is_metric(F), ..) | mcamxm.config / mcamx.config (the default names; no other configuration file can be chosen) |
| defaults library | default_op / cam_default_op_record / mcam_defaults_library (keyed by units) | Mill_MM / Mill_Inch (config 2303 for the save base) |
| system tolerances | `McSysTol mc_systol` (mcam_load.h, mcam_saveops.c `mc_systol_units`) | config 1000-1008 + 7004 of the current configuration (section 8): every engine, chaining, the front end |
| operations made before a switch | CamOp.gen_units (cam_generate_op); scaled geometry -> users dirty (mod_pair -> dirty_ent_users) | section 9: not converted, dirty only through the scaled geometry; the post (nc_post.c, nci_write.c) refuses moves computed in the other units: "regenerate it first" (+ "(not dirty: Regenerate all selected)") |
| stored Mastercam toolpaths | McamFile.nci_metric (1016 met_tool) | the LinuxCNC post refuses a stored NCI whose units differ from the system units |
| save as Mastercam part | mcam_saveops.c save_impl | version entity +0x6f and new records' +0x4d = IsMetric() (records copied from the source part keep their +0x4d: NOT DONE) |
| project | doc_write / doc_read | `UNITS 0|1` when the units are not those opening the part gives (the header's; a new part: metric); applied right after the part loads |

## 6. NOT DECODED / not ported

- The scale question's caption; `sld_scale_sldBodies` (solids are not rescaled here either way); splines / surfaces / solids
  are not scaled (no xform of those in this program).
- The "Units / All settings" prompt on open (CCfgChgDlg) and the Units-only item list (always "All settings" here).
- Configuration files other than the two defaults (Open / Save / Merge of the Configuration dialog, the remembered per-units
  configuration DAT_18142d410 / d510).
- Operations / tools / stock on a units change: decoded (section 9) - nothing converts them; the only effect is the dirty mark
  of the scaled geometry's users. Stock / machine group values and part tools keep their numbers (as in Mastercam).
- The save of an op computed in the other units (nci_write.c): refused; Mastercam keeps its NCI section with its own 1016 +0x44
  (our NCI writer stamps IsMetric(): writing gen_units there instead is NOT DONE).
- Drill ops made here keep their points as coordinates (Hole, no entity reference): the scale does not move them and does not
  mark them dirty (Mastercam: mc_op_modify_entity_and_points / tp_change_eptr on the point entities) - NOT PORTED.
- Opening a part in the other units re-reads the tolerances (scene.c load_file) but not the front end's other configuration
  items (cui_hooks.config_reload is only run by cui_units_set): TODO.
- File > New: Mastercam's init_db(1) runs ReloadConfiguration (the current file again, mastercam_filenew.c l.122) - the session
  tolerances (Chaining Options' chain_tol) would return to the file's; here File > New reads nothing: NOT PORTED.
- Records copied from the source part keep their old +0x4d stamp after a switch (Mastercam re-stamps each stored record).

## 7. Audit (2026-10-27): every units-dependent site

Conventions found: `F.metric == 1` (a new part -1 and a mixed part 2 = inch), `!= 0` / `? :` (-1 / 2 = metric), engines
`== 1 || < 0` (2 = inch), raw `F.metric` passed as an `int metric` argument (callee decided). Mastercam: one flag.
Fix everywhere: `mcam_is_metric(f)`; a new part now has F.metric = the system units (no more -1).

| site | what | before | Mastercam | fix |
|---|---|---|---|---|
| cam3d_finish.c (6), cam3d_rough.c 404, cam3d_horiz.c 97, cam2d_dyn.c 233 / 414, cam2d_face.c 309 | engine units | `== 1 \|\| < 0`: mixed = inch | IsMetric() | helper |
| cam3d_rough.c 87 | XT scale | `== 0` | IsMetric() | helper |
| cam2d.c 63 / 292, cam_fileop.c 116, cam2d_pock*.c, cam2d_ops.c 548, cam_cli.c 35 / 187 | chain tol (config 1001 default) | raw | IsMetric() | helper |
| cam2d.c 38 spl_min_step, 2112 g_nc_tol / g_min_arc | config 1004 / 1008 defaults | `== 0` | IsMetric() | helper |
| cam2d.c 878 cam_defaults, cam_filter.c 516, cam2d_dyn.c 376, cam2d_engrave.c 24, nc_post.c 73 / 106 | defaults op | raw -1 / 2 -> metric | IsMetric() | helper |
| cam2d.c 2138, cam_fileop.c 351 | Area Mill macro (scale 25.4) | `== 1`: new part = **inch** (bug) | IsMetric() | helper |
| cam2d_engrave.c 35 / 38 | Engrave tol / engine units | `== 1`: new part = **inch** (bug) | IsMetric() (FUN_180052250) | helper |
| cam2d_area.c 71 | Area passes scale | `== 0` | IsMetric() | helper |
| verify.c 181-213 | through-cut margin | `== 1`: new part = inch (bug) | ours (margin) | helper |
| nci_reverse.c 648 | Reverse feed fallback 2.5 / 0.1 | raw -1 -> `== 1` false = **inch** (bug) | IsMetric() [S 3.4.j] | helper |
| cam_cli.c 280 / 326 / 346 / 366 | CLI engines / NCI writer | `== 1` | IsMetric() | helper |
| nc_post.c 401, cam2d.c 2449 | program G20 / G21 | header units | IsMetric() (+ stored NCI's own units) | helper + refusal (5) |
| nc_post.c 393 / 394, cam2d.c 2448 | mixed / unknown units refused | kept | - | kept (no guess of G20 / G21) |
| cam_fileop.c 136-158 | XT scale guess for unknown units | kept | - | kept (ours, a part without header units) |
| mcam_saveops.c 600 / 760 | save base / defaults library | `!= 0` | IsMetric() | helper |
| mcam_saveops.c 851 / 1160 | a new tool's units | `== 1`: new part = **inch** tool (bug) | IsMetric() | helper |
| mcam_saveops.c save_impl | header +0x6f / stamp +0x4d | copied from the source | IsMetric() (FUN_1808c4ce0) | stamped |
| scene.c 390 | shading tol (gl_tolerance) | `== 0` | IsMetric() (FUN_1801b4070) | helper |
| scene.c 1227 / 1365 | fit margins / empty-view width | `!= 0` / `== 0` | IsMetric() (init_db) | helper |
| scene.c load_file / new_file | open / new | -1 for a new part | ChangeConfiguration / init_db | system units set / taken |
| core_ui cui_toolpage.c 185 | tool units check | skipped for -1 / 2 | IsMetric() | helper (always checked) |
| cui_toolpage.c 242 tp_metric | IsMetric() | `!= 0` | IsMetric() | helper |
| cui_toolmgr.c 377 / 410 | new library units | `== 1`: new part = **inch** library (bug) | IsMetric() | helper |
| cui_toolwiz.c 35 / 60 / 292, cui_ops.c 598, cui_doc.c 256 / 268 | new tool units | `!= 0` / `== 1` mixed | IsMetric() | helper |
| cui_stock.c 354 | stock geometry tol | `== 1`: new part = inch (bug) | IsMetric() (FUN_1801b4070) | helper |
| cui_stock.c 1021 / 1114 | curve chord default | `== 0` | IsMetric() | helper |
| cui_feat.c 115 / 247 / 274 | extrude tolerances | `?` / `!= 0` | IsMetric() | helper |
| cui_sldchain.c 155 | nc_tol | `?` | IsMetric() | helper |
| cui_xform.c 467 | Array lengths 25 / 1 | `!= 0` | RegisterVariable | helper |
| cui_extrude.c 90 | Extrude session values | set once at the first start, **stale after a switch** (bug) | RegisterVariable conversion | ex_units (ratio 50, 10) |
| cui_geo.c 226-228 / 1293 | wireframe panel defaults / stored keys per units | `F.metric ?` / `!= 0` | RegisterVariable conversion | helper; per-units defaults + stored keys kept (ours, TODO) |
| cui_planes.c 189, cui_opdlg.c 193 / 462 / 855, cui_grpprops.c 505, cui_ops.c 270 / 336 / 880, cui_defaults.c 312 | defaults op / X9 update | raw / `== 1` | IsMetric() | helper |
| cui_sel.c 135 | config 17014 | raw | current configuration | helper |
| cui_optree.c 26 | tool list text units | `== 1` | IsMetric() | helper |
| cui_misc.c 252 | File Info "Units" | blank for -1 / 2 | IsMetric() (ribbonui FUN_1800fa5b0) | helper |
| cui_verify.c 479 | removed volume units | `== 1` | IsMetric() | helper |
| core_bridge.c (Qt) screen_colors_load / load_view_factors | configuration read | mcamxm.config first, always | current configuration | current units first; read again on a switch |
| cam_fileop.c defaults_file, cui_ops.c default_op, mcam_saveops.c mcam_defaults_library, cui_toolpage.c tlm_ | caches | keyed by units | - | not stale (keyed) |
| cui_draft.c dr_S_units | drafting globals cache | keyed by units | - | not stale (keyed) |
| **cui_draft.c 46 dr_metric** (drafting agents' file, not edited) | drafting units | `== 1 \|\| < 0`: mixed = inch | IsMetric() | **should be `mcam_is_metric(&F)`** |
| **scene.c 504 draft_build** (drafting part, not edited) | config 15007 | raw F.metric | current configuration | **should pass `mcam_is_metric(&F)`** |
| **draft_create.c 798 / cui_draft.c 480** (not edited) | DIM record units | S->metric from the globals | the dimension's own | fine as long as S->metric comes from dr_metric |
| cam2d.c cam_defaults_base | built-in op values (feed 600, clearance 25, ...) | mm only | - | ours, used only without a defaults library: TODO |
| engines' tolerances | FUN_1803bc7d0 defaults | constants | config 1000-1008 of the current configuration | McSysTol (section 8) |

## 8. System tolerances: the configuration items (2026-10-27)

Decompiles: `decomp/mccore_systol.c` (FUN_1803bc7d0, FUN_1801aed10, FUN_1801bab70, FUN_1801b6bd0), `decomp/units/systol_readers.c`
(FUN_1803bd410 / FUN_1803bcb10, FUN_1801f9680, ReadCfgFile's section readers incl. FUN_1801ba530, FUN_1801bb1f0, the missing-item
bookkeeping FUN_180178120 / FUN_1801781b0 / CfgErrorDialog_REPORT), `systol_callers.txt` (CallersOf / SymWrites of the globals),
`core_cfgcallers.c` (ReadCfgFile body FUN_1801bb2c0, startup FUN_1803b9f90), `core_units.c` (ChangeConfiguration).

| item | label (user's file) | mccore global | default metric / inch (FUN_1803bc7d0) | user's mcamxm / mcamx |
|---|---|---|---|---|
| 1000 | System tolerance | `tol` @1812b5b58 | 5e-5 / 5e-5 | 0.00005 / 0.00005 |
| 1001 | Chaining tolerance | `chain_tol` @1812f7cb0 | 0.002 / 0.0001 | 0.002 / 0.0001 |
| 1002 | Planar chaining tolerance | `chain_plane_tol` @1812d3c00 | 0.02 / 0.002 | 0.02 / 0.002 |
| 1003 | Minimum arc length | `min_arc_length` @1812f7cd0 | 0.02 / 0.0002 | 0.02 / 0.0002 |
| 1004 | Curve minimum step size | `curve_min_step` (mastercam.dll) | 0.02 / 0.001 | 0.02 / 0.001 |
| 1005 | Curve maximum step size | `curve_max_step` (mastercam.dll) | 100 / 100 | 100 / 100 |
| 1006 | Curve chordal deviation | `curve_chord_dev` (mastercam.dll) | 0.02 / 0.001 | 0.02 / 0.001 |
| 1007 | Maximum surface deviation | `surf_step_chord_dev` @1813066f8 | 0.02 / 0.001 | 0.02 / 0.001 |
| 1008 | Toolpath tolerance | `nc_tol` @1812b5b68 | 0.002 / 0.0001 | 0.002 / 0.0001 |
| 7004 | Chord height (shading) | `gl_tolerance` (mastercam.dll) | 0.025 / 0.001 (mastercam.dll FUN_1801b4070) | 0.025 / 0.001 |

(`msf_join_tol` 0.2 / 0.01 is also set by FUN_1803bc7d0 but has no item.) **The user's files hold exactly the defaults**, so
every engine sees the values it had before this change.

- Item -> global: ReadCfgFile (FUN_1801bb2c0 l.131) calls FUN_1801bab70(key, value) for every parsed item when the [TOLERANCES]
  section flag (+0x1a) is on (a switch on 1000 .. 0x3f0, no range check); 7004 goes through FUN_1801ba530 (flag +0x17, case
  0x1b5c -> `gl_tolerance_exref`). After the file, FUN_1801b6bd0 (core_cfgmisc.c l.185) raises nc_tol, min_arc_length,
  curve_min_step, curve_chord_dev, surf_step_chord_dev to `tol` and min_arc_length to nc_tol.
- Defaults: FUN_1803bc7d0 is called only by FUN_1803bd410 (section flag +0x1a) with the units in DAT_1813eb30c; FUN_1803bd410's
  callers are FUN_1803bcb10 (startup FUN_1803b9f90: inch defaults + mcamx.config check, metric + mcamxm.config check, then the
  current units' defaults), the Configuration dialog's restore-defaults path FUN_180174730 (local_41e4 2 / 3) and FUN_18018d4d0.
  **ChangeConfiguration does not reset them**: it calls ReadCfgFile(current, 1, 1, 1) only, so an item the new file lacks keeps
  the value it had (FUN_180178120 / CfgErrorDialog_REPORT only clear the missing-item lists; no defaults merge).
- Other writers (SymWrites): chain_tol / chain_plane_tol by the Chaining Options OK (FUN_1801f9680: the dialog's values, the
  same globals) and AutoChainTol; nc_tol temporarily by FUN_1800edba0; tol by FUN_18014a300 / FUN_180174730 / FUN_1809b5210
  (temporary). The Tolerances page (FUN_1801aed10) bounds edits to 1e-8 .. 10000.

Port: `McSysTol mc_systol` (mcam_load.h; mcam_saveops.c `mc_systol_defaults` = FUN_1803bc7d0 + FUN_1801b4070, `mc_systol_item`
= FUN_1801bab70 / FUN_1801ba530, `mc_systol_check` = FUN_1801b6bd0, `mc_systol_units(metric)`: the first call = startup (defaults
of those units, then the file), a call with other units = ChangeConfiguration (the file over the values held), reading
mcam_config_value's file). Re-read on: cui_units_set (the switch), scene.c load_file (open: the part's units), cui_config_done
(every front-end configuration read, incl. cui_hooks.config_reload). Readers:

| reads | site |
|---|---|
| tol (1000) | CAM_SYS_TOL (cam2d.h: every engine use), cam2d_dyn.c Peel, mcce_dyn.c LoadDynPockParm floors, cam3d_blend.c, cam2d.c cam_tool_tip_length, cam_fileop.c / cam_cli.c Area Mill macro, feedspeed.c TOL, nci_reverse.c TOL, nci_write.c DTOL, cam_filter.c, core_ui cfg_sys_tol (tool manager filters, op dialog, chain dialog bounds), EX_TOL (extrude), BB_TOL (bounding box), BC_TOL (stock), Stock Setup NCI extents |
| chain_tol (1001) | cam_chain_tol(metric) (cam2d.c spline sampling, cam_fileop.c, cam_cli.c, Pocket, Pocket entry, Facing / OptiRough / Dynamic mcce chain_tol), core_ui cfg_chain_tol (chaining, Chaining Options, script `gen:`) |
| chain_plane_tol (1002) | cam_chain_plane_tol (cam2d.c plane filter, Chaining Options) |
| min_arc_length (1003), nc_tol (1008) | cam2d.c g_min_arc / g_nc_tol (cam_generate_op: every 2D engine, MC_NC_TOL / MC_MIN_ARC, srough chord fallback), cam_filter.c (cam_cli nci-filter), cui_sldchain.c nc_tol |
| curve_min_step / curve_max_step (1004 / 1005) | cam2d.c spline breaking (chain_to_ctour_array_break) |
| curve_chord_dev (1006) | cfg_curve_chord (Stock Setup arc extents, NCI extents chord) |
| surf_step_chord_dev (1007) | no reader here yet |
| gl_tolerance (7004) | scene.c surface shading, cui_stock.c cylinder stock mesh |

Not changed (no configuration item behind them, cited constants): mcce_dyn.c's IsMetric() 0.025 / 0.002 chain tolerance cap,
cam2d_dyn.c tolP, area_mill.c / cam3d_horiz.c 0.025 / 0.001, cui_feat.c sort_pock_bounds 0.025 / 0.001 (FUN_180039370), toolprofile.c
build_auto_mill_tool arguments, nci_write.c make_reals_0. Left as they were (uncited 5e-5 / CAM_CHAIN_TOL): cam3d_srough.c l.312 `tol`,
cam3d_finish.c MCSystemContext eps, cam2d_ops.c proj_pt_ar, cui_analyze.c (Analyze's tolerance), the drafting files' TOL
(draft*.c), mcam_load.c spline display polyline (ours). The Configuration dialog's Tolerances page is not in this program.

## 9. Operations after a units switch (2026-10-27)

Decompiles: `decomp/units/units_ops_assoc.c`, `systol_callers.txt`, `core_scale.c`, `core_units.c`, `core_cfgcallers.c`.

- Nothing on the switch converts operations, tools or stock: SetUnits / ChangeConfiguration / the Configuration OK FUN_180170520 /
  fmu_scale_yn / fmu_scale_db (sections 1, 4) contain no op, tool or stock call; FUN_1805f9da0 converts only RegisterVariable'd
  values, whose only users outside mccore's CAD code are mcgeomsld / mccad / mccadutil and mcmill's
  `run_drill_point_selection_dialog` (CallersOf in mcmill.dll: the drill point dialog's session values). mastercam.dll calls
  SetUnits only in InitInstance (and CncGaf::GrIBeam::SetUnits in FUN_1801d9220, a display unit), ReloadConfiguration only in
  init_db. No message for converting operations exists in any *res.dll (pe_strings). The tool units stay (TlAssemblyItem
  SetIsMetric callers: loaders / converters only, none on this path).
- **Scale current part: Yes** -> fmu_scale_db -> FUN_1808d8820 with param_8 = 1 (disassembly 1808d3862: `[RSP+0x38] = 1` ->
  DAT_18143d6a8, associativity on): CXfAssocInfo::Load(0x1f) collects the ops' geometry (FUN_1800341c0); each scaled entity is
  stored as a new record (xform_store_ent -> CXfAssocInfo::Check(old, new, ids, true)); CXfAssocInfo::End -> tp_change_eptr(old,
  new, .., 1, 1): every op whose chains / points used it is re-pointed and, unless already dirty, gets op +0x94 = 1, +0xf2 = 1,
  +0x97 = 0 (DatabaseRetrieve / UpdateListAndDB), then mark_op_dependents. The parameters are not touched.
  Port: xf_scale_all's mod_pair -> dirty_ent_users (cui_undo.c, the same rule as Move) - nothing new.
- **Scale current part: No** (or an empty part, or opening a part in other units): no op is marked dirty, none converted.
  Port: unchanged - CamOp / F.ops keep their state; "Regenerate all dirty" (op +0x94) does not pick them up, as in Mastercam.
- Regenerating such an op (Regenerate all selected) computes it with its numbers as they are, in the new units: NCI 1016 +0x44 =
  IsMetric() (CamOp.gen_units = the new units). Posting it without regenerating: Mastercam posts its old NCI with its own 1016
  +0x44 (met_tool$; what an MP post does with it is the post's) - **our LinuxCNC post refuses it** (no conversion decoded):
  "operation N's toolpath was computed in mm and the part is now in inch - regenerate it first (not dirty: Regenerate all selected)".
- Test: tools/tests/units_test.sh scenes a (scaled: op dirty) and e (unscaled: not dirty, refused, regenerated selected, G20).
