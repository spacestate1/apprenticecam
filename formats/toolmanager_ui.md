# Mastercam 2017 Tool Manager and Define Tool wizard: UI implementation spec

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

Status: decoded from Mastercam 2017 (v19) files. **HARD RULE: no improvisation.** Every item below cites its source:
a decompiled C# file, class and method; a BAML file and element (x:Name / x:Uid); a resx string id; a mccoreres.dll resource id;
or a help page under `~/mastercam-re/chm`. Anything without a source is marked **NOT DECODED**. Implement it only after it has been decoded.

Sources:
- `~/mastercam-re/dotnet/toolmanager.*` is the ILSpy C# output plus `.resx` string tables. The `*.baml` WPF layouts are read with
  `tools/baml_dump.py FILE.baml`, which prints pseudo-XAML. Its record layouts and the WPF known-type tables come from ILSpy's ICSharpCode.BamlDecompiler.
  All 80 `.baml` files parse. Path-geometry (`XamlPathDataSerializer`) values are printed as hex.
- `~/mastercam-re/files/mccoreres.dll` resources are read with `tools/pe_dialogs.py`, `tools/pe_menus.py` and `tools/pe_strings.py`.
- Notation: «text» is an English resx string, with its id in [Class.Key].

Reading order for an implementer of `viewer/mcamui.c`:
- §0 identifies which window is "Tool Manager" inside Mastercam. It is the MFC dialog in §1, not the WPF app.
- §1 is the window to rebuild; it is implemented in `viewer/toolmgr_ui.inc` (§1.5.10: code map, toolmanager.conf, deviations).
- §2 is the Define/Edit Tool wizard, which Mastercam runs in-process; its logic is `viewer/toolwiz.c` (§2.13), drawn by
  `draw_tool_editor` in mcamui.c; the Coolant dialog is §2.12a.
- The tool library data layer is `formats/toollib_net.md` / `formats/tooldb.md` (code `viewer/toollib.c`, CLI `viewer/tool_cli.c`).
- §3 is the stand-alone WPF Tool Manager. Use it for reference: its columns, numbering and copy rules are the decoded .NET behaviour.

## 0. Which UI is "Tool Manager"

**Answer: inside Mastercam 2017, the "Tool Manager" for mill/router (the ribbon command and the Toolpaths-manager / toolpath Tool page entry)
is the MFC dialog `mccoreres.dll` dialog 1099 "Tool Manager" (help calls it "Tool Manager (Classic)"). The WPF ToolManager
(`toolmanager.exe` + `toolmanager.core/workspace/...`) is a separate stand-alone program started from the Windows Start menu. The only WPF
pieces Mastercam runs in-process are the wizards: Create/Edit Tool, Create/Edit Holder, Create/Edit Live Center, and the Cut Parameter search.**

Evidence:

| # | Claim | Source |
|---|---|---|
| E1 | Ribbon command "Mill Tool Manager" runs `RunMillToolManager` in MCCORE.DLL (native C++, kind `CPP`); tooltip string 32932 = "Tool Manager..." | `formats/ui_commands.tsv` line 361 (`mastercam.ft  Tool Manager  Mill Tool Manager  MCCORE.DLL  RunMillToolManager  CPP  MCCORERES.DLL 10452 10453 32932`); `mccoreres.dll` string 32932 |
| E2 | mccore.dll contains the MFC classes `CToolListBoth` (methods `EditPartTool`, `EditPartHolder`, `EditPartAssemblyNames`, `EditPartAssemblyProjection`, `WriteSettings`), `CToolList` and `CMillToolListCtrl` (CListCtrl subclass with `InitColumns`, `ColumnCount`, `GetDefaultColumnWidth`, `ToggleColumnVisible`, `VisibleColumnPopupMenu`, `OnHeadingRightClick`, `SortColumn`, `BeginDrag/EndDrag`, `SetListDisplayMode`, `Read/WriteRegistrySettings`). "Both" = the dialog's Part+Library display. | `strings -a ~/mastercam-re/files/mccore.dll` (RTTI `.?AVCToolListBoth@@`, `.?AVCMillToolListCtrl@@`, assert strings `CToolListBoth::EditPartTool` ...) |
| E3 | Toolpath dialogs call native `ToolpathDlg::OnRunToolMgrPb`, `ToolpathDlg::RunToolManager`, `ToolPageBase::RunToolManager` | mccore.dll export/symbol strings `?OnRunToolMgrPb@ToolpathDlg@@`, `?RunToolManager@ToolPageBase@@` |
| E4 | Help: "Classic Mill/Router Tool Manager ... Access this classic Tool Manager by selecting Tool Manager from the Mill or Router contextual tab"; "To work in the stand-alone Tool Manager, select the Windows Start menu, and choose Mastercam, Tool Manager. You do not have to have Mastercam open" | `~/mastercam-re/chm/mccore/Tools/Mastercam_s_Tool_Managers.htm` |
| E5 | Help topic "Tool Manager (Classic)" describes exactly dialog 1099's controls: drop list upper-right with Both / Part Tools / Library Tools, Filter active + Filter button, Part and Library windows, Up/Down arrow buttons, "= Tool used in an operation" check mark, Import tools from parts | `~/mastercam-re/chm/mccore/Tools/Tool_Manager.htm`; screenshot `~/mastercam-re/chm/mccore/Pics_Screens/select_tool_mgr_view.jpg` (list shows "Both", "Part Tools", "Library Tools" next to the "(Part)" label) and `Tool_List_Column_RCM.jpg` (title bar "Tool Manager", "Machine Group-3" combo, "= Tool used in an operation", library path `C:\Users\Public\Docu...\Mill_Inch.tooldb`) |
| E6 | Right-click Create new tool / Edit tool / Edit holder in the Classic manager "opens the Create Tool wizard [Edit Holder wizard] in the stand-alone Tool Manager" (i.e. the WPF wizard) | `~/mastercam-re/chm/mccore/Tools/Tool_Manager_(Classic)_right-click_menu.htm` |
| E7 | mccore.dll loads `\Extensions\ToolManager.ToolWizardLauncher.dll` and calls its exports `ExecuteServiceRequest`, `m_open`, `m_close`; native exports `Cnc::Tool::CreateNewToolWizard`, `CreateNewHolderWizard`; classes `ToolManagerUIProxy`, `ToolManagerInteropThunk` | mccore.dll strings (adjacent block: `\Extensions\ToolManager.ToolWizardLauncher.dll`, `ExecuteServiceRequest`, `m_open`, `m_close`) |
| E8 | `ExecuteServiceRequest(shared_ptr<TlRequestMgr>)` switches on `TlRequestType`: 0 `LaunchCreateToolWizard`, 1 `LaunchCreateHolderWizard`, 2 `LaunchSearchCutParam`, 3 `LaunchEditToolWizard`, 4 `LaunchEditHolderWizard`, 5 `LaunchCreateLiveCenterWizard`, 6 `LaunchEditLiveCenterWizard`; parent window = `GetLastActivePopup()` of the Mastercam main window, which is disabled (`EnableWindow(0)`) during the call; results are added back with `TlMgr::Add`. No request type opens a tool-manager window. | `toolmanager.toolwizardlaunch/-Module-.cs` `ExecuteServiceRequest` (line 4132 ff.) |
| E9 | `m_open`: if registry key named `RunToolManagerIPC` is present -> `SetupInterprocessCommunications` (IPCSession, talks to a ToolManager process via `ApplicationClientFactory.ThreadStartToolManager`), else `SetupInProcessCommunication` (RequestInProcess). | `-Module-.cs` `m_open` (line 4547), string literal `RunToolManagerIPC` (line 512); `IPCSession.cs` `StartupServiceClient` (line 677-680) |
| E10 | In-process path: `RequestInProcess` creates `ToolManagerInteropFactory` -> separate AppDomain `ToolManagerInterop`; `IMastercamInterop` offers only `RunToolWizardInProcess`, `RunHolderWizardInProcess`, `RunLiveCenterWizardInProcess`, `RunSearchCutParamInProcess`, `PreInitializeUI` -> `wizardManager.LaunchToolWizard(...)` | `toolmanager.toolwizardlaunch/RequestInProcess.cs` ctor + `LaunchCreateToolWizard` (L166-210); `toolmanager.interop/ToolManager.Interop/ToolManagerInteropFactory.cs` `InitializeInterop` (L109-147); `toolmanager.interop/ToolManager.Interop.Services/IMastercamInterop.cs`; `MastercamInterop.cs` L37-57 |
| E11 | `toolmanager.exe` is a stand-alone WPF app (`App.Main`); with `-silent<pid>` it shows no splash, watches the Mastercam process and shuts down when it exits; otherwise shows `toolmanagersplashscreen.png`. `ModuleToolManagerInterprocess.Initialize` starts the web (WCF) service only when started with `-silent:` | `toolmanager.exe/ToolManager.Shell/App.cs` `Main`, `OnStartup`, `SetupMastercamWatcher`; `toolmanager.interprocess/ToolManager.Interprocess/ModuleToolManagerInterprocess.cs` L129-136 |
| E12 | Toolpath Tool-page right-click menu (mccoreres MENU 1247) has both "Tool &manager..." (32976) and separate items "Edit Tool In New Editor..." (33069), "Save Tool To New Library..." (33072), "Get Item From New Tool Manager..." (33075), "Send Item To New Tool Manager..." (33079) — i.e. the WPF one is the "New Tool Manager", distinct from "Tool manager..." | `python3 tools/pe_menus.py ~/mastercam-re/files/mccoreres.dll`, MENU 1247 |
| E13 | Lathe Tool Manager is a different native dialog: `LatheToolManager(CWnd*)` builds a `CLToolListDlg` | `decomp/mclathe_all.c` L265201-265230 |

Wizard-launch parameters decoded from `RequestInProcess.cs` (useful for the wizard spec):
- Create tool (`LaunchCreateToolWizard`, L166-210): `IsReadOnly=readOnly`, `ParentWindowPtr=parent`, `CoolantEditorParameters=CreateCoolantParameters(coolantInfo)`,
  `AddToDiamterOffsetValue=doff`, `AddToLengthOffsetValue=loff`, `ToolNumberDefault=tlno`, `MetricMode=isMetric`, `InitialPage=(ToolWizardPages)0`,
  `DefaultToolType=(ToolTypeList)(isDrilling ? 3 : 10)`; result converted with `TlDataInterop.ConvertExtensionsToolToMastercamTool`.
- Edit tool (`LaunchEditToolWizard`, L263 ff.): `MetricMode` = tool's `GetIsMetric`, `AddToDiamterOffsetValue=addToDoff`, `AddToLengthOffsetValue=addToLoff`,
  `ToolToEdit`, `InitialPage` = 1 or 2 when `startPageNumber` is 1/2, else 0.
- After a Create-tool request returns, `ExecuteServiceRequest` copies the tool's `op_tool_info`, stores the request's machine group id at offset 628,
  the metric flag at offset 600, and if the short at offset 4 is 0 and the double at offset 32 is 0.0 sets that double to 180.0 (field names NOT DECODED),
  then `TlMgr::Add` (`-Module-.cs` case `(TlRequestType)0`).

What decides the Tool Manager window's behaviour (list fill, numbering on copy, column order/widths, filter logic) is native code in mccore.dll
(`CToolListBoth`, `CMillToolListCtrl`, `TpToolMgr::GetOpenToolNumberAllLists`) — **NOT DECODED here** (needs Ghidra on mccore.dll; not run by this agent).


## 1. In-Mastercam Tool Manager: MFC dialog 1099 "Tool Manager"

Source: `python3 tools/pe_dialogs.py ~/mastercam-re/files/mccoreres.dll | awk -F'\t' '$1==1099'`; window/control style words read from the same
DIALOGEX template (style decoding = standard Win32 constants). Units are dialog units (DLU).

Dialog: 600 x 350 DLU, style 0x80CC0040 = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME (resizable) | DS_SETFONT; font "MS Sans Serif" 8 pt; title "Tool Manager".

| id | class | style | x | y | w | h | text (verbatim) | meaning (source) |
|---|---|---|---|---|---|---|---|---|
| 25276 | ComboBox | 0x50210003 CBS_DROPDOWNLIST, WS_VSCROLL | 7 | 7 | 130 | 85 | | machine-group selector; screenshot shows "Machine Group-1" / "Machine Group-3" (`Pics_Screens/Edit_tool_from_RMC_in_Tool_Manager.jpg`, `Tool_List_Column_RCM.jpg`); help "You can display tools from a different machine group by selecting it from the list" (`Tool_Manager.htm`) |
| 25330 | Static | 0x5000020E SS_BITMAP, SS_CENTERIMAGE | 145 | 7 | 15 | 14 | #25046 | bitmap resource 25046 (the "used in operation" check icon, per screenshot) |
| 25331 | Static | 0x50020000 | 167 | 10 | 96 | 8 | = Tool used in an operation | |
| 25457 | Static | 0x50020000 | 345 | 15 | 18 | 8 | (Part) | |
| 25297 | ComboBox | 0x50210003 CBS_DROPDOWNLIST | 513 | 7 | 81 | 55 | | view selector; items "Both", "Part Tools", "Library Tools" (`Pics_Screens/select_tool_mgr_view.jpg`; help `Tool_Manager.htm` "Use the drop list in the upper-right corner to select which tools to view"). Items are strings 5414 «Both», 5460 «Part Tools» and 5464 «Library Tools»; see §1.5.2 |
| 25464 | SysListView32 | 0x50810109 LVS_REPORT, LVS_SHOWSELALWAYS, LVS_AUTOARRANGE, WS_BORDER | 7 | 25 | 358 | 114 | List2 | Part tool list (upper) |
| 25325 | Button | 0x50010003 BS_AUTOCHECKBOX | 377 | 87 | 57 | 10 | Filter Active | filter for part list |
| 25460 | Button | 0x50010080 BS_BITMAP | 395 | 161 | 27 | 24 | Add a Library Tool to Part | "Up" arrow button (help: "Use the Up and Down arrow buttons to copy tools from one window to the other") |
| 25461 | Button | 0x50010080 BS_BITMAP | 395 | 193 | 27 | 24 | Add a Part Tool to Library | "Down" arrow button |
| 10253 | Static | 0x50028200 SS_CENTERIMAGE, SS_PATHELLIPSIS | 7 | 187 | 130 | 15 | Tool library file | shows the current library path (screenshot shows `C:\Users\Public\Docu...\Mill_Inch.tooldb`) |
| 25299 | Button | 0x50010080 BS_BITMAP | 147 | 185 | 25 | 18 | New Library | |
| 25079 | Button | 0x50010080 BS_BITMAP | 183 | 185 | 25 | 18 | Browse Libraries | |
| 25225 | Button | 0x50010080 BS_BITMAP | 219 | 185 | 25 | 18 | Import Tools | help: "Click the Select file for tool import button. Select the Mastercam file ... Enter a name and location for the new tool library. Mastercam opens the new library in the Tool Manager" (`Tool_Manager.htm`, "Importing tools from parts") |
| 25456 | Static | 0x50020000 | 339 | 195 | 26 | 8 | (Library) | |
| 25463 | SysListView32 | 0x50810109 (as 25464) | 8 | 211 | 358 | 132 | List2 | Library tool list (lower) |
| 25193 | Button | 0x50010000 BS_PUSHBUTTON | 377 | 241 | 50 | 14 | &Filter... | opens "Tool List Filter" (dialog 25519, below) — per help `Tool_Manager.htm` |
| 25263 | Button | 0x50010003 BS_AUTOCHECKBOX | 377 | 261 | 57 | 10 | Filter Active | filter for library list |
| 10407 | Button | 0x50000007 BS_GROUPBOX | 375 | 276 | 72 | 44 | Display mode | |
| 10408 | Button | 0x50030009 BS_AUTORADIOBUTTON, WS_GROUP | 387 | 287 | 33 | 10 | Tools | |
| 10409 | Button | 0x50010009 BS_AUTORADIOBUTTON | 387 | 297 | 51 | 10 | Assemblies | |
| 10410 | Button | 0x50010009 BS_AUTORADIOBUTTON | 387 | 308 | 31 | 10 | Both | |
| 1 | Button | 0x50010080 BS_BITMAP | 374 | 325 | 36 | 18 | \|&OK | (leading "\|" is in the resource text; its meaning NOT DECODED) |
| 9 | Button | 0x50010080 BS_BITMAP | 415 | 325 | 36 | 18 | \|&Help | |

Defaults and control effects (view combo, Display-mode radios, Filter Active, machine-group combo): see §1.5.2. Layout on resize
(`FUN_18087a9c0` / `FUN_1808790f0`): §1.5.9.

### 1.1 List columns

Column chooser (right-click on a column header, `CMillToolListCtrl::OnHeadingRightClick` / `VisibleColumnPopupMenu`) shown in help screenshot
`~/mastercam-re/chm/mccore/Pics_Screens/Tool_List_Column_RCM.jpg`, all ticked, in this order:
`#`, `Assembly Name`, `Tool Name`, `Holder Name`, `Dia.`, `Cor. rad.`, `Length`, `# Flutes`, `Type`, `Rad. Type`, `Tool Projection`.
Help `~/mastercam-re/chm/mccore/Tools/Customizing_tool_lists.htm`: columns can be dragged to reorder; right-click header to choose columns.
Help `Tool_Manager.htm`: click a heading to sort by that column; drag header edges to resize.

The same header texts exist in the mccoreres string table: 3759 "#", 12128 "Assembly Name", 3764 "Tool Name", 12127 "Holder Name", 3794 "Dia.",
3791 "Cor. rad.", 3781 "Length", 3792 "# Flutes", 3761 "Type", 3793 "Rad. Type", 3801 "Tool Projection". Radius-type values candidates 3798 "None",
3799 "Corner", 3800 "Full". The id → column mapping, value sources, number formats, default widths, default visibility and sorting are decoded in §1.5.3
(`CMillToolListCtrl::InitColumns` loads exactly these ids). The native list has 13 columns: two more, both without a header text,
precede «#». Column 0 has width 0, and column 1 (width 25) is the image column.

Other candidate strings near the column block (usage NOT DECODED): 3774 "Filtered ", 3788/3805 "%d of %hd tools displayed", 3783 "Overall length",
3784 "Shoulder length", 3786 "Holder length", 3822 "in.", 3823 "mm", 3824 "Are you sure you want to delete the selected tool(s)?", 3825 "Tool",
3767 "Convert a library to text...", 3770 "Convert a library from text...", 3795 "Create a library from text...", 3772 "TOOLLST",
3776 " already exists", 3779 "Append to existing file? (Yes = Append, No = Overwrite)", 3826 "Invalid Custom Tool - ",
3827 "You will need to edit this tool and regenerate the operations which use it.", 3829 "Warning - Cannot find custom tool geometry file for: ",
12130 "Composite", 12131 "Edit composite holder".
Note help `Tool_Manager_(Classic)_right-click_menu.htm` says Delete tool(s) has "no undo or confirmation prompt". The code does prompt:
always for the library list, and for the part list only when a selected tool is used by an operation. The prompt uses strings 3840/3841/5585,
not 3824 (see §1.5.4). The use of 3824 is still NOT DECODED.
Resolved by §1.5: 3759-3801 and 12127/12128 are the column headers (§1.5.3). 12131 is the title of the composite-holder popup in menu 1225 (§1.5.5). The use of 3774, 3788/3805, 3783/3784/3786, 3767/3770/3795
and the other strings in the list above is NOT DECODED.

Views: Details (report, with check-mark icon on used tools), Small icons, Large icons (help `Tool_Manager.htm`, screenshots
`Green_Check_on_details_icon.jpg`, `Green_Check_on_small_icon.jpg`, `Green_Check_on_Large_icon.jpg`).

### 1.2 Right-click menu (mccoreres MENU 1225)

`python3 tools/pe_menus.py ~/mastercam-re/files/mccoreres.dll`, MENU 1225 (identified as the Classic Tool Manager list menu because its item set
matches the help page `Tool_Manager_(Classic)_right-click_menu.htm` and `CToolListBoth::EditPartHolder/EditPartAssemblyNames/EditPartAssemblyProjection`;
the binding is decoded: `CToolListBoth`'s WM_CONTEXTMENU handler `FUN_1808777f0` loads menu resource 0x4c9 = 1225. Per-list
enable/disable and handlers are in §1.5.5):

```
Create &new tool ...          32933
&Edit tool...                 32935
Edit holder...                33217
Edit projection...            33229
Edit assembly names...        33215
&Delete tool(s)               32937
Delete &unused tool(s)        32938
----
&View >  &Large Icons 32939 | &Small Icons 32940 | &Details 32941
A&rrange tools >  by Tool N&umber 32942 | by Tool &Type 32943 | by Tool &Name 32944 | by Tool &Diameter 32945
----
&Copy tool(s)                 32947
&Paste tool(s)                32948
----
Load tool(s) on machine...    33003
&Save tool(s) to library...   32946
&Import/export tools... >
    Convert a library &to text...    32949
    Convert a library &from text...  32950
    Import from Machining Cloud...   33080
    &Compress                        32951
    &Report...                       32952
    &Detailed report...              32953
```

Help descriptions (`~/mastercam-re/chm/mccore/Tools/Tool_Manager_(Classic)_right-click_menu.htm`), mill-relevant:
- "Some options are available only in the Part window or the Library window. Options that work on a single tool are available only if you right-click directly on a tool definition."
- Create new tool / Edit tool: "For Mill, this option opens the Create Tool wizard in the stand-alone Tool Manager."
- Edit holder (Mill only): opens the Edit Holder wizard. Edit composite holder: replaces Edit holder for composite assemblies, fly-out to choose the component.
- Edit projection: edit projection of tools / composite holders graphically in the graphics window.
- Edit assembly names (Mill only): opens the Assembly Names dialog (mccoreres dialog 33053, below).
- Delete tool(s): "Removes the selected tool definition from either your part file or tool library, depending on which window you were in. Note: There is no undo or confirmation prompt".
- Delete unused tools: removes part-file tools not used by a toolpath; not deleted from the library.
- View: large icons / small icons / details. Arrange tools: "Sort the tools by tool number or tool name. Use these options if you have the tools displayed as icons without any column headers." (menu also has by Type / by Diameter).
- Copy tool(s) / Paste tool(s): via Mastercam's clipboard, to another machine group or library.
- Save tool(s) to a library: from Part or Library window.
- Compress: packs the library. Report / Detailed report: ActiveReports.
- Load tool(s) on machine: Lathe / Mill-Turn only per help.

Dialog 33053 "Assembly Names" (293x135 DLU): Static "Assembly Name:" (13026, 6,13), "Tool Name:" (13027, 6,37), "Holder Name:" (13028, 6,60);
Edits 13029 (105,10,150x14), 13030 (105,34), 13031 (105,57); buttons |&OK (1), |&Cancel (2), |&Help (9).

### 1.3 Copy / drag-drop / numbering (help only)

From `~/mastercam-re/chm/mccore/Tools/Tool_Manager.htm`:
- Multi-select with Ctrl+click / Shift+click. Copy to the other window by: the Up/Down arrow buttons; dragging; double-clicking a Library tool copies it to the Part window.
- Part window: double-click a tool to edit it. Edits in the part window change only the machine-group copy, not the library. Deleting from the part removes it from the machine group only.
- "Copying a tool from the Part window to the Library window saves it to the tool library that is displayed in the window. When you copy a tool to the library, Mastercam assigns it the next available tool number. This means that if an identical tool already exists in the library, Mastercam will still create a new tool with a new tool number."
- Ctrl+C / Ctrl+V copies tools between machine groups or between libraries.
- Mill and Router users can open each other's libraries.

From `~/mastercam-re/chm/mccore/Tools/Creating_Tools_with_the_Tool_Wizard_NEW.htm`: "The tool is added to the library if you right-clicked and selected Create new in the lower tool list of the Mill Tool Manager. All other tool lists will create a tool in the part file."

Numbering and D/H offsets when a tool is added to the PART (Library->Part) are decoded in §1.5.4 (Up) and §1.5.7
(`GetOpenToolNumberAllLists`). Library-side numbering for Part->Library (`TlToolLibraryX::AddAssemblies`): the tool number is kept (§1.5.4). Related help
(`Working_with_tool_numbers_and_offsets.htm`, `Setting_a_default_tool_numbering_method.htm`) covers operation tool numbers: sequential-by-operation vs. read from tool
definition (Machine Group Properties / defaults "Assign tool numbers sequentially"); offsets either from tool number (or tool number + N, "Add to tool", computed only when the tool is first chosen) or from the tool definition (Control Definition Tool page).

Dialog 10293 "Assembly Has Changed" (246x117): Static "The tool and holder in this operation do not match the tool and holder specified for the selected tool number";
radio-like buttons 10305 "Update assembly and all operations that use the assembly", 10306 "Create a new assembly", 10307 "Cancel assembly changes"; OK (1), Help (9). (When shown: NOT DECODED.)

### 1.4 Tool List Filter (mccoreres dialog 25519)

Opened by "&Filter..." (help `Tool_Manager.htm`). 473x218 DLU, style 0x80C800C0 (WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT), MS Sans Serif 8.

| id | class / style | x,y,w,h | text |
|---|---|---|---|
| 25459 | Button BS_GROUPBOX | 8,8,234,185 | Tool Types |
| 1769,1770,1781,1783,1786,1788,1790 | Button BS_OWNERDRAW (picture toggles) | row y=20, x=19,49,79,109,139,169,199, 30x28 | \|Endmill1 Flat, \|Endmill2 Sphere, \|Endmill3 Bull, \|Face mill, \|Radius mill, \|Chamfer mill, \|Slot mill |
| 1792,1793,1795,1798,1800,1802,1803 | same | y=49 | \|Taper mill, \|Dove mill, \|Lollipop mill, \|Drill, \|Reamer, \|Bore, \|Tap RH |
| 1804,1806,1808,1810,1813,1827,1830 | same | y=78 | \|Tap LH, \|Center Drill, \|Spot Drill, \|CBore, \|CSink, \|Undefined, \|Engrave tool |
| 1833,1834,1835,1836 | same | y=107, x=19,49,79,109 | \|Bradpt Drill, \|High Feed Mill, \|Thread Mill, \|Barrel Mill |
| 1815 / 1817 | Button | 71,141,50,14 / 127,141,50,14 | &All / &None |
| -1 / 2497 | Static / ComboBox DROPDOWNLIST | 34,161,63,8 / 34,173,87,41 | Operation masking |
| -1 / 2502 | Static / ComboBox DROPDOWNLIST | 131,161,45,8 / 130,173,86,40 | Unit masking |
| 1358 | Button | 7,198,50,14 | &Reset all |
| 1771 | Button BS_GROUPBOX | 250,8,214,38 | Tool Diameter |
| 1772 / 1776 / 1779 | ComboBox / Edit / Edit | 258,24,80,67 / 344,24,52,14 / 403,24,52,14 | |
| 1868 | GROUPBOX | 250,54,214,34 | Radius Type |
| 1876 / 1871 / 1874 | BS_AUTOCHECKBOX | 258,70 / 321,70 / 382,70 (55x10) | None / Corner / Full |
| 1761 | GROUPBOX | 250,96,214,84 | Tool Material |
| 1764,1767,1819 | BS_AUTOCHECKBOX | x=272, y=111/126/140 | HSS / Carbide / Coated carbide |
| 1821,1822,1824 | BS_AUTOCHECKBOX | x=357, y=111/126/140 | Ceramic / Borzon / Unknown |
| 1856 / 1858 / 1860 | Button | 260,158,50,14 / 316,158 / 372,158,82,14 | A&ll / Non&e / Copy job &setup matl |
| 1 / 2 / 9 | BS_BITMAP | 348/388/428,196,36,18 | \|&OK / \|&Cancel / \|&Help |

Help `~/mastercam-re/chm/mccore/Tools/Tool_List_Filter_dialog_box.htm`: select tool-type pictures (hover shows the type); drop-downs filter on
"whether they have already been selected for an operation or whether they are inch/metric"; "Use the Tool Diameter drop list ... Select an option from the list
to begin creating the criteria"; checkboxes for material / radius type. The combo item texts (2497, 2502, 1772), the defaults, the type-button → tool-type map and the
exact filter predicate are decoded in §1.5.6.

### 1.5 Native behaviour (decoded)

Source: Ghidra decompile of `~/mastercam-re/files/mccore.dll` (project `~/mastercam-re/ghidra_core`, `-noanalysis -readOnly`,
`tools/ghidra_scripts/DumpDecomp.java` / `VtableDump.java` / `StringRefs.java`), plus `objdump -d` where the decompiler dropped call
arguments. Addresses are mccore.dll virtual addresses (image base 0x180000000). `FUN_xxxxxxxx` are Ghidra auto-names. String ids are
mccoreres string-table ids (`tools/pe_strings.py`). "info+N" means the struct that `TlToolMill` vtable slot +0xa8 fills into a 656-byte
caller buffer (its type name is NOT DECODED). "tp_tool+N" means the `tp_tool` that `CMillToolListCtrl::GetToolBySlot(int, tp_tool&)` returns.

#### 1.5.1 Dialog class, launch, controls

- `RunMillToolManager` (0x1803de7b0) calls `RunToolLibrariesMgr(1, 1, get_MainFrame())` (0x18087c970). That function shows a warning
  (`warn_not_avail`) if `GetProdEnableBits() & 6`. Otherwise, if `OMgetActiveMachineGroup(1)` is non-null, it builds the dialog
  `FUN_180870420` = `MCDialog(1099, parent, …)` with vftable `CToolListBoth::vftable` (0x180df7898; RTTI `.?AVCToolListBoth@@`),
  calls `FUN_18087cff0(dlg, 1, 1)`, and runs `DoModal`. `FUN_18087cff0` stores the two bytes at dlg+0x47278 (**part list editable**)
  and dlg+0x47279 (**library list editable**). Both are 1 here. The objdump at 0x18087ca17 shows the constant `mov $0x1,%r8b`.
- DoDataExchange `FUN_1808730f0` (vtable slot 66) binds the controls to members. 25276→+0x1700 (machine-group combo);
  25330/25331→+0x1550/+0x1628; 10253→+0x33a8 (`UILabel`); 25299/25079/25225→+0x3680/+0x4640/+0x5600; 25297→+0x65c0 (view combo);
  25457→+0x8268; 25456→+0x8340; 25193→+0x8418; 25325→+0x84f0 plus DDX_Check int +0x85c8 (part Filter Active);
  25263→+0x85d0 plus DDX_Check int +0x86a8 (library Filter Active); 25460/25461→+0x86c0/+0x9680 (Up/Down);
  **25463→+0xa640 = library list**; **25464→+0x28c10 = part list** (both `CMillToolListCtrl`); DDX_Radio 10408→int +0x86b8.
- Message map (AFX_MSGMAP entries at 0x180df7c90, read with a PE reader; getter `FUN_1808749d0` = vtable slot 12):

| message / notify | control | handler | what it does (decoded below) |
|---|---|---|---|
| NM_SETFOCUS | 25464, 25463 | `FUN_180879ed0` | Focusing one list clears the selection and focus state (`SetItemState(i, ~3, 3)`) of every selected item in the **other** list |
| LVN_COLUMNCLICK | 25464, 25463 | `FUN_180879e50` | `CMillToolListCtrl::SortList(iSubItem)` on that list |
| NM_DBLCLK | 25464 | `FUN_180878bd0` | part double-click (§1.5.4) |
| NM_DBLCLK | 25463 | `FUN_180878b90` | library double-click = the Up button (§1.5.4) |
| WM_CONTEXTMENU | — | `FUN_1808777f0` | right-click menu 1225 (§1.5.5) |
| LVN_KEYDOWN | 25464 / 25463 | `FUN_180879d10` / `FUN_180879c10` | keys (§1.5.4) |
| LVN_BEGINDRAG | 25464 / 25463 | `FUN_180876f00` / `FUN_180876ed0` | drag (NOT DECODED) |
| WM_SIZE, WM_GETMINMAXINFO | — | `FUN_18087a9c0`, `FUN_1808790f0` | layout on resize (§1.5.9) |
| BN_CLICKED 25193 | Filter... | `FUN_18087c640` | §1.5.6 |
| BN_CLICKED 25325 / 25263 | Filter Active | `FUN_18087a280` / `FUN_180879e10` | §1.5.2 |
| CBN_SELCHANGE 25297 | view combo | `FUN_18087a050` | §1.5.2 |
| CBN_SELCHANGE 25276 | machine group | `FUN_180879150` | §1.5.2 |
| BN_CLICKED 25460 / 25461 | Up / Down | `FUN_18087bc00` / `FUN_180878d90` | §1.5.4 |
| BN_CLICKED 10408-10410 | Display mode | `FUN_180878d40` | §1.5.2 |
| BN_CLICKED 25079 / 25299 / 25225 | Browse / New / Import | `FUN_180876f70` / `FUN_1808789a0` / `FUN_1808791c0` | §1.5.8 |

Menu 1225 commands are not in the message map. They are handled in the `OnCommand` override `FUN_180877120` (vtable slot 63).

#### 1.5.2 Dialog defaults and control effects

OnInitDialog is `FUN_180879770` (vtable slot 95). In order:
1. `FUN_180874e60` fills the **machine-group combo** (25276). It adds every group in `TpMainGrpMgr`'s main list that has op_group+0x10c == 0
   and op_group+0x134 == 1. The item text is op_group+4 and the item data is the op_group pointer. The selection is the previous selection if
   the combo already had items. Otherwise it is the root group of the active group (`RetrieveRootGroup`). It also stores
   `OMgetGrpMachine(active group)` at dlg+0x472b0.
2. `FUN_180875040`:
   - Filter Active: dlg+0x85c8 and part-list+0x1e370 are set from the static byte `DAT_181431680`. dlg+0x86a8 and library-list+0x1e370
     are set from `DAT_181431681`. Both bytes are in the zero-initialised part of .data, so **both Filter Active boxes start unchecked**
     in a session. They keep the last state because the click handlers write the bytes back.
   - `CToolFilt::ReadPrm` runs for both lists' filters. part-list+0x1e374 = 0, library-list+0x1e374 = 1. The second flag is the
     filter's "ignore operation masking" flag (§1.5.6).
   - Library tool list: `ToolLibrary(…)` → `TlToolLibraryX::GetTools` → `TpLibToolList` at dlg+0x471e0. Its manager becomes the library
     list's object. The part list's object is `GetInternalToolSystem()` vtable +0x1c8.
   - Per-list flags (list+0x6a8..0x6ab): +0x6a8 = 1 and +0x6a9 = 1 for both lists (`0x101`). +0x6aa = 0 (part) / 1 (library), the
     "is library" flag. +0x6ab = `GetProdEnableBits() & 0x40`.
   - Part list group: `InitMachGroup(id)`, where id = first int of the selected combo item's op_group. Library list: `InitMachGroup(-1)`.
   - Then `InitColumns` and `InitBitmaps` run on both lists.
3. `FUN_18087c200` reads the registry. The key is built from `get_REG_KEY` plus the globals `DAT_181431690/98/a0/a8`: mccore's static
   initialisers (objdump 0x180004114-0x1800041ab) set them to `"GUI"`, `"Mill Tool Manager"`, `"Part Tool List"` and `"Lib Tool List"`
   (2026-10-22). Key `REG_KEY\GUI\Mill Tool Manager` holds the two values below; the lists use `...\Part Tool List` and
   `...\Lib Tool List`. The user's Mastercam X4 hive (NTUSER.DAT, `Mastercam X4\GUI\Mill Tool Manager\Lib Tool List`) has the same names.
   - `"Tool Manager Mode"` gives the view combo index (dlg+0x47200). It is accepted if ≤ 2. **Default 0.**
   - `"Tool Manager Display Mode"` gives the Display-mode radio (dlg+0x86b8). It is accepted if < 3. **Default 2 (= Both).**
   - It then calls `CMillToolListCtrl::ReadRegistrySettings` for the part list and the library list (two different subkeys).
4. `SetListDisplayMode(library list, radio)`, then `FillList(-1)` on both lists.
5. `FUN_180875440` fills the **view combo** 25297 with string 5414 «Both», 5460 «Part Tools» and 5464 «Library Tools» (in that order) and
   selects the index from step 3.
6. Label 10253 ← `CCNCMachine::ToolLibFile(machine, Units())`. The button bitmaps are Up 0x61aa, Down 0x61a9, Browse 0x15a7, New 0x61c2 and
   Import 0x61b2, with transparency colour 0xC0C0C0. Tooltips use string ids 0x230e-0x2312 on 25299, 25079, 25460, 25461 and 25225.
7. `FUN_18087ca80` copies the active group's tool-library file to `<file>bup` (`DAT_180df8188` = "bup") with `CopyFileA`, and clears
   the "library modified" byte dlg+0x47202.
8. `FUN_18087a050` (the view-combo handler) applies the visibility below.

**View combo** (`FUN_18087a050`). It stores `CB_GETCURSEL` in dlg+0x47200 and calls `ShowWindow`:

| index | part group (25276 combo, 25330, 25331, 25325, 25457, part list) | library group (10253, 25299, 25079, 25225, 25263, 25456, library list) | Up/Down 25460/25461 | Display mode 10407-10410 |
|---|---|---|---|---|
| 0 Both | shown | shown | shown | shown |
| 1 Part Tools | shown | hidden | hidden | hidden |
| 2 Library Tools | hidden | shown | hidden | shown |

It then sends WM_SIZE to the dialog. Filter... (25193) is not in either group, so it is always shown.

**Display mode radios** (`FUN_180878d40`): `UpdateData(TRUE)`, then `SetListDisplayMode(library list, radio)` and `FillList(-1)` on the
**library list only**. The part list keeps the constructor value 2. Mode values: 0 = Tools, 1 = Assemblies, 2 = Both. In `FillList`, an
assembly whose byte +0x58 ≠ 0 is a bare tool row. Those rows show `  --  ` for the assembly name and the holder name. Bare-tool rows are
listed when mode ≠ 1, and real assemblies (+0x58 == 0) when mode ≠ 0. Each display mode has its own column set (§1.5.3). Switching saves
the current widths and order into the old set and applies the new set (`SetListDisplayMode` 0x18050abe0).

**Filter Active** (`FUN_18087a280` part / `FUN_180879e10` library): `UpdateData(TRUE)`, then copy the check value into list+0x1e370 and
into `DAT_181431680`/`DAT_181431681`, then `FillList(-1)` on that list.

**Machine-group combo** (`FUN_180879150`): part-list+0x6ac ← selected group id, then `FillList(-2)` on the part list (no reselection).

**Settings saved**: `FUN_18087d480` (the `_MCAssert` string "CToolListBoth::WriteSettings") writes `"Tool Manager Mode"`,
`"Tool Manager Display Mode"` and both lists' `WriteRegistrySettings`. It is called from `FUN_18087a250` and `FUN_180876f30` (vtable
slots 97/98). `FUN_18087a250` ends in `UIDialog::OnOK` and `FUN_180876f30` in `UIDialog::OnCancel` (decompile), so they are OnOK / OnCancel:
- **OnOK** (`FUN_18087a250`): `FUN_18087c0e0` (below), WriteSettings, `OnOK`.
- **OnCancel** (`FUN_180876f30`; also Esc and the close box, standard MFC): if "library modified" (dlg+0x47202) `FUN_1808753c0` (restore the
  backup), else `FUN_180875370` (delete the backup); WriteSettings; `OnCancel`. **Cancel therefore undoes every library change of the session.**
- **`FUN_18087c0e0`** ("keep the library?"; also run by Browse / New / Import before they switch library). If the library was modified:
  `message_box_yes_no(3843 «Save changes to your current library?», 3842 «Save Library?», 2)` (ids from objdump 0x18087c10a-0x18087c121; the
  callee 0x18001e8b0 is `message_box_yes_no`). No → `FUN_1808753c0`. Yes → `get_file_version` of the backup file; if it is a Mastercam file
  older than the running version (`DAT_18143d54a`), `message_box_yes_no(308 «You are about to overwrite a previous version library…», 310 «Save
  Library File», 2)`, and No → `FUN_1808753c0`. Otherwise `FUN_180875370`. Not modified → `FUN_180875370`.
- The backup: `FUN_18087ca80` stores the library path in dlg+0x47208 and `<path>bup` in dlg+0x47210, `CopyFileA`s the library to it and clears
  dlg+0x47202. `FUN_1808753c0` = `CopyFileA(bup → library)`, `DeleteFileA(bup)`. `FUN_180875370` = `DeleteFileA(bup)`.
- Every library-changing handler sets dlg+0x47202 = 1 (checked in the decompiles of `FUN_180873c50`, `FUN_180871670`, `FUN_180873980`,
  `FUN_180873600`, `FUN_180873360`, `FUN_1808727a0`, `FUN_18087cb50`, Down and library paste).

#### 1.5.3 List columns (`CMillToolListCtrl`)

`InitColumns` (0x1805097e0) inserts 13 columns with `LVM_INSERTCOLUMNA`. `ColumnCount` = 13, see `GetColumnVisible` bound `< 0xd`.
Default widths come from `GetDefaultColumnWidth` (0x508020) in pixels. Sort keys come from `SortColumn` (0x18050b060).

| col | header string id (text) | default width | cell value (`FillList` 0x180506ca0) | sort key (`SortColumn`) |
|---|---|---|---|---|
| 0 | none (LVCFMT_CENTER, mask 0xf) | 0 (hidden) | item text, used in the icon views: `"%3ld - %s, %s"` of info+0, the Dia. string and the Type string. Item lParam = tool slot. Item image = MC tool type + 0x34 (assembly, unused), + 0x4e (assembly, used), + 0 (bare tool, unused), + 0x1a (bare tool, used) | constant (all equal) |
| 1 | none | 25 | image only. Assembly: 4 if used by an operation, else 5. Bare tool: 0 if used, else 1. In the operation manager, when tp_tool+0x468 ≠ 0: 3 − used. Image lists: §1.5.3 "Image lists" | assembly byte +0x58 (bare tool > assembly) |
| 2 | 3759 «#» | 40 | `"%3ld"` (fmt at 0x180d709d8) of info+0 (tool number) | tp_tool+0 (int) |
| 3 | 12128 «Assembly Name» | 80 | `TlAssembly::GetName`, or `"  --  "` for a bare tool | item text, MFC compare (Ordinal_2857) |
| 4 | 3764 «Tool Name» | 80 | `TlObject::GetName(tool)`, truncated to 0x78 bytes | item text |
| 5 | 12127 «Holder Name» | 80 | main holder `GetName`, or `"  --  "` for a bare tool | item text |
| 6 | 3794 «Dia.» | 50 | `FormatReal(info+0x8, 1, 1, 0)`. If the short info+4 is 4 or 5: `"%s-%s"` with the second part `FormatReal(floor((info+0x18+0.005)*100)/100, 1, 0, 0)`. Else, if `GetMCToolType` is 12, 16, 17 or 21: `"%s-%s"` with the second part `FormatReal(info+0x20, 1, 0, 0)` | tp_tool+0x8 (double) |
| 7 | 3791 «Cor. rad.» | 50 | `FormatReal(info+0x10, 1, 1, 0)` | item **text** |
| 8 | 3781 «Length» | 50 | `FormatReal(TlToolMill+0x800, 1, 1, 0)`. +0x800 is "FL" in `GetProfileParams` (tool_profiles.md §4.2) | tp_tool+0x290 (double) |
| 9 | 3792 «# Flutes» | 50 | `"%hi"` (0x180d709e8) of the short TlToolMill+0x858 | tp_tool+0x52 (short) |
| 10 | 3761 «Type» | 50 | string id `Cnc::Tool::tl_string_chooser(MCToolType, list+0x6ab ? 0x40 : 8)` (id map in `decomp/mccore_toolfilt.c` l.5907 ff.; the mill-product texts, mccoreres 5014..5412, are `viewer/toollib.c` `tool_type_name`; the 0x40 branch (ids 0xf82.. for types 10..) is not used here) | item text |
| 11 | 3793 «Rad. Type» | 50 | string id 3798 + info+6 (3798 «None», 3799 «Corner», 3800 «Full») | constant |
| 12 | 3801 «Tool Projection» | 50 | `FormatReal(TlToolMill+0x7f0, 1, 1, 0)`. +0x7f0 is "OAL" in `GetProfileParams` (tool_profiles.md §4.2) | constant |

Field offsets for columns 8, 12 and 9 are from objdump at 0x1805078ff (`movsd 0x800(%rax)`), 0x180507a5d (`0x7f0`) and 0x180507981 (`0x858`),
with rax = the TlToolMill. The `FormatReal` arguments (value, a, b, n) come from objdump: r8b = a, r9b = b, [rsp+0x20] = n = r13d = 0.

**Number format** (`FormatReal` 0x1803255b0, `fmt_real` 0x1803698f0):
- If a ≠ 0: `fmt_real` writes `"%.*lf"` and strips trailing '0' characters. FormatReal sets the global `n_places_nci` to n only when n > 0,
  so here the current `n_places_nci` applies. The exact precision argument of that sprintf is not visible in the decompile: NOT DECODED.
- If `GetProdEnableBits() & 1` and `DAT_1813e91a0 == 0`, `fmt_real` first rounds the value to 3 significant digits.
- |v| ≥ 1e38 prints `$$$$$$$$$$$$$$$$$$$$`.
- After that, if the string ends in '.': b = 0 removes the '.', b = 1 appends "0" (`DAT_180cf7cd4` = "0").

**Default visibility** (`ResetListCtrlColumns` 0x18050a7f0). There are three column sets, one per display mode (`GetActiveColumnSet` =
this+0x1e3a8+mode·0xb0). Each entry is {bool visible, int width}, followed by an order array. Every set starts with width = default width,
visible = (width ≠ 0) and identity order. Then:
- set 0 (Tools) hides col 3, col 5 and col 12;
- sets 1 and 2 hide col 12;
- col 11 is hidden in all sets only when list+0x6a9 == 0. CToolListBoth sets it to 1, so «Rad. Type» is visible.

So with the defaults, the part list (mode 2) shows # … Rad. Type without Tool Projection, and col 0 is width 0.
Hiding a column sets width 0 and moves it to order slot 1. Showing it gives it the default width and moves it to the last order slot
(`SetColumnVisibilityOnData` 0x18050aa70). Hidden columns cannot be resized (`OnHeadingBeginTrack` 0x509f30). Column 0 can never be
resized (`OnNotify` 0x18050a270 blocks HDN_BEGINTRACKA/W for item 0).

**Column chooser** (`VisibleColumnPopupMenu` 0x18050b500, from header NM_RCLICK → `OnHeadingRightClick`). It is a popup with command ids
2..12 = columns 2..12, ticked when visible. The item texts are the header string ids 3759, 12128, 3764, 12127, 3794, 3791, 3781, 3792, 3761, 3793 and
3801, in that order (objdump 0x18050b55c-0x18050b718: `mov $id,%edx` before each `LoadString`). In display mode 0, items 3, 5 and 12 are greyed. Item 11 is greyed
when list+0x6a9 == 0. Columns 0 and 1 are not listed. A choice toggles the column (`ToggleColumnVisible`).

**Sort.** The constructor sets every set to column 2, ascending. Defaults applied when the registry is missing (`ReadRegistrySettings`)
are view mode 1 (Details) and sort column 2 ascending in all three sets. `SortColumn` returns cmp × (+1 if ascending else −1). Numeric
columns compare with the global `tol`. Two paths set the sort column:
- `SortList(col)` (0x18050b3e0), used by LVN_COLUMNCLICK and the Arrange menu: the same column toggles the direction, a new column starts **ascending**.
- `OnHeadingItemclick` (0x509f60, HDN_ITEMCLICKA in the list control's own message map at 0x180d708d0), columns 2..12 only: the same column
  toggles the direction, which is kept in that column's `LVCOLUMN.cxDefault`. A new column starts with flag 0, i.e. **descending**.

Which of these two runs for a header click (or whether both do) is NOT DECODED.

**Every CMillToolListCtrl list in the app (N13-32, 2026-11-14).** Each dialog that owns one routes LVN_COLUMNCLICK to SortList(iSubItem)
in its own message map, so every tool list sorts on a header click as decoded (no deviation was needed):

| list | owner (message map) | column-click handler | settings kept | ours |
|---|---|---|---|---|
| part 25464 / library 25463 | CToolListBoth (see above) | FUN_180879e50 | GUI\Mill Tool Manager\Part / Lib Tool List | toolmanager.conf, `tm_sortlist` |
| op dialog Tool page 25463 | ToolPageBase 0x180df98c0 | OnListClickColumn 0x180889340 | GUI\Mill Tool Display\Tool List Settings (at OK) | toolpage.conf, `tp_sortlist` |
| Tool List 6410, 1762 | CToolList 0x180df6000 | 0x18086bdb0 | GUI\Mill Tool Library Selection (+ \Tool List Settings; on close) | toollist.conf, `tl6410_sortlist` |
| Edit Common Parameters 243, 395 | CEditCommonDlg (mastercam.dll) 0x1805cfa10 | 0x180143b10 | none (mastercam.dll imports no Read/WriteRegistrySettings) | `ecp_sortlist` |

The Tool Manager's icon views have no header: they show the same rows in the same SortColumn order (Arrange menu 32942-32945). Not lists
of tools in this sense: the Toolpaths manager tree (one tool line per operation, in operation order), the tool reports (menu 1225
Report / Detailed report), the test / MCP dumps (`tools`, `list_tools`: the part's tool table in file order).

**Registry per list** (`ReadRegistrySettings` 0x18050a3f0 / `WriteRegistrySettings` 0x18050b870). Value names:
- `"List Style"` = view mode; valid values 0, 1, 3;
- `"DisplayMode%d  SortInfo"` = ±column (+ is ascending; valid when 1 ≤ |v| ≤ 12);
- `"DisplayMode%d  Column%dOrder"` (≤ 12), `"…Column%dVisible"` (0/1) and `"…Column%dWidth"`, for modes 0..2 and columns 0..12.

The double space comes from the format `"DisplayMode%d "` followed by `"%s SortInfo"`. Any missing or invalid value switches to the
defaults above.

**View modes** (`SetViewMode` 0x18050af10, menu IDs in §1.5.5):
- 0: `LVM_SETEXTENDEDLISTVIEWSTYLE` 0x400 (LVS_EX_INFOTIP), normal image list;
- 1: ext style 0x32 (LVS_EX_SUBITEMIMAGES | HEADERDRAGDROP | FULLROWSELECT), small image list;
- 3: small image list.

The context menu reads the window style back with `GWL_STYLE & 3`: 0 = Large Icons, 3 = Small Icons, otherwise Details.

**Image lists** (`CMillToolListCtrl::InitBitmaps()` 0x1805091c0, the overload `FUN_180875040` calls for both lists; `ImageList_Create`
via `Ordinal_3042(list, cx, cy, ILC_COLOR32 = 0x20, initial, grow)`, bitmaps from mccoreres with `LoadBitmapW`, added with `ImageList_Add`
in this order):

| member | size | bitmaps (mccoreres BITMAP ids, in order) | set by `SetViewMode` as |
|---|---|---|---|
| list+0x1e380 | 16 x 16 | 25046, 25045, 1339, 1242, 10325, 10326 (one image each) | `LVSIL_SMALL` in mode 1 (Details) |
| list+0x1e388 | 32 x 32 | 5798, 10338, 10323, 10336 (26 images each), 1241 | `LVSIL_SMALL` in mode 3 (Small Icons) |
| list+0x1e390 | 96 x 96 | 10324, 10337, 10322, 10335 (26 images each), 1240 | `LVSIL_NORMAL` in mode 0 (Large Icons) |

The strips are 833 x 32 and 2496 x 96 pixels in the user's mccoreres (`tools/extract_icons.py` pack), i.e. 26 cells, one per MC tool type
0..25 (cell 0 is the "?" of type 0). So the column-1 image of the Details view is 25046 (bare tool, used = 0), 25045 (bare, unused = 1),
10325 / 10326 (assembly used / unused = 4 / 5) and 1339 / 1242 (2 / 3, the operation-manager case); the item image of the icon views
(MC type + 0 / 26 / 52 / 78) is cell *type* of 5798 / 10338 / 10323 / 10336 (32 px) or 10324 / 10337 / 10322 / 10335 (96 px). 1241 /
1240 are image 104 (not referenced by `FillList`). The `InitBitmaps(int,int,int,int,int)` overload (0x180508d80) is not used by this dialog.

**Which rows are listed** (`FillList`), for each assembly of the list's `TlMgr`:
1. The main tool is present (`FUN_180044fc0(asm+0xa8)`), and the display-mode rule above passes.
2. `CToolFilt::TestToolOk(filter key, tool)` passes (§1.5.6). The key is `in_operation_mgr ? DAT_181377a14 : 0`.
3. Part list only (list+0x6aa == 0):
   - when list+0x6ac ≥ 0, info+0x274 must equal the list's machine-group id;
   - inside the operation manager with an MCExtensions machine, `CCNCMachine::ValidMtOpTool(…)` must pass.
4. When list+0x6aa or list+0x6ab ≠ 0 (always true for the library list), these extra exclusions apply:
   - MC type 20 is hidden outside the operation manager;
   - types 3 and 22 are hidden when tp_tool+0x3d0 == 3;
   - inside the operation manager, key 0x132 shows only type 20 and other keys hide type 20.

The "used" flag comes from `ToolsUsedInOperations(TlMgr, map<slot,bool>)`, and only for the part list. Afterwards `SortList()` runs.
FillList(n): n == −1 reselects the slots that were selected before, n ≥ 0 selects slot n, n == −2 selects nothing.

#### 1.5.4 Up / Down, double-click, keys, delete

- **Up = "Add a Library Tool to Part"** (`FUN_18087bc00`; library double-click calls the same function). For each selected library item:
  1. Look up the assembly. If it is missing or has no main tool: `message_box_ok(6512 «Tool not found», 6510 «Tool Libraries Error»)`.
  2. Clone the main tool (asm vtable +0x78).
  3. `FUN_18087d330`: `CCNCMachine::ValidateCoolant` on the tool's pre-X and canned-text coolant.
  4. Set it back as the main tool (`OnSetMainTool`) and call `ResetIDs`.
  5. Call `GetInternalToolSystem()` vtable +0x10 = `ToolSystem` slot 2 `FUN_180a965a0` with (assembly, selected machine-group id, &declined,
     1, 1, 1, 0). The stack arguments come from objdump at 0x18087bee4-0x18087bf03.
  6. If it fails and was not declined: `message_box_ok(6515 «Unable to add tool to tool list», 6510)`.

  Finally `FillList(-1)` on the part list.

  Inside `FUN_180a965a0`, where group = `OMgetGrpMachineGroup(id)` (or the active group if id == 0):
  - **Numbering**:
    - if the int group+0x11b8 ≠ 0, the tool number becomes the lowest free number: number := 0 and then vtable +600 (§1.5.7);
    - else, if group+0x11bc ≠ 0 (and the 8th argument is 0), vtable +0x288 is called with (number, tp_tool field, group) (meaning NOT DECODED);
    - otherwise the library tool number is kept.
    - The names of the group fields +0x11b8 / +0x11bc are NOT DECODED.
  - **D/H offsets** (7th argument = 1): `Cnc::Tool::tp_tool_offset` (0x1800dc270). If `GetProdEnableBits() & 6` it does nothing. Otherwise
    it asks the group machine `CCNCMachine::AddToToolRegister(1)`:
    - 0: length offset = `LOfsAmt(1)` + tool number and diameter offset = `DOfsAmt(1)` + tool number;
    - 1 (or no machine): keep the tool's own values (tp_tool+0x2c length, tp_tool+0x28 diameter).

    `tp_tool_station` runs after that.
  - **Similar-tool check** (5th argument = 1). ToolSystem slot 59 `FUN_180a9b510` (= vtable +0x1d8) takes the part tool list
    (vtable +0x1c8) and `FUN_180a93b30` collects every assembly of its map (except those `FUN_180a93770(0, asm)` rejects: NOT DECODED) for
    which the lambda (`_Func_impl` vftable 0x180e603b8, slot 2 `FUN_180a9e240` → `FUN_180a95ce0`) returns true. The lambda captures
    (assembly, group id) (`FUN_180a94ff0`) and calls `TlAssembly::IsSimilar(new, candidate, group)` (0x1807172e0):
    1. if group ≠ 0 the candidate's group (`GetMachineGroup` = asm+0x4c) must equal it;
    2. both must have a main tool, and the "bare tool" byte +0x58 must be equal;
    3. for a real assembly (+0x58 == 0) the main holders must match: `TlHolder::operator==(tp_holder)` (0x1807257b0) = same name, same
       +0x130 string, same segment count and every segment's three values within `tol`;
    4. `TlTool::IsSimilar(tool, other, group, 0)` = virtual +0xf8 (vtable slot 31 = 0x18083f720 in the TlToolMill / TlToolEndmill /
       TlToolDrill vftables, found from their RTTI complete object locators) = `TlToolMill::DoIsSimilar`:
       `TlTool::DoIsSimilar` (returns true); same class (`__RTtypeid`); if group ≠ 0 the other tool's +0x138 equals it;
       |FL(+0x800) − FL'| < `tol`; equal stored profiles (`TlObject::operator==` of `GetStoredProfileConst`, NOT DECODED further); then on the
       two `op_tool_info` (+0xa8): +600 metric flag, +4 type, +6 radius type and +0x52 flutes equal, |+8 dia| and |+0x10 corner radius|
       differences < `tol`; +0x18 (pitch) compared when this type is 4 or 24 or the other's is 5; +0x20 (angle) when this type is 16 or 17 or
       the other's is 12 or 21 (the asymmetric tests are as decompiled).

    If it finds matches, `FUN_180a9dbc0` shows `message_box_yes_no_do_not_show_str` with title 5258 «Similar Tool Warning» and text
    5254 «A similar tool (%s) already exists in tool list.  Add another?» (one match; %s = `FUN_180a98f50` = `tl_make_description` of the
    tool being added, below) or 5257 «%d similar tools already exists…».
    The answer defaults to Yes (`DAT_181326564` = 1 in .data). "Don't show again" is the flag `DAT_1814442cd`. If the answer is No,
    the tool is not added and the declined flag is set, so no error box appears.
- **Down = "Add a Part Tool to Library"** (`FUN_180878d90`). It does nothing if there is no active machine group. For each selected part assembly:
  - it is skipped when its main-tool pointer is null;
  - it is skipped when `TpToolList::IndexByToolExists(library list, info, prompt 1, group 0)` returns a non-null `TlID` (objdump
    0x180878e72-0x180878e88: r9b = 1, [rsp+0x20] = r12d = 0);
  - otherwise the assembly is cloned into a vector.

  `IndexByToolExists` (0x1808f45f0) walks every assembly of the list as loaded (dlg+0x471e0; tools added in the same Down are not in it),
  with a main tool and (group == 0 or info+0x274 == group), and counts those with `TpToolList::ToolAttribsMatch(info, theirs)` (0x1808f5480):
  +600 metric flag, +4 type, +6 radius type, +0x52 flutes equal; |+8 dia| and |+0x10 corner radius| ≤ `tol`; |+0x18| ≤ `tol` for types 4 / 5;
  |+0x20| ≤ `tol` for types ≤ 21 with bit set in 0x231000 (12, 16, 17, 21). The tool name is not compared. With ≥ 1 match and prompt set it
  returns the last match's ID unless `UserPromptDuplicateTool(last match, count)` (0x1808f60f0) answers Yes. That box (unless its "don't show"
  flag `DAT_18143db69` is set) is `message_box_yes_no_do_not_show_str` with 5258 «Similar Tool Warning» and 5254 (%s =
  `TpToolList::CreateToolDescription` of the **matching library tool**, below) or 5257 (count); the answer is `DAT_181313644`, initially 1 = Yes
  (.data byte read from the PE). So a duplicate is **added** when the answer is Yes (the default) and skipped on No; this matches the help
  sentence in §1.3 ("Mastercam will still create a new tool") for the default answer.

  Then `TlToolLibraryX::AddAssemblies(ToolLibrary(), vector)`. On success with ≥ 1 added: reload the library (`FUN_18087c600`),
  `FillList(-1)`, and set "library modified" (dlg+0x47202). On failure: 6515 / 6510.

  **Library-side numbering**: `AddAssemblies` (0x1807b2d00) opens the library (`OpenLibrary` → `MakeLibrary` by extension) and, for each
  assembly, `PrepareObjectForStorage` (0x1807b4910: `ResetIDs`, machine group := 0) then library vtable +0x88. For a `.tooldb` that is
  `TlToolLibrarySQL` slot 17 `FUN_1807e7850`: it makes sure the tool's material / manufacturer / grade rows exist and stores the object with
  `FUN_1807ffd10` (`TlToolLibrarySQL::Store`: the object's `GetInsertSQLData` rows). Nothing in this path changes the tool number: **the part
  tool's number is kept** in the library.

  **Descriptions** (`tl_make_description(tp_tool*)` 0x1800db8a0 upper-cases the result with `_mbsupr_s`; `CreateToolDescription` 0x1808f34e0
  does not): by `IsMetric()` (the part's units), inch / metric:
  - type 4: `"#%ld - %.4lf X %.2lf %s - %s"` / `"#%ld - M%.2lf X %.2lf %s - %s"` (number, dia, +0x18, type text, name +0x56);
  - type 5: the same with `" - %s"` of a second string before the name (empty unless tp_tool+0x2c0 == 1);
  - tp_tool+0x2c0 == 1: `"#%ld - %.4lf %s - %s - %s"` with a word taken from string 5023 (`sscanf "%*s%s"`) - meaning of +0x2c0 NOT DECODED;
  - type 20: `"#%ld - %s - %s"` (number, type text, name);
  - else `"#%ld - %.4lf %s - %s"` / `"#%ld - M%.2lf %s - %s"`. The type text is `tl_string_chooser(type, 0)`.
- **Part double-click** (`FUN_180878bd0`): MC tool type 20 opens `BlockselectpgDlg(tp_tool, 1)` and, on OK, `ToolSystem` vtable +0x1a0
  and `FillList(-1)`. Any other type calls `FUN_180874750` = CToolListBoth::EditPartTool (StringRefs: "CToolListBoth::EditPartTool").
- **Keys**:
  - part list (`FUN_180879d10`): Insert → Create new (when dlg+0x47278); Delete → Delete tool(s) (when dlg+0x47278); Ctrl+C with a
    selection → Copy (`FUN_180878770`); Ctrl+V when the clipboard count dlg+0x47290 > 0 → Paste (`FUN_18087a660`);
  - library list (`FUN_180879c10`): the same keys with dlg+0x47279 and `FUN_180871670` / `FUN_1808727a0` / `FUN_180878760` / `FUN_18087a2c0`.
- **Delete tool(s), part** (`FUN_180872b80`):
  1. If any selected tool is used by an operation (`ToolsUsedInOperations` map), and "don't show again" (`DAT_1814317cc`, initially 0) is
     not set, it asks `message_box_yes_no_do_not_show_str`. The title is 3840 «Deleting Tools». The text is 5585 «1 or more tools are used
     by operations» + "\n\n" (`DAT_180cf5448`) + 3841 «Confirm deletion of tool(s)?». The answer is in `DAT_18130c044` (initially 1 = Yes).
  2. Each selected tool that is unused, or all of them if the answer is Yes, is deleted with `ToolSystem` vtable +0x68 (slot, 0, answer, 0).
     A failure shows 996 «Tool could not be deleted» / 6510.
  3. `FillList(-1)`.

  So **unused tools are deleted without a prompt**.
- **Delete tool(s), library** (`FUN_1808727a0`):
  1. `message_box_yes_no(3841 «Confirm deletion of tool(s)?», 3840 «Deleting Tools», 2)`. The ids come from objdump at 0x18087281d; the
     callee 0x18001e8b0 is the export `message_box_yes_no(long,long,short,HINSTANCE,CWnd*)`. If the answer is No, nothing happens.
  2. `TlToolLibraryX::OpenLibrary`. For a `.tooldb`, each selected entry is removed through library vtable +0xb8 (an assembly, with its
     holder) or +0xa8 (a bare tool).
  3. Any failure shows 996 / 6510. Otherwise: reload, `FillList(-1)`, library modified = 1.
- **Delete unused tools** (`FUN_180872f40`, part list only): it takes the slots from `ToolSystem` vtable +0x40 (which list that is: NOT DECODED).
  Each slot with no `TpPartOpList::FirstOpUsingMillTool` is deleted with vtable +0x68. Then `FillList(-1)`. There is no prompt.
- **Create new tool** (part `FUN_180871ad0` / library `FUN_180871670`):
  1. The template is the single selected tool (`tp_tool` copy), or `init_tl_ent(tp, 3, …)` if 0 or ≥ 2 items are selected.
  2. `TlToolMill::CreateNewFromOldTool` → `ToolSystem` vtable +0x90 (define/edit UI, with the type short tp+4).
  3. On OK, part list: vtable +0x18 adds it to the selected group; on failure 997 «Tool could not be added to list» / 6510; on success
     `FillList(new slot)`. The library path after +0x90 is NOT DECODED.

#### 1.5.5 Right-click menu 1225 (`FUN_1808777f0`)

The menu is loaded as resource 0x4c9 = 1225 (`Ordinal_2140(0x4c9, 4)`). The list under the cursor sets dlg+0x4727a: 0 = part list
(+0x28c10), 1 = library list (+0xa640). Every list command is routed to that list (`OnCommand` `FUN_180877120`). "sel" = there is a
selected item (`LVM_GETNEXTITEM` ≠ −1). "editable" = dlg+0x47278 for the part list, dlg+0x47279 for the library list.

| id | item | part list | library list | handler (part / library) |
|---|---|---|---|---|
| 32933 | Create new tool | enabled if editable | enabled if editable | `FUN_180871ad0` / `FUN_180871670` |
| 32935 | Edit tool | enabled if editable and sel | enabled if editable and sel | `FUN_180874940` / `FUN_180873c50` |
| 33217 | Edit holder | enabled if editable and sel. Replaced by a «Edit composite holder» (12131) popup when `FUN_180871e80` returns component entries. Ids 12145-12150 also route here | same | `FUN_180874550(-1 or index)` / `FUN_180873980` |
| 33229 | Edit projection | removed unless `FUN_1800d7fd0()`; else enabled if editable and sel | same | `FUN_180874360` / `FUN_180873600` |
| 33215 | Edit assembly names | enabled only if editable, sel, and the assembly is real (+0x58 == 0) | also requires `FUN_1808752a0(dlg)`: the library's extension (after the last '.' that follows the last '\\') is `"tooldb"` (`CString::Compare`, case-sensitive) | `FUN_180874170` / `FUN_180873360` |
| 32937 | Delete tool(s) | enabled if editable and sel | enabled if editable and sel | §1.5.4 |
| 32938 | Delete unused tool(s) | enabled | **disabled** | `FUN_180872f40` |
| 32939/32940/32941 | View Large / Small / Details | checked from the style; sets view mode 0 / 3 / 1 on that list, then `FillList(-1)` | same | `SetViewMode` |
| 32942/32943/32944/32945 | Arrange by Number / Type / Name / Diameter | checked when the sort column is 2 / 10 / 4 / 6 | same | `SortList(2/10/4/6)` |
| 32947 | Copy tool(s) | enabled if sel | enabled if sel | `FUN_180878770` / `FUN_180878760` (§1.5.8) |
| 32948 | Paste tool(s) | enabled if clipboard count dlg+0x47290 ≥ 1 | same | `FUN_18087a660` / `FUN_18087a2c0` (§1.5.8) |
| 33003 | Load tool(s) on machine | removed unless the machine is `FromMCExtensions`; enabled if editable and sel and not `inMDM` | **disabled** | `LoadToolsOnMachine(part list, 0, machine, dlg)` (always the part list) |
| 32946 | Save tool(s) to library | enabled if sel | enabled if sel | `FUN_18087cb50` (§1.5.8) |
| 32949/32950 | Convert to / from text | removed unless `FUN_1800d8ec0()` | same | `FUN_1800d96f0` / `FUN_1800d9600` (from-text then reloads the library and the label) |
| 33080 | Import from Machining Cloud | removed when the `GetToolDataImporterManager()` count is 0. If importers exist, a popup titled string 255 «Import tool data from» with one item per importer (`FUN_180872470`) is inserted at that position | same | id 33080 itself does nothing (`case 0x8138: break`). The importer ids go to `FUN_180874a20(index)` |
| 32951 | Compress | **disabled** | enabled | `FUN_180872660` (§1.5.8) |
| 32952/32953 | Report / Detailed report | — | — | `FUN_180875590(dlg, 0 / 1, active list)` (the second value is decompiled as 0x180000001) (§1.5.8) |

The menu is shown with `TrackPopupMenu` (flags 2) at the WM_CONTEXTMENU point.

#### 1.5.6 Tool List Filter (`CToolFilt`, dialog 25519)

Each `CMillToolListCtrl` owns one `CToolFilt` at list+0x6b8. Filter... (`FUN_18087c640`) runs the **part list's** filter `DoModal`
(vtable +0x2f0). On IDOK it runs `ReadPrm` on the library list's filter, sets library-list+0x1e374 = 1, and re-runs `FillList(-1)` on
each list whose Filter Active box is checked.

**Controls** (`CToolFilt::DoDataExchange` 0x180863470, `MapSettingsToDlg` 0x1808641e0):
- 2497 (op masking): 5356 «Used by operations», 5357 «Not used by operations», 5358 «No operation masking».
- 2502 (unit masking): 4958 «English», 4960 «Metric», 5355 «No unit masking».
- 1772 (`MCCombo`, 5 strings): 2040 «Ignore», 2043 «Equal», 2048 «Less Than», 2052 «Greater Than», 2061 «Between». Edit 1776 holds value 1
  and edit 1779 holds value 2. For selections 1-3 only 1776 is shown, for selection 4 both, for selection 0 neither (`OnSelchangeDiaCmb`).
- At run time the material checkboxes are relabelled with `SetDlgItemText`: 1764 ← 3711 «HSS», 1767 ← 3712 «Carbide»,
  1819 ← 3713 «Ti Coated», 1821 ← 3714 «Ceramic», 1822 ← 3715 «User Def 1», 1824 ← 3716 «User Def 2». These replace the resource
  texts listed in §1.4.
- Radius checkboxes: 1876 None, 1871 Corner, 1874 Full.
- When the "ignore op masking" flag is set (library list), combo 2497 is disabled.

**Tool-type buttons → MC tool type** (`CtlToolType` 0x180863270):

| 1769→10 | 1770→11 | 1781→19 | 1783→13 | 1786→15 | 1788→12 | 1790→14 |
|---|---|---|---|---|---|---|

| 1792→17 | 1793→16 | 1795→18 | 1798→3 | 1800→6 | 1802→7 | 1803→4 |
|---|---|---|---|---|---|---|

| 1804→5 | 1806→1 | 1808→2 | 1810→8 | 1813→9 | 1827→20 if `ProdEnableBits & 0x40`, else 0 | 1830→21 |
|---|---|---|---|---|---|---|

| 1833→22 | 1834→23 | 1835→24 | 1836→25 |
|---|---|---|---|

**Defaults** (`InitSettings` 0x1808640f0 with key 0):
- all 26 type flags on, all 6 material flags on, all 3 radius flags on;
- unit and op masking set to "no masking" (combo index 2);
- diameter min −1e38 / max +1e38, i.e. «Ignore».

Reset (`OnResetPb`) restores the same values. `OnInitDialog` calls `AllTrueToFalse`: a group whose flags are all on is **shown with
nothing ticked**, and `OnOK` → `AllFalseToTrue` turns the reverse back into "all on".

**Persistence**: `ReadPrm`/`WritePrm` use the file name "toolfilt". Byte 7 becomes 'm' when `IsMetric()`, giving "toolfilm". The
`prm_prefix`/`prm_suffix` path parts are NOT DECODED. The file holds a 0x2c-byte header, then 0xe0-byte `OpFilter` records keyed by a short
(the operation key; `in_operation_mgr ? DAT_181377a14 : 0`).

**Diameter → [min, max]** (`GetDlgFilterSettings` 0x1808638c0):

| selection | min | max |
|---|---|---|
| Ignore | −1e38 | +1e38 |
| Equal | v | v |
| Less Than | −1e38 | v − tol |
| Greater Than | v + tol | +1e38 |
| Between | min(v1, v2) | max(v1, v2) |

**Predicate** (`TestToolOk` 0x180865900). A tool row is shown when any one of these holds:
- the list's Filter Active (list+0x1e370) is 0;
- there is no `OpFilter` for the key.

Otherwise all of these must hold:
1. type flag[`GetMCToolType`] is on;
2. unless list+0x1e374 (library): op masking 0 requires `FirstOpUsingMillTool(slot)` ≠ null, and 1 requires null;
3. unit masking 0 hides metric tools and 1 hides inch tools (`TlAssemblyItem::GetIsMetric`);
4. min ≤ TlToolMill+0x808 ≤ max. +0x808 is "D" in tool_profiles.md §4.2;
5. radius flag[info+6] is on;
6. if info+0x54 > 0, material flag[info+0x54 − 1] is on, in the order HSS, Carbide, Ti Coated, Ceramic, User Def 1, User Def 2 (via the
   `OpFilter` layout: radius flags at +0xb8, materials at +0xc4).

#### 1.5.7 `TpToolMgr::GetOpenToolNumberAllLists(long& n, long group, long p3)` (0x1808f9150)

It forwards to `GetInternalToolSystem()` vtable +600 = `ToolSystem` (vftable 0x180e600b8) slot 75 `FUN_180a9c9c0` → `FUN_180a9c610`.
`ToolSystem` (RTTI `.?AVToolSystem@Tool@Cnc@@`) is identified as the `IInternalToolSystem` implementation for two reasons: it is the only
concrete class with that name in the RTTI strings, and the parameter list of its slot 2 (`FUN_180a965a0`) matches the Up call site.
The constructor `FUN_18076b650` in `InitToolSystem` was not followed to confirm this.

The algorithm:
1. n := max(n, 1).
2. Walk all mill assemblies (vtable +0x40) whose machine group ≠ 9999 and (group < 1 or the assembly group == group). Then walk all lathe
   tools (vtable +0x48), with group at +0x138 and number at +0x130.
3. If any of them already has number n, set n := n + 1 and restart the walk. The result is the **lowest number ≥ the start that no tool in the
   group uses**.
4. The p3 flag (the 4th argument) restricts the check to tools used by an operation (`FirstOpUsingMillTool`/`…LatheTool`). For a type-2
   machine it also checks that operation's turret.
5. Mill tools of type 3/22 with tp_tool+0x3d0 == 3 are ignored.

Up (§1.5.4) calls it with start 0 → the lowest free number ≥ 1.

#### 1.5.8 Clipboard, library files, compress, report (decoded 2026-10-15)

Sources: the decompiles in `decomp/toolmgr/tm_native/c.c` (all `FUN_18086f…`-`FUN_18087d…`) and `decomp/toolmgr/tm2/{a,b,c,d,e}.c` (Ghidra
`DumpDecomp.java` on `TlToolLibraryX::*`, `TpToolList::*`, report writers, `TlToolLibrarySQL` slots), vtables from `VtableDump.java`
(`decomp/toolmgr/tm2/vt.txt`). String ids are mccoreres (`tools/pe_strings.py`). (Copied from the session scratch folders
`tm_native/` and `tm2/`. Also kept there: `decomp/toolmgr/tm_native/{a,b,d,e,f}.c`, `vt.txt`, `vt2.txt`, `strings.tsv`,
`sr.txt`, `sec15.md`; `decomp/toolmgr/tm2/{f,g}.c`, `vt2.txt`, `vt3.txt`, `add.md`, `oldblock.txt`.)

**Copy tool(s)** (`FUN_180878770` part / `FUN_180878760` library → `FUN_180878780(dlg, list)`). The dialog's own clipboard is the vector
dlg+0x47280 (entries of 0x18 bytes: `shared_ptr<TlAssembly>` + a valid byte at +0x10; dlg+0x47290 = size, dlg+0x472a8 = count). It is emptied,
resized to `LVM_GETSELECTEDCOUNT`, and for every selected item (`LVM_GETNEXTITEM(…, LVNI_SELECTED)` order) the assembly is cloned
(`TlAssembly::Clone`) and marked valid; a missing assembly shows 6512 «Tool not found» / 6510 and is marked invalid. The clipboard lives in
the dialog object, so it ends with the dialog.

**Paste tool(s)**:
- part (`FUN_18087a660`): for each valid entry with a main tool (`FUN_180044fa0(+0xb8)`, `FUN_180044fc0(+0xa8)`): clone, `ResetIDs`, clone the
  main tool, `FUN_18087d330` (coolant), `OnSetMainTool`, then `ToolSystem` slot 2 with (assembly, the machine-group combo's group id,
  &declined, 1, 1, 1, 0) = exactly the Up path (§1.5.4: numbering, offsets, similar-tool check). Failure and not declined: 6515 / 6510.
  Any success → `FillList(-1)` on the part list.
- library (`FUN_18087a2c0`): needs an active machine group; each valid entry with a main tool goes through `IndexByToolExists(dlg+0x471e0,
  info, 1, 0)` (objdump 0x18087a42b-0x18087a444) and, when not found / accepted, is cloned (`FUN_18086e4c0`) into a vector; then
  `AddAssemblies(ToolLibrary(), vector)` = exactly the Down path.

**Save tool(s) to library** (`FUN_18087cb50`). Default name: `ToolLibrary(active group, dir, file, ext)`, `"Untitled"` joined with that dir and
extension (`FUN_18002b490`). `TlToolLibraryX::BrowseForToolLibraryToSaveOrAppend` (0x1807b3a30) = `PromptForFileToWriteOrAppend(name, ".tooldb",
653 «Save to library», 5005 «Tool library», …)`: a Save dialog (OFN flags 0x108, no overwrite prompt); a name without extension gets the first
filter extension. Then the selected assemblies of the list the menu was opened on (dlg+0x4727a) are cloned (`GetSelectedAssemblies`) and
`AddAssemblies(file, clones)` is called: an existing file is **appended to**, a new one is created by `OpenLibrary`; there is no duplicate check.
Failure: 6515 / 6510. If the file is the current library (`_stricmp`), reload, `FillList(-2)` on the library list and library modified = 1. The
library label is refreshed in every case.

**Compress** (`FUN_180872660`, library list only). `OpenFile(path, dlg, 1)` (0x180351990 → 0x1803512b0: Mastercam's file-open dialog, title
string 7283 «Open», preset to the current library `ToolLibrary()`). If it returns 1: a missing file → 5593 «Error - file not found» / 6510;
else `OpenLibrary` and library vtable +0xe0 = `TlToolLibrarySQL` slot 28 `FUN_1807e8f80`: `FUN_1807fed50` (a read-only file → `MCMessageBox`
857 «Warning» / 8987 «Cannot write to tool library because the file is marked read-only.», fail), `"COMMIT"` (on error `"ROLLBACK"`),
`"VACUUM;"`, `"BEGIN TRANSACTION;"`; success = the VACUUM result. Failure → 6513 «Tool Library compression failed» / 6510.

**New Library** (25299, `FUN_1808789a0`). Default name "Untitled" + `tool_suffix` in the current library's folder.
`TlToolLibraryX::BrowseForToolLibraryToSave` (0x1807b38c0) = `PromptForFileToSave(name, "r", ".tooldb", 992 «Enter name for new tool library:»,
5005 «Tool library», …, false)`. On OK: an existing file is **deleted** (`RemoveFile`), `OpenLibrary` creates it; failure → 6517 «Tool Library
could not be created» / 6510. Success: `FUN_18087c0e0` (§1.5.2, for the old library), `SetToolLibrary(path, active group, true)`, reload,
`FillList(-1)` on the library list, label = `CCNCMachine::ToolLibFile`, new backup (`FUN_18087ca80`).

**Browse Libraries** (25079, `FUN_180876f70`). Needs an active group. `TlToolLibraryX::BrowseForToolLibraryToOpen` (0x1807b3750) =
`PromptForFileToOpen(current library, "r", ".tooldb.tools-9.tools-8.tools-7.tools-6.tools-5.tools", 25549 «Select tool library:», 2794 «Mastercam
Tool Files», …)`. Only a file different from the current one (`CString::Compare`) is opened: `OpenLibrary`; on success `FUN_18087c0e0`,
`SetToolLibrary(path, group, false)`, reload, `FillList(-2)`, new backup, label, `Ordinal_13906(dlg, 0)`. The meaning of `SetToolLibrary`'s
third argument (true for New / Import, false here) is NOT DECODED.

**Import Tools** (25225, `FUN_1808791c0`). `PromptForFileToOpen(DAT_1814316b0 (the last import file, empty at
first), "rb", ".mcam" (".emcam" for some product bits / user types), 25550 «Select file for tool import:», 25537 «Mastercam Files», …)`. The
proposed library name is that file with the current library's extension (`file_new_extension`). If `GetMastercamFileVersion(name)` reports a
file older than the running version → 3887 «The selected library is from a previous version of Mastercam and cannot be edited…» / 5005, stop.
`PromptForFileToSave(name, "w", ext, 992, 25531 «Tool library», …, true)`, `OpenLibrary(name)` (failure → 6517 / 6510), `GetTools(part file)`
into a `TpToolList`; when it has tools, every assembly (`GetAssemblyIDs` + `Find`) goes through library vtable +0x88, stopping at the first
failure; vtable +0x28 (slot 5 `FUN_1807e8f00`: `COMMIT`, close). On success: `FUN_18087c0e0`, `SetToolLibrary(name, group, true)`, reload,
`FillList(-2)`, label, new backup, `DAT_1814316b0` := the part file. Whether the empty-tool-list case still switches libraries depends on a
register the decompiler merged (cVar4), NOT DECODED.

**Report... / Detailed report...** (`FUN_180875590(dlg, detailed, list)`). First `FUN_18087c6b0`: every item of the list (`LVM_GETITEMCOUNT`,
`GetItemData`, `GetAssemblyBySlot`, clones of those with a main tool, in list order) → `FUN_1806168b0(assemblies, name, detailed + 1, filter,
is library)` where name = `geo_filename` (part list) or `ToolLibrary()` (library list) and filter = that list's Filter Active (dlg+0x85c8 /
+0x86a8). `FUN_1806168b0`: `GetToolMatlRpx(true, 0, kind)` (0x180615c10: product 0 = the mill names whatever the machine; kind 1 =
`report_mill_tools_simple`, 2 = `report_mill_tools_detailed`, the configuration values 31006 «TOOL LIST (MILL-SIMPLE)» / 31007 «TOOL LIST
(MILL-DETAILED)» by name; the path is `"%sMSC\"` of `reports_prefix` + name + `.rpx`); `FUN_1806405a0` checks the file; Ctrl held → `FUN_180616580`
(an options step, NOT DECODED); `FUN_18061b5c0` = `WriteXML_MillToolList` writes root `TOOLS` with `LIBNAME` (name), `FILTER` (`AddNode_YN`),
`AddNode_DATE`, `SORT` (5360 «No»: its sort variable is only set for kind 3) and per assembly `FUN_18061bb40` → `FUN_180620e40` →
`FUN_180649320` (the same per-tool writer as the Toolpaths-manager tool list; the operation is `FirstOpUsingMillTool(slot)`); then
`RunActiveReportsViewer`. When that path returns false the rest of `FUN_180875590` writes a text report: `PromptForFileToWriteOrAppend` with
string 3772 «TOOLLST» (+ `txt_suffix`), an existing file asks 3779 «Append to existing file? (Yes = Append, No = Overwrite)», then a header
and one line (simple) or a `name = value` block (detailed) per tool - the fprintf arguments are not visible in the decompile: NOT DECODED.

#### 1.5.9 Layout on resize (`FUN_18087a9c0` = OnSize, `FUN_1808790f0` = OnGetMinMaxInfo)

`FUN_1808790f0` sets `ptMinTrackSize` (MINMAXINFO +0x18 / +0x1c) to `MapDialogRect(0, 0, 600, 350)`: the template size is the minimum.
`FUN_18087a9c0` runs after `UIDialog::OnSize` when both lists exist. Units are pixels. m = dlg+0x4721c = 10 and m2 = dlg+0x47218 = 5 (the
constructor, `decomp/toolmgr/tm_native/c.c` line 7721); C = the client rect (left = top = 0); mid = (C.top + C.bottom) / 2. Each control is moved
with `Ordinal_8353(wnd, x, y, w, h = its current height, 0)` (argument shape of `CWnd::MoveWindow`); "keep x" = its current x; sizes are kept
unless given. rw = 2m + width of the view combo 25297; col = C.right + m − rw (the right column).

| control | Both (dlg+0x47200 = 0) | Part Tools (1) | Library Tools (2) |
|---|---|---|---|
| 25276 machine combo | (m, m) | (m, m) | hidden |
| 25330 bitmap | (keep x, m); ytop = 2m + its height | same | hidden |
| 25331 label | (keep x, m2 + m) | same | hidden |
| 25297 view combo | (C.right − rw, m) | same | same |
| 25325 part Filter Active | (col, ytop + 40) ¹ | (col, ytop + 40); b = its new bottom | hidden |
| 25263 library Filter Active | (col, mid + 100) | hidden | (col, top2 + 40); b = its new bottom |
| 25193 Filter... | (col, m + h(25263) + 100 + mid) | (col, b + m) | (col, b + m) |
| 10407 group box | (col, m + g), g = h(25193) + 120 + mid | hidden | (col, m + g), g = 80 + h(25193) + top2 |
| 10408-10410 radios | x = col + 5, same offset from the box as before | hidden | same |
| 10253 path label | (m, mid − m) | hidden | (m, m); top2 = 2m + its height |
| 25299 / 25079 / 25225 | (keep x, mid − m); hh = max height of these and 10253, halved | hidden | (keep x, m) |
| 25460 Up | (col, max(dlg+0x47220, mid − m − its height)) | hidden | hidden |
| 25461 Down | (col, hh + mid + m) | hidden | hidden |
| 25464 part list | (m, ytop, C.right − 2m − rw, mid − m − ytop − hh) | (m, ytop, C.right − 2m − rw, C.bottom − m − ytop) | hidden |
| 25463 library list | (m, m + hh + mid, C.right − 2m − rw, C.bottom − 2m − hh − mid) | hidden | (m, top2, C.right − 2m − rw, C.bottom − m − top2) |
| 25457 "(Part)" | right edge at C.right − 2m − rw; y = m | same | hidden |
| 25456 "(Library)" | right edge at C.right − 2m − rw; y = mid − m | hidden | right edge at C.right − 2m − rw; y = m |
| 9 Help | (C.right − m − w, C.bottom − m − h) | same | same |
| 1 OK | (C.right − 2m − w(Help) − w, C.bottom − m − h) | same | same |

¹ In "Both", dlg+0x47220 is set to the bottom read **before** the move (after the first layout this equals the new bottom); in the other
modes the new bottom is stored. The whole client is invalidated afterwards.

#### 1.5.10 This program (viewer/toolmgr_ui.inc)

The dialog is implemented in `viewer/toolmgr_ui.inc` (included by mcamui.c). Done from the decodes above: menu 1225 Copy / Paste / Save tool(s) to
library / Compress / Report / Detailed report / View Large-Small-Details; the keys Insert / Delete / Ctrl+C / Ctrl+V on the focused list (the
dialog is modal: while it is open no key reaches the graphics window's keymap, so Delete never deletes geometry); New / Browse / Import; the
similar-tool prompts of Up / paste (DoIsSimilar) and Down / paste (ToolAttribsMatch) with their default answers and "don't show" flags; the
OnOK / OnCancel library backup rules and the «Save Library?» question; the resizable window with the §1.5.9 layout and minimum size; the
image lists of §1.5.3 (icon pack names `mccoreres_BMP_<id>.png`); the settings of §1.5.2 / §1.5.3 ("Tool Manager Mode", "Tool Manager Display
Mode", per list "List Style", "DisplayMode%d  SortInfo", "…Column%dOrder / Visible / Width", the same validity rules) stored in
`<config dir>/toolmanager.conf` (never the registry or Mastercam's files); column widths can be dragged on the header (not column 0, not hidden
ones); the column chooser moves columns as `SetColumnVisibilityOnData` does.

Differences and gaps (ours, with reason):
- the backup `<library>bup` is made just before the first write of the session instead of at open (the same file results), so opening a library
  never writes next to it (INTENTIONAL, see "Backup timing" below);
- the icon views' item spacing and label wrapping are ours (the system list-view metrics are NOT DECODED);
- the "Don't show this again" check box text belongs to `message_box_yes_no_do_not_show_str` (mastercam.dll), NOT DECODED: our label;
- `tol` is configuration 1000 «System tolerance»;
- Detailed report: the template's subreport `-Setup Sheet (MILL-TOOL).rpx` is in `reports\SST\`, which our rpx reader (same folder) does not
  find, and `FUN_180649320` writes more fields than the ones decoded for the Toolpaths-manager list (CHUCK, DESCRIPTION, FLUTES, MATERIAL, …),
  NOT DECODED here: the detailed report shows its header only;
- the text-report fallback of `FUN_180875590` (no template) is NOT DONE (a status message instead);
- Import Tools: the 3887 «previous version» check is not made (what `GetMastercamFileVersion` reports for a `.tooldb` is NOT DECODED); an
  existing target file is appended to;
- column reordering by dragging headers (LVS_EX_HEADERDRAGDROP), drag and drop between the lists (LVN_BEGINDRAG): NOT DONE;
- menu 1225 (2026-10-16) is drawn with its real cascades (View > 32939-32941, Arrange tools > 32942-32945, Import/export tools >
  32949 / 32950 / 33080 / 32951-32953) as top-level windows beside the menu; Edit holder, Edit projection (33229), Edit assembly names,
  Load tool(s) on machine (33003), Convert a library to / from text (32949 / 32950) and Import from Machining Cloud (33080) are shown
  greyed (not ported). Mastercam removes some of these by product / machine (§1.5.5); here they are always shown;
- the registry keys are decoded (§1.5.2 step 3); toolmanager.conf uses them as name prefixes instead of the registry;
- `n_places_nci` of FormatReal is NOT DECODED: the configured places (default 4) are used;
- Tool List Filter: one settings set for both lists; the owner-drawn type / material pictures and control 1860 are NOT DECODED (captions
  shown); the machine-group combo holds one group;
- double-click on a block drill (type 20): its dialog is NOT DONE; a new library tool's path after `+0x90` is NOT DECODED;
- at first open with no library, the library named in the part's first operation record (op +0x416) is opened when that file exists.

Code map (2026-10-21, formats/qt_port_plan.md phase 7: the model below is `viewer/core_ui/cui_toolmgr.c` (dialog state, actions,
prompts, library file rules, filter, menu 1225 as `tm_menu_items`, `tm_layout`, reports, Renumber) and `cui_toolwiz.c` (the wizard
window's model); the column sets / cells / `tlib` / `tm_open_lib` are `cui_toolpage.c`; `viewer/toolmgr_ui.inc` draws it with
nuklear and `viewer-qt/src/toolmgr.cpp` / `toolwiz.cpp` with Qt; `tm_script` is now `cui_tm_script`):

| Function | Behaviour | Mastercam function |
|---|---|---|
| `tm_settings_read` / `tm_settings_write` / `tm_cs_reset_list` | toolmanager.conf (below); any missing / invalid value of a list resets that list to the defaults | FUN_18087c200 + ReadRegistrySettings 0x18050a3f0 / WriteSettings FUN_18087d480 + 0x18050b870 / ResetListCtrlColumns 0x18050a7f0 |
| `tm_col_toggle` | hide = width 0, order slot 1; show = default width, last slot | SetColumnVisibilityOnData 0x18050aa70 |
| `tm_filter_ok`, `tmf_defaults`, `draw_tool_filter` | Filter Active test, defaults, dialog 25519 | TestToolOk 0x180865900, InitSettings 0x1808640f0 |
| `tm_cell`, `tm_real`, `tm_cmp` / `tm_sortlist` | column texts, FormatReal, sort | FillList 0x180506ca0, SortColumn 0x18050b060 / SortList 0x18050b3e0 |
| `tm_lib_backup` / `tm_lib_finish` / `tm_lib_write` | `<library>bup` copy; restore (copy back, delete) or drop; every library change written at once (.tooldb via `toollib_save_tooldb`, else .tools) and marks the library modified | FUN_18087ca80 / FUN_1808753c0 / FUN_180875370 |
| `tm_job_*`, `tm_up` / `tm_down`, `tm_copy` / `tm_paste` | add tools one at a time with the similar-tool prompts (`tm_is_similar`, `tm_attribs_match`) | FUN_18087bc00 / FUN_180878d90 / FUN_180878780 / FUN_18087a660 / FUN_18087a2c0; DoIsSimilar 0x18083f720, ToolAttribsMatch 0x1808f5480 |
| `tm_delete*`, `tm_create`, `tm_edit` | delete (part: confirm when used), delete unused, create / edit through the wizard | FUN_180872b80 / FUN_1808727a0 / FUN_180872f40 / FUN_180871ad0 / FUN_180871670 |
| `tm_save_prompt`, `tm_ok`, `tm_cancel`, `tm_switch_lib`, `tm_browse` / `tm_new` / `tm_import`, `tm_save_to`, `tm_compress`, `tm_fd_accept` | «Save Library?», OK / Cancel, library buttons and file commands | FUN_18087c0e0, FUN_18087a250, FUN_180876f30, FUN_180876f70 / FUN_1808789a0 / FUN_1808791c0, FUN_18087cb50, FUN_180872660 |
| `tm_report` | Report / Detailed report via the rpx templates | FUN_180875590 → FUN_18087c6b0 |
| `tm_key` / `tm_keydown` | list keys; modal key capture | FUN_180879d10 / FUN_180879c10 |
| `tm_list`, `tm_item_image`, `tm_draw_menu`, `tm_draw_colmenu`, `tm_layout`, `draw_tool_manager` | list control, image lists, menu 1225, column chooser, OnSize layout, the dialog | InitBitmaps 0x1805091c0, FUN_1808777f0, VisibleColumnPopupMenu, FUN_18087a9c0 / FUN_1808790f0 |
| `tm_script` | `MCAMUI_SCRIPT` test steps after `show:tm` (copy / paste / key / ans / view / mode / disp / col / sort / report / compress / saveto / new / browse / import / ok / cancel / size / dump) | ours (test hook) |

**`<config dir>/toolmanager.conf` (INTENTIONAL, ours - Mastercam keeps these in its registry, `formats/provenance_audit.md`).**
Plain text, one `name=value` per line (value = integer; the name is everything before the last `=`; unknown lines ignored). Rewritten
in full when the dialog closes (`tm_close_now`). Names, with the accepted values (else the default):
- every name starts with the decoded key path `GUI\Mill Tool Manager\` (relative to REG_KEY; since 2026-10-22, older files with bare
  `Tool Manager ...` / `Part Tool List\...` / `Library Tool List\...` names are read and rewritten with the new names);
- `Tool Manager Mode` 0 Both / 1 Part / 2 Library (default 0); `Tool Manager Display Mode` 0 Tools / 1 Assemblies / 2 Both (default 2);
- per list K = `Part Tool List` / `Lib Tool List`: `K\List Style` 0 Large Icons / 1 Details / 3 Small Icons; per display mode m = 0..2
  `K\DisplayMode<m>  SortInfo` = ±column (1..12; negative = descending) and per column c = 0..12 `K\DisplayMode<m>  Column<c>Order`
  (0..12), `…Column<c>Visible` (0 / 1), `…Column<c>Width` (>= 0). Note the two spaces after `DisplayMode<m>` (Mastercam's
  `"DisplayMode%d "` + `"%s SortInfo"` value-name pattern).

**Backup timing (INTENTIONAL, ours).** Mastercam copies `<library>bup` when the dialog opens (FUN_18087ca80, OnInitDialog step 7, §1.5.2)
and after New / Browse / Import switch libraries (§1.5.8); here `tm_lib_backup` makes the same copy just before the first write of the session (`tm_lib_write`), so opening a
library never writes next to it. Restore on «Save Library?» No / Cancel and drop on OK are as decoded; switching libraries resets the flag.

#### 1.5.11 Still NOT DECODED (Tool Manager)

- `FUN_180a93770` (which part-list assemblies the similar-tool search skips); stored-profile equality in `DoIsSimilar`.
- tp_tool+0x2c0 and string 5023 in the tool description. (The `tl_string_chooser` id → text map is decoded since: §1.5.3
  column 10.)
- `SetToolLibrary`'s third argument. (The registry key names `DAT_181431690/98/a0/a8` are decoded since 2026-10-22, §1.5.2.)
- `get_file_version` of a `.tooldb` backup (whether the 308 / 310 question can appear for one).
- the empty-part case of Import Tools (merged register).
- the per-tool XML of the detailed report (`FUN_180649320` in full) and the text-report fprintf arguments.
- group fields +0x11b8 / +0x11bc (Up numbering) and vtable +0x288.

## 2. Define Tool / Edit Tool wizard

Citation shorthand used in this section:

- `TW/…` = `~/mastercam-re/dotnet/toolmanager.toolwizard/…` (ILSpy C#). `DB/…` = `~/mastercam-re/dotnet/toolmanager.database/…`. `SH/…` = `~/mastercam-re/dotnet/toolmanager.shared/…`. `TWL/…` = `~/mastercam-re/dotnet/toolmanager.toolwizardlaunch/…`.
- `baml:views/x.baml` = `TW/views/x.baml`, read with `tools/baml_dump.py`. Elements are cited by `Name=` or `x:Uid=`.
- `[Cls.Key]` = the resx string `Key` in `TW/ToolManager.ToolWizard…Cls.resx`, or in the DB/SH resx that has that class name. The English text is given in «guillemets».

### 2.1 How the wizard is launched (Mastercam → wizard)

| Item | Value | Source |
|---|---|---|
| Create entry point | `RequestInProcess.LaunchCreateToolWizard(isMetric, readOnly, parent, coolantInfo, doff, loff, tlno, isDrilling…)` builds `CreateToolWizardParameters` with `IsReadOnly=readOnly`, `ParentWindowPtr=parent`, `AddToDiamterOffsetValue=doff`, `AddToLengthOffsetValue=loff`, `ToolNumberDefault=tlno`, `MetricMode=isMetric`, `InitialPage=ToolTypePage(0)`, and `DefaultToolType = isDrilling ? 3 (Standard drill) : 10 (Flat endmill)` | TWL/RequestInProcess.cs `LaunchCreateToolWizard` |
| Edit entry point | `LaunchEditToolWizard(editTool, addToLoff, addToDoff, startPageNumber, coolantInfo, readOnly, parent)`: `MetricMode` = the tool's IsMetric. `InitialPage` = 1 → DefineGeometryPage, 2 → PropertiesPage, anything else → 0 | TWL/RequestInProcess.cs `LaunchEditToolWizard` |
| Values of tlno / loff / doff / isDrilling passed by Mastercam | NOT DECODED (native caller) | — |
| Parameter defaults | `WizardParameters`: IsReadOnly=false, MetricMode=false. `ToolWizardParameters`: InitialPage=ToolTypePage, AddToDiamterOffsetValue=0, AddToLengthOffsetValue=0. `CreateToolWizardParameters`: DefaultToolType=10, ToolNumberDefault=0. `EditToolWizardParameters`: InitialPage=DefineGeometryPage | TW/ToolManager.ToolWizard.Models/*.cs ctors |
| Show | `WizardManager.LaunchToolWizard` → `ToolWizardViewModel.InitializePageViewModels(parameters)` → `new WizardView(parent)` → `ShowDialog()` (modal) | TW/…Services/WizardManager.cs `LaunchToolWizard`, `ShowToolWizardDialog` |
| Result | If `DialogFinished`, returns `ToolServiceReturn(DefineGeometryPage.EntitySelected.Entity as TlToolMill)`. Otherwise it returns an empty `ToolServiceReturn` (Cancel = no tool) | WizardManager.cs `ShowToolWizardDialog` |
| Finish / Cancel | `FinishCommand` sets `DialogFinished=true`. `CancelCommand` sets `DialogFinished=false`. `HelpCommand` calls `HelpBrowser.Show(HelpTopicId)` | TW/…ViewModels/WizardViewModel.cs |
| Help topic | `HelpTopicId = 75005` | ToolWizardViewModel ctor |
| Saving | The wizard itself saves nothing. It hands the edited `TlToolMill` back to the caller. What Mastercam does with it is NOT DECODED here | WizardManager.cs |

### 2.2 Wizard window (shell)

| Item | Value | Source |
|---|---|---|
| Window class | `WizardView : RibbonWindow`, `x:Uid=WizardWindow` | baml:views/wizardview.baml root, TW/…Views/WizardView.cs |
| Window properties | `ResizeMode="NoResize"`, `SizeToContent="WidthAndHeight"`, `IsGlassEnabled=False`, `PersistWindowState=True` (ControlId 69D98D4C-71D7-433C-B4DB-E1D78644CAF2) | wizardview.baml root |
| Title | `DocumentName="{Binding WizardTitle}"`. The inner wizard also has `Wizard.WindowTitleBehavior="PageTitle"` | wizardview.baml root; toolwizardview.baml root |
| Title strings | Create: «Define Tool» [WizardStrings.ToolWizardTitle_Generic_Text]. Edit: «Edit Tool» [WizardStrings.EditToolWizardTitle_Text]. Read-only: «{0} (Read Only)» [WizardStrings.WizardTitle_ReadOnlyFormat_Text], where {0} is the create or edit title | ToolWizardViewModel.InitializePageViewModels(Create/Edit…) |
| Owner / modality | `WindowInteropHelper.Owner = parent HWND`. On Loaded it calls `SetForegroundWindow`. On Closing it calls `EnableWindow(parent, true)`. `OnStateChanged` forces `WindowState=Normal` (the window cannot be minimised or maximised) | WizardView.cs |
| Layout (ControlTemplate of `AeroWizard`, Style `WizardStyle`) | Grid with 2 columns (`200`, `*`) and 2 rows (`*`, `Auto`). Column 0 is the sidebar `ListBox` plus a 1-px right border `Rectangle`. Column 1 is `WizardPagePresenter PART_TransitionPresenter`, **Width=800 Height=520**. Row 1 (ColumnSpan 2) is `WizardButtonContainer PART_ButtonContainer` | wizardview.baml `Style x:Key=WizardStyle` / `Grid_1`, `ColumnDefinition_1/2`, `PART_TransitionPresenter` |
| Sidebar header | TextBlock «Current Step:» [WizardStrings.CurrentStepTextBlock_Text], Margin 10, Bold, FontSize 12 | wizardview.baml `TextBlock_2` |
| Sidebar items | One per wizard page, showing the page `Title` (FontSize 12). `SelectedItem` is two-way bound to the wizard's `SelectedPage`, so clicking an item jumps to that page | wizardview.baml `DataTemplate_19`, `ListBox_1` |
| Sidebar item enable rule | Item i is enabled if i==0, or if page (i-1) has `NextButtonEnabled==true` | TW/…Converters/WizardSidebarItemEnabledConverter.cs `Convert` (MultiBinding `MultiBinding_1`: Binding1 = item, Binding2 = `Wizard.SelectedPage.NextButtonEnabled`) |
| Buttons | Back «Back», Next «Next», Finish «Finish», Cancel «Cancel», Help «Help» [WizardStrings.BackButton_Text / NextButton_Text / FinishButton_Text / CancelButton_Text / HelpButton_Text]. Cancel, Finish and Help are visible | toolwizardview.baml root `Wizard.*ButtonText`, `*ButtonVisible` |
| Page transition | 0.25 s forward and backward. `PageSequenceType=Normal` | toolwizardview.baml root |
| Enter key in edit boxes | Enter (Key 6) commits that box's binding (`UpdateSource`) and is marked handled unless Ctrl is held. So plain Enter does not trigger Finish. This applies to boxes with `WizardEditBoxBehavior.SuppressFinishOnEnter=True` and to boxes wired to `TextBox_KeyDown` | TW/…Behaviors/WizardEditBoxBehavior.cs `OnEditBoxKeyPressed`; WizardPageViewModel.TextBox_KeyDown |

### 2.3 Pages in order

`enum ToolWizardPages { ToolTypePage, DefineGeometryPage, PropertiesPage }` (TW/…ViewModels/ToolWizardPages.cs). Pages declared in baml:views/toolwizardview.baml: `ToolTypePage`, `DefineGeometryPage`, `PropertiesPage`.

| # | Page | Title (sidebar / window) | Caption | Back / Next / Finish | Source |
|---|---|---|---|---|---|
| 1 | Tool type | «Select Tool Type» [ToolTypePageViewXaml.WizardPageToolTypeTitle_Text] | «What type of tool would you like to create?» [ToolTypePageViewXaml.WizardPageToolTypeCaption_Text] | Back disabled, Next enabled, Finish disabled | baml:views/toolpages/tooltypepageview.baml root |
| 2 | Define geometry | «Define Tool Geometry» [ToolDefinePageViewXaml.Page_Title] | `{Binding WizardPageTitle}` = «Define {0}» [WizardStrings.ToolWizardTitle_Partial_Text], with {0} = the tool-type label (§2.4) | Next = `CanNext`, Finish = `CanFinish` | tooldefinepageview.baml root; each *PageViewModel ctor |
| 3 | Properties | «Finalize Properties» [PropertiesPageViewXaml.Title_Text] | «Finalize miscellaneous properties.» [PropertiesPageViewXaml.Caption_Text] | Next visible but disabled, Finish = `CanFinish` | baml:views/propertiespageview.baml root |

Page rules:

- In edit mode (`!IsCreating`) the ToolType page is removed from the wizard on load. After that, `StartPage != 0` executes `WizardCommands.GoToPage(StartPage)`. Source: TW/…Views/ToolWizardView.cs `ToolWizardOnLoaded`.
- When leaving the ToolType page, the code takes `defaultDatabaseManager.GetDefaultTool(selectedType, isMetric)` and wraps it. It then rebuilds DefineGeometryPage and PropertiesPage only if the page VM type differs or the `MCToolType` differs. A new tool type therefore resets geometry to that type's defaults. Source: ToolWizardViewModel.PreviewSelectedPageChanging.
- `CanNext` is always true. `CanFinish` is `!IsReadOnly` (WizardPageViewModel). Every page gets the wizard's IsReadOnly. `IsErrorState` changes re-raise CanNext/CanFinish, but the base rules do not include the error state. So an invalid tool does not disable Finish (GeometryPageViewModel.IsErrorState setter, CanWizardFinish). Source: WizardPageViewModel, GeometryPageViewModel.
- Tool number and offsets (`SetUpViewModelWrapper`): `ToolNumber = toolNumber` (Create: `ToolNumberDefault`; Edit: the tool's ToolNumber or 0). On Create, and on every tool-type change, `LengthOffsetNum = toolNumber + AddToLengthOffsetValue` and `DiameterOffsetNum = toolNumber + AddToDiamterOffsetValue`. On the Properties page, any change to ToolNumber sets `LengthOffsetNum = ToolNumber + addToLoff` and `DiameterOffsetNum = ToolNumber + addToDoff`. Source: ToolWizardViewModel.SetUpViewModelWrapper; ToolPropertyPageViewModel.RaisePropertyChanged.

### 2.4 Page 1 – Select Tool Type

Layout (tooltypepageview.baml):

- TextBlock «Select the tool type you wish to create from the list below.» [WizardPageToolTypeSubCaption_Text], margin 0,0,0,20.
- `ListBox LstToolTypeSelection`: Height 400, BorderBrush #999, vertical scrollbar Visible. `WrapPanel` items panel, grouped by `Group`.
- Each group is an `Expander` (IsExpanded=True, style `ExplorerExpander`). Its header is TextBlock FontFamily "Segoe UI", FontSize 15, Foreground #1E3287.
- Each item is a Border 200 px wide, CornerRadius 3, containing a 48×48 Image and a label (Foreground #333).
- Selected item: outer border background #DDEBFD→#C2DCFD, BorderBrush #7DA2CE, inner border #DBEAFD. Hover: #FCFDFF→#EDF5FF, #B9D7FC, inner #F3F8FF.
- `SelectedValuePath="ToolType"`, SelectedValue bound to `SelectedType`. The default `SelectedType = 10`, but Create/Edit set it to the default tool's type (ToolTypePageViewModel ctor; ToolWizardViewModel.InitializePageViewModels).

Item list in display order (TW/…Views.ToolPages/ToolTypePageView.cs `GenerateModelList`; labels from [ToolTypePageViewXaml.*]; icon = `Application.Current.Resources[key]`). The page VM is registered in ToolWizardPageFactory ctor.

| Group | Label | ToolTypeList value | Icon resource key | Page VM / View |
|---|---|---|---|---|
| «Milling» [WizardPageToolTypeGroupMilling_Text] | «End Mill» | 10 FlatEndmill | ImageFlatEndMill48 | EndmillPageViewModel / EndmillView |
| Milling | «Bull Mill» | 19 BullNosedEndmill | ImageBullMill48 | Endmill… |
| Milling | «Ball Mill» | 11 SphericalBallNosedEndmill | ImageSphereMill48 | Endmill… |
| Milling | «Face Mill» | 13 FaceMill | ImageFaceMill48 | Facemill… |
| Milling | «Radius Mill» | 15 RoundoverTool | ImageRadMill48 | RadiusMill… |
| Milling | «Chamfer Mill» | 12 EndmillChamfer | ImageChfrMill48 | ChamferMill… |
| Milling | «Slot Mill» | 14 SlottingToolSaw | ImageSlotMill48 | SlotMill… |
| Milling | «Taper Mill» | 17 TaperedEndmill | ImageTaperMill48 | TaperMill… |
| Milling | «Dove Mill» | 16 DovetailCutter | ImageDoveMill48 | DoveMill… |
| Milling | «Lollipop Mill» | 18 LollipopCutter | ImageLolMill48 | LollipopMill… |
| Milling | «Engrave Tool» | 21 EngravingBit | ImageEngrave48 | EngraveMill… |
| Milling | «Thread Mill» | 24 ThreadMill | ImageThreadMill48 | ThreadMill… |
| Milling | «Barrel Mill» | 25 BarrelMill | ImageBarrelMill48 | BarrelMill… |
| Milling | «High Feed Mill» | 23 HighFeedMill | ImageFeedMill48 | FeedMill… |
| Milling | «Custom Tool» [WizardPageToolTypeGroupHolemakingCustom_Label] | 0 CustomTool | ImageUnknownTool48 | CustomToolPageViewModel / CustomDXFPageView |
| «Holemaking» [WizardPageToolTypeGroupHolemaking_Text] | «Drill» | 3 Standarddrill | ImageDrill48 | SpotDrillPageViewModel / SpotDrillView |
| Holemaking | «Spot Drill» | 2 SpotDrill | ImageSpotDrill48 | SpotDrill… |
| Holemaking | «Center Drill» | 1 CenterDrill | ImageCenterDrill48 | CenterDrill… |
| Holemaking | «Reamer» | 6 Reamer | ImageReamer48 | Reamer… |
| Holemaking | «Countersink» | 9 CounterSink | ImageCounterSink48 | CounterSink… |
| Holemaking | «Counterbore» | 8 CounterBore | ImageCounterBore48 | Counterbore… |
| Holemaking | «Tap» | 4 RightHandedTap | ImageLHTap48 (icon key as written in code) | TapDrill… |
| Holemaking | «Bradpoint Drill» | 22 BradPointDrill | ImageBradpointDrill48 | BradPointDrill… |
| Holemaking | «Bore Bar» | 7 FineBoringTool | ImageBoreBar48 | Boring… |
| Holemaking | «Custom Tool» | 0 CustomTool (listed a second time) | ImageUnknownTool48 | CustomTool… |

Notes:

- Type 5 LeftHandedTap is not in the list. It is reached through the Tap page "Left hand" checkbox (§2.6 Tap).
- The Drill page title uses «Drill» when the type is not 2. Source: SpotDrillPageViewModel ctor.
- `ToolTypeList` enum descriptions such as «Flat Endmill» and «Face Mill / Shell Mill» are in DB/ToolManager.Database.Resources.DatabaseEnumStrings.resx `ToolTypeList_*`. They are used for auto-naming (§2.8).

### 2.5 Page 2 – Define Tool Geometry (common frame)

Frame (baml:views/toolpages/tooldefinepageview.baml):

- TextBlock «Adjust geometric properties used to define the tool shape.» [ToolDefinePageViewXaml.SubCaptionTextBlock_Text].
- Grid with columns `*`, `20`, `400`:
  - **Left** (col 0): `ScrollViewer` (MaxHeight 400, vertical Auto) holding a `ContentControl` that DataTemplates by page-VM type to the per-type view (EndmillView, …). `CustomToolPageViewModel` maps to `CustomDXFPageView`.
  - **Right** (col 2, `StackPanel Name=GAFControl`): bordered (#999) panel with rows `30` and `370`.
- Row 0 is a toolbar (background #F7F7F7) with these controls:
  - `CustomLayer`: Style ImportFromLayerButtonStyle. Command `CustomLevelClick`, icon ImageLinkProfileFromLevel16, tooltip «Import and link to custom geometry from a level.». If `IsMCfSW`: command `CustomSketchClick`, icon ImageProfileFromSketch16, tooltip «Import custom geometry from a sketch.».
  - `SaveToLevel`: command `SaveToLevelClick`, icon ImageLinkProfileToLevel16, tooltip «Save profile geometry to a level and link to that level.». If MCfSW: `SaveToSketchClick`, «Save profile geometry to a sketch.».
  - `CustomFile`: command `CustomFileClick`, icon ImageLinkProfileFromFile16, tooltip «Import and link to custom geometry from a file.». If MCfSW: ImageProfileFromFile16, «Import custom geometry from a file.».
  - `BreakLink`: command `BreakProfileLinkCommand`, icon ImageProfileLinkBreak16, tooltip «Break the link to the custom tool profile source.».
  - `DropCustomProfile`: command `DropCustomProfileClick`, icon ImageRemoveProfile16, tooltip «Reset profile geometry.».
  - SplitButton (icon ImageRenderMastercamTool16, command `RenderMastercamToolCommand`) with menu items «Render Mastercam Tool» and «Render Imported Tool» (ImageRenderImportedTool16, `RenderImportedToolCommand`).
  - Right-aligned CheckBox «Scalable» [WizardStrings.ToolWizardGAFToolbar_Scalable_Label], bound to `EntitySelected.IsScalable`, enabled only when `DropCustomProfile` is enabled.
- Row 1 is `GAFControl3DView Name=GAF` (DataContext `View3D`), the tool preview. When `IsErrorState` is true it is collapsed and `ErrorWindowView` is shown in its place.

Behaviour of the frame:

| Behaviour | Rule | Source |
|---|---|---|
| Preview contents | `toolProfileGenerator.GenerateWithOutline(tool)` → `AddDimensionLines` → `SetProfileSource` → shown in `View3D.MachineModel`. Machine axes are hidden | ToolWizardPageViewModel.UpdateGraphicsWindow; GeometryPageViewModel ctor (`ShowMachineAxes=false`) |
| When it redraws | Whenever the tool VM's `ModelIsDirty` changes. The same change calls `AutoGenerateName()` | GeometryPageViewModel ctor (PropertyObserver on ModelIsDirty) |
| Initial camera | On page load: CameraView "Isometric" then "Top", FitSpacing 15 | TW/…ToolPages/ToolDefinePageView.cs `OnPageLoadedEvent` |
| Validation | `GenerateWithOutline` calls `ToolValidityChecker.CheckToolValidity` (DB/…ProfileUtilities/ToolProfileGenerator.cs:53). An `InvalidToolException` → `ShowErrorMessage(msg)`: error icon (ErrorMessageTypes 2), the message, and `IsErrorState=true`, which swaps the preview for the error panel. Messages and rules are in §2.7 | UpdateGraphicsWindow; GeometryPageViewModel.ShowErrorMessage |
| Dimension line | Focusing an edit box that has `WizardEditBoxBehavior.DimensionType` (or code-behind `DimensionEditboxGotFocus` with the box Name mapped by `SetDimensionLineType(string)`) sets `VisibleDimensionLine` and redraws. Only that one dimension is drawn (length, diameter, radius or angle). Geometry formulas are in `AddDimensionLines` | WizardEditBoxBehavior.OnEditBoxGotFocus; ToolWizardPageViewModel.SetDimensionLineType / AddDimensionLines |
| Profile source caption | CustomDisplayType 1 → «Source:» + file. 2 → «Source: Level: {0}», or «Source: Sketch: {0}» in MCfSW. 3 → «Source: Stored profile». If CustomDisplayType ≠ 0: «Rendered: {0}» / «Outlined: {0}», using «Imported Tool» / «Mastercam Tool» [ToolDefinePageViewXaml.*] | ToolWizardPageViewModel.SetProfileSource |
| Import level / file | `ImportCustomProfile(loader)`. When editing, it first asks the ImportingProfileOverwritingToolParametricData warning (§2.10). After a successful import the view state is reset and the model is marked dirty | ToolWizardPageViewModel.ImportCustomProfile, PromptOverwriteParametricValues |
| Import enable | File: `CanWizardFinish()` (= not read-only). Level and Save-to-level: also need `IsMastercamAvailable`. Sketch buttons: need `IsMcfswAvailable` | GeometryPageViewModel.CanSelect*/CanSave* |
| Reset profile | Enabled when `!IsReadOnly && IsCustom && (MCToolType≠0 or profile has segments)`. Clears TlProfileDatas and sets IsCustom=false, CustomLevel=0, CustomDisplayType=0, ToolFileName="" | ToolWizardPageViewModel.CanDropProfile / OnDropProfileClick |
| Break link | Enabled when finishable, IsCustom, and CustomDisplayType is 1 or 2. Asks the BreakToolProfileLink warning, then sets CustomDisplayType=3 (stored), ToolFileName="", CustomLevel=0 | CanBreakProfileLink / BreakCustomProfileLink |
| Render Mastercam / Imported | Enabled if the profile has segments or IsCustom. Sets `GeneralOptions.RenderImportedToolInWizard` false/true (default true, DB/…Options/GeneralOptions.cs:130), publishes ApplicationOptionsUpdatedEvent, and redraws | OnRenderMastercamTool / OnRenderImportedTool |
| Default focus | Each per-type view focuses `CuttingDiameter` on Loaded (seen in FacemillView). Per-view coverage NOT VERIFIED for every view | TW/…ToolPages/FacemillView.cs `DefineToolPageOnLoaded` |
| Standard sizes | `BulkUpdateProperties(row)`. When editing, it asks the StdSizeOverwritingToolGeometry warning first | GeometryPageViewModel.BulkUpdateProperties |

Field rules common to every geometry edit box:

| Rule | Value | Source |
|---|---|---|
| Control | `TextBox` (Actipro editors in EndmillView) with `IsAutoSelectAllEnabled=True`, in a 3-column grid (`200`, `*`, `80`). The label sits in column 0 and the box in column 2 (80 px wide) | per-type baml, e.g. endmillview.baml `Grid_11` |
| Groups | `UXSharedResources.AdvancedExpander` with `HeaderText`. `BasicContent` is always shown. `AdvancedContent` is the expandable extra part. Its expand UI comes from UXSharedResources (NOT DECODED) | per-type baml |
| Units | Linear values are in inch if the tool `IsMetric=false`, else mm. Angles are in degrees. No unit label is shown in the boxes | ToolEditBoxConverter (no unit text); IsMetric from tool VM |
| Display format | `Math.Round(value, IsMetric ? 5 : 6)` then `Convert.ToString(…, InvariantCulture)` | DB/…Converters/ToolEditBoxConverter.cs `Convert` |
| Input parse | `GAFControlHwndHost.EvaluateExpression(text, IsMetric)` (the native expression evaluator: NOT DECODED). A result < 0, or any exception, makes the binding reject the value (`DependencyProperty.UnsetValue`), so the old value stays. No message is shown | ToolEditBoxConverter.ConvertBack |
| Max | None per field in the converter, except Required pilot diameter (§2.9), which uses `ToolEditBoxConverterWithMax` (value > max → rejected) | DB/…Converters/ToolEditBoxConverterWithMax.cs |
| Post-edit adjustments | Boxes with `NotifyOnSourceUpdated=True` call `ValidateEditFieldInput(Name)`. The per-type rules are in §2.6 | e.g. FacemillView.cs `TextBoxSourceUpdated` |

Corner-type ButtonTabControl images (tooltips from [<View>Xaml.CornerButton*_ToolTip]):

| Resource key | Icon | Tooltip |
|---|---|---|
| NoCornerImage | Icons/Bitmaps.ToolManager/NoCorner16.png | «No Corner» |
| CornerChamferImage | CornerChamfer16.png | «Corner Chamfer» |
| CornerRadiusImage | CornerRadius16.png | «Corner Radius» |
| FullRadiusImage | FullRadius16.png | «Full Radius» |

`CornerTypeList` enum: 0 None, 1 Chamfer, 2 Corner Radius, 3 Full Radius, 4 Roundover (DB/…Resources/CornerTypeList.cs; texts in DatabaseEnumStrings `CornerTypeList_*`).

### 2.6 Per-tool-type Define page fields

Columns: **Label** (resx text; the resx class is the view's `…ViewXaml`), **Box Name**, **Binding**, **Dim** (DimensionType shown on focus). Every value box uses `ToolEditBoxConverter` unless noted. Each per-view section cites its baml file.

The defaults in the subsections come from DB/ToolManager.Database.Utilities.SqlScripts.InsertDefaultsData.sql. `GetDefaultTool(type, metric)` returns the matching row from the Defaults database, or falls back to `toolTypeFactory.GenerateModel` (DB/…Services/DefaultDatabaseManager.cs `GetDefaultTool`). Whether the shipped defaults database equals this script is NOT VERIFIED. Each default is given as **inch / metric**.

Default op params for all types (TlOpParams): SFM adjust 100 %, FPT adjust 100 %. Inch / metric: feed 5 / 120, retract 50 / 1200, spindle speed 1000 / 1000, plunge 25 / 600. Exception: taps (4/5) have feed 50 / 2500. Default ToolNumber, LengthOffsetNum and DiameterOffsetNum are 0 in all rows. These are then overwritten as described in §2.3.

#### End Mill / Bull Mill / Ball Mill (10/19/11)

Source: endmillview.baml, EndmillPageViewModel. The NonCuttingGeometry view follows these fields (§2.6.x).

| Group | Label | Name | Binding | Dim |
|---|---|---|---|---|
| «Overall dimensions» | «Cutting diameter:» | CuttingDiameter | EndmillObject.OverallDiameter | CuttingDiameter |
| | «Overall length:» | OverallLength | EndmillObject.OverallLength | OverallLength |
| | «Cutting length:» | CuttingLength | EndmillObject.CuttingDepth | CuttingLength |
| «Tip / corner treatment» | ButtonTabControl `SelectedIndex = CornerType` (two-way = TlRadiusType 0..3). Tabs: None, Chamfer, Radius, Full radius | | | |
| (Chamfer tab) | «Chamfer distance:» | – | EndmillObject.ChamferLength | ChamferLength |
| (Radius tab) | «Radius:» | – | EndmillObject.CornerRadius | CornerRadius |

ValidateEditFieldInput rules:

- **CuttingDiameter**:
  - When creating, and if the shank was not edited yet, it sets ShoulderDiameter = ArborDiameter = OverallDiameter.
  - If the corner is Full Radius, it sets CornerRadius = D/2.
  - Otherwise, when creating, it calls `ToolConverters.CorrectRadiusOnCornerTypeChange`.
- **Shank/Shoulder diameter**: sets `arborModified`.
- **CuttingLength**: if CuttingDepth > ShoulderLength, it sets ShoulderLength = CuttingDepth.

Defaults:

| Type | D | OAL | CutLen | ShLen | Shank | ShoulderD | TlRadiusType | CornerR |
|---|---|---|---|---|---|---|---|---|
| 10 | 0.5 / 10 | 4 / 75 | 1 / 25 | 1.2 / 25 | 0.5 / 10 | 0.5 / 10 | 0 | 0 |
| 19 | 0.5 / 12 | 3 / 100 | 1 / 25 | 1.2 / 25 | 0.5 / 12 | 0.5 / 12 | 2 | 0.125 / 3 |
| 11 | 0.5 / 12 | 4 / 100 | 1 / 25 | 1.2 / 30 | 0.5 / 12 | 0.5 / 12 | 3 | 0.25 / 6 |

All three have 4 flutes. TaperAngle is stored as 180.

#### Face Mill (13)

Source: facemillview.baml, FacemillPageViewModel, FacemillView.cs.

| Group | Label | Name | Binding |
|---|---|---|---|
| Overall dimensions | «Cutting diameter:» | CuttingDiameter | FaceMillObject.OverallDiameter |
| | «Secondary diameter:» | TipDiameter | FaceMillObject.TipDiameter |
| | «Overall length:» | OverallLength | …OverallLength |
| | «Cutting length:» | CuttingLength | …CuttingDepth |
| Tip / corner treatment | «Corner type:» ComboBox `TipTreatment` | | items: Chamfer, Corner Radius, Full Radius (CornerTypeList 1, 2, 3) |
| | «Chamfer angle:» | TaperAngleFaceMill | …TaperAngle |
| | «Radius:» | FacemillRadius | …CornerRadius |
| Non-cutting geometry | «Shoulder length:» | ShoulderLength | …ShoulderLength |
| | «Upper diameter:» | ShankDiameter | …ArborDiameter |

Behaviour:

- Combo index i sets TlRadiusType = i+1 (`FaceMillSelectionChanged`).
- Enable states by combo index:
  - 0: angle on, radius off, diameter on.
  - 1: angle on, radius on, diameter on.
  - 2: angle off, radius on, diameter off.
- Validation:
  - **TipDiameter / FacemillRadius**: when Full Radius, OverallDiameter = TipDiameter − 2·CornerRadius.
  - **CuttingLength**: shoulder ≥ cutting length (as for the end mill).

Defaults: D 1.5 / 40, Tip 2 / 50, OAL 2 / 50, Cut 0.25 / 5, ShLen 0.75 / 18, Shank 0.5 / 12, ShoulderD 2 / 50, angle 45, TlRadiusType 1.

#### Radius Mill (15)

Source: radiusmillview.baml, RadiusMillPageViewModel.

| Label | Name | Binding |
|---|---|---|
| «Tip diameter:» | CuttingDiameter | RadMillObject.OverallDiameter |
| «Overall length:» | OverallLength | …OverallLength |
| «Cutting length:» | CuttingLength | …CuttingDepth |
| (Tip/corner) «Radius:» | RadMillRadius | …CornerRadius |
| (Non-cutting) «Shoulder length:» | ShoulderLength | …ShoulderLength |
| «Shank diameter:» | ShankDiameter | …ArborDiameter |

Rule: CuttingLength sets shoulder ≥ cut.

Defaults: D 0.25 / 6, OAL 2 / 75, Cut 0.25 / 6, ShLen 0.625 / 18, Shank 0.375 / 10, ShoulderD 0.75 / 20, TlRadiusType 4, R 0.25 / 6.

#### Chamfer Mill (12)

Source: chamfermillview.baml, ChamferMillPageViewModel.

| Group | Label | Name | Binding |
|---|---|---|---|
| Overall | «Outer diameter:» | CuttingDiameter | ChamferMillObject.OverallDiameter |
| | «Overall length:» | OverallLength | …OverallLength |
| | «Cutting length:» | CuttingLength | …CuttingDepth |
| Tip / corner | «Chamfer angle:» | TaperAngleChamferMill | …TaperAngle |
| | «Tip diameter:» | TipDiameter | …TipDiameter |
| Tip / corner (**Advanced**) | «Corner radius:» | CornerRadius | …CornerRadius (Dim CornerRadius) |
| Non-cutting | «Shoulder length:» | ShoulderLength | …ShoulderLength |
| | «Shank diameter:» | ShankDiameter | …ArborDiameter |

Rule: CuttingLength sets shoulder ≥ cut.

Defaults: D 1 / 25, OAL 3 / 75, Cut 1 / 25, ShLen 1.2 / 30, Shank 0.5 / 12, ShoulderD 1 / 25, Tip 0.1 / 2.5, angle 45.

#### Slot Mill (14)

Source: slotmillview.baml, SlotMillPageViewModel.

| Group | Label | Name | Binding | Dim |
|---|---|---|---|---|
| Overall | «Cutting diameter:» | CuttingDiameter | SlotMillObject.OverallDiameter | CuttingDiameter |
| | «Overall length:» | OverallLength | …OverallLength | OverallLength |
| | «Thickness:» | CuttingLength | …CuttingDepth | CuttingLength |
| Tip / corner | CheckBox «Lock upper and lower corners» → `LockUpperAndLowerCorners` | | | |
| | Upper ButtonTabControl (`UpperCornerType` = SecondaryRadiusType; tabs None/Chamfer/Radius): «Chamfer distance:» → SecondaryChamferDistance (UpperChamferDistance); «Radius:» → SecondaryCornerRadius (UpperRadius) | | | |
| | Lower ButtonTabControl (`LowerCornerType` = TlRadiusType): «Chamfer distance:» → ChamferLength; «Radius:» → CornerRadius | | | |
| Non-cutting | «Shank diameter:» | ShankDiameter | …ArborDiameter | ShankDiameter |
| Non-cutting (**Advanced**) | «Shoulder length:» | ShoulderLength | …ShoulderLength | ShoulderLength |

Rules:

- CuttingLength sets ShoulderLength = CuttingDepth (always).
- The lock behaviour (decoded in the port, `viewer/toolwiz.c` `tw_set_lock_corners` / `raise_pc`; TW `SlotMillPageViewModel.cs`
  L66-81, L101-150): switching the lock on copies the upper corner (type, chamfer, radius) to the lower one; while locked, a change
  of either corner's type, chamfer distance or radius is copied to the other. The page constructor starts the lock as `AreCornersEqual()` (L89).

Defaults: D 2 / 50, OAL 2 / 50, thickness 0.25 / 6, ShLen 0.25 / 6, Shank 0.5 / 12, both corners None/0.

#### Taper Mill (17)

Source: tapermillview.baml, TaperMillPageViewModel, TaperMillView.cs.

| Group | Label | Name | Binding |
|---|---|---|---|
| Overall | «Tip diameter:» | CuttingDiameter | TaperMillObject.OverallDiameter |
| | «Angle:» | TaperAngle | …TaperAngle |
| | «Cutting length:» | CuttingLength | …CuttingDepth |
| | «Shoulder length:» | ShoulderLength | …ShoulderLength |
| | «Overall length:» | OverallLength | …OverallLength |
| Tip / corner | «Corner type:» ComboBox TipTreatment | | items: None, Corner Radius, Full Radius (CornerTypeList 0, 2, 3); index↔type mapping 0↔0, 1↔2, 2↔3 |
| | «Radius:» | CornerRadius | …CornerRadius |
| Non-cutting | «Shank diameter:» | ShankDiameter | …ArborDiameter |

Behaviour:

- Radius is enabled when the combo index ≠ 0. Diameter is enabled when the index ≠ 2.
- **TaperAngle / CuttingDiameter**: when Full Radius, R = D/2, floored to 4 decimals. Otherwise, when creating, `CorrectRadiusOnCornerTypeChange` is applied.
- **CornerRadius**: when Full Radius, D = 2R (floored to 4 dp). Otherwise R is clamped to ≤ floor4(D/2).
- **CuttingLength**: shoulder ≥ cut.

Defaults: D 0.218 / 6, OAL 6 / 200, Cut 1.5 / 40, ShLen 1.5 / 40, Shank 0.75 / 20, angle 10.

#### Dove Mill (16)

Source: dovemillview.baml, DoveMillPageViewModel.

| Group | Label | Name | Binding / Dim |
|---|---|---|---|
| Overall | «Cutting diameter:» | CuttingDiameter | DovetailObject.OverallDiameter |
| | «Overall length:» | OverallLength | |
| | «Cutting length:» | CuttingLength | |
| | «Taper angle:» | TaperAngleDoveMill | …TaperAngle |
| Tip / corner | ButtonTabControl CornerType (tabs None, Radius); «Radius:» → CornerRadius | | |
| Non-cutting | «Shank diameter:» | ShankDiameter | |
| Non-cutting (**Advanced**) | «Shoulder length:» | ShoulderLength | |

Rule: CuttingLength sets ShoulderLength = CuttingDepth.

Defaults: D 0.75 / 20, OAL 2.125 / 60, Cut 0.375 / 10, ShLen 0.5 / 15, Shank 0.375 / 10, ShoulderD 0.317 / 8.453, angle 30.

#### Lollipop Mill (18)

Source: lollipopmillview.baml, LollipopMillPageViewModel.

Overall group: «Cutting diameter:» (EndmillObject.OverallDiameter), «Overall length:», «Cutting length:». The NonCuttingGeometry view follows.

Rules:

- CuttingDiameter sets CornerRadius = D/2.
- CuttingLength sets shoulder ≥ cut.

Defaults: D 1 / 25, OAL 5 / 200, Cut 0.75 / 18, ShLen 1.25 / 75, Shank 0.75 / 25, ShoulderD 0.5 / 12, ShankType 1 (tapered), TaperLen 1.4288 / 71.438, TlRadiusType 3, R 0.5 / 12.5.

#### Engrave Tool (21)

Source: engravemillview.baml, EngraveMillPageViewModel.

| Group | Label | Name |
|---|---|---|
| Overall | «Outer diameter:», «Overall length:», «Cutting length:» | CuttingDiameter, OverallLength, CuttingLength |
| Tip / corner | «Angle:» | TaperAngleEngraveMill (…TaperAngle) |
| | «Tip diameter:» | TipDiameter |
| Non-cutting | «Shoulder length:», «Shank diameter:» | ShoulderLength, ShankDiameter |

Rule: same as Spot drill (shank follows diameter when creating; shoulder ≥ cut).

Defaults: D 0.25 / 6, OAL 2 / 50, Cut 0.437 / 10, ShLen 1.2 / 50, Shank 0.25 / 6, Tip 0.002 / 0.2, angle 15, 1 flute.

#### Thread Mill (24)

Source: threadmillview.baml, ThreadMillPageViewModel.

| Label | Control / Binding |
|---|---|
| «Thread form:» | ComboBox, SelectedValue EntitySelected.ThreadShape; items from enum ThreadShapeList: «Unified National», «Metric», «Acme», «Acme, truncated (Stub)», «American Buttress Pull», «American Buttress Push», «American National Pipe Thread» (DatabaseEnumStrings ThreadShapeList_*) |
| `ThreadInfoLabel` = «Threads per inch:» (inch) / «Pitch:» (metric) | TextBox ThreadPer → `ThreadInfo`, Dim ThreadPitch. Inch: shows 1/ThreadPitch and stores 1/value. Metric: shows the pitch directly. A value of 0 is ignored. Tooltip «Set the number of threads per inch.» / «Set the thread pitch.» |
| «Outside diameter:» | CuttingDiameter → EntitySelected.OverallDiameter |
| «Overall length:» / «Cutting length:» | OverallLength / CuttingLength |

The NonCuttingGeometry view follows. Rule: CuttingLength sets shoulder ≥ cut.

Defaults: D 0.25 / 6, OAL 2.5 / 60, Cut 1 / 25, ShLen 1.5 / 40, Shank 0.25 / 6, ShoulderD 0.2 / 5, ShankType 1, TaperLen 0.025 / 0.5, pitch 0.05 in (= 20 TPI) / 1 mm, shape 0 (inch) / 1 (metric).

#### Barrel Mill (25)

Source: barrelmillview.baml, BarrelMillPageViewModel.

| Label | Name | Binding | Dim |
|---|---|---|---|
| «Cutting diameter:» | CuttingDiameter | EntitySelected.OverallDiameter | CuttingDiameter |
| «Overall length:» | OverallLength | | OverallLength |
| «Cutting length:» | CuttingLength | | CuttingLength |
| «Profile radius:» | ProfileRadius | EntitySelected.ProfileRadius | UpperRadius |
| «Corner radius:» | CornerRadius | EntitySelected.CornerRadius | LowerRadius |

The NonCuttingGeometry view follows. Rule: CuttingLength sets shoulder ≥ cut and BarrelLength = CuttingDepth.

Defaults: D 0.5 / 12, OAL 3 / 75, Cut 0.875 / 22, ShLen 1.5 / 40, Shank 0.375 / 10, ShoulderD 0.25 / 6, ShankType 1, TaperLen 0.354455 / 11.3426, BarrelLength 0.875 / 22, CornerR 0.05 / 1, ProfileR 0.75 / 19.

#### High Feed Mill (23)

Source: feedmillview.baml, FeedMillPageViewModel.

| Group | Label | Binding | Dim |
|---|---|---|---|
| Overall | «Cutting diameter:», «Overall length:», «Cutting length:» | EntitySelected.OverallDiameter / OverallLength / CuttingDepth | |
| «Cutting geometry» | «Tip diameter:» | TipDiameter | TipDiameter |
| | «Upper taper angle:» | UpperTaperAngle via `AngleComplimentConverter` | UpperTaperAngle |
| | «Lower radius:» | LowerRadius | LowerRadius |
| | «Upper radius:» | UpperRadius | UpperRadius |
| | «Relief taper angle:» | ReliefTaperAngle | ReliefTaperAngle |
| Cutting geometry (**Advanced**) | «Lower taper angle:» | LowerTaperAngle via AngleComplimentConverter | LowerTaperAngle |

The NonCuttingGeometry view follows. AngleComplimentConverter formula (as ported in `viewer/toolwiz.c` `tw_get` / `tw_set` / `tw_format`): Convert = 90 − value,
unrounded; ConvertBack = 90 − text value.

Defaults: D 1 / 25, OAL 4 / 100, Cut 0.08 / 2, ShLen 1.2 / 30, Shank 0.75 / 20, ShoulderD 0.75 / 20, ShankType 1, Tip 0.5 / 12, LowerR 0.031 / 1, UpperR 0.031 / 1, LowerTaper 90, UpperTaper 76, Relief 7.

#### Custom Tool (0)

Source: customdxfpageview.baml, CustomToolPageViewModel.

- Overall group: «Cutting diameter:» / «Overall length:» / «Cutting length:» (MillToolObject.*).
- Non-cutting group: «Shoulder length:» / «Shank diameter:».
- Rule: shoulder ≥ cut.

Defaults: D 0.5 / 10, OAL 4 / 75, Cut 1 / 25, ShLen 1.2 / 25, Shank 0.5 / 10.

#### Drill / Spot Drill (3 / 2)

Source: spotdrillview.baml, SpotDrillPageViewModel.

| Group | Label | Name | Binding |
|---|---|---|---|
| «Standard sizes» | `DropdownGridView Name=SpotDrills` (§2.6.y) | | |
| Overall | «Drill diameter:» | CuttingDiameter | SpotDrillObject.OverallDiameter |
| | «Overall length:» / «Cutting length:» | OverallLength / CuttingLength | |
| «Tip treatment» | «Tip angle:» | TipAngle | …TipAngle |
| Non-cutting | «Shoulder length:» / «Shank diameter:» | ShoulderLength / ShankDiameter | |

Rules:

- CuttingDiameter: when creating, and if the shank was not edited, Shoulder = Arbor = D.
- ShankDiameter sets `arborModified`.
- CuttingLength sets shoulder ≥ cut.

Defaults:

- Drill: D 0.25 / 6, OAL 2 / 50, Cut 1 / 25, ShLen 1.5 / 40, tip 118, 2 flutes.
- Spot: same, except ShLen 1.5 / 37 and tip 90.

#### Center Drill (1)

Source: centerdrillview.baml, CenterDrillPageViewModel.

| Group | Label | Name | Binding |
|---|---|---|---|
| Standard sizes | DropdownGridView `CenterDrills` | | |
| Overall | «Cutting length:» | CuttingLength | CenterDrillObject.CuttingDepth |
| | «Shank diameter:» | ShankDiameter | …ArborDiameter |
| | «Overall length:» | OverallLength | |
| «Tip treatment» | «Drill diameter:» | CenterDrillDiameter | …DrillDiameter |
| | «Drill length:» | CenterDrillLength | …DrillLength |
| | «Drill angle:» | TipAngle | …TipAngle |
| | «Shoulder angle:» | ShoulderAngle | …ShoulderAngle |

Rules:

- **CuttingLength**: ShoulderLength = CuttingDepth + 0.005.
- **Shank / drill length / drill diameter / shoulder angle**:
  - A shank edit sets OverallDiameter = ArborDiameter.
  - Then CuttingDepth = round(((D − drillD)/2)·tan(90° − shoulderAngle/2) + drillLength, 6 inch / 5 metric dp).
  - Then ShoulderLength = CuttingDepth + 0.005.

Defaults: D 0.25 / 7.94, OAL 2 / 54, Cut 0.2312 / 7.30228, ShLen 0.2362 / 7.30728, drill D 0.109375 / 3.18, drill length 0.109375 / 3.18, tip 118, shoulder angle 60.

#### Reamer (6)

Source: reamerview.baml, ReamerPageViewModel.

| Group | Label | Name | Binding |
|---|---|---|---|
| Overall | «Cutting diameter:» / «Overall length:» / «Cutting length:» | CuttingDiameter / OverallLength / CuttingLength | ReamerObject.* |
| Tip / corner | «Chamfer distance:» | ChamferDistanceDrill | …ChamferLength |
| Non-cutting | «Shank diameter:» | ShankDiameter | |
| Non-cutting (**Advanced**) | «Shoulder length:» | ShoulderLength | |

Rules:

- CuttingLength sets ShoulderLength = CuttingDepth.
- CuttingDiameter: when creating, and if the chamfer was not edited, ChamferLength = round(D/8).
- ChamferDistanceDrill: if chamfer > D/2, it is set to D/2.

Defaults: D 0.25 / 6, OAL 6 / 150, Cut 1 / 25, ShLen 1 / 25, Shank 0.225 / 5.4, chamfer 0.03125 / 0.75, tip 45, 6 flutes.

#### Countersink (9)

Source: countersinkview.baml, CounterSinkPageViewModel.

| Group | Fields |
|---|---|
| Overall | «Outer diameter:» CuttingDiameter, «Overall length:», «Cutting length:» |
| Tip / corner | «Tip angle (included):» TipAngle |
| Non-cutting | «Shoulder length:», «Shank diameter:» |

Rule: shoulder ≥ cut.

Defaults: D 0.75 / 25, OAL 2 / 75, Cut 0.5 / 16, ShLen 1 / 25, Shank 0.5 / 12, tip 82.

#### Counterbore (8)

Source: counterboreview.baml, CounterborePageViewModel.

| Group | Fields |
|---|---|
| Overall | «Cutting diameter:» / «Overall length:» / «Cutting length:» (DrillObject.*) |
| Non-cutting | «Shoulder length:» / «Shank diameter:» |

Rule: shoulder ≥ cut.

Defaults: D 0.781 / 25, OAL 5 / 100, Cut 1 / 18, ShLen 1.5 / 24, Shank 0.5 / 12, pilot 0.25 / 6 (pilot fields are not on the page).

#### Tap (4 RH / 5 LH)

Source: tapdrillview.baml, TapDrillPageViewModel, TapDrillView.cs.

| Group | Label | Control / Binding |
|---|---|---|
| Standard sizes | DropdownGridView `TapDrills` | |
| | «Nominal diameter:» | CuttingDiameter → TapDrillObject.OverallDiameter |
| | «Threads per inch:» / «Pitch:» (DataTrigger on TapDrillObject.IsMetric) | ThreadPer → `ThreadInfo` (same inversion as the thread mill) |
| | CheckBox «Left hand» | LeftHand → `LeftHanded`: checked → MCToolType 5, SpindleDir 1; unchecked → 4, SpindleDir 0. Tooltip «Set whether the tool is a left hand tap.» |
| Overall | «Cutting length:» / «Shank diameter:» / «Overall length:» | CuttingLength / ShankDiameter / OverallLength |
| «Tip treatment» | ComboBox TipTreatment | «Plug» (index 0) / «Bottoming» (index 1) [TapDrillViewXaml.TapDrillTypes_*] |
| Tip treatment (**Advanced**) | «Shoulder length:» | ShoulderLength |

Rules:

- Combo index 0 sets TipAngle = 120 if the original tip angle was 120, else 118. Index 1 sets TipAngle = 180.
- Displayed index: 180 → 1; 118 or 120 → 0; anything else → 1.
- CuttingLength sets ShoulderLength = CuttingDepth.

Defaults: D 0.25 / 6, OAL 2 / 50, Cut 1 / 25, ShLen 1.5 / 37.5, Shank 0.25 / 6, ShoulderD 1 / 20, pitch 0.05 / 2.5, tip 180.

#### Bradpoint Drill (22)

Source: bradpointdrillview.baml, BradPointDrillPageViewModel.

| Group | Fields |
|---|---|
| Standard sizes | DropdownGridView `BradPointDrills` |
| Overall | «Drill diameter:» / «Overall length:» / «Cutting length:» |
| Non-cutting | «Shoulder length:» / «Shank diameter:» |

Rule: same as Spot drill.

Defaults: D 0.25 / 6, OAL 2 / 50, Cut 1 / 25, ShLen 1.5 / 30, tip 180.

#### Bore Bar (7)

Source: boringview.baml, BoringPageViewModel.

| Group | Label | Name | Binding |
|---|---|---|---|
| Overall | «Cutting diameter:» / «Overall length:» / «Cutting length:» | | BoringObject.* |
| | «Taper angle:» | TaperAngleBoring | …TaperAngle |
| Tip / corner | ButtonTabControl `CornerType`: tab 0 None (TlRadiusType 0), tab 1 Radius (TlRadiusType 2); «Radius:» → CornerRadius | | |
| Non-cutting | «Shank diameter:» | ShankDiameter | |
| Non-cutting (**Advanced**) | «Shoulder length:» | ShoulderLength | |

Rule: CuttingLength sets ShoulderLength = CuttingDepth.

Defaults: D 1 / 25, OAL 2 / 50, Cut 0.25 / 6.5, ShLen 1.5 / 37.5, Shank 0.5 / 12, angle 45, shift 0.1 / 0.25, 1 flute.

#### 2.6.x Non-cutting geometry view

Used by the End/Bull/Ball mill, Lollipop, Thread mill, Barrel mill and High feed mill pages. Source: noncuttinggeometryview.baml, NonCuttingGeomViewModel.

| Label | Name | Binding | Dim |
|---|---|---|---|
| Group «Non-cutting geometry» | | | |
| «Shoulder length:» | ShoulderLength | EntitySelected.ShoulderLength | ShoulderLength |
| «Shoulder diameter:» | ShoulderDiameter | EntitySelected.ShoulderDiameter | ShoulderDiameter |
| «Shank diameter:» | ShankDiameter | EntitySelected.ArborDiameter | ShankDiameter |
| ButtonTabControl `SelectedIndex = NonCuttingGeometry.ShankStyleType`. Tabs: «Straight Shank» (StraightShank16.png); «Tapered Shank» (TaperedShank16.png, enabled = `TaperedShankPossible`); «Reduced Neck Shank» (ReducedShank16.png, enabled = `ReducedNeckPossible`) | | | |
| (Tapered) «Taper angle:» | – | NonCuttingGeometry.ShoulderTaperAngle | ShoulderTaper |
| (Tapered) «Taper length:» | – | NonCuttingGeometry.ShoulderTaperLength | ShoulderTaperLength |
| (Neck) «Neck diameter:» | – | NonCuttingGeometry.ReducedNeckDiameter | NeckDiameter |
| (Neck) «Neck length:» | – | NonCuttingGeometry.ShoulderTaperLength | ShoulderTaperLength |

Rules (NonCuttingGeomViewModel):

- `TaperedShankPossible` = (shank − shoulderD)/2 > 5e-5 and (OAL − ShLen) > 5e-5.
- `ReducedNeckPossible` = (OAL − ShLen) − ((shank − m)/2 + (neck − m)/2) > 5e-5, where m = min(shank, shoulderD).
- Taper angle = round(atan(|shank − shoulderD|/2 / TaperLength), 4 dp) in degrees, or 90 if there is no step.
- Entering an angle sets TaperLength = step/tan(angle). It is capped at (OAL − ShLen) when the angle is below the minimum, and is 0 when there is no step or the angle ≥ 90.
- Switching to Tapered: TaperLength = min(step, OAL − ShLen).
- Switching to Neck: neck = 0.9·m, and TaperLength = max((OAL − ShLen)/3, (shank − neck)/2 + (neck − neck)/2).
- Auto-adjust on OAL / ShLen / shank / shoulderD changes:
  - Straight: TaperLength = 0 and neck = shoulderD.
  - Tapered: reverts to Straight if step < 5e-5 or length < 5e-5, and caps length at OAL − ShLen.
  - Neck: neck is capped at m; reverts to Straight if there is no step and neck = shank, or if length < 5e-5.

#### 2.6.y Standard sizes grid (DropdownGridView)

Source: baml:toolmanager.shared/controls/dropdowngridview.baml, SH/…Controls/DropdownGridView.cs.

- Rows come from the **catalog database** (`CatalogDatabase.GetDatabaseCatalogConnection`). Rows are drills of the same MCToolType for types 1, 2, 3, 8, 9 and 22, or all `TlToolThreading` items for types 4 and 5. Rows are filtered to the tool's IsMetric and sorted by diameter (center drills by drill diameter).
- Catalog contents: NOT DECODED here (see InsertCatalogData.sql).

Columns:

| Template | Column headers |
|---|---|
| CenterDrill | «Size», «Shank Diameter», «Drill Diameter», «Drill Length», «Overall Length» |
| SpotDrill | «Size», «Drill Diameter», «Drill Length», «Overall Length» |
| Drill | «Name», «Diameter», «Flute Length», «Overall Length» |
| Tap | «Name», «Diameter», «Flute Length», «Overall Length», «Threads» / «Thread Pitch» |

Headers come from [DropdownGridViewXaml.DropDownGrid_*] plus the baml texts.

- Prompts: «Select a standard size drill.» / «… bradpoint drill.» / «… center drill.» / «… tap.» / «… spot drill.».
- Selecting a row → `BulkUpdateProperties` (the center-drill handling is shown in CenterDrillPageViewModel.BulkUpdatePropertiesInternal). The row's "Size" is used as the naming seed.

### 2.7 Geometry validation messages (shown in the preview error panel)

Source: DB/…ValidityChecking/ToolValidityChecker.cs (all comparisons use a 5e-5 tolerance) and DB/…InvalidToolStrings.resx. The first failing check throws.

| Applies to | Condition | Message |
|---|---|---|
| all | OAL ≤ 0 | «Tool length must be greater than 0.0.» |
| all | D ≤ 0 | «Tool diameter must be greater than 0.0.» |
| all | Cut ≤ 0, or Cut > ShLen | «Cutting length exceeds shoulder length or is less than or equal to 0.0.» |
| all | Cut > OAL | «Cutting length exceeds shoulder or overall length.» |
| all | ShLen > OAL | «Shoulder length exceeds tool length.» |
| all | Shank ≤ 0 | «Shank diameter must be greater than 0.0.» |
| reduced neck | neck ≤ 0 | «The reduced neck radius must be greater than 0.0.» |
| reduced neck | neck > shoulderD or neck > shank | «The reduced neck diameter cannot be larger than the shoulder diameter or the shank diameter.» |
| reduced neck | (ShLen + TaperLen) − (shank − neck) − (ShLen + shoulderD − neck) < 0 | «The taper length must be larger to support a neck length.» |
| reduced neck | OAL − (ShLen + TaperLen) ≤ 0 | «The total tool length (Overall) is less than the total of all of its components, and illegal condition.» |
| end/ball/bull | shoulderD = 0 | «Shoulder diameter cannot be zero.» |
| end/ball/bull | chamfer > D/2 | «Chamfer distance too large for cutting diameter.» |
| end/ball/bull | R < 0 | «Corner radius must be greater than 0.0.» |
| end/ball/bull | R > D/2 | «Corner radius too large for cutting diameter.» |
| end/ball/bull | R > OAL | «Corner radius too large for tool length.» |
| end/ball/bull | R > ShLen | «Corner radius too large for shoulder length.» |
| radius mill | R ≤ 0 | «Corner radius must be greater than 0.0.» |
| radius mill | R ≥ ShLen | «Corner radius too large for shoulder length.» |
| dove | R < 0 | «Corner radius must be greater than 0.0.» |
| dove | R > Cut | «Corner radius and/or cutting length error.» |
| dove | R > D/2 | «Corner radius too large for cutting diameter.» |
| dove | shank > D | «Shank diameter too large for tool diameter.» |
| dove | shank = D | «Tool diameter can not equal shank diameter.» |
| dove | D/2 − Cut/tan(90 − angle) ≤ 0 with R = 0 | «Taper angle and/or cutting length error.» |
| lollipop | shoulderD = 0; shank > D (non-tapered); OAL < ShLen + TaperLen; shoulderD > shank; shoulderD > D; ShLen < √((D/2)² − (shoulderD/2)²) + D/2 | the respective ShoulderDiameterCantBeZero / ArborRadiusExceedsToolRadius / ToolShorterThanSumOfComponents / ShoulderDiameterLargerThanArbor («The shoulder diameter is greater than the shank diameter, an illegal condition.») / ShoulderDiaLargerThanToolDia («The shoulder diameter cannot be larger than the cutting diameter.») / «Shoulder length too short for tool diameter.» |
| taper mill | R < 0 | IllegalCornerRadius |
| face mill | OAL < D/8 | «Tool length too short.» |
| face mill | D > tip | «Tip diameter exceeds tool diameter.» |
| face mill | taper intersection < 0 or > ShLen; R > Cut; the D = tip cases | «Tip angle/tool diameter don't intersect.» / «Corner radius and/or cutting length error.» |
| chamfer / engrave | OAL < D/8 | «Tool length too short.» |
| chamfer / engrave | tip ≥ D | «Tip diameter exceeds tool diameter.» |
| chamfer / engrave | (D/2 − tip/2)·tan(90 − angle) outside [0, ShLen] | «Tip angle/tool diameter don't intersect.» |
| chamfer / engrave | R < 0 | IllegalCornerRadius |
| slot mill | R < 0 or upper R < 0 | IllegalCornerRadius |
| slot mill | lower or upper (R or chamfer) > Cut/2, or Cut < lower + upper | «Corner radius and/or cutting length error.» |
| slot mill | shank/2 > D/2 − upper | «Shank diameter too large.» |
| barrel | shoulderD = 0 | ShoulderDiameterCantBeZero |
| barrel | cornerR ≥ profileR | «The corner radius is too large for the current profile radius.» |
| barrel | D < shoulderD | ShoulderDiaLargerThanToolDia |
| barrel | profile geometry checks | «The profile radius is too small for the current tool dimensions.» / «… too large …» |
| feed mill | shoulderD = 0 | ShoulderDiameterCantBeZero |
| feed mill | relief > 0 and D ≤ shoulderD | «Relief angle must be zero if the shoulder diameter is not smaller than the cutting diameter.» |
| feed mill | relief ∉ [0, 90] | «The relief taper angle must fall between 0.0 and 90.0.» |
| feed mill | D ≤ tip | TipDiameterExceedsToolDiameter |
| feed mill | lower > 90, or lower < upper | «The lower taper angle must fall between 0.0 and the upper taper angle.» |
| feed mill | upper ∉ [0, 90] | «The upper taper angle must fall between the lower taper angle and 90.0.» |
| bore bar | R < 0 | IllegalCornerRadius |
| bore bar | shank > D | ArborRadiusExceedsToolRadius |
| bore bar | shank = D | ToolDiameterEqualsArborDiameter |
| bore bar | D/2 − Cut/tan(90 − tipAngle) ≤ 0 | TaperAngleOrFluteLengthError |
| center drill | drillD/2·tan(90 − tip/2) ≥ drillLen | «Tip angle too steep for tool diameter.» |
| center drill | (D − drillD)/2·tan(90 − shoulderAngle/2) + drillLen > ShLen | «Shoulder angle too steep for tool diameter.» |
| reamer | OAL < D/8 | «Tool length too short for tool diameter.» |
| reamer | D ≠ shank and ShLen < D/8 | «Shoulder length too short for tool diameter.» |
| reamer | D < 2·chamfer | ChamferDistanceTooLargeForDiameter |
| drill / spot / bradpoint (TlToolDrill, type ≠ 9) | (D/2)·tan(90 − tip/2) outside [0, ShLen] | TipToolDiameterIntersectionError |
| countersink | same with R = 0 | TipToolDiameterIntersectionError |
| countersink | otherwise R < 0, or R > D/2 | IllegalCornerRadius / CornerRadiusTooLargeForRadius |
| tap | same as countersink, using the tap fields | |
| thread mill | shoulderD = 0 | ShoulderDiameterCantBeZero |
| thread mill | D/2 − k·pitch ≤ 0, with k = 0.866 (Unified/Metric), 0.5 (Acme), 0.3 (Stub), 0.80804 (Buttress); NPT: D/2 − Cut/32 + 0.8657·pitch | «Thread pitch too large for tool diameter.» |

Counterbore: `ValidateTool(TlToolCounterBore)` runs only the common (all) checks. Dispatch is `ValidateTool((dynamic)mill)`, so the most-derived overload wins (CheckToolValidity).

### 2.8 Auto-naming

| Item | Rule | Source |
|---|---|---|
| When | On every model change (`ModelIsDirty`) and on Properties-page construction when creating. Only applies if `!NameManuallyChanged && GeneralOptions.AutoNaming` (default true) | TlAssemblyItemViewModel.AutoGenerateName; GeneralOptions.cs:129; ToolPropertyPageViewModel ctor |
| General tool | «{0} {1}» [TlToolViewModelStrings.Tool_Name] = OverallDiameter, then ToolTypeList description | DB/…ViewModels/TlToolViewModel.cs CreateAutoGeneratedName |
| Drills (except bore bar) | StandardSizeNamingSeed + type description + " - " + OverallDiameter | TlToolDrillViewModel.CreateAutoGeneratedName |
| Threading (tap, thread mill) | seed + " " + description + " - " + diameter | TlToolThreadingViewModel |
| Taper mill | ArborDiameter + " " + description | TlToolEndmillViewModel |
| Holder | «{0} {1} - {2} {3}» = upper type, upper size, lower type, lower size | TlHolderViewModel |
| Number formatting | C# default `double` → string (no explicit format) | same |

### 2.9 Page 3 – Finalize Properties (ToolPropertiesView)

Source: baml:views/toolpages/toolpropertiesview.baml, ToolPropertyPageViewModel.

Layout:

- Top: TextBlock `PropertyTopText` = «Adjust any miscellaneous properties before finalizing tool creation.» [SH ApplicationStrings.ToolPropertyTopLabel].
- Grid with columns `*`, `20`, `350`. Each column is a ScrollViewer with MaxHeight 400.

Left column, group «Operation». The grid has columns 150/*/80/70/20, and labels are in col 0.

| Label | Name | Binding | Notes |
|---|---|---|---|
| «Tool number:» | ToolNumber | EntitySelected.ToolNumber | A change re-derives the offsets (§2.3) |
| «Length offset:» | LengthOffsetNum | EntitySelected.LengthOffsetNum | |
| «Diameter offset:» | DiameterOffsetNum | EntitySelected.DiameterOffsetNum | |
| «Head number:» | HeadNumber | EntitySelected.HeadNumber | |
| `SfmLabel` = «SFM:» (inch) / «CS:» (metric) | – | EntitySelected.Sfm | Tooltip «Sets the cutting speed in surface feet per minute.» / «… surface meters per minute.» |
| «FPT:» | – | EntitySelected.Fpt | |
| «Number of flutes:» | NumFlutes | FluteCount | |
| «Feed rate:» | FeedRate | FeedRate | |
| «Plunge rate:» | PlungeRate | PlungeRate | |
| «Retract rate:» | RetractRate | RetractRate | |
| «Spindle speed:» | SpindleSpeed | SpindleSpeed | RPM per tooltip |
| Calculator button `CalculateFeedSpeedButton` (Calculator.png, bracket image CalcFeedSpeedLink.png spanning the feed…spindle rows) | | `CalculateFeedSpeedCommand` | Tooltip «Click to recalculate feeds and speeds». Enabled when a feed/speed calculator was supplied. Calls `RecalculateFeedsAndSpeeds(flutes, D (tip D for chamfer mill/engrave), material, isDrill, FPTadj, SFMadj, spindleDir, type, metric, pitch)` and writes spindle speed, feed, retract and plunge. The formula is native: NOT DECODED |
| «Spindle direction:» | SpindleDirection | SpindleDir | Enum SpindleDirectionList descriptions (DatabaseEnumStrings) |
| «Material:» | Material | TlToolMaterialID | Items = catalog tool materials sorted by name (`InitializeDatabaseLists`). Defaults DB has '' and 'Carbide' |
| **Advanced:** «Cutting speed adjustment:» | CuttingSpeedAdjustment | MaterialSFMAdjust | |
| **Advanced:** «FPT adjustment:» | FPTAdjustment | MaterialFPTAdjust | |
| **Advanced:** Button «Coolant» (75×23) | Coolant | EditCoolantCommand | Tooltip «Canned text coolant settings». Opens `IEditCoolantService.EditCoolant(tool, coolantParameters)` (dialog in toolmanager.database coolantview.baml; decoded 2026-10-16, see §2.12a) |
| **Advanced:** CheckBox «Metric» | ToolMetric | EntitySelected.IsMetric | Toggling it switches the SFM/CS label |

Right column, group «General» (cols 150/*/70/80):

| Label | Control / Binding |
|---|---|
| «Name:» | ToolName → EntitySelected.Name |
| «Description:» | ToolDescription → Description |
| «Manufacturer name:» | `CatalogDataManagerView` (DataContext ManufacturerManager) |
| «Manufacturer's tool code:» | MfgToolCode |
| «Tool Grade:» | CatalogDataManagerView (ToolGradeManager) |
| **Adv.** «Required pilot diameter:» | RequiredPilotDiameter: MultiBinding with `ToolEditBoxConverterWithMax`. A value above the max (second binding) or below 0 is rejected. The max is the second binding, `EntitySelected.OverallDiameter` (toolpropertiesview.baml `PilotConverterMultiBinding`: `PilotBinding` = EntitySelected.RequiredPilotDiameter, `OverallDiameterBinding` = EntitySelected.OverallDiameter) |
| **Adv.** «Helix Angle:» | HelixAngle |
| **Adv.** «Helix Type:» | ComboBox HelixType (enum HelixTypeList) |
| **Adv.** «Chuck:» | Chuck |

Below General is a type-specific panel (`ToolPropertyDataTemplateSelector`). The mapping is:

- Endmill VM (includes feed and barrel mills) → EndmillPropertiesView = «Milling» group (MillingPropertiesView).
- Reamer → ReamerPropertiesView «Reaming» + Holemaking.
- Boring → «Boring» + Holemaking.
- Drill VM → «Drilling» + Holemaking.
- Tap → «Tapping» + Holemaking.
- Thread mill → «Thread milling» + Milling.
- Anything else → empty.

Panel fields:

- **Milling**:
  - CheckBoxes «Rough Tool» (IsRough) and «Finish Tool» (IsFinish).
  - «Rough XY step (%):», «Rough Z step (%):», «Finish XY step (%):», «Finish Z step (%):» (StepoverRoughXY/Z, StepoverFinishXY/Z).
- **Holemaking**:
  - «Peck 1 (% dia):», «Peck 2 (% dia):», «Peck clearance (%):», «Dwell:», «Chip break (% dia):».
  - «Cycle:» ComboBox (CannedCycleTypeList).
- **Boring**: «Shift value (% dia):» (Shift).
- **Tapping** and **Thread milling**: «Thread root diameter:» (RootDiameter).

The milling/holemaking defaults are in the TlMillingOpParams / TlHolemakingOpParams rows of InsertDefaultsData.sql (not tabulated).

### 2.10 Warnings and messages

Warning dialog: `ToolWizardWarningView` (baml:services/toolwizardwarning/toolwizardwarningview.baml), shown by `ToolWizardWarning.ShowDialog`.

- Window: title «Tool Manager», SizeToContent, background #F1F1F1.
- Top band: white, with the ImageWarning32 icon and the message.
- Buttons: «Yes» (75×23, returns true) and «No» (IsCancel).
- CheckBox «Do not show this warning again for this session.» This is stored in a static per-type dictionary. When it is set, ShowDialog returns true without showing.

| WizardWarningType | Text [ToolWizardWarningViewXaml.WizardWarningType_*] | Shown when |
|---|---|---|
| StdSizeOverwritingToolGeometry | «Changing the standard size overwrites tool geometry settings.\nDo you wish to continue?» | Picking a standard size while editing (not creating) (GeometryPageViewModel.BulkUpdateProperties) |
| StdSizeOverwritingHolderGeometry | «Changing the standard size overwrites holder segment data.\nDo you wish to continue?» | HolderDefinePageViewModel:310 |
| OverwritingHolderGeometry | «"Remove All" can not be undone.\nDo you wish to continue?» | Segment grid Remove All (SegmentGridViewModel:239) |
| BreakToolProfileLink | «Breaking the link to the custom tool geometry source cannot be undone.\nDo you wish to continue?» | Break link button |
| ImportingProfileOverwritingToolParametricData | «Do you want to overwrite the existing tool geometry settings \nwith the imported custom profile data?» | Importing a custom profile while editing |

Other strings:

- Holder: «Overwrite existing data» / «Overwrite existing geometry data?» [HolderDefinePageViewXaml.ConnectionError_Title/Text]. These are defined in the resx, but no C# reference was found in toolmanager.toolwizard, so where they are used is NOT DECODED.
- File import type names [FileImportExportStrings]: «STEP File»/«Solid Model File», «DXF File»/«AutoCAD File», «Mastercam File»/«Mastercam Drawing File», «SOLIDWORKS Part File»/«SOLIDWORKS File».
- `PreInitializeUI` exception text "Exception in WizardManager.PreInitializeUI…" is only assigned, never shown.

### 2.11 Holder wizard and Live Center wizard (brief)

They use the same `WizardView` shell and `WizardStyle`, with two pages each: Define, then Properties (baml:views/holderwizardview.baml, livecenterwizardview.baml).

| Item | Holder | Live center | Source |
|---|---|---|---|
| Window title | «Define Holder» / «Edit Holder» (+ «(Read Only)») | «Define Live Center» / «Edit Live Center» | HolderWizardViewModel.DoInitializePageViewModels; LiveCenter*ViewModel |
| Page 1 | «Define Holder Geometry» / «Define geometric parameters.». Sub-caption «Select the upper and lower holder connection types.». Group «Connection» with columns «Type» / «Size». «Upper:» = ComboBox (UpperConnectionTypeList) + size box (UpdateSourceTrigger Explicit). «Lower:» = ComboBox (LowerConnectionTypeList) + size box. Segment grid (§ below). Back disabled | «Define Live Center Geometry» / «Define geometric parameters.». «Set the geometric parameters.». Group «Geometry»: «Taper style:» (LiveCenterTaperStyle), «Taper size:», «Point style:» (LiveCenterPointStyle), «Gauge length:» | holderdefinepageview.baml; livecenterdefinepageview.baml |
| Segment grid | Xceed DataGrid with columns «Top» (TopWidth), «Height», «Bottom» (BottomWidth), each 80 px. Button «Add Segment». Context menu: Move to Top, Move to Bottom, Move Up, Move Down, Insert Above, Insert Below, Remove, Remove All. When the holder has arcs: «Components with arc segments cannot be edited.» | – | segmentgridview.baml |
| Page 2 | General: «Name:», «Description:», «Manufacturer name:», «Catalog ID:», «Metric», «Thru coolant». Top text «Adjust any miscellaneous properties before finalizing holder creation.» | General: Name, Description, Manufacturer, «Catalog ID:», «Compression length:», «Maximum weight:», «Maximum RPM: », «Metric». Top text «Adjust any miscellaneous properties before finalizing live center creation.» | holderpropertiesview.baml; livecenterpropertiesview.baml |
| Holder total length caption | «Total length: {0}» [HolderDefinePageViewXaml.GafDisplayHolderLength_Text] | – | resx |
| Holder catalog import | Changing the upper connection type or size triggers `ImportCatalogDataOnUpperConnectionChange`. Changing the lower size triggers auto-naming | – | HolderDefinePageViewModel ctor |

### 2.12 NOT DECODED (wizard)

- What the native Mastercam side passes (tool number, loff/doff, isDrilling, start page) and what it does with the returned tool.
- `GAFControlHwndHost.EvaluateExpression` (allowed expression syntax).
- The feeds/speeds calculator formula.
- The Coolant dialog's Custom Coolant binding (meaning of the TlCoolant state / Pos values, toollib_net.md 6); the standard part is
  decoded (§2.12a).
- The `AdvancedExpander` expand UI and its default state.
- `ToolEditBoxConverterWithMax` internals (its max input is decoded as OverallDiameter, §2.9).
- The full HolderDefinePageViewModel behaviour.
- The live-center enums' texts.
- Exact pixel sizes of the expanders' inner content beyond the grid column widths given.
- .NET Framework G15 digit generation for ties in the 16th digit (taken as C `%.15G`); the current-culture number format of the
  auto name's "+" concatenations (taken as '.' decimals); custom (imported-profile) tools in `CheckToolValidity`.
- (Decoded since, in the port `viewer/toolwiz.c`: `AngleComplimentConverter` (§2.6 Feed mill note), `ToolConverters.
  CorrectRadiusOnCornerTypeChange` (ToolConverters.cs L31-307), the slot-mill corner-lock observers (§2.6 Slot Mill), the
  DropdownGridView rows (read from DatabaseCatalog.tooldb by `viewer/toollib.c` `toollib_catalog_sizes`).)

### 2.12a Coolant dialog (decoded 2026-10-16; `viewer/mcamui.c` `draw_coolant_dialog`)

Sources: toolmanager.database `CoolantView.baml`, `CoolantViewModel`, `TlCoolantViewModel`, `EditCoolantService`,
`CoolantParameters`, `CoolantStrings.resx` (as cited in the code comment).
- Title «Coolant»; expander «Standard Coolant» with Flood / Mist / Thru-tool combos (items On / Off = `CoolantParameters.<X>On / Off`,
  the parameterless constructor's labels); expander «Custom Coolant» with Flood, Mist, Thru-tool, Custom 1..7 (state On / Off / Ignore,
  time Before / With / After); Help, OK, Cancel (72 wide; Cancel = IsCancel, also Esc).
- Standard Coolant edits `TlCoolant.OldStyle` (8 off, 16 flood, 32 mist, 64 thru-tool): a combo shows On when OldStyle equals its flag;
  On writes the flag, Off writes 8 (only when the value changes); Flood is enabled only while Mist and Thru-tool are Off
  (`OldStyleFloodEnabled`), and likewise for the others. `EditCoolant` edits a copy; OK keeps it.
- This program: Custom Coolant rows and Help (topic 75007) are greyed (the TlCoolant Flood..Custom7Pos columns are not kept in `Tool`).
  There is no host to pass the control definition's coolant labels, so the default labels are shown.

### 2.13 Code: `viewer/toolwiz.c` / `toolwiz.h`

The wizard's logic; the UI (`viewer/mcamui.c` `draw_tool_editor`) only draws what these tables say. Citations in the code are
`TW/` `DB/` (as §2) and `LOC/` = MastercamExtensions.Localization.dll (`EnumExtensions.ToDescription`).

| Function / struct | Behaviour | C# source |
|---|---|---|
| `TwState` | wizard state outside the tool: creating, arbor / chamfer modified, name typed by hand, slot corner lock, tap's original tip angle, add-to-offset values, standard-size naming seed | page / TlToolViewModel fields |
| `net_round`, `floor4`, `to_int32`, `net_g15`, `IsEqualTo`.. | .NET Math.Round (half to even), Convert.ToInt32 overflow, double.ToString() "G" 15 digits, tolerance 5e-5 compares | DatabaseViewModelBase.cs L58, ToolValidityChecker.cs L11-34 |
| `vm_set_*`, `em_set_*`, `sl_*`, `th_*`, `bo_*`, `raise_pc`, `model_dirty` | one function per view-model property setter with its change test, side effects and PropertyChanged observers (NonCuttingGeomViewModel, slot corner lock); ModelIsDirty → auto name | TlToolViewModel.cs, TlToolEndmillViewModel.cs, ..., GeometryPageViewModel.cs L123-131 |
| `correct_*_corner_type`, `correct_radius_on_corner_type_change`, `correct_corner_type_on_radius_change` | corner type ↔ radius / chamfer rules per type (may change the MCToolType between flat / bull / ball / radius mill) | ToolConverters.cs L6-457 |
| `ncg_*` | non-cutting geometry: taper angle / length, tapered-shank / reduced-neck possible, auto adjust, type change | NonCuttingGeomViewModel.cs L81-259 |
| `tw_type_items` / `tw_type_label` / `tw_type_description` | page 1 list and type descriptions | ToolTypePageView.cs L43-330, DatabaseEnumStrings.resx |
| `tw_rows` / `tw_box_name` / `tw_field_name` | Define page rows per type (one table per view baml) | views/toolpages/*.baml |
| `tw_get` / `tw_set` / `tw_format` | box value, ToolEditBoxConverter (reject < 0, Math.Round 5 / 6 places) and the page's ValidateEditFieldInput when the view wires it | ToolEditBoxConverter.cs, <page>ViewModel.ValidateEditFieldInput |
| corner / shank / tap / thread-form / left-hand controls (`tw_set_corner_index`, `tw_set_shank_style`, `tw_set_lock_corners`, ...) | the combo / tab controls of §2.6 | page view models |
| `tw_validate` | `CheckToolValidity`: first failing message (§2.7) | ToolValidityChecker.cs, InvalidToolStrings.resx |
| `tw_autoname` / `tw_set_name` | auto name (§2.8) unless typed by hand | TlToolViewModel.cs L821-828 and subclasses |
| `tw_sfm` / `tw_fpt` / `tw_set_*` speeds | feed / speed coupling, K 318.3 / 3.82 | toollib_net.md 6 |
| `tw_new_type` / `tw_init_edit` / `page_ctor` | leaving page 1: `tool_new` default, number, offsets; Edit: state from an existing tool | ToolWizardViewModel.cs |
| `tw_std_size` | a standard-size row → BulkUpdatePropertiesInternal per page; sets the naming seed | Spot / BradPoint / CenterDrill / TapDrill PageViewModel |

Representation choices (ours, documented in the code): spindle speed kept positive with the direction in `spindle_dir`; nullable C#
values are always present; setting `taper_angle` also sets `tip_angle` for types 7, 10-19, 21 (this program reads OpToolInfo +0x20 as
`tip_angle`); `raise_pc` has a recursion depth guard that the C# does not have.
Test: `tools/tests/toolwiz_test.c` (standalone; `tool_new` stubbed with the inch defaults of §2.6): checks validation messages, corner
rules, face / taper / slot-mill (lock) / tap controls, shank rules, auto names, SFM / FPT coupling and the Define page rows.

## 3. WPF "Mastercam Tool Manager" application (toolmanager.*.dll, v19.0.0.0)

Path abbreviations: `DN` = `~/mastercam-re/dotnet`; xaml = output of `tools/baml_dump.py` on the named `.baml`;
resx strings are quoted as «text» [ResxClass.Key].

This is the WPF tool-database (`.tooldb`) editor. Whether/how Mastercam's Toolpaths-manager "Tool Manager" command shows
it is discussed in section 0.

### 3.1 Application shell

| Item | Value | Source |
|---|---|---|
| Application name | «Mastercam Tool Manager» | DN/toolmanager.workspace/ToolManager.Workspace.Properties.Resources.resx `ApplicationName`; also DN/toolmanager.core/ToolManager.Properties.Resources.resx `ApplicationName` |
| Caption (message boxes) | «Tool Manager» | ToolManager.Shared.Resources.ApplicationStrings.resx `Caption` (used in ModuleToolManager save-as error, DN/toolmanager.core/ToolManager/ModuleToolManager.cs `_0001(SaveAsEventArgs)`) |
| Shell framework | `MastercamExtensions.Shell` application (`me:MastercamExtensionsApplication`) - Ribbon + Backstage + docking | toolmanager.exe/app.baml root element |
| File type | «Tool Database File», extension `tooldb` | DN/toolmanager.database/ToolManager.Database.ImportExport/ToolDatabaseFileFormat.cs ctor; FileFormatStrings.resx `ToolDatabaseFileName`, `ToolDatabaseFileDescription` «Mastercam ToolDb File» |
| Shell window size, title-bar format, Backstage Save/Open/New/Print/Options/Exit button texts | NOT DECODED - the styles `styleFileSaveButton`, `styleFileSaveAsButton`, `styleFileOpenButton`, `styleFileRecentDocumentsBackstageTab`, `styleFileNewBackstageTab`, `styleFilePrintBackstageTab`, `styleHelpBackstageTab`, `styleFileOptionsButton`, `styleFileExitButton` are `DynamicResource`s defined in MastercamExtensions.Shell, which is not in the decompiled set | toolmanager.workspace/resources/applicationmenu.baml `Backstage x:Key="ribbonApplicationMenu"` |

Backstage (File) menu order (applicationmenu.baml `Backstage`): Save, Save As, Open, **Close** («Close»
[ApplicationMenuXaml.RibbonFileCloseButton_Label], key tip «C», icon ImageFileClose16/32, command
`DocumentWorkspace.CloseDatabaseCommand`), tab Recent, tab New, tab Print, tab «Help»
[ApplicationMenuXaml.RibbonFileHelpBackstageHelpButton_Label], Options, Exit. Only Close and Help texts are in our sources.

### 3.2 Ribbon (toolmanager.workspace/views/ribbonview.baml, strings RibbonViewXaml.resx)

Commands: DN/toolmanager.workspace/ToolManager.Workspace.ViewModels/ToolManagerWorkspaceViewModel.cs ctor.
All `CanExecute` = `CanDoDatabaseCommands` (a database is open) except AddSearchPage, ShowDatabaseExplorer, ShowProperties.

**Tab «Home»** (key tip H)

| Group | Control | Label / screentip | Command (parameter) | Action |
|---|---|---|---|---|
| «Search» | Button, icon ImageFind32, key S | «Search» / «Open the Search Window to search a database and display results matching search criteria.» | `AddSearchPageCommand` | publishes AddSearchPageEvent -> `ActiveToolLibrary.OpenDocument(Search=9)` (ModuleToolManager `_0001(AddSearchPageEventArgs)`) |
| «Database» | Button, ImageDatabaseRefresh32, key R, InputGestureText **F5** | «Refresh» / «Refresh the active database with changes made by an outside source. Changes will be merged into current session.» | `RefreshDatabaseCommand` | publishes RefreshDatabaseEvent |
| «New Component» | Button `InsertHolderButton`, key H | «Holder» / «Open the Holder Wizard to create a new holder. The Holders document opens, and the new holder is added.» | `InsertRecordCommand("Holder")` | |
| | Button, key A | «Assembly» / «Open a new (blank) assembly in the Assembly Tree to create a new assembly. The Assemblies document opens, and the new assembly is added.» | `InsertRecordCommand("Assembly")` | |
| | Button, key M | «Material» / «Open the Materials document to create a new material.» | `InsertRecordCommand("Material")` | |
| | Button, key C | «Cut Parameter» / «Open the Cut Parameters document to create a new cut parameter.» | `InsertRecordCommand("CutParameter")` | |
| «New Milling» | RibbonGallery «Milling» (key NM), enabled = InsertHolderButton.IsEnabled; items at opacity 0.5 when gallery disabled | 48x48 images; screentips below | `InsertRecordCommand(<ToolTypeList id>)` | opens the Define Tool wizard (section 2) for that type |
| «New Hole Making» | RibbonGallery «Hole Making» (key NH) | idem | idem | idem |

Gallery items in ribbon order (screentip [RibbonViewXaml key] -> CommandParameter = `ToolTypeList` value, image key):

- Milling: «Endmill»[RibbonEndMill] 10 ImageFlatEndMill48; «Spherical / Ball-Nosed Endmill»[RibbonSphereMill] 11 ImageSphereMill48;
  «Bull-Nosed Endmill»[RibbonBullMill] 19 ImageBullMill48; «Face Mill / Shell Mill»[RibbonFaceMill] 13 ImageFaceMill48;
  «Roundover Tool»[RibbonRadMill] 15 ImageRadMill48; «Chamfer Mill»[RibbonChfrMill] 12 ImageChfrMill48; «Slotting Tool»[RibbonSlotMill] 14 ImageSlotMill48;
  «Tapered Endmill»[RibbonTaperMill] 17 ImageTaperMill48; «Dovetail Cutter»[RibbonDoveMill] 16 ImageDoveMill48; «Lollipop Cutter»[RibbonLollipopMill] 18 ImageLolMill48;
  «Engraving Bit»[RibbonEngraveTool] 21 ImageEngrave48; «Thread Mill»[RibbonThreadMill] 24 ImageThreadMill48; «Barrel Mill»[RibbonBarrelMill] 25 ImageBarrelMill48;
  «High Feed Mill»[RibbonViewFeedMill] 23 ImageFeedMill48; «Custom Tool»[RibbonCustomTool] 0 ImageUnknownTool48.
- Hole making: «Drill»[RibbonDrill] 3; «Reamer Type»[RibbonReamer] 6; «Fine Boring Tool»[RibbonBoreBar] 7; «Tap»[RibbonTap] 4;
  «Center Drill»[RibbonCenterDrill] 1; «Spot Drill»[RibbonSpotDrill] 2; «Counter Bore»[RibbonCounterBore] 8; «Counter Sink»[RibbonCounterSink] 9;
  «Bradpoint Drill»[RibbonBradpointDrill] 22; «Custom Tool» 0.
- Each gallery also has a `PopupGallery` (InitialColumnCount=3) repeating the same items.

`ToolTypeList` enum (DN/toolmanager.database/ToolManager.Database.Resources/ToolTypeList.cs; display text DatabaseEnumStrings.resx
`ToolTypeList_*`): 0 «Custom Tool», 1 «Center Drill», 2 «Spot Drill», 3 «Standard Drill», 4 «Right-Handed Tap», 5 «Left-Handed Tap»,
6 «Reamer Type», 7 «Fine Boring Tool», 8 «Counter Bore», 9 «Counter Sink», 10 «Flat Endmill», 11 «Spherical / Ball-Nosed Endmill»,
12 «Chamfer Mill», 13 «Face Mill / Shell Mill», 14 «Slotting Tool or Saw», 15 «Roundover Tool», 16 «Dovetail Cutter», 17 «Tapered Endmill»,
18 «Lollipop Cutter», 19 «Bull-Nosed Endmill», 21 «Engraving Bit», 22 «Brad-Point Drill Bit», 23 «High Feed Mill», 24 «Thread Mill», 25 «Barrel Mill» (no 20).

**Tab «View»** (key V): group with Style `StyleRibbonViewWindowGroup` (NOT DECODED, shell style); group «Show»: start-page
button (Style `StyleShowStartPageButton`, NOT DECODED), «Database Explorer» («Open (display) Database Explorer.»,
`ShowDatabaseExplorerCommand` -> `ShowToolWindow("DatabaseExplorerToolWindow")`), «Properties» («Open (display) Properties
Grid.», -> `ShowToolWindow("PropertyGridToolWindow")`), «Assembly Tree» («Open (display) Assembly Tree.», `AddAssemblyTreeCommand`
-> `OpenDocument(Assembly=3)`); group Style `StyleRibbonView3DViewGroup` (NOT DECODED).

### 3.3 Main window layout (toolmanager.core/views/mainview.baml)

Actipro `DockSite MainDockSite` (Margin 0,0,0,3) -> `SplitContainer` of three parts, left to right:

1. `ToolWindowContainer` (DockSite.ControlSize 250,100): ToolWindow Title **"Library Explorer"** (CanAutoHide, not a document) containing `DatabaseTreeView`.
2. `Workspace` -> `TabbedMdiHost` (close button on each tab) -> `TabbedMdiContainer tabbedContainer` - the document tabs.
3. `ToolWindowContainer` (ControlSize 300,500): ToolWindow **"Properties"** (`PropertyGrid`) - a property grid of the selected record (ToolManager.Shared.Controls.WpfPropertyGrid; tooltips «Categorical Sort», «Alphabetical Sort», «Enter Search Criteria», «Property editing not available» [ApplicationStrings `WpfPropertyGridSort*`, `WpfPropertyGridSearchToolTip`, `WpfPropertyGridUninitializedPropertyGridStringToolTip`]).

Database tree (DN/toolmanager.core/ToolManager.ViewModels/ToolLibraryViewModel.cs `OpenDatabase`, lines 133-138; databasetreeview.baml):
the open `.tooldb` node with children
- «Milling / Holemaking» [ApplicationStrings.MillingHolemaking] -> «Tool Assemblies» (doc type 3), «Holder» (4), «Cutting Tools» (6)
- «Materials» (7)
- «Cut Parameters» (8)

Each leaf shows a 16x16 icon (ComponentToImageSourceConverter), `Name` and ` ({0})` item count (`ItemCount`, StringFormat " ({0})").
Opening a leaf opens a document tab. Library document (toollibrarydocumentview.baml): inner DockSite with ToolWindows
"Database Tree View" and "Assembly Tree View" and a workspace; keys Ctrl+V = Paste, F9 = HomeAxes, Shift+F9 = ZeroAxes.

### 3.4 Record list document (toolmanager.core/views/displaywindowview.baml, DisplayWindowView.cs)

`SplitViewDocument`: **Panel A** = `DataGridView` (Xceed DataGrid), **Panel B** = `GAFControl3DView` (3D preview of the
selected tool/holder, visible per `GAFVisibility`) or `ErrorWindowView` (per `ErrorVisibility`). Splitter buttons switch
vertical/horizontal split and maximise a panel (styles `AdvancedGridSplitterVertical` / `...Horizontal`; hidden tabs
"Datasheet"/"Design").

Grid properties (toolmanager.shared/controls/datagridview.baml root + displaywindowview): ReadOnly, SelectionUnit Row,
NavigationBehavior RowOnly, right-click selects row, AutoCreateColumns False, TableflowView with ColumnStretchMode Last,
grid lines #E0E0E0, no row selector, groups InitiallyExpanded False; group-by panel text «Drag a column header here to group by
that column.» [DataGridViewXaml.GroupByControl_TextBlock]; column headers have Xceed AutoFilter dropdowns and sort glyphs;
invalid rows (`IsValid` False) get a red (#FFFF0000) border (`DatagridRowStyle`); selected row brush gradient #FAFBFD->#EBF3FD.
Group keys: Space toggles, Right/+ expand, Left/- collapse, double-click group header toggles.

**Double-click a row** -> `EditCurrentRecord()` (DisplayWindowView.cs `OnMouseDoubleClick`) -> for tools: Edit Tool wizard
(ToolDocumentViewModel.cs `EditCurrentRecord`: deserialised copy, `MetricMode` = tool's IsMetric; on Finish
(`ServiceReturnObjectType == 1`) copies the result into the record).

**Context menu** `DatagridContextMenu` (strings DataGridViewXaml.resx): «Cut» (ApplicationCommands.Cut, ImageCut16), «Copy»
(ToolManagerCommands.Copy, ImageCopy16), «Paste» (ApplicationCommands.Paste, ImagePaste16), «Duplicate»
(ToolManagerCommands.Duplicate, ImageCopyAppend16), «Delete» (ApplicationCommands.Delete, ImageDelete16).
Behaviour (DocumentWindowViewModel.cs / ToolDocumentViewModel.cs):
- Copy: puts the selected entities on the clipboard as `DataObject(typeof(TlTool), List<TlAssemblyItem>)`.
- Cut = Copy then Delete. Duplicate = Copy then Paste (if paste possible).
- Paste enabled when clipboard holds `TlTool` data; executes `ToolDatabase.AddTools(clip)`.
- Delete enabled when a row is selected; deletes every selected entity (reverse order) inside one undo transaction; **no confirmation prompt in this code**.

**Columns - Cutting Tools document** (DN/toolmanager.database/ToolManager.Database.Models/ColumnLists.cs `GetColumnList` default branch;
header strings from Tl*ViewModelStrings.resx; width 0 = not set in code):

| # | Header | Field (binding) | Width | Default visible | Notes |
|---|---|---|---|---|---|
| 0 | «Tool Number» [TlToolViewModelStrings.Tool_Number_Name] | ToolNumber | 60 | yes | |
| 1 | «Name» [TlAssemblyItemViewModelStrings.Component_Name_Name] | Name | 190 | yes | |
| 2 | «Diameter» [TlToolViewModelStrings.Diameter_Name] | OverallDiameter | - | yes | |
| 3 | «Cutting Depth» [TlToolViewModelStrings.Cutting_Depth_Name] | CuttingDepth | - | yes | |
| 4 | «Overall Length» | OverallLength | - | yes | |
| 5 | «Corner Radius» [TlToolEndmillViewModelStrings.Corner_Radius_Name] | CornerRadius | - | yes | |
| 6 | «Radius Type» [TlToolEndmillViewModelStrings.Radius_Type_Name] | TlRadiusType | 150 | yes | enum -> description (CornerTypeList: «None», «Chamfer», «Corner Radius», «Full Radius», «Roundover») |
| 7 | «Number of Flutes» | FluteCount | - | yes | |
| 8 | «Metric» [TlAssemblyItemViewModelStrings.Is_Metric_Name] | IsMetric | - | yes | |
| 9 | «Catalog ID» | CatalogID | - | no | |
| 10 | «Crib Location» | Location | - | no | |
| 11 | «Description» [TlToolViewModelStrings.Part_Description_Name] | Description | - | no | |
| 12 | «Thru Coolant» | ThruCoolant | - | no | |
| 13 | «MC Tool Type» | MCToolType | - | no | enum -> description |
| 14 | «Manufacturers product line.» [TlToolViewModelStrings.MFG_Tool_Code_Desc - note: the code uses the *_Desc* string as header] | MfgToolCode | - | no | |
| 15 | «Variable Pitch» | IsVariablePitch | - | no | |

Default visible list: `ColumnLists.GetDefaultColumns(Tool)` =
`"ToolNumber,Name,OverallDiameter,CuttingDepth,OverallLength,CornerRadius,TlRadiusType,FluteCount,IsMetric"`, stored in
`Settings.Default.ToolColumns` and re-saved whenever a column's Visible changes (DisplayWindowView.cs `UpdateColumns`,
`UpdateUsersDocumentColumns`, `CheckUserSettings` - visibility test is `ToolColumns.Contains(fieldName)`, a substring test).
Enum columns get `XceedEnumToDescriptionValueConverter` and an enum group description. Number format / unit suffix of the
numeric cells: NOT DECODED (plain `DataGridBindingInfo` with no converter/StringFormat in `UpdateColumns`).

Other documents (same ColumnLists.cs): Holders: Name 180, Description 180, Upper Connection Type (enum), Upper Connection Size,
Lower Connection Size, Lower Connection Type (enum), Metric, Holder Type, Thru Coolant, Manufacturer Name, Location, Catalog ID;
default visible `Name,Description,UpperConnectionType,UpperConnectionSize,LowerConnectionSize,LowerConnectionType,IsMetric`.
Assemblies: Tool Number 65, Assembly Name 180, Holder Name 180, Tool Name 180, Diameter Offset 180, Length Offset 180, Cutting Depth,
Overall Diameter, MC Tool Type (enum), Number of Flutes 65, Description 65, Overall Length 65, Tool Grade 65; default visible
`ToolNumber,Name,MainHolderName,MainToolName,DiameterOffsetNum,LengthOffsetNum`. Materials: Name 325, Description 300, ISO Group
Value (enum), Hardness Units (enum), Hardness; default `Name,Description,ISOGroup`. Cut Parameters: Name 250, Description,
Material ISO Group, Operation Type, Tool Grade, Manufacturer Name, Surface Speed, Feed Per Tooth, Cut Source (enum), Proven;
default `Name,Description,ISOGroup,TlOpTypeName,TlToolGradeName,TlManufacturerName,CutSpeed,FPT`.

### 3.5 Drag and drop / copy between libraries

DisplayWindowView.cs `DataGridDragOver`, `DataGridDrop`, `IsDroppable<TM>`, `InitiateDragDrop<TVm,TM>`:
- Drag starts after the mouse moves more than `SystemParameters.MinimumHorizontal/VerticalDragDistance` with the left button
  down (`IsDragging`); data = `DataObject(typeof(TlTool or TlHolder), List<TlAssemblyItem>)`, effect **Copy**; the source grid's
  AllowDrop is switched off for the duration (no drop onto itself).
- A grid accepts `TlTool` data only if it is a Cutting Tools document (type 6) and `TlHolder` data only if it is a Holders
  document (type 4); otherwise effect None.
- Drop of tools -> `ToolDatabase.AddTools(data)`; drop of holders -> `ToolDatabase.AddHolders(data)`.

### 3.6 Numbering rules (tool number, D/H offsets)

DN/toolmanager.database/ToolManager.Database.Services/ToolDatabase.cs:
- `GetNextToolNumber(isCatalogItem)`: `SELECT ToolNumber FROM TlTool t join TlAssemblyItem a on t.ID=a.ID where a.IsCatalogItem = 1|0 Order By ToolNumber Desc LIMIT 1`; result = that + 1 (capped at int.MaxValue), or **1** when empty.
- New tool from the ribbon (ToolDocumentViewModel.cs `InsertRow`): wizard `CreateToolWizardParameters` with `ToolNumberDefault = GetNextToolNumber(true)`, `AddToDiamterOffsetValue = 0`, `AddToLengthOffsetValue = 0`, `MetricMode = GeneralOptions.IsMetric`, `DefaultToolType = <ribbon id>`, `InitialPage = ToolWizardPages 1`.
- Pasted / dropped tools (`AddTools`): each copied tool is deep-copied and renumbered `next, next+1, ...` starting at `GetNextToolNumber(true)`.
- `AddTool(..., incrementToolNumber=false)`: keeps the tool's own number if it has one, else next number.
- New assembly: `ToolNumber = GetNextAssemblyToolNumber()` (`SELECT ToolNumber FROM TlAssembly Order By ToolNumber Desc LIMIT 1` + 1, or 1) and `DiameterOffsetNum = LengthOffsetNum = ToolNumber`.
- Editing an assembly's tool number (TlAssemblyViewModel.cs `ToolNumber` setter) sets the tool's and the assembly's ToolNumber, LengthOffsetNum and DiameterOffsetNum all to the new value.

### 3.7 Open / save / close

- Data is a SQLite `.tooldb`; edits are committed per operation (`LocalConnection.SaveChanges`, `FinalizeDatabaseTransaction` in ToolDatabase.cs `AddTools`/`AddTool`/...), with undo/redo intervals per document type. There is no separate "Save" step in the decoded code (the Backstage Save style is NOT DECODED).
- **Save As** (ModuleToolManager.cs `_0001(SaveAsEventArgs)`): only if a database is open and its file exists; save dialog filter «Tool Database File» (*.tooldb), initial name = database name; if a different path is chosen: `File.Copy(src, dst, overwrite)`, close the current database, open the copy. On failure: message «Could not copy existing database to destination» [ToolManagerStrings.SaveAsCouldNotCopyExistingDatabase] + newline + exception text, caption «Tool Manager».
- **Close**: `DatabaseClosingEvent` -> `ActiveToolLibrary.CloseDatabase()`.
- Open failure text: «A call to Open Database has failed.» [ToolManagerStrings.OpenDatabaseFailed].

### 3.8 Search window (filter) - toolmanager.search/views/searchdialog.baml, SearchDialog.cs, SearchViewModel.cs

Own ribbon: tab «Home» with group «Scope»: «Tools», «Holders», «Assemblies», «Materials», «Cut Parameters» (each «Open a searchable
... datagrid from the current TOOLDB ...» [SearchDialogXaml.SearchDialogRibbon*_Description]); group «Actions»: «Search»
(«Filter and display current database search results based on your entered conditions and critieria.»), «Accept» («Accept your
selected datagird choice. The Accept button is only active when you access the Search feature from within the Mastercam
interface.»); group «Searches»: «Recent», «Favorite» (popup «Add Current Search to Favorites», «Name:», «Add»). Left panel
"My Searches": trees «Default Searches» and «Favorites». Criteria grid columns (SearchDialog.cs ~line 585): «Search Item» (240),
«Condition» (120), «Value» (100); first row = new `SearchCriterion(DocType, 0)` (default condition "=", SearchCriterion.cs:146).
Condition lists: numeric/string fields `= != > >= < <=`; enumerated fields (MCToolType, TlCutQuality, TlCutSource, ISOGroup,
HardnessUnit, TlSpindleDir, TlToolGradeID, TlGradeID, TlManufacturerID, Upper/LowerConnectionType, TlWorkMaterialID,
TlToolMaterialID, TlOpTypeID) only `= !=` with a dropdown value editor; bool fields get a boolean editor. Result grid uses
`ColumnLists.GetColumnList(DocType)` (3.4). Validation messages: «double value expected», «integer value expected», «boolean value
expected», «Invalid value», caption «ToolManager Search» [SearchViewModelStrings]. Progress text «Searching...».

### 3.9 Assembly tree context menu (assemblytreeview.baml, AssemblyTreeViewXaml.resx)

«New Assembly» Ctrl-A (`NewAssembly`), «Rename» F2 (`EditComponentNameCommand`), «Paste» Ctrl-V (`PasteCommand`), «Delete» Del
(`DeleteTreeItemCommand`), «Prune» Shift-Del (`RemoveToolCommandReference`).

### 3.10 Options (strings ToolManager.Shared ApplicationStrings.resx; GeneralOptions.cs)

«General Tool Manager settings»; «Units» / «Adjust the default unit settings.» / «Default units: » (GeneralOptions.IsMetric -> new
tools/holders metric); «Search Options» / «Adjust the settings for searching a database.» / «Display result grid tool-tip preview» /
«Tool-tip hover delay (seconds): » / «Maximum saved recent searches:» / «Enable auto-naming for assemblies, tools, and holders».
Dialog layout: NOT DECODED (no BAML for it in our set).

### 3.11 Messages (main window)

ToolManagerStrings.resx / ApplicationStrings.resx (ToolManager.Shared): «Copying database item failed:\n» [CopyDataFailed],
«Pasting selected Item(s) failed» [DatagridControlPasteSelectedItemFailed], «Copying database item failed»
[DatagridControlDropCopyingDatabaseItemFailed], «Drag drop error:» [DragDropErr], «Error loading tools» [ErrLoadTools],
«Error loading holders:» [ErrLoadHolders], «Something went wrong: Tool being added is not being recognized as a tool type.»
[SomeWentWrong], «Something Went Wrong During Profile Generation» [SomeWentWrongProf], «Adding Tool to Assembly failed.»,
«Adding Holder to Assembly failed.», «Adding Assembly failed.», «Error: No Assembly Selected», «Exporting tool profile failed.»,
«Mastercam timed-out attempting to connect to Tool Manager.\n\nPlease retry the operation. If doing this does not fix the problem,
contact your Mastercam Reseller for assistance.» [ConnectionTimeOut], «Failed to launch» [FailToLaunch]. Invalid-tool list texts:
«Invalid Required General Properties», «Invalid Required Endmill Tool Properties», «Invalid Required Drill Tool Properties»,
«Invalid Required Reamer Tool Properties», «Invalid Required Threading Tool Properties»; bad-tool entry format
`DatabaseErrorStrings.ErrorLoadingTool + ": " + ProfileErrorMessage + "; " + DatabaseErrorStrings.ToolNumber + "=" + n + " DB(" + name + ")"`
(ToolDatabase.cs `UpdateBadToolList`).
