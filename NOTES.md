# Mastercam 2017 reverse engineering notes

Goals: (1) file formats, (2) post processor / MP engine, (3) toolpath & geometry algorithms, (5) general Ghidra study.
Out of scope: licensing (CodeMeter / HASP / NHaspX / SIM checks) and decrypting encrypted posts (`.psb`, `NSC^` header).

## Current state (2026-10-16) - start here
The reimplementation is **apprenticeCAM** (the Qt app `viewer-qt/build/apprenticecam` + the C core in `viewer/`; the
nuklear `viewer/mcamui` was removed 2026-10-22, formats/qt_port_plan.md phase 9; MIT, README.md + LICENSE with its use-at-your-own-risk
warning). Rule for all work: no improvisation - every behaviour cites decompiled code / original data, else it is
marked NOT DECODED and left out (formats/provenance_audit.md, formats/improvisation_review.md).

**Works end to end** (details and gaps in formats/README.md, formats/tools_toolpaths_plan.md):
- **Open parts:** Mastercam 2017 `.mcam` and X9 `.mcx-9`: geometry (points, lines, arcs incl. arcs in other views),
  Parasolid solids (meshed for display), operations with all parameters, tools (TL_ID + the TlMgr archive with holders /
  assemblies), stored NCI. All 70 Mastercam-format files of the install parse to EOF.
- **Toolpaths:** every stored 2D op is regenerated from its record (`cam_cli PART fileop ID`) and new ops are made
  from Mastercam's default ops: Contour (2D / chamfer, multipasses, leads, tabs), Drill (cycles 0-2), Face, Slot,
  Helix Bore, Circle Mill, Thread Mill (mcmill ports), Pocket (mcmill pocket port in progress: High Speed and
  finish-only; Zigzag correlated), Dynamic Mill (mcce port), Area Clearance (decoded passes, safe links). Not generated
  (stored ops kept as made): Dynamic Contour, Peel, Area Core Roughing, the other pocket methods. Oracle: contour 141 ops
  p95 median 0.049 mm, drill exact, thread <= 0.082 mm, Dynamic median ~0.46 mm with the same material removed.
- **Post:** LinuxCNC post (`viewer/nc_post.c`) of stored and generated ops, the user's proven format (`G43 H0`
  deliberate); BRACKET4 matches Mastercam's program block for block.
- **Backplot** (classic NWDViewer: Options, colours, VCR, conditional stops, Details / Info), Reverse toolpath, Renumber
  tools, Edit Common Parameters, Tool list report, Recalculate feeds / speeds.
- **Tools:** `.tooldb` read / write (new libraries from Mastercam's own SQL scripts), Tool Manager (dialog 1099), Define
  Tool wizard, parametric tool / holder profiles, feeds / speeds from the material library.
- **Wireframe** (Mastercam's MCCAD / MCCADUTIL commands with function panels from the WPF views), Analyze, Chaining
  dialog with its modes, auto-highlight on hover + AutoCursor (the full decoded FUN_1804e1bb0 pass order 2026-10-19:
  tangent, feature points, intersections (the two closest entities), origin, Temporary Center Point (bbox centre),
  captured Temporary Midpoints (N key, opmanres 360 "Temporary Midpoi&nts"), perpendicular, grid, H-V, nearest, and
  consumer-side angular; Settings dialog opmanres 779 gating all but Fast Point / power keys - formats/ui.md
  "Selection and AutoCursor"; 2026-10-20: perpendicular on the anchor's own entity = the polar drag-distance snap
  (FUN_180131620 param_6, the "90 deg collapse" fix), and the dashed two-pass selection / hover highlight for
  non-solid line styles (draw_ent_nolist, config 4056; entity line styles now loaded from +0x18, and the base
  drawing draws them dashed too - Poly.ln_style + the baked screen-space dash buffer, core GL has no line stipple)), **Solids:**
  Extrude (2.5D slab kernel; no Parasolid writer) and the Solids manager. Sheet
  grouping decoded 2026-10-19 (FUN_180039370 / FUN_18003a650, decomp/extrude/b7.c): disjoint loops and even-depth
  islands are separate groups (Create = one body each), holes join their root; chn_get_normal's winding flip too.
- **Export:** "Save as Mastercam part" writes a new .mcam / .mcx-9 with new / edited ops, edited and new tools, or a
  new part from the defaults library. NOT yet opened in Mastercam (no Windows here) - UNVERIFIED.
- Lathe (2026-11-17, formats/lathe.md): Lathe Face only - stored Face ops regenerate (= Mastercam's NCI), a new Face from the
  stock (UI in progress, formats/TODO.md "Where we are"); other lathe ops decoded (Rough) or not started; no lathe post (P4).
- Not done: Verify (stock removal), 3D / multiaxis toolpaths, other lathe ops, wire / router, splines / surfaces, the Windows
  build has never been run.

**Build and run** (Linux; needs Qt 6 + CMake for the GUI, GLEW, OpenGL, sqlite3, gcc + g++; SDL2 for mcamview only):
```
cd viewer && make                       # cam_cli tool_cli mcamview xt2obj nc_dev + core (compile check of the core objects)
./cam_cli PART list | PART fileop ID --moves F | PART post OUT.nc     # formats/program_structure.md 4
python3 ../tools/setup_from_install.py Mastercam_2017.iso             # icons from your own copy (optional)
python3 ../tools/import_libraries.py Mastercam_2017.iso               # tool / defaults / material libraries -> <config dir>/shared
```
Qt UI (the only GUI since 2026-10-22; it replaced mcamui phase by phase, formats/qt_port_plan.md; phase 2 2026-10-21: the shared UI behaviour - document /
projects, selection, picking, AutoCursor, highlight stripes, undo, keymap - lives in viewer/core_ui/ and both UIs call it; phase 3: the wireframe commands + function panels (core_ui/cui_geo.c; Qt panels docked in ADS, AutoCursor point entry, mini toolbar, AutoCursor Settings 779); phase 4 chaining (cui_chain.c), phase 5 Extrude / Solids manager (cui_extrude.c); phase 6 the toolpath dialog 6544 + Tool page + Tool List 6410 + the Toolpaths manager menu (cui_ops.c / cui_opdlg.c / cui_toolpage.c / cui_optree.c; Qt viewer-qt/src/opdlg.*); phase 7 the Tool Manager 1099 + Tool List Filter 25519 + Define / Edit Tool wizard + Coolant + tool reports + Renumber tools 805 (cui_toolmgr.c / cui_toolwiz.c; Qt viewer-qt/src/toolmgr.*, toolwiz.*; tools/tests/tool_compare.sh); needs Qt 6 Widgets / OpenGLWidgets / Svg, GLEW, EGL,
CMake >= 3.21; SARibbon v2.9.5 + Qt-ADS v5.1.1 are fetched at configure time):
```
cmake -S viewer-qt -B viewer-qt/build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build viewer-qt/build -j4
viewer-qt/build/apprenticecam [PART.mcam|.mcx-9|.mcamui]               # asks the risk agreement at every start; config dir ~/.config/mcamui
env -u DISPLAY -u WAYLAND_DISPLAY QT_QPA_PLATFORM=offscreen MCAMUI_I_ACCEPT_THE_RISK=1 MCAMUI_SCRIPT="open:P;view:7" \
    viewer-qt/build/apprenticecam --shot out.png --size 1600x1000      # offscreen test (EGL surfaceless view)
```
Site folders (Mastercam's shared data, the user's CONFIG, Parasolid schema) come from `<config dir>/paths.conf`
(viewer/paths.conf.example); without them the defaults-based features find nothing. Windows: the Qt app's build is open
(qt_port_plan.md phase 9); `make windows WINDEPS=DIR` cross-builds cam_cli.exe + the core objects only.

**Docs:** `formats/README.md` = index of every spec, which source file each documents, the decompiles and the Ghidra
projects. `formats/program_structure.md` = binaries, data model, our own file formats, test hooks. `viewer/README.md` =
user guide. `tools/tests/README.md` = regression scripts and harnesses.

**Regression procedures** (run after every code change; parts are only read; memory cap as below):
```
( ulimit -v 4000000; cd viewer && make cam_cli )
bash ~/mastercam-re/regress/fileop_all.sh OUT && bash tools/tests/fileop_cmp.sh OUT      # expect 0/189 differ
bash tools/tests/postreg.sh OUT2                                                          # expect 0/16 differ
python3 tools/tests/safety.py OUT                                                         # no rapid below the stock top
bash tools/tests/tlmgr_rt_all.sh                                                          # export round trip, 188/188 identical
```
- fileop baseline: `~/mastercam-re/regress/base` (189 stored ops of the 16 user parts `<user-parts>/*.mcx-9`;
  older baselines `base_before_*`). An accepted change is copied into base with the reason in the status log.
- post baseline: `~/mastercam-re/regress/post_base` (16 programs; copied 2026-10-16 from the session scratchpad
  `postreg/base`, which is temporary). Diff ignores DATE lines.
- NAKTO op 3 (Core Roughing) is an accepted FAIL entry (not generated).
- UI regressions (since 2026-10-22, the Qt app against golden files): `bash tools/tests/<suite>_compare.sh OUT` for
  wf / chain / solid / op / tool / misc / stockplanes / planes / srf, `python3 tools/tests/scenes_compare.py OUT`,
  `python3 tools/tests/pf_roundtrip.py OUT`, `bash tools/tests/parity_check.sh OUT`; golden files in tools/tests/golden/
  (+ ~/mastercam-re/regress/golden for saved parts, tool-library dumps, screenshots); `BLESS=1` re-blesses an intended
  change. tools/tests/README.md has the details.

**Oracle** (our engine vs Mastercam's stored toolpaths): `tools/oracle.py PARTS [--op ID] [--type T] [--keep DIR]`
regenerates every stored op with cam_cli and reports both-way 3D deviation (p50 / p95 / max, sampled 0.1 mm, in part
units for inch parts). `tools/oracle_refs.sh SCRATCH` runs it (capped, ORACLE_FILEOP=1) on the user's parts plus
Mastercam's reference parts Mill.mcam (shared SST Samples, inch) and cad/mc-test01.mcam, copied to SCRATCH first
(<windows> is never written). Helpers: tools/tests/orfiles.py, dyn_cmp.py, dyn_batch_cmp.py.

## Layout
| Path | What |
|---|---|
| `~/mastercam-re/files/` | Flat extraction of `Data1.cab`+`Data11.cab` from `Mastercam_Installer.msi` (3723 files, 2.3 GB). Names are MSI file keys: long names truncated, some have `.GUID` suffixes. |
| `~/mastercam-re/ghidra/` (project `mc2017`) | mp.dll, mpbinx.dll, mcpost.dll, mastercam.exe |
| `~/mastercam-re/ghidra_core/` (project `mccore`) | mccore.dll |
| `pe_inventory.tsv` | name, arch, native/dotnet, exe/dll, size for all 907 PEs |
| `exports/*.tsv` | demangled<TAB>mangled export lists for key DLLs |
| `~/mastercam-re/ghidra_*/` | further Ghidra projects (bp, cad, gaf, gafgr, hmm, mdef, sld, tl; ribbon is empty) - list with their DLLs in formats/README.md |
| `~/mastercam-re/dotnet/` | ILSpy decompiles of the .NET assemblies (Tool Manager, tool wizard, mc*.ui panels) |
| `~/mastercam-re/regress/` | regression baselines (base, post_base, golden = the out-of-repo golden files of tools/tests) and scripts |
| `formats/` | format notes / schemas; index in `formats/README.md` |
| `decomp/` | Ghidra decompile texts cited by the specs and code (list in formats/README.md) |
| `viewer/` | apprenticeCAM C core + cam_cli, tool_cli, ...; `viewer/README.md` user guide (the Qt GUI: `viewer-qt/`) |
| `tools/` | scripts — see the Tools section below; `tools/tests/` regression scripts and harnesses |

Extraction lives on `/` because `<data-disk>` had only ~4.7 GB free.

## Binary landscape (x64 native unless noted)
- `mastercam.exe` (88 KB launcher) -> `mastercam.dll` (9.4 MB, 4871 exports), `mccore.dll` (20 MB, 12804 exports).
  Exports are MSVC-mangled C++ => class/method names + signatures for free.
- Geometry: `pskernel.dll` (Parasolid), `spaacis.dll` (ACIS), `mcgeomsld.dll`, `_axmsurf.dll`/`_axcore.dll`.
- Toolpaths: `millkernel.dll` (x86!), `mchmm.dll`, `engrave.dll`, `txtchain.dll`; verify/sim from ModuleWorks (`mw*.dll`, `machsim.dll`).
- Post: `mcpost.dll` (MP interpreter, contains postblock names), `mp.dll`/`mpbinx.dll` (thin, export only `m_main`/`m_version`),
  `postengine.dll` (.NET), `postprocessor.dll` (x86 native). `mp_plt.dat` is an unrelated Delphi plotter exe.
- .NET: 173 assemblies (`nethook3_0.dll`, `mccore.interop.dll`, `*.ui.dll`, ...). Need ILSpy/ilspycmd (not installed).
- Nothing examined so far is packed (normal section entropy).

## Formats
### Tool library `.tooldb` — SQLite 3
Fully self-describing; 51 `Tl*` tables. Schema in `formats/tooldb_schema.sql`. `_Header.version = 20`.

### Part file `.mcam` — see `formats/mcam.md`
Fully framed: all 7 bundled templates parse to EOF. Entity records, groups + material table blocks,
embedded Parasolid XT solids, binary NCI tail. Points/lines/arcs decoded; `tools/mcam_export.py` -> DXF + JSON + XT + thumbnail.
Operations (type/comment/tool/feeds/heights + all MP parameters by number), tools, toolpath geometry links, binary NCI,
curve/surface layouts (from code), size table for every class/subtype, CArchive object records (class/schema level).
All 70 Mastercam-format files in the install parse to EOF (parts + machine/control/component definitions); machine/control
records decoded at name level.
Remaining long tail: member layouts of CArchive classes (DeserializeV*), machine-component kinematics, names for most
parameter numbers (no offline source found — not in any of the 60 .chm help files), drafting (N_ID/W_ID/D_ID) semantics,
curve/surface decoders untested (no sample files contain them), fs2 formatter not validated against real Mastercam output.
Tool data inside ops uses `Cnc::Tool::*::DeserializeV1..V9` (CArchive).

### Posts
- `.pst` = plain-text MP language source. `postblockapi.py` = IronPython MPNet API sample.
- `.psb` = encrypted (`NSC^` magic) — not touching.
- MP interpreter = `mcpost.dll` — see `formats/mp.md`: all predefined operators/postblocks/variables/strings
  extracted (`formats/mp/*.tsv`, 100% coverage of generic posts), slot globals labelled in Ghidra, and the
  NCI-code -> postblock dispatch table. `mp.dll` ("MPXRun") is just the driver.
  Also: .pst structure (tools/pst_scan.py, 0 unknown lines over 16 posts), precedence + operator semantics,
  fs2 number formatting re-implemented (tools/mp_format.py).

## Dynamic / 2D High Speed toolpaths — see `formats/dynamic.md`
Engine = mcce.dll (CNC Software "cutting engine"), driven from mcmill.dll CHST2DProcess. Style byte at op entity 0x1228:
0 Dynamic Mill, 1 Area Mill, 2 Dynamic Contour, 3 Peel, 4 Blend. Stepover % -> constant engagement angle acos(1 - s/R).
Full mcce decompile: `decomp/mcce_all.c` (`tools/callgraph.py` for reachability).
Steps are accepted when mean radial width (swept area / step length) stays under s_work. After each linking move,
re-entry is ramped from s_10 to s_work over 10.666·R ("bUseRateOfEng"). 26 hidden registry knobs live under
`...\Mastercam 2017\Configurations\Variables\Toolpaths\Dynamic Mill` (table in dynamic.md).
Pocket-object offsets = CDynPockParm offsets + 8.

## 3D HST / OptiRough — see `formats/hst3d.md`, `formats/hst_plans.md`
mcmsurf (UI) -> mckernel (TP* classes generate macro "plan" programs) -> mchmm ("Mastercam HMM" engine with its own
macro language). OptiRough slices Z profiles and runs the 2D Dynamic engine (mcce MCCECalcDynPock) per level.

## UI — see `formats/ui.md`
787 UI commands indexed from *.ft (`formats/ui_commands.tsv`); toolpath dialog controls mapped to operation fields for
mcmill (`formats/ui_fields_mcmill.tsv`) and mcmsurf (`formats/ui_fields_mcmsurf.tsv`) by `tools/ui_map.py`.

## Toolpath / geometry module origins (from PDB paths)
- `mchmm.dll` — CNC Software "HmmSuite" (likely Dynamic/HST toolpaths)
- `_axmsurf.dll` — multiaxis (5axmsurf), ModuleWorks-linked (`MWGL`)
- `millkernel.dll`, `turnkernel.dll` — separate 32-bit "camkernel" codebase
- `mcmsurf.dll`, `tpfiveax.dll`, `tpiftools.dll`, `mckernel.dll` — Jenkins-built core CNC modules

## Ghidra workflow
```
P=<repo>
/opt/ghidra/support/analyzeHeadless ~/mastercam-re/ghidra_core mccore -process mccore.dll -noanalysis -readOnly \
  -scriptPath $P/tools/ghidra_scripts -postScript DumpDecomp.java $P/decomp/out.c '<regex>' <max> <calleeDepth>
```
Outputs so far: `decomp/mccore_{fileio,dbread,sections,bnci,bnci_io,entdebug,storeent}.c`, `decomp/mcgeomsld_read.c`.
`tools/pe_table.py FILE VA` dumps `{i32 id; char* name}` tables (e.g. entity class names at mccore 0x181307690).

## Tools
| tool | purpose |
|---|---|
| `mcam_walk.py` | frame any .mcam/.mmd/.control file into records, extract Parasolid (`--extract-xt`) |
| `mcam_export.py` | DXF + JSON (entities, ops with all parameters, tools, geometry links, machine names) + XT + thumbnail |
| `nci_decode.py` | binary NCI tail -> readable listing, `--check` validates arcs |
| `render_geom.py` | dependency-free plan-view render of exported geometry |
| `carchive_scan.py` | list MFC CArchive classes/schemas in serialized records |
| `pst_scan.py` | classify every line of an MP post |
| `mp_format.py` | fs2 number formatter re-implementation (+ self-test) |
| `mp_predef_extract.py` | MP predefined operators/postblocks/variables/strings from mcpost.dll |
| `mpprm_extract.py` | operation parameter id -> offset map from decompiled mcmpparameters.dll |
| `switch_strings.py` | resolve `case -> mov eax, <string id>` jump tables against a string-resource dump |
| `pe_inventory.py` `pe_sections.py` `pe_strings.py` `pe_table.py` `pe_xref.py` | PE helpers |
| `ghidra_scripts/DumpDecomp.java` `StringRefs.java` `ApplyLabels.java` | headless Ghidra: decompile by regex(+callees), string xrefs, apply labels |
| `ghidra_scripts/` (others) | `StringRefsW` (UTF-16 strings), `StringRefsDecomp`, `FindImm`, `FindConsts`, `FindDisp` (structure displacements), `DumpAt` / `DumpRange`, `DecompAt`, `CallersOf`, `RefsTo`, `VtableDump`, `VtSlot`, `SymList`, `SymWrites` |
| `oracle.py`, `oracle_refs.sh` | our engine vs Mastercam's stored toolpaths (see "Current state") |
| `import_libraries.py`, `msi_tables.py` | tool / operation-defaults / material libraries from the user's own copy -> <config dir>/shared; MSI table reader |
| `setup_from_install.py`, `extract_icons.py`, `debrand.py`, `ui_pack.py`, `gen_cmd_labels.py`, `gen_icon_map.py`, `gen_gallery_tips.py` | icon pack and gallery tooltips from the user's own install; shipped layout files with the product name filtered out |
| `baml_panel.py`, `baml_dump.py`, `ribbon_extract.py`, `ui_res_extract.py`, `pe_dialogs.py`, `pe_menus.py`, `dialog_labels.py` | UI resources: WPF BAML panels, ribbon XML, MFC dialogs / menus |
| `mcam_ops.py`, `mcam_post.py`, `gcode_compare.py`, `gcode_deviation.py`, `dyn_*.py` | part ops / NCI access, Python post, G-code comparison, Dynamic validation |
| `tests/` | regression scripts and C / Python harnesses (tools/tests/README.md) |

## Posting without Mastercam — see `formats/post.md`
**LinuxCNC post** `viewer/nc_post.c` (mcamui Post / `cam_cli PART post`): all ops, stored + edited, in the user's proven
LinuxCNC format (G43 H0 deliberate). Units from NCI 1016 +0x44 (met_tool): G20/G21, inch = 4 decimals.
`tools/mcam_post.py` turns the NCI stored in a part file into G-code in the user's post format. Validated against
Mastercam's own output: BRACKET4 matches block for block (100 %, every feed and arc word), and the older post style
matches 97–99.97 %. Ops follow the Operations Manager order at op entity +0xe6.

## Oracle: our engine vs Mastercam's own toolpaths — `tools/oracle.py`
Re-generates every stored op of a part with `viewer/cam_cli` (op settings + chains with direction + part tool) and
measures both-way 3D deviation against the op's NCI. Run capped: `( ulimit -v 4000000; python3 tools/oracle.py PARTS... )`.
Status 2026-10-14 (17 user parts, 190 ops, all generate): contour 138/138 p95 ≤ 0.2 mm (133 ≤ 0.1, median 0.049,
worst 0.167: tabs Mastercam skips on collapsed arcs); drill 5/5 exact; thread mill 4/4 ≤ 0.08 mm; pocket 0.14 mm; face
2/2 ≤ 0.05 mm (incl. style 4 dynamic); Dynamic / Area 40 ops incl. the solid-chain op 31: same material as Mastercam
(missed ≤ 0.5 %), paths differ by design. Later rules: solid chains = top-face outer loop, roll cutter sharp (≥ 45°),
tab position = entity + t (+0xc8), exact-normal / mitre entry, globally trimmed open offsets (formats/mcam.md). Decoded on the way: TP_BND +0xa8 direction,
TP kinds (6/7/1/27/30), chain role +0xe0, contour type 2 = chamfer, tabs (kind 30, w + D span, 45° ramps), lead
fitting (line shortened, then arc shrunk, else dropped), midpoint entry, keep-down between depths, retract/clearance
planes, thread mill motion, drill records 81/100, facing/pocket rules, floor stock +0x888, Dynamic containment.

## apprenticeCAM: what ships, what the user supplies (2026-10-15)
Ships in the repo: the menu / ribbon layout and its texts, viewer/ui/{layout.txt, cmd_labels.tsv, icon_map.tsv},
with the product name filtered out by tools/debrand.py (texts naming the program -> apprenticeCAM; vendor-service
entries such as the web site / registration / updates dropped). The app reads `--ui DIR`, else <exe dir>/ui, else ./ui.
No user-visible text says Mastercam (file types read "MCAM part (*.mcam / *.mcx-9)"); code comments still cite the
Mastercam DLLs as provenance (HARD RULE).

Not shipped - icons and the gallery tooltips' description texts. Build them from your own copy (DVD .iso, its
Mastercam_Installer.msi, or the program folder):

    python3 tools/setup_from_install.py Mastercam_2017.iso      # -> ~/.config/mcamui/icons.pack + gallery_tips.tsv   (~8 s, needs 7z + Pillow)
    python3 tools/setup_from_install.py SRC --layout viewer/ui   # developers: also regenerate the layout files

Without the pack every icon is a letter tile; without gallery_tips.tsv the toolpath palette's tooltips are the item
names only.

Starter tools (2026-10-21, ours): viewer/ui/starter_tools_mm.tools ships 46 standard metric tools (spot drills, DIN 338
jobber drills incl. tap drills, DIN 6527 K flat end mills 2-20, ball 3/6/10, chamfer 6/10, M3-M12 taps). Geometry only:
the "BASE defaults" line makes toollib.c load_text start each tool from its type's DatabaseDefaults tool (tool_new), so
speeds / feeds / holder are Mastercam's per-type defaults (generic, not per diameter). The Tool List 6410 and the Tool
Manager open it when no Mastercam tool library is reachable.

Libraries (2026-10-21): `python3 tools/import_libraries.py SOURCE [OUT_DIR]` (SOURCE = .iso, its .msi, the program
folder, or a shared folder such as <windows>/Users/Public/Documents/shared Mcam2017) copies mill / router
Tools/*.tooldb, mill / router Ops/*.mcam-defaults, common/ToolData/DatabaseDefaults + DatabaseCatalog.tooldb and
MATERIALS/*.mcam-materials to <config dir>/shared in the <APP_DIR_SHARED> layout and lists them (40 files from the DVD,
~4 s). In the installer they are the files under mcam/common/SharedDefaults (tools/msi_tables.py reads the MSI File /
Component / Directory tables to map the cabinet keys to names). viewer/paths.h host_path / site_file: "<APP_DIR_SHARED>"
is paths.conf shared first; unset or the file missing there -> <config dir>/shared. The user's CONFIG (config 2303 /
25000 ...) still comes from paths.conf `user`: no default .config is shipped in the installer (Mastercam's built-in
config defaults, mastercam.dll string table near 0x86ab60, are NOT DECODED). The DVD has four Mastercam_Installer.msi (data/art, catia, mastercam,
wirepower); the program is data/mastercam; Data1.cab continues in Data11.cab (spanned).

Site folders (no personal paths in the source): <config dir>/paths.conf (viewer/paths.h, example in
viewer/paths.conf.example; MCAMUI_<KEY> env overrides): shared (<APP_DIR_SHARED>: Ops defaults, ToolData, Tools),
user (CONFIG/: Workspace, keymap, default.mtb, *.config), schema (Parasolid schema folders), cad, posts. Unset = the
feature finds nothing (no right-click menu / QAT / keymap without "user", no default tools without "shared").

## Linux and Windows (2026-10-15)
viewer/platform.h + paths.h carry every filesystem rule; tools/apppaths.py mirrors them for the Python tools.
- Linux is native and first class: '/' is the only separator, a name may hold '\\' or ':', a Linux path is used
  exactly as written. Windows: '/' and '\\' both separate; drives, UNC paths.
- Config folder: Linux $XDG_CONFIG_HOME/mcamui or ~/.config/mcamui; Windows %APPDATA%\\apprenticeCAM
  (MCAMUI_CONFIG_DIR overrides). Layout files: <exe dir>/ui, else ./ui. Icon pack, paths.conf, recent.txt in config.
- Windows paths written by Mastercam (CONFIG files, .rpx report folders, tool libraries, <APP_DIR_SHARED>) go through
  host_path(): on Linux "X:" -> paths.conf drive_x, '\\' -> '/', names matched case-insensitively (NTFS).
- Lists in paths.conf: ';' (Linux also ':' - a "C:" drive is never split).
- Build: the Qt app with CMake (Linux). `make windows WINDEPS=DIR` cross-compiles win/cam_cli.exe + the core objects
  with MinGW-w64 (DIR = glew-2.2.0 source, sqlite amalgamation). Until 2026-10-22 it also built win/mcamui.exe (built
  and linked 2026-10-15, never run); the Qt app's Windows build is open (qt_port_plan.md phase 9).
- Setup on Windows: `py tools\\setup_from_install.py D:\\Mastercam_2017.iso` (needs 7-Zip, Pillow) -> %APPDATA%\\apprenticeCAM.
