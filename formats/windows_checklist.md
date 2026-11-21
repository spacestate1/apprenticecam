# Checks to run in real Mastercam 2017 on Windows (N11-3, 2026-11-11)

Three things only real Mastercam can tell us. Send back the answers (and the exact text of any message) per item.

Copy the files to the Windows side first, for example:
```
mkdir -p "<windows>/apprenticecam-checks" && cp ~/mastercam-re/work/occt/proof/out/cube_*.mcx-9 <windows>/apprenticecam-checks/
```

## 1. Solid proof files (unblocks: saving new solids into .mcam, TODO "Solids tree" item 3; occt_xt.md 6)
Open each file in Mastercam 2017 in this order. File > Open, do not merge.

| # | File | What to check | Answer |
|---|---|---|---|
| 1a | `cube_A_regenxt.mcx-9` | Opens with no message? Solid looks the same as before? Solids manager shows Extrude + Extrude Cut? | |
| 1b | | Right-click the solid > Regen: works with no error? | |
| 1c | | Analyze > Dynamic on the solid, or a 2D Contour chained on a solid edge: does it pick? | |
| 2a | `cube_B_newsolid.mcx-9` | Two solids in the Solids manager, the new one with "Extrude" under it? New one = 10 x 10 x 3 block, Z -3..0? | |
| 2b | | Edit the new Extrude, distance 3 -> 4: does it regenerate? Then Regen All: any error? | |
| 2c | | File > Save, close, reopen: is the new solid still there? | |
| 3 | `cube_C_newsolid_dirty.mcx-9` | Same as 2a. Any message on open such as "Error reapplying operation"? | |
| 4 | `cube_D_newsolid_records.mcx-9` | Same as 2a. Select a face of the new solid, then Regen: any crash? | |
| 5 | any | Any other message on open (size mismatch, version, PK error): copy its exact text. | |

## 2. D3: the X on a dirty operation (opdlg_port_audit.md 19.6)
1. Open any part with a toolpath, for example BRACKET4.
2. Make an op dirty: open its Parameters, change the feed rate, OK. Do not regenerate.
3. Look at its icon in the Toolpaths manager in each of these states:

| State | Red X on the op icon? |
|---|---|
| collapsed, not selected | |
| collapsed, selected | |
| expanded (click the + so Parameters / Tool / Geometry show), not selected | |
| expanded, selected | |

(The decompile says the X disappears when the op is **expanded**, not when it is selected. Our app does it on
selection. This table decides which is right.)

## 3. 3D roughing test parts (oracle for the 3D toolpaths; hst3d_port_plan.md 5, decision 3D-1)
There is no stored 3D High Speed op to compare against. Please make one small part with these ops and save it
with every op regenerated, so the toolpaths (NCI) are stored in the part:

- Stock about 100 x 80 x 40 mm. A solid with a raised boss, a pocket with a floor, and a dome or fillet
  (any curved surface).
- Ops on that solid, each with defaults except the tool:
  - Dynamic OptiRough with a 10 mm flat endmill
  - Area Roughing with a 10 mm flat endmill
  - Waterline with a 6 mm ball
  - Raster with a 6 mm ball
  - Scallop with a 6 mm ball
  - Pencil with a 6 mm ball
  - One of the ops above again with a 6 mm bull nose (R1)
- Regenerate all, save as `rough3d_test.mcam`, and copy it back (for example to `<windows>/apprenticecam-checks/`).
- If there is time, an inch copy of the same part helps too.

## 4. Saved 3D operations with surface / solid references (S6, formats/surface_selection.md 6 "S6")
apprenticeCAM now writes a 3D op's drive / check references (TP kind 4 records) and, for a solid picked here as a whole body, a new
"Toolpath" op (SldToolPOp) in the solid's history. Files (copies of corpus parts saved by `cam_cli PART saveref all OUT`, in
`~/mastercam-re/work/s6/`): `waterline_gen.mcam` (WATERLINE.mcam, 182 drive surfaces written by the new writer), `swarf_metric_gen.mcam`
(TUTORIAL 2 SWARF_METRIC: drive + 43 check surfaces + containment chain), `horizontal_newsolid.mcam` (HORIZONTAL.mcam: the solid picked
again as a whole body, a new Toolpath op appended to sld_0's history). Each op opens dirty (red X).

| # | File | What to check | Answer |
|---|---|---|---|
| 4a | each | Opens with no message? The edited ops are in the Toolpaths manager (red X)? | |
| 4b | each | Op > Toolpath geometry (Toolpath/surface selection dialog): Drive count (and Check / Containment) as in the original part? Show... highlights the same surfaces / solid? | |
| 4c | `swarf_metric_gen.mcam` | The 43 check surfaces are under Check, not Drive? | |
| 4d | `horizontal_newsolid.mcam` | Solids manager: sld_0's history ends with a second "Toolpath" op? Right-click the solid > Regen: any message? | |
| 4e | each | Regenerate the edited ops: toolpath as the original op's (no "no drive surfaces" message 241)? | |
| 4f | each | File > Save, close, reopen: the ops still have their geometry? | |

## 5. STL mesh entity written by apprenticeCAM (formats/cad_import.md 7)
apprenticeCAM writes a PMESH_ID record (class 0x100000) + its "MASTERCAM STL DATA" block for an STL file opened as a mesh. Make one:
open `~/mastercam-re/files/g68.2_cube.stl` (or any STL) in apprenticeCAM, Save as Mastercam part -> `stl_mesh_gen.mcam`; also open the same
STL in Mastercam (File > Open, STL, Options: Import as STL Mesh) and save it as `stl_mesh_ref.mcam`.

| # | File | What to check | Answer |
|---|---|---|---|
| 5a | `stl_mesh_gen.mcam` | Opens with no message? The mesh shows (shaded / wireframe), its level / colour as in apprenticeCAM? | |
| 5b | `stl_mesh_ref.mcam` | Send the file back: its PMESH_ID record and data block are the read-compare oracle (none in the corpus) | |
| 5c | `stl_mesh_gen.mcam` | Analyze the mesh / delete it / Save: any message? | |


## 6. Drill per-point options (oracle for formats/nci_writer.md 10 "2026-11-13"; none of the 50 corpus drill ops uses them)
apprenticeCAM now writes the drill NCI for points with their own initial height, canned text and manual entries, and for bolt circle /
grid points, ported from bnci_drill but never compared with Mastercam. Please make one small part (any plate with 4 holes in a row):

- One Drill op (Simple drill, G81) on the 4 points, defaults otherwise. Through Mastercam's per-point drill editing (the drill point list / point manager):
  - point 2: its own Initial height (e.g. 20 mm above the default);
  - point 3: a Canned text entry and a Manual entry text (two lines, e.g. `M00` and `(CHECK)`);
- A second Drill op whose points come from a Bolt Circle (the drill point selection's bolt circle pattern, 4 holes).
- A third Drill op with "Use clearance only at the start and end of operation" on, plus one point with its own initial height.
- Regenerate all, save as `drill_points_test.mcam`, copy it back (for example to `<windows>/apprenticecam-checks/`).

## 7. New Lathe Face from the stock (L-6, formats/lathe.md 7; oracle for tools/tests/lathe_face_test.sh section 4)
apprenticeCAM now makes a new Face "from stock" from the LATHE_INCH defaults, but only checked against its own decoded rules. Please:

- Open `Lathe.mcam` (shared Mcam2017 / common/reports/SST/Samples/Lathe), do not change the stock / machine group. Insert arrow after op 2.
- Lathe toolpaths > Face. Tool page: note which tool is selected by default (number / name), keep it. Face page: "Use stock" on,
  do NOT touch Finish Z - write down the Finish Z value the page shows. OK. (op A)
- Again Face: Use stock, Finish Z = -0.25, Rough stepover on (default 0.1), finish as default. OK. (op B)
- Again Face with the same tool on a bottom-turret / left-hand tool if the tool list has one (otherwise skip). (op C)
- Save as `lathe_face_new.mcam`, copy it back (for example to `<windows>/apprenticecam-checks/`), plus the posted NC of ops A / B if easy.

| # | What | Answer |
|---|---|---|
| 7a | Finish Z shown for op A before OK (default of a new op) | |
| 7b | Default tool of the new Face (number, name) | |
| 7c | Any message on OK / regenerate | |

## 8. Extend Trimmed Edges default Style (decision S-12, formats/surfaces_trim_decode.md 3)
The panel's default Style is an uninitialised byte in the decompile (FUN_180305960). Please:

- Start Mastercam fresh (no Extend Trimmed Edges used in this session), open any part with a trimmed surface.
- Surfaces > Extend > Extend Trimmed Edges: before picking anything, note which Style radio is on.
- Repeat once after restarting Mastercam (to see if it is stable).

| # | What | Answer |
|---|---|---|
| 8a | Default Style on the first start (Mitered / Rounded) | |
| 8b | Default Style after a restart | |

## 9. A part with a kind 4 surface (CS-6, formats/chaining.md "Decisions CS-5 .. CS-8"; unblocks eval_surf / eval_surf_const of kind 4)
No surface record of kind 4 exists in the corpus, so its evaluation (eval_surf_const FUN_18042c080 / FUN_18042d9b0, eval_surf) is not
ported and such a surface is refused (no display-list points; hover / click / surface edge chaining refused with a message) until a part
holds one (user decision CS-6). Kind 4 is a curve-based loft (formats/mcam.md: "kind 4 loft"; evalsurf FUN_18042cad0). In Mastercam
2017 please make a Lofted surface and a Ruled surface through 3 or more curves (once with the Surface type / Create NURBS option off if the
panel has one), and to be sure a surface of each other kind you can reach from the Surfaces tab (Revolved, Swept, Net, Fence, Draft, Extruded, Offset, Fillet; also Surfaces > Flat Boundary and a surface from a solid face),
leave each untrimmed, save them all in one part `srf_kinds.mcam` and copy it back (for example to `<windows>/apprenticecam-checks/`).
Whichever command makes kind 4 (the record's class word) then gives the sample; apprenticeCAM can tell which one it was.

| # | What | Answer |
|---|---|---|
| 9a | The commands used (in the order made) | |
| 9b | Any surface Mastercam made trimmed although untrimmed was asked | |

## 10. Solids written by apprenticeCAM (SOL-1..SOL-5, branch of the solid save path; formats/solid_history.md 3.2.1)
apprenticeCAM now writes new solids as Parasolid XT + their Extrude history. Nothing it writes has been opened in Mastercam yet.
Make the two test parts with `tools/tests/solid_compare.sh` (scenes `sv_new_mcam`, `sv_x9_add`; they are written to the run's output
folder), copy them to the Windows disk, then in Mastercam 2017 (and X9 for the .mcx-9):

| # | What | Answer |
|---|---|---|
| 10a | `sv_new_mcam.mcam` opens without a message; the solid shows and is listed in the Solids manager | |
| 10b | Solids manager: right-click the solid > Regen - any error / change? | |
| 10c | Edit the Extrude op (distance), OK - the solid regenerates correctly? | |
| 10d | `sv_x9_add.mcx-9` in X9: the added solid opens, regens and edits like 10a-c | |
| 10e | Any message about the part file (version / corrupt / repaired) | |

## 11. Engrave reference ops (EStamp7 engine port; formats/engrave_engine.md 3.7, 6, 10)
The corpus has two Engrave ops with a stored toolpath (the X9 engraved coaster ops 2 / 4), so most engine branches are decoded
but not yet compared with Mastercam. What proves the port is the **engine's own input and output** of real runs: engrave.dll
writes `ENGCONT.LG1` (contours) and `EST_DAT.TXT` (parameters) into
`C:\Users\Public\Documents\shared Mcam2017\engrave\`, runs `estamp7.exe`, and reads `ENGTOOLP.LG1` back; `EST_DAT.TXT` also gets
the `FEHLER=` line. All three files are overwritten by the next Engrave regen, so **after regenerating each op below, copy those
three files at once** into a folder named after the case (e.g. `<windows>/apprenticecam-checks/engrave/E1a/`), then save the
part (with the op's toolpath) as `engrave_ref.mcam` at the end. Note the exact text of any message.

Geometry: `python3 tools/engrave_ref_geom.py <windows>/apprenticecam-checks/engrave` writes the DXF files (mm, closed
polylines); open each in Mastercam 2017 (File > Open, DXF), chain all its contours, and make the op. Tool for all cases unless
said otherwise: an Engrave tool (or Chamfer Mill) 90 deg included angle (45 deg per side), tip diameter 0.2 mm; depth 0.5 mm,
everything else at the defaults of the Engrave op.

| # | Geometry / setting | What it checks | Answer (folder name / message) |
|---|---|---|---|
| 11a | `e1_corners.dxf`, corners **Sharp**, "on top" (Spiegel) off, roughing off | precompute corner arcs (KAbrundungswinkel 20), concave tags | |
| 11b | same, corners **None** | KAbrundungswinkel 150 | |
| 11c | same as 11a with "on top" **on** and the 3D sharpening (calc type 2D+3D) | Spiegel sharpening (FUN_00517594) on known corners | |
| 11d | same as 11c with only the 3D moves (calc type 3D, if the tool page offers it) | 'A' filter | |
| 11e | `e1_corners.dxf`, roughing **zigzag**, angle 0, overlap 20 % | hatch rows, links, the `e` value (sec. 6) | |
| 11f | same, roughing angle **30** | rotation path of the hatch | |
| 11g | same, roughing **one way** | 'Y' | |
| 11h | same, roughing **Radius** and then **Diameter** (two runs, two folders) | 'C' / 'D' (still NOT DECODED, refused here) | |
| 11i | same as 11e with "rough first" toggled and "smooth contours" / re-sort toggled | KAbraeumZuerst, Knachsortieren | |
| 11j | same as 11e with climb / conventional toggled | KGegenlauffraesen (= the offset side flag) | |
| 11k | `e2_overlap.dxf` (figure-eight + two overlapping squares) | the overlap check (FEHLER 53, message "Some of the base contours are overlapping") | |
| 11l | `e4a_32000.dxf` (one 32 000-point contour), roughing off | point reduction FUN_004fab4c with a long contour | |
| 11m | `e4b_36000.dxf` (36 000 points) | decode says: the contour comes out empty (16-bit counter, sec. 3.7.1) - does the op fail or produce nothing? | |
| 11n | `e4c_52000.dxf` (52 000 points) | the split path (> 0xbffc, NOT DECODED) | |
| 11o | `e6_comb3200.dxf` and `e6_comb3203.dxf` (comb of 3200 / 3203 teeth, two runs) | corner-arc overflow: the reduction retry (3200) and possibly the i16 wrap (3203; may give garbage or a crash - say which) | |
| 11p | Mastercam text "ENGRAVE", Arial, 20 mm high, roughing zigzag, depth cuts 3 with and without the ramp | depth cuts, ramp FUN_004f9824 (NOT DECODED) | |
| 11q | any of the above with the corner tool / "clean corners" option of the tool page, if there is one (EST_DAT shows `KorrekturVerf=Ecke`) | corner tool 'E' and variable mode (NOT DECODED) | |
| 11r | the coaster: open `engraved coaster.mcx-9`, regenerate op 2 and op 4 (one at a time) and copy the three files after each | the exact engine input behind the two corpus ops (settles engrave_engine.md 10) | |

For 11l-11o also note the size of `ENGCONT.LG1` (13 bytes per point). If a regen shows the depth warning (dialog 1808), answer it
the same way each time and say which.

## 12. Engrave reference ops (engrave.dll side) (formats/engrave.md 2.4 / 3 / 3.4 / 5a; the corpus has 2 Engrave ops with NCI, none with these options)
In Mastercam 2017 (Mill, mm and inch where noted), on simple closed text-like geometry (e.g. Create > Letters "AB" 20 mm high, plus a
rectangle around it), make each op below as its own Engrave op (Toolpaths > 2D > Engrave), regenerate, and save all in
`engrave_ref.mcam` (and the NC of each with the default post). Copy back to `<windows>/apprenticecam-checks/`.
Tools: a CHAMFER MILL 10/90DEG (type 12), an Engrave Tool (type 21, 30 deg, tip 0.2), a flat end mill 3 mm.

| # | Op | Settings (others default) | What to note | Answer |
|---|---|---|---|---|
| 12a | depth cuts | 1804: 3 cuts equal; then a copy with constant volume | NC saved | |
| 12b | entry ramp | Rough on, 1805 plunge angle 30 | NC saved; any message | |
| 12c | 1805 DDV | type 5 in the plunge angle, OK | the message text (exactly) | |
| 12d | filter | 1806: tolerance 0.0003, arcs off; then arcs on, min 0.5 max 100 | NC saved | |
| 12e | XY stock | chamfer mill, at depth, XY stock 0.2; and -0.2 | NC saved; the -0.2 message if any | |
| 12f | flat cutter | end mill 3 mm, Angle for flat cutter offset 20, on top | NC saved | |
| 12g | on top, flat | end mill 3 mm, flat angle 0, on top | NC saved; any 3D (z-varying) finish moves | |
| 12h | depth warning | chamfer mill, depth -8 (max is 4.75): answer each of the three choices in three copies; and close it with Esc in a fourth | the dialog's text (value), NC / message per answer | |
| 12i | depth warning twice | after 11h's "use the maximum", set the depth to -2 and regenerate | the NC depth (-2 or -4.75?) (E-DW2) | |
| 12j | remachining, previous op | an 11a-like op with a 3 mm end mill first, then Engrave with the chamfer mill, Remachining on, Previous operation, Finish after remachining on; a copy with it off | NC saved; message if any | |
| 12k | remachining, roughing tool | Remachining on, Roughing tool: major 6, angle 45, tip 1 | NC saved | |
| 12l | wrap between 2 curves | 1149 Wrapping on, Between 2 curves, grid 0.5; pick the geometry the Geo manager asks (two curves, ref point) | the prompts in order; NC saved (4 / 5 axis?) | |
| 12m | wrap on surfaces | On surfaces, grid 0.5, a cylinder surface | prompts; NC saved | |
| 12n | hidden controls | Roughing/Finishing page: are "Do 3D Engraving moves", "Try to keep tool down", "Start at corners" visible? Engraving parameters: "rapid retract"? | yes / no each | |
| 12o | tool type 17 | if the tool library has a type 17 tool: an Engrave op with it | NC saved | |
For every op also keep the `.mcam` (the op records hold the decoded fields; the round trip is checked with tools/tests/engrave_rt.sh).

## 13. A part with a stored Dynamic Contour toolpath (NS-6, formats/nci_save.md 7.3; sections 11 / 12 are taken by the engrave branches)
None of the user's parts or the corpus has a 2D High Speed op of style Dynamic Contour (record 0x1228 = 2) with stored NCI, so the
true-arc engine input of Dynamic Contour (cam2d_dyn.c gen_dyncontour_mcce) is checked only against mcce.dll on our own inputs, and its
NCI gate stays closed (user decision NS-6). Wanted, in Mastercam 2017:
- a part with two or three closed chains of lines and arcs (one with a full circle), a Dynamic Contour op (Toolpaths > 2D High Speed >
  Dynamic Contour) with wall stock 0.2, and a second one with wall stock 0 and depth cuts (2 cuts);
- regenerate both, save as .mcam, and copy the file over. Then: tools/tests/nci_pass_oracle.py and tools/oracle.py --type dynamic on it
  decide whether the Dynamic Contour NCI gate can be lifted (the user's rule: every section that regenerates move for move places its
  section-start blocks as Mastercam's).

## 14. A second Dynamic Mill op whose engine switches regions inside one call (NS-8, formats/nci_save.md 7.3)
NAKTO-BRAKE-BRACKET op 2 is the only stored example: where one region is split in two by an island the tool cannot pass, Mastercam
retracts to the clearance and writes a second section-start block, ours keeps the feed-plane link. Wanted, in Mastercam 2017: a pocket
chain with an island that leaves gaps narrower than the tool on both sides (e.g. a 40 x 20 rectangle, a circle island leaving 1 mm gaps,
a 6 mm tool), a Dynamic Mill op on it, regenerate, save as .mcam, copy over. Then: compare the NCI at the region switch (G0 height,
1002 block) with ours.

