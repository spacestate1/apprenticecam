# Writing new 2D operations into a .mcam / .mcx-9 file

Scope: what a writer has to emit or patch so Mastercam 2017 opens a copied part file containing an
added Contour/Pocket operation. This builds on `formats/mcam.md`. All offsets are **entity offsets** (from
the start of the ent, so the class-specific body starts at +0x68). "op+N" means the `operation` struct,
which is the ent body: op+N = ent+0x68+N.

Evidence:
- **Code**: `save_db` (`decomp/mccore_fileio.c`), `FUN_1802cf2a0` (ProcessAssocEntity), `FUN_1802cfb60`
  (ProcessAssocOperation), `FUN_1802d0900` (TP reader), `bnci_restore` (`decomp/mccore_dbread.c`), and new
  Ghidra dumps of `FUN_1802bbd60` (`rewrite_tp_hdr`), `FUN_1802bbf60`, `FUN_1802c72c0` (raw writer),
  `AddListAndInit`, `bnci_op_section_valid`, `GetDirtyOperationIDs` and `CountByNCIFlag`.
- **Data**: tramming-rod, BRACKET4, fight-stick, and the bundled `mill.mcam_8` and `lathe.mcam_8`.
  Scratch scripts used: `dump.py`, `hdr.py`, `chain.py`, `grp.py`, `nciseq.py` (session scratchpad, not kept).

---------------------------------------------------------------------------------------------------
## 1. Order in which save_db writes the file

Every record is written by `write_ent_to_disk` → `FUN_1802c72c0`. That function writes
`u32 len = bytes_per_ent(e,1)+bytes_per_ent(e,2)`, then the fixed part, then the variable tail. It also
stamps the version bytes ent+0x48/0x49/0x4b..0x4e. It **skips any ent whose eptr has bit 0 set**
(`(byte)ent[0x20] & 1`).

| # | record | written by | tramming-rod offset |
|---|---|---|---|
| 1 | DB 0x200/sub 8 header (version, code page) | `FUN_1802c5a00` | 0x0 |
| 2 | sub 0x12 thumbnail | `write_bitmap_info` | 0x9c |
| 3 | sub 0x11 description | `write_descr_info` | 0x40150 |
| 4 | sub 0x13 history | `FUN_1802c00c0` | 0x401dc |
| 5 | **sub 0x0d "tp_hdr"**, placeholder rewritten at the end (§7) | save_db (offset kept in `local_b4d8`) | 0x40bb0 |
| 6 | sub 0x17 | `FUN_180342bc0/cc0` | 0x40c34 |
| 7 | sub 0 view ents (referenced views), sub 1 graphics/config ent | `FUN_1802c8290/82f0` | 0x40da8, 0x40e64 |
| 8 | ASSOC 36 MastercamPlanes (ClassEnt) | `MastercamPlanes::WriteToDisk` | 0x40f48 |
| 9 | sub 0x19 worksheets, then sub 0x15 levels, then sub 0x14 converter data | | 0x430d3.. |
| 10 | sub 0x0f + `u32 n` + n × op_group (0x1ae0 + members, §2), then wire change points | `TpGrpList::WriteFileGroups`, `write_wchgpts` | 0x4373b |
| 11 | sub 0x0e + material table | save_db / `FUN_180302ad0` | 0x4a32f |
| 12 | every non-ASSOC DB ent in DB order (points/lines/arcs/splines/surfaces/SOLID_ID 0x10000 ...), mask 0x3f1fff | `get_raw_ent` loop → `FUN_1802bbf60` | 0x4a3c5.. |
| 13 | SOLIDXMT 0x20000 + Parasolid XT + ops history (here: the part's solids as stored; solids made here appended after them with our XT + history, formats/solid_history.md 8) | `sld_save_solids` | 0x4ab51 |
| 14 | **ASSOC 7 SETUP** (from `job_setup`), with sel bits 1 and 0x100 | save_db | 0x4d56a (= tp_hdr+0x74) |
| 15 | **all ASSOC 1 OP_ID**, in DB order, only ops present in TpMainOpList (counted → tp_hdr+0x70) | 1st `get_ent_assoc` pass | 0x4e6be.. |
| 16 | all other ASSOC in DB order (TL, M, MACHCOMP, ClassEnt 31/33/35, TP...); **TP_ID is written only if its op id exists** | 2nd `get_ent_assoc` pass | 0x58bae.. |
| 17 | if any op has NCI: ASSOC 5 NCI_ID (len 0xb2, body all zero) | save_db | 0x8a5b2 |
| 18 | binary NCI: per op, every record from its `-2` to its `-3`, copied from the temp NCI file; only for ops with op+0x80 ≥ 0 and (op+0x94 == 0 or op+0xe0 != 0) | `bnci_write_to_disk` | 0x8a668 (= tp_hdr+0x78) |
| 19 | seek back and rewrite tp_hdr (record 5) | `FUN_1802bbd60(file, pos, ngeo, nops, nctl, setup_pos, nci_pos, max_idn)` | |

There is a second branch (`local_b664 != 0`, save as an older version) that writes ASSOC 6 view ents
instead. It does not apply here.

### Ordering rules on read (what matters if records are inserted)
The reader is `FUN_1802d5c80` → `FUN_1802d3990` → `FUN_1802cf2a0` for ASSOC records.
- **An ASSOC ent is processed only if sel bit 1 is set** (`sel_bit_is_on(ent,1)`, u16 at ent+4). All
  ASSOC records in the samples have ent+4 = 0x0101, and save_db sets bits 1 and 0x100.
- **OP_ID must come before all of its TP_ID records.** Reading an OP record adds `{op id → eptr,
  expected TP count = ent+0xf0}` to map param+0x3b48. `FUN_1802d0900` looks up each TP's op (TP+0x68) in
  that map and increments a counter. If the op is not in the map yet, or the counter goes past ent+0xf0, the
  TP record is **not stored** and the op id is added to a bad-op list (param+0x3d10).
- **Each TP_CHN (kind 6) must be followed directly by its TP_BND (kind 7) records.** A kind-6 record is
  copied to param+0x1200. Each following BND gets TP+0xa0 = that chain header's new eptr. The BND with
  TP+0x9c == 1 (first element) triggers a rewrite of the header with header+0x80 = that BND's eptr
  (`rewrite_ent_assoc(param+0x1200)`).
- **Geometry referenced by TP+0x6c (ent_idn) must be read before the TP.** TP+0x80 is cleared and then
  resolved through the idn→eptr map (param+0x3b38). Normal files satisfy this because geometry precedes ASSOC.
- TL_ID order does not matter: case 3 only collects the tool (§8). The op carries its own copy of the tool, and
  the tool slot is checked later against NCI 1016 records.
- NCI_ID (ASSOC 5) is not stored. It only sets the "restore NCI" flag (param+0x10). NCI is located through
  tp_hdr+0x78, not through the NCI_ID record (§6).
- Observed TP order is plain DB order. BRACKET4 op 15's TP block sits after op 21's, and chains of op 7 are
  not in idn order. The reader does not care as long as the rules above hold.

---------------------------------------------------------------------------------------------------
## 2. How an operation is tied to everything else

### OP_ID record (ASSOC 1, len 0x2938) — linkage fields
| ent off | op off | type | meaning | evidence |
|---|---|---|---|---|
| 0x68 | 0x00 | i32 | **op id**: unique; must not already exist ("operation id # %ld already exists") | FUN_1802cfb60, AddListAndInit |
| 0x6c | 0x04 | u16 | opcode (1 Contour, 3 Pocket, 2 Canned Cycle, 0x86 2D HS) | |
| 0xe6 | 0x7e | i16 | **position in Toolpaths manager (1-based)**: tramming 1,3,4,2; BRACKET4 is a permutation of 1..16 | AddListAndInit stores it into op+0x7e |
| 0xe8 / 0xec | 0x80 / 0x84 | i32 | NCI section start/end. These are offsets in the session temp NCI file, **not** the part file. Forced to -1 on load | FUN_1802cfb60 sets ent+0xe8 = -1 (8 bytes) |
| 0xf0 | 0x88 | i32 | **number of TP_ID records owned by this op** (tramming 9/1/9/2; BRACKET4 op 15 = 20 = 1 CHN + 3 kind-30 + 16 BND) | map value checked in FUN_1802d0900 |
| 0xf4 / 0xf8 | 0x8c / 0x90 | i32 | number of chains (op 7: 4, op 9: 2, op 20: 3; drill/point ops 0). The second copy is usually equal (mill template op 3: 1/2) | data only |
| 0xfc | 0x94 | u8 | **dirty / NCI-invalid flag** (§4) | |
| 0x15a | 0xf2 | u8 | set to !bnci_op_section_valid on load; 1 in every sample | |
| 0x2f4 | 0x28c | i32 | **owning group id** (leaf toolpath group) | CheckOpsGroupAssignment, AddListAndInit |
| 0x348.. | 0x2e0.. | | embedded tool (tool# 0x348, dia 0x350, ...) | mcam.md |
| 0x5ac / 0x5a4 | | i32 | tool slot (set from 0x348 if 0) / -1 | FUN_1802cfb60 |
| 0x5bc | 0x554 | i32 | **machine group id** (root group). tramming/BRACKET4: 3; mill template: 1 | data: matches group tree in all files |
| 0x5d8 / 0x688 / 0x738 | | op_view | 3 planes (+0x68 i16 plane id, +8 matrix, +0x50 origin), resolved through GetDbPlaneIdMap; if missing, matched by matrix or a new plane is created | FUN_1802cfb60 |
| 0x1218 | 0x11b0 | f64 | op struct version (10.15/10.18 checks) | FUN_1802cfb60 |

For opcodes 9, 10, 15, 0x2e, 0x6b, 0x83 (pointer at ent+0x12f8) and 0x84 (ent+0x1570), the OP record has a
variable tail. Contour (1) and Pocket (3) have none, so their record is exactly 0x2938 bytes.

### What makes the op appear in the right group
Group membership is **only** op ent+0x2f4. The op_group member list (+0x110 count) is 0 in every sample.
- `AddListAndInit`: if `GroupByID(op+0x28c)` fails → "Bad group assignment", and the op goes to the active
  group or the first operation group.
- `CheckOpsGroupAssignment` (load, version ≥ 10): if the group exists but is a **machine group** (+0x10c ==
  0), Mastercam creates a new toolpath group under it and moves the op.
  → Set ent+0x2f4 = id of an existing toolpath group, ent+0x5bc = id of its root machine group, and
  ent+0xe6 = unique position.
- During file load AddListAndInit appends ops in **record order** (`in_file_get` short-circuit) and keeps
  the stored +0xe6. Display order follows +0xe6 (BRACKET4 records are in id order but +0xe6 is permuted).
  Safest: append the new OP record after the existing OP records and give it +0xe6 = N+1.

### op_group record (follows sub 0x0f: `u32 n`, then n × [0x1ae0 struct + members])
`WriteFileGroups` for the current version (major 0x12/0x13) writes the 0x1ae0 struct and then
**`count(+0x110)` member records of 0x17 bytes each** (from a 0x18-stride array at +0x118); `RetrieveFileGroups`
reads them back the same way (decomp/mccore_sections.c 1636-1639). The user's parts have count 0, but 16 of the 2017
corpus parts have members (e.g. AXIS SUBSTITUTION - ROLL THE GEOMETRY.mcam: 22): with a fixed 0x1ae0 stride every reader
lost the records after the block (the memory table, the geometry - such a part opened empty). All readers now walk the
members (viewer/mcam_grp.h `mcam_grp_block`, tools/mcam_walk.py; 2026-11-05).
| off | type | meaning (sample values) |
|---|---|---|
| 0x000 | i32 | group id (1,2,3,4...) |
| 0x004 | char[0x105] | name ("Machine Group-1", "Toolpath Group-2", "Feature 1 Zone 1-TOP") |
| 0x10c | i32 | parent group id (0 = machine/root group) |
| 0x110 | i32 | member count (0 in all samples) |
| 0x118 | ptr | member array (in memory only) |
| 0x128..0x12e | 4×u16 | 1,1,1,0 |
| 0x130/0x131/0x132 | u8 | 1,0, 0x20 or 0x01 (0x01 seen on "Toolpath Group-1"; meaning unknown) |
| 0x134 | i16 | type: 1 everywhere (machine group = type 1 and parent 0). 4 also exists (pattern groups) |
| 0x136 | u16 | color (replaced by main color if equal to the background) |
| 0x138 | i32 | level |
| 0x13c | i32 | 1 |
| 0x140 | i64 | **file offset of this group record** (WriteFileGroups stores GetPosition; the reader overwrites it with the read position) |
| 0x148 | char[] | operation defaults file (`...MILL_MM.defaults-9`) |
| 0x355 | char[] | machine definition (`%MCAMDIR%CNC_Machines\...mmd-9`; tokenized with PathToToken) |
| 0x45a | char[] | NCI output directory |
| 0x55f | char[] | control path (tokenized when byte +0x19e8 == 6) |
| 0x978 | ptr | CCNCMachine* (in memory) |
| 0x1150 | i32 | insert location (in memory, zeroed on save) |
| 0x11d0 | char[] | stock material name ("ALUMINUM mm - 2024") |
| 0x1272 | char[] | stock STL path |

A new op can reuse an existing toolpath group. A new group means appending a 0x1ae0 struct (clone a
"Toolpath Group-N" record, new id, +0x10c = machine group id) and bumping the `u32 n`.

---------------------------------------------------------------------------------------------------
## 3. Entity identity (+0x20 eptr, +0x30 time, +0x34 ent_idn)

- **ent_idn (+0x34)** is a file-wide unique id across geometry and ASSOC ents. System ents (0x200),
  SETUP, NCI_ID and ClassEnt 36 use 0.
  - Load: `cur_ent_idn` = max idn over the loaded ents (FUN_1802d5c80). New ids come from
    `get_next_ent_idn` = `++cur_ent_idn`.
  - Duplicates are logged ("Duplicate entity ID: #%ld") and **renumbered** after load (FUN_1802d1b60 via
    get_next_ent_idn). Records with idn 0 are given a new id. Renumbering would break TP→geometry links,
    so new ids must be unique.
  - **Rule: new idn = max(idn in file, tp_hdr+0x7c) + 1, 2, ...**, then update tp_hdr+0x7c.
- **Stored counter**: tp_hdr (DB sub 0x0d) +0x7c = max ent_idn at save time (tramming 173, BRACKET4 9995,
  mill 686; equals the true max in all 5 files). No other "next id" exists. Op ids have no stored counter:
  use max(op id)+1. (tramming ids are 1,3,4,5; gaps are fine.)
- **eptr (+0x20)** is the save-time in-memory DB_LIST_ENT pointer. Values are 0x30 apart in write order,
  start around 0x1007xxxx–0x100exxxx, and are 0 for system/SETUP/NCI ents. On load it serves only as the
  *old* key in an old→new eptr table (param+0x3b68, {idn, old eptr, class}). That table is used by some TP
  kinds (9, 0x1f, 0x20, 0x21, 0x2b) and by solid ops histories. For OP/TP_CHN/TP_BND every pointer field is
  re-resolved by op id, idn or record order (§1).
  → Give new records unique, 16-byte-aligned eptrs (bit 0 must be clear or the writer skips the ent), e.g.
  max eptr + 0x30·k. Fill the cross-pointer fields consistently even though the reader recomputes them.
- **+0x30 created time** (time32). TP_BND+0x70 copies the referenced geometry's +0x30 (verified for every BND
  in tramming). Copy it exactly; it probably drives associativity change detection (unverified).
- **+0x38 on geometry = TP reference count.** It equals the number of TP records pointing at the idn in
  tramming, fight-stick and lathe. It is ≥ that count in BRACKET4 and mill (stale counts). `FixDbReferenceCounts`
  runs at the end of every load (FUN_1802d1b60), so a wrong count is repaired. Still, increment it for each
  new BND.

---------------------------------------------------------------------------------------------------
## 4. Dirty / needs-regeneration state

**Dirty flag = op+0x94 = ent+0xfc (u8).**
- `TpOpList::CountByNCIFlag` counts ops with op+0x94 != 0.
- `CInterrogateMastercamFile::GetDirtyOperationIDs` returns ops with `ent[0xfc] != 0`.
- save_db skips an op's NCI when op+0x94 != 0 (unless op+0xe0 != 0).
- `bnci_restore` sets op+0x94 = 1 (with op+0xf2 = 1, op+0x97 = 0) for ops whose NCI section is missing or
  has errors.

**On load the stored value does not matter.** `FUN_1802cfb60` forces every op to ent+0xe8/0xec = -1,
**ent+0xfc = 1**, ent+0xff = 0, ent+0x131 = 0, and ent+0x15a = !bnci_op_section_valid. Afterwards
`bnci_restore` scans the NCI tail and clears the flag only for ops whose `-2 … -3` section is found and
valid (op id in -2+4 and -3+4, sos flag -2+0x18 set).
→ **An op written with no NCI records stays dirty and shows the red X, and Mastercam lets the user
regenerate it.** Write ent+0xfc = 1, ent+0xe8 = ent+0xec = -1 for consistency, and leave ent+0x15a = 1 as
in the samples.

If the file has **no** NCI at all, omit the NCI_ID record and set tp_hdr+0x78 = 0. `bnci_restore` treats
`+0x78 < 1` as "no NCI", so all ops stay dirty.

---------------------------------------------------------------------------------------------------
## 5. Chain storage (TP_ID, ASSOC 2, len 0x388 = 0x68 + 0x320)

Header words in samples: ent+4 = 0x0101 sel bits plus color 0x9f at ent+6, level 1 at +8.
- BND records carry attrib bytes: +0x10 f64 1.0, +0x18 u16 1, +0x1a u16 1, +0x1c 1.
- CHN records have zeros there.

Everything past +0xb0 is zero for kinds 6 and 7. tp_entity starts at ent+0x68:

| ent off | TP_CHN (kind 6) | TP_BND (kind 7) |
|---|---|---|
| 0x68 i32 | op id | op id |
| 0x6c i32 | 0 | **ent_idn of chained entity** |
| 0x70 i32 | 0 | created time of that entity (copy of its +0x30) |
| 0x74 i32 | 0 | its class (2 line, 4 arc, 0x10000 solid) |
| 0x78 i32 | 0 | class again (2/4) |
| 0x80 i64 | eptr of the **first BND** in this chain (rebuilt on load) | eptr of the geometry (rebuilt from +0x6c) |
| 0x88 i64 | eptr of the OP record (rebuilt) | eptr of the OP record (rebuilt) |
| 0x90 i16 | 6 | 7 |
| 0x98 i32 | chain number (1..n) | chain number |
| 0x9c i32 | **number of entities in the chain** | **1-based position in the chain** (1 = first: triggers header fix-up) |
| 0xa0 u8 | 0 | — (0xa0 i64 = eptr of its chain header, rebuilt) |
| 0xa1 u8 | **closed flag**: 1 closed (tramming outline, circles, BRACKET4 holes), 0 open (BRACKET4 op 20: 3 one-line open chains) | |
| 0xa4 char[] | chain name "Chain 1", "Chain 2", ... | |
| 0xa8 u8 | | **reversed**: 1 = traverse the entity end→start (L p2→p1), 0 = start→end |
| 0xa9 u8 | | 0 (non-zero has wire meaning) |
| 0xe8/0xf0, 0x160/0x168 | | two optional sub-structs with an idn at +8, resolved by find_ent_idn when non-zero (0 in samples) |

**Verified on tramming op 4 (Contour).** The 8 BNDs reference idn 4, 9, 34, 7, 1, 11, 3, 14 with reversed
flags 1, 0, 1, 0, 1, 1, 0, 1. Applying the flags gives a continuous closed loop starting at (2.235, 8.894):
→(150.235, 8.894)→(152.235, 10.894)→(152.235, 26.894)→(150.235, 28.894)→(2.235, 28.894)→(0.235, 26.894)
→(0.235, 10.894)→(2.235, 8.894). That loop is counter-clockwise. Op 1 (2D HS) chains the same 8 lines
starting with idn 1 forward (clockwise). The full circles (tramming op 5, BRACKET4 op 7/12) are single arc
BNDs with flag 0 in closed chains.

Multiple chains repeat [CHN, BND...] blocks: BRACKET4 op 7 has 4 blocks with chain# 1, 3, 2, 4 in file
order. Chain processing order is file order. Other kinds seen: 1 (point / drill position; ref is a point or
arc), 27 (solid ref), 30 (extra per-op entries with a double at +0xa8, 2D Dynamic op 15) — not needed for
Contour/Pocket.

---------------------------------------------------------------------------------------------------
## 6. Binary NCI and how it is tied to ops

Each record is `u16 n` + `nci_bin`:
- **+0x04 i32 op id**: every record of the section, including motion.
- +0x08 i32: op id again on 10xx records, 0 on markers.
- +0x0c u8 format flag.
- +0x10 i32 absolute file offset of this record.
- +0x14 i32 record size incl. the u16 (= `bnci_bytes(gcode)`). **The reader uses +0x14 as the memcpy length.**

`bnci_read_from_disk` **overwrites +0x10 with the actual read position** and recomputes +0x14. So stale
self-offsets after an insertion are harmless, but +0x14 in the stored bytes must be sane (≤ 0x370).

Association is by **op id in each section's `-2` record** (`bnci_restore`: `OpByID(rec+4)`; "Multiple
operation id #'s in nci" if an id repeats). Validity (`bnci_op_section_valid`): the -2 record at the section
start has +4 = op id and +0x18 set, and the record at the section end is `-3` with +4 = op id.
Sections are stored in temp-file order, not op order: tramming has op 1, 3, 4, 5; BRACKET4 is shuffled.

Contour op 4 in tramming (file 0x117a18–0x12036e, 35158 bytes; selected fields shown):
```
-2   sz 0x28 : +4 op=4, +0x18 i32 1 (start-of-section), +0x1c i32 4 (op id), +0x20 i32 1 (opcode), +0x24 time32
-4   sz 0x38 : +0x20 two doubles (2.235.., 5.893..)        \ per-pass begin markers
-6   sz 0x38 : same                                          /
1011 1012(+0x18=2) 1013 1014(tplane 3x3) 1016(+0x18 slot 4, 0x0a, opcode 1) 1017 1025 1027 1028 1029 1020(stock)
20001 "6 Flat Endmill", 20002..20009, 20010/12/14 "Top", 20016 "ALUMINUM mm - 2024", 20018 "Toolpath Group-2", 20019, 20800
1002 tool change: tool 2, rpm 800, feed 200, first pos (2.235, 5.894, 16)
0 rapid → 1 plunge (feed 150) → 3/1 contour moves at z = -11.833 (feed 200) ...
-7, -5                                  (pass end)
-4, -6, 1011 … 20800, motion …          (5 more depth passes, z down to -26; no further 1002)
0 rapid to z 16, -7, -5
-3   sz 0x20 : +4 op=4, +0x18 i32 4 (op id), +0x1c time32  (end-of-section)
```
The minimal *valid* section is `-2 … -3` with matching op ids. The 1011–1017 records are checked against the
op: 1013/1014 "Tplane does not match", 1017 Cplane, 1016 tool slot "Mill tool does not exist", each forcing a
regeneration. A synthetic section is risky and gains nothing. **Recommended: emit no NCI for new ops and let
the user regenerate.**
(2026-11-19: superseded for ops whose toolpath was computed here - formats/nci_save.md: the save writes their section with
nci_write.c; by default Drill ops only, the other engines' per-pass block rule being NOT DECODED.)

---------------------------------------------------------------------------------------------------
## 7. Counts, offsets and checksums that break on insertion

There are no checksums or CRCs (`FUN_1802c72c0` and the save path only write lengths). What must be kept
consistent:

1. **Every record's u32 total_len** (`bytes_per_ent(e,1) + bytes_per_ent(e,2)`): OP 0x2938, TP 0x388.
2. **tp_hdr = DB 0x200 / sub 0x0d record** (len 0x80, right after sub 0x13). Rewritten last by
   `FUN_1802bbd60` ("rewrite_tp_hdr"):

   | off | type | meaning | tramming | BRACKET4 | mill |
   |---|---|---|---|---|---|
   | 0x6c | i32 | count of assoc-capable geometry ents written (`ent_is_assoc && FUN_180035fe0`) | 10 | 36 | 48 |
   | 0x70 | u16 | **number of OP_ID records** | 4 | 16 | 6 |
   | 0x72 | u16 | number of TP "control" ents (non-kind-6 `tp_ent_is_control`) | 0 | 0 | 0 |
   | 0x74 | i32 | **file offset of the SETUP record** (start of the ASSOC section) | 0x4d56a | 0x5552c | 0x3b6be8 |
   | 0x78 | i32 | **file offset of the first binary NCI record** (after NCI_ID) | 0x8a668 | 0xd93f9 | 0x3f2391 |
   | 0x7c | i32 | **max ent_idn** | 173 | 9995 | 686 |

   `bnci_restore` reads +0x78 and seeks there to scan for -2 records, so **+0x78 must be patched** or NCI
   restore reads garbage and every op goes dirty. Update +0x70 and +0x7c too. +0x74 is only meaningful if
   records are inserted before SETUP. Its readers were not traced, so patch it anyway.
3. **OP ent+0xf0** = exact number of TP records for that op. Too small drops TPs and flags the op.
   ent+0xf4/0xf8 = number of chains.
4. Group block: `u32 n` after sub 0x0f, and op_group+0x110 member count (each member adds 0x17 bytes).
   op_group+0x140 is overwritten on read.
5. NCI record +0x10 (self offset) is overwritten on read, so no patch is needed. OP ent+0xe8/0xec are reset
   on read.
6. Geometry +0x38 reference counts are repaired by FixDbReferenceCounts on load.

---------------------------------------------------------------------------------------------------
## Recipe: add one Contour op (no NCI) to a copy of a file

1. Choose a template OP record in the file with the same opcode and machine (e.g. tramming op 4), so tool,
   planes, version (ent+0x1218) and parameters are valid.
2. New OP record (copy the template):
   - ent+0x68 = max op id + 1
   - ent+0x34 = next idn; ent+0x20 = new unique eptr; ent+0x30 = now
   - ent+0xe6 = (#ops + 1)
   - ent+0xe8/0xec = -1, ent+0xfc = 1 (dirty)
   - ent+0xf0 = 1 + #entities (one chain); ent+0xf4 = ent+0xf8 = 1
   - ent+0x2f4 = target toolpath group id, ent+0x5bc = its machine group id
   - change the comment at ent+0x6e and parameters as needed
3. Insert it **after the last existing OP_ID record** (before the first non-OP ASSOC record).
4. TP records: one TP_CHN, then one TP_BND per entity in traversal order, inserted anywhere after the new
   OP record and before NCI_ID. Easiest is right before the NCI_ID record or at the end of the ASSOC block.
   Each gets a new idn and eptr.
   - CHN: +0x68 op id, +0x80 eptr of the first BND, +0x88 OP eptr, +0x90 = 6, +0x98 chain#, +0x9c n,
     +0xa1 closed, +0xa4 "Chain 1".
   - BND: +0x68 op id, +0x6c geometry idn, +0x70 its time, +0x74/+0x78 its class, +0x80 its eptr, +0x88 OP
     eptr, +0x90 = 7, +0x98 chain#, +0x9c index (1..n), +0xa0 CHN eptr, +0xa8 reversed.
   - Use a template TP header (ent+0..0x67) from the same file (sel bits 0x0101, attrib block on BND).
5. Bump +0x38 on each referenced geometry ent (optional; repaired on load).
6. Patch tp_hdr: +0x70 += 1, +0x7c = new max idn, +0x78 += inserted bytes (and +0x74 if inserting before
   SETUP).
7. Leave the NCI tail as is. Existing ops keep their NCI. The new op loads dirty (red X) and can be regenerated.

---------------------------------------------------------------------------------------------------
## Uncertain / not verified
- Not yet tested by opening a modified file in Mastercam. Everything above comes from decompiled code plus
  5 sample files.
- The +0xe6 display ordering is inferred: load keeps file order plus stored +0xe6, and the post-load sort
  routine was not located.
- tp_hdr+0x74: no reader found (seen only in rewrite_tp_hdr). tp_hdr+0x6c/+0x72 semantics are approximate.
- op ent+0xf8 (second chain count) and ent+0x15a: exact meaning unknown; copy them from the template.
- The TlMgr ClassEnt (ASSOC 33) holds tool GUIDs. Whether an op must reference a TlMgr tool by GUID beyond
  the tool#/slot was not checked. Reusing an existing op's tool block avoids the question.
- op_group +0x130..+0x136 flags, the member-record (0x17-byte) layout, and the meaning of type 4.
- TP_BND +0x70 (copied geometry time): whether a mismatch marks the op dirty/out of date is unverified.
  Copy it exactly.

---------------------------------------------------------------------------------------------------
## Implementation (viewer/mcam_write.c, viewer/mcam_saveops.c) — 2026-10-14
- `mcam_write.c` frames a part into segments: ent records, out-of-band group and material blocks, solid XT and
  trailers, NCI records. It rewrites them with blocks inserted. Unchanged round trips are byte-identical on 20 parts.
- `mcam_saveops.c`:
  - Each new op copies an OP record with the same opcode from the part itself. If the part has none, it uses one of
    the user's other parts (`MCAM_TEMPLATE_DIRS`, the part's folder, the CAD folders); group ids are then set to
    this part's.
  - Patched on the copy: ids, eptr, time, op id, comment (0x6e, < 0x78 bytes), +0xe6, +0xe8/0xec = -1, +0xfc dirty,
    +0xf0/0xf4/0xf8 counts, +0x2f4/+0x5bc groups.
  - Common parameters: clearance, retract, feed plane, depth, top of stock with their abs/inc flags; XY / Z stock;
    the tool fields (op = TL body + 0x2e0); feeds; rpm.
  - Contour: compensation computer/left, with chains reversed so left comp = climb; depth cuts 0x868/0x870; multi
    passes 0x8a8/0x8b8/0x8c0; lead in/out 0x8f0/0x948/0x958/0x960; contour type 0x1308, chamfer 0x1288/0x1290; tabs
    0x1380-0x1390.
  - Pocket: stepover 0x1240/0x1248, finish 0x1239/0x125a/0x1260.
  - Drill: cycle 0x1228 (0 G81 / 1 G83), pecks 0x1230/0x1238, tip comp 0x1268, feed plane = R plane.
  - (Superseded 2026-10-16, §8: the whole tool block +0x348..+0x5cf is written. +0x398 is the coolant
    `TlCoolant.OldStyle` and +0x39a the flute count, toollib_net.md §3.5 / `TlToolMill::GetOpToolInfo`; the earlier
    "always 8 / 2-4, not coolant" reading was wrong.) +0x891 (keep tool down) is not written.
- Chains: kind 6 header + kind 7 per entity, as in §5.
- Drill points: one kind-1 TP per point, copied from a drill op's point record. Fields: +0x6c/+0xc8 source ent idn
  (the circle for a centre), +0x70 its time, +0x74 = 1, +0x78 its class, +0x80/+0xd0 its eptr, +0x98/+0xa0/+0xa8 xyz,
  +0x116 "Point n"; ±1e38 sentinels kept.
- New op records go after the last OP record. TP records go before NCI_ID. Header sub 0x0d gets the op count, max idn,
  and setup/NCI offsets shifted by the inserted bytes.
- Exported: Contour (1), Pocket/Slot (3), Drill (2), Dynamic family → 2D High Speed (0x86). The 2D HS parameters are
  the template's apart from the common ones. Face, Helix, Circle, Thread and Peel/Area-specific settings are not
  exported yet.
- Verified here: framing to EOF, header counts/offsets, the NCI offset lands on the first NCI record, TP references
  resolve, and only intended fields differ from the template. **Not yet verified in Mastercam**: the files in
  `test-exports/` need opening in Mastercam 2017 on Windows.

## Edited Mastercam operations (2026-10-14, `viewer/cam_fileop.c`, `mcam_saveops.c`)
- `cam_op_from_file` turns a stored op into a CamOp (C port of `tools/oracle.py` settings; 188/190 ops give the same
  toolpath as the Python reference, the other two differ by 0.001 mm from rounding of the oracle's command line).
  `CamParams.file_op_id` = the op it replaces.
- Export of such an op: its OP, TP and NCI records (-2 .. -3 section) are left out; the new OP record is a copy of the
  original (all unmapped settings kept), keeps its tree position (+0xe6), an empty comment stays empty; tab records
  (kind 30) and point records (kinds 1 / 27) are copied; solid chains are written as one TP naming the solid
  ("Solid Chain N"); the compensation side is written per chain (+0x5d2 left/right) without reversing chains.
- Header offsets (setup +0x74, NCI +0x78) are recomputed for inserted and removed segments (`new_pos`); op count
  +0x70 = + added − replaced.
- Fixed on the way: clearance / retract "on" flags (+0x179 / +0x189) were zeroed on every export; the Dynamic region
  flag +0x1567 was written inverted; roll (+0x5d4) and lead sweep (+0x960) were not written.
- Round trip (export each of the 190 user ops unedited, regenerate from the export): 190/190 identical toolpaths.
- mcamui project files (`.mcamui`) now keep `file_op_id`, retract, lead sweep / midpoint, side_rel, tab positions
  (`TAB x y`), avoidance chains (`CHAIN c A ...`) and solid chains (`CHAIN c S<idn>`).

---------------------------------------------------------------------------------------------------
## 8. Tools of written operations (2026-10-16, EXPORT stream)

Evidence: `decomp/mccore_tooladd.c` (Ghidra `~/mastercam-re/ghidra_core`, mccore.dll), the 16 user parts, the 2017
`test-exports/new-plate.mcam` and `shared Mcam2017/mill/Ops/Mill_MM.mcam-defaults`.

### How an operation names its tool
- Op ent+0x348..+0x5cf (op +0x2e0, 0x288 bytes) is an `op_tool_info`, the same layout as a TL_ID body +0x68..+0x2ef and a
  .tooldb `TlTool.OpToolInfo` blob. Its +0x264 (ent+0x5ac) is the TlMgr **slot** (part_tools.md 1.1), +0x274 (ent+0x5bc)
  the machine group.
- Data (189 ops, 16 parts): the op block equals its tool's TL_ID body byte for byte except the op's own feed / plunge /
  retract (+0x30 / +0x38 / +0x40), rpm (+0x48), bytes after the NUL of the name (+0x56) and library path (+0xce)
  strings (`_mbsnbcpy_s` keeps the old bytes), and +0x260 (1 in 8 ops, 0 in the TL record; NOT DECODED).
- `ProcessAssocOperation` (FUN_1802cfb60): slot 0 -> slot = tool number, +0x5a4 = -1; no check of the slot against
  the TlMgr (only `bnci_restore` checks NCI 1016 slots: "Mill tool does not exist").

### How a tool gets into the part's TlMgr on load
- `ProcessAssocEntity` (FUN_1802cf2a0) case 3: each TL_ID body (`tp_tool`, 0x698 bytes) is appended to a list
  (param+0x3ba8); case 0x21: the TlMgr archive goes to the tool system (vfunc +0x1c0).
- Post-load (FUN_1802d1b60 -> **FUN_1802ce370**): for every collected TL_ID whose slot the tool system lacks (vfunc
  +0x200, the same check bnci_restore uses), `TlMgr::Add(tp_tool)` = `TlToolMill::CreateNewFromOldTool` (class by
  `MCToolType`, then `SetFromOldTool`) + `SetupNewAssembly(tool, holder length and diameter both > tol)`:
  - a holder: when the legacy holder length / diameter (TlToolMill +0x878 / +0x880 = tp_tool +0x2b8 / +0x2b0) are
    <= tol, `SetLegacyHolderData` sets 50 / 25 (mm) or 2 / 1 (inch) and the default holder (FUN_180323600) is used;
    otherwise a holder named by string 0x1dc is built from them;
  - `AdjustLengthForStickout(OAL)`, +0x86c = 1; the assembly is added (`TlMgr::Add` registers the slot map entry).
- Lathe TL records (LTL, list +0x3bc0) go the same way through `TlMgr::Add(LATHETOOL)`.
- So **a tool the part lacks is written as a TL_ID record with a free slot**; no TlMgr archive needs writing.
- Free slot: `TpPartToolList::GetOpenSlot` = `TpToolList::OpenSlot()` (smallest slot >= 1 not in the TlMgr slot map)
  checked by `VerifyOpenSlot` (no TL_ID record with that slot).

### op_tool_info from a tool (`TlTool / TlToolMill / subclass ::GetOpToolInfo`)
| off | field | source |
|---|---|---|
| all | the tool's stored block (TlTool +0x158; TL_ID body, OpToolInfo blob) | TlTool::GetOpToolInfo 0x180754580 |
| +0x00 | ToolNumber (TlTool +0x130) | same |
| +0x56 | name, 0x78 (copied up to its NUL) | same |
| +0xce | GeometryFile 0x105 | same |
| +0x258 | IsMetric (byte) | same |
| +0x25c | ToolStation (TlTool +0x134) | same |
| +0x264 | slot | same |
| +0x26c | DisplayType (TlTool +0x150) | same |
| +0x270 | GeometryLevel (TlTool +0x14c) | same |
| +0x274 | MachineGroup (TlTool +0x138) | same |
| +0x04 / +0x08 | MCToolType (+0x8b8) / OverallDiameter (+0x808) | TlToolMill::GetOpToolInfo 0x18083ff20 |
| +0x28 / +0x2c | DiameterOffsetNum / LengthOffsetNum | same |
| +0x30 / +0x38 / +0x40 / +0x48 | FeedRate / PlungeRate / RetractRate / SpindleSpeed (TlOpParams) | same |
| +0x50 | TlCoolant IsOldStyle (i16) | same |
| +0x52 | FluteCount (i16) | same |
| +0x54 | tool material code | same |
| +0x278 | IsScalable | same |
| +0x10 / +0x20 / +0x06 | Endmill: CornerRadius +0x988 / TaperAngle +0x968 / TlRadiusType (0,1 -> 0; 2,4 -> 1; 3 -> 2) | TlToolEndmill 0x1807677b0 |
| +0x10 / +0x20 | Drill: CornerRadius +0x978 / TipAngle +0x968 (Boring: +0x20 TaperAngle +0x980, +6 radius type) | TlToolDrill 0x18075ece0, TlToolBoring 0x18075a750 |
| +0x18 / +0x10 / +0x20 / +6 | Threading: pitch (inch: 1 / pitch) / CornerRadius +0x9a0 / TipAngle / radius type | TlToolThreading 0x18085f2d0 |
| +6 / +0x20 | FeedMill: 1 / UpperTaperAngle (+0x10 GetProgramRadius NOT DECODED) | TlToolFeedMill 0xa37700 |
Classes per type (`CreateNewFromOldTool` 0x180834f20): 0, 20 TlToolMill; 1 CenterDrill; 2, 3, 9, 22 Drill; 4, 5, 24
Threading; 6 Reamer; 7 Boring; 8 CounterBore; 10-13, 15-19, 21 Endmill; 14 SlotMill; 23 FeedMill; 25 BarrelMill.
**Check:** rebuilding the block from the loaded Tool reproduces all 62 TL_ID bodies of the 16 parts byte for byte.

### tp_tool (TL_ID body) from a tool (`::SetToOldTool`)
TlTool 0x180755070: zero, op_tool_info, MfgToolCode at +0x2d8 (0x78). TlToolMill 0x1808420e0: +0x288 RequiredPilotDiameter,
+0x290 CuttingDepth, +0x298 OverallLength, +0x2a0 ShoulderLength, +0x2a8 ArborDiameter, +0x2b0 / +0x2b8 legacy holder
diameter / length, +0x2c0 i16 spindle dir == 1, +0x2c8 / +0x2d0 SFM / FPT adjust, +0x350 Chuck (0x78), +0x3d0 0x78 bytes
(TlToolMill +0x8f0), +0x468 IsAggregate, +0x469 IsFBMTool, +0x470 ShoulderDiameter, +0x478 TaperLength, +0x66e 20 x i16
canned-text coolant. Subclasses: op params at +0x3d0.. (milling: i16 !rough -> 2 / !finish -> 1 / 0, +0x3d8..+0x3f0
stepovers rough XY / Z, finish XY / Z; holemaking: i16 cycle, +0x3d8 peck1, +0x3e0 peck2, +0x3e8 peck clear, +0x3f0
chip break, +0x3f8 dwell); Endmill / SlotMill +0x3f8 TipDiameter; FeedMill +0x3f8 TipDiameter; Drill +0x400
ShoulderAngle, +0x408 = 0; Boring +0x410 Shift; Reamer +0x400 TipAngle; Threading +0x400 (type 24) / +0x408 RootDiameter.
**Check:** against the 62 TL_IDs, everything matches except +0x2b0..+0x2bf (legacy holder size: not in Tool), +0x3c8 (1
in 2 records; CreateNewFromOp sets it to 1 for op-made tools) and +0x408 in one record.

### Implementation (viewer/mcam_saveops.c, toollib.c `tool_op_info` / `tool_tp_tool`)
- The op's tool (by slot + number, else number) -> its op_tool_info (`tool_op_info`), +0x274 = the op's machine group,
  then the op's feed / plunge / rpm and, when the op's coolant flag differs from the block, OldStyle 16 / 8. An edited
  Mastercam op whose tool slot is unchanged keeps its own block (then the same op values).
- The tool is the part's own when the base has a TL_ID / TlMgr slot entry with the same slot, number and name. Otherwise
  a TL_ID record (len 0x700, header as the 62 user TL_IDs: sel 0x0101, colour 0x9f, level 1) with `tool_tp_tool` and a
  slot from the OpenSlot rule is written (once per tool) before the TP records.
- An op whose tool is in neither: Mastercam's default tool of the type (DatabaseDefaults.tooldb, `tool_new`).
- NOT DECODED / not written: the legacy holder size (+0x2b0 / +0x2b8; Mastercam then gives the assembly its default
  holder, so a library tool's holder is not carried over), the TlToolMill +0x8f0 block, canned-text coolant, +0x3c8,
  op ent+0x5a8 (+0x260).
- **A part tool edited here** (2026-10-16, formats/part_tools.md 10): the tool is the part's when it is the first of F's
  tools with a slot the base part has (number / name may have changed). The exporter loads the source part again, pairs
  tools by slot and writes only what differs: the TlMgr archive is rebuilt by `part_tools_write` (TlMgr::Serialize
  layout, unedited bytes identical) and replaces the subtype-33 record; the tool's TL_ID body and the op_tool_info of
  operations written here keep their stored bytes except those `tool_tp_tool` / `tool_op_info` change between the loaded
  and the edited tool, so the TL_ID body and the TlTool block stay equal (part_tools.md 1.1). Holder name / outline go
  to the assembly's holder object. Not written: a type of another tool class, and any edit when the archive cannot be
  read (lathe classes, pre-X9 schemas). Stored operations that are not written here keep their old op_tool_info
  (whether Mastercam refreshes it from the TlMgr on load is NOT DECODED).
- Op ent+0x1210 (op +0x11a8) = ent_idn of an ASSOC 31 holder record (tramming-rod ops 129 / 141 = its 31 records);
  `TlAssembly::CreateNewFromOp` reads it with `HolderGetHolder`. A new op's copy is set to 0 (no holder record).

### Record stamps (`FUN_1802c72c0`, the raw writer)
Sets ent+0x48 / +0x49 / +0x4b..+0x4e of every record from one set of globals (DAT_18143d54a..550): copied from the base
part's header record. +0x4a is not stamped; mccore only tests it for 2 (wire): written 0. Selection bits: ASSOC 0x0101
(save_db), geometry 0x81 (all user geometry records; meaning of 0x80 NOT DECODED).

## 9. Operation templates and new parts (2026-10-16, EXPORT stream)
- A new operation is a copy of the operation of its type in the operation defaults library, config 2303 "Mill Defaults
  Library" of the user's configuration (CONFIG/mcamxm.config for a metric part: `<APP_DIR_SHARED>Mill\Ops\Mill_mm.mcam-defaults`;
  mcamx.config for inch: Mill_Inch). The user's other parts are no longer searched. An edited Mastercam op copies its own
  record.
- Group: the base's first op's group, else the base's first toolpath group (op_group +0x10c != 0) and its root.
- A new part (`init_db` + `dbinit` = empty database; a toolpath needs the default machine group, config 25000 "Mill
  Machine Definition" = Mill Default mm.mcam-mmd): the defaults library is the base - its machine group is that
  machine (op_group +0x355 compared by file name; a mismatch is reported) - with geometry, solids, OP / TP / TL / NCI_ID,
  the ops' holder records (ASSOC 31) and NCI removed and its TlMgr archive emptied (record +0..+0x378 kept, then
  `FF FF 05 00 05 00 "TlMgr"` and three empty lists, as TlMgr::DeserializeV5 reads).
- NOT DECODED: the thumbnail / description / history records of a new part (the library's are kept).

## 10. Operation fields written (item 4, 2026-10-16)
| field | param | written |
|---|---|---|
| +0x180 / +0x188 / +0x189 retract | 10021.. | on: value, absolute, on; off: only +0x189 = 0 (the copied op's value kept) |
| +0x1a0 depth, +0x888 Z stock | 10029 / 10068 | stock kept, depth = final Z - stock (reader: final = depth + stock, cam_fileop.c) |
| +0x878 / +0x880 depth finish cuts | 10066 / 10067 | contour / pocket 0 (zlevels: calc_depth_cuts with nf 0); slot and 2D HS: the op's (mc_depth_nf / fs) |
| +0x868 / +0x870 depth cuts | 10065 | on / max step; 2D HS also written (hst_depths) |
| +0x8aa rough passes | 15560 | 1: one rough pass per depth (mcmill contour loop, decomp/mcmill_ctour.c l.1506-1580) |
| +0x1381 auto tab positions | 16003 | 1 unless the op's tab records are copied (then the copied value) |
| +0x1230 / +0x1238 / +0x1240 pecks | 10108 / 10109 / 10110 | first peck written; subsequent peck and clearance kept |
| +0x5d2 comp direction, chain direction | 15346.. | side_rel: per p->side, chains as chained; outside / inside contours: comp left and the chain in the direction cam2d cut it (Mastercam follows the chain); other op types: as chained |

---------------------------------------------------------------------------------------------------
## 11. Operation planes and work offset (2026-10-22, `viewer/mcam_saveops.c`, reader `viewer/cam_fileop.c`)
Layout: formats/mcam.md "Operation planes". (Revised 2026-10-22 after review: `cam2d.c cam_planes_to_record` /
`cam_planes_from_record`.) A new op: always written - InitOperation (mccore 0x180046660, decomp/mccore_ops.c l.1770)
replaces the defaults library op's views with the active planes (MastercamPlanes::InitOperationViewsToActivePlanes ->
Plane::Convert, decomp/mastercam_opviews_init.c: +0x92 = the plane's -1) and fills an Automatic Tplane offset from
GetMatchingWorkOffset (BuildWorkOffsetMap, NOT DECODED; every new op in the user parts - Dynamic ones included - holds -1
although Mill_MM.mcam-defaults' Dynamic ops hold 0), so the defaults library's 0x66a is not carried into a new op. An
edited Mastercam op: rewritten only when its planes differ from the stored ones (same bytes otherwise). Each op_view
keeps its own +0x92 (MapToOp sets the WCS / Cplane ones to the Tplane's when the plane id is the same). The Iso matrix
is mastercam.dll's own constants (0.70710678118655, 0.57735026918962, 0.40824829046386, 0.81649658092772 occur in the
DLL; the exact sqrt values do not), so an Iso view writes the bytes Mastercam would. Reading: a view is taken only when
its matrix is a system plane's, its id is that plane's id and its origin is zero (FUN_1802cfb60 resolves by id through
the file's plane map first; a user plane or a moved system plane cannot be resolved without the planes archive -> the op
stays as stored). Old text: each op_view is zeroed and filled as
Plane::InitOperationView does (+0 = 1, +2 view number, +8 matrix rows, +0x50 origin, +0x68 plane id, +0x92 work offset);
the Tplane's +0x92 = the work offset when Manual (>= 0) else -1 (MapToOp), the WCS / Cplane +0x92 = the same when they
are the Tplane's plane (same id and origin), else -1 (the system planes' own work offset, Plane::Init). Plane id = the
view number 1..7 (the SystemPlanes ids of the user parts); Mastercam's reader maps it through the file's plane id map and
falls back to a matrix + origin search, so the system planes are found in any part. A new Top / Automatic op keeps the
defaults library record's views untouched (byte-identical output as before: fileop / postreg). An edited Top op of a
user part writes the same bytes it had (checked on BRACKET4: the 0x210 view bytes of the rewritten op equal the
original's). Only the seven system planes are written / read: the part's MastercamPlanes archive (ASSOC 36) is not
parsed, so no plane is created in it; a stored op whose view is not a system plane is not regenerated (it stays as
stored, cam_fileop.c message). Verified: tools/tests/planes_scenes.txt pl_bottom_save_reopen (save, reopen, edit, post:
the same G-code), pl_b4_edit_top.

---------------------------------------------------------------------------------------------------
## 12. A new op saved = the op generated (2026-11-19, branch regen-consistency; test `tools/tests/regen_consistency.py`)
Rule: an op made here (`cam_cli PART TYPE ... --mcam OUT`, the Qt app) must regenerate from the saved record (`cam_cli OUT fileop
ID`, the app's Regenerate) to the same moves and feeds. Two roots of a mismatch: the save not writing a field the engine cut with, or
the new op's parameters not coming from the record the save writes. Found and fixed (moves + feeds compared, `--movesf`):
| mismatch | root | fix (source) |
|---|---|---|
| `--engrave`: no leads generated, R10 arc leads after a regeneration (user's PCR rack op 4) | contour_type 2 is ours (no such Contour type: 0x1308 0 2D / 1 3D / 2 2D chamfer / 3 3D chamfer / 4 Ramp / 5 Remachining / 6 Oscillate); the engine forced lead off / on the line, the save wrote the default op's 0x8f0 (on) | cam_cli: `--engrave` = Compensation Off (0x5d0 = 4 -> SIDE_ON) + Lead In/Out off (0x8f0 = 0, LEADS) before generation |
| compensation Off on a CCW closed chain cut CW, the saved op ran CCW | cam2d.c chain_offset "on the line: CW" (ours) for chains without a stored direction, while Mastercam's contour follows the stored chain (TP_BND +0xa8, formats/mcam.md; side_rel on reading) | user decision 4: cut in the chain's own direction from its start (as Mastercam), the save keeps the chain as chained |
| a library tool whose number a part tool has: the saved op cut with the part's tool | cam_cli appended the tool with no slot; the save took mcam_tool(number) | cam_cli: a slot of its own (tool = slot + number, cam_fileop.c op_tool); the save gives it its part slot (open_slot) |
| Thread Mill pitch (and hand, ID / OD, direction) | not written; the engine reads op +0x11f0 = 0x1258 (FUN_1802350f0 / FUN_180235a80 / FUN_180236170, decomp/mcmill_all.c l.385268 / 385626 / 385807) | save writes 0x1258, S16 0x1290, 0x12a0, 0x12a2 when they read back otherwise |
| retract rate (feed of retracts with Rapid Retract off) | a new op kept the default record's 0x388, the save wrote the tool's (tool_op_info +0x40) | new op takes the tool's (GetFeedsAndSpeedsFromTool mccore 0x180884bf0: feed / plunge / retract / rpm, formats/mcamui_dialogs.md 3581); the save writes the op's mc_rrate into +0x40 |
| 2D chamfer 0.25 mm off after reload | a new tool (TL_ID only, no TlMgr entry) lost its tip diameter | loader: TipDiameter = tp_tool +0x3f8 for end mill / slot / feed mill classes (TlToolEndmill::SetFromOldTool decomp/mccore_tooladd.c l.10357, TlToolFeedMill l.11870) |
| 2D High Speed: first-pass / ramp / back feeds | a new op's engine record was the default op's (its feed 0x378) | cam2d_dyn.c hst_record: feed / plunge / retract / rpm put where the save writes them (tool block 0x378 / 0x380 / 0x388 / 0x390) |
| 2D High Speed gap size (% of D) | computed with the default op's tool | cam_defaults_from_file: 0x1608 x the op's D (as params_from_record) |
| 2D High Speed stepover / min radius / micro lift / plunge angle / back feed / gap | not written (only 0x1580) | save writes 0x1730 (when 0x1700 && 0x1701, LoadDynPockParm l.1612-1624), 0x1658, 0x16d0, 0x1678, 0x1248, 0x1600 / 0x1608 |
| coolant (M8) | the reader took the tool's (0 for part tools) | params_from_record: op coolant word 0x398 == 0x10 (PV_COOL_OLD), which the save writes |
| 2D HS back feed 0 (= the feed) | a new op took the default op's feed as its back feed | cam_defaults_from_file: back feed = the op's (tool's) feed when 0x1248 is 0 |
| pocket zigzag (`--set pocket_style=1`) | not written | save: cutting method S16 0x123c = 0 (the engines' method 0 for style 1; the reader's style 1) |
| Helix Bore rough pitch | not written | save: 0x1250 (dialog 2823; params_from_record) |
| drill: clearance from the R plane | the save tested clearance-off against top + CamParams.feed_plane, the reader computes top + its feed plane field 0x190 = the R plane | save: clearance-off value of a drill op = the R plane |
| contour depth cuts (`--step`): keep tool down / 0x93b rule | a new op's 0x868 (default record: off) while the save writes it on for more than one cut | cam_cli: 0x868 as the save writes it |
| contour `--finish N` at every depth | Multi Passes off in the generated op, on in the saved one (with the default op's "finish at every depth" 0x1230) | cam_cli: Multi Passes on, rough 1, N finish, as the save writes them |
Measured 2026-11-19: regen_consistency.py 34 cases x (fileop, GUI tpregen) 0 differ, the user's PCR op 4 identical (after decision 4: its moves
equal the user's original op4.nc exactly); fileop_all 0/205 vs the master build;
tools/oracle.py contour 149 / pocket 1 / drill 5 / dynamic 37 / thread 6 ops: per-op output identical to master's. Op goldens
(op_compare.sh): 47 output files change against the int-pcr base build (71 -> 99 / 668 differ vs golden/op, not re-blessed): new Drill
ops show retract rate R2000 (the tool's) instead of R0; new 2D High Speed ops' gap size (P link_retract 100 -> 50, page 3205 edit
1000 -> 500, rt_dyn_medax_entry2's links) and first-pass / ramp feed (F7. -> F3060., the default op's feed before); reopened ops
show their saved coolant (cool 0 -> 1, "Coolant=On") - each the value the saved op regenerates with.
User decision 1 (2026-11-19): those 47 golden/op files re-blessed for exactly these four reasons (each file's diff checked; the
int-pcr base's other known differences in them kept): op_compare.sh 71 / 668 differ, as the int-pcr base build (3 of them MISSING
goldens). Found on the way: the 2D HS writes ran before the dialog's record patches and rewrote the % gap field of an op set to
Distance (rt_dynamic reopened: 500 -> 50) - now after the patches.
Not fixed / NOT DECODED: the 3D types (optirough, srough, sf*, ...) and Lathe Face are not in the test (their new ops need surface /
solid references or a lathe part); cam_cli flags that only set CamParams fields the record-driven engines (face, slot, helix, circle,
thread passes: p->mc_p) do not read are no-ops both ways. User decisions 2026-11-19: `--engrave` stays as an ours
test shorthand and contour_type 2 left the engine (cam_contour_engrave_compat); the library tool number on a clash stays until
WarnOfDuplicateToolNumbers is decoded (TODO.md); Compensation Off follows the chain's direction (the CW rule was removed); cam_cli's
feeds always from the tool (test tooling; the app follows the group's Feed Calculation).
