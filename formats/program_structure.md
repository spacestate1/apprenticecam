# apprenticeCAM program structure: binaries, source files, our own file formats (2026-10-16)

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

What each program and source file in `viewer/` is for, the data model they share, and the file formats that are
**ours** (intentional, not Mastercam's). The Mastercam behaviour each file reproduces is specified in the spec named
in `formats/README.md` (index); this file covers the infrastructure that has no other home. Everything here is read
from the code (file:line or function names given); nothing is decoded from Mastercam unless a source is named.

## 1. Programs (viewer/Makefile)

| target | sources | what it is |
|---|---|---|
| `apprenticecam` (viewer-qt, CMake) | viewer-qt/src/*.cpp + core_bridge.c, and the core sources of viewer/ (rpx, scene, mcam_load, part_tools, xt_mesh, cam2d (+ cam2d_ops / _dyn / _area / _pock / pgeo, included), mcce_dyn, toollib, toolprofile, toolwiz, feedspeed, backplot, mcam_write, mcam_saveops, geo2d, solid25, cam_fileop, lathe_face (Lathe Face, opcode 66), nc_post, nci_reverse, area_mill, verify, core_ui/*.c, offset.cpp, Clipper 6.2) | the workbench GUI (Qt 6 Widgets + SARibbon + Qt-ADS, OpenGL 3.3), Mastercam 2017 layout; built by `cmake -S viewer-qt -B viewer-qt/build` (formats/qt_port_plan.md). Until 2026-10-22 this was `mcamui` (mcamui.c, SDL2 + Nuklear), removed in phase 9 |
| `cam_cli` | cam_cli.c + the CAM / data files above (no UI) | command-line harness: generate, regenerate stored ops, post, reverse, export (section 4) |
| `tool_cli` | tool_cli.c, toollib, toolprofile, mcam_load, part_tools | tool library harness (formats/toolmanager_ui.md / toollib_net.md) |
| `mcamview` | mcamview.c | first SDL2 software viewer (wireframe + stored NCI playback; viewer/README.md) |
| `xt2obj` | xt2obj.c, xt_mesh, mcam_load, part_tools | `xt2obj PART SCHEMA out.obj`: meshed Parasolid solids as OBJ (formats/xt.md) |
| `nc_dev` | nc_dev.c | `nc_dev A.nc B.nc`: max 3D deviation between the feed paths of two G-code programs (densified to 0.01 mm) |
| `core` | the core sources above (Makefile CORE_C / CORE_UI) | compiled to core_obj/*.o with the Qt build's flags, linked into nothing: a warnings check without CMake |
| `windows` | cam_cli + the core sources | MinGW-w64 cross build: `make windows WINDEPS=DIR` -> win/cam_cli.exe + the core objects (the Qt app's Windows build is open, qt_port_plan.md phase 9) |

Dependencies (Linux): GLEW, OpenGL, sqlite3, a C11 compiler and a C++ compiler (offset.cpp, vendor/clipper6); SDL2 for
mcamview only; Qt 6 + CMake for the GUI. Bundled third-party code: `viewer/vendor/` = Clipper 6.2.0 (Boost licence)
(Nuklear and stb_image went with mcamui, 2026-10-22). `make` builds all targets; `make clean` removes them.

## 2. Source files without a spec of their own

| file | purpose | notes / sources |
|---|---|---|
| `mcamui.c` (removed 2026-10-22) | was the nuklear GUI; its behaviour moved to core_ui/ (phases 2-8) and the Qt app (viewer-qt/src: mainwindow / managers / backstage / testhooks / the dialogs). The line numbers cited by older notes refer to the copy in ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz | Behaviour specs: ui.md, wireframe.md, extrude.md, opmanager_menu.md, analyze.md, level_picker.md, renumber_tools.md, backplot.md, toolmanager_ui.md, provenance_audit.md |
| `optree_text.inc` | the Toolpaths manager item texts (InitTvItem / InitTvOpText), included by core_ui/cui_optree.c | mcamui_dialogs.md, opmanager_menu.md. The other `*_ui.inc` (levels, geopanel, solidsmgr, toolmgr, backplot, opparams, chaindlg) were mcamui-only and were removed with it |
| `cam_cli.c` | CLI harness (section 4); `--verify` replays the moves on a raster (cell D / 60) and reports material left, gouges (> 0.02 mm) and the engagement angle | the raster model is ours (a test tool, not Mastercam's Verify) |
| `mcam_load.c` / `.h` | part-file reader: record stream (`[u32 len][record]`, out-of-band blocks skipped as mccore FUN_1802c62d0), points / lines / arcs (arc view matrices: mastercam view_to_world @180341520), OP_ID / TP_ID / TL_ID / TlMgr (via part_tools.c) / SOLIDXMT / NCI tail, ops grouped in Operations Manager order (+0xe6) | layouts: formats/mcam.md; data model section 3 below |
| `mcam_write.c` / `.h`, `mcam_saveops.c` / `.h` | writers: unchanged rewrite and "Save as MCAM part" (new ops, edited ops, tools, new parts) | formats/mcam_write.md sections "Implementation", 8, 9, 10; part_tools.md 10 (TlMgr writer) |
| `xt_mesh.c` / `.h`, `xt2obj.c` | Parasolid XT reader and display mesher (display only) | formats/xt.md; mesher = ours (SUBSTITUTE) |
| `platform.h` | every filesystem rule, Linux and Windows: separators, absolute paths, config folder (Linux `$XDG_CONFIG_HOME/mcamui` or `~/.config/mcamui`, Windows `%APPDATA%\apprenticeCAM`, `MCAMUI_CONFIG_DIR` overrides), home / exe folder, `host_path()` for Windows paths written by Mastercam (drive letters via paths.conf `drive_x`, case-insensitive component match) | ours; mirrored for the Python tools by tools/apppaths.py |
| `paths.h`, `paths.conf.example` | site folders `shared`, `user`, `schema`, `cad`, `posts`, `drive_<x>` from `<config dir>/paths.conf` or `MCAMUI_<KEY>` (section 5.2) | ours |
| `opcodes.h` | Mastercam operation code -> name, generated from formats/opcodes.tsv | data from mccore op names |
| `offset.cpp` / `.h` | C wrapper over Clipper 6.2 (polygon offset / boolean) | SUBSTITUTE for Mastercam's offsetter; see formats/mcce_port_plan.md 3 |
| `mcamview.c`, `nc_dev.c`, `tool_cli.c` | small programs (section 1) | (mcamgl.c, the Nuklear viewer, was removed 2026-10-22) |

## 3. Data model (viewer/mcam_load.h, cam2d.h)
- `McamFile` = one loaded part: entities `Ent` (exact wireframe for selection / chaining / CAM: type, level, colour,
  points a / b / c, radius, start angle, sweep, `created` = drawn here, `idn`), drawable polylines `Poly`, operations
  `Op` (id, opcode, comment, tool, Operations Manager order, stored NCI section, `stale`, `nrev`), TP_ID references
  `TpRef`, tools `Tool` (TL_ID + TlMgr fields, holder outline, `op_info` = the 0x288-byte op_tool_info block), solids
  `Solid` (XT bytes), `metric` (header +0x6f, else NCI).
- `Move` = one generated move (rapid flag, point, feed; arc fields `arc`, `cw`, `cx`, `cy` for the engines' true arc
  records, cam2d.c mb_tag_arc).
- `CamOp` (cam2d.h) = an operation made or regenerated here: `CamParams p` (type, tool, heights, depths, feeds,
  per-type fields; `file_op_id` = the stored op it replaces), chains (`Chain`: entity indices + reverse flags, closed,
  avoid, Dynamic cuts `cut0` / `cut1`, `solid_idn`), holes, tab positions, record patches `rpatch` (bytes for the
  Mastercam op record written at export, e.g. Linking flags), moves.
- Mutators: `mcam_new_empty` (File > New), `mcam_new_ent` / `mcam_delete_ent`, `mcam_add_op` / `mcam_remove_op` /
  `mcam_move_op` (Operations Manager order = `F.ops` order = post order), `mcam_op_set_nci`.

## 4. cam_cli (viewer/cam_cli.c)
```
cam_cli PART list                                         chains of the part (index for the commands below)
cam_cli PART TYPE CHAIN[,CHAIN..] [options]               generate one op; TYPE = contour pocket drill face slot helix
                                                          circle thread dynamic area dyncontour peel
cam_cli PART fileop OPID [--moves F] [--nc F] [--set k=v,..] [--mcam OUT]   regenerate a stored op (edits: --set)
cam_cli PART post [OUT.nc] [--style r180|ij-quad]         LinuxCNC post of all ops (formats/post.md)
cam_cli PART reverse N[,N..] [--post OUT.nc] [--save OUT] Reverse toolpath (formats/reverse_toolpath.md)
cam_cli PART areaparams OPID                              the Area Mill NCG macro parameters (formats/area_mill.md)
cam_cli PART bpinfo ...                                   backplot motion list + Info values (formats/backplot.md)
```
Options of the generate form: `--dia --depth --top --step --stepover --side out|in|on --stock --finish --lead --tabs
--tabpts --noarcs --tool N --lib LIB --libtool NAME --tipcomp --coolant --chamfer W --chamfer-tip T --engrave --pitch
--major --external --lh --passes --single --outside --wall --width --minrad --oneway --angle --box X0 Y0 X1 Y1 --holes
--nc OUT.nc --svg OUT.svg --side-svg --mcam OUT --verify` (tests, also on `fileop`: `--movesf F` = --moves + the feed, `--params F` =
the op's parameters as the sidecar writes them). `--engrave` is ours, not a Contour type: a 2D contour with Compensation Off (record
0x5d0 = 4) and Lead In/Out off (0x8f0 = 0), the fields the saved op carries (formats/mcam_write.md 12). Environment: `VERIFY_HIGH` (print high-engagement moves),
`VERIFY_PGM` (write the verify raster), `MCAM_H_OFFSET=tool` (H<tool> instead of H0), `MCCE_DEBUG=1..3`,
`PEEL_DUMP=FILE` (Peel Mill Motion / Material loops before / after PeelAesthetics, testing), `PK_DEBUG` (pocket engine
trace). (`MCCE_DCTOUR` is gone: Dynamic Contour is generated since 2026-10-23.)

## 5. Our own files (INTENTIONAL: not Mastercam formats)

### 5.1 `.mcamui` project (mcamui.c doc_write / doc_read l.1346-1520, cam2d.c cam_write_ops / cam_load_ops_ord)
Text, one record per line; written atomically (`<path>.tmp`, the previous file kept as `<path>.bak`). The part file
itself is never written (doc_write refuses .mcam / .mcx* paths). Opening a part `X` also loads `X.mcamui` when it
exists.
```
MCAMUI-PROJECT 1
# comment line
PART <absolute part path>            | NEWPART          (a new part = mcam_new_empty)
GEO <type> ax ay az bx by bz cx cy cz r a0 sweep level colour      geometry drawn here, in order (type 0 = drawn then deleted)
DEL <idn>                            a part entity deleted here
EXTRUDE <20 fields> <name>           an Extrude feature (field list in mcamui.c feats_write), followed by
XCHAIN <closed> <n> (<ent> <rev>)*n <dir>      one line per chain of that feature
MACHINE <name>
TOOLS <n>  then  TOOL k=v ... name=<name>  [THOLDER <n> r z ... <name>]    the part's tool list (toollib.c tool_write)
OP <type> <name>                     an operation made / edited here, then:
  P <key> <value>                    every CamParams field by name (cam2d.c PKEYS table)
  CHAIN <closed> [A] [T<cut0>,<cut1>] (<ent>:<rev>@<type>,<ax>,<ay>,<bx>,<by>)* | S<solid idn>
  HOLE x y d   |  TAB x y  |  RP <record offset> <height> <hex bytes>
END
TREE C<k>|S<id> ...                  Operations Manager order (C = k-th OP block, S = stored op id)
STALE <op id>                        a stored op whose tool was changed here
REVERSED <op id> <count>             Reverse toolpath applied count times (replayed on open)
```
Chains carry the entity's type and end points so a moved index is found again; an op whose geometry is gone is
dropped with a message ("N operation(s) could not be regenerated"). Older files: `P drill_cycle` absent -> peck > 0
means G83; EXTRUDE lines with 11 / 12 fields are converted (feats_read).

### 5.2 Config folder files (platform.h config_dir)
| file | written by | content |
|---|---|---|
| `paths.conf` | the user (example viewer/paths.conf.example) | `key = value` lines, `#` comments; keys `shared`, `user`, `schema`, `cad`, `posts`, `drive_c`, `drive_d` ...; lists separated by `;` (Linux also `:`); `MCAMUI_<KEY>` env wins; unset = the feature finds nothing |
| `recent.txt` | mcamui.c recent_add | up to 12 recently opened paths, newest first (not written in `MCAMUI_SCRIPT` runs) |
| `icons.pack` | tools/setup_from_install.py | icons extracted from the user's own Mastercam install (`MCAMUI_ICON_PACK` = another file, `MCAMUI_ICONS` = a PNG folder) |
| `panel_keys.tsv` | geopanel_ui.inc | wireframe panel values kept between runs (Mastercam's own storage NOT DECODED; formats/wireframe.md) |
| `toolmanager.conf` | toolmgr_ui.inc | Tool Manager settings (formats/toolmanager_ui.md 1.5.10) |
| `.tools` libraries | toollib.c toollib_save | `MCAMUI-TOOLS 1`, `UNITS mm|inch`, `TOOL` lines as in 5.1, `HOLDER <n> r z ... <name>` (formats/toolmanager_ui.md / tooldb.md) |

### 5.3 Program identity
- Name **apprenticeCAM** in the window title (`<file> - apprenticeCAM`, mcamui.c l.1153), the backstage "About
  apprenticeCAM" entries (l.729-730), the About box (l.7446: our own text; Mastercam's dialog 103 is licence / version
  information about Mastercam and is not reproduced), the unsaved-changes prompt (mccoreres 7291 with the program
  name) and message captions.
- Risk agreement (mcamui.c draw_risk_agreement l.1790-1815): shown at every start until "I understand and accept all
  risk" is pressed; no other input reaches the program before; `MCAMUI_I_ACCEPT_THE_RISK=1` skips it (scripts). Text =
  the warning in ../LICENSE.
- File dialog type "apprenticeCAM Project (*.mcamui)" (l.1585-1586) beside Mastercam's own filters.

## 6. Test hooks (no desktop needed)
The Qt app has the same hooks (viewer-qt/src/main.cpp, testhooks.cpp: `--shot out.png`, `QT_QPA_PLATFORM=offscreen`,
the script steps of core_ui and its own widget steps; tools/tests/README.md); the lines below describe them as mcamui
had them (removed 2026-10-22; line numbers = the backup copy).
- `mcamui PART --shot out.bmp [--size WxH] [--tab N] [--ui DIR]`: one frame into a BMP, hidden window, views jump.
- `MCAMUI_SCRIPT="open:P;gen:Contour:12,14;save;saveas:X;answer:save|discard;close;exit;draw:...;gset:...;sel:...;
  ents;extrude:...;lkpick:N;lkval;screen:X,Y;dump"` (mcamui.c run_script l.7475): scripted actions; the program exits afterwards
  unless `--shot` or `MCAMUI_LIVE` is set.
- `MCAMUI_EVENTS="F:click:x,y;F:clickw:wx,wy;F:move:x,y;F:down:x,y;F:up:x,y;F:key:Ctrl+Z;F:text:abc"` (l.7800): input
  pushed into SDL's queue at frame F; the state is printed as `[events]` at the end.
- `MCAMUI_TEST`, `MCAMUI_CLICKTEST=N`, `MCAMUI_DUMP_ENTS`, `DUMP_TREE`: older picking / generation / dump hooks.
- Never drive the user's live desktop (DISPLAY :1) with xdotool; use these hooks.
