# Classic surface toolpaths, batch 2: Multisurface Pocket (op 131), Rough Plunge (op 44), Finish Contour (op 15) (2026-10-26)

Specs for porting three items of Mastercam 2017's 3D Mill gallery. They follow formats/surf_rough.md (cited "sr N"). Citation
key as there: **l.N** = `decomp/mcmsurf_all.c` line N; "op+X" = the operation struct offset; **record = op + 0x68** (the .mcam op
record, the raw bytes `tools/mcam_ops.py` returns). Defaults = the first op of the opcode in the shipped
`<shared Mcam2017>/mill/Ops/Mill_MM.mcam-defaults` / `Mill_Inch.mcam-defaults` (MM op ids 26 / 24 / 33, Inch 33 / 32 / 39). They were
read with the scratch scripts `fields.py` / `fieldsf.py` + `*.spec` in ~/mastercam-re/work/sc2/. Anything taken from data and
not from code is labelled **measured**. A gap is labelled **NOT DECODED**.

Gallery item -> command. This comes from the ribbon XML `~/mastercam-re/ui/ribbonuires/BCGP_RIBBON_XML/IDR_BCGP_RIBBON_XML`: each
3D gallery palette (ids 53952 Mill, 54138 Router, 53875 Lathe Milling, 54055 Mill-Turn) lists its items as `<ELEMENT>` buttons with
`<FT_FUNCTION>`.
- "Plunge" = msurf_plunge_toolbar (XML l.10236). It calls msurf_assoc(0x2c) (l.212126-212160).
- "Multisurface Pocket" = msurf_pocket_light_toolbar (XML l.10251). It calls `IsMillOpSupported(4, 0x83)`, then msurf_assoc(0x83, -1)
  (l.212198-212230).
- "Contour" with the description "Create constant Z cuts around the steep walls of your part." = msurf_contour_toolbar (XML
  l.10356-10360, button id 53888; the same at l.11637 / 13213 / 14736 in the other galleries). It calls msurf_assoc(0xf)
  (l.211598-211627).
- So **"Contour" is the classic Surface Finish Contour, op 15**. It is not the HST Waterline: Waterline is its own item,
  msurf_hmm_waterline_toolbar (XML l.10281).

## 1. Multisurface Pocket (op 131 = 0x83)

### 1.1 Verdict: the engine is op 10's
Every regeneration branch treats 0x83 exactly as 10:
- bnci_surface fzt / emm selection (l.11964-11996): emm follows msurf_use_emm_pocket for both.
- 3rd-party gate (l.12260-12262).
- msurf_map: `case 10: case 0x83:` share one block (l.340115-340206). The direction block is cleared for both (l.340049-340057).
  The short gap is pct x rough_zdist for both (l.340451).
- Stage 1: `case 10: case 0x83:` FUN_1802a14e0(1, ..) (l.197601-197605).
- Stage 2: FUN_1802a0710 (l.199483). The levels: adjust-for-stock op+0x13ca and keep-top (l.438263, l.438380).
- Auto-detect flats op+0x13ca / 0x13d9 (l.474318, l.493010).
- The NCI writer's tests (l.204438-206014). Top-of-stock (FUN_1802a1d90 l.438894-438908). The Surface parameters page 999
  (l.143586-143620, l.144220).
- The section names: `case 10: case 0x83: "[roughpock]"` (l.340580).

So the surf_rough sections 2.1-2.6 apply unchanged, and `gen_srough_pocket` is the port.

The differences are below.

### 1.2 Dialog: tab 4 is CMsPocPgLev2 (dialog 1312), not CMsPocPg (242)
- **The sheet** (CSurfaceSheet ctor FUN_1802b6550):
  - Op 10 (l.450233-450262) and op 0x83 (l.450487-450516) both add tab 3 = FUN_1800eca30 + FUN_1800f4b40 (CAlt2, Rough parameters
    1310, title string 0x79 = 121).
  - Op 10's tab 4 is FUN_1800ecb40 = `MCPropertyPage(0xf2 = 242)`, CMsPocPg (l.152785-152795).
  - Op 0x83's tab 4 is **FUN_1800ecd80 = `MCPropertyPage(0x520 = 1312)`, CMsPocPgLev2** (l.152855-152865).
  - Both tab 4s use title string 0x93 = 147 "Pocket parameters".
  - The two classes have the same member layout (ctors l.152785-152925). Their DDX functions are FUN_1800edf40 / FUN_1800ee110:
    formats/ui_fields_mcmsurf.tsv labels both as 1312; which one belongs to 242 is NOT DETERMINED.
- **The page functions come in pairs** (242 / 1312). Diffs are in ~/mastercam-re/work/sc2/: init242.c / init1312.c, ok242.c / ok1312.c.
  - Init: FUN_1800f08a0 l.154779 / **FUN_1800f1170 l.154972**.
  - OK: FUN_1800f21a0 l.155355 / **FUN_1800f2600 l.155456**.
  - Method list fill: FUN_1800ef0a0 / **FUN_1800ef410 l.154049**.
  - Enable logic: FUN_1800ed530 / FUN_1800ed9d0, FUN_1800eede0 / FUN_1800eef40, FUN_1800ee340 / FUN_1800ee890. These are identical
    apart from the list size.
- **What 1312 changes:**

| what | 242 (op 10) | 1312 (op 131) | source |
|---|---|---|---|
| on init, rpock_prm (record 0x1228 + off) forced | - | +0x252 finish passes = 1 (0x147a), +0x1f8 quick zigzag = 0 (0x1420), +0x42e thin wall = 0 (0x1656), +0x418 spring passes = 0 (0x1640), +0x251 minimise burial = 1 (0x1479), +0x230 rough = 1 (0x1458), +0x258 finish spacing = 0 (0x1480) | l.155015-155021 |
| cutting method list 741 | 8 rows (sr 7) | **2 rows**: row 0 = mcmsurfres 0x1b4 = 436 "Zigzag", row 1 = 0x1ba = 442 "One Way" (l.154135-154171) | FUN_1800ef410 |
| row -> record 0x145c (rpock +0x234) | row with 4 <-> 7 swapped | row 0 -> 0, **row 1 -> 6** (l.155495-155499) | FUN_1800f2600 |
| record 0x145c -> row | value with 4 <-> 7 swapped | **6 -> row 1, else the value itself** (l.154176-154179) | FUN_1800ef410 |
| image list | bitmap 0x523, 8 images | bitmap 0xa3, 2 images (l.155023-155027) | FUN_1800f1170 |
| enable test "Zigzag-like" | method 6 or 0 | row < 2 (always) | FUN_1800ed9d0 vs FUN_1800ed530 (l.153139) |
| spindle override 729 -> rpock +0x428 (0x1650) | through Cnc::Tool::GetToolSpindleRotation, shown abs | stored as typed (l.155540) | l.155448-155450 vs l.155540 |
| AutoTips | InitializeAutoTips | not called | init diff |

- **The controls of 1312** (exports/mcmsurfres_dialogs.tsv) are the same ids as 242's, relaid. The record fields behind them are
  sr 4's dialog-242 table.
- **Op 131 = Zigzag / One Way only.** cam2d_pock_zz.c ports both (2D methods 0 / 6), so neither is refused by the
  gen_srough_pocket gate.

### 1.3 The non-3D-product forcing (bnci_surface)
- FUN_180012d60 (l.12788-12832) sets the flag local_6c7c = 1 when `product_level_ok_no_warn(6, ..)` fails, i.e. on a non-3D Mill
  product.
- With that flag, l.12485-12509 refuses every multi-surface op except 0xb and 0x83, with warning 0xf3 = 243 "Only 1 drive surface
  may be selected for non-3D Product".
- For 0x83 it forces, on the op:
  - op+0x1412 = 1 (record 0x147a passes);
  - stepover op+0x13f8 (0x1460) clamped to D op+0x2e8, and then op+0x1400 (0x1468) = 1.0;
  - op+0x13b8 (0x1420) = 0;
  - op+0x15ee (0x1656) = 0;
  - op+0x15d8 (0x1640) = 0;
  - op+0x1411 (0x1479) = 1;
  - op+0x13f0 (0x1458) = 1;
  - op+0x1418 (0x1480) = 0.
- So op 131 exists for the Mill 1/2 ("light") level.
- On a 3D product the flag is 0 and none of this runs. Which level the user's licence reports is NOT DETERMINED. The port should
  assume a 3D product (gate off), since this program is not product-gated.

### 1.4 Record and defaults: the shipped op 131 records use a shifted layout (measured)
- **Measured.** In both shipped files the op 131 record holds op 10's pocket-block values **8 bytes lower**.
  - Inch: every word from 0x1450 to 0x1648 equals op 10's at +8. Examples:
    - rough / finish / climb 1 / 1 / 1 at 0x1450 (op 10: 0x1458);
    - stepover 0.275 at 0x1458 (op 10: 0x1460);
    - 55 % at 0x1460 (op 10: 0x1468);
    - feed / spindle override 25.0 / 3000 at 0x1640 / 0x1648 (op 10: 0x1648 / 0x1650).
  - MM: the same pattern (stepover 15, 75 % at 0x1458 / 0x1460; feed / rpm 50 / 3500 at 0x1640 / 0x1648). It is overlaid at
    op-10 offsets with exactly the values the 1312 init forces (1.2):
    - rough byte 0x1458 = 1, written over the low byte of the shifted 15.0. That is why the double there reads
      15.000000000000002.
    - burial 0x1479 = 1, passes 0x147a = 1, spacing 0x1480 = 0.
  - The block 0x13c8-0x13d8 shows the same shift: 0x13ca = 1 and float 45.0 at 0x13d0, against op 10's 0x13d2 / 0x13d8.
  - 0x12e8-0x1318 (depths) are not shifted. So the shift starts in 0x1320-0x13c8, where both records are all zero.
  - Both records are 10552 bytes, with op version (record 0x1218) 20.0.
- **Code.** default_ops_get copies the file's op struct whole: it matches by opcode and copies (decomp/mccore_grpsettings.c
  l.4671-4720, inside default_ops_get @18004a600 l.4256). No per-opcode fix-up of 0x83 was found there.
- So Mastercam 2017 reads these bytes at the op-10 offsets of 1.1. For the Inch file that gives:
  - rough = 0x9a, finish = 0x99, climb = 0x99;
  - method = (short) 0x9999;
  - stepover 55, % 0.
  Whether lib_list / record I/O converts such a record is **NOT DECODED**.
- **Decision needed (user), before the default op can be wired:** (a) take the record as Mastercam's code reads it (op-10 offsets:
  garbage values in Inch); (b) take op 10's default record with opcode 131 and the 1.2 init forcing (ours); (c) un-shift the block
  by 8 (ours, from the measurement).
- The op 131 defaults at op-10 offsets, outside the shifted block (measured, MM / Inch):

| record | field | MM | Inch |
|---|---|---|---|
| 0x34c / 0x350 / 0x358 | tool type / D / r | 19 bull / 20 / 4 | 11 ball / 0.5 / 0.25 |
| 0x170 / 0x180 / 0x190 | clearance / retract / feed plane | 50 (off) / 25 / 5 | 2 (off) / 0.25 / 0.1 |
| 0x1240 / 0x1248 | total tolerance / max stepdown | 0.025 / 2 | 0.001 / 0.1 |
| 0x1288 / 0x1290 / 0x1298 / 0x12a0 | gap % on / dist / % / type | 1 / 2.5 / 3 / 1 | 1 / 0.01 / 3 / 11 |
| 0x12c8 | containment compensate (double) | 1 Center | 1 Center |
| 0x12e8 / 0x12e9 / 0x1300 / 0x1308 / 0x1310 / 0x1318 | depths inc / rel corner / top adj / bottom adj / min / max | 1 / 1 / 0.2 / 0 / -10 / 0 | 1 / 1 / 0.01 / 0 / -1 / 0 |
| 0x1408 / 0x1409 / 0x140a / 0x1434 / 0x1440 / 0x1441 | entry pt / plunge outside / bottom-top / align / keep top / auto flats | 0 | 0 |
| 0x161a / 0x1656 | facing / thin wall | 0 | 0 |

### 1.5 NOT DECODED (op 131)
- Everything sr 2.6 lists for op 10: run_mctutl_zcut, the containment boolean, links, the HS engines (unreachable: no High Speed
  row in 1312), quick zigzag (forced 0), facing, flats, thin wall (forced 0), spring passes (forced 0), align plunge entries, check
  surfaces.
- The record conversion of 1.4.

### 1.6 Oracle
- **None.** No op 131 exists in ~/mastercam-re/work/hst3d/netscan/scan.tsv (1460 internet parts), in
  ~/mastercam-re/work/hst3d/scan.tsv (local parts), or in any stored part checked.
- The op-10 oracle PARALLELSTEEP.mcx-9 op 4 (tools/tests/srough_oracle.sh) is a Zigzag op 10. It is the stand-in, because the
  engine is identical (1.1).

### 1.7 Port outline (wiring only) - DONE 2026-10-28 (branch toolpaths2)
**Done:** CAM_SRMPOCKET ("Multisurface Rough Pocket", mccoreres 4432; the Mill / Router 3D gallery's "Multisurface Pocket"; cam_cli
"srmpocket"). Default record (user decision T-1): the defaults' first op 131 (Mill_MM op 26 / Mill_Inch op 33) with the block from 0x13c8 to
0x1660 moved up by 8 (measured again 2026-10-28: the file's op 131 equals op 10 at +8 from 0x1300 to 0x165f in both files; 0x1300-0x131f
match only by coincidence of zeros, so the shift start is taken in the all-zero 0x1320-0x13c7 as 1.4 says), then the 1312 init forcing
(1.2), in viewer/cam_fileop.c cam_default_op_record (the dialog's record, cui_ops.c heights_from_default_op, starts from it too).
Generation: gen_srough_pocket unchanged. Dialog: Tool parameters, Surface parameters 999, Rough parameters 1310, Pocket parameters 1312
(opdlg_fields.tsv s1312 = the s242 rows, the method list Zigzag 0 / One Way 6). Not done: the spindle override "stored as typed" (no 729
row on 242 either). Scene gen_srmpocket (BRACKET4: 4652 points; MM defaults 75 % stepover, finish pass 1).

1. A type CAM_SRMPOCKET. `default_op` (viewer/cam_fileop.c l.665-668) gets opcode 131, under the 1.4 decision.
2. Gallery: in `cam_type_from_gallery` (viewer/cam2d.c l.818-833), "Multisurface Pocket" maps to CAM_SRMPOCKET (Mill / Router
   galleries, as Pocket).
3. Generation: `gen_srough_pocket` (viewer/cam3d_srough.c l.64) unchanged. Its refusals that are reachable from the 1312 page:
   none by default; facing / auto-detect flats / align plunge entries stay refusable from page 1310.
4. Dialog: tabs Tool parameters, Surface parameters 999, Rough parameters 1310 (as op 10), and **Pocket parameters 1312**:
   - The 242 rows with the 2-row method list (row 1 <-> value 6).
   - On page open, the init forcing of 1.2 (rough 1, passes 1, spacing 0, quick zigzag 0, thin wall 0, spring 0, burial 1) as record
     patches.
   - The spindle override stored as typed.
   - New opdlg_fields.tsv rows "s1312" are generated from FUN_1800f1170 / FUN_1800f2600.

## 2. Rough Plunge (op 44 = 0x2c)

### 2.1 Pipeline
- **bnci_surface** (l.11964-11968): fzt is cleared, because 0x2c is not 9/10/0xf/0x6b/0x83.
- emm: 0x2c is in the long test; at its end, emm = msurf_use_emm_plunge (l.12033-12036). That is 0 by default (read_appmch_cfg
  l.18497).
- So the model is build_machine_model, the offset-surface model of Parallel (sr 3.2) (FUN_18012a310 l.197312-197413). It is NOT
  DECODED (mccore make_surf_model).
- **msurf_map case 0x2c** (l.340302-340308):
  - msurf_slice_dist = op+0x1368 (**record 0x13d0**, max stepover);
  - the source kind DAT_1804d7260 = 2 (grid), or 0 when op+0x13b2 (**record 0x141a**) == 1 (NCI);
  - then LAB_180206ec4 (l.340383-340398): depths rough_depth_is_inc op+0x1280 (0x12e8), top / bottom / min / max op+0x1298..0x12b0
    (0x1300-0x1318), user depths FUN_1802a1b20(1), levels on DAT_1805034a2 = 1;
  - posz / negz / plunge control are **not** read (bVar3 = bVar4 = false, l.339980 / 340306). They keep the msurf_map preset: plunge control
    0, negz 1, posz 1 (l.339994-339996).
  - Common block (l.340009-340048): stock 0x2e0, chk 0x1230, tip comp 0x1239, msurf_chord_dev 0x1240, rough_zdist 0x1248,
    short gap 0x1288..0x12a2, entry r / a 0x12a8 / 0x12b0.
  - Max short gap = msurf_slice_dist x pct (l.340474). Stage 2 passes 0 for 0x2c (l.206333-206336).
- **Stage 1**, `case 0x2c` -> FUN_18013a370 (l.197518-197524; shared with Project 7 / 0xd):
  - Source kind 0 (NCI): the source op id = op+0x1380 (**record 0x13e8**). Its NCI goes to msproj.nci (msurf_to_nci_op,
    l.206626), then FUN_18012cf00. Then:
    - "Allow small steps" op+0x13c0 (**record 0x1428**) == 0 -> FUN_18012b940(step), with step = op+0x1368, or D/2 when <= tol, or
      a metric / inch constant when D <= tol. Only the low 32 bits are visible in the decompile: **NOT DETERMINED**.
    - else FUN_18012bf40 (l.206737-206762).
    - **NOT DECODED** (FUN_18012cf00 / FUN_18012b940 / FUN_18012bf40, l.197888-199179).
  - **Grid** (record 0x141a == 0): FUN_18012c430(frame u, frame v, msurf_slice_dist) (l.206733-206734). Then FUN_18025dd50 (the model
    boxes in the frame, l.206772; body l.399269) and **FUN_180290fb0(1, stock, tool type, r)** (the drop / projection,
    l.206776).
- **Stage 2** `case 0x2c` -> FUN_180139c80 (l.199450-199455; l.206254-206548):
  - max short gap forced 0 for 0x2c (l.206333-206336);
  - levels FUN_1802906c0 (2.3);
  - FUN_180293f40 (2.3);
  - FUN_180226c30 (the exact model);
  - FUN_180290fb0(2, ..) (gap projection);
  - FUN_180136670 (the NCI writer, sr 1.4; NOT DECODED).

### 2.2 The grid: FUN_18012c430 @18012c430 (l.198226-198454), decoded
1. **Corners.** Among the op's TP records (tp_get_all), the first two with record type +0x28 == 1 (point), each through
   msurf_process_pt, are the corners P1, P2 (l.198287-198312). Mastercam's plunge needs two picked points; no default exists in the
   code.
2. **Frame.** u = P . F1, v = P . F2 (l.198318-198325). FUN_18013a370 builds F1 / F2 from the active view's matrix with cos / sin(pi/2)
   (l.206674-206719; passed as `&local_2ab0`, `&local_2a90`). Read literally: e = -(view row 0), d = view row 1, and F1 = -e = view
   row 0, F2 = d = view row 1. For the Top view that is u = x, v = y. FUN_180266780(d, f, e) runs before F1 / F2 are taken
   (l.206719); its effect is NOT DECODED.
3. **Counts.** du = u2 - u1, dv = v2 - v1.
   - |du| >= tol: n = ceil(|du| / s); du /= n (signed, equal steps); nu = n + 1.
   - |du| < tol: nu = 1.
   - The same for v (l.198326-198347).
4. **Order (zigzag).** Rows j = 0 .. nv-1 at v = v1 + j dv. In row j the k-th point is u1 + k du, or, on every other row, u2 - k du.
   The flag starts "from P1" and flips after each row (l.198357-198415).
5. **Output.** Each point goes to msproj.tmp as a 0x38-byte record: start (u, v), end (u + 10 x msurf_chord_dev, v), +0x20 / +0x30
   = 0, +0x30 int -1, +0x34 short 0 (l.198354-198404). It is a short segment along u per point; its use by FUN_180290fb0 is NOT
   DECODED.

### 2.3 Levels and the per-level loop, decoded
- **FUN_1802906c0** @1802906c0 (l.428420-428483), called with (levels on, posz, negz, plunge control, inc, top, bottom, rough_zdist).
  FUN_180139c80 l.206340-206356:
  - absolute: top = max(min, max), bottom = min(min, max);
  - incremental: top / bottom = top adjustment / bottom adjustment.
- When absolute (l.428443-428470):
  - st = max(zdist, 2 x DAT_180513a60, tol, 1e-10). DAT_180513a60 is the first double of the msproj.tmp header written by
    FUN_180290fb0 (l.429601); its meaning is NOT DETERMINED.
  - R = top - bottom. When R > st: n = max(1, int(R / st)), +1 when R / n > st; step = R / n.
  - Else step = max(R, 2 DAT_180513a60, tol, 1e-10).
  - This is the same rule as op 5's FUN_1802022d0 (sr 8 "Levels"). sr_parallel already has it.
- **FUN_180293f40** @180293f40 (l.430485-430657):
  - It reads the msproj header, including DAT_180513a78 (the in-band tolerance) and DAT_180513a08 (a tool value; with absolute
    depths and tip comp it is added to top / bottom, l.430600-430605).
  - Incremental: FUN_180296cf0 resolves top / bottom from the projected data (l.430608). NOT DECODED.
  - Loop (l.430611-430635): L = top, first = 1, P = 0. While L >= bottom - 0.4 step: L = max(L, bottom); FUN_180143620; per level
    FUN_180289ff0(nci, feed, plunge); plunge control 2 runs it again (unreachable: control 0). Then P = L, first = 0, L -= step.
- **FUN_180289ff0** @180289ff0 (l.424618): the generic per-level writer of projected points. For the grid (DAT_1804d7260 == 2) it
  calls, per point:
  - **FUN_180296360** when not (op 0x2c and helix on op+0x1384 = **record 0x13ec** and helix radius op+0x1388 = **record 0x13f0**
    > tol);
  - else FUN_18028b7f0, the helix plunge, **NOT DECODED** (l.424972-424992: test l.424974, calls l.424983 / 424991).

### 2.4 Per plunge: FUN_180296360 @180296360 (l.431698-431968), decoded except the link geometry
The point comes from the projected edge list DAT_180513ab8 (0x28-byte entries). A point is skipped when its type > 0x11 or |a| >=
DAT_180513a78 (l.431764-431770). Its projected tool-tip Z is z.

1. **Skip and clamp** (levels on, l.431781-431787):
   - not first level and z >= L + step: no motion (already cut at the level above);
   - z < L: z = L.
2. **Top of the move** (0x2c only, l.431789-431804):
   - not first level: H = P (the previous level);
   - first level: H = max(L, z) + step.
   - With DAT_180513eef (top of stock: op+0x13b4 = **record 0x141c** via FUN_1802a1d90 l.438897 / 438912), H is raised to
     top-of-stock (op+0x280 = record 0x2e8) + corner r (op+0x2f0 = record 0x358).
3. **Feed plane** op+0x128 / op+0x130 (record 0x190 value / 0x198 incremental), l.431806-431825:
   - Absolute and fp > H: the approach goes to fp first, then a second move feeds down to z (bVar7).
   - The rate handling is via a temporarily zeroed incremental feed plane when the move rate equals the plunge rate
     (l.431806-431825). The exact rates are NOT DETERMINED.
4. **Link to the point:** FUN_180140d80(.., max short gap = msurf_max_short_gap, gap type **4** for 0x2c) (l.431842-431860).
   - Type 4 = always the retract-type link (sr 8 "Moves": "gap > max short gap or type 4 -> retract (clearance when retract is
     off)").
   - Its geometry is **NOT DECODED** (as for op 5).
   - The first point of a pass uses FUN_1801408c0 instead (the approach, l.431903).
   - Then FUN_180141720(1, 1, ..) feeds the plunge.
5. **Retract after the plunge** when rapid retract is off (op+0x141 = **record 0x1a9** == 0; formats/mcamui_dialogs.md l.91), at
   l.431881-431895 and l.431920-431950:
   - Z_ret: not first level -> P; first level -> max(L, z) + step.
   - If the feed plane is absolute and higher, Z_ret = fp.
   - It is raised to top-of-stock + r (as 2).
   - With an incremental feed plane, + fp.
   - Move FUN_180141720 at rate op+0x320 (record 0x388), or the plunge-move rate when that is < tol (l.431752-431762).
   - With rapid retract on, the retract is left to the next link. NOT DECODED.

### 2.5 Dialog: tab 3 "Rough plunge parameters" (title 0x7d = 125) = CMsPlungePg, dialog **1038**
The ctor is FUN_1800e99a0 `MCPropertyPage(0x40e)` (l.151079-151089), added for case 0x2c (l.450428-450441). Init FUN_1800ea590
(l.151538-151634), OK FUN_1800eae90 (l.151799-151852), OnSetActive FUN_1800eb170 (l.151889-151946), kill-active FUN_1800eacd0
(l.151744-151756).

| control | label | record (op+X) | type | source |
|---|---|---|---|---|
| 25458 / 669 | Total tolerance | 0x1240 (0x11d8); with the filter on (op+0x2d2) the page shows tolerance + filter op+0x2b8 and splits it back on OK | f64 | l.151566-151577, l.151820-151829 |
| 920 | Max stepdown | 0x1248 (0x11e0) | f64 | l.151581, l.151831 |
| 845 | Maximum stepover | 0x13d0 (0x1368), shown abs | f64 | l.151579, l.151830 |
| 1084 / 1085 | Plunge path NCI / Zigzag | 0x141a (0x13b2) short: 1 = NCI, 0 = Zigzag (radio member = (value == 0)) | s16 | l.151587, l.151834 |
| 997 tree / 1086 | Source operations | 0x13e8 (0x1380) long op id; NCI with nothing selected -> warning 0x123, forced Zigzag | i32 | l.151589-151610, l.151837-151846, FUN_1800ead90 l.151758 |
| 695 | Allow small steps (NCI only: disabled with Zigzag together with 1086 / 997) | 0x1428 (0x13c0) | u8 | l.151588, l.151836, l.151904-151920 |
| 739 / 736 | Helix (check) / Helix... | 0x13ec (0x1384) | u8 | l.151583, l.151833 |
| 664 / 904 / 906 | Cut depths / Gap settings / Advanced settings | sr 4 (177 / 182 / 183) | | |

Helix sub-dialog 487 "Helix parameters":
- DDX FUN_1800e9df0 (l.151245-151258); init FUN_1800ea8a0 l.151635-151743 (default branch l.151679-151689); OK FUN_1800ea1a0
  (l.151404-151465; the plunge branch l.151425-151435).
- Fields:
  - Radius 1089 = **0x13f0** (op+0x1388); <= tol -> 1.0 in / 25 mm.
  - Z clearance 1090 = **0x13f8**; <= tol -> 0.5 / 10.
  - Plunge angle 981 = **0x1400**; <= tol -> 0.0523599 (3 degrees in radians); limits tol .. 89.
  - Tolerance 1095 = **0x1410**; <= tol -> 0.001 / 0.025. Disabled with op+0x13a0 = record 0x1408.
  - CW / CCW 1093 / 1094 = **0x1418** (1 = CCW).
  - Plunge rate / Feed rate 622 / 624 = **0x1419** (1 = Feed rate).
- The defaults' substitutions are at l.151695-151740.

Other record fields:
- **record 0x1420** (op+0x13b8): read as the lead length of 0x2c (FUN_1801300f0 l.200596-200598). Its control is NOT DECODED.
- **record 0x13d8 / 0x13e0** (op+0x1370 / 0x1378): the plunge / retract direction lengths of 0x2c, used when msurf_use_dirn (op+0x11e8
  = record 0x1250, page 999 758) (FUN_18012fee0 l.200519-200521). They are unused in the default op (0x1250 = 0).

### 2.6 Op 44 defaults (measured, MM / Inch)

| record | field | MM | Inch |
|---|---|---|---|
| 0x34c / 0x350 / 0x358 | tool | 19 bull D 20 r 4 | 11 ball D 0.5 r 0.25 |
| 0x170 / 0x179 / 0x180 / 0x189 / 0x190 | clearance (off) / retract (on) / feed plane | 50 / 0 / 25 / 1 / 5 | 2 / 0 / 0.25 / 1 / 0.1 |
| 0x1239 / 0x1240 / 0x1248 | tip comp / tolerance / max stepdown | 1 / 0.025 / 2 | 1 / 0.001 / 0.25 |
| 0x1288 / 0x1290 / 0x1298 / 0x12a0 | gap | 1 / 2.5 / 3 / 1 | 1 / 0.01 / 3 / 1 |
| 0x12e8 / 0x12e9 / 0x1300 / 0x1308 / 0x1310 / 0x1318 | depths: incremental, rel corner, top 0.2 / 0.01, bottom 0.2 / 0.01, min 0, max -10 / -1 | 1 / 1 / 0.2 / 0.2 / 0 / -10 | 1 / 1 / 0.01 / 0.01 / 0 / -1 |
| 0x13d0 | max stepover | 12 | 0.45 |
| 0x13e8 / 0x141a / 0x1428 | source op / plunge path / small steps | 0 / 0 Zigzag / 0 | 0 / 0 / 0 |
| 0x13ec / 0x13f0 / 0x13f8 / 0x1400 / 0x1410 / 0x1418 / 0x1419 | helix on / radius / Z clear / angle / tol / CCW / feed | 0 / 25 / 10 / 0.05236 / 0.025 / 0 / 0 | **1** / 1 / 0.5 / 0.05236 / 0.001 / 0 / 0 |
| 0x1408 | (disables helix tolerance) | 1 | 1 |
| 0x1250 / 0x13d8 / 0x13e0 / 0x1420 | direction / lengths / lead | 0 | 0 |

The Inch default has the helix plunge on (FUN_18028b7f0, NOT DECODED). That one would be refused.

### 2.7 NOT DECODED (op 44)
- The drop: build_machine_model's offset-surface model, and FUN_180290fb0(1, ..), the projection of the grid segments (l.428828-429796,
  968 lines). This includes what the 10-chord segment of 2.2 is for, and the msproj header fields DAT_180513a60 / a08 / a78.
- The incremental depth resolution FUN_180296cf0.
- The link geometry (FUN_180140d80 type 4, FUN_1801408c0), the rapid-retract branch, the exact rates.
- The helix plunge FUN_18028b7f0.
- The NCI source mode (FUN_18012cf00, FUN_18012b940, FUN_18012bf40).
- Containment: the common pipeline writes tmptc.tmp (sr 1.2); its use by the plunge projection is not traced.
- Check surfaces.
- The NCI writer FUN_180136670.
- The meaning of record 0x1420.

### 2.8 Oracle
- **None.** No op 44 exists in either scan.tsv (netscan 1460 parts, local scan) or in the user's parts.
- The port would be unverified, as Parallel's is (sr 3.4).

### 2.9 Port outline (viewer/cam3d_srough.c), `gen_srough_plunge` - DONE 2026-10-28 (branch toolpaths2), unverified (no oracle)
**Done:** CAM_SRPLUNGE ("Surface Rough Plunge", mccoreres 4365; the 3D gallery's Roughing "Plunge"; cam_cli "srplunge"); default record =
the defaults' first op 44 (Mill_MM op 24 / Mill_Inch op 32). Corners (user decision T-2): after the drive pick, two point picks with
select_surf_points' prompts (l.20442-20470: mcmsurfres 415 "Select plunge point at lower left" / 416 "Select plunge point at upper right";
the click's AutoCursor point), kept as the op's holes 0 / 1; Mastercam asks after the op dialog (ours: before). gen_srough_plunge as below,
the decoded rules cited in its comment; ours: the drop (op 5's height field), incremental depths from the grid's Z range, the gap-type-4
link geometry, st's 2 x DAT_180513a60 taken as op 5's 2 x 0.9 chord. Refused: NCI path, helix (the Inch default has it on: refused there),
absolute depths with tip comp Tip, Center with a corner radius. Page 1038 (opdlg_fields.tsv s1038: tolerance - shown plain, the filter
split is not done -, stepdown, stepover, plunge path, small steps, helix check); the source op tree, Helix 487 and the sub-dialogs are not
shown. Scene gen_srplunge (a ruled surface, 10 mm ball: 2221 points). The steps as planned:
1. **Record and refusals.** sr_record. Refuse when:
   - plunge path 0x141a == 1 (NCI);
   - helix 0x13ec with radius > tol;
   - not both corner points;
   - check surfaces.
2. **Corners.** Mastercam's are the op's two point TP records. A stored op carries them (mcam_ops `pts`, record type 1 at +0x90 /
   +0x98).
   - New ops need a two-point pick in the UI (the classic "select points" step; select_surf_points l.12425).
   - Taking the drive's XY box instead would be ours and needs a user decision.
3. **Grid.** 2.2 in the Top plane: u = x, v = y as read in 2.2 step 2 (FUN_180266780 NOT DECODED). The zigzag rows and the
   equalised steps (s = record 0x13d0, < 1e-10 -> 0.05, l.340495).
4. **Drop (ours, as sr_parallel).** sr_hf_build + sr_hf_tip with the stock-inflated tool (Fo / Rb as in sr 2.3). This stands in for
   the offset model + FUN_180290fb0(1) (NOT DECODED). Off the model: no point (sr_hf_tip returns -1e30).
5. **Levels.** sr_parallel's absolute / incremental level block (same rule, 2.3). Incremental: profile max - top adjustment /
   profile min + bottom adjustment, as op 5. FUN_180296cf0 is NOT DECODED, so this is "ours, from op 5".
6. **Per level, per point** (grid order): the rules of 2.4.
   - skip if not first and z >= L + step;
   - z' = max(z, L);
   - H / Z_ret as decoded;
   - moves: link = full retract (gap type 4: rapid to retract, or clearance when retract is off; then over; then rapid to H); then
     a plunge feed to z'; then the retract feed to Z_ret (rapid retract off), else a rapid.
   - The link geometry is ours, labelled, as sr_parallel's links.
7. **Dialog.** Tool parameters, Surface parameters 999 (as op 10's, with top of stock 0x141c), page 1038 (2.5), helix 487 rows
   (shown; generation refuses helix).

## 3. Finish Contour (op 15 = 0xf)

### 3.1 Pipeline
- **bnci_surface** (l.11964-11996):
  - fzt stays on (0xf is in the set);
  - emm only if stock < 0 with |stock| > r, or msurf_use_emm_contour (default 0, l.18492);
  - 3rd party off (msurf_use_3rd_party_contour = 0, l.18485).
  - So the drive is the bare mesh and run_mctutl_zcut, as op 10 (sr 2.1 / 2.3).
- **msurf_map case 9 / 0xf** (l.340103-340114):
  - prompt start point op+0x1368 (**0x13d0**);
  - open-contour method op+0x136a (**0x13d2**: 1 One way, 0 Zigzag; rough_one_way);
  - climb op+0x136c (**0x13d4**);
  - DAT_180513eee = 0;
  - then LAB_180206132 (l.340366-340382):
    - bottom to top op+0x1380 (**0x13e8**) -> rough_top_down = 0 and optimise cut order op+0x123c (0x12a4) cleared;
    - with gap type 2 (High speed), corner rounding op+0x1398 (**0x1400**) is used;
    - FUN_180386eb0(ramp length op+0x13c0 = **0x1428**, corner radius, loop length op+0x13b8 = **0x1420**, 0).
  - Then the depth block LAB_180206ec4 (as 2.1; posz / negz / plunge not read).
  - Short gap (l.340450-340466): max = pct x rough_zdist. For 0xf with op+0x1438 (**0x14a0**), pct = op+0x1440 (**0x14a8**) x D.
- **Stage 1** `case 9: case 0xf: case 0x6b:` (l.197547-197558):
  - FUN_18013b2f0 (l.207046), then op+0x13a0 temporarily 0, then FUN_18013bb50 (l.207308).
  - Then **FUN_1802a14e0(1, ..)**, the same Z-level region builder as op 10:
    - FUN_1802a0d50 levels (sr 2.2);
    - the adjust-for-stock flag for 0xf is op+0x13e6 = **record 0x144e** (l.438263-438268);
    - keep-top 0x1440 is op-10-only (l.438380);
    - FUN_180305ab0 tool-centre loops by run_mctutl_zcut (sr 2.3).
  - A temp-file rename branch runs when FUN_18013bb50 returns true. When record 0x1408 > 11, a re-run follows (l.197573-197590).
    **NOT DECODED.**
- **Stage 2:** FUN_1802a0710 (l.199483; l.437931-438183) -> FUN_180302350(rough_cleanup, .., rough_min_ctour_len) (l.491102-492476).
  - For 9 / 0x6b / 0xf with record 0x1408 > 11 and tol < op+0x13a8 (record 0x1410) < zdist: the shallow re-levelling FUN_180316a00 /
    FUN_180316870 (l.491698-491703).
  - Per level: **FUN_18035b9d0** (l.541195-541456). For contour (pocket flag param_14 = 0, DAT_180513eee = 0):
    - param_16 == 0: FUN_1803719c0 (l.552191) + **FUN_18035d930** (l.541996-542025). It loops: FUN_1803694e0 picks the next loop
      (by the start point), then FUN_18035d150 cuts it.
    - Flats (record 0x1492 > 1, step record 0x1498, or zdist, or chord; l.541262-541278) with param_16 set: FUN_18036ad90 /
      FUN_1803698b0 / FUN_18036a310, chosen by the registry DWORD "Contour flats style" (1 / 2 / 3, default 1; FUN_18031cce0).
    - Else FUN_18036ba50 (shallow 0x1408 in 10..11) / FUN_18036c1f0, then FUN_18035cb00 (l.541361-541380).
    - param_16's meaning is NOT DETERMINED.
  - Then FUN_18022f700(1, nc, 0xd) and FUN_180136670 (l.438117-438134).

### 3.2 Dialog: tab 3 "Finish contour parameters" (title 0x77 = 119) = CMsContor, dialog **180**
The ctor is FUN_1800d4a60 `MCPropertyPage(0xb4)` (l.450323-450338). DDX FUN_1800d5160; OK FUN_1800d7f20 (l.141298-141483; the 0xf
branch l.141307-141392); init FUN_1800d54d0 (not transcribed here). MCReal value = member + 0xc00; a control's HWND = member + 0x40.

| control | label | record (op+X) | notes | OK line |
|---|---|---|---|---|
| 25458 / 669 | Total tolerance | 0x1240 (0x11d8) + filter split | as plunge | l.141309-141322 |
| 920 | Maximum stepdown | 0x1248 (0x11e0) | | l.141323 |
| 1023 | Corner rounding radius | 0x1400 (0x1398) | used with High speed transition | l.141343 |
| 858 / 863 | Direction of closed contours Climb / Conventional | 0x13d4 (0x136c) = (radio == 0) | 1 = Climb | l.141324 |
| 1027 | Start length | 0x1440 (0x13d8) | | l.141344 |
| 870 / 873 | Direction of open contours One way / Zigzag | 0x13d2 (0x136a): row 0 -> 1, row 1 -> 0 | | l.141325-141330 |
| 1046 / 1047 / 755 / 1048 | Transition High speed / Broken / Ramp / Follow surface | 0x12a0 (0x1238): row 0 -> 2, 1 -> 1, 2 -> 0, 3 -> 3; the old value's tens (>= 10 in v % 100) and hundreds (>= 100) are kept | type = (v % 100) % 10 (msurf_map l.340022-340024) | l.141366-141379 |
| 1049 | Entry/exit arc/line (check) | 0x1430 (0x13c8) | | l.141352 |
| 956 / 957 | Arc radius / Arc sweep | 0x12a8 / 0x12b0 (0x1240 / 0x1248; entry_r / entry_a degrees, l.340034-340035) | | l.141362-141363 |
| 1149 | Line length | 0x1488 (0x1420) | | l.141357 |
| 1055 | Allow arc/line outside boundary | 0x144f (0x13e7) | | l.141354-141355 |
| 1016 / 1018 | Loop length / Ramp length | 0x1420 (0x13b8) / 0x1428 (0x13c0) | to FUN_180386eb0 | l.141351, l.141342 |
| 790 | Use approximate start point | 0x13d0 (0x1368) | | l.141331-141332 |
| 806 | Optimize cut order | 0x12a4 (0x123c) | | l.141345-141346 |
| 1153 | Minimize burial | 0x1490 (0x1428) = **!check** | | l.141347-141348 |
| 817 | Order cuts bottom to top | 0x13e8 (0x1380) | | l.141349-141350 |
| 739 / 736 | Helix (check) / Helix... | 0x1450 (0x13e8) / dialog 487: radius 0x1458, Z clear 0x1460, angle 0x1468, tolerance 0x1478, CCW 0x1480, feed rate 0x1481, tolerance disabled by 0x1470 | FUN_1800ea590 l.151642-151656, FUN_1800ea1a0 l.151446-151453 | l.141380-141381 |
| 1012 / 1014 | Shallow (check) / Shallow... (dialog 998) | 0x1408 (0x13a0) short: +10 when checked, -10 when not | 998's fields NOT DECODED (DDX FUN_1800d50d0 only) | l.141333-141341 |
| 1152 / 983 | Flats (check) / Flats... (dialog 493) | the OK clears the check's state (BM_SETCHECK 0, l.141382); the record of "flats on" = 0x1492 (op+0x142a) > 1 (FUN_18035b9d0 l.541262), its writer NOT DECODED; 493 stepover 1169 -> 0x1498 inferred from FUN_18035b9d0's read | | |
| 1176 / 1177 | Spiral (check) / limit | 0x14b1 (0x1449) / 0x14b8 (0x1450) | | l.141358-141359 |
| 664 / 904 / 906 | Cut depths / Gap settings / Advanced | sr 4 | | |

Also: op+0x136d (record 0x13d5) is cleared on OK (l.141342).

### 3.3 Op 15 defaults (measured, MM / Inch) and the oracle op

| record | field | MM | Inch | oracle op |
|---|---|---|---|---|
| 0x34c / 0x350 / 0x358 | tool | 19 bull D 10 r 4 | 11 ball D 0.5 r 0.25 | 11 ball D 0.0625 r 0.03125 |
| 0x1240 / 0x1248 | tolerance / max stepdown | 0.025 / 2 | 0.001 / 0.1 | 0.001 / 0.005 |
| 0x12a0 | transition (v % 10) | 11 -> Broken | 11 -> Broken | 11 -> Broken |
| 0x12a8 / 0x12b0 / 0x1430 | arc radius / sweep / entry on | 5 / 90 / 0 | 0.25 / 90 / 0 | 0.25 / 90 / 0 |
| 0x12e8 / 0x12e9 / 0x1300 / 0x1308 / 0x1310 / 0x1318 | depths | inc 1 / 1 / 0.2 / 0.2 / 0 / -10 | inc 1 / 1 / 0.01 / 0.01 / 0 / -1 | **abs** 0 / 1 / .. / min 0 / max -0.087953 |
| 0x13d0 / 0x13d2 / 0x13d4 / 0x13e8 | approx start / open method / climb / bottom-top | 0 / 1 One way / 1 / 0 | 0 / 0 Zigzag / 1 / 0 | 0 / 0 / 1 / 0 |
| 0x1400 / 0x1420 / 0x1428 / 0x1440 | corner rounding / loop / ramp / start length | 2 / 2 / 10 / 0 | 0 / 0.05 / 0.5 / 0 | 0 / 0.05 / 0.5 / 0 |
| 0x1408 / 0x1410 | shallow / (shallow min stepdown?) | 0 / 0.2 | 0 / 0.01 | 0 / 0.005 |
| 0x144e / 0x144f / 0x1450 / 0x1490 / 0x1492 / 0x14b1 | adjust stock / arc outside / helix / !burial / flats / spiral | 0 / 1 / 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 / 0 / 0 |

**Oracle (the only stored op 15 with NCI):**
- `.../net/emastercam-training/2017/Mastercam 2017 Handbook Volume 2 Files/MCX Files/CONTOURFINISH1.mcam` **op 3** (3188 moves).
- The same op in `.../X9/Mastercam X9 Handbook Volume 2 Files/MCX Files/SHALLOWFINISH.mcx-9` op 3 (scan.tsv).
- No containment chain (mcam_ops chains = []). The drive surfaces come through the TP refs, sampled as in sr 5 (tools/surfs2stl.py).
- It needs the tool given (--dia 0.0625, corner 0.03125), as srough_oracle.sh does.
- The settings are the simplest path: absolute 0 .. -0.088 by 0.005 (about 18 levels), Broken transition, climb, no
  shallow / flats / helix / spiral / entry arcs.
- The 2018 copy of CONTOURFINISH1.mcam has no NCI (scan.tsv: 0 moves).

### 3.4 NOT DECODED (op 15)
- run_mctutl_zcut (as op 10).
- How a level's loops become passes: FUN_1803694e0 (the next-loop choice), FUN_18035d150 (the cut of one loop: direction, start,
  the 0x12a4 / 0x1490 options), FUN_1803719c0.
- The transitions between levels / loops for all four types (FUN_180140d80 types, the High speed FUN_180386eb0 params).
- Entry / exit arcs and lines, helix entry, spiral (0x14b1 / 0x14b8).
- Shallow (dialog 998, FUN_180316a00 / FUN_180316870, FUN_18036ba50) and flats (dialog 493, FUN_18036ad90 / 9698b0 / 36a310,
  FUN_18036c1f0).
- Stage-1 helpers FUN_18013b2f0 / FUN_18013bb50 (1466 lines).
- The NCI writer.
- The open-contour case: what makes a loop "open" is containment clipping, inferred from the dialog labels and not traced.

### 3.5 Port outline (viewer/cam3d_srough.c), `gen_sfinish_contour`
1. **Record and refusals.** sr_record. Refuse when (each NOT DECODED):
   - shallow (0x1408 >= 10);
   - flats (0x1492 > 1);
   - helix (0x1450);
   - entry arc / line (0x1430);
   - spiral (0x14b1);
   - High speed / Ramp / Follow surface transitions ((0x12a0 % 100) % 10 != 1). Broken is the oracle's; its geometry is still ours.
   - approximate start point 0x13d0, optimise cut order 0x12a4, check surfaces.
2. **Levels.** sr_depths, with the adjust-for-stock flag at 0x144e instead of 0x1432 and no keep-top (l.438263-438268,
   l.438380). Then sr_levels (FUN_18029cee0 is shared).
3. **Drive.** c3_mesh_op at 0.5 chord (fzt, as op 10).
4. **Per level** (top to bottom unless 0x13e8):
   - c3_slice(z) gives the forbidden region; minus it from the containment (sr_containment) gives the free region. Its boundary loops
     are the tool-centre loops that run_mctutl_zcut returns (sr 2.3). Ours: the slice stands in, as in gen_srough_pocket.
   - Each loop is one pass at Z = level.
   - Direction: closed loops climb (0x13d4 = 1) / conventional. Orientation relative to material is ours until FUN_18035d150 is
     decoded.
   - Open pieces (cut by the containment): one way / zigzag (0x13d2).
   - Loop order and start point: ours (FUN_1803694e0 NOT DECODED).
5. **Links.** sr_parallel's: retract when the gap is > max short gap (pct x rough_zdist, l.340452); else a straight feed (ours, as
   op 5's short gap).
6. **Dialog.** Tool parameters, Surface parameters 999 (as op 15: check stock, containment), Contour parameters 180 (3.2). The
   Shallow / Flats / Helix buttons are shown; generation refuses them.
7. **Oracle script:** as srough_oracle.sh, on CONTOURFINISH1.mcam op 3 with tools/oracle3d.py (ref->ours / ours->ref distances, cut
   length).
