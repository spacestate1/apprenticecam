# Chaining direction (mccore.dll) — decomp/mccore_chaining.c

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

Globals: `chain_direction` (i16 @0x1812d3bf8, config 6001 "Direction for closed chains", 1 = see below),
`chain_direction_cursor` (u8 @0x1812d3beb, config 6002 "Use cursor position"). Config dialog binds them in
FUN_1801d2cd0 (controls 0x1771 / 0x1772).

## Picking one entity (single mode FUN_1801df0b0, add_chain @0x1801da9b0)
1. `ep_n_from_t_sel(ent, t)` (@0x1801e32c0), t = the pick's parameter on the entity:
   - S_ID 0x8 / NB_ID 0x80 (splines): end 1 if |t - t_end(+0xa0)| <= |t - t_start(+0x98)|, else end 0.
   - everything else (points, lines, arcs; t in 0..1, arcs = fraction of the sweep as in eval_curve): end 1 if t >= 0.5.
2. `add_curve_to_chain(ent, ep_n, ...)` puts the picked entity first, entered at end ep_n.
3. Pass 0 searches on from the other end (FUN_1801e6b50(n) = !n, FUN_1801e61d0 = endpoint n) appending; if the chain
   does not close, pass 1 searches from the start end and prepends. So the chain runs from the picked entity's end
   nearest the pick (by parameter) through that entity; a closed chain starts at the picked entity. Open and closed
   chains use the same rule.
4. `FUN_1801e8660(chain, !chain_direction_cursor)`: when 6002 = n, closed chains get their signed area in the active
   view (add_to_area over sampled points; arcs: floor(|sweep|/(pi/2)) + 2 samples) and are reversed when
   `(area < 0) != (chain_direction == 1)`. Sign convention of add_to_area not decoded.
5. `chain_start_at_point` (config 6009) -> FUN_1801e8be0 moves the start to a point entity (not ported).

Help (`chm/cadfields/MCCore/Config_fields/Direction_for_closed_chains_Use_cursor_position.htm`) agrees for closed chains.
Port: viewer/cam2d.c `cam_chain_orient` / `ent_pick_t` (2026-10-15). The user's config: 6001 = 1, 6002 = y, 6009 = n.

## Next entity at a joint (FUN_1801e3de0) — decomp/mccore_chaining_next.c
Endpoints are hashed (hash_endpoint, bsearch over a sorted table, window 3 * chain_tol, then 3D distance < chain_tol).
Candidates = ends of every other entity (not the current one; points excluded; type mask, colour / level / plane
filters when on) within tol of the tip, each end counted, chained entities included but not takeable (sel bit 0x40).
- 0 candidates: chain ends. 1: taken if not yet chained, else ends.
- 2 or 3: chosen = nearest free candidate. If another candidate is the same entity (both its ends at the tip):
  with a third, distinct candidate, stop when |tip - P_other| < |P_dup - P_other| (both points = endpoint number of the
  duplicate's end: the second FUN_1801e61d0 call reuses edx), else take. Without a duplicate: branch point (chain
  ends, branch flag) unless exactly 2 candidates and the current entity is shorter than chain_tol.
- 4+: branch point.
At a branch Mastercam's UI lets the user continue picking (flag at CHAIN+9, FUN_1801da8e0). Port: viewer/cam2d.c
cam_chain (2026-10-15); continuing at a branch is now ported as `cam_chain_extend` (below).

## Code: viewer/cam2d.c chaining section / cam2d.h `Chain` (state 2026-10-16)
`Chain` = entity indices in chain order + per-entity `rev` (traversed end -> start), `closed`, `avoid` (Dynamic chain
role 4, TP_CHN +0xe0), `solid_idn` (Solid Chain), `partial` / `branch` (CHAIN +10 / +9), `cut0` / `cut1` (Dynamic
start / end inside the first / last entity, CHAIN +0x28 / +0x30). While chaining for an operation (`cam_chain_all`, CS-7): Mastercam's
mask - lines, arcs, splines and NURBS in any plane (section "Curves outside the C-plane, splines and NURBS (CS-7)"); otherwise (Extrude,
the wireframe chain functions, Area) planar lines and arcs only (ours, CS-9 / CS-10).
The decompiles cited only in the code comments (FUN_1801dfae0, chain_area 0x1801dd160, FUN_1801df560, FUN_1801e3850,
FUN_1801e6980 / 69f0, FUN_1801e7740 / 7890 / 7500 / 75d0, FUN_1801ec820 / FUN_1801e25e0 / 28e0 / 2be0, FUN_1801e8230)
are not in `decomp/` (only `decomp/mccore_chaining.c` and `decomp/mccore_chaining_next.c` are); the rules below are
the code comments' statements.

| function | mccore source (as cited in the code) | rule |
|---|---|---|
| `chain_next` (+ `chain_brk`) | FUN_1801e3de0 | next entity at a joint (section above); colour / level masks = Options config 6018 / 6019 (`cam_chain_color_filter` / `cam_chain_level_filter`) |
| `chain_walk`, `cam_chain` | add_chain 0x1801da9b0 | forward from the seed's end, then prepend from its start; a single full circle is closed; sets `branch` |
| `ent_pick_t`, `cam_chain_orient` | ep_n_from_t_sel 0x1801e32c0 | the pick picks the start end (t >= 0.5 -> end 1); with 6002 = n the pick handler then runs `cam_chain_set_dir` (FUN_1801e8660) |
| `cam_chain_single` | chain_single FUN_1801df0b0 | the picked entity alone, closed when its ends meet |
| `cam_chain_set_dir` | FUN_1801e8660 | closed chain to config 6001 (1 CCW, 0 CW); area = sum (y1 + y2)(x2 - x1) |
| `cam_chain_window` | FUN_1801dfae0 | every window-selected unused entity seeds add_chain (t = 0, only window entities taken); 6001 direction; open / closed masks 6012 / 6013; then ordered |
| `cam_chain_order` | FUN_1801dfae0 / FUN_1801df560 tail, FUN_1801e8230 | from the search point the nearest chain start (closed: any entity start, made first; open: start or end, reversed); next search from its end (Zigzag, config 6000) or start |
| `cam_chain_vector` | FUN_1801df560 | each entity a vector crosses (database order) chained with the crossing's pick parameter; 6001, 6012 / 6013, ordered from the first vector point |
| `cam_chain_area` | chain_area 0x1801dd160 | every chainable entity chained; boundary = innermost closed chain around the point; open nested / closed nested or intersecting chains stay; infinite nesting off (6004): first-level islands only |
| `cam_chain_partial`, `cam_chain_partial_more` | FUN_1801db3f0 states 1-3, find_path_between_ents_normal FUN_1801e54a0 | walk from the first pick's far end to the last picked entity; no path -> mccoreres 4171 |
| `cam_chain_move_start` | FUN_1801e6980 / FUN_1801e69f0 | Start Back / Forward on closed chains |
| `cam_chain_extend` | FUN_1801e3850 | continue at a branch (prompt 4269); the pick must touch the chain end (else 4175) |
| `cam_chain_dynamic` | FUN_1801ec820 -> FUN_1801e25e0 / 28e0 / 2be0 | Dynamic start / end anywhere on an entity (cut0 / cut1); Break entities in Dynamic (config 6007) not done |
| `cam_chain_end_move` | FUN_1801e7740 / 7890 / 7500 / 75d0 | Start / End Back / Forward on open chains |
| `cam_chain_polyline` | (ours); splines: chain_to_ctour_array FUN_180282bd0 | chain as an XY polyline, arcs sampled to the given chord tolerance, `cut0` / `cut1` applied, duplicate closing point dropped; spline curves (E_SPLINE) broken into lines as chain_to_ctour_array does (formats/spline_chain.md) |

Tolerance: `cam_chain_tol(metric)` = config 1001 chain_tol default 0.002 mm / 0.0001 in (mccore FUN_1803bc7d0,
decomp/mccore_systol.c); was 0.0025 for every unit before 2026-10-16 (improvisation_review.md CAM small items part 1).
## Dialog modes, filters and end-of-chaining (2026-10-21) — decomp/mccore_chain_range.c, mccore_chain_feat_opts.c, mccore_chain_cfg.c
Decompiles: `DumpRange 0x1801d8000-0x1801f2000` (CChainDlg, run_chain_dlg, chaining core) and `0x1801f1fc0-0x1801fb000`
(Chain Feature, CChainOptions, hash); config setter FUN_1801b8270 + config page FUN_1801d2cd0 (mccore_chain_cfg.c);
uictrls UIVertDialog (decomp/uictrls_uivertdialog.c). CChainDlg message map @0x180d286d8, CChainOptions @0x180d2a110.

Config keys (FUN_1801b8270): 6000 chain_zig_zag, 6001 chain_direction, 6002 chain_direction_cursor, 6003
chain_ignore_depths, 6004 DAT_1813c4ca7 (infinite nesting), 6005 DAT_1813c4ca8 (reverse inner chains), 6006
nested_chain_sort_mode, 6007 chain_dynamic_break, 6008 g_chain_sync_mode, 6009 chain_start_at_point, 6010 DAT_1813c4ca6
(break closest entity to thread point), 6011 chain_section_stop_angle (deg -> rad), 6012 / 6013 open / closed masks,
6014 chain_mode_default (< 1 -> 1), 6015 ces_plane_filter, 6016 ces_ignore_depths, 6017 chain_section_stop_angle_on,
6018 / 6019 colour / level mask defaults, 6020 chain_default_last_mode, 6021 DAT_1813c4ca9 (C-plane radio default),
6022 DAT_1812d3c10, 6023 DAT_1813c4caa (Wait default), 6024 DAT_1812d3c12, 6025 chain_use_ent_mask, 6026
chain_surface_edges_in_single_mode, 6027-6030 lathe solids / arrow size / translucent. 1002 = chain_plane_tol
(@0x1812d3c00, .data 0.002; FUN_1801bab70 case 2).

| item | source | rule (port) |
|---|---|---|
| dialog mode | CChainDlg +0x17ec; ctor FUN_1801ead40 | DAT_1813c4bb4 if 6020 = y and set, else 6014; with 6020 = n chain_plane_filter = 6021 and Wait = 6023 at every dialog (`chain_dlg_init`); run_chain_dlg end stores the mode back (`chain_dlg_done`) |
| Point (549, mode 9) | FUN_1801edbb0; run_chain_dlg get_event(0x800020f, mask 1); chain_append_point @0x1801dcc10 | a picked point entity -> a chain of it (skipped when it already is the last chain); any other position (AutoCursor) -> store_ent of a point with the main attributes, then its chain (`cam_chain_point`, mcamui handle_click). Point chains: polyline of one point; generators skip them (n < 2) |
| C-plane / 3D (1482 / 1499) | FUN_1801ec7e0 / FUN_1801ec560 | chain_plane_filter 1 / 0 (`cam_chain_plane_filter`); DDX_Radio +0x17e0 = !filter |
| plane filter in picks | FUN_1801ddfb0 (Chain), FUN_1801db3f0 (Partial) | entity not parallel to the C-plane (FUN_1801fa6e0) -> post_warning 4213 |
| plane filter at joints | FUN_1801e3de0 l.8902 | candidate parallel and at the tip's depth within chain_plane_tol |
| plane filter in Window / Area / all-selected | FUN_1801f9e90(0x200) | entities not parallel or not at constr_depth (within chain_plane_tol) leave the selection (`plane_ok`, C-plane depth = grid_z) |
| Ignore depths | FUN_1801dddb0 | joint distances in the C-plane (2D; XY here) (`jd2`) |
| Start chain at point | FUN_1801e8be0 (add_chain l.2125, FUN_1801daec0 l.2339; not chain_single); FUN_1801e5180 | open: a point entity at the end reverses the chain; closed: the first entity whose start has a point becomes first; point search: type 1, colour / level masks vs the curve, plane filter, distance < chain_tol; window / vector ordering then measures from the chain start only (FUN_1801dfae0 l.5947, FUN_1801df560 l.5732) |
| Unselect after Window / Polygon | run_chain_dlg (+0x1f0aa count, +0x1f0ac flag), FUN_1801ee030 | Unselect removes all chains of the last Window / Polygon selection; any posted dialog message or pick clears the flag; Vector leaves it |
| Last | FUN_1801ed6b0 | Last already used and chains selected: warning 4211 and nothing added |
| End Forward 545 | FUN_1801f00d0 | enabled only for a partial chain (CHAIN +10); End Back 543 for branch or partial; Start 542 / 561 for closed, partial or branch; Dynamic 556 with a last chain (mill product) |
| Sorting | run_chain_dlg l.18214; sort_inside_to_outside @0x1801e98f0, sort_outside_to_inside @0x1801e9be0 | modes 1 / 3 inside -> outside (repeatedly the first chain of the deepest level), 2 / 4 outside -> inside (by level, list order); all chains must be closed (else 4221, unchanged). Nesting: chain_to_pock_bound at 2 px; parent = innermost enclosing (is_ctour_nested @0x1805e49e0: no intersection, all vertices inside) (`cam_chain_sort_nested`). optimize_inner / outer_nested_order (modes 3 / 4) NOT PORTED |
| Reverse inner chains | FUN_1801e83c0 (after sorting) | chains at odd nesting levels reversed (`cam_chain_reverse_inner`) |

NOT PORTED / NOT DECODED: (Solids selection: decoded and ported 2026-10-22, see "Solid chaining" below; the lathe
template 6519 is not), Chain Feature (CFChain search FUN_1801f54b0, ChainFeatureDlg dialog 9436, config 6800-6814), the entity mask
dialog CEntMaskDlg (6466) and colour / level mask dialogs, Break entities in Dynamic (FUN_1801e25e0 / 28e0, prompt
4230), sync modes (FUN_180214c00 / FUN_180215cc0), section angle (surface edges of trimmed surfaces only; see "Surfaces in the
Chaining dialog" below), lathe-only paths
(GetProdEnableBits & 0x10: thread points, chain_sort_options_assoc), the nested-order optimisers.

UI state machine (dialog modes, picks, Window / Polygon / Vector / Area / Point / Partial / Dynamic, Last, Options 405,
Drill Point Selection 2300, arrows): viewer/core_ui/cui_chain.c (moved from mcamui.c / chaindlg_ui.inc, qt_port_plan.md phase 4).

## Solid chaining (2026-10-22) — the Chaining dialog's Solids mode
Decompiles: `decomp/mccore_chain_range.c` (CChainDlg, run_chain_dlg), new `decomp/mcgeomsld_solidchain.c` (mcgeomsld:
selection state, OnSolidsSelect*, sld_init_solid_chains_selection, sld_sel_solid_chains -> sld_array_pick_loops and its
callees, SRefFaceDlg, SldToolPOp::ExtractToolPChainInput = vtable +0x170 FUN_18028ccd0, sld_loop_to_chain,
sld_generate_chains_toolp_op, highlight / arrow helpers), new `decomp/mccore_sldchain_misc.c` (colour config bindings
FUN_1801d3400, prompt_pocket), `decomp/mcgeomsld_toolp.c` (sld_chains_to_toolp_op / sld_unassigned_toolp_to_chains /
temp file), `decomp/mcgeomsld_snap.c` (sld_select / ps_select), `decomp/mcgeomsld_hover.c` (FUN_1800a44c0, hover).
Resources: `exports/mccoreres_dialogs.tsv` 6518, new `exports/mcgeomsldres_dialogs.tsv` (134) and
`exports/mcgeomsldres_strings.tsv`. CChainDlg message map parsed at **0x180d286c0** (the earlier note's 0x180d286d8 is
8 bytes into the first entry). Code: `viewer/core_ui/cui_sldchain.c` (+ cui_chain.c hooks), topology `viewer/btopo.h`,
`solid25.c body_brep_topo`, `xt_mesh.c xt_brep_topo`.

### Who may chain solids
- `allow_solid_chains` (mcgeomsld) is set only by `sld_init_solid_chains_selection`, which these callers run right
  before chain_it and undo with `sld_chains_to_toolp_op` after it (decomp/mcmill_all.c): ctour_toolpaths (Contour, incl.
  chamfer / engrave), face_assoc (Facing), slotmill (Slot Mill), FUN_1802cddc0 (2D High Speed chains: Dynamic Mill,
  Area Mill, Dynamic Contour, Peel Mill), FUN_1801ba4c0 (Blend; not in this program) and mccore prompt_pocket (Pocket,
  `chain_it_point(0x10ac)`). Not: drilling / point ops, the Extrude command, wireframe chain commands.
- run_chain_dlg (l.17736-17780): `+0x1f0b8` (Wireframe allowed) = param_6 != 2; `+0x1f0b9` (Solids allowed) =
  param_6 == 0 ? allow_solid_chains && sld_get_num_solids_visible() : param_6 == 2 (chain_it passes 0). The dialog
  starts in Solids mode (`+0x17f8`) when only solids are allowed, or (param_6 0) when `DAT_1813c4bb2` (the last
  chaining ended in Solids mode) and solids are allowed and visible.
- FUN_1801f00d0: **351 Solids enabled = +0x1f0b9 && allow_solid_chains && sld_get_num_solids_visible() >= 1**; 537
  Wireframe = +0x1f0b8. Port: `sld_allowed()` (the op types above while toolpath chaining, and at least one edge set:
  Extrude bodies on screen or the part's solids).

### The dialog (mccoreres 6518, 143 x 286 du)
FUN_1801edde0 (351): `+0x17f4 = +0x17f8 = 1`, `DAT_1813c4bb2 = 1`, the dialog re-created (FUN_1801f06d0 picks template
0x1976 = 6518; 0x1977 = 6519 is the lathe variant), msg 7 posted, 351 checked. FUN_1801ee2b0 (537): OnSolidsSelectEnd,
`+0x17f8 = 0`, `DAT_1813c4bb2 = 0`, back to 6464 / 1058. No expand icon (ShowExpandButton(+0x17f8 == 0)).
Images (OnInitDialog FUN_1801ecfc0 solid branch, members from DDX FUN_1801ec100, bitmaps from the ctor FUN_1801ead40):
Edge 2953 -> 1148, Linked edges 2956 -> 10477, Face 2954 -> 1149, Loop 2955 -> 1150, Partial loop 2957 -> 1151, From
back 2959 -> 1152, Unselect All 10204 -> 10205, End chain 12321 -> 7004, Back 542 -> 1139, Forward 561 -> 1147, plus
537 / 351 / 541 / 25381 / 525 as in wireframe mode. 3041 Face / 1499 3D = a radio (DDX_Radio 0xbe1 -> +0x17e4 =
!GetSolidChainingUseFace()). Handlers (message map): 2953 FUN_1801eca80, 2956 FUN_1801ed740, 2955 FUN_1801ed830, 2957
FUN_1801edac0, 2954 FUN_1801ece90, 2959 FUN_1801ecf30, 3041 FUN_1801ecf20 = SetSolidChainingUseFace(1) (disasm), 1499
FUN_1801ec560 = in Solids mode SetSolidChainingUseFace(0) (disasm), 541 / 10204 / 25381 / 542 / 561 / 525 / 12321 / OK
as below.

**Selection state** (mcgeomsld globals, session-wide; .data / bss values): Edges button DAT_1803baa57 = 1, Faces
DAT_1803baa56 = 1, Loops DAT_1803baa62 = 1, Partial DAT_1803e80f0 = 0, Back DAT_1803e80f1 = 0, contiguous edges
allowed DAT_1803baa8b = 1, finish contiguous DAT_1803e8115 = 0, SolidChainingUseFace DAT_1803e80dc = 0 (3D),
reference face DAT_1803e80e4. FUN_1800c2da0 calls FUN_1800a44c0(6, 1, 1, 1) for every pick: CanSelect Edges / Faces /
Loops / Partial, ShowReverse, ShowLast all on, Bodies off (disasm: cl kept for the setters).
OnSolidsSelect*: Edge = edges toggled (off -> loops and partial off); Loop = loops toggled (on -> edges on, off ->
partial off); Partial = toggled (on -> edges and loops on); Face = faces toggled; Back = toggled.

**FUN_1801f00d0 (solid branch)** - what the dialog shows: enabled 25381 = ShowReverse, 525 = ShowLast, 541 / 2959 =
active, 2953 / 2956 = CanEdges, 2955 = CanLoops, 2957 = CanPartial, 2954 = CanFaces; checks: d_edge = Edges, d_loop =
Loops, d_partial = Partial, d_face = Faces, d_back = Back; nothing checked and Linked box unchecked -> OnSolidsSelectFace
(faces on); partial -> edge / loop shown off; loop -> edge shown off; edge shown and contiguous allowed -> the Linked
box takes the edge check (Edge shown off) and End chain 12321 is enabled, else Linked off, SetSldFinishContiguousEdges
(true), End chain off; 542 / 561 enabled = d_loop. Default look: Loop + Face pressed.
Button handlers (all ignore clicks while a partial loop waits, GetSldPartialInProgress): Edge / Linked edges: checked ->
partial and loops off (Set..Checked(1) + toggle), contiguous allowed = Linked; unchecked or edges off -> OnSolidsSelectEdge;
Loop: checked -> partial off; unchecked or loops off -> OnSolidsSelectLoop; loops off -> edges off (Set 1 + toggle);
Partial loop: toggle; off -> edges off; Face: toggle; From back: toggle.

### Picking (sld_array_pick_loops, FUN_1800c2da0, FUN_1800a97c0 = sld_array_pick_loop_add)
- Prompt: mcgeomsldres 441 "Solid Toolpath Chain:\nSelect faces, edges, and/or loops", 101 "Click End chain, or
  continue from last end point." while DAT_1803e8114 is set (a Linked edge did not touch), 450 "Select edge for
  end-of-loop" (the partial loop's second pick).
- Pick mask: 6 (edges + faces) with Edges checked else 4, faces dropped without Faces. sld_select: edges within 10 px
  first (front to back; the From back button flips the ray, ps_select), then faces; hidden candidates dropped
  (IsPosVisible). Hit kind: face -> face pick; edge -> loop pick when Loops is on (partial loop when Partial is on),
  else an edge pick.
- Toggles: an edge already selected, the start edge of a selected loop, or a selected face picked again is removed.
- **Edge picks**: the edge's direction from the pick: PK_EDGE_ask_geometry's ends compared with the pick point (on a
  closed edge the points 1/16 of the interval in from each end): the chain runs from the end nearer the pick. Linked
  edges (contiguous allowed and the previous pick an edge): FUN_1800a2d00 - the new edge must touch the previous
  edge's travel end within chain_tol (with its start, or with its end: then it is turned); if it touches it extends the
  run (one chain), else: with finish-contiguous set (End chain 12321 = SetSldFinishContiguousEdges(true)) a new run
  starts, without it the pick is refused and prompt 101 shows. With Edge (contiguous off) every edge is its own chain.
- **Loop picks**: PK_EDGE_ask_faces; one face -> its loop. Two faces: 3D radio (UseFace 0): default = the face whose
  normal at a point (sld_get_face_normal_at_pt) is nearer the active plane's Z (|dot| larger; tie -> first face), then
  ALWAYS the **Pick Reference Face** dialog (FUN_1800a0dd0 `_sld_select_ref_face`: SRefFaceDlg = mcgeomsldres dialog 134
  "Pick Reference Face" 118 x 49 du, 273 "Other face" (FUN_18005d690: the other face's loop highlighted), OK, Help;
  modeless, the Chaining dialog hidden (show_chain_dlg(0)), get_event until OK). Face radio (UseFace 1): the last
  reference face (DAT_1803e80e4) when the edge lies on it, else the dialog (default: the second face). The chosen face
  becomes the reference face.
  SLD_LOOP_INP = { start edge +0, face index +4, rev +5, end edge +8 }. rev = (pick nearer the edge's end) for the
  second face, the opposite for the first; with the face order of PK_EDGE_ask_faces taken as the edge's positive fin
  first (FUN_1800c14b0 toggles the face index when it moves to an edge whose fin positivity differs - the index follows
  the fin sense), this is: **rev set = travel along the chosen face's fin = the picked edge travelled from the end
  nearer the pick** (the same rule as the edge pick and wireframe chaining). A loop pick with rev set moves the stored
  start one fin back (FUN_1800c14b0(inp, 0, 0, 0)); then (sld_loop_to_chain) the chain starts with the picked edge in
  both cases. Arrow: sld_show_loop_arrow.
- **Partial loop**: the first pick as a loop pick (incl. the reference face), then FUN_1800c2af0(body, 2 = edges, 450)
  repeats until an edge of the same loop (ps_edge_on_loop); no pick -> cancelled. With rev the stored start / end are
  swapped (the face index kept for that loop). Chain: from the first picked edge to the second inclusive, in the first
  pick's direction.
- **Face picks**: every loop of the face (PK_FACE_ask_loops in ExtractToolPChainInput), each from its first fin; the
  face's flag (Reverse) reverses them.

### From picks to chains (sld_loop_to_chain, ExtractToolPChainInput, sld_chains_temp_file_to_chains)
- Fins of the loop (PK_LOOP_ask_fins: Parasolid order, the face on the left seen from outside). No start edge (a face's
  loop): start fin 0, or fin 1 with rev (more than one fin). Start edge: its fin; with rev and no end edge the next fin.
  End edge: rev starts at the end edge's fin. rev clear: fins walked backward, each traversed against the fin; rev set:
  forward, along the fins; the walk stops after the end edge. An entity is the edge's curve (ps_edge_to_mc_curve,
  stored with store_ent_nodraw), entered at end 1 when traversed against the edge (PK_FIN_is_positive vs the walk).
  So a face's outer loop runs clockwise seen from outside (from above on a top face) and its holes counter-clockwise.
- Each pick in order makes chains: an edge run = one chain, a loop = one chain, a face = one chain per loop;
  add_curve_to_chain per curve, set_chain_closed(chain, nc_tol) (config 1008: 0.002 mm / 0.0001 in). No direction
  rule (chain_direction) is applied.
- The selection ends with OK (FUN_1801ed940: FUN_1801ecb70 = SetSldFinishContiguousEdges(true), OnSolidsSelectEnd) or
  Wireframe; FUN_18009fb70 keeps it for Last; FUN_1801df360 -> sld_chains_to_new_tp_ops stores it as SldToolPOps in the
  solids' history (chain position = number_of_chains then); after chain_it the op's caller runs sld_chains_to_toolp_op
  -> sld_unassigned_toolp_to_chains, which rebuilds the chains and **inserts them at that position** among the dialog's
  (wireframe) CHAINs (insert_chain; sld_chain_pos_removed lowers positions when wireframe chains are unselected). The
  wireframe CHAINs were sorted (Options Sorting / Reverse inner) by run_chain_dlg before that - solid chains are not.
  The op then takes chain depths as for any chain (ctour_toolpaths get_chain_depth_op): the curves carry the edges' Z.
- Esc during the pick: command 2 -> sld_array_pick_loops returns 0 -> FUN_1801df360 -2 -> run_chain_dlg turns Solids
  mode off and posts 537 (the selection is dropped). Cancel (FUN_1801ec5e0): OnSolidsSelectClear + Cancel.

### Commands (DAT_1803e80f4 -> FUN_1800c2da0 command codes)
| control | handler -> command | effect (on the last pick's kind, DAT_1803e80f8) |
|---|---|---|
| 25381 Reverse | FUN_1801edc50 -> OnSolidsSelectReverse (1, FUN_1800aaae0) | edge run: order and directions reversed; face: its flag toggled; loop: rev toggled (start kept) |
| 542 / 561 Start Back / Forward | FUN_1801ede80 / FUN_1801edf60 -> OnSolidsSelectBackward / Forward (6 / 5, FUN_1800aa5f0 / FUN_1800aa6c0 -> FUN_1800c14b0) | loop (not partial) only: the stored start one fin on (rev clear: Back = next fin, Forward = previous; rev set: the reverse) |
| 541 Unselect | FUN_1801ee030 -> OnSolidsSelectRemove (7, FUN_1800aa790) | the last edge of the last run / the last face / the last loop |
| 10204 Unselect All | FUN_1801ee0f0: MCMessageBox 5007 / 10206, Yes -> OnSolidsSelectClear (4) | all |
| 525 Last | FUN_1801ed6b0 -> OnSolidsSelectLast (3, FUN_18009f930 / f960 / f110) | the previous solid selection added; none -> mcgeomsldres 669 "There are no previous selections.", invalid -> 274 "The last selection is no longer valid." |
| 12321 End chain | FUN_1801ecb70 | SetSldFinishContiguousEdges(true) |
| 1 OK / 537 Wireframe | FUN_1801ed940 / FUN_1801ee2b0 | OnSolidsSelectEnd (selection ends; OK then ends the dialog) |

### Highlights and arrows
Selected topology (ps_hilight_edges -> ps_redraw_hilights -> FUN_180036fc0, colour -1): edges in
sldEdge_select_color = **config 5034** "Solid edge select color" (mccore FUN_1801d3400: key 0x13aa), faces filled in the
select colour when shaded (FUN_1800bc970; render_cfg 3 / 4) or drawn as wire in sldFace_select_color = **config 5035**
(0x13ab) when not shaded. Hover while picking (the solid pick mask is set during read_cursor_pos): an edge in the select
colour 5033 (FUN_180098b20 -> FUN_180042a00); a face gets the GAF AutoHighlight. Arrows: EraseArrows(true) before each
pick, then the new pick's arrows (sld_show_loop_arrow / SldShowEdgeLoopArrow start + end colour 0xc /
sld_show_face_loop_arrow) via DisplayChainArrow.

### Port (2026-10-22, both apps through core_ui) and what stays out
- Topology: the part file's solids from the XT (xt_brep_topo: FACE / LOOP / HALFEDGE order, fin positive = it is its
  edge's own halfedge, the direction xt_brep_edges gives the edge); the bodies made here have no B-rep - their faces /
  loops / fins are rebuilt from the slabs (body_brep_topo, **ours**): cap faces (one per outer region with its holes),
  wall faces (wall quads joined across non-edge sides), loops oriented as Parasolid's; so on Extrude bodies the start
  fin of a face's loop (Parasolid's internal order) and a ring edge's start point are ours.
- Chains: Chain.sld (1; 2 while the selection runs), the curves appended to F.ents with **Ent.gen** (not drawn, saved,
  or chained as wireframe: cam2d chainable()); arcs stored counter-clockwise (an edge running clockwise = the arc
  traversed end -> start). Projects (.mcamui) keep them as `CHAIN c L G<type>,...` (ours). Writing such an operation
  into a Mastercam part (SldToolPOp) is NOT DECODED: mcam_saveops leaves the op out with a note.
- Left out / NOT DECODED: the face fill highlight (the overlay draws lines; shaded selected faces show no highlight),
  the face hover look (GAF: the surfaces' look is decoded in formats/ui.md "Shaded face highlight (GAF stipple)", not ported for
  solid faces), the cursors, curves other than lines and XY circles (an edge of another class cannot be
  chained: the pick makes no chain), the PK_EDGE_ask_faces order itself (taken as above), FUN_1800ba300's point on a
  curved face (cylinders parallel to Z: dot 0; others: no normal -> 0), MessageBeep on a pick of another body during a
  partial pick (ignored), the lathe variant (6519, allow_solid_lathe_chains), the Bodies button, Help, positions of the
  Pick Reference Face dialog, selection of solids hidden in the Solids manager (all shown solids count as visible).

## Surfaces in the Chaining dialog (2026-11-17) — decomp/mccore_chain_range.c, new decomp/chain_srfedge*.{c,asm}
User report: "when in the chaining menu and I select 3d it should visible highlight 3d surfaces like mastercam right not just the
wireframes". Decoded from mccore 2017 (no stand-ins); new dumps: `decomp/chain_srfedge.c` (build_edgecrv_on_surf @0x180273970 with its
callees to depth 2), `decomp/chain_srfedge_store.c` (FUN_1800e4680, FUN_18054d250, remove_nodes, put_spline_in_db), `decomp/
chain_srfedge_mcs.asm` (make_constant_spline 0x1800e33c0..0x1800e4100), `decomp/chain_srfedge_undo.c` (FUN_1802c7ce0, the DbUndo
internals, put_spline_in_db, project_pt_onto_surf / build_grid / fill_grid / find_neighbor), `decomp/chain_srfedge_dl_mastercam.c` (mastercam.dll
DisplayList.cpp / DlPoint.cpp helpers), `decomp/chain_srfedge_startvals.txt` (the globals' addresses and start bytes).

**Which picks see surfaces.** The wireframe Chaining dialog's entity mask is `DAT_1813c4cbc` = 0x8f (points, lines, arcs, splines 8,
NURBS 0x80; run_chain_dlg l.17719, and FUN_1801fa9a0's caller when `chain_use_ent_mask` 6025 is off). run_chain_dlg (0x1801f0920)
l.17843-17850: **only dialog mode 2 = Single**, and only while config **6026 `chain_surface_edges_in_single_mode`** ("Allow surface
edges in single mode?", Chaining Options) is on, ORs SURF_ID 0x100 into the mask for the get_event and clears it after. Mode 1 Chain
(l.17837-17841 -> LAB_1801f0eaf), Partial 7, Window / Area / Polygon / Vector, Point keep 0x8f. **The C-plane / 3D radio
(chain_plane_filter) has no part in it**: chain_single does not test the plane filter (only FUN_1801ddfb0 Chain and FUN_1801db3f0 Partial
do), and nothing else in run_chain_dlg / CChainDlg reads 0x100. The lathe dialog RunLatheChainDlg (0x1801ee400, l.16393-16403) adds
0x100 for modes 1 and 2 (its decompile clears the whole mask to 0 after the event - lathe, not ported). Solids are not wireframe
entities: their hover needs the solid pick mask, which only the Solids mode sets ("Solid chaining" above; formats/ui.md "Solid selection
hover highlight").

**The user's configuration has 6026 = n** (`<windows>/Users/<user>/Documents/my mcam2017/CONFIG/mcamxm.config` line 267 "6026. Allow
surface edges in single mode? n", the same in <windows>/mcamx/config): with it Mastercam 2017 highlights no surface in the
wireframe Chaining dialog in any mode, C-plane or 3D - which is what this program did before. The user confirmed (CS-1) the option was on in the Mastercam session they described.

**Hover and pick** (with the mask): the auto-highlight FUN_1804df930 -> DrawAutoHighlight with the current selection mask (formats/ui.md
"Auto-highlight") and select_ent_univ take surfaces exactly as in the general selection: shading off dist_to_surf (the nearest of the iso
lines the surface is drawn with, 0x1802ddba0), shading on gl_Hit_Surfs (mastercam.dll FUN_180224220: the nearest shading triangle along
the cursor ray). The hovered surface is drawn by the same auto-highlight path as a surface in the general selection (its wireframe in the
config 5000 colour; FUN_180135ad0's two-pass dash with 4056). No PMESH_ID, notes or solids join.

**A click on a surface** (chain_single FUN_1801df0b0, l.5385-5418; GetProdEnableBits & 2 taken clear): FUN_1801e1a50(ent, (u, v) of the
pick, 0, &eptr) -> `build_edgecrv_on_surf(ent, (u, v), surf_step_chord_dev, chain_section_stop_angle * 57.2958 (degrees), 0, list)` with
edgecrv_make_spline = 1 / edgecrv_make_cons = 0 (what every caller restores, decomp/curves/ed_cad1.c l.1016-1017) **stores the edge
curve as new database entities** (main colour / level), then `get_ent(list[0], mask 0x8e)` = the first curve (a point - a degenerate
edge - fails the mask: nothing chained), `set_clr_sel_bits(0, 4, 0, 4, 0x100)` clears the surfaces' selection bit 4. The direction:
eval_surf(u, v) and curve_endpoints projected (proj_iso_pt); ep = (|pick - end| <= |pick - start|); `add_curve_to_chain(curve, ep)`:
the chain runs from the end nearer the pick (the wireframe rule); FUN_1801e8660(chain, !6002); draw_chain_direction. The pick's (u, v):
dist_to_surf's interpolation along the iso segment (shading off), project_pt_onto_surf of the ray hit (gl_Hit_Surfs, shading on).

**build_edgecrv_on_surf, untrimmed surface** (kind != 10): the nearest boundary by signed parameter differences, tested V0 (v - v0), U1
(u1 - u), V1 (v1 - v), U0 (u - u0), first of equal (no abs; unlike Curve One Edge's MaSurface::BoundaryClosestToUVPoint, no degenerate
boundary is skipped); `make_constant_spline(0, msf_nurbs_splines, 1, surf, 2 (chord height), .., surf_step_chord_dev, 1, 0, along_u,
const, {start, end})` along it (u0 -> u1 or v0 -> v1): eval_surf of order 2, unit tangent of the running derivative, calc_chord_ht
(0x1805eca80) over chord * 1.05 halves the step and retries, a point within tol of the previous one re-fits the last segment
(FUN_1800e1b90), else pt_tan_to_sp_coefs adds a Hermite segment; next step from the curvature (curv 0x1804b7990: k >= 0.001 -> chord of
sagitta `chord` (k <= 2 / chord) or 1 / k, over |d1|; k < 0.001 -> range / 10; over half the range halved while below the middle and
reaching the end); **curvature's second derivative = Suu when u runs but Suv when v runs** (disassembly 1800e3c12: base + (4 - param_10)
* 24 - the DLL's choice, ported as is); a running derivative < tol ends it. Then FUN_1800e4680(spline, n, closed (|last - first| < tol),
0, msf_nurbs_splines, 1, chord * 0.1) (stack arguments: 1800e3fa9..3fd3): put_spline_in_db, remove_nodes at chord * 0.1, with
msf_nurbs_splines convert_entity_to_nurbs; no segment -> store_point. Last FUN_18054d250(curve, msf_nurbs_splines):
convert_ent_to_line_or_arc(surf_step_chord_dev) -> a line / arc (GetProdEnableBits & 0xf002 taken clear).

**Trimmed surface** (kind 10; most surfaces of real parts): the trim-loop walk - PORTED 2026-11-17 (viewer/mcpartline.c
ec_build_edgecrv_list), oracle-exact: see "The trimmed walk (port)" below.

**Setting 6026** (user decision CS-1: it was on in the user's Mastercam session): the Chaining Options checkbox 2475 sets
chain_surface_edges_in_single_mode for the session only (CChainOptions OK FUN_1801f9680 writes the globals, no config file; decomp/
mccore_chain_feat_opts.c l.5011-5050); the config file sets it at start (FUN_1801b8270 key 0x178a) and is written by the System
Configuration dialog's Save (not ported here). This program reads the same file Mastercam 2017 does: paths.conf `user` = `my mcam2017`,
`CONFIG/mcamxm.config` then `mcamx.config` (core_bridge.c screen_colors_load -> cui_chain_config_item); both have 6026 = n, so in a fresh
start the option is off until the Chaining Options checkbox (or the file) turns it on - as in Mastercam. Start values in the image (MemDump
2026-11-17): chain_surface_edges_in_single_mode 0x1813c4c9f = 0, edgecrv_make_spline 0x1812d7988 = 1, edgecrv_make_cons 0x1813c7ff8 = 0,
**msf_nurbs_splines 0x1813fb8f2 = 0** (cfg_createNurbs 0x181305308 = 1, msf_make_spline 0x1812f3969 = 1).

**The pick's (u, v)** (user decision CS-2: Mastercam's own calculation or refuse). select_ent_univ (mastercam.dll 0x18020f4a0) hands its
surface parameters to get_event's t (l.557-558), which chain_single receives. Shading off, for each candidate surface: `dist_to_surf`
(mccore 0x1802ddba0) when `UnBlanking()` or the entity's display-list node has +0x10 clear (l.406), else **FUN_18013cbc0**: the display
list's stored surface points (DlPoint.cpp init_surf_pt_array FUN_18013ea20, 0x24-byte records with the (u, v) as floats at +0x18 / +0x1c,
screen-projected) - the nearest segment (FUN_18013b2a0, strictly nearer than the best so far) and (u, v) linear along it by t. The node's
+0x10 byte and the builder of the surface display-list node (type 8) are NOT DECODED (searched: DisplayList.cpp functions dumped in
the session, no surface point writer found). dist_to_surf, untrimmed (ported, `dist_to_surf_uv` in cui_chain.c): pass 1 the u = const lines at
surf_division's spacing (density 0: half the range) from u0, sampled along v at its step, pass 0 the v = const lines (kind 7: u step at
most 2); each sample through proj_iso_pt (the view's x / y axes); a segment that dist_to_finite_line (0x1804b6680) hits within the best
distance so far gives (u, v - (1 - t) step) / (u - (1 - t) step, v) with t from t_on_line (0x1804b48a0) - the full step even for a
shortened last step; clamped to the domain. The early outs on the bounding circle / surf_screen_box are not ported (they only reject).
Kind 10: FUN_1802de180 - PORTED 2026-11-17 (viewer/mcsrfdl.c sdl_dist_trimmed, oracle-exact: "dist_to_surf's trimmed branch (port)"
below). Shading on: gl_Hit_Surfs (FUN_180224220) hits Mastercam's own shading triangles and refines with
project_pt_onto_surf from a build_grid / fill_grid / find_neighbor start: ported since (see "The shading-on pick (port)"; the earlier
refusal is lifted).

**Cancel and undo.** cancel_chaining (0x1801dc6e0) frees the chains / hash, clears the 0x40 bits and the arrows: the made curve stays.
store_ent -> FUN_1802c4010 -> FUN_1802c7ce0 writes the entity, no undo call; the undo (DbUndo.cpp, decomp/align/core_undo.c + the
session dump) records a transaction's new entities as those after the db_end saved by UndoTransStart (FUN_1802df510 at commit);
UndoTransCommit leaves no transaction open unless an outer one is stacked (FUN_1802df620: DAT_1813cadb8 empty -> 0, DAT_1813cadc0 = 0);
run_chain_dlg / chain_single and mcmill's op commands call no UndoTransStart (grep: 0 in mcmill_all.c, 0 in mccore_chain_range.c).
So the curve made by an op's chaining is in no transaction: Ctrl+Z does not remove it. Here: no undo record (doc marked changed).
(A wireframe command that chains inside its own open transaction would carry it: not traced for each command.)

Port (2026-11-17, both apps through core_ui): `chain_srf_pick()` (cui_chain.c) = the Chaining dialog in Single mode with config 6026 on
(not Solids mode, Dynamic, a window start point, the drill point dialog); `pick_entity` (cui_sel.c) then takes surfaces (not meshes /
notes) - so the hover draws the surface highlighted through the existing auto-highlight; a click -> `chain_srf_edge` (cui_chain.c):
shading on gl_Hit_Surfs (see "The shading-on pick (port)"), shading off see "Which routine normal picking uses" below; `dist_to_surf_uv` (test step srfdts only; the cursor's view x / y, aperture sel_dist in model units); `ec_build_edgecrv`
(viewer/mcpartline.c: the untrimmed branch above, reusing remove_nodes / mcsf_spline_rec / mcsf_spline_to_nurbs / mcs_simplify_curve)
with msf_nurbs_splines = its start value 0 (the mccad commands that set it to cfg_createNurbs are not mirrored yet: formats/TODO.md), the
curve added as a new entity (no undo record, as Mastercam), then the single chain with the decoded end.
Tests: tools/tests/chain_scenes.txt (a revolved kind 0 surface, Isometric) ch_srf_hover (shaded: the surface hovered, `cd srf mask on,
hover N (surface)`; shot), ch_srf_single (Wireframe display, two clicks: the far circle edge chained as a view arc, the profile edge as an XY
line, each from the end nearer the pick; `cd srfedge` lines give the (u, v) and the made curve), ch_srf_shaded (shaded click: since the port it chains),
ch_srf_chainmode (Chain mode: no surface pick), ch_srf_cfg_off (Single with 6026 n: none), ch_srf_trimmed (kind 10: hovered, the click
chains the trimmed edge since the walk is ported).

NOT DECODED / NOT PORTED (2026-11-17, after the trimmed-surface port below): the shaded pick for surfaces whose mesh is not ported (see "The shading-on pick (port)"); eval_surf_const of kind 4 (FUN_18042c080 / FUN_18042d9b0 dumped; no kind 4 surface in the corpus to check, eval_surf
of kind 4 not ported either: sdl_build refuses it) and of a kind 6 surface over an arc (the arc's first derivative); eval_surf of a
non-NURBS base in the trim walk (mcsn_eval: the same surface, not checked bit for bit - the ECV oracle has NURBS bases only); the repaint
check's page-setup / view +0x45a9 conditions (taken clear); the msf_nurbs_splines writers in the mccad commands (ours stays 0); ESC during
make_constant_spline / get_constant_uvpts / the walk (KeyPress); msf_create_spline's 2-node line with FUN_1805502a0's plane snaps (their
state comes from other msf commands: taken off, as plc_curve); the CSplineNodeMgr capacity flush (FUN_1800e4680 at get_max_n_spline_pts -
ours grows) and the node buffer's contents left by the Mastercam users of it not ported onto it (create_spline, Roll, Curve Surface,
Loft, ...: see "The node buffer (CS-5)"); the lathe dialog's modes 1 / 2 (RunLatheChainDlg). Chaining the made curve (a NURBS, a 3D
spline, a line / arc off XY): **ported 2026-11-17 (CS-7)**, see "Curves outside the C-plane, splines and NURBS (CS-7)" below.

### Decisions CS-5 .. CS-8 (2026-11-17, the trimmed-surface port; the user's answers: formats/user_decisions.md)
- CS-5 (answered: Mastercam's default - share the buffer as Mastercam does; done, "The node buffer (CS-5)") The trim walk reads one node past its count (FUN_180274d90 on N[old count] after get_constant_uvpts stopped before adding one,
  1802743c6..1802743e2): Mastercam reads whatever its session-wide CSplineNodeMgr buffer holds there (left by earlier walks and by
  other commands using the buffer: Curve Parting Line, ...). Here: one static buffer for the walk only, zeroed at the first use - the
  oracle (a run of many walks) matches only this way (an isolated walk = a zeroed buffer). Keep, or also share it with this program's
  other users of that buffer (Curve Parting Line's port keeps its own nodes)?
- CS-6 (answered: kind 4 refused until a kind-4 part exists; part requested in formats/windows_checklist.md) eval_surf_const of kind 4 (and eval_surf of kind 4) is not ported for lack of a sample: sdl_build refuses a kind 4 surface (no
  display-list points; the hover / click on it are refused with a message). Decode it blind from the dump, or keep refusing?
- CS-7 (answered: port Mastercam's chaining of these curves; done, "Curves outside the C-plane, splines and NURBS (CS-7)") A loop inside the base's domain becomes a polyline NURBS (store_linear_nurb_from_pts, as Mastercam); the 2D chaining core here
  cannot chain a NURBS (nor a spline outside Engrave, nor a line / arc off XY): the curve is made and the chain refused with a message
  (ch_srf_trim_cap's bottom cap, ch_srf_trim_part2). Chaining such curves is the existing gap "chaining a made curve the 2D core cannot".
- CS-8 (answered: keep) FUN_1802de180's surf_division view inputs (get_wk_height, get_x / y_scr_scl) and proj_iso_pt's axes come from this program's view
  (as dist_to_surf_uv): exact against the DLL for the same inputs (dts oracle), the inputs themselves are our view's.

### Curves outside the C-plane, splines and NURBS (CS-7, 2026-11-17) — viewer/cam2d.c, core_ui/cui_chain.c
What Mastercam's chaining does with lines / arcs not parallel to the C-plane, parametric splines (S_ID 8) and NURBS curves (NB_ID 0x80):
**it takes them like any curve**, the C-plane radio being the only plane limit, and leaves the rest to the consumer.
- **The entity mask**: run_chain_dlg sets `DAT_1813c4cbc = 0x8f` (point, line, arc, spline, NURBS) for every caller
  (mccore_chain_range.c l.17719; FUN_1801fa9a0's options OK resets it to 0x8f unless chain_use_ent_mask, mccore_chain_cfg.c l.286); only
  Single mode with config 6026 adds 0x100 (surfaces) for one get_event. `init_chaining` -> FUN_1801e6440 -> `build_hash_table(hash, 1)`
  (0x1801fa080, mccore_chain_feat_opts.c l.5362-5450) hashes both ends (ent +0x68 / +0x80, 3D) of **every entity of mask 0x8f**
  (rewind_dl_db(.., 0x8f, ..)) - no plane test.
- **Joints** (FUN_1801e3de0 l.8800-8925): every hashed end within 3 chain_tol in the hash, of a type in DAT_1813c4cbc, colour / level
  masks, then the joint distance FUN_1801dddb0 (3D, or 2D with chain_ignore_depths) < chain_tol. With **chain_plane_filter** (the C-plane
  radio) the candidate must also pass FUN_1801fa6e0 and its depth be within chain_plane_tol of the tip's depth (the tip . the active view's
  z axis). 3D radio: nothing else.
- **FUN_1801fa6e0(ent, tol, &depth)** (mccore_chain_feat_opts.c l.5743-5826): the active view (GetActiveViewNo(0)), its row at +0x60
  = the z axis; depth = start . z; a point: true; else |depth - end . z| <= tol required; an arc whose view (ent +0xc8) is not the active
  one: | |z . z_arc| - 1 | <= 2e-7; a spline / NURBS: the point at (t1 + t2) / 2 (curve_t1t2, eval_curve) within tol of the depth (only
  the ends and the middle are tested). Used by the pick in Chain (FUN_1801ddfb0 l.4583: post_warning 0x1075 = 4213 "not parallel to the
  Cplane") and Partial (FUN_1801db3f0), the joints above and FUN_1801f9e90 (Window / Area / all-selected: also |depth - constr_depth| <
  chain_plane_tol). **Single mode (chain_single FUN_1801df0b0) tests no plane at all.**
- **Direction** (FUN_1801e8660 l.12442-12562, after set_chain_closed(chain_tol)): closed chains only, with !6002: each CHAIN_ENT's
  entity sampled with eval_curve from its start to its end as traversed (+0x18 = 1 swaps them): lines (and points) n = 2 over 0..1, arcs
  n = floor(|sweep| / (pi/2)) + 2 when |sweep| >= pi/2 (else 2) over 0..1, splines / NURBS n = floor(|t2 - t1|) + 3 over curve_t1t2 (ent
  +0x98 / +0xa0); the parameter is advanced by (end - start) / (n - 1) each sample; world_to_2d into the active view; add_to_area per
  step inside one entity (sum (y1 + y2)(x2 - x1)); reversed when (area < 0) != (chain_direction == 1). Every kind, every plane.
- **The pick's end** (ep_n_from_t_sel l.8131): splines / NURBS by |t - t2| <= |t - t1|, the rest t >= 0.5; t = select_ent_univ's
  parameter, measured in the view: lines t_on_line of the cursor on the proj_iso_pt-projected line (mastercam.dll decomp/mastercam_seluniv.c
  l.420-470), arcs dist_to_arc, splines dist_to_spline, NURBS dist_to_nurbs (mccore; their parameter computation NOT DECODED).
- **Storage**: a CHAIN_ENT holds the entity's eptr and its direction like any curve (add_curve_to_chain); the op stores the chain
  (save_chain / the TP records) naming the entity - the spline / NURBS records are entities like lines (formats/spline_chain.md: the
  saved records name class 0x80 entities and regenerate).
- **Consumers**: each operation decides (check_chains_3d / check_chains_3d_view for Contour: a chain not flat -> the 3D contour;
  chain_to_ctour_array refuses splines unless the caller passes splines_ok - Engrave does; the rest NOT DECODED here).

**Port** (cam2d.c; consumer = an operation's chaining, `cui_all_chaining`):
- `cam_chain_all` = the 0x8f mask: `chainable()` takes every line, arc (XY or a view's) and spline / NURBS entity (E_SPLINE), any plane,
  not blanked, not a solid's edge curve; joints 3D as before (FUN_1801dddb0). An arc whose stored XY data contradicts its ends (vw 0,
  planar 0: mcam_load's arc_fixups) is not taken (its curve is not known here).
- `cam_ent_cplane` = FUN_1801fa6e0 for the Top C-plane (this program's C-plane; depth = z): the pick in Chain / Partial (message 4213),
  the joint candidates, Window / Area (FUN_1801f9e90's plane_ok) - in place of the old Ent.planar test.
- `cam_chain_set_dir` = FUN_1801e8660 above for every chain (also Window / Vector / Area's directions; before: the area of the XY
  polyline - the same sign for any non-degenerate chain); view-arc chains now get it too.
- `ent_pick_t`: a curve outside the C-plane (a line whose ends differ in z, a tilted view arc, a spline not parallel to XY: `cam_ent_tilted`)
  is measured in the view when the UI gives the click's view rows and cursor (`cam_chain_pick_view`, set by chain_click_ent): lines by
  t_on_line on the projected line (decoded); splines by the nearest of their break points at chain_tol projected (**ours**: dist_to_spline /
  dist_to_nurbs NOT DECODED, as the XY rule for planar splines); view arcs keep their old handling (the pick side not measured - **ours**,
  dist_to_arc NOT DECODED). Curves in the C-plane keep the XY measure.
- Drawing / dump: `chain_is_3d` chains (a tilted curve) are drawn and dumped in 3D (`chain_polyline3`, splines at their break points).
- Surface edges: chain_srf_edge chains whatever build_edgecrv_on_surf made (chain_single has no plane filter): the trimmed caps'
  polyline NURBS, 3D NURBS / spline edges (ch_srf_trim_cap / _dts / _part2 / _part_dts re-blessed; ch_srf_trim_part re-blessed for the
  dump only: its vertical line edge chained before too - a line made here has planar 1, CS-12 - and is now dumped in 3D, start3 / end3).

**Consumers here** (cam_generate): **Contour** takes a chain that is not flat (check_chains_3d -> the 3D contour; gen_contour: the narrow
case FUN_18005ef90 - incremental depth, compensation off, no leads, no wall stock - is generated, splines through FUN_1800601f0; every
other 3D case is refused: connect_3d_moves NOT PORTED); a **flat spline chain in a 2D Contour** stays refused (Contour's own spline
conversion with chain_to_ctour_array splines_ok = 0: NOT DECODED); **Engrave** takes flat splines (as before), not tilted ones; **every other
operation** refuses a chain holding a line not parallel to XY ("a chain that is not flat in the tool plane ... NOT DECODED") - splines and
tilted view arcs were refused there already. **Extrude and the wireframe chain functions** (Offset / Fillet / Chamfer Chains, Roll,
Revolve, Loft, Flat Boundary, Letters, Nesting) keep the old rule (planar lines / arcs) and a pick on such a curve says "chaining a curve
outside the XY planes or a spline curve for it is NOT PORTED here" (`chain_off_consumer`): their handling of 3D / spline chains is not
decoded (CS-9). **Area** keeps the old rule (CS-10). Dynamic / Partial / the branch extension refuse a tilted line / spline pick as they
refused a view arc (they measure in XY here).

Tests (chain suite): ch_cs7_spl3d (SURFACE_FILLET: the 3D NURBS edge made in Single mode, unselected, then picked in Chain mode with the
3D radio near its stored end -> chained from that end (60r); with the C-plane radio -> warning 4213), ch_cs7_spl3d_far (picked near its
stored start: chained forward),
ch_cs7_capdir / _cw (the bottom cap's closed polyline NURBS turned by FUN_1801e8660 with 6002 = n, 6001 = 1 / 0); op suite:
rt_ctour3d_nurbs (Contour on the 3D NURBS: compensation off, depth incremental -1, leads off -> 3D contour generated and posted),
rt_ctour3d_nurbs_comp (the default parameters: refused with the connect_3d_moves message), rt_ctour2d_nurbs_cap (the flat cap NURBS in a
2D Contour: refused), rt_pocket3d_line (Pocket on tilted lines: refused). The test hook MCAMUI_EVENTS `F:step:NAME:ARG` runs one script
step between clicks.

NOT DECODED (CS-7): dist_to_arc / dist_to_spline / dist_to_nurbs's parameter (the pick's end on a tilted arc / spline: ours as above);
Contour's 2D handling of spline chains; every other operation's handling of 3D chains (projection in chains_to_pock_bounds /
chain_to_pock_bound, the 3D engines' boundaries); Extrude's and the wireframe functions' handling of 3D / spline chains; chain_area with
curves outside the C-plane; Dynamic / Partial / branch picks on tilted curves; a C-plane other than Top (the active view is Top here).

### Decisions pending (CS-9 .., 2026-11-17, the CS-7 port)
- CS-9 Extrude and the wireframe chain functions keep the old chaining rule (planar lines / arcs) and refuse a tilted curve / spline pick
  with a message, while Mastercam's dialog chains it and the function decides. Decode each function's handling (one by one), or keep?
- CS-10 Area mode keeps the old rule (curves outside the C-plane left out of chain_area's chains) - Mastercam hashes them too; its
  containment of such chains is not decoded. Decode chain_area's 3D path, or keep?
- CS-11 The pick's end on a tilted arc / spline / NURBS comes from the nearest projected chord (ours; Mastercam: dist_to_arc /
  dist_to_spline / dist_to_nurbs's t). Decode those three (mccore), or keep ours? (View-arc chains still skip the pick side.)
- CS-12 Lines drawn here keep Ent.planar = 1 even when their ends differ in z (mcam_new_ent); the CS-7 code tests the z values instead
  (cam_ent_tilted), but the old rule of Extrude / the wireframe functions still chains such drawn lines. Set planar from the z values at
  creation (as mcam_load does), or keep?
- CS-13 Pocket (and every non-Contour op) on a chain of tilted lines was generated before (projected to XY by the polyline) and is now
  refused (Mastercam's projection there is not decoded): rt_pocket3d_line. Accept the refusal, or decode chains_to_pock_bounds'
  projection next?

### The node buffer (CS-5, 2026-11-17) — viewer/mcpartline.c csn_nodes1 / csn_reset
CSplineNodeMgr's spline nodes 1 (DAT_1813c8008, p_3d records) is one buffer for the session: `CSplineNodeMgr::AllocateMem` (mccore
0x18027c3e0, decomp/partline/core_nodemgr.c: DeallocateMem, calloc(96000) = 4000 zeroed records, DAT_1813c8000 = 4000) runs at `dbinit`
(mccore 0x1802c4770, a file open: FUN_1803509a0 l.256) and `init_db` (mastercam.dll 0x18033ae30, File > New, decomp/mastercam_filenew.c
l.157); `AllocateMemNPts` (0x18027c4e0) grows it to the next 1000 above n + 5 keeping the records. Port: `csn_nodes1(n)` / `csn_reset()`
(scene.c load_file / new_file call the reset); the trim walk (ec_build_edgecrv_list) and Curve Parting Line (plp_alloc's P) use it, so
the walk's one read past its count sees what the last walk or parting line left, as in Mastercam. Not mirrored (NOT PORTED): the other
users of nodes 1 in Mastercam (create_spline / the sketcher, Roll, Curve Surface, Loft, Trim, ...) keep their own arrays here. ECV oracle
unchanged (352 / 352).

### mcs_simplify_curve on the edge curves (oracle, 2026-11-17) — tools/tests/ecv_simp_oracle.sh
FUN_18054d250 (mccore 0x18054d250) runs mccadutil's `convert_ent_to_line_or_arc(curve, surf_step_chord_dev)` (imported by mccore,
decomp/proj/core_exp.txt l.2324) on each class 8 / 0x80 edge curve. Oracle: ecv_test (ECV_SIMP) writes every edge curve the walks make as a
SIMP case; tools/peloader --splfit runs the DLL's convert_ent_to_line_or_arc on them (tools/peloader/mccore_spl_simp.c), the port
mcs_simplify_curve (tools/spline_simp_port). **2017 Advanced geometry folder: 318 / 318 identical (the DLL converts 152: 58 lines, 94
arcs); Design + Mill Advanced Training (inch, metric) + Handbook 2 + 3: 1658 cases, 1655 identical**, the 3 others (one curve, three picks)
equal in every number and differ only in the harness's "a view was added" flag (g_nv > 8: a half-fit made a temporary view while the
final view is 1; PlanesTemporaryStart is a stub there). **The bottom cap**: the app's cylinder (r 12, height 25) saved as a part, its two
picks (ECV_UV, the app's tol 5e-5 / chord 0.02): the walks are identical to the DLL's (4 / 4) and the DLL's convert_ent_to_line_or_arc
also refuses the bottom cap's 85-knot polyline (the closed-curve split at knot 43: the two half fits' centres (2.9936, 4.0561) and
(2.9944, 4.0138) are 0.042 apart, more than tol 0.02) while the top cap's 84-knot one splits at knot 42 into matching halves -> an arc.
**So the NURBS stays a NURBS in Mastercam too: nothing to fix** (the difference comes from where the walk starts, i.e. the pick).

### Which routine normal picking uses (CS-4, 2026-11-17) — new dumps decomp/chain_srfedge_dl2.c (mastercam.dll)
- **Display lists are on**: `dl_is_on` (mastercam.dll 0x180869ee0) = 1 in the image; its only writer FUN_1801fdd60 pushes / restores it
  around a temporary drawing (UtPushValue). `dl_ready` (0x18099ca48) is set by the display-list rebuild FUN_18032d870 / FUN_1801314b0.
- **The surface entries**: `store_ent_dl` (0x180340430; entity visible, not blanked, dl_is_on && dl_ready) takes a 0xa0-byte node
  (+0x00 = 1 when FUN_180132600 lists the eptr, +0x04 type from FUN_18033ac90 (8 for a surface), +0x80 float = get_y_scr_scl at build,
  +0x88 the eptr, +0x90 / +0x98 the list links), then FUN_180138b70 (= store_ent_pl): class 0x100 -> **FUN_1801391f0**: +0x08 = 0,
  **+0x10 = 0**, +0x14 = 0, +0x18 = the point buffer (FUN_1801362a0); only for a SURF_ID with surf +6 bit 0x40: the view scales are
  set to a power of two (s = 2^(floor(log2(y_scr_scl)) + 2), put_x / put_y_scr_scl(s), put_wk_height(wk * 4 * old_y / s)), then
  FUN_18013a5e0 (kind 10) or **FUN_18013a130** (other kinds) writes the points, the scales are restored, and **+0x10 = 1 when the
  generation returned nonzero**. So +0x10 = "the surface's DlPoint array was built".
- **The points** (FUN_18013a130, untrimmed): like the wireframe draw FUN_180238320 (render_cfg 4 outline rule, surf_division at the
  power-of-two scale via FUN_180139a60, density 0: half ranges), but: on a NURBS / parametric direction the sampling also stops at every
  knot / patch boundary (FUN_18013a040: nurb_interval (kind 0) / floor (kind 1), the sample moved back onto the knot when it overshot by
  > 1e-8) unless FUN_1801321c0 says that direction is linear (kind 0 order 2 with 2 control points, kind 1 matching flags, kind 4 / 6
  cases); each sample through **FUN_180139360**: eval_surf / eval_surf_const, FUN_18013b210 (doubles to floats, |x| outside 1e-16 ..
  1e16 -> 0), a 0x24-byte record {x, y, z float, normal x y z float (only with surf_back_draw), **u, v float**, flag byte +0x20 (1 =
  continue, 0 = first point of an iso line, 2 = an iso line's last point)}; from the third point of a line the previous point is dropped
  (overwritten) when FUN_180137d20 (the angle at it, float, against DAT_180869788) or FUN_1801323a0 "dist_ok" (the chord sag of the
  three points times the view scale (view +0x770) < 0.99 px) says it is colinear enough. NOT YET PORTED (the remaining callees:
  FUN_18013b5c0 / FUN_18013b590 float vector helpers, FUN_180136030 buffer growth, FUN_180139a60, DAT_180869788's value, view +0x770).
- **The pick** (select_ent_univ l.403-411), shading off: `UnBlanking()` false (cVar27 = 1) and the node's +0x10 = 1 ->
  **FUN_18013cbc0** (decomp/surfpick/mc_pick.c l.3-84): the node's records (FUN_18013edd0 / FUN_18013ea20 / FUN_18013ef90: for a
  type 8 node no subsampling), each projected to the screen (FUN_18013b4a0) with its clip outcode (FUN_18013b530); for each segment
  whose two outcodes do not share a bit, FUN_18013b2a0 (point-segment distance, t clamped to 0 .. 1) strictly below the best so far ->
  (u, v) = uv[i-1] + (uv[i] - uv[i-1]) t from the stored floats. dist_to_surf is only for surfaces without a built node (+0x10 = 0:
  generation failed / not a valid surface) or in UnBlanking mode. **So the normal wireframe-display pick is FUN_18013cbc0 on the
  display-list points**; shading on, gl_Hit_Surfs (its shading triangles: FUN_180224100's cache, built elsewhere - NOT DECODED).
- Port state (CS-4, superseded by "The display-list points (port)" below): dist_to_surf_uv (the no-display-list branch) kept, reachable
  by the test step `srfdts` and for a surface whose node is not built.

### The display-list points (port, 2026-11-17) — viewer/mcsrfdl.c, viewer/core_ui/cui_srfdl.c; new dumps decomp/chain_srfedge_dl3.{c,asm}, chain_srfedge_dl3_mem.txt
**Generation** (FUN_1801391f0 = store_ent_pl for class 0x100, only with ent +0x6e bit 0x40): node +0x08 = 0, +0x10 built = 0, +0x14 = 0,
+0x18 = the point buffer's top (FUN_1801362a0); the view's scales set to s = 2^(floor(log2((uint)get_y_scr_scl)) + 2) (y < 2: 4), both
x and y (put_x / put_y_scr_scl), put_wk_height(wk * 4 * y / s); FUN_18013a5e0 (kind 10) / FUN_18013a130 (others); scales restored;
built = the generator's result (nonzero). get_y_scr_scl = view +0x770 = client height in pixels / +0x760 (set_parms FUN_1801dd950), so
the points depend only on the level floor(log2 y), the client height, the display mode and the surface.
- **surf_div_dl FUN_180139a60** (not mccore's surf_division): defaults 0.1 / (0.1 / zoomscl) (zoomscl 0x180869ed8 = **20.0**, no
  writer); the counts n1 / n2 by get_wk_height against ent +0xd8 (> 20 size: 1 / 3, > 12: 2 / 6, > 4: 4 / 12, else 8 / 36 or 12 / 36 when
  4 wk <= size); find_base_surf; a direction with curvature (+0xe0 u / +0xe8 v of the base) <= tol: range / (n1 x zoomscl in the sampled
  direction, else 1) x 0.5 / 2; else sqrt(3.2 / (scale x curvature)) capped at half the range, x 4 for the spaced lines (n1) or at least
  range / (n2 x zoomscl) for the samples, rounded to whole divisions; kind 1 > 20 patches x 0.25, kind 4, kind 7 (pass 0: v 0.5); a density
  (ent +0x69) > 1 divides **only the spacing** (pass 1 the u spacing, pass 0 the v spacing).
- **FUN_18013a130** (untrimmed): full = not (render_cfg 4 and ent +0x6e bits 1 / 2 clear); pass 1 the u = const lines from u0 every
  spacing (full: at most half the range, density 0 half the range; shaded outline: the whole range = the boundary), each sampled along v
  from v0 at the step, the last at v1; on a non-linear direction (FUN_1801321c0) every knot / patch boundary crossed by more than 1e-8 is
  sampled (FUN_18013a040: nurb_interval + 1 / floor + 1; the knot value K[nu + idx] / idx; **the third argument, the running parameter,
  is lost by Ghidra: asm 18013a284 / 18013a304 / 18013a35b**); then pass 0 likewise (kind 7: the u step at most 2). init_surf_const(h, 1)
  / (h, 0) around the passes: the iso points by **eval_surf_const** (kind 0: FUN_1805b2280 / FUN_1805b1aa0, the fixed parameter's basis
  cached, the 1e-8 basis cut; kind 1: FUN_1805ec5c0 / FUN_1805ebcd0, the patch (int)(t + 1e-8), powers collapsed; other kinds:
  kinds 2 / 6 / 7 (2026-11-17): only init_surf_const(h, 1) sets them up (dir 0: eval_surf) - FUN_18047ae20 / FUN_18047ad80 (kind 2: the
  profile point split into its axis foot F, radius R and d x R; cos v R + F + sin v (d x R)), FUN_180413460 / FUN_180413400 (kind 6:
  v dir + C), FUN_18041d090 / FUN_18041cdb0 (kind 7: FUN_1803fca60 + FUN_180114560, stack arguments asm 18041ce17..18041ce9a); decomp/
  chain_srfedge_const.{c,asm}; kind 4 refused (NOT PORTED). srfdl oracle with those kinds (srfdl_test.c writes the referenced curves,
  mastercam_sdl.c serves them): default folder 1112 / 1112 identical, the kind 2 / 6 / 7 parts 5408 cases 0 differ (110 within float
  rounding: trimmed over kind 0, as before)).
- **FUN_180139360** (one record, 0x24 bytes: float xyz, normal (surf_back_draw 0x1809a0b7a = 0: none), float u v, flag +0x20 = 1
  continue / 0 a polyline's first point; FUN_18013b210: |x| <= 1e-16 or >= 1e16 -> 0): from the third point of a polyline, the corner
  test FUN_180137d20 at the previous point (float vectors by FUN_18013b5c0 = a - b **with z = b - a**, lengths FUN_18013b590; cos
  > DAT_180869788 = **-0.86**, or the length-ratio rule (0.14 / max ratio - 1)) marks it 2 (kept); else (not the line's last point)
  FUN_1801323a0 "dist_ok" (the chord sag of the three points x view +0x770 = s, float arithmetic, < 0.99) overwrites it with the new
  point. The caller marks each polyline's last record 2.
- **FUN_18013a5e0** (kind 10): the loops first (**FUN_1801395e0**: per loop the closing point, then each stored uv point; a piece along
  u (|dv| < 1e-4) or v (|du| < 1e-4) subdivided at surf_div_dl's step through the base (eval_surf); the node's +0x0c = the loop records,
  +0x80 = (float) get_y_scr_scl); shaded outline: nothing more. Then pass 1: x from min(u0 of the trimmed, of the base) + the base's u
  spacing (density 0 / spacing over half the trimmed range: one line at the middle), each line u = x: hit_polygon (mccore 0x18046be80,
  from v0 - 0.01 to v1 + 0.01), at most 0x100, clip_uv_to_minmax, an odd count drops the first crossing when in_polygon of the middle of
  the first two is **0** (on a loop) else the last when that of the last two is 0 (before sorting), masort_2d_real_index (0x18047b060:
  the DLL's quicksort with its pivot sequence) by v; the walk from v0 at the step with the knot snap, a crossing toggles inside (entering
  starts a polyline, leaving marks 2), samples inside only. Pass 0 likewise along u. (asm 18013a7f0..18013b136 for the arguments.)
**Pick** (FUN_18013cbc0, select_ent_univ l.403-411, shading off with the node built; else dist_to_surf): the bounding circle (ent +0xc0
centre projected, radius +0xd8: reject when farther), surf_screen_box tested against the **centre's** projection (asm 18013cc69..: always
inside), then per polyline (FUN_18013ea20(0, 1, 1, 0, 1): up to the record before a flag 0, at most 0x1000 points, the next chunk starts
at the last point) the records projected (view +0x720 / +0x738 = SetGrView's rows), outcodes against view +0x788..+0x7a0 (FUN_1801dde20:
scrn_to_uu of the client rectangle = the visible window), each segment whose outcodes share no bit: FUN_18013b2a0 strictly below the best
-> (u, v) linear along it from the stored floats. Here cui_sel.c pick_entity (hover and click, shading off) and chain_srf_edge use it
(srfdl_pick); not built -> dist_to_surf_uv (untrimmed) / srfdl_dist_trimmed (kind 10: FUN_1802de180).
**When the points are (re)built** (cui_srfdl.c): store_ent_dl at an entity's store / FUN_18032d870 at a file open (init_db ->
FUN_1801da060 -> FUN_1801da220: before fit()) -> here the upload hook (scene.c upload(), before a load's fit); the repaint FUN_1801dadd0
(asm 1801db17e..1801db1d5): with "Surface outline refine" (registry GUI\Graphics; absent in the user's hive -> DAT_180876dc5 .data 1) and a
level different from DAT_1809a0148 (.data 0): FUN_180130520(1) clears the built nodes whose extents (FUN_18013e1e0) meet 1.5 x the view
box, and if any FUN_180130af0 rebuilds every unbuilt node and keeps the level (not for render_cfg 3 / page setup / the view's +0x45a9 flag,
taken clear) -> here the draw hook and before every pick; DoShadeWire / DoShadeShadedOutline (switching wireframe <-> shaded = render_cfg
4) clear and rebuild every node. The shaded modes here (solid_mode 1 / 2) are render_cfg 4.
**Oracle**: tools/peloader `mastercam.dll --sdl` (mastercam_sdl.c: mastercam.dll + mccore.dll mapped, no DllMain, the view / buffer /
render_cfg globals set, get_ent_from_eptr served) runs FUN_1801391f0 and FUN_18013cbc0 on stored surfaces; tools/tests/srfdl_oracle.sh
(srfdl_test.c: four view heights against the surface's size x wireframe / shaded outline, picks near every 7th record): the 2017 Advanced
geometry folder 1080 cases all identical; Design / Mill Advanced Training / Handbook 2 1160 cases: 1128 identical, 32 positions within
float rounding (trimmed surfaces over kind 0: loop points through eval_surf at ~1e-16 - mcsn_eval vs FUN_1805b08d0), 0 differ.
Scenes: ch_srf_dlpick (real wireframe clicks: the far circle and the profile edge), ch_srf_trim_wire (the cylinder, wireframe: node
counts `cd srfdl`, a click on the side surface), ch_srf_single (srfdts: the dist_to_surf branch).

### build_edgecrv_on_surf against mccore (oracle, 2026-11-17)
- **Oracle**: tools/peloader `mccore.dll --csurf` has the command `ECV k u v chord angle` (mccore_csurf.c: build_edgecrv_on_surf on
  REC k with edgecrv_make_spline 1 / edgecrv_make_cons 0 / msf_nurbs_splines 0, FUN_18054d250 replaced by a counter, the stored
  entities printed; CSURF_ECV_TRACE=1 prints check_edge / get_constant_uvpts / FUN_180274d90 / FUN_180275800 through trampolines);
  tools/tests/ecv_oracle.sh + ecv_test.c + ecv_cmp.py (the port's curves before the line / arc test; trimmed: every stored entity in
  order, untrimmed: the last one = remove_nodes' replacement). **2017 Advanced geometry folder (3 surfaces per part, angle 30): 352 / 352
  (trimmed 112 / 112, untrimmed 240 / 240); Design + Mill Advanced Training + Handbook 2 + 3 (4 per part): 928 / 928 (trimmed 672,
  untrimmed 256)**, every number within 1e-9.

### The trimmed walk (port, 2026-11-17) — viewer/mcpartline.c ec_build_edgecrv_list; dumps decomp/chain_srfedge.c, chain_srfedge_trim.c, chain_srfedge_ecv.asm
Ported from the disassembly of 0x180273d31..0x180274d19 (Ghidra merges variables there: local_54f4 is the loop index in the search and
the walk direction after, iVar33 the closing index and the boundary direction, local_5428 the list and then the point pointer) checked
against the decompile. Registers: [rbp-0x80] the node count, [rbp-0x78] the point, [rbp-0x48] the previous point, [rbp+0x80] the one
before (only for the 2D colinear test), [rbp-0x18] the start, r12w the previous point's check_edge bits, [rbp-0x38] the current ones
(0xffffffff at the start: a first point skipped as a duplicate counts as on every boundary), [rbp-0x34] the start's.
- **The search** (180273d55..18027401a): the base by +0x100 (get_ent_from_eptr fails: nothing); a running point pointer that moves count
  + 2 per loop (each loop stores count + 2 points, the last a copy of the first); piece j = 1 .. count + 1 of a loop runs from point j - 1
  to j (the last back to the loop's first point); dist_to_finite_line within the best so far (start 1e38); t_on_line <= 1e-4 / >= 0.9999
  takes that end and its own distance (the point is written even when that distance then loses), else the point at t with the
  perpendicular distance; a best below tol ends the search. Then eval_surf(base, 0) and check_edge (0x18047b620: within 0.01 in u (u0
  first, else u1), then in v (v0, else v1) of the base's boundary with the boundary point (eval_surf_extrap) within surf_step_chord_dev:
  snapped; bits 8 / 2 / 1 / 4) give the start node.
- **The walk** (LAB_180274150): backwards (direction -1, from the piece's first point down to point 0), the nodes reversed
  (reverse_real_array), then forwards (+1, from the piece's end up to the closing index count + 1 = point 0). Per point: within 1e-10 (uv)
  of the previous one: skipped (the bits carried); else eval_surf + check_edge; bits shared with the previous point (int16 > 0): the run
  along that boundary (1 / 4: v constant, u runs; else u constant) by **get_constant_uvpts** (0x180274fa0, asm 180274fa0..1802757fb: from
  the previous to this parameter, reversed with the tangent negated; eval_surf order 2; the first point appended unless within tol of the
  last node; calc_chord_ht over chord x 1.05 halves the step; within tol of the previous point or check_colinear replaces the last node,
  else FUN_180274d90 then appended; the step from curv(D[2 - along], D[4 - along]) = Suv when v runs, as make_constant_spline; k < 0.001:
  range / 10; a derivative below tol ends at the far end), then FUN_180274d90 on N[old count] (**read even when nothing was added: a stale
  node** - CS-5) ends the pass when false; stopped inside (an angle break): FUN_180275800(1, ..) stores the curve so far and the run
  restarts from the stop parameter with the last node. Not shared: within tol of the last node skipped; both check_colinear (3D on the
  nodes, then 2D on the uv points) replace the last node; else the piece bisected in uv while the midpoint is more than 1e-8 off the point
  and its surface point more than 0.8 x chord off the 3D chord's middle; each accepted point FUN_180274d90 (the section stop angle; false
  ends the pass) then appended (4000 nodes: FUN_180275800(0, ..) and a restart). The end: FUN_180275800(start on a boundary, n, nodes).
- **FUN_180275800**: n < 2 nothing; p1 0 and n != 2: store_linear_nurb_from_pts (make_linear_nurb_from_pts 0x180262400: order 2, knots
  0, 0, 1 .. n - 1, n - 1, the nodes as control points, the closed bit at tol) + FUN_18054d250(ent, 0) (the line / arc test); else
  msf_create_spline (plc_curve, chord mode = the static surf_use_chord_dev 1) and with msf_nurbs_splines FUN_18054d250(ent, 1). So a pick
  on a loop inside the base's domain makes a **polyline NURBS through the loop points** (then perhaps a line / arc), a loop on the base's
  boundary a fitted spline.
- **Several curves**: an angle break inside an iso run (and a 4000-node flush) stores a curve first; FUN_1801e1a50 chains list[0]; here
  all are stored (main colour / level, no undo), list[0] chained.
- The angle: FUN_1801e1a50 passes chain_section_stop_angle x 57.29577951308233 (degrees; chain_section_stop_angle = config 6011 x
  0.01745329251994329, mccore_chain_cfg.c l.44); build_edgecrv_on_surf multiplies by 0.01745329251994329 (the "on" flag 6017 is not read).

### dist_to_surf's trimmed branch (port, 2026-11-17) — viewer/mcsrfdl.c sdl_dist_trimmed; dumps decomp/chain_srfedge_trim.c, chain_srfedge_de180.asm
FUN_1802de180 (dist_to_surf 0x1802ddba0 calls it for kind 10 after its early outs) from its disassembly (Ghidra's aiStack_44f0[q] /
[q + 1] are idx[q - 2] / idx[q - 1]). surf_division (0x1806f9530) on the trimmed surface; pass 1 the u = x lines from min(u0 trimmed,
u0 base) + the spacing while x < max(u1s) (density 0 or a spacing over 0.6 x the trimmed range: one line at the middle); each line:
hit_polygon from (x, v0 - 0.1) to (x, v1 + 0.1) (at most 0x100, clip_uv_to_minmax, masort_2d_real_index by v; no odd-count fix, unlike
the display list), lines outside [u0, u1] skipped; y from v0 at the step: crossings at or below y evaluated and appended, and when inside
the polyline so far goes to **dist_to_polyline** (0x1802dd3f0: the segment index + t_on_line clamped; strictly nearer only): a hit
gives uv = (x, between y - (n - 1 - floor t) step and that + step, the ends replaced by the crossings idx[q - 2] (t < 1, q > 1) /
idx[q - 1] (t > n - 2)); the inside flag toggles at every crossing; inside, (x, y) appended, at 0x200 points tested (the same
interpolation without the crossing ends) and restarted from the last point. Pass 0 likewise (base kind 7: the u step at most 2).
init_surf_const(base, pass) / eval_surf_const (kinds 0 / 1 / 2 / 6 / 7; else eval_surf). Found: uv clamped to the base's domain.
**Oracle** tools/tests/dts_oracle.sh (peloader mccore.dll --csurf `DTS k px py best wk xs ys axes`: FUN_1802de180 itself, get_wk_height /
get_x / y_scr_scl / get_x / y_iso_axis answered from the line; dts_test.c: trimmed surfaces over NURBS bases, top / front / iso views, two
heights, picks near loop points and the middle): **Advanced geometry folder 1782 / 1782 identical (852 found); Design + Mill Advanced
Training + Handbook 2 + 3: 5292 / 5292 (3102 found)**. Used by cui_sel.c / cui_chain.c (srfdl_dist_trimmed in cui_srfdl.c) when a trimmed
surface's display-list node is not built, and by the test step srfdts.

### The shading-on pick (gl_Hit_Surfs) — decode 2026-11-17 (decomp/chain_srfedge_dl3.c; ported below)
- **The triangle cache's builder** (not NOT DECODED any more): FUN_180229d90 (the shaded draw) / FUN_180137640 -> FUN_180136460 (a node
  in the list FUN_1801b2b50 = DAT_18099e1c8, linked +0xb0, +0x00 the eptr) -> FUN_180136cb0 -> FUN_180136510 -> FUN_180136f70 ->
  **make_surf_polygons(1, 1, ent, gl_tolerance, 0, DAT_18087b854, &tri, &xyz, &uv, ..)** (asm 18013661b..18013664c: param_7 = 1 from
  FUN_180136cb0; DAT_18087b854 = FUN_18046a2b0() = registry GUI\Surface Rendering "Render surfaces during tessellation" == 1: absent in the
  user's hive -> 0). These are FUN_18054be80's arguments with the step gl_tolerance (config 7004): viewer/mcpartline_mesh.c plm_mesh
  (oracle-exact for kind 0 and kind 10 over kind 0). A surface with +0x6e bit 4 has its triangles' first two indices swapped. The
  vertices become floats (the GAF GrFace when UseGAFSolids, else the node's +0x88 array; +0x80 the triangle count).
- **gl_Hit_Surfs** (FUN_180224220): the cursor ray (view_to_world of (x, y, 1) and (x, y, 0)); per listed surface (mask, level, the
  bounding circle and surf_screen_box against the **cursor**, the node not of type 2) each triangle: FUN_180221770 (its view-projected
  box contains the cursor FUN_180226780, inside the triangle FUN_1802267b0, lp_ints of the ray with its plane, world_to_view; the hit
  nearest the viewer (largest view z) kept with its eptr). Then on the hit surface: build_grid(1, surf, 0), up to 6 tries of
  project_pt_onto_surf(surf, -1, 0, hit, uv0, uv, S) accepted when |hit - S|^2 <= 400 gl_tolerance^2 (the first start from fill_grid /
  find_neighbor, then the 0.25 / 0.75 corners), free_grid. select_ent_univ (l.818-838) takes it unless a wireframe hit is nearer.
- Port: see "The shading-on pick (port)" below.

### The shading-on pick (port, 2026-11-17) — viewer/mcglhit.c, mccsurf.c cs_glhit_uv, core_ui/cui_srfdl.c srfhit_pick; new dump decomp/chain_srfedge_glhit2.asm
Disassembly (objdump of mastercam.dll) of FUN_180221770, FUN_180226780 / FUN_1802267b0, FUN_180130980, view_to_world, world_to_view, gl_Hit_Surfs'
tail (180224b6f..180224de0), FUN_180136510's index store; the capture block from decomp/chain_srfedge_glhit.asm (RDX / R8 / R9 / [RSP+0x20..
0xd8] = slots 0..0x1a: 0 / 1 x min / max, 2 / 8 / 10 the three vertices' view x y, 3 / 9 / 11 the vertices (doubles of the node's floats),
4 / 7 the view rows x / y, 5 / 6 y min / max, 12 the cursor, 13 / 14 / 15 V1 - V0, V2 - V0, the normal, 16 the ray (view_to_world of (x, y,
1) then (x, y, 0): an l_3d), 17 / 18 / 19 lp_ints' point / t / ok, 20 the view number, 21 the hit in view coordinates, 22 found, 23 the best
view z (start -1e38, .rdata 0x1805c1078), 24 the hit, 25 its eptr, 26 the ent).
- **Triangles** (glh_mesh): plm_mesh(f, s, gl_tolerance) = make_surf_polygons(1, 1, ent, gl_tolerance, 0, 0); ent +0x6e bit 4 swaps each
  triangle's first two indices (FUN_180136f70); the vertices stored as floats (FUN_180136510 cvtpd2ps; the non-GAF node stores the indices x 3
  as ints (asm 1801368d0..1801368e2), the GAF GrFace the same floats). Kinds: 0 and 10 on a kind 0 base (plm_mesh); others have no node here.
- **Per surface** (glh_candidate, asm 180224603..18022470a): the centre with the Matrix33 rows (r0y cy + r0x cx) + r0z cz, skipped when (cur y
  - y)^2 + (cur x - x)^2 > r^2 (ent +0xd8); surf_screen_box against the cursor (skipped when box x min > x, y min > y, x > x max, y > y max).
  dl_is_on's eptr_to_dl check: a node not hidden (+0 != 1 with DAT_1809acbb8 = 0: it is set only around the command at 0x180472200) and not
  type 2 - here every displayed surface (cui_srfdl.c node_ok).
- **FUN_180221770** (glh_surface / tri_test): view x / y of each vertex = (v.y r.y + v.x r.x) + v.z r.z; the box min / max (strict updates,
  else-if); FUN_180226780: x >= x min, x max >= x, y >= y min, y max >= y; FUN_1802267b0(cursor, P0, P1, P2): d1 = (P1x - P0x)(cy - P0y) -
  (cx - P0x)(P1y - P0y) >= -1e-8, d2 (P1 -> P2) > -1e-8, d3 (P2 -> P0) > -1e-8, all three the same truth value (.rdata 0x1805f4218 = -1e-8);
  N = (V1 - V0) x (V2 - V0) as N.x = E.z D.y - E.y D.z, N.y = E.x D.z - D.x E.z, N.z = D.x E.y - E.x D.y, normalised (zero stays zero);
  lp_ints(N, V0, ray) (mccore tol); world_to_view z = (r20 x + r21 y) + r22 z; taken when nothing was found yet or z > best (NaN never);
  found = 1 on every lp_ints success.
- **The (u, v)** (cs_glhit_uv, asm 180224b7a..180224d88): build_grid(1, surf, 0) - its points by **eval_surf_const** after
  init_surf_const(base, 0) (the v basis kept, first computed at the base's own (u0, v0)): mccsurf.c's build_grid1 now does this (it used
  eval_surf; the Curve Surface oracle stays 2575 / 2575); k = 0..5: k > 0 project_pt_onto_surf(surf, -1, 0, hit, uv0, uv, S) (FUN_1805ed4a0,
  then param_3 = 0: both in-range flags, kind 10 in_polygon != -1), accepted unless (dy^2 + dx^2) + dz^2 > (gl_tolerance x 400) x
  gl_tolerance (.rdata 0x1805f41f8); k = 0 fill_grid + find_neighbor(.., -1, 0, ..) (the cell edges' foot points, FUN_1804b47c0 with dy^2 +
  dx^2 + dz^2, strictly nearer; none -> k = 6, no try); after a failed try with k - 1 <= 3: uv0 = ((1 - a) u1 + u0 a, (1 - b) v1 + v0 b), a =
  0.75 for k = 1, 2, b = 0.75 for k = 1, 3, else 0.25 (.rdata 0x1805ced30 / 0x1805ced18); free_grid. The uv returned = the last one
  FUN_1805ed4a0 wrote (it writes on its success path only); gl_Hit_Surfs returns found whatever the projections did.
- **select_ent_univ** (l.818-838): the hit taken unless the entity's sel bits +4 hold 0x1000; eval_surf(surf, 0, u, v), and a wireframe
  candidate already found wins when its view z >= that point's. Here cui_sel.c pick_surf_shaded (hover and click, the general selection and
  chaining's Single + 6026 mask) and cui_chain.c chain_srf_edge (the click's (u, v)).
- **Oracle**: tools/peloader `mastercam.dll --sdl` commands M (IsMetric, tol, gl_tolerance), T (FUN_180136f70 itself + FUN_180136510's store
  into a node of the list DAT_18099e1c8), H (**the DLL's FUN_180224220** with view number 2 = the case's rows, dl_is_on 0, UseGAFSolids 0,
  ent_matches_mask answered 1, project_pt_onto_surf wrapped to record the hit); tools/tests/glhit_oracle.sh + glhit_test.c + glhit_cmp.py:
  the 2017 Advanced geometry folder, up to 24 surfaces per part as nodes together, views top / front / isometric / turned, 60 cursors per view
  (triangle centroids, points inside triangles and on vertices, points over the screen extents): **6480 picks (5145 hits): 6480 identical**
  (the surface, the hit point, the uv-written flag and the (u, v) bit for bit), 0 triangle-count differences.
- **CS-20** (user decision): surfaces whose make_surf_polygons is not ported (kinds 1 / 2 / 4 / 6 / 7 / 8, kind 10 on another base) and
  polygonal meshes (PickShadedPolygonalMeshFromCursor FUN_180206b10 -> MCLoadSTLMesh / MCLoadIFFMesh / MCLoadMCOMesh::Intersect, mccore
  0x180b7ba60 / 0x180b7b2d0 / 0x180b7b760 with their virtual set<float> variants and the GAF MtVector3d ray: NOT DECODED, not within this
  task's reach) are **not picked with shading on** - hover, general selection and chaining. This program's own shading triangles only
  decide whether the cursor is over one of them (cui_sel.c noport_under): one nearer the viewer than the port's hit hides that hit; a click
  that took nothing then says "Picking this surface type (kind N) / a mesh in shaded display is not decoded yet ...; switch to wireframe to
  pick it" (pick_noport_msg, from the Qt click handler).
- **CS-21** (user decision: do what Mastercam does): select_ent_univ's (u, v) buffer (param_6) is its caller's. The hover DrawAutoHighlight
  (0x18031f290) -> FUN_18020d280 (find_an_entity) -> select_ent_from_sel_pt -> select_ent_univ passes the global DAT_1809a0c28 (2 doubles,
  .bss: 0 at start); the click handler FUN_1804dff70 (decomp/draft/ev_ac.c l.50-52) takes GetAutoHighlightEntity's entity and that buffer
  before any find_an_entity (whose own buffer is FUN_1804e3b90's uninitialised stack local_2ab0). So when no projection writes a (u, v), the
  surface is evaluated at what DAT_1809a0c28 held: the last (u, v) written, or - when select_ent_univ found a wireframe / other candidate
  earlier in the same call (l.540-716 write its parameter into the buffer) - that candidate's parameter. Ported (cui_sel.c ah_t / ah_known):
  the buffer kept across picks (start 0, 0), updated by every written shaded (u, v); a wireframe candidate's parameter is not computed by
  this program's wireframe pick: then NOT DECODED, the surface hit not taken (a chaining click says so).
Scenes (chain_scenes.txt, real clicks, shading on): ch_srf_shaded (the far circle), ch_srf_shaded_single (the far circle, then the profile
edge: the same arc / line and directions as the wireframe ch_srf_single), ch_srf_shaded_top (Top view), ch_srf_shaded_trim and ch_srf_trimmed
(the cylinder, kind 10: the (u, v) is found and, since the merge with the trim-walk port (int-s1), the cap's circle is made and chained
as with shading off, ch_srf_trim_wire).

### msf_nurbs_splines writers (CS-4: follow Mastercam at integration; mccad.dll, decomp/mccad_sketch.c, decomp/curves/*.c)
| command (mccad) | function | write | net effect after the command |
|---|---|---|---|
| Curve Dynamic (sketcher_curve_4) | FUN_180282390 (mccad_sketch.c l.27985; curves/ed_cad1.c l.369) | = cfg_createNurbs | left = cfg_createNurbs |
| Curve Flowline (sketcher_curve_3) | FUN_180287870 (curves/cad2.c l.18), from FUN_180287a40 | = cfg_createNurbs | left |
| Curve Slice (sketcher_curve_5) | FUN_18028d0d0 (l.29071) | = cfg_createNurbs | left |
| Curve Constant Parameter (sketcher_curve_2) | FUN_1802980f0 (l.29650-29670; ed_cad2.c l.402 / 421) | saved (cVar4), = cfg_createNurbs, restored | unchanged |
| Curve One Edge (sketcher_curve_1) | FUN_1802a2940 (l.30641, l.30675; ed_cad1.c l.952 / 986) | = cfg_createNurbs | left |
| Curve at Intersection (sketcher_curve_8) | FUN_1802b8960 (l.31260-31263): FUN_1802b9990 saves it in local_536 and sets cfg_createNurbs; restored after intersect_multiple_surfs | unchanged |
| Curve Parting Line (sketcher_curve_7) | FUN_1802be7b0 (l.31673) | = cfg_createNurbs | left |
Curve All Edges (FUN_1802c3f10 / FUN_1802c4a70) does not write it. Start value 0 (mccore 0x1813fb8f2). At integration: one shared
`msf_nurbs_splines` in core_ui, set by our ports of the five "left" commands (cui_dsurf.c Curve Dynamic, cui_surf.c Flowline / One Edge,
cui_slice.c, cui_partline.c), read by chain_srf_edge (cui_chain.c, now its own static 0).
