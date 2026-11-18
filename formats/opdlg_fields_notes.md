# opdlg_fields.tsv - decode notes per page group (batch 0, 2026-10-22)

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

Companion of formats/opdlg_fields.tsv (646 rows, one per control of the 58 op-dialog templates' decoded pages) and
formats/opdlg_port_audit.md section 6. Written by four parallel decode passes (F1-F4) plus Arc Filter / Coolant; the
notes are kept as written (functions with addresses and decomp/mcmill_all.c lines, data checks, conflicts, fix lists).
Record offset = op offset + 0x68; page dialog-op copy *(page+0x1560)+X = record X-0x2088 (+0x20f4 = rec 0x6c opcode,
+0x32b0 = rec 0x1228 HST style); MapToOpEnt(ent&) offsets = record offsets. Data checks used the Mill_MM / Mill_Inch
.mcam-defaults (<windows>/.../shared Mcam2017/mill/Ops) and the 17 user parts in <user-parts> (read only).

## F1 notes - drill / contour group (batch 0)

Rows: rows_F1.tsv (104 rows). Offsets = record offsets (ent offsets in MapToOpEnt; dlg copy `*(page+0x1560)+X` -> rec X-0x2088;
IsPageDisabled(operation&) uses op offsets = rec-0x68, which cross-checks every tree on/off flag below).

### Functions used (decomp/mcmill_all.c)
- Drill Link 25593: HSTDrillLinkPage DoDataExchange 0x180162e40 (l.248758), EnableClearance 0x180162f90 (l.248789),
  EnableDepthControls 0x180163030 (l.248815), EnableRetract 0x1801633d0 (l.248947), GetMessageMap 0x180163510 (l.249027),
  MapToDlg 0x180163bb0 (l.249380), MapToOpEnt 0x180163e40 (l.249447), OnCalcPb 0x180163f40 (l.249478), ValidateSubpgmCb 0x180164b80 (l.250141).
- Tip Comp 25594: HSTDrillTipCompPage DoDataExchange 0x180165090 (l.250375), MapToDlg 0x1801655c0 (l.250650), MapToOpEnt 0x180165720 (l.250707),
  OnTipcompCb 0x180165a20 (l.250846); mccore op_is_tip_comp (decomp/mccore_op_is_tip_comp.c).
- Drill Cut 25595: DrillCyclePageBase DoDataExchange 0x18028b410 (l.444377), EnableControls 0x18028b4f0 (l.444402), InitPage 0x18028b9d0 (l.444621),
  MapToDlg 0x18028bb10 (l.444680), MapToOpEnt 0x18028bcf0 (l.444742), OnSelchangeCycleCombo 0x18028c320 (l.445076);
  HSTDrillCyclePage DoDataExchange 0x180162270 (l.248145), InitPage 0x1801623d0 (l.248225), MapToOpEnt 0x180162680 (l.248333),
  OnCustomDrillCb 0x180162700 (l.248362), SetDialogText 0x1801628e0 (l.248473).
- Break Through 25599: HSTBreakThruPage DoDataExchange 0x180150b40 (l.235513), IsPageDisabled 0x180150dd0 (l.235678), MapToDlg 0x180150f00 (l.235715),
  MapToOpEnt 0x1801514b0 (l.235861), OnBreakThruCb 0x180151720 (l.235920).
- Multi Passes 25591: HSTMultiPassPage DoDataExchange 0x180176aa0 (l.261040), IsPageDisabled 0x1801772b0 (l.261387), MapToDlg 0x1801772c0 (l.261400),
  MapToOp 0x180177540 (l.261480), MapToOpEnt 0x1801777b0 (l.261546).
- Tabs 2834 (HSTTabPage, unnamed; region from ctor FUN_18018e470 arg 0xb12): DDX FUN_18018e9a0 (l.277027), MapToDlg FUN_18018f4f0 (l.277376),
  UpdateLocalOp FUN_18018fba0 (l.277510), enable FUN_180190d80 (l.278238).
- Tab Cutoff 3022 (HSTTabSettingsPage, unnamed; ctor FUN_1801910d0 arg 0xbce): DDX FUN_180191260 (l.278460), IsPageDisabled FUN_180191550 (l.278578),
  MapToDlg FUN_180191570 (l.278588), UpdateLocalOp FUN_180191660 (l.278624), enable FUN_1801918c0 (l.278771).
- Contour Cut 2938 (HSTContourCutPage, unnamed; ctor arg 0xb7a): MapToDlg FUN_180157c50 (l.240736), UpdateLocalOp FUN_180157e30 (l.240798),
  contour-type fill FUN_180158500 (l.241126), direction/roll fill FUN_180158790 (l.241238), enable_controls FUN_180158ac0 (l.241375);
  base HST2DCutParametersPageBase InitPage 0x1801470f0 (l.229623) / UpdateLocalOp 0x180147980 (l.230075).

### Data checks (rd.py on Mill_MM / Mill_Inch defaults + 17 user parts: 151 contour ops, 5 drill ops)
All offsets hold sensible values: flags 0/1, counts small (tabs 0x1382 = 2/4, MP 0x8aa = 1), lengths plausible (tab width 20 mm / 1 in,
max tab distance 250 / 9.5, depth variance 0.05 / 0.005, clearance 50 / 2, breakthrough 0.25 / 0.01), tab ramp angle 0x13b0 = 0.785398 (45 deg in rad),
contour type 0x1308 in {0,2}, comp 0x5d0 in {0,4}. Custom drill params / break-through amounts are 0 everywhere (not discriminating).

### Conflicts with the audit / viewer code
1. **Contour Roll cutter (1335) is rec 0x5d3 (u8), not 0x5d4.** 0x5d4 is "Optimize cutter comp in control" (2144). FUN_180157e30 writes
   combo 0x9148 -> dlg+0x265b (rec 0x5d3) and check 0xd968 -> +0x265c (rec 0x5d4). Pocket already uses 0x5d3 (cam_fileop pk_roll).
   Data: user parts have 0x5d3 = 0x5d4 = 1 so stored-op output is unchanged by a fix; Mill_MM default op has 0x5d3=1, 0x5d4=0 (new MM contour
   ops today get roll None instead of Sharp).
2. **Contour / pocket Tip comp (1310) is rec 0x1231 (u8, 0 Center / 1 Tip), not 0x2f8.** mccore op_is_tip_comp: contour (op_is_contour), 0x82, 0x87
   and pocket 3 -> op+0x11c9 = rec 0x1231; face/slot/helix/2D HST (0x66/0x69/0x6a/0x86) -> op+0x290 = rec 0x2f8; circle 0x12 -> op+0x11d0 =
   rec 0x1238; drill -> op+0x1200 = rec 0x1268. (Circle / pocket rows belong to F2 / F3 - flagged for them.)
3. Multi Passes 1239 "Rough" / 1250 "Finish" are group-box frames (no value); the on/off is group 2945 = 0x8a8. Keep tool down on this page is 0x8c8
   (Depth Cuts' is 0x891).
4. Tabs: audit labels 1176 "Tab Position" / 1179 "x" are the shape-size X / Y edits of "Create tabs on shapes less than" (0x13e0 / 0x13e8).
   0x1398 = 1 means Full (radio 0), 0 Partial. 0x1381 = automatic, 0x1440 = by max distance.
5. Drill 1837 (audit ND) = countersink depth calculator (WM_COMMAND 0x72d -> OnCalcPb -> CsinkDepthDlg), writes rec 0x13c8 / 0x13d0.
6. Drill cut labels 1082 / 1087 are retitled from the post (page 39); handler names say peck clearance / retract amount. Cycle names are post text,
   so the combo enum is NOT DECODED without the .pst text reader (audit "post page 45" - the code reads page 39 (0x27; lathe 40) for
   labels/combo, page 45 (0x2d; lathe 46) for the selected cycle name, page 42 (0x2a) for custom parameter labels).
7. Comp type (1290) strings: the combo is mccore CCutterCompComboBox::InitMill (not dumped); index order 0..4 is from MapToDlg + cam_fileop's
   oracle (4 = Off). mcmillres 227/229/231/232/233 (Computer/Control/Wear/Reverse Wear/Off) are a candidate, not traced.

### Source fixes for batch 1 (do not apply now)
- viewer/cam_fileop.c:196 `p->roll = S16(0x5d4)` -> `B(0x5d3)` (contour); viewer/mcam_saveops.c:674 `w16(e + 0x5d4, roll)` -> `w8(e + 0x5d3, roll)`
  (the w16 also overwrites 0x5d4 optimize-comp and 0x5d5); viewer/cam2d.c:839 comment "op +0x5d4" -> record 0x5d3. Gate per batch-plan rule (2)
  only if any stored op has 0x5d3 != 0x5d4 (none in the 17 user parts).
- viewer/cam_fileop.c:240 `p->tip_comp = S16(0x1268)` and mcam_saveops.c:690 `w16(e + 0x1268, ...)`: the field is u8 (HSTDrillTipCompPage
  MapToOpEnt writes one byte; 0x1269 belongs to something else) -> B / w8.
- viewer/cam_fileop.c:312 `p->mc_tip = B(0x2f8)` is right for slot/helix (0x69/0x6a) but circle (0x12) is rec 0x1238 and thread (0x64) falls
  to op_is_tip_comp's global `comp_to_tip` path (not a record field) - check with F2.
- formats/opdlg_port_audit.md section 2: contour 1310 0x2f8 -> 0x1231, 1335 0x5d4 -> 0x5d3 (and 2144 = 0x5d4), pocket 1310 0x2f8 -> 0x1231,
  MP 1239/1250 not fields, tabs 1176/1179 labels, drill 1837 decoded.

## F2 notes - circle / thread / helix / slot pages (batch 0)

Rows: rows_F2.tsv (128 rows: 117 DECODED, 4 PARTIAL, 6 NO FIELD, 1 H). Data check = rd.py over Mill_MM / Mill_Inch
.mcam-defaults (opcodes 18, 100, 105, 106) + user parts (thread mills in BRACKET4, bracket4-dashed, thread-test; no
circle / helix / slot ops in the parts). Every value is sensible (percentages vs distances agree with D = rec 0x350,
feeds with rec 0x378, rpm with rec 0x390).

### Functions used (decomp/mcmill_all.c)
- 25592 HSTPtBasedCutPage: DoDataExchange l.270851, InitPage l.270944, MapToDlg l.271017, MapToOp l.271226,
  SetDialogText l.271469, set_cc_icon l.271579; base HST2DCutParametersPageBase::InitPage l.229623 / UpdateLocalOp
  l.230075 (stock, tip comp via mccore op_is_tip_comp, decomp/mccore_op_is_tip_comp.c).
  Offsets per opcode: helix 0x6a start 0x1228, sweep 0x1230, overlap 0x1238, dia 0x1240, center 0x1248, perp 0x1249, tip
  0x2f8; circle 0x12 tip 0x1238, start 0x1240, sweep 0x1248, overlap 0x1250, dia 0x1258, center 0x1260, perp 0x1261.
  For circle mill InitPage hides sweep / center / perpendicular / overlap (they are on 3271, same offsets).
- 3271 HSTCircmillTransPage: MapToDlg l.239508, MapToOpEnt l.239572, IsPageDisabled l.239491.
- 25597 HSTCircmillRoughPage: MapToDlg l.238387, MapToOpEnt l.238492, enable_controls l.239175, IsPageDisabled l.238365.
- 3270 HSTCircmillFinishPage: MapToDlg l.237312, MapToOp l.237372, enable_controls l.237939, IsPageDisabled l.237297.
- 2824 (0xb08): DDX FUN_180191da0, MapToDlg FUN_180192230 l.279220, MapToOp FUN_1801923d0 l.279268, enable
  FUN_180191ee0 l.279068, labels FUN_180192d70 / FUN_180192db0 l.279805 / 279818, arc check FUN_180192050 l.279141.
- 2969 (0xb99): DDX FUN_180192f10, MapToDlg FUN_1801931f0 l.280003, MapToOp FUN_180193280 l.280027, enable FUN_180192fe0.
- 2837 (0xb15): ctor FUN_180193610 (TreeStylePageBase(...,0xb15) -> mcmillres 2837; vftable HSTThreadmillLinkPage),
  DDX FUN_180193790, MapToDlg FUN_1801939a0 l.280366, MapToOp FUN_180193a40 l.280390.
- 2825 (0xb09): DDX FUN_180193f60 l.280605, MapToDlg FUN_1801941a0 l.280670, MapToOp FUN_1801943b0 l.280727, enable
  FUN_180194910 l.280977. (region.py for 0xb09 over-runs: the next page ctor is far away; methods end at l.~281000.)
- 2823 (0xb07): DDX FUN_18016e0c0, InitPage FUN_18016e480 l.255918, MapToDlg FUN_18016e630 l.255984, MapToOp
  FUN_18016e840 l.256037, enable FUN_18016e240 l.255853.
- 2944 (0xb80): DDX FUN_18018d2a0, MapToDlg FUN_18018d4b0 l.276137, MapToOp FUN_18018d5c0 l.276189.
- 3020 (0xbcc): DDX FUN_18018dc10, MapToDlg FUN_18018de50 l.276620, MapToOp FUN_18018df40 l.276649, enable
  FUN_18018e3a0 l.276855, %<->distance FUN_18018e050 / FUN_18018e0c0.

### Conflicts settled
- **Slot stepover (3020)**: rec 0x1268 = Stepover **%** of tool dia (edit 2079, the one followed by the "%" static);
  rec 0x1270 = stepover **distance** (edit 2077 = 0x1268 x 0.01 x D). MapToDlg FUN_18018de50. Data MM 50 / 10 (D 20),
  Inch 50 / 0.25 (D 0.5). cam_fileop.c (stepover_pct = D(0x1268)) is right; the audit's labels "2079 Stepover / 2077 %"
  are the template statics, the meanings are the other way round for 2077. Same layout on 25597 (2078 % 0x1268 / 2076
  distance 0x1270; 2022 % 0x12d0 / 1478 abs 0x1280; 1719 % 0x12d8 / 1480 abs 0x1288) and 2823 (2157 % 0x1268 / 2158 abs
  0x1270; 2159 % 0x12a0 / 2160 abs 0x12a8; 2155 % 0x1290 / 2156 rpm s32 0x1298).
- **Thread Linking 2837** is its own template and class (HSTThreadmillLinkPage, ctor arg 0xb15). Offsets clearance
  0x1230, use-clearance-only 0x319, feed 0x1238, top 0x1240, depth 0x1248, Absolute/Incremental s16 0x1250 (0 = abs).
  Agrees with cam_fileop.c CAM_THREAD. The dialog has one radio for the page; cam_fileop applies it to top/depth/feed but
  not clearance - that is engine semantics, not decided by the dialog (not checked here).
- **Unlabelled thread buttons 1685 / 1688**: unlabelled Button controls whose rectangles enclose 1677/1678/1412/1682
  and 1690/1691; not bound by DDX. Inferred frames (group boxes) - NO FIELD. (Inference from geometry; the dialogs tsv has
  no style bits.)

### Differences vs audit / cam_fileop.c
- Circle mill tip comp is **rec 0x1238** (MapToOp opcode 0x12; op_is_tip_comp 0x12 -> op+0x11d0), not 0x2f8. cam_fileop.c
  `p->mc_tip = B(0x2f8)` for CAM_CIRCLE reads the wrong byte (defaults have 1 in both, so no observable diff in data).
  Helix bore and slot do use 0x2f8. (Side note for other forks: op_is_tip_comp gives contour / pocket op+0x11c9 = rec
  0x1231, not 0x2f8 as the audit's contour / pocket rows say.)
- Thread lead 1714 "Start at center" is an inverted bool: checked when rec 0x12a1 == 0 (FUN_1801931f0).
- Circle lead 1647 "Angle" (high speed entry) is stored in radians at 0x1330, shown in degrees.
- Circle Rough / Finish / Lead pages carry real offsets the audit left blank (all rows of 25597, 3270, 3271, 25592).
- 25592 1290 / 1274: audit says "stored value honoured"; offsets confirmed 0x5d0 / 0x5d2.
- Helix 2823: audit labels 2155 "Spindle speed (RPM)" / 2156 "%" - 2155 is the % (f64), 2156 the rpm (s32).
- Thread multi 2825: audit's 1239 / 1250 rows don't exist as fields (caption frames); spring passes 580 = s16 0x12b0
  (agrees with cam_fileop); 571 feed 0x12b8, 573 rpm s32 0x12c0 (signed through GetToolSpindleRotation), overrides
  0x12c4 / 0x12c5.

### NOT DECODED / uncertain
- Cutter compensation combo strings (CCutterCompComboBox, filled by vtbl+0x308 from another DLL) for 25592 / 2824 /
  2944: index = rec 0x5d0 value, index 4 = Off; the item texts / order 0-3 need the mastercam / mccore class.
- 2825 571 enable rule: FUN_1801948f0(param_1,0x23b) argument lost in the decompile (likely group && 586).
- 2944 rec 0x1260 is round-tripped through a page int with no control (MM / Inch 1): meaning NOT DECODED.
- 2824 1723 Taper angle unit: degrees by label only (no conversion in the page code).

### Batch-1 source fixes (from this group)
1. cam_fileop.c CAM_CIRCLE: tip comp from rec 0x1238 (dialog 25592 MapToOp), not 0x2f8.
2. Model pages: 25592 for circle / helix with per-opcode offsets above; 3271 / 25597 / 3270 with the offsets in rows_F2.
3. Thread: use the real 2837 page; 2825 split into rough / finish / spring fields (0x8aa / 0x8b0 / 0x8b8 / 0x8c0 /
   0x12b0) instead of one "Radial passes".
4. Label the %/distance pairs by meaning (3020, 25597, 2823) - do not take the label from the static to the left.

## F3 notes - Depth / Leads / Pocket / Face / Type pages (batch 0)

Rows: `rows_F3.tsv` (255 rows: 237 DECODED, 17 NO FIELD, 1 NOT DECODED). Generator: `gen_F3.py` (cites the
mcmill_all.c line of every function used). Record offset = page `dlg(+0x1560)` offset - 0x2088 (checked: dlg+0x20f4 =
rec 0x6c opcode, dlg+0x32b0 = rec 0x1228 HST style) or the `ent` offset of MapToOpEnt (= record offset).
Unnamed functions taking `longlong *param_1` index in 8-byte units; `mapx8.py` rescales them.

### Functions used (mcmill_all.c)
- Depth 25598: DepthCutsPageBase::MapToDlg / MapToOpEnt / UpdateLocalOp / IsPageDisabled / SubpgmAllowed
  (l.66319-66850), HSTDepthCutsPage::MapToDlg / MapToOpEnt / UpdateLocalOp / EnableSubpgmRB / enable_taper_controls /
  OnSetActive_Page.
- Depth 3030: HSTPocketDepthCutsPage = FUN_18017c020 (l.264693, derives HSTDepthCutsPage); DDX FUN_18017c170, load
  FUN_18017c550, store FUN_18017c870, enables FUN_18017c1e0 / FUN_18017c5f0.
- Depth 25602: HST2DDepthCutsPage = FUN_180147b90 (derives FUN_18017c020); DDX FUN_180147cf0, show/hide FUN_180147e30
  (ShowWindow 0 / 8), load FUN_180148160, store FUN_1801489e0.
- Leads: HSTLeadIOPageBase ctor FUN_18016efd0 (dialog id passed in: 0xae9 pocket, 0xbc5 contour; FUN_18016f280 is the
  0xbc5 variant), DDX FUN_18016f7b0, load FUN_180171320 (also FUN_180170bf0, InitPage-side), store FUN_1801735c0 /
  FUN_1801735b0, enables FUN_18016fb00 (entry) / FUN_18016fcd0 (exit). HSTContourLeadIOPage FUN_180158ef0 adds adjust
  start / end: DDX FUN_180159160, load FUN_1801599d0, store FUN_180159e00, InitPage FUN_180159770 (sets
  `ctour_prm` = record 0x1228 for contour, 0x1288 for opcode 0x86).
- Pocket Cut 2940 (HSTPocketCutPage, ctor at l.263905, 0xb7c): load FUN_18017b650, store FUN_18017b760, pocket-type combo fill
  FUN_18017bc40, child panels FUN_18017b220 / FUN_18017b8b0; tip comp / stock via HST2DCutParametersPageBase::InitPage /
  UpdateLocalOp + mccore op_is_tip_comp.
- Pocket Rough 3003: load FUN_180183fb0, store FUN_1801841f0, method table FUN_180183c20, list fill FUN_180183890.
- Pocket Finish 3004: load FUN_18017ed30, store FUN_18017f050, comp combo FUN_18017f9e0.
- Entry 3005 load FUN_18017cf40 / store FUN_18017d020; Helix 3010 load FUN_18017ffd0 / store FUN_1801801f0; Ramp 3011
  load FUN_180181820 / store FUN_180181a80.
- Face 2942: load FUN_18016ba50, store FUN_18016bcb0, enables FUN_18016caf0, stepover clamp FUN_18016b620, %->abs
  FUN_18016cea0, combo fills in the InitPage (strings 0xa73-0xa7a, 0x15e-0x165).
- Type pages: ctors only (FUN_180151a00 HSTChainBasedPage 0xb0e, FUN_18015fb10 HSTCXPtBasedPage 0xaf0, FUN_1801a11b0
  MillToolpathTypePage 0xadf); ToolpathTypePageBase lives in mccore (not dumped) - handlers NOT DECODED.

### General finding: "[edit] % [edit]" pairs
In every template pair laid out `Label [edit A] % [edit B]` (static "%" right after A) edit A holds the **% of tool
diameter** and edit B the **absolute** value (the code computes B = A * D / 100, D = record 0x350). The audit / tsv put
the "%" label on B. Affected: leads 25166/25165, 25172/25171, 25181/25180, 25186/25185; adjust start/end 1990/1302,
1992/1306; 3003 2061/2062 (and 25329 % / 1364 distance); 3010 2022/1478, 1719/1480; 3011 2024/1961, 1720/1965;
25602 3304/25199; Face 1722/1730, 1737/1743, 1748/1752, 1755/1756, 1704/1708. Data agrees everywhere (e.g. Pocket MM D=20:
helix min 0x12f8 = 50, 0x12a8 = 10; Face MM D=20: 0x1240 = 75, 0x1238 = 15).

### Conflicts settled
1. **Lead offsets** (code: FUN_180171320 load, FUN_1801735c0 store): entry on 0x8f8, perpendicular flag 0x8f9, length %
   0x930 / abs 0x900, ramp height 0x908, arc radius % 0x938 / abs 0x910, sweep 0x918, helix height 0x920, first depth
   only 0x928, use point 0x929, use point depth 0x92a, plunge after first move 0x992; exit on **0x940**, flag 0x941,
   length % 0x978 / abs 0x948, ramp 0x950, radius % 0x980 / abs 0x958, sweep 0x960, helix 0x968, last depth only 0x970,
   use point 0x971 / depth 0x972, retract before last 0x993; master 0x8f0, overlap 0x988, midpoint 0x990, gouge 0x991.
   Override feed (on/value): contour 0x1358/0x1350 entry, 0x1368/0x1360 exit; pocket 0x1448/0x1440, 0x1458/0x1450; 0x86
   0x13b8/0x13b0, 0x13c8/0x13c0 (the tsv's "0x1350 0x13b0 0x1440" lists are these three opcodes, not cross-talk).
   The tsv (0x930/0x938/0x918 entry, 0x978/0x980/0x960 exit) was right for % / sweep; **viewer/cam_fileop.c LEADS() is
   wrong in naming**: it takes 0x948/0x958/0x960 (EXIT absolute values and exit sweep) for both sides and treats 0x940
   (Exit on/off) as a "% of D" switch with 0x978/0x980 (exit %). Data: all 25+ user contour ops and both defaults have
   entry == exit, 0x930 = 0x938 = 0x978 = 0x980 = 100, 0x8f9 = 0x941 = 0 (Tangent), 0x940 = 1, so the regenerated moves
   are the same; but e.g. buckle-v6 op 13 (D 1.5) stores abs 3.0 with 100 %: Mastercam's dialog shows 1.5 (% wins on load,
   FUN_180171320); which value the 2D contour engine uses is NOT DECODED here.
   Lead sweep is stored in radians (all data 1.5708).
2. **Face overlaps**: % fields at 0x1260 (across), 0x1250 (along), 0x1270 (approach), 0x1280 (exit), 0x1240 (max
   stepover); absolutes at 0x1258, 0x1248, 0x1268, 0x1278, 0x1238. cam_fileop.c face_along 0x1250 / face_overlap 0x1260 /
   stepover 0x1240 are the % values - correct. Audit row 1730 "% 0x1240" is reversed (1722 = %, 0x1240; 1730 = abs, 0x1238).
3. **3010 / 3011 multi-offset rows**: each control has one offset (table rows). The extra tsv offsets were the other
   child page's fields (helix 0x12a8-0x1308, ramp 0x1310-0x1370): ui_map.py's heuristic pairing mixed the two pages'
   copy statements (each page has two identical store blocks, e.g. 3010 FUN_1801801f0 and the block at ann l.384).
4. Depth 25602: 3304 is the overlap %, 0x1760; 25199 the distance 0x1768 (audit swapped); 3013 here is the "Island
   facing" group 0x1740 (shown only for HST styles 0 / 1).

### Other findings
- Tip comp for Contour / Pocket is record **0x1231** (mccore op_is_tip_comp: op_is_contour / opcode 3 -> op+0x11c9),
  not 0x2f8 (0x2f8 = op+0x290 is Face 0x66 / Slot 0x69 / Helix 0x6a / HST 0x86 / 0x65). Data cannot distinguish (both 1).
- Pocket record 0x5d4 = "Optimize cutter comp in control" (3004 ctl 2144); pocket roll is 0x5d3. cam_fileop.c reads
  contour roll as S16 0x5d4 - check against the contour cut page (F1 scope).
- Pocket cutting method codes (0x123c): 0 Zigzag, 1 Constant Overlap Spiral, 2 Parallel Spiral, 3 Parallel Spiral
  Clean Corners, 4 Morph Spiral, 5 True Spiral, 6 One Way, 8 High Speed (defaults = 8). cam_fileop `pocket_style =
  0x123c == 0` (zigzag) is consistent.
- Face style codes (0x1228 s16): 0 Zigzag, 4 One way, 3 One pass, 5 Dynamic.
- Pocket type 0x13fa: combo index 0 Standard .. 4 Open (strings 2334-2338); type 3 forces entry Off; SubpgmAllowed
  only for types 0 / 4. Child panels 3000 / 3001 / 3002 / 3146 not decoded.
- Angles stored in radians where data is non-zero (taper 0x1290 / island taper 0x1298 = 0.0523599, helix / ramp plunge
  0x12c0 / 0x1328 / 0x1330); roughing angle 0x1250 (pocket), face 0x1290, ramp XY angle 0x1340: all data 0, units
  unverified.
- Midpoint 25158 / gouge check 25200 are hidden unless opcode 1, 0x85 or 0x86 style 2 -> hidden on the Pocket lead page.

### Batch-1 source fixes (list only, nothing edited)
1. viewer/cam_fileop.c LEADS(): entry = 0x8f8 on, 0x930/0x900 length, 0x938/0x910 radius, 0x918 sweep; exit = 0x940 on,
   0x978/0x948, 0x980/0x958, 0x960; fix the comment that calls 0x940 a "% of D" flag. Keep output identical for the
   189 stored ops (they all have entry == exit, % = 100).
2. Audit / cui_opdlg.c citations: contour & pocket tip comp 0x1231 (not 0x2f8); the "%"-label swap list above; Face
   1722/1730 swap; 25602 3304/25199 swap.
3. If the model exposes Use point / plunge / retract etc., use the per-side offsets above (not shared).

### Remaining NOT DECODED (my pages)
- Face page member +0x2f80 <-> record 0x12a8 (no control).
- Use entry/exit point enable flag (+0x11ad8) source for contour ops.
- 2D engine use of the absolute lead values vs the %.
- Pocket-type child panels 3000/3001/3002/3146; Toolpath Type page handlers (mccore ToolpathTypePageBase).
- Copy buttons 25502 / 25504 / 1391 / 1392 handlers (display only).

## F4 notes - 2D HST pages (batch 0)

Rows: rows_F4.tsv (131 rows; record = op + 0x68, confirmed by HSTCtourWallPage::MapToOpEnt passing ent+0x68 to
MapToOp, and dlg(+0x1560)+X = record X-0x2088: +0x20f4 = rec 0x6c opcode, +0x32b0 = rec 0x1228 HST style).

### Functions used (decomp/mcmill_all.c unless noted)
- 3205 HSTDynPockParamsPage: ctor FUN_180165d00, init FUN_180167050 l.251552 (combo strings, MCReal ranges), combo
  3328 fill FUN_180166d30 l.251453, MapToDlg FUN_180167720 l.251736, UpdateLocalOp FUN_180167a40 l.251840, stepover
  clamp FUN_1801661a0 l.251129, %->distance FUN_180168640 l.252412, enable FUN_180168b70 l.252576 / FUN_180168c30
  l.252613. Tip comp / stock: HST2DCutParametersPageBase::InitPage l.229623 / UpdateLocalOp l.230075 through mccore
  op_is_tip_comp (decomp/mccore_op_is_tip_comp.c: opcodes 0x66/0x69/0x6a/0x86 -> op+0x290 = rec 0x2f8).
- 2784 HSTPeelTrochoidalPage: InitPage FUN_180179370 l.262879, MapToDlg FUN_180179bc0 l.263107, UpdateLocalOp
  FUN_18017aca0 l.263820, enable FUN_180179de0 l.263199.
- 2833 HST2DPeelFinishPassesPage: MapToDlg FUN_180149450 l.231091, UpdateLocalOp FUN_180149e00 l.231499, enable
  FUN_180148f60 l.230937.
- 3299 HSTCtourWallPage (named) MapToDlg l.244447 / MapToOp l.244493, block FUN_18015ce40.
- 3301 HSTFinCtourPage (named) MapToDlg l.255231 / MapToOp l.255305 / EnableOrDisableCtls l.254932, block FUN_18016d390.
- 1337 HST2DRestMaterialPage: mcmill FUN_18014a230 l.231670 / FUN_18014a440 l.231755; control members from the base
  class in **mcmsurf.dll** (decomp/mcmsurf_all.c HSTRestMaterialPageBase::DoDataExchange l.100109, EnableComputeGroup
  l.100214, EnableAdjustmentsGroup l.100135, SetDialogText l.101123; strings mcmsurfres 25543-25545). Not mccore.
- 2826 HSTRoughTransitionPage: MapToDlg FUN_18018a6d0 l.274182, UpdateLocalOp FUN_18018b1e0 l.274668, defaults
  FUN_18018b160/b320/b340/b3b0/b410.
- 3219 HSTEntryMotionPage: MapToDlg FUN_180169e40 l.253075, UpdateLocalOp FUN_18016ae90 l.253641, enable FUN_180169600.
- 379 HSTCoreRoughPage (vtable only; base HSTRoughPageBase ctor FUN_180187e80, region l.272677-273870): MapToDlg
  FUN_180188b20 l.273041, UpdateLocalOp FUN_1801897f0 l.273650, corner-rounding enable FUN_180188520 l.272848.
- 25603 HSTLeadPage: MapToDlg FUN_180274960 l.429580, UpdateLocalOp FUN_180274db0 l.429790.
- 1343: mcmill HST2DTrochoidalMotionPage MapToDlg FUN_18014eb90 l.234228 / UpdateLocalOp FUN_18014ece0 l.234301; the
  mccore base has no MapToDlg / MapToOp (OnOK_Page is empty).

### Ghidra run
- mccore (read-only, -noanalysis, flock): `decomp/opdlg_mccore_hst_rest_troch.c` (33 functions,
  HSTTrochoidalMotionPage::*; HSTRestMaterialPageBase / HSTRoughRestMaterialPage are NOT in mccore - they are in
  mcmsurf, already in decomp/mcmsurf_all.c). Log: batch0/ghidra_f4a.log.

### Data checks (rd.py, Mill_MM / Mill_Inch defaults + user parts)
- 0x1250 = 0x1580 % x D everywhere (MM D20: 25% -> 5; parts D3: 30% -> 0.9; D6: 25% -> 1.5); 0x1650 = 0x1658 % x D
  (10% -> 2 / 0.6). Peel MM: 0x1258 = 80 = 4 x D20 (code default 4 D). Area: 0x1598 = 0.45 D, 0x1590 = 0.55 x 0x1598,
  0x1588 = 55 (matches formats/area_mill.md 2). Contour wall 0x1748 = 0.025 mm / 0.001 in. Entry plunge angle
  0x1678 = 2 (degrees); Transitions plunge angle 0x15e0 = 0.0349066 (radians). 3202 gap/retract 0x16b9 = 0 in all.

### Conflicts settled
1. **Peel 2784 mixed rows**: not tsv cross-talk. Peel uses the same HST cut-parameter block as Dynamic:
   615 = stepover *distance* 0x1250, 2078 = stepover % 0x1580, 3198 = min radius *distance* 0x1650, 3199 = % 0x1658,
   616 back feed 0x1248, 953 approach 0x1660, 3438 conventional feed 0x17d0, 3200 micro lift 0x16d0. Slot width 618 =
   **0x1258** (0x350 is tool diameter, read only for the 4 D default). New: 3330 style 0x17d8, 3328 method
   0x1564/0x1784 bit 1, Extend entry/exit groups 0x1240 / 0x1272, lengths 0x1278 / 0x1280, Left/Center/Right 0x1270
   (Left=1 Center=0 Right=2).
2. **3205 label pairing (audit wrong)**: on 3205 / 2784 the "Stepover" row is [2078 %][615 distance] and "Min toolpath
   radius" is [3199 %][3198 distance] (template x positions + MapToDlg). Audit rows "615 % ... abs of 2078" and
   "3198 % 0x1650 abs of 3199" have the labels reversed for 3198/3199 (3199 is the %, 3198 the distance).
3. **3328 Compensation direction**: for Dynamic Contour (opcode 0x86 style 2) it is Left/Right writing BOTH 0x5d2 and
   0x126e (index != 0); for Dynamic Mill / Peel the same control is relabelled "Cutting method" Climb / Conventional /
   Zigzag (0x1564 / 0x1784 bit 1). Our stand-in "Outside / Inside the chain" (cam_fileop.c `dyn_outside = B(0x1567)`)
   is **not** this control; 0x1567 is not written by any F4 page (it only selects a picture in FUN_180167720).
4. 3202 gap/retract combo = 0x16b9 (formats/ui.md confirmed); 3203/3204 = 0x15cd; 3356 = 0x1790 (8 positions);
   3357 = 0x16bf (None / Material / Air).
5. **Area 379**: the dialog edits fixed record fields (0x1564, 0x1566, 0x1580-0x15b0, 0x15cd, 0x1600, 0x1608), not the
   macro. The McHmm macro is built from them at generation (formats/area_mill.md 2: MinOffset/MaxOffset = 0x1590 /
   0x1598, LinkClimbMilling = 0x1564 == 0). Same for Transitions 2826 (0x15c9-0x15f8), Trochoidal 1343 (0x15ce-0x1638)
   and Leads 25603 (0x1798 / 0x17a0): record fields; whether / how each reaches the macro NOT DECODED here.
6. Contour Wall 3299 = 0x1750 / 0x1758 / 0x1748 (op+0x11c0 block +0x528/+0x530/+0x520). HST Finishing 3301 and Peel
   Finish 2833 share the same fields (0x1241, 0x1780, 0x1788, 0x14b8, 0x14c0, 0x14c4, 0x14c5, 0x1290 inverted, 0x5d0).
7. Entry 3219: 3212 ramp spindle = 0x1698 (s32) (audit had none); 3215/3217 radio = 0x16bd.

### Outside F4 scope, noticed
- op_is_tip_comp: Contour (op_is_contour), opcode 0x82, Pocket 3, 0x87 -> op+0x11c9 = **rec 0x1231**; Circle 0x12 ->
  rec 0x1238; drill -> rec 0x1268; Facing 0x66 / Slot 0x69 / Helix 0x6a / 0x86 -> rec 0x2f8. The audit lists tip comp
  0x2f8 for Contour / Pocket / Circle pages - those owners should check (F1-F3).

### NOT DECODED / partial
- 2833 1290 and 3301 3324 Compensation type: combo filled by UICombobox vt+0x308(op) outside mcmill; strings and the
  index<->value map of 2833 (lost by the decompiler) not decoded.
- 2784 enable flag of the "Single chain only" group (618/631/633/634): set from FUN_18017ab60 chain counts, which
  output is used is lost in the decompile.
- 2826 1006 plunge angle: stored in radians; whether the edit shows degrees (MCReal type) not decoded.
- 1337 radio index order (25608/25609/25615 and 25616/25612/25613) taken from template order (DDX_Radio on the first).
- Enable rule of 953 Approach distance on 3205: none found.
- 1343 IsPageDisabled in mccore reads dlg+0x3768 (surface layout); the 2D override, if any, not found.

### Source fixes for batch 1 (do not apply now)
- cui_opdlg.c / opparams_ui.inc / opdlg.cpp 3205 + 2784: swap labels so 3199 = "%", 3198 = distance; 615 is the
  stepover distance (0x1250), not a %.
- Dynamic Contour comp direction: replace the "Outside / Inside the chain" stand-in (0x1567) by 3328 Left/Right writing
  0x5d2 and 0x126e; keep cam_fileop.c reading 0x1567 until its generator meaning is decoded (fileop_cmp gate).
- Dynamic Contour "Wall stock width" stand-in -> Contour Wall page 3299 (0x1748 / 0x1750 / 0x1758).
- Area Mill cut page: use template 379 fields (above) instead of the 3205 controls.
- Peel slot width: 0 = 4 x D (Mastercam code default, FUN_180179370), not "0 = 1.5 D" (audit note on our stand-in).
- Entry 3219: 3212 ramp spindle has record 0x1698.

## Arc Filter / Tolerance 10318, Coolant 6546, Canned Text 6548

### Arc Filter / Tolerance - mccoreres 10318, ArcFilterRefinePage (resolved)

New dump decomp/opdlg_mccore_arcfilter.c (mccore, 73 functions: ArcFilterRefinePage / ArcFilterTolerancePageBase
ctors, DoDataExchange, InitPage 0x1809a8810, UpdateLocalOp 0x1809aaaf0, KeepTolerancesConsistent 0x1809a8ef0,
ValidateRftpDialogControls 0x1809aaf70, UpdateEnableStareForTighteningGroup 0x1809aa990, Get_m_* / Set_m_*). The page
has no MapToOp / MapToDlg: InitPage reads the op (dialog op copy at page +0x1560, record = X - 0x2088) and
UpdateLocalOp writes it back. Record fields:

- Line/Arc filter group 10385: 0x33a (u8 on); line tolerance "+" 10368: 0x320 (f64, written only when changed);
  XY / XZ / YZ (G17 / G18 / G19) 10373-10375: 0x33b / 0x33c / 0x33d; One way 10372: 0x33e; Min / Max arc radius
  10377 / 10379: 0x328 / 0x330; tighten radio 10388-10390: 0x33f (0 use maximal, 1 line, 2 arc - index order
  inferred from the template); tightening value 10391: 0x340 (meaning of the value NOT DECODED).
- Smoothing group 10386: 0xca8; smoothing tolerance "+" 10382: 0xc98; Use fixed segment length 10397: 0xca9; Segment
  length 10401: 0xca0; Shift points randomly 10398: 0xcaa; Minimize number of points 10399: 0xcab; Present arcs as
  line segments 10400: 0xcac.
- Cut tolerance 10366: the cut page's linearization field, by opcode: 0x1228 (contour 1, 0x33-0x35, 0x82, slot 0x69,
  pocket 3), 0x1230 (face 0x66), 0x1288 (2D HST 0x86), 0x12a0 (others); InitPage clamps <= 1e-5 to 1e-5.
- Output 3D arcs 10405: 0x15cb (0x86), 0x16da (0x84), 0x12e1 (pocket), 0x1300 (face); not stored for other opcodes.
- Total tolerance 10371, the three % edits (1393 / 1383 / 1392) and the sliders 1484 / 1485 / 1483 are derived
  (sum / shares), not stored.
- Enable rules: smoothing controls follow group 10386, filter controls group 10385 (ValidateRftpDialogControls);
  10401 needs 10397 on, 10398 / 10399 need it off; 10391 + slider 1483 need radio != 0 and the filter group on.

Data (rd.py, Mill_MM / Mill_Inch defaults + 205 ops of the user's parts): 0x33a = 0 in every mill 2D default and every
user op (only opcode 416 default has it on); 0x33b = 1; 0x328 / 0x330 = 1 / 1000 (MM, 2D mill), 0.005 / 100 (drill-type
defaults); smoothing 0xca8 = 0 everywhere; cut tolerance 0.025 MM at the per-opcode offsets above; Output 3D arcs
HST 0x15cb = 1, pocket 0x12e1 = 1, face 0x1300 = 0. So in the user's parts Mastercam's line/arc filter is OFF: the arcs
in their NC come from the engines, not from the filter.

Our "Create arcs in XY" (cui_opdlg.c PG_ARC) is `CamParams.arcs`, a switch of this program (cam2d.c 1839 / 1856,
nc_post.c 166 / 173: engine arc records + our own arc fitting), not a record field, and the old mcmillres 318 label.

### Coolant 6546 / Canned Text 6548 (pointer only)

Both pages work on one op array: record 0xb52..0xb79 (20 x s16; dialog op +0x2bda..+0x2c00).
CannedTextPageBase::SplitCannedSelect 0x180167800 (decomp/opaudit_mccore_pages.c) splits it by |value|: 1-999 and
3000-3999 = before, 1000-1999 and 4000-4999 = with, 2000-2999 and >= 5000 = after. CoolantPageBase::SetDialogData
0x180234fd0 passes the same array to CCNCMachine::ValidateCoolant and takes labels / commands / enable state from the
machine definition (CoolantOnCmd / CoolantOnTxt / IsCoolantOptionEnabled / UseCoolantFromPost). All 205 user ops and all
defaults ops have zeros there; the coolant actually in the user's files is the tool's TlCoolant OldStyle (mcam_saveops.c
637). Per-control value coding of the coolant combos: NOT DECODED.

## B34 notes - engine semantics of Depth Cuts 25598 / 3030 and Lead In/Out 3013 / 2793 (batches 3 + 4, 2026-10-23)

What the generators need beyond the record fields above (decomp/mcmill_all.c unless noted; op offset = record - 0x68).

### Leads (contour: the lead application FUN_180051990 l.55862, called through FUN_1800518b0 from the contour loop)
- **Which value**: bnci_ctour (decomp/mcmill_ctour.c l.394-406): unless GetProdEnableBits & 0x10, with leads on (op
  +0x888 = 0x8f0) the entry's abs line / arc (op +0x898 / +0x8a8 = 0x900 / 0x910) are recalculated as % x 0.01 x D from
  op +0x8c8 / +0x8d0 (0x930 / 0x938) when the entry is on (op +0x890 = 0x8f8), the exit's (+0x8e0 / +0x8f0 = 0x948 /
  0x958) from +0x910 / +0x918 (0x978 / 0x980) when the exit is on (+0x8d8 = 0x940). Settles 6.3 "abs or %": the engines
  take the absolute values, recomputed from the % per side. (Bit 0x10 of the product bits: meaning NOT DECODED; the
  stored data agree with the recompute.)
- **Pieces on** (bVar22..25): entry arc = leads on && entry on && sweep op +0x8b0 (0x918) > 1e-6 && radius > nc_tol;
  entry line = leads && entry && |line| > nc_tol; exit the same with +0x8d8 / +0x8f8 / +0x8f0 / +0x8e0. With "first
  depth only" op +0x8c0 (0x928) / "last depth only" +0x908 (0x970) the side is dropped unless param_8 && param_10 /
  param_9 && param_11 - those arguments are lost in the decompile of the call (mcmill_ctour.c l.1813: FUN_1800518b0 with
  4 of its 18 arguments), so which depth / pass they are is NOT DECODED (the controls stay greyed + NOT DECODED).
  FUN_1801bfd20 (mcmill_ctour.c l.10297): depth cuts && leads && (entry && first-only || exit && last-only) makes the
  contour loop cut every depth itself instead of the depth copies (no motion difference here).
- **Piece geometry** FUN_1801c24a0 (l.311657; formats/mcmill_2d_engines.md 1.4): arc centre P + r N on the side ('L'
  / 'R'; for the entry T is negated and the side flipped), the line along T (Tangent) or along N (Perpendicular, op
  +0x891 / +0x8d9 = 0x8f9 / 0x941), entry: the arc built on the path's first element, the line on the arc; exit: arc on the
  last element, line on the arc. Each piece's +0x78 = its ramp height (line op +0x8a0 / +0x8e8 = 0x908 / 0x950, arc
  +0x8b8 / +0x900 = 0x920 / 0x968); after pock_bound_to_ctour the z of each entry record is the next one's plus its
  piece's height (the dVar6 sums), exit records the previous one's plus its height.
- **Overlap** FUN_180050a10 (l.55185): with leads on, 0x988 > nc_tol, adjust end off (ctour_prm +0x110) and a closed
  contour (param_15): the ctour's records are appended from its start, each line / arc length taken off the overlap,
  the last one cut by creep_along_ent where it runs out.
- **Adjust start / end** (leads on, ctour_prm +0xf8 / +0x110 = 0x1320 / 0x1338, Extend = +0xf9 / +0x111 != 0, length
  +0x100 / +0x118 = 0x1328 / 0x1340 > nc_tol): Extend = FUN_180054420 (l.57727): a line of that length along the end
  tangent (negated at the start), the entry / exit arc then built on that line; Shorten = FUN_1800566e0 (l.58846): the
  path trimmed by the length (FUN_1800549f0 per element); start and end only without the overlap (FUN_180051990 l.56164-56172
  calls FUN_180050a10 or FUN_1800566e0, never both - review 2026-10-23; the start Extend FUN_180054420 is independent of it). The page's % pairs:
  FUN_18015a1d0 / FUN_18015a100 (abs = % x D / 100).
- **Gouge check** 0x991 (op +0x929): FUN_1801c46f0 runs on the entry / exit pieces against the offset bounds only with it
  (l.56176 bounds, l.56368 the check); what happens on a gouge (LAB_180052fe1) is NOT DECODED - this program keeps its measured lead fit there.
- **Plunge after first move / retract before last move** (0x992 / 0x993, op +0x92a / +0x92b): flags of the depth copies'
  writer FUN_180045770 (mcmill_ctour.c l.6139 / l.6147) feeding NCI markers; the motion NOT DECODED.
- **Pocket** (2793): the pocket's finish leads go through FUN_1801cd200 (lead checks NOT DECODED) - only the sides on /
  lengths / radii / sweeps are taken (cam2d_pock.c); 25158 / 25200 are hidden for pocket (FUN_180171320).

- **Review 2026-10-23** (formats/opdlg_port_audit.md 8.1): the pieces exist with sweep > 1e-6 and radius > nc_tol, line
  |length| > nc_tol (bVar22..25) - a record sweep 0 is no arc; each piece ramps by its own height (ctour_rec +0x40 is the
  record's END z - FUN_180050a10 writes the creep point's z there - and pock_bound_to_ctour's first record is the bound's start,
  so the entry loop l.56450-56510 gives piece k a start z = path z + the heights of k and the pieces inside it); the line
  built on the arc runs along the arc's normal towards its centre with Perpendicular; end Extend first drops the path's last
  element when its gt +0x70 is 0 (op type != 0x86, l.56311-56332; +0x70 NOT DECODED, not ported); the gouge check also runs
  for open contours (l.56183-56205, offset bounds of both sides) - NOT DECODED.

### Depth Cuts (contour 25598)
- Levels: calc_depth_cuts (mccore, decomp/mccore_depths.c) from the contour loop (mcmill_ctour.c l.1546 / l.1552) with
  the finish cuts S16 0x878 / 0x880 = cam2d.c hst_depths.
- Tapered walls: FUN_180056a70 (mcmill_ctour.c l.10101): opcode 1 / 0x85 / 0x87 / 0x33-0x35: depth cuts (op +0x800) &&
  +0x12a2 (0x130a) && +0x12a8 (0x1310, rad) > tol; then the wall stock of each depth is FUN_1800569a0 (l.10086):
  tan(a) (chain Z - z [+ corner radius with tip comp ctour_prm +9 = 0x1231]) + (rc + stock) / cos(a) - rc, rc = op +0x2f0
  (0x358, the tool's corner radius), called at l.982 with the chain Z and the current depth. Pocket (opcode 3: +0x1220 /
  +0x1228 / +0x1230) and 2D HST (0x86: +0x1302 / +0x1308 / +0x16d0) taper are other engines (not ported).
- Order: ctour_prm +0xf0 (0x1318) is the first key of the sort FUN_1800447e0 (l.48183): By depth -> depth (higher
  first), then the chain, the pass; By contour -> chain first.
- Keep tool down (0x891, op +0x829): the depth copies' writer FUN_180045770 (l.6162): between depths it writes markers
  -7 / -5 / -4 / -6 instead of the retract when the flag is set or the next start is within nc_tol of the last point (the
  latter = this program's measured stay-down rule); the markers' motion NOT DECODED for contour / pocket.
- Use island depths (0x892, op +0x82a): pocket FUN_1801c7970 (l.313673) with depth cuts; not ported.
