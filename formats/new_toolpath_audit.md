# New-operation audit of the Mill "Toolpaths" ribbon tab (2026-11-19)

Question: can every command on the Toolpaths (Mill) ribbon tab (viewer/ui/layout.txt l.623-706: the 2D, 3D and Multiaxis
galleries and the Utilities group) create a new operation?

How it was run: the Qt app (a copy of viewer-qt/build/apprenticecam, branch op-rename, built 2026-11-19 19:09), headless
(`QT_QPA_PLATFORM=offscreen`, `ulimit -v 6000000`), one scene at a time, each in a fresh work folder holding copies of
tools/tests/screenshots/PARTS, LIB = mill_mm.tooldb, LIBIN = mill_inch.tooldb and its own XDG_CONFIG_HOME (as
tools/tests/op_compare.sh), with `MCAMUI_OPDUMP=1 DUMP_TREE=1`. Every command is started the way its button does it:
`galitem:Toolpaths@Mill|<gallery>|<item>[#n]` (prints `gallery KEY: on|off`, clicks when on) or
`ribbon:Toolpaths@Mill/Utilities/<label>[/<sub>]` (prints `ribbon ... : on|off`). Then the geometry, the op dialog, OK, and
the checks: (a) the new tree row; (b) points in the `op N` dump line, no error; (c) `selops:K;tpregen`; (d)
`saveas:.mcamui` + `export:.mcx-9`, `close;open:` of each; (e) `post`. Scenes: tools/tests/newop_audit_scenes.txt (93 lines,
runnable with `OP_SCENES=tools/tests/newop_audit_scenes.txt bash tools/tests/op_compare.sh OUT`; it has no golden files).

Parts: 2D and Multiaxis: `new` + rectangle 0,0-100,50 + circle at 50,25 r10, FLAT END MILL - 10 / SPOT DRILL - 8 (the
recipe of op_scenes.txt rt_*). 3D: BRACKET4.mcx-9 (16 stored ops, the new op is row 17; drive = its solid), Plunge / Blend
on the lofted surface of op_scenes.txt gen_blend (BALL tool). Every new row ends without " - comment" (L-7) and shows
`[WCS: Top] - [Tplane: Top]`.

Result values: WORKS = (a)-(e) pass with the default parameters; WORKS* = passes once one default is changed or other
geometry is used (the default's message is quoted); REFUSED = greyed (or an explicit not-ported message); BROKEN = a bug.

## Table

Greyed gallery items: viewer-qt/src/mainwindow.cpp:723-727 (`ok = gt != 0`, `setEnabled(ok)`), gt = cam_type_from_gallery
(viewer/cam2d.c:1064-1089: 3D galleries by name, 0 for the rest of a "3D " gallery at l.1086, the other galleries by
cam_type_from_label l.1088). Greyed ribbon buttons: mainwindow.cpp:488-496 cmdOk (no local command and no ported FT
function), invoke returns at l.501.

| group | command (gallery/item) | state on ribbon | result | evidence / message (cite file:line) |
|---|---|---|---|---|
| 2D | Contour | on | WORKS | `1 - Contour (2D) - [WCS: Top] - [Tplane: Top]`, 65 pts; regen 65; .mcamui reopen same; .mcx-9 reopen stored op 64 pts, regen 65; NC written (na2_contour) |
| 2D | Drill | on | WORKS | `1 - Drill/Counterbore - ...`, 7 pts; .mcx-9 reopen `op 1 Canned Cycle ... 8 pts`, same row (na2_drill). The cycle text is an inference (optree_text.inc:52-55) |
| 2D | Dynamic Mill | on | WORKS | `1 - 2D High Speed - ...` (opcodes.tsv 134), 5921 pts; .mcx-9 reopen stored 8767 pts; regen 5921 |
| 2D | Face | on | WORKS | `1 - Facing - ...`, 51 pts. .mcx-9: "1 operation(s) written without their toolpath (where Mastercam starts a new NCI pass is NOT DECODED for their type)" (mcam_saveops.c:2051), reopens DIRTY 0 pts, regen 51 |
| 2D | Dynamic Contour | on | WORKS | `1 - 2D High Speed - ...`, 599 pts, warning "Dynamic Contour: unverified (no Mastercam NCI to compare) ..." (cam2d_dyn.c:570); .mcx-9 without toolpath, regen OK |
| 2D | Pocket | on | WORKS | `1 - Pocket (Standard) - ...`, 459 pts; .mcx-9 reopen stored 1169 pts, regen 459 |
| 2D | Peel Mill | on | WORKS* | default part (closed rectangle): "Peel Mill failed: Peel Mill needs open chains (closed chain: LogError 8)" (cam2d_dyn.c:396), dialog stays open, no op. Two open lines (na2_peelmill_open): `1 - 2D High Speed - ...`, 6283 pts, all checks pass |
| 2D | Area Mill | on | OK | `1 - 2D High Speed - ...`, 24 pts, (a)-(e) pass; the reopen + regen sequence (bug B1, fixed 2026-11-19) is scene op_areamill_reopen_regen |
| 2D | Blend Mill | off | REFUSED | greyed |
| 2D | Slot Mill | on | WORKS | `1 - Slot milling - ...` (opcodes.tsv 105), 295 pts; .mcx-9 without toolpath, regen OK |
| 2D | Engrave | on | WORKS* | default depth 0: "Engrave failed: The current top of stock or depth values are invalid" (engrave.c:107), dialog stays open, no op. With depth -0.5 (`oppage:5;pfset:1022=-0.5`, na2_engrave_depth): `1 - Engrave - ...`, 9 pts, all checks pass |
| 2D | FBM Mill | off | REFUSED | greyed |
| 2D | Swept 2D | off | REFUSED | greyed |
| 2D | Swept 3D | off | REFUSED | greyed |
| 2D | Revolved | off | REFUSED | greyed |
| 2D | Lofted | off | REFUSED | greyed |
| 2D | Ruled | off | REFUSED | greyed |
| 2D | Drill#2 (Holes) | on | WORKS | same as Drill (na2_drill_2) |
| 2D | Circle Mill | on | WORKS | `1 - Circle Mill - ...`, 69 pts; .mcx-9 without toolpath, regen OK |
| 2D | Helix Bore | on | WORKS | `1 - Helix Bore - ...`, 136 pts; .mcx-9 without toolpath, regen OK |
| 2D | Thread Mill | on | WORKS | `1 - Thread Mill - ...`, 718 pts; .mcx-9 without toolpath, regen OK |
| 2D | FBM Drill | off | REFUSED | greyed |
| 2D | Auto Drill | off | REFUSED | greyed |
| 2D | Start Hole | off | REFUSED | greyed |
| 2D | Manual Entry | off | REFUSED | greyed |
| 2D | Point | off | REFUSED | greyed |
| 3D | OptiRough | on | WORKS* ² | defaults: "OptiRough failed: nothing to cut (2 levels, 4 regions)" (BRACKET4) / "(3 levels, 0 regions)" (loft) (cam3d_rough.c:1429; the first level is cut only when the top of stock is above the model, l.1373-1377); op kept as `17 - Surface High Speed - ...` 0 pts DIRTY. Loft + top of stock 30 (`oppage:12;lkset:4,1=30`, na3_optirough_loft_top): 4938 pts, warning "OptiRough Core (from outside) is NOT DECODED: cut as Pocket ..." (cam3d_rough.c:1431) |
| 3D | Pocket | on | WORKS* ² | default cutting method: "Surface Rough Pocket: High Speed with Spiral inside to outside or no channel runs mcmsurf's own High Speed engine (FUN_180376fd0 / FUN_180374bb0): NOT DECODED" (cam3d_srough.c:88), op kept 0 pts. `oppage:27;pfset:741=0` (na3_pocket_set): `17 - Surface Rough Pocket - ...`, 3732 pts |
| 3D | Project | on | WORKS ² | `17 - Surface Rough Project - ...`, 15165 pts |
| 3D | Parallel | on | WORKS ² | `17 - Surface Rough Parallel - ...`, 4379 pts |
| 3D | Plunge | on | WORKS ² | loft: `1 - Surface Rough Plunge - ...`, 2221 pts |
| 3D | Multisurface Pocket | on | WORKS ² | `17 - Multisurface Rough Pocket - ...`, 4652 pts |
| 3D | Area Roughing | on | BROKEN ² | `17 - Area Roughing - ...`, 20813 pts; row after .mcx-9 reopen `17 - Surface High Speed - ...` (bug S2) |
| 3D | Waterline | on | BROKEN ² | BRACKET4: "Waterline failed: Waterline at Z -6.9750: pocket's Voronoi failed (0x4c3, 18 tries): FUN_1803fc9b0 NOT DECODED" (cam3d_finish.c:1564), op kept 0 pts and then dropped on .mcamui reopen (S3). Loft (na3_waterline_loft): `1 - Waterline - ...` 16426 pts, row after .mcx-9 reopen `Surface High Speed` (S2) |
| 3D | Raster | on | BROKEN ² | `17 - Raster - ...`, 16222 pts; .mcx-9 reopen row `Surface High Speed` (S2) |
| 3D | Hybrid | off | REFUSED | greyed (cam2d.c:1086) |
| 3D | Pencil | on | BROKEN ² | BALL: `17 - Pencil - ...`, 151 pts; .mcx-9 reopen row `Surface High Speed` (S2) |
| 3D | Blend | on | WORKS ² | loft: `1 - Surface Finish Blend - ...`, 164 pts |
| 3D | Contour | on | WORKS ² | `17 - Surface Finish Contour - ...`, 2681 pts |
| 3D | Horizontal Area | on | BROKEN ² | `17 - Horizontal Area - ...`, 2660 pts; .mcx-9 reopen row `Surface High Speed` (S2) |
| 3D | Scallop | on | BROKEN ² | default: "Scallop: boundaries as drive curves (0x16e1): Shallow Area Detection needs the CWeave cell test (FUN_18026c540), NOT PORTED - switch it off" (cam3d_finish.c:3787), op kept 0 pts, dropped on .mcamui reopen (S3). `pfset:696=0` (na3_scallop_set): `17 - Scallop - ...` 32169 pts; .mcx-9 reopen row `Surface High Speed` (S2) |
| 3D | Project#2 (Finishing) | on | BROKEN ² | default: "Project: NCI - choose the source operation (Cut Parameters, record 0x157c = 3 is not an op of this part)" (cam3d_finish.c:3910), op kept 0 pts, dropped on .mcamui reopen (S3). `pfset:997=2` (na3_project_2_set): `17 - Project - ...` 5225 pts; .mcx-9 reopen row `Surface High Speed` (S2) |
| 3D | Flowline | off | REFUSED | greyed (cam2d.c:1086) |
| 3D | Spiral | on | BROKEN ² | `17 - Spiral - ...`, 1034 pts; .mcx-9 reopen row `Surface High Speed` (S2) |
| 3D | Radial | on | BROKEN ² | `17 - Radial - ...`, 2530 pts; .mcx-9 reopen row `Surface High Speed` (S2) |
| Multiaxis | Drill | on | BROKEN | starts the 2D Drill: dialog "2D Toolpaths - Drill/Circles Drill/Counterbore", row `1 - Drill/Counterbore - ...` (bug S1) |
| Multiaxis | Circle Mill | on | BROKEN | starts the 2D Circle Mill: dialog "2D Toolpaths - Circle Mill", row `1 - Circle Mill - ...` (S1) |
| Multiaxis | Curve, Swarf Milling, Parallel, Along Curve, Morph, Flow, Multisurface, Port, Triangular Mesh, Swarf, Rotary, Project Curve, Roughing, Port Expert, Blade Expert, Rotary Advanced (16) | off | REFUSED | greyed (cam_type_from_label returns 0) |
| Utilities | Tool Manager (RunMillToolManager) | on | not op-creating | opens the Tool Manager (mainwindow.cpp:481); ops unchanged. Mastercam: no op |
| Utilities | Stock Model (button, stock_model_dialog) | off | REFUSED | greyed. Mastercam: creates a Stock Model op (opcode 0x8b, formats/opmanager_menu.md l.100) |
| Utilities | Stock Model > Stock Model | off | REFUSED | as above |
| Utilities | Stock Model > Export as STL | off | REFUSED | greyed. Mastercam: no op (live only on a Stock Model op, opmanager_menu.md l.55) |
| Utilities | Stock Model > Convert to PMesh | off | REFUSED | greyed. Mastercam: no op (as above) |
| Utilities | Stock Model > Viewer (stock_model_compare_dialog) | off | REFUSED | greyed. Mastercam: no op (as above) |
| Utilities | Probe (probe_interface) | off | REFUSED | greyed. Mastercam: creates a Probe op (0x96, opmanager_menu.md l.98) |
| Utilities | Multiaxis Linking (safelink_toolpath_dialog) | off | REFUSED | greyed. Mastercam: opcodes.tsv 154 "Multiaxis Link"; whether this button makes it: NOT CHECKED |
| Utilities | Toolpath Transform (xform_toolpath_dialog) | off | REFUSED | greyed. Mastercam: creates a Transform op (opcode 4, opmanager_menu.md l.96) |
| Utilities | Convert to 5-axis (ConvertTo5axToolpathEntry) | off | REFUSED | greyed. Mastercam: NOT CHECKED |
| Utilities | Trim (trim_toolpath_dialog) | off | REFUSED | greyed. Mastercam: creates a Trimmed op (0x67, opmanager_menu.md l.97) |
| Utilities | Nesting (do_nest_t) | off | REFUSED | greyed. Mastercam: creates a Nesting op (0x6c, opmanager_menu.md l.96) |

² every 3D type, also when it WORKS: the .mcx-9 export writes it without its toolpath (mcam_saveops.c:2034, 2051), and the
reopened op cannot be regenerated, so the post after that reopen writes no NC (bug B2). The .mcamui round trip works.

Type names: every row text is a Mastercam resource name (formats/opcodes.tsv through mcam_opcode_name, the contour /
pocket subtypes and the drill cycle texts of optree_text.inc otx_desc) except the 3D HST styles of a new op (Area Roughing,
Waterline, Raster, Pencil, Horizontal Area, Scallop, Project, Spiral, Radial): these are our CamParams labels
(cam2d.c:1030 TYPE_LABELS, the gallery item names), not op_make_description's text (bug S2). Not checked: whether
op_make_description adds a style to "2D High Speed" / "Surface High Speed" - otx_desc's decode (optree_text.inc:8-13)
lists suffixes only for contour, pocket and drill.

## Summary

75 commands: 21 WORKS (17 with the defaults, 4 WORKS* after one parameter / geometry change), 41 REFUSED (greyed: 12 in
2D, 2 in 3D, 16 Multiaxis, 11 Utilities), 12 BROKEN (1 2D, 9 3D, 2 Multiaxis), and the Tool Manager (on, not
op-creating). No crash; no command printed a NOT-ported message on click (the refusals are all greyed items).

## Bugs

Fixed on branch op-rename (after this table was made): **S1** (cam2d.c cam_type_from_gallery: the Multiaxis galleries' items
are greyed - Multiaxis Drill / Circle Mill are now REFUSED like the other 16) and **S2** together with a decoded gap in every
High Speed row: op_make_description (mccore @1800c7720, decomp/mccore_descriptions.c l.369-376) appends " (" + style + ")" to
0x84 Surface High Speed (FUN_1800b7130: record 0x16bc style, 0x16c5 rest) and 0x86 2D High Speed (FUN_1800b7080: 0x1228 style,
0x17b8 rest, 0x17d8 peel; decomp/mccore_hst_desc.c), texts mccoreres 541 / 542 / 4609 ... / 288 / 4642 / 365 / 4641 ...;
optree_text.inc otx_hst_style. Rows now read e.g. `2D High Speed (2D Dynamic Mill)`, `Surface High Speed (Raster)`, the same
before and after the .mcx-9 round trip (an op of ours: its opcode from the save's mapping, mcam_save_opcode_for). After the
fixes (re-run with the scenes of this file): 30 WORKS (the 9 3D items that were BROKEN only by S2 now WORKS ² / WORKS* ²),
43 REFUSED (+ Multiaxis Drill / Circle Mill), 0 BROKEN in 2D (2D Area Mill B1 fixed 2026-11-19); B2 fixed 2026-11-19 (every 3D item regenerates after the
.mcx-9 reopen with the same moves and posts: tools/tests/rt3d_roundtrip.sh 0/17 fail) and S3 fixed (the failed op is kept dirty on
reopen, user answer Q5). Open: none of the audit's bugs; the greyed commands are not ported.

### Small, fix candidates

- **S1 Multiaxis Drill / Circle Mill start the 2D ops.** `nam_drill`, `nam_circlemill`:
  `new;draw:Rectangle|0,0|100,50;draw:Circle Center Point|50,25|60,25;libtool:LIB|FLAT END MILL - 10;libtool:LIB|SPOT DRILL - 8;galitem:Toolpaths@Mill|Multiaxis Mill Toolpaths Gallery|Drill;chainat:60,25;drillok;tppick:1;opok;camops`
  -> `gallery ...|Drill: on`, dialog "2D Toolpaths - Drill/Circles Drill/Counterbore", row `1 - Drill/Counterbore - ...`.
  Cause: cam2d.c:1088 cam_type_from_gallery sends every non-"3D " gallery to cam_type_from_label(item), so a Multiaxis item
  named like a 2D type is live (the same bug the comment at cam2d.c:1061-1062 records for the 3D gallery). Mastercam's
  Multiaxis items are the multiaxis toolpaths (opmanager_menu.md l.100 "multiaxis `*Entry`"; opcodes.tsv 28 "Drill 5 Axis",
  113 "Circle 5 Axis"). Fix: return 0 for the Multiaxis gallery (and any gallery not known to hold 2D types).
- **S2 new 3D High Speed rows use our label, the saved op shows "Surface High Speed".** `na3_raster`:
  `open:PARTS/BRACKET4.mcx-9;galitem:Toolpaths@Mill|3D Mill Toolpaths Gallery|Raster;drvsolid:1;drvdone;opok;export:@D@/x.mcx-9;close;open:@D@/x.mcx-9`
  -> `17 - Raster - ...` before, `17 - Surface High Speed - ...` after (OptiRough, also op 132, shows "Surface High Speed"
  from the start). Cause: optree_text.inc:29-32 otx_opcode takes mcam_opcode_for (mcam_saveops.c:51-70), which knows only
  OptiRough among op 132's styles; save_opcode_for (mcam_saveops.c:244-254, static) maps the other styles to 132, so
  otx_desc falls back to `o->type`. Fix: give otx_opcode the save's opcode (save_opcode_for exported or its 3D cases
  moved into mcam_opcode_for).
- **S3 a new op whose toolpath failed is dropped when its project reopens.** `na3_drop_on_reopen`:
  `open:PARTS/BRACKET4.mcx-9;galitem:Toolpaths@Mill|3D Mill Toolpaths Gallery|Waterline;drvsolid:1;drvdone;opok;saveas:@D@/na3_drop_on_reopen.mcamui;close;open:@D@/na3_drop_on_reopen.mcamui`
  -> before save `17 - Waterline - ...` 0 pts DIRTY; after reopen 16 ops and "1 of 1 operation(s) could not be
  regenerated (the part geometry changed?) and were dropped." (cui_doc.c:465). Same for OptiRough, 3D Pocket, Scallop,
  Project#2 with their defaults. Cause: cam2d.c:4947 cam_load_ops_ord keeps an op only when cam_generate succeeds
  (`else free(cur)`), while a new 3D op with an error is kept in the tree (cui_ops.c:674 gen_end -> ms_gen_end). Fix: keep
  it as a dirty op with its error, as before the save.

### Bigger

- **B1 Area Mill: Regenerate after reopening the exported part does not return - FIXED 2026-11-19 (branch op-rename-b1).**
  Not a hang and not session state: every Area Mill generation of this part got bit-identical inputs (env-gated dump in
  gen_area_hmm / area_mill_passes, all runs: metric, D 10, tol 0.02, stock 1, MinOffset 4.95, MaxOffset 9, T0 = the 4-point
  rectangle 6,6-94,44, grid h 0.0409, 2150 x 932 cells) and each took ~47 s in am_grid_build. The sequence generates it
  4 times (OK, Regenerate, the project reopen, the last Regenerate), so it passed op_compare's 120 s only when it had
  fewer generations (the shorter variants). Cause: the grid's nearest-boundary ring search (OURS, the port's stand-in for
  McHmm's exact Voronoi, formats/area_mill.md 11.5) looped over the full (2r+1)^2 square of every ring and walked all the
  empty rings up to the boundary (a rectangle = 4 segments, 0.5 bucket size). Fix (area_mill.c am_grid_build): the ring
  search starts at the bucket's Chebyshev distance to the nearest non-empty bucket and visits only the ring's buckets, in
  the old order: the grid (clearance, nearest points, axis cells) is bit-identical (checked on 5 shapes), 52 s -> 1.4 s.
  24 pts is what rule 11.3 gives with this record's MinOffset 4.95 / MaxOffset 9: passes at t = 15.62, 7.81, 0 (three
  rectangles; 4.95 + 4.05 sin 45 = 7.81, 12.76 + 2.86 = 15.62, next 20.57 > hi 19).
- **B2 3D ops do not survive the Mastercam-part round trip.** Every 3D command (e.g. `na3_parallel`): .mcx-9 export ->
  "1 operation(s) written without their toolpath" (mcam_saveops.c:2034, 2051: only drill, contour, pocket and 2D HST with
  NCI blocks keep theirs); on reopen the op is DIRTY 0 pts, `selops:17;tpregen` -> "Surface Rough Parallel operations
  can't be regenerated here yet" (cam_fileop.c:676: cam_fileop_type, cam_fileop.c:38-52, has no 3D opcode), and the
  following `post` writes no NC (the dirty-op check). The 2D types that are written without toolpath regenerate fine.
  Needs the 3D record -> CamParams path (params_from_record for opcodes 5, 7, 10, 15, 44, 109, 131, 132).

## Other observations (not counted as bugs)

- A failed 2D generation keeps the dialog open and creates no op (Peel, Engrave); a failed 3D one creates the op with 0
  points, DIRTY (cui_ops.c:674 -> ms_gen_end, the S4 flow).
- After a .mcx-9 reopen, the op dump shows `T0` for ops written without toolpath (dump only; the row is right).
- `post` of a `new` part names the NC `-mcamui.nc` even after `saveas:` of the project (the part has no path; the
  goldens hold the same names, e.g. tools/tests/golden/op/dlg_cut_edits.-mcamui.nc).
- The BRACKET4 posts include the empty op 17 when its generation failed ("posted 17 operation(s)").
- Not tested: the Router / Lathe / Mill-Turn galleries, other machine groups, non-Top planes; the greyed commands'
  Mastercam behaviour beyond the op codes cited.
