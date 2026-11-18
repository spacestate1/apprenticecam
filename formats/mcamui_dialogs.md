# mcamui dialogs and manager tabs - code map (viewer/*.inc)

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

Code map of the UI include files of viewer/mcamui.c that have no other spec: the operation parameter dialog
(opparams_ui.inc), the Chaining / Chaining Options / Drill Point Selection dialogs (chaindlg_ui.inc), the Toolpaths
manager item texts (optree_text.inc), the Solids manager (solidsmgr_ui.inc) and the Levels / Planes / Recent Functions
manager tabs (levels_ui.inc, mcamui.c). Written 2026-10-16 from the code and its comments; the decode history is in
formats/improvisation_review.md "Replacement run" (UI items 10, 12, 15, 16, 20a, 20 manager tabs, 20 op Tool page).
Every source below is the one the code cites. Unknown = NOT DECODED. No emojis, no invented behaviour.

Other specs: Tool Manager = formats/toolmanager_ui.md; wireframe panels = formats/wireframe.md "Code"; Extrude panel =
formats/extrude.md; backplot = formats/backplot.md 14; chaining behaviour = formats/chaining.md; right-click menu of
the Toolpaths manager = formats/opmanager_menu.md; analyze / level picker = formats/analyze.md / level_picker.md.

## 1. Operation parameter dialog (viewer/opparams_ui.inc, mcmill ToolpathDlg)

(2026-10-21, Qt port phase 6: the model below moved to viewer/core_ui - cui_opdlg.c (page tree, pages = `pp_page_body`,
Linking = `lk_rows` / `lk_set`, pictures, frame, quick view), cui_toolpage.c (Tool page = `tp_ctls` / `tp_ctl_set`, menu 1247 =
`otp_menu`, Tool List = `TL6410` / `tl6410_*`), cui_ops.c (`open_params`, `generate_pending`, the Toolpaths manager menu);
the names in the tables are kept. opparams_ui.inc and viewer-qt/src/opdlg.cpp draw it.)

Decompiles: decomp/mcmill_all.c, decomp/mccore_tpdlg.c, decomp/mccore_tptree.c; fields: formats/ui_fields_mcmill.tsv;
per-control record offsets / enums / enable rules of the op dialog pages: formats/opdlg_fields.tsv (batch 0, 2026-10-22).

**Title** (FUN_18026dc70): mcmillres 2755 "2D Toolpaths" (opcodes 1, 3, 0x66, 0x69, 0x82, 0x85) or 2707 "2D High Speed
Toolpath" (0x86), " - ", then GetToolpathTypeName: ChainBasedToolpathDlg FUN_18026d220 (mccoreres 5015 Contour, 5016
Pocket, 5017 Facing, 5018 Slot Mill), PointBasedToolpathDlg FUN_180270050 (4343 Circle Mill, 4394 Thread Mill, 4397 Helix
Bore; drill = mcmillres 2756 "Drill/Circles" + " " + the cycle text), HST FUN_18026ea30 (mcmillres 2777 Dynamic Mill, 2773
Area Mill, 2818 Dynamic Contour, 2703 Peel Mill, 2713 Blend Mill by style +0x32b0).

**Page tree**:

| code | behaviour | source |
|---|---|---|
| `PG_*`, `PG_TITLE[]` | 35 page slots and their titles | FUN_18014a700 string list (FUN_1800c0200: mccoreres / mcmillres) |
| `pg_has(p, s)` | which pages the op type has: MakeSpecificPages slot list x the page factory's NULL cases; product bits taken as not Mill-Turn (& 0x100) and no multiple heads (& 0x40) | ChainBased FUN_18026d480, PointBased FUN_1802701e0, HST2D FUN_18026eb40; factories FUN_18026c800 / FUN_18026f760 / FUN_18026e210 |
| `PG_TREE[]`, `pg_parent_ok` | parent of each slot in insertion order; a slot appears once, under the first parent present | ToolpathTree builder FUN_18014a700 |
| `pg_done(p, s)` | pages this program shows: Tool, Linking (rows below; Thread Mill: template 2837), Planes (1.x), Coolant, and every page with a decoded template (`pf_dlg`, 1.y: 43 templates; 2D chamfer contour keeps Depth Cuts / Tabs greyed as before); the rest (Toolpath Type, Holder, Home / Ref. Points, Canned Text, Misc Values, Axis pages) are greyed in the tree and cannot become the active page (a click keeps the current page, as ToolpathDlg::ActiveNewPage 0x18088f870 -> SelectNewPage(s_ActivePage) when MakePage gives no page; `pp_page` is s_ActivePage, kept between dialogs) | porting status ours; click rule ActiveNewPage / OnNotify 0x180893840 |
| `draw_op_params` | mccoreres dialog 6544 at template positions (see Graphics); OK = `generate_pending` (regenerates; height flags applied there) | ToolpathDlg ctor 0x18088f1a0 = MCDialog(0x1990) |

**Linking Parameters** (mcmillres dialog 25596, HSTLinkPageBase 0x63fc, DDX FUN_180239350; drill dialog 25593,
HSTDrillLinkPage 0x63f9):

| code | behaviour |
|---|---|
| `OpHFlags` | clr_on / clr_end / clr_inc, ret_on / ret_inc, fp_inc, top_inc, dep_inc, chain Z, floor stock, clearance value kept while off |
| `hf_from_record` | flags from a stored op record: 0x178 / 0x179 / 0x319 clearance inc / on / start-end only, 0x188 / 0x189 retract inc / on, 0x198 feed plane inc, 0x2f0 top inc, 0x1a8 depth inc, 0x888 floor stock, values 0x170 / 0x2e8 (record offset = op offset + 0x68: op +0x108 / +0x110 / +0x2b1 / +0x118 / +0x120 / +0x121 / +0x128 / +0x130 / +0x280 / +0x288 / +0x138 / +0x140) |
| `hf_from_op` | an op made here: the height bytes saved with it (rpatch), else this program's save convention (clearance / retract absolute, feed plane incremental except drill) |
| `page_linking`, `lk_row` | rows Clearance (check + value + Absolute / Incremental + "Use clearance only at the start and end of operation"), Retract (check; drill: the feed plane field, no switch), Feed plane, Top of stock, Depth; incremental shown relative to the top (clearance / retract / feed plane) or to the chain Z (top / depth) as mccore set_depths; the "..." buttons arm a point pick (mccore tp_select_depth 0x1800cd910, decomp/mccore_tp_select_depth.c, via HSTLinkPageBase::OnClearancePb / OnRetractPb / OnFeedPb / OnStockPb / OnDepthPb, prompt mccoreres 5321 "Select point"; the dialog is hidden for the pick and comes back after it: AfxSuspendWindow(dialog, 0, 6) / AfxResumeWindow(0, 9) around tp_select_depth in FUN_180019090 / FUN_180019160, decomp/mcmill_all.c:18975 - both apps since 2026-10-21): the field gets the picked point's Z (AutoCursor entity snap carries its Z, a missed click the cursor-plane depth `grid_z`; no top / bottom-of-model snap exists in the original); drill Subprogram greyed |
| `hf_apply` | at OK: values and flags written as height record patches (`rp_put`, as cam_fileop.c rpatch_set), clearance / retract of the motion from the on switches (params_from_record's rule) |

Heights of a new op (mcamui.c `heights_from_default_op`, UI item 5): the Mill_MM / Mill_Inch default op's height fields
evaluated with mccore set_depths' rule (decomp/mccore_depths.c @18089f680) against the first chain's (or point's) Z.
Deviation (code / review): our CamParams are absolute, evaluated once at the first chain's Z (multi-Z chains get one
depth); clearance off -> rapids at retract / feed plane.

**Cut Parameters** (before 2026-10-22 batch 1; now template pages, 1.y - `pp_cut_page` stays for a page without a template): per type, sources in the code comments - Drill 25595 (Cycle = mccoreres 7645 + 11 n,
1st peck +0x1230), Contour 2938 (type 4561 "2D" / 4565 "2D chamfer"), Face 2942 (Style mcmillres 2675-2679, Max.
stepover +0x1240), 2D High Speed 2784, Thread Mill 2824, Slot 3020, Helix Bore 2823, Multi Passes 25591, Pocket Rough
3003 / Finish 3004 / Entry 3010, Filter 318 (the code's citation; the 2017 dialog's page is mccoreres 10318
ArcFilterRefinePage and our "Create arcs in XY" is CamParams.arcs - formats/opdlg_port_audit.md 6), Coolant (mcmillres 126 / 124 / 2287), Tip comp control 1310; point ops show
mcmillres 2397 "0 points defined!" when empty. Ours: Dynamic Contour wall controls (NOT DECODED), Peel "Slot width" 0 =
1.5 D, Thread Mill major diameter 0 = the picked circle's, thread depth / full profile (not Mastercam fields), Circle
Mill helix pitch (page not mapped).

**Tool page** (`page_tool`, `otp_list`, `otp_draw_menu`, `draw_tool_list_6410`; decompiles decomp/mccore_toolpage.c,
mccore_ctoollist.c, mccore_selmillasm.c, mccore_toollibrary.c, mdef_toollibfile.c, mdef_turret.c; 2026-10-21).

Class / template: the mcmill page factories (FUN_18026c800 case 2, FUN_18026f760, FUN_18026e210) build mccore
**ToolPageHEM** (ctor 0x18087e850, dialog 25524 = 0x63b4) for opcodes 3 / 0x66 / 0x69 / 0x12 (Pocket, Facing, Slot,
Circle Mill) and all 0x86 (2D High Speed), and **ToolPage** (ctor 0x18087e810, dialog 6556 = 0x199c, no HEM controls,
turret group on the right) for the rest (Contour, Drill, Thread Mill, Helix Bore). (mcmill CToolProp = mcmillres dialog
152 is the old non-tree page, not used.) Common code ToolPageBase, op = dialog +0x20f0.

| control | behaviour | source |
|---|---|---|
| list 25463 | the page's own CMillToolListCtrl (page +0x15a8, default ctor 0x180505e90: display mode 2, every set sorted by column 2 ascending, the Tool Manager's column defaults and cell texts); its column set is read at InitPage (ReadToolIconPrm 0x18088ac60: ReadRegistrySettings under REG_KEY\GUI\Mill Tool Display\Tool List Settings - static initialisers 0x180004244 / 0x180004214 / 0x180004274; not the Tool Manager's "Part Tool List") and written at OK (UpdateLocalOp 0x18088d2a0 when the page was made, +0x1570 -> WriteToolIconPrm 0x18088ddc0); ours: toolpage.conf. A header click = LVN_COLUMNCLICK (message map 0x180df98c0) -> OnListClickColumn 0x180889340 -> SortList(iSubItem) 0x18050b3e0: the same column toggles the direction, another starts ascending; rows in SortColumn 0x18050b060 order (cui_toolmgr.c `tm_cmp`; cui_toolpage.c `tp_rows` / `tp_sortlist`, Qt `opdlgset:tpsort=C`). click = OnItemSelect (feeds / speeds from the source); right click selects the row and opens menu 1247 (its Arrange tools 32980 / 32981 = SortList 2 / 4: not ported) | DDX 0x180883180, OnContextMenu 0x180887930 |
| 3561 / 3563 / 3565 / 3566 / 3567 / 3569 | diameter, corner radius, name, number, length / diameter offset of the tool, read-only (their kill-focus handlers write the part tool, UpdateDbTool: not ported; use Edit tool...) | OnKillfocusToolDiameterEdit 0x180889020 etc. |
| 3568 Head number | op +0x53c; 0 shown (value for a new op NOT DECODED) | MapToDlg 0x180886c10 |
| 3575 Feed rate | op feed; FPT follows | OnKillfocusFeedrateEdit 0x180888980 |
| 3747 FPT | feed / (z x abs rpm), z = 1 for tool types 4 / 5 else flutes; taps with pitch: the pitch; edit: feed = abs rpm x z x FPT | CalculateFPT 0x180882ee0, OnKillfocusFPTEdit 0x1808888a0 |
| 3579 Plunge rate | op plunge (drill: disabled) | InitControls 0x180885cc0 |
| 3207 Spindle direction | CW / CCW (mccoreres 3993 / 3994); CCW = negative rpm | InitCombos 0x180885c40, GetDialogSpindleSpeed 0x180884870 |
| 3577 Spindle speed | abs rpm; SFM / FPT follow | OnKillfocusSpindleSpeedEdit 0x180888e70 |
| 3743 SFM (label 3741 = 7569 "SFM" inch / 7570 "CS" metric) | abs rpm x D / (metric 318.3, inch 3.82); edit: rpm = (uint)(k x SFM / D), feed = abs rpm x z x FPT | CalculateSFM 0x180882fb0, OnKillfocusSFMEdit 0x180888d60, InitPage 0x1808864a0 |
| 3581 Retract rate | op +0x320 = record 0x388 (a record patch at OK), enabled only when Rapid Retract is off (drill: disabled); a picked tool sets it (GetFeedsAndSpeedsFromTool 0x180884bf0: tool +0x40; From material: feed x 0.5) | MapToDlg, OnRapidRetractCb 0x180889460 |
| 3305 Rapid Retract / 25196 Force tool change / 25601 To batch | op +0x141 / +0x289 / +0x291 = record 0x1a9 / 0x2f1 / 0x2f9: start from the stored record or the defaults op, written as record patches at OK; rapid retract also drives the motion (feed_retract = !0x1a9, mc_rapid_ret) | MapToOp 0x180886e20 |
| 3583 Comment | the op comment (op +6 = record 0x6e, pending.name; EM_LIMITTEXT 0x77, our buffer 63) | MapToDlg / MapToOp, InitControls |
| 3605 / 3607 / 3609 | UIDivider lines | DDX |
| 3293 Select library tool... | Tool List 6410 | OnSelectLibToolPb 0x1808895d0 |
| 3306 Safety zone enabled | hidden | end of InitPage |
| 3055 Tool inspection / change | shown only for opcode 0x84, multiaxis, or 0x86 style 1 (Area Mill: drawn greyed, values not ported); hidden otherwise | ShowToolInspectionGroup 0x18088cc50, InitPage, OnSetActive_Page 0x1808898b0 |
| turret group 25587.. | hidden unless CCNCMachine::HasAlternateIndexPositionTurret (machine Type +0xd8 == 2; mill records read 1) | ShowTurretIndexPosCtrls 0x18088cce0, mcmachinedef 0x180069da0 |
| HEM (25524): 10156 Iscar HEM, 10159 / 10160 / 10165 HEM factor + trackbar, 10207 "* Machine limited", 10158 RCTF | prm_hem flags: [0] IsHemAvail (HST style 0 / 2 + Iscar manufacturer: not read -> hidden), [1] HEM on (0: factor / trackbar / machine-limited hidden, RCTF shown); RCTF enabled by IsRctAvail (tool +0x978 != 3: NOT DECODED -> greyed) | ShowHideHemCtrls 0x180881690, GetHemCtrls 0x18087f6a0, IsHemAvail 0x180880130 |

Greyed / NOT DECODED here: Recalculate values 5782 (posts UWM_TOOL_CHANGED, handler not decoded), Filter Active 25263 /
Filter... 3056 (CToolFilt not ported), RunMachineDefChecks (feed / rpm limits), a tap's calc_tap_rpm / calc_tap_feed, the
CCW sign of a picked tool (tp_tool +0x2c0).

**Menu 1247** (mccoreres; ProcessContextMenu 0x18088a4e0): Select library tool... 33074 (6410), Create new tool... 32972
(AddNewTool: Define Tool wizard, the new tool is then selected), Edit tool... 32973 (needs a selection; Edit Tool wizard),
Tool manager... 32976 (+ "  (" library name ")"; opens the Tool Manager), Re-initialize feeds & speeds 33067
(SetFeedsAndSpeedsFromSource). Greyed: Edit projection 32987, Edit assembly names 33215, Get angled head 32975 (text 7524:
product bit 0x40 off), Save tool to library 33002, View 32977-32979 / Arrange 32980-32981 (this list), Feed speed
calculator 32983, Search for cut parameters 32982. Removed as decoded: Get block drill 32974, Load tool on machine 33001
(FromMCExtensions = machine +0x1c8 = 0), 33069 / 33072 (always), 33075 / 33079 (registry key, taken as absent).

**Tool List 6410** (CToolList, ctor 0x180869260, run by FUN_18086e220 with the library flag +0x230a; OnInitDialog
0x18086c4b0): label 10253 = CCNCMachine::ToolLibFile(machine, Units()) (machine +0xb26 / +0xc2b = machine record +0xab6 /
+0xbbb, McamFile.toollib; a new part has no machine group, so the operation defaults library's (config 2303) machine
record is used - the base mcam_saveops new_base writes); list = ToolLibrary(NULL) -> TlToolLibraryX::GetTools
(FUN_18086a480): the library open in the Tool Manager, else that machine library is opened ("%MCAMDIR%" / relative names
under the shared folder: expansion NOT DECODED, measured). Filter Active 25194 starts checked (dlg +0x20230 = 1; the
filter itself not ported), Display mode 10407-10410 = "Tool Manager Display Mode" of this dialog's own key (greyed; below),
"%d of %hd tools displayed" (3805). **The list** (N13-32, decoded 2026-11-14): control 1762 (the template's SysListView32; our
TL6410 table names it 25463) is the dialog's own CMillToolListCtrl (CToolList +0x1c60; default ctor: +0x6a9 = 1, so Rad. Type is
shown). OnInitDialog -> FUN_18086d050: "Tool Manager Display Mode" from REG_KEY\GUI\Mill Tool Library Selection (accepted if < 3,
else 2; not the Tool Manager's GUI\Mill Tool Manager) and ReadRegistrySettings(...\Tool List Settings) - the strings set by mccore's
static initialisers 0x180004080 / 0x180004050 / 0x1800040b0 (DAT_181431620 / 28 / 30) - then SetListDisplayMode, FillList(-1).
CToolList's message map 0x180df6000: WM_NOTIFY LVN_COLUMNCLICK from 1762 -> 0x18086bdb0 = SortList(iSubItem) 0x18050b3e0 (same
column toggles, a new one ascending; SortColumn order). DestroyWindow 0x18086a0a0 (OK and Cancel) -> FUN_18086dd30
"CToolList::WriteParameters" writes the display mode and WriteRegistrySettings under the same keys. Ours: toollist.conf (cui_toolpage.c
`tl6410_rows` / `tl6410_cols` / `tl6410_sortlist`; a row maps to its library tool; test keys `tl6410:sort=C|rows`, Qt
`tldlgset:sort=C|row=R|ok|cancel`). The header's commands 0x80ba-0x80c0 (view mode / Arrange: SortList 2 / 4 / 10 / 6, FUN_18086bde0)
belong to the dialog's menu 0x4ca / 0x4cb: not ported. OK adds the tool to the part (part_add_tool) and the op. New Library, Filter, Help
greyed. Default tool of a new op: InitOperation rule (first part tool with is_mill_tool / is_drill_tool for the opcode;
Drill gets none); with no tool, OK says mccoreres 3837.

Measured on the way: the existing matlib read (+0xce7) is a second string 0x27 bytes into the material field (+0xcc0);
in the 2017 defaults files it reads "ls" (the X9 parts give "\Materials\Default.materials-9"): left as is (feeds
regression), to be moved to +0xcc0 with the "%MCAMDIR%" rule.

Greyed pages (not ported): Toolpath Type (TreeStyleDlgBase::ChangeOperationType), Holder, Stock, Break Through, Home /
Ref. Points, Planes (WCS), Canned Text, Misc Values, Axis Control / Rotary Axis Control, Tab Cutoff; HST Trochoidal /
Transitions / HST Leads / Contour Wall. NOT DECODED: HST tree builder FUN_18014c4a0 nesting (the chain builder's order is
used), the drill title's post cycle text (page 45; the mccoreres 7645 + 11 n names are used), Help.

**Graphics** (2026-10-21; decompiles decomp/mccore_tpdlg_frame.c, mccore_tpdlg_qv.c, mccore_tpdlg_qvdata.c,
uictrls_dlgbuttons.c (new Ghidra project ~/mastercam-re/ghidra_ui = uictrls.dll), mastercam_mcdialog_init.c, mcmill_all.c).
Images come only from the user's pack: tools/extract_icons.py now also packs mcmillres and uictrls BITMAPs
(`<dll>_BMP_<id>.png`), a 192,192,192-clear copy `<dll>_BMPK_<id>.png` for the bitmaps the code draws transparent (list
`KEYED`), and mcmillres PNGs `mcmillres_PNG_<id>.png`; setup_from_install.py extracts uictrls.dll too. Missing image =
blank frame (buttons: their tooltip text). Code: opparams_ui.inc "dialog frame graphics", `draw_op_params`, `draw_cut_page`.

| element | source |
|---|---|
| frame | mccoreres dialog 6544, 567 x 395 du (ToolpathDlg(CWnd*, int) 0x18088f1a0, used by TreeStyleDlgBase 0x1809097f0): tree 25455 4,33 115x170; "Quick View Settings" 9000 4,211; list 3329 4,223 115x129; legend 3608 / 3610 "= edited", 3611 / 3620 "= disabled"; page frame 3667 127,33 435x319; OK 1 / Cancel 2 / Apply 12321 / Help 9 at x 404 / 444 / 484 / 526, y 373, 36x18 |
| tree images | ToolpathDlg::OnInitDialog 0x180893000: image list 16x16, mccoreres 0x5c5 (1477) / 0x5e5 (1509) / 0x5da (1498), mask 0xc0c0c0, set on 25455; ToolpathTree::SetTreeClean / SetTreeDirty / SetTreeDisabled 0x18089bb10 / bb0 / cc0 = image 0 / 1 / 2 (FUN_18014a700 inserts every item with image 0). Legend statics: UIBitmap 0x5e5 / 0x5da |
| tree states | OnPageDataChanged 0x180893c70 (wParam page, lParam 0 clean / 1 dirty / 2 disabled); ToolpathPageBase::SetPageDataChanged 0x180899190 posts 1 from the page handlers (here: the page changed the op during a click / key); InitializeTreeStates 0x180892490 posts 2 for pages whose IsPageDisabled (vtable +0x3e8) is true: DepthCutsPageBase 0x1800634c0 (op +0x800), HSTContourLeadIOPage FUN_180171300 (+0x888), HSTMultiPassPage 0x1801772b0 (+0x840), HSTTabPage FUN_18018f030 (+0x1318), HSTPocketFinishPage FUN_18017ed20 (+0x11d1), HSTDrillTipCompPage 0x1801655a0 (+0x1200); others = base 0 |
| toolbar | InitToolBar 0x180891be0 + AddDefaultsButtons 0x18088faa0 (this[0x729c] = 0 images, mccoreres): Tool Manager... 5699 (cmd 3223, tip 32932) / separator / Save parameters to defaults file 9433 (32933), Reload parameters from defaults file 9431 (32935) / [opcodes 0x84, 0x86, 0x87: separator, Feed speed calculator... 9435 (32938)] / separator, Preview toolpath... 10501 (32939, opcodes 1, 2, 3, 0x12, 0x13, 100, 102, 105, 106, 130, 134, 135, 0x132) / Hide dialog 10320 (403); 24x24 images (mask 0xc0c0c0) in 34x33 buttons docked top. Only Tool Manager is live (this program's Tool Manager); the rest greyed (not ported) |
| OK / Cancel / Apply / Help | uictrls.dll UIDialog::AddDefaultBitmapButtons 0x1800094f0 table at 0x180094010 {7003 IDOK, 7005 IDCANCEL, 7004 12321, 7006 IDHELP} applied by SetBitmapButtons 0x18000ac10 (UIBitmapButton, transparency 0xc0c0c0); captions "\|OK" = text "" + tooltip "OK" (MCDialog::InitializeAutoTips 0x1802ec440, mastercam.dll). Apply starts disabled (OnInitDialog EnableWindow(0x3021, 0)) and is not ported; Help not ported |
| quick view | InitQuickViewList 0x1808917e0 (2 columns 45 % / 53 %, LVS_EX 0x4020), AddQuickViewItem 0x18088fc90, QuickViewData::GetDisplayString 0x1805ee500 ("%d", "%ld", "%g", On 5130 / Off 5126, string). Rows in slot order: ToolPageBase::GetCustomizableData 0x1808840e0 (Tool, Tool Diameter, Corner Radius, Feed Rate, Spindle Speed, Length Offset, Diameter Offset), CoolantPageBase 0x180232ef0 (Coolant, slot 5), PlanesPageBase 0x1805c4b90 (Cplane / Tplane, slot 9, GetPlaneName - always Top here), HSTDrillTipCompPage FUN_180165190 (Tip comp, slot 13, mcmillres 319 / 2287 / 2288) |
| Linking 624 | HSTLinkPageBase::InitPage 0x180174680 SetBitmap 0x648d (25741); drill HSTDrillLinkPage::InitPage 0x180163a40 0x64b6 (25782, transparent) |
| Contour cut 2938 | 1358 FUN_180158db0: comp type Off 188, else Left 178 / Right 181; 2908 HST2DCutParametersPageBase::SetStockToLeaveBmp 0x180147720 (walls / floors vs tol: 25744-25747, 25947-25949) |
| Pocket cut 2940 | 1358 FUN_18017bf60: Climb 25742 / Conventional 25743 (radio 25113 = op +0x11d2 == 0, record 0x123a; radios drawn greyed); 2908 as contour |
| Face cut 2942 | 1360 FUN_18016ba50 / FUN_18016caf0 by style +0x11c0: 0 25753, 3 25755, 5 25756, else 25754 |
| Slot cut 2944 | 3132 FUN_18018d3b0 25783 |
| Drill cut 25595 | 2938 DrillCyclePageBase::UpdateCycleBitmap 0x18028c780: cycle 0-7 = 25804, 25807, 25802, 25810, 25808, 25809, 25805, 25806; custom 25803 |
| Depth Cuts 25598 / 3030 | 2918 HSTDepthCutsPage::MapToDlg 0x1801613f0 -> DepthCutsPageBase::SetControlBitmap 0x180063860: 25704 + (Tapered walls 1381 ? 4 : 0) (contour record 0x130a, pocket 0x1288; hidden for opcodes 0x12 / 0x66 / 0x69) |
| Lead In/Out 3013 / 2793 | 2917 UIPNGImage FUN_180172910: entry Perpendicular / Tangent (record 0x8f9) x exit (record 0x941) -> PNG 25657 / 25659 / 25664 / 25658 |
| Multi Passes 25591 | 2939 HSTMultiPassPage::InitPage 0x180176fe0 25909; OnBnClickedMultipassCb 0x180177a00 25909 off / 25910 on (after a toggle) |
| Tabs 2834 | 1076 FUN_18018f050: Full / Partial (record 0x1398) x Vertical / Ramp moves (record 0x139c): 25764 / 25769 / 25758 / 25775 |
| Pocket Rough 3003 / Finish 3004 | 617 FUN_1801843a0 25838 + (record 0x1408 != 0); 2186 25817 (mcmill_all.c l.266454) |
| Slot Rough/Finish 3020, Helix 2823 | 3093 FUN_18018dda0 25784; 3094 FUN_18016e480 25879 |
| Tip Comp 25594 | 1346 HSTDrillTipCompPage::InitPage 0x180165440 25811 |

Record flags the CamParams do not carry (pocket direction, pocket taper, tab type) are read from the record the dialog
was opened from (`pp_rec`: the stored op's, or the defaults op a new op starts from); an op made here and reopened has
none: those pictures then show the decoded default (first branch). The lead line types (0x8f9 / 0x941) and the contour
taper (0x130a) are CamParams since batches 3 / 4 (`lead_in_perp` / `lead_out_perp`, `dc_taper`): their pictures follow the
live controls. The pictures sit at
their template rectangles; this program's row-laid controls are put in a group beside them (or above), and the small
cut-page icons beside the control they belong to.

NOT DECODED (graphics): the focus-dependent picture swaps (OnSetfocus* handlers of the depth, multi-pass, lead, circle
mill pages), the Tip comp icon 1359 (no tip comp control on these pages here), Face 3092, Thread / Circle Mill / HST
cut and lead pictures, Tool Length quick-view row (main tool field), Axis Combination row, the title-bar pin
(UIDialog::EnablePin), tree lines / buttons style, the list header.

### 1.y Template pages (batches 1 + 2, 2026-10-22; 3 + 4, 5, 2026-10-23) - core_ui cui_opdlg.c "template pages", cui_opfields.h

Every page with a decoded template is drawn from it: the template's controls at their positions (dialog units, the
Tool page's way) joined by control id with their record field (formats/opdlg_fields.tsv). Generated table:
`tools/gen_opdlg_fields.py` (run from the repository root, no arguments) -> viewer/core_ui/cui_opfields.inc (1072
controls of 43 templates; each row carries the tsv's decompile citation as a comment). Front ends: mcamui
opparams_ui.inc `page_template`, viewer-qt opdlg.cpp `OpDialog::buildTemplate`; tests `pfset:ID=V` (both apps, the
model) and `opdlgset:pf:ID=V` (Qt widgets); `opctl` prints `template N` and one `tctl` line per control.

| code | behaviour | source |
|---|---|---|
| `pf_dlg(p, slot)` | the template per type: Cut 2938 / 2940 / 2942 / 2944 / 25595 / 25592 (circle, helix) / 2824 / 3205 (Dynamic, Dynamic Contour) / 379 (Area) / 2784 (Peel); Depth 25598 / 3030 / 25602; Lead 3013 / 2793 / 3271 / 2969; Linking 2837 (thread only); Multi Passes 25591 / 2825; Rough 3003 / 25597; Finish 3004 / 3270; Rough/Finish 3020 / 2823; Entry 3005 (+ child 3010 helix / 3011 ramp in static 2990 by 0x12a0 / 0x12a2) / 3219; Arc Filter mccoreres 10318; Tip Comp 25594; Tabs 2834; Tab Cutoff 3022; Break Through 25599; Finish Passes 2833; Trochoidal mccoreres 1343; Transitions 2826; HST Leads 25603; Contour Wall 3299; Finishing 3301; Stock 1337 | formats/opdlg_port_audit.md table 1 + 6.1 (2837 HSTThreadmillLinkPage 0xb15, 379 HSTCoreRoughPage, 25592 HSTPtBasedCutPage, 10318 ArcFilterRefinePage 0x284e, 3299 HSTCtourWallPage) |
| record value | the dialog's edits, else the op's record patches, else the record it was seeded from (`pp_rec`: stored op) or the Mill_MM / Mill_Inch defaults op (`cam_default_op_record`, the record mcam_saveops.c copies); per-opcode offsets (25592 helix / circle, 25598 taper 1 / 3 / 0x86, leads' override feed, 25599, 10318 cut tolerance / 3D arcs) | opdlg_fields.tsv, 6.1 |
| live | (1) bound (`pf_bget` / `pf_bset`): the CamParams values the generators use, ranges of the row-laid controls they replace; (2) a field an engine reads from its record block (`pf_honoured`: cam2d_pock.c pkb / pkd / pks on pk_p, cam2d_ops.c mcb / mcd / mcs on mc_p, per engine) with the template's group / check-box enable rule ("buddy of N", "group N on", "(N) on", "enabled with / by N"); any other rule, and every other field: greyed | ours (which values are honoured) |
| edits | `pf_put`: kept per dialog, poked into pk_p / mc_p at once; at OK `pf_apply` writes them as record patches (non-height, `tp_rp`) - only edited fields, so an untouched op saves byte-identically; an operations sidecar read again reapplies them to the engine blocks and derived fields (cam_fileop.c `cam_params_from_patches`) | ours |
| % pairs | in "[A] % [B]" A is the % of the tool diameter, B the distance (6.1); editing an honoured % side writes B = A x D / 100 (25597 2078 / 2022 / 1719, 3010 2022 / 1719); 2823's feed / rpm pairs (base NOT DECODED) stay greyed | notes F2 / F3 |
| hidden | 25592 circle: 1648 / 1738 / 1437 / 1652 / 1380 / 1384 (InitPage l.270944); 929 / 932 (opcode 0x71 only); Depth taper / cut order for 0x12 / 0x66 / 0x69; 25602 1377 / 1979 / 1980 / 3230, island controls for styles 2 / 3; leads 25158 / 25200 (contour, Dynamic Contour only), overlap 25324 (Peel); 2826 1011 / 2891; 1343 704; 3205 3438 (style 2), 3356 (styles 1 / 3); 3219 3210 / 3211 / 3226 by entry method; 2996 25140 / 1798 (overlap 1702, ours) | the tsv enable_rule column |
| captions set by code | 3205 3327 "Cutting method" / "Compensation direction" (mcmillres 362 / 363), 2942 2028 (2710 / 2711), 2824 1412 (2643 / 2644) | FUN_180167720, FUN_18016ba50, FUN_180192db0 |
| pictures | template statics with an image: 2908 stock (SetStockToLeaveBmp), 1358 contour comp / pocket climb, 1360 face style, and the pp_page_pic pictures at their static | as 1 "Graphics" |
| NOT DECODED | greyed with the marker: status NOT DECODED / PARTIAL rows (comp-type item strings when not offered, drill dwell unit, 10318 tightening value, 2826 plunge unit, 2833 / 3301 comp type); decoded fields whose engine effect is not (`pf_engine_nd`: leads first / last depth only, plunge after first / retract before last move, contour / pocket keep tool down) | opdlg_port_audit.md 6.3, 8 |

Bound per page (live unless noted): Cut 927 walls (as the row-laid page: not Face / chamfer) and 928 floors (all but
Face; the cut bottom moves with it, the depth 0x1a0 stays, 0x888 patched); 2938 comp type (Computer / Off, mccoreres
4589 / 4597 - Control / Wear / Reverse Wear not offered: no G41 / G42 in the post), direction, contour type (2D / 2D
chamfer; Engrave shown), roll 1335 (`CamParams.roll`), 2D chamfer child 2996 (1695 / 1702 + ours tool tip diameter and
preview); 2940 roll (`pk_roll`, 0x5d3); 2942 stepover / overlaps / angle and the style combo (ours mapping, see below);
2944 / 25592 / 2824 comp type + direction (`mc_comp` 0x5d0 / `mc_comp_right` 0x5d2, same two items); 25592 helix tip
comp (`mc_tip` 0x2f8); 25595 cycle (0-2 posted) and 1st peck; 2824 pitch, major diameter (ours: 0 = the circle's), ID / OD
0x1290, hand 0x12a0, direction 0x12a2; 3205 / 2784 / 379 stepover, min radius %, micro lift, back feed, slot width
(Peel), gap / keep tool down radio 0x15cd + distance 0x1600 / % 0x1608 (`link_retract`, LoadDynPockParm's rule);
Depth (batch 3, 2026-10-23): the Depth cuts group 3012 (0x868, `mc_depth_on`; its buddies follow it), max rough step,
finish cuts / step (contour, pocket, slot, circle, HST), keep tool down (slot, circle), contour tapered walls / taper angle
(0x130a / 0x1310 rad shown in degrees, `dc_taper*`) and By contour / By depth (0x1318, `dc_bydepth`); Lead In/Out (batch 4):
master 3229 / Entry 3391 / Exit 3392 (0x8f0 / 0x8f8 / 0x940), per side line % / length, arc % / radius, sweep (B = A x D /
100 both ways), contour also Perpendicular / Tangent, ramp / helix heights, overlap 0x988, midpoint 0x990, gouge check
0x991, adjust start / end of contour (0x1320.. check, Extend / Shorten, % / length); any lead edit patches both sides
(`pf_lead_put`); the sweeps as the record / a patch holds them, 0 = no arc on the contour (`pf_lsw`, CamParams
`lead_sw_rec`; review 2026-10-23, FUN_180051990 bVar22 / bVar24 - was shown and written as 90); first / last depth only, plunge after first / retract before last move, contour / pocket keep tool down:
greyed + NOT DECODED with the reason as tooltip (`pf_engine_nd`); 3271 Lead In/Out on (`mc_lead_on`);
2837 clearance / feed plane / top of thread / thread depth (0x1250 = 1: relative to the point's Z) and "Use clearance
only at the start and end"; 25591 finish number / spacing, rough number / spacing / final-all depths when the op's
multipasses are on (`ct_mp_rec`); 2825 / 3270 multipasses (`mc_mp_*`, thread: radial passes resynced so the engine
takes the record's fields); 2834 number / width / thickness; 3003 Rough, cutting method list (0x123c, 0 = zigzag),
roughing angle, stepover %; 3004 passes / spacing; 3010 plunge angle (0x12c0 rad shown in degrees; `ramp_deg` too); 3020
stepover %, plunge angle; 2823 rough pitch, Finish, finish stepover, finish method (0x12c0 / 0x12c1); 3219 plunge angle;
25594 Tip Comp (`tip_comp`, u8 0x1268). Honoured through the engine blocks (live by `pf_honoured`): pocket 0x1230,
0x1258, 0x12a0 / 0x12a2 (3005), 0x12b8, 0x12c8, 0x12d0, 0x12d1, 0x12f8, 0x1300, 0x1400, 0x1408, 0x1436; slot 0x1248,
0x1250, 0x1261, 0x1262, 0x1290, 0x1298; helix bore, circle mill and thread mill the offsets their engines read
(cui_opdlg.c lists them).

Batch 5 (2026-10-23; formats/opdlg_port_audit.md 9): 2938 compensation type with Control / Wear / Reverse Wear (`ct_comp`
0x5d0; offered only with MCAM_CUTTER_COMP=1 and the 2D type, `pf_cc_ok`; nc_post.c `nc_cutter_comp_on`), Optimize cutter comp
in control 2144 (`ct_ccopt` 0x5d4, Control only), Tip comp 1310 (`ct_tipcomp` 0x1231), internal corner rounding 1210 / external
corner break 1211 (`ct_crnd` / `ct_cbrk` 0x1450 / 0x1468; cam2d.c `ct_offset`); 25598 tapered walls also for 2D chamfer;
25591 Multi Passes group 2945 live (0x8a8; buddies greyed when off; `pf_mp_on`); 2834 Tabs group 1129 live (buddies greyed when
off), Vertical / Ramp 1208 / 1201 (`tab_ramp` S16 0x139c), ramp angle 1213 (`tab_ang` 0x13b0 rad shown in degrees); 25599
Break Through 1220 / 1338 for every opcode set_depths knows (cam_fileop.c `cam_bt_offset` / `cam_bt_eff`, `pf_bt_update`,
OpHFlags.bt, CamParams `bt_eff`). Greyed + NOT DECODED tooltips (`pf_engine_nd`): 1223 look ahead, 25591 1377 keep tool down,
2834 Full / Partial / feed plane / placement controls, 3022 Tab Cutoff. The 2938 "(ours)" marker explains the switch
(off) or the LinuxCNC D semantics (on). Engine / post: Move.cc (mcam_load.h), cam2d.c `pass_cc_tag` / `pl_overtrim` /
`tab_run`, nc_post.c G41 / G42 D / G40 (formats/post.md). openg prints a sixth line (`openg b5`).

Batch 6 (Pocket, formats/opdlg_port_audit.md 10): `pf_pock_rule` (the 3003 per-method enables, FUN_1801837b0 / FUN_180183260 /
FUN_1801836b0), `pf_honoured` POCK list grown by the fields the new engines read (entry, ramp, finish, type, direction, burial), the
% / distance pairs of 3003 (25329 / 1364, 0x1240 written with the %), 3010 (1478 / 1480 -> % ) and 3011 (2024 / 1961, 1720 / 1965),
roughing angle 1366 shown in degrees / stored radians, 3011 XY angle 1976 in degrees, 2793 perpendicular / tangent and overlap for
the pocket, 3004 1814 / 2144 compensation (live only with MCAM_CUTTER_COMP=1; "(ours)" marker). Greyed + NOT DECODED tooltips:
3004 keep tool down / spring passes / feed and spindle overrides, 2793 ramp / helix heights and use entry / exit point, 2940 1423,
3010 / 3011 save skipped boundary, 3003 1470.

Batch 7 (2026-10-23, formats/opdlg_port_audit.md 11): Face Cut 2942 on the record (pf_honoured FACE: 0x1228 / 0x122a / 0x1230 -
0x1290 / 0x1298 / 0x12a0 / 0x12c0 - 0x12c2), Style bound with Mastercam's codes (0 / 4 / 3 / 5, FUN_18016ba50: other codes show
One way), the decoded enables of the "-1" rows (`pf_en_rule`: FUN_18016caf0; 1857 Zigzag only, dis 0x18016cd60), % / distance
pairs both ways (PAIR / RPAIR: 1722 / 1730, 1737 / 1743, 1748 / 1752, 1755 / 1756, 1704 / 1708; the engine reads the %), roughing
angle 1758 in degrees / stored radians, Move between cuts usable with no selection (the defaults' -1), Feed rate between cuts live
only with MCAM_FACE_LINK_FEED=1 ("(ours)" marker otherwise); Depth Cuts 1233 / 25195 / 1377 live for Facing; Arc Filter 10366 is
live for Facing (record 0x1230, the same field as 25458). CamParams' face fields follow the record edits (cam_fileop.c
cam_face_mirror). Slot: 3020 2077 distance bound (-> % FUN_18018e050; shown % x D, FUN_18018de50), 2944 1290 Control / Wear /
Reverse Wear with MCAM_CUTTER_COMP=1; greyed with the reason: 3020 1847 / 1724 (not ported: the ramp is a polygon), 1438 (never
read by bnci_slotmill), Face 1310 / 1335 (not read by bnci_face).

Batch 8 (2026-10-23, formats/opdlg_port_audit.md 12): Drill 25595 labels / enables / cycle names from the post text (nc_post.c
`nc_drill_text`: DrillCyclePageBase::EnableControls l.444402 -> CPostText::SetDlgControlTextFromIndex, page 0x27 entry cycle x 12 +
k; the Generic Fanuc 3X Mill.pst text stands in for the part's own control text), the cycle combo lists the posted cycles (0-2,
0-4 with MCAM_DRILL_CYCLES=1) plus a stored other one, Dwell 1089 bound (record 0x1250, live only with the switch); Tip Comp 25594
1338 breakthrough bound (0x1260, buddy of 3019), 1339 tip length shown (tool_tip_length); Helix Bore 2823 feed pairs 2157 / 2158
and 2159 / 2160 (FUN_18016ec60 / FUN_18016ebf0 / FUN_18016eab0 / FUN_18016ea40; shown as MapToDlg FUN_18016e630, `pf_apply`
re-derives 0x1270 / 0x12a8 = % x F as MapToOp FUN_18016e840), enables 1725 / 1740 / 1749 (FUN_18016e240); Thread 2824 1674 and
2969 1714 / 1670 / 1711 enables (`pf_en_rule`: FUN_180191ee0 / FUN_180192fe0); Thread Linking 2837 Absolute / Incremental radio
bound (0x1250; shown values kept, mcam_saveops.c takes the radio's patch); Circle / Helix 25592 1735 Circle diameter live when a
picked point is not an arc.

Batch 9 (2026-10-23, formats/opdlg_port_audit.md 13): 2D High Speed. Engine glue: cam2d_dyn.c `hst_record` (the stored record or the
defaults op + the dialog's record patches >= 0x1228 and 0x5d2) feeds LoadDynPockParm (`mcce_parm_from_record`); cam2d.c re-derives the
Area Mill macro values from it (cam_fileop.c `cam_area_macro_params`) when the op has such patches. Model (cui_opdlg.c): `pf_honoured`
DYN (Dynamic 3205 / 3219 fields), AREA (379 / 2826 fields Area Clearance takes), DCT (Dynamic Contour, only with MCCE_DCTOUR=1);
`pf_en_rule` 3205 (3438 iff Zigzag FUN_180168b70, 3356 styles 0 / 2 FUN_180168c30), 3219 (FUN_180169600), 3299 (always);
`pf_custom` / `pf_custom_set` 3328 (FUN_180167720 / FUN_180167a40); bound 3219 3206 (items Helix only / Profile), 3216 (radio 0),
379 2216 / 2206 / 2207 (FUN_1801892a0 / FUN_180189120 / FUN_1801891d0 with vtable 0x18033b530 +0x420..+0x430); `pf_engine_nd`
tooltips for 3219 3226 / 3219 / 3208 / 3209 (NOT DECODED), 986 / 3212 / 3213 ("N:" = greyed for the F / S rule, no marker), 379
2346-2352 / 2211 / 313 and 2826 25173 / 25164 / 1004 / 1005 / 1008 (McHmm linker / smoothing / helix NOT DECODED). Removed ours: the
Dynamic Contour "Wall stock width" / "Outside / Inside the chain", Area "Min toolpath radius / Micro lift / Back feedrate / Plunge
angle", Peel "Plunge angle / Gap size", Dynamic entry "Helix radius (0 = auto)" and the Peel slot width "(ours)" marker - no generator
read them.

Batch 10 (2026-10-23, formats/opdlg_port_audit.md 14): Arc Filter / Tolerance 10318. Engine: viewer/cam_filter.c (included by cam2d.c;
`cam_arc_filter` at the end of cam_generate_op: the op's record (stored, else the Mill_MM / Mill_Inch defaults op) with the page's
patches in 0x320..0x347 -> FFilt; applied when 0x33a is on and record 0x11e8 == 0 (BNCISection::PostProcess FUN_180103fb0), not for
drill ops (bnci_filter_op), and for a stored op only after such a patch or with MCAM_ARC_FILTER=1); CamOp.filtered makes nc_post.c /
cam2d.c skip their own arc refit; Move.apl / Move.cz carry XZ / YZ arcs (MCAM_ARC_FILTER_PLANES=1, posted with G18 / G19).
cam2d_dyn.c `hst_patch_off` now includes 0x320..0x347 (LoadDynPockParm adds 0x320 to the tolerance when 0x33a is on). Model
(cui_opdlg.c): `pf_honoured` 0x33a / 0x320 / 0x33b / 0x33e / 0x328 / 0x330 / 0x33f / 0x340 (+ 0x33c / 0x33d with the switch) for every
non-drill type; `pf_filter_group` = the 10385 group click (OnBnClickedStaticLineArcFilter 0x1809a9b10 + the UpdateLocalOp rescale);
10391 live only with a Tighten radio (UpdateEnableStareForTighteningGroup); 10368 / 10382 show 0 while their group is off (InitPage);
`pf_engine_nd` 10374 / 10375 ("N:" without the switch) and the smoothing group 10386 / 10382 / 10397 / 10401 / 10398-10400 (NOT
DECODED: bnci_point_distribution). opdlg_fields.tsv: 10388-10391 DECODED (the consumers FUN_180940b90 / FUN_18093fd40). Testing (ours):
cam_cli `nci-filter` (nodes in / out), `nci-filterpost`, `fileop ... --post`.

Batch 11 (2026-10-24, formats/opdlg_port_audit.md 17): the post-side pages as template pages - Coolant mccoreres 6546, Canned Text 6548,
Home / Ref. Points 6554, Misc Values 6555 (`pf_dlg` PG_COOL / PG_CANNED / PG_HOME / PG_MISC; opdlg_fields.tsv rows c6546-c6555; the
row-laid "Flood Off / On" page is unused). Values: viewer/cam_postvals.c (coolant state decode / encode = CoolantPageBase SetDialogData /
UpdateCannedText + MapCoolantToCannedText, canned split = SplitCannedSelect, the post text stand-in for [misc integers] / [canned text],
`pv_nci_rewrite`); the machine's coolant definition = McamFile.mach_cool_* (mcam_load.c, CCNCMachine coolant block record +0x228),
a new part's = the defaults library's machine (`PV_MACH` / `pv_machine` -> cui_toolpage.c tl6410_mach). Model (cui_opdlg.c): `pf_custom`
/ `pf_custom_set` / `pf_custom_items` for 6546 (`cool_get` / `cool_put` / `cool_oldw`), the 6548 lists and the 6554 flag bits;
`pf_honoured` 0x7e8 / 0x7ec.. / 0x818..; `pf_en_rule` 6555 (EnableStuff); `misc_post_values` on 2252 (HideDisableButtons -> OnPostValsPb);
`pf_hidden` 6555 3449-3486 (HideCheckBoxes); `pf_engine_nd` 6554 (NOT DECODED: rapid_approach, FUN_1800a0d70) and 6548 (display only);
Quick View "Coolant" = `pv_qv_coolant` (IsCoolantEnabled). OK (cui_ops.c `pp_ok_postonly` / `pp_ok_apply_postvals`): a stored op whose
dialog changed only post fields keeps Mastercam's toolpath, its record and NCI section-start records are rewritten (F.ops[i].postv;
project line `POSTVALS id groups hex` in cui_doc.c; mcam_saveops.c copies the fields into the saved record). Post: nc_post.c
`op_coolant` / `cool_pos` / `cool_off_all` with MCAM_COOLANT=1 (formats/post.md "Coolant").

Batch 12 (2026-10-24, formats/opdlg_port_audit.md 18; decompiles decomp/b12/): the frame and Tool page extras. Model (cui_opdlg.c):
Apply 12321 live - `pp_apply_enabled` (any tree item dirty = OnPageDataChanged's EnableWindow(0x3021, 1)) / `pp_apply` (OnApplyPb
0x180892b20: SetAllPagesDataChanged(0) + InitializeTreeStates, the dialog stays open); the toolbar `pp_tb_live` / `pp_tb_click`: Hide
dialog 10320 = `pp_hide` / `pp_hide_end` (OnHideDialogPb 0x180892f20: "Press <ESC> when done", the front end hides the dialog as for a
Linking pick, Esc ends it), Reload parameters from defaults file 9431 = cui_ops.c `pp_reload_defaults` (OnReadFromDefaultsPb 0x180894220 ->
ReplaceOperation 0x1808951f0: the op re-seeded by `pp_seed_params` - open_params' seeding, factored out - keeping chains, tool, the
stored op it replaces); Save to defaults 9433 / Feed speed calculator 9435 / Preview toolpath 10501 stay greyed (reasons in the code).
Q19 / Q20 (2026-10-24, branch `defs`, formats/opdlg_port_audit.md 22): Save parameters to defaults file 9433 = cui_defaults.c
`pp_save_defaults` (WriteOperationToDefaultsFile 0x180896fc0's rewrite branch into the program's copy, paths.h `defaults_read` /
`defaults_work`; ours `dfl_reset_ask` / `dfl_restore_ask` / `dfl_answer`, Qt "Defaults file (ours)" button `OpDialog::defaultsMenu`);
Preview toolpath 10501 = cui_preview.c `pv_begin` / `pv_end` / `pp_preview` (Qt `OpDialog::preview` on the worker thread `runJob`),
OK via cui_ops.c `pp_ok_quick`'s preview branch (`pv_commit` / `pv_commit_dirty`), Cancel `pv_cancel` (from `pp_cancel`), drawing
`pv_overlay` + scene.c `scene_tp_hide_id`.
Tool page (cui_toolpage.c): Filter Active 25263 / Filter... 3056 live - `tp_factive` (DAT_1814317d1) / `tp_rows` with the Tool Manager's
Tool List Filter settings (cui_toolmgr.c `tm_filter_test`; `tmf_ok` turns Filter Active on after the page's Filter...); the tool fields,
RCTF, Recalculate values stay read-only / greyed with the decoded chains in the comments. Post: nc_post.c `op_force_tc` with
MCAM_FORCE_TOOL_CHANGE=1 (formats/post.md "Force tool change"). Qt: opdlg.cpp (toolbar / Apply enable + clicks, `fillToolRows`, test
keys `opdlgset:apply` / `opdlgset:tb=ID`), mainwindow.cpp (Hide = suspended like lk_pick; the Tool List Filter dialog also while the op
dialog is up), viewwidget.cpp (the prompt while hidden). Script steps (ours): `opapply`, `optb:ID`, `ophideend`, `opframe`.

Decisions batch dec1 (2026-10-23, formats/opdlg_port_audit.md "Decisions batch dec1", formats/user_decisions.md): Pocket - zigzag through
the decoded engine by default (cam2d.c gen_pocket; PK_ZIGZAG_DECODED=0 = the old correlated engine, testing), Parallel Spiral with
Minimize tool burial = FUN_1801e9ee0's fallback link + CamOp.warning (cam2d_pock.c; shown by core_ui cui_ops.c gen_end / camop_regen
and cam_cli "WARNING:"), 0x1400 / 0x126b / 0x1308 ungated (cam2d_pock_fin.c, cam2d_pock_entry.c); Face - every op with its record
through cam2d_face.c (cam2d_ops.c face_decoded; FACE_DECODED=0 = the measured engine, testing); Slot - 3020 1847 / 1724 live
(cui_opdlg.c SLOT[] 0x1280 / 0x1288, the NOT PORTED tooltip removed) on the new viewer/cam2d_slot_arc.c (create_ramped_ctour
FUN_1802178c0 + FUN_1802189e0 on the exact loop; SLOT_RAMP_POLY=1 = the old polygon ramp, testing); post - a drill dwell without
MCAM_DRILL_CYCLES=1 refused (nc_post.c).

Decisions batch dec2 (2026-10-24, formats/opdlg_port_audit.md 20, formats/user_decisions.md 2026-10-24): Dynamic - the first record
starts at its own Z (mcce_dyn.c mcce_calc_dyn_pock output loop; OutputMotionToBnci); Peel / Dynamic Contour - the "Unverified (ours)"
static on pages 2784 / 3205 (cui_opdlg.c pf_marks, tooltip) and CamOp.warning (cam2d_dyn.c hst_unverified; status line, cam_cli
"WARNING:"); Peel side curves (cam2d_dyn.c peel_side: sliver points dropped; cam2d.c offset_open_trim_x first_arrival); Arc filter -
the stored 0x33a honoured without an edit (cam_filter.c ff_params); post - stored XZ arcs read in view 2 and G18 / G19 for stored
arcs with MCAM_ARC_FILTER_PLANES=1, else refused (nc_post.c), tools/mcam_ops.py (load, move_points); cutter comp - Wear / Reverse
wear refused, D<tool number> (nc_post.c op_cc_type; cui_opdlg.c "(ours)" texts on 2938 / 2944 / 3004).

Ours on template pages (marked `ours` in opctl): 2D chamfer tool tip diameter + preview; Facing stock box; drill "(not
posted)"; circle / thread picked circles; thread depth (0 = 0.6134 x pitch), full profile; Dynamic Mill machining
region strategy (op +0x1567, dialog 657); plunge angle (Dynamic Contour; batch 9 removed the Area / Peel ones and the other
stand-ins listed under "Batch 9" above); Arc Filter "Create arcs in XY" (`CamParams.arcs`, the old
mcmillres 318 label; batch 10: not applied to an op the Line/Arc filter ran on). Removed: the Circle Mill "Helix pitch", "Stepover % of D", "Start circle radius" and "Finish
passes" stand-ins (gen_circlemill reads none of them: the record fields of 25592 / 25597 / 3270 are live instead), the
Thread Mill "Radial passes" (2825's fields replace it, 6.2 item 8).

"(ours)" markers (review 2026-10-22): a static "(ours)" beside a control filled differently from Mastercam, its tooltip the
explanation, drawn the same in both apps (opctl: `tctl static 91xxx ... "(ours)"`): contour roll 1335 (0x5d4), the
compensation-type combos (Computer / Off), Face Feed rate between cuts 1846 (MCAM_FACE_LINK_FEED; the style 1855 marker is gone with
batch 7), the contour lead
page's Gouge check 25200 (this program's measured lead fit stands in for FUN_1801c46f0; the Exit side's marker of batches 1 +
2 is gone - each side is its own since batch 4), Thread Linking clearance 1650 (follows the Linking flags). Edit boxes commit only when their
text differs from the text shown (both apps); unchanged values never make a patch.

Not reconciled (reported, the posted program would change): the Face style codes - since batch 7 the combo and the decoded
engine (cam2d_face.c) use Mastercam's 0 / 4 / 3 / 5; since decisions batch dec1 stored Face ops use it too (the measured
engine with this program's old codes only with FACE_DECODED=0); the contour roll offset (resolved 2026-10-22, the user's decision: record u8 0x5d3 is
read / written, FUN_180157e30; new Mill_MM / Inch contours roll Sharp corners). (The Peel slot
width note is gone with batch 9: no Peel engine reads it.)

### 1.x Planes page (mccoreres dialog 6547, PlanesPageBase) and its dialogs - core_ui cui_planes.c (2026-10-22)
Decompiles: decomp/mccore_planespage.c (PlanesPageBase), decomp/mccore_updateview_wofs.c (InitPage, GetPlaneName,
CUpdateViewDialog), decomp/mastercam_plane_convert.c (Plane::Init / Convert / InitOperationView, SelectPlaneFromList,
FUN_18043c260), decomp/mastercam_planeselect_dlg.c (PlaneSelectListDlg ctor), decomp/mccore_opview.c (wpt_to_op).
Drawn by opparams_ui.inc (`pp_page_body` -> `planes_page_body`, `draw_planes_dialogs`) and viewer-qt opdlg.cpp
(QT_PP_OPS `qp_button` / `qp_radio`, `PlanesDialog`); PpOps gained `button` (a push button, 1 = clicked; greyed = not
ported) and `radio` (a live radio, 1 = clicked into the selected state).

| control (6547) | behaviour | source |
|---|---|---|
| groups "Working coordinate system" / "Tool plane" / "Comp / construction plane" | the op's three op_views (op +0x6d0 / +0x570 / +0x620) | template |
| 3143 / 3147 / 969 plane name | GetPlaneName of the op_view's plane id (system planes: cui_sys_plane; not found: mccoreres 4974 "UNDEFINED") | RefreshData 0x1805c69e0, GetPlaneName 0x1805c4d90 |
| Origin X / Y / Z (3142 426 3144, 3148 433 439, 444 3151 3152) | world_to_view(origin, view number) (PlanesWorldCoords off); shown as labels, editing NOT PORTED (UpdateOrigin creates planes) | RefreshData |
| 3145 / 3149 / 3153 Select Plane... | the Plane Selection dialog; OK = Plane::Convert into that op_view; Tplane: the work offset becomes the plane's (-1, Automatic) | OnWCSSelViewPb / OnTPlaneSelViewPb / OnCPlaneSelViewPb -> OnSelViewPb 0x1805c6010 |
| 3146 / 3150 / 3154 Select Point | greyed (NOT PORTED) | SelectNewOrigin |
| (every Select Plane / copy) | Plane::Convert into the op_view (+0x92 = the plane's -1), ResyncOpViews (op_views of the same plane id become copies of it; the Tplane's +0x92 restored when the page was Manual >= 0), SetToolPlane(Tplane) = Manual is the Tplane's +0x92 >= 0; each op_view keeps its own +0x92 (CamParams wofs / wwofs / cwofs) | OnSelViewPb 0x1805c6010, ResyncOpViews 0x1805c73b0, SetToolPlane 0x1805c79e0 |
| 3289 -> / 3290 <- (between WCS and Tool plane) | WCS -> Tplane (OnSelViewPb + SetToolPlane(WCS op_view): its +0x92 - -1 after the resync - becomes the work offset, so Manual returns to Automatic); Tplane -> WCS | OnXferWCSToTPlanePb 0x1805c6700, OnXferTPlaneToWCSPb 0x1805c6620 |
| 3291 -> / 3292 <- (between Tool plane and Comp) | Tplane -> Cplane; Cplane -> Tplane (+ SetToolPlane) | OnXferTPlaneToCPlanePb 0x1805c6540, OnXferCPlaneToTPlanePb 0x1805c64d0 |
| Work Offset: 3155 Manual / 3166 Automatic, edit 3157 | Automatic = -1; Manual = the Tplane plane's (+0x330, -1) else the defaults library op's (record 0x66a) when not -1; edit "%d" while Manual; a typed value < -1 -> -1, > 0x7fff -> -1, Manual = value >= 0; a value other than the plane's opens Update Plane | OnWorkOffsetManual 0x1805c6480, EnableDisableWorkOffset 0x1805c47f0, OnKillfocusWorkOffsetEdit 0x1805c5f00, CheckWorkOffset 0x1805c4240 |
| 3158 "*Displayed relative to WCS" | left out: shown only when PlanesFollowRules::GetDisplayStatusRelToWcs (NOT DECODED) | InitPage 0x1805c5480 |
| OK of the op dialog | MapToOp: Tplane +0x92 = the value when Manual and >= 0, else -1; WCS / Cplane +0x92 = the same when their plane id is the Tplane's (`planes_map_to_op` in generate_pending) | UpdateLocalOp 0x1805c7fd0, MapToOp 0x1805c5ab0 |
| Quick view 7545 "Cplane / Tplane" | the Cplane's name: GetCustomizableData puts the Tplane's, RefreshData (InitPage) replaces it with the Cplane's (this +0xea20) | GetCustomizableData 0x1805c4b90, RefreshData 0x1805c69e0 |

**Plane Selection** (dialog 842 "Plane Selection", 242 x 160 du, `PLS842`; mastercam.dll SelectPlaneFromList ->
FUN_18043c260 -> PlaneSelectListDlg FUN_18043d070): list 1457 (PlanesListCtrl, persistence key "\GUI\Plane Selection") of
the part's planes = the seven system planes here, the current plane preselected (FUN_18043d6b0); OK returns the selected
plane id, Cancel 0 (no change). NOT DECODED / greyed: the list's columns (one column of names), 546 "Select plane icon",
630 "Set new origin", Help; 1467 (MCforSW only) left out.

**Update Plane** (mccoreres dialog 10269 "Update Plane", 230 x 137 du, `UPL10269`; CUpdateViewDialog::DoModalWorkOffset
0x1808bc1c0, MCDialog 0x281d): shown when a typed work offset differs from the Tplane plane's (+0x1898 = Plane +0x330);
static 10280 = mccoreres 375 "Changing your work offset value will update plane %s and any operations that use %s."
(the WorkOffsetInUse addition 5605 is an empty string); radios 10281 / 10282 / 10283 (0x1a00, default 0 =
registry "Update Plane"), 10177 "Don't show this warning again" (registry "Work Offset Warning"). The op keeps the typed
value in every branch (OK with any choice, Cancel). Choices 0 "Update the plane and all operations that use this
plane" / 1 "Create a copy of the plane ..." change the planes archive: greyed, NOT PORTED; 2 is live; the checkbox and
Help greyed (no registry); OK is enabled only with choice 2 (ours: 0 / 1 not ported, 0 stays the decoded default
selection); the WorkOffsetInUse addition (5605 + plane name + 5190 "Work offset already used.") is appended only when
5605 is non-empty, and 5605 is empty in this mccoreres.dll - nothing is appended (no registry: the user's hive <windows>/Users/<user>/NTUSER.DAT has no
"Configurations\Variables\Toolpaths" key, so Mastercam there uses the defaults: choice 0, warning shown). Script steps (both apps): plsel:W (0 WCS, 1 Tplane, 2 Cplane), plrow:I, plok, plcancel,
plcopy:ID, plwofs:manual|auto|N, uplchoice:K, uplok, uplcancel, pldump; Qt widget steps pldlg / pldlgset:row=I|choice=K|ok|cancel
and opdlgset:LABEL@N=V (the N-th control of that label).

## 2. Chaining dialogs (viewer/chaindlg_ui.inc)

**Chaining, mccore CChainDlg** (decomp/mccore_chain_range.c, message map @0x180d286d8; `CH_6464[]` / `CH_1058[]`,
`chain_dlg_body`, `chain_act`, `chain_ctl_live`, `chain_ctl_on`, `chain_dlg_title_icon`): FUN_1801f06d0 builds it from
mccoreres **6464** (143 x 321 du, collapsed) or **1058** (143 x 428 du, expanded: + Start / End groups, Back / Forward,
Dynamic) by +0x1f2b5, which starts from DAT_1813c4bb0 (bss: collapsed at the first chaining; saved on close by
FUN_1801ec0c0). The toggle is UIVertDialog's title-bar icon (uictrls, decomp/uictrls_uivertdialog.c): icons 7021
(collapsed) / 7022 (expanded) (`uictrls_ICO_*.png`, tools/extract_icons.py), a click = SC_MOUSEMENU -> OnSysCommand ->
WM_COMMAND 358 (FUN_1801ece50), tooltip opmanres 7020 "Expand dialog" / 7021 "Contract dialog" (mastercam.dll
SetUICtrlStrings 0x18028fe08). Controls at template positions, captions / tooltips from the template; button images =
CChainDlg CBitmap members (DDX FUN_1801ec100): Wireframe 537 -> bitmap 1133, Solids 351 -> 1142, Chain 527 -> 1134,
Point 549 -> 1143, Window 25496 -> 1135, Area 531 -> 1144, Single 529 -> 1136, Polygon 534 -> 1145, Vector 25481 -> 1137,
Partial 535 -> 1146, Chain Feature 9990 -> 10095, Chain Feature Options 9991 -> 10098, Unselect All 10204 -> 10205, End
chain 12321 -> 7004, Start / End Back 542 / 543 -> 1139, Forward 561 / 545 -> 1147, Dynamic 556 -> 1140, Unselect 541 ->
1216, Reverse 25381 -> 1123, Last 525 -> 1138; Wait 540 a check box (+0x17e8), C-plane 1482 / 3D 1499 a radio group
(+0x17e0); Options 550, OK, Help show text.

| control | handler | behaviour |
|---|---|---|
| mode buttons 527 / 529 / 535 / 25496 / 531 / 534 / 25481 / 549 | FUN_1801ec690 (1), FUN_1801edd90 (2), FUN_1801edb60 (7), FUN_1801ee260 (5), FUN_1801ec590 (4), FUN_1801edc00 (6), FUN_1801ee1e0 (8), FUN_1801edbb0 (9) | Chain / Single / Partial / Window / Area / Polygon / Vector / Point (formats/chaining.md) |
| 1482 C-plane / 1499 3D | FUN_1801ec7e0 / FUN_1801ec560 | chain_plane_filter 1 / 0; default config 6021 |
| 358 (title icon) | FUN_1801ece50 | expand / contract (template 1058 / 6464) |
| 538 combo | FUN_1801edd10 | Window / Polygon selection mode (mccoreres 4224-4228) |
| 540 Wait | FUN_1801ee230 | chain_wait_at_end; default 6023 |
| 525 Last | FUN_1801ed6b0 | re-adds the last chains; Last already used with chains selected: warning 4211, nothing added |
| 12321 End chain | FUN_1801ecb70 | ends a partial / branch chain |
| 541 Unselect | FUN_1801ee030 | the last chain, or all chains of the last Window / Polygon selection (+0x1f0ac / +0x1f0aa) |
| 10204 Unselect All | FUN_1801ee0f0 -> msg 0xb | Yes / No box MCMessageBox 5007 / 10206 |
| 25381 Reverse | FUN_1801edc50 | reverse_chain of the last chain |
| 542 / 561 / 543 / 545 | FUN_1801ede80 / FUN_1801edf60 / FUN_1801ecd00 / FUN_1801ecda0 | start / end back / forward; enabled as FUN_1801f00d0 (Start: closed, partial or branch; End Back: branch or partial; End Forward: partial) |
| 556 Dynamic | FUN_1801ec820 | dynamic start / end; enabled with a last chain |
| 550 Options | FUN_1801eda80 -> set_chain_options | Chaining Options 405 |
| 1 OK | FUN_1801ed940 | chains done: nested sorting / Reverse inner chains (run_chain_dlg end), mode remembered, -> op parameters (or Extrude `ex_chains_done`); Facing may continue without chains |

Solids 351 (FUN_1801edde0 -> template 6518, 143 x 286 du; live as FUN_1801f00d0: solid chaining allowed and
sld_get_num_solids_visible() >= 1; buttons 2953 Edge, 2954 Face, 2955 Loop, 2956 Linked edges, 2957 Partial loop, 2959
From back, 3041 Face / 1499 3D radio -> mcgeomsld OnSolidsSelect* / SetSolidChainingUseFace) and the Pick Reference Face
dialog (mcgeomsldres 134): ported 2026-10-22 (core_ui cui_sldchain.c CH_6518 / SREF_134; rules in formats/chaining.md
"Solid chaining"; mcamui `sld_ref_draw`, Qt `RefFaceDialog`). Greyed / NOT PORTED: Chain Feature 9990 / Chain Feature Options 9991 (FUN_1801ec6d0 / FUN_1801ec710: CFChain
search and ChainFeatureDlg 9436, decomp/mccore_chain_feat_opts.c, not ported), Help 9. The second DDX branch of
+0x1f2b5 (earlier read as a lathe image set) is the expanded template's Start / End / Dynamic members. NOT DECODED: the
pressed / hot look of the CMFCButtons. Point chains are not
drawn in the selection colour (the overlay draws entity polylines only; points have one vertex).

**Chaining Options, mccoreres dialog 405** (143 x 403 du; mccore CChainOptions: ctor FUN_1801f8a60, init FUN_1801f9a20,
DDX FUN_1801f8ef0, OK FUN_1801f9680, enables FUN_1801f9940 / FUN_1801f9870; `chain_opts_draw`), session only (the config
file is not written). Live: Mask Entity 2469 (chain_use_ent_mask), Color 1893, Level 2461, Closed / Open chains 2471 /
2473, Section angle 547 + edit 407 (0..180 deg, enabled by 547), Ignore depths 2466, Start chain at point 2468 and Break
closest entity to thread point 2520 (exclusive, FUN_1801f98f0 / FUN_1801f9230), Allow surface edges in Single mode 2475,
Break entities in Dynamic 702, Closed chains CW 2496 / CCW 2501 radio + Use cursor position 2493 check, Open chains One
way 2508 / Zigzag 2510, Sorting combo 2901 (mccoreres 5132-5136), Infinite nesting 2512, Reverse inner chains 2536,
Sync mode combo 2447 (mccoreres 4200-4206), Chaining tolerance 2350 and Plane tolerance 2359 (MCReal, minimum = system
tolerance). With an effect here: masks, Closed / Open, Ignore depths, Start chain at point, directions, Zigzag,
Sorting (not the optimise passes), Infinite nesting, Reverse inner chains, both tolerances. Stored without an effect:
Entity mask (no mask dialog), Section angle (surface edges only), Allow surface edges, Break closest entity (lathe),
Break entities in Dynamic (NOT PORTED), Sync mode. Greyed: the Entity / Color / Level buttons (CEntMaskDlg 6466 and the
colour / level mask dialogs, not ported), Help.

**Drill Point Selection, mcmillres dialog 2300** (152 x 434 du; `drill_sel_body`): the CDrillPtSelection dialog that
mcmill tp_pt_mgr (decomp/mcmill_all.c @18007a6c0) runs while picking points for Drill / Helix Bore / Circle Mill /
Thread Mill (FUN_18008b0d0: get_event mask 0x200 until OK; picked points drawn in palette colour 0xe,
FUN_18006c980(op, 0xe)); DDX FUN_18008b700. Live: manual picks (2954, default: each pick adds its point, an arc gives
its centre), Unselect 3244 (last point), Unselect all 3245, OK (`open_params`). Greyed: Automatic, Entities, Window
Points, Mask on Arc (+ Diameter / Tolerance), Subprograms..., Last, Sorting..., Edit..., Pattern / Create points, Grid,
Bolt Circle and their edits, Help. NOT DECODED: the manual button's image (2954 has no caption), the point marker
shape, tp_pt_mgr's sort order (pick order kept). The tree dialog 2074 "Drill Point Manager" belongs to the Sorting /
Edit path and is not shown.

## 3. Toolpaths manager texts (viewer/optree_text.inc)

Sources: decomp/opman_tree_all.c, decomp/mccore_descriptions.c, decomp/mccore_toolfilt.c. Op record offset = op struct
offset + 0x68.

| function | text | source |
|---|---|---|
| `otx_op_row` | "N - <description> - [WCS: <plane>] - [Tplane: <plane>]" + " - <comment>" when not empty; WCS = record 0x7a0, Tplane 0x640 | COpMgrTree::InitTvOpText @1800c3350 with OpMgrDisplay defaults FUN_180343d50 (all flags 1 except op ID) |
| `otx_desc` | op_make_description: type name (formats/opcodes.tsv); contour " (2D / 3D / 2D chamfer / 3D chamfer / Ramp / Remachining)" from 0x1308; pocket " (Standard / Facing / Island facing / Remachining / Open)" from 0x13fa; drill = the cycle text | mccore @1800c7720, FUN_1800b0010 (4561..4571), FUN_1800cb1c0 (4792..4800), FUN_1800b0530 |
| `drill_cycle_text` | cycles 0-7 = the op dialog's Cycle names (mccoreres 7645 + 11 n - an inference: the post text page 0x27 is not read); 8-19 "Custom drill cycle #n" (4885-4896); 30 "5 axis"; else "UNDEFINED" | FUN_1800b0530 fallback 4877 + cycle / 5218 / 4974 |
| `otx_params_row` | "Parameters", + " - Work offset #n" when record 0x66a != -1 | InitTvItem -3, opmanres 3043 / 3044 |
| `otx_tool_row` | tl_make_description upper-cased (`tm_describe`); "Tool does not exist!" (opmanres 158) | InitTvItem -4, mccore @1800db8a0 |
| `otx_geom_row` | "Geometry -  (N) chain(s)" (0xf4) for chained non-msurf ops, "Geometry -  (N) point(s)" (0x13c) for point ops | InitTvItem -5, opmanres 3047 / 3045 / 3046; op_is_chained @1800c5ca0, op_is_point, op_is_msurf @1800c62b0 |
| `otx_path_row` | "Toolpath - %.1fK - Program # N" (NCI size (0xec - 0xe8) / 1024, program 0x160); new ops 0.0K | InitTvItem -6, opmanres 27625 / 161 |
| `otx_line` | Doc file line: 4 spaces per depth, trailing blanks trimmed | DocFile @180378c70 -> DumpItemArray @1800be160 -> CStringArrayToFile @180440760 |

Doc file (mcamui.c): "<part name> Operations Manager.txt" in $TMPDIR (else /tmp), opened with xdg-open outside scripted
runs (Mastercam: its editor). NOT DECODED: plane names other than the seven system planes ("?"), the NC file name +
NCSuffix part of the Toolpath row (control definition text 0x15: left out), facing_op_uses_stock (facing rows show
chains), the first (op class) test of op_is_chained. Ours: an op type that cannot be exported has no opcode and shows
this program's type label.

## 4. Solids manager (viewer/solidsmgr_ui.inc)

Sources: mastercam.dll history tree, decomp/mastercam_histdlg.c, decomp/mastercam_solidsmgr.c.

| code | behaviour | source |
|---|---|---|
| `draw_solids_tree` | solid rows: the solid's description (SOLID_ID +0xb8 = the name after the XT), else "Solid" (opmanres 27600), image 0 of bitmap 749; the part's Parasolid solids with their stored history (2026-10-24, core_ui `sm_psolid_text` / `sm_pop_*`: mc_add_solid_to_dialog - each op's stored name, else the type name of opmanres 27601 + type / 27657 / 27669, image of the 0x18087e520 table, not-editable ops shown with their selected image; then "Stop Op" (27640, opmanres bitmap 759) after the history position; the solid row expanded by ops header +0x0e; Toolpath op names, dirty / invalid overlays and tool solids NOT PORTED) then the bodies made here, each with its features below: text = feature name, else the type name (`SLD_OPNAME`: 27601 Extrude Cut, 27602 Extrude Boss, 27603 Extrude), image type * 4 + 8 (selected + 10) from the per-type strip (`SLD_OPBMP`: 730 / 736 / 733) | HistDlg_addSolid -> FUN_180232b00; HistDlg_addOp -> FUN_1802326e0; image list FUN_18022dbd0 (bitmap 749 + 754 + table 0x18087e520) |
| `draw_solids_menu` | right-click menu: submenus "Solids" (opmanres 1211, 30 items) and "Model Prep" (1190, 20 items); op rows: Delete / Suppress / Highlight / Rename; solid rows: Delete / Rename / Duplicate Solid / Attributes / Check solid... / Remove solid history; then Regen Solid, Regen All Solids, Expand all, Collapse all, Auto-Highlight, (op) Edit Parameters, Recreate trimmed bodies | FUN_180230a10 (strings 1173-1228, commands 33495-33569; item commands 27641-27670, 3255 / 3256 / 3810) |

Working items: Solids > Extrude..., Delete on the last feature (`feat_delete_last`, one undo step), Regen All Solids
(`solids_rebuild`), Expand all / Collapse all; the rest greyed. Which item group goes with a solid row and which with
an op row is inferred from the item kinds (1 solid, 2 op). Removed (were invented): "(made here, volume ...)", the
"Delete last feature" button, the solid-mesh diagnostics line, "Stock (solid box)" / "Show stock box" (the stock box
was the solids' bounding box; Mastercam draws the machine group's Stock Setup - decoded and drawn since 2026-10-22 by
core_ui cui_stock.c / cui_overlay.c, formats/mcam.md "Stock Setup"). NOT DECODED: solid descriptions and history ops of the part's
solids, op flags / dirty images, Suppress. Note: Facing without chains still uses the solids' box (`open_params`,
`stock_lo` / `stock_hi` from scene.c) - Mastercam uses the defined stock.

## 5. Manager tabs: Levels, Planes, Recent Functions

Tabs (mcamui.c `draw_manager`): Toolpaths, Solids, Planes, Levels, Recent Functions.

**Levels** (levels_ui.inc `draw_levels_tab`; opmanres dialog 459, decomp/mastercam_levels.c): list 1390 with columns
FUN_1802491d0 opmanres 1979 Number, 3273 Visible, 1948 Name, 3275 Entities, 3274 Level Set; default widths
FUN_180247e90 with u = list width / 40: 7u, 6u, width - 32u, 8u, 7u; rows = levels that have entities, with the entity
count (entities, not polylines); the form under the list (Number 1359, Name 1313, Level set 1315, Display Used / Named
/ Used or named / Range 1363-1366) greyed. NOT DECODED / not here: row fill of the default Display, level names and
sets (the part's level records are not read), visibility (all drawn) and which image (26045 'ON' tile / 26044 check)
a row shows, main-level marker, editing; column chooser FUN_180253ec0 and registry "ColumnVisible%d" (FUN_180249630)
and sorting NOT PORTED.

**Planes** (levels_ui.inc `draw_planes_tab`; opmanres dialog 457, decomp/mastercam_planes.c): columns FUN_18040b1f0
opmanres 1948 Name, 59399 G, 3058 WCS, 126 C, 127 T, 1949 Display, 1947 Offset, 1998 Coordinate. Rows: the seven
system planes 1 Top, 2 Front, 3 Back, 4 Bottom, 5 Right side, 6 Left side, 7 Iso (`SYS_PLANES`, the names seen in the
user parts' MastercamPlanes "SystemPlanes" archive; the archive itself is not parsed here, so these seven are always
listed). The Toolpaths rows take WCS / Tplane names from this list. NOT DECODED: column widths (drawn Name 1/3, the
rest shared), the row fill (G / WCS / C / T marks, Display, Offset, Coordinate), selection, the fields under the list.
A click no longer sets the view (that was invented).

**Recent Functions** (mcamui.c `add_recent` and the manager tab): MCCore.UI RecentFunctionsManagerView
(recentfunctionsmanagerview.baml): flat buttons (padding 8), the command's small icon (right margin 7) + its tooltip
text, a click runs the command; newest first, up to mcamxm.config 4023 "Number of functions in the MRU menu?". Ours:
when 4023 is not in the config the count is 10 (`cfg_mru_n`, no source cited), and the list holds at most 16
(array size).

## 6. NOT DECODED summary (this file)
- Op dialog: HST tree nesting FUN_18014c4a0, drill title post text, Help, the graphics listed in 1 "Graphics",
  Dynamic Contour wall settings; the not-ported pages listed in 1.
- Chaining: CMFCButton pressed / hot look; Solids selection and Chain Feature (+ Options) not ported (section 2); Drill Point Selection manual-button image,
  marker shape, tp_pt_mgr sorts.
- Toolpaths texts: user plane names, NC file name / NCSuffix, facing_op_uses_stock, op_is_chained's first test, post
  cycle texts.
- Solids manager: part solids' description / history, op flags / dirty images, Suppress, stock setup display.
- Levels / Planes: row fill, names, sets, visibility marks, plane column widths, column chooser, sorting, editing.
- Planes page (1.x): planes other than the system planes, Select Point / typed origins, display relative to the WCS,
  PlanesWorldCoords, Plane Selection columns / icon / new origin, Update Plane choices 0 / 1 and its registry checkbox.
