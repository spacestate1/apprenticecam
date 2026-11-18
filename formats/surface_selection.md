# 3D toolpath surface selection (decision N13-2, TODO S1) - decode and port plan

Status 2026-11-12: **decoded (selection flow, dialog, record layout); S2 (data model + .mcam load, section 6), S3 (solid face identity,
read-only, section 7), S4 (the pick flow, section 6 "S4" status), S6 (.mcam save of the references, section 6; X9 / 2017) and S7 (the
engines use check surfaces / containment, section 8) ported; S5 not ported.** This replaces the port's own whole-entity pick
(`viewer/core_ui/cui_chain.c` drv_pick / drv_click / drv_done, `cui_stock.c` st_pick_drive_at), which is NOT Mastercam's flow
(audit item 1.10, formats/audit_2026-10-26_coreui.md).

Sources (line numbers are in these files; `decomp/` is the gitignored dump directory of the main checkout):
- `decomp/mcmsurf_all.c` (mcmsurf.dll, whole): msurf_assoc @1800167c0 (l.15838-16263), the drive / check step FUN_18001bcf0
  ("select_surf_assoc", Assocsrf.cpp, l.19758-20165), containment FUN_18001cff0 (l.20726) / FUN_18001b640 (l.19342), the dialog
  classes CEditGeom* (l.37983-40300), the handlers EditSelected* / RemoveSelected* / ShowSelectedGeom (l.4254-8238, 451398-452990),
  the counters msurf_ent_count / MSurfEntCount / UpdateEntCount (l.16370-16700, 5897, 452813), surf_sel_bits (l.21400),
  tp_surf_list (l.22182, 22410), msurf_map (l.339985).
- `decomp/surfsel/` (made for this decode):
  `mcgeomsld_surfsel.c` (S), `mcgeomsld_surfsel_internals.c` (I), `mcgeomsld_surfsel_assign.c` (A): mcgeomsld.dll sld_* (Ghidra
  `~/mastercam-re/ghidra_sld`); `mccore_cfg319_ddx.c`, `mccore_cfg319_page.c`: the Configuration - Toolpaths page (mccore.dll,
  `ghidra_core`); `recs_scripts/*.py`: the .mcam corpus scanner used for section 2.7 (tpscan.py, analyze.py, solidsel.py ...).
- Resources: `exports/mcmsurfres_dialogs.tsv`, `exports/mcmsurfres_strings.tsv`, `exports/mccoreres_dialogs.tsv` (dialog 319).
  Message maps read from mcmsurf.dll's .rdata (AFX_MSGMAP_ENTRY arrays at 0x1803f36f0 .. 0x1803f3c00).
- Corpus: 908 parts under <mastercam-samples> (491 Mastercam 2017, 395 2018), 12402 TP_ID records.

Strings are mcmsurfres unless marked. "ent_tp" is the 0x388-byte toolpath reference record (ent header 0x68 + tp_entity); it is
also the TP_ID record's layout in a 2017 file (section 2.1).

---------------------------------------------------------------------------------------------------------------------------------

## 1. The flow

### 1.1 Entry points
Every classic surface op and every Surface High Speed (HST, op 132 = 0x84) style starts in `msurf_assoc(opcode, hst_style)` from its
ribbon / toolbar function (l.211516-213080):

| toolbar function | msurf_assoc(...) |
|---|---|
| msurf_rgh_parallel / radial / project / flowline | (5) / (6) / (7) / (8) |
| msurf_rgh_contour, msurf_pocket, msurf_pocket_light (Multisurface pocket), msurf_restmill | (9), (10), (0x83), (0x6b) |
| msurf_parallel / radial / project / flowline / contour | (0xb) / (0xc) / (0xd) / (0xe) / (0xf) |
| msurf_pencil, msurf_leftover, msurf_steep_parallel, msurf_shallow, msurf_scallop | (0x27), (0x28), (0x29), (0x2a), (0x2b) |
| msurf_plunge, msurf_blend | (0x2c), (0x6d) |
| msurf_hmm_core_rough / opti_core / waterline / scallop / horizontal_area / raster / pencil / hybrid / spiral / radial / project | (0x84, style 0 / 1 / 2 / 3 / 4 / 5 / 6 / 7 / 9 / 10 / 15) |
| msurf_hmm_toolbar, msurf_*_1_toolbar | (code, -1) |

Re-entry on an existing op: the Toolpaths Manager's geometry item (mastercam.dll `COpMgrTree::EditSelectedGeometry2`,
decomp/opman_tree_all.c l.15326-15377: `SurfaceOpHasGeomBranches` -> `EditAllGeom()`) and mcmill's `surface_manager`
(decomp/mcmill_all.c l.7668) open the same dialog as step 1.3 (8).

### 1.2 The configuration that drives it (Configuration - Toolpaths, mccoreres dialog 319, group 3014 "Toolpath/surface selection")
Page DDX (decomp/surfsel/mccore_cfg319_ddx.c, FUN_1801b0a00) and load / store (mccore_cfg319_page.c FUN_1801b0cc0 l.161-168,
FUN_1801b1790 l.196-204, FUN_1801b1820 l.222-224):

| control (radio group, first id) | values | global (mcmsurf export) |
|---|---|---|
| Drive: 2876 None / 2877 Prompt / 2878 All | 0 / 1 / 2 | `msurf_all_geom % 10` |
| Check: 2881 None / 2880 Prompt / 2879 Unselected | 0 / 1 / 2 | `msurf_all_geom / 10` (`msurf_all_geom = check * 10 + drive`) |
| CAD file: 2883 None / 2884 Prompt | 0 / 1 | `msurf_mach_stl` |
| Containment: 2899 None / 2900 Prompt | 0 / 1 | `msurf_toolcont` |
| 2882 "Show toolpath/surface selection dialog" | 0 / 1 | `msurf_show_selection_dlg` |

Defaults (mccore reset, decomp/mccore_tooledit.c l.11977-11982): all_geom 1 (Drive Prompt, Check None), toolcont 0, mach_stl 0,
show dialog 1. Config file keys 0x427a / 0x427b / 0x427f / 0x4279 (decomp/units/systol_readers.c l.1130-1153). The DLL's own static
initial values (mcmsurf .data, read from the PE): all_geom 1, toolcont 1, the others 0.

`msurf_toolcont == 2` (use `msurf_chain`) and `msurf_all_geom_preselected == 1` are tested (l.16021-16024, l.19864-19872) but no
writer of either was found (both zero-initialised .bss exports; no other DLL of the install names them): **NOT DECODED / unreachable
from the decoded code**; the port leaves both out.

### 1.3 msurf_assoc, in order (l.15838-16263)
1. Licensing (GetProdEnableBits, product_level_ok(4); l.15921-15944): out of scope.
2. Ops 5..8 (rough parallel / radial / project / flowline): the **"Select Boss/Cavity" dialog** (100: 512 Boss, 514 Cavity, 516 Undefined;
   FUN_1800250b0 l.26245 opens it with Undefined selected, returns 1 / 2 / 3). (S4 correction: FUN_1800250b0 returns +0x1550 + 1 without
   testing DoModal's result, and DDX_Radio writes +0x1550 only at OK - so Cancel answers 3 = Undefined and the command goes on; the
   l.15957 test only ends it for answers outside 1..3, which this dialog cannot give. MCDialog's OnCancel is mccore's, not read.) FUN_180010560
   (l.11161) writes the answer into op fields (op 5: +0x1380, +0x1368..+0x136a; op 6: +0x1378 ...).
3. `init_operation_surface(cur_op, opcode, 1, hst_style, &ok)` (l.15988): the new op, as for every op type (not part of this decode).
4. **Drive / check selection**: FUN_18001bcf0 (l.15993), section 1.4. Cancelled (Esc in a prompt) -> step 13.
5. **Containment** (l.16001-16025), only when the op has containment (`msurf_op_has_tcbs`) and none is stored yet (kind 6 role 2 count 0,
   msurf_ent_count): `msurf_toolcont == 1` -> FUN_18001cff0 (section 1.6); `== 2` and msurf_chain -> FUN_18001b640 (save_chain(msurf_chain,
   role 2)), unreachable (1.2).
6. OMrefreshOperation (l.16031).
7. If `msurf_show_selection_dlg`: **EditAllGeom()** - the "Toolpath/surface selection" dialog (l.16033, section 1.7).
8. HST (0x84): FUN_180009270 (l.5745): the drive surfaces' / solid faces' XY box centre -> op +0x1388 / +0x1390, half diagonal -> +0x13a0.
9. **The op's parameter dialog** `edit_operation(cur_op, 1, ...)` (l.16038). Cancel -> step 13.
10. Op 7 / 0xd / 0x2c fix-ups (l.16053-16075, op +0x136c / +0x1298 / +0x13b2), `msurf_map()` (l.16078: the op's fields into the msurf
    globals; also sets `msurf_prompt_st_pt` and the project source, section 3), make_tool_shape.
11. Picks that follow the dialog (l.16080-16220), each only when the op has none of that kind yet:
    - start point: `msurf_prompt_st_pt` (or HST with op +0x1657) and no kind 1 point -> FUN_18001ccc0 (l.20569): prompt 310
      "Select an approximate starting point" (309 "Select radial point" for ops 6 / 0xc), point_proc_assoc, stored as a kind 1 record
      (init_tp_ent(.., 1, ..), store_ent_assoc, l.20638-20650);
    - ops 7 / 0xd / 0x2c by the project source (msurf_map: op 7 +0x136c, op 0xd +0x1298, op 0x2c 2 or 0 when +0x13b2 == 1): 0 = source
      operations (no pick; the op must list some, l.16102-16114), 1 = curves -> FUN_18001c4d0, 2 = points -> select_surf_points (l.20364:
      prompts 355 "Select points to project", plunge 415 / 416, select_ents mask 1);
    - op 0x6d (Blend) with no role-3 chains: FUN_18001c4d0 (l.20168): `chain_it_sync(blend_tp_title, ...)`, save_chain(.., role 3);
    - flowline ops (op_is_flowline): surf_sel_bits + `set_flow_surf` (l.21000: the "Flowline data" dialog 141);
    - CAD file: `msurf_mach_stl`, or op 0x6b with op +0x13e4 bit 8, or HST rest material with op +0x1674 == 3, and no CAD file ->
      FUN_18001b8a0 (the CAD file pick; not traced further).
12. If drive + check surfaces + solid drive + check faces + CAD files > 0: **generate** (`bnci_section`, l.16228). If the total is 0 and
    Drive is not None: message 241 "Surface operation does not contain any drive surfaces" (title 429), once per session
    (DAT_1804e3758, l.16232-16240); the op is kept.
13. Keep-or-delete (LAB_180017523 / LAB_180017532, l.15965-15985), reached when FUN_18001bcf0 reports a cancel or a failure, when
    edit_operation returns not-ok, and after bnci_section when its out flag (local_2ab7) is 0: if the selection was cancelled
    (local_2ab0) the new op is deleted at once (`TpPartOpList::DeleteListAndDB`); otherwise, unless edit_operation's result code
    (local_2a40) is 99, `message_box_yes_no` 427 "Do you wish to keep this operation?" - No deletes it, Yes keeps it. (S4, disassembly
   18001754b-1800175c5: text 427, caption 428 "Toolpath/surface", flags 2; local_2a40 = edit_operation's *param_6 = the dialog's return
   code (DoMSurf, decomp/mccore_edit_operation.c l.752 / 3170); which answer is 99 is NOT DECODED. Message 241: caption 429, text 241.)
14. End: `set_clr_sel_bits_all(0, 4, 0, 4, 0x1f1fff, 1, 0)` (the selection is cleared, l.16247).

### 1.4 Drive and check selection (FUN_18001bcf0, l.19758-20165)
- Only for surface / C-hook ops (op_is_surface_or_chook, l.19844).
- Select mask: `0x110100` (SURF_ID 0x100 + SOLID_ID 0x10000 + PMESH_ID 0x100000); flowline ops 8 / 0xe (ms_tp_type) use `0x10100`
  (no meshes) (l.19845-19849, 19883-19886). Class names: mccore table 0x181307690 (FILE_ID 0x40000, PMESH_ID 0x100000).
- drive = all_geom % 10, check = 0 / 1 / 2 for all_geom < 10 / < 20 / >= 20 (l.19850-19862).
- The selection is cleared first (`set_clr_sel_bits_all(.., 0x1f1fff, ..)`, l.19865) and the solid selection state reset
  (`sld_init_solid_surfs_selection(0)`, l.19867; S:400: clears mcgeomsld's body / face lists, sets allow_solid_surfaces_assoc).

Drive:
- **Prompt (1)**: prompt 406 "Select Drive Surfaces", `select_ents(prompt, mask, .., true)` (l.19882-19889). Return 2 (Esc) -> cancelled
  (*param_3 = 1). Otherwise `sld_sel_selected_solid_surfs(true, false)` (l.19872): every solid whose selection bit 4 is on joins the
  solid selection as a whole body (S:826). Faces picked with the solid selection toolbar are already in mcgeomsld's face list (1.5).
- **All (2)**: FUN_18001b530 (l.19303) turns sel bit 4 on for every unblanked SURF_ID / PMESH_ID (get_raw_ent mask 0x100100), and
  `sld_sel_all_solid_surfs()` adds every visible solid as a whole body (S:528).
- **None (0)**: nothing is selected.
- `tp_surf_list(op)` (l.19898; l.22410) builds one kind 4 record per selected surface / mesh (get_raw_ent sel bit 4, mask 0x100100) and
  one per solid that already has an assigned SldToolPOp (sld_assign_toolp); each is stored with flag 4 cleared (drive) by
  `store_ent_assoc(rec, .., 2, ..)`, the op's reference count (op +0x88) counted up (l.19903-19918); `rewrite_ent_assoc` writes the op
  with +0x94 = 1 (dirty), +0x97 = 0 (l.19919-19925).
- The selection is cleared and `sld_set_toolp_srf_type(4)` (l.19988-19990): solid faces picked from now on are typed 4 (check).

Check:
- **Prompt (1)**: prompt 407 "Select Check Surfaces", select_ents (same mask) (l.20003-20009); Esc -> cancelled. Then
  `sld_sel_selected_solid_surfs(true, false)`.
- **Unselected (2)**: if Drive was All, nothing (everything is drive); else FUN_18001b530 + `sld_sel_all_unselected_solid_surfs()`
  (S:597: the faces not yet picked of partly picked bodies, and every visible solid in neither list) (l.19993-19999).
- `sld_surfs_to_toolp_op()` (l.20017; S:1228): one SldToolPOp per solid with picks is created in that solid's history (2.3).
- `tp_surf_list` again; every new surface record gets flag 4 (check). A surface whose eptr is already in the op (it was picked as drive)
  is not added again: its new record's flag 4 is cleared and - when Check is Prompt - the **existing drive record gets flag 4** (picking a
  drive surface as check makes it a check surface; tp_update_ent) (l.20027-20058). Records with flag 4 and every solid record are
  stored (l.20066-20080); op rewritten as above; `*param_4 = 1` (done).

Messages of the same family (not on this path, used by the HST checks): 284 "No drive surface(s) selected (all surfaces selected are check
surfaces)", 285 / 286 (check / drive picked from a drive / check solid is converted), 279 "No drive surfaces/faces in operation".

### 1.5 What a click does
- A surface / mesh: select_ents toggles its selection bit 4 (mccore general selection; the port has it as `cui_sel.c` with masks).
- A solid: mcgeomsld's solid selection is active while `allow_solid_surfaces_assoc` is set: `OnSolidsSelectActivate` calls
  `sld_sel_solid_surfs` (decomp/mcgeomsld_solidchain.c l.1281; S:906), which runs `sld_array_pick(bodies, faces, NULL)` (A:687; pick mask
  from the arrays given: 1 bodies, 4 faces) and merges the hits (FUN_1800a92f0, I:1098: `ps_is_face` / `ps_is_body`; picking an
  entry again removes it; Shift + face adds the tangent faces) and sets sel bit 4 on the picked solids. A body hit goes to the body list
  (whole body), a face hit to the face list, each with the current type (`sld_set_toolp_srf_type`, S:1006: 0 drive, 4 check, 0x20 gouge).
  Merge rule FUN_1800af580 (I:1): a known tag gets the type ORed in (overwritten when the type is 0 or replace is set), a new tag is
  appended.
- **End selection / Enter**: select_ents returns; Esc returns 2 (cancel). select_ents' own interface (the selection toolbar, window
  selection, the End Selection button) is mccore's general selection - **NOT DECODED here** (only the masks and return codes are used).

### 1.6 Containment (FUN_18001cff0, l.20726-20900)
`sld_init_solid_chains_selection()`; `chain_it_point(434 "Chain 2D tool containment boundary #", &chains, res)` (the chaining dialog;
solid edges allowed); `sld_chains_to_toolp_op(&chains)`; if chaining succeeded `save_chain(chains, 0, 0, "", NULL, role 2, ..)` - every
chain is stored as a kind 6 header with role 2 and its kind 7 members (2.4); op re-read; free_chains, close_solid_chains_temp_file.

### 1.7 The "Toolpath/surface selection" dialog (EditAllGeom, l.451398)
Dialog by op type (EditAllGeom switch; constructors l.37983-38240, `MCDialog(id)` arguments):

| op codes | class | dialog | extra group |
|---|---|---|---|
| 5, 9, 0xb, 0xf, 0x27-0x2b, 0x6b; 0x84 styles other than 15 | CEditGeomStpnt | 116 | "Approximate starting point" (614 Select, 609 Remove) |
| 6, 0xc | CEditGeomRadial | 118 | "Radial point" (617 Center) |
| 7, 0xd with points / without | CEditGeomPoints / CEditGeomCurves | 126 / 128 | "Points" (625, 607, count 602) / "Curves" (628, 606, count 592) |
| 8, 0xe | CEditGeomFlow | 114 | "Flowline" (612 Flow) |
| 10, 0x83 | CEditGeomEpnt | 122 | "Entry point" (621 Select, 608 Remove) |
| 0x2c | CEditGeomGrid | 130 | "Grid" (632 Select, 610 Remove) |
| 0x2e | CEditGeom4ax | 132 | "4 axis point" (633 4axis) |
| 0x6d | CEditGeomBlend | 120 | "Blend" (619 Blend, 605 Remove) |
| 0x84 style 15 (Project) | CEditGeomCurvesPoints | 1519 | Curves + Points |
| others | CEditGeom | 112 | none |

Common controls (all of them): group 584 **Drive** (count 587, 599 Select, 600 Remove, count 588 + 598 "CAD file...", 601 Remove, 597
Show...), group 585 **Check** (count 590, 595 Select, 596 Remove, 594 Show...), group 586 **Containment** (count 591, 593 Select, 603
Remove), OK / Cancel / Help. (116 / 1519 also have 604 / 605 "Color" buttons: MCColorPickerButton members, wired only for op 0x84 in DDX
FUN_180038ca0 as 0x25c / 0x25d - their handlers are NOT DECODED.)

Handlers (message map at 0x1803f36f0, base class CEditGeom; subclasses' maps 0x1803f3870 .. 0x1803f3c00):

| id | handler | action |
|---|---|---|
| 599 Drive Select | FUN_180039a20 | `EditSelectedGeom(op, .., 1)` (below), refresh counts |
| 600 Drive Remove | FUN_180039780 | `RemoveSelectedDriveCheck(op, 1)` (l.6449): deletes every kind 4 record without flags 4 / 0x20 (surfaces / meshes); for a solid record `sld_remove_surf_selection_by_type(0, solid, link id)` and the record is deleted when nothing is left |
| 598 CAD file... | FUN_180039810 | `EditSelectedCadfile(op, 0, ..)` (l.4366 -> FUN_18001b8a0) |
| 601 CAD Remove | FUN_1800396b0 | `RemoveSelectedCadfile` (l.6065, class 0x40000 records) |
| 597 / 594 Show... | FUN_180039ca0 / FUN_180039c20 | `ShowSelectedGeom(op, 1 / 0)` (l.8050): highlights the drive / check geometry, status bar text (FUN_18001dbb0: "%ld Total Surface(s),  %ld From Solid(s)" with 332 / 333, check 334), the modeless note dialog 1313 until a click (FUN_1802ba2d0) |
| 595 Check Select | FUN_180039860 | `EditSelectedGeom(op, .., 0)` |
| 596 Check Remove | FUN_1800396e0 | `RemoveSelectedGeom(op, 0)` (l.6604): the check records (flag 4) of surfaces / meshes deleted, solid ones by type (0xb); with none, warning 0x1b1 (433 "There are no visible check surfaces"; drive: 0x1b0 = 432) |
| 593 Containment Select | FUN_180039bd0 | `EditSelectedTcbs` (l.452605): the op has role-2 chains -> `chain_edit(main frame, 2)` (Chain Manager on them); else FUN_18001cff0 (1.6) |
| 603 Containment Remove | FUN_180039500 | `RemoveSelectedContainment` (l.6191): every kind 6 record with role 2 and its members deleted (DeleteToolpathEntities) |
| 614 / 621 / 617 / 633 | EditSelectedSurfPnt (l.452266) | the start / entry / radial / 4-axis point, kind 1 record; sets has_start_pt / has_entry_pt / has_hmmpoint |
| 609 / 608 | FUN_18003b000 / FUN_180039d20 | RemoveSelectedPoints + clear has_start_pt (HST: has_hmmpoint) / has_entry_pt |
| 625 / 607, 632 / 610 | EditSelectedPoints (l.452024) / RemoveSelectedPoints | points (prompts 355 / 415 / 416) |
| 628 / 606, 619 / 605 | EditSelectedChns (l.451861) / RemoveSelectedDriveChain | curves / blend chains (role 3) |
| 612 | EditSelectedFlowline (l.4411) | surf_sel_bits + set_flow_surf |
| Cancel | FUN_1800394d0 | PostMessage(0x8028, 0) + OnCancel |

Every Select / Remove ends with FUN_18003b3a0 (l.40301): `UpdateEntCount` and the counts written into the statics: 587 = drive
(surfaces + solid drive faces), 590 = check (surfaces + solid check faces + CAD check), 588 = CAD files, 591 = containment chains,
592 = curve chains, 602 = points (EntCountStats, section 2.6). The Select handlers run inside `AfxSuspendWindow(dlg, 0, 6)` /
`AfxResumeWindow(0, 9)` (the dialog is hidden during the pick).

**EditSelectedGeom(op, .., drive)** (l.4480-4920), the re-entry pick:
1. clear the selection; `tp_get_all(op)`; CountSolidFaces (l.4254);
2. `surf_sel_bits(list, .., ...)` (l.21400): the op's current drive (or check) surfaces are selected and drawn; its solids' faces are
   written to the temp file `sldpick.tmp` (sld_toolp_surfs_to_temp_file_whole_or_partial) and highlighted by type;
3. status text (FUN_18001dbb0); `reset_solid_toolp_assignments`; `sld_start_toolP_surf_edit(op, drive ? 0 : 4)` (S:1019: for every
   solid record, find its SldToolPOp, roll the solid back to it, load the picked tags / types and highlight them);
4. `sld_set_toolp_srf_type(drive ? 0 : 4)`; prompt 406 (drive) / 407 (check; 451 "Select Surfaces" for multiaxis ops); select_ents
   mask 0x110100; Esc -> `sld_UndoStartToolPSurfEdit` (S:3896), nothing changes;
5. `sld_sel_selected_solid_surfs(false, !drive)`; `sld_complete_toolP_surf_edit(drive)` (S:50: rebuild / update one SldToolPOp per
   body, delete the empty ones; returns "changed");
6. the new list is compared with the old (counts and FUN_18000d340 per record, l.4700-4880); if different, FUN_18000f7d0 (l.10345)
   rewrites the op's kind 4 records;
7. clear selection, repaint, close the temp file.

EditSelectedGeomType(op, .., type 0 / 1 / 2 -> flags 0 / 4 / 0x20, prompts 406 / 407 / 177 "Select Gouge Surfaces") is the multiaxis
variant (callers l.35858-36896; dialogs 140 CEditChk, 150 CEditDrv, 1314 CEditComp, 1336 CEditChk5): **out of this scope** (no
multiaxis ops in the port).

Op 0x84 only: a list-based variant behind `thunk_FUN_1803aeac0()` (FUN_1800393c0, FUN_180039780 ...) - that function returns 0 in this
build (l.588128), so the path is dead and is left out. Dialog 1323 "Mill Toolpath Surf Type" (MillToolpathSurfTypePage, l.135286) is
a parameter page, not traced here.

---------------------------------------------------------------------------------------------------------------------------------

## 2. Data model

### 2.1 The reference record (TP_ID, cls 0x4000 sub 2; in memory ent_tp, 0x388 bytes)
Built by tp_surf_list (l.22182-22300, l.22463-22600; local_5658 = the record), measured on the corpus (2.7):

| off | type | meaning |
|---|---|---|
| +0x68 | i32 | op id |
| +0x6c | i32 | referenced entity's idn (+0x34) |
| +0x70 | i32 | referenced entity's +0x30 (its creation stamp; 4967 of 5319 match) |
| +0x74 | i32 | **selection class**: 0x100 for every kind 4 record, also for solids (22 / 22 solid refs in 2017); 1 for kind 1 / 8 picks on wireframe |
| +0x78 | i32 | **the referenced entity's class**: 0x100 surface, 0x10000 solid, 0x100000 mesh, 0x40000 CAD file (matches every measured ref) |
| +0x80 | i64 | the referenced entity's eptr (kind 6: the eptr of its first member record) |
| +0x88 | i64 | the op's OP_ID eptr |
| +0x90 | i16 | kind: 1 point, 4 surface / solid / mesh / file, 6 chain header, 7 chain member |
| +0x92 | i16 | 0 |
| +0x98 | u16 | kind 4: flags (below). Kind 6 / 7: chain number |
| +0x9a | i16 | kind 4 solid: the **SldToolPOp link id** (2.3); 0 otherwise |
| +0x9c | i32 | kind 6: entity count; kind 7: index in chain |
| +0xa0 | | kind 6: 0x100; kind 7: eptr of its header |
| +0xa4 | char[] | kind 6: name ("Chain N"; the number does not follow +0x98) |
| +0xa8 | i32 | kind 7: 1 = reversed |
| +0xe0 | u16 | kind 6: role, 2 = containment, 3 = curves / blend chains (save_chain's 6th argument) |

**2018 files** (TP record 0x398 bytes): every field from +0x98 on is 0x10 later (+0xa8 flags / chain, +0xaa link, +0xf0 role)
[measured, 395 parts]. The kind 22 records (only in 2018 op 132) are NOT MEASURED.

Kind 4 flags (+0x98): `0x4` = check, `0x20` = gouge (multiaxis), `0x1` / `0x2` = counted separately by msurf_ent_count (l.16410-16418:
bit 1 without bit 2 is not a drive) - their meaning NOT DECODED (strings 216 suggest variable stock-to-leave groups); meshes get `2`
when not check / gouge (l.22545-22552). In the corpus's 3D ops only 0 and 4 occur; 0x20 never.
Surfaces carry the same bits 4 / 0x20 in their own entity byte +0x6b (PMESH: +0x12a) while an op is being edited: surf_sel_bits
copies the op's flags there and tp_surf_list reads them back (l.21512-21537, l.22502-22510, l.22581-22588).

Op fields touched by every store: +0x88 i32 reference count (iStack_28a8), +0x94 = 1 dirty, +0x97 = 0 (l.19917-19922).

### 2.2 Drive / check / gouge per op
- A surface / mesh / CAD file: one kind 4 record; drive = flags without 4 / 0x20; check = 4.
- A solid: **one kind 4 record per solid** (+0x78 0x10000, +0x80 SOLID_ID eptr, +0x9a link id, flags 0); which faces and their roles
  live in the solid's history (2.3). Counts per solid: `sld_num_of_surfaces_in_toolp(solid, link, &check, &gouge, ..)` (S:426) returns
  all faces (the whole body's face count when the op is whole) and the type-4 / type-0x20 counts; drive faces = total - check - gouge
  (msurf_ent_count l.16619-16650, l.16437; _v2 at S:4003 corrects faces whose own type overrides the body type).

### 2.3 Solid faces: SldToolPOp (ops-history type 0x18) in the solid
- Created by `sld_surfs_to_toolp_op` -> FUN_1800b8880 (I:203), one per solid with picks; appended to the solid's ops history (the block
  after the XT, formats/solid_history.md 2.6). Same class as the solid chains of formats/mcam.md "Solid chains".
- Fields (ctor FUN_18028c700, 0x170 bytes): +0x90 u8 1 = whole body; +0x94 / +0x98 / +0xa0 the face list - per face a u64 (the face's
  `mcEptrPtr` attribute = an McEptr record id, formats/solid_history.md 3) and 0x18 bytes (a point: the face's box centre per
  FUN_180275980, I:1338; the corpus shows pick-like points); +0x148 u16 the body's type when whole; +0x14c count / +0x150 u16 type per
  face; +0xf0 kind (0 = surfaces), +0xf4 link id (vfunc +0x148, FUN_18028e490). Serialized by FUN_18028e9d0
  (decomp/solid_history/h2_op_base_extrude_toolp.c l.1194, 1332): +0xf0 / +0xf4 as 8 bytes, then u16 +0x148 and the type array (file
  version >= 5).
- Link: `sld_assign_toolp(kind, ent*, int* id, ..)` (A:3) takes an SldToolPOp whose link id is 0 and gives it max + 1 (FUN_18026a080);
  that id goes to the TP record's +0x9a. Measured: 22 / 22 solid refs match one type-24 op of their solid (data +0x17 = the solid's
  eptr; size 97 + 34 n for n picked faces; the id at data +0x43 + 0x20 n).
- Reading back: the solid is rolled back to that op (FUN_18026c170) and each stored u64 is matched against the faces' mcEptrPtr
  attribute (FUN_180274280, I:1231; `SldToolPOp::GetToolPSurfSelections` FUN_18028e300, I:364).
- Corpus: every 2017 3D op uses whole bodies (n = 0). Face picks occur in 5-axis ops (CURVE 5 AXIS TOOL AXIS CONTROL LINES op 1,
  DRILL TIP CONTROL PROJECTED POINT op 1) and 2018 Propeller ops 1 / 3 (check / drive, 2 faces each).

### 2.4 Chains
Kind 6 header (role +0xe0) followed by its kind 7 members (formats/mcam.md "Toolpath geometry references"). Role 2 = containment
(25 records in 3D ops), 3 = curves / blend chains (4, op 109). Solid edges in a containment chain go through
sld_chains_to_toolp_op (an SldToolPOp of chain kind, formats/mcam.md "Solid chains").

### 2.5 Points
Kind 1 records (init_tp_ent(.., 1, ..)): start / entry / radial / 4-axis point, project points, plunge corners. Op flags set with them:
msurf_op_set_has_start_pt / has_entry_pt / has_hmmpoint / has_points / has_grid / has_curves (handlers above). No kind 1 record occurs in
a 3D op of the corpus (NOT MEASURED for 3D ops).

### 2.6 The counters
TpEntTypeStats (msurf_ent_count l.16370, ints): [0] surface drive, [1] surface check, [2] surface flag-1, [3] surface gouge, [4] solid
drive faces, [5] solid check faces, [6] solid flag-1 faces, [7] solid gouge faces, [8..11] the same for PMESH, [12..15] the same for
FILE_ID, [16] role-2 chains, [17] role-3 chains, [18] points. FUN_180016370 folds PMESH into surfaces.
EntCountStats (UpdateEntCount l.452826): +0 drive (surfaces + solid drive faces), +4 check (surfaces + solid faces + CAD), +8 CAD drive,
+0xc containment chains, +0x10 curve chains, +0x14 points, +0x18 flag-1 total, +0x1c CAD flag-1, +0x20 CAD check, +0x24 gouge total.

### 2.7 Corpus (2017, by op type; scanner decomp/surfsel/recs_scripts)
| op (132/style) | ops / parts | kind 4 drive | kind 4 check | kind 6 (role) | kind 7 |
|---|---|---|---|---|---|
| 10 Rough Pocket | 12 / 11 | 431 (2 solids) | 0 | 11 (2) | 44 |
| 11 Finish Parallel | 1 / 1 | 34 | 0 | 1 (2) | 4 |
| 14 / 15 / 43 | 1 / 1 / 2 | 6 / 20 / 12 | 0 | 0 | 0 |
| 109 Blend | 2 / 2 | 20 | 0 | 4 (3) | 600 |
| 132/0 Area Roughing | 5 / 5 | 372 | 0 | 2 (2) | 16 |
| 132/1 OptiRough | 4 / 3 | 7 | 0 | 3 (2) | 30 |
| 132/2 Waterline | 2 / 2 | 145 (S1 wrote 102; S2's recount: TUTORIAL 2 SWARF op 4 94 + SWARF_METRIC op 3 51) | 43 | 2 (2) | 432 |
| 132/3 Scallop | 3 / 3 | 192 | 0 | 3 (2) | 433 |
| 132/4 Horizontal Area | 2 / 2 | 2 (solids) | 0 | 0 | 0 |
| 132/5 Raster | 4 / 3 | 80 | 0 | 0 | 0 |
| 132/6 Pencil | 1 / 1 | 38 | 0 | 0 | 0 |
| 132/7 Hybrid | 3 / 3 | 7 | 0 | 3 (2) | 30 |
Examples: TUTORIAL 7 METRIC op 5 (2 surfaces + containment chain of 13); TUTORIAL 2 SWARF_METRIC op 3 (51 drive + 43 check);
Benchmark 3_0 for 2017 ops 2 / 4 / 6 / 7 (one solid, link ids 6 / 8 / 9 / 10). No 3D op references a mesh or a CAD file.

### 2.8 What this means for the current port
- `viewer/mcam_load.c` (TpRef, ~l.488) reads the class from +0x74 (the selection class): solid refs look like surfaces; and it reads
  2018 records' chain / index / role 0x10 too early.
- `cam3d_rough.c` c3_mesh_op (l.82-85) and `cam3d_blend.c` (l.121) take every kind 4 record with "cls 0x100" as drive: check surfaces
  (flag 4) are machined as drive; solid refs are dropped silently (their idn is not a surface).
- CamOp drive[16] / dsurf[256] and the side-file DRIVE / DSURF lines (cam2d.c l.4484-4600) have no check / role / face data.

---------------------------------------------------------------------------------------------------------------------------------

## 3. What each 3D op asks for (port type -> Mastercam op)
`msurf_prompt_st_pt` from msurf_map (l.339990-340357); dialog from 1.7; Boss/Cavity from 1.3 (2).

| port type | Mastercam op | Boss/Cavity | after the dialog | dialog |
|---|---|---|---|---|
| CAM_SRPARALLEL | 5 | yes | start point if op +0x1382 | 116 |
| CAM_SRPROJECT | 7 | yes | by source +0x136c: source ops / curves / points | 126 / 128 |
| CAM_SRPOCKET, CAM_SRMPOCKET | 10, 0x83 | - | entry point if rpock_prm +0x1e0 | 122 |
| CAM_SFPARALLEL | 0xb | - | start point if op +0x12aa | 116 |
| CAM_SFRADIAL | 0xc | - | radial point (always; prompt 309) | 118 |
| CAM_SFPROJECT | 0xd | - | by source +0x1298 | 126 / 128 |
| CAM_SFCONTOUR | 0xf | - | start point if op +0x1368 | 116 |
| CAM_SRPLUNGE | 0x2c | - | the two plunge corners (415 / 416) unless +0x13b2 == 1 | 130 |
| CAM_BLEND | 0x6d | - | the two blend chains (role 3) | 120 |
| CAM_OPTIROUGH, CAM_AREAROUGH, CAM_WATERLINE, CAM_SCALLOP, CAM_HORIZAREA, CAM_RASTER, CAM_PENCIL, CAM_SPIRAL, CAM_RADIAL | 0x84 styles 1, 0, 2, 3, 4, 5, 6, 9, 10 | - | start point if op +0x1657; FUN_180009270 box centre | 116 |
| CAM_HSTPROJECT | 0x84 style 15 | - | as above | 1519 |
All of them: drive (+ check, containment per configuration), select mask 0x110100; containment when msurf_op_has_tcbs(op) (which ops
have containment is that function's answer, NOT DECODED here). Rough radial (6) and the flowline ops (8 / 0xe) are not port types.

---------------------------------------------------------------------------------------------------------------------------------

## 4. NOT DECODED (left out until decoded)
1. mccore `select_ents` itself: the selection toolbar, window / chain selection of surfaces, End Selection UI, how it hands solid picks to
   mcgeomsld (only OnSolidsSelectActivate's call is known).
2. `sld_array_pick` / `sld_pick_one_ent` internals (the Parasolid hit test), GenerateBodyFaceStructArray beyond grouping by body,
   CreateSldToolpOpFromFaceSelection (A:581, half read).
3. ~~How the per-face u64 (McEptr id) is written on save and remapped on load~~: decoded by S3 (section 7). Still open: the link id
   that sld_start_toolP_surf_edit reads at record +0x2c8 (disassembly 1800cdd59) vs +0x9a used by mcmsurf; which XT face a picked
   face is without a Parasolid replay (7.4, question SS-5).
4. Kind 4 flag bits 0x1 / 0x2 (meaning), kind 22 records (2018).
5. `msurf_op_has_tcbs` (which op types / settings ask for containment), `FUN_18001b8a0` (CAD file pick), `FUN_18001ccc0`'s point_proc_assoc
   options, `set_flow_surf` (Flowline data dialog).
6. The 116 / 1519 Color buttons (0x25c / 0x25d) and the 0x84 list-view variant (dead in this build).
7. Writers of `msurf_all_geom_preselected` / `msurf_chain` (none found).
8. The check / drive conversions behind strings 284-287 (HST checks), the "Remove" semantics of RemoveSelectedGeomType (multiaxis).
9. Start points in stored 3D ops (no corpus example).

## 5. Open questions for the user (do not decide here; also in formats/TODO.md "Waiting on the user")
- SS-1 Face hit test: Mastercam picks solid faces with Parasolid (`sld_array_pick`, NOT DECODED, no Parasolid here). The port has
  OCCT-meshed solids. May a face click be resolved by a ray against our tessellation of each XT face (an own hit test, like
  st_pick_drive_at today), keeping everything else (lists, types, storage) as decoded? The alternative (Mastercam default) is to wait
  for a decode of the pick, which needs Parasolid behaviour we cannot run.
- SS-2 Order of the picks: Mastercam asks start points / plunge corners / blend chains **after** the op dialog (1.3 steps 9-11). The port
  asks them before it (user decision T-2 for Plunge, and Blend / Radial by the same code, cui_chain.c drv_done). Move them after the
  dialog as Mastercam does (default), or keep T-2?
- SS-3 The Configuration - Toolpaths settings (Drive None / Prompt / All, Check None / Prompt / Unselected, CAD file, Containment,
  Show dialog): read them from the user's Mastercam configuration as the app reads its other configuration items, else
  Mastercam's defaults (Drive Prompt, Check None, Containment None, dialog on) - and show them in our Configuration dialog? (Default:
  yes, as decoded; question only whether our Configuration page gets the group now or later.)
- SS-4 Saving ops with solid drive / check faces needs writing an SldToolPOp into the solid's history (XT ops block). Until that writer
  is decoded and ported (stage S6), such ops keep being refused at save (as OptiRough is now, mcam_saveops.c l.1076). OK?
- SS-5 (S3, 2026-11-12; section 7.4) Which XT face a stored solid face pick is: Mastercam knows it only from the face names
  (CNC/mcEptrPtr -> McEptr record) that a Parasolid replay of the history re-creates, with the stored box centre as tie-breaker between
  faces of the same name (FUN_180269310 / FUN_18026e030). We cannot replay. Options: (a) leave stored face picks unmapped (Mastercam
  default as far as decodable: S2 / S4 then treat such an op's solid faces as NOT DECODED - refuse to regenerate the op, or ignore those
  faces with a message); (b) DEVIATION: map each stored face to the XT face whose own box centre is nearest to the stored point
  (Mastercam's tie-breaker applied to all faces instead of the same-name ones; our box is not Parasolid's PK_TOPOL_find_box, so the
  choice can differ - the corpus has 10 stored picks, none checkable against Mastercam here); (c) decode / emulate the history replay
  (needs Parasolid behaviour for every op type: not feasible). Default: (a).

---------------------------------------------------------------------------------------------------------------------------------

## 6. Staged port plan
Stages are cut so that separate agents touch few common files. Each stage: no improvisation, cite this doc; NOT DECODED stays out.

**S2 - data model + .mcam load (no UI change)**
- Files: `viewer/mcam_load.c/.h` (TpRef: sel class +0x74, entity class +0x78, eptr +0x80, flags +0x98, link +0x9a, role +0xe0; the 2018
  +0x10 shift by record size / file version), `viewer/cam2d.h` (CamOp: a reference list replacing drive[16] / dsurf[256] - per entry the
  entity (surface / solid / mesh), role drive / check (flags), solid link id, and the op's containment / curve chains by role),
  `viewer/cam2d.c` (side file lines; keep reading DRIVE / DSURF), `viewer/cam3d_rough.c`, `cam3d_blend.c`, `cam3d_srough.c`,
  `cam3d_finish.c`, `cam3d_horiz.c`, `cam3d_pencil.c` (consumers: drive = no flag 4 / 0x20; checks kept apart; whole solids from records
  with +0x78 0x10000).
- Verify: a dump of the parsed refs for the corpus (counts must equal table 2.7, both 2017 and 2018); `tools/oracle3d.py` /
  `tools/tests` 3D regressions unchanged except ops that had check surfaces (TUTORIAL 2 SWARF_METRIC op 3: checks no longer machined
  as drive) and solid-ref ops (Benchmark 3_0: now meshed from the solid) - list each change with its reason.
- **Status: DONE 2026-11-12 (branch of agent S2).** `TpRef` (mcam_load.h) has cls +0x74, ecls +0x78, eptr +0x80, flags +0x98, link
  +0x9a; the loader reads every field from +0x98 on 0x10 later in a 0x398-byte (2018) record (`tp_sh`; measured on all 7083 2018 TP
  records, kinds 1 / 4 / 6 / 7 / 0x17 - this also fixes 2018 chain number / index / rev / role, point xyz / mode and the stock model's
  source op; kind 30 has no 2018 example). `tp_ref_role` = msurf_ent_count's rule (gouge 0x20, check 0x4, bit 0x1 without 0x2 counted
  apart - NOT DECODED, refused by the engines -, else drive). `CamOp.ref[CAM_REF_MAX] / nref` (`CamRef`: entity class, F.surfs /
  F.solids index, idn, flags, link) replaces drive[16] / dsurf[256]; `cam_refs_from_tp` (a stored op's kind 4 records, file order),
  `cam_op_set_drive` (the current pick, cui_chain.c drv_done / cui_ops.c, and cam_cli --solid / --surf). Side file: `REF <cls> <index>
  <idn> <flags> <link>`; DRIVE / DSURF lines still read. Consumers: `c3_mesh_op` (cam3d_rough.c; every cam3d_* engine meshes through
  it) and `blend_drive_from_tp`: drive refs only, check / gouge refs left out (S7), a mesh / CAD-file drive refused (NOT PORTED), a
  solid's role from its SldToolPOp (S3's sldtoolp_get: whole body by its body type +0x148; single face picks refused, N13-7);
  unresolved surfaces are refused instead of skipped, and the old 256-surface cap is gone. Chains by role: `CamOp.chains` with
  `Chain.role` (2 containment / 3 curves) - roles of chains picked here come with S4.
  Verify: `cam_cli PART tprefs` (new) against the raw records (scratch rawrefs.py) on all 908 corpus parts: equal on every part (886
  read; 22 newer-release parts refused at load); 4842 kind 4 refs, all resolved to a surface / solid. Per op type the 2017 counts equal
  table 2.7 (Waterline: 145 drive, see the table's note); 2018: op 10 417 surfaces + 2 solids, 11 34, 14 6, 15 20, 109 20 (4 role-3
  chains, 600 members), 132 879 surfaces + 3 solids (11 role-2 chains), no check refs in 2018 3D ops.

**S3 - solid face identity (decode + read-only)** - DONE 2026-11-12 (section 7): viewer/sldtoolp.c/.h, xt_faces() in xt_mesh.c,
SolidOp.base_n in mcam_load.c; tools/tests/sldtoolp_test.sh (expected output checked in). Picked face -> XT face: NOT DECODED (SS-5).
- Decode the remaining SldToolPOp read (FUN_18028e9d0 face list, item 3 of section 4) against formats/solid_history.md 2.6 / 3 (McEptr
  ids, mcEptrPtr attributes in the XT); map each picked face to our XT face (viewer/xt_mesh.c face index).
- Files: `viewer/mcam_load.c` (solid_history(), l.667-: the ops block parser, formats/solid_history.md), `viewer/xt_mesh.c/.h` (face
  lookup), a new `viewer/sldtoolp.c/.h` (link id -> faces + types).
- Verify: the corpus SldToolPOps (22 solid refs; the 2018 Propeller face picks): face count = sld_num_of_surfaces_in_toolp's rule
  (whole body = all faces); a peloader oracle (`tools/peloader`) only if mcgeomsld's reader runs without Parasolid - it rolls the body back
  (FUN_18026c170), so most likely not; then the corpus counts are the check.

**S4 - the pick flow (depends on S2; S3 for faces)**
- Replace drv_pick / drv_click / drv_done (`viewer/core_ui/cui_chain.c` l.88-158) and `st_pick_drive_at` (cui_stock.c) by msurf_assoc's
  order (1.3) and FUN_18001bcf0 (1.4): config-driven Drive / Check modes, prompts 406 / 407, mask 0x110100 (0x10100 for flowline),
  Esc = cancel -> 427 keep-op question, the check-over-drive conversion, the 241 message; containment by chaining (prompt 434, role 2)
  through the existing chaining code (`cui_chain.c` chain_dlg*), solid face picks per SS-1, solid body picks.
- Files: `viewer/core_ui/cui_chain.c`, `cui_sel.c` (masks / sel bit for surfaces), `cui_stock.c` (remove the old pick), `core_ui.h`,
  Qt hooks in `viewer-qt/` only for prompts / Enter / Esc if not already generic.
- Verify: new golden scenes in `tools/tests/op_scenes.txt` / `srf_scenes.txt` (golden_lib.sh, BLESS=1 once reviewed): pick drive, pick
  check, Esc at each prompt, Drive All / Check Unselected, containment chain; the resulting ref list printed by the scene.

- **Status: DONE 2026-11-12 (branch of agent S4).** viewer/core_ui/cui_chain.c section "3D surface ops (S4)" (ms_* functions):
  - Configuration items 17017 / 17018 / 17019 / 17023 read as the other config items (cui_chain_config_item; script step
    `cfgitem:KEY=VALUE`), mccore's reset values otherwise (1 / 0 / 0 / 1). The Configuration page itself is S5 (N13-5).
  - Order (1.3): dialog 100 for ops 5 / 7 (hook `cui_hooks.radio_dialog`, Qt dialog at the template positions; FUN_180010560's fields
    written as dialog record edits after the op is seeded) -> FUN_18001bcf0 (Drive None / Prompt 406 / All, Check None / Prompt 407 /
    Unselected; records in tp_surf_list order: drive surfaces (F.ents order), check surfaces, then one record per solid with picks in
    reverse solid order; a surface picked again as check: not added, its drive record gets flag 4 with Check Prompt; solid picks
    through mcgeomsld's list rules FUN_1800cd8a0 / FUN_1800af580 / FUN_1800a92f0, so a drive body picked as check becomes check)
    -> containment chaining when msurf_toolcont == 1 and msurf_op_has_tcbs (now decoded: mccore @1800c4850, ops 5, 6, 9, 10, 0xb, 0xc,
    0xf, 0x27-0x2b, 0x6b, 0x83, 0x84), prompt 434, chains role 2 -> the S5 hook (EditAllGeom; goes straight on) -> open_params ->
    at OK the fix-ups of ops 7 / 0xd / 0x2c, then the start / radial point (310 / 309), the projection source's picks (curves:
    chaining 356, role 3; Plunge Zigzag corners 415 / 416), Blend's chains (role 3) - all after the dialog (N13-4, replaces T-2) ->
    generation if references > 0, else message 241 once per session (Drive not None) and the op kept.
  - Esc: drive / check prompt -> the op is dropped at once (cancel flag, no question); containment -> no containment, on to the
    dialog; start / radial point -> none, on; plunge corners / blend / curve chaining -> 427. Dialog Cancel -> 427 (Yes: kept as
    seeded + dialog 100's fields, dirty, no toolpath); generation failure / cancel -> 427 [inferred: bnci_section's flag 0].
  - Picks: surfaces through the general selection (ent_sel = selection bit 4, drawn selected; pick_entity_surf: surfaces only);
    solids through our hit test (N13-3: sld_pick_part_solid / sld_pick_part_face = a ray against this program's XT face
    tessellation, cui_sldchain.c topo, XT FACE order). Body or face: the Selection Bar's solid buttons (below, N13-8).
  - Data (S2 layout kept): CamOp.ref as tp_surf_list makes it; solid records link 0 (no SldToolPOp written: S6) and flags = the body
    type / the faces' common type (corpus-observed, 7.3; the code setting the solid's bits NOT DECODED). Addition: `CamOp.rface[]`
    (CamRefFace: ref index, face ordinal, type 0 / 4 / 0x20), side file `RFACE <ref> <face> <type hex>`. An op with rface is refused
    at generate (cam2d.c cam_generate) until the engines take single faces (S7); whole bodies / surfaces generate as before.
  - Not ported / NOT DECODED here: the S5 dialog, op 0x84's FUN_180009270 box centre, the CAD file pick FUN_18001b8a0 (config 17017
    read, unused), ops 7 / 0xd Points pick (select_surf_points mask 1; the engines refuse Points), flowline ops, meshes (no PMESH
    entities here), Shift + face tangent faces, the highlight of picked solid bodies / faces (ps_hilight_edges' colour per topol
    type), select_ents' own UI (Enter / Esc only), the order between a surface and a solid under the cursor (ours: the surface).
    (Generation after the post-dialog picks: since N13-9 on the worker thread, below.)
  - Tests: tools/tests/op_scenes.txt s4_* (25 scenes; step `srfsel` prints the flow and every 3D op's references) and the updated
    gen_srplunge / gen_blend* / gen_sfradial (picks after the dialog), gen_optirough_refuse (no empty End Selection first).
  - Regression vs the base 501e611 (same suites, same machine): wf / srf / chain / solid / misc / tool / note / units / blend / horiz /
    pencil / hst_rest / stockmodel / xformop / draft_* identical, cad identical (0 failed), fileop 410 / 410 identical. op: changed
    only in the 3D gen_* scenes - the prompt is mcmsurfres 406 exactly (was ours with counts), dialog 100 asked for ops 5 / 7, the
    picks of Plunge / Radial / Blend after the dialog (toolpaths identical: 2221 / 57417 / 164 points), and an engine refusal after OK
    now ends in the 427 question instead of leaving the dialog open (gen_optirough: "nothing to cut" as before, the op kept dirty,
    so the post asks dialog 977 and writes no NC; gen_optirough_refuse / gen_srpocket_refuse likewise) - blessed.

- **N13-8 / N13-9 (2026-11-13, answers to SS-6 / SS-7)**:
  - **Body / Face buttons** (N13-8). Decoded: the Selection Bar's solid buttons are two independent switches, each toggling its own
    bit of the solid pick mask - OnSolidsSelectBody @1800d1260 (bit 1) and OnSolidsSelectFace @1800d13a0 (bit 4), mcgeomsld
    (decomp/mcgeomsld_hover.c); sld_pick_one_ent @1800c2560 builds the mask from the allowed kinds less the buttons not checked;
    sld_identify_from_cursor @1800bd010: mask 0 -> no solid hit, a face hit stays a face with bit 4, else it becomes its body
    (LAB_1800bd1c3); enabled only while a solid selection runs (OnUpdateSolidsSelectBody @1800d15c0). Their ribbon entries:
    ribbonuires IDR_BCGP_RIBBON_XML, the Home category's ELEMENTS outside its panels, 53394 GSSolidBody "Select Solid body" and 53396
    GSSolidFace "Select Solid face", images INDEX_SMALL 68 / 70 of IDR_BCGP_IMAGE_CATEGORY_SMALL_0 (menu text cmd_labels.tsv
    "Select body" / "Select face" = opmanres 1917 / 1916). Port: core_ui `ms_sld_body` / `ms_sld_faces`, `ms_selbar_enabled` (the
    drive / check prompt), `ms_selbar_toggle`; drv_click applies the mask rule; Qt: two checkable buttons with those images and
    tooltips (mainwindow.cpp buildStatus / updateSelBar). **NOT DECODED**: the buttons' state when the 3D toolpath selection starts
    (N13-8: Body on, Face off, set at every flow start - whether Mastercam keeps the last state between selections is not known);
    the Selection Bar itself (its place under the ribbon, its other controls: Standard / Window / Chain ..., Quick Masks, Activate
    solid selection, Select edge) - **ours**: the two buttons sit in the status bar after the plane panes and are shown only while
    they are enabled (Mastercam shows them greyed otherwise), so the window does not change outside the prompt; OnSolidsSelectFace's
    early return when FUN_180097cb0 is false and faces are "enabled in get_event" (not reached in this flow: not ported).
    Tests: op_scenes s4_selbar_face (disabled outside the prompt, Face on -> face picks, Body off, both off -> no solid pick),
    s4_selbar_reset (the state reset at the next flow, a body pick then a face pick on one solid), s4_selbar_check (the check
    prompt); steps `selbar[:body|face]` (core) and `selbarclick:body|face` (Qt: the button through its widget). The old
    `drvfacemode:0|1` step still sets the Face button.
  - **Generation after the picks on the worker thread** (N13-9). ms_generate (cui_chain.c) queues the job when the front end has
    `cui_hooks.gen_queued` (ms_gen_take hands it over); the Qt app runs it with the op dialog's machinery (OpDialog::runJob: worker
    thread, InputHold, the progress window after 400 ms, Cancel for the abortable engines only) once the input that ended the
    last pick is handled (MainWindow::queueGen / runPendingGen), then gen_end as before (ms_gen_end: commit, or the 427 question).
    Scripted runs (testhooks.cpp runScript, the MCP server's steps) run the queued job right after the step that queued it, before
    the step's dump - the same results in the same order as before. cam_cli / front ends without the hook compute at once.
    Every new 3D op of the flow goes this way (with or without picks after the dialog: OK always passes through ms_post_next).
  - Regression (2026-11-13, same machine, base 375c86c built in the same worktree): op_compare 50 / 630 differ on both, the same
    files (the 3 new s4_selbar_* scenes MISSING golden on both + 47 pre-existing); no output changed but the 3 new scenes (blessed).
    gen_srplunge / gen_blend / gen_sfradial identical (their generation now runs through OpDialog::runJob: MCAMUI_OK_INPUT=main:0
    reports the progress machinery after plgpt).

**S5 - the "Toolpath/surface selection" dialog (Qt; depends on S2, S4)**
- The dialog per op type (1.7 table, dialogs 112-132 / 1519 from mcmsurfres_dialogs.tsv), counts from EntCountStats, Select / Remove /
  Show handlers, re-entry from the Toolpaths Manager geometry item (EditSelectedGeom flow, incl. the compare-and-rewrite step).
- Files: new `viewer-qt/surfseldlg.cpp/.h`, `viewer/core_ui/cui_surfsel.c` (logic, testable without Qt), `cui_ops.c` (geometry item).
- Verify: Qt-vs-golden UI tests (tools/tests/golden; the dialog's layout from the resource like the other dialogs), scene tests for
  each button.

**S6 - .mcam save of the references (depends on S2, S3)**
- Write kind 4 / 6 / 7 / 1 records exactly as 2.1 (2017 and 2018 layouts), op +0x88 count, +0x94 / +0x97; SldToolPOp writing for solids
  (with S3's decode of the writer) - until then ops with solids stay refused (SS-4).
- Files: `viewer/mcam_saveops.c` (l.1076 refusal), `viewer/mcam_write*.c`.
- Verify: round-trip on the corpus ops of table 2.7 (load -> save -> byte compare of the TP records), and opening the saved part in
  Mastercam on Windows (formats/windows_checklist.md item to add).
- **Status: DONE 2026-11-12 (branch of agent S6) for X9 / 2017 parts; 2018 refused.** viewer/mcam_saveops.c:
  - 3D port types are written (save_opcode_for: the classic opcodes 5 / 7 / 10 / 11 / 12 / 13 / 15 / 44 / 109 / 131 and 132 with the
    style byte +0x16bc, as cam_fileop.c default_op; mcam_opcode_for, which the dialogs use, is unchanged). The template is the op's own
    record or the defaults library's op of that opcode / style, **with its variable tail** (opcodes 9, 10, 15, 0x83, 0x84:
    formats/mcam_write.md 3 - 3D records of 0x2938 + 8 / 32 / 69 / 71 / 89 bytes in the corpus; the old exact-0x2938 test made every
    132 / pocket op fall back to the library). The parameters as for every op (heights, rpatch).
  - Kind 4 records: an op whose references were picked here (CamRef list) gets new records `put_k4`: tp_surf_list's fields (2.1) and
    store_ent_assoc's header (class 0x4000, subtype 2, selection 0x0101, colour tp_linear_color - a configuration colour not read here,
    0x9f as the other TP writers -, level main_level = F->main_level, no attributes; mccore 0x180034efd). A stored op edited here keeps
    its own kind 4 records (new ids / op id / op eptr), as edit_operation does. The records go before the op's chains (msurf_assoc
    steps 4 / 5). Op record: +0xf0 counts them; +0x151 = 1 with a solid reference (store_ent_assoc -> FUN_1800c1250(op, 1, 1), mccore
    0x1800c1250 = op +0xe9; measured 20 / 20 X9 / 2017 ops with a solid kind 4 record, 0 / 444 without); +0xff = 0 for a new list
    (FUN_18001bcf0's rewrite, +0x97). Header +0x6c now counts the kind 4 entities too (measured: = entities with +0x38 > 0).
  - Solids: a reference with a link (stored) names its existing SldToolPOp (checked with sldtoolp_get; the history is kept byte for
    byte, also for face picks: nothing about faces is written). A solid picked here (link 0, whole body) gets a new whole-body
    SldToolPOp appended to its history (`sldtoolp_whole_bytes`, 97 bytes, every byte cited from FUN_1800b8880 / the ctors
    FUN_18028c700 / FUN_1802739b0 / FUN_180261b90 / FUN_180265420 / FUN_1802656b0 and the writers FUN_1802907e0 / FUN_180276400
    (dumped for S6: Ghidra mcgeomsld 0x180276400) / FUN_180266560; ops header +0x14 / count + 1, FUN_18026dc50 / FUN_180267170); link =
    the highest link of the solid's ops + 1 (FUN_18026a080, McOpsFile +0xb0 = 0 by the ctor), op id = the history position.
    Refused (NOT PORTED message, the op is not written): a solid not in the Mastercam part, a history not parsed, a rolled-back history
    (position < op count: FUN_180267170 then inserts mid-history and renumbers the later ops' records), a history with FBM Drill / Lathe
    toolpath ops (their slot 41 link query FUN_18027dc60 / FUN_18027f580 is not decoded). **Single face picks**: a CamRef has no face
    list; a new face pick needs the face's McEptr record id and Parasolid's box centre (7.4 / 7.7, NOT DECODED) - N13-6's refusal
    applies when S4 adds them.
  - Refused too: a referenced surface / entity deleted here; any 3D op into a part whose TP records are 0x398 bytes (2018): the
    2018 kind 4 record has fields the +0x10 shift does not explain (below).
  - Verify (tools/tests/surfref_save_test.sh, expected tools/tests/surfref_save_expected.txt; `cam_cli PART saveref all OUT [--gen |
    --newsolid]` takes every stored 3D op with kind 4 records over as an edited op of its port type and saves): 21 2017 parts, 36 ops
    of port types (26 others are not port types: 132 style 7 Hybrid, 14 Flowline, 43, multiaxis). Kept: kind 4 1380 / 1380 identical
    (but ids / eptrs / time / stamp / op id). Built (--gen): 1375 / 1380; the 5 differ only at +0x70, the entity's creation stamp,
    which the solid's record changed after the pick (Mastercam keeps the old one: the 352 mismatches of 2.1). Read back (tprefs):
    36 / 36 ops as the original. Op records 35 / 36 (but ids, +0xe8 / +0xec -1, +0xfc dirty); PROJECTBLEND op 7: +0x1a8 (its depth is
    incremental to the chain Z, which saveref's parameter read - cam_params_from_raw, chain Z 0 - leaves out: the writer stores it
    absolute; a harness limit). Chain records (the existing 2D chain writer, rebuilt from kind 6 / 7): only the header colour / level
    (0x9f / 1 vs tp_linear_color / main_level), kind 7 attributes (width / style: main_attrib at chaining) and the kind 6 name number
    ("Chain N" does not follow +0x98, 2.1) differ. --newsolid (Benchmark 3_0, HORIZONTAL, RASTER, RESTMATERIAL: 8 solid refs): the
    appended SldToolPOps equal the stored whole-body ones but for op id / version 0x1c / link / type; sldtoolp.c resolves every new link
    (Benchmark 3_0: links 11 / 12 / 13 after the stored 6 / 8 / 9 / 10). 2018 (test copies, records captured, not saved): 491 / 1375
    identical; the rest differ at 2018 +0xc4 (values 0 / 1 / 3 / 5 ...: NOT DECODED) and solid refs' +0x74 0x10000 (2017: 0x100).
    Windows check: formats/windows_checklist.md 4 (files in ~/mastercam-re/work/s6).

**S7 - engines use check / containment (depends on S2)**
- The 3D engines take check surfaces as gouge-protected geometry and the role-2 chains as containment, each per the engine's own decoded
  rule (formats/hst3d_port_plan.md 6.4 item 4, surf_rough.md, blend.md); where an engine's use of check / containment is NOT DECODED, the
  op refuses with a clear message instead of ignoring the geometry.
- Files: `viewer/cam3d_*.c`.
- Verify: oracle runs on the corpus ops with containment / check (TUTORIAL 7 METRIC ops 5 / 6, TUTORIAL 2 SWARF op 3) via tools/oracle3d.py,
  results appended per formats/oracle_results.md.

Parallelism: S2 and S3 can run at once (different files); S4 after S2 (S3 only for faces); S5 after S4; S6 after S2 + S3; S7 after S2.

---------------------------------------------------------------------------------------------------------------------------------

## 7. S3: solid face identity (decoded 2026-11-12; read-only port viewer/sldtoolp.c)

New dumps for this stage (mcgeomsld.dll, `~/mastercam-re/ghidra_sld`, DumpDecomp, read-only; quoted by address): SldToolPOp vtable
(decomp/solid_history/sld_op_vtables.txt l.1010-1052) slots 1 `FUN_1802901b0`, 25 `FUN_18028f030` (regen), McSldEntOp slots 1
`FUN_180275f30`, 21 `FUN_180276400` (write); `FUN_18026a240` / `FUN_180269d90` / `FUN_18026a010` (record id <-> record),
`FUN_18026a080` (next link id), `FUN_180269310` (records -> Parasolid tags), `FUN_18026e030` (box-centre distance).

### 7.1 The SldToolPOp in the file (FUN_18028e9d0 after McSldEntOp::Read FUN_180274c80 after the base FUN_1802649f0)
Ops version V = SOLIDXMT +0x49 (mc_read_opsfile); 64-bit X9 / 2017 / 2018 files:

| bytes | field | meaning |
|---|---|---|
| u8 | +0x90 | 1 = the whole body (slot 16 FUN_180274c20) |
| i32 n, n x u64, n x 0x18 | +0x94 / +0x98 / +0xa0 | picked faces: McEptr record id; the face's box centre at pick time (FUN_180275980 ps_get_bbox_center; part units - FUN_18026e030 compares it with PK_TOPOL_find_box x 39.37 / x 1000) |
| i32 n2, n2 x u64, n2 x 0x18; u8 +0xc0 (+ n2 bytes) | +0xa8 / +0xb0 / +0xb8 / +0xc8 | edges (chain kinds) |
| V > 2: i32 n3 + n3 x 0x48; V > 3: i32 n4 + n4 x 0x60 | +0xd0 / +0xe0 | (formats/solid_history.md 2.6) |
| i32, i32 | +0xf0 / +0xf4 | kind (0 surfaces, 1 / 2 chains) / link id (slot 41 FUN_18028e490) |
| V > 2: i32 + bytes, i32 + bytes, i32 n + n x i16 + n strings (u16 len) | +0xf8 / +0x108 / +0x118 | NOT DECODED (empty in the corpus) |
| V >= 0x1a: i32 n + n x i32 + n strings | +0x130 | NOT DECODED (empty) |
| V >= 5: u16; i32 n + n x i16 | +0x148; +0x14c / +0x150 | the whole body's type; per face its type (V < 5: all 0) |
| V > 0xb: i32 n + n x (u32 flag [+ object]) | +0x158 | flag != 0 -> object FUN_180278610 / FUN_180278d30: NOT DECODED (refused) |

The reader checks the bytes read against FUN_18028de60 (message 0x103 otherwise); the port does the same. Every SldToolPOp of the
corpus parses to its size.

### 7.2 Face id -> McEptr record (load)
After all ops are read, FUN_18026b3c0 calls each op's slot 1 with 0: SldToolPOp `FUN_1802901b0` -> McSldEntOp `FUN_180275f30(op, 0)`
-> per stored u64 `FUN_18026a240(opsfile, id, NULL, op id, 0)`: (ops version < 6 only: an old table at opsfile +0x80) the ops from the
**last to the first**, each op's first record list (+0x48) then its second (+0x68), the first record whose stored id (+0x20, kept by
FUN_1802649f0 until FUN_180266b20 zeroes it) equals the u64; 0 when none (or id 0). The u64 becomes the record pointer.
Corpus: **all 10 stored face ids resolve** (2017 / X9 / 2018).

Save side (for S6): FUN_18026c690 sets each record's +0x20 to its own address before a write (formats/solid_history.md 2.3); the
McSldEntOp writer FUN_180276400 writes the +0x98 u64s as they are (the record pointers = those ids). FUN_180275f30 mode 1 =
FUN_180269d90 (record -> its +0x20), modes 2 / 3 = FUN_18026a010 (null for records with +0x28 bit 2 or listed in DAT_1803e9a38):
their callers NOT TRACED.

### 7.3 The counts and lists
- `sld_num_of_surfaces_in_toolp(solid, link, &check, &gouge, b)` @1800c1820 (S:426): over every op whose slot 41 gives (kind 0, link):
  roll back to it (FUN_18026c170); whole -> total += PK_BODY_ask_faces(body), check / gouge += the same when +0x148 is 4 / 0x20; then
  total += +0x94 (FUN_180274240 sizes the tag list to +0x94: **unresolved faces count too**), check / gouge += the type entries equal to
  4 / 0x20. Drive = total - check - gouge (msurf_ent_count).
- `SldToolPOp::GetToolPSurfSelections` FUN_18028e300 (I:364; asserts +0x14c == +0x94, SldToolPOp.cpp l.0x6a1): the face tags
  (FUN_180274280: each id matched against the faces' CNC/mcEptrPtr record, FUN_180049740) with their types; **entries whose tag is 0
  are removed**; whole -> the body tag and +0x148 in the body lists.
- The regen FUN_18028f030 (slot 25) maps ids -> tags with FUN_180269310 and marks the op failed (op+0xa, message 0x133) when one is 0.
- Observed (corpus): the per-face type and a whole body's +0x148 equal the TP record's flags (+0x98) in every case: 0; 1 (CURVE 5 AXIS
  TOOL AXIS CONTROL LINES / PLANE); 2 (DRILL TIP CONTROL PROJECTED POINT); 4 (2018 Propeller op 1). Types 1 / 2 count as drive in the
  rule above (their meaning: section 4 item 4, NOT DECODED).

### 7.4 McEptr record -> XT face: NOT DECODED (question SS-5)
Mastercam names a face only through its CNC/mcEptrPtr attribute; the XT carries it empty (pointer fields are not transmitted,
formats/solid_history.md 3.1 / 3.3) and a history replay re-creates it (FUN_1802656b0; record order = the caller's tag order). The
resolution of a stored face, FUN_180269310: the record's own tag (+0x18, set by the replay) if it is a tag; else among the body's edges
(PK_BODY_ask_edges), then faces (PK_BODY_ask_faces), those whose mcEptrPtr record has the same kind, op id, ref1, ref2, and of them the
one whose box centre (FUN_18026e030: PK_TOPOL_find_box centre in part units, squared distance) is nearest to the stored point; none ->
message 0x11a. Both need the faces' names, i.e. a Parasolid replay: **not reproducible here**. A record's ordinal does not give the face
either: CURVE 5 AXIS ... LINES' face is record #28 of op 0 (Revolve, kind 0x22, ref1 = a chain entity), DRILL TIP's #13 of op 1 (Loft
Boss, kind 0x29), each followed by ops that change the body; the 2018 Propeller's faces are records #185 / #186 of op 0 (a Body op: 185
unnamed edges, then 6 unnamed faces, kind 0x34), and its XT has schema 28101 (not on this machine). A rough geometric look (vertex
boxes, tools/xt_parse.py) gives no unique face for DRILL TIP (two B-surface faces within 0.04 in of the point). The port leaves
xt_face = -1 for picked faces and asks **SS-5** (section 5). Whole-body references need no names: every face of the body.

The body a whole-body reference counts is the one rolled back to the SldToolPOp (FUN_18026c170). The port takes the stored XT when the
history position (+0x14) is the op count and every op from the SldToolPOp on is a toolpath op (mc_opType_is_toolpath @18002bdf0: 0x18,
0x1b, 0x1c) [inferred: toolpath ops add no geometry - FUN_18026a110 leaves them out of old files; their regen only maps tags]; true for
every corpus reference. Otherwise: not reproducible (no count).

### 7.5 The port (read-only)
- `viewer/sldtoolp.c/.h`: `sldtoolp_get(solid, link)` (the SldToolPOp; per face the resolved McEptr record, point, type),
  `sldtoolp_at(solid, op index)`, `sldtoolp_count` (7.3), `sldtoolp_faces(file, solid idn, link)` -> (XT face, type) list for S2 / S4
  (whole body: every XT face with +0x148; picks: the resolved faces with xt_face -1), `sldtoolp_solid_by_idn / _by_eptr` (TP +0x6c /
  +0x80), `sldtoolp_xt_faces`. XT face numbering: `xt_faces()` (viewer/xt_mesh.c) = FACE nodes in node index order = xt_mesh's face
  order = the keys of xt_occt's `face_of`.
- `viewer/mcam_load.c` solid_history(): SolidOp.base_n (the McOperation base's size; op+0x18 is u32 below ops version 0x19, which
  the old code skipped as 8 bytes).
- Not touched (stage S2): TpRef / CamOp. S2 / S4 pass the TP record's solid idn (+0x6c) and link id (+0x9a; 2018 +0xaa).

### 7.6 Verification (tools/tests/sldtoolp_test.sh OUTDIR; expected output tools/tests/sldtoolp_expected.txt)
All parts under <mastercam-samples> (2018 parts from a copy whose header says 2017, test only - the loader refuses
them, FV-1): 21 parts, **30 solid references (2017 12, 2018 10, X9 8), 30 links resolve**. 22 whole-body references: 16 counted and
mapped to all XT faces (Benchmark 3_0 / HORIZONTAL / RASTER 491 faces, RESTMATERIAL / RESTMILL 145, CURVE 5 AXIS ... PLANE 5), the 6
of 2018 NOT KNOWN (XT schema 28101 missing). 8 face-pick references with 10 faces: all 10 ids resolve to an McEptr record, counts per
the rule (Propeller op 1: 2 check; op 3: 2 drive), **0 mapped to an XT face (NOT DECODED, SS-5)**. Two kind-0 SldToolPOps that no TP
record names (RESTMATERIAL 2017 / RESTMILL X9 op 13, link 10). Kind-1 (chain) SldToolPOps (HS Dynamic 2018): outside S3.
Peloader oracle: not possible - every reader of the face list beyond the bytes (FUN_180274280, FUN_180269310,
sld_num_of_surfaces_in_toolp) calls Parasolid (PK_BODY_ask_faces / ask_edges, PK_TOPOL_find_box, attributes) after a rollback replay
(FUN_18026c170); the file-only part (FUN_18028e9d0 + FUN_18026a240) is what the port does and the corpus check covers.

### 7.7 Notes for S6 (writing an SldToolPOp)
- Writer chain: slot 21 `FUN_1802907e0` -> McSldEntOp `FUN_180276400` -> McOperation `FUN_180266560`; the McOpsFile writer
  `FUN_18026dc50` (formats/solid_history.md 2.1) writes each op as u32 type, u32 size (slot 19 = FUN_18028de60), body; toolpath op types
  are left out for file versions < 0xb (FUN_18026a110). Field order = 7.1 (strings: u16 len = strlen + 1).
- A new op: `sld_surfs_to_toolp_op` -> FUN_1800b8880 (I:203) from the picks (FUN_180275980: per face its McEptr record pointer = id
  and ps_get_bbox_center), appended to the history (FUN_180267170); link id `sld_assign_toolp` -> FUN_18026a080 = max over the ops'
  slot-41 links (start: McOpsFile +0xb0) + 1, stored in the TP record +0x9a.
- A whole-body reference needs no face ids (+0x90 = 1, n = 0, +0x148 = the type): writable from what is decoded.
- Face picks need per face the id of the McEptr record naming it and Parasolid's box centre: for a loaded solid we do not know which
  record names which XT face (7.4); a solid made here has records only if our writer emits them (formats/solid_history.md 5.1 item 6).
  So S6 can write whole-body references; face picks depend on SS-5 and on emitting McEptr records for our own solids.

---------------------------------------------------------------------------------------------------------------------------------

## 8. S7: the engines use check surfaces and containment (2026-11-12; viewer/cam3d_rough.c c3_check_rule / c3_containment)

### 8.1 Check surfaces (decomp/mcmsurf_all.c bnci_surface, process_3rd_party_algorithm, FUN_180258310; decomp/mckernel_all.c)
- bnci_surface counts the check surfaces (surf_sel_bits -> local_6c80, l.12172-12202; DAT_1805034a1 = 1 when > 0) and, for op 132,
  sets op +0x16c4 (record 0x172c = the kernel's K+0x495; K = TP - 0x60 by FUN_180923f60) per style (l.12212-12234): styles 0 Area
  Roughing, 1 OptiRough, 3 Scallop, 4 Horizontal Area, 6 Pencil -> 0; 7 Hybrid -> 1; any other style -> `mutlGetHstCheckTrimmingMethod()`
  (l.376213: registry "\Configurations\Variables\Toolpaths" "HST check trimming method", **1 when absent**; absent in the user's hive).
- 0x16c4 == 0: process_3rd_party_algorithm calls FUN_180258310(2, ..) (l.18212-18215), which writes every machine-model surface (drive
  and check: mach_model_surf +0x66, 1 = check, as FUN_180290fb0 l.429259 tests it) into tmphardmillraw.stl (l.395794) - the kernel's
  drive model and PosZHi / PosZLo source - and mckernel builds no check plan (K+0x495 tested at mk l.92847, 93569, ...). FUN_1800202d0
  (l.23046) warns mcmsurfres 282 "Check geometry in HST/Roughing operation will be treated as drive geometry" (styles 0 / 1) / 287
  "... in this HST/Finishing operation ..." (styles 3 / 4 / 6). **Ported: the check references are meshed with the drive, the warning
  goes to op->warning.** (No stored check of these styles in the corpus: unverified.)
- 0x16c4 != 0: FUN_180258310(0, ..) writes the drive only; mckernel loads the checks (FUN_18007ca68 mk l.92160: Check Database Loader
  0x1421, Detect Top Check Surfaces 0x1423, Check Surface Tessellation 0x1424 at job +0xa0) and trims the passes with Edit Passes to
  Surfaces 0x141f ("MachiningPassesIn", "ProtectedSurfaces"; OverThickness etc. FUN_180070e8c mk l.86832). **Decoded and ported
  2026-11-13 (N13-10): section 8.3, viewer/cam3d_checktrim.c** (until then: refused).
- Ops 10 / 0x83 (Rough Pocket, Multisurface Pocket): no toolpath, mcmsurfres 249 "This operation cannot have check surfaces"
  (bnci_surface l.12645-12700) - **ported as that refusal.**
- The other classic ops (5, 7, 11, 12, 13, 15, 44) and Blend: the check model tmpmn4.tmp, projected with the check stock (FUN_180290fb0
  l.429259-429310, chk' = max(chk, -r)); the per-surface contact solvers are NOT DECODED -> **refused**. Blend also takes another path
  with checks (FUN_180268e10, blend.md 2) -> refused (blend_drive_from_tp).
- Gouge references (0x20, multiaxis; none in the corpus' 3D ops): refused. No drive left (all selected are checks): mcmsurfres 241
  (bnci_surface l.12272-12278, 0xf1).

### 8.2 Containment
- FUN_1800135a0 (l.13325-13337): `number_of_type_chains(type, chains)` with type = 2 for op 132 style 15 (Project; its role 3 chains are
  the curves), else **0xff = every chain of the op** (mccore number_of_type_chains, mccore_chain_range.c l.11165: CHAIN +0x4a == type or
  0xff); none -> FUN_18012b7c0 removes the files. FUN_18012f750 (l.200165) -> FUN_180128390 (l.196075) writes each chain's linearised
  points to tmptc.tmp, and with param_6 (every op but 7 / 0xd / 0x6d, l.200203-200209) **closes it with its first point** (l.196389-196416):
  an open chain is closed by the segment back to its start. Then the tool containment offset (op 132: 0x16c7 / 0x12b0 / 0x16c8; classic:
  0x12c8 / 0x12c0 / 0x12b8; l.196444-196478, offsetWriteTcb) - already ported (c3_tcb_offset, sr_containment). mckernel reads every
  tmptc.tmp chain as a "2D Closed Boundary" (FUN_180081f6c mk l.94336 -> FUN_1800ee404).
- Ported (c3_containment): the op's chains (its own list; for a stored op regenerated with no chains given, its TP kind 6 / 7 records,
  c3_tp_chains, as c3_mesh_op reads its kind 4 records), type 0xff / 2 as above, open chains included (closed); united even-odd as
  before (how the kernel's Boundaries plan combines several boundaries: NOT DECODED, ours). Replaces the old "closed chains only".
  Users: OptiRough, Waterline, Area Roughing, Raster, the grid styles (Spiral, Radial, Scallop, Project, Pencil), Horizontal Area (now
  also c3_tcb_offset's 1e-4 test and warning 295), classic Pocket 10 / 0x83 and Finish Contour 15 (sr_containment).
- No containment: HST -> the Bounding Box Plan 0x1425 (unchanged: OffsetDistance |0x13c0| + D/2 + 2 x 0x12a0 for the styles not in mask
  0x209, mk l.92849-92875); the no-chain Offset Boundaries Plan branch (FUN_18006fb44, mk l.92892-92918) is not ported (as before).
  Classic -> the drive's XY box (ours, as before: their use without tmptc.tmp is NOT DECODED).
- Parallel 5 / 11 (sr_parallel): ported - FUN_180133860 l.202508-202612 drops the lines outside the containment's extent across the lines
  (FUN_18013ebb0 l.208686: DAT_1804e8eb8 / ec0; none left -> one line at the middle); each line's crossings with tmptc.tmp:
  FUN_180267000 (l.403956) in mode 0 (FUN_180226810, called per line by FUN_1801fd1e0 l.336331-336360; sr_x_line): tol = 0.1 x
  curve_chord_dev; per chain the walk starts at its first point off the line; a point within tol of the line is a pending crossing (its
  along-coordinate); a change of side adds the pending value or the interpolated one (the new point's when the edge spans < tol across);
  values kept sorted (FUN_180229840); then pairs shorter than tol are dropped and runs of on-line points longer than tol are added as
  intervals (FUN_180229890: skipped when covered, covered pairs removed, inserted by midpoint, joined to neighbours closer than tol).
  A chain lying wholly on the line (LAB_1802673c6 reads past local_d8: NOT DECODED) refuses the op. A line with fewer than two values
  keeps no profile (FUN_180205770 -> FUN_1801fb100); no line with a crossing -> error -0x18; the profile trimmed to the pairs
  (FUN_180205770 l.339833: segments outside removed, a crossing segment cut there with Z interpolated; the gap ends the chain).
  "Follow the containment boundary" (record 0x12a6 = msurf_follow_tc_bdy, msurf_map l.340035 -> FUN_180226340): NOT DECODED, refused.
- Refused (NOT DECODED): Radial 12 with a containment (the rays FUN_18013ae60); Project 7 / 13 with role 2 chains
  (tmptc.tmp then holds the curves too, unclosed; its use NOT DECODED); Plunge 44 with any chain (use not traced); Blend with role 2
  chains; HST Project Curves with a containment and a tool containment offset (offsetWriteTcb's rewrite, hst_project_hybrid.md 1.3.2).
- HST Project: containment = role 2 chains, the curves / points = the other chains (chains picked here carry role 0 until S4).
- SS-8 answered (N13-10): method 1 decoded and ported, 8.3.
- S6 (2026-11-12) wrote it: section 6 "S6" status (whole bodies only; viewer/mcam_saveops.c sldtoolp_whole_bytes).

### 8.3 Edit Passes to Surfaces 0x141f: the check trimming of HST Waterline / Raster / Spiral / Radial / Project (decoded 2026-11-13, N13-10; viewer/cam3d_checktrim.c)
Sources: **ts l.N** = decomp/hst3d_trimsurf.c (mchmm.dll DumpRange 18034e000-18035cd70, ghidra_hmm, read-only), **tb2** = decomp/raster/tb2.c,
**d3** = decomp/scallop/d3.c (plan defaults), **mk** = decomp/mckernel_all.c, **ms** = decomp/mcmsurf_all.c. Classes (RTTI,
~/mastercam-re/work/hst3d/class_addr_map.txt): CTrimPassesToSurfacePlan (vftable 18078f4a8, bases CTrimPassesPlan / CTrimPassesBasePlan /
CEditedPassesPlan), EvaTrimPassesToSurfacePlan (vftable 18078f080, Evaluate [1] = FUN_18034ffd0); the same trimmer serves Edit Passes to
Toolholder 0x1459 (CTrimPassesToHolderPlan).

**mckernel's plan** (after the passes plan of each style; Waterline mk l.93590-93603, the "(check geometry)" variants mk l.93616-93626,
93722-93733, 93818-93830, 93916-93947, 94021-94050; Project mk l.223712-223716, after the Axial Offset depth cuts and the toolholder plan):
`ekPlan4a = CreatePlan(0x141f) <- "MachiningPassesIn", "ProtectedSurfaces" from ekPlan4b (the passes), ekPlanC2` (the check model: Check
Database Loader 0x1421 -> Detect Top Check Surfaces 0x1423 -> Check Surface Tessellation 0x1424, "Tolerance" = job +0xa0, FUN_18007ca68
mk l.92160), then `OutputID = TrimmedOutPasses (0x1cb2)` (FUN_1800ed9bc): the next plan reads the trimmed-out output.
Parameters FUN_180070e8c (mk l.86832-86889), only when tmpmmlu.tmp exists (the check model's header, FUN_1800f1700): DefragmentThickness 0,
**OverThickness = tmpmmlu +0x150, or 0 when +0x160 > 0**, TrimToSurfaceTolerance 1e-6 (factor 1), TrimToSurfExtension 0, TrimToSurfJoinGaps 0,
TrimToSurfFragmentAllowance 0. tmpmmlu +0x150 / +0x160 are written by FUN_180220fe0 (ms l.359080-359130): for op 0x84 with 0x16c4 != 0,
+0x150 = op +0x1240 = **record 0x12a8, the stock to leave on the checks** (FUN_1800202d0 ms l.23081 compares it with the Z stock op +0x1360 =
record 0x13c8 for styles 2 / 5 / 9 / 10 and warns mcmsurfres 0x97 when they are equal - not ported, its text not located); +0x160 > 0 only for
a negative check stock beyond the tool (ms l.359105-359115, partly decoded).

**The plan's defaults for 0x141f** (FUN_180303a20 d3 l.218-412; MachWeaveResolution decomp/scallop/d2.c l.470): EnableTrimToSurfaces =
`!IsNullKey(ProtectedSurfaces)`; TrimToSurfaceSimultanious (0x1ce7) false; TrimToSurfacePerZLevel (0x1ce8) =
`IsZProfilePassesPlan(PlanTypeNumber(MachiningPassesIn))`; SurfaceTrimKeepExterior (0x1c9d) false; TrimToSurfaceResolution (6999 = 0x1b57)
0.5; TrimToSurfaceTolerance 0.1 (mckernel: 1e-6); TrimToSurfJoinGaps 0.5 (mckernel: 0); TrimToSurfExtension max(1.0, -3 x OverThickness)
(mckernel: 0); TrimToSurfFragmentAllowance 25 (mckernel: 0); MachWeaveResolution 0.5. McHmm lengths are mm (inch values reach it x 25.4).

**The evaluator.** FUN_18034ffd0 -> FUN_18034f5a0 (ts l.816-1290): reads MachiningPassesIn / MachiningPasses / TrimmedOutPasses, takes every pass
through FUN_180352a90 (per-Z mode: the pass's Z level weave) and stores them through FUN_1802ed680 (decomp/pencil/d2.c l.172-690), which with
EnableTrimToSurfaces and not Simultanious calls **FUN_1803581a0(Eva + 2000)** (ts l.6566): a tbb parallel_for_each over the passes,
FUN_1803585e0 -> **FUN_180357ab0** (ts l.6310) per pass.
- The trimmer (Eva + 2000) is set up by FUN_1803503e0 (ts l.1431-2348) from FUN_1802eb170 (tb2 l.370-534): +0x48 = OverThickness
  (+ ToolThicknessZ when KeepExterior), +0x40 = +0x48 + DefragmentThickness, +0x58 TrimToSurfaceResolution, +0x60 DefragmentThickness,
  +0x68 TrimToSurfaceTolerance, +0x70 FragmentAllowance, +0x78 Extension, +0x80 JoinGaps, +0x3eb KeepExterior; **tool 1** (+0x268) =
  FUN_18013e330(the tool, ToolHolderThickness, OverThickness, 0) = radius R + t, corner Rc + t (hst3d_grid.c); the tool is FUN_18013ece0 /
  FUN_18013e200 of ToolShaftRadius, ToolCornerRadius, ToolTaperAngle and **ToolHolderDefinition** (set by mckernel's FUN_180079bfc under
  FUN_18006f480 = K+0x472 (record 0x16d7) with a holder and K+0x49e (record 0x165c) = 0, mk l.87618-87620); tool 2 (+0x328) the same with
  DefragmentThickness more. FUN_18034e860 (ts l.253-815) builds the check boxset (Eva +0x7c0, EvaBoxSet of the ProtectedSurfaces'
  Tessellations) and, with TrimToSurfacePerZLevel and not KeepExterior, collects the Z of every planar pass (FUN_1803530d0 ts l.3259).
- **The test** FUN_180351fe0 / FUN_180351ee0 (ts l.1897-1985): a CToolDropPos of tool 1 (FUN_180223650, hst3d_boxset2.c) at the position's
  XY from z - (+0x48); "in" when the drop rises above its start, i.e. the stock-inflated tool touches a check surface. Per-Z mode: before the
  drop, FUN_1802887a0 (MeasureInterior2D, the signed distance to the level's weave) - farther outside than the tolerance is "out" without
  the drop (an acceleration: the weave is grown by sqrt(2) x MachWeaveResolution, FUN_1803503e0 +0x50).
- **FUN_1803589c0** (ts l.6896): FUN_1803574d0 (ts l.6003) walks the pass piece by piece: the state at the piece start (a pass starting in
  opens an interval there), then per segment FUN_180353440 -> FUN_1803538c0 (ts l.3596): n = int(L / TrimToSurfaceResolution) (L the 3D
  length), at least 1, t = i / n, the test at each t, the (t_prev, t) pairs where it changes (per-Z mode: FUN_180352e30's weave crossing
  measure skips a segment with no crossing that starts outside); FUN_180353c70 (ts l.3752) refines each by **FUN_180356f50** (ts l.5793):
  bisection on the segment (Z linear) while the bracket is longer than TrimToSurfaceTolerance; the crossing = the bracket end that is
  **out**; crossings pair into in-intervals; on a **closed** pass that starts in, the first crossing ends the last interval (through the
  seam) instead (ts l.3929-3946, l.6230-6256). Then FUN_1803541c0 (defragment; with DefragmentThickness 0 the whole pass is the piece),
  FUN_180355410 (Extension 0: nothing), FUN_180356240 (touching intervals joined; JoinGaps > 0 also joins gaps: 0, not run).
- **FUN_180357ab0**: KeepExterior false -> the main output (MachiningPasses) = the in-intervals and **TrimmedOutPasses = their complement**,
  FUN_1803569d0 (ts l.5561): no interval -> the whole pass; open pass: [start, first) unless it starts in, the gaps, [last, end) unless it
  ends in; closed pass: the gaps and [last, first) across the seam, unless the interval covers the whole loop. The pieces are written in
  that order (FUN_180358d50 ts l.7050). mckernel reads TrimmedOutPasses: **the passes are cut where the tool (with the check stock) would
  touch a check surface.**

**Ported** (viewer/cam3d_checktrim.c c3_chk_open / c3_chk_trim; hooked into gen_waterline, gen_raster, hst_grid_open2 / hst_grid_pass /
hst_point_pass for Spiral, Radial, Project; c3_mesh_refs meshes the check references, and c3_mesh_op leaves them out of the drive when the
generator opened them): tool 1 = flat radius R - Rc, corner Rc + t (hd_build / hd_tip = CToolDropPos), the test hd_tip > z - t, the sampling,
bisection, interval and complement rules above, Waterline whole loops as closed passes. A one-position pass (Project Points) is kept when out.
**Refused** (NOT PORTED / NOT DECODED): a negative check stock (+0x160); the tool holder check together with checks (ToolHolderDefinition in
tool 1). **Ours**: the check tessellation (OCCT / mcs_mesh at the drive's deflection; Detect Top Check Surfaces not applied - every triangle
is dropped on); the per-Z weave acceleration is not built (every test is the drop); our passes are polylines (McHmm's frags may hold arcs);
the links between the pieces (the SAFE rule of each style's generator, as before).

**Validation** (tools/tests/checktrim_test.sh): `cam_cli chktrim-test` - analytic cases (a flat square check, ball tool with t = 0 / 0.5:
crossings at 10 - h / 20 + h within 2e-6 on the out side, a closed loop starting in, a pass wholly in, one-position passes, a pass t + 0.001
above). No DLL oracle: mchmm.dll is not loaded by tools/peloader (tbb, MFC, the HMM entity database). Corpus: the only stored op of these
styles with checks is TUTORIAL 2 SWARF_METRIC op 3 (Waterline, 43 checks; the X9 copy is the same op); its record says 0x172c = 0 (method 0
when its author generated it), so it is **not a method-1 reference** - but its stored NCI agrees with the method-1 port far better than with
method 0 (cutting moves, Corner rounding off as waterline_oracle.sh: ours->ref p95 0.315, cut length 9933 vs Mastercam 10239; checks merged
as drive (method 0): 13.36 / 11507; checks left out (S2): 1.27 / 10473). Raster / Spiral / Radial / Project with checks: no stored op;
scene s4_pick_check (Raster, the check = a copy of the drive) generates; scenes s4_check_trim / s4_check_trim_stock (Raster, BALL D10 over a flat loft, a check strip 30 above at x 40..60; check stock 0 / 1 through the test verb recpatch:12a8=d:1, the stock has no control in our dialog): the posted cuts stop at X 35 / 65 and 34 / 66 = R + check stock short of the strip (golden NC; asserted by checktrim_test.sh).
