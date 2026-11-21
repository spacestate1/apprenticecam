# Saving an operation's toolpath (NCI) with the part - what makes an op open without the red X

Goal (user, 2026-11-19): when apprenticeCAM saves operations whose toolpath it computed, store each op's NCI section as
Mastercam does, so the op opens without the red X (dirty) and Toolpaths > Reverse toolpath works on it after a re-open.
Code: `viewer/mcam_saveops.c` (save_impl: "the NCI sections of the ops computed here"), `viewer/nci_write.c` (the section,
formats/nci_writer.md). Tests: `tools/tests/nci_save_check.py` (the loader's checks on a saved part),
`tools/tests/nci_save_oracle.py` (saved section vs Mastercam's stored section). Branch nci-save.

Decompiles (all mccore 2017 x64 unless noted; paths relative to the repo's decomp/):
- save_db @1802bc3d0 (mccore_fileio.c l.258-; NCI part l.1335-1500)
- bnci_restore @180133f40 (mccore_dbread.c l.9510-12530), bnci_op_section_valid @180132a80 (b12/acp2.c l.2254-2330)
- bnci_wr_sos @180097170 (nciw/core_wr.c l.2732-2808), bnci_wr_eos @180092820 (nciw/core_wr.c l.1932-2030)
- bnci_section @180088880 (xformop/xform_assoc.c l.392-420), FUN_180105130 (its engine dispatch, xformop/xform_assoc.c l.5393-7376),
  FUN_180103fb0 (its end, mccore_filter.c l.3480-3730), bnci_regenerate @180087bc0 (dirty/core_regen.c l.595-1032)
- edit_operation @1800b0760 (mccore_edit_operation.c l.2572-2590), op_is_no_nci @1800c6930 (mccore_bnci.c l.4921)
- mcmill bnci_drill @180067cf0 (mcmill_all.c l.69483-70300), FUN_1802177f0 / FUN_180218410 (the contour writer's section start,
  mcmill_all.c l.366922-367620)

## 1. Where the NCI is and how it is tied to the op

| what | rule | source |
|---|---|---|
| place | after every ASSOC record: an NCI_ID record (ASSOC 5), then the records of every saved section to the end of the file | save_db l.1381-1490 (formats/mcam_write.md 1, rows 17-18) |
| NCI_ID | len 0xb2; +0 class 0x4000, +4 sel bits 0x0101 (1, 0x100), +0x2c sub 5, +0x48.. the writer's version stamp, the rest 0 | save_db l.1381-1387 (memset, 0x4000, +0x2c = 5, sel_bit_turn_on 1 / 0x100); measured: the same bytes in 16 / 16 user parts |
| written when | some op has op +0x80 >= 0 and (op +0x94 == 0 or op +0xe0 != 0) | save_db l.1344-1370 |
| tp_hdr +0x78 | file offset of the first NCI record (after NCI_ID); bnci_restore seeks there, < 1 = no NCI | bnci_restore l.10013-10025 (local_2f50); formats/mcam_write.md 7 |
| record | u16 size + the record; +0 i16 gcode, +4 op id, +0x10 = its own offset in the part, +0x14 = its size | bnci_write_to_disk (nci_writer.md 2); save_db writes every copied record with it (l.1488); measured: +0x10 = the offset in every record of BRACKET4 / buckle-v8 |
| which sections | the session NCI file is walked from its start; a -2 whose op has +0x80 >= 0, +0x80 < +0x84 and (+0x94 == 0 or +0xe0) and lies inside [+0x80, +0x84] starts a copy, every record is copied up to the next -2 | save_db l.1400-1490 |
| order | the session file's order: bnci_section deletes an op's old section (DeleteToolpath) and bnci_wr_sos appends the new one at the end (seek end) | bnci_section l.410-416, bnci_wr_sos l.2785 |
| link to the op | only the op id: -2 +4 / +0x1c, -3 +4 / +0x18; one section per id ("Multiple operation id #'s in nci") | bnci_restore l.11878-11988 |
| op record +0xe8 / +0xec | op +0x80 / +0x84 = the -2 / -3 positions in the session file (bnci_wr_sos / bnci_wr_eos + rewrite_ent_assoc); forced to -1 when the part loads | bnci_wr_sos l.2796, bnci_wr_eos l.2017, FUN_1802cfb60 (mcam_write.md 2) |
| checksums | none; lengths only | mcam_write.md 7 |

A section (written by the engine through nci_write.c's port of bnci_wr_*): -2 (+0x18 = 1, +0x1c op id, +0x20 opcode, +0x24 the op
record's +0x30), the section-start block (bnci_wr_sof: 1011 .. 20800, 1002 = the tool change with the first position), the motion,
-3 (+0x18 op id, +0x1c _time32). An empty section gets a 1005 with the op description before the -3 (bnci_wr_eos l.1990-2008).

## 2. The dirty flag (red X)

| what | rule | source |
|---|---|---|
| flag | op +0x94 = record +0xfc (u8); the save writes the op's value | mcam_write.md 4; opdlg_port_audit.md 19.1 |
| cleared | only at the end of a successful regeneration: FUN_180103fb0 (end of bnci_section) after the engine returned 1, writes the reference point (FUN_180096820, not multiaxis), bnci_wr_eos, then op +0xf6 = 0, op +0x94 = 0, UpdateListAndDB | mccore_filter.c l.3531-3563 |
| set | edit_operation: a change bnci_update cannot patch -> op +0x94 = !op_is_oldnci, op +0x97 = 0 (no regeneration); tool / chain edits; stock setup | edit_operation l.2572-2590; opdlg_port_audit.md 19.1 |
| on load | FUN_1802cfb60 forces +0x94 = 1, +0xe8 / +0xec = -1; bnci_restore then gives the op its section's positions and +0x94 = (op +0x80 == -1) at the -3 | mcam_write.md 4; bnci_restore l.11748-11800 |
| section valid | -2 at op +0x80 with +4 = op id and +0x18 != 0; the record at op +0x84 is -3 with +4 = op id (op_is_no_nci ops: always valid) | bnci_op_section_valid (acp2.c l.2283-2310) |
| load checks | an op is regenerated-dirty (its section deleted, bnci_delete_section) when: a 1014 / 1017 / 1027 matrix is invalid or differs from op +0x578 / +0x628 / +0x6d8 (mx33_match 2e-7; only with op +0x570 / +0x620 / +0x6d0 set and op +0xe0 == 0), the 1013 Tplane origin differs, 1016 +0x1e != opcode (not xform / no-regen / lathe / chook), 1016 +0x60 != op +0x544 or the tool does not exist, 1000-1002 +0x24 / +0x28 / +0x2c != op +0x2e0 / +0x308 / +0x30c | bnci_restore l.10569-10760, 10836-10860, 11100-11230, 11712-11722, 12396-12489 |
| measured | all 205 stored ops of the user parts that have a section: record +0xfc 0, +0x148 0, +0x15a 1, +0x15e 0, +0xe8 / +0xec >= 0 (+0xff 0 in 102, 1 in 103) | user parts |

nci_write.c builds 1013 / 1014 / 1016 / 1017 / 1027 / 1002 from the op record itself (nci_writer.md 3), so a section it writes
for a record passes these checks by construction; `nci_save_check.py` emulates them (all but the 1013 origin test).

## 3. When an op made / regenerated here gets its section (the port)
The analogue of "op +0x94 cleared by FUN_180103fb0": the op's toolpath was computed here (CamOp.nm > 0), it is not dirty in this
session (CamOp.dirty: an edit applied but not computed - Mastercam would have +0x94 set and save_db would skip it), and its moves are
in the part's units (CamOp.gen_units, as nciw_view). Ops without moves are written as before (dirty, no section). Then (save_impl):
- the section is nciw_section on the op's own saved record (`opb`), its new TL_ID record or the part's of its slot, its tp records
  (drill points), the moves in op coordinates (world -> Tplane, wpt_to_op; as nciw_view);
- the sections follow the kept sections of the base (replaced ops' sections are left out, as before); with no NCI_ID in the base one
  is written first;
- record +0xe8 / +0xec = the -2 / -3 offsets, +0xfc = 0; +0xff / +0x148 / +0x15a stay as the save writes them;
- tp_hdr +0x78 = the first NCI record (ours when no stored section is left);
- every NCI record of the output gets +0x10 = its offset (save_db writes each with bnci_write_to_disk; the kept stored records move
  when records before them change - the reader overwrites +0x10 anyway);
- a section the writer refuses (nci_writer.md 7) leaves the op dirty, with a note in the save message.
Gate (section 6): by default only Drill ops (opcode 2); `MCAMUI_SAVE_NCI=all` (ours, testing) every op the writer takes.

## 4. Results (2026-11-19)
- Loader emulation (`nci_save_check.py`): every section written opens clean; BRACKET4 op 8 regenerated + saved: 16 / 16 ops clean,
  0 records with a wrong +0x10 / +0x14.
- `nci_save_oracle.py` (MCAMUI_SAVE_NCI=all) on the 17 user parts, 205 stored ops with NCI: 204 sections written (NAKTO op 3: Area
  Mill not generated here), all with +0xfc 0 and valid. 15 ops are regenerated move for move (A001 13, BRACKET4 / bracket4-dashed
  8 / 18 / 19, buckle-v10 / v11 / v12 16, buckle-v8 41, test-cube-10mm 2, thread-test 2 / 4, tramming-rod 3). Their saved sections
  vs Mastercam's stored ones (masked: +0x10, the two time stamps, the op id - the save gives the op a new id - and the drill points'
  TP idn at 80 / 81 / 82 / 100 +8, renumbered by the save): **0 / 15 sections byte-identical**; paired records 778 / 946
  byte-identical, **897 / 946 with the coordinates within 2e-5** (engine arithmetic in the last bits). The other 49 records and the
  unpaired ones are all NOT DECODED items of nci_writer.md 7: 20004 (15) / 20007 (15) tool-assembly fields (X9's layout in these
  X9-made sections), 20020 / 20021 (15 + 15, records 2017 writes and X9 does not), gcode 1 +0x40 contour flags (13), the markers
  -4..-7 (19), 1020 stock size (6: BRACKET4's stock was edited after its NCI was made), and A001 op 13's second section-start block
  (section 6). Every other record kind is identical in all 15 ops (-2, -3, 80, 81, 82, 100, 1011-1029, 20001-20003, 20006,
  20008-20019, 20800, 0 / 1 / 3 within 2e-5).
- 2017-made Handbook parts (BLOCK, CONTROL HORN, CHGREC_EX1, MULTI LEVEL POCKETS, CLEARANCE PLANE, Thread Milling, WCS_EX-0): 15 / 17
  written, all clean; 8 same-move ops; more differences (another environment: tool library path in 1013, 1016 +0x64 / +0x78, 1028
  head ids, an older drill writer, nci_writer.md 10) - not used as the oracle.
- PCR rack (s8.mcx-9: 8 ops, 2 Drill + 6 Contour, saved dirty): GUI open, regenerate all, export -> default: the 2 Drill ops open
  clean, the 6 Contours keep the red X; with MCAMUI_SAVE_NCI=all all 8 open clean in apprenticeCAM. cam_cli reverse 1,2 (default
  file) reverses the hole order (posted G81 cycle Y97.483, 88.983, 80.483 ... instead of 80.483, 88.983 ...); reverse 3,7,8 (=all
  file) runs, but see section 6. The Contour moves regenerated here carry no arc tags (Move.arc) for these chains, so their
  sections hold G1 chords only: no G2 / G3 records before or after the reverse (the LinuxCNC post fits arcs to an op's moves, not to
  stored NCI).

## 5. NOT DECODED
- Where each mill engine starts a new section-start block + 1002 (bnci_wr_sof) and writes the markers -4..-7: Contour
  (FUN_180218410 l.367586-367611 writes one through FUN_1802177f0 when its *param_4 is set; who sets it per depth / chain / pass is
  not traced), Pocket, 2D High Speed, Facing, Thread Mill (no call site found in mcmill; mccore ct_* not read). Drill is decoded
  (bnci_drill l.69999: once, at the first cycle).
- The 1013 Tplane-origin load check's view_to_world (not emulated by nci_save_check.py).
- What Mastercam X9 itself writes in 20004 / 20007 / 20020 / 20021 (the save writes 2017's records into an X9 part; 2017's reader
  converts nothing for majors >= 0x12, bnci_restore l.11573-11745).
- The +0xff / +0x15a values a regenerated op gets (FUN_180103fb0 does not touch them; the save keeps the record's).

## 6. Per-pass section structure (why the default writes Drill only)
Measured on the user parts' stored sections: several section-start blocks (1011 .. 1002) in 128 / 149 Contour, 8 / 41 2D High
Speed and 1 / 1 Pocket sections; one in every Drill (5), Thread Mill (6) and Facing (3) section and the other ops. nci_write.c
writes one block per op (nci_writer.md 3). The NCI then posts the same moves, but Reverse toolpath counts one pass per 1000-1002
record and reverses each pass in place (reverse_toolpath.md 3.3): with one block a multi-depth op is reversed as one pass. PCR op 3
(Contour, 11 depth cuts to -32) reversed from such a section starts with a G1 from Z25 to -19.091 and -32: the deepest cut
first, in uncut material. So the save writes sections only for Drill ops until the engines' block rule is decoded (user
question NS-1).

## 7. The per-pass section-start blocks (branch nci-passes, 2026-11-19; user decision NS-1)
Stored sections (user parts): a pass = `-6 [1011 .. 20800, 1002] 0 ... -7`, the passes joined by the previous pass's retract rapids
(A001 op 1: `-7 -6 0(z8) 0(z10) [block: 1002 = next start, z10] 0(next start, z10) 1 ...`; BRACKET4 op 12 the same at z25).
- Contour and 2D High Speed (also Peel) replay the engine's passes through one writer, FUN_180045770 (decomp/mcmill_ctour.c l.5904-;
  callers bnci_ctour paths l.2596 / 4959 / 5713, CHST2DProcess::OutputMotionToBnci and bnci_peel, mcmill_all.c l.468576 / 305536). Each
  pass begins with a 0xffa0 header record (bnci_wr_ctour_marker @180091d80, decomp/nciw/core_wr.c l.1906-1921: +0x18 group, +0x1a,
  +0x1c depth index, +0x20 depth, +0x28 top, +0x30, +0x32 param_7, +0x33, +0x34, +0x36). The writer's block test reads +0x32 (local_bf4):
  set -> wr_sof at l.6634 unless the previous pass had it too; clear -> at the pass's first record (l.6650-6732) the retract when the
  pass starts farther than nc_tol (or the depth / group differs), then **wr_sof (l.6732) always**, then plunge_move - or cancel_q_1000
  (l.6774: the queued block dropped) when the tool is still down at the same height and the start is within nc_tol. Every Contour
  header (mcmill_ctour.c l.1750, l.2343) and every 2D HS header (mcmill_hst2d.c l.4505, l.10852) passes param_7 = 0. Markers: a new
  depth index at the same point -7 -5 -4 -6, the same depth -7 -6 (l.6162-6182); deferred after the retract otherwise (l.6702-6728).
- Pocket: its own writer, wr_pock_toolpath @1801c7fe0 (mcmill_all.c l.315266-), wr_sof at every section start after the retract
  (l.316183; Remachining type 3: l.316024 under its own condition) and no cancel_q_1000 in it. pock_in_finish = the section marker's
  finish flag at the section start (l.315581); the block's 1016 +0x40 / 1013 +0x18 = 0 for a Pocket section unless pock_in_finish,
  else cc_computer_gcode / cc_control_gcode of op +0x568 / +0x56a (decomp/nciw/core_wr.c l.950-968, l.6932). With the finish
  compensation in control FUN_1801cd200 changes cur_op +0x568 / +0x56a first (l.318449-318452).
- Drill: one block (bnci_drill l.69999, section 6).
Port (Move.sof: 1 = a block before this move, 2 = a Pocket finish section's block; nci_write.c writes bnci_wr_sof there with the move's
point as the 1002 first position):
- Contour, cam2d.c pass(): the approach rapid of every pass after the first (after the previous retract); the "stays down" branch: a
  block at the plunge to a deeper depth (the l.6732 block, not cancelled: the height differs), none for a same-depth continuation
  (cancelled). Ramp / Oscillate (ct_ramp_chain / ct_osc_chain: writer FUN_1801fe8d0), 3D contour, chamfer paths: no blocks placed.
- 2D HS, cam2d_dyn.c hst_group_link: the first move after the retract between machining regions; depth copies inside a region none.
- Pocket, cam2d_pock.c pk_sec_start (sections of types other than 3): the next move after the retract (a rapid straight up at the
  last point still belongs to clear_move / retract_move, decomp/mccore_moves.c l.8 / l.206); CamOp.nci_blocks = 1 when the op went
  through it without the finish compensation in control.
Proof (`tools/tests/nci_pass_oracle.py`: block count + every 1002 first position within 1e-3, user parts):
| op type | block placement | block count | sections that regenerate move for move | save gate |
|---|---|---|---|---|
| Contour (1) | 129 / 149 | 149 / 149 | 0 | **open** for the standard 2D contour (user decision NS-4) |
| 2D High Speed (134) | 11 / 40 | 39 / 40 (NAKTO op 2: 2 vs 1) | 0 | closed |
| Pocket (3) | 1 / 1 | 1 / 1 | 1 / 1 (A001 op 13; both blocks' 1011-1029 / 1016 / 1013 byte-identical) | **open** (nci_blocks) |
| Drill (2) | (one block, section 6) | | 5 / 5 | open |
The 20 Contour sections that differ have the right count and XY within 0.01; the positions differ where our moves do (the approach
height 5 / 10 / 15 above Mastercam's traverse height in 18, a retract where Mastercam stays down in 2). The 2D HS first blocks
differ because the regenerated motion starts elsewhere (millimetres).
PCR s8 op 3 (Contour, 16 chains x 11 depths), MCAMUI_SAVE_NCI=all: 176 blocks; cam_cli reverse 3 keeps the depth order (first cut at
-2.909, down to -32) and reverses each pass in place - no plunge to full depth (section 6's hazard gone for this op).
NOT DECODED: Ramp / Oscillate contour blocks (FUN_1801fe8d0), the 2D HS region join of NAKTO op 2, Pocket Remachining (l.316024
condition), the finish-in-control compensation words (no corpus section), the markers -4..-7 (still not written).

### 7.1 Contour block placement / move accuracy (branch move-accuracy, 2026-11-19)
The 20 Contour sections of section 7 that differed were, on inspection, 17 block placements and 3 offset-geometry cases, plus 2
links where our moves retracted (pass oracle before 129 / 149, after **146 / 149**):
- A next, deeper depth cut that starts elsewhere (same chain): Mastercam writes the markers, the retract and the traverse at the
  retract height first, then the block (1002 = the start at the feed plane), then the rapid down (measured: BRACKET4 op 2, buckle-v6 op
  23 ...: 17 sections); a new chain's first (higher) depth or the same depth: the block before the traverse (as before). The
  FUN_180045770 branch that orders the records so is NOT DECODED - the rule is measured (cam2d.c pass(), g_pass_z).
- The tool stays down whenever the next pass starts within nc_tol of the last point - also when the next depth is higher (a new chain
  starting where the last ended): FUN_180045770 retracts only beyond nc_tol (decomp/mcmill_ctour.c l.6650-6667), and plunge_move with
  the tool down writes one G1 to the new depth at the plunge feed, up or down (decomp/mccore_moves.c l.157-189). Our stay-down branch
  took only deeper passes (ours); now any depth (buckle-v6 / v7 op 29: G1 up to -3.75 at F100 = plunge, as stored). fileop: these 2 of
  205 ops change.
- Left (3: buckle-v10 op 12, buckle-v11 / v12 op 18): Mastercam's offset path is arcs, starting 0.01 away and running the other way
  round; ours is a chorded polyline - the offset geometry (with NS-3, arc records), not the links.
2D High Speed (40 sections): the first point is within 0.001 in 12, within 0.02 in most; large in buckle-v10 / v11 / v12 op 2 (2.7-4.8
mm, 107 vs 440 motion records), NAKTO op 2 (3.2 mm), BRACKET4 op 16 (0.49); the motion record counts differ in nearly all - the
Dynamic engine's motion (mcce port), not the links. No section regenerates move for move: the 2D HS gate stays closed (NS-1 rule).

### 7.2 NS-3: the Contour's arc records and the line writer's same-point rule (2026-11-19)
- Arcs: the Contour offset is the ported MCCECalcContourOffset (cam2d.c ctoff_port, formats/mcce_ctour_offset.md), whose rings hold true
  line / arc entities; ctoff_port tessellated them for the polyline passes and nothing carried the arcs on, so every regenerated contour
  was G1 chords. Mastercam's contour pass writer emits one record per offset entity, an arc through gr_arc_mill / ct_arc_mill
  (nci_writer.md 1, 3). Ours: ctoff_port registers every result ring's arcs (g_ct_arcs, per op); after each pass's cut, ct_tag_arcs tags
  each run of feed moves lying on one ring arc (on its circle, inside its sweep, along its direction) as that arc (Move.arc, centre,
  direction) - nci_write.c writes one bnci_wr_arc per run, the LinuxCNC post a G2 / G3.
- ct_lin_mill @1808c9eb0 (mccore; Ghidra DumpDecomp 2026-11-19: decomp/nciw/core_ct_lin_mill.c): a line / rapid record is written only when x or
  y differs from prev_nc_pt or the depth from cur_depth by more than tol (`tol <= |d| && |d| != tol`), or when DAT_18143d5be is set
  (the move after a section-start block: stored "1002 (p) 0 (p)"). The engines' zero-length moves (the start point after the plunge,
  the closing rapid to the clearance already reached) therefore leave no record; nci_write.c applies the same test (mc_systol tol).
- On the chain (compensation side ON, no offset, ctoff_port not used): the pass is the chain itself, so the chain's own arc entities are
  registered (gen_contour; A001 op 10: 27 arcs, as stored).
- ct_lin_mill also splits a rapid that changes both xy and depth (gcode 0, feed not -4, cur_depth valid): ll_lin_move first to the new
  point at the current depth when going down, else to the current point at the new depth - never diagonal; this pre-record precedes a
  queued section-start block (stored A001 op 10: "0 (old, 8) 0 (old, 10) [S 1002 (new, 10)] 0 (new, 10)"). nci_write.c does the same.
- Results (user parts, pass oracle): Contour sections that regenerate move for move 0 -> **112 / 149**, their block placement 112 / 112;
  all Contour sections 146 / 149 (unchanged); Pocket 1 / 1; drill oracle 28 / 28. PCR s8 (default save): ops 3 / 7 / 8 store 176 / 52 /
  8 arc records (G3, as the op's own cam_cli NC files), op 3's section 37745 -> 6225 records; after reverse 3,7,8 all 236 are G2.
- NOT DECODED: the start point of buckle-v10 op 12 / buckle-v11 / v12 op 18 (a closed ring whose chain start is at a rolled closing
  corner): Mastercam starts at the ring's own first entity (measured 19.684441, 19.104364 = the port's ring[0] start), ours at the
  chain start pushed out along its normal (0.01 away). Starting every closed ring at ring[0] fixes these 3 but breaks 19 others (130 /
  149), starting at the corner arc's start breaks 22 (127 / 149): the rule Mastercam uses is neither; not changed.

### 7.3 2D High Speed (Dynamic): the engine input as true lines / arcs (branch hs-dynamic, 2026-11-19)
- Finding: the port of MCCECalcDynPock equals Mastercam's own mcce.dll on the same input (tools/tests/mcce_dll_oracle.sh: buckle-v6 op 33
  255 / 255 records, 250 / 250 commits), yet both were ~0.06 mm off the stored NCI. The input differed: mcmill builds the engine's
  pock_bounds from the chains' own lines and arcs (ConvertChainsToPockBounds, decomp/mcmill_hst2d.c l.2276-2470: one bound per chain,
  a full-circle single-arc chain split in two halves) and offsets them with MCCECalcOffset(1, d, bound, 0x101, 0, 1e-6, 1e-6)
  (FUN_1801ac7e0 l.17281-17321 islands, l.17426-17522 region); the port handed it 15-degree chord polygons. Measured: the DLL on buckle-v6
  op 33's region as true arcs reproduces the NCI (p50 / p95 / max 0.000 / 0.000 / 0.004 mm, as chords 0.037 / 0.059 / 0.077).
- Port: McceBnd carries the bound's entities (ne / ety / ent; mcce_dyn.h); cam2d_dyn.c registers each region / island chain's lines and
  arcs (dyn_ents_add: untrimmed line / arc chains, full circles in two halves) next to its polyline; bnds_to_sl builds the engine
  shapes from them; the wall / island offsets of such a bound run through the DLL-identical MCCECalcOffset port (sl_offset_dllf,
  viewer/mcce_offset.inc, flags 0x101) and keep the result's entities (bnd_set_ents); where that port refuses, the bound is redone as
  chords with the old model (ours). MCCE_DUMPIN writes the entity form ("B" blocks) so the DLL oracle feeds the DLL the same input.
- Results: pass oracle 2D HS block placement 11 / 40 -> **33 / 40**; sections that regenerate move for move 0 -> 6, block placement 6 / 6.
  Left: NAKTO op 2 (a region join: Mastercam starts a second region the port does not visit), buckle-v10 / v11 / v12 op 2 (another
  toolpath: 107 vs 440 records - the region / entry selection, NOT DECODED), buckle-v6 / v7 / v8 op 31 (solid-chain regions: the
  face loops stay polylines, xt_loop_path), Dynamic Contour (dc_stock_offset still chords).
- NCI gate (user rule): 6 / 6 move-identical sections place their blocks as Mastercam's -> lifted for the Dynamic region path
  (CamOp.nci_blocks in gen_dynamic_mcce); Dynamic Contour stays gated.
- Measured (2026-11-19, against the move-accuracy build): oracle.py p95 vs the stored NCI over the 37 Dynamic ops: 33 better, 3 same
  (op 31 x3), 1 worse (buckle-v9 op 46: 0.163 -> 0.205 mm; port = mcce.dll on its input, 1379 / 1379 records - the input still differs
  from Mastercam's there, NOT DECODED); median p95 0.062 -> 0.044 mm; 10 sections at 0.000. Port = DLL on every call in 19 / 37 ops
  (the others: the port writes line records where the DLL writes arc records on true-arc input - the port's arc output, open).
  Save oracle on the 6 move-identical sections: 15100 / 15126 records within 2e-5 (the rest: 20004 / 20007 and 14 motion records).
  fileop vs the master build: 39 / 205 differ = the 37 Dynamic ops (this change) + buckle-v6 / v7 op 29 (7.1).
- Later the same day (hs-dynamic):
  - Solid-chain regions (buckle-v6 / v7 / v8 op 31): the solid's face loops reach the engine as their XT lines / arcs (cam2d_dyn.c
    dyn_ents_add_xt; ConvertChainsToPockBounds takes the chain's entities as they are, decomp/mcmill_hst2d.c l.2276-2470) instead of
    polylines: p95 0.059 -> 0.000 on all three. MCCE_XTENTS=0 = the old polylines.
  - The op's end: one rapid from the last cut straight to the clearance. Measured: 40 / 40 stored 2D HS sections end with the last
    feed record and a single G0, and none has two upward G0s at one XY; ours stopped at the feed plane first (the SAFE rule's retract,
    mcce_moves) - that retract is now lifted to the clearance (gen_dynamic_mcce end). Between levels / regions nothing changes.
  - Result: pass oracle 2D HS block placement 33 -> 36 / 40; sections that regenerate move for move 6 -> 19 (placement 19 / 19). Save
    oracle on those 19: same moves 19 / 19, 54781 / 54925 records within 2e-5 (the rest: markers -4..-7 stored only, 20020 / 20021
    ours only - NOT DECODED, section 5). oracle.py over the 37 Dynamic ops: median p95 0.044 -> 0.038, worst 0.205 (buckle-v9 op 46).
  - buckle-v9 op 46 (0.163 with the chord input -> 0.205 with the true-arc input): not a port error. The true-arc input reproduces the
    stored NCI's first 88 motion records to 1e-6 (the chord input: none); the first difference is a state-0/1 line step before the
    turn up the wall x = 31.57 (Mastercam 0.0248 mm, ours 0.0390), where mcce.dll (2017) on the same input writes ours (1379 / 1379
    records). Removing the offset's four tiny corner arcs (sweep < 1.2 deg) from the input does not change the DLL's output. The
    stored NCI is Mastercam X9's; what makes that step differ (an input we do not see, or X9 vs the 2017 engine) is NOT DECODED.
  - NAKTO op 2 (one engine call; the island splits the region in two for a 6 mm tool, the engine runs both): the first 3767 motion
    records equal the stored NCI to 1e-6; port = mcce.dll on our input (14830 / 14830 records, 15289 / 15289 commits). At the
    engine's region change ours plunged to the level at the plunge feed and fed back up: the engine's rapid record runs at the level
    (record 7184, z -8) and the next record goes straight up to the ramp start (7185, z 3.0); mcce_moves now writes that vertical-up
    record as a move to its own XYZ (OutputMotionToBnci, header of mcce_moves) - the stored NCI has a G0 down to 3.0 there. Only
    NAKTO op 2 changes (oracle.py 37 Dynamic ops; Face ops unchanged). Still different there, NOT DECODED: Mastercam retracts to the
    clearance (6.0, not the feed plane) and writes a second section-start block at that region change, and its second region's ramp
    starts at (68.504, 3.034) where mcce.dll (2017) on our input starts at (71.126, 7.141); the stored NCI is X9's.
  - buckle-v10 / v11 / v12 op 2 are Area Mill (style 0x1228 = 1, McHmm - formats/area_mill.md 11), not the Dynamic engine: their
    entry / link differences are McHmm's general linker (EvaGeneralLinkerPlan, area_mill.md 11.7 item 2), NOT DECODED - not this branch.
  - Dynamic Contour input (NS-6; gate stays closed - no stored Dynamic Contour NCI exists, windows_checklist.md 13): the engine gets
    true lines / arcs - the chain's entities (stock 0; ConvertChainsToPockBounds, decomp/mcmill_hst2d.c l.2276-2470, via
    chains_to_pock_bounds in FUN_1801b58f0 l.11331) or MCCECalcContourOffset's output entities (wall stock; FUN_1801ac7e0
    l.17528-17545), the reversal of a loop reverses its entities too (cam2d_dyn.c gen_dyncontour_mcce / dc_stock_offset; MCCE_DCENTS=0 =
    the old chords). A full-circle single-arc chain goes to the offset as its two halves (ConvertChainsToPockBounds; before, one 2 pi
    arc: the DLL wrote nothing on that bound). The CTOUR band offsets of FUN_1800922e0 (decomp/mcce_all.c l.80951-80985: MCCECalcOffset
    0x121, then 0x100) and FUN_180090290's wall growth (l.81276-81278, 0x127) now run through the DLL-identical offset (mcce_dyn.c
    ct_off; MCCE_CTOFFDLL=0 = sl_offset_m, the earlier own model).
    Check, port vs mcce.dll on the port's own input (buckle-v6 chains 0 / 1 / 6 / 13 / 17, stock -0.4 .. 0.5, D 2 / 6): 5 / 7 calls
    with records equal record for record (chords before: 5 / 7 with the 2 pi fix; without it 4 / 7); the 2 others part 5-6 records
    before the end, at the end of a state-0/1 march (the DLL takes one more 0.004 mm step) - the parked state-0/1 step rule (TODO.md).
    hst_check.py (tool vs the walls): no gouges on the six cases (chords: chain 17 stock 0.3 went 0.011 mm into the stock).
- 2026-11-19 (branch hs-steprule, NS-7): the parked state-0/1 "last step" difference was the attach probe's polarity (FUN_180024d50
  returns true when FUN_180023460's circles test is 0, disasm 0x180024f00 sete) plus the U-turn / offset-step arithmetic
  (mcce_port_plan.md 9.y). Port = mcce.dll on 37 / 37 Dynamic ops (all 56 calls); pass oracle 2D HS same-moves sections 19 -> 21
  (placement 36 / 40 unchanged); save oracle 21 / 21 same moves, 77179 / 77333 records within 2e-5; p95 median / worst 0.038 / 0.205
  unchanged; fileop vs master 39 / 205 (the 37 Dynamic ops + v6 / v7 op 29, as before). Dynamic Contour: chain 13 stock 0 equal;
  chain 6 stock 0.5 still parts at a boundary graze decided by last-bit differences (entry ramp split, FUN_18003e2f0).
