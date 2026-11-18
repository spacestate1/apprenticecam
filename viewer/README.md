# apprenticeCAM programs: the workbench (Qt app), cam_cli, tool_cli, mcamview

> **2026-10-22:** the workbench is the Qt app `viewer-qt/build/apprenticecam` (built with CMake, see ../README.md). The
> nuklear programs `mcamui` and `mcamgl` were removed (formats/qt_port_plan.md phase 9; their sources are in
> ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz). The workbench sections below were written for mcamui; the
> Qt app has the same layout, commands, project files and test hooks (`--shot out.png`, `QT_QPA_PLATFORM=offscreen`,
> `MCAMUI_SCRIPT`, `MCAMUI_EVENTS`; tools/tests/README.md), so they still describe it apart from the build lines.

> **Status 2026-10-16.** Start with `../README.md` (getting started, licence and its use-at-your-own-risk warning) and
> `../NOTES.md` "Current state". This guide grew with the program; sections marked *(historical)* describe earlier
> versions. Since 2026-10-16 every dialog, panel and message follows Mastercam's own decoded resources, commands not
> ported are drawn greyed and do nothing, and toolpath engines that are not decoded are not generated (stored ops of
> those types stay as Mastercam made them). Spec index: `../formats/README.md`; program structure, our file formats
> and test hooks: `../formats/program_structure.md`.

## mcamview

A small C/SDL2 viewer for Mastercam 2017 / X9 part files (`.mcam`, `.mcx-9`). It shows the wireframe geometry and
the toolpaths stored in the file, and can play the toolpaths back. Everything it reads was decoded from the Mastercam
DLLs (see `../formats/mcam.md`). No Mastercam code or libraries are used.

```
make
./mcamview PART.mcx-9                                   # interactive window
./mcamview PART.mcx-9 --shot out.bmp --iso --op 14      # render one frame to a BMP (no window)
```

| input | action |
|---|---|
| left drag / right or middle drag / wheel | rotate / pan / zoom (about the cursor) |
| `T` `I` `F` | top / isometric / front view |
| `Z` | fit to extents |
| `G` `P` `R` | toggle geometry / toolpaths / rapids |
| `[` `]` | previous / next operation (0 = all ops) |
| `SPACE`, `BACKSPACE`, `+` `-` | play/pause toolpath simulation (tool shown as a circle of its real diameter), restart, speed |
| `Q` / `ESC` | quit |

Colours: geometry uses Mastercam's colour index. Toolpaths are coloured per operation and rapids are yellow. The
window title shows the selected op and entity counts.

What it reads:
- entity stream: points, lines and arcs (arcs outside the XY plane are drawn through start/end around the centre);
- TL_ID tool records (number → diameter);
- the binary NCI tail: rapids, feeds, arcs in any of the three planes (helices included), drill points, tool changes.

Not shown: splines/NURBS/surfaces (layouts decoded but untested), Parasolid solids (stored as XT; would need a
Parasolid reader), and stock.

## mcamgl (OpenGL 3.3 + Nuklear GUI) *(removed 2026-10-22)*
The side panel has:
- file open (path box or drag-and-drop) and stats;
- view buttons (Top/Front/Right/Iso/Fit) and toggles (geometry, toolpaths, rapids, grid, tool, ortho);
- a **Solid** mode button: off / opaque / ghost (translucent, so toolpaths inside the stock stay visible);
- simulation (play, restart, scrub slider, speed);
- the operation list in Operations Manager order (click one to show only that op);
- the tool list.

Mouse: left drag rotate, right/middle drag pan, wheel zoom. `SPACE` play/pause, `ESC` quit.

Solids are the real Parasolid B-rep stored in the file, meshed by `xt_mesh.c` (see `../formats/xt.md`). It needs a
Parasolid schema file from the Mastercam install (`sch_26105.s_t`). It looks in `$MCAM_SCHEMA_DIR`,
`~/mastercam-re/files`, the Windows disk's Mastercam `PSLDDATA` folders and `./schema`.

`xt2obj PART.mcx-9 SCHEMA.s_t out.obj` exports the meshed solids as OBJ.

## The workbench (Mastercam-style; mcamui until 2026-10-22, now the Qt app)
```
cmake -S ../viewer-qt -B ../viewer-qt/build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build ../viewer-qt/build -j4
../viewer-qt/build/apprenticecam PART.mcx-9
```
The window follows Mastercam 2017's layout:
- title bar with quick-access buttons;
- File backstage (Open lists the test parts);
- the 10 main tabs plus the machine-context tab (Mill → Toolpaths, Lathe → Turning/Milling, …);
- ribbon groups with large/small buttons, dropdowns and inline galleries (expand arrow → full grid);
- the manager dock (Toolpaths with its two toolbars and the Machine Group / Toolpath Group / ops tree, Solids, Planes,
  Levels, Recent Functions);
- the 3D view in a light theme and a status bar with live X/Y.

**No original artwork ships.** Names, layout and command bindings come from Mastercam's ribbon definition
(`BCGP_RIBBON_XML`) and manager toolbars via `ui/layout.txt`, `ui/cmd_labels.tsv`, `ui/icon_map.tsv` (product name
filtered out by tools/debrand.py). Icons are read at run time from `<config dir>/icons.pack`, which you build from your
own Mastercam copy with `tools/setup_from_install.py`; without it every icon is a generated letter tile. The same setup
writes `<config dir>/gallery_tips.tsv`: the toolpath palette's tooltip descriptions (Mastercam texts, so not shipped;
tools/gen_gallery_tips.py).

Commands this program implements:
- views (Top/Front/Right/Isometric/Fit), Shaded/Wireframe/Translucency, grid (F9);
- manager toggles, machine type (Mill/Lathe/Router/Wire/Mill-Turn);
- Select all operations, Backplot, Verify, Toggle display, Hide rapids, Open, Exit.

Commands this program has not ported are drawn greyed and do nothing (2026-10-16). Mouse (Mastercam's): left
selects, middle drag rotates, Shift + middle drag pans, wheel zooms about the cursor (config 33009 / 33010 swap /
reverse them).

### 2D toolpaths in mcamui (selection, chaining, contour / pocket / drill, posting) *(historical, 2026-10-14)*
Since then: chaining uses Mastercam's Chaining dialog (template 1058) with its modes and direction arrows, a second
pick of a chained entity adds the chain again (as Mastercam), drill points are picked in the Drill Point Selection
dialog (mcmillres 2300), and the parameters dialog is Mastercam's ToolpathDlg page tree (formats/mcamui_dialogs.md).
1. **Toolpaths** tab → 2D gallery → **Contour**, **Pocket** or **Drill**.
2. Click entities in the view. Contour/Pocket chain the clicked entity through connected endpoints (stopping at
   branches); clicking again removes the chain. Drill picks points and circle centres. Chains are magenta with a green
   start square; drill points are green crosses. Click without a command toggles plain selection (yellow).
3. **OK** in the Chaining window opens the parameters dialog:
   - tool (from the part file, or a custom diameter), spindle, feed, plunge feed;
   - clearance, feed plane, top of stock (defaults to the top of the solid's bounding box), depth, max stepdown;
   - contour: side (closed chains: on / outside climb CW / inside climb CCW; open chains: on / left / right), stock to
     leave, tabs (count, width, height);
   - lead-in/out: quarter-arc radius plus a straight lead, on the free side of the cut;
   - finish passes (count and spacing) at final depth, for contour and pocket walls;
   - pocket: stepover %, ramp angle, corner clean-up on/off;
   - drill: R plane and peck;
   - arc output (G2/G3) on/off.
4. **Generate** adds the operation to the Toolpaths manager and the view (the solid switches to ghost).
5. The manager toolbar button **Post selected operations** writes `<part>-mcamui.nc` next to the part file, in the
   user's post format: G81/G83 cycles; otherwise G1 moves, with G2/G3 (R format) fitted to arcs within 0.003 mm.
6. **Delete op** in the Toolpaths manager (or the Delete key) removes a selected operation created here. Toolpaths
   stored in the part stay; they are read-only.

### Toolpaths
2D ops this program generates (Toolpaths tab → 2D gallery). Each takes chains (or circles) and opens a parameter
dialog with the fields that op uses:

| Gallery item | What it does |
|---|---|
| **Contour** | 2D contour: side, stock, leads, depth steps, finish passes, tabs. Contour type **2D chamfer** (also "Chamfer" in the gallery) cuts a chamfer of a given width with a chamfer mill / spot drill / countersink: the tip goes to `top - w / tan(a) - tip offset`, the centre sits `tip dia / 2 + offset · tan(a)` off the chain. **Engrave** cuts on the line. |
| **Pocket** | Mastercam's pocket engine (port in progress, viewer/cam2d_pock.c): High Speed and finish-only from mcmill; Zigzag from the measured engine; the other cutting methods are refused (NOT DECODED). |
| **Drill** | G81 / G83, tip compensation. |
| **Face** | Zigzag or one way across the chain's (or the stock box's) extent, with stepover, overlap past the edges and a cut angle. |
| **Slot Mill** | Closed slot chains are pocketed with a ramp. On open chains the tool ramps back and forth along the chain to each depth, then cuts its full length. |
| **Helix Bore** | Per circle: half-circle lead from the centre, helix to depth at the given pitch, a flat-bottom lap, helical finish pass(es). |
| **Circle Mill** | Per circle and depth: helix entry on a small circle, then spiral out by the stepover to the wall, finish pass(es). |
| **Thread Mill** | Port of mcmill bnci_threadmill: radial rough / finish / spring passes, Z passes by active teeth, entry / exit arcs, quarter-pitch helical ends; taper and control compensation refused. (The dialog's "Thread depth" / "Full-profile tool" fields are ours and unused.) |
| **Dynamic Mill** | Port of Mastercam's mcce engine (viewer/mcce_dyn.c): constant-engagement clearing, entries per the op, depth cuts. Safety rule: no rapid below the feed plane. |
| **Area Mill** | Area Clearance: decoded pass positions (McHmm); links are straight only inside the tool-centre region, else retract. Core Roughing: not generated (NOT DECODED). |
| **Dynamic Contour** | Not generated (engine partly decoded, unverified); stored ops are kept and posted from their NCI. |
| **Peel Mill** | Not generated (NOT DECODED); stored ops are kept and posted from their NCI. |

Posting fits G2/G3 arcs, including helices (G2/G3 with Z).

**Dynamic engine** *(historical: the first, own engine in `cam2d_dyn.c`, replaced 2026-10-15/16 by the mcce port
`mcce_dyn.c`, formats/mcce_port_plan.md; the measurements below are of the old engine)*. It works on a vector material
model: Clipper polygons, as Mastercam's engine does. There is no raster.
- A = cut area, M = material left, C = allowed tool centres.
- Each round takes the boundary of `offset(A, s - R) ∩ C`, with corners rounded to the min toolpath radius. A disk on
  it reaches exactly one stepover past the cut area. The tool cuts the part of it that still touches material,
  climbing.
- It measures the width of cut on the chosen piece (swept-away area / travel, the engine's own measure). If it is
  over 1.5 × stepover (tight curves near the entry, fronts opening out of narrow necks), the round is redone at half
  the stepover.
- Pieces that start next to where the last one ended (the corners at the walls) are cut in the same round.
- Links stay down at the micro lift and finish with a short descent ramp, as measured on the user's files. A blocked
  link backs up along the piece just cut; it retracts only if there is no clear way.
- Narrow passages that the rounding would close are retried without it.

Checked with `cam_cli ... --verify`, which replays the moves on a raster and reports material left, gouges and the
engagement angle:

| Part / op | Left | Gouge | Engagement p50 / p90 / p99 | Time |
|---|---|---|---|---|
| fight-stick pocket, D5, 25 % | 0.02 % | 0 | 50 / 56 / 66° (60° nominal) | 5.4 s |
| tramming-rod outline + 2 islands, D6, 25 % | 0.05 % | 0 | 52 / 68 / 120° | 0.6 s |
| A001 outline + 6 islands, D6, 20 % | 0.24 % | 0 | 38 / 48 / 70° (53° nominal) | 5.2 s |
| tramming-rod Area Mill (from outside) | 0.00 % | 0 | 50 / 52 / 58° | <1 s |

For comparison, Pocket on the tramming rod runs at a p90 of 148°. Max values of 180° are slotting through gaps
exactly one tool diameter wide; no strategy avoids that.

### Files: New / Open / Save / Save As / Close
A workbench **project** (`.mcamui`, text) holds the part path, the tool list and the operations. Toolpaths are
regenerated when it is opened. Mastercam part files are never written; saving onto a `.mcam` / `.mcx*` path is refused.
- **Open** (Ctrl+O, File → Open, quick-access button, or drop a file on the window) shows a file browser with
  Mastercam's title and file-type filters (path field, Up / Go, file list). It takes `.mcam`, `.mcx-9` and `.mcamui`.
  Opening a part also loads `<part>.mcamui` when it exists. Old (pre-X9) `.MCX` files and non-part files get a clear
  error. A failed open leaves the current document as it was.
- **Save** (Ctrl+S) writes the project (default `<part>.mcamui`). **Save As** (Ctrl+Shift+S) picks any `.mcamui` path and
  asks before replacing. Writes go to a temporary file first; the previous version is kept as `.bak`.
- **New** (Ctrl+N) / **Close** (Ctrl+W) / **Exit** (Ctrl+Q, window close) ask "Save / Don't save / Cancel" when there are
  unsaved changes. The title bar shows `*` when there are. Esc only closes dialogs; it no longer quits.
- Recent files are kept in `~/.config/mcamui/recent.txt` and listed under File → Open.

### Hot keys (Mastercam's)
The keys are Mastercam's own. At startup the program reads your `mastercam.kmp` (`my mcam2017/CONFIG`), or uses the same
51 default bindings, and maps each Mastercam function to an action here. **Alt+H** lists them.

| Group | Keys |
|---|---|
| Views | Alt+1..7 standard views, Alt+F1 fit, F1 zoom window (drag), Alt+F2 unzoom 0.8, F2 unzoom previous, Alt+P previous view, Alt+F12 rotation point |
| Display | Alt+S shading, Ctrl+T translucency, Alt+T toolpaths, Alt+F9 axes, F9 axis lines (OnDisplayInfo), F3 / Shift+Ctrl+R repaint |
| Edit | Ctrl+Z / Ctrl+Y undo / redo (geometry and operations), Ctrl+X / C / V, Ctrl+A select all, F5 delete, Alt+E hide / unhide, Alt+X main attributes from an entity, F4 analyze |
| Managers / settings | Alt+O Toolpaths, Alt+I Solids, Alt+L Planes, Alt+Z Levels, Alt+G grid, Alt+F8 configuration (grid settings) |
| Files | Ctrl+N / O / S, Shift+Ctrl+S, Ctrl+P print (saves a picture of the window), Alt+F4 exit, Alt+V about |

Mastercam features that don't exist here (Alt+D drafting globals, Alt+C add-ins, Alt+A auto save, Alt+M
multi-threading, Alt+B Art) do nothing.

Graphics-window keys (Mastercam's FUN_1803222f0): arrows pan, Alt + arrows rotate, Page Up / Down zoom, Home fits (one
step per key press, key repeat repeats it; Mastercam's pan / rotate / zoom factors). Also: Delete deletes, Esc
cancels, Ctrl+W closes. Help (Alt+H) lists the keymap with the commands' own labels.

View changes (Alt+1..7, the view buttons, Fit, Home, previous view) rotate / zoom the camera smoothly to the new view in
about 0.35 s, as Mastercam does, instead of jumping; dragging or zooming during the move takes over. `--shot` runs jump.

F1 is zoom window and F9 toggles the axis lines (Mastercam's OnDisplayInfo = "Show Axes"; there is no info text). Grid
settings: Alt+F8 opens Mastercam's "Configuration - Screen Grid" page.

### New parts and drawing geometry
**File > New** (Ctrl+N) starts an empty part the way Mastercam's File > New does: Top view, level 1, construction depth
0, Mill. Draw geometry with the **Wireframe** tab. The commands follow Mastercam's MCCAD / MCCADUTIL functions; modes and
defaults are in `../formats/wireframe.md`.
- **Points:** Position, Dynamic (on an entity), Segment (n points incl. ends), Endpoints, Small Arcs (centres of arcs up to
  a radius), Bolt Circle (holes / points / centre point).
- **Lines:** Endpoints (length / angle, horizontal / vertical, multi-line), Parallel (through a point or at a distance),
  Perpendicular, Closest, Bisect, Tangent Through Point.
- **Arcs:** Circle Center Point, Arc 3 Points, Arc Tangent (two entities, or entity + point; click near the solution you
  want), Circle Edge Point (2 / 3 points), Arc Endpoints (third point, or radius + pick), Arc Polar, Arc Polar Endpoints.
- **Shapes:** Rectangle (corner or centre anchor, typed size), Rectangular Shapes (rectangle with corner radius, obround,
  single D, double D; rotation), Polygon (corner / flat radius, rotation, corner fillets), Ellipse, Helix, Spiral (made
  of lines within 0.01; Mastercam makes splines), Bounding Box (2D or 3D box, expansion).
- **Modify:** Trim Break Extend (trim 1 / 2 / 3 entities, divide, to point, length), Break Two Pieces, Join Entities, Break at
  Intersection, Break Many, Trim Many, Break at Points, Fillet Entities / Chains, Chamfer Entities (1 / 2 distances,
  distance + angle) / Chains, Offset entity (distance or through point, copies, move), Offset Chains (sharp or round
  corners), Project (to a Z depth), Close Arc, Break Circles.
- Each command lists what to pick next (a point, an entity, or a chain), runs when the picks are complete, and then
  waits for the next set. Commands on the selection have an **Apply** button (Trim Many: select, **Next**, then the
  trimming entity and the side to keep). Esc or Done ends it. The command's function panel (length, angle, radius,
  distances, …) docks as a tab of the left managers' dock bar while the command runs - inserted between "Levels" and
  "Recent Functions" and made active; clicking a manager tab is possible mid-command, and the command's tab goes when
  the command ends (the tabbed dock = the Mastercam.Workspace layout).
- Points come from clicks (grid snap, and AutoCursor snapping to endpoints / midpoints / centres / quadrants /
  points / intersections / the tangent / perpendicular / nearest point within 8 px, horizontal-vertical and angular
  about the previous point; N captures a temporary midpoint of the entity under the cursor) or typed
  X / Y. Every run is one undo step; trims replace entities, and Undo brings the originals back.
- Not here: splines, letters, curves from surfaces / solid edges (they need surfaces or solid edges).
- Split buttons in the ribbon: click the top to run the command, the arrow to open its list.

Then chain it and add toolpaths as usual. The project file (`.mcamui`) stores the drawn geometry. For an existing part
it stores only the geometry added or deleted here.

### Solids: Extrude
**Solids > Extrude** (Mastercam's `sld_newExtrude`): pick closed chains (nested chains become holes) or open chains
(thin wall only), then choose:
- **Create body**, **Cut body** or **Add boss** (Cut / Boss work on a body made here);
- distance, one direction / both directions / symmetric, Reverse. A cut goes into the target body and a boss grows away
  from it; Reverse flips that;
- draft angle (inward / outward), and thin wall (inward / outward / both).

The preview updates as you change the chains or values. OK makes it, and Apply makes it and stays open.
- Bodies are stacks of Z slabs of 2D regions, so Create, Cut and Boss are exact for straight walls. A drafted wall is
  approximated by 24 steps.
- Bodies are shaded with the part and are listed in the Solids manager (Mastercam's history tree: solid rows with
  their operation rows; right-click Delete on the last feature, Regen All Solids, Expand / Collapse all).
- They are rebuilt from the feature list, which the project stores as `EXTRUDE` + `XCHAIN` lines, so Undo and reopening
  replay the same steps.
- They are not written into exported `.mcam` files (no Parasolid writer).

### Saving as a Mastercam part
File → Save As → **Save as Mastercam part (.mcam)...** writes a **new** `.mcam`: a copy of the open part with the
operations made here added as real Mastercam operations. They open dirty (red X): select them and Regenerate in
Mastercam. The original part is never written, and existing files are never overwritten.
- Each new operation is a copy of Mastercam's default operation of that type from the defaults library named by your
  configuration (config 2303: `Mill_MM` / `Mill_Inch.mcam-defaults`); your other parts are not read. Then our chains,
  tool, heights, depth cuts, stock, feeds, leads, tabs, chamfer, stepover and drill cycle are filled in, with the
  tool's full op_tool_info block (and a TL_ID record for a tool the part lacks). See `../formats/mcam_write.md` 8-10.
- Exported types: Contour (incl. chamfer / engrave), Pocket, Slot, Facing, Drill, Helix Bore, Circle Mill, Thread Mill, and
  Dynamic Mill / Area / Dynamic Contour / Peel as 2D High Speed of the matching style.
- Settings this program doesn't have come from that default operation (edited Mastercam ops keep their own record).
- The extension follows the part's version: `.mcx-9` for X9 parts, `.mcam` for 2017 parts and new parts.
- New parts: the file is the defaults library emptied (geometry, solids, operations, tools, toolpaths removed; its
  machine group is your configured machine). The geometry drawn here and the operations are then added.
- Tools edited here are written back into the part's tool archive (TlMgr), its TL_ID records and the ops' tool blocks.
- Exported files have not yet been opened in Mastercam: treat them as unverified.
- `cam_cli PART OP CHAINS ... --mcam OUT.mcam` does the same from the command line. Test files: `../test-exports/`.

### Tools and tool libraries
- Part tools are read from the part's tool records with the full field set (type, corner radius, tip angle, pitch, D/H
  offsets, rpm, feed, plunge, retract). See `../formats/tooldb.md`.
- **Tool Manager** (Toolpaths ribbon, manager button, or "Tools..." in the operation dialog):
  - part tools: New, Edit, Delete (refused while an operation uses the tool), Add to library;
  - a library: picked from the libraries found (Mastercam `.tooldb` on the Windows disk, `./tools`,
    `~/.config/mcamui`, `$MCAM_TOOLLIB_DIR`) or Browse;
  - filter by type, diameter range and name;
  - Copy to part (renumbers on a clash), Edit copy, Remove; Save library writes our `.tools` format.
- **Tool editor**: name, number, type, diameter, corner radius, tip angle / angle per side, pitch, flutes, lengths,
  shank diameter, D/H offsets, rpm / feed / plunge / retract, a calculator (cutting speed + chip load → rpm and feed),
  coolant, peck and dwell, holder (from an open holder library such as `BT40-mm.tooldb`), and a side-view preview.
  Editing a tool regenerates the operations that use it.
- Choosing a tool for an operation applies its rpm, feeds, coolant and (drill) peck. The dialog defaults to a drill for
  Drill and to a mill for Contour/Pocket. Drill has an optional tip compensation (depth to the full diameter).
- The post writes the tool's corner radius in the tool comment, and M8/M9 for coolant. Their post's `G43 H0` is kept.
- The backplot draws the real cutter outline (flat / ball / bull / pointed) plus its holder.
- `tool_cli LIB.tooldb|.tools [--save OUT.tools] [--holders]`, `tool_cli find`, `tool_cli PART --part`.
  `cam_cli` takes `--tool N`, `--lib LIB --libtool NAME`, `--tipcomp`, `--coolant`.

(The earlier "Show stock box" toggle is gone: the stock box was invented; Mastercam's stock display is NOT DECODED.)

Engine: `cam2d.c` (chaining + toolpaths), `offset.cpp` (C wrapper over Clipper 6.2.0, Boost licence, in
`vendor/clipper6`). Test harness: `cam_cli PART list | contour|pocket|drill CHAINS [--dia --depth --top --step
--stepover --side out|in|on --tabs N --finish N --lead R --stock S --noarcs --side-svg --nc F --svg F]`.

Checked:
- Pocket on the tramming-rod outline with both holes as islands (D6, 3 mm deep, 40 % stepover) plus corner clean-up:
  replaying the G-code at final depth leaves 0.32 % uncut, which is raster noise.
- Contour: leads, finish passes and tabs (seen in the side view, tabs only on passes below the tab top).
- Open chains: left/right offsets on the correct side.
- Arc fitting: max deviation 0.004 mm against the line-only output; the tram program shrinks from 2087 to ~1223 lines.
- Drill emits G99 G81 … / G80 / G0 Z clearance.
- Sidecar save → reopen restores the operation.

- File handling: a scripted run (`MCAMUI_SCRIPT=...`, see `run_script` in mcamui.c) covers generate → unsaved prompt →
  save → close → reopen, tool edit → regenerate → post, save as, delete op, and error cases. The part file's checksum is
  unchanged afterwards.
- GUI input without a desktop: `MCAMUI_EVENTS="F:click:x,y;F:clickw:wx,wy;F:key:Ctrl+Z;F:text:abc"` pushes mouse / key
  events into the program's own event queue at frame F. Use it with `--shot out.bmp` (hidden window). The state is
  printed as `[events]` at the end.
- Script steps for drawing tests:
  - `draw:CMD|x,y|eN@x,y|cN|apply`: a point, entity N picked at x,y, the chain through entity N, Apply;
  - `gset:r=5,mode=1` (options) and `sel:1,2,3`;
  - `ents` (lists the geometry);
  - `extrude:type,dist,dir,reverse,draft,thin,thick,target|seed,...`.

  The drawing commands, Extrude (create / cut / boss, undo / redo, save / reopen) and every command panel ran under
  AddressSanitizer/UBSan without errors.
- Loader robustness: 2,268 damaged copies of the user's and sample parts (truncated, random bytes, huge sizes,
  zeroed blocks) load under AddressSanitizer/UBSan without a crash or hang.

Not yet:
- the exported `.mcam` files are not verified in Mastercam yet;
- stock removal simulation in the GUI (Verify; `cam_cli --verify` has the raster model), 3D toolpaths;
- Dynamic: link routing is greedy, so paths are longer than Mastercam's on parts with large islands (A001);
- Engrave text, FBM, Blend Mill and swept/revolved 2D ops are not implemented.
- hand-testing in the GUI (checks so far are scripted).
