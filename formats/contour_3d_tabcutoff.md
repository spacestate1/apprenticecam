# Contour: 3D contour, Tab Cutoff as a separate operation, tab edge cases (2026-11-08)

User request (via the contour pass): finish the missing core parts of the 2D Contour toolpath - the 3D contour / 3D chamfer on 3D
chains, Tab Cutoff as its own operation (opcode 0x82), and the refused tab edge cases. Everything below is from the decompiles
(citations inline); what could not be decoded stays refused. Prior work: formats/contour_100_plan.md (section 3 = the pass before).

Decompiles made for this pass (own copy of the mccore Ghidra project, ~/mastercam-re/work/c3d_a045/gh_core; DumpDecomp.java):
- ~/mastercam-re/work/c3d_a045/c3d.c: check_chains_3d / check_chains_3d_view, connect_3d_moves (mccore 0x18027cdb0),
  adjust_depths_3d (0x18089e460), adjust_depths, and their callees (pt_on_ent_from_t, t_from_pt_2d, proj_pt_on_ent, world_to_2d ...).
- ~/mastercam-re/work/c3d_a045/tc.c: init_operation (0x18004c6e0 / 0x18004c710), list_op_get_chains, tabs_from_chains,
  FUN_18027e290 (connect_3d_moves' record insert), CDependentOperations::DetectChildOperationIDs (0x1802e9a50).
- ~/mastercam-re/work/c3d_a045/lp.c: list_op_get_prev (0x1800be800), save_chain, store_ent_assoc.
- refs_aceb0.txt: the readers / writer of InitOperation's "previous op of the same type" switch (0x1812aceb0).

## 1. 3D contour (3D chains)

**Where Mastercam goes for a 3D chain.** bnci_ctour (decomp/mcmill_ctour.c l.351-421): check_chains_3d sets ctour_3d when any chain is
not flat in the tool view (check_chains_3d_view: lines / arcs by their end Z against the first entity's start Z, chain_plane_tol; an arc
whose view is not parallel; splines sampled 3..7 times, NURBS by their control points). With ctour_3d and incremental depth (op +0x140 =
record 0x1a8) a 2D type (0) becomes 3D (1). Then:
- type 5 (Remachining) -> FUN_1800550c0 (unchanged: refused);
- ctour_3d, incremental depth, compensation Off (op +0x568 = 4), no leads (op +0x888) and |wall stock| (op +0x278 = record 0x2e0) <= tol
  -> **FUN_18005ef90** (decomp/mcmill_all.c l.63754-64210): the narrow case, **ported**;
- every other case -> the contour loop FUN_18005b340 with ctour_3d, where FUN_180051990 calls **connect_3d_moves** after the 2D
  compensation (mcmill_all.c l.56159-56163) - only with incremental depth (absolute depth: the 2D contour of the projected chain, with
  ctour_3d acting in chain_to_ctour_array_break and the depth loop, l.62066-63607) - **not ported** (below).

**FUN_18005ef90 (ported 2026-11-08, cam2d.c gen_contour3d).** Per chain: SetActivePlane(tool view); the first entity's start (as chained,
reversed entities from their end) in view coordinates = z0; D = floor stock (op +0x820) + depth (op +0x138); set_depths(op, z0, ..)
(decomp/mccore_depths.c l.319-466: depth = z0 + depth + stock with incremental depth; top of stock = record 0x2e8, + z0 when 0x2f0;
start_top_of_stock = it); with depth cuts calc_depth_cuts (l.245-310) and the first pass lifted to D + finish cuts x finish step +
(rough cuts - 1) x rough step (rough cuts 0: (finish cuts - 1) x finish step). The entities are written straight (no offset, no tabs,
no leads, no multi passes, no chamfer, no taper - none is read there):
- the first record: the chain's start (gr_lin_mill flag 0x4001);
- line: gr_lin_mill to its end;
- arc: view_to_plane(arc view, tool view) - in a plane of the view (0 XY, 1 / 2 the two others): gr_arc_mill in that plane (XY-parallel
  arcs: our tagged arcs; XZ / YZ arcs: chords at the op tolerance here, ours - the post has no XZ / YZ arc output); not parallel:
  FUN_18005fee0 (l.64210-64290): n = round(|sweep| / sqrt(8 ctour_tol / r)) (ctour_tol = **ctour_prm = record 0x1228), n < 2 -> 1,
  points at equal arc length (creep_along_ent, from the end for a reversed entity);
- spline (classes 8 / 0x80): FUN_1800601f0 (l.64297-64360): from the chained start's parameter (ent +0x98, or +0xa0 reversed)
  brk_spline_const_err(curve_min_step, curve_max_step, ctour_tol) steps, a line to each point, until the far parameter within tol;
- every point's view Z + D (when |D| >= tol).
Then FUN_1800451f0 (decomp/mcmill_ctour.c l.9892-10085) copies the chain's records n - 1 times, each lower by the rough step for the
first (rough cuts - 1) copies, then the finish step (op +0x818) - the same levels as the 2D contour's calc_depth_cuts (cam2d.c
hst_depths / zlevels on the chain's own depth and top) - and FUN_180045770 links the copies. Heights: after the chain, the end's view Z
at the first level (uVar28; the test `n_depth_cuts <= 1 || closed` always holds there, n_depth_cuts being 1 inside the loop) raises
start_top_of_stock when above it by tol, and adjust_depths_3d (mccore 0x18089e460: *d -= step; adjust_depths(op, &z, step, step, 1, 0))
-> adjust_depths (0x18089e310: top_of_stock = z; incremental feed plane / retract / clearance = z + their values) sets the heights on
it. Between the copies the writer raises the reference depth to the last written point's Z when it is higher (l.6672, types 1 / 3)
and calls set_heights(ref, ref) (the feed plane never below ref: cam2d.c ht3), then retract_move; a closed chain's next copy starts
where the last ended (no retract, l.6162). Ours: depth cuts by depth with several chains and keep tool down on an open 3D chain are
refused (their 3D handling in FUN_180045770 not read); point chains are skipped. bnci_cutoff (Tab Cutoff op) takes the same branch:
an 0x82 op on a 3D chain in this case writes the whole 3D contour (as coded).

Data added: CamParams.dep_inc / top_inc / dep_cz (record 0x1a8 / 0x2f0 and the chain Z the incremental values were resolved with:
cam_fileop.c params_from_record / cam_common_apply / cam_params_from_patches, cui_opdlg.c hf_apply, cui_ops.c), so each chain takes its
own start Z. cam_generate lets tilted arcs (view normal not +-Z) and splines through for a contour in Top; gen_contour refuses them
unless the narrow 3D case takes them (the 2D contour does not read them). cam_chain_3d also flags a non-planar spline.

Scenes: rt_ctour3d_open (open 3D polyline, incremental depth -1, depth cuts 0.4: 8 copies, each retracted at the raised reference),
rt_ctour3d_closed (closed 3D chain, depth -2, step 1: 7 copies fed down at the start), rt_ctour3d_comp (computer compensation: refused
with the reason). Unverified against Mastercam: no stored op takes FUN_18005ef90 (the corpus' two 3D contours - TOOL AXIS CONTROL
-RELITIVE TO DIRECTION-1 LINE.mcx-9 ops 5 / 6 - use computer compensation and leads on the user tool planes 52 / 9 with Axis Control,
and stay refused for their tool planes).

**connect_3d_moves (decompiled, NOT PORTED).** connect_3d_moves(ctour_rec recs[], double *z_first, long *n, long cap, double
max_depth_variance (ctour_prm +0x18 = record 0x1240), double depth (op +0x138), double offset (the 2D compensation distance), char *ok)
(c3d.c): the offset path's records (0x60 bytes: +0 x, +8 y, +0x21 'L' line / 'A' arc, +0x28 the chain entity the record came from,
+0x30 its parameter on it, +0x38 the other entity at a corner, +0x40 z):
1. every record's z = depth + the view Z of its chain entity at its parameter (pt_on_ent_from_t);
2. a closed path (start / end within nc_tol, same z) whose ends are lines: the start is re-projected on the first and last chain entities
   (t_from_pt_2d in the entity's own view for an arc not in the tool view; a projection farther than 2 x offset falls back to the
   record's own parameter); both Zs within 2 x max depth variance -> record 1's z = their mean + depth; else the start moves to the
   projection on the first entity (proj_pt_ln on the start's line) and gets that z, or a record is inserted (FUN_18027e290: the array
   shifted up by one record, warning 0x149c when full);
3. every following pair: a corner made of an arc ('A' on either side, different entities): the Z of the next entity's start or end
   (by its +0xa8 flag) against this record's - beyond 2 x variance a record is inserted, a vertical 'L' move to that Z; two lines: the
   record's Z on its own entity and on the next record's entity at its x, y - within 2 x variance their mean, else the record is split
   (proj_pt_ln / proj_pt_on_ent on the entity end, the end parameter by its +0xa8 flag) and both get their own Z.
It works on the records that ccomp_pock_from_ctour makes from chain_to_ctour_array_break's records: each knows its chain entity and
parameter. The contour offset here is Clipper's (cam2d.c ct_offset: polylines), which keeps neither, and the depth loop's 3D branches
(FUN_18005b340 l.62960-63030 / 63560-63610: prev_gr_nci_pt's Z as the next pass's reference, adjust_depths_3d per pass) would have to
go with it - so the 3D contour with compensation / leads / wall stock / absolute depth stays refused, with that reason. Max. depth
variance (1209) stays greyed (used only here).

### 1.1 connect_3d_moves: what the records hold after the offset (read 2026-11-08, still NOT PORTED)

Decompiled for this: `pock_bound_to_ctour` (mccore 0x1805d6350) and `ctour_to_pock_bound` (0x1805d6020), in ~/mastercam-re/work/ct3full/pc.c. They copy pock_chn +0x60 / +0x68 / +0x70 / +0x78 / +0x80 to record +0x28 / +0x30 / +0x38 / +0x40 / +0x4c and back. What the Contour's `ccomp_pock` leaves in those fields (decomp/mcce_all.c):
- +0x28 (chain entity): carried. `FUN_1800db950` l.118749 and `FUN_1800dadb0` l.118452 / 118479 copy +0x60 onto the offset pieces. So the port's `src_ent` supplies it; inserted corners have none.
- +0x30 (parameter): not carried. The offset pieces get no +0x68; only `FUN_1800d9960` (l.117718-117724) copies +0x68 of the first entity of each bound from the source. **Every other record reaches connect_3d_moves with parameter 0**, so the parameter is not "derivable from the point" as formats/mcce_ctour_offset.md 6 suggested before: step 1 of connect_3d_moves really takes the entity's t = 0 Z, and the following pairs re-project with `t_from_pt_2d`.
- +0x38 (other entity at a corner): copied by the raw pieces (+0x70) but cleared on the result by `FUN_1800d9830` (l.11661-11740 call, `param_2` 0), so `get_ent_from_eptr` fails and the `+0xa8` flag branch takes its "no entity" side.
- Record 1 (the path's start) is not written from an entity: `pock_bound_to_ctour` keeps the +0x28 / +0x30 the input record 1 had.

Left to port: the input records (`chain_to_ctour_array_break`, 0x180283120, in pc.c), connect_3d_moves itself (c3d.c, about 600 lines of decompile), the writer's 3D branches (FUN_180056be0, decomp/mcmill_ctour.c l.7894, 8331, 8711, 10332: heights and arcs with a Z change), the depth loop's 3D branches (FUN_18005b340 l.1870-1916, 2458-2505: `prev_gr_nci_pt`, `adjust_depths_3d`), and the absolute-depth branch.

**Stored ops, corpus <mastercam-samples>/net + <user-parts> (1365 files read, 16 unreadable, 194 Contour-family ops): 5 ops whose chains are not flat in world Z.** `TOOL AXIS CONTROL -RELITIVE TO DIRECTION-1 LINE.mcx-9` ops 5, 6 (tool planes 52, 9) and `AEROSPACE.mcx-9` / `SWARF 5-AXIS_RAILS-AEROSPACE PART.mcx-9` op 1 (plane 53) are refused for their tool planes before the 3D code (whether their chains are 3D in those tool views was not determined); op 8 of the first file is not a 3D contour: its chain has constant X and its tool plane is view 5, so it is flat in the tool view and runs as a 2D contour. So **no stored op can check connect_3d_moves in the planes this program runs**: a port could only be checked against the decompile. Not started for that reason and its size; the refusal in `gen_contour` stays.

## 2. Tab Cutoff as a separate operation (opcode 0x82)

**Making it (FUN_180044170, decomp/mcmill_ctour.c l.3905-4307; called by bnci_ctour l.353 at every generation of a contour op).**
- CDependentOperations::DetectChildOperationIDs(op id) (tc.c): the ops of the file, in op-manager order, whose op +0xd0 (record 0x138)
  is this op's id (and their own children, AddIDs); every one of opcode 0x82 is deleted (TpPartOpList::DeleteListAndDB).
- With Tabs on (op +0x1318) and Tab Cutoff (op +0x138a = record 0x13f2) = 3: the current op is saved, init_operation(new, 1, 0, ok) =
  InitOperation(new, 1, 0, 1, -1, read defaults 0, ok) (decomp/mccore_ops.c): when the op list is not empty and the switch 0x1812aceb0
  is set - set_init_op_use_prev_op_same_type, from config item 17028 "Get defaults from previous operation?" (mccore_tooledit.c
  l.12021 / l.12110; the user's mcamxm.config: y) - list_op_get_prev(new, 0, 1, .., machine group) copies the machine group's op of
  opcode 1 with the highest op id (lp.c: it keeps the op whose id is greater), clears op +6 .. +0x7d (the comment), then the common
  InitOperation tail (new id, group, planes, tool; the group's modal heights override l.2086-2122); without it the defaults file's op.
  Then: op +0x170 (the NC name, 0x104 bytes) from the parent, opcode +4 = 0x82, op +0xd0 = the parent's id, the parent's tool block
  (op +0x2e0, 0x288 bytes) when the tool numbers differ, op +0x1318 = 1. store_ent_assoc stores it (TpPartOpList::AddListAndInit);
  the parent's chains (list_op_get_chains) are saved for it with the parent's tabs (tp_get_chains + tabs_from_chains -> save_chain with
  the tab list); its id goes to the op tree's IDArray (+0x3660), and ctour_toolpaths (l.558-571, after bnci_section of the parent) generates that op right after the
  parent (bnci_section).
- **Generating it: bnci_cutoff** (decomp/mcmill_all.c l.47562-47740; mccore's dispatch FUN_180105130 sends opcode 0x82 to its case
  0x2c - the byte table at mccore 0x180106e0c holds 44 for 0x82, 0 = bnci_ctour for 1 / 0x33-0x35 / 0x85): bnci_ctour without
  FUN_180044170 and Remachining, with set_cutoff_prm (ctour_prm = op +0x11c0). The same make_geo_tabs / add_auto_tabs, 3D test, and
  FUN_18005ef90 / FUN_18005b340. In FUN_18005b340 the pass writer FUN_180056be0 is skipped for opcode 0x82 (l.1753: only the markers
  are written) and after each chain the cutoff writer runs (l.1955-1957: `tabs && (opcode == 0x82 || (mode == 2 && local_125d))`) on
  the chain's last pass - without the retract_move that mode 2 puts before it (l.1963) and without "cut tabs with first finish pass"
  (local_125d is mode 2's). The parent (mode 3) has no cutoff of its own (neither branch) and cuts its tabs.

**Ported (cam2d.c gen_contour, cut_tab_cutoff; cam_fileop.c; mcam_saveops.c; core_ui cui_ops.c cutoff_children, cui_dirty.c):**
- the parent with mode 3 generates (it was refused) - its tabs cut as before;
- cutoff_children (after the op dialog's OK, a regeneration, a dirty op's regeneration): the parent's 0x82 children are deleted (ours
  and stored ones: record 0x138 = the parent's stored id), and with Tabs on and mode 3 a new child (CamParams.ct_cutoff_op), generated at
  once - a new op each time, as FUN_180044170 makes one (2026-11-08 "follow Mastercam" pass, below);
- generation (gen_contour with ct_cutoff_op): the passes computed but not written, the cutoff after each chain (mode 2's writer, no
  retract before it); the op's first move a rapid at the clearance height over the first span (ours, as every op here starts);
- saving: opcode 0x82 on a copy of the defaults library's contour op (none of 0x82 there), record 0x138 = the parent's new op id
  (the parent is written first when it stands before it); the project keeps the link (sidecar keys ct_cutoff_op / ct_uid /
  ct_parent_uid, written only when set);
- reopening (.mcx-9): opcode 0x82 is a contour (cam_fileop_type) with ct_cutoff_op and the stored parent's id (record 0x138); the tree
  names it "Tab Cutoff" (mcam_opcode_name 130, optree_text.inc). Regenerating the stored child alone gives the same moves; regenerating
  the parent replaces it.
- Scenes: rt_tabcut_sep (the parent + its Tab Cutoff op, posted), rt_tabcut_sep_rt (export .mcx-9, reopen, regenerate the stored child
  then the parent), rt_tabcut_sep_pj (project save / reopen / regenerate). Unverified against Mastercam (no stored op has opcode 0x82).

**Follow Mastercam (user, 2026-11-08: "tab cutoff follow mastercam yes") - the four deviations the port left:**

1. **The new op's parameters (DONE).** FUN_180044170 calls init_operation(new, 1, 0, ok) = InitOperation(new, 1, 0, 1, -1, 0, ok)
   (~/mastercam-re/work/c3d_a045/tc.c). redo_op_idn is 0 there: InitOperation is its only writer (it resets it, mccore_ops.c l.1744;
   the CallersOf refs in mccore are InitOperation / init_operation_db and the export entry; only mccore.dll and mcmill.dll name it, mcmill
   only reads it in operation_manager), so l.330-336 run: list_op_get_prev(copy, 0, 1, ok, active machine id, -1), and the copy is kept
   only when the switch DAT_1812aceb0 is set (`if (DAT_1812aceb0 == 0) local_d7c7 = 0`).
   - The switch = config item 17028 "Get defaults from previous operation?": ReadCfgFile's item setter case 0x4284 (= 17028) ->
     set_init_op_use_prev_op_same_type (decomp/mccore_tooledit.c l.12110; the Configuration page's check, l.12021); not in the file:
     the image's static value 1 (mccore .data 0x1812aceb0 = 01, MemDump). The user's: `17028. Get defaults from previous operation? y`
     (<windows>/Users/<user>/Documents/my mcam2017/CONFIG/mcamxm.config and mcamx.config l.610).
   - y: list_op_get_prev (lp.c) keeps, of the op list, the op whose opcode is exactly 1 (`param_3 == op +4`), in the active machine's
     group, with the greatest op id (op +0). InitOperation's milling branch copies it whole (l.553-1390), clears the comment (op +6,
     0x78 bytes), then the tail: new id (OMgetNextOpId l.272 -> op +0 l.1698), opcode, op +0x7e = 0 (l.1700), the active planes
     (InitOperationViewsToActivePlanes l.1773). So the source is the NEWEST contour op - the parent itself only when no contour op was
     made after it.
   - n (local_d7c7 = 0): LAB_1800479d4 (l.1397-1416) - for opcode 1 both product-bit branches end in default_ops_get(1, op, group):
     the machine group's defaults file's contour op; then list_op_get_prev(any opcode) only for the NC name op +0x170 (l.1416-1430),
     which FUN_180044170 overwrites from the parent anyway; the same tail.
   - Then FUN_180044170: op +0x170 from the parent, opcode 0x82, op +0xd0 = the parent's id, the parent's tool block op +0x2e0 .. +0x568
     only when the tool numbers (op +0x2e0) differ, op +0x1318 = 1, the parent's chains with the parent's tab list.
   - Ours (cui_ops.c cutoff_source / newest_contour_op / cutoff_tool_block): 17028 read from the user's config (mcam_config_value;
     MCAMUI_TEST_CFG17028 overrides it in tests); y = the newest op of opcode 1 (mcam_opcode_for; stored ops by their stored id, ours
     taking one over by that id, ops made here newer than every stored one in the order made - CamParams.op_seq, assigned at gen_commit /
     Paste / the Tab Cutoff op; 0 after a project reopen = tree order, as the .mcx-9 save numbers them; a dirty op with an edit = the
     edit), its CamParams copied (a stored op through cam_op_from_file; unreadable: no child, a message); n = cam_defaults +
     heights_from_default_op for CAM_CONTOUR at the parent's chain Z (the op a new Contour starts from here); then the active planes
     (planes_set_active), the tool fields of the block (tool number / slot / shape, feed, plunge, retract, rpm, coolant, D offset and
     the record patches in 0x348 .. 0x5d0) from the parent when the tool numbers differ, the parent's tab list (tab_xy / tab_ed /
     tab_own), Tabs on, the comment empty. NOT PORTED (decoded, no field here): init_misc_values (l.1721), the tool change position,
     the machine group's "override with modal values" heights (l.2086-2122), the NC name (written at save from the tree, nci_write.c).
2. **Its place in the operation list (DONE).** store_ent_assoc -> TpPartOpList::AddListAndInit (decomp/align/core_oplist.c l.2414):
   op +0x7e (0 from InitOperation l.1700; FUN_180044170 does not set it) < 1 -> appended to the op list (vtable +0x10), op +0x7e = the
   count. Then OMrefreshOperation (unless OMrefreshDisabled) -> COpMgrTree::RefreshOperation (decomp/opman_tree_all.c l.39960) ->
   AddOperation (l.25883): with the insert arrow in the op's toolpath group (OMinsertInfo +0x37b4) at the arrow (after the op above it,
   +0x37b8, SelectOperation of the new op, SortOpListToMatchTree), else last in its group. (The IDArray at tree +0x3660 that
   FUN_180044170 fills is the "regenerate next" list: ctour_toolpaths l.558-571 and COpMgrTree::RegenSelected run bnci_section /
   bnci_regenerate on it - not a position.) This tree has no insert arrow; it stands after the last item (COpMgrTree::LoadTree), where
   this program puts every new op: the child goes to the END of the list, a new op each regeneration (it was: right after the parent,
   reused). NOT DECODED: whether AddOperation's selection of the new op happens during a regeneration (OMrefreshDisabled) - the
   selection is left as it is.
3. **Its parameter dialog (DONE).** MakeSpecificPages FUN_18026d480 (decomp/mcmill_all.c l.424817): opcode 1 and 0x82 get the same slot
   list except Tabs 0x19 / Tab Cutoff 0x1e (l.424900: only `!= 0x82`); the factory FUN_18026c800 returns no page for 0x82 in cases 0x19 /
   0x1e (l.424622 / l.424654), builds HSTCutoffPage (FUN_18015e100, mcmillres 3027) for the Toolpath Type page (case 1) and
   HSTCutoffCutPage (FUN_18015d790, mcmillres 3025 "Cutoff Cut parameters", case 5 l.424431) for Cut Parameters. HSTCutoffCutPage
   (vftable 0x18033bd78, VtableDump): MapToDlg FUN_18015dab0 / UpdateLocalOp FUN_18015db80 read / write the contour page's fields
   (record 0x5d0 compensation type, 0x5d2 direction, 0x5d3 roll, 0x1238 infinite look ahead) + HST2DCutParametersPageBase (tip comp
   0x1231, stock to leave 0x2e0 / 0x888); the combo fills FUN_18015dd60 (mcmillres 1261 / 1263, 1272 / 1276 / 1277); the bitmap
   FUN_18015e000 (0xbc Off / 0xb2 Left / 0xb5 Right); none of its functions enables or disables a control. Title: FUN_18026d220 case 0x82
   = mccoreres 0x113f (4415) "Tab Cutoff" -> "2D Toolpaths - Tab Cutoff". Ours (cui_opdlg.c pg_has / pf_dlg / pp_title; formats/
   opdlg_fields.tsv dialog 3025 -> cui_opfields.inc): the tree without Tabs / Tab Cutoff, the Cut page from template 3025 with the
   2938 controls' handlers (the greyed states are this program's engine limits, as on 2938), the Type page greyed as every Type page.
   (FUN_18014c4a0, the l.232924 "page flags", is another ToolpathTree builder: there too 0x82 differs from 1 only by the Tabs / Tab Cutoff
   items, local_c7.)
4. **Paste, Edit Common, Tool Manager (DONE - decoded: none of them makes or refreshes the 0x82 op).** The 0x82 op is made only by
   FUN_180044170, i.e. by bnci_ctour, i.e. by a generation of the contour op.
   - Paste: COpMgrTree::PasteOperations -> COperationsArray::Paste (decomp/opman_opsarray.c l.3118) -> AddListAndInit (cut buffer, the
     same ids) / list_op_copy (copy buffer, decomp/align/core_oplist.c l.3: OMgetNextOpId l.612; no write to op +0xd0 anywhere in it) and
     ValidatePastedOps (mcmachinedef.dll 0x1800fb900, decompiled here: machine compatibility only) - no bnci_* call. So a pasted copy of
     the parent has no 0x82 op until it is regenerated (then its own); a pasted copy of a 0x82 op keeps op +0xd0 = the original parent,
     whose next regeneration deletes it with its own child (DetectChildOperationIDs). Ours: op_paste gives a copy a new op_seq and an
     empty link (ct_uid 0); a 0x82 copy keeps ct_parent_uid; no cutoff_children (as before).
   - Edit Common Parameters (FUN_180145b50, formats/opmanager_menu.md): bnci_update (mccore 0x180139200, decomp/dirty/core_bnci_update.c
     -> BnciUpdate only) patches the op's own NCI, else op +0x94 (dirty); no regeneration. BnciUpdate::DontAllow's dependents test
     (CDependentOperations::Detect(id, 2, 2) -> DetectWithOpcodes: dependency type 2 keeps only subprogram ops) does not count the 0x82
     op. Ours: unchanged (the dirty case -> the regeneration path; the patch case is this program's immediate regeneration stand-in,
     without cutoff_children, as the patch makes no op).
   - Tool Manager / Define Tool (AssemblyChangedProcessor FUN_180a449c0 -> FUN_180a45440, decomp/dirty/core_tooldirty.c): the ops of the
     tool are marked dirty or patched (BnciUpdate), not regenerated - the 0x82 op too when it uses the tool (it has the parent's tool).
     Regenerate then runs bnci_ctour on the parent: a new 0x82 op. Ours: regen_ops_for_tool marks, cui_dirty.c's regeneration runs
     cutoff_children (unchanged); Renumber tools likewise patches (ours: unchanged).
   Scenes (2026-11-08): rt_tabcut_sep_dlg (the child's dialog: title, tree, Cut page 3025), rt_tabcut_sep_newest (contour A with a
   separate Tab Cutoff, contour B with stock 0.5 on walls, A regenerated: the child takes B's parameters and goes last),
   rt_tabcut_sep_cfgn (the same with MCAMUI_TEST_CFG17028=n: the defaults file's contour op), rt_tabcut_sep_paste (a copy of the parent:
   no child; regenerated: its own child, last), rt_tabcut_sep_pastekid (a copy of the child; the parent regenerated: both deleted, one
   new), rt_tabcut_sep_ecp (Edit Common feed on the parent: the child stays), rt_tabcut_sep_tool (an edited tool: both dirty;
   Regenerate dirty: the parent, then a new child).

## 3. Tab edge cases (still refused; what was read)

- **A Full tab over the path's start.** The pass writer's record-1 branch (decomp/mcmill_ctour.c l.9786-9817, ptStack_12b30 = the tab
  at the path's start, its +0x30 t <= tol): a Partial tab writes record 1 at the tab's top (ctour_depth + break through + thickness,
  not below the pass) - ported 2026-11-04; a Full tab (+0x48) writes **no record 1** and sets the raised height dStack_12af0 =
  init_depth, so the pass's first written record is the tab end's move (the rapid at the raised height to the end point, or
  FUN_18005ab70's lead). The depth copies' writer FUN_180045770 approaches a pass from its first record read back (l.6234-6470: the
  record types 1 / 0xb / 2-3 - lines / arcs - with the 0x4000 / 0x8000 flags decide the approach, plunge_move, Plunge after first
  move); its handling of a pass whose first record is that rapid (record type 0, no 0x4001 flag) is not read - refused as before.
- **Ramp tabs whose carried height leaves the move's line at an entity end.** The writer walks the ctour records (one per offset line /
  arc) and carries the height across a record end (contour_100_plan.md 2 "Ramp tab moves"); the offset here is a Clipper polyline whose
  vertices are not the records' ends (rolled corners are chords), so where the carried height differs from the straight move's the
  record ends are needed - the same missing piece as connect_3d_moves (section 1: the offset records with their chain entity /
  parameter). Refused as before.
- **Square-point tabs on splines.** FUN_1802256c0's spline branch needs length_ent and proj_pt_on_ent / t_from_pt_2d on splines (now
  decompiled as connect_3d_moves' callees, ~/mastercam-re/work/c3d_a045/c3d.c); but a 2D contour does not take spline chains here at all
  (gen_contour: "this operation type does not take spline curves"; only the 3D narrow case does, and FUN_18005ef90 cuts no tabs), so
  the case cannot be reached until the 2D contour takes splines. Left refused.
- **Tab Cutoff with Control compensation.** Read: with Control (bVar22 = cc_in_control and D > tol) FUN_18005b340 copies the chain's
  own ctour records (local_1240, chain_to_ctour_array) over the cutoff path's first records (l.1970-1995, local_100[1..n] = local_1240
  [1..n]; the path's count DAT_1804f1708 kept), and the pass writer gives records cc codes by record number (l.8164-8177: record 2
  cc_control_gcode, the last record 140 unless param_11, record 3 too in the cutoff with Wear / Reverse wear and the entry lead) -
  a written span piece carries its record's code. Not ported: the cutoff here works on the offset polyline, whose segments are not
  the records (which record a span piece belongs to is not kept), so the G41 / G42 / G40 would land by guess; refused (Control and Wear
  / Reverse wear alike).
