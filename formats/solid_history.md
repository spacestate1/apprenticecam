# Mastercam solids in a part file: solid entity, XT, history (ops) records (decoded 2026-10-23)

Scope: how a .mcx-9 / .mcam stores a solid and its feature history, how Mastercam regenerates it, and what a writer
must emit. Rule 1 applies: every fact cites its source; **NOT DECODED** = unknown, **[inferred]** = a reading of code /
data that is not proven. Companion docs: formats/mcam.md (file framing), formats/extrude.md (Extrude panel / P block),
formats/xt.md + formats/occt_accuracy.md (the XT itself).

Sources (mcgeomsld.dll, Ghidra `~/mastercam-re/ghidra_sld`, headless `-readOnly`; dumps in `decomp/solid_history/`):

| file | functions |
|---|---|
| `h1_read_save_regen.c` | `sld_save_solids` 0x1800c6b50 -> `FUN_1800c6d20` (writer), `mc_save_opsfile`, `FUN_18026b3c0` (McOpsFile read), `FUN_18026dc50` (McOpsFile write), `FUN_180266d90` (McOpsFile ctor), op factory `FUN_180267440`, `FUN_180267170` (add op), `sld_regen_solid`, `FUN_1802677f0` (regen-if-dirty), `FUN_18026c170` (rollback), `FUN_18026c590`, `mc_is_entity_a_brick`, `sld_remove_history_task`, `FUN_1800fa860` (remove history), `sld_make_brick_copy` |
| `decomp/mcgeomsld_read.c` (older dump) | `sld_read_solids` 0x1800c40a0, `mc_read_opsfile`, `mc_bypass_opsfile_read`, `sld_regen_solids`, `FUN_1800ce6e0` (PK_PART_receive / transmit) |
| `h2_op_base_extrude_toolp.c` | McOperation read `FUN_1802649f0` / write `FUN_180266560` / size `FUN_180263a70`; SldExtrudeOp ctor `FUN_18027abc0`, read `FUN_18027bbe0`; SldToolPOp read `FUN_18028e9d0` |
| `h3_chainop.c` | McChainOp ctor `FUN_180270230`, read `FUN_180271cc0` |
| `h6_op_readers.c` | McSldEntOp read `FUN_180274c80`, SldBodyOp read `FUN_18026f4f0`, SldExtrudeOp write `FUN_18027cb40`, Boolean / Chamfer / Fillet / Primitive / Revolve / Shell readers (not decoded further) |
| `h5_postload_replay.c` | `FUN_18026b6b0` (history replay), `FUN_180266b20`, `FUN_180271370`, `mc_op_updateToolEntPtrs` |
| `h7_brick_bodyop.c` | `sld_convert_CArray_tags_to_mc_ents`, `mc_create_body_op` |
| `h8_mceptr_entries.c`, `h11_entinfo.c`, `h10_sweep_naming.c` | `FUN_1802656b0` (op entity records from body tags), `FUN_180049510` / `FUN_180050bd0` (mcEntinfoPtr get / set), `FUN_180049740` / `FUN_180050e00` (mcEptrPtr get / set), `ps_sweep_sheet` (extrude naming) |
| `h4_attdefs.c` | `ps_open_parasolids` (the CNC/* attribute definitions) |
| `sld_op_vtables.txt` | VtableDump of every `Sld*Op` / McOperation / McOpsFile vftable (slot 19 = size, 20 = read, 21 = write) |

Parser: `tools/sld_hist.py PART [-v]` (new) dumps every solid, ops header and op; on all 17 user parts
(`<user-parts>/*.mcx-9`) **303 of 303 ops parse to their exact byte size** (108 solids). No other parts with
solids were available (Mastercam's templates contain none; <mastercam-samples>/net does not exist yet).

## 1. The two records of a solid

A solid is two DB entities plus a trailer:

### 1.1 SOLID_ID (class 0x10000) - the solid as a DB entity
Written with the other geometry by mccore (formats/mcam_write.md section 12); file record length 0x100. The body at
+0x68 is `mc_solid_ent` (`sld_init_solid_ent` 0x1800bd5c0, `sld_compute_bbox` 0x1800b0120, `sld_read_solids`):

| ent off | type | meaning | source / evidence |
|---|---|---|---|
| +0x00 | u32 | 0x10000 | |
| +0x06 / +0x08 / +0x20 / +0x30 / +0x34 | | colour / level / eptr / created time / ent_idn, as every entity (formats/mcam.md) | cube: eptr 0x0f884ea0, idn 267 |
| +0x48.. | 6 bytes | per-entity version stamp `12 41 00 01 05 01` in every user SOLID_ID | data (meaning per formats/mcam.md +0x48) |
| +0x68 | i32 | Parasolid body tag (runtime; stale value in the file, replaced by PK_PART_receive) | `FUN_1800ce6e0` writes `*param_2 = *parts` |
| +0x6c | i32 | 0 | `sld_init_solid_ent` |
| +0x70 | u16 | flags: init 1; **bit 2 (4) = solid rolled back / no current body**: set by `FUN_18026c170` (rollback), tested by `FUN_18026a920`; when set at save the XT is not written (xt_size 0) | `FUN_1800c6d20`, `sld_regen_solid` |
| +0x78 | 3 double | bounding box min, part units | `sld_compute_bbox` (ps_get_body_bbox); cube (-5,-5,-6) |
| +0x90 | 3 double | bounding box max | cube (5,5,0) |
| +0xa8 | u32 | **solid id N** (the `sld_N` of the XT key; next id = global DAT_1803e80cc, incremented by sld_init_solid_ent) | `sld_read_solids` matches SOLID_ID+0xa8 == the u32 after the XT |
| +0xb0 | ptr | McOpsFile* (runtime; garbage in the file, set by `mc_read_opsfile`) | |
| +0xb8.. | char[0x100] | solid name (runtime copy; the file record ends at +0x100, the stored name is the SOLIDXMT trailer) | `sld_read_solids` copies the trailer name to +0xb8 |

The bytes +0xb9..+0xff in the file are stale heap data (pointers) in every user part: [inferred] they are never read.

### 1.2 SOLIDXMT (class 0x20000) + XT + trailer + ops (section 13 of the file, `sld_save_solids`)
`sld_save_solids` picks by the target file version (MCFile+0x2c) `(major byte, solid ver, PK transmit version)`:
7 -> (7, 2, 100) ... 0x11 -> (0x11, 0x16, 0x104), **0x12 (X9, .mcx-9) -> (0x12, 0x1a, 0x10e = Parasolid 27.0)**,
default (2017 .mcam) -> (current_version_exref[2], **0x1c**, 0x118). Solids that `sld_can_save_as_v*` rejects are
skipped for old versions. Then per solid (`FUN_1800c6d20`), in reverse DB order (DAT_1803e8130 list, `lVar2--`):

```
u32 0x16c                                  (file versions 0 or > 9; else write_ent_to_disk)
SOLIDXMT ent, 0x16c bytes, all zero except:
  +0x00 u32 0x20000
  +0x48 u8  major byte (0x12 in the user's X9 parts)
  +0x49 u8  solid ver (0x1a in the user's parts; 0x1c = current)
  +0x68 char[0x100] XT key  "sld_N"  (sld_get_xmt_name)
  +0x168 u32 xt_size (0 when SOLID_ID flag 4 is set: no XT follows)
xt_size bytes: PK_PART_transmit output (".x_b" binary if ver >= 0xf, else ".x_t"), copied from a temp file
u32 solid id N                               (SOLID_ID +0xa8)
char name[ver > 0x1b ? 0x100 : 0x40]         (SOLID_ID +0xb8; '' in 92 of 108 user solids, user names otherwise)
ops block (section 2)
```
Verified on the user parts: the only non-zero SOLIDXMT bytes are +0x00..+0x03 (class), +0x48 = 0x12, +0x49 = 0x1a,
the name and +0x168.

Reader checks (`sld_read_solids`): a solid with major < 7, major > current, ... and ver != 0x1c gets: ver < 2 ->
sld 676 "Attempting to bypass solids created by an earlier (pre-release) version.", ver 2..0x1c accepted, ver > 0x1c
-> sld 678 "... more recent version." (bypassed via `mc_bypass_opsfile_read`). Env var `SLD_NOLOAD` bypasses all
solids. The XT is written to `<temp>/sld_N.x_b` and later received with `PK_PART_receive` (`FUN_1800ce6e0`, retried
with other option tokens on errors 0x3a9 / 0x22b). The SOLID_ID with +0xa8 == N is found among the already-read DB
solids; if none exists one is created (`FUN_18009be90` -> `dbput_solid`).

## 2. The ops (history) block = McOpsFile

### 2.1 Header (`FUN_18026b3c0` read, `FUN_18026dc50` write; McOpsFile ctor `FUN_180266d90`, 0xb8 bytes)
| bytes | McOpsFile field | meaning | values in user parts |
|---|---|---|---|
| u8 | +0x0c | **"needs regen" flag**: `FUN_1802677f0` replays the history only when it is != 0; cleared after a good replay; set to 1 when an op's re-check (vfunc 200) fails (`FUN_180267170`) or by `FUN_18026c590(ops,1)` | 0 in all 108 |
| u8 | +0x0e | NOT DECODED | 0 (18 solids) / 1 (90) |
| u32 | +0x10 | solid id N (ctor param) | = N |
| u32 | +0x14 | number of active ops = history position (rollback marker: `FUN_18026c170` uses +0x14-1 as the last active op); the reader restores it after adding the ops | = count in all 108 |
| u64 (u32 if file version < 0x11) | +0x20 | eptr of the SOLID_ID entity (save-time DB pointer) | = SOLID_ID +0x20 |
| u32 | +0x38 | op count | 1..9 |

Then per op: `u32 op type`, `u32 size` (vfunc slot 19 = 0x98), `size` bytes (slot 21 = 0xa8 write / slot 20 = 0xa0
read). The reader creates the op from the type with the factory `FUN_180267440`, appends it (`FUN_180267170`, which
also adds it to the Solids manager: `HistDlg_addOp`) and calls its Read; each Read re-computes its own size and
reports sld 259 / 225 on a mismatch. McOpsFile+0x08 = ops version: 0x1c, or the SOLIDXMT ver when that is not 0x1c
(`mc_read_opsfile`); all version gates below use it (user parts: 0x1a). File version < 0xb or `param_3`: toolpath op
types (`mc_opType_is_toolpath`) are not written (`FUN_18026a110`).

### 2.2 Op types (factory `FUN_180267440`; names = opmanres 27601 + type, mastercam.dll HistDlg; classes = vftables)
| type | name | class (ctor, object size) | in user parts |
|---|---|---|---|
| 0 / 1 / 2 | Extrude Cut / Extrude Boss / Extrude | SldExtrudeOp (FUN_18027abc0, 0x1b0) | 187 / 0 / 108 |
| 3 / 4 / 5 | Revolve Cut / Boss / Revolve | SldRevolveOp (FUN_1802881d0, 0x130) | 0 |
| 6 / 7 / 8 | Sweep Cut / Boss / Sweep | FUN_18028a430 (0xf8) - class name NOT DECODED | 0 |
| 9 | Fillet | SldFilletOp (FUN_1802805d0) | 0 |
| 0xa | Chamfer | SldChamferOp (FUN_180277a60) | 0 |
| 0xb / 0xc / 0xd | Boolean Remove / Common / Add | SldBooleanOp (FUN_180276ac0, 0xa0) | 0 |
| 0xe | Body (a "brick", imported / history removed) | SldBodyOp (FUN_18026e130, 0x198) | 0 |
| 0xf..0x13 | Block / Cone / Cylinder / Sphere / Torus | SldPrimitiveOp (FUN_180285cd0) | 0 |
| 0x14 | Shell | SldShellOp (FUN_180289bc0) | 0 |
| 0x15..0x17 | Loft Cut / Boss / Loft | SldLoftOp (FUN_180282db0) | 0 |
| 0x18 | Toolpath (solid chain of a toolpath) | SldToolPOp (FUN_18028c700, 0x170) | 8 (BRACKET4 x2 copies, buckle-*) |
| 0x19 | Draft Face | SldDraftFaceOp (FUN_180279b00) | 0 |
| 0x1a | Trim to surface/sheet | SldTrimOp (FUN_180290ba0) | 0 |
| 0x1b | FBM Drill | SldFeatureDrillOp (FUN_18027d320) | 0 |
| 0x1c | Lathe Toolpath | SldFeatureLatheOp (FUN_18027ec90) | 0 |
| 0x1d | Thicken | SldThickenOp (FUN_18028c0b0) | 0 |
| 0x1e / 0x1f | (no opmanres name) | [inferred from vtable address order] SldFXFOp (FUN_1802822b0) / SldPatternOp (FUN_180283970) | 0 |
Other types: sld 278 "Unknown operation." Class hierarchy: McOperation <- McChainOp (Extrude, [inferred] Revolve /
Sweep / Loft) and McSldEntOp (ToolP, [inferred] Fillet / Chamfer / Boolean ...), SldBodyOp directly on McOperation.

**User parts: only Extrude (type 2), Extrude Cut (type 0) and Toolpath (0x18) occur** - 108 Extrude, 187 Extrude Cut,
8 Toolpath ops; no Boolean, Fillet, Boss, Body or primitive. Per part: every solid starts with one Extrude; cuts follow
(e.g. BRACKET4 sld_46: Extrude + 6 Extrude Cut + 1 Toolpath; test-cube-10mm sld_378: Extrude + 1 Extrude Cut;
tramming-rod sld_50: one Extrude).

### 2.3 McOperation base (read `FUN_1802649f0`, write `FUN_180266560`, size `FUN_180263a70`)
Sizes for ops version >= 0x19 and a 64-bit file (MCFile+0x31 == 0):
```
i32 n1;  n1 x McEptr record            (op+0x44 / op+0x48: the faces and edges this op created, see 3.2)
i32 n2;  n2 x McEptr record            (op+0x60 / op+0x68: see 3.2)
i32 n3;  n3 x u64                      (op+0x80 / op+0x88: NOT DECODED; non-zero only in cut ops, 3..28 values)
u16 len; char name[len]                (op+0x20, len = strlen + 1; e.g. "Extrude", "Extrude Cut")
u8 op+9, u8 op+0xa, u8 op+0xb, u8 op+0xc
u32 op+0x10                            (if 0 the reader clears op+0xc)
u64 op+0x18  = SOLID_ID eptr           (u32 if ops version < 0x19)
u32 op+0x34  = op id (0, 1, 2 ... in history order)
u32 op+0x30  = op type (repeats the type before the size)
u32 op+0x3c  = op version (ver > 8; 26 = 0x1a in all user ops; drives the version-dependent PK tokens, occt_3d_plan Q4)
u8 op+0x40, u8 op+0x41                 (ver > 0xf; 0 / 12 in all 303 user ops: NOT DECODED)
```
McEptr record, serialized 38 bytes (in memory 0x30; ver >= 0x18 has 64-bit refs, ver > 0xf has the two bytes):
`u64 id (mem +0x20), u32 kind (+0), u32 op id (+4), u64 ref1 (+8), u64 ref2 (+0x10), u32 (+0x28), u8 (+0x2c), u8 (+0x2d)`.
mem +0x18 = the Parasolid tag (runtime, not saved). The **id** is the record's own memory address at save time
(`FUN_18026c690` sets +0x20 = this); after load it is zeroed (`FUN_180266b20`), so it only has to be unique and
consistent with the refs that point at it. User data: x28 / b2c / b2d are 0 in all 10387 records, op id == the
owning op's id in all.

Op flag bytes: op+9 is set to 1 for every op after a load (`FUN_180268520` from `sld_read_solids`) and makes
`FUN_1800c1630` mark stock models / dependent toolpaths dirty; op+0xa = "failed" (set by SldBodyOp when the body
cannot be read; `FUN_1802677f0` reports the last failed op). op+0xb / +0xc / +0x10: NOT DECODED (all 0).

### 2.4 McChainOp part (read `FUN_180271cc0`), after the base
```
u8  op+0x90                         (0 in all user ops)
i32 nchains (op+0x94)
i32 count[nchains]                  (entities per chain, op+0x98)
i32 xa0[nchains]                    (op+0xa0: 0 or 1; NOT DECODED - no correlation with chain orientation or cut/create)
per chain: count x u64              (op+0xb0: save-time eptrs of the chained wireframe entities, in chain order)
u8 xc0[nchains]                     (ver > 5; 0 in all)
u8 xa8[nchains]                     (ver >= 0x14; 0 in all)
per chain: count x u8               (op+0xb8: 1 = the entity is traversed end -> start)
```
Chain entities are referenced by their DB eptr (+0x20 of the line / arc record in the same file); the reader maps old
to new eptrs through mccore's old->new table (formats/mcam_write.md section 3). Verified on test-cube-10mm: the four
eptrs are the four lines (idn 259..262) of the 10 x 10 square at z 0; with the flags [0,1,1,0] the chain runs
(-5,-5) -> (5,-5) -> (5,5) -> (-5,5) -> back, continuous only with flag 1 = reversed.

### 2.5 SldExtrudeOp part (read `FUN_18027bbe0`, write `FUN_18027cb40`), after McChainOp
```
0x70 bytes  SLD_EXTRUDE_PARAMS, raw (op+0xd8; formats/extrude.md section 0 "P+x"; 0x54 / 0x68 packed for old files)
i32 ntrim   (op+0x148) ; ntrim x u64 (op+0x150) ; ntrim x 0x18 bytes (op+0x158)   trim-to faces: NOT DECODED (0 in all)
nchains x 0x60 bytes   per-chain data SLD_EXT_CHAIN_DATA (op+0x1a8): 3 double direction, then 9 doubles [inferred
                       PLANE_sf: origin, normal, x axis]; user data: (0,0,-1 | 0,0,0 | 0,0,1 | 1,0,0) 306x,
                       (0,0,1 | 0,0,0 | 0,0,1 | 1,0,0) 5x
```
P values seen (all 295 extrude ops): type 0 Create / 1 Cut; Create ops keep a stale P+0x08 target eptr in 49 of 108
(ignored for Create [inferred]); Cut target = the solid's own SOLID_ID eptr in all 187; distance method 0 / 1 (Through
all); distance; vector (0,0,-1) mostly; all bools 0; draft angle 0.0872664626 rad (5 deg default); thin 1.0 / 1.0.
Example test-cube-10mm: op 0 Extrude, distance 6.0, vector -Z (bbox z -6..0 matches); op 1 Extrude Cut, Through all,
chain = one arc r 1.5 at the origin (idn 263).

### 2.6 Other classes (only what the user parts need)
- **SldToolPOp (0x18)**: McSldEntOp part (`FUN_180274c80`: u8, i32 n + n u64 + n x 0x18, i32 n2 + n2 u64 + n2 x 0x18,
  u8 flag (+ n2 bytes), i32 n3 + n3 x 0x48, i32 n4 + n4 x 0x60 pick records), then `FUN_18028e9d0`: u64, i32 + bytes,
  i32 + bytes, i32 + n shorts + n strings, i32 + n ints + n strings, u16, i32 + shorts, i32 + n sub-records (each u32
  flag, flag != 0 -> object `FUN_180278610` NOT DECODED; all 0 in the user parts). BRACKET4's op 7: one 0x60 pick
  record (eptr + point (7, 7.5, -8) ...). The chain itself is rebuilt by rolling the solid back (formats/mcam.md
  "Solid chains"). The field meanings, the face-id -> McEptr record resolution after load (FUN_18026a240) and the writer chain:
  formats/surface_selection.md 7 (stage S3, viewer/sldtoolp.c).
- **SldBodyOp (0xe)**: base, then `u32 size + size bytes of XT` (its own transmit "sld_N_bo"), read back with
  `ps_read_body` - a brick stores its body twice (SOLIDXMT XT + this op).
- Boolean / Fillet / Chamfer / Revolve / Primitive / Shell readers are dumped (`h6_op_readers.c`) but not decoded:
  no user data to verify them against. **NOT DECODED.**

## 3. Face / edge identity

### 3.1 Parasolid attributes (`ps_open_parasolids` 0x18004d370)
Mastercam defines seven attribute definitions (PK_ATTDEF_create, class token 0x16ae):

| name | owner class tokens | fields (tokens) |
|---|---|---|
| CNC/mcEptr | 0x138c, 0x138a, 0x7d3, 0x7d2 | 1 x 0x170d |
| CNC/mcEntinfo | same 4 | 4 x 0x170d |
| CNC/mcEntSel | 0x138c, 0x138a, 0x1389 | 1 x 0x170d |
| CNC/mcColor | 0x138c, 0x138a | 1 x 0x170d |
| **CNC/mcEptrPtr** | 0x138c, 0x138a, 0x7d3, 0x7d2 | 1 x 0x1715 |
| CNC/mcEntinfoPtr | same 4 | 0x170d, 0x170d, 0x1715, 0x1715 |
| CNC/mcEntSelPtr | 0x138c, 0x138a, 0x1389 | 1 x 0x1715 |

(Token names NOT DECODED - no Parasolid headers; by the XT data 0x170d is an int field and 0x1715 a pointer field.)

**In the XT of every user solid (108/108) there is exactly one ATTRIB_DEF, "CNC/mcEptrPtr"** (XT: type_id 9000,
actions [0,0,0,0,3,6,0,0], legal_owners [0,0,0,0,1,0,1,0,0,1,1,0,0,0], fields [9]), and **one ATTRIBUTE per FACE and
per EDGE** (e.g. cube 7 + 14, BRACKET4 34 + 74), each with its field pointer = null (1): pointer fields are not
transmitted. mcEntinfoPtr (set during feature creation, `FUN_180050bd0`) is deleted before save
(`FUN_180041d20`); no mcColor attributes in the user parts (no face colours). A writer must reproduce this (task A):
the ATTRIB_DEF + ATT_DEF_ID "CNC/mcEptrPtr" + one empty ATTRIBUTE per face and edge.

### 3.2 McEptr records = Mastercam's topological names
`FUN_1802656b0` (op, n, tags): for each face / edge tag of the op's result, the record is filled from the tag's
mcEntinfoPtr attribute (`FUN_180049510`: kind, op id, ref1, ref2), or kind 0x34 (face) / 0 (edge) when it has none
(message sld 677), and the tag gets an mcEptrPtr attribute pointing at the record (`FUN_180050e00`). So
**mcEptrPtr(face or edge) -> McEptr record**, and the records are the persistent names that later ops and toolpaths
use (refs between records, `HistDlg`, SldToolPOp).

Kinds (`ps_sweep_sheet` 0x1800530c0 sets them for Extrude; meanings checked on test-cube-10mm geometry):

| kind | set by | meaning | ref1 / ref2 |
|---|---|---|---|
| 1 | the profile sheet ([inferred] `ps_chains_to_sheet_bodies`) | edge on the start (profile) cap | chain entity eptr / 0 |
| 4 (5, 6, 0x12) | `ps_sweep_sheet` for source kind 1 (2, 3, 0x10) | side face swept from a chain entity | chain entity eptr / 0 |
| 0x1f = 31 (0x20, 0x21, 0x2b) | same | edge on the end cap | chain entity eptr / 0 |
| 0x13 = 19 | `ps_sweep_sheet` (0x8000013 for every edge without entinfo) | side (lateral) edge | the two kind-31 records that meet at its end vertex (cube: verified by corner geometry) |
| 0x38 = 56 | NOT TRACED ([inferred] sheet / cap naming) | start and end cap faces | 0 / 0 |
| 0x3b | `ps_sweep_sheet` for faces without entinfo | (not seen in data) | 1 or 2 / 0 |
| 0x18 = 24 | NOT TRACED (cut ops) | test-cube: the hole's side face after the cut | a face record of an earlier op / an own record |
| 0x2d = 45 | NOT TRACED | 6 records in cut ops | |
| 0x34 / 0 | `FUN_1802656b0` | face / edge without a name | 0 |

Counts confirm it: an Extrude of one closed chain of N entities has N x 19 + N x 4 + N x 1 + N x 31 + 2 x 56 records
(N side faces + 2 caps, 3N edges); a single circle gives 4, 1, 31, 56, 56 (no vertex, no side edge). For every
single-op solid the record count equals XT faces + edges (e.g. cube op 0: 18 = 6 + 12; tramming-rod 34 = 34).
Records of later ops may be consumed: sum over ops >= faces + edges of the final body.
The second list (op+0x60) holds kind | 0x10000000 records ([inferred] the tool body's names before the boolean of a
Cut: cube op 1 has 56, 56, 31, 1, 4 of the arc); empty for Create. Record **order** follows Parasolid's
PK_BODY_ask_edges / ask_faces order: NOT reproducible without Parasolid.

### 3.3 After loading
PK_PART_receive restores the bodies but **not** the mcEptrPtr pointers (null in the file), and the reader zeroes the
record ids (`FUN_180266b20`). The link face/edge -> record is rebuilt only when the history is replayed
(`FUN_18026b6b0` -> each op's regen, then `FUN_180265240` / `FUN_1802656b0`) - e.g. by a rollback for a toolpath's
solid chain (`sld_get_ToolP_Chains`, formats/mcam.md) or Regen. [inferred: so Mastercam itself works with a loaded,
un-replayed solid whose faces carry empty attributes; the records only matter once something replays.]

## 4. Regeneration

- **On open** (`sld_read_solids`): XT -> PK_PART_receive (`FUN_1800ce6e0`), ops read, `mc_op_updateToolEntPtrs`,
  op+9 = 1, solids whose ops file is missing / empty and not rolled back lose their ops file (`mc_delete_opsfile`);
  then `sld_regen_solids(0)` -> per solid `sld_regen_solid(eptr, 0)` -> `FUN_1802677f0(ops)`, which **replays only if
  the ops header byte +0x0c is != 0** (and the solid is not rolled back). **With 0 (all user parts) Mastercam trusts
  the stored XT and does not re-run the history.** Then `mc_add_solid_to_dialog` (Solids manager).
- **Replay** `FUN_18026b6b0`: from op index ops+0xa0, for each op: skip if vfunc 0x118 (active / not suppressed)
  says so, else vfunc 200 = the op's own regenerate (SldExtrudeOp -> `sld_extrude` with the stored P and chains),
  rename (`FUN_180265240`), stop at the first failure (sld 207 "Error reapplying operation. Restoring model state." /
  255 "... Regen not complete."; on failure the solid is rolled back to the last good op, ops+0x14 = that index).
- **Regen Solid / Regen All** (Solids manager): `sld_regen_solid(eptr, 1)` -> `FUN_18026c590(ops, 1)` sets the
  dirty byte, then the same replay. `sld_regen_solids(1)` also does this for solids that depend on each other
  (`FUN_18026b260`, boolean tool bodies).
- **Rollback** `FUN_18026c170`: sets SOLID_ID+0x70 bit 4, replays to the given op.
- **Transform** (occt_3d_plan T.1): `sld_xform_solid` transforms each op's parameters and regenerates.
- **No history / brick**: `mc_is_entity_a_brick` = the ops file has exactly 1 op and it is type 0xe (SldBodyOp).
  Import (`sld_convert_CArray_tags_to_mc_ents` -> `mc_create_body_op`) and **Remove History**
  (`sld_remove_history_task`: prompt sld 808, confirm 982 / 983 "Some toolpaths will require geometry
  re-selection..."; `FUN_1800fa860`: PK_ENTITY_copy_2 with tracking, a new solid via sld_convert_tags_to_mc_ents,
  attributes / name / level copied, `idnSwap`, the old solid deleted, face / edge identifiers mapped with
  PK_ENTITY_ask_identifier / PK_PART_find_entity_by_ident) produce such a solid: one Body op holding the XT.

## 5. Writer design (what to emit for a solid made in apprenticeCAM)

All in a 64-bit X9 file (.mcx-9 major 0x12 -> solid ver 0x1a) - the format of every user part; Mastercam 2017 opens
X9 parts (it accepts solid ver 2..0x1c). For a 2017 .mcam: ver 0x1c, name trailer 0x100 bytes, transmit
version 0x118 (Parasolid 28) - our XT is schema 26105 / 27.0, which [inferred] Parasolid 28 still receives.

### 5.1 One Extrude (Create) solid with history
1. **Wireframe**: the chain's lines / arcs as normal DB entities (they already exist when the user chained them); note
   their eptrs E_i (unique, 16-aligned, formats/mcam_write.md section 3).
2. **SOLID_ID record** (section 12, DB order with the geometry), len 0x100: class 0x10000, colour / level, eptr S,
   time, new idn, +0x48 stamp copied from an existing SOLID_ID (`12 41 00 01 05 01`), +0x68 0, +0x70 u16 1,
   +0x78 / +0x90 bbox (part units), +0xa8 N (unused solid id: max existing + 1 [inferred: the reader only matches
   ids]), rest 0.
3. **Section 13**: `u32 0x16c`, SOLIDXMT (class 0x20000, +0x48 0x12, +0x49 0x1a, +0x68 "sld_N", +0x168 size), the XT
   (task A writer; header KEY=sld_N, FILE=SLD_N.X_B, present in all 108 user XT headers),
   `u32 N`, `char[0x40]` name (zeros, or the solid's name).
4. **Ops header**: `u8 0` (trust our XT) **or 1** (make Mastercam replay the Extrude with its own Parasolid on open -
   then our XT only has to be valid; untested, no user file has it), `u8 1` (NOT DECODED; 90 of 108 have 1), `u32 N`,
   `u32 1`, `u64 S`, `u32 1`.
5. **Op**: `u32 2`, `u32 size`, then base: `i32 n1` + McEptr records (below), `i32 0`, `i32 0`, `u16 8` "Extrude\0",
   `u8 0 x4`, `u32 0`, `u64 S`, `u32 0` (op id), `u32 2`, `u32 0x1a`, `u8 0`, `u8 12`; McChainOp: `u8 0`, `i32 nch`,
   `i32 count[]`, `i32 0 x nch`, eptrs, `u8 0 x nch`, `u8 0 x nch`, reversed flags; P (0x70 bytes: type 0, target 0,
   method, distance, vector, bools, draft angle 5 deg in rad, thin 1.0 / 1.0 metric (inch: formats/extrude.md 2.4),
   padding 0); `i32 0`; per chain 0x60: direction + plane (origin, normal, x axis).
   Size check: cube op 0 = 736 (base, 18 records) + 51 + 112 + 4 + 96 = 999 bytes = the stored size.
6. **McEptr records**: one per face and edge of the result, kinds per 3.2 (1 / 4 / 31 per chain entity with ref1 =
   E_i, 19 per vertex of the profile with refs = the two kind-31 records meeting there, 56 x 2 for the caps), op id 0,
   ids = any unique 16-aligned values (e.g. a fake address base + 0x30 k). Alternative: n1 = 0 ([inferred] valid -
   the records are rebuilt by any replay; nothing references them until a toolpath or a later op does).

### 5.2 Extrude Cut / Boss on it
Same op layout with type 0 / 1, P type 1 / 2, P+0x08 target = S, op id k, header counts k + 1; the second record list
and the kind 24 / 45 records of a cut are NOT DECODED well enough to synthesise - write n1 = n2 = 0 for cuts and/or
set the dirty byte so Mastercam regenerates (needs a test on Windows).

### 5.3 Brick (no history)
SOLID_ID + SOLIDXMT + XT as above, ops header (`u8 0, u8 ?, N, 1, S, 1`), one op type 0xe: base (records: one per
face / edge, kind 0x34 / 0 when un-named per `FUN_1802656b0`, or n1 = 0), name [NOT DECODED: the Body op's default
name], then `u32 xt_size` + the XT again. The other way, an empty ops block (count 0), makes the reader delete the
ops file (`sld_read_solids`: "ops == 0 or (not rolled back and count < 1) -> mc_delete_opsfile"): [inferred] then a
plain solid without any history - also acceptable, but not what Mastercam itself writes.

### 5.4 What must be copied / what can be zero
Copy: SOLID_ID +0x48 stamp, op+0x41 = 12, op version 0x1a, draft angle default. Zero: SOLIDXMT padding, SOLID_ID
+0x68 / +0xb0.., op flags, McEptr x28 / bytes, trim faces, chain xc0 / xa8.

## 6. NOT DECODED
- ops header byte +0x0e; op+0xb / +0xc / +0x10 / +0x40 / +0x41 (=12); the per-op u64 list op+0x80 (cuts only).
- McChainOp xa0 per chain (0 / 1).
- McEptr kinds 0x38 / 0x18 / 0x2d producers and their refs; the second record list (kind | 0x10000000); record order.
- Parasolid token names (0x138c, 0x138a, 0x7d2, 0x7d3, 0x1389, 0x16ae, 0x170d, 0x1715).
- Readers of Boolean, Fillet, Chamfer, Revolve, Sweep, Loft, Primitive, Shell, Draft, Trim, Thicken, Pattern, FXF,
  FBM ops (dumped, no data); SldToolPOp's sub-object `FUN_180278610`; the Sweep class name.
- What Mastercam does with a dirty byte of 1 on open in practice, and with n1 = 0 records (no Windows / Wine here).
- How the Solids manager's suppressed state is stored (vfunc 0x118 source field).

## 7. Internet sample parts and the serialiser (added 2026-10-23, task A/C agent)

The sample parts arrived after section 2 was written: `<mastercam-samples>/net` (read-only;
1364 part files, 820 solids extracted to `~/mastercam-re/work/occt/netx/`). Census with tools/sld_hist.py
(`netx/opcensus.py`, `opcensus.txt`; "parsed" = the class has a reader here):

| op | count | files | status |
|---|--:|--:|---|
| Extrude Cut / Extrude Boss / Extrude | 1329 / 388 / 364 | 152 / 96 / 181 | parsed |
| Body (brick) | 334 | 77 | parsed |
| Fillet | 823 | 65 | NOT DECODED (data now available) |
| Revolve Cut / Revolve | 94 / 68 | 18 / 39 | NOT DECODED |
| Chamfer | 65 | 45 | NOT DECODED |
| Boolean Add / Remove | 53 / 33 | 26 / 11 | NOT DECODED |
| Toolpath | 31 | 18 | parsed |
| types 30 / 31 / 34 / 35, Sphere, Loft Boss, Draft Face, Shell, Torus, Block, Trim, Sweep (Cut), Cone, Cylinder | 3..21 each | | NOT DECODED |

Op versions seen: 8 .. 31 (SOLIDXMT solid ver 0x16 / 0x1a / 0x1c / 0x1f / 0x33); the ops header dirty byte is 0 in
every one of the 807 solids read.

Serialiser `viewer/occt/sld_ops.py` (inverse of the readers above): **all 295 extrude ops of the user parts and all
2,081 parsed extrude ops of the samples re-serialise byte-identical**; `spec` / `splice` / `addsolid` write solids
into a copy of a part (formats/occt_xt.md section 6: the end-to-end proof and what to check in Mastercam).
