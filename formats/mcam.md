# .mcam / .mcx-N part file (Mastercam 2017 = major 0x13)

Derived from mccore.dll (addresses are Ghidra VAs, image base 0x180000000). Walker: `tools/mcam_walk.py`.

## Version detection — `get_file_version` -> FUN_18006f890
First `u32` of the file selects the generation:
| first u32 | meaning |
|---|---|
| 0x98 | current; header ent follows (0x98 bytes) |
| 0x90 | older |
| 0x88 | VX-era (`ReadVXEntFromDisk`) |
| other | V9-era (`ReadV9EntFromDisk`), only if extension is a known Mastercam suffix |

Header ent (offsets relative to ent start, i.e. file offset 4): `+0x00 u32 == 0x200`, `+0x68 u16 == 8`,
`+0x6a u8 major` (must be > 0x10), `+0x6b u8 minor` (must be > 0x3b), `+0x6c..` more `version_header` bytes.

Newer releases: Mastercam 2017's own major is `current_version[2]` = 0x13 (minor 0x45; FUN_1808c4ce0,
decomp/units/core_units.c:834). A part with a larger file major is refused, nothing read: `DoFileOpen` (mccore 0x1803475b0,
decomp/mastercam_filedlg.c:850-857) `post_warning`(mccoreres 5280 "The part file cannot be loaded because it is from a newer release
of Mastercam. A new part file will be opened instead.") and returns failure; the Open dialog (`DoFileDialog`, l.413-424) shows
5280 + "\n" + 5301 "Please select a different file." (MB_OK) and opens again. Ours (viewer/mcam_grp.h `mcam_part_newer`): refused
from major 0x16 up (decision FV-1), so the 2018 (0x14) parts keep loading.

Extension -> expected major (`IsMastercamFile`): `.mcam`/`.emcam`/`.mcam-*md` 0x13, `-9` 0x12, `-8` 0x11, `-7` 0x10,
`-6` 0x0f, `-5` 0x0e, plain `.mcx` 0x0a, `.mc9` 9, `.mc8` 8. Bundled templates (`mill.mcam_8` etc.) are major 0x12, minor 0x3f.

## Overall layout (verified: all 7 bundled templates parse to EOF)
```
[ent records ...]                 u32 len + ent
  DB_ID/0xF  -> u32 count + count*(0x1ae0 op_group + its +0x110 members*0x17)   (toolpath groups; mcam_write.md 2)
  DB_ID/0xE  -> mtable records until id==0               (material table)
  geometry / ASSOC (ops, toolpaths, tools, setup...)
  SOLIDXMT_ID -> XT bytes, u32 id, name, ops-history     (repeated per solid)
[binary NCI records ...]          u16 size + nci_bin, until EOF
```

## Body — loop of `read_ent_from_disk` (DbLoIO.cpp, FUN_1802c62d0)
Record = `u32 total_len` + `ent` (in-memory max 0x2938 bytes). `ent` layout:
- `+0x00 u32 class` — official names from the table at mccore 0x181307690:
  P_ID 0x1, L_ID 0x2, A_ID 0x4, S_ID 0x8, N_ID 0x10, W_ID 0x20, D_ID 0x40, NB_ID 0x80, SPLINE_ID 0x88, SURF_ID 0x100,
  DB_ID 0x200, CONS_ID 0x400, CD_ID 0x800, XH_ID 0x1000, CC_ID 0x2000, ASSOC_ID 0x4000, CHILD_ID 0x8000,
  SOLID_ID 0x10000, SOLIDXMT_ID 0x20000, FILE_ID 0x40000, RELIEF_ID 0x80000, PMESH_ID 0x100000, STOCK_ID 0x200000.
- `+0x06 u8 color`, `+0x08 i32 level`, `+0x10 attrib[8]` (the line width double, compared against
  WfHighlightWidth in draw_ent_nolist), `+0x18 u16 line style` (set_gl_style: 1 solid, 2-7 stippled),
  `+0x1a u16` (1 in every file seen) — `MaCurve::InitAttributes`.
- `+0x20 i64 eptr`, `+0x30 time32 created`, `+0x34 i32 ent_idn` — `debug_ent_str`.
- `+0x2c i32` ASSOC_ID subtype (table at 0x181307800): OP_ID 1 (0x28d0 fixed), TP_ID 2 (800), TL_ID 3, M_ID 4, NCI_ID 5,
  V_ID 6, SETUP_ID 7 (0x10e8), LTL_ID 8, WPSET_ID 9, LBDRY_ID 10, MISC_ID 11, MACHCOMP_ID 12, MACHGRP_ID 13, STATION_ID 14,
  ENTLIST_ID 15, TOOLBLOCK_ID 16, ..., DATA_STREAMS_ID 29. TP_ID kind at `+0x90 i16`: TP_CHN 6, TP_BND 7, TP_EPTRS 0x2b, ... (table 0x1813079d0).
- `+0x48 u8 major, u8 minor, ?, u8 release, u8 product` (per-entity version stamp).
- `+0x49 u8` per-ent version byte (gates field sizes).
- `+0x56 u8` != 0 -> payload is a serialized `ClassEnt` (`ClassEnt::CreateFromFile`).
- `+0x68` start of class-specific union. For class 0x200, `+0x68 u16` = system subtype. For class 0x100, `+0x68 u8` = surface kind (0 NURBS, 1 parametric, 4 loft, 10 trimmed).

Fixed/variable sizes per class: `bytes_per_ent(ent, 1)` (fixed) and `bytes_per_ent(ent, 2)` (variable tail) at 0x1802e5230
— see `decomp/mccore_sections.c`. `total_len` must equal fixed + variable, except system subtypes 12/17/18/21 where the tail is `total_len - fixed`.

### Geometry bodies (at +0x68; `bytes_per_ent` sizes exclude the 0x68 header)
| class | body |
|---|---|
| P_ID | `3d xyz` |
| L_ID | `3d p1, 3d p2` |
| A_ID | `3d start, 3d end, 3d center, d radius, d start_angle (rad), d sweep (rad), d ?` |

### Arc views (A_ID +0xc8) — decoded 2026-10-22
a_3d at ent +0x68: `+0x00 p_3d start (world)`, `+0x18 p_3d end (world)`, `+0x30 p_3d centre in the arc's VIEW coordinates`,
`+0x48 radius`, `+0x50 start angle`, `+0x58 sweep` (view angles, radians; a negative sweep runs clockwise in the view),
`+0x60 i16 view number` (= A_ID +0xc8). A point at angle t = view_to_world(centre + r (cos t, sin t, 0), view)
(mcgeomsld ps_arc_to_ps_arc 0x3e060, decomp/mcgeomsld_arcs.c l.80: get_view_matrix + view_to_world of +0x98 / +200;
mastercam.dll view_to_world 0x180341520: views < 2 = identity, else world = M^T v with M = the view's rows). Views 1..8
are the reserved views (ReservedPlanes::BuildStandardMatrices, decomp/mastercam_stdviews*.c: 1 Top, 2 Front, 3 Back,
4 Bottom, 5 Right, 6 Left, 7 Iso, 8 Axo) and are not stored in a part; other views are DB_ID 0x200 subtype 0 records
(+0x6a view number, +0x70 matrix_33; numbers 9..18 in a new part from the defaults, 20 / 65535 in the user parts).
Every arc of the 16 user parts has view 1. find_view_k (mastercam.dll 0x180337960, decomp/mastercam_findviewk.c):
the first view 1.. whose z row is +-dir within 1e-12, else the best within 2e-7, else (create) a new view: (1,0,0) -> 5,
(0,+-1,0) -> 2, (0,0,+-1) -> 1. AutoCursor: mid point = eval_curve at the mid parameter, centre = view_to_world
(decomp/mastercam_dlsearch.c l.775 / l.980); Analyze shows the view number, the world centre and the view angles
(decomp/analyze/arc.c FUN_1800737b0). Here: viewer/mcam_load.c (Ent.vw / vm, mcam_arc_pt, mcam_new_arc_view), drawn /
picked / snapped (mid, quadrants) / chained (operations only) / saved (mcam_saveops.c) / projects (GEO line + view);
cam2d.c cam_generate maps an arc whose view z is +-the Tplane's z into the Tplane (others are refused).

### Fixed body sizes (`bytes_per_ent(ent,1)` minus the 0x68 header) and variable tails (`bytes_per_ent(ent,2)`)
| class | fixed | variable tail |
|---|---|---|
| P_ID 0x1 | 0x18 | – |
| L_ID 0x2 | 0x30 | – |
| A_ID 0x4 | 0x68 | – |
| S_ID 0x8 (parametric spline) | 0x58 | `nseg(+0x48) * order(+0x4c byte) * dim(+0x4d byte) * 8` |
| N_ID 0x10, W_ID 0x20 | 0x160 | – (drafting; position at +0x70, sizes like text height 0.12 / arrow 0.25 follow; purpose of N_ID unconfirmed — no text stored) |
| D_ID 0x40 (dimension) | 0x7a8 | – |
| NB_ID 0x80 (NURBS curve) | 0x70 | `(dim * ncp + nknots) * 8` |
| SURF_ID 0x100 | 0x98 + per kind | kind 0 NURBS: +0x18, `(dim*ncu*ncv + nku + nkv)*8`; kind 1 parametric: +0x10, `a*b*c*n*m*8`; kind 2: +0x50; kind 4 loft: +0x10, `(n*m + k*8)*4`; kinds 6/8: +0x30; 7: +0x20; 9: +0x40; kind 10 trimmed: +0x38, `(n + 2*m)*8` |
| CONS_ID 0x400 | 0x88 | – |
| ASSOC_ID 0x4000 | per subtype: OP 0x28d0, TP 0x320, TL 0x698, M 0x3a0, NCI 0x360, V 0x360, SETUP 0x10e8, LTL 0xa48, WPSET 0x780, LBDRY 0x90 (+n*0x78), MISC 0xb40, MACHCOMP 0xa10, MACHGRP 0x130, ENTLIST 8 (+n*4), TOOLBLOCK 0x178, TLPOSTN 0x150, 19 0x3c0, MACHPOS 0x2f0, MACHGRP_PRM 0xc0, CNC_MACHINE 0x1320, CNC_CONTROL 0x1c4, CNTRLCOMP 0xe68, CNTRLPOST 0x214, AXIS_COMBO 0x1e90, MACH_MODE 0x1b4, DATA_STREAMS 0x280, 30 0x640 | |

### Curve / surface layouts (from mccore code; **no samples in the bundled files, so unverified against data**)
NURBS curve (`nb_type` at ent+0x68; `MaNURBSpline::MakeNURBSplineFromNbType`):
`+0x60 i32 nknots, +0x64 i32 ncp, +0x68 u8 degree (order = degree+1), +0x69 u8 values per point (2 = xy, 3 = xyz or wxy when
rational, 4 = wxyz), +0x6a u8 flags (bit2 clear = rational; bits 1, 3 copied to curve flags)`. Tail = `knots[nknots]` then control points
(weight first when rational).

Parametric spline (`spline` at ent+0x68; `convert_ppspline_to_nbspline`): `+0x48 u32 nseg, +0x4c u8 order, +0x4f bit0 closed`.
Tail = per segment `order` power-basis coefficient vectors (xyz), parameter 0..1 per segment.

NURBS surface (`nbs_type` at surf+0x98 = ent+0x100; `MaNURBSurf(DB_LIST_ENT*, nbs_type*, bool)`):
`+0 i32 nku, +4 i32 nkv, +8 i32 ncu, +0xc i32 ncv, +0x10 u8 values per point (3 xyz / 4 wxyz), +0x11 u8 u degree,
+0x12 u8 v degree, +0x13 bit0 = non-rational`. Tail = u knots, v knots, control net.
**Verified on data (2026-10-25):** nku = ncu + udeg + 1 (full knot vectors); the control net runs **u fastest**; rational
points are stored **homogeneous** (w, w·x, w·y, w·z): TUTORIAL 2 SWARF's arc surfaces are exact circles only with u fastest +
division by w (radius spread 0.00000 vs 0.5-58 for the other three readings). MaNURBSurf normalises the knots to 0..1 for its
own evaluation; the stored range is the raw knot range.

Trimmed surface (kind 10; `MaSurface::LoadTrimmedSurf` -> `MaFace::MaFace(DB_LIST_ENT*, bool, double)`, decomp/trimsrf/loadtrim.c):
`ent +0x100` = the base surface (in the file: the base record's eptr, ent +0x20 - measured), `+0x10c i32 m` uv points, `+0x128
i32` loops, `ent +0x6e bit 2` = normal reversed (SetNormalReversed). Tail at **ent +0x138**: m (u, v) doubles, then per loop `i32
start, i32 count` (points start .. start + count, closed back to the first; count + 1 == 1 skipped; loop 0 outer, the rest holes).
The uv points are in the base record's `+0x70 u0, u1, v0, v1` range (= its knot range for a NURBS base, measured), which
MaPolyLine::UVTransform maps onto MaSurface's range when they differ.

**Blanked entities:** sel bit `0x100` (ent +4) = blanked (hidden) - `ChangeBlankState` (mccore 0x1806d72a0) turns it on and erases
the entity, turns it off and draws it (decomp/trimsrf/blank.c). Every trimmed surface's base is blanked; 4520 lines, 1631 arcs and
259 points of a 500-part corpus sample carry it too.

**Ops' drive surfaces:** TP_ID kind 4 records name an op's drive surfaces (`+0x6c` idn, `+0x74` class 0x100) - measured on TUTORIAL
7 METRIC ops 5 / 6 (op 132), the containment chain following as kinds 6 / 7.

Corpus (1363 parts): surface kinds 0 NURBS (30458 in 625 parts), 10 trimmed (18591 / 398), 1 parametric (1750 / 32), 2 (378 / 7),
7 (28 / 7), 6 (23 / 4), 8 (16 / 15).

### Tools (TL_ID, 0x698) — verified on mill/router
`+0x68 i32 tool#, +0x6c i32 type (formats/tooltypes_mill.tsv: 3 Drill, 4 Tap RH, 10 Endmill1 Flat, ... 25 Barrel mill),
+0x70 f64 diameter, +0x90/+0x94 dia/len offset, +0x98/+0xa0/+0xa8 feed/plunge/retract, +0xb0 rpm, +0xbe name, +0x136 library,
+0x33e holder name`. Operations embed the same struct at ent+0x348 (tool# 0x348 <-> 0x68, name 0x39e <-> 0xbe).

### Toolpath geometry references (TP_ID, 0x320) — verified
`+0x68 op id, +0x6c referenced ent_idn, +0x74 its class, +0x90 i16 TP kind (6 TP_CHN chain header, 7 TP_BND chained entity, ...)`.
Check: for all 6 mill operations the bounding box of the chained entities equals the pocket size in the op comment.
TP_CHN: `+0x98 i32 chain number (1-based), +0x9c i32 entity count, +0xac "Chain N"`.
TP kinds seen in the user's parts: 6 chain header, 7 chained entity (lines/arcs, also class 0x10000), 1 and 27 points
(drill / thread mill), 30 tab position on a contour (entity + index; not part of the chain: `+0x98 chain, +0x9c index,
+0xa8 x y z on the chain (zero in some files), +0xc8 f64 t on the entity from its own start, +0xd0 width, +0xd8 height,
+0xf8 ramp angle (rad)`; NAKTO op 5: t 0.002 on a 54.6 mm line = x 70.889). TP_BND: `+0x98 chain number,
+0x9c index in chain, +0xa8 i32 direction` (1 = entity traversed against its stored direction: line p2->p1, arc
end->start). With it, single-entity chains (circles) get their true direction. Checked 2026-10-14 with
`tools/oracle.py`: contours whose median deviation from Mastercam's NCI is under 0.06 mm went 89 -> 110 of 137.

### Solid chains (TP_BND class 0x10000)
A chain of solid edges ("Solid Chain N" header, one TP_BND per chain naming the SOLID_ID entity, +0x9c = 2) stores no edge
identity anywhere in the file: not in the TP records, not in the op, and the Parasolid bodies carry only the runtime
`CNC/mcEptrPtr` attribute on every edge/face. SOLID_ID `+0xa8` = N of its XT blob "sld_N". On buckle-v6/7/8 op 31 the chain
is the **outer loop of the solid's highest upward planar face**: Mastercam's tool centre stays within R + stock (4.02 for
R 3 + stock 1, from outside) of it, and the material cut inside matches 100 % (`viewer/xt_mesh.c xt_top_loop`,
`tools/mcam_ops.py solid_top_loop`). Parasolid detail: HALFEDGE.vertex is the vertex at the halfedge's END.

**Where Mastercam keeps the chain (2026-10-16, decomp/mcgeomsld_toolp.c):** in the solid's ops history (the block after
the XT, `mc_read_opsfile` -> FUN_18026b3c0), as an op of type **0x18 = SldToolPOp** (buckle-v6: a 139-byte op 24 in
sld_200 and sld_108, one per solid chain; it holds the SOLID_ID record's eptr (= TP_BND +0x80) and a pick point, e.g.
(20.04, 14.01, -3.5)). `sld_get_ToolP_Chains(solid, op id, ...)` finds that op (`FUN_18026a5c0`, vfunc +0x148 = op id),
**rolls the solid back to it** (`FUN_18026c170`) and reads `SldToolPOp::GetToolPChainSelections` (FUN_18028e010):
arrays of Parasolid entity tags of the rolled-back body (faces / edges / loops, SLD_LOOP_INP, chain orders, names,
SldChgAtPt). Rebuilding the chain needs the body at that history point (Parasolid rollback) and its tags: **NOT
DECODED / not reproducible without a modelling kernel**; `xt_top_loop` (the outer loop of the highest upward planar
face) and `xt_cylinders` stay CORRELATED with Mastercam's toolpaths on the user parts, not decoded.
**2026-11-07:** op 31's NCI machines every loop of that face - the outer loop and the face's two holes, each as a
region of its own in the face's XT loop order (formats/dynamic.md "Machining regions (decoded)"); the Dynamic port takes
all of them (`xt_top_face_loops`, `cam_solid_face_loops`; CORRELATED, the chain itself stays NOT DECODED).

### Contour corner / entry rules (oracle, 2026-10-14)
- Op `+0x5d4` i16 = Roll cutter around corners (0 none, 1 sharp, 2 all; 1 in every user op). Sharp rolls corners that
  turn by >= 45 deg (corner of 135 deg or less): NAKTO op 5 rolls its 45.000 deg turns and leaves 44.99 deg ones sharp.
- Entry at the chain start: the start pushed out along the exact normal of the first entity (a sampled arc's chord
  normal is half a step off: 0.11 mm on buckle op 7); if the closing corner is an outside corner left sharp, the mitre
  point (d tan(turn/2) back along the first entity; buckle op 13).
- Open chains are trimmed globally (the offset of a concave arc smaller than R vanishes; r = R collapses to its centre,
  buckle-v12 op 13).
- Tabs on entities whose offset collapses (concave arcs r 1.0 / 1.6 with R 1.5, buckle-v6 op 18) are not cut by Mastercam;
  not modelled (one op family).

### Facing (opparams case 81, checked on BRACKET4 op 8 and buckle-v8 op 41)
`+0x1228` u8 style: 0 zigzag, 1 one way, 2 one pass, 4 dynamic (help: "Dynamic cuts from the outside to the inside").
`+0x1240` max stepover %, `+0x1248/+0x1250` along overlap abs/%, `+0x1258/+0x1260` across overlap abs/%,
`+0x1268/+0x1270` approach distance abs/%, `+0x1278/+0x1280` exit distance abs/%, `+0x1288` auto angle, `+0x1290` angle,
`+0x1298` feed between cuts, `+0x12a8` Z stock (mirrors the floor stock `+0x888`: applied once), `+0x12c0` climb,
`+0x12c1` reverse last pass. Passes: evenly spaced, floor(range/step) + 1 intervals, tool centre on the chain edges at
50 % across overlap; each pass spans the chains grown by along·D − R (zigzag / one way) or along·D (dynamic). Dynamic
(3 passes on a 3 mm strip): starts at −v, first pass one way, then zigzag; matches to p95 0.04 mm.

### Tool slots and the control's tool registers (2026-10-15)
- An operation's tool is its **slot** `op +0x544` (record +0x5ac): 1-based index of the TL_ID records in file order
  (BRACKET4: slots 1, 9, 10, 11 = 1st / 9th / 10th / 11th TL record). Tool numbers can repeat (BRACKET4 has two T8 and
  two T2), so the slot is the identity (Renumber tools, the Tool list report and the tool system use it).
- Control pages are ASSOC subtype 24 records (BRACKET4: pages 0-11, two copies), page number at ent+0x68. Page 9 = NC
  path, written by CCNCControl::AddNcPath (mccontrol.dll) as the control object's +0x238..+0x1018 at ent+0x80: the
  lathe tool registers (GetTool, control +0x4c0a) land at ent+0x18a and the mill ones (GetToolMT, control +0x4d1e) at
  ent+0x29e (on mill / router controls both slots hold the mill page). nc_path_tool_ctrl: +0 register type (0 =
  AddToTool), +4 length offset add (LOfsAmt), +6 diameter offset add (DOfsAmt), +8 back offset (lathe), +0xc u8 use
  head number for tool (UseHeadForTool); ControlDefaults: 1 / 30 / 30 / 30, tokens T_TOOLREGISTER*MILL / *LATHE. The
  user's parts: type 0, length 0, diameter 0, use head 0.

### Machine / control definitions (.mmd .lmd .rmd .wmd .gmd .control) — same container
All 70 Mastercam-format data files in the install (7 part templates + every machine/control/component file) parse to EOF.
Machine files: MACHCOMP_ID components (name at +0x68: "Machine Base", "VMC X Axis", "Lathe Chuck" ...), MACHGRP_ID groups
(name +0x68), MACHGRP_PRM_ID, AXIS_COMBO_ID (name +0x68), DATA_STREAMS_ID (+0x68), CNC_MACHINE_ID (coolant names ~+0x1ec),
ENTLIST_ID, plus a TlMgr (33) and MastercamPlanes (36) object. Control files: CNC_CONTROL_ID (control name +0x78, e.g. "Haas"),
CNTRLCOMP_ID pages (post text, NC paths, misc values), V_ID views. Kinematic/numeric fields of components are not mapped.

### Serialized objects (ClassEnt: ent+0x56 != 0; ASSOC subtypes 31/33/35/36)
Body is an MFC `CArchive` stream: a new class is written as `FF FF <u16 schema> <u16 len> <name>`. The schema selects the
versioned reader exported from mccore (e.g. `Cnc::Tool::TlMgr::DeserializeV5`). Seen: subtype 36 `MastercamPlanes v4`
(SystemPlanes, Plane, Matrix33, P3D), subtype 33 `TlMgr v5` (TlAssembly v8, TlTool v12, TlToolMill v9, TlToolEndmill v4,
TlHolder v3, TlCoolant, TlProfile ... — the same object model as the `.tooldb` SQLite tables), subtype 35 `MTMgr v6`.
`tools/carchive_scan.py` lists them. Member-level read sequences for 106 mccore `DeserializeVn` / `DeserializeItemsVn`
routines are transcribed in `formats/carchive_layouts.md` (generated by `tools/deser_layout.py` from
`decomp/mccore_deserialize.c`): primitive reads with sizes, raw `CArchive::Read` blocks, nested objects, `base` = the
previous version is read first, and loop counts are flagged. Reads routed through setter lambdas are not yet named.
Spot check (tramming-rod, subtype 33): after the `TlMgr` v5 tag the stream holds i32 3, a 16-byte TlID GUID
(`e2264524 65ee3b4f bc9a9927 82cd2690`), then the nested object (`TlToolEndmill` → `TlObject` with the same GUID
and CString "6 Flat Endmill"). This matches the transcribed V5 sequence (count, bytes[0x10], object).

#### Stream grammar (observed on tramming-rod subtype 33; `tools/mfc_archive.py` tokenizes)
- MFC tags: `FF FF schema len name` = new class; `0x8000|i` = class i, already seen; `i < 0x7FFF` = back-reference to
  object i. Map indices count classes and objects together, from 1.
- Mastercam writes **one class tag per inheritance level** (`WriteClass`, class index only) before that level's data,
  base level first. An embedded object (`WriteObject`) is a leaf class tag (class + object index), then its levels,
  ending with the leaf level re-tagged. Example: one holder-profile line segment (repeated 18 times):
  `0f 80` (new TlProfileLineSegment object) · `04 80` + 16-byte GUID + `00 00` (TlObject level: id, empty name) ·
  `11 80 00` (TlProfileSegment level, 1 byte) · `0f 80` + 4 doubles (TlProfileLineSegment level: x1 z1 x2 z2).
  The segment list is preceded by `07 80 12 00 00 00` (TlProfile level: count 18). **Verified:** the decoded segments
  chain end to start with zero gap into the outline of holder "B2C3-0020" (radius, height): (10.858, 0) → (15.938,
  2.718) → … → (23, 47.29) → (21.476, 48.814) → (0, 48.814); 48.8 mm tall, max radius 23.
- **Writer side (`decomp/mccore_tlserialize.c`, 2017 mccore) settles the mechanism.** Every `X::Serialize(ar)`:
  1. calls `Base::Serialize(ar)`;
  2. calls `ar.SerializeClass(X::GetThisClass())` (`Ordinal_12658`): the level tag, class index only;
  3. when loading, switches on `ar.GetObjectSchema()` (`Ordinal_5880`) to `DeserializeVn`; when storing, writes
     the level fields.

  Pointer members go through `ar.WriteObject` (`Ordinal_14063`: class tag + object index). Embedded members call
  their own `Serialize` directly, so they emit only level tags. Writers:
  - `TlMgr`: u32 n, n × {GUID[16], WriteObject(item)}, then further counted lists.
  - `TlHolder` level: CString +0x130, u32 +0x138, CString +0x140, CString +0x150, raw4 +0x148, raw4 +0x158.
  - `TlTool` level (v15 writer): raw[0x288] legacy tool struct (vfunc +0xa8), u32 +0x130/+0x134/+0x138,
    CString +0x140, bool +0x148, raw4 +0x150, u32 +0x14c.
  - `TlToolEndmill` level: f64 +0x988, f64 +0x968, f64 +0x970, raw4 +0x978, f64 +0x980.
- **Version matters.** tramming-rod was written by X9 (TlTool **v13**). Its reader starts with an embedded
  `TlProfile::Serialize`, which writes the TlObject level (`04 80` + GUID + empty CString, the 20 bytes after the TlTool
  tag), then the TlProfile level tag (new class #7, class only). The index fit is consistent: TlMgr root (level only,
  #1); TlToolEndmill via WriteObject (class #2, object #3); TlObject #4; TlAssemblyItem #5; TlTool #6; TlProfile #7.
  **Verified:** the TlProfile level holds u32 0 (no segments). The next 0x288 bytes (TlTool's raw block) are
  **byte-identical to the TL_ID entity body (entity +0x68 .. +0x2f0)**, so the existing TL_ID field decoder also decodes
  each tool inside the serialized TlMgr library.
  A generic tokenizer can't tell level tags from object tags. A real reader must follow the class grammar (`Serialize`
  calls `SerializeClass`; `WriteObject`/`ReadObject` for pointer members).
- Consequence: the `DeserializeVn` read order in `carchive_layouts.md` describes one level's own fields only. The
  level tags and nested objects between them come from `CArchive::ReadClass/ReadObject` and are not in those listings.

### System (0x200) subtypes
| sub | meaning | notes |
|---|---|---|
| 0 | view matrix (`matrix_33` at +0x70) | |
| 1 | ? | |
| 8 | file header | |
| 10 | ? (FUN_1802d19d0) | |
| 0xc | font | tail = text |
| 0xd, 0x17 | ? | |
| 0xe | material table marker | **followed out-of-band** by mtable: `[0x28 hdr: i32 id@0, u64 size@0x10][size bytes]...` until id==0 |
| 0xf | toolpath groups marker | **followed out-of-band** by `u32 count` + per group the 0x1ae0 op_group and its member entries (+0x110 count x 0x17 bytes; the record size varies by major version, see `TpGrpList::RetrieveFileGroups`, mcam_write.md 2) |
| 0x10 | wire change points | followed by `read_wchgpts` data |
| 0x11 | import history text ("Original Import File name = ...") | |
| 0x12 | thumbnail: BITMAPINFOHEADER 256x256x32 | |
| 0x13 | history ent | |
| 0x14 | converter ("ea") data, 13 x 0x24 | |
| 0x15 | ? (FUN_1802d5220) | int count at +0x7c |
| 0x18 | ? | |
| 0x19 | worksheet | |

### Solids (SOLIDXMT_ID 0x20000) — mcgeomsld.dll `sld_read_solids` / `mc_bypass_opsfile_read`
Ent record (total 0x16c): `+0x49 u8 ver`, `+0x68 char name[0x100]` (e.g. `sld_108`), `+0x168 u32 xt_size`. Then:
1. `xt_size` bytes of Parasolid transmit data — binary `.x_b` if ver >= 0xf, else text `.x_t`. Valid standalone XT files.
2. `u32 solid_id`, `char name[ver > 0x1b ? 0x100 : 0x40]`
3. ops history: `u8, u8, u32, u32, u64 (u32 in 32-bit files), u32 count`, then `count * {u32 id, u32 size, size bytes}`.

### Thumbnail (DB_ID 0x12)
BITMAPINFOHEADER at ent+0x88 (its biSize field holds the total DIB size 0x40028), 256x256x32 bottom-up BGRA.

## Operations (ASSOC_ID / OP_ID, record 0x2938 = 0x68 hdr + 0x28d0)
Verified on mill/router/lathe/millturn/wire templates (`tools/mcam_export.py` prints them).
| off | type | field | evidence |
|---|---|---|---|
| 0x68 | i32 | op id | matches TpOpList ids, `bnci_restore` OpByID |
| 0x6c | u16 | opcode (TP_OPCODE) -> name via `formats/opcodes.tsv` | reader special-cases 0x1b8; names resolve sensibly per machine type |
| 0x6e | char[] | op comment | |
| 0x170/0x180/0x190/0x1a0 | {f64, flags[8]} | clearance/retract (order unconfirmed), feed plane, depth | NCI Z levels 1.0 / 0.25 / -0.5 |
| 0x1d8 | char[] | NCI file path | |
| 0x348 | i32 | tool number | = NCI 1002 tool field |
| 0x350 | f64 | tool diameter | 0.375 vs 0.5 across ops with 3/8 vs 1/2 tool |
| 0x370 / 0x374 | i32 | diameter / length offset numbers | |
| 0x378 / 0x380 / 0x388 | f64 | feed / plunge / retract rate | feed = NCI F word |
| 0x390 | i32 | spindle rpm | = NCI 1002 |
| 0x39e | char[] | tool name | |
| 0x416 | char[] | tool library path | |
Lathe ops store feeds negative (likely feed-per-rev convention — unconfirmed).

### Operation planes (op_view, 3 x 0xb0 bytes) — decoded 2026-10-22
| record | op | plane |
|---|---|---|
| 0x5d8 | +0x570 | Tplane (tool plane) |
| 0x688 | +0x620 | Cplane (comp / construction plane) |
| 0x738 | +0x6d0 | WCS |

op_view (Plane::InitOperationView mastercam.dll 0x1803db1c0 = Plane::Convert(op_view&)): zeroed, then `+0 u8 1`,
`+2 i16 view number` (PlaneBase::GetShortViewNo: the reserved view whose matrix equals the plane's, else FindViewNo),
`+8 matrix_33` (9 f64 rows = the plane's x / y / z axes in world coordinates, Matrix33::Convert), `+0x50 p_3d origin`
(world), `+0x68 i16 plane id` (PlaneID::GetShortID), `+0x92 i16 work offset` (Tplane: -1 = Automatic, >= 0 = Manual
n; PlanesPageBase::MapToOp writes the WCS / Cplane +0x92 too when they are the Tplane's plane). Plane +0x330 = the
plane's own work offset (Plane::Init: -1). op coordinates (wpt_to_op, decomp/mccore_opview.c) = the Tplane rows . (p -
origin); the NCI is in op coordinates. On read (ProcessAssocOperation FUN_1802cfb60, decomp/mccore_tooladd.c l.261)
the three views (WCS, Tplane, Cplane order) get their plane id mapped through the file's plane id map
(GetDbPlaneIdMap); a missing plane is searched by matrix + origin (SearchPlanePtr), else created as a new plane named
"<type description>: <op description>"; a found plane whose data differ overwrites the view (ConvertNoWorkOffset).
Data: every op of the 16 user parts and of the defaults libraries is `1, view 1, id 1, identity, 0, wofs -1` in all three
views, except Mill_MM / Mill_Inch.mcam-defaults Dynamic / 2D HS ops (opcode 134 / 132): work offset 0. The system planes'
ids in the user parts' SystemPlanes archive: 1 Top .. 7 Iso (= the view numbers). Writer: formats/mcam_write.md 11;
reader: viewer/cam_fileop.c via cam2d.c cam_planes_from_record (system planes only: matrix + id + zero origin).
New ops: InitOperation overwrites the defaults op's views with the active planes (decomp/mastercam_opviews_init.c).

**Every MP-visible operation parameter** is mapped by number (`formats/opparams/`), recovered from mcmpparameters.dll
`PrmAppMPParameter::ParseOperation` (the code that feeds operation parameters to posts):
- `op_params_common.tsv`: 483 parameters present for every operation (e.g. 10002 tool# 0x348, 10005 diameter 0x350,
  10010 XY stock to leave 0x2e0, 10020 0x170, 10024 feed plane 0x190, 10029 depth 0x1a0, 10030 feed 0x378, 10034 rpm 0x390,
  10068 Z stock to leave 0x888, 15346 cutter comp type 0x5d0).
- `op_params_by_case.tsv`: opcode-specific parameters (union at ent+0x1228), 132 builder cases, ~30k rows;
  `opcode_param_case.tsv` maps each TP_OPCODE to its case (byte map at mcmpparameters 0x180012c50).
- `param_names_from_posts.tsv`: 44 parameter names taken from the generic posts' comments; the rest are known by number only.
`tools/mcam_export.py` emits every operation's parameters ({id: value}); values checked against known settings
(e.g. 2D HST roughing leaves 0.02 XY stock, finishing pocket 0, cutter comp 0 = computer).

`formats/opcodes.tsv`: 139 TP_OPCODE -> name pairs, resolved from the jump table in mccore FUN_1800ca8a0
(byte map @0x1800caffc, dword targets @0x1800cae04, each target `mov eax, <string id>`) + RT_STRING in mccoreres.dll
(`exports/mccoreres_strings.tsv`, 5428 strings).

## Stock Setup (machine group, struct group_pg3 = op_group +0x1228) — decoded 2026-10-22
Stored in each machine group (0x1ae0 bytes after the DB 0x200 / sub 0x0f record; the group's own id at +0x10c = 0 for
machine groups). Page: mastercam.dll CGroupPg3, opmanres dialog 1590 (decomp/mastercam_stocksetup.c); model: mccore
stock_ok / stock_calc_geom / FUN_1800d5a20 stock_calc_center / job_setup_stock_corners / calc_top_of_stock
(decomp/mccore_stock.c, decomp/mccore_tpsd_helpers.c). Code: viewer/core_ui/cui_stock.c, viewer/mcam_load.c, mcam_saveops.c
stock_patch.

| group off | type | meaning |
|---|---|---|
| +0x8a8 | group_view | stock plane: i16 plane id, +0x8aa i16 view no, +0x8b0 matrix_33, +0x8f8 origin |
| +0x1228 | u8 | Fit screen |
| +0x1230 / 38 / 40 | f64 x3 | size X / Y / Z (cylinder: Z = 0, written by the page's apply FUN_1801ed0b0) |
| +0x1248 / 50 / 58 | f64 x3 | origin, stock-plane view coordinates |
| +0x1260 | u8 | Display |
| +0x1261 | u8 | Solid (shaded) display; 0 = Wire frame |
| +0x1264 | i32 | shape 0 rectangular, 1 cylindrical, 2 solid, 3 file (STL) |
| +0x1268 | i32 | solid ent id (shape 2; the SOLID_ID entity's ent_idn) |
| +0x126c | i32 | cylinder axis 0 X, 1 Y, 2 Z |
| +0x1270 | u8 | centre on axis |
| +0x1271 | char[0x104] | STL file |
| +0x1380 | l_3d x12 (0x30 each) | the stock lines stock_calc_geom stores (world) |
| +0x15c0 / +0x1628 | arcs | the cylinder's end circles |
| +0x1690 | i16 | origin corner 0..8 (> 8 read as 0) |
| +0x16a8 | u8 | Use Machine Tree |

**Corner rule** (job_setup_stock_corners 0x1805e4dc0; FUN_1800d5a20 and the page's box -> origin FUN_1801edd80 use the
same convention): box min = origin + OFF, max = min + size, OFF = 0 (-X/2, -Y/2, -Z) centre of the top face, 1 (0, -Y, -Z),
2 (-X, -Y, -Z), 3 (-X, 0, -Z), 4 (0, 0, -Z), 5 (0, -Y, 0), 6 (-X, -Y, 0), 7 (-X, 0, 0), 8 (0, 0, 0).
**Lines** (stock_calc_geom, rectangular): top x0 edge, top y1, top x1, top y0, the same four at the bottom, then the
verticals top -> bottom at (x0,y0) (x0,y1) (x1,y1) (x1,y0).
**stock_ok**: rectangular X, Y, Z >= tol; cylinder X, Y >= tol; solid id > 0; file exists (else a message, Display and
shape reset to 0); not for lathe / mill-turn groups (+0x19e8 = 1 / 6; 0 in every user part; ported since the review fixes).
**calc_top_of_stock** = highest Z of the 12 lines in
op coordinates; InitOperation (mccore_ops.c:2425) applies it to the first operation only (empty op list, not lathe,
absolute top). **Facing** without chains (mcmill facing_op_uses_stock / make_facing_pock_bound): X and Y > tol, shape 0:
the job_setup_stock_corners rectangle at its top Z. **Display** (draw_stock 0x1803360a0): active group, Display on,
stock_ok; shapes 0 / 1 as the 12 lines in line style 4 and mill_stock_color (config 5029); Solid display with a stock
model -> shaded model (FUN_18033fe70).
**After the sheet** (EditGroupPropertiesDlg): mill groups run stock_ok(1); if FUN_1801e1a20 reports a change (shape,
stock plane view no, origin within 1e-10, then sizes X / Y (/ Z for rectangular), cylinder axis, solid id or file name;
the corner and display flags are not compared) -> mccore Mark2dHSTOpsRefMachStockDef4Regen(group id) marks dirty the
2D High Speed ops (opcode 0x86) of that group whose sub-style (op +0x11c0 = record +0x1228, MP 12713) < 2 (Dynamic /
Area Mill) and whose byte op +0x1760 (record +0x17c8, MP 41144, meaning NOT DECODED) is set: op +0x94 = 1 (dirty, record
+0xfc) and op +0x97 = 0 (record +0xff) (decomp/mccore_stockregen.c, decomp/mastercam_stockchanged.c; Ghidra's `long` is 4
bytes, so plVar1[0x470] / [0x5d8] are byte offsets 0x11c0 / 0x1760 - the earlier reading +0x2380 / +0x2ec0 was wrong). This
runs after the sheet whether it was closed with OK or Cancel (the stock plane is changed in the group directly).
Posting dirty ops (COpMgrTree::PostSelected -> DirtyOperationWarning, decomp/mastercam_post_dirty.c): when a selected op has
op +0x94 set (+0xe0 clear, an op with NCI), opmanres dialog 977 "Warning: Dirty Operations Selected" asks: 25350 "Regenerate
unlocked operations and continue (Locked operations will not be regenerated and will be processed as is)" (default:
RegenSelected, then post) / 25351 "Do not regenerate and continue" (ops whose NCI section is invalid, bnci_op_section_valid,
are unselected; the rest is posted from the stored NCI) / Cancel (no post). Ported 2026-10-24 (formats/opdlg_port_audit.md 19,
viewer/core_ui/cui_dirty.c): dialog 977 before Post / Backplot / Verify; Regenerate computes the dirty ops with this program's engines
(a stored op is then taken over), Do not regenerate posts the stored NCI.
**Dirty operations** (opdlg_port_audit.md 19): op +0x94 = record +0xfc (read on load into Op.regen, written by the save; +0x97 =
record +0xff cleared with it). Set by edit_operation when a changed op cannot have its NCI patched by bnci_update (BnciUpdate::DontAllow's
field mask), by a tool change (FUN_180a45440 IsDifferentEnoughToDirtyOp / EditSelectedMillTool), chain edits (EditSelectedGeometry2),
Edit Common Parameters / Recalculate feeds/speeds, the Stock Setup rule above; cleared by regeneration (bnci_regenerate) and Reverse
toolpath. Mastercam keeps the op's old NCI until it is regenerated.
Data: +0x17c8 is 0 in every 2D HS op of the user's parts and of the four defaults libraries - nothing gets marked there.

**Verification (2026-10-22).** cui_stock.c checked function by function against the decompiles above: corner offsets,
stock_ok, calc_top_of_stock / InitOperation, the centre, load (FUN_1801f2000) / apply (FUN_1801ed0b0), enable rules
(FUN_1801ed700), box -> size / origin (FUN_1801edd80, all 9 corners), All Surfaces / Solids / Entities (FUN_1801f1bc0 ->
FUN_1801f26d0 -> create_bbox_stock_no_dlg; "Override 2D construction mode?" No = flatten to the construction depth,
FUN_180347840), Select corners incl. Esc after the first point (FUN_1801f1840), Select origin (FUN_1801f16f0), picture
index (FUN_1801f2980), draw_stock, Facing. Data: the 12 lines recomputed from size / origin / corner match the stored
+0x1380 lines of all 23 machine groups with stock in the user's parts and the shared samples (max error 2.2e-16; every
one uses corner 0). A saved stock edit (BRACKET4: X 60, origin X 30 / Z 5, corner 4, Solid) re-reads with the
independent scripts and its lines match exactly; the changed bytes are only the group's stock fields + lines (and the
tp_hdr +0x6c geometry count, which the exporter recounts on every save: 35 vs Mastercam's 36 for BRACKET4 - exporter
issue, not stock). Small differences kept: a corner click on a non-rectangular shape is stored by FUN_1801f0070 (ignored here). (The Solid /
File shapes' reset on page load, FUN_1801f2000, is ported since the "Stock Setup rest" below.)

**Bounding box (25451)** (ported 2026-10-22, viewer/core_ui/cui_bbox.c): mc:FUN_1801f1150 suspends the sheet and runs
mccad create_boundingshape_either_method = the Bounding Box function panel in stock mode (formats/wireframe.md "## Bounding
Box"): settings from DAT_180649fc0 with the page's shape / axis / centre, Manual, no create flags, +0x60 = 1; Apply and
Create Tool Plane greyed. Every compute writes the stock live (rect FUN_1803481e0: shape 0, Display, sizes = spans, origin
= the box minimum, corner 8; cylinder FUN_1803483b0), so the outline follows the panel. OK: rect -> FUN_1801edd80 with the
page's corner, cylinder -> axis / centre / origin c / diameter 2 r / length len (FUN_1801f2f60); Display + Fit screen
checked, shape set. Cancel: the page is unchanged. The sheet comes back on OK / Cancel (st_pick = ST_PICK_BBOX meanwhile).
Deviation (ours): Mastercam then overwrites group +0x1228 from the *_no_dlg cache DAT_1806a1790; here the group's stock is
restored to its state before the panel, and the page's own OK / Cancel decides what is stored.

**Active machine group** (2026-10-22): OMgetActiveMachineGroup = the Operations Manager's machine group, else
TpGrpList::RetrieveActiveGroup -> RetrieveInsertLocationGroupIndex = the group holding the insert arrow. On load
COpMgrTree::LoadTree (mastercam.dll, decomp/opman_tree_all.c ~l.30690) walks the tree items from the last one backwards and
puts the arrow after the last op / group item - i.e. in the LAST machine group of the list (every machine group has a
toolpath-group item). The loader takes the Stock Setup (and the group's tool settings) from that group; the save patches
the same group (by id). Parts affected: BRACKET4, NAKTO-BRAKE-BRACKET, thread-test (two machine groups each; every op is in
the second). Op -> machine group: OP_ID record +0x5bc.

**Cylindrical stock** (stock_calc_geom shape 1, ported 2026-10-22): the axis (+0x126c) selects a RelativeType {X 5, Y 2,
Z 1} and Plane::RotateRelative (mastercam.dll 0x1803dd440 / RotatePlane 0x1803dce30) rotates the stock plane into the axis
view (Top stock plane: Right, Front, Top). End circles (a_3d at +0x15c0 / +0x1628, 0x68 bytes: first 0x30 bytes 0, centre
in the axis view's coordinates +0x30, radius = size X / 2 +0x48, start 0, sweep 2 pi, i16 view +0x60): centre =
view_to_view(origin, stock view, axis view), the second at depth - size Y for Front, + size Y otherwise, so the cylinder
runs from the origin along +axis for size Y. The 12 lines join the two circles at creep distances i * length / 12 (30 deg
steps). stock_ok: X, Y >= tol; size Z is written 0. Facing (make_facing_pock_bound): only when the axis is parallel to the
Tplane Z: one full circle (r = X / 2) at the origin's X / Y, Z = origin Z + size Y.
**Bounding cylinder** (All Surfaces / Solids / Entities with shape 1): create_bcyl_stock_no_dlg @mccad 18005e8b0 ->
FUN_18005bab0 (fit view: page axis Z -> active view, Y -> RotateRelative(3) = Back, X -> RotateRelative(5) = Right),
add_to_minmax @180057c70 in cylinder mode: an order-dependent incremental circle (first point = centre, the second
distinct point (> tol) makes a diameter, each later point outside grows the circle over the old far side), depth range =
length; Center on axis: centre = the active origin, r = the largest distance; FUN_180057e50 refines towards the minimum
circle (one support -> slide to two, two supports on one side -> diameter / shrink into the largest gap > pi - 1e-4).
Result: r, length, the min-depth end face's centre (world), axis = view row 2. Page (FUN_1801f2f60): origin = that
centre, size X = 2 r, size Y = length, Z 0, Display + Fit screen checked. The axis radios re-run the fit with the last
mask (FUN_1801ef060 / 0a0 / 0e0); create_bcyl has no 2D-construction question. Decompiles decomp/mccad_bcyl*.c,
mccad_calcpointsoncurve.c, mastercam_rotateplane.c. NOT DECODED: l_hv_ints, MultiplyMatrix33's operand order (views
checked by consistency), the temporary plane's origin for Center on axis, the display-list point order (ours: entity order).
**NCI extents** (25077, FUN_1801ef250 -> mccore get_bnci_extents @1800fd670 -> FUN_1800fda60; decomp/mccore_bnci_extents.c):
every op of the active machine group (+ child groups; record +0x5bc); records: rapids only as the pending point before the
next cut (dropped at a tool change), feed points (1 11 21-23 31 81 100) add P, P - 1013+0x28 (mill, 1013 i16 +0x30 = 0) and
P - 1013+0x20 / 2; 1013 = bnci_wr_1013 @18008e540: +0x20 = op +0x2e8 = tool OverallDiameter, +0x28 = op +0x2f0 =
CornerRadius, +0x30 = op_is_tip_comp == 1; arcs (plane 0) broken into round(|sweep| / sqrt(8 chord / r)) (2..20) chords at
the start point's depth, else the end + quadrant points; Z cap at each tool change: max(top of stock rec+0x2e8, depth
rec+0x1a0) when the top is absolute (rec+0x2f0 = 0; the opcode test only restricts 9 / 10 / 15 / 44 / 107 / 131 / 132 /
multisurface), applied to the running max Z at -3 / -7; end: spans <= tol (X) / 2 tol (Y, Z) widened by 2 tol. Page:
corner 0, FUN_1801edd80, Display + Fit screen. Enabled for the rectangular shape with a non-empty op list. Ours: Mastercam's
ops from their stored NCI, ops made here from their generated moves with the 1013 values of their tool (nc_post.c
nc_extents). NOT DECODED (left out): -2 +0x18 meaning (used as "active"), code 201 meaning, 4-axis arcs 32 / 33, arcs in
planes 1 / 2 or a non-Top tool view, constr_circle's start angle (full-circle arcs), the Z cap's view (Top).
**Solid / File shapes, the stock model, shaded display, Fit screen, stock plane** (decoded and ported 2026-10-22,
"Stock Setup rest"; decomp/mastercam_stocksetup.c, mccore_stock.c, mastercam_stock_fitshade.c, mccore_stl_read.c,
mastercam_multmat33.c, systypes_matmul.c, mastercam_gltol.c):
- Solid (522): button 528 = FUN_1801f0fc0: AfxSuspendWindow, select_ent(opmanres 2798 "Select an entity", mask 0x10000,
  one entity); a solid -> page +0x10338 = its ent_idn -> group +0x1268 at OK. Enabled only for shape 2 (FUN_1801ed700). Page
  load (FUN_1801f2000): an id that fill_in_ent_idn does not find as a SOLID_ID entity -> the page's shape 0. stock_ok: id > 0.
- File name on every page open (FUN_1801f2000, decomp/mastercam_stlname*.{c,txt}, mccore_filename_parts.c): prefix =
  GetFilePrefix (before the last '\\'), stl_prefix when none / dir_exists fails; suffix = GetFileSuffix (no dot) compared
  with ".stl" by _stricmp - never equal, so always ".stl"; name = GetFileName(.., 1) (extension cut), empty -> "T";
  build_filename_s(+0x1271, 0x104, prefix, suffix) = prefix + "\\" + name + ".stl", written to the group. So a stored
  "dir\\box.txt" opens as "dir\\box.stl" (and, missing, gives message 3163). stl_prefix is set at run time: NOT DECODED
  (the file is left as it is here when there is no existing directory part).
- File (523): browse 1253 = FUN_1801ef850: PromptForFileToOpen(path, "rb", stl_suffix ".stl"), then VerifyFileSuffix ("." +
  GetFileSuffix compared with ".stl") and file_exists -> CFileComboBox 364 gets the file; OK (FUN_1801ed0b0): +0x1271 = the
  combo's file, shape 3 with a missing file / other suffix -> 0. Page load: missing file -> message opmanres 3163 "Cannot open
  file: %s\nFile not found" (caption 3855 "Stock Setup"), shape 0; other suffix -> 0. stock_ok: a missing file -> message
  mccoreres 7594 "File does not exist." + "\n\n" + path (caption 7581 "Stock setup"), Display and shape reset to 0 in the
  group. NOT DECODED: the default folder (stl_prefix, set at run time; FUN_1801f2000 names an empty file "T" there) and the
  combo's list.
- STL reader (mccore get_first_stl_triangle 0x1806d1070 / get_next_stl_triangle): check_ascii_binary reads the 0x50-byte
  header and looks for "solid" / "SOLID" before any NUL: ASCII first, else binary; a failing first triangle tries the other.
  ASCII: "facet normal" (normal, leaves the loop), "outer loop" / "OUTER LOOP" / "outerloop" / "OUTERLOOP" enter, "endloop" /
  "end loop" / "ENDLOOP" / "END LOOP" leave, "vertex" / "VERTEX" + 3 atof values inside; binary: 0x50 + 4 bytes, then
  12 normal + 3 x 12 vertex floats + 2 bytes per triangle.
- Stock model (CreateShadedStockModel 0x180158770, run by stock_calc_geom; stock_model +0x80 triangles, +0x84 vertices, +0x88
  float xyz, +0x98 int indices, +0xb8.. extents): rectangle = 8 corners from the lines (+0x1380 x0y0z1, +0x1398 x0y1z1, +0x13e0
  x1y0z1, +0x13f8 x1y1z1, +0x1440 x0y0z0, +0x1458 x0y1z0, +0x14a0 x1y0z0, +0x14b8 x1y1z0) and 12 triangles {0 1 5, 0 5 4, 1 3 7,
  1 7 5, 3 2 6, 3 6 7, 2 0 4, 2 4 6, 0 2 3, 0 3 1, 4 5 7, 4 7 6}; cylinder: n = (short)(2 pi / (2 acos(1 - gl_tolerance / r))
  + 1), at least 32, n points per end circle (eval_curve at i / n), the two centres, 4n triangles (sides (i, i+n+1, i+1),
  (i, i+n, i+n+1), caps fanned from the centres); solid: sld_to_stl(gl_tolerance) into a temporary STL read back; file: the
  STL. Solid / file triangles go through P3D::ViewToWorldAdd(stock view, plane origin). gl_tolerance = 0.001 inch / 0.025 mm
  (mastercam.dll FUN_1801b4070).
- draw_stock: shaded (+0x1261) with a model -> FUN_18033fe70 only (GL_BLEND, depth mask off, per-triangle normals
  FUN_180136e60 = (p0 - p1) x (p1 - p2) normalised, ambient = diffuse = Wd_colors[mill_stock_color] alpha 0.375
  (gl_Copy_Color), specular 0.5 grey, shininess 64, FUN_180229750 GL_FRONT_AND_BACK; lights gl_Init_Lights_E NOT DECODED);
  else shapes 0 / 1 the 12 lines (+ circles), shapes 2 / 3 each triangle as one GL_LINE_STRIP p0 p1 p2 p0 in style 4.
- Fit screen (+0x1228): GetFitExtents (FUN_180320770) adds FUN_180337650 when Display and Fit screen are on: stock_ok, shapes
  0 / 1 the 24 line end points, else every model vertex (proj_iso_pt into the view).
- Stock plane (960 = FUN_1801f1d70): SelectPlaneFromList(the plane id) (Plane Selection 842); a plane -> Plane::Convert into
  the group's group_view directly (page +0x105f0 = &group +0x8a8, so Cancel keeps it): zeroed 0x68 bytes, +0 plane id, +2
  view number, +4 the plane's +0x348, +8 matrix, +0x50 origin; CheckGroupView(gv, 1, 1); the name. Every page conversion uses
  world_to_view(p - plane origin, view) / view_to_world (+ origin): load / OK (origin), FUN_1801edd80 (box corners), Select
  origin, Select corners (the Esc branch converts X / Y only and keeps Z unconverted; corner 0 hands world_to_view an
  uninitialised Z - NOT DECODED for planes whose X / Y rows use Z), the bounding shapes (FUN_1801f26d0 runs them with the
  stock plane active, ActivePlaneSaveRestore; FUN_1801f2e60 turns the box back with view_to_world(active view), so the box
  is in that view's coordinates), the cylinder origin (world_to_view(c - origin, active view)). world_to_view(p, v) = the
  view's rows . p, view_to_world the transpose, view numbers < 2 the identity (mastercam.dll 0x180341b30 / 0x180341520).
- Cylinder axis view: RotatePlane builds R from the RelativeType alone and Plane::MultiplyMatrix33(current, R) = SYSTYPES
  `operator*(current, R)` = R . current (systypes.dll 0x180002a10: result[r][c] = sum_k second[r][k] first[k][c]); R = the
  rotated Top plane (type 1 Top, 2 Front, 3 Back, 5 Right). The new view number = Matrix33::GetViewNo (a non-standard matrix
  gets a new view: NOT DECODED, such a cylinder is left out here).
- Use Machine Tree (734): CCNCMachine::UpdateStockFromMGData / the machine tree 720 (machine definition components): NOT
  DECODED, greyed.
Ported in viewer/core_ui/cui_stock.c (+ scene.c shaded model / fit hook, cui_overlay.c, cui_planes.c pls which 3,
mcam_saveops.c stock_patch: +0x1268, +0x1271 when changed, the group_view when chosen here, the regen marks' +0xfc / +0xff).
Ours: the solid pick is a ray / triangle test on this program's mesh (select_ent NOT DECODED), the solid model is this
program's mesh (sld_to_stl NOT DECODED), only the part's solids can be picked (solids made here have no SOLID_ID id), the
Bounding box panel (25451) is greyed with a non-Top stock plane, Verify uses the box of the lines for Front .. Left stock
planes (none for Iso), the shaded model uses this program's mesh shader.
NOT PORTED: the corners rubber band.

## Machine group: Files / Tool Settings fields and its machine / control (2026-10-23)
Spec: formats/grpprops_tabs.md (CGroupPg1 / CGroupPg2). The **active** machine group (the last one, see Stock Setup) holds
+4 name (char[0x105]), +0x355 machine definition file, +0x980 comment (char[2000]), **+0x1170 = the ent id (+0x34) of its
CNC_MACHINE_ID record** (ASSOC 0x4000 sub 22, n 5000), +0x1178 program #, +0x115c..+0x1168 override with modal values /
clearance / retract / feed plane (i32), +0x119c Feed Calculation, +0x11a0 / +0x11b0 / +0x1188 / +0x1180 user feed / rpm /
retract / plunge, +0x11b8..+0x11c4 assign tool numbers sequentially / warn duplicates / use tool's step, peck, coolant /
search the library, +0x11d0 material. The machine record (record = CCNCMachine - 0x70): +0x68 Type, +0xf8 / +0xfc min arc
feed (f32 inch / metric), +0x8ac control def file, +0x9b1 post, +0xab6 / +0xbbb tool libraries, +0xcc0 / +0xce7 material
library, +0xdc5 comment, **+0xe94 = the ent id of its CNC_CONTROL_ID record** (sub 23, name at +0x78). The control's pages
are the sub 24 records with ent id = control id + 1 + page and page number at +0x68 (measured in every part; the stored
link field NOT DECODED): page 2 file_ctrl (NC path +0x94, op libraries +0x4a4 / +0x5a8, op defaults +0x6ac / +0x7b0), page 8
nc_output_ctrl at +0x80 (+0 seq start, +8 increment, +0x10 maximum; +0x129 / +0x12a / +0x12b / +0x12d the NC comment
switches), page 9 NC path (+0x29e tool registers, +0xa35 arc feed flag). A part with two machine groups stores two machines
and two controls; viewer/mcam_load.c resolve_machine follows the active group's chain (before 2026-10-23 it took the first
machine record and the first page 9). The save (mcam_saveops.c grp_patch) writes only the group fields; new ops' record
+0x160 / +0x164 / +0x168 = program # / seq start / increment (OMsetOperationToMachine).

## Binary NCI tail — mccore `bnci_read_from_disk` / `bnci_bytes`
Records until EOF: `u16 size` (0 = end, max 0x6e0) + `nci_bin`: `+0 i16 gcode`, `+0xc u8 flag`, `+0x10 i32 file offset of the record`,
`+0x14 i32 byte count`. Full record size per gcode from `bnci_bytes` (includes the u16): 0/1 rapid/linear 0x4a, 2/3 arc 0x62,
4 dwell 0x32, 0x51 drill 0xb2, -2 0x2a, ... 1000-series (op/tool setup) and 2000-series records. mill template: 626 records
(312 G1, 69 G3, 18 G0, plus -2/-4/-6 and 1011..1017 per operation).
The stored bytes may be shorter than the struct: the reader zero-fills a 0x370 `nci_bin` before copying.
Field map (from `bnci_to_string` -> `nci_str_w*` formatters exported by mcwire.dll); see `tools/nci_decode.py` docstring:
- G0/G1: `+0x18 i16 cc, +0x20 xyz, +0x38 feed (-2 = rapid), +0x40 i32 contour flags (0x4000 start, 0x8000 end, 1/2 entry/exit)`
- G2/G3: `+0x18 i16 plane, +0x1a i16 cc, +0x20 end, +0x38 center` — **plane-local (u,v,depth)**; plane 0 = XY, plane 2 = XZ (lathe, verified);
  `+0x50 feed, +0x60 i32 contour flags`. Verified: 147 arcs across 5 templates, start/end radii agree.
- 1000-1002 tool change: tool# `+0x24`, rpm `+0x38` (i32), feed `+0x40`, first position `+0x50`, home `+0x68`.
- 1014 tool plane 3x3 `+0x18`; 1020 stock size `+0x18`; 20000-series text params (20001 tool name, 20016 material, 20018 feature).
- Code families: 0/1 linear, 2/3 arc, 4 dwell, 11 5-axis, 20-33 4-axis, 81/82/100 drill, 1000-1003 tool change/EOF,
  1008 op comment, 1010-1016 misc, 1018/1019 subprograms, 1020 stock, 1025 canned text.

## Tools
- `tools/mcam_walk.py FILE [-v] [--extract-xt DIR]` — framing/inventory.
- `tools/mcam_export.py FILE OUTDIR` — DXF (points/lines/arcs, layer=level, color kept), JSON summary, XT solids, thumbnail.bmp.
- `tools/render_geom.py OUT.json OUT.ppm` — dependency-free plan-view render for sanity checks.
- `tools/nci_decode.py FILE [--ascii] [--check]` — readable NCI listing; `--check` validates arc geometry.
- `tools/pe_strings.py DLL` — RT_STRING dump; `tools/pe_table.py DLL VA` — {id, char*} tables.
