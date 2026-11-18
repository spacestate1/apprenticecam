# Mastercam 2017 classic Backplot (NWDViewer) — implementation spec

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

Decoded from the original binaries. Every statement cites its source. Anything without a source
is marked **NOT DECODED** (with the next function to look at). Nothing here is "typical behaviour".

## 0. Sources and conventions

| Binary | Image base | Notes |
|---|---|---|
| `mastercam.dll` | 0x180000000 | MFC140. Ghidra project `~/mastercam-re/ghidra` / `mc2017`. |
| `backplot.dll` | 0x66d00000 | Links **MFC120** + BCGCBPro (`BCGCBPRO2430120.DLL`) + `uictrls.dll`, MSVCR120. Ghidra `~/mastercam-re/ghidra_bp` / `bp`. Full decompile used: 7385 functions. |
| `backplotbnci.dll` | 0x67d00000 | NCI model filter (motion entities, details pages, cycle time). Imported into `bp` for this work; 13641 functions decompiled. Loaded by backplot.dll via `BackplotBNCI.dll` + `GetProcAddress("CreateModelFilter")` (`FUN_66d7c8f0`). |
| `backplotres.dll` | — | Resources (dialogs, strings, bitmaps, menus, DLGINIT). Loaded as `resources\BackplotRes.dll` next to Backplot.dll (`FUN_66d7c5e0`). |
| `mcmachinedef.dll` | 0x180000000 | `RapidMoveTime` (Ghidra `~/mastercam-re/ghidra_mdef` / `mdef`). |
| `mccore.dll` | — | Owns all `bp_*` / `bpw_*` config globals (exports list `exports/mccore.dll.tsv`). |

- Addresses `66dxxxxx` = backplot.dll, `67dxxxxx` = backplotbnci.dll, `1800xxxxx` = mastercam.dll unless stated.
- MFC120 ordinal names used below are **inferred from call usage** (no MFC120 export table was available):
  `Ordinal_1480` = operator new, `_2165` = AfxGetModuleState, `_2175` = AfxGetApp (then vtbl+0xf8 = GetMainWnd),
  `_4671` = CWnd::FromHandle, `_5128` = CWnd::GetDlgItem, `_13643` = ShowWindow, `_13542` = SetWindowPos,
  `_4282` = EnableWindow, `_13906` = UpdateData, `_3548` = DDX_Control, `_3547` = DDX_Check, `_3646` = DDX_Text(CString),
  `_13318` = CSliderCtrl::SetRange, `_4604` = CString::Format, `_7401/_7402` = GetWindowText, `_13548` = SetWindowText,
  `_8295` = CWnd::MessageBox, `_12994` = CWnd::UpdateWindow, `_3662` = CWnd::Default.
- Win32 message numbers decoded from constants: 0x400 TBM_GETPOS, 0x401/0x402 TBM_GETRANGEMIN/MAX, 0x404 TBM_SETTIC,
  0x405 TBM_SETPOS, 0x409 TBM_CLEARTICS, 0x130a/0x130b/0x130c TCM_GETITEMRECT/GETCURSEL/SETCURSEL,
  0x1100 TVM_INSERTITEM, 0x1102 TVM_EXPAND, 0x110a TVM_GETNEXTITEM.
- Sizes: dialog-template sizes are **dialog units** (from `pe_dialogs.py backplotres.dll`); sizes set in code are **pixels**.
- Message maps were dumped from the `AFX_MSGMAP` tables (GetMessageMap = vtable slot 12); OnInitDialog = slot 95,
  OnOK = 97, OnCancel = 98, DoDataExchange = 66, PreTranslateMessage = 69.

### Class ↔ dialog map (backplot.dll)

| Dialog | Class (RTTI) | ctor | vtable | message map | OnInitDialog |
|---|---|---|---|---|---|
| 14000 "Backplot" | `OperationsManager` | `FUN_66d158f0` | 66dd0888 | 66dd0d00 | `FUN_66d15d30` |
| 16000 "Backplot Toggles and Functions" | `NWD::ToolpathControl` (derives `NWD::CSplitDialog`) | `FUN_66d17000` | 66dd25d8 | 66dd2c80 | `FUN_66d182c0` |
| 15000 "VCR bar" | `NWD::IncrementalSliderGeometryView` | `FUN_66d1d5f0` | 66dd45a0 | 66e1a990 (built at runtime by `FUN_66d1ef30`) | `FUN_66d1f330` |
| 17000 "Operation List" | `NWD::TreeView` (tree = `CMultiTree`) | `FUN_66d2aac0` | 66dd6840 | 66dd6d20 | `FUN_66d2bd90` |
| 12000 "Info Area" | `NWD::EditInfoView` | `FUN_66d1a5b0` | 66dd31c0 | 66dd35d0 | `FUN_66d1a970` |
| 13000 "Entity Info" (Details tab) | `NCIEntityDialog` | `FUN_66d26290` | 66dd4f90 | 66dd5460 | `FUN_66d26e20` |
| 4006 "Info Tab" | `NCIInfoDialog` | `FUN_66d28210` | 66dd5648 | (empty) | `FUN_66d288d0` |
| 6000 "Conditional Stops" | `BackplotDisplayStop` | `FUN_66d2e4b0` | 66dd6f18 | 66dd7370 | `FUN_66d2eba0` |
| 18000 "Save As Geometry" | `SaveAsGeometry` | `FUN_66d29040` | 66dd5b30 | 66dd5fa0 | `FUN_66d29430` |
| 4000–4013 per-move pages | `NWD::nci_*_AdapterDlg` (backplotbnci.dll) | see §7 | | | |

All dialogs share base `NWD::ToolpathViewerDialog` (`FUN_66d03700`): the 3rd ctor arg `param_3` (1 for 15000, 17000, 12000,
13000, 4006) makes `OnCreate` (`FUN_66d04320`) clear `WS_CAPTION` (`style & ~0x00C00000`), i.e. these are caption-less child panes.

---

## 1. Entry, lifetime and panel composition

### 1.1 Entry (mastercam.dll `backplot` @1800f8bb0)
1. `COpMgrTree::InvalidToolWarning`; negative → abort.
2. `UseUBVSPrototype(0)` (mcverify.dll @18003d870 → `FUN_18003d6a0`, registry `<REG_KEY>\Configurations\Variables`
   DWORD `"Use Classic Backplot"`; missing → written 1 → classic). If it returns true the Simulator path runs instead (not this spec).
3. Re-entrancy guard `DAT_1808d6a58` (pushed to 1 by `FUN_1800f8400`, restored by `FUN_1800f84a0`).
4. `repaint_toolpath` is cleared for the session and restored on exit; `graphics_support` is forced to 1 (with
   `rebuild_in_repaint`) and restored on exit (`backplot`, plus backplot.dll `FUN_66d14f90`/`FUN_66d14db0` do the same).
5. `NWDViewer::Construct(hwnd)` (`FUN_1800f8680`, viewer object `DAT_1808d6970`) where `hwnd` = parent window of the
   current graphics view (`GetParent(*(HWND*)(DAT_1809a8578+0x40))`). Then `NWDViewer::ResizeSlider` (`FUN_1800f8990`).
6. Event loop while `viewer+0xc8` ("active", `FUN_1800f88b0`) — see §10.
7. `NWDViewer::Destroy` (`FUN_1800f86a0`), `ClosePrompt`, `repaint_graphics`, `free_tools`.

Other entry: `OMbackplotSelectedOperations` @18034eba0 → `COpMgrTree::BackplotSelected` (body NOT DECODED).

### 1.2 `NWD::NWDViewer::Construct` @66d10ce0
- If `hwnd != NULL` **and Ctrl is held** (`GetKeyState(VK_CONTROL)` high bit) → only `ImmediateDraw()` (@66d12490: draws the
  toolpath once if no facade exists and `repaint_toolpath` is set); no dialogs are opened.
- If already active (`this[0xc8]` or facade present) → `MessageBox("Recursive call to Backplot.","Error",0x1000)` and break.
- Creates, in order:
  1. **14000 `OperationsManager`**, modeless, parent = main frame (`AfxGetApp()->GetMainWnd()`), via `Create(14000, …)` (vtbl+0x2d0).
  2. First time in the process only (`DAT_66e1d7ad`): expanded height `dlg+0x15d0 = height(hwnd window rect) − 0x23` and the
     dialog is sized to the collapsed height (`dlg+0x15cc − 1`, `FUN_66d11da0`); expanded flag `DAT_66e1d838 = 0`
     (**starts collapsed**).
  3. **15000 VCR bar** (`CreateSlider` @66d11570, §2).
  4. **16000 `ToolpathControl`** as a child of the 14000 dialog; stored in `dlg+0x2598`; both shown (`ShowWindow(…,SW_SHOW=4)`)
     and laid out by `FUN_66d15aa0`.
  5. `NWD::ToolpathFacade` (`FUN_66d7c8f0`, temp dir from `GetTempDirectory`) which loads `BackplotBNCI.dll` and creates the
     `NWD::GeometryModel` (`FUN_66d579c0`). The facade is registered with ToolpathControl (`FUN_66d19db0`) and VCR (`FUN_66d20c70`).
  6. `CreateGraphicalView` @66d11350: `NWD::MastercamGL` + `NWD::GraphicalGeometryView`; reads
     `HKLM\SYSTEM\CurrentControlSet\Control\Video\{4BC3AC62-CA54-44B6-A130-69B39E963428}\NNNN` (last existing NNNN) value
     `Ogl_EnableSingleBackDepthBuffer`; non-zero → view+0x40 = 1.
  7. Loads the NCI (`FUN_66d7cb30`, loops until the model reports complete, `Sleep(5)` every 20 iterations).

### 1.3 14000 "Backplot" dialog (`OperationsManager`)
Template: 154×307 DU, only control 14001 (14×12 DU button, tooltip "Expand or contract this dialog").
- OnInitDialog `FUN_66d15d30`: stores current window height in `+0x15d0`; collapsed height `+0x15cc = 0x8b (139) + GetSystemMetrics(SM_CYCAPTION)`
  (`0x8b` from ctor `FUN_66d158f0`); if not expanded → height set to collapsed height (`FUN_66d11c90`). Copies the main frame's
  icon (WM_GETICON → WM_SETICON). Button 14001 is a `UIBitmapButton` with bitmap **0x61b1 (25009) when collapsed, 0x61b0 (25008) when
  expanded**, loaded from the **core resource handle** (`GetCoreResourceHandle()`), transparent colour 0xC0C0C0.
- 14001 click `FUN_66d15c10`: toggles `DAT_66e1d838`.
  - → expanded: height = max(`+0x15d0`, **500 px**); bitmap 0x61b0.
  - → collapsed: saves current height in `+0x15d0`; bitmap 0x61b1; height = `+0x15cc`.
- WM_SIZING `66d15f30` (disassembly): if expanded and new height ≥ collapsed height → accept and store in `+0x15d0`;
  otherwise force `bottom = top + collapsed height`. ⇒ collapsed: fixed height; expanded: minimum = collapsed height.
- WM_SIZE `FUN_66d15ef0` → layout `FUN_66d15aa0`: ToolpathControl placed at **x=0, y=30 (0x1e) px**, width = client width,
  height = client height − 30, `SWP_NOZORDER`. (The top 30 px strip holds the 14001 button at template position 0,0.)
- Close (WM_CLOSE / OnCancel `FUN_66d15bb0`, DestroyWindow override `FUN_66d15a10`): posts **0x802a** to the main frame
  (which ends the backplot loop, §10) once the dialog is fully constructed (`+0x15c0`).
- Destroy (`NWDViewer::Destroy` @66d11730) remembers the dialog size in `DAT_66e1d7b0/_b4`; use of these on the next run NOT DECODED.
- Dialog x/y placement: decided by base `MCDialog` (mastercam.dll) — NOT DECODED.

### 1.4 16000 ToolpathControl layout (inside 14000)
Template 168×259 DU (button positions in the template are DU). Code overrides (pixels):
- OnInitDialog `FUN_66d182c0`: the 7 toggle buttons, each resized to **26×26**, at y=0, x =
  31001 → 0, 31002 → 26, 31003 → 52, 31004 → 78, 31005 → 104, 31006 → 130, 31007 → 164.
  Also 26×26: 16010, 16011, 16007, 16001.
  Bitmaps (backplotres BITMAP ids): 31001→31001, 31002→31002, 31003→31003, 31004→31004, 31005→31005, 31006→31006,
  31007→29354, 16002→16002, 16010→29364, 16011→29365, 16007→16007, 16001→16001 (`FUN_66d06da0` calls).
  DDX map `FUN_66d17bf0`: 16007,31007,31006,31004,31003,31002,31005,31001,16001,16002,16010,16011 are `UIBitmapCheckbox`/buttons.
- Creates **17000 TreeView** (child of 16000) and **12000 EditInfoView**, puts them in a 2-pane vertical splitter
  (`FUN_66d16100`/`FUN_66d16950`, pane 1 = tree, pane 2 = info area), shows both, starts timer **id 100, 50 ms** (§4.4).
- WM_SIZE `FUN_66d19410` (via `FUN_66d120a0`, mode 1 = x relative to right edge, mode 3 = x and y relative to right/bottom):
  row 2 at **y = 25**: with `w` = width of the 16001 button: 16010 at `W−(4w+12)`, 16011 at `W−(3w+10)`, 16007 at `W−(2w+8)`,
  16001 at `W−(w+4)`. OK (id 1) at `(W − 2·hw − 6, H − 1 − hh)`, Help (id 9) at `(W − hw − 4, H − 1 − hh)`
  (hw/hh = Help button size). Splitter area: **x=0, y=60 (0x3c), width = W, height = H − 68 − hh**; if that is < 1 both panes
  are hidden (that is the collapsed state). 16002, 16003, 16004, 16005, 16006 keep their template positions.

### 1.5 12000 Info Area (`EditInfoView`)
- One `SysTabControl32` (id 100). OnInitDialog `FUN_66d1a970` creates two pages as children of the tab:
  **tab 0 = string 16005 "Details" → dialog 13000 `NCIEntityDialog`**, **tab 1 = string 16006 "Info" → dialog 4006 `NCIInfoDialog`**;
  selects tab 0 and shows the 13000 page.
- WM_SIZE `FUN_66d1aab0`: tab fills the client; pages placed inside at (`SM_CXEDGE`=GetSystemMetrics(0x20), itemHeight+edge),
  size (W − 2·edge, H − 2·edge − itemHeight).
- TCN_SELCHANGE (0xfdd9) `FUN_66d1ac10`: cursel 0 → show 13000 page only; 1 → show 4006 page only.
- 13000 page (`FUN_66d27040`): hosts the per-move page (§7) under its static 29359 (`+0x1768`), sized to fill the rest.

---

## 2. VCR bar (dialog 15000, `IncrementalSliderGeometryView`)

### 2.1 Placement and layout
- `CreateSlider` @66d11570: created as child of the `hwnd` passed to Construct (parent of the graphics view), resized to
  **client width × 30 px**, moved to **(0, 1)**, shown; `SetFocus(hwnd)`. `NWDViewer::ResizeSlider` called after Construct (body NOT DECODED).
- DDX `FUN_66d1eb30`: 30002 Stop(+0x1678), 30005 Step fwd(+0x2680), 30004 Step back(+0x3688), 30006 Next stop(+0x4690),
  30003 Prev stop(+0x5698), 30001 Play(+0x66a0), 15006 speed `CSliderCtrl`(+0x86b0), 15001 position `NWD::NWDSliderCtrl`(+0x8788),
  15002 Trace(+0x99a8), 15009 Run(+0xa9b0), 15007 Conditional stops(+0x89a0), 15008 Help(+0x76a8).
- OnInitDialog `FUN_66d1f330`: all buttons resized to **24×24**; bitmaps: Play 29348, Stop 29345, Step fwd 29346,
  Step back 29349, Next stop 29347, Prev stop 29350, Cond. stops 29360, Help 29361, Trace 28002, Run 29358.
  Tooltips for the two sliders: strings 15006 "Run speed", 15001 "Visible motion position".
- WM_SIZE `FUN_66d1ff40` (y for buttons = height/2 − 12):
  Play x=0, Stop 24, Prev stop 48, Step back 72, Step fwd 96, Next stop 120.
  Only when width > height: Trace x=150, Run x=174, run-speed slider x=198 y=2 **w=100** h=H−4,
  position slider x=298 y=2 **w=W−346** h=H−2, Cond. stops x=W−48, Help x=W−24.

### 2.2 Position slider (15001) — units = visible motions
- Range set in `FUN_66d20e60` (model-changed handler): `N` = Σ(end−start) over the visible motion ranges (`model+0xbe0`);
  if the list is empty, `N` = motion count (`FUN_66d5dfd0`). If `N−1 ≠ current max`: TBM_CLEARTICS, `SetRange(0, N−1)`, pos 0.
  A slider position is an index into the concatenated visible ranges (mapped back by `FUN_66d5da40` / `FUN_66d1ee20`).
- Tick marks = conditional-stop list (`FUN_66d83be0(slider, model+0x700)`), coloured channel segments per operation/path
  (`FUN_66d1dcf0`, default colour **0x808080** for ops with no paths; per-path colours from `FUN_66d3bcf0`/`FUN_66d86e20`: NOT DECODED).
- WM_HSCROLL/WM_VSCROLL `FUN_66d1f0f0` (both map to the same function): on a change of position the model is moved to
  the **end of that motion** (`FUN_66d841f0` → `FUN_66d5c420(...,FLT_MAX)` → `FUN_66d62650`). On `SB_ENDSCROLL` (8) the
  quick-verify refresh `FUN_66d1e9f0(…,1,…)` is triggered.
- `NWDSliderCtrl` (msg map 66ddff30): left click outside the thumb (`FUN_66d83900`) jumps the thumb proportionally to the
  click x and sends `WM_HSCROLL(SB_ENDSCROLL)` to the VCR; mouse wheel (`FUN_66d83ac0`) is forwarded to the parent (whose
  WM_MOUSEWHEEL handler `66d1fb70` just returns TRUE) and then default trackbar processing; right-click → default.
- Model → slider sync: queued id 1 → `FUN_66d1e780`/`FUN_66d21580` (position → slider index; −1 if outside visible ranges).

### 2.3 Run-speed slider (15006)
- `FUN_66d20e60`: `SetRange(0,100)`, pos 100, ticks at 0 and 100, then pos = `ModelVisualOptions+0x90` (= mccore
  `bp_speed_pos`, loaded by `FUN_66d76180`); **if 0 it becomes 75 (0x4b)**. For wire operations (`FUN_66d1eaa0`):
  `bpw_speed_pos`, 0 → 75.
- Changing it (`FUN_66d1f0f0`, slider = +0x86f0) writes the value into ModelVisualOptions+0x90 (applied with `FUN_66d630a0`);
  the ModelVisualOptions destructor (`FUN_66d75d10` → `FUN_66d766c0`) writes it back to `bp_speed_pos`.

### 2.4 Play / timing (Run)
- Play 30001 `FUN_66d1fbe0`: if the run timer exists → **stop** (acts as pause). Otherwise: if the slider is at its max, or
  |end − current distance| ≤ 1e‑4, rewind to slider pos 0. If at pos 0 and `fit` is enabled (`ModelVisualOptions+0xc0`
  = `bp_fit_toolpath`; wire: `bpw_fit_wirepath`) → fit view to the path extents (`FUN_66d1ec40`: `bplot_min/max_x/y`, `fit_plot()`).
  Then reset the clock and `SetTimer(id 0x65, 1 ms)`.
- WM_TIMER 0x65 `FUN_66d20510` → `FUN_66d84490(engine, speed = ModelVisualOptions+0x90, …)`; false → timer killed (end reached).
- Advance per tick `FUN_66d84490` (distance along the path, `L` = total length `FUN_66d5eae0`, `c` = current `FUN_66d5eb30`,
  `u` = `model+0xc48`, `dt` = seconds since last advance via QueryPerformanceCounter):
  ```
  if dt < 0.02: return            # max 50 updates/s
  maxStep = min(u*50000, (dt/5)*L)
  minStep = u*5*dt;  if maxStep < minStep: minStep = maxStep   (as coded: dVar1 = min(dVar2, dVar1))
  if speed == 0: return
  f    = (1 - (ln(101-speed)^2 / ln(10)^2) * 0.25) * (speed/100)
  step = f*(maxStep - minStep) + minStep
  if step <= 1e-8: return
  if c+step <= L: seek(c+step) else seek(L) and stop
  ```
  ⇒ speed 100 traverses the whole path in ~5 s (bounded by `u*50000` per second); speed 1 gives `f = 0` (min step only).
  `model+0xc48` (`u`) = model-space length of one screen pixel — decoded in §12.2.
- Stop 30002 `FUN_66d204e0`: KillTimer(0x65).

### 2.5 Step back / Step forward / Prev / Next stop (`FUN_66d84250(engine, mode, count)` via `FUN_66d7d380`)
| Button | Handler | mode, count |
|---|---|---|
| 30003 Previous stop | `FUN_66d1fdb0` | 1 → previous stop (`FUN_66d84030` lower bound in stop list) |
| 30004 Step back | `FUN_66d20440` | 2, 1 |
| 30005 Step forward | `FUN_66d20490` | 3, 1 |
| 30006 Next stop | `FUN_66d1f0a0` | 4 → next stop |
Other modes: 0 → motion `DAT_66e1eff8` (.bss, no writer found ⇒ 0 = start), 5 → last motion.
- Granularity (`FUN_66d76150`): for wire ops (motion flag +0x69) uses `bpw_interpolate_steps`/`bpw_interpolate_distance`,
  else `bp_interpolate_steps`/`bp_interpolate_distance` (ModelVisualOptions +0xa0/+0xa8, +0xb0/+0xb8).
  If the steps flag is **off**: one **motion** per step (index ±count). If **on** (and mode is 2/3): move **count × distance**
  along the path (`FUN_66d627c0`).
- Prev/next stop when exactly on a stop (`FUN_66d84030`): if the current position is a stop and its parameter ≥ 1.0 at the
  end of motion, the search moves one motion further in each direction.
- **Turbo** (button held): the VCR's runtime message-map entry (message from `OnMouseMove_exref`, handler `FUN_66d20ab0`,
  lParam = held time in ms) — if `(ms/1000)·bp_fast_steps_per_second` ≥ 1: for 30005 step `+fast_steps`, 30004
  `−fast_steps`, applied by `FUN_66d84720` as `elapsed_seconds × rate` motions (or × step distance in interpolate mode).
  `bp_fast_steps_per_second` = ModelVisualOptions+0x18 (ctor default 1000, `FUN_66d75bd0`, then loaded from mccore).
- After every step `FUN_66d1e9f0`: if quick verify (flag 0xb) is on, refresh it (throttled: if < 0.5 s since last, deferred
  to queued id 2 via timer 0x8069, `FUN_66d04150`).

### 2.6 Run mode (15009) vs Trace mode (15002)
- Trace 15002 `FUN_66d1f2e0`: display flag byte **0xd = 0**, change byte 0x10 |= 0x20. Run 15009 `FUN_66d205a0`: byte **0xd = 1**,
  0x10 |= 0x20. Both via `FUN_66d21730`: Trace checkbox = (0xd == 0), Run checkbox = (0xd ≠ 0) (mutually exclusive), then
  `FUN_66d63280` (apply) + `FUN_66d5f990`.
- Default: **Run** (byte 0xd = 1 in `FUN_66d80400`), subject to the saved toggles (§4.2).
- What the renderer does differently for byte 0xd: decoded in §12.3 (Run = path up to the current motion, Trace = whole path). Original note: **NOT DECODED** (no reader of model+0x25d / view+0x60 found in
  backplot.dll or backplotbnci.dll; next: GraphicalGeometryView `FUN_66d78280`/`FUN_66d7b950` callees and the pipe
  built by `FUN_66d67f60`/`FUN_66d68170`). (Help text `Backplotting_toolpaths.htm` says Run "displays the toolpath as the tool
  moves along", Trace "displays the entire toolpath as the tool moves over it" — documentation only, not verified in code.)

### 2.7 15007 Conditional stops → §3. 15008 Help → `setCurrentIDD(15000); get_dialog_help(0xb,15000)` (`FUN_66d1fd80`).

---

## 3. Conditional stops (dialog 6000, `BackplotDisplayStop`)

Settings struct = `GeometryModel+0x700` ("cs"); defaults from `FUN_66d8a460`: everything 0/false, step number 0, X/Y/Z values 0.0.
Dialog opened by 15007 (`FUN_66d1fe00`); on IDOK and if the dialog reports a change (`cs+0xd8`), stops are recomputed (`FUN_66d62070`).

| Control | cs field | Init (`FUN_66d2e4b0`) |
|---|---|---|
| 6001 "Stop at Operations" (check) | +0x98 | |
| 6013 "Stop at Toolchange" (check) | +0x99 | |
| 6002 "Stop at step number:" (check) | +0xd0 | |
| 6003 step number (edit) | +0xcc (uint) | `"%d"` |
| 6004 "X:" / 6006 "Y:" / 6008 "Z:" (checks, group 1631 "Stop at XYZ value") | +0x9b / +0xa8 / +0xb8 | |
| 6005 / 6007 / 6009 (edits) | +0xa0 / +0xb0 / +0xc0 (double) | 4 decimals, trailing zeros removed (`FUN_66d87620`) |
| 6010 X / 6011 Y / 6012 Z (checks, group 1632 "Stop at XYZ change") | +0xc8 / +0xc9 / +0xca | |
DDX `FUN_66d2e950`; OK `FUN_66d2ebc0` → `FUN_66d2e7a0` (atof/atoi; written only if different, compare tol 1e‑5, `FUN_66d8a710`).
- 6001 handler `FUN_66d2ebe0`: when "Stop at Operations" is checked, "Stop at Toolchange" is **forced checked and disabled**
  (previous value kept in `DAT_66e1d8c8` and restored when unchecked).
- Evaluation (`FUN_66d62070`): reset (`FUN_66d8b180`) → stop at motion 0. Walking the visible ranges:
  - new operation (`FUN_66d8b1f0`): if ops flag → stop at current motion; (always also recorded in list +0x38).
  - tool number change (`FUN_66d8b400`): if toolchange flag and tool ≠ previous (or −1) → stop.
  - per motion point (`FUN_66d8b260`), in this order:
    1. if step flag and N≠0 and `stepCounter % N == 0` → stop;
    2. else compare with previous point: X changed by > 1e‑5 and X-change flag → stop; X unchanged and X-value flag → **no stop for this point**;
       same for Y, Z;
    3. stop if any enabled value matches: |x−Xv| < 1e‑5 or |y−Yv| < 1e‑5 or |z−Zv| < 1e‑5.
  - Stops outside the visible ranges are moved/removed (`FUN_66d8adf0`).

---

## 4. Toggles and functions (dialog 16000)

### 4.1 Display flag struct (`GeometryModel+0x250`, 17 bytes) and the toggles
Getter `FUN_66d7cf10`; apply `FUN_66d7d370` → `FUN_66d63280` (copies 17 bytes, invalidates caches when byte 4 changes, notifies
views with `GMMChangedView`). Each 31xxx handler copies the struct, toggles one byte, sets a "changed" bit, applies it
(`FUN_66d19e90`, which also writes byte 0xb into every machine group `op_group+0x16e2`) and updates the checkbox.
Checkbox sync from flags every 50 ms (`FUN_66d17aa0`).

| Byte | Button | Handler | Changed bit | Default (`FUN_66d80400`) |
|---|---|---|---|---|
| 0 | 31001 Display with color codes | `FUN_66d18f00` | byte 0xf bit0 | 0 |
| 1 | — (no button) | | | **1** |
| 2 | 31005 Display endpoints | `FUN_66d18fd0` | 0xf bit2 | 0 |
| 4 | 31004 Display rapid moves | `FUN_66d191a0` | 0xf bit4 | 0 |
| 7 | 31002 Display tool | `FUN_66d19340` | 0xf bit7 | 0 |
| 8 | 31003 Display holder (turning it **on also turns tool on**) | `FUN_66d190a0` | 0x10 bit0 | 0 |
| 0xb | 31006 Quick verify | `FUN_66d19270` | 0x10 bit3 | 0 |
| 0xd | Run(1)/Trace(0) (VCR) | §2.6 | 0x10 bit5 | **1** |
| 0x10 | change bits | | | `&= 0x80` |
Bytes 3,5,6,9,0xa,0xc,0xe: default 0, no writer decoded.

### 4.2 Persistence
- Load (`FUN_66d805c0`, called by the init above inside the GeometryModel ctor `FUN_66d579c0`):
  `HKCU\Software\CNC Software, Inc.\Mastercam 2017\Configurations\Variables\BackplotToggles`, **unnamed value, REG_BINARY 0x11 bytes**
  = the struct verbatim (key name gets `" Demo-HLE"` appended to `Mastercam 2017` when `GetProdEnableBits() & 1`).
- Save (`FUN_66d80730`, from GeometryModel dtor `FUN_66d58fa0`): same key, `RegSetValueExA(…,REG_BINARY,…,0x11)`.
- Everything else (colours, speed, steps, tool appearance…) lives in mccore config globals, loaded into
  `NWD::ModelVisualOptions` by `FUN_66d76180` and written back by `FUN_66d766c0` (table §4.3). How mccore persists those
  globals to the config file: NOT DECODED (next: `CfgPageBackplotDialog` / `CfgPageBackplotWireDialog` in mccore.dll).

### 4.3 ModelVisualOptions ↔ mccore globals (`FUN_66d76180` load / `FUN_66d766c0` save; ctor defaults `FUN_66d75bd0`)
The ctor writes the defaults below and then immediately calls `FUN_66d76180`, so at runtime the mccore globals win.
Colours are `Wd_colors[index]` (RGB) at load; on save the RGB is searched back in the 256-entry `Wd_colors` table.
| Off | Global | Ctor default |
|---|---|---|
| 0x24 | `bp_rapid_color` | 0xffff |
| 0x28 | `bp_linear_color` | 0xff00ff |
| 0x2c | `bp_cwarc_color` | 0xff0000 |
| 0x30 | `bp_ccwarc_color` | 0xff8080 |
| 0x34 | `bp_5axisvector_color` | 0xc000c0 |
| 0x40 / 0x4c | tool colour: `bp_tool_material_on` ? `(bp_tool_material+1)<<24` : `Wd_colors[bp_tool_color]` / `Wd_colors[bp_tool_color]` | 0xc0c0c0 |
| 0x38 / 0x44 | holder (same pattern with `bp_holder_*`) | 0x38: 0xc0c0c0 |
| 0x3c / 0x48 | wire guide (`bpw_guide_*`) | 0x3c: 0xc0c0c0 |
| 0x54..0x74 | `bpw_rapid_color`, `bpw_xy_linear/cwarc/ccwarc_color`, `bpw_uv_linear/cwarc/ccwarc_color`, `bpw_offset_color`, `bpw_connector_color` | 0xffff, 0xff00ff, 0xff0000, 0xff8080, 0xff00ff, 0xff0000, 0xff8080 |
| 0x94 | background `Wd_colors[gr_back_color]` | |
| 0x9c | `in_black_white` (1 → all motion colours 0 / black) | 0 |
| 0x0c / 0x0e | `mill_tool_appearance < 2` / `== 1` (meaning of values 0,1,2 NOT DECODED — **decoded §13.4: 0 Plain, 1 Fluted, 2 Shaded**) | |
| 0x0d | `bpw_guide_appearance < 2` | |
| 0x0f | `bp_spin_tool` | |
| 0x10 | `show_holder` | |
| 0x18 | `bp_fast_steps_per_second` | 1000 |
| 0x90 | `bp_speed_pos` | 0x32 (50) (then 0 → 75 in VCR) |
| 0x98 / 0x99 / 0xe0 | `bp_draw_to_from_homepos` / `_lathe` / `bpw_draw_to_from_startpos` | |
| 0xa0 / 0xa8 | `bp_interpolate_steps` / `bp_interpolate_distance` | 0.05 (0x3fa999999999999a) |
| 0xb0 / 0xb8 | `bpw_interpolate_steps` / `bpw_interpolate_distance` | 0.05 |
| 0xc0 | `bp_fit_toolpath` | 0 |
| 0xc8 / 0xd0 / 0xd8 | `bp_opacity_active` / `bpw_opacity_active` / `bp_opacity_inactive` | 0.5 / 0.5 / 0.15 |
| 0x1c / 0x1d / 0x1e / 0x1f | `bp_cleanup_scrn_on_op_change` / `_tl_change` / `bpw_cleanup_scrn_on_thrd` / `bpw_cleanup_scrn_on_pass` | |
| 0x08 / 0x09 / 0x0a / 0x0b / 0x50 | `bp_simulate_cutter_comp` / `bp_show_caxis_top` / `bp_simul_rot` / `bp_simul_caxis` / `bpw_simulate_wire_comp` | |
| 0x7a..0x84 | `bp_5axisvector_check3/2/1`, `_length_cb`, `_length` (float), `_on` (inverted) | |
| 0xe1 / 0xe4 | `!bp_verify_use_motion_color` / `Wd_colors[bp_verify_color]` | 0x7f7f7f |

### 4.4 50 ms timer (ToolpathControl WM_TIMER `FUN_66d197d0`)
Syncs checkboxes; enables 16002 only when the position is valid; when the position changes writes the status bar
(`UpdateStatusBarText`): mill `"X:%1.4f   Y:%1.4f  Z:%1.4f"`; lathe `"X:%1.4f   Z:%1.4f"` or, if `LatheXIsDiameter()`,
`"D:%1.4f   Z:%1.4f"` with X×2; empty text when no valid position. (Lathe point is transformed by `FUN_66d177d0` when
ModelVisualOptions+8 is set.)

### 4.5 Function buttons
| Id | Behaviour | Source |
|---|---|---|
| 31007 Options | `FUN_66d19c90` sets `bp_cc_in_control_enabled/_radius` (first multiaxis op: enabled, radius = op+0x2e8 × 0.5, clamped ≥ 0; wire op first: disabled); then mastercam.dll `BackplotOptions(NULL, isWireOpAtPosition)`; if it returns true, reload ModelVisualOptions from globals (`FUN_66d76180`) and apply. Options dialog contents: NOT DECODED (mastercam.dll `BackplotOptions`). → **Decoded §13.1** (`BackplotOptions` is in mccore.dll; dialogs 1228/1229). | `FUN_66d17fa0` |
| 16001 Save as geometry | modal 18000 `SaveAsGeometry`; IDOK → `FUN_66d7d230` (pipeline that converts the drawn path to geometry). | `FUN_66d18dc0` |
| 16007 Save tool geometry | same dialog with caption string 18003 "Save Tool Geometry"; IDOK → `FUN_66d7d180` (GMMDraw to the GraphicalGeometryView). | `FUN_66d19680` |
| 16002 Add point | if position valid: selects the **Details** tab (TCM_SETCURSEL 0) and shows the 13000 page. | `FUN_66d18840` → `FUN_66d1a820` |
| 16003 Isolate area | **no-op** (`ret`). | 66d18aa0 |
| 16004 Change path view type | popup menu 19000 at cursor; its commands **19001 View by Rapid and 19002 View by Level are no-ops** (`ret`). | `FUN_66d18ac0`, 66d18b90, 66d18b80 |
| 16005 | static, bitmap #29278 | template |
| 16006 (edit) | on EN_KILLFOCUS (and Enter = OnOK slot 97): text `"a-b"` split with `strtok("-")`, `atol`; missing b → b = a+1; each converted with `FUN_66d5bf70` and the range set as visible (`FUN_66d5f040`); the edit is then cleared. | `FUN_66d188b0` |
| 16010 Restrict drawing | checkbox checked; visible restriction list = [current motion (+1 if the current parameter ≥ FLT_MAX), motion count); invalidates cache `model+0x9a8`, redraw. | `FUN_66d18ba0` |
| 16011 Turn off restricted drawing | same handler with flag `+0xed48`: 16010 unchecked; list = [0, count). | `FUN_66d18d90` |
| 1 OK | sets abort flag `model+0xa50`; if any operation is modified (`FUN_66d5fc00`) shows MessageBox(string 17002 "Changes have been made to the toolpath.\nDo you want to save the changes?", caption 29001 "Warning", MB_YESNO) — **the answer is not used** (disassembly 66d18130…66d1822f); posts 0x802a to the main frame (exit). | `FUN_66d18130` |
| 9 Help | `get_dialog_help(0xb,16000)` | `FUN_66d18280` |

18000 dialog (`FUN_66d29040`/`FUN_66d29430`/`FUN_66d29500`): level from `HKCU\…\Configurations\Variables\BackplotMisc` DWORD
`SaveLevel` (0 → `bp_save_geom_level_number`); 29352 "Save using attribute manager settings" = `bp_save_with_EA && ea_active`,
disabled when `ea_active` is 0; when checked, 29353/18001/18002 are disabled; 18002 "Level" → `DisplayLevelsDialog(1,1,&lvl)`.
IDOK writes `bp_save_geom_level_number`, `bp_save_with_EA` and the `SaveLevel` DWORD.

---

## 5. Drawing

- Tool/holder drawn at the current position only when display flags 7 (tool) or 8 (holder) are set (`FUN_66d78a00` via view
  copies `+0x5a`/`+0x5b`). Polygon "lit/shaded" choices depend on ModelVisualOptions+0xc (`mill_tool_appearance < 2`):
  lighting/blend toggled (`vtbl+0x18/+0x20/+0x68` of the render layer); exact shaded vs wireframe mapping NOT DECODED → decoded §13.4.
- Spin tool (ModelVisualOptions+0xf = `bp_spin_tool`): tool rotated by **360°/s** of wall-clock time, sign from tool entity
  +0xa8 (`FUN_66d78a00`).
- Tool colour/material from ModelVisualOptions 0x40/0x38 (§4.3).
- Rapid display (flag 4): when 0, `FUN_66da4d40`/`FUN_66da6880` emit a `RenderSuppress` attribute around the motion being
  processed (button tooltip "Display rapid moves"); which motions are classified as rapid there: NOT DECODED.
- Endpoints (flag 2): read in `FUN_66da5ae0` (line using `param_1[0x15]+2`); marker shape NOT DECODED → §13.3 (white GL point, size 2.0).
- Colour codes (flag 0): `FUN_66da4750`/`FUN_66da4d40` select between `bp_rapid_color`(+0x24), `bp_linear_color`(+0x28),
  5-axis vector colour (+0x34) and grey 0x808080, black when `in_black_white`; the exact motion-type → colour table
  (incl. cw/ccw arc +0x2c/+0x30) NOT fully DECODED (next: `FUN_66da4750`, `FUN_66da4d40`, `FUN_66da5ae0`, `FUN_66da6320`) → §12.3 and §13.2 (with RGB).
- Quick verify (flag 0xb): `glEnable(GL_POLYGON_OFFSET_FILL)`, `glPolygonOffset(1.25,1.5)` around the model draw (`FUN_66d78280`). More in §13.5 (geometry still NOT DECODED).
- Screen cleanup on operation/tool/pass/thread change (ModelVisualOptions 0x1c–0x1f) limits the dynamic visible range
  (`FUN_66d77b70`).
- Path drawn so far vs whole path: depends on §2.6 — NOT DECODED.

---

## 6. Info tab (dialog 4006, `NCIInfoDialog`) — computed on demand

- Data: `FUN_66d5e740` creates an info object (model+0xa00 vtbl+0x28, argument ModelVisualOptions+0x98) and accumulates
  every entity in the **visible ranges** (`model+0xbe0`) via entity virtuals +0x130 (bounds), +0x158 (time/length), +0xa8.
- Fill: `FUN_66d28a10` (on GMM messages), DDX `FUN_66d28540`, time format `FUN_66d28710`:
  hours = 0 and minutes = 0 → `"%1.2lfs"`; hours = 0 → `"%im:%1.2lfs"`; else `"%ih:%im:%1.2lfs"` (value in seconds; hours include days×24).

| Field (ctrl) | Value |
|---|---|
| Cycle time Feed (4601) | `info+0x88` (feed time, seconds); NaN/inf → string 29003 "Invalid feedrate" |
| Cycle time Rapid (4602) | `info+0x90 + info+0x98` |
| Cycle time Total (4603) | `info+0x88 + info+0x90 + info+0x98`; NaN feed → 29003 |
| Path length Feed (4604) | `info+0x70` |
| Path length Rapid (4605) | `info+0x78 + info+0x80` |
| Min X/Y/Z (4606/4607/4608) | AxisBounds min (`info+0x30/0x38/0x40`) |
| Max X/Y/Z (4609/4610/4611) | AxisBounds max (`info+0x50/0x58/0x60`) |
| Min/Max feedrate (4612/4613) | min(|info+0xa0|,|info+0xa8|) / max(...) |
Numbers use `n_places_an` decimals with trailing zeros removed (`FUN_66d87620`). If total cycle time is exactly 0 the
cycle-time statics (25414–25421) and 4601–4603 are hidden; this also happens when the feed time is NaN/inf (the code then uses 0 as total).

Accumulation (backplotbnci.dll):
- Linear feed `FUN_67d9e740`: length += L (`+0x70`); feedrate F → min/max (+0xa0/+0xa8); time `+= (L/F)·C1·60·C2` (shown as seconds by `FUN_66d28710`).
- Arc / helix with F ≥ 0 `FUN_67d9e3f0`: same formula; F < 0 branches (inverse-time/rev with spindle `info+0xb8`,
  `IsMetric`, 0.0254) NOT fully DECODED.
- Rapid `FUN_67da5c00`: length → `+0x78` (or `+0x80` for home moves when info flag +0xc8 (mill) / +0xc9 (lathe) is set);
  time = `RapidMoveTime(op, Δ, 1)·C3` → `+0x90` (or `+0x98` for home moves).
- Tool change `FUN_67dbb350` (entity type 0x3ea): `+0x90 += avg_tool_change / 60.0 · C4` (mccore `avg_tool_change`).
- `C1..C4` = `_DAT_67ea6e70`, `DAT_67ea6e68`, `DAT_67ea7638`, `_DAT_67ea94d8`: writable .data doubles, static value **1.0**,
  no writer found in the decompile.
- **Rapid rate source** (mcmachinedef.dll `RapidMoveTime` @1800ffc80; field offsets and defaults in §13.6): machine definition of the operation
  (`OMgetOpMachine`, axis combination `op+0x11bc`), per-axis `CLinearAxisComponent::MaxFeedrate(metric)` for axes 1..3.
  Lathe ops: Δ transformed view→world→view (`op+0x572`, `op+0x6d2`); op types 0x34/0x38 swap Y/Z.
  Control `CtrlLinearMotion(machineType==2)`: first short == 0 → **time = max over axes |Δaxis| / maxFeed(axis)**;
  otherwise → **|Δ| / (smallest non-zero max feed)**. ×60 (seconds) when the 3rd arg is set. |Δ| ≤ `tol` → 0; no machine/axis
  combination/control → 0.

---

## 7. Per-move detail pages (Details tab, 13000)

Page classes live in backplotbnci.dll (`NWD::nci_mill_0/1/2/4/11_Adapter(Dlg)`, `nci_wire_linear/arc/4axis_AdapterDlg`);
the entity's virtual +0x18 creates/fills the page (`FUN_66d26a10`, refresh throttled to 0.2 s by `FUN_66d26940`).
Fields are **two-way** (DDX both directions): edits are applied by 13003 / 25322 (`FUN_66d27190` / `FUN_66d26e50` →
`FUN_66d61950`, optional split `FUN_66d5ef00` when `+0x1618/+0x1620 ≥ 0`).

| Page | DDX fn | Fields |
|---|---|---|
| 4000 Rapid / 4009 Lathe Rapid (`nci_mill_0`) | `FUN_67daf130` | X 4100, Y 4101, Z 4102, Feedrate 25192, Cutter comp 4105. Lathe & `LatheXIsDiameter`: X shown ×2 and its label set to "D" (else "X"). Feed < 0 (mill) → feed edit shows "". |
| 4001 Feed / 4007 Lathe Feed (`nci_mill_1`) | `FUN_67dae4f0` | X, Y, Z, Feedrate 25192, (lathe) RPM 4848, Cutter comp 4105 |
| 4002 Arc / 4008 Lathe Arc (`nci_mill_2`) | `FUN_67dafbe0` | end X/Y/Z 4100–4102, center X/Y/Z 4202/4204/4206, Feedrate, (lathe) RPM 4848, Plane 4201 = "XY"/"XZ"/"YZ" for value 0/1/2, Circle 4207 = entity+0x42, Cutter comp |
| 4003 5 Axis (`nci_mill_11`) | `FUN_67dcf770` | tip 4100–4102, center 4203/4205/4510, Feedrate, surface normal 4301–4303 |
| 4011 Wire Feed | `FUN_67db0460` | X, Y, Z, Taper 4801, Wire comp 4105 |
| 4012 Wire Arc | `FUN_67db09b0` | X, Y, Z, center 4202/4204/4206, Taper, Wire comp |
| 4013 Wire 4 Axis | `FUN_67db1010` | X, Y, Z, U/V/W 4810–4812, Taper, Wire comp |
| 4004 Drill Start, 4005 Drill, 4010 Mill Generic Info | — | NOT DECODED (no reference to these ids found in backplotbnci.dll; next: search other DLLs for 0xfa4/0xfa5/0xfaa) |

- Cutter comp (`FUN_67dcdf80`): 0 → 4703 "None", 40 → 4700 "Off", 41 → 4701 "Left", 42 → 4702 "Right";
  −40/−41/−42 (0xffd8/0xffd7/0xffd6) display Off/Left/Right and set a "negative" flag kept on write-back.
- Coolant combo 4104: DLGINIT items `off`, `flood`, `mist`, `tool` (backplotres DLGINIT 4000/4001/4002/4007/4008/4009);
  no DDX of 4104 found ⇒ how it is filled: NOT DECODED. → §13.7: the combo and its label are hidden + disabled in every template.
- Editing: see §13.7 — all page edits are ES_READONLY and the apply commands 13002/13003/25322 have no sender ⇒ display-only.

---

## 8. Operation List (dialog 17000, `NWD::TreeView`)

- Build `FUN_66d2c4f0`: level 1 = machine groups (text = group name at `op_group+4`, image bitmap 0x112 from the
  Mastercam resource handle); level 2 = operations of that group present in the backplot (text =
  `op_make_description(op,1,buf,255)`, image bitmap 0x69); level 3 = each "path" (sub-range with end ≠ start) of the
  operation, text by callback (TVN_GETDISPINFO `FUN_66d2bc10`) = `"%s %d"` with string 17001 "Path" and 1-based index
  (or `"%d"` when the item has no stored data). [§13.8: the number is position+1 with a 1-based position ⇒ first path shows "Path 2"; record layout there.] A group item inserted under an existing tree item (`FUN_66d88370`) uses image 0x2d5 (`+0x1604`); the exact condition is NOT DECODED. Root expanded at the end. Image list 16×16 (`FUN_66d2bd90`).
- Selection (TVN_SELCHANGED → `66d2c300` sets a flag; processed in `FUN_66d2bfc0`): the motion ranges of all selected items
  become the **visible ranges** (`FUN_66d5f220(model, ranges, 1, 0)`) and the position jumps to the **last motion of the
  selection** (`FUN_66d841f0`). (Multi-selection via `CMultiTree`.)
- Right click → menu 19100 (`FUN_66d2c1d0`): 19101 Expand All (`FUN_66d2bb90`, TVE_EXPAND on every root),
  19102 Collapse All (`FUN_66d2bb10`, TVE_COLLAPSE).

---

## 9. Keyboard and mouse

- Main loop (mastercam.dll): 0x802b → `NWDViewer::KeyDown` → `SendMessage(VCR, WM_KEYDOWN)`; 0x802c KeyUp → WM_KEYUP;
  0x802d/0x802e → WM_SYSKEYDOWN/UP (backplot.dll @66d124e0, @66d12560, @66d131c0, @66d13240).
- VCR WM_KEYDOWN `FUN_66d1fb50` → slot 113 `FUN_66d1e700(key)` and `PreTranslateMessage` (`FUN_66d039c0`, WM_CHAR,
  `toupper`): keys are the `&` accelerator letters parsed from the button captions (`FUN_66d1ed30`, ctor defaults identical):
  **S** = Step forward, **B** = Step back, **N** = Next stop, **P** = Previous stop, **R** = Play (toggles run/stop).
- **Shift + `/`** (VK 0xBF) → context help for the dialog with focus (`get_dialog_help(0xb, IDD)`).
- `NWDViewer::OverrideCommand` @66d12a00 always returns false (no command overrides). `ReadNCI` is empty.
- Mouse in graphics: 0x8030 down → `MouseDown` (@66d125f0, arms pick); 0x8031 up → `MouseUp` → `FUN_66d7b3a0` →
  `FUN_66d7bb00`: picks the motion under the cursor ray and **moves the current position there** (`FUN_66d841b0` → `FUN_66d62650`);
  0x8032 move → AutoCursor update (`update_AC_point`) and `MouseMove` (@66d126e0, drag distance test; threshold NOT DECODED);
  0x8036 right click → posts WM_CONTEXTMENU to the graphics view.

## 10. Exit

- Loop (`backplot` @1800f8bb0, `RunPump(0x477d,…)`): mask bits from the table at 1805f7670 (0x8028→1, 0x802a→4, …).
  0x802a makes `RunPump` return 1 → loop ends. 0x8028 also breaks. 0x8015 + `OverrideCommand` false → handled by Mastercam.
- Producers of 0x802a: 16000 OK (`FUN_66d18130`), 14000 close/cancel (`FUN_66d15bb0`). Loop also ends when the viewer
  becomes inactive (`viewer+0xc8 = 0`, e.g. 14000 DestroyWindow override `FUN_66d15a10` → `NWDViewer::Destroy`).
- `Destroy` @66d11730: destroys the 14000/16000 dialogs, facade and GL view; restores `repaint_toolpath` and
  `graphics_support` and saved machine-group data (`FUN_66d14db0`); pops the GL attrib stacks pushed in Construct.
- On GeometryModel destruction the display toggles are saved to the registry (§4.2); ModelVisualOptions destruction writes
  speed/colours back to the mccore globals (§2.3, §4.3).
- `NWDViewer::GetCycleTime` @66d12240 (used for cycle time without UI): builds a facade if none, loads, returns
  `FUN_66d7cd90(facade)` (body not inspected).

## 11. NOT DECODED summary (next lead)
(Updated after §12; items resolved there are removed. 2026-10-15: pointers to §13 added in place; current list in §13.9.)
- Colour of feed motions when "Display with color codes" is off comes from the path record colour (`path+0x48`, §12.3); who writes that colour in backplotbnci.dll — next: writers of `+0x48` on the path records / `bp_color_loop_on_tc` users.
- CW/CCW arc colours (`bp_cwarc_color`/`bp_ccwarc_color`, ModelVisualOptions +0x2c/+0x30): no reader found apart from load/save/compare; the arc-type attribute handler `FUN_66da4920` uses the linear colour +0x28 (§12.3). Which pipe slot handles which entity type is inferred, not proven. (§13.2: still no reader; the Options dialog edits them.)
- Endpoint marker size/shape (RenderStyle types 0/1/2 meaning) — `FUN_66d32240`, `FUN_66d32030`. → decoded §13.3.
- Shaded vs wireframe tool; meaning of `mill_tool_appearance` 0/1/2 (`FUN_66d78a00`); display flag byte 1. → appearance decoded §13.4 (Plain/Fluted draw identically); display flag byte 1 still NOT DECODED.
- Whether Step forward/back visits each sub-motion of an expanded drill cycle (position struct has an `EntityMotion` field; mapping in `FUN_66d5c420`/`FUN_66d5beb0`).
- Drill pages 4004/4005 and Mill Generic Info 4010: no code references found in backplot.dll or backplotbnci.dll (next: other DLLs for 0xfa4/0xfa5/0xfaa); E_nci_mill_80/82/83 expansion; peck/chip-break motions are not generated by `FUN_67dc9540`.
- 14000 dialog x/y placement and reuse of the saved size — `MCDialog` (mastercam.dll), `DAT_66e1d7b0/b4` readers.
- `NWDViewer::ResizeSlider` body; `COpMgrTree::BackplotSelected`.
- Options dialog (`BackplotOptions`, mastercam.dll) contents; the config **reader** (parser of `NNNNN. label value` lines) — only the writer and the defaults were decoded (§12.1). → Options dialog decoded §13.1; key→global setter `FUN_1801b7980` found (§13.1), line parser still NOT DECODED.
- Name/meaning of the mccore flag at 0x1813eb30c that selects the 25.0/1.0 vs 1.0/0.05 defaults (consistent with metric, not proven).
- Coolant combo; arc/helix negative-feed time branches. → coolant combo is hidden + disabled (§13.7); negative-feed branches still NOT DECODED.
- The `bp_toggles`/`bpw_toggles` config value (19033/20016) is written by mccore but not read by backplot.dll (which uses the registry `BackplotToggles`, §4.2); any other reader NOT DECODED.

## 12. Follow-up decode

### 12.1 Config file `[BACKPLOT]` / `[WIRE BACKPLOT]` ↔ mccore globals
Source: mccore.dll. Writer functions **`FUN_1801d19d0`** (section string 479 "Backplot", checked by `FUN_1801d0ca0(…,0x1df)`) and
**`FUN_1801d1f80`** (string 481 "Wire Backplot", `0x1e1`). Each item is one call `item(table, key, …)`; the value passed is the
current global, the label is a resource id: for the "colour" form the label is a **string** id (r8), otherwise r9 is a **control id**
whose caption (mccoreres dialogs 322 = mill page, 324 = wire page) gives the label; the file adds `?`.
Item writers and their formats (format strings in mccore .rdata):
`FUN_1801d0880` / `FUN_1801d0940` = `"%d. %s %c\n"` (y/n), `FUN_1801d0ec0` and colour form `FUN_1801d0d20` = `"%d. %s %d\n"`,
`FUN_1801d0720` = `"%d. %s %lu\n"`, `FUN_1801d14d0` / `FUN_1801d13c0` = `"%d. %s %.*f\n"` (13c0 has a second label id on the stack).
Factory defaults: **`FUN_1803ba450`** (mill) and **`FUN_1803ba5c0`** (wire), which assign constants to the same globals.
Two defaults depend on the byte at mccore 0x1813eb30c (`U`, not exported; values for U≠0 / U=0 shown).
Global addresses are mccore exports (`exports/mccore.dll.tsv`). Labels below are as written in the user's file
(`CONFIG/mcamxm.config`), which matches the control captions except where the wire page uses page 324 captions.

| Key | Label (file) | Global | Writer type | Default (`FUN_1803ba450/5c0`) |
|---|---|---|---|---|
| 19000 | Appearance | `mill_tool_appearance` (short) | int | 2 |
| 19001 | Material | `bp_tool_material_on` | y/n | n |
| 19002 | Color | `bp_tool_color` | int | 15 |
| 19003 | Material | `bp_tool_material` (short) | int | 0 |
| 19004 | As defined | `bp_tool_as_defined` | y/n | y |
| 19005 | Spin tool | `bp_spin_tool` | y/n | n |
| 19006 | Material | `bp_holder_material_on` | y/n | n |
| 19007 | Color | `bp_holder_color` | int | 12 |
| 19008 | Material | `bp_holder_material` (short) | int | 0 |
| 19010 | By operation | `bp_color_loop_on_tc` | y/n | y |
| 19011 | Use Verify color | `bp_verify_use_motion_color` | y/n | n |
| 19013 | Rapid color (str 1443) | `bp_rapid_color` | colour | 14 |
| 19014 | Linear feed color (1447) | `bp_linear_color` | colour | 159 |
| 19015 | CW Arc feed color (1451) | `bp_cwarc_color` | colour | 159 |
| 19016 | CCW Arc feed color (1455) | `bp_ccwarc_color` | colour | 159 |
| 19017 | Verify color (1459) | `bp_verify_color` | colour | 138 |
| 19018 | 4-5 Axis vector color (1461) | `bp_5axisvector_color` | colour | 11 |
| 19019 | 4-5 Axis tool vectors (str 27962) | `bp_5axisvector_on` | y/n (`FUN_1801d0940`) | n |
| 19020 | Connect Bottom | `bp_5axisvector_check1` | y/n | y |
| 19021 | Connect Top | `bp_5axisvector_check2` | y/n | n |
| 19022 | Shade | `bp_5axisvector_check3` | y/n | n |
| 19023 | Length | `bp_5axisvector_length_cb` | y/n | y |
| 19024 | Length | `bp_5axisvector_length` (double) | double | U≠0: 25.0 / U=0: 1.0 |
| 19025 | Fast steps/sec | `bp_fast_steps_per_second` | int | 10 |
| 19026 | Operation change | `bp_cleanup_scrn_on_op_change` | y/n | n |
| 19027 | Tool change | `bp_cleanup_scrn_on_tl_change` | y/n | n |
| 19028 | Fit Toolpath | `bp_fit_toolpath` | y/n | n |
| 19029 | Simulate Cutter Compensation | `bp_simulate_cutter_comp` | y/n | y |
| 19030 | Show Mill / Router Home Position | `bp_draw_to_from_homepos` | y/n | n |
| 19031 | Simulate Axis Substitution | `bp_simul_caxis` | y/n | y |
| 19032 | Simulate Rotary Axis | `bp_simul_rot` | y/n | n |
| 19033 | Toggles (str 8968) | `bp_toggles` (long) | `%lu` | 0 |
| 19034 | Interpolate | `bp_interpolate_steps` | y/n | n |
| 19035 | Step increment | `bp_interpolate_distance` | double | U≠0: 1.0 / U=0: 0.05 |
| 19036 | Speed | `bp_speed_pos` | int | **60** |
| 19037 | Show Lathe Home Position | `bp_draw_to_from_homepos_lathe` | y/n | y |
| 19038 | Show C-Axis tools in top orientation | `bp_show_caxis_top` | y/n | n |
| 19039 | Opacity - Active | `bp_opacity_active` | double (sub-label 3560) | 0.5 |
| 19040 | Opacity - Inactive | `bp_opacity_inactive` | double (sub-label 3562) | 0.5 |
| 20001 | By pass | `bpw_color_loop_on_pass` | int | 0 |
| 20003 | Rapid color | `bpw_rapid_color` | colour | 14 |
| 20004–20006 | XY Linear / CW / CCW color | `bpw_xy_linear/cwarc/ccwarc_color` | colour | 11, 11, 11 |
| 20007–20009 | UV Linear / CW / CCW color | `bpw_uv_linear/cwarc/ccwarc_color` | colour | 13, 13, 13 |
| 20010 | Fast steps/sec | `bpw_fast_steps_per_second` | int | 10 |
| 20011 | Each pass | `bpw_cleanup_scrn_on_pass` | y/n | n |
| 20012 | Each operation | `bpw_cleanup_scrn_on_thrd` | y/n | n |
| 20013 | Fit Wirepath | `bpw_fit_wirepath` | y/n | n |
| 20014 | Simulate Wire Compensation | `bpw_simulate_wire_comp` | y/n | y |
| 20015 | Show move from Start position | `bpw_draw_to_from_startpos` | y/n | y |
| 20016 | Toggles | `bpw_toggles` (long) | `%lu` | 0 |
| 20017 | Endpoints (control 2982) | **`bpw_interpolate_steps`** (as coded) | y/n | n |
| 20018 | Step increment | `bpw_interpolate_distance` | double | U≠0: 1.0 / U=0: 0.05 |
| 20019 | Speed | `bpw_speed_pos` | int | 60 |
| 20020 | Show wire comp vectors | `bpw_show_comp_vectors` | y/n | n |
| 20021 | Comp Vector color | `bpw_compvector_color` | colour | 26 |
| 20022 | Display UV motion | `bpw_display_uv_motion` | y/n | y |
| 20023 | Display XY-UV connectors | `bpw_display_xyuv_connectors` | y/n | y |
| 20024 | Wire color | `bpw_wire_color` | colour | 15 |
| 20025 | Appearance | `bpw_guide_appearance` (short) | int | 2 |
| 20026 | Material | `bpw_guide_material_on` | y/n | n |
| 20027 | Guide color | `bpw_guide_color` | colour | 12 |
| 20028 | Material | `bpw_guide_material` (short) | int | 0 |
| 20029 | Opacity - Active | `bpw_opacity_active` | double | 0.5 |
| 20030 | Offset path color | `bpw_offset_color` | colour | 86 |
| 20031 | XY-UV connector color | `bpw_connector_color` | colour | 7 |
Also set by the default functions but not written to the file: `bp_color_loop` = 0, `bp_loop_color` = 11,
`bp_5axisvector_check4` untouched, `bp_simul_rot_axis` = 2, `bpw_color_loop` = 0, `bpw_loop_color` = 11.
Colours are indices into Mastercam's 256-entry palette (`Wd_colors`, §4.3). Note: the backplot.dll ModelVisualOptions ctor
values in §4.3 (e.g. speed 50, fast steps 1000) are the **static initial values** of the globals, overridden by these
defaults/the config file before use.

### 12.2 `model+0xc48` (`u` in the run-speed formula, §2.4)
Written only by **`FUN_66d77a00`** (called from `FUN_66d78920`, which runs before every view draw in `FUN_66d78280` and the
tool draw `FUN_66d78a00`): it un-projects window points (0,0,0) and (1,0,0) through the GL layer's slot 19
(`FUN_66d307d0` = `gluUnProject` with the current modelview/projection/viewport) and stores their 3‑D distance in
`view+0x48` and `model+0xc48`. ⇒ **`u` = model-space length of one screen pixel at the current zoom.**
So in §2.4: minimum step = 5 px per second of elapsed time, cap = 50 000 px per update, upper bound `(dt/5)·L`.

### 12.3 Drawing: colours, rapids, endpoints, Run vs Trace, drawn portion
Attribute pipe = `NWD::VisualAttributeInserterPipe` (ctor `FUN_66da4400`, vtable 66de2e40); `param_1[0x14]` =
ModelVisualOptions, `param_1[0x15]` = display flags (§4.1).
- **Motion classification** (`FUN_66da6320`, from the `NWD::RapidFlag` attribute the model emits before a motion):
  state = 2 if RapidFlag+8, else 4 if +9, else 3 if +10, else 5 if +0xb, else 1; no RapidFlag ⇒ state 0 (feed).
  The state is reset to 0 after each motion. (`FUN_66da6880`, `FUN_66da4920`: states 2/3 are tied to
  `bp_draw_to_from_homepos` (+0x98) and 4/5 to the lathe flag (+0x99), i.e. moves to/from home.)
- **Rapids hidden** (`FUN_66da6880`): if state ≠ 0 and display flag 4 ("Display rapid moves") = 0 → `RenderSuppress(1)` for
  that motion. Home moves (state 2 without +0x98, state 4 without +0x99) are always suppressed.
  Also suppressed: motions whose index is outside the window set by slot 8 (`FUN_66d32980`, window = the range being drawn,
  `FUN_66d68590`).
- **Colours** (`FUN_66da4750`, `FUN_66da4920`; B/W mode `ModelVisualOptions+0x9c == 1` → 0):
  - rapid (state ≠ 0): always `bp_rapid_color` (+0x24), whether colour codes are on or off;
  - feed (state 0) with "Display with color codes" **on**: `bp_linear_color` (+0x28) (both the line and the arc handler);
  - feed with colour codes **off**: no colour change ⇒ the path is drawn in the **block colour** set by the `NWD::BlockPrep`
    attribute (`FUN_66da6320`): 0xc0c0c0 in B/W, else the path record colour `FUN_66d3bcf0(rec)` = `rec+0x48`, or
    `FUN_66d86e20` (`+0x88` index ≥ 0 → that record's colour, else 0x808080); 0xffffff if neither exists.
  - `FUN_66da4ba0` (vector/point handler) uses `bp_5axisvector_color` (+0x34) for feed with colour codes on, rapid colour otherwise, width 2.
  - CW/CCW arc colours: see §11.
- **Endpoints** (flag 2): at the end of each non-suppressed motion `FUN_66da6f10` (pipe slot 10) emits a point entity at the
  motion end point (`FUN_66d32030`). Style: BlockPrep emits RenderStyles {1.0, colour, type 1}, {1.0, colour, type 2},
  {2.0, 0xffffff, type 0}; mapping of these to the point marker NOT DECODED → §13.3 (type 0 = points ⇒ white point, size 2.0).
- **Run vs Trace — drawn portion.** The drawn ranges are the model's dynamic visible ranges (`FUN_66d5eb60`): visible ranges
  (§8 selection, 16006) ∩ answers of every view to `GMMGetDynamicVisibility` (∩ by `FUN_66d16c90`):
  - VCR (`FUN_66d205f0`): display flag 0xd = 0 (**Trace**) → [0, motion count) = **whole path**;
    flag 0xd ≠ 0 (**Run**) → [0, current motion + 1) = **path up to and including the current motion**
    (start value `DAT_66e1d87c` is .bss with no writer ⇒ 0).
  - ToolpathControl (`FUN_66d19a40`): the "Restrict drawing" list (16010/16011, §4.5), [0, count) when not restricted.
  - GraphicalGeometryView (`FUN_66d7b5d0` → `FUN_66d77b70`): screen cleanup on operation/tool change (mill) or
    pass/thread (wire) limits the range to the current operation/tool/pass; [0, count) when those options are off.

### 12.4 Drill cycles (NCI 81) and pages 4004/4005
- backplotbnci.dll has entity classes `NWD::E_nci_mill_80`, `_81`, `_82`, `_83` (vtables 67e44390, 67e44890, 67e44b38, 67e452b8).
  `E_nci_mill_81` (ctor `FUN_67dc9250`) is expanded by **`FUN_67dc9540`** (only when entity+0xd0 is set) into a list of
  ordinary motion entities (`E_nci_mill_0` rapid via `FUN_67dce6a0`, `E_nci_mill_1` linear feed via `FUN_67dce4f0`):
  1. rapid to the start point S = (+0x80, +0x88, +0x90);
  2. if |(+0x60) − (+0x68)| > `tol`: rapid to R = S + unit(B − S)·((+0x60) − (+0x68));
  3. feed (feedrate +0x38) to the hole point B = (+0x18, +0x20, +0x28);
  4. back to S: **feed** if the short at +0x10 == 3, otherwise **rapid**.
  No peck/chip-break moves are generated by this function.
- The drill entity's details-page slot (vtbl+0x18, `FUN_67dca270`) expands and forwards to the sub-motion selected by the
  position's sub-index (`FUN_67dceee0`, `param+0x24`) ⇒ the Details tab shows the **Rapid (4000) / Feed (4001)** page of
  that sub-motion. Info/cycle time (vtbl+0x158, `FUN_67dca1e0` → `FUN_67dcee70`) sums the sub-motions' rapid and feed time.
- Pages 4004 "Drill Start" / 4005 "Drill": still NOT DECODED (no reference to ids 0xfa4/0xfa5 in either DLL).

## 13. Options dialog, colours, tool appearance, rapid time (2026-10-15 decode)

Sources added for this section: **mccore.dll** (Ghidra `~/mastercam-re/ghidra_core` / `mccore`, image base 0x180000000;
addresses `1807xxxxx` / `1801xxxxx` below are mccore unless stated), mccoreres.dll dialogs (parsed with `tools/pe_dialogs.py`
plus the control style word), backplot.dll / backplotbnci.dll full decompiles, mcmachinedef.dll (`~/mastercam-re/ghidra_mdef`),
mastercam.dll static data (`Wd_colors`).

### 13.1 Options dialog (31007 → `BackplotOptions`)
**Correction to §4.5:** `BackplotOptions(CWnd*, char)` is exported by **mccore.dll** (@1801700d0, `exports/mccore.dll.tsv`),
not mastercam.dll. Body (`BackplotOptions` @1801700d0):
1. Switch the MFC resource handle to `GetCoreResourceHandle()` (mccoreres.dll) for the call, restore it on return.
2. Parent NULL → `get_MainFrame()`.
3. 2nd arg (`isWireOpAtPosition`, §4.5) == 0 → **mill dialog `CBackplotDlg`, template 1228 (0x4cc)** (ctor `FUN_180703c80`,
   vtable 180da3ab0); ≠ 0 → **wire dialog `CBackplotWDlg`, template 1229 (0x4cd)** (ctor `FUN_180703fc0`, vtable 180da3e38).
   Both `MCDialog(id, parent, 1, false, 1)`, run with `CBCGPDialog::DoModal`.
4. Returns 1 iff DoModal == IDOK (then backplot.dll reloads ModelVisualOptions, §4.5); else 0.

Class map (vtable slots as in §0):
| | mill `CBackplotDlg` | wire `CBackplotWDlg` |
|---|---|---|
| DoDataExchange (66) | `FUN_180704440` | `FUN_180704930` |
| OnInitDialog (95) | `FUN_180705ab0` | `FUN_180706200` |
| IDOK button (message map id 1) | `FUN_1807051d0` | `FUN_180705440` |
| virtual OnOK (97) | thunk → `FUN_1807082c0` (UpdateData + refresh only) | thunk → `FUN_1807082e0` |
| PreTranslateMessage (69) | `FUN_1807070c0` | `FUN_1807071b0` |
| enable/refresh ("update controls") | `FUN_180707280` | `FUN_180707b00` |
| message map (static part) | 181308470 (runtime tail `FUN_180704e10`) | 181308bd0 (`FUN_180704ef0`) |
| WM_DRAWITEM (colour swatches) | `FUN_1807055f0` | `FUN_180705780` |
| WM_VSCROLL (opacity spin) | `FUN_180706db0` | `FUN_180706e90` |

Message maps (parsed from the AFX_MSGMAP_ENTRY arrays): every check box / radio (BN_CLICKED), every edit (EN_KILLFOCUS 0x200)
and both material combos (CBN_SELCHANGE 1) go to `UpdateData(TRUE)` + update-controls (`FUN_1807082c0` → `FUN_180707280`; wire
`FUN_1807082e0` → `FUN_180707b00`). Exceptions are named in the tables (colour pickers, Color/Material radios, opacity edits, OK).
The update-controls function ends by repainting the swatches (`FUN_180708130` / `FUN_1807081e0`, InvalidateRect).

Common control behaviour (all from the decompile):
- **Colour edits** are `MCByte` (`DDX_ProcessEditByte` @1804db710): the text is evaluated with `EvalToDouble` (expressions allowed);
  accepted iff `min ≤ v ≤ max` with min/max bytes at +0xdd/+0xde, which OnInitDialog sets to **0 / 255** (`*(short*)(+0xdd) = 0xff00`).
  Otherwise a ClueWnd tooltip `"%u to %u"` (`FUN_1804dcdc0`) is shown and the old value is put back. The value is the palette index.
- **Swatch** (owner-draw button next to each colour edit): `FillRect(Wd_colors[index])`; when the swatch control is disabled it is
  painted with index **7** (192,192,192) (`FUN_1807055f0`, `bVar1 = 7`).
- **Select** buttons (`UIBitmapButton`, bitmap **0x61d2** from mccoreres, transparent colour 0xC0C0C0, OnInitDialog):
  `sel_color_dialog(dlg, &index, 1)`; if it returns true the index is stored; then update-controls + `UpdateData(FALSE)`.
- **Color / Material radios**: stubs that store 0 / 1 and jump to update-controls (disassembly e.g. 180706ca0:
  `mov dword [rcx+0x1a97c],0; jmp 180707280`).
- **Material combos** are filled by `FUN_180704fd0`: `gl_InitMaterialList()`, then CB_ADDSTRING of each `gl_mat_list[i]`
  (0x44-byte entries, `i < gl_mat_index`). The list contents (names) are NOT DECODED (`gl_InitMaterialList` is an import, not in mccore).
- **Opacity**: edit = `UIEditReal` (field +0xc0c = 2, fields +0xbf0/+0xbf8 = range: active 0.0..1.0, mill inactive 0.01..1.0) plus an
  up-down with `UDM_SETRANGE(0, MAKELONG(100, 1))` (range 1..100). Init: `n = round(global/0.01)` (mill; wire truncates), n outside
  1..100 → **50**; edit shows n·0.01, spin pos n. Edit commit (EN_KILLFOCUS, or Enter via PreTranslateMessage) `FUN_180706820` /
  `FUN_180706a00` / wire `FUN_180706910`: if |edit − global| > 0.001: n = round(edit/0.01), kept only if 1 ≤ n ≤ 100; **the global
  is written immediately** (`bp_opacity_active = n·0.01`). Spin (WM_VSCROLL SB_THUMBPOSITION/SB_THUMBTRACK, pos ≤ 100) also writes
  the global immediately. ⇒ as coded, **Cancel does not undo an opacity change**.
- **Enter key** (PreTranslateMessage): in 358 → active-opacity commit, in 360 → inactive commit, any other control →
  `UpdateData(TRUE)` + swatch repaint; Enter never closes the dialog.
- `MCInt` fast-steps edit: fields +0xbf0/+0xbf4 = **1 / 100000** (OnInitDialog); `MCReal` edits: +0xbf0 = interpolate distance
  `tol`·10, tool radius 0.0, vector length `tol`. Validation itself is `UIEditInt::Validate` / `UIEditReal::Validate` (uictrls.dll):
  that these fields are min/max is inferred from the pairs (1,100000), (0.0,1.0) — the Validate bodies are NOT DECODED.

OnInitDialog extra rules (mill `FUN_180705ab0`): if `bp_cleanup_scrn_on_op_change` then `bp_cleanup_scrn_on_tl_change = 1`
(written to the global); `bp_tool_material` / `bp_holder_material` < 0 or ≥ combo count → set to 0 (global). Wire `FUN_180706200`:
`bpw_color_loop_on_pass > 1` → 0; `bpw_guide_material` clamped like the mill materials.

Update-controls rules (mill `FUN_180707280`) are in the table column "Binding / rule"; summary: Shaded radio disabled unless
`graphics_support == 1` (a stored Shaded becomes Plain); Fluted enabled only with Profile = As defined; Material radios and
material combos only when Shaded (otherwise both `*_material_on` forced to 0); Spin tool disabled when Shaded; Material ⇒ colour
edit / swatch / Select disabled, Color ⇒ combo disabled; "Operation change" checked ⇒ "Tool change" disabled + forced on;
Step increment only with Interpolate; Verify colour controls only with "Use Verify color"; all vector controls only with
"Display vectors", vector length also needs "Length"; "Cutter comp in control" enabled iff `bp_cc_in_control_enabled`, radius
iff that and checked. Wire (`FUN_180707b00`): Shaded guide disabled unless `graphics_support == 1`; guide Material radio and
combo only when Shaded; comp-vector colour controls only with "Show wire comp vectors".

OK (mill `FUN_1807051d0`): `UIDialog::OnOK`, `UpdateData(TRUE)`, then writes **every** global listed in the table (radios with
`== 1`, colours as byte indices, materials = CB_GETCURSEL, `bp_verify_use_motion_color = (3351 unchecked)`). Wire `FUN_180705440`
likewise (`bpw_guide_appearance = radio ≠ 0 ? 2 : 0`). Nothing is written on Cancel except the opacity rule above.
The dialog has **no Speed slider** (speed lives on the VCR bar, §2.3; the separate Configuration page 322 has one, range 1..100,
`FUN_18017ab60`). `bpw_connector_color` (§4.3 +0x74) has no control in 1229.

Bonus (config reader, §11): **`FUN_1801b7980`** is the mill key→global setter used when reading the config file:
`switch(key)` 19000 → `mill_tool_appearance`, 19001 (0x4a39) → `bp_tool_material_on`, … 19040 (0x4a60) → `bp_opacity_inactive`
(same key↔global pairs as the §12.1 table). The line parser that calls it is NOT DECODED.

Control tables (templates from mccoreres.dll; type from the style word: autocheck = BS_AUTOCHECKBOX, autoradio =
BS_AUTORADIOBUTTON, ownerdraw = BS_OWNERDRAW; "hidden" = no WS_VISIBLE). Offsets are dialog-object offsets used by DDX.
Dialog 1228 "Backplot Options" 413×369 DU; dialog 1229 "Backplot Options" 413×363 DU.

**Dialog 1228**

| Id | Type | Caption | x,y,w,h (DU) | Binding / rule |
|---|---|---|---|---|
| 3395 | groupbox | General Settings | 7,7,134,331 |  |
| 3415 | groupbox | Step mode | 13,17,122,67 |  |
| 3416 | Static | Fast steps/sec: | 19,28,60,14 |  |
| 3414 | Edit |  | 85,28,40,14 | MCInt +0x196c0 ↔ `bp_fast_steps_per_second`; bounds fields +0xbf0/+0xbf4 = 1 / 100000 |
| 2982 | autoradio | Endpoints | 20,43,55,10 | radio group (DDX_Radio) +0x1a98c: 0 = Endpoints, 1 = Interpolate ↔ `bp_interpolate_steps` (OK: `== 1`) |
| 2984 | autoradio | Interpolate | 20,55,55,10 | (2nd radio of the 2982 group) |
| 2978 | Static | Step increment: | 19,65,60,14 | enabled iff Interpolate |
| 2980 | Edit |  | 85,65,40,14 | MCReal +0x17388 ↔ `bp_interpolate_distance`; lower-bound field +0xbf0 = `tol`·10; enabled iff Interpolate |
| 3340 | groupbox | Cleanup screen on | 13,88,122,42 |  |
| 3354 | autocheck | Operation change | 19,102,109,10 | +0x1a938 ↔ `bp_cleanup_scrn_on_op_change`; checked ⇒ 3355 disabled and forced checked |
| 3355 | autocheck | Tool change | 19,114,109,10 | +0x1a93c ↔ `bp_cleanup_scrn_on_tl_change` |
| 3358 | autocheck | Fit Toolpath | 13,221,122,10 | +0x1a940 ↔ `bp_fit_toolpath` |
| 3399 | autocheck | Show Lathe Home Position | 13,233,122,10 | +0x1a94c ↔ `bp_draw_to_from_homepos_lathe` |
| 3042 | autocheck | Show C-Axis tools in top orientation | 13,245,122,10 | +0x1a974 ↔ `bp_show_caxis_top` |
| 3397 | autocheck | Show Mill / Router Home Position | 13,257,122,10 | +0x1a948 ↔ `bp_draw_to_from_homepos` |
| 3404 | autocheck | Simulate Cutter Compensation | 13,269,122,10 | +0x1a944 ↔ `bp_simulate_cutter_comp` |
| 3341 | autocheck | Cutter comp in control | 13,281,122,10 | +0x18180 ↔ `bp_cc_in_control_active`; enabled iff `bp_cc_in_control_enabled` (set by backplot.dll `FUN_66d19c90`, §4.5) |
| 10285 | Static | Tool Radius: | 24,293,42,12 | enabled iff 3341 enabled and checked |
| 2981 | Edit |  | 69,292,40,14 | MCReal +0x18188 ↔ `bp_cc_in_control_radius`; lower bound 0.0; enabled iff 3341 enabled and checked |
| 3403 | autocheck | Simulate Axis Substitution | 13,309,122,10 | +0x1a950 ↔ `bp_simul_caxis` |
| 3405 | autocheck | Simulate Rotary Axis | 13,321,122,10 | +0x1a954 ↔ `bp_simul_rot` |
| 3318 | groupbox | Appearance | 147,7,259,331 |  |
| 3431 | groupbox | Tool | 153,17,247,78 |  |
| 3424 | autoradio | Plain | 159,28,44,10 | radio group +0x1a978 ↔ `mill_tool_appearance`: 0 = Plain, 1 = Fluted, 2 = Shaded |
| 3426 | autoradio | Fluted | 159,41,44,10 | (value 1) enabled iff Profile = As defined (+0x1a980 ≠ 0) |
| 3429 | autoradio | Shaded | 159,54,44,10 | (value 2) enabled iff `graphics_support == 1`, else disabled and value 2 → 0 |
| 3421 | autoradio | Color | 210,28,40,14 | radio group +0x1a97c ↔ `bp_tool_material_on`: 0 = Color, 1 = Material (stub 0x180706ca0 / 0x180706cb0 set 0/1) |
| 3422 | autoradio | Material | 210,54,40,12 | (value 1) enabled only when Shaded |
| 3419 | Edit |  | 254,28,20,14 | MCByte value +0x19144 ↔ `bp_tool_color` (0..255); disabled when Material |
| 3420 | ownerdraw |  | 277,28,20,14 | swatch (WM_DRAWITEM) = `Wd_colors[tool colour]`; disabled when Material |
| 3417 | push, UIBitmapButton bitmap 0x61d2 | Select | 300,28,16,14 | `FUN_180706c50`: `sel_color_dialog(dlg,&idx,1)` → tool colour |
| 3433 | ComboBox |  | 254,54,63,101 | material list; CB_SETCURSEL `bp_tool_material` (clamped to 0 if <0 or ≥ count); OK: CB_GETCURSEL → `bp_tool_material`; enabled only when Shaded and Material |
| 3434 | groupbox | Profile | 335,25,59,41 |  |
| 3436 | autoradio | Auto | 340,36,50,10 | radio group +0x1a980 ↔ `bp_tool_as_defined`: 0 = Auto, 1 = As defined |
| 3437 | autoradio | As defined | 340,50,50,10 |  |
| 3438 | autocheck | Spin tool | 340,73,50,14 | +0x1a958 ↔ `bp_spin_tool`; disabled when Shaded |
| 3367 | groupbox | Holder | 153,99,247,32 |  |
| 3362 | autoradio | Color | 159,111,42,14 | radio group +0x1a984 ↔ `bp_holder_material_on`: 0 = Color, 1 = Material (stubs 0x180705a90 / 0x180705aa0) |
| 3364 | autoradio | Material | 284,111,40,14 | (value 1) enabled only when Shaded |
| 3361 | Edit |  | 207,111,20,14 | MCByte value +0x1905c ↔ `bp_holder_color`; disabled when Material |
| 3366 | ownerdraw |  | 230,111,20,14 | swatch = `Wd_colors[holder colour]` |
| 3360 | push, UIBitmapButton bitmap 0x61d2 | Select | 253,111,16,14 | `FUN_180705a40`: colour picker → holder colour |
| 3368 | ComboBox |  | 328,111,64,101 | holder material list ↔ `bp_holder_material` (same rules as 3433) |
| 3559 | groupbox | Opacity | 153,136,247,29 |  |
| 3560 | Static | Active | 197,146,22,14 |  |
| 358 | Edit |  | 225,146,40,14 | UIEditReal +0x14aa0 (2 decimals field +0xc0c = 2; range fields 0.0 / 1.0) ↔ `bp_opacity_active` |
| 359 | updown |  | 265,146,10,14 | spin, UDM_SETRANGE lower 1 upper 100; UDM_SETPOS = opacity·100 |
| 3562 | Static | Inactive | 281,146,25,14 |  |
| 360 | Edit |  | 311,146,40,14 | UIEditReal +0x15818 (range fields 0.01 / 1.0) ↔ `bp_opacity_inactive` |
| 363 | updown |  | 351,146,10,14 | spin 1..100 for 360 |
| -1 | groupbox | Color Loop | 153,169,247,27 |  |
| 3376 | autoradio | By operation | 207,178,57,12 | radio group +0x1a988 ↔ `bp_color_loop_on_tc`: 0 = By operation, 1 = By tool change |
| 3378 | autoradio | By tool change | 284,178,70,12 |  |
| 3344 | groupbox | Colors | 153,200,247,79 |  |
| 3044 | Static (SS_BITMAP) | #1235 | 164,210,11,10 |  |
| 3393 | Static | Rapid | 180,213,21,14 |  |
| 3391 | Edit |  | 207,213,20,14 | MCByte value +0x1922c ↔ `bp_rapid_color` |
| 3392 | ownerdraw |  | 230,213,20,14 | swatch (rapid) |
| 3388 | push, UIBitmapButton bitmap 0x61d2 | Select | 253,213,16,14 | `FUN_180706bb0` picker (rapid) |
| 3487 | Static | Linear feed | 163,234,38,14 |  |
| 3483 | Edit |  | 207,234,20,14 | MCByte value +0x195cc ↔ `bp_linear_color` |
| 3485 | ownerdraw |  | 230,234,20,14 | swatch (linear) |
| 3481 | push, UIBitmapButton bitmap 0x61d2 | Select | 253,234,16,14 | `FUN_180706af0` picker (linear) |
| 3478 | Static | CW Arc feed | 280,213,43,14 |  |
| 3474 | Edit |  | 328,213,20,14 | MCByte value +0x194e4 ↔ `bp_cwarc_color` |
| 3476 | ownerdraw |  | 351,213,20,14 | swatch (CW) |
| 3472 | push, UIBitmapButton bitmap 0x61d2 | Select | 374,213,16,14 | `FUN_180705180` picker (CW) |
| 3470 | Static | CCW Arc feed | 275,234,48,14 |  |
| 3466 | Edit |  | 328,234,20,14 | MCByte value +0x193fc ↔ `bp_ccwarc_color` |
| 3468 | ownerdraw |  | 351,234,20,14 | swatch (CCW) |
| 3464 | push, UIBitmapButton bitmap 0x61d2 | Select | 374,234,16,14 | `FUN_1807050e0` picker (CCW) |
| 3351 | autocheck | Use Verify color | 207,257,68,14 | +0x1a95c = `!bp_verify_use_motion_color` (checked ⇔ use the Verify colour) |
| 3460 | Edit |  | 282,257,20,14 | MCByte value +0x19314 ↔ `bp_verify_color`; enabled iff 3351 checked |
| 3462 | ownerdraw |  | 305,257,20,14 | swatch (verify); enabled iff 3351 |
| 3458 | push, UIBitmapButton bitmap 0x61d2 | Select | 328,257,16,14 | `FUN_180706f30` picker (verify); enabled iff 3351 |
| 3326 | groupbox | 4-5 Axis tool vectors | 153,283,247,48 |  |
| 3320 | autocheck | Display vectors | 159,293,70,14 | +0x1a960 ↔ `bp_5axisvector_on`; enables 3324 3325 3322 3327 3328 3330 3331 3333 3335 3336 |
| 3324 | Edit |  | 159,310,20,14 | MCByte value +0x196b4 ↔ `bp_5axisvector_color` |
| 3325 | ownerdraw |  | 182,310,20,14 | swatch (vector) |
| 3322 | push, UIBitmapButton bitmap 0x61d2 | Select | 205,310,16,14 | `FUN_180705090` picker (vector) |
| 3327 | Static | Connect Bottom | 239,295,32,18 |  |
| 3328 | Static | Connect Top | 275,295,32,18 |  |
| 3330 | Static | Shade | 311,295,28,18 |  |
| 3331 | autocheck |  | 251,314,10,10 | +0x1a964 ↔ `bp_5axisvector_check1` (Connect Bottom) |
| 3333 | autocheck |  | 286,314,10,10 | +0x1a968 ↔ `bp_5axisvector_check2` (Connect Top) |
| 3335 | autocheck |  | 320,314,10,10 | +0x1a96c ↔ `bp_5axisvector_check3` (Shade) |
| 3336 | autocheck | Length | 351,298,40,10 | +0x1a970 ↔ `bp_5axisvector_length_cb` |
| 3338 | Edit |  | 351,310,40,14 | MCReal +0x16590 ↔ `bp_5axisvector_length`; lower bound `tol`; enabled iff 3320 and 3336 |
| 1 | push | OK | 290,344,36,18 | OK `FUN_1807051d0` |
| 2 | push | Cancel | 330,344,36,18 | Cancel (UIDialog::OnCancel) |
| 9 | push | Help | 370,344,36,18 | Help (MCDialog) |

**Dialog 1229**

| Id | Type | Caption | x,y,w,h (DU) | Binding / rule |
|---|---|---|---|---|
| 3395 | groupbox | General Settings | 7,7,134,327 |  |
| 3415 | groupbox | Step mode | 13,17,122,73 |  |
| 3416 | Static | Fast steps/sec: | 19,28,63,14 |  |
| 3414 | Edit |  | 85,28,40,14 | MCInt +0x1b678 ↔ `bpw_fast_steps_per_second`; 1 / 100000 |
| 2982 | autoradio | Endpoints | 20,45,111,10 | radio +0x1cc78: 0 = Endpoints, 1 = Interpolate ↔ `bpw_interpolate_steps` |
| 2984 | autoradio | Interpolate | 20,58,111,10 | (value 1) |
| 2978 | Static | Step increment: | 19,71,63,14 | enabled iff Interpolate |
| 2980 | Edit |  | 85,71,40,14 | MCReal +0x19da0 ↔ `bpw_interpolate_distance`; lower bound `tol`·10; enabled iff Interpolate |
| 3340 | groupbox | Cleanup screen on | 13,98,122,40 |  |
| 3354 | autocheck | Each pass | 19,110,112,10 | +0x1cc54 ↔ `bpw_cleanup_scrn_on_pass` |
| 3355 | autocheck | Each operation | 19,122,112,10 | +0x1cc58 ↔ `bpw_cleanup_scrn_on_thrd` (no forcing between the two) |
| 3342 | groupbox | UV Motion | 13,147,122,40 |  |
| 3363 | autocheck | Display UV motion | 19,160,112,10 | +0x1cc6c ↔ `bpw_display_uv_motion` |
| 3365 | autocheck | Display XY-UV connectors | 19,171,112,10 | +0x1cc70 ↔ `bpw_display_xyuv_connectors` |
| 3358 | autocheck | Fit Wirepath | 13,280,124,10 | +0x1cc5c ↔ `bpw_fit_wirepath` |
| 3401 | autocheck | Show wire comp vectors | 13,292,124,10 | +0x1cc68 ↔ `bpw_show_comp_vectors`; enables 3413 3411 3412 3410 |
| 3402 | autocheck (hidden) | Show move from Start position | 13,316,124,10 | +0x1cc64 ↔ `bpw_draw_to_from_startpos` — template style has **no WS_VISIBLE** (hidden) |
| 3404 | autocheck | Simulate Wire Compensation | 13,304,124,10 | +0x1cc60 ↔ `bpw_simulate_wire_comp` |
| 3318 | groupbox | Appearance | 147,7,259,327 |  |
| 3504 | groupbox | Wire | 153,17,247,32 |  |
| 3428 | Static | Color | 179,29,69,14 |  |
| 3505 | Edit |  | 253,29,20,14 | MCByte value +0x1b584 ↔ `bpw_wire_color` |
| 3506 | ownerdraw |  | 276,29,20,14 | swatch (wire) |
| 3507 | push, UIBitmapButton bitmap 0x61d2 | Select | 299,29,16,14 | `FUN_180706f80` picker (wire) |
| 3503 | groupbox | Wire Guide | 153,53,247,50 |  |
| 3508 | autoradio | Plain | 168,64,90,14 | radio +0x1cc7c: 0 = Plain, 1 = Shaded; OK: `bpw_guide_appearance = (v≠0) ? 2 : 0` |
| 3509 | autoradio | Shaded | 168,83,90,14 | (value 1) enabled iff `graphics_support == 1`, else disabled and value 1 → 0 |
| 3510 | autoradio | Color | 264,64,60,14 | radio +0x1cc80 ↔ `bpw_guide_material_on`: 0 = Color, 1 = Material (stubs 0x1807059e0 / 0x180705a10) |
| 3511 | autoradio | Material | 264,83,60,14 | (value 1) enabled iff Shaded |
| 3512 | Edit |  | 327,64,20,14 | MCByte value +0x1b66c ↔ `bpw_guide_color`; disabled when Material |
| 3513 | ownerdraw |  | 350,64,20,14 | swatch (guide) |
| 3514 | push, UIBitmapButton bitmap 0x61d2 | Select | 373,64,16,14 | `FUN_180705990` picker (guide) |
| 3515 | ComboBox |  | 327,83,63,101 | guide material list ↔ `bpw_guide_material`; enabled iff Shaded and Material |
| 3559 | groupbox | Opacity | 153,107,247,29 |  |
| 3560 | Static | Active | 161,117,87,14 |  |
| 358 | Edit |  | 253,117,40,12 | UIEditReal +0x19028 (range 0.0 / 1.0) ↔ `bpw_opacity_active` |
| 359 | updown |  | 296,117,10,12 | spin 1..100 |
| 25414 | groupbox | Color Loop | 153,140,247,27 |  |
| 3376 | autoradio | By pass | 199,149,80,12 | radio +0x1cc74 ↔ `bpw_color_loop_on_pass`: 0 = By pass, 1 = By operation (init: >1 → 0) |
| 3378 | autoradio | By operation | 285,149,111,12 |  |
| 3385 | groupbox | XY Motion | 153,171,247,56 |  |
| 3393 | Static | Rapid | 159,184,48,14 |  |
| 3391 | Edit |  | 210,184,20,14 | MCByte +0x1b014 ↔ `bpw_rapid_color` |
| 3392 | ownerdraw |  | 232,184,20,14 | swatch |
| 3388 | push, UIBitmapButton bitmap 0x61d2 | Select | 254,184,16,14 | `FUN_180706c00` picker |
| 3487 | Static | Linear feed | 159,205,48,14 |  |
| 3483 | Edit |  | 210,205,20,14 | MCByte +0x1b0fc ↔ `bpw_xy_linear_color` |
| 3485 | ownerdraw |  | 232,205,20,14 | swatch |
| 3481 | push, UIBitmapButton bitmap 0x61d2 | Select | 254,205,16,14 | `FUN_180707070` picker |
| 3478 | Static | CW Arc feed | 275,184,57,14 |  |
| 3474 | Edit |  | 336,184,20,14 | MCByte +0x1b1e4 ↔ `bpw_xy_cwarc_color` |
| 3476 | ownerdraw |  | 358,184,20,14 | swatch |
| 3472 | push, UIBitmapButton bitmap 0x61d2 | Select | 380,184,16,14 | `FUN_180707020` picker |
| 3470 | Static | CCW Arc feed | 275,205,57,14 |  |
| 3466 | Edit |  | 336,205,20,14 | MCByte +0x1b2cc ↔ `bpw_xy_ccwarc_color` |
| 3468 | ownerdraw |  | 358,205,20,14 | swatch |
| 3464 | push, UIBitmapButton bitmap 0x61d2 | Select | 380,205,16,14 | `FUN_180706fd0` picker |
| 3448 | groupbox | UV Motion | 153,232,247,56 |  |
| 3456 | Static | Linear feed | 157,266,50,14 |  |
| 3452 | Edit |  | 210,266,20,14 | MCByte +0x1ac74 ↔ `bpw_uv_linear_color` |
| 3454 | ownerdraw |  | 232,266,20,14 | swatch |
| 3450 | push, UIBitmapButton bitmap 0x61d2 | Select | 254,266,16,14 | `FUN_180706d60` picker |
| 3446 | Static | CW Arc feed | 275,245,57,14 |  |
| 3444 | Edit |  | 336,245,20,14 | MCByte +0x1b3b4 ↔ `bpw_uv_cwarc_color` |
| 3445 | ownerdraw |  | 358,245,20,14 | swatch |
| 3443 | push, UIBitmapButton bitmap 0x61d2 | Select | 380,245,16,14 | `FUN_180706d10` picker |
| 3442 | Static | CCW Arc feed | 275,266,57,14 |  |
| 3440 | Edit |  | 336,266,20,14 | MCByte +0x1b49c ↔ `bpw_uv_ccwarc_color` |
| 3441 | ownerdraw |  | 358,266,20,14 | swatch |
| 3439 | push, UIBitmapButton bitmap 0x61d2 | Select | 380,266,16,14 | `FUN_180706cc0` picker |
| -1 | groupbox | Offset path | 153,292,122,34 |  |
| 3417 | Static | Color | 156,305,50,14 |  |
| 3418 | Edit |  | 210,305,20,14 | MCByte +0x1ae44 ↔ `bpw_offset_color` |
| 3419 | ownerdraw |  | 232,305,20,14 | swatch (offset) |
| 3420 | push, UIBitmapButton bitmap 0x61d2 | Select | 254,305,16,14 | `FUN_180706b60` picker (offset) |
| -1 | groupbox | Comp Vectors | 280,292,120,34 |  |
| 3413 | Static | Color | 283,305,49,14 | enabled iff 3401 |
| 3411 | Edit |  | 336,305,20,14 | MCByte +0x1ad5c ↔ `bpw_compvector_color`; enabled iff 3401 |
| 3412 | ownerdraw |  | 358,305,20,14 | swatch; enabled iff 3401 |
| 3410 | push, UIBitmapButton bitmap 0x61d2 | Select | 380,305,16,14 | `FUN_180705130` picker; enabled iff 3401 |
| 1 | push | OK | 290,338,36,18 | OK `FUN_180705440` |
| 2 | push | Cancel | 330,338,36,18 | Cancel |
| 9 | push | Help | 370,338,36,18 | Help |

### 13.2 Motion colours per toggle, with RGB
Colour storage: ModelVisualOptions holds **COLORREF** values (`0x00BBGGRR`) copied from `Wd_colors[index]` (§4.3); the GL
layer converts with R = low byte (e.g. `FUN_66d31130`: `r = (c & 0xff)/255`, `g = (c>>8 & 0xff)/255`, `b = (c>>16 & 0xff)/255`).
`Wd_colors` = mastercam.dll export `?Wd_colors@@3V?$array@K$0BAA@@std@@A` @1808a8640, static data identical to
`formats/mastercam_palette.tsv`.

Rules (from §12.3, `FUN_66da4750` / `FUN_66da4920` / `FUN_66da4ba0` / `FUN_66da6320`), with the factory-default indices
(§12.1, `FUN_1803ba450` / `FUN_1803ba5c0`) resolved through the palette:
| Motion | "Display with color codes" (flag 0) **off** | flag 0 **on** | B/W (`in_black_white` = 1) |
|---|---|---|---|
| rapid (RapidFlag state ≠ 0) | `bp_rapid_color`, default idx 14 = RGB(255,255,0) | same | 0 (black) |
| linear feed | block colour (path record colour `rec+0x48`; else `FUN_66d86e20` → record colour or 0x808080 = RGB(128,128,128); 0xffffff when no record) | `bp_linear_color`, default idx 159 = RGB(0,128,255) | 0; block colour 0xc0c0c0 |
| arc feed | block colour | `bp_linear_color` (arc handler `FUN_66da4920` uses +0x28) | 0 |
| 4/5-axis vector (`FUN_66da4ba0`, width 2) | `bp_rapid_color` | `bp_5axisvector_color`, default idx 11 = RGB(0,255,255) | 0 |
- `bp_cwarc_color` / `bp_ccwarc_color` (default idx 159, RGB(0,128,255)): edited by the Options dialog (3474/3466) and saved, but no
  reader in backplot.dll / backplotbnci.dll apart from load/save/compare (§11 unchanged) ⇒ arcs use the linear colour.
- Other defaults: verify idx 138 = RGB(255,102,0); tool idx 15 = RGB(255,255,255); holder idx 12 = RGB(255,0,0);
  wire: rapid 14 = RGB(255,255,0), XY 11 = RGB(0,255,255), UV 13 = RGB(255,0,255), wire 15 = RGB(255,255,255),
  guide 12 = RGB(255,0,0), comp vector 26 = RGB(204,255,0), offset 86 = RGB(0,204,0), XY-UV connector 7 = RGB(192,192,192).
- "Display rapid moves" (flag 4) only hides rapids (`RenderSuppress`, §12.3); it does not change colours.

### 13.3 Endpoints (flag 2) — marker
Per path block the BlockPrep handler (`FUN_66da6320`) emits three `RenderStyle` attributes (layout: +8 float width,
+0xc colour, +0x10 type): **{1.0, block colour, type 1}**, **{1.0, block colour, type 2}**, **{2.0, 0xFFFFFF, type 0}**
(`local_48 = 0x40000000`, `local_44 = 0xffffff`). The style consumer **`FUN_66d982b0`** dispatches by type into the render
context's four batches (ctor `FUN_66d90fb0`: ctx+8 `PointList`, ctx+0xd0 `LineList`, ctx+0x190 `LineStripList`, ctx+0x250
`TriangleStripList`):
- type 0 → PointList: width (WidthRange → OpenGLLayer slot 33 `FUN_66d31590` = `glPointSize`) and colour;
- type 1 → LineStripList and LineList: colour + line width;
- type 2 → TriangleStripList: colour, alpha 1.0.
The endpoint entity emitted by `FUN_66da6f10` (§12.3) is a point ⇒ **endpoint marker = GL point of size 2.0 px, colour 0xFFFFFF
(white)** — the white is not replaced in B/W mode (only the block colour becomes 0xc0c0c0). Lines are width 1.0 in the block colour.
(A second branch draws the block's lines with width **3.0** when byte `+0xf2` of the pipe is set — set from a RenderStyle whose
width is −1.0 (its byte +0x17); who emits that style is NOT DECODED.)

### 13.4 Tool / holder appearance, colours, spin
`mill_tool_appearance` (Options 3424/3426/3429): **0 = Plain, 1 = Fluted, 2 = Shaded** (radio order, DDX_Radio into +0x1a978,
written back by `FUN_1807051d0`). backplot.dll derives ModelVisualOptions +0xc = (value < 2) "unlit", +0xe = (value == 1); wire guide
+0xd = (`bpw_guide_appearance` < 2) (`FUN_66d76180`). Factory default 2 = Shaded (§12.1).
- **Plain vs Fluted: no difference in drawing.** +0xe has no reader in backplot.dll other than the save function `FUN_66d766c0`, and
  `mill_tool_appearance` / `bp_tool_as_defined` have no reader in backplot.dll, backplotbnci.dll or mccore other than the dialogs,
  config writer/reader and defaults (mccore disassembly: refs at 18017ae61 (config page init), 18017c559/18017c6cb (config page),
  1801b79a6 (config reader), 1801d1a0f (config writer), 1803ba47b (defaults), 180705325/180705e39 (Options dialog)). The tool profile
  comes from the tool definition (backplotbnci `FUN_67d17570` / `FUN_67d18540` / `FUN_67d193e0`: `TlTool::GetDisplayType`,
  `CustomMillTool::build_custom_mill_tool_profile`). Any effect of Fluted / As defined elsewhere (mastercam.dll): NOT DECODED.
- Draw entry: `FUN_66d78a00` → per tool `FUN_66d35aa0(tool, ctx, 1, active=1, …)`; aggregate tools (`FUN_66d84df0`) pass a per-tool
  active flag. Context: ctx[1] = ModelVisualOptions, ctx[2] = view flags (view+0x5a tool, +0x5b holder).
- Before drawing (`FUN_66d78a00`): material ambient (0.05,0.05,0.05,1), specular (1,1,1,1) (layer slot 0xe0 = `FUN_66d31460`;
  defaults when NULL: diffuse 0.8, ambient 0.2, specular 0, emission 0, shininess 0 → 5.0); headlight **GL_LIGHT6** (slot 0x90
  `FUN_66d31130`) colour **0xEAEAEA**: diffuse = (234/255)·(1,1,1), specular = c + (1−c)/1.25 per channel (as coded the G channel uses
  the B input: `g' = (1−g)/1.25 + b`), ambient = specular × 0.05, position = (view direction, 0) (directional, from un-projecting).
  If unlit (+0xc): layer slot 0x18 (`FUN_66d30390(false)`: disables ALPHA_TEST, BLEND, DITHER, FOG, LIGHTING, …); then slot 0x68
  (`glDrawBuffer`) with `GL_FRONT` when unlit and the view has no single-back-depth-buffer flag (`view+0x40`, §1.2), else `GL_BACK`.
- **Shaded** (unlit = 0) in `FUN_66d35aa0`: slot 0xd0(0) = `glShadeModel(GL_SMOOTH)`, enable COLOR_MATERIAL, LIGHTING, ALPHA_TEST,
  BLEND (`glBlendFunc(GL_SRC_ALPHA, …)`), `glColorMaterial(FRONT_AND_BACK, AMBIENT_AND_DIFFUSE)`, `glPolygonMode(FILL)`;
  slot 0x48(1) = enable CULL_FACE; material diffuse 0.1 grey, ambient 0.15 grey. **Two passes**: first with `glCullFace(GL_FRONT)`
  (back faces), then `glCullFace(GL_BACK)` (front faces) (`DAT_66e1d9b0` recursion + slot 0xe8 `FUN_66d30640`).
- **Plain / Fluted** (unlit = 1): slot 0xd0(1) = lighting/blend/cull off, `glPolygonMode(GL_LINE)` ⇒ wireframe of the same tool
  polygons, single pass.
- Alpha = `opacity·255` in the colour word: active tool `bp_opacity_active` (+0xc8), inactive tools of an aggregate
  `bp_opacity_inactive` (+0xd8), wire guide `bpw_opacity_active` (+0xd0) (`FUN_66d3c2f0` / `FUN_66d3bd80` / `FUN_66d3c300`).
  Blending is only enabled in Shaded.
- Colours (`FUN_66d3c2d0`, `FUN_66d3bd60`, `FUN_66d3bd50`):
  | Part | normal | B/W |
  |---|---|---|
  | tool | ModelVisualOptions+0x40 (`Wd_colors[bp_tool_color]`, or material `(bp_tool_material+1)<<24`) | 0xC0C0C0 |
  | holder (mill) | +0x38 (`bp_holder_*`) | **0x808080** |
  | wire guide | +0x3c (`bpw_guide_*`) | 0 |
  High byte ≠ 0 ⇒ material: `FUN_66d92c50` stores a `0x101`-tagged entry `(alpha<<8 | material)<<16 | 0x101`; else
  `FUN_66d92b10` stores `RGB | alpha<<24`. How a material index is rendered (GL material table): NOT DECODED.
- Tool drawn iff view flag tool (display flag 7); holder drawn iff holder flag (8) and tool entity +0xb8 == 0; the holder uses the
  second profile list (entity+0x18), the tool the first (entity+8) (`FUN_66d35aa0`).
- **Spin tool** (`FUN_66d78a00`): only when `bp_spin_tool` (+0xf), the tool entity is not lathe (+0x61 = 0) and not wire (+0xf8 = 0).
  Angle accumulates `Δt(QueryPerformanceCounter) × 360°`, negated when tool entity +0xa8 == 0; rotation axis **(0,0,1)** in tool
  space (`DAT_66e1b2e8` = {0,0,1,0}). (The Options dialog disables "Spin tool" when Shaded, but the draw code does not check the
  appearance.)

### 13.5 Quick verify (flag 0xb)
Decoded so far: toggling writes `op_group+0x16e2 = flag` for every machine group (`FUN_66d19e90`, bp_all lines with
`poVar6[0x16e2]`); every step/slider end calls `FUN_66d1e9f0` (refresh only when the flag is on; < 0.5 s since the last refresh →
deferred via timer, §2.5) → `FUN_66d53280` (view slot 0x28/0x30 refresh); the model draw is wrapped in
`glPolygonOffset(1.25, 1.5)` (§5). Lathe: `NWD::LatheVerifyView` / `NWD::LatheSimpleVerifyPipe` created on `GMMChangeImportState`
(`FUN_66d0a0f0`); `FUN_66d0a1c0` rebuilds when the operation changes and flag 0xb is **off**. backplotbnci emits
`NWD::SimpleVerifyParams` (`FUN_67dccd40`), consumed in `FUN_66d9bb70`. What geometry quick verify draws (swept stock / colours):
NOT DECODED (next: `FUN_66d9bb70`, `FUN_67dccd40` callees `FUN_67dcc1f0`, `FUN_67dcefc0`, `FUN_67dce3c0`).

### 13.6 Rapid time (Info tab)
Formula (mcmachinedef `RapidMoveTime` @1800ffc80, §6) — restated with the data sources:
1. Machine = `OMgetOpMachine(op)`; axis combination = `CCNCMachine::AxisCombinationFromID(machine, op+0x11bc)` (falls back to
   index 0). Axes X/Y/Z = `CAxisCombination::LinAxComp(n = 1,2,3)` (linear axis components whose **machine axis** (UParams+0x198) is n).
2. Per-axis rapid rate = **`CLinearAxisComponent::MaxFeedrate(metric)`** @1800a8ab0 = double at **component UParams + 0x1a0 (inch) /
   + 0x1a8 (metric)**, UParams = `Params + 0x4a0`, Params = component object + 0x20 ⇒ object +0x660 / +0x668. Unit picked by `IsMetric()`.
3. Linear-motion control record = `CCNCMachine::CtrlLinearMotion(Type()==2)` @180073110: struct `nc_path_linear_ctrl` at
   **CCNCMachine+0x1e3e**, filled from the control definition (`CCNCMachine::Control()` = machine+0x1478) with
   `CCNCControl::GetLinear` (or `GetLinearMT` when `CCNCMachine::Type()` (machine+0xd8) == 2). Its **first short** selects:
   - 0 → `t = max over axes |Δaxis| / maxFeed(axis)` (axes with maxFeed ≤ 0 contribute 0) — independent axes;
   - ≠ 0 → `t = |Δ| / min(maxFeed over axes with |maxFeed| ≥ tol)` (initial min 1e30) — vector move at the slowest axis rate.
   ×60 when the 3rd argument is set (backplotbnci passes 1 ⇒ seconds); |Δ| ≤ `tol` or missing machine/combination/control → 0.
4. Δ per axis: mill = machine-axis mapping (axis n reads Δ component n−1 of the motion); op types 0x34/0x38 use (x, z, y); lathe ops
   transform view→world→view (`op+0x572`, `op+0x6d2`).
Where the axis feeds come from: `CLinearAxisComponent::InitDflts` (@1800a83c0) defaults them to the owning machine's
`CCNCMachine::MaxLinearFeedrate` (machine **+0x1f8 inch / +0x200 metric**), or 1000.0 / 25000.0 when there is no machine;
`FeedratesFromOwnerMachine` (@1800a7d80) overwrites both with the machine values when `UseMaxLinearFeedrate` (machine **+0x210**) is set.
`SetMaxFeedrate` ignores values ≤ 0. The machine definition (components + control) is the one stored with the machine group in the
part (machine record class 0x4000, record +0x2c = 22, see formats/feeds_speeds.md); **the byte offsets of these fields inside the
part's machine record (serialisation of `CCNCMachine` / `CLinearAxisComponent`) are NOT DECODED** (next: the CCNCMachine and
CMachineComponent `Serialize` functions in mcmachinedef.dll), and so is the meaning of `nc_path_linear_ctrl` beyond its first short
and of machine `Type() == 2`.
Tool change time (§6) is added to the rapid bucket: `avg_tool_change / 60 · C4` per tool change.

### 13.7 Details tab: editing
**Mastercam 2017 shows the per-move pages read-only.** Evidence: every edit control of pages 4000–4013 in backplotres.dll has
style 0x50810880 (**ES_READONLY** 0x800) or 0x48810880 (hidden + disabled + read-only); no `EM_SETREADONLY` (0xcf) or other
read-only toggle appears in the backplot.dll / backplotbnci.dll decompiles. The apply handlers of the 13000 page are wired in its
message map (66dd5460) to WM_COMMAND **13003 → `FUN_66d27190`, 25322 → `FUN_66d26e50`, 13002 → 66d26df0**, but no control or menu item
with those ids exists in backplotres.dll and the constants (0x32ca/0x32cb/0x62ea) do not occur in either decompile ⇒ no UI path sends
them. The coolant label and combo 4104 are hidden and disabled in every template (style 0x48020002 / 0x48210102), the "Circle" check
4207 is visible but disabled (0x58010003). ⇒ implement the Details tab as display-only. (What the apply path would do: write the edited
entity back through `FUN_66d61950`, optional split `FUN_66d5ef00` — unreachable.)

### 13.8 Operation List: Path level (§8 detail)
- Data: `model+0x268` = vector of per-operation records, stride **0x428**; record +8/+0xc = motion range [start, end) of the operation,
  +0x418 = operation id (→ `TpOpList::OpByID`, its group `op+0x28c`), **+0x20/+0x28 = vector of path ranges, 0x20 bytes each**
  (`NWD::PathOperation` : `GeometryRange`: +8 start, +0xc end, +0x10 path-record pointer (colour `rec+0x48`, §12.3), +0x18 byte)
  (`FUN_66d2c4f0`, copy helpers `FUN_66d42950` / `FUN_66d429b0`).
- Build (`FUN_66d2c4f0`): for each machine group, each operation record whose operation belongs to that group gets a level-2 item
  (`op_make_description`), and **one level-3 item per path range with end ≠ start** (image `+0x160c`), text by callback.
- Label (TVN_GETDISPINFO `FUN_66d2bc10`): `"%s %d"` with string 17001 "Path" and `FUN_66d5c210(item) + 1`, where `FUN_66d47730`
  returns the **1-based** position of the path in its operation's full path vector (zero-length paths included) ⇒ as coded the first
  path is labelled **"Path 2"** (position 1 + 1). Without stored data: `"%d"` with the same number.
- Selecting a path item makes its range the visible range (§8).
- How an operation's motions are split into path ranges (who fills record +0x20): NOT DECODED (next: writers of the 0x428 record's
  +0x20 vector; `NWD::OperationBuilder` vtable 66dda1f0 / `FUN_66d4a6a0`).

### 13.9 Still NOT DECODED after this pass
- `UIEditInt/UIEditReal::Validate` bodies (uictrls.dll) — meaning of +0xbf0/+0xbf4/+0xbf8/+0xc0c inferred, not read.
- Material list names (`gl_InitMaterialList` / `gl_mat_list`) and how a material index is rendered.
- Any effect of Fluted / "As defined" (no reader in the backplot DLLs or mccore).
- CW/CCW arc colours: no drawing reader.
- Quick-verify geometry (13.5); the width-3.0 line style trigger (13.3).
- Byte offsets of the axis rapid feeds / `UseMaxLinearFeedrate` inside the part's machine record; `nc_path_linear_ctrl` fields; machine type 2.
- The splitting of operations into Path ranges (13.8); the config-file line parser that calls `FUN_1801b7980`.

## 14. Code: viewer/backplot.c / backplot.h / backplot_ui.inc (and the mcamui.c / scene.c parts)
(Qt-port phase 8, 2026-10-21: the UI model below - bp_start / bp_stop / bp_toggle / bp_keys / bp_sync_tool / bp_build_lines /
bp_set_lines / bp_advance, the Options dialog 1228 (now a control table BPO_CTLS with bpo_on / bpo_en / bpo_click / bpo_val /
bpo_edit_commit), the Operation List rows (bp_tree_rows), the Details / Info pages, Conditional Stops - moved to
viewer/core_ui/cui_backplot.c with the same names; mcamui.c / backplot_ui.inc and viewer-qt src/bpdlg.* draw it.)
Motions = what the post outputs (`nc_motions`, nc_post.c), one `NcMotion` per move (kind rapid / line / CW / CCW /
drill / tool change, start `a`, end `b`, centre `c`, feed, plane, op index, tool).

**backplot.h / backplot.c** (state, no drawing):

| item | behaviour | spec |
|---|---|---|
| `BpStops` | conditional stops of dialog 6000 (at ops 6001, tool change 6013, every N steps 6002 / 6003, at X / Y / Z value 6004-6009, at X / Y / Z change 6010-6012); defaults all off (FUN_66d8a460) | §3 |
| `Backplot` | motions, cumulative lengths `cum`, position `pos` (= end of that motion) and exact distance `dist`, visible range [vis0, vis1), stop list, 17 display flags, interpolation | §2.2, §4.1, §8 |
| `bp_init` | flags byte 1 = 1 and Run (0xd) = 1 (FUN_66d80400), interp distance 0.05 until the config is read | §4.1 |
| `bp_set_visible` | Operation List selection -> visible range, stops recomputed, position = last motion of the selection | §8 |
| `bp_seek_motion` / `bp_seek_dist` / `bp_point` | slider position / distance along the path -> current tool point | §2.2 |
| `bp_step(mode, count)` | FUN_66d84250: first / last, back / forward one motion per step or count x Step increment along the path when Interpolate (FUN_66d627c0), prev / next stop (FUN_66d84030) | §2.5 |
| `bp_compute_stops` | FUN_66d62070: stop at the first motion, then op change (FUN_66d8b1f0), tool change (FUN_66d8b400), step counter / XYZ value / XYZ change (FUN_66d8b260); tolerance 1e-5 | §3 |
| `bp_info` | Info tab sums over the visible motions: feed / rapid length, feed time L / F x 60, XYZ min / max, feed min / max | §6 |
| `bp_info_rapid` + `BpMachine` | rapid time = sum of RapidMoveTime, tool change time avg / 60 per tool change; only when the machine inputs are known - `known` is always 0 here (machine record offsets NOT DECODED), so no rapid / total time is shown | §6, §13.6 |
| `bp_rapid_move_time` | RapidMoveTime @1800ffc80: independent axes (ctrl0 0) or vector move at the smallest axis rate, x 60 | §13.6 |
| `BpOptions`, `BP_OPT_FACTORY`, `bp_opt_cfg` | the mccore [BACKPLOT] globals 19000-19040 + config 1000 tol; factory defaults FUN_1803ba450 (U = 0 branch, flag 0x1813eb30c NOT DECODED); key -> global FUN_1801b7980 for "NNNNN. label? value" lines | §12.1, §13.1 |

**backplot_ui.inc** (included by mcamui.c):

| part | behaviour | spec |
|---|---|---|
| `bp_options_open`, `bpo_update`, `bpo_ok`, `draw_bp_options` | Options dialog 1228 (modal): controls at template DLU positions; working copy `bpo`; update-controls FUN_180707280 (Shaded-only Material, Operation change forces Tool change); OnInitDialog FUN_180705ab0; OK FUN_1807051d0 writes the globals then reloads interpolation and lines; Cancel / X write nothing except the opacity edits / spins, which write the globals at once (FUN_180706820 / FUN_180706a00); cutter comp in control stays disabled (no multiaxis ops, FUN_66d19c90) | §13.1 |
| `bpo_edit` | MCByte colour edits 0..255 with ClueWnd "%u to %u"; MCInt / MCReal bounds taken from +0xbf0 / +0xbf4 (inferred), rejected value keeps the old; opacity n x 0.01, 1..100 else 50; plain numbers only (EvalToDouble NOT DECODED) | §13.1, §13.9 |
| `bpo_color`, `bpo_swatch` | colour row: edit + owner-draw swatch (disabled -> index 7, FUN_1807055f0) + Select (bitmap 0x61d2 -> Colors picker) | §13.1 |
| `bp_draw_tree`, `bp_tree_menu` | Operation List 17000: machine group / operations / one "Path 2" item per operation (FUN_66d2bc10 numbering); selection sets the visible range; right click menu 19100 Expand All / Collapse All | §8, §13.8 |
| `BP_PG4000/4001/4002`, `bp_det_val` | Details pages Rapid / Feed / Arc from the backplotres templates, read-only; plane 0/1/2 -> XY / XZ / YZ; rapid feed blank | §7, §13.7 |
| `BP_PG4006`, `bp_info_val`, `bp_draw_info` | Info tab 4006: cycle time statics hidden when the total is 0 or the feed time is invalid ("Invalid feedrate", string 29003) | §6 |

**mcamui.c** (backplot section): `bp_start` (COpMgrTree::BackplotSelected: no selected op -> opmanres 3964; speed 0 ->
75), `bp_stop`, `bp_toggle` (holder on -> tool on), `bp_keys` (VCR accelerators S B N P R, FUN_66d1ed30),
`bp_sync_tool` (§13.4: tool iff flag 7, holder iff flag 8; no part tool -> the op's own tool from OP_ID +0x34c type /
+0x350 dia, else none, FUN_67d1a2e0), `bp_build_lines` / `bp_set_lines` (§12.3 / §13.2: rapids in bp_rapid_color,
Run vs Trace portion, cleanup on tool / op change, endpoints), `bp_advance` (Play FUN_66d84490, max 50 updates / s),
`draw_bp_vcr` (15000), `bp_num` (n_places, trailing zeros cut, FUN_66d87620), `bp_time` (FUN_66d28710).
**scene.c**: `bp_draw_tool` (GL state of FUN_66d78a00 / FUN_66d35aa0 as a shader; tessellation 48 NOT DECODED),
`bp_draw_endpoints` (white GL points of size 2, §13.3), the motion lines `bp_lv` / `bp_ln`.

Not done / NOT DECODED in the code: Save as geometry 16001 / tool geometry 16007, Isolate 16003 / path view 16004,
the 14000 dialog's screen position, quick verify (§13.5), wire Options dialog 1229 (no wire ops), rapid time
(§13.6), the operation -> path split (§13.8), material lists (combos empty), the Select button's 3rd argument (no
pick from an entity), ClueWnd lifetime (until a click), Help (greyed), cutter comp 4105 / Circle 4207 values on the
Details pages, a page for tool-change motions, Mastercam's handling of a backplot with no motion (nothing said).
Ours: the message caption is the program name.
