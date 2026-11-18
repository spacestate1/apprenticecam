# NCI writer (Mastercam 2017 mccore `bnci_wr_*`) - operations made here through any Mastercam post

User decisions (formats/user_decisions.md): **Q23** port the NCI writer so ops made here can be posted through any .pst with the
ported post engine (viewer/mp); **Q24** name the NC file(s) per op NC name + the post's suffix as Mastercam does (several files
when ops name different NC files); **P2** the machine group's "Post with Mastercam post (.pst)" stays default off (the LinuxCNC
post, nc_post.c, is the default and unchanged).

Code: `viewer/nci_write.{h,c}` (writer + Q24 naming + the app glue `nciw_view`), `viewer/mcam_saveops.c` `mcam_op_records`
(the OP_ID record an op would be saved with), `viewer/core_ui/cui_ops.c` `post_ops_mp`, `viewer/mcam_load.c`
(`mcam_planes_list`, `McamFile.ctl_ncsuffix`). Tests: `tools/tests/nciw_oracle.py` (records), `tools/tests/nciw_nc_cmp.sh`
(posted NC), `cam_cli PART nciw ID OUT [--sof X,Y,Z]`, `cam_cli PART nciw-replace IDS OUT.mcx-9`.

## 1. Sources (Mastercam 2017 x64; decompiles)
- `decomp/nciw/core_wr.c` (Ghidra DumpDecomp 2026-10-24, mccore, 80 functions): bnci_bytes @18003a800, FUN_18007bc90,
  bnci_wr_0 @18008d6b0, _1004, _1008, _100, _1016, _1018, _1019, _20300, _20800, _2600, _4, _80, _81, bnci_wr_arc @1800917d0,
  bnci_wr_eos @180092820, bnci_wr_lin @180094e50, bnci_wr_marker @1800950f0, bnci_wr_prm, FUN_180096820 (reference point),
  bnci_wr_sof @180096f30, bnci_wr_sos @180097170, bnci_wr_tool @180098ca0, bnci_wr_views @180099bf0, op_is_* helpers,
  bnci_write_to_disk @18009d930.
- `decomp/b11/core_b11.c`, `core_b11b.c` (batch 11): bnci_wr_1002 / 1003 / 1005 / 1011 / 1012 / 1013 / 1025 / sof / sos / eos,
  1014 / 1016 / 1017 / 1020 / 1027 / 1028 / 1029, FUN_1800a0d70 (home). `decomp/native_agent/core_wr81.c`:
  rotary_axis_op_param_to_bnci @1805ff0d0. `decomp/mccore_cc.c`: cc_control_gcode_2 / cc_computer_gcode_2.
  `decomp/mccore_op_is_tip_comp.c`, `decomp/mccore_toolfilt.c` (op_is_point).
- `decomp/nciw/core_misc.c`: FUN_1800a1ea0 (1013 +0x50; switch over the byte table @0x1800a20f4 read from mccore.dll).
- `decomp/nciw/core_grmill.c` / `core_ctarc.c`: gr_arc_mill @18036cf60 / gr_lin_mill @18036d7d0 / ct_arc_mill @1808c9b90
  (the line / arc path of the mill engines: one record per move); mcmill bnci_face @180097230 / FUN_1801e32a0 (decomp/mcmill_all.c).
- Q24: mccore build_filename @1806f1da0 (decomp/mccore_bnci.c l.7967), mcpost CPostFileUtil::CheckNcSuffix @180077610
  (decomp/mcpost_run.c; strings @0x18020fea8 "." and @0x180215538 ".%s" read from mcpost.dll), init_post @1801d6920
  (decomp/mcpost_lib.c), mcmachinedef CCNCMachine::NCSuffix @180077080 (file_ctrl +0xc36; decomp/mdef_grpprops_accessors.c),
  mcmill CBatch::PostSelectedOperations FUN_180023fa0 / FUN_180023500 (decomp/mcmill_all.c l.25501-26700: ops grouped by
  op +0x170, one post run per group, build_filename(NCI name, NC folder, NCSuffix)), mccore InitOperation
  (decomp/mccore_ops.c l.1416-1430: a new op copies the previous op's +0x170).

## 2. Record writing (bnci_write_to_disk)
Every writer: `memset(rec, 0, 0x370)`, the gcode, +4 op id, +0xc FUN_18007bc90(opcode) (0 mill, 1 lathe, 2 wire; several
writers set 0), +8 (per writer, below), the fields, then bnci_write_to_disk: 1000 / 1001 -> 1002, +4 = cur_op when 0,
+0x10 = the file position, +0x14 = bnci_bytes(gcode), then u16 (bytes - 2) and the first bytes - 2 bytes of the record.
bnci_bytes is ported whole (`nciw_bytes`).

## 3. A section (`nciw_section`)
`-2` bnci_wr_sos: +0x18 = 1, +0x1c op id, +0x20 opcode, +0x24 = op ent +0x30. Then **bnci_wr_sof(op, first point)**:
1011 (op[0x780] 0: InitToPostText reals, else op +0x7b0 x10), 1012 (InitToPostText ints = cam_postvals.c pv_misc_value, else
op +0x784 x10), 1013, 1014 (op +0x578 matrix), 1016, 1017 (op +0x628; pocket: the Tplane's), 1025 (op[0xae8] ? op +0xaea 20 shorts),
1027 (op +0x6d8 matrix, origin op +0x720 in that view), 1028 / 1029 (not lathe / wire), 1020, bnci_wr_tool (not lathe / wire:
20001-20009, 20020, 20021), bnci_wr_views (20010-20019), [20300 router], 20800, 1002(first), [drill: bnci_wr_0(first)],
[FUN_180096820 approach point]. Then **one record per move** (gr_lin_mill -> bnci_wr_lin: G0 feed -2, G1 the move's feed,
+0x18 cc; an engine arc record (Move.arc) -> bnci_wr_arc: plane 0, +0x1a cc, end, centre, +0x48 end z, feed; a move with cc 140
-> bnci_wr_1004 after it), then `-3` bnci_wr_eos (+0x18 op id, +0x1c _time32). The op's +0x80 / +0x84 = the -2 / -3 positions.
Field maps (record <- operation offset; record = op + 0x68 in the part file):
- 1013: +0x18 cc_control_gcode(op +0x568, +0x56a) (pocket 0), +0x20 op +0x2e8 (16 bytes), +0x30 tip comp == 1 (op_is_tip_comp),
  +0x32 view of op +0x578, +0x38 world_to_view(op +0x5c0), +0x50 FUN_1800a1ea0(opcode), +0x52 op +0x3ae (empty -> "NONE"),
  +0x154 op +0x550, +0x158 op[0x54c] (> 3 -> 0).
- 1016: +8 / +0x18 op id, +0x1c op +0x2e4, +0x1e opcode, +0x20 view of op +0x628, +0x28 world_to_view(op +0x670), +0x40
  cc_computer_gcode, +0x42 op +0x602 (< -1 -> -1), +0x44 IsMetric, +0x46 op +0x332, +0x48 op[0x541], +0x50 op +0x2f8, +0x58 op +0x53c,
  +0x5c op[0x540], +0x60 op +0x544, +0x64 / +0x78 op id (not xform), +0x68 op +0x5d8, +0x6a op +0xb90, +0x70 TapPitchCalculate.
- 1020: machine group (op +0x554) +0x1230..+0x125f stock size / origin, +0x48 group +0x1690, +0x50 rotary vector (op +0x978 -
  op +0x960, ...), +0x70 (int)(char)op[0x289], +0x74 MaxToolSpindleRPM (nci_ascii.c's port), +0x96 material (group +0x11d0, "NONE").
- 1002: +0x18 op +0xf8 / +0xfc / +0x100 / +0x2e0 / +0x308 / +0x30c, +0x30 plane, +0x35 op[0xc1c], +0x38 op +0x328, +0x40 op +0x310,
  +0x48 op +0x330, +0x50 first point, +0x68 home = make_reals_0(world_to_view(op +0x870 - op +0x5c0, view op +0x572)), +0x80
  rotary_axis_op_param_to_bnci(op +0x958), +0x88 op +0x990 (rotary type 1).
- 20001 op +0x336; 20002 / 20003 T +0x340 / +0x3b8; 20004 T +0x68, +0x6c, +0xbc, +0x6e, +0x70..+0x8f, +0x98..+0xb3, +0xb8, +0xba,
  +0x4d8..+0x4e7; 20005 (drill tool) / 20006 T +0x438.. ; 20007 T +0x2f0..+0x327, +0x68 = (short)T[0x2c0]; 20008 (+8 = op +0xa0)
  +0x28 = 1.0; 20009 op +0x550, op[0x54c]; T = the tool's ent image (ours: the part's TL_ID record of slot op +0x544; +8 = T +0x34).
- 20010..20015 the Cplane / Tplane / WCS plane (op +0x688 / +0x5d8 / +0x738, PlaneID(id, 2), fallback id 1) name (+0x110) and
  +0x358 when Plane +0x360; 20016 / 20017 machine group material / its comment; 20018 group name (+ ": " + comment), 20019 "".
  One buffer for all of them: bytes of a longer earlier text stay after a shorter one ("\0op" after "Top"), +8 = 0 from 20018.

## 4. App integration (Q23)
`post_ops_mp` (machine group switch on, P2): `nciw_view` makes a copy of the part in which each op made / edited here carries
the OP_ID record the save would write (`mcam_op_records` = the save's own record builder with the file write left out) and
the section written from its moves (world -> op coordinates as nc_post.c, wpt_to_op); stored ops keep Mastercam's NCI. A new
op's NC name (op +0x170): the previous op's (InitOperation); an edited Mastercam op keeps its own. Then mp_post_part per NC file.
Drill ops (2026-10-26, section 10): the section is bnci_drill's (80 / 81 / 82 / 100); the save's tp point records reach the writer
(`mcam_op_records_tp`, NciwOp.tp). Refused (never a partial program): a drill case section 10 does not port, an op the save leaves
out (3D OptiRough, chains on solids, no template), an op without moves, a stored op with a changed tool.

## 5. Q24 naming
One program per NC file name (op +0x170, case-insensitive, tree order; disabled ops left out), as CBatch::PostSelectedOperations
runs the post per NCI file: `build_filename(NCI name, NC folder, suffix)` = folder + base name of the NCI name without its
extension + the suffix; suffix = CheckNcSuffix(control NCSuffix) = ".%s" of the text trimmed of '.' and blanks (".NC" in every
user part, control page 2 ent +0xcc4). Ours: the folder is the part's (the control's NC path is a Windows path); no suffix read
(no control record): the LinuxCNC post's `<part>-mcamui.nc`; an existing file is replaced (Mastercam asks; no prompt here).
User parts: BRACKET4 -> `BRACKET4.NC`, test-cube-10mm / thread-test -> `T.NC`, the buckles -> `buckle-v6.NC` / ... .

## 6. Oracle (2026-10-24, `tools/tests/nciw_oracle.py`, 17 user parts, 205 stored op sections; X9-made NCI)
Section start, the writer on each stored op's own record vs its stored records (all bytes but +0x10 and the time stamps):
-2, 1002, 1011, 1012, 1013, 1014, 1016, 1017, 1025, 1027, 1028, 1029, 20001-20003, 20006, 20008-20019, 20800: **205/205 each**;
the drill ops' first rapid 5/5. 1020 175/205: the 30 others (BRACKET4 / bracket4-dashed) hold the stock size of their time (30 x 14,
the group now 27 x 13; the stored NCI predates a stock edit). 20004 / 20007 0/205: the X9 writer's layout (20004 +0x48 two longs,
20007 +0x40.. other fields) differs from 2017's, plus the TlAssembly fields (NOT DECODED). 20020 / 20021: 2017 records X9 does not
write. Total **6480 / 8347 records (77.6%)**, the section-start block alone 6125 / 6565 (93.3%).
Motion, ops our engines reproduce move for move (BRACKET4 / bracket4-dashed 8 / 18 / 19, buckle-v8 41, thread-test 2 / 4):
355 / 390 records byte-equal (x / y / z / feed within 2e-5: op 8's 1.3e-5), every other difference is a contour flag (+0x40 /
+0x58, NOT DECODED). Posted NC (`nciw_nc_cmp.sh`, the stored sections replaced by the writer's, mp_cli with Generic Fanuc 3X
Mill.pst and MPFAN.pst): **8 / 8 programs equal** (BRACKET4 / bracket4-dashed 23272 lines, buckle-v8 16608, thread-test 207).

## 7. NOT DECODED (written with a stand-in, counted in NciwStats.not_decoded) / refused
- The engines' section markers -4 .. -7 and the contour flags (+0x40 lines, +0x58 arcs; gr_lin_mill / ct_arc_mill param from each
  mcmill engine): none / 0 - except Thread Mill (threadmill 2026-10-26: Move.ctf from FUN_180236810, mcmill_2d_engines.md 4.4; its ops'
  motion records now all equal). bnci_to_nci skips the markers; the flags only reach `pos` of the ASCII line (the NC is the same).
- Drill (section 10), refused: rotary axis (op +0x958: unroll / FUN_1800698a0), subprograms (+0x1298: set_n1018 / 1018 / 1019), points
  from another op (op_is_opid: +0x28b), the approach point (+0x150: FUN_180096820's position test + bnci_drill's first-point rapid),
  drill entities other than points (kinds 4 / 8 of the 5-axis drill, solid points 0x1b, solid arcs 0x1c: get_solid_arc_point /
  tp_is_solid are mccore exports not decompiled), a view-dependent point with tp +0xec (tp_is_solid), a bolt circle / grid point of an op
  made here whose source is not in the part file, drill opcodes other than 2 (5-axis drill 0x1c, 0x37-0x39, router 0x132). Corpus
  (2026-11-13): none of these occurs in any drill op with stored NCI but the 5-axis drill (20 ops, its own writer, not generated here).
- bnci_wr_arc +0x5a: the callers that set it are not decoded; [inferred] 1 for a full circle (every stored arc of the user parts).
- TlAssembly / tool-system values: 20004 +0x30 / +0x80..+0xc3, 20006 +0x58, 20007 holder (+0x40 / +0x48), 20020 / 20021 names;
  is_drill_tool (20005 vs 20006: tool types 1..9 taken as drills); TapPitchCalculate (1016 +0x70, 0).
- find_view_m beyond the reserved views 1..8 (the op's stored view number), view_to_plane (1002 +0x30: 0), world_to_view of a
  session view (the op's matrix); 1013's FUN_1800db520 (an empty library path -> "NONE"); the material table (20017 "", 1020 a
  non-empty name taken as found); 1028 / 1029 / 20008 head and axis-combination components (op +0xa0 / +0xb0: 0 in every user op);
  the reference point (FUN_180096820 wpt_to_op); 'old NCI' op types; arc feed adjustment (AdjustArcFeedrates).
- Ours: the positions of a section made here start at 0x40000000; plane 1 / 2 (XZ / YZ) engine arcs are written as their chords.

## 8. Regression (2026-10-24, ~/mastercam-re/regress/run_nciw)
wf / chain / solid / tool / misc / planes 0 differ, op 1/354 (rt_post_b11_stored, as the baseline run_merge_3d), stockplanes 5/124 ->
blessed (gp_mppost_files / gp_mppost_refused: the Mastercam-post program is now T.NC; the face op made here is posted instead of
refused; run_nciw/blessed.txt), fileop 0/189, post 0/16 with every switch, fileop arc filter 0/189, scenes dumps 0/46, parity OK,
pf_roundtrip same, dlg_fileop_cmp 1/205 (NAKTO, known), MCP smoke OK (the post tool reads the last "posted" line), MPINT OK,
mp oracle c947f0c2 (as baseline), hst3d OK, nciw oracle 6480/8347, nciw NC 8/8.

## 9. NOT DONE
- Drill: the refused cases listed in section 7 (none is used by the user's parts or by a corpus drill op of type 2).
- The engines' contour flags / markers, the tool-assembly fields (section 7).
- No overwrite prompt when an NC file of the same name exists (Mastercam asks).

## 10. Drill canned cycles (2026-10-26, user decision N3; branch `ncidrill`)
Port of mcmill `bnci_drill` @180067cf0 (decomp/mcmill_all.c l.69483-70300) + `bnci_drill_depth` @1800694d0, `get_drill_direction`
(non-rotary: the op's Tplane view +0x572), `drill_view_to_op_view` (the same view: unchanged), `tp_ent_is_control`, mccore
`bnci_wr_80` / `_81` / `_82` / `_100` / `bnci_wr_lin` (decomp/nciw/core_wr.c), `op_is_opid` @1800c6f30 (decomp/ncidrill/core_opid.c,
Ghidra 2026-10-26), `tool_tip_length` @1800a5440 (objdump: K = 0.01745329251994329 @0x180d018a8, one ulp under pi / 180).
Per point (tp kind 1, the op's tp list in order; bnci_drill's ent offsets = op + 0x68):
- p = world (tp +0x98) - op +0x5c0, world_to_view(op +0x572); top = op +0x280 (-1e38 -> 0; +0x288 incremental: + p.z); R = tp +0x148 or
  op +0x128 (+0x130: + top); clearance C = R, or with op +0x111: tp +0x140 or op +0x108 (+0x110: + top); depth = tp +0xe0 or op +0x138
  (+0x140: + p.z) - (tip comp +0x1200: breakthrough +0x11f8, + tool_tip_length for drill tools other than types 6-8); start height
  A = C when +0x111 and (not +0x2b1 "clearance at start / end only" or the first point), else R.
- First point / A differs from the tool's height B: a new cycle - the first: bnci_wr_sof at (x, y, A) (its drill rapid 0), then with
  C == A, (no clearance or +0x2b1), op +0x88 != 1 and R != C a rapid to R; later: 80, then the rapids up / down between the holes; then
  82 (op +0x1258 ? the 10 values at op +0x1208 : zeros) and 81 (+8 the point's ent idn, +0x18 cycle op +0x11c0, +0x20 (x, y, depth),
  +0x38 tp +0x158 or op +0x11e8, +0x40 tp +0x150 or the feed op +0x310, +0x48..+0x67 op +0x11c8, +0x68 / +0x70 / +0x78 B / R / top - p.z,
  +0x80 op +0x11f0, +0x88 (x, y, start z), +0xa0 0x4001 | tp +0x160).
- Same height: 100 (+0x20 (x, y, depth), +0x38 / +0x40 B / R - p.z, +0x48 / +0x50 as 81's +0x38 / +0x40, +0x58 (x, y, start z), +0x70
  -0x7ffe on the last point | tp +0x160, +0x78 top - p.z, +0x80 the Tplane matrix op +0x578 (make_reals_0)).
- After the points: 80; with +0x111 and +0x2b1 a rapid back up to C.
Oracle (`tools/tests/nciw_drill_oracle.py`): the user parts' 5 drill ops (buckle-v10 / v11 / v12 op 16, test-cube-10mm op 2,
tramming-rod op 3): **28 / 28 records byte-equal**; posted NC (nciw-replace + mp_cli, Generic Fanuc 3X Mill.pst and MPFAN.pst):
**10 / 10 programs equal**. Internet corpus (Handbook Vol 1 BLOCK ops 4-6, CONTROL HORN ops 3-4, X9 and 2017): the record sequence
equal in all 10 (incl. the clearance-at-start/end rapids 0, 0 ... 80, 0), the depths / heights / feeds equal; their 81 +0xa0 (1) and 100
+0x70 (2) lack the 0x4000 / 0x8000 bits and their 100 +0x78..+0xc0 are zero / uninitialised bytes - an older writer's records (the
user's own parts carry the 2017 values). App: a Drill op made here (test-cube-10mm, 3 holes) posts through MPFAN.pst as
`G99 G81 Z.. R.. F..`, `X..`, `Y..`, `G80`.

### 10.1 Per-point options, view-dependent points (2026-11-13, branch worktree-agent-a724afed6e93378ea)
Corpus count first (all 1381 parts of <mastercam-samples>/net/ + <user-parts>/; drill ops = op_is_drill
opcodes): 50 drill ops in 26 parts - type 2: the user's 5 + Handbook Vol 1 BLOCK ops 4-6 / CONTROL HORN ops 3-4 (2017, X9, 2018);
type 0x1c (5-axis drill): Handbook Vol 3 DRILL * parts, 10 ops each in 2017 / X9 / 2018. The 2018 parts (op record 0x2940: +8 from op
+0x88 on; tp records +0x10 from +0x98 on) have no stored NCI. **No drill op uses any of the remaining features**: rotary 0, subprogram 0,
opid 0, approach / retract point 0, per-point canned text 0, per-point manual entry 0, per-point initial height 0, per-point depth 0,
bolt circle / grid / solid points 0 (point kinds: 1 in all type-2 ops; 1 / 4 / 8 in the 5-axis ones). The app cannot set them either
(Subprogram and Home / Ref. Points greyed, no per-point editor, the save writes kind 1 points with every override unset and type 'C').
So nothing below has a Mastercam reference: ported from the decompile, checked only by tracing synthetic edits of buckle-v10 op 16 /
BLOCK op 4 (formats/windows_checklist.md 6 asks for a reference part). The 15 checkable ops are unchanged byte for byte.
Ported (viewer/nci_write.c drill_section; bnci_drill decomp/mcmill_all.c l.69734-70282):
- Per-point initial height tp +0xd8 (l.69860-69873): A = tp +0xd8, local_2c5a set; such a point never starts a cycle by itself (after
  the height compare, own height or equal heights -> local_2c63 && tp[0x92], 0 without subprograms; l.69880-69899), so it is a 100 at
  the tool's old height. Whenever the height changed (local_2c67 up / local_2c64 down) after the point's 81 / 100 (l.70157-70193): 80,
  bnci_wr_0(tp, (x, y, A)), and unless it is the op's last entity (op +0x88 - 1): the next entity in op coordinates (world_to_op
  @1800a5690, decomp/align/core_oplist.c l.4119 -> wpt_to_op @1800a5950 decomp/mccore_opview.c l.96, make_reals_0) at A, and with an own
  height and none on the next one, again at the old height (local_2b78); then local_2c28 = 0: the next point starts a new cycle (with
  its own 80 - two 80 records in a row, as the decompile writes). The z index of that point is 2 (FUN_18000b7e0(&local_2c40, 2):
  objdump 0x18006911e, the decompile drops it). bnci_wr_0 with an entity (@18008d6b0): only for ent +0x74 1 / 2 / 4, +8 = ent +0x34.
  A new cycle with an own height takes q = (x, y, A) without the up / down rapids and keeps the flags (l.70107-70118).
- New cycle at the first point: B = C when C == A (the decompile's assignment inside the rapid-to-R condition, l.69944-69960).
- After the points (l.70253-70282): also a rapid back to A when local_2c67 is still set and B != A.
- Per-point canned text tp[0xed]: bnci_wr_1025(op, 1, tp + 0xee) (core_wr.c l.6670: 40 bytes at +0x18) before the point's 81 / 100.
- Per-point manual entry tp[0x163] (bnci_wr_81 / bnci_wr_100 -> FUN_18009d8a0 @18009d8a0, core_misc.c l.196): text tp +0x164,
  position tp +0x2c4 (< 2 before the 82 / 100, 2 after the 81 / 100), gcode 1005 (0x3ed) / 1006 with tp[0x2c6] for positions 0 and 2,
  1007 (0x3ef) / 1026 (0x402) for 1; bnci_wr_1005 @18008dc90 (core_wr.c l.5250): one record per line ('\n' / '\r' runs end a line,
  the text's end gives a last, possibly empty, line), +8 op id, +0xc FUN_18007bc90(op +4) (objdump 0x18008dd0b), the line at +0x18; the
  record is cleared once, so a shorter line keeps the bytes of a longer earlier one after its NUL.
- View-dependent points (bolt circle / grid): tp_pt_is_viewdep(tp + 0x98, 1) @18007a5e0 (mcmill_all.c l.82558): the source (tp +0xd0
  eptr = a part record's ent +0x20, NciwPart.ent) an ASSOC tp control entity and tp +0xe8 > 0; a source not in the file -> the char test
  (tp +0xb0 'B' / 'G' and tp +0xe8 > 0); such a point is world = view_to_world(tp +0x98, op +0x572) (l.69757-69766) and its incremental
  depth adds tp +0xa8 as it is (bnci_drill_depth l.70407-70421). With tp +0xec set it is skipped unless tp_is_solid: refused.
Not ported, why: rotary (FUN_1800698a0 + unroll_x / unroll_y, not checkable: 0 ops), subprograms (set_n1018, subs_get_status, the
1018 / 1019 records and their post side - N9-7's M98 / M99 is for Contour and not started; 0 ops, checkbox greyed), opid (the
referenced ops' sections re-read, bnci_drill_section; 0 ops), approach point (FUN_180096820's test against the section start
DAT_1813755a4 not decoded; greyed), the 5-axis drill (opcode 0x1c: its stored sections hold 80 / 81 / 82 / 100 and 11 records - an op_is_multiax
section start and a tool axis per point from get_drill_direction, both not ported; our engines do not generate the op at all, so the
writer is never reached for it).

Regression (2026-11-13, vs a master run c3cc44a): fileop 0/205 differ, post 0/16 default and with every switch (CUTTER_COMP,
FACE_LINK_FEED, DRILL_CYCLES, ARC_FILTER, ARC_FILTER + PLANES, COOLANT, FORCE_TOOL_CHANGE); drill oracle 28 / 28 and the 15 drill
sections byte-equal to master's (posted NC therefore unchanged).
