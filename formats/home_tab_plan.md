# Home tab - plan and status (started 2026-10-28, branch toolpaths2)

User request 2026-10-28: "get everything in the home tab working" (before the multiaxis work, user_decisions.md M-1..M-3).
Ribbon: viewer/ui/layout.txt TAB Home (FT functions in mastercam.exe "EXE", MCCADUTIL.DLL, MCCAD.DLL, MCCORE.DLL).
Ghidra: mccadutil.dll / mccad.dll = ~/mastercam-re/ghidra_cad (project cad); mccore.dll = ghidra_core (mccore); mastercam.dll /
the EXE handlers = ghidra (mc2017).

## Status per command (2026-10-28)
| group | command (FT) | status |
|---|---|---|
| Clipboard | Paste / Cut / Copy (OnEditPaste / OnEditCut / OnEditCopy) | WORKS (core_ui cui_misc.c; the ribbon now falls back to the QAT / keymap FT functions, mainwindow.cpp cmdOk / invoke) |
| Clipboard | Copy Image (ToClipboard @1802732d0) | WORKS: opmanres 3288 prompt, two corners or Esc = the whole graphics view, the bitmap to the clipboard; failure 3289 (mainwindow.cpp copyImage) |
| Attributes | Point style / Line style / Line width combos (ChangeSelectedEntity*) | WORKS: CBmpCombo2 bitmaps (opmanres 0x61c9.. / 0x61b7.. / 0x61bd..); the selection (FUN_1804670d0 / FUN_180467430) or the main attribute (config 10002 / 10000 / 10001); one undo step; saved (ent +0x18 / +0x1c / +0x10) |
| Attributes | 2D / 3D (OnScreen2D3DToggle @180470200) | WORKS: constr_view_is_3d (config 16009); 2D puts every new point at the Z depth (the rule is ours, see cui_attr.c) |
| Attributes | Default Wireframe / Solid / Surface Color (ChangeSelected*Color(WithPicker)) | WORKS: the Colors dialog; OK = the kind's default (main_color / solidColor = config 5052 / surfaceFrontColor) + the selection's colour (FUN_180469e70 / FUN_180466d70). The live picker with a selection: the same dialog (ours) |
| Attributes | Clear Colors (ClearColors) | WORKS |
| Attributes | Set From Entity (set_main_from_entity) | WORKS (QAT path) |
| Attributes | Set All (ChangeEntityAttribs @18046bc80) | WORKS: prompt opmanres 186, dialog 192 (CAttribs::UpdateEnt rules); Force visibility / Surface Density / EA Mgr / Same as...: NOT PORTED (greyed); start values (LoadGlobals) ours |
| Organize | Z depth (OnScreenZDepth @180470cc0 -> FUN_1801adce0), Level combo | WORKS: mccoreres 2388 pick (AutoCursor, the point's C-plane Z) or a typed depth in the Z combo; status bar Z; the Level combo = the main level (typed) |
| Organize | Change Level (ChangeEntityLevel @18046c210) | WORKS: prompt opmanres 179, dialog 516 Move / Copy, Use Active Level, Select... (Levels picker); Copy of splines / trimmed or offset surfaces and Force visibility: NOT PORTED |
| Delete | Delete Entities (delete_entities) | WORKS |
| Delete | Non-Associative (delete_all_nonassoc) | read: below |
| Delete | Duplicates / Advanced (delete_duplicates_basic(_options) / _advanced -> del_duplicate) | partly read |
| Delete | Undelete Entity (undelete_single) | read: below |
| Display | Hide/Unhide (OnHide) | WORKS (QAT path) |
| Display | Hide More / Unhide Some (OnHideMore / OnUnhideSome) | not decoded |
| Display | Arc Center Points / Endpoints (OnDisplayArcCenterPts / displayEndpoints) | not decoded |
| Display | Blank / Unblank (blank_entities / unblank_entities) | read: below |
| Analyze | Analyze Entity (AnalyzeStart) | WORKS |
| Analyze | Distance, Toolpath, Dynamic, Position, Angle, Draft Angle, Chain, Contour, Check Solid, Solid Properties, Test Surface, Curvature, 2D Area, Surface Area, Statistics | not decoded |
| Add-Ins | Run Add-In (run_user_app) | out of scope (runs Windows add-in DLLs) |

## Decoded so far (mccadutil.dll; mccore.dll)
- **blank_entities** @18006c500: UnBlankOff; status "Blank"; select_ents(prompt, mask 0x1f1fff, mode 2); on OK FUN_180065a50; UnBlankOff.
  **unblank_entities** @18006ccf0: the same with UnBlankOn, status "UnBlank", select_ents mode 0.
- **FUN_180065a50** @180065a50: RemoveSelectedGeometryFromMiscOps; undo transaction; delete_all_by_mask(4, 0x1f1fff, 1, 1);
  validate_toolpath when perform_tp_cleanup; DfRedrawDraft.
- mccore **delete_all_by_mask(sel, mask, quick, ask)** @1802939b0: quick (param 3 != 0) -> FUN_180292f80 @180292f80: every entity whose
  selection word has the bits `sel` and whose type is in `mask` (and on mask_level when set): through the display list (FUN_180293ff0(dl,
  ent, 1)) or, while UnBlanking() or without a display list, the database (delete_ent(ent, 1, 1, 0, ..)) - so blanking / unblanking is
  delete_ent under the UnBlanking state (delete_ent @180294860 NOT READ YET).
- **delete_all_nonassoc(sel)** @18006c600: undo delete transaction; sel != 0: select_ents(mask 0x1f1fff, mode 2), walk the selected (4),
  else every entity (1); each with EntIsAssoc == 0 is delete_ent'ed; commit when any.
- **EntIsAssoc** @180065de0: 1 ent +0x28 > 0, 2 ent +0x38 > 0, 3 is_solid_dependent_on_curve, 4 DfDraftDependentOnEnt, 5 ent +0x54 > 0,
  6 IsReliefDependentOnCurve, else 0.
- **delete_duplicates_advanced** @18006c810: select_ents, del_duplicate(1, 1, 1, 4, mask, sel != 0, 0, 0, 0); basic = delete_duplicates_basic_options(.., 0).
- **undelete_single** @18006cdf0: pop_del_stack -> undelete_ent(ent, 1) + undeleteUndo; empty stack -> MessageBeep.
Raw dumps: decomp/home/disp_del.c, core_del.c, f292f80.c (gitignored).

## Attributes / Organize (2026-10-28): code
core_ui/cui_attr.c (attributes, the selection-first commands selrun_*, ent_duplicate, the Z depth, the templates CLDLG_516 / ADLG_192,
script steps lnstyle / lnwidth / ptstyle / defcolor / chlevel / setall / selrunok / selrunesc / chlevelok / setallok / zdepth / zdpick /
zdpt / cplane3d / attrdump); viewer-qt homedlg.cpp (Change Levels, Attributes, the bitmap combos) and mainwindow.cpp (the FT functions,
the ribbon combos, the 2D / 3D status button, Copy Image, test step copyimage); Ent.width / attr_edited (mcam_load.h) and the save's
in-place attribute rewrite (mcam_saveops.c); the renderer's width pass (scene.c, base width + (width - 1): ours). The ribbon's button
groups now show every entry (the Attributes colour group has six). The graphics right-click menu's mini toolbar runs the same
functions. Scenes: misc_scenes.txt home_attr / home_attr_part / home_copyimage.

## Next
1. delete_ent (blank bit / delete stack), del_duplicate, the Hide More / Unhide Some handlers (EXE).
2. The Attributes combos and colours, Change Level, Z depth.
3. Analyze family (AnalyzeBtwnPtStart & co, mccadutil).
