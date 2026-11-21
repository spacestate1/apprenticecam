# Qt port of the apprenticeCAM UI (started 2026-10-21)

Decision (user, 2026-10-21): the UI moves from nuklear (viewer/mcamui.c + *_ui.inc) to **Qt 6 Widgets**, with
**SARibbon** (ribbon) and **Qt Advanced Docking System** (docking managers), both LGPL-2.1, built from source by our
CMake. The nuklear app is **replaced directly**: once a phase lands, the matching nuklear code is no longer maintained.

Rules that still apply: no improvisation (every label / behaviour / colour cited to decomp or resources - the existing
citations move with the code), nothing Mastercam-derived committed (icons / tips from the user's icons.pack at runtime),
regressions green after every phase (fileop 0/189 `~/mastercam-re/regress/fileop_all.sh`, post 0/16 `postreg.sh`),
offscreen testing only (never the user's display), memory caps (formats: memory-limits).

**Current state: see "STATUS / HANDOFF (2026-10-23)" at the end of this file.**

## Layout
- `viewer/` C core, unchanged API where possible: mcam_load / mcam_write / mcam_saveops, cam2d*, mcce_dyn, nc_post,
  nci_reverse, toollib / toolprofile / toolwiz / part_tools / feedspeed, solid25, backplot, verify, scene.c (GL 3.3 core
  drawing), paths.h / platform.h. cam_cli / tool_cli keep building from here.
- `viewer/core_ui/` (new, C): the behaviour now inside mcamui.c, moved out without nuklear: document state, selection,
  picking / AutoCursor snap, wireframe commands (geo_*), chaining state machine, Extrude command (ex_*), overlay /
  highlight building (build_overlay, ov_entity), op dialog model (pending op, page tree rules), Tool page model, config
  reading, undo. Each piece keeps its decomp citations.
- `viewer-qt/` (new, C++17, CMake): main window, SARibbon from ui/layout.txt, ADS docks (Toolpaths / Solids / Planes /
  Levels / Recent Functions + function panels), QOpenGLWidget hosting scene.c, status bar, dialogs as QDialog laid out
  from the decoded templates (dialog units -> px), offscreen test hooks equivalent to MCAMUI_SCRIPT / MCAMUI_EVENTS /
  --shot (QOffscreenSurface / QWidget::grab).
- `viewer-qt/third_party/` SARibbon, Qt-Advanced-Docking-System (fetched by CMake FetchContent at a pinned commit).

## Phases
| # | Phase | Status |
|---|---|---|
| 0 | Plan, deps check (Qt 6.11.2, cmake), memory note | DONE 2026-10-21 |
| 1 | `viewer-qt` skeleton: CMake + FetchContent (pinned), main window, SARibbon tabs/groups/buttons from ui/layout.txt + icons.pack + cmd_labels.tsv, ADS dock with the Toolpaths manager tree (read-only), QOpenGLWidget drawing a loaded part via scene.c (pan / zoom / rotate as decoded), status bar, File open / save via the C core, risk-accept modal, offscreen `--shot` + script hook | DONE 2026-10-21 (see "Phase 1" below) |
| 2 | core_ui extraction 1: document, selection, picking, AutoCursor, highlight / overlay (stripes), undo; Qt view uses it | DONE 2026-10-21 (see "Phase 2" below) |
| 3 | Wireframe commands + function panels (geopanel) in ADS | DONE 2026-10-21 (see "Phase 3" below) |
| 4 | Chaining dialogs (6464 / 1058 / 405 / 2300) + chaining state machine | DONE 2026-10-21 (see "Phase 4" below) |
| 5 | Solids: Extrude command + panel, Solids manager | DONE 2026-10-21 (see "Phase 5" below) |
| 6 | Toolpath dialog (6544 frame, page tree, all pages, Tool page 25524 / 6556, Tool List 6410) | DONE 2026-10-21 (see "Phase 6" below) |
| 7 | Tool Manager 1099, Define Tool wizard, Tool filter, reports | DONE 2026-10-21 (see "Phase 7" below) |
| 8 | Backplot, Verify, Post, analyze, levels / planes / colours, backstage, remaining dialogs | DONE 2026-10-21 (see "Phase 8" below) |
| 9 | Remove nuklear (mcamui.c, *_ui.inc, vendor/nuklear.h), Windows build (MinGW / MSVC + windeployqt) | nuklear removal DONE 2026-10-22 (see "Phase 9" below); Windows build OPEN |

## Phase 1 (done 2026-10-21)

### Third-party versions (pinned in viewer-qt/CMakeLists.txt, FetchContent, built as shared libraries in viewer-qt/build/_deps)
| library | upstream | tag | commit |
|---|---|---|---|
| SARibbon (SARibbonBar) | https://github.com/czyt1988/SARibbon | v2.9.5 | a21d30c2a8495da92c7db3adcdc124699b0f2a60 |
| Qt Advanced Docking System | https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System | v5.1.1 | 023ce95934fecfd4cf5c672c9c404fabe0f54923 |

Options: SARIBBON_BUILD_EXAMPLES / BUILD_EXAMPLES / BUILD_TESTS off, SARibbon without the frameless library, both shared
(LGPL-2.1: relinkable). System: Qt 6.11.2 (Widgets, OpenGL, OpenGLWidgets, Svg), GLEW, EGL, sqlite3, CMake >= 3.21.

### Build / run
```
cmake -S viewer-qt -B viewer-qt/build -DCMAKE_BUILD_TYPE=RelWithDebInfo     # first run clones the two libraries
cmake --build viewer-qt/build -j4                                            # (cap: ulimit -v 6000000)
viewer-qt/build/apprenticecam [PART.mcam|.mcx-9] [--ui DIR] [--tab NAME]     # ui/ (layout.txt, cmd_labels.tsv, icon_map.tsv) is copied next to it
# offscreen test (no display used; the view renders through a surfaceless EGL context):
env -u DISPLAY -u WAYLAND_DISPLAY QT_QPA_PLATFORM=offscreen MCAMUI_I_ACCEPT_THE_RISK=1 \
  MCAMUI_SCRIPT="open:<user-parts>/BRACKET4.mcx-9;view:7;shade:1" \
  MCAMUI_EVENTS="1:click:741,47;3:wheel:3,1100,560;5:mdown:900,560;6:move:960,520;8:mup:960,520" \
  viewer-qt/build/apprenticecam --shot out.png --size 1600x1000
# the nuklear reference shot, also without the display: SDL's offscreen video driver
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen MCAMUI_I_ACCEPT_THE_RISK=1 MCAMUI_SCRIPT="..." viewer/mcamui --shot out.bmp --size 1600x1000
```

### Layout of viewer-qt/
| file | what | moved from |
|---|---|---|
| CMakeLists.txt | Qt + FetchContent; `mcamcore` = static library of the Makefile's mcamui sources minus mcamui.c (rpx scene mcam_load part_tools xt_mesh cam2d mcce_dyn toollib toolprofile toolwiz feedspeed backplot mcam_write mcam_saveops geo2d solid25 cam_fileop nc_post nci_reverse area_mill verify + offset.cpp + vendor/clipper6), same warning flags; copies viewer/ui next to the binary | viewer/Makefile |
| src/core_bridge.c/.h | C side (paths.h / platform.h are C-only inline code): config dir / paths.conf / site files, the user's mcamxm.config items (view factors 4026-4029, 33002/06/07/08/09/10, colours 5001/5007/5009/5027/5032/5045/5046/5052/8027, grid 23002-23007, MRU 4023), recent.txt, open / new / close / save-as-part, the Toolpaths manager texts (#include viewer/optree_text.inc, tm_describe), level counts, SystemPlanes names | mcamui.c load_view_factors, screen_colors_load, recent_*, open_part, export_mcam; toolmgr_ui.inc tm_describe; levels_ui.inc |
| src/uilayout.* | layout.txt parser (TAB / GROUP / BTN / GAL / SUB / GITEM / FILE / TBAR, same field rules), cmd_labels.tsv | mcamui.c load_ui, cmd_label |
| src/icons.* | icons.pack (MCUIICN1) / MCAMUI_ICON_PACK / MCAMUI_ICONS, icon_map.tsv, #file: / #strip: / #strip14:, the letter-tile fallback, greyed = alpha 80 | mcamui.c ipack_find, icon_tex, icon_resolve, draw_icon |
| src/mainwindow.* | SARibbon (tabs, context tabs per machine, panels, large / small / Group stacks, split buttons, galleries), QAT from CONFIG/Mastercam.Workspace, command dispatch, ADS docks, status bar, file actions | mcamui.c run_local_x, cmd_ok, invoke, add_recent, run_mc_function, draw_ribbon, draw_tabs, draw_qat, draw_status, do_action |
| src/managers.* | Toolpaths (toolbars 800 / 804 + tree), Solids, Planes, Levels, Recent Functions | mcamui.c draw_manager / tree_row, solidsmgr_ui.inc, levels_ui.inc |
| src/viewwidget.* | GLView (QOpenGLWidget 3.3 core, MSAA 4) / EglView (surfaceless EGL + FBO, used when Qt has no GL: offscreen); ViewInput = the view mouse / keys / animation timer | mcamui.c main loop mouse handling, view_key, view_push |
| src/backstage.* | File tab placeholder: the 15 BS_ITEMS, New / Open (recent documents + Browse) / Save / Save As live | mcamui.c BS_ITEMS, draw_backstage |
| src/testhooks.* | MCAMUI_SCRIPT steps, MCAMUI_EVENTS, --shot (QWidget::grab -> PNG / BMP by extension) | mcamui.c run_script, ev_push, dump_state |
| src/main.cpp | arguments (--shot / --size / --tab / --ui / PART), the risk agreement (MCAMUI_I_ACCEPT_THE_RISK=1 skips), start-up order | mcamui.c main, draw_risk_agreement |

### What works
- Ribbon: every TAB / GROUP / BTN / SUB / GAL of layout.txt; context tabs only for the current machine (Machine tab
  Mill / Lathe / Router / Wire / Mill-Turn switch them); start on the machine's toolpath tab, File > New -> Wireframe.
  Enabled exactly where mcamui.c run_local_x has a ported behaviour (views Top / Front / Right / Isometric, Fit, Shaded /
  Wireframe / Translucency, the five managers, Show Grid / Snap to Grid, machine type, Select all operations, Toggle
  display, Hide rapid, New / Open / Save / Save As / Close / Exit, "MCAM part"); everything else greyed. Icons from
  icons.pack via icon_map.tsv; tiles without it. (The inherited label-matching bug - View > Save = OnGViewSave saved
  the part - is fixed in phase 2 in both apps.)
- QAT: the user's Mastercam.Workspace commands, tooltips = cmd_labels.tsv, the ported FT functions live
  (run_mc_function subset: views incl. Back / Bottom / Left with the view history, Fit, Unzoom, shading toggles,
  axes, grid, managers, File New / Open / Save / Save As / Exit).
- Managers (ADS, tabs at the bottom): Toolpaths tree with optree_text.inc rows (machine group, Properties + 4 children,
  toolpath group, ops with Parameters / tool / Geometry / Toolpath rows, opmanres tree images), click selects an op
  (sel) or the group; the 800 / 804 toolbars (greyed unless ported); Solids (part solids), Planes (7 SystemPlanes,
  1/3 + 7 shared widths), Levels (used levels + entity counts, FUN_180247e90 widths, greyed form), Recent Functions.
- View: scene.c draws geometry, toolpaths, solids, gradient, grid, axes; middle drag rotates (free / turntable by
  33002, Ctrl / X / Y / Z axis modes) or pans with Shift (33009 swaps), wheel zooms about the cursor by 4026 (33010
  reverses), left press ends a wheel run, arrows / Alt+arrows / PgUp / PgDn / Home as FUN_1803222f0; view
  animations (33006 / 33007 / 33008) on a 15 ms timer; X / Y status panes with the grid snap rule.
- Status bar: X / Y / Z panes, 3D (greyed), CPLANE / TPLANE / WCS Top, the six display buttons (Wireframe / Shaded /
  Translucency live, Dimmed / No Hidden / Outline Shaded greyed).
- File: Open dialog with the decoded filters; parts load through scene.c load_file (a failed open keeps the part);
  Save / Save As write a Mastercam part through mcam_saveops.c (never over the opened part); recent.txt kept;
  backstage Open lists the recent documents. Risk agreement as mcamui (text verbatim).
- Tests: `--shot`, MCAMUI_SCRIPT steps open / new / newpart / close / exit / saveas (a part in phase 1; the project since phase 2) / view / shade / fit / selop /
  opexpand / mcfn (as mcamui) + tab / mgr / cmd (ours), MCAMUI_EVENTS click / rclick / move / down / up / mdown / mup /
  wheel / key / hold (as mcamui; F = tick).

### Stubbed / not ported yet (greyed or absent; phase in brackets)
- (done in phase 2: .mcamui projects, the unsaved-changes prompt, pivot picking + markers, selection, auto-highlight,
  the keymap, the graphics right-click menu, Ctrl / Shift in the tree - see "Phase 2"; still open from this list: the
  tree's right-click menu (phase 6), AutoCursor in the view (point entry, phase 3).)
- Wireframe commands + function panels (3), chaining (4), Extrude + Solids manager items (5), toolpath galleries
  (items greyed, no gallery_tips tooltips yet) and the op dialog (6), Tool Manager (7), Backplot / Verify / Post /
  Grid settings / analyze / backstage pages (Info, Print, Help, ...) (8).
- Cosmetic: SARibbon's Office 2013 theme instead of mcamui's hand-drawn ribbon; the manager dock is 385 px (mcamui 330,
  ours) so ADS's tab margins fit the five names.

### Screenshots (screenshots/, not committed)
2026-10-21_qt_phase1_bracket4_top.png / _bracket4_iso.png / _new.png / _events_view_tab_zoom_rotate.png /
_backstage.png / _optree_expanded.png, and the nuklear references 2026-10-21_qt_phase1_mcamui_ref_bracket4_top.png /
_bracket4_iso.png / _new.png (same scripts). Compared by eye: the graphics view has the same framing, colours, grid / axes and drawing as mcamui's (it is 55 px
narrower because of the wider dock); ribbon structure, enabled set, tree texts and status fields match.

## Phase 2 (done 2026-10-21): viewer/core_ui
The behaviour below moved out of viewer/mcamui.c into plain C (no nuklear / SDL / Qt) under `viewer/core_ui/`, with
its comments and decomp citations and with its mcamui.c names (so the citations in formats/*.md still find it). Both
UIs call it: viewer/Makefile builds the six files into mcamui (`-I.`; the Windows rule too), viewer-qt/CMakeLists.txt
adds them to `mcamcore`. What the moved code needs from a UI goes through `cui_hooks` (message box, the command state a
document change resets, solids cleared, ops changed, the overlay's chained / picked entities and extra lines).

| file | what | moved from mcamui.c |
|---|---|---|
| core_ui.h | the API: hooks, config items, document, features, undo records, selection / picking, AutoCursor, overlay; `SEL_APERTURE_PX`, `ACS_*`, `GK_*`, `RB_TPN` / `RB_HV`, `U_*`, `URec`, `ExFeat` / `FeatPack` / `ExGrp`, `EX_TOL` / `EX_NCTOL` / `EX_MINDIST` | the static declarations |
| cui_doc.c | `camops` / `ncamops`, `doc_path`, `doc_dirty`, `machine`, `cam_msg` (prompt line), `cui_message`; `ends_with_ci` / `is_project_file` / `is_part_file` / `file_exists`, `recent_load` / `recent_add`, `doc_clear_ops` (UI part = hook), `doc_write` / `doc_read` (PART / NEWPART / GEO / DEL / EXTRUDE+XCHAIN / MACHINE / TOOLS / OP / TREE / STALE / REVERSED lines), `open_part` (now returns 1 / 0; the caller leaves the backstage), `doc_close`, `cui_doc_save_to` + `cui_doc_save_target` (doc_save_to / doc_save without the file dialog and the after-save action), `remove_stored_op` | document section, remove_stored_op |
| cui_feat.c | the Extrude feature list the project stores and the bodies it rebuilds: `feats` / `sbodies` / `feat_body`, `ex_defname` / `ex_is_defname` / `ex_nvis`, `feat_dup` / `feat_free` / `feat_release`, `ex_chain_xy` / `ex_chain_normal` / `ex_wind` / `ex_pdist` / `ex_grouping` / `feat_groups` / `feat_apply`, `bodies_*`, `solids_rebuild` / `solids_clear`, `feat_push` / `feat_pop` / `feat_undo`, `feats_write` / `feats_read` (the Extrude command itself, ex_*, stays for phase 5) | Extrude section |
| cui_undo.c | `URec` stacks, config 16013 / 16014 depth, `ent_off` / `ent_on` (poly stash), `ent_hidden`, `undo_clear` / `undo_reset` / `undo_push`, `mod_begin` / `mod_del` / `mod_end` (+ `mod_from` / `mod_rec`), `undo_ents_added`, `camops_detach` / `camops_attach`, `undo_apply` (was `apply`; U_ENTS_* / U_OP_* / U_FEAT records), `do_undo` / `do_redo` | undo / edit section |
| cui_sel.c | `vp_x` / `vp_y` / `vp_w` / `vp_h`, `ent_sel` / `nselected`, `hover_ent`, `seg_dist`, `pick_entity` / `pick_point_under` (select_ent_univ / FUN_18020f4e0), `sel_grow` / `is_sel` / `sel_list` / `sel_clear`, new `sel_toggle` / `sel_all` / `sel_set_list` (the handle_click toggle, SelectAllMask, the sel: step), `ent_users` / `delete_selected_ents`, `hide_toggle` (OnHide), `seg_hits_rect` / `window_select` / `polygon_select` (now takes the polygon), `cui_autohl_active` (autohl_active's rule; the UI fills `CuiHlState`), `cui_hover_update`, `cui_cursor_world` (status-bar cursor + sel_grid_search), the pivot markers `PIV_BM0` / `PIV_BM1` + `cui_rotate_begin` / `cui_rotate_free_step`, `op_msel` / `msel_owner` / `op_selected` / `op_nselected` / `op_click(i, ctrl, shift)` (COpMgrTree::Selchanged), `KMP_DEFAULT` + `cui_kmap_load` (CONFIG/mastercam.kmp; the UI parses the key names), `rmb_drop` / `rmb_load` / `rmb_find` (CONFIG/default.mtb), `cui_config_item` / `cui_config_done` (5000 / 4036 / 4001 / 5033 / 5007 / 4056 / 16013 / 16014) | picking, selection, window / polygon select, autohl_active, op multi-selection, keymap, rmb menu, config items |
| cui_snap.c | `ac_snap` / `ACS_*` (dialog 779 flags), `ac_ang_step`, `geo_snap_kind`, Temporary Midpoints `ac_tmid` / `ac_tmid_clear` / `ac_tmid_capture(cursor)`, `xy` / `ent_ge`, `angfold_pi`, `geo_snap` (FUN_1804e1bb0 pass order), `geo_ang_snap(.., ctrl_down)` | AutoCursor section |
| cui_overlay.c | `overlay_dirty`, `mc_rgb`, `ov_pt`, `ln_stipple` / `stipple_px` / `ov_entity_pat` / `ov_entity` (FUN_180135ad0 stripes), `ov_fast_marker` (draw_fast_marker 0x180331460, split out of build_overlay), `build_overlay` (selection + hover + the UI's extra lines + the dash buffer) | build_overlay section |

mcamui.c keeps its UI: the chaining state (cchains: `ov_chained`), the wireframe picks / preview / drill points /
Extrude arrows (`ov_picked` / `ov_extra`), its message box, file dialog, doc_save wrapper, SDL key parsing of the
keymap, the nuklear right-click menu, pivot drawing. The mcamui script scenes (tools/tests/screenshots/scenes.txt plus
10 new selection / undo / snap / pivot / project / right-click scenes) give the same dumps and pixel-identical shots as
before the move, except the View tab (below) and two shots that show the user's recent files / the clock.

**Fixed in both apps (item 3):** the File commands are matched by label, and View > "Save" (FT OnGViewSave "Save
Graphics View", opmanres 6032) and "New" (NewWorksheet "Create New Viewsheet", 6144) carry the same labels - Save saved
the part / project, New started a new part. Now the labels only count for commands without an FT function (the
backstage FILE entries) or an OnFile* one (mcamui.c `rl_file_cmd`, MainWindow::localRun `rlFt_`); OnGViewSave and
NewWorksheet are greyed (NOT DECODED).

### Qt features now working (viewer-qt)
- Document: Open / sidecar / .mcamui projects through `open_part` (the Open dialog lists "apprenticeCAM Project
  (*.mcamui)" as mcamui's FDT_OPEN), MACHINE line -> the machine tab; Save = the project (or PART.mcamui beside the
  part), Save As = the dialog with mcamui's FDT_SAVE types (project, or "Mastercam 2017 Files" = save as a part);
  "MCAM part" keeps saving a Mastercam part. The unsaved-changes prompt (save_on_exit, mccoreres 7291 / caption 5175,
  Yes = Save As then the action, No = discard; default No) before New / Open / Close / Exit / the window's X. Title =
  the project's name, else the part's. Projects written by either app open in the other (checked: GEO / DEL lines,
  round trip byte-identical).
- Graphics view: left click (< 4 px) flips the entity under the cursor (select_ent_univ); hover auto-highlight with the
  FUN_180135ad0 stripes (config 4036 / 4001 / 5000, contrast pass + dash pass, the dash buffer for dashed styles);
  selection stripes (5033); middle-button rotation pivot = `pick_point_under` and the two pivot markers in the
  auto-highlight colour; status bar X / Y from `cui_cursor_world` (Z = construction depth 0, as mcamui); the prompt
  box (CPromptWnd, top-left) for cam_msg; Alt+F12 SbSelectRotationPosition (click = rotation centre, an entity's
  AutoCursor point; Esc cancels).
- Toolpaths tree: Ctrl / Shift multi-selection (`op_click`), the rows' selected images from `op_selected`.
- Keymap: CONFIG/mastercam.kmp (else the 2017 defaults) for every FT function the Qt UI has: views, Fit, Unzoom, F9,
  Alt+F9, Alt+G, Alt+S, Ctrl+T, Alt+T, Alt+Z / Alt+O / Alt+I / Alt+L, Ctrl+N / O / S, Shift+Ctrl+S, Alt+F4, Ctrl+A
  (SelectAllMask), Ctrl+Z / Ctrl+Y (UndoEvent / RedoEvent), F5 (delete_entities), Alt+E (OnHide), Alt+F12. Not while a
  text field has the keyboard, not on key repeat. Delete removes the selected entities.
- Undo / redo: Ctrl+Z / Ctrl+Y, the QAT's UndoEvent / RedoEvent (the user's Workspace), the right-click menu; records =
  deleted entities in this UI (the geo / op / feature records arrive with phases 3 / 6 / 5 - the hooks are in place).
- Right-click menu: default.mtb's "Right mouse button menu" with its one submenu level, texts from cmd_labels.tsv,
  unported functions greyed.
- Ribbon: "Delete Entities" live; OnGViewSave / NewWorksheet greyed.
- Test hooks: MCAMUI_SCRIPT open / new / close / exit go through the prompt (answer:save|discard), save, saveas:P (the
  project), export:P (a part), sel:LIST, ents, selops:LIST, key:KEYS (through the keymap), screen:X,Y[,Z], snapat:X,Y
  (both apps: prints the core_ui geo_snap result; in Qt it is also a TEST PROBE that draws the draw_fast_marker square,
  which the program itself draws only during point entry - phase 3); MCAMUI_EVENTS clickw:WX,WY; the dump line has
  doc / dirty / camops / sel / hover / undo / redo / prompt; --shot also draws open popup menus.

### Still stubbed / not ported (phase)
- Window selection for general (non-chaining) selection: not in mcamui and not decoded - only Window / Polygon
  chaining uses `window_select` / `polygon_select` (moved; the chaining that calls them is phase 4).
- AutoCursor glyphs / snap markers in the Qt view: mcamui draws them only during point entry of the wireframe
  commands (phase 3); `geo_snap`, `geo_ang_snap`, `ov_fast_marker`, the Temporary Midpoints capture are in core_ui.
- The right-click menu's mini toolbar (only Set From Entity is live in mcamui: wireframe attributes, phase 3);
  AutoCursor Settings dialog 779 (doAutocursorDlg, phase 3 with the commands); Ctrl+W / Ctrl+Q (mcamui's own, no
  source) left out; F1 zoom window, Ctrl+C / X / V (clipboard + Paste panel), Analyze F4, OnHelp keymap window,
  deleting an operation with Delete (phase 6), Esc closing dialogs that do not exist yet.

### Screenshots (screenshots/, not committed)
2026-10-21_qt_phase2_qt_hover_stripes.png / _qt_selection_stripes.png / _qt_click_select.png / _qt_pivot.png /
_qt_rightclick.png / _qt_tree_multisel.png / _qt_view_tab_save_greyed.png / _qt_snap_probe.png and the nuklear ones
_mcamui_hover_stripes.png / _mcamui_selection_stripes.png / _mcamui_click_select.png / _mcamui_pivot.png /
_mcamui_rightclick.png / _mcamui_snap_end.png / _mcamui_snap_mid.png / _mcamui_view_tab_save_greyed.png.

## Phase 3 (done 2026-10-21): wireframe commands + function panels
The wireframe command engine and the model of its function panels moved out of viewer/mcamui.c + viewer/geopanel_ui.inc
into `viewer/core_ui/cui_geo.c` (plain C, names and decomp citations kept); mcamui.c / geopanel_ui.inc keep only the
nuklear widgets and call it, the Qt app builds its panels from the same body. Makefile (mcamui + the Windows WCUI list)
and viewer-qt/CMakeLists.txt (`mcamcore`) build it. Backup of the tree before phase 3:
~/mastercam-re/backups/pre_qt_phase3_2026-10-21.tgz.

| moved to core_ui | what | moved from |
|---|---|---|
| core_ui.h "wireframe commands" | `G_*` command enum, `TR_*`, `GeoParams` / `gps[]`, the geo_* API, `CuiPanelOps`, hooks `new_part` / `geo_started` | mcamui.c static declarations |
| cui_geo.c state machine | `gcmd` / `geo_unsupported`, `geo_find` / `geo_start` / `geo_steps` / `geo_prompt` / `geo_need` / `geo_take` / `geo_point` / `geo_run` / `geo_reset_picks` / `geo_end` / `geo_cancel`, `geo_rb_type`, `geo_add` / `geo_line` | mcamui.c "wireframe" section |
| cui_geo.c live results | `geo_live_kind` (LV_* with every citation), `geo_live_start` / `_commit` / `_drop` / `_update` / `_valid`, `geo_param_hash`, `GeoIn` | same |
| cui_geo.c geometry | `geo_exec` (all commands), `geo_apply` (selection commands), `add_ge*`, `replace_ent`, `cuts_on`, `split_at`, `trim_ent`, `corner_chain`, `offset_chain`, `loc` / `loc_arc` | mcamui.c "wireframe commands" section |
| cui_geo.c panel model | `PKEYS` / `pk_*` (panel_keys.tsv), `geo_defaults`, `geo_panel_sync`, `pk_note`, `geo_panel_body` (the BAML order / texts, walked through `CuiPanelOps`), `geo_panel_button` (OK 0 / Apply 1 / Cancel 2) | geopanel_ui.inc, geo_panel_content |
| cui_geo.c view glue | `geo_ov_picked`, `geo_ov_extra` (picked crosses, rubber band, draw_fast_marker), `geo_click` (handle_click's drawing branch: geo_snap + geo_ang_snap), `geo_cursor_update` (the main loop's drawing cursor), `geo_tmid_key` (N), `set_main_from_entity` | mcamui.c handle_click, ov_extra, main loop, run_mc_function |
| cui_geo.c script steps | `cui_geo_script`: draw / gset / geoline / geocmd / gpdist / geodone / geoesc / geoapply / geolive / gpset / geopick / geochamf / geoparam (same output in both apps) | mcamui.c run_script |
| cui_sel.c `cui_config_item` | config 1001 chain_tol (`cfg_chain_tol`), 5026 live colour (`cfg_live_color`), 10002 point style (`cfg_pt_style`) | mcamui.c screen_colors_load |

mcamui.c now: `geo_click` in handle_click, `geo_ov_picked` / `geo_ov_extra` in its overlay hooks, `geo_cursor_update`
on mouse moves, `geo_tmid_key` on N, `cui_geo_script` in run_script, hooks `new_part` = do_action(ACT_NEW) and
`geo_started` = cam_mode / show_params off + dock_show_panel; geopanel_ui.inc = the nuklear widgets + `NK_PANEL`
(CuiPanelOps) + geo_panel_content (header, buttons). Test-only addition in both apps: MCAMUI_EVENTS `movew` / `downw` /
`upw` (world X,Y like clickw).

Fixed on the way (both apps, shared code): (1) scene.c - undoing the last visible entities crashed the GL driver
(glMultiDrawArrays over zero-count polylines with an empty vertex buffer): zero-count polylines are skipped. (2) the
panel body computed the Diameter field's value before the Radius field, so a radius typed into the panel (Circle Center
Point, Arc Endpoints, Arc Polar, Arc Tangent, Arc Polar Endpoints) was undone by the stale diameter in the same frame;
the diameter is now taken after the radius field.

### Qt features now working (viewer-qt)
- Ribbon: every Wireframe command mcamui has is enabled (geo_find; a command of another DLL with a wireframe name, e.g.
  Toolpaths > Trim, is not: rl_nogeo from the layout's dll field); the rest stay greyed (geo_unsupported: splines,
  surfaces, letters, ...). A click starts the command (geo_start; nothing open = File > New first).
- Function panel (src/geopanel.*): an ADS dock tab between Levels and Recent Functions titled with the command name
  (mcamui's dock bar tab), brought to the front on start, closed at the end (the manager tab shows again); clicking a
  manager tab shows the manager, the panel's tab brings it back. Contents = core_ui geo_panel_body (expanders with
  arrows, labels, radios, check boxes, EvaluatorBox line edits with the hard-lock check box, the greyed Reselect /
  point toggle buttons, the TangencyModes combo with "Arc dynamic" greyed); OK / Apply / Cancel / Help (greyed) and the
  "Basic" tab. Every change goes through the body walk (so Diameter -> Radius, the trim-mode pick reset ... run as in
  mcamui), then geo_panel_sync + pk_note + geo_live_update (live results rebuild), then a refresh of values and enabled
  states. Number display as mcamui (2 decimals enabled, %.4f greyed); typed values clamped like nk_property.
- View: point entry with the AutoCursor (geo_click / geo_cursor_update: geo_snap with the rubber-band anchor, angular
  snap with Ctrl suppressing), the draw_fast_marker square on an entity snap, the picked-point crosses, the rubber band
  (Line / Arc 3 Points / Rectangle / Circle) and the grid cross exactly as mcamui (shared geo_ov_extra); entity picks;
  picks take the PRESS point (OnLButtonDown FUN_1801ca2c0 posts 0x8030, decomp/mastercam_viewmouse.c), other clicks
  keep the < 4 px test; Esc = Cancel (geo_cancel), N = Temporary Midpoints capture, hover highlight follows the
  command's pick kind (CuiHlState geo_cmd / geo_need); no right-click menu while a command runs (mcamui).
- Right-click mini toolbar above the default.mtb menu (FUN_1801c9660 list, IDR_BCGP_IMAGE_CATEGORY_SMALL images, combos
  as empty greyed boxes, tooltips); only Set From Entity (set_main_from_entity) live, as mcamui.
- AutoCursor Settings, opmanres dialog 779 (src/acsettings.*): template positions in dialog units mapped as Windows'
  MapDialogRect with the dialog's font (average char width / 4, height / 8); OK clamps the angle step [1e-6, 90];
  Fast Point / power keys / Help greyed. From the keymap / QAT (doAutocursorDlg) and `show:acset`.
- Undo / redo of the commands' steps (U_ENTS_ADD / U_ENTS_MOD through core_ui undo; an undo ends a live step).
- Test hooks: every mcamui geo script step (cui_geo_script), `show:acset`, MCAMUI_EVENTS movew / downw / upw; ours:
  `panel` (dump the panel's controls), `panelset:K=V` / `panellock:K=0|1` (change control K through its widget).

### Tests (2026-10-21)
- 55 wireframe scenes (tools/tests/wf_scenes.txt, run by tools/tests/wf_compare.sh: every command - points, lines incl. multi-line / locked length, circles,
  arcs, Arc Tangent modes 0 / 6 + its candidates, rectangle / shapes / polygon / ellipse / helix / spiral / bounding
  box, trim / break / join / Trim Many / Break at Points, fillet / chamfer entities + chains, offset / offset chains,
  project, close arc, break circles, undo / redo, live rebuilds and Esc): the entity dumps and live lists of mcamui
  and the Qt app are identical (0 / 55 differ); mcamui before / after the move: identical dumps and pixel-identical
  shots. Event-driven runs (clicks, press-drag-release, snap to an endpoint, Esc, N capture) give the same snap kinds
  and entity counts in both apps.
- mcamui scenes.txt (46): identical dumps; shots pixel-identical except the clock digit (tool_list_report) and the
  user's recent-files list (backstage_open).
- fileop 0/189, postreg 0/16.

### Still stubbed / not ported (phase)
- (resolved 2026-10-21, "Wireframe chain commands -> Mastercam chaining" below) Chaining for the chain commands (Fillet /
  Chamfer Chains, Offset Chains): they now run the Chaining dialog (6464 / 1058) with several chains in both apps.
- The ADS bottom tab bar scrolls when the panel tab is added (dock 385 px); mcamui's tab strip clips too.
- Per-snap cursor glyphs (opmanres cursors 261-714): not ported in either app. The graphics cross cursor (gafres
  CURSOR 4, mcamui) is not in the Qt view yet - phase 8 cosmetic.
- NOT DECODED (as in mcamui, greyed / left out): the EvaluatorBox number format, the FunctionPanel caption source,
  the Help topics, the WPF button images, Reselect / point toggle buttons, the mini toolbar's other commands, Fast
  Point mode / power keys, the original's rubber-band drawing (the crosses / colours are this program's).

### Screenshots (screenshots/, not committed)
2026-10-21_qt_phase3_qt_*.png and the mcamui references 2026-10-21_qt_phase3_mcamui_*.png (same scripts):
line_rubber, circle_rubber, rect_rubber, snap_end, press_point, chamfer_panel, fillet_panel, bolt_live,
arc_polar_panel, trim_panel, acset, rightclick_mini; Qt only: chamfer_width (Width style picked in the panel).
Compared by eye: panel structure / texts / enabled states, live results, rubber band and snap marker match.

## Phase 4 (done 2026-10-21): chaining
The chaining state machine (mccore CChainDlg / run_chain_dlg / chain_it) and the model of its dialogs moved out of
viewer/mcamui.c + viewer/chaindlg_ui.inc into `viewer/core_ui/cui_chain.c` (plain C, mcamui names and decomp citations
kept; formats/chaining.md has the rules); mcamui.c / chaindlg_ui.inc keep only the nuklear drawing and call it, the Qt app
lays the same templates out with Qt widgets. Makefile (mcamui + the Windows WCUI list) and viewer-qt/CMakeLists.txt
(`mcamcore`) build it. Backup of the tree before phase 4: ~/mastercam-re/backups/pre_qt_phase4_2026-10-21.tgz.

| moved to core_ui | what | moved from |
|---|---|---|
| core_ui.h "chaining" | `cam_mode`, `cchains` / `ncchains`, `choles` / `ncholes`, `show_params` (the op dialog flag, phase 6 owns it), `cam_variant`, `ex_mode` / `ex_phase` (the Extrude command's flags, phase 5 owns them), every chaining variable (`chain_single`, `partial_*`, `win_*`, `dyn_*`, `chain_wait`, `last_chains`, `chain_dlg_exp`, `chain_last_mode`, the `cfg_*` chaining items, `chain_prompt`, `poly_*`, `last_clicks`), `ChainArrow`, `ChCtl`, `ChainOpts`; hooks `view_port` / `chain_ok` / `chain_ask_unselect_all` | mcamui.c static declarations |
| cui_chain.c state machine | `cam_clear_selection`, `chain_prompt_upd`, `chain_dlg_init` (FUN_1801ead40), `chain_dlg_done`, `gallery_pick` (the toolpath prompts), `chain_begin_extrude` (ex_chaining's chain part), `chain_keep_last` (open_params' Last copy), `chain_cancel`, `chain_dlg_shown` | mcamui.c 2D CAM section, gallery_pick, ex_chaining, open_params |
| cui_chain.c picks | `polygon_end` / `vector_end` / `window_chains` (FUN_1801dfae0 / FUN_1801df560), `chain_window_drag` / `chain_window_done` (the window drag of the main loop), `chain_sketching` / `chain_sketch_end` (Enter), `chain_press_pick` (geo_pick_now's chaining part), `chain_click_pre` (handle_click: Area, Polygon / Vector points, window start point, Point mode), `chain_click_ent` (handle_click: drill points, Chain / Single / Partial / Wait / branch / Dynamic picks), `ov_chained`, `chain_ov_holes` (ov_extra's drill crosses), `chain_arrows` (draw_cam "chainarrows" geometry + colours) | mcamui.c handle_click, main loop, ov_extra, draw_cam |
| cui_chain.c dialog model | `CH_1058` / `CH_6464` / `chain_dlg_template`, `CH_WIN_MODES`, `chain_act` (every handler), `chain_unselect_all`, `chain_ctl_live` (FUN_1801f00d0), `chain_ctl_on`, `chain_px_world`; Options 405: `chain_opts_init` (FUN_1801f9a20) / `chain_opts_exclusive` (FUN_1801f98f0 / FUN_1801f9230) / `chain_opts_ok` (FUN_1801f9680), `CHO_SORT` / `CHO_SYNC`; Drill Point Selection 2300: `drill_unselect` / `drill_unselect_all` / `drill_ok`, `chain_points_dlg` | chaindlg_ui.inc |
| cui_chain.c config / tests | `cui_chain_config_item` (6000-6026, 1000, 1002; called by cui_config_item, so the Qt app reads them too), `chain_dump`, `cui_chain_script`: chainmode / chainopt / chainopts / chainact / chaindyn / chains (as mcamui) + ours chaindump / chainyes / chainwinmode:1..5 / drillun / drillunall / drillok | mcamui.c screen_colors_load, run_script |

mcamui.c now: `chain_click_pre` / `chain_click_ent` in handle_click, `chain_window_drag` / `chain_window_done` /
`chain_sketching` / `chain_press_pick` / `chain_cancel` in the main loop, `chain_arrows` drawn with nuklear,
`chain_ov_holes` in ov_extra, `chain_keep_last` in open_params, hooks `view_port` = vp_main, `chain_ok` = ex_chains_done or
open_params, `chain_ask_unselect_all` = yn_act; chaindlg_ui.inc = the nuklear widgets over the core templates (+
`ui_chain_act`: the Extrude log line, then chain_act). cui_sel.c's polygon_select statics were renamed (sp_x / sp_y /
sp_n) so the chaining's poly_x / poly_y / poly_n keep their names. core_ui.h now wraps the core headers in extern "C"
(cam2d.h has no C++ guard; the Qt app calls cam_type_from_label).

### Qt features now working (viewer-qt)
- Toolpath galleries: the items this program generates (cam_type_from_label, as mcamui) are enabled with their normal
  image; a click = gallery_pick (the op's prompt, CChainDlg's mode / C-plane / Wait defaults) + Recent Functions;
  Recent Functions re-runs a gallery item (run_local else gallery_pick).
- src/chaindlg.*: **Chaining** (6464 collapsed / 1058 expanded) as a tool window with a drawn title strip: UIVertDialog's
  icon (uictrls ICO 7021 / 7022, tooltip "Expand dialog" / "Contract dialog", a click = 358 and the dialog is re-made from
  the other template), the close box (= cancel_chaining); the bitmap buttons (mccoreres BMP via icons.pack, faded when not
  live, mcamui's fill / border colours for pressed / hovered), the Window / Polygon combo, Wait, C-plane / 3D, group
  boxes Start / End; enabling = chain_ctl_live, pressed = chain_ctl_on; every control -> chain_act. **Drill Point
  Selection** (2300) for the point ops (Unselect / Unselect all / OK live, the rest greyed as mcamui). **Chaining
  Options** (405) from Options (window-modal QDialog; OK / Cancel through chain_opts_*). Dialog units: the template font's
  base units 6 x 13 (= mcamui BS_DX / BS_DY) and its 11 px size (the measured Noto line height made the 428-unit
  dialog taller than the window). Placed at the top right of the graphics area (mcamui: W - width - 30); Options to its left.
- View: chaining picks take the press point (pressPick: geo or chain_press_pick); handle_click's order (rotation centre,
  chain_click_pre, the drawing command, pick_entity, chain_click_ent, selection); Window mode drags the selection
  rectangle (1.5 px rgb(20,70,160), mcamui's), the release = chain_window_done (then the start point pick, 4235);
  Polygon / Vector points with the band to the cursor, Enter or a double click ends them (last_clicks); the start /
  end arrows (chain_arrows, flat, as mcamui) and the drill crosses; chained entities in the select colour and left out of
  the hover highlight (ov_chained); the auto-highlight rule gets cam_chaining / ex_mode / ex_phase.
- Keys: Esc = the op dialog state off, else chain_cancel (mcamui order after the wireframe command); Enter ends a
  polygon / vector; the keys also work while a chaining tool window is the active window.
- Hooks: clear_ops (doc_clear_ops: chaining ends), geo_started (a wireframe command ends chaining, as mcamui), view_port,
  chain_ask_unselect_all (QMessageBox "Unselect All Chains?" / "Are you sure?", Yes default; scripted runs print and
  wait for `chainyes`).
- Test hooks: every mcamui chaining step (chainmode / chainopt / chainopts / chainact / chainat / chaindyn / chains),
  ours chaindump / chainyes / chainwinmode / drillun / drillunall / drillok (both apps), Qt-only `chainclick:ID` (a
  control through its widget) and `chaindlg` (dump the dialogs' controls); `MCAMUI_CHAINDUMP=1` adds chain_dump to every
  dump line (both apps).

### Stubbed / not ported (phase)
- The chaining's OK (cui_hooks.chain_ok): mcamui opens the op dialog (open_params) - Qt keeps the chains for Last
  (chain_keep_last) and sets show_params with no dialog yet (phase 6: `MainWindow::chainOk` is where open_params goes);
  Esc leaves that state like mcamui's Esc on the op dialog. The Extrude command's chaining (phase 5): call
  `chain_begin_extrude()` after setting ex_mode (the dialog / picks / arrows follow chain_dlg_shown and ex_mode /
  ex_phase already), fill the `ex_mode` branches of `MainWindow::chainOk` (ex_chains_done) and `chainClose`
  (ex_escape), and route `ex_phase` clicks before chain_click_pre in viewClick (mcamui order).
- (resolved 2026-10-21, see "Wireframe chain commands -> Mastercam chaining" after phase 5) Wireframe chain commands
  (Fillet / Chamfer Chains, Offset Chains) used mcamui's single cam_chain pick; both apps now run chain_it's Chaining
  dialog with several chains (user decision "make it like master cam").
- Toolpaths tree right-click "op" menu (mcamui CM_OP -> gallery_pick): phase 6 with the tree menu. The `gen:` script step
  (gallery + chains + generate) needs the op dialog (phase 6).
- As mcamui (NOT DECODED / NOT PORTED, greyed): Solids 351, Chain Feature 9990 / Options 9991, Help; the Options
  dialog's Entity / Color / Level mask dialogs; the drill dialog's automatic / pattern parts; the manual button's image.
- Cosmetic: group boxes are Qt's (title above the frame; mcamui draws it on the border); the shaded GrArrow cone /
  cylinder are drawn flat in both apps.

### Tests (2026-10-21)
- `tools/tests/chain_compare.sh OUTDIR` runs `tools/tests/chain_scenes.txt` (40 scenes: Chain / Single / Partial /
  Partial + Wait / End chain / branch + extend / Reverse / Start / End Back / Forward / Dynamic on closed and open chains /
  Unselect / Area / Area hole / Point / Last / OK with sorting / C-plane / 3D / mode switches / expand / Drill points by
  script; event-driven: Window (Inside, Intersect, open chain), Polygon + Enter, Vector + Enter, clicks, press-drag-
  release pick, Area, Point mode with AutoCursor snaps, drill pick, Esc) in both apps with MCAMUI_CHAINDUMP=1 and
  compares the chain dumps (entity order + r, start / end points, cuts, flags, holes): **0/40 differ**. Note for new
  scenes: avoid picks at an entity's exact midpoint (t = 0.5 decides the direction; the two views differ by float
  noise) and, for mcamui event scenes, points under its Chaining window (x > ~1140 px at 1400 x 900).
- mcamui before / after the move: the original 35 chain scenes identical (dumps + pixel-identical shots); scenes.txt 1/46
  (tool_list_report: the clock, as before).
- wf_compare 0/55; fileop 0/189 (a first run showed NAKTO-BRAKE-BRACKET_3 with a doubled "FAIL" line - that op is NOT DECODED and
  always FAILs; cam_cli does not link core_ui and was not rebuilt; a clean rerun and the op alone give the base); postreg 0/16.

### Screenshots (screenshots/, not committed)
2026-10-21_qt_phase4_qt_*.png and the mcamui references 2026-10-21_qt_phase4_mcamui_*.png (same scripts): chaining,
chaining_expanded, chaining_options, drill_points, window_drag (rubber band), window_chains, polygon_band, partial,
point_mode; Qt only: toolpaths_gallery (the enabled 2D items). Compared by eye: dialog structure / images / enabled and
pressed states, arrows and their colours, bands match; the Qt view is zoomed in further by the scripts' `fit` (the
Qt view's size at script time - not a chaining difference).

### Notes for phase 5 (Extrude / Solids)
- Chaining for Extrude is ready in core_ui: ex_mode / ex_phase are core_ui variables now; `chain_begin_extrude()` is the
  chain part of mcamui's ex_chaining; OK reaches `cui_hooks.chain_ok` (mcamui: ex_chains_done); the dialog's close box
  / Esc: mcamui calls ex_escape (fill `MainWindow::chainClose` and the Esc branch); the Extrude panel's graphics events
  (ex_click) come before chain_click_pre; `exchain:` / `exchainok` script steps are still mcamui-only (they use cam_chain
  directly - move them with ex_*). The Qt left dock width rule (mcamui left_dock_w: ex_mode && ex_phase) is not needed in
  ADS but the panel tab should follow ex_phase as GeoPanel follows geo_cmd.

## Phase 5 (done 2026-10-21): Solids - Extrude command + panel, Solids manager
The Extrude command (ex_*) and the Solids manager's model moved out of viewer/mcamui.c + viewer/solidsmgr_ui.inc into
`viewer/core_ui/cui_extrude.c` (plain C, mcamui names and decomp citations kept; formats/extrude.md is the spec); mcamui.c
keeps the nuklear panel (draw_extrude_panel / ex_page_*), solidsmgr_ui.inc the nuklear tree and menu, both now over the
core model. Makefile (mcamui + the Windows WCUI list) and viewer-qt/CMakeLists.txt (`mcamcore`) build it. Backup of the
tree before phase 5: ~/mastercam-re/backups/pre_qt_phase5_2026-10-21.tgz.

| moved to core_ui | what | moved from |
|---|---|---|
| core_ui.h "Extrude" | session globals `ex_sess` / `ex_sess_single` / `ex_auto_op` / `ex_min_dist` / `ex_autoprev`, the command state `ex_p`, `ex_name`, `ex_single`, `ex_prev`, `ex_cdir` / `ex_cid`, `ex_ninit`, `ex_thin_auto`, `ex_panel`, `ex_edit_idx`, `ex_tab`, `ex_chsel`, `ex_pick` / `ex_pick_p0`, `ex_prev_req` / hashes, `ex_vec_txt` / `ex_vec_bad`, the drag state, `ex_exp`; `ExArrow`, `EXV_*`, `ExEn`, `SmItem`, `SM_*`; hooks `msg_open` / `msg_close` | mcamui.c static declarations |
| cui_extrude.c command | `ex_sld`, `ex_log`, `ex_report`, `ex_prompt_panel`, `ex_chaining`, `ex_start`, `ex_edit`, `ex_unchanged`, `ex_force`, `ex_setparams`, `ex_set_type`, `ex_set_target`, `ex_chains_done`, `ex_below_min`, `ex_escape`, `ex_end`, `ex_cancel`, `ex_cur_feat`, `ex_preview`, `ex_accept`, `feat_delete_last`, `ex_any_sel` / `ex_remove_sel` / `ex_reverse_sel` / `ex_add_chains`, `ex_set_vector`, `ex_autocursor`, `ex_pick_body`, `ex_click` (now takes the UI's world point) | mcamui.c "solids: Extrude" |
| cui_extrude.c arrows | `ex_mid_tan`, `ex_arrow_list`, `ex_seg_dist`, `ex_arrow_hit`, `ex_drag_start` / `_move` / `_end`, `ex_ov_arrows` (the overlay lines) | mcamui.c Extrude section, ov_extra |
| cui_extrude.c panel model | `ex_validate`, `ex_panel_frame` (draw_extrude_panel's ex_force + ex_preview + arrow check), new `ex_enables` (the IsEnabled bindings that were inline in ex_page_*), `ex_btn_select_target` / `ex_btn_autocursor` / `ex_btn_select_vector`, `ex_set_normal` / `ex_set_cplane` / `ex_set_through_all` / `ex_set_check`, `ex_vec_commit` / `ex_vec_show`, `ex_chain_label` / `ex_chain_click` | mcamui.c ex_page_basic / ex_page_advanced / draw_extrude_panel |
| cui_extrude.c Solids manager | `SLD_OPNAME`, `SLD_OPBMP`, `sm_body_open`, `sm_sel_kind` / `sm_sel_idx`, `sld_optype`, `sm_init`, `sm_feat_text`, `sm_feat_icon`, `SM_SOLIDS` / `SM_PREP`, `sm_menu` (the FUN_180230a10 item list per row kind), `sm_menu_act` | solidsmgr_ui.inc |
| cui_extrude.c tests | `cui_extrude_script`: extrude / exset (as mcamui) + ours exset:dragtow=X,Y,Z, smmenu:K,F, smact:LABEL[,F], smrows, exdump; `ex_show_step` (show:extrude / exedit:N / exchain:E / exchainok); `ex_dump` (MCAMUI_EXDUMP=1, both apps) | mcamui.c run_script |

### Changed in both apps (Mastercam rule over mcamui, user rule 2026-10-21; all in core_ui, flagged)
1. OK / Apply of a new op with abs(Distance) < app+0x8 now gives sld 1101 "Below minimum distance." (caption 201) and
   makes no feature (formats/extrude.md 1.3, 0x1800d892f; app+0x8 = tol * 10 from SolidsAppBase ctor FUN_180164c60,
   decomp/extrude/b5.c:146). mcamui checked it only for the preview.
2. AutoCursor (FUN_1800de680) flips a chain only when abs(projection) > app+0x8 (was > 0).
3. Edit mode (Edit Parameters) shows no prompt (formats/extrude.md 1.3 "empty in edit mode"); mcamui showed sld 851.
4. The ribbon's "Extrude" runs the solid Extrude only for FT sld_newExtrude: Draft Extrude (sld_newDraftFace_3) and
   Surfaces > Extrude (ExtrudeSurface) share the tooltip "Extrude" and were enabled + started the solid command; now
   greyed (NOT PORTED). mcamui scene baselines updated: solids_tab, extrude_interactive (the Draft Extrude button),
   extrude_edit (the prompt) in tools/tests/screenshots (both variants).
5. (fix, scripted runs only) sm_menu_act initialises the tree's open flags first (Collapse all before the first tree draw
   was undone by the later sm_init).

### Qt features now working (viewer-qt)
- Ribbon: Solids > Extrude (and Recent Functions "Extrude") start the command (ex_start; nothing open = File > New);
  every other Solids / Model Prep command greyed as mcamui.
- Chain selection through phase 4's Chaining dialog (chain_begin_extrude); its OK = ex_chains_done (MainWindow::chainOk),
  the close box / Esc = ex_escape (chainClose, the Esc branch after the wireframe command).
- src/expanel.*: the **Solid Extrude** panel as an ADS tab in the function-panel pane (shown / brought to the front while
  ex_mode && ex_phase, closed during Add chains / Rechain all and at the end): Basic / Advanced tabs, the expanders
  Operation / Chains / Distance / Trim to Faces | Draft / Thin Wall / Plane Orientation / Preview with mcamui's texts,
  checkbox headers, enabled states (ex_enables), validation errors (red label / border, text as tooltip), the Chains list
  (Ctrl multi-select, context menu Remove / Reverse / Add / Rechain all), Target text + Select Target Body, AutoCursor /
  Select Vector picks, the Vector box, OK / Apply (Regen in edit mode) / Cancel / Help (greyed) at the bottom. Every
  change runs the panel frame (live preview, arrows) like mcamui's next frame.
- View: the drag arrows drawn exactly as mcamui (shared ex_ov_arrows: orange distance, red / green thickness); a left
  press on an arrow drags it (ex_arrow_hit / ex_drag_move / ex_drag_end, Esc restores), graphics clicks go to ex_click
  first (reverse a chain, AutoCursor point, target body, vector), the hover highlight is off during a panel pick.
- Keys: Enter = Apply, Delete removes the selected chains, Esc = ex_escape.
- Undo / redo of OK / Apply / edits / Delete through the U_FEAT records (core_ui feat_undo; Ctrl+Z / Ctrl+Y / QAT).
- Solids manager (managers.cpp): part solids, Extrude bodies with their feature rows and the opmanres strip images
  (selected image on the selected row), expand / collapse kept in sm_body_open, the right-click menu with the Solids /
  Model Prep submenus and the row kind's items (Delete on the last feature, Regen All Solids, Expand all / Collapse all,
  Edit Parameters, Solids > Extrude... live; the rest greyed). mcamui's Solids tab has no toolbar (none decoded) - none here.
- Test hooks: extrude / exset / show:extrude / exedit / exchain / exchainok (as mcamui), ours: exset:dragtow, smmenu,
  smact, smrows, exdump, MCAMUI_EXDUMP=1 (both apps); Qt-only: expanel (dump the panel), expanelset:KEY[=V] (a control
  through its widget), smpopup:KIND,FEAT. Message boxes of a step are printed (MESSAGE:) and closed at its end, as mcamui.

### Still stubbed / not ported (phase / reason)
- NOT DECODED (as mcamui, greyed / left out): the target search FUN_1800d8d70, the automatic op type (4.8), face picking
  (Trim to Faces list, Add Selection / Reselect All), the selected chains' graphics highlight (draw_chain_sel colour), the
  preview colour, the arrows' screen-constant shaft, the OK-Regen label, Help topics, Enter's ev+0x150.
- Solids manager: Suppress / Highlight / Rename / Duplicate / Attributes / Check / Remove history / Regen Solid /
  Auto-Highlight / Recreate trimmed bodies and every Solids / Model Prep command except Extrude (not implemented in
  either app); the part solids' description and history (NOT DECODED).
- Cosmetic: Qt widgets (line edits for the EvaluatorBoxes, Qt radio / check styles) instead of nuklear's.

### Tests (2026-10-21)
- `tools/tests/solid_compare.sh OUTDIR` runs tools/tests/solid_scenes.txt (36 scenes: Create / holes / groups / Cut /
  Through all / Boss / Both + draft / Reverse / thin open / automatic Thin Wall / panel steps / Apply / Cancel / session
  values / non-planar refusal / edit + unchanged + base chain / undo + redo + undo of an edit / Solids menu per row kind /
  Delete + undo / Regen + Collapse / Expand / Edit Parameters / Solids > Extrude / Remove / Rechain / reverse a chain /
  below minimum / arrow drag + Esc / thickness drag / save + reopen / edited save; events: click-reverse, vector pick,
  target pick) in both apps with MCAMUI_EXDUMP=1 and compares the command / feature / body / exset / Solids lines and the
  messages, plus the 2 saved projects byte for byte: **0/38 differ**. Qt panel through its widgets (expanelset) = the
  same scene through exset in mcamui: identical feature / body.
- mcamui before / after the move (scenes.txt 46): identical dumps, shots identical except the clock (tool_list_report),
  the recent-files list (backstage_open) and the flagged changes above (solids_tab / extrude_interactive: Draft Extrude
  greyed; extrude_edit: no prompt).
- fileop 0/189 (first run aborted: the output folder did not exist - rerun clean), postreg 0/16, wf_compare 0/55, chain_compare 0/40, solid_compare 0/38.

### Screenshots (screenshots/, not committed)
2026-10-21_qt_phase5_qt_*.png and the mcamui references 2026-10-21_qt_phase5_mcamui_*.png (same scripts): chaining,
panel_basic, panel_advanced, arrows_iso, error_state, edit_regen, cut_target, solids_tree, solids_menu (the mcamui one
shows the tree only: it has no script step to open the menu); Qt only: solids_tab (Extrude live, Draft Extrude greyed).

### Notes for phase 6 (toolpath dialog)
- `MainWindow::chainOk` now branches on ex_mode (Extrude) - the op dialog (open_params) goes into its else branch, where
  chain_keep_last + show_params are now; the Esc order in eventFilter is geo_cmd, ex_mode, show_params, cam_mode (mcamui's); last (2026-11-14) the idle Esc: no state of ours running (MainWindow::escIdle) -> sel_escape_idle clears the general selection (formats/ui.md "Esc with no function running").
- `g_qt_scripting` (mainwindow.cpp) marks a running MCAMUI_SCRIPT step: per-frame work mcamui does in draw functions
  (the Extrude preview, display-only writes such as ex_vec_show) must not run between steps, or the dumps diverge.
- Message boxes: MainWindow::msgShown / msgClose mirror mcamui's show_msg (cleared after every step's dump).
- The managers' dock now holds two function-panel tabs (wireframe panel, Solid Extrude); the op dialog is a dialog,
  not a tab.

## Wireframe chain commands -> Mastercam chaining (done 2026-10-21, both apps)
User decision 2026-10-21: "make it like master cam". Fillet Chains, Chamfer Chains and Offset Chains used mcamui's
single chain pick (cui_geo.c geo_take 'c' -> cam_chain); the originals run chain_it (several chains). Now both apps open
the real Chaining dialog (phase 4) for them. Backup of the tree before the change:
~/mastercam-re/backups/pre_chaincmds_2026-10-21.tgz. Spec: formats/wireframe.md "Fillet Chains" / "Chamfer Chains" /
"Offset Chains" (loops, codes, sources) and its "Chain commands" implementation note. Other wireframe commands with
chain_it in the dumped decomp: none (chain_it callers in mccad / mccadutil: sketcher_fillet_2, sketcher_chamfer_2,
do_xform_offset_ctour only; Spline Curves' ReselectAllChainsCommand belongs to a command not ported in either app).

| where | what | source |
|---|---|---|
| core_ui cui_chain.c `chain_begin_geo(prompt)` | the chain command's chain_it: empty list (init_chaining), the dialog mode from the config (run_chain_dlg gets no mode), every mode (Chain / Single / Partial / Window / Area / Polygon / Vector / Point, Last, Options ...) via `chain_full()`; `chain_dlg_shown` / `chain_press_pick` / `chain_window_drag` / `chain_sketching` / `chain_click_pre` / `chain_click_ent` include `geo_chaining`; the pick itself moved to `chain_pick_at(e, wx, wy)` (also used by the scripts' `draw:CMD|cN`); `chain_reverse` (reverse_chain, shared with the dialog's Reverse); `gallery_pick` ends a chaining geo command first | chain_it(int) -> run_chain_dlg(.., NULL, 0, 0, 0, 0, false) mccore_chain_range.c l.5014; init_chaining l.10699 |
| cui_geo.c `geo_chaining`, `geo_chains[]` / `geo_nchains`, `geo_chain_cmd` | chain_it running / the command's chain list (local_4d0, local_2f0); `geo_start` starts chain_it (prompts mccadres 4233 "Select chain"; Offset Chains mccoreres 3940 "Offset: select chain" [inferred: mccadutilres 3940 is empty]) | sketcher_fillet_2.c:196-210, sketcher_chamfer_2.c:197-212, do_xform_offset_ctour.c:93 |
| cui_geo.c `geo_chains_done` | the dialog's OK (front ends: `cui_hooks.chain_ok` while `geo_chaining`): `chain_keep_last` (cleanup_chaining -> FUN_1801ea2b0 lastchn.tmp), the chains handed over (no longer drawn chained: unselect_chains / EraseArrows), the worker on every chain in one undo step | as above |
| cui_geo.c `geo_chain_button` (from `geo_panel_button`) | Apply 1 = commit + chain_it again (Fillet / Chamfer: only with chains); OK 0 = commit + end; Cancel 2 = `geo_cancel`; **Reselect 4** = drop the preview + chain_it again (Esc there ends the command); nothing while chaining | codes: sketcher_fillet_2.c:228-299; Reselect = ReselectAllChainsCommand -> manager vtable +0x90 FUN_18016aa80 / +0x80 FUN_180176cc0 PostMessage(0x8038, 4) (mccad.interop view models, decoded with ilspycmd + objdump this session) |
| cui_geo.c `geo_panel_enabled` / `geo_panel_apply_enabled`, `geo_panel_req` | panel greyed while chain_it runs (Ordinal_4282 EnableWindow(0)); Apply = EnableApply(chains) for Fillet / Chamfer Chains (manager vtable +0x30, FUN_18016adf0 / FUN_180176f50 set "EnableApply"); `CuiPanelOps.toggle` gets a code (>= 0 = a live button posting it) | as above |
| cui_geo.c `geo_live_update` | chain commands rebuild from the list on any panel change while it holds chains (bVar13), even when the last run made nothing | codes 5-9 / 5-7 :286-291 / :293-298 |
| cui_geo.c Offset Chains | no side point: each chain offset to 'L' of its direction (`offset_chain(c, +1, d)`); `geo_need` = 'e' after chaining: a chain's entity reverses that chain + rebuild (prompt mccadutilres 1523); live in the main colour (`LV_MAINCOL`, Xform preview colours NOT DECODED), Esc / Cancel drop it; Apply greyed (NOT DECODED: the dialog's +0x5091 setter is not dumped) | FUN_18001bc50 / FUN_18001c220 'L' / 'R', FUN_18001ca60, do_xform_offset_ctour.c:120-189 |
| mcamui.c / geopanel_ui.inc | `hook_chain_ok` geo branch, the chaining window for `chain_dlg_shown()`, its close box = `geo_cancel` while `geo_chaining`, auto-highlight while chaining, polygon band via `chain_sketching()`; panel greyed with input kept away (`fp_off`), Apply greyed, Reselect live | - |
| viewer-qt mainwindow.cpp / geopanel.cpp | `MainWindow::chainOk` geo branch (before ex_mode), `chainClose` = `geo_cancel` while chaining, hlActive; GeoPanel `syncEnabled` (body + OK / Apply / Cancel disabled while chaining, Apply = EnableApply), the Reselect QPushButton emits `button(4)`; test hook `panelset:K=1` clicks a live toggle | - |

Script steps (both apps): `draw:Fillet Chains|c0|c5` = Chain-mode picks at the entities' midpoints, then the dialog's OK;
ours `geobtn:N` (panel code N: 0 OK, 1 Apply, 2 Cancel, 4 Reselect), `geochains` (state + chain list); `geoapply` on a
chain command = code 1. Deviations / NOT DECODED: the panel stays visible (greyed) during Offset Chains' chain_it
(Mastercam creates CXfOffsetContourDlg after it - cosmetic); Offset Chains' other dialog fields (Move / Join, copies,
direction R / both, corners, depth / angle) are still not in the panel; the fillet panel prompt "*" is not decoded; an
entity in two chains of one list: the later chain is skipped; the get_event return codes besides 2 (e.g. 1 from
chain_it, Esc inside the panel loop's local_4d7 flag) keep the earlier rules (Esc in the panel phase commits Fillet /
Chamfer Chains, drops Offset Chains).

### Tests (2026-10-21)
- wf_compare: 70 scenes (55 + 15 new: two chains for each command via `chainat` through the click path, `draw:..|c0|c5`,
  Area mode (outer + island), Apply -> chain again, Reselect (+ Esc in it), Esc while chaining, live rebuild of both
  chains, Cancel, Undo / Redo of a two-chain result (one step), Offset Chains reverse pick): **0/70 differ**. The 55
  earlier scenes: old mcamui (built from the backup) vs new mcamui identical entity / live dumps, offsetc included (its
  side point token dropped: the same outside offset comes from 'L' of the picked chain).
- Qt-only: `panel` greyed during chaining, Reselect through its widget (`panelset:7=1`) = geobtn:4.
- chain_compare 0/40, solid_compare 0/38; mcamui scenes.txt (46 x 2): dumps identical, shots identical except the
  clock (tool_list_report); fileop 0/189, postreg 0/16.

### Screenshots (screenshots/, not committed)
2026-10-21_qt_chaincmds_before_{qt,mcamui}_fillet.png, _before_qt_offset.png (old single pick: one chain, Reselect
greyed); _after_{qt,mcamui}_fillet_chaining.png (Chaining dialog with two chains, panel greyed), _fillet_preview,
_chamfer_preview, _offset_preview (two chains each; offset with the second chain reversed).

### Notes for phase 6 (toolpath dialog)
- `MainWindow::chainOk` / mcamui `hook_chain_ok` now branch geo_chaining -> ex_mode -> op dialog; keep the geo branch
  first. `chain_dlg_shown()` is true for geo chaining too - the op dialog's own state must not rely on cam_mode alone.
- `gallery_pick` ends a wireframe chain command that is chaining (they share cchains).


## Phase 6 (DONE 2026-10-21): the toolpath (operation) dialog
The operations model, the toolpath dialog's model (mcmill ToolpathDlg = mccoreres dialog 6544: page tree, every page,
Linking, pictures, quick view, toolbar), the Tool page (25524 / 6556) with its menu 1247 and the Tool List (6410), and the
Toolpaths manager's right-click menu model moved out of viewer/mcamui.c, opparams_ui.inc and toolmgr_ui.inc into
`viewer/core_ui/` (plain C, mcamui names and decomp citations kept; formats/mcamui_dialogs.md 1 is the code map, its
function names are now core_ui's). mcamui keeps only the nuklear drawing and calls the moved code; the Qt app draws the
same model with Qt widgets. Makefile (mcamui + the Windows WCUI list) and viewer-qt/CMakeLists.txt (`mcamcore`) build the
four new files. Backup of the tree before phase 6: ~/mastercam-re/backups/pre_qt_phase6_2026-10-21.tgz. Done in one
piece (no 6a / 6b split was needed in the end).

| moved to core_ui | what | moved from |
|---|---|---|
| core_ui.h "operations" / "toolpath dialog" / "Tool page" | `pending`, `tool_choice`, `edit_camop`, `op_clip` / `op_deleted` / `op_clip_cut` / `op_deleted_at`, `tp_grp_name` / `mach_grp_name`, `cm_group` / `cm_ctrl`, `CM_*`, `CMItem`, `PG_*`, `OpHFlags`, `LKP_*`, `TM_COLS` / `TmColSet` / `TM_SET`, the walker `PpOps`, `LkRow`, `TpCtl` / `TPK_*`, `OtpItem` / `OTP_*`, `PpRect` / `PP_FRAME` / `PP_BUTTONS` / `PP_TB`, `TL6410`; hooks `op_menu`, `tool_wizard`, `tool_manager`, `lib_opened` | mcamui.c / opparams_ui.inc / toolmgr_ui.inc statics |
| cui_ops.c | `default_op`, `heights_from_default_op`, `open_params`, `generate_pending`, `post_ops`, `regen_ops_for_tool`, `chain_dup`, `pending_free`, `open_op_params`, `readd_camops`, `takeover_index`, `add_op_at`, `edit_selected`, `fs_group` / `fs_material` / `recalc_feeds` / `reverse_selected` / `fs_from_source`, `selected_camop`, `delete_selected_op`, `camop_clone` / `camop_insert` / `camop_regen`, `op_copy` / `op_paste` / `op_undelete`; the Toolpaths manager menu (OPMANRES menu 780): `cm_items`, `cm_kids_of` / `cm_shown` / `cm_has_kids` / `cm_router`, `cm_leaf_ok`, `cm_usable`, `cm_run` (the front end's items through `cui_hooks.op_menu`); `op_dump` (MCAMUI_OPDUMP=1), `op_ctl_dump`, `cui_op_script` | mcamui.c (2D CAM, tool manager, edit / menu sections, run_script) |
| cui_opdlg.c | `PG_TITLE`, `pg_has`, `pg_done`, `PG_TREE`, `pg_parent_ok`, `pp_hf` / `pp_page`, `hf_from_record` / `hf_from_op` / `rp_put` / `hf_apply`, `lk_pick` / `lk_pick_z` + new `lk_click` (handle_click's pick), the Linking rows `lk_frame` / `lk_rows` / `lk_set` / `lk_set_clr_end` (was lk_row / page_linking), `pp_rec`, `pp_tstate` / `pp_tree_fresh` / `pp_mp_pic`, `pp_rs16`, `pp_page_pic`, `pp_stock_bmp`, `pp_disabled`, `pp_wrap_rect` (pp_wrap_begin's rule), the frame `PP_FRAME` / `PP_BUTTONS` / `PP_TB` / `pp_hst`, `pp_title`, `pp_frame_begin`, `pp_page_shown`, `pp_tree_rows` / `pp_tree_click`, `pp_note_change` (the tree's dirty image), `pp_quickview_rows`, `pp_cancel` (Cancel / Esc), `pp_page_body` = every page's controls in mcamui's row order (was draw_cut_page and draw_op_params' page switch), `pp_hf_record` / `pp_hf_op` / `pp_hf_apply` | opparams_ui.inc, mcamui.c (lk_pick, Esc) |
| cui_toolpage.c | `tlib` / `tlib_ok`, `tm_open_lib`, `starter_lib_open` (`cui_ui_dir`), `next_tool_num`, `part_add_tool`, the tool lists' column sets + settings file (`tm_cs`, `tm_head`, `tm_view`, `tm_dispmode`, `tm_factive`, `tm_listview`, `tm_cs_reset_list`, `tm_settings_read` / `_write`, `tm_col_toggle`), `tm_real`, `tm_info_pitch`, `tm_cell`, `tm_tool`, `tm_count`, new `tm_list_cols`, `cfg_places` (config 29003, now read by `cui_config_item`); the Tool page: `tp_rapid` / `tp_ftc` / `tp_batch`, `tp_metric`, `tp_hem`, `tp_from_record` / `tp_from_rpatch` / `tp_rp` / `tp_apply`, `tp_z` / `tp_fpt` / `tp_sfm`, the controls `tp_ctls` (template positions, draw order) / `tp_ctl_text` / `tp_ctl_check` / `tp_ctl_combo` / `tp_ctl_set` (the kill-focus handlers that were inline in page_tool) / `tp_set_comment` / `tp_pick` / `tp_new_tool_done`; menu 1247 `otp_menu` / `otp_menu_act`; Tool List `TL6410`, `tl6410_mach` / `_libfile` / `_machine_lib`, `tl6410_prepare` / `_count_text` / `_ok` / `_cancel` | opparams_ui.inc, toolmgr_ui.inc, mcamui.c |
| cui_optree.c | `cui_otx_*` (the Toolpaths manager rows of optree_text.inc for the core and Qt), `tm_describe` + the SystemPlanes names (from viewer-qt core_bridge.c), `op_doc_file` (Doc file) | mcamui.c, viewer-qt/src/core_bridge.c |

mcamui.c / opparams_ui.inc now: the nuklear widgets over the model (`NK_PP_OPS`, page_tool / draw_tool_list_6410 iterate
the core templates in the old order), `hook_op_menu` (Edit Common Parameters, Expand / Collapse, Tool list report,
Renumber tools, Rename), `hook_tool_wizard` / `hook_tool_manager` / `hook_lib_opened`, `ui_cm_run`; run_script calls
`cui_op_script` (the moved steps) - `show:otpmenu` (menu 1247 open, screenshots) is new. Old vs new mcamui (built from
the backup): scenes.txt 0/46 (dumps + shots), 128 op scenes (every op type, every page shot, edits, regen, delete, undo,
menu, stored ops) identical dumps, shots and posted NC except the post's TIME line.

### Qt features now working (viewer-qt)
- src/opdlg.*: **OpDialog** = the 6544 frame as a tool window (FloatDlg, no close box as mcamui's): toolbar (PP_TB images,
  only Tool Manager live in Mastercam - greyed here until phase 7), the page tree (FUN_18014a700 order, images clean /
  edited / disabled, unported pages greyed and not selectable), Quick View Settings, legend, the page frame, the status
  line (cam_msg), OK / Cancel (+ Apply / Help greyed) with the uictrls bitmaps. Positions in dialog units (chainDu, the
  templates' 6 x 13 base units, 11 px font). Pages: every page mcamui has - Cut Parameters of every op type, Depth Cuts,
  Lead In/Out, Multi Passes, Tabs, Roughing, Finishing, Rough/Finish (slot, helix), Entry Motion, Tip Comp, Arc Filter,
  Coolant - from `pp_page_body` (row-laid in mcamui's row order: a dynamic row repeats as nuklear's), the page picture at
  its template rectangle with the controls beside it (`pp_wrap_rect`); the **Linking Parameters** rows (check, "..." pick
  button, value, Absolute / Incremental, "Use clearance only ...", drill Subprogram greyed); the **Tool page** at its
  template positions (25524 HEM / 6556: tool list with the Tool Manager's part columns, read-only tool fields, feed / FPT /
  plunge / spindle direction / speed / SFM-CS / retract rate edits with mcamui's commit rule, Force tool change, Rapid
  Retract, To batch, Comment, Recalculate values / Filter / RCTF / turret / Area Mill inspection group greyed) and its
  right-click **menu 1247** (Select library tool, Re-initialize feeds & speeds live; Create new tool / Edit tool / Tool
  manager greyed until phase 7). **ToolListDialog** = 6410 (library label + tooltip, the library's tools, count, OK adds
  the tool to the part and the op, Cancel; New Library / Filter / Display mode / Help greyed).
- The dialog opens from the chaining's OK (`MainWindow::chainOk` else branch = `open_params`), from the Toolpaths tree's
  Parameters row or a double click on an op (`open_op_params`), from the tree menu's toolpath items (Contour ... via
  `cm_run` -> gallery_pick -> chaining); OK = `generate_pending` (regenerates an edited op in place, undo record), Cancel /
  Esc = `pp_cancel`; a page change marks its tree item edited (`pp_note_change`); the Linking "..." buttons arm a graphics
  point pick (`lk_click`, Esc ends it).
- Toolpaths manager: the right-click menu (OPMANRES menu 780 from `cm_items`: Mill toolpaths cascade, Router alias,
  hidden / Ctrl items, `cm_usable`; an op row selects it first, the group rows set `cm_group`): Cut / Copy / Paste / Delete
  / Undelete / Undo, Reverse toolpath, Recalculate feeds/speeds, Expand / Collapse, Doc file, Groups > Delete / Delete all
  empty live; the group names from core_ui; Delete on a selected op made here (not while the dialog is up) as mcamui.
  Toolbar 800: Post selected operations live (post_ops); Select all operations, Toggle display, Hide rapid as before.
- Stale ops: an edited tool regenerates the ops made here that use it and marks Mastercam's stored ops with it STALE
  (`regen_ops_for_tool`, tree image 0x13 / 0x18, the post refuses them) - shared code, both apps.
- Test hooks: every mcamui op step through `cui_op_script` (gen, libtool, usetool, edittool, delop, post, camops, recalc,
  reverse, editop, params, setdepth, regen, lkpick, lkval, menu, group, tools, docfile, show:page:N) and ours (both apps):
  opdump, opctl, oppage:SLOT, opset:LABEL=V (combo#K), lkset:ROW,WHAT=V, lkclick:X,Y,Z, tpset:ID=V, tpcomment:T,
  tppick:I, tpmenu:TEXT, tl6410:open|sel=I|ok|cancel, opok, opcancel, delete; MCAMUI_OPDUMP=1. Qt only: opdlg (the
  widgets), opdlgset:KEY=V (a control through its widget: page labels, combo#K, tp:ID, lk:ROW,WHAT, tree=SLOT, ok,
  cancel), toollist, oppopup (menu 1247 open), tpmpopup:GROUP (the tree menu open). mcamui only: show:otpmenu.

### Changed in both apps (Mastercam over mcamui)
- None in behaviour. (Qt-only fix: Delete removed the selected entities only when no op was selected - phase 2's
  guard - now mcamui's rule: the selected op made here first, else the entities.)

### Still stubbed / not ported (phase)
- Phase 7 (Tool Manager): the toolbar's Tool Manager..., menu 1247 Create new tool / Edit tool / Tool manager (the Define
  / Edit Tool wizard; `cui_hooks.tool_wizard` / `tool_manager` are NULL in Qt -> greyed), the tree's tool row click
  (mcamui: opens the Tool Manager on that tool), edittool's wizard (the script step uses tw_init_edit directly).
- Phase 8: the tree menu's Edit common parameters... (243), Tool list... (report window), Renumber tools... (805), Groups >
  Rename (input box) - greyed in Qt (`ToolpathsManager::menuHas`); toolbar 800 Backplot / Verify selected.
- NOT DECODED / NOT PORTED (as mcamui, greyed): pages Toolpath Type, Holder, Stock, Break Through, Home / Ref. Points,
  Planes (WCS), Canned Text, Misc Values, Axis Control / Rotary, Tab Cutoff, HST Trochoidal / Transitions / Leads / Contour
  Wall; Apply, Help, the toolbar's other buttons, Recalculate values, the CToolFilt filter, Display mode, RCTF / HEM /
  turret, the inspection group, the focus-dependent pictures (formats/mcamui_dialogs.md 1).
- Cosmetic: Qt widgets for nuklear's property fields (label + line edit showing 2 decimals), list headers in the UI
  font's size.

### Tests (2026-10-21)
- `tools/tests/op_compare.sh OUTDIR` runs tools/tests/op_scenes.txt (72 scenes: every op type created with its pages'
  controls dumped, the dialog through its model steps - chaining OK, pages, Linking rows / picks / Esc, Tool page edits /
  switches / comment / menu 1247 / Tool List, tree states, Cancel / Esc, drill / pocket / face / dynamic pages - edits,
  regen, delete, Delete key, undo / redo, the tree menu items, reverse / recalc / edittool / save + reopen / post toolbar,
  stored ops) in both apps with MCAMUI_OPDUMP=1 and compares the op / dialog / page lines, messages, the saved projects
  and the posted NC: **0/135 differ** (76 scenes + 59 saved projects / NC files). 4 of the scenes (`w_*`) run the Qt app
  through its widgets (opdlgset: tree, Cut page fields / combo, Tool page list / edits / check box, Linking value / switch,
  OK / Cancel) against mcamui's model steps: same results. PARTS (a link to the user's part folder) is copied first: the
  post writes next to the part.
- old vs new mcamui (the backup's build): scenes.txt 0/46 (dumps + pixel shots); 128 op scenes (every op type, every
  page's shot, edits, regen, delete, undo, menu, stored ops; 17 projects, 20 NC files) 0/128 but the post's TIME line.
- Regressions after the final builds: fileop 0/189, postreg 0/16, wf_compare 0/69, chain_compare 0/40, solid_compare 0/38.

### Screenshots (screenshots/, not committed)
2026-10-21_qt_phase6_qt_*.png and the mcamui references 2026-10-21_qt_phase6_mcamui_*.png (same scripts), one per page
kind: contour_tool, pocket_tool_hem, area_tool_inspection, drill_tool, the Cut page of every op type (contour, pocket,
face, drill, slot, dynamic, area, dyncontour, peel, circle, thread, helix, chamfer, engrave), contour_depth, pocket_depth,
contour_lead, pocket_lead, contour_link, drill_link, contour_arc, contour_coolant, contour_multi, thread_multi,
contour_tabs, pocket_rough, pocket_finish, pocket_entry, circle_rough, dynamic_entry, slot_roughfin, helix_roughfin,
drill_tip, edited_tree, tool_list_6410, tool_menu_1247, stored_op, stored_op_link, new_op_from_chaining; Qt only:
tree_menu.

### Notes for phase 7 (Tool Manager)
- The tool lists' column sets / settings file / cells (`tm_cs`, `tm_settings_read`, `tm_cell`, `tm_list_cols`), the current
  library (`tlib`, `tm_open_lib` + `cui_hooks.lib_opened` for the dialog's selection state), `part_add_tool`,
  `next_tool_num`, `cfg_places` are already in core_ui/cui_toolpage.c; the rest of toolmgr_ui.inc (filter, sort, the
  dialog's actions, the wizard) moves next to them.
- Qt: set `cui_hooks.tool_wizard` (index -1 = new; on close call `tp_new_tool_done()` - OpDialog::buildTool does when
  `otp_wait_new >= 0`) and `cui_hooks.tool_manager`; menu 1247 and the op dialog's toolbar button then enable themselves
  (the toolbar button is greyed explicitly in OpDialog's constructor: enable it with the hook). The tree's tool row
  (`ToolpathsManager::fillOp`, ROLE_OP -2) should open the Tool Manager on that tool (mcamui tm_part_sel_set).
- `edittool:` (cui_op_script) mirrors mcamui's wizard path with tw_init_edit; keep it identical when the wizard moves.
- Test folders: copy PARTS with `cp -rL` (it links to <user-parts>) and pass absolute `saveas:` paths.

## Op dialog batches 1 + 2: template pages (2026-10-22, both apps through core_ui)

formats/opdlg_port_audit.md 5 batches 1 / 2 and 6.2; code map formats/mcamui_dialogs.md 1.y.
- Model (core_ui): cui_opdlg.c "template pages" + cui_opfields.h (`pf_page_dialog`, `pf_active`, `pf_ctls` -> `PfShown`,
  `pf_set`, `pf_reset`, `pf_apply`, `pf_dirty_sig`); the table viewer/core_ui/cui_opfields.inc is generated by
  tools/gen_opdlg_fields.py from formats/opdlg_fields.tsv + exports/mcmillres_dialogs.tsv / mccoreres_dialogs.tsv
  (43 templates, 1072 controls). cui_ops.c: `pf_apply` in `generate_pending`, the `pfset:ID=V` step, opctl prints
  `template N` / `tctl` lines; pp_hf_record / pp_hf_op reset the edits.
- mcamui: opparams_ui.inc `page_template` (nk_layout_space at the template positions, as page_tool; `pf_edit_txt`
  commits any finite number); a template page draws its pictures as its statics (no pp_wrap group).
- Qt: opdlg.cpp `OpDialog::buildTemplate` (QLabel / QLineEdit / QComboBox / QListWidget / QCheckBox / QRadioButton /
  checkable QGroupBox / disabled QPushButton / QSlider at du() rectangles); `opSig` includes the edit state; test keys
  `opdlgset:pf:ID=V`.
- Engines / files: rpatch capacity 64 (`CAM_NRPATCH`, ours); cam_fileop.c LEADS reads the entry side, the contour roll
  (u8 0x5d3 since 2026-10-22; was S16 0x5d4 + an opt-in), drill tip comp u8, circle tip comp 0x1238, `cam_params_from_patches` (sidecar
  reload); mcam_saveops.c writes the lead to entry and exit (only differing fields), drill tip comp u8, roll per the
  opt-in. No change in core_ui.h, mcamui.c, mainwindow.cpp.
- Tests: tools/tests/op_scenes.txt: the scenes that set row-laid controls by label now use `pfset:ID=V` /
  `opdlgset:pf:ID=V`; new: pw_* (every page of the 12 op types), rt_* (edit + OK + camops + save + post + export + reopen
  the sidecar and the exported part: 9 types), pf_grey, pf_cancel, pf_helix_finish_mode (a live field the engine refuses:
  its NOT DECODED error), w_pf_widgets / w_pf_widgets_pocket (Qt widgets);
  op_compare.sh compares the template / tctl lines too. Results (formats/opdlg_port_audit.md 7): op 0/193, the shared
  scenes' NC / projects identical to the base build, wf / chain / solid / tool / misc / stockplanes / planes 0-diff,
  fileop 0/189, post 0/16. Screenshots: screenshots/2026-10-22_opdlg_<page>_{mcamui,qt}.png (16 pages).

## Phase 7 (DONE 2026-10-21): Tool Manager, Define Tool wizard, Tool filter, reports, Renumber tools
The model of the Tool Manager (mccoreres dialog 1099, CToolListBoth), the Tool List Filter (25519, CToolFilt), the Define /
Edit Tool wizard window and its Coolant dialog, the tool reports (the Tool Manager's Report... / Detailed report... and the
Toolpaths manager's Tool list...) and Renumber tools (opmanres 805, CRenumbTl) moved out of viewer/mcamui.c and
viewer/toolmgr_ui.inc into `viewer/core_ui/` (plain C, mcamui names and decomp citations kept; formats/toolmanager_ui.md
1.5.10's code map now points at core_ui). mcamui keeps only the nuklear drawing (toolmgr_ui.inc: the lists, menus, filter
window, prompts, the window; mcamui.c: draw_tool_editor / tw_page_* / draw_coolant_dialog / draw_tool_report /
draw_renumber, the file dialogs) and calls the moved code; the Qt app draws the same model with Qt widgets. Makefile (mcamui
+ the Windows WCUI list) and viewer-qt/CMakeLists.txt (`mcamcore`) build the two new files. Backup of the tree before phase
7: ~/mastercam-re/backups/pre_qt_phase7_2026-10-21.tgz.

| moved to core_ui | what | moved from |
|---|---|---|
| core_ui.h "Tool Manager" / "Define Tool wizard" | `TmFilt`, `TmfType` / `TMF_TYPES` (CtlToolType + template positions), `TMF_OPM` / `TMF_UM` / `TMF_DM` / `TMF_RAD` / `TMF_MAT`, `TmPrompt` / `TQ_*`, `TmJob`, `TMK_*`, `TMA_*` / `TmMItem` (menu 1225), `TmRect` / `TmLayout`, `TwPRow` / `TWP_*`, `TwCool`; hook `tm_closed` | toolmgr_ui.inc / mcamui.c statics |
| cui_toolmgr.c | dialog state `show_tm`, `tm_part_sel` / `tm_lib_sel`, `tm_sel` / `tm_anchor`, `tm_bup_done`, `tm_found_init`, `tm_confirm`, `tm_focus`, `tm_lib_modified`, `tm_clip` / `tm_nclip`, `tm_imp_last`; config 31020 / 31011 / 31017 / 31006 / 31007 (`cfg_rpt_*`, read by `cui_config_item` -> `cui_rpt_config_item`); the filter `tmf` / `tmf_edit`, `tmf_defaults`, `tm_open_filter`, new `tmf_ok` / `tmf_types_all` (the dialog's OnOK / All / None), `tm_used`, `tm_filter_ok`; `tm_cmp` / `tm_rows` / `tm_sortlist` / `tm_nsel` / `tm_clear_sel`, new `tm_colmenu_ok`; the library file `tm_copy_file` / `tm_lib_backup` / `tm_lib_reload` / `tm_lib_finish` / `tm_lib_readonly` / `tm_lib_write`; the prompts `tmq` / `tm_ask` / `tm_q_answer`, `tm_is_similar`, `tm_attribs_match`; the add jobs `tmj` / `tm_job_*`, `tm_up` / `tm_down` (+ `tm_up_ok` / `tm_down_ok`), `tm_copy` / `tm_paste`, `tm_delete` / `tm_delete_ask` / new `tm_confirm_answer` (the «Deleting Tools» buttons) / `tm_delete_unused`, `tm_create` / `tm_edit`; `tm_close_now` / `tm_after` / `tm_save_prompt` / `tm_ok` / `tm_cancel` / `tm_switch_lib` / `tm_browse` / `tm_new` / `tm_import` / `tm_save_to` / `tm_compress`, new `tm_def_ext` / `tm_import_target` / `tm_import_to` / `tm_import_start` (tm_fd_accept's and the Import button's file rules), `tm_lib_dir`, new `tm_lib_opened` (was mcamui hook_lib_opened); new `tm_open_init` (draw_tool_manager's first-frame part); `tm_key` (keys as `TMK_*`), new `tm_escape` (tm_keydown's Esc chain), new `tm_click` (tm_click_row's selection rule), `tm_dblclick`, `tm_item_text`, new `tm_item_strip` / `tm_item_cell` (tm_item_image's strip + cell); menu 1225 as data `tm_menu_items` + `tm_menu_act` (was tm_draw_menu's items); `tm_layout` (OnSize, now with the du scale as arguments), new `tm_shown` (FUN_18087a050); reports `tlr` / `show_tlr` / `tlr_new` / `tlr_title`, `make_rstr`, `ci_path`, `report_root_nodes`, `report_tool_node`, `op_tool_index`, `tool_report`, `tm_report`, new `tlr_dump`; Renumber `rn_*`, `rn_open`, `rn_apply`, new `rn_ok` / `rn_cancel`; `tm_dump` + the test steps `cui_tm_script` (was tm_script) / `cui_tool_show_step` / `cui_rn_script` | toolmgr_ui.inc, mcamui.c (tool manager, reports, renumber, run_script sections) |
| cui_toolwiz.c | `tedit` / `tedit_index`, `tm_lib_tool_edit`, `tws` / `tw_page`, `TW_TITLES`, `tedit_open` / `tedit_close`, new `tw_open_part` (was hook_tool_wizard), `tw_first_page` / `tw_title` / `tw_caption` / `tw_can_back` / `tw_can_next` / `tw_can_finish` / `tw_back` / `tw_next` / `tw_goto` / `tw_pick_type` (draw_tool_editor's and tw_page_type's rules); standard sizes `tw_sizes` / `tw_nsizes` / `tw_size_pick`, `tw_apply_std`, new `tw_std_refresh` / `tw_std_prompt` / `tw_std_row_text` / `tw_std_choose`, `tw_warn_row` / `tw_warn_off`, new `tw_warn_answer`; `tw_finish`; Finalize Properties as rows `tw_prop_rows` + `tw_prop_get` / `tw_prop_text` / `tw_prop_set` / `tw_prop_combo` / `tw_prop_set_combo` / `tw_prop_check` / `tw_prop_set_check` / `tw_prop_str` / `tw_prop_set_str` / `tw_materials` and the edit-box rules `tw_parse_num` / `tw_parse_int` (were tw_page_props' IROW / DROW setters and tw_edit_txt / tw_edit_int's parse); Coolant `cool`, `coolant_is_open`, `coolant_dlg_open`, new `cool_idx` / `cool_enabled` / `cool_set` / `cool_ok` / `cool_cancel`, `COOL_ONOFF` / `COOL_NAMES` / `COOL_CNAMES`; `cui_tw_show_step` + new `tw_dump` | mcamui.c (wizard / coolant sections, run_script) |
| cui_optree.c | new `cui_tm_describe` (the exported tl_make_description; mcamui's optree_text.inc calls it) | toolmgr_ui.inc tm_describe |

mcamui.c / toolmgr_ui.inc now: the nuklear widgets over the model (the props page keeps its explicit rows, routed through
`tw_prop_*`; the type panel rows come from `tw_prop_rows`; menu 1225 iterates `tm_menu_items`), `hook_tm_closed` (its menu /
window state), `tm_fd_accept` (file dialog purposes -> core), the UI-only script steps `show:tmkey:K` / `tmsize:W,H` /
`tmmenu:L`. Old vs new mcamui (the backup's build, each in its own copied work folder + config dir): scenes.txt 46/46
identical dumps and pixel-identical shots; 19 extra tool scenes with the old steps (up / down / similar prompts / copy-paste /
keys / views / menu / filter / report / save-to / new library / cancel / wizard pages / props / coolant / finish / edit /
Renumber / Tool list): identical dumps and shots (one shot differs only in the work folder name printed in the report).

### Qt features now working (viewer-qt)
- src/toolmgr.*: **ToolManagerDialog** = 1099 as a resizable tool window (the template size is the minimum, a grip in the
  corner; WS_THICKFRAME / FUN_1808790f0) laid out by core_ui `tm_layout` (OnSize FUN_18087a9c0) in pixels from the template's
  dialog units (chainDu 6 x 13 base units, 11 px font): machine group combo, the 25046 bitmap + "= Tool used in an operation",
  the view combo (Both / Part Tools / Library Tools, FUN_18087a050 shows / hides), the part / library lists in Details
  (`TM_SET` column order / visibility / widths, the 25046 / 25045 images, header click = SortList with the sort indicator,
  header right-click = the column chooser, header edge drag = the persisted width) or Large / Small Icons (the 96 / 32 px strip
  cells of InitBitmaps, "%3ld - %s, %s" item text), Ctrl / Shift / right-click selection (`tm_click`: NM_SETFOCUS deselects
  the other list), double-click (`tm_dblclick`: library = Up, part = Edit), the keys Insert / Delete / Ctrl+C / Ctrl+V / Esc
  through the modal key path (no key reaches the keymap while it is up), Up / Down (bitmaps 25002 / 25001, enabled by
  selection), the library path label with SS_PATHELLIPSIS + full-path tooltip, New Library / Browse Libraries / Import Tools
  (25026 / 5543 / 25010; QFileDialog with mcamui's filters, 25550 title, the second Import dialog), Filter Active ×2,
  Filter..., Display mode radios, OK (`tm_ok` -> «Save Library?»), the close box = Cancel (`tm_cancel`, backup restore),
  Help greyed; the prompts inside the dialog (3840 «Deleting Tools» with 5585, 5258 «Similar Tool Warning» with "Don't show
  this again", 3842 «Save Library?»); **menu 1225** from `tm_menu_items` with its three cascades (View, Arrange tools,
  Import/export tools: Compress, Report..., Detailed report...; Save tool(s) to library... and Compress open file dialogs);
  the column chooser (VisibleColumnPopupMenu). **ToolFilterDialog** = 25519 at its template positions (25 type toggles with the
  caption + tooltip, All / None, operation / unit masking, diameter Ignore / Equal / Less / Greater / Between with its one or
  two edits, radius / material checks with the 3711-3716 labels, All / None, Reset all, OK = AllFalseToTrue, Cancel; 1860 and
  Help greyed). **ReportWindow** (the rpx layout on letter pages, closable). **RenumberDialog** = 805 at its template positions
  (25951 text, Start / Increment, the two offset checks with their amounts enabled by the box, head, unused, OK regenerates,
  Cancel, Help greyed).
- src/toolwiz.*: **ToolWizardDialog** (Define Tool / Edit Tool: the step list «Current Step:», the caption, page 1 the type
  list in Milling / Holemaking groups (3 per row), page 2 the tw_rows of the type (group headers, edit boxes with
  ToolEditBoxConverter's commit rule, greyed disabled boxes, corner / slot / shank tabs, combos, Lock / Left hand checks,
  «Standard sizes» with the StdSizeOverwritingToolGeometry warning) and the tool picture (toolprofile.c `tool_outline`, or
  the ToolValidityChecker error panel), page 3 the Finalize Properties rows from `tw_prop_rows` (Operation column, General +
  the type panel; Coolant button, Metric, Rough / Finish), Back / Next / Finish / Cancel, Help greyed, the close box = Cancel)
  and **CoolantDialog** (Standard Coolant On / Off with the one-at-a-time enable rule, Custom Coolant greyed, OK / Cancel).
- Entry points (as mcamui): the ribbon's Tool Manager / Mill Tool Manager / Lathe Tool Manager (`localRun` "Tool Manager"),
  the op dialog's toolbar button Tool Manager... (enabled with the hook), menu 1247's Create new tool... / Edit tool... / Tool
  manager... (enable themselves through `cui_hooks.tool_wizard` / `tool_manager`; the new tool is selected on the Tool page
  when the wizard closes: `tp_new_tool_done`, now only after it closed as mcamui's page_tool), the Toolpaths tree's tool row
  (the Tool Manager on that tool: tm_part_sel), the tree menu's Tool list... and Renumber tools... (`hook_op_menu`), a
  .tooldb / .tools file dropped on the window (mcamui SDL_DROPFILE; any other file opens as a part). The QAT has no Tool
  Manager command in the user's Workspace (RunMillToolManager is reached through the ribbon).
- Test hooks: every mcamui tool step through core_ui (`cui_tool_show_step`: show:tm[:LIB] / tmsel / tm* / tmup / tmdown / tmdel /
  tmfilter / tnew / twtype / twpage / coolant / twfinish / twset / tedit / holder; `cui_rn_script`: renum / rnset / rnok) and
  the new ones in both apps: show:tmclick:L,R[,c|s|r], tmdbl:L,R, tmmitem:L,TEXT, tmmdump:L, tmfact:L,V, tmconf:Y|N, tmfset:KEY=V,
  tmfok, tmfcancel, tmfilters, tmrdump, twprop:ID=V, twstd:I, twwarn:Y|N|dY, twback, twnext, twgo:P, twpick:T, cool:I=K, coolok,
  coolcancel, twcancel, twdump, tlrclose; rncancel, rndump, tlrdump, toolrpt. UI-only per app: show:tmkey:K, tmsize:W,H,
  tmmenu:L. Qt only (widgets): tmdlg / tmdlgset:KEY=V (row, dbl, up, down, ok, close, filter, view, fa, disp, sort, width,
  col, menu, ans, key), twdlg / twdlgset (back, next, finish, cancel, step, warn, type:T, f:BOXNAME, p:ID, corner:K, ...),
  tfdlg / tfdlgset, rndlg / rndlgset, cooldlg / cooldlgset, tmcolpopup:L. --shot stacks the tool windows as mcamui (the
  Tool Manager under the wizard under the Coolant / filter / report / Renumber windows).

### Changed in both apps (Mastercam over mcamui)
- None in behaviour. One determinism fix: mcamui set the wizard tool's `has_extras` (the .tooldb wizard columns are kept)
  only while drawing a frame (draw_tool_editor), so a scripted Finish without a frame saved a tool without them; core_ui now
  sets it whenever the wizard (re)makes the tool (tedit_open, a type pick, tm_create's template copy) - the same result as
  any interactive run. Test-only: show:tm runs the dialog's OnInitDialog part (`tm_open_init`) at the step (mcamui did it at
  the first frame after the script).
- Qt-only layout notes: the Tool Manager's lists use the whole client height (mcamui leaves nuklear's 14 px grip row);
  Renumber tools is at its template positions (mcamui: rows); the BS_MULTILINE offset checks wrap their text beside the box.

### Still stubbed / not ported (phase)
- Phase 8: the tree menu's Edit common parameters... (243) and Groups > Rename (input box); Backplot / Verify / Post /
  analyze / levels / planes / colours / backstage.
- NOT DECODED / NOT PORTED (as mcamui, greyed or absent; formats/toolmanager_ui.md 1.5.10 / 1.5.11 / 2.12): column reordering
  by dragging headers (LVS_EX_HEADERDRAGDROP is decoded, the drag handling is not ported in either app); drag and drop between
  the lists (LVN_BEGINDRAG FUN_180876f00 / FUN_180876ed0 NOT DECODED); menu 1225's Edit holder / Edit projection (33229) / Edit
  assembly names / Load tool(s) on machine (33003) / Convert a library to / from text (32949 / 32950) / Import from Machining
  Cloud (33080); the block drill dialog (type 20 double-click); the text report fallback of FUN_180875590; the detailed
  report's per-tool XML (header only); the 3887 previous-version check; the filter's type / material pictures (captions
  shown) and 1860 "Copy job setup matl"; the filter prm file ("toolfilt" prefix NOT DECODED: session only); one machine
  group in the combo; the registry key names (toolmanager.conf, ours); the wizard's expression evaluation in edit boxes
  (plain numbers), the holder / live-center wizards, Custom Coolant rows, every Help button; the ActiveReports viewer's own
  window (toolbar, zoom).
- Cosmetic: Qt's fonts are wider than MS Shell Dlg, so some template-sized texts clip (column headers, filter type captions);
  the icon views' spacing is Qt's (the system list-view metrics are NOT DECODED).

### Tests (2026-10-21)
- `tools/tests/tool_compare.sh OUTDIR` runs tools/tests/tool_scenes.txt (68 scenes: the Tool Manager open / library search /
  starter library, clicks, Up with the similar prompts (Yes / don't show / several), Down with the duplicate prompt, OK + «Save
  Library?» Yes / No, Cancel restore, copy / paste both ways, keys, delete (used / confirm Yes / No / library), delete unused,
  Esc chain, menu 1225 model + View / Arrange items, sort / columns / settings file re-read, view / display modes, the filter
  (types / diameter Between / Less / Equal / materials / operation and unit masking / Reset / Cancel), reports (library, part,
  detailed), Save tool(s) to library, New Library, Browse with a modified library, Import Tools, Compress, Create (Insert /
  library) / Edit (double-click / library) / double-click Up, holder; the wizard: new tool every page, all 25 types' Define
  rows, standard sizes + the overwrite warning, Finalize Properties of drills / taps / bores / thread mills, coolant, Back /
  Cancel, duplicate number, renumbering a tool; Renumber tools (basic / options / cancel + reopen / menu), the Tool list...
  report, menu 1247's Create / Edit / Tool manager from the op dialog; 7 `w_*` scenes drive the Qt dialogs through their
  widgets (tmdlgset / twdlgset / tfdlgset / rndlgset / cooldlgset) against mcamui's model steps) in both apps, each scene in a
  fresh work folder and config dir, and compares the tool / wizard / report / filter / renumber / op dumps, messages, saved
  projects, every library (sqlite3 dump, fresh GUIDs numbered by guid_norm.py) and toolmanager.conf: **0/226 differ**
  (68 scenes + 158 files).
- old vs new mcamui: see above. Regressions after the final builds: fileop 0/189, postreg 0/16, scenes.txt 46/46 identical (dumps + pixels, old vs new mcamui), wf_compare 0/69, chain_compare 0/40, solid_compare 0/38, op_compare 0/135, tool_compare 0/226.

### Screenshots (screenshots/, not committed)
2026-10-21_qt_phase7_qt_*.png and the mcamui references 2026-10-21_qt_phase7_mcamui_*.png (same scripts): tool_manager,
tool_manager_menu, tool_manager_similar_prompt, tool_manager_delete_confirm, tool_manager_save_library,
tool_manager_large_icons, tool_manager_library_only, tool_filter, tool_wizard_type, tool_wizard_geometry,
tool_wizard_drill_std, tool_wizard_properties, tool_wizard_coolant, tool_report, toolpaths_tool_list_report, renumber_tools.

### Notes for phase 8
- The Toolpaths manager menu's remaining front-end items are CM_EDIT (Edit Common Parameters 243: mcamui.c ecp_* is still
  in mcamui) and CM_GRP_RENAME (the input box); `ToolpathsManager::menuHas` greys only those two now. `hook_op_menu` in
  mainwindow.cpp already dispatches CM_TOOLLIST / CM_RENUMTOOLS.
- `make_rstr` / `ci_path` / `op_tool_index` are core now (cui_toolmgr.c): Analyze (az_*) and Edit Common Parameters use them.
- The report window (ReportWindow) draws any `tlr` (rpx.c layout): reuse it for setup sheets / other rpx reports.
- `syncTools()` (mainwindow.cpp) is the pattern for model-driven tool windows; `toolKey()` makes the Tool Manager modal for
  keys; the shot's window stacking is a fixed layer list in testhooks.cpp `shot()` - add new tool windows there.
- Tests: copy PARTS with `cp -L` per scene, point XDG_CONFIG_HOME at a scratch dir holding a copy of paths.conf, copy ui/
  (not a link) - tool_compare.sh `fresh()` does all three.

## Phase 8 (DONE 2026-10-21): everything mcamui still has
Backup of the tree before phase 8: ~/mastercam-re/backups/pre_qt_phase8_2026-10-21.tgz. The old mcamui binary (for the
old-vs-new checks) is copied to the session scratchpad as mcamui_old. Split: **8a** = the core_ui moves (mcamui calls them,
identical behaviour), **8b** = the Qt windows / commands / script steps, **8c** = tests (misc_compare.sh, parity_check.sh),
regressions, plan / report.

### Inventory (mcamui.c + *_ui.inc vs viewer-qt, 2026-10-21) - checklist
Ribbon / toolbar commands (run_local_x) the Qt localRun lacks:
- [x] "Grid Settings" / "Screen Grid" -> mccoreres 1589 Configuration - Screen Grid (draw_gridset)
- [x] "Backplot selected" (toolbar 800 / Toolpaths) -> bp_start (Backplot 14000 + VCR 15000)
- [x] "Verify selected" -> vf_start (the Verify window, ours)
FT functions (run_mc_function: QAT / keymap / right-click menu) the Qt mcFunction lacks:
- [x] OnScreenZoom (F1: zoom window rubber band, prompt "Specify zoom window", Esc ends)
- [x] OnEditCopy / OnEditCut / OnEditPaste (clipboard of entity definitions, Paste prompt + Enter / Esc)
- [x] AnalyzeStart (F4: the pre-selection, else a pick with mccadutilres 6333; Analyze Point / Line / Arc)
- [x] ConfigureX (the grid settings dialog)
- [x] OnFilePrint (INTENTIONAL: a BMP of the window next to the part / project)
- [x] OnHelp (INTENTIONAL: the "Keyboard shortcuts" window = the user's keymap with cmd_labels)
- [x] OnAppAbout (the About apprenticeCAM window, ours)
- [x] doDraft_GLOBALS / run_user_app / AutoSaveParams / ToggleMultiThreadDialog / ViewArtManager: mcamui binds them to a
      no-op (returns 1, does nothing) - greyed in Qt (nothing to port; parity check lists them as no-ops)
Windows / dialogs:
- [x] Backplot: 14000 dialog (collapse / expand 14001, toggles 31001-31007, row 2 16010 / 16011 / 16007 / 16001 / 16002,
      OK / Help, Operation List 17000 + menu 19100, info area Details 13000 (pages 4000 / 4001 / 4002) / Info 4006),
      VCR bar 15000 (Play / Stop / Prev / Back / Fwd / Next stop, Trace / Run, run speed, position slider + stop ticks,
      Conditional stops, Help), Conditional Stops 6000, Backplot Options 1228 (+ the Colors picker for its Select
      buttons), keys S / B / N / P / R, play timer, status bar "X:%1.4f   Y:%1.4f  Z:%1.4f", the drawn motions / tool /
      endpoints in the view (scene.c globals)
- [x] Verify window (ours: Play / Stop / Step back / fwd / end / start, run speed, position slider, status lines), keys
      S / B / R, the stock mesh in the view
- [x] Analyze Entity Properties: mccadutilres 1073 Point / 1080 Line / 6470 Arc (fields, Previous / Next, Propagate,
      AutoCursor picks 5342, OK / Esc commit, Cancel / X restore), the Colors picker (opmanres 593 / 106) and the Levels
      picker (opmanres 563, formats/level_picker.md) with their Select-entity picks
- [x] Edit Common Parameters (opmanres 243) from the Toolpaths manager menu (CM_EDIT)
- [x] Groups > Rename: the input box (draw_ask / ask_ok: CheckGroupName messages)
- [x] Configuration - Screen Grid (mccoreres 1589)
- [x] About apprenticeCAM, Keyboard shortcuts (INTENTIONAL windows, formats/ui.md)
- [x] zoom-window rubber band (1.5 px rgb(20,70,160)), Paste prompt
Backstage (ribbonuires templates): the Qt pages are placeholders:
- [x] Info 107 (+ the host callback values: name / folder / size / units / dates, "Not saved yet"), Convert 105, Print 140,
      Help 115 (About apprenticeCAM live), Community 111, Open (129 Recent Documents / 130 Computer + Browse), Save As (130)
      - drawn from the templates, greyed as mcamui; mcamui bug: its About button calls run_local("About"), which does
      nothing - both apps now open the About window (OnAppAbout, the page's button 1099 = About)
Graphics view / status bar:
- [x] the graphics cross cursor over the view (gafres CURSOR 4, a 17 px inverting cross, generated)
- [x] status bar: the Backplot position text while backplotting
- [-] per-snap cursor glyphs (opmanres cursors 261-714): NOT in mcamui either - stays out
Ribbon:
- [x] gallery tooltips (gallery_tips.tsv: name + the description wrapped at 320 px) - phase 6 leftover
- [x] show:gallery = the palette dropdown open
Toolpaths manager:
- [x] menu CM_EDIT (Edit Common Parameters) / CM_GRP_RENAME (input box) - greyed by menuHas
- [x] toolbar 800 Backplot selected / Verify selected (follow localRun)
Script steps mcamui has and the Qt test hooks lack:
- [x] bp, bpstep, bptool, bpplay, bpopt, bptog, bptab, bph, bpset, bpopen; verify, vfseek, vfplay
- [x] az, azlevel, azcolor, azok, azcancel; ecp, ecpset, ecpok; ask; fsmode
- [x] show:open, show:gridset, show:backstage-new, show:backstage:N, show:top, show:acsnap:K=V, show:acsang:DEG,
      show:gallery, show:sim:N, show:filter:T (dead in mcamui: tm_text has no reader - accepted as a no-op), show:otpmenu
      (Qt: oppopup)
Post: already shared (core_ui post_ops, toolbar 800 "Post selected" live since phase 6; mcamui has no post dialog / NC
window - the program is written next to the part, its path in the prompt). Also ported: mcamui's MCAMUI_CLICKTEST /
MCAMUI_TEST start-up test modes (viewer-qt main.cpp).
Already equal (no work): Levels / Planes managers (phase 1, both read-only as mcamui), Recent Functions, the keymap
dispatch, file drop, the risk agreement, the unsaved prompt, every phase 2-7 window.

### Progress
- 8a (core moves) done: viewer/core_ui/cui_backplot.c (Backplot model, Options 1228 table BPO_CTLS, Operation List rows,
  pages, Conditional Stops, VCR / toggle tables, script steps cui_bp_script), cui_verify.c (Verify model, cui_vf_script),
  cui_analyze.c (Analyze 1073 / 1080 / 6470 + Colors 593 / 106 + Levels 563 pickers, cui_az_script), cui_ecp.c (Edit Common
  Parameters 243 model + the template ECP_243 with bindings, cui_ecp_script), cui_misc.c (Screen Grid 1589, group rename,
  clipboard, zoom window, Print / Help / About flags, backstage tables + Info values, cross cursor, gallery tips, QAT parse,
  misc_dump, cui_misc_script). bp_opt_cfg moved into cui_config_item. mcamui.c / backplot_ui.inc draw over them. Makefile
  (+ WCUI) and viewer-qt/CMakeLists.txt list the five files. Old vs new mcamui: scenes.txt 0/46 (dumps + pixel shots),
  tools/tests/misc_scenes.txt (new, 57 scenes) 0/56 + the fixed crash (below).
- Fixed (shared core, both apps): edit_selected freed a stack CamOp when the edit function skipped a stored op
  (Recalculate feeds/speeds on a stored op with fsmode 2 aborted both programs: "free(): invalid size").
- Mastercam over mcamui (both apps): (1) Groups > Rename = TVM_EDITLABEL, an in-place edit of the group's tree item
  (formats/opmanager_menu.md 3585, OnEndlabeledit@180361440); mcamui's modal "Rename" / "Name:" box (texts not Mastercam's)
  is gone; Esc keeps the label, Enter / focus loss commits; not modal. (2) Backstage Help > About apprenticeCAM opens the
  About window (OnAppAbout); mcamui's button called run_local("About"), which did nothing.
- 8b (Qt) done: src/bpdlg.* (BackplotDialog 14000, BpVcrBar 15000, BpOptionsDialog 1228, BpStopsDialog 6000, VerifyWindow),
  src/dialogs8.* (AnalyzeDialog 1073 / 1080 / 6470, ColorPicker 593 + 106, LevelPicker 563, EcpDialog 243, GridDialog 1589,
  AboutWindow, KeysWindow), backstage.* (every page from the templates, Info values, About live), MainWindow::syncMisc /
  printNow / parityDump / galleryTip / showGalleryPopup, the commands and FT functions, the keys, the view's zoom window and
  cross cursor, the tree rename edit (ToolpathsManager::beginRename), the status bar's backplot text, the play timer, the
  test hooks (every mcamui step + widget steps), shot stacking.
- 8c: tools/tests/misc_compare.sh + misc_scenes.txt (68 scenes incl. 11 w_* widget scenes): 0/74 differ;
  tools/tests/parity_check.sh: PARITY OK (0 failures). Remaining: regressions, screenshots, plan close-out.

### Moved to core_ui (phase 8)
| file | what | moved from |
|---|---|---|
| cui_backplot.c | `bp_on` / `bpl` / `bp_opt` (config 1000 / 19000-19040 via cui_config_item), `bp_start` / `bp_stop` / `bp_toggle` / `bp_keys` / `bp_sync_tool` / `bp_build_lines` / `bp_set_lines` / `bp_advance` (+ `bp_frame`, `bp_frame_still`), `BP_VCR` / `bp_vcr` / slider / speed (15000), `BP_TOGGLES` / `bp_tgl` / `bp_row2` (16000), Conditional Stops `bp_cs_*` (6000), Options 1228 `bpo` / `bpo_update` / `bp_options_open` / `bpo_ok` / `bpo_cancel` / edit rules `bpo_edit_text` / `bpo_edit_commit` / `bpo_spin` + the control table `BPO_CTLS` with `bpo_on` / `bpo_en` / `bpo_click` / `bpo_val` / `bpo_select`, Operation List `bp_tree_rows` / `bp_tree_toggle` / `bp_tree_click` / `bp_tree_expand_all` (17000 / 19100), pages `BP_PG4000/4001/4002/4006`, `bp_det_val` / `bp_info_val` / `bp_info_refresh`, `bp_num` / `bp_time`, `bp_status_text`, `bp_dump`, `cui_bp_script`, `cui_clock` | mcamui.c backplot section, backplot_ui.inc |
| cui_verify.c | `vf_*` (start / stop / seek / apply / snapshots / advance / mesh / tool), `VF_BTNS` / `vf_btn` / `vf_keys` / sliders / `vf_status`, `vf_dump`, `cui_vf_script` | mcamui.c Verify section |
| cui_analyze.c | Colors picker `color_dialog` / `cdlg_*` (593 / 106, `CDLG_593`), Levels picker `level_dialog` / `ldlg_*` / `ldlg_rows` (563, `LDLG_563`), Analyze `AZ_1073` / `AZ_1080` / `AZ_6470`, `az_*`, `analyze_start` / `analyze_pick_ent`, the template helpers `az_template` / `az_hidden` / `az_ctl_y` / `az_readonly` / `az_button_bmp` / `az_static_bmp` / `az_live` / `az_act`, `az_dump`, `cui_az_script` | mcamui.c analyze / pickers |
| cui_ecp.c | Edit Common Parameters: `ecp` / `ecp_txt` / `ecp_open` / `ecp_propagate` / `ecp_valid` / `ecp_apply` / `ecp_ok` / `ecp_cancel` / `ecp_all`, the template `ECP_243` with each control's binding (`ecp_check_ptr`, `ecp_combo`), `ecp_dump`, `cui_ecp_script` | mcamui.c Edit Common section |
| cui_misc.c | Screen Grid `gs_*` + `GS_1589`, the group rename `ask` / `ask_open` / `ask_ok` / `ask_end` / `grp_rename_begin`, clipboard `copy_selected` / `paste_clip` / `edit_copy` / `edit_cut` / `edit_paste`, zoom window, `show_help` / `show_about` / `want_print` / `show_info`, `print_path`, `ABOUT_TEXT`, `cui_cross_cursor`, `gal_tip`, backstage `BS_*` / `BS_ITEMS` / `bs_page_ctls` / `bs_live` / `bs_info_values`, `cui_qat_load`, `misc_dump`, `cui_misc_script` | mcamui.c (clipboard, gridset, ask, backstage, keys / cursor, QAT parse, gallery tips) |

### Qt features now working (viewer-qt, phase 8)
- Backplot (Toolpaths manager toolbar 800 / ribbon "Backplot selected operations"): the 14000 window (collapse / expand,
  toggles, row 2, OK / Help, Operation List with menu 19100, Details / Info pages), the VCR strip 15000 over the view
  (buttons, Trace / Run, run speed, position slider with the stop ticks, Conditional stops, Help greyed), Backplot Options 1228
  at its template positions (+ the Colors picker from its Select buttons, the ClueWnd text as a tooltip), Conditional Stops
  6000, keys S / B / N / P / R, Play on a 20 ms timer, the status bar's position text, the motions / tool / endpoints drawn.
- Verify ("Verify selected operations"): the window, keys S / B / R, Play, the stock mesh.
- Analyze (F4 / AnalyzeStart): Point / Line / Arc dialogs from 1073 / 1080 / 6470, Previous / Next / Propagate, AutoCursor
  picks, Level / Color buttons -> LevelPicker (563) / ColorPicker (593 + 106) with their entity picks.
- Edit Common Parameters (Toolpaths menu) from template 243; Groups > Rename as the tree item's label edit; Screen Grid (1589,
  Grid Settings / ConfigureX); About; the keymap window (OnHelp); Print (OnFilePrint); clipboard Copy / Cut / Paste with the
  Paste prompt; the zoom window (OnScreenZoom, F1) with its band; the graphics cross cursor; gallery tooltips with the
  gallery_tips.tsv descriptions; the backstage's every page from the templates (Info values, Open / Save As sheets, About).
- Test hooks: every mcamui step (bp* / verify / vf* / az* / ecp* / ask / fsmode / show:gridset / backstage:N / backstage-new /
  top / acsnap / acsang / gallery / sim / filter / otpmenu / open) + `paritydump` (both apps) and the core steps new in both
  apps (bpvcr / bpslider / bpspeed / bpcs* / bpo* / bpexp / bptree / bpexpandall / bpok, vfbtn / vfslider / vfspeed /
  vfclose, azact / azpickat / cdlg* / ldlg*, ecpbtn / ecpcancel / ecpdump, rename / askcancel / gridset / gridsnap /
  gridclose / zoomwin / paste); Qt-only widget steps bpdlg[set] / bpodlg[set] / vcrclick / azdlg[set] / cdlgset / ldlgset /
  ecpdlg[set] / griddlgset / renameedit / keyswin (`KEY=V&KEY=V` sets several controls in one step); MCAMUI_MISCDUMP=1 (both).
  MCAMUI_CLICKTEST / MCAMUI_TEST start-up modes in viewer-qt main.cpp.
  2026-11-19 (branch op-rename): core step oprename:N (an op's label edit, cui_misc.c op_rename_begin; ask:TEXT / askcancel end
  it); Qt steps tpslowclick:N (the click on the selected op's label, ToolpathsManager::slowClick), renameesc (Esc in the label
  edit), galitem:Tab@Context|Gallery|Item[#n] (a ribbon gallery item as its click runs it, MainWindow::invokeGalleryItem).

### Changed in both apps (Mastercam over mcamui, phase 8)
1. Groups > Rename = TVM_EDITLABEL: the group's tree item becomes an edit (formats/opmanager_menu.md 3585, OnEndlabeledit
   @180361440 / CheckGroupName); mcamui's modal box "Rename" / "Name:" (texts not Mastercam's) is gone; Enter / focus loss
   commits, Esc keeps the label, not modal. mcamui baseline shot rename_edit_open differs on purpose.
2. Backstage Help > About apprenticeCAM opens the About window (= OnAppAbout); mcamui's button called run_local("About"),
   which matched nothing and did nothing.
Fixes (shared code, behaviour otherwise unchanged): edit_selected freed a stack CamOp when the edit skipped a stored op
(Recalculate feeds/speeds aborted both programs, "free(): invalid size"); Qt F9 now also sets show_info as mcamui.

### Parity status (tools/tests/parity_check.sh, 2026-10-21): PARITY OK, 0 failures
ribbon 89 / 89 on, gallery 70 / 70, toolbars 6 / 6, QAT 7 / 7, keymap 44 / 44, right-click 12 / 12, backstage 9 / 9 + page
buttons 2 / 2; every mcamui script step (25 direct, 15 show:, 12 shared core dispatchers) has its Qt counterpart.
Still mcamui-only (with the reason):
- Ctrl+W / Ctrl+Q (close / quit): mcamui's own keys with no Mastercam source (phase 2 left them out: no improvisation).
- doDraft_GLOBALS / run_user_app / AutoSaveParams / ToggleMultiThreadDialog / ViewArtManager: mcamui binds the keys to a
  no-op; the Qt app greys them (no behaviour in either app).
- mcamui's own file browser window (Qt uses the platform QFileDialog with the same filters) and nuklear's per-frame
  drawing details (pixel layout of its windows) - not features.
- mcamui's show:filter:T (sets tm_text, which nothing reads): accepted as a no-op in Qt.
Cosmetic differences (Qt vs mcamui): Qt widgets / fonts (template-sized captions may clip, e.g. Edit Common's 30-du
"Enable / Disable All Settings" buttons - their bitmaps are NOT DECODED), the windows' positions (as mcamui, ours: the
originals' are NOT DECODED).

### Tests (2026-10-21)
- misc_compare.sh (misc_scenes.txt, 69 scenes incl. 11 w_* widget scenes + 6 saved projects / NC files): 0/75 differ (final run).
- parity_check.sh: PARITY OK.
- old vs new mcamui (the backup's build in the scratchpad): scenes.txt 0/46 (dumps + pixel-identical shots); misc_scenes 57
  model scenes: 2 differ, both expected (rename_edit_open: the in-place edit; fsmode_recalc: the old binary crashed).
- Regressions after the final builds: fileop 0/189, postreg 0/16, scenes.txt old vs new mcamui 0/46 (dumps + pixels),
  wf_compare 0/69, chain_compare 0/40, solid_compare 0/38, op_compare 0/135, tool_compare 0/226.

### Screenshots (screenshots/, not committed)
2026-10-21_qt_phase8_qt_*.png and the mcamui references 2026-10-21_qt_phase8_mcamui_*.png (same scripts): backplot,
backplot_info, backplot_options, backplot_stops, verify, analyze_line, analyze_arc_multi, colors_picker, levels_picker,
edit_common, screen_grid, rename_edit, backstage_info / open / saveas / convert / print / help / community, about,
keymap_window, zoom_window_drag, paste_prompt, toolpaths_toolbar.

### Notes for phase 9 (removing nuklear + the Windows build) - written before it; outcome in "Phase 9" at the end
- Delete viewer/mcamui.c, *_ui.inc (backplot_ui, chaindlg_ui, geopanel_ui, levels_ui, opparams_ui, solidsmgr_ui, toolmgr_ui;
  optree_text.inc is still used by core_ui cui_optree.c), vendor/nuklear*.h, the Makefile's mcamui / windows targets
  (keep cam_cli / tool_cli). The compare scripts (wf / chain / solid / op / tool / misc / parity) then lose their
  reference: keep the last mcamui dumps as fixed baselines first (run each with the Qt app only against stored m/ outputs).
- tools/tests/screenshots/run.py + scenes.txt run mcamui: port to the Qt app (--shot PNG) or retire.
- Windows: viewer-qt CMake with MinGW-w64 or MSVC, Qt 6 + SARibbon + Qt-ADS built for Windows, GLEW / sqlite3, EglView is
  Linux-only (offscreen shots: use QOffscreenSurface + QOpenGLContext there), core_bridge paths (platform.h / paths.h already
  handle Windows), windeployqt for the Qt DLLs; the Makefile's WCUI list shows the core files that must build with MinGW.
- core_ui's remaining mcamui-isms: `cui_hooks` entries mcamui set only (none left unset in Qt), MCAMUI_* env names (keep).

## Solid AutoCursor snap (2026-10-21, both apps through core_ui)
User problem: the Linking page's "Depth..." / "Top of stock..." pick (core_ui lk_click -> geo_snap) snapped only to
wireframe, so the top edge / corner of an extruded block could not be picked. Decoded (formats/ui.md "AutoCursor on
solids"; new dumps decomp/mcgeomsld_snap.c = mcgeomsld sld_search_fast / ps_select / sld_select / ..., decomp/mccore_proj2d.c):
solids are display-list nodes inside the existing passes - Endpoint / Midpoint (Quadrant / Center on circle edges) of the
edges within 10 px of the cursor, front to back, in the feature pass (one running distance with the wireframe), the two
front-most edges as intersection candidates, and the first visible edge in the Perpendicular / Nearest passes; every
candidate passes IsPosVisible when solids are shaded.
- Code: viewer/solid25.c `body_brep_edges` / `bedges_free` (edges rebuilt from the slabs: cap outlines + sharp vertical
  edges, merged through in-line degree-2 vertices; faceted runs flagged `curved`), core_ui cui_snap.c `ac_solids_set`
  (called by bodies_show / solids_clear), `pos_visible` (IsPosVisible), `sld_edge_hits` (ps_select), the solid parts of
  `geo_snap`; script step `snapw:X,Y,Z` (cui_ops.c, both apps: the snap of a world point's screen position).
- Changed for wireframe as decoded: the feature pass is nearest-wins over all feature kinds (it ranked kinds first),
  intersection candidates need sel_dist * 0.5 + visibility, IsPosVisible(1) on feature / tpn / intersection candidates
  (only matters with shaded solids on screen).
- NOT PORTED: circular / curved edges (the kernel facets arcs: only their end vertices snap), Tangent on solids,
  Perpendicular on vertical edges, intersections with vertical edges / across Z planes, translucent hiding, the part
  file's XT solids, the snapped-edge highlight, the selection hover on solid faces / edges / vertices.
- Tests: tools/tests/solid_scenes.txt `ss_*` (extruded 100 x 50 x 50 block, iso view: corner / edge midpoint / hidden
  corner / Linking Depth + Top of stock picks by lkclick and by clickw events / a boss with a merged vertical edge);
  solid_compare.sh now also compares the `snapw`, `linking:` (message left out) and `(params dialog open` lines.
- Screenshots: screenshots/2026-10-21_qt_solidsnap_{qt,mcamui}_{corner,mid}.png (Line Endpoints, cursor over the top
  corner / the right top edge's midpoint: the draw_fast_marker square on the solid).
- Fix found on the way (both apps): core_ui chain_copy_into left Chain.cut0 / cut1 uninitialised; cam_chain_polyline reads
  them, so an Extrude chain could get a garbage start / end cut (a reopened ex_save project rebuilt 2 slabs / 48048.033
  instead of 1 / 46859.447; with a different heap layout Clipper looped forever). The chain is now zeroed first.
- Results: old vs new mcamui - wf 0/69, chain 0/40, op 0/76, misc 1/69 (print_window: the print file's timestamp),
  solid: only the 5 new ss_* scenes + ex_save (the fix above); scenes.txt 0/46 (dumps + shots). Compares: wf 0/69,
  chain 0/40, solid 0/43, op 0/135, tool 0/226, misc 0/75, fileop 0/189, postreg 0/16. No baseline files changed.
- Backup before the change: ~/mastercam-re/backups/pre_solidsnap_2026-10-21.tgz.

## Solid snap 2: round edges (A), solid hover highlight (B), part-file solid edges (C) (2026-10-21, DONE)
Backup before: ~/mastercam-re/backups/pre_solidsnap2_2026-10-21.tgz. Old mcamui (built from that backup) for
old-vs-new checks: the session scratchpad old/viewer/mcamui. Order A -> B -> C (user).
### Progress
- A (round edges on Extrude bodies): code DONE - solid25.h `BArc` carriers on Body / Profile, `BEdge.arc` (1 arc,
  2 ring) with c / ax / xa / r / t0 / t1; solid25.c `seg_carrier`, `arc_edge`, trimmed-arc vertex correction,
  ring-edge loops; cui_feat.c feat_groups fills the carriers; cui_snap.c `arc_pt` / `arc_nearest` / `arc_visible` /
  `arc_ge`, circle-edge Midpoint / Quadrant / Centre fallback, arc intersection candidates, tpn on arcs (tangent too).
  Spec: formats/extrude.md 4.9, formats/ui.md "AutoCursor on solids" (Round edges). New decomp dump
  decomp/mcgeomsld_arcs.c. Scenes ss_cyl_top / ss_cyl_link / ss_cyl_click_quad / ss_cut_hole / ss_cut_through.
  solid_compare 0/48; old vs new mcamui: only the 3 new snapw arc scenes differ. wf 0/69, chain 0/40, op 0/135,
  tool 0/226 (regress run A). Screenshots 2026-10-21_qt_solidsnap2_A_{qt,mcamui}_cyl_{quad,center,mid}.png.
- B (solid hover highlight): decoded (formats/ui.md "Solid selection hover highlight", decomp/mcgeomsld_hover.c);
  code: cui_sel.c `sld_hover_body` in cui_hover_update, cui_overlay.c draws the body's edges in the select colour
  during Select Target Body. Scenes ss_hover_target / _idle / _chain (sldhover lines, MCAMUI_EXDUMP).
- C (part solids): xt_mesh.c `xt_brep_edges` (EDGE/LINE/CIRCLE -> XtBEdge), cui_snap.c `part_edges_sync` (edge
  sets after the Extrude bodies), `part_hit` (IsPosVisible against the part mesh), scene.c `scene_part_tris`.
  Scenes ss_part_bracket4 / ss_part_nakto / ss_part_link (solid_compare.sh copies the parts into OUTDIR/PARTS).
- Final regressions (after B + C builds, regress run BC): wf 0/69, chain 0/40, solid 0/54 (48 + ss_hover_target /
  _idle / _chain, ss_part_bracket4 / _nakto / _link), op 0/135, tool 0/226, misc 0/75, fileop 0/189, postreg 0/16;
  mcamui scenes.txt new vs old mcamui 4/92 shots (backstage_info: the scratch part copy's file dates; tool_list_report:
  the clock). NOTE: the PNG baselines in tools/tests/screenshots are stale (67/92 differ for the OLD binary too) -
  compare against a run of the backup's mcamui instead. Old vs new mcamui solid scenes: only the new scenes +
  ex_pick_target (one new `sldhover 0` line) differ.
- Screenshots: 2026-10-21_qt_solidsnap2_B_{qt,mcamui}_hover_target.png, _C_{qt,mcamui}_bracket4_{edge_mid,arc_center}.png.
- Baselines changed: none (solid_compare.sh: pick() also compares `sldhover` lines and copies the two parts to
  OUTDIR/PARTS; solid_scenes.txt +11 scenes).
- Open / NOT DECODED: PK's ring-edge ends (no Endpoint on full circles) and its interval normalisation (Mastercam's
  FUN_180043850 rule used), the GAF AutoHighlighted look and highlight width, face / edge solid picks (no command here
  asks for them), XT circles with sense '-' and other curve classes, display-list order of part solids vs wireframe.
  viewer-qt/build-asan not rebuilt in this round.

## Solids chaining (2026-10-22, both apps through core_ui)
User goal: pick edges / loops / faces of a solid as toolpath chains (Chaining dialog Solids mode, template 6518).
Backup before: ~/mastercam-re/backups/pre_solidchain_2026-10-22.tgz. Old mcamui for old-vs-new: the session scratchpad
old/viewer/mcamui (built from that backup). Decode: formats/chaining.md "Solid chaining" (new dumps
decomp/mcgeomsld_solidchain.c, decomp/mccore_sldchain_misc.c, exports/mcgeomsldres_dialogs.tsv / _strings.tsv).
### Progress
- DONE decode: who may chain solids (allow_solid_chains callers), 6518 controls / images / handlers / FUN_1801f00d0's
  solid branch, the selection state machine (OnSolidsSelect*, FUN_1800a44c0), picks (edge / Linked edges / loop /
  partial loop / face, toggles, the Pick Reference Face dialog 134 and the 3D / Face radio), sld_loop_to_chain (fin walk,
  start, direction), chain creation + insertion order, commands (Reverse, Start Back / Forward, Unselect, Unselect All,
  Last, End chain, OK, Wireframe, Esc), highlight colours (config 5034 / 5035 bound by mccore FUN_1801d3400), prompts.
- DONE code: viewer/btopo.h (face / loop / fin topology), solid25.c `body_brep_topo` (ours, from the slabs; the edge
  build now records segment -> edge), xt_mesh.c `xt_brep_topo` (XT order); core_ui/cui_sldchain.c (CH_6518, SREF_134,
  state, picks, chains with Ent.gen curves, overlay, arrows filter, dump / script steps sldclick:X,Y,Z / sldrefother /
  sldrefok / sldesc / slddump / sldok); cui_chain.c hooks (template, chain_act routing, 351, Unselect / Last / Reverse
  on the dialog's wireframe chains only, OK sorting wireframe chains then re-inserting solid chains at their positions,
  chain_radio_on, chain_dlg_icon, chain_escape, prompt count = wireframe chains); cui_snap.c `sld_edge_pick`,
  `ac_solid_sets` / `ac_solid_body` / `ac_solids_gen`; cui_sel.c hover + config 5034 / 5035; cui_overlay.c sld_overlay;
  cam2d chainable() skips Ent.gen; Chain.sld; projects keep solid chains as `CHAIN c L G...`; mcam_saveops skips ops
  with solid chains (SldToolPOp writing NOT DECODED); mcamui chaindlg_ui.inc (`sld_ref_draw`, radio / icon / height from
  the template), Esc; Qt chaindlg.cpp (rebuild on template change, `RefFaceDialog`), mainwindow syncChain / Esc;
  events clickw / movew take an optional Z (both apps).
- Tests: tools/tests/chain_scenes.txt sc_* (23 scenes: block loop near / far / side, reference face Other, Start / Reverse,
  loop toggle, Face radio, partial loop + Esc, edges, linked edges + End chain, face picks, hole ring, hole wall face,
  Unselect All, wireframe + solid mix + OK, Esc, Last / no Last, Solids enabling per op, BRACKET4 face / loop, hover +
  clicks by events); chain_compare.sh copies BRACKET4 into OUTDIR/PARTS. tools/tests/op_scenes.txt op_sld_* (contour on
  the block's top loop: depth inc -5 -> posted Z45; pocket in a through hole's top ring: depth -20 -> Z30; BRACKET4
  top-face contour; save + reopen + post).
- Results (final builds, 2026-10-22): chain_compare 0/63 (40 + 23 sc_*), op_compare 0/148 (incl. op_sld_*), wf 0/69,
  solid 0/54, tool 0/226, misc 0/75, fileop 0/189, postreg 0/16; mcamui tools/tests/screenshots/scenes.txt new vs old
  (backup build): 0/92 shots pixel-identical, 0/92 dumps (paths normalised); viewer-qt/build-asan rebuilt, the sc_* chain
  scenes and the op_sld_* scenes run clean under ASan (leak check off).
- Baselines changed: none. Test files extended: chain_scenes.txt (+23), op_scenes.txt (+4), chain_compare.sh (copies
  BRACKET4 to OUTDIR/PARTS), tools/tests/README.md.
- Screenshots: screenshots/2026-10-22_qt_solidchain_{qt,mcamui}_{loop_hover,ref_face,hole_loop,linked,part_face,
  contour_toolpath}.png (block loop picked + hover edge, the Pick Reference Face dialog with the front face's loop,
  the through hole's top ring for Pocket, a Linked edges run, BRACKET4's top face picked (wireframe: config 5035 red),
  the contour toolpath at Z45 on the block's top loop).
- How to use: Toolpaths > Contour / Pocket / Facing / Slot / Dynamic Mill / Area Mill / Dynamic Contour / Peel Mill with
  a solid on screen (an Extrude body or the part's solid) -> the Chaining dialog's Solids button -> pick an edge (a loop
  of the face nearer the Top plane: the Pick Reference Face dialog shows it, Other face / OK) or a face (all its loops),
  or switch Loop off and pick Edge / Linked edges, Partial loop for a stretch of a loop; Reverse / Back / Forward /
  Unselect / Last as in Mastercam; OK -> the op dialog: the Linking depths see the chain's Z (incremental depths
  relative to the solid's edge).
- Open / NOT DECODED: see formats/chaining.md "Solid chaining" (face fill highlight, face hover look, non-line / non-XY
  curves, PK face order taken from the fin sense, FUN_1800ba300's face point on curved faces, lathe 6519, Help, the
  reference dialog's position, writing solid-chain ops into a Mastercam part).
### Notes for the next task (Stock Setup + WCS / Tplane planes)
- The reference-face default uses GetActivePlane's Z (+0xa8); here the Top plane's (0, 0, 1) in cui_sldchain.c
  `nrm_dot` - take the active plane's normal there once planes exist.
- Solid chains are world-coordinate Ent.gen curves (XY arcs only); a Tplane other than Top needs them (and wireframe
  chains) mapped into the plane, and the chain depth (open_params: the first entity's Z) taken along the plane normal.
- Stock Setup "from solid" / bounding box can use the same edge sets (cui_snap.c ac_solid_sets / ac_solid_edges) and the
  face triangles of btopo (solid25.c body_brep_topo / xt_mesh.c xt_brep_topo).

## Stock Setup + WCS / Tplane planes (2026-10-22)
STOPPED at a handoff request (part A done and tested; part B half done, its page / UI not wired). Backup before any change:
~/mastercam-re/backups/pre_stockplanes_2026-10-22.tgz (viewer viewer-qt formats NOTES.md tools). Old mcamui built from
that backup for old-vs-new compares: session scratchpad `old/viewer/mcamui`
(<scratch>;
/tmp is RAM - rebuild from the backup with `make mcamui` if gone).

### Decoded (new dumps)
- decomp/mastercam_stocksetup.c (mastercam.dll, CGroupPg3 = Machine Group Properties > Stock Setup page, opmanres dialog
  1590; 6488 is the wire variant): vtable @1805ef4c0; message map built at run time in FUN_1801ee0f0 (ids: 25143 Display,
  1252 Fit screen, 518/520/522/523 shape radios, 25070-25072 axis, 25077 NCI extents, 1219/1221/1223 size X/Y/Z,
  1215/1230/1231 origin X/Y/Z, 1227 picture click = corner hit test FUN_1801f0070, WM_DRAWITEM FUN_1801ef4e0 (corner
  marker), 528 select solid FUN_1801f0fc0 (select_ent mask 0x10000, prompt opmanres 2798), 1253 / 364 STL file, 530/532
  Wire frame / Solid, 25452 Select corners FUN_1801f1840 (point_proc, prompts opmanres 1447 / 1448, rubber band; Esc after
  the 1st point = origin from it), 25451 Bounding box FUN_1801f1150 (-> mccad create_boundingshape_either_method = the
  Bounding Box function panel), 1243/1244/1245/1246 All Surfaces (mask 0x100) / All Solids (0x10000) / All Entities
  (0x1905ff) / Unselect All -> FUN_1801f26d0 -> mccad create_bbox_stock_no_dlg, 1241 Select origin FUN_1801f16f0
  (select_point), 960 Select stock plane, 734 Use Machine Tree); DDX FUN_1801ed460; OnInitDialog FUN_1801ef9d0 (bitmaps
  831 / 938 / 929 on the buttons, picture ids 832-839); group->page FUN_1801f2000; page->group (apply) FUN_1801ed0b0; OnOK
  FUN_1801f0030; enable rules FUN_1801ed700; picture index FUN_1801f2980 (rect 0/1 wire/solid, cyl X 2/3, Z 4/5, Y 6/7 ->
  opmanres BITMAP 832+i); FUN_1801ee060 corner k -> button 1294+k; box -> size/origin FUN_1801edd80; draw_stock 0x1803360a0.
- decomp/mastercam_grpprops.c: EditGroupPropertiesDlg (the sheet; title opmanres 3851, tabs 3852 Files / 3853 Tool Settings
  / 3855 Stock Setup / 3856 Safety Zone; mill page templates 0x32f / 0x669 / 0x636=1590 / 0x637 - CORRECTED 2026-10-22 in formats/grpprops_tabs.md: mill uses 0x32f / 0x635 / 0x636, 0x669 is the wire page, and Safety Zone (0x637) is never built: RunningSafetyZone() returns false, a mill group has three tabs). Opened by COpMgrTree::
  Selchanged on the Properties children -0x10..-0x13 -> EditGroup(page 0..3) (decomp/opman_tree_all.c:41730).
- decomp/mccore_stock.c: stock_ok (rect: X,Y,Z >= tol; cyl; solid id > 0; file exists), stock_calc_geom (12 world lines at
  +0x1380, verified bit-exact against all 20 machine groups of the user's parts), FUN_1800d5a20 = stock_calc_center,
  calc_top_of_stock (max Z of the lines in op coords), bnci_wr_1020. job_setup_stock_corners already in
  decomp/mccore_tpsd_helpers.c. InitOperation (mccore_ops.c:2425): the FIRST op (empty op list), absolute top -> top of stock.
- decomp/mccad_bbox_stock.c: create_bbox_stock_no_dlg (2D mode asks mccadres 17 "Override 2D construction mode?" / caption
  30; constr_view_is_3d = config 16009, mccore FUN_1801ba870 case 0x3e89, .data default 1), create_boundingshape_either_method,
  make_bounding_box / add_to_minmax / FUN_180057940 (entity extents; arcs via CalculatePointsOnCurve, chord = config 1006
  curve_chord_dev). mcmill facing_op_uses_stock / make_facing_pock_bound (mcmill_all.c:102249): Facing w/o chains faces the
  job_setup_stock_corners rectangle at its top Z. Stock colour: config 5029 = mill_stock_color (FUN_1801d3400 0x13a5), line
  style 4 (pattern 0x333f).
- group_pg3 = op_group +0x1228: +0x1228 Fit screen, +0x1230/38/40 size, +0x1248/50/58 origin (stock-plane view coords),
  +0x1260 Display, +0x1261 shaded, +0x1264 shape (0 rect 1 cyl 2 solid 3 file), +0x1268 solid id, +0x126c axis,
  +0x1270 centre on axis, +0x1271 STL path, +0x1380 12 lines (0x30 each), +0x15c0/+0x1628 cylinder arcs, +0x1690 corner,
  +0x16a8 Use Machine Tree; +0x8a8 group_view stock plane (i16 plane id, i16 view no, +8 matrix, +0x50 origin).
- Planes (part B): decomp/mccore_opview.c: wpt_to_op = world_to_view(Tplane view, p - Tplane origin) (op_view at op
  +0x570: +2 view no, +8 matrix rows, +0x50 origin, +0x68 plane id; Cplane +0x620, WCS +0x6d0 = record 0x5d8/0x688/0x738),
  op_to_wpt inverse; NCI and the engines are in op coords (callers: get_chain_depth_op, tp_select_depth, calc_top_of_stock,
  bnci_create_zfeed_arcs). decomp/mccore_planespage.c: PlanesPageBase (dialog 6547) DDX / InitPage / RefreshData / Enable*;
  work offset = Tplane op_view +0x92 (record 0x66a; -1 Automatic in every user op). Generic Fanuc 3X Mill.pst pwcs:
  workofs < 6 -> G(54+n), else G54.1 P(n-5). Plane Selection = opmanres 842; Planes manager = opmanres 457. No user part
  has a non-Top plane (no data oracle).

### Code done (part A, both apps, tested)
- viewer/mcam_load.c/.h: McamFile stock_rec / fit / disp / shaded / shape / solid / axis / center / mtree / file / stock
  plane (vid vno vm vo) / stock_lines / stock_dirty (first machine group record).
- viewer/core_ui/cui_stock.c (+ core_ui.h section, Makefile, viewer-qt CMakeLists): ST_1590 template, model, enable rules,
  corners, Select origin / corners picks, All Surfaces / Solids / Entities / Unselect All (bbox), 2D question (st_q2d),
  stock_ok / stock_box / stock_calc_lines / stock_top / stock_facing_box, script steps stocksetup / stclick / stedit /
  stcorner / stpick / stesc / st2d / stok / stcancel / stdump.
- cui_overlay.c: stock drawn in the dash buffer (style 4, colour 5029). cui_sel.c: config 5029 / 16009 / 1006.
- cui_ops.c: Facing w/o chains uses the defined stock (was the solids' bbox - removed); first-op top of stock;
  cui_chain.c Facing OK test uses F.stock. verify.c: rect shape only.
- mcam_saveops.c stock_patch: group_pg3 fields + lines written back when stock_dirty (91 bytes differ on BRACKET4, lines
  verified); "nothing to add" no longer refuses a stock-only change. cui_doc.c: project line STOCK.
- UIs: mcamui.c draw_stock_setup / draw_stock_q2d, tree rows open it, Esc / picks; viewer-qt/src/stockdlg.* (StockSetupDialog),
  managers.* propsRow, mainwindow syncMisc / clicks / Esc / 2D QMessageBox, testhooks stdlg / stdlgset + cui_stock_script.
- Tests: tools/tests/stockplanes_compare.sh + stockplanes_scenes.txt (14 stock scenes): 0/19 differ. Regression run A
  (before the B edits): wf 0/69, chain 0/63, solid 0/54, op 0/148; tool / misc / fileop / postreg / scenes.txt were NOT
  finished (run killed at the handoff). Screenshots not yet copied to screenshots/.

### Part B (planes): DONE 2026-10-22 - see "Progress: planes (part B)" below
(The "Half done" state and the "Next steps" list that stood here were carried out; what stays NOT DECODED / NOT
PORTED for planes is listed in that section. The handoff's Stock Setup list - solid / STL stock, stock plane Select, Use
Machine Tree, shaded stock display, the corners rubber band, Select origin prompt ("*"), Fit screen effect, the sheet's
other pages / Apply / Help - stays for a later phase; cylindrical stock, NCI extents, the Bounding box panel and the
active machine group were ported since, see the progress notes below.)

### Progress (session master-cam-reverse-9a, 2026-10-22, after the handoff)
- Verified cui_stock.c against the decompiles function by function + the 23 stored stock line sets (formats/mcam.md
  "Stock Setup" -> Verification). Backup before the porting below: ~/mastercam-re/backups/pre_stockport_2026-10-22.tgz.
- **Active machine group = the LAST machine group** (COpMgrTree::LoadTree puts the insert arrow after the last tree item):
  mcam_load.c reads the Stock Setup + tool settings from it, mcam_saveops.c stock_patch writes that group (by id). BRACKET4 /
  NAKTO / thread-test / tramming-rod now show Machine Group-2's stock (scenes.txt: 32/46 shots differ from the old build -
  the stock outline only; dumps 0/46).
- Ported: Cylindrical stock (geometry, display with end circles, stock_ok, save of +0x126c / +0x1270 / the a_3d circles,
  project STOCK line + axis / centre, Facing circle boundary via CamParams use_circ / circ), the bounding cylinder for All
  Surfaces / Solids / Entities (decoded by subagent: decomp/mccad_bcyl*.c), Center on axis, NCI extents (nc_post.c
  nc_extents; decomp/mccore_bnci_extents.c, mccore_bnci_wr_1013.c, mccore_op_is_tip_comp.c, mccore_activegroup.c).
- Tests: stockplanes_scenes.txt +11 (st_cyl_*, st_bcyl_*, st_nci_extents*): 0/34. Full regression run_9b: wf 0/69,
  chain 0/63, solid 0/54, op 0/148, tool 0/226 (toolmanager.conf key names decoded), misc 0/75, fileop 0/189, post 0/16.
- Found, not fixed: the Contour chain prompt counter differs between mcamui and Qt after chainat ("chain 1" / "chain 2");
  the exporter rewrites tp_hdr +0x6c (35 vs Mastercam's 36 on BRACKET4); viewer-qt/build-asan not rebuilt.
- Still NOT PORTED: ~~Bounding box button~~ (ported, see below), Solid / STL stock, stock plane Select, Use Machine Tree,
  shaded stock, the 2D HS regen marking; then part B (planes).

### Progress: Bounding Box function panel (2026-10-22)
- Backup before: ~/mastercam-re/backups/pre_bboxpanel_2026-10-22.tgz. Spec decoded the same day (formats/wireframe.md
  "## Bounding Box", which also fixes the earlier axis-code note: 0 = Z, 1 = Y, 2 = X).
- New viewer/core_ui/cui_bbox.c (Makefile, viewer-qt CMakeLists): Wireframe > Bounding Box and Stock Setup's Bounding box
  (25451) run the same panel model: modal statics, entity step (Manual = selection + Enter, All shown), compute with
  resize / 2D rules / Volume, codes 0 / 1 / 2 / 4 / 5 / 7 / 8, output FUN_18005a2e0 / FUN_18005b220, stock writes and the
  page result. cui_stock.c: stock_bbox_sel / stock_bcyl_sel (the decoded point rules with an entity filter),
  st_bbox_panel_result, st_point_pick, ST_PICK_BBOX (the sheet hidden while the panel runs). CuiPanelOps gained `tab`
  (Basic / Advanced: mcamui draws the shown tab, the Qt GeoPanel one page per tab). Preview = the outline in the live
  colour (ours, no live solid). Greyed: Push Pull, Solids, Create Tool Plane; X / Y axis circles not created (all ported since: "Bounding Box rest + arcs with a view").
- Tests: wf_scenes.txt bbox (rewritten: its output changed as expected) + 9 bbox_* scenes; stockplanes_scenes.txt +4
  st_bboxpanel_*; the compare scripts also compare the `bbox` dump lines. Steps (ours): bbenter, bbdump, constr3d:N,
  gpset:bb_*=V; geobtn:7 = Select Entities.
- Regression run_bbox: wf 0/78, chain 0/63, solid 0/54, op 0/148, tool 0/226, misc 0/75, stockplanes 0/39, fileop 0/189,
  post 0/16, scenes.txt dumps 0/46 (shots 31/46 vs the pre-stock old build: the stock outlines, as before).


### Progress: planes (part B) (2026-10-22, both apps through core_ui)
- Backup before: ~/mastercam-re/backups/pre_planes_2026-10-22.tgz. New decompiles: decomp/mastercam_plane_convert.c
  (Plane::Init / Convert / InitOperationView / SetWorkOffset, SelectPlaneFromList, FUN_18043c260),
  decomp/mccore_updateview_wofs.c (CUpdateViewDialog, PlanesPageBase::InitPage / GetPlaneName),
  decomp/mastercam_planeselect_dlg.c (PlaneSelectListDlg), decomp/mcgeomsld_pickloop_activeplane.txt (new Ghidra script
  tools/ghidra_scripts/CallContext.java: GetActivePlane's PlaneType = 1 Tplane in sld_array_pick_loop_add).
- core_ui/cui_planes.c reviewed against the decompiles and rewritten (in Makefile mcamui + WCUI, viewer-qt CMakeLists):
  Planes page 6547 (PG_PLANES enabled in pg_done / pp_page_body), Plane Selection 842 (PLS842), Update Plane 10269
  (UPL10269), the copy buttons with SetToolPlane, the work offset rules (EnableDisableWorkOffset with the defaults library
  op's record 0x66a, CheckWorkOffset, MapToOp at OK = planes_map_to_op), Quick view "Cplane / Tplane" = the Tplane name.
  Corrections to the earlier draft: Manual no longer forces 0 (it takes the plane's -1 / the defaults op's value);
  selecting / copying into the Tplane resets the work offset to the plane's (Automatic). Spec: formats/mcamui_dialogs.md 1.x.
- PpOps.button / radio filled: mcamui opk_button / opk_radio + draw_planes_dialogs (opparams_ui.inc); Qt qp_button /
  qp_radio + PlanesDialog (opdlg.cpp), MainWindow::syncOps shows them; the page walkers PS_OPS / PL_OPS (cui_ops.c) too.
- mcam_saveops.c writes the three op_views + work offset (formats/mcam_write.md 11) for non-default planes and edited
  Mastercam ops; cam_fileop.c reads them (system planes by matrix; another plane = stays as stored); chain depth in op
  coordinates. Layout: formats/mcam.md "Operation planes".
- cui_sldchain.c nrm_dot: |normal . active Tplane Z| (cui_active_tplane: Top, active-plane switching NOT PORTED).
- cam2d.c: point-based ops in a Tplane whose Z is not world Z are refused (Hole has no Z).
- Fixed on the way: Qt's "Save as MCAM part" (core_bridge.c qc_save_part) passed no ops to mcam_save_part (mcamui
  passes camops): the ops made here were never exported from the Qt app. mcamui's chainat step now updates the chaining
  prompt as the next frame / the Qt step does (the "chain 1" / "chain 2" difference of the handoff).
- Ours (deviations): the page is row-laid (template captions on the bitmap buttons, origins as labels with 4 decimals,
  the work offset edit only while Manual); the WCS / Cplane op_view +0x92 is derived (the Tplane's when the same plane,
  else -1); the Update Plane dialog's choices 0 / 1 are greyed but 0 stays the preselected default (the op gets the
  value in every branch); the 3-axis post's refusals (Tplane != WCS; Automatic with a WCS other than Top) and their texts.
- NOT DECODED / NOT PORTED: planes other than the seven system planes (MastercamPlanes archive), Select Point / typed
  origins, display relative to the WCS + PlanesWorldCoords, Plane Selection columns / icon / new origin / Help, Update
  Plane choices 0 / 1 + registry checkbox, Automatic work offsets other than G54 (BuildWorkOffsetMap), Contour's
  set_ctour_type after a plane change, active-plane switching, arcs / points outside the Tplane, rotary output.
- Arcs with a view (item 5) NOT done (done since: "Bounding Box rest + arcs with a view"): Ent arcs carry no plane - a non-XY arc is drawn from start / end / centre, which
  fails for full circles; needed: an Ent view number + matrix (A_ID +0xc8, centre and angles in view coordinates,
  formats/mcam.md), drawing / snapping / chaining from it, mcam_saveops writing +0xc8 and the view centre, cam_generate
  mapping arcs whose view normal is the Tplane Z, then the Bounding Box X / Y axis circles (views 5 / 2).
- Tests: tools/tests/planes_scenes.txt (13 scenes) + planes_compare.sh (through stockplanes_compare.sh, which now also
  compares opctl lines, handles multi-line prompts and compares saved parts via mcam_norm.py): 0/22.
  Regression run_planes: wf 0/78, chain 0/63, solid 0/54, op 0/148, tool 0/226, misc 0/75, stockplanes 0/39, fileop
  0/189, post 0/16, scenes.txt dumps 0/46 (shots 31/46 vs the old build: the known stock outlines).

### Planes: review fixes (2026-10-22, backup ~/mastercam-re/backups/pre_planesfix_2026-10-22.tgz)
1. Loader (cam2d.c cam_planes_from_record, used by cam_fileop.c and mcam_saveops.c): a view is taken only when matrix,
   stored id (+0x68) and a zero origin all name one system plane (FUN_1802cfb60 resolves by id first); else the op stays
   as stored (checked on patched BRACKET4 copies: origin 50,25,0 and id 12 are refused).
2. Defaults' 0x66a for new ops: NOT applied - InitOperation (decomp/mccore_ops.c l.1770) overwrites the defaults op's
   views with the active planes (InitOperationViewsToActivePlanes -> Plane::Convert, -1) and fills an Automatic Tplane
   offset from GetMatchingWorkOffset (NOT DECODED); data: every new Dynamic op in the user parts holds -1. New ops are now
   always written with their own views (the defaults record's 0 was kept before).
3. Update Plane OK enabled only with choice 2 (0 / 1 greyed, 0 the default selection).
4. Per-view work offsets: CamParams wwofs / cwofs (default -1, PKEYS_PL), ResyncOpViews, SetToolPlane, MapToOp ported;
   WCS -> Tplane now returns Manual to Automatic as Mastercam does.
5. Quick view row = the Cplane's name (RefreshData rewrites GetCustomizableData's Tplane name).
6. Every Select Plane / copy re-derives Manual from the Tplane's +0x92 (SetToolPlane).
7. Edited Mastercam ops: plane records rewritten only when changed; the Iso constants are mastercam.dll's own.
8. WorkOffsetInUse text: nothing is appended because mccoreres 5605 is empty (decoded guard), documented.
9. CamParams wofs default -1 (was 0: plwofs:0 on a new op was a no-op).
Also found: project files wrote PKEYS_PL / PKEYS_FC but cam_load_ops never read them back (planes and the Facing
circle were lost on reopen); now read through cam_set_param. New scenes: pl_wcs_to_tplane_resets, pl_manual_minus1_select,
pl_mapto_views.

## Stock Setup rest (2026-10-22, both apps through core_ui)
Backup before: ~/mastercam-re/backups/pre_stockrest_2026-10-22.tgz (source tree incl. decomp/). Spec: formats/mcam.md "Stock
Setup" ("Solid / File shapes, the stock model, ..." and the corrected regen rule). New dumps: decomp/mastercam_stock_fitshade.c
(FUN_180337650 fit extents, gl_Copy_Color, FUN_180229750 material, FUN_180136e60 normal), decomp/mccore_stl_read.c (STL reader,
VerifyFileSuffix, FreeShadedStockModel), decomp/mccore_stl_strings*.txt ("solid" / "SOLID", ".stl", "." , "\n\n"),
decomp/mastercam_gltol.c (gl_tolerance 0.001 in / 0.025 mm), decomp/mastercam_multmat33.c + _call.txt, decomp/systypes_matmul.c
(new Ghidra project ~/mastercam-re/ghidra_systypes: SYSTYPES operator* = second . first), decomp/*_writes.txt; invocations in
~/mastercam-re/regress/ghidra_stockrest*.sh.

### Ported (sources in the code comments)
1. Solid stock (522 / 528): select_ent mask 0x10000 (FUN_1801f0fc0) -> the part solid's SOLID_ID ent_idn; page-load / OK checks
   (FUN_1801f2000 / FUN_1801ed0b0); stock_ok id > 0; the model from the solid's mesh; saved at group +0x1268.
2. File (STL) stock (523 / 364 / 1253): FUN_1801ef850 browse (mcamui fd FD_STOCK_STL, Qt QFileDialog; "STL Files (*.stl)" /
   "All Files (*.*)"), VerifyFileSuffix + file_exists, the combo shows the file; the decoded ASCII / binary STL reader; stock_ok's
   missing-file message + reset; FUN_1801f2000's "Cannot open file" message; saved at +0x1271 (only when changed).
3. Stock model + display: CreateShadedStockModel for all four shapes (box index table, cylinder 4n triangles with gl_tolerance),
   draw_stock's triangle outlines (shapes 2 / 3) and FUN_18033fe70's translucent shaded model (scene.c scene_set_stock_mesh,
   alpha 0.375, flat normals); Fit screen = FUN_180337650 (scene.c scene_fit_extra hook <- cui_stock.c stock_fit_points).
4. Use Machine Tree: NOT DECODED (CCNCMachine / CMachineTreeCtrl machine definition), stays greyed.
5. 2D HS regen marking: the earlier "+0x2380 / +0x2ec0 NOT DECODED" was a misreading (Ghidra long = 4 bytes): op +0x11c0 /
   +0x1760 = record +0x1228 (sub-style < 2) / +0x17c8 (MP 41144 set). FUN_1801e1a20 change test after the sheet (OK and Cancel)
   + Mark2dHSTOpsRefMachStockDef4Regen on the stored Mastercam ops of the active machine group: stale (dirty icon, post refuses)
   + Op.regen -> the part save writes record +0xfc = 1 / +0xff = 0; project line REGEN id. (Review fix: no longer stale - see
   "Stock Setup review fixes": the marked ops are posted from their stored NCI.) No real part / defaults library has
   +0x17c8 set (tested on a patched copy, tools/tests/stock_fixtures.py).
6. Stock plane Select (960): FUN_1801f1d70 through cui_planes.c's Plane Selection 842 (pls_which 3; mcamui draw_planes_dialogs /
   Qt plsDlg_ shown for the sheet too); Plane::Convert into the group at once (Cancel keeps it, decoded); every conversion of the
   page / model through the stock view (stock_w2v / stock_v2w); the cylinder axis view R . plane; saved group_view (+0x8a8, 0x68
   bytes) when chosen here; project STOCK line field. Seven system planes only.
Also: project STOCK line gained plane / solid / Use Machine Tree / file fields (older lines still read); verify.c uses the box of
the lines for Front .. Left stock planes (ours).

### Ours / NOT DECODED (also in cui_stock.c's header)
Ours: solid pick = ray / triangle test on this program's mesh of the part solids (select_ent NOT DECODED); solids made here
cannot be stock (no SOLID_ID id; Parasolid writing NOT DECODED); the solid model = this program's mesh (sld_to_stl NOT DECODED);
the shaded model's lighting (gl_Init_Lights_E NOT DECODED); group_view +4 (the plane's +0x348) written 0; Bounding box (25451)
greyed with a non-Top stock plane (the panel works in Top only); Verify's stock box for non-Top planes; script steps stfile:,
stsolid:, stsolidw:, stsolidat:. NOT DECODED: Use Machine Tree, the STL default folder (stl_prefix) and the combo list, planes
other than the system planes, cylinder axis views that are no standard view, the Select corners Esc Z for non-Top planes, the
meaning of MP 41144, VerifyFileSuffix's case rule (case-insensitive here).

### Tests
tools/tests/stockplanes_scenes.txt +20 (st_solid_*, st_stl_*, st_shaded_*, st_fit_screen, st_plane_*, st_regen*,
st_mtree_greyed); stockplanes_compare.sh runs tools/tests/stock_fixtures.py in each work folder (STL boxes + PARTS/b4regen.mcx-9);
the stdump closed line is followed by a "stocksetup model" line (plane, solid, file, triangles, lines, shaded, fit points, regen
marks + the model's box). Screenshots: screenshots/2026-10-22_stock_{solid_shaded,solid_wire,stl_shaded,plane_front}_{mcamui,qt}.png.
Regression run_stockrest (~/mastercam-re/regress/run_stockrest.sh, summary.txt): wf 0/78, chain 0/63, solid 0/54, op 0/148,
tool 0/226, misc 0/75, stockplanes 0/66, planes 0/27, fileop 0/189, post 0/16, scenes.txt old (pre_stockrest build) vs new:
dumps 0/46, shots 1/46 (backstage_info: the part copy's file times). The user's parts are unchanged in view: their stocks lie
inside the fitted extents, so Fit screen moves no scene shot.

## Stock Setup review fixes (2026-10-22, backup ~/mastercam-re/backups/pre_stockfix_2026-10-22.tgz, source only)
Review items (independent review) and the user's large-STL requirements. New dumps: decomp/mastercam_stlname.c +
_call.txt (FUN_1801df580 = build_filename_s, its arguments), decomp/mccore_filename_parts.c (GetFilePrefix / GetFileName /
dir_exists / build_filename_s / reduce_filename), decomp/mastercam_post_dirty.c (OMpostOperations, COpMgrTree::PostSelected,
DirtyOperationWarning), decomp/mastercam_dirtywarn_dlg.c (CDirtyOpsDlg = opmanres dialog 977); invocations
~/mastercam-re/regress/ghidra_stockfix.sh.
1. Allocations: the stock model is counted, allocated once (size_t + overflow checks) and checked, as CreateShadedStockModel
   (count loop, one calloc each, give up when either fails); STL files are read twice (count / fill). No more per-element
   realloc; every malloc / calloc / realloc on the path is checked; failure = no model (never half-built) + a message (ours).
2. ASCII reader: lines read into a zeroed 0x150-byte buffer with at most 0xff characters (FUN_1806d0aa0's own buffer), so
   the p + 7 scan after "vertex" stays inside it; long lines are read on in 0xff pieces (no growth).
3. st_open now ports FUN_1801f2000's file rebuild (prefix / forced ".stl" / name / build_filename_s, written to the group):
   "dir/box.txt" opens as "dir/box.stl" and gives 3163 when missing. NOT DECODED: stl_prefix (no directory part or a missing
   directory: the file is left as it is, ours).
4. gl_tolerance: `F.metric == 1 ? 0.025 : 0.001` (IsMetric()).
5. st_plane_selected no longer copies Display / Fit screen; creating a missing group record is marked ours.
6. mcamui: a scripted stfile: closes the nuklear file dialog; st_browse_done ignores results when no browse is open.
7. scene_part_solid_tris is bounded by the count meshed (solid_nmeshed).
8. Regen marks: Op.regen only (the dirty tree image in both apps, the OPDUMP shows DIRTY); not `stale`, so the post does not
   refuse them. Decoded: PostSelected -> DirtyOperationWarning -> dialog 977 (Regenerate / Do not regenerate / Cancel); 977 is
   NOT PORTED and the stored NCI is posted (= "Do not regenerate and continue"). Posted NC of a marked op: identical (checked
   on PARTS/b4regen, 23274 lines).
9. bb[4] = bb[5] = grid_z() for "No" in the 2D question: marked NOT DECODED for a non-Top stock plane (ours: the Top depth).
10. stock_ok's lathe / mill-turn rule is now in the code (group +0x19e8 read by mcam_load.c as F.stock_gtype; 0 in all parts).

Large / malformed STL (the user's requirements):
- Buffers: model 48 B / triangle (3 vertices x 3 floats + 3 int indices); the Fit walks the model in place (was a 72 B /
  triangle copy); the overlay's triangle outlines read the model in place (was a 100 B / triangle run copy); the dash and
  shaded buffers are handed to scene.c (scene_take_dash / scene_take_stock_mesh: no copy, made model-centre relative in
  place; was a copy at set time + a copy every frame). Per triangle: wireframe ~652 B -> ~192 B (48 model + ~144 dashes),
  shaded ~336 B -> 120 B (48 + 72), plus the GL driver's own copy.
- Binary STL: one 50-byte record at a time (a short last record is dropped, as FUN_1806d0ec0); the count field is ignored
  (as Mastercam); 0 triangles = no model + message. ASCII: as item 2. NaN / +-Inf triangles are left out with a message
  (ours; Mastercam keeps them - its extents tests skip NaN but would take Inf).
- Messages (all ours; Mastercam's CreateShadedStockModel only _MCAsserts and its caller ignores the result), caption "Stock
  Setup": "The stock file X could not be read: no stock model." / "The stock file X holds no triangles[ with valid
  coordinates]: no stock model." / "Not enough memory for the stock model of X (N triangles): no stock model." / "The stock
  file X: the last triangle record is incomplete and was left out[; triangles with invalid (NaN / infinite) coordinates were
  left out]." / "Not enough memory to draw the shaded stock model (N triangles)." / "Not enough memory to draw the stock
  model's outlines." / "Not enough memory to draw the stock model."
- GL: the NVIDIA driver (libnvidia-eglcore) segfaulted inside glBufferData when its copy could not be allocated under
  ulimit -v. scene.c now gives GL arrays of more than 64k vertices in 1M-vertex chunks, fills the stock VBO with
  glBufferSubData chunks, and probes headroom (3 x the chunk + 64 MB) before each; a failed probe skips the rest of that draw
  and the overlay reports it. ov_pt's growth is checked and, once failed, stops for the build (no retry per point).
- Freed: on any input change (sm_sync at stock_calc_store / every use), with no part / no stock record, at exit (atexit
  stock_free_all); the scene buffers are replaced / freed by their owner calls.
- Measured (offscreen, ulimit -v 4000000, peakrss.py; 5M-triangle 250 MB binary, 800k-triangle 94 MB ASCII):
  mcamui before -> after: binary wire 3.41 -> 1.27 GB, binary shaded 1.94 -> 1.25 GB, ASCII wire 0.73 -> 0.57 GB, ASCII shaded
  0.56 -> 0.47 GB; Qt after: binary wire 2.10 GB, shaded 1.69 GB, ASCII wire 0.80 GB, shaded 0.60 GB. Limits 1.1 / 1.5 / 2.2 /
  3.0 / 4.0 GB x both apps x wire / shaded: no crash; at low limits the model or its drawing is refused with the messages
  above and the app keeps running (before the fixes: a segfault in the driver at 1.1 - 2.2 GB).
- ASan (viewer-qt/build-asan rebuilt): the 26 Stock Setup STL / solid / shaded / plane / regen / fit scenes: no errors; the
  only leaks reported (~2 KB per run) are in libdbus and the GL driver.
- Test data: ~/mastercam-re/regress/mk_stl_big.py (big / truncated / huge count / zero files, outside /tmp; the big files were
  deleted after the runs), run_mem.sh / peakrss.py there; the small malformed cases are in tools/tests/stock_fixtures.py
  (trunc, hugecount, zero, longlines, nan) with scenes st_stl_truncated / _hugecount / _zero / _longlines / _nan, st_stl_free,
  st_scripted_browse.
- Regression run_stockfix (~/mastercam-re/regress/run_stockfix.sh): wf 0/78, chain 0/63, solid 0/54, op 0/148, tool 0/226,
  misc 0/75, stockplanes 0/73 (+7 scenes), planes 0/27, fileop 0/189, post 0/16, scenes.txt old (pre_stockfix build) vs new:
  dumps 0/46, shots 0/46.

## Bounding Box rest + arcs with a view (2026-10-22, both apps through core_ui)
Backup before: ~/mastercam-re/backups/pre_bboxrest_2026-10-22.tgz (old build for the scene compare:
~/mastercam-re/regress/old_bboxrest). Specs: formats/bbox_pushpull.md, formats/bbox_toolplane.md, formats/mcam.md "Arc views",
formats/wireframe.md "Rest of the panel". New dumps: decomp/mccad_bbox_pp2*.{c,txt}, decomp/mastercam_findviewk.c,
mastercam_planeadd.c, mastercam_planetype.c, mastercam_mcview.c, mastercam_stdviews_init.c / _static*.{c,txt},
mastercam_mcviews_refs.txt, mastercam_stdmatrix_callers.txt, mastercam_opmanres_load.c, mccore_make_view_k.c,
mcgeomsld_basic_primitive.c, mcgeomsld_basic_cyl.c, mcgeomsld_primop.c, mastercam_sldop_bmp_table.txt (invocations
~/mastercam-re/regress/ghidra_bboxrest_{pp,tp,icons}.sh).

### Ported
1. Arcs with a view: Ent.vw / vm (A_ID +0xc8; centre and angles in view coordinates; reserved views 1..8 without a file record,
   mcam_std_view); mcam_arc_pt / mcam_new_arc_view; loaded, drawn (3 degree chords), picked (polylines), snapped (mid,
   quadrants, centre, temporary midpoint, bbox extents), Analyze (view number; edits keep the view), saved (mcam_saveops.c:
   +0xc8 view, view-coordinate centre), project GEO line + view number; chained for operations only (cam2d.c cam_chain_views
   set by cui_chain.c; the chain keeps its own direction), 3D chain arrows / chaindump start3 / end3; cam_generate maps an arc
   whose view z is +-the Tplane z into the Tplane (cam_arc_in_plane), Top refuses view arcs.
2. Bounding Box X / Y cylinder circles: find_view_k(dir) (X -> 5, Y -> 2) view arcs.
3. Solids: the live solid (preview) and bit 8 -> Block (15) / Cylinder (17) features (ExFeat.prim, PRIM project line, Solids
   manager names opmanres 27616 / 27618, bitmaps 709 / 724); EnableSolidsCheckBox = the compute succeeded.
4. Push Pull: face pick (geo_click while the live solid exists), arrow (SelRefArrow / SelArrow rules, Absolute offset),
   drag preview / commit / Esc = code 6, Both directions, cylinder Height / Radius; mouse hooks in mcamui.c and
   viewer-qt viewwidget.cpp (as the Extrude arrows).
5. Create Tool Plane: origin / matrix / naming / id (cui_planes.c uplanes, planes_add), the Plane Selection list shows them,
   cam2d.c cam_plane_register for their matrices, op_view view number = GetShortViewNo; Set as current tool plane / Set WCS
   -> plane_active (new operations take the active planes: planes_init_op = InitOperation); project UPLANE / ACTPLANE lines.
   CuiPanelOps gained `text` (the Name TextBox; mcamui nkp_text, Qt op_text / IT_TEXT).

### Ours / NOT DECODED
Ours: the live solid = this program's 2.5D body (no live colour); a non-Z cylinder has no solid (Solids greyed, outline
preview); the face pick is a ray test, the side face's mid point in the reference direction; the arrow drawing / colour
order; Solids as its own undo step; plane ids after the highest id here; planes kept in the project only (the part gets them
through the op_views). View arcs are refused by the XY-only wireframe functions ("An arc in another plane ... NOT PORTED"),
by Extrude chaining (planar check) and as drill holes. NOT DECODED: the typed arrow value (SelArrowBase edit field), GrArrow
+0x1a8, sldAskPrimBBoxSolidFaceId internals, sldModifyBBoxSolidPrim, the primitive's colour / level, CorrectName /
IsReservedName / IsStatusBarPlaneLabel, the group_view update of SetActivePlane, new views for non-reserved matrices,
reading planes made here back from a part (cam_planes_from_record stays system-planes only), the STL mode / plane events.

### Tests
wf_scenes.txt +6 (bbox_cyl_x_views, bbox_cyl_y_views, bbox_live_pushpull, bbox_cyl_pushpull, bbox_solids_greyed,
bbox_toolplane); planes_scenes.txt +4 (pl_view_arc_right: Right-plane contour on a view circle exported / reopened / posted,
pl_view_arc_top_refused, pl_bbox_tplane_op: an op in the new plane at (50, 25, 20), posted + project reopen,
pl_view_arc_project); stockplanes_scenes.txt +1 (st_bboxpanel_pushpull); stockplanes_compare.sh also compares uplane /
active planes / cd chain lines. Steps (ours): bbface:ID bbclick:X,Y bbgrab bbdrag:V bbdragend bbdragesc,
gpset:bb_solids/bb_incr/bb_both/bb_ctp/bb_tpcur/bb_tpwcs/bb_tpname. Screenshots:
screenshots/2026-10-22_bbox_{pushpull,view_circles}_{mcamui,qt}.png.
Regression run_bboxrest (~/mastercam-re/regress/run_bboxrest.sh, summary.txt): wf 0/84, chain 0/63, solid 0/54, op 0/148,
tool 0/226, misc 0/75, stockplanes 0/74, planes 0/36, fileop 0/189, post 0/16, scenes.txt old (pre_bboxrest build) vs new:
dumps 0/46, shots 1/46 (backstage_info: the part copy's file times, as before).

## AutoCursor point-pick snap fix + Bounding Box review fixes (2026-10-22, both apps through core_ui)
Backup before: ~/mastercam-re/backups/pre_snapfix_2026-10-22.tgz (old build: ~/mastercam-re/regress/old_snapfix).
### Job 1: point picks did not snap (user report: Linking Depth "...", Extrude direction)
Root causes (formats/ui.md "AutoCursor on solids" -> "Point picks and the cursor"):
1. geo_snap searched around the screen projection of the world point it was handed - the cursor on the construction plane
   (scene_pick). In a view looking along that plane (Front / Back / Side) the ray never meets it, the point lands at the
   bottom of the view and the snap searched there (a vertex / edge under the cursor was never found; the Linking pick got the
   wrong Z). Fixed: every click and hover searches around the mouse (cui_snap.c geo_snap_ptr; mcamui handle_click, Qt
   MainWindow::viewClick, geo_cursor_update, geo_click) - the decoded AutoCursor works from the cursor.
2. The AutoCursor (marker + snap on mouse moves) ran only for the wireframe commands; the point_proc picks (Linking "...",
   Analyze new position, Stock Setup origin / corners, Extrude AutoCursor / direction) showed nothing while hovering.
   Fixed: cui_snap.c pp_hover (from cui_cursor_world) + pp_ov (the draw_fast_marker square), Qt redraws while one waits.
Checked: the solid edge sets (in-app bodies and part XT edges) were fine (ss_part_* / ss_* scenes); Top / Iso clicks already
snapped (the plane point projects to the cursor there). Not changed (noted): a solid vertex under a wireframe line in the
Extrude direction pick takes the line (event order of mask 0x4003 NOT DECODED); the Alt+F12 centre pick has no hover marker.
Shared by: wireframe point entry, Linking, Stock Setup corners / origin, Analyze, Extrude picks, rotation centre (click).
Bounding Box has no point picks (its face pick is a ray test).
### Job 2: Bounding Box review findings
1. Clipboard (cui_misc.c): Clip carries vw / vm; Paste rebuilds view arcs (mcam_new_arc_view_w); realloc checked.
2. Projects (cui_doc.c): GEO lines of view arcs write the view number + the 9 matrix values; older lines with a reserved view
   still read, an older line with another view reads as XY (ours).
3. geo_apply: with nothing selected the implicit all-set leaves the view arcs out (refused only when one is selected).
4. Trim Divide / cuts_on: view arcs are no XY cutters.
5. Plane ids: the part's MastercamPlanes archive is read (mcam_load.c planes_archive; formats/planes_archive.md,
   decomp/mastercam_planes_serialize*.c, tools/planes_list.py): next id = GetHighID(2) + 1 = max(0x33, the highest class-2 id
   when the last plane is class 2, the planes made here) + 1 = 52 in every user part (was 8). The op_view +2 view number
   stays GetShortViewNo.
6. cam2d.c ent_pick_t / ent_tangent: view arcs return 0 (ours); Dynamic / Partial / branch picks on a view arc are refused
   with a message (cui_chain.c, NOT PORTED).
Regression run_snapfix (~/mastercam-re/regress/run_snapfix.sh): wf 0/87, chain 0/63, solid 0/70, op 0/148, tool 0/226, misc 0/75,
stockplanes 0/76, planes 0/39, fileop 0/189, post 0/16, scenes.txt old vs new: dumps 2/46 (chamfer_first_pick / chamfer_live: only the new
end-of-events pphover test lines), shots 0/46. Screenshots: screenshots/2026-10-22_{linking_snap_front,linking_snap_iso,extrude_dir_snap_front}_{mcamui,qt}.png.
Plane creation undo: none recorded (MastercamPlanes::Add has no undo step in the decode; see cui_planes.c).
### Tests
solid_scenes.txt +16 (ss_lk_hover_*, ss_lk_click_*, ss_lk_part_b4_*, ss_ex_dir_*, ss_ex_autocursor_front, ss_wire_pt_front;
solid_compare.sh compares pphover lines), stockplanes_scenes.txt +2 (st_corner_*_snap_front; the compare also takes pphover /
part planes lines), wf_scenes.txt +3 (bbox_view_clip, bbox_view_breakpts, bbox_view_divide), planes_scenes.txt +2
(pl_view20_analyze_project with the fixture PARTS/nakto_v20.mcx-9 from tools/tests/stock_fixtures.py, pl_bbox_tplane_part_ids).
After the events both apps print the point pick's AutoCursor (pphover ...), the last entity of a wireframe command and the
Stock Setup page (ours, tests). Script step ppdump (ours).

## Phase 9 (2026-10-22): nuklear removed - DONE; Windows build - OPEN
User decision (2026-10-22): "forget about the nuklear app, actually remove it, just Qt moving forward". From now on all UI
work and fixes are Qt only (viewer-qt/src + viewer/core_ui). Backup before: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz
(the whole repository without viewer-qt/build*: mcamui.c, the *_ui.inc, vendor/nuklear*.h, the mcamui binary, the old
mcamui-vs-Qt compare scripts).
### Removed
- viewer/mcamui.c (+ binary / mcamui.o), the mcamui-only includes backplot_ui.inc, chaindlg_ui.inc, geopanel_ui.inc,
  levels_ui.inc, opparams_ui.inc, solidsmgr_ui.inc, toolmgr_ui.inc; vendor/nuklear.h, vendor/nuklear_sdl_gl3.h,
  vendor/stb_image.h (used by mcamui only); viewer/mcamgl.c (+ mcamgl.c.bak, binary): the older viewer was a Nuklear app
  too and could not build without the library; win/mcamui.exe, win/mcamui.o, win/SDL2.dll, win/ui; the stale objects only
  the mcamui rule built (cui_*.o, rpx.o, scene.o, geo2d.o, solid25.o, toolwiz.o).
- Kept: viewer/optree_text.inc (core_ui cui_optree.c includes it), every core source, cam_cli, tool_cli, mcamview (plain
  SDL2, no Nuklear), xt2obj, nc_dev. Nothing in core_ui needed mcamui: every cui_hooks entry (view_port, chain_ok,
  chain_ask_unselect_all, ...) and every extern core_ui uses is supplied by the Qt app (mainwindow.cpp, core_bridge.c),
  and no non-Qt binary links core_ui, so no stubs were needed.
- viewer/Makefile: the mcamui / mcamgl targets went; new `core` target = every core + core_ui source compiled with the Qt
  build's flags into core_obj/ (warnings check, links nothing); `windows` now cross-builds cam_cli.exe + the core objects.
- Docs: formats/*.md that cite mcamui.c / the *_ui.inc got a "mcamui removed 2026-10-22" note under their title; README.md,
  NOTES.md, viewer/README.md, formats/README.md (source table), formats/program_structure.md (programs, files, test hooks)
  were updated; core_ui.h's header says comments naming mcamui.c refer to the backup copy. Decomp citations untouched.
### Golden files (the compare scripts' reference since mcamui is gone)
- Frozen from the last mcamui-vs-Qt run (~/mastercam-re/regress/run_opdlg_b12_merged, all 0-diff, current builds of
  20:15 / 20:16): `QD=m SKIP_RUN=1 BLESS=1 bash tools/tests/<suite>_compare.sh .../work/<suite>` = mcamui's outputs through the
  same pick / sed normalisation. Checked: the same run's Qt outputs vs the golden files 0-diff in every suite.
- In the repo: tools/tests/golden/{wf,chain,solid,op,tool,misc,stockplanes,planes}/ (picked dumps, projects, posted NC,
  .tools, toolmanager.conf; all text), golden/parity/ (mcamui_commands.tsv = mcamui's paritydump, mcamui_steps.txt = its
  run_script steps), golden/pf_roundtrip/report.txt (mcamui's last report), golden/scenes/ (Qt stdout per scene).
- Outside the repo, ~/mastercam-re/regress/golden/<suite>/: saved parts (*.mcx-9 after mcam_norm.py - copies of the user's
  parts), the sqlite dumps of LIB / *.tooldb (*.sql: Mastercam's shipped mill_mm.tooldb), the scene screenshots (scenes/*.png).
- tools/tests/golden_lib.sh: gold_init / gold_put / gold_compare; `BLESS=1` (or `BLESS=scene1,scene2`) re-blesses an intended
  change, `QD=q SKIP_RUN=1` re-normalises an existing run; documented in tools/tests/README.md "Golden files".
- scenes.txt: tools/tests/scenes_compare.py (new) replaces ~/mastercam-re/regress/scenes_oldnew.py (old vs new mcamui,
  retired); its golden files are the Qt app's own (mcamui's shots cannot stand in for Qt's), blessed with the 20:16 build;
  two shots mask clock text (backstage_info file times, tool_list_report date / time). risk_agreement: the Qt risk dialog
  is modal and blocked the offscreen run (timeout); main.cpp now shoots the dialog under --shot and refuses it (ours,
  tests), golden re-blessed for that scene only.
- pf_roundtrip.py runs the Qt app; its REFUSED reason is read from Qt's prompt="..." (mcamui printed msg="..."); report
  compared with the golden report.
- parity_check.sh: kept (not retired) - it checks the Qt app against mcamui's frozen command / step lists, which still
  guards against losing a command mcamui had. Its one failure ("snapw not in the Qt hooks": Qt reached snapw only through
  the generic cui_op_script dispatcher) is fixed with an explicit testhooks.cpp step that sets the main viewport first
  (ViewInput::setViewport, as mcamui's vp_main) and then calls cui_op_script.
- Check of the 20:16 Qt build against the golden files before anything was deleted (~/mastercam-re/regress/run_qtverify):
  wf 0/87, chain 0/63, solid 0/70, op 0/194, tool 0/226, misc 0/75, stockplanes 0/76, planes 0/39, scenes dumps 0/46 shots
  0/46, parity OK, pf_roundtrip 185 controls 150 ok / 35 refused / 0 FAIL, golden same.
### Results after the removal (~/mastercam-re/regress/run_qtonly)
After the deletion, rebuilt (make clean + make all: cam_cli, tool_cli, mcamview, xt2obj, nc_dev, core; cmake --build
viewer-qt/build) - no new warnings (the remaining ones are older: -Wmissing-field-initializers in cui_geo / cui_ops / cui_bbox
/ cui_undo / cui_sel, -Wformat-truncation in paths.h / nc_post / xt_mesh / mcam_saveops for cam_cli's plain CFLAGS, mcce_dyn
unused / alloc-size, Qt's qopenglcontext.h -Wcpp, one deprecated call in viewwidget.cpp). Regression: wf 0/87, chain 0/63,
solid 0/70, op 0/194, tool 0/226, misc 0/75, stockplanes 0/76, planes 0/39, fileop 0/189, post 0/16 (posted NC unchanged,
every G43 still H0), scenes dumps 0/46 shots 0/46, parity PARITY OK 0 failures, pf_roundtrip 185 controls: 150 ok,
35 refused by the engine, 0 unchanged, 0 FAIL, golden same.
### Still open
- Windows build of the Qt app: viewer-qt CMake with MinGW-w64 or MSVC, Qt 6 + SARibbon + Qt-ADS built for Windows,
  GLEW / sqlite3, EglView is Linux-only (offscreen shots: QOffscreenSurface + QOpenGLContext there), core_bridge paths
  (platform.h / paths.h already handle Windows), windeployqt. The Makefile's `windows` target checks that the core compiles
  with MinGW (not run since the removal: no MinGW deps here).
- The PNGs in tools/tests/screenshots/ are still mcamui's (screenshots/run.py now renders with the Qt app; not rerun).
- Code comments in core_ui / viewer-qt that say "as mcamui" / cite mcamui.c line numbers are historical (the backup copy).


## MCP control (2026-10-23, ours - tooling, no Mastercam behaviour; built in ~/mastercam-re/work/tree_mcp, delivered as ~/mastercam-re/work/mcp.patch)
Lets Claude Code drive the Qt app live (tests in an offscreen instance, or work in the user's open session).
- viewer-qt/src/mcpserver.{h,cpp}: `apprenticecam --mcp` / APPRENTICECAM_MCP=1 opens a Unix socket
  ($XDG_RUNTIME_DIR/apprenticecam-<pid>.sock, mode 0600, link apprenticecam.sock = the latest instance). Line JSON requests run
  the MCAMUI_SCRIPT steps through the unchanged TestHooks::runScript (and MCAMUI_EVENTS input through TestHooks::pushEvents) on
  the UI thread; fds 1 / 2 are captured per request, so the reply holds exactly the state lines the tests print. One request at a
  time; requests wait while g_regen_busy (the op dialog's OK computing on its worker thread). status / modal / dismiss_modal /
  screenshot are answered at once even while a step waits on a modal box (live sessions without MCAMUI_SCRIPT).
- main.cpp: `--mcp` accepted, McpServer::maybeStart before app.exec() (nothing changes without the flag); CMakeLists.txt:
  target_sources. testhooks.cpp is NOT changed (MCAMUI_SCRIPT runs and the golden tests are untouched).
- tools/mcp_apprenticecam.py: the MCP server for Claude Code (stdio JSON-RPC, protocol 2025-06-18, no third-party modules):
  app_start (offscreen, owned, RLIMIT_AS 6 GB; scripted = the tests' MCAMUI_SCRIPT semantics by default) / app_attach / app_status /
  app_stop, document, state, command, selection, chaining, wireframe, input, op dialog, Tool Manager, plane / stock dialog, post,
  backplot / verify, screenshot (image content), modal and run_steps / list_steps (the step catalogue generated from testhooks.cpp,
  the core_ui dispatchers and the dialogs' testSet keys). Registered by .mcp.json at the repository root.
- Tests: tools/tests/mcp_smoke.sh (MCP SMOKE OK); op_compare / wf_compare outputs identical to the unpatched build of the same
  tree. Usage: tools/tests/README.md "MCP".


## Snap review fixes + contour roll 0x5d3 (2026-10-22, Qt only; backup ~/mastercam-re/backups/pre_snapfix2_2026-10-22.tgz)
### Part A
1. geo_snap's Nearest / intersection candidates (and the grid pass) used the construction-plane point `cur`, which is not
   the cursor in a view along the Cplane (viewZ . CplaneZ = 0). There they are now found in screen space from the cursor
   (scr_nearest: lines = the finite segment, proj_2d_pt_to_curve / dist_to_finite_line; arcs: dist_to_arc not dumped, ours
   sampled + refined); the grid is not searched there (ours, NOT DECODED); tangent / perpendicular stay off there by their
   decoded gate dot(CplaneZ, viewZ) > 1e-10 (FUN_180131c50:627 / FUN_180131620:782). Top / Iso unchanged.
2. Stock Setup Select origin / corners and the Extrude direction picks: an unsnapped click in such a view has no point -
   refused with a prompt (ours; map_cursor_to_3d's result there NOT DECODED). geo_cursor_on_plane.
3. Speed (results identical, checked by checksum with the caches off): the camera matrix is cached (scene.c camera_cached,
   scene_view_serial); the edge sets get the decoded body box cull (sld_solid_extents / FUN_1800cd000: cursor in the body's
   view box +-10 px, es_box_near); the edge hits are computed once per cursor / view / edge-set state (sld_edge_hits); the
   part mesh visibility test uses a view-plane grid of the triangles (part_hit, pg_sync); no AutoCursor during a
   middle-button view drag (cui_view_drag, ours: gr_dynamic's AutoCursor NOT DECODED; the next move snaps); a point pick's
   hover sets overlay_dirty only when its marker changes. BRACKET4 shaded, `snapbench:400` (Qt, 1400x900):
   Iso 0.960 -> 0.125 ms/move, Top 0.906 -> 0.202, Front 1.376 -> 0.264; wireframe display Iso 0.940 -> 0.172, Top 0.878 ->
   0.274, Front 1.379 -> 0.468.
4. planes_add: an id already used (a part whose last plane is of class > 2 makes GetHighID(2) 0x33 every time) is refused as
   Add does ("unable to add plane", id 0).
5. View-arc chains: the comment now says what happens (start at the seed's start, its stored direction; the pick-side rule
   for an entity outside the Cplane NOT DECODED).
6. The point pick's marker is keyed to the armed pick (pp_key): a re-armed / other pick shows no stale marker.
Scenes (blessed new): solid ss_near_top / _front / _side, ss_int_front / _iso, ss_tan_top, ss_tan_front_off, ss_perp_top,
ss_perp_front_off, ss_ex_dir_miss_front; stockplanes st_corner_miss_front. Step snapbench:N (ours, measurements; not in scenes).
### Part B: contour roll
cam_fileop.c reads u8 0x5d3 (FUN_180157e30), mcam_saveops.c writes it (0x5d4 / 0x5d5 no longer overwritten); the
MCAM_CONTOUR_ROLL_5D3 opt-in and the "(ours)" mark on 1335 removed. Pocket (pk_roll, 0x5d3 already) and Face are unaffected
(gen_contour is the only user of CamParams.roll). Blessed (op, after checking every diff: combo 1335 None -> Sharp, the
"(ours)" static gone, project "P roll 0 -> 1", the new contours' outside corners rolled with R = tool radius, rt_contour's
2144 = the defaults op's 0x5d4 = 0 now that roll no longer writes it): dlg_comp_off, dlg_cut_edits, dlg_link_rows,
dlg_open_new, dlg_tool_comment, dlg_tool_list, dlg_tool_switches, edit_depth, edit_save_reopen, gen_chamfer, gen_contour,
gen_engrave, menu_copy_paste, op_sld_contour_block (D10: R5 corners), op_sld_part_face (T12 D3: R1.5 corners; BRACKET4's
stored ops identical), op_sld_reopen, pf_grey, pw_contour, rt_contour.
Also blessed after the full run: planes pl_bbox_tplane_op, pl_bottom_save_reopen, pl_front_solid_post, pl_mapto_views,
pl_project_reopen, pl_refuse_auto_nontop, pl_refuse_tplane_ne_wcs (contour roll: R5 corner arcs with D10, P roll 1, point
counts), pl_bbox_tplane_part_ids, solid ss_wire_pt_front and scenes chamfer_first_pick / chamfer_live (the new end-of-events
"pphover geocursor" test line), pf_roundtrip report (rt_Contour_5_1335 starts from Sharp now).
Regression run_snapfix2 (~/mastercam-re/regress/run_snapfix2/run.sh, baseline run_qtonly): wf 0/87, chain 0/63, op 0/194 (after
the roll blessing), tool 0/226, misc 0/75, stockplanes 0/77, fileop 0/189, post 0/16, parity OK, shots 0/46; solid 1/80,
planes 13/39, scenes dumps 2/46, pf_roundtrip 1 line = the intended changes above, blessed (re-compare 0).
## Machine Group Properties: Files + Tool Settings, Safety Zone removed, loader chain (2026-10-23, Qt + core_ui; backup ~/mastercam-re/backups/pre_grptabs_2026-10-23.tgz)
Spec + status: formats/grpprops_tabs.md ("Status"). Code: viewer/core_ui/cui_grpprops.c (new: GP_815 / GP_1589 templates, page
model, OK rules, CheckGroupName, project lines, modal copy, sequential tool numbers), cui_stock.c (ST_TABS = 3 tabs,
st_set_page, the sh_* page dispatch, visited pages apply at OK, script steps stpage / gpdump), mcam_load.c/.h (resolve_machine:
active group +0x1170 -> machine -> +0xe94 -> control pages; group / machine / control fields), mcam_saveops.c (grp_patch; new
ops' +0x160 / +0x164 / +0x168), cui_ops.c (one line: grp_modal_record in heights_from_default_op), cui_toolpage.c
(part_add_tool: +0x11b8), cui_doc.c (GRPSET / GRPNAME / GRPCMT project lines), cui_optree.c (Doc file: no Safety zone),
viewer-qt stockdlg.cpp/.h (all three pages built from the tables; tab=K), managers.cpp/.h (three Properties children,
clickPropsRow), testhooks.cpp (tpmprops:K). Not touched: opdlg.cpp, mcce_dyn.c, mainwindow.cpp.
Tests: stockplanes_scenes.txt gp_* (19 scenes; stockplanes_compare.sh copies test-cube-10mm + thread-test too and picks
`grpprops ` / `stdlg:` / `tool N:` lines - none of the old scenes print those).
Open: the tree's machine group label stays "Machine Group-1" / "Properties - Mill Default" (not loaded from the part; a rename
in Files updates it, as ChangeGroupNameInTree); machine / control record saving (SaveControlToDb) for the read-only fields;
the post's stored sequence numbers (not wired, by request).
Regression run_grptabs (~/mastercam-re/regress/run_grptabs/run.sh, baseline run_snapfix2): wf 0/87, chain 0/63, solid 0/80, op
0/194, tool 0/226, misc 0/75, planes 0/39, fileop 0/189, post 0/16, scenes dumps 0/46, parity OK, pf_roundtrip same;
stockplanes 26/103 = the new gp_* scenes only (blessed: gp_open_files_tree, gp_open_toolset_tree, gp_open_stock_tree,
gp_tabs_widgets, gp_thread_test, gp_test_cube, gp_edit_roundtrip, gp_widgets_roundtrip, gp_cancel, gp_untouched_save,
gp_name_rules, gp_override_rules, gp_enable_rules, gp_newop_seq, gp_modal_copy, gp_modal_off, gp_tool_seq,
gp_project_newpart, gp_project_part; re-compare 0/103); scenes shots 2/46 = clock text outside the masks of backstage_info /
tool_list_report (masks widened, no re-bless). No existing golden changed: the loader fix's only effect (toollib[1] of the
two-machine parts -> user-tools.TOOLDB) is not in any golden output.
### Group properties review fixes (2026-10-23; formats/grpprops_tabs.md "Review fixes")
Modal copy via the save's own height bytes (mcam_saveops.c heights_to_record / mcam_op_record_heights), comment EM_LIMITTEXT in
the Qt edit, the tree's machine group label = F.grp_name (scene_part_changed hook; the tree Rename writes the group), control
page file-order fallback, tp_tool_offset on renumbered tools (AddToToolRegister decoded), CP1252 texts (viewer-qt cp1252.h),
bounded group names, FUN_1801e2f70's "Invalid file name" warning. stockplanes_compare.sh's pick uses grep -a (ANSI bytes).
Regression run_grptabsfix (baseline run_perf): wf 0/87, chain 0/63, solid 0/80, fileop 0/189, post 0/16, scenes dumps 0/46,
parity OK, pf_roundtrip same, dlg_fileop_cmp 1/205 (known NAKTO tree 3). Blessed (lists: ~/mastercam-re/regress/run_grptabsfix/
blessed.txt): op op_sld_part_face + tool tm_copy_paste, tm_up_multi_similar, tm_up_similar_yes, w_tm_click_up (D / H of
renumbered tools); misc 58 scenes, planes pl_b4_edit_top / pl_bbox_tplane_part_ids / pl_view20_analyze_project, stockplanes
16 st_* scenes, 24 scene shots (the tree label = the part's group name); stockplanes gp_* (warnings, label, D / H, modal
flags) and new gp_tree_name, gp_tree_name_project, gp_ansi_text, gp_ansi_tree, gp_comment_limit, gp_modal_copy_stored,
gp_tool_offsets_clash. All re-compare 0.

## Op dialog batches 3 + 4: Depth Cuts + Lead In/Out (2026-10-23, Qt + core_ui; backup ~/mastercam-re/backups/pre_opdlg_b34_2026-10-23.tgz)

formats/opdlg_port_audit.md 8 (status, live controls per page, ours / NOT DECODED); decode notes formats/opdlg_fields_notes.md
"B34 notes"; code map formats/mcamui_dialogs.md 1.y.
- Engines: viewer/cam2d.h (CamParams: the exit side, lead on / sides / perpendicular, overlap, gouge check, adjust start /
  end, `dc_taper` / `dc_taper_a` / `dc_bydepth` / `ct_tipcomp`; `mc_lead_dz` also for the contour), cam2d.c (`LeadS`,
  `lead_sides`, `lead_in` / `lead_out` / `lead_start` per side with Perpendicular and ramp heights, `adj_path` overlap /
  Extend / Shorten, `pass()` fitting both sides, `gen_contour` finish depth cuts / taper / By depth, `cam_lead_recalc`),
  cam2d_pock.c (finish leads per side), cam_fileop.c (`LEADS()` both sides + the per-side % recalc, contour adjust / taper
  / order / finish cuts, `cam_params_from_patches` lead / depth fields, `cam_lead_sidecar_compat`), mcam_saveops.c (the
  exit side from its own values; contour / pocket finish cuts kept), cam_cli.c (`--set` keys, `--lead` one lead).
- Model: core_ui/cui_opdlg.c (pf_bget / pf_bset for 25598 / 3030 / 25602 / 3013 / 2793, `pf_lead_put`, `pf_dc_on`,
  `pf_engine_nd`, the gouge check marker, pictures from CamParams, IsPageDisabled rules for Depth Cuts / Lead In/Out);
  cui_ops.c `openg` (a fifth line: the new engine inputs). No Qt source change (opdlg.cpp draws the model).
- Tests: op_scenes.txt: dlg_depth_lead / w_cancel_widget tick the Depth cuts group before the max rough step (its buddy
  now), dlg_depth_lead / rt_contour / w_pf_widgets set the exit side as well (the NC they post is unchanged), pf_grey
  targets controls still greyed (25159 / 25162); new rt_lead_sides (both sides different, Perpendicular, ramp / helix
  heights, overlap; save / export / reopen / post), rt_lead_exit_off (exit off, entry only, gouge check off),
  rt_lead_adjust (Extend start 5, Shorten end 3), rt_depth_cuts (two chains, finish cuts, taper 3 deg, By depth),
  rt_pocket_b34 (pocket finish cuts, exit lead 50 % / 30 %), w_pf_leads (Qt widgets: exit off, perpendicular).
  pf_roundtrip.py: `openg` compared whole (5 lines per dump), ENABLE presets reach the controls a group / check box greys
  on a default op (Depth cuts group + tapered walls, adjust start / end).
- Regression ~/mastercam-re/regress/run_opdlg_b34 (baseline run_grptabsfix; summary.txt, blessed.txt): see the numbers in
  formats/opdlg_port_audit.md 8 and the report. Blessed (dumps only - no posted NC of an existing scene changed): op
  dlg_depth_lead, gen_area_mill, gen_circle_mill, gen_contour, gen_dynamic_contour, gen_dynamic_mill, gen_face,
  gen_peel_mill, gen_pocket, gen_slot_mill, pf_grey, pw_* (9 chain types), rt_circle, rt_contour, rt_slot,
  w_cancel_widget, w_pf_widgets (the Depth cuts group live with greyed buddies showing the record's values, the Depth
  Cuts tree state following 0x868, the lead pages' sides live, the "(ours)" exit marker replaced by the gouge-check one, the
  lead picture for ops made here; rt_contour's project: the lead patches of both sides) + the 6 new scenes; planes
  pl_b4_edit_top (BRACKET4 op 14's Depth Cuts tree state: 0x868 = 1); pf_roundtrip report (the new live controls).
  Screenshots: screenshots/2026-10-23_opdlg_lead_inout.png, 2026-10-23_opdlg_depth_cuts.png.

## Performance pass: toolpath dialog OK + dialog-path fixes (2026-10-23, Qt + core; backup ~/mastercam-re/backups/pre_perf_2026-10-23.tgz)
User report: "tool path parameters seems slow". Measured (offscreen): open 10-13 ms, page switch 7.5 ms, commit 6.5 ms;
the real cost was **OK**: synchronous regeneration even when nothing changed (Dynamic Mill 4.6 s BRACKET4, up to 30 s
buckle-v12, 48 s NAKTO op 2). Patch: ~/mastercam-re/work/perf.patch (applied).
- **Unchanged OK = nothing** (decoded): mccore `edit_operation` 0x1800b0760 (decomp/mccore_edit_operation.c,
  decomp/mccore_edit_operation_rule.c: memcmp_op, op_tool_info_changed, TlHolder compare; the old-NCI string / byte
  compares only apply to old-NCI opcodes, none edited here). Ported as `pp_ok` / `pp_ok_unchanged` (cui_ops.c): compares
  the engine inputs gen_begin would hand cam_generate + the full tool record + holder outline. Ours: an op with no valid
  toolpath still generates; "changed = mark dirty, regenerate later" is NOT ported (we regenerate at once).
- **OK off the UI thread**: generate_pending split into gen_begin / gen_run / gen_end; gen_run on a QThread (64 MB stack);
  viewer-qt/src/regenprogress.h = application-modal progress window after 400 ms (ours: Mastercam's regen progress UI is
  NOT DECODED), Cancel / Esc / title-bar X cancel (mcce_abort, C11 atomic; abort points in mcce_dyn incl. the cam2d_dyn
  prep: latency < 0.1 s). Cancel enabled only for Dynamic Mill (the only engine with abort points), disabled with an
  "ours" tooltip otherwise or when a new tool was registered. A busy flag blocks open / new / close / exit / drops /
  window close / play timer while it runs. Cancel restores the op byte-identically.
- **Bug: wrong tool on dialog regeneration** (NAKTO has two T2: 6 mm and 3 mm): new `cam_op_tool_index` (cam_fileop.c)
  = the tool cam_op_from_file uses (slot first, then number). **Bug: thread mill clearance** from record 0x1230
  (template 2837) in hf_from_record / hf_apply. New tools/tests/dlg_fileop_cmp.sh: every stored op regenerated through
  the dialog path vs `cam_cli fileop`: 204/205 same (NAKTO op 3 Core Roughing, generated by neither).
- Incremental template-page update (opdlg.cpp: in place when structurally equal; tree / quick view only on change;
  focus kept); trial-line memo in mcce_dyn.c (cap 160).
- Timing in main: unedited OK NAKTO op 2 48.4 s -> ~0 (left as stored); edited 48.4 s frozen -> ~7 s responsive;
  edit commit 10.6 -> 6.0 ms. Other areas measured and fine: draw 0.12-0.25 ms/frame, open 22-87 ms, post 0.45 s,
  vsim <= 0.7 s. Not done: regression suite parallelism (est. 20 -> 8 min), testhooks re-render per step.
- Reviewed (findings 1-9 all fixed before applying). Regression run_perf (baseline run_grptabs):
  all 0-diff after blessing rt_floor_stock, pl_b4_edit_top, ok_unchanged_stored, regen_dup_toolnum,
  w_ok_unchanged_stored; fileop 0/189, post 0/16. NOT verified on the real desktop (offscreen only).

## Batch 3 + 4 review (2026-10-23; backup ~/mastercam-re/backups/pre_b34fix_2026-10-23.tgz)
Independent review of op dialog batches 3 + 4 against the decomp; details formats/opdlg_port_audit.md 8.1. Fixed (decomp
cited in the code): overlap + Shorten start (cam2d.c adj_path, FUN_180051990 l.56164-56172), lead % recalc after the
sidecar's patches (cam_fileop.c cam_params_from_patches, bnci_ctour l.394-406), record / patch sweep 0 = no arc on the contour
(CamParams lead_sw_rec, cam2d.c lead_side, cui_opdlg.c pf_lsw, mcam_saveops.c; bVar22 / bVar24), the pieces' nc_tol
thresholds. New scene rt_lead_b34fix. Regression run_b34fix all 0-diff (op 0/219 after blessing the new scene; fileop 0/189,
post 0/16, dlg_fileop_cmp 204/205).

## Op dialog batch 5: Contour rest, cutter comp in control, Multi Passes, Tabs, Break Through, Tab Cutoff (2026-10-23, Qt + core_ui; backup ~/mastercam-re/backups/pre_opdlg_b5b_2026-10-23.tgz)
formats/opdlg_port_audit.md 9 (ported / greyed / ours, sources); formats/post.md "Cutter compensation in control" (the
MCAM_CUTTER_COMP=1 switch and how G41 / G42 D meets the user's LinuxCNC tool table); code map formats/mcamui_dialogs.md 1.y;
new decomp/mccore_cc.c. Also folded in (user decisions on the batch 3 + 4 review): tapered walls for 2D chamfer (FUN_180056a70:
opcode only), Mastercam's over-length Shorten (FUN_1800566e0: no refusal).
- Engines: cam2d.h (CamParams `ct_comp`, `ct_ccopt`, `ct_doff`, `ct_crnd`, `ct_cbrk`, `bt_eff`, `tab_ramp`, `tab_ang`),
  mcam_load.h (Move.cc), cam2d.c (`ct_offset` corner break / rounding / optimize, `pass_cc_tag`, D/2 by comp type in
  gen_contour, `pl_overtrim`, `tab_run` + vertical tabs in cut_path, `ct_tapered` for chamfer), cam_fileop.c (record + patch
  reads, `cam_bt_offset` / `cam_bt_eff`, break through for stored ops), mcam_saveops.c (0x5d0 from `ct_comp`, 0x8a8 / 0x8aa
  with the Multi Passes group), nc_post.c (`nc_cutter_comp_on`, G41 / G42 D / G40, refusals, the arc refit stops at cc moves).
- Model: core_ui/cui_opdlg.c (bindings for 2938 1290 / 2144 / 1310 / 1210 / 1211, 25591 2945, 2834 1129 / 1208 / 1201 / 1213,
  25599 1220 / 1338, `pf_bt_update`, `pf_cc_ok`, `pf_mp_on`, ND tooltips, the 2938 marker, PG_MULTI tree state), core_ui.h
  (OpHFlags.bt), cui_ops.c (new-op break through via cam_bt_eff, `openg b5` line). No Qt source change.
- Tests: op_compare.sh takes a per-scene `ENV:NAME=VALUE` extra field; op_scenes.txt: dlg_multi_tabs / rt_contour /
  w_pf_widgets tick the Multi Passes group (+ Final depth, same NC) and the Tabs group before their buddies; new rt_comp_control
  (ENV MCAM_CUTTER_COMP=1: Control, save / export / reopen / post: `G1 G41 D5`, `G1 G40`), rt_comp_revwear (Reverse wear
  right -> G41), rt_comp_noswitch (Computer / Off only), rt_corners (rounding 2, break 3), rt_tabs_b5 (vertical tabs,
  thickness 2), rt_tabs_ramp30, rt_break_thru (0.5 + tabs, save / export / reopen both), rt_mp_group (rough 2 x 3),
  rt_shorten_over / rt_shorten_end (500 on a 300 loop), rt_taper_chamfer (spot drill, 2D chamfer, taper 5 deg).
  pf_roundtrip.py: ENABLE presets for the two groups and Break Through, `openg` 6 lines.
- Regression ~/mastercam-re/regress/run_opdlg_b5 (baseline run_b34fix): see formats/opdlg_port_audit.md 9.5.

++ formats/qt_port_plan.md	2026-10-23 12:20:09.769187080 -0400
## Transform tab (2026-10-23, core_ui + Qt; built and tested in an isolated copy ~/mastercam-re/work/tree_transform, delivered as ~/mastercam-re/work/transform.patch)
Spec: formats/wireframe.md "Transform" (+ its "The port" subsection); dumps decomp/xform/ (mccadutil do_xform_* + dialogs,
mccore Xf* machinery, find_color / ClearColors). Code: viewer/core_ui/cui_xform.c (new), hooked into cui_geo.c the way the
Bounding Box panel is (geo_start / geo_end / geo_cancel / geo_need / geo_take / geo_prompt / geo_panel_body /
geo_panel_button / geo_live_update / geo_ov_extra / cui_geo_script), the G_X* commands in core_ui.h.

| ported | Mastercam source |
|---|---|
| Translate (Move / Copy / Join, #, Distance between / Total distance, Delta X/Y/Z, From / To points + line, Polar, Direction one way / opposite / both) | do_xform_trans, CXfTransDlg 6433 / 6434, FUN_18003d210 |
| Translate to Plane (Move / Copy, #, source / destination plane combos, origin points) | do_xform_trans3d, CXfTrans3dDlg 6435 / 6437, FUN_18003cc10 |
| Rotate (#, Angle between / Total sweep, centre pick, Rotate / Translate, Direction) | do_xform_rotate, CXfRotateDlg 6441 / 6443, FUN_180031900 / FUN_180032010 |
| Move to Origin (all shown entities, point pick -> active origin) | do_xform_movetoorigin, translate_silent |
| Mirror (X axis / Y axis / Polar / Line / Two points, value edits switch the mode, line click in the dialog) | do_xform_mirror, CXfMirrorDlg 6439 / 6440, FUN_180014230 |
| Rectangular Array (Direction 1 / 2: #, length, angle, direction) | do_xform_array, CXfArrayDlg 6457 / 6458, FUN_18003c8b0 |
| Scale (Uniform / XYZ, Factor / Percentage, #, Auto center / reference point; cumulative per copy) | do_xform_scale, CXfScaleDlg 6445-6451, FUN_180035820 |
| Stretch (In + Intersect windows, line ends inside move, Move / Copy, delta / From-To / Polar) | do_xform_stretch, CXfStretchDlg 6558 / 6559, dbselwin_stretching, xform_store_ent |
| common: preview (config 10026) / Regen, OK / Apply (select again) / Cancel, Use New Attributes (+ Increment level per copy), group / result colours (config 5010 / 5031 / 5044), Join lines, undo | XfCreatePreview / XfDoFinalAction / FUN_1808d8820, XfApplyColorLevelToResult_Index, find_color, FUN_1808e48b0 |
| Home > Clear Colors and the mini toolbar's (FT ClearColors) | mastercam.dll ClearColors 0x180272880 |

Greyed / NOT PORTED: Dynamic (gnomon UI, NOT DECODED), Roll and Distribute (decoded, see wireframe.md - not ported), Geometry
Nesting (not decoded), Remove Item / Reset Items, Fit, Help, Scale Calculator, the plane dialogs and the 3-point plane
definition of Translate to Plane, the Level / Color pickers, "Mirror note and label text". Solids are not transformed (the
extrude features here are rebuilt from their chains); toolpaths are not touched.
Ours (also in cui_xform.c's header): the dialog as a function panel with one fixed layout and the decoded parameter names as
labels (bitmap labels NOT DECODED), the Direction button as three radios, Stretch's window = two picked corners in world XY,
copies / moves make new entities (new ids; Mastercam keeps the moved entities' ids), arcs that would leave XY / the system
views or become splines are refused with a message, the session keeps the parameters (no XFORM.PRM), the marks are not
saved. Qt: Enter ends the selection (xf_selecting), a function-panel button whose label is wider than 60 px grows to fit; the
Transform commands start only from their do_xform_* FT functions (Art > Translate / Scale = OnTranslate / OnScale share the
labels and stay greyed).

Tests (the copy): wf_compare 0/123 differ (87 old scenes unchanged + 36 new xf_* scenes blessed after checking every output by
hand: copies / total / both directions, join with the shared-vertex dedupe, move + polar, From / To, line pick, rotate centre /
total / both / translate, mirror X / Y + move / polar / line / line clicked in the dialog / Y-axis point / two points, scale copies / XYZ (arc refused) / percentage +
reference point, array + directions, Translate to Plane Top -> Front / origin copies, Move to Origin + its Esc, Stretch + From / To + no-stretch exit,
Apply loop, Esc, no selection, Clear Colors, undo / redo, export + reopen of a Right-view arc, the panel dump), chain_compare
0/63, solid_compare 0/80, parity_check PARITY OK (Qt-only: the 9 Transform buttons). Screenshots
screenshots/2026-10-23_transform_translate.png, _rotate_join.png, _mirror_tab.png (not committed).
Handoff: this section came as a patch (the STATUS / HANDOFF list below is left to whoever applies it: "Transform tab done; Roll,
Distribute, Dynamic, Geometry Nesting open - see wireframe.md Transform / Not ported").


## Op dialog batch 6: Pocket 2940 / 3003 / 3004 / 3005 + 3010 / 3011 (2026-10-23, core + core_ui; backup ~/mastercam-re/backups/pre_opdlg_b6_2026-10-23.tgz)
formats/opdlg_port_audit.md 10 (ported / greyed / ours, the oracle table, questions); decode notes formats/pocket_engine_b6.md; Ghidra
dumps decomp/pocket_b6/; code map formats/cam_engines_code.md 3, formats/mcamui_dialogs.md 1.y; post side formats/post.md "Pocket finish".
Process: six engine agents in work trees ~/mastercam-re/work/tree_b6_{pspiral,tspiral,zigzag,entry,hsptype,finish}, merged by hand in
~/mastercam-re/work/tree_b6_merge (finish's section structure as the base), checked there (fileop 0/189 + the corpus oracle), copied.
- Engines (viewer/): cam2d_pock.c (gen_pocket_v2: refusals by method / type, per-pocket plan, sections in wr_pock_toolpath order,
  pk_rough_depth / pk_fin_section), new cam2d_pock_{tspiral,entry,pspiral,zz,fin,hsx}.c, pgeo.c gls_merge, cam2d.c (PK_ZIGZAG_DECODED,
  correlated zigzag refuses pocket types 1-4), cam2d.h lead_xflags, cam_fileop.c (lead flags, 0x5d4 for pockets, roughing angle
  radians -> degrees), cam_cli.c (--rp / --rec record patches, --verify on fileop), Makefile.
- Model: core_ui/cui_opdlg.c (pf_pock_rule, POCK honoured offsets, pairs, roughing angle / XY angle units, 2793 pocket perp / overlap,
  3004 comp + optimize behind MCAM_CUTTER_COMP=1 with an "(ours)" marker, ND tooltips). No Qt source change.
- Oracle (corpus <mastercam-samples>/net, local only): True Spiral coaster p95 0.005; Parallel Spiral CHGREC refused
  (burial), 0.004 / 0.011 with PK_PS_MEDIAL_FALLBACK=1; Mill.mcam 5 / 9 0.008 -> 0.004; mc-test01 2 and A001 13 unchanged.
- Tests: op_scenes.txt rt_pock_oneway / pspiral / clean / tspiral / refused / hs_rd / ramp / helix_follow / finish_b6 / comp_control
  (ENV MCAM_CUTTER_COMP=1) / comp_noswitch. Regression ~/mastercam-re/regress/run_opdlg_b6: all 0-diff after blessing (op 64 blessed,
  pf_roundtrip 271: 240 / 31 / 0 FAIL), fileop 0/189, post 0/16 (also with MCAM_CUTTER_COMP=1), dlg_fileop_cmp 204/205, MCP OK.

## Op dialog batch 7: Facing 2942 + Slot Mill 2944 / 3020 (2026-10-23, core + core_ui; backup ~/mastercam-re/backups/pre_opdlg_b7_2026-10-23.tgz)
formats/opdlg_port_audit.md 11 (ported / greyed / ours, the oracle table, questions); decode formats/mcmill_2d_engines.md 8 (Facing)
and 1.3 (Slot compensation call sites); code map formats/cam_engines_code.md 3a, formats/mcamui_dialogs.md 1.y "Batch 7"; post side
formats/post.md "Facing Feed rate between cuts" / "Slot Mill compensation in control". Done in the main tree (no helper trees).
- Engine (viewer/): new cam2d_face.c (bnci_face 0x180097230: styles Zigzag / One way / One pass / Dynamic, climb, even passes /
  reverse last pass, auto angle FUN_180096d20, move between cuts loops / linear / rapid, feed between cuts behind
  MCAM_FACE_LINK_FEED=1, across / along / approach / exit, depth cuts + keep tool down, mccore plunge / retract / clear moves with
  adjust_depths' re-based heights; Dynamic through MCCECalcDynPock); cam2d_ops.c `gen_face` gate `face_decoded` (new op / Face Cut
  record edit / FACE_DECODED=1; stored ops as read keep the measured engine), Slot Control / Wear / Reverse wear codes on the finish
  passes with MCAM_CUTTER_COMP=1 (`g_mw_rec1`); cam2d.h `ht_inc`, `cam_face_link_feed_on`; cam2d.c includes cam2d_face.c and sets
  ht_inc; cam_fileop.c (Facing in the mc block: mc_p / mc_depth_* / mc_keep_down / mc_rrate..., ht_inc, `cam_face_mirror`);
  nc_post.c (a cc code on an engine arc refused, not dropped); Makefile deps.
- Model: core_ui/cui_opdlg.c (2942 on the record: pf_honoured FACE, Style bound with Mastercam's codes, `pf_en_rule` FUN_18016caf0,
  PAIR / RPAIR, 1758 radians, 1857 with no selection usable, 1846 / 1710 only with the switch + "(ours)" marker; Depth Cuts 1233 /
  25195 / 1377 for Facing; 3020 2077 bound; 2944 1290 all five with MCAM_CUTTER_COMP=1; 3020 1847 / 1724 greyed with the reason;
  hf_apply sets ht_inc; the row-laid fallback uses Mastercam's codes). No Qt source change.
- Oracle (no Face / Slot op in the internet corpus; the user's three Face ops, FACE_DECODED=1): BRACKET4 / bracket4-dashed op 8
  p95 0.048 / max 1.502 -> 0 / 0 (every NCI move within 1.3e-5); buckle-v8 op 41 move-for-move equal (p95 0.032 / max 0.050 =
  the oracle's sampling floor).
- Tests: op_scenes.txt rt_face_b7_zigzag / oneway / ktd / onepass (0xa5a refusal) / dynamic / linkfeed (ENV MCAM_FACE_LINK_FEED=1)
  / nolinkfeed, rt_slot_b7 / rt_slot_b7_cc (ENV MCAM_CUTTER_COMP=1: G41 D5 on the entry line, G40 on the exit line) /
  rt_slot_b7_ccarc (refused: comp on the entry arc) / rt_slot_b7_noswitch.
  Regression ~/mastercam-re/regress/run_opdlg_b7: all 0-diff after blessing (op 32 + stockplanes 14 blessed, pf_roundtrip 287:
  255 / 32 / 0 FAIL), fileop 0/189, post 0/16 (also with MCAM_CUTTER_COMP=1 and with MCAM_FACE_LINK_FEED=1), dlg_fileop_cmp 204/205, MCP OK.

## Op dialog batch 8: Drill / Circle Mill / Helix Bore / Thread Mill (2026-10-23, core + core_ui; backup ~/mastercam-re/backups/pre_opdlg_b8_2026-10-23.tgz)
formats/opdlg_port_audit.md 12 (ported / greyed / ours, oracle table, questions); post side formats/post.md "Drill cycles"; code map
formats/mcamui_dialogs.md 1.y "Batch 8"; formats/mcmill_2d_engines.md 6 (bnci_drill_depth decoded).
- Engine / post (viewer/): cam2d.c `cam_drill_z` = bnci_drill_depth (breakthrough 0x1260 + tool_tip_length for drill tools except types
  6-8), `cam_tool_tip_length`; cam2d.h `drill_dwell` (0x1250) / `drill_bt` (0x1260); cam_fileop.c reads them (record + patches);
  nc_post.c `nc_drill_text` (post text page 0x27 from Generic Fanuc 3X Mill.pst) and the opt-in MCAM_DRILL_CYCLES=1 (G82 P, G84 / G74 +
  G94, G85 / G89 P; Bore #2 / Fine Bore / Rigid Tapping / custom refused with LinuxCNC reasons); stored NCI 81 dwell read;
  mcam_saveops.c thread heights follow the 2837 radio's patch; cam_cli `post --ops`.
- Model: core_ui/cui_opdlg.c drill labels / enables / combo / tooltips from the post text, 1089 dwell (switch only), 25594 1338 / 1339;
  2823 feed pairs + `pf_apply` re-derivation (MapToOp) + enables 1725 / 1740 / 1749; 2824 1674, 2969 1714 / 1670 / 1711 enables;
  2837 1655 / 1656; 25592 1735. No Qt source change.
- Oracle: CONTROL HORN op 4 / BLOCK op 5 drill depths now equal the NCI (were 0.1 / 0.015 too shallow); circle / thread unchanged.
- Tests: op_scenes.txt rt_drill_b8_page / _dwell / _bore / _tap / _tap_noswitch, rt_helix_b8_feeds, rt_thread_b8_pages. Regression
  ~/mastercam-re/regress/run_opdlg_b8: all 0-diff after blessing (op 34, scenes shot op_parameters, pf_roundtrip 292: 260 / 32 / 0
  FAIL), fileop 0/189, post 0/16 (also with each switch), dlg_fileop_cmp 204/205, MCP OK. Screenshot
  screenshots/2026-10-23_opdlg_b8_drill_cycle.png.

## Op dialog batch 9: 2D High Speed - Dynamic / Area / Dynamic Contour / Peel (2026-10-23, core + core_ui; backup ~/mastercam-re/backups/pre_opdlg_b9_2026-10-23.tgz)
formats/opdlg_port_audit.md 13 (ported / greyed / ours, oracle table, questions); code map formats/mcamui_dialogs.md 1.y "Batch 9";
formats/dynamic.md "Glue code" (hst_record), formats/area_mill.md 11.5.
- Engine glue (viewer/): cam2d_dyn.c `hst_record` / `hst_has_patches` (record + the dialog's HST record patches -> LoadDynPockParm);
  cam2d.c Area dispatch re-derives am_* (cam_fileop.c `cam_area_macro_params`, factored from params_from_record). Stored ops unchanged.
- Model: core_ui/cui_opdlg.c pf_honoured DYN / AREA / DCT, pf_en_rule 3205 / 3219 / 3299, 3328 custom store, 3206 / 3216 / 379 2216 /
  2206 / 2207 bound, NOT DECODED / "N:" tooltips; dead ours controls removed (Dynamic Contour stand-ins, Area / Peel extras, entry
  helix radius). No Qt source change. Dynamic Contour stays behind MCCE_DCTOUR=1, Peel not generated (NOT DECODED).
- Oracle: corpus has no 2D HST op with NCI; no Peel / Dynamic Contour op anywhere; new Dynamic reference Probing Example 2_MM (p95 3.7 mm,
  plunge vs no entry). User ops unchanged (dynamic p95 median 0.462, area 0.914).
- Tests: op_scenes.txt rt_dyn_b9_cut / _entry / _helix, rt_area_b9_cut, rt_dctour_b9_pages / _switch. Regression
  ~/mastercam-re/regress/run_opdlg_b9: 0-diff after blessing (op 20 HST dumps, pf_roundtrip 297: 269 / 28 / 0 FAIL), fileop 0/189, post
  0/16 (all switches), dlg_fileop_cmp 204/205, MCP OK. Screenshots screenshots/2026-10-23_opdlg_b9_dynamic_cut.png / _entry.png.

## Dynamic measure_block speed-up + B9-5 (2026-10-23, viewer/mcce_dyn.c only; isolated copy ~/mastercam-re/work/tree_dynperf, delivered as ~/mastercam-re/work/dynperf.patch)
- **What** (ours, pure optimisation, no engine rule changed): `measure_block` (the engagement trial of every
  trial_line_m / refine_f / brent step, ~77 % of a Dynamic op's time) clipped the whole material group against the small
  swept polygon. New `crop_band` gives Clipper only the material edges whose integer Y span meets the clip polygon's Y band
  (+-0.01, widened to the next material vertex Y); runs of other edges are shortened to a -> r1 -> rn -> b (kept whole when
  that would let Clipper's AddPath merge points), rings wholly outside the band are dropped. This keeps every scanbeam that
  meets the clip polygon identical (Clipper 6 rounds intersections to the beam: clipper.cpp IntersectPoint l.677-696), so
  Clipper's raw output is **bit-identical**, not just the same region. The end-cap open clip uses the cap polyline's own band.
  A first try (crop to the bbox with detours) changed 333 of 65k clips - scanbeam boundaries matter - and was dropped.
- **Proof**: `MCCE_CROP_CHECK=1` runs every clip both ways and compares the raw Clipper outputs: 2 613 082 clips over the 37
  stored Dynamic ops, 0 differ; 841 414 clips over the Dynamic op scenes (gen_dynamic_mill, dlg_dynamic, rt_dynamic,
  rt_dyn_b9_cut / _entry / _helix, rt_face_b7_dynamic), 0 differ. Moves of the 37 ops before / after: 0/37 differ; full
  regression ~/mastercam-re/regress/run_dynperf (baseline run_opdlg_b9).
- **Speed** (CPU user s, cam_cli fileop, base vs patched interleaved on a loaded machine): the 37 stored Dynamic ops +
  Probing op 2 270.8 -> 146.5 s (1.85x); buckle-v8 42 35.2 -> 16.1, buckle-v10 10 31.2 -> 15.9, buckle-v9 42 29.5 -> 15.9,
  buckle-v6 36 14.9 -> 8.0, BRACKET4 14 6.0 -> 2.7, tramming-rod 1 7.7 -> 5.8, Probing op 2 11.5 -> 7.0. measure_block
  falls from ~77 % to ~56 % of the time; the rest is commit/subtract (the material difference, whose whole output is
  kept, so it cannot be cropped this way) and the trial geometry.
- **Tessellation cache (2026-10-28, ours, pure optimisation)**: measure_block re-tessellated every material shape
  (sl_to_cpaths) and recomputed its Clipper Y on every trial. `tess_get` keeps each shape's polygon + integer Y and reuses
  them only when the shape is byte-for-byte unchanged (entities, dir, fl, chord; shapes are edited in place and pointers
  reused, so a pointer key alone is not enough). Moves of the 37 stored Dynamic ops: 0/37 differ; the 31 Dynamic-engine op
  scenes (56 outputs, NC included): 0 differ. CPU: the 37 ops 148.2 -> 134.7 s; gen 100 x 50 pocket, 10 mm tool 5.4 -> 4.6 s
  wall (4349 records, 53 171 measures, ~12 trials per committed move). What is left (perf, 2026-10-28): Clipper ~50 % (the
  swept / material intersection of every trial ~31 %, the end-cap open clip ~16 %, the material difference at commit ~20 %
  incl. cpaths_to_sl), crop_band's vertex scan ~10 %. The trial count is Mastercam's own (the 50-step heading scan + Brent to
  1e-7 of FUN_18001aa70 / FUN_1800194e0) and sequential; the per-trial cost is ours (Mastercam's kernel measures on arcs).
- **Optimisation pass (2026-11-07, ours, pure speed-ups - every stored op's moves byte-identical, 204 / 204)**. Measured first: of the
  205 stored ops of the user's parts, 2D High Speed (41) is 147.8 of 149.9 s CPU; everything else ~2 s. No DLL is missing: mcce.dll is
  decompiled (decomp/mcce_all.c); the cost is our Clipper stand-in for mcce's exact material kernel (the kernel port runs separately).
  Ruled out: compiler flags (-O3 -march=native -ffp-contract=off -fno-math-errno: 3 %, same output); a parallel heading scan (the
  50-step scan of FUN_18001aa70 stops at step 1 in 97 % of searches; 81 % of the trials are Brent's, sequential).
  1. Regeneration on threads (core_ui cui_dirty.c regen_mask: prepare all - generate - finish in order, the op found again by its id;
     the Dynamic-engine ops (Dynamic, Dynamic Contour, Peel, Area through mcce) on worker threads, MCAM_REGEN_THREADS (default the
     processor count, 1 = one by one); the engine's per-run statics are _Thread_local (mcce_dyn.c, cam2d.c, cam2d_ops.c, offset.cpp
     cl_err), mcce_thread_release frees a worker's caches. buckle-v12 (20 ops, 6 through mcce): 18.8 -> 14.9 s wall, the moves the
     same as one by one (test step movehash; mb_tag_arc's running arc id is a tag only and is left out of the hash). Also g_mat_ver++
     at the start of every engine run (a freed Pock's address coming back must not meet the last run's measure records).
     Thread safety checked with ThreadSanitizer (a harness generating the 6 mcce ops of buckle-v12 one by one, then on 6 threads):
     after the merge of the exact kernel it found the qsort comparators' base pointers (g_is_base, g_isab_base) and cam2d.c's
     generator pointers (ofile, pass_chain, g_tch_*, g_tcl / g_tcn / g_tcoff, g_kd_*) - now _Thread_local; 0 races after, and
     buckle-v8 / v11 / v12 regenerate the same on 1 and on N threads (3 runs each).
  2. crop_band_y with a per-block Y index (CROP_BS 32 edges, kept with the tessellation cache, iy_alloc): blocks wholly out of the band
     are passed over; MCCE_CROP_CHECK=2 compares it with the plain scan (crop_band_lin): 3 912 281 crops over the 41 ops, 0 differ.
     Plus e_bbox's min / max inline (comparisons only) and an exact inline llround (|v| < 2^52) in cl_int / to_paths.
     buckle-v11 op 10: 13.1-13.4 -> 11.2-11.3 s CPU (crop_band 10.5 % -> 2.8 % of the profile); Clipper itself is what is left.
- **B9-5 (Probing Example 2_MM ops 2-7)**: two causes, neither fixed (both change the user's stored ops):
  1. the "plunge": the output loop of mcce_calc_dyn_pock gives the first record a start Z of depth + 1 (`zprev =
     P->depth + 1.0`, ours: the engine's previous-position Z of FUN_180012390 l.15564-15617 is not a motion), and
     cam2d_dyn.c mcce_moves feeds down to that start Z before the record. Mastercam writes every record straight to its own
     XYZ (OutputMotionToBnci, decomp/mcmill_hst2d.c l.4445 / 4534-4540); its NCI goes feed plane -> G1 (plunge feed) to the
     first record's Z on all 37 user ops and the 6 Probing ops. On stay-inside entries this program plunges to depth + 1 at
     the helix centre and climbs back to the helix top (Probing -35 -> -47 -> -37; NAKTO op 2 3 -> -7 -> 3; BRACKET4 /
     bracket4-dashed 14 / 16 3 -> -3.5 -> 3); on the 32 from-outside ops it only stops at depth + 1 on the way down.
     Stored ops are posted from Mastercam's NCI, so the user's posted programs are not affected; a stay-inside op edited or
     made here is. Gated fix proposal: first record start Z = its own Z (one line in the output loop), behind a switch
     until the user accepts the 37 changed fileop moves (prototype: the plunge goes, Probing ours->ref p95 3.68 -> 3.27 mm).
  2. most of the p95: the helix centre. Mastercam takes the medial-edge end point with the largest clearance (FUN_1800c5df0,
     formats/mcce_engines.md §1.5, on CalcMedAx - NOT DECODED); max_clearance substitutes the centroid of the deepest inset,
     the middle of the slot (-260, -450) where Mastercam has the end-arc centre (-320, -450). With Mastercam's centre forced
     (diagnostic only) op 2 p95 drops to 1.5 mm, p50 0.55 -> 0.07. Needs CalcMedAx decoded (B9-3 / B6-1).

## Op dialog batch 10: Arc Filter / Tolerance 10318 (2026-10-23, core + core_ui; backup ~/mastercam-re/backups/pre_opdlg_b10_2026-10-23.tgz)
formats/opdlg_port_audit.md 14 (ported / greyed / ours, oracle table, questions); code map formats/mcamui_dialogs.md "Batch 10";
post rules formats/post.md "Arc filter"; decompiles decomp/mccore_filter.c.
- Engine: viewer/cam_filter.c = Mastercam's Line/Arc filter (mccore bnci_filter / FUN_180948750 / FitArc FUN_18093ab00, one way),
  run at the end of cam_generate_op when record 0x33a is on (record + the page's patches); stored ops only after an edit of the page
  or with MCAM_ARC_FILTER=1; XZ / YZ arcs + G18 / G19 only with MCAM_ARC_FILTER_PLANES=1. Move.apl / cz, CamOp.filtered (no own refit).
- Model: cui_opdlg.c pf_honoured (filter block), pf_filter_group (group click: 5 % of the total, cut tolerance rescaled), 10391 rule,
  pf_engine_nd (XZ / YZ "N:", smoothing NOT DECODED). No Qt source change.
- Oracle (no regenerable corpus op with the filter on): the filter over Mastercam's own filtered NCI - engraved coaster op 4 exact
  (186 / 9 from the resampled input), op 2 p95 4e-9; ROUGHPROJNCI op 1 p95 0.012 (tol 0.02); STEEPSHALLOW op 1 (3910 XZ arcs) exact as
  is, p95 0.0006 resampled (tol 0.000667). User ops: none has the filter on.
- Tests: op_scenes.txt rt_arcf_b10_dyn / _contour / _planes; cam_cli nci-filter / nci-filterpost / fileop --post (testing). Regression
  ~/mastercam-re/regress/run_opdlg_b10: wf 0/123, chain 0/63, solid 0/80, op 23/317 before blessing (16 page-10318 dumps + 7 new
  goldens; 0/317 after), tool 0/226, misc 0/75, stockplanes 0/113, planes 0/38, fileop 0/189 (also 0/189 with MCAM_ARC_FILTER=1
  MCAM_ARC_FILTER_PLANES=1), post 0/16 (all switches, incl. the two new), scenes 0/46 / 0/46, parity OK, pf_roundtrip 305: 275 ok /
  30 refused / 0 FAIL (blessed), dlg_fileop_cmp 204/205 (NAKTO tree 3, known), MCP OK, oracle dynamic / area unchanged.
  Screenshot screenshots/2026-10-23_opdlg_b10_arcfilter.png.

## Decisions batch dec1 (2026-10-23, core + core_ui; backup ~/mastercam-re/backups/pre_dec1_2026-10-23.tgz)
The user's answers (formats/user_decisions.md B6-1..B6-5, B7-1, B7-3, B8-2) applied in the main tree; results, oracle and sources
formats/opdlg_port_audit.md 15; regression ~/mastercam-re/regress/run_dec1 (baseline run_opdlg_b10, blessed.txt).
- Files: viewer/cam2d.c (zigzag -> decoded engine, PK_ZIGZAG_DECODED=0 = old; CamOp.warning cleared; includes cam2d_slot_arc.c),
  cam2d.h (CamOp.warning), cam2d_pock.c (Parallel Spiral + burial: fallback link + warning; PK_PS_MEDIAL_FALLBACK removed),
  cam2d_pock_pspiral.c (comment), cam2d_pock_fin.c (0x1400 / 0x126b ungated), cam2d_pock_entry.c (0x1308 ungated, pk_patched
  removed), cam2d_ops.c (Face gate removed, FACE_DECODED=0 = old; slot_chain ramp on the exact loop, SLOT_RAMP_POLY=1 = old), new
  cam2d_slot_arc.c, Makefile, cam_cli.c ("WARNING:"), nc_post.c (dwell without MCAM_DRILL_CYCLES refused), core_ui/cui_ops.c
  (warning in the status line), core_ui/cui_opdlg.c (3020 1847 / 1724 live); tools/tests/op_scenes.txt (+5 scenes); docs
  formats/opdlg_port_audit.md 15, user_decisions.md, mcamui_dialogs.md, post.md, pocket_engine_b6.md, mcmill_2d_engines.md.
  Not touched: viewer/mp/, viewer/occt/, mcce_dyn.c, cam2d_dyn.c. No Qt source change.
- Oracle: A001 op 13 p95 0.031 -> 0.028 (ours->ref 0.027 -> 0.022); BRACKET4 / bracket4-dashed op 8 0.048 / 1.502 -> 0 / 0 (43 moves
  within 1.3e-5); buckle-v8 op 41 12 moves = the NCI's (2.5e-7). Posted NC: default post unchanged (stored ops post their NCI).
- Tests: rt_slot_dec1_arcs / _lines, rt_pock_dec1_burial / _zigzag_burial / _zigzag. Regression: op 50 DIFF + 5 new (explained:
  new metric pockets' rounded finish corners and per-depth finish order; 1847 / 1724 dumps) -> blessed 0/326; fileop 3/189 -> base
  rebaselined (old files in ~/mastercam-re/regress/base_before_dec1) -> 0/189; post 0/16 with every switch; pf_roundtrip 306: 276 /
  30 / 0 FAIL (blessed); dlg_fileop_cmp 204/205; scenes / parity / MCP OK. Screenshots screenshots/2026-10-23_dec1_*.png.

## MP post engine in the app (mpint, 2026-10-23; patch ~/mastercam-re/work/mpint.patch)
User decisions P1-P4 (formats/user_decisions.md "Post engine"); details formats/mp_engine.md "Integration (app)", formats/post.md.
- Machine Group Properties > Files: check box "Post with Mastercam post (.pst) (ours)" (cui_grpprops.c `GP_MPPOST`, id 0x7a01,
  default off; Mastercam shows the post read-only there, 604 / FUN_1801e6720). `McamFile.grp_mppost`, project line `GRPMPPOST 1`,
  step `grpmppost:0|1`, MCP `post` option `mastercam_post`. Dumps list it only when on (`mppost 1` / `31233[x]` / `[31233]`):
  every earlier golden is unchanged. Screenshot screenshots/2026-10-23_mpint_files_page.png.
- Post (cui_ops.c post_ops): off = the LinuxCNC post, unchanged; on = `post_ops_mp` -> viewer/mp/mp_post.c (mp_cli's chain) with
  the machine's .pst, `%MCAMDIR%` = paths.conf shared (paths.h host_path), "\n" line ends (MpEnv.eol), mill / router only; ops
  made / edited here, stale ops and ops nci_ascii cannot convert completely are refused.
- Build: viewer/mp compiled into mcamcore (CMake `MP_C`, Makefile `CORE_MP`).
- Tests: tools/tests/mpint_cmp.sh (app = mp_cli on BRACKET4 / test-cube-10mm), stockplanes scenes `gp_mppost_files` / `_off` /
  `_cancel` / `_refused` (new goldens), mcp_smoke.sh (+2 checks). tools/mp_oracle_parts.sh no longer runs the deleted
  tools/mp_nci_fix_tlchg.py. Regression: ~/mastercam-re/regress/run_mpint (baseline run_mpint_base = the same tree without the
  patch).
- Open: the program file name (ours `<part>-mcamui.nc`; Mastercam: the op's NC name + the post's extension, several files when
  ops name different NC files - here one file); writing binary NCI for ops made in the app (needed to post them with a .pst);
  CR LF on request (not asked: only "\n").

## Merge of dynperf + mpint + hst into the main tree (2026-10-23/24; backup ~/mastercam-re/backups/pre_merge_hst_mp_dyn_2026-10-23.tgz)
- Applied in order ~/mastercam-re/work/dynperf.patch, mpint.patch, hst.patch (each against its pre-batch-10 tree copy). Code hunks all
  applied (offsets only); main vs tree_hst after the merge differs only by the batch 10 / dec1 / mpint code (checked file by file:
  cam2d_dyn.c hst_patch_off 0x320 band, cui_opdlg.c 10318 filter, dec1 burial / slot arc, cui_ops.c dec1 warning + post_ops_mp).
- Conflicts (docs / tests only): this file (mpint section re-inserted before STATUS), formats/opdlg_port_audit.md (the "HST engines
  (post-batch 9)" section appended after section 15), tools/tests/op_scenes.txt (rt_dctour_hst / rt_peel_hst after
  rt_dctour_b9_switch), golden pf_roundtrip/report.txt (regenerated + blessed, not hand-merged). Patch .orig / .rej files removed.
- Screenshots screenshots/2026-10-23_mpint_files_page.png, 2026-10-23_hst_peel.png, 2026-10-23_hst_dyncontour.png. Kept
  tools/tests/hst_check.py. Note: ./build.sh does not build viewer/mp (mp_cli for mpint_cmp.sh): `make -C viewer/mp`.
- Regression ~/mastercam-re/regress/run_merge3 (baseline run_dec1; + mpint_cmp.sh, MP oracle, hstcases.sh): wf / chain / solid / op
  0/329 / tool / misc / stockplanes 0/122 / planes 0-diff, fileop 0/189 (also MCAM_ARC_FILTER*), post 0/16 with every switch, scenes
  0/46 + 0/46, parity OK, pf_roundtrip 322: 298 / 24 / 0 FAIL (blessed: blessed.txt), dlg_fileop_cmp 204/205 (NAKTO tree 3), MCP OK,
  MPINT OK, MP oracle unchanged, Dynamic oracle p95 median 0.462 (unchanged), hst_check 0 gouges (moves identical to tree_hst).

## Op dialog batch 11: post-side pages - Coolant 6546, Canned Text 6548, Home / Ref. Points 6554, Misc Values 6555 (2026-10-24; done)
Backup ~/mastercam-re/backups/pre_opdlg_b11_2026-10-24.tgz. Decompiles ~/mastercam-re/work/b11 (-> decomp/b11/). New viewer/cam_postvals.{c,h};
template pages in core_ui/cui_opdlg.c (pf_dlg 6546 / 6548 / 6554 / 6555, formats/opdlg_fields.tsv rows c65xx, gen_opdlg_fields.py PAGES);
machine coolant definition read by mcam_load.c (McamFile.mach_cool_*); "post values only" OK on a stored op keeps Mastercam's
toolpath and rewrites its NCI section-start records (cui_ops.c pp_ok_postonly, cam_postvals.c pv_nci_rewrite, sidecar POSTVALS line,
mcam_saveops.c record copy); LinuxCNC post coolant opt-in MCAM_COOLANT=1 (nc_post.c). Results: formats/opdlg_port_audit.md 17.
- Files: viewer/cam_postvals.{c,h} (new), mcam_load.{c,h} (mach_cool_*, Op.postv / postv_groups), nc_post.{c,h} (op_coolant / cool_pos /
  cool_off_all, nc_coolant_on), mcam_saveops.c (post fields of a postv op into the saved record), core_ui/cui_opdlg.c (pf_dlg 6546 / 6548 /
  6554 / 6555, cool_* / misc_* / ref_bit helpers, pf_custom*, pf_honoured, pf_en_rule, pf_hidden, pf_engine_nd, pf_emit, Quick View
  Coolant), core_ui/cui_ops.c (pp_ok_postonly / pp_ok_apply_postvals; pv_set_machine_fallback before the LinuxCNC post),
  core_ui/cui_doc.c (POSTVALS line), viewer/Makefile + viewer-qt/CMakeLists.txt (cam_postvals.c), viewer/mp/Makefile (header
  dependency), tools/gen_opdlg_fields.py + formats/opdlg_fields.tsv (+79 rows) -> cui_opfields.inc, tools/tests/op_scenes.txt (+6),
  decomp/b11/. No Qt source change.
- Regression ~/mastercam-re/regress/run_opdlg_b11 (baseline run_merge3): fileop 0/189, post 0/16 (also with MCAM_COOLANT=1 and every other
  switch), op / tool / stockplanes / planes / scene-shot diffs = the tree greying of slots 13 / 17 / 18 gone + the Coolant page as a
  template + page walks reaching slot 13 (blessed, blessed.txt), the rest 0. Docs: formats/opdlg_port_audit.md 17 (+ batch 11 row),
  formats/post.md "Coolant", formats/mcamui_dialogs.md (batch 11 code map), formats/README.md. Screenshots
  screenshots/2026-10-24_opdlg_b11_coolant.png, 2026-10-24_opdlg_b11_misc.png. User questions: opdlg_port_audit.md 17.7.

## Op dialog batch 12: frame + Tool page extras (2026-10-24; done)
Backup ~/mastercam-re/backups/pre_opdlg_b12_2026-10-24.tgz (includes tools/tests/golden). Note: ~/mastercam-re/work/opdlg_b12.patch and
~/mastercam-re/regress/run_opdlg_b12 are the old batches 1+2 work (merged 2026-10-22), not this batch; this batch's regression is
~/mastercam-re/regress/run_opdlg_batch12 (baseline run_opdlg_b11). Progress notes below as they happen.
- Decompiles (2026-10-24, ghidra_core mccore): ~/mastercam-re/work/b12/tpdlg_all.c (169 ToolpathDlg / ToolpathPageBase / TreeStyleDlgBase
  functions incl. OnApplyPb, OnHideDialogPb, OnReadFromDefaultsPb, ReplaceOperation, OnPreviewToolpathPb, RestoreOperation, FeedCalculator),
  vt_tpdlg.txt (ToolpathDlg vtable: 0x3d0 OnOK_Pages, 0x3e0 ValidateActivePage, 0x400 InitializeTreeStates, 0x598 ReplaceOperation),
  acp.c / acp2.c / vt_acp.txt (Cnc::Tool::AssemblyChangedProcessor FUN_180a449c0, TlUpdateAssemblyDlg = mccoreres 10293), copied to decomp/b12/.
- Plan: (1) Force tool change in the LinuxCNC post, opt-in MCAM_FORCE_TOOL_CHANGE=1 (AssignToolChanges rule); (2) Apply 12321;
  (3) Hide dialog 10320; (4) Reload parameters from defaults file 9431; (5) Tool page tool fields editable (UpdateDbTool +
  AssemblyChangedProcessor + dialog 10293) if time; (6) Preview toolpath 10501 if time. Rest stays greyed with the decoded reason.
- Done: (1)-(4) + the Tool page Filter Active / Filter... (the Tool Manager's Tool List Filter settings). (5) / (6) decoded but not
  ported (tool-system slots NOT DECODED; Preview's Cancel cannot undo a stored-op takeover here). Results formats/opdlg_port_audit.md 18
  (questions 18.6). Files: core_ui/cui_opdlg.c (pp_apply*, pp_hide*, pp_tb_*), cui_ops.c (pp_seed_params factored from open_params,
  pp_reload_defaults, script steps opapply / optb / ophideend / opframe), cui_toolpage.c (tp_factive / tp_rows, comments), cui_toolmgr.c
  (tm_filter_test, tmf_ok -> Filter Active), core_ui.h, viewer/nc_post.{c,h} (op_force_tc, MCAM_FORCE_TOOL_CHANGE), viewer-qt/src/opdlg.cpp
  / mainwindow.cpp / viewwidget.cpp, tools/tests/op_scenes.txt (+7 rt_b12_*), decomp/b12/, formats/post.md, mcamui_dialogs.md,
  tools/tests/README.md. Screenshot screenshots/2026-10-24_opdlg_b12_toolpage_filter_apply.png.

## 3D roughing: Dynamic OptiRough (3drough, 2026-10-24; patch ~/mastercam-re/work/3drough.patch)
Toolpaths > 3D gallery > OptiRough: drive solid pick (Enter = End Selection), op dialog pages Cut Parameters 1465 / Transitions 1430
(template pages from exports/mcmsurfres_dialogs.tsv), generate (viewer/cam3d_rough.c: OCCT mesh, decoded Z levels, weave slice,
regions -> the ported mcce Dynamic engine, SAFE links), backplot, LinuxCNC post (unchanged rules). Not exported into .mcam (sidecar
DRIVE lines). Decode / ours / NOT DECODED / oracle: formats/hst3d_port_plan.md 6; decisions 3D-1..4: formats/user_decisions.md.
OCCT must be installed for the 3D ops (else they refuse; the Makefile / CMake link it into cam_cli and the Qt app when found).

### Merge of 3drough into the main tree (2026-10-24, after batch 12; uncommitted on master for the user's review)
- `patch -p1 -N` of 3drough.patch (base = before batch 11's golden re-bless and batch 12). Code hunks all applied (offsets only):
  cui_opdlg.c (pg_has / pp_title / pf_dlg / pf_honoured OPTI / pf_rad 1430 / pf_opti_custom / pf_engine_nd), cui_opfields.inc,
  opdlg_fields.tsv (s1465 / s1430 rows appended after batch 11's c6555), cui_ops.c, core_ui.h, mainwindow.cpp, this file.
  One reject: tools/tests/op_scenes.txt (gen_optirough / gen_optirough_refuse appended after batch 12's rt_b12_* scenes).
  Fixed on the way: pf_rad's batch 7 comment (2942 1758) had slid onto the new 1430 line. .orig / .rej removed.
- Note (pre-existing, not changed): tools/gen_opdlg_fields.py regenerates cui_opfields.inc with st = 0 for Canned Text 6548
  3162-3165, the checked-in file (batch 11) has 1; the merged .inc keeps main's 1s plus the patch's new rows.
- Regression ~/mastercam-re/regress/run_merge_3d (baseline run_opdlg_batch12 + hst3d_check / hst3d_oracle): wf / chain / solid / tool /
  misc / stockplanes / planes 0-diff, op 1/354 (rt_post_b11_stored .nc header path only: the regress work folder baked into its
  golden by batch 12 - not blessed, normaliser fix pending), fileop 0/189 (also MCAM_ARC_FILTER*), post 0/16 with every switch,
  scene dumps 0/46, shots 29/46 -> blessed (OptiRough gallery icon only, blessed.txt) -> 0/46, parity OK, pf_roundtrip 322: 298 /
  24 / 0 FAIL, dlg_fileop_cmp 204/205 (NAKTO tree 3), MCP OK, MPINT OK, oracles + MP oracle c947f0c2 unchanged, HST3D CHECK OK,
  hst3d oracle = run_3drough (p95 1.045 / 7.861). Screenshots screenshots/2026-10-24_optirough_*.png.

## Dirty operations (2026-10-24, Qt + core; starting HEAD 25819f0, uncommitted on master)
User: "when toolpath parameters are changed after they exist mastercam puts an x through them and makes it so they need to be
regenerated". Full decode + port: formats/opdlg_port_audit.md 19 (sources decomp/dirty/, README.txt there). In short:
- OK on an existing op (script opok, Qt OK -> core pp_ok_quick): a change outside BnciUpdate::DontAllow's mask (feeds / rpm / numbers /
  comments / post values) or another tool marks the op dirty (op +0x94) and keeps its toolpath; nothing is computed. Feed-only changes
  and ops without a toolpath compute at once as before (the NCI patch is NOT PORTED). Supersedes "changed = mark dirty, regenerate
  later: NOT ported" in "Performance pass" above.
- Toolpaths manager: X image, toolbar 800 Select all dirty / Regenerate all selected / Regenerate all dirty live (core cui_dirty.c).
- Dialog 977 before Post / Backplot / Verify (DirtyOpsDialog in viewer-qt/src/dialogs8.cpp, core dop_*): Regenerate / Do not regenerate /
  Cancel. Supersedes "dialog 977 NOT PORTED" (Stock Setup review fixes item 8, STATUS item 5).
- Tool edits, Edit Common Parameters (non-patch changes), Recalculate feeds/speeds now mark dirty instead of regenerating.
- .mcam save: edits written in place of their ops, stale stored ops +0xfc = 1; load reads +0xfc (Op.regen). Project: DIRTY / D: lines.
- Tests: script steps tpseldirty, tpregen, tpregendirty, dirtyopt:0|1, dirtyok, dirtycancel, dirtydump; 7 new op scenes + dirty_export;
  scenes that edited an existing op and then dumped / posted now run tpregendirty (or answer 977). op_compare.sh strips the work folder
  from the upper-cased "(MCX FILE - ...)" header (rt_post_b11_stored re-blessed).
- Regression ~/mastercam-re/regress/run_dirtyops (blessed.txt): all 0-diff after the explained blessings; fileop 0/189, post 0/16 with
  every switch. Decisions D1-D4 in formats/opdlg_port_audit.md 19.6.

## Decisions batch dec2 (2026-10-24, core + core_ui; starting HEAD 9c10186, uncommitted on master)
formats/opdlg_port_audit.md 20 (table, oracle, Peel cases, regression); formats/post.md (Arc filter, Cutter compensation);
formats/mcamui_dialogs.md "Decisions batch dec2". Regression ~/mastercam-re/regress/run_dec2 (+ final/, blessed.txt, hstcases.sh).
- Q1: mcce_dyn.c - the first record starts at its own Z (no plunge to depth + 1). 35 base Dynamic ops rebaselined
  (~/mastercam-re/regress/base_before_dec2 = the old files). Oracle p95 equal within 0.002 mm on all 37; plunge error on the 5
  stay-inside ops -6.5 / -9.9 mm -> 0; Probing Example 2_MM ours->ref p95 3.68 -> 3.27.
- Q2: CamOp.warning + "Unverified (ours)" on pages 2784 / 3205 for Peel / Dynamic Contour.
- Q5: Peel side curves (cam2d_dyn.c peel_side, cam2d.c offset_open_trim_x): sliver chord dropped (false ray caps -> "nothing to
  cut", 0.17 mm wall, and a 7.3 mm gouge with both extensions), first-arrival trim (chain 8 width 20). All Peel cases 0 gouges.
- Q6: stored 0x33a honoured (cam_filter.c). Q9: stored XZ arcs read as mccore does; G18 / G19 with MCAM_ARC_FILTER_PLANES=1, else
  refused (nc_post.c, tools/mcam_ops.py). Q29: Wear / Reverse wear refused on the LinuxCNC post; Q30 D<tool number>; Q32 unchanged.
- New scenes rt_peel_dec2_ext, rt_peel_dec2_v, rt_comp_dec2_wear. Screenshots screenshots/2026-10-24_dec2_peel_unverified_dialog.png,
  2026-10-24_dec2_peel_v_width20.png.

## Save parameters to defaults file + Preview toolpath (Q19 / Q20, 2026-10-24, branch `defs`)
Details formats/opdlg_port_audit.md 22. Op dialog toolbar: Save parameters to defaults file 9433 (WriteOperationToDefaultsFile's in-place
rewrite, into the program's own copy of the group's defaults file under `<config dir>/defaults`, never Mastercam's file; pristine copy +
dated backups; "Defaults file (ours)" button: Reset defaults file to original / Restore a dated backup, confirmed) and Preview toolpath
10501 (computed on a worker thread, drawn in place of the op's toolpath; OK after it as GetPreviewOp; Cancel leaves the op byte-identical,
dirty). Code: core_ui cui_defaults.c / cui_preview.c, mcam_saveops.c mcam_op_dialog_record, paths.h defaults_*, Qt opdlg.cpp runJob /
preview / defaultsMenu. Regression ~/mastercam-re/regress/run_defs (see the audit 22.6). Decisions S-D1, P-D1, P-D2 (audit 22.7).

## STATUS / HANDOFF (2026-10-24, after decisions batch dec2; branch `defs` on top)
Branch `defs` (worktree ~/mastercam-re/work/wt_defs, from fe01be3, committed, not merged): "Save parameters to defaults file + Preview
toolpath" above (Q19 / Q20; regression ~/mastercam-re/regress/run_defs; open decisions S-D1 / P-D1 / P-D2 in formats/opdlg_port_audit.md
22.7). Next in the main tree after merging it: formats/TODO.md "Next in the main tree" 3 (Solids tree / OCCT).
Before it on master: "Decisions batch dec2" above (uncommitted on master for review; regression ~/mastercam-re/regress/run_dec2 + final/, all
0-diff after its blessings and the Dynamic fileop rebaseline; fileop 0/189, post 0/16 with every switch). Next in the main tree:
formats/TODO.md "Next in the main tree" 2 (Save parameters to defaults file, Preview toolpath). Before it: "Dirty operations" above (uncommitted on master for review; regression ~/mastercam-re/regress/run_dirtyops, all 0-diff after
its blessings; fileop 0/189, post 0/16; user decisions D1-D4 in formats/opdlg_port_audit.md 19.6). Before it:
Main tree = op dialog batch 12 + the 3D roughing merge ("3D roughing: Dynamic OptiRough" above: Toolpaths > 3D > OptiRough on part
solids via OCCT, decisions 3D-1..4 in formats/user_decisions.md chosen pending user confirmation; regression
~/mastercam-re/regress/run_merge_3d, all 0-diff after its blessings except the known rt_post_b11_stored header path; fileop 0/189,
post 0/16 with every switch; uncommitted on master for review). Before it: "Op dialog batch 12" (frame + Tool page extras: Apply, Hide dialog, Reload defaults, Tool page filter, Force tool
change opt-in MCAM_FORCE_TOOL_CHANGE=1; regression ~/mastercam-re/regress/run_opdlg_batch12 all 0-diff after its blessings; fileop 0/189,
post 0/16 with every switch; open user questions formats/opdlg_port_audit.md 18.6). All op-dialog batches 0-12 of section 5 are done;
batch 13 (class D) is not planned. Before it: "Op dialog batch 11" (post-side pages; regression ~/mastercam-re/regress/run_opdlg_b11 all 0-diff after its
blessings; fileop 0/189, post 0/16 with every switch incl. the new MCAM_COOLANT=1; open user questions formats/opdlg_port_audit.md 17.7).
Before it: the "Merge of dynperf + mpint + hst" (last full regression ~/mastercam-re/regress/run_merge3: all 0-diff
after its blessing; Dynamic Contour + Peel Mill generate by default (no Mastercam NCI, user question H.7 1), Mastercam post (.pst)
opt-in per machine group, Dynamic ~1.85x faster bit-identical). Before it: "Decisions batch dec1" (regression ~/mastercam-re/regress/run_dec1: all 0-diff after its blessings;
the fileop base now holds the decoded A001_13 / BRACKET4_8 / buckle-v8_41 - next runs compare against it; dec1 changed nothing posted).
Before it: "Op dialog batch 10" (regression ~/mastercam-re/regress/run_opdlg_b10, all
0-diff after its blessings; fileop 0/189, post 0/16 (also with MCAM_CUTTER_COMP=1, MCAM_FACE_LINK_FEED=1, MCAM_DRILL_CYCLES=1,
MCAM_ARC_FILTER=1 + MCAM_ARC_FILTER_PLANES=1), dlg_fileop_cmp 204/205, pf_roundtrip 305: 275 ok / 30 refused / 0 FAIL, MCP SMOKE OK). Qt only (nuklear removed); tests = Qt vs golden (tools/tests/README.md "Golden files", BLESS=...).
No agent left partial edits: batch 5 was stopped before touching source (backup pre_opdlg_b5_2026-10-23.tgz taken).

Done this session (sections above, in order): planes + review fixes; Stock Setup rest (solid / STL / shaded / regen
marking / stock plane Select) + review fixes + large-STL memory hardening; Bounding Box rest (view arcs, X/Y cylinder
circles, live solid + Solids, Push Pull, Create Tool Plane) + review fixes; AutoCursor point-pick snapping (cursor-ray
search, live marker, body-box cull, Front/side Nearest + intersection); contour roll 0x5d3 (user: new contours roll
like Mastercam); op dialog batches 0-4 (formats/opdlg_fields.tsv, template pages, Depth Cuts, Lead In/Out entry/exit);
nuklear removal + golden tests; Machine Group Properties Files + Tool Settings (+ review fixes, loader chain);
performance pass.

Done after that: **Batch 3 + 4 review** (section above; formats/opdlg_port_audit.md 8.1): 4 fixes, regression
~/mastercam-re/regress/run_b34fix all 0-diff (op 0/219 with the new scene rt_lead_b34fix, fileop 0/189, post 0/16,
dlg_fileop_cmp 204/205). Open from it (8.1 "Plausible"): end Extend's drop of a last element (gt +0x70 NOT DECODED), the
gouge check on open contours, taper for non-2D contour types, the taper's tip comp D/2 - still nothing NCI-verified.
Then **op dialog batch 5** (section above; formats/opdlg_port_audit.md 9): compensation Control / Wear / Reverse wear behind
MCAM_CUTTER_COMP=1 (G41 / G42 D / G40, LinuxCNC checks; formats/post.md), optimize / corner rounding / corner break / tip comp,
Multi Passes and Tabs groups live, vertical / ramp tabs + angle, Break Through (all opcodes set_depths knows). From the 8.1
review: taper for 2D chamfer done (user), the over-length Shorten now does what FUN_1800566e0 does (user), the taper's tip comp
D/2 does not apply to the 2D contour (9.4).

Open, in the planned order:
1. (done: batch 3 + 4 review)
2. (done: batch 5, formats/opdlg_port_audit.md 9; the user's questions in 9.6 - Wear on LinuxCNC, the D word, stored NCI
   with comp codes. Still greyed: Tab Cutoff, Full tabs, tab placement, keep tool down, MP feed / spindle overrides, look
   ahead, contour types Ramp / Remachining / Oscillate.)
3. (done: batch 6 Pocket, formats/opdlg_port_audit.md 10; the user's questions in 10.6 - Minimize tool burial / medial axis,
   decoded zigzag as default, finish corner smoothing gate, 0x126b gate, entry feed gate. Still refused: Constant Overlap
   (pbmap) and Morph Spiral, trochoidal modes, pocket types Facing / Island facing / Remachining / Open, keep tool down, spring
   passes, F / S overrides.)
   (done: batch 7 Facing + Slot, formats/opdlg_port_audit.md 11; the user's questions in 11.6 - stored Face ops to the decoded
   engine (BRACKET4 / bracket4-dashed op 8 then equal Mastercam's NCI, buckle-v8 op 41 move for move), the feed-between-cuts
   opt-in, the exact-arc slot ramp for Output helixes as arcs, slot comp on LinuxCNC. Still greyed: 3020 1847 / 1724 / 1438,
   Face 1310 / 1335; Dynamic facing unverified (no NCI anywhere).)
   (done: batch 8 Drill / Circle / Helix / Thread, formats/opdlg_port_audit.md 12; the user's questions in 12.6 - drill cycle opt-in,
   stored dwell, tip comp breakthrough, helix feeds, part post text. Still greyed: drill subprogram / custom cycles, F / S overrides,
   helix finish methods 1 / 2, thread taper.)
   (done: batch 9 2D HST, formats/opdlg_port_audit.md 13; the user's questions in 13.6 - Dynamic Contour (MCCECalcOffset sign), Peel
   (PreparePockBoundsPeel), entry methods 1 / 2 / 4 / 5, F / S overrides, Probing Example plunge. A queued perf fix in mcce_dyn.c
   measure_block: done later (dynperf, crop_band, merged).)
   (done: batch 10 Arc Filter, formats/opdlg_port_audit.md 14; the user's questions in 14.6 - stored filter-on ops, XZ / YZ opt-in,
   own refit after the filter, the stored-XZ-arc post finding, total-tolerance bookkeeping. Still greyed: smoothing, total / % / sliders.)
   (done + merged: dynperf (B9-5 speed; plunge investigated, not fixed), mpint (P1-P3), hst (B9-1 / B9-2; open: B9-3 CalcMedAx port +
   leaf builders, B6-1 decode = mcmill POCKMDL.cpp medial axis, Peel style / finish / both-extensions issue - formats/opdlg_port_audit.md
   "HST engines (post-batch 9)" H.7 questions to the user).)
   (done: decisions batch dec1, formats/opdlg_port_audit.md 15 - the answers to 10.6 / 11.6 / 12.6 (formats/user_decisions.md). Left:
   zigzag + Minimize tool burial still refused (FUN_1801f0540 + find_path_around_island burial branch to decode; new Mill_Inch zigzag
   pockets are refused until then); the medial axis (CalcMedAx, another agent) then replaces the burial fallback + warning; slot rough /
   finish loops still polygons.)
   (done: batch 11 post-side pages, formats/opdlg_port_audit.md 17 - Coolant / Misc Values live, Canned Text / Home / Ref. Points shown;
   post-values-only OK keeps Mastercam's toolpath and rewrites its NCI section-start records; LinuxCNC coolant opt-in MCAM_COOLANT=1.
   Left from the batch 11 row: Planes origins + Select Point, Force tool change in the post.)
   (done: batch 12 frame + Tool page extras, formats/opdlg_port_audit.md 18 - Apply / Hide dialog / Reload defaults / Tool page filter
   live, Force tool change opt-in. Left greyed with decoded reasons: Save defaults, Feed speed calculator (CalcFeedDialog), Preview
   toolpath, tool field edits (Cnc::Tool system slots), RCTF effect, Recalculate values handlers, Toolpath Type page, Planes origins.)
4. Windows build of the Qt app (phase 9 rest).
5. Small leftovers: cui_stock.c 1004 / 1097 curve-chord default test `F.metric == 0` (source of 0.001 / 0.02 not
   verified); Alt+F12 rotation-centre pick has no hover marker; (dialog 977: done, "Dirty operations"); chaining Chain Feature + Drill Point Selection automatic modes not ported; Toolpath Type page (change op
   type) not ported.

Waiting on the user:
- `user-tools.TOOLDB`: the loader chain makes BRACKET4, bracket4-dashed, NAKTO-BRAKE-BRACKET, tramming-rod, thread-test
  (and test-cube-10mm already) use %MCAMDIR%mill\Tools\user-tools.TOOLDB, which is not on this machine (searched
  <windows>, <data-disk>). Tool List shows the starter tools + Mastercam's "Invalid file name" warning. Options: the
  user copies the file into the shared folder, or a fallback to mill_mm.tooldb (would be "ours").
- Panel keys: replace panel_keys.tsv last-value persistence with Mastercam's 4-entry autocomplete MRU (decoded,
  formats/wireframe.md "Panel key storage")?
- Desktop check of the new OK progress / Cancel (only tested offscreen).
- Dirty operations D1-D4 (formats/opdlg_port_audit.md 19.6); D3 needs a look at Windows Mastercam (image of an expanded dirty op).
- 3D toolpaths: confirm decisions 3D-1..4 (formats/user_decisions.md, chosen by the assistant for Dynamic OptiRough: oracle via the
  corpus TUTORIAL 7 op, OCCT BRepMesh drive mesh, OptiRough first, SAFE links) and the patch's open points (hst3d_port_plan.md 6).

## STATUS / HANDOFF (2026-10-24, everything merged; master 40cc103)
This supersedes the handoff above where they differ. All branches are merged and committed, and no worktrees are left.

Merged, in order:
- 9c10186 dirty operations (opdlg_port_audit.md 19);
- fe01be3 dec2 (20);
- 63273de + 105ea67 medial axes (21);
- 0a233af Save parameters to defaults file + Preview toolpath (22);
- 96c33d0 Simulator window for Verify (formats/simulator.md; mainwindow.h keeps SimulatorWindow plus master's DirtyOpsDialog);
- fbbd1ca NCI writer (formats/nci_writer.md). Merge note: cui_ops.c post_ops_mp takes dialog 977's "Do not regenerate" mask,
  and mcam_saveops.c captures the record on top of op_fields_to_record.

Final regression: ~/mastercam-re/regress/run_merge_all.
- All suites 0 differ; fileop 0/189; post 0/16 by default and with each of the 6 switches.
- dlg_fileop_cmp 204/205 (NAKTO tree 3, known); one shot differs (tool_list_report date, known).
- Oracles unchanged (Dynamic p95 median 0.462).
- nciw NC oracle: 8/8 programs identical; NCI records 6480/8347.

User answers (user_decisions.md, 2026-10-24): D1-D4, M1-M4, S-D1, P-D1, P-D2, SIM1-3 and N1-N3, all as recommended.
Next: formats/TODO.md "Next in the main tree", items 3-5:
- the follow-ups S-D1, D2, N1 and SIM1;
- then Solids / OCCT;
- later M2 / M3 and N3.
Still waiting on the user: Q31 (T5 at 7 mm), and three checks on Windows (the solid proof files, 3D roughing test parts, and D3).

## STATUS (2026-10-24, later): UI-1 e7c29fe + S-D1
- UI-1 committed (dialogs at the right-hand edge; 19 dialog scene shots re-blessed, placement only). Open point for the user:
  Backplot Options opens over the Backplot dialog (both at the right edge; Chaining Options has its own "left of Chaining" rule).
- S-D1 done (opdlg_port_audit.md 22.8): X9 op update (mcprvverfile.dll) + the delete + append branch of Save to defaults; loader
  skips ASSOC records with selection bit 1 off. Regression ~/mastercam-re/regress/run_sd1: all suites 0 differ except op 2/392 (the
  two new scenes, blessed; dfl_stored_refused removed); scenes dumps 0/46; fileop 0/189; post 0/16 with every switch; oracles,
  mpint, mp oracle, hst3d, nciw NC 8/8 / records 6480/8347 unchanged; dlg_fileop_cmp 1/205 (NAKTO, known). hst's "vs base" column
  is void (the base tree ~/mastercam-re/work/tree_dec2_head is gone); its gouge counts are unchanged.
- Next: N1 (started, notes in TODO.md), D2, SIM1.

## STATUS (2026-10-24, evening): all on master
- Committed since the last STATUS: ec531e2 Solids manager history, a57359d OptiRough stock (decision R-1), cfbc5b8 XWayland cursor,
  and the double-click / solid dialog commit (formats/analyze.md 9). Full regression ~/mastercam-re/regress/run_dbl: every suite
  0 differ (misc 0/83 with 4 new scenes), scenes 1/46 shot (tool_list_report date), dlg_fileop_cmp 1/205 (NAKTO, known), oracles
  unchanged except hst3d (ref->ours p95 1.024 / ours->ref 1.040, cut length 19981 vs 20093), nciw 8/8 + 6480/8347.
- Branch `srough` (worktree ~/mastercam-re/work/wt_srough): f3e48ec = an earlier session's unfinished classic Surface Rough port
  (formats/surf_rough.md; cam3d_srough.c missing) - committed there, NOT merged.
- Next: the idle consumer of Mastercam's dblclk event (decode), OptiRough PosZLo, N1, D2, SIM1, then solids step 2 (OCCT replay).

- 2026-10-28 (formats/surfaces_tab_plan.md): Surfaces > Create > Offset (OffsetSurfaces) set to `off` in tools/tests/golden/parity/
  mcamui_commands.tsv by hand - mcamui enabled it only because its label matched the wireframe Offset (it ran the wireframe command, a
  bug); the Surfaces-tab FT routing greys it until OffsetSurfaces is ported.
