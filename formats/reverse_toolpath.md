# Reverse toolpath (Operations Manager 3582): implementation spec

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

Mastercam 2017. Command 3582 (`0xdfe`) -> `COpMgrTree::ReversePath(1)` (mastercam.dll @180366890) -> mccore
`bnci_reverse` @1800d0120 -> core `rev_core` = FUN_1800d0770. Everything below comes from decompiled code.
Where the code's intent is unclear, the spec gives what the code does and marks it **NOT DETERMINED**. None of it
has been checked against NCI that Mastercam actually reversed. Every rule here should be confirmed with a
before/after part file.

## 0. Sources and citation keys

Each citation gives a listing key and a line number, e.g. `[C:690]`.
| key | content | how produced |
|---|---|---|
| `D` | `decomp/opman_handlers_d1.c` (ReversePath) | in repo |
| `R` | `decomp/opman_mccore_rev_sf.c` (bnci_reverse, default Ghidra types) | in repo |
| `C` | rev_core @1800d0770, find_passes @1800d2130, post_fix @1800cefa0, ins_fn @18012f510, bnci_reverse_block @1800d0540 | DecompSig (below) |
| `H` | tail_xy @1800cf1b0, get_block @1800cf500, pass_max_z @1800d2470, is_vert @18013a970, comp_pairs @1800cfa20 | DecompSig |
| `K` | default Ghidra decomp of 180086810, 18013a8a0, 18013a8d0, 1800d2740, 1800d2650, 1800cf1a0, 1801327c0, 1800d2430, 18008d4b0 | `DecompAt.java` |
| `M` | 1800cfdd0, 1800cfe30, 1800cfc10, 1800cf9c0, 18013a800, 18000dd10 | `DecompAt.java` |
| `N` | bnci_get_pt @1800865c0, bnci_find_line @1801324c0, bnci_read_section @180133b50, bnci_rewrite_section @1801384b0 | `DumpDecomp.java` |
| `S` | op_is_multiax @1800c6700, op_is_mw_multiax @1800c67c0, op_is_subpgm @1800c7020 (op_is_msurf / op_is_xform are in `decomp/mccore_bnci.c`) | `DumpDecomp.java`, 2026-10-15 |

Ghidra's default prototypes drop arguments at most of these call sites. For listings `C` and `H`, a small script
(DecompSig.java: `FunctionSignatureParser` + `ApplyFunctionSignatureCmd` in the read-only session, then decompile)
first applied the prototypes below. The argument registers were checked against objdump at the
`bnci_reverse -> rev_core` call (0x1800d03cb..0x1800d03ea: r9b = bnci_reverse's r8b, `[rsp+0x20]` = esi = 0).
The listings are kept in `decomp/reverse/` (`core2.c`=C, `helpers2.c`=H, `core.c`=K, `more.c`=M, `named.c`=N, plus
`sigs.txt` (the prototypes), `dis.txt` (objdump of mccore bnci_reverse from 0x1800d0120) and `ins.txt` (objdump of
ins_fn 0x18012f510)). DecompSig.java itself was a session
script and is not kept (it is not in `tools/ghidra_scripts/`). To regenerate the listings, apply the same prototypes:
```
1800d0770 void rev_core(long opid, longlong *pnci, int *pcount, char toggleComp, char useMaxZ, char *ok)
1800d2470 void pass_max_z(longlong nci, int n, int *passes, int npasses, double **out)
1800cfa20 char comp_pairs(longlong nci, int n, char swap, int **out, int *outn)
1800cf1b0 void tail_xy(longlong nci, long n, int idx, longlong *out, int *outn, int *outidx)
1800cf500 void get_block(longlong nci, long n, long from, int to, char backward, longlong *out, int *outn, int *outidx, double *feed)
180086810 void pt_ptr(longlong rec, double **out)                       ("bnci_get_pt_ptr" per its DebugStr)
18013a970 char is_vert(longlong nci, int n, int idx, char *isUp, double *feed, char *valid)
1801327c0 void prev_rec(longlong nci, int n, int idx, int minIdx, char motionOnly, int *outIdx, longlong outRec)
18013a8a0 char is_transparent(short)   18013a8d0 char is_motion(short)   1800d2740 char is_marker_rapid(longlong rec, char which)
1800d2650 void clearance_z(longlong opparm, double a, double b, double *z)  1800cf1a0 void max_z(double a, double *z)
1800d2130 void find_passes(longlong nci, int *n, int **out, int *outn)   1800cefa0 void post_fix(longlong *pnci, int *pcount, double *arr, int n)
18008d4b0 char bnci_toggle_comp(longlong rec)   1800d2430 void swap_neg(short *rec)   18013a800 char is_pass_end(short code, longlong op)
18012f510 int ins_fn(longlong *pnci, int *pcount, int *pidx, double v)   ("adjust_z_in_bnci_rapid_line" per its _MCAssert)
```

NCI record = 0x370-byte `nci_bin`, `+0 i16 code` (see `formats/mcam.md` "Binary NCI tail", `tools/nci_decode.py`).
"Point" means the xyz that `bnci_get_pt` reads [N:3-112] and `pt_ptr` writes [K:1917-1972]:
| code | point offset |
|---|---|
| 0, 1, 2, 3, 81, 100 | +0x20 (for arcs this is the end point in plane-local coordinates) |
| 20, 21, 30, 31 | +0x20 (`bnci_get_pt` only) |
| 22, 23, 32, 33 | x,y +0x20, z from +0x40 (`bnci_get_pt` only) |
| 11, 83, 1003 | +0x18 (`pt_ptr` handles 11 and 1003 only) |
| 1002 | +0x50 (tool change "first position") |
| 1013 | +0x38 |
| 1016 | +0x28 |

`pt_ptr` returns NULL for every code not listed against it. Comparisons use the global `tol`.

## 1. Entry points and flags

**`ReversePath(char p)`** [D:994-1032]: returns at once if `GetProdEnableBits() & 0x10` [D:1007] (bit meaning
**NOT DETERMINED**). For each selected operation it calls `list_op_restore_bnci(&id,1,0)`, then
`bnci_reverse(&id, 0, p, 1, 1, &ok)` [D:1020-1021]. After the loop it calls `Refresh`. The menu command 3582 passes
`p=1` (`opman_onrclick.c:1475-1476`, `opman_tree_all.c:22159`). No call with 0 exists in the decomp.

**`bnci_reverse(long *id, char copyMode, char toggleComp, char markEdited, char p5, char *ok)`** [R:143-320]:
1. Returns if `GetProdEnableBits() & 6` [R:176-179]. Returns if the op is not found.
2. **Multiaxis op** (`op_is_multiax(op+4)`): returns silently, nothing changes [R:198-201]. `op_is_multiax`
   @1800c6700 [S]: opcodes 0x1c, 0x1d, 0x2d, 0x2e, 0x30, 0x31, 0x6e, 0x70, 0x71, 0x9a.
3. **Subprogram op** (`op_is_subpgm(op)`): `post_warning(0xb5f)` = mccoreres 2911 "Editing an operation that has
   sub-programming enabled is not allowed." The op is not changed [R:202-207]. `op_is_subpgm` @1800c7020 [S]
   (operation offsets; OP_ID record offset = operation offset + 0x68):
   - opcodes 1, 0x33, 0x34, 0x35, 0x85: no if u16 op+0x12a0 > 3; yes if op[0x800] and op[0x828] and
     (op[0x12a2] == 0 or double op+0x12a8 <= tol);
   - 2: op[0x28b] != 0;
   - 3: no if `(DAT_181378da2 & 0xfffb) != 0`; else yes if op[0x1220] == 0, op[0x800], op[0x82a] == 0 and op[0x828].
     DAT_181378da2 = `cur_op` + 0x1392, i.e. a field of the operation that was current before (bnci_reverse copies the
     reversed op into `cur_op` only later, step 5): **NOT DETERMINED** which op that is;
   - 4, 0x50, 0x9a: op[0x1229] and op[0x1227] == 0 and (i16 op+0x11c0 != 8 or op[0x12d1]);
   - 0x12, 0x66, 0x69, 0x86: op[0x800] and op[0x828];
   - 0x4a, 0x53: op[0x1496]; 0x6c: op[0x1375] and op[0x1371] == 0; any other opcode: no.

   (`op_is_mw_multiax` @1800c67c0: opcode - 0x1b8 < 0x28, unsigned.) None of the 189 stored ops in the user's parts
   is a subprogram op by these rules.
4. `copyMode==0`: `draw_bnci(id,1,0,0)` [R:209]. `copyMode!=0`: `list_op_copy(index,-1,1,1,0,&ok,NULL)` copies the
   op; `*id` becomes the op with the highest id (the copy), and the copy is reversed instead [R:212-219]. Meaning of
   the draw_bnci flags: **NOT DETERMINED**.
5. Copies the operation into global `cur_op` (0x51*0x80+0xd0 bytes) [R:228-275]. `is_pass_end` uses it.
6. `bnci_read_section(id,0,0,...,p5,&ok)` [R:276]. With `p5!=0`, records 1018/1019 (subprogram) are skipped and not
   loaded; with `p5==0` they are counted and loaded [N:276-279; label at N:395].
7. `rev_core(id, &nci, &count, toggleComp, 0, &ok)` [R:282; the 5th argument is a literal 0 (esi=0, see section 0)].
8. If ok: `bnci_rewrite_section(id,..., nci, count, ..., 0, p5, &ok)` [R:287]. `p5` there is the flag that makes
   `bnci_wr_eos` run after the records [N:566-583].
9. If `copyMode==0`: `DatabaseRetrieve(id)`. `markEdited` sets ent+0x148 = op+0xe0 = 1 (param 15095 "Binary NCI
   of operation has been edited"). `p5` sets ent+0xfc = op+0x94 = 0 (param 15086 "NCI marked for regeneration
   (dirty)"). Then `UpdateListAndDB` and `mark_op_dependents(id,0,0,1,...)` [R:294-306]. If `p5`:
   `draw_bnci(id,0,0,0)` [R:308-309]. `*ok=1` [R:311].
10. **On any failure inside rev_core (`ok==0`), nothing is written back. The op is unchanged** [R:283-312].

What ReversePath(1) passes: copyMode=0, toggleComp=1, markEdited=1, p5=1, and rev_core gets useMaxZ=0.
`bnci_reverse_block(long, CBnciReadWrite&, char toggleComp, char useMaxZ, char*)` @1800d0540 is the exported
variant that works on an in-memory record list. It is the only caller that can pass `useMaxZ` [C:2121ff].

## 2. Definitions used by the algorithm

- **transparent** codes are skipped by every scan: 1005, 1006, 1007, 1008, 1022, 1055 (bitmask
  0x400000020000f on code-1005) [K:2074-2087].
- **motion** codes: 0, 1, 2, 3, 11, 20-23, 30-33, 81, 82, 83, 100 [K:2092-2119].
- **marker rapid**: a G0 with feed (+0x38) == -3.0 (`which=0`) or -4.0 (`which=1`) [K:2124-2138]. These are not
  seen in the template NCI, where every G0 has feed -2. Meaning: **NOT DETERMINED**.
- **pass end code** (`is_pass_end(code, cur_op)`): returns true for:
  - 80, 1003, 1004, 1011;
  - -5, -4;
  - -7, -6, but not when `op_is_msurf`; for `op_is_xform` ops only when op+0x88 <= 1.
  [M:367-411]
- **prev_rec(a,n,idx,min,motionOnly)**: the nearest record with index < idx and >= min that is not transparent
  (and is motion, when `motionOnly`). Its index goes to `outIdx`, or -1 if none [K:2191-2284].
- **is_vert(a,n,idx)** (a "vertical move"): finds `P = prev_rec(idx,0,any)`. If there is none, the result is false
  and `valid=0`. Otherwise `valid=1`. The output `feed` is:
  - G1: +0x38;
  - G2/G3: +0x50;
  - 11: +0x48;
  - 81: +0x40;
  - 100: +0x40 of the nearest earlier 81;
  - any other code: -2.0.

  The result is true if idx != 0 and the code is 0 or 1 and XY distance(point(idx), point(P)) < tol; then
  `isUp = P.z < z`. The result is also true for code 100. [H:495-583; K:2009-2068]
- **Comp-pairs** (`comp_pairs`, [H:584ff], counting in FUN_1800cfc10 [M:143-240]) read the cc field
  (`FUN_1800cf9c0` [M:242-267]):
  - G0/G1: +0x18;
  - G2/G3: +0x1a;
  - a 1004 record: the code itself.

  A **start** is cc 41 or 42 (0x29/0x2a). An **end** is cc 140 (0x8c), or a 1004 record whose previous record's cc
  is not 140. If the number of starts differs from the number of ends, the count is -1 and **the whole reverse
  aborts** [C:217-220; M:226-237]. Each pair is {start idx +0, end idx +4}, stride 0x18.

## 3. Algorithm (rev_core, per op section)

All edits go through a working copy `src` = memcpy of the section [C:147-152]. Results are written into the
original array `dst` (`*pnci`), and only to index ranges that are copied back (section 3.3). **Records outside
those ranges keep their original bytes in `dst`.** That includes everything before a pass's approach block and
records between passes, except the explicit link fixes in steps 3.5.e and 3.5.f.

### 3.1 Set-up
- `toolView` = `find_view_m` of the first 1014 matrix (+0x18), default 1 [C:153-158].
- `O` = origin: `view_to_world(1013+0x38, toolView)`. With product bit 0x10 set, `O` = the raw doubles at
  1013 +0x28, +0x30, +0x38 [C:159-174].
- `REF` = point of the first 1002 (+0x50). Then, while the following records are G0, `REF` = that G0's point. So
  `REF` is the last rapid of the first run of rapids after the first tool change [C:175-189]. Only `REF.z` is used.
- The op's plunge rate (op+0x318) and retract rate (op+0x320) are read into `feedA` and `feedR` [C:145-146].
  `get_block` overwrites both before any use (it sets `*feed=-2.0` at entry [H:196]), so **the op's own rates are
  never used**.

### 3.2 Global toggles on `src` (before any reordering)
1. If `toggleComp` (1 from ReversePath): `bnci_toggle_comp` on every record [C:190-196]. It swaps an i16 value
   41<->42 at:
   - G0/G1: +0x18;
   - G2/G3: +0x1a;
   - 1013: +0x18;
   - 1016: +0x40.

   [K:2318-2363]
2. Code **1040 <-> 1041** on every record [C:197-210]. Meaning of 1040/1041: **NOT DETERMINED**.
3. `comp_pairs(swap=1)`: for every pair where neither end is a 1004 record, the start's cc value and the end's cc
   value are exchanged. The comp-on record becomes 140 and the comp-off record becomes 41/42 [H:584ff, lines 62-75 of
   that function]. After reversal the old comp-off move comes first and therefore carries the comp-on code.
4. Per-pass `maxZ[p]` (`pass_max_z`) [C:216; H:416-494]: start at -1e38. Over the records from pass start up to the
   next pass start - 1 (the last record, for the last pass), take the max Z of every G0 with is_vert, valid and isUp
   (upward vertical rapids).

### 3.3 Passes (`find_passes`, [C:1189-1341])
- **Number of passes** = number of records with code 1000, 1001 or 1002 (FUN_1800cfdd0 [M:1-27]). Each tool-change
  record starts one pass. The notes in `formats/post.md` say multi-depth ops write one 1002 per depth pass; this has
  not been checked against the reverse code.
- **Pass start S**: the first record after the tool change that is motion and not a marker rapid(-3)
  [C:1229-1250].
- **Pass end E**: scan forward from S to the first pass-end code or marker rapid(-4). Then E = the last motion
  record before that stop [C:1270-1335].
- A 1016 before the tool change fills entry +8 (1016+0x04), +0xc (1016+0x60) and +0x10 (1016+0x64)
  [C:1253-1263]. **Entry +0x10 is used as an operation id** for `DatabaseRetrieve` [C:231-232]. The meaning of
  +0x04 and +0x60 is **NOT DETERMINED**.
- `FUN_1800cfe30` [M:29-141] computes a byte flag at entry +0x14 ("this pass has the same 1016 values and the same
  XY sequence as the previous pass"), gated by DAT_181376eb6. **rev_core never reads it.**
- **Passes stay in their original order. Each pass is reversed in place, inside its own index range** [C:222-1160].

### 3.4 Per pass p, with entry {S, E}
a. **Op link parameters**: `DatabaseRetrieve(opid = entry+0x10)` into `ent`. `haveOp` = success. `LP` = ent+0x160
   (op+0xf8) [C:231-232, 328]. The `LP` fields are:
   | field | `LP` offset | op offset | flags |
   |---|---|---|---|
   | clearance | +0x10 | op+0x108 | on +0x19, incremental +0x18 |
   | retract | +0x20 | op+0x118 | on +0x29, incremental +0x28 |
   | feed plane | +0x30 | op+0x128 | incremental +0x38 |
   | top of stock | +0x188 | op+0x280 | incremental +0x190 |

   Names come from `formats/opparams/param_reference.tsv` (10020-10028, 15100, 15101, 15374) and
   `opmanager_menu.md:124-129`.
b. **Tail** (`tail_xy(src,n,E)`) [C:233-240; H:1-159]:
   1. T = the 1002 at or before E. Its point (+0x50) is `ref`. Z is ignored in all comparisons here.
   2. If a 1011 exists at or after E, the scan starts just before it.
   3. Walk back over G0 records that are not vertical (is_vert false) while XY == `ref` XY. After each step,
      `ref` = point of prev_rec.
   4. The run found is the **tail block** (count, first index). If it is not empty, **E = tail first index - 1**.

   Original quirk: a transparent record at the scan position makes the walk re-test the same index forever
   (0x1800cf3ac..0x1800cf3b7 jumps back to 0x1800cf294 without a decrement).
c. Abort if E <= S. Otherwise `A0` = point(S) ("old start") and `B0` = point(E) ("old end"). If either is missing,
   abort [C:242-252]. If `dst[E]` is code 100, save its +0x58, +0x60, +0x68 [C:253-258].
d. **Approach block** = `get_block(src,n,S,E,fwd)` [C:269-270; H:160-303]:
   1. Find the 1002 at or before S.
   2. Skip marker(-3) G0s and transparent records after it.
   3. Then take consecutive records while each is transparent, or is G0/G1 and is_vert. The first record after a
      skipped marker is taken without the is_vert test.
   4. If is_vert reports `valid=0`, the block is empty.

   The block runs from `outA` with count `nA`. `feedA` = the feed of the last G1 in the block, else -2.0.

   **Retract block** = `get_block(src,n,E,S+nA,back)` [C:272-273; H:304-406]: walk back from E with the same test,
   using marker(-4). It runs from `outR` with count `nR`. `feedR` = the last G1 feed in the block, else -2.0.

   `zA` = Z of `dst[S+max(nA-1,0)]` (bottom of the old approach). `zB` = Z of `dst[E-nR]` (last cut point before the
   old retract) [C:274-283].
e. **Approach rewrite** (stays first; written to `dst[outA..]` in the original order) [C:284-401]. For each record
   that is not transparent, not marker(-3), and has a point:
   - XY: if XY == B0 XY, then XY := A0 XY. Else if XY == A0 XY, then XY := B0 XY. Otherwise XY is unchanged.
   - Z: if `haveOp`, `clearance_z(LP, a=zB, b=zA, &z)`. Else if `useMaxZ`, `z = max(z, zB)`. Else Z is unchanged.

   **Retract rewrite** (stays last; written to `dst[outR..]`) [C:403-519]: the same rule with marker(-4), using
   `clearance_z(LP, a=zA, b=zB)` or `max(z, zA)`. So **plunges and retracts are not reversed. The approach stack is
   moved to the new start XY and the retract stack to the new end XY. Their Z is re-based to the depth at the
   other end.**

   `clearance_z(LP,a,b,z)` [K:2143-2174] works as follows:
   1. If clearance is on, absolute, and |z-clearance| < tol: z := max(z, a).
   2. Else, if z is within tol of the absolute retract plane (retract on), the absolute feed plane, or the absolute
      top of stock: z := max(z, a).
   3. Otherwise: z := max(z - b + a, a).
f. **Tool change**: the 1002 at or before the body start gets first position X,Y (+0x50, +0x58) := B0 XY. Z is
   unchanged [C:521-530]. If there is none, abort.
g. **Body** = `src[S+nA .. E-nR]`. It is written to `dst[S+nA ..]` in **reverse index order**: all records in the
   range, motion or not, are copied whole [C:690-835]. Each motion, non-transparent record also gets these changes
   (non-drill only):
   - G2 <-> G3 [C:708-711]. The centre (+0x38) and every other field, including feed, are untouched.
   - New end point := point of `prev_rec(src, idx, min = that 1002 index, motionOnly)`, i.e. the move's own old
     start point [C:712-721, 770-774]. The previous record is located in `src`, so it is in original order.
   - If the previous record is an arc with plane (+0x18) != 0: its point goes
     `plane_to_view(plane,toolView)` -> `view_to_world` -> + O -> `world_to_view(toolView)` [C:722-741].
   - If this record is an arc with plane != 0: the point goes `view_to_world(toolView)` -> + O ->
     `world_to_view(plane_to_view(plane,toolView))` [C:748-769].
   - Abort if there is no previous motion record, the point is missing, or `pt_ptr` returns NULL. That last case
     means codes 20-23, 30-33, 82 and 83 in a body abort the reverse.
   - The first G0 in a run checks that some non-G0 record exists at or before it; otherwise abort [C:698-707].

   **Feeds stay attached to their own move** (records are copied whole and only the point changes).
h. **Tail rewrite** (non-drill) [C:839-932]: the tail records are written back to their original slots in
   **reverse order**. For tail record k-1 (k = nT..1) the code takes `prev_rec(tail, nT, k, 0, any)`, which is index
   k-1, i.e. **the record itself**: every tail record keeps its own point, except the last one written (the old first
   tail record), which gets B0 (xyz). (Corrected 2026-10-15; the earlier text said "the point of its predecessor".)
i. **Contour flags** over `dst[S..E_orig]` [C:933-963]:
   - G0/G1 i16 at +0x40 and G2/G3 i16 at +0x58: the value 1 <-> 2. Other values, such as 0x4000/0x8000, are left.
   - Records -4 <-> -5 and -6 <-> -7 (`swap_neg` [K:2289-2313]).

   In the template NCI the -2/-4/-6 and -7/-5/-3 records lie outside [S..E], so this swap does not reach them there.
j. **Rapid plunge -> feed** [C:964-999]: if `feedA` <= tol, `feedA` := 2.5 (metric) or 0.1 (inch). Every G0 in
   [S..E] that is a downward vertical (is_vert, valid, !isUp, feed <= tol) with z < REF.z becomes **G1, feed
   feedA**.
k. **Feed retract** [C:1000-1026]: only if `feedR` <= tol (the retract block had no G1). Every G1 in [S..E] that is
   an upward vertical with feed >= 0 is changed:
   - if the opcode is 1 (Contour), 3 (Pocket) or 0x85, and op+0x141 (rapid retract) == 0: feed := `feedR` (that is,
     -2.0 or another value <= tol);
   - otherwise: **G0, feed -2.0**.
l. **Links after the pass** (records E_orig+1 .. next S-1, or to the end) [C:1027-1079], for G0/G1 only:
   1. Clear byte +0xaa.
   2. If XY == B0: XY := A0.
   3. Else if XY != A0: set +0xaa = 1.
   4. Else (XY == A0): XY := B0.
m. **First record**: if `dst[S]` is G1 with i16 +0x40 == 0, z >= REF.z, and its prev_rec is a 1002 whose point
   equals `dst[S]` (3D, within tol), it becomes **G0, feed -2.0** [C:1080-1104].
n. **Links before the pass** (p > 0; from S-1 down to the previous pass end, exclusive) [C:1105-1153]: each G0/G1
   with +0xaa set gets the same B0/A0 XY swap as in step l and its flag cleared; the rest stay flagged. Byte +0xaa
   of G0/G1 is a scratch flag that stays in the output. Its meaning is **NOT DETERMINED**.

### 3.5 Final rapid-height pass (`post_fix`) [C:1162-1166, 1456-1555; ins_fn C:1556-1933]
`find_passes` runs again on `dst`. If the pass count changed, nothing more is done. Otherwise
`adjust_z_in_bnci_rapid_line(idx, v)` runs:
- with v = maxZ[p], for every record of pass p;
- with v = maxZ[p], for every record between pass p and the next.

The function does nothing if the record is not G0/G1, or if the next record is neither G0/G1 nor 1011. Otherwise,
if z < v:
- **G0**: z := v (0x18012faa6..0x18012fab1).
- **G1** followed by a G0 or a 1011: a new **G0 at the same XY, z=v, feed -2.0, +0x10 = -1** is inserted after it,
  unless the next point is within tol in 3D. The array grows and the pass ranges are shifted. The G1's own Z is
  unchanged. A 1011 next record: the code first takes `pt_ptr` of it, which is NULL for 1011, so it raises
  `_MCAssert` and inserts **nothing** (the `2*tol` distance for a 1011 is never reached). (Corrected 2026-10-15; the
  earlier text said a 1011 always gets the insert.)
- In the first loop (records of a pass) an insert shifts the pass's end and the later passes only when the inserted
  index lies inside some pass range p..; an insert just after the pass end shifts nothing. The second loop (records
  between pass p and the next pass start, inclusive, or to the last record) never shifts anything [C:1489-1547].

Result: every rapid in a pass that is lower than that pass's highest upward vertical rapid is raised to it. This
includes a rapid down to the feed plane, unless step j turned it into a G1. It follows literally from the code and
is unverified.

`*ok = 1` [C:1166].

## 4. Drill / point operations
If an 81 record exists between the 1002 and the body end [C:531-547]:
- The tail block is discarded. No point is rewritten in the body loop; the records are only reordered
  [C:695, 839].
- If the 81 is not the last record of the body [C:549-688]:
  - The 81 stays first. Its X,Y := X,Y of the last point. Its +0x88 and +0x90 (16 bytes, `movups` at 0x1800d131a;
    +0x98 is not written) := the saved +0x58 / +0x60 of the last 100. When the pass end is not a 100 these saved
    values are uninitialised stack: **NOT DETERMINED** (corrected 2026-10-15; the earlier text said +0x88..+0x98).
  - The last 100 stays last. Its X,Y := the 81's X,Y (from S). Its +0x58..+0x68 := the 81's original +0x88..+0x98.
  - The records between them are reversed.
  - Net effect: **the hole order is reversed**.
  - Each record's Z (depth) and its other fields stay with the record. In particular, the R plane (81 +0x68, 100
    +0x38) and the feeds are **not** exchanged.
  - The meaning of 81 +0x88..+0x98 and 100 +0x58..+0x68: **NOT DETERMINED**.
- A single 81 (one hole): the body is re-copied in reverse order without point changes, so only the order of the
  records between the approach and the 81 changes (an 82 before the 81 moves after it; section 7).
- Steps 3.4.i-n and 3.5 still run.

## 5. Refused, skipped and aborted cases
| case | effect |
|---|---|
| multiaxis op | silently skipped [R:198-201] |
| subprogram op | warning 2911, skipped [R:202-207] |
| `GetProdEnableBits()&6` | nothing happens [R:176-179; C:137-138] |
| `GetProdEnableBits()&0x10` | nothing happens [D:1007] |

The reverse aborts, and the op stays unchanged, in these cases:
- the comp starts and ends are unbalanced [C:217-220];
- E <= S, or the point at S or E is missing [C:242-252];
- there is no 1002 before the body [C:525];
- a body motion record has no previous motion record, or no point [C:716-721];
- a tail point is missing [C:860-863];
- `bnci_read_section` or `bnci_rewrite_section` fails [R:277-292].

## 6. NOT DETERMINED / open
- Meaning of codes -4..-7 and 1040/1041, of G0 feed -3/-4, and of `GetProdEnableBits` bits 2, 4 and 0x10.
- Meaning of DAT_181376eb6, of the draw_bnci flags, and of 1016 +0x04/+0x60.
- Whether the cc values 41/42/140 appear only on the comp start and end moves in real data. The sample templates
  only contain cc=0.
- Whether the outcome matches Mastercam. In particular: the 1013/1016 comp toggle is lost because those records sit
  outside the copied-back ranges (step 3.2.1); step 3.5 raises rapids; step 3.4.k assigns feed -2 to a G1.
- `op_is_subpgm` case 3 (Pocket) reads DAT_181378da2 = `cur_op` + 0x1392 of the previously current operation (section 1).
- The meaning of 81 +0x88..+0x98 / 100 +0x58..+0x68, and the 81's +0x88 / +0x90 when the pass does not end in a 100.
- The plane_to_view / view_to_world / world_to_view transforms of arcs off the XY plane (step 3.4.g): the viewer leaves
  such an op unchanged. No stored op in the user's parts has an arc with plane != 0.

## 7. Write-back, undo, and the viewer's implementation (2026-10-15)

**bnci_rewrite_section** as bnci_reverse calls it (param_6 = the 1018/1019 count = 0 with p5 = 1, param_7 = 0) [N]:
the records are written **in place** from the section start (after the -2 record), each with +0x04 := the op id.
Before each record the code checks `eos_position < position + rec[+0x14]` (+0x14 = the record's byte count, u16 size
+ 2 in the part file) and stops there: **records that no longer fit before the old -3 record are lost**. Only step 3.5
grows the array (and p5 drops 1018/1019 records, which shrinks it). Records are written with their stored size, so
G0 / G1 keep only bytes 0..0x47: the scratch byte +0xaa (steps l / n) never reaches the file.

**Undo**: `ReversePath` [D:994-1032] pushes no undo record (it only calls `list_op_restore_bnci`, `bnci_reverse`,
`Refresh`). The spec has nothing else on undo; reversing again is the only way back, and it is not exact (below).

**Viewer** (`viewer/nci_reverse.c`, `nci_reverse_op` / `nci_rev_core`; UI `reverse_selected` in mcamui.c, menu item
"Reverse toolpath"; `cam_cli PART reverse N[,N..] [--post F] [--save F]`; script hook `reverse:N`):
- a port of sections 1-5 on the stored NCI of a Mastercam operation; the rewritten section (with the in-place byte
  budget above) replaces the op's section for the post, the backplot and the toolpath display, the OP_ID record
  gets +0x148 = 1 / +0xfc = 0, and saving the part (mcam_save_part) writes that section and those bytes. The project
  file records `REVERSED <id> <count>` and replays it on open (same NCI -> same result). `mark_op_dependents` is not
  modelled. tol = 5e-5 (system tolerance, as elsewhere in the viewer); IsMetric() = the part's NCI units.
- operations made or edited in the viewer have no NCI (their moves are regenerated from parameters): left unchanged
  with a message - Mastercam reverses NCI only.
- Code map (viewer/nci_reverse.c / .h; GetProdEnableBits() taken as 0, its bit paths not taken): op class tests
  op_is_msurf / op_is_xform / op_is_multiax / op_is_subpgm [S]; record helpers is_transparent / is_motion / is_marker
  [K], get_pt [N], pt_off (pt_ptr) [K], find_line [N], prev_rec [K], is_vert [H], cc_off [M], is_pass_end [M],
  bnci_toggle_comp [K]; `find_passes` / `pass_max_z` / `comp_pairs_swap` (3.3) [C, H, M]; `tail_xy` / `get_block` /
  `clearance_z` / `max_z` (3.4) [H, K]; `ins_fn` / `post_fix` (3.5) [C]; `nci_rev_core` = rev_core (section 3);
  `find_section` (the -2 .. -3 record range of one op) and `nci_reverse_op` = bnci_reverse (section 1, write-back).
  Return values: 1 reversed, 0 refused / failed with a message (subprogram: NCI_REV_SUBPGM_MSG = mccoreres 2911),
  -1 skipped silently (multiaxis).
- Mastercam's endless loop in tail_xy (a transparent record at the scan position) is a stop here.

**Results on the user's parts** (all 189 stored ops of the 15 parts that have ops, each reversed once; session
tests with cam_cli `reverse` + an NCI comparison script. The result files and that script were scratch-only and are
not kept (they are not in `decomp/reverse/` or `tools/`); rerun cam_cli `reverse` to reproduce):
- every op reversed (none refused or aborted). In 174 of 189 the cutting moves (G1 / G2 / G3 that change XY) of every
  pass are exactly the original ones in reverse order, with swapped ends and the same arc centres. The other 15 have
  passes split by mid-pass -7/-6 markers (is_pass_end ends the pass there, so only the first part is reversed) or
  passes whose tail (3.4.b) pulls E past the -7/-5/-4/-6 markers and the retract.
- 3.4.b in practice: in multi-depth contours each depth has its own 1002 whose first position is the pass start;
  the link rapid back to that XY is the "tail", so E moves to just before it and the end markers and the retract
  rapid become part of the reversed body.
- 3.4.e: the approach Z is rebased when zA != zB (call site 0x1800d0f07: xmm1 = a = zB from E - nR, xmm2 = b = zA from
  S + max(nA-1, 0), checked in the disassembly). A pocket with a helix entry: the approach rapid at Z10 (incremental
  retract, clearance off) becomes Z8.5 and then a G1 (step j). A single-hole drill (tramming-rod op 3, clearance and
  retract off): the approach rapid at Z25 is rebased to the hole depth Z-26 and becomes a G1 at 2.5 mm/min (step j).
- 3.4.j: an approach without a G1 plunges at feedA = 2.5 mm/min (metric) - e.g. BRACKET4's ramped contour.
- section 4, one hole: the body (82, 81 in the user's drill op) is re-copied in reverse order, so an 82 record and the
  81 swap. A second reverse then aborts (B0 = the 82, which has no point), and the op stays as it is.
- 3.5: every lower rapid after an upward vertical rapid is raised (A001: the "G0 Z8." retracts become Z10).
- Reversing twice gives the original program back in no part exactly: the raised rapids stay, approach moves changed
  by 3.4.e / 3.4.j / step m and passes restructured by 3.4.b do not come back.
- one op (buckle-v8 op 12, id 41) grew by inserted rapids and lost its last 2 records (-7, -5) to the in-place budget.
- None of the user's parts has an op Mastercam itself reversed (OP_ID +0x148 = 0 in all of them), so nothing could be
  compared with Mastercam's own result.
