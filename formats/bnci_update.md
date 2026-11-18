# BnciUpdate - the NCI patch of an edited operation (user decision D2, 2026-10-26)

Code: `viewer/bnci_update.{h,c}` (bnciu_patch = BnciUpdate::Update's path for mill ops; bnciu_apply = the app glue),
`viewer/core_ui/cui_ops.c` pp_ok_bnciu (the dialog's OK), test driver `tools/tests/bnciu_test.{c,sh}`.
Decompiles: `decomp/dirty/core_bnciupd.c` (all BnciUpdate methods), `decomp/ncidrill/core_brw.c` / `core_brw2.c` (CBnciReadWrite
FindGcode / IsZmove / IsAlongVectorMove / GetValidPreviousMove / ReadSection / RewriteLine, bnci_get_pt; Ghidra 2026-10-26).

## Ported
On OK of a stored Mastercam op whose dialog changed only fields DontAllow lets through (bnci_mask; same tool): the op's OP_ID record
takes the dialog's feed / plunge / retract rate / spindle speed / coolant / D offset (+ record patches inside the masked ranges) and its
section is patched (mcam_op_set_nci); the toolpath is not regenerated. Refused (as before: regenerated / dirty): the High Speed feed
records (opcodes 0x84 / 0x86: ChangeFeedHmm / BackHmm2d), transform ops, 5-axis moves (IsAlongVectorMove), lathe / wire, tool number
or tool changes (the tool-system update after ProcessNci, this+0x344), home position / misc values / comments / canned text (batch 11
covers misc values and canned text alone), a drill subprogram, any other difference (DontAllow's masked memcmp).

## Tests (2026-10-26)
- `bnciu_test` (feed 777 / plunge 111 / retract 222 / rpm 4321) on BRACKET4 ops 8 / 18, buckle-v8 41, thread-test 2, test-cube-10mm 2,
  posted with Generic Fanuc 3X Mill.pst: every motion / other line equal, only F / S words change (and the post's own S..M3 lines when
  the next op on the same tool keeps its old speed).
- App (scene: BRACKET4 op 6 "top-holes", Tool page feed 777 / speed 4321 / plunge 111, OK, post): "Operation 6 updated: 144 toolpath
  record(s) patched"; LinuxCNC post and the .pst post: 0 lines differ beyond F / S / N (plunges F111, cuts F777, S4321).
- No Mastercam oracle (a part saved by Mastercam before / after a feed edit) - the user could make one on Windows.

## The decoded rules (spec, with core_bnciupd.c line numbers)
### 0. Call chain and return codes

- EO l.2577: `bnci_update(&old_copy /*iStack_10528 = copy of the op before the dialog, EO l.612-660*/, &cur_op /*edited*/, 1)`.
  So **param_1 = OLD op, param_2 = NEW op** everywhere below. False -> op gets dirty (audit section 19.1).
- BU l.47-84: only if old id == new id; lathe misc / mill-turn misc "no tool" ops -> false. Lathe ops first run
  `UpdatePossible` with CalcMode 0 and give up if feed/rpm changed or code != 3. Everything else (mill):
  `BnciUpdate(CalcMode=1)` + `Update(old,new)`; result = (code != 0).
- `Update` B l.7473-7503: memcmp(old,new,0x28d0)==0 -> 2 (nothing to do = success). SetProductType(new opcode);
  `UpdatePossible` -> if 3: SetGcodeMask, CollectOpIDs (<1 -> 0), ProcessNci (always returns true, B l.6606) -> 1 + db_has_changed.
  Other codes returned as is (0 = refused, 2 = nothing patchable changed).
- `UpdatePossible` B l.7537-7673: memcmp equal -> 2. Copies **old -> this+0x5518**, **new -> this+0x2c48** (0x28d0 each,
  B l.7559-7645). `chg = SetChangeFlags()`; `*out = SomeFeedInfoChanged || SomeRpmInfoChanged`; `DontAllow()` -> 0;
  else `3 - !chg` (3 if anything relevant changed, else 2).
- All BnciUpdate constructions found use CalcMode 1 (BU l.71, dirty/core_tooldirty.c l.708, b12/acp2.c l.1919); CalcMode 0
  only in BU l.62 (lathe pre-check, no ProcessNci). => for the port, **mode is always 1 = "replace when equal to old"**;
  the ratio ("mode 0") paths are dead code for us (documented below only for completeness).

### 1. Object layout (this+...)

| this+ | meaning | source |
|---|---|---|
| 0x2c48 .. +0x28d0 | NEW operation copy (op+X at this+0x2c48+X) | B l.7604-7645 |
| 0x5518 .. +0x28d0 | OLD operation copy (op+X at this+0x5518+X) | B l.7562-7603 |
| 0x7de8 .. +0x28d0 | copy of the *source* op of the 1016 being processed (op id at +0x7de8, opcode short at +0x7dec) | B l.6235-6276 |
| 0x1b8 | CalcMode (ctor arg) | B l.1402 |
| 0x1c0 | IDArray gcode mask passed to ReadSection | B l.6211, SetGcodeMask l.7329 |
| 0x228/0x230/0x238 | IDArray of op ids to process (CollectOpIDs, starts with old op id this+0x5518) | B l.4211-4216, ProcessNci l.6172-6175 |
| 0x290/0x298/0x2a0 | ops sharing the tool (list_op_by_toolID(old op+0x544)) when tool # / tool id changed | B l.4229-4242 |
| 0x2f8 | CCNCMachine* (OMgetOpMachine(old op id)) | B l.7282-7283 |
| 0x300 / 0x301 | product = lathe / wire (SetProductType: op_is_lathe / op_is_wire) | B l.7387-7396 |
| 0x302 | "only header ints changed" (set when every feed/rpm/dwell/misc/comment/canned flag is clear) | B l.7291-7307 |
| 0x308 | min arc feed: MinArcFeedRate if machine && AdjustArcFeedrates(1), else -1.0 | B l.7282-7290 |
| 0x344 | (int) gate for the post-loop tool-system update; set in CollectOpIDs (this+0x310 block, B l.4272-4274) - NOT DECODED |
| 0xa6b8 | update log (UpdateLog collects file positions) | B l.7516-7522 |

Change records are heap structs pointed to by this+N (ctor). Layouts read from SetChangeFlags:
- **D (double)**: `{double old @0; double new @8; bool changed @0x10; int mode @0x18; double ratio @0x20}`.
  Rule (B l.6697-6722, same block repeated): `changed = (old != new)` (exact). mode = this+0x1b8 (CalcMode);
  if changed and (old==0 || new==0) mode = 1; ratio = 1.0, or new/old when changed and both non-zero.
- **I (int)**: `{int old @0; int new @4; bool changed @8}`, changed = old != new (exact). (B l.6968-7007)
- **S (short)**: `{short old @0; short new @2; bool changed @4}` (coolant, B l.6989-6993).
- **R (int with ratio)**: `{int old @0; int new @4; bool changed @8; int mode @0xc; double ratio @0x10}`, same rule as D,
  ratio = (double)new/(double)old (B l.7009-7027).
- Arrays / strings / P3D: `{old, new, bool changed}` built with CompareArray / CString compare (B l.7034-7279).

| this+ | type | operation offset (old this+0x5518+X, new this+0x2c48+X) | condition | line |
|---|---|---|---|---|
| 0xf8 | D | **+0x320 retract feed** | only if !wire | 6697-6721 |
| 0x108 | D | **+0x318 plunge feed** | only if !wire | 6722-6746 |
| 0x118 | D | **+0x310 feed rate** | always | 6748-6771 |
| 0x128/0x138/0x158/0x148 | D | +0x1340 / +0x1428 / +0x14a0 / +0x1400 | old opcode == 0x84 | 6772-6867 |
| 0x198 | D (from ints) | +0x1510 (int) | old opcode == 0x84 | 6868-6892 |
| 0x1a8 | D | +0x1408 (dwell) | old opcode == 0x84 | 6893-6917 |
| 0x168 / 0x178 | D | +0x11e0 / +0x1768 | old opcode == 0x86 (2D HST) | 6918-6967 |
| 0x8 | I | +0x2e0 tool number | | 6968-6972 |
| 0x18 | I | +0x544 tool id/slot (1016 +0x60) | | 6973-6977 |
| 0x28 | I | **+0x308 D (dia) offset** | | 6978-6982 |
| 0x38 | I | **+0x30c H (length) offset** | | 6983-6987 |
| 0x48 | S | **+0x330 coolant** | | 6988-6993 |
| 0x58 / 0x68 / 0x78 | I | +0xf8 program # / +0xfc seq start / +0x100 seq inc | | 6994-7007 |
| 0x188 | R | **+0x328 spindle rpm (int)** | only if !wire | 7008-7028 |
| 0x88 | I | +0x53c station (1016 +0x58) | | 7029-7033 |
| 0x98 | P3D | +0x870 home position | | 7034-7046 |
| 0xa8 | double[10] | +0x7b0 misc reals | | 7047-7112 |
| 0xb8 | long[10] | +0x784 misc ints | | 7113-7166 |
| 0xc8 | string | +6 op comment | | 7167-7192 |
| 0xd8 | string | +0x336 tool comment | | 7194-7219 |
| 0xe8 | short[20] | +0xaea canned text | | 7221-7279 |

Notes: the "op" offsets match formats/nci_writer.md's 1002 / 1016 maps (1002 +0x18/+0x1c/+0x20 = op +0xf8/+0xfc/+0x100,
+0x24 = +0x2e0, +0x28 = +0x308, +0x2c = +0x30c, +0x38 = +0x328, +0x40 = +0x310, +0x48 = +0x330).
There is **no separate "traverse/rapid feed" field**: "Traverse" in the code is the main feed rate (+0x310); G0 records
(feed -2) are never touched (sec. 3). SetChangeFlags' result (B l.7292-7322) = any of the flags above changed
(0x158 and 0x178 are only in SomeFeedInfoChanged, l.7417-7432).

Predicates: SomeFeedInfoChanged = any of 0xf8,0x108,0x118,0x128,0x138,0x148,0x158,0x168,0x178 changed (B l.7417-7432);
SomeRpmInfoChanged = 0x188 || 0x198 (l.7439-7447); SomeDwellInfoChanged = 0x1a8 (l.7405-7410, 0x84 only);
SomeToolInfoChanged = 0x8 || 0x28 || 0x38 || 0x48 || 0x88 (l.7454-7465).

### 2. SetGcodeMask (B l.7329-7380) - which records ReadSection loads

Indices used everywhere are indices into this **filtered** list, not file records.
- this+0x302 == 0: {0,1,2,3,4,0xb,0x14..0x17,0x1e..0x21,0x51,0x52,0x53,100,1000,1001,1002,1008,1011,1012,1014,1016,1025,20001,20004}.
- this+0x302 == 1: {1000,1001,1002,1014,1016,20004}.

### 3. ProcessNci (B l.6078-6606)

```
for each id in this->ops (this+0x230 .. 0x238):                         // l.6172-6175, 6371
  op = GetOperationPtr(id); if !op continue
  SetProductType(op->opcode)                                            // l.6180
  n = ReadSection(op->id, mask)                                         // l.6181-6183
  if n <= 0 continue
  memset(this+0x7de8, 0, 0x28d0)
  i = 0; pos = -1
  while (i < n):                                                        // l.6190, 6366
    pos = FindGcode(1016, pos, forward)       // first index > pos (RW FindGcode 1800f6190: starts at pos+1)
    if pos < 0 || !rec || rec.gcode != 1016: break                      // l.6202-6205
    src = op_is_xform(op->opcode) ? GetSourceOperation(rec) : op        // l.6206-6209
    if !src: assert; pos++; i++; continue
    if src->id not in this->ops: pos++; i++; continue                   // l.6214-6222, 6361
    copy *src -> this+0x7de8                                            // l.6223-6264
    -- header records, in this order, each with start index pos:        // l.6265-6280
    ChangeToolInfoNo, ChangeToolNo, ChangeToolID(pos-1), ChangeDiaOffset, ChangeLenOffset, ChangeCoolant,
    ChangeProgNo, ChangeSeqNo, ChangeSeqInc, ChangeStationNo, ChangeHomePos, ChangeMiscReals, ChangeMiscInts,
    ChangeOpComment, ChangeToolComment, ChangeCannedText
    lim = Get1016Limit(pos)   // FindGcode(1014, pos, fwd); none -> n; lim <= pos -> -1   (l.4994-5014)
    if lim < pos: break                                                 // l.6282, 6304-6305
    if feedChg || rpmChg || dwellChg:                                   // l.6283-6299
      for k = pos+1 .. lim-1:
        if feedChg  ChangeFeed(k)
        if rpmChg   ChangeRpm(k)
        if dwellChg ChangeDwell(k)
      i = max(pos+1, lim)          // the loop variable is reused as i
    else pos++
    i++
```
Record range = strictly between the 1016 and the next 1014 (next sof) or the section end. SOF order (nci_writer.md sec. 3):
1011,1012,1013,1014,1016,1017,1025,...,20001..,1002(first), moves. So the motion records and every 1000/1001/1002 after the
1016 are visited.

Every patch writes the 0x370-byte record back in place: `RewriteNciLine` (B l.6610-6643) -> `CBnciReadWrite::RewriteLine(rec,1)`
(RW 1800f9620): seek to rec+0x10 (file position), re-read the record and require same gcode (or both tool-change gcodes) and same
size rec+0x14, then bnci_write_to_disk the edited image at the same position. Size never changes.

After all ops (l.6372-6545), if SomeToolInfoChanged && this+0x344: tool-number change also renumbers the TlAssembly
(l.6373-6455) and, for ops of this+0x298 **from index 1** (index 0 skipped, l.6372 `lVar13 = 1`; meaning NOT DECODED), copies the
changed new values into that op's ent image (op +0x2e0 tool#, +0x308 D, +0x30c H, +0x330 coolant, +0x53c station; ent image =
op + 0x68) and calls TpPartOpList::UpdateListAndDB if the 0x288 bytes from op +0x2e0 differ (l.6457-6531). Not needed for D2's
feeds/rpm.

### 4. Header Change* (record found = first of gcode G with index > start; product byte rec+0xc: 0 mill, 1 lathe, 2 wire)

| function | gate | record | mill field | rule (mill) | lines |
|---|---|---|---|---|---|
| ChangeToolNo | 0x8 | 1002 | +0x24 int | if rec == old && rec != new -> new | 4061-4105 |
| ChangeToolInfoNo | 0x8 | 20004 | +0x18 int | rec != old -> skip, else new | 4001-4053 |
| ChangeToolID | 0x18 | 1016 (start pos-1 -> the current 1016) | +0x60 int | == old -> new | 3949-3993 |
| **ChangeDiaOffset** | 0x28 | 1002 | **+0x28 int** | mill: **no old check**: if rec != new -> rec = new (lathe +0x28 not handled: only warns) | 2172-2224 |
| **ChangeLenOffset** | 0x38 | 1002 | **+0x2c int** | mill: **no old check**: if rec != new -> new (lathe: +0x28, requires == old) | 3001-3062 |
| **ChangeCoolant** | 0x48 | 1002 | **+0x48 short** | if rec == old && rec != new -> new (lathe +0x50; wire: none) | 2119-2165 |
| ChangeProgNo / SeqNo / SeqInc | 0x58 / 0x68 / 0x78 | 1002 | +0x18 / +0x1c / +0x20 int | == old && != new -> new | 3472-3515, 3792-3835, 3742-3785 |
| ChangeStationNo | 0x88 | the 1016 itself (GetPtrAt(pos)) | +0x58 int | == old -> new | 3842-3870 |
| ChangeHomePos | 0x98 | 1002 | +0x68 (mill) / +0x70 P3D | details not read | 2847-2995 |
| ChangeMiscReals / Ints | 0xa8 / 0xb8 | 1011 / 1012 searched **backwards** | +0x18 x10 | details not read | 3071-3385 |
| ChangeOpComment | 0xc8 | 1008 backwards | +0x18 string (strcpy0 0x79) | | 3391-3465 |
| ChangeToolComment | 0xd8 | 20001 | +0x18 string (0x78) | | 3878-3942 |
| ChangeCannedText | 0xe8 | 1025 | +0x18 short[20] | | 1949-2112 |

Only the **first** 1002 after the 1016 gets these header patches (FindGcode returns one index). For the in-range 1002s
the feed (+0x40) and rpm (+0x38) are handled by ChangeFeed / ChangeRpm (sec. 5, 6), D/H/coolant of later 1002s are not.

### 5. Feeds

### 5.1 GetFeedData (B l.5079-5956) -> VArray<FeedData>, element 0x20 bytes:
`{double* p @0 (into the record); double v @8 (value read); int type @0x10; int srcOpId @0x14 (this+0x7de8); int prevType @0x18 (0)}`.
An element is added only if `*p > 0.0` (so G0's -2 / 0 feeds are never collected).

| gcode | offset | initial type | line |
|---|---|---|---|
| 0 (rapid) | +0x38 | 1 | 5202-5210 |
| 1 (line) | +0x38 | 2 | 5234-5242 |
| 2, 3 (arcs) | +0x50 | 2 | 5266-5306 |
| 0xb (5-axis line) | +0x48 | 2 | 5330-5338 |
| 0x14 / 0x1e | +0x38 | 1 | 5362-5370, 5459-5467 |
| 0x15 / 0x1f | +0x38 | 2 | 5394-5402, 5491-5499 |
| 0x16, 0x17, 0x20, 0x21 | +0x48 | 2 | 5426-5435, 5523-5532 |
| 0x51 (81 drill cycle) | +0x40 | 2 | 5556-5564 |
| 100 | +0x50 | 2 | 5588-5596 |
| 0x386 | +0x70 type 9, then +0x68 type 10 (two elements) | | 5126-5176 |
| 0x399 / 0x39b / 0x39c / 0x39d | +0x28 / +0x20 / +0x38 / +0x20 | 0xb / 0xc / 0xd / 0xe | 5624-5743 |
| 1000, 1001, 1002 | mill (rec+0xc==0) **+0x40**; lathe (==1) +0x48 | 2 | 5767-5810 |
| other (incl. 4, 0x52, 0x53) | - | none | |

Reclassification, only if >= 1 element (l.5836-5951); "≈X" means `|v - X| < |tol|` (strict):
1. Source opcode (this+0x7dec) 0x84: element[0] type := 5/6/7 if 0x128/0x138/0x148 changed and v ≈ its old (l.5839-5856).
   Opcode 0x86: element[0] type := 8 if 0x168 changed and v ≈ old op+0x11e0 (l.5857-5864).
2. `IsZmove(k, &up)` true and gcode != 0 and != 100: for **every** element: if !up and plunge (0x108) changed and v ≈ old
   plunge -> type 4; if up and retract (0xf8) changed and v ≈ old retract -> type 3; else unchanged (l.5865-5891).
3. CalcMode == 1 (always for us) and source opcode in {3 (pocket), 9, 10, 0xf, 0x2c, 0x6b, 0x84, 0x86} and !lathe
   (for 0x86 further: skipped when op+0x11c0==0 && op+0x1654!=0 && element[0].v ≈ op+0x1600; this+0x8fa8/0x943c/0x93e8,
   l.5905-5909): with v = element[0].v and **old** op values (this+0x5518 copy, not the change flags):
   if v ≈ old feed (+0x310): keep. Else if v ≈ old plunge (+0x318) -> type 4; else ≈ old retract (+0x320) -> 3;
   else (0x84) ≈ +0x1340 -> 5; else (0x86) ≈ +0x11e0 -> 8; else (0x84) ≈ +0x1428 -> 6; else keep.
   On a change, element[0].prevType (+0x18) := the previous type (l.5892-5948).
   Contour (1), drill (2) and face are **not** in this list.

### 5.2 ChangeFeed (B l.2312-2359): dispatch on **element[0].type** only:
0, 2 -> ChangeFeedTraverse; 3 -> ChangeFeedRetract; 4 -> ChangeFeedPlunge; 5,6,7 -> ChangeFeedHmm; 8 -> ChangeFeedBackHmm2d;
1, 9..14 -> nothing (so G0 / 0x14 / 0x1e feeds and 0x386 / 0x39x records are never patched).

Common replace rule (mode 1) for Traverse / Retract / Plunge, per element e (loop over all elements):
`if (e.v == chg.old) *e.p = chg.new;` (**exact** double equality with the old op value), mode 0 would be `*e.p *= ratio`.
Then counted as changed if `|e.v - *e.p| >= |tol|`; the record is rewritten if >= 1 element counted.

- **ChangeFeedTraverse** (B l.2739-2837), gate 0x118 (feed +0x310) changed. No Z-move restriction (a Z move at feed is only
  logged "considered as traverse feed", l.2771-2783). For each counted element `CheckMinFeedRate(e.p)` (after the write,
  l.2800). So: **every gcode 1/2/3/0xb/0x15-0x17/0x1f-0x21/0x51/100/1000-1002 record whose feed == old feed becomes the new feed**,
  unless reclassified to 3/4 above.
- **ChangeFeedRetract** (B l.2635-2732), gate 0xf8 (+0x320), record gcode != 0. Requires: 5-axis move type (Get5axMoveType,
  l.5022-5047: gcode 0xb with rec+0x50 in 50..59 -> 3, 30..39 -> 4) 3/4 -> IsAlongVectorMove(k,&up), else IsZmove(k,&up); and
  `up == true`. Then the common rule against old retract; **no CheckMinFeedRate**.
  => a non-Z move reclassified to 3 by rule 3 (e.g. pocket) is **not** patched (IsZmove false -> -1).
- **ChangeFeedPlunge** (B l.2532-2628), gate 0x108 (+0x318), gcode != 0. 5-axis types 1/2 (rec+0x50 40..49 / 10..19) ->
  IsAlongVectorMove; else requires `IsZmove(k,&up) || element[0].prevType != 0`, and then `up == false`. IsZmove clears `up`
  first (RW 1800f7b80), so non-Z moves reclassified by rule 3 **are** patched (pocket / surface ramps at plunge rate).
  Common rule against old plunge; **no CheckMinFeedRate**.
- **ChangeFeedHmm** (B l.2443-2525, types 5/6/7 -> chg 0x128/0x138/0x148, 0x84 only) and **ChangeFeedBackHmm2d**
  (l.2366-2436, type 8, chg 0x168, 0x86 only): skip Z moves (IsZmove true -> -1); element[0] only;
  mode 1: replace with new only if `|v - old| < |tol|`; then if `|new - v| >= |tol|`: CheckMinFeedRate, write, rewrite.

### 5.3 CheckMinFeedRate (B l.4125-4137)
`m = this+0x308; if (tol <= m && *f < m) { *f = m; return true; }` - m is CCNCMachine::MinArcFeedRate when the op's machine
has AdjustArcFeedrates(1), else -1.0 (no clamp) (l.7282-7290). Applied to every patched element in Traverse, not only arcs.

### 5.4 IsZmove / IsAlongVectorMove (RW, outside the B file)
- IsZmove(k,&up) (1800f7b80): up=false. gcode 0/1: point P = bnci_get_pt(rec), Q = bnci_get_pt(GetValidPreviousMove(k,0,false));
  true iff `sqrt((Qx-Px)^2+(Qy-Py)^2) < tol`, with `up = Qz < Pz`. gcode 100: true, up=false. Arcs (2/3), 0xb etc.: no
  return statement in the decompile (presumably false - NOT DECODED). k <= 0 -> false.
- IsAlongVectorMove(k,&up) (1800f77d0): gcode 0xb records only; true iff the move direction is parallel to the tool axis
  (1-|dot| <= tol), up = dot < 0.

### 6. Spindle rpm

- GetRpmData (B l.5964-6020) -> RpmData `{int kind @0 (1 int, 2 double); int type @4; ptr @8; double v @0x10; int iv @0x18}`:
  gcode 1000/1001/1002 mill (rec+0xc==0): **+0x38 int**, kind 1, type 1; lathe: +0x40 int; 0x39c: +0x40 int, type 4;
  gcode 4: +0x20 double, kind 2, type 2 (type 3 if source opcode 0x84, 0x198 changed and v ≈ its old). Others: none.
- ChangeRpm (B l.3522-3559): type < 3 -> ChangeRpmOpToolInfo; type 3 -> ChangeRpmRampHmm; else nothing.
- **ChangeRpmOpToolInfo** (B l.3566-3648), gate 0x188 (+0x328) changed. Mode 1: kind 1 (int field): `if (rec_int == old) rec_int = new`
  (exact int compare); kind 2 (double): `if (|v - old| < |tol|) rec = (double)new`. Mode 0: `(int)(v*ratio)` / `v*ratio`.
  Rewritten whenever the mode-1 test passes (no "differs" test).
- ChangeRpmRampHmm (B l.3655-3735): same against 0x198 (0x84 only).

### 7. Dwell (0x84 only)
GetDwellData (B l.5056-5072): gcode 4, +0x18 double. ChangeDwell -> ChangeDwellHmm (B l.2231-2305): gate 0x1a8; mode 1:
`if |v - old| < |tol|: *p = new`; rewrite if `|*p - old| >= |tol|`. Not reachable for 2D mill ops (flag only set for 0x84).

### 8. DontAllow / refusals
Already decoded in formats/opdlg_port_audit.md 19.1 (DontAllow 0x180a5c530, B l.4500-4991): the masked fields are exactly the
ones of sec. 1; any other difference -> refuse (code 0 -> op dirty). Also refuses ops with dependents, subprogram, drill
+0x1298, no restorable NCI, a 2D HST min-feed case. Not re-read for this spec.

### 9. Port recipe for a plain 2D mill op (contour 1 / pocket 3 / drill 2 / face), mode 1

Per op section, for each 1016 (non-xform: its own op), records k in (1016, next 1014 or end), using old/new op values:
1. feed changed (old+0x310 != new+0x310 exactly):
   for gcode 1 (+0x38), 2/3 (+0x50), 0x51 (+0x40), 100 (+0x50), 1000/1001/1002 mill (+0x40) with value > 0 whose class is 2:
   if value == old feed (exact) -> new feed, then clamp up to MinArcFeedRate if enabled (else none).
2. plunge changed: a record is class 4 if (a) IsZmove down (XY dist to previous move < tol, z decreasing) and |v-old plunge| < tol,
   or (b) pocket (3) only: |v-old feed| >= tol and |v-old plunge| < tol (any move direction except up-Z).
   Then `if v == old plunge (exact) -> new plunge` (no min clamp). Gcode 0 and 100 excluded from (a); gcode 0 with v > 0 can
   still hit (b).
3. retract changed: class 3 if IsZmove up and |v-old retract| < tol (or pocket rule 3 by value) - but only patched when
   IsZmove up. `v == old retract` -> new retract.
   Precedence (pocket): rule (b) runs after (a) and only looks at element[0]: v ≈ old feed -> keeps (a)'s class; else
   ≈ old plunge -> 4 even for an up-Z move (which ChangeFeedPlunge then rejects: up == true), else ≈ old retract -> 3.
4. rpm changed: 1000-1002 mill +0x38 int == old -> new (all of them in range); gcode 4 +0x20 double ≈ old -> new.
5. D / H / coolant / prog / seq: first 1002 after the 1016 only: D +0x28 := new (unconditional if different), H +0x2c := new
   (same), coolant +0x48 short == old -> new, +0x18/+0x1c/+0x20 == old -> new. Tool #: 1002 +0x24 and 20004 +0x18 == old -> new;
   tool id 1016 +0x60; station 1016 +0x58.
6. Rewrite the record in place (same size).

### 10. External dependencies (not in core_bnciupd.c)

| item | address | needed for 2D mill? |
|---|---|---|
| CBnciReadWrite::FindGcode | 1800f6190 / 1800f6320 (RW) | yes - decoded: start+1 forward, skips null / size-0 records, **returns -1 at a record with op id (+4) == 0** |
| CBnciReadWrite::ReadSection (mask) | export 7523 | yes - loads the section's records whose gcode is in the mask; NOT DECODED (ordering assumed = file order) |
| CBnciReadWrite::IsZmove / IsAlongVectorMove | 1800f7b80 / 1800f77d0 (RW) | yes (plunge/retract) - decoded above |
| CBnciReadWrite::GetValidPreviousMove, bnci_get_pt | not dumped | yes (IsZmove's previous point) - NOT DECODED (assumed: previous motion record in the filtered list and its end point) |
| CBnciReadWrite::RewriteLine | 1800f9620 (RW) | yes - in-place rewrite, decoded above |
| `tol` | global | yes, 5e-5 |
| CCNCMachine::AdjustArcFeedrates / MinArcFeedRate, OMgetOpMachine | mcmachine | only if the machine def enables arc feed adjust - NOT DECODED (what our parts' machines return) |
| GetOperationPtr, op_is_xform, GetSourceOperation (B l.6028-6054: xform -> op of 1016 +0x64) | | non-xform ops: trivial |
| CollectOpIDs (B l.4144-4491) | | feed-only edits: list = [old op id] + xform ops built from it (l.4290-4373, partly read); tool-# edits pull in every op using the tool |
| op_is_lathe / op_is_wire (SetProductType) | | mill: both false |
| DontAllow | B l.4500 | yes (gate) - see audit 19.1 |
| Cnc::Tool::* / TpPartOpList::UpdateListAndDB | | tool-number edits only |

### 11. Ambiguities / caveats
- Old vs new: inferred from EO (iStack_10528 is copied from param_1 before the dialog at EO l.612-660, the dialog edits the
  global cur_op, and EO l.2577 passes (iStack_10528, &cur_op)); consistent with the log strings "old.. new.." and ratio new/old.
  Other callers (tpdlg_all.c l.5711 `bnci_update(&cur_op, local_2908)`) were not checked for order.
- IsZmove's return for gcodes other than 0/1/100 has no explicit return in the decompile (likely false).
- ReadSection / GetValidPreviousMove semantics NOT DECODED; this+0x344 (tool-system update gate) NOT DECODED.
- ProcessNci reuses the record counter as the outer bound (i jumps to lim+1); with one sof per section this simply ends the loop.
- Exact equality (`==`) is used for feed/plunge/retract replacement and integer rpm; tolerance (< tol) only for classification,
  Hmm types, and double rpm/dwell. A record whose feed was written rounded differently from the op value will not be patched.
- Mill D/H offsets are overwritten without checking the old value (lathe checks it) - as decompiled, l.2189-2208, 3036-3052.
- ChangeFeed dispatches on element[0] only; multi-element records exist only for gcode 0x386 (not a 2D mill record).
