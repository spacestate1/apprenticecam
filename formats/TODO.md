# apprenticeCAM to-do list

The work queue in one place. Details live in the linked docs. The user's answers are in `user_decisions.md`.
When an item is done, mark it done here (with the commit) and record it in the doc it links to.

## Solids on OpenCASCADE: save path + Extrude (2026-11-18, SOL-1..SOL-4; branch worktree-agent-a34e0d6f9f3a722e2, not merged)
- DONE (not merged): stage 1 - the XT writer in the build, Parasolid 27 (X9) / 28 (2017) output, `tools/tests/xt_roundtrip.sh` (formats/
  occt_xt.md 10); the C history serialiser `viewer/sld_ops.c` (`tools/tests/sld_ops_test.sh`: 2,988 / 2,988 corpus extrude ops and 1,150 /
  1,150 ops blocks byte-identical). Stage 2 - the Cut / Boss names decoded (formats/solid_history.md 3.2.1: n1 / n2 / n3, kinds 24 / 45 / 51,
  the 4 / 31 swap corrected), McChainOp xa0 = first entity reversed (2,894 / 2,894), every part save writes the solids made here (SOLID_ID +
  SOLIDXMT + XT + McOpsFile, solid_history.md 8), refusing bodies with NOT DECODED names (Both directions, Draft, Thin wall, primitives).
  Stage 3 - Extrude on OCCT (`viewer/occt_solid.cpp`, formats/extrude.md 7): exact arcs, every mode, Mastercam's names per op;
  `tools/tests/sld_replay_test.sh` replays every user solid's history: 277 / 295 ops make Mastercam's names, 83 / 108 volumes equal
  (buckle-v* chain gaps); `sld_kernel_test.sh` closed-form volumes; solid_compare `sv_*` scenes (new 2017 part, solid added to an X9 part,
  edited op regenerated + saved, refused draft, thin open).
- Regression vs rg_s3: solid 2 -> 61 + 5 new scenes (count word / exact volumes / edge order, extrude.md 7), chain 0 -> 19 (sc_*: solid
  chaining of bodies made here walks their XT now), stockplanes 16 -> 18 (exact cylinder volume; the Iso-plane stock box of a body: a
  pre-existing triangle-count bug in stock_bbox_sel fixed), wf 1 -> 3 (bbox_2d / bbox_cyl_z: exact cylinder volume), srf 0 / 92, fileop
  205 / 205 moves files identical to rg_s3. Goldens re-blessed for exactly those scenes (the baseline failures left as they were).
- Left: (a) Cut / Boss / edit on the part's own solids (convert a loaded solid's history into features: chains by eptr; Mastercam replays
  on edit) and edit of a reopened part solid's ops; (b) the names of Both directions / Draft / Thin wall (trace ps_sweep_wire,
  ps_thicken_sheet, ps_draft_extrude_faces, the Both union) so those solids can be written; (c) SldPrimitiveOp (Bounding Box solids);
  (d) writing SWEPT_SURF / SPUN_SURF instead of exact B-surfaces for extruded / revolved splines (the reader maps them since xtfix2; check
  what Mastercam writes for a spline extrude); (e) Boolean, Fillet / Chamfer (SOL-4 next); (f) delete solid25.c's slab kernel once the
  no-OCCT build is not needed; (g) decode Parasolid's face merge after booleans + its naming (SOL-5a) and chain_to_pock_bound's gap
  closing (SOL-5b); (h) the SOLID_ID attributes from main_attrib (dbput_solid). Not verified in Mastercam (no Windows): formats/windows_checklist.md should get "open sv_new_mcam.mcam /
  sv_x9_add.mcx-9, Regen, edit the Extrude".
## Constant Fillet decode (2026-11-18, branch worktree-agent-a351f00a68046add6, not merged; formats/solid_fillet.md)
- DONE (phase 1, decode only): Solids > Modify > Constant Fillet fully decoded - flow, selection -> edges, every panel control
  (defaults, limits, session persistence), PK_EDGE_set_blend_constant / PK_BODY_fix_blends options (field names from pskernel.dll's
  printers), result naming, the SldFilletOp history record (tools/sld_fillet.py: 858 corpus ops parse exactly), failures, OCCT mapping.
- DONE (phase 2, port, on int-s5 merged into the branch; user decisions SOL-F1..F5): the command, the panel, the pick, preview, OK / OK-New /
  Cancel / Esc, edit + replay, session, project lines, the SldFilletOp record (858 / 858 corpus ops byte-identical through viewer/sld_ops.c);
  solid_scenes fl_* / sv_fillet_* (solid_fillet.md 11).
- Open (user): a fillet on a solid read from the part is refused ("not ported"): its faces / edges have no names until its history is
  replayed (solid_history.md 3.3) - port the replay with names first, or write the op with id 0 records (a deviation)?
- Open (user): SOL-F5 (smooth-edge test at OCCT's default angle) was decided by the main session - confirm.
- Left: mitered fillets name some rebuilt faces 0x34 (OCCT rebuilds a previous blend face without history; no corpus op is mitered);
  Variable / Face-to-Face Fillet (S17).

## Shaded face highlight (2026-11-18, branch worktree-agent-afdd857f29b50aa2a, not merged)
- DONE (not merged): decoded gafgr's GAF stipple highlight (GrOGLPipe stipple lists, InitStippleData masks / passes, the draw items'
  user masks, MaintainGrRenderSurface's flags; formats/ui.md "Shaded face highlight (GAF stipple)", dumps decomp/gafhl/) and ported
  it for surfaces in solid_mode 1 (scene.c scene_srf_look + mesh shader stipple, config 4053); srf_scenes.txt srf_hl_* (+ srf_compare.sh
  EV: column and .px colour counts).
- Open (user): this program's one shaded mode is render_cfg 4 (Outline Shaded); Mastercam's Shaded (render_cfg 3) differs for a
  selected surface (list 1: lit select colour + black alpha 0xc4 dots, the Selected flag set by the repaint). A separate mode would
  need the status-bar Shaded / Outline Shaded split (both are solid_mode 1 now).
- Left: translucent display, Glow type 1 / 2, solid faces / bodies, meshes (formats/ui.md, same item).

## UI text fit (2026-11-17, user: "make sure text never overlaps anywhere in drop downs etc"; branch worktree-agent-a49c431a2f8fde47a)
- DONE (not merged): viewer-qt/src/textfit.cpp (combo popups as wide as their items; hand-placed text controls grow into free space,
  multi-line statics / BS_MULTILINE check boxes wrap; header-less trees scroll sideways; Win32 check box gap; the checker
  MCAMUI_TEXTCHECK) + tools/tests/ui_text_check.sh (SCALES="1 1.25 1.5", JOBS, SKIP_RUN; textcheck_scenes.txt = every op type's
  dialog pages, Lathe Face, ribbon tabs, managers). User decisions UI-2..UI-5 (formats/user_decisions.md): tool list columns fit
  their content, NOT DECODED / (ours) markers as colour + tooltip, Wine's Tahoma as the dialog font (viewer-qt/fonts), Quick View
  fit + status area scroll + edit tooltips; Mastercam's bitmap buttons (ECP, Tool Filter types, mcmsurf 999 / 1338) and decoded
  ShowWindow rules for stacked template controls (core_ui pf_hidden: 999, 242 / 1312, 240, 1802 / 1803, 2990 frames, 3205).
  UI-6: four controls nudged by a few px (user_decisions.md); Analyze's one-entity move decoded (32 / 64 px, window 68 px).
  Distinct problems 479 -> 2 at 100 / 125 / 150 % (op dialogs 215 -> 1); GUI suites as the baseline.
- Left (tools/tests/ui_text_known.txt): two values longer than their edit field (Misc Values name, a stock dimension) - they
  scroll with the whole value as tooltip, as UI-5 decided.
- The icon pack needs mcmsurfres bitmaps (tools/extract_icons.py now takes them): the user's ~/.config/mcamui/icons.pack was
  extended in place 2026-11-17 (backup icons.pack.bak-2026-11-17; old entries unchanged, 157 added).

## Where we are (2026-11-17, session master-cam-reverse lathe; master d90cee0, nothing pushed)
**master** (rebuilt 2026-11-16 22:19: build.sh + make -C viewer all) has, from this session:
- Lathe Face REGENERATION of stored ops (merge 63e02b1, L-1..L-4): viewer/lathe_face.c; millturn.mcam_8 op 1 = stored NCI 3/3 moves
  (tools/tests/lathe_face_test.sh). The LinuxCNC post refuses lathe ops (P4). Regenerated ops keep their Tplane label (L-4).
- Op comments as Mastercam (merge d90cee0): L-5 a regenerated / taken-over op keeps its stored comment; L-7 new ops start with an empty
  comment (our "<type> <n>" naming removed; Copy / Paste keeps the source's). Tree, dialog, post, project and saved part.
- New Lathe Face stage 1 (merge d90cee0, L-6 / L-8 / L-9): face from the stock + lathe stock (viewer/lathe_stock.c), engine only;
  test path `cam_cli PART lface - --lathe-defaults FILE`. formats/lathe.md 1-7, formats/face_dialog_fields.tsv.
- Lathe survey + Lathe Rough DECODE only (lathe.md 1-5): turnkernel.dll = CIMCO Edit's HSMWorks engine, not Mastercam's; mclathe.dll
  fully dumped (~/mastercam-re/work/lathe/dump/mclathe_all.c, Ghidra project work/lathe/gh); Rough's depth rule = stored NCI of 4 ops.

**2026-11-17 update:** int-l3 MERGED (c9a69f3) after regress3 rg_l3 (reboot mid-run; resumed from stockplanes): every suite = rg_l2, 7 lface_* scenes pass, fileop identical, 3D oracles identical; master rebuilt. Lathe toolpaths > Face is live. In flight on agent branches (not merged): L-12 spindle clamp + feed modes (worktree-agent-aaac568e0123b626e), Surfaces Extrude / Draft (worktree-agent-a4f5fddf971b042b2), Surfaces Set normals / Untrim / Remove Boundary (worktree-agent-aab335cc2a38c285b), Surfaces Trim x3 + Extend x2 (decode started).

**Chaining dialog + surfaces (user report 2026-11-17, branch worktree-agent-ab4658f97be21905b, not merged):** decoded in
formats/chaining.md "Surfaces in the Chaining dialog": Mastercam adds surfaces to the wireframe chaining mask ONLY in Single mode with
config 6026 "Allow surface edges in single mode" on (the user's config: n), never through the 3D radio; a click builds the surface edge
curve (build_edgecrv_on_surf) as a new entity and chains it. Ported: the mask / hover / click for untrimmed surfaces, the pick's (u, v) by
dist_to_surf (shading off; shading on refused), start values (msf_nurbs_splines 0), no undo record / kept on Cancel (as Mastercam).
User decisions CS-1..CS-4 (6026 was on; exactly as Mastercam; refuse rather than approximate; display list first).
**2026-11-17 (branch worktree-agent-aad5b6dedc7db6525):** the surface display-list points and the wireframe pick are PORTED
(viewer/mcsrfdl.c, core_ui/cui_srfdl.c; formats/chaining.md "The display-list points (port)"; oracle peloader mastercam.dll --sdl,
tools/tests/srfdl_oracle.sh: 2240 cases, 0 differ): real wireframe clicks on untrimmed surfaces chain the edge; the hover / general
selection of surfaces with shading off use the same pick. The shaded triangle cache's builder is found (make_surf_polygons(1, 1, ent,
gl_tolerance, 0, 0) = plm_mesh) and gl_Hit_Surfs decoded (chaining.md "The shading-on pick"), not ported. build_edgecrv_on_surf has an
oracle now (peloader mccore.dll --csurf ECV, tools/tests/ecv_oracle.sh: untrimmed 240 / 240 same; the 112 trimmed answers ready). Next:
**2026-11-17 (branch worktree-agent-a3488f3fce985a190, from ff7794a):** trimmed surfaces in the Chaining dialog are PORTED:
build_edgecrv_on_surf's trim walk (mcpartline.c ec_build_edgecrv_list, from the asm; ECV oracle 352 / 352 default folder, 928 / 928 on
Design + Mill Advanced Training + Handbook 2 / 3), dist_to_surf's FUN_1802de180 (mcsrfdl.c sdl_dist_trimmed; new oracle
tools/tests/dts_oracle.sh 1782 / 1782 and 5292 / 5292), eval_surf_const kinds 2 / 6 / 7 (srfdl oracle 1112 / 1112, kind 2/6/7 parts 0
differ); scenes ch_srf_trim_cap / _dts / _part / _part2 / _part_dts. formats/chaining.md "The trimmed walk (port)", "dist_to_surf's
trimmed branch (port)". Left:
- the shaded pick: **PORTED 2026-11-17** (branch worktree-agent-ae9afce64b219c18e; viewer/mcglhit.c, mccsurf.c cs_glhit_uv, cui_srfdl.c
  srfhit_pick; chaining.md "The shading-on pick (port)"): oracle = mastercam.dll's own gl_Hit_Surfs (tools/tests/glhit_oracle.sh) 6480 /
  6480 picks identical; shaded Single + 6026 clicks chain (ch_srf_shaded*). Left: the shading mesh (make_surf_polygons) of kinds 1 / 2 /
  4 / 6 / 7 / 8 and kind 10 on other bases, and PickShadedPolygonalMeshFromCursor (meshes: MCLoad*Mesh::Intersect in mccore): CS-20
  = not picked with shading on, a click says so - decode them to make them pickable. CS-21 ported (select_ent_univ's buffer = the hover's
  DAT_1809a0c28); left NOT DECODED: a wireframe candidate's parameter in that buffer (select_ent_univ l.540-716 t values).
- eval_surf_const of kind 4 (FUN_18042c080 / FUN_18042d9b0, dumped in decomp/chain_srfedge_const.c): no kind 4 surface in the corpus to
  check it; sdl_build refuses kind 4 - CS-6: refused until a kind 4 part exists (asked: windows_checklist.md section 9).
- CS-5..CS-8 answered (user_decisions.md); CS-5 done (the session node buffer, chaining.md "The node buffer (CS-5)").
- at integration: the msf_nurbs_splines writers (formats/chaining.md table: Curve Dynamic / Flowline / Slice / One Edge / Parting Line leave
  cfg_createNurbs; Constant Parameter / at Intersection restore) into one shared core_ui global.
- chaining a made curve the 2D core cannot (non-XY line, spline outside Engrave, NURBS): **PORTED 2026-11-17 (CS-7, branch
  worktree-agent-a120efd64780c3ea3)** for an operation's chaining - Mastercam's 0x8f mask (any plane), FUN_1801fa6e0 (C-plane radio),
  FUN_1801e8660 for every kind, the made edge curves chain (chaining.md "Curves outside the C-plane, splines and NURBS (CS-7)"). Contour
  takes 3D chains (the narrow 3D contour generated, rt_ctour3d_nurbs); other ops refuse tilted lines; Extrude / wireframe functions /
  Area keep the old rule with a message. Open: CS-9 (decode Extrude's / each wireframe function's handling), CS-10 (chain_area 3D),
  CS-11 (dist_to_arc / dist_to_spline / dist_to_nurbs's t), CS-12 (drawn lines' planar flag), CS-13 (Pocket & co. projection of 3D chains:
  chains_to_pock_bounds), Contour's 2D spline conversion (chain_to_ctour_array splines_ok 0). The bottom cap's NURBS not simplified to an
  arc: the DLL refuses it too (tools/tests/ecv_simp_oracle.sh: 318 / 318, 1655 / 1658 - the 3 = a harness flag), nothing to fix.
- RunLatheChainDlg's modes 1 / 2.

**2026-11-18 night:** int-s5 + int-s6 MERGED (cb16450, rebuilt): the solid save path (SOL-1..6: XT writer, McOpsFile history, Extrude on OCCT, colour / level at creation, exact booleans) and Constant Fillet (solids made here; SOL-F1..F5). regress3 rg_s6 clean (solid 2/127 = the known ex_save files; op_sld_contour_block / op_sld_pocket_hole re-blessed for the XT edge order; horiz / pencil source lists fixed). Next: named history replay of part solids (then Fillet / Cut / Boss on them), Boolean; open: windows_checklist.md 10 (open our solids in Mastercam); SOL-F5 smooth-edge test chosen by the main session (user may object).
**2026-11-18 evening:** int-s4 MERGED (a82b594, rebuilt): Shaded / Outline Shaded as two modes, the decoded shaded highlight in both, Outline Shaded trim loops only (S-15..S-17). regress3 rg_s4 = rg_s3 apart from the new / re-blessed srf_hl scenes. Running: the solid save path (SOL-1..4: XT writer + history + Extrude on OCCT), then Boolean / Fillet / Chamfer.
**2026-11-18 later:** int-s3 MERGED (fcd6eac, rebuilt): Surfaces > Offset + the shaded face highlight. regress3 rg_s3 = rg_s2 apart from the 9 new srf scenes (op lface_project.mcamui re-blessed for the moved ~/mastercam-re path). Next: S-15 (Shaded vs Outline Shaded as two modes).
**2026-11-18 (S-15, branch worktree-agent-a3a78fff9d3870e2a):** Shaded (render_cfg 3) and Outline Shaded (render_cfg 4) are two modes (formats/ui.md "Shaded vs Outline Shaded"): status bar / View tab / Alt+S (LastShadedMode) / Ctrl+T (only shaded, kept through wireframe) as decoded; Shaded draws no surface lines and no solid edges; Outline Shaded draws the surfaces' outline in config 5054 (was the entity colour). OPEN (question to the user): the decode does NOT support "Shaded: selected = list 1, Outline Shaded: recolour only" - the GrRender flags are set by the same paths in both modes (gl_Shade -> FUN_1802298b0 in every repaint sets Selected from the entity), and the only per-mode difference found there is MaintainGrRenderSurface's prop-set index (render_cfg 4: 1; 3: selected ? 2 : 0) into a one-entry UtPropSetList, which most likely changes nothing (an index past the list skips the prop push: GrPipe::Draw 0x10f42250 local_400 = 0) but is not fully decoded. If the gl_Shade path decides the frame, BOTH modes would show a selected surface with stipple list 1 (select colour lit + black 0xc4 dots) and a hovered one lit in its OWN colour (not config 5000) + black 0x40 + config 5000 0x80 dots - i.e. the pre-S-15 port (selected = recolour, hover pass 0 = 5000) would be wrong in both; both modes keep the pre-S-15 highlight until the user decides. To decode next: GrPipe::Draw 0x10f42250 after UtPropSetList::GetAt (what local_400 = 0 skips), and whether the face passes then still find materials 0xb / 0xc. Not ported: Dimmed / No Hidden (render_cfg 2, Alt+S's LastWireFlags choice), the start-up mode / persistence.
**2026-11-18 (S-16 / S-17, same branch):** DONE. The per-mode value (MaintainGrRenderSurface's SetVisibility index) selects one of the GrFace's four own prop sets (AddGrPropSets 0x18031da90 / SetGrRenderColor 0x180328970), which differ only in line colour / width; the face material is the same in all and a surface's GrFace has no edge data, so no pixel changes. Both shaded modes now show the gl_Shade-path highlight (selected: select colour + black 0xc4 dots; hovered: own colour + black 0x40 + config 5000 0x80 dots; hovered + selected: select colour + the hover passes). Outline Shaded draws the surfaces' display-list nodes (a trimmed surface: loops only, FUN_18013a5e0 l.75-85). The S-15 open question above is settled by this.
**2026-11-18 (bug, found by the Offset branch):** after Save As in the same session, a trimmed surface whose base was also made in that session no longer finds its base (the save renumbers the surface ids, the in-memory references are not updated); seen with Flat Boundary and Offset; reopening the saved file is fine.
**2026-11-18 (S-14):** Offset's ours-items to decode: the arrow drawing (shared with Change Normals), non-NURBS bases' stored normal, kind 8 header extents; Calculate (TessellateUntrimmed chord mode, GenGrid(chord) 0x180495000).

**2026-11-18 (open, PUB-2):** open-source stick-font fallback for Drafting (draft.c draft_fonts_load) and Create Letters (cui_wfmisc.c let_font_dir) when no Mastercam fonts are found: a reader for the bundled font (Hershey public-domain data or KiCad Newstroke CC0, licence file alongside), a message that it is a substitute; Mastercam's fonts always win. Then add it to tools/export_public.sh (it is the second allowed non-text file after tahoma.ttf).

**2026-11-18 (to check):** toolpath display vs selection - buckle-v12.mcx-9 with nothing selected draws every op's toolpath; with all 21 ops selected (selops:1..21) only ops 1-5's paths showed (offscreen MCP session). Check against Mastercam's rule for drawing selected / unselected ops before calling it a bug.

**2026-11-18:** int-s2 MERGED (bab7ab8, rebuilt): trim core matched to mccore (worktree-agent-a7c97f289ddaf4224), CS-7 chaining of curves outside the C-plane / splines / NURBS + CS-5 (worktree-agent-a120efd64780c3ea3), lathe feeds From defaults / User defined L-13..L-16a (worktree-agent-aaac568e0123b626e; From material still refused, L-16a search next). regress3 rg_s2 = rg_s1 apart from wf ccurve_tilt (-0 centre z, re-blessed); fileop 205 identical, 3D oracles identical. Still open: CS-9..CS-13 (uncommitted work in worktree-agent-a7b77ff864dc6a5be, 14 files), Trim to Plane shell, srfext oracle batch-order dependence. Stale branches with their work already in master: worktree-agent-a1b6bbd3194321d61, -a77f89982e67ec08e, -aa6a12a5b1ef82b0a, w-backup-cells.

**2026-11-17 later:** int-s1 MERGED (af7ab9e, rebuilt): UI text fit (ui_text_check.sh 479 -> 2), Surfaces Extrude / Draft / Set normals / Untrim / Remove Boundary / Extend (+ To plane via the Plane Selection dialog) / Trim to Surfaces, Ma trim core + march = DLL, Chaining surface edges (wireframe + shaded pick, trimmed edges). regress3 rg_s1 = rg_l3 apart from the branches' new scenes; oracles identical. Still on agent branches: lathe From-material converter search (worktree-agent-aaac568e0123b626e), face trimmer / BinaryPointsAtTol / pm_tess (worktree-agent-a7c97f289ddaf4224), NURBS / 3D-spline chaining CS-7 (worktree-agent-a120efd64780c3ea3, finished, not merged) + its follow-up CS-9..CS-13 (worktree-agent-a7b77ff864dc6a5be). Open: Trim to Plane command shell (build ported, greyed); Plane list 'Select plane icon' button bitmap (misc text-check overflow); srfext oracle 25/582 differ in a batch run vs 2 alone (order-dependent - look).

**In flight** (before 2026-11-17 update):
**In flight** (before 2026-11-17 update):
- New Lathe Face stage 2 = the UI (L-6 / L-8 / L-10..L-12): branch worktree-agent-ae2f66970f48c6949 (f09041f + a939804), test-merged as
  int-l3 (<scratch>, built), regress3 running -> rg_l3. Baseline to compare = int-l2's results
  (rg_l2 + reruns rg_l2b / rg_l2c): op 47, misc 5, solid 2, stockplanes 16, wf 1, srf / chain 0, cad 0 failed, fileop 0/205, units /
  horiz / pencil / blend / hst_rest / stockmodel pass, 3D oracles = rg_end. Stage 2 should add only its 7 lface_* op scenes (passing).
  If clean: `git merge --no-ff int-l3` into master + rebuild; then "Lathe toolpaths > Face" works in the app (lathe.mcam_8: right-click
  Toolpaths manager > Lathe toolpaths > Face > OK). Turning ribbon Face: only after Machine > Lathe.
- Stage 2 refuses / greys: T0202 (right spindle), a second Face, groups with group +0x11c0 set, Mill-Turn groups, point picks,
  sub-dialogs. Saves without stock-after copies (L-11). Part tools only (L-10).

**Next** (lathe, in order): spindle clamp + check after a spindle change (L-12, decode); the stock update writing a lathe op's stock
copies (op +0xb40..); the lathe .tooldb for "Show library tools" (L-10); millturn op 3's axis combination 1805 vs 1802 (unexplained);
then Lathe Rough regeneration (lathe.md 5 NOT DECODED list first). Windows reference parts wanted: windows_checklist.md item 7 (a new
Face from stock), plus Groove / Thread / Cutoff with NCI (L2). Open from before this session: N13-36 HST mesh flip (regress3 not run).

**Gotchas met this session** (integration via regress3, memory integration-regress3):
- A fresh integration tree needs `ln -s <main>/tools/tests/screenshots tools/tests/screenshots` (the test parts) BEFORE regress3; without
  it srf / op scenes that open BRACKET4 fail.
- tools/tests/horiz_test.sh and pencil_test.sh list viewer sources by hand: add every new viewer/*.c there (and Makefile, CMakeLists).
- A system package update during a run (2026-11-16 21:43, qt6-base / glibc / mesa) voids the GUI suites running then ("Bus error",
  libQt6OpenGLWidgets missing): rerun them; never bless output from that window.
- Targeted re-bless (SOLID_SCENES / SP_SCENES + BLESS=1) also rewrites a scene's other files (e.g. stale *.mcamui known differences):
  restore those, and remove anything written to ~/mastercam-re/regress/golden/<suite> that is not a saved part.
- Leftover worktrees of this session: .claude/worktrees/agent-a0d38af22da00ce86, agent-a945793fba8475c35, agent-a8b86f32bc2437725
  (merged), agent-ae2f66970f48c6949 (stage 2, in int-l3), and overnight/int_lface, int_l2 (merged), int_l3 - prune once int-l3 is merged.

## Plane Selection (get_plane_info, 2026-11-17; formats/plane_selection.md)
- DONE on its branch: the dialog 1085 + get_plane_info as a reusable module (viewer/core_ui/cui_plsel.c `psel_begin`, Qt
  viewer-qt/src/plseldlg.cpp), misc scenes psel_*. Not yet called by any command.
- Next (the callers' branches): Surfaces > Extend "To plane" (prompt mccadres 7457), Trim to Plane (worker +0xd0, reselect code 5), Curve
  Slice code 4 (cui_slice.c: replace the NOT PORTED message), Draft Surface - each calls `psel_begin` and disables its panel while
  `psel_open` (spec section 5 "Call interface").
- P-1..P-7 answered and done (user_decisions.md; second pass): stand-ins replaced by decoded behaviour, solid faces ported. Left
  (plane_selection.md 7): a clicked surface's (u, v) = the display-list pick FUN_18013cbc0 (port it once, shared with the chaining
  branch's CS-4), solid edges, expressions in the edits (UIEdit::Evaluate's evaluator), FUN_18031a100 (first-open position).

## TOP PRIORITY (N13-16, 2026-11-13): toolpath motion, fully ported (decode first, no stand-ins)
0. N13-30 / N13-33 HST drive mesh (formats/hst_linker.md 27; N13-33 pass 2026-11-14, branch worktree-agent-addd4274de5be6b70): behind
   MCAM_HST_MESH=1, OFF by default (user decision N13-33). Ported and DLL-matched (mccore.dll, peloader --msm): surface kinds 0, 1
   (parametric), 8 (offset) and 10 over them - corpus kind 0 / 10-over-0 919 + sample 2093, kind 1 / 10-over-1 560, kind 8 / 10-over-8 8
   identical (fill 161 + 3 + earlier 331 identical); refused: 2 degenerate kind 1 / 5 kind 0 (calc_surf_norm fails in the offset evaluation
   or the trim repair). Every surface drive of the corpus's HST ops is now ported; still refused with the switch on: the 10 solid-drive ops
   (Parasolid faceter, third-party), MULTIAXIS ROUGHING FALSE FLOOR op 6 (Cplane not Top), ops with record 0x1662 set (IFF path, none in
   the corpus). Decoded, NOT PORTED (no corpus HST drive needs them): kinds 2 / 4 / 6 / 7 (their eval_surf cases + eval_curve), the trim
   repair FUN_180586a50, polygon meshes (makePmeshStlModel -> make_stl_model / FUN_1803df720), the check model tmpmn5.emm (FUN_180220fe0's
   tmpmn5.tmp converted by mckernel FUN_1800f200c). Measured with the switch on (27.6):
   port (cut ref->ours p95 / ours->ref p95, off -> on): Waterline SWARF_METRIC 3 0.464 / 0.051 -> 0.457 / 0.052; SWARF 4 0.087 / 0.002 ->
   0.066 / 0.172 (the port's contact-angle normal from the mesh); MOLD BOTTLE 4 0.194 / 0.002 unchanged; Raster PARALLELSTEEP 5 (now the
   whole Mastercam mesh, its kind 1 surface included) seg_dist ref->ours p95 0.0006 -> 0.0007, ours->ref 0.0003 -> 0.0004; RESTPASSES 18
   0.0009 -> 0.0011, 0.0004 -> 0.0007. DLL harness (mchmm.dll on the op's mesh) vs the stored NCI, seg_dist p50 off -> on: SWARF_METRIC 3
   0.049 -> 0.0065 mm (levels = stored down to 13.238), SWARF 4 0.0025 -> 0.0002 in, MOLD BOTTLE 4 0.0037 -> 0.0003 in, PARALLELSTEEP 5
   p95 0.0003 both, max 0.0044 -> 0.0042, RESTPASSES 18 p95 0.0005 both. OptiRough TUTORIAL 7 op 5: off fails "nothing to cut" (as on master), on p95 0.124 / 0.121 mm, cut 21522 / 21444.
   N13-33a answered: N13-34 (off until the Waterline contact-angle filter uses Mastercam's weave contact).
   N13-34 pass (2026-11-14, branch worktree-agent-aa8e60cca3ef18e5b, formats/hst_linker.md 31): the weave contact word DECODED + PORTED (= the DLL vertex
   by vertex: MOLD BOTTLE op 4 38846 / 38846, SWARF op 4's weave 34620 / 34620), the window's degrees as mckernel writes them, the weave's fibre count
   (+ 1, FUN_180131690). Passes MOLD BOTTLE 98 = DLL (was 117); SWARF op 4 (bull) 62 off / 54 on vs 65: its passes are the Clipper offset filtered with
   the mesh normal (ours) - with the switch on its cut ours->ref p95 stays 0.171 (0.004 off), as on master. Default unchanged; question N13-34a (answered: N13-35, resume the Voronoi line).
   N13-35 pass (2026-11-14, branch worktree-agent-a722b6e517f8bf72d = W7 Voronoi + weave contact + master fb90997, formats/hst_linker.md 32,
   hst3d_port_plan.md 7 "W8"): bull-tool Waterline WIRED through mchmm's own chain (pockets FUN_18012b1e0, the filter's writer Eva +0x1440,
   FUN_180407c10's Voronoi offset + Corner rounding, the pass writer, the span rule). SWARF op 4 with the switch on = the DLL (65 / 65 passes,
   vertex for vertex); SWARF_METRIC op 3 with Corner rounding generates (27 / 28 passes = DLL). Left (refused, NOT DECODED): FUN_180407c10's
   fallback, WeaveSubdivision FUN_18040af10 (ball tools stay on the direct path), K+0x472's holder, XY != Z stock, the post-flush step of
   SWARF_METRIC 3's lowest level, the piece order. Question N13-35a (Waiting on the user).
   N13-36 / N13-37 pass (2026-11-14, branch worktree-agent-ae2b6e5dad11bd410, hst_linker.md 35, hst3d_port_plan.md 7 "W9"): FUN_180407c10's fallback
   + FUN_1803faee0's tolerance retry PORTED, = mchmm.dll (hmm_vorfix_oracle.sh 27 / 27, 100 / 100). The DLL itself never takes the fallback on
   gen_waterline: the port's pockets differ from the second level on - NEXT: decode FUN_1803fc9b0 (per-pocket weave spawning / "part N killed",
   hz l.1881-2240). N13-36 flip prepared but LEFT OFF (its regress3 not run - do that, explain, flip). Then FUN_18040af10, XY != Z stock, K+0x472.
1. M1 The general linker (replaces the SAFE link rule for every 3D op and 2D Area Mill): Waterline Linking Plan 0x141c (LinkByZLevel /
   LinkWaterlineDirectionStyle, FUN_180075b48), the Raster / Spiral / Radial linkers incl. spiral pass joining FUN_18037dc90.
   M1 status 2026-11-13 (branch worktree-agent-ae1376e089acdc726): DECODE ONLY, nothing ported, oracles unchanged - formats/hst_linker.md:
   mckernel's Link* parameters with record offsets (FUN_180075b48), the Waterline plan's defaults, the linker state, the order pipeline
   (clusters / FragOrderer / direction), the keep-down rule (XY gap < LinkMaxStayOnSurface + adjacency), the surface ramp length, the
   retract height rule and the vertical lead arcs (checked on the stored NCI). Stopped at: the adjacency graph, the connect path
   (FUN_1801d2bd0 / wander), the path height + ClearSurfaceBy's consumer, the lead gouge test FUN_18019a6a0, the helix first entry, and
   mccore's MCHSTKOTranslator (clearance / feed-plane rapids) - list of dumps to make in hst_linker.md 7. Raster / Spiral / Radial /
   FUN_18037dc90 / 2D Area Mill not started. Link baseline (tools/tests/link_stats.py): Waterline op 3 retracts ref 39 / ours 57,
   Raster PARALLELSTEEP op 5 ref 2 / ours 479.
   M1 second pass 2026-11-13 (branch worktree-agent-a0f9840255aff5e56): DECODE ONLY again, nothing ported, oracles unchanged -
   hst_linker.md 9: mccore's MCHSTKOTranslator adds no moves (every NCI rapid is a McHmm move of type 0; record / type map decoded); the
   retract polyline [end, (end, h), (start, h), start] (FUN_1801d62e0 / 1801d7210); LinkClearSurfaceBy enters the link height as the linker's
   layer offset (+0x1258 = ToolThicknessZ + ClearSurfaceBy + LinkMinThinningTolerance) added to the tool drop (FUN_1801cf500). Checked on op 3:
   the straight cutter drop + 25 is 30-70 below Mastercam's retract tops, so h is still NOT DECODED (tool assembly in the drop, or a non-straight
   connect). Raster op 5's links are stay-down loops of the connect FUN_1801d2bd0 (wander / smoothing NOT DECODED). Stopped at: the dumps listed
   in hst_linker.md 9.5 (LinPathRangeWorker FUN_180007050, the drop profile +0x208, FUN_1801d2110, the connect's layer arguments).
   M1 third pass 2026-11-13 (branch worktree-agent-a1c417d7c484c9a83): DECODE ONLY, nothing ported, oracles unchanged - hst_linker.md 10:
   the connect constructor's arguments from the disassembly (retract-height connect: mode 1, layer 1 for retract style 1, wander on; stay-down
   connect: layers 0 / 1, wander on, mode from the caller), the layer-1 drop profile (+0x2d0 = the cutter grown by ClearSurfaceBy +
   LinkMinThinningTolerance, default min(ClearSurfaceBy, 0.1); CToolDropPos with the side-profile pass), the TBB reduce (+0x30 = the highest
   dropped midpoint along the straight segment). Checked: stored Waterline retract tops = that inflated drop + ToolThicknessZ + 25.1 + exactly
   1.000 mm (37 links of SWARF_METRIC op 3 within 0.005, 17 of 18 of op 4 within 0.012); the 1.0 mm is NOT DECODED (candidates in hst_linker.md
   10.4), so (b) is not ported. Raster op 5's stay-down loop (a): the wander / FUN_1801cea60 / 1801dcc30 dumped, not read to the end; the
   connect mode for stay-down not traced. Next: hst_linker.md 10.6.
   M1 fourth pass 2026-11-13 (branch worktree-agent-a0949f106895e3a13): DECODE ONLY, nothing ported, oracles unchanged - hst_linker.md 11:
   the 1.0 mm is still NOT DECODED. Re-measured: a pure Z offset on the layer-1 node height (op 3 37/37 at 1.000 +- 0.009; op 4 17/18 at 1.000 +- 0.012
   once the profile's flat is R - Rc + XY - Z stock, its first link 0.67 = an inner maximum, likely the reduce's under-sampling). Decoded and
   ruled out: LinkerProtectThickness = LinkProtectThickness(MachiningPasses, ToolThicknessZ) = ToolThicknessZ here (built-in 1802a8120), so
   the protect model adds nothing; the linker's own tool LinkerToolThickness / LinkerToolCornerRadius (kind-7 defaults, 0.02 % narrower);
   ToolShaftRadius default (+ XY - Z stock); MinThinningTolerance / radial growth variants; end lifts; no other FP 1.0 in the height chain.
   Stay-down connect mode = the link kind (1 -> TBB reduce, 3 -> + surface-ramp extras). Next: hst_linker.md 11.3 (CToolDropPos +0x58 = 1
   side pass vs profile +0x90, the kind-7 toolbox in FUN_1802f2340, the reduce tolerances emitter +0x70 / +0x78), then the Raster wander.
   M1 fifth pass 2026-11-13 (branch worktree-agent-a8f7046d8bdac620d, hst_linker.md 12): FIRST PORT. (b) DECODED + PORTED for Raster: the
   stay-down link is the kind-3 surface-ramp connect (FUN_1801d2110): a cubic Bezier from the pass end to the next start with tangent handles
   SurfaceRampTangentFactor = MachStepover (Raster plan default FUN_1803adb40), subdivided by bisection in t (FUN_1801cd970 / FUN_1801cf230:
   SurfaceRampMaxDeviation 0.005 mm in XY, then StepMinHorizontalChange / StepMachTolerance from the plan's +0xcb8 block); 473 / 473 links of
   PARALLELSTEEP op 5 reproduced to 4.4e-7 in. gen_raster uses it when the XY gap < LinkMaxStayOnSurface and SurfaceRampStyle is 1; lifted /
   colliding links, SurfaceRampStyle 0 and retracts keep SAFE. PARALLELSTEEP op 5 retracts 479 -> 2 (ref 2), cut length 3154 -> 2553 (ref 2543),
   ours->ref p95 0.037 -> 0.034; RESTPASSES op 18 retracts 374 -> 2 (ref 2), length 1241 -> 985 (ref 978). (a) the Waterline 1.0 mm: still NOT
   DECODED (12.1: LinkWanderInitialOffset, the style-2 +1.0 gate, unit constants ruled out). Left: the Raster retract (style 2: LinkZClearance
   or path + 1.0 - its +0x30 drop is decoded, the leads / clearance moves are not ported), lifted links (FUN_1801d3a60 / 1801d4270 / 1801d3c20 /
   FUN_18019a310), the kind-1 straight connect with the wander, FragOrderer's order, then Waterline / Spiral / Radial.
   Checked vs master run rg_new5: fileop 205 / 205 identical; op_compare 54/635 vs master 47/635, the 7 new = the Raster scenes' point counts and
   posted NC (gen_raster, s4_check_trim(_stock) .txt / .nc, s4_pick_check / drive) - goldens re-blessed. NOT MERGED.
   M1 sixth pass 2026-11-13 (branch worktree-agent-aad7f1e8b6ac4cb3a, hst_linker.md 13): DECODE ONLY, nothing ported, code / oracles unchanged.
   Decoded: LinkRetractStyle 2 retracts go to LinkZClearance (all 138 stored style-2 links of 5 ops, Raster and Waterline); the lead arcs are
   linearised by mccore FUN_1809187f0 with n = ceil(sweep / acos((r - 0.45 cut tol) / r)) (checked on 4 ops); approach moves at the plunge rate,
   exit moves at the feed (measured); the lead test FUN_18019a6a0's arguments (layers 0, node base Z = centre Z - 1.1 R). Corpus: no Spiral /
   Radial op has stored NCI; no third style-1 Waterline op exists. Blockers: the lead rotation / fallback in FUN_18019e010 and its base Z
   FUN_18019ab50, FragOrderer FUN_1801b9d70, the lifted / kind-1 stay-down links. N13-21 (user, 2026-11-13): the lead test runs strictly on our
   own values exactly as the code does (no forced "tie = accepted"); an arc may come and go with rounding.
   M1 seventh pass 2026-11-13 (same branch, hst_linker.md 14): PORTED for Raster (LinkRetractStyle 2, clearance on + absolute): the retract link
   (XY gap >= LinkMaxStayOnSurface), the first approach and the final exit with the vertical leads (slope-following arcs, lead test FUN_18019a6a0
   strict on our values, mccore's segment count), LinkRampHeightOffset moves, LinkZClearance; blocked / undecided leads keep SAFE (the rotation /
   straight fallback of FUN_18019e010 is NOT DECODED). PARALLELSTEEP 5 / RESTPASSES 18: final exit ported, first approach SAFE (its flat-start tie
   comes out "blocked" on our values); no corpus op has a ported inter-pass retract (op 19's passes are not ported). FragOrderer: Raster's
   LinkSimpleOrder = (0x16c0 == 0) is false on the corpus ops, the graph order is NOT DECODED; FUN_1801b9d70 (nearest by squared 3D distance) decoded.
   M1 eighth pass 2026-11-13 (same branch, hst_linker.md 14.5): DECODE ONLY. Raster passes carry no adjacency edges (the grid plan writes only
   (pass, -1) / (-1, pass); real edges come from EvaAxiallyOffsetPassesPlan's FUN_18031f660); the next-pass chain FUN_1801b90d0 -> FUN_1801bfbd0 /
   FUN_1801c0320 -> FUN_1801b9820 (first pass: nearest to the start point, then the last candidate ending within LinkMaxStayOnSurface of its start;
   later: nearest start to the previous end) decoded. Left before porting the order: FUN_1801bff00, FUN_1801bf440, Raster's clusters, the +0x1107
   stack, LinkStartX / Y, the zigzag direction pass FUN_1803a1ec0 without edges. Lead fallback / FUN_1801cea60: not reached (budget).
   M1 tenth pass 2026-11-13 (branch worktree-agent-ab740920dbea41a1b, hst_linker.md 16): mchmm.dll RUNS under tools/peloader (--hmm: mchmm +
   tbb + the real VC120 runtime / mfc120u, cooperative threads; no licence code in mchmm, none touched). The whole engine runs our macros
   (CreatePlan statements as mckernel's CMacroWriter writes them) and returns passes and linked toolpaths through the CMCHstExtractTag messages.
   tools/hmm_oracle.py + MCAM_HMM_DUMP (gen_raster) run it on the port's mesh and parameters; with the port's 18 parameters the DLL does not yet
   reproduce PARALLELSTEEP op 5 (retracts 597 vs 2): next = write the full Raster / linking macro as mckernel does (FUN_180071314 /
   FUN_180075b48), then use the DLL for 15.5 (order, blocked leads, Waterline exit). Measured with it: Raster direction groups = raster lines
   (every second line reversed, passes of the line together); the first pass depends on LinkStartX / Y but is not the plain nearest start.
   Port unchanged.
   M1 eleventh pass 2026-11-13 (branch worktree-agent-a38204f05adf8e30c, hst_linker.md 17): mckernel's macro writer for the DLL oracle
   (tools/hmm_macro.py: FUN_18007d7c8 / FUN_180071314 / FUN_180075b48 on the op record; K -> record map from mccore FUN_1809232b0 / FUN_180923f60 /
   FUN_180923d30; LinkStartX / Y = -9999 without a start point, mcmsurf FUN_1800199d0 - DECODED; containment as Create2DBoundary). The DLL
   REPRODUCES PARALLELSTEEP op 5 (stored vertices within 0.0044 in of the DLL path, retracts 2 / 2) and RESTPASSES op 18 (within 0.0019);
   Waterline TUTORIAL 2 SWARF op 4 close (retracts 42 / 42, p95 0.013 in); SWARF_METRIC op 3 NOT reproduced (loops 0.3 mm out, lowest 4 levels
   full loops, retracts 2 vs 39: input mesh / containment suspected). Port vs DLL: op 5 links agree except the first approach (the port's pass
   starts 0.013 farther out and 0.0003 below the floor, so its strict lead test ties - a passes-plan difference); op 18's last line in another
   order / direction. Not written: tool body (Cnc::Tool +0x98), rest material (op 19). Next: hst_linker.md 17.5. Port code unchanged.
   M1 twelfth pass 2026-11-13 (branch worktree-agent-a97d7ce5e70b70a5f, hst_linker.md 18): the DLL's passes feed the port's linker
   (hmm_oracle.py --passes, MCAM_HMM_PASSES, tools/tests/pass_cmp.py / link_cmp.py). PORTED for Raster: lines on the ConstraintBoundaries' range
   (the Bounding Box plan's box without a containment: RESTPASSES 18's 0.005 in shift and its extra line), pass ends bisected to the tight Z
   window (FUN_180361bd0 second loop: PARALLELSTEEP 5's first pass 0.013 out / 0.0003 low), the direction pass FUN_1803a2750 on the line key,
   LinkRFMinPassLength. Port passes = DLL passes (478 / 372, ends within 0.00008 in); the port's linker on the DLL's passes = the DLL's links
   (477 / 371 stay-down links + exit) except the first approach (strict lead tie by 2 ulps, N13-21). Op 18 port vs NCI p95 0.0070 -> 0.0009,
   op 5 ours->ref max 0.015 -> 0.004. Pass order PORTED: FUN_1801b9820 (first pass: start point + LinkMaxStayOnSurface walk; then the
   nearest start) over the passes FUN_1801bf440 lets through, the prerequisites MEASURED with the DLL (the overlapping passes of the previous
   raster line; the +0x340 writer and the overlap margin NOT DECODED) - 5 synthetic DLL orders + ops 5 / 18 reproduced. Waterline op 3: the DLL macro now carries the check plans (0x141f,
   mk l.93590); remaining DLL-vs-NCI difference = level Z pushes on our mesh + check method 0 in the record. Next: hst_linker.md 18.7.
   M1 thirteenth pass 2026-11-14 (branch worktree-agent-ac5d0720f4db84798, hst_linker.md 19): HARD-RULE FIX: the pass-order prerequisites
   are DECODED and ported (the measured "previous line, closed intervals" rule and its guessed margin are gone): the grid plan's virtual +0x20
   FUN_180013ab0 -> FUN_1802ed2c0 joins consecutive raster lines (edge i -> i-1); FUN_1801ba430 / FUN_1801ba970 (LinkHeadNorthwards true ->
   FUN_180184200) + FUN_1801c98a0 give each pass the overlapping passes of the previous line holding passes; the overlap test FUN_1801c2300 /
   FUN_18027a8d0 (XY, ends of the pass with fewer points, alternating nearest points) at LinkORAdjacentLevelOverlap = 1.5 x MachStepover.
   Checked: 5 / 5 synthetic DLL orders (the old rule 1 / 5), ops 5 / 18 order equal. N13-27 done: the Raster linker runs in mm. NEW finding:
   for Raster (no horizontal leads) Mastercam does not run FUN_18019e010 / the lead test FUN_18019a6a0 at all (traced: 0 calls); the approach /
   exit / retract leads come from the connect path FUN_1801db250 / FUN_1801dab60, whose collision steps are NOT DECODED - per user decision
   N13-29 every Raster lead falls back to SAFE until they are decoded (a drop guard briefly on the branch was removed). Ops 5 / 18: first
   approach / exit SAFE again (port vs NCI max 0.0750). The connect path's collision engine is traced and mapped
   (hst_linker.md 19.6: FUN_1801cece0 / FUN_1801ce270 / FUN_1801cf7b0 ...), NOT DECODED yet - next step. New tool: peloader --hmm HMM_TRACE (call tracer). Open: 19.5.
   M1 fourteenth pass 2026-11-14 (branch worktree-agent-aa1d21ab8b233e49a, hst_linker.md 20): N13-29 step 4 - the Raster lead connect's collision
   handling DECODED + ported (c3_lc_*): FUN_1801d7450's top drop, FUN_1801cece0's arc refinement (FUN_1801ce270 -> FUN_180252d90 step criteria +
   FUN_180252a70 from the disassembly, the 90 deg rule, FUN_1801cc840 splits at FUN_180060d70 midpoints, each dropped by FUN_1801cf7b0), the merge
   FUN_1801e0ee0 / FUN_1801cfda0 and FUN_1801d9330. = DLL node by node on ops 5 / 18 (62 / 62 calls each, 1e-13 mm). The first approach and the
   final exit leave SAFE when no node rises (N13-29's condition met for them); a rise (FUN_1801cc840's lifted branch, FUN_1801d7450's node removal,
   FUN_1801d87b0 / 1801d76a0), a retract between passes (FUN_1801a1d70, not traced) and stock to leave (the linker's tool profile) keep SAFE.
   Ops 5 / 18 vs NCI max 0.1086 / 0.1216 -> 0.0078 / 0.0063; link_cmp 479 / 373 equal (0.00015); stay-down 477 / 371 unchanged. Open: 20.4
   (the rise branches, the stay-down guard -> the same engine, retract leads). HMM_TRACE :l / :r / :R.
   M1 fourteenth pass 2026-11-14 (branch worktree-agent-a819e97273f4ddcb9, hst_linker.md 21): WATERLINE. DLL vs stored NCI: SWARF op 4
   p95 0.013 / 0.014, retracts 42 / 42; MOLD BOTTLE op 4 p95 0.013 / 0.024, retracts 71 / 71; SWARF_METRIC op 3: the level push is DECODED -
   CZInflexions takes every vertex / near-flat edge height of the mesh (FUN_1804a2270 -> FUN_1804a2040 / FUN_1804a20b0), our tessellation
   has them in the windows (the port left them out; N13-30: now applied); with the snap off (hmm_macro HMM_NO_INFLEXIONS, testing) the DLL's
   levels = the stored ones (p50 0.049 -> 0.0036). The Waterline pass order DECODED (region graph FUN_180400db0 / FUN_1803fe090 probe
   containment, FragOrderer lists at 2.2 x ToolShaftRadius, next pass, FUN_1801b9490 prerequisite resolution, closed-pass entry with the
   surface-ramp trim) and checked with the DLL (model 65 / 65 op 4, 98 / 98 MOLD BOTTLE) - PORTED (wl_link; ops without checks, LinkByZLevel
   false, direction style 0). Rapid XY op 4 81.5 -> 63.9 (ref 51.2), MOLD BOTTLE 204 -> 105 (ref 84). Links still SAFE. Open: 21.5.
   M1 fifteenth pass 2026-11-14 (same branch, hst_linker.md 22): N13-30 applied (Waterline levels with FUN_1804a2270's edges / vertices: the
   port's levels = the DLL's on our mesh, mean 0.0050 / 0.0004 / 0.0001 on ops 3 / 4 / MOLD BOTTLE; seg p50 vs NCI 0.0047 -> 0.059 / 0.0005 ->
   0.0025 / 0.0004 -> 0.0039 until the tessellation task). MCAM_HMM_PASSES + MCAM_HMM_WLGRAPH: the port's linker on the DLL's input orders
   65 / 65 and 98 / 98 as the DLL. Open: hst_linker.md 22.3 (link_cmp for Waterline, the passes-plan splits, the links).
   M1 fifteenth pass 2026-11-14 (branch worktree-agent-a148b2c2960cf643b, hst_linker.md 23): the Raster stay-down links run the DLL's connect
   engine (FUN_1801cf230 = FUN_1801ce270 on nodes + curve points, FUN_1801cd970's drop of each new node, kind 1 = the mode-1 connect, lifted nodes
   kept up to SurfaceRampMaxVerticalChange; our chord guard removed): ops 5 / 18 22900 / 11130 calls = mchmm.dll, toolpaths unchanged; the retract
   between passes (FUN_1801a1d70 -> FUN_1801d69f0: exit / approach lead connects + FUN_1801990c0's height) PORTED (our 1 mm guard removed); the
   linker's tool with stock (LinkerToolThickness, FUN_1802f5270) measured + ported, also the narrowed stockless profile and the linker's own drop;
   synthetic cases shared with the DLL (MCAM_MESH_STL / MCAM_TC): valley 3572 / 3572 stay-down calls, 366 / 366 lead, 344 / 344 height calls,
   80 / 84 links equal (4 within 0.0001). Check surfaces: the protect model FUN_1803a46d0 is NOT PORTED -> every link SAFE. Open (23.7): the lead
   rise branches FUN_1801d8240 / 1801d7b00 before the decoded fallback FUN_1801d8630 (wall case traced), the stay-down rise retry FUN_18019a310,
   the kind-1 wander FUN_1801dde40, the protect drop.
   M1 sixteenth pass 2026-11-14 (branch worktree-agent-ade6ca1381ef88939, hst_linker.md 24): the check surfaces' protect drop FUN_1803a46d0
   PORTED (linker +0x7c0 = the 0x141f plan's check model, LinkerProtectThickness = OverThickness = the check stock, the protect profile = the
   linker's tool grown by P - ToolThicknessZ, FUN_1803a3f50 / 1803a4130): ops with checks no longer keep every Raster link SAFE; synthetic
   valley + check strip (MCAM_MESH_STL + new MCAM_CHK_STL): 176 / 176 stay-down links call by call = mchmm.dll (also with check stock / Z stock),
   flat + strip: FUN_1803a46d0 4591 / 4591, every link equal. The stay-down rise branch of FUN_180199220: the pass-extension tries FUN_18019a310,
   the mode-1 retry and the hand-off to the retract PORTED (2361 / 2361 calls; 31 links retried straight, 22 retracts as the DLL); kept SAFE: both
   extensions taken (the extension loop's connect / moves NOT DECODED, the 80 deg valley's 4 links), a split retry. 3 Raster check scenes re-blessed,
   fileop 0 / 205. Open (24.4): the lead attempts FUN_1801d8240 / 1801d7b00 before the vertical fallback (every blocked lead of the check cases),
   the extension loop, the kind-1 wander FUN_1801dde40.
   M1 seventeenth pass 2026-11-14 (branch worktree-agent-a612a1432f1dc328a, hst_linker.md 25): the stay-down extension loop of FUN_180199220
   (both pass extensions taken) PORTED: the new connect between the extension ends on the extension frags' tangents, repeated while it rises, the
   extension moves + connect emitted, else retry / retract; = mchmm.dll call by call on 9 synthetic valleys (v80 2432 / 2432 ... 89 deg 8532 /
   8532; 12 links emitted with their extensions on hl / hm / hn, every link equal). The lead attempts before the vertical fallback TRACED (23.2:
   FUN_1801a1d70 calls FUN_1801d69f0 twice; FindBetterArc FUN_1801d8240 fails, FUN_1801d7b00 stores the trim, LinkMaxAllowedTrimming / pass length
   decide the second call's FUN_1801d8630 vertical leads) and decompiled (x11), NOT PORTED: they need the arc fitting FUN_180171b50 + helpers and
   FUN_1801dbf80 (the c45 family's 26 / 29 / 25 retracts stay SAFE). The wander FUN_1801dde40 decompiled, no case (23.3). Regression vs a fresh
   master build: fileop 0 / 205; op_compare 7 Raster files changed (the flat-loft scenes' edge links now extended, 419 / 419 calls = DLL on the
   scene's mesh) and re-blessed, then 47 / 636 = master; checktrim ok. Open: 23.5.
   M1 seventeenth pass 2026-11-14 (branch worktree-agent-a19e9e70bbe455091, hst_linker.md 26): WATERLINE SURFACE RAMP DECODED + PORTED
   (wl_ramp_link): FUN_1801c3dd0's pieces (P's extension, N's ramp piece), the connect curve FUN_1801dd430 (the pieces blended with LinkSurfaceRamp-
   Smooth's smoothstep), the Raster reduce on it (SurfaceRampMaxDeviation 0.0005 mm) and the closed-closed wander hull FUN_1801dde40 / 1801de1f0 /
   1801e0d70. SWARF op 4 on the DLL's passes: 7 ramps emitted = mchmm.dll node by node (1e-5 mm); 14 keep SAFE (a lifted node: the node wander
   FUN_1801d4990, NOT PORTED), 1 (open -> closed branch, NOT PORTED). Own passes: retracts op 4 71 -> 53 (ref 42), MOLD BOTTLE 118 -> 105 (ref 71).
   The 40 retracts (FUN_1801a1d70 with horizontal leads: FUN_1801a1240's side from the toolpath grid, the 10 deg helix lead-in, the lead retries)
   are mapped, NOT DECODED: SAFE. Passes-plan difference (weave contact word): not started. Open: hst_linker.md 26.4.
   M1 eighteenth pass 2026-11-14 (branch worktree-agent-a394c48013b4bcc61, hst_linker.md 28): EVERY WATERLINE SURFACE RAMP = THE DLL.
   PORTED: the node wander FUN_1801d4990 / 1801d4840 (c3_sd_wander; connect +0x80 = the emitter box's min Z = the passes' lowest Z, traced)
   and the mode-3 post-checks FUN_1801d3a60 / 1801d4270 / 1801d3c20 / 1801d39e0 (c3_sd_wcheck), the open -> closed ramp (FUN_18027b580: the open
   pass's closed toolpath item). On the DLL's passes: SWARF op 4 22 / 22 ramps node for node (was 7), MOLD BOTTLE 27 / 27 closed -> closed (+ the
   first, whose test item is 1e-4 mm off). Own passes: ramps op 4 21 (0 SAFE), MOLD BOTTLE 21 (0 SAFE); retracts op 4 53 -> 50 (ref 42),
   MOLD BOTTLE 105 -> 97 (ref 71). Still SAFE: the retracts with leads (FUN_1801a1240's side needs the passes plan's toolpath grid data GridC
   0x1e / 0x1f - not in the port), approach / exit, style 1's +1.0, the passes-plan difference. Open: hst_linker.md 28.6.
   M1 twenty-first pass 2026-11-14 (branch worktree-agent-a9c0ae11b074b90d4, hst_linker.md 34): THE BLOCKED WATERLINE LEADS PORTED
   (wl_retract = FUN_18019e010's rounds, read from the disassembly + HMM_TRACE: the out / in checks' blocked results with the step-0 retry, the
   rotation of the lead-out tangent point (1/6 of the angle, 6 rounds), the ramp lead-in FUN_180176760 along the next pass's item, the lead-out
   fallback FUN_18019d5d0 -> FUN_1801dab60's vertical lead + offset). DLL passes: SWARF op 4 links = DLL 37 -> 49 / 57 (retracts emitted 14 ->
   27, every comparable one = the DLL), MOLD BOTTLE 47 -> 69 / 90 (retracts 5 / 8 / 21 and the final exit included); ramps unchanged (22 / 22, 27 / 28). Own passes
   vs NCI: op 4 ref->ours p95 0.0138 -> 0.0110 (max 0.50 -> 0.17), MOLD BOTTLE 0.0239 -> 0.0132 (max 0.50 -> 0.21); ours->ref up (the offset
   line the NCI drops, the extra passes). DECODED: the parent map FUN_1801af3d0 reads = FUN_180182e20's CFragAdjacency map (direct edges with
   toolpaths, = wl_vtop). Also merged M1s (Raster) - its synthetic cases byte-identical. Open: hst_linker.md 34.4 (FUN_18019db50,
   nonzero retry steps, the NCI's offset line, op 3's +1.0).
   STOPPED (session wound down) after merging master c59ce7c: build clean, DLL-pass checks and the Raster synthetic cases re-run equal; the
   regress3 run of the merged tree was not completed (the pre-merge run of 159de30 = master rg_new17 + gen_waterline). Op 3's +1.0 and the
   NCI exit offset line not started (hst_linker.md 34.4).
   M1 twentieth pass 2026-11-14 (branch worktree-agent-a7ddf1635849485e0, hst_linker.md 30): THE WATERLINE RETRACT WITH HORIZONTAL LEADS PORTED
   for the clean path (viewer/cam3d_finish.c wl_retract): FUN_180176540's side / centre on the region loops, FUN_18019e010's helix top / start,
   tangent point, the checks FUN_18019bba0 / FUN_18019c7c0 / FUN_1801d3530 (read from the disassembly with block-coverage traces, HMM_TRACE :n),
   the vertical leads + FUN_18019a6a0, the writers. On the DLL's passes every clean DLL link equals the DLL (SWARF op 4: approach + 14 retracts
   15 / 15; MOLD BOTTLE op 4: approach + 18 retracts 19 / 19, link_cmp <= 0.0005 in), the blocked ones keep SAFE. Own passes vs NCI: op 4
   ref->ours p95 0.103 -> 0.014 in, plunges at feed 49 -> 69 (ref 69); MOLD BOTTLE ref->ours p95 0.118 -> 0.024; ours->ref p95 0.004 -> 0.012
   (leads on the port's extra upper-level passes, 22.2c). Left (30.4): the blocked lead-out rotation + fallback FUN_18019d5d0 (retracts 16-40,
   exit), the lead-in retries / FUN_180176760 (retract 15), local_fe1 with n = 0, the NCI's missing exit offset line, op 3's +1.0.
   M1 nineteenth pass 2026-11-14 (branch worktree-agent-a2083b8a0dffed7d6, hst_linker.md 29): DECODE ONLY (harness: HMM_TRACE `:g`), the port
   and the oracles unchanged, the Waterline retracts still SAFE. DECODED: FUN_1801a1240 returns 1 for Waterline but its FUN_180176540 (Ghidra
   lost the arguments) writes the lead circle's centre and side: left normal first, kept when MeasureInterior2D(C) >= R - 0.012 inside the
   ToolBoxSet of the toolpath's GridC 0x1e = the region's full (unfiltered, arc-fitted) Waterline loop at that level (traced: centres = the
   DLL's arc centres); the helix top (max(z + RampHeightOffset, FUN_1801af3d0 = the parent level's Z, or RampHeightOffset + plan +0x1240 + z),
   capped by LinkZClearance +0x1e8), the helix start FUN_18019afc0, the tangent point FUN_18019b180, the arc / helix writers FUN_18019b590 /
   FUN_18011a350. Op 4's links classified: approach + retracts 1-14 = the clean path, retract 15 mixed, 16-40 + exit = blocked lead-out
   (FUN_18019d5d0 / FUN_180176760, not read). Style 1's +1.0: mckernel's linking block = hmm_macro's (no missing input); the two-macro form
   spins in the harness; stock written into the linking plan changes nothing - still open. Item 4 (passes-plan difference) moved to another
   agent (N13-34). Next: hst_linker.md 29.4.
   M1 eighteenth pass 2026-11-14 (branch worktree-agent-aec0e44b919baf8b9, hst_linker.md 28): the retract leads' attempts PORTED (viewer/
   cam3d_hstlink.c: FUN_1801dbf80's pass nodes, FindBetterArc FUN_1801d8240 with FUN_180171b50 / 180172020 / 1801725b0 / 180172950 and the
   FUN_1801d8b30 check, FUN_1801d7b00, the trim fit test and second FUN_1801d69f0 call of FUN_1801a1d70, the vertical lead FUN_1801d8630): c45
   400 / 400 arc fits = mchmm.dll, its 26 lead-blocked retracts now equal the DLL (c45s / c45z likewise). The wander FUN_1801dde40 PORTED for
   mode-1 connect paths (kind-1 links, the mode-1 retry, the extension loop) with FUN_180007eb0's 0x80 marks / FUN_1801d0240: ridge cases
   w1 / w2 / w3 every link = DLL. Regression (regress3 vs master rg_new16): all suites = master except op +4 s4_check_trim files (vertical leads + wander, re-blessed, 128 / 136 fits = DLL on the scene mesh), fileop 0 / 205, 3D oracles identical. Not ported (SAFE): the pass trimming of a fitting lead (FUN_1801a44a0 / 1801a47b0), LinkTrimmingStyle 2, the
   rising first approach / final exit, the wander's arc branch. Open: 28.4.
   M1 twentieth pass 2026-11-14 (branch worktree-agent-a054bf162af18f8d9, hst_linker.md 33): RASTER. PORTED, call for call = mchmm.dll on the
   synthetic cases: (1) the first approach / final exit leads that rise (FUN_180194140 / FUN_180197b80: attempts, fit test, second call,
   FUN_1801d8630), and an arc refinement that rises takes the same FUN_1801d76a0 path (FUN_1801cc840's rise -> FUN_1801d9330 = 1; in
   FindBetterArc a bad fit); (2) the pass trimming of a fitting trim (FUN_1801a44a0 / FUN_1801a47b0 -> FUN_180170f00 / FUN_1801713f0, the fitted
   arc [T', C', Q] or FUN_1801d7b00's Q) for retracts, first approach and final exit; (3) LinkTrimmingStyle 2 (FUN_1801db570 / 1801daf90 with
   param_4: FUN_1801d7b00 only). Checked: s4_check_trim (DLL passes) 136 / 136 fits, 17 / 17 vertical leads; e1 465 / 465 fits, 4 / 4 trims; e2 250 /
   250, 50 / 50 trimming arcs exact; e3 277 / 277, 56 / 56 (first approach trimmed); s4 at 10 mm 85 / 85, 17 / 17 (final exit, one-node leads);
   e4 / e5 (style 2) 70 / 70, 64 / 64 and 72 / 72, 14 / 14, 50 / 50 vertical leads, e5 180 / 180 links equal. Other synthetic cases unchanged.
   Regression (regress3 of 8604280 = this branch after merging master 4249f6a, against the orchestrator's master run rg_new17): wf 1 / 524, srf 0 / 37, chain 0 / 63, solid 2 / 83, cad 0 failed, misc 5 / 90, stockplanes 16 / 125, op 47 / 637 - every DIFF list = master's; the normalised op outputs = master's except the 4 s4_check_trim .txt / .nc files of the M1r branch (its re-blessed goldens, unchanged here: their own passes have no rising first approach / final exit and no fitting trim at 1 mm); fileop 0 / 205; unit tests = master; 3D oracles identical; checktrim_test all ok (stops 35 / 65, 34 / 66); PARALLELSTEEP op 5 / RESTPASSES op 18 byte-identical to M1r. No golden re-blessed.
   Not done: (4) the wander's arc branch FUN_1801de3a0 - no Raster path reaches it (the mode-1 connects that wander hold line nodes only; the
   lead connects run without the wander flag): Waterline's mode-3 / arc paths, left to the Waterline linker work. Open: hst_linker.md 33.6
   (FUN_1802810a0's +0x30 / +0x38 array, the closed-pass count, the neighbour frag, arc nodes in the walk).
   M1 ninth pass 2026-11-13 (branch worktree-agent-a955d3fd54b685b7a, hst_linker.md 15): DECODE ONLY, code / oracles unchanged. Raster = plan
   kind 1: its direction pass is FUN_1803a2750 (groups of consecutive passes with equal frag +0x70, odd groups reversed with the group order
   reversed), not FUN_1803a1ec0; one cluster (ClusterEnablePreOrder false); with no graph edges the group (FUN_1801bff00) is empty and the
   filter (FUN_1801bf440 -> FUN_1801be020) never blocks, so the order is the nearest-start chain of FUN_1801b9820 over all passes, passes under
   LinkRFMinPassLength 0.02 mm dropped (FUN_1801c48d0). Not ported: frag +0x70 of a Raster pass, the start point object (K + 0x10), the entity
   key order are NOT DECODED. Blocked leads: FUN_18019e010's retry / rotation are no-ops for Raster (no horizontal lead) and a blocked lead
   goes to FUN_18019db50 / FUN_18019d5d0 (a lifted connect seeded from the pass, FUN_1801dbf80 / 1801db570 / 1801db820 decoded); its
   FUN_1801cece0 / 1801e0ee0 / 1801d76a0 / curl branch and FUN_1801ce270 (also needed by the node removal FUN_1801cea60, otherwise decoded)
   are NOT DECODED, so blocked leads keep SAFE. Waterline exit offset move: not the retract style (FUN_1801dab60), still open.
2. M2 Dynamic Mill guided planner FUN_1800f1080 (484 functions / 39.9k lines with shared kernels; call-tree map in mcce_port_plan.md
   9 "M2 stage 1"): every stage is ported (MCCE_CTOURGUIDE=0 MCCE_GUIDED=1; the default D-3 branch never calls it, in the DLL
   either). 2026-11-13 (branch worktree-agent-a2db03d796f74efe5, stage 2): FUN_18012f2b0, FUN_1801270e0, FUN_1800a9490,
   FUN_180146c10 ported; outputs byte-identical. 2026-11-13 M3 (branch worktree-agent-ae8225a36b873ef64, mcce_port_plan.md 9
   "M3"): the region boolean FUN_180153b30 / FUN_18015b2e0 / SoPockUnionAr / core FUN_180162010 ported, 262/262 unit cases
   DLL-identical (tools/tests/sobool_oracle.sh), wired into the planner (gp oracle: cuts 0 -> 78 equal + 20 box-only,
   traps 100/101, channel 96, search / post / rotate_back 101/101). Evidence (call counters, peloader MCCE_COUNT): P+0x261
   is the registry's bUseCtourGuide (not in the op; default 0, absent in the user's hive), so mcce.dll's own default routes
   every link of ops 3 / 10 / 32 / 36 / 42 / v12 22 / v9 51 through FUN_1800f1080 (M2 stage 1's "never calls" was about our
   D-3 default only, corrected). Planner path vs the X9 NCIs: 0.202 / 0.705 (default ctour-guide 0.111 / 0.643).
   2026-11-13 M2 links (branch worktree-agent-ad10432c899f82051, mcce_port_plan.md 9 "M2 links"): isect() = FUN_180134270 (DLL-exact),
   the link smoothing's MCCECalcContourOffset (FUN_180048840, flags 0x101) = the mcce_ctour.c port, its two SoPockUnionAr paths ported:
   ct link oracle 108 -> 142/146 (left: 4 both-fail calls, stale box only), new coffct_oracle.sh 43063/43063 passes exact, coff oracle
   syn 18000/18000; planner channel equal on same input. NCI 0.111 / 0.643 unchanged (8 ops slightly better, none worse).
   2026-11-13 N13-17: the target stays the contour guide (D-3); the guided planner stays behind MCCE_GUIDED.
   MERGED 8479656 (rg_new5 vs rg_new6: only the Dynamic ops with routed links change; 37 ops p95 median 0.111 -> 0.110, worst 0.643;
   14 Dynamic / Dynamic Contour / Peel op scenes re-blessed b8072b0). M4 cleanups merged with it.
   LEFT: the NCI gap is upstream - the link region sets S1/S3/S4 need MCCECalcOffset (FUN_1800d48b0 frame ring, FUN_180152cb0 for
   several outer-derived rings); planner gp.23 of buckle-v10 10 (cut circles' entity boxes before the 1e-4 shrink, FUN_1800e9c60).
   2026-11-13 M2 offset (branch worktree-agent-a8fe68dcaeede06c4, mcce_port_plan.md 9 "M2 offset"): MCCECalcOffset ported DLL-exact
   (viewer/mcce_offset.inc: nest groups, frame ring FUN_1800d48b0, FUN_1800d3d30 + FUN_180152cb0 / FUN_18015b1c0, tail FUN_1800d3b60, all
   FUN_1800da1c0 flag bits); unit oracle tools/tests/off_oracle.sh (peloader --off, MCCE_OFFDUMP from --dyn) 1411 / 1411 identical (200
   synthetic, 1211 of the DLL's own calls at 21 sites); S1 / S3 / S4 through it by default (MCCE_DLLOFF=0 old), FUN_180050410 corner skip on
   S1 ported. Dynamic NCI 37 ops: median 0.110 -> 0.075 (gap in the distribution), worst 0.643 =; 9 better / 11 worse / 17 same (question
   under "Waiting on the user"); mcce.dll on our input: v12 10 / 17 / 23 records agree to 443 / 262 / 540 (was 0); ct link oracle 141 / 146
   (4 stale-box + v8 42 call 21: smoothing pass choice); fileop 205: only the 37 Dynamic ops change.
   LEFT: v8 42 call 21 (FUN_18004a070 distance choice); v12 10 after record 443; the other region sets (tool-centre region, region
   builder, local window sets) still Clipper / own offset - their MCCECalcOffset calls are already identical in the unit set; C+0x340 by
   Clipper; planner gp.23 of buckle-v10 10 (cut circles' entity boxes before the 1e-4 shrink, FUN_1800e9c60) not started.
   2026-11-13 M2 sets (branch worktree-agent-a036f5997a1cd06b5, mcce_port_plan.md 9 "M2 sets"): DONE v8 42 call 21 (it was not the
   distance choice: FUN_180113300's verdict, mcce_dyn's PtInSh now the DLL-exact mcce_ctour.c one); DONE v12 10 record 443's broken path
   (FUN_18001c510 makes a zero-length trial the walked candidate-path piece); region builder A1 / A2 / material / bull / sliver filter and
   C+0x3c0 / 0x3e8 through the DLL offset + boolean (MCCE_DLLRG default 6). Dynamic NCI median 0.075 -> 0.072, worst 0.643 =, 14 better /
   0 worse; ct link oracle 144 / 145 (1 stale box), coffct 15130 exact, off 1383 / 1383, sobool 60 / 60; fileop 205: only Dynamic ops.
   Opt-in: the tool-centre region (MCCE_DLLRG=7: DLL-identical initial material, but 20 ops worse) and the window sets (MCCE_DLLWIN=1:
   worst 0.643 -> 0.436, median 0.072 -> 0.084; question below). The N13-18 "worse" ops vs mcce.dll on the same input: v12 23 / v11 1 /
   v10 1 port = DLL; v9 46 0.164 vs DLL 0.163; v11 / v12 10 0.324 vs DLL 0.277 (needs the tool-centre region).
   LEFT: the window target list (FUN_180066ad0; v12 17 commit 555 with DLLWIN), the re-entry target after a state-2 air exit (v12 10 record
   1460 / v6 32 record 500 with DLLRG=7), the material subtraction's entity splits (FUN_180037790), the measure-flag 0x20 accumulation
   (FUN_180030de0; flags only), S2 with C+0x340, planner gp.23.
   2026-11-13 M2 window targets (branch worktree-agent-ae0659c76810ed991, mcce_port_plan.md 9 "M2 window targets"): DONE the window target
   list (v12 17 commit 555): the DLL's target came from FUN_180059ca0's path target, whose Q (FUN_18005a8a0 MCCECalcContourOffset 0x100) was
   our own polyline offset; now the DLL-exact mcce_ctour.c port (CVe overload, open P kept open). Op 17 with DLLWIN: every commit = DLL,
   0.396 -> 0.071. DONE (by the same fix) the re-entry target after the state-2 air exit with bit 1: with DLLWIN + DLLRG=7 v6 32 is
   commit-for-commit the DLL's, v12 10 now first differs at commit 6538. Default: 37 ops byte-identical. MCCE_DLLWIN=1: 0.071 / 0.436
   (was 0.084 / 0.436; 11 better / 0 worse). MCCE_DLLWIN=1 MCCE_DLLRG=7: 0.062 / 0.435 (ok 2), vs default 21 better / 3 worse (v8 / v9 42
   0.242 -> 0.435 = mcce.dll's own result on that input; NAKTO 2 0.351 -> 0.354, DLL 0.390); mcce.dll on the same input: 27 / 37 ops every
   commit equal, p95 equal on 29, median / worst 0.062 / 0.435 both (question under "Waiting on the user"). New traces: MCCE_TRACE_WIN
   (WT / GT target lists, port + peloader), MCCE_TCDBG.
   Also: MCCE_DLLRG bit 8 = the start regions M / AIR (FUN_18008f4a0 / FUN_180090fa0 / FUN_180090ae0) through the DLL ports: with
   MCCE_DLLRG=15 MCCE_DLLS2=1 v12 10's region-builder offset inputs equal the DLL's byte for byte; DLLWIN + DLLRG=15 + DLLS2 0.062 / 0.435,
   v11 / v12 10 0.260 (DLL 0.277). Unit oracles: off 202 / 202, sobool 60 / 60, coffct 15130 / 15130, ct link 144 / 145; fileop 205: only
   the 37 Dynamic ops differ from master.
   2026-11-13 N13-24: window sets (MCCE_DLLWIN) and the tool-centre region (MCCE_DLLRG=7) ON by default (MCCE_DLLWIN=0 / MCCE_DLLRG=6 =
   the old behaviour); default now 0.062 / 0.435 (ok 2); fileop 205: only the 37 Dynamic ops change vs master f57f39e (rg_new8).
   2026-11-13 M2 bit-exact (mcce_port_plan.md 9 "M2 bit-exact"): the measure's area sum = FUN_180126ac0 (step drift at commit 1 1.5e-12 ->
   7e-15); NAKTO 2's entry = bit 8 (region order) + MCCE_DLLENT (FUN_18007ca70 DLL offset) + MCCE_MEDAX: records = DLL to 17114.
   2026-11-13 N13-25: MCCE_MEDAX, MCCE_DLLENT, MCCE_DLLRG=15 and MCCE_DLLS2 on by default (=0 / MCCE_DLLRG=7 = the N13-24 behaviour).
   2026-11-14 DONE: CalcMedAx P-P bisector end-normal sign fixed (mcce_engines.md 6.15.1): with MCCE_MEDAX on, OptiRough TUTORIAL 7
   op 5 had failed "nothing to cut" since c8fbf89 (centre outside the region); port = mcce.dll's MAE list on that region. The 3D
   oracles were not in the integration run: <scratch> adds them (hst3d metric + inch).
   LEFT: the material subtraction's entity splits (FUN_180037790 / FUN_180036250; v11 / v12 10: with bit 8 + DLLS2 the material is equal
   to commit 67, then a 6.3e-8 bridge line the DLL keeps - the step positions drift by 1.5e-12 from commit 1 (FP order), so bit-exact
   step arithmetic first), then v10 10 commit 6608 / v8 42 commit 6622; plunge-entry centre (BRACKET4 14 = DLL with MCCE_MEDAX=1); NAKTO 2 entry region
   (record 0 in every configuration); S2 by default from the DLL boolean (MCCE_DLLS2); measure-flag 0x20; planner gp.23.
   2026-11-13 M2 bit-exact measure + order mode 1 (branch worktree-agent-a4228046a72d825d2, mcce_port_plan.md 9 "M2 bit-exact measure +
   order mode 1"): DONE the 1.3e-14 commit-0 width (crossings = CalcEntIntersect gp_ent_isect; swept-region cap angles from the scaled
   normal), DONE the commit-20 refinement step (FUN_1801270e0's segment term x * (r * r), disasm 0x180127289), plus tangent vectors
   (FUN_18008e710 / FUN_1801204a0 / FUN_18011ffd0), candidate length sqrt, GetNearestArcPt / FUN_1800acd50 exact, measure flags ORed
   (the "mf 0x20" difference), FUN_180036250 collinear merge exact: MCCE_MEASX (default on; p95 of all 37 ops unchanged). v12 10 is
   bit-equal to mcce.dll through commit 6537 (was 1.5e-12 at commit 1). DONE the coarse divergences: all four (v11 / v12 10 6538, v10 10
   6608, v8 / v9 42 6622) are order mode 1 FUN_180055190 taking the NEAREST instead of the FARTHEST points (FUN_18013caf0) -
   MCCE_ORD1, ON by default (user decision N13-26; =0 the old rule): all 37 ops / 56 engine calls equal mcce.dll record for record and commit for
   commit, p95 port = DLL on all 37 (0.062 / 0.435); v10 10 0.171 -> 0.144, v11 / v12 10 0.260 -> 0.277 (= DLL). New traces
   MCCE_TRACE_SW (SW / LP / PS / LI / LK / TAN lines, tags, nrec) both sides, MCCE_NOMEMO.
   LEFT: v12 10 (MCCE_ORD1=1) commit 6538's re-entry point 1.8e-15 off (boundary target); mcce_ctour.c sh_orient_t's area is not the
   piecewise FUN_1801270e0 sum; plunge-entry / NAKTO items above are now DLL-equal with the N13-25 defaults.
3. M3 mcmill boundary building for from-outside ops (CHST2DProcess / PackageMachiningRegion S1 / S2 / S3, plan section 5 item 25):
   cause of buckle op 31 (the mcce.dll oracle showed the engine itself matches). MERGED 9c728e2 (rg_base5 vs rg_new5 clean).
   2026-11-13 (branch worktree-agent-a2e2b41b8da1e5076, formats/dynamic.md "Nested regions" / "Region-to-region link"): S1 / S2 / S3,
   the region order, the wall-stock offsets and the islands were already decoded and ported (2026-11-07 / 11-09). Op 31's remaining
   difference was not the region but its top of stock: HasOuterPocket (the decompile drops the depth-cut setter's arguments; read
   from the disassembly 0x1802b05ae-0x1802b0701) gives a region nested in another region that region's z as top of stock (feed
   plane / depth list / engine P+0x78 / group header). Ported (`outer_pockets`), with the depth writer FUN_180045770's group link
   between regions (`hst_group_link`: clear_move / retract_move, plunge_move). Oracle: op 31 (v6 / v7 / v8) ours->ref p95 0.111 /
   0.111 / 0.069 -> 0.060 / 0.060 / 0.059, max 3.501 / 3.501 / 2.501 -> 0.226; the other 34 Dynamic + 3 Area ops unchanged.
   Left (no user op needs them; all refused with a message): containment chains (role 7: PackageChains l.4737-4835 =
   MCCECalcPockUnion + MCCECalcPockIntersect, mcce boolean cores NOT DECODED, Clipper substitutes only), air / entry chains (roles
   6 / 5) and open machining chains (CloseOpen* / TraceOpenPockBound / BuildAirEntryPockBound, ~2000 lines unread); the links
   between depth copies of one region (whether CHST2DWorkItemDCS FUN_1802b11f0 writes a group per copy: not read) and Area Mill
   Core Roughing (NAKTO 3, McHmm FUN_1802ef450, not mcmill). The Solid Chain loops stay CORRELATED (Parasolid rollback).
4. M4 DONE 2026-11-13 (e9a6f04): the heap-layout-dependent "invalid coordinates" bug (buckle-v8 op 32 / v10 op 10 / v9 op 42) was
   the Dynamic Mill tessellation-cache heap-use-after-free fixed by ef3f42b (2026-11-05, mcce_dyn.c tess_get / g_tess_pin); this
   item had not been closed. Re-checked on 6fd69c8: the 3 ops export + regen identical (v8 32 2710 moves, v10 10 10109, v9 42 10045)
   under MALLOC_PERTURB_ 0/1/85/165/255, ASan+UBSan and -ftrivial-auto-var-init=pattern; fileop of all 205 user-part ops
   byte-identical across HEAD / pattern / zero auto-init / MALLOC_PERTURB_ 165 / 1 + tcache off / ASan; ASan: no reports.
   e9a6f04: md_d6d50 rewritten without the freed-pointer alias (gcc -Wuse-after-free gone; it was a false positive, same walk),
   and UBSan's zero-length NULL qsort / memmove / memcpy (mcce_dyn.c isect, region_next; mcam_write.c mcw_build) guarded.
   Output unchanged (dynamic oracle 37 ops p95 median 0.111 worst 0.643, identical before / after).
Orchestration handed over to session master-cam-reverse-1e on 2026-11-13 (user).

Main branch: master (since 2026-11-12, fast-forwarded to toolpaths2 b09df6f; toolpaths2 is retired). Agent branches merge into master.

- Test suites (2026-11-12, after the N13 / lost-branch / S2 / S3 merges; all suites compared with a run on 18d75e5): pre-existing, not from
  today's merges: planes 25/38 stale goldens, stockplanes 16/125, op 51/596, misc 5/87, tool 76/226; draft_ang / draft_circ /
  draft_create / draft_ord / draft_tan print a compile error; engrave_test.sh fails on a missing OUTDIR/engrave folder. To review
  (bless with reasons or fix) - none of these are run by the usual regression.

## Lathe decoding + porting (L-1, amended by L-2 2026-11-16: op by op, ported once decoded and matched to stored NCI; posts stay out, P4; formats/lathe.md)
- L1 mclathe.dll in Ghidra (`~/mastercam-re/work/lathe/gh`, project `lathe`, script work/lathe/import.sh); first dump
  CLRoughTp / CLFinishTp / CLFaceTp + callees -> work/lathe/dump/lrough_finish_face.c. Then: prm_lrough / ctour_rec layouts,
  the lathe op records in a part file, Groove / Thread / Cutoff / Drill. Each op ported once decoded and matched to stored NCI (L-2).
  2026-11-16: whole DLL dumped (work/lathe/dump/mclathe_all.c); Lathe Rough decoded in lathe.md 5 (prm_lrough at op+0x11c0,
  depth rule = stored NCI of 4 sample ops); its NOT DECODED list = next (entry / exit, lthcc*, build_lctour, zigzag).
  2026-11-16 (user request "the most basic lathe toolpath decoded and implemented"): **Lathe Face (opcode 66) decoded and
  ported** - lathe.md 6, viewer/lathe_face.c (CAM_LFACE; cam_cli fileop + the app's Regenerate). millturn.mcam_8 op 1 = its
  stored NCI (3 / 3 moves, max dev 2e-15; tools/tests/lathe_face_test.sh, also self-consistency edits and refusals). The
  LinuxCNC post now refuses lathe ops (stored or regenerated). Next for Face: (a) the face from the stock boundary
  (GetFaceBoundaryFromStock + BOUNDARYCLASS / the machine group's lathe stock - every defaults-library Face uses it, so a NEW
  Face op needs it), (b) entry / exit vectors (CLEntryExit rotate_tp_vec / calc_arc_pts / CalcEEPoints, shared with Rough),
  (c) corner strategies, (d) the first-move home branch of add_rapid_from_prev_pt / SendLToolHome components (needs a Face
  NCI that shows them), (e) what loads pclsLTool's turret (TopTurret) before bnci_lface, (f) a Face op dialog, the lathe NCI
  records (lwr_sof, SetBNciInfo) and the save. More stored Face ops from Mastercam (L2) would turn the self-consistency
  checks into oracle checks.
  L-3: three Face behaviours are CORRELATED (lathe.md 6.7a; trace next: FixLatheToolOrigin / SetActiveTurret callers for the
  turret, add_rapid_from_prev_pt's home branch for the first move, mccore ccomp_pock for the face-line offset).
  L-4 done 2026-11-16: a regenerated / taken-over op's Toolpaths manager row keeps the stored op's WCS / Tplane (optree_text.inc,
  InitTvOpText @1800c3350; CamParams.tplane_rec_id for Lathe Face); scene tree_regen_planes (op_compare.sh).
  L-5 done 2026-11-16: a regenerated / taken-over stored op keeps its own comment (op +6 = record 0x6e; none = none) - before, the
  takeover (cam_op_from_file, cam_cli) put the type name in CamOp.name, so the tree row gained " - Contour" / " - Lathe Face" and
  the LinuxCNC post / cam_cli --nc a "( CONTOUR )" line; the .mcam save already wrote no comment. Scene tree_regen_comment.
  L-7 done 2026-11-16 (user: "Match Mastercam"): a NEW op starts with an empty comment (cui_ops.c open_params, cam_cli new op;
  InitOperation clears op +6; every Mill_MM.mcam-defaults op has none); our "<type> <n>" naming removed. Copy / Paste keeps the
  source's comment (list_op_copy @1800b8700).
  L-6 "New Face end to end" (lathe.md 7):
  - Stage 1 DONE 2026-11-16 (decode + engine, branch worktree-agent-a8b86f32bc2437725): the Face command / InitOperation flow (7.1),
    the lathe stock = machine group components + LBDRY records + each op's stock-after copies (7.2, viewer/lathe_stock.c), the face
    from the stock + per-pass stock clipping (7.3, viewer/lathe_face.c), the dialog's two pages with a 51-row control -> field map
    (7.4, formats/face_dialog_fields.tsv), lathe tools (7.5); `cam_cli PART lface - --lathe-defaults FILE` makes a new Face from
    LATHE_INCH op 56 (self-consistency only, lathe_face_test.sh section 4). Open: the Windows reference (windows_checklist.md 7),
    the default Finish Z of a new op on a plain lathe, OpActiveSpindle / TopTurret (CORRELATED), mccore intersection helpers (NOT
    TRACED), a new Face after an earlier Face (InitOperation's lathe copy branch), the bottom turret / right spindle (mirror polygons).
  - Stage 2 DONE 2026-11-16 (L-8; lathe.md 8): Lathe toolpaths > Face (menu 780 / Turning ribbon) on a lathe group -> the two-page
    dialog (core_ui cui_lface.c, Qt lfacedlg.cpp), the part tool list + Make_op_tool (lathe_tool.c, byte-identical to lathe.mcam_8's
    ops), the axis combination + default Tplane (lathe_axis.c, 11 / 12 stored ops), reset_lathe_op_prm_defaults' bytes, the active
    planes, Finish Z (InitZFinEd), the save with the two point entities (lathe_newop.c, mcam_saveops.c); op_compare scenes lface_*.
    Open (lathe.md 8.8): "Show library tools" default on (library lathe tools not read), the stock-after-op copies (UpdateOpStock's
    stock update NOT DECODED, not written), max spindle clamp / ValidateSpindleSpeeds, the right spindle / bottom turret, point picks
    and the sub-dialogs (greyed), millturn op 3's combination (1805 vs 1802).
  - (was) Stage 2 (UI, after the cui_ops.c / optree_text.inc op-comment work lands): Lathe toolpaths > Face command (lathe group only), the
    Face dialog (tool page 3911 + Face page 5361), the lathe tool list (LTL_ID records + Lathe_Inch.tooldb TlToolLathe; Make_op_tool -
    Rotate_ltool_rec / Xlate_t_center NOT DECODED), saving the op with its two point entities (LfIn.made) and its stock copies
    (op +0xb40..: UpdateOpStock NOT DECODED), the active planes and reset_lathe_op_prm_defaults' bytes.
  L-12 DONE 2026-11-17 (lathe.md 9): the tool page's machine checks DECODED + PORTED for a new Face - the spindle clamp (MapToDlg,
    MaxLatheSpindleRPM = the combination's part-list chuck), the spindle-change check (ValidateSpindleSpeeds() from GetToolProperty),
    UpdateToolFeedInfo, RunMachineDefChecks after every feed / speed edit (feed limits from the machine record, italics, the 3275
    warning); scenes lface_rspindle (re-blessed: max italic) / lface_mclamp (new). Refused now: group +0x119c != 0 (feeds / speeds
    from material: CalcFeedSpeed NOT DECODED; from defaults / user defined: NOT PORTED) - millturn.mcam_8's group has 1. Open: the
    3275 "Turn off warning for session" check box (not offered), EnableFeedRateLock on a re-pick, list arrow keys.
  L-13..L-15 DONE 2026-11-17 (lathe.md 9.5): feed calculation From defaults / User defined ported; the 3275 session check box;
    EnableFeedRateLock (config 17027) on a re-pick; the Face sheet cancelled when the part closes. From material (L-13 next item):
    lathe.md 11 - CalcFeedSpeed decoded, NOT PORTED: the 0x388-byte (older minor version) part material record's layout is not
    traced (measured: 0x18 bytes fewer before +0x1b8, matches millturn ops 7 / 8), the inch-material / metric-tool feed branch.
    L-16 pass (lathe.md 11.1): feed branch settled from the asm (/ 25.4 whenever units differ); converter trace: none in the X9
    read path (mcprvverfile FUN_180065720 / FUN_18005e7f0) nor in mccore's material code - X9 bytes kept in place as traced;
    open: search mastercam.dll / mclathe file-open passes, or accept the as-traced reading (question L-16a). Still refused.
  Next lathe items (L-10..L-12, 2026-11-17): (1) DONE (L-12 above);
  (2) the stock update that writes a lathe op's stock-after copies (op +0xb40..) - DECODE STARTED 2026-11-17 (lathe.md 10: writer
  SaveComponentsToDb 0x180110b60, the per-move ToolDiffLinArc 0x1801e03a0 -> mcmachinedef BOUNDARYCLASS DiffB1B2 / Stretch / ArcStretch;
  next: the mccore / mastercam caller, the tool boundary polygons 0xc / 0xd, the polygon engine, oracle LBDRY 174 of lathe.mcam_8); (3) the lathe .tooldb (Lathe_Inch.tooldb TlToolLathe) for
  "Show library tools"; (4) millturn op 3's axis combination 1805 vs stored 1802 (unexplained).
- L2 lathe parts with stored toolpaths from Mastercam 2017 (Windows): the oracle; none on this machine.
- turnkernel.dll = CIMCO Edit's HSMWorks turning engine (not Mastercam's): surveyed, frozen Python extracted, not pursued.

## Overnight 2026-11-12 -> 11-13 (user: "make a plan for the night ... tackle some new areas that need decoding"; merge if clean)
At most 3 agents at once (RAM), builds serialised by ~/mastercam-re/build.lock, shared brief <scratch>
MERGED into master 2026-11-13: S7 (33befd2), R (461f677), K + T (bcffd61), A + B + S (28a7d10; check vs bcffd61: fileop 205/205 identical,
  GUI suites same differences as master, op +8 new scenes pass, units / blend / hst_rest ALL PASS, cad 0 failed, checktrim_test all ok).
  B (Body / Face Selection Bar buttons, worker-thread generation): the buttons sit in our status bar - the Selection Bar position is NOT
  DECODED (question for the user). W (Waterline) merged af7177d (vs 28a7d10: SWARF_METRIC op 3 ours->ref p95 0.315 -> 0.051, cut 9972
  vs 10239; SWARF inch op 4 angles 50..90 now generates; all GUI suites + fileop identical to master).
  V line (NOT merged, parked behind N13-16): mchmm Voronoi stages 1-4 incl. Corner rounding + CPipeThinToolpath writer modes 0/1/2,
  all identical to mchmm.dll (hmm_voronoi_oracle.sh) on branch worktree-agent-a82f3c31073064da1; not wired - the Waterline pass
  consumer in Linking Plan 0x141c and the Eva +0x15c0 tool-holder condition are NOT DECODED (hst3d_port_plan.md 7 "W7").
- V. S7 merged-tree check (base f02a6d9 vs c236b04: oracles, extra corpus ops, regression), then merge S7 into master + oracle_record.
  (The first check at 20:12 used stale binaries: the base had no Qt app, the S7 tree's binaries predated its merge commit. Redone.)
- K. Pocket finish Keep tool down 0x126a (FUN_1801cd200 / wr_pock_toolpath keep-tool-down sections): blocks 7 corpus 3D-pocket ops + BLOCK.
  K DONE 2026-11-12 (branch worktree-agent-abc570c9b9166bf63; formats/pocket_engine_b6.md "Keep tool down 0x126a"): FUN_1801cd200's
  condition, op code 4 (wr_pock_toolpath's -97 / -98 markers: the finish link at once, else pending lVar19 for 0x891; the roughing's
  depth markers FUN_1801ede50 / FUN_1801ef830 with the entry off) and op code 0xe (Surface rough levels: the link at once inside the
  pocket's tool-centre bounds, the FUN_1801c3b20 / FUN_1801bf680 retry through FUN_1801edcd0's bound, plunge_move with the tool down;
  machine_pocket's order, no wr_pock_toolpath sort); record 0x891 read for Pocket; dialog 3004 / 3030 1377 live. BLOCK op 3 (2017 / X9)
  generates (ours->ref p95 0.202, the finish link = NCI records 121-123; the rest is the known helix centre). Corpus op 10: 6 of 7 generate
  (CONTOURFINISH1 2, FINISHSCALLOP 1, LEFTOVER 16, RESTPASSES 16, STEEPSHALLOW 4, CURVE 5 AXIS 1; numbers in pocket_engine_b6.md);
  PROJECTBLEND 7 now stops on a bad gls_offset of its level region (ours, safety refusal; it hung there before this refusal).
  Left: Remachining 0x126a (its own writer branch), Surface rough depth cuts + 0x891 + entry off (pock_rough_start_pt), the levels /
  slice of op 10 themselves (FINISHSCALLOP's Z levels differ from Mastercam's: sr_levels).
  Checks against f02a6d9 (master base): fileop 205 / 205 identical; pocket_regress 7 identical + BLOCK x2 (refused -> generated);
  op_compare 60/622 vs master 47/622: the 13 new = only the two Keep tool down controls (no longer "NOT DECODED", the Finishing one
  live) - goldens re-blessed (.txt only); srough_oracle.sh (finish now on) p95 0.079 / 0.049 -> 0.030 / 0.052, cut length 1772 -> 2459
  (ref 2472). The open pocket's existing refusal "keep tool down between depth cuts with the entry off" (cam2d_pock_open.c) now
  sees 0x891 (it was never loaded for Pocket ops before).
- R. Contour Remachining (mccore pbmap engine + rest material, FUN_1800550c0; formats/contour_remachining.md).
  R DONE 2026-11-12 (branch of agent R, formats/contour_remachining.md last section): corpus has no type 5 op (194 contour ops scanned: 176 / 2 / 16 of
  types 0 / 1 / 2), so no oracle.py measurement exists; mcmill.dll's lead builder FUN_180050de0, FUN_1801c2b30 and the gouge check FUN_1801c46f0 now run
  in the peloader oracle against the port (850 cases, 0 differences); compensation Off ported (side = record 0x5d2, no comp codes); Control + taper +
  gouge check ported (the depth's offset chain kept). Left: chains with trimmed ends (chain_to_pock_bound's FUN_1805d4060 not decompiled), points /
  splines / 3D chains, verify_2d_operation on moves generated here (no NCI markers), GetProdEnableBits bit 0x10 (NOT DECODED); the driver
  FUN_1800550c0 itself has no oracle (needs the DB / NCI). Also fixed (all contour types, UpdateLocalOp FUN_180157e30 l.240808-240811): switching
  Compensation to Off keeps the last direction in 0x5d2, and back from Off restores it (scene gen_ct_remach_off_right).
- After S7 is merged: A. N13-10 HST check trimming plan 0x141f (Edit Passes to Surfaces); B. N13-8 Body / Face toggle + N13-9
  post-dialog 3D generation on the worker thread.
  A DONE 2026-11-13 (branch of agent A, formats/surface_selection.md 8.3): Edit Passes to Surfaces 0x141f decoded in mchmm.dll
  (CTrimPassesToSurfacePlan / EvaTrimPassesToSurfacePlan, decomp/hst3d_trimsurf.c: the trimmer FUN_1803503e0, the drop test FUN_180351ee0, the
  0.5 mm sampling FUN_1803538c0, the bisection FUN_180356f50, the intervals FUN_1803574d0, TrimmedOutPasses = the complement FUN_1803569d0;
  mckernel's parameters FUN_180070e8c, OverThickness = record 0x12a8 check stock) and ported (viewer/cam3d_checktrim.c) for HST Waterline,
  Raster, Spiral, Radial, Project: the passes are cut where the stock-inflated tool touches a check. Validated: analytic self test
  (tools/tests/checktrim_test.sh); TUTORIAL 2 SWARF_METRIC op 3 now generates (NOT a method-1 reference, its 0x172c = 0) and agrees with its
  NCI far better than method 0: cutting ours->ref p95 0.315, length 9933 vs 10239 (merged 13.36 / 11507, drive only 1.27 / 10473). No DLL
  oracle (mchmm is not loadable by tools/peloader). Left (refused): negative check stock (tmpmmlu +0x160), holder check (0x16d7) with checks;
  not ported: mcmsurfres 0x97 (check stock = Z stock warning, FUN_1800202d0 ms l.23081; text not located), Detect Top Check Surfaces.
- As slots free: W. Waterline Corner rounding (ProfileSmoothing) + contact angle filter inputs (FUN_18040b6f0) + inch Waterline cut
  length; S. Scallop "Use boundaries as drive curves, then collapse" (0x16e1: plan 0x1445 fed by Shallow Area Detection 0x140a).
  S 2026-11-13: the stand-ins of the first port were withdrawn (S-1 answered: decode first); 0x16e1 = 1 is refused at the cell classification.
  2026-11-13: FUN_18004cb90 DECODED (rotating calipers: for line loops the XY hull diameter = largest vertex distance); FUN_18026c540's
  structure DECODED (crossing pairing FUN_18026a990, contact test FUN_1802f3a80 on both crossings of each pair, split on
  AreaDetectContactNormalChange via FUN_180252a70; scallop.md 10.6). Port blocked: it works on the CWeave's fibre crossings and the
  drop's contact records - porting it means porting the CWeave engine (off.c, 134 functions) + FUN_180222920's contact record; the 2D
  path stands in for the same engine. NEXT (multi-session): port the CWeave; until then 0x16e1 = 1 stays refused.
  S 2026-11-12 (branch of agent S, formats/scallop.md 10): CDetectShallowAreasPlan 0x140a decoded (EvaAreaDetectShallowPlan FUN_18031dd90:
  toolbox, weave, contour groups, MinDiameter 0x1b49 = 1.0 mm filter, z + ToolThicknessZ; defaults FUN_18030fc80; McHmm lengths are mm) and the
  3D plan's first offset (Mach3DBoundaryTolerance 0.1 mm, x 1.5 retries); ported in gen_scallop (the default op now generates; the op's angle
  limits go to the detection, so TUTORIAL 2 SWARF op 5 / MOLD BOTTLE op 6 run: ref->ours p95 0.014 in, cut length 313 / 336 vs 320 / 343).
  2D path byte-identical. Left: the weave cell classification FUN_18026c540 and MeasureCurveDiameter FUN_18004cb90 (ours stand in, labelled);
  MULTIAXIS ROUGHING FALSE FLOOR op 6 needs a non-Top tool plane (id 52) for the HST engines (measured only through the MCAM_SC_PLANE test
  harness: p95 0.006 in); Scallop's contact angles use the same Eva window as Waterline (FUN_1802f2340) but in the area detection, not FUN_18040b6f0.
- T. STL import (user 2026-11-12: "get it queued for translation and incorporation"): locate and decode Mastercam 2017's File > Open /
  Merge of .stl (ASCII and binary; which entity type it creates, its options dialog / config items), port it into CAD import
  (formats/cad_import.md) with display / pick / save as Mastercam does. Today STL is read only as stock (cui_stock.c) and as the
  Simulator Compare target; the surface -> STL writer (ifk_surfpoly.c) is internal.
  T DONE 2026-11-12 (branch of agent T, formats/cad_import.md 7): mcstl.dll decoded (Ghidra project ~/mastercam-re/work/stlimp/gh, dumps there);
  File > Open of .stl: config 3019 n (default) = one PMESH_ID mesh entity (StoreSTLFileAsSTLMesh: CreateSTLEntryFromFile / FromStream
  enhanced form, box FUN_1805cc560), shown (outlines / shaded), picked, deleted / undone, saved to .mcam (FUN_1802c72c0 + WriteEntryToStream:
  "MASTERCAM STL DATA" binary STL) and read back, project MESH lines; 3019 y = lines (rd_stl with its duplicate test). The Stock Setup's STL
  reader moved to viewer/stl_mesh.c (shared). cad_compare.sh section 5: 18 new checks pass; fileop 0/205 differ vs the base tree.
  Left (NOT PORTED / NOT DECODED, written in the doc): the save of a mesh whose colour bytes differ (WriteEnhancedSTL's MSVC std::sort
  FUN_1808bef00 - decompiled in work/stlimp/dump/core_match.c, portable), IFF / MCO meshes (kinds 2 / 3) from parts, the STL Options dialog
  (mcstlres "STL Read Parameters"), transform / copy / analyze / AutoCursor / window selection / attribute writes of meshes, Esc in the lines
  import. No corpus part has a PMESH_ID record, so the written record is UNVERIFIED in Mastercam (add to formats/windows_checklist.md).
- Found 2026-11-12: OptiRough "Core (from outside) is NOT DECODED: cut as Pocket (the region walls); stand-in, ours" (TUTORIAL 7 op 5)
  is an improvised stand-in (hard rule): decode it or refuse.
- Found 2026-11-13 (agent S): the generating Scallop 2D path stands in for Mastercam's CWeave engine (off.c, ~134 functions) with our
  height-field grid - also an improvised stand-in under the hard rule. For the user: refuse it until CWeave is ported, or keep it meanwhile?

- W (2026-11-13, branch worktree-agent-a4adbad5598fa732f, hst3d_port_plan.md 7): DONE the contact angle filter's three inferred inputs decoded
  (evaluator chain FUN_18040ea00 -> FUN_1803f9c20 -> FUN_1802eb170 -> FUN_1802f2340; DAT_1809bf1a0 = acos(-1) x 0.5; PassesJoinGaps in McHmm mm via
  job +0x80 = 25.4), N9-2's refusal lifted (TUTORIAL 2 SWARF op 4, MOLD BOTTLE op 4 generate); the inch "10 % short" cut length = Mastercam's links
  (8-10 % off the levels); Corner rounding FUN_180557f60 decoded, not ported. The own cell polygon method was NOT approved (user 2026-11-13:
  decode first) and removed. W2 DONE 2026-11-13: McHmm's weave loop traversal decoded and ported for Waterline (cam3d_rough.c wv_loops;
  hst3d_port_plan.md 4.5 "Weave -> loops (decoded 2026-11-13)" and 7): the recess of SWARF op 4 is matched, MOLD BOTTLE op 4's ambiguous cells
  go through the decoded probe (FUN_18012a140 / FUN_1801127c0). (a) DONE 2026-11-13 (branch worktree-agent-a6c0de7878cb94835): the weave box + boundary fill ported (FUN_180289d90 read:
  hst3d_port_plan.md 7 "The weave box and the boundary fill"; cam3d_rough.c wv_fill, gen_waterline's fibre ranges); the clipped contact flag
  (0x80000000) travels through the weave to the loop vertices; BRACKET4's default Waterline generates; SWARF op 4 / MOLD BOTTLE op 4 unchanged.
  The flags drop the containment outline only where the passes are the weave's own loops (Fo = 0). NOT PORTED: with a flat-radius offset the
  flags must reach the passes through the mchmm Voronoi offset (W3 stage 3) - until then those passes are clipped to the containment by
  wl_inside (master's rule), and refused where that cannot drop the fill's outline (BoundaryOutwardsOffsetRad <= Fo); flat tips keep wl_inside;
  the x 1.1 on BoundaryOutwardsOffsetRad (Eva +0x208 profile decoded) and FUN_180271070's re-fitted box (the inflexion levels' XY box) ported. (b) wv_loops for OptiRough / Area Roughing / Horizontal / srough; (c) the angle filter on the wv_loops vertices before the flat-radius offset
  (W3 stage 3). W3 (Voronoi library, Corner rounding; user decision N13-12): stage 1 DONE 2026-11-13 - the construction ported (viewer/hmm_voronoi.c, not yet called by an engine), identical to mchmm.dll record for record on 395 oracle cases (tools/tests/hmm_voronoi_oracle.sh, peloader --hmmvor); stage 2a DONE (the offset radius push FUN_18054a640, the region labels FUN_18054b480, same oracle); stage 2b DONE (W4: the region copy FUN_18054ec00 and its finishing, 358 copies identical); stage 3 DONE (W4: the offset loops FUN_180557f60 without Corner rounding, through a recorded pass writer: 401 loops / 7767 writer calls identical; not wired); stage 4 DONE (W5: the Corner rounding block of FUN_180557f60, its element arcs and flag split with mchmm's arc library: oracle mode corner, seeds 1-5, 401 loops / 9060 writer calls identical with msvcr120's libm; not wired); wiring: the input side decoded (FUN_18040b6f0 writes the loops with their keep bytes through the writer at Eva +0x1440, FUN_180123330 turns element +0x14 -1 into the 0x40000000 site flag, param_9 = 0 for Waterline), the output side: W6 2026-11-13 ported the pass writer CPipeThinToolpath + its flush FUN_18011ca00 into TGridC records with no thinning (mode 0) and line thinning (mode 1), DLL-identical (viewer/hmm_pipethin.c, oracle modes pipe0 / pipe1; FUN_180120e60 is a straight-line split, not a span drop); W7 2026-11-13: Waterline runs template mode 2 (ThinArcEnable defaults true for 0x1436), the arc thinning FUN_180122380 / FUN_18011f6a0 ported and DLL-identical (oracle mode pipe2); the span drop rule decoded (span i-1 -> i dropped when vertex i's bits are exactly 0x40000000, every consumer found) and the flag rule's dependence on Eva +0x15c0 (PosContactOnly && ToolHolderDefinition && !WeaveSubdivision: then both ends must be kept, as master's direct path; else only the end vertex); still NOT DECODED: which consumer reads the Waterline passes (Waterline Linking Plan 0x141c), Eva +0x15c0's holder condition for our ops, the build-failure fallback, FUN_18040af10 (WeaveSubdivision 0x1bc2), wv_loops' start node - both gen_waterline refusals stay; hst3d_port_plan.md 7 "W7".

## 3D toolpath surface selection (N13-2, 2026-11-12; user: full flow, agents)
- S1 decode: Mastercam's Toolpath/surface selection (mcmsurf msurf_assoc @1800167c0, FUN_18001bcf0, containment FUN_18001cff0 / FUN_18001b640,
  surf_sel_bits, msurf_ent_count, the dialog handlers, the sld_* solid face helpers, the .mcam drive / check / containment records)
  into formats/surface_selection.md with a staged port plan. Then S2.. port stages by agents, merged after the full regression.
  S1 DONE 2026-11-12 (decode only, formats/surface_selection.md): msurf_assoc order, drive / check selection (FUN_18001bcf0: config
  Drive None/Prompt/All, Check None/Prompt/Unselected, mask 0x110100), solid / face picks (mcgeomsld sld_*, SldToolPOp link id at TP +0x9a),
  containment (role 2 chains), the per-op dialogs 112-132 / 1519 and their handlers, TP record layout checked on 908 parts (+0x74 is the
  selection class, the entity class is +0x78; 2018 records shift +0x10 from +0x98; flag 4 = check). Found: stored ops' check surfaces are
  machined as drive and solid refs dropped (mcam_load.c / cam3d_rough.c). Port stages S2-S7 in section 6; questions SS-1..SS-4 below.
- S3 DONE 2026-11-12 (solid face identity, read-only; formats/surface_selection.md 7): SldToolPOp read (FUN_18028e9d0 / FUN_180274c80),
  face id -> McEptr record (FUN_18026a240, post-load slot 1), sld_num_of_surfaces_in_toolp's counts, GetToolPSurfSelections' lists;
  viewer/sldtoolp.c/.h (API for S2 / S4: solid idn + link id -> (XT face, type) list), xt_faces() in xt_mesh.c, SolidOp.base_n.
  Corpus (tools/tests/sldtoolp_test.sh, expected output checked in): 30 solid refs, 30 links resolve, 16 of 22 whole-body refs mapped
  to all XT faces (6 of 2018: schema 28101 missing), 10 of 10 picked face ids resolve to their McEptr record, 0 mapped to an XT face:
  NOT DECODED (needs a Parasolid history replay) -> SS-5 below. No peloader oracle (the readers call Parasolid). S6 notes in 7.7.
- S2 DONE 2026-11-12 (data model + .mcam load; formats/surface_selection.md 6 "S2" status): TpRef ecls +0x78 / eptr / flags / link,
  2018 TP records read 0x10 later from +0x98 (all kinds: also fixes 2018 chain / index / rev / role, points, stock model source op);
  CamOp.ref (CamRef list, side file REF lines, DRIVE / DSURF still read) replaces drive[16] / dsurf[256]; cam_refs_from_tp,
  cam_op_set_drive; c3_mesh_op / blend_drive_from_tp take drive refs only (check surfaces left out until S7), solids by their
  SldToolPOp (S3: whole body by body type; face picks refused, N13-7), mesh / CAD-file drives refused. New `cam_cli PART tprefs`.
  Verified: tprefs = raw records on all 908 corpus parts, counts = table 2.7 (its Waterline drive count corrected 102 -> 145);
  fileop 410 / 410 identical; 3D oracles identical except waterline TUTORIAL 2 SWARF_METRIC op 3 (43 check surfaces no longer
  machined: cutting ours->ref p95 13.36 -> 1.27, cut length 11507 -> 10473 vs Mastercam 10239); Benchmark 3_0 op 2 Horizontal Area
  now generates from its solid (was "no drive"), ops 4 / 6 still refused (OptiRough Stepup NOT DECODED).
  Left for S4: chain roles (2 / 3) of chains picked here; S7: check surfaces / containment in the engines.
- S7 DONE 2026-11-12 (engines use check surfaces / containment; formats/surface_selection.md 8): check refs meshed with the drive for HST
  styles 0 / 1 / 3 / 4 / 6 (bnci_surface 0x172c = 0 -> FUN_180258310(2), warning 282 / 287); refused for Waterline / Raster / Spiral /
  Radial / Project (kernel Edit Passes to Surfaces 0x141f NOT DECODED), ops 10 / 0x83 -> mcmsurfres 249, other classic ops + Blend refused;
  containment = every chain of the op (HST Project: role 2), open chains closed (FUN_180128390), stored ops read their TP chains
  (c3_containment). Parallel 5 / 11 trims its lines to the containment (FUN_180133860 / FUN_180205770; oracles: PARALLELSTEEP op 1 ours->ref p95 0.057 ->
  0.044, length 2775 -> 2594 vs 2557; LEFTOVER op 17 p95 0.095 -> 0.050, 1135 -> 972 vs 977). Next: Radial containment, the 0x141f evaluator (DONE 2026-11-13, A above)
  (mchmm) for HST finishing checks, the classic check model (FUN_180290fb0 solvers). Question SS-8 below.
- S6 DONE 2026-11-12 for X9 / 2017 (formats/surface_selection.md 6 "S6" status): 3D ops are written to .mcam (own record or the defaults
  op, with the variable tail), their kind 4 references kept (stored op) or built (picked here: tp_surf_list + store_ent_assoc), op +0xf0 /
  +0x151 / +0xff, header +0x6c with kind 4 entities; a solid picked here as a whole body gets a new 97-byte SldToolPOp in its history
  (link = max + 1). The OptiRough refusal is gone. Refused with NOT PORTED: rolled-back histories, histories with FBM / lathe toolpath ops,
  solids not in the part, deleted references, 2018 parts. tools/tests/surfref_save_test.sh (+ expected): 2017 kind 4 1380 / 1380 kept,
  1375 / 1380 built (+0x70 stale stamps), read back 36 / 36; Windows check formats/windows_checklist.md 4.
  Follow-ups: (1) S4: a face pick on a solid (CamRef has no face list yet) must be refused at save (N13-6: McEptr id of a face NOT
  DECODED); S4's CamRef order should follow tp_surf_list (surfaces / meshes in DB order, then solids; drive, then check). (2) All TP
  writers (kinds 1 / 6 / 7 / 30): header level = main_level (store_ent_assoc, decoded) instead of 1; colour = tp_linear_color (its
  configuration item not located: decode); kind 7 attributes = main_attrib. (3) 2018 TP record +0xc4 and solid +0x74 0x10000: decode
  before 2018 parts are written. (4) A stored 3D op edited here (cam_op_from_file does not take 3D ops yet) - only the saveref test
  path and ops made here reach the writer.
- S4 DONE 2026-11-12 (the pick flow; formats/surface_selection.md 6 "S4" status): msurf_assoc's order in core_ui cui_chain.c (config
  17017-17023, dialog 100, Drive / Check None / Prompt / All / Unselected, check-over-drive conversion, containment chaining role 2 with
  msurf_op_has_tcbs decoded, S5 hook, picks after the op dialog (N13-4), 241 once, 427 on Esc / Cancel / failure), solid body / face
  picks (own ray, N13-3) into CamOp.ref + new CamOp.rface (side file RFACE); face-pick ops refused at generate until S7. Questions
  SS-6 / SS-7 below (answered N13-8 / N13-9, done 2026-11-13: the Selection Bar's Select body / Select face buttons in the Qt app,
  the generation after the picks on the worker thread with the progress window). Scenes op_scenes s4_*. Left: the Selection Bar
  itself (place, other controls) and the buttons' starting state are NOT DECODED (surface_selection.md 6 "N13-8 / N13-9").

## Overnight 2026-11-11 (no user input; user: "pick a few areas ... overnight ... get going on them")
Agents in worktrees off toolpaths2 (ef028b7+), merged by the main session after its regression run:
- O1 Curve Parting Line (parting_line mccore 0x180566740): decode, port, peloader DLL oracle, wf scenes.
  O1 DONE 2026-11-11, merged 997bece: ported for untrimmed NURBS surfaces (viewer/mcpartline.c, mcpartline_mesh.c,
  core_ui/cui_partline.c; formats/wireframe.md "Curve Parting Line"); oracle tools/peloader --partline: 615 / 615 cases (205 records x 3 view / angle settings)
  with the bit-identical mesh and nodes, chains equal, curve coefficients within 3.2e-11; 14 cpart_* scenes. Refused (NOT PORTED): trimmed surfaces,
  kinds other than 0, solid faces; not done: live colour, Esc colour prompt, the per-surface prompts.
- O2 Curve Surface (project_chains_onto_surfs): decode, port, wf scenes. DONE 2026-11-11, merged: kernel
  viewer/mccsurf.c (the msf march, start search, thinning, ordering, links, joins), surface curves (CONS 0x400) loaded / drawn / saved /
  deleted, the command (cui_csurf.c), oracle `peloader --csurf` 2513 / 2575 runs the same (62 differ: march / join details not traced),
  scenes csurf_*. Refused: non-NURBS surface kinds, curves used by operations, FUN_180531890 (NOT DECODED). formats/wireframe.md
  "Curve Surface".
- N12-2 Curve Surface piece order through Mastercam's view: DONE 2026-11-12 (branch worktree-agent-a75fc5dc145b10eea): calc_surf_norm (with
  its stepping branch), find_view_k / make_view_k / AddMCView over the session view table, world_to_view; the oracle 2513 -> 2575 / 2575
  (eval_surf kind 0 and eval_nurbs as the DLL computes them, the projection's arithmetic order from the disassembly, MaNURBSpline::Length
  per knot span). Open: MaCubicSpline::Length (class 8 grid length, segment by segment through MaCubicCurve) NOT PORTED; whether Mastercam's
  undo removes a view find_view_k added NOT DECODED; the view-less first piece end (uninitialised view) NOT DECODED (refused).
- O3 Interfacial: CurveXferMC curve-file writer + the mcmsurf reader side (HBE, chains) for Turn Profile; then the
  Silhouette Boundary kernel (interfacial11 Silhouette::Run) on the ported Shell2D. App commands stay greyed (N11-5).
  Part 1 DONE (interfacial.md 9): curve file writer ported byte for byte (viewer/ifk/ifk_curvexfer.c, only closed chains
  are written), ReverseTransformation on IFF files, tools/tests/ifk_curvexfer_test.sh ALL PASS. Found: the kernel
  port's cone / sphere profiles (apex as two nodes) give 0 curves in the file - check with the oracle.
  Part 2 PARTIAL (interfacial.md 10): ported the STL box, axis frame + both axis transforms, the HBE back-transform,
  mucbTransformStl, the HBE records before the filter (tools/tests/ifk_mcside_test.sh ALL PASS). NOT DECODED / not
  ported: wr_stl's tessellation (make_surf_polygons, question IF-1), the arc fitter CSpinBdryArcFitter (mcmachinedef),
  the offset check, HBE -> chains (FUN_180232540, decoded in outline). The HBE filter is ported (ifk_hbefilter.c).
  Part 3 PARTIAL (interfacial.md 11): Silhouette::Run decoded in outline; input stage ported (ifk_silhouette_in.c,
  tools/tests/ifk_silhouette_test.sh ALL PASS); the i11 Shell2D is a different build (not reusable as is); the core
  PlaneCut (interfacial03) decoded in outline only - next FUN_18001bde0 (exact layer), then arrangement, Polygon2D,
  Stitch2D. No ifk_silhouette_run yet.
  2026-11-12 continuation: PlaneCut stage A DONE (interfacial.md 11.7: exact layer, cut, intersections, split;
  viewer/ifk/ifk_planecut.c, tools/tests/ifk_planecut_test.sh ALL PASS); stage B DONE (11.8: prune, angle sort,
  chains, test points, i11 classifier + Shell2D side path, Polygon2D nesting, curve tree ifk_pc_run; box, standing /
  lying cylinder, sphere, box with through hole PASS); stage C DONE (11.9: Stitch2D loop, zero-width
  pairs, Surfer binary tree curve writer, ifk_silhouette_run, ifk_cli --silhouette; end-to-end box / cylinders / sphere /
  box with through hole kept as child: ALL PASS). Silhouette Boundary kernel ported end to end; not checked against
  Mastercam (N11-5), commands stay greyed, IF-1 open. Left: oracle, mcmsurf chains (FUN_180232540), CSpinBdryArcFitter,
  MCCE offset check, make_surf_polygons (IF-1), Surfer::ReadFile call order.
  2026-11-12 IF-1 / N12-1 (interfacial.md 12): wr_stl -> surf_to_stl ported (viewer/ifk/ifk_surfpoly.c, ASCII STL text
  with MSVCR120's %f) on plm_mesh = make_surf_polygons for untrimmed NURBS (kind 0); DLL oracle tools/peloader --stl /
  --crtf (msvcr120.dll's own sprintf), tools/tests/ifk_surfpoly_oracle.sh (533 parts: 1060 / 1066 runs byte-identical, the rest 4 degenerate surfaces; %f same
  as msvcr120.dll on 2.68 M values) and ifk_surfpoly_test.sh
  ALL PASS. 2026-11-12 trimmed surfaces (kind 10 over a kind 0 base) ported (interfacial.md 12.8, viewer/mcpartline_trim*.inc):
  KINDS=10 oracle 822 / 846 corpus runs byte-identical, 14298 surfaces identical, 36 refused, none different; self-test
  with 6 synthetic trimmed surfaces. Left (NOT PORTED): the trim repair FUN_180586a50 / was_bad_trim_found (30 corpus
  surfaces: TUTORIAL 9, CURVE 5 AXIS SURFACE EDGE ...), the other kind 10 refusals of 12.8, kinds 1 2 4 6 7 8 and kind 10
  over them (1305 over kind 1, 391 over kind 2), degenerate surfaces (stale stack values: NOT DECODED), solids (Parasolid's
  PK_TOPOL_render_facet: third-party, not decodable here) and selected solid faces (ps_face_to_mc_surf, Parasolid),
  polygon meshes, binary STL; wiring into the app.
- N12-3 Turn Profile / Silhouette oracle: DONE 2026-11-12 (interfacial.md 13). `peloader tpiftools.dll --tpif spinsil|silbndy`
  runs Mastercam's own chain (tpiftools -> IF*.dll -> interfacial*.dll, real MFC120 / MSVCR120 / MSVCP120 from
  <windows> read only) the way mckernel does; the vendor security step runs unmodified and passes (GetCurrentHwProfileA
  answered like Windows). tools/tests/ifk_oracle_test.sh: 60 / 60, 400 / 400 and 600 / 600 random cases byte-identical
  curve files or the same fault / error (Turn Profile: cone / sphere apex fixed). Port fixes: walk-end union roots,
  EdgeQuadtree groups, pool address order (ifspinsil + interfacial11), Shell2D+0x68 commit / rollback, the NULL-vertex
  fault. Left (not decodable): pool blocks beyond 1000 records are assumed at increasing addresses (the heap's choice;
  seen differing once without output effect). Commands stay greyed: IF-1 (kind 0 done, trimmed surfaces / solids left), mcmsurf chains, arc fitter.
- N13 Turn Profile / Silhouette Boundary, the last Mastercam-side steps (interfacial.md 14, 2026-11-12, branch
  worktree-agent-a59d4620b6a5d421a): dialogs decoded (mccad 0xc14 / 0xc16: fields, defaults, hmmSiluetBndyDat mapping, run
  paths); HBE -> curves / chains FUN_180232540 ported (viewer/ifk/ifk_hbechain.c; oracle `peloader mcmsurf.dll --mcms`
  840 / 840 same structure, 592 byte-identical, rest = spline coefficient rounding of mcspline_fit.c); the HBE filter
  checked against the DLL (1000 / 1000 identical); FUN_18020c830 / FUN_18020cb90 ported (ifk_tpconv.c, 800 / 800); the
  offset check's bound ported (ifk_offchk.c), MCCECalcOffset's result NOT DECODED (question IF-2); CSpinBdryArcFitter
  decoded and ported (viewer/ifk/ifk_arcfit.c; oracle `peloader mcmachinedef.dll --arcfit` 3800 cases, 0 DIFF, byte-identical
  with msvcr120's libm; parts with solids NOT DECODED). Commands stay greyed; the wiring list is interfacial.md 14.8. Left: IF-2, BreakAtMidpoint's
  helpers (14.6), Slice (slice_sel_surfs_with_a_plane), the Parasolid fitter path (out of scope), views other than Top
  (ported, not oracle-checked), make_spline_coefs bit-exactness, chain_ignore_depths' run-time source.
- Main session: bbox_cyl_pushpull investigation (done: explained, see below); merges.
Rules: no improvisation (cite decompile), Mastercam default rule; open questions go to "Waiting on the user" below;
Ghidra only under `flock ~/mastercam-re/ghidra.lock`, builds only under `flock ~/mastercam-re/build.lock`.

## Where we are (2026-11-09, end of session; branch toolpaths2, merges up to 5e4562f, nothing pushed, nothing running)
- Last round: the user's list "lets get 1 through 5 working". Only part of it generates toolpaths:
  1. Pocket types: Constant Overlap Spiral and Facing generate. Island facing, Remachining and Open pocket not attempted (the
     work stopped there, nothing blocks them). Still refused: Facing with finish passes, roughing angle or chained entry; Morph Spiral.
     Stored BLOCK op stays refused (Keep tool down finish 0x126a); its helix centre does not match. (0x126a ported 2026-11-12, K: BLOCK generates.)
  2. Stock Model: stored ops are read only. Generation, Restmill and the 3D rest options stay refused (closed Interfacial kernel).
  3. Flowline, Restmill, Hybrid, Leftover, Parallel Steep / Shallow, Constant Scallop: nothing ported, sizes in surf_classic3.md 8.
  4. 3D accuracy: half tolerance on HST ops (FUN_18006dd68), Waterline contact angle filter (FUN_18040b6f0). Raster
     PARALLELSTEEP is the only clear gain. Inch Waterline cut length about 10 % short, shallow-floor rings still NOT DECODED.
  5. Contour: several result rings and near-180-degree corners done. General 3D contour and Subprogram not ported.
- Also done before that round: Contour Remachining with gouge check and the Entry / Exit combo; Extrude's target search (step 6,
  extrude.md 1.1; not compared with real Mastercam; the user's failing second hole was not reproduced from its log, so the log
  now prints `target search:`).
- Checks on the combined build 5e4562f: Contour 149/149 (median 0.004, worst 0.010), units ALL PASS, solid_compare 2/83 (ex_save /
  ex_save_edit, PROJECT-FORMAT 2 line, not from this work), op_compare 50/545 = baseline 46 + the 4 references below.
- Open questions are at the end of this file, under "Waiting on the user".

## Where we were (2026-11-06, end of session; branch toolpaths2 at a5b0366, nothing running, no claude worktrees)
- Checks at a5b0366: wf 0/225, srf 0/37, note 0/32, draft_ui OK. Earlier this session: units ALL PASS, nesting job 125/125
  (seeds default / 29 / 31 / 37), nip 67/67, rect 179/179, roundtrip 204/205 (NAKTO op 3 = Area Mill core roughing from
  outside, NOT DECODED), tool_compare 76/226 (same as the baseline before the merges, not investigated).
- Done this session (2026-11-05 / 06): nesting TrueShape glue, Delete (per-instance records), Details (rpx report), trims,
  level visibility, chains in any view, Esc (nest_ts_cancel via queued input); refusal of parts newer than 2018 (FV-1 / FV-2,
  MCAM_REFUSE_MAJOR); soft locks in every function panel / dialog with their decoded clearing points (fixed: a typed line
  length did nothing); radius zeroed at the commit, Simple surfaces made at once, Arc Endpoints / Arc Tangent candidates,
  Line rubber bands, Block / MFC lock buttons, Point Dynamic Along; mcce_dyn.c tess-cache use-after-free; oracle.py record
  types through fileop. Decisions NS-2, FV-1, FV-2, PJ-1 in user_decisions.md. X9 parts verified to open.
- Waiting on the user: the path of any X9 part that fails to open.
- DONE 2026-11-07 late (D-3, worktree branch of toolpaths2): Dynamic links in the bUseCtourGuide = 1 branch (connectors 4 / 5,
  strategy 5, link smoothing FUN_18004a070 / FUN_180049950 / FUN_180045c40, self-crossing test / loop removal) made the default,
  islands grown one by one (mcce_grow_islands); route() and arc_line_arc decode fixes; connector unit oracle
  tools/tests/mcce_ct_oracle.sh (peloader --ct). Dynamic oracle 0.204 / 0.546 -> 0.194 / 0.643 (ops 3 / 51 / 42 worse: the
  polygonal region sets fail FUN_1800458c0's 1-degree test, see mcce_port_plan.md handoff). Needs the user: keep the default
  or MCCE_CTOURGUIDE=0 until exact region sets. ANSWERED 2026-11-11 N11-2: keep the new default.
- DONE 2026-11-07 (user: yes to porting the greyed Trim / Break options): Break mode of Trim Break Extend (all 7 types) and Trim
  Many, Trim Many's Opposite side, Break Many's Create lines / Tolerance / Exact / Rounded / Keep / Blank (Blank undoable + saved as
  sel bit 0x100) - from new mccadutil dumps (trim_123, FUN_180049560, FUN_18004e810, FUN_18004c290..., formats/wireframe.md
  "Break mode"); 21 new wf scenes (trim_brk_*, trimmany_brk / _opp, breakn_*). Follow-up the same day: FUN_18005fa20 decoded in full
  (Trim 3 entities' break exact), the original's record kept by the piece Mastercam rewrites (mod_pair; every one-piece trim too),
  Break Many live (codes 4-9 rebuild, Esc commits) and on splines (creep / break_spline_const_error). Open: Break Many Tolerance +
  Create curves on a spline (break_spline_arcs biarcs + FilterEntities -> mccore FilterChains, the Filter command) - left as it is.
- Open, NOT DECODED / not ported: Point Dynamic with Along locked before the pick, Point Dynamic Reselect, Block re-asking
  a picked size; Line Endpoints Tangent mode; nesting Sort / Customize / Drag, report images, Nesting.xml writing, chains on
  a non-existing view plane, output when the Cplane is not Top, Level Manager main-visible check boxes; note hover speed;
  the 76 tool_compare differences; contour leftovers (Remachining, 3D contour, Tab Cutoff, Subprogram, MP spindle override,
  arc filter, Infinite look ahead); re-bless the 45 old screenshot goldens; unmapped icons.

## In progress
- 2026-11-08 Remachining: Lead In/Out gouge check ported (FUN_1801c46f0 / FUN_1801c3d70, contour_remachining.md), hand-worked tests only; the combo follows Mastercam's order (Ramp, Remachining, Oscillate).
- 2026-11-08 Extrude target search (FUN_1800d8d70, extrude.md 1.1 step 6) ported: cuts drawn on a face aim into the body. Touching = no clash is an inference (PsClashTest type 0); no real Mastercam comparison. Left: Automatically determine operation type (FUN_1800da050). `ex_save` / `ex_save_edit` references still lack the PROJECT-FORMAT 2 line (not from this change).
- CAD import (2026-11-08, worktree branch of toolpaths2, formats/cad_import.md, tools/tests/cad_compare.sh): File > Open of DXF (entity mapping from mcautocad.dll: points, lines, circles, arcs, ellipses, LW / 2D / 3D polylines, splines, INSERT), STEP (OCCT reader; solids as imported BRep solids: shaded, chained, 3D drive, saved in the .mcamui project; not writable to a Mastercam part: the save warns), IGES (OCCT reader, surfaces and wireframe, never solids as in Mastercam). Open: File > Merge of CAD files, the Options dialogs (500 / 1500 / 701) and DWG.MSK / IGES masks, DWG, binary DXF, text / dimensions / hatches / 3D DXF entities (skipped and reported), STEP / IGES file colours and levels, repair / stitching, a Parasolid XT writer for imported solids.
- Notes (2026-11-05, worktree branch of toolpaths2, formats/drafting/notes.md): part notes / labels / leaders as entities (display,
  pick, delete, undo, attributes, byte-exact save, memory-table writer), Drafting > Note Single / Multiple, the creation API
  `viewer/draft_note.h` (`mcam_note_create`; Geometry Nesting's sheet-name note uses it, its part labels are not ported). Next: labels and leader-only notes (CalcLabel's leader branch, add_a_leader,
  df_adjust_ldrs; FUN_18003caa0 / FUN_1800408f0); editing a note (dialog with DAT_18142cb14, FUN_1800457f0 drag-edit); window
  selection / Transform / Copy-Paste / Analyze of drafting entities; dimensions as entities (selection / delete of part D_ID records);
  Load File / Add Symbol / Properties of dialog 452; which FunctionPanel button posts ids 23 / 24 / 25 (the panel is greyed during Note meanwhile, NT-2; notes.md 5.4).
- Geometry Nesting (2026-11-05, formats/nesting_addin.md §12): the property sheet / Configuration / Results, Add rectangular /
  chained parts and sheets, both engines (bit-exact ports), the write-back with undo, Optimized pairs, the sheet-name note,
  Results Copy / Copy Blank, scenes xf_nest_*. Next (each decoded / scoped in §12): part labels (get_ext + the label TrueShape
  run of FUN_180041d00), .nstx sessions / Save-Load defaults (86 [PARAMETERS] keys: a full CParam block; the .defx data folder
  is a user decision), groups / user clusters (FUN_180074fb0 NOT DECODED), multi-select lists, trims, reports, Details / Delete
  / Sort / Customize / Drag / Preview. Level names on OK: done 2026-11-05 (the level table, mcam_load.h).
  TrueShape glue review (2026-11-05, formats/nesting_engine.md §9) done: the DLL's C++ catch blocks (gen/eh.py), message
  arguments (gen/vaargs.py), cvt64 / pointer-cast generation checks, tmpnst cleanup, resource release on ts_fatal, per-sheet
  run arrays. Open: Esc during a run in the app (the engine hook `nest_ts_cancel` exists; the run is synchronous on the UI
  thread - needs an event-loop decision).
- Surfaces tab (branch `surfaces`, formats/surfaces_tab_plan.md, decisions S-1 / S-2): groundwork done (surfaces are entities: pick,
  highlight, delete, undo, save, project), Normals > Change and Create > Revolve live; mcsurf_build.c = put_surf_in_db's header fields
  (corpus-checked) + the line / arc / NURBS-curve -> NURBS and revolve conversions. Next: Extrude / Draft / Loft / Flat Boundary
  (decomp/surfcreate/create_cmds.c), then Normals Set / Orient, Untrim, Remove Boundary, Split, Offset, then From Solid (OCCT).
  Normals > Set, Modify > Untrim, Modify > Remove Boundary DONE 2026-11-17 (branch worktree-agent-aab335cc2a38c285b, cui_srfedit.c;
  surfaces_tab_plan.md "D."; refused / NOT PORTED: Untrim on non-NURBS or knot-regenerated bases and ratio >= 1000, Remove Boundary
  of an outer touching the base range (FUN_1808abe50); surfaces used by operations follow through tp_substitute_ent, decision S-9). Orient (Ma* coedge library), Split
  (FUN_180317570, make_constant_spline, trim_surf_to_cons) NOT DECODED, greyed.
  Create > Offset (OffsetSurfaces) DECODED + PORTED 2026-11-18 (branch worktree-agent-a08eb39b79019d4bc; surfaces_tab_plan.md "D. Offset",
  decomp/surfoffset/): offset_a_surface records (kind 8 + the trimmed copy), Copy / Move (Move blanks the original: delete_ent(.., 1, 1)),
  distance changes in place, Single flip / Cycle / Flip with the arrows, one undo step; scenes srf_offset_*. Open: Calculate and the
  analysis after a single flip (MinMaxPrincipalNormalCurvatures over GenGrid(chord) = TessellateUntrimmed's chord mode, not transliterated;
  greyed), the 0x8000 attribute events, the distance validator's limits; no DLL oracle of offset_a_surface's record yet (peloader: needs
  the DB stubs get_ent_from_eptr / put_surf_in_db / store_ent).
  Simple > Block / Cylinder / Sphere / Cone / Torus, Create > Flat Boundary, From Solid, Loft (Ruled / Lofted) DONE 2026-10-28 (Loft open: make_spline_coefs_closed, surface types 0 / 1) (surfaces_tab_plan.md "B. Simple"; the radius / height / corner picks decoded; open: the
  axis Line / Two points modes, the solid preview).
  Create > Draft and Create > Extrude PORTED 2026-11-17 (branch worktree-agent-a4f5fddf971b042b2, surfaces_tab_plan.md "C. Draft" /
  "C. Extrude"; scenes srf_draft, srf_extrude*). Open: Draft's Plane mode (get_plane_info / surf_slice) and the drafted-spline
  approximation FUN_1802647b0; Extrude's spline profiles, Axis Line / 2 Points, both directions with a taper (rm_ccomp_pock refuses the
  toggled-ccw bound; decision S-4); v_z_3d = (0, 0, 0) applied to view_k / eval_draft (decision S-3). Swept: decode
  started (surfaces_tab_plan.md "C. Swept"; the Ma* sweep classes in mccore NOT DECODED).
  Gaps filled 2026-10-28 (surfaces_tab_plan.md "Gaps filled"): exact NURBS derivatives, trimmed extents, spline profiles, the revolve
  arrow, window tests, Transform of kinds 0 / 1 / 10. Open: Copy / Paste, Analyze, AutoCursor, Bounding Box extents of surfaces, the
  curve-dependent kinds' Transform, a general-selection window (no entity type has one yet).
  Modify > Trim / Extend (2026-11-17, branch worktree-agent-a2cd0ddd2b8f13dcc, surfaces_tab_plan.md "E.", formats/surfaces_trim_decode.md):
  Extend PORTED for NURBS surfaces (mcsrfext.c + cui_srftrim.c G_SEXT; peloader --ext oracle 580 / 582 identical, scene srf_extend);
  NOT PORTED: Extend To plane / trimmed surfaces / other kinds / op substitution.
  Trim (2026-11-17, branch worktree-agent-a8054b51bc057f690): the Ma face-trimming core ported (viewer/mcsrftrim.c; peloader --trim
  oracle 203 / 209 + 241 / 242), Trim to Surfaces ported (kernel --trimss 118 / 141, the rest the surf / surf march; command G_STRIMS,
  scenes srf_trim_surfs / srf_trim_surfs_checks), Trim to Plane's build done but its button greyed until get_plane_info (S-11; scene
  srf_trim_plane through the test step trimplane), Trim to Curves NOT PORTED (~9 900 lines). Open: questions T-1..T-4
  (surfaces_tab_plan.md "E. Trim"); the surf / surf march differences on edge-tangent pairs (mcsrfint.c); Reselect buttons; unclamped
  regenerated-knot NURBS (mi_regen_knots).
  Surf / surf march + knot regeneration (2026-11-17, branch worktree-agent-a8e61ad47e05a14ae, surfaces_trim_decode.md "The surface /
  surface march matched to mccore"): EvaluateSrf (Bezier patches), PointMinDist, the hunting grid (GenGrid / TessellateUntrimmed),
  GetClosedInfo, GenCrvPoints fallback matched; march-level oracle (peloader --trimss/--trim TRIMSS_TRACE + ssmarch_cmp.py): every
  computed step identical on 792 / 792 fixed cases. MaNURBSurf DB constructor + knot regeneration exact (mi_db_knots; peloader --nbsdb:
  1969 records + the Simple Cylinder identical); the Trim command no longer refuses unclamped surfaces. Open: the trim-core differences
  after an identical march (TrimToLoopsNew / GenUVKeepPoint / part-loop facets; ~6 + 6 + 5 + 2 cases), MaCurve::BinaryPointsAtTol
  (iso_hunt) and GetEdgeInfo (is_edges) still approximations (18 regenerated-surface plane seeds differ), mcsrfcrv.c pm_tess, Curve
  Slice / Curve at Intersection loaders without the DB constructor, Simple Cylinder trims (DLL and port both > 60 s on several pairs).
  [All of these done 2026-11-17 (branch worktree-agent-a7c97f289ddaf4224, surfaces_trim_decode.md "The trim core matched to mccore"):
  trimmer-level oracle (TRIMSS_TRIM Z lines, tools/tests/trimcore_cmp.py); the four fixed sets identical in every trimmer step and output
  (0 / 209, 0 / 209, 0 / 242, 0 / 242, 0 / 60, 0 / 81) and in every march evaluation incl. the plane seed search; TRIM_DBCTOR sets incl.
  regenerated surfaces 0 / 594 planes, 0 / 90 pairs; BinaryPointsAtTol + GetEdgeInfo checks exact; Curve Slice / Curve at Intersection
  through the DB constructor; mcsrfcrv.c's hunting grid = GenGrid for NURBS; CurveFit's stale hunting points reproduced (the cylinder
  pairs' fitted curves = the DLL's). Open: IsSmooth / GetU/VListAtSlopeDiscontinuities (mi_pieces: iso-tangent approximations, the DLL's
  flow-line NURBS HasSharpNodes not ported), TessellateUntrimmed mode 0 (flow lines by chord height still pm_tess), the surf / surf seed
  search's evaluation order of the second surface's edge info vs its box (seeds identical), mcsrfcrv.c Curve All Edges' own loop
  normalisation (division, not UVTransform).]
  Extend Trimmed Edges ~4 500 lines (MaTrimmedEdgeExtend, MaPolyLine::Offset). Decisions S-10..S-13 (user_decisions.md; was E-1..E-4 in surfaces_tab_plan.md).
- Nothing running (2026-10-27 end of day). Master is clean: the full regression run_m27b (tolerance constants + XT pass 2)
  finished identical to the blessed baseline (op 0/406, stockplanes 0/125, scenes 1/46 = the clock shot, every oracle unchanged).
- Next (agreed with the user, not started - the two agents were stopped before writing anything):
  - Surfaces tab (all greyed today): A) groundwork for new surface records (session, display, undo, save as SURF_ID via
    put_surf_in_db) + Simple (Cylinder / Block / Sphere / Cone / Torus) + Extrude / Revolve / Loft / Draft / Flat Boundary;
    B) route Surfaces buttons by FT name (BUG: Surfaces > Offset matches the wireframe Offset label and runs it), Normals
    (Change / Set / Orient), Untrim, Remove Boundary, Split, From Solid; later Trim / Extend / Fillet / Blends / Fill Holes / Net /
    Fence / Sweep.
  - Drafting leftovers: Smart Dimension (its state machine, determine_hvp, FUN_1800442d0), Ordinate Window (dialog 2533 + set
    builders), the panel Mode change, '~' formatting pick; Align reproduces 20 / 60 CAVITY positions (create.md 9).
  - Thread Mill new-op clearance 50 mm: as Mastercam (user rule 2026-11-04, user_decisions.md).

## Done 2026-10-27 (session master-cam-reverse-04)
- Engrave: spline text chains (E_SPLINE, brk_spline_const_err, 9e78634); app path oracle recorded. Left: the engine port's
  vertex-density sensitivity (coaster op 2 16 / 17 passes).
- XT -> OCCT translator: pass 1 b5384b5 (24-byte h fields, cone / sphere / torus / offset / rolling-ball blends / intersections;
  343 / 821 solids) and pass 2 e54ffbc (tolerant edges, sheet bodies, swept / spun, periodic B-splines, speed; 449 / 821).
  Left (xt_occt_fixes.md 8): B-surface extension (13), fits > 10 um (8), RIB CUT / HS AREA (3), schema 28101 skipped (172).
- Drafting tab: Horizontal / Vertical / Parallel (41b25dc), Circular / Angular / Baseline / Chained / Tangent / Perpendicular / Point
  and Ordinate Horizontal / Vertical / Parallel / Add To Existing / Align with the shared drafting pick (merge 854d7a9).
- Thread Mill: Mastercam's bnci_threadmill ported (050624c), 307 / 307 NCI records; B8-6 was an oracle unit bug.
- Units: one IsMetric rule, File > Configuration units switch with the scale question (merge 854d7a9); system tolerances from the
  configuration items 1000-1008 / 7004 and ops after a switch per Mastercam (7d283bb); the uncited tolerance constants traced to
  the system tolerance, Analyze used the chaining tolerance (49591ae).
- Fixes: defaults loaders thread-safe (the op dialog's worker thread) and drafting on a new part metric (fa49bd4).
- Tests blessed for the intended units changes (91a8960). Oracle results recorded in tools/tests/oracle_history.tsv.
## Next in the main tree (after the dirty-op work)
0. (DONE 2026-10-24, branch `medax` at 81178b9 merged into master on top of dec2, formats/opdlg_port_audit.md 21) **Medial axes:**
   POCKMDL for Minimize tool burial (Q3), the CalcMedAx port and the Dynamic entry methods 1 / 2 / 4 (Q4); helix centre behind
   MCCE_MEDAX=1 (default off). Left: 2D engines below.
1. (DONE 2026-10-24, formats/opdlg_port_audit.md 20) **Decisions batch dec2** (user_decisions.md 2026-10-24):
   - Q1 remove the Dynamic first-record plunge (rebaseline the 37 Dynamic ops after the NCI check);
   - Q2 "unverified" note on Peel / Dynamic Contour;
   - Q5 Peel "nothing to cut" + the 0.17 mm wall case;
   - Q6 honour the stored arc filter flag;
   - Q9 fix the stored XZ arc read;
   - Q29 cutter comp Control only;
   - Q30 D<tool#>;
   - Q32 refuse stored control-comp toolpaths.
2. (DONE 2026-10-24 on branch `defs`, formats/opdlg_port_audit.md 22) **Save parameters to defaults file** (Q19; pristine copy, dated
   backups, ours "Reset defaults file to original" / "Restore a dated backup") and **Preview toolpath** (Q20). Left (NOT DECODED, refused
   with a message): ConvertFile (older defaults file), delete + append (oplib_save / FUN_1800697e0) for a version mismatch or a missing
   op type, the X9 -> 2017 op struct update (so stored ops of the user's .mcx-9 parts cannot be saved as defaults - decision S-D1).
3. **Follow-ups the user accepted on 2026-10-24** (user_decisions.md):
   - S-D1 DONE 2026-10-24 (opdlg_port_audit.md 22.8: X9 op update + the delete + append branch);
   - D2 port Mastercam's feed / speed NCI patch;
   - N1 ask before overwriting an existing NC file - DONE 2026-10-26 (align_status.md). Started 2026-10-24 (decomp/n1/): the Post dialog is mcpost.dll CPostDlg
     (mcpostres dialog "Post processing": NC / NCI file checkbox + Overwrite / Ask radios); OnInitDialog / OnOK map post_settings
     (mccore global 0x1812ab000, filled from the control: CCNCControl::GetPostSettings, mcmill FUN_180023fa0 l.26246) bytes
     [0] NCI save, [1] NCI radio (1 = first button), [2] NC save, [3] NC radio, [6] / [7] / [8] / [0xc] / [0xd] other options;
     the NCI file's prompt is mccore FUN_1800a1800 (mccoreres 7277 "File already exists, overwrite?\n%s", caption 5028);
     batch posting (CBatch::PostSelOps FUN_180023500) uses [7] / [2] / [6] + edit_filename. Left: the interactive post's use of
     [3] (FUN_180023fa0 packed copy local_cab8), the radio order in the dialog template, the control page offsets of
     GetPostSettings;
   - SIM1 read the user's MastercamSimulatorDefaults.xml, read-only.
4. **Solids tree / OCCT integration** (Q34, below).
5. Later: M2 / M3 Dynamic entry methods 4 / 5 (FUN_18015b2e0 / FUN_18015c370, FUN_180132200); N3 drill canned-cycle NCI
   records (bnci_drill 80 / 81 / 82).

## Queued
- **planes / stockplanes goldens stale (2026-11-12, formats/planes_archive.md 7.1)**: 30 / 43 and 16 / 125 differ already at fd4aed0 (PROJECT-FORMAT line, GEO order, Tab Cutoff greyed; the N9-27 pl_open_* scenes never blessed). Review each and re-bless; the wf / srf / chain / solid suites do not cover these.
- **Transform tab leftovers (2026-11-05, formats/wireframe.md "Transform")**: Roll's "Select an arc" (721: the roll origin / view V for a
  Cplane other than Top and Matrix33 AlignAxis / IsValid / Plane::CalcAndTransform not ported), Roll Type Splines (make_spline_from_pts /
  create_nurbs_from_points: no spline fitting here), the Roll cylinder outline and Ctrl seam split; Distribute's box reference lines and the
  edge pick 1251 (XfSaveReferenceLine's other arguments NOT DECODED); xform_del_duplicate (config 10019, off in the user's configuration:
  needs del_duplicate = Home > Delete Duplicates, not ported); AddMCView for an arc rotated / mirrored into a plane no view has (needs new
  planes saved with the part); the WfMgr hover highlight of Remove Item; prompt 3930's string (MFC resource-chain order).
- **Units (formats/units.md, branch `units` 2026-10-27)**: the switch (File > Configuration's Current: combo, `units:` step) and the
  IsMetric() helper are in. Left:
  - drafting (other agents' files): cui_draft.c `dr_metric` and scene.c draft_build's config 15007 to use `mcam_is_metric(&F)`;
  - DONE 2026-11-05 (formats/wireframe.md "Transform"): splines / surfaces scaled on "Scale current part". Left: solids (sld_xform_solid)
    and sld_scale_sldBodies on "No" (this program's solids are Extrude features);
  - the "Units / All settings" prompt on open (CCfgChgDlg, mccoreres 1112) and the Units-only item list; other configuration files
    (Open / Save / Merge, the remembered per-units file);
  - wireframe panel values: per-units defaults + stored keys instead of RegisterVariable's ratio conversion of the session value;
  - DONE (branch `unitstp`, units.md 8 / 9): the system tolerances 1000-1008 + 7004 read from the current configuration (McSysTol),
    re-read on a switch / open; ops after a switch decoded (not converted; dirty only via the scaled geometry). Left from it:
    surf_step_chord_dev (1007) has no reader yet; the uncited 5e-5 / CAM_CHAIN_TOL sites listed in units.md 8 (cam3d_srough.c `tol`,
    cui_analyze.c, draft*.c TOL); File > New's ReloadConfiguration (init_db) and an open in other units do not re-read the front
    end's other configuration items; the save of an op computed in the other units is refused (Mastercam keeps its old NCI and
    1016 +0x44: write gen_units there); drill points made here are not associative (not scaled / dirtied by the scale);
  - cam_defaults_base's mm-only built-in values (used without a defaults library);
  - a saved part's copied records keep their old +0x4d units stamp.
### From the defs branch (opdlg_port_audit.md 22.6)
- DONE (22.8): the X9 -> 2017 op update and lib_save's append / lib_delete. Left: ConvertFile; init_operation_db's branch (redo_op_idn);
  lib_delete's reference counts (header +0x6c >= 1 / ASSOC 2 records - not in the shipped files).
- How Mastercam resolves a relative OpDfltsFile ("Ops\\Mill_MM.mcam-defaults", file_exists(path, 1)) - ours falls back to the reader's file.

### Solids tree to Mastercam's level (OCCT integration, occt_xt.md section 7, solid_history.md)
1. DONE 2026-10-24: the stored history of the part's solids in the Solids manager (mcam_load keeps each solid's ops block,
   formats/mcamui_dialogs.md 4). Left: Toolpath op names (sld_get_toolp_name), dirty / invalid overlays, tool solids of Boolean ops.
2. Move Extrude onto OCCT replay.
3. Save new solids (XT + history) into the .mcam (mcam_write.c), and keep the history block on load (mcam_load.c).
   This needs a real Mastercam 2017 open test of proof files A-D on Windows (occt_xt.md section 6).
4. Solids manager items, all greyed today:
   - Regen Solid, Remove History, Suppress, Highlight / Auto-Highlight, Rename, Duplicate, Attributes, Check,
     Recreate trimmed bodies;
   - rollback with the stop-op marker, drag to reorder features;
   - dirty marks on solids.
5. Decode Boolean / Fillet from sample parts, then the remaining Solids / Model Prep commands.
6. Related:
   - Build the Surfaces tab.
   - Fix Surfaces > Offset starting the wireframe Offset (needs a guard like Extrude's in mainwindow.cpp).
   - Replace the xt_mesh.c display mesher with OCCT's (it is wrong on 30 buckle solids).
   - Fix xt_parse.py:82 (unknown node on about 17 sample files).

### Contour to 100 % (user 2026-10-28; formats/contour_100_plan.md)
- 2026-11-08 MCCECalcContourOffset port handles open chains (Retrace, forward-half pieces) and the one-closed-piece input split; Contour uses it for open chains, with look-ahead off too (branch work: D-4 / D-5 record closed chains only, needs the user's word). Left: the boolean for several result rings (corpus 3), FUN_1800cb710 beyond a retrace, FUN_1800cd350 tiny pieces.
- 2026-11-08 Pocket Constant Overlap Spiral (method 1, cam2d_pock_ovlp.c, mccore-pbmap oracle, BLOCK NCI cutting records exact: decompile-vs-NCI ring-closing record conflict flagged) and Pocket type Facing (no stored op: pk_face_*; combo 2101 selects it, its panel 3002 is not wired) done. Left: Island facing, Remachining and Open pocket refused (formats/pocket_engine_b6.md "Constant Overlap / Facing"), Facing panel 3002 wiring, Facing finish passes, the Keep tool down finish (0x126a) refused which also stops BLOCK from generating in the app.
- 2026-11-08 MCCECalcContourOffset port is the default for closed chains (D-4), gap closing / tail / loop removal in; Infinite look ahead off live for closed chains (D-5); Horizontal Area live with seven extra pockets (T-4). Next: open chains (FUN_18011d800 Retrace, FUN_1800cd350 end caps, FUN_1800cd7e0), the 23 of 781 corpus refusals left; then Remachining and general 3D contour.
- 2026-11-08 MCCECalcContourOffset partial port (viewer/mcce_ctour.c, closed chains, DLL oracle 0 mismatches, MCCE_CTOFF=1 off by default, formats/mcce_ctour_offset.md lists the remaining kernels: open chains, loop removal, gap closing, tail).
- Tabs (2026-11-04): automatic, manual (Position...), Full tabs, square point (make_geo_tabs) and the TP kind 30 write done; left:
  the writer's Ramp branch ported (Full + Ramp; a ramp whose carried height at an entity end leaves the move's line is refused -
  keeping the ctour entities through chain_offset would lift that), the square-point spline walk (needs length_ent / proj_pt_on_ent for splines). Overwrite edited tabs
  ported (per-tab values + edit_operation's rewrite, 2026-11-04); Infinite look ahead off refused (MCCE flag 0x20000 NOT DECODED). Chains in chain-number order (all op types). Accuracy: all 149 stored contour ops within 0.05 mm
  (exact corner start, lead gouge check). Then:
- 2026-11-04: Tab Cutoff (after each / after all chains), Ramp contour (Angle / Depth, closed chains), Oscillate (Linear), 3D types on
  flat chains done. Ramp/Oscillate leads (FUN_1801fcc30) and Multi Passes done. Left: (Ramp open chains / Plunge / One way and Oscillate High
  speed done), Remachining (FUN_1800550c0 + rest material), 3D contour (3D chains), Tab Cutoff as a separate op (opcode 0x82),
  (Use entry / exit point + point chains done 2026-11-04); post-side
  (as Generic Fanuc 3X Mill.pst, user rule 2026-11-04): Subprogram, MP spindle override, coolant aux; arc filter smoothing; Infinite look ahead off.
- 2026-11-07 "full contour options" (contour_100_plan.md 3): DONE 2D chamfer with Control / Wear / Reverse wear, Ramp / Oscillate with
  them (the lap writer's cc codes), Multi Passes spindle override (decoded: Mastercam writes nothing for it - live, no refusal); Tab
  Cutoff with Wear / Reverse wear now refused (its codes by record not placed). Left, each with what was found (plan 3): Remachining
  (mccore pbmap engine + rest material), 3D contour (connect_3d_moves; FUN_18005ef90 read, no oracle), Infinite look ahead off (MCCE
  0x20000), Subprogram (NCI 1018 / 1019 read; FUN_180045770's sub form + the post side), Tab Cutoff op 0x82 (dependent op creation
  FUN_180044170 read), Smoothing (PointDistribution_3D FUN_18093bcf0 located), Full tab over the path start, the ramp-tab / spline
  leftovers, Tab Cutoff with Control (bVar22) / Wear codes; Home / Ref. Points stay per Q16; the greyed pages.
- 2026-11-08 Remachining: ported (source 2 / 0 / 1, leads, dialog 2999; formats/contour_remachining.md status), real mccore.dll oracle 350 cases / 0 mismatches. Open: gouge check on Remachining leads (FUN_1801c3d70) not ported - refused.
- 2026-11-08: 3D contour (FUN_18005ef90 case) + Tab Cutoff op 0x82 done; connect_3d_moves case and tab edge cases left - formats/contour_3d_tabcutoff.md.
- 2026-11-08 MCCECalcContourOffset: several result rings and near-180-degree corners done (formats/mcce_ctour_offset.md 5.1 / 5.2): corpus 781/781, var 4800/4800, synthetic 17964/0/36. Left there: 24 self-intersecting inputs with several closed pieces (the real boolean FUN_180162010), 12 open-chain pieces within 1e-4 (FUN_1800cd350).
- General 3D contour (connect_3d_moves): NOT started; the record contents after the offset are read (formats/contour_3d_tabcutoff.md 1.1); no stored op can check it in the planes we run (5 stored Contour ops have chains not flat in world Z: 4 refused for their tool planes, 1 flat in its tool view).
- Contour Subprogram: NOT ported; engine side read (formats/contour_100_plan.md). Needs a user decision on the LinuxCNC form (M98 / M99, o-word subroutines, or expanded moves). No stored Contour op has it on.
- 2026-11-08 Tab Cutoff op 0x82 follows Mastercam: newest contour op / config 17028 n = defaults op, appended last, own dialog pages
  (3025), Paste / Edit Common / Tool Manager decoded (no child made) - formats/contour_3d_tabcutoff.md 2 "Follow Mastercam".
- 2026-11-08 Smoothing ported for Contour (line chains + lead arcs; chain arcs / rolled corners / other ops refused), Infinite look ahead off
  read but NOT DECODED for our offset: formats/contour_smoothing.md (section 5 = what is left).
- Batches: 1 Tabs (see above), 2 Multi Passes, 3 Depth Cuts, 4 Lead In/Out, 5 Cut
  Parameters, 6 Arc Filter, 7 Coolant, 8 Holder / Toolpath Type / Axis pages.
- Then: the Simulator holder (drawing + collision check; user said build it 2026-10-28).

### Simulation (Q33 approved; window done 2026-10-24 on branch sim, formats/simulator.md)
- DONE: Verify opens a copy of the Mastercam Simulator window (ubvs.dll WPF layout, labels, icons from the user's pack; the
  player run modes / stop conditions / speed / Move List / settings decoded from the managed code); our engine behind it.
- Open: the Options window (options.baml), the Simulator's Backplot mode, the greyed ModuleWorks features (simulator.md 7).
- Verify improvements:
  - DONE 2026-10-28 Compare (simulator.md 8): the tool window, bands, Options, Workpiece / STL target; our vertical deviation.
    Open: the 3D (non-vertical) deviation, Show Model, which entities MachSim sends as the workpiece;
  - DONE 2026-11-14 (user request "the cut area in yellow"; simulator.md 9): cut faces in the cut colour, uncut in StockColor. Decoded:
    Color Loop off -> Scheme[0] = Yellow #FFFF00 (UpdateColorScheme draw mode 12; not MillingFluteColor #DDBB00, the cutter's);
    Color Loop on -> per operation / per tool (ComputeToolColorIndex) Scheme[i % 12]. Color Loop button live; vfcolors test step.
    Open: the position bar's colour intervals (UpdateOperationSlider ColorIntervals), the Options page (Loop by / Loop colors),
    IsTheSameTool (ours: same tool number), which faces ModuleWorks counts as cut (ours: lowered cells + the wall on their low side);
  - solid, STL or cylinder stock;
  - holder collision check;
  - finer resolution for 3D finishing.

### Housekeeping
- tools/gen_opdlg_fields.py does not reproduce the hand edit of cui_opfields.inc's Canned Text lists (6548 3162-3165: flag 1); re-applied
  by sed after each regeneration - move it into the generator.
- golden_lib BLESS=1 with a custom scene list deletes golden files with prefix parts / proj (fix agent 2026-11-05).
- DONE (ef3f42b + e9a6f04, see M4): roundtrip buckle-v8 op 32 "invalid coordinates" heap-layout dependence; medax -Wuse-after-free.
- Note pick still rebuilds the notes document per hover; dragging re-renders all notes (needs a single-note path in scene.c).

### UI bugs (user reports 2026-10-25)
- DONE 2026-10-25 an extruded solid stayed drawn after opening another part: open_part (cui_doc.c) cleared the solids after load_file had
  uploaded the scene - now re-uploaded and re-fitted; solids_clear also drops the solids' edges. The right-click Mill toolpaths menu now starts the
  ported surface ops (Surface rough Pocket / Parallel, Surface finish Parallel, OptiRough, Area Roughing, Waterline, Raster).
- DONE 2026-10-25 Alt+1 etc.: the animation ran back and forth - each view input (main view + a second one) stepped the shared
  transition with its own clock (~210 ms apart); one process clock now (viewwidget.cpp). Test hooks: script step wait:MS, event F:mcfn:NAME.
- DONE 2026-10-25 Close (X): the prompt's close() ran inside closeEvent (Qt ignores the nested close); closeEvent accepts once quitting.
- DONE 2026-11-14 (user report "in mastercam when I hit escape does it unselect everything; in this it does not"; branch
  worktree-agent-aaf9f8986d8914440): Esc with no function running clears the general selection as Mastercam's idle pump does
  (main frame FUN_18026ad90 / COpMgrTree::PreTranslateMessage -> 0x802a -> CMastercamApp::Run -> mccore PumpPreSelection -> 0x8104 ->
  set_clr_sel_bits_all(0, 4, ..) + repaint; only with config 4001 Allow pre-selection on). formats/ui.md "Esc with no function running";
  core_ui sel_escape_idle, Qt MainWindow::escIdle; scenes misc esc_*. Left: the Selection Bar's Clear selection command 0xa012's
  handler, Esc in a floating pane (routing NOT DECODED). Regression (stopped at session end): misc 5/94 = master's 5 (rg_new17),
  the 4 new scenes pass; wf / chain / srf / solid / op / stockplanes / fileop NOT RUN yet on this branch - run them before merging.

### 3D toolpaths (hst3d_port_plan.md section 6)
- OptiRough:
  - DONE 2026-10-24: the ~8 mm outer band = the wall stock (record 0x13c0 / 0x13c8, hst3d_port_plan.md 6.6); tool containment;
  - DONE 2026-10-25: PosZLo / PosZHi (hst3d_port_plan.md 6.7); open: depth limits / stock-model range (record offsets NOT DECODED);
  - DONE 2026-10-25: Mastercam surfaces as drive geometry: NURBS + trimmed (hst3d_port_plan.md 6.8). DONE 2026-10-25 too: every surface
    kind (formats/surfaces.md), the surfaces' wireframe flowlines / outline, blanked wireframe hidden. Open: face picking on solids;
    the "Toolpath/surface selection" dialog; the gl_* shading settings' config keys; kind 4 / 7 untested (none visible in the corpus);
  - Core (machine from outside), Stepup, Zigzag, Skip pockets, cut-order optimisation, stock between levels;
  - save the op into .mcam.
- DONE 2026-10-25: classic Surface Rough Pocket (op 10) and Parallel (op 5) (formats/surf_rough.md 7 / 8). Open: mcmsurf's own High
  Speed pocket engine (the Mill_MM default op 10's method), run_mctutl_zcut, the parallel section / offset model, the gap motion types,
  the classic containment pick, thin wall / spring passes / facing, saving the ops; Radial / Project / Plunge / Restmill / Flowline /
  Contour; the classic Surface Finish ops.
- DONE 2026-10-25: HST Waterline (hst3d_port_plan.md 7). DONE 2026-11-08: contact angle filter FUN_18040b6f0 (hst3d_port_plan.md 7, inch ops 50..90 now generated; open: arcs at convex rims kept by Mastercam and dropped by us, FUN_1801344c0's tool layers), StepMachTolerance = 0.5 x 0x12a0 for every HST op. Open: ProfileSmoothing (Corner rounding), Add cuts, the linker. The extra low-level rings along shallow floors: rule NOT DECODED (plan 7 Investigation 2026-11-08).
- DONE 2026-10-25: HST Raster (hst3d_port_plan.md 8) and the classic Surface Finish Parallel op 11 (surf_rough.md 9). Use Z depths / Flute contact at the limits: DONE 2026-11-08 (plan 8). Open: the drop-cutter maths
  (CToolDropPos), Z-depth trimming, perpendicular fill, Down / Up Mill, the linkers.
- DONE 2026-10-25: HST Area Roughing (hst3d_port_plan.md 9): Area Clearance (the defaults' mode) decoded; Core = the same passes on a region
  open to the outside, kept inside the grown constraint boundary (measured). Open: Core's own pass region and FUN_1802ef450's trim without
  trim boundaries, Corner rounding (the loop smoothing FUN_180557f60), Add cuts, the inflexion levels (CZInflexions*), the Horizontal Area
  add-on (TP+0x475 = 2), the linker; the speed of area_mill_passes' 2e6-cell grid (about 15 s per level).
- DONE 2026-10-26: HST Spiral / Radial (hst3d_port_plan.md 10): line / spiral layout, parameters, pages 1338 / 1339. Open: no stored op to
  check against; the plan default far radius (Outer radius off), Spiral Zigzag, the Spiral / Radial linkers, the spiral pass joining FUN_18037dc90.
- 2026-10-26 "lets get all the surface icons working": the 12 greyed 3D gallery items.
  - DONE: Spiral, Radial (hst3d_port_plan.md 10), Scallop 2D plan (scallop.md 9; oracle p95 0.5 mm), classic Finish Contour op 15
    (surf_classic2.md 3; oracle: the 18 levels exact, p95 0.057 / 0.022 in), HST Project NCI / Curves (hst_project_hybrid.md 3), classic
    Rough Project op 7 NCI / Curves (surf_classic3.md 4).
  - DONE 2026-10-28 (branch toolpaths2): Pencil and Blend wired into the app (pencil.md 8, blend.md 6.1); Horizontal Area: the fibre
    toggle and the Eva+0x670 window decoded (horizontal_area.md 9.3 "Third pass"), the open-side trim still NOT DECODED (FUN_1803ffdb0 /
    FUN_1803fc9b0).
  - DONE 2026-10-28: Multisurface Pocket (T-1, surf_classic2.md 1.7) and Rough Plunge (T-2, unverified: no oracle; 2.9).
  - DONE 2026-10-28: Surface Finish Radial (op 12) and Finish Project (op 13) (surf_classic3.md 5, unverified). Left of the classic finish
    ops: Pencil 39 / Leftover 40 / Parallel Steep 41 / Shallow 42 / Constant Scallop 43 (the shared FUN_180133f10 engine, NOT DECODED) and Flowline 14.
  - DONE 2026-11-08: Horizontal Area generates (T-3 closed: the open side DECODED, horizontal_area.md 9.5; CAM_HORIZAREA, gallery item live, page 1327). Open: the seven small extra regions and level 0's inner passes on the oracle (RASTER op 2), the filter of region size, the links; flat tips / stock != 0 / containment unverified.
  - 2026-11-13 (answered: decode first): Scallop's default 3D plan (Shallow Area Detection) - plan decoded (scallop.md 10), the cell classification FUN_18026c540 not yet; refused.
  - 2026-11-08 scoping pass over Flowline 14 / 8, Hybrid, Leftover 40, Steep 41, Shallow 42, Constant Scallop 43 (surf_classic3.md 8): nothing ported, all stay greyed. Stored ops: one part each for 14, 43 and Hybrid, none for 8 / 40 / 41 / 42. Constant Scallop's default goes to FUN_180123090, not FUN_180133f10 (8.2). Closures 39k-132k decompiled lines (8.3). Smallest next piece (needs a decision, it would refuse the page's defaults and has no oracle): single-surface op 14 with Distance stepover and gouge check off (8.5).
  - 2026-11-13 classic finish engine (branch worktree-agent-a29137f82363a31e4, surf_classic3.md 9-11): FUN_180133f10 decoded (a frame / step / raster-count wrapper; mode -> FUN_1801cfb60 kinds read from the asm). Corrected 8.2: with the default config bnci_surface clears EMM for ball tools, so Constant Scallop 43 runs FUN_180133f10(6 + 5) -> FUN_1801cfb60, not FUN_180123090; classic Pencil 39 without check surfaces does not use the engine at all: McHmm's "Pencil Pass Macro" (plan 0x140f + Pencil Linker 0x1442). DONE: Surface Finish Pencil (op 39) generates on the HST Pencil's net (CAM_SFPENCIL, cam_cli sfpencil, Toolpaths manager Surface finish > Pencil kept GREYED by user decision N13-19 until the Pencil Linker 0x1442 is decoded - the same open item as HST Pencil's ours pass order / SAFE links; page 240); oracle PENCILSEAMS X9 op 1 with Parallel passes off: p95 0.0077 in, 100 % on Mastercam's centre passes; the stored op is refused (0x140d), the MM default too (bull tool, depth limits). Ours as HST Pencil's: pass order / SAFE links (linker 0x1442 NOT DECODED). Left: Leftover 40 / Steep 41 / Shallow 42 / Scallop 43 need FUN_1801cfb60 (48k lines, unread) - next: its kinds 6 / 7 against the CURVE 5 AXIS Scallop op; the Pencil Linker 0x1442 and the offsets plan 0x140d (both Pencils). Found: tools/gen_opdlg_fields.py no longer reproduces the committed viewer/core_ui/cui_opfields.inc (it drops the 2997 ramp / 2999 / 3146 rows), so page 240's rows were added by hand; the script needs those pages back before its next use.
  - 2026-11-13 classic finish engine, session 2 (surf_classic3.md 12): Constant Scallop 43 mapped end to end - bnci_surface -> machine
    model (FUN_18012a310) -> FUN_1801cfb60 kinds 7 / 6 (plain surfaces: the plane-section scanner FUN_1801ea7e0 of the inverse-offset
    tessellation, then the per-line stages FUN_180183310 / FUN_1801974e0 / FUN_180184cc0 / FUN_180182620 / FUN_180181a80 / FUN_18018fd80
    / FUN_1801a1a70 into the temp.rgn records; file formats in 12.2 / 12.3) -> FUN_1801d3ca0 (FUN_18018e630, the collapse / zone core
    FUN_1801b1ad0 54.9k, FUN_1801b0df0 order -> cut.tmp / msproj.tmp) -> FUN_180290fb0 links -> FUN_180136670 NCI (gr_lin_mill /
    gr_arc_mill). Port stages A-F with sizes in 12.5 (~100k decompiled lines in all); NOTHING PORTED, op 43 stays greyed. DLL oracle
    `peloader mcmsurf.dll --lmm` (tools/peloader/mcmsurf_lmm.c, case tools/lmm_case.py) maps mcmsurf + mccore and runs bnci_surface
    on the corpus op as far as the licence query FUN_180012d60 (SC-1). N13-22 (user: enter below bnci_surface, never touch licence
    code): lmm_setup repeats bnci_surface's setup step by step (surf_classic3.md 12.7, each step with its line) and calls FUN_1800135a0;
    its first step FUN_18029e890 -> mccore alloc_nc_all -> mccore 180020050 sizes the NC buffer from GetProdEnableBits() - licence code
    on every path of the engine (edge_setup / pl2MemBufSetup take that buffer), so the harness STOPS there as instructed; no NCI, no
    stage file. N13-23 (SC-2): no DLL stage oracle; port B-F from the code, internal-consistency checks per stage, end-to-end check
    against the stored NCI of CURVE 5 AXIS op 2. NEXT (not started): stage B, see surf_classic3.md 12.8.
  - 2026-11-13 classic finish engine, session 3 (surf_classic3.md 12.8 corrected, 12.9): the tessellation stand-in for stage A is withdrawn
    (N12-1's rule). Stage A decoded: mccore make_surf_model = the make_surf_polygons engine FUN_18057a980 with the offset arm (side test
    FUN_18058abd0, steps on the offset surface = FUN_180460cd0 in full, the two-side passes / merge, the grid outline loop FUN_18057dd40 /
    FUN_18058e180 / FUN_1805858b0, mode / box / counts); the model on disk is the grid (tmpmm1) + 0x78 records, triangles are rebuilt by
    fill_mach_model_buffer. PORTED: viewer/mcpartline_mesh.c plm_model (untrimmed kind 0, ball tools) and plm_ridge (check_for_ridge).
    DLL unit oracle `peloader mccore.dll --msm` (no licence code reached; arena = mccore's poly space, not alloc_nc_all's buffer):
    CURVE 5 AXIS op 2 6 / 6 surfaces bit-identical; corpus numbers in surf_classic3.md 12.10. LEFT for stage A: the tmpedg0 boundary
    edges (FUN_18059c040 / FUN_18059c520), tmpcrn0 / tmpmm2 / tmpmmx (FUN_180229ba0), trimmed surfaces in model mode, non-ball tools
    (FUN_1805851e0), the buffer readers (setup_ / fill_mach_model_buffer); then stage B. Op 43 stays greyed.
  - 2026-11-13 classic finish engine, session 4 (surf_classic3.md 12.11): DONE stage A pieces, each against the DLL: the tmpedg0 boundary
    edge records (FUN_18059c040 / FUN_18059c520, grid border cells and the outline loop's ears in FUN_180592030; viewer/mcpartline_mesh.c
    model_edges, mcpartline_trim3.inc): corpus 4283 surfaces / 635,537 records identical; FUN_180225000 / FUN_180224ec0 (tmpedg0 chained,
    tmpcrn0; viewer/cam3d_mmodel.c mm_corners) against mcmsurf's own function (`peloader mcmsurf.dll --lmm` CRN unit mode); tmpmm / tmpmm1
    (mm_record / mm_mm1_bytes) read back by mccore's own setup_ / fill_mach_model_buffer (`--msm` MMB lines) with the port's fill (mm_fill /
    plm_model_tris, with the outline loop: FUN_18057db70 / FUN_18057eeb0) identical. LEFT for stage A: tmpmmx / tmpmm2 (read, not ported), trimmed surfaces and non-ball tools in model mode. The small-arena layouts (border-only vertices,
    smaller fill blocks) depend on the alloc_nc_all buffer's licence-dependent size: the port assumes room (as plm_mesh); stage B must read
    whether the scan depends on the block size. Then stage B (FUN_1801e8360 phase A, FUN_1801ea7e0). Op 43 stays greyed.
  - 2026-11-14 classic finish engine, session 5 = stage B part 1 (surf_classic3.md 12.12, branch worktree-agent-a3930ec38ec4ff869):
    FINDING (FUN_1801e8360): the scan's triangles / pieces do not depend on the fill block split, but the ERRCHK groups and so the piece
    order handed to FUN_1801e7350's (unstable) qsort do; whether FUN_1801e5200 is order-sensitive on equal keys is open. N13-28: one
    block per surface assumed. DONE (viewer/cam3d_cscallop.c, driver viewer/cs_port): scanner setup, phase A FUN_1801e8360 (incl. the
    mode-1 passes and the counter quirk of 1801e9214), per-line triangle activation FUN_1801daa60 (kinds 10 / 11) + FUN_1801dba80 /
    FUN_1801dc360 / FUN_1801dbc50. DLL unit oracles (no licence code, no NC buffer): `--lmm` SCN (scn_cmp.py) and PHA (pha_cmp.py),
    corpus runner scn_oracle.sh; numbers in oracle_results.md. NEXT, in order: FUN_1801e7e20 + edge / corner groups (FUN_1801df420 /
    FUN_1801e0d10 / FUN_1801e1050 / FUN_1801e0ae0, FUN_1801ed160; FUN_1801df380, FUN_1801ec6b0 with the arc tables FUN_1801da800 /
    FUN_1801d9e20 / FUN_1801db440), then FUN_1801e7350 / FUN_1801e5200 (the estore: 0x68 nodes at DAT_1804ea4e0, head 5f0 / tail 5fc /
    free 600 / free count 604, capacity DAT_1804ea5e4 = min(0x7fff, pl2MemBufRemainFit) - overflow = error -7), FUN_1801e02b0,
    FUN_1801e0680, FUN_1801ed9b0; each unit-testable the same way (SCN mode can hook / call them). Op 43 stays greyed.
  - 2026-11-14 classic finish engine, session 6 = stage B part 2 (surf_classic3.md 12.13, branch worktree-agent-a0d962b5b292cdeee):
    DONE the edge and corner primitives, DLL-identical: FUN_1801e7e20 groups; activation FUN_1801df420 (FUN_1801e0d10 / FUN_1801e1050 with
    FUN_1801e74e0 / FUN_1801df770 / FUN_1801ea2e0 / FUN_1801e77c0) + edge quads FUN_1801e0ae0 / FUN_1801d9520, corners FUN_1801df380;
    FUN_1801db8c0's edge / corner sections FUN_1801e1ac0 (FUN_1801e0050 / FUN_1801ed4a0 / FUN_1801ecfb0 / FUN_1801db170, mccore
    UnitNormal / proj_pt_ln(_3d) / lp_ints), FUN_1801e2030, FUN_1801ec6b0 (FUN_1801dfd10, FUN_1801e4200) with the arc table
    (FUN_1801da800 / FUN_1801d9e20 / FUN_1801db440 / FUN_1801e9990 / FUN_1801e14c0); viewer/cam3d_cscallop_edge.c, cam3d_cscallop_sect.c.
    Oracle (SCN mode extended, scn_cmp.py / scn_oracle.sh): CURVE 5 AXIS model 11,727 lines, 14.76 M pieces + 1.8 M nodes identical;
    corpus sample 740 lines / 384,064 pieces identical. Kinds 4 / 5 (no normals) are not on the op 43 path (param_2 = 1, asm 1801d0255):
    NOT PORTED. FINDING (step 3): FUN_1801e5200 depends on the order of equal-lower-end pieces (the DLL's merge with batches reversed:
    248 / 740 corpus lines differ in the last bits) - covered by N13-28 (one fill block per surface = the DLL's order) + msvc_qsort.
    NEXT: port FUN_1801e7350 / FUN_1801e5200 (the harness runs the real merge: LMM_MERGE, "EST" / "WALL" per line), then
    FUN_1801e02b0, FUN_1801e0680, FUN_1801ed9b0, then the scanner's full per-line output (task step 4). Op 43 stays greyed.
  - 2026-11-14 classic finish engine, session 7 = stage B part 3 (surf_classic3.md 12.14, branch worktree-agent-a81a0a9e879d40f22):
    DONE, DLL-identical: the envelope merge FUN_1801e7350 / FUN_1801e5200 (from its disassembly), the estore / hint / batch helpers,
    the walls FUN_1801e01a0 / FUN_1801edb20, the cleanup FUN_1801e02b0 (FUN_1801e9ec0, FUN_1801e3190, FUN_1801e23f0 / FUN_1801e6a70 /
    FUN_1801e6fe0 / FUN_1801e2680), the line writer FUN_1801e0680 and the wall blocks FUN_1801ed9b0 (viewer/cam3d_cscallop_merge.c).
    Oracle (harness LMM_MERGE=1 / 4 runs the DLL's own functions; scn_cmp.py --merge / --full, scn_oracle.sh --full): CURVE 5 AXIS
    model 11,727 lines, 1.98 M list nodes and the 58 MB scan file identical; corpus sample 740 lines, lists, 2971 walls, scan and wall
    files identical. The scanner arguments and line positions are TEST values. LEFT in stage B: the scan file's 0x38 header
    (FUN_180229640) and the scanner's trailer (l.326713-326730), the in-line wall flush, containment. NEXT: stage C (12.2):
    FUN_1801cfb60's real scanner arguments and line placement, then its per-line stages. Op 43 stays greyed.
    Stage C started (12.15): the scanner's argument mapping read from the asm (stock / r / D = scanner p3 / p4 / p5, tol =
    msurf_chord_dev, lines / first / step = FUN_180133f10's uVar16 / dVar29 / dVar24); the unit oracle's tool values are equivalent to
    CURVE 5 AXIS op 2's real ball (same offsets), tol / corner deviation / frame / line positions are test values. NEXT: port
    FUN_180133f10's frame / step / lines with FUN_18012f9c0's extents, then the scan file header / trailer, then 12.2's per-line stages.
  - 2026-11-14 classic finish engine, session 8 = stage C parts 1-2 (surf_classic3.md 12.16 / 12.17; viewer/cam3d_cscallop_lines.c):
    DONE, DLL-identical: FUN_180133f10 before FUN_1801cfb60 (msurf_map's op 0x2b values, frame, step with FUN_180246570, the model turn
    FUN_18025dd50 / FUN_180261f40 / quarter turns FUN_1802619c0, extents FUN_18012f9c0, line count / first / step) - harness S1 mode runs
    the DLL's own FUN_180133f10(6 / 5) with FUN_1801cfb60 recorded (tools/tests/s1_cmp.py): CURVE 5 AXIS op 2 both modes, 120 lines,
    every argument and the model files identical. The scanner with the REAL arguments and lines (kind 7 and kind 6, with the kind 7
    scanner's in-place edge sort between them): 120 / 120 lines each, 113,956 / 114,004 pieces, the scan files identical (scn_cmp
    --full). The scan file's frame: header FUN_180229640, the scanner's trailer, FUN_18022c850's appendix + final header - the DLL's
    FUN_1801e7ad0 reads the port's trailer, the DLL's FUN_18022c850 on it gives the port's whole file byte for byte (apx_cmp.sh).
    NEXT: stage C item 3 = FUN_1801cfb60's per-line stages (12.2 steps 1-7) into temp.rgn. Checked (12.18, cfb_probe.sh): FUN_1801cfb60 cannot run
    whole in the harness - its node array comes from pl2MemBufRemainFit / pl2StackAlloc (the NC buffer, not emulated): build an SCN-like
    unit mode for the per-line functions (DAT_1804e9320 = 0x7fff per N13-28), then port FUN_180183310 onwards. Then stage D. Op 43 stays greyed.
  - 2026-11-14 classic finish engine, session 9 = stage C item 3, part (surf_classic3.md 12.19; viewer/cam3d_cscallop_cfb.c, viewer/cfb_port):
    under the orchestrator ruling (no pl2 answers, no NC buffer, a 0x7fff array only as a direct argument) the per-function unit modes
    CFBU / CFBA (tools/tests/cfbu_cmp.sh, cfbu_sample.sh). DONE, DLL-identical: the block walk FUN_180183310 (+ readers), FUN_180182070,
    FUN_180182620, FUN_180181a80, the code families - CURVE 5 AXIS op 2 both kinds (22,124 segments, 236 arrays / 22,340 nodes) and the
    corpus sample (730 lines, 81,925 segments, 595 arrays). DONE without a DLL oracle (they work on the global store DAT_1804e9328 from
    pl2StackAlloc): the line list FUN_180182e70 / FUN_180183080 / FUN_1801a2760, FUN_1801974e0 (asm twin of the scanner's verified
    cleanup, FUN_1801d7da0 from its asm), FUN_180184cc0 without walls, FUN_180181680. LEFT: FUN_18018fd80 (endpoint flags, ~1.5k lines;
    its 0x58 array callees FUN_1801cb640 / FUN_1801cdcf0 / FUN_1801cac00 are testable), FUN_180182de0 -> FUN_1801a1a70 (temp.rgn blocks),
    FUN_1801c9350 + the trailer, then the DLL's temp.rgn reader on the port's file; walls (FUN_1801b0c80 / the vcs branch) NOT PORTED;
    msurf_max_short_gap into cs_op43_params. Then stage D. Op 43 stays greyed. (SC-3 answered: N13-31.)
  - 2026-11-14 classic finish engine, session 10 = stage C item 3 continued (surf_classic3.md 12.20; viewer/cam3d_cscallop_fd80.c): under
    N13-31 (the harness points DAT_1804e9328 at its own 0x7fff-node array; still no pl2 answer / NC buffer / licence code) the CFBL mode.
    DONE, DLL-identical on op 2 both kinds, the 37-part corpus sample and synthetic files (tools/tests/cfb_synth.py): the list insert
    FUN_180182e70 / FUN_180183080 / FUN_1801a2760, FUN_1801974e0 (+ FUN_1801d7da0), FUN_180184cc0 (no walls), FUN_180181680; FUN_18018fd80
    ported (its pl2 arrays supplied by the harness, its callees FUN_180185e50 / FUN_1801c5620 / FUN_180182b00 / FUN_1801cad70 /
    FUN_1801cb510 / FUN_1801cdcf0 / FUN_1801cfa90 / FUN_1801c5a50 run by the DLL); the temp.rgn line block FUN_1801a1a70; NaN-faithful
    comparisons in the run straightening (also the scanner's); msurf_max_short_gap computed as msurf_map does (op 2 = the DLL's).
    LEFT: the temp.rgn frame (header FUN_18025d6d0, start offset / table, FUN_1801c9350, the trailer l.310888ff) and the DLL's reader on the
    port's whole temp.rgn; walls (FUN_1801b0c80 / FUN_180184cc0's vcs branch); stage D. Op 43 stays greyed.
  - 2026-11-14 classic finish engine, session 11 (surf_classic3.md 12.21-12.23; branch worktree-agent-a12a0228305dbdb27): DONE, DLL-identical:
    the temp.rgn frame (header, accumulated line position, FUN_1801c9350 wall copy, trailer, kind 7 header rewrite / kind 6 FUN_18022c850) -
    the port's whole temp.rgn of op 2 = FUN_18022c850's output and read back by the DLL (RGNR); lines with walls (FUN_1801b0c80, FUN_180184cc0's
    wall branch FUN_18015bca0 / FUN_1801c8080 from the asm) - synthetic 1200 cases and the corpus sample 37 / 37 (the 135 refused lines run).
    Containment FUN_180192af0: not on op 43's path (DAT_1804e8ffc = 0), not ported. Stage D mapped (12.23). LEFT: stage D port (12.23's
    stages), E, F. Op 43 stays greyed.
  - 2026-11-14 classic finish engine, session 12 (surf_classic3.md 12.25; branch worktree-agent-adde4357f5756b59b): DONE, DLL-identical: D1
    FUN_18018e630 (temp.rgn -> tmpemesh.tmp, both directions, the line-end / gap extension across the other direction's lines FUN_1801a9150
    and its callees, all from the asm); op 2 tmpemesh = temp.rgn (nothing extended), 40 synthetic files identical (peloader --lmm EMESH,
    tools/tests/emesh_cmp.sh). Resolved: tmpzone header +4 = the pass (FUN_1801ab410), tmpdzdat entries are addresses (asm). FUN_18018a640 (segment records, 12.27) = DLL on op 2 + 60 synthetic runs. LEFT (12.28):
    FUN_1801b88e0, FUN_18015d0f0, the zone pass FUN_180187850, FUN_1801b1ad0's frame, stage E, F. Op 43 stays greyed.
  - 2026-11-14 classic finish engine, session 13 (surf_classic3.md 12.29-12.31; branch worktree-agent-acc8de4ba17a25555): DONE, DLL-identical:
    D2 FUN_1801b4e80 sizes, FUN_1801a7fd0's in-memory arrays (N13-28), FUN_1801b88e0 (segment point / code / flag arrays), FUN_1801b84f0 (B
    flipped) - op 2 + 60 synthetic runs (pts_cmp.sh); FUN_18015d0f0's first part FUN_180150840 (records split by code class, FUN_180157620's
    jump tables read from the DLL) and FUN_18014d5c0 (pieces with a short gap merged) - op 2 + 84 mixed-code synthetic runs (em_synth.py mix,
    d15_cmp.sh). NOT PORTED: FUN_1801a7fd0's windowed arm (buffer-dependent), ctx +0x309 != 0 (ops 0x28..0x2a). LEFT (12.31): FUN_180149a20
    (closure 4678 lines; structure mapped), FUN_180146e60, FUN_18015c8f0, FUN_180186ff0, the zone pass (D5), FUN_1801b1ad0's frame (D3), E, F.
    Op 43 stays greyed.
  - 2026-11-14 classic finish engine, session 14 (surf_classic3.md 12.32-12.33): DONE, DLL-identical: FUN_180149a20 (the crossing pass of
    FUN_18015d0f0: gather FUN_18014aa50, FUN_18014a0a0 / FUN_180148d20 / FUN_180148f90 with FUN_180148960, FUN_180248a70, FUN_1802485d0 and
    the mccore segment helpers, merges FUN_180150ff0) - op 2 + 90 mixed synthetic runs (d15_cmp.sh D15_FULL) + 10,812 unit calls
    (D15_UNIT). Unexercised (ported from the asm): FUN_180150930's branch, results 6 / 7. LEFT (12.33): FUN_180146e60 (mapped), FUN_18015c8f0,
    FUN_180186ff0 / the zone pass (D5), FUN_1801b1ad0's frame (D3), E, F. Op 43 stays greyed.
  - 2026-11-14 classic finish engine, session 15 (surf_classic3.md 12.34-12.35): DONE, DLL-identical: FUN_180146e60 / FUN_1801470e0 (records
    cut where they cross the other direction's lines: FUN_180147980, FUN_1801aad80 / FUN_1801c9520 / FUN_1801c65e0, FUN_180149e90,
    FUN_1801498e0, FUN_18014a070, FUN_1801561e0 / FUN_1801aeac0, FUN_1801af5f0 / FUN_1801af0f0 / FUN_180166e50 / FUN_1801a9980 /
    FUN_180166480) and FUN_18015c8f0, so FUN_18015d0f0 is complete. Found: ctx +0x30a is 1 during FUN_18015d0f0 (asm 1801b2a9f; 12.33
    said 0); header n is read one past the array and is record 0's memory (pl2StackAlloc layout; the harness now lays headers and
    records out so); the DLL's LIFO free list is modelled (CsFree). Refused: a header-n chain through further freed memory (-101), the
    windowed arrays (N13-28), ctx +0x309 != 0. Unexercised: FUN_1801af0f0 cases 0 / 2 / 3 / 6, FUN_180149a20 results 6 / 7,
    FUN_180150930. LEFT (12.35): FUN_180186ff0 = the zone builder FUN_180187850 + FUN_180162dd0 (D5: closure 33,697 instructions, 322
    functions), FUN_1801c16d0 / FUN_1801597b0 / FUN_180192330, D3, E, F. Op 43 stays greyed.
  - 2026-11-14 classic finish engine, session 16 (surf_classic3.md 12.36-12.37): D5 started. DONE: a whole-function DLL oracle of the zone
    builder - harness D15_ZB runs mcmsurf's FUN_180187850 as FUN_180186ff0 calls it on the D15 records (FUN_1801b1ad0's FUN_18016a2a0 /
    FUN_1801588f0 setup, FUN_1801b3000, FUN_1800cde60 / FUN_1800ce4b0 repeated on harness memory per N13-31; every other pl2 / edge import
    trapped = refused); op 2: 1 zone, no trap; 75 / 75 synthetic runs complete; one-shot int3 call coverage (D15_ZBCOV): 104 functions run
    on op 2, 209 / 329 by any input. The D5 port plan Z0-Z9 (12.36). Z0 ported, DLL-identical (D15_ZU, d15_cmp.sh): FUN_1801bc5c0,
    FUN_1801c0320 (+ FUN_1801bec50 / FUN_1801c1c40 / FUN_1801c3490 / 3470 / 34b0), FUN_1801c32f0, FUN_1801beff0, FUN_1801be460 /
    FUN_1801bf0d0 / FUN_1801bd8a0 - op 2 + 135 synthetic runs. Found: tmpprojz.tmp / tmpdztmp.tmp hold heap addresses (compare by
    structure); FUN_180162dd0 refuses at FUN_1801469b0 (pl2, body to repeat). NOT DECODED: the zone files' record layout. LEFT (12.37): Z1-Z9
    (~33k instructions), FUN_180162dd0, D6, the unexercised-branch synthetic cases, D3, E, F. Op 43 stays greyed.
  - 2026-11-14 classic finish engine, session 17 (surf_classic3.md 12.38-12.40): D5 continued. DONE, DLL-identical: Z1 = the zone buffer
    and the zone files (viewer/cam3d_cscallop_zbuf.c: FUN_1801ab410 / FUN_1801aa460, FUN_1801a7d60, FUN_1801aa5e0 with FUN_1801c71b0 /
    FUN_1801c6ae0 / FUN_1801c7060 / FUN_1801c9be0, FUN_1801ad550 with FUN_180166550 / FUN_18015c3e0, FUN_1801aea00, FUN_1801a95e0,
    FUN_1801a9460, FUN_1801adec0) - harness D15_ZBLOG logs the DLL builder's calls and buffer, cfb_port D15_ZREP replays them,
    tools/tests/zbuf_cmp.sh: op 2 + 135 synthetic runs identical (380,226 calls, 1,783 zones, 76,650 file entries); Z2 part 1 =
    FUN_1801c24f0, FUN_1801716d0, FUN_18016e860, FUN_18016eb80 (+ FUN_180179f30 / FUN_180179e00 / FUN_18016ce00 / FUN_18017b790 /
    FUN_18017c490 / FUN_18016cdb0 / FUN_18016ca20), FUN_18015a830, FUN_1801aaea0's leaves FUN_1801b6000 / FUN_1801aed20 / FUN_1802537e0 (viewer/cam3d_cscallop_zcore.c) - harness D15_ZBUNIT (inputs / outputs
    per call), tools/tests/zcore_cmp.sh: op 2 + 135 runs, 18,792,119 calls identical except 1,078 FUN_1801aed20 calls on a record in no list (refused;
    likely header n, 12.34; the DLL returns 1). NOT DECODED: the zone header's +0x84 short (and
    byte +0xb) and the step's +4 / +0x2e bytes - Mastercam writes uninitialised stack there (the port writes 0; whether a reader uses
    them is to check with FUN_180162dd0). NOT PORTED (refused): the draw mode of FUN_1801aa5e0, FUN_18017c1c0 (never reached), the ctx
    +0x309 arm of FUN_18015a830. Not exercised: the buffer full (FUN_1801aea00). LEFT (12.40): FUN_1801aaea0's tree (rest of Z2), Z3-Z9,
    the Z1-list record helpers (FUN_1801be2f0, FUN_1801c3380, FUN_1801bfd90, FUN_18018b6e0, FUN_18022d010) with Z8, FUN_180162dd0, D6, D3,
    E, F. Op 43 stays greyed.
  - 2026-11-14 classic finish engine, session 18 (surf_classic3.md 12.41-12.43): DONE, DLL-identical: Z2 complete = FUN_1801aaea0 (its
    per-record FUN_1801aed20 / l_ints block is dead code), FUN_1801ab730, FUN_1801c2a10, FUN_1801bfbd0, FUN_1801aeb30, FUN_1801aba60
    (viewer/cam3d_cscallop_zcore.c); the 1,078 off-list FUN_1801aed20 records of session 17 settled = freed records reached through header n
    (harness names them by memory "? F dir slot" / "? P k"; the port tracks which record took a freed record's memory, CsFree / cs_zc_res) -
    zcore_cmp.sh op 2 + 135 synthetic runs 136 / 136 identical, 28,105,469 calls, none refused. Z3 started (viewer/cam3d_cscallop_zwalk.c):
    FUN_1801bed30, FUN_1801ab6d0, FUN_1801aef30, FUN_1801c5ef0, FUN_1801bf450 (the candidate gatherer, header n included), FUN_18016fb60 +
    FUN_18017a2c0 / FUN_180147900 (the candidates' grid cells), FUN_18017c2e0 - zcore_cmp.sh 136 / 136 identical, 37,760,572 calls;
    FUN_18015bf40 (the chooser) ported from the asm but reached by no input (unchecked); the end codes the builder
    writes during the walk are handed to the replay ("ZK" lines). NOT DECODED (inputs logged, needed by Z8): ctx +0x310 / +0x318 / +0x320
    (FUN_1801b1ad0's frame). Not exercised: FUN_1801bf450's full array (max 11 of 50 used). LEFT (12.43): the rest of Z3 (FUN_1801bca00
    own 762, FUN_18016a7b0, FUN_18015c2e0 chain, FUN_180180090, FUN_180179090, FUN_18017d8d0, the pool FUN_18017daa0 + FUN_18017d330
    tree), Z4-Z9, D6, D3, E, F. Op 43 stays greyed.
  - 2026-11-14 classic finish engine, session 19 (surf_classic3.md 12.44 / 12.45): DONE, DLL-identical (zcore_cmp.sh op 2 + 135 synthetic
    runs: 136 / 136, 39,499,422 calls, none refused): the rest of FUN_1801bca00's callees but the pool - FUN_18015c2e0 / FUN_1801bfdc0 / FUN_18015a1c0 / FUN_1801695c0 (the
    side test), FUN_18016a7b0 (the cell runs: FUN_18017c7c0, FUN_180175dc0, FUN_18016b610 / FUN_18016b4b0, FUN_18016c130 / FUN_18016d5c0 /
    FUN_18016e620 with the .data tables DAT_1804d4610 / 4670 / 46d0, FUN_18016c2c0), FUN_180179090 (FUN_18016e170, FUN_18016ab30,
    FUN_18016e5c0, FUN_18016c1a0), FUN_180180090 (the candidate score; FUN_1801be340, FUN_1801c2780, FUN_1801bd790, FUN_18017b8e0). Harness
    fix: ctx +0x2f0 = msurf_colaps_dist (FUN_1801b1ad0 asm 1801b1e07) was left 0.0. FUN_18015bf40: unreachable on op 43 (behind ctx +0x309
    != 0, asm 1801bd471), stays unchecked. LEFT (12.45): FUN_18017d8d0, the pool FUN_18017daa0 (+ FUN_18017d330 tree, ~4,800 instructions),
    FUN_1801bca00 whole; FUN_1801bf450's replace-the-farthest branch (needs > 50 ends in the box: no synthetic case yet); Z4-Z9, D6. Op 43
    stays greyed.
  - 2026-11-14 classic finish engine, session 20 (surf_classic3.md 12.46-): DONE, DLL-identical (zcore_cmp.sh op 2 + 135 synthetic runs
    136 / 136, 39,562,628 calls): the zonbuf pool FUN_18017d8d0 / FUN_18017d8a0 and the cell triangulation FUN_18017daa0 with its tree
    (FUN_180175ef0, FUN_18017c8a0 / FUN_1801863e0, FUN_180175f40 / FUN_180154fb0 / FUN_180154ea0, FUN_18017cb60, FUN_18017cc10 /
    FUN_18017eb50, FUN_18017d330 with FUN_18017d4c0 / d090 / d610 / ce10 / FUN_1801727c0 / 27f0 / FUN_18017eac0 / ea30, FUN_18017e750 /
    FUN_18017d950 / FUN_18017e5a0 / e9f0 / e0e0 / e370 / e180 / d8f0 / da50 / e210 / e2d0 / e0a0 / e070, FUN_18017ebb0). Found: the
    triangulation compares record addresses (FUN_18017e180); Mastercam's A records, B records and FUN_1801b86c0 pool come from one upward
    bump allocator (mccore edge_alloc 1805797c0 = pl2StackAlloc's), the port ranks records so.
    Also DONE: FUN_1801bca00 checked whole (cs_zw_walk, 12.47; zcore_cmp.sh 120 / 120 runs identical before the session ended, the last
    16 sweep runs (mixrun 110 v3 .. 115) still to rerun). Z3 complete. NOT committed: the Z4 FUN_180195fc0 port (draft from the asm, never
    run; 12.48). LEFT (12.48): check Z4, FUN_1801a7d10, Z5-Z9, D6; FUN_1801bf450's replace-the-farthest case. Op 43 stays greyed.
  - 2026-11-08 Stock Model op 139 / Restmill 107 scoped (formats/stock_model.md): DONE reading stored Stock Model ops (viewer/stockmodel.c, cam_cli PART stockmodel: all stored fields, source ops; the 1 stored op in the corpus, Benchmark 3_0 op 5). Regeneration of 139 / 107 refused with a message (cam_fileop.c), items stay greyed. Left: the generation = Mastercam's third-party Interfacial sweep kernel (tpiftools ComputeMCSweep -> ifsweep.dll + interfacial*.dll, 16.8 MB, not decompiled), so all rest-material consumers (Restmill, HST rest roughing, OptiRough rest, stock-model heights) are blocked on it; the HMM path (registry "Use HMM 3D Stock Model", off by default) and the Restmill Z-cut family FUN_1802a14e0 / FUN_1802f9e00 are not decoded; no stored op 107 and no stored model mesh (kind 0x2c) anywhere in 1477 files, so no oracle. Needs a decision before any port (peloader oracle of ifsweep.dll, or our own engine = deviation).
  - Not yet (scoped 2026-11-08; surf_classic3.md 6): Flowline 14 / 8 (the only stored op, PROJECTBLEND op 12, has 6 drive surfaces -> msurf_flow, not the single-surface path; no op 8 anywhere), Restmill 107 (needs a rest-material source: an earlier rough op's stored data, a roughing tool's model or an STL; no stored op; surf_classic3.md 7), Hybrid (separate multi-session project, hst_project_hybrid.md 2.4).
- XT -> OCCT translator (the 3D ops' drive solids; formats/xt_occt_fixes.md 8, branch xtfix2 2026-10-27: corpus 343 -> 449 / 821 meshed).
  Open (8.12): build the measured B-surface extension (offset of the tangent-extended basis, 8.8; 13 solids); the blend / intersection
  fits that stay above 10 um near spine ends past 'L' limits (8 solids); RIB CUT / HS AREA blend-face edges; PORT 5 AXIS plane face
  (BRepMesh); sch_28101.s_t for the Mastercam 2018 parts (172 solids) - SKIPPED by the user (2026-10-27: "skip the schema if not really needed"): not in the 2018 install, the 2021 package in Downloads is an InstallShield download stub without program files; revisit only if 2018 solids matter.
- The general linker (replaces the SAFE link rule, also helps 2D Area Mill).
- The user is asked for Mastercam test parts: OptiRough on a solid drive with flat, bull and ball tools.

### 2D engines
- Pocket burial: DONE (medax batch, opdlg_port_audit.md 21 "Medial axis"): POCKMDL medial axis + link walk, zigzag burial. Left:
  FUN_1801e0f80's path around islands; methods 1 (pbmap) / 4 (Morph).
- Dynamic: CalcMedAx ported (mcce_medax.c); entry methods 1 / 2 ported, 4 refused unless FUN_18015b2e0's candidate is provably
  empty (decode FUN_18015b2e0 / FUN_18015c370), 5 refused (FUN_180132200). Helix centre MCCE_MEDAX: ANSWERED N13-25 (on by default,
  opdlg_port_audit.md 21 M.6). First-record plunge: DONE (dec2 Q1).
- Peel: "nothing to cut" with both extensions; the 0.17 mm wall stock case; closed chains; the "Peel" cut style (bnci_peel).
- Dynamic Contour: open chains, finish passes, tapered walls.
- Dynamic MCCE_EXACT (mcce_port_plan.md 9, status 2026-11-04): op 22 fixed by exact material geometry. Left before the switch:
  the tool-centre region built exactly. Findings 2026-11-04: exact tcr alone stalls buckle-v6 op 36 (2523 iterations) because
  region_sets' S2 = cur - offset(mat, R - r) through Clipper leaves chord-wide slivers along the walls that become targets; with
  region_sets / local_link exact too (930 exact calls, 17 s) the stall moves: the tool now sits exactly on the tcr boundary,
  an approach target (state 1, not blacklisted - FUN_180053cd0 only filters boundary targets) aims toward_material outward,
  the step is cut to length 0 by attach_direct, state 2 marches 3 air steps, the link picks the same target (missed 76 %).
  So an exact tcr exercises engine branches Clipper never reached; check state 1 / toward_material / the target builders
  against the decode before retrying. v6/v7/v8 op 32 and buckle-v11 op 1 slightly worse than off; the switch itself
  (user decision, goldens change).
  2026-11-07 (mcce_port_plan.md 9 "Status 2026-11-07"): exact model made the default on the kernel branch, the default since the user's decision D-2 (user_decisions.md 2026-11-07) (MCCE_MEASURE=clipper / MCCE_EXACT=0 =
  the old bridges); local windows FUN_18002a140 / FUN_180029c30 / FUN_1800331a0 and the marker order of FUN_18002fa30
  ported; 41 ops 125 s -> ~20 s CPU. Left: the Dynamic op goldens are NOT blessed (list in the plan); v6/v7/v8 op 32 and
  buckle-v11 op 1 still slightly worse than Clipper; the pass bytes of §6.1.7 (first-pass feed, FUN_180033cf0 /
  FUN_180035ff0 / FUN_180033b60, col_merge's pass test) not ported (no stored op uses the first-pass feed); the exact tcr.
  2026-11-07 accuracy pass (mcce_port_plan.md 9 "Status 2026-11-07 (accuracy ...)"): MCCECalcOffset output orientation decoded
  and applied (S1 tangents were reversed: entries ~1 mm off on 30 ops); exact model oracle 0.385 / 1.425 -> 0.321 / 1.473; the
  4 ops worse than Clipper are now better than it. Left, ranked: buckle op 31 side pockets (region order: the offset-core order
  feeding DetNestLvlShAr is NOT DECODED); Profile entry Chaikin pipeline (FUN_18014c250 / d5c0 / c820 / f740) + loop start by
  source index (needs source tracking in our offsets); exact link / target sets (MCCE_EXSETS=1: 11 better, 3 worse) need an
  exact tool-centre region, which still ends buckle-v6/v7 op 36 early (88.7 % missed) - find that stall; op 3 depth change.
  2026-11-07 second pass (mcce_port_plan.md 9 "second accuracy pass"): op 31 is a material divergence, not region order (needs
  a per-step trace of the original engine); ops 3 / 10 / 32 / 36 / 42 deviate mainly by Mastercam's link smoothing
  FUN_18004a070 (decoded: opening + closing with MCCECalcContourOffset at d = 10m … 0.1m) - next: decode MCCECalcContourOffset's
  core + FUN_180049150 / FUN_180049950 / FUN_180045c40 and port the smoothing; exact tool-centre region (MCCE_EXTCR=1) completes
  op 36 now but is worse overall; with MCCE_EXSETS=1 the state-2-away-from-material loop remains.
  2026-11-07 third pass: correction - the routed links of ops 3 / 10 / 32 / 36 / 42 / v12 22 come from the guided planner
  FUN_1800f1080 (413 functions, ~33k lines; top level decoded), not from FUN_18004a070 (connector 1 only: 1 link in the corpus).
  Next: decode the guided planner; then the MCCECalcOffset core (175 functions, ~14k lines) for loop order / start entity.
  2026-11-07 entry loop smoothing (mcce_port_plan.md 9 "Status 2026-11-07 (entry loop smoothing ...)"): ChaikinSmoothSh +
  FUN_18004ae80 ported (exact model); 0.321 -> 0.222 median (BRACKET4 14 / 16 -> 0.08, NAKTO 2 0.643 -> 0.372). Left there:
  the loop start by source index (FUN_1800da6a0; needs source tracking in our offsets) - BRACKET4's first ~5 mm of ramp;
  NAKTO op 2's second region: this+0x340's wavy (material) side 0.04 mm off -> largest clearance 3.084 vs Mastercam's ~3.124.
  2026-11-07 mcce.dll oracle (mcce_port_plan.md 9 "mcce.dll oracle"; tools/peloader --dyn, tools/tests/mcce_dll_oracle.sh):
  Mastercam's own engine runs on our engine inputs, 37/37 ops. **op 31 is NOT a material divergence**: the DLL on our input
  matches the port commit for commit and gives the same 1.473 vs the NCI - the cause is the engine input (mcmill boundary
  building, plan §5 item 25), next: decode CHST2DProcess / PackageMachiningRegion's S1 / S2 / S3 for from-outside ops and test
  candidates on the DLL. Port-side leads found by the oracle: Profile entry (BRACKET4 14/16: DLL p95 0.038/0.046 vs NCI, port
  0.33), arc steps on tool-centre-region arcs (ops 3, 10, 22, 36, 42, 51), the first-record entry offset (0.03-0.18 mm on most
  buckle ops), a state-2 step ending at half the DLL's length (ops 1, 17, 23, 32, 46), measure flag 4 near its tolerance.
- Thread Mill: DONE (threadmill 2026-10-26, mcmill_2d_engines.md 4): B8-6 was the oracle's sampling (lengths 22.442 vs 22.449 in), every
  stored op's NCI motion records equal Mastercam's. Left: the finish spindle override 0x12c5 (refused - needs a spindle change inside
  an op), the per-point base Z with 0x1250 = 1 (Hole has no Z), sld_get_toolp_point_data for solid points; no oracle for taper / OD /
  teeth / control compensation (ask the user for Mastercam parts with them).
- Feed / speed overrides: one opt-in design for all of them (B9-4).
- Arc filter: total-tolerance bookkeeping (KeepTolerancesConsistent), smoothing group.

### Post
- NCI writer, drill: canned cycles for ops made here DONE 2026-10-26 (nci_writer.md 10); per-point initial heights / canned text /
  manual entries and bolt circle / grid points ported 2026-11-13 (10.1, no Mastercam reference yet: windows_checklist.md 6). Left,
  refused (0 of the 50 corpus drill ops use them, the app cannot set them): rotary axis, subprograms (the LinuxCNC side needs the user's choice; N9-7 chose M98 / M99 for Contour),
  points from another op, the approach / retract reference points (FUN_180096820), solid points / arcs (tp_is_solid,
  get_solid_arc_point), the 5-axis drill 0x1c (20 corpus ops with NCI, but no engine generates it here).
- Each part's own embedded post text (B8-5).
- Misc integers from the post on new ops.
- Home / Ref. Points and canned-text editing (only if the user uses them).

### Op dialogs (leftovers)
- Holder / Rotary Axis / Axis Combination / Multihead pages.
- Toolpath Editor; Toolpath Type page.
- Planes origins / Select Point.
- Preview toolpath.
- Feed speed calculator.
- RCTF.
- Tool page tool fields.
- DONE 2026-11-14 (branch worktree-agent-a20b1a335566b4da1): Tool page list 25463 sorts on a header click (LVN_COLUMNCLICK ->
  OnListClickColumn 0x180889340 -> SortList), its own column set read / written under GUI\Mill Tool Display\Tool List Settings
  (formats/mcamui_dialogs.md "Tool page"). Left: menu 1247's View 32977-32979 / Arrange tools 32980 / 32981 (SortList 2 / 4) and
  the header's column chooser / width drags on this list (OnHeadingRightClick / HDN_TRACK, the same CMillToolListCtrl handlers as
  the Tool Manager's) are not wired on the Tool page.
- DONE 2026-11-14 (N13-32, branch worktree-agent-a87ce106b99a873fe): every tool list sorts on a header click, all decoded (no
  approved deviation needed): Tool List 6410's list 1762 (CToolList map 0x180df6000 -> 0x18086bdb0 -> SortList; settings under
  GUI\Mill Tool Library Selection, ours toollist.conf) and Edit Common Parameters' tool list 395 (mastercam.dll CEditCommonDlg
  0x1805cfa10 -> 0x180143b10 -> SortList; not kept), besides the Tool Manager and Tool page lists (formats/toolmanager_ui.md
  "Every CMillToolListCtrl list"). Left on those two: their right-click / view menus (Tool List menus 0x4ca / 0x4cb, commands
  0x80ba-0x80cb; ECP 0x180143388), ECP's LVSCW_AUTOSIZE_USEHEADER widths on columns 1..4, column chooser / width drags, the
  Tool List's CToolFilt filter.

### Other
- Windows build of the Qt app.
- Small leftovers:
  - stock curve-chord defaults;
  - Alt+F12 hover marker;
  - chaining auto modes (Chain Feature, Drill Point Selection);
  - panel keys 4-entry history.
- Lathe / wire posts: skipped for now (P4).

## Done 2026-10-26 (session master-cam-reverse-04, merged 8e61465)
- N3 drill canned-cycle NCI (nci_writer.md 10; drill ops made here post through a .pst); D2 BnciUpdate feed / speed patch
  (bnci_update.md; a refused patch marks the op dirty). Left: rotary / subprogram / per-point drill cases; 2D HST / tool-number patches.
- Drafting (formats/drafting/README.md): display of dimensions / notes / leaders from parts (stroke fonts from the install, %v values,
  config 15007 view rule); linear dimension creation core (create_port.md, oracle 533 / 706). Left: the Drafting tab commands (UI,
  needs create.md's flow), save into .mcam, other dimension types, TrueType text, the view table.
- Drafting tab, first commands (2026-10-26, branch drafting; create.md 7): Horizontal / Vertical / Parallel live in the app
  (cui_draft.c), saved as D_ID records into .mcam / kept in the project; tools/tests/draft_ui_test.sh. Left (each needs decoding
  first): Smart Dimension (its state machine branches), Note (FUN_18003d410: dialog 452, DfCreateLabel / CalcLabel + the memory
  table writer), the panel keys / Font / Edit text / Height / Options / Update / witness Both-Right-Left enum, the '~' formatting
  pick, AutoCursor kinds other than P / 1 / 2 as associative aPoints (t of arcs / midpoints / centres), the corner tie rule
  (display-list order), body+0x766 = 0x21 writer, selecting / deleting / drag-editing a dimension made here.
- Drafting > Ordinate (2026-10-27, branch draftord; create.md 8 / 9): Horizontal / Vertical / Parallel / Add To Existing / Align live
  (cui_draft_ord.c, draft_ord.c, the drafting pick draft_pick.c / cui_draft_pick.c). Left: Window (dialog 2533 controls / defaults,
  get_window, FUN_18003bdd0 / FUN_18003aaa0 / FUN_18003ae40, align_ordinate_quiet); the Mode event 0x10000000's sender; the '~' key
  state; the oc in a plain drag (panel id 17); the Align oracle's 40 / 60 differing corpus entries (create.md 9.3); TrueType / arc-arrow /
  styles 6, 7, 9 in the pick; DraftIsDirty's regeneration of a base; InitDIM's second aPoint / anchor bytes; leader style 1 / signed
  values / arrowhd 0 / parallel checked against Mastercam (no corpus samples).
- Drafting tab, Dimension group passes 2-3 (2026-10-27; create.md 10-13): Angular, Baseline, Chained, Tangent, Perpendicular, Point live
  (Baseline without increments' drag-edit included). Left: Smart Dimension (create.md 9 row); join db_pick_dim (the Baseline / Chained base
  pick) with the Ordinate branch's viewer/draft_pick.c dp_pick; EvalToDouble for typed values; the greyed panel ids (digits FUN_18012bca0,
  angular flip 0x10, tangent side id 18, maintain 26, the tangent angle id 8 decoded not wired); DfTurnOffApAssoc (tangent with DfAssoc off);
  arcs of other views; the perpendicular 180-degree offsets question (needs a corpus record); the drag-edit of picked dimensions.
- Drafting tab, Dimension group (2026-10-27, branch draftdim; create.md 8-9): Circular live (cui_draft_dim.c, draft_create_circ.c;
  oracle 132 / 164, click-path 18 / 22). Angular: calc ported + oracle 85 / 144 (draft_create_ang.c), button flow decoded, NOT wired:
  wire it (DcAngInput needs config 11000 / 11007 / 11016 / 11019 / 11020 / 14011 in the loader), decode FUN_180242fa0 (message 0x6987)
  and `get_angle` (line + typed angle). Left greyed, each to decode first: Tangent (cad state 6, calc_tangent_dimension / calc_tangent_eps,
  10 corpus records), Perpendicular (state 5, calc_perp_dimension), Point (calc_point_dimension + its value string FUN_180310700 for the
  display), Baseline / Chained (FUN_18003c3d0 + BaselineChainProc: needs the fresh-entity drag-edit FUN_180038c50 -> FUN_1800490a0 state
  0x62, message 0x69c0, DAT_1812d97fa), Smart (its branches + the drag-edit of a picked dimension). Circular leftovers: the arc phantom,
  arcs of other views (map_arc_to_parallel_view / constr_view_3d), the circular drag-edit (FUN_18004c290 / FUN_18004d840), FUN_180306330
  (taken as a 0..360 normalisation in the oracle), the n/255 text-width family (font version).
- Alignment with Mastercam (align_status.md): op Delete / Undelete / Cut / Copy / Paste, Delete-key / Ctrl+Z focus, ops off Undo,
  geometry delete dialog 6561, Move keeps ids, N1 NC Save As. Left: insert arrow for Paste, a stored op's copy (list_op_copy),
  mark_op_dependents, regenerating a chain with deleted geometry, X9 post settings offset.
- Engrave: estamp7.exe engine ported (engrave_engine.md, viewer/engrave.c; coaster oracle p95 0.0001 in) and wired into the app
  (CAM_ENGRAVE, pages 1802 / 1803, cam2d_engrave.c); spline chains 2026-10-26 (spline_chain.md: E_SPLINE entities, chain_to_ctour_array's
  breaking). Left: the engine port's dependence on vertex density (coaster op 2 loses a short rough pass with the decoded, coarser
  contours - spline_chain.md 6), the other ops' spline handling, the push-button sub-dialogs
  1804 / 1805 / 1806 / 1807 / 10502, dialog 1808 (depth warning), XY stock to leave, the KorrekturVerf value for "on top" with a
  flat tool, remachining / wrap / other rough styles, 2018 records in the dialog (S at +0x12f8).
- gen_opdlg_fields.py drift: rerunning it flips the four Canned Text lists (c6548 3162-3165) from status 1 to 0; the committed
  cui_opfields.inc keeps 1 (patched back by hand on 2026-10-26) - find which is right.
- Audits of deviations (formats/audit_2026-10-26_*.md): open items to decide with the user (Dynamic SAFE rapids, contour types
  refused, 3D finishing "unverified" note, inch thresholds, Engrave / stored-op questions, Qt look items).
- In progress (worktree ~/mastercam-re/work/wt_ncidrill): Blend (cam3d_blend.c), Pencil (cam3d_pencil.c), Horizontal Area
  (cam3d_horiz.c), Transform toolpath (cam_xformop.c), then Engrave / these wired into the app.
- Small leftovers: cam2d.c cam_tool_tip_length uses pi / 180 where mccore's constant is 0.01745329251994329 (one ulp less; the NCI
  writer uses Mastercam's); cui_ops.c post_ops_mp's comment still says drill canned cycles are NOT DECODED.

## Done 2026-10-24 (later)
- Double-click in the graphics window opens the properties dialog (points / lines / arcs / part solids); Analyze's solid dialog
  6494 with description / level / colour saved (formats/analyze.md 9). Left: the idle consumer of Mastercam's dblclk event (decode),
  the solid's display colour from its record, undo for the solid dialog.
- View cursor black on XWayland (cfbc5b8).

## Waiting on the user
- ANSWERED 2026-11-18 (user_decisions SOL-5a..d): SOL-5 (1) keep OCCT's faces, (2) keep the midpoint joints - both rules stay on the decode list; (3) fuzzy value 1e-5 m (implemented behind MCAM_OCS_FUZZY=1, not default - see SOL-6); (4) colour / level at creation (done).
- SOL-6 (2026-11-18, user_decisions SOL-5c) The OCCT fuzzy value 1e-5 m breaks Through all cuts (the tool ends exactly 1e-5 m past the
  target and OCCT merges them; replay 277 -> 226 / 295 names, 83 -> 66 / 108 volumes; Mastercam's test-cube cut shows Parasolid did not
  merge). Options: (a) exact booleans as before (current default); (b) fuzzy 1e-5 m anyway (MCAM_OCS_FUZZY=1); (c) a smaller fuzzy value
  (ours, no source). Which?
- N13-35a (2026-11-14, formats/hst_linker.md 32, hst3d_port_plan.md 7 "W8") N13-35 switches MCAM_HST_MESH on once bull-tool Waterline follows the
  DLL. Done: SWARF op 4 with the switch on = the DLL's passes plan vertex for vertex (65 / 65 passes, 2705 / 2705 end points), cut p95 ref->ours /
  ours->ref 0.011 / 0.169 -> 0.005 / 0.002 in; SWARF_METRIC op 3 as stored (Corner rounding, refused before) now generates, 27 of 28 passes = DLL,
  on: 0.461 / 0.051 mm (off 0.446 / 0.529). With the switch on vs off (this branch) every Waterline op is better on ours->ref; slightly worse:
  SWARF_METRIC 3's cut ref->ours p95 0.446 -> 0.461 mm, Raster PARALLELSTEEP 5 seg p95 0.0006 / 0.0003 -> 0.0007 / 0.0004 in, RESTPASSES 18
  0.0009 / 0.0004 -> 0.0011 / 0.0007 in, OptiRough TUTORIAL 7 op 5 p95 1.020 / 1.032 -> 1.034 / 1.028 mm (N13-34, not touched here). Options:
  (a) switch MCAM_HST_MESH on now (Mastercam's own mesh; the small Raster / OptiRough moves are the mesh, as measured since N13-33);
  (b) keep it off until the Raster / OptiRough differences are explained; (c) on for Waterline only (a mixed rule: a deviation from both).
  Default unchanged (off) until answered.
- N13-35b (2026-11-14, formats/hst_linker.md 32.2) The BRACKET4 scene's default Waterline (gen_waterline, bull tool) is now REFUSED: on one pocket
  (Z -0.975) mchmm's Voronoi construction fails (the DLL too, on the same polygons), and FUN_180407c10's fallback (hmm_zprofile.c l.253-586:
  FUN_18011ce60 on the filter writer, FUN_18014c0a0, a second construction) is NOT DECODED. Before, the op ran our Clipper offset. Options:
  (a) keep the refusal and decode the fallback next (default); (b) let the old Clipper offset run for that case only (a stand-in: a deviation).
- ANSWERED 2026-11-14 (N13-35: off until bull-tool Waterline follows the DLL; resume the Voronoi line) N13-34a (2026-11-14, formats/hst_linker.md 33.6 / 31.7) N13-34 switches MCAM_HST_MESH on once Waterline's contact-angle filter uses Mastercam's weave
  contact. Done for the weave's own loops (ball tools: MOLD BOTTLE op 4 = the DLL vertex by vertex, 98 / 98 passes, cut ours->ref p95 0.002 on / 0.006 off).
  Not enough for bull tools: SWARF op 4's passes are the Clipper offset of the weave region by the flat radius, filtered at the polygon's vertices with
  the mesh normal (ours); with Mastercam's mesh its straight wall runs keep only their ends, so its cut ours->ref p95 is 0.171 on vs 0.004 off (the
  same on master; the links are not the cause). McHmm carries the keep flags through its Voronoi offset (W3 stage 3, not ported). Options: (a) keep
  the switch off until W3 stages 1-3 are ported (SWARF op 4 then follows the DLL); (b) switch it on now, accepting SWARF op 4's regression (every
  other measured surface-drive op is equal or better: SWARF_METRIC 3 ours->ref 0.53 -> 0.05 mm, MOLD BOTTLE 0.006 -> 0.002, Raster within 0.0003,
  OptiRough TUTORIAL 7 op 5 p95 1.020 / 1.032 -> 1.034 / 1.028 mm); (c) switch it on for ball-tool Waterline and the other styles, keep the old mesh for flat-radius Waterline (a mixed rule:
  a deviation from both). Default: (a).
- ANSWERED 2026-11-14 (N13-34: off until the weave-contact fix) N13-33a (2026-11-14, formats/hst_linker.md 27.5 / 27.6) N13-33 keeps MCAM_HST_MESH off "until the remaining surface kinds are ported".
  Now every surface drive of the corpus's HST ops is ported and DLL-matched (kinds 0, 1, 8, 10 over them); kinds 2 / 4 / 6 / 7, the trim
  repair and polygon meshes are decoded as far as 21.5 says but NOT PORTED (no corpus HST drive uses them; an op that has one would be
  refused with the switch on, as solid drives (Parasolid) and a non-Top Cplane are). Options: (a) keep it off until those are ported too
  (strict N13-33); (b) switch it on now, with those refusals (no stand-in: such an op fails with its NOT PORTED / NOT DECODED message
  instead of using the OCCT / mcs_mesh mesh). Measured (27.6): with the switch on the DLL harness reproduces the stored Waterline
  levels and paths (p50 0.0002-0.0003 in), OptiRough TUTORIAL 7 op 5 generates (p95 0.12 mm) where master's mesh fails "nothing to cut"
  (pre-existing), the port's own Waterline SWARF 4 gets worse (ours->ref p95 0.002 -> 0.172 in: its contact-angle normal from the mesh),
  Raster is unchanged within 0.0003 in. Default: (a).
- ANSWERED 2026-11-14 (N13-33: off until the remaining kinds are ported) N13-30a Make MCAM_HST_MESH the default?
- ANSWERED 2026-11-13 (N13-23: no DLL stage oracle; no licence answer, stub or buffer pre-allocation, the harness stays stopped;
  port stages B-F from the code and check end to end against the stored NCI) SC-2 (2026-11-13, formats/surf_classic3.md 12.7) Under N13-22 the mcmsurf oracle stops in mccore, not in mcmsurf: FUN_1800135a0's
  first step FUN_18029e890 (decomp/mcmsurf_all.c l.436954) calls mccore alloc_nc_all, which allocates mccore's global NC work buffer
  once (mccore 180020050, asm): size = 0x80000 bytes if GetProdEnableBits() & 6, else 0x117fba0 bytes. It is a memory size chosen from the
  product bits, not a refusal, but it is a licence query on the path, so per N13-22 the run stops (exit 5, "licence query reached from
  mccore 0x180020069"). Every stage needs that buffer (edge_setup, pl2MemBufSetup in the same function; the scanner uses
  pl2MemBufRemainFit / edge_mark). Nothing was worked around. Options: (a) stop here: no DLL stage oracle; the Constant Scallop port
  is checked only against the stored NCI (25451 moves) once all of stages A-F are ported; (b) another way you approve. Default: (a).
- ANSWERED 2026-11-13 (N13-22: option (b); never answer, patch, stub or emulate the licence check, stop at any licence code on the path;
  done: tools/peloader/mcmsurf_lmm.c lmm_setup - it stopped again, in mccore alloc_nc_all, see surf_classic3.md 12.7) SC-1 (2026-11-13, formats/surf_classic3.md 12.7) The mcmsurf.dll oracle of the classic finish ops (`peloader mcmsurf.dll --lmm`, Mastercam's
  own bnci_surface on a corpus op, so every stage of the Constant Scallop port can be checked against the DLL's own temp files and NCI) now
  runs into bnci_surface's product check FUN_180012d60 (decomp/mcmsurf_all.c l.12786ff): it asks Mastercam.dll GetProdEnableBits /
  product_level_ok_no_warn (the licence level: Mill 3 needed, else post_warning 0xed / 0xfb and no toolpath). Mastercam.dll is not loaded,
  so the harness must answer these imports. The tpif oracle's rule is "nothing in the licence code is patched, bypassed or emulated", so
  nothing was done. Options: (a) answer GetProdEnableBits / product_level_ok_no_warn as your installed licence does (Mill level 3),
  i.e. emulate Mastercam.dll's answer to the licence query inside the test harness only (the app itself never calls them); (b) do not
  touch the licence query: enter below bnci_surface (FUN_1800135a0 / the stage functions, as every other unit oracle calls engine
  internals), with the harness repeating bnci_surface's setup (read_appmch_cfg, the EMM decision, surf_sel_bits, msurf_map, ...) -
  more glue, so more risk that the harness differs from Mastercam; (c) neither: the scallop port is checked only against the stored NCI
  (no stage oracle). Default until you answer: (c); the harness stops at that import.
- ANSWERED 2026-11-14 (N13-27: option (b), the linker geometry in mm as mchmm does) M1-L1 (2026-11-13, M1 twelfth pass, hst_linker.md 18.3) The HST linker's lead test (N13-21: strict on our own values). With the passes plan
  now equal to mchmm.dll's, PARALLELSTEEP op 5 and RESTPASSES op 18 differ from the DLL (and the stored NCI) only in the first approach:
  at a flat pass start the test compares two values that are both R^2 (0.005625), and on the port's numbers one is 2 ulps larger, so the
  lead-in arc is "blocked" and the approach falls back to the SAFE plunge; the DLL keeps the arc. The DLL computes in mm (the macro's x 25.4),
  the port in the part's unit; the DLL's operation order alone did not change the result. Options: (a) keep N13-21 as is (SAFE first approach
  on these ops); (b) run the linker's geometry in mm as mchmm does (no deviation: Mastercam's own unit; the tie may then fall the DLL's way, or
  not - still strict on our values); (c) DEVIATION: treat an exact tie within a few ulps as clear. Default: (a) until answered.
- ANSWERED 2026-11-12 (N13-10: decode the method-1 path 0x141f and port it; refused until then) SS-8 (2026-11-12, S7, formats/surface_selection.md 8.1) Which check-surface method for HST Waterline / Raster / Spiral / Radial /
  Project: Mastercam takes it from the registry value "HST check trimming method" (mutlGetHstCheckTrimmingMethod; 1 = the kernel trims
  the passes to the checks, 0 = the checks are machined as drive; absent in your hive -> 1). Method 1's plan (Edit Passes to Surfaces
  0x141f) is NOT DECODED, so such ops are refused. The only corpus op with checks, TUTORIAL 2 SWARF_METRIC op 3 (Waterline, 43 checks),
  was generated by its author with method 0 (its record 0x172c = 0). Options: (a) refuse (default, as Mastercam on your machine would
  use method 1); (b) add the setting to our Configuration (default 1 = refuse; 0 = merge, as decoded); (c) DEVIATION: for a stored op,
  use the method its record 0x172c says (Mastercam recomputes it on regeneration). (a) is in place. Measured with method 0 forced
  (MCAM_HST_CHECK_TRIM=0, testing): SWARF_METRIC op 3 cutting ours->ref p95 13.36, length 11507 vs 10239 (drive only: 1.27 / 10473) -
  the stored NCI is NOT method 0's, so its 0x172c = 0 does not tell the method used; (c) is not supported by the data.
- ANSWERED 2026-11-12 (N13-8: bodies + a Body / Face toggle; DONE 2026-11-13, surface_selection.md 6 "N13-8 / N13-9") SS-6 (2026-11-12, S4, formats/surface_selection.md 6 "S4") A click on a solid in the drive / check prompt: body or face? Mastercam
  decides by the Selection Bar's solid buttons (OnSolidsSelectBody / Face toggle the pick mask); their state when the 3D toolpath
  selection starts is NOT DECODED. The port picks whole bodies (every 2017 corpus 3D op references whole bodies) and faces only via
  the core flag ms_sld_faces (script drvfacemode:1) - there is no button for it in the Qt app. Options: (a) keep bodies, add a
  Body / Face toggle to the Qt app as Mastercam's Selection Bar buttons (default proposal); (b) decode the Selection Bar state first.
- ANSWERED 2026-11-12 (N13-9: worker thread + progress; DONE 2026-11-13) SS-7 (2026-11-12, S4) Generation after the picks that follow the op dialog (plunge corners, blend / curve chains, start point) runs
  on the UI thread without the progress window the op dialog's OK uses (ours). OK, or move it to the worker thread (front-end work)?
- Note for the user (S4): the brief said "Esc at each prompt -> 427"; the decompile says Esc in the drive / check prompt deletes the new
  op at once without asking (msurf_assoc's cancel flag, LAB_180017532), and 427 is asked for the op dialog's Cancel, a failed pick
  after the dialog and a failed generation. The port follows the decompile.
- ANSWERED 2026-11-12 (N13-7: refuse with a message) SS-5 (2026-11-12, S3, formats/surface_selection.md 5 / 7.4) A stored solid face pick (SldToolPOp face list) names its face by an McEptr
  record; Mastercam finds the XT face only from the names a Parasolid replay of the history re-creates (FUN_180269310), using the stored
  box centre just to choose between faces of the same name. We cannot replay. (a) leave stored face picks unmapped (default: S2 / S4
  treat those faces as NOT DECODED - refuse to regenerate such an op, or skip the faces with a message); (b) DEVIATION: map each stored
  face to the XT face whose own box centre is nearest the stored point (Mastercam's tie-breaker over all faces; our box is not
  Parasolid's, so it can pick another face); (c) emulate the history replay (not feasible). Affects 10 faces in 8 corpus refs
  (5-axis CURVE / DRILL TIP tutorials, 2018 Propeller); whole-body refs (all 3D ops of the corpus) are not affected.
- ANSWERED 2026-11-12 (N13-3: yes, own ray test) SS-1..SS-4 (2026-11-12, N13-2 / S1, formats/surface_selection.md 5): SS-1 may a solid-face click be resolved by a ray against our own
  tessellation of each XT face (Mastercam's Parasolid pick sld_array_pick is NOT DECODED), the lists / types / storage staying as decoded?
  SS-2 move the start point / plunge corners / blend chains picks after the op dialog as Mastercam does (default), or keep T-2 (before)?
  SS-3 the Configuration - Toolpaths "Toolpath/surface selection" group (Drive / Check / CAD file / Containment / Show dialog): add it to
  our Configuration now or later (the flow reads it either way, Mastercam's defaults otherwise)? SS-4 ops with solid drive / check faces
  stay refused at save until the SldToolPOp writer is decoded (stage S6) - OK?
- ANSWERED 2026-11-12 (N13-1: option (b), the existing substitute with a measured agreement rate) IF-2 (2026-11-12, N13, formats/interfacial.md 14.3) Silhouette Boundary drops every kernel curve for which mcce.dll's
  MCCECalcOffset(1, -|10 x tessellation tol|, the curve as a pock_bound, mode 0x100) returns nothing. The bound is ported
  exactly (viewer/ifk/ifk_offchk.c); MCCECalcOffset's offset core is NOT DECODED (also for the MCCE port, which uses a
  Clipper / own-construction substitute: mcce_engines.md 6.13). Observed with the DLL: kept iff the loop is wider than
  2 x 10 tol with its orientation matching "no parent = CCW"; open curves dropped. Options: (a) decode the MCCE offset core
  (about 175 functions, mcce_port_plan.md) - the Mastercam default; (b) use the existing exact-offset substitute
  (mcce_dyn.c sl_offset_m) for this yes / no test, checked against the DLL with `peloader mcmsurf.dll --mcms` O cases;
  (c) keep Silhouette Boundary greyed until (a). Recommendation: (b) with a measured agreement rate, (a) later.
- ANSWERED 2026-11-12 (N12-1: decode it, no OCCT stand-in; PARTLY DONE 2026-11-12: untrimmed NURBS surfaces ported and matched to the DLL, interfacial.md 12; trimmed surfaces next, solids need Parasolid) IF-1 (2026-11-11, O3 Interfacial): Turn Profile / Silhouette Boundary tessellate the selection with Mastercam's own
  wr_stl -> surf_to_stl -> make_surf_polygons (mccore, formats/interfacial.md 10.2), written as an ASCII STL (float values
  printed with %f). make_surf_polygons (and how solids are turned into surfaces for it) is NOT DECODED, so the kernel
  input cannot match Mastercam yet. Plan by the Mastercam default rule: decode make_surf_polygons. Question: until then,
  may the kernel ports be fed from our OCCT tessellation for testing / a preview (a deviation), or must they stay
  unused until make_surf_polygons is ported?
The 2026-11-09 decisions are answered (`user_decisions.md` N9-1 to N9-7). Queue, none started:
- N9-2 DONE 2026-11-09: Waterline contact angle limits other than 0..90 refused again (MCAM_WL_ANGLES=1 = diagnostic only, waterline_oracle.sh).
- N9-5 DONE 2026-11-09: Pocket Constant Overlap keeps the ring-closing record (decompile / DLL; differs from the stored NCI). The entry moves too: the next ring starts from the record.
- N9-1 DONE 2026-11-09: references updated after N9-2 / N9-5: gen_waterline 2742 points, gen_arearough 21127, gen_spiral 2511, rt_pock_refused 88, and rt_pock_cospiral 271 (changed by N9-5). gen_waterline: only the tolerance changed in 341327b; measured 5404 points at tol 0.005, 2742 at 0.0125 (old reference 14182 not reproduced at 0.005, not explained).
- N9-3 DONE 2026-11-09 (merged e3fe08b): Pocket types 2 Island facing, 3 Remachining, 4 Open pocket generate; each with its refused cases in pocket_engine_b6.md. No stored op of these types: DLL pieces checked, the ops unverified against Mastercam. A provenance audit of this round is running.
- N9-4 Flowline: start (about 39k decompiled lines, 1 stored op).
- N9-6 / N9-24 Stock Model generation and Wireframe Silhouette Boundary / Turn Profile: decompile Interfacial (ifsweep.dll + interfacial*.dll) and tpiftools.dll. STARTED 2026-11-11 (N11-4): formats/interfacial.md. Turn Profile kernel decoded and ported (viewer/ifk, self-test tools/tests/ifk_spinsil_test.sh ALL PASS); not wired into the app; oracle deferred (N11-5). Next: CurveXferMC curve-file writer, the mcmsurf side (wr_stl -> chains), Silhouette Boundary (reuses Shell2D), then Stock Model.
- N9-7 Contour Subprogram on LinuxCNC: M98 / M99 (0 of 194 Contour-family ops use it).
- N9-8 / N9-9 / N9-10 DONE 2026-11-09 (merged e3fe08b): Contour Remachining clearance as % x D; FUN_1800cd350 ported, the collapse rule gone; Open pocket and Remachining inferred items with code decoded, the rest labelled in pocket_engine_b6.md.
- N9-11 to N9-14 (before Flowline): fix the audit findings (formats/audit_2026-11-09_pocket.md), the four Standard Pocket problems, Contour Remachining's 0x1278 and 25 / 1, Facing panel 3002; then an independent review.
- N9-16 Wireframe to 100 %: splines, curves on surfaces / solids, the other missing commands, and an accuracy audit of the live commands.

Known failing (identical on plain toolpaths2 1985764+): wf scene bbox_cyl_pushpull (cylinder r 70.0532 / centre 49.9739, 25.0464 after the push-pull; golden r 70, centre 50, 25). Bisected 2026-11-11: first bad d75f566 (decoded Rectangle order, N9-22), as recorded in formats/audit_2026-11-09_wireframe.md 2b: bcyl_fit (matches the decompile) depends on the points' visiting order; Mastercam's points for a solid come from its own facets (FUN_1800587c0, TMP.STL), ours from our mesh, so neither value is Mastercam's. Settled only when Mastercam's solid faceting is ported; the reference stays unblessed until then.

Stale reference (2026-11-11): ~/mastercam-re/regress/base (fileop_cmp) gives 164/189 differ on toolpaths2 997bece itself (outputs identical before and after the O2 merge, 205 files), i.e. the baseline predates accepted toolpath changes; it needs re-baselining with the reasons per op before fileop_cmp is useful again.

Still waiting on the user:
- ANSWERED 2026-11-12 (N12-2: build the view exactly; DONE, see the top) O2 Curve Surface (formats/wireframe.md "Curve Surface"): the ordering of a curve's pieces (project_chains_onto_surfs LAB_180569df8) measures
  the 2D distance in a view whose z is the surface normal, made by find_view_k(n, .., create) (it may also take an existing view within
  2e-7 of n). Ours computes the same distance as |d - (d.n) n| without views: equal up to rounding, it can only change a tie between two
  pieces' ends at tol. Keep it, or build Mastercam's view (find_view_k / make_view_k) exactly?
- Mastercam on Windows: the solid proof file test, test parts for 3D roughing, and D3 (does the dirty X show on expanded / selected ops?).
  Checklist for the user: formats/windows_checklist.md (N11-3). Q31 answered (N11-1), Dynamic default kept (N11-2).
- ANSWERED 2026-11-13 (N13-18: on by default) M2 offset (2026-11-13, branch worktree-agent-a8fe68dcaeede06c4, mcce_port_plan.md 9 "M2 offset"): the Dynamic Mill link region sets
  S1 / S3 / S4 now come from the DLL-exact MCCECalcOffset port (1411 / 1411 unit cases identical to mcce.dll); on by default, MCCE_DLLOFF=0
  gives the old Clipper / own construction. Against the X9 NCIs it is mixed per op: 9 ops better (v11 / v12 17 0.202 -> 0.072, v6/7/8 31 max
  0.226 -> 0.102), 11 worse (v11 / v12 10 0.331 -> 0.393, v9 46 0.150 -> 0.164, the rest +0.001..0.003; v11 1 / v12 23 max 0.086 -> 0.205,
  which is what mcce.dll's own engine gives on the same input). mcce.dll's engine on our input now agrees record for record much further
  (v12 10 / 17 / 23: first difference record 0 -> 443 / 262 / 540). Question: keep it on by default (it is what Mastercam computes; the
  remaining gaps are downstream, e.g. v12 10 after record 443), or keep the old sets until no op is worse at p95?
