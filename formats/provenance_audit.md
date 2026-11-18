# Provenance audit (2026-10-14)

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

What in the code came from Mastercam (decompiled code, extracted resources, the user's config/keymap, help pages,
real part files / NC output) and what was improvised. Four read-only reviews; verdicts: SOURCED, VALIDATED (measured
against Mastercam's own output), PARTLY, INVENTED.


## Current status (2026-10-15, end of day) - the lists further down are the 2026-10-14 audit plus later fixes
Still NOT DECODED / ours (everything else below has been replaced by sourced behaviour or validated):
- Toolpath engines without reference ops: Dynamic/Area path pattern (material validated), Dynamic Contour, Peel, Helix
  Bore, Circle Mill, contour-parallel pocket ordering, arc refit for our ops. Dynamic entry methods (+0x16bb).
- Op dialog: title name part, tool info line, Circle Mill labels (pages HSTCircmill* not mapped to record offsets),
  thread depth / radial passes / full-profile controls, Dynamic Contour wall controls, Peel slot-width rule.
- Drawing: Arc Tangent mode 3 (Dynamic); Extrude: draft direction (Reverse = negated taper angle, base sense is
  Parasolid's), default thin direction,
  Through all / Trim to faces.
- Windows of our own: none left on the list. Pivot markers done 2026-10-15 (glBitmaps from mastercam.dll
  0x18087d918 / 0x18087d940 via FUN_18022a1b0, auto-highlight colour config 5000). The
  read-only "Operation N parameters" window is removed (an op this program cannot regenerate opens nothing).
- Renumber tools: dialog 805 + FUN_1804574e0 (formats/renumber_tools.md); the earlier invented scope / clash message
  are gone. Differences: ops are written directly (Mastercam patches their NCI tool-change records), stored ops in
  scope are taken over and regenerated; the drill "+0x3d0 == 3" exclusion of the unused-tool pass is NOT DECODED.
- File tab (backstage): the 15 entries of IDR_BCGP_RIBBON_XML CATEGORY_BACKSTAGE; pages from ribbonuires dialogs
  (107 Info, 129 / 130 Open, 130 Save As, 105 Convert, 140 Print, 115 Help, 111 Community). Working: New, Save, Open
  (recent documents, Browse), Save As (Browse), Help > About Mastercam; the rest greyed. NOT DECODED: page button
  images, Info property values (strings ribbonuires 193-201 are theirs; the code filling them was not traced),
  which page shows first (Info here), the back button's look. The invented help texts and "Test parts" list are gone.
- F9 (OnDisplayInfo = ribbon "Show Axes"): flips mastercam.dll show_part_info, which mccore reads only for axis
  drawing (XfBBox_DrawAxes, setup-sheet capture saves / clears it) - no information text. The invented "Info" panel
  is removed; F9 toggles the axis lines. About stays this program's own box on purpose (Mastercam's dialog 103 is
  licence / version / build information about Mastercam itself; licensing is out of scope).
- Analyze (AnalyzeStart): the invented "Analyze entity" window is replaced by Mastercam's Point / Line / Arc
  Properties dialogs (mccadutilres 1073 / 1080 / 6470, formats/analyze.md): live edits with Mastercam's rules, OK /
  Esc commit as one undo step, Cancel / X restore, Previous / Next with wrap, Propagate (level, changed colour),
  AutoCursor point picks, Mastercam's bitmaps, Color button = the Colors picker (sel_color_dialog: CColorDlg IDD 593,
  page 106 with the 16 x 16 palette grid; page 108 Customize greyed). NOT DONE (greyed): Level picker, Style / Width (not
  modelled), arc resize AutoCursor, Flip view, Help; dialog position; non-Top planes (values are world = Top here).
- Right-click mini toolbar (2026-10-15): mastercam.dll FUN_1801c9660 builds UIMiniToolbar from the ribbon controller's
  list (ribbonui 0x18007a360): ribbon commands 53409-53423 (point / line style, width, wireframe / solid / surface
  colour, Set All, Set From Entity, Clear Colors, 2D/3D, Z depth, Change Level). Drawn with their ribbon images; only
  Set From Entity works here. Its position relative to the menu is NOT DECODED.
- Toolpaths menu greyed as NOT DONE (their invented versions removed 2026-10-15): Reverse toolpath (ReversePath ->
  mccore bnci_reverse / FUN_1800d0770 reverses the stored NCI: comp 1040 / 1041 swapped, rapids / arcs rebuilt) .
  Recalculate feeds/speeds: DONE 2026-10-15 (formats/feeds_speeds.md). (Chaining window: mccoreres dialog 1058, done; Edit common parameters and Tool list: below.)
- Tool list... (mccore RunToolReportForSelectedOps; formats/opmanager_menu.md): the report template named by config
  31017 / 31011 from the reports folder (31020) laid out by viewer/rpx.c over the data FUN_18061bdc0 /
  WriteXML_MillToolList write. NOT DECODED: ASSY-NAME / HOLDER-NAME (part TlMgr assemblies), the ActiveReports viewer
  window (title, toolbar, zoom, print), report fonts (drawn with the UI font), LoadRpxInfo's tag -> kind codes (the
  TOOL/TOOL-x choice follows the template's subreport bindings). Growth / KeepTogether rules are ActiveReports' own,
  not measured against the viewer.
- Edit Common Parameters (opmanres dialog 243, CEditCommonDlg; formats/opmanager_menu.md section 4) - done for the
  check box / value / combo settings, Propagate, Enable / Disable All, OK / Apply / Cancel, the empty-field check
  (3786 / 3787), Ctrl / Shift multi-selection in the tree (COpMgrTree::Selchanged). NOT DONE (greyed): the
  Clearance... / Retract... / Feed plane... / Top of stock... / Depth... pick buttons, Coolant, Canned text, Misc.
  values, Home pos, Planes, NC Name, Tool display, Ref point, Select library tool, Tool filter, Help; the machine
  definition rpm / feed clamp (FUN_1801451d0, 3788 / 3789); stored ops this program cannot regenerate are skipped
  (Mastercam edits them too); Propagate from an op made here fills only what CamParams hold (head #, offsets,
  retract rate, When, rapid retract, force tool change, batch come from the save's template record). Op list rows
  (COpMgrList columns) NOT DECODED - shown as the tree's. Deviations: edited ops are regenerated at once (Mastercam
  patches the NCI via bnci_update or marks them dirty); Tool Selection sets the op's tool shape only (the tool
  system call's feed / speed handling NOT DECODED); right-click on a selected op keeps the multi-selection (ours).
- Posting from stored NCI: drill cycles 0-2 now read from the NCI record (+0x18 cycle, +0x48 1st peck, bnci_wr_81);
  tap / bore / misc cycles: the post stops with an error naming the op and cycle (no silent G81).

## Core (files, toolpaths, post) - mostly sourced / validated
- SOURCED/VALIDATED: part-file reading (all install files parse), op/TP/NCI records, unchanged rewrite byte-identical
  (20 parts), stored-op -> editable op (190/190), contour engine (138/138 within 0.2 mm), drill 5/5, thread 4/4,
  face 2/2, Dynamic/Area material (missed <= 0.5 %), stored-NCI LinuxCNC post (BRACKET4 23231/23231 blocks).
- INVENTED: Dynamic/Area path *pattern* (only the material removed matches), Dynamic Contour, Peel, Slot, Helix Bore,
  Circle Mill (no reference ops), contour-parallel pocket link/order rules, arc refit for our own ops, G83 peck
  (no sample), XT mesher (display only), .mcamui project format.
- UNVERIFIED: every exported .mcam / .mcx-9 (never opened in Mastercam). New arc records write +0xc8 = u16 1,
  u16 0: the same as all 98 arcs Mastercam wrote in A001.mcx-9 (other parts carry varying non-zero +0xca values,
  session-dependent, most likely padding) - measured 2026-10-14, so not a bug. Template fallback from other parts brings in unrelated hidden settings.

## Window, keys, view - data sourced, look and mouse invented
- SOURCED: ribbon commands/tabs/galleries, Toolpaths manager toolbars, keymap (user's mastercam.kmp), arrow/zoom/
  pan/rotate steps (config 4026-4029), grid defaults (mastercam_grid.c), background colours (config 5007/5009 +
  palette), New = empty part / Top / level 1 / depth 0 (init_db), graphics cursor (gafres.dll CURSOR 4).
- INVENTED: mouse (left-drag rotate, right-drag pan, wheel 1.15 - Mastercam: left selects, right = menu, config
  33009/33010), geometry/toolpath/solid colours (config 5027/5045/5046/5052 unused), grid look (fading infinite grid
  under the part; Mastercam: points + dashed lines at construction depth), view animation 350 ms (config 33006-33008
  unused), gradient direction (config 5041 ignored), status bar, QAT, backstage panes, gnomon, title bar, Info/About/
  Analyze windows, start-up Isometric + Toolpaths tab, Mill machine group on a new part, unsaved-prompt wording
  (real string: mccoreres 722 "Save changes?").

## Drawing and solids - about half the defaults sourced, interaction invented
- SOURCED: Point Position, Small Arcs, Bolt Circle, Line Closest, Circle Center Point, Arc 3 Points / Endpoints,
  Rectangle, Polygon options, Trim Many, Break/Join family, many panel defaults (formats/wireframe.md [code]).
- PROMPTS (2026-10-14): Mastercam's strings, by the id each command loads (mccad.dll / mccadutil.dll imported in
  ~/mastercam-re/ghidra_cad; decomp/mccad_cmds/; formats/mccadres_strings.tsv, mccadutilres_strings.tsv). Steps whose
  string was not found show the command name (Circle Center Point radius pick, Arc 3 Points 2nd/3rd, Arc Tangent
  point, Point Dynamic entity pick, Offset / Project / Fillet Chains / Polar Endpoints / Rect Shapes). AutoCursor:
  see the camera section (8.7 px, help-page order).
- FIXED 2026-10-14: Line Tangent Through Point = sketcher_line_6 (tangent at the projected pick, to the 2nd pick
  along the tangent, or +-length with "Select which line to keep"); Arc Tangent = sketcher_arc_6 modes 0 (one entity:
  semicircles at P +- R n), 1 (through point: offset meets circle (q, R), enclosing case for arcs), 6 (two entities:
  untrimmed fillet arc), arcs from FUN_18017bc70 (both halves per centre), the one picked kept; modes 2-5 not ported.
- FIXED 2026-10-15: Trim modes renumbered to Mastercam's (0 Automatic default, executes like Trim 1 entity; 6 Modify
  length); Chamfer Chains styles Distance / Width (one value, make_chamfer_chain) instead of a hidden 2nd distance;
  Offset Chains side = the picked point (as decoded, direction 0).
- FIXED 2026-10-15: Paste = runPaste flow (prompt 5457, Enter / Esc, merged in place, attributes kept; the panel's
  attribute checkboxes NOT DECODED); undo depth = config 16013 / 16014 (unlimited for the user); Arc Tangent mode 2
  (centerline: full circles at the offset line x centre line, 0x16a9 on failure). Gallery icons drawn at their own
  64 x 32 size (no stretching), cells from the gallery's GAL size / column count.
- Extrude (2026-10-15, mcgeomsld.dll imported in ~/mastercam-re/ghidra_sld): defaults distance 50 mm / 1 in, draft
  5 deg (off), thickness 1 mm / 0.1 in, type Create; labels Create Body / Cut Body / Boss Body (mcgeomsldres 847-849).
- INVENTED / WRONG: Extrude direction / thin-wall option names and meanings of the other param ints (not decoded);
  Arc Tangent mode 3 (dynamic). Modes 4 / 5 ported 2026-10-15 (createArcTangentTo3Entities iteration, 0x16a5).

## Toolpath dialogs and tool manager - menu sourced, dialogs and defaults invented
- SOURCED: Toolpaths right-click menu tree, enable rules, group messages, tooldb loading, tool type numbers,
  face stepover 75 / across 50, Dynamic 25 % / 10 % / 0.25, ramp 3 deg.
- INVENTED: default heights/depth/feeds (real: clearance 50 off, retract 25 on, feed plane 10 inc, depth 0 inc,
  rpm/feed 3500/50/25, max rough step 2), pocket 40 % (real 50), contour leads/passes, tabs, face along/style,
  back feed, helix pitch, thread depth rule, dialog labels (real labels in ui_fields_mcmill.tsv), contour type /
  compensation controls, tool editor feeds (Vc 150 / fz 0.03; real: material SFM/FPT), Recalc feeds, Reverse
  toolpath, Edit common parameters, Tool list, Undo snapshots, chaining window, op info window.
- Unused source found: shared Mcam2017/mill/Ops/Mill_MM.mcam-defaults (one default op per type, part format).
- Fixed 2026-10-14: new operations take their parameters from Mastercam's default operation of that type in the
  shared Mill_MM / Mill_Inch.mcam-defaults (cam_defaults_from_file, same record mapping as stored ops; all 189 stored
  ops still regenerate identically). Lead lengths stay the default op's absolute values (rescaling with the tool:
  NOT DECODED). The invented Vc 150 / fz 0.03 feed calculation (and its always-metric bug) is removed (Mastercam's
  material-based calculation: NOT DECODED).
- Fixed 2026-10-15: a new tool = Mastercam's default tool of that type and units from
  common/ToolData/DatabaseDefaults.tooldb (TlAssemblyItemFactory FUN_180abb8f0 loads it, lambda FUN_180abba80 matches
  MCToolType + IsMetric): geometry, flutes, angles, pitch, rpm 1000 / feeds (metric 120 / 600, inch 5 / 25), empty
  name. A given diameter replaces only the diameter (SetOverallDiameter), sphere mills keep r = dia / 2. Tool editor
  "new" = Endmill1 Flat (TlToolEndmill::Initialize type 10) at its default size. Part tools get D / H = tool number
  (all 114 tools in the user's parts). Block Drill (20) has no default in the file.

## Replacement status (no-improvisation rule, started 2026-10-14)
Done (sourced):
- Colours: Mastercam's 256-colour palette (mastercam.dll @0x8a6440) for geometry (entity colour), toolpath feed /
  rapid (config 5045 / 5046), solids (5052), grid (5032), background (5007 / 5009), origin lines (5001). Solids opaque
  by default (config 6030 Translucent? n); the automatic switch to translucent after generating was removed.
  Translucency level: NOT DECODED (alpha still a placeholder).
- Unsaved-changes prompt = mccore save_on_exit (decomp/mccore_save_on_exit.c): caption 5175 "Mastercam 2017", text
  7291 "Mastercam file '%s' has changed. Save it?" (name with .mcam), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2;
  Yes -> Save dialog, No -> continue; no Cancel. Name for a never-saved part (default_geo_filename): NOT DECODED.
- Graphics cursor: gafres.dll CURSOR 4 shape.

- Graphics view mouse and camera (decomp/mastercam_viewmouse.c, _viewrot*.c, _dynrot.c, _dynview.c, _mouseaccel.c,
  _stdviews.c, _fit*.c, _setview.c, _viewanim.c, _pankeys.c, mccore_viewanim.c, mccore_spin.c; handler map
  mastercam.dll 0x5ea160):
  - middle drag rotates; Shift+middle pans (swapped when config 33009 = y); left drag no longer rotates, right drag no
    longer pans.
  - pivot = geometry under the cursor (selection aperture 522/30 = 17.4 px), else view centre at construction depth.
  - config 33002 y = free rotation (FUN_1801b0bd0 mode -1, a = dx 2pi/w, e = dy 2pi/h, pivot fixed on screen);
    n = turntable (mode 0, az/el from the matrix).
  - wheel: 120 per notch, s = n * zoom_fac (config 4026), height *= 1 - s about the cursor; 33010 reverses;
    one history push per wheel run.
  - view = matrix rows + centre (view coords) + view height, parallel projection. Standard views 1-8 from
    ReservedPlanes::BuildStandardMatrices; SetGrView keeps centre and height; Fit = visible extents x 1.05
    (empty -> 250 mm / 10 in); Unzoom = height / 0.8; Unzoom previous = history or x2; Fit/zoom animation
    (33007 / 33008, delay*12 steps, sin^2 easing); pan keys = height * steps * config 4028.
  - Tested with MCAMUI_EVENTS mdown/move/mup/wheel (2026-10-14).
  - Standard view change (Top / Front / Right / Iso, CommonPlaneMenu 0x8024-0x8027 -> FUN_1801ccd70, config 33006):
    rotation along MtTmat3d::GetMinAngleDiff (gafmt.dll, 6 Euler orders, smallest |a|+|b|+|c|), eased 1 - cos(t pi/2)
    (FUN_1801c0a90 mode 0, hidden 5th argument), delay*12 steps of >= 17 ms; target centre / height = StoredViews
    entry for that view (last centre / height used in it), else a fit in the target view; the current centre at the
    depth of the FUN_1801bdd00 box middle is carried along. Back / Bottom / Left too (commands 0x81a6-0x81a8 -> views 3 / 4 / 6). Box middle leaves out STL / stock / part-info parts (no data here).
  - View keys (FUN_1803222f0): one step per key-down incl. key repeat; Alt+arrows rotate config 4029 degrees
    (FUN_1803278f0: Left/Right about the screen up axis, Up/Down about the right axis, centre carried at the depth of
    the mean displayed point); arrows pan; PgUp/PgDn zoom a notch; Home fits. Removed (invented): smooth held-key
    repeat, Shift = fine rotation, Ctrl+Up/Down stepping through operations.
  - Ctrl / X / Y / Z held in a free-mode middle drag: rotation about the screen normal / world axis (RotateObject
    mode 1-4, FUN_1801cf1c0), angle = cursor direction about the pivot's screen point; pivot fallback is now the screen
    centre at the mean displayed depth (FUN_18013dbd0). The two pivot markers (GrTemp_AddMarker 0 / 1,
    auto-highlight colour): decoded 2026-10-15 and drawn (see "Current status").
    - Fit includes the displayed toolpaths (fit_toolpath = 1 in mastercam.dll .data; FUN_180320770 adds the draw_bnci
    extents); the view-transition target fit runs with it cleared (FUN_1801ccd70).
  - Graphics right-click menu = the "Right mouse button menu" dropdown in the user's CONFIG/default.mtb (DDCOMMAND n
    -> the dropdown "DROPDOWN START n" as a submenu, here "GView"); item text = the command's tip string, second line,
    from its resource DLL (tools/gen_cmd_labels.py -> viewer/mc_cmd_labels.h). Commands without a port say so.
    The 2017 mini toolbar is NOT DECODED and not shown. (FUN_1803a9440 turned out to be the status-bar plane menus.)
  - AutoCursor snap: aperture sel_dist * 0.5 = 8.7 px (dl_search_fast); among several, the help page's kind order
    (point, endpoint, midpoint, centre), nearest within a kind. Grid near-snap also sel_dist * 0.5 (mastercam_grid.c).
  NOT DECODED (left out): AutoCursor quadrant / intersection / nearest defaults and the per-entity choice inside
  dl_search_fast; pivot / AutoCursor visual cues.

## Messages (2026-10-15)
- Chaining prompts: Contour "Select Contour chain" (ctour_toolpaths chain_it_point 4267), Slot "Select SlotMill chain"
  (mcmillres 2808), 2D HST "Select 2D HST machining chain" (mccoreres 10208), Pocket "Select Pocket chain" (pock_assoc ->
  mccore prompt_pocket chain_it_point 4268), Facing (face_assoc: mcmillres 2647 "Select OK to use defined stock or select
  chain" when the active machine group's stock X / Y (op_group +0x1230 / +0x1238, read from the first group) > 0, else
  2648 "Select Face chain"); drill / helix / circle / thread not traced: their name only.
- View / edit prompts from opmanres (mastercam.dll LoadString): OnHide 3863 "Select entities to keep on screen",
  OnScreenZoom 3873 "Specify zoom window", SbSelectRotationPosition 3317; AnalyzeStart mccadutilres 6333.
- Drawing failures: only "Cannot create the arc" (0x16a1, arc commands), "Unable to create an arc using this radius"
  (0x16b2, Arc Tangent two entities), "The selected objects do not intersect" (0x1743, Trim Many). All other invented
  geometry / undo / delete / paste / hide / count messages removed (the commands just do nothing on failure).
- Removed 2026-10-15 (Mastercam is silent there): op generated ("N moves generated..."), op deleted / pasted /
  undeleted / reversed, feeds recalculated, tools renumbered, ops regenerated after a tool edit, the Extrude result
  line, "nothing to paste / undelete". Extrude prompt = mcgeomsldres 470 "Select chain(s) to extrude"
  (SolidsExtrudeApp FUN_1800dc600 chain_it).
- Kept (our own features, no Mastercam counterpart) - INTENTIONAL, 2026-10-16 list: .mcamui project open / save
  errors (Refusing to write a part, Cannot write / replace, not a CAM workbench project, part could not be loaded, N
  operations could not be regenerated), file dialog errors (cannot list, does not exist, part files are never
  overwritten, press Save again to replace), Save as MCAM part (open a part first / nothing made here), LinuxCNC post
  results (nothing to post, cannot write, Not posted, posted N operations), tool library load / write errors, op
  generation failures (the CAM engine's own text), "Only operations created here can be deleted", "Too many
  operations", deleting geometry used by an op (Mastercam asks opmanres/mccoreres 4421 "Geometry is used in a toolpath
  or plane.  Are you sure?" and deletes; refused here so a regenerate cannot cut a wrong path), Print result, the
  part_add_tool unit-mismatch and 256-tool limits. The "not available in this program" results are gone (2026-10-16):
  unported commands are greyed and silent.

## Icons / QAT / manager (2026-10-15)
- Icons: Mastercam's own PNGs read at run time from the user's extraction (~/mastercam-re/ui/icons; ribbon images via
  ribbon_extract.py, map in viewer/ui/icon_map.tsv, gallery items keyed "gallery|item"); nothing bundled. Commands
  without a known image draw none (the generated letter tiles are gone).
- QAT = CONFIG/Mastercam.Workspace QuickAccessToolbarCommands + QATFunctionTable (New, Save, Open, Print, Save As,
  Zip2Go, separator, Undo, Redo).
- Toolpaths manager toolbars 800 / 804: opmanres TOOLBAR + PNG strips (one 16 px cell per button in order). The
  invented "Tool Manager / Delete op / Save" button row is removed.
- Tree images: COpMgrTree::LoadImageList (65 opmanres bitmaps, white mask) with InitTvItem indices: machine group
  0x31, toolpath group 0x10, op 4 (selected 5; dirty 0x13 / 0x18 not used here), Parameters 6, tool 8, geometry 10,
  toolpath 0xd (0x29 none), Properties 0x33 with Files 0x34 / Tool settings 0x35 / Stock setup 0x36 / Safety zone 0x37
  (names opmanres 3825-3829; Safety zone row added).
- Status bar = ribbon XML STATUS_BAR: X / Y panes ("X:   " + 5 decimals, ribbonui.dll templates), Z depth, 2D/3D,
  CPLANE / TPLANE / WCS popups (runtime text format NOT DECODED: name + plane), display buttons with
  IDR_BCGP_IMAGE_STATUS_GROUP_5 images (Dimmed / No Hidden / Outline Shaded not available). Removed (invented):
  SECTION VIEW, SELECTED ENTITIES, GRID / SNAP / Grid... buttons.
- Window title: "<file> - Mastercam" (MFC SDI default with IDR_MAINFRAME string 128; version suffix NOT DECODED).
- Drag sizes (2026-10-15): Polygon (centre, radius pick unless typed), Ellipse (centre, A, B picks; lines within
  min_arc_length 0.02 instead of Mastercam's NURBS spline), Arc Polar (centre, start, end picks with radius / start /
  end locks), Rectangular Shapes 1-point mode (base + size pick, anchors lower-left / centre), defaults from the
  decoded param structs (sizes 0 = pick).
- Op dialog (2026-10-15): Contour type 2D / 2D chamfer, Compensation type Computer / Off and direction Left / Right
  (mccoreres 4561-4597; side_rel = Mastercam's chain-relative side; new contours default Computer + Left from the
  defaults file (Mill_MM contour op: +0x5d0 comp 0, +0x5d2 dir 0); Left = tool left of the chain direction (help
  tpfields/Fields_common_tp_params/Comp_direction.htm), verified on picked rectangles 2026-10-15); sections named Tool / Linking Parameters; Retract (drill R plane), Stock to leave on
  walls, Tip comp. Control / Wear / Reverse wear not offered (post has no G41/G42).
- Chaining direction (2026-10-15): Mastercam's "Use cursor position" rule (user config 6002 = y; help
  Direction_for_closed_chains_Use_cursor_position) - the chain starts at the picked entity's end nearest the cursor
  and runs toward its other end; closed chains start at that entity (cam_chain_orient). Now from the decomp
  (formats/chaining.md, decomp/mccore_chaining.c: add_chain / ep_n_from_t_sel): the end is chosen by the pick
  parameter on the entity (t >= 0.5 -> end 1), the same for open and closed chains. The CW/CCW default (6001) used
  when 6002 = n is not implemented (add_to_area sign not decoded). The chaining walk itself is now FUN_1801e3de0
  (formats/chaining.md): stops at branch points like Mastercam; continuing past a branch by picking is NOT PORTED.
- Op dialog labels (2026-10-15, mcmillres dialogs / ui_fields_mcmill.tsv): Drill - Cycle combo (dialog 25595; mccoreres
  7645 Drill/Counterbore, 7656 Peck Drill, ... every 11 ids; op +0x1228 now kept as drill_cycle, written back), "1st peck"
  (+0x1230), "Tip comp" (1310); a tool's CannedCycleType / Peck1 set the op's cycle and peck. Only Drill/Counterbore (G81)
  and Peck Drill (G83) are offered; a stored Chip Break op still posts G83 (no G73 yet), other cycles show "(not posted)".
  Pocket - "Stepover percentage" (dialog 3003), "Plunge angle" (Entry - helix, dialog 3010), finish "Passes" / "Spacing"
  (dialog 3004); the invented "Corner clean-up" switch removed (behaviour kept on, as validated). Contour - finish
  "Number" / "Spacing" (Multi Passes, dialog 25591). Face - "Max. stepover [%]" (dialog 2942), style names mcmillres
  2675-2679. Still invented: dialog title, the tool info line, Output arcs / Coolant buttons, Generate / Cancel, the
  status texts under them; helix / circle / thread / slot / dynamic labels not yet mapped.
- 2D High Speed (2026-10-15): stored / default ops now take min toolpath radius % (+0x1658), micro lift (+0x16d0),
  plunge angle (+0x1678), back feedrate (+0x1248, else the feed - the invented "2 x feed" fallback removed) and the gap
  size (+0x1600, or +0x1608 % of D when +0x15cd) from the record, as LoadDynPockParm (mcmill FUN_1801af380) does. The
  helix radius (+0x16c0) is NOT taken: Mastercam's entry methods (+0x16bb) aren't modelled and the 10 mm values left
  material in small pockets (oracle). Oracle after the change: 34 Dynamic ops, missed median 0.0 %, worst 0.1 % (same as
  before); 26 ops' motion changed (regression baseline refreshed, old one in ~/mastercam-re/regress/base_before_dyn_params).
  Labels from dialog 2784 / Entry Motion / dialog 657 (Machining region strategy: Stay inside / From outside); the
  engagement read-out removed. Still ours: Helix radius 0 = 0.5 R, Peel slot width 0 = 1.5 D, Dynamic Contour wall
  stock and side.
- Helix Bore: Rough pitch (+0x1250), Finish (+0x1278) / Finish stepover (+0x1288) from the record (dialog 2823); no
  stored helix ops to validate against. Circle Mill page not mapped (labels unchanged).
- Thread Mill (dialog 2824): Thread pitch, ID / OD thread, Right- / Left-hand thread, Machining direction Top to bottom
  / Bottom to top. Major thread diameter 0 = the picked circle's (ours; +0x1298 holds 25 in every stored op). Thread
  depth, radial passes and "full profile" are still our own controls.
- Drill / Helix Bore / Circle Mill / Thread Mill point picking: all go through mcmill tp_pt_mgr (Drill Point Manager,
  dialog 2074); the status-line prompt during picking is NOT DECODED (op name shown).
- Chip Break (2026-10-15): posted as G73 with Q = 1st peck, like Peck Drill's G83 (Generic Fanuc 3X Mill.pst: pchpbrk$ =
  ppeck$ with sg73); offered in the drill Cycle list. Stored ops posted from their NCI still always give G81 (the NCI
  drill record's cycle field is not read; all the user's stored drill ops are cycle 0).
- Op dialog chrome: OK / Cancel (OK regenerates), "Create arcs in XY" (Filter settings dialog 318), "Flood: Off / On"
  (Coolant page); the "editing ... Regenerate replaces ..." texts removed. Title: Mastercam builds "2D Toolpaths" (mcmillres
  2755) or "2D High Speed Toolpath" (2707) + " - " + a name from a virtual method (FUN_18026dc70) that was not resolved,
  so our titles stay ours.
- Extrude panel labels from mcgeomsld.ui.dll (WPF SolidsExtrudeManagerView BAML): Create body / Cut body / Add boss,
  Both directions (checkbox; our "Symmetric" removed from the panel), Reverse all, Thin Wall Direction 1 (inward,
  ThinWallInwardDistance) / Direction 2 (outward, ThinWallOutwardDistance) / Both with separate Direction 1: / Direction 2:
  thicknesses (new thick2, saved as a 12th EXTRUDE field). NOT DECODED: whether Both directions means the distance each
  way (ours) or split, the draft direction (our "Outward"), the default thin direction, Through all / Trim to faces.
- Arc Tangent mode 3 (Dynamic): createArcTangentDynamnic = cr_a_ppt(tangent point, tangent direction, end point), 0x16a1
  on failure; the tangent point / direction are filled through a pointer Ghidra does not show (sketcher_arc_6
  local_3598 / local_3550) - NOT DECODED, not ported.
- Slot Mill (2026-10-15): Stepover [%] (+0x1268) and Plunge angle (+0x1278, degrees) from the record (dialog 3020);
  Mill_MM default 50 % / 3 deg.
- Dynamic re-entry ramp (2026-10-15): length pi x min toolpath radius (measured in all stored Dynamic ops, = mcce
  +0x1f0) instead of the invented 0.15 x D; straight-line shape still ours. Oracle: 35 ops, missed worst 0.1 % (unchanged);
  regression baseline refreshed (old in ~/mastercam-re/regress/base_before_ramp). Helix-only entry radius / placement:
  NOT DECODED (one sample in the data).
- Extrude Both directions DECODED (2026-10-15, mcgeomsld sld_extrude -> FUN_1800d2c80): params +0x3a set -> the profile is
  copied, swept by Distance one way and by Distance the other way (both distances = params +0x18), then unioned. Ours
  matches (EXD_BOTH). Draft: +0x40 on, +0x44 Reverse (ps_sweep_sheet negates the angle), +0x48 angle; in Both directions
  the second sweep gets the opposite Reverse unless +0x3b. Base direction of an unreversed taper: NOT DECODED.
- Point picking (2026-10-15): Drill / Helix Bore / Circle Mill / Thread Mill go through mcmill tp_pt_mgr; its Drill Point
  Manager loop (FUN_18008b0d0) calls get_event with an empty prompt, so the prompt line is now blank for these ops; an
  op without points shows "0 points defined!" (mcmillres 2397, tp_pt_mgr FUN_180072bf0 warning).
- Chaining dialog (2026-10-15, mccoreres dialog 1058 + run_chain_dlg / message map in mccore): Wireframe, Chain
  (FUN_1801ec690 -> FUN_1801ddfb0), Single (FUN_1801edd90 -> chain_single FUN_1801df0b0, cam_chain_single), Unselect
  (unselect_last_chain), Unselect All (MCMessageBox 5007 "Are you sure?" / 10206 "Unselect All Chains?", MB_YESNO),
  Reverse (reverse_chain of the last chain), OK; closing = cancel. Picking an already chained entity adds the chain again
  (the 0x40 bits are cleared per chain; pocket warns 4808 on duplicates) - our click-to-remove is gone. Chain prompt =
  Format("%s %d", resource text incl. trailing space, chains + 1). Chained / selected entities in config 5033 Select
  color. Direction arrows: start arrow (palette 86, or 4 when (5007 - 2) & 0xf7 == 0), open chains also an end arrow
  (palette 12); length 0.1 x view height, head 13 x 1.07 px, head radius / 2, shaft radius head / 5 (ChainArrowRatio
  {0.05, 0.075, 0.1}, size 2; GrArrow::Regenerate). Drawn flat; head at the far end NOT confirmed from the code.
  Not implemented (left out of the dialog): Solids, C-plane / 3D, Point, Window, Area, Polygon, Vector, Partial, Wait,
  Last, End chain, Chain Feature, Start / End Back / Forward, Dynamic, Options.
- Prompt (2026-10-15): DisplayPrompt -> CPromptWnd: bordered popup (opmanres dialog 0x202 by default, style
  WS_POPUP | WS_BORDER), white background (registry Prompt Wnd "Background color", default 0xffffff; the user's registry
  has no Prompt Wnd key), size = longest line + 12 x lines x line height + 4. Position NOT DECODED (drawn at the top-left
  of the graphics area). Replaces our blue result line.
- Chaining modes added (2026-10-15): Partial (mode 7: first pick sets the first entity / start end, the walk from its far end
  with the next-entity rule until the last picked entity, which joins at the touching end; prompt 4271 "Select the last
  entity"; failure 4171; FUN_1801db3f0 / FUN_1801e54a0 - whether the walk continues through branch points is NOT clear
  from the decomp, ours stops). Start Back / Forward (FUN_1801e6980 / FUN_1801e69f0: last entity to the front / first to
  the end, closed chains). Window (mode 5, FUN_1801dfae0): screen-space window select with the mode combo Inside / In +
  Intrs / Intersect / Out + Intrs / Outside (default 1 Inside); config 6024 y -> "Sketch approximate start point" (4235);
  each selected entity seeds add_chain(t = 0) taking only selected entities; closed chains to config 6001 (1 = CCW,
  add_to_area sum of (y1 + y2)(x2 - x1)); masks 6012 / 6013; ordered from the start point (nearest start; closed: start
  moved to the nearest entity start; open: reversed when the end is nearer); next search from the chain end (6000 Zigzag
  y) or start. FUN_1801e9490 post-processing of the window chains not ported. Fixed on the way: the chain arrow layer
  blocked clicks (hover test now skips it); mode buttons reset Partial every frame.
  Area (mode 4, chain_area @0x1801dd160): every chainable entity chained (t = 0, closed to 6001, open kept by 6012);
  boundary = innermost closed chain containing the click; open chains kept if nested, closed if nested or intersecting;
  infinite nesting off (6004 n) -> first-level islands only; database order; no boundary -> nothing (FUN_1801dd8c0 not
  ported); point entities inside the boundary not added (no point chains here).
  Polygon (mode 6): screen polygon, same selection modes and chaining as Window; prompt + 9904 "Sketch polygon for
  selection / Double-click or Enter to end polygon". Vector (mode 8, FUN_1801df560; help Vector_button): entities the
  sketched line crosses, chained with the crossing's pick parameter, closed to 6001, ordered from the first point; ended
  like Polygon (double-click / Enter - inferred from the shared sketch input, the vector prompt adds no text). Prompts per
  mode (run_chain_dlg): Area 4234 "Select area", Partial 4270 / 4271. Last (FUN_1801ed6b0, help Last_button): the
  previous chaining's chains again, warning 4211 when chains are selected and Last was used. End chain (FUN_1801ecb70):
  a partial in progress ends with the first entity. Options (dialog 405): masks, CW / CCW / Use cursor position, One way /
  Zigzag, Infinite nesting - session only (the config file is not written). Use cursor position off -> picked closed
  chains turned CW / CCW (add_chain FUN_1801e8660(!cursor)).
  Branch points (2026-10-15): a chain whose walk stops at a branch is flagged (add_chain CHAIN +9); the prompt is 4269
  "Branch point reached.  Select next branch." and the next pick continues it (FUN_1801e3850: the pick must touch the
  chain end, else 4175; then the walk goes on); End chain finishes it (FUN_1801ecb70). Start Back / Forward on open
  partial / branch chains = one entity more before the start / the first dropped (FUN_1801e7740 / FUN_1801e7890), End Back
  / Forward = last dropped / one more after the end (FUN_1801e7500 / FUN_1801e75d0); enabling as FUN_1801f00d0.
  Wait (control 540, config 6023): a partial chain stays open after its last pick; further picks extend it from its end
  (FUN_1801db3f0 state 3, prompt 4269); End chain closes it. Dynamic (FUN_1801ec820, help Dynamic_move_start_end): prompt
  4229 "Select a chain arrow to move", the nearer chain end (closed chains: the start), then the new position on one of
  the chain's entities; kept as a cut of the first / last entity (CHAIN +0x28 / +0x30 -> Chain.cut0 / cut1, honoured by
  cam_chain_polyline and saved in the project file). Mastercam's live drag (FUN_1801e79a0) is a second click here, and
  FUN_1801ec820's local variables read "nearer the end moves the start" (likely a Ghidra stack-variable mix-up; the help
  is followed). Options: Color / Level masks (chain_color_filter / chain_level_filter in FUN_1801e3de0, config 6018 /
  6019), Chaining tolerance (config 1001 chain_tol, 0.002 for the user). Default mode from config 6014.
  Not implemented (all off / unused in the user's config): Point chains, Solids, C-plane / 3D (chaining here is planar
  in Top), Break entities in Dynamic (6007), Start chain at point (6009), Section angle (6011 / 6017), Plane mask (6015),
  Sync mode (6008), Reverse inner chains (6005), entity type mask (6025), planar tolerance (1002); dynamic cuts are not
  written into exported .mcam chain records (the TP record layout for them is not decoded).


## Intentional deviations (UI, 2026-10-16)
- Program name "apprenticeCAM" for "Mastercam" in the title bar and in Mastercam strings that name the program
  (unsaved prompt mccoreres 7291 / caption 5175, post_warning captions).
- About box and the startup risk-agreement modal (MIT licence, use at own risk) - ours, no Mastercam counterpart.
- .mcamui project files and their open / save messages (listed under Messages).
- LinuxCNC post and its result messages (the user's machine; formats/post.md).
- Print (OnFilePrint) saves a BMP of the view: Mastercam's print pipeline (opmanres dialogs 781 / 868, Windows printers)
  is not ported.
- Help (OnHelp, Alt+H) shows the user's keymap with the commands' own labels: Mastercam's CHM help is not ported.
- Doc file opens the written text file with xdg-open (Mastercam opens it in its own editor, edit_filename).
- Toolpath dialog width sized to fit a 435-du page template (ToolpathDlg size not decoded).

## Intentional deviations (all areas, 2026-10-16 documentation pass)
Ours on purpose; each is either a feature Mastercam does not have, a safety rule, or a stand-in for a Mastercam
facility that is out of scope. Formats: formats/program_structure.md 5.
- **Program identity:** name apprenticeCAM (title `<file> - apprenticeCAM`, About apprenticeCAM entries, file-type
  label "apprenticeCAM Project (*.mcamui)"); our About box (Mastercam's dialog 103 is licence / version information,
  licensing is out of scope); the use-at-your-own-risk agreement shown at every start (text = ../LICENSE warning,
  `MCAMUI_I_ACCEPT_THE_RISK=1` skips it for scripts); no user-visible text says Mastercam except Mastercam's own file
  filter names (tools/debrand.py filters the shipped layout).
- **.mcamui project format** (mcamui.c doc_write / doc_read, cam2d.c cam_write_ops): part reference, geometry drawn /
  deleted here, Extrude features, the tool list, ops made / edited here with their chains, Operations Manager order,
  stale / reversed marks. Toolpaths are regenerated on open; the part file is never written (saving onto .mcam /
  .mcx* is refused); written via `.tmp`, the previous version kept as `.bak`.
- **.tools text tool library** (toollib.c toollib_save / tool_parse): `MCAMUI-TOOLS 1`, `UNITS`, `TOOL k=v ...` lines,
  `HOLDER` outlines; beside Mastercam's .tooldb, which is read and written in Mastercam's format.
- **paths.conf** and `MCAMUI_<KEY>` (viewer/paths.h): the site folders (shared, user, schema, cad, posts, drive_x)
  instead of Mastercam's registry / install paths; nothing personal in the source.
- **Config-folder state files:** recent.txt (12 entries), icons.pack (built by tools/setup_from_install.py from the
  user's own install; icons are not shipped), panel_keys.tsv (wireframe panel values; Mastercam's storage NOT
  DECODED), toolmanager.conf (Tool Manager mode / display mode / list style / sort / column order, visibility, width;
  Mastercam keeps these in its registry / config, not traced).
- **Tool library backup timing** (toolmgr_ui.inc tm_lib_backup): Mastercam's FUN_18087ca80 copies `<library>bup` when the
  library is opened; here the copy is made before the first write instead (same restore on "No" / Cancel, FUN_1808753c0,
  same drop on OK, FUN_180875370).
- **Safety rules in the CAM code:** "no rapid below the feed plane" for the mcce Dynamic port (cam2d_dyn.c mcce_moves:
  rapid to the feed plane, then plunge feed; Mastercam's NCI has G0 descents into air that our engine cannot prove are
  in air); Area Clearance links only as a straight feed inside the tool-centre region, else retract / rapid / plunge
  (cam2d_area.c; the McHmm linker is NOT DECODED); delete of geometry used by an op is refused (Mastercam asks 4421 and
  deletes); unported toolpath engines are not generated rather than approximated (Dynamic Contour, Peel, Core
  Roughing, pocket cutting methods other than High Speed / finish-only / Zigzag).
- **Edit flow:** ops edited here are regenerated at once (Mastercam patches the NCI or marks them dirty); duplicate tool
  number warning keeps the edit open (ops find tools by number).
- **LinuxCNC post** (nc_post.c, the user's proven format, `G43 H0` deliberate) instead of Mastercam's MP posts.
- **Host file browser** for Open / Save (Mastercam's titles and filters, but not the Windows common dialog).
- **Print** saves a BMP; **Help** shows the keymap list; **Doc file** opens with xdg-open (see the UI list above).
- **cam_cli --verify** raster model (test tool only; Mastercam's Verify / ModuleWorks is not ported).
- **Surface closest-point fallback** (viewer/mcsrfcrv.c `mcsc_uv_of_point`, used by Wireframe > Curves): DEVIATION approved by the user, N9-25 in formats/user_decisions.md. Our damped Gauss-Newton when the port of MaSurface::PointMinDist stops off the surface; the DLL itself misses 12 of 399 points on PARALLELSTEEP ent 42 (tools/peloader --pmd), so the fallback is ours (formats/wireframe.md "Curves on surfaces").

## Corrections to the lists above (rechecked against the code 2026-10-16)
The sections above are dated logs; these entries are superseded by later work (formats/improvisation_review.md
"Replacement run"):
- l.166 "Removed (invented): smooth held-key repeat, Shift = fine rotation, Ctrl+Up/Down stepping": now true in the
  code (mcamui.c view_key: Alt+arrows rotate, arrows pan, PgUp / PgDn zoom, Home fits; Ctrl only = active viewport).
  The 2026-10-16 review had found them still present; they are gone. Likewise no "op reversed" message remains (l.193).
- l.10-11 / l.69-70 (engines without reference ops, invented Dynamic / Area pattern, Dynamic Contour, Peel): the
  invented patterns are removed; Dynamic Mill = mcce port, Area Clearance passes decoded (links made SAFE), Dynamic
  Contour / Peel / Core Roughing NOT GENERATED, Slot / Helix Bore / Circle Mill / Thread Mill ported from mcmill.
- l.12 / l.248-249 / l.270-272 (op dialog title, tool info line, "our titles stay ours"): the dialog is Mastercam's
  ToolpathDlg with decoded titles and pages (UI item 10, formats/mcamui_dialogs.md / opparams_ui.inc).
- l.25 "Help > About Mastercam": the entry is "About apprenticeCAM" (intentional, above).
- l.74 "Template fallback from other parts": removed; new ops copy config 2303's defaults library op only (EXPORT item 3).
- l.224 "Window title: '<file> - Mastercam'": the title is `<file> - apprenticeCAM` (mcamui.c l.1153).
- l.257 "Still ours: ... Peel slot width 0 = 1.5 D, Dynamic Contour wall stock and side": those generators are removed.
- l.262-263 Thread Mill "Thread depth, radial passes and full profile are still our own controls": the engine is now
  the bnci_threadmill port and ignores them; the two controls "Thread depth" / "Full-profile tool" are still drawn in
  opparams_ui.inc (l.278-288, marked ours) - an open UI item.
- l.266-268 "Stored ops posted from their NCI still always give G81": superseded by l.62-63 (cycles 0-2 read from the
  NCI 81 record, other cycles stop the post with an error).
- l.303-304 chaining modes "Not implemented": Window, Area, Polygon, Vector, Partial, Wait, Last, End chain, Start /
  End Back / Forward, Dynamic, Options were added the same day (l.309-345); the dialog itself is drawn from template
  1058 (chaindlg_ui.inc, UI item 15).
- l.72 "every exported .mcam / .mcx-9 (never opened in Mastercam)": still true - no exported file has been opened in
  Mastercam (no Windows here).

## Still NOT DECODED - summary by area (2026-10-16, collected from all specs)
Short list; each spec's own NOT DECODED section has the details and the next function to look at.
- **Part file** (mcam.md, mcam_write.md): curve / surface layouts unverified (no samples), machine-component
  kinematics, names of most op parameter numbers, drafting entities, thumbnail / description / history of a new part,
  built-in views 2-8 for arcs in other views, the machine group stock origin / display flag, solid-chain restore
  (needs a modelling kernel), op +0x5a8, tp_tool +0x3c8.
- **Dynamic / 2D High Speed** (dynamic.md, mcce_engines.md, mcce_port_plan.md): mcce geometry kernels and comparators,
  guided link planner FUN_1800f1080, link post-processors, medial / trochoidal / custom entries, helix-only radius,
  Chaikin smoothing, island-depth filter, taper + depth cuts, Mastercam's own rapid placement (safe rule used), Dynamic
  Contour band semantics and open chains, Peel roles (PeelAesthetics).
- **Area Mill** (area_mill.md): Core Roughing passes (McHmm kind 1, mckernel expansion), the general linker
  (EvaGeneralLinkerPlan: pass order, links, ramp Z), per-loop island chains, profile smoothing, AntiVibration /
  Horizontal Area, triangle-model Z.
- **Pocket** (cam_engines_code.md, tools_toolpaths_plan.md P5): cutting methods other than High Speed / finish-only /
  Zigzag, channel modes, ramp entry, sort_pock_bounds, find_path_around_island, open pockets, an arc-exact offset
  (ccomp_pock / MCCECalcOffset).
- **2D mcmill engines** (mcmill_2d_engines.md): Slot slot9.dll argument, Helix Bore finish modes, Circle Mill 0x1330 /
  0x1338 and feed switching, Thread Mill taper, bnci_drill_depth, tp_pt_mgr point sorts; Face has no decompile
  (measured rules only).
- **Post / reverse** (post.md, reverse_toolpath.md): coolant words, tap / bore / misc drill cycles, multi-axis, lathe,
  subprograms, cutter compensation; reverse codes -4..-7 / 1040-1041 meaning, arcs off the XY plane.
- **Feeds / speeds** (feeds_speeds.md): machine max-rpm clamp, shared-folder prefix rule, "From defaults" values,
  CheckNumberOfFlutes, op +0xf0.
- **Chaining** (chaining.md, above): add_to_area sign, start chain at point (6009), forced direction when 6002 = n,
  break entities in Dynamic, window-chain post-processing FUN_1801e9490.
- **Tools** (toollib_net.md, toolmanager_ui.md, tool_profiles.md, part_tools.md): native ToolLibrarySystem and
  `_Header.id` generation, TlCoolant state / Pos values and OldStyle 0 / 1, detailed-report tool fields, list registry
  names, wizard expression evaluator / calculator / custom coolant / holder page, dove mill r1 > 0 and buttress teeth,
  TlMgr record prefix +0x68..+0x378 and some discarded bytes, default holder of a new assembly, type change to another
  tool class, lathe and pre-X9 archives.
- **Backplot** (backplot.md): machine axis rapid rates (rapid / total time blank), path splitting, quick verify,
  material lists, cutter comp / Circle values on Details, wire dialog 1229.
- **Wireframe / solids** (wireframe.md, extrude.md, mcamui_dialogs.md): Mastercam's storage of panel keys, panel
  captions / Help, AutoCursor Fast Point, Point Dynamic commit, Extrude target search FUN_1800d8d70 and auto op type,
  draft taper direction, minimum distance, editing an existing extrude, part solids' history / Suppress.
- **Dialogs / managers / chrome** (mcamui_dialogs.md, ui.md, level_picker.md): op dialog HST tree nesting, drill title
  post text, page bitmap, dialog size, pick buttons; chaining lathe image set; drill point marker shape and sort;
  Levels / Planes row fill (plane archive not parsed), plane names other than Top, NC file name in tree rows,
  facing_op_uses_stock; Windows common dialog and GetDefaultPath; the rest of the Configuration dialog; prompt window
  position; AutoCursor quadrant / intersection defaults; translucency level.
- **Not studied / not ported at all:** Verify (ModuleWorks), 3D HST / multiaxis toolpaths (hst3d.md is a study only),
  lathe / wire / router ops, splines / surfaces, encrypted posts and licensing (out of scope).

Code comments found stale during the documentation pass (code not edited by it): mcamui.c:6289 (Analyze says Level
picker not done; it opens), solid25.c open_band vs mcamui.c feat_groups (open-chain side decoded or not),
levels_ui.inc Planes rows are seven fixed names (plane archive not read), mcam_saveops.c:7 (user's other parts as
a source), mcamui.c wireframe header (typed X / Y points removed), cam2d_ops.c header (arcs as chords), cam2d_area.c
header (Core Roughing old generator), toollib.h toollib_save_tooldb (DatabaseDefaults copy), nc_post.c splits tagged
arcs at up to 180 degrees while cam_write_gcode / the review say 120; dead pocket code after the returns in
cam2d.c gen_pocket.
