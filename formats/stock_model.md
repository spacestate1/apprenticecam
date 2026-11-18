# Stock Model (op 139 = 0x8b) and Restmill (op 107 = 0x6b): what is decoded, what is ported (2026-11-08)

Sources: `decomp/mcmill_all.c` (mcmill.dll, the dialog pages); mccore.dll dump `~/mastercam-re/work/sm107/out/core_stockmodel.c`
(CStockModel, regexp `.*StockModel.*`, not checked in, addresses below); `decomp/mckernel_all.c` (mk); `decomp/mcmsurf_all.c` (ms);
mcmillres dialog 25600 / 25601 (tools/pe_dialogs.py).

## 1. Corpus counts (scan of every .mcam / .mcx* under <user-parts>, <cad-files> and ~/mastercam-re/files, 1477 files, 1 unreadable)
- **Stock Model op 139: 1** (caminstructor/Benchmark 3_0 for 2017.mcam op 5, name "DDD"; the file has no NCI at all).
- **Restmill op 107: 0.**
- **TP references of kind 0x2c (44, TP_STOCK_MODEL_RESULT), the stored result: 0.** No stored stock model mesh exists anywhere.
- Coverage limit: only files the record walker understands are counted (tools/mcam_walk.py). In net/TRIAGE_lister_output.txt 269 files have operations, 1093 have none, and 96 had an unknown / scrambled header (not read).
- So no stored output (mesh, NCI) of a Stock Model or Restmill op exists: the op parameters are all that can be checked.

## 2. The stored record (op 139) - ported: `viewer/stockmodel.c`, `cam_cli PART stockmodel`
Field map: the dialog pages hold the op at `page +0x2ac[8]` with the operation struct at +0x20f0, so record offset = page offset - 0x2088.
The op is 0x2938 bytes (ent + operation; MapToDlg memset l.374948).

| record | page offset | field | dialog / source |
|---|---|---|---|
| +0x6e | +0x20f6 | Name | ctrl 3583; Op.comment |
| +0x1b88 (i32) | +0x3c10 | Initial stock shape: 0 Rectangular, 1 Cylindrical, 2 Model, 3 File, 4 Stock model, 6 Model (spindle, requires op +0x2238), 7 Lathe operation | MapToDlg switch l.374954 + radio order of dialog 25600 (Model 518, Rectangular 520, Cylindrical 519, Stock model 521, File 522, Lathe op 523) |
| +0x7a0 (i16) | +0x2828 | Stock plane view number | ctrl 1442 / 307 |
| +0x1230 / 8 / 40 | +0x32b8.. | size X / Y / Z | ctrls 1219 / 1221 / 1223; CStockModel Params +0x08 / 10 / 18 (BuildIffFileFromStockModelParameters 0x1806d53f0) |
| +0x1248 / 50 / 58 | +0x32d0.. | origin X / Y / Z | ctrls 1215 / 1230 / 1231 |
| +0x2040 (i16) | +0x40c8 | origin anchor (nine buttons 1298-1306) | MapToDlg l.375112; Params +0xe18, switch in BuildIffFile.. |
| +0x1270 | +0x32f8 | Initial stock shape tolerance | ctrl 1216 |
| +0x1278 | +0x3300 | Additional offset (Params +0x50) | ctrl 1217 |
| +0x12d0 / d8 / e0 / e8 | +0x3358.. | Path arc / Path / Tool shape / Compress tolerance (page 2) | FUN_18021d770 l.370881-370924; DDX ctrls 3365 / 3361 / 3359 / 3363 l.370546 |
| +0x1ba0 (i32) | +0x3c28 | 5-axis buffering 0 Auto, 1 Low, 2 Medium, 3 High | l.370929-370941, ctrls 3372 / 3373 / 25352 / 25353 |
| +0x229e (u8) | +0x4326 | 5-axis tip only | ctrl 3374 |
| +0x2298 (u8) | +0x4320 | colour | |
| TP refs, kind 0x17 | | source ops: +0x98 op id; +0x9c = 1 the initial stock op | GetInvalidLatheOps l.370653, FindAssocInitialOpId l.373961 |
| TP refs, kind 0x2c | | result: +0x80 DB pointer to the polygonal mesh entity (`GetStockModelFromOperation` 0x1806da760; `pl2CreateIFFFileFromStockModelOperation` 0x18058f920, `pl2CopyPmeshMMFile`) | not in the corpus |

Check (Benchmark op 5): shape 0 (Rectangular), size 7.32506 x 7.71453 x 2.6, origin 0.75626 / 1.04853 / 1.5, tol 0.001 / 0.0001 / 0.001, source op 4.
The size / origin equal that part's machine group Stock Setup exactly (`cam_cli ... stockmodel` prints both). That is the only stored op, one data point.
The triple record +0x1280 / 88 / 90 holds 0.001 / 0.002 / 0.02: the inch defaults (value, x2, x20) that mccore's dbread applies when +0x1280 <= 1e-7 (mccore_dbread.c l.6277-6287); they go to CStockModel this+0x10 / 18 / 20 (InputParameterPreparation), their use NOT DECODED, not read.

Not read (no stored example, labels from the dialog only): the File / Model entity refs of shapes 2 / 3 / 6 (kind 4 refs of class 0x10000 / 0x100 / 0x100000 / 0x40000 in MapToDlg, the File case stores a file name), the Cylinder axis (`+0x3c14` page offset radio; ctrls 3279-3281), the lathe boundary fields; the Stock Compare page.

## 3. Generation chain (decoded, NOT ported)
`stock_model_dialog` (mcmill 0x1a24a0: refused when GetProdEnableBits & 2 / & 4, product_level_ok(4)) -> `stock_model_assoc` -> `RunStockModelDialog` -> CStockModel::Regen (0x1806dd570):
1. ClearResult -> InputParameterPreparation (0x1806db270: tolerances; record 0x12d8 -> this+0x28 / 0.5 etc.)
2. InputStockPreparation: writes the stock (Rectangular / Cylindrical / solid / file / STL / previous stock model) as an IFF mesh file `StockModelInput.tmp` (BuildIffFileFromStockModelParameters 0x1806d53f0).
3. InputToolpathPreparationViaUBVS (0x1806db970): selects the source ops (CDependentOperations::Detect, skipping 0x8b), writes the Verify / backplot motion export `TmpPathInfoUBVS*.tmp` (startUpUBVS / runVerifyDlg -> ScanMotionUBVS 0x1806dd9a0, per-op tool shape CreateToolInfo).
4. OutputStockPreparation, InputPartPreparation (the part mesh `PartFileToCompare.iff`), CreateCompareInfo.
5. AssocRegenKernel (0x1806d4e40) -> **TPStockModel::Compute** (mk l.240231) -> FUN_1801305fc:
   - default: mk FUN_180131058 = **ComputeMCSweep** (the class lives in tpiftools.dll): worker count min(3, CPUs); AddToolpathDataUBVS per UBVS file; output `StockModelOutput.tmp` (IFF mesh); then FUN_180130a0c (stock compare, ComputeMCStockCompare) and FUN_180131e08.
   - only if the registry value `Configurations\Variables\Toolpaths\Use HMM 3D Stock Model` is 1 (UseHMMStockModel 0x1806e0cf0; also needs not 5-axis): the HMM path (mk FUN_18013236c: macro "//Mastercam X - Stock model Macro" -> McHmm "//3D Stock Model Plan") -> `tmpHstStockModel.tmp`.
   That value is not set under this path in the user's registry hive (tools/regf.py, NTUSER.DAT): the sweep path is the path that ran.
6. The result is stored as a polygonal mesh entity (kind 0x2c TP ref).

**tpiftools.dll -> ifsweep.dll** (IFSweep: Open3AxisInputChannel / OpenStockChannel, SetTool, Run, Compare...) and the other IF* DLLs (ifzcut, ifuncut, ifoffset, ifheal, ...) drive the Interfacial engine `interfacial01-30.dll` (19 DLLs, 16.8 MB; imports Interfacial01/02/04/09/10/16/20/28 from ifsweep alone; "INTERFACIAL PROCESS START" / GenerateInterfacialSecurity in tpiftools): a third-party closed triangle-mesh boolean / sweep kernel. **Not decompiled, not ported**: it is a mesh-kernel project of its own and the corpus has no stored result to check a port against (section 1).

## 4. Consumers of a Stock Model op
- **Restmill op 107**: source op id record 0x1448 (`op+0x13e0`), must be op 9, 10, 15, 0x83 or 0x6b or a 0x8b stock model op (ms l.469779: FUN_1802d4350 for 0x8b). Dialog 234.
- **HST rest roughing (Area / OptiRough rest material)**: `IsHstOldOptRestStyle` / `IsHmmOldRoughRestStyle` (ms l.395190-395350): the stock model -> `pl2CreateIFFFileFromStockModelOperation`, then the rest STL / boundary. Registry `Use HST rest 3D stock model` (ms l.392043, default off). HST stock source record 0x14f4 etc. (mcmsurf dialog HSTRoughRestMaterialPage).
- HST linking heights "Stock model plus" (FBM drill method 1), get_stock_model_minmax / get_stock_model_overthickness (ms l.363120-363257), mark_op_dependent_stock_models_dirty, lathe stock (mucbMakeIffLatheStockProfileFromStockModel).
Corpus count (op 132, `op_is_hmm_with_rest_material`, mccore 0x1800c5fb0: style record +0x16bc in 0,1,2,3,5,6,9,10,15 and rest enabled record +0x16c5): 6 of the corpus's 61 op-132 ops (file names repeat across folders; not deduplicated): 1 takes source kind +0x16dc = 1 with source op record +0x155c = 5 (Benchmark 3_0 op 6 rest-roughing from the Stock Model op 5); 4 kind 0 and 1 kind 2 (source kind meanings other than 1 = one op: ms l.469628-469640, NOT DECODED here).
All of them need the mesh of section 3, which we do not have. None is reachable without it.

## 5. Restmill op 107 (classic surface, NOT ported)
The record and rest sources are in `surf_classic3.md` 7. Traced further here:
- Dispatch (the msurf switch, ms case 9 / 0xf / 0x6b at l.197547-197549): case 9 / 0xf / 0x6b = the classic surface contour engine FUN_18013b2f0 (keep only the previous computation's records whose flag is 0, temp2<suffix>) then FUN_1802a14e0(1, ..) and, as for the classic pocket / contour, FUN_1802a0710 -> FUN_180302350 (rough_cleanup, surf_classic2.md 3, l.491102-492476), which is where the rest helpers FUN_1802e2640 (l.491359) / FUN_1802d9780 (l.491687) enter when the op has rest material; FUN_1802e2640 also runs from FUN_1802f9e00 (l.487379), FUN_1802e2040 from FUN_180305ab0 (l.493011); or, with msurf_use_emm_restmill / msurf_use_3rd_party_restmill (off by default: ms l.18487, 18493), the EMM / 3rd party path.
- Rest sources: flags record 0x144c (bits 8 STL, 4 Roughing tool, 2 / 1 All previous / One other op): source op record 0x1448; Roughing tool Diameter +0x13e8 and Corner radius +0x13f0 (ms l.474535-474550); the previous ops' `temp`/`prev`/`tmpzrest.tmp` engine files (ms FUN_1802e2640 l.474466, FUN_1802d9780 l.469536), or the HST rest (FUN_1802dc130) or the stock model IFF (FUN_1802d4350).
What a port needs that does not exist here: the rest-material model (IFF stock from section 3, or the engine temp files that only exist in a Mastercam session), the rest handling inside the rough cleanup stage above (the classic pocket / contour ports in viewer/cam3d_srough.c and cam3d_finish.c cover the rest-free path only), and an oracle (no op 107 in the corpus). **Refused** (cam_fileop.c: "the rest material engine ... is not ported; the operation stays as stored"); gallery stays greyed.

## 6. Not ported, by decision needed
Using our own Verify (viewer/verify.c, ours) or our height-map to fake a stock model / rest material would be a deviation from Mastercam: not done. The Interfacial engine cannot be run on Linux: a real-DLL oracle through tools/peloader for `ifsweep.dll` would be possible in principle but is not built.

## 7. The IFF mesh file (the input / output of the sweep)
`StockModelInput.tmp` / `StockModelOutput.tmp` / `tmpMesh.iff` etc. (CStockModel::BuildIffFileFromStockModelParameters 0x1806d53f0, the Rectangular case): the ASCII text "binary surface" (14 bytes, no NUL), i32 vertex count, i32 triangle count, then the vertices as 3 doubles each, then the triangles as 3 i32 vertex numbers (1-based) + 1 byte (`this+4`, the colour, CStockModel::InputParameterPreparation).
`ConvertBinIFFToStlFile` (mccore) reads the same format. The stored mesh is not this file: it is the polygonal mesh entity of the kind 0x2c reference (`pl2CopyPmeshMMFile` writes the IFF from it; section 2).
