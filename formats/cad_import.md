# CAD import: DXF, STEP, IGES, STL (File > Open)

Status 2026-11-08. User decision (delegation message 2026-11-08): OpenCASCADE's readers parse STEP / IGES instead of a port of Mastercam's converters (mcstep.dll, mciges.dll);
the public DXF format is parsed here (`viewer/dxf_read.c`). Everything around the parse follows Mastercam. Code: `viewer/cad_import.c`, `viewer/dxf_read.c`,
`viewer/occt_cad.{h,cpp}`, `viewer/core_ui/cui_doc.c` (`open_cad`). Test: `tools/tests/cad_compare.sh`. Decompiles: `~/mastercam-re/work/cadimp/dump/`
(`ac_all.c` = all of mcautocad.dll, `ac_main2.c` = FUN_180085e50, `step_main.c` / `step_core.c`, `iges_all.c`, `core_import.c`, `core_conv.c`, `sld_import.c`; Ghidra projects `work/cadimp/gh*`).

## 1 Routing (mccore.dll)

| what | decoded | here |
|---|---|---|
| File types offered | `FUN_180358740` (table of DLL / extensions / description, mccoreres 2725 IGES `.igs .iges`, 2723 AutoCAD `.dwg .dxf .dwf .dwfx`, 2730 STEP `.stp .step`, ...); `FUN_1803552e0` Open dialog filter: Mastercam types, the Import types (`FUN_18034c200`), 2858 All files | `MainWindow::openDialog` filters `IGES Files (*.igs *.iges)`, `AutoCAD Files (*.dxf)`, `STEP Files (*.stp *.step)` in the decoded order. DWG / DWF: refused with a NOT DECODED message (no reader available: Mastercam uses Teigha, `TD_Db_4.01_12.dll`) |
| Open | `DoFileOpen` -> `FUN_1803463b0` -> `FUN_180345830(files, merge 0, 1, 1, ...)`: `init_db` (new empty part, conv* options saved / restored by `FUN_18035b2a0` / `FUN_18035b100`), `sld_loading_file`, the converter's `DoImport(file)` via `GetProcAddress`, `sld_add_new_solids_to_histtree`, `OnScreenFit`. `geo_filename` = the file with `.mcam` (`ensureMCX`) before the import, restored on failure | `open_part` -> `open_cad`: `doc_clear_ops(); new_file()`, then the import, `cad_doc_name` = the file with `.mcam` (the post's NC file name and the title) |
| Merge | the same function with `param_2 = 1` (no `init_db`) | **NOT PORTED**: this program has no File > Merge at all (backstage entry greyed) |
| Options | Mastercam's Open dialog Options button: `DoImportOptionsDialog` (DWG/DXF 500, STEP 1500, IGES 701) | **NOT PORTED** (no Options button): the values are read from the configuration (below), so the default path is the same |
| Options' defaults | conv* globals, config keys in `FUN_1801d3ec0`: 3000 Edge curves (n), 3001 Repair (n), 3002 Untrimmable surface level (10000), 3003 Solids (y), 3007 Blank Paper Space (y), 3008 Break drafting (n), 3009 IGES file tolerance (n), 3010 IGES unit conversion (y), 3011 Stitching (n), 3020 Use System Color (n) | `cui_doc.c` `cad_cfg`, the user's `mcamxm.config` (mcam_config_value), else those defaults |
| Units | DXF: no conversion at all (`DAT_18013fc38` is set to 1 and stays; `$INSUNITS` / `$MEASUREMENT` not read). STEP: the file's units converted to the system units (`FUN_18006d710`'s table; `IsMetric`). IGES: file units vs `IsMetric` (`FUN_180008b30`, `fmu_scale_yn` / `ChangeConfiguration` when convIGESOverrideUnits is off) | DXF: numbers as they are. STEP / IGES: OCCT gives mm, scaled to the part's units (`mcam_is_metric`). IGES with Unit conversion off: **NOT PORTED** |

## 2 DXF: entity mapping (mcautocad.dll)

Path: `DoImport` -> `FUN_18003e660` -> `FUN_18003e260` -> `FUN_18007d090` (Teigha database) -> `DbDumper` (`FUN_1800184f0`: `*Model_Space` always, a layout's block when selected; paper space blanked with convDWGBlankPS) -> the class switch `FUN_180085e50` (per entity). Masks: `FUN_180040e80` with `DWG.MSK` (temp folder, dialog 502): every type on by default; the mask file and dialog **NOT PORTED**.

Common (FUN_180083240 / FUN_1800385f0): `visibility != visible` skipped. Level = the layer's (`FUN_18003c130`, table `FUN_18000a210`: names unique; the first new layer takes the main level when empty, then `nextAvailableLevelNumber`; off or frozen layer -> hidden level; level by name `FUN_18003c8c0`, 0xff when not in the table). Colour: ByLayer the layer's ACI, ByBlock the block's (in a block) or main (`0xffff` -> main), ACI -> Mastercam colour by nearest RGB over the 256 palette (`FUN_180028a40` + table `DAT_18013e030`, ACI 7 white/black by background `gr_back_color`). Line style by line type name (`FUN_18003c6a0`: HIDDEN / DASHED 2, CENTER / CHAIN 3, PHANTOM 4, DOT 5, ACAD_ISO). Width by lineweight (`FUN_180082ee0`: 1/100 mm >= 141 -> 5, >= 101 -> 4, >= 51 -> 3, > 25 -> 2, else 1; ByLayer the layer's 370, `FUN_180082f80`; default -3 -> 1). Paper space (`FUN_180081e60` = 2) with Blank on: sel 0x101 (blanked). Tolerance `tol_exref` (config 1000).

| DXF entity | mask id | Mastercam entity | decoded in |
|---|---|---|---|
| POINT | 2 | point (style by `$PDMODE`, `FUN_180038810`: 0 -> 2, 1 -> 1, 2 -> 3, 3 -> 4, 4 -> 1, else the main style); top level with PDMODE 1: not stored; in a block: stored, blanked (`FUN_1800388b0`) | FUN_180085e50, FUN_1800388b0 |
| LINE | 1 | line; thickness: a copy along world Z and the end lines (`FUN_180038a40`) | yes |
| CIRCLE, ARC | 3, 7 | arc (`FUN_180030980`): normal -> view (`FUN_18002dfd0`: reserved 1..6, else the DXF arbitrary axis, `FUN_18002dd00`, stored view **not created** here: view number 9 held on the entity only); ARC sweep = (end - start) mod 360 (the area test), end 0 with start > 0 -> 2 pi | yes |
| ELLIPSE | 10 | rational degree 2 NURBS, knots 0 0 0 1 1 2 2 3 3 4 4 4, 9 control points (weights 1, sqrt(2)/2), `t1` / `t2` in the knot range (quadrants exact, else Newton `FUN_180031630`) `FUN_1800319e0`, split at 360 `FUN_180039220` / `FUN_180085030`; flags 0x11 (0x13 full) | yes |
| LWPOLYLINE, POLYLINE 2D | 4 | lines and arcs in the normal's view at the elevation (`FUN_1800399f0` .. `FUN_180039bf0`): `|bulge| <= tol` a line, else bulge arc `FUN_180030fe0` (sweep > 0, start at p0 for b > 0, at p1 for b <= 0); closed: last to first; 2D vertices per poly type as in the decompile (spline: the fit vertices) | yes |
| POLYLINE 3D | 11 | lines in world, vertices as decoded | yes |
| SPLINE | 5 | NURBS curve (`FUN_180083c00` -> `FUN_180039870` -> `FUN_180037fb0`): flags bit 2 non-rational, bit 1 closed, bit 3 periodic; `|z| > 1e38` -> 0; `put_nurbs_in_db`. Fit-point-only splines: **skipped and reported** (the control points are Teigha's) | yes |
| INSERT | 8 | the block's entities with the transform stack (`FUN_18002e400`, `FUN_18002e6f0`), layer "0" and ByBlock inherit the INSERT's level / colour; non-uniform scale on arcs / ellipses / splines: **skipped and reported** (the NURBS conversion path `FUN_1800302a0` not ported); MINSERT arrays and XREF blocks: **skipped and reported** | yes (uniform) |
| TEXT, MTEXT, ATTRIB, DIMENSION, LEADER, HATCH, SOLID, TRACE, 3DFACE, 3DSOLID, REGION, BODY, meshes, RAY, XLINE, HELIX, VIEWPORT, proxy | decoded switch cases exist | **Skipped and reported on import** (counts in the message): 3DSOLID / REGION / BODY are ACIS data; the others not ported |

Notes: entities are read in file order inside each section; the DXF `$INSUNITS` is deliberately ignored (Mastercam does not read it). `ents` geometry checks: `tools/tests/cad_compare.sh`.

## 3 STEP (mcstep.dll, OCCT parser)

`DoImport` -> `FUN_18002d480(file, 0, edges, solids, !solids, stitching, badSurfLev, scale)`. Result types per the flags (`DAT_1806ebca8`): solids (`sld_import_solid`, mcgeomsld `FUN_180088cb0`..., `sld_import_add_mc` -> `dbput_solid`), surfaces, edge curves (`FUN_180014830`), wireframe (`FUN_180029470`: a NURBS curve whose control points are collinear -> line; main colour / level). `nameSolidsAndLevels` (mcgeomsld `0x1800a5300`): the file's base name, " (n)" when several solids; level name set when empty; description.

Here (OCCT `STEPControl_Reader`): each TopAbs_SOLID -> an imported Solid (below); free shells / faces -> trimmed surfaces (`srf_faces_make`, the From Solid path) on the main level with the surface colour rule; free edges -> lines / arcs / NURBS; Edge curves when 3000 = y. Default Solids y (3003).
**NOT DECODED / NOT PORTED**: the STEP file's colours (Use System Color off, 3020 n: `sld_import_solid`'s colour handling): imported solids take the system solid colour (cfg_solid_color); the repair / stitching / untrimmable-surface-level options (3001 / 3011 / 3002: read, not applied: OCCT's transfer is used as is); saved views and PMI (mcstep's named planes).

Imported solids (`Solid.brep`, OCCT BRep text; `xt = NULL`): shaded (OCCT mesh), AutoCursor edges (lines / circles, as XT), solid chaining (`occt_brep_topo`), 3D drive (`occt_brep_drive`), From Solid, Analyze. Consumers that read only XT (the 2D ops' `xt_top_loop` etc., `cam_fileop.c`) refuse them. Saved in the project (`.mcamui`, `OCCSOLID` + `BH` hex lines). **Saved to a Mastercam part (2026-11-19) as Mastercam keeps an imported solid: a brick** - SOLID_ID, SOLIDXMT + XT, and one SldBodyOp holding its own XT copy with CNC/mcEntinfoPtr attributes and one record per edge / face (`sld_import_add_mc` -> `mc_create_body_op`; decode, proof 424 / 424 corpus body ops byte-identical, writer and round trip: formats/solid_import_save.md). The description / level name follow `nameSolidsAndLevels` (formats/solid_import_save.md 5: a named level gives its name as the description).

## 4 IGES (mciges.dll, OCCT parser)

`DoImport` -> `FUN_18006b300` (error log options, tolerance / units overrides, trim preference, masks `FUN_1800294a0`). mciges imports no `sld_import_solid`: **IGES never makes solids in Mastercam**, so OCCT solids are written as trimmed surfaces and reported. Wireframe as free edges. **NOT PORTED**: the entity masks and their dialog (700), the 406 / level / colour mapping of the D section (everything goes to the main level and colour), trim surface handling options, the file-tolerance override.

## 5 Saving and project format

New geometry goes in the project as usual; additions (ours): `CURVE` + `SH` hex (a created spline, which earlier went out as an empty `GEO` line), `GEOATTR` (line style / width / blank of the entity just above), `OCCSOLID` + `BH` (imported solid).

## 6 Not decoded / refused summary

DWG / DWF (no reader), binary DXF (not implemented), DXF fit-point-only splines, text / dimensions / hatches / 3D entities in DXF, non-uniform blocks, MINSERT, XREF; STEP / IGES file colours and levels, repair / stitching, masks; the Options dialogs; File > Merge.
STL (section 7): saving a mesh whose facets' colour bytes differ; meshes of IFF / MCO data (kinds 2 / 3) read from a part; transform / copy / analyze /
AutoCursor / window selection / attribute edits of meshes; Esc during an import as lines.

## 7 STL (mcstl.dll, decoded 2026-11-12; code viewer/stl_mesh.c, cad_import.c, mcam_load.c, scene.c, core_ui)

Decompiles: `~/mastercam-re/work/stlimp/dump/` (`mcstl_all.c` = all of mcstl.dll, Ghidra project `work/stlimp/gh`; `core_stl.c`, `core_mmf.c`,
`core_pack.c`, `core_match.c` from mccore; `mc_pmesh.c`, `mc_pick.c` from mastercam.dll; `cu_match*.c` from mccadutil; scripts `run1.sh`..`run9.sh`).

**Routing.** `FUN_180358740`'s table: `.stl`, DLL McStl, description mccoreres 2746 "StereoLithography Files" (after McAscii). The Open dialog lists
"StereoLithography Files (*.stl)" after STEP here. mcstl.dll exports `DoImport` / `DoImportOptionsDialog` / `DoExport` / `DoExportOptionsDialog`.
`DoImport` (0x180004910): EA off, `in_file_get` = 1, `FUN_1800040e0(file)`, returns 1 whatever happened (Mastercam says nothing when the file
makes nothing; the same here: the reason goes to stderr only). No units conversion (the file's numbers are taken as the part's units).

**Options.** `DoImportOptionsDialog` (mcstlres dialog "STL Read Parameters": radios "Import as STL Mesh" / "Import as Lines") sets
`convSTLImportLines` = config **3019** (0xbcb, FUN_1801d3ec0 key, string 0x2848); default **n** (FUN_1803babd0). Read here from the user's
configuration (`cad_cfg(3019, 0)`, cui_doc.c); the dialog itself is NOT PORTED (as the other converters' Options). Export (STL writer, config
3012-3014: ASCII / resolution / world coordinates, "Save as an STL File") is not part of this import.

**As a mesh (3019 n, the default).** `FUN_1800040e0` -> mccore `StoreSTLFileAsSTLMesh(file, identity 4x4, &eptr)` (0x1805ccac0) -> `draw_ent`:
- `UtMMFDataMgr::CreateSTLEntryFromFile` (0x1808c14a0): `FUN_1806ccb40` = 2 (binary) when the u32 at 0x50 times 0x32 plus 0x54 is the file
  size, else 1 (ASCII) - independent of check_ascii_binary, so a binary file with a "solid" header is binary; size 0 = no entry.
- `CreateSTLEntryFromStream` (0x1808c1610) with the .data byte `DAT_181311e62` = 1 (never written: the "enhanced" form): binary: every facet,
  its normal normalised in float (`x = n.n`, `<= 1e-12` -> 0, else `/ sqrtf(x)`), the attribute's low byte; "mixed" when a byte differs from
  the first. ASCII: `FUN_1806d0aa0` per facet (the get_first_stl_triangle reader + the "facet normal" values, sscanf at +0xd, atof), normal and
  vertices as floats (not normalised), attribute 0; no facet = no entry. `WriteEnhancedSTL` (0x1808c2c80) keeps the facets as runs of one
  colour byte (mixed: `std::sort` by the byte, `FUN_1808bef00` = MSVC introsort with `LAB_1808c2880` = a.byte < b.byte) and per vertex
  x y z nx ny nz floats.
- The record, PMESH_ID class 0x100000 (`bytes_per_ent` 200 + 0x68 = 0x130): header by `store_ent` (main level, main colour, main_attrib),
  +0x68 i16 kind 0 (`FUN_1805cc510`: 0 STL, 2 IFF, 3 MCO), +0x70 i64 the MMF entry, +0x78 4x4 matrix (identity from DoImport), +0xf8 / +0x110
  the box (`FUN_1805cc560`: the vertices through `MCLoadSTLMesh::GetSTLPoly(.., true)`, min from 1e38 / max from -1e38, strict), +0x128 u8 1.
- Display (`mastercam.dll FUN_180216f30`): the record's matrix multiplied in, the triangles in the entity's colour (per-run colours only for
  STOCK_ID 0x200000); shading off: `glPolygonMode(GL_LINE)` = triangle outlines; shading on: filled with the stored normals. Here: outlines /
  faces in scene.c (`pm_*`, the surfaces' shader; lighting ours, as the surfaces').
- Pick: shading off `FUN_180205380` (box test, then each triangle's three projected edges, `FUN_180204f40` -> `dist_to_finite_line`); shading
  on `PickShadedPolygonalMeshFromCursor` (`FUN_180206b10`, the cursor ray against the triangles). Here: cui_sel.c, as the surfaces'. Window /
  polygon selection of meshes (`FUN_1802075b0` / `FUN_180207860`): NOT PORTED (not window-selected).
- Delete / undo: as a surface (McMesh.off).
- **Part file** (`FUN_1802c72c0` writer, `FUN_1802c62d0` reader, decomp/mccore_tooladd.c / mccore_dbread.c): u32 length = 0x130 +
  `GetPolygonalMeshDataSize` - 8; the record without +0x70..+0x77 (`FUN_1802c2f00`; the reader `FUN_1802c28d0` puts -1 back); then
  `WritePolygonalMeshDataToStream` = `UtMMFDataMgr::WriteEntryToStream` (0x1808c3060): enhanced STL -> a binary STL: the 0x50-byte header
  "MASTERCAM STL DATA" padded with spaces (.data 0x181306720), u32 count, per facet the first vertex's normal, the three vertices, the run's
  colour byte, 0. The reader: `StorePolygonalMeshDataFromStream(stream, len - 0x128, kind)` -> kind 0 `CreateSTLEntryFromStream(.., binary)`.
  Here: written (mcam_saveops.c) and read (mcam_load.c) for kind 0; **refused with a note** for a mixed mesh (the sort order of MSVC's
  `std::sort` is NOT PORTED) and for kinds 2 / 3 (NOT DECODED). A part mesh deleted here is dropped from the save. Attribute changes of a part
  mesh are not written (NOT PORTED). No corpus part holds a PMESH_ID record (scan of <cad-files> + the user's parts with the
  loader's framing, 2026-11-12), so there is no read-compare against Mastercam; the shipped samples `stl_load.stl` (binary, "solid" header) and
  `g68.2_cube.stl` ("MASTERCAM STL DATA" header) are opened by cad_compare.sh when the install's files are present.
- Project (.mcamui, ours): `MESH level colour count mixed` + `MH` hex lines (the record, per facet 12 floats + the byte).

**As lines (3019 y).** `SysFile::Open` then mccore `rd_stl` (0x1806d15f0): per facet (`get_first_stl_triangle` / `get_next_stl_triangle`, the
Stock Setup's reader) the edges p0-p1, p1-p2, p2-p0 become L_ID lines (store_ent: main level / colour / attributes) when longer than `tol`
and not equal to a line already made: a sorted array (`FUN_1806d1360` 1000 entries, `FUN_1806d13a0` insert, grows by 1000) keyed by
`(x2 + x1 + y1 + y2 + z1 + z2) * 0.5` (`FUN_1806d1330`), `FUN_1806ceb90` bsearch (equal within 3 tol, `LAB_1806ced60`) + the neighbours within
3 tol + `check_ent_match` (mccadutil 0x180068310, flags 0: both ends `pt_equal` in either order). Allocation failure: mccoreres 5434 "STL memory
allocation error". Not here: the status line (5000 "Reading " + file + "..."), Esc (`KeyPress(0x1b)`).

Tests: `tools/tests/cad_compare.sh` section 5 (`tools/tests/cad/make_stl.py`: ASCII / binary / binary with a "solid" header / mixed colour bytes /
unnormalised normals / bad count, lines with a shared edge and a degenerate facet), the .mcam write + read back, delete + undo, project round trip,
a pick. No Mastercam oracle (no part with a mesh; the writer's output is UNVERIFIED in Mastercam: formats/windows_checklist.md).
