# NestLib engine (n64dll.dll) — decode map + port plan (phase 1, 2026-11-05)

User decision N-1 (formats/user_decisions.md l.218): decompile Geometric's NestLib and port the engine itself. This file
maps the engine; the Mastercam side (NESTING.DLL glue, dialogs, write-back) is in formats/wireframe.md "Geometry Nesting —
scope / port spec (2026-11-04)". That section's sentence "NOT DECODED, and not proposed for porting" is superseded by this file.

## Sources
- Binary: `~/mastercam-re/files/n64dll.dll` (2 891 776 bytes, md5 22654858752f472420e5a6f0f8f4f494, identical to
  `<windows>/Program Files/mcamX9/N64DLL.DLL` — X9 and 2017 ship the same engine). Image base 0x180000000.
- Ghidra project `~/mastercam-re/ghidra_nest64` (project "nest64", imported + full auto-analysis 2026-11-05, MSVC FID applied).
- Dumps (decomp/ is gitignored), all in `decomp/nest/`:
  - `n64dll_all.c` — every function (DumpDecomp ".", 5190 functions, 291 854 lines). **"L<n>" below = line in this file.**
  - `n64dll_index.txt` — `addr name startline nlines` per function.
  - `n64dll_exports.txt` (394 exports with VA), `n64dll_syms.txt` (SymList), `n64dll_strings.txt` (strings -n5),
    `n64dll_pe.txt` (objdump -p: imports / exports).
  - `nesting_cb.c` — NESTING.DLL callbacks FUN_180006700 / FUN_180005f90 / FUN_180005830 / FUN_180009da0 / FUN_180009ca0
    (project ghidra_nesting, DecompAt, 2026-11-05). "NE" = nesting.dll addresses.
- Mastercam's import list from n64dll: `objdump -p nesting.dll` (89 R/N functions of the 394 exports).
- Tags: `[inferred]` = reasoned, not read; `NOT DECODED` = not read / not understood.

## 1. What the DLL is
- Strings "Nestlib Development Version", "http://nestlib.geometricglobal.com", "VERSION : 3.0, DATE : 30 JUN 95" (n64dll_strings.txt).
- Imports: USER32 (a small progress dialog for runNestWin: CreateDialogParamA L3690, PeekMessageA L3769), KERNEL32, GDI32
  only (n64dll_pe.txt l.62-190). **No CreateThread: single-threaded.** No third-party math / geometry / compression library
  is linked; the geometry kernel is NestLib's own code.
- Layout by address (scratchpad range map; the boundary is the first CRT/STL function std::basic_stringstream @180219ed0):

| range | content | functions / decompiled lines |
|---|---|---|
| 0x180001000-0x180219e00 | NestLib application code (C with a few C++ classes) | 3530 / 235 330 |
| 0x180219e00-end of .text | MSVC CRT + Dinkumware STL ("Copyright (c) 1992-2004 by P.J. Plauger") — skip | ~1000 / 45 400 |
| 0x180502000-0x1805034d1 (separate `text` section) | MSVC libm (sin/cos-type polynomials FUN_180502000 / FUN_180502490 [inferred]) | 3 |
| 0x1801eadd0-0x1801ec5d0 | NProfileUtility API (NCreateProfile..NIfDetectLeadins) — not used by Mastercam | |
| ~0x1801ed000-0x1801f0000 | DXF reader (`CDxfLoop`, "ENTITIES"/"BLOCKS"/"LWPOLYLINE") — not used | |
| 0x1801f0000-0x180219e00 | .nip reader / writer (`NipReader`, `Nip*Section`, `NipWriter`, ~661 fns / 20k lines) | used only by runNestCommon |

- Reach (scratchpad reach2.py: call + function-pointer references, roots = the 89 functions Mastercam imports; the only
  vtables are destructors / the rect engine's callback vtable, so virtual dispatch does not hide code):

| from | functions | decompiled lines |
|---|---|---|
| all 89 Mastercam-imported APIs | 1811 | 158 254 |
| TrueShape APIs (N*) | 1673 | 150 948 |
| Rectangular APIs (RN*) | 383 | 20 314 (245 fns / 13k lines shared with TrueShape: lists, logging, errors) |
| app code not reachable from Mastercam | 1700 | 76 492 |

## 2. API (the Mastercam-used subset)
Global state machine `DAT_1802931a8`: 1 closed, 2 open/idle, 3 setting callbacks, 4 setting params, 5 part list, 6 nest
order / inquiry, 7 inside a closed profile, 9 geometry phase; a call in the wrong state throws a NestLib error via
FUN_1800537a0(code,..) + _CxxThrowException (e.g. NNestOrder L8085-8100). `FUN_180061dc0(&DAT_1802b4d20, n)` @180061dc0
L49567 (the feature gate called by every API) just returns 1. Every API call is also logged ("State: %d" + call text) to
`NInqLogFileName()` when env `NESTLIB_MODE=Geometric` (FUN_180033190 L28604) — a free tracing facility (§7).

**Licence.** NSetDLLEnableKey @1800158c0 L11450 -> FUN_180061000 L48957: `strcmp(key, s_023FED93..._1802ab040)` (L48981);
match -> `DAT_1802b0da0` = 1, else error 0x465. With the flag 0, NAddPartInfo adds nothing (L7259) and the part-geometry
callback wrapper FUN_180061720 L49280 never calls the client — nothing nests. No demo mode. The port drops this gate.

TrueShape (addresses = exports; L = n64dll_all.c):

| function | args | effect |
|---|---|---|
| NOpenNestLib @18000ab40 L4506 | (dir, flag, errCB) | state 1->2; errCB -> DAT_1802b2f90; installs the default function table FUN_18001e6f0 L16600 (main entry `DAT_1802b1108` = FUN_18001e600); defaults FUN_18005e0c0 (resolution 1.0, cutter 0, clearance 0, FitPartInPart 1, others 0) |
| NStart/NEndSettingCallBacks @18000b100 / @18000bbc0 | | 2->3->2 |
| NSetErrorHandlerCB / NSetProgressFunctionCB / NSetNewPartGeomHandleCB / NSetNewSheetGeomHandleCB / NSetNestedSheetResultCB / NSetNestedOrderResultCB (@18000b260..@18000ba70, L4786-5152) | fnptr | DAT_1802b2f90 / 2f98 / 2fa0 / 2fb0 / 2fb8 / 2fd0 |
| NStartSetParams @18000bd20 / NEndSetParam @18000e330 | | 2->4->2 |
| NSetResolution @18000bea0 L5330 | double | FUN_18005e540: must be > 0.001 (else error 0x3ec) -> params+0x408 |
| NSetCutterDiameter @18000c190 | double | FUN_18005e810 (<= 0.001 -> 0) -> params+0x820 |
| NSetClearance @18000cbd0 | double | FUN_18005eb20 (<= 0.001 -> 0) -> params+0x838 |
| NSetPartDistType @18000cd50 | 0/1 | FUN_18005f3d0 -> params+0x3f8 |
| NSetThicknessAndMaterialSelection @18000df80 | (thick, mat) | FUN_180060d70 |
| NSetFitMaxOnLast / NSetFitClusterFirst @18000d340 / @18000d4b0 | 0/1 | params+0x2fc / +0x300 |
| NSetFitPartInPart @18000d640 | (fit 0/1/2, preferHole) | FUN_18005f210 -> params+0x304 / +0x308 |
| NSetExactDistance @18000d930 | 0/1/2 | FUN_18005f590 -> params+0x374 / +0x37c |
| NSetClusterCutoffUtil @18000dc40 | double | params+0x868 |
| NSetAccuNest @1800149a0 L10818 | int | FUN_180060b40 -> params+0x378 = (x==1) (enables the "shake" pass; Mastercam passes 0) |
| NSetGrpMaxElmtSize @180013f00 | int | FUN_180075b90: DAT_1802aad7c = max(old, x) |
| NSetGuillotineCut @180013120 | 0/1 | FUN_18005ffa0 -> params+0x36c |
| NSetNestFillersUptoNestedHeight @180011c90 | int | FUN_18005ff20 |
| NStartNewPartList @18000e470 / NEndNewPartList @18000f860 | | ->5 / FUN_18006f290 ->2 |
| NAddPartInfo @18000ebf0 L7132 | (name, minQty, maxQty, isRect [corrected 2026-11-05 by the peloader work: the 4th argument is the "part is a rectangle" flag, L55029; mirror is the 12th], d1, d2, stepAngle, grainChar, priority, fitType, p11, mirror) | staged in DAT_1802b4ec0..53a0: min FUN_18006ff10, max >= min FUN_180070020, mirror FUN_180070120, step (0,360.0001] FUN_180070b10 (default 90.0), grain 'N'/'X'/'Y' -> 1/2/3 FUN_180070cd0, priority >= 0 FUN_180070e70, fit type FUN_180070f60 (5 -> 3 + flag); p11/p12 NOT DECODED |
| NDefinePartMaterialList, NStartGroup / NAddGroupMember / NEndGroup | | logged; internals NOT DECODED |
| NStartCluster @180121740 L130072, NAddClusterElement @180121d80, NAddClusterInfo @1801223e0, NEndCluster @180121f80 | | cluster module 0x18012xxxx; internals NOT DECODED |
| NStartNestOrder @180010070 L7950 | | ->6 |
| NAddSheetInfo @180010ad0 L8328 | 18 args: name, qty, p3, (rect flag, length, width), local area (flag, xmin, ymin, xmax, ymax), corner 1..4, grain, direction, collar ... | staged DAT_1802b6f70..7144, committed FUN_180077f20; exact arg order partly [inferred] |
| NAddSheetPriority @180011890, NDefineSheetMaterialList @1800108d0 | | |
| NNestOrder @1800102d0 L8040 | | FUN_1800582d0 pre-pass, then `(*DAT_1802b1108)()` (L8124) = FUN_18001e600 -> FUN_180053960 (driver). Returns -1 / 0 / 1 |
| NDwgGeomStart / NDwgProfsStart(type) / NDwgClosedProfStart(type) / ...End (@180015d40, @1800160d0, @180017410, @18001aa70, @18001a590, @18001b110) | | called by the client from inside the geometry callback; profs type -> DAT_1802aae28, closed type -> DAT_1802aae2c |
| NDwgAddLine @1800181b0 L13030 | (x1,y1,x2,y2) | FUN_180096f60 -> entity type 4 |
| NDwgAddArc @1800192c0 L13635 | (cx, cy, sx, sy, sweepRad, ..) | |sweep-2pi| < 1e-4 -> circle; else FUN_1800979f0 L72863 (r > 1e-4, end = c + R(sweep)(s-c)) -> type 6 |
| NDwgAddCircle @180019da0 | (cx,cy,r) | FUN_180098640 -> type 5 |
| NDwgChangeCutterDia @180017fd0 | double | FUN_180098ca0: per-part cutter (list DAT_1802f2520, keyed by part name) |
| NInqNestedSheet @18002f0e0 L26126, NInqCurrentSheet @18002f580 | | FUN_180041740 / FUN_180042010 (current sheet DAT_1802b3258, result DAT_1802b3238) |
| NInqNestedParameters @18002fac0 L26473 | 5 doubles | FUN_180041ef0 L34503: resolution, cutter, clearance, part offset (+0x848), sheet+0x3b0 |
| NInqNestedPartType / NInqLastNestedPartType @18002d700 / @18002dda0 | | |
| NInqNestedInstance @18002e270 L25638 | (sheet, inst, 7 x double*, int*, int*) | FUN_1800400c0 L33621: 0x1b0-byte instance record +0x18..+0x40, +0, status +0x4c (0..3), +0x60. Which double is x / y / angle: NOT DECODED (the format string "NInqNestedInstance(%d, %d, %lf x7, %d, %d)" only gives types) |
| NInqLastNestedInstance @18002e970, NInqLastNestedPart @18002fee0, NInqNextPartType @1800314b0, NInqOrientationBeingTried @180031a70 | | progress-callback queries |
| NCommitNestedOrientation @180011f70 L9167 | (action, n) | FUN_180043ad0; reply flag DAT_1802b734c (0 accept, 3 reject part, 5 retry, 6 skip — fork reading of L9471, [inferred] names) |
| NSetCurrentSheetLocalArea @1800127d0 L9476 | 4 doubles | FUN_180043ad0 / FUN_18005be10 |
| NCloseNestLib @180011a30 L8957 | | frees, ->1 |

Rectangular (wrappers L18505-19900, back-ends L105397-105922): RNInitRectangleNest @1800228b0; callback setters store only
(error DAT_1802f2c40, progress 2c48 — **never called**, new sheet 2c50, part nested 2c58, sheet result 2c60, order result 2c70);
RNStartSetParams (FUN_1800e6f20: cutter 0, clearance 0, resolution 1.0, fillers 0); RNSetCutterDiameter -> DAT_1802f2c80,
RNSetClearance -> 2c88, RNSetResolution -> 2c90 (**stored but unused**, see §5), RNSetNestFillersUptoNestedHeight(1) -> 2c98;
RNEndSetParam -> FUN_1800e7010 L105546 allocates the 0x85ca80-byte `optiRectOrder` (ctor FUN_1801b65e0 / FUN_1801b6890 L192805 /
L192918); RNAddPartInfo2(name, min, max, len, wid, rotate, grain) -> FUN_1800e7090 -> FUN_1801b6f60 L193023; RNSetPartPriority
-> +0x150 of the last part (FUN_1801b7370); RNAddSheetInfo2(name, qty, len, wid, collar, grain) -> FUN_1801b73d0; RNNestOrder
@180023e70 L19420 -> FUN_1800e7520 -> FUN_1801b7960 L193352; RNInqNestedSheet (FUN_1801c0d30 L196768) / RNInqNestedPart(i)
(FUN_1801c10f0) valid only inside the sheet-result callback (state DAT_1802ac098 == 3; else "Cannot Inquire in this state.");
RNEndRectangleNest frees (FUN_1800e7800). RNSetMethodType (not called by Mastercam) != 0 makes RNNestOrder a no-op (L193364).

**Mastercam's progress callback** (NE FUN_180006700, nesting_cb.c): Esc (`KeyPress(0x1b)`) returns 0 = stop; code 1 (part
nested) refuses > 50000 instances of one part type (message 0x2718/0x2719); code 0x13 NInqNextPartType; code 0x14 (orientation
being tried), when DAT_180135e89 is set [condition NOT DECODED]: NInqOrientationBeingTried, the part's mode short at part+0x51a
(1/2/3 -> FUN_1800cf2d0 with 1/4/9; 0 -> the whole sheet +0x230/+0x238) -> NSetCurrentSheetLocalArea(...), then at code 0xe
`NCommitNestedOrientation(2, -1)`. Other codes only update the status bar. So the client can restrict parts to sheet regions;
with DAT_180135e89 off the callback does not influence placement.

## 3. Data structures
- **Paged array** (ctor FUN_1800e6960 L105243 / FUN_1800a1f10 L77669): +0 elemSize, +4 pageSize, +8 max, +0xc count, +0x10 page
  pointers, +0x18 alloc fn, +0x20 free fn, +0x28 error fn, +0x30 pages used, +0x34 page count; element i at
  `pages[i/pageSize] + (i%pageSize)*elemSize` (loops in FUN_180053960 L42282). Port as a plain growable array.
- **Entity** 0xb8 bytes, type byte +8: 4 line (+0x48..+0x60 start/end), 5 circle (centre +0x48/+0x50, r +0x58), 6 arc (centre,
  r, start deg +0x60, sweep deg +0x68; sweeps within 1e-4 of 0/±90/±180/±360 are snapped) (FUN_180098d30 L73484, FUN_1800991f0
  L73627, FUN_180098da0 L73500).
- **Profile** 0x150 bytes (FUN_1800b00d0 L84490): short type +0, entity list +0x28, per-profile offset (init -1, FUN_1800b4b80
  L86910). **Drawing** = paged array of profiles (FUN_1800b0c20, page 100), built per part / sheet by FUN_1800af810 L84265
  (kind 1 sheet via FUN_1800618c0, 2/4 part via FUN_180061720), cached by name (FUN_1800b3f00); "mirror"/"Mirror" name prefix
  handled specially.
- **partToBeNested** 0x560 bytes (part list stride, FUN_180053960); embeds partType. Sort keys at +0x36c (int), +0x120 / +0x130
  (double), +0x360 (int) (§4); fit type +0x370; step angle +0x410; angle list +0x420. Rest NOT DECODED.
- **Sheet / trial record** 0x3d8 bytes (FUN_1800201b0 copies). Current sheet DAT_1802b3258 (+0 name, +0x200, +0x2a8, +0x2d0,
  +0x340, +0x350 / +0x358 length / width [inferred], +0x3b0, +0x3cc, +0x3d0).
- **Results**: list DAT_1802b32b8 of 0x2b0-byte nested-sheet records, each with 0x1b0-byte instance records (status 3 / 5 skipped
  when counting) + a 0x180-byte secondary list [inferred cluster / filler instances].
- **nestParameters** 0x9d8 bytes at DAT_1802b4340, copied per run by FUN_18005dfc0 L46412; derived fields (FUN_18005f9c0 L47709):
  +0x848 part offset = cutter + clearance/2 (guillotine and DAT_1802b46b0==0: (cutter+clearance)/2), +0x850 = clearance/2,
  +0x908 = cutter/2, +0x910 = 1.5*cutter + clearance.

## 4. TrueShape algorithm
**It is a raster (run-length grid) engine, not a no-fit-polygon engine.** Every part orientation and the sheet are rasterised
at `resolution` into column run-lengths ("angularCmap"); placement is a bottom-left column scan over those rasters.

Call tree (own lines; reach functions / lines):
```
NNestOrder L8040 -> FUN_1800582d0 pre-pass L44216 (1248; 263 f / 15.4k)
                        FUN_18010bbe0 part -> orientation rasters L120584 (463; 497 f / 43.6k)
                          FUN_18010df80 rotate / offset / rasterise one part, all angles L121510 (1135; 396 f / 34.2k)
                 -> FUN_18001e600 L16557 -> FUN_180053960 driver L42282 (471)
                     negCollar (sheet negative offset), FUN_180036090 hole pre-pass L30048 (597) [inferred: FitPartInPart]
                     FUN_180033980 sheet loop L28968 (963; 1374 f / 131k)
                       FUN_180048210 part sort L37073
                       FUN_1800376d0 L30645 -> FUN_180037840 nest one sheet L30679 (889)
                         FUN_18003a2a0 sheet rasterise L31581 (942)
                         FUN_18007cd10 place one part type L61310 (255; 839 f / 71k)
                           FUN_1800820a0 load / filter / sort orientations L63206 (343)
                           fit 2/4/13: FUN_1800ec190 L108177 (615) -> FUN_1800f1a20 scan + select L109854 (1213)
                                        -> FUN_1800f0fa0 lowest-y fixed point L109578 -> FUN_18007dc50 column fit (~L61560)
                           fit 1:      FUN_18018fdd0 L174864 (91; 446 f / 28k)
                         FUN_180149950 cluster / pair placement L147207 (598)
                         FUN_180139640 / FUN_18013c770 post-sheet passes (~2.5k lines each; guillotine / strip [inferred])
                         FUN_18004a640 finalise sheet, result callback L38157
```
1. **Pre-pass** (FUN_1800582d0): loads every part drawing through the geometry callback (FUN_1800af810), adjusts parameters,
   then FUN_18010bbe0 makes 360/step rotations of each part (respecting the angle list), offsets the profiles by the part offset
   (+0x848 = cutter + clearance/2: two parts end up `clearance` apart) and writes one run-length raster per angle to files
   `.cmp1..7` in the data directory (writers FUN_180116060 / FUN_180116440 / FUN_18010dca0). Arcs stay exact through the offset
   (offset core FUN_1800b00d0 L84490 -> FUN_1801a9bb0 L187664, builds type-6 arcs in FUN_18019cbe0); no arc-to-polyline step
   was found — the arc enters the raster directly [inferred]. The raster scan-conversion routine is NOT DECODED.
2. **Driver** (FUN_180053960): creates negCollar (DAT_1802b41d0); per part `negCollarSetMinOffset`; FUN_1800582d0; optional
   FUN_18005d410 sheet re-list; loop `while (more && !stop DAT_1802b2d78)`: per-part max of some per-orientation shorts ->
   FUN_18004f7a0; `FUN_180035f00()==1` (multi-trial path FUN_1800452d0, only set by NSetApplyBestNestingDirection, not called by
   Mastercam [inferred off]) else optional FUN_180036090 then **FUN_180033980**.
3. **Sheet loop** (FUN_180033980): sort parts (FUN_180048210 via fn slot DAT_1802b1110; params+0x2f4==1 (default) comparator
   FUN_18003ece0, ==2 FUN_1800482d0 L37104; keys descending: int +0x36c, then double +0x120 (method 1) or +0x130 (method 2),
   then int +0x360 — meaning NOT DECODED, [inferred] priority then area / size; FUN_180048660 puts "MirrorX" copies after their
   originals); outer loop over sheet types, inner over the quantity of each; per sheet FUN_1801233e0 (when params+0x3e0==1, mode
   NOT DECODED, default 0) else FUN_1800376d0 -> shake path when AccuNest (+0x378) else FUN_180037840. Sheet status out:
   1 redo, 2 next sheet, 3 sheet unusable, 4 finished.
4. **Nest one sheet** (FUN_180037840): rasterise the sheet (outline, holes, collar [inferred]) FUN_18013d380 + FUN_18003a2a0;
   per-column free extents FUN_180067cb0 / FUN_180067be0 L51767; main pass over the sorted parts (skip via FUN_180049060 or at
   the 30000-instance cap): progress callback, then FUN_18007cd10 (or FUN_180149950 for cluster / pair parts); nested count and
   area into the sheet result +0x28 / +0x38; then a **filler pass** (FUN_1800d68e0(parts,1)) topping up parts up to max-min
   quantity (params+0x34c / +0x350); optional post passes; FUN_18004a640 finalises and calls the nested-sheet-result callback.
5. **Place one part type** (FUN_18007cd10): fit type = part+0x370 if 1/2/4/13 else params+0x310 else 2 (FUN_18007de80 L61719).
   Orientations (FUN_1800820a0): read back the rasters (header checked against the resolution, message 0x264 on mismatch);
   filter by step angle (fmod(angle, step) within 0.5 deg), angle list (FUN_180084340), grain (cos/sin of params+0x860 - angle,
   FUN_18007df50) and fit-in-sheet; then `qsort(.., 0xc0, FUN_180081ba0)`: by orientation +0x38 ascending (1e-6), tie by angle.
6. **Position search** (FUN_1800f1a20): for each orientation, columns are scanned left to right (step 1; 5 when params+0x3f0+4);
   at each column FUN_1800f0fa0 finds the lowest row where every part column fits the sheet column's free runs
   (FUN_18007dc50 / FUN_180067720), iterating until no column raises the row (fixed point). The first fitting column = leftmost x.
   Selection (L110520-110700): minimise `orient+0x38/res + col - 1` (strictly smaller by 1e-6 wins), tie -> smaller y, fallback
   smallest right edge; first orientation wins exact ties. Commit FUN_180084f90 L64301: world x = orient xmin + (col-1)*res -
   sheet offsets (row flip when params+0x31c; rotation by params+0x860 when > 0.5 deg); last placement in DAT_1802b7700..7740.
   Repeated while the part still fits (do/while L≈110700). **The raster update after placing an instance: NOT DECODED**
   (candidates FUN_18013d4a0 / FUN_18013df60 / FUN_1801340e0).
7. Not active with Mastercam's settings: shake / exact improvement (`shakeNest::optExctShakeNestedParts` @180145ab0 L145602, needs AccuNest=1;
   Mastercam passes NSetAccuNest(0)); best-nesting-direction multi-trial (FUN_1800452d0); guillotine (unless the user ticks
   Guillotine cut); multitorch / master plate / shear / strip / common edge / punch / leads (no Mastercam API calls set them).

## 5. Rectangular algorithm (optiRectOrder, FUN_1801b7960 L193352)
- Part record 0x1a0 (FUN_1801b6f60 L193023 / FUN_1801e4340 L209869): +0x120 min, +0x124 max (:= min if min==0 or max<min),
  +0x130 rotate allowed, +0x134 grain, +0x150 priority (default 1), +0x158/+0x160 length/width, +0x168 category (2 if area < 3%
  of the usable sheet, else 1; FUN_1801e45e0 L209928), +0x190/+0x198 dims inflated by 2*(cutter + clearance/2). Up to 5000 parts,
  1000 sheets. Sheet record 0x1c0 (FUN_1801e3ba0 L209721): qty, length, width, four collars (RNAddSheetInfo2: one value), grain.
- **The resolution is ignored**: the ctor stores constant 1.0 at +0x20 (asm 0x1801b69c0..0x1801b6ac5), not the RNSetResolution value.
- Driver: one part type -> adds a quantity-0 "DummyPart"; walks sheet types in insertion order (no sheet priority), FUN_1801b7c30
  per sheet instance; stops when everything is nested or the last sheet fits nothing.
- Per sheet (FUN_1801b7c30 L193410): usable rect = collar + clearance/2 inset; grain mismatch rotates the part 90 deg and locks
  rotation (FUN_1801e4b60 L210140); **2 passes (sheet as is / sheet turned 90 deg) x 3 trials** (L194027): fill direction 2 if
  L >= W else 1; the other direction; direction 3 = randomised first row. Each trial = FUN_1801bbde0 -> utilisation; a trial is
  kept only when better by > 1e-6 (ties keep the earlier one).
- One trial (FUN_1801bbde0 L194794): 4 "dummyPart" walls (3% of L or W thick) outside the usable rect; candidate-segment list
  = the bottom edge (FUN_1801bf1c0); distinct priorities qsorted **descending** (FUN_1801c5ea0 L198550); per priority
  FUN_1801bd690 for category 1 then 2; then a filler phase (remaining := max-min when the minimum was met) over the same loop,
  restricted to the nested height/width when fillers-up-to-nested-height is on.
- Greedy step (FUN_1801bd690 L195334, cap 10000 rects): each eligible part, at every candidate segment, 0 deg and (if allowed)
  90 deg (FUN_1801be190 L195601): place at the segment's left end, slide right past overlaps (FUN_1801be7d0 L195769 /
  FUN_1801e4d80), fail if outside. **Score = contact %** (overlap of one-wall-thick strips around the part with placed rects and
  walls / perimeter strip area, FUN_1801e5000). Max score wins; ties -> smaller normalised centre along the fill direction, then
  larger area, then lower index. Segment update FUN_1801bf6c0 L196179 (cut / trim / split FUN_1801e5320, merge top edge
  FUN_1801e5660). = a skyline bottom-left fill with a touching-perimeter score.
- Randomised first row (FUN_1801c3a40 L197851, trial 2, category 1, empty sheet only): `srand(0)` (FUN_18022bc38 L197961 sets
  _holdrand), 3000 iterations of k = rand()%(2n) picks idx = rand()%(2n) (idx >= n = rotated); minimise leftover length; the
  best row is placed left to right.
- Identical sheets (FUN_1801bf4c0 L196132): the layout is repeated k+1 times, k = min over the part types of
  floor(remaining / nested here), capped by remaining sheets - 1.
- Results: the part-nested callback (name, x, y, rotated) per instance and RNInqNestedPart: x = rx + half gap (FUN_1801c10f0).
- Size: ~199 functions / ~8.5k decompiled lines (core 0x1801b65e0-0x1801c6060 80 fns / 5.8k; helpers 0x1801e3b40-0x1801e5800
  37 / 0.7k; back-ends 0x1800e6e10-0x1800e7940 37 / 0.5k; RN API 45 / 1.4k mostly logging). Much of FUN_1801b7c30 is dead code
  under DAT_1802b32c8 (never written, bss 0). Live logic ~3-4k lines; port ~1.5-2.5k lines of C [inferred].

## 6. Determinism
- **RNG**: only MSVC `rand` (L251059: `h = h*0x343fd + 0x269ec3; (h>>16)&0x7fff`), only in FUN_1801c3a40, seeded with the constant
  0 each time (srand call site 0x1801c3bef). No other PRNG constants in code (searched 0x41c64e6d, 69069, 16807, 0x5DEECE66D, MT).
- **Time**: clock() feeds only the timing accumulators FUN_180033470..FUN_1800338f0 (DAT_1802b3e00 / DAT_1802b0ca0); the sum
  DAT_1802b3300 (L9002, L38220) is never read. GetTickCount / QueryPerformanceCounter only in __security_init_cookie (L258635);
  GetLocalTime only in the NestWin.log writer FUN_180009170 (L3645). OPTIMIZER_MAXTIME / OPTIMAL_NESTING_MAX_TIME /
  TIME_PAUSE_POINT are parsed only by the .nip reader (FUN_18020dec0 L228799, FUN_18020fbb0 L229952, FUN_180210b00 L230577).
- **Single-threaded** (no CreateThread import).
- **Therefore both engines are deterministic** for a given input and parameter set [inferred from every reference found],
  except: the user's Esc (Mastercam callback returns 0) and Mastercam's local-area replies (§2).
- Bit-exactness caveats for a port: (a) 8 qsort comparators (FUN_180077a20/ae0, FUN_180081ba0, FUN_180115fd0, FUN_18011d810,
  FUN_18019c630, FUN_1801aacc0, FUN_1801c5ea0), several never return 0 -> port MSVC's qsort (@180229ce0) rather than glibc's;
  (b) TrueShape uses the MSVC libm sin/cos in the separate text section -> port those polynomials (or accept last-bit differences
  that can flip a raster cell); (c) the rasters are written to and read back from files in the data dir (`.cmp*`), so the
  in-memory representation must match what the reader expects, not the file format itself.
- TrueShape's uninitialised-memory behaviour: not audited.

## 7. Verification (oracle)
- **Replay through the original engine is possible**: the DLL exports `runNestCommon(path, int *status, ..)` @180001220 L162 and
  `runNestWin` @180001180 L145. runNestCommon accepts a `.NIP` file (FUN_180001a00 L404: NipReader FUN_180024a40, then
  NNestOrder or RNNestOrder) or `.PRM`, and writes a `.nop` result (sprintf "%s%s.nop" in FUN_18000a900 L4444; writer
  FUN_1800022f0 L698 prints per sheet UTILIZATION and per instance XMATRIX / YMATRIX / XLL / YLL / ANGLE / MIRROR / IDENTIFIER,
  L932-1080). Mastercam's NE FUN_180035970 already writes this exact .nip format (nesting.nip). So: our port writes a .nip for
  each test case; the original n64dll.dll nests it and writes the .nop; the port must reproduce the .nop. Whether the .nip runner
  needs the licence key (NSetDLLEnableKey) is NOT DECODED — a harness can call NSetDLLEnableKey itself before runNestCommon, or
  drive the C API directly with the same calls NESTING.DLL makes.
- Also available: with env `NESTLIB_MODE=Geometric` the engine logs every API call (§2) — a trace to diff call sequences.
- **Running the DLL here**: no wine on this machine (checked 2026-11-05); x86_64-w64-mingw32-gcc is installed. Options:
  (1) install wine (user decision) and run a mingw-built harness; (2) a minimal PE loader on Linux (the DLL imports only ~120
  KERNEL32/USER32/GDI32 functions, mostly CRT plumbing; stubs + SysV<->MS-ABI thunks); (3) the user's Windows install.
- Stored test inputs: `<windows>/mcamx/documentation/ExampleParts/BUMP.NSTX` (Mastercam X3 session: 1 sheet 24 x 12,
  11 parts with contours, step 10 deg, min quantities) — inputs only, **no stored results** anywhere on disk (searched *.nstx /
  *.nip / *.nop under ~, <data-disk>, <windows>). The sheet libraries STANDARD/METRIC SHEETS.NSLX are inputs too.
- Without running the DLL there is **no** oracle for layouts; unit tests of single primitives (offset, raster, contact score) can
  only be checked against hand calculation.

## 8. Port plan
Scope (only what Mastercam's calls activate): TrueShape ~1673 functions / 151k decompiled lines reachable, of which the path
live under Mastercam's parameter values is [inferred] 400-600 functions / 40-60k decompiled lines (rough rule: decompiled
lines / 2-3 = C lines -> ~15-25k lines of C). Rectangular ~1.5-2.5k lines of C.
1. **Rectangular engine first** (small, self-contained, decoded end to end above): `rect_nest.c` with the RN* API, the
   segment / contact-score greedy fill, the srand(0) first-row search, identical-sheet repetition, MSVC rand + qsort. Verify
   with hand cases, then via the oracle (§7) when available.
2. **Shared infrastructure**: paged arrays -> plain arrays, error codes / messages (FUN_1800537a0 table), the parameter block
   (nestParameters with the derived fields of FUN_18005f9c0), the API state machine; skip logging, the licence gate, CRT/STL.
3. **TrueShape geometry**: entity store (line / arc / circle), drawings, offset (FUN_1800b00d0 -> FUN_1801a9bb0 / FUN_18019cbe0),
   negCollar sheet offset, rotation. Testable in isolation (closed-form offsets).
4. **Rasteriser + orientation cache** (FUN_18010bbe0 / FUN_18010df80 / FUN_18003a2a0), keeping the caches in memory.
5. **Placement core**: sort (FUN_180048210), sheet loop (FUN_180033980), nest-one-sheet (FUN_180037840), FUN_18007cd10 ->
   FUN_1800820a0 / FUN_1800ec190 / FUN_1800f1a20 / FUN_1800f0fa0, raster update, filler pass, finalise / results.
6. Then the options Mastercam exposes: FitPartInPart / prefer hole fill (FUN_180036090, the +0x308 hole rasters), guillotine
   (post passes), clusters (0x18012xxxx), groups, exact distance; last NESTING.DLL's UI glue (wireframe.md).
Not ported: .nip / DXF readers, profile utility / overlap / offset utility APIs, multitorch, master plate, shear, strip, punch,
cutter path, common edge, PDF (only a file-name setter exists, NSetPDFFileName @180015a20), AccuNest shake, best-direction trials.

## 9. TrueShape port: errors, C++ exceptions, resources (2026-11-05, review of the glue)
- **C++ exceptions** (viewer/nest/ts/gen/eh.py, ehdump.py): NestLib reports errors by throwing nestError* objects
  (FUN_1800537a0 / FUN_180052fd0 build them: code, severity, kind, message at +0xc) and catches them in 141 of the ported
  functions (the API entry points catch nestErrorPrm / Gen / Prt / Sht / Dwg / Ord, report through FUN_180053920 =
  the error table + the error callback, and return their failure value; NDwgAddLine / NDwgAddArc / the drawing reader
  catch per entity and go on with the other parts: "Line is too small. ( x, y ) to ( x, y ).", "Arc is too small. ...",
  "Part : %s. Can not get the part drawing."). The generator reads each function's FuncInfo (try blocks: state range,
  catch types, funclets) and IP-to-state map from the DLL, splits the function into FUN_<a>__b (the decompile; every call
  of a function that can throw records the DLL state of that call instruction) and FUN_<a> (pushes a ts_eh frame,
  setjmp; on a catch the funclet + the continuation). The catch body + continuation are read from the machine code when
  they are the usual pattern (copy the object, FUN_180053920, constant stores to the result variable / globals, the no-op
  destructor FUN_180010690, return the variable / a constant / nothing); 6 others are hand ports in gen/ehfix.txt (2 of
  them in place in the function, gen/patches.txt). `ts_cxx_throw` matches the thrown class (the ThrowInfo's catchable
  types, tabled at generation) against the frames from the innermost, as msvcrt's FindHandler. catch(...) blocks that only
  free a buffer and rethrow are dropped (the throw passes, the buffer leaks); unwind actions (destructors of the frames a
  throw passes) are not run - in the ported functions they are all free() / container destructors (ehdump.py --unwind),
  so memory only. No handler: ts_fatal with the object's own message.
- **Message arguments** (gen/vaargs.py): Ghidra gave the two error builders 4 parameters, so the decompile dropped the
  printf arguments of the messages (the port printed "( 0.000000, 0.000000 )" where the DLL prints the points). Every
  call's argument slots [rsp+0x20+8k] (and r9 for FUN_180052fd0) are traced in the machine code to a frame slot / global /
  immediate and appended to the C call (138 calls); an untraceable one stops the generation.
- **Generation checks**: a (longlong) cast of a double in a function with 64-bit cvttsd2si (gen/cvt64.txt) must be a
  conversion (the reference decompile has it, or it is narrowed at once) or be listed as a raw copy, else gen.py stops
  (FUN_1800eeb00's 13 and FUN_1800f1a20's 2 sites had become bit copies; the FUN_1800eeb00 branch, sheet +0x290 == 1, is
  not reachable from NESTING.DLL's calls: NAddSheetInfo's name setter FUN_180077ba0 L58960 sets it to 0 and nothing else
  writes it - checked on the DLL with the coverage tracer); regen.sh fails on pointer-to-int casts in ts_gen.c (an image
  address folded into a constant, FUN_18010df80 `+ 0x7fd4c051` = `- &DAT_1802b3fb0 + 1`, now patched).
- **ts_fatal** (NOT PORTED, a test hook, an uncaught throw) longjmps to nest_engine_ts (jump target set before the engine
  is loaded): the run's work directory DAT_1802b0ca0 = <dir>/tmpnst<k> is emptied and removed (FUN_1800577a0 + _rmdir, as
  NOpenNestLib does with a leftover one, L4602-4606), every block / stream / directory listing the engine holds is
  released (ts_crt_release) and the image is loaded again for the next run.  NOpenNestLib(dir, 1) itself cleans leftover
  tmpnst1..9 (FUN_1800577a0 ported: _findfirst / _findnext / remove).
- **The engine's heap and streams** (ts_crt.c / ts_rt.c): the engine's blocks come from its own arena (mmap'd chunks, free
  lists and the set of live blocks outside the blocks) and its streams are listed, so a release drops everything.  The DLL
  itself is not clean when the progress callback stops a run at some points: seed-17 x003 stopped at the 40th progress
  call closes a stream twice (the DLL under the coverage loader aborts in glibc's free there); MSVC's fclose of a closed
  stream returns EOF, the port does the same (closed TSFILEs are kept until the release), and a free of a block that is
  not live is ignored (stderr) - neither can reach the host's heap.
- **Process-global, one thread**: the engine is the DLL's static data image (ts_img) plus the CRT state - one engine per
  process, not reentrant, not thread-safe (the DLL is the same: NESTING.DLL calls it from Mastercam's UI thread).
- **Cancel**: NESTING.DLL's progress callback asks KeyPress(0x1b) first and answers 0 (the engine stops) when Esc was
  pressed (FUN_180006700 L4613-4616). Ported as the hook `nest_ts_cancel` (nest_job.h); the test drivers set it with
  NEST_CANCEL_AT=n (both sides of the job oracle). The app sets it during a TrueShape run (cui_nest.c ns_run: nest_ts_cancel =
  KeyPress(0x1b) through cui_hooks.key_press). KeyPress (mastercam.dll @1803221b0, decomp/getevent/mastercam_keypress.c 7-28):
  PeekMessage(0x100..0x109, PM_REMOVE) of one keyboard message; a key-down (0x100 / 0x104) of the asked key -> 1; another key-down
  goes to FUN_1803222f0 (decomp/mastercam_keyrot2.c, ported as ViewInput::viewKey: Alt+arrows rotate, arrows pan, Page Up / Down
  zoom, Home fits - only while the graphics window has the focus); another keyboard message is removed and ignored. All other input
  stays in the thread's queue and is dispatched after the run. Qt (mainwindow.cpp hook_key_press / hook_key_press_end): a filter keeps
  every user input event in arrival order while the run's calls deliver the window system's events; each call takes the first keyboard
  event of that queue as above; at the run's end (cui_hooks.key_press_end, after the engine in ns_run) the rest is posted again in
  order; g_regen_busy meanwhile. (Corrected 2026-11-05: the first port dropped mouse input.) The Rectangular engine never calls its progress callback, so Esc does not stop it. After a
  stop with no result the driver's no-result path shows 10102 + 10103 and returns to the dialog (L42647). Scene xf_nest_esc_run
  (step nestescat:N: an Esc key-down posted before the N-th KeyPress call, ours).
- **Oracle cases** (tools/tests/nest_ts_job_oracle.sh): family e = lines 1e-7 .. 1e-3 long (the limit is between 1e-4,
  "too small", and 1.1e-4), arcs / circles of radius 1e-5 .. 1e-3 (limit between 1e-4 and 2e-4), open profiles (outer,
  with a gap, alone, as a hole); seq = 6 runs in one process with a leftover tmpnst1, a run stopped by ts_fatal followed
  by runs equal to a fresh process's, Esc at the 1st / 5th / 40th progress call.

## NOT DECODED (phase 2 work list)
TrueShape: raster scan-conversion and the raster update after placement; the `.cmp` record layout beyond headers; meaning of the
sort keys (+0x36c / +0x120 / +0x130 / +0x360) and of fit types 1/2/4/13; FUN_1801233e0 (+0x3e0 mode); FUN_18003a2a0 internals;
FUN_180139640 / FUN_18013c770; FUN_180036090 hole pre-pass; groups and user (type 4) clusters (the type-7 Optimized-pair clusters run ported and oracle-checked through the job path, nesting_addin.md §12); NAddPartInfo p11 / p12 and NAddSheetInfo's exact
argument order; the instance-record field meanings (x / y / angle / mirror); NInqNestedSheet field meanings; negCollar bodies;
outer / hole classification (FUN_180099fe0 L74114). Mastercam side: DAT_180135e89 (local-area mode) and part+0x51a; the
values NESTING.DLL passes for every argument (wireframe.md lists the calls only).
