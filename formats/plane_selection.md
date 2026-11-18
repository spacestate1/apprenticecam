# Plane Selection: get_plane_info (mccore 0x180a2aea0) and dialog 1085

The plane prompt Mastercam's commands call for "a plane" (Surfaces > Extend "To plane", Trim to Plane, Curve Slice, Draft
Surface, ...). Port: `viewer/core_ui/cui_plsel.c` (the loop + the dialog model), `viewer-qt/src/plseldlg.cpp` (the dialog).
Status 2026-11-17: decoded and ported as a standalone module; no command calls it yet (the callers' branches wire it). Second pass
(user answers P-1..P-7): every stand-in of the first pass replaced by the decoded behaviour or refused; section 7 lists what is left.

## Sources (gitignored dumps, decomp/planesel/)
| file | what |
|---|---|
| get_plane_info.c, gpi.c | get_plane_info (1606 lines) + its callees, depth 1 (DumpDecomp, mccore) |
| dlg_range.c, msgmap.txt, small.asm | CPlaneDlg 0x180238130-0x180239800 (ctor FUN_180238280, DoDataExchange FUN_1802385c0, OnInitDialog FUN_180238a20, handlers); the AFX message map at 0x180d320c0 (32-byte entries, parsed); the one-line handlers 0x180238870.. disassembled from the DLL file (Ghidra had no functions there) |
| sym.txt, vt.txt | CPlaneDlg RTTI / vftable 0x180d31d38 |
| mccoreres_dialogs.tsv, mccoreres.json | tools/pe_dialogs.py / ui_res_extract.py over mccoreres.dll (dialog 1085, the string table) |
| selcue.c, selcue2.c, mcreal_core.c | SelectionCue 0x1804dd7b0 -> FUN_1804dccb0 / FUN_180708aa0 -> FUN_18027b600 (the picks) -> MCReal::SelcueEnd 0x180510630 -> FUN_180510950 -> FUN_18060f350; MCReal ctors (PlaneType 0) |
| flat2.c, flat3.c, distpt.c | mastercam.dll Plane::CheckFlat2lines 0x1803d7f70, GetMatrixFrom2lines 0x1803da0d0, FUN_180130980 (unit); mccore dist_pt_l_3d 0x180013bd0 |
| arrow.c | draw_plane_arrow 0x18053a2e0 (draw_plane 0x180539ae0 / erase_plane 0x18053ae10 are in gpi.c) |
| sld_planar.c | mcgeomsld sld_is_surf_planar 0x1800bdec0, sld_check_face_normal 0x1800aecc0 |
| core_misc.c, core_refs.txt, spin_users.c | mccore t_on_line, proj_iso_pt, DDX_ProcessEditReal, make_mcreal_str, IsSelectionCueString, get_current_sel_mask; the spin globals' users (FUN_1803bc480 defaults, FUN_1801d7810 config 32000..32005) |
| mc_misc.c, mc_res.c, mc_cues.c, mc_p3ds.c, mc_p3d.c, mc_mcdialog.c | mastercam.dll ActiveOriginIsOn, GetActiveOriginInViewCoordinates, get_y_scr_scl, GetMastercamResourceHandle, CMastercamApp::LoadSelectionCues, GetActiveOrigin, P3D / P3DS, MCDialog::OnInitDialog |
| ui_validate.c, ui_onok.c | uictrls UIEditReal::Validate / InvalidRealField / DisplayErrorTip, UIEdit::Evaluate / EnableBuddyCtl, UIDialog::OnOK / OnCancel |
| gpi.asm | get_plane_info disassembled from the DLL (every access to the saved plane [RBP+0x270]) |
| mcreal.c | mastercam.dll GetSelectionCues 0x180321240, SelectPlaneFromList overloads 0x18043c620 / 760 / 790 |
| callers_core.txt, callers_cad.txt, cc1..6.txt | the callers and their call sites' argument registers (CallContext) |

## 1. Signature, callers, return value

`void get_plane_info(short prompt_id, PLANE_sf &plane, char *ok, CWnd *parent, HINSTANCE res, uint cmd, PlaneEventData *ped)`
(export ordinal 10967). `get_plane_info_without_waiting(prompt_id, plane, ok, parent, res)` (0x180a2d0a0) = the same with the global
DAT_181444171 = 1 around it (the loop leaves at the first accepted plane), cmd 0, ped NULL.

**PLANE_sf** (0x48 bytes, world coordinates): `+0x00 p_3d origin`, `+0x18 p_3d normal`, `+0x30 p_3d X direction` (a unit vector in
the plane). On return with `*ok = 1` the normal and the X direction are unit vectors (see 4.9).

Callers (mccad, CallContext): FUN_180337330 (Surfaces > Extend: twice, prompt_id 0x1d21 = mccadres 7457 "Select plane to extend to",
res = the current resource handle = mccadres), FUN_18008f0f0 (Trim to Plane, worker +0xd0, prompt 0), FUN_18028d0d0 (Curve Slice,
prompt 0), DraftSurface 0x1801273d0 (prompt 0), FUN_180097280 and FUN_18009f470 (prompt 0; not identified). Every caller passes
parent NULL, cmd 0, ped NULL. `*ok` is set to 0 at entry.

## 2. Dialog 1085 "Plane Selection" (mccoreres, 143 x 268 du, modeless)

CPlaneDlg : MCDialog(0x43d, parent, 1, false, 1), created by FUN_1802387e0 (vt +0x2d0 Create with the core resource handle) when
cmd == 0, else cmd is posted as the first message (no caller does this - NOT PORTED).

| id | control | rect (du) | text / tooltip | bitmap (FUN_180238ff0, 0xc0c0c0 transparent) | handler (message map 0x180d320c0) |
|---|---|---|---|---|---|
| 963 | group | 7,7 128x229 | "Plane" | | |
| 4466 / 4467 / 4468 | bitmap button | 13,25 / 48 / 71 20x18 | "X" / "Y" / "Z" | mccoreres 5674 / 5675 / 5676 | FUN_180238ec0 / ef0 / f20: UpdateData(TRUE), post 0x66 / 0x67 / 0x68 |
| 964 / 965 / 966 | MCReal edit | 35,27 / 50 / 73 50x14 | | | EN_KILLFOCUS FUN_180238ae0 / b30 / b80; spin UDN_DELTAPOS FUN_180238bf0 / c50 / cb0; Enter: PreTranslateMessage FUN_180238f70 |
| 4480 / 4481 / 4482 | bitmap button | 88,25 / 48 / 71 20x18 | "Select point" | 5673 (DDX 0x1180.. -> +0x6e78..: SetBitmap 0x1629) | FUN_180238d70 / de0 / e50: SelectionCue("X" / "Y" / "Z", the MCReal) |
| 4483 / 4485 / 4486 | bitmap button | 110,25 / 48 / 71 20x18 | "Construction depth" | opmanres 3451 (0xd7b with GetMastercamResourceHandle = the app's +0x7e0 handle = opmanres: it holds 3451 and the cue strings 26980..) | 0x180238890 / 8b0 / 8d0: value = constr_depth (mccore ConstrDepth constr_depth +0) |
| 1258 | bitmap button | 53,101 36x18 | "&Line" | 1126 | 0x180238bd0: post 0x69 |
| 1262 | | 53,122 | "&Three Autocursor Points" | 1198 | 0x180238870: post 0x6a |
| 1265 | | 53,143 | "&Entities" | 1199 | 0x180238950: post 0x6b |
| 1269 | | 53,164 | "&Normal" | 1200 | 0x180238be0: post 0x6c |
| 4470 | | 53,185 | "N&amed" | 1197 | 0x180238880: post 0x6d |
| 575 | | 53,206 | "Flip" | 1123 (1124 while the flag is set) | FUN_180238960: UpdateData(TRUE), flag +0x12b90 toggled, bitmap, post 0x6e |
| 1 / 2 / 9 | buttons | 24 / 62 / 100,243 36x18 | OK / Cancel / Help | | OnOK FUN_180238d10 (only with the OK button focused): DestroyWindow, post 100; OnCancel FUN_1802388f0: DestroyWindow, post 0x65; Help: MCDialog::LaunchHelp (NOT PORTED, greyed) |

"Posting" = `PostMessageA(0, 0x8028, code, 0)`: get_event returns it as event 0x200 with the code at MCEvent +0x158.
Edits: DDX_ProcessEditReal over the MCReal values at dialog +0x5090 / +0x5e88 / +0x6c80 (= MCReal +0xc00). OnInitDialog:
InitializeAutoTips, the window at the remembered position (DAT_1813c7988, the window rect saved by DestroyWindow FUN_180238590 -
**the only thing remembered between calls**), EnableBuddyCtl(-100, 100) on the three edits (uictrls 0x18000f210: an up-down control,
style 0x50000024 = UDS_ALIGNRIGHT | UDS_ARROWKEYS, buddy = the edit), the bitmaps, UpdateData(FALSE). The first opening (the rect empty):
MCDialog::OnInitDialog (mastercam.dll 0x1802ec930) puts it at the top left of a main-frame pane (FUN_18031a100: identity NOT
DECODED) or centres it; the port opens it at the program's right-hand edge (user decision UI-1) and keeps its position afterwards.
The dialog's MCReals (MCReal::MCReal(void) 0x18050f570): 6 places (+0xd94), trailing zeros cut (+0xd9c 1), no sign (+0xd88 0), not an
angle (+0xd84 0), selection cues on (+0xd80 1), bounds -1e38 .. 1e38.

- EN_KILLFOCUS / Enter in an edit: UpdateData(TRUE); `tol < |last - value|` (last = +0x12b78..) -> FUN_180239430 (X; Y / Z alike):
  the other two values 0, the edit's text = make_mcreal_str(value, 10 places), UpdateData(FALSE), post 0x66 (0x67 / 0x68), last := the
  values. Spin (UDN_DELTAPOS, no UpdateData): value += iDelta x spin_metric_float / spin_inch_float (mastercam.dll; defaults
  FUN_1803bc480: 0.1 inch, 1.0 mm; config 32001 / 32004 via FUN_1801d7810) then the same.
- DDX_ProcessEditReal (mccore 0x1804dbd20): load = make_mcreal_str(value, 6) (0x1804dd8d0: "$$$$$$$$$$$$$$$" from 1e38, "%.*f",
  trailing zeros cut to one); save, per edit in order 964 / 965 / 966: '?' posts the edit's right click (its menu); a selection cue
  string (IsSelectionCueString 0x1804dd6c0: one letter of "XYZRDLSHQBOTPW", upper-cased) runs SelectionCue first (section 3);
  UIEditReal::Validate (uictrls 0x180013de0): non-empty, UIEdit::Evaluate (the evaluator object m_pDfltEval), "%.12f" matching
  "\d.\d", within the bounds -> the value; else InvalidRealField (0x180013c10: the error tip "<min> to <max>", each "%.*f" at
  n_places_an = config 29003, the text back to the value in that format) and the exchange fails (the edits after it not read; the
  handlers do not look at UpdateData's result); the text = make_mcreal_str(value, 6).
- OK (FUN_180238d10) -> UIDialog::OnOK (uictrls 0x180009ef0) -> CBCGPDialog / CDialog::OnOK: UpdateData(TRUE) first.
- "Construction depth": value = constr_depth, then the same FUN_180239430 path.
- "Select point" = the selection cue (section 3).

## 3. The X / Y / Z selection cue ("Select point")

SelectionCue("X", MCReal) (mccore 0x1804dd7b0): MCReal::GetXyzPopup false (ctor +0xdf0 = 0) -> IsSelectionCueString -> FUN_1804dccb0
finds the cue whose first letter is 'X' in GetSelectionCues' list (mastercam.dll 0x180321240; filled by CMastercamApp::LoadSelectionCues
0x18028faa0 from opmanres 26980..26994: "X  =  X coordinate of a point", Y, Z, R radius, D diameter, L length, S distance, "Angle menu",
O one line, T two lines, P points, W arc sweep, H solid hole diameter, Q drill tables, B tap tables: X / Y / Z = cues 1 / 2 / 3, the
coordinate cases of FUN_180510950) -> FUN_180708aa0 ->
FUN_18027c1e0: the dialog suspended (AfxSuspendWindow), FUN_18027b600: cue 1..3 = one point, prompt mccoreres 1872 "\nSelect a point",
get_event(prompt, ev, 1, 0, 0) (Esc: nothing, the dialog resumes) -> MCReal::SelcueEnd -> FUN_180510950(MCReal +0xdec = PlaneType 0
= Cplane, ...) -> FUN_18060f350: world_to_view(point, the Cplane's view), less GetActiveOriginInViewCoordinates (the active origin
through world_to_view) when MastercamPlanes::ActiveOriginIsOn(0) (mastercam.dll 0x1802c4830: GetActiveOrigin's length above the P3D's
own tolerance +0x20 = 2e-7 (P3D::P3D; operator= copies it); GetProdEnableBits() & 0x10 forces it on - the license bits DAT_1808d6be4,
taken as 0 as in every port), coordinate x / y / z (CoordDisp 0). SelcueEnd: the value (inside the bounds), the text make_mcreal_str(v,
6); true -> FUN_180239430 / 1802394f0 / 1802395b0 as a typed value. The same pick runs when a cue letter was typed into an edit and
UpdateData(TRUE) reads it: its value / text, then the handler goes on (the port runs the handler again after the pick); Esc there:
SelectionCue false, the letter fails Validate (InvalidRealField). The other cues (R .. B: radius, length, hole, tables ...) are refused
with a message (NOT PORTED).

## 4. get_plane_info

### 4.1 Entry
- The core resource handle current; MCEvent; prompt = mccoreres 7436 "Select Plane" when prompt_id == 0, else string prompt_id
  from res (or the current resource); the prompt is kept as the "default prompt" (local_d948) and every finished pick returns to it.
- `*ok = 0`; the dialog made (FUN_180238280) and created.
- FUN_180a2d0e0 (validity): every coordinate of the plane below 1e38 in magnitude, |normal| > tol and |X| > tol.
  - invalid: plane = origin (0,0,0), normal (0,0,1), X (1,0,0); the dialog values stay 0; the saved copy local_d648 ([RBP+0x270]) is
    never written - the three accesses are this save and the two restores (gpi.asm 0x180a2b27c / b3f3 / b617), so Cancel / Esc then
    copy an uninitialised stack area into the plane (undefined in Mastercam itself). The port refuses that restore: the plane stays as
    it is and ok = 0.
  - valid: o' = world_to_view(o - active origin, Cplane); the values (FUN_180239400, members only, the edits keep "0"): if x' == 0:
    y' == 0 and z' != 0 -> (0,0,z'); y' != 0 (or NaN) and z' == 0 -> (0,y',0); else (0,0,0). x' != 0: y' == z' == 0 -> (x',0,0) else
    0. The origin put back (view_to_world, AddActiveOrigin); the plane saved (FUN_180a2a510 -> local_d648).
- Then **origin, normal and X are put through view_to_world of the active Cplane once more** (no origin added) - a no-op with the
  Cplane at Top; the port does the same.
- draw_plane(origin, normal) (cmd == 0).
- Loop state: event mask 0x200 (dialog messages only), entity mask 0x8e, mode 0, "X known" (bVar18) true, bVar19 true.

### 4.2 The loop
`get_event(prompt, ev, mask | (mask >> 14 ? sel 0x400000 : 0), sel, 1, &go_on)` while go_on; get_event sets go_on = (its return == 0)
(FUN_1804e9920, decomp/mccore_getevent.c). Esc: the 0x802a message, RunPump returns 1 (formats/backplot.md), FUN_1804e34f0 returns 1 when
no window / polygon selection is being drawn (decomp/getevent/mccore_getevent_msgs.c l.216): get_event returns 1 -> erase_plane,
repaint, the saved plane back, ok = valid; go_on 0: the loop ends.

A click (get_event's click handler FUN_1804e3b90, mccore_getevent_msgs.c l.611-797), in this order: an AutoCursor snap
(FUN_1804e27d0) with mask bit 0 -> a point event; else with the solid bits 0x7000 a solid face / edge under the cursor (FUN_1804e0390)
-> event 0x2000 / 0x4000; else with the entity bits (2 / 0x10 / 0x400) select_ent_univ (FUN_1804dff70) -> event 2; else with bit 0
the free point (map_cursor_to_3d: the construction plane at the construction depth) -> event 1. After each event: the third-point flag (4.6); `DAT_181444171 && ok` -> leave; `cmd && ok`
-> post 100.

### 4.3 Dialog messages (event 0x200)
| code | |
|---|---|
| 100 OK | erase_plane, repaint, go_on = 0; ok = valid(plane) (valid: the PlaneEventData copy when ped != NULL - NOT PORTED, no caller) |
| 0x65 Cancel | erase_plane, repaint, go_on = 0; plane = the saved one; ok = valid |
| 0x66 / 0x67 / 0x68 | FUN_180238760 reads the three values; plane origin = view_to_world((v,0,0) / (0,v,0) / (0,0,v), Cplane) + active origin; normal = view_to_world((1,0,0) / (0,1,0) / (0,0,1)); X unknown; drawn; ok = 1 |
| 0x69 Line | prompt 7437 "Select line in construction plane"; sel = 2; mask \|= 0x4002; mode 1 |
| 0x6a Three points | prompt 1878 "Select first point"; sel = 1; mask \|= 1; mode 2 |
| 0x6b Entities | prompt 7438 "Select a flat entity, 2 lines, or 3 points"; sel = 0x19ff; mask = 0x6202; mode 3 |
| 0x6c Normal | prompt 5094 "Select the normal line"; sel = 2; mask \|= 0x4002; mode 4 |
| 0x6d Named | SelectPlaneFromList(0, NULL, 0, 0) (mastercam.dll 0x18043c760 -> FUN_18043c260: dialog 842 over every plane, nothing preselected). No plane: post 0x65 (= Cancel of the whole get_plane_info), bVar18 = bVar19. A plane: **SetActivePlane(Planes, 0, id) - the active Cplane becomes it**; normal = unit(its view's row 3), X = row 1, origin = Plane::GetOriginInWorldCoordinates; X known; drawn; ok = 1 |
| 0x6e Flip | normal reversed; X reversed too when known; drawn |

The masks OR into the event mask (only Entities replaces it and a finished pick resets it to 0x200): after "Three points" then
"Line", free points still count.

### 4.4 Points (event 1)
Counter local_d940: 0 -> point 1, prompt 1879 "Select second point"; 1 -> point 2, prompt 5433 "Select third point"; else point 3 and
the three-point plane (4.6). No mode test.

### 4.5 Entities (event 2; 0x4000 solid edges the same path - NOT PORTED)
get_ent_from_eptr, the selection bit cleared (sel_bit_turn_off 4, rewrite_ent). By mode:
- **Line (1)**: d = p1 - p2 (ent +0x68 - ent +0x80); in the Cplane (world_to_view) |dx| or |dy| >= tol: normal = unit(Zc x d) (Zc =
  the Cplane view's row 3), origin = p1, X = p2 - p1, X known, ok = 1, prompt back, mask 0x200, mode 0. Else post_warning 5011
  "Warning - zero length line" (mode kept).
- **Normal (4)**: t = the pick's parameter (MCEvent +0x10); t >= 0.5: origin = p2, normal = unit(p1 - p2); else origin = p1, normal =
  unit(p2 - p1). X unknown; ok = 1; prompt back; mask 0x200; mode 0.
- **Entities (3)**, by ent class:
  - 1 point: stored as point local_d940 (prompts as 4.4), sel = 1 (points only); the third sets the third-point flag.
  - 2 line: the first: prompt 5330 "Select a second line", sel = 2. The second: prompt back; FUN_180a2ad80 = Plane::CheckFlat2lines(
    false, line 1, line 2): both lines at least tol long and GetMatrixFrom2lines succeeds (rows = the unit directions, each reversed
    when its end is nearer the other line than its start; row 3 = row 1 x row 2 > 2e-7, row 2 = row 3 x row 1 of length 1 within
    2e-7; failing, once more with line 2's start replaced by line 1's). With `false` the four points' distances along row 3 are not
    compared: **two skew lines pass**. Failed: post_warning 5346 "Selected lines do not lie in a plane", ok = 0, prompt 7438, sel =
    get_current_sel_mask() +0xc (mccore 0x180657590: the Selection bar's hard / maintained / one-time masks; with none set 0x1f1fff,
    formats/wireframe.md / cui_sel.c - this program has none of those masks). Passed: n = d1 x d2 (the lines' p2 - p1); |n| < tol: n = d1 x (line 2
    start - line 1 start); still < tol: post_warning 5345 "Cannot define a plane - lines are parallel" (as the failure). Else normal
    = unit(n), origin = line 1 start, X = d1, X known, ok = 1, mask 0x200, mode 0.
  - 4 arc: origin = view_to_world(the view centre +0x98, view +0xc8); normal / X = view_to_world((0,0,1) / (1,0,0), view +0xc8).
  - 8 / 0x80 spline: SplineIsFlat (0x180a2a970): n = ent +0xb0 (class 8) / +0xcc (0x80) as int; step = (t2 - t1) / (n + 9); P0 =
    eval(t1), two more samples; the first normal = unit((P1 - P0) x (P - P0)) once longer than tol; the later samples (to n + 9) test
    (P - P0) . normal > 1e-4 (metric) / 1e-6 (inch) - **one-sided** - not flat. No normal found: not flat. Not flat: post_warning 5329
    "Selected entity is not flat". Flat: normal, origin = ent +0x68; X unknown.
  - 0x10 note / 0x20 witness-leader: origin = ent +0x70, normal / X = view_to_world((0,0,1) / (1,0,0), view i16 ent +0x88).
  The pick parameter of a line (MCEvent +0x10, Normal's end choice): select_ent_univ (mastercam.dll, decomp/mastercam_seluniv.c
  l.483) t_on_line (mccore 0x1804b48a0) of the cursor on the line, both projected by proj_iso_pt (the graphics view's x / y axes):
  unclamped, 0 when the projection is shorter than tol. A surface's (u, v): select_ent_univ's display-list pick FUN_18013cbc0 on the
  surface's DlPoint records (formats/chaining.md "Which routine normal picking uses", decoded on branch
  worktree-agent-ab4658f97be21905b, not ported there either): a surface clicked here is refused with that message.
  - 0x40 dimension: origin = ent +0x310, view i16 ent +0x328.
  - 0x80 is the spline above; 0x100 surface: eval_surf(order 2) at the event's (u, v) (+0x10 / +0x18); surf_curv_geom -> k1, k2;
    k = |k1| <= |k2| ? k2 : k1; limit = tol (metric) / tol x 25.4 (inch); |k| > limit: post_warning 5329. Else normal =
    calc_surf_unit_norm(Su, Sv, u, v), reversed when ent +0x6e bit 4; origin = S(u, v); X unknown.
  - 0x1000 cross hatch: fill_in_ent, origin = (data[1], data[2], data[0]), view i16 ent +0xb0 - NOT PORTED (no cross hatching here).
  - 0x800: in the mask, no branch (nothing).
  Every taken entity: ok = 1, prompt back, mask 0x200, mode 0, drawn.
- Solid face (event 0x2000): sld_is_surf_planar(face): planar = Parasolid PLANE (class 0xfa1), or a B-surface (0xfa6, also under an
  offset 0xfa8) of form 0x21fe or degree 1 x 1; not planar: post_warning 5329; else sld_check_face_normal: the surface's position and
  normal at the middle of the face's uv box (PK_SURF_eval_with_normal), the position x 1000 (mm) / 39.37007874015748 (inch), the
  normal reversed when the face is not same-sense; drawn; X unknown; ok = 1. Ported (occt_mesh.cpp occt_xt_face_plane): the class
  from the XT nodes (PLANE; B_SURFACE whose NURBS_SURF surface_form = SCH_planar 3 = PK_BSURF_form_planar_c 0x21fe, or degree 1 x 1;
  under one OFFSET_SURF), the point / normal on the face xt_occt builds (same parametrisation, senses folded into the face orientation)
  at the middle of its uv box; part solids only (a solid made in this session: refused).

### 4.6 Three points (LAB_180a2cbca)
Prompt back, counter 0; a = p2 - p1, b = p3 - p1, c = a x b; |c| <= tol: post_warning 5347 "Selected points are either collinear or
2 of them are coincident" (mode kept); else normal = unit(c), origin = (p1 + p2) / 3 + p3 / 3, X = unit(a), X known, drawn, ok = 1,
mask 0x200, mode 0.

### 4.7 The end
ok: X unknown -> X = (0,1,0) x normal, or (1,0,0) x normal when ||n.y| - 1| < 0.1; X made unit (always). cleartext, clearprompt; the
dialog destroyed (its rect remembered); the old resource handle back.

### 4.8 draw_plane / erase_plane (GrTemp lines)
L = 60 / get_y_scr_scl() (mastercam.dll 0x18033ab00: view +0x770 = pixels per unit, decomp/mastercam_viewrot.c:334: 60 pixels); r = (0,1,0) when ||n.x| - 1| <= 0.1 else (1,0,0); a = unit(n x r), b = unit(a x n). Lines -Lb..Lb,
-La..La and the diamond -0.75Lb..0.75La, -0.75Lb..-0.75La, 0.75Lb..-0.75La, 0.75Lb..0.75La; draw_plane_arrow: o..o+Ln, the head
o+Ln -+ 0.15La and the two sides to o+1.25Ln. Colour 8 when the background colour is 7 or 15, else 15; attributes width 2.

### 4.9 PlaneEventData (ped, 0x7c50 bytes copied at OK)
3 x 0x2938 ent copies (the picked entities), the picks' parameters (+0x7ba8: 2 x 2 doubles), the Named plane id (+0x7bc8), the three
points, the surface evaluation. Used by CreateNamedPlane (0x180a2a550). NOT PORTED: no ported caller passes one.

## 5. The port (cui_plsel.c)
- `psel_begin(prompt, &plane, done, ctx, nowait)` = get_plane_info(prompt, plane, &ok, NULL, res, 0, NULL) (nowait 1 = the
  without_waiting variant). The plane is the caller's struct, written in place (as Mastercam). `done(ok, &plane, ctx)` runs where
  get_plane_info returns. One at a time (`psel_open`).
- Events: `psel_click(mx, my, w, ws, snapped)` in get_event's click order (4.2); `psel_ctl(id)`; `psel_set_text` / `psel_edit_done`
  (the edits); `psel_spin` (the buddies); `psel_esc`; `psel_named_answer` (the 842 list: cui_planes.c pls_which 4).
- Surfaces: eval_surf order 2 = mcsn_eval, surf_curv_geom = mcsb_curv_geom, calc_surf_unit_norm = mcsn_unit_norm - reached by the
  test step `pselent:E,U,V` only, until the display-list pick is ported.
- Arcs: Ent.avw (mcam_load.c) = the record's view number, so a +-Z arc in another view (Bottom: view 4) keeps its normal.
- draw_plane: psel_overlay (the core overlay; its line width is the overlay's 2.5 px, not GrTemp's 2).
- post_warning: cui_message("Mastercam", text); DisplayErrorTip: a tool tip at the edit.

### Call interface (for the Extend / Trim to Plane / Curve Slice branches)
```c
static PlaneSf my_plane;                         /* keep it in the command's state: it is the plane on entry next time */
static void my_plane_done(int ok, PlaneSf *pl, void *ctx) { if (ok) { /* use pl->o, pl->n (unit), pl->x (unit) */ } /* re-enable the panel */ }
...
psel_begin(NULL /* "Select Plane" */ or "Select plane to extend to" /* mccadres 7457 for Extend */, &my_plane, my_plane_done, NULL, 0);
```
While `psel_open` the caller's panel is disabled and its own picks wait (Mastercam's caller is blocked inside get_plane_info); the
front end routes clicks / Esc to the dialog first (mainwindow.cpp viewClickIn, the key filter). Trim to Plane then uses
`n = unit(normal)`, `d = origin . n`, keep point = origin + n (formats/surfaces_trim_decode.md); Curve Slice replaces its point and
normal (cui_slice.c code 4); Extend passes origin and normal to extend_surf.
Callers wired (int-s1, 2026-11-17): Extend's To plane (cui_srftrim.c srf_ext_exec / ext_pl_done, cui_geo.c geo_sext_plane_done;
the Qt function panel disabled while psel_open, mainwindow.cpp syncPlaneSel). Not yet: Trim to Plane (its command is not ported), Curve
Slice (cui_slice.c code 4 still uses the active Cplane), Draft's Plane mode.

## 6. Tests
misc suite scenes `psel_*` (tools/tests/misc_scenes.txt, 31): the dialog through the core steps (`psel`, `pselp`, `pselnw`, `pselctl:ID`,
`pseltext:I=T` / `pseledit:I=T`, `pselspin:I=D`, `pselpt:X,Y,Z`, `pselent:E[,T[,V]]`, `pselface:SOLID,FACE`, `pselesc`, `pseldump`) and
through the Qt widgets (`pseldlg`, `pseldlgset:btn=ID` / `edit=I:TEXT` / `spin=I:up|dn`, `pselclickw:X,Y,Z` - the view's click path,
`pldlgset:row=I` / `ok` for Named); every scene prints `planesel result ...` (the callback).

## 7. Left (summary, after the second pass)
- Undefined in Mastercam: Cancel / Esc after an invalid plane on entry (an uninitialised copy) - refused: the plane stays, ok 0.
- Refused with a message: a surface picked by a click (the display-list (u, v), FUN_18013cbc0, not ported - see 4.5); solid edges
  (event 0x4000); solid faces of a solid made in this session; selection cues other than X / Y / Z typed into an edit; expressions in
  the edits (UIEdit::Evaluate's evaluator object, not decoded) - InvalidRealField as for any text Validate refuses; the edit's '?' menu.
- NOT DECODED: FUN_18031a100 (the pane MCDialog::OnInitDialog places a first-time dialog at; the port: the right-hand edge, UI-1).
- Not ported: cross hatch (no cross hatching here), the PlaneEventData copy and cmd != 0 (no caller passes them), Help, the
  MessageBeep of a non-planar solid face; GrTemp's line width 2 (the overlay's 2.5 px); GetProdEnableBits taken as 0.
