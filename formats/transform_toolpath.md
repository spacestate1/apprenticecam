# Transform toolpath (Toolpaths > Transform, opcode 4) - Mastercam 2017

Code: `viewer/cam_xformop.{h,c}` (engine: the transform op's NCI section from its source ops' NCI), oracle
`tools/tests/xformop_test.{c,sh}`. Decompiles (Ghidra headless, read-only, 2026-10-26) in
`decomp/xformop/` (gitignored): `xform_assoc.c`, `bncixform.c`, `core_range.c` (mccore 0x18013d000-0x180151200, the
CBnciXfm engine), `core_range2.c` (0x180151200-0x180158000, CBnciXfmCalcXm / CBnciXfmCalcTransformOperationXM),
`helpers.c` / `helpers2.c`, `mc_view.c` / `mc_ops.c` (mastercam.dll find_view_m, view_to_world, Plane::Transform ...),
`sys_ops.c` (systypes operator*), `wo3.c` (WorkOffsetUtil::GetNumberingMode), `mill_pages.c` / `mill_base.c` (mcmill
dialog pages), `cc_*.txt` / `vx_dis.txt` (register arguments the decompiler lost), `run.sh` (the headless command).
`clean.py` collapses the unrolled struct copies for reading (check the raw file before relying on a collapsed block:
`local_1795 = 1` in FUN_180140f50 hid in one).

## 1. Entry and flow
- Menu 3630 -> mcmill `xform_toolpath_dialog` @1801a2640 -> mccore `xform_assoc` @18014efb0 (`xform_assoc.c`): the selected
  ops become `BnciXformPreSelectedOperations`, `init_operation(&cur_op, 4 (0x50 wire))`, `edit_operation` (the dialog,
  mcmill `run_xform_sheet` @18024d760 -> FUN_18025f890 property sheet), `bnci_section(op)`.
- `bnci_section` of an `op_is_xform` op (4 / 0x50 / 0x6c / 0x9a) -> `BnciXform(op id)` @18013f900 -> (C-hook "bncixf" if
  loaded) FUN_18013fb40: context ctor FUN_18013dac0, FUN_1801482e0 (copy of the op at ctx+0x2cf8, so prm = ctx+0x3eb8;
  the flags below), setup FUN_1801422b0, then FUN_180144480 (**CBnciXfm::CreateBnci**, the NCI) when ctx+0xa604 < 0,
  else FUN_1801464b0 ("Create new operations"); the -2 / -3 records around it are bnci_section's.
- Defaults: `init_operation_xform` @18004cdb0 (copies the previous transform op's prm, else per type below).

## 2. Parameters: prm_xform = op +0x11c0 (0x2b8 bytes; part record +0x1228)
Dialog pages (mcmillres.dll, `tools/pe_dialogs.py`): 606 "Type and Methods" (CXformPg1, save FUN_180261b60, base
CXformPg1Base FUN_1802834a0 / DDX FUN_180282fa0 + FUN_180260d30), 608 Translate (CXformPg2), 612 Rotate (CXformPg3, save
FUN_1802659b0), 614 Mirror (CXformPg4). Page 2 is chosen by +0x00 (FUN_18025f890).

| prm | control / meaning | source |
|---|---|---|
| +0x00 i16 | Type: 0x10 Translate (25478), 0xd Rotate (25479), 8 Mirror (25480) | FUN_180261ae0, FUN_1802834a0 |
| +0x02 i32 | 0 | init_operation_xform |
| +0x06.. | (op +6) comment 1113 | FUN_1802834a0 |
| +0x17 | Work offset numbering By instance (3370) = 1, By toolplane (3371) = 0 | FUN_180261b60 |
| +0x18 | Custom parameters (1651) on: 1031 / 1032 values ints +0x1c..+0x28, doubles +0x30..+0x48 | FUN_180262060, FUN_1802620c0, CreateBnci |
| +0x50 | Remove comments (3338) | FUN_1802834a0 |
| +0x51 | Source: 0 NCI (3331), 1 Geometry (3332) | FUN_180261b60 |
| +0x52 | Keep this transform operation (2140) (with +0x67) | FUN_180261b60 |
| +0x54 i16 | Work offset numbering 0 Automatic (2132), 1 Maintain source's (2133), 2 Assign new (2134); with +0x60 -> 3 | FUN_180261b60, GetNumberingMode @mastercam 1804477d0 |
| +0x58 / +0x5c i32 | Start (2137) / Increment (2138) | FUN_180261b60 |
| +0x60 | Match existing offsets stored in planes (2139) | idem |
| +0x61 | Unique subprograms (1458) | idem |
| +0x62 | Disable posting in selected source operations (2129) | idem |
| +0x63 | Copy source operations (2089): the untransformed instance is output too | idem; CreateBnci |
| +0x64 | Group NCI output by: 1 Operation type (3097), 0 Operation order (3098) | idem; BuildRunList |
| +0x65 | Incremental (1644) / Absolute (1643) | FUN_1802834a0 |
| +0x66 | Save planes (2146) (Tool plane method) | FUN_180261b60 |
| +0x67 | Create new operations and geometry (1451) | idem |
| +0x68 | Method: 0 Tool plane (1896), 1 Coordinate (1897) | idem |
| +0x69 | Subprogram (1463) | FUN_1802834a0 |
| +0x6a | Include origin (2145) (Tool plane method) | FUN_180261b60 |
| +0x70 i16 | page method: Rotate 0xe about the origin / 0xf about a point (FUN_1802659b0); Translate 0x11 Rectangular, 0x12 Delta (polar), 0x13 Between points, 0x14 Between planes; Mirror 9 X, 10 Y, 0xb line, 0xc two points, 0x15 polar (FUN_1801482e0, Init) | |
| +0x72 | the page's plane group box checked (Rotation / Mirror plane; Translate: planes) | FUN_1802659b0; Init |
| +0x74 / +0x76 i16 | # instances (Rotate; Translate X / Y) | Init |
| +0x78 3d | Rotate centre (method 0xf); Translate / Mirror points (+0x78 .. +0x90) | Init |
| +0x90 f64 | Rotate angle step | Init (this+0x1280) |
| +0x98 9d, +0xf8 view | Rotate plane matrix | Init (this+0x1288) |
| +0x101 | Rotate "Total sweep" (step / #) | FUN_1802659b0 (+0xd140), Init |
| +0x102 | Rotate method Translate (CalcRotateT); the page writes 0 | FUN_1802659b0 |
| +0x111 | Mirror: subprogram allowed | op_is_subpgm |
| +0x120 f64 | Rotate: angle of the first rotated instance (the page's angle combo +0x10f80) | FUN_1802659b0, CalcRotateR |
| +0x2b0 | Include WCS (2147) | FUN_180261b60; FUN_18014dd60 |

Defaults (init_operation_xform): Mirror +0x70 = 10, +0x72 = 1, +0x110 = 0x101, +0x130 = 45; Rotate +0x70 = 0xe, +0x74 = 3,
+0x90 = +0x120 = 90; Translate +0x70 = 0x11, +0x74/+0x76 = 2/2, +0x78 = +0x80 = 1, +0xb8 = 1, +0xc0 = 45, +0x228 = +0x230 = 1,
the plane matrix at +0x118 = identity; the type's plane = the active Tplane (+0x60 its view); +0x54 = lathe bit; +0x67 = 0,
+0x68 = (type == 8), +0x69 = 0.

Context flags (FUN_1801482e0): a600 = +0x68, a606 = +0x64, a604 = -1 / 0 / 1 (new ops, + keep), a602 = -1 / 0 / 1 (+0x63,
+0x62), a608 = GetNumberingMode, 0x5670 = +0x50, 0x5671 = +0x63 && +0x62, 0x5664 / 0x5668 / a5fe per type (Rotate 1 / 1 /
8; Translate 0x5668 = 3, 0x5664 = 3 or 1 with +0x72 (0x14: 1), a5fe = 0 / 2 / 4 / 6 (+1 with Include origin) for
0x11..0x14; Mirror 0x5664 = 0, a5fe = 9 / 10 / 12 / 13 / 11 for 9 / 10 / 0xb / 0xc / 0x15).

## 3. Source operations
- TP_ID records of the transform op with ent +0x90 = 0x17 (23), +0x98 = source op id (FUN_180140bd0 via
  CDependentOperations::Detect(op, 4)); in Operations Manager order (SortIDsByOpMgrTreeOrder, not for nesting).
  Measured: CHAIN BREAKER (2017) one ref +0x98 = 1; TUTORIAL 9 (2018) two refs, op id at +0xa8.
- Setup FUN_1801422b0 reads each source's NCI (1018 / 1019 removed), splits it into sections per tool / construction /
  WCS plane set (1013 / 1014 / 1016 / 1017 / 1027), each section (CBnciXfmSection, 0x2d0) with its instance list.

## 4. Instances (CBnciXfmCalcTransformOperationXM::Init @180152a80; x_matrix = 9 doubles rows + translation)
- Per source op its view: op +0x572 when `!op_has_no_view && op[0x570]`, else find_view_m of its first 1014; the target
  view = find_view_m(the page plane) when +0x72, else the same. ViewNumberOK: 1 .. n_views - 1.
- **Rotate** (0xe / 0xf): `CalcRotateR(# = +0x74, step = +0x90 (/ # with +0x101), start = +0x120, view, centre = +0x78 if
  0xf else 0)` @180151660: # + 1 matrices, angles 0, start, start + step, ...: in the view R = [[c, s, 0], [-s, c, 0],
  [0, 0, 1]], t = (s cy + (1 - c) cx, (1 - c) cy - s cx, 0) (c = the centre in view coordinates); ViewXmToWorld @180154cc0:
  view < 2 as is, else M = V^T . R . V, t = V^T t (operand order from `vx_dis.txt`; systypes `operator*(a, b)` = b . a,
  `sys_ops.c`). Rotate method Translate (+0x102) = CalcRotateT with the NCI extents (not ported).
- Init drops the first (identity) matrix unless +0x63, but FUN_180140f50 calls it on a copy with op[0x1223] = 1, so the
  section keeps all # + 1 (FUN_18013f150: make_reals_0 on the 12 doubles: |x| < 1e-9 -> 0).
- **Translate Rectangular** (0x11): from view = the source's (prm +0x111 "From plane": +0x118 matrix, not ported), to view
  = find_view_m(prm +0x180) with +0x72, else the same; vec = prm +0xa0 - prm +0x88 (VectorSubtract3D @180154c40);
  CalcTranslateDataRectangular @180152450: grid (i dx, j dy, 0), j (Y, +0x76) outer, i (X, +0x74) inner, dx = +0x78,
  dy = +0x80 (/ # with +0xc9 Total distance), # < 1 -> 1; CalcTranslateXMs @1801525e0: p0 = view_to_world(grid[0], from),
  per point p = view_to_world(grid[k], from), Plane::CalcXmatrix(from matrix, p0, to matrix, p) @mastercam 1803d7b90:
  M = F^T . T (operator*(T, F^T), `cxm_dis.txt`), t = p - p0 . M, plus vec when |vec| > tol (this +0xa8 = 0,
  ZeroMemberVariables); sorted grid (+0xc8, SortGridPts) not ported. The identity (grid[0]) is kept the same way
  (FUN_180141ff0 sets op[0x1223] = 1 on its copy). ctx 0x5664 = 3 (1 with +0x72).
- Delta / Between points / Between views (Init 0x12..0x14), Mirror (CalcMirrorX / Y / A / Line / Points): read, not ported.

## 5. Run list and the NCI (CreateBnci FUN_180144480)
- BuildRunList FUN_180141400: +0x64 set: for each source op (Manager order), each instance; else for each instance
  number, each source op. The instances are numbered from 1 per section (run +0x1e0); run +0x1e4 = (+0x63 Copy source).
- Per run: **number 1 (the identity) is skipped unless +0x63** (CreateBnci, `plVar32[0x78] != 1` / `[0x79]`).
- Each run writes: **999** (+0x18 source op id, +0x1c its opcode, +0x1e op[0x11b8]); then the source's section records
  (ReadSection; 0x38f / 0x390 removed; Remove comments: a further list, not decoded) each with +4 = the transform op id,
  through `FUN_18014a290(rec, x_matrix = run +8, view, source op id)` [cc_gen.txt]:
  - **1016**: +0x18 = the transform op id, +0x78 (+0x68 when rec[0xc] & 2) = the source op id; work offsets
    FUN_1801470f0: +0x42 < -1 -> -1; mode 0 (Automatic) -> -1; mode 1 keeps it; modes 2 / 3 assign / match (not ported).
  - before the **1020**: the source op's comment (op +6, 0x78 bytes) as a **1008** (+0x18 text) once per run when the
    control's OutputOpComment (CCNCMachine) and not Remove comments; after the 1020: **1031** (0x407: +0x18 4 doubles prm
    +0x30.. with custom parameters, else 0), **1032** (0x408: 4 ints prm +0x1c..), then **1018** (first 1020 of the run).
  - end of the run: **1019**.
  - Method Tool plane (a600 = 0) = **TransformBnciLineByToolplane** FUN_18014d300:
    - 1014: AdjustNciMatrixAndOrigin FUN_18013f3b0 on its matrix with the saved 1013's origin (+0x38, view coordinates) /
      view (+0x32); then the saved 1013 is rewritten at its file position (the 1013 gets the new view and origin).
    - 1017: the same with the saved 1016's +0x28 origin / +0x20 view; the saved 1016 rewritten.
    - 1027: only with Include WCS (+0x2b0): its matrix and +0x60 origin.
    - 83: view_to_view (not ported). Every other record: written as is when Include origin (+0x6a), else
      **TransformBnciLineByCoordinate** FUN_18014a300 (below).
    - Saved records (1013 / 1014 / 1016 / 1017 / 1027) are reset per run (FUN_180148970).
  - AdjustNciMatrixAndOrigin: view = find_view_m(matrix, create); world origin = view_to_world(origin, view); Plane;
    **Plane::Transform** @mastercam 1803df750: identity matrix and |t| < tol -> unchanged, else matrix = plane . M
    (MultiplyMatrix33 [cc_mm.txt] + OrthoNormalize), left-handed -> row 0 negated, Include origin -> origin = xform_pt;
    view = GetViewNo (find / add); origin = world_to_view(world origin, new view).
  - TransformBnciLineByCoordinate for the point records (FUN_180132ca0, mill: G0 / G1 / 20 / 21 / 30 / 31 +0x20; G11
    +0x18, +0x30, +0x58; 81 +0x20, +0x88; 83 +0x18, +0x30; 100 +0x20, +0x58; 1002 +0x50): world = view_to_world(p, source
    1013 view) + view_to_world(its origin) (BnciWorldPoints FUN_180128890), p' = xform_pt(world, M) - the new 1013 origin
    (world), local = world_to_view(p', the new 1013 view). Arcs (constr_ns_arc / xform_ent / direction rules), 4-axis
    arcs, 201 and 1015 take other branches (not ported). Records without points are written as is.
- **1018** (bnci_wr_1018 @18008f440, struct at record +0x18; global subpgm_n1018 zeroed by set_n1018): +0x1c transform op
  id, +0x20 source op id, +0x28 i16 instance number, +0x2a i16 = 2 unless subpgm_on (op_is_subpgm of the transform op:
  +0x69 && !+0x67 && (type != 8 || +0x111)), +0x30 i16 = run +0x1e4, +0x32 = +0x65 (incremental), +0x34 i16 = ctx 0x5664,
  +0x36 i16 = 2 with Coordinate, 1 with Tool plane + incremental, else 0 (Mirror: a5fe - 9 / 1 / 2 rules), +0x38 the
  instance matrix, +0x80 its translation (0x5664 != 0; else prm +0x110.. in the plane view), +0xb8 0.
  **1019** (bnci_wr_1019 @18008f590): +0x18 0, +0x1c op id, +0x20 source op id, +0x28 number, +0x2a 2 / 0.
- Records are written with bnci_write_to_disk (+0x10 position, +0x14 bnci_bytes; +4 = cur_op if 0); +8 keeps the
  source's value.
- find_view_m @mastercam 180337b10: views 1 .. n_views in order, mx33_match (each element within 2e-7, mccore @180017880);
  not found -> AddMCView @18031df40 = view n_views + 1.

## 6. Ported (viewer/cam_xformop.c)
`xfop_section(op, sources, views, opts)` -> the -2 .. -3 section (2017 layout); `xfop_from_part(McamFile, index)` reads
the sources and their stored NCI from a 2017 / X9 part. Ported: Type Rotate (0xe / 0xf, any view) and Translate
Rectangular (0x11, any from / to view; **no oracle**: no corpus Translate has stored NCI - a self-check runs the TUTORIAL 9
sources through a 2 x 2 grid, `XF_SYNTH_TRANSLATE=1 tools/tests/xformop_test.sh`: 1018 translations (100,0) (0,50)
(100,50) per source, identity skipped), Method Tool plane,
Source NCI, Group by op type / operation order, Copy source operations, work offsets Automatic / Maintain, Include
origin on (records copied) or off (point records transformed), Include WCS, custom parameters, subprogram flag in 1018.
The control's OutputOpComment is an input (`XfOpts.output_op_comment`: where Mastercam stores it is not decoded).

Refused with the reason (never approximated): opcodes 0x50 / 0x6c / 0x9a; Create new operations (+0x67); Method
Coordinate (+0x68); Source Geometry (+0x51); Remove comments (+0x50); Type Mirror; Translate other than Rectangular, From
plane (+0x111), sorted grid (+0xc8); Rotate method Translate;
work offset Assign new / match planes; sources that are transforms, manual entries, have 1018 / 1019, or more than one
tool plane; arcs / 4-axis arcs / 201 / 1015 / 83 when the motion must be transformed (Include origin off); a source
whose op +0x572 view differs from its 1014's (op_is_solid_ctrl not decoded).

## 7. Oracle (2026-10-26, `tools/tests/xformop_test.sh`)
Corpus (`<mastercam-samples>/net`, every part scanned for opcodes 4 / 0x50 / 0x6c / 0x9a): 3 parts with
a Transform op - CHAIN BREAKER 2017 / X9 (Translate rectangular 2 x 2, **no stored NCI**), TUTORIAL 9 (2018 Multiaxis
Essentials, op 3: Rotate 3 x 90 degrees about the origin, Top, Tool plane, Include origin off, group by op type, two Flow
5-axis sources, 12849 + 523 G11 moves). TUTORIAL 9 is a **Mastercam 2018** file: the test reads it with the measured
2018 layout (records: +0x10 int64 position, +0x18 byte count, fields from +0x20 = 2017's +0x18; OP_ID offsets >= 0x88
8 further, the TP_ID source op at +0xa8) and compares in 2017 layout.

Result: **40348 / 40352 records byte-equal** (all bytes but +0x10 and the -2 / -3 time stamps; +0x14 = stored size),
gcode sequence identical (6 runs: 999, 1011 .. 1029, 1008, 1020, 1031, 1032, 1018, 20001 .. 1002, the G11 moves, 1019),
all 40116 G11 moves identical. The 4 others: the first copy of each source, 1013 +0x32 / 1016 +0x20 = the view of the
90-degree plane: Mastercam numbered it **55**, ours 53 = the part's highest stored view (52) + 1. Mastercam's session
had views 53 / 54 that the part does not store (the views find_view_m adds during load / other operations' regen are
not decoded: 2018 planes archive v5 is not parsed either), so the number of an added view is NOT DECODED; matching
views (9, 18) agree.

## 8. NOT DECODED / NOT PORTED
- The number an added view gets (session view table, section 7); Matrix33 tolerance in Plane::Transform's identity
  test and GetViewNo (taken as find_view_m's 2e-7); OrthoNormalize (exact rotations need none; a non-orthonormal
  product is counted).
- OutputOpComment's storage; the Remove comments gcode list (FUN_180144480 local_3938); op_is_solid_ctrl.
- Translate Delta / Between points / Between planes, From plane, SortGridPts; Mirror
  (CalcMirror*, cutter compensation / direction flips FUN_18008d4b0, arc direction rules), Rotate method Translate
  (CalcRotateT, CInterrogateMastercamFile::CalcBnciExtents), Method Coordinate (FUN_18014a300 for every record incl.
  1013 .. 1027), arcs in TransformBnciLineByCoordinate, work offset Assign new (WorkOffsetUtil map, FUN_18011ad00),
  Create new operations (FUN_1801464b0), Source Geometry (FUN_18014a0a0), nesting (0x6c), transforms of transforms
  (FUN_180147760 / FUN_180147e50), several plane sections per source, Save planes (FUN_180146a00: planes added to the part).
- No corpus part has stored NCI for Translate / Mirror or a 2017-made Transform: Translate Rectangular is ported from the
  decompile alone (no oracle), Mirror and the other Translate methods have none either.
