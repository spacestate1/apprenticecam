# An imported solid in a Mastercam part: the "brick" (SldBodyOp) records (decoded 2026-11-19)

Scope: what Mastercam writes into a .mcx-9 / .mcam for a solid it imported (STEP: mcstep.dll -> mcgeomsld `sld_import_solid`), and how
apprenticeCAM now writes the solids it imports the same way. Rule 1 applies: every fact cites a decompile line or measured Mastercam data;
**NOT DECODED** = unknown, **[inferred]** = a reading that is not proven, **ours** = a choice of this program. Companion docs:
formats/solid_history.md (SOLID_ID / SOLIDXMT / McOpsFile / McOperation base, sections 1 - 3), formats/occt_xt.md (the XT writer),
formats/cad_import.md 3 (the STEP import itself).

Sources (mcgeomsld.dll, Ghidra `~/mastercam-re/ghidra_sld`, headless `-readOnly`, tools/ghidra_scripts/DumpDecomp.java; the dumps are in
`decomp/solid_history/` (gitignored) and in `~/mastercam-re/work/imp_solid/`):

| file | functions (address, first line of the function in the file) |
|---|---|
| `h20_bodyop.c` (new) | `ps_import_solid` 0x18004c3e0 (l.42), `sld_import_solid` 0x18008ac50 (l.228), `nameSolidsAndLevels` 0x1800a5300 (l.389), `sld_get_xmt_name` 0x1800bc860 (l.527), `sld_import_add_mc` 0x1800bd390 (l.561), `FUN_180262b50` (l.765), `FUN_18026e260` (l.789, vtable slot 40), `FUN_18026e420` (l.839, slot 19 size), `FUN_18026e530` (l.883, slot 34), `FUN_18026f800` (l.1102, slot 25 regen), `FUN_18026fa70` (l.1222), `FUN_18026fc80` (l.1313, slot 21 write) |
| `h22_mccolor.c` (new) | `FUN_1800486c0` (l.3-51): the first int of the entity's attribute of definition DAT_1803daa84 |
| `h4_attdefs.c` | `ps_open_parasolids` 0x18004d370: DAT_1803daa84 = **CNC/mcColor** (l.250-256), DAT_1803daa88 = CNC/mcEptrPtr (l.262-268), DAT_1803daa8c = CNC/mcEntinfoPtr (l.275-281), DAT_1803daa7c = CNC/mcEntinfo (l.225-231) |
| `h21_bodyop2.c` (new) | `FUN_180041d20` (l.3), `FUN_180041de0` (l.48), `ps_save_body` 0x18004f780 (l.204), `FUN_180050bb0` (l.281), `sld_init_solid_ent` 0x1800bd5c0 (l.291), `FUN_180265240` (l.348), `FUN_18026e180` (l.374, dtor), `FUN_18026e380` (l.396), `FUN_18026e410` (l.444) |
| `h7_brick_bodyop.c` | `sld_convert_CArray_tags_to_mc_ents` 0x1800b06b0 (l.3), `sld_solid_from_tag` 0x1800cccc0 (l.298), `mc_create_body_op` 0x18002b610 (l.549) |
| `h6_op_readers.c` | McOperation ctor `FUN_180261b90` (l.3), SldBodyOp ctor `FUN_18026e130` (l.41), SldBodyOp read `FUN_18026f4f0` (l.55) |
| `h8_mceptr_entries.c` | `FUN_1802656b0` (l.14: the op's McEptr records from tags) |
| `h2_op_base_extrude_toolp.c` | McOperation write `FUN_180266560` (l.361) |
| `sld_op_vtables.txt` | `SldBodyOp::vftable` @ 0x180309c90: slot 19 `FUN_18026e420`, 20 `FUN_18026f4f0`, 21 `FUN_18026fc80` |
| mcstep.dll, `~/mastercam-re/work/cadimp/dump/step_main.c` | `FUN_18002d480` l.95-102: `reduce_filename(file, dir, name, ext)` then `nameSolidsAndLevels(db_end_before_import, name)` |
| mccore.dll, `decomp/mccore_filename_parts.c` | `build_filename_s` l.216: `reduce_filename(path, dir, name, ext)` - the third output is the name without directory and extension |

Data: the corpus `<mastercam-samples>` (read-only; X9 / 2017 / 2018 parts) and the user's parts `<user-parts>`
(no body ops there). **438 body ops in 371 + 67 solids; 424 have the 64-bit layout (ops version >= 0x19) handled here**, 14 older ones
are not read.

## 1. The import chain (what makes the records)

STEP: `FUN_18002d480` -> ... -> `sld_import_solid` (h20 l.228): `PK_BODY_check`, `FUN_18008a370` / `FUN_180088cb0` (repair / colour, NOT
DECODED here), `PK_TOPOL_delete_redundant`, optional `bs_body_clean`, then **`sld_import_add_mc(body, 0)`** (h20 l.370). That
(h20 l.561-579) moves the body to a new partition (`FUN_1800414f0`, `FUN_18004cfe0`), makes the DB entity **`sld_solid_from_tag(body,
false)`** (h7 l.303-354: `sld_init_solid_ent` - solid id N = the counter DAT_1803e80cc, a new McOpsFile, flags u16 1, name empty
(`param_1[0x50] = 0`, h21 l.318-336), `sld_compute_bbox`, **`dbput_solid`** = the SOLID_ID record on the main level with the solid colour,
formats/solid_history.md 8 SOL-5d) and then **`mc_create_body_op(solid)`** (h7 l.555-602):

1. `ps_import_solid(body, &n, &tags)` (h20 l.46-132): `PK_BODY_ask_faces`, `PK_BODY_ask_edges`; n = edges + faces; the tag list is **every
   edge, then every face**; each edge gets mcEntinfo `FUN_180050bb0(edge, 0, 0, 0, 0)` (l.104), each face `FUN_180050bb0(face, 0x34, 0, 0,
   0)` (l.116) - kind, op id, ref1, ref2 (`FUN_180050bb0` -> `FUN_180050bd0`, the mcEntinfoPtr setter, formats/solid_history.md 3.1).
2. `op = new SldBodyOp(opsfile)` (0x198 bytes; ctor `FUN_18026e130` h6 l.41-52 over `FUN_180261b90` h6 l.5-38): type +0x30 = 0xe, the
   body tag +0x190 = 0, the XT key +0x90 = "", McOperation: name +0x20 = `calloc(1, 1)` = "", op id +0x34 = the ops file's +0x14 (0 for
   a new solid), +0x3c = **0x1c**, u16 +0x40 = **0x0c00** (+0x40 = 0, +0x41 = 0x0c), every flag and list 0.
3. `op+0x18 = solid` (h7 l.592).
4. `FUN_1802656b0(op, n, tags)` (h8 l.14..): the n1 records, one per tag in tag order (section 2.1).
5. `FUN_18026fa70(op, body)` (h20 l.1227-1310): `PK_ENTITY_copy(body) -> op+0x190`; on the copy every edge gets mcEntinfo (0, 0, ...)
   and every face (0x34, 0, ...) again (l.1271-1294); the key op+0x90 = `sld_get_xmt_name` ("sld_%d", N) + **"_bo"** (l.1295-1299).
6. `FUN_180262b50(op)` (h20 l.767-773): `FUN_180265240` (resolves bit-27 refs: none here) and `FUN_180267170` (append to the ops file +
   the Solids manager row).
7. `sld_regen_solid(solid, 1)` (h7 l.598): the history replays (the Body op's regen `FUN_18026f800`, h20 l.1104-1219: copy the stored
   body, rename, op+9 = 0) and clears the regen flag - so the saved header byte +0x0c is 0, as in every corpus solid.

Afterwards mcstep calls `nameSolidsAndLevels` (section 5). The other caller of `mc_create_body_op`, `sld_convert_CArray_tags_to_mc_ents`
(h7 l.240-241, Solids > "convert" paths with `param_3`), makes the same op.

## 2. The SldBodyOp record (op type 0xe)

Read `FUN_18026f4f0` (h6 l.55-150), write `FUN_18026fc80` (h20 l.1317-1389), size `FUN_18026e420` (h20 l.843-880 = the base size + 4 + the
XT file's size):

```
McOperation base (FUN_180266560, formats/solid_history.md 2.3; ops version >= 0x19, 64-bit)
u32 xt_size
xt_size bytes  the XT of the op's own body copy (op+0x190), PK_PART_transmit via ps_save_body(op+0x190, temp, "sld_N_bo", version, text)
```

`ps_save_body` (h21 l.204-279) transmits a copy of the body (`PK_ENTITY_copy`, `PK_PART_transmit`, options 3 / 1 / text when version
< 0xf); the Parasolid version is **`FUN_18026e380(op, ops version)`** (h21 l.396-442): 0x1a -> 0x10e (27.0), 0x1c -> 0x118 (28.0), ... - the
same table as the SOLIDXMT XT (formats/solid_history.md 1.2).

### 2.1 The base of a new body op (all from section 1)

| field | value | source |
|---|---|---|
| n1 records | one per edge, then one per face, of the body: **kind 0 (edge) / 0x34 (face), op = the op's id (0), ref1 = ref2 = 0, x28 = 0; b2c = 1 and b2d = the colour when the face has a CNC/mcColor attribute, else 0 / 0** | `ps_import_solid` order + entinfo (h20 l.100-121); `FUN_1802656b0`'s entinfo branch (h8 l.123-137): kind / op / refs from the entinfo (`FUN_180049510`), +0x20 / +0x28 = 0, +0x2c = `FUN_1800486c0(tag, &c) != 0`, +0x2d = c; `FUN_1800486c0` = the tag's CNC/mcColor int (h22 l.3-51, h4 l.250-256) |
| record order | edges (`PK_BODY_ask_edges`), then faces (`PK_BODY_ask_faces`); the faces' order = the XT's shell face chains (SHELL.face -> FACE.next) in every op where it shows (the 7 parsable ops with coloured records; XT node order fails on 2) | data |
| record ids | the records' own addresses: one block `malloc(n * 0x38)`, records 0x30 apart after n pointers (h8 l.47-59) - **consecutive, 0x30 apart** | data: 424 / 424 ops |
| n2, n3 | 0 | ctor (h6 l.20-27); data 424 / 424 |
| name | "" (u16 1 + one 0 byte) | `FUN_180261b90` `calloc(1, 1)`; data **424 / 424 body ops have the name ""** |
| +9..+0xc, +0x10 | 0 | ctor; `FUN_18026f800` clears +9 / +0xa after its regen (h20 l.1129, 1204); data 424 / 424 |
| +0x18 | the SOLID_ID's eptr | h7 l.592; data: = the ops header's eptr in 424 / 424 |
| +0x34 / +0x30 | 0 / 0xe | ctor; data 424 / 424 (the body op is op 0 of its solid in all 424) |
| +0x3c | op version: the ctor's 0x1c in Mastercam 2017 | `FUN_180261b90`; written as stored (`FUN_180266560` l.~165: `Write(op+0x3c, 4)`); data: 8 .. 31 = the version of the Mastercam that imported it |
| +0x40 / +0x41 | 0 / 0x0c | ctor u16 0xc00; data 424 / 424 |

Header (McOpsFile, formats/solid_history.md 2.1): +0x0c 0, +0x0e the Solids manager row's open state (0 or 1 in the data), N, position =
count = 1 (371 of the 424 body-op solids have only the body op: `mc_is_entity_a_brick`; the other 53 have later ops: Boolean Add 23,
Fillet + Thicken 24, Fillet 5, Toolpath 1).

### 2.2 The op's XT (`FUN_18026fa70` + `ps_save_body`)

The op XT is a second transmit of the same body (op+0x190 is a copy of the solid's body made at import), with:

- header `KEY=sld_N_bo;` `FILE=SLD_N_BO.X_B;` (Parasolid 27 / 28 upper-case the file name; Mastercam 2018's Parasolid 29 keeps the key's case
  for both XTs), the other header lines as the SOLIDXMT XT's (304 / 317 parsed pairs; DATE equal in 301 / 317: one save);
- the same schema as the SOLIDXMT XT (317 / 317);
- an extra attribute definition **CNC/mcEntinfoPtr** (`ps_open_parasolids`: fields 0x170d, 0x170d, 0x1715, 0x1715 = int, int, pointer,
  pointer; in the XT `type_id 9000, actions [0,0,0,0,3,6,0,0]`, the owners of mcEptrPtr, `fields [1, 1, 9, 9]`), **last** in the BODY's
  attribute-definition list (317 / 317);
- on **every edge and face** an mcEntinfoPtr ATTRIBUTE, **first** in the entity's attribute chain, then mcEptrPtr (then any others), with
  INT_VALUES [0] [0] on an edge and [0x34] [0] on a face and both pointer fields empty (18,961 edges + 7,638 faces of the 317 parsable
  op XTs, none other).

The SOLIDXMT XT of the same solid has no mcEntinfoPtr: `FUN_180041d20` / `FUN_180041de0` (h21 l.3-88) delete the entinfo attributes from the
solid's body before it is saved (formats/solid_history.md 3.1); the op's copy keeps them.

### 2.3 Proof: `tools/sld_bodyop_check.py`

Regenerates every body op from the rules above, with only these inputs taken from the stored op: the first record id (a heap address),
the op version (+0x3c: which Mastercam made it), the XT bytes; the record counts come from the op XT's EDGE / FACE nodes (tools/xt_parse.py
with `~/mastercam-re/files/sch_<n>.s_t`), the eptr from the ops header. It also checks the XT rules of 2.2. Run 2026-11-19:

```
body ops 438: readable (ops version >= 0x19) 424, old 32-bit layout (not handled) 14
regenerated byte-identical 411 / 424 (edge / face counts from the XT: 310 / 317; no schema file, counts from the stored records: 101 / 107);
differ 13 (of which in a record's x28 / b2c / b2d bytes: 13)
```
(first run, b2c / b2d written 0). With b2c / b2d from the faces' CNC/mcColor (`FUN_1800486c0`), final run:
```
body ops 438: readable (ops version >= 0x19) 424, old 32-bit layout (not handled) 14
regenerated byte-identical 424 / 424 (edge / face counts from the XT: 317 / 317; no schema file, counts and face colours from the stored
records: 107 / 107); differ 0
ops with coloured faces in the XT: 70 (records = the XT's CNC/mcColor: 13; records uncoloured = faces coloured after the import: 57)
op XT rules (FUN_18026fa70): ok 424, violated 0 (XT not parsed for lack of a schema: 107)
```

Faces coloured in the XT come in two states and no op mixes them: the records carry exactly the XT's colours (7 parsable ops + 6 of 2018),
or no colour at all (57 ops) - [inferred] the faces were coloured after the import (the records are made once, at the import; a later face
colour changes the body, not the records). For the 57 the check falls back to uncoloured records (one bit of input). The 107 XTs without a
schema are Mastercam 2018 (SCH_28101): record counts and colours there come from the stored records, so only the other fields are proven.
An import here has no face colours (STEP colours NOT DECODED), so its records have b2c = b2d = 0. `tools/tests/sld_ops_test.sh` also reads and re-writes every body op through `viewer/sld_ops.c` (class
`SLD_CLS_BODY`) byte for byte.

## 3. The XT writer option (viewer/occt/occt_xt.cpp)

`occxt::Options::mc_entinfo` writes 2.2's definition and attributes (the ATTRIB_DEF after mcEptrPtr's, last in the list; per edge / face
an ATTRIBUTE + two INT_VALUES, prepended to the chain). `ocs_xt_ex(body, scale, pk, key, date, entinfo, ...)` (viewer/occt_solid.cpp) also
returns the EDGE / FACE counts written, which are the op's record counts.

## 4. The writer here (viewer/mcam_saveops.c, core_ui cui_sldsave.c)

An imported solid (`F.solids[i].brep`, cad_import.c) goes into the part at every part save as a brick: `sld_new_build` adds it as a
`SldNew` with `imported = 1` and one `SLD_CLS_BODY` op (type 0xe, name "", id 0, +0x40 / +0x41 0 / 0x0c); `mcam_save_part` then writes,
as for the solids made here (formats/solid_history.md 8):

- the SOLID_ID record (level / colour of the import = main level / system solid colour, cad_import.md 3; box; N = an unused id);
- section 13: SOLIDXMT, the XT (`ocs_xt_ex(key "sld_N", entinfo 0)`), N, the description (section 5), the ops block: header (+0x0c 0,
  +0x0e the row state, N, 1, eptr, 1) and the body op: n1 = the XT's edges (kind 0) then faces (kind 0x34), op 0, no refs; ids 0x30 apart
  (ours: base 0x10000000, any unique values; formats/solid_history.md 8); +0x3c = the file's ops version (0x1a X9 / 0x1c 2017, the rule
  of every op written here, formats/solid_history.md 8); then the op XT `ocs_xt_ex(key "sld_N_bo", entinfo 1)` with the same DATE.

The OCCT shape written is the one imported (OCCT's STEP transfer; Mastercam's own `PK_BODY_check` / repair / `bs_body_clean` are not
reproduced - cad_import.md 3).

The part a STEP-opened document saves to is a Mastercam **2017** part (.mcam; major 0x13, solid ver 0x1c, Parasolid 28): File > Open of a
STEP file starts a new part (`FUN_180345830`: `init_db` then DoImport, cui_doc.c open_cad, which names the document .mcam) and a new part's
base here is the 2017 operation defaults library (mcam_saveops.c `new_base`). The X9 layout (0x1a, Parasolid 27) is written when the solid
goes into an X9 part (tested through `mcam_save_part` on a copy of an X9 part).

### 4.1 Round trip (`tools/tests/sld_import_test.sh`, run 2026-11-19)

`<data-disk>/CAM-posts/pcr-rack-96-10mm-thin.stp` (Mastercam X9 STEP export, one solid): OCCT volume 275615.16 mm3, box 79.5 x 113.5 x 38.
1. App, headless: `open:` the STEP, `export:` part -> 146 new entities (145 + the SOLID_ID), "1 solid(s) ... written - 0 not written".
2. `sld_bodyop_check.py` on the written part: 1 / 1 byte-identical, XT rules ok; sld_hist: sld_0 "pcr-rack-96-10mm-thin", ops header
   (0, 0, 0, 1, eptr, 1), one Body op, name "", 258 records (136 edges kind 0, 122 faces kind 0x34), +0x3c 0x1c, +0x41 12.
3. Read back: the SOLIDXMT XT and the op XT each rebuilt in OCCT: BRepCheck valid, volume 275615.16 (relative difference 1.1e-15).
4. X9 path: the same solid added to a copy of <user-parts>/A001.mcx-9: brick sld_8, ver 0x1a, both XTs valid, same volume.
5. Both written parts reopen in the app (`open:` -> solids=1 / solids=2).
Also: `sld_ops_test.sh` (C reader / writer, viewer/sld_ops.c): body ops 424 / 424 byte-identical (extrude 2,988 / 2,988, fillet 858 / 858
unchanged); `sld_kernel_test.sh` OK.

## 5. Name and level (`nameSolidsAndLevels`, h20 l.396-524)

`FUN_18002d480` (mcstep step_main.c l.95-102) calls it when solids were imported (`param_4`) and the DB grew, with the DB end before the
import and the file's base name (reduce_filename's name output: no directory, no extension). It counts the new SOLID_ID entities, then for
each in DB order: nm = the name, plus `" (v)"` when more than one solid (l.469-495); if the solid's level has no name (`get_levname` fails,
"" or " ", l.496-497) the level is named nm (`LevelData::setName`, l.498) and the description becomes nm when it is empty or equals string
resource 0x310 (l.499-511; a new solid's description is empty: `sld_init_solid_ent`), and v + 1 (l.513); **if the level is named, the
description becomes the level's name** (l.517-518). v counts only the first case, so two solids on one unnamed level give "base (1)" for
both (the level is named by the first). The description is SOLID_ID +0xb8 (`sld_set_description` 0x1800cbac0, at most 0x100 bytes,
`~/mastercam-re/work/cadimp/dump/sld_import.c` l.2117-2156) = the SOLIDXMT trailer name. Ported in cad_import.c (until 2026-11-19 the
description was always the file name).

## 6. Status, NOT DECODED

- **NOT DECODED**: string resource 0x310's text (only compared
  against an empty description here, so not needed); the STEP colours (`FUN_180088cb0`, `sld_convert_CArray_tags_to_mc_ents`' colour paths,
  `FUN_1800a0ab0` "SDL/TYSA_COLOUR") - imported solids keep the system solid colour (cad_import.md 3); the old 32-bit body-op layout (14 ops).
- Not ported: a toolpath on an imported solid (the solid gets its SOLID_ID only at the save, so a toolpath op of the same save cannot
  reference it); later ops on an imported solid (Boolean, Fillet ... on a brick).
- No written file has been opened in Mastercam (no Windows here): formats/windows_checklist.md.
