# Geometry delete / transforms / NC overwrite: Mastercam 2017 behaviour (implementation spec, 2026-10-26)

Scope: (1) deleting geometry that an operation (or a plane) uses, (2) Xform Move / Copy / Join and how ops follow,
(3) other edits that rewrite geometry in place, (4) N1, the NC / NCI "file exists" flow when posting.
Rule: every behaviour cites its source. Anything without a source is marked **NOT DECODED**.

Decompiles made for this spec (read-only Ghidra; gitignored) are in
`<repo>/decomp/align/`, shortened below as `align/`:
- `geo_core_a.c`: mccore delete_ent (7 args), store_ent, xform_swap_idn, CXfAssocInfo ctor / End / Load, CAssocUtils::GetToolpathEntitiesByGeometry.
- `geo_core_b.c`: message_box_yes_no x2, FUN_18001d7f0 (MB flags), FUN_1800341c0, tp_change_eptr, delete_assoc_tp, delete_ent_assoc, idnSwap, the undo helpers FUN_1802e1cf0 / FUN_1802e2430.
- `geo_core_c.c`: FUN_180038480 (re-points TP records and marks ops dirty), validate_toolpath, Okay2DeleteEnt, FUN_180293ff0, the undo / redo FUN_1802df8f0 / FUN_1802dfa50, FUN_1808df760, FUN_1808dfb00.
- `geo_core_d.c`: delete_all_by_mask, FUN_180293220, FUN_1800425a0, mark_op_dependents, FUN_1801d7cc0 (config page).
- `geo_core_e.c` / `geo_core_g.c`: the Delete dialog DelRefEnt (FUN_1802958d0, ctor FUN_180292460), FUN_180295530, FUN_180295800.
- `geo_cadutil_delete.c`: mccadutil delete_entities, delete_all_nonassoc, EntIsAssoc, FUN_180065b70.
- `geo_sld_mcop.c`: mcgeomsld mc_op_* (solid-history ops).
- `geo_callers_core*.txt`: callers of the functions above.
- `geo_run*.log`: the runs.

Earlier dumps used: `decomp/xform/mccore_xform.c` (xform_store_ent, FUN_1808d8820, XfCreatePreview, XfDoFinalAction,
replace_ent, tp_update_assoc_ptr_single), `decomp/dirty/core_tooldirty.c` (list_op_check_ent),
`decomp/dirty/core_regen.c` (CAssocUtils::GetOperationsByGeometry), `decomp/n1/` (CPostDlg, FUN_1800a1800),
`decomp/mcmill_all.c` (OpMgrPostX, the batch post driver), `decomp/mastercam_filedlg.c` (PromptForFileToSave),
`decomp/mccad_cmds/*.c`, `~/mastercam-re/work/mp/prm/mccontrol_all.c` (CCNCControl).

Terms used below:
- **op +0x94** is "dirty", the X in the tree (opdlg_port_audit.md 19.1). The op record on disk = the `ent` + 0x68, so
  op +X is ent +0x68+X. In each line cited, the local offsets were checked against that base.
- **TP record** = an associative DB entity of type 0x4000, sub-type 2, that links one geometry entity (by eptr and by
  entity id) to an op id (record +0x68). These are the "TP_ID references" the app already reads (`F.tp[k].ent / op_id`,
  cui_sel.c `ent_users`). On a geometry entity, **ent +0x38** counts the TP references and **ent +0x54** counts the plane
  references. ent_is_assoc = (+0x38 > 0 || +0x54 > 0) (decomp/xform/mccore_xform.c l.6178-6189).

---------------------------------------------------------------------------------------------------------------------
## 1. Deleting geometry that an op or a plane uses

### 1.1 Home > Delete entities (Delete key): mccadutil `delete_entities` -> dialog 6561, not message 4421
Call chain (align/geo_cadutil_delete.c):
1. `delete_entities` (l.112-152): `select_ents(.., 0x1f1fff, ..)` (the preselection is taken).
2. If entities were picked: `FUN_180065b70`. Undo: UndoDeleteTransStart, then UndoTransCommit if something was picked,
   else UndoTransTerminate. The command is one undo step.
3. `FUN_180065b70` (l.339-357): `perform_tp_cleanup = 0`, then `delete_all_by_mask(4 /*selected*/, 0x1f1fff, 0, 1 /*ask*/)`.
   If `perform_tp_cleanup` was set during the delete, it calls `validate_toolpath` (see 1.3).
   Then DfRedrawDraft and UpdateLevelsManager.

`delete_all_by_mask` (align/geo_core_d.c l.518-715):
- **Pass 1** (l.600-670) looks at every selected entity. It counts one as associated when:
  - it is not a TP record, Xform-Move is not running (`xform_move_assoc_ent == 0`), and `ent_is_assoc`
    (it is used by an op or a plane); or
  - a solid depends on it (`is_solid_dependent_on_curve`); or
  - a relief depends on it (`IsReliefDependentOnCurve`).

  For the first such entity, unless the warning is suppressed (next item), it shows **dialog 6561** (`FUN_1802958d0`, l.655).
- **Suppression**: no dialog when `(GetProdEnableBits() & 6) == 0 && power_user` (l.651-653).
  - power_user is the Configuration checkbox **"Suppress associativity warning messages"**: mccoreres dialog 319,
    control 2651 (pe_dialogs), help page `cadfields/MCCore/Config_fields/Suppress_associativity_warning_messages.htm`.
  - FUN_1801d7cc0 (align/geo_core_d.c l.150) reads and writes power_user with control id 0xa5b (2651).
  - The meaning of the product bits 6 is **NOT DECODED**.
  - When suppressed, everything is deleted as with "Delete all selected entities".

**Dialog 6561** (class DelRefEnt, mccoreres; template from tools/pe_dialogs.py; ctor FUN_180292460 in align/geo_core_g.c):
- Title: "Deleting a referenced entity!"
- Text: "Some of the geometry selected is associated to a Toolpath, Solid, or a Plane.  Deleting this geometry will
  permanently alter those operations." / "Would you like to"
- Push buttons:
  - 3690 "Delete &all selected entities"
  - 3688 "Delete &only non-associative entities"
  - 2 "&Cancel"
  - 9 "&Help"
- The message map at mccore 0x180d35650 (parsed from the file) sends 3690 to 0x180292df0 and 3688 to 0x180292e30.
  - Both jump to the same import, assumed to be CDialog::OnOK. **NOT DECODED**: the import at 0x180cee7c8 was not resolved by name.
  - 3688 first sets `this+0x1550 = 1`. The ctor sets +0x1550 = 0.
- DoModal == IDOK copies +0x1550 to the caller's "skip associated" flag (FUN_1802958d0, align/geo_core_e.c l.197-250).
- There is no default push button in the template data that was read; the DS_/BS_DEFPUSHBUTTON style was not dumped (**NOT DECODED**).

What each answer does (delete_all_by_mask l.655-715, FUN_180293220 align/geo_core_d.c l.175-517):

| answer | result |
|---|---|
| Delete all selected entities | `FUN_180293220(.., skipAssoc = 0, ..)` deletes every selected entity. Each delete is `delete_ent(eptr, 1, 0, 0, &flag /* = 0 */, 0, true)` (l.355; disassembly 0x1802935ef-0x180293610). The display-list path uses `FUN_180293ff0` with the same arguments (0x18029358b-0x1802935b1). With param_4 = 0, `delete_ent` sets `perform_tp_cleanup = 1` for associated entities (align/geo_core_a.c l.380-386), so the ops are fixed up by validate_toolpath afterwards (1.3). |
| Delete only non-associative entities | `FUN_180293220(.., skipAssoc = 1, ..)` skips entities that are ent_is_assoc / relief-dependent, solids with flag 4, and curves a solid depends on (l.330-346). The others are deleted. |
| Cancel | nothing is deleted. `set_clr_sel_bits(0, 4, 0, 4, mask, 1, 0)` runs (l.699); its argument meaning is **NOT DECODED** (it reads as "selection bit 4 off"). |

- **Entities with a non-zero `ent +0x28`.** These are EntIsAssoc code 1. The meaning of +0x28 is **NOT DECODED**: Xform moves them along with FUN_18028ea60 (mccore_xform.c l.1431).
  - FUN_180293220 never deletes them in the main pass. It counts them (l.322-329).
  - Afterwards (delete_all_by_mask l.681-693):
    - If dialog 6561 was shown and "Delete all" was chosen, `FUN_180295530` deletes them too.
    - If the dialog was not shown, it asks mccoreres **7359 "Delete associated entities?"** with caption **5073 "Mastercam"** (Yes / No, question icon).
      - Yes: FUN_180295530 deletes them.
      - No: FUN_180295800 writes "<n>" + mccoreres **4464 " associated entities not deleted"** to the prompt area and waits for a key (align/geo_core_e.c l.159-195).
- Planes: FUN_180293220 collects the ids of the deleted entities and calls `MastercamPlanes::GetPlanesByGeometryIDs` then
  `ValidatePlanes` (l.409-432). delete_assoc_tp calls `SwapEntityIDs(id, 0)` for entities with ent +0x54 > 0
  (align/geo_core_b.c l.552-557). The plane side after that is **NOT DECODED**.

### 1.2 mccoreres 4421 is a different prompt, used by other commands
- `delete_ent` (DbDel.cpp; align/geo_core_a.c l.19-417; `FUN_180293ff0` is the display-list twin, align/geo_core_e.c l.549-562):
  - Applies when the entity is not a TP record, `xform_move_assoc_ent == 0` and `ent_is_assoc`.
  - Only if the caller passed `param_5 != NULL && *param_5 != 0`: it sets `*param_5 = 0`, so the prompt is asked once per caller loop.
  - Unless suppressed (`(prod & 6) == 0 && power_user`), it shows `message_box_yes_no(4421, 4422, 2)` (l.387-394).
- **The message box:**
  - text mccoreres **4421 "Geometry is used in a toolpath or plane.  Are you sure?"**
  - caption **4422 "Deleting a referenced entity!"**
  - The char overload message_box_yes_no(text, caption, 2) calls MCMessageBox(text, MB_YESNO | MB_ICONQUESTION (4 | 0x20, FUN_18001d7f0 with style 2), caption). It returns true only on IDYES (align/geo_core_b.c l.3-25, 92-150).
  - No MB_DEFBUTTON flag, so the Windows default button is **Yes**.
  - When the message log destination is "file only" (2) or 0, it returns No without showing anything.
- **Yes:**
  - param_4 == 0: `perform_tp_cleanup = 1` (deferred clean-up);
  - param_4 != 0: `delete_assoc_tp(ent, -1)` at once. This deletes the TP records that point at the entity (DeleteToolpathEntities) and unhooks planes (align/geo_core_b.c l.512-596).

  Then the entity is deleted (undo record, `mc_op_delete_entity` for solid-history ops, l.290-296).
- **No:** `delete_ent` returns 0 and the entity stays.
- Example caller that passes the flag as 1: mccad FUN_1801da5f0 (create points at arc centres with "delete arcs";
  `local_29d7[0] = 1` then `delete_ent(.., 1, 0, 1, local_29d7, 0)`, decomp/mccad_points_extra.c l.167-196).
  The Delete command passes 0 (1.1).

### 1.3 What happens to the ops: `validate_toolpath` (align/geo_core_c.c l.310-1065)
Runs after the delete when `perform_tp_cleanup` is set (FUN_180065b70; also after Undo / Redo FUN_1802df8f0 / FUN_1802dfa50, l.1465-1615).
1. Recount: every op's +0x88 and +0xd4 are reset, then counted again over all TP records. +0x88 counts the TP
   records; +0xd4 counts the same except opcode 0x1c records with sub != 1 (l.455-466, 500-515). op +0x95 is reset.
2. **Stale TP records.** A TP record is stale when its geometry cannot be read or is deleted (sel bit 1 off) (l.504-560).
   For each stale record:
   - op +0x95 = 1 and the TP record is killed (`sel_bit_turn_off(rec, 1)` + `write_ent_sel`) (l.736-742);
   - the same +0x95 mark goes to the ops found by `CDependentOperations::Detect(op, 1, 1)`, `list_ops_ref_lcanned` and
     `list_ops_ref_quick_finish` (l.743-785).

   Control records (sub-types 2/3, 6/0x29, 0x16, 0x17) are dropped when they no longer have members (l.786-905).
3. The counts op +0x88, +0x8c and +0xd4 are decreased by the killed records (l.925-975) and written to the op record.
   **Every op with +0x95 gets op +0x94 = 1 (dirty) and op +0x97 = 0** (l.985-1003).
4. `DeleteListAndDB` (the op is deleted) is reached only when `FUN_1800425a0(op)` is true. Its disassembly
   (0x1800425a0-0x1800425df) ends in `xor al, al`: it always returns 0. **An op is never deleted, even with no geometry left.**
5. `perform_tp_cleanup = 0`.

So, on "Delete all selected entities":
- the entity is gone;
- the op's chain loses the entries that pointed at it (their TP records are killed);
- the op and the ops that depend on it get the X (dirty) and keep their old toolpath / NCI until they are regenerated.

**NOT DECODED:** what Regenerate does with a chain that lost entries, or with an op that has no geometry left
(the CAM engines; nothing in this trace shows an error message).

### 1.4 Spec for the app (cui_sel.c `delete_selected_ents`, today l.119-125 refuses with an invented message)
- If any selected entity is used by an op chain / points (ours: `ent_users`; stored ops: their TP references) or by a
  plane, and "Suppress associativity warning messages" is off (ours: off; the user's registry value is not read
  here, **NOT DECODED**), show dialog 6561 with its exact text and buttons:
  - "Delete all selected entities": delete everything. For each op that used a deleted entity: remove those chain
    entries / points (ours: the CamOp chain entries; stored ops: their TP references), mark the op dirty
    (cui_dirty, as an op edit), and mark its dependents dirty. Do not delete the op. Undo restores the entities and
    the chain entries. Mastercam's undo re-runs FUN_180038480 + validate_toolpath, which marks the restored ops dirty again.
  - "Delete only non-associative entities": delete only the entities no op / plane uses.
  - Cancel: delete nothing.
- Message 4421 is not the Delete command's prompt. Use it only where a command deletes geometry as a side effect (1.2).

---------------------------------------------------------------------------------------------------------------------
## 2. Transforms: Move vs Copy vs Join

### 2.1 Which mode moves the originals
- XfDoFinalAction (decomp/xform/mccore_xform.c l.1859-1931): MoveCopyJoin 0 (Move) becomes **99**. With a valid preview,
  `FUN_1808dff20` removes the preview and calls `XfCreatePreview(type, 99, ..)` (l.6968-7060).
- XfCreatePreview (l.70-160):
  - 99: `cVar1 = 1`, so `uVar6 = 1` = FUN_1808d8820 param_8, the **move flag** (DAT_18143d6a8, l.2514).
  - Copy (1) and Join (2): param_8 = 0. Join also sets the "join lines" byte (uVar5).
- Commands built on this machinery: Translate, Translate to plane, Rotate, Mirror, Scale, Rectangular array, Stretch
  (decomp/xform/*, formats/wireframe.md "Transform").
  - **NOT DECODED:** Move to Origin's move flag (translate_silent calls FUN_1808d8820 with arguments Ghidra lost,
    decomp/xform/mccore_translate_silent.c l.257).
  - **NOT DECODED:** Roll.

### 2.2 Move (move flag, first copy): the entity keeps its id; the ops stay attached and become dirty
- Before the loop: `CXfAssocInfo::Load(0x1f)` (mccore_xform.c l.2543; align/geo_core_a.c l.1004-1073). It snapshots:
  - every eptr / entity id that a TP record references (bit 1, FUN_1800341c0, align/geo_core_b.c l.151-213);
  - plane geometry ids (2), group geometry (4), C-hooks (8), and set 0x10.
- Per entity, `xform_store_ent` (l.896-1457; `cVar5 = move && copy 1`, l.989):
  1. The entity is transformed in its `ent` struct.
  2. `FUN_1808d1df0` writes it with `store_ent` as a **new DB record** (new eptr). Then `xform_swap_idn(old eptr, ent)`
     swaps the ids (idnSwap), so **the moved entity keeps the original's entity id** (l.5630-5665; align/geo_core_a.c l.819-860).
  3. `CXfAssocInfo::Check(old eptr, new eptr, old id, new id)` queues the remap (l.1393; body at mccore_xform.c l.4810-4960). The TP / plane reference counts ent +0x38 / +0x54 are copied back, then rewrite_ent (l.1396-1398).
  4. `mc_op_modify_entity_and_points(old eptr, ent, matrix)` (l.1420) updates **solid-history** ops only.
     mcgeomsld walks its own list DAT_1803da940 (align/geo_sld_mcop.c l.91-152).
  5. `df_change_eptr` updates drafting (l.1423).
- **Arcs that become splines** (non-uniform scale, convert_arc_to_nb) take another path (l.1280-1306):
  - `tp_update_assoc_ptr_single(ent, new eptr, true)` re-points the TP records with the arc's id;
  - each of their ops gets op +0x94 = 1 and op +0xf2 = 1 (l.5938-6050, offsets checked: base 0x2910);
  - replace_ent then rewrites the arc.
- After the loop (FUN_1808d8820 l.2940-3010):
  1. `CXfAssocInfo::End` (align/geo_core_a.c l.928-1002). For each queued pair:
     - flag 1: `tp_change_eptr(old, new, oldId, newId, 1, 1)`;
     - flag 2: `MastercamPlanes::SwapEntityIDs`;
     - flag 4: `grp_change_eptr`;
     - flag 8: `chook_change_eptr`;
     - flag 0x20: `mc_change_eptr`;
     - flag 0x40: `df_change_eptr`.
  2. `tp_change_eptr` (align/geo_core_b.c l.215-510):
     - every TP record whose eptr / id (and sub-type eptr fields 1/2/3/8/0x1b/0x1c/0x1e/0x2b) matches is re-pointed and
       rewritten (`rewrite_ent_assoc`, undo record 5);
     - with param_6 = 1, each op found gets **op +0x94 = 1, op +0xf2 = 1, op +0x97 = 0** (l.425-436; base 0x2930);
     - then `mark_op_dependents(op, 0, 0, 1, 0)` for each (l.442).
  3. The originals (group sel bit 2) are deleted: `FUN_1808dfb00(0x1f1fff)` -> `FUN_1808df760` ->
     `delete_ent(.., 1, 0, 1, .., 0, true)` (l.2963; align/geo_core_c.c l.1892-2040). `xform_move_assoc_ent` is still set,
     so there is no 4421 prompt and no TP clean-up. It is reset afterwards (l.2966).
  4. If any moved entity was ent_is_assoc (`local_5880`, l.2717-2760): `FUN_180038480(db_start)` and `validate_toolpath` (l.2999-3005).
     - FUN_180038480 (align/geo_core_c.c l.3-308) re-points any TP record whose entity id matches but whose eptr differs.
     - Each op touched gets **op +0x94 = 1, +0xf2 = 1, +0x97 = 0** and `mark_op_dependents` (l.215-237).
- **Stored chain points:**
  - tp_change_eptr and FUN_180038480 only change eptr / id fields in the TP records. Neither transforms point
    coordinates kept in a TP record.
  - **NOT DECODED:** whether any point data in a chain's TP record is moved, and how regeneration re-reads the moved geometry.
  - Decoded: the op stays attached to the same entity ids and is dirty.
- op +0xf2 meaning: **NOT DECODED**. It is also set to 1 together with +0x94 when a new op record is made
  (opdlg_port_audit.md 22.x, init_operation_db).

### 2.3 Copy and Join
- param_8 = 0, `cVar5 = 0`. Results go to new DB records with new ids: `FUN_1808d1df0` without an id swap (`store_ent`).
  There is no CXfAssocInfo and no TP change (l.1357-1366).
- The originals are untouched, so **no op changes**.
- Join adds connecting lines (FUN_1808e48b0, l.2757-2760).

### 2.4 Spec for the app (cui_xform.c l.15 / 28-30, cui_undo.c l.50 `mod_del`)
- Move: rewrite the selected entities in place, keeping their index / idn, instead of `mod_del` + new entities.
  Each op whose chain or points use a moved entity (ours: CamOp chains; stored: TP references by id):
  - mark it dirty (+0x94);
  - mark its dependents dirty (mark_op_dependents; ours: the existing cui_dirty dependents rule);
  - do not delete it;
  - do not show a warning (Mastercam shows none on Move; the invented "Warning: modified geometry ..." in mod_del must go).
- Point lists stored in our CamOp (2D chains' points, drill points) are **NOT DECODED** in Mastercam (2.2). The app
  needs a rule for them. Recommendation for the user: recompute them from the moved entities at Regenerate, as the
  op is dirty anyway.
- Copy / Join: new entities, ops untouched (as today).

---------------------------------------------------------------------------------------------------------------------
## 3. Other edits that rewrite geometry in place

Mechanism:
- `replace_ent(ent, &eptr, ..)` keeps the DB record and its id (mccore_xform.c l.6414-6545) and calls
  `list_op_check_ent(ent, eptr, -1)` (l.6521) and `mc_op_test_and_modify_entity` (solid history, l.6526).
- `list_op_check_ent` (decomp/dirty/core_tooldirty.c l.822-1130):
  - acts only for a live entity with ent +0x38 > 0 (or param_3 == -2);
  - acts only when the **geometry bytes changed**: the first 0x68 header bytes are zeroed before the memcmp
    (l.960-985), so a colour / level / attribute-only change does not dirty an op;
  - finds the ops with `CAssocUtils::GetOperationsByGeometry` (TP records of type 0x4000 / sub 2,
    decomp/dirty/core_regen.c l.1863-2060);
  - for each op not already dirty, with FBM / 0x88 / 0x8b filters, sets **op +0x94 = 1, op +0x97 = 0, op +0xac4 = 1**,
    and for opcode 0x89 also op +0x2880 = 1 (l.1046-1056).
  - The meanings of +0xac4 and +0x2880 are **NOT DECODED**.

Callers that rewrite in place:
- through replace_ent: `geo_callers_core.txt`, decomp/mccad_cmds;
- through `list_op_check_ent` + `write_ent` directly: Break2Pieces / BreakAtPoints.

| command | function -> in place | source |
|---|---|---|
| Trim Break Extend (1 / 2 / 3 entities) | sketcher_trim_1 -> trim_123 -> replace_ent | decomp/mccad_cmds/sketcher_trim_1.c l.1798, sketcher_trim_2.c (trim_123); wireframe.md 746 |
| Trim Many | sketcher_trim_2 -> FUN_1800dc870 -> replace_ent | sketcher_trim_2.c; wireframe.md 766 |
| Break Many Pieces | sketcher_trim_3 -> break_spline_const_error -> replace_ent (pieces: store_ent) | sketcher_trim_3.c l.2448 |
| Break Two Pieces / Break at Points | FUN_18004f9f0: list_op_check_ent + write_ent on the original; the new piece is store_ent (new id, not in any chain) | Break2Pieces.c l.322-323, 625; BreakAtPoints.c l.932 |
| Close Arc | close_arc -> replace_ent | close_arc.c l.86 |
| Break Circles | break_circles -> replace_ent on the first piece, store_ent for the rest | break_circles.c l.90-99 |
| Offset (Entity), Move mode | do_xform_offset -> FUN_180018230 -> replace_ent (squash_curve result). The command ends with `CheckOperationPlanes` + `validate_toolpath` when its flag bVar2 is set (the flag's source is NOT DECODED) | do_xform_offset.c l.1728, 636-642 |
| Sketcher line / arc edits | FUN_18018a6d0, FUN_1801904e0, FUN_180190f30, FUN_180204d10, FUN_180273c00 / e20 (lines); FUN_180199880, FUN_1801999a0, FUN_1801a9130 / 280, FUN_1801af6c0, FUN_1801b1a50, FUN_1801b4b60, FUN_1801c51b0 (arcs) call replace_ent. Which ribbon commands these are is NOT DECODED | sketcher_line_*.c, sketcher_arc_*.c |
| Fillet (trim on), spline from chain, connect to arc, rect attrs | make_fillet, make_nurbs_from_chains, make_spline_from_chain, connect_to_existing_arc, rect_set_rents_attrib / color (attribute only, so no dirty, see above) | geo_callers_core.txt |
| Undo / Redo | FUN_1802e1cf0 / FUN_1802e2430 -> list_op_check_ent; then FUN_180038480 + validate_toolpath | align/geo_core_c.c l.1465-1615 |

Not in place:
- **Join entities** rewrites the kept entity with `rewrite_ent` (no list_op_check_ent: decomp/xform/mccore_xform.c l.4578-4660).
  It deletes the absorbed ones with `delete_ent(.., 1, 0, 1, flag, 0)`, so their TP records are deleted at once and
  without a prompt when the flag is 0 (join_selected_entities.c l.1640, 1756).
- Whether `write_ent` itself marks ops is **NOT DECODED**.
- Chamfer / fillet without trimming, the Project command and Offset Chains delete and store (decomp/mccad_cmds counts);
  their op effect = 1.3.

Spec for the app:
- An in-place edit (the rows above) keeps the entity index / idn.
- The ops using it become dirty (+0x94) only if the geometry really changed.
- No prompt, no warning.
- The app's `mod_del` path (new entity + old one switched off) should become a rewrite for these commands.

---------------------------------------------------------------------------------------------------------------------
## 4. N1: "NC file exists" when posting

### 4.1 Where the settings come from (defaults)
`post_settings` is a 15-byte `post_dlg_settings` (mccore export `?post_settings@@3U..`, 0x1812ab000;
`?default_post_settings@@3U..` 0x1812ab010).

| byte | meaning (CPostDlg OnInitDialog / OnOK, decomp/n1/mcpost_postdlg.c; controls from the mcpostres dialog 25518 template) | default_post_settings (mccore .data) | mc-test01.mcam (2017 part, control page 2) |
|---|---|---|---|
| [0] | NCI file checkbox (130) | 0 | 0 |
| [1] | NCI radio: 1 = Overwrite (25341), 0 = Ask (25342) | 0 (Ask) | 0 |
| [2] | NC file checkbox (131) | 1 | 1 |
| [3] | NC radio: 1 = Overwrite (25339), 0 = Ask (25340) | **0 (Ask)** | **0** |
| [6] | Send to machine (25343) | 0 | 0 |
| [7] | NCI Edit (25336) && NCI saved | 0 | 0 |
| [8] | NC Edit (25335) && NC saved | 1 | 1 |
| [0xc] | Output Tplanes relative to WCS (109) | 1 | 1 |
| [0xd] | Output Mastercam file descriptor (25141) | 0 | 0 |
| [4] [5] [9] [0xe] | the Mori APT-CL dialog variant (mcpost_msg.c l.4216-4227) | 1 1 0 1 | 0 0 0 1 |

- Radio encoding: OnInitDialog sets radio = `2 - (byte != 0)`; OnOK stores `byte = (radio == 1)`. The radio's first button in
  the template and the DDX id is Overwrite (DDX_ProcessRadioButton 0x62fb = 25339, 0x62fd = 25341). This matches
  PostSelOps, where [3] = 1 means "no file prompt" (4.2), and the help pages
  (`cadfields/MCCore/Config_fields/NC_file_Overwrite.htm`: "automatically overwrites ... without asking permission";
  `NC_file_Ask.htm`: "prompts for an NC file name and asks permission before overwriting"). MCRbutn's value
  convention itself was not decompiled.
- Help agrees on the defaults: NC file checked "(default setting)" (`Config_fields/NC_file.htm`); NCI file cleared "(default
  setting)" (`NCI_file.htm`).
- **Storage: the control definition, not the registry.**
  - `CCNCControl::GetPostSettings` copies `CCNCControl +0x416b..+0x4179` (mccontrol_all.c l.34751-34766).
  - That is control page 2 (file_ctrl, copied to +0x3430 by SetFileCommon, 0xed0 bytes), at **file_ctrl +0xd3b**, which is
    **page-2 record +0xdc9** in the .mcam (file_ctrl = record +0x8e as in mcam_load.c l.534).
  - ControlDefaults seeds it from default_post_settings (mccontrol_all.c l.29461; mcmpparameters_mdcd_sources.c l.397-401).
  - Help: `mdcdlatmmtfields/Fields_controldef/Post_processing_dialog.htm` says these defaults are set in the control
    definition's "Post processing dialog".
  - The bytes in the table above were read from `~/mastercam-re/work/mp/coord/oracle/mc-test01.mcam` (record base 0x63091).
    The neighbouring bytes +0x416a = 1 and +0x417a = 1 match ControlDefaults (l.29463-29464).
- **The user's .mcx-9 (X9) parts:** the file_ctrl layout is shifted (strings at +2; A001.mcx-9). The X9 offset of the post
  settings is **NOT DECODED**, so use default_post_settings (NC Ask, NCI off) for them.
- **Interactive post** (Toolpaths manager Post selected: `COpMgrTree::PostSelected` -> `OpMgrPostX(NULL, ..)` ->
  FUN_180023fa0, decomp/opman_tree_all.c l.22661-22728, mcmill_all.c l.25419-25470):
  1. `post_settings` is saved (l.26198), then filled from the first group's machine control (`GetPostSettings`, l.26246).
  2. It is replaced by the values kept from this session's last Post dialog when the machine (+0xe28) is the same and
     only one group is posted (DAT_1804a81f8, l.26709-26714).
  3. CPostDlg is shown (l.26738-26755). Cancel stops the post. If neither NC nor NCI is to be saved ([2] == 0 && [0] == 0),
     nothing is posted (l.26852-26856).
  4. The dialog result is remembered for the session (l.26761-26765).
  5. At the end post_settings is restored (l.26640). Nothing is written back to the control.

### 4.2 NC file: Overwrite vs Ask (`CBatch::PostSelOps` FUN_180023500, decomp/mcmill_all.c l.25501-25947)
For each NC program (NCI file name group):
1. If [7] (Edit NCI) and the NCI text exists: `edit_filename(NCI, [2] || [6])` (l.25653-25665).
2. The NC name is `build_filename(NCI name, NC folder, NC suffix)` (l.25667-25676).
3. **[2] && [3] (Overwrite) && the NC folder is writable**: post directly; the post opens the file and replaces it.
   No question is asked. "Writable" = FUN_1800201f0(folder): it must be a directory and GetTempFileNameA "MC" must succeed there
   (l.23582-23620; call site 0x18002377c).
4. **[2] && (Ask, or the folder is not writable)**: `PromptForFileToSave(nc, "w", suffix, 0, 0, .., true)` (l.25854-25880).
   This is a Windows **Save As** dialog:
   - initial folder / name = the NC name;
   - title "" (the Windows default);
   - filter "<SUFFIX without dot> Files (*.<suffix>)|*.<suffix>|" plus mccoreres 2858 "All Files (*.*)" "|*.*|";
     the exact upper-casing of the label is **NOT DECODED** (MFC ordinals 8263 / 13813);
   - default extension = the suffix;
   - flags `OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY` (Flags = 10), so **Windows itself asks "... already exists. Do you want to
     replace it?"** when the chosen file exists;
   - the dialog repeats while `canWriteFile` fails (decomp/mastercam_filedlg.c PromptForFileToSave l.2444-2640).

   **OK**: the chosen folder becomes the group's NC folder (`SetNCPrefix(dir, group)`, l.25877) and that file is posted.
   **Cancel**: that NC program is **skipped** (no post, no message). The NCI text file of that program is then not removed
   even when "NCI file" is off. The `remove(NCI)` at l.25848-25851 is only reached on the post path.
5. After posting (l.25810-25846):
   - [8] Edit opens the NC file (`edit_filename_list`);
   - [6] sends it;
   - if NC is not to be kept ([2] == 0, send only), the NC file is removed;
   - if [0] == 0, the NCI text file is removed.

### 4.3 NCI file (FUN_180021280, decomp/mcmill_all.c l.24105-24268)
- `[0] == 0` (NCI not kept) or `[1] != 0` (Overwrite): `bnci_to_nci(0, 1, ..)` writes the NCI with no question (l.24154-24155).
- `[0] && Ask`: for each NCI file, `PromptForFileToSave(nci, "w", ..)` (l.24181).
  - **Cancel stops the whole post** (`*param_4 = 0` makes PostSelOps skip everything, l.25619).
  - A changed name is put into the ops' NCI name (op +0x170, UpdateListAndDB) for this post, and PostSelOps puts the
    original names back at its end (l.25925-25940).
- **mccoreres 7277 is not part of posting.**
  - The prompt `"File already exists, overwrite?\n%s"` (the resource text includes the quote characters), caption
    **5028 "Operations Manager"**, Yes / No, question icon, is FUN_1800a1800 (decomp/n1/core_ncifile.c). It is
    message_box_yes_no_str(caption, text, 2).
  - It is asked only when bnci_to_nci's param_2 == 0 (call 0x180088f60 passes it).
  - Posting always passes 1 (above). Callers with 0 are nci_manager modes 0xc / 0xd (mcmill_all.c l.5688, 5701).
    Their commands are **NOT DECODED**.
  - In FUN_1800a1800, when several ops share one NCI file, the first opens "wt" and later ones "at".
  - No: the mode becomes one character (`sprintf(mode, "%c", ..)`, argument lost by Ghidra). bnci_to_nci compares it with
    DAT_1812b10dd and skips writing that file (decomp/mccore_bnci_nci.c l.311). The exact mode character is **NOT DECODED**.

### 4.4 Spec for the app (cui_ops.c ~l.633 / 675-721; there is no Post dialog yet)
- Use the control's post settings: the 2017 page-2 record +0xdc9 when the part is a .mcam. For .mcx-9 parts and when
  the page is missing, use default_post_settings: **NC Ask, NCI not kept, NC Edit on**.
- With NC = Ask (the default):
  - before writing each NC file, show a Save As file dialog on the NC name (folder = the NC folder; filter "NC Files (*.nc)" +
    "All Files (*.*)"; default extension the suffix) with the platform's overwrite confirmation;
  - OK: post to the chosen path and remember the folder as the group's NC folder;
  - Cancel: skip that program, with no message.
- With NC = Overwrite and a writable folder: write without asking (today's behaviour).
- NCI: nothing to ask with the defaults (the app does not keep an NCI text file).
- Session memory of Post-dialog choices only matters once a Post dialog exists.
- Our own deviations to record, if any: the NC Edit default (opening an editor after posting) is not ported here.

---------------------------------------------------------------------------------------------------------------------
## 5. Open questions / NOT DECODED (summary)
1. Regenerating an op whose chain lost entries, or that has no geometry left: what the engines do (1.3).
2. Whether point data in a chain's TP record is transformed on Move (2.2). This matters for the app's stored chain points.
3. The meanings of op +0xf2, +0xac4, +0x2880 and of ent +0x28.
4. Product enable bits 6 (power_user suppression gate); the user's "Suppress associativity warning messages" value.
5. The X9 (.mcx-9) offset of post settings in the control page; the exact Save As filter label case.
6. Move to Origin / Roll move flag; Offset's bVar2; `write_ent` marking ops (Join).
7. Dialog 6561's default push button; the import behind 0x180cee7c8 (assumed CDialog::OnOK).
