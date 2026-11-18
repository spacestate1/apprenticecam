# Surfaces tab: plan and status (started 2026-10-28)

Goal: every button of the ribbon's Surfaces tab (viewer/ui/layout.txt lines 184-230, 37 buttons) works as Mastercam 2017's
does, from decompiled code (no improvisation: what is not decoded stays greyed with "not decoded yet").
Mastercam builds these surfaces in MCCAD.DLL / MCCADUTIL.DLL as its own SURF_ID entities (class 0x100, formats/surfaces.md),
not Parasolid; the results are stored as SURF_ID records (kinds per surfaces.md 1).

## State at the start
- Surfaces are read (mcam_load.c Surf: raw record), evaluated (mcsurf.c), meshed (occt_mesh.cpp) and drawn (scene.c).
- Not there: picking / selecting a surface, delete / blank of surfaces, undo of surface edits, new SURF_ID records in the
  session, writing new / changed SURF_ID records on save (mcam_saveops.c writes new lines / arcs / points / D_ID only).
- Every Surfaces button is greyed except Offset (53557), which wrongly runs the wireframe Offset (label match; needs the FT
  guard Extrude has in viewer-qt/src/mainwindow.cpp).

## Decompile dumps already made (gitignored)
- decomp/surfcreate/: create_cmds.c (ExtrudeSurface, RevolvedSurfaces, RuledLoftedSurfaces, DraftSurface, FlatBoundarySurfaces),
  prims.c + prim_tosurf.c (create_* primitives), revs.c (create_revs, convert_revsurf_to_nbs), core_nurbs.c, core_helpers.c.
- decomp/surfedit/: cad_cmds.c (UnTrimSurfaces, RemoveBoundary, SplitSurface, OffsetSurfaces, sld_convert_to_surfs), normals.c
  (change / set / orient_normals), core_flip.c (surf_flip_normal, sel bits, put_special_undo, rewrite_ent), core_norm.c, arrows.
- decomp/surfedit2/ (2026-11-17): core_untrim.c (untrim_a_trimsurf, remove_a_trimsurf_bndy, find_outer_bndno, boundary_is_untrimmed_outer,
  outer_boundary_touches_untrimmed_outer, find_base_surf, check_edge, select_surface, dyno_surf, FUN_1808abe50), core_masurf.c (MaSurface
  allowables / subset, MaNURBSurf ctor), core_selarrow.c (display_normal_arrow_for_selection), cu_setmgr.c / cad_untrimmgr.c (panel
  managers), untrim_warn.asm (FUN_18030dfc0).
- decomp/surftrim/ (2026-11-17): trim_surface_1/2/3, SurfaceExtend, ExtendTrimmedEdge (apps, managers, worker classes, the mccore
  kernels and their closures; list in formats/surfaces_trim_decode.md).
- decomp/surfoffset/ (2026-11-18): OffsetSurfaces' app (cad_offset.c, depth 3), its manager / view model / lambdas, put_surf_in_db, the
  curvature analysis (core_curv*.c, core_gengrid.c), sktcher_* (sk_mastercam.dll.c); list in "D. Offset".
- Not dumped yet: FillHoles, fillet_surface_1/2/3, blend_2_surfs, blend_3_surfs, blend_3_fils, NetSurfCreation, FenceSurfCreation,
  SweptSurfaces.
- WPF panels: ~/mastercam-re/dotnet/mccad.ui (MCCAD.UI.Surface.*ManagerView), mccadutil.ui (SurfaceNormal*).

## Phases
A. Groundwork: surface picking (selection of SURF_ID entities: mesh hit + the wireframe), delete / blank, undo, new SURF_ID
   records in the session (put_surf_in_db: header fields, density, shading byte), save of new / changed SURF_ID records,
   route the Surfaces buttons by FT name (fix Offset).
B. Simple: Cylinder / Block / Sphere / Cone / Torus (create_* PARAM 0, the primitive panel loop). Block / Cylinder / Sphere / Cone / Torus DONE (see below).
C. Create from curves: Extrude, Revolve, Draft, Loft (ruled / lofted), Flat Boundary.
D. Edit: Normals Change / Set / Orient, Untrim, Remove Boundary, Split, From Solid, Offset.
E. Trim to Curves / Surfaces / Plane, Extend, Extend Trimmed Edges, Fill Holes.
F. Fillets to Surfaces / Plane / Curves, Two / Three Surface Blend, Three Fillet Blend, Net, Fence, Sweep.

Each command: decode (dump in decomp/<cmd>/, notes in a section below), port, panel (Qt, from the BAML), test
(tools/tests: a scripted run + a golden shot; numeric check against corpus surfaces of the same kind where one exists).

## Status
(per phase, with commits)

### A. Groundwork (branch `surfaces`, 2026-10-28)
- Surfaces are entities: Ent type E_SURF (0x100), Ent.srf = the F.surfs index, appended after every wireframe entity at load (the
  wireframe keeps its indices: project files reference entities by index). Surf.ent / .off / .edited. mcam_new_surf makes one from a
  SURF_ID record; Ent.septr = a session stand-in eptr (mcam_session_eptr) for references between new records.
- Pick (decomp/surfpick/): select_ent_univ / FUN_18020f4e0 (decomp/mastercam_seluniv.c l.403-411, 819-838): shading off ->
  dist_to_surf (mccore 0x1802ddba0) = the screen distance to the iso lines it is drawn with (scene_surf_wire); shading on -> not a
  wireframe candidate; afterwards gl_Hit_Surfs (mastercam FUN_180224220) = the cursor ray against the shading triangles, the hit
  nearest the viewer replaces the wireframe hit unless that one's view z is at least the surface point's. Surfaces are candidates only
  in the general selection and a Surfaces command's prompt (pick_surfs_ok; ours, by state). Window / polygon selection of surfaces:
  NOT DECODED (left out). sel_list (the wireframe commands) never holds surfaces; sel_list_surfs.
- Highlight: the selected / hovered surface's wireframe through ov_entity (FUN_180135ad0's two-pass dash). The shaded face highlight
  (GAF stipple) decoded and ported 2026-11-18 (formats/ui.md "Shaded face highlight (GAF stipple)"): in solid_mode 1 (render_cfg 4)
  a hovered surface = lit in the auto-highlight colour + flat black alpha 0x40 + auto-highlight colour alpha 0x80 through the 32 x 32
  mask 0; a selected one = lit in the select colour (render_cfg 4 never sets the GAF Selected flag); scene.c scene_srf_look.
- Delete: delete_ent (decomp/align/geo_core_a.c l.211-250): a deleted offset (8, base at D+0x20) / trimmed (10, D+0) surface takes its
  blanked, otherwise unreferenced base along. Undo / hide (Alt+E) through ent_off / ent_on (Surf.off).
- Save (mcam_saveops.c): new surfaces = their record with the writer's header (head(), attributes as built), references to entities
  made here remapped (mcam_surf_ref_offsets); deleted part surfaces dropped; edited ones rewritten in place (same length).
  Project (.mcamui, ours): SURF / SURFEDIT + SH hex lines, references to entities made here written as 0x7d00000000000000 + index.
- Left out for now (NOT PORTED, unchanged from before surfaces were entities): Transform, Copy / Paste, Analyze, AutoCursor
  candidates, the Bounding Box / Temporary Center extents of surfaces.
- Routing: every Surfaces-tab FT (cui_surf.c SRF_FTS) is greyed unless ported, never a wireframe command (fixes Surfaces > Offset).

### D. Normals > Change (mccadutil change_normals 0x18005b6b0, decomp/surfedit/normals.c)
- Loop on get_event(prompt mccadutilres 2383 "Select a surface to flip the normal", mask 0x100) until Esc; UndoTransStart / Commit
  around it = one undo step (U_SURFEDIT, the records before / after).
- A pick: put_special_undo(3); surf_flip_normal (mccore 0x1806fa810) toggles the surf header's +6 bit 4 (ent +0x6e); selection word
  bit 8 -> 2, else 2 -> 8; rewrite_ent; the operations using the surface are checked (dirty_ent_users); EraseArrows; the arrow
  (auto_highlight_normal_arrow 0x180208b50) at the parametric middle along calc_surf_unit_norm (N = Su x Sv), reversed by bit 4;
  colour 4 on backgrounds 2 / 10, else 0x56. EnableAutoHighlightNormals(1): the hovered surface shows its arrow.
- Ours: the arrow length (a tenth of the view height; get_x_scr_scl x the view's +0x760 not decoded), the head shape (GrArrow),
  derivatives by central differences; surf_back_draw (backside drawing while shading) not needed (both sides are drawn).

### D. Normals > Set (ported 2026-11-17, branch worktree-agent-aab335cc2a38c285b; viewer/core_ui/cui_srfedit.c, G_SNSET)
- Decoded: set_normals (mccadutil 0x180112b20) -> SurfaceNormalSetApp FUN_180111490 (decomp/surfedit/normals.c l.695, setn2.c,
  setn3.c). select_ents (FUN_180110f10, mccadutilres 1544 "Select Surfaces to flip normals toward the Current C-Plane", mask 0x100;
  none selected ends the command). DoNormals FUN_180110a20: per listed surface FUN_180110760 = 3 x 3 samples at the quarter points of the
  header range (a trimmed surface: its base's range), FUN_180110d80 = eval_surf mode 1 + calc_surf_unit_norm, negated with +6 bit 4;
  d = N . the Cplane's Z (GetActiveViewNo(0), view row +0x38): > 0.001 toward, < -0.001 away; away > toward -> put_special_undo(3) +
  surf_flip_normal. Selection word: bit 4 off, then FUN_180111fe0 8 (samples agree) or 2 (mixed); rewrite_ent + the ops checked.
  Status mccadutilres 4871; prompt 2383. Loop: a surface pick flips it (FUN_180111df0, bit 8 <-> 2); panel codes 0 / 2 end (commit),
  1 OK-New (commit, select again), 4 Reselect (UndoTransTerminate, select again), 5 Show normals (FUN_180112070), 6 direction radio
  (FUN_1801110c0 OnFlip: every listed surface flipped). Esc commits (FUN_180110e60). Panel = surfacenormalsetmanagerview.baml
  (Surface: Reselect; Settings: Show normals; Direction: Cplane +Z / Cplane -Z). Defaults: SurfaceNormalSetManager FUN_1800ac120
  (decomp/surfedit2/cu_setmgr.c): ShowNormals false; SaveRestoreLastParams FUN_1800acb90 restores false without dialog modality.
  The arrows: display_normal_arrow_for_selection (mccore 0x180538f50, decomp/surfedit2/core_selarrow.c): the surface's own header
  range middle, length get_n_y_pixels() * 0.1, UtColor(0xff00, 0xff, 0).
- Ported: all of the above. Tests: srf scenes srf_setnormals, srf_setnormals_reselect (steps snenter, sndump).
- NOT DECODED / ours: calc_surf_norm's degenerate-point fallback (a sample without a normal counts neither way); the modal static
  DAT_1802558c0 (Show normals starts off); which manager field IsDirection1 binds (the radio starts at Cplane +Z, what DoNormals
  produces); UtColor's arguments (ours: green) and GrArrow's head; the SelArrow pick (event 0x400000) and the window list event (0x10)
  NOT PORTED; surf_back_draw (both sides are drawn here anyway). The status text goes through UpdateStatusBarText (mastercam.dll
  0x180329cc0, decomp/surfedit2/mc_statustext.c: the frame's CBCGPStatusBar (+0x9bb8) pane 0, SetPaneText only when the text differs)
  = core_ui cui_status_text, the Qt status bar's first pane (decision S-6; pane 0's place among the X / Y / Z panes NOT DECODED).
  Cancel keeps the flips (the exit commits), as Mastercam (decision S-7).

### D. Normals > Orient (decoded in part, NOT PORTED)
- orient_normals (mccadutil 0x18010d650) -> SurfaceNormalOrientApp FUN_18010b420: the same loop as Set (prompt 1545 "Select Surfaces to
  flip normals by neighbor orientation", panel surfacenormalorientmanagerview.baml: Reselect, Show normals, Direction 1 / 2; codes as
  Set's). DoNormals FUN_180109ed0 works on the MaSurface / MaFace / MaCoedge topology (MaSurface::LoadSurf(.., false, 30.0),
  MaFace::GetMinMaxBox / SetLoopDirections / BuildCoedges, MaCoedge::IsFreeEdge / IsOnSeam / IsOnPole / ComputePartners (a WORD
  2D array), GetPartnerMidPtArray, CanBeFlipped / FlipSurfaceNormal / SetNormalFlipped, MaCurve::PointMinDist against
  10 x surf_step_chord_dev, FUN_18003f7a0 normals compared by sign), then per surface the MaFace's flip flag (+0x10) against +6 bit 4.
- NOT DECODED: the Ma* coedge library (MaCoedge::ComputePartners, GetPartnerMidPtArray, CanBeFlipped, FlipSurfaceNormal,
  GetNeighbouringSurfaces, MaFace::BuildCoedges, FUN_18003f7a0, FUN_18010c040 / c090 / c0e0) - not dumped. Left greyed.

### D. Untrim (ported 2026-11-17; cui_srfedit.c, G_SUNTRIM)
- Decoded: UnTrimSurfaces (mccad 0x18030ea80) -> SurfaceUntrimApp FUN_18030d870 (decomp/surfedit/cad_cmds.c l.1260): get_event(mccoreres
  2377 "Select a surface", mask 0x100); a trimmed pick: UndoTransCommit of the previous, FUN_18030dfc0 (level warning: Format(mccadres
  7424, level), disassembly 0x18030e06b-79), UndoTransStart, untrim_a_trimsurf (mccore 0x1808afff0, decomp/surfedit2/core_untrim.c
  l.882) with mode 0 (Keep original surface) or 2. Code 4 (the check box changed): UndoTransTerminate + redo on the last pick, no
  warning. Codes 0..2 / Esc: commit, end. untrim_a_trimsurf: mode 2 deletes the trimmed surface and the base's references - 1; the
  base unblanked (word &= 0xfe7f), +0x6a = shading && shade-all; MaSurface::LoadSurf(base, true): IsSubseted &&
  !IsUVRangeChangedFromOriginal -> the base's range = the allowables (MaNURBSurf: knot[deg], knot[nk - order] of the knots normalised
  to 0..1, decomp/surfedit2/core_masurf.c); ops / solids: tp_substitute_ent / mc_op_modify_entity. Panel surfaceuntrimmanagerview.baml
  (Keep original surface, default off: SurfaceUntrimManager FUN_1802fc2f0, restore FUN_1802fcda0).
- Ported: all modes 0 / 2, the toggle redo, the warning, undo (mode 2: one U_ENTS_MOD with the base's record before / after - the
  U_ENTS_MOD undo now applies a SurfEdits sx too, cui_undo.c). Tests: srf_untrim_keep, srf_untrim_delete.
- tp_substitute_ent (mccore 0x180037f10, decomp/surfedit2/core_untrim.c l.2583; decision S-9, ported 2026-11-17): with mode 2 (and
  Remove Boundary's store_new_trim_surf_ext mode 2) when ent_is_assoc(old) (+0x38 / +0x54 > 0): add_undo_eptr_reference(5) +
  tp_substitute_ent(old, new, 0, 1): SwapEntityIDs (planes: NOT DECODED, no plane here keeps an entity id); every TP_ID record naming
  the old id: +0x6c id, +0x70 the new +0x30, +0x80 eptr, kinds 1 / 2 / 3 / 0x1b also +0xc8 / +0xd0; rewrite_ent_assoc; each op not
  yet dirty: +0x94 = 1, +0xf2 = 1, +0x97 = 0 (UpdateListAndDB). Ours: TpRef.subst1 (in force while the old surface is deleted and the
  new one lives: tp_ref_subst - cam_refs_from_tp, uses_deleted and the save follow it; undo / redo need nothing else), the save
  rewrites the kept TP_ID records (mcam_saveops.c), the session ops' CamRef re-pointed with undo / redo (srf_tp_subst_sync); "assoc" =
  +0x38 / +0x54, or (ours) a stored op's TP record / a session op naming it (the parts this program writes keep +0x38 at 0). Not
  ported: group lists (grp_change_eptr / list_grp_append_ents); a second substitution of a surface made by an earlier one after that
  earlier one is undone. Tests: op scenes srf_untrim_sessop(_reopen), srf_op_make, srf_untrim_op(_reopen), srf_rmbnd_op(_reopen),
  srf_rmbnd_sessop (step tprefdump); cam_cli tprefs on the saved parts names the base / the new record. A stored Surface Finish
  Radial cannot be regenerated by this program (cam_fileop_type: no 3D stored-op path, before this change too): the session op is.
- NOT PORTED (refused with a message): a base that is not kind 0 (MaBicubicSurf / MaGenericSurf / MaOffsetSurf allowables);
  a NURBS base MaNURBSurf would regenerate (RegenNonUniformUKnotsFromDerivRatios condition, as mcsrfcrv.c fe_reparam); the
  allowable / range ratio >= 1000 case (CheckSurfaceToSubsetDeltaUVRatios -> Truncate); is_solid_dependent_on_curve (no solid here depends on a surface). NOT DECODED: the modal static
  DAT_1806a5648 (Keep starts off); MaSurface::SetMinMaxParams (taken as UMax - UMin = +0x40 from IsUSubseted's use).

### D. Remove Boundary (ported 2026-11-17; cui_srfedit.c, G_SRMBND)
- Decoded: RemoveBoundary (mccad 0x18008d310, cad_cmds.c l.3): select_surface (mccore 0x1802deee0: select_ent 4869 "Select a surface:",
  then dyno_surf mode 3 without AutoCursor, 7425 "Slide to boundary which is to be removed"); 7426 on untrimmed surfaces; the nearest
  segment by dist_to_finite_line (0x1804b6680) / t_on_line (0x1804b48a0), start best 1e38, stop below tol; find_outer_bndno
  (0x1808aa250); outer picked: boundary_is_untrimmed_outer (0x1808a69e0) -> 7428; outer_boundary_touches_untrimmed_outer (0x1808ab7f0) +
  check_edge (0x18047b620) -> 7429 and the command ends; inner with > 2 loops: yes / no 7427 (caption 5002) -> all internal loops.
  remove_a_trimsurf_bndy (0x1808ab8c0): the base range rectangle as the new outer, the other loops; one untrimmed-outer loop left ->
  untrim_a_trimsurf(.., 2, ..), else store_new_trim_surf(.., 2, ..) (store_new_trim_surf_ext, decomp/surfprim/storetrim2.c).
- Ported: all of it except the list below; one undo step per removal (U_ENTS_MOD: the new record added, the old deleted). Tests:
  srf_rmbnd, srf_rmbnd_all, srf_rmbnd_part_op (step trimdump).
- NOT PORTED (refused with a message): the picked outer touching the base's range (FUN_1808abe50 re-routes the outer along the range
  edges, decomp/surfedit2/core_untrim.c l.2121 - read, not ported); an outer loop that is not the record's first
  (boundary_is_untrimmed_outer reads past the point array then). Surfaces used by operations: tp_substitute_ent (see Untrim, S-9). Ours: the new record is mcsurf_build.c
  trim_rec with the old record's +0x109..+0x10b / +0x118..+0x127 copied and its normal bit / colour / level / attributes; the base's
  reference count stays (+1 inc_refs_eptr, -1 delete_ent). No function panel, as Mastercam (decision S-5, geo_has_panel): Esc ends it.

### D. Split (decoded in part, NOT PORTED)
- SplitSurface (mccad 0x1803181e0) -> FUN_180315f80: panel surfacesplitmanagerview.baml (Direction U / V, Attributes Use current /
  Use original); prompts 0x949 / 7452; a pick -> dyno_surf mode 3 with AutoCursor (7430 "Slide arrow to splitting location") ->
  FUN_180316cd0: 7453 when the point is on an edge (within 0.0001 of the range); untrimmed NURBS / bicubic 4x4: FUN_180317570;
  otherwise make_constant_spline at the u or v, trim_surf_to_cons on both sides (the side points moved 10 % inward, in_polygon
  checks) -> two store_new_trim_surf_ext (2 then 0), the original's reference fixes; live colour, replace_ent on the attribute
  change, TpSubstituteSrfEnts.
- Dumped 2026-11-17 (decomp/surfedit2/core_split.c: get_uv_lims 0x1800e1ed0, make_constant_spline 0x1800e33c0, trim_surf_to_cons
  0x1808ae3e0 + depth-1 helpers (hit_polygon, FUN_1808ab040 / a9aa0 / aa9d0 / b0590 / a8bc0 / aa450 / a6cb0, reduce_uv_data,
  array_2d_to_nurbs, put_cons_in_db); decomp/surfedit2/cad_split.c: FUN_180317570). FUN_180317570 (untrimmed NURBS / bicubic 4x4):
  eval_surf at the arrow's (u, v), MaSurface::LoadSurf(.., true, 30) + PointMinDist, then the MaSurface virtual split (+0x30 in u /
  +0x28 in v, the new halves written by +0x90), the attributes (Use original: both rewritten in the old colour; Use current: main
  colour), delete_ent of the original. NOT DECODED: the MaSurface split / save virtuals (MaNURBSurf's +0x28 / +0x30 / +0x90, the
  Ma library) and trim_surf_to_cons' helpers not read yet (they overlap Trim, which another branch decodes in cui_srftrim.c).

### D. Offset (decoded 2026-11-18, ported; cui_srfedit.c, G_SOFFSET)
- Dumps (gitignored): decomp/surfoffset/cad_offset.c (FUN_1802f1420 + ctor / dtor, callee depth 3, 116 functions), mgr_slots.c /
  lambdas.c / lambdas2.c (SurfaceOffsetManager slots and Value observers), vtables.txt, interop_vm.cs (ilspycmd
  MCCAD.Interop.SurfaceOffsetManagerViewModel), core_offset.c (put_surf_in_db and helpers), core_curv.c / core_curv2.c /
  core_gengrid.c (the analysis), sk_mastercam.dll.c (sktcher_live_color / main_color / updt_attrib, set_disp_mult_arrows).
  offset_a_surface itself: decomp/curves/core1.c l.8453 (mccore 0x18026c860). delete_ent: decomp/align/geo_core_a.c.
- SurfaceOffsetApp (ctor FUN_1802ef3c0): +8 the selection (DB_LIST_ENT* array), +0x10 its count, +0x18 the distance (ctor 1.0 / 20.0,
  overwritten by the build), +0x20 CArray of the surfaces made (+0x28 data, +0x30 count), +0x48 Move, +0x49 the toolpath question asked,
  +0x4a its answer, +0x4c the Cycle index (short, -1). vtable 0x1804ab640 ([1] FocusInEdit: a focus in an edit box skips non-panel events).
- Panel: surfaceoffsetmanagerview.baml. Entity: Mode Copy / Move (IsMove); Surfaces: Reselect; Offset Distance (EvaluatorBox with lock);
  Computer Analysis: Max + offset / Max - offset (read-only texts) and Calculate; Direction: Single flip, Cycle / Next, Flip (ReverseButton,
  Tag "_Flip", IsEnabled = FlipEnabled). Manager slots (vtable 0x1804972b8, from mccad.interop's view model): +0x28 / +0x30 FlipEnabled
  (+0x69), +0x38 / +0x40 EnableApply (+0x68), +0x48 IsMove (+0x70), +0x50 OffsetDistance (+0x80), +0x58 MaxPositiveOffset (+0x90), +0x60
  MaxNegativeOffset (+0xa0), commands +0x88 Reselect -> code 4, +0x90 Calculate -> 9, +0x98 Flip -> 10, +0xa0 Single flip -> 7, +0xa8 Cycle /
  Next -> 8 (FUN_1802ab010: PostMessage 0x8038); Value observers (ctor FUN_1802a9af0, lambdas FUN_1802aa4c0 / aa5a0 / aa560 / aa590): IsMove
  -> 12, OffsetDistance -> 11, MaxNegative -> 13, MaxPositive -> 14. Defaults: manager ctor IsMove false, distance 0.1 inch / 2.0 mm
  (registered "SurfaceOffset-OffsetDistance"); SaveRestoreLastParams FUN_1802ef320 -> FUN_1802ab110 without dialog modality: Copy, 0.1 / 2.0,
  unlocked (the modal statics DAT_1806a5578..88: NOT DECODED).
- The loop FUN_1802f1420: FUN_1802f21b0 = select_ents(mccadres 7420 (0x1cfc) "Select surfaces to offset", mask 0x100, mode 2, End
  Selection); nothing selected: the loop ends. Then FUN_1802aac90 reads the panel (IsMove, distance, GetLocked(false)) and the build
  FUN_1802efc30(Move, distance): UndoTransStart; per selected surface get_ent_from_eptr + offset_a_surface(1, ent, distance, 0, &new); a
  valid one is added to +0x20, with Move the original is draw_ent_erase'd (display only); sktcher_live_color(+0x20) (each one's colour =
  live_entity_color, rewrite_ent); EnableApply(1). get_event(prompt, 0x10008008 | 2 in Single flip, mask 0x100): the prompt is 7421
  (0x1cfd) "Select surface to flip" in Single flip, 7464 (0x1d28) "Select flip or next" in Cycle, else none (cleartextall).
  - Codes 0 / 1 / 2: the flip modes off; FUN_1802f0ad0 = sktcher_main_color(+0x20) (colour = main_color; ea_adjustment when ea_active),
    with Move FUN_1802efa30, UndoTransCommit (UndoTransTerminate when nothing was made), set_disp_mult_arrows(0), the lists freed, index -1.
    0 / 2 end; 1: set_clr_sel_bits, EnableApply(0), the distance written back as 0.1 / 2.0 (FUN_1802ab2a0: SetValue(v, false) = only into
    an unlocked box), select again.
  - FUN_1802efa30 (Move): per selected surface (the made one at the same index): the first one with ent_is_assoc asks
    tp_blank_surf_yes_no once per command (mccoreres 5028 "Operations Manager" / 5188 "Blanking surfaces used in a toolpath.  Update
    toolpaths to use the new surfaces?"); sel bits 4 and 2 off, rewrite_ent; delete_ent(eptr, 1, 1, 0, ..): param_3 = 1 -> put_special_undo(4)
    + sel bit 0x100 = the original is BLANKED, not deleted (decomp/align/geo_core_a.c: the local_52f8 branch) - it stays as the base of the
    offset surface; with Yes add_undo_eptr_reference(5) + tp_substitute_ent(original, made, 0, 1).
  - Code 4 (Reselect): FUN_1802f1190 = set_clr_sel_bits, UndoTransTerminate, with Move the originals redrawn, the lists cleared; FUN_1802f1350
    the two Max texts emptied; EnableApply(0); the flip modes off; select again.
  - Code 7 (Single flip): Cycle off (FlipEnabled(0), EraseArrows); FUN_1802f0fa0: set_disp_mult_arrows(1) and every made surface's arrow
    (FUN_1802efd50); the event mask | 2. A surface pick then (FUN_1802f1000): a made surface: FUN_1802f0120 (its arrow removed:
    DisplayArrow(.., -4)), FUN_1802f0480 (flip), FUN_1802efd50 (the arrow again); another surface: its sel bit 4 off (write_ent_sel).
  - Code 8 (Cycle / Next): Single flip off (set_disp_mult_arrows(0)); FUN_1802f0f50: EraseArrows, index + 1 (wraps at the selection count),
    that one's arrow; FlipEnabled(1). Code 10 (Flip, FUN_1802f0c10): with an index: arrow removed, FUN_1802f0480(index), the arrow again.
  - Code 9 (Calculate): the flip modes off, FUN_1802f07f0 (below). Code 11 (distance): the modes off; the panel read; |old - new| > tol ->
    FUN_1802f0cd0. Code 12 (IsMove): the modes off; changed -> FUN_1802f0b50 (Move: draw_ent_erase of the originals, Copy: draw_ent).
  - Event 0x8000 with the status bar codes 0xc / 0xe / 0x11 / 0x12 (mask 0x65000): sktcher_updt_attrib(+0x20).
  - After every handled event FUN_1802ef9d0 (AddToAutoCompleteList) and FUN_1802f12d0: the struct lock = 0 unless the distance is hard-locked,
    written back with SetLocked(l, false) (the soft lock follows). Esc / the loop's end: FUN_1802f0410 = commit (Terminate when nothing was
    made), the selection freed, set_clr_sel_bits, EraseArrows; SaveAutoCompleteList; DestroyFunctionPanelDialog.
- offset_a_surface(param_1 = 1, ent, d, param_4 = 0, out) (mccore 0x18026c860): the record = the surface's own surf struct (ent +0x68..+0x150)
  with kind 8; d negated when the surface's +0x6e bit 4 (normal reversed) is set. A trimmed surface (10): fill_in_ent of its base (+0x100),
  then while that is an offset surface: + its +0x118, fill_in_ent of its +0x120; the range (+0x70..+0x88) and the base eptr = that surface's;
  store flag 1 (blanked). An offset surface (8): its own +0x118 plus the nested ones, the base found the same way. Other kinds: the surface
  itself, its own range. +0x118 = d + the nested offsets, +0x120 = the base. At the middle of the range eval_surf(mode 1) and
  calc_surf_unit_norm on that struct (before put_surf_in_db: the +0xd8 size is the copied one) -> +0x100..+0x117 (the stored normal).
  param_1 = 0 only: make_approx_parametric_surface + create_nurbsurf_from_surf (not reached from this command). put_surf_in_db(struct,
  main_color (-> surfaceFrontColor), main_level, flag, out); the new record's sel |= 8. Then (param_1): the base's references (+0x28) + 1 for
  kinds 8 / 10, else the surface's own; the surface's sel |= 2; rewrite_ent. A trimmed surface also gets a trimmed copy: its surf struct
  with kind 10, D+0 = the new offset surface, alloc_trimsurf_data_ptr + memcpy of its points (+0x10c x 16 bytes) and loop table (+0x128 x 8),
  put_surf_in_db(main_color, main_level, 0) (reduce_trimsurf_data, the +0x124 byte 1), sel |= 8, the offset surface's references + 1 and
  the base's + 1 again (the (kind - 8) & 0xfd test: the base ends at + 2); out = the copy. The kind 8 record is 0x130 bytes (fixed data
  +0x30, formats/surfaces.md 1).
- FUN_1802f0cd0 (a new distance, in place): per made surface i (a trimmed copy: its base, the offset surface): sign = the stored distance x
  the old distance >= 0 ? 1 : -1; new = sign x distance, plus the nested offsets of selected surface i (a trimmed one's base, then down the
  offsets); the offset surface rewritten; surf_extent(3) of the made surface (the trimmed copy, or the offset surface) - a trimmed copy's
  offset base keeps its extents.
- FUN_1802f0480 (flip i): the offset surface's distance = -distance + 2 x the nested offsets of selected surface i; the made surface's sel
  bit 4 off, surf_extent(3), rewrite_ent; update_graphics when shading; with one surface selected FUN_1802f07f0 (the analysis) on the new
  state.
- FUN_1802efd50 (the arrow of made surface i): the offset surface (a trimmed copy: its base) at the middle of its range: eval_surf(1) +
  calc_surf_unit_norm = N; -N when the distance < 0; -N again when the stored distance == the distance + the nested offsets of selected
  surface i (exact compare); DisplayArrow(P, N, 1 = drawn reversed, 0.1, -2 = colour 4 on backgrounds 2 / 10 else 0x56, 1, 0) (mastercam
  0x1801fda20, decomp/surfedit/mc_arrows.c: length get_x_scr_scl x 0.1 x the view's +0x760).
- FUN_1802f07f0 (Calculate): per selected surface MaSurface::LoadSurf(eptr, true, 30) + MinMaxPrincipalNormalCurvatures(surf_step_chord_dev)
  (mccore 0x180487d10: CreateHuntingGrid -> MaSurfHuntingGrid::GenGrid(chord) 0x180495000 = TessellateUntrimmed(chord, 0, 0, dir, 5000) per
  direction, fewer than 5 values -> quarters, a failure -> 10 x 10; Curvatures 0x180481570 from FundamentalForms 0x1804848c0 at every grid
  point, the min k1 / max k2 kept); FUN_1802f22b0 writes 1 / k with format DAT_1804190b0 (4 decimals) into Max + / Max -, mccadres 5527
  "Unlimited" when |k| < 1e-6.
- Ported (2026-11-18): all of the above except the list below. The command is one undo step (U_ENTS_MOD: the new records, the records edited
  - references, sel bits, Move's blank - as before / after images). Ours: the panel's selection = Set Normals' select_ents (a pick toggles,
  Enter / End Selection ends it); draw_ent_erase of a Move original = Surf.off while the result is live (display only); the normal +0x100
  through mcpartline_mesh.c plm_eval / plm_unit_norm (bit-exact for kinds 0 / 1 and offsets / trimmed surfaces over them), else
  mcsrfcrv.c's frame; closed / extents through mcsb_finish (its numeric derivatives for kind 8, as every surface kind other than 0 here);
  the trimmed copy's loops through mcsb_trim_rec's reduce_trimsurf_data with the original's +0x108..+0x10b / +0x118..+0x127 and struct
  bytes 4..5 copied; the arrow's length / head as srf_ov's. The TpRef substitution (tp_ref_subst) is now in force while the named surface is
  deleted or blanked (Move blanks it).
- NOT PORTED (greyed / left out): Calculate and the analysis after a single flip with one surface (TessellateUntrimmed's chord mode is not
  transliterated: mcsrfint.c ports only GenGrid(void)'s angle mode) - the Max texts stay empty; the 0x8000 attribute events
  (sktcher_updt_attrib); ea_adjustment; the AutoComplete lists; FocusInEdit; the distance validator's limits (FUN_1802f48f0 ->
  FunctionPanelEditableDouble::Validate: not read - the box takes any value).

### C. Revolve (decoded 2026-10-28, not ported yet; decomp/surfcreate/create_cmds.c FUN_1802cc6c0, decomp/surfrev/rev_obj.c)
- RevolvedSurfaces -> panel ISurfaceRevolvedManager (BAML mccad.ui Surface*Revolved*); chaining (prompt mccadres 0x1cfb), axis pick;
  the build FUN_18008c9e0: UndoTransStart; rev_dir_arrow; create_revs(chains, axis eptr, axis ent, pick t, start, end, reverse, out).
- create_revs (mccad 0x18008cd20, decomp/surfcreate/revs.c): end angle normalised to start < end <= start + 2 pi; axis = the line's
  start -> end (pick t >= 0.5: end -> start, point = the end), unit, negated when reversed; per chain curve (not the axis): rev_type
  D+0 dir, D+0x18 point, D+0x30 / +0x38 angles, D+0x40 axis eptr, D+0x48 profile eptr; header u0 / u1 = curve_t1t2, v0 / v1 = the
  angles; flags +6: 0x40 | 2 (end - start >= 2 pi - 1e-5) | 4 (dot(eval_revsurf mode 1 at (mid u, v0) +0x18 vector, dir) > 0) |
  1 (the profile's ends within tol). Then by surface_type (config 10014; factory default 2, mccore FUN_1801b7d30): 2 =
  create_nurbsurf_from_surf -> convert_revsurf_to_nbs (mccore 0x180266570, decomp/surfcreate/core_nurbs.c l.4763) -> put_surf_in_db;
  1 = make_approx_parametric_surface (NOT DECODED); 0 = the kind 2 record itself (+ the axis / profile records' ref counts +0x28).
- put_surf_in_db (mccore 0x1802db160, decomp/surfpick/core_dist.c): colour main_color -> surfaceFrontColor when equal to main and not
  the background (not while reading / transforming); +1 density = surf_density (default 1), +2 = 1 with shading on and shade-all;
  kind 0: check_nurb_knots both ways; kinds 1 / 4 / 10: D+0x? flag; 10: reduce_trimsurf_data; surf_closed(1), surf_closed(2),
  surf_extent(1, or 3 with DAT_1812d9064); refused when a parameter range < 1e-12; attributes = main_attrib.
- Ported (viewer/mcsurf_build.c): surf_closed, surf_extent with FUN_1806f8ce0 / FUN_1806fa880 / surf_curv_geom / FUN_1806f9480
  (mcsb_closed / mcsb_extent / mcsb_finish), convert_line_to_nb / convert_arc_to_nb (mccadutil, decomp/surfrev/util_conv.c),
  convert_2darc_to_nurbdata, make_view_k, convert_revsurf_to_nbs + create_nurbsurf_from_surf's kind 2 tail (rev_nbs), create_revs for
  one profile (mcsb_revolve). Parametric spline profiles (convert_spline_to_nb) and surface type 1 are NOT PORTED (refused).
- Verified: tools/tests/surfhdr_test.c recomputes the header of stored untrimmed surfaces (150 corpus parts, 2317 surfaces): closed u / v
  0 differ, degenerate-edge bits 5, box 7, centre 2, radius 4, range 4 (the edge moves near the 1e-8 threshold); the curvature maxima
  differ on 10-18 % (ours: one-sided numeric derivatives; they only steer the flowline density). tools/tests/surfrev_test.c converts
  every stored kind 2 surface (63) with the port and finds the NURBS surface within 7.4e-4 of Mastercam's revolved one (the nearest-
  point search's limit; a rational arc is not parametrised as the angle).
- Command (2026-10-28): G_SREV in core_ui/cui_geo.c (a function-panel chain command, started by its FT only) + cui_surf.c srf_rev_*:
  chain_it (7418), the axis = a line (get_event mask 2; the pick's t_on_line), the build in the live colour, LV_LIVE | LV_REBUILD (the
  angles / direction rebuild: FUN_18008cbd0 / FUN_18008cb50), Reselect chains (code 4, FUN_18008cc00) / axis (code 5, FUN_18008cb10),
  OK-New commits and starts again with no axis (FUN_18008ca80). Panel from surfacerevolvedmanagerview.baml (Chains, Axis of Rotation,
  Angle Start / End in degrees, Direction 1 / 2). Colour: surfaceFrontColor = 56 (mastercam.dll .data 0x18087b86e) for the main colour.
  Checked in the app: a 270 deg cone, the rebuilds (flags 0x46 -> 0x44 at 180 deg, 0x40 with Direction 2), written and read back.
- Not ported: rev_dir_arrow (the axis direction arrow), chain_zig_zag's push (UtPushValue), parametric-spline profiles, surface type 1.
- View fit (scene.c view_fit) now includes the displayed surfaces (before: wireframe / solids only).

### Gaps filled (2026-10-28, second pass)
- Exact derivatives (mcsurf_build.c nurbs_ders): eval_surf modes 1 / 2 on NURBS = FUN_1805b1f10 -> FUN_1805b0fb0 (the hodograph) +
  FUN_1804b7f40 (rational, w floored at 1e-12) - ported as the basis-function form with nurb_interval's exact span rule (mccore
  0x1805b0630: a knot within 1e-10 above u counts as passed; zero-length spans searched from the end). surf_curv_geom's helpers dumped
  (FUN_18047c530 E F G, FUN_18047c8b0 L M N with UnitNormal(Su, Sv), 1e-12 floor). Corpus (149 parts without TURBO_IMPELLER, 2859
  surfaces incl. 982 trimmed): cu 3, cv 1 differ; k1 36 / k2 49, of them 25 / 38 on surfaces with a degenerate edge (flags 0xf00).
  TURBO_IMPELLER alone holds 759 curvature mismatches (stored values 0.95-0.99 x ours, unrelated to the box: headers carried over from
  another representation, NOT DECODED). Kinds 1 / 2 / 4 / 6 / 7 / 8 keep one-sided numeric derivatives (their eval_surf modes are not
  dumped).
- Trimmed surfaces (surf_extent's kind 10 branch, extent_trim): the range, the outer loop's sampling, the curvature fields = the base's
  (measured, 982 / 982).
- Revolve: parametric-spline profiles (convert_spline_to_nb -> convert_ppspline_to_nbspline; tools/tests/surfnb_test.c: 93 splines within
  2.5e-6 of mcsurf.c's evaluation, 5830 NURBS curves exact); rev_dir_arrow's circle + arrow (mcgeomsld 0x1800a5c60) in the overlay.
- Window / polygon tests for surfaces (ent_in_polygon_view -> FUN_180211b00, ent_intersect_polygon -> FUN_180211cd0, surf_screen_box)
  in window_select / polygon_select. They apply where pick_surfs_ok allows surfaces; this program's only window / polygon selection is
  chaining's (never surfaces) - a general-selection window does not exist here yet (TODO).
- Transform (cui_xform.c xf_surf + mcsurf_build.c mcsb_xform): xform_store_ent's surface branch FUN_1808db320 for kinds 0 / 1 / 10
  (FUN_1808d7c00, FUN_1808d8570, FUN_1808dbac0: the base first, a new blanked base), mirror flips the normal, FUN_1808db6e0's header
  update (types 2 / 3 move the stored box; others surf_extent(1)), xform_type per command (do_xform_*: Mirror 0, identity linear part 3,
  Scale 2, Rotate / Translate 3D 1). Move: the old base goes with the moved trimmed surface (srf_drop_base, delete_ent's rule). Surfaces
  are in select_ents (xf_selecting). Kinds 2 / 4 / 6 / 7 / 8 (transformed through their curves: FUN_1808d9950 / d7220 / db920 / d6f80 /
  d8280) are refused (counted in the refusal message) - NOT PORTED.
- Tests: tools/tests/srf_compare.sh + srf_scenes.txt (golden/srf: pick / delete / undo, the trimmed base rule, Change Normal, Revolve,
  Transform copy / move / rotate, the written parts); script steps srfdump, srfpick:N, srfend.
- Copy / Cut / Paste (cui_misc.c): the record (and a trimmed / offset surface's blanked base) on the clipboard, Paste = merge_db2 in
  place (the base first, the surface pointing at it).
- Analyze (cui_analyze.c): NURBS (6471) and trimmed-on-NURBS (6528) dialogs - Control Points / Degree "%u by %u" (mccadutilres 6169),
  Boundaries / Boundary points, Flip Normals (slot 119 FUN_18008e3c0: +6 bit 4 in the record at once, bitmap 0x45b / 0x1674), level /
  colour in the record, Propagate, OK = one undo step (surface transaction), Cancel = the records back. The other kinds' dialogs (6472-
  6476 Ruled / Parametric / Draft / Revolved / Fillet, 6524-6536 trimmed / offset on them; factory FUN_18008b600) NOT PORTED: those
  surfaces stay out of the Analyze list. Style / Width greyed as for the wireframe dialogs.
- Temporary Center (cui_snap.c): ExamineEntity_XYZ -> FUN_180151060 = the 8 corners of the stored box (blanked bases left out).
- AutoCursor: no surface candidates, as decoded (dl_search_fast has no class 0x100 case).
- Bounding Box / Stock extents: make_bounding_box -> FUN_180057940 uses MaSurface::GetMinMaxBoxUnexpanded / MaFace (the Ma* library,
  not dumped) - NOT DECODED, surfaces left out (as before).
- Transform of the curve-dependent kinds: FUN_1808d9950 (revolved) transforms the profile (D+0x48) and axis (D+0x40) records through
  xform_store_ent, the axis point / direction (negated with the normal flipped on Mirror), then FUN_1808db6e0; loft / draft / sweep /
  offset alike (FUN_1808d7220 / db920 / d6f80 / d8280) - NOT PORTED (corpus: 378 revolved, 23 draft, 28 swept, 16 offset surfaces of
  ~49 000), refused with the Transform's refusal count.
- Still open: a general-selection window (none for any entity type yet).

### B. Simple: Block / Cylinder (2026-10-28, third pass)
- Decoded (mccad decomp/surfcreate/prims.c, prim_static.c, prim_pick.c, prim_flip.c, prim_build.c; mcgeomsld decomp/surfprim/sb2s.c,
  trimrec.c, cylsweep.c; mccore decomp/surfprim/linuv.c, linuv2.c, storetrim*.c, evsurf1.c):
  - create_sz_block / create_sz_cylinder: static settings DAT_180648370 / DAT_180648980 + their initialisers FUN_1800014a0 /
    FUN_1800015f0 (sizes 0, axis view (0,0,1), ref (1,0,0), block anchor 1, cylinder sweep 0 .. 360); Surfaces = byte +0.
  - Block (dialog mccadres 6419 / collapsed 6420; FUN_1800118c0 / FUN_180013640 map it): +8 length, +0x18 width, +0x28 height (each
    with a lock byte), +0x38 rotation, +0x40 anchor 1-9, +0x48 / +0x60 / +0x78 axis / x / y (Cplane view), +0x90 both directions
    (flip button state 2), +0x91 expanded. Flip (FUN_18000efd0): state 0 -> 1 -> 2; 1 = -|height|.
  - Picks (FUN_180011b50): mccoreres 5917 base, 5918 corner (FUN_1800110c0: the delta in the block frame rotated by -angle, x2 for
    anchors 4/5/6 in x and 2/5/8 in y; FUN_18000d360 moves the anchor with the signs), 5919 height (rubber band view z). Locked
    sizes skip their pick. Build FUN_18000dbc0 -> FUN_1800102d0 (FUN_18000d450 sign normalisation, FUN_1800150a0 frame,
    FUN_1800112e0 anchor -> origin = centre of the bottom face, the rotation) -> a point at the base (store_ent, then the main
    colour) -> sld_basic_primitive 0xf -> FUN_1800146a0 (SolidBodiesToSurfaces, the solid deleted, the surfaces to
    surfaceFrontColor) -> FUN_18000d7d0 (uses eval_surf mode 1's Su at the middle: a no-op on a box's faces).
  - Cylinder (6415 / 6416): +8 radius, +0x18 height, +0x28 / +0x30 sweep, +0x38 / +0x50 / +0x68 axis / ref / third, +0x80 both.
    Picks 5904 / 5905 (rubber band type 8: radius) / 5906 (height). FUN_1800270e0 -> FUN_180028a10 (both: origin - h a, 2h;
    negative height: axis negated) -> sld_basic_primitive 0x11 -> ps_create_cylinder (radius / height >= tol, angles in 0 .. 360;
    full: CRCYSO, else FUN_180033e60) -> FUN_1800146a0.
  - ps_face_to_mc_surf: create_nurb_surf (blanked when trimming data came), the face's sense -> +0x6e bit 4; per loop
    create_uv_nurb_curve + linearize_uv_nurb_to_surf (decoded fully, incl. FUN_1806f8ad0, brk_spline_const_err, check_colinear);
    FUN_18003da10: one loop + check_untrimmed (check_edge: 0.01 / surf_step_chord_dev) -> the base unblanked, else
    create_trim_from_uvs (corner shortcut; kind 10: range = the points' box, +0x109 1, flags 0x70 | base bit 4, +0x108 min(loops,
    255), loops {start, count - 2}, the loop holding the smallest u first) + store_new_trim_surf_ext (the base's level / colour,
    normal bit) + put_surf_in_db (reduce_trimsurf_data). Measured: CAMSHAFT.mcam's Parasolid-derived records (N9x2 cylinders, trimmed
    N2x2 caps, m = count + 2, the loop closed by its end point).
- Ported: viewer/occt_prim.cpp (OCCT box / cylinder, NurbsConvert, each face's B-spline + pcurves, knots normalised to 0 .. 1 as
  Parasolid's are - decision S-1: the nets differ, e.g. a full cylinder 7 x 2 here vs 9 x 2 in Mastercam), mcsurf_build.c
  mcsb_prim_* (the records), cui_surf.c srf_prim_exec, cui_geo.c G_SBLOCK / G_SCYL (panel = the expanded dialogs; live rebuild on
  panel changes, committed by the next base point / OK / Apply). Checked: tools/tests/surfprim_test.c (cap loop points on the
  circle within 5e-15), the app (renders, undo / redo, written and read back), srf scenes srf_block / srf_cylinder.
- Picks (2026-10-28, decomp/rubber/: rb.c = mastercam.dll draw_rubber_band / init_rubber_band / FUN_180331dd0, isoaxis.c,
  setgrview.c, mapcursor.c, map2d.c, projiso.c): draw_rubber_band's param_2 = 1 for an AutoCursor point, 0 for the free cursor
  (map_cursor_to_3d on the Cplane; mccore_autocursor2.c l.189 / l.261). Radius (type 8, +0x88): free = the 2D distance through
  proj_iso_pt (get_x_iso_axis / get_y_iso_axis = the graphics view's rows, SetGrView +0x720 / +0x738), AutoCursor = in the
  cylinder's view (off the axis); a minimum put_rubber_band_double(0) when > tol. Height (types 0x17 / 0x1a): the delta in the
  graphics view, its depth zeroed for the free cursor, view_to_view into the solid's view: z. Block corner (FUN_1800110c0): both
  points world_to_view(gr view) -> map_cursor_to_3d(view, depth 0) = map_2d_to_view (along the view direction onto z = 0), the
  delta's x / y. Ported in cui_geo.c (geo_pick_ac per pick: geo_snap_ent; tests: geopick with a Z = AutoCursor). Checked: Isometric
  free picks give radius 8.16 / height 3.33 for (10, 0, 0) / (0, 10, 0); Top view free height = 0 (as Mastercam: no rubber band).
- Ours / NOT DECODED: CurveLength (MaCurve) for a uv curve: Gauss-Legendre; where the loop's closing point is added (measured);
  the AutoCursor kind 'K' (aPoint +0, DAT_1813fb338) that draws as the free cursor - every snap counts as AutoCursor here; the axis X / Y reference
  directions and the dialog's axis handlers; Line / Two points axis modes (NOT PORTED); the live preview is the surfaces themselves
  (Mastercam: a GrTemp solid tag); FUN_180033e60 (partial cylinder) beyond its inputs (OCCT sweeps from the start angle);
  check_model_onesurf, compute_surface_area's zero-area delete, MaFace::Make_MC_Ent for > 1 loop: not ported (none apply to a block
  or cylinder); Solids output (the Surfaces tab only).

### B. Simple: Sphere / Cone / Torus (2026-10-28, fourth pass)
- Decoded (mccad decomp/surfcreate/prims.c, prim2.c, prim_axis.c, cone_dlg.c; mastercam.dll decomp/mastercam_planeadd.c):
  - create_sz_sphere / create_cone / create_torus: statics DAT_180648030 / DAT_180649140 / DAT_180648d70 + FUN_180001360 /
    FUN_180001890 / FUN_180001740 (sizes 0, sweep 0 .. 360, axis view (0,0,1), cone +0x2a 1 = Top radius mode, +0x90 = tol).
    MastercamPlanes::SetActivePlane(3, 0) copies the Cplane into the WCS slot for the command (the (PlaneType, PlaneType) overload),
    so the vectors are the Cplane's (as for the cylinder).
  - Axis radios (all four solids of revolution): X = axis (1,0,0) ref (0,1,0); Y = axis (0,-1,0) ref (1,0,0); Z = axis (0,0,1)
    ref (1,0,0) - the cylinder's Y is now -Y (it was +Y here); the block's radios: X / +Y / Z.
  - Readers: sphere FUN_18000ad70 (+8 radius, +0x10 lock, +0x18 / +0x20 sweep, +0x28 / +0x40 / +0x58 vectors); torus FUN_18002da30
    (+8 major, +0x18 minor, locks +0x10 / +0x20, +0x28 / +0x30 sweep, +0x38.. vectors); cone FUN_180032710 (+8 base radius, +0x10
    height, +0x18 top radius, +0x20 taper angle, +0x28 / +0x29 locks, +0x2a Top radius mode, +0x30 / +0x38 sweep, +0x40.. vectors,
    +0x88 both directions, +0x90 = tan(taper) h (x2 both) or tol). Dialogs mccadres 6421 / 6418 / 6423.
  - Builds FUN_1800093f0 (0x12: FUN_18000abf0) / FUN_180030090 (0x10: FUN_180031f80 - both: origin - h a, 2h; negative h: axis
    negated) / FUN_18002ba60 (0x13: FUN_18002d8f0) -> ps_create_sphere (radius >= tol) / ps_create_cone ((base or top > tol), base
    and height >= tol) / ps_create_torus (tol <= minor <= major); full when start < tol and end > 360 - tol -> FUN_1800146a0.
  - Picks: sphere 5921 / 5922 (rubber band 0x1b: free = screen distance, AutoCursor = in the Cplane's view); cone 5913 / 5914
    (type 8, at least +0x90) / 5915 (0x17), the Top radius button 598 (type 8 at base + height x axis -> top radius, Top radius
    mode; FUN_180032250 case 4); torus 5924 / 5925 (type 8 major) / 5926 (type 8 centred on the major circle - FUN_18002d030 at
    the start angle in make_view_k(axis), FUN_18002d2c0 towards the major pick - in the view normal to axis x that direction).
  - The cone dialog's sync FUN_1800340d0: taper <= atan2(radius, h) (89.8 deg near 0 / 90); Top radius mode: taper = atan2(radius -
    top, h); Taper angle mode: top = radius - tan(taper) h.
- Ported: occt_prim.cpp kinds 2-4 (BRepPrimAPI_MakeSphere / MakeCone / MakeTorus, a cone with equal radii as a cylinder),
  srf_prim_exec, cui_geo.c G_SSPH / G_SCONE / G_STOR (panels, picks, the cone sync and Top radius pick). Scenes srf_sphere /
  srf_cone / srf_torus.
- Ours / NOT DECODED: the cut taper goes through the dialog's text (prec_tol digits) - kept unrounded; a top radius of about
  -1e-15 at the largest taper is taken as 0 (OCCT); the torus major-radius rubber band's view (param_6, assumed the axis view as
  the cylinder's); the base-point / radius / height re-pick buttons (544 / 25368 / 574 / 617 / 611 / 570) are not ported.

### C. Flat Boundary (2026-10-28)
- Decoded (mccad decomp/surfflat/flat.c, decomp/surfcreate/create_cmds.c l.4139; mccore decomp/surfflat/trimflat.c):
  FlatBoundarySurfaces -> FUN_18033dec0 (SurfaceFlatBoundaryApp): chain_it (mccadres 7423), FUN_18033d800: open chains ->
  message_box_yes_no(7422, 5073), No drops them; one chain of one entity -> trim_surf_from_flat_curve, else FUN_18033e460 groups the
  chains by their boxes (a chain strictly inside a group's box = an island; one strictly enclosing it = its outer, a second enclosed
  group merged) and trim_surf_from_flat_chains per group; FUN_18033f400 store_new_trim_surf(rec, 1 = the base blanked, 1 = main
  attributes), live colour; OK / OK-New FUN_18033dd60, Cancel FUN_18033ddb0; Add = FUN_18033dcd0 (chain_it, append_chains, rebuild).
  - find_chain_normal: the chain's points (a line its end, an arc 2 + end, a spline 19 + end) in 3 groups, n = unit((G1 - G0) x
    (G2 - G0)), depth = the mean of Gi . n (FUN_1808a9200 for one entity: an arc's view z / depth, a spline 21 samples in 3 x 7).
  - FUN_1808a85a0: the chains' box in make_view_k(n) (lines 2 points, splines (segments + 1) x 5 / points x 5, arcs 9), the chain
    holding all four extremes = the outer (else error), 10 % margins -> 4 corners at the depth; create_nurbsurf_from_4_points (2 x 2,
    range 0 .. 1) + FUN_1806f8a00 (the normal bit when det(Su, Sv, Cplane z) < 0).
  - The loops (outer first, then the others in order): chord mode (mccore statics surf_use_chord_dev 1, surf_step_chord_dev 0.001
    0x1813066f8 - the configuration's 1007 at run time, surf_step_dist 0.1 unused): a line 1 step, an arc round(|sweep| / sqrt(8 dev /
    r)) + 1 equal steps, a spline by brk_spline_const_err (tol x 10, its length, dev); uv = the view x / y relative to the base;
    the first point again when the last is more than 1e-6 away; the record: the base's struct as kind 10 - its flags, range 1/12 ..
    11/12 both ways, +0x124 = 1 (CAMSHAFT.mcam's caps carry exactly these: Flat Boundary surfaces).
- Ported: mcsurf_build.c mcsb_plane4 / mcsb_flat_trim (trim_rec shared with the solids' trimmed records), cui_surf.c srf_flat_exec,
  cui_geo.c G_SFLAT (chain command, Reselect / Add). Scene srf_flat.
- NOT DECODED / ours: CurveLength of a spline (200 chords); FUN_1808b0650's loop-crossing check (error 5) not ported; reduce_uv_data
  only when Mastercam's buffer overflows (not reached here); the error codes' messages (ours).

### From Solid (2026-10-28)
- Decoded (mccad decomp/surfsolid/; mcgeomsld decomp/surfprim/sb2s.c sld_convert_sel_to_mc_ents): sld_convert_to_surfs ->
  FUN_1803116e0 (SurfaceFromSolidsApp): the selection FUN_1803112e0 (get_event 0xb21a, mccadres 462, Enter ends; bodies / faces),
  FUN_180310fa0: UndoTransStart, sld_convert_sel_to_mc_ents(surfaces, colour flag 1, .., preprocess) -> ps_face_to_mc_surf per
  face; FUN_180312040: Use current attributes -> surfaceFrontColor + main level, else the face's colour + the body's level; OK /
  Apply FUN_180310e60: the bodies deleted unless Keep original solid, UndoTransCommit. SurfaceFromSolidsManager (FUN_1802f7870)
  defaults: +0x50 Use current 1, +0x60 Keep original solid 0, +0x70 Preprocess 0 (FUN_1802f8310 maps them).
- Ported: occt_mesh.cpp occt_xt_faces (xt_occt -> NurbsConvert, XT FACE order), cui_surf.c srf_solid_exec / srf_faces_make (shared
  with Simple), cui_geo.c G_SSOLID (body picks, Enter, Reselect, the settings), mainwindow Enter. Scene srf_from_solid (BRACKET4).
- NOT PORTED: face picks (the Selection Bar's solid buttons), deleting the original solid (Keep original solid off: the part solid is
  kept, with a message), Preprocess solid (FUN_18002fc40), the face colour (FUN_1800a0730: the solid's colour is used).

### Loft (Ruled and Lofted ported 2026-10-28 - formats/surfaces_loft_decode.md; open: make_spline_coefs_closed, surface types 0 / 1)
- RuledLoftedSurfaces -> FUN_1802b2190 -> mccore CreateLoftOrRuledSurface(surface_type, lofted, chains) -> loft_ruled_maker: chains
  "alike" (all_chains_alike: the same entity counts; ruled + NURBS -> FUN_180701c20 (each chain one NURBS curve); lofted: lines /
  parametric splines of order <= 4 -> FUN_1807012f0); otherwise break_ctour_smart -> boundary.dat ->
  filter_ctour_dat -> FUN_180702fc0. Dumped in decomp/surfloft/loft.c; ported in cui_surf.c srf_loft_exec (lf_*) + mcsurf_build.c.

### Analyze on surfaces (decoding, 2026-10-28; decomp/analyze/surf*.c, surf_vt.txt)
- Classes (mccadutil RTTI): CAnalyzeSurface (dialog 1083 "base for analyze surface"), per kind CAnalyzeSurfaceNurbs (6471 "NURBS
  Surface"), Ruled 6472, Parametric 6473, Draft 6474, Revolved 6475, Fillet 6476, and Offset* / Trimmed* variants of each (the trimmed
  ones show the base's fields too). Common controls as CAnalyzeSecondDialog (Previous / Next, Level / Color, Style 895 / Width 896,
  Propagate 891) + Flip Normals (844).
- Slot 115 (entity -> dialog): NURBS FUN_18008df40 - Control Points (926) = "ncu" + mccadutilres 6169 " by " + "ncv" (ent +0x108 /
  +0x10c), Degree (920) = "udeg by vdeg" (ent +0x111 / +0x112); Trimmed FUN_1800907e0 (the loop and uv-point counts, ent +0x128 /
  +0x10c); TrimmedNurbs FUN_180090990 (the base's control points / degrees + the trimmed fields). The separator comes from the ctor
  FUN_18008d4e0 (LoadString 0x1819).
- Next: the Flip Normals handler (message map), slot 116 / 113 (dialog -> entity, Apply), the other kinds' fields, then the Qt dialog
  (dialogs8.cpp AnalyzeDialog) and core_ui az_* for E_SURF.

### C. Draft (2026-11-17, branch worktree-agent-a4f5fddf971b042b2)
- Dumps (gitignored, main checkout): decomp/surfcreate/create_cmds.c l.368 (DraftSurface 0x1801273d0), decomp/surfextr/draftdlg.c (the
  CDraftSurfDlg range 0x180123f00-0x180127400), decomp/surfextr/extr.c l.1959 (FUN_180126b90), msgmap.txt (the message map
  DAT_18064e2a0), statics.txt (DAT_18064e250..), dis1.txt (the bytes of the 961 / 773 / Apply handlers), core_draft.c (mccore
  FUN_1802699a0 / FUN_18026a290 / FUN_180269120 / nbrat_3d_to_4d ...); decomp/surfcreate/core_nurbs.c (create_nurbsurf_from_surf l.3,
  FUN_180266ca0 case 6 l.1757, FUN_1802641d0 l.2010, FUN_180266ed0 l.3556, FUN_1802647b0 l.5047). Dialog mccadres 1084 "Draft Surface"
  (~/mastercam-re/work/occt/plan/mccadres_dialogs.tsv).
- Loop: chain_it(mccadres 5731 "Select line, arc, or spline"); UndoTransStart; the parameters FUN_180126310 (+0 Length mode = dialog
  +0x1550 == 0, +8 length, +0x18 running length, +0x28 angle (rad), +0x32 Split Draft, +0x34 the length Flip state, +0x38 the angle Flip
  state); FUN_180125d80 builds, sktcher_live_color; 0x6b / 0x67 (panel changes) undo_go_back + rebuild; 0x66 (Select 961) undo + chain
  again; 0x65 (Apply) commit + chain again; 100 (OK; OnCancel posts 100 as well) commit (sktcher_main_color + UndoTransCommit).
- Dialog (DDX FUN_180124570, message map): radios 1491 Length / 1294 Plane (+0x1550), 759 Length (+0x5568, lock 755), 575 Flip
  (FUN_1801249d0: 0 -> 1 -> 2, bitmaps 0x463-0x465; Split Draft 962 enabled at 2), 766 Running Length (+0x5578, lock 763), 772 Angle
  (+0x5588, lock 769, spin -360..360), 577 Flip (FUN_1801248e0: 0 -> 1), 773 Plane (0x68: get_plane_info), 962 Split Draft (+0x1630).
  Running Length = Length / cos(angle) (cos > tol, else Length) after a Length / Angle change (FUN_180124fd0 / FUN_180124f00), Length =
  cos(angle) x Running Length (FUN_180125090); each compares with its last value (+0x5548 / +0x5550 / +0x5558, more than tol).
  FUN_1801272a0: Length mode enables 759 / 755 / 575 / 766 / 763 (962 at state 2), Plane mode 773. First values (ctor FUN_180123fe0,
  DAT_18064e260..): Length mode, Length and Running Length 1.0 RegisterVariable(20, 1), angle 0, the locks 0, Split 0, Flip states 0.
- FUN_180125d80: a = the angle (degrees, negated by the angle Flip), slope: 90 -> 2e12, -90 / 270 -> -2e12, else -tan(a), negated again
  for ||a| - 180| < 1e-12; Split: the second side uses -a. Per chain entity: +6 bit 0 = its ends within tol; rev = its direction differs
  from the chain's first entity's; FUN_180126b90 with the length (state 1: negated), at state 2 again with the length negated.
- FUN_180126b90 (Length mode): a line whose ends coincide in the Cplane view's x / y is skipped; kind 6: +6 = 0x40 | closed | 4 (rev),
  D (+0x98) = viewz(Cplane) x length, d = +-slope (negated for rev), the curve eptr; bit 2 toggled for a negative length; |N| < 1e-12
  skipped; u = curve_t1t2, v 0 .. 1. surface_type (config 10014) 2: create_nurbsurf_from_surf -> FUN_180266ca0 case 6 -> FUN_1802641d0:
  exact (FUN_180266ed0) for |d| <= 1e-5, a line, or an arc whose view z is parallel to unit(N) (|dot| >= 0.99999): u = the curve's nb_type,
  v degree 1 (0 0 1 1), row 0 the curve, row 1 without a draft each point + N ((P / w + N) w when rational and |w - 1| > 1e-5), with a
  draft a line + N - d|N| p, an arc of radius r - d|N| (the sign by (centre - C(t1)) . p) at its depth + N's view z, p = unit(C'(t1) x n)
  (|d| >= 1e12: +-|N| p only); otherwise the approximation FUN_1802647b0. 0: the kind 6 record + the curve's ref count. put_surf_in_db
  (main colour, main level).
- Ported: mcsurf_build.c mcsb_draft (surface types 2 (exact) and 0), cui_surf.c srf_draft_exec / srf_draft_sync, cui_geo.c G_SDRAFT (a
  chain command; panel = dialog 1084; codes 4 Select, 6 / 7 the Flip buttons). Scene srf_draft.
- NOT PORTED / NOT DECODED: Plane mode (get_plane_info mccore 0x180a2aea0; FUN_180125a40 / FUN_1801263c0 the per-entity length to the
  plane; surf_slice trimming) - the radio is there, the build refuses with a message, the Plane button is greyed; the approximation
  FUN_1802647b0 (a spline, or an arc not square to N, with a draft angle: FUN_1802699a0 / FUN_18026a290 / pt_tan_to_sp_coefs /
  proj_pt_on_coeff / FUN_180260910 dumped, not ported) - refused with a message; surface type 1 (make_approx_parametric_surface); the
  Cplane-change rebuild (event 0x8000 0x14 / 0x15) and sktcher_updt_attrib; the locks (CSkRLCombo), spin buttons, recent-value combos.
  Ours: C'(t1) of a spline by a forward difference (only its direction is used). The test hook could not chain a lone Spline Manual
  spline (chainat), so the zero-angle spline path has no scene.

### C. Extrude (2026-11-17, same branch)
- Dumps: decomp/surfcreate/create_cmds.c l.7 (ExtrudeSurface 0x180025a70) and its depth-2 callees; decomp/surfextr/extr.c (depth 3 from
  the builders + every CExtrudeSurfDlg method), extr2.c (FUN_180017520, FUN_180018c30, FUN_180023160, FUN_1800254a0); dis_18100.txt /
  dis2.txt / dis3.txt (disassembly where the decompiler lost arguments: FUN_180024290's distance / flag, FUN_1800241e0's t = 0.5);
  statics.txt (DAT_18064e520..). Dialog mccadres 1086 "Extruded Surface"; combo 884's items X / Y / Z (mccadres.dll RT_DLGINIT).
- The shape (FUN_18001c6d0 type 6; P = shape +0x40): P[0] height, P[1] taper (degrees), +0x10 base point, +0x28 axis, +0x58 view, +0x5a
  orientation, P[0x11] / [0x12] / [0x13] bottom / top / other-side lists (0x48-byte nodes 'L' / 'A' / 'S' / 'N' in the axis view,
  relative to the base point).
- Chain (FUN_18001b2a0): chain_1(mccoreres 7441 "Select Chain of Lines and Arcs or one closed spline"); a spline unless it is the only,
  closed entity -> 7445; FUN_180019fa0 the normal: the chain's points (a line its end, an arc 1/3, 2/3 and its end, splines their nodes)
  in three groups (n / 3, the rest to the first two), the group means (sums started at v_z_3d), UnitNormal(G1 - G0, G2 - G0); < 3 points
  -> 7446; make_view_k(normal) (find_view_m / find_view_k snapping onto an existing view: left out); the nodes (arcs re-expressed in the
  view by FUN_180015500, a full circle as two halves, reversed entities flipped) and their box; not closed -> 7447 / 7448, Yes adds the
  closing line; the base = the box centre at the first point's depth, the nodes relative to it; FUN_180019450 the orientation.
- First build (FUN_18001f090 + FUN_180018100): the top list = ccomp_pock offset of the bottom by tan(taper) x height (FUN_180024290:
  FUN_180018a00's bound (+3 = P+0x5a), set_min_fil_angle(0), side 'L', 'R' when the flag equals P+0x5a; the result must keep the entity
  count); both directions: P+0x5a toggled and -(tan(-taper) x height); a failure: 7444, taper 0. Then the dialog values in order: taper
  (its Flip negates), height (Flip state 1 negates; else the tops again), scale (FUN_180024dc0), rotate (FUN_180024ac0), offset
  (FUN_180024290(offset, Flip == 0), a failure 7443), axis (|dot| != 1: the dialog's; +0x70 the height too). A dialog change (0x68 /
  0x69): FUN_1800254a0 applies the differences against the last parameters incrementally (scale 1 / old then new, rotate -old then new,
  the old offset undone with the flag old Flip == 1, then the new) - results depend on the order of the edits, as in Mastercam.
- Build (FUN_18001f210 -> FUN_180020170, FUN_1800202e0): a point at the base and a blanked line base -> base + axis; mode = the height
  Flip state (1: P+0x5a toggled); the walls (FUN_180020410) node by node against the top list (mode 2 also against the other side's,
  P[0] / P[1] negated and P+0x5a toggled meanwhile): lines create_nurbsurf_from_4_points(b0, b1, t0, t1), arcs FUN_18001ddd0 (the two
  arcs' NURBS, v degree 1), +6 bit 2 = P+0x5a; the caps (FUN_18001f270; mode 2: the other side's instead of the base one, then the top):
  a plane over the list's box + 5 % of its x extent (FUN_18001c200), bit 2 from the middle normal . axis, the trim polygon (arcs in steps
  of acos((r - dev) / r), the last at end - step; lines their start), kind 10 over the blanked base. Colours: live_entity_color through
  put_surf_in_db; the commit (FUN_180017520) gives the point, the line, the walls and the trimmed caps the main colour; the blanked cap
  planes keep the live colour.
- Dialog: 990 Select Curve (0x66), 544 Select Base Point (0x67 + get_event, mccoreres 7442 "Indicate Base Point"; a point within tol of
  the base: nothing is rebuilt), 750 Height + 575 Flip Height (0 -> 1 -> 2), 752 Scale, 758 Rotate (-360..360), 764 Offset + 577 Flip
  Offset (enabled when the offset is not 0), 770 Taper Angle + 580 Flip Taper Angle (enabled when not 0); Axis group: radio 1492 + combo
  884 (OnAxisXYZChange: X (1, 0, 0), Y (0, -1, 0), Z (0, 0, -1), then 0x69), 1295 / 583 Axis Line, 25069 / 586 Axis 2 Points. First
  values (ctor 0x180127bf0, DAT_18064e530..): Height 1.0 RegisterVariable(20, 1), Scale 1, the rest 0, Flip states 0, combo index 2,
  axis (0, 0, 1).
- Ported: cui_surf.c srf_extr_exec (xs_chain / xs_first / xs_update / xs_walls / xs_cap; the offsets through rm_ccomp_pock), mcsurf_build.c
  mcsb_arc_wall, cui_geo.c G_SEXTR (panel, codes 4-8, the base point pick). Scenes srf_extrude, srf_extrude_arc, srf_extrude_axis,
  srf_extrude_open.
- NOT PORTED / NOT DECODED: spline profiles ('S' / 'N' nodes: FUN_180018f50 / FUN_180019320 / FUN_180025250 / FUN_1800166c0, the walls
  FUN_180018c30 make_spline_from_pts + FUN_180023160) - refused with a message; Axis Line / Axis 2 Points (OnAxisLnPb / OnAxis2ptPb:
  get_event / point_proc_proj + MCMessageBox 0x1714) greyed; the find_view_m snapping of the normal; both directions with a taper: the
  other side's offset gets a bound whose ccw byte disagrees with its geometry (P+0x5a toggled) and the rm_ccomp_pock port returns nothing
  for it, so 7444 follows and the taper is reset (Mastercam's MCCECalcContourOffset result for that input: NOT DECODED); a wall whose
  bottom / top nodes differ in type after an offset (Mastercam reads the arc node as a line) is left out; the locks, spin buttons and
  recent-value combos.
- v_z_3d (mccore 0x181371ca8) is zero in the image and has no writer in mccore.dll or mastercam.dll (SymWrites / RefsTo,
  decomp/surfextr/vz*.txt): (0, 0, 0) at run time (with +Z FUN_180019fa0 would tilt every extrusion axis). Decision S-3: mcsurf_build.c
  view_k (make_view_k: e = v_z_3d with e[i] = 1), mcsurf.c eval_draft (the degenerate Dir) and Revolve's arrow frame now use (0, 0, 0);
  one frame function (mcsb_view_k). The frame is unchanged for an axis along +-Z, its x / y differ otherwise. Both-direction taper:
  decision S-4 (stays refused). Re-run srf / solid / wf / stockplanes: solid (2) and stockplanes (16) as on master; re-blessed (each an
  axis not along +-Z, the same points): srf parts.srf_rev (revolve axis along Y: the box's -0.000 z becomes 0.000), wf ccurve_tilt (the
  cylinder rotated 30 deg: the circle's start angle 310.89 -> 0.00 in its view), cedge_sld_arc / cedge_sld_body (arc edges normal to Y:
  make_view_k now gives the Front matrix, so find_view_m reuses view 2 instead of making view 19; start angle 15 -> 60).

### C. Swept (decode started 2026-11-17, NOT PORTED)
- Dump: decomp/surfsweep/sweep.c (SweptSurfaces 0x18030a710, depth 3). SurfaceSweptApp (vtable; ctor FUN_180307aa0: +0x48 mode 1,
  +0x4c 0), the WPF panel ISurfaceSweptManager (FUN_1803091c0: CreateFunctionPanelDialog, the get_event loop 0x10008008; manager codes
  0 / 2 OK / Cancel, 4 reselect (FUN_180309040), 5-10 the panel's settings (FUN_1802c9ee0 reads mode / flag; 10 or a mode change ->
  FUN_1803090d0); Cplane events 0x15 rebuild in mode 1).
- Chains: get_swept_chains(across, +8), then (unless refused) get_swept_chains(along, +0x10) (FUN_1803081e0); mode +0x48: 0 Translate,
  1 Rotate (piecewise-planar along curve), 2 Normal to a surface (+0x18 picked surface, MaSurface::LoadSurf(.., 30.0)), 3 two along
  chains (NumberOfCurveChains == 2).
- Build FUN_1803088e0 -> FUN_180308920: UndoTransStart; MaCompositeCurve per curve chain; mode 1: IsPiecewisePlanar(1e-6) ->
  MaSweptSurfPiecewise, else MaSweptSurf(across, along, surface, mode, Cplane matrix, +0x4c); GenSurf(surf_step_chord_dev);
  NeedsSubdivisions(10.0) -> subdivision (vtable +0x68..+0x80); sktcher_live_color.
- NOT DECODED: the Ma* classes (MaCompositeCurve, MaSweptSurf, MaSweptSurfPiecewise, MaSurface - in mccore.dll, not dumped),
  get_swept_chains, the panel BAML (mccad.ui SurfaceSweptManagerView), the record the sweep stores (kind 7 arc sweep? or NURBS).
  Next: dump MaSweptSurf::* / MaCompositeCurve::* from ghidra_core with depth, then the BAML.
### E. Extend (SurfaceExtend) - ported for NURBS surfaces (2026-11-17, formats/surfaces_trim_decode.md 1)
- SurfaceExtend (mccad 0x180339600) -> SurfaceExtendApp FUN_180337330: prompt mccadres 7454 "Select a surface to extend" (get_event mask
  0x100), dyno_surf mode 3 with the AutoCursor (7455 "Slide arrow to edge to extend from"), the edge nearest the arrow's (u, v)
  (FUN_180338540), the build in an open transaction (extend_surf mccore 0x18032fb30 -> FUN_18032d240), the original deleted unless Keep
  original surface; panel changes undo and rebuild; the next pick / OK / Apply / Esc commit.
- Panel (surfaceextendmanagerview.baml; bindings by ilspycmd on mccad.interop): Mode By distance (Distance) / To plane, Style Linear /
  Non-linear, Keep original surface; defaults By distance, 1.0 in / 20 mm (RegisterVariable), Non-linear, Keep off (the session block
  DAT_18064f460 under enable_dialog_modality).
- FUN_18032d240: one knot span of length 1 added at the edge, p new control points per row: Linear = evenly along the end leg (FUN_180332080),
  Non-linear = a quadratic continuing the end's circle (FUN_180331070) rescaled until its length is the distance (FUN_180332680 /
  FUN_1803316b0), degree-elevated to p. Self-intersection test FUN_18032cd00 / FUN_18032c670 -> mccoreres 7458 "Surface extension has
  self-intersection - Keep surface anyway?".
- Port: viewer/mcsrfext.c (kernel), core_ui/cui_srftrim.c (command), cui_geo.c G_SEXT (panel, picks: as Curve Constant Parameter's
  dyno_surf). Oracle: tools/peloader --ext (mccore_ext.c: FUN_18032d240 and the test called in mccore.dll) + tools/tests/srfext_oracle.sh:
  582 corpus cases, 580 identical, 2 within 1.5e-8 (the oracle's find_view_k stub frame); 24 cases where mccore.dll loops forever (the
  Non-linear iteration at a pole) left out - the port stops after 100 000 iterations (user-approved deviation S-10: a message). Scene srf_extend.
- To plane (2026-11-17, integration int-s1): get_plane_info(7457 "Select plane to extend to") through the Plane Selection port (cui_plsel.c
  psel_begin; the function panel disabled while it runs): the app's plane local_aaa8 (origin 0, normal (0, 0, 1) at the start) and its ok
  flag local_aba5 (0 at the start) as decomp/surftrim/apps.c l.22116-22202 / 22440-22530 - a build in To plane mode asks only while no
  plane was accepted (Cancel: nothing built), the Mode change into To plane (code 6) always asks again, Style / Keep changes rebuild with
  the plane kept; extend_surf gets (origin, normal). Scenes srf_extend_plane (z = 25 above the top edge: the same surface bytes as By
  distance 5 in srf_extend_plane_dist), srf_extend_plane_codes, srf_extend_plane_cancel.
- Left (code 6 / 8 with an edge known but no live result builds nothing here; Mastercam rebuilds when local_aba6 is set).
- NOT PORTED (greyed / refused with a message): trimmed surfaces
  (extend_trimmed_surf and the trimmed edge rule: decoded, 1.3 / 1.7), kinds 1 / 2 / 4 / 6 / 7 / 8 (FUN_18032efb0, convert_entity_to_nurbs,
  FUN_18032eae0) and the 7456 NURBS-approximation question, the operations' substitution when the original is deleted (tp_substitute_ent:
  Keep off is refused on a surface used by an operation).

### E. Trim to Surfaces / Curves / Plane - core ported, Trim to Surfaces ported, Plane built but greyed, Curves NOT PORTED (2026-11-17, formats/surfaces_trim_decode.md 2)
- trim_surface_1 = Trim to Surfaces (FUN_18031ed90), trim_surface_2 = Trim to Curves (FUN_180323340), trim_surface_3 = Trim to Plane
  (FUN_1803287c0); a shared worker class family (mccad TrimSurfacesBase 0x1804174b0); kernels trim_surfs_to_surfs 0x180564220 /
  trim_surfs_to_curves 0x1805686b0 / trim_surfs_to_a_plane 0x18056ed00 -> MaTrimSurfToSurf / MaTrimSurfToCurvesOnSurf / ByView /
  MaTrimSurfToPlane (DoTrim, GenUVKeepPoint, MaFace::TrimToLoopsNew, Make_MC_Ent, store_new_trim_surf_ext).
- The shared core (the Ma library's face trimming: TrimToLoopsNew / SplitToLoop / TrimExtendToSurfEdges / Traverse / ReconstituteLoops /
  BinaryUVPointsAtTol / Break / Truncate ...) is viewer/mcsrftrim.c; MaTrimSurfToPlane's kernel too (mi_trim_to_plane); oracle tools/peloader
  --trim + tools/tests/srftrim_oracle.sh (results in surfaces_trim_decode.md 2 "Port of the shared core").
- Trim to Surfaces: kernel mi_trim_to_surfs (MaTrimSurfToSurf, commit 1d95ba3, oracle --trimss: 52 / 60 and 66 / 81 identical, the rest the surf / surf march on edge-tangent pairs); command G_STRIMS (cui_srftrim.c + cui_geo.c): the two sets by the
  selection + Enter, the checks 7449 / 7450 / 7451, the keep picks (4870, dyno_surf mode 3, 5431), the build (sets swapped when set 1 has one
  surface and set 2 several), Make_MC_Ent per face (a new blanked base for a trimmed surface / Keep original, else the original blanked as
  the base), the original kind 10 deleted, Use current attributes, the live rebuild on panel changes, OK / OK + New / Cancel commit (S-13),
  scene srf_trim_surfs. Greyed / refused: the Reselect buttons (FUN_18008fa90 / FUN_18008f9e0 not read), the IsEnabled rules, the status-bar
  0x8000 codes, surfaces used by operations when they would be replaced (tp_substitute_ent), kinds other than 0 / 10, regenerated knots.
- Trim to Plane: its build srf_trimp_exec (the kernel, Make_MC_Ent, Delete untrimmed surfaces) is reachable through the test step
  trimplane (scene srf_trim_plane); the ribbon button stays greyed: get_plane_info is ported (cui_plsel.c, merged in int-s1) but the command around the build
  (FUN_1803287c0's selection, the ISurfaceTrimToPlaneManager panel and its codes, Reselect) is not.
- Trim to Curves: NOT PORTED (~9 900 more decompiled lines: MaSurfProjectedCurve, MaTrimSurfToCurvesOnSurf / ByView, MaTabcylSurf).
- Questions (to become S-20.. when answered):
  - T-1: the new base of Make_MC_Ent (MaNURBSurf::Make_nbs_type -> create_nurb_surf_new) is made here as the original NURBS record with the
    knots normalised and the range replaced; create_nurb_surf_new's own header (density, flags) is not compared - port create_nurb_surf_new
    (and check against Mastercam's files), or keep this?
  - T-2: the Reselect buttons of Trim to Surfaces: decode FUN_18008fa90 / FUN_18008f9e0 (are the keep points asked again?) or leave greyed?
  - T-3: surfaces used by operations: refused when they would be replaced (as Extend); port tp_substitute_ent now (it is on the Untrim branch
    worktree-agent-aab335cc2a38c285b) or after that branch is merged?
  - T-4: Trim to Curves next (projection, ~9 900 lines), or Extend Trimmed Edges / Extend's trimmed surfaces first?

### E. Extend Trimmed Edges - decoded, NOT PORTED (2026-11-17, formats/surfaces_trim_decode.md 3)
- ExtendTrimmedEdge (mccad 0x18032e600) -> FUN_18032ccd0: a surface, one or two edge points by dyno_surf (mccadres 255 / 257), panel
  Style Mitered / Rounded, Distance (1.0 in / 20 mm), Solution 1 / 2; MaTrimmedEdgeExtend (DetermineLoopsToExtend, FindDuDvForOffset,
  ExtendSurface = MaNURBSurf::ExtendU/VByParameter, MaLoop::Offset -> MaPolyLine::Offset -> MCCECalcContourOffset flags 0x2f, DoExtend,
  Make_MC_Ent): a NEW strip surface on an extended copy of the base; the original is kept.
- Stopped at: an exact port needs ~4 500 decompiled lines (list in the decode) on top of mcsrfint.c; not read: MaLoop(MaSurface*),
  MaLoop::Direction / IsValid, MaFace::InitFaceData, MaNURBSpline::Reverse, MaNURBSurf::Make_MC_Ent, the default Style byte
  (uninitialised in the decompile - S-12: windows_checklist.md item 8), MCCECalcContourOffset with flags 0x2f. The button stays greyed.

### Decisions (2026-11-17, surftrim; formats/user_decisions.md)
- S-10 (E-1): Extend Non-linear stops after 100 000 iterations with a message (user-approved deviation; Mastercam loops forever).
- S-11 (E-2): get_plane_info (the Plane Selection dialog) is to be ported (a separate agent); To plane stays greyed until then. Done:
  the port merged with this branch in int-s1; Extend's To plane wired and enabled. Trim to Plane's button stays greyed: its command
  (FUN_1803287c0's selection, panel and codes) is not ported - only the build (srf_trimp_exec, test step trimplane).
- S-12 (E-3): Extend Trimmed Edges' default Style: to be checked on Windows (windows_checklist.md item 8).
- S-13 (E-4): Extend Trimmed Edges: Cancel keeps a pending result, as Mastercam.
