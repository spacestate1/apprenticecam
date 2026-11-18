# Classic surface toolpaths, part 3: Rough Project (op 7), Finish Flowline (op 14), Finish Blend (op 109) (sc3, 2026-10-26)

Decode-only spec (nothing ported yet). Companion of formats/surf_rough.md (ops 10 / 5 / 11: its sections 1 and 4 - the common
pipeline, the Surface parameters page 999, the Cut depths / Gap / Advanced sub-dialogs - apply here unchanged) and
formats/surf_classic2.md (Multisurface Pocket / Plunge / Contour, another agent).

Citation key as surf_rough.md: **l.N** = `decomp/mcmsurf_all.c` line N; "op+X" = operation struct offset, **record = op + 0x68**
(the .mcam op record). **core:** = mccore.dll, decompiled this session from a scratch copy of ~/mastercam-re/ghidra_core
(DumpDecomp "make_raw_flowlines|msurf_flow_|flowline", depth 1; dump: `~/mastercam-re/work/sc3/flow_core.c`, see 0.3).
Defaults: `Mill_MM` / `Mill_Inch.mcam-defaults` (tools/mcam_ops.py), MM value first.
"NOT DECODED" = not read from code; "inferred" / "measured" = not from code, labelled.

## 0. Common

### 0.1 Ribbon gallery -> command -> op type (Mastercam's own data + code)
| gallery item | ribbon XML (ribbonuires IDR_BCGP_RIBBON_XML) | command | op type |
|---|---|---|---|
| 3D Roughing "Project" | ID 53878, l.10206: TEXT Project, "Project geometry or a toolpath from an earlier operation onto surfaces.", keys J | msurf_rgh_project_toolbar @180145500 (l.212766): `IsMillOpSupported(4,7)` -> `msurf_assoc(7,-1)` | **7** |
| 3D Finishing "Flowline" | ID 53892, l.10416: "Follows the shape and direction of the surfaces and creates a smooth and flowing toolpath motion.", keys FL | msurf_flowline_toolbar @180144740 (l.211670): `msurf_assoc(0xe,-1)` | **14** |
| 3D Finishing "Blend" | ID 53887, l.10341: "Create a toolpath along a surface between selected chains.", keys FB | msurf_blend_toolbar @180144580 (l.211526): `msurf_assoc(0x6d,-1)` | **109** |

(formats/ui_commands.tsv l.369 / 370 / 391 list the same command names; the gallery order is viewer/ui/layout.txt.)

### 0.2 Dispatch
- Stage 1 (FUN_18012a960, switch on ms_tp_type): **7 / 0xd / 0x2c -> FUN_18013a370** (l.197517-197524); **8 / 0xe / 0x2d / 0x6e / 0x70
  -> FUN_180044c70(D_eff / 2, r, ..)** with D_eff = D, or for tool type 0xc with r > tol: `2 max(r, D_t/2 - tan((90 - A)/2 deg) r + r)` (D_t, A = the tool record's
  +0x3f8 / +0x20 values from tool_manager; what they are is NOT DECODED)
  (l.197525-197545); **0x6d -> [EMM pre-pass] + FUN_18013a370** (l.197756-197779, see 3.2).
- Stage 2 (FUN_18012e440 l.199437-199484): **7 / 0xd / 0x2c / 0x6d -> FUN_180139c80** (l.199452); **8 / 0xe / .. -> FUN_180044830** (l.199461).
- fzt / emm (bnci_surface l.11964-12045): fzt is cleared for all three (only 9 / 10 / 0xf / 0x6b / 0x83 keep it, l.11964-11968). emm stays
  on only with stock < 0 and |stock| > r, the tool types of mask 0x223000 / 0x19 / 0x17 / 0xe-with-chamfer, or msurf_use_emm_project /
  _blend (l.12000-12022); those flags default to 0 (read_appmch_cfg l.18495-18496), op 14 has no such flag. **Default path: fzt = 0, emm = 0**
  -> the machine model is `build_machine_model(kind 1, ..)` (FUN_18012a310 l.197399-197416), the offset-surface model of op 5
  (surf_rough.md 3.2; make_surf_model NOT DECODED).
- msurf_map (l.339935): common block l.340009-340048 (stock 0x2e0 l.340009, chk 0x1230 l.340015, tip comp 0x1239, chord 0x1240, zdist
  0x1248 l.340018, gap block 0x1288-0x12a6 l.340020-340028, corner dev l.340047, directions 0x1250-0x1280 except op 10), per-op cases below; the short-gap % rule l.340443-340480 (op 0x6d:
  max short gap = % x D, l.340445-340448; op 7: slice dist x %, l.340474 - overridden in stage 2 by % x D, 1.3).

### 0.3 Scratch
`~/mastercam-re/work/sc3/` (session scratch - copy it to `~/mastercam-re/work/sc3/` to keep it; this agent was told to write only this file): flow_core.c (mccore make_raw_flowlines + 20 callees), the default op records `*_op{7,13,14,109}.bin`, dump.py.

## 1. Surface Rough Project (op 7) - mostly decoded at the pipeline level; the drop solver and the per-level clip are NOT DECODED

### 1.1 Record fields (op 7)
| record | op+ | meaning | default MM / Inch | source |
|---|---|---|---|---|
| 0x2e0 | 0x278 | stock to leave on drive | 0 / 0 | msurf_map l.340009 |
| 0x34c / 0x350 / 0x358 | 0x2e4 / 0x2e8 / 0x2f0 | tool type / D / corner r | 19 bull 20 r4 / 11 ball 0.5 r0.25 | stage 1 l.197522 |
| 0x1230 | 0x11c8 | stock on check | 0 / 0 | l.340015 |
| 0x1239 | 0x11d1 | tip comp (1 = Tip, surf_rough 3.4) | 1 / 1 | l.340016 |
| 0x1240 | 0x11d8 | total tolerance (msurf_chord_dev; < 1e-10 -> nc_tol) | 0.025 / 0.001 | l.340017, l.340492 |
| 0x1248 | 0x11e0 | max stepdown (rough_zdist; < 1e-10 -> 0.05) | 2 / 0.1 | l.340018, l.340498; page 1037 |
| 0x1288 / 0x1290 / 0x1298 / 0x12a0 | 0x1220.. | gap: % flag / max short gap / % / type ((v%100)%10) | 1, 2.5, 3, 1 / 1, 0.01, 3, 11 | l.340020-340027 |
| 0x12e8 / 0x12e9 / 0x1300 / 0x1308 / 0x1310 / 0x1318 | 0x1280.. | depths: incremental / (DAT_1804d848d) / top clearance / bottom stock / min / max | 1, 1, 0.2, 0.2, 0, -10 / 1, 1, 0.01, 0.01, 0, -1 | LAB_180206ec4 l.340385-340391 |
| 0x13d0 / 0x13d1 / 0x13d2 | 0x1368 / 0x1369 / 0x136a | allow negative Z / allow positive Z / plunge control (0 multiple, 1 one side, 2 both) | 1, 1, 0 / 1, 1, 0 | l.340393-340397 |
| **0x13d4** | 0x136c | **projection type** DAT_1804d7260: 0 NCI, 1 Curves, 2 Points | 0 / 0 | case 7 l.340087; page 1037 radios 834 / 837 / 841 |
| **0x14e8** | 0x1480 | **source operation id** (one op) | 19 / 0 (stale in the defaults file) | l.206617; page OK l.158015 |
| **0x1508** | 0x14a0 | **retract between cuts** (forces gap type 4 = retract) | 0 / 0 | l.417499-417503, l.431844-431859 |
| 0x14d6 | 0x146e | forced 0 by msurf_map case 7 (l.340088); meaning NOT DECODED | 0 | |

### 1.2 Stage 1: the source -> MSPROJ segments (FUN_18013a370 @18013a370, l.206555)
1. **NCI source (0x13d4 = 0)**: the op at 0x14e8 is retrieved (FUN_1802287f0 -> TpPartOpList::DatabaseRetrieve, l.362964); it must
   exist and its op struct byte +0xfc must be 0 (l.206619-206620; meaning NOT DECODED), else error -0xe (l.206666). Its binary NCI is
   converted to text NCI `msproj.nci` (msurf_to_nci_op -> bnci_to_nci_op, l.206626 / l.202318) and read by **FUN_18012cf00**
   (l.198628): line pairs (code, values); codes 0 rapid / 1 line / 2-3 arc, 1013 (0x3f5: view number + origin offset, l.198993),
   1014 (0x3f6: a 3x3 matrix into the scratch view, l.199041). Every position goes view -> world -> the projection frame (dot with the
   frame rows). **A rapid after cutting ends the cut chain** (point flag -2, l.198824); the first feed after a rapid starts the chain
   at the rapid's end point when that differs in XY by > tol (l.198833 / l.198886); arcs become entities (nci_to_ent l.198960) broken
   into points (FUN_18013f080 l.198984). The points are then filtered (MSFILTER.TMP, l.199078-199111): **Z is zeroed for op 7**
   (`uVar17 = 1` l.199104 -> FUN_18012f190 clears +0x10, l.199982; only op 0xd with op+0x139a keeps Z), so only the XY of the source
   path is projected; the buffer is flushed (FUN_18012f670) when its deviation (FUN_18012f2d0) exceeds msurf_chord_dev or it holds 500
   points (l.199089-199090, l.199989) - the reduction itself NOT DECODED. Result: MSPROJ.TMP segment records **0x38 bytes {p0 xyz, p1
   xyz, int -1, short flag}** (l.199131).
2. **Curves (1)**: FUN_18013eaf0(1, ..) saves the frame and sets it to identity (l.208638), **FUN_18012ca70** (l.198454) reads the
   selected chains from the containment file `tmptc.tmp` (GetTcbFileInfo, l.198519), takes each chain point's frame XY, writes a
   segment (z = 0) whenever |dx| or |dy| > curve_chord_dev (l.198563), closes closed chains (l.198595); then the frame is restored.
3. **Points (2)**: **FUN_18012dfd0** (l.199179): every selected point entity (get_ent mask 0x40, l.199248) -> one segment from (x, y)
   to (x + 10 chord, y) (l.199262).
4. Frame (l.206671-206731): the active tool plane rotated by 90 deg about its Z (cos / sin of pi/2 rows) - the same rotation as the
   Parallel frame at angle 90 deg (surf_rough 8 "Frame"). FUN_18025dd50 (l.399269) reuses the cached machine model (tmpmm3) when only
   that 90-degree turn differs (it rotates the stored records), else rebuilds it (FUN_180261f40) - plumbing, no geometry choice.
5. **Projection**: `FUN_180290fb0(1, stock, tool type, r, ..)` (l.206776; @180290fb0 l.428828, 968 lines). Read from it:
   - tolerances (l.429030-429040): a60 = 0.9 chord, a68 = (chord - a60) / 2, a70 = 0.01 a60, a78 = min(tol, 0.0001 a60); corner
     tolerance a80 = 0.9 corner_dev when corner_dev <= chord, else corner_dev - 2 a68 (msurf_corner_dev, msurf_map l.340047 / 340482);
   - tool (l.429071-429095). The double arguments travel in XMM registers and Ghidra shows them as in_XMM1 = **stock** (the 2nd
     argument, `param_3` at the call l.206776), param_4 = **r**, param_5 = **D** (this reading is inferred from how the body uses
     them; the call site's argument list is truncated by the decompiler): stock' = max(stock, -r); **tube = r + stock'** (0 when <= a70);
     **flat radius = (2 stock' + D) / 2 - tube = D/2 - r** (0 when <= a70, the tube then = stock' + D/2); check surfaces chk' = max(chk,
     -r) (l.429078) - the same tool model as op 10 (surf_rough 2.3);
   - corner discretisation: n = ceil(pi / (4 acos(1 - a80 / R))) + 1 per radius (l.429102);
   - then **per machine-model surface** (get_mmsurf loop from l.429255): FUN_180295ce0 (l.429353) or FUN_180294690 (l.429370) compute
     the tool positions over the MSPROJ segments, merged into tmpedg2 by FUN_180288510 (l.429443); a08 = tube - stock' = **r** for drive
     surfaces (l.429308), tube - chk' for check surfaces (l.429269), r for tool types 0x19 / 0x17 (l.429611); results to tmpsurf.tmp with a
     header {frame (3 x 24), a60, a68 / 5, a70, a78, D, a08, count} and the segments (l.429598-429650); FUN_180229640(-0x38, 8, 0x1e) for
     op 7 (l.429657, meaning NOT DECODED).
   **The segments are 2D (XY) and the frame's Z is the tool axis: the projection is a drop along the tool axis** (inferred from the
   segment layouts above - the per-surface contact solvers FUN_180295ce0 / FUN_180294690 / FUN_180288510 are NOT DECODED).

### 1.3 Stage 2: levels, links, NCI (FUN_180139c80 @180139c80, l.206254)
- Level range (l.206340-206356; l.206341 / 206352): absolute -> top = max(min depth, max depth), bottom = the smaller; incremental -> top clearance /
  bottom stock (resolved later). `FUN_1802906c0(levels_on, posz, negz, plunge, inc, top, bottom, zdist)` (l.428420): posz / negz
  coded 2 allowed / 1 not; absolute: R = top - bottom, st = max(zdist, 2 a60 (= 1.8 chord), tol, 1e-10) (l.428439); R > st -> n =
  max(1, int(R / st)), +1 when R / n > st, step = R / n (l.428461); else step = max(R, 1.8 chord, tol, 1e-10).
- Incremental (FUN_180296cf0 l.432097, called from FUN_180293f40 l.430608): scans the projected points (flag < 0x24) for min / max Z;
  bottom = bottom stock + min Z, top = max(top, bottom) (end of FUN_180296cf0); the top's assignment from max Z is lost in the
  decompile (NOT DECODED); same step rule.
- Path (not the EMM branch, l.206369-206379): **FUN_180293f40** (l.430485) reads the tmpsurf header (l.430534..): its 5th read lands
  the header's **D** in a08, so **max short gap = % x D when the % flag is on** (l.430545; the reader's order vs the writer's
  l.429598-429615), then the real a08 (= r). Tip comp with absolute depths adds a08 = r to top and bottom (l.430603). **Level loop**
  (l.430614-430635): L = top, P = 0, first = 1; while bottom - 0.4 step <= L: L = max(L, bottom); FUN_180289ff0 (one pass over all
  projected chains); plunge control 2 -> a second pass with ac3 = 1; P = L; first = 0; L -= step. Levels off (the finish op 0xd):
  one pass (l.430597).
- **FUN_180289ff0** (l.424618): chains in file order (reverse when param_4); per chain the stored profile records **0x18 bytes {along,
  z, short flag}** (flags >= 0x27 carry a move type = flag - 0x27, l.424831 / 424947); second-pass look-ahead (ac3): a chain is stopped when a
  point is above P + a78 (not the first level, l.424792), or with positive Z not allowed it rises / with negative Z not allowed it falls
  by more than a78 (l.424801 / 424812); the per-level clip and output is **FUN_18027b520** (l.417258, called l.425019; FUN_180296360
  l.431698 for point-type sources, l.424983) - NOT DECODED beyond its links: between two pieces, XY gap <= a70 and 3D gap <= a70 ->
  nothing; XY <= a70 -> FUN_180141720 (l.417483-417489); else **FUN_180140d80(.., max short gap, type)** with type = msurf_short_gap,
  or **4 (retract) when record 0x1508 is set** (l.417499-417503; l.431844-431859 in FUN_180296360); FUN_180140d80 = the Parallel
  port's long-gap rule (surf_rough 3.3).
- Gouge check and output: FUN_180226c30 (exact model) + `FUN_180290fb0(2, ..)` on the gap moves (l.206493-206518), then
  FUN_180136670 (l.206385) (the NCI writer, NOT DECODED, as surf_rough 1.4).

### 1.4 Dialog (CSurfaceSheet ctor l.450196-450210 (case 7): tab 3 = CMsProjectRoughPg, dialog **1037** "Project parameters", tab string 138
"Rough project parameters")
Init FUN_1800f5740 (l.157154) / FUN_1800f5cf0 (l.157369), OK **FUN_1800f6c20** (l.157978):
| control | record | note |
|---|---|---|
| 25458 total tolerance | 0x1240 | + the filter tolerance 0x320 when 0x33a (as op 10; init l.157160-157167, OK l.157991-158005) |
| 920 max stepdown | 0x1248 | l.157177 / 158012 |
| 834 NCI / 837 Curves / 841 Points | 0x13d4 (radio index) | l.157171 / 158006; 1054 + tree 997 "Source operations" active only for NCI (FUN_1800f5200 / FUN_1800f53d0) |
| 997 source-op tree | 0x14e8 | selected op (FUN_1800f5530), 0 when not found (l.158013-158022); with no ops the type is forced to Curves (l.157403) |
| 879 / 883 / 888 plunge control | 0x13d2 | radio index l.157172 / 158007 |
| 894 negative Z / 900 positive Z | 0x13d0 / 0x13d1 | l.157173-157174 / 158008-158011 |
| 1180 retract between cuts | 0x1508 | l.157180 / 158025 |
| 664 Cut depths / 904 Gap / 906 Advanced | as surf_rough 4 | |

### 1.5 NOT DECODED (op 7)
The per-surface drop (FUN_180295ce0 / FUN_180294690 / FUN_180288510) and the offset model it runs on (make_surf_model); the call-site
argument list of FUN_180290fb0 (the stock / r / D reading of 1.2 is inferred); the point-reduction filter (FUN_18012f2d0 / FUN_18012f670); the incremental top; the per-level
clip FUN_18027b520 and the short-gap motions (as op 5); the NCI writer; op+0xfc of the source op; op+0x146e.

### 1.6 Oracle candidates
**None with NCI**: no stored op 7 in any corpus (~/mastercam-re/work/hst3d/netscan/scan.tsv: no opcode 7 / 13; PROJECTBLEND.mcam op 7
is a Rough **Pocket**, opcode 10). Self-checks only: project our own 2D Contour / Pocket toolpath onto BRACKET4 and compare against the
drop of the same XY path (the identity case: a flat drive at Z = c must give the source XY at c + stock).

### 1.7 Port outline (viewer/cam3d_srough.c)
- `gen_srough_project`: `sr_record` (the stored or default op 7); refuse 0x13d4 = 2 / 1 until the point / chain pick exists (or take
  the op's chains as cam_cli does for containment), refuse the emm conditions (0.2) and check surfaces.
- Source: the source op's computed moves in this program (CamOp by id 0x14e8, rapids split chains, arcs sampled at curve tolerance,
  Z dropped) instead of the text NCI; reduction of collinear points: ours (FUN_18012f670 NOT DECODED).
- Drop: `c3_mesh_op` + `sr_hf_build` / `sr_hf_tip` (the op-5 height field of the stock-inflated tool: Rb = r + stock', Fo = R - r) at
  segment samples every cut tolerance (ours, as op 5).
- Levels: the 1.3 rule (write it as a sibling of op 5's FUN_1802022d0 code - the step clamp is the same shape); per level reuse
  `sr_band_split` / `sr_state` **only as an ours stand-in** (FUN_180289ff0 / FUN_18027b520 is a different, undecoded state machine; the
  posz / negz / P + a78 tests of 1.3 are what is decoded); links: op 5's long-gap retract, retract-between-cuts -> always retract.

## 2. Surface Finish Flowline (op 14) - structure and dialog decoded; the line generator is a large mixed mcmsurf + mccore family, partly decoded

### 2.1 Record fields (op 14; msurf_map case 0xe l.340226-340236, then the z-limit block l.340427-340440)
| record | op+ | meaning | default MM / Inch | source |
|---|---|---|---|---|
| 0x1240 | 0x11d8 | total tolerance | 0.025 / 0.001 | common |
| **0x1300** | 0x1298 | cutting method msurf_cutting_method: 0 Zigzag, 1 One way, 5 Spiral (combo 833 rows 313 / 314 / 316; row 2 stored 5) | 0 / 0 | l.340228; FUN_1800e2480 l.146772 (fill), OK l.146682-146685, init l.145930 |
| **0x1302** | 0x129a | cut control "Distance" on (864); off -> step along = 0.9 chord | 0 / 0 | l.340229 (bf6c8 = byte == 0), use l.45406 |
| 0x1308 | 0x12a0 | cut control distance (760, stored abs) | 2 / 0.01 | l.340230 |
| **0x1310** | 0x12a8 | stepover by distance (1) / scallop height (0) (radio 764 / 766) | 0 / 0 | l.340231 (bf6c9 = byte == 0); page int = (byte == 0) |
| 0x1318 | 0x12b0 | stepover distance (767, abs) | 2 / 0.01 | l.340232 |
| 0x1320 | 0x12b8 | scallop height (768) | 0.5 / 0.002 | l.340233 |
| 0x1328 | 0x12c0 | check flowline motion for gouge (778) | 1 / 1 | l.340234 (DAT_1804e6373) |
| 0x1340 | 0x12d8 | flow data short (DAT_1804e637c): != 0 -> FUN_180045e60(value) | 0 / 0 | l.340236; l.45420-45421 |
| 0x1342 | 0x12da | flow data flags (DAT_1804e6378): bit 0 -> FUN_180044510 / FUN_180040a90 direction argument, bit 1 -> FUN_180044a10 | 0 / 0 | l.340235; l.45418 / 45429 |
| 0x1348 | 0x12e0 | a distance set by the interactive flowline dialog (FUN_180045b20 l.45869); meaning NOT DECODED | 0 / 0 | |
| 0x1350 | 0x12e8 | single row only (1147) | 0 / 0 | OK l.146687 |
| 0x1360 / 0x1362 / 0x1368 | 0x12f8 / 0x12fa / 0x1300 | rib cut count (MCInt 1..32000; read as local_1930 in FUN_18003cd50 l.41609-41612) / rib cut (686) / resolution % of tool diameter (691, stored / 100) | 1, 0, 0.5 | OK l.146688-146692 |
| 0x12e8 / 0x12e9 / 0x12f0 / 0x12f8 | 0x1280 / 0x1281 / 0x1288 / 0x1290 | depth limits on (944) / relative to the tool plane origin (adds the tool plane's Z, l.340430-340440) / limit 1 / limit 2 | 0, 1, -10, 0 / 0, 1, -1, 0 | l.340427-340429 |
Levels: case 0xe breaks before LAB_180206ec4, so DAT_1805034a2 = 0 (no levels; posz / negz / plunge stay 1 / 1 / 0, l.339995-339997).

The flow data (0x1340 / 0x1342 / 0x1348) is chosen in the interactive "flowline data" step: set_flow_surf @18001d270 (op 14 uses
op+0x12da / 0x12d8, l.21015-21016), FUN_180045b20 (the arrows dialog; it writes 0x1348, l.45869). Which bit is "cut direction u / v", "step
direction", "start corner", "offset side": NOT DECODED (needs FUN_180045b20 / FUN_180040a90 / FUN_180045e60 read through).

### 2.2 Algorithm (single surface; FUN_180044c70 @180044c70, l.45244)
1. Frame: the tool plane turned 90 deg (l.45307.., as Project).
2. Drive surfaces from the op (FUN_180226970, l.45364). **More than one surface -> msurf_flow** (l.45385; @ l.50801, 929 lines;
   with the 0x108 / 0x40 product bits it needs mill / router level >= 6, else warning 0xf3, l.45378-45382) - NOT DECODED. One surface
   -> 3..8.
3. FUN_180045440 (l.45490): for tool type 0xe returns D - 2 r (the flat, l.45514), else 0.
4. FUN_1800448f0 (l.45401) -> FUN_18003c360 (l.40933) with comp-to-tip and the display off: the four boundary passes flowuup / flowudw / flowvup /
   flowvdw.tmp, each a FUN_18003cd50 run (the edges of the surface in u and v, both directions).
5. Step along = 0.9 chord, or the cut control distance (0x1302) (l.45406); stepover = the distance (0x1310 = 1) or the scallop
   height (l.45413). **FUN_180044510** (l.45418; @ l.45049) -> FUN_180040a90 (l.45094; @ l.43413) -> **mccore make_raw_flowlines**
   (l.43494 / 43573 / 43767; core: @18058bf80):
   - the base surface (find_base_surface) and its evaluation grid (get_evaluation_grid: the knot spans; none -> one span);
   - per span: FUN_180581f00 (core) evaluates S, Su, Sv at the span middle; **du = param_4 / |Su|** (clamped to [1e-5, span / 3];
     with its mode flag set: span x 0.05) and dv = param_5 / |Sv| the same way (param_4 / param_5 = the two steps - which is the
     along-cut step and which the stepover follows the direction bit, NOT DECODED) (core: flow_core.c FUN_180581f00 body);
     FUN_1805851e0 (core) is the curvature (chord / scallop) step - its curvature solve FUN_180585d10 NOT DECODED;
   - per span n = ceil(span / d) divisions (make_raw_flowlines, the `ceil(.. / ..)` loops); FUN_18058e2a0 merges neighbouring single
     divisions (mode != 1), FUN_18057e630 for mode 1 - the exact equalisation NOT DECODED;
   - the u / v parameter lists are written to flowitu.tmp / flowitv.tmp (count, n + 1 doubles, span table, shorts; l.43500..).
   The modes passed (uVar14 / uVar10, l.43473..: 0 / 1 / 2, their meaning NOT DECODED) are read from bf6c8 / bf6c9 swapped by
   the direction argument; the scallop-height -> stepover formula sits in FUN_1805851e0 / its callees: NOT DECODED.
6. **FUN_18003cd50** (l.45111; @ l.41246, 1374 lines) walks the lines: for each line parameter it samples the other parameter
   (trimmed surfaces: the line is cut by the trim loops with hit_polygon, l.41790 / 41836); per sample (l.42177-42345):
   P = S(u, v), N = the unit normal (calc_surf_unit_norm; flipped when ent +0x6e bit 2 "normal reversed", l.41658, XOR the offset-side argument);
   **C = P + rho N** (l.42241) with rho = local_1788 (param_6 on the degenerate branch; that it is r + stock is inferred - the
   caller's arguments are lost), then + F unit(N - (N.A) A) (F = local_1940, inferred the flat radius; A = the tool axis, l.42250-42275);
   **tip comp: C -= param_7 A** (l.42280; param_7 inferred r); with param_8 > 1e-6 (l.41503): C -= param_8 A when N.A < 0, C -=
   param_8 / 2 A when |N.A| < 1e-6 (l.42304..; param_8's source NOT DECODED). So Flowline is a
   **normal-offset (contact point) toolpath, not a drop**.
   Cutting method 5 (Spiral) has its own branch (l.41729) - NOT DECODED. The count 0x1360 is read at l.41609 (rib cut).
7. FUN_180043530 (l.44583) joins the line files (flowit -> flowit2, end marker -100); FUN_180045e60(0x1340) (l.45421; @ l.45898, 397
   lines) NOT DECODED; FUN_180046950 (l.45423; @ l.46347, 805 lines) NOT DECODED (where the zigzag / one-way order and the 0x1328 gouge
   check happen is not established).
8. FUN_180044a10 (l.45429; @ l.45182): flowit.tmp -> flow.tmp (l.45207); with no levels, gouge check off and param_4 < 1 ->
   FUN_18003cba0(2 x param_5, param_6, stock) (l.45217); else FUN_18003c9d0 in the construction plane (l.45235).
9. Stage 2 FUN_180044830 (l.45125): the depth block (absolute only; levels off for op 14) -> FUN_18007d600 (l.45152), then
   FUN_180041d90 (l.45154; @ l.43857) - NOT DECODED.

### 2.3 Dialog (sheet ctor case 0xe l.450308: tab 3 = CMsFlowFinishPg, dialog **1041** "Flowline parameters", tab string 117 "Finish flowline parameters")
DDX FUN_1800e0620 (l.145795): 1147 single row (+0x4d8), 864 cut-control distance check (+0x2350), 833 method combo (+0x2428), 778 gouge
check (+0x2500), 767 / 760 / 768 / 25458 MCReal (+0x26b0 / +0x34b0 / +0x42a8 / +0x50a0; value at +0xc00), radio 764 (+0x34a8), 944
(+0x5e9c), 686 rib cut (+0x5ea0), 691 resolution (+0x6ce0). Init FUN_1800e0980 (l.145896), OK **FUN_1800e1f20** (l.146647). Rows: table 2.1.
Sub-dialogs: 762 Depth limits (the z limits), 904 Gap settings, 906 Advanced settings (surf_rough 4). Flowline direction: the
"flowline data" step at selection time (set_flow_surf), not a page.

### 2.3a The scallop / curvature step, partly read (2026-10-28; mccore via ~/mastercam-re/ghidra_core, DecompAt; flow_core.c)
- **FUN_1805851e0** (flow_core.c l.742-964): over the surface domain (record +0x70 / +0x78 u range, +0x80 / +0x88 v range) split into
  param_5 x param_6 equal spans, for every span cell meeting the window [param_9, param_10] x [param_11, param_12]: eval_surf mode 2
  (with derivatives) at (u + du / 3, v + dv / 3) and mode 0 at (u + 2 du / 3, v + dv / 3) / (u + du / 3, v + 2 dv / 3) (the two
  distances it computes are discarded), then FUN_180585d10(eval, direction (1, 0), du / 3, 0) and (0, 1), dv / 3; per u span the largest
  ratio 3 x a / b is kept in param_15 (and per v span in param_16) with its pair - the per-span step data make_raw_flowlines divides by.
  Esc aborts (KeyPress 0x1b).
- **FUN_180585d10(eval, dir, h, k, a, b, ..)** @180585d10: |S_k| x h (the span's chord along the direction) against two limits a / b:
  outside the band (b < a, the chord < a, more than 40 % below b or 200 % above it) -> FUN_180589880 (a sampled search, not read) and
  FUN_1805869d0(curvature, a); else the curvature from surf_curv_dir(eval, dir) / curv(S_k, S_kk) (the larger when |S_k| > a) through
  FUN_1805869d0, at least 2 a; the ratio max(b, chord) / that value is kept when larger. FUN_1805869d0 (presumably the scallop -> step
  formula), FUN_180589880, surf_curv_dir and curv are NOT DECODED; the decompile of FUN_1805851e0's two calls passes fewer arguments
  than FUN_180585d10 takes (lost in the decompile).

### 2.4 NOT DECODED (op 14)
Multi-surface msurf_flow; the scallop-height and curvature step (FUN_1805851e0 / FUN_180585d10); the span equalisation (FUN_18058e2a0 /
FUN_18057e630); the meaning of the flow-data bits (0x1340 / 0x1342 / 0x1348); Spiral; the line ordering FUN_180045e60, the linking /
gouge pass FUN_180046950, the z-limit / NCI stage FUN_180041d90; param_8 of FUN_18003cd50; the rib cut.

### 2.5 Oracle candidates
- **PROJECTBLEND.mcam op 12** (2017 Handbook Vol. 2; also the X9 .mcx-9 op 12; the 2018 copy has 0 moves), Finish Flowline, **474 moves**
  (scan.tsv): ball D 0.25 r 0.125, tolerance 0.001, cut control by tolerance (0x1302 = 0), **stepover by scallop 0.002**, Zigzag, gouge
  check on, flow data 0x1340 = 2, 0x1342 = 1, inch. Its drive-surface count / kinds were not checked (mcam_ops.load returns only ents,
  ops - read the TP refs with tools/mcam_walk.py).

### 2.6 Port outline
- `gen_sfinish_flowline` (single surface only; refuse > 1 surface = msurf_flow): `mcs_surf` / `mcs_surf_domain` / `mcs_surf_eval`
  (viewer/mcsurf.c) for S; Su / Sv / N by central differences (the mcsurf.c convention, surfaces.md 2 "port notes"; ours); the trim
  loops of kind 10 (mcsurf.c `trim_loops`, and the trimmed-wireframe clipping already ported from FUN_180238ee0) to cut the lines.
- Line parameters: per evaluation span du = step / |Su| clamped to [1e-5, span / 3], n = ceil(span / du) (decoded); refuse scallop
  stepover (default!) until FUN_1805851e0 is decoded, or label it ours.
- Tool position: C = P + rho N + F unit(N_perp), tip comp - param_7 A (the form is decoded, l.42241-42280; rho = r + stock, F = the
  flat radius, param_7 = r are inferred).
- Refuse Spiral, rib cut, gouge check on (the default!) or run without it labelled "gouge check NOT DECODED", any 0x1340 != 0 / 0x1342
  bits until decoded; zigzag / one-way order: ours.

## 3. Surface Finish Blend (op 109) - fields and dispatch decoded; the generator is the "lm" sub-engine, NOT DECODED

### 3.1 Record fields (op 109; msurf_map case 0x6d l.340400-340412; lm parameters FUN_1800caff0 l.133450-133473)
| record | op+ | meaning | default MM / Inch | source |
|---|---|---|---|---|
| 0x1240 | 0x11d8 | total tolerance | 0.025 / 0.001 | page 1040 |
| 0x1300 | 0x1298 | forced 3 by the page OK | 3 | FUN_1800d4720 l.139635 |
| **0x1420** | 0x13b8 | **max stepover** (1066); < tol at init -> 1.2 mm / 0.05 in (l.139115-139125); lm +0x10 (<= lm tol -> 0.05, l.133470-133473) | 1.2 / 0.05 | |
| **0x1428** | 0x13c0 | cutting method (993: rows 313 Zigzag, 314 One way, 316 Spiral stored as the row index 0 / 1 / 2) | 0 / 0 | FUN_1800d40f0 l.139385-139401, OK l.139637 / 139641 |
| **0x142a** | 0x13c2 | **Along** (1) / Across (0) (radio 1199 / 1200; the 0/1 order is inferred from the radio group order) | 1 / 1 | l.139130 / 139642; lm +0x40 |
| **0x142b** | 0x13c3 | **2D** (1) / 3D (0) (radio 1138 / 1139: page int = (byte == 0), radio index 0 = 2D - inferred order) | 0 / 0 | l.139131 / 139643; lm +0x44 |
| 0x1438 | 0x13d0 | blend-along stepover fraction ("% of stepover" 1194 / 100; 0 -> 100 %); slice dist = 0x1420 x it | 1 / 1 | dialog 1007 init FUN_1800d3fb0 l.139337-139343; msurf_map l.340404; lm +0x20 (< tol -> 1.0) |
| 0x1440 | 0x13d8 | blend-along 3D projection **Quick** (1) / Thorough (0) (1195 / 1196; inferred order); lm +0x50 | 0 / 0 | l.139336 |
| 0x12e8 / 0x12f0 / 0x12f8 | 0x1280.. | depth limits (944) / limits | 0, -10, 0 / 0, -1, 0 | init l.139129, OK l.139638 |
msurf_map case 0x6d: DAT_1804d7260 = 3, msurf_slice_dist = min(0x1420 x 0x1438 (or 0x1420 when 0x1438 < tol), FUN_1802465b0(D, chord,
0x1420)) (FUN_1802465b0(a, c, f) = 2 sqrt((a - c) c), or f when that is not > 0: l.383345, decoded in 9.1), msurf_colaps_res_pct = 0.8, angle 90 deg; max short gap % x D (l.340445-340448).

### 3.2 Dispatch and algorithm
- **Along + 3D + Thorough** (0x142a = 1, 0x142b = 0, 0x1440 = 0) and no check surfaces (FUN_180268e10 = !DAT_1805034a1, l.405199)
  - **the Mastercam default op and every corpus op** - stage 1 first runs **FUN_180133f10(0xb, 1, D, r)** and **FUN_180133f10(10, 1,
  ..)** (l.197756-197777; FUN_180133f10 @ l.202694, 974 lines, the shared engine of ops 0x27-0x2b Pencil / Leftover / Steep / Shallow /
  Scallop), then FUN_18013a370; stage 2 takes **FUN_180278890** (l.206376-206379, the EMM path) instead of FUN_180293f40.
- Otherwise stage 1 FUN_18013a370 with type 3 -> **FUN_18002fd90(3)** (l.206607-206612; @ l.32738): the **lm** engine: FUN_1800cc7b0(0x6d)
  context (l.134118: frame = the tool plane turned 90 deg, a 0x78 block for 0xd / 0x6d, l.134276-134293), FUN_1800caff0 (l.133416:
  the op values above + tolerance, feed, D, r, stock), FUN_1800c0e10 (the chains), FUN_1800c8730 stages 2 / 3, FUN_1800307f0 /
  FUN_180030080 (-> FUN_18028e8b0, the projection family) / FUN_1800bbef0, errors through lmOpError. The result goes to the same
  tmpsurf / projected-chain files that FUN_180293f40 reads (no levels: case 0x6d breaks before LAB_180206ec4). **All of it NOT DECODED.**
- Selection: two chains (+ drive surfaces); how they are read (FUN_1800c0e10 / FUN_180032cb0): NOT DECODED.

### 3.3 Dialog (sheet ctor case 0x6d l.450472: tab 3 = CMsBlendFinishPg, dialog **1040** "Blend parameters", tab string 142 "Finish blend parameters")
DDX FUN_1800d3810 (l.139037): 1066 stepover (MCReal +0x4d8), 25458 tolerance (+0x1490), 944 (+0x2288), 993 combo (+0x2290), radio 1199
(MCRbutn +0x2368). Init FUN_1800d3990 (l.139094), OK **FUN_1800d4720** (l.139608). "Blend..." 909 -> dialog **1007** "Blend along cut
settings" (init FUN_1800d3fb0 l.139326: 1194 %, 1189 distance = stepover x % (FUN_1800d4320 / FUN_1800d42d0 keep them in step), 1198
max stepover, 1195 / 1196 Quick / Thorough, disabled (1195 / 1196 / 1159) when 2D); its OK writer was not found (a jumptable /
copy): the 0x1438 / 0x1440 store is NOT DECODED.

### 3.4 Oracle candidates (all on the undecoded EMM path)
TUTORIAL 2 SWARF.mcam op 2 (2017 Multiaxis Advanced, inch, ball 0.375, stepover 0.025, One way, Along, 3D, Thorough: 41 973 moves),
TUTORIAL 2 SWARF_METRIC.mcam op 2 (10 mm ball, 0.5, 60 687 moves), MOLD BOTTLE.mcx-9 op 3 (17 162; 0x1438 = 0), CONVERT TO 5
AXIS.mcx-9 op 10 (41 859, Zigzag). No Across / 2D / Quick op exists.

### 3.5 Port outline
**Superseded 2026-10-26: formats/blend.md** - the lm pipeline (FUN_180031990 / break_chain_to_points / make_loft_surf / FUN_180030fb0) is decoded there and ported (viewer/cam3d_blend.c; oracle tools/tests/blend_test.sh: p95 <= 1 tol on all 4 corpus ops); the text below is the earlier status.
Not portable from code yet: both Blend branches are undecoded engines (FUN_180133f10 + FUN_180278890 for the default; the lm engine
FUN_18002fd90 for the rest). Next decode targets: FUN_180133f10 (shared with five more finish ops - highest value), then FUN_1800c0e10 /
FUN_1800c8730 (the lm chain blend). Until then: the dialog page (3.3) and refusing generation.

## 4. Biggest gaps (all three)
1. The projection solver FUN_180290fb0's per-surface contact (FUN_180295ce0 / FUN_180294690 / FUN_180288510) and its offset-surface model
   (make_surf_model, mccore) - Project and every projected finish op; the port stands in with the height-field drop.
2. The projection family's per-level clip / links FUN_18027b520 and the NCI writer FUN_180136670.
3. Flowline: scallop stepover and curvature step (FUN_1805851e0 / FUN_180585d10), the flow-data bits, linking FUN_180046950,
   multi-surface msurf_flow.
4. Blend: everything below the dialog (FUN_180133f10 EMM path, lm engine).

## 4. Port: Rough Project op 7 (2026-10-26, viewer/cam3d_srough.c gen_srough_project)
- NCI (the source op's cut polylines, XY) and Curves (the op's chains); levels, step rule, level loop, the "cut by an earlier level" skip
  (P + a78), the gap links (a70, max short gap = % x D or 0x1290, retract between cuts) as 1.2 / 1.3.
- Refused: Points; plunge control other than multiple; negative / positive Z not allowed. Ours: the drop (op 5's height field), the source
  as this program's toolpath points, Curves without containment, the incremental top (op 10's rule), the short-gap motion.
- Page 1037 (opdlg_fields.tsv s1037), the source list as HST Project's 997 (a stale default id that is an op of the part is preselected, as
  Mastercam's tree would). Scene gen_srproject (BRACKET4, source op 2). **Unverified** (no stored op 7 anywhere).

## 5. Surface Finish Radial (op 12) and Finish Project (op 13) - ported 2026-10-28 (branch toolpaths2), unverified (no stored op)
- **Dispatch** (decomp/mcmsurf_all.c): stage 1 (l.197504-197524) 6 / 0xc -> FUN_18013ae60 (the rays), 7 / 0xd / 0x2c -> FUN_18013a370 (the
  projection, as op 7); stage 2 (l.199400-199420) 5 / 6 / 0xb / 0xc -> FUN_180132430 (op 5 / 11's, ported in sr_parallel), 7 / 0xd / 0x2c /
  0x6d -> FUN_180139c80 (op 7's). Pencil / Leftover / Steep / Shallow / Constant Scallop (0x27-0x2b) -> FUN_180133f10 (stage 1, the
  FUN_1801cfb60 / FUN_1801d4df0 engine with tmptc file swaps, NOT DECODED) and FUN_180132b20 (stage 2).
- **Radial (0xc)**: msurf_map l.340213-340221 + page 1036 (CMsRadialFinishPg init FUN_1800f7600, OK FUN_1800f8410): max angle increment
  0x1300, cutting method 0x1308, Start inside / outside 0x130a (radio index = (byte == 0)), start angle 0x1310, sweep 0x1318 (radians,
  MCReal 0..360 / -360..360 degrees), start offset distance 0x1320, depth limits 0x12e8; the radial point = mcmsurfres 309 "Select radial
  point" (FUN_18001ccc0: ops 6 / 0xc; after the op dialog in Mastercam, before it here). FUN_18013ae60: n = ceil(|sweep| / step - 1e-6) + 1
  when |sweep| < 2 pi; the start angle into [0, 2 pi); ray i at start + i x sweep / ceil(..). Max short gap: pct x D (l.340445). Port:
  cam3d_srough.c sr_parallel(.., 2) - the rays from the start offset to the model's farthest point + D / 2 + stock (ours), profiles = op 11's
  height field (FUN_1802aed20, 1116 lines, NOT DECODED), op 11's stage 2 / links. Scene gen_sfradial.
- **Project (0xd)**: msurf_map l.340223-340226 (projection type = 0x1300, no levels) + page 1002 (CMsProjectFinishPg init l.157135-157150,
  OK l.157950-157975): source op 0x1418, Add depths 0x1402 (860), Retract between cuts 0x1439 (1180), depth limits 0x12e8. Port:
  gen_project_x(.., 1) = op 7's port with one pass at the projected Z (FUN_180293f40 without levels NOT DECODED: ours), the depth limits as op
  11's band; Add depths and Points refused. Scene gen_sfproject (BRACKET4, source op 2).

## 6. Flowline status (2026-11-08)
Scan of every part (viewer/mcam_load, 1442 files under <cad-files>, user-parts, ~/mastercam-re/files and the shared Mcam2017 folder, each run capped at 60 s / 3 GB, files that did not load are not listed): **op 8 Rough Flowline: no stored op. op 14: only PROJECTBLEND op 12 (2017 and X9 copies, 487 NCI points), with 6 drive surfaces (6 TP kind 4 refs)**, so it runs msurf_flow (l.50801, 929 lines), not the single-surface FUN_180044c70 path of 2.2: msurf_flow orders / joins the surfaces with FUN_18004fca0 / FUN_180049730 / FUN_18004bcc0 / FUN_180049f90 / FUN_18004ca80 (not read) before the FUN_180045e60 / FUN_180046950 / FUN_180044a10 stages. No single-surface stored op exists, so a single-surface port (make_raw_flowlines + FUN_18003cd50, about 2000 lines with the curvature step FUN_1805851e0 / FUN_180585d10 / FUN_1805869d0 still NOT DECODED) could not be proven against any oracle. Decision: not ported, gallery items stay greyed. Size of a real port: msurf_flow and its helpers, FUN_18003cd50 (1374 lines), FUN_180045e60 (397), FUN_180046950 (805), FUN_180041d90 and the mccore flowline family (flow_core.c, 3000 lines): a multi-session item with a single 487-point oracle.

## 7. Restmill op 107 (2026-11-08): what its stock input requires
Further decoded in formats/stock_model.md (the Stock Model source, the sweep engine). No stored op 107 exists in any corpus (same scan). Decoded from mcmsurf: the rest material is chosen on page 234 "Rest Material parameters" (mcmsurfres dialog 234), flags **record 0x144c = op+0x13e4** (l.434432-434450 / l.434670): bit 8 = **STL file** (radio 1100; also sets msurf_mach_stl l.16190), bit 4 = **Roughing tool** (1096: Diameter 1097 / Corner radius 967, Stock resolution 1140), bit 2 clear with bit 1 set = **All previous operations** (1053), else **One other operation** (1052, source op id **record 0x1448 = op+0x13e0**, must be >= 1 and of type 9, 10, 0xf, 0x83 or 0x6b, FUN_180012e70 l.12834-12882), plus "Use regen file" (1008) and the adjustments 1101-1103 / 25324.
FUN_18013b2f0 (l.207046) then reads the previous computation's own temp file (op+0x9b9, default "temp" + rec suffix; it keeps only its records whose flag is 0, writes temp2<suffix> with op+0x13a0 / 0x13a8 / 0x13b0) and passes it to the contour Z-cut family FUN_1802a14e0 / FUN_1802a0710. So the input is **not a 3D stock model**: it is Mastercam's own intermediate surface-rough result from the earlier op (or a model built from the roughing tool / STL by the Z-cut / rest engines: FUN_18013bb50 and msurf_use_emm_restmill / msurf_use_3rd_party_restmill, NOT DECODED). This program writes no such files and has no rest model. Restmill therefore stays refused (gallery greyed); no oracle, unverified either way.

## 8. Scoping of Flowline, Hybrid, Leftover, Steep, Shallow, Constant Scallop (2026-11-08; nothing ported)
Line numbers are decomp/mcmsurf_all.c unless marked. "Closure" = transitive callees of the named function within that one Ghidra dump (regex over the output): shared helpers are counted, calls out into mccore are not. It sizes what must be read; not all of it runs for any one mode.

### 8.1 Stored ops
Full scan with tools/mcam_ops.py of 1486 parts under <cad-files>, <user-parts>, ~/mastercam-re/files and test-exports (60 s / 3 GB per part): 1470 parsed, 16 raised errors (none of them named for these strategies, but their ops are unknown).

| strategy | op | stored op (copies) | NCI |
|---|---|---|---|
| Flowline finish | 14 | PROJECTBLEND op 12 (2017, X9, 2018) | 474 moves in 2017 and X9, 0 in 2018; 6 drive surfaces |
| Flowline rough | 8 | none | - |
| Leftover | 40 | none | - |
| Parallel Steep | 41 | none | - |
| Parallel Shallow | 42 | none | - |
| Constant Scallop | 43 | CURVE 5 AXIS REVIEW FILE op 2 (2017, 2018, X9) | 25451 moves each; 6 drive surfaces |
| Hybrid | 132 style 7 | TUTORIAL 7 op 6 (2017 and X9, inch and metric) | 17346 inch / 24328 metric |

The Handbook parts LEFTOVER, PARALLELSTEEP, STEEPSHALLOW, FINISHSCALLOP and FLOWLINE hold only their exercises' earlier ops, so 8 / 40 / 41 / 42 cannot be checked against Mastercam at all. Outside this scope: Pencil 39 has PENCILSEAMS X9 op 1 (1507 moves).

### 8.2 Dispatch, corrected
"FUN_180133f10 is the shared engine" (TODO) is only part of it.
- Stage 1 is the switch in FUN_18012a960 (l.197600-197750). FUN_180133f10 (l.202694, 974 lines) is a frame and raster-count wrapper over **FUN_1801cfb60** (l.309504, 1880 lines, the MSurfLMM.cpp family). Modes: Leftover 4 + 1 when neither 0x12e8 nor 0x1302 is set or 0x12a8 == 2; otherwise FUN_1800c9010 when msurf_use_emm, else 9 + 8 and FUN_180135a80 (l.197640-197695). Steep 2 (l.197700). Shallow 7 (method 2 only) + 3 (l.197704-197722). Scallop 6 + 5 (l.197726-197742). Blend 0xb + 10.
- **(Corrected in 9.3: with the default config bnci_surface clears msurf_use_emm for a ball tool, so Constant Scallop does run FUN_180133f10(6) + (5).)** **Constant Scallop's compiled-in default does not use this engine.** read_appmch_cfg (l.18391) sets msurf_use_emm = 1 (l.18479) and msurf_use_new_3d_collapse_on_emm_scallop = 1 (l.18481); configuration entries can change either (l.18614 for the second; the config source was not traced), and bnci_surface clears msurf_use_emm per op under tool / parameter conditions (l.11977-11979, not fully read). With both set stage 1 is skipped (l.197726) and stage 2, FUN_180132b20 (l.202007-202048), runs mutl_strip_emm (tmpmm5 -> tmpmm4x) and **FUN_180123090**. Modes 6 + 5 run only when DAT_1805034a1 is set (a tp_get_all count > 0, l.12202-12203; presumably check surfaces, not confirmed). So the one stored Scallop op most likely exercises FUN_180123090, not FUN_1801cfb60.
- Shallow's collapse flag defaults to 0 (l.18482): Shallow stays on FUN_1801cfb60.
- Inside FUN_1801cfb60, kind-2 mach_model entries (EMM) are scanned by FUN_180195860 (l.309881, l.309899; files zd_scn00 / zc_scn00) and kind-1 surfaces by FUN_1801d6390 (l.310411). Which kind these ops get by default was not established.
- Not in scope but found: msurf_use_3rd_party_pencil = 1 routes op 39 to the third-party algorithm when DAT_1805034a1 is clear (l.12264-12266).

### 8.3 Size of what a port needs (closure, decompiled lines)
| target | closure | parts |
|---|---|---|
| FUN_1801cfb60 (Leftover, Steep, Shallow) | 48004, 543 functions | per-line stages alone: FUN_180195860 6420, FUN_18018fd80 2125, FUN_180184cc0 2276, FUN_1801974e0 914, FUN_180181680 447, FUN_180182de0 413 |
| FUN_180123090 (default Scallop) | 50046, 755 functions | not read |
| msurf_flow (Flowline) | 38992, 379 functions | generator FUN_18003cd50 2209; FUN_180046950 1111; FUN_180045e60 480; topology FUN_180048b70 951, FUN_180049730 1001, FUN_18004fcf0 855; FUN_18003c9d0 13525, taken when gouge check 0x1328 is on, the default (FUN_180044a10 l.45207-45235); FUN_18003cba0 2646 otherwise. The mccore curvature step of 2.3a is on top |
| FUN_1800882e0 (Hybrid, decomp/mckernel_all.c) | 132313, 2524 functions | FUN_180086730 (Z-cut stage) alone 31042 |

### 8.4 What the only Flowline oracle shows (PROJECTBLEND op 12)
Ball D 0.25, tolerance 0.001, scallop 0.002, zigzag, gouge check on, 0x1340 = 2, 0x1342 = 1. Measured from the NCI: rows run along Y and step about 0.04 in X, rows continue across surface boundaries, and rapids break the op into 6 feed runs (37, 4, 368, 35, 6, 6 points). It can verify only the multi-surface stitching of msurf_flow plus the gouge branch. A single-surface port (2.2) has no stored op to prove it, and both page defaults (scallop stepover, gouge check) need the 2.3a curvature step and FUN_18003c9d0.

### 8.5 Status
Nothing was ported or guessed; the gallery items stay greyed. The decompiled functions above were read only as far as this section states: FUN_1801cfb60's model setup and control flow, FUN_180133f10 whole, the first half of msurf_flow; the rest is sized, not read. Smallest honest next piece: single-surface op 14 / 8 with Distance stepover and gouge check off (FUN_18003cd50, FUN_18003f070, FUN_180043530, FUN_180046950, FUN_18003cba0, FUN_180040a90, make_raw_flowlines), which would refuse the page's two defaults and could not be verified against Mastercam.


## 9. FUN_180133f10 decoded; the defaults of ops 39-43 corrected (2026-11-13, branch of agent "classic finish engine")
Sources: **l.N** = decomp/mcmsurf_all.c; **asm** = `objdump -d -M intel` of ~/mastercam-re/files/mcmsurf.dll 180133f10-180135a70 (the
decompiler mangled the mode tests after the raster count; the table in 9.2 is read from the assembly, mode in r13w, param_6 in dil /
[rbp+0x2b8]); **mk l.N** = decomp/mckernel_all.c; **core** = decomp/poszlo/hardmill.c, setup.c, core_hst.c and decomp/proj/d4.c (mccore).

### 9.1 What the engine is
`FUN_180133f10(mode, method, D, r; stack: stock, swap, &err, &ok)` (l.202694-203668) is **not** the generator: it sets the frame, the
slice step and the number of raster lines, then calls **FUN_1801cfb60(kind, a, b, stock, ...)** (l.309504, the MSurfLMM.cpp family,
closure 48004 lines, 8.3) one or more times. Steps, in order:
1. Frame (l.202737-202791): the tool plane (GetActiveViewNo(0)) turned by msurf_cut_angle: x axis DAT_1804e8dd8, y axis DAT_1804e8e08,
   origin DAT_1804e8df0. **Modes 4, 6, 7, 9, 0xb (mask 0xad0) turn it a further 90 deg** (x' = old y, y' = -old x; l.202782-202790).
   With `swap` set the temp files tmptc / tmptc90 are exchanged around the call (FUN_1801b8e10 = rename) so these modes read the
   90-deg copy of the containment curves (GetConvertProjectPts l.200202-200253 writes tmptc90 for ops 0x27, 0x28, 0x2a, 0x2b).
   `swap` = DAT_1805039d4: msurf_map clears it (l.340483); the chain conversion sets it to (number of containment curves > 1)
   (l.196583, l.200253). So with no or one containment curve no swap happens.
2. Step (l.202819-202863): method 2 (the "collapse" ops): op 0x28 with record 0x1368 (op+0x1300) set -> msurf_colaps_res_pct =
   FUN_180246570(.., slice, {min(0.05, chord / slice), 0.8}) (the 2 sqrt rule below divided by the slice, clamped to the pair); ops 0x2a /
   0x2b read op+0x1310 / op+0x12d0 into the same local but the call is only on the 0x28 path (as decompiled; NOT re-checked in asm);
   **step = msurf_colaps_res_pct x msurf_slice_dist**. Other methods: step = msurf_slice_dist, for ops 0xd / 0x6d recomputed as
   **FUN_1802465b0(D, chord, 0.05 in / 1.27 mm)**.
   **FUN_1802465b0(a, c, f) = 2 sqrt((a - c) c) if (a - c) c > 0, else f** (l.383345): the chord of a circle of diameter a at sagitta c.
   (This decodes the rule left open in 3.1 / blend.md 3.1; msurf_map uses it for op 0x27's slice too, l.340252.)
3. Reference tool (l.202864-202894): modes 1 / 4 (Leftover) use dref = msurf_ref_dia + 2 msurf_ref_ovld and the tolerance op+0x1318
   when op+0x1320 is set; their margin = 0.75 dref. Others: margin 0 (or 10 steps with swap and method 2); op 0x27 takes the chord.
4. Extent (FUN_18012f9c0 l.200261): the model's records in tmpmm.tmp and its edges in tmpedg0.tmp, measured in the frame; Z range grown by
   |stock| + D / 2. Lines (l.202917-202944): lo / hi = -(the two extents), each moved in by half a step when its flag is set;
   **n = ceil((hi - lo) / step); n < 2 -> one line at the middle; else step = (hi - lo) / n and n + 1 lines**, then one more line per
   step while lo / hi are still inside the second extent pair (local_270 / local_268).
5. With `swap` only (l.202945-203535): a pre-pass over tmptc.tmp that drops the lines outside the containment's range and errors -0x18
   when nothing is left. Not taken with swap = 0.
6. The generator calls (9.2), then FUN_1802257c0(2) for modes 1, 2, 3, 5, 8 and, with swap, FUN_1801d4d60(mode, method) (asm 1801359b4).

### 9.2 Mode -> FUN_1801cfb60 calls (asm; the third / fourth arguments are a, b of FUN_1801cfb60(kind, a, b, stock))
| mode | calls | used by (stage 1 FUN_18012a960) |
|---|---|---|
| 1 | cfb60(10, 1, 1) + FUN_1801d4df0(10); FUN_1801ae030(tip, short gap); FUN_1801afab0(4, D, chord, 0); cfb60(1, 0, 1); FUN_1802257c0(2) (needs D < dref and 2 (ovld + rref) <= dref + tol, else error -0xb) | Leftover 40, 2nd |
| 2 | FUN_180224310; cfb60(2, 1, 1); FUN_1802257c0(2) | Parallel Steep 41 |
| 3 | (method != 2: FUN_180224310); cfb60(3, method != 2, 1); FUN_1802257c0(2) | Parallel Shallow 42, 2nd |
| 4 | cfb60(0xb, 1, 1) + FUN_1801d4df0(0xb) (90 deg); FUN_18025dd50; cfb60(4, 1, 1); FUN_180260860 (same tool checks as 1) | Leftover 40, 1st |
| 5 | cfb60(6, 0, 1); FUN_1802257c0(2) | Constant Scallop 43, 2nd |
| 6 | cfb60(7, 1, 0) (90 deg) | Constant Scallop 43, 1st |
| 7 | FUN_180224310; cfb60(5, 1, 0) (90 deg) | Parallel Shallow 42, 1st (method 2 only) |
| 8 | cfb60(8, 0, 1); FUN_1802257c0(2) | Pencil 39, 2nd (only on the path of 9.3) |
| 9 | FUN_180224310; cfb60(9, 1, 0) (90 deg) | Pencil 39, 1st (idem) |
| 10 | cfb60(10, 1, 1) + FUN_1801d4df0(10) | Blend 109 (pre-pass), 2nd |
| 0xb | cfb60(0xb, 1, 1) + FUN_1801d4df0(0xb) (90 deg) | Blend 109 (pre-pass), 1st |

So every classic finish op of 39-43 that reaches this engine needs FUN_1801cfb60 (and for 40 / 109 FUN_1801d4df0, l.311890) - the
48k-line closure of 8.3, of which nothing is ported. Stage 2 for 0x28-0x2b without EMM is FUN_1801d3ca0 (the collapse / zones / link
writer, l.202013), also not read.

### 9.3 Which path each op takes by default (corrects 8.2)
Compiled-in configuration (read_appmch_cfg l.18470-18496; the per-option file `<apps>/tp.cfg` l.18497 is absent from the installs on
this machine, so these values hold): msurf_use_emm = 1, msurf_use_new_3d_collapse_on_emm_scallop = 1, _shallow = 0, _leftover = 0,
msurf_use_3rd_party_pencil = 1, every msurf_use_emm_<op> = 0.
- **bnci_surface clears msurf_use_emm** (l.11977-12015) for every op in the mask 0x1f80000079e0 (ops 5-8, 11-14, **39-44**) and 0x6d
  unless the stock is negative with |stock| > r, or the tool type (op+0x2e4) is in 0x223000 / 0x19 (FUN_18021b330) / 0x17 (FUN_18021b3b0)
  / 0xe with a bottom chamfer, or the op's own msurf_use_emm_<op> flag is set (0x2c: _plunge keeps it). Default ball / bull tools with
  stock >= 0 therefore run **without EMM**.
- **Pencil 39**: with no check surfaces (DAT_1805034a1 = 0, l.12202) and msurf_use_3rd_party_pencil (l.12256-12267) the op gets
  msurf_use_3rd_party_algorithm = 1 and process_3rd_party_algorithm FUN_1800199d0 (l.13350-13377) replaces stages 1 and 2:
  run_mchardmill_macro (mccore) -> MakeMCHSTToolPath FUN_180573610 (core hardmill.c l.3-54: type 0x27 -> FUN_1809228d0, the generic
  MCHSTToolPath) -> FUN_1809232b0 (core setup.c l.49-83) copies the op with **FUN_180923dd0** (core d4.c l.16-59) -> mckernel
  FUN_18007bb48 (mk l.91845: type 0x27 -> **FUN_180081850**, mk l.94155), the McHmm macro "// Mastercam X - Pencil Pass Macro":
  Database Loader 0x1421 -> Detect Top Surfaces 0x1423 -> Surface Tessellation 0x1424 -> Bounding Box 0x1425 [-> Boundaries 0x1427]
  -> **Pencil Passes Plan 0x140f** (the HST Pencil's plan, formats/pencil.md) -> **Pencil Linker 0x1442**, or with offsets
  Constant Stepover Passes From Passes 0x140d + Constant Surface Stepover Linker 0x1420. FUN_180133f10(9 / 8) runs only with check
  surfaces. See 10 for the parameters and the port.
- **Leftover 40**: FUN_180133f10(4) + (1) when neither op+0x12e8 nor op+0x1302 is set or op+0x12a8 (cutting method) = 2; else (the
  default config clears msurf_use_emm) FUN_180133f10(9, 2, D' = 2 ovld + dref, r' = ovld + rref) + (8) and FUN_180135a80 (l.197640-197695).
- **Parallel Steep 41**: FUN_180133f10(2). **Parallel Shallow 42**: (7 when method 2) + 3 (msurf_use_new_3d_collapse_on_emm_shallow = 0).
- **Constant Scallop 43**: msurf_use_emm is cleared for a ball tool with stock >= 0 (no msurf_use_emm_scallop), so FUN_180135a70 returns
  0 and **stage 1 runs FUN_180133f10(6) + (5), stage 2 FUN_1801d3ca0** - not FUN_180123090 as 8.2 assumed (that is the EMM branch:
  tool types of the EMM list or a negative stock). The corpus op (CURVE 5 AXIS REVIEW FILE op 2: tool type 11, D 0.75, r 0.375, stock 0,
  slice 0.05) takes FUN_1801cfb60.
- **Blend 109**: as blend.md 3.1 (modes 0xb + 10 only feed the EMM projection FUN_180278890).

### 9.4 Oracles (stored ops with NCI; 8.1 plus this pass)
Pencil 39: PENCILSEAMS.mcx-9 op 1 (X9 Handbook Vol. 2; D 0.25 ball, tol 0.001, **offsets 0x1324 = 1, 0.05**, bitangency 170). Constant
Scallop 43: CURVE 5 AXIS REVIEW FILE op 2 (FUN_1801cfb60 kinds 7 + 6, not portable now). None for 40 / 41 / 42.

### 9.5 Port status
Nothing of FUN_180133f10 / FUN_1801cfb60 is ported: 40 / 41 / 42 / 43 stay greyed (the engine is 48k decompiled lines, unread past its
setup). Classic Pencil 39's default does not use it: section 10.

## 10. Surface Finish Pencil (classic op 39) - ported on the HST Pencil's net (2026-11-13)
Code: viewer/cam3d_pencil.c **gen_sfpencil** (the decode is its header comment) -> pc_generate, the net / passes / links of gen_pencil
(formats/pencil.md 6-8), split out of gen_pencil. Type CAM_SFPENCIL ("Surface Finish Pencil", mccoreres 4360), cam_cli "sfpencil", the
Toolpaths manager's Surface finish > Pencil (**kept greyed: user decision N13-19, until the Pencil Linker 0x1442 is decoded**), default record = the defaults file's first op 39 (Mill_MM op 35 / Mill_Inch op 41).
- **Op -> McHmm** (9.3): FUN_180923dd0 copies, record (= op + 0x68) -> TP (= MCHSTToolPath + 0x60): 0x1239 -> +0x120 tip comp, 0x1240 ->
  +0x128 tolerance, 0x1326 -> +0x1a4, 0x12d0 / 0x1304 -> +0x160 / +0x1a7, 0x12b0 -> +0x150, 0x1290 / 0x1298 / 0x12a8 -> +0x138 / +0x140 /
  +0x148, 0x12a0 -> +0x158, 0x1288 -> +0x15a, 0x12e8 / 0x12e9 / 0x12f0 / 0x12f8 -> +0x168 / +0x169 / +0x170 / +0x178, 0x1324 / 0x1322 ->
  +0x1a2 / +0x1a0, 0x1340 / 0x1338 -> +0x190 / +0x198, 0x1300 / 0x1301 -> +0x180 / +0x181, 0x1320 -> +0x1a6, 0x1328 -> +0x188; stock
  0x2e0 -> +0x40 (FUN_180923d30); the tool (FUN_180924880) as op 132's.
- **Kernel parameters** (FUN_18007a988 mk l.91071-91199): ToolThicknessXY = ToolThicknessZ = stock + l, StepMachTolerance = tol / 2 + l,
  l = (tol / 2 - Rc) / 2 when Rc < tol / 2; PencilOverthickness = 0x1340; PencilBiTangentMaxNormalChange = 180 - 0x1338 (degrees) or 15;
  PosZLo / Hi from the depth limits (- Rc unless 0x12e9); PosContactOnly true; ContactAngleLo / Hi = 0 / tol only when neither Allow flag.
- **Links** (FUN_18007b240 from its asm; FUN_18007a3e4 mk l.90846: LinkRetractStyle 1, LinkClearSurfaceBy, LinkTrimmingStyle,
  vertical lead radii from 0x12a8 / 0x12b0, LinkSafeZ / LinkZClearance, LinkMaxStayOnSurface = 0x1290 or D x 0x1298 (with 0x1288),
  SurfaceRampStyle from the gap type): see gen_sfpencil's comment. The Pencil Linker 0x1442 that reads them is NOT DECODED.
- **Page 240 CMsPnclPg** (DDX FUN_1800eb7d0 l.152138, init FUN_1800ebaa0 l.152240, OK FUN_1800ec2c0 l.152485, enables FUN_1800ec020
  l.152417): formats/opdlg_fields.tsv s240. Machining direction 1045 = (0x1320 == 0) with Climb / Conventional 0x1304; Parallel passes
  None / Number per side / Unlimited = 0x1324 (0 / 1 / 2), Number of passes n stored as 2 n + 1 in 0x1322, Stepover 0x1328, Cut method
  0x1326 (0 Zigzag, 1 One way); Bitangency 0x1338 in degrees, stored 0 shown as 165 (and 165 stored as 0); Overthickness 0x1340; Depth
  limits 0x12e8; approximate start point 0x1305; Allow negative / positive Z 0x1300 / 0x1301.
- **Defaults**: Mill_Inch op 41: D 0.5 ball, tol 0.001, no parallel passes, Zigzag, Machining direction off, Bitangency 0 (-> 15 deg),
  depth limits off -> **generates** (LinkClimbMilling false). Mill_MM op 35: D 10, r 4 (bull) and depth limits on -> refused (non-ball).
- **Oracle** (tools/tests/pencil_test.sh, the classic part of it: `pencil_test PART 1 MOVES --classic`): PENCILSEAMS.mcx-9 op 1 as stored is
  refused (Parallel passes = Number per side, 0x140d). With Parallel passes None (`--rec 1324=s:0`) the centre passes: **ours->ref p50
  0.0030 p95 0.0077 max 0.0097 in, 100 % of ours within 0.01 of Mastercam's moves**; 32 % (16.0 in) of Mastercam's 49.9 in covered (the
  rest are its offset passes at +-0.05: ref->ours p50 0.0500). The same net and the same numbers as the HST op of the 2017 copy
  (pencil.md 7). cam_cli: the stored op with `--rec 1324=s:0` 240 moves / 18.5 in; a new default inch op (D 0.5 ball, the 61 surfaces)
  353 moves / 29.5 in.
- **Ours** (labelled in the code, as gen_pencil): the drop on our tessellation, the cell types, the pass order (nearest start, the net's
  direction) and the SAFE links - the Pencil Linker 0x1442 is NOT DECODED; the default op's LinkClimbMilling false is the only link
  setting accepted.
- **Refused**: Parallel passes (0x140d), Overthickness, non-ball tools (so the MM default), negative stock, Depth limits, the approximate
  start point, Allow negative / positive Z off, LinkClimbMilling on (One way, or Machining direction with Climb), check surfaces (their path is
  FUN_180133f10 / FUN_1801cfb60). Dialog: the Depth limits / Gap / Advanced sub-dialogs of page 240 are not wired.

## 11. Where this stopped (2026-11-13) and what is next
- Leftover 40, Parallel Steep 41, Parallel Shallow 42, Constant Scallop 43 and Blend's EMM pre-pass all need **FUN_1801cfb60** (9.2), the
  48k-line MSurfLMM closure; nothing of it is read past its setup (8.2). Only Constant Scallop has a stored op (CURVE 5 AXIS REVIEW FILE op
  2, kinds 7 + 6). Next decode target: FUN_1801cfb60 kinds 6 / 7 (Scallop) - its per-line stages FUN_180195860 (6420 lines),
  FUN_18018fd80, FUN_180184cc0 - then the stage-2 writer FUN_1801d3ca0. Multi-session; nothing can be ported before that.
- Classic Pencil: the offsets plan 0x140d (shared with HST Pencil, pencil.md 9: evaluator in decomp/scallop/cs.c) would make the stored op
  regenerate as stored; the Pencil Linker 0x1442 would replace the SAFE links.
- **N13-19 (user decision, 2026-11-13)**: Surface Finish Pencil stays **greyed** in the Toolpaths manager until the Pencil Linker 0x1442 is
  decoded; cam_cli `sfpencil` and the port stay. HST Pencil's "ours" pass order / SAFE links (pencil.md 8) are the same open item.

## 12. Constant Scallop (op 43) through FUN_1801cfb60 / FUN_1801d3ca0: the map (2026-11-13, branch "classic finish engine 2")
Sources as 9 (**l.N** = decomp/mcmsurf_all.c, **asm** = objdump of mcmsurf.dll). Sizes: "n lines / closure" (closure = transitive callees
inside mcmsurf by name reference, over-counts shared code; mccore calls not counted). Nothing below is ported yet: this is the call tree
and the file chain that the port and the DLL oracle (12.7) follow. Default op 43, ball tool, stock >= 0 (9.3).

### 12.1 The whole op, in order (bnci_surface l.11781 -> FUN_1800135a0 l.13212)
1. bnci_surface: operation_manager(cur_op, 5) (l.11912) / tool_manager(tool, 5) (l.11955; both mcmill imports), read_appmch_cfg, the EMM
   decision (l.11977-12015, 9.3), tp_get_all(op) (the op's TP records, 0x388 bytes each = the file's TP_ID record; kind at +0x90, 4 = a
   surface whose eptr is at +0x80; the op itself comes back in the ent buffer at +0x68, l.12046-12098; stride 0x388 from FUN_180229ba0's
   loop), make_tool_shape, **msurf_map** (l.339935: op record -> msurf_* globals; for 0x2b msurf_cutting_method = 2 (l.340295), slice =
   op+0x1298, cut angle op+0x12c8, start point op[0x12a0], climb op[0x12a1], outside-in = !op[0x12a2], zones op[0x12a3], collapse %
   op+0x12a8).
2. FUN_1800135a0(op id, chains, start pt, &ok) (l.13212): FUN_18029e890, FUN_180045920, FUN_180012e70, FUN_18023b510, FUN_18012b7c0 (no
   containment: removes tmptca / tmptc / tmptc2), **FUN_18012a310** (l.197248, closure 18.8k: build_machine_model -> FUN_180229ba0
   l.363762 -> mccore make_surf_model per surface -> tmpmm.tmp (0x78 mach_model_surf records), tmpmm2.tmp, tmpedg0.tmp (0xb0 edge
   records), tmpcrn0.tmp (0x70 corner records)), **FUN_18012a960** (stage 1, l.197428), **FUN_18012e440** (stage 2, l.199345).
3. Stage 1 for 0x2b: FUN_180133f10(mode 6) -> **FUN_1801cfb60(7, 1, 0, ..)** (l.203361, raster turned 90 deg, param_18 = 1 l.203347),
   then FUN_180133f10(mode 5) -> **FUN_1801cfb60(6, 0, 1, ..)** (l.203326, param_18 = 1 l.203312). Containment flag DAT_1804e8ffc =
   FUN_1801cfb60's param_12 (l.310062).
4. Stage 2: FUN_180132b20 (l.201922, from the op switch l.199469 with method 2): FUN_1802022d0, FUN_180129b40(cut angle), FUN_18022f700(1),
   FUN_180207f00, msurf_colaps_dist = msurf_slice_dist (l.201994); without EMM (9.3) NCIFile(op) + **FUN_1801d3ca0** (l.202013, call asm
   180132dc7); then, when msproj.tmp has records (DAT_1804e8ea0 > 0), FUN_180227b60, FUN_18025dd50 (l.202128), FUN_180290fb0(2, ..)
   (l.202140, gap / link pass over gap.tmp, tmpmm2.tmp); always **FUN_180136670(nc_file)** (l.202160, the NCI writer) and FUN_1802257c0(3).
   FUN_1801d3ca0's arguments (asm 180132d23-180132dc2): msurf_opt_cut_order, msurf_create_zones, msurf_colaps_dist, msurf_prompt_st_pt,
   &DAT_1805138b0 (start point), (short)DAT_1804d7038, nc_file, comp_to_tip, method 2, outside-in, climb, init_feedrate, plunge_rate,
   msurf_max_short_gap, the NCI name (unused), abort flag, &err.

### 12.2 FUN_1801cfb60 (l.309504, 1880 lines, closure 57.3k) for kinds 7 / 6
- **Kind flags** FUN_18018b500(kind, ..) (l.264801); its outputs map to locals by the call's rbp offsets (asm 1801d0670-1801d06e8): for
  kinds 6 / 7 local_3655 = 2, local_364b = 2, local_3667 = 0, local_364c = 0, local_3648 = 0, **local_3674 = (kind == 7)**, local_364a =
  0, local_3671 = 1, local_3672 = 1, local_3647 = 1, local_3654 = 0, local_3649 = 0, local_3630 = 0, local_366c = 0.
- **Model kinds** (l.309853-309917): get_mmsurf (tmpmm.tmp, 0x78 records) +0x68 = 2 (EMM) -> FUN_180195860 (zd_scn00 / zc_scn00, EMM
  only); +0x68 = 1 (plain surface, this case) -> local_3644 = 1 -> **FUN_180195b80** (l.271489) = a rename wrapper around the scanner
  **FUN_1801ea7e0** (12.3); local_3676 = (it returned 0) (l.309918). Only if the scanner fails: the per-surface fallback FUN_1801d6390
  (l.310411; l.312649, 660 lines / 9.4k) and the 0x58-record tmpedg2 reader (l.310430-310498).
- **Constants** (l.309965-310048): DAT_1804e9238 = 0.9 tol (param_10), DAT_1804e9240 = (tol - 0.9 tol) / 2, DAT_1804e9248 = 0.01 x
  DAT_1804e9238, DAT_1804e9250 = min(system tol, 0.0001 x DAT_1804e9238), DAT_1804e9258 = corner tolerance from msurf_corner_dev (kinds
  other than 10 / 11); frame DAT_1804e9168 / 9150 / 9180 = param_7 / -param_9 / param_8; DAT_1804e9144 = number of lines (param_15);
  DAT_1804e9230 = -param_17 (first line), DAT_1804e9228 = -param_16 (step); arc counts DAT_1804ea47c / ea478 = ceil(pi / (4 acos(1 -
  dev / r))) + 1 (l.310115-310150).
- **Setup**: FUN_180164ab0, FUN_18015d700(.., local_3672, ..) (l.310030; its output byte local_3157 = 1), FUN_1801868a0(local_3676,
  local_3674) (l.310050; l.262043): renames the scanner's plmvcs00.tmp to **rufvcs90.tmp (kind 7) / rufvcsd.tmp (kind 6)**, loads its
  per-line index into DAT_1804e9798 (FUN_1801e7c20) and the 500 x 0x68 pool DAT_1804e97a8.
- **Scan file**: plmscn00.tmp -> tmpedg2.tmp (l.310549-310572), opened "rb"; FUN_1801e7ad0 (l.310629) loads its line table into
  DAT_1804e92f8 -> local_6e8[2]. FUN_1801b3130(0, 10, ..) (l.310730) allocates the work arrays (max(16000, pool) x 0x68).
- **Recut file** = "temp.rgn" + rec_suffix in the temp dir (FUN_180228850 l.362981), DAT_1804e9060, by FUN_18025d6d0(local_3646 == 0, ..)
  (l.310658): kind 7 creates it with a 0x38 header (FUN_180229640(-0x38, -1, -1)); kind 6 opens it "rb+" and appends.
- **Per-line loop** (l.310762-311254; line DAT_1804e92d8 at offset DAT_1804e92b8, += DAT_1804e9228 per line). For kinds 6 / 7
  (local_364a = 0, param_18 = 1, local_3671 = 1; asm 1801d2c3c) the arm l.311127-311168 runs:
  1. FUN_180183310(scan, chain, FUN_1801a6ee0, FUN_180182e70, ..) (l.311101; l.259964, closure 435): walks the line's chain of blocks
     (0x18 trailer: +2 n, +4 previous block or -1, +8 line position; n x 0x30 points before it: +0 s, +8 z, +0x10 normal[3], +0x28 short
     code, +0x2a byte); consecutive points pair into segments inserted, ordered by s, into the line list (FUN_180182e70 l.259774; 0x68
     nodes by FUN_1801a2760: +0 flag, +8 s0, +0x10 z0, +0x18 n0[3], +0x30 flag, +0x38 s1, +0x40 z1, +0x48 n1[3], +0x60 code, +0x62 next).
  2. FUN_1801974e0 (l.272574, closure 914): drop segments shorter than DAT_1804e9250 (FUN_1801d7da0), merge runs (FUN_1801a1d80).
  3. FUN_1801b0c80 (l.288238): the line's rufvcs records (vertical walls) into DAT_1804e97a8.
  4. FUN_180184cc0(0, &local_3158, nvcs, vcs, ..) (l.260939, closure 2.2k): list -> array, code 0x24 dropped, vertical-gap connectors
     (code 0x28) where two segments meet at the same s with different z (straight, or from the vcs records by FUN_18015bca0 / FUN_1801c8080).
  5. FUN_1801a5510 (free the list), FUN_180182620 (l.259450: the code of each 0x28 connector from its neighbours), FUN_180181a80(0,
     param_5, ..) (l.259010: recode the middle of joined runs), FUN_180181680 (l.258829: back into the list).
  6. FUN_18018fd80(1, 0, .., 0) (l.267871, closure 2.1k): endpoint flags (node array FUN_1801cad70 / FUN_1801cb640 / FUN_1801cdcf0,
     FUN_1801cfa90, FUN_1801c5a50(3)).
  7. FUN_180182de0(2, recut, ..) -> **FUN_1801a1a70** (l.277668): the line block into the recut file: 0x18 points (+0 s, +8 z, +0x10 code,
     +0x12 flag; a joint within DAT_1804e9250 becomes one midpoint with code + 0x27) and a 0x18 header (+0 2, +2 n, +4 previous block, +8
     line position); DAT_1804e92f8[line] = its offset.
- **After the loop**: containment only (l.310802-310828; FUN_180192af0 -> FUN_180198370: z = -1000, code 0x25 points); FUN_1801c9350
  (l.304982) copies the rufvcs records per line into the recut file; the trailer (l.310888ff: frame, tolerances, line table, ..); header
  +4 = 0x20, +8 = 0x11 for kinds 6 / 7 (FUN_180229640(-0x38, 0x11, 0x20), l.310977-310989); kind 7's trailer offset at header +0x18, kind
  6's at +0xc (mask 0x2b0, l.310995); kind 7 rewrites the header (FUN_180268b30, l.311014), kind 6 finishes with FUN_18022c850 (l.311022,
  l.365146: appends tolerances, the surface list, the op (0x28d0), the tool (0x698), the interfacial tool).
- NOT DECODED here: the merge criterion of FUN_1801a1d80, the vcs connectors, the endpoint flag values, the segment code families
  (predicate tables at l.229818, 230426, 233638, 239850).

### 12.3 The scanner FUN_1801ea7e0 (l.325980, 849 lines, closure 13.1k)
An exact plane section, per raster line, of the tessellated surfaces offset to the tool centre (an inverse-offset model), not a
per-point tool drop.
- Arguments (asm 1801d01f0-1801d025b): kind, 1, FUN_1801cfb60's param_4 / 5 / 6, the frame (param_7-9), tol (param_10), 0, containment,
  .., number of lines (param_15), first line, step (param_16 / 17), ..; tolerances 0.9 / 0.05 / 0.009 / min(tol, 0.00009) x tol
  (l.326181-326186); line i at -p15 - i p16, processed from the last to the first (l.326209, l.326524-326528, l.326590).
- Tool: s = max(p3, -p4), normal offset 588 = p4 + s, horizontal 590 = (2 s + p5) / 2 - 588; **590 > 0.009 tol -> error -16**
  (l.326224-326248: only a ball tool takes the scanner; others fall back to FUN_1801d6390), else 5a0 = s + p5 / 2 (l.326250-326258).
- Phase A per surface (l.326312-326378): FUN_1801e7e20 (edges / corners -> ERRCHK.tmp index), FUN_1801e8360 (l.324442, 900 lines):
  tessellation by mccore setup_ / fill_ / inquire_mach_model_buffer, vertices offset by FUN_1801e99f0 (l.325363), triangles + normals
  -> ID2371.tmp. DAT_180501d4c == 1 adds flipped / side passes (l.324767-325329; meaning NOT DECODED).
- Phase B per line (l.326383-326600): FUN_1801daa60 (activate the primitives near the plane), FUN_1801db8c0 (l.316469: section triangles
  FUN_1801dba80 -> FUN_1801dbc50, edges FUN_1801ed160, corner spheres FUN_1801ec6b0), pieces (0x58, FUN_1801e40c0) merged into the line's
  upper envelope by FUN_1801e7350 -> **FUN_1801e5200** (l.322473, 827 lines, NOT DECODED), cleanup FUN_1801e02b0, block writer
  FUN_1801e0680 (l.319476), vertical walls -> plmvcs00.tmp (FUN_1801ed9b0).
- Output: the scan file of 12.2 step 1 (blocks of 0x30 points + 0x18 trailer, l.319519-319627; trailer l.326693-326717).

### 12.4 FUN_1801d3ca0 (l.311384, 466 lines; the 0x2b path is about 57.7k lines with its branches, ~49.7k beyond stage 1)
Reads the recut file (FUN_18025d6d0(2, ..), l.311471): record 0 = kind 6 (FUN_18015da80 + FUN_180186bb0 l.262163 -> the globals), record 1
= kind 7 (kind 0x11 branch l.311510: FUN_18018f780 builds the two-direction context). Then:
1. FUN_18018e630 (l.266927, closure 6.5k) twice (asm 1801d47c0-1801d482a): rebuilds both directions into tmpemesh.tmp.
2. (z limits only) FUN_18018f0d0 x 2.
3. **FUN_1801b1ad0** (l.288723, closure 54.9k): the collapse / zone core - FUN_18018a640, FUN_1801a7fd0, FUN_1801b88e0 per direction,
   FUN_18015d0f0 (two-direction iteration), FUN_180179cc0 (containment) or FUN_180186ff0, the pass loop FUN_180189a90 (core
   FUN_180187850) -> FUN_1801c16d0 -> FUN_1801597b0 -> FUN_180192330 while the step < colaps_dist (l.289075-289095); files tmpsurf,
   tmpzone, tmpdzmap, tmpdzdat. Geometry NOT DECODED.
4. FUN_180141e20(1, ..) (l.311796): cut.tmp + msproj.tmp; **FUN_1801b0df0** (l.288292, closure 10.8k): zone order (mode from
   msurf_opt_cut_order and op+0x12c0, asm 1801d4bc9-1801d4c30), nearest start, op-0x2b-only clip by tmpmm5 (l.288537-288647), per pass
   FUN_180196ec0 -> cut.tmp / msproj.tmp records.
5. FUN_180140600, FUN_18023c390, FUN_180260860.
NCI: FUN_180136670 (l.203958, 2267 lines, closure 4.9k): FUN_180141e20(2) reopens cut / msproj, link pieces FUN_180293a50 /
FUN_180292d50, moves through FUN_18012ea90 -> **gr_lin_mill**, FUN_18012e8e0 -> **gr_arc_mill**, bnci_wr_marker, wr_sof (mccore).

### 12.5 Sizes and port stages
| stage | entry | lines (closure) | output to check |
|---|---|---|---|
| A machine model | FUN_18012a310 / FUN_180229ba0 | 18.8k (+ mccore make_surf_model) | tmpmm / tmpmm2 / tmpedg0 / tmpcrn0 |
| B scanner | FUN_1801ea7e0 | 13.1k | plmscn00 (-> tmpedg2), plmvcs00 (-> rufvcs*) per kind |
| C per-line | FUN_1801cfb60 loop | ~6k beyond B | temp.rgn records 1 (kind 7) / 0 (kind 6) |
| D collapse / zones | FUN_1801d3ca0 -> FUN_18018e630, FUN_1801b1ad0 | ~45k | tmpemesh, tmpzone |
| E order / cut records | FUN_1801b0df0 | 10.8k | cut.tmp, msproj.tmp |
| F links + NCI | FUN_180290fb0, FUN_180136670 | 15.8k (shared by the classic finish ops) | NCI |
The port goes stage by stage against the DLL's own intermediate files for the same input (12.7), B first: its output is a plain
geometric fact (the section of the offset model per line), testable alone.

### 12.6 Oracle op
CURVE 5 AXIS REVIEW FILE op 2 (2017 / 2018 / X9): op 43, 6 NURBS surfaces (kind 0) as TP kind 4 refs, tool D 0.75 ball (TL_ID key 5),
slice 0.05; the stored NCI is 3 rapids and 25448 feed moves (708.8 in): concentric loops from the centre outward joined by short feed
links (measured 2026-11-13 with tools/mcam_ops.py).

### 12.7 The DLL oracle (tools/peloader/mcmsurf_lmm.c, `peloader mcmsurf.dll --lmm CASE OUTDIR`; case = tools/lmm_case.py PART OPID)
Mastercam's own op pipeline on one corpus op, mcmsurf.dll + mccore.dll (+ systypes, mcce) mapped, mcmsurf's DllMain run (its static
SysFile objects), mccore's not (nc_file constructed by hand). OUTDIR is GetTempDirectory, so every temp file of 12.1-12.4 lands there, and
the NCI goes to OUTDIR/op.nci through mccore's own nc_file. The glue (all in the file's header comment): tp_get_all / tp_get_assoc_ent /
get_ent_from_eptr / ValidateGlobalCurOp from the case's records, mcmill operation_manager / tool_manager (mode 5 = the case's op / tool),
Mastercam.dll as Top view only with no display, GetMSurfResourceHandle's cache set (MCMSurfRes.dll is not loaded). Added for it: MFC
CString ordinals 12380 / 1499 / 12417 / 8299 / 2857 / 1665 / 4458 and host stdio (_fsopen, fread, fseek, fprintf, ...) in mcce_rt.c,
string / mbs / misc CRT functions in tools/peloader/msvcr_more.c (consulted only by --lmm).
First version (2026-11-13): bnci_surface itself - stopped at its product / licence check FUN_180012d60 (l.12786, Mastercam.dll
GetProdEnableBits), TODO SC-1. **User decision N13-22**: enter below bnci_surface, the harness repeating its setup; never answer, patch,
stub or emulate the licence check; stop at any licence code on the path. The licence imports (GetProdEnableBits, product_level_ok,
product_level_ok_no_warn, get_mill_level, get_router_level) now end the run (exit 5) with the calling address; nothing answers them.
Further glue for the repeated setup: write_ent_sel (the image copied back), set_clr_sel_bits_all(0, b, 0, b, classes) (bit b cleared on
those classes; other forms stop the run), mccore's own entries of tp_get_all / tp_get_assoc_ent / write_ent_sel / ValidateGlobalCurOp
patched to the same answers, mcgeomsld.dll absent (no solids).

**The setup lmm_setup repeats** (tools/peloader/mcmsurf_lmm.c; bnci_surface lines of decomp/mcmsurf_all.c; any path the op would take
that is not listed stops the run with "lmm: not repeated: ..."):
| step | bnci_surface | harness |
|---|---|---|
| msurf_use_alt_solid_proc = (type == 0x84) | l.11904 | same |
| mutlSettingConfigurationToleranceSanityCheck() | l.11906 | called |
| cur_op +0 = op id; post_args[0x101] = 0 | l.11908-11909 | same |
| operation_manager(cur_op, 5) | l.11912 | the case's op |
| cur_op +0xa8 = 0, +0xac = 0 | l.11913-11914 | same |
| operation_manager(.., 2); ActivePlaneSaveRestore x 2; ValidateGlobalCurOp | l.11915-11920 | not repeated (write back; Top view only; the op exists) |
| read_appmch_cfg; msurf_access_ridge_test(2) if msurf_disable_sharp; FUN_180019250 | l.11933-11938 | called |
| internal_sharp_angle_threshold = msurf_sharp_corner_angle (restored at the end) | l.11939-11942, LAB_180012b69 | same |
| multiaxis op / cur_op +0x56a, +0x568 cleared | l.11943-11949 | multiaxis refused; the clear repeated |
| FUN_18001ad50 | l.11950 | called |
| **FUN_180012d60 (licence)** | l.11951-11952 | **not run (N13-22)**; its output local_6c7c is read only at l.12473 (that branch is not taken) |
| tool +0x264 = cur_op +0x544; tool_manager(tool, 5); FUN_18001ffd0; FUN_180020960 == 0; FUN_1800205e0 == 0 | l.11953-11962 | the case's tool; called; non-zero refused |
| msurf_use_fzt decision | l.11963-11975 | same expression |
| msurf_use_emm decision (mask 0x1f80000079e0 / 0x6d ops) | l.11976-12015 | same expression; tool types 0x19 / 0x17 / 0xe and other op types refused |
| fzt / emm switch for tool 0x17 or op 0x6b + tool 0x19 | l.12016-12026 | refused |
| tp_get_all; op_is_msurf; cur_op_ptr; cur_op = the returned op | l.12046-12098 | same (the case's records) |
| make_tool_shape; msurf_map (FUN_180205a90) | l.12099-12100 | called |
| DAT_1805034a2 = 0; FUN_180045920(type in 0x2d / 0x70 / 0x6e) | l.12107-12114 | same |
| allow_solid_surfaces(_assoc) = 0; close_solid_surfs_temp_file; solid STL refs | l.12115-12160 | solids refused |
| DAT_1805034a1 = 0; DAT_1804e3751 = 1; surf_sel_bits(tps, n, 0, 0, 0, 0, &drive, &check, &ok); DAT_1804e3751 = 0 | l.12162-12197 | called (surf_sel_bits_c5 / _MM ops refused) |
| check surfaces (DAT_1805034a1 = 1, ...) | l.12202-12212 | refused |
| FUN_180021100; the third-party algorithm flags | l.12254-12267 | called; third-party ops refused |
| drive count 0 or = check count | l.12272-12283 | refused |
| has_tcbs / has_curves / blend -> chain_regen + surface_manager | l.12289-12296 | not repeated: ops with chain TP records (kind 6 / 7) refused, so chains = NULL |
| FUN_1800202d0 | l.12299 | **not run** (holds licence queries, l.23086ff); without check surfaces it returns 0 at l.23077 |
| op 0x84 steps | l.12303-12330 | refused |
| start / 4-axis / entry / hmm points, points, grid, radial ops | l.12331-12470 | refused |
| local_6c7c branch | l.12473 | not taken (see FUN_180012d60) |
| fzt tool test; stock test | l.12519-12556 | same; negative stock beyond r + tol refused |
| op_is_hmm_with_rest_material, type checks 0xd / 7 / 0x2c / 0x2d / 10 / 0x83 | l.12560-12651 | rest-material and those types refused |
| make_unique_recut_filename if cur_op +0x9b9 empty; FUN_180266820(cur_op +0x9b9) | l.12655-12665 | called |
| FUN_1800135a0(op, NULL, NULL, &ok); FUN_1802244d0 | l.12692-12694 | called (NULL chains / start point: asm, rdx = local_6c68, r8 = local_6c40) |

**Status 2026-11-13 (N13-22 version)**: on CURVE 5 AXIS REVIEW FILE op 2 the setup runs through (drive 6, check 0, msurf_use_emm 0,
msurf_use_fzt 0; one MCAssert "DbLoIO.cpp:401 eptr_valid" from mccore on the way, not traced) and FUN_1800135a0 starts. Its first
step FUN_18029e890 (l.436954) calls mccore alloc_nc_all, which allocates mccore's NC work buffer once in mccore 180020050 (asm): size =
0x80000 bytes if GetProdEnableBits() & 6 else 0x117fba0 - **a licence query on the path**, reached from mccore 0x180020069 (stack: mcmsurf
180013823 FUN_1800135a0 -> 18029e8b1 FUN_18029e890 -> mccore 1800201d5 -> 180020050). Per N13-22 the run stops there: no stage file,
no NCI. Every stage needs that buffer (edge_setup / pl2MemBufSetup in FUN_18029e890; the scanner's pl2MemBufRemainFit / edge_mark), so
no entry point of the engine avoids it. Nothing was worked around (the buffer is not pre-allocated). TODO "Waiting on the user" SC-2.
A run: `python3 tools/lmm_case.py PART 2 > c; peloader ~/mastercam-re/files/mcmsurf.dll --lmm c out/` -> "lmm: STOP - a licence query
was reached from mccore 0x180020069".

### 12.8 Where this stopped (2026-11-13) and what is next
**N13-23 (user, SC-2)**: no DLL stage oracle (the harness stays stopped at mccore alloc_nc_all; no licence answer, stub or buffer
pre-allocation). The port goes on from the code: stages B-F (12.5), each checked by internal consistency, then the op end to end
against the stored NCI of CURVE 5 AXIS REVIEW FILE op 2 (25451 moves). Op 43 stays greyed until it generates fully decoded, links
included (an undecoded linker keeps it greyed, as N13-19).

**CORRECTION (2026-11-13, user):** the earlier plan here took our own tessellation in place of stage A. That is a stand-in and is NOT
allowed (the rule of N12-1 for surf_to_stl / make_surf_polygons: decode it, no stand-in). Stage A comes first: mccore make_surf_model
and the machine-model buffer readers the scanner uses (setup_ / fill_ / inquire_mach_model_buffer) are decoded and ported, checked against
mccore.dll itself under tools/peloader (12.9: make_surf_model runs there without any licence code - the unit oracle `--msm`). Stage B
starts only after stage A. The stage B plan below stands, its input being the ported machine model.

**Stage B plan (after stage A, 12.9)** - the scanner FUN_1801ea7e0 (12.3), per kind (7: frame turned 90 deg, 6: not):
1. Read, in this order: FUN_1801e99f0 (l.325363, vertex offset to the tool centre: + 588 n + 590 n_xy / |n_xy|, ball tool: 590 = 0),
   FUN_1801dbc50 (l.316600, 457 lines, triangle / plane section), FUN_1801ed160 -> FUN_1801e1ac0 / FUN_1801e2030 (edge pieces),
   FUN_1801ec6b0 (l.326830, corner spheres with the arc tables FUN_1801da800 / FUN_1801d9e20), FUN_1801e7350 -> **FUN_1801e5200**
   (l.322473, 827 lines, the upper-envelope merge of the pieces into the line's 0x68-node list), FUN_1801e02b0 (cleanup:
   FUN_1801e9ec0 short nodes, FUN_1801e3190 runs, FUN_1801e23f0 collinear merge), FUN_1801e0680 (l.319476, the block writer) and
   FUN_1801ed9b0 (vertical walls -> plmvcs00).
2. Port as viewer/cam3d_cscallop.c (new; output = the plmscn00 / plmvcs00 records of 12.2 step 1 in memory, per line).
3. Internal checks (no DLL oracle): every point of a line lies on its plane (x = line position in the frame); z is non-decreasing
   under the envelope (a ball of radius r at (u, z) does not cut into any triangle of the offset model: sampled check); the nodes of a
   line are ordered by u and joints within 0.009 tol carry tag + 0x27 (l.319563); the number of lines = n of 9.1 step 4.
4. Against the stored NCI: Mastercam's passes are constant-scallop loops built later (D), so B alone cannot be compared move by move;
   what the NCI does fix is the tool-centre surface: every stored feed point must lie on (within tol of) the envelope B computes at its
   (x, y) - checked by sampling the NCI points against the nearest scan line.
Then C (12.2's per-line stages, checks: the 0x18 / 0x30 record formats round-trip, joint codes), D (FUN_1801d3ca0's FUN_18018e630 /
FUN_1801b1ad0, still to be read), E (FUN_1801b0df0), F (FUN_180290fb0 links, FUN_180136670 NCI).

### 12.9 Stage A: the machine model - mccore make_surf_model (decoded and ported 2026-11-13, part 1)
Sources: decomp/cscallop/msm1.c .. msm4.c (DumpDecomp of mccore: make_surf_model, the mach_model_buffer exports, check_for_ridge,
FUN_18057dd40, FUN_1805858b0 and callees; gitignored), objdump of mccore.dll (FUN_18057a980 18057a980..18057e440, make_surf_model
18058dc30..18058de1a, FUN_18057dd40, FUN_18058e180), decomp/partline/core_tess.c / core_evalcache.c (the make_surf_polygons port's dumps).

**What the machine model is.** mcmsurf FUN_18012a310 (l.197248) -> build_machine_model(0x15c, msurf_chord_dev, crad, dia, 0, 0, 0, 1,
stock, chk, frame ..) (l.197413; for op 0x2b: crad = op+0x2f0, dia = op+0x2e8, neither EMM nor fzt) -> FUN_180229ba0 (l.363767): tol =
chord x 0.9 (l.364101), the raster frame (Top view turned by cos / sin(pi / 2), l.364023-364060), the header file tmpmmx.tmp, then per
drive surface (TP kind 4) make_surf_model (l.364700) - or, when check_for_ridge(ent, 5.0) holds (only with param_4 == 0, the case of op
43), the same with d = h = 0, tol x 0.5 and mms +0x68 = 2 (l.364684-364698). Per surface the files: tmpmm.tmp (the 0x78-byte
mach_model_surf), tmpmm1.tmp (u params nu + 1, v params nv + 1, the int n21, the loop table and the loop points, l.364718-364733),
tmpedg0.tmp (0xb0-byte boundary edge records), tmpcrn0.tmp, tmpmm2.tmp. **No triangle is stored**: the scanner's phase A (12.3,
FUN_1801e8360) rebuilds them through setup_mach_model_buffer (reads tmpmm1) / fill_mach_model_buffer (FUN_18057f490 / FUN_18057db70
re-evaluate point + unit normal per grid / loop point by FUN_18057a510, FUN_18057ebd0 / FUN_18057ece0 / FUN_18057eeb0 / FUN_18057f3c0
make the cells' triangles; kind 3 (STL) models read tmpstl.tmp, FUN_18057f6c0). So stage A = the grid (u / v parameters, loops) and the
records, and the buffer readers.

**make_surf_model = FUN_18057a980, the make_surf_polygons engine** (wrapper 18058dc30: no own code). Arguments (asm; in FUN_18057a980's
frame): ECX = EDX = -1 (no progress text), R8 = 0, R9B = 1 (rot_view into the frame), in_stack_28 / 30 / 38 = frame rows f1 f2 f3, 40 = d
(normal offset: crad + stock), 48 = h (dia / 2 - crad), 50 = c (a second shape, 0), 58 / 60 = its d2 / h2, 68 = r (1 for op 43), 70 = 0
(no uv out), 78 = 1 (normals out), 80 = ent, 88 = 90 = tol, 98 = a0 = 0, a8 = &mms, d0 = 3 (boundary edges to tmpedg0), d8 = tmpedg0's
SysFile; the outputs (point / normal arrays, counts, the loop table / points) as listed in tools/peloader/mccore_stl.c MSM.
Differences from make_surf_polygons (plm_mesh, which ports the d = h = 0 arm):
1. tol <= d or tol <= h: the side test FUN_18058abd0 (core_tess.c l.5709; asm 18057b0f0..18057b15d): eval_surf order 1 +
   calc_surf_unit_norm at the surface's (u0, v0) (+8 / +0x18; trimmed: the outer loop's box), normal through rot_view; z > 1e-5 -> 1, z <
   -1e-5 -> 0, else the same at the range's middle (still flat: 0). local_2d87 (side) = (that == 0); the step arrays set A.
2. The steps: FUN_180581f00 on the kind 8 copy at distance (side ? +1 : -1) (3h + d) + base offset (asm 18057b3a6..18057b3b3), i.e. the
   offset surface - FUN_180460cd0 in full (port ev8_off: P = d n + S, the first and second derivatives of the unit normal, l.131-880).
   h > 2 tol adds FUN_1805851e0 (NOT PORTED: non-ball tools); c != 0 a second pair (NOT PORTED).
3. The counts / merge / parameters as plm_mesh; then (untrimmed, tol <= d; l.939-1063) **the grid outline as one loop** - u[0..nu-1] at
   v[0], v[0..nv-1] at u[nu], u[nu..1] at v[nv], v[nv..1] at u[0] - refined by **FUN_18057dd40**(1, tol, surf, 1 loop) (msm2.c l.127; asm
   18057c5bb): per segment (the last to the first included) eval_surf order 2 at 1 % and 99 %, **FUN_18058e180** (P1, P2, |du|, |dv| of
   those parameters (ANDPS), surf, u1, v1, tol): chord = |P2 - P1|, **FUN_1805858b0** the L / S ratio from both ends (as plm_mesh's
   corner_len: the march FUN_180589880 or surf_curv_dir, chord_of(|k|, tol) >= 2 tol; from P2 the march starts at (|du| + u1, |dv| + v1)
   going back), n = 1 when the ratio <= 1.02 else ceil; n - 1 points (q - p)(i / n) + p inserted. The loop is kept (one trim loop, mms
   +0x5c = 1) only when it gained points; the triangle count mms +0x50 = 2 nu nv + points - 2 (nu + nv) (local_2cb0, l.1067-1070). The
   arena room (edge_fit(0x10)) bounds the insertions in the DLL - its size comes from alloc_nc_all's buffer (12.7, GetProdEnableBits):
   not modelled (as plm_mesh's arena); the oracle's arena is mccore's poly space.
4. The vertex pass (FUN_18057a510 with param_6 = 1): eval_surf order 1 + calc_surf_unit_norm, both rot_view'd; the box (mms +8..+0x30,
   reset to +-1e38 each pass; with a loop only points inside it); the normal test (param_21 = local_2d86, param_22 = side): side 0 wants
   z > 1e-5, side 1 z < -1e-5, a point outside the loop never fails. The passes (l.1110-1393, LAB_18057cdd8): the first pass on side s
   flips to the other side at its first failing point or at its end; after the second pass: no failure in the first -> mode = (s2 ? 0 : 2)
   (1 when d, h < tol); a failure in the first but none in the second -> the first side's steps recounted, mode = (s2 ? 2 : 0); failures
   in both -> per span the steps with the larger count, mode 1, recounted. mms +0x64 = mode, +0x48 / +0x4c = nu / nv.
5. The boundary edges (in_stack_d0 = 3; l.1598-1616): FUN_18059c040 per outer cell edge (core_tess.c l.6083: the plane through the
   edge's two grid points and a third, its normal kept when the normals agree, 0.995 / -0.999847 tests) -> FUN_18059c520 (msm2.c l.727) a
   0xb0 record (+0 P1, +0x18 i1, +0x20 P2, +0x38 i2, +0x40 the plane normal, +0x58..+0x70 the parameters, +0x78 N1, +0x90 N2, +0xa8 a
   facing flag) to tmpedg0; their count -> mms +0x54. NOT PORTED yet.

**Port** (viewer/mcpartline_mesh.c `plm_model`, header mcpartline.h; ev8_off, seg_ratio, refine_outline): untrimmed kind 0 surfaces,
c = 0, h <= 2 tol, r != 2. Out: u / v parameters, loop, mode, box, triangle count. Refused (NOT PORTED): kind 10 (FUN_18057dd40(0, ..) on
the trim loops and the trimmed vertex pass), other kinds, h > 2 tol, c != 0, r == 2, the degenerate surfaces plm_mesh refuses.

**Oracle** `tools/peloader mccore.dll --msm CASE` (tools/peloader/mccore_stl.c MSM / RIDGE; case tools/msm_case.py PART OPID for an op,
`--all PART [--d D1,D2] [--max K]` for every visible kind 0 surface at the given ball radii): mccore's own make_surf_model on the
part's records, the arena = mccore's poly space (init_poly_space(0) + FUN_1806fc170, as make_surf_polygons) - not the alloc_nc_all
buffer (N13-23). Licence queries (GetProdEnableBits, product_level_ok(_no_warn), get_mill_level, get_router_level) stop the run (exit 5);
**none is reached** by make_surf_model or check_for_ridge. Port tool viewer/msm_port (make msm_port), compare tools/tests/msm_cmp.py
(nu, nv, triangle count, mode, nloops, box, every u / v / loop parameter bit for bit), corpus run tools/tests/msm_oracle.sh OUT LIST.
Results: CURVE 5 AXIS REVIEW FILE op 2 (6 drive surfaces, tol 0.0009, d 0.375): **6 / 6 identical** (surface 0: nu 47, nv 128, mode 1).
Corpus: see 12.10.

**NOT DECODED / not ported in stage A (next):** check_for_ridge -> check_surf_for_internal_sharps -> FUN_1806f5510 (msm3.c: per internal
u / v knot of full multiplicity (FUN_1806f4b90) the calc_surf_norm angle across it (+-1e-6) against 5 deg; Ghidra shows the second
calc_surf_norm at u + 1000000.0 and a fall-through from kind 0 into kind 1's case - both to be read in the asm) - the oracle's RIDGE
answers it per surface; the boundary edges (5.); tmpcrn0 / tmpmm2 / tmpmmx (FUN_180229ba0 l.364101-365600, FUN_180225000,
FUN_180220fe0); trimmed surfaces; the buffer readers the scanner calls. Then stage B (12.8).

### 12.10 Stage A oracle results and where this stopped (2026-11-13, session 3)
- `tools/tests/msm_oracle.sh OUT LIST --max 20` over the 533 corpus parts with visible kind 0 surfaces (tol 0.0009 in, ball radii 0.375
  and 0.125 in, x 25.4 for metric parts): **4283 surface runs bit-identical** to mccore.dll (u / v parameters, outline loop points, nu,
  nv, triangle count, mode, nloops, frame box), **0 different**, 12 refused - ROUGHPROJNCI's degenerate surfaces (calc_surf_norm fails
  in the offset evaluation: the DLL then works on stale stack values, NOT DECODED, as plm_mesh / interfacial.md 12.4).
- check_for_ridge (RIDGE): 4295 surfaces, 30 with a ridge, **all 4295 answers identical** (plm_ridge). CURVE 5 AXIS op 2: no ridge on
  its 6 drive surfaces, so the l.364700 arm (d = 0.375, h = 0) is the one Mastercam takes - and its 6 grids are identical.
- No licence code is reached by make_surf_model / check_for_ridge (every licence import stops the oracle; none fired).
- Stopped here. Stage A still to do, in this order: (1) the tmpedg0 boundary edges (12.9 item 5; the oracle already prints them as
  "W 176" lines - compare byte for byte), (2) mcmsurf FUN_180229ba0's files around make_surf_model (tmpmmx header l.364103-364145 read
  in outline, tmpmm / tmpmm1 / tmpmm2 / tmpcrn0, FUN_180225000, FUN_180220fe0), (3) setup_ / fill_mach_model_buffer and the cell
  triangles (FUN_18057ece0 / FUN_18057eeb0 / FUN_18057f3c0) - the triangles stage B's scanner sees; oracle: call them in --msm after
  the model, (4) trimmed (kind 10) surfaces and non-ball tools, refused until then. Then stage B (12.8).
- Regression (viewer code changed: mcpartline_mesh.c's new paths are gated, plm_mesh unchanged): against a build of this branch's base
  67f5ba0 (master cbd1dd2 + the previous Constant Scallop branch), op_compare.sh 47 / 635 differ on both, the same 47 scenes;
  fileop (205 stored ops of the user's parts) 0 different.

### 12.11 Stage A part 2: tmpedg0 edges, tmpcrn0 corners, tmpmm / tmpmm1, the buffer readers (2026-11-13, session 4)
Sources as 12.9, plus objdump of mccore 18059c040..18059c674, 180592030..180592f60, 1805933e0 (read_edge_full), and of mcmsurf 180224ec0,
180225000..1802256bf, the FUN_180229ba0 call sites 18022a2fe..18022a31c / 18022b407..18022b4a8 / 18022bbf0..18022bc4e.

**1. The tmpedg0 edge records (make_surf_model with in_stack_d0 = 3).** FUN_18059c040 (core_tess.c l.6083; the sums in the DLL's order from the
disassembly): a = P[i2] - P[i1] (|a| = sqrt((ax^2 + ay^2) + az^2)), b = P[i3] - P[i1] (|b| = sqrt((by^2 + bx^2) + bz^2)); the plane normal is 0
when tol (mccore ?tol@@3NA, 0x1812b5b58 = the system tolerance) > |a| or > |b|, when (N1y N2y + N2x N1x) + N1z N2z < 0, or when
|((by ay + bx ax) + bz az) / (|b| |a|)| > 0.995; else n = b x a (n0 = by az - bz ay, n1 = bz ax - az bx, n2 = ay bx - by ax), m = a x n
normalised (FUN_18000f2f0: / sqrt((x^2 + y^2) + z^2), 0 stays 0), d1 / d2 = m . N1 / m . N2 (each negated when that normal's z < 0), k = i2
when d2 > d1 else i1; |a . Nk / |a|| > 0.995 -> 0; else q = Nk x a normalised, turned to m's side (q . m < 0 -> -q), 0 when q.z < -0.999847.
Count +1 (mms +0x54), FUN_18059c520 writes the 0xb0 record (disassembly 18059c537..18059c65b; layout in viewer/mcpartline.h PlEdge: +0 P1,
+0x18 i1, +0x20 P2, +0x38 i2, +0x40 q, +0x58 / +0x60 / +0x68 / +0x70 = *param_2 / *param_3 / *param_4 / *param_5, +0x78 N1, +0x90 N2, +0xa8
flag = 0 when the three vertex normals all have z > 1e-5 or all z < -1e-5, else 1; +0x1c..+0x1f, +0x3c..+0x3f, +0xa9..+0xaf are stack bytes).
Who calls it (FUN_18057a980's cell loop, cells u outer v inner, b = i (nv + 1) + j): without trim loop (local_2d7c = 0) each border cell, in
the order bottom j = 0 (b, b + nv + 1, third b + nv + 2; u[i] v[0] u[i + 1] v[0]; call 18057d3e0), right i = nu - 1 (b + nv + 1, b + nv + 2,
third b; u[i + 1] v[j] **u[i]** v[j + 1] - the DLL passes u[i] as the second u, 18057d44e..18057d46c), top j = nv - 1 (b + nv + 2, b + 1, third
b; u[i + 1] v[j + 1] u[i] v[j + 1]), left i = 0 (b + 1, b, third b + nv + 2; u[0] v[j + 1] u[0] v[j]). With the outline loop (local_2d7c = 1)
the border cells (i = 0, i >= nu - 1, j = 0, j >= nv - 1) go through FUN_1805913b0 / FUN_180592030 (the trimmed-cell path of
make_surf_polygons, ported in mcpartline_trim3.inc) and the ears write the edges (1805923d9..18059271a): per accepted ear (pv, cur, nx) its
sides (pv, cur), (cur, nx), (nx, pv) in that order, each when both vertex indices are loop points (>= the grid count) and the second follows
the first along its loop (the loop's last point followed by its first), parameters = the two piece points' (u, v), third = the ear's
other vertex. Inner cells write none.
**Small arena**: when the arena has no room for the whole grid (edge_fit((nu + 1)(nv + 1) + loop points) x 0x30) < 1, msm1.c l.1891-1906)
make_surf_model keeps only the border points (a different vertex numbering in the records). In Mastercam that arena is the alloc_nc_all
buffer whose size comes from GetProdEnableBits (12.7, N13-23); the port assumes room, as plm_mesh (make_surf_polygons) does.
Ported: viewer/mcpartline_mesh.c model_edges, mcpartline_trim3.inc edge_rec / ear_edge (PlModelIn.edges = 1: also the vertex arrays of the
last pass, PlModel pts / nrm). Oracle: the --msm runs print the records as "W 176" lines (SysFile::Write); tools/tests/msm_cmp.py compares
them byte for byte but the stack bytes, and the vertex arrays bit for bit.

**2. FUN_180225000 + FUN_180224ec0 (tmpedg0 chained, tmpcrn0)**, called by FUN_180229ba0 when mms +0x54 != 0 (18022bc4e) with (ne, nv, pts,
nrm - reused as an nv x 0x18 table -, f1, f2, f3, tol = chord x 0.9 x 0.01 (18022a2fe..18022a31c: 0.9 at 0x1803eb818, 0.01 at 0x1803f3ef0),
tmpedg0 positioned at mms +0x3c, tmpcrn0, &chained); read_edge_full (mccore 1805933e0) gives P1 P2 q N1 N2 (0, 0, -1), i1 i2, the four
parameters, the flag. The walk, the rewrite and the corners are described in viewer/cam3d_mmodel.c's header (mm_corners); the 0x70 corner
record (FUN_180224ec0, disassembly 180224ec0..180224ff5): +0 vertex, +8 P, +0x20 d1, +0x38 d2 (each (0, 0, -2) - constants 0x1803eba90 /
0x1804183d0 - when every |component| <= tol), +0x50 N, +0x68 flag; after the corners, (ne - corners) more copies of the first corner with
flag 1. Not chained (or no edge) -> mms +0x74 = 0 (LAB_18022bc61).
Ported: viewer/cam3d_mmodel.c mm_corners. Oracle: `peloader mcmsurf.dll --lmm CASE OUTDIR` unit mode (CRN lines, tools/peloader/
mcmsurf_lmm.c): mcmsurf's FUN_180225000 itself on the DLL's own edge records from the --msm output, real SysFile files in OUTDIR; no licence
code, no NC buffer on that path. tools/tests/crn_cmp.py CASE DLL_OUT PORT_OUT WORK. Glue fix for it: mccore's SysFile::VerifyFileOpen tests
the MSVC FILE's _flag (+0x18), which is glibc's _IO_read_base - c_fsopen (mcce_rt.c) now gives the FILE its buffer at once (setvbuf).

**3. tmpmm / tmpmm1 and the buffer readers.** FUN_180229ba0 per surface (l.364715-364768, 18022b9fc..18022bc6f): tmpmm1 at the current offset:
u (nu + 1 doubles), v (nv + 1), the int n21 (make_surf_model's vertex count), with loops the table (nloops x 8) and the points (16 bytes,
count = last start + last count + 1); the 0x78 tmpmm record = make_surf_model's mms with +0x40 = that tmpmm1 offset (8 bytes), +0x58 = the
loop point count, +0x68 = 2 on the ridge arm, +0x74 as 2. above (+0x3c the tmpedg0 offset). setup_mach_model_buffer (mccore 180598790,
msm1.c l.154; mcmsurf's scanner calls it at l.324640 with param_1 = 1) reads record k (FUN_1805935c0 through MCMSurf's mutlFileOpenE) and
tmpmm1 (m_fopen_hl), FUN_1805798b0 (msm1.c l.2599) sizes the blocks from the arena's room (DAT_1813fbec8): the whole grid when
(ntri + ((nu + 1)(nv + 1) + loop points) x 4) x 0xc fits, else whole u rows, else smaller. fill_mach_model_buffer (18057e9c0) per block:
FUN_18057db70 (the loop points; none without loop), FUN_18057f490 (FUN_18057a510 per grid point of the block, u outer v inner, through the
caller's frame - the scanner's DAT_1804ea500..530, rot flag DAT_1804ea4f2), FUN_18057ebd0 -> per cell FUN_18057f3c0(buf, 1, 0) without loop:
(b, b + nv + 1, b + nv + 2), (b, b + nv + 2, b + 1), block-local indices, no orientation flip; with the outline loop (mms +0x5c = 1) the
loop points are evaluated by the first call (FUN_18057db70, msm1.c l.718) after the block's grid points (base = buf +0x64, the block's grid
count) and the border cells go to FUN_18057ece0 (FUN_1805913b0 with nloops = mms +0x5c, outer loop = mms +0x60) and FUN_18057eeb0 (msm2.c
l.452): FUN_180592030's ear clipping in resumable form, but after an ear it keeps a8 (pv) and only moves ac / b0 - fans, not the
make_surf_model ears (mcpartline_trim3.inc cell_tris_fill).
Ported: viewer/cam3d_mmodel.c mm_record / mm_mm1_bytes (the files) and mm_fill + mcpartline_mesh.c plm_model_tris (untrimmed models with
or without the outline loop, the blocks joined to the whole grid). Oracle: `peloader mccore.dll --msm` MMB lines (tools/peloader/mccore_stl.c): mccore's own setup_ / fill_mach_model_buffer read
the files the port wrote (viewer/msm_port with MSM_MMDIR) - the readers run without licence code; glue: GetTempDirectory = the case's
folder, MCMSurf's mutlFileOpenE = SysFile::Open (mcmsurf l.375192), m_fopen / m_fclose = host fopen / fclose, MFC CString ordinals 5558
GetLength, 1623 GetAt, 5593 / 305 / 2875 (operator+), 7907 Mid, 4846 as 1634 (mcce_rt.c). tools/tests/mmb_cmp.py CASE WORK joins the DLL's
blocks (the oracle's poly-space arena gives one-row blocks on large grids) and compares triangles and points / normals bit for bit.
**Open for stage B**: the scanner consumes the blocks; their size is the arena's (the licence-dependent alloc_nc_all buffer in Mastercam),
so whether the scan result depends on it must be read in FUN_1801e8360 / FUN_1801ea7e0 (NOT DECODED yet).

**Results (2026-11-13)**: CURVE 5 AXIS REVIEW FILE op 2: 6 / 6 surfaces - 482 / 482 edge records, vertex arrays, chained flags, 482 corner
records and the rewritten tmpedg0 identical to mcmsurf's FUN_180225000, and mccore's fill output on the port's files identical (6192 points
/ 12032 triangles on surface 0, 47 DLL blocks). Corpus (msm_oracle.sh, 1364 parts, --max 20, radii 0.375 / 0.125): 4283 surfaces identical
(grid, loop, counts, box, **635,537 edge records**, every vertex), 0 different, 12 refused (12.10), 70 small-arena runs (the DLL's grid did
not fit the poly space) not compared. Sample of every 4th corpus part with surfaces (134 parts, 2424 MSM lines; crn_cmp.py / mmb_cmp.py):
the fill output 2424 / 2424 identical (outline-loop models included; the DLL's blocks - whole grid, row blocks, rows split over several
calls when the triangle store +0x60 is full - joined), tmpedg0 chain + tmpcrn0 2401 / 2401 identical, 23 small-arena runs not compared.

**Regression (session 4)**: against a fresh build of master 977df9e: op_compare.sh 47 / 635 differ on both, the same scenes; fileop (205
stored ops of the user's parts) 38 differ = the 37 Dynamic Mill ops and NAKTO-BRAKE-BRACKET op 3 (refused on both: Area Mill FUN_1802ef450)
- master's Dynamic Mill merge 977df9e, not in this branch's base 09d3900; the app's paths are unchanged here (plm_mesh calls cell_tris with
no edge output; plm_model's new outputs are asked only by msm_port).
**NOT DECODED / NOT PORTED after session 4**: the small-arena layouts (make_surf_model's border-only vertices, fill's smaller blocks) -
arena-dependent, see above; tmpmmx (header: 0x1c, the
surface count, the part name line, flags and the doubles chord x 0.9 x 0.01, chord, crad + stock, dia, ..., FUN_180243b20's 0x48 bytes, the
frame rows; then one 0x20 entry per drive / check surface, l.364103-364434) and tmpmm2 (the part name line, 0, -1, the 0x20 entries copied
from tmpmmx, the record count) are read but not ported (their readers are in stage B); trimmed surfaces (kind 10) and non-ball tools in
model mode (12.9); what the flag *in_stack_e8 means at the end (the oracle prints 0 even when the grid was stored).
**2026-11-14 (N13-30, formats/hst_linker.md 27.2)**: make_surf_model on trimmed surfaces (kind 10 over kind 0) ported for d, h < tol (the HST machine model, FUN_180224190), with the fill readers on trimmed models (every cell clipped, FUN_18057ebd0) - corpus 919 surfaces identical at d = 0; the loop points widen the box without the loop test (param_12 = 1, asm 18057cb79), corrected in the outline-loop path too. Kind 10 with tol <= d (FUN_18057dd40(0, ..)) stays NOT PORTED.
**Where this stopped**: stage A is complete for untrimmed kind 0 surfaces with a ball tool (the scanner's input = mm_fill, with or
without the outline loop), every piece checked against the DLL. Next: stage B (12.8: FUN_1801e8360 phase A first - it calls setup_ /
fill_mach_model_buffer and must say whether the scan depends on the block size - then FUN_1801ea7e0); trimmed surfaces / non-ball tools in
model mode when a corpus op needs them. Op 43 stays greyed.

### 12.12 Stage B part 1: the scanner's setup, phase A, the per-line triangle pieces (2026-11-14, session 5)
Sources: decomp/mcmsurf_all.c (l.N) and objdump of mcmsurf.dll: FUN_1801e8360 1801e8360..1801e9990 (the classification loop
1801e9209..1801e9358), FUN_1801daa60 1801daa60..1801db002 with its jump table at 1801db004, FUN_1801dba80 1801dba80..1801dbb62, the
comparators LAB_1801df340 / LAB_1801df2c0 / LAB_1801df300 / LAB_1801df2e0 / LAB_1801da1a0, the call of FUN_1801e8360 at 1801eb449..1801eb4ba;
.data: DAT_1804d5fb8 = 1000, DAT_180501d4c = 0.

**1. Does the scan depend on the reader's block split? (FUN_1801e8360, task step 1.)** FUN_1801e8360 runs once per fill block
(setup_mach_model_buffer, then fill_mach_model_buffer until done). Per block it offsets the block's vertices, sorts the block's
triangles and writes them to ID2371.tmp as **one ERRCHK group per block and pass** (0x20 bytes: xmin, xmax, byte offset, count, kind).
The scanner sorts all groups by xmin (MSVC qsort, LAB_1801df2c0) and per line activates the groups' triangles in group order, each
pushed at the front of the active list (FUN_1801df620 -> FUN_1801da8e0); FUN_1801dba80 sections the active list in its order, and the
pieces go in that order to the batch of FUN_1801e40c0, which FUN_1801e7350 qsorts by the lower end (LAB_1801da1a0) before the envelope
merge FUN_1801e5200. So:
- the vertices, the triangles and the set of pieces of a line do NOT depend on the split (stage A: the joined fill output is the whole
  grid's, bit for bit; each triangle is sectioned on its own);
- the ERRCHK groups (their number, xmin / xmax, the triangle order in ID2371) DO depend on it, and through the activation order so
  does the order of the pieces handed to the batch. MSVC's qsort is not stable, so pieces with equal lower ends can leave FUN_1801e7350
  in a split-dependent order; whether FUN_1801e5200's result depends on that order is NOT DECODED yet (it is the next function).
- mode-1 models (both sides): the inquire_mach_model_buffer call is made on the first block only (l.324664), so on a later block
  local_3b4 keeps the first block's loop-point count - the vertex loop count differs from a one-block run (multi-block only).
- the scanner's own pool sizes come from the same arena: pl2MemBufRemainFit -> DAT_1804ea5e4 (capped 0x7fff) and the 500000 cap
  (l.326438-326452).
Per N13-28 the port assumes the large buffer: **one fill block per surface** (setup's FUN_1805798b0 takes the whole grid when
(ntri + ((nu + 1)(nv + 1) + loop points) x 4) x 0xc fits the arena room: 441 KB for CURVE 5 AXIS surface 0, against ~18 MB) and the
caps of the scanner reached. A model larger than the large buffer's room would split there too: not modelled (NOT PORTED).

**2. The scanner's setup (FUN_1801ea7e0 l.326181-326290)**, cam3d_cscallop.c cs_setup: 0.9 tol, (tol - 0.9 tol) / 2, e = 0.009 tol,
min(system tol, 0.00009 tol), the corner deviation; s = p3, or -p4 when p4 < -p3; normal offset p4 + s (off when <= e), horizontal
(2 s + p5) / 2 - (p4 + s): above e -> error -16 (not a ball tool: FUN_1801cfb60 falls back to FUN_1801d6390, NOT PORTED); else the normal
offset becomes (2 s + p5) / 2 for drive and check surfaces. Check surfaces (DAT_1805034a1) are refused upstream (12.7).

**3. Phase A, FUN_1801e8360 per block** (cs_phase_a): kind = 10 / 11 (normals; 4 / 5 without) for drive / check (param_6 = mms +0x66 != 1);
every vertex offset by FUN_1801e99f0 (the normal flipped when mode 2, or mode 1 and n.z < 0; a second flip in mode 4; P += off n, then the
horizontal offset when on); the triangles' indices ordered by x (FUN_1801db5c0), the triangles sorted by their first vertex's x (MSVC
qsort, LAB_1801df340), written (3 points, then the 3 normals through FUN_1801e7db0's flips) with their group. **Mode 1** (a model seen
from both sides) then: the offset taken back per vertex (FUN_1801ea5f0 - not bit-exact, the later passes start from P + a n - a n);
the triangles whose three normals are not all z >= 1e-5 / all z <= -1e-5 kept; for them three more passes and groups with
DAT_180501d4c = 0 (no flip), 2 (all flipped), 4 (the lower side); then the "edge quads": **the sign counters of the classification loop
are zeroed once before the loop, not per triangle (asm 1801e9214 / 1801e921b, bx and r11w)**, so only the triangles at which the running
count of positive (or else negative) vertices is exactly 2 are kept, reordered (the two same-sign vertices first) only when the write
slot differs from the read slot; per kept triangle the quad of its first edge offset to both sides (4 points, 2 triangles), sorted and
written as one more group whose normals go through FUN_1801e7db0 with DAT_180501d4c as the last triangle left it (0 or 2). Ported as the
DLL does it. After the block loop DAT_180501d4c = 1.

**4. Per line: the triangle groups and pieces** (cs_line_tris): pos / lo / hi start at x0, x0 - 2e, 2e + x0 and each gets += step
(l.326507, l.326590 - three separate sums, not recomputed). FUN_1801daa60 from the first live group (+0x118; +0x11c advances by one
dead group per line): a group with xmin > hi waits, one with lo > xmax dies; else (kinds 10 / 11, jump table entry 1801dac15) its
triangles from its file offset: v2.x < lo skipped, v0.x > hi stops the group (its xmin := v0.x), the others become active (pushed at
the front). FUN_1801dba80: an active triangle with v2.x < lo leaves the list; one strictly across the window (v0.x < lo, v2.x > hi)
is cut by **FUN_1801dc360** (v0-v2 and v1-v2 / v0-v1 interpolated, normals by FUN_1802535d0 = lerp + normalise, (0, 0, 1) when 0);
the others by **FUN_1801dbc50**: the edge crossings (xa <= p < xb or xb < p <= xa, edges shorter than e in x ignored); two -> the piece;
else nothing when p is beyond [xmin - e, xmax + e]; else the vertices within e of the line, two of them needed, ordered by y, extended
by the one crossing. Piece = 0x58 bytes: code (kind 10 -> 0, 11 -> 0x12, 12 -> 5, 13 -> 0x17), two (s, z, normal) ends, to
FUN_1801e40c0 (ends ordered by s, batch of 999 -> FUN_1801e7350). Edge groups (kinds 0 / 1: FUN_1801e7e20 sorts tmpedg0 by min x,
LAB_1801df300, and tmpcrn0 by x, LAB_1801df2e0, one group each; activation FUN_1801df420 / FUN_1801e0ae0, sections FUN_1801ed160) and
corner groups (2 / 3: FUN_1801df380, FUN_1801ec6b0) and triangles without normals (4 / 5, FUN_1801dbb70) are NOT PORTED: a line that
meets one is refused.

**Oracles (DLL unit modes; no licence code, no alloc_nc_all buffer on these paths).**
- `peloader mcmsurf.dll --lmm CASE` **SCN** mode (tools/peloader/mcmsurf_lmm.c; tools/tests/scn_cmp.py CASE WORK [--vtx K]): the DLL's
  FUN_1801daa60 + FUN_1801dba80 on the port's ID2371 / groups, line by line, DAT_180501d90 hooked to print each piece.
- **PHA** mode (tools/tests/pha_cmp.py CASE WORK): FUN_1801e8360 itself on the port's stage-A files (mccore's setup_ / fill reading them,
  poly-space arena as the --msm MMB oracle), ID2371 / ERRCHK compared byte for byte.
- viewer/cs_port (make cs_port) is the port's driver; the CS line gives the scanner's arguments (a unit-test input: p3 = 0.375,
  p4 = 0, p5 = 0, tol = chord 0.001 for the CURVE 5 AXIS model; FUN_1801cfb60's real arguments are stage C's to decode). Its lines are a
  test placement (the groups' x range in n steps, or every K-th vertex x and x +- e / 2), not Mastercam's (9.1 step 4).

**Read for the next step (not ported): the line list ("estore").** FUN_1801e5200 merges each piece into the line's list of 0x68-byte
nodes in DAT_1804ea4e0 (12.2's node layout: +0 flag, +8 s0, +0x10 z0, +0x18 n0, +0x30 flag, +0x38 s1, +0x40 z1, +0x48 n1, +0x60 code,
+0x62 next index); head DAT_1804ea5f0, tail DAT_1804ea5fc, free head DAT_1804ea600, free count DAT_1804ea604, search hint DAT_1804ea5f4 /
5f8 (FUN_1801da5d0, used when DAT_1804ea55f = FUN_1801e7cd0's argument, 1 here); LAB_1801e5190 (before the lines) chains all
DAT_1804ea5e4 nodes free, LAB_1801e4460 (after each line, DAT_180501da0) returns the line's nodes; FUN_1801e2a90 takes a free node
(none left: "ERROR: Ran Out of ROOM in ESTORE", DAT_1804ea60c = 2 -> the scanner's error -7). The capacity DAT_1804ea5e4 =
min(0x7fff, pl2MemBufRemainFit(..)) is arena-dependent (N13-28: large buffer); node indices do not reach the output, so only an
overflow would differ.

**Where this stopped (session 5).** Ported and DLL-checked: setup, phase A, the triangle pieces per line. Next in order: FUN_1801e7e20
(tmpedg0 / tmpcrn0 sorted per surface, LAB_1801df300 / LAB_1801df2e0, one group each, xmin / xmax widened by 588 + 590), the edge and
corner activations and sections, FUN_1801e7350 / FUN_1801e5200, FUN_1801e02b0, FUN_1801e0680, FUN_1801ed9b0. Op 43 stays greyed.

**Results (session 5).** CURVE 5 AXIS op 2 model (CS 0.001 0.375 0 0): phase A 5 / 6 surfaces byte-identical to FUN_1801e8360 (ID2371,
ERRCHK, offsets); surface 0 (mode 1, 6192 points / 12032 triangles, needs 441 KB) is split into blocks by the oracle's 400000-byte
poly-space arena, so not comparable (the port is the one-block run, N13-28). Per-line pieces: 40 lines (9739 pieces) and 1209
vertex-placed lines (305,967 pieces; branch coverage: span 290251, two crossings 11156, no piece 10683, two vertices on the line 4560,
extended by a crossing 16; "i2 below A" not hit) identical. Corpus sample (tools/tests/scn_oracle.sh, every 10th of 908 parts, 91
parts, 37 with surfaces): phase A 283 surfaces identical, 0 differ, 7 split by the oracle arena (159 mode-1 models of which 75 carry the
edge-quad group); 740 lines, 115,957 pieces identical; 0 failed runs. Regression: no app / cam_cli source changed in this session
(cam3d_cscallop.c / cs_port.c are linked only into viewer/cs_port; `make -C viewer -n all` does not reference them), so op_compare /
fileop were not rerun; stage A's numbers (12.11) stand.

### 12.13 Stage B part 2: the edge and corner primitives, the merge-order verdict (2026-11-14, session 6)
Sources: decomp/mcmsurf_all.c (l.N) and objdump of mcmsurf.dll: FUN_1801daa60's jump table at 1801db004 (kinds 0 / 1 -> 1801dacda, 2 / 3
-> 1801dae5d, 4 / 5 -> 1801dab67, 10 / 11 -> 1801dac15, 6-9 -> nothing), FUN_1801db8c0 1801db8c0..1801dba72, FUN_1801e1050 ..1801e14bb,
FUN_1801df770 ..1801dfd07, FUN_1801e1ac0 ..1801e1ed9, FUN_1801e0050 ..1801e0160, FUN_1801e7440, FUN_1801e2030, FUN_180274990,
FUN_1801ec6b0 ..1801ecfaa, FUN_1801e9990, FUN_1801db440, FUN_1801e4200, the comparators LAB_1801df300 / LAB_1801df2e0 / LAB_1801da1a0;
mccore UnitNormal (1804b7480), proj_pt_ln (180002780), proj_pt_ln_3d (1804b42c0), lp_ints (1804b3ed0). Ported in
viewer/cam3d_cscallop_edge.c (groups, activation) and viewer/cam3d_cscallop_sect.c (sections, arc table); cam3d_cscallop.c's
FUN_1801daa60 now takes every kind the op meets and cs_line runs FUN_1801db8c0 (triangles, edges, corners) up to its final FUN_1801e7350.

**1. The groups, FUN_1801e7e20 (l.324267), per surface before FUN_1801e8360** (called with ne = mms +0x54 and the tmpedg0 / tmpcrn0 offsets
mms +0x3c / +0x38, asm 1801eb38c..1801eb3bb; mms +0x74 is not looked at): nothing when ne = 0; the surface's ne tmpedg0 records are qsorted
(MSVC) by min(P1.x, P2.x) (LAB_1801df300) and written back, one group: xmin = min x of the first - (588 + 590), xmax = max x of the last
+ 588 + 590, kind = 0 drive / 1 check; then ne tmpcrn0 records (the corner pass writes as many, 12.11) by P.x (LAB_1801df2e0), one group
of kind 2 / 3. They go into ERRCHK before the surface's triangle groups, so the scanner's group qsort sees them in that order.

**2. Activation (FUN_1801daa60's arms).** Edges: per record in file order (offset += +8 = 0xb0): max(P1.x, P2.x) + 590 + 588 < lo ->
skipped; min(..) - (590 + 588) > hi -> the group waits there; else FUN_1801df420 then FUN_1801e0ae0. Corners (0x70 records): P.x
+ (590 + 588) < lo skipped, P.x - (590 + 588) > hi stops, else FUN_1801df380 (a 0x58 node: P, d1, d2, kind).
- FUN_1801df420 (l.318704): v = P2 - P1; a node is taken first (FUN_1801da400) and, for an edge along z (|v.x|, |v.y| <= e), dropped
  without a node (leaked from the pool; no output). a = v x (0, 0, 1), b = a x v (both normalised after). |v.x| <= e: FUN_1801e0d10
  (l.319727; kind 8 / 9: x range of P1 +- 588 a and P2 +- 588 a, no arc); else FUN_1801e1050 (l.319854; kind 0 / 1): the lines through
  P1 + 588 a, P1 + 588 b, P1 - 588 a parallel to the edge met with the plane x = P1.x (asm 1801e12a4..1801e1329) give three points of the
  cylinder's ellipse; FUN_1801e74e0 (l.323849) the node's x range (a circle of radius 588 about the edge sampled every sqrt(8 t90 / 588)
  radians, +- t90); FUN_1801df770 (asm 1801df770..1801dfd07) the arc: the points turned by -atan2(v.z, v.y) about P1, the angles of the
  first and last on the ellipse (588 / |v.x / |v|| x 588), counter-clockwise or not by the middle point, the step count round(|span| / (2
  acos((rx - cdev) / rx))) (at least 2) and its cos / scaled sines; FUN_1801ea2e0 (l.325823) the arc's points at x = P1.x, FUN_1801e77c0
  (l.323968) keeps those on the outer side of the edge record's plane q (rec +0x40; all when q = 0: (p - c) . q < system tol), and with
  more than 2 kept FUN_1801df770 again on them.
- FUN_1801e0ae0 (l.319645) "edge quads", when the edge is not along z: DAT_180501d4c = 0 (record flag set, or N1.z >= -1e-5) or 1, then
  with the flag also 2 and 4; per mode FUN_1801d9520 (l.314645): P1, P2 offset by FUN_1801e99f0 with their normals, the normals turned
  about the edge by +- sqrt(8 t90 / 588) (FUN_18024d220), Q = P + (588 - t90) x the turned normal: four triangles (kind 12 / 13: pieces
  code 5 / 0x17) with their vertices sorted by x (FUN_1801da920), pushed onto the triangle list.

**3. The sections, FUN_1801db8c0 (asm 1801db8c0..1801dba72)**: DAT_180501db0 (FUN_1801dba80, 12.12), then the edge list (node x max < lo
-> freed; x min <= hi -> 588 / 590 by its kind and FUN_1801ed160: kind > 1 -> FUN_1801e2030, else FUN_1801e1ac0; failure -> error 0x1ce8),
then the corner list (P.x + 590 + 588 < lo -> freed; P.x - (590 + 588) <= hi -> FUN_1801ec6b0, result ignored), then FUN_1801e7350.
- FUN_1801e1ac0: the ellipse at x = pos stepped over its arc (A' = A c - s98 B, B' = sa0 A + c B, turned by (c78, s80), plus the edge's
  point at pos); each point classed along the edge by FUN_1801e7440 (1 at P1, 3 before, 2 at P2, 4 after, 0 between, 1e-12); per step
  FUN_1801e0050 by 5 a + b: 0-2 / 5 / 7 / 10 / 11 piece as is, 6 / 12 piece (4), 8 / 14 / 16 / 18 / 22 / 24 nothing, 3 / 4 / 9 / 13 the
  second end and 15 / 17 / 20 / 21 the first end moved onto the end cap, 19 / 23 both (FUN_1801ed4a0: the cap circle FUN_1801ecfb0 (the
  plane x = pos met with the end's normal plane by FUN_180274990, the point projected, +- sqrt(588^2 - d^2) along that line) - its point
  whose foot on the piece's line is within (-2e-7, 1 + 2e-7), the nearest; else the piece's line met with the end's plane (lp_ints));
  piece code 4 / 0x16, normals from the nearest point of the edge (FUN_1801db170).
- FUN_1801e2030 (kind 8 / 9): one piece from the higher point of each cap circle.
- FUN_1801ec6b0 (corners): rho = sqrt(588^2 - dx^2) (none when |dx| > 588); the table level ceil(|dx| / step) (FUN_1801da6b0, one lower
  when it has fewer points); per table step the point's side of the two side planes (d1 . p, d2 . p < 0); inside both: a piece on the
  circle; crossings moved onto a side plane by FUN_1801dfd10 (the side's cap circle (normal d) met with the chord, then 3 when that point
  is at or before P along the other side, else 2) or onto the inner circle rho - t90 (rho / 2 when rho <= t90); pieces code 0xb / 0x1d
  with normals from the centre (FUN_1801e4200); when none came out, up to 10 steps within 2 t90 of a side plane as pieces.
- The arc table DAT_180501dc0 (FUN_1801da800(100, r = 5a0, cdev) l.326251, FUN_1801d9e20 l.326287, FUN_1801db440 l.326394): level i at
  h = sum of i steps r / 99 has radius sqrt(r^2 - h^2) (stops when h > r) and FUN_1801e9990(radius, pi, cdev) = ceil(|pi / (2 acos((rad -
  dev) / rad))|) (at least 2) + 1 half-circle points from (1, 0) by the turn pi / (n - 1) (FUN_1801e14c0), shared with the level before
  when the count is equal. DAT_1805025f0 (check surfaces) is not built (no check surfaces on this path, 12.7).

**4. Triangles without normals (kinds 4 / 5, task step 2): not on the op 43 path.** FUN_1801cfb60 calls FUN_180195b80 with dl = 1 (asm
1801d0255), passed on as the scanner's param_2 (180195be0 / 180195caf); with param_2 = 1 FUN_1801db740 (l.316433) installs the
normal-carrying set (DAT_180501db0 = FUN_1801dba80, not FUN_1801dbb70) and FUN_1801e8360 writes kinds 10 / 11 only (its param_1). So
FUN_1801dbb70 / FUN_1801dca10 / FUN_1801dc520 and the arm 1801dab67 never run for Constant Scallop: NOT PORTED (a group of kind 4 / 5 is
refused by name).

**5. The merge-order verdict (task step 3, first part).** The DLL's own batch / merge (FUN_1801e40c0 -> FUN_1801e7350: MSVC qsort by the
lower end, LAB_1801da1a0, then FUN_1801e5200 per piece) was run in the SCN harness on each line's pieces (LMM_MERGE=1) and on the same
pieces with each 999-piece batch reversed (LMM_MERGE=3: the same batches, only the order qsort receives of equal lower ends changes), the
resulting line lists ("estore", 0x68 nodes) and walls compared (scn_cmp.py --merge-order): CURVE 5 AXIS op 2 model, 40 test lines: 3
lines differ; 11,727 vertex-placed lines: 1236 differ (same node counts; in the inspected line 2 of 179 nodes differ in the last bits of
z / normals, ~2e-15, corner pieces); corpus sample below. **So FUN_1801e5200's result depends on the order of pieces with equal lower
ends**: the port has to hand them over in the DLL's order and sort them with MSVC's qsort exactly. Both are in place: the piece order is
the scanner's activation order, which depends on the ERRCHK groups and so on the fill block split (12.12) - N13-28's one block per
surface, the large buffer; the sort is viewer/nest/nest_rect.c msvc_qsort (MSVC qsort.c: median of three, cutoff 8, shortsort by max
selection; the same algorithm as mcce_dyn.c gp_msqsort), already the scanner's group / triangle sort, whose ties came out identical to
the DLL in phase A (12.12) and in every group order here. With a smaller buffer (the 512 KB licence layout) Mastercam's own output would
differ in the last bits on such lines - not modelled (N13-28).

**Oracles.** `peloader mcmsurf.dll --lmm` SCN mode extended (tools/peloader/mcmsurf_lmm.c header): the edge / corner files, pools, work
arrays and the arc table set up as the scanner leaves them, FUN_1801db8c0 itself per line (its FUN_1801e7350 patched out), the activated
nodes hashed ("NODES"), the pieces as before or hashed per line (SCN_PCHASH); LMM_MERGE=1 / 2 / 3 runs the real merge after it.
tools/tests/scn_cmp.py compares pieces and nodes (and --merge-order the two merges); scn_oracle.sh runs the corpus (--merge-order, --no-pha).
- CURVE 5 AXIS op 2 model (CS 0.001 0.375 0 0; 482 edges / 482 corners): 40 lines: 38,159 pieces identical (triangles 9739, edge
  pieces (code 4) 4410, edge quads (code 5) 1838, corner pieces (code 0xb) 22172), 2366 edge / 3411 corner nodes identical. 11,727
  vertex-placed lines (--vtx 10): 14,762,737 pieces identical (code 0: 2,984,491, 4: 1,420,559, 5: 832,506, 0xb: 9,525,181), 694,920 edge
  and 1,103,744 corner nodes identical. Branch coverage (cs_port stderr): flat edges 152, refitted arcs 202, edge pieces by class 0
  778,982 / 4 664 / 1 135,434 / 2 135,437 / 3 9,826, caps moved 290,523, flat sections 360,216, corner sections 1,103,225 (pieces
  9,470,240, near-plane fallbacks 54,941, side cuts 3: 1,489,044 / 2: 181,007, inner circle 797,728 + 691,326), flagged quads 130,
  edges along z 128. Not hit: the cap fallback lp_ints / its failure, a side cut without a candidate.
- Corpus sample (tools/tests/scn_oracle.sh --merge-order, every 10th of 908 parts = s10 list, 91 parts, 37 with surfaces, --max 20, ball
  0.375, 20 test lines): phase A 283 surfaces identical, 0 differ, 7 split by the oracle arena (unchanged); 740 lines, 384,064 pieces
  identical (triangles 103,055, edge pieces 104,228, edge quads 33,650, corner pieces 143,131), 93,405 edge and 101,443 corner nodes
  identical; 0 differ, 0 failed runs. Merge order (batches reversed): 248 of 740 lines differ.

**Where this stopped (session 6).** Task step 1 (edge and corner groups) done and DLL-identical; step 2 (kinds 4 / 5) shown off the path;
step 3: the order verdict done (order-dependent, handled by N13-28 + msvc_qsort), the port of FUN_1801e7350 / FUN_1801e5200 itself NOT
started: FUN_1801e5200 (l.322475, asm 1801e5200..; 827 decompiled lines, a goto network around the estore list) inserts one piece
(ends ordered by s, FUN_1801e40c0) into the line's upper envelope: from the search hint FUN_1801da5d0, per overlapping node the piece's and
the node's z at the overlap ends (interpolated, normals by FUN_1802535d0), equal within DAT_1804ea578 / one above / crossing (FUN_1802531e0,
else a 1000-step bisection), nodes split or trimmed with FUN_1801e2a90 (node insert, arguments partly on the stack - read them from the
asm, Ghidra shows in_stack values) and FUN_1801e2230 (unlink); pieces with |s1 - s0| < DAT_1804ea578 and |z1 - z0| > system tol whose
normals are horizontal (FUN_1801edb00: |n.z| < 8.7266462e-5) and whose code FUN_1801688d0 does not mark go to the wall store
(FUN_1801e01a0 / FUN_1801edb20, flushed to plmvcs00.tmp by FUN_1801ed9b0); containment (DAT_1804ea4f0, DAT_1804ea5c8 / 5d0) is off for op
43. The harness already runs the real merge (LMM_MERGE) and prints each line's list / walls ("EST", "WALL", LMM_ESTFULL for every node),
so the port can be checked line by line as soon as it exists. Then FUN_1801e02b0, FUN_1801e0680, FUN_1801ed9b0 (task step 4). Op 43
stays greyed. Regression: no app / cam_cli source changed (the new files are linked only into viewer/cs_port; `make -C viewer -n all`
does not reference them), so op_compare / fileop were not rerun.

### 12.14 Stage B part 3: the envelope merge, the cleanup, the line writer, the walls (2026-11-14, session 7)
Sources: objdump of mcmsurf.dll (FUN_1801e5200 1801e5200..1801e66b4 translated from the disassembly: the decompile garbles its register
pairs and FUN_1801e2a90's stack arguments; tools/ghidra_scripts/SigDecomp.java re-decompiles with callee signatures applied, still not
usable for the register pairs), FUN_1801e9ec0 1801e9ec0..1801ea2d5, FUN_1801e3190 1801e3190..1801e3349, FUN_1801e0680 1801e0680..1801e0a9c,
FUN_1801da5d0, LAB_1801e5190, LAB_1801e4460, LAB_1801da1a0, FUN_1801e01a0, FUN_1802531e0; decomp/mcmsurf_all.c for FUN_1801e7350
(l.323767), FUN_1801e40c0 (l.321821), FUN_1801e2a90 (l.320886), FUN_1801e2230 (l.320495), FUN_1801da360 (l.315458), FUN_1801e7cd0
(l.324167), FUN_1801edb20 / FUN_1801edb00 (l.327726 / 327715), FUN_1801688d0 (l.239852), FUN_1801e23f0 (l.320578), FUN_1801e6a70
(l.323432), FUN_1801e6fe0, FUN_1801e2680 (l.320692), FUN_18015bc40 (l.231117), FUN_1801e7d60 (l.324208), FUN_1801ed9b0 (l.327643),
FUN_1801ed720 / FUN_1801ed8d0; mccore l_ints (0x1804b3480, the port of cam2d_pock_zz.c) and dist_pt_l (0x1804b32a0, asm). Ported in
viewer/cam3d_cscallop_merge.c.

**1. The line list ("estore") and the batch.** Nodes as 12.2 (0x68 bytes: +0 flag, +8 s0, +0x10 z0, +0x18 n0, +0x30 flag, +0x38 s1, +0x40
z1, +0x48 n1, +0x60 code, +0x62 next). LAB_1801e5190 chains all DAT_1804ea5e4 nodes free, FUN_1801e7cd0(1) turns the search hint on
(DAT_1804ea5f4 / 5f8: the node before / the node to start from; FUN_1801da5d0 restarts from the head when the piece starts before the
hint node, FUN_1801da360 finds the node before when the pair is stale), LAB_1801e4460 returns a line's nodes. FUN_1801e40c0 stores each
piece with its ends ordered (s1 <= s0: the second end first) in the 999-record batch; FUN_1801e7350 qsorts the record pointers by the lower
end (LAB_1801da1a0, MSVC qsort), resets the hint, and calls FUN_1801e5200 per record in that order.

**2. FUN_1801e5200, one piece A..B into the line's upper envelope.** The piece's walls first: FUN_1801e01a0 (wall codes of FUN_1801688d0
always; else |s1 - s0| < DAT_1804ea578, |z1 - z0| > system tol and both normals with |n.z| < 8.7266462e-5) -> FUN_1801edb20 (the wall
store, 2000 records, flushed by FUN_1801ed9b0 when full). Pieces with |sB - sA| <= 1e-12 stop there. From the hint node, while there are
4 free nodes: a node starting at / after sB -> the piece before it; a node ending at / before sA is passed (and becomes the hint when it
ends more than the system tol before sA); else over the overlap [lo, hi] both are interpolated (z linearly, normals by FUN_1802535d0 when
the length is above DAT_1804ea578) and compared at lo and hi with DAT_1804ea578:
- equal at both ends: the node stays when it is a triangle piece (code 0 / 0x12) and the piece is not; the piece wins in the opposite
  case; else the longer stays (node length > piece length: the node);
- equal at one end: the other end decides; above at both ends: that one; else they cross.
The node staying: the piece's part before lo is inserted, the rest goes on from hi (or ends). The piece winning: the node is cut back /
split around [lo, hi] or removed (FUN_1801e2230), and the piece is inserted whole when it ends inside the node. A crossing: x = the linear
crossing when hi - lo > DAT_1804ea578, else mccore l_ints of the two segments (FUN_1802531e0), else 1000 halvings until the two z are within
DAT_1804ea578; x is kept within (lo + 1e-12, hi - 1e-12) (the middle when hi - lo <= 2e-12), and the pieces are split at x the same way.
What is left of the piece goes before the current node or at the end. Faithful DLL quirks kept: on several paths the piece's running
start normal is not updated (only the scratch slot [rbp-0x38] is: 1801e594c, 1801e5fb1; 1801e6427), at 1801e6543 a node's new start normal
is the crossing normal while its start point is hi, at 1801e59e6 the end normal slot takes the start normal, FUN_1802531e0's parameters
along the whole segments interpolate the normals of the overlap ends.

**3. The cleanup FUN_1801e02b0 (DAT_180501d60), per line after the merge.** FUN_1801e9ec0: a node shorter than DAT_1804ea578 (wall codes:
DAT_1804ea570) starts a group of following short nodes (joined within DAT_1804ea578, the group within DAT_1804ea570 in s); the group becomes
one flat node at the group's highest z with the normal of the first node's higher end (the later nodes' normals are never taken: the
compare at 1801ea00e is `comisd xmm0, xmm0`), the code of its longest node, ending at the next node's start or the group's end; joined to
the neighbour with the smaller gap below DAT_1804ea578 (the previous one: its end point / z / normal / code and the start moved there; the
next one: its start); the nodes after it in the group collapse onto its end. Then per run (FUN_1801e3190: joined within DAT_1804ea568, wall
codes also in z, code 0 / non-0 and < 0x12 / >= 0x12 and FUN_18015bc40 alike) FUN_1801e23f0 joins straight stretches: FUN_1801e6a70 checks
every joint of the stretch against the chord to the next node's start (when the next node is not joined) or end - distance (dist_pt_l)
below DAT_1804ea568 and the normal against the normal interpolated along the chord (FUN_1801e6fe0: largest component difference below
DAT_1804ea568, else the chord and the node within 15 degrees of each other); FUN_1801e2680 turns the stretch into its last node and the
others into code 0x24 points.

**4. The writers.** FUN_1801e0680 (DAT_180501d78; its first argument is {+0 local_31c8[0] = param_2 = 1, +2 the list head, +0x10 the
estore, +0x18 DAT_1804ea578}, its second the SysFile of FUN_18025d6d0(0, ..) = the recut file with its 0x38 header, +0x20 offset, +0x28 the
per-line int table, +0x30 line, +0x38 position): the nodes (code 0x24 skipped) as 0x30 points, a joint within DAT_1804ea578 in s, z and
(flag 1) all three normal components becomes the midpoint with code + 0x27, else the two ends; then the 0x18 trailer (+0 1, +2 n, +4 the
table's previous entry, +8 the line position), table[line] = the trailer's offset. Empty lines write nothing. Bytes +0x2b..+0x2f of each
point and +1 / +0x10..+0x17 of the trailer are the DLL's stack contents. FUN_1801ed9b0: the line's wall records (0x68 bytes), a trailer (+2
n, +4 the line's previous block, +8 position, +0x10 0; +0 / +1 / +0x12.. stack), the 8-byte line table entry (offset, count) and
DAT_180501d48 (the largest count); plmvcs00.tmp = line count, table (rewritten by FUN_1801ed8d0 at the end), blocks.

**5. Oracles.** The SCN harness runs the DLL's own merge and, with LMM_MERGE=4, after it the DLL's own FUN_1801e02b0, FUN_1801e0680 (mccore
SysFile on OUTDIR/dll_scn.bin) and the walls (FUN_1801ed9b0; FUN_1801ed8d0's table rewrite repeated by the harness: its m_fclose needs an
MFC ordinal the loader lacks). The SCN line now carries DAT_1804ea568. viewer/cs_port CS_MERGE=1 / 2 does the same with the port;
tools/tests/scn_cmp.py --merge / --full compares every line's list (EST, with LMM_ESTFULL every node), walls, cleaned list (ESTC / ENC),
the scan file (port_scn.bin vs dll_scn.bin) and its table, and plmvcs00.tmp byte for byte, the stack bytes masked; scn_oracle.sh --full.
- CURVE 5 AXIS op 2 model (CS 0.001 0.375 0 0, test lines): 40 lines: 6602 list nodes, the cleaned lists and the scan file (187,928
  bytes, 3894 points) identical. 11,727 vertex-placed lines: 1,976,689 list nodes identical, the scan file (58,119,344 bytes, 11,725
  blocks, 1,204,956 points) identical. Merge branches: inserted before 90,575, appended 1,439,736, node kept 14,213,529, piece kept
  3,532,630, ties decided by code / length 716,176, crossings linear 118,080 / l_ints 43 / bisection 16, nodes removed 1,565,200.
  Cleanup: short groups 54,069 (joined to the previous 287, to the next 53,782), straight joins 1,075,014; writer joints merged 1,045,896,
  split 67,805. No walls on this model.
- Corpus sample (scn_oracle.sh --full, the s10 list, 91 parts, 37 with surfaces, 20 test lines, ball 0.375): 740 lines, 96,589 list nodes,
  2971 walls (135 wall blocks), the cleaned lists, the scan files (730 blocks, 93,596 points) and the wall files identical; 0 differ, 0
  failed. Not hit: the wall store's 2000-record flush, estore overflow (error 2 / -7), containment (DAT_1804ea4f0, off on op 43).
- The scanner arguments in all of these are unit-test values (CS line: tol 0.001 / ball radius, corner deviation = tol, the corpus parts'
  msm_case frames), and the lines are test placements - NOT FUN_1801cfb60's real arguments or Mastercam's line positions (stage C).

**Where this stopped (session 7).** Stage B's per-line pipeline is complete and DLL-identical: activation, sections, merge, cleanup,
writer, walls. NOT PORTED in stage B: the scanner's own frame of the file (FUN_18025d6d0(0, ..) / FUN_180229640's 0x38 header, the
trailer l.326713-326730: frame DAT_1804ea500 / 518 / 530, 560, 568 / 5, 570, 578, param_5, DAT_1804ea4f8, DAT_1804ea550 / 548, param_18
flag, line count and table, containment arrays), the wall store's in-line flush (S->wall_flush hook, untested), containment, check
surfaces. Next: stage C - FUN_1801cfb60's scanner arguments (its call at asm 1801d01f0-1801d025b), the line placement and the per-line
stages of 12.2 (FUN_180183310, FUN_1801974e0, FUN_1801b0c80, FUN_180184cc0, FUN_180182620 / FUN_180181a80 / FUN_180181680,
FUN_18018fd80, FUN_1801a1a70). Op 43 stays greyed. Regression: no app / cam_cli source changed (the new file is linked only into
viewer/cs_port; `make -C viewer -n all` does not reference cam3d_cscallop*), so op_compare / fileop were not rerun.

### 12.15 Stage C start: the scanner's real arguments (2026-11-14, session 7)
Sources: objdump of mcmsurf.dll: FUN_1801cfb60's prologue 1801cfb60..1801cfea1 (7 pushes, rbp = entry rsp - 0x35f8, so [rbp + 0x3620 +
8 k] = its stack parameter 5 + k; xmm3 -> xmm10), the scanner call 1801d01cd..1801d025b (through the pass-through wrapper FUN_180195b80,
asm 180195c6f..180195d1f), no write to the argument registers in between (checked over 1801cfea1..1801d025b); FUN_180133f10's call of
FUN_1801cfb60 for modes 6 / 5 (l.203312-203361, its stack locals local_3a8 .. local_310 = parameters 5 .. 24); the scanner l.326181-326215.
| scanner FUN_1801ea7e0 | FUN_1801cfb60 | value on the op 43 path (FUN_180133f10 modes 6 / 5) |
|---|---|---|
| param_1 kind | param_1 ([rbp-0x72]) | 7 (90 deg frame), then 6 |
| param_2 | dl = 1 (asm 1801d0255) | 1 (normals; FUN_1801db740(1), 12.13 4) |
| param_3 (s: the stock) | param_4 (xmm3 -> xmm10) | FUN_180133f10's param_5 = the stock to leave |
| param_4 (the normal offset part) | param_5 ([rbp+0x3620] -> xmm9) | FUN_180133f10's param_4 (local_2f8) = the tool's corner radius r |
| param_5 | param_6 ([rbp+0x3628]) | FUN_180133f10's param_3 = the tool diameter D |
| param_6 / 7 / 8 (frame) | param_7 / 8 / 9 (r14 / r15 / rsi) | &DAT_1804e8dd8 / &DAT_1804e8df0 / &DAT_1804e8e08 (FUN_180133f10's frame, 9.1 step 1) |
| param_9 tol | param_10 (xmm15) | msurf_chord_dev |
| param_10 | 0 (asm 1801d0224) | 0 |
| param_11 containment | param_12 (dil) | FUN_180133f10's param_6 (swap) |
| param_12 / param_13 | param_13 / param_14 | iVar20 / the margin (9.1 step 3; used only with containment) |
| param_14 lines | param_15 (r13d) | uVar16 (9.1 step 4) |
| param_15 / param_16 | param_16 / param_17 (xmm7 / xmm8) | dVar29 (the first line) / dVar24 (the step); line i at -param_15 - i param_16, run from i = n - 1 down (l.326206-326209) |
| param_17 | param_20 (r12b) | *comp_to_tip |
| param_18 | param_21 | 0 (the trailer's flag) |
The corner deviation is the global msurf_corner_dev (l.326188-326193), not an argument.

**So the unit oracle's tool values are equivalent, not the real ones:** the scanner's offsets are s = stock (or -r when r < -stock),
588 = r + s, horizontal 590 = (2 s + D) / 2 - 588 (12.12 2). The test CS line (p3 0.375, p4 0, p5 0) gives 588 = 0.375, 590 = 0, 5a0 = 0.375,
the same values as the real stock 0, r 0.375, D 0.75 of CURVE 5 AXIS op 2 (ball D 0.75), so the offset model, pieces and lists above are
those of the real tool; only DAT_1804ea598 (s) differs, which reaches the scanner's trailer (DAT_1804ea4f8, not ported) and nothing per
line. The tolerance (0.001), the corner deviation (= tol), the frame (msm_case.py's) and the line positions are TEST values: the real ones
are msurf_chord_dev / msurf_corner_dev from msurf_map (the op record) and FUN_180133f10's frame, uVar16 / dVar29 / dVar24 (9.1 steps 1-4,
with the extents FUN_18012f9c0 l.200261) - NOT PORTED.

**Where stage C stopped.** Next, in order: (1) FUN_180133f10's frame (tool plane turned by msurf_cut_angle, + 90 deg for kind 7), step and
line count with FUN_18012f9c0's extents, msurf_chord_dev / msurf_corner_dev for op 43 from msurf_map, ported and run through the
scanner on the CURVE 5 AXIS model (unit oracle: FUN_18012f9c0 can run in the harness on the port's tmpmm / tmpedg0); (2) the scanner's
file frame (FUN_18025d6d0(0) / FUN_180229640 header, trailer l.326713-326730); (3) FUN_1801cfb60's per-line stages (12.2 steps 1-7) on
the scan file. Op 43 stays greyed. (Items (1) and (2) done in session 8: 12.16, 12.17.)

### 12.16 Stage C part 1: FUN_180133f10 before FUN_1801cfb60 - frame, step, model turn, extents, lines (2026-11-14, session 8) - PORTED, = DLL
Sources: objdump of mcmsurf.dll FUN_180133f10 180133f10..180135a80 (frame 18013402b..180134227, step 18013432b..180134469, extents call
180134472..180134621, lines 18013463b..1801346fc, the FUN_1801cfb60 calls 1801351ea..1801352ba (mode 5) / 18013533c..18013540c (mode 6)),
FUN_18012f9c0 18012f9c0..18012fec0, FUN_180261940..180261ec2, FUN_180261f40..1802632c0, mccore read_edge 180593320; decomp/mcmsurf_all.c
FUN_18025dd50 l.399269, FUN_180261f40 l.401306, FUN_180229ba0 l.364023-364178 / 364480-364530 / 364775-364916, FUN_180129b40 l.197025,
FUN_18012a960 l.197725-197742, msurf_map l.339935-340525, FUN_180246570 / FUN_1802465b0 l.383327-383360. Port: viewer/cam3d_cscallop_lines.c.
1. **msurf_map's op 0x2b values** (cs_op43_params): chord op+0x11d8, corner deviation op+0x1270 or op+0x1278 x chord when op+0x126a,
   slice op+0x1298, cut angle op+0x12c8 (used as stored), collapse % op+0x12a8 (clamped: < 1e-10 -> 0.8, >= 0.9999999999 ->
   0x3feffffffff24190, then to (slice - 1e-10) / slice or 1e-10 / slice), stock op+0x278, D / r op+0x2e8 / +0x2f0.
2. **Frame** (cs_s1_frame): with the view matrix rows V0..V2 (MCView +8..+0x48) and the cut angle a: y = (cos a V1 - sin a V0) + V2 x 0
   (componentwise, DLL order), x = (cos a V0 + sin a V1) + V2 x 0, z = V2; modes 4 / 6 / 7 / 9 / 0xb (mask 0xad0): x' = y, y' = -x.
   FUN_180266780 only copies it (world_to_2d).
3. **Step** (cs_s1_step, method 2): ops 0x28 / 0x2a / 0x2b with the byte op+0x1300 / +0x1310 / **+0x12d0** set: msurf_colaps_res_pct =
   FUN_180246570(D, chord, slice, {min(0.05, chord / slice when slice > 1e-12), 0.8}) = (2 sqrt((D - chord) chord), or slice) / slice
   clamped to the pair; msurf_colaps_dist = slice; **step = colaps_res_pct x slice** (op 2: 0.8 x 0.05). (Corrects 9.1 step 2: the call
   is on the 0x2a / 0x2b paths too, asm 180134335-1801343c7.)
4. **The model turned into the frame** (cs_mm_reframe = FUN_18025dd50 with p1 = -y, p2 = x, p3 = z): tmpmm3.tmp holds the model's frame
   rows (after +2 count, the part name line, one flag byte and FUN_180224290() = 0x96 bytes) - FUN_180229ba0's rows: the view turned by
   cos / sin(pi / 2): a = (1, -c, -0), b = (c, 1, 0), the view z, for Top (c = cos(pi / 2) as a double; its param_17 is FUN_180129b40's
   view z row, build_machine_model's third frame argument, so the cut angle never enters the model). z differs: nothing (count unset).
   Same rows: nothing. p1 = b, p2 = -a / p1 = -b, p2 = a: the exact quarter turns (FUN_1802619c0 / FUN_180261940: per matching record
   the box (+8, +0x10, +0x18, +0x20); per edge P1 / P2 / plane normal / N1 / N2 (+0, +0x20, +0x40, +0x78, +0x90); per corner +8 / +0x20 /
   +0x38 / +0x50). Else FUN_180261f40: the angle acos(clamp(base . p1)) signed by base . p2 (base = the view turned by pi / 2, recomputed
   - not the stored rows), FUN_180261c30 / FUN_180261980 (x c - y s, x s + y c) on the same fields, the box from its four turned corners.
   The rows p1 / p2 are written back. Records match tmpmm3 entries by +0 (the eptr). On op 2: mode 6 takes FUN_180261f40 with the angle
   -0 (the stored rows carry c, the frame exact zeros), mode 5 then the -90 deg quarter turn.
5. **Extents** (cs_extents = FUN_18012f9c0(D, stock, n, ..), from the asm - the decompile lost the arguments): lo4 / hi5 = the records'
   x range (+8 / +0x10 of records with short +0x66 != 1) -/+ (|stock| + D / 2); then every tmpedg0 edge (read_edge: P1, P2), x' = x - y x 0,
   y' = x x 0 + y: lo6 / hi7 = the min / max of the midpoints of the edges along the line direction (|dx'| < chord, |dy'| > 10 chord); when
   there is none, or the lowest is more than chord above all edges' min, lo6 = that min and flag f8; when the highest is more than chord
   below the edges' max (or there is none), hi7 = that max and f9.
6. **Lines** (cs_s1_lines, asm 18013463b..1801346fc, no swap): lo = -(lo6 - f8 step / 2), hi = -(hi7 + f9 step / 2), n = (int)ceil((lo -
   hi) / step); n <= 1: one line at the middle; else step = (lo - hi) / n, n + 1 lines; then one more line per step while hi > -hi5 (hi
   -= step) and while -lo4 > lo (lo += step). FUN_1801cfb60 gets n (param_15), first = hi (param_16), step (param_17); the scanner's line
   index n - 1 lies at -first - (n - 1) step and the position grows by step per line (l.326209, l.326526-326590).
FUN_1801cfb60's other arguments (mode 6 / 5): kind 7 / 6, 1 / 0, 0 / 1, stock, r, D, &x / &z / &y, msurf_chord_dev, 0, swap, 0, margin 0,
n, first, step, 1, DAT_1804d7038, *comp_to_tip, 0, msurf_max_short_gap. The scanner's frame rows are -y / x / z (l.326195-326203) and its
rotation flag DAT_1804ea4f2 = (x.y < 0.999999999999 or -y.x < 0.999999999999) (l.326204); fill_mach_model_buffer skips rot_view when it
is 0 (FUN_18057a510 param_8; viewer/cam3d_mmodel.c mm_fill(F = NULL)). Between the two calls the kind 7 scanner sorts each surface's
tmpedg0 / tmpcrn0 records in place (FUN_1801e7e20 l.324362-324395, written back), which mode 5 and the kind 6 scanner then read.

**DLL oracle** (tools/tests/s1_cmp.py PART OPID WORK; tools/peloader --lmm S1 mode): bnci_surface's setup repeated up to l.12665, then the
DLL's own FUN_180133f10(6, 2, D, r, stock, swap) and (5, ..) as FUN_18012a960 calls them, FUN_1801cfb60 replaced by a recorder (its
arguments; the model files after each mode) - on the machine-model files of the port's stage A (FUN_180229ba0 cannot run in the DLL:
alloc_nc_all, N13-23; the files: tmpmm records with +0 eptr / +0x38 / +0x3c / +0x54 / +0x68 = 1, tmpmm3 with the rows and entries,
tmpmm2 name / count / head / entries, tmpedg0 / tmpcrn0). New glue: Mastercam.dll mcViews = the Top view (identity, the same answer as
GetActiveViewNo / view_to_world), mfc #8010 LoadString = FALSE (status-bar text). No licence code on the path.
- CURVE 5 AXIS REVIEW FILE op 2: **both modes identical** - msurf values (chord 0.001, corner deviation 0.001, slice 0.05, cut angle 0,
  collapse % 0.8), FUN_1801cfb60's kind / stock / r / D / frame rows / chord / n = 120 / first / step = 0.04, and tmpmm / tmpmm3 / tmpedg0
  / tmpcrn0 after each mode byte for byte.
- **The scanner with op 2's real arguments and lines** (cs_port "OP" / "TL" / "S1" case lines: pass 1 = stage A files, stage 1, pass 2 =
  the scanner; CS_S1MODE=5 for the second call; tools/tests/scn_cmp.py --full and pha_cmp.py on s1_cmp's port.case): kind 7: 120 / 120
  lines, 113,956 pieces, 19,617 list nodes, the scan file (559,400 bytes, 118 blocks, 11,594 points) identical; kind 6: 120 / 120 lines,
  114,004 pieces, 19,762 nodes, scan file 562,952 bytes identical; no walls; phase A with the real frames (rotation flag 0 / 1): 5 surfaces
  identical, surface 0 split by the oracle's arena (N13-28, as 12.12).
NOT PORTED here (refused with a message): the swap / containment paths (FUN_18013ebb0, the tmptc pre-pass), FUN_180261f40 with a view z
different from the model's (cross products + FUN_18002dcd0), 0x120-byte edge records (tmpmm3 flag != 0), the slice of ops 0xd / 0x6d
(FUN_1802465b0 with IsMetric), modes 1 / 4 (reference tool); tmpmm3's 0x96 header bytes and tmpmm2's list head (written 0 / -1: not read
by stage 1; the sorted-by-xmin record list of l.364800-364895 and record +0x6c are NOT PORTED).

### 12.17 Stage C part 2: the scan file's frame - header, trailer, FUN_18022c850 (2026-11-14, session 8) - PORTED, = DLL
Sources: decomp/mcmsurf_all.c FUN_180229640 (l.363480), FUN_18025d6d0 (l.399067), FUN_1801ea7e0 l.326713-326768 (asm 1801ebf97..1801ec0b0:
4f8 = 5a0 - xmm12 = s), FUN_18022c850 (l.365146), FUN_1802410d0 (l.379542), FUN_1801e7ad0 (l.324073). Port: cs_scan_header /
cs_scan_trailer / cs_scan_appendix (viewer/cam3d_cscallop_lines.c), written by cs_port S1 runs (CS_MERGE=2) as port_scn_s1.bin /
port_scn_apx.bin.
- The scan file is the op's recut file (FUN_180228850: s_temp_rgn = the op's +0x9b9 path after bnci_surface l.12665). Opened by
  FUN_18025d6d0(0): header FUN_180229640(-0x38, -1, -1) = {+0 -0x38, +4 c, +8 b, +0xc..+0x23 -1, +0x28 -1.0, +0x30 0; +0x24..+0x27 /
  +0x31..+0x37 the caller's stack}; the line blocks from 0x38 (FUN_1801e0680, 12.14).
- The scanner's trailer after the blocks: the rows 500 / 518 / 530, 560, 568 / 5.0, 570, 578, D, 5a0 - s, -first, -step, int (param_18
  != 0) (0 for op 43), int n, the n-int line table (-1 = an empty line), int -1 (no containment; with containment the count and two n x 8
  arrays). FUN_1802410d0 reads exactly this at header +0xc.
- Then FUN_180229640(-0x38, 0x11, 0x20) (op 0x2b: 0x11 / 0x20; 5 -> 5 / 0x1c, 0x27 -> 0x10 / 0x1f, 0x28 -> 0xf / 0x21, 0x29 -> 0xd / 0x1c,
  0x2a -> 0xe / 0x1d, else 0 / 0x1d), +0xc = the trailer's offset, and FUN_18022c850 appends the part name line, tmpmm2's count and head,
  tol / chain_tol / chain_plane_tol / min_arc_length / curve_min_step / curve_max_step / curve_chord_dev / surf_step_chord_dev / nc_tol,
  tmpmm2's 0x20-byte entries, -1, the op id, the op entity's +0x30, the op id, 0x28d0 (8 bytes) + the operation, 0x698 + the tool
  (tool_manager(5)), the interfacial tool's 13 doubles (0 unless tool type 0x19 / 0xe / 0x17: refused, NOT PORTED), and writes the header
  with +0x10 / +0x14 (appendix start / after the entries), +0x1c / +0x20 = -1 (no swap), +0x24 (the 13 doubles).
- **DLL oracle** (tools/tests/apx_cmp.sh WORK [5|6], after s1_cmp.py): the DLL's FUN_1801e7ad0 (harness SCNR mode) reads the port's file
  and returns its line table (identical, 120 lines, both kinds); the DLL's own FUN_18022c850 (harness APX mode: DatabaseRetrieve = the
  case's OP record, the configuration tolerances given by the case, the recut file placed in OUTDIR) on the port's file without the
  appendix gives **the port's whole file byte for byte**: kind 7 572,577 bytes, kind 6 576,129 bytes.

**Where stage C stopped (session 8).** Done: 12.15's items (1) and (2). Next, item (3): FUN_1801cfb60's per-line stages (12.2) on the
scan file - the setup (FUN_18018b500 kind flags, FUN_180164ab0, FUN_18015d700, FUN_1801868a0 rufvcs, FUN_1801e7ad0, FUN_1801b3130 arrays,
FUN_18025d6d0 temp.rgn header), then per line FUN_180183310 (block walk: FUN_1801a6ee0 the 0x18 trailer, FUN_1801ae2b0 the record size,
FUN_1801abc40 / FUN_1801aab50 point reads, FUN_180182e70 -> FUN_180182070 / FUN_180183080 / FUN_1801a2760 list insert), FUN_1801974e0
(FUN_1801d7da0, FUN_1801a2cc0, FUN_1801a1d80), FUN_1801b0c80, FUN_180184cc0, FUN_1801a5510, FUN_180182620, FUN_180181a80, FUN_180181680,
FUN_18018fd80, FUN_180182de0 -> FUN_1801a1a70 (read), the vcs copy FUN_1801c9350 and the trailer. Its DLL oracle needs FUN_1801cfb60's
arrays without the NC arena (FUN_1801b3130 / FUN_1800cddd0 -> FUN_1800cdfa0: pl2StackAlloc for modes 0 / 2, calloc for mode 1): to be
checked before porting. Stage D not started. Op 43 stays greyed.

### 12.18 Stage C item 3: can FUN_1801cfb60 run whole in the harness? No - the probe (2026-11-14, session 8)
tools/tests/cfb_probe.sh WORK (harness CFBRUN mode: S1's setup, the DLL's FUN_180133f10(6 / 5) with the REAL FUN_1801cfb60, its scanner
FUN_1801ea7e0 replaced by the port's whole scan file of 12.17 at the recut name and its wall file at plmvcs00.tmp; GetTempDirectory = "./"
in OUTDIR because mccore add_prefix_suffix re-prefixes a Unix path; m_fopen / m_fclose = the CRT's as in PHA). It gets through the
scanner wrapper FUN_180195b80 (plmscn00.tmp -> tmpedg2.tmp, plmvcs00.tmp -> rufvcs90.tmp, temp.rgn opened) and crashes in FUN_1801a5570
(called at 1801d26f1): the line-list node array DAT_1804e9328 is NULL - FUN_1801cfb60 sizes it as min(0x7fff, pl2MemBufRemainFit(0x68))
(l.310682-310694) and takes it from FUN_1800ce330(.., 2) -> FUN_1800cdfa0 -> pl2StackAlloc, i.e. from alloc_nc_all's NC buffer. Answering
pl2MemBufRemainFit / pl2StackAlloc would emulate that buffer (N13-22 / N13-23 / N13-28): NOT done. So stage C item 3's oracle must be a
unit mode as SCN is for the scanner: the harness sets DAT_1804e9320 = 0x7fff (N13-28's large buffer, as the scanner's estore) with its
own node array DAT_1804e9328 and the other work arrays FUN_1801b3130 would take, opens tmpedg2 / rufvcs90 / temp.rgn as FUN_1801cfb60 does
(DAT_1804e9040 / 9120 / 9060), sets FUN_1801cfb60's globals (12.2 Constants: DAT_1804e9238 .. 9258, the frame, DAT_1804e9144, 9230, 9228,
the kind flags) and calls the per-line functions of 12.2 steps 1-7 itself, line by line, in FUN_1801cfb60's order (l.310762-311254).
(Session 9, 12.19: the harness store of this plan is NOT allowed by the orchestrator ruling; the unit mode supplies only direct arguments.)
Next session: that SCN-like unit mode, then the port of FUN_180183310 (FUN_1801a6ee0, FUN_1801ae2b0, FUN_1801aaa50 / FUN_1801ad6c0,
FUN_1801abc40 -> FUN_1801abd40 / abe30 / abf40, FUN_1801aab50, FUN_180182e70 -> FUN_1801b4b60 / FUN_180182070 / FUN_180183080 /
FUN_1801a2760) and the steps after it, each against the DLL per line. Op 43 stays greyed.

### 12.19 Stage C item 3: FUN_1801cfb60's per-line stages, per-function unit mode (2026-11-14, session 9) - PARTLY PORTED, = DLL where testable
**Orchestrator ruling (binding, this session):** the harness answers no pl2MemBufRemainFit / pl2StackAlloc and supplies the alloc_nc_all
buffer in no form, so FUN_1801cfb60 is not run whole (N13-23); the harness calls the per-line functions itself and gives each only its
documented arguments; a 0x7fff node array (N13-28) only where it is a direct argument of the function under test, never via the pl2
allocators; a function that calls pl2MemBufRemainFit / pl2StackAlloc itself is not run: its callees are. Consequence (the boundary
list below): the line list ("estore") of FUN_1801cfb60 is the GLOBAL DAT_1804e9328 (from FUN_1800ce330(.., 0x68, 2) -> pl2StackAlloc,
l.310682-310694), not an argument of any per-line function, so no function that reads or writes it has a DLL oracle.
Sources: decomp/mcmsurf_all.c (l.N), objdump of mcmsurf.dll (asm). Port: viewer/cam3d_cscallop_cfb.c, driver viewer/cfb_port (make
cfb_port), harness tools/peloader/mcmsurf_lmm.c modes CFBU / CFBA, scripts tools/tests/cfbu_cmp.sh, tools/tests/cfbu_sample.sh.

**The per-line arm for kinds 6 / 7** (local_364a = 0, param_18 = 1, local_3671 = 1, local_3648 = 0; asm 1801d2881..1801d2ea1), per line
in index order 0 .. n - 1 whose chain DAT_1804e92e0[line] != -1 (l.311064-311254): FUN_180183310 (the scan file DAT_1804e9040 =
tmpedg2.tmp, reader FUN_1801a6ee0, inserter FUN_180182e70 - l.311064-311069 pick them for the scanner file; param_5 = 1, param_6 =
local_3408 (FUN_1801ad6c0(1, 500, ..) l.309956), param_7 = local_3478 (FUN_1801ada90 l.310759), param_8 = local_32a8); then if the list is
not empty: FUN_1801974e0; DAT_1804e9794 = FUN_1801b0c80(rufvcs, reader, ..); n = FUN_180184cc0(0, &local_3158, nvcs, DAT_1804e97a8,
local_362c, local_3638) (asm 1801d2c96..1801d2cba; n < 0 -> FUN_180268e20(3)); FUN_1801a5510; FUN_180182620(n, local_3638);
FUN_180181a80(0, xmm9 = param_22 = msurf_max_short_gap (asm 1801d1181, 1801d2cf7), n, local_3638); FUN_180181680(n, local_3638); then
(local_3671 = 1) the second vcs pass only when DAT_1804e92a9 and local_3157 and the first pass found no connector; FUN_18018fd80(1, 0,
local_3448, 0); FUN_180182de0(2, &DAT_1804e9060, &DAT_1804e92ec, DAT_1804e92f8) (temp.rgn); DAT_1804e9344 != 0 -> -7. After each line
FUN_1801a5510 and DAT_1804e92b8 += DAT_1804e9228.

**Ported and checked against the DLL (unit mode, byte for byte):**
| function | port | oracle |
|---|---|---|
| FUN_1801cfb60 l.309965-309980: DAT_1804e9238 / 9240 / 9248 / 9250 / 9258 | cs_cfb_consts | none (arithmetic of the decompile; 9250 is handed to the DLL functions) |
| FUN_180183310 + FUN_1801a6ee0 (asm: returns read == 0x18), FUN_1801ae2b0, FUN_1801aaa50 / FUN_1801ad6c0, FUN_1801abc40 -> FUN_1801abe30, FUN_1801aab50 | cs_cfb_walk | CFBU: the DLL's own FUN_180183310 on the port's scan file (a SysFile, its param_1) with a recorder as its param_4 (in place of FUN_180182e70): every segment (code, the two flag bytes DAT_1804e8ff0, s / z pairs, both normals) and the return / line position |
| FUN_180182070 (FUN_1801688d0 jump table 180168900, FUN_1801d8ed0 constant 0x1804150a0 = 8.7266462e-05) | cs_seg_vertical | CFBU: the DLL's FUN_180182070 on each recorded segment |
| FUN_180182620 / FUN_180182690 | cs_conn_codes | CFBA: the DLL's own on each line's node array (its direct argument) |
| FUN_180181a80 (asm 180181a80..180181c55) | cs_join_runs | CFBA, the same arrays |
| code families FUN_1801688d0, FUN_18015afd0, FUN_18015a310, FUN_180163f30, FUN_18015bc40, FUN_18018af60, FUN_18018b0e0, FUN_18015f6c0 | cs_code_* | CFBA: all codes -3 .. 0x45 |
DAT_1804e8ff0 (the flag bytes FUN_180183310 passes) is never written (only lea references in the asm; .data's raw data ends at
1804e3800): zero. FUN_180268e20: 0 -> -11, 1 -> -12, 2 -> -4, else -16 (l.405217).

**Ported, NO DLL oracle** (each reads or writes the store DAT_1804e9328; checked only as inputs of the functions above: the CFBA arrays are
the port's FUN_180184cc0 output):
- the store: FUN_1801a5570 (free list), FUN_1801a5510 (the line freed), FUN_1801a2760 (a node stored), FUN_180182e70 (asm 180182e70..
  180183079: after the tail when s0 >= its s1, else before the first node with s0 >= s1, s1 snapped within 1e-12; DAT_1804e8fff = 0 on
  this path, l.310261 / 310681, so FUN_1801b4b60 is not reached), FUN_180183080 (asm 180183080..180183294, the vertical-segment place /
  z snap; the decompile's comparison reads the same), FUN_180182120 (FUN_180182070 on a node);
- FUN_1801974e0 = the scanner's FUN_1801e02b0 on this store: FUN_1801a2cc0 / FUN_1801a1d80 / FUN_1801a55f0 / FUN_1801a5bc0 /
  FUN_1801a2650 are asm-identical to FUN_1801e3190 / FUN_1801e23f0 / FUN_1801e6a70 / FUN_1801e6fe0 / FUN_1801e2680 (DLL-checked in stage B)
  but for the globals (DAT_1804e9328 / 9330 / 9240 for DAT_1804ea4e0 / ea5f0 / ea568; FUN_1801a2cc0 and FUN_1801a55f0 differ only in
  instruction scheduling) - the port shares that code (cs_clean_runs, split out of cs_clean_line; the scanner's whole-file oracle
  apx_cmp.sh is unchanged after the split); FUN_1801d7da0 is NOT the twin of FUN_1801e9ec0: it also requires the node's FUN_18015bc40
  family to be the previous node's (FUN_18015b290, asm 1801d7e1c..1801d7e32), ported from its asm (clean_short_cfb);
- FUN_1801b0c80 for lines without walls (offset -1 -> 0); FUN_180184cc0 with nvcs = 0 (asm / l.260939: the list into the array, code 0x24
  left out, a node vertical by geometry but not by code -> 0x28, a connector of code 0x28 between a node and its next at the same s and
  another z (|ds| < DAT_1804e9250 < |dz|), its normals by FUN_18015f2f0 mode 0 (asm 18015f4dd..18015f593), cap 0x7fff -> -4);
  FUN_180181680 (the array back into the list).
Op 2's lists have no vertical segment (FUN_180182070 = 0 for all 22,124 segments of both kinds, and for the corpus sample's 81,925), so
FUN_180183080's branch is not exercised by any input yet.

**Allocator boundaries found** (where the harness stops by the ruling; the callees named are the testable ones):
| function | pl2 use | consequence |
|---|---|---|
| FUN_1801cfb60 | pl2MemBufRemainFit(0x68) + FUN_1800ce330 -> pl2StackAlloc (the store, l.310682-310694), FUN_1801b3130 (work arrays local_3558 / 3638 / 3550, modes 0 / 2), FUN_1800cddd0 (line tables) | not run (12.18) |
| the store DAT_1804e9328 (a global, not an argument) | from pl2StackAlloc | FUN_180182e70, FUN_180183080, FUN_1801a2760, FUN_1801a5510 / 5570, FUN_1801974e0 and its callees, FUN_180184cc0, FUN_180181680, FUN_18018fd80's FUN_1801cad70 / FUN_1801cfa90 / FUN_1801c5a50 / FUN_1801c5620 / FUN_180182b00 / FUN_180185e50, FUN_180182de0 -> FUN_1801a1390 / FUN_1801a17a0 / FUN_1801a1a70: no DLL oracle |
| FUN_180184cc0 | FUN_180158460 x 3 -> FUN_1800cdfa0 -> pl2StackAlloc, only when nvcs > 0 (l.261001-261018) | (also store-bound) |
| FUN_18018fd80 | FUN_1800cdfa0(2, 0, 1, ..) + FUN_1800ce260 (its 0x58-byte node array local_d0, (n + 10) x 2 nodes) | not run; its callees FUN_1801cb640(n, m, local_d0, DAT_1804e9240), FUN_1801cdcf0(.., local_d0 ..), FUN_1801cac00(n, .., local_d0, file) take that array as a direct argument (testable); FUN_1801cad70 builds it from the store |

**NOT PORTED / NOT DECODED here:** lines with walls (FUN_1801b0c80's reads, FUN_180184cc0's vcs branch FUN_18015bca0 / FUN_1801c8080
and its pl2 arrays; the corpus sample has 135 such lines, refused); FUN_18015f2f0 mode 1 (z limits); containment (FUN_180192af0);
FUN_18018fd80 (endpoint flags: FUN_1801cad70 354 l., FUN_1801cb640 -> FUN_1801cb510 -> FUN_1801ca500, FUN_1801cdcf0 -> FUN_18015add0 /
FUN_1801cc0f0 -> FUN_18015c590 / FUN_1801616d0, FUN_1801cfa90, FUN_1801c5a50, FUN_1801c5620 / FUN_180182b00, FUN_180185e50,
FUN_1801cf800; about 1.5k lines); FUN_180182de0(2, ..) -> FUN_1801a1a70 (the temp.rgn line block); the rufvcs copy FUN_1801c9350 and the
trailer (l.310888ff). So no temp.rgn yet, and the DLL's own reader of it (FUN_18025d6d0(2, ..) / FUN_18015da80 / FUN_180186bb0, 12.4)
was not run. msurf_max_short_gap (param_22) is taken from the S1 oracle's record of FUN_1801cfb60's arguments (0.15 on op 2), not yet
from cs_op43_params (msurf_map l.340020-340022 / 340443-340477: op+0x1228, or op+0x1230 x slice when op+0x1220).

**Oracle results (2026-11-14):**
- CURVE 5 AXIS REVIEW FILE op 2, the real scan files of both kinds (12.17; chord 0.001, system tol 5e-5, corner deviation 0.001, gap
  0.15): kind 7: 118 lines (2 empty), 11,041 segments, FUN_180182070 per segment, 118 arrays / 11,149 nodes (108 connectors), FUN_180182620
  + FUN_180181a80 (1453 recoded) identical; kind 6: 118 lines, 11,083 segments, 118 arrays / 11,191 nodes, 1493 recoded identical; code
  families identical over -3 .. 0x45.
- Corpus sample (tools/tests/cfbu_sample.sh on scn_oracle.sh's 37 parts with surfaces; unit-test scanner arguments and 20 test lines
  per part): 730 lines / 81,925 segments, 595 arrays / 67,864 nodes / 11,957 recoded identical, 0 differ; 135 lines with walls refused.
- Runs: `tools/tests/cfbu_cmp.sh WORK/apx/port/port_scn_apx.bin 7 CHORD SYSTOL CDEV GAP OUT WORK/apx/port/port_vcs.bin` (after apx_cmp.sh
  WORK 6; kind 6 after apx_cmp.sh WORK 5); `tools/tests/cfbu_sample.sh SCNDIR OUT`.

**Where this stopped (session 9).** Next, in order: (1) FUN_18018fd80 - port FUN_1801cad70 (store -> the 0x58 array, no oracle),
FUN_1801cb640 / FUN_1801cdcf0 / FUN_1801cac00 (array arguments: a CFBA-like unit mode on the port's arrays), FUN_1801cfa90 / FUN_1801c5a50
/ FUN_1801c5620 / FUN_180182b00 (store); check local_3448 (FUN_1801cac00 writes into it only when it is open); (2) FUN_180182de0(2) ->
FUN_1801a1a70 (the line block: 0x18-byte points +0 s, +8 z, +0x10 code, +0x12 flag, joints within DAT_1804e9250 merged with code + 0x27,
the 0x18-byte block header) and the temp.rgn frame (FUN_18025d6d0 header, FUN_1801c9350, the trailer l.310888ff, kind 7 FUN_180268b30 /
kind 6 FUN_18022c850); then the DLL's own reader of temp.rgn on the port's file (FUN_18025d6d0(2, ..) + FUN_18015da80 / FUN_180186bb0, as
SCNR did for the scan file); (3) stage D. Op 43 stays greyed.

### 12.20 Stage C item 3 continued: the line list on a harness store (N13-31), FUN_18018fd80, the temp.rgn line blocks (2026-11-14, session 10) - PORTED, = DLL
**User decision N13-31** (formats/user_decisions.md): in the test harness only, FUN_1801cfb60's global node list DAT_1804e9328 may point at a
harness-owned fixed 0x7fff-node array (as SCN mode does for DAT_1804ea4e0); no pl2MemBufRemainFit / pl2StackAlloc answer, no NC buffer, no
licence code; FUN_1801cfb60 is not run whole; a function that allocates through pl2 itself is not run: its callees are.
Port: viewer/cam3d_cscallop_cfb.c (list), viewer/cam3d_cscallop_fd80.c (new: FUN_18018fd80 and the temp.rgn block), driver viewer/cfb_port
(make cfb_port), harness tools/peloader/mcmsurf_lmm.c mode **CFBL** ("CFBL file n t9238 t9240 t9248 t9250 t9258 gap [vcsfile]"), scripts
tools/tests/cfbu_cmp.sh (now 5 checks), cfbu_sample.sh, tools/tests/cfb_synth.py (synthetic scan files: test inputs only).

**CFBL mode.** DAT_1804e9238 .. 9258 = the case's values (cfb_port's CONST, 12.19), DAT_1804e9320 = 0x7fff (N13-28), DAT_1804e8fff = 0
(l.310261 / 310681), mccore tol = ST; the store initialised by the DLL's FUN_1801a5570 (l.310760). Per line of the scan file's table (the
DLL's FUN_1801e7ad0) whose chain != -1, in FUN_1801cfb60's order (l.311101-311141): FUN_180183310(file, pos, FUN_1801a6ee0, **FUN_180182e70**,
1, buf, p7, &p8) - the real inserter (FUN_180183080 / FUN_1801a2760 inside) -> "LN"; FUN_1801974e0 -> "EST i n hash freehead nfree err"
(hash over list order: store index + the node's bytes, padding zero); a line with walls is skipped as the port refuses it; FUN_180184cc0(0,
{0, 1}, 0, NULL, 0x7fff, arr) -> "AR"; FUN_1801a5510, FUN_180182620, FUN_180181a80(0, gap, n, arr), FUN_180181680 -> "RL"; then FUN_18018fd80's
body (below) and FUN_1801a1a70; FUN_1801a5510 (LAB_1801d2ea1). CFB_DUMP=1 / 2 (both sides) dump every node ("N"), FUN_1801d7da0 alone ("D7")
and each FUN_1801a2cc0 / FUN_1801a1d80 run ("RUN", "TR"). A list that loops ("CYCLE i", the store re-initialised by FUN_1801a5570) happens
on cfb_synth.py inputs whose blocks come in arbitrary s order: FUN_180183080 (l.259840) can return a place whose prev is not before next
(it restarts from the head keeping *prev), and the DLL's own FUN_1801974e0 then never returns - the port does the same.

**FUN_18018fd80(1, 0, local_3448, 0) for kinds 6 / 7** (l.267871, asm 18018fd80..1801900a0; param_1 = local_3647 = FUN_18018b500's 11th
output, 1 for kinds 6 / 7 (l.264801, call asm 1801d06e8); local_3448 is a SysFile constructed at l.309830 and never opened, so FUN_1801cac00
is never called; param_4 = (kind - 8) < 2 = 0). It takes two arrays from pl2 (FUN_1800cdfa0(2, 0, 1): (n + 10) x 2 0x58-byte nodes;
FUN_1800ce260(n + 10): shorts) and its FUN_1801cb640 a byte array (FUN_1800cdfa0), so per N13-31 neither runs whole: CFBL repeats the body
with harness arrays and calls each callee on its asm arguments (asm 18018fe4f..18019003c):
1. every list node's flags f0 / f1 = param_2 = 0 (l.267916-267922);
2. FUN_1801cf800(1) = max(9238 x 2, tol x 10), (2) = 165 deg, (3) = 15 deg (op type 0x27 with tol < op+0x12d0 < 180 changes them; op 43 is
   not 0x27), (6) = 9238 x 3 (l.309326); dmin = tol + tol; dir = FUN_180185e50 (l.261571: the first node not code 0x24 with |ds| > 9250:
   s0 < s1);
3. FUN_18015d700(1, 1, 1, dir, 0, 9238, cf800(3), f) (l.232366): the block FUN_18015f2f0 mode 1 reads;
4. FUN_1801c5620(0x24) (l.302394: a node between two up-steps > 9238 and shorter than 9250 gets code 0x24) and, when any, FUN_180182b00(0x24)
   (l.259651, FUN_1801a12f0 l.277379: those nodes unlinked) -> "FP i pits dir" + "FS";
5. FUN_1801cad70(0, DAT_1804e92b8, f, &nruns, arr, 0) (l.306117; the decompile's "bVar == true -> +2" field offsets are wrong: the asm
   1801cae4a..1801cae7d indexes by the bool's value, so +8 = the line position, +0x10 = s, +0x18 = z): runs of 0x58 nodes {+0 link (a run's
   first node: its last node's index), +2 flag, +8 pos, +0x10 s, +0x18 z, +0x20 normal a, +0x38 normal b, +0x50 / +0x52 codes, +0x54 / +0x56
   store nodes}: the run's start, at each joint one node (|ds| < 9250 and |dz| < 9250; |dz| == 9250 ends the run) or, for a step, two nodes
   whose shared normal is FUN_18015f2f0 **mode 1** (l.233433 with f[0] = 1, f[2] = 1, f[4] = 0, f[5] = 1: (0, y, 0), or k.n1 when |k.z1 -
   nx.z0| < 10 x 9238 and their angle < cf800(3), else y = -1 / +1 by the step direction and dir), the run's end (|ds| >= 9250 or the
   list's end) -> "FA i n nruns hash";
6. FUN_1801cb640's body (l.306524): per run FUN_1801cb510(st, end, arr, DAT_1804e9240, cf800(3), mark) (l.306471: windows of 100;
   FUN_1801ca500 l.305828: the chord test in (pos, s, z) and the normals' angle), the marked nodes' code a = 0x24 -> "FB";
7. FUN_1801cdcf0(dir, 0, 1, nruns, arr, cf800(1), 2 tol, cf800(2), cf800(6), list) (l.308284): per inner node with at most one 0x24 code
   FUN_1801cc0f0 (l.307112, asm 1801cc0f0..1801cc681: FUN_1801616d0 l.235050 side vectors, mutl_v_angle_3d l.398750, the offset lines (0.01 =
   0x1803f3ef0 along each side's (s, z) normal) intersected by mccore l_ints (import 0x1803e7340), FUN_18015c590 l.231526 / FUN_180157540
   l.227678 replacement normals at a fold) -> flag 0 / 1 / 2 / 3; the flag-3 list thinned by FUN_18015add0 (l.230308) / FUN_18014b830
   (l.218057) to the lowest z of each cluster -> "FC i k hash list";
8. FUN_1801cfa90(nruns, arr) (l.309461: the inner nodes' flags back to the store's f0 / f1), and when > 0 FUN_1801c5a50(3) (l.302550: flag 3
   spread across joints and point-like nodes) -> "FD i n3 spread" + "FL".
acos / sqrt: the C library's (the DLL's are MSVCR120's); acos is used in comparisons only. pos = the walk's line position p8 in the unit
check (FUN_1801cfb60's is DAT_1804e92b8, += DAT_1804e9228 per line).

**The temp.rgn line block** FUN_180182de0(2, &DAT_1804e9060, &DAT_1804e92ec, DAT_1804e92f8) -> FUN_1801a1a70 (l.277668), cs_rgn_block: the
list (code 0x24 left out) as 0x18-byte points {s, z, short code, byte flag}: the first start; per joint either the previous end and this
start (|ds| or |dz| > 9250) or their midpoint with this node's code + 0x27; the last end; the 0x18 trailer {byte 2, short count, int
table[line] (the line's previous block), double DAT_1804e92b8}; off += count x 0x18, table[line] = off, off += 0x18. Bytes the DLL leaves
from its stack (points +0x13..+0x17, trailer +1 and +0x10..+0x17) are 0 in the port and masked by the check. Unit check: CFBL_RGN / CFB_RGN
from offset 0 with the table at -1 (FUN_1801cfb60's start offset and table come from the frame, not ported).

**NaN-faithful comparisons (a port fix, shared with the scanner):** a run of zero-length nodes makes FUN_1801a5bc0's t = acc / total = 0 / 0.
The asm's comisd + jbe / jb send an unordered value the way "not greater" goes (FUN_1801a55f0 asm 1801a5727 / 1801a5748 (jb: pass 0 taken),
1801a59d7, 1801a5a3b, 1801a5a9e, 1801a5b02; FUN_1801a5bc0 1801a5cb6..1801a5cd6, 1801a5d8a, 1801a5ecb; the scanner twins FUN_1801e6a70 /
FUN_1801e6fe0 have the same jumps); the port now reads !(a > b) where the asm has jbe (cam3d_cscallop_merge.c run_straight / norm_dev). Found
with cfb_synth.py; op 2 and the corpus never had such a run (scn_cmp --full on op 2 unchanged: 120 lines, scan file identical).

**msurf_max_short_gap** (FUN_1801cfb60's param_22: FUN_180133f10 passes the export 1804d6f90, asm 180135203..18013520b) is now computed by
cs_op43_params as msurf_map does: op+0x1228 (l.340022), or with op+0x1220 (l.340020) the slice (op+0x1298, before the 0.05 clamp of l.340495)
x op+0x1230 (l.340476; op type 0x2b is in none of the lists of l.340446-340474). CURVE 5 AXIS op 2: 0.05 x 3.0 = 0x1.3333333333334p-3 = the
S1 record (cs_port prints "S1GAP"). The later write in FUN_18007abe0 (l.81192) is after stage C.

**Oracle results (2026-11-14, session 10)** (tools/tests/cfbu_cmp.sh, every check byte for byte):
- CURVE 5 AXIS op 2, kind 7: 118 lines / 11,041 segments; list insert + FUN_1801974e0 + FUN_180184cc0 + FUN_180181680: 118 lists / 11,149
  nodes identical (store indices, free list and error included); FUN_18018fd80 callees: 11,267 array nodes, 116 corners kept, 116 flag-3
  ends; FUN_1801a1a70: 118 blocks / 273,240 bytes identical. Kind 6: 118 lists / 11,191 nodes; 11,309 array nodes, 116 corners; 118 blocks /
  274,248 bytes identical.
- Corpus sample (cfbu_sample.sh, 37 parts): 37 / 37 identical: 730 lists, 595 rebuilt / 67,864 nodes, FUN_18018fd80 68,593 array nodes /
  368 corners, 595 temp.rgn blocks / 1,660,512 bytes; 135 wall lines refused (as before).
- Synthetic (cfb_synth.py seeds 1, 2, 4, 5, 7-10, 60 lines each, with vertical segments, so FUN_180183080 is exercised): all five checks
  identical (seed 1 has one CYCLE line on both sides).

**NOT PORTED / NOT DECODED (stage C):** lines with walls (FUN_1801b0c80's reads, FUN_180184cc0's vcs branch FUN_18015bca0 / FUN_1801c8080
and its pl2 arrays: 135 sample lines refused); the second vcs pass (DAT_1804e92a9); containment (FUN_180192af0); the temp.rgn frame: its
header FUN_18025d6d0(local_3646 == 0, &DAT_1804e9060, &DAT_1804e92ec, &local_3500) (l.310658), the start of DAT_1804e92ec / the table
DAT_1804e92f8 (l.310652-310672), the rufvcs copy FUN_1801c9350 (l.310859, when DAT_1804e92a9 and local_3671) and the trailer (l.310888ff:
frame rows DAT_1804e9150 / 9168 / 9180, 9238, 9240 / 5, 9248, 9250, DAT_1804e9288, param_6, ...); so no whole temp.rgn yet and the DLL's
reader (FUN_18025d6d0(2, ..) / FUN_18015da80 / FUN_180186bb0) was not run on one. FUN_18015f2f0 with f[2] = 0 (the direction search) is not
on this path (f[2] = 1 here; mode 0 is 12.19's); the z-limit pass FUN_1801a6d00 is reached only from FUN_180184cc0(1, ..) with
msurf_use_zlimits, not from FUN_1801cfb60's calls (param_1 = 0).

**Where this stopped (session 10).** Next: (1) the temp.rgn frame (header, start offset / table, FUN_1801c9350, trailer) and the DLL's reader
on the port's whole temp.rgn (a mode like SCNR); (2) walls (FUN_1801b0c80 + FUN_180184cc0's vcs branch: FUN_180158460 -> pl2 arrays, so a
CFBL-style repeat of its body with harness arrays); (3) stage D (FUN_1801d3ca0 -> FUN_18018e630 -> FUN_1801b1ad0). Op 43 stays greyed.

### 12.21 Stage C: the temp.rgn frame - header, FUN_1801c9350, trailer, the whole file of both kinds (2026-11-14, session 11) - PORTED, = DLL
Sources: decomp/mcmsurf_all.c (l.N), objdump of mcmsurf.dll (asm). Port: viewer/cam3d_cscallop_rgn.c (cs_rgn_tool, cs_rgn_vcs_copy,
cs_rgn_trailer), the run of one kind in viewer/cfb_port.c (rgn_kind; `cfb_port --rgn` writes the whole file, `--vcs` FUN_1801c9350 alone);
harness tools/peloader/mcmsurf_lmm.c modes **RGNV** / **RGNR** (and APX of 12.17); scripts tools/tests/rgn_cmp.sh, rgn_vcs_sample.sh.

**The file FUN_1801cfb60 leaves for op 43** (FUN_180133f10 mode 6 -> kind 7 with param_2 = 1, param_3 = 0; mode 5 -> kind 6 with 0, 1):
1. kind 7: FUN_18025d6d0(local_3646 == 0 -> 0, ..) (l.310658, l.399067): created, header FUN_180229640(-0x38, -1, -1) written, DAT_1804e92ec
   = 0x38; kind 6: FUN_18025d6d0(1, ..) reads the header into local_3500, then SetPosition(0, 2) and DAT_1804e92ec = the end (l.310665-310672).
   Both: the line table DAT_1804e92f8 = -1 (l.310650-310656).
2. The per-line blocks (12.20, FUN_1801a1a70); the line position DAT_1804e92b8 = DAT_1804e9230 (l.310762) += DAT_1804e9228 after EVERY line
   (LAB_1801d2ea1, l.311253): an accumulated sum, not the scanner's p8 (on op 2 all 118 block positions differ from p8 in the last bits; the
   points themselves are the same bytes).
3. FUN_1801c9350(&DAT_1804e9120, &DAT_1804e9060) (l.310858-310866, asm 1801d3386..1801d33ab: when DAT_1804e92a9 (the scanner path) and
   local_3671): seek to the end; per line FUN_1801b0c80 (l.288238) reads the line's wall records from rufvcs (the scanner's plmvcs00.tmp
   renamed by FUN_1801868a0, its table loaded by FUN_1801e7c20: int n + n x {int offset, short count, 2 bytes}) along the chain of 0x18
   trailers (FUN_1801a6f50, asm 1801a6f50: returns read == 0x18; +2 count, +4 previous, +8 position) into DAT_1804e97a8 (500 records,
   DAT_1804e97a0 = 500; a table count or a block count > 500 gives nothing - "'vcs' array too small" / "reading file fb_vertd"); count >= 1:
   the records, then a 0x18 trailer {+2 count, +4 -1, +8 the last read position, +0x10 short 0; +0 / +1 and +0x12..+0x17 not written (the
   DLL's stack, asm 1801c942f..1801c946d)}, DAT_1804e9798[line] = {the trailer's offset, count}, DAT_1804e97a4 = max; else {-1, 0}.
   A line whose blocks hold more than 500 records in all would overrun the pool in the DLL: NOT PORTED (refused).
4. The trailer at DAT_1804e92ec = the position (l.310883-310955, asm 1801d33df..1801d3780; one pass, local_3654 = 0): short 0 (local_360c);
   the frame rows DAT_1804e9150 / 9168 / 9180 (-param_9, param_7, param_8, l.309983-309991); DAT_1804e9238; DAT_1804e9240 / 5.0 (0x1803eb840);
   DAT_1804e9248; DAT_1804e9250; the 0x18 bytes at DAT_1804e9288; param_6 (D); DAT_1804e9148; DAT_1804e9230 (-param_16); DAT_1804e9228
   (-param_17); int (param_21 != 0) = 0; int n + the line table (local_35e8 = 0, l.310840); int -1 (no containment); short DAT_1804e97a4 and,
   when > 0, DAT_1804e9798 (n x 8). Then SysFile::Close.
   The tool block (l.310085-310115, asm 1801d0981..1801d0abb): s = stock, or -r when -stock > r; a = s + r (0 unless > DAT_1804e9248, flag
   9298); b = (2 s + D) x 0.5 - a (0 unless > 9248, flag 9299; then a = (2 s + D) x 0.5); DAT_1804e9288 = b, 9290 = a, 9298 / 9299 the flags,
   929a..929f never stored (.bss, 0). DAT_1804e9148 = a - s (asm 1801d33af: local_3608 = a, xmm10 = local_3620 = s). Tool types 0x19 / 0x17
   with FUN_18021b330 / FUN_18021b3b0 take cur_op +0x2f0 for 9148: refused (as the harness setup refuses those tools).
5. kind 7: FUN_180229640(-0x38, 0x11, 0x20) (switch l.310958-310986, case 6 / 7), +0x18 = its trailer (0x2b0 mask, asm 1801d3813..1801d382b),
   +0 = 0 (l.311013), FUN_180268b30 (l.405081: the header written at 0). kind 6: the header as read, +0xc = its trailer (asm 1801d3833), +0 =
   -0x38 (asm 1801d387d), FUN_18022c850 (12.17's appendix, cs_scan_appendix; its inputs, the same as the scan file's, are read back from the
   port's own scan-file appendix).
6. The second wall pass (asm 1801d2da6..1801d2e32) is unreachable on this path: it needs [rbp+0x4a1] (local_3157) != 0 and r15b == 0, and
   r15b is loaded from [rbp+0x4a1] after the first pass (asm 1801d2ccb) with no store in between.
Header bytes +0x24..+0x27 (until FUN_18022c850 sets them) and +0x31..+0x37 are FUN_1801cfb60's stack in the DLL (zero in the port).

**Oracle** (tools/tests/rgn_cmp.sh WORK CHORD SYSTOL CDEV GAP STOCK R OUT; WORK = s1_cmp.py's work directory with apx_cmp.sh's mode 6 / 5
output copied to WORK/apx7 and WORK/apx6):
- RGNV: the DLL's own FUN_1801868a0(1, kind == 7) + FUN_18025d6d0(1) + FUN_1801c9350 on the port's file before the copy, DAT_1804e9798 /
  97a8 harness arrays (calloc'd in the DLL by FUN_1800ce330(.., 1), whose FUN_1800cdfa0 also calls pl2MemBufRemainAdjust: N13-31's rule);
- APX: the DLL's own FUN_18022c850 on the port's whole file without the appendix -> byte for byte;
- RGNR: the DLL's FUN_18025d6d0(2) (as FUN_1801d3ca0 l.311471), FUN_18015da80's reads repeated (its table comes from FUN_1800cddd0 mode 2 =
  pl2StackAlloc: not run), the DLL's FUN_180186bb0 (the globals) and FUN_1801b5fb0 (each line's block chain, FUN_1801b4e80) on record 0
  (kind 6) and record 1 (kind 7; FUN_18018f780's reads repeated: it allocates by pl2StackAlloc);
- tools/tests/rgn_vcs_sample.sh OUT LIST: FUN_1801c9350 alone on the corpus sample's wall files (cs_port's port_vcs.bin, after a bare header).
The diagnostic file DAT_1804e8fb0 (m_fopen_hl "tmpdiag.tmp", l.309999) is opened by the harness: FUN_1801b0c80 prints to it.

**Results (2026-11-14, session 11):**
- CURVE 5 AXIS REVIEW FILE op 2 (chord 0.001, system tol 5e-5, corner deviation 0.001, gap 0.15, stock 0, r 0.375): the port's whole
  temp.rgn, 561,405 bytes = header + kind 7 (118 blocks, 273,240 bytes; no walls; trailer at 273,296) + kind 6 (118 blocks; trailer at
  548,200) + the appendix: FUN_18022c850 on it gives the port's file byte for byte; FUN_1801c9350 identical for both kinds (nothing to copy);
  both records read back by the DLL identical (n 120 / 120, FUN_1801b5fb0 129 / 127 = the longest line's 119 / 117 points + 10).
- Corpus sample (s10 list = every 10th of the 908 corpus parts sorted, 91 parts; 12 with walls, 133 wall lines): FUN_1801c9350 identical on
  12 / 12, 4,248 records copied; one part (SWEEP3) has 9 lines with more than 500 walls, dropped by both ("'vcs' array too small").
  With 100 test lines per part (rgn_vcs_sample.sh OUT LIST 100): 16 parts with walls, 651 wall lines, 19,778 records, 16 / 16 identical.

### 12.22 Stage C: lines with walls - FUN_1801b0c80 and FUN_180184cc0's wall branch (2026-11-14, session 11) - PORTED, = DLL
Sources: decomp/mcmsurf_all.c (l.N), objdump of mcmsurf.dll (asm; the decompile loses the arguments of FUN_18015aff0, FUN_18014d900 and
FUN_180162300 and several stores, so all of it is read from the asm). Port: viewer/cam3d_cscallop_wall.c (cs_wall_split, cs_wall_connector,
cs_wall_read), cs_cfb_to_array_v (viewer/cam3d_cscallop_cfb.c); harness: CFBL mode now runs wall lines (h_4cc0_walls), mode **WSPL**;
scripts tools/tests/wall_synth.py / wall_cmp.sh (synthetic gaps and walls: test inputs only), cfbu_sample.sh unchanged.

**The per-line call** (12.19 / 12.20): DAT_1804e9794 = FUN_1801b0c80(rufvcs, FUN_1801a6ee0, ..) - the line's wall records (the chain of 12.21
item 3, the same reader) into DAT_1804e97a8; n = FUN_180184cc0(0, flags, DAT_1804e9794, DAT_1804e97a8, ..). With nvcs > 0 FUN_180184cc0 takes
three work arrays of nvcs + 5 records from pl2 (FUN_180158460 -> FUN_1800cdfa0 mode 2, asm 180184d55..180184dd7; with the large buffer they
never fail, N13-28), and at a vertical gap (|k.s1 - nx.s0| < 9250 < |k.z1 - nx.z0|) passes pts = {k.s1, k.z1, nx.s0, nx.z0} (asm
180184fce..180185028) to:
1. **FUN_18015bca0**(pts, d = 9238 x 8.0 + 9258 (0x1804140a0), nvcs, walls, A, B, C, &arr[n]) (asm 18015bca0..18015be99): P = the gap from its
   lower end (dir = pts[3] > pts[1]); window [min(s) - d, max(s) + d]; **FUN_18014aef0** (asm 18014aef0..18014b12b): each wall strictly inside
   the window in s, turned to start at its lower end (kept when z1 > z0), into A when FUN_18015aff0(code) (code 0..0x11), else B when
   FUN_18015a310 (0x12..0x23); **FUN_18014cb60**(2, ..) (asm 18014cb60..18014ccd7): insertion sort by z0, strict >; **FUN_180145d80** /
   **FUN_18014d900** (asm 180145d80 / 18014d900): runs merged while the next z0 <= the run's z1 + mccore tol, the run's end taken from a wall
   reaching higher; A then B into C, C sorted; **FUN_180151510**(P, dir, nC, C, out) (asm 180151510..1801518e3): nothing when P lies above or
   below all of C; R1 / R2 = the nearest wall points to P's two ends (**FUN_180162300** mode 1, asm 180162300..1801624b5: FUN_180153b60 /
   FUN_1801539d0 clamped projection in (s, z), FUN_1802535d0 normal lerp, the search ends at d < tol or when the next wall's z0 is farther
   than the best); |R2.idx - R1.idx| + 1 nodes from R1.idx stepping with dir: code 2 (FUN_18015aff0 of the wall's code) or 0x14, flags 0,
   chained ends: each inner end = the projection on P of the wall's end (FUN_180162300 over P alone), the first start / last end = P's with
   R1's / R2's normal; a failed projection returns the count so far.
2. when that gives 0: **FUN_1801c8080**(pts, nvcs, walls, &arr[n]) (asm 1801c8080..1801c8675): the walls chained by **FUN_1801c7aa0** /
   **FUN_1801c74f0** (asm 1801c7aa0 / 1801c74f0: an unused wall, then every later unused wall continuing it end to start within 9250 either
   way; used walls marked code = -1 - code, restored at the end); for each chain taller than 9238 the gap's lower end A and upper end B are
   projected on its line (systypes proj_pt_ln; -d3 < t < 1 + d3, d3 = 9238 / height) and the closest within |seg.s0 - seg.s1| + d in s gives
   that end its normal (the chain's start normal for A, end normal for B) and code; one connector (pts[0..1] -> pts[2..3], code 0x28 -> 2 /
   0x14 from A's code, else B's), returns 2 (both), 0 (A), 1 (B) or -1 (none); > -1 counts as 1 node.
3. else (or nvcs = 0) the straight connector of 12.19 (FUN_18015f2f0).
The r nodes get f0 = k.f1 on the first, else 1, f1 = nx.f0 on the last, else 1 (asm 1801850b0..18018510e). The second wall pass of
FUN_1801cfb60 is unreachable (12.21 item 6).

**Oracle (2026-11-14, session 11):**
- tools/tests/wall_cmp.sh (WSPL: the DLL's own FUN_18015bca0 and FUN_1801c8080, their arrays harness-owned direct arguments), wall_synth.py
  seeds 1-3, 400 cases each: all identical - 936 gaps split (1,809 nodes, 624 into more than one), 704 FUN_1801c8080 connectors (185 both
  ends, 305 lower only, 214 upper only), the walls' codes restored.
- CFBL with walls (cfbu_cmp.sh: the DLL's FUN_1801b0c80 on the port's wall file opened as DAT_1804e9120, table by FUN_1801e7c20, DAT_1804e97a8
  a harness pool; FUN_180184cc0's body repeated in the harness with harness work arrays, every callee the DLL's): corpus sample (s10, 37 parts
  with surfaces) 37 / 37 identical with 20 test lines (124 wall lines: 48 gaps split, 937 straight) and with 100 test lines (602 wall lines:
  221 split, 11 FUN_1801c8080 connectors, 4,361 straight; lists, arrays, FUN_18018fd80 and temp.rgn blocks). The 135 sample lines refused
  before (a wall table entry != -1) now run and match: 124 read walls, the rest have more than 500 (FUN_1801b0c80 gives 0 on both sides).
  The harness opens DAT_1804e8fb0 (tmpdiag.tmp) for FUN_1801b0c80's message.

### 12.23 Stage D mapped: FUN_1801d3ca0 for op 0x2b (method 2, header kind 0x11, no containment, no z limits) (2026-11-14, session 11)
A read-only map (call tree, branch values, files, proposed port stages); nothing of stage D is ported yet. Roles marked NOT DECODED
are structural only. Stage C's outputs feed it: temp.rgn (12.21) is read, rewritten per direction into tmpemesh.tmp by FUN_18018e630,
then FUN_1801b1ad0 builds the zones (tmpzone / tmpdzdat / tmpdzmap) and FUN_1801b0df0 orders them into cut.tmp / msproj.tmp (stage E).

Sources: decompile decomp/mcmsurf_all.c (l.N), objdump of mcmsurf.dll (asm ADDR).
Sizes: "own" = lines of the function in the decompile; "clos" = functions / lines of its closure (transitive FUN_ references
inside the decompile). "PL2" = something in the closure calls pl2StackAlloc / pl2MemBufRemainFit / pl2MemBufRemainAdjust /
edge_alloc / edge_fit / edge_return. Note: a text search for edge_* also matches `edge_motion_color_exref` (a colour global, FUN_1801aa5e0
l.283712): FUN_1801aa5e0 is a false positive.
The allocator layer met everywhere: FUN_1800cdfa0 (l.134848, buffer-descriptor array alloc: mode 0 pl2StackAlloc, 1 calloc, 2
pl2StackAlloc then calloc, l.134876-134895; descriptors 0x30 bytes: +0xc count, +0x10 size, +0x18 kind, +0x20 size, +0x28 ptr),
FUN_1800ce590 (free, edge_return + pl2MemBufRemainAdjust), FUN_1800ce4b0 / FUN_1800cde60 / FUN_1800ce3a0 (pl2MemBufRemainAdjust),
FUN_1801b86c0 (edge_alloc of 0xb0 nodes, l.293104), FUN_1801aaaa0 (edge_fit / pl2MemBufRemainFit), FUN_18016a3e0 / 18016a600 / 18016a660 /
18016a150 / 18016f480 / 18016f6b0 / 18017ad00 / 18014e5f0 (pl2StackAlloc), FUN_18016a040 / 18016a500 / 18016a6b0 (edge_alloc/fit).

Header struct (local_828 at rbp-0x50 in FUN_1801d3ca0, 0x38 bytes read by FUN_18025d6d0 l.399141): +8 uStack_820 low = header +8
= kind (0x11 for the op-0x2b file, 12.21 item 5), high = header +0xc = kind-6 trailer; +0x18 uStack_810 low = header +0x18 = kind-7
trailer (asm 1801d3f40 / 1801d3fed read [rbp-0x38]).

#### Call tree (path taken for op 0x2b, method 2, header kind 0x11, no containment, no z limits)

```
FUN_1801d3ca0 l.311384-311848 (465; clos 2390 fn / 232k incl. not-taken FUN_180301e00) own: edge_fit, pl2StackAlloc
  m_fopen_hl "tmpdiag.tmp" "w" -> DAT_1804e8fb0                                  l.311457-311459
  FUN_18023ec20 x3 (direction contexts local_7b8/598/378, 0x220 each)            l.311465-311467
  FUN_18025d6d0(2, &DAT_1804e9080, &DAT_1804e92ec, &hdr) open temp.rgn "rb"     l.311471
  FUN_18015da80(file, &hdr, 0, 2)  record 0 = kind 6 -> local_7b8 [PL2]          l.311477 (l.232514, clos 272)
  FUN_180186bb0(local_7b8) -> DAT_1804e91xx/92xx globals                         l.311480 (l.262163, 56)
     FUN_180190760, FUN_180207f00 (DAT_1804d7038)
  FUN_18015de40 (6 outputs, asm 1801d3e83-1801d3ea5)                             l.311481 (l.232613, 14)
  FUN_1801b5fb0(file, DAT_1804e9144, local_830)  kind-6 chain sizes              l.311487 (l.291319, clos 78)
  [kind 0x11 arm l.311510-311531]
  FUN_18018f780(local_830, 1, file, hdr+0x18, &DAT_1804d5390, err) [own pl2StackAlloc]  l.311513 / asm 1801d3ffa (l.267677, 193)
  FUN_1801b5fb0(file, DAT_1804d5788, DAT_1804d57c0)  kind-7 chain sizes          l.311517
  edge_fit(0x68) x2, pl2StackAlloc: DAT_1804e9328 (pool), DAT_1804e9310, DAT_1804e9318   l.311538-311553, l.311750
  FUN_180266760 (save name), FUN_180266820("tmpemesh.tmp")                       l.311756-311758
  FUN_18018e630(file, &hdr, cVar9, 1, 1, 0, &DAT_1804d5390, abort)  dir B (kind 7) -> new tmpemesh   l.311759 / asm 1801d47ef
  FUN_18018e630(file, &hdr, cVar9, 0, 0, 1, &DAT_1804d5390, abort)  dir A (kind 6) appended          l.311762 / asm 1801d482a
     (l.266927-267367, 441; clos 94 fn / 6581; own pl2StackAlloc x4, edge_return)
     FUN_1801ada90, FUN_1801ad940, FUN_18015d700, FUN_1801900b0, FUN_18025d6d0, FUN_180229640, FUN_1801a5570
     per line: FUN_1801a1f60 (+FUN_1801a6f50, FUN_1801a7a00)  l.267196 (l.277861, clos 545)
               FUN_180184cc0  l.267199 (l.260939, clos 2207 PL2 via FUN_180158460)
               FUN_1801adf60 (dir B only)  l.267207
               FUN_1801a9e60 (only DAT_1804d7038 == 1)  l.267229 (l.283339, clos 1251)
               FUN_1801a8e50 / FUN_1801a9150 loop  l.267236-267282 (l.282599 127; l.282782 clos 1409)
               FUN_180182800  l.267295 (l.259536, clos 214)
               FUN_180182de0(2, ..) -> FUN_1801a1a70  l.267297 (l.259751, clos 413)
               FUN_1801a5510
     FUN_1801b4070 (trailer copy)  l.267324 (l.289934, 117)
     FUN_180268b30 (call 1) / FUN_18022c850 (call 2)  l.267328 / l.267336
  SysFile::Close; FUN_18025d6d0(2, ..) reopens: now the name is tmpemesh.tmp      l.311766
  [msurf_use_zlimits: FUN_18018f780 + "tmpzmesh.tmp" + FUN_18018f0d0 x2 - NOT TAKEN]  l.311772-311789
  FUN_1801b1ad0(file=tmpemesh, &hdr, opt_cut_order, prompt_st_pt, start_pt, zones&&m2, 2, 1, colaps_dist,
                DAT_1804e9140, max_short_gap, &zones_n, &local_850, &surf_off, abort)  l.311709 / asm 1801d4969-1801d49d6
     (l.288723-289215, 493; clos 830 fn / 55343; own edge_return)
     FUN_180250500(cur_op) -> op+0x12e0 byte            l.288821 (l.390239)
     FUN_1802504c0(cur_op, &n) "tmptc.tmp" count         l.288830 (l.390220)
     FUN_1800ce680 x5, FUN_18015d4f0(100, &DAT_1804e95b0)  l.288844-288849
     FUN_180186360(DAT_1804e9144, DAT_1804e9310, file, FUN_1801a7a90)  block type byte   l.288857 / asm 1801b1d05-1801b1d1c
     FUN_1801ad6c0(DAT_1804d5729, 500, &local_6f0)  point buffer (calloc)   l.288889 (l.285789)
     SysFile::Open tmpsurf.tmp "wb" -> DAT_1804e9060     l.288908-288912
     FUN_18018f780(DAT_1804e9310, 1, file, hdr+0x18, &DAT_1804d5390, err) [own pl2StackAlloc]  l.288918 / asm 1801b1f10
     FUN_180172160(ctx)   l.288919 (l.247278)
     FUN_1801b4e80(DAT_1804d56c8, DAT_1804d5700, DAT_1804d56f8, FUN_1801a6e70, ..)  l.288926 (l.290541, 42)
     FUN_18024d390 [+ FUN_18015f7b0, FUN_180186210, FUN_180159f70, FUN_18015d280]  l.288932-288937
     dir A: FUN_1801ae960, FUN_1800cdfa0(2,0,3,..) [PL2], FUN_1801a8340, FUN_18018a640(0, ..)  l.288939-288954
     dir B: FUN_1801b4e80, FUN_1801ae960, FUN_1800cdfa0 [PL2], FUN_1801a8340, FUN_18018a640(1, ..)  l.288961-289186
     FUN_18016a2a0(ctx, 100) [PL2 via FUN_1800ce330] -> DAT_1804d53b4 (only if op+0x12e0 == 0)  l.288975
     FUN_180171db0, FUN_1801588f0(1, &DAT_1804e95b0) [PL2]  l.288976-288977
     FUN_1801adf10, FUN_180190140  l.288982-288983
     FUN_1801a7fd0 x2 [PL2: FUN_1801aaaa0 edge_fit, FUN_1800cdfa0]  l.288984 / l.288994 (l.281830, clos 278)
     FUN_1801b88e0 x2  l.288987 / l.288996 (l.293212, clos 123, no PL2)
     FUN_1801b88c0, FUN_1801adab0, FUN_1801b86c0 [own edge_alloc], FUN_1801b84f0  l.288999-289003
     Open tmpzone.tmp / tmpdzmap.tmp / tmpdzdat.tmp "wb+"  l.289122-289162
     FUN_18015d0f0(ctx, abort)  l.289037 (l.232037, clos 114 fn / 7272, PL2)
        FUN_180150840(0/1, ctx, &e) [PL2 via FUN_1801b86c0]  l.232050/232058 (l.222162, clos 925)
        FUN_18014d5c0(0/1, ctx)  (l.219779, clos 739)
        FUN_180149a20(ctx, abort) loop  l.232068 (l.216411, clos 4678, no PL2)
        FUN_180146e60(0/1, ctx, abort) [PL2 via FUN_180169990]  l.232078/232080 (l.214170, clos 2509)
     FUN_18015c8f0(ctx) -> max into DAT_1804d56c0  l.289040 (l.231676, clos 82)
     [if DAT_1804d53b4] (FUN_18015a2b0 if bVar4) + FUN_180186ff0(DAT_1804e9240, ctx, abort)  l.289051-289054
        (l.262313, clos 440 fn / 30768 PL2): "tmpprojz.tmp", "tmpdztmp.tmp" "wb+", FUN_180187850, FUN_180162dd0 (clos 10966 PL2)
     pass loop l.289073-289096:
        FUN_180189a90(tmpzone, tmpdzdat, sf4, &local_718, pass, 1, 0, flag, 10, DAT_1804e9240, ctx, buf, &zone_off, err, abort)
              asm 1801b2c3d-1801b2caa (l.263834, 55; clos 388 fn / 27828 PL2)
           FUN_180187850 (same args)  l.263852 (l.262543-263832, 1290; clos 324 fn / 23480 PL2)
              FUN_18015d200, FUN_1801b9bd0 (kinds 0xe/0xf only), FUN_1801bc5c0, FUN_1801b3000 [PL2], FUN_1800ce4b0 [PL2],
              FUN_1801a8380, FUN_1801be460, FUN_1801c0320 x2, FUN_1801ab410, FUN_1801c32f0, FUN_1801bf0d0, FUN_1801beff0,
              FUN_180182b70 (clos 1116), FUN_18018b180 (clos 1214), FUN_1801bc960, FUN_1801a7d60 [PL2], FUN_1801c3490/3470,
              FUN_180195fc0 (clos 9591 PL2), FUN_1801a7d10 [PL2], FUN_1801bff10 (clos 12303 PL2), FUN_1801ad550 (clos 558),
              FUN_1801c3380, FUN_1801bf2b0, FUN_1801c2b10, FUN_18018b6e0, FUN_1801bfd90, FUN_1801864b0,
              FUN_1801bca00 (604; clos 8051 PL2), FUN_1801c1b80, FUN_1801c0670, FUN_1801c2b40 (clos 1913), FUN_1801c1dd0,
              FUN_1801803f0, FUN_1801c2320, FUN_1801aade0, FUN_1801bd790, FUN_1801c1b50, FUN_1801c2950, FUN_1801b49b0,
              FUN_1801c0a00, FUN_1801c1150, FUN_1801b8d00, FUN_1801c0dc0, FUN_1801bf100, FUN_1801aa5e0, FUN_1801a95e0
              (zone header writer), FUN_1801a9460, FUN_1801be2f0, FUN_1801adec0   (callee lines l.262804-263820)
           [on -0xd retry] FUN_1801bc5c0 x2, FUN_18015d020(ctx, abort) (clos 6732 PL2)  l.263873-263877
        FUN_1801c16d0(tmpdzdat, 0, pass, prev_n, new_n, &local_850, tmpdzmap, err)  asm 1801b2d0d-1801b2d44 (l.299584, 191; clos 441 PL2)
        FUN_1801597b0(tmpzone, zone_off, pass, new_n, 0.8*colaps, 10, .., ..)  asm 1801b2d5d-1801b2d91 (l.229305, 259; clos 1953, no PL2)
           FUN_18014d1b0 (clos 1617), FUN_1802428f0, FUN_1800062d0
        FUN_180192330(1, tmpzone, zone_off, pass, new_n, colaps_dist, ctx)  asm 1801b2da3-1801b2dca (l.269237, 150; clos 11990 PL2)
           FUN_180271e20, FUN_180271c10 (clos 180), FUN_180192720 (155; clos 11647 PL2)
     FUN_1801b87e0 [own edge_return], FUN_1800ce590 frees, FUN_18016da90, FUN_180182a20, FUN_18015acd0, FUN_1801aaa50  l.289098-289203
  [tmpsurf read only if *p14 > 0 - NOT TAKEN, see FUN_1801b1ad0]  l.311716-311739
  DAT_1804e92f8 = local_830   (LAB_1801d4b3b)                                    l.311791-311792
  FUN_1801a5570()  node pool reset                                               l.311794 (l.280052)
  FUN_180141e20(1, DAT_1804e9148, &DAT_1804e9150, &DAT_1804e9168, &DAT_1804e9180)  cut.tmp + msproj.tmp   l.311796 / asm 1801d4b4b-1801d4b74
     FUN_180227b60 (tmpmm2.tmp), get_mmsurf loop  (l.210368, 148; clos 256, no PL2)
  [cVar9 == 0: FUN_1801962a0 and exit - NOT TAKEN when colaps_dist > tol]       l.311797-311799
  FUN_1801b0df0(mode, zones_n, local_850, prompt_st_pt, start_pt, nc_file, outside_in, climb, feed, plunge, abort, err)
              l.311830 / asm 1801d4c35-1801d4c93 (l.288292-288687, 396; clos 190 fn / 11174 PL2)
     FUN_1800ce680, open tmpzone.tmp "rb", FUN_18017fcc0 x n [PL2], FUN_1801b5d80, FUN_1801b5ed0,
     mode 1: FUN_1801b59c0 (tmpdzmap.tmp) + FUN_1801b79b0;  mode 2: FUN_1801b5c50 + FUN_1801b7740;  mode 3: FUN_1801b5ca0 + FUN_1801b7740
     FUN_180186db0 (zone points), [op 0x2b clip: FUN_18011c0d0, FUN_18011c720("tmpmm5.tmp") PL2, FUN_180121670, FUN_180158b70 PL2,
     FUN_1801651f0 PL2, FUN_1801c1550], FUN_18013f380, per zone FUN_1801866a0, FUN_180195eb0 (DAT_1804d7038 == 1), FUN_1801b9da0,
     FUN_180196ec0 (l.272308, 241; clos 2272, no PL2) -> FUN_180140d80 / FUN_180142d30 -> FUN_1801417c0 (cut.tmp / msproj.tmp records),
     FUN_180140820, FUN_18013f310, FUN_1800ce590, thunk_FUN_18011c980
  FUN_180140600 (close cut.tmp, msproj.tmp)  l.311836 (l.209561)
  FUN_18023c390 x3 (clear contexts; FUN_1800ce590 PL2)  l.311837-311839 (l.375607)
  FUN_180260860 (restore the temp.rgn name, local_857 = 1)  l.311843 (l.400649)
  m_fclose tmpdiag; SysFile::Close(DAT_1804e9080)
```

#### Functions

**FUN_1801d3ca0** (1801d3ca0, l.311384-311848, 465 lines; own edge_fit l.311538/311540, pl2StackAlloc l.311547/311553/311731/311750).
Arguments (caller asm 180132d23-180132dc7, callee frame asm 1801d3ce7-1801d3d11; rbp = entry rsp - 0x7d8, stack arg k at
rbp+0x800+8(k-5)): rcx = msurf_opt_cut_order (0x1804d6fa3) -> [rbp-0x7d]; dl = msurf_create_zones (0x1804d6fb6); xmm2 =
msurf_colaps_dist (0x1804d7008) -> xmm7; r9b = msurf_prompt_st_pt (0x18050340a) -> [rbp-0x7e]; arg5 rdi = start point ->
[rbp-0x10]; arg6 (short)DAT_1804d7038; arg7 nc_file -> [rbp-0x18]; arg8 comp_to_tip -> DAT_1804e8ffd (asm 1801d3eaa); arg9 = method
(2) -> r15w; arg10 [rbp+0x828] = msurf_colaps_outside_in (0x1804d6fb7); arg11 [rbp+0x830] = rough_climb (0x1804d848a); arg12/13
feed / plunge; arg14 [rbp+0x848] = msurf_max_short_gap (0x1804d6f90) -> xmm6; arg15 NCI name (unused); arg16 rsi = abort flag; arg17
rdi = &err (names of exported globals from the export table, the export table, ordinals 181/182/186/191/205/207/212/367).
- local_853 = zones && method 2 (l.311468-311469; asm 1801d3dff-1801d3e0e, [rbp-0x7b]) -> FUN_1801b1ad0's p6.
- If msurf_use_short_gap_pct: msurf_max_short_gap = |msurf_slice_dist| x pct (l.311483-311486).
- Kind dispatch on header +8 = 0x11: `(k-0xd)&~3 == 0 && k != 0xf` false (l.311491); `(k-0xf)&~2 == 0` true (l.311510): sets DAT_1804d56f8
  = DAT_1804d57b8 = &DAT_1804e9080, FUN_18018f780(.., 1, .., hdr+0x18); with method 2 and kind-7 trailer >= 0 cVar10 = 1 and the
  node-pool size iVar6 = max(kind-6 + walls, kind-7 + walls + 100) + max(DAT_1804d56c8, DAT_1804d5788) + 100 (l.311514-311530).
  Error -0x13 only for kind 0xf (l.311534). Pool DAT_1804e9320 = min(iVar6, edge_fit(0x68)/4, 0x7fff) (l.311538-311546);
  DAT_1804e9328 = pl2StackAlloc(n x 0x68); DAT_1804e9310 = pl2StackAlloc(DAT_1804e9144 x 4) (l.311547-311558).
- Not taken: kinds 0xd/0xe/0x10 (l.311560-311705: rufpclsc.tmp, pncilseg.tmp, pncilrec.tmp, tmpfswsa.tmp, FUN_1801ce4c0,
  FUN_1801ca7f0, FUN_1801cd460, FUN_180301e00, FUN_180191b50).
- Taken arm l.311742-311790: cVar9 = (method 2 && colaps_dist > tol) (l.311744-311749; asm 1801d475d-1801d4776, r13b);
  DAT_1804e9318 = pl2StackAlloc(DAT_1804d5788 x 4); FUN_180266760 + FUN_180266820("tmpemesh.tmp") (the recut-file name global
  s_temp_rgn_1804d73a0 now names tmpemesh.tmp + rec_suffix, l.403662-403665; FUN_180228850 l.362990 reads it, so every FUN_18025d6d0
  until FUN_180260860 opens tmpemesh.tmp); FUN_18018e630 twice; Close + FUN_18025d6d0(2) = tmpemesh.tmp "rb"; z limits off ->
  LAB_1801d49a6 (l.311706).
- FUN_1801b1ad0 result -0xb -> FUN_1801ada50 maps to -0x15 for op 0x2b (l.285983-286004). *p14 (local_840) = 0 on this path (see
  FUN_1801b1ad0) -> l.311716 `local_840[0] < 1` -> LAB_1801d4b3b: DAT_1804e92f8 = local_830 (the kind-6 line table).
- FUN_1801b0df0 mode (l.311800-311829, asm 1801d4bc9-1801d4c30): opt_cut_order == 0: 3, or 2 when op+0x12c0 (short) == 1;
  opt_cut_order != 0: 1, or 2 when op+0x12c0 == 1 (kind 0x11 reads op+0x12c0; kind 0xe would read op+0x1322).
- Tail (LAB_1801d4c9f l.311835-311847): FUN_180140600, FUN_18023c390 x3, DAT_1804e9798 = DAT_1804e97a8 = 0, FUN_180260860 (local_857
  = 1 since l.311757), m_fclose tmpdiag, Close DAT_1804e9080.

**FUN_18015da80** (l.232514, 98, clos 7 / 272, PL2 via FUN_1800cddd0 mode 2 per 12.21) - reads record 0's trailer into local_7b8
(12.21 RGNR). **FUN_180186bb0** (l.262163-262218) copies context fields into globals: frame DAT_1804e9150/9168/9180, tolerances
9238/9240/9248/9250, tool block 9288/9290/9298, DAT_1804e9148 (+0x98), first line DAT_1804e9230 (+0xa0), step DAT_1804e9228 (+0xa8),
n lines DAT_1804e9144 (+0xb4), **DAT_1804e8ffc = (ctx+0xc0 >= 0)** (l.262195; the trailer's containment int = -1, 12.21 item 4 ->
0), DAT_1804e9140 = DAT_1804d7038 (from FUN_180207f00 l.340747: msurf_find_tei_ctrl = op+0x1268 clamped to 1..3 (l.340042-340044);
3 becomes 2 for ms_tp_type 0x2b: mask 0x48000000a280 has bit 43 = 0x2b, l.340752 -> DAT_1804d7038 is 1 or 2), wall pool DAT_1804e97a4
/ 97a8 / 9798 (l.262211-262216). **FUN_18015de40** (l.232613): out = ctx+2 (short, -> local_84c = zones_n seed), +0x90, +0xb0
(byte), +0xb8 (-> local_830 = kind-6 line table), +0xc0. **FUN_1801b5fb0** (l.291319): max block size over the line chains
(FUN_1801b4e80, reader FUN_1801a6e70), 12.21.

**FUN_18018f780** (18018f780, l.267677-267869, 193; own pl2StackAlloc l.267803/267832/267835/267845-267851). Builds the
two-direction context at p5 = &DAT_1804d5390 (asm 1801d3f17 / 1801b1ef2): dir A at +0x338 (n = DAT_1804e9144, +0x340 first,
+0x348 step, +0x350 last, +0x358/+0x360 = 1.0 signs, +0x370 = p1 line table, +0x3c0 containment, +0x3e0.. walls) from the globals
(l.267703-267752); dir B at +0x3f8 read from the file at p4 = kind-7 trailer (l.267783-267828: frame rows, tolerances, tool block
into DAT_1804e9288, +0x400 first, +0x408 step, +0x3f8 n, +0x430 = pl2StackAlloc'd line table, +0x480 containment int, wall table
+0x4a0/+0x4a8/+0x4b0), **+0x418 = -1.0, +0x420 = 1.0** (l.267825-267826), +0x2f8 = |dir-diagonal|, +0x300 = 3 x that,
DAT_1804d4750 = +0x300, +0x308 = msurf_short_gapchk (l.267772-267780). Already reproduced by the harness (12.21 RGNR).
Context offsets as globals: +0x338 = DAT_1804d56c8, +0x348 = DAT_1804d56d8, +0x370 = DAT_1804d5700, +0x388 = DAT_1804d5718, +0x3f8 =
DAT_1804d5788, +0x408 = DAT_1804d5798, +0x430 = DAT_1804d57c0, +0x448 = DAT_1804d57d8 (0x1804d5390 + offset).

**FUN_18018e630** (18018e630, l.266927-267367, 441; clos 94 / 6581; own pl2StackAlloc l.267131/267136/267147/267153 (two
(DAT_1804e9320+1000) x 0x68 node arrays, (max n + 100) x 0x68, (max n + 100) x 8), edge_return l.267344-267352). The "emesh"
rewrite of one direction of temp.rgn into tmpemesh.tmp. Args (asm 1801d47c0-1801d482a; Ghidra drops p5-p8): p1 = temp.rgn SysFile
(input), p2 = &hdr, p3 = cVar9 (used only for kind 0xf, l.267046), p4 = direction (1 = B / kind 7 first, 0 = A / kind 6), p5 =
create (1 then 0), p6 = finish (0 then 1), p7 = &DAT_1804d5390, p8 = abort.
- Containment flag local_324[4] = (kind 0xf && p3) || (kind 0x11 && DAT_1804e8ffc) = 0 here (l.267046-267052).
- kind 0x11: reader FUN_1801a7a00 (0x18 points: +0 s, +8 z, +0x10 short code, +0x12 byte flag, normal = (0,0,1); l.281505-281513),
  stride 0x18, ctx+0x399 = ctx+0x459 = 2 (l.267066-267069).
- p4 = 1: input table ctx+0x430, n = ctx+0x3f8, output table DAT_1804e9318, frame DAT_1804e9198/91b0/91c8; p4 = 0: ctx+0x370, ctx+0x338,
  DAT_1804e9310, DAT_1804e91e0/91f8/9210 (l.267078-267103) -> FUN_1801900b0 (l.267979: those rows into DAT_1804e9150.. and
  DAT_1804e8ffe).
- Output file: FUN_18025d6d0(p5 == 0, local_2b0, &DAT_1804e9308) (l.267106): p5 = 1 -> mode 0 = create "wb" with a 0x38 header
  (l.399082-399094), then FUN_180229640(-0x38, hdr+8, hdr+4) (l.267121); p5 = 0 -> mode 1 "rb+" (l.399096-399114), seek end,
  DAT_1804e9308 = end (l.267112-267118).
- Per line (l.267176-267316): FUN_1801a1f60(p1, input_table[line], FUN_1801a6f50, reader, 0x18, containment ctx, &DAT_1804e92b8)
  reads the line's block chain into the 0x68 node list; FUN_180184cc0(0, .., DAT_1804e9794 = 0, DAT_1804e97a8 = 0 (l.267165-267166),
  .., psVar19) -> node array (n = sVar4); not contained (l.267205): dir B -> FUN_1801adf60(ctx+0x418, n, arr) multiplies s0 / s1 by
  ctx+0x418 = -1.0 (l.286248-286268); pHVar22 = sign of the first segment with |z1 - z0| > tol, kept across lines (l.267209-267222);
  **DAT_1804d7038 == 1 only:** FUN_1801a9e60(dir, line pos, sign, ctx, ..) (l.267224-267230); then the FUN_1801a8e50 / FUN_1801a9150
  loop (l.267236-267282: FUN_1801a8e50 l.282599 walks the array for s-gaps > p2 and returns 0 / 1 / 2 with end points;
  FUN_1801a9150 l.282782 projects them with FUN_1801a83c0 / FUN_1801a9080 using ctx+0x358 or +0x418 scales - geometry NOT DECODED);
  FUN_180182800 (l.259536: each 0x28-code node -> FUN_180182870); FUN_180182de0(ctx+0x399 = 2, out, &DAT_1804e9308, out_table)
  -> FUN_1801a1a70 (l.259766), the same block writer as stage C (12.20: 0x18 points + 0x18 trailer); FUN_1801a5510.
- After the loop: FUN_1801b4070(p1, hdr+0xc (p4 = 0) or hdr+0x18 (p4 = 1), n, out_table, out, &newhdr+0xc / +0x18, ..)
  (l.267319-267324): copies the input trailer (l.289972-290038) but writes the new line table (l.290008-290009); then p6 = 0:
  FUN_180268b30 (header rewritten, l.267325-267331); p6 = 1: header +0 = -0x38, FUN_18022c850 (appendix, opens tmpmm2.tmp,
  l.365220; l.267332-267338).
- So tmpemesh.tmp has temp.rgn's layout (header kind 0x11, +0x18 dir-B trailer, +0xc dir-A trailer, appendix) - inferred from the
  calls above, not from a dump.

**FUN_1801b1ad0** (1801b1ad0, l.288723-289215, 493; clos 830 / 55343; own edge_return l.289112). The collapse / zone core.
Args (asm 1801d4969-1801d49d6; signature l.288728-288731): p1 tmpemesh SysFile, p2 &hdr, p3 opt_cut_order, p4 prompt_st_pt, p5 start
pt, p6 = local_853, p7 = 2, p8 = 1, p9 = colaps_dist, p10 = DAT_1804e9140 (= DAT_1804d7038), p11 = max_short_gap (xmm6 =
[rbp+0x848]), p12 = &local_84c (zones count, += per pass, l.289079), p13 = &local_850, p14 = &local_840 (tmpsurf offset), p15 abort.
Branch values for this path:
- bVar3 = (p7 == 2 && p9 > tol) = 1 (l.288823-288828; asm 1801b1be6-1801b1c06 [rbp-0x7c]); bVar2 = op 0x2b && op+0x8c > 0 &&
  FUN_180253720("tmptc.tmp") in {1,3} (l.288830-288833, l.390230-390234) - 0 without containment chains (12.1: tmptc removed);
  bVar20 = op 0x28 only (l.288836-288838) = 0; bVar4 = (kind-0xd < 3) || p10 == 1 = (DAT_1804d7038 == 1) for kind 0x11 (l.288841);
  local_76a = (op+0x12e0 byte == 0) (FUN_180250500 l.390252-390255); local_76b = p3 + 1 (used only when !bVar3).
- Block type: FUN_180186360 reads the first non-empty line's first 0x18 trailer with FUN_1801a7a90 (byte +0, asm 1801b1d05) = 2
  (FUN_1801a1a70's +0 = 2, 12.20) -> local_760 = FUN_1801a7880, DAT_1804d5729 = DAT_1804d57e9 = 2 (asm 1801b1d3d-1801b1d6e); kind !=
  0/5 -> readers FUN_1801a6e70 / FUN_1801a6f50, DAT_1804d5728 = DAT_1804d57e8 = 1 (asm 1801b1d7f-1801b1d9c).
- DAT_1804d5640 = p10 (2 only for kind 0xe, asm 1801b1deb-1801b1df9), DAT_1804d5680 = p9, DAT_1804d57b8 = DAT_1804d56f8 = p1,
  DAT_1804d5d60 = hdr+0x1c (l.288899-288907). tmpsurf.tmp opened "wb" (DAT_1803ee548) in DAT_1804e9060 (l.288908-288916).
- FUN_18018f780 again on tmpemesh (dir B from tmpemesh's hdr+0x18; asm 1801b1ee9-1801b1f10); DAT_1804d5668 = 3 x max(|stepA|,
  |stepB|) (asm 1801b1f1d-1801b1f4f); FUN_1801b4e80 sizes dir A.
- FUN_18018a640 args (asm 1801b20c9-1801b2122 dir A, 1801b22bc-1801b2315 dir B): (dir, reader [rbp-0x50] = FUN_1801a6f50, &point
  buffer, r13b = (kind != 0xe) = 1, 2, xmm7 = min(|stepA|, |stepB|, 10 x DAT_1804e9238) (asm 1801b1f92-1801b1fc9), xmm6 = p9
  (bVar3; else FUN_1801ad940 = max |step|) (asm 1801b1fcc-1801b1ff1), DAT_1804e9250, max segments, 0xb0 array from FUN_1801a8340,
  &count, ctx, &err). Arrays: FUN_1801ae960 sizes 3 descriptors (n_lines x 0x20, segs x 0xb0, k x 0x28; l.286773-286782), FUN_1800cdfa0
  mode 2 allocates, FUN_1801a8340 returns the 3 pointers (dir A: DAT_1804d5718 = ctx+0x388, local_700, local_758; dir B:
  DAT_1804d57d8, local_708, local_710). Retried once with the returned count when it exceeds 2 n (l.288948-288955, 289180-289187).
  0 segments in both -> -0xd (l.289191-289193).
- l.288974-288980: bVar3 && local_76a -> DAT_1804d53b4 = FUN_18016a2a0(ctx, 100) (3 descriptor allocations via FUN_1800ce330 +
  calloc 0x1900, l.241085-241108), FUN_180171db0, FUN_1801588f0(1, &DAT_1804e95b0); else DAT_1804d53b4 keeps its previous value
  (its only store is l.288975).
- FUN_1801adf10 size, FUN_180190140, FUN_1801a7fd0 (per-direction work arrays, 7 descriptors, returns pointers through p6..p19,
  l.281849-281931), FUN_1801b88e0 (reads each segment's points from p1 at the segment's +0x28 offset through the reader, l.293248-293259,
  into arrays given as args), FUN_1801b88c0 / FUN_1801b86c0 (0xb0 node pool, edge_alloc) / FUN_1801b84f0 (dir B, FUN_1801b8410 per
  segment).
- !bVar3 block l.289005-289029 (FUN_18015a2b0, FUN_1801bc5c0, FUN_1801bbc90, FUN_1801babd0 / FUN_1801ba0c0 / FUN_1801ba5e0, local_770,
  tmpzune.tmp) - NOT TAKEN. Hence local_770 = 0 and tmpsurf.tmp gets no table (l.289103-289110), *p14 = 0.
- bVar3 -> the three zone files are opened "wb+" (DAT_1803f3ec4) whatever p6 is (l.289030, 289122-289166): tmpzone.tmp -> local_698
  (rbp+0x60), tmpdzmap.tmp -> local_658 (rbp+0xa0), tmpdzdat.tmp -> local_678 (rbp+0x80) (asm 1801b1b75-1801b1b9c order); local_638
  (rbp+0xc0) is constructed but never opened.
- LAB_1801b2a74: bVar3 and kind 0x11 ((k-0xe)&~3 == 0, k != 0x10) -> DAT_1804d569a = 1, FUN_18015d0f0(ctx, abort), DAT_1804d56c0 =
  max(DAT_1804d56c0, FUN_18015c8f0(ctx)) (l.289035-289043; asm 1801b2a82-1801b2ae4); FUN_1801872f0 needs bVar20 (op 0x28) - not taken.
- DAT_1804e8ffc == 0 -> (bVar3 && DAT_1804d53b4): [bVar4: FUN_18015a2b0] then FUN_180186ff0(DAT_1804e9240, ctx, abort) (l.289049-289056;
  asm 1801b2b66-1801b2bb1). Containment arm FUN_180179cc0 / FUN_1801afa70 (l.289059-289064) - NOT TAKEN.
- bVar2 -> FUN_18018d0a0 (tmptc) - not taken; kind 0xe -> FUN_18015ada0 - not taken (l.289068-289071).
- Pass loop (l.289073-289096; asm 1801b2c12-1801b2dd3): pass = 1, 2, ..; DAT_1804e930c = 0 on pass 1; FUN_180189a90 -> new zones
  sVar10, *p12 += sVar10; exit when sVar10 < 1 or colaps_dist <= |DAT_1804d56d8| (dir-A step; no store to ctx+0x348 /
  DAT_1804d56d8 exists in FUN_1801b1ad0's closure by grep, so this is a fixed test: one pass only when the step is >= colaps_dist);
  -0xd with zones already found is cleared (l.289081-289084); total 0 -> -0xd (l.289086). Each further pass: FUN_1801c16d0, then
  (new > 1) FUN_1801597b0, then FUN_180192330.
- After: FUN_1801b87e0, descriptor frees, tmpsurf table only if local_770 > 0, FUN_18016da90 + FUN_180182a20 (p8), closes (l.289098-289212).

**FUN_18018a640** (18018a640, l.264180-264516, 338; clos 13 / 745; no PL2). Segments of one direction: per line (DAT_1804e92d8),
header entry {+0 first 0xb0 record ptr, +8 last ptr, +0x10 short count, +0x12 short count, +0x14 byte dir, +0x18 double line
position} in ctx+0x388 / +0x448 (l.264271-264276), points read block by block (seek to the trailer, reader p2 l.264283-264284,
FUN_1801abc40 reads the n points l.264303); points whose code passes FUN_180195e70(p5 = 2, code mod 0x27) open / extend a 0xb0 segment
record in p10 (+0 next, +8 prev, +0x28/+0x2c point indices, +0x70.. start s,z, +0x80.. end s,z, +0xa8 ..; l.264369-264417); a new
point within p7 (= colaps_dist here) of the previous segment's end continues it (l.264338-264373); code 0x25 counted in *p11
(l.264319-264320); afterwards segments shorter than p6 in s, z and length are dropped by FUN_1801a9c80 (l.264492-264513). Returns the
segment count. Takes all arrays as arguments (p3, p10, p11, ctx p12) -> DLL-callable with harness arrays.

**FUN_180189a90** (180189a90, l.263834-263888, 55; clos 388 / 27828 PL2). Calls FUN_180187850 with its own args plus DAT_1804e9240;
on -0xd (first time only): DAT_1804e930c = *p13, FUN_1801bc5c0 for the directions, FUN_18015d020(ctx, abort), retry with p9 = 0x32
(l.263851-263885). Args as passed (asm 1801b2c3d-1801b2caa): tmpzone, tmpdzdat, sf4 (unopened), &local_718, p5 = pass, p6 = p8 = 1,
p7 = 0, p8 = 2 on pass 1 when p6 (zones) of FUN_1801b1ad0 is set, else (msurf_disp_inters != 0) (asm 1801b2c1f-1801b2c39;
msurf_disp_inters = 0x1804d6f80, export ordinal 191), p9 = 10, DAT_1804e9240, ctx, rbp+0x210 (local_4e8 descriptors), &local_740
(zone start offset), &err, abort.

**FUN_180187850** (180187850, l.262543-263832, 1290; clos 324 / 23480 PL2: FUN_1801b3000 l.262895, FUN_1800ce4b0 l.262913,
FUN_1801bff10 -> FUN_180176810 -> FUN_180176220 -> FUN_18016a3e0 / FUN_18017ad00 / FUN_180174c70...). Zone builder over the two
directions' segment headers (ctx+0x388 / +0x448, counts +0x338 / +0x3f8, l.262822-262842; p7 must be 0 or 1, else -0x10 l.262831-262835)
using a `zonbuf_ctrl` object local_9a18 (vftable, l.262798): +8 ctx, +0x18 zone number (local_9a00 = previous + 1, l.262946), +0x1c
FUN_1801ad550 result (l.263212), +0x1e IsOpen(tmpzone), +0x1f DAT_1804e8ffd, +0x20 p8, +0x28 p10, +0x30 tol, +0x38 DAT_1804e9250,
+0x70 tmpzone SysFile*, +0x78 its start position, +0x90 tmpdzdat SysFile*, +0x98 its start position (l.262944-262961). Seeks tmpzone
to DAT_1804e930c, *p13 = that (l.262805-262813). The third SysFile (p3) is not open on this path, so the FUN_180159fa0 reload
(l.262929-262940) is skipped. On exit DAT_1804e930c = tmpzone position (l.262903). Geometry / zone logic NOT DECODED.

**FUN_1801c16d0** (1801c16d0, l.299584-299774, 191; clos 8 / 441; PL2 via FUN_1800ce260 -> FUN_1800cdfa0). Args (asm 1801b2d0d-
1801b2d44): tmpdzdat, 0, pass, total - new, new, p13 (&local_850), tmpdzmap, &err. Three short arrays of new+10 (l.299639-299646);
FUN_1801b76d0 / FUN_1801b4f80 read tmpdzdat groups (0xc header, l.290612) of 8-byte entries; each entry is dereferenced as a pointer
(`*(short *)(local_e0 + 0x30)` / `+0x32`, l.299711-299716) - i.e. tmpdzdat stores in-process addresses; writes to tmpdzmap per group
{short pass, short n, n shorts zone number} (l.299736-299761). Returns the group count (l.299763). Meaning NOT DECODED.

**FUN_1801597b0** (1801597b0, l.229305-229563, 259; clos 27 / 1953; no PL2, calloc only l.229383/229390). Args (asm 1801b2d5d-
1801b2d91): tmpzone, zone offset (local_740), pass, new zones, p5 = xmm9 = 0.8 x colaps_dist (asm 1801b1fd2-1801b1fda, 0x1803f4080
= 0.8), 10, rbp+0x40, rbp+0x540. Walks the pass's zones (0xc header: int n >= 2, short +4 == pass, l.229421-229422; n x 0x30 records,
3 doubles via FUN_1800062d0 l.229453-229459); zones with n < 0x1e tested by FUN_18014d1b0(.., p5) and their header rewritten
(l.229494-229499). DLL-callable on a harness tmpzone file.

**FUN_180192330** (180192330, l.269237-269386, 150; clos 181 / 11990 PL2 via FUN_180192720). Args (asm 1801b2da3-1801b2dca): p1 = 1,
tmpzone, zone offset, pass, new zones, colaps_dist, ctx. Per zone: 0xc header (n >= 2, +4 == pass, l.269326-269327), consecutive 0x30
records; FUN_180271c10(prev+8, cur+8, FUN_180271e20(colaps)) false -> FUN_180192720(p1, prev, cur) (l.269341-269360). Not DLL-callable
(FUN_180192720 -> FUN_180178e90 -> FUN_180173530 -> FUN_1801588f0 -> FUN_1800cdfa0).

**FUN_18015d0f0** (l.232037-232085, 49; clos 114 / 7272 PL2). Two-direction iteration: FUN_180150840(dir) + FUN_18014d5c0(dir)
for each direction with lines, then FUN_180149a20 until both FUN_18014d5c0 return 0, then FUN_180146e60(0) and (1). Roles NOT
DECODED. FUN_180149a20 (clos 4678) and FUN_18014d5c0 (739) have no PL2; FUN_180150840 (-> FUN_1801b86c0) and FUN_180146e60
(-> FUN_180169990 -> FUN_1800cdfa0) do.

**FUN_180186ff0** (l.262313-262387, 75; clos 440 / 30768 PL2). Only if DAT_1804d53b4. Opens tmpprojz.tmp and tmpdztmp.tmp "wb+"
(l.262347-262364), FUN_180187850(tmpprojz, tmpdztmp, sf3, ..) (l.262369; Ghidra shows 4 args), then FUN_180162dd0(ctx, tmpprojz, ..)
unless aborted (l.262372). Both files closed, not deleted here.

**FUN_18015c8f0** (l.231676, 40; clos 82, no PL2): a double from ctx via FUN_18014b350; max-ed into DAT_1804d56c0. Meaning NOT DECODED.

**FUN_180141e20** (180141e20, l.210368-210515, 148; clos 5 / 256; no PL2). p1 = 1 (asm 1801d4b4b): DAT_1804e8ed0 = 3000.0 bits
0x40a77..., DAT_1804e8ec8 = 3000 (l.210399-210402); cut.tmp "wb+" (DAT_1803f3ec4) -> DAT_1804e8d98, msproj.tmp "wb" -> DAT_1804e8db8
(l.210405-210417); cut.tmp header = double p2 (DAT_1804e9148) + three 0x18 rows (DAT_1804e9150, 9168, 9180) = 0x50 bytes
(l.210422-210426), copied to DAT_1804e8e20.. (l.210427-210437); DAT_1804e8ea0 = 0 (msproj record count); FUN_180227b60 (tmpmm2.tmp
surface count, l.362587) and DAT_1804e8ec8 = max over get_mmsurf(i) of mach_model_surf +0x30 (l.210446-210465), then + op+0x2f0 + stock
(or msurf_chk_offset when DAT_1805034a1 and larger) + max of the enabled op+0x108 / 0x118 / 0x128 (l.210467-210487). p1 = 2 reopens
both "rb" (l.210491-210503; used by the NCI writer, 12.4).

**FUN_1801b0df0** (1801b0df0, l.288292-288687, 396; clos 190 / 11174 PL2). Args (asm 1801d4c35-1801d4c93): p1 mode (1/2/3 above),
p2 = local_84c (zones), p3 = local_850, p4 prompt_st_pt, p5 start pt, p6 nc_file, p7 = [rbp+0x828] (msurf_colaps_outside_in), p8 =
[rbp+0x830] (rough_climb), p9 / p10 feed / plunge, p11 abort, p12 err.
- No zones -> FUN_1801ada50 -> -0x15 (l.288434-288437). Start point: p4 ? p5 rotated by rot_view into the frame : -100000 x 3
  (0xc0f869f000000000, l.288421-288432). Opens tmpzone.tmp "rb" (l.288439-288443). 13 descriptors local_7a8 via FUN_18017fcc0 (PL2,
  l.288463-288510; shrinks by 0x2800 on failure). Zone table 0x50 entries (from FUN_1801b5d80 / FUN_1801b5ed0): +0 int file offset,
  +4 int point count, +0xc int, +0x10 p3d, +0x30/+0x38/+0x40 array pointers, +0x48 byte valid (l.288606-288622; FUN_180186db0 l.262250-262289
  fills +0x30.. from the 0x30 records' +8 p3d).
- Order: mode 1 FUN_1801b59c0 (reads tmpdzmap.tmp, l.291089) + FUN_1801b79b0; mode 2 FUN_1801b5c50 + FUN_1801b7740(start, ..);
  mode 3 FUN_1801b5ca0 + FUN_1801b7740 (l.288512-288533). Iteration forward when p7 != 0, backward from the last otherwise
  (l.288579-288585).
- **op 0x2b clip** (l.288537-288563): only when op+0x12e8 (double) > 1e-6 and op+0x12f0 > tol and op[0x12e7] != 0: FUN_18011c0d0,
  FUN_18011c720("tmpmm5.tmp") (opened through m_fopen_hl "rb" in FUN_18011c1c0 l.187144), FUN_180121670, FUN_180158b70; per zone
  FUN_1801651f0 (PL2) + FUN_1801c1550. Meaning of the op fields for 0x2b NOT DECODED.
- Per zone (l.288588-288655): FUN_1801866a0 loads its points; DAT_1804d7038 == 1 -> FUN_180195eb0(5 x |DAT_1804e9228|, ..) may skip;
  FUN_1801b9da0, FUN_1801c1550 (nearest point, p7 == 0 or clip), **FUN_180196ec0** (l.272308, clos 2272, no PL2) -> FUN_180140d80(..,
  DAT_1804e9248, .., msurf_max_short_gap, msurf_short_gap, ..) (l.272475) and FUN_180142d30 -> FUN_1801417c0 (the cut.tmp /
  msproj.tmp records). Then FUN_180140820(.., p9, p12) and FUN_18013f310 (-> FUN_18012f560, rewrites a cut.tmp record l.200099-200120).

**FUN_180140600** (l.209561): closes DAT_1804e8d98 (cut.tmp) and DAT_1804e8db8 (msproj.tmp). **FUN_18023c390** (l.375607-375644): zeroes
a direction context and frees its +0xf8 descriptors (FUN_1800ce590(0, 6, ..), PL2). **FUN_180260860** (l.400649-400659): restores
s_temp_rgn_1804d73a0 from the copy saved by FUN_180266760 (l.403621).

Not taken here: **FUN_18018f0d0** (l.267369, 307, clos 4765, own pl2StackAlloc; tmpzmesh.tmp; only msurf_use_zlimits l.311772),
**FUN_1801962a0** (l.271868, clos 4473, no PL2; only colaps_dist <= tol, l.311797), FUN_1801872f0 (op 0x28), FUN_180179cc0 /
FUN_1801afa70 (containment), FUN_18018d0a0 (tmptc), FUN_1801babd0 / FUN_1801ba0c0 / FUN_1801ba5e0 (!bVar3).

#### Files

| file | opened by | mode | SysFile | records (as far as shown) |
|---|---|---|---|---|
| tmpdiag.tmp | FUN_1801d3ca0 l.311457 | m_fopen_hl "w" | DAT_1804e8fb0 (FILE*) | text diagnostics (fprintf, e.g. l.267113) |
| temp.rgn | FUN_18025d6d0(2) l.311471 | "rb" | DAT_1804e9080 | 12.21 (input) |
| tmpemesh.tmp (+rec_suffix) | FUN_18018e630 -> FUN_18025d6d0(0 / 1) l.267106 | "wb" then "rb+" | local_2b0 | temp.rgn layout: 0x38 header (kind 0x11, +0x18 dir-B trailer, +0xc dir-A trailer), per line blocks of 0x18 points + 0x18 trailer (FUN_1801a1a70), trailers copied by FUN_1801b4070 with the new line table (l.290008-290009), appendix by FUN_18022c850 |
| tmpemesh.tmp | FUN_1801d3ca0 l.311766 | "rb" | DAT_1804e9080 | read by FUN_1801b1ad0 |
| tmpzmesh.tmp | (z limits only) l.311774 | - | - | NOT TAKEN |
| tmpsurf.tmp | FUN_1801b1ad0 l.288908 | "wb" | DAT_1804e9060 | int n + n x int line table, only when local_770 > 0 (l.289103-289110): empty on this path; FUN_1801d3ca0 reads it only when *p14 > 0 (l.311716-311739) |
| tmpzone.tmp | FUN_1801b1ad0 l.289122 | "wb+" | local_698 (zonbuf +0x70) | per zone: 0xc header {+0 int n (0x30 records), +4 short (== pass in FUN_1801597b0 l.229422 / FUN_180192330 l.269327; zonbuf +0x1a, store NOT DECODED), +6 short zonbuf +0x1c, +0xa byte 1} (FUN_1801a95e0 l.283014-283021, FUN_1801c9be0 l.305440-305446), then n x 0x30 {+8 p3d (FUN_180186db0 l.262282-262284), +0x20 8 bytes, +0x2a short} (FUN_1801c9be0 l.305462, FUN_1801c7060 l.303462) |
| tmpdzdat.tmp | FUN_1801b1ad0 l.289150 | "wb+" | local_678 (zonbuf +0x90) | 0xc header {+0 int count, +4 short, +6 short, +0xa byte 1} (l.283028-283036, l.303446-303448) then count x 8 bytes = the +0x20 qword of 0x30 records with +0x2a < 2 (l.303455-303464); read back as pointers (l.299711-299716) |
| tmpdzmap.tmp | FUN_1801b1ad0 l.289136 | "wb+" | local_658 | written by FUN_1801c16d0: {short pass, short n, n x short zone} (l.299737-299754); read by FUN_1801b59c0 (l.291089, mode 1) |
| tmpprojz.tmp, tmpdztmp.tmp | FUN_180186ff0 l.262347 / 262358 | "wb+" | locals | only if DAT_1804d53b4 (op+0x12e0 == 0); layout NOT DECODED |
| tmpzone.tmp | FUN_1801b0df0 l.288439 | "rb" | local_2610 | as above |
| tmptc.tmp | FUN_1802504c0 / FUN_180253720 l.390231 | (count) | - | containment chains; absent here |
| tmpmm2.tmp | FUN_180227b60 l.362587; FUN_18022c850 l.365220 | - | - | stage A |
| tmpmm5.tmp | FUN_1801b0df0 l.288548 -> FUN_18011c720 | m_fopen_hl "rb" (l.187144) | FILE* | only the op 0x2b clip condition; layout NOT DECODED |
| cut.tmp | FUN_180141e20 l.210405 | "wb+" | DAT_1804e8d98 | header 0x50 {double DAT_1804e9148, 3 x 0x18 frame rows}; moves 0x48 {+0 p3d from, +0x18 p3d to, +0x30 double, +0x38 int msproj offset or -1, +0x3c short, +0x3e short, +0x40 short; +0x42..+0x47 unwritten} (FUN_1801417c0 l.210116-210168); 4-byte ints written by FUN_18013f470 / FUN_180140d80 / FUN_180142420 / FUN_180142ee0 / FUN_180143630 (l.209073, 209871-209957, 210711-210787, 210903-211062, 211155) - meaning NOT DECODED |
| msproj.tmp | FUN_180141e20 l.210413 | "wb" | DAT_1804e8db8 | 0x38 {+0 p3d, +0x18 p3d, +0x30 int -1, +0x34 byte (1 for kind 3, 3 for kind 5, else 0), +0x35 byte 0} per non-retract move (l.210169-210188); kinds 2/3/5 with both z >= DAT_1804e8ec8 - tol become kind 1 = no msproj record (l.210141-210148); count DAT_1804e8ea0 |

#### Port stages proposed

Harness rule: the DLL may run a function only if nothing it runs on this path calls pl2StackAlloc / pl2MemBufRemainFit /
edge_fit (or, by N13-31's rule, pl2MemBufRemainAdjust / edge_alloc / edge_return through FUN_1800cdfa0 / FUN_1800ce590).
1. **D1 FUN_18018e630 (tmpemesh)**: port the whole function; compare files. Its own allocations block running it in the DLL. Pieces
   DLL-callable with harness arrays: FUN_1801a1f60 (l.277861, clos 545, no PL2; reads into the 0x68 pool DAT_1804e9328 - global,
   harness-set as in 12.20), FUN_1801adf60 (array arg), FUN_1801a8e50 (array arg), FUN_1801a9150 (clos 1409, no PL2; arrays p5/p9/p10),
   FUN_1801a9e60 (clos 1251, no PL2; only find_tei_ctrl = 1), FUN_180182800, FUN_180182de0 / FUN_1801a1a70 (already checked, 12.20),
   FUN_1801b4070 (file in, file out, table arg), FUN_180268b30, FUN_18022c850 (12.21 APX). FUN_180184cc0 reaches FUN_1800cdfa0 via
   FUN_180158460 (path l.260939 -> ..): compare by output only (or reuse 12.20's handling). Check: tmpemesh.tmp byte for byte (except
   stack bytes noted in 12.21).
2. **D2 FUN_18018a640** (no PL2; all arrays are args: p3 point buffer (FUN_1801ad6c0 calloc), p10 0xb0 array, p11, ctx p12 = harness
   copy of DAT_1804d5390 filled as FUN_18018f780 does (12.21 RGNR repeats its reads)). Compare the 0x20 line headers and 0xb0
   segment records for both directions on the DLL's tmpemesh.
3. **D3 FUN_1801b88e0** (no PL2, arrays as args) after FUN_1801a7fd0's array shapes are ported (FUN_1801a7fd0 itself allocates).
4. **D4 FUN_18015d0f0 pieces**: FUN_18014d5c0, FUN_180149a20 (no PL2) callable; FUN_180150840 / FUN_180146e60 not (FUN_1801b86c0,
   FUN_180169990). Needs the 0xb0 node pool (DAT_1804e97b8 list) harness-filled.
5. **D5 zone pass** FUN_180189a90 / FUN_180187850: not callable (FUN_1801b3000, FUN_1800ce4b0, FUN_1801bff10 chain). Port, compare
   tmpzone.tmp / tmpdzdat.tmp (tmpdzdat holds pointers: compare structure, not bytes). Sub-pieces to map next: FUN_180182b70,
   FUN_18018b180, FUN_1801bca00, FUN_1801bff10, FUN_180195fc0.
6. **D6 FUN_1801597b0** (calloc only) - DLL-callable on the port's tmpzone.tmp; **FUN_1801c16d0** not (FUN_1800ce260 -> FUN_1800cdfa0):
   compare tmpdzmap.tmp; **FUN_180192330** not (FUN_180192720 chain): compare tmpzone after the pass.
7. **E1 FUN_180141e20** (no PL2) DLL-callable: cut.tmp header, DAT_1804e8ec8.
8. **E2 FUN_1801b0df0**: not callable (FUN_18017fcc0); DLL-callable pieces: FUN_180186db0 (arrays as args), FUN_1801b5d80 / 5ed0 /
   59c0 / 5c50 / 5ca0 / FUN_1801b79b0 / FUN_1801b7740 (no PL2 flagged), FUN_1801c1550, FUN_1801b9da0, FUN_180196ec0 (no PL2; writes
   cut.tmp / msproj.tmp through the global SysFiles; inputs param_2/param_3/param_4 arrays) - compare cut.tmp / msproj.tmp records.
   The tmpmm5 clip (FUN_18011c720, FUN_1801651f0: PL2) only when op+0x12e8/0x12f0/0x12e7 enable it.

#### NOT DECODED / open questions
- tmpzone header +4 short: FUN_1801597b0 / FUN_180192330 require it == pass, but in FUN_180187850 it is copied from zonbuf +0x1a,
  whose store is not visible in the decompile (local_9a00 is +0x18) - needs the asm of FUN_180187850.
- The zone geometry: FUN_180187850 (1290 lines) and its large callees, FUN_18015d0f0's four callees, FUN_180186ff0 / FUN_180162dd0,
  FUN_180192720, FUN_18014d1b0, FUN_1801a9150 / FUN_1801a83c0 / FUN_1801a9080, FUN_1801a9e60 - roles above are structural only.
- DAT_1804d53b4 is stored only at l.288975 (when op+0x12e0 == 0); with op+0x12e0 != 0 it keeps an earlier value - its initial value
  (.data/.bss) not checked.
- Meaning for op 0x2b of op+0x12c0 (order mode), op+0x12e0 (byte, FUN_180250500), op+0x12e7 / 0x12e8 / 0x12f0 (tmpmm5 clip),
  op+0x1268 (find_tei_ctrl -> DAT_1804d7038 1 or 2): field semantics not taken from the dialog here.
- The pass loop's exit uses |DAT_1804d56d8| (dir-A step); no writer of ctx+0x348 / DAT_1804d56d8 found in the closure by text search
  (aliases through other pointer names were not traced in asm).
- tmpdzdat stores 8-byte values later dereferenced as pointers (l.299711-299716): confirm in asm before porting; the port must
  model them as indices.
- FUN_180186360's reader FUN_1801a7a90 returns byte +0 of the first trailer; = 2 is inferred from 12.20 (FUN_1801a1a70), not from
  a dump of tmpemesh.
- cut.tmp's int markers (FUN_18013f470, FUN_180140d80, FUN_180142420, FUN_180142ee0, FUN_180143630) and the 0x48 record's three
  shorts' meaning; msproj.tmp +0x36..+0x37.
- FUN_1801ad6c0 point-buffer size for DAT_1804d5729 = 2: 500 x 0x18 (l.285810), regrown by FUN_18018a640 (l.264294-264296).

### 12.24 Where this stopped (2026-11-14, session 11)
Done and DLL-identical: the temp.rgn frame and the whole temp.rgn of op 2 (12.21), lines with walls (12.22). Containment FUN_180192af0 is not
on op 43's path (DAT_1804e8ffc = FUN_180133f10's swap = 0, and FUN_1801d3ca0 reads it back as 0 from the trailer's -1, l.262195): not
ported. Stage D is mapped (12.23); corrections to 12.4: FUN_1801b1ad0 reads tmpemesh.tmp (FUN_180266820("tmpemesh.tmp") renames the recut
file before FUN_18018e630, l.311758), FUN_18018e630 takes 8 arguments (asm 1801d47c0..1801d482a), FUN_1801b1ad0 15 (asm 1801d4969..
1801d49d6). Next, in order (12.23 "Port stages proposed"):
1. D1 FUN_18018e630 (tmpemesh.tmp): it allocates itself (pl2StackAlloc x 4), so port it whole and check its callees in the DLL with
   harness arrays: FUN_1801a1f60 (the block chain into the N13-31 store, clos 545), FUN_1801adf60, FUN_1801a8e50 / FUN_1801a9150 (clos
   1409, geometry NOT DECODED yet), FUN_1801a9e60 (only when DAT_1804d7038 == 1, i.e. op+0x1268 = 1), FUN_180182800, FUN_1801b4070, then
   tmpemesh.tmp whole against FUN_18022c850 (as APX) and FUN_1801b5fb0 (as RGNR). FUN_180184cc0 runs there with no walls (DAT_1804e9794 = 0,
   l.267165), so the existing no-wall check applies.
2. D2 FUN_18018a640 (DLL-callable: all arrays are arguments), D3 FUN_1801b88e0, D4 FUN_18015d0f0's callees FUN_18014d5c0 / FUN_180149a20.
3. D5 the zone pass FUN_180189a90 -> FUN_180187850 (1290 lines, its callees ~23k lines; not DLL-callable whole), D6 FUN_1801c16d0 /
   FUN_1801597b0 / FUN_180192330; then E (FUN_180141e20, FUN_1801b0df0 -> FUN_180196ec0) and F (links, NCI).
Op 43 stays greyed (N13-19 rule) until the whole op generates decoded, links included.


### 12.25 Stage D1: FUN_18018e630, temp.rgn -> tmpemesh.tmp (2026-11-14, session 12) - PORTED, = DLL
Sources: decomp/mcmsurf_all.c (l.N), objdump of mcmsurf.dll (asm). The decompile drops most callee arguments (FUN_1801a8e50's gap = xmm7 =
FUN_1801ad940, FUN_1801a83c0's 7 arguments, FUN_1801a8ab0's 13) and garbles every 3-vector index ("bVar == true -> +2": the asm indexes by
the 0 / 1 value), so the whole path was read from the asm. Port: viewer/cam3d_cscallop_emesh.c (cs_em_ctx, cs_em_dir, cs_em_pool,
cs_em_tei), driver `viewer/cfb_port --emesh OUT RGN APX6 CHORD SYSTOL CDEV`; harness tools/peloader/mcmsurf_lmm.c mode **EMESH** (S1 setup +
"EMESH temprgn" + APX's TOLS line); scripts tools/tests/emesh_cmp.sh WORK RGN CHORD SYSTOL CDEV OUT, tools/tests/em_synth.py SEED REF OUT
(synthetic temp.rgn files: test inputs only).

**Setup (FUN_1801d3ca0 l.311471-311762, kind 0x11, method 2).** The kind-6 record (header +0xc) gives direction A and, through FUN_180186bb0,
DAT_1804e9238 / 9240 (as stored: the chord tol / 5) / 9248 / 9250 (9258 keeps FUN_1801cfb60's kind-6 value; not read on this path); the
kind-7 record (header +0x18) direction B (FUN_18018f780's reads l.267768-267850: +0x400 first, +0x408 step, +0x3f8 n, +0x430 table; +0x418 =
-1.0, +0x420 = 1.0; A: +0x358 / +0x360 = 1.0; last = first + step added n - 1 times). DAT_1804d7038 = FUN_180207f00 (l.340743) of
msurf_find_tei_ctrl (op +0x1268, 3 when outside 1..3, l.340042-340044): 3 -> 2 for op type 0x2b (mask 0x48000000a280); **CURVE 5 AXIS op 2
has +0x1268 = 3 -> DAT_1804d7038 = 2**, so FUN_1801a9e60 is not called (DAT_1804d7038 = 1 is refused: NOT PORTED). The store size
DAT_1804e9320 (l.311487-311546) = FUN_1801b5fb0(kind 6) + DAT_1804e97a4 (a4 + 5 when > 0), raised to FUN_1801b5fb0(kind 7) + ctx+0x4a0 + 100
when that is larger, + max(nA, nB) + 100, min 0x7fff; edge_fit(0x68) / 4 is taken as not binding (N13-28). Op 2: 129 + 0, + 120 + 100 = 349.
FUN_1801b5fb0 = per line the sum of its blocks' counts along the chain, the largest + 10 (asm 1801b4e80).

**FUN_18018e630(file, &hdr, cVar9, p4, p5, p6, &DAT_1804d5390, abort)** (asm 18018e630..18018f0a0), p4 = p5 = 1, p6 = 0 for direction B
(creates tmpemesh.tmp), then 0, 0, 1 for A (appends). Header kind 0x11: +0x399 = +0x459 = 2 (stride 0x18, readers FUN_1801a7a00 /
FUN_1801a7bf0). Output header: the caller's block local_270 (asm 18018e6e9..18018e715: +0..+0xb 0, +0xc..+0x27 -1, +0x28..+0x30 0;
+0x31..+0x37 the DLL's stack), FUN_18025d6d0(0) writes FUN_180229640(-0x38, -1, -1) of it, then FUN_180229640(-0x38, hdr +8, hdr +4);
direction A: FUN_18025d6d0(1) reads it back, DAT_1804e9308 = the end. Arrays: two of min(DAT_1804e9320 + 1000, 0x7fff) 0x68 nodes (the
line's and FUN_1801a9150's work array). Per line (DAT_1804e92d8; the output table entry = -1):
1. **FUN_1801a1f60**(file, table[line], FUN_1801a6f50, FUN_1801a7a00, 0x18, ctx, &DAT_1804e92b8) (l.277861): the block chain from the
   table's block back (trailer {+2 count, +4 previous, +8 position -> DAT_1804e92b8}); a block's first point starts a segment with its
   code, the next point ends it (end flag = its flag) and FUN_180182e70 stores it when the start code < 0x25; a point with code > 0x26
   starts the next segment with code - 0x27; normals (0, 0, 1). Its return (store full) is not checked by FUN_18018e630.
2. FUN_180184cc0(0, {0, 1, ..}, 0, NULL, cap, arr) - the stage C function without walls (DAT_1804e9794 = DAT_1804e97a8 = 0, asm 18018eaac).
3. n > 0, not contained: direction B: FUN_1801adf60(ctx+0x418, n, arr): s0 / s1 times -1. The sign (r15w, kept for all lines of the call,
   0 at its start): of the first node with |s0 - s1| > tol, +1 when s1 - s0 > 0 else -1 (asm 18018ec60..18018eccd).
4. **FUN_1801a8e50**(d, gap, n, arr, DAT_1804e92b8, &ctr, out) (asm 1801a8e50..1801a9073; d = 1 for B, 0 for A, e = (d == 0), gap =
   FUN_1801ad940 = max(|stepA|, |stepB|)): one end or gap per call - ctr 0: P = {P[d] = pos, P[e] = s0, z0} of node 0, returns 0; next, the
   first node k >= ctr with |s0(k) - s1(k-1)| > gap: P = node k-1's end, Q = node k's start, returns 2; at ctr == n: Q = the last end,
   returns 1; then -1. ctr += 1 each call (past the node found).
5. per result (asm 18018ed60..18018eea6; a = 0 for B, 1 for A): 0 -> FUN_1801a9150(a, -sgn, P, 0, ..); 1 -> (a, sgn, Q, 0, ..); 2 -> (a, sgn,
   P, 1, Q, ..) and, unless it returns 2, (a, -sgn, Q, 1, the first call's end, ..).
6. **FUN_1801a9150**(a, sgn, P, lim, Q, ctx, cap, work, &cnt, out) (asm 1801a9150..1801a9451): from P line by line of the OTHER direction,
   read back from temp.rgn: **FUN_1801a83c0** (asm 1801a83c0..1801a86c9) - FUN_18015f990 (asm 18015f990: the A lines i0 / i1 around P[0],
   the B lines j0 / j1 around P[1] by floor((x - first) / step), tol margins; 0 outside) - a = 0: the A line i1 (sgn == 1) or i0, compared at
   P[1]; a = 1: the B line j1 / j0 (its s times -1), compared at P[0]; **FUN_1801a8ab0** (asm 1801a8ab0..1801a8e4a) reads that line straight
   into the work array (segments as FUN_1801a1f60 makes them, plus a bridging node before a segment whose start is 9250 < |ds| <
   FUN_1801ad940 from the previous end; a full array -> -4 -> no step), its position into t; the node holding the coordinate within [min,
   max] of the line by **FUN_180158fb0** (asm 180158fb0..1801591c6: bisection in at most n + 10 steps, a node shorter than 9250 replaced by
   the nearest longer one holding it, below then above); **FUN_1801a9080** (asm 1801a9080..1801a9140): out[a] = t, out[e] = P[e], z
   interpolated in the node; out[a] +- tol by sgn; with lim, past Q (sgn 1: out[a] > Q[a]; sgn -1: Q[a] > out[a]) out = Q and return 2;
   **FUN_1801aa4a0**(a == 0, 1, cur, (0,0,1), out, (0,0,1), {1 / +0x418 or 1 / +0x358}) (asm 1801aa4a0..1801aa5d5): the step stored by
   FUN_180182e70 as a code-1 segment in the line's own s (index (a != 0); times -1 for B lines), ends ordered by s, flags 0. Returns 2,
   1 (no lim, at least one step) or 0.
7. **FUN_180182800** / FUN_180182870 (l.259536-259620): code-0x28 nodes recoded from their neighbours; FUN_180182de0(2, ..) -> FUN_1801a1a70
   (cs_rgn_block, 12.20) at DAT_1804e9308 with the line position read in step 1; FUN_1801a5510.
After the lines: **FUN_1801b4070**(file, hdr +0x18 / +0xc, n, table, out, &newhdr +0x18 / +0xc, &err) (l.289934): the input record's trailer
copied to the end with the new table (also DAT_1804e9288 <- its tool block); Close; B: newhdr +0 = 0, FUN_180268b30; A: +0 = -0x38
(asm 18018efd8), FUN_18022c850 (the appendix, cs_scan_appendix). tmpemesh.tmp has temp.rgn's layout (confirmed; 12.23 had inferred it).

**Oracle (2026-11-14, session 12)** (emesh_cmp.sh: the per-line trace EMCTX / EL (list after FUN_1801a1f60) / EA (array) / EX (every
FUN_1801a9150: kind, return, steps, end) / EC (list after FUN_180182800) / EMTAB line for line, and the file after B, before FUN_18022c850 and
whole, byte for byte; masked: header +0x31..+0x37 and the blocks' stack bytes of 12.20):
- CURVE 5 AXIS op 2: trace identical (1197 lines, 472 FUN_1801a9150 calls, 0 steps), tmpemesh.tmp identical at all three points (561,405
  bytes). **On op 2 tmpemesh.tmp = temp.rgn byte for byte**: every line end lies on the domain's border (no line of the other direction
  beyond it), so nothing is extended; the DLL's own FUN_18025d6d0(2) / FUN_1801b5fb0 read it back identical (RGNR).
- em_synth.py seeds 1-40 (lines ending inside the domain, holes, two-block chains): all 40 identical at every check; 7,075 FUN_1801a9150
  calls, 767 extension segments (at gaps FUN_1801a9150 reached Q 3 times).
NOT PORTED (refused): DAT_1804d7038 = 1 (FUN_1801a9e60), containment (DAT_1804e8ffc / FUN_1801b1ab0 / FUN_180192af0), header kinds other
than 0x11, a full store (DAT_1804e9344 = 2).

**Two stage-D questions of 12.23 resolved (asm):**
- tmpzone header +4 short = zonbuf +0x1a = **the pass number**: FUN_1801ab410(zone, pass, 0, &zonbuf) stores it (asm 1801ab4d2 `mov
  [rbx+0x1a], bp`, bp = its second argument; +0x1c = the third, +0x18 = the first), called by FUN_180187850 (asm 180187f3f..180187f50) with
  edx = [rbp+0x9c20] = its 5th argument (rbp = entry rsp - 0x40 - 0x9bb8, so +0x9c20 = entry +0x28) = the pass FUN_180189a90 passes.
- tmpdzdat entries ARE in-process addresses: FUN_1801c16d0 reads 8 bytes and loads [rcx+0x30] / [rcx+0x32] through them (asm
  1801c197d..1801c19a6), so tmpdzdat can be compared only by structure, and the port must model the entries as references to the objects
  whose +0x30 / +0x32 shorts are zone numbers (written from the +0x20 qword of the zone's 0x30 point records, FUN_1801c7060 l.303455; which
  object it points to: NOT DECODED yet).

### 12.27 Stage D2 start: FUN_18018a640, the segment records of both directions (2026-11-14, session 12) - PORTED, = DLL
Source: asm 18018a640..18018af55 (the decompile l.264180-264516 swaps the point's s and z and drops the stack arguments); callees
FUN_1801a6f50 (the trailer), FUN_1801abc40 -> FUN_1801abf40 (kind 2: the block's points read raw, 0x18 each, returns read == n x 0x18),
FUN_1801aac80 (kind 2: +0 s, +8 z, +0x10 code, +0x12 flag), FUN_180195e70 (asm 180195e70: m 1: code 0..3 / 0x28..0x2a; m 2: 0..0x11 /
0x28..0x2a), FUN_1801a9c80 (asm 1801a9c80: unlink). Port: viewer/cam3d_cscallop_seg.c (cs_seg_dir, cs_code_5e70, cs_seg_bytes), driver
`viewer/cfb_port --segs EM TOL P6 P7`; harness mode **SEGS** (EMESH's setup on a tmpemesh.tmp + "SEGS file p6 p7": the DLL's own
FUN_18018a640 on harness arrays - FUN_1801b1ad0 allocates them by FUN_1800cdfa0 mode 2, N13-31 - and the DLL's FUN_1801ad6c0(2, 500, ..)
point buffer); script tools/tests/segs_cmp.sh WORK EMDIR P6 P7 OUT.

**The call** (FUN_1801b1ad0 asm 1801b20c9..1801b2122, direction A first, B when FUN_1801b1ad0's p8): FUN_18018a640(dir, FUN_1801a6f50, buf,
p4 = (header kind != 0xe) = 1, p5 = r14w = 2 (asm 1801b1de5), p6 = xmm7 = min(|stepA|, |stepB|, 10 x DAT_1804e9238) (asm 1801b1f92..
1801b1fc9, 0x1803ed280 = 10.0), p7 = xmm6 = msurf_colaps_dist (bVar3), p8 = DAT_1804e9250 (not read), p9 = 2 n, arr, &n25, ctx, &err);
called again with fresh arrays when it returns more than p9 (l.288948-288955). Line headers (0x20, ctx+0x388 / +0x448): +0 first, +8
last, +0x10 / +0x12 counts, +0x14 dir, +0x18 the line position (the block trailer's). Records (0xb0): +0 next, +8 prev, +0x10 / +0x18 /
+0x20 zero, +0x24 the line (set when finished), +0x28 / +0x2c **the file offsets of the first / last point** (so the records point back
into tmpemesh.tmp), +0x34..+0x3d / +0x40..+0x57 / +0x5a..+0x5d zero (+0x30..+0x33 never written), +0x58 / +0x5e copies of +0x5a / +0x5c,
+0x60 / +0x68 = the start (s, z) copy, +0x70 / +0x78 the start, +0x80 / +0x88 the end, +0x90 / +0x98 the end copy, +0xa0 the start s,
+0xa8 the last s.
**Per point** (in order, the code c' = code - 0x27 for joints): outside a segment, a point of FUN_180195e70(p5)'s family starts one - a new
record when the line has none yet, or the point is outside the last record's block (its point-offset range), or its distance to the
last record's end (p4: |ds| > tol -> sqrt(dz^2 + ds^2), else that |ds|) > p7; otherwise the last record is continued (its end moved to
the point); inside, the end moves to the point when the previous point's code is of the family (or the record is still a point), the
segment goes on through a joint (code >= 0x27) of the family, else it ends: finished (+0x24 = line, copies) or, when its start = end,
dropped (the counts and the index go back). Records beyond p9 - 1 reuse the last slot but are counted (the return = the count asked
for). After the lines (when the count fits): every record with |ds|, |dz| and their length all < p6 is unlinked (FUN_1801a9c80); code-0x25
points counted in *n25.

**Oracle (2026-11-14, session 12)** (segs_cmp.sh: "SG dir ret n25 err", every line header, every record's 0xb0 bytes with the links as
indices, +0x30..+0x33 masked):
- CURVE 5 AXIS op 2 tmpemesh.tmp (p6 = 0x1.26e978d4fdf3cp-7 = 10 x DAT_1804e9238, p7 = 0.05): identical, 118 + 118 segments.
- the 40 synthetic tmpemesh.tmp of 12.25 with their p6: 40 / 40 identical (3,127 segments); seeds 1-10 with p6 / p7 = (0.1, 0.3) and
  (0.16, 0.01) (joins, drops, unlinks): 20 / 20 identical.

### 12.28 Where this stopped (2026-11-14, session 12)
D1 done and DLL-identical (12.25); D2 started: FUN_18018a640 ported and DLL-identical (12.27). Next (12.23 "Port stages proposed", 3-8):
FUN_1801a7fd0's array shapes and FUN_1801b88e0 (no PL2), FUN_1801b88c0 / FUN_1801b84f0 (the 0xb0 pool of FUN_1801b86c0), FUN_18015d0f0's callees, then the zone pass FUN_180189a90 /
FUN_180187850 (D5), FUN_1801c16d0 / FUN_1801597b0 / FUN_180192330, FUN_1801b1ad0's frame (D3), E (FUN_180141e20, FUN_1801b0df0). Op 43 stays
greyed.

### 12.29 Stage D2: FUN_1801b4e80, FUN_1801a7fd0's arrays, FUN_1801b88e0, FUN_1801b84f0 (2026-11-14, session 13) - PORTED, = DLL
Source: FUN_1801b1ad0 asm 1801b1f57..1801b1f84 (A) / 1801b2211..1801b223a (B), 1801b2475..1801b2700; callees from the asm (the decompile
l.293214 garbles FUN_1801b88e0's argument order and l.281830 FUN_1801a7fd0's). Port: viewer/cam3d_cscallop_seg.c (cs_seg_sizes,
cs_seg_pts_alloc, cs_seg_points, cs_seg_flip; CsSeg +0x40 / +0x48 / +0x50 now the array entries, +0x30 / +0x32 / +0x34 / +0x38 separate
fields), driver `viewer/cfb_port --pts EM TOL P6 P7`; harness mode **PTS** (SEGS + the DLL's FUN_1801b4e80 / FUN_1801b88e0 / FUN_1801b84f0 on
harness arrays); tools/tests/pts_cmp.sh WORK EMDIR P6 P7 OUT.
- **FUN_1801b4e80**(n, table, file, FUN_1801a6e70, &mx, &tot) (asm 1801b4e80..1801b4f7d): per line the counts of its blocks along the chain;
  mx = the largest line sum + 10 (short, [rbp-0x34] / [rbp-0x70]), tot = the sum over all lines + 10 ([rbp-0x28] / [rbp-0x58]).
- **FUN_1801a7fd0**(DAT_1804d5729 = 2, mx, tot, DAT_1804d53b8 = FUN_1801adf10(nsegsA, nsegsB, 100, mxB), 7 descriptors, 14 outputs)
  (asm 1801a7fd0..): FUN_1800ce680(7, ..); FUN_1801aaaa0(DAT_1804d53b8, &mode) (asm 1801aaaa0) = max(pl2MemBufRemainFit(0x13), edge_fit(0x13))
  = the number of 0x13-byte points (0x10 (s, z) + 2 code + 1 flag) the licence-sized buffer still holds. **tot < that**: only
  FUN_1801a7f00 (asm 1801a7f00..1801a7fc2) allocates - descriptor 4 = tot x 0x10, 5 = tot x 2, 6 = tot x 1, counts p14 / p16 / p18 = tot;
  the outputs of descriptors 0..3 (ctx +0x3a0.. / +0x460..: DAT_1804d5730.. / DAT_1804d57f0..) stay NULL. Otherwise (the windowed arm)
  descriptors 1..3 of mx entries and 0 of mx x 0x18 (p1 = 0 / 2) or x 0x30 (p1 = 1), then FUN_1801a7f00 with the buffer's fit: FUN_1801b88e0
  fills the arrays only partly and FUN_1801acb60 / FUN_1801ac3c0 (via FUN_1801ac9b0 / FUN_1801aca80 / FUN_1801acc80, FUN_1801ac200 /
  FUN_1801ac2e0 / FUN_1801ac540) reload a record's points when its +0x48 / +0x40 is NULL. Per N13-28 (the ~18 MB buffer, ~990k points) **the
  in-memory arm is ported; the windowed arm is NOT PORTED** (op 2: tot = 11,319 / 11,277).
- **FUN_1801b88e0**(file, FUN_1801a7880, nsegs, n4, n5, n6, records, flags, codes, points) (asm 1801b88e0..1801b8b0f): for each of the first
  nsegs records of the array (unlinked ones too) the points from +0x28 while the position after the read is <= +0x2c (asm 1801b8a7c, a
  32-bit compare); FUN_1801a7880 (asm 1801a7880) reads 0x18 bytes (+0 s, +8 z, +0x10 code, +0x12 flag). The record's first point sets
  +0x40 / +0x48 / +0x50 to the arrays' next entries; an array that fills sets that field of the filling record to NULL and is not written
  again. Then +0x3c = k (the count), +0x58 = 0 (int), +0x5c = +0x5e = k - 1. Returns 0 for nsegs < 1 or a read failure.
- FUN_1801b88c0 (zeroes DAT_1804e97b8 / DAT_1804ea448 / DAT_1804ea440), FUN_1801adab0(nsegsA + nsegsB, n25A + n25B) = (n25 / 2 - nsegs) +
  100 when nsegs < n25 / 2, else 100; FUN_1801b86c0(that) = the 0xb0 record pool (edge_alloc, else calloc; linked by +0x10, head
  DAT_1804e97b8; FUN_1801b8870 pops, refilling by FUN_1801b86c0(100)). The port appends records instead (12.30).
- **FUN_1801b84f0**(&ctx +0x418, nB, ctx +0x448) (B only) -> **FUN_1801b8410** per record on a line's list (asm 1801b8410..1801b84ec): the
  line position *= +0x420 (1.0); +0x70, +0x80, +0x60, +0x90 *= +0x418 (-1.0); +0xa0 = +0x70, +0xa8 = +0x80; when +0x40 is set the s of its
  points +0x58 .. +0x5e *= -1.0. Records off the lists (unlinked by FUN_18018a640's FUN_1801a9c80) keep their s.
- Before them: FUN_180190140(ctx) (asm 1801b2498: ctx +0xc8 = +0xe8 = +0x108 = +0x28).
**Oracle** (pts_cmp.sh: SEGS's trace, "PZ dir mx tot ret", the line headers and every record again after FUN_1801b84f0 with +0x40 / +0x48 /
+0x50 as array indices, "PA" an FNV hash of the three arrays): CURVE 5 AXIS op 2 identical (236 records, 11,319 + 11,277 points); the 40
synthetic tmpemesh.tmp of 12.27 with their p6 / p7 and seeds 1-10 with (0.1, 0.3) and (0.16, 0.01): 60 / 60 identical.

### 12.30 Stage D2: FUN_18015d0f0's first part - FUN_180150840 (split by code class), FUN_18014d5c0 (merge) (2026-11-14, session 13) - PORTED, = DLL
FUN_18015d0f0(ctx, abort) (l.232037, asm 18015d0f0): for A (ctx +0x338 > 0) FUN_180150840(0, ctx, &err) then FUN_18014d5c0(0, ctx), then
the same for B; when both directions have lines: loop { FUN_180149a20(ctx, abort); FUN_18014d5c0(0) / (1) } while either merged something;
then FUN_180146e60(0, ctx, abort) and (1). The first part is ported: viewer/cam3d_cscallop_d15.c (cs_cls_7620, cs_d15_split, cs_d15_merge;
CsSegDir = one direction's records, headers and arrays), driver `viewer/cfb_port --d15 EM TOL P6 P7 B2B0`; harness mode **D15**;
tools/tests/d15_cmp.sh WORK EMDIR P6 P7 OUT [B2B0]. Context values (FUN_1801b1ad0): ctx +0x2b0 = DAT_1804d5640 = p10 (DAT_1804e9140 =
DAT_1804d7038, 2 for op 2), 2 when bVar3 and header kind 0xe (asm 1801b1deb..1801b1df9); ctx +0x2d8 = DAT_1804d5668 = 3 x max(|stepA|,
|stepB|) (asm 1801b1f1d..1801b1f4f, 3.0 at 0x1803eba58); ctx +0x309 = DAT_1804d5699 = FUN_18024d390(cur_op, &DAT_1804d56a0, ..) (asm
1801b2015..1801b201a; l.388326: nonzero only for ops 0x28 / 0x29 / 0x2a) = 0 for op 0x2b.
- **FUN_180157620**(cls, code) (asm 180157620..1801576bd; the two jump tables 0x1801576c0 / 0x1801576dc and their index bytes 0x1801576c8 /
  0x1801576e4 read from the DLL file): code >= 0x27 taken as code - 0x27; cls 0: {0, 2, 4, 5, 6, 7, 9, 11, 12, 13, 14, 16}; cls 1: {1, 3, 8,
  10, 15, 17} or FUN_18015a310 (code - 0x12 <= 0x11: 18..35); cls 2: {0, 2}; cls 3: 1, 3..17 or FUN_18015a310. With ctx +0x2b0 == 2 the
  classes are A = 0, B = 1 (else 2 / 3).
- **FUN_18014b550**(cls, start, n, codes, R) (asm 18014b550..18014b639): from start the points of the class up to and including the first
  point after start that is not (or n - 1); R = {start, that}, returns R[1]; none of the class, start outside [0, n) or R[1] == start:
  R = {-1, -1}, returns start. Consecutive runs share their boundary point.
- **FUN_1801504e0**(dir, ctx, 1, hdrs, R, &err) (asm 1801504e0..18015083a) per record: codes = FUN_1801acb60 (= +0x48), points =
  FUN_1801ac3c0(.., 0) (= +0x40). First point of class B: runs B (from 0) then A; else of class A: run A from 0 (no B before); else the record
  is freed (FUN_1801a9c80(1, ..): unlinked, onto DAT_1804e97b8). No A run: freed. No B run before and none after the first A run: kept
  unchanged (returns 0). Otherwise per A run **FUN_18014e050**(prevB, A, nextB, points, L) (asm 18014e050..18014e188): fails unless the runs
  touch; L +0x5a / +0x5c = A, +0x70.. / +0x80.. its points, +0xa0 / +0xa8 their s, +0x58 / +0x5e = prevB start / nextB end (A's own when
  absent), +0x60.. / +0x90.. their points. The first run is written into R (FUN_180154b40, asm 180154b40: those fields only), each further
  one into a pool record = a copy of R as it was (FUN_1801ab500(1, L, hdrs, last), asm 1801ab500..1801ab623: 0xb0 bytes copied, inserted
  after the last, line counts +0x10 / +0x12 + 1). Errors: FUN_180268e20(0) = -11 (runs not touching), (2) = -4 (no pool record).
- **FUN_180150840**(dir, ctx, &err) (asm 180150840..180150921): FUN_1801504e0 on every record of every line, the next record taken before
  the call; stops at an error.
- **FUN_18014d5c0**(dir, ctx) (asm 18014d5c0..18014d6c1): per line, while a record has a next one: both cut from one segment
  (**FUN_180155810**, asm 180155810: same +0x28, +0x2c, +0x40), **FUN_180156cf0**(ctx +0x2d8, dir, ctx, 1, R) (asm 180156cf0..180156f3e: the
  distance from R's end (+0x80, +0x88) to the next one's start (+0x70, +0x78) <= gap and, along the points **FUN_18014b260** gives (asm
  18014b260..18014b333: Q = {R +0x5c - 1 (0 when <= 0), min(next +0x5a + 1, R +0x3c)}), the path end -> points Q[0] + 1 .. Q[1] - 1 -> start
  shorter than gap; one step: the distance) and **FUN_1801574a0** (asm 1801574a0: 1 when ctx +0x309 == 0) -> **FUN_18014d6d0**(1, hdrs, R)
  (asm 18014d6d0..18014d764: R takes the next one's +0x5c, +0x80, +0x88, +0xa8, +0x32, +0x38, +0x5e, +0x90, +0x98; the next one freed) and R
  is tried again; else the next record. Returns the merges.
- The pool: the port appends records (CsSegDir, realloc); the DLL's addresses (pool order, freed records reused) are not modelled, so
  records are compared in list order. The harness repeats FUN_1801b86c0's body on its own array (N13-31) of FUN_1801adab0's size + 50,000
  spare records ("QP ok" = the DLL never called FUN_1801b86c0 itself).
**Oracle** (d15_cmp.sh: PTS's trace, "QS dir splits err", "QM dir merges", per line "QH dir line n10 n12 dir pos k" and its records in list
order "QR" (0xb0 bytes, +0 / +8 as list positions, arrays as indices)): CURVE 5 AXIS op 2 identical (0 splits, 0 merges: every segment there
is of one class); **mixed-code synthetic files** (tools/tests/em_synth.py SEED REF OUT **mix**: a run's joints and ends get codes of both
classes and of neither - test inputs only) seeds 1-30: emesh_cmp.sh trace identical for all 30; d15_cmp.sh identical for all 30 with (10 x
DAT_1804e9238, 0.05) and for seeds 4-30 also with (0.1, 0.3) and (0.16, 0.01): 84 / 84 runs identical, 36,381 runs written, 27,219 merges,
10,091 records left in the lists, QP ok everywhere.

### 12.31 Where this stopped (2026-11-14, session 13)
Done and DLL-identical: D2 up to FUN_18015d0f0's first part (12.29, 12.30). NOT PORTED (refused): the windowed arrays of FUN_1801a7fd0 (a
record's +0x40 / +0x48 NULL), ctx +0x309 != 0 (ops 0x28..0x2a), plus the refusals of 12.25. Next, in FUN_18015d0f0's order:
1. **FUN_180149a20**(ctx, abort) (asm 180149a20..180149c8c, own 184 lines; decompile l.216411, closure 4678 lines, no PL2): a work object
   (FUN_1801459c0(obj, 100) / destructor FUN_180145ba0), a gather FUN_18014aa50 (238 lines: FUN_18014a950 (recursive), FUN_180154a70,
   FUN_1800cdb20 / FUN_1800679c0 / FUN_18022d010), per gathered entry (0x38 bytes, +0 and +0x30 set) FUN_180149980 (-> FUN_18014a0a0, 373 lines,
   the hub: FUN_18017b860, FUN_180155810, FUN_1801574a0, FUN_180156cf0, FUN_18014c740, FUN_180156740, FUN_1801556c0, FUN_180154ac0, FUN_180148d20,
   FUN_1801575e0, FUN_180148140; and FUN_180156800) whose result 0..8 picks a counter by the jump table at 0x180149c90 (cases 1 / 2 ->
   FUN_180150ff0(+1 / -1, ..) -> FUN_18014d6d0), then FUN_1801513e0 compares the counters with the previous round's and the whole repeats
   while the gather's flag [rbp+0x77] is set; 3 x mccore tol (asm 180149a6b..180149a7d) is FUN_180149980's tolerance. NOT DECODED beyond this
   structure. The oracle is ready: `D15_FULL=1` in the harness's D15 mode runs the DLL's FUN_180149a20 / FUN_18014d5c0 loop after the
   first part ("QL round ret abort mergesA mergesB"; no pool growth, "QP ok"): op 2 one round, nothing changed; mixed-code seeds 1 / 3 / 5
   (variant a) 2-4 rounds with 3-16 further merges per round - so FUN_180149a20 does change the records there, and op 2 does not exercise it.
2. FUN_180146e60(dir, ctx, abort) (own 159 lines): FUN_180169990 allocates through FUN_1800cdfa0 (PL2): repeat its body with harness arrays
   (N13-31); FUN_1801470e0 (376 lines: FUN_180147980, FUN_1801a9c80, FUN_18017b860, FUN_1801aad80, FUN_180149e90, FUN_1801b1ab0, FUN_18014a070,
   FUN_1801498e0, FUN_1801561e0, FUN_1801af5f0).
3. FUN_18015c8f0(ctx) (55 lines, FUN_18014b350) -> DAT_1804d56c0; then FUN_180186ff0 (only when DAT_1804d53b4, i.e. op +0x12e0 == 0), the zone
   pass (D5: FUN_180189a90 / FUN_180187850, FUN_1801c16d0, FUN_1801597b0, FUN_180192330), FUN_1801b1ad0's frame (D3) and stage E (12.28).
   Op 43 stays greyed.

### 12.32 Stage D2: FUN_180149a20, the crossing pass of FUN_18015d0f0 (2026-11-14, session 14) - PORTED, = DLL
Source: objdump of mcmsurf.dll for every function below (the decompile l.215586-217400 drops or garbles arguments: FUN_18014a950's entry,
FUN_180235b00's double x, FUN_1801864b0's stores, FUN_180148960's mode); mccore's UnitNormal / lp_ints / proj_pt_ln_3d / pt_equal as 12.13.
Port: viewer/cam3d_cscallop_9a20.c (cs_d15_9a20; records as (direction, index), the line header's +0x14 = the direction, checked), driver
`D15_FULL=1 viewer/cfb_port --d15 ..` (FUN_18015d0f0's loop: FUN_180149a20 then FUN_18014d5c0(0) / (1) while either merges, "QL round ret
abort mergesA mergesB"), oracle tools/tests/d15_cmp.sh with D15_FULL=1 (the harness's D15_FULL loop, the DLL's own functions) and
**D15_UNIT=1** (unit oracle: the port writes every FUN_180148960 / FUN_180248a70 / FUN_1802485d0 call - inputs and its result - to a file,
D15_UDUMP; `peloader --lmm D15` with D15_UCALL runs the DLL's function on each and prints its result; "UNIT k / n identical").
- **FUN_180149a20**(ctx, abort) (asm 180149a20..180149c8c): a CBlockBsda work object (FUN_1801459c0(W, 100): +8 / +0xc the current
  direction / line, +0x10 / +0x14 the stop, +0x18 count, +0x1c capacity, +0x20 the 0x38-byte entries; starts at B line 0); per gather
  FUN_18014aa50 every entry with both records set goes through FUN_180149980(ctx, 3 x tol, E) (tol + tol + tol, asm 180149a6b) and its
  result 0..8 is counted (jump table 0x180149c90: 0, 1, 2 (1 / 2: FUN_180150ff0(+1 / -1)), 3 .. 8; other -> -16). After a complete gather
  the 10 counters ([rbp-0x79]: total, 0, 1, 2, 4, 5, 6, 7, 8, 3) are compared with the previous pass's (FUN_1801513e0): equal -> done;
  else W restarts at its stop. A gather that finds nothing ends it.
- **FUN_18014aa50** (asm 18014aa50..18014ae12): from W's line to the end of its direction, then the other from line 0, until W's stop; per
  record of a line, sides 0 and 1, FUN_18014a950 into entries[count]. A full array: on the call's first line (line == the line it started
  at, also in the second direction) it grows by 2 x the line's +0x10 + 100 (FUN_180154a70 / FUN_1801458f0) and the gather ends after
  that line; elsewhere the call returns at once with done = 0 and the line is gathered again (and its entries handled again) next call.
  ms_break / the status bar (mfc #8010 0x15b) are UI.
- **FUN_18014a950**(ctx, side, rec, hdrs, E): P = FUN_1801864b0(side, rec) (P[line dir] = the line position, P[dir == 0] = s, P[2] = z of
  the start / end); FUN_1801565a0(cls (B line: side 0 -> 1, 1 -> 3; A: 0 / 2), P): FUN_18016e800 / FUN_18015f990 (the A lines either side
  of P[0], within tol of the frame, floor((P - first) / step) and +1, swapped for a negative step; the B lines of P[1]), the first record
  of that other-direction line whose s range holds P[1] (A) / P[0] (B); flag 0 / 1 when P is pt_equal to its start / end, else 2. E = {rec,
  side, P, dir, flag, the crossing record}.
- **FUN_18014a0a0**(ctx, 3 tol, E) (asm 18014a0a0..18014a652), FUN_180149980 repeats it (with FUN_180156800 = FUN_18014a950 again) while it
  returns 0: 8 when FUN_18014a020 refuses P (DAT_18050341a: P.z within tol of DAT_180503438 / DAT_1804d6fb8 = op +0x1288 / +0x1290); x = the
  crossing line's position +- 3 tol (sign by FUN_18017b860 = the direction's first record with |+0xa0 - +0xa8| > 1e-12, and the side);
  the neighbour on that side (prev / next) reaching past x: not the same segment (FUN_180155810) -> 4 / 5, the gap short (FUN_1801574a0 =
  1 for op 0x2b, FUN_180156cf0) -> 2 / 1 (merge), else 7 / 6. Otherwise the extension refreshed (FUN_18014c740 x inside +0x60..+0x70 /
  +0x80..+0x90; else FUN_180156740 / FUN_1801556c0 the same-segment gap's path point, or FUN_180154ac0 reset to the segment's first / last
  point) and FUN_180148d20: 3 -> the end moved to x at the crossing's z (FUN_180150930: the crossing at the other record's end, flag < 2);
  else FUN_1801a9770 walks the extension to x (DAT_1804e9250 minimum span; idx, the point, the path length d): d > ctx +0x2d8 -> 7 / 6,
  else the end moved there (1 when FUN_180156b60 finds the other direction there); otherwise FUN_180148f90 (the continuation: S1 = the
  extension's points up to x (FUN_180235b00, bisection), S2 = the crossing record's points within max(|stepA|, |stepB|) of the line, its
  ends set to the record's end points during the call): FUN_180148960 (S2's end within ctx +0x2b8 (DAT_1804e9238) of P in the line
  coordinate - mode 1 when S2's byte != 0, else 4 (asm 180148a18 cmove) - or within half of it in z; then the nearest point of S1,
  FUN_180249c50 / FUN_180249ef0), else FUN_180248a70 (alternating closest segment pairs, FUN_1802532e0 = FUN_1802409e0 (common normal,
  UnitNormal, the 0.05 rescale FUN_180236cb0, lp_ints) + the end clamps FUN_18024aec0; t > 0.999 / < 0.001 steps on, 2-cycle FUN_18021e150),
  else or farther than g + tol FUN_1802485d0 (S1's segments clipped to the box +- 4 tol x box1 (FUN_180237220 / FUN_1802537e0) against S2
  by FUN_18023fbd0); found -> 2: the end moved to x at R's z. After any move FUN_180148140 pulls the same-segment neighbour's extension back.
- **FUN_180150ff0**(ctx, d, k, W): FUN_18014d6d0(d, hdrs, rec) - d = 1 as 12.30, d = -1: R takes the previous record's +0x5a, +0x70, +0x78,
  +0xa0, +0x30, +0x34, +0x58, +0x60, +0x68 - and the freed record is cleared from every entry's +0 / +0x30.
- Context: ctx +0x2b8 = DAT_1804e9238 (FUN_18018f780 asm 18018f7c0; the port's temp.rgn trailer tl[0]), DAT_1804e9250 = tl[3], ctx +0x2d8 =
  3 x max |step| (12.30), mccore tol. The z limits DAT_18050341a / DAT_180503438 / DAT_1804d6fb8 come from the op (+0x1280 / +0x1288 / +0x1290, asm
  180206cb2..180206cd7); **op 2 has them on (1, -2.025, 0.375)**; d15_cmp.sh passes the harness's values (its "QZ" line) as CFB_ZL.
- NOT PORTED (refused, -100): a record whose +0x40 is NULL (FUN_1801a7fd0's windowed arm, N13-28); FUN_1801574a0 / FUN_1801575e0's arm for
  ctx +0x309 != 0 (ops 0x28..0x2a, refused before).
**Oracle (2026-11-14, session 14):** CURVE 5 AXIS op 2: identical (one pass, nothing changed, "QL 1 0 0 0 0"); em_synth.py mix seeds 1-30 x
the three (p6, p7) of 12.30: **90 / 90 runs identical** (records, line headers, QL rounds 2-4 with their merge counts); unit oracle
10,586 / 10,586 calls identical (5,380 FUN_180148960, 5,206 FUN_180248a70); with the FUN_1802485d0 check on every FUN_180248a70 input
(seeds 1, 3, 7, 2: 226 more calls) identical; D15_ZLSET (test-only z limits on record end heights, seed 2): 8 refusals, identical.
Coverage of the sweep: results 0 (5,380) / 1 (307) / 2 (115) / 3 (26,935) / 4 (514) / 5 (507); FUN_180148d20 0 / 1 / 2; in the pass itself
FUN_1802485d0 was never needed (FUN_180148960 174, FUN_180248a70 5,206). **Not exercised by any input**: FUN_180150930's branch (r = 3: the
crossing exactly at the other record's end), results 6 / 7 (gap or path longer than ctx +0x2d8) - ported from the asm, unverified.

### 12.33 Where this stopped (2026-11-14, session 14)
Done and DLL-identical: FUN_180149a20 (12.32), so FUN_18015d0f0 is ported up to its FUN_180149a20 / FUN_18014d5c0 loop. Next, in order:
1. **FUN_180146e60**(dir, ctx, abort) for 0 then 1 (asm 18015d1bf / 18015d1db; only when both directions have lines, after the loop).
   Mapped from the asm (180146e60..1801470dc), not ported: own count / headers and the other direction's count m; FUN_1800ce680(2, desc)
   + FUN_180169990(m + 0x32, 1, desc, &err) = two 0x30-byte descriptors of (m + 0x32) x 8 bytes, calloc (FUN_1800cdfa0 mode 1, which also
   calls pl2MemBufRemainAdjust: N13-31, harness arrays); then per line, per record (next taken first, asm 180147030) FUN_1801470e0(ctx,
   hdrs, rec, desc, &err), stopping at an error; ms_break -> abort; FUN_1800ce590 frees. **The oracle is ready**: `D15_FULL=1 D15_6E60=1`
   in the harness's D15 mode repeats that body on harness descriptors with the DLL's own FUN_1801470e0 and prints "QE dir err ctx+0x30a"
   before the QH / QR lists (d15_cmp.sh does not set it yet - add it with the port). Op 2: no change; mixed seed 3: 396 list lines change;
   ctx +0x30a = 0 there (it is set only later, FUN_1801b1ad0 asm 1801b2a9f and the zone builder asm 180187b59).
   **FUN_1801470e0**(ctx, hdrs, rec, desc, &err) (asm 1801470e0..180147975, decompile l.214285, 331 lines): own line dir 0 -> other = B
   (step ctx +0x408, hdrs +0x448, count +0x338, flags 0 / 2), else A (+0x348, +0x388, +0x3f8, 1 / 3). When ctx +0x30a and |s1 - s0| <
   |step|: FUN_180147980(ctx, step^2, hdrs, rec, &err) and, true, the record freed (FUN_1801a9c80(1, ..), returns -1). Otherwise
   FUN_18017b860, a box around the record (s0 / s1 x line position +- (tol x 0.5 + tol)), FUN_1801aad80 (-> FUN_1801c9520 / FUN_18015f990 /
   FUN_1801c65e0: the other direction's lines in the box), FUN_180149e90 (> 2 needed), FUN_1801b1ab0 on descriptor 1, then along those lines
   FUN_18014a070 / FUN_1801498e0 collect positions into descriptor 0 (full: FUN_180268e20(2)), and FUN_1801561e0 (-> FUN_1801aeac0) +
   FUN_1801af5f0 (265 lines: FUN_1801af0f0 / FUN_180166e50, FUN_1801a9980 / FUN_1801a9770, FUN_180166480, FUN_1801ab500 = new pool records)
   rebuild the record. Its asm also calls FUN_18015cf30 (4 x) and 0x1803c7fc8 (CRT). NOT DECODED beyond this structure.
2. FUN_18015c8f0(ctx) (FUN_18014b350) -> DAT_1804d56c0, FUN_180186ff0 (only when op +0x12e0 == 0) and the zone pass FUN_180187850 (1,291
   lines; its closure is the largest of stage D: FUN_1801bca00 605, FUN_1801b6100 888, FUN_18017daa0 323 and the FUN_18016xxxx /
   FUN_18017xxxx families), FUN_1801c16d0 / FUN_1801597b0 / FUN_180192330 (D5), FUN_1801b1ad0's frame (D3, FUN_1801b1ad0 as collapse core),
   stage E. Op 43 stays greyed.
### 12.34 Stage D2: FUN_180146e60 (records cut at the other direction's lines) and FUN_18015c8f0 (2026-11-14, session 15) - PORTED, = DLL
Source: objdump of mcmsurf.dll (the decompile l.214170-214620 drops FUN_1801470e0's arguments at the call and both jump tables); jump
tables read from the DLL file (0x1801afa50: 7 entries, 0x1801af170: 13); constants 1e-12 (0x1803eb800), 0.5 (0x1803eb810), 1.0 / -1.0
(0x1803eb820 / 0x1803eb888), 1.5 (0x180405548), 1e38 (0x1803ea528). Port: viewer/cam3d_cscallop_6e60.c (cs_d15_6e60, cs_d15_c8f0), driver
`D15_FULL=1 D15_6E60=1 viewer/cfb_port --d15 ..` ("QE dir err ctx+0x30a", then "QV" = FUN_18015c8f0), oracle tools/tests/d15_cmp.sh with
the same variables (the harness's D15_6E60: FUN_180146e60's body on harness descriptors (N13-31) with the DLL's FUN_1801470e0, then the
DLL's FUN_18015c8f0).
- **ctx +0x30a = DAT_1804d569a is 1 here** (correcting 12.33): FUN_1801b1ad0 sets it at asm 1801b2a9f, just before it calls FUN_18015d0f0
  (asm 1801b2ab0), when bVar3 and header kind 0x11 (op 0x2b). Its only reader in FUN_18015d0f0's tree is FUN_1801470e0 (grep of the asm:
  1801471a6; the zone builder writes it at asm 180187b59, FUN_1801b9cea reads it later), so the ported FUN_180150840 / FUN_18014d5c0 /
  FUN_180149a20 are unaffected. The harness now sets it in D15 mode; CsLoop.f30a in the port.
- **FUN_180146e60**(dir, ctx, abort) (asm 180146e60..1801470dc): own count / headers, m = the other count; FUN_180169990(m + 0x32, 1, ..)
  = two descriptors of m + 0x32 doubles (port: own arrays); per line, per record (next taken first, asm 180147030) FUN_1801470e0, stopping
  at an error; status bar / ms_break UI.
- **FUN_1801470e0**(ctx, hdrs, R, desc, &err) (asm 1801470e0..1801476bb): other direction = B for an A record (step ctx +0x408, headers
  +0x448), else A. With ctx +0x30a and |step| > |s1 - s0|: **FUN_180147980**(ctx, step^2, ..) (asm 180147980..180147c64): 0 when dz^2 +
  ds^2 of R's ends (FUN_180239160) > step^2; else the points (s0 / s1 at the line position + step / 2, then - step / 2) must both lie in
  the grid (FUN_1801716d0 = FUN_180176c90 (FUN_18015f990) + FUN_1801716b0, the cell = the lower line of each pair); none: 0; the same cell
  (FUN_18017c1a0 = (nA - 1) x cell[1] + cell[0]): 1; else the line the two cells share - none, or a line without records: 0 (asm
  180147c54 calls FUN_180268e20(3) and drops its value: no error) - and 1 when no record of it crosses R (FUN_1801498e0). 1 -> R freed
  (FUN_1801a9c80(1, ..)), returns -1; 0 -> returns 0 (the record is kept as it is). Otherwise: FUN_18017b860 (up), a box = R's s range x
  the line position +- (tol x 0.5 + tol); **FUN_1801aad80** -> FUN_1801c9520 (asm 1801c9520..1801c9677): the lines of both corners
  (FUN_18015fba0 = jmp FUN_18015f990, else **FUN_1801c65e0**, asm 1801c65e0: the same lines clamped into the grid), out[0] / out[2] the
  B lines, out[1] / out[3] the A lines; **FUN_180149e90** (asm 180149e90..18014a010): from each end of that span the first line within
  1e-12 of R's s range, 3 = both found (else return 0); none crossed between (out equal): 0. Then A0 = s0, per line of the span (end lines:
  the position outright when the end point is refused by the op's z limits, **FUN_18014a070** = FUN_1801864b0 + FUN_18014a020; else, as
  for the inner lines, the position when a record of the line crosses R, **FUN_1801498e0** asm 1801498e0), s1; A0 full (desc +0xc):
  FUN_180268e20(2) = -4. Only s0 / s1: R freed, -1. Else **FUN_1801561e0**(|step|, n, A0, W, &err) (asm 1801561e0..180156473; W =
  FUN_1801b1ab0 on descriptor 1): per run of steps shorter than tol + step a start before it and an end after it (half the neighbouring
  step - 3 tol, at most step - 3 tol; the first / last step: step - 1.5 tol or |A0[0] - A0[1]| - tol), sorted by **FUN_1801aeac0**
  (insertion sort) when W[0] > W[1]; returns the count of the long steps (0 -> return 0). Then **FUN_1801af5f0**(W, dir, up, ctx, R) (asm
  1801af5f0..1801afa4d; always 0): per pair (W[k], W[k + 1], **FUN_1801ada20**: -1 -> 0 -> 2 ..) **FUN_1801af0f0** (asm 1801af0f0: FUN_180166e50
  with DAT_1804e9250, the table at 0x1801af170) of R's (s0, s1) as it came (s1, s0 when not up): **FUN_180166e50** (asm 180166e50..18016702d:
  four **FUN_18014c710** compares (0 within eps, 2 below, 3 above, 4 equal / unordered) -> 1..13 or 0xffdf) -> 0 / 2 next pair, 1 both
  ends to the pair, 4 one end (R itself), 5 the other end and stop, 3 / 6 stop. An end is moved by **FUN_1801a9980**(mode, x, 1e38, dir,
  ctx, R) (asm 1801a9980..1801a9ab2: FUN_1801a9770 along R's points from that end; +0x5a / +0x70.. or +0x5c / +0x80..). Case 1 moves R
  itself the first time, later a copy of R as it came; 5 likewise; a copy is inserted by FUN_1801ab500 beside the last record, the side
  from **FUN_180166480** (asm 180166480..18016654b: same line and sense, -1 before / 1 after; 0 -> stop).
- **Header n** (new, also Mastercam's behaviour): FUN_1801c65e0 lets an index reach n (b == n passes, asm 1801c6658 / 1801c6704), so
  FUN_180149e90 / FUN_1801470e0 read the other direction's header n, one past the array. FUN_1801b1ad0 allocates the headers (FUN_1801ae960
  descriptor 0: ctx +0x338 x 0x20) and the records (descriptor 1: x 0xb0) in one FUN_1800cdfa0 call of mode 2 (r14d = 2, asm 1801b1de5,
  1801b2097 / 1801b228a) = pl2StackAlloc first: mccore 1805903c0 -> 1805797c0 is a bump allocator (base + used, no header, no alignment).
  With the large buffer (N13-28) header n is therefore record 0's first 0x20 bytes: +0 its next = the header's first, +0x18 its +0x18
  (0: FUN_18018a640 zeroes +0x10 / +0x18 of every record it opens, asm 18018abb9 / 18018abbd) = the position 0.0. Record 0's memory may
  have been freed since (FUN_1801a9c80 leaves +0 / +8, asm 1801a9c80..1801a9d41) and reused (the LIFO free list DAT_1804e97b8: pushed at asm
  1801a9d2a..1801a9d38, popped by FUN_1801b8870 in FUN_1801ab500), for a record of either direction. The port models that list (CsFree:
  every FUN_1801a9c80(1, ..) pushes (dir, index), every FUN_1801ab500 pops; occ[X] = the port record now in X's record-0 memory; freed
  records keep their +0 / +8 in sn / sp) and follows header n's chain through that record as the DLL does (each record's line looked up in
  the other direction's headers). Refused (ND, -101, NOT PORTED): a chain that reaches a further freed record, or a line index past the
  headers. **The harness now lays the headers and the records out as Mastercam does** (one block, records right after the headers;
  before, a separate zeroed header n+1 hid this: the harness and the port both agreed with each other but not with Mastercam's layout).
- **FUN_18015c8f0**(ctx) (asm 18015c8f0..18015c9c3), ctx +0x309 == 0: max(FUN_18014b350(0), (1)) (asm 18014b350: the largest |z0 - z1|
  of the direction's records, from 0.0). FUN_1801b1ad0 keeps max(that, DAT_1804d56c0 = ctx +0x330) (asm 1801b2ada..1801b2ae4); the
  earlier value of ctx +0x330 is FUN_1801b1ad0's frame (D3, not found among its own writes). The ctx +0x309 arm is not ported.
**Oracle (2026-11-14, session 15)**: CURVE 5 AXIS op 2 identical (records unchanged: every record has no long step, FUN_1801561e0 = 0; QV 0x1.889f79p-24; also with D15_FULL alone after the harness layout change); em_synth.py mix seeds 1-30 x the three (p6, p7) of 12.30: **90 / 90 identical**; the new `mixrun` inputs (em_synth.py: codes kept for long stretches) seeds 101-115 x 3: **45 / 45 identical** (and their 15 emesh traces); D15_ZLSET (test z limits on record end heights, seeds 1 / 4 / 102): 9 / 9 identical. 27,038 records compared in lists. Coverage: FUN_180147980 1,764 calls (609 records freed), FUN_180149e90 < 3 379, span equal 284, none crossed (freed) 128, FUN_1801561e0 counts 0..4+ 1,901 / 2,988 / 2,264 / 1,463 / 2,761, FUN_1801af0f0 cases 1 / 4 / 5 = 12,409 / 5,316 / 5,477, header n read 85 times (with the free list modelled; before it, 7 of the 90 mix runs and 3 of 3 mixrun runs of one seed met record 0 freed or reused), z-limit end lines 5.
- Before the fixes the sweep found two port errors, both corrected from the asm: FUN_180147980's empty shared line returns 0 without an error (asm 180147be3 / 180147c54), and header n (above).
- Not exercised by any input: FUN_1801af0f0's cases 0 / 2 / 3 / 6 (only 1 / 4 / 5 occur), FUN_180149a20's results 6 / 7 and FUN_180150930's
  branch (12.32; the new em_synth.py `mixrun` inputs - codes kept for long stretches - did not reach them either).

### 12.35 Where this stopped (2026-11-14, session 15)
Done and DLL-identical: FUN_18015d0f0 whole (12.30, 12.32, 12.34) and FUN_18015c8f0, i.e. D2 up to FUN_1801b1ad0 asm 1801b2ae4. Next:
1. **FUN_180186ff0** (only when DAT_1804d53b4 = FUN_18016a2a0(ctx, 100), i.e. op +0x12e0 == 0, as op 2): opens tmpprojz.tmp / tmpdztmp.tmp
   "wb+", calls the zone builder FUN_180187850 (asm 180187202) and then FUN_180162dd0 (asm 180187236). So it is D5 too.
2. D5, the zone builder **FUN_180187850**: own 1,955 instructions, closure 33,697 instructions in 322 functions (the direct calls in the
   objdump, by instruction count); its largest callees FUN_1801bff10 (closure 17,680 / 169 functions), FUN_180195fc0 (13,442 / 154), FUN_1801bca00
   (762 own, 11,418 / 133), FUN_1801c2b40 (2,558), FUN_1801a7d60 (1,781), FUN_1801aa5e0 (1,682), FUN_18018b180 (1,555), FUN_180182b70
   (1,529), FUN_1801aade0 (1,339), FUN_180159fa0 (1,055). Then FUN_1801c16d0 / FUN_1801597b0 / FUN_180192330 (12.23). D3 (FUN_1801b1ad0's
   frame) and E after. Op 43 stays greyed.

### 12.36 Stage D5: the zone builder FUN_180187850 - a whole-function DLL oracle, its call coverage, the port plan, Z0 ported (2026-11-14, session 16)
Sources: objdump of mcmsurf.dll (asm ADDR), decomp/mcmsurf_all.c (l.N). Sizes are instructions of the objdump; functions = the decompile's
"// ==== FUN_" markers plus every direct call target; "closure" = the direct-call closure (CRT functions 0x1803c.... left out).

**FUN_180186ff0**(double d = DAT_1804e9240, ctx, abort) (asm 180186ff0..1801872ae; FUN_1801b1ad0 calls it at asm 1801b2bb1 when DAT_1804d53b4,
after FUN_18015a2b0 when its p6 is set, asm 1801b2b74..1801b2b99): DAT_1804e930c = 0, FUN_1800ce680(3, desc), tmpprojz.tmp and tmpdztmp.tmp
opened by SysFile::Open(.., 0x40) (failure: FUN_180268e20(1)), a third SysFile constructed and never opened, then **FUN_180187850**(&projz,
&dztmp, &sf3, &n4 = 0, 1, 1, 0, 0, 0x32, d, ctx, desc, &zone_off, &err, abort) (stack arguments asm 1801871a5..180187202; the decompile drops
11 of the 15); err -13 (0xfff3) with zones > 0 becomes 0 (asm 180187207..180187219); then, no abort and err >= 0, **FUN_180162dd0**(ctx,
&projz, zone_off, zones) (asm 180187227..180187236) -> err; FUN_1800ce590(0, 3, desc), both files closed. Returns err.
The builder's prologue (asm 180187850..180187f4f): FUN_18015d200(1, 9, 0x32, 1000, zonbuf) (the zone buffer local_9908: +0 kind 1, +4 1000
= the 0xa0-node pool's grow count, +0x18 its free list, +0x20 its block list, freed by FUN_1800ce4b0 at the end); projz seeked to
DAT_1804e930c (= zone_off), dztmp to 0; p7 = 0 -> own direction A, other B (p7 1 swaps; else ctx +0xc20 = -0x10); ctx +0x30a = (p5 == 1);
FUN_1801b9bd0 only when ctx +0x20 is 0xe / 0xf (0 on op 43); FUN_1801bc5c0 on A and (p6) B; n = 2 x (A + B records) + 10, p9 clamped by it;
FUN_1801b3000(n, 1, desc, ..) when desc +0xc < n (three work arrays n x 1 / 0x18 / 0x30, FUN_1800cdfa0 mode 1) else FUN_1801a8380;
FUN_180159fa0 only when sf3 is open (never from FUN_180186ff0); the zonbuf_ctrl block (local_9a18..: files, positions, tol, DAT_1804e9250,
d); FUN_1801be460(7, L); FUN_1801c0320 on A and (p6) B. Then the zone loop LAB_180187ef0 (callees as listed in 12.23).

**The DLL oracle (harness D15_ZB, tools/peloader/mcmsurf_lmm.c zb_run)**: after D15_FULL + D15_6E60 (the records as FUN_18015c8f0 leaves
them) the harness repeats FUN_180186ff0's body and runs the DLL's FUN_180187850 whole. Before it, FUN_1801b1ad0's setup that the earlier
harness stages skip and the builder's tree reads (l.288849, l.288975-288977): the DLL's FUN_18015d4f0(100, &DAT_1804e95b0); FUN_18016a2a0(ctx,
100) (asm 18016a2a0..18016a3d5) repeated on harness memory (N13-31: ctx +0x30 descriptors; nA x 0x20 -> ctx +0x128 with ctx +0x130 = 0x100;
100 x 0x48 -> ctx +0x160, ctx +0x15c = 100; (max(nA, nB) / 2) x 0x10 -> ctx +0x170, ctx +0x16c; ctx +0x1f8 = 100; ctx +0x1f0 = 0x1900 bytes);
the DLL's FUN_180171db0(ctx); FUN_1801588f0(1, &DAT_1804e95b0) repeated (seven arrays at +0x10..+0x40 of 0x18 / 0x18 / 2 / 1 / 8 / 8 (c + 5) /
3 bytes x c, descriptors at +0x48; asm 1801588f0..180158a40 with FUN_1800ce140 / FUN_1800ce260 / FUN_1800cdc30 / FUN_1800ce1d0 /
FUN_1801584f0). Without this setup op 2's tmpprojz.tmp came out different. FUN_1801b3000's body (descriptor k = n x {1, 0x18, 0x30}) is
repeated, so the DLL takes FUN_1801a8380; FUN_1800cde60 (a pool block: calloc + pl2MemBufRemainAdjust, then FUN_1800cdfa0) and FUN_1800ce4b0
(the block list freed) are replaced by their bodies on harness memory (patch_jmp h_cde60 / h_ce4b0; with mode 1 = calloc the memory is as
Mastercam's, only the pl2 bookkeeping is not done). Every other pl2 / edge import of mcmsurf (IAT 0x1803e6850 / 6858 / 6860 / 7060 / 7068 /
7368 / 7388) is trapped: a call ends the run with "ZTRAP name caller-chain" (no pl2 call is answered: N13-22 / N13-23 / N13-31). D15_ZBCOV =
a list of function addresses: one-shot int3 at each entry (as mcce_coff.c), "ZV addr" in the order of first arrival. Prints "ZG", "ZB ret err
zone_off n4 DAT_1804e930c", "ZF name size hash", then the records (QH / QR, with their zone fields).
- **Op 2** (CURVE 5 AXIS REVIEW FILE): 1 zone, err 0, tmpprojz.tmp 13,932 bytes, tmpdztmp.tmp 3,796 bytes, no trap; 112 functions run (104 of
  the closure, 10,583 instructions).
- **Synthetic** (em_synth mix 1-30 x 2 (p6, p7), mixrun 101-115): 75 / 75 complete (36 err 0, 39 err -13 = the stall limit p9; 1..14 zones);
  before h_cde60, 68 of them stopped at the zonbuf pool's first grow (FUN_1801bca00 -> FUN_18017daa0 -> FUN_18017d330 -> FUN_18017e750 ->
  FUN_18017e5a0 -> FUN_18017e370 -> FUN_18017da50 -> FUN_1801460d0 -> FUN_1800cde60). Union: 209 functions / 22,141 instructions run; 120 /
  11,829 never (among them FUN_1801b9bd0, FUN_180159fa0, FUN_1801b3000's allocator).
- **Both files hold in-process addresses**: tmpprojz.tmp differs between two runs of the same input at its bytes 16..17 and at +0x2d..+0x31
  of every 0x30-byte step (a heap address of a 0xb0 record), so they must be compared by structure (pointer -> record identity), as 12.23
  says for tmpdzdat. Per zone both files start with **FUN_1801a95e0**'s 12-byte header (asm 1801a95e0..1801a9767: zbc +0x7c int count,
  +0x80 short = zbc +0x1a (the pass), +0x82 short = zbc +0x1c, +0x84 short (written elsewhere or stale: 0 on op 2, 16756 / 20 on a synthetic
  run - NOT DECODED), +0x86 byte 1; written at the zone's start position zbc +0x78 / +0x98 of SysFile zbc +0x70 / +0x90 when it changed,
  then the file is put back at its end; a write that is not 12 bytes: FUN_180268e20(1) into zbc +0x22). tmpprojz.tmp: count x 0x30-byte
  entries (+0 int, +4 four bytes never written, +8 / +0x10 / +0x18 doubles, +0x20 the address of a 0xb0 record, +0x28 int, +0x2c int not
  always written); tmpdztmp.tmp: count x 8-byte record addresses. The harness prints them by structure ("ZH file zone count pass +0x82 +0x84
  +0x86", "ZQ zone k +0 x y z dir line j +0x28", "ZD zone k dir line j": each address as its record's place in the lists after the builder);
  deterministic over repeated runs (op 2: one zone of 290 / 473; a synthetic run: four zones), every address found among the records.
  FUN_1801a9460 (asm 1801a9460..1801a94aa): a zone of fewer than 4 entries, or whose box |+0x58 - +0x50| x |+0x48 - +0x40| is below
  DAT_1804e9238 x (the double at 0x1803f04d8), gives -1.
- **FUN_180162dd0** (D15_ZB=2): its first call FUN_1801469b0 (asm 180162e40) allocates through FUN_1800cdfa0 -> pl2MemBufRemainAdjust (ZTRAP at
  1800ce058 via 180146a53): refused; FUN_1801469b0's body must be repeated first (closure 183 functions / 16,600 instructions, FUN_1801796d0
  181 / 16,322). Arming the coverage on FUN_180162dd0's closure crashed the run (an address in that list is not a function entry: to check).

**Port plan** (bottom-up; each function checked against the DLL's on the D15 state (harness D15_ZU), then the whole builder against D15_ZB by
structure). "run" = functions of the stage run by some input (op 2 / synthetic).
| stage | functions | size (ins) | run |
|---|---|---|---|
| Z0 prologue (DONE) | FUN_1801bc5c0, FUN_1801c0320 (FUN_1801bec50, FUN_1801c1c40, FUN_1801c3490 / 3470 / 34b0), FUN_1801c32f0, FUN_1801beff0, FUN_1801be460 / FUN_1801bf0d0 / FUN_1801bd8a0 | 560 | all |
| Z1 zone buffer, files | FUN_18015d200, FUN_1801ab410 + FUN_1801aa460 (per zone: positions; tmpzone header +4 = pass), FUN_1801a95e0 (zone header), FUN_1801a9460, FUN_1801be2f0, FUN_1801adec0, FUN_18018b6e0, FUN_18022d010, FUN_1801c3380, FUN_1801bfd90, FUN_1801aa5e0 (21 fn / 1,823) | ~2,300 | most on op 2 |
| Z2 shared core | 44 functions in all three closures of FUN_1801bca00, FUN_180195fc0, FUN_1801bff10: roots FUN_18016eb80 (22 / 883), FUN_1801aaea0 (14 / 1,361), FUN_18015a830 (7 / 273), FUN_18016e860 (6 / 285), FUN_18015c3e0 (5 / 397), FUN_180236cb0, FUN_1801864b0, FUN_1801c24f0 | 3,082 | most |
| Z3 FUN_1801bca00 | own 762; closure inside FUN_180195fc0's and FUN_1801bff10's; FUN_18017daa0 (353 own) -> FUN_18017d330 / FUN_18017e750 / FUN_18017e5a0 / FUN_18017e370 / FUN_18017da50 (the zonbuf 0xa0 pool) | 11,559 | 120 / 135 fn |
| Z4 FUN_180195fc0 | -> FUN_1801806b0 (recursive: FUN_1801803f0, FUN_1801a7d60, FUN_1801ad550, FUN_1801bca00); FUN_1801a7d10 / FUN_1801a7d60; exclusive 7 fn / 711 beyond Z2 / Z3 | 13,663 with Z3 | 139 / 159 fn |
| Z5 FUN_1801bff10 | own 264; exclusive 112 fn / 13,411 (FUN_1801b6100 1,161 own, FUN_180176810, FUN_1801be8a0, FUN_18016d250) | 17,886 | 78 / 173 fn |
| Z6 start search | FUN_180182b70 (21 / 1,670), FUN_18018b180 (22 / 1,843), FUN_1801bc960 | ~3,800 | synthetic |
| Z7 walk helpers | FUN_1801c2b40 (31 / 2,880), FUN_1801c0670, FUN_1801c0a00, FUN_1801c1150, FUN_1801c0dc0, FUN_1801c1dd0, FUN_1801aade0, FUN_1801c2320, FUN_1801bf2b0, FUN_1801bf100, FUN_1801bd790, FUN_1801b49b0, FUN_1801b8d00, FUN_1801c1b50 / 1b80 / 2b10 / 2950 | ~9,000 overlapping | mostly synthetic |
| Z8 the body | FUN_180187850 own 1,955 + FUN_180186ff0; whole against D15_ZB (files by structure, records) | ~2,000 | all |
| Z9 FUN_180162dd0 | 183 fn / 16,600 (FUN_1801796d0); FUN_1801469b0's body first, in the harness | 16,600 | not run yet |
Then D6 (FUN_1801c16d0 / FUN_1801597b0 / FUN_180192330 on tmpzone / tmpdzdat / tmpdzmap; FUN_180189a90 runs the same builder per pass).

**Z0 ported** (viewer/cam3d_cscallop_zone.c, from the asm; the decompile drops arguments of FUN_1801be460's and FUN_1801c0320's calls):
cs_zb_count = FUN_1801bc5c0 (per line n10 = records, n12 = n10; record +0x20, +0x30 dword, +0x10 zeroed); cs_zb_ends = FUN_1801c0320 (the
sense from the first record of lines 1..n-2 with |+0xa0 - +0xa8| > 1e-12; a record no neighbouring line covers (FUN_1801bec50) gets both
ends -4; a gap to its list neighbour wider than DAT_1804e9250 that both neighbouring lines cover (FUN_1801c1c40) gets the facing ends -5;
FUN_1801c34b0: both ends coded -> +0x20 = 1, n12 - 1, count + 1); cs_zb_clear = FUN_1801c32f0; cs_zb_clear_stall = FUN_1801beff0 (also the
codes c with L[-c] == 1, FUN_1801bd8a0); cs_zb_stall_init = FUN_1801be460 (L[0] = 7, L[1..3] = L[6] = 1, L[4] = L[5] = 0, L[7] unwritten).
**Oracle** (d15_cmp.sh with D15_FULL=1 D15_6E60=1 D15_ZU=3: the DLL's own functions on the harness records - FUN_1801bc5c0 A / B, FUN_1801be460,
FUN_1801c0320 A / B in the builder's order, then as test inputs FUN_1801beff0(L, -4) on A, FUN_1801c32f0(-5) on B, L[4] = 1 and FUN_1801beff0
(L, -5) / (L, 7); "ZU tag dir ret cnt b20 ends-4 ends-5 sum-n12", then QH / QR): op 2 identical (nothing coded: every record is covered);
em_synth mix seeds 1-30 x 3 (p6, p7) and mixrun 101-115 x 3: **135 / 135 identical** (1,215 ZU lines; 27,038 records with their zone
fields; 526 records closed by -4 ends, 987 ends -5).

### 12.37 Where this stopped (2026-11-14, session 16)
Done: the whole-builder DLL oracle (D15_ZB) with call coverage, the D5 plan (12.36), Z0 ported and DLL-identical. FUN_180186ff0 is mapped
(12.36), not ported: it is the builder plus FUN_180162dd0. Next, in order:
1. Z1: FUN_18015d200 / FUN_1801ab410 / FUN_1801aa460, FUN_1801a95e0 / FUN_1801a9460 and the entry writers; the harness already prints the zone
   files by structure (ZH / ZQ / ZD, 12.36): the port writes the same lines and d15_cmp.sh compares them (+0x84 of the header masked until
   decoded).
2. Z2 (the 44 shared functions) as unit checks on the D15 state, then Z3 FUN_1801bca00 (the zonbuf pool: the port's own list; the DLL's grow
   = FUN_1801460d0's block of 1000 nodes linked in address order, the last to the old free head), Z4, Z6, Z7, Z5, Z8.
3. FUN_180162dd0: repeat FUN_1801469b0's body in the harness (D15_ZB=2); find the coverage-list entry that breaks the int3 arming.
4. D6, then synthetic cases for FUN_1801af0f0 cases 0 / 2 / 3 / 6, FUN_180149a20 results 6 / 7, FUN_180150930 (task item 4: not started).
Op 43 stays greyed.


### 12.38 Stage D5 / Z1: the zone buffer and the zone files (2026-11-14, session 17) - PORTED, = DLL
Port: viewer/cam3d_cscallop_zbuf.c (CsZbc = the builder's zonbuf_ctrl local_9a18 at rbp+0x1e0, CsZFile = a SysFile on memory). Sources:
decomp/mcmsurf_all.c (l.N) and the objdump (asm ADDR); the builder's setup of the buffer is asm 180187d85..180187e8f (the decompile's
local_9a18..local_9948 = zbc +0..+0xd0; +0xd8 / +0xe0 come from FUN_1801a8380's rdx / r8 = descriptors 0 (n x 1) / 1 (n x 0x18), +0xd0
descriptor 2 (n x 0x30), asm 180187e60..180187e75).
- **The buffer** (zbc): +0x18 zone number (1 at the start: FUN_180159fa0's count + 1, 0 without the third file), +0x1a pass, +0x1c side,
  +0x1e tmpprojz.tmp open, +0x1f DAT_1804e8ffd (read only by the draw branch; 0 in every run), +0x20 draw mode (param_8 = 0 from
  FUN_180186ff0), +0x22 error, +0x28 param_10 (DAT_1804e9240), +0x30 mccore tol, +0x38 DAT_1804e9250, +0x40..+0x68 the box, +0x70 / +0x90
  the files, +0x78 / +0x98 the zone's start positions, +0x7c / +0x9c the 12-byte headers (int count, short pass, short side, short
  **+0x84 / +0xa4 never written** (stack: NOT DECODED, the port writes 0), byte 1, byte never written), +0x88 / +0xa8 entries written,
  +0xac zone steps, +0xb0 capacity n = 2 x records + 10, +0xb4 step counter, +0xb8 / +0xbc the range not yet written, +0xc0 steps,
  +0xc4 / +0xc8 last step per direction, +0xd0 steps (0x30: +0 int = +0xb4, +4 the caller's bytes (stack), +8 / +0x10 / +0x18 point,
  +0x20 record, +0x28 direction, +0x29 (FUN_1801a7d10: FUN_180197440 / FUN_180185d30, Z4), +0x2a end, +0x2c short), +0xd8 flags,
  +0xe0 work points, +0xe8 / +0xf0 the zone's first record and end.
- **FUN_1801ab410**(zone, pass, side, zbc) (l.284295): headers' counts 0, pass -1; FUN_1801aa460 (l.283614: +0xb8 = 0, +0xbc = -1, +0xc0
  = 0, +0xac = 0, +0xc4 / +0xc8 = -1, +0xe8 = 0, +0xf0 = -1); the same zone number: both files back to the zone's start, else the start =
  the files' positions. **FUN_1801a7d60**(step, zbc) (l.281697): the step appended (+0 = +0xb4), the zone's first sets +0xe8 / +0xf0 and
  the box, later ones grow it (min / max as the decompile: an unordered value replaces the minimum); at the capacity FUN_1801aa5e0 then
  **FUN_1801aea00**(10, ..) (the last 10 steps to the front; the last-step indices are taken from the destination slots before the copy).
- **FUN_1801aa5e0**(zbc) (l.283712, asm 1801aa5e0..1801aa868): points to +0xe0, **FUN_1801c71b0**(n, pts, +0x28, flags) (asm 1801c71b0:
  windows of 100, a point flagged while the chord from the base stays within +0x28: **FUN_1801c6ae0**, asm 1801c6ae0: max(|w x u|, 2 x
  (|w| - |chord|)), 2.0 at 0x1803eb830); the draw mode (+0x20 > 0: FUN_180197e40 / FUN_1801d7ca0) is NOT PORTED (refused; never on the
  builder's path); with +0x1e: side 0 -> FUN_1801ad550; **FUN_1801c7060** (tmpdztmp.tmp: the header when the file is at the zone's start,
  then the records of the range's steps with a record and end < 2) and, without an error, **FUN_1801c9be0** (tmpprojz.tmp: the box (x, y)
  over max(+0xb8, 1)..+0xc0, steps closer than +0x30 to the last kept one flagged; header when at the zone's start and (count or +0x88)
  >= 1; the range's unflagged steps, 0x30 bytes each).
- **FUN_1801ad550**(n, steps, ctx) (asm 1801ad550): the zone's side from consecutive steps with a record and end <= 1, **FUN_180166550**
  (asm 180166550..18016692f; the decompile garbles the 3-vector indexing): per step **FUN_18015c3e0**(rec, end, dir = +0x28, ctx, tol x 4.0,
  out) (asm 18015c3e0: the first inner point farther than d from the end's s, else the other end) -> P[dir] = the line position, P[1 - dir]
  = out s, P[2] = out z; UnitNormal(FUN_180236cb0(0.05, d), FUN_180236cb0(0.05, P - step)) with z dropped: 0 -> sign of its z, 3 ->
  mutl_v_angle_3d below 1e-6 / within 1e-6 of pi. The builder (asm 180188726) and FUN_1801806b0 (asm 180180807) store it in +0x1c;
  FUN_1801806b0 also clears +0x1c (asm 18018096f, r15w = 0) and calls FUN_1801aa460 (asm 180180974) after a trial.
- **FUN_1801a95e0** (l.283002): a header that differs from (+0x88 / +0xa8, +0x1a, +0x1c) is rewritten at the zone's start, the file put
  back. **FUN_1801a9460** (asm 1801a9460): +0x88, -1 below 4 entries or a box |dy| x |dx| below DAT_1804e9238 x 0.1. **FUN_1801adec0**
  (l.286195): the zone dropped (both files back to its start). A dropped or rewritten zone leaves its bytes behind the files' last zone
  (in Mastercam heap addresses): only the builder's returned count of zones is meaningful.
- The files: tmpprojz.tmp per zone the header then count x 0x30-byte steps; tmpdztmp.tmp the header then count x 8-byte records. Mastercam
  writes the record's heap address at step +0x20 / dztmp entries; the port writes (direction + 1) << 32 | index (cs_zf_rec). The step's +4
  / +0x2e bytes (the caller's stack) are 0 in the port.
**Oracle**: harness D15_ZBLOG (tools/peloader/mcmsurf_lmm.c zh_arm / zh_trap): repeating int3 hooks (original byte back, one instruction
single-stepped with TF, int3 re-armed) at the entries of FUN_1801ab410, FUN_1801aa460, FUN_1801a7d60, FUN_1801aa5e0, FUN_1801a95e0,
FUN_1801a9460, FUN_1801adec0, FUN_1801ad550 in the DLL's own builder: "ZC name return-address args" and "ZZ" = the buffer as the call finds it
(fields that Mastercam leaves uninitialised are left out: +0x84 / +0xa4, +0x86 / +0xa6 before the first header, the box before the
first step, the first FUN_1801ab410 / FUN_1801aa460). viewer/cfb_port --d15 with D15_ZREP replays every top-level call on the port's buffer
(the calls inside them - FUN_1801aa5e0 from FUN_1801a7d60 asm 1801a7ee6, FUN_1801ad550 from FUN_1801aa5e0 asm 1801aa809, FUN_1801aa460 from
FUN_1801ab410 asm 1801ab472 - are the port's own and print the same lines); then both files by structure for the builder's zones and the
end position of tmpprojz.tmp (DAT_1804e930c). tools/tests/zbuf_cmp.sh WORK EMDIR P6 P7 OUT. **CURVE 5 AXIS op 2 identical (487 calls, 1 zone,
763 file entries); em_synth mix 1-30 x 3 (p6, p7) + mixrun 101-115 x 3: 135 / 135 identical; in all 380,226 calls, 340,602 buffer states,
1,783 zones, 76,650 file entries.** Not exercised by any input: the buffer full (FUN_1801aea00 and a flush inside FUN_1801a7d60: no zone
reached 2 x records + 10 steps), write errors, the draw mode (refused).

### 12.39 Stage D5 / Z2 (part 1): the walks' shared core (2026-11-14, session 17) - PORTED, = DLL
The 44 functions in all three closures of FUN_1801bca00 / FUN_180195fc0 / FUN_1801bff10 (3,082 instructions; coverage on op 2 + 5 synthetic
runs: 36 run; never: FUN_1800cdfa0 (allocator), FUN_180149630 / FUN_18015a860 (the ctx +0x309 arm), FUN_18017c1c0, FUN_1801ac200 / 2e0 / 540
(the windowed point arrays, N13-28), FUN_180239180). Already ported and reused: FUN_1801864b0 (cs_9a20_pt), FUN_18015f990 / FUN_18015fba0
(cs_9a20_lines), FUN_1801c65e0, FUN_1801ac3c0, FUN_18015c3e0 / FUN_180236cb0 / FUN_18002fbd0 / mutl_v_angle_3d (12.38), FUN_1801bd8a0 (Z0).
Ported now (viewer/cam3d_cscallop_zcore.c, from the asm):
- **FUN_1801c24f0**(zbc, end, rec): +0xac > 1 and the zone starts at (rec, end). **FUN_18015a830**(ctx): 1 when ctx +0x309 = 0 (op 43);
  its +0x309 arm stays NOT PORTED.
- **FUN_1801716d0**(ctx, P, lines, cell) (asm 1801716d0 -> FUN_180176c90 = FUN_18015f990 with the four line slots, FUN_1801716b0: cell =
  the lower line of each pair as ushorts).
- **FUN_18016e860**(P, v, ctx, lines[2], cells[2]) (asm 18016e860..18016e9f0): N = UnitNormal((0, 0, 1), v) (error -> 0), h = |stepA
  unless stepB is not above it| x 0.5; P + N h and P - N h each looked up (FUN_1801716d0) into the next free slot; returns how many.
- The **walk point** (0x48 bytes): +0 record, +8 its header array, +0x10 end, +0x18 point, +0x30 the cell's four lines (A low / high, B
  low / high), +0x40 corner, +0x41 its code. **FUN_18016eb80**(ctx, d, W1, W2, out) (asm 18016eb80..18016ec55): both ends < 2 on lines of
  different kinds within 2 x 1.5 d (FUN_18016cdb0 -> FUN_18016ca20, 1.5 at 0x180405548): FUN_18017c1c0 (NOT PORTED, never reached:
  refused); else **FUN_180179f30** (asm 180179f30..18017a1d9: near -> the midpoint's cell; else A + u 1.5 d and B - u 1.5 d in one cell;
  then A and B inside the cell widened by 2e-12 (0x180413ab8), **FUN_180179e00** with **FUN_18016ce00** corners 0 / 2); W2's lines = W1's;
  **FUN_18017b790** (asm 18017b790: the corner of the record's line - kind != 0: lines[2] -> 0, lines[3] -> 2; kind 0: lines[1] -> 1,
  lines[0] -> 3 - and +0x41 = 02 01 03 04 05 06 08 07 [corner x 2 + end]) for both; **FUN_18017c490** (asm 18017c490: *out = 100, the
  position of W2's code in row W1's code of DAT_1804d4728 = 8 6 4 / 3 5 7 / 2 8 6 / 5 7 1 / 4 2 8 / 7 1 3 / 6 4 2 / 1 3 5, read from the
  DLL's .data, no writer in the code).
**Oracle**: harness D15_ZBUNIT: an int3 at each entry ("ZI name return-address inputs", walk points field by field, records as list places)
and one at the return address, per call frame ("ZO name outputs": the point, the lines / cells found, both walk points' lines and codes,
the out byte). viewer/cfb_port D15_ZUNIT makes every logged call on the port's functions; tools/tests/zcore_cmp.sh. **op 2 + the 135
synthetic runs identical: 2,305,097 calls (FUN_1801864b0 1,772,334, FUN_1801c24f0 399,400, FUN_18016eb80 104,264 (31,393 returning 1),
FUN_18016e860 27,687 (0 / 1 / 2 found: 0 / 1,612 / 26,075), FUN_18015a830 1,412).**
Left of Z2: **FUN_1801aaea0** (326 own) with FUN_1801ab730 (225), FUN_1801aba60 (127), FUN_1801aeb30 (114), FUN_1801aed20 (71),
FUN_1801b6000 (79), FUN_1801c2a10 (80), FUN_1801bfbd0 (40), FUN_1802537e0 (29): a 2D segment against the records of the lines it
spans (l.284106; FUN_1801b6000 builds the segment, FUN_18015f990 / FUN_1801c65e0 its lines, FUN_1801aed20 per record) - reached on 5 of
6 sampled runs; not started.

**FUN_1801aaea0's leaves** (also in cam3d_cscallop_zcore.c, same oracle): **FUN_1801b6000**(mode, A, B, widen, w, out) (asm 1801b6000..1801b60f7:
the (x, y) box of A and B, widened when asked so each side is at least w - mode 1: w - dx on each x side, (w - dy) x 0.5 on each y side;
other modes the halves the other way), **FUN_1801aed20**(up, hdrs, rec, box) (asm 1801aed20..1801aedf2: 0 when the record's line lies strictly
inside the box and its s range reaches the box's other side, else 1), **FUN_1802537e0**(box, P) (the outcode with 1e-12: 1 / 2 | 4 / 8).
zcore_cmp.sh over op 2 + the 135 runs: **18,792,119 calls, all identical except 1,078 FUN_1801aed20 calls (6 runs, all from FUN_1801aaea0 asm
1801ab1ca) on a "record" that is in no list** - the harness prints it as "? address", the port refuses (NOT PORTED); the DLL returns 1 for all
1,078. Most likely the header-n read of 12.34 (FUN_1801c65e0 lets a line index reach n, so FUN_1801aaea0 walks header n = record 0's
memory): to settle when FUN_1801aaea0 itself is ported (the port models that memory with CsFree, 12.34). Per function: FUN_1802537e0
13,707,640, FUN_1801aed20 2,491,707 (19,722 return 0), FUN_1801b6000 287,675.

### 12.40 Where this stopped (2026-11-14, session 17)
Done and DLL-identical: Z0 (12.36), Z1 (12.38), Z2 part 1 (12.39). Not done: the rest of Z2 (FUN_1801aaea0's tree, 12.39), Z3 FUN_1801bca00,
Z4 FUN_180195fc0, Z5, Z6, Z7, Z8, Z9; the builder-body record helpers of the Z1 list that act on records / builder locals rather than the
buffer (FUN_1801be2f0, FUN_1801c3380, FUN_1801bfd90, FUN_18018b6e0, FUN_18022d010) go with Z8. Next, in order:
1. FUN_1801aaea0's tree from the asm (its leaves FUN_1801b6000 / FUN_1801aed20 / FUN_1802537e0 are done; left FUN_1801aaea0 326,
   FUN_1801ab730 225, FUN_1801aba60 127, FUN_1801aeb30 114, FUN_1801c2a10 80, FUN_1801bfbd0 40; and the 1,078 off-list records of 12.39), checked by adding it to D15_ZBUNIT (inputs: the two points, the direction, the mode; outputs: the
   result and its out values).
2. Z3 FUN_1801bca00 (762 own; its exclusive part FUN_18017daa0 -> the zonbuf 0xa0 pool FUN_18017d330 / FUN_18017e750 / FUN_18017e5a0 /
   FUN_18017e370 / FUN_18017da50), checked the same way per function and then by the builder's log (D15_ZBLOG + D15_ZBUNIT).
3. Z4, Z6, Z7, Z5, Z8; FUN_180162dd0 (12.37 item 3); D6.
The two harness logs are general: a function is added to D15_ZBUNIT by its entry address, an input printer (zu_in) and an output printer
(zu_out); the port's replay goes into cfb_port's zunit_port. Op 43 stays greyed.


### 12.41 Stage D5 / Z2 complete: FUN_1801aaea0's tree, the 1,078 off-list records settled (2026-11-14, session 18) - PORTED, = DLL
Port: viewer/cam3d_cscallop_zcore.c (from the asm; the decompile hides a dead block and garbles the argument slots). Records are (direction,
index); a pointer the DLL follows to a freed record is followed in the port as Mastercam's memory holds it (below).
- **FUN_1801aaea0**(mode, w, dir, A, B, ctx) (asm 1801aaea0..1801ab3fe; callers FUN_1801aade0, FUN_1801bca00 asm 1801bd16c / 1801bd1a0 ...):
  does the segment A -> B (x, y) cross a strip between two neighbouring lines of direction dir (0: A, else B; n = ctx +0x338 / +0x3f8,
  headers +0x388 / +0x448, step +0x348 / +0x408)? box = FUN_1801b6000(dir, A, B, 0, 0.0); the lines of its two corners (FUN_18015fba0 =
  jmp FUN_18015f990, else FUN_1801c65e0, which lets an index reach n), the A lines for dir 0, else the B lines; from the lowest to the highest
  index, or the other way (the highest first unless B - A - x for dir 0, y for 1, z for 2 - and the step are both > 0 or both < 0; inc = +1
  when the last index is above the first, else -1), every record of each line's list: FUN_1801ab730(A..B, 2 x DAT_1804e9238, R, inc, n,
  headers) >= 0 -> 1; none -> 0. The block per record at asm 1801ab1ca..1801ab38f (FUN_1801aed20, mccore l_ints, two counters kept in the
  argument homes +0xb8 / +0xc8, xmm6 = |s1 - s0| x w when mode) writes only locals that are never read again: it has no effect, so mode and
  w (which feed only it) do not change the result. The FUN_1801aed20 calls are still logged and checked (12.39).
- **FUN_1801ab730**(seg, w2, R, inc, n, hdrs) (asm 1801ab730..1801aba54): Q = FUN_1801c2a10(up = R +0x80 > +0x70, R, R's line + inc, n,
  hdrs); none -> -2; box = FUN_1801aeb30(w2, R, Q, hdrs, box); 0 -> -2; then seg (a copy) clipped against the box in at most 4 rounds:
  outcodes c0 / c1 (FUN_1802537e0); c0 & c1 -> -1; both 0 -> 1 when an earlier round moved an end, else 0; else the first side 1, 2, 4, 8
  that an end lies beyond and that FUN_1801aba60 cuts (a side it cannot cut is passed): the cut's parameter along the side u in (0, 1.0)
  (0x1803eb820) -> 1, else the end beyond it (c0's when it has the bit, else c1's) moves to the cut point; 4 rounds -> -2.
- **FUN_1801c2a10**(up, R, line, n, hdrs) (asm 1801c2a10..1801c2b02): line <= -1 or >= n -> NULL; along the line's list (up: from +0 by
  +0, else from +8 by +8) Q with R's start above Q's end is passed, Q's start above R's end -> NULL, else FUN_1801bfbd0(1, up, R, Q) -> Q.
  **FUN_1801bfbd0**(a, up, R, Q) (asm 1801bfbd0..1801bfc79): the s ranges (+0x70 / +0x80 when a, else +0xa0 / +0xa8) overlap.
- **FUN_1801aeb30**(w, R, Q, hdrs, box) (asm 1801aeb30..1801aecb9): the box between R's line and Q's over the common part of their s
  ranges (R's kind k = header +0x14: positions to y when k == 1 else x, s to x when k == 0 else y; lower position = R's unless Q's is not
  above it, ...), each side moved in by w; 0 when a side is not longer than 2w.
- **FUN_1801aba60**(code, box, seg, I, &u, &t) (asm 1801aba60..1801abc34): the segment against the box side of the outcode bit (1 x =
  box[0], 2 x = box[2], 4 y = box[1], 8 y = box[3]): 0 when the segment's extent across it is below 1e-12 (0x1803eb800); t along seg (6th
  argument), I the point, u along the side (5th argument). Other codes leave the side unset (asm 1801abb2e); never called so (refused).
- **The 1,078 off-list records of 12.39 settled**: they are records FUN_1801a9c80 freed, reached through header n (12.34): FUN_1801c65e0
  gives index n, header n is record 0's memory of the direction's array, its +0 the next that memory holds (freed: the next it had, sn).
  zb_ident now names a record in no list by its memory: "? F dir slot" (slot of FUN_1801b1ad0's record array) or "? P k" (the k-th record
  of the FUN_1801b86c0 pool, which the harness links in address order, asm 1801b8785..); the port keeps the same identity: CsFree n0[] (the
  original records), pool[] (the k-th record taken with an empty free list), and per freed record rf / rud / rui = the port record that
  FUN_1801b8870 later gave its memory to (cs_zc_res follows a pointer to that). All 1,078 calls: freed original records of direction A
  (F 0) and 76 of the pool (P); 6 runs; FUN_1801aed20 returns 1 on all, and FUN_1801ab730 / FUN_1801c2a10 on them give the DLL's results.
**Oracle**: FUN_1801aaea0, FUN_1801ab730, FUN_1801c2a10, FUN_1801aeb30, FUN_1801aba60, FUN_1801bfbd0 added to D15_ZBUNIT (inputs / outputs,
records by list place or memory); cfb_port's replay now prints each output where the log has its "ZO" (calls nest). tools/tests/zcore_cmp.sh
over op 2 + em_synth mix 1-30 x 3 (p6, p7) + mixrun 101-115 x 3: **136 / 136 identical, 28,105,469 calls, nothing refused** (FUN_1801aaea0
240,410 (19,680 return 1), FUN_1801ab730 2,491,707 (-1 1,713,079, -2 758,948, 1 19,680; 0 never), FUN_1801c2a10 2,688,221, FUN_1801aeb30
1,742,655, FUN_1801aba60 34,818 (all 1), FUN_1801bfbd0 2,115,539, FUN_1801aed20 2,491,707 incl. the 1,078, FUN_1802537e0 13,707,640).
Z2 (the 44 functions of 12.36) is complete; not reached by any input and refused: FUN_18017c1c0, the ctx +0x309 arm (FUN_180149630 /
FUN_18015a860), the windowed point arrays FUN_1801ac200 / 2e0 / 540 (N13-28), FUN_180239180.

### 12.42 Stage D5 / Z3 start: FUN_1801bca00's candidate gatherer FUN_1801bf450 and leaves (2026-11-14, session 18) - PORTED, = DLL
FUN_1801bca00(p1 both directions, p2, p3 (pool), mode, i, zbc, p7, factor, hdrs[2], .., out) (l.295983; asm 1801bca00): the walk from step i
of the zone buffer zbc (+8 ctx, +0x10 the 0xa0-node zonbuf pool, +0xd0 steps, +0xc4 / +0xc8 last steps, +0xf2 the stall list, +0x100 error).
Its candidate / walk entry is 0xa8 bytes (+0 byte, +2 short index (FUN_1801bca00), +0x20, +0x30 point, +0x48 point - walk point, +0x60 end
code, +0x68 distance, +0x70 / +0x78 angles, +0x80 / +0x88, +0x90 record, +0x98 its headers, +0xa0 direction, +0xa2 end). Ported now
(viewer/cam3d_cscallop_zwalk.c, from the asm):
- **FUN_1801bed30**(i, zbc) (asm 1801bed30..1801beebe): the step whose direction step i continues: dA / dB = (x, y) distances (sqrt(dy^2 +
  dx^2)) from step i to the last step of each direction (0 without one); both above mccore tol: the last A step when dB > dA else the last
  B step; one above: that one; none: step i - 1 when its x or y differs from step i's by more than tol, else -1.
- **FUN_1801ab6d0**(L, R, end, mode) (asm 1801ab6d0..1801ab72d): the end's code (+0x30 / +0x32) > 0 -> 0, 0 -> 2, < 0 -> 3 when mode 2 or
  (mode 1 and FUN_1801bd8a0(code, L)), else 1.
- **FUN_1801aef30**(hdrs, R, box, out) (asm 1801aef30..1801af0e7): R's ends as points (line position / s at the slots of the line's kind,
  z = +0x78 / +0x88) inside the box (outcode 0): 0 end 0 only, 1 end 1 only, 2 both, -1 none.
- **FUN_1801c5ef0**(side, a, b) (asm 1801c5ef0..1801c5f90): mutl_v_angle_3d (FUN_18025ce50) turned to 0..2 pi: UnitNormal(a, b) fine -> z > 0
  gives +1 else -1, equal to side -> the angle, else 2 pi - angle (0x1803eb848); error 3 with the angle >= pi / 2 (0x1803eb828) -> the angle;
  other errors -> 2 pi.
- **FUN_1801bf450**(L, mode, side, W, v, box, zr, ctx, dir, hdrs, max, &cnt, arr) (asm 1801bf450..1801bfbcf; 13 arguments, the decompile
  shows 4): the record ends of direction dir inside the box nearest the walk point W into arr (at most max = 0x32 from FUN_1801bca00). The
  lines of the box's corners (FUN_1801c9520, as cs_d15 box_ad80) lowest to highest index whose position lies strictly inside the box's span
  (header n included: record 0's memory, +0x18 as the position, +0 as the first record); per record FUN_1801aef30; per end inside with z
  strictly inside zr and other than W's own (+0x90 / +0xa2): d = |end - W| (3D); d < mccore tol: both angles 1 degree (0x1803f4070), else
  a1 = mutl_v_angle_3d(unit v, unit (end - W) in x, y), a2 = FUN_1801c5ef0(side, ..). Kept while cnt < max (the farthest slot tracked:
  strictly farther, or the first), else it replaces the farthest when strictly nearer - the farthest then looked up again among the old
  values (the new entry is written after, asm 1801bf98a..1801bf9d6). Returns the number added.
**The end codes change during the walk** (the builder writes +0x30 / +0x32): the harness now prints "ZK record c30 c32" before a Z3 call that
reads them for every listed record whose codes changed since the last scan (all at the first; zk_scan), and the replay sets them on the
port's records (state injection until Z8 ports the writers). **Oracle**: FUN_1801bed30 (step points as inputs), FUN_1801ab6d0 (stall list),
FUN_1801aef30, FUN_1801c5ef0 (xmm0 at the return), FUN_1801bf450 (the input entries and the output entries in full) added to D15_ZBUNIT;
zcore_cmp.sh over op 2 + the 135 synthetic runs: **136 / 136 identical, 36,653,019 calls** (FUN_1801bed30 270,850, FUN_1801ab6d0 2,266,594,
FUN_1801aef30 4,678,892, FUN_1801c5ef0 886,378, FUN_1801bf450 444,836 - adding 0..9 entries, header n reached on em_synth mix 7). Not
exercised: FUN_1801bf450's full array (at most 11 of the 50 entries were ever used, so the replace-the-farthest branch never ran).
- **FUN_18016fb60**(zbc, W, max, &cnt, arr, &nc) (asm 18016fb60..18016fee0; FUN_1801bca00 calls it with max 0x3c): the grid cell of each
  candidate. id0 = FUN_180176200(ctx) + 1 = (nB - 1) x (nA - 1) + 1; per entry of the count on entry (+8 / +0x28 cleared) FUN_18017a2c0(W,
  entry, ctx, zbc +0x30 = mccore tol): 0 -> +4 = ++id0 and +0xc..+0x1b = -1; else +4 = FUN_18017c1a0 (= (nA - 1) x cell[1] + cell[0]) with
  the cell's four lines, the cell noted once by FUN_180147900 (at most 10; more: FUN_180268e20(3)); 2 while the count < max - 1: a copy of
  the entry appended with the second cell. Then per noted cell a copy of W with that cell, at count + k - these are not counted: *cnt =
  the entries, *nc = the cells (FUN_1801bca00 adds them). Returns the entries that got a cell. **FUN_18017a2c0**(W, C, ctx, d, lines, cells)
  (asm 18017a2c0..18017a3fc): FUN_180179f30(ctx, d, W point, C point) (12.39) -> 0 when not one cell; same direction (+0xa0) and the same
  line (records' +0x24): FUN_18016e860(W + (C - W) x 0.5, C - W, ..) -> 0..2 cells beside; else 1. **FUN_18017c2e0**(code) (asm 18017c2e0;
  the jump table at 0x18017c310 / 0x18017c318 read from the DLL): 1 for 4..22 but 8, 13, 18.
  The candidate entry is now logged in full (+2 / +4 / +8 / +0xc..+0x1b / +0x28 too); FUN_1801bf450's output entries only with the fields
  it writes (a new slot keeps stale stack bytes elsewhere). Op 2 + em_synth mix 7 x 3 identical (op 2: 472 FUN_18016fb60, 1,352
  FUN_18017a2c0 calls); the sweep: 12.43.
- **FUN_18015bf40**(ctx, w, n, arr) (asm 18015bf40..18015c2da; FUN_1801bca00 l.296517 with w = |ctx +0x318 - ctx +0x310| x 0.1, at least
  9.999999999999999e-05): the candidate to go on with (-1 none; n > 0x3c: FUN_180268e20(3)). t = ctx +0x2b8 x 0.1 (0x1803f04d8) unless
  not above mccore tol. List A: entries with +0x2c > 20 and (+0x80 < ctx +0x320 + 1e-6 (0x1803f3ed8) or |+0x58| < ctx +0x330); m = their
  least +0x80 (from 2 pi); list B: of those, +0x80 < 9.999999999999999e-06 (0x180414408) or |+0x58| < t. No A: entries with +0x2c > 20 and
  +0x88 < ctx +0x320 + 1e-6 (m = least +0x88); none -> -1. B not empty: its first entry with the largest +0x2c. Else over the list: r =
  +0x2c / best when best > 0 and +0x2c > best, else 1.0; |+0x80 (list A) or +0x88 - m| / r < w and +0x2c > best -> that entry. ctx +0x320 /
  +0x330 / +0x310 / +0x318 are logged as inputs (ctx +0x330 = max(FUN_18015c8f0, DAT_1804d56c0), 12.34; +0x310..+0x320 come from
  FUN_1801b1ad0's frame: NOT DECODED here, needed by Z8). The entry's +0x2c short is now in the log. **No input reaches it** (its only
  call, asm 1801bd4b8, ran 0 times over op 2 + the 135 synthetic runs): ported from the asm, NOT CHECKED against the DLL yet.

### 12.43 Where this stopped (2026-11-14, session 18)
Done and DLL-identical: Z2 complete (12.41); Z3 functions FUN_1801bed30, FUN_1801ab6d0, FUN_1801aef30, FUN_1801c5ef0, FUN_1801bf450,
FUN_18016fb60 (+ FUN_18017a2c0 / FUN_180147900 / FUN_180176200 / FUN_18017c1a0), FUN_18017c2e0 (12.42); FUN_18015bf40 ported but not
reached by any input. **Final sweep (zcore_cmp.sh, op 2 + em_synth mix 1-30 x 3 + mixrun 101-115 x 3): 136 / 136 identical, 37,760,572
calls** (FUN_18016fb60 221,175 (186,848 with cells), FUN_18017a2c0 886,378, FUN_1801bf450 444,836, FUN_18015bf40 0). Note: the
coverage lists of session 16 (scratch zcov*.txt) are the armed closures, not the functions run - check "run" with the unit log's counts.
Next, in order:
1. The rest of Z3, bottom-up with the same unit log: FUN_18015c2e0 (82) -> FUN_1801bfdc0 (92) -> FUN_18015a1c0 (80) -> FUN_1801695c0 (162;
   leaves FUN_18015c3e0 / FUN_180236cb0 / mutl_v_angle_3d are ported): writes the candidates' +0x2c (5 / 10 / 300); FUN_18016a7b0 (221,
   closure 1,744: FUN_18016b610 / FUN_18016b4b0 / FUN_18014a950 / FUN_1801565a0 (ported as cs_9a20), FUN_18016e620 / FUN_18016c130 /
   FUN_18016d5c0 / FUN_18016c2c0 / FUN_180177910, FUN_180175dc0, FUN_18017c7c0); FUN_180180090 (212, closure 2,710: FUN_1801be340,
   FUN_1801c2780, FUN_1801bd790, FUN_18017b8e0); FUN_180179090 (218, closure 1,362); FUN_18017d8d0 / FUN_18017d8a0 (the pool's reset); the
   zonbuf 0xa0-node pool FUN_18017daa0 (353) -> FUN_18017d330 / FUN_18017e750 / FUN_18017e5a0 / FUN_18017e370 / FUN_18017da50 (closure
   4,829; the DLL's grow FUN_1801460d0 = a block of 1000 nodes on harness memory, h_cde60); then FUN_1801bca00's own 762 instructions,
   checked whole (its inputs: the zone buffer, the records' codes "ZK", the pool).
2. Z4 FUN_180195fc0 (-> FUN_1801806b0 recursive, FUN_1801803f0, FUN_1801a7d10), Z6 / Z7, Z5, Z8 (the builder body; its writers of the
   records' +0x30 / +0x32 replace the "ZK" injection), Z9 FUN_180162dd0, D6.
Op 43 stays greyed.

### 12.44 Stage D5 / Z3 continued: the side test, the cell runs FUN_18016a7b0 / FUN_180179090, the score FUN_180180090 (2026-11-14, session 19) - PORTED, = DLL
Port: viewer/cam3d_cscallop_zwalk.c (+ cs_zb_95c0 in _zbuf.c, cs_9a20_ent in _9a20.c, cs_zb_c1c40 / cs_zb_bec50 exported from _zone.c),
from the asm. In FUN_1801bca00 (12.42) these run after FUN_18016fb60 (asm 1801bcfaf..1801bd320).
- **The side test**: **FUN_1801695c0**(ctx, flag, S0, S1, &t) (asm 1801695c0..18016987d) = the per-step body of FUN_180166550 (12.38) for
  one step (flag: S0, else S1); **FUN_18015a1c0**(side, flag, S0, S1, ctx) (asm 18015a1c0..18015a2a1): 0 without side / records / with an
  end above 1; s != 0 -> 1 same sign as side, else 2; s == 0 -> 3 (t = 1), 4 (t = -1), else 0; **FUN_1801bfdc0**(zbc, W, C) (asm
  1801bfdc0..1801bff04): two 0x30 steps made of W and C (+0x30 point, +0x90 record, +0xa0 direction byte, +0xa2 end), FUN_18015a1c0 with
  flag 0 then (0 / 3 / 4) flag 1; 1 / 2 kept, 3 / 4 the second time -> 1, else FUN_180268e20(3) and 0. **FUN_18015c2e0**(zbc, W, n, idx,
  arr) (asm 18015c2e0..18015c3d0): +0x2c = 5 (side 0) / 10 (side 2) / kept (1) for the listed entries, then the one with +0x2c == 300 and
  the largest +0x70 above 0.0.
- **FUN_18016a7b0**(zbc, W, n, ls, &cnt, out) (asm 18016a7b0..18016ab23, only when FUN_18016fb60 gave two or more cells): the n entries
  sorted by cell (+4, FUN_18017b570 insertion sort); per run of two or more in one cell (FUN_1801711f0): the corner of each entry's record
  line in the cell (FUN_18017c7c0: +0x1c corner, +0x1d = 02 01 03 04 05 06 08 07 [corner x 2 + end]); not exactly one entry with +0
  set (FUN_180170a40): FUN_180268e20(3) and W appended with the run's cell / lines; +0x20 (a double here) = the distance along the cell's
  sides from corner 0 (FUN_180175dc0: |x or y - the corner line's position| + {0, |stepA|, |stepB| + |stepA|, |stepA| + that}), sorted
  by it (FUN_18017b4f0); FUN_18016b610 marks with +0x60 = 4 the entries on a corner whose neighbour corner holds them (FUN_18016b4b0:
  inside an s range of an entry there, or with no entry there FUN_18014a950 on its own record) and FUN_18016ac50 drops them; again exactly
  one with +0 set, or FUN_180268e20(3) and return; +0x29 / +0x2a = the codes with the next / previous entry (FUN_18016c130 / FUN_18016d5c0:
  the 9 x 9 tables DAT_1804d4670 / DAT_1804d46d0 of .data, row +0x1d, column the neighbour's), +0x28 = FUN_18016e620(1) (1 replaced by
  the other side's code when that is of class 3 / 8 / 13 / 18; classes FUN_18016e540, its jump table at 0x18016e574: 4..7 -> 3, 9..12 ->
  8, 14..17 -> 13, 19..22 -> 18); the distinct +0x28 of class 8 / 13 / 18 counted; a run of two with one: FUN_18016c2c0 (the neighbour
  with +0x60 2 / 3 and the matching code gets +8 = 1, +0x2c = 300) -> its +2 into out. Returns the count. The tables were read from the
  DLL (rd of 0x1804d4610 / 4670 / 46d0, 81 bytes each; only lea references, no writer).
- **FUN_180179090**(zbc, 0x1e, W, n, ls) (asm 180179090..180179405, when FUN_1801bca00's p2 and two or more cells): side 0 -> 0; the same
  runs; W appended as above (its +0x20 alone by FUN_180175dc0); sorted by +0x20; the distinct +0x28 of class 8 / 13 / 18 (b) and of
  class 3 (c); b + c > 1: f = FUN_18016e170(ctx, 2 x ctx +0x2f0) when b > 1 or b + c > 2 (two entries with code 1 / 2 whose next one
  is more than that apart in z; FUN_18015a830's ctx +0x309 arm never: op 43) -> FUN_18016ab30 (the neighbour on the side +
  zbc +0x1c ...: +8 = 2, +0x2c = 120 (f2 and |dz| > 2 x ctx +0x2f0) / 140) and, c == 0, b > 1, f == 2, FUN_18016c1a0 (+0x2c = 150);
  returns the largest. **ctx +0x2f0 = DAT_1804d5680 = FUN_1801b1ad0's 9th argument (asm 1801b1e07) = msurf_colaps_dist (12.23)**; the
  harness did not set it (0.0 from .data) - now set in zb_run as FUN_1801b1ad0 does.
- **FUN_180180090**(p1, zbc, p3, dir, i, W, rec, end, d, k, arr) (asm 180180090..1801803e9; FUN_1801bca00 asm 1801bd239 for the
  candidate's own direction (p3 1), 1801bd29f the other (p3 0); rec / end = the zone step's record of that direction, d = |its step|):
  E = arr[k]; FUN_1801bfdc0 0 -> 5, 2 -> 10, other -> FUN_180268e20(3), 0; i < 10 and E starts the zone (FUN_1801c24f0) -> 20; E +0x70
  below pi / 2: p1 and FUN_1801be340 (FUN_18016eb80 on the walk points of W and E, d = mccore tol) -> E +8 = 1, {220, 210, 200}[out],
  else 30; else p1 and FUN_1801be340 -> {250, 240, 230}[out]; no rec -> 40; E on rec's line, another record, end != p8:
  FUN_1801c2780 (the gap to rec's neighbour on that end covered by line + 1 or line - 1) -> p3 ? 110 : 90; E's record = rec and p3:
  FUN_1801bd790 != 0 -> 110, FUN_18017b8e0 (rec's start and end, the end on the neighbouring line, in one cell: FUN_180179f30) -> 70;
  else end == p8 and (lines one apart or |W - E| (x, y) < d) and FUN_1801bfbd0 -> p3 ? 80 : 70, else 40; the fall-through: E +0x70
  above pi / 2 -> 40, else 30. An out byte above 2 would read a stack word past the tables (refused; never seen).
**Oracle**: FUN_1801bfdc0, FUN_18015c2e0, FUN_18016a7b0, FUN_180179090, FUN_180180090 added to D15_ZBUNIT (entries in full; for the two
run functions also +0x1c / +0x1d / +0x29 / +0x2a, inputs and outputs of W and every listed entry; ctx +0x2f0 / +0x309 / the zone
buffer fields as inputs); zcore_cmp.sh over op 2 + the 135 synthetic runs: **136 / 136 identical, 39,499,422 calls, none refused**
(FUN_1801bfdc0 288,352, FUN_18015c2e0 124,593 (117,528 picking an entry), FUN_18016a7b0 149,051 (count 0 / 1 / 2: 24,458 / 117,426 /
7,167), FUN_180179090 8,540 (0: 7,858, 140: 682), FUN_180180090 156,592 (5: 26, 10: 49,215, 20: 165, 30: 33,128, 40: 22,633, 70: 1,099,
80: 13,422, 90: 87, 110: 4,393, 200-250: 32,424)). The ctx +0x2f0 fix changed the DLL's own runs (m7 p6/p7 set 1: 518,912 -> 511,712
calls). Not exercised: FUN_180180090's error 0, FUN_180179090's 120 / 150 (FUN_18016ab30 with f2, FUN_18016c1a0).
- **FUN_18015bf40 cannot be reached on op 43**: its only call (asm 1801bd4b8) is behind `cmp BYTE PTR [rcx+0x309], 0; je 1801bd54b`
  (asm 1801bd471..1801bd478), and ctx +0x309 (= FUN_18024d390(cur_op, ..), 12.34) is 0 for op 43 (non-zero only for ops 0x28..0x2a). A
  synthetic op-43 case for it does not exist (task item 3); it stays ported but unchecked, to be checked with the 0x28..0x2a ops.
- FUN_1801bf450's replace-the-farthest branch: still not exercised (the most entries in any run: 11 of 50); it needs more than 50 record
  ends inside the box of about one step around the walk point, i.e. many records shorter than a step on the neighbouring lines - not
  built this session.

### 12.45 Where this stopped (2026-11-14, session 19)
Done and DLL-identical (12.44): FUN_18015c2e0 / FUN_1801bfdc0 / FUN_18015a1c0 / FUN_1801695c0, FUN_18016a7b0 and its tree, FUN_180179090
and its tree, FUN_180180090 with FUN_1801be340 / FUN_1801c2780 / FUN_1801bd790 / FUN_18017b8e0. Left of Z3, in order:
1. FUN_18017d8d0 / FUN_18017d8a0 (the pool reset: the used block list back onto the free list, zonbuf +0x2a = 0; 16 + 22 instructions).
2. The zonbuf pool FUN_18017daa0 (353 own; closure ~4,800: FUN_180175ef0, FUN_18017c8a0 (174, -> FUN_1801863e0 / FUN_18016e860),
   FUN_180175f40 (-> FUN_180154fb0 / FUN_180154ea0), FUN_18017cb60, FUN_18017cc10 (170, -> FUN_18017eb50), FUN_18017d330 (121) ->
   FUN_18017d4c0 / FUN_18017d090 / FUN_18017d610 / FUN_18017ce10 / FUN_18017d950 / FUN_18017e5a0 -> FUN_18017e370 (-> FUN_18017da50 ->
   FUN_1801460d0, the 1000-node grow on harness memory h_cde60), FUN_18017ebb0). It copies the candidates (0xa8 each) into a 0x3c-entry
   local array (l.? asm 18017daa0..) and reruns the cell runs (FUN_18016b610 / FUN_18016e620 already ported).
3. FUN_1801bca00 itself (762 instructions, asm dumped 1801bca00..1801bd790), checked whole on its inputs (zone buffer, records' codes "ZK",
   the pool) and outputs (the chosen entry's record / end / point, +0x70 angle, the out byte).
4. Z4 FUN_180195fc0, Z6 / Z7, Z5, Z8 (ctx +0x310 / +0x318 / +0x320 still NOT DECODED: FUN_1801b1ad0's frame), Z9, D6. Op 43 stays greyed.

### 12.46 Stage D5 / Z3: the zonbuf pool FUN_18017d8d0 and the cell triangulation FUN_18017daa0 (2026-11-14, session 20) - PORTED, = DLL
Port: viewer/cam3d_cscallop_zwalk.c (cs_zw_pool_*, cs_zw_tri), from the asm (objdump of mcmsurf.dll; the decompile's FUN_18017daa0 is
readable, its callees' stack arguments are not). FUN_1801bca00 calls FUN_18017d8d0 at asm 1801bcebd and FUN_18017daa0 at asm 1801bd027,
both only when its p3 is set; the pool is used by nothing else (its only taker FUN_18017da50 is called only from FUN_18017e370).
- **The pool** (the builder's local_9908 = zbc +0x10; FUN_18015d200(1, 9, 0x32, 1000) at asm 180187a02 (asm 18015d200): +0 kind 1, +4 1000 nodes
  per grow, +8 / +0x10 the used list of 0xa0-byte edge nodes (head / tail, kept sorted by +0x48), +0x18 the free list, +0x20 the block
  list of FUN_1800cde60, +0x28 = 9 blocks at most, +0x2a blocks in use, +0x2c the current block (never initialised by FUN_18015d200; every
  FUN_18017daa0 follows a FUN_18017d8d0), nine 0xfb8-byte blocks at +0x30 (+0 the cell id (-1), +4 the cell, +8 cap 0x32, +0xa points used,
  +0x10 0x32 points of 0x18, +0x4c0 triangles used, +0x4c8 0x32 triangles of 0x38: +8 three point slots, +0x20 three edge nodes), the
  vertex list at +0x8db0 (0x28 each: +0 kind, +8 point slot, +0x10 the entry, +0x18 corner, +0x20 z; cap +0x8da8 = 0x32, count +0x8daa).
  **FUN_18017d8d0** (asm 18017d8d0..18017d8e8) = **FUN_18017d8a0** (asm 18017d8a0..18017d8c4: a non-empty used list goes in front of the free
  list, head / tail cleared) + one dword 0 at +0x2a (+0x2a and +0x2c). **FUN_18017da50** (asm 18017da50) pops the free list, growing it by
  FUN_1801460d0 (asm 1801460d0: +4 nodes of FUN_1800cde60, linked in address order, in front). A node is rewritten whole when taken
  (FUN_18017e2d0), so the port keeps only the count: free = all nodes grown - used.
- **FUN_18017daa0**(W, n, arr, zbc, mode, &out) (asm 18017daa0..18017e062): *out = -1; W copied and put in its cell (**FUN_180175ef0**, asm
  180175ef0: FUN_1801716d0, +4 = FUN_18017c1a0, FUN_18017c7c0's result; 0 -> -16); the n entries copied, those whose +4 is not below
  FUN_180176200 (asm 180176200: (nB - 1) x (nA - 1), the ids FUN_18016fb60 gave entries without a cell) put in theirs the same way; the
  cells beside each copy and W (**FUN_18017c8a0**, asm 18017c8a0..18017cb54: FUN_18016e860 at the entry's point along the record's end 1 -
  end 0 by **FUN_1801863e0** (asm 1801863e0: the line position at the slot of the header's kind, the other slot and z from +0x60 / +0x68 or
  +0x90 / +0x98); a cell already held by an entry of the same record / direction / end gives that entry its lines, else a copy of the entry
  with the cell is appended while fewer than 0x3c); sorted by cell (FUN_18017b570); per run of two or more in one cell (FUN_1801711f0): the
  corners (FUN_18017c7c0; one 0 -> -16), **FUN_180175f40** (asm 180175f40..1801760aa: +0x20 (a double) = the place of the entry's point
  among the ends of its corner's line, **FUN_180154fb0** (asm 180154fb0..180155135: along the line from its last record down (corners 0 / 3,
  end 1) or its first up (1 / 2, end 0), the ends equal to the point (pt_equal); one -> its place, two or none -> **FUN_180154ea0**, asm
  180154ea0..180154fa3: the nearest by FUN_180239180 = (dy^2 + dx^2) + dz^2) + {0, 10000, 20000, 30000}[corner] (0x180414480); a corner above
  3 or an empty line stops it, the later entries keeping their +0x20), sorted by it (FUN_18017b4f0); the block (**FUN_18017cb60**, asm
  18017cb60..18017cc0a: the block of the cell's id, else a new one while fewer than 9; none -> -16) into +0x2c; FUN_18016b610 marking any ->
  return 0; per entry +0x28 = FUN_18016e620(1); the first entry of each new code of class 8 / 13 / 18 (b) or 3 (c) listed as (code, index)
  over W's copy; b == 1 and c == 0, or more than one listed and **FUN_18017cc10** 0 (asm 18017cc10..18017cdb8, jump table 0x18017cdc4: the
  code's cell sides (4..7 one side 0..3; 9 / 19 {0, 3}, 10 / 20 {0, 1}, 11 / 21 {1, 2}, 12 / 22 {2, 3}, 14 / 16 {1, 3}, 15 / 17 {0, 2})
  against the previous (cyclic) and, more than two, the next code by **FUN_18017eb50** (asm 18017eb50: bit masks 0x1a8610 / 0x314c20 /
  0x629840 / 0x495280)) -> **FUN_18017d330** (asm 18017d330..18017d46e, jump table 0x18017d470): +0x8daa = 0; per listed code its vertices
  - **FUN_18017d4c0** (codes 4..7: the record ends of codes 1 / 3 / 5 / 7 and the next), **FUN_18017d090** (9..12: a = 7 / 1 / 3 / 5, b =
  2 / 4 / 6 / 8 both present (FUN_1801727f0), a's end, corner 0..3 at z = (z(a) + z(b)) x 0.5, b's end), **FUN_18017d610** (14..17) and
  **FUN_18017ce10** (19..22): z = (z(next cyclic) + z(this)) x 0.5, record ends of two codes around two / three corners looked up with the
  first code - each first checking the vertex room (count + k below 0x32) and the block's point room (used + k below 0x32), else -4
  (FUN_180268e20(2)); vertices by **FUN_18017eac0** (record end: kind 1, corner -1, z 999.99) / **FUN_18017ea30** (corner: kind 0) of the
  first entry with the code (**FUN_1801727c0**); none -> -16; then **FUN_18017e750** (asm 18017e750..18017e9e8): fewer than 3 vertices ->
  -16, the triangle room else -4; the fan (V0, Vs, Vs+1): per vertex the edge byte **FUN_18017d950** (asm 18017d950..18017da4a; two corners
  2 apart, two record ends of different corners, a record end not on the corner's sides) of (Vk, Vk + 1) - **note: the vertex index + 1,
  so every triangle's first edge byte is (V0, V1)'s** (asm 18017e899); **FUN_18017e5a0** (asm 18017e5a0..18017e743): record ends by
  **FUN_18017e9f0** (the entry's point into the slot, id (1, record address, end), pair (record, +0xa0, +0xa2 bytes)), corners by
  **FUN_18017e0e0** (FUN_18016ce00 of the entry's lines and the z; id (0, A line, B line)); **FUN_18017e370** (asm 18017e370..18017e593):
  per edge (0, 1), (1, 2), (2, 0) the key **FUN_18017e180** (asm 18017e180: kind, then x, then y, signed), the node of that key in the used
  list (**FUN_18017d8f0**, stopping at the first +0x48 above the key's) gets its second triangle (+0x98; already set -> -16), else a new
  node (**FUN_18017e210** / **FUN_18017e2d0**: the points and pairs in the key's order, +8 the edge byte, +0x90 the triangle) is put after
  the last node whose +0x48 is not above its own (**FUN_18017e0a0** / **FUN_18017e070**). Then **FUN_18017ebb0** (asm 18017ebb0..18017eccb):
  the first used node with one triangle between two record ends, one of them W's (record, +0xa0, +0xa2), the other's end code
  (FUN_1801ab6d0(zbc +0xf2, .., mode)) 2 or 3 -> the entry of arr[0..n) at that end gets +8 = 1 and *out = its index. Returns 0, -16 or -4.
- **Record addresses are compared** (FUN_18017e180's keys, so the node order and which open edge FUN_18017ebb0 finds first): in Mastercam
  the A records, then the B records (FUN_1801b1ad0's FUN_1800cdfa0 mode 2 = pl2StackAlloc, asm 1801b1de5 / 1801b2097 / 1801b228a), then the
  FUN_1801b86c0 pool (edge_alloc) all come from one bump allocator growing upwards: mccore's edge_alloc is 1805797c0 (base 0x1813fbeb8 +
  size - remaining) and pl2StackAlloc (1805903c0) calls it. The port ranks a record by the memory it sits in: (A 0 / B 1 / pool 2, slot;
  a later record in the memory it took, CsFree), above every line index. The harness's records are laid out the same way ("ZMEM 1 1" on
  every run: A below B below the pool).
**Oracle**: harness D15_ZBUNIT also logs FUN_18017d8d0 (the pool before / after: used, free, +0x2a, +0x2c, +0x8daa) and FUN_18017daa0
(inputs: mode, n, the stall list, the pool, W and the n entries in full; outputs: the result, *out, the entry's +8, the pool, every used
node in list order: both ends' identities, +8, a second triangle, the two points); the replay keeps one pool for the builder.
zcore_cmp.sh over op 2 + em_synth mix 1-30 x 3 + mixrun 101-115 x 3: **136 / 136 identical, 39,562,628 calls** (FUN_18017d8d0 50,840, FUN_18017daa0 12,366; on op 2 + mix 7 x 3: 321 FUN_18017daa0 calls - 135 return -16 (W or an entry has no cell or corner, FUN_180175ef0), 45 pick an entry (+8 = 1), the rest none; up to 26 used nodes listed, every node's ends, byte, second triangle and points identical); none refused..

### 12.47 Stage D5 / Z3 complete: FUN_1801bca00 checked whole (2026-11-14, session 20) - PORTED, = DLL
Port: cs_zw_walk in viewer/cam3d_cscallop_zwalk.c, from the asm (asm 1801bca00..1801bd789; the decompile drops and garbles its stack
arguments): FUN_1801bca00(p1 both directions, p2 cell runs, p3 triangulation, mode, i, zbc, p7 z window, p8 box factor, hdrs, &p10, &p11,
&p12, &p13, p14) -> the chosen entry's record (+0x90) or NULL.
- i below 1: FUN_180268e20(-16), NULL. The step i's direction byte (+0x28) above 1: zbc +0x100 = -16, NULL (asm 1801bcb13..1801bcb23).
  c = that direction, o = the other; |step c| / |step o| (ctx +0x348 / +0x408); the step's record / end for c, the last step of o (zbc
  +0xc4 / +0xc8; none: no record). FUN_1801bed30(i) below 0: NULL (p10 not written).
- W (the walk entry, 0xa8 zeroed): +0 = 1, +2 = -1, +0x30 = the step's point, +0x80 / +0x88 = pi (0x180415170), +0x90 / +0x98 / +0xa0 /
  +0xa2 = the step's record, hdrs[c], c, its end; v = step k (FUN_1801bed30) - the point; *p10 = 0.
- The box (asm 1801bcd1d..1801bcda2): dA = |stepA| x p8, dB = |stepB| x p8, one below DAT_1804e9250 takes the other's (A first); the point
  +- (dA, dB); the z window: p7 -> the point's z +- max(dA, dB), else -1e38 .. 1e38 (0x1803f4940). FUN_1801bf450(zbc +0xf2, mode, zbc +0x1c,
  W, v, box, zr, ctx, c, hdrs, 0x32, &cnt, arr) and, p1, the same for o; p3: FUN_18017d8d0. No candidate: NULL.
- FUN_18016fb60(zbc, W, 0x3c, &cnt, arr, &nc) = got; tot = cnt + nc (at most 0x3c); +2 = the index, the list of pointers. p2 and got > 1:
  FUN_18016a7b0(zbc, W, tot, ls, &k, out), k > 0: FUN_18015c2e0 -> above -1: that entry, done (asm 1801bcfe8..1801bcff5).
- Else p3: FUN_18017daa0(W, cnt, arr, zbc, mode, &out) == 0 with out in [0, cnt): the triangulation's choice (taken after a 150).
- The scores (asm 1801bd0a0..1801bd2da), per entry: +0x60 below 2 -> -1; +4 above FUN_180176200 and FUN_1801aaea0(0, 2 x DAT_1804e9238
  (0x1803eb830), c, point, entry) or (p1) the same for o -> -1; else for an entry of direction c / o: FUN_180180090(!(+0x28 and
  FUN_18017c2e0(+0x28)), zbc, 1 / 0, c / o, i, W, that direction's record / end, its |step|, k, arr); +0x2c = the score; si = the largest.
- Two or more positive: p2 and got > 1: si = max(si, FUN_180179090(zbc, 0x1e, W, tot, ls)); neighbours of score si on one record: the one
  with the larger +0x78 (|difference| at least 1e-6, 0x1803f3ed8), else the larger +0x68, gets -1. Then a 150 is taken at once; else among
  score si the largest +0x70 (strictly, first kept); with two or more and its +8 clear, an entry of score si in another cell with a
  smaller +0x68 than the current choice and +0x70 less than 0.26179938779914935 (0x180413af8) below the best's replaces it.
- The choice E (asm 1801bd620..1801bd778): *p12 / *p13 = +0xa0 / +0xa2, p14 = its point, *p11 = mutl_v_angle_3d(v, point - W's), *p10 =
  1 when +8 is clear, FUN_18017c2e0(+0x28) and FUN_1801be340(zbc, W, E), else +8; returns +0x90.
- ctx +0x309 (FUN_18015a720 / FUN_180168d20 / FUN_18015bf40): 0 on op 43 (12.44), refused.
**Oracle**: harness D15_ZBUNIT logs FUN_1801bca00 (inputs: p1 p2 p3 mode i p7 p8, hdrs, zbc +0x1c / +0xac / +0xe8 / +0xf0 / +0x30, ctx
+0x2f0 / +0x309, DAT_1804e9238 / DAT_1804e9250, zbc +0x100, +0xc4 / +0xc8, the stall list, the steps i, i - 1 and the two last; outputs:
the record, p10 p11 p12 p13 p14, zbc +0x100, the pool); the replay runs cs_zw_walk on its own pool (all pool use is below FUN_1801bca00)
besides replaying the nested calls. **zcore_cmp.sh: 120 / 120 runs identical (op 2, em_synth mix 1-30 x 3, mixrun 101-110 v1 / 110 v2: the sweep was stopped there when the session ended; mixrun 110 v3-115 not run with this build), 32,284,298 calls incl. 196,773 FUN_1801bca00 and 10,762 FUN_18017daa0; op 2 + mix 7 x 3: 4,272 FUN_1801bca00 (3,903 return a record), every output identical; none refused.** Harness fix: the pool's +0x2c is printed 0 until the first FUN_18017d8d0 (FUN_18015d200 leaves it as stack).

### 12.48 Where this stopped (2026-11-14, session 20)
Done and DLL-identical: Z3 complete - the pool and the triangulation (12.46), FUN_1801bca00 whole (12.47; its sweep reached 120 of the 136
runs, all identical - rerun the last 16 (mixrun 110 v3 .. 115) first next session).
Z4 FUN_180195fc0 was ported from the asm but NOT CHECKED and is NOT committed (session ended before its first run): FUN_180195fc0 (asm
180195fc0..180196297: every record end of code 2 (FUN_1801ab6d0 mode 0) -> FUN_1801806b0; above 2 or zbc +0x100 < 0 returns at once; the
last 2 kept and put back), FUN_1801806b0 (asm 1801806b0..18018099d: FUN_1801803f0's neighbour, W and it appended and coded (FUN_1801bfd90),
+0x1c = FUN_1801ad550, up to 4 FUN_1801bca00(1, 1, 0, 0, +0xbc, ..) steps with byte 1 / 2, then FUN_1801c32f0 for A and B, +0x1c = 0,
FUN_1801aa460), FUN_1801803f0 (asm 1801803f0..1801806a2) with FUN_1801c1ce0 / FUN_1801c1bd0 / FUN_1801baa60 (all read from the asm). The
draft (port + harness hook "5fc0" printing the buffer, steps and every changed code / +0x20 / line count against an entry snapshot, and
the replay) is kept in the session scratch (cf20/keepz4, z4.c, ed3.py) - to be re-applied and checked with zcore_cmp.sh.
Left: Z4 check, FUN_1801a7d10, Z5, Z6 / Z7, Z8 (ctx +0x310 / +0x318 / +0x320 NOT DECODED), Z9 FUN_180162dd0, D6. FUN_1801bf450's
replace-the-farthest branch still not exercised (the builder's box is 1.2 / 1.3 steps either side, p7 0; both directions share the 0x32
array; at most 9 ends seen in one call) - no synthetic case built. The ctx +0x309 arms stay refused (0 on op 43). Op 43 stays greyed.
