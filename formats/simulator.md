# Mastercam Simulator (Verify) - launch, window, player, settings; apprenticeCAM's copy

Status: **partial**. The launch, the window's layout and the player model are decoded and ported. The window runs
apprenticeCAM's own stock-removal engine (`viewer/verify.c`, **ours**). ModuleWorks is not ported. Every control the
engine cannot back is greyed (section 7).

User decision: Q33 in formats/user_decisions.md.
Code: `viewer-qt/src/simwin.{h,cpp}` (the window), `viewer/core_ui/cui_verify.c` (player, settings, Move List),
`viewer/verify.c` (the engine, ours), `tools/dotnet_resources.py` (icons).

## 1. What the Simulator is (format of its UI)

Mastercam 2017's Verify window is the **Mastercam Simulator**, internally **UBVS**. It is made of four parts:

- **The window.** `ubvs.dll` is a .NET 4 WPF assembly. It uses the **Actipro** controls: Ribbon, Docking and PropertyGrid
  (`actiprosoftware.*.wpf.dll`). Its layout is compiled XAML (BAML) inside `UBVS.g.resources`.
- **The labels.** They are in `ubvsresources.dll`, a .NET satellite: `UbvsResources.MainWindowXaml.resx`,
  `OptionsXaml.resx`, `Resources.resx`, and others.
- **The player.** `mwsimstreammanager.dll` is a C++/CLI ModuleWorks layer. It holds `MachineSimulationPlayer` and
  `SimulationSettings`. `mwubvsproxy.dll` is the C++/CLI bridge to the native side.
- **The engine.** The native ModuleWorks DLLs (`mwverifier`, `mwcncsim`, `mwmsim*`, ...) do the cutting. They are not
  ported.

It is **not** an MFC ribbon. `machsim_res.dll`, `mwmachsim_res.dll`, `mwmsimdefgui.dll` and `mwmachsimcustom_icons.dll`
belong to the old MFC/BCG MachSim GUI, the "Machine Simulation" product. Mastercam's Verify does not use them; see 2.

Extraction:
- **C# code.** `ilspycmd -p` → `~/mastercam-re/dotnet/{ubvs,ubvsresources,mwubvsproxy,mwsimstreammanager}`. It needs
  `DOTNET_ROOT=~/.dotnet` and `DOTNET_GCHeapHardLimit=0x80000000`. Do not use `ulimit -v`: CoreCLR fails under it.
- **BAML.** `tools/baml_dump.py`. The labels are resolved from the .resx files.
- **Icons.** `tools/dotnet_resources.py` reads `resources/icons/*.png|ico` (188 files) from the assembly in pure Python.
  `tools/extract_icons.py` packs them as `ubvs_<name>.png` into the user's local `icons.pack`; they are never committed.
  `setup_from_install.py` now also pulls `ubvs.dll` out of the installer.

Help: `machsim.chm` is the MachSim product's help (not read further). The Simulator's help page is opened by
`HelpCommand` (`HelpPage.BackplotAndVerify`) and is NOT DECODED.

## 2. The launch (mcverify.dll / MachSim.DLL / mwUbvsProxy)

Decoded 2026-10-24 by Ghidra on `mcverify.dll` (project `ghidra_bp`) and `MachSim.DLL` (a scratch import). The dumps are in
`~/mastercam-re/work/sim_decomp/` (Ghidra project of MachSim.DLL: `~/mastercam-re/ghidra_machsim`; the resolved XAML dumps: `sim_decomp/xaml/*.res.xaml`): exports.c, launch2.c, sess.c, mc_verify.c, ms1-5.c, mcverifyres_strings.tsv.

**The path from the button to the window:**
1. Toolpaths manager "Verify selected operations" → `OMverifySelectedOperations` (mastercam.dll @18034fba0).
2. → `COpMgrTree::VerifySelected` @180368c80.
3. → `UseUBVSPrototype(1)` (mcverify @18003d870). It returns **true without reading the registry**. "Use Classic
   Backplot" only steers Backplot (`UseUBVSPrototype(0)`, formats/backplot.md). Verify **always** opens the Simulator,
   and no classic Verify is left.
4. → `startUpUBVS` @1800388e0, then `runVerifyDlg(info, 0, 0, 1, 0)` @18000d110.

**Modes in `UBVSInfo`** (+0 = mode, +4 = byte):

| Path | mode | +4 |
|---|---|---|
| Verify | 1 | 0 |
| Backplot (Simulator path) | 1 | 1 |
| Machine Simulation (`startUpUBVSForMachSim`) | 5 or 6 | - |
| file dumps | 2 / 3 / 7 | - |

The launcher `FUN_18003b1e0`:
- loads `chooks\MachSim.DLL`, which loads `mwUbvsProxy.dll`;
- calls `m_startup` and `m_getListenerPtr`;
- fills the info block (+4 = the current MCX file name);
- calls the listener's start. That builds `McamStartupInfo` (MachSim `FUN_1801f1020`).

**What is passed** (fields of `McamStartupInfo` / `mwMachsimObjectBVListenerStartupParameters`):

| What | Source |
|---|---|
| ops | the selected ops of the main op list (`FUN_180030940`: op+0x95 set, type ≠ 0x8b, not ghosted unless `verify_include_ghosted_ops`) |
| stock | the active machine group's Job Setup (`GetStockSetupValuesInForUBVS`, `FUN_1800260a0`): shape (group +0x1264), cylinder axis / diameter / length / centred (+0x126c / +0x1230 / +0x1238 / +0x1270), box from `stock_calc_geom`, STL name +0x1271 |
| session resolution | 0.03 in / 0.75 mm (`FUN_180022ac0`) |
| fixtures | `FindDatabaseFixtureEntities` |
| tools / holders | per-op encoding NOT DECODED (only the MachSim log strings "SetHolderFromProfile ...") |
| units | `2 - IsMetric()` |
| settings file | `GetDir(AppDir 4)` + `"MastercamSimulatorDefaults.xml"` = `<My Documents>\my mcam2017\MastercamSimulatorDefaults.xml`, `CreateXmlFile` = true (see 5) |
| key bindings | Mastercam's keymap (`GetKeymapFromFunction`); the fallback file format is NOT DECODED |
| registered user / company / serial / type / maintenance / version / build / copyright | `GetMastercamHelpAboutInfo` (shown on the Help backstage) |
| `MotionControllerRotPoint` | `SbGetRotationPoint()` |

**Messages:**

| Condition | Text |
|---|---|
| no op selected | 3964 "No operations were selected.  Please select at least one operation." (already used by ours) |
| selected ops not in the active machine group | mccore 33010 + checkbox 33011 |
| stock extents fail | mcverifyres 163 "Unable to estimate stock extents. Please use Stock Setup." |
| progress title | 182 "Processing NCI data..." |
| an already-running Backplot / Verify | silent no-op |

**Return.** The NCI is streamed synchronously under the progress dialog, then EndSession, then `m_shutdown`. Mastercam
takes control back while the WPF window **stays open (not modal)**. Session values are copied back into the `verify_*`
globals (`FUN_180031f10`). "Save Toolpath / Tool as geometry" goes through `SaveDataFromUBVS`.

**Ours.** The launch goes through `vf_start` (cui_verify.c). It keeps the decoded no-selection message. It builds our
motions (`nc_motions`) and the stock box (`vf_stock_box`, mccore `job_setup_stock_corners`), then opens the window.
- Not ported: the machine-group warning 33010, the progress dialog, fixtures, tool holders, STL / solid stock.
- The window is a separate, non-modal top-level window, as in Mastercam.

## 3. The window (ubvs.dll mainwindow.baml + MainWindowXaml.resx)

| Item | Source |
|---|---|
| title | "Mastercam Simulator" (debranded: "apprenticeCAM Simulator") |
| size | 1290 x 768 at (200, 200) (`UbvsPersistant` defaults) |
| layout | `DockPanel` = Ribbon `ubvsRibbon` + `DockSite` (Workspace + a right `SplitContainer` of tool windows) |

### 3.1 Ribbon

The tables list every control in order. Each row is `Id` "Label" (icon) and its binding. Contextual tabs are shown for
the current mode.

**Backstage (File):**
- `SaveStockAsStlCommand` "Save Stock as STL";
- the "Help" tab: Support (Help, Export Support Data), License Information, About;
- `PrintCommand` "Print";
- `ShowOptionsCommand` "Options";
- `CloseCommand` "Exit".

**Home:**

| Group | Controls |
|---|---|
| **Mode** | `Backplot` (Backplot32, `IsBackplotEnabled`, `SwitchToBackplotCommand`), `Verify` (Verify32, `IsVerificationEnabled`), `MachineSimulation` "Simulation" (`Visibility = IsCNCSimVisible`) |
| **Playback** | `ColorLoop` "Color Loop"; split button `StopConditions` "Stop Conditions" (`IsStopConditionsEnabled`) with menu: Operation Change, Tool Change, Collision, Tool Inspection, sep "XYZ Change", X / Y / Z Change (`CheckChange?Delta`), sep "Specific Values", "Specify Values"; popups "Collision Checking" and "Material Cutting" (Milling: Holder / Shank / Shoulder / Cutting Length; Turning: Holder / Insert) |
| **Visibility** | popup "Tool Components" (same items), checkboxes Toolpath (IsEnabled `IsStockEnabled`), Tool, Workpiece (IsEnabled `IsWorkpieceSet`), Stock, Initial Stock, Fixtures, Wireframe, Gnomon, Axes |
| **Focus** | Tool / Workpiece / Multiple Workpiece (`ComponentFocus` CF_TOOL / CF_STOCK / CF_NONE; keys T / W / M) |
| **Defaults** | "Read from Defaults" (Open dialog → Deserialize), "Save to Defaults" (`WriteSimulationSettingsXmlCommand`), "Reset to Defaults" (`SimulationSettingsProvider.ToDefaults`) |
| **Video** | "Record", "Recording Options" (Quality High / Medium / Low, Full Window, Audio) |

**View:**

| Group | Controls |
|---|---|
| **Show** | "Collision Report", "Move List", "Axis Control" (CNC sim only) |
| **3D View** | Fit, Isometric (Alt+7), Top (Alt+1), Right (Alt+5), Front split (Alt+2) with Bottom (Alt+4) / Left (Alt+6) / Back (Alt+3) |
| **Viewports** | Single View, 2 Views in Rows, 2 Views in Columns, 4 Views, Restore default view |
| **Utilities** | Motion Controller, Sync views |

**Debug Tools:** `Visibility = DebugUiVisibility` (hidden).

**Contextual tab groups:**
- **Simulation**: Operations, Toolpath, Display, Vectors, Clipping, Quality.
- **Backplot**: Operations, Toolpath, Display (+ Quick Verify, Interpolate Mode), Vectors, Geometry (Save Tool / Save Toolpath).
- **Verify**, in order:

| Group | Controls |
|---|---|
| Operations | All Operations / Current Operation / Segment |
| Toolpath | Trace / Follow / Both |
| Display | Restrict Drawing, Stop Restrict Drawing, Points, Leads |
| Vectors | Enable Vectors |
| Analyze | Compare, Keep Chips, Remove Chips, Points, Distance, Clear All |
| Clipping | 3/4 (quadrants / Off), XY / YZ / ZX Clipping Plane (Clip Top / Bottom ...) |
| Quality | Accurate Zoom, Reset Zoom, Show Edges, Turbo Mode, True Thread |

**QAT:** `CustomSettings.QuickAccessToolbarItems` = Backplot, Verify, Simulation.

### 3.2 Workspace

The workspace is a grid with three rows (`*`, `22`, `Auto`):

1. **Viewport.** The `ViewportContainer` (Mode = `ViewportMode`) on a white grid.
2. **Progress row**, background #F7F7F7 with a #CCC top border:
   - `GrayOutColorProgressBar` (`SimulationProgressValue`, colour intervals);
   - the position slider: Minimum 1, Maximum `OverallToolpathLength`, Value `SliderValueOneBased`, tick frequency
     length * 0.1, IsEnabled `CanDragSlider`.
3. **Playback bar.** Gradient #FFFFFF / #F1F1F1 0.5 / #E1E1E1 0.5 / #F3F3F3, columns 1* 2* 1*:
   - **Left:** "Precision: " + `LazySlider` (width 100, ticks 1, `PrecisionIndex`, IsEnabled `IsPrecisionSliderEnabled`).
   - **Centre:** GlassButtons, Background #FF1D5BBA, 32 x 32, Play 48 x 48 (images below).
   - **Right:** "Speed: " + slider (width 100, TickFrequency 2, `PerformanceIndex`, Min / Max `Min/MaxPerformanceIndex`).

| Button | Command | Image | Tip |
|---|---|---|---|
| Start | `ResetCommand` | UBVSStart16 | "Start" |
| Previous Operation | `PrevOperationCommand` | UBVSFirst16 | "Previous Operation (P)" |
| Step Backward | `PrevStepCommand` | UBVSPrevious16 | "Step Backward (B)" |
| Play | `PlayStopCommand` | UBVSPlay16 / UBVSStop16 while running | "Play (R)", "Click to play or pause." |
| Step Forward | `NextStepCommand` | UBVSNext16 | "Step Forward (S)", "Step forward to next entity. Press and hold (S) to step quickly." |
| Next Operation | `NextOperationCommand` | UBVSLast16 | "Next Operation (N)" |
| End | `EndCommand` | UBVSEnd16 | "End" |
| Loop | toggle `LoopSimulation` | UBVSLoop16 | "Loop" |

### 3.3 Tool windows

The tool windows sit in one `StylingToolWindowContainer`, right of the workspace:

- **"Move List"** (`twMoveInfo`), a PropertyGrid. Rows and formats (VM `UpdateMoveInfoPropertyGrid`):

| Row | Format |
|---|---|
| Move ID | "{0} of {1}" |
| Move Type | Rapid / Feed Linear / Arc / Thread Move (`TypeToText.MoveTypeToText`) |
| Operation Name | text |
| Operation Number | "ID: {0} ({1} of {2})" |
| Tool Number | "#{0} - {1}" |
| Tool Orientation, Tool Tip Position | "{0:N3}; {1:N3}; {2:N3}" |
| Feed Length | F3 |
| Feed Time | `TimeToText` "1h 2min 3.45s" |
| Min/Max X/Y/Z | "lo / hi" F3 |
| Rapid Length, Rapid Time, Total Length, Total Time | as above |
| Coolant | On / Off |
| Compensation Type / Direction | text |
| Feed Rate | F3 |
| NC Code | "G" + gCode |
| Spindle Speed | F3 |

  A second debug grid (`DebugUiVisibility`) is hidden.
- **"Collision Report"** (`twCollisions`): columns Move / Collision.
- **"Compare"** (`twStockCompare`): `IsOpen` False; it opens with Verify's Compare.
- **"Axis Control"**: CNC sim only.

### 3.4 Keys

`MainWindow.SetContext` binds these keys:

| Key | Command |
|---|---|
| S / B / R / N / P | Mastercam's keymap (`McamUbvsKeyBindings` m_stepForward / m_stepBackwards / m_runAndPause / m_nextStop / m_previousStop), as the tips say |
| Home (Key 22) | Reset |
| Space (Key 18) | End |
| Numpad + / - (Keys 85 / 87) | speed up / down |
| F1 | Help |
| F10 / F11 / F12 | Backplot / Verify / Simulation |
| T / W / M | focus |
| Alt+1..7 | views |

## 4. The player (mwsimstreammanager MachineSimulationPlayer, ubvs Regulator) - ported in cui_verify.c

**Commands:**

| Command | Job (`SimulationParameters.JobType`) / action |
|---|---|
| Play | `RUN_PLAY` |
| Stop | `RequestStop`, i.e. Play again while running (`ApplyPlayStop`) |
| Next Operation | `RUN_SINGLE_OPERATION` |
| End | `RUN_FASTFORWARD`; `Regulate(fastForward)` does no pacing |
| Step Forward | `RUN_SINGLE_STEP` (`NextMove`) |
| Step Backward | moves iterator `MoveBack` + `JumpTo(.., simulateForwardMotion false)` |
| Previous Operation | `StepContextOperationIterator.MoveBack`: inside an op → its first move; at its first move → the first move of the previous op that has moves |
| Start | `ResetThisViewModel` |

**The run loop** (`rst_StandardPlay`). After each `DoNextStep` it compares the previous step context with the new one:
- operation change = `pOperationIter` differs, or there was no previous context;
- tool change = `ToolId` differs.

It stops when any of these holds:
- mode `RUN_SINGLE_STEP`;
- mode `RUN_SINGLE_OPERATION` and the operation changed;
- the `GoToPosition` was reached;
- a stop condition fired. Stop conditions apply only when `GetStopConditionsEnabled()`:
  - OperationChange;
  - ToolChange;
  - Collision (ModuleWorks);
  - ToolInspection;
  - StepMatch / OpNo / ToolNo / X / Y / Z value matches (`CheckChange?Value`, tolerance 0.001);
  - X / Y / Z Change (`CheckChange?Delta`: `|new - old| > 0.001`).

At the end of the toolpath: when `LoopSimulation` is on and (no step yet, or `RUN_PLAY`), the run resets and goes on; a
single-operation job stops after that reset.

**Speed** (`UBVS.Regulator`; base ModuleWorks `Regulator(minSpeed 0, maxSpeed 20, initialSpeed 10)`):
- `k = min(TotalMoveCount * Multiplicator(1) * 2 / 3000, 1)`;
- moves/s = `5k + (3000k - 5k) / 400 * speed^2`;
- frames/s = `20 - 0.75 * speed`;
- `PerformanceIndex` = `CustomSettings.PerformanceIndexVerification` (default 10) in Verify;
- Numpad + / - step it by 1 within [0, 20].

**Ours:**
- one step = one of our motions; tool-change markers (NCM_TOOL) count as steps too;
- whether the first step after Start counts as an operation change depends on the reset context, which is NOT DECODED;
  ours says yes;
- pacing uses our wall-clock budget per 20 ms timer tick. The moves/s and frames/s are the decoded ones; the
  Regulate / Flush / Render scheduling is not copied;
- not ported: the Collision, Tool Inspection and Specify Values stops (they need ModuleWorks collision checking or the
  XYZValue window).

## 5. Settings (SimulationSettings XML)

UBVSLauncher builds the settings in this order:
1. `Instance = Helper.CreateDefaults(units)`, i.e. `SimulationSettings.SetDefaults` + the `UbvsCustomSettings` field
   initialisers + `UpdateUnits`.
2. If `PathToXmlSettings` is missing, `TryCreateDefaultsFileIfRequired` writes it (`CreateXmlFile` = true).
3. `Deserialize(file)` over the defaults.
4. The result is kept in `settingsGlobal` for the rest of the Mastercam session: it is read only at the first launch.

The shipped `mastercamsimulatordefaults.x` (install root, May 2016) holds the same values as `CreateDefaults` for every
key used here. The user's own `Documents/my mcam2017/MastercamSimulatorDefaults.xml` (on <windows>) is that same file:
same size and date (8843 bytes, 27 May 2016), byte-identical after CR stripping. So the installer places the `.x` as the
per-user file [inferred; the installer step is not traced]. Mastercam X9 has the template at
`common/UserDefaults/MastercamSimulatorDefaults.xml`. `mwmachsim.ini` belongs to the MachSim
product (layout file names).

**Used here** (default → our use):

| Key | Default | Our use |
|---|---|---|
| StopConditions Enabled / OperationChange / ToolChange / Collision / ToolInspection / CheckChange?Delta | false | stop conditions (4) |
| CustomSettings PerformanceIndexVerification | 10 | speed |
| RenderingSettings Tool | CV_SHOW | tool drawn |
| StockVis | CV_SHOW | stock mesh |
| ToolpathVis | CV_HIDE | `show_tp` |
| ShowWireframeGeometries | True | `show_geom` |
| ShowCoordinateSystem | True | `show_axes` |
| InitialStockVis / WorkpieceVis | CV_HIDE | the boxes stay greyed |
| FixtureVis / ShowGnomon | CV_SHOW / True | greyed: no fixtures, gnomon not separate |
| StockColor | #F66D6D | the uncut stock's colour (section 9) |
| MillingFluteColor | #DDBB00 | cutter colour (the tool only, not the cut stock) |
| CustomSettings IsColorLoop / drawMode / Scheme | false / Operation / Yellow, Blue, ... (12) | the cut colours (section 9) |
| MillingHolderColor | #336699 | read; holder not drawn |
| VerifySettings PrecisionIndex | 5 | shown on the greyed Precision slider |
| BackplotSettings showOperation / showToolpathSegment / showRapidMoves / showPoints | AllOperations / Follow / True / False | read; the Verify tab's buttons are greyed |
| CustomSettings Viewports | SingleView | Single View checked |

`LoopSimulation` is the player's field (default false), not a setting.

**Ours:**
- Path: `MCAMUI_SIM_DEFAULTS` (tests), else `<config dir>/MastercamSimulatorDefaults.xml`. Our config dir stands in
  for `my mcam2017`. Without the file the built-in values apply.
- The file is never written: Save to Defaults is greyed.
- Reset to Defaults is greyed: ModuleWorks `ToDefaults` is not in the managed code.

## 6. apprenticeCAM's window (viewer-qt/src/simwin.cpp) - live vs greyed

Live (wired to cui_verify.c):
- **Color Loop** (section 9).
- **Playback:** all 8 bar buttons, the position slider, the Speed slider (0..20), keys S / B / R / N / P / Home / Space /
  Numpad + and -.
- **Stop Conditions:** the toggle, Operation Change, Tool Change, X / Y / Z Change.
- **Visibility:** Toolpath, Tool, Stock, Wireframe, Axes.
- **View tab:** Fit, Isometric, Top, Right, Front, Bottom, Left, Back. These run our `view_std` / `view_fit` on the
  camera shared with the main window (ours; Mastercam syncs the view through `UpdateViewFromMcam`).
- **Mode:** Verify (checked).
- **Other:** Move List (show the dock), Read from Defaults, File > Exit, the window's close box (ends the session).
- **The Move List rows** (3.3) that our motions carry: Move ID, Move Type, Operation Name (ours: comment, else type),
  Operation Number, Tool Number, Tool Tip Position, Feed Length, Feed Time, Min/Max X/Y/Z, Rapid Length, Total Length,
  Feed Rate, NC Code (G0-G3), Spindle Speed.
- **Blank rows:** Tool Orientation, Rapid / Total Time (the rapid rate is NOT DECODED), Coolant, Compensation.

Greyed, with tips saying "not supported by apprenticeCAM's Verify engine (ours)" or "NOT DECODED":
- **Backstage:** Save Stock as STL, Help, Print, Options.
- **Home:** Backplot mode (apprenticeCAM's Backplot is the classic one), Collision / Tool Inspection stops,
  Specify Values, Collision Checking, Material Cutting, Tool Components, Workpiece / Initial Stock / Fixtures / Gnomon,
  Focus x3, Save / Reset to Defaults, Record, Recording Options.
- **View:** Collision Report, the 2 / 4 viewport modes, Restore default view, Motion Controller, Sync views.
- **Verify tab:** everything except Analyze > Compare (section 8): Operations, Toolpath, Display, Vectors, the other Analyze
  buttons, Clipping, Quality.
- **Other:** the Precision slider, the Collision Report list.
- **Hidden as in Mastercam:** MachineSimulation (`IsCNCSimVisible` false), Axis Control, Debug Tools.

**Ours** (beyond the engine):
- The viewport draws the main window's scene off-screen at its own size (`renderSceneImage` in viewwidget.cpp). Camera,
  toolpath / geometry toggles and the verify mesh are therefore shared with the main window; the toggles are restored at
  close.
- The status line under the bar (removed volume, rapid-collision warnings).
- A menu instead of the Backstage page.
- The Qt look (SARibbon Office 2013 theme, QDockWidget tabs).
- The dock width of 300 px.
- The viewport background: our gradient. MW's GL background is NOT DECODED; the WPF grid behind it is #FFF.

Test steps (testhooks.cpp, cui_verify.c):
- `simdump`;
- `simset:ID=V` (a control by its UBVS Id; V empty = press; also `speed=N`, `slider=N`, a VCR tip);
- `simshot:PNG`;
- `vfkey:K`;
- `vfwait`: runs the job to its stop, unpaced;
- `vfmoves`;
- `vfset:NAME=V`.

Scenes: tools/tests/misc_scenes.txt `sim_window`, `sim_runmodes`, `sim_stops`, `sim_visibility`.

## 7. NOT DECODED / not done
- The ModuleWorks engine: cutting, collision, the true 3D Compare distance (ours: vertical, section 8), precision, chips, clipping, Accurate Zoom, Turbo, True
  Thread, edges. Not to be ported (user brief); ours stands in for cutting.
- Per-op tool / holder encoding passed by MachSim; fixtures; STL / solid / cylinder stock in our engine (TODO.md
  "Verify improvements").
- The reset step context (4), the rapid rate (Rapid / Total Time), the Options window (options.baml: decodable, not
  ported), the Help page, the Backplot and Simulation modes of the Simulator, the `mastercamsimulatordefaults.x` reader,
  the keymap fallback file.

## 8. Compare (ubvs.dll twStockCompare; ported 2026-10-28)

**Decoded.**
- **Tool window** (mainwindow.baml `twStockCompare`, `IsOpen` False, Background #F0F0F0). Grid rows Auto / Auto / * / Auto:
  - Row 0 (`TopRowButtons`):
    - image StockToLeave16 (tip "Stock to leave") + a DoubleEditBox bound to `StockToLeave`;
    - Refresh (`RefreshCommand`, Refresh16);
    - Show Stock (`ToggleStockVisibilityCommand`, ShowStock16, checked = StockVisibility);
    - Show Model (`ToggleStlCompareVisibilityCommand`, ShowModel16);
    - Compare Options (`ShowStockCompareOptionsCommand`, Options16; label `StockCompareDynamicRangesCommandButton_Label`).
  - Row 1: the target combo (`StockCompareComboBoxSource`, DisplayMemberPath Content).
  - Row 2: the colour grid (`StockCompareColors`): an unlabelled colour column, Start, End; single full-row selection.
  - Row 3: an empty button row.
- **Opening it:** Verify tab > Analyze > Compare (`ShowStockCompareCommand`: ShowStockCompare toggles the tool window).
- **The combo** (MainWindowViewModel):
  - At setup it holds "Browse..." (`CreateBrowse`). "Workpiece" is inserted at 0 and selected when MachSim sent a workpiece
    (`OnSetupCompleted`, `e.WorkpieceSet`).
  - Browse... opens an OpenFileDialog (`Resources.StockCompareBrowseTitle` "Open STL model for the stock comparison", filter
    "STL files(.stl)|*.stl|All Files (*.*)|*.*"). The file is added once, at the end, and selected (`SetStockCompareStl`).
  - Selecting an item sets the target (`OnUpdateStockCompareComboBoxSelectedItem`): the workpiece mesh, or
    `ManagedFactory.CreateMesh(units, file)` moved by the inverse step context. Then NeedRefresh = true.
- **Refresh** (`ApplyRefresh`):
  - Enabled when verification is on, NeedRefresh is set and the item is not Browse.
  - Sets `DeviationOffset` = StockToLeave, updates the colour scheme, and clears NeedRefresh.
  - Editing StockToLeave sets NeedRefresh; so does any band change (`NeedRefreshRequired`).
- **Bands** (`UbvsCustomSettings.StockCompareColors`; the shipped XML holds the same; values in `ToleranceUnit`, Imperial,
  converted by `UpdateUnits`):

  | EndValue | Colour |
  |---|---|
  | -0.04 | DarkRed |
  | -0.02 | Red |
  | -0.01 | Yellow |
  | 0.01 | Green |
  | 0.02 | LightBlue |
  | 0.04 | Blue |
  | INF | DarkBlue |

  - Item k covers (EndValue[k-1], EndValue[k]].
  - The grid lists them last first. A row's Start is the previous item's End ("Min" for the first item), and its End is its
    own ("Max" for the last). Both are read-only at the ends.
  - Editing a value pushes the neighbours to keep the order (`StockCompareGrid.Increase` / `Decrease`).
- **Compare Options** (compare.xaml, title "Compare Options"):
  - "Tolerance:" (DoubleEditBox, Minimum 0, Format G7, Width 125), Help / OK / Cancel (Width 70). Tolerance starts at 0.
  - OK with Tolerance > 0 (`CompareWindowViewModel.ApplyOk`) keeps the band count N. It sets the first N/2 ends to
    -tol·2^(N/2-1-i), the next N - N/2 - 1 to tol·2^j, and the last to +INF. With N = 7 and tol = 0.01 this is the default.
  - The resx also has Steps / Model / Display stock / Display model / Stock to leave labels, which the xaml does not use.
  - Remove Color (`ApplyRemoveStockCompareColor`) only edits the view's collection and has no button in the baml: not ported.

**Ours.**
- **The deviation.** ModuleWorks' `McamVerification.SetTarget` / `DeviationOffset` are not portable.
  - Ours is per cell of our height-map: stock top minus (the target's highest triangle at the cell centre (`vf_target_top`) +
    the applied offset).
  - No target under a cell gives +inf.
  - So it is vertical only: hole walls and undercuts are not measured as ModuleWorks would.
- **The Workpiece.** machsim.dll builds it from an entity list (`GetMesh`: get_raw_ent + wr_stl into a temp file). Which
  entities go in is NOT DECODED. Ours: the part's solids plus the solids made here, set as the target from the start.
- **The colours.**
  - They show while the window is open, after a Refresh (the applied scheme is kept until the next Refresh).
  - Walls take the high side's colour.
  - The depth tint is off while comparing.
- **Show Model** is greyed: the model is not drawn.
- The colour cell opens a QColorDialog (Actipro's colour editor in Mastercam).
- STL files are read in part units, world coordinates (`cam3d_stl_load`).

**Code.**
- `viewer/verify.c`: `vf_mesh` (per-cell colours, 9 floats per vertex) and `vf_target_top`.
- `viewer/scene.c`: the mesh shader's vertex colour, attribute 2, white for the other meshes.
- `viewer/core_ui/cui_verify.c`: `vf_cmp_*` and the XML keys StockToLeave / ToleranceUnit / StockCompareColors.
- `viewer-qt/src/simwin.cpp`: `buildCompare` / `fillCompare` / `compareOptions`.
- Test steps:
  - `vfcmp:show|hide|refresh|sel=N|stl=PATH|tol=V|opt=V|start=ROW,V|end=ROW,V|dump`;
  - `simset:` with RefreshCommand / ToggleStockVisibilityCommand / ShowStockCompareOptionsCommand / StockToLeave /
    StockCompareComboBox (an index, or a path) / StockCompareColors:ROW,COL.

## 9. The cut colour and Color Loop (decoded 2026-11-14)

The user asked for the cut area in yellow; Mastercam's default cut colour **is** yellow, so this is Mastercam's behaviour, not a
deviation.

**Decoded** (ilspy output in `~/mastercam-re/dotnet/`):
- **The uncut stock** is the verifier's mesh colour, RenderingSettings `StockColor` (#F66D6D):
  `mwsimstreammanager SimStreamAbstractRenderer.UpdateRenderSettings` sets `verifier.MeshColor = paramAdapter.GetStockColor()`.
- **The cut faces** are coloured by the colour scheme `ubvs MainWindowViewModel.UpdateColorScheme` hands to the verifier
  (`SimStreamColorScheme.UpdateVerifierColorScheme` -> the native `mwThreadSafeMachsimVerifier` draw mode + scheme):
  - Compare on (`ShowGouges`, a target selected): draw mode 13 with the StockCompareColors bands (section 8). Wins over the rest.
  - **Color Loop off** (the default, `UbvsCustomSettings.IsColorLoop` false): draw mode 12 with one entry,
    `Scheme[0]`. The Options resx says it outright: `GraphicsOptionsDefaultCutColorTextBlock_Text` "First color used as material
    removal color in verification mode if color loop is disabled."
  - **Color Loop on**, `drawMode` Operation (the default): draw mode 3, operation k -> `Scheme[k % n]`.
  - **Color Loop on**, `drawMode` Toolnumber: draw mode 2, keyed by the operation's tool id -> `Scheme[toolColorIdx % n]`.
    `toolColorIdx` is `ToolpathInfo.ComputeToolColorIndex`: an operation whose tool `IsTheSameTool` as an earlier operation's (the
    first such from 0) takes that one's index, else 1 + the highest index so far. The start value is `NoTool`
    (`?NoTool@VerificationOperation@ModuleWorks@@2HB`, -Module-.cs: data FF FF FF FF = -1), so the first tool gets 0.
- **`Scheme`** (`UbvsCustomSettings` initialiser; the shipped `mastercamsimulatordefaults.x` and the user's
  `MastercamSimulatorDefaults.xml` hold the same, with Aqua for Cyan): Yellow, Blue, DarkMagenta, Green, Pink, Cyan, Orange, Lime,
  Maroon, Navy, Gold, Fuchsia (System.Windows.Media.Colors). **So the default cut colour is Yellow #FFFF00**, not
  MillingFluteColor #DDBB00 (that is the cutter's flute mesh: `RenderableMillingToolFluteMesh`, `WireToolToRenderWire`).
- **The button:** Home > Playback > Color Loop, `ToggleIsColorLoopCommand` (CanExecute `IsStockEnabled`) toggles `IsColorLoop`;
  tip "Changes the color of the cut stock by operation or by tool change. Choose File, Options to set the colors." The setting
  change re-runs UpdateColorScheme (`updateColorSchemeAction`, 250 ms delayed) and the operation slider's colour intervals.
- The position bar's colours (`UpdateOperationSlider` -> `ColorIntervals` of the `GrayOutColorProgressBar`): per operation,
  `Scheme[0]` (loop off), `Scheme[i % n]` (Operation) or `Scheme[max(toolColorIdx, 0) % n]` (Toolnumber).
- NOT DECODED: which faces ModuleWorks counts as "cut" and how it keys them per operation / tool (native mwverifier). The
  Options page that edits Scheme / drawMode (options.baml: Loop by Operation / Tool number, Loop colors) is not ported (section 7).

**Ours** (`viewer/verify.c`, `viewer/core_ui/cui_verify.c`):
- Each height-map cell keeps the verified operation's index of the last cut that lowered it (`VF.cut`, -1 = never cut;
  saved in the backward-seek snapshots). The operation index counts operation changes along the verified motions.
- Cut cells: their top and the inner wall on their low side (the wall the cut exposed: `vf_mesh_ex` wall_low) get the cut colour;
  uncut tops and the stock box's outer sides get StockColor. Compare keeps its rule (walls take the high side).
- `IsTheSameTool` -> the same tool number.
- The depth tint (ours) is no longer applied to the Verify stock: the colours are drawn as Mastercam's.
- **Shading: the user-approved look** (2026-11-14, "brighter, near pure yellow"). ModuleWorks' stock lighting is NOT DECODED:
  the managed code only toggles `glLightModelf(GL_LIGHT_MODEL_TWO_SIDE)` (Rendering.cs, RenderableVerifier.cs,
  RenderableToolPartMesh.cs) and sets the mesh colour (MeshRenderer `glColor4fv(meshFrontDiffuse)`); the lights themselves are set
  natively. Ours (`viewer/scene.c` mesh shader, `tintz` = 1): the height map's tops are drawn unshaded, so cut tops are exactly
  #FFFF00 and uncut tops #F66D6D; walls keep a light headlight shade (0.55 + 0.45 x |n.light|) so the shape reads.
- Read from the XML: `IsColorLoop`, `drawMode`, `Scheme/SchemeColor` (names or #RRGGBB).
- Not ported: the position bar's colour intervals (our slider is a plain QSlider), the Options page.
- Test steps: `vfcolors` (cells per drawn colour), `simset:ColorLoop=1` / `vfset:IsColorLoop=1`.
- Scenes: tools/tests/misc_scenes.txt `sim_cutcolor`, `sim_colorloop`.
