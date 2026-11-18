# Audit 2026-10-26: shared UI core (viewer/core_ui) vs Mastercam behaviour

Read-only review of `<repo>/viewer/core_ui/*.c|*.h|*.inc` (main tree, current
files, including another agent's uncommitted edits) plus `~/mastercam-re/work/wt_ncidrill/viewer/core_ui/cui_ops.c`
`pp_ok_bnciu` (D2 work in progress). Checked against `formats/user_decisions.md` (ids P*, B*, Q*, D*, M*, S-D1, P-D*,
SIM*, N*, UI-1, R-1, 3D-*), `formats/improvisation_review.md` and `formats/provenance_audit.md` (2026-10-16 classes
DECODED / CORRELATED / SUBSTITUTE / STUB / IMPROVISED).

Method: grep of every marker (`ours`, `inferred`, `stand-in`, `measured`, `fallback`, `instead`, `simplif`,
`convenience`, `TODO`, `guess`, `invent`, `approximat`, `NOT DECODED`, `our own`, `assum`: 586 hits) read in context;
all 167 message / prompt call sites (`cui_message`, `message`, `snprintf(cam_msg...)`) checked against the string
tables in `~/mastercam-re/regress/str/*.tsv` and the dialog dumps (`regress/old_opdlg_b12/exports/*_dialogs.tsv`);
the op-edit, delete, copy, reverse, post, undo, tool-add and chaining handlers read in full.

Line numbers are the main tree unless the path says `wt_ncidrill`. "Decision" = an id in user_decisions.md;
"2026-10-16 list" = provenance_audit.md "Intentional deviations", which is NOT a user decision.

Most of core_ui is well sourced: the dialog templates, the decoded strings, the dirty-op machinery (cui_dirty.c,
edit_operation compare, dialog 977), Q19 / Q20 (cui_defaults.c, cui_preview.c), the AutoCursor pass order and most
chaining prompts. Nearly all string literals are real resource strings. The findings below are the places where the
behaviour differs.

---

## 1. User-visible deviations without a user decision (most important)

| # | file:line | what our code does | what Mastercam does | marker? | decision? | impact | recommendation |
|---|---|---|---|---|---|---|---|
| 1.1 | wt_ncidrill cui_ops.c:527 (+ :506 comment, :545) | D2 `pp_ok_bnciu`: when `bnciu_apply` refuses the patch it returns 0. `pp_ok_dirties` then also returns 0 (only masked fields changed), so OK goes on to `generate_pending`, which regenerates the stored op and takes it over. The comment at :506 says "the op is marked dirty, as before", but the code does not do that. | BnciUpdate::Update fails -> the op is marked dirty (+0x94 = 1) and keeps Mastercam's NCI (decomp/mccore_edit_operation.c l.2572-2589) | comment says the opposite of the code | D2 asks for the patch, not for a regenerate on failure | **user-visible: Mastercam's toolpath is replaced by ours** on a feed / speed edit that the patch cannot take | replace with decoded behaviour: on failure call `pp_ok_mark_dirty()` (the stored op stays dirty with its NCI) |
| 1.2 | wt_ncidrill cui_ops.c:528 | after a D2 patch the status line shows "Operation N updated: K toolpath record(s) patched (toolpath kept)" | edit_operation's patch path is silent; `gen_commit` here already clears the message "as Mastercam shows the toolpath only" (cui_ops.c:617) | no | no | user-visible text with no source | remove (or print to stdout for tests only) |
| 1.3 | wt_ncidrill cui_ops.c:511 | D2 is skipped for records shorter than 0x1600 and for `regen` ops, and those ops are regenerated | DontAllow has no length gate; a dirty op stays dirty | no | no | some stored ops regenerate instead of being patched or marked dirty | fall back to dirty, as in 1.1 |
| 1.4 | cui_ops.c:149, :1041-1043, :1088 | Delete / Cut / Copy of an op **stored in the part** is refused ("Only operations created here can be deleted; toolpaths stored in the part are read-only.", "operations stored in the part cannot be deleted here", "toolpaths stored in the part cannot be copied here") | any op can be deleted, cut, copied and pasted (CutOperations / CopySelected / PasteOperations, formats/opmanager_menu.md) | partly (messages kept as "ours" in improvisation_review item 17) | no | **high**: the main op-manager commands do not work on the user's own parts | ask the user. Delete needs `mcam_remove_op` + the save's delete path (mcam_saveops delete + append exists since S-D1); copying needs the record copy (list_op_copy) |
| 1.5 | cui_sel.c:119-125 | deleting geometry that an op uses is refused with an invented message ("Some of the selected geometry is used by operation(s) ... Delete or edit those operations first."; not in any string table) | asks mccoreres 4421 "Geometry is used in a toolpath or plane.  Are you sure?" (Yes deletes) | yes ("deliberate") | no (2026-10-16 list only) | high: blocks a normal edit, and the text is invented | ask the user. If the refusal stays, still show 4421's text and record a decision |
| 1.6 | cui_undo.c:50-51; cui_xform.c:15, :28-30 | Move / modify of geometry makes new entities; ops keep the old (deleted) entity "until rechained", with an invented warning "Warning: modified geometry is used by operation(s) ..." | Xform Move rewrites entities in place and keeps their ids, so the ops stay associative (they become dirty) | yes ("ours: replaced") | no | **high**: after a Move the op still machines the old position; the warning text has no source | replace with decoded behaviour: keep ids on Move (do_xform_trans's in-place rewrite) and mark the users dirty; drop the invented text |
| 1.7 | cui_ops.c:56, cui_chain.c:132, cui_opdlg.c:614/1498-1499, cam2d.c:1651/1673 | gallery "Engrave" makes a **Contour** op with contour_type 2, compensation off, leads off. It is saved as a Contour (mcam_saveops.c:529) | Engrave 3628 is a separate module (MWENGRAVE `m_main`, formats/opmanager_menu.md l.92) with its own op and dialog. Contour type value 2 in Mastercam's own combo is not "Engrave" | yes ("Engrave is ours") | no | high: wrong op type in the user's part, and an invented engine | grey "Engrave" until MWENGRAVE is decoded, or ask the user |
| 1.8 | cui_ops.c:1000-1017 | Reverse toolpath leaves ops made / edited here unchanged, with no message (`first` is built but never shown) | ReversePath reverses every selected op's NCI | partly (comment) | no | medium: the command silently does nothing for our ops | needs decode or ask: reverse our moves through nci_write + nci_reverse, or grey the command when only our ops are selected |
| 1.9 | cui_ops.c:633 (comment), :675-686, :704-721 | posting replaces an existing NC file without asking | asks before overwriting (decision **N1**, marked TODO) | yes ("ours has no prompt") | N1 says ask: **not done** | medium: the user's NC file is lost silently | implement N1 (the decoded overwrite prompt) |
| 1.10 | cui_chain.c:88-98, :128 | 3D ops: drive selection toggles whole solids / surfaces; the status line is composed ("Select Drive Surfaces (n solids, n surfaces; Enter = End Selection)"); containment is never asked and the drive's box is used instead | msurf_assoc + the "Toolpath/surface selection" dialog (mcmsurfres 120 family: drive / check / containment); prompt mcmsurfres 406 alone | yes ("ours", NOT PORTED) | 3D-1..4 cover the engine and mesh, not the selection | medium-high: a different machining region from Mastercam's (no containment, no check surfaces) | needs decode (mcmsurfres 120 dialog); until then use the plain 406 prompt |
| 1.11 | cui_ops.c:360-361, :805, :832 | OK on an op without a valid toolpath (stored op without NCI or stale, our op with no moves) always regenerates | edit_operation marks it dirty; nothing is computed until Regenerate | yes ("ours") | no | low-medium: an unexpected computation, and a stored op is taken over | replace with decoded behaviour (dirty), or record a decision |
| 1.12 | cui_ops.c:789, :1270 | an op that `cam_op_from_file` cannot read: double-click / Parameters does nothing on screen (stdout only) | the parameters dialog opens | yes ("nothing here") | no | medium: silent no-op on the user's parts | show the decoded "not editable" state (dialog greyed) or a sourced message |
| 1.13 | cui_ops.c:554-563 | OK with a tool not in the part's list registers an invented "custom" flat endmill (type 10, op diameter, op feeds) in the part tool list | the op holds a tool assembly from a library / the part; no tool is made up | no | no | medium: a tool appears in the user's part | needs decode (tool assembly) or refuse with 3837 |
| 1.14 | cui_toolpage.c:183-188 | an inch tool in a metric part (or the reverse) is refused with an invented message ("... Use a tool library in the part's units.") | the tool is converted (conversion NOT DECODED, formats/toollib_net.md) | yes (NOT DECODED) | no | medium: library tools cannot be used across units; the text is invented | needs decode (unit conversion). The refusal is a fair STUB, but its text needs a decision |
| 1.15 | cui_toolpage.c:190-197 | a tool number already in the part: renumbered silently to max + 1 | WarnOfDuplicateToolNumbers (+0x11bc gate, mccoreres 5583) | yes (NOT DECODED) | no | low-medium: tool numbers change without notice, and the post's T words change | needs decode (+0x11bc / OpenToolNumber) |
| 1.16 | cui_toolpage.c:172-177, :528-533; cui_toolmgr.c:467 | with no Mastercam library reachable, a shipped "starter_tools_mm.tools" (our own data) is opened in the Tool Manager / Select library tool | the machine definition's library, else nothing | yes ("user request 2026-10-21") | **not in user_decisions.md** | medium: invented tool data is offered as the library | ask the user to confirm, then record it in user_decisions.md (UI-*) |
| 1.17 | cui_undo.c:106-130 (U_OP_ADD / U_OP_DEL / U_OP_EDIT), cui_ops.c:607, :613, :907, :931, :1046, :1073 | Ctrl+Z / Ctrl+Y undo and redo op creation, deletion and edits | UndoEvent covers geometry / solids; ops are recovered with Undelete (DeleteBufferedOperations). No source puts ops on the undo stack | no (header says ops are undone; no citation) | no | medium: undo semantics differ, and an op edit "undone" is not what Mastercam does | needs decode (UndoEvent's record kinds); if ops are not on it, drop the U_OP_* records |
| 1.18 | cui_snap.c:18-20 | AutoCursor: every implemented snap mode on by default; Angular step 1.0 | the default set and step are NOT DECODED; the user's own settings live in their config / registry | yes | no | low-medium: snaps the user does not have on fire | needs decode: read the user's AutoCursor settings (config / NTUSER.DAT hive, tools/regf.py) |
| 1.19 | cui_opdlg.c:1432 + :2088 marker | Thread Mill Linking 2837: editing Clearance turns clearance on (`clr_on = 1`); a new Thread Mill op takes its clearance from the generic Linking flags, not record 0x1230 (cui_ops.c:215) | the HSTThreadmillLinkPage keeps its own clearance field 0x1230 | marked (tooltip) | no | low-medium | replace with decoded behaviour: take 0x1230 for new ops too |
| 1.20 | cui_opdlg.c:684-690, :2009-2010, cam2d_ops.c:1186/1191 | Thread Mill: "Major thread diameter" 0 = the picked circle's diameter (the generator takes the circle before 0x1298); extra controls "Thread depth (0 = 0.6134 x pitch)" and "Full-profile tool (one revolution)" | the page's 0x1298 (25 in every stored op) drives the engine; no depth / full-profile controls | yes ("ours") | no | medium: depth / full-profile are **dead controls** (no engine reads `thread_depth` / `thread_full`); the diameter rule differs | remove the two dead controls (still open from 2026-10-16); decode how bnci_threadmill picks the diameter from the circle |
| 1.21 | cui_opdlg.c:2027 (10318 page) | the Arc filter page gets an extra "Create arcs in XY" check box (our G2/G3 output switch; label from the old mcmillres 318) | no such control on 10318; the arc output comes from the filter record | yes ("ours") | partly (Q6 / Q8 cover the filter, not this switch) | low-medium: turning it off changes the posted NC | ask the user: keep as a labelled "(ours)" post option, or remove |
| 1.22 | cui_opdlg.c:2015-2017 | Dynamic: "Machining region strategy" / "Stay inside" / "From outside" (dialog 657 strings, sourced) drawn on page 3205 | the controls are on their own page 657 | yes ("ours") | no | low: placement only | keep as glue until page 657 is laid out (labels are decoded) |
| 1.23 | cui_bbox.c:31-35, :293-296, :424-433, :728-732 | Bounding Box: a Lines-and-arcs outline preview instead of the live solid; face pick by our ray test; the push-pull arrow takes no typed value; arrow length is `ext * 0.08` | live Parasolid solid, PK face pick, SelArrowBase edit field | yes | no | low-medium | needs decode (SelArrowBase); keep the rest as labelled stand-ins |
| 1.24 | cui_extrude.c:204, :420-421, :571-573 | Extrude preview drawn shaded even in Wireframe display; arrowhead size and the orange distance-arrow colour are ours | GrArrow is screen-constant, colour NOT DECODED | yes | no | low | keep as glue; decode GrArrow later |
| 1.25 | cui_planes.c:256-258 | Update Plane: OK enabled only for choice 2 | choices 0 / 1 change the planes archive | yes | no | low: a greyed OK that Mastercam has enabled | fine as a STUB (NOT PORTED); no action |
| 1.26 | cui_geo.c:366-367, :540, :1105-1110; cui_chain.c:349-354; cui_xform.c:405-406 | arcs in another plane refused / skipped with invented messages ("An arc in another plane: this function works in the XY plane here (NOT PORTED)", "... cannot be used by this chaining mode here (NOT PORTED)", "%d arc(s) not scaled ...") | these functions work in 3D / any C-plane | yes | no | low-medium: invented text, though honest STUBs | keep as STUBs; record the wording as a decision or switch to greying the command |
| 1.27 | cui_stock.c:38, :367-378; cui_overlay.c:207, :250, :266; cui_misc.c:76; cui_bbox.c:497 | "Not enough memory ...", "The stock file ... holds no triangles ...", "The tool plane could not be added." boxes | Mastercam shows nothing in these cases (cui_stock.c:38 says so) | yes ("ours") | no | low (error paths only) | keep (safety glue), but list them in provenance_audit "Messages" |
| 1.28 | cui_extrude.c:406, :409; cui_stock.c:1230-1231 | "No point: pick a snapped point in this view" (invented) | point_proc's map_cursor_to_3d result NOT DECODED | yes | no | low | needs decode, or record the wording |
| 1.29 | cui_ops.c:167 | Delete empty groups: "no empty groups" / "the Toolpath Manager keeps at least one group" | not traced | no | no | low: invented text | replace with the decoded DeleteEmptyGroups result (likely silent) |
| 1.30 | cui_ops.c:698, :712, :722; :691-692 | "nothing to post: the part has no operations", "posted N operation(s) to ... (LinuxCNC post)" | not traced for the empty case; the post results belong to the LinuxCNC post | partly (2026-10-16 list: LinuxCNC post results) | P2 covers the LinuxCNC post | low | keep; add "nothing to post" to the messages list |
| 1.31 | cui_dirty.c:61 | "too many changed operations (%d)" | no limit | yes ("ours") | no | none in practice (SED_MAX 1024) | keep as glue |
| 1.32 | cui_dirty.c:203; cui_defaults.c:292, :349; cui_ops.c:1008 | message box caption "Mastercam" | caption is Mastercam's | no | contradicts the 2026-10-16 de-branding rule (caption 5175 -> program name) | low | replace with the program name, as everywhere else |

## 2. Unmarked improvisations (no marker, no cited source)

| # | file:line | what our code does | Mastercam | impact | recommendation |
|---|---|---|---|---|---|
| 2.1 | wt_ncidrill cui_ops.c:528 | D2 success message (see 1.2) | silent | user-visible | remove |
| 2.2 | cui_ops.c:554-563 | "custom tool" registration (see 1.13): type 10 = flat endmill, number `next_tool_num()` | unknown | user-visible | needs decode |
| 2.3 | cui_undo.c:106-130 | op undo records (see 1.17) | unknown (likely not on UndoEvent) | user-visible | needs decode |
| 2.4 | cui_ops.c:1000-1017 | Reverse: our ops skipped silently; the reason text `first` is computed and dropped | ReversePath reverses them | user-visible | see 1.8 |
| 2.5 | cui_ops.c:167 | Delete empty groups messages | not traced | user-visible | see 1.29 |
| 2.6 | cui_ops.c:149 | "select an operation first" (also :1088) | not traced (the menu items are greyed with no selection) | user-visible | grey the menu entries instead (as OMregenSelected's 3964 rule) |
| 2.7 | cui_toolpage.c:182 | "The tool list is full (256 tools)." | not traced (also listed 2026-10-16) | low | keep, add to the messages list |
| 2.8 | cui_stock.c:865-880 (stock_regen_mark comment) | says "Dialog 977 is NOT PORTED ... the post takes the 25351 branch without asking" and "a stored Mastercam op cannot be regenerated here" | 977 **is** ported now (cui_dirty.c dop_check / dop_answer) | stale comment only | fix the comment (docs) |
| 2.9 | cui_opdlg.c:674-679 (row-laid fallback) | Dynamic Contour "Wall stock width" / "Outside / Inside the chain" are still in the fallback page body. Batch 9 removed them from the template page (cui_opdlg.c:2019) and no generator reads them | none | only if a template is missing | remove from the fallback too |
| 2.10 | cui_feat.c:114, :181 | extrude polyline chord tolerance `0.002 + ext * 2e-6` | no source | low (solid shape accuracy) | needs decode or a cited constant |

## 3. Acceptable "ours" glue (no user-visible Mastercam behaviour, or covered by a decision)

| file:line | what | covered by |
|---|---|---|
| cui_defaults.c whole (pristine copy, dated backups, "Reset defaults file to original (ours)", "Restore a dated backup (ours)", refusal texts for undecoded ConvertFile / oplib_save / X9 update) | Q19 + Q19 addition, S-D1 |
| cui_preview.c:20-26, :67-68 (overlay preview, custom-tool refusal, Cancel exact) | Q20, P-D1, P-D2 |
| cui_ops.c:310-330 pp_reload_defaults commits at OK | Q18 |
| cui_ops.c:624-692 post_ops_mp (Mastercam post, NC naming, folder) | P1-P4, Q23, Q24, N2 (but see 1.9 for N1) |
| cui_ops.c:647, :743; cui_dirty.c:175 (refuse stale stored ops; post works on every op) | D1, D4 |
| cui_ops.c:590-592, :1080 ("%s: warning: %s" after an engine stand-in) | B6-1 / dec1 |
| cui_opdlg.c:2058-2087 (cutter comp Computer / Off, MCAM_CUTTER_COMP, Face link feed opt-in, drill "(not posted)") | B7-2, B7-4, B8-1, Q29, Q30 |
| cui_opdlg.c:2090-2095 "Unverified (ours)" on Peel / Dynamic Contour | Q2, M4 |
| cui_opdlg.c:1500-1505 entry methods 1 / 2 / 4 listed, 5 only when stored | B9-3, M2, M3 |
| cui_opdlg.c:2219-2222 misc-values description from Generic Fanuc text | B8-5 (stand-in until the embedded post text is decoded) |
| cui_grpprops.c:46, :75, :282, :354, :431, :523 GP_MPPOST "(ours)" check box | P2 |
| cui_toolpage.c:286-310, cui_grpprops.c:201-238 %MCAMDIR% / shared-folder rule | P1 |
| cui_verify.c (Simulator window, our engine, resolution `/400`, status lines, settings subset) | Q33, SIM1-SIM3 |
| cui_opdlg.c:67-92, :486-522 (3D op page trees / titles composed as OptiRough's) | 3D-1..3D-5, R-1 (the trees are labelled ours; titles low impact) |
| cui_misc.c:116-126 About, Print -> BMP | 2026-10-16 intentional (program identity, out of scope) - keep |
| cui_doc.c (all .mcamui project lines: UPLANE, SOLIDATTR, REGEN, DIRTY, GRP*, messages) | 2026-10-16 intentional (.mcamui format) - glue |
| cui_overlay.c:23-30, :204, :249, :265; cui_stock.c:40 (GL upload chunking, OOM drops) | pure glue (crash safety) |
| cui_dirty.c:144 (no regen while the op dialog is up), :177 (bnci_op_section_valid = has moves) | glue with a stated reason |
| cui_snap.c:124, :221, :248, :347, :368, :392-406, :838 (binning, culling, golden-section refinement, +1 px, drag suppression, snap_bench) | numerical / speed glue |
| every script / test step marked "ours (tests)" (cui_ops.c:1204-1410, cui_geo.c:1688-1786, cui_extrude.c:790-916, cui_verify.c:487-501, cui_bbox.c:855, cui_toolmgr.c:865-954, cui_analyze.c:439-451, cui_sldchain.c:609, cui_ecp.c:201, cui_misc.c:271, cui_stock.c:1323) | test glue |
| cui_opfields.h:25-28 (nd / ours / tip fields) | glue |
| UI-1 dialog placement (front ends) | UI-1 |
| cui_xform.c:577-599, :698 (toggle codes, Direction as radios, one layout for Uniform / XYZ) | layout glue with decoded parameter names; low |
| cui_backplot.c, cui_analyze.c, cui_toolmgr.c NOT DECODED blanks / greyed items | proper STUBs |

## 4. 2026-10-16 items still open (re-checked against the current core_ui code)

| 2026-10-16 item | current location | status |
|---|---|---|
| "Only operations created here can be deleted" (improvisation_review item 17, KEPT) | cui_ops.c:1043, :149, :1088 | **still open, significant**: see 1.4 |
| delete of geometry used by an op refused (Mastercam asks 4421) | cui_sel.c:119-125 | **still open**: see 1.5. The message text is now also invented (not 4421) |
| Edit flow "ops edited here are regenerated at once" | cui_ops.c:340-361, :466-474 | **mostly fixed**: dirty marking ported (pp_ok_mark_dirty, op_mark_dirty_edit). Still open: our ops are regenerated instead of NCI-patched; ops without a toolpath regenerate (1.11); D2 failure path (1.1) |
| Thread Mill "Thread depth" / "Full-profile tool" ours controls (provenance_audit corrections l.262-263) | cui_opdlg.c:686-690, :2009-2010 | **still open**, and now confirmed dead (no engine reads them): see 1.20 |
| UI item 5: heights evaluated once at the first chain's Z; clearance off -> rapids at retract / feed plane | cui_ops.c:228-247, :262 | still open (marked; multi-Z chains get one depth) |
| panel_keys.tsv (wireframe panel values persisted per unit) | cui_geo.c:1208-1209 | still open: user-visible (values survive sessions, Mastercam keeps only autocomplete history). Needs a user decision |
| AutoCursor Fast Point not ported | cui_snap.c:17 | still open (greyed / absent; STUB) |
| duplicate tool number warning keeps the edit open | cui_toolpage.c:194 | changed: now a silent renumber on add (1.15) |
| Levels / Planes row fill, planes beyond the 7 system planes | cui_planes.c:44, :63-71; cui_stock.c:146 | still open (STUB) |
| facing_op_uses_stock | cui_ops.c:265-275 | **fixed** (decoded make_facing_pock_bound) |
| tool number renumber, unit mismatch, 256-tool limit messages in part_add_tool | cui_toolpage.c:182-197 | still open: 1.14 / 1.15 / 2.7 |
| Engrave as a contour variant (not listed in 2026-10-16) | cui_ops.c:56 | new finding 1.7 |

## Notes for the decode queue (formats/TODO.md candidates; not added, read-only review)
- D2 failure -> dirty (1.1) is a one-line fix with a decoded source and should come before D2 is merged.
- Xform in-place rewrite (1.6): do_xform_trans / xform_store_ent already partly decoded (cui_xform.c header).
- Op Delete / Copy for stored ops (1.4): the save side exists (S-D1 delete + append).
- UndoEvent record kinds (1.17), WarnOfDuplicateToolNumbers (1.15), tool unit conversion (1.14), mcmsurfres 120 selection (1.10),
  MWENGRAVE (1.7), the user's AutoCursor settings (1.18).
