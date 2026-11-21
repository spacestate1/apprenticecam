# EStamp7.exe - the Engrave toolpath engine (decode + C port)

Status 2026-10-26. Companion of `formats/engrave.md` (engrave.dll: op record, dialog, EST_DAT writer). This file: the engine
`EStamp7.exe` that engrave.dll runs, the engrave.dll code that turns the engine output into moves, and the C port
`viewer/engrave.{h,c}` with its oracle `tools/tests/engrave_test.{c,sh}`.

Sources. `EStamp7.exe` (32-bit Delphi, HCAM / eStamp by AHSoft; copy in `~/mastercam-re/files_engrave/`). Decompiles in
`decomp/engrave/est/` (gitignored): `p_4b0000.c` (application units 0x4b0000-0x52b000) and `p_460000.c` (HCAM base units
0x460000-0x4b0000), made from Ghidra project `~/mastercam-re/ghidra_estamp2` (program `EStamp7p.exe` = the exe with every
`call 0x403a68` / `call 0x403a60` - Delphi's FDIV-bug-safe division helpers - replaced by an inline `fdivp` / `fdivrp`, imported
with the `borlanddelphi` compiler spec (register convention EAX, EDX, ECX, rest pushed left to right) and the System math helpers
0x403350 Cos, 0x403360 Sin, 0x403370 ArcTan, 0x403380 Sqrt, 0x403390 Round, 0x40339c Trunc marked inline). Citations
"`p_4b0000.c` l.N" are line numbers in those dumps; x87 formulas were read from the disassembly at the cited addresses
(Ghidra does not model ST0 returns). engrave.dll citations: `decomp/engrave/*.c` as in engrave.md, plus
`decomp/engrave/contour_build.c` (FUN_18001e400, FUN_180041910, FUN_180046920, FUN_18001de90 / FUN_18001df60 ...).
Delphi unit names (PACKAGEINFO resource, 303 units) show the engine's modules: `EStDaten`, `EStDRdWr` (EST_DAT),
`AKorrBer`, `AKorrAbr`, `AKorrEin` (auto correction), `HrKorrD_`, `HRKBAHN_`, `HrKUprg` (offset, "HRKORRD" in its messages),
`HSchraff` (hatching), `HKont2D`, `HKontur`, `BahnOp`, `BahnS`, `LG1Obj`, `HFrOff2D`, `HKorrU1`, `HKorrU2D`, `HSpirale`.
Everything not traced to code or measurement is marked **NOT DECODED**; the port refuses those cases.

## 1. Run

engrave.dll (formats/engrave.md 1.1) writes `ENGCONT.LG1` + `EST_DAT.TXT`, runs `estamp7.exe <key>` in the data folder and
reads `FEHLER=` and `ENGTOOLP.LG1`. The exe's main path (FUN_00529e94, `p_4b0000.c`):

1. FUN_00523bf0: read EST_DAT.TXT (parser FUN_005229b0) into parameter blocks and save them with FUN_0049ace0(id, buf, size)
   (blocks 0x17, 3, 7, 0xf, 0x11, 0x2b - sec. 2); FEHLER writer FUN_00522164.
2. FUN_004dd3e4: load `Quelle` (ENGCONT.LG1) into the working layer; then, when Bahnoptimierung, FUN_00529700 ->
   FUN_0050fa18 path optimisation of that layer (sec. 7.1a).
3. FUN_00529514: the contour box; FUN_00529e38 -> FUN_00529b50 (engine tool rows, checks) -> FUN_0052919c (the
   computation, sec. 3).
4. FUN_005299e8: depth cuts (when SpantZustellung). FUN_00529884: write `Ziel` (ENGTOOLP.LG1).

## 2. Parameter blocks

Parser FUN_005229b0 keys, in its test order: QUELLE ZIEL ZIEHWND GRENZE_X0 GRENZE_Y0 GRENZE_X1 GRENZE_Y1 BAHNOPTIMIERUNG
BOMINDESTAUSD BOMINDSTUETZP BOBAHNENSCHL BOVEKTORAUFL KREISAUFLOESUNG SPLINEAUFLOESUNG DXFSKALFAKTOR KABRUNDUNGSWINKEL
KGEGENLAUFFRAESEN KABRAEUMUEBERLAPP KPARAMNORMGR KDATENCHECK KBLOCKWEISE KSTARTINNEN KSTARTPUNKT KABRAEUMZUERST KZEILENVERBINDEN
KNACHSORTIEREN KABRWEGZUSAMMEN KABRZEILENRUND WERKZEUGNR LNAME LEINTAUCHT LFLUGHOEHE LFRADIUS LFWINKEL LFSCHAFT LVORSCHUB
LEINTVORSCHZ LSPINDELDREH LWECHSELSTAT KORREKTURVERF SPANTSCHICHTZ SPANTAUFTEIL SPANTEINTWI ABRAEUMVERF ABRAEUMWINKEL KONTURENAUF
GRAVURTYP SPANTZUSTELLUNG SPANTSPIEGELT SPANTABRSCHICHTZ SPANTABRAUFTEIL SPANTABREINTWI (key strings at 0x523770..0x523ab4).

| block | field | key / meaning |
|---|---|---|
| 7 (0x6b) | +0 f32, +4 copy | KAbrundungswinkel (deg) |
| | +8 f32, +0xc copy | KAbraeumueberlapp (%) |
| | +0x14 / +0x15 / +0x16 / +0x17 | KGegenlauffraesen / KParamNormGr / KDatencheck / KBlockweise |
| | +0x18 | KStartpunkt (KStartInnen writes 1 / 2 there first) |
| | +0x19 / +0x1a / +0x1b / +0x1c / +0x1d | KAbraeumZuerst / KZeilenVerbinden / Knachsortieren / KAbrWegZusammen / KAbrZeilenRund; Knachsortieren := 0 when KAbrWegZusammen (parser l.83772-83774) |
| 3 (0x78) | +8 / +9 | Kreisaufloesung (26) / Splineaufloesung (10) |
| 0x17 (0x1c) | +0 f32 / +8 f32 / +0xc f32 / +0x10 bool | BOVektorAufl (10000) / BOMindStuetzp (4) / BOMindestausd (0.05) / BOBahnenSchl (JA) - parser 0x522ba6 / 0x522b7d / 0x522bf7 / 0x522bcf, defaults 0x523cf1..0x523d0c (block base [ebp-0x6ed8] of FUN_00523bf0); +4 is not written by the parser (the value block 0x17 held before; not read by FUN_00529700) |
| (FUN_00529e94 [ebp-4]) | | BAHNOPTIMIERUNG (parser 0x522b55 stores it at +0x204 of the name block [ebp-0x208]; default 1 at 0x523c43); tested at 0x529f19 |
| 0x11 (0x31) | +4 | Abraeumwinkel (deg; FUN_00529b50 passes it on in radians) |
| | +0x10 | 10 x {i16 tool index, char KorrekturVerf} |
| | +0x2e / +0x2f / +0x30 | AbraeumVerf ('Z' zeilebi, 'Y' zeileuni, 'C' Radius, 'D' Durchm, 'K' keine; unknown -> 'Z') / KonturenAuf ('S' Spiegel, 'G' Grund) / GravurTyp ('E' erhaben, 'V' vertieft) |
| 0xf (28000) | 0x8c per tool | +1 LName, +0x2a LEintauchT (depth D), +0x2e LFlugHoehe, +0x32 LFRadius (tip radius), +0x36 LFWinkel (half angle), +0x3a radius of a rounded tip transition, +0x3e LFSchaft, +0x42 LVorschub, +0x46 LEintVorschZ, +0x4a LSpindelDreh, +0x52 LWechselStat |
| 0x2b (0x51, DAT_005369b0) | per tool slot (11, slot 10 = roughing) | i16 SpantSchichtZ, char SpantAufteil ('L' for 0, 'V' otherwise), f32 SpantEintWi; f32 SpantSpiegelt, bool SpantZustellung |

KorrekturVerf values: `2D` -> '2', `2D3D` -> '3', `3D` -> 'A', `ECKE` -> 'E', other -> '3'.

Engine tool rows (FUN_00529b50, 10 x 0x1d bytes): +0 i16 library index (< 0 = none), +2 i16 (lib +0x54), +4 D, +8 fly, +0xc
tip radius rt, +0x10 angle a, +0x14 lib +0x3a, +0x18 **offset radius** = rt for Grund, else FUN_004a2054(lib) (below),
+0x1c KorrekturVerf. Checks (error = 0x38e + code): no tool -> -1; ... (sec. 7).

FUN_004a2054 (0x4a2054-0x4a2233), the radius of the tool at depth D: R = rt; if the transition radius Rb (+0x3a) > 0:
s = (Rs - rt) / Rb, phi = (s >= 1) ? (90 - a) deg : min(asin(max(s, 0)), (90 - a) deg); h = (1 - cos phi) Rb; if h > D:
return min(rt + sqrt(Rb^2 - (Rb - D)^2), Rs); else R = rt + Rb sin phi. Then if a > 0: R += tan(a) (D - h), capped at Rs
(flag lib +0x8b = 1). For a plain V tool R = rt + D tan(a) (the measured coaster offset, engrave.md 7).

## 3. The offset precomputation (envelope): FUN_005041a4 -> FUN_00501550

Port: `viewer/engrave.c`, the section marked "port of eg_pre.c" (the decode was ported as that module; its file-scope helpers carry the prefix `pre_` in engrave.c, the public `eg_*` functions keep their names). Self-check paths below refer to the decode work area, not the repo.


Dump = `decomp/engrave/est/p_4b0000.c` (line numbers, `@N`); `dis X` = annotated disassembly from `dis.py`.
Port: `eg_pre.c / eg_pre.h` (this directory). Self-check: `test_pre.c`, `test_op2_3d.c`.
Brief correction (agreed with mod_offset): FUN_004bbcf4 = Reset, FUN_004bbd68 = Rewrite.

### 0. Summary
The precompute builds the offset in two layers:
1. **Contour preparation** (FUN_004fd510). Deduplicate points, shift the polygon 0.0002 away from the offset side,
   nudge sharp vertices, then **replace each convex corner with a polygonal arc of radius 0.001**, with
   n = trunc(|turn| / KAbrundungswinkel) + 1 segments. Each vertex gets a class (tag 0/1/2). The result goes to
   VekAR7. A second copy, shifted by W (the "O" offset polygon, mitered at tag-0 vertices), is kept in memory.
2. **Envelope** (FUN_005013cc). For each segment j, the region of points within W on the offset side that are
   closer to j than to any other segment *line*. That region is bounded by its own O line and by line-line
   bisectors with other segments. Each bisector is clipped to both segments' perpendicular bands. The region's
   boundary is traced, and its vertices are written as VekAR6 samples with d = height/W.

The **corner geometry of the final offset comes from step 1**. Corners whose turn is at least the
Abrundungswinkel are rounded; smaller ones are mitered. Point-to-vertex (parabolic) distances are **not**
modelled anywhere. Concave corners are handled only by line-line bisectors.

### 1. Entry and parameters
- Call site 528e2c-528e5b (dump 87210-87211). Pushed in order: parent[-0x80] (W, FUN_00504268) → [ebp+0x10];
  (float)(0.017453292519943295 · parent[-0x68]) → [ebp+0xc]; 0x3f20d97c = 0.62831855 → [ebp+8].
  EAX = "VekAR1.TMP", DL = parent[-0x58].
  - parent[-0x6c..] is block 7, read at @86936, so parent[-0x68] = block 7 +4 = KAbrundungswinkel (copy).
    The engrave.dll roll setting gives Sharp → 20, None → 150 (engrave.md 3.2).
- FUN_005041a4 @59358 only copies the name and forwards to FUN_00501550 @57396 (dis 5041bd-5041cc).
- FUN_00501550:
  - W → *PTR_DAT_0052f24c (= DAT_00535960, float) @57428.
  - **sd = DAT_005359c0 = side ^ 1** @57430. sd is the side argument of FUN_004fbfdc (0 = (dy,−dx), right of
    travel; 1 = left).
  - bbox min = (1e6, 1e6), max = (−1e6, −1e6) @57431-57441.
  - FUN_004fd7dc(name, sd, [A=0.628, B=abr]) @57443. That pushes [ebp+0xc], [ebp+8] (dis 501617-50162a) and
    stores sd at its [ebp-0x155] (dis 4fd7f5).
  - Then VekAR6 is rewritten with 25-byte records and VekAR5 with 4-byte records @57446-57449, the grid is built
    (FUN_004fde5c) @57453, and the envelope (FUN_005013cc) @57461.
- Global pointer aliases (read from .data): 52f0e4→DAT_00535968 (C table), 52f07c→DAT_0053596c (O table),
  52f354→DAT_00535970 (work buffer / edge list L), 52f24c→DAT_00535960 (W), 52f26c→DAT_00535964 (grid cell),
  52f318→DAT_0053598c (bbox min x, y), 52f2cc→DAT_0053597c (bbox max x, y), 52ed34→DAT_00535974 (total segments).
- Tables (0x27102 bytes, @57420-57423): i16 count; i32 start[20000] at +2 (cumulative segment index); ptr[20000]
  at +0x13882, each pointing to an i16 count followed by 0x2e-byte records.
- **Record (0x2e bytes):** +0 tag (vertex class), +1 axis flag (1 if |dx| ≤ |dy|, FUN_004fbdec @52900),
  +2 i32 id, +6 x, +0xe y, +0x16 dx, +0x1e dy (f64), +0x26 slope f32 (dx/dy or dy/dx), +0x2a angle f32 in
  [0, 2π) (FUN_004fc098, dis 4fc098).
- Record i is vertex i *and* segment i → i+1. Contours are closed implicitly.

### 2. Contour preparation (FUN_004fd510 @54011, nested in FUN_004fd7dc @54122)
Input: VekAR1.TMP as 13-byte records (FUN_004c3ae0 @14567). Fewer than 4 records → **error 100** @54035.
1. **Line-to (0x0a)** @54053-54080, dis 4fd5fb-4fd6e7. Let ax = |last.x − p.x| and ay = |last.y − p.y|
   (as f32). If ax > 0.003 or ay > 0.003, then:
   - if ax < 0.0005, last.x += 0.001; else if ay < 0.0005, last.y += 0.001. *The previous point is moved.*
   - Append p (f32 → f64).
   Otherwise p is dropped. More than 0x7ffd points → **error 2**.
2. **0x0b** @54082-54092. If the buffer holds more than 2 points, process the previous contour. Then start a new
   one. The last contour is processed at the end (@54098), and start[count] = total (@54108).
   VekAR7 gets a sentinel 0x0b (FUN_004fcd88 @53770, x/y/z are uninitialised stack bytes).
3. **FUN_004fd3a4** (dis 4fd3a4) shifts the polygon away from the offset side. off = sd ? −0.0002 : +0.0002
   (0xb951b717 / 0x3951b717), times the left normal.
   - For vertex i: with prev = T[i−1] (T[n−2] for i = 0) and next = T[i+1], call FUN_004fd1d0 (dis 4fd1d0).
     The vertices processed are T[0], T[1..n−2], and again T[0]. T[n−1] is assumed to be the closing duplicate.
   - FUN_004fd1d0: shift both adjacent segments by off·left-normal (f32 vertices). Intersect the two shifted
     segments with FUN_004fd07c (dis 4fd07c): t, u ∈ (0, 1], or the midpoint if det = 0.
     - If they intersect, the vertex is the intersection.
     - Otherwise, if the line intersection is within 0.00022 (0x3966afcd) of the first shifted copy of the
       vertex, use it.
     - Otherwise use the midpoint of the two shifted vertex copies.
4. **FUN_004fcec0** (dis 4fcec0). c = 0.00042424168043941895. Call FUN_004fcda4 (dis 4fcda4) on
   (rec[n−2], rec[0], rec[1]), then on (rec[i−1], rec[i], rec[i+1]) for i = 1..n−2. It returns 1 when
   |u/|u| + v/|v||² < 3.532 (f32), i.e. the turn is above ~40°. For each such vertex:
   - add (c, c) to the vertex (for vertex 0, also to the duplicate);
   - c += 0.000424…; inside the loop, if c ≥ 0.00155555 then c −= 0.00155555.
5. **FUN_004fcca0** (dis 4fcca0):
   - count−− drops the closing duplicate.
   - a = (double)abr ([ebp-8]).
   - Loop: error := the error at entry, then FUN_004fc2a4. On error 2 (overflow):
     - FUN_004fab08 eps: eps starts at 0.0005; if eps < 0.0045 then eps ·= 3.333, else ·= 1.5.
     - If a < 0.698 (40°), a += 0.0873 (5°).
     - FUN_004fcb6c reduces points through FUN_004fab4c (decoded 2026-11-19, sec. 3.7.1).
   - Then FUN_004fc6c0, FUN_004fc890, FUN_004fcb24, and total += n.
6. **FUN_004fc2a4 corner arcs and vertex tags** (dis 4fc2a4-4fc688):
   - First recompute d, flag and slope (FUN_004fbe68) and the angle (FUN_004fc098) for all records.
   - Walking the vertices: t = FUN_004fc190(prev_ang, cur_ang) = (f32)(cur − prev) wrapped to [−π, π]
     (dis 4fc190).
   - **Convex for the offset side, (t > 0) xor sd** (dis 4fc33c). tag = 0. If |t| ≥ a, insert an arc:
     - L64 = previous segment record and L94 = current one, each shifted by 0.001 to side sd^1 (FUN_004fc060).
     - C = their line intersection (FUN_004fbe84). If |C − V| > 0.0017, clamp C to V + (C − V)·0.0017/|C − V|
       (f32).
     - m = Trunc(|t|/a) + 1 (dis 4fc4b3-4fc4c9). Shift the following records by m and **overwrite vertex i..i+m
       with C + 0.001·(cos θ, sin θ)**.
     - θ starts at prev_ang + π/2 (sd = 1) or prev_ang − π/2 (sd = 0), in steps of (f32)(t/m). The new points
       get tag 0.
     - Recompute records (n+i−1 .. n+i+m) mod n, then skip i += m.
     - If count ≥ 0x7ffe → error 2 (count is restored).
     - If the previous vertex also got an arc, L64 is that vertex's L94 (flag [ebp-0x25]).
   - **Otherwise** (concave or straight): tag 0 if |t| < 0.01745329 (1°); tag 1 if |t| > A (0.62831855, i.e.
     36°); tag 2 otherwise.
   - Result: the offset of an arc of radius 0.001 at distance r is a polygonal arc of radius r + 0.001. This is
     how **KAbrundungswinkel acts: as the subdivision step, and as the threshold (turns smaller than it stay
     mitered)**.
   - **The 0.62831855 argument acts only as the tag 1 / tag 2 boundary for concave corners.**
7. **FUN_004fc6c0** @53394: start[c] = total. Copy the records into the C table. Write VekAR7: 0x0b for the
   first point, 0x0a for the rest, z = 0 (local_13/local_f = 0).
8. **FUN_004fc890** @53503:
   - bbox ∪= the points. Shift each start point by W to side sd (FUN_004fc060 with (double)W). The direction is
     unchanged, so the flag, slope and angle stay those of C.
   - At tag-0 vertices, FUN_004fc7a0 (dis 4fc7a0) intersects the previous and the current offset lines, accepting
     t > 0.1 and u < 0.9. On success: cur.d = cur.d + cur.p − P, cur.p = P, prev.d = P − prev.p. For vertex 0, the
     last record's d is set to rec0.p − rec_last.p.
   - Second bbox pass on the shifted points (if/else-if form).
   - FUN_004fcb24 @53633 copies the records to the O table. More than 19999 contours → **error 3**.

### 3. Grid (FUN_004fde5c @54512, dis 4fde5c-4fe02x)
- cell = (f32)(1.1·W) (FUN_004fd7dc @54143).
- nx = Trunc((max.x − min.x)/cell) + 5. While nx ≥ 127: cell ·= (nx + 5)/128, recompute nx.
- ny likewise. While ny ≥ 127: cell ·= (ny + 4)/128, recompute nx and ny.
- min −= 2·cell. The table is 128×128 i16 counts.
- Each segment registers its id in every cell crossed by the 4 border lines of its band: C, O, C.end→O.end, and
  C.start→O.start (@54614-54657). Rasterizer FUN_004fdb88 @54289 (dis): grid coordinates via FUN_004fd920
  @54205, cell = Round (fistp).
  - y-dominant lines walk the columns; x-dominant lines walk the rows.
  - The crossing at k + 0.5 is Round((k + 0.5 − c)/slope), with c = (f32)(g0 − slope·g0'), and the cell range
    between consecutive crossings is filled.
- FUN_004fd974 @54221 appends the id unless it equals the cell's last entry.
- Bucket memory 0x1f4000 bytes with overflow to VekAR3.TMP and error 9: **not modelled**. The port uses unbounded
  lists; the order is the same.

### 4. Envelope (FUN_005013cc @57306; per segment FUN_00500cac @56966)
- Loop over the contours, then the segments:
  - write the VekAR5 entry = number of samples so far (FUN_004fe1e4 @57357);
  - when j = 0, set first = 1;
  - process the segment.
- At the end of a contour, write **0x0a (first sample of the contour)** @57372-57374. After the last contour,
  write **0x0b (that sample again)**, then the final VekAR5 entry @57382-57386. Both records count in the
  last segment's range.

**Edge list L (FUN_004ff874 @55830):**
- Candidates come from FUN_004fe7f4 @55103, which rasterizes the current segment's 4 band lines with FUN_004fe534
  (same code as fdb88). Callback FUN_004fe3d8 @54827: cells with **> 1** id are merged into an ascending unique
  list (FUN_004fe33c @54790; error 6 above 0x1fff).
- For each candidate id: contour = last k with start[k] ≤ id. Then FUN_004ff658 (dis 4ff658):
  - Δ = fc190(cur.ang, cand.ang). **|Δ| < 1° → rejected** (parallel, same direction).
  - Same contour: reject the same segment. Reject the next segment if its start vertex has tag 0, and the
    previous segment if the current start vertex has tag 0. Neighbours across tag 1/2 vertices stay in.
  - **|Δ| > 3.1398473 (antiparallel), FUN_004feb6c (dis 4feb6c) + FUN_004fe940 @55196 + FUN_004feaa0 @55244:**
    1. Midline from mid(C.start, X1) to mid(C.end, X2). X1/X2 are where the band side lines meet the other
       segment's line.
    2. Clip it to the current band's start side, which must hit within the side (fe214, else rejected), and
       end side.
    3. Intersect it with the other band's sides (R0, R1). Interval test and clip on the dominant axis of the
       current segment, sign-dependent (dis 4febd4-4fed93).
  - **Otherwise, bisector:** P = C∩C' and Q = O∩O' (FUN_004fbe84, |det| < 1e-19 → 1e-19). Edge P→Q.
    - Clip to the current band: FUN_004ff44c @55608 intersects with the end side and the start side (both must
      lie within the side, FUN_004fe214 slack 0.0004). It falls back to Q (within O) and P (within C), dropping
      points closer than 0.0001. Fewer than 2 points → rejected.
    - Same for the other band.
    - FUN_004feda8 @55336: interval intersection on the edge's dominant axis (empty → rejected). Then orient the
      edge so its sign along the dominant axis matches the current segment.
  - Accepted edges: FUN_004fbdec + FUN_004fc098, id stored, appended. More than 0x7ffc → **error 7**.
- **Finally the segment's own O record is appended** (@55901-55911). Its id bytes are never written.

**Tracing (00500cac):**
- N = C.ang + π/2 (sd = 0) or C.ang − π/2 (sd = 1), wrapped (dis 500ce4-500d80). N points away from the region.
- If L has fewer than 2 entries: samples O.start and O.end.
- **End edge**: FUN_00500a64 @56861.
  - If vertex j+1 has tag 0: on V = (C vertex → O vertex) (FUN_004fe17c), FUN_00500770 @56714 finds the L edges
    whose **end** lies on V within 0.00033. It keeps the nearest to C along V's dominant axis (tie list, eps
    0.00033, cap 299) and among those the largest |fc190(N, ang)|.
  - Otherwise: the L entry whose id is the next segment. Not found → index = |L| (stale read, error 0xd only if
    above).
  - E[0] = that edge's end point.
- **Start edge**: FUN_00500b7c @56911, symmetric. It uses the edge's **start** point, the smallest angle, and
  the previous segment for tag 1/2. S[0] = that edge's start point.
- **Trace step forward** (FUN_004ffc3c @56085, dis):
  - From point S[k−1] on edge cur, intersect cur with every other edge (excluding cur and the previous edge).
  - Accept X strictly ahead of the point and before cur's end + 0.00022, if |fc190(N, ang_s)| < |fc190(N, ang_cur)|
    and X is inside edge s (fe214).
  - Keep the nearest along cur's dominant axis (tie list: eps 0.00033; closer → prepend; else sorted insert from
    index 1).
  - If none: edges whose start is within 0.00033 of cur's end.
  - Choose the smallest |angle − N| (≤, scanning back to front).
- **Trace step backward** (FUN_005001d8 @56409): mirror image. X behind the point and beyond start − 0.00022,
  angle must increase, fallback on edges ending at cur's start, choose the largest (≥).
- **Combination** (@57033-57300):
  1. E step 1.
  2. If S0 == E0 (not own O), only S[0] and E[0].
  3. If the E trace is empty, run the S trace to the end.
  4. Otherwise S steps until S[k] ≈ E[1] (< 0.0001).
  5. Failing that, E steps until E[k] ≈ S[1] (< 0.0003), or a common point S[a] ≈ E[b] (< 0.0001), or append
     all E reversed.
  6. Limits: 1999 steps (**error 8**), merged length (**error 10**).
- **Samples (FUN_004ffb0c @56000):** S[0] (unless vertex j has tag 2), S[1..m], E[0] (unless vertex j+1 has
  tag 2). Here d = |P − F|/|n| on the dominant axis of n. n is the W normal of C[j] on side sd, and F is the foot
  of the normal through P on C[j] (FUN_004fbe84). d is f32. The contour's first sample is 0x0b, the others 0x0a.
- FUN_004ffa4c @55946 stores x, y raw and z = (double)d.

### 5. Port completeness (eg_pre.c)
- **Ported in full:** FUN_004fd510, 4fd3a4, 4fd1d0, 4fd07c, 4fcec0, 4fcda4, 4fcca0 (with its overflow retry and FUN_004fcb6c since
  2026-11-19),
  4fc2a4, 4fc6c0, 4fc890, 4fc7a0, 4fcb24, 4fde5c, 4fdb88/4fe534, 4fd920, 4fd974, 4fe3d8, 4fe33c, 5013cc,
  500cac, 4ff874, 4ff658, 4ff44c, 4feda8, 4feb6c, 4fe940, 4feaa0, 4ffc3c, 5001d8, 500770, 500a64, 500b7c,
  4ffb0c, 4ffa4c, and all geometric helpers.
- f32 rounding is applied where the original stores f32. x87 temporaries are modelled as long double.

### 6. Self-check (`./test_pre ../test/oracle.txt`, `./test_op2_3d`; ASan/UBSan clean)
Common parameters: abr = 20° (roll Sharp), A = 0.62831855, side = 1.

| case | result |
|---|---|
| (a) 76.2 mm CW square, inside, dist 0.127, W 0.13335 | rc 0. 1 loop, 5 records, corners (0.1282, 0.1272) … (76.0755, 76.0745). The distance to the input is 0.1245-0.1272; the deviation comes from the decoded vertex nudges of 0.0004-0.0013 |
| (a') same, outside (side 0) | 25 records, rounded corners of 5 segments (90°/20° → m = 5), distance 0.1257-0.1306 |
| (b) op4: 3 chains as given, ×25.4, dist 0.127, W 0.13335 | rc 0, 3 loops, 737 records. **Ours → stored finish passes (first 3 feed runs at z −0.135): max 0.0086 mm, mean 0.0032. Stored → ours: max 0.0020, mean 0.0006** |
| op2 2D: dist L = 1.045794, W = L + 0.01 = 1.05579 | rc 0, 6 loops. Stored z = −0.135 points of the first 6 groups → ours: max 0.0115, mean 0.0016 (2 of 182 above 0.01) |
| op2 3D: core mode 0 → eg_sharp_spiegel (ratio 1.1382, D 3.429) | 10532 records. Ours → stored 3D: max 0.052, mean 0.0042. Stored → ours: max 0.070, mean 0.0026 (5 of 227 above 0.01; these include the stored ramp/lead moves) |

Caveat: the oracle input is the chain points directly. The real VekAR1 comes from FUN_00504154/FUN_004fb97c
(contour preparation upstream, not this module).

### 7. NOT DECODED / deviations (rewritten 2026-11-19, branch engrave-core)
1. **The overflow retry is decoded and ported** (it was NOT DECODED here before):
   1. FUN_004fcca0 (dis 4fcca0-4fcd6e): `a = (double)abr` ([ebp-8], 4fccb8), `eps = 0.0005` (parent [-0x15c] := 0x3a03126f,
      4fccc2). Loop (4fcccc-4fcd1c): error := the error saved at FUN_004fd7dc entry (parent [-0x15e], 4fd803; 0 on this path),
      FUN_004fc2a4; on error 2: FUN_004fab08(&eps, 1.5) (x 3.333 below 0.0045, TBYTEs 0x4fab34 / 0x4fab40), `a += 0.0872665`
      while `a < 0.6981317` (TBYTEs 0x4fcd7c / 0x4fcd70), FUN_004fcb6c, again. `a` and `eps` are per contour.
   2. FUN_004fc2a4's overflow (4fc4d1-4fc4ed) is an **i16** add + `cmpw $0x7ffe; jl`: only a sum of 0x7ffe or 0x7fff is error 2
      (count -= m, the arcs already inserted earlier in the walk stay). A sum >= 0x8000 wraps negative and passes: the shift
      (System.Move 0x4030b0 returns at once for a negative count: `cmp $0x20,%ecx; ja; ... jle ret` at 0x403135) is skipped,
      the m+1 arc points overwrite the next vertices, and the record refresh indexes `k % count` with a negative count -
      writes in front of the work buffer. **Undefined (heap corruption); the port refuses** (EG_PRE_ERR_STALE).
   3. FUN_004fcb6c @53655: GetMem(0x9bfe6) x 2; record 0 = {0x0b, (f32) wk[0]}, records 1..n-1 = {0x0a, (f32) wk[i]}, z = 0
      (n capped at 0x7ffd); FUN_004fab4c(src, work, **n**, &cnt, 1.0, eps); wk[0..cnt-1].x / .y := the cnt records (as double),
      count := cnt - 1. FUN_004fab4c takes n as the index of the LAST point, so it reads record n, one past the polygon. 0x9bfe6 is
      above FastMM's medium-block limit (0x401818: `cmp $0xa2c` / 0x401a5c: `cmp $0x40a2c`, "FastMM Borland Edition" string), so
      the block comes from 0x401778 = VirtualAlloc(MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE): fresh, zero-filled. Record n is
      therefore {tag 0, (0, 0), 0} - **deterministic: the origin is the closing point** (it decides whether wk[n-1] survives pass 1
      and pass 2). Ported (fcb6c in engrave.c, through pip_reduce_contour).
   4. FUN_004fab4c itself (pipe sec. 7.3, 0x4fab4c-0x4faf37), the parts that were NOT DECODED in the pipe:
      - pass 1 runs only when `(int16)(n - 1) > 0` (0x4faba7: movzwl, dec, `test si,si; jle`): a contour with n >= 0x8001
        (32 770 points or more, up to the 0xbffc split limit) keeps p0 and p[n] only, comes out with 1 record and FUN_004fb624
        drops it (count > 3 required). Pass 2 loops while `(int16)(m - 2) >= 0` (0x4fad47). Ported (no more refusal above 32767).
      - the count > 0x7ffe retry (0x4faeec-0x4faf2b): tol *= 1.5 (count - 0x7ffe <= 0x1f4) or 2.0 via FUN_004fab08, n := count,
        and the whole function repeats from 0x4fab74 (eps [ebp-0x20] is not recomputed). That pass reads p[count]: pass 2 and the
        closing write only indices < count and count <= n + 1, so p[count] is the input record count, or (count = n + 1) the
        record after the contour in FUN_004fb97c's buffer - GetMem(0x9bfe6), zero-filled VirtualAlloc memory again, holding zero or
        a record of a longer earlier contour of the same call. Deterministic; ported (the port's buffer is calloc'ed and kept
        across contours like the original's).
2. **Stale values the original reads:**
   - the own-O record's id bytes (+2..+5): the append @55901-55911 copies the whole 0x2e-byte O record, and nothing writes the id of
     a work / C / O record (only the L append @55888 does). The O table is copied from the work buffer DAT_00535970, which is
     GetMem(0x16ffa6) per FUN_00501550 call (@57426, freed @57469) = fresh zero memory. **The id is 0** (it was -1 in the port);
     find_se can match it when it looks for segment 0. Ported.
   - L[|L|] when the neighbour edge is missing (500a64 / 500b7c: index = |L|, error 0xd only for an index above |L|, which cannot
     happen): L is the work buffer itself (PTR_DAT_0052f354 -> 0x535970, .data), so the record read is the last thing written to
     that slot - a record of an earlier segment's L, a work record of the contour preparation, or zero. **Deterministic; ported**:
     the port's L now lives in the zero-filled work buffer of the preparation (grow0 / L_slot in engrave.c).
   - S_ids/E_ids (5013cc locals, ushort) when FUN_00500770 finds nothing: they keep the previous segment's index - ported, and an
     index >= |L| reads that slot of the shared buffer as above. **Before the first segment they hold whatever earlier calls left
     in that stack slot** (the preparation's frames and the Windows calls of the progress display FUN_004bc3b0 / 4bc3c0 /
     4bc3d0): undefined; the port starts them at -1 and refuses (EG_PRE_ERR_STALE) only if the first segment of a precompute
     needs them.
   - the sentinel's x/y/z (FUN_004fcd88, uninitialised stack bytes): **never read** - FUN_00501ec4 runs FileSize-1 iterations
     (58816) and stops right after it has used the sentinel's tag; the port writes 0.
   - a 0x0a record before any 0x0b: unreachable (VekAR1 comes from FUN_004f194c / FUN_004f129c / FUN_00519380, which write every
     contour as 0x0b + 0x0a records); the port skips it.
3. **Grid capacity:** in-memory buckets 0x1f4000/(nx·ny·4), overflow file VekAR3.TMP, error 9. Not modelled.
   Cells outside 0..127 would write outside the original's table (heap corruption); the port returns `EG_PRE_ERR_STALE`.
4. Progress display (FUN_004bc3b0/3b8/3c0/3d0/3d8/3ec) and file block sizes are left out.
5. Transcendentals: the Delphi RTL is Cos = 0x403350 (`fldt; fcos`), Sin = 0x403360 (`fsin`), ArcTan = 0x403370 (`fld1;
   fpatan`), Sqrt = 0x403380 (`fsqrt`), argument and result extended. glibc 2.44's sinl / cosl on x86-64 are NOT fsin / fcos
   (measured: they differ in the last bit for ~12 % of f32 arguments); atanl is fpatan and sqrtl is correctly rounded like
   fsqrt. The port calls the x87 instructions (x87_sin / x87_cos in engrave.c, libm fallback off x86) - since 2026-11-19 in the
   corner arcs (0x4fc573 / 0x4fc597), FUN_004a1f04 and FUN_004a2054.

## 4. The 2D offset back end: FUN_0051001c / FUN_0050443c / FUN_005041dc / FUN_00501ec4 / FUN_00503ed4

Port: `viewer/engrave.c`, the section marked "port of eg_offset.c" (the decode was ported as that module; its file-scope helpers carry the prefix `off_` in engrave.c, the public `eg_*` functions keep their names). Self-check paths below refer to the decode work area, not the repo.


Dump = `<repo>/decomp/engrave/est/p_4b0000.c` (line numbers);
`dis` = annotated disassembly from `dis.py` (addresses). Port: `eg_offset.c/.h` (same directory).

### 0. Key finding: the core does not offset the input contour
`FUN_00501ec4` never reads its `param_1` file in plain-offset mode. It reads three temp files that the
**precomputation `FUN_00501550`** writes once per tool pass. `FUN_00528dac` calls it through
`FUN_005041a4("VekAR1.TMP", side)` at dump 87211, before the per-tool `FUN_0051001c` calls at 87237/87249:

| file | rec | contents | evidence |
|---|---|---|---|
| VekAR7.TMP | 25 B: tag u8 + 3 f64 | the contours (0x0b start, 0x0a line-to), implicitly closed, plus 1 trailing 0x0b sentinel | written by FUN_004fd510 (54042-54043), sentinel FUN_004fcd88 (53770); read 57583 via FUN_00501954 |
| VekAR6.TMP | 25 B | per segment a polyline of envelope samples (x, y, z=d). **d = distance of the sample from the segment line, measured along the offset normal, divided by W** | FUN_004ffb0c 56030-56058 (normal of length W from FUN_004fbfdc, foot point from FUN_004fbe84, d = \|Δ/normal\|) |
| VekAR5.TMP | i32 | cumulative sample index. Entry k = number of VekAR6 records before segment k, plus one final entry | FUN_005013cc 57357/57386 → FUN_004fe1e4 |

So the offset is computed in two stages:
1. A precomputed envelope for the maximum width W. This is a "Vorberechnung": per segment, the region where that
   segment is the nearest one, clipped at height W.
2. For each distance dist ≤ W, the core extracts the iso-level d = dist/W from that envelope. This is why the
   retry loop can change dist without recomputing anything, and why dist is capped at 0.999·W.

**Everything about corner geometry, self-intersection removal and vanishing comes from the envelope, i.e. from
FUN_00501550 (decoded and ported: sec. 3).** The core only intersects and trims straight offset lines.

`FUN_005041dc` (59379) only copies the names, sets a global short string `DAT_0052eb14 := "\nVekAR6.TMP"`
(59407-59412, no other reference in the dump) and calls FUN_00501ec4 with the same arguments.

### 1. Parameters of FUN_005041dc / FUN_00501ec4 (src, dst, p3..p9)
Stack offsets are confirmed in dis 501f12-501f80: p4 is at [ebp+8], p7 at +0x14, p9 at +0x1c.

| param | meaning | rule |
|---|---|---|
| p1 (src name) | used **only if p7 > 0**: rewritten (FUN_004bbd68) as the second output file | 58072-58076 |
| p2 (dst name) | main output, 13-byte LG1 records | 58069-58071 |
| p3 | side flag. Normal side = p3 xor 1 for FUN_004fbfdc. Side 0 gives (dy,−dx)·dist/len (right of travel); side 1 gives (−dy,dx)·dist/len | 58221; dis 4fc024-4fc050 |
| p4 | index into float table DAT_0052eafc (.data, never written): [1]=0.002 [2]=0.0025 [3]=0.001 [4]=0.0005 [5]=0.0002 ([0] is not a float). Used only as the p7>0 tolerance | 58031 |
| p5 | layer for progress display (FUN_004bc3d0) | 58032-58043 |
| p6 | mode. 1 = offset pieces only. 0 = also emits the envelope samples where the offset does not fit, with z = d/r (the 3D "Ausspitzen" input of FUN_00517ac8, which calls with mode 0, p7 = 0) | 58226, 58268, 58294, 58410 |
| p7 | > 0 turns on variable mode: second level r2 = p7/p9 (`local_6c8`). Otherwise r2 = 1.0 | 58045-58051, dis 501f56-501f8a |
| p8 | dist. Level r = p8/p9 (`local_6c4`, float32) | 58094 |
| p9 | W, the precompute width (FUN_00504268) | |

Brief correction: FUN_004bbcf4 sets FileMode and calls 403f70, which is **Reset**. FUN_004bbd68 → 403f8c is
**Rewrite** (8039-8091). FUN_00501550 rewrites VekAR6/5 with 4bbd68 (57447-57449).

### 2. Core algorithm (FUN_00501ec4, 57834-58903). Per segment, in VekAR7 order
Segment loop: FileSize(VekAR7)−1 iterations (58816). `start` = the current contour's first record. When the
record read is 0x0b, the segment processed is prev → old start (the closing segment), and the 0x0b becomes the
new start (58123-58149). `first_seg` (local_55a) = 1 for the first segment of each contour (58121).

1. S = line prev→cur, with dominant axis flag (|dx| ≤ |dy| → y) and slope (FUN_004fbdec 52900).
2. Samples: count = VekAR5[k+1] − VekAR5[k]. The samples E[i] = (x, y, d = (float)z) are read into a persistent
   buffer (58157-58178). n = count−1 intervals. If n = 0, a second sample is synthesized:
   E1 = E0 + (dx,dy)/f with f = (float)((flag ? dy : dx)·1000), and d1 = d0 (dis 50235f-5023bd).
3. O = S shifted by the normal of length dist on side p3^1 (58221-58223).
4. Start (58224-58261):
   - If d0 ≤ r: state *below*. Mode 0 writes 0x0b E0, z = d0/r.
   - Else: P = O ∩ line(S.start → E0) (FUN_004fbf40 52987; if the lines are parallel, P keeps its previous
     value). Write **0x0b P, z = 1.0**. State *above*.
5. Scan intervals i (58262-58408):
   - *below*: mode 0 writes each 0x0a E[i] (z = d_i/r). If d_i ≤ r < d_{i+1}, compute
     t = (float)((d_i−r)/(d_i−d_{i+1})) and P = E[i] + t·(E[i+1]−E[i]). Write P as **0x0b (mode 1) / 0x0a
     (mode 0)**, z = 1 → *above*.
   - *above*: if d_i > r ≥ d_{i+1}, P is computed the same way. Write 0x0a P, z = 1 → *below*.
6. End (58409-58438):
   - *below*: mode 0 writes 0x0a E[n].
   - *above*: E = S.end. If \|E[n] − E\| > 0.0008 in x or y (DAT_00503778, TBYTE), write
     **0x0a (O ∩ line(E → E[n])), z = 1**. Otherwise nothing is written.
7. Writer FUN_00501a54 (57622) stores float32 x, y, z. It **drops a point within 0.001 (DAT_00501bd4) in both x
   and y of the last written point**, unless first_seg is set or the file is empty. In variable mode it writes
   nothing. The dedupe is what merges the end of segment k with the 0x0b start of segment k+1, so pieces only
   break where the envelope dips below r or where corner points differ by more than 0.001.

**Corners.** A corner point is always a line-line intersection: the offset line with the ray from the segment's
vertex through its first or last envelope sample. There are no arcs and no segment counts here; the
Abrundungswinkel (block 7) is not read anywhere in 51001c, 50443c, 5041dc, 501ec4 or 503ed4 (callee trees contain
no FUN_0049acb8). The precompute (sec. 3.2 step 6) rounds convex corners whose turn is at least the Abrundungswinkel with a
0.001-radius polygonal arc (so the offset gets a polygonal arc of radius r + 0.001) and miters smaller ones; the envelope's
samples then lie on the line-line bisectors (sec. 3.4).

**Inside or outside.** p3 = 1 → offset to the right of travel → the inside of a clockwise contour. The precompute
uses the same flag (DAT_005359c0 = p2^1, 57430, used with FUN_004fbfdc at 56030). The contour direction is set
upstream by FUN_004f129c.

**Vanishing.** Where the envelope stays at d ≤ r, mode 1 writes nothing for that segment. If fewer than 4
records reach the join, the result is error 100.

#### Variable mode (p7 > 0; used by FUN_005043c4 "VekAR1.Eck" and others, not by the plain 2D offset)
- Every level-r crossing also records two points (58311-58341, 58374-58404): the crossing point (arrays
  Ae0 up / Ad8 down) and that point minus the normal offset, clamped to S along its dominant axis
  (FUN_00501d80 57754; arrays Ae4 / Adc). local_724 = the type of the first crossing (1 down, 2 up).
- The same scan is run for level r2. It builds a 25-byte list B (0x0b at up-crossings, 0x0a at down-crossings,
  plus start/end intersections) (58440-58594).
- If the segment had no level-r "above" event (local_721 = 0), all of B is written (FUN_00501c18 57697). The
  dedupe there uses tolerance DAT_0052eafc[p4] in float.
- Otherwise B is walked as a polyline. At each level-r crossing, B is cut by the perpendicular line
  (crossing → projected point), with an extent check (FUN_005017c0 57481, slack 1e-6). Up-crossings write the B
  vertices before the cut and then the cut point (0x0a). Down-crossings write the cut point as 0x0b and skip the
  B vertices. The projected point is replaced by the cut point. After the last crossing, if the state is
  "outside", the rest of B is written (58610-58812). Records written here carry the stale z (local_933).
- After all segments, down-crossings are paired with up-crossings: the first j with \|Ad8[k]−Ae0[j]\| ≤ tol.
  For each pair it writes 0x0b Ae4[j], 0x0a Adc[k], then swap-removes j (58827-58888).
- The main output stays empty in variable mode.

### 3. Join (FUN_00503ed4 59202, called from FUN_0050443c with tol = **dist**; dis 504488 pushes [ebp+0x10])
Piece scan, FUN_00503784 (58906):
- If the file has fewer than 4 records → **100**.
- A piece is the run from one 0x0b to the record before the next 0x0b. It is kept only if it has ≥ 2 records.
  The last piece is always kept.
- start pt = the 0x0b's x, y. end pt = the last non-0x0b x, y (float).
- More than 65535 pieces → **0x65**.

Passes (dis 503fd7-5040be):

| pass | rule |
|---|---|
| 0 | each piece with \|start−end\| < 0.001 in x and y (DAT_00504148) is copied whole, with its 0x0b (FUN_00503968) |
| 1 (FUN_00503e5c mode 0) | for each unused piece i in index order, a **trial** walk (FUN_00503a48 59040, see below) runs with acceptance best < tol. If it closes, it is replayed with mode 1, which writes it |
| 2 (mode 1) | every remaining unused piece i is written, and nearest pieces are appended **without a distance limit** |
| end | output < 4 records → **100**. Any piece not consumed (state ≠ 0xff) → **0x66** |

Walk (FUN_00503a48), with C = current end and S = start of piece i:
- best = \|C−S\|, computed as float sqrt. For j > i still unused (state 0), the nearest start wins with strict <,
  and the search stops early at 0.
- If the result is S itself: if best > 0, append the closing record 0x0a S. The loop is closed.
- If it is another piece: append that piece **without its 0x0b record**, and set C to its end.
- In trial mode, pieces are only marked (state 1), and the marks of j > i are cleared at the end.

All records written by the join get z = 0 (59027, closing record 59105). The output start point is the first
record of the lowest-index piece of each loop. Loops that are already closed come first, in file order, then
joined loops in the order of their first piece.

**Error 0x66**: by this decode, pass 2 consumes every piece with state 0. The trial marks are reset, and a
closed trial is always replayed. So the 0x66 check at 59300-59311 cannot fire from this path. The code at
50410a (`test esi,esi`, where esi = 0 from 503f1c) is dead.

### 4. Retry (FUN_0051001c 68719; dis 510065-5100f9)
- step = (W − dist)/2 in float, capped at 0.01 (if step > 0.01 then step = 0x3c23d70a).
- Loop: call FUN_0050443c(src, dst, p3, layer, 0, dist, W). If the error is 0x66:
  - dist += step.
  - If dist > 0.999·W (DAT_0051011c), set dist = 0.999·W and raise the try counter to at least 4.
  - Clear the error and increment the counter. If the counter > 5 → **0x67**, stop.
- At most 6 calls. Any other error code ends the loop. The precompute is not redone.

### 5. FUN_004a1f04 (p_460000.c 51307; dis 4a1f04-4a1f54)
**Tan**: returns (float)(sin x / cos x), and returns 0 when x == 0.0 (0.0f @0x4a1f58). Sin (0x403360) is kept as a TBYTE
(fstpt -0x10), Cos (0x403350), the division is extended (0x4a1f3e `de f1` = st0 / st1 = sin / cos), then fstps. It is not used
by this module (FUN_004a2054 uses it for the tool radius, sec. 7.2).

### 6. Port status (eg_offset.c)
- FUN_00501ec4: ported fully, in all three modes (0, 1, p7 > 0). This includes the stale-value behaviour: P after a
  parallel intersection, z = local_933, B[n704] reads past the end (deterministic when an earlier segment of the call wrote
  that record, else undefined and refused, sec. 4.9), and E[0] when a segment has 0 samples.
- Ported in full: FUN_00503ed4, 00503784, 00503968, 00503e5c, 00503a48, 0050443c, 0051001c, and the helpers
  4fbdec, 4fbe68, 4fbf40, 4fbfdc, 5017c0, 501d80, 501a54, 501c18.
- Left out: the progress display (FUN_004bc3d0/3d8/3ec), file I/O block sizes, and the global name
  DAT_0052eb14.
- Precision: float32 where the original stores float (r, r2, d, t, the factor f, output x, y, z, and all join
  geometry), doubles where it stores doubles, long double for the values it keeps on the x87 stack; TBYTE constants as their
  long double. The variable-mode paths were re-read from the disassembly with the corner tool (sec. 4.9).

### 7. Self-check (test_offset.c, `./test_offset`)
The envelope is a **hypothesis fixture**, because the precompute is not decoded: two samples per edge on the
90° corner bisectors at height W, d = 1. dist = 0.127 mm (0.005 in tip radius), W = 1.05·0.127 = 0.13335,
p3 = 1.

| test | result |
|---|---|
| T1: 76.2 mm square, CW | core: 5 records (0x0b + 4 × 0x0a, closed), corners (0.127, 0.127) … (76.073, 76.073). Join: copied in pass 0, z = 0. The distance of every output point to the input contour is 0.127000-0.127002 (float32). retry rc = 0 |
| T2: 76.2 × 0.2 rectangle (envelope height 0.1 < 0.127) | nothing extracted → rc = **100** |
| T3: dip on one edge (d 1 → 0.5 → 1) + a second contour | the edge splits at y = 30.476 / 44.524 (interpolated). The trial fails (gap 14.05 > tol). Pass 2 joins without a limit and drops the 0x0b point at 44.524. The closed second contour is output first |
| T3, mode 0 | the dip samples come out with z = 0.525 = 0.5/r |
| T3, variable mode (`test_var.c`, p7 = 0.10) | the r2 path is written only inside the dip. ASan/UBSan clean |

(2026-11-19) The coaster oracle now runs through the real precompute (sec. 3, sec. 9).

### 8. NOT DECODED (rewritten 2026-11-19)
1. FUN_00501550 precompute: decoded and ported (sec. 3), including its overflow retry (sec. 3.7.1).
2. The Abrundungswinkel and the 0.62831855 constant are both used by the precompute (sec. 3.2 step 6, read from the call at
   528e2c-528e5b): KAbrundungswinkel (block 7 +4, x deg) is the corner-arc step and threshold for convex corners; 0.62831855
   (36 deg) is the tag-1 / tag-2 boundary for concave corners (the envelope keeps the start / end sample of a tag-1 vertex, drops
   it at tag 2, sec. 3.4 "Samples"). Grund sharpening (sec. 5.2) uses its own hard-coded 36 deg (TBYTE@518b30).
3. Stale values: E[0] of a segment with 0 samples is **deterministic**: the sample buffer is local_6e8 = GetMem(0x16ffa6) per
   FUN_00501ec4 call (58060, freed 58822) - a FastMM large block, fresh zero memory - so the first such segment reads zeros and
   later ones the previous segment's E[0] (the port's zero-filled persistent buffer does exactly this). B beyond n704 (variable
   mode only) is local_6d4 = GetMem(0x1a000) (58053): a FastMM **medium** block (<= 0x40a2c), i.e. heap memory reused from earlier
   frees - its content depends on the whole allocation history of the process: **undefined**. Variable mode is only called by the
   corner tool (FUN_005043c4 "VekAR1.Eck", KorrekturVerf 'E', sec. 4.9). The read is reachable (the B walk runs one past its end
   when a crossing's cut is not found); a record an earlier segment of the same call wrote is deterministic and ported, a record
   never written in the call is refused (EG_OFF_ERR_UNDEF). None of the Wine cases of sec. 4.9 reached the refusal.
4. The corner tool FUN_00528a1c / FUN_00528b10 (KorrekturVerf 'E'): **decoded and ported, sec. 4.9**. Still NOT DECODED / refused
   there: the 45000-contour branch of FUN_004f5048 (short error 0x65) and a hatch output ending in 0x0b (FUN_004f4b54 loops
   forever), both unreached.

### 9. The corner tool ("Ecke", KorrekturVerf 'E') - decoded 2026-11-19
Port: `viewer/engrave.c`, section "the corner tool" (`ck_*`, driver `eng_corner`, called from `eng_block`). Citations: `l.N` =
`p_4b0000.c`, addresses = the patched exe's disassembly (every stack argument mapped from the pushes; Ghidra's `local_N` is
`[ebp-(N-4)]`). The corner tool cleans the corners the next (bigger) tool cannot reach: the region between the next tool's offset
and its own, hatched and outlined. FUN_00529b50 (sec. 7.2, check -4) guarantees a next row with rt >= 2.5 rt('E').

**Slot loop** (FUN_00528dac l.87218-87229): 'E' -> FUN_00528a1c, the engine error it leaves is kept only for a message
(local_8, 0x274 / 0x275 at the end of FUN_00528dac), FUN_00528b10, then the error := 0. A corner tool never fails the run.

**FUN_00528a1c** (l.87010-87044, dis 528a1c-528b02). p7 = R[s] + *extra, dist = R[s+1] + *extra (row +0x18 / +0x35, flds; fadds;
fstps). tries := 1; loop: FUN_005043c4(EAX side = Gegenlauf, EDX colour, ECX tries; pushes W, dist, p7 -> [ebp+8] = p7,
[ebp+0xc] = dist, [ebp+0x10] = W); on error 0x66: tries++, tries > 5 -> error := 0, dist := (f32)(0.97 dist) (TBYTE@528b04 =
0.97L); repeat while 0x66. Then error 100 -> 0, short error 0x44f -> 0. (0x66 cannot come from the join, sec. 4.3: the loop runs
once.)

**FUN_005043c4** (dis 5043c4-504407): FUN_005041dc(src "VekAR1.Eck", dst "VekAR2.TMP", side, p4 = tries, colour, p6 = 1,
p7, p8 = dist, p9 = W) -> the core in **variable mode** (sec. 4.2: p7 > 0; level r = dist / W = the next tool, r2 = p7 / W = the
corner tool; tolerance DAT_0052eafc[tries] = 0.002 for the first call). Error := 0, then FUN_00503ed4("VekAR1.ECK" ->
"VekAR12.TMP", tol = p7) (the join of sec. 4.3; its third argument CL = 1 is not read). The join rewrites VekAR12 after its piece
scan in every case (l.59268-59271), so a failed join leaves it empty, never stale.

**FUN_00528b10** (l.87050-87100, dis 528b10-528c68): s = (2 - KAbraeumueberlapp / 50) rt[s] (block 7 +8 [-0x64], 50.0f @528c6c,
2.0f @528c70, x87, fstps); error 0x66 -> 0; if FUN_00498edc(VekAR12.TMP) (records) > 3: FUN_00499380(VekAR12.TMP ->
VekH2R0.H2R, CL = 1 = rewrite): type 1 (.TMP, 13-byte) -> type 3 (.H2R, 9-byte tag, x, y; FUN_00498900: LG1 1, H2R 3, TMP 1)
dropping tags 0, 1, 5..7, 9, 0x96..0x9c (0x9d becomes a 0x80 record: refused, never in VekAR12); the parameter block
{+0 / +4 / +8 / +0xc box of FUN_0052919c's [ebp+0xc] (replaced by the file box, flag = 1), +0x10 s, +0x14 s / 4 (4.0f @528c8c),
+0x18 angle 0, +0x1c flag 1, +0x1d link 1, +0x1f one-way 0; +0x1e not set and not read by the hatch}; FUN_004f5048(VekH2R0 ->
VekH2R1, colour, &err (= [ebp+8]), 3 (= [ebp+0xc]), &block (= [ebp+0x10])); err != 0 -> engine error err + 0x14 (cleared by the
slot loop) and nothing appended; else FUN_00499380(VekH2R1 -> file(slot), 0): appended with z = 0. VekH2R0 / VekH2R1 deleted.

**FUN_004f5048** (l.46583-46676, dis 4f5048-4f5316), mode = 3:
1. short error := 0, *err := 0; contour table F[-0xc] = GetMem(0x62e11) (9-byte {start, end, used}), boxes F[-4] / F[-8] =
   GetMem(0x57e48) (min / max per contour) - large blocks, zero memory; box F[-0x24] := +-100000 (FUN_004c4434).
2. FUN_004f442c (dis 4f442c-4f467a): < 4 records -> short error 0x3f3 (nothing else happens). Else per record: tags 0x0a / 0x0c
   widen the box and the contour's box (FUN_004f43e4 starts it at (10000, 10000) / (-9999, -9999)); a 0x0b after the first closes
   the contour (end = i - 1) and, if end > start, opens the next (count > 45000 -> short error 0x65); start := i. The last contour
   ends at the last record.
3. The output is rewritten empty (FUN_004c4720). Short error 0x65: the whole file goes to FUN_004f39e0 at once (with mode bit 1
   a copy appended after) - **refused** (not reachable with fewer than 45001 corner contours, not measured).
4. Short error 0: cells. cw = (xmax - xmin) / 20, ch = (ymax - ymin) / 20 (20.0f @4f531c, x87, fstps), each at least 2.0
   (@4f5320). cy from ymax down (cy -= ch, f32) while !(cy < ymin), cx from xmin right (cx += cw) while !(cx > xmax), both loops
   also end on *err != 0. Per cell FUN_004f4d38 (dis 4f4d38-4f5020): x1 = cx + cw, y0 = cy - ch (f32); repeat while a contour was
   found and *err == 0:
   - the unused contour with max.y >= y0 and min.x <= x1 nearest to the cell corner: d = (f32)sqrt((min.x - cx)^2 +
     (max.y - cy)^2) (extended, 0x403380), strict <, start 10000.0;
   - FUN_004f467c: its records appended to VekH2R2 (rewritten at the group's first contour), marked used; then every unused
     contour whose box lies inside its box (>= / <=) and whose first point is inside it (FUN_004f47a4: parity of the crossings
     with t in (0, 1) and x < P.x; on an exact y hit P.y += 0.0002 (TBYTE@4f49e8 = 0.0002L) and recount - the original compares
     the previous record's **x** with P.y there (4f48a7), ported as is);
   - with mode bit 1: group < 30000 records -> FUN_004f49f4 (reads the group into F[-0x178]; mode bit 2: the hatch angle (+0x18)
     := pi/2 (0x3fc90fdb) when the box of records 1..n-1 is higher than wide, else 0); else a copy VekH2R4;
   - FUN_004f39e0(VekH2R2 -> VekH2R3, colour, err, block) = the hatch of sec. 6.3 (flag 1: box := the group's file box, rows at
     spacing s inset by s / 4, linked); *err == 0 -> VekH2R3 appended to the output (FUN_004c3cf0 CL = 1);
   - with mode bit 1: group < 30000 -> FUN_004f4b54: the group's records appended to the output, the first one re-tagged 0x0a when
     *err == 0, link (+0x1d) is on and the straight line from the last VekH2R3 point (its last record, which must be 0x0a / 0x0c:
     4f4bcc-4f4bfe re-reads the same record forever otherwise - **refused**) to the group's first point crosses none of the group's
     segments (FUN_004f426c, dis 4f426c-4f43d8: records 2..n-2 against their predecessor from record 1 on, 0x0b restarts, other
     tags skipped; det and both parameters f32, strictly inside (0, 1)), else 0x0b; empty VekH2R3 -> 0x0b. Group >= 30000: VekH2R4
     appended unchanged (and the angle stays the previous group's).
5. VekH2R2 / VekH2R3 deleted.

**Variable mode, completed decode** (FUN_00501ec4, used only here). Re-read from the disassembly; corrections ported:
- the writer FUN_00501c18 (dis 501c18-501d7d) compares |last record (f32) - new value (double)| in extended with the f32 tolerance
  (the port rounded the new value to f32 first); the first record (buffer count 0) is always written;
- the crossing points minus the offset (5026d5-5026f4), the r2 offset line (502b22-502b41) and the r2 end line (502e7c-502ed1) are
  extended sums / differences stored as doubles; the pairing distance (503538-50359f) is an extended Sqrt, the search goes on while
  d > tol;
- FUN_005017c0 (extent, dis 5017c0-5018d7, e = TBYTE@5018dc = 1e-6L): d < 0: e + o > p and (o + d) - e < p; d >= 0: o - e < p and
  (o + d) + e > p (the port had <= at the far end and summed e + o + d); FUN_00501d80 (clamp) compares p with the extended o + d and
  stores (double)(x0 + dx); FUN_004fbe68 stores (double)(x - x0) of the extended difference and FUN_004fbdec the slope from the
  extended quotient (shared with the plain offset; the Wine classes below are unchanged by it).
- Buffers (dis 501f9a-501ff4): B = [ebp-0x6d0] = GetMem(0x1a000) (25-byte records, 4259 of them) and the four crossing arrays
  GetMem(0x20000) (8192 points each) are FastMM **medium** blocks. B is read one past its last record when the walk runs off its end
  (sec. 4.2: i9 = n704); what is there was written by an earlier segment of the same call (deterministic) or not at all
  (**undefined**, reused heap): the port tracks the highest record written and refuses a read beyond it (EG_OFF_ERR_UNDEF), and
  refuses more than 4259 B records / 8192 crossings (the original overruns its blocks). No Wine case below reached either.

**Oracle (Wine, EG-2).** Cases: tools/tests/engrave_wine_cases.py (write_estdat takes a tool list, P['tools']); the tool sets of
engrave.dll's remachining list (engrave.md 3.4 on the engrave-dll branch): [T, T-Ecke, BIGTOOL] and [T-Ecke, BIGTOOL] with T = V 15 deg tip r 0.1, BIGTOOL tip
r 0.3 / 0.5 (2.5 x 0.1f = 0.25000000372 > 0.25 fails check -4: FEHLER 906), flat tools, a 0.03 tip V corner tool under a 0.6 tip
tool; shapes e1 (star, notch, sliver triangle) and text in four fonts (DejaVu Sans / Serif Bold, Liberation Sans / Mono Bold);
Spiegel / Grund, KAbrundungswinkel 20 / 150, Bahnoptimierung on / off, Gegenlauf, KBlockweise 1 / 2 / 3 / 4, GravurTyp erhaben,
roughing keine / Z / Y / C / D, overlap 0 / 20 / 50.

| class (geometry x 9: e1, 4 fonts x 'Rg&8@' / 'ENGRAVE') | identical |
|---|---|
| 3 tools [T, T-Ecke, BIG] Spiegel, keine / Z / C / Y (no Bahnopt.) / KAbrundungswinkel 150 / Gegenlauf / KBlockweise 1 | 9/9 each (63) |
| 3 tools Grund (T and BIG 2D), keine / Z | 9/9 each (18) |
| 2 tools [T-Ecke, BIG] keine / D / Z without Bahnoptimierung | 9/9 each (27) |
| flat tools (tip r 0.25 / 0.8): 3 tools keine; 2 tools Z, KAbrundungswinkel 150 | 9/9 each (18) |
| **set 1** | **126/126** |
| class (geometry x 5: e1, 4 fonts x 'MWK4%') | identical |
| [V 20 deg tip r 0.03 Ecke, V tip r 0.6]: keine; 150 + Gegenlauf; Z overlap 50; C erhaben; KBlockweise 2; KBlockweise 4 + KStartpunkt 3 | 5/5 each (30) |
| [flat tip r 0.1 Ecke, V 30 deg tip r 0.4 2D]: keine; Grund Z without Bahnoptimierung | 5/5 each (10) |
| 3 tools [2D 0.03, Ecke 0.03, 0.6]: keine; D overlap 0 | 5/5 each (10) |
| **set 2** | **50/50** |

All 176 with FEHLER 0 on both sides. Paths reached (counted with a temporary instrumented build): 941 corner slots with output and
27 where the join ended with error 100 (no corner left), 5412 hatch groups (angle 0: 3598, pi/2: 1814), contours inside a group's
first contour: 115 (29 rejected by the parity test), outline link 0x0a: 4202, 0x0b after a crossing: 675, 0x0b with an empty hatch:
535; never: a group >= 30000 records, the 0x65 branch, a B read beyond its records. The existing classes stay identical
(cases_nobo 13/13, cases non-C/D 44/46 as before, C/D pool 302/310 after the error-100 fix of sec. 7.4, before 293) and the coaster
oracle (engrave_test.sh) is unchanged.

## 5. The V-tool 3D corner sharpening ("Ausspitzen"), KorrekturVerf 3 / A

Port: `viewer/engrave.c`, the section marked "port of eg_sharp.c" (the decode was ported as that module; its file-scope helpers carry the prefix `shp_` in engrave.c, the public `eg_*` functions keep their names). Self-check paths below refer to the decode work area, not the repo.


Dump lines = `decomp/engrave/est/p_4b0000.c` unless marked p_460000.c. Disassembly addresses from `dis.py`
(all float formulas were read from the disassembly). Port: `eg_sharp.c` / `eg_sharp.h` (same directory).

### 0. Argument order (stack args, checked in the caller disassembly)
Stack args are pushed in source order, so the first stack arg is at the highest address. E.g. 517ac8
pushes ratio, then row+4, then row+2 (517b46-517b5a), so inside FUN_00517594 [ebp+0x10] = ratio,
[ebp+0xc] = D (row+4) and [ebp+8] = row+2 (used only for progress, FUN_00468270(0xd, .)). Ghidra's
`FUN_00517594(.., param_4=row+2, param_5=D, param_6=ratio)` is consistent with this.

### 1. Spiegel (KonturenAuf 'S'): FUN_00528c9c -> FUN_00517ac8 -> FUN_005041dc + FUN_00517594

#### 1.1 Driver: FUN_00528c9c (dump 87105-87141) and FUN_00517ac8 (dump 74632-74668)
- Output file: the per-tool file FUN_00499f80(slot), *except* when slot >= the number of finishing tools
  and AbraeumVerf is 'Z' or 'Y' (dump 87116-87119). In that case it writes to a temp file
  (DAT_00528da0, deleted first by FUN_00498b84) and appends that file (FUN_00499380) to the file of
  slot `byte[frame-0x6d]` (dump 87129-87138).
- `L = row+0x18 + offset` (517af4-517afd; offset = **(float**)(frame+0x1c), the allowance).
- `FUN_005041dc("" , "VekAR2.TMP", flag, 1, -1 (0xffff), mode 0, 0.0, L, W)` (517b13-517b2d). The
  source name is the empty string at 0x517b8c. 5041dc puts "\nVekAR6.TMP" in DAT_0052eb14 and calls
  FUN_00501ec4 (dump 59406-59413). This is the 2D offset engine (decoded by another module).
- `ratio = L / (L - (row+0xc + offset))` (517b32-517b49; row+0xc = tip radius rt).
- `FUN_00517594("VekAR2.TMP", outfile, only3d = (row+0x1c == 'A'), ratio, D = row+4, row+2)`.

#### 1.2 Input contract (from the offset engine, mode char param_6 == 0)
- VekAR2 records are written by FUN_00501a54 (dump 57657-57661) as tag, x, y, z (f32, from doubles).
- In mode 0 the engine writes **z = (vertex value at +0x2c) / local_6c4**, with local_6c4 = param_8/param_9
  = L/W (dump 58094). See dump 58232 (0x0b), 58274 and 58416 (0x0a). Points where that value crosses
  L/W are written with **z = 1.0** (dump 58304, 58368). The +0x2c value is a distance in units of W,
  so **z = d/L, the normalised distance from the contour (1 = full offset, 0 = on the contour)**. How the
  engine produces the corner/ridge points and their d values belongs to the offset module (NOT DECODED here).
- Measured confirmation: for the 68 op2 NCI points with -0.135 < z <= 0 (3D moves), the distance d to
  the nearest op2 chain segment satisfies `d = rt + (L-rt)*(-z_nci)/D` with median residual -0.0001 in
  and 71% within 0.0003 in (max 0.0015, NURBS-sampled corners). Script: `check_dist.py`.

#### 1.3 Core FUN_00517594 (dump 74402-74629; disasm 517594-517aa4)
I/O: VekAR2 is opened for reading (FUN_0046b438(.,0xd,0)); the output file is opened with mode 2 and
positioned at its end (FUN_0046b438(.,0xd,2) + FUN_00403f98(f, FileSize)), so the output is
**appended** (dump 74483-74488). Reading is done in blocks of 5000 records (FUN_0046a680), and the
state carries across blocks.

State: prev record (ptag=0x0b, px=py=pz=0), "need move-to" nm=1, last written point wx=wy=0 (517640-51767f).
For each record (tag, x, y, z):
1. **Duplicate skip**: if |px-x| <= 0.002 and |py-y| <= 0.002 and tag == ptag, the record is ignored
   completely (517726-517761, TBYTE@517aa8 = 0.002).
2. **Depth map**: `z' = (z - 1)*ratio + 1`, stored as f32 (517767-517779). With z = d/L and
   ratio = L/(L-rt') (rt' = rt+offset) this is **z' = (d - rt')/(L - rt')**, i.e. the fraction of the
   depth at which a V tool whose tip has radius rt' touches both walls at distance d. The output
   depth is `z'*D` (+0.0) (5179fd-517a03), so **z_out = D*(d - rt')/(L - rt')**; the tool reaches z = 0
   where d = rt'.
3. If z' <= 0 and tag == 0x0a (51777c-517793): the segment goes above the surface.
   If pz > 0: (if nm: put(0x0b, prev, 0); nm=0; put(0x0c, prev, pz*D)); t = pz/(pz-z');
   w = prev + t*(cur-prev) (f32); put(0x0c, w, 0) (517799-517865). **The point where z' = 0 is emitted at
   z = 0 and the part above it is dropped.**
4. Else if tag == 0x0a (z' > 0) (517870-517a28):
   - if nm and pz > 0: put(0x0b, prev, 0); nm=0; put(0x0c, prev, pz*D); w = prev.
   - if pz < 0 (coming back from above the surface): t = pz/(pz-z'); prev := prev + t*(cur-prev) (f32);
     if |prev-w| > 0.002 in x or y: put(0x0b, prev, 0); nm=0; put(0x0c, prev, 0). Tag := 0x0c.
   - else tag := 0x0a if z' > 0.99 and pz > 0.99 (TBYTE@517abc) else 0x0c.
   - put(tag, cur, z'*D); w = cur.
5. Else (any other tag, e.g. 0x0b): nm = 1 (517a2a).
6. prev := (tag, x, y, z') (517a31-517a4f).
At the end the 1000-record write buffer is flushed (FUN_005170b8; error 0x32 on a short write).

Results:
- **Each loop starts with a move-to at z = 0 followed by a plunge** (0x0b at (x,y,0), then 0x0c at depth).
  If the loop's first record is a contour vertex (z' < 0), the first output is the z = 0 crossing point on the
  bisector, and the path then **descends diagonally** to the offset corner. This is the op2 NCI pattern
  "loop starts at a sharpened corner at z=0, descending". Which point a loop starts at
  (KStartpunkt) is decided upstream, not here.
- A corner whose ridge reaches the contour (records C(z=1) -> V(z=0) -> C(z=1)) produces
  `C (D) -> P (0) -> C (D)`, with P = C + (V-C)*(1 - rt'/L): **straight up the bisector to z = 0 and back
  down to the same corner**. The return crossing equals w, so no extra move-to is written (step 4).
  A ridge point with d > rt' (two bisectors meet first) is passed through with z = D*(d-rt')/(L-rt')
  (partial height, e.g. op2 (-0.435993,-0.081086,-0.050452)).
- Full-depth segments (both ends z' > 0.99) keep tag 0x0a. All 3D moves are 0x0c. Move-tos are 0x0b with z=0.
- **This function has no angle threshold.** Which corners get sharpened depends only on the d values that
  the offset engine emits. KAbrundungswinkel is not read here.

#### 1.4 put = FUN_00517204 (dump 74304-74399; disasm 517204-517577): the 'A' filter
Let Dm = D - 0.02 (TBYTE@51757c). l = the last record given to put (initially zero), and pending is the
move-to flag (initially 0).
- Not only3d: write the record unchanged.
- only3d, tag 0x0b: pending = 1.
- only3d, l.z >= z (rising or level): z >= Dm -> pending=1; else if l.z <= Dm -> (pending? write 0x0b(l,0)) write 0x0c(cur);
  else (crossing Dm upward) t=(Dm-l.z)/(z-l.z), i = l + t*(cur-l): write 0x0b(i,0), 0x0c(i,Dm), 0x0c(cur).
- only3d, l.z < z (descending): l.z >= Dm -> pending=1; else if z <= Dm -> (pending? 0x0b(l,0)) 0x0c(cur);
  else if |l-cur| > 0.002 (x or y, TBYTE@517588): (pending? 0x0b(l,0)) 0x0c(l + t*(cur-l), Dm).
- l := cur.
**'A' = only the parts of the path shallower than D-0.02 mm (the corner moves). Every kept run starts with
a move-to at z = 0, and all kept moves are 0x0c.**

#### 1.5 write = FUN_0051710c (dump 74257-74301; disasm 51710c-5171f3)
A 0x0b is dropped when the previously written record has a nonzero tag and lies within 0.009 of it in both x
and y (TBYTE@5171f8). Otherwise the record is appended (buffer of 1000, flushed by FUN_005170b8).
FUN_0046e1d4 and FUN_0046e05c (p_460000.c dump 10924, 10844) only draw on screen.

### 2. Grund (KonturenAuf 'G'): FUN_0051001c + FUN_00518bd4 (called at dump 87249-87256)
FUN_0051001c("VekAR1.TMP" -> "VekAR8.TMP", flag, 1, row+2, offset = row+0x18 (= rt for 'G') + allowance, W)
is the plain 2D offset. Then FUN_00518bd4("VekAR8.TMP", perToolFile, flag, row).

#### 2.1 FUN_00518bd4 (dump 75543-75764; disasm 518bd4-518f5b)
- Opens the output with FUN_00498274(name, 1, &h, 2) and moves to its end (FUN_004985c0), i.e. **append**.
  It reads VekAR8 in blocks of 5000.
- **Error 54 (0x36)** if D (row+4) < 0.01, angle (row+0x10) < 1.0 deg or row+0x18 < 0.01 (518c56-518cac).
- ang = 0.017453292519943295 * row+0x10 (f32), the half angle in rad (518cb1).
- It keeps a 3-point window of 0x1f-byte slots S0, S1, S2 at [F-0x68], [F-0x49], [F-0x2a] (fields: +1 steep,
  +2 tag, +3 x, +7 y, +0xb z, +0xf dx, +0x13 dy, +0x17 slope, +0x1b direction angle of the segment to the
  next point), plus P at [F-0x90] = the 2nd point of the current loop. count is the byte at [F-0x71].
- 0x0b (518d72-518dc4): close(); write the record **as is** (its own z); its z := D; S0 := it; count = 1.
- 0x0a / 0x0c (518dc9-518ee2): 0x0a gets z := D (**0x0c keeps its z**); if count == 1, P := rec.
  If count <= 2: S[count] := rec; seg(S[count-1]); count++. Otherwise shift S0:=S1, S1:=S2, S2:=rec; seg(S1).
  If count > 2: corner().
- Other tags (518ee5-518f02): close(); count = 0; write the record unchanged.
- At EOF: close(); flush (FUN_00518610).
- **Points are written only by corner(), which writes the middle point S1.** As a result, a loop of only 2
  points (0x0b + one 0x0a) loses its 2nd point. This is reproduced as-is.

#### 2.2 Helpers
- seg = FUN_00518738 + FUN_005186b8 (dump 75314, 75287): dx, dy = next - this (f32). If |dx| <= |dy|:
  steep = 1, slope = dx/dy (0 if dy = 0). Else steep = 0, slope = dy/dx (0 if dx = 0).
- angle = FUN_00518754 (dump 75326; 518754-518815): steep: dy >= 0 -> pi/2 - atan(slope), else 3pi/2 - atan.
  Flat: dx < 0 -> pi + atan, else dy < 0 -> 2pi + atan, else atan. This is the direction angle in [0, 2pi).
- turn = FUN_0051884c (disasm 51884c-5188a3; Ghidra shows an empty body because of the ST0 return):
  d = a(S1->S2) - a(S0->S1), wrapped by -/+2pi when |d| > pi.
- close = FUN_00518b48 (dump 75494): if count > 2 and P.tag != 0: S0:=S1, S1:=S2, S2:=P; seg(S1); corner().
  **This handles the corner at the loop's start/end point** (the last point = the first point for closed loops).
- corner = FUN_00518960 (dump 75409-75491; 518960-518b2c):
  1. write(S1.tag, S1.x, S1.y, S1.z).
  2. d = turn. If side_flag (byte [F-0x69], the 3rd argument) is set: d = -d, s = +1. Else s = -1.
  3. Sharpen only if **d > 0.6283185307 rad (36 deg, TBYTE@518b30, hard-coded)** and **S1.z >= 0.01** (TBYTE@518b3c).
     So only turns towards one side are sharpened (side_flag = 0: left/CCW turns).
  4. t = tan(ang) (FUN_004a1f04 = sin/cos, p_460000.c dump 51307). Offset each segment sideways by
     s*z*t at each end, using the left normal FUN_004a1b40 (p_460000.c dump 51075:
     (-dy, dx)*dist/len): A' = S0 + n(S0S1)*s*S0.z*t, B1 = S1 + n(S0S1)*s*S1.z*t,
     B2 = S1 + n(S1S2)*s*S1.z*t, C' = S2 + n(S1S2)*s*S2.z*t.
  5. I = intersection of line A'B1 with the line through B2 parallel to C'B2 (FUN_005188c4, dump 75381; it gives up
     if den = uy*vx - ux*vy = 0).
  6. write(0x0c, I, z=0), write(0x0c, S1, S1.z). This is **S1 (depth) -> apex at the top surface (z=0) -> back to S1**.
     The apex is where the two V-wall top edges (bottom contour offset by depth*tan(half angle)) meet.

### 3. Conventions
- Output z is a **positive depth** (0 = top surface, D = full depth). Move-tos (0x0b) written by these
  functions have z = 0. engrave.dll negates z when reading.
- Tags written: 0x0b move-to, 0x0a full-depth line (Spiegel only when both ends are at z' > 0.99, Grund
  for input 0x0a), and 0x0c 3D point-to-point for every sharpening move. Spiegel 'A' writes only 0x0b/0x0c.
- Loops are not explicitly closed here. The closing point comes from the input loop: Spiegel passes it
  through, and Grund writes it in close().

### 4. Self-check (selftest.c; inch = mm/25.4; op2: rt 0.005, 15 deg, D 0.135 -> L = 0.041173 in, ratio 1.138224)
Hand-made VekAR2 input: offset corner C (NCI) -> contour vertex V (op2 chain point, z = 0) -> C, all in mm.
| corner | port apex (z=0) | NCI apex | diff |
|---|---|---|---|
| C=(-0.589964,1.697857), V=(-0.591579,1.816875) | (-0.591383, 1.802422) | (-0.591350, 1.802466) | 5.5e-5 in |
| C=(-0.381669,1.154872), V=(-0.334971,1.147955)* | (-0.340642, 1.148795) | (-0.340165, 1.147676) | 1.2e-3 in |
\* The chain has no exact vertex there (NURBS samples). The extrapolated vertex lies 0.0014 in from the nearest sample.
The move sequence matches the NCI: C(-0.135) -> apex(0) -> C(-0.135). A loop that starts at V gives
0x0b apex(0), apex(0), C(D) ... C(D), apex(0), which is the NCI's "start at the corner at z=0 and descend".
'A' mode keeps only the corner moves (cut at Dm = D-0.02 mm = 0.134213 in) and starts each one with a 0x0b.
Grund (no oracle; op4 is '2'): CCW 10 mm square, D 3.429, 15 deg, side_flag 0: the apex is at the outward
diagonal (10.918797, -0.918798), i.e. offset by D*tan15 = 0.918798. side_flag 1: no sharpening.

### 5. NOT DECODED / open (updated 2026-11-19)
1. The VekAR2 corner / ridge records: decoded through the precompute (sec. 3.4): the samples are the vertices of each
   segment's nearest-segment region (line-line bisectors, own offset line at W), d = height / W along the segment normal; mode 0
   writes them with z = d / (L / W) where the envelope is below L (sec. 4.2 step 5). A ridge point is where two bisectors meet;
   its z' comes from that d. Measured: the op 2 sharpening moves match to 0.003 in (sec. 9) and the remaining 0.077 one is an
   input effect (sec. 10).
2. Grund: a 0x0a/0x0c record while count == 0 (after a non-0x0a/0x0b/0x0c tag, or at the start of the file).
   The original then works on "slot -1" = [F-0x87], which overwrites its own count byte and the tool angle.
   The port returns EG_SHARP_ERR_NOT_DECODED.
3. KAbrundungswinkel (block 7 +0) is **not used by either function**. Spiegel has no threshold, and Grund uses
   the constant 36 deg. The offset precompute uses it (sec. 4.8 item 2).
4. side_flag = **KGegenlauffraesen** (block 7 +0x14): the parent frame of FUN_00528dac is FUN_0052919c's, which holds block 7
   at [ebp-0x6c] (0x5287a3), so [-0x58] = +0x14; read at 528d1e / 528d63 (-> FUN_00517ac8), 528e52 (precompute), 528de9 (start
   point, xor 1), 528f49 / 528f78 / 528fac (offsets). Meaning: the travel direction, and with it the offset side. engrave.dll
   always writes GravurTyp vertieft, so FUN_00528784 reverses the contours exactly when Gegenlauf = 1 (sec. 7.3 step 8: outer
   contours / islands CW, holes CCW) and the offsets then go to the right of travel (normal side = flag ^ 1 = 0); with
   Gegenlauf = 0 the contours stay CCW / CW and the offsets go to the left. Both put the offset inside the engraved area. In Grund
   sharpening (sec. 5.2) side_flag = 1 negates the turn, so the turns sharpened are always the convex corners of the offset side.
5. Not ported (I/O and display only): FUN_00468270/4bc3d0 progress, FUN_0046e05c/0046e1d4/0046e3e4 drawing, and
   file buffering/flush errors (0x32).

## 6. The roughing ("Abraeumen") Z / Y / C / D and its merge with the finishing

Port: `viewer/engrave.c`, the section marked "port of eg_rough.c" (the decode was ported as that module; its file-scope helpers carry the prefix `rgh_` in engrave.c, the public `eg_*` functions keep their names). Self-check paths below refer to the decode work area, not the repo.


Line numbers `l.N` are from `decomp/engrave/est/p_4b0000.c`; `@addr` = read from the disassembly (`dis.py`).
Disassembly extracts used are saved next to this file (`mod_rough_dis528104.txt`, `dis51064c.txt`, `dis4f24.txt`,
`dis4f27b0.txt`, `dis4f3334.txt`, `dis4f3100.txt`, `dis4f3094.txt`, `dis4f39e0.txt`, `dis511310.txt`).
Port: `eg_rough.c` / `eg_rough.h` (each function names its source). Units mm.

Ghidra stack-argument order caveat: in this dump, for `__register` functions Ghidra lists the stack args with
`param_4 = [ebp+8]` = the LAST pushed one. All argument meanings below were checked against the pushes.

### 1. Entry FUN_00528104 (l.86733-86870)
Called once per block from FUN_00528dac (l.87281, pushes @529006-529073). Arguments (meaning):
R = tool rows (parent -0x74), block 7 copy (-0x6c), PTR_DAT_0052ed80 = drawing box (18 bytes: f32 @+1 xmin, +5 ymin,
+0xa xmax, +0xe ymax), layer = `RAHMEN.TMP` if `*PTR_DAT_0052f3b4 & 0x40` and the file has > 3 bytes, else '' (FUN_004de528
l.31735), GravurTyp char, AbraeumVerf char, idx (i16 of the tool with max offset, progress only), angle = `*param_2` of
FUN_0052919c, W (pre-computation width), maxR = max over tools of `R+0x18 + *offset` (FUN_00528784 l.86945-86952), slot =
last used tool slot (l.86939-86943).
- spacing `s = (2 - KAbraeumueberlapp/50) * R[slot].rt` (@528431-528456; = tip dia x (100-ov)/100; op4: 0.1905 mm = 0.0075 in).
- 'Z' (l.86833-86843): `FUN_0051001c(VekAR1.TMP -> VekAR10.TMP, block7+0x14, 1, idx, maxR, W)` = boundary = prepared
  contours offset by maxR (the same offset as the finishing pass when one tool) -> `FUN_0051064c(VekAR10, layer, Gravur=='V',
  idx, box, angle, s)` -> `FUN_0051170c(VekAR10, file(10), file(slot), connect=1, KZeilenVerbinden(+0x1a), idx, s)`.
- 'Y' (l.86825-86831): same offset, then `FUN_005108d4(VekAR10, file(10), layer, idx, box, angle, s, Gravur=='V')`; no merge.
- after any Verf except 'K', if KAbrWegZusammen (+0x1c): KAbraeumZuerst (+0x19) = 0: `FUN_00527dfc(file(10) -> append to
  file(slot))`, file(10) emptied (FUN_004c4720 = Rewrite+Close, l.15250); else finishing appended to file(10), file(slot)
  emptied (l.86845-86862). FUN_00527dfc (l.86594) copies only tags 0x0a..0x0c.
- tail in FUN_0052919c (l.87399-87407, @5293cd-529468): 'Z' && KAbrZeilenRund (+0x1d) && !KAbrWegZusammen:
  `FUN_00513e38(file(10) -> VekAR2.TMP, 5, s/2.5)`, rename back. Runs after FUN_00528688 (re-sort, sec. 6).
- 'B' FUN_00527f94 (l.86690): `st = (1 - ov(+0xc)/100) * R[slot].rt`; if `param_12 > 0 && st >= 0.01` (TBYTE @527ff0): if
  `st <= param_12`: `n = max(1, Round(param_12/st))`, step = param_12/n, VekAR1 -> VekAR4 copy, `FUN_0051a298`-family
  `FUN_0051a6c8(VekAR4, VekAR10, block7, Gravur, 0xc, n, step, maxR, W)`, append to file(10); else error 0x69.
  FUN_0051a6c8 is the ring/offset module (not decoded here).

### 2. Hatch set-up FUN_0051064c ('Z', l.69055-69174, @51064c) and FUN_005108d4 ('Y', l.69177-69297)
1. Nothing if VekAR10 has <= 0x27 bytes (FUN_004c48c4 l.15352, a FindFirst size; <= 3 records). VekAR10 then still holds the boundary.
2. `FUN_00510438` (l.68955): VekAR10 -> VekH2R0.H2R as 9-byte records (tag, x, y f32) and box over tags 0x0a/0x0c only
   (out-param order checked @510438: ecx xmin, [ebp+0x10] ymin, [ebp+0xc] xmax, [ebp+8] ymax).
3. q = s/4: `xmin' = min(xmin - q, G.xmin)`, `ymin' = min(ymin-q, G.ymin)`, `xmax' = max(xmax+q, G.xmax)`, `ymax' = max(ymax+q, G.ymax)`
   with G = drawing box (@5106bc-51076a).
4. Param block (@51076a-5107dc): `{xmin', xmax', ymax', ymin', s, e, angle, flag=(Gravur=='V'), link, oneway}`:
   'Z': e = max(<uninitialised stack float>, s/4) (@510797, NOT DECODED value; measured = s/4), link (+0x1d) = 1, oneway (+0x1f) = 0.
   'Y': e = s/4 (l.69277), link = 0, oneway = 1.
5. If flag = 0 and layer != '': layer (RAHMEN) appended to VekH2R0 (FUN_004f54fc l.46788) and flag := 1 (l.69156).
6. `FUN_004f39e0(VekH2R0 -> VekH2R1, idx, &err, &block)`; err != 0 -> global error = err + 0x14 (l.69161).
7. 'Z': if no error, layer != '' and |angle| > 0.01: layer contours appended to VekH2R1 (l.69165). Then VekH2R1 -> VekAR10 with
   z = 0 (FUN_004f5348 rewrite, l.46703/46770). 'Y': VekH2R1 appended to file(10) (z = 0). Temp files deleted (FUN_004c4970).

### 3. Hatch core FUN_004f39e0 (l.45673-45999, @4f39e0)
- n = records of VekH2R0; n < 1 -> err 0x15. `|angle| >= 2 pi` -> err 0x19 (@4f3aba: angle is in radians here; the caller's
  degree->radian conversion is outside this module). e < 0 -> e = 0.
- File box FUN_004f378c (l.45576): over tags 0x0a/0x0c, each side +-0.01 (@4f38a4). flag != 0: block box := file box
  (@4f3b04); flag = 0: err 0x14 if file box (without the 0.01) leaves the block box (@4f3b35-4f3b92).
- angle != 0 (l.45805-45861): rectangle of the block box (0b (xmin,ymin), 0a (xmax,ymin), (xmax,ymax), (xmin,ymax),
  (xmin,ymin)); flag = 0 -> appended to VekH2R0 and flag := 1. New box = bbox of the 4 rectangle corners rotated by -angle,
  +-0.01; whole VekH2R0 rotated by -angle (FUN_004f391c l.45628). Rotation FUN_004f24ec (l.44625): polar, phi stored f32.
- constants: `s/20` (@4f3db0), `(36 s)^2` (@4f3dc8), `1.9 s` (@4f3dd6).
- first row `Y = ymax - s/8` (double, @4f3e6c). With flag = 1 ymax = boundary max y + 0.01, i.e. **first row = top + 0.01 - s/8**
  (op4: all three regions, top - 0.000543 in, exact).
- n < 15000: one strip = all records, strip bottom = ymin (@4f3e97). n >= 15000 (@4f3ee2-4f4077): strips of height
  `(ymax-ymin)/(n/6000)`; a record is kept if inside [low, top] or if the segment from the previous one crosses `top`
  (t in [0,1]); dropped runs re-insert the previous record before the next kept one (FUN_004f243c l.44590). >= 30000
  records in a strip -> band halved, < 750 -> err 0x17 (@4f4132-4f4190). Loop while strip bottom > ymin.
- per strip: link (+0x1d): rows -> HELP.TMP (FUN_004f2a80 with p2 = 0), then FUN_004f3334(HELP.TMP -> VekH2R1);
  else FUN_004f2a80(VekH2R1, p2 = +0x1f) directly.

#### 3.1 Rows FUN_004f2a80 (l.44878-45069, @4f2a80)
State: dir = p2, a = 1 (outside), first = 1, firstShift = 1, once = 1, pending = 0, pA = pB.
Per row: if p2 = 0: dir ^= 1 (first row dir = 1 = +x; zigzag alternates; 'Y' always +x).
- Crossings FUN_004f27b0 (l.44762, @4f27b0): for consecutive records i-1, i with record i tagged 0x0a/0x0c (a 0x0b record
  starts a new contour, the jump is not intersected), `t = (Y - y0)/(y1 - y0)` in (0, 1) open -> x = (x1-x0) t + x0 (f32).
  If a record y equals Y exactly (float vs double) or the count is odd: tries++, Y -= s/20 while firstShift else Y += s/20,
  retry up to 10 times; > 10 tries: dir ^= 1 and the row is skipped. After a successful row firstShift = 0.
- Row start: first row: move-to (xmin, Y); later rows when a = 1: move-to at xmin (dir = 1) / xmax (dir = 0), pA = pB = x.
- Crossings taken in sweep order (dir = 1: smallest x first, `x <= lim`; dir = 0: largest, `x >= lim`), only those strictly inside
  (xmin, xmax). Entering (a = 1 -> 0): `pB = x - e` if (flag ^ dir) else `x + e`; emit `kind = flag ^ 1` at pB if
  `(pB > pA) == dir`. Leaving (a = 0 -> 1): `pA = x + e` if (flag ^ dir) else `x - e`; emit `kind = flag` at pA if `(pA > pB) == dir`
  (FUN_004f2a44 l.44864). Row end (no crossing left, a = 1): kind flag ^ 1 at the far box side (FUN_004f29e8 l.44848).
  => flag = 1 (vertieft / with frame): cut the intervals c1-c2, c3-c4, ... shortened by e at both ends; intervals with
  length <= 2e vanish. flag = 0: cut the complement (box side to c1 - e, c2 + e to c3 - e, ...).
- kind 0 = remember move-to (0x0b, x, (f32)Y); kind 1 = line-to (0x0a): written together with the pending move-to
  (FUN_004f2644 l.44698). Without +0x1d the records are rotated back by +angle there.
- Next row: `Y -= s`; if `Y <= stripLow && stripLow - Y < s/2`: `Y = stripLow + s/8` (@4f2cc4-4f2cfe); then (link only, once)
  if `0 <= ymin - Y <= 0.75 s`: `Y = ymin + 0.01` (@4f2d3b-4f2d74). Stop when `Y <= stripLow` (stripLow := (f32)Y).
  op4 region 3 last row (-0.737951, 0.005284 in below the previous) is this rule.

#### 3.2 Linking FUN_004f3334 (l.45321-45573, @4f3334)
- Segments from HELP.TMP: every 0x0a record with the last non-0x0a record as its start (max 45000 -> err 0x18).
- For each unused segment i in file order: write 0x0b start, 0x0a end; then repeat: among unused segments j > i whose
  start y is strictly within `end.y +- 1.9 s` take the nearest start (`d^2 < (36 s)^2`, strict, first minimum).
  If the straight joint end -> start does not cross the boundary (FUN_004f2f20 l.45109 / FUN_004f2e08 l.45072: all
  consecutive record pairs of the strip incl. the jump between contours, both parameters in [-0.0001, 0.9999]): write
  0x0a start_j, 0x0a end_j. Else FUN_004f3100 (l.45226): if start_j.x lies within the x span of the current segment,
  go back along the row to `Q = (start_j.x, end.y)` and down: if Q -> start_j does not cross, write 0x0a Q, 0x0a start_j,
  0x0a end_j. Otherwise the path ends (no other candidate is tried); the next unused segment starts a new 0x0b path.
- Output records rotated back by +angle on flush (FUN_004f3094 l.45197).
=> zigzag rows are joined by straight short segments between the row ends (or along the row then straight down).

### 4. Merge FUN_0051170c -> FUN_00511310 (l.69953 / l.69772-69950, @511310)
- `thr = 0.7 s` (@511365); finishing file(slot) loaded whole (FUN_004c3ae0 l.14567).
- Subpaths of VekAR10 (FUN_00510c6c l.69417): split at 0x0b, kept only with >= 2 records; start = the 0x0b point, end = last
  non-0x0b record; a 0x80 record with z > 0 sets thr = 0.7 z for following subpaths; < 4 records -> short error 0x44f.
- Order: start at subpath 0 (connect = 1, 'Z') or the last (connect = 0). Loop: nearest unused subpath start to the current
  position (FUN_005110dc l.69677: Euclid, strict <, scan ascending for connect = 1). Written joined (its 0x0b becomes 0x0a when
  the next record is 0x0a/0x0b, else dropped; FUN_00510f84 l.69585) if it is not the first after a break and
  `(dist < thr || KZeilenVerbinden) && the joint does not cross the finishing path` (FUN_00511194 l.69705: params in
  [-0.001, 1.001]); else written with its 0x0b (retract + plunge). On a refused joint the next subpath is the first unused in
  index order (not the nearest). Only tags 0x0a..0x0c are written; VekAR10 is erased afterwards.
- z: hatch records carry z = 0; depth comes from the later depth-cut stage (FUN_005299e8, other module).

### 5. Corner rounding FUN_00513e38 (l.71861-72088), KAbrZeilenRund
d = s/2.5, n = 5 (max 8). For each 0x0a/0x0c/0x12 record N with previous corner C and its predecessor P, unless P is
missing or a 0x0b (first corner of a path stays sharp): A = C - d (P->C unit) (midpoint of P,C if |PC| < 2d), B = C + d (C->N unit)
(midpoint if shorter); emit A then the quadratic Bezier (A, C, B) at t = k/n, k = 1..n (FUN_00513b7c l.71770, FUN_00513994
l.71707). Emission skips points within 0.001 of the last (FUN_0051380c, @51382e) and zeroes z of 0x0a/0x0b; 0x0a records
take z from the last 0x9c record's +5 field; other tags are copied (FUN_005138f4).

### 6. Self-check (op4, test/test_op4.c + test/cmp.py)
Boundary = the three finish passes of the NCI (arcs sampled at <= 0.002 rad), mm, flag = 1 ('V'), e = s/4, one call per
region (KBlockweise: each region has its own phase), merge with KZeilenVerbinden = 1 (EST_DAT constant `KZeilenVerbinden=JA`).
| region | NCI points | port points | rows NCI/port | max |dy| | max |dxy| | mean |dxy| |
|---|---|---|---|---|---|---|
| 1 (top left) | 52 | 52 | 26/26 | 1e-6 in | 0.00041 in | 0.00010 in |
| 2 (top right) | 52 | 52 | 26/26 | 1e-6 | 0.00023 | 0.00007 |
| 3 (bottom) | 44 | 44 | 22/22 | 1e-6 | 0.00031 | 0.00004 |
Every row, row direction, joint and the single 0x0b per region match; x deviations are the NCI arc-filter approximation
of the boundary (0.0002 in tolerance). The top row of regions 1/2 (in the pointed tip) vanishes (interval < 2e), so their
first visible row runs -x; region 3's first row runs +x - both as the code predicts. e = s/4 fits best (e = 0.05 mm: mean
error 1.3-2.5x; 0.06 mm: 5x), so the uninitialised value was <= s/4 in that run.

### 7. Path-parallel roughing 'C' (AbraeumVerf=Radius) and 'D' (Durchm) (decoded 2026-11-19, EG-3)
Port: `viewer/engrave.c`, section "the path-parallel roughing 'C' ... 'D'" (`cd_*`, driver `eng_rough_cd`). Citations: `l.N` =
`p_4b0000.c`, addresses = the patched exe's disassembly; every stack argument was mapped from the pushes at the call site
(Ghidra's `param_4` = `[ebp+8]` = the last push). Engine error = `*(int*)PTR_DAT_0052f230`.

**Entry FUN_00528104 'C' (0x5281ea-0x5282b8) / 'D' (0x5282bd-0x528391).** ECX of FUN_00528104 = `[FUN_0052919c ebp+0xc]` = the
pointer FUN_00529e94 passes to FUN_00529e38 (0x529f35) = its local box filled by FUN_00529514 -> FUN_004dce20(VekARH0, 0, box) ->
FUN_004dcb90 (l.30007): the working layer's first two records when they are a `{1, xmin, ymin, zmin} {5, xmax, ymax, zmax}`
header, otherwise the box over tags 0x0a / 0x0b / 0x0c / 0x11 / 0x12 from FUN_00499018 (+-100000); zeros for an empty layer.
1. FUN_004c4720(VekAR10) (empty).
2. box = FUN_00519508(layer box, W) (l.76119): layer box -+ W, then united with the Grenze box PTR_DAT_0052ed80 (f32).
3. step = `(1 - KAbraeumueberlapp(+0xc) / 100) * rt[slot]` in x87, pushed f32 (0x528204-0x528232; 100.0f @0x528678, 1.0f @0x52867c).
4. 'C': FUN_0051a298(VekAR10, layer, Gravur=='V', colour, box, block 7, maxR, step, maxR, W) (pushes 0x5281f4-0x528252).
   'D': FUN_0051abac(VekAR1 (unused), VekAR10, layer, colour, box, block 7, Gravur=='V', maxR, step, maxR, W) (0x5282c7-0x52832b).
5. FUN_0051170c(VekAR10, file(10), file(slot), connect = 0, KZeilenVerbinden, colour, `(f32)(rt[slot] * 2.0f)`) (0x52825c-0x5282b3;
   sec. 6.4: the start threshold 0.7 x 2 rt is replaced by 0.7 z of every 0x80 record, here the first record 2 maxR).
6. KAbrWegZusammen, error 100 -> 0, short error 0x44f -> 0 as for 'Z' (sec. 6.1). FUN_00528688 re-sorts C / D / Z (sec. 7.4b).

**FUN_0051a298 ('C', l.76902, 0x51a298-0x51a39e)** and **FUN_0051abac ('D', l.77335, 0x51abac-0x51ac93)**, common part:
- nothing when `0.01 (TBYTE @0x51a3a4 / @0x51ac98) > maxR`.
- FUN_0051973c(VekAR10, (f32)(maxR * 2.0f)) (l.76221): appends `{0x80, 0, 0, z}` (Reset + Seek(FileSize), FUN_004bbcf4 mode 2).
- FUN_00519588("VekAR11", Gegenlauf, colour, 1, maxR, W) (l.76151, 0x519588-0x519677) = the boundary, from the block's precompute
  (FUN_00528dac). FUN_00519588 is FUN_0051001c's retry loop (sec. 4.4: step (W - dist)/2 capped 0.01 (TBYTE @0x519680), 0.999 W
  (TBYTE @0x519690), 6 tries, 0x67) around FUN_0050443c; its flag (try 5 && 1) is FUN_0050443c's 5th argument, which only reaches
  FUN_00503ed4's ECX, never read there (0x503ed4-0x504137: ECX is pushed / popped around the name copies only). Port: eg_offset_retry.
- Gravur != 'V': FUN_00510128(VekAR11, VekAR12, layer, colour, Gegenlauf, box) and FUN_00510128(VekAR10, ...) (l.68785, 0x51a31c-
  0x51a34f / 0x51ac29-0x51ac5c). With layer '' (the port never has a RAHMEN file, as for 'Z' / 'Y') it appends 5 records, z = 0:
  0x0b (xmin, ymin), 0x0a (xmax, ymin), (xmax, ymax), (xmin, ymax), (xmin, ymin) for Gegenlauf = 0; (xmin, ymax) and (xmax, ymin)
  swapped otherwise (l.68877-68908). A layer would be copied (FUN_004c3cf0, reversed by FUN_004f129c when Gegenlauf = 0): not used.
- 'C' (0x51a354-0x51a393): `n = Round(maxR / step)` (x87 FDIV, FUN_00403390 = banker's rounding), compared as i16 into 1..100;
  if error = 0: FUN_0051a154(VekAR10, n, colour, block 7, step).
- 'D' (0x51ac61-0x51ac88): if error = 0: FUN_0051aa5c(VekAR10, 1, colour, block 7, (f32)(step * 2.0f)).

**FUN_0051a154 ('C', l.76849) / FUN_0051aa5c ('D', l.77279)**: block 7 +0 (KAbrundungswinkel, f32 - the parser's working copy;
the finishing precompute reads the +4 copy) := clamp to [10, 30] **in place** (0x51a174-0x51a19b / 0x51aa7c-0x51aaa3: `< 10 -> 10`,
`> 30 -> 30`; the change and the decrements below persist into later blocks); VekAR8 emptied; then while error = 0: core(VekAR11 ->
VekAR10, next VekAR8), and if still 0 core(VekAR8 -> VekAR10, next VekAR11) - the last ring of one call is the source of the next.
Error 100 -> 0 at the end. The core's `[ebp+0x18]` (only the z of the 0x80 records below) starts at step ('C') / (f32)(1.2 (TBYTE
@0x51ab88) x 2 step) ('D') and grows by n x step after each successful call.

**Ring core 'C' FUN_00519e68 (l.76687, 0x519e68-0x51a114)**, `[ebp+8]` block 7, +0xc 'V' (constant push 0x51a1c7), +0x10 colour,
+0x14 n, +0x18 z, +0x1c step; src / out / next = EAX / EDX / ECX:
- tol = FUN_0051969c(step) (0x51969c, ST0 return: `<= 0.25 -> 0.001, <= 0.5 -> 0.002, <= 0.75 -> 0.003, <= 1 -> 0.004, <= 2 -> 0.008,
  else 0.01`, f32 constants); W' = FUN_00504348((f32)(n * step)) (0x504348, ST0 return: `w = (f32)(1.05 w)`, `0.03 > w -> 0.03f`,
  `0.01 + x > w -> (f32)(0.01 + x)`, TBYTEs @0x5043a0 / ac / b8). k = 1 (ebx), tries = 0 (esi).
- loop: FUN_00504154(src -> VekAR9, colour, pushes step, W', -KParamNormGr, tol) = the contour preparation FUN_004fb97c with
  `[ebp+0xc]` = W' and the tolerance `[ebp+0x14]` = tol (sec. 7.3: the finishing pass pushes 1e-4 there). FUN_004c449c(VekAR9) <= 3
  records -> error 100. Else box9 = FUN_004c3bb0(VekAR9) (tags 0x0a..0x0c, init +-100000) +- 0.01 (FUN_004c4540; 'V' is always
  pushed), tries++, error := -2, FUN_005041a4(VekAR9, Gegenlauf, pushes W', (f32)(TBYTE pi/180 @0x51a124 x block7[0]), **pi**
  (0x40490fdb)), error < 0 -> 0. The -2 only meets `error < 1`, `0 < error` and the display test `< -1` inside the precompute
  (l.54098, 54106, 54663, 55972, 57363, 57444, 57458; FUN_004fcca0 l.53733 restores it per retry): the same paths as 0.
- rings: while error = 0 and k <= n: [FUN_0051973c(next, z): appended, then lost - FUN_00519588 rewrites next], z += step,
  FUN_00519588(next, Gegenlauf, colour, tries == 3, (f32)(k step), W') from the VekAR9 precompute; error = 0 and a record of next
  outside box9 (FUN_004c45e4 l.15182) -> 0x67; error = 0 -> k++, FUN_004c4344(next, out, 4) = byte append (both names .TMP:
  FUN_004c41d4 type 0 -> FUN_004c3cf0(.., 1)).
- error 0x67 and tries < 3: error 0; tries = 2: W' = FUN_00504348((f32)(k step)), n = k (local); else W' = (f32)(1.1 W') (TBYTE
  @0x51a130); block7[0] > 45 -> 45 else -= 1.0f; tol < 0.01 (TBYTE @0x51a144) -> tol x 2.0f else (f32)(1.1 tol); the loop restarts at
  the preparation and continues with ring k. Otherwise tries := 0xff. Loop while tries < 3 and error <= 0.

**Ring core 'D' FUN_0051a7a0 (l.77149, 0x51a7a0-0x51aa13)**, `[ebp+8]` block 7, +0xc colour, +0x10 n (= 1), +0x14 z, +0x18 step
(= 2 x stepover): as 'C' with these differences: the precompute gets A = **0.62831855** (0x3f20d97c); the ring index restarts at 1
on every try (0x51a8a6); per ring k: FUN_00519588(next, .., (f32)(k step), W'); error = 0 and outside box9 -> 0x67; then if error is
0 **or 100**: FUN_0051973c(next, z) (kept: after the offset; the preparation ignores 0x80), z += step, **FUN_00519dc8**(next, out,
Gegenlauf, colour, W', (f32)(k step)), and error 100 ends the ring loop; the next ring is NOT appended to out. Retry: tries = 2 ->
W' = (f32)(1.01 (TBYTE @0x51aa30) x step), else (f32)(1.1 W') (@0x51aa3c); block7[0] > 45 -> 45 else -= **2.0f**; tol as 'C'.
- FUN_00519dc8 (l.76644, 0x519dc8-0x519e57): FUN_005041dc(name+'N' (unused, p7 = 0), "VekAR2.TMP", Gegenlauf, tol index 1, colour,
  **mode 0**, p7 0.0, dist (f32)(k step), W') (pushes 0x519df9-0x519e04) = the offset core with the envelope samples (sec. 4.2,
  z = d / r), then if error is 0 or 100: FUN_00519904(VekAR2 -> append to out).
- **FUN_00519904** (l.76356, 0x519904-0x519da1): records read 500 at a time; L = previous record (init zeros), last = last written
  (init zeros), the pending-move flags p1 / p2 (init 0), T = 0.49 (TBYTE @0x519db0). 0x0b: L := r, p1 := 1. 0x0a / 0x0c (tag set to
  0x0a): if r.z >= T or L.z >= T: L.z >= T and r.z >= T -> cur := r, p2 := 0; L.z >= T > r.z (leaving) -> cur.xy := the T crossing,
  cur.z = 0.49f, p2 := 1 (cur's tag is **not** written here); L.z < T <= r.z (entering) -> L.xy := the crossing, L.z := 0.49f, cur := r.
  Crossing: a = (f32)(r.x - L.x), b, c = (f32)(r.z - L.z), d = (f32)(T - L.z), x = (f32)(a / c * d + L.x) (0x519aed-0x519b71). Then
  p1 -> L.tag := 0x0b, L.z := 0, last := L, write L, p1 := 0; then unless |cur.x - last.x| <= 0.001 and |cur.y - last.y| <= 0.001
  (TBYTE @0x519dbc): cur.z := 0, last := cur, write cur, p1 := p2. Else (both below T): p1 := 1. Finally L := r. Writing
  (FUN_00519860): a record is dropped when its f32 distance (FUN_004a2bd4, x87 sum, Sqrt, fstps) to the last written one is
  `< 0.001` (FUN_004a2c14, TBYTE @0x4a2c38), init {0, -9999, -9999, 0}; buffered 5000 records, appended (Reset mode 2 + Seek).
- => 'D' cuts every ring at 2 x stepover only where the envelope height reaches 0.49 of the ring distance, i.e. the rings plus the
  centre parts between them that the 2 x stepover rings leave.

Port: `cd_rough` / `cd_c_core` / `cd_d_core` / `cd_mid` / `eng_rough_cd` (merge, KAbrWegZusammen via the sec. 6.4 port); the ring
preparation uses `eg_contour_prep_tol` (the sec. 7.3 port with the pushed tolerance), the precompute `eg_offset_precompute`, the
offsets `eg_offset_retry` / `eg_offset_core` (mode 0).

**NOT DECODED / refused here:** (1) FUN_00519904 writes `cur` with the tag byte `[ebp-0x1cf0]` that only a copy of a record ever
sets: when the first written `cur` of a call comes from the leaving branch, that byte is stack garbage of earlier calls (the frame
is 0x1e80 deep, overwritten by the precompute / offset calls in between) -> undefined, refused (`cd_emit`). (2) A RAHMEN layer
(FUN_004de528) is not modelled (layer '' only), as in 'Z' / 'Y'. (3) The precompute's own NOT DECODED / undefined cases (sec. 3.7)
refuse as elsewhere (the contour preparation's split path is decoded, sec. 7.3).

**Oracle (Wine, EG-3, tools/tests/engrave_wine.c):** a set of 130 cases - e1 shapes x {V15, V45, flat r 0.25, flat r 0.5} x overlap
{20, 50}; e1 with Bahnoptimierung off, Knachsortieren off, KAbraeumZuerst, KAbrWegZusammen (with / without KAbraeumZuerst),
Gegenlauf, KBlockweise 1 / 2 / 4, KAbrundungswinkel 5 / 40 / 150 (both clamps), Grund (V45 2D and flat), GravurTyp erhaben (the
frame rectangle; also with KBlockweise 1 and Gegenlauf), KParamNormGr 3, KStartpunkt 3; text in 4 fonts x 3 strings x {V15, V45
overlap 50, flat with Bahnoptimierung off}; each for C and D - **130 / 130 identical** (C 65 / 65, D 65 / 65; 'C' with n = 1..12
rings per batch). A random pool of 310 text cases (24 fonts, V 15 / 30 / 45 and flat tools, overlap 10..60, all switches random):
**293 / 310 identical** (C 143 / 152, D 150 / 158), including all 19 cases that take the 0x67 retry branches (C tries 1 / 2, D tries
1 / 2; 9 of them end with the engine error 103 = 0x67 on both sides). The 17 differing cases differ in the same way with
AbraeumVerf = keine (finishing only, Wine re-run): they are not C / D (9 x the original reports FEHLER 100 where the port returns
an empty toolpath; 6 x one f32 ulp in 0x0a / 0x0b / 0x0c records of the finishing or the 3D sharpening; p084: a finishing 0x0b
start 2 mm off; p264: the box header record). Earlier classes re-checked after the change: 57 / 59 of the Wine cases in
`cases/` (non C / D) and `cases_nobo/` identical, the 2 others (e6_comb, the original crashes) unchanged; `engrave_test.sh` unchanged.

### NOT DECODED / not ported (updated 2026-11-19)
- The uninitialised `e` (FUN_0051064c [ebp-0x238], read @510797 as `max(e, s/4)`): **documented-undefined**. FUN_00528104's 'Z'
  branch calls FUN_0051001c (5284a9) and FUN_0051064c (5284f7) with the same stack depth (4 pushed words each), so both frames
  have the same ebp E, and [E-0x238] is a word of the earlier call tree: in FUN_0050443c's frame (ebp E-0x230, called with 4
  pushes from 5100a9) it is [ebp-8], byte 0xf8 of the 256-byte name copy at [ebp-0x100] that only receives the short name -
  not written; before that, FUN_0051001c calls the progress display FUN_004bc3d0 (51005a) -> FUN_00468270 -> FUN_00468190
  (only when 0x5342db) and FUN_00468260 -> **gdi32 SetROP2** (thunk 0x408488, IAT 0x537cec), whose ret address sits at E-0x230:
  [E-0x238] is a word written inside gdi32 (a saved register or local of its prologue) - Windows-version dependent. Not
  derivable from the exe. The coaster op 4 rows fit e = s/4 (sec. 6.6: 0.05 mm gives 1.3-2.5x the error), i.e. that word read
  as a float was <= s/4 (a pointer, handle, negative or small value; a return address into a system DLL, 0x7xxxxxxx, would be a
  float ~1e30 and wipe the rows). The port keeps e = s/4 (measured), see the question in the branch report.
- Re-sort FUN_00528688 -> FUN_0051bff0 / FUN_0051bef8 / FUN_0051bbfc / FUN_0051170c: decoded and ported (sec. 7.4b,
  eg_rough_resort).
- Degree -> radian of Abraeumwinkel: FUN_00529b50 0x529dc7 (sec. 7.2), ported.
- The boundary offset FUN_0051001c: the decoded offset back end (sec. 4), ported (eg_offset_retry).
- FUN_00513b7c with nseg >= 1001: **unreachable** - FUN_00513e38 caps the count at 8 (l.71929) and its only caller passes 5
  (FUN_0052919c 529445). The KAbrZeilenRund tail is wired since 2026-11-19 (FUN_0052919c 5293bf-529468: error < 1, 'Z',
  KAbrZeilenRund, !KAbrWegZusammen; d = (2 - KAbraeumueberlapp/50)·rt[last]/2.5 in x87, f32; FUN_00513e38(file(10), 5, d)
  replaces file(10)). engrave.dll always writes `KAbrZeilenRund=Nein` (engrave.md 3.3), so Mastercam never reaches it.
- 'B' (FUN_00527f94 -> FUN_0051a6c8), 'S' (FUN_0051d07c) and 'A': **unreachable from Mastercam**. The EST_DAT parser maps
  AbraeumVerf only to Z / Y / C / D / K, anything else to 'Z' (FUN_005229b0 l.83322-83378: DAT_005239f8.. = ZEILEBI, ZEILEUNI,
  RADIUS, DURCHM, KEINE), and engrave.dll writes only those five (engrave.md 3.2). ('B' would also take its width from the
  uninitialised [ebp-0x134] of FUN_00529e38, sec. 7.0 item 4.) Refused, not decoded further.
- **'C' / 'D' (Radius / Durchm, path-parallel roughing)**: decoded and ported 2026-11-19 (sec. 6.7). FUN_0051a3d8 (named in the
  earlier pointer) belongs to 'B' (FUN_0051a6c8), not to 'C' / 'D': 'C' runs FUN_0051a154 -> FUN_00519e68, 'D' FUN_0051aa5c ->
  FUN_0051a7a0 -> FUN_00519dc8 / FUN_00519904. What is left undefined there is listed at the end of sec. 6.7.
- FUN_004bc3c0 user-abort (error 3) and display calls (FUN_004bc3d8/3ec) - not ported (no geometry).

## 7. The pipeline around the kernels: input, tool rows, contour preparation, blocks, re-sort, depth cuts, output

Port: `viewer/engrave.c`, the section marked "port of eg_pipe.c" (the decode was ported as that module; its file-scope helpers carry the prefix `pip_` in engrave.c, the public `eg_*` functions keep their names). Self-check paths below refer to the decode work area, not the repo.


Citations: `FUN_xxxxxxxx @line` = dump line in `decomp/engrave/est/p_4b0000.c` (or `p_460000.c` where
marked `[460]`); `0xADDR` = instruction address from `dis.py` (used for every float formula and for all
stack-argument mappings).

**Warning about the decompile's stack arguments:** for functions with pushed arguments, the Ghidra
parameter order is reversed. Ghidra's first stack parameter is `[ebp+8]`, which is the LAST value pushed.
I checked every mapping below against the push sequence at the call site, e.g. 0x529aa4..0x529acd ->
FUN_004fa8b8 and 0x4faaae..0x4faadd -> FUN_004fa5bc. Nested procedures get the parent frame as their
last pushed value.

### 0. Main flow FUN_00529e94 @87944
1. FUN_00523bf0 reads EST_DAT and fills `[ebp-0x208]`: a float unit factor followed by the input and
   output file names at `[ebp-0x204]` / `[ebp-0x104]`.
2. FUN_004dd3e4(input ENGCONT.LG1 -> working layer VekARH0.TMP). The tolerance pushed at 0x529eea is
   0.001 (0x3a83126f) and the flags pushed are 0 (§1).
   Then, if `[ebp-4]` (BAHNOPTIMIERUNG) != 0 (0x529f19), FUN_00529700: path optimisation of that layer (§1a).
3. FUN_00529514(box) @87430:
   - Gets the layer bounding box through FUN_004dce20 (box = {tag, xmin, ymin, tag, xmax, ymax}).
   - Unions it with the global box PTR_DAT_0052ed80 and sets the view (FUN_00471310).
   - FUN_004b361c(layer, 0xb) @2618 sets error 1 (messages 0x3a/0x43a) if the layer contains tool
     records 0x96..0x9d, 0x0c points or 1/5/6/7/9 records. This only applies when the globals
     0x52eee8/0x52ede8 are set; their values were NOT DECODED.
4. FUN_00529e38 @87922:
   - FUN_00529b50 builds the tool rows and runs the checks (§2).
   - FUN_0052919c @87304 does the compute, with the registers/pushes shown at 0x529e4f..0x529e87:
     - EAX = rows; EDX = &Abraeumwinkel(rad); ECX = &(block 0x11 +0).
     - Pushed: [ebp+8] = 1, +0xc = box, +0x10 = &GravurTyp, +0x14 = &KonturenAuf, +0x18 = &AbraeumVerf.
     - **+0x1c and +0x20 are pointers to `[ebp-0x138]` / `[ebp-0x134]` of FUN_00529e38. These are never
       initialised.** Their floats are read as an "extra offset" (0x528806, 0x528f33) and as a roughing
       parameter (0x529011). The port takes the extra offset as an argument (`eg_slot_info(..., extra)`,
       use 0 unless measured).
5. If error == 0 and `*PTR_DAT_0052f038`: FUN_005299e8 depth cuts (§6).
6. FUN_0050d1a4/FUN_0050d074 concatenate the slot files into layer 1 (§7).
7. FUN_00529884 writes the output by file extension (§7).

### 1. Input: FUN_004dd3e4 @30574 (ENGCONT.LG1 -> VekARH0.TMP)
The input is read with FUN_0049867c in blocks of 5000 records of 13 bytes. Output is buffered through
FUN_004dd094 and flushed by FUN_004dd058 every 5000 records.

- **0x0b (move)** @30866: not written immediately. It becomes the *pending* record `[ebp-0x3c]` with
  flag `[ebp-0x3d]=1`. A trailing 0x0b with no following line is never written.
- **0x0a / 0x0c** @30856:
  - First FUN_004dd168(rec.z) @30409 runs. If a move is pending, it is written unless both hold: the
    pending point is within 0.001 in xy of the last written record (FUN_004a2490 [460]@51559 =
    `dist < 0.001`) AND pending.z == rec.z. In that case a contour that starts where the previous one
    ended, at the same z, is merged into it.
  - Then FUN_004dd0d4 @30352 skips the record if it has the same tag as the last written record AND
    dist2d <= tol (0.001) AND the same z (0x4dd0dc..0x4dd10d). Otherwise it is written.
  - z is copied unchanged. With flags = 0 (0x529eef) the 0x0c conversion branch is off.
- **Initial state** (@30723..30748): last written = pending = {tag 0, -99999, -99999, 0}, and the
  pending flag is 1.
- **Other tags:**
  - Copied raw: 1, 5, 6, 7, 9, 0x16 and 0x96..0x9d (FUN_004dd094).
  - 0x1b goes through FUN_004dd0d4.
  - 0x0e/0x0f/0x14 are kept as state only.
  - **NOT DECODED:** 0x0d arc (FUN_004dd24c -> FUN_004a15d0), 0x10, 0x11/0x12 spline, 0x15,
    0x19/0x1a text, 0x80.
  - The real ENGCONT.LG1 contains only 0x0b/0x0a.

Port: `eg_load_layer`.

### 1a. Input path optimisation ("Bahnoptimierung"): FUN_00529700 @87525 -> FUN_0050fa18 @68359

Run by FUN_00529e94 right after FUN_004dd3e4 when BAHNOPTIMIERUNG (`cmpb $0,-0x4(%ebp)` at 0x529f19; the flag is the byte
the EST_DAT parser stores at 0x522b55, default 1 at 0x523c43). engrave.dll writes Bahnoptimierung = JA whenever the op's
"smooth contours" S+0x13 is on (the coaster ops). Decoded from the disassembly 0x529700..0x5297a9 and 0x50eca8..0x50fcbe;
the Ghidra dump of FUN_0050fa18 is garbled (nested procedures, stack frame of 0x2f40 bytes), use it only for orientation.
Port: `eg_bahnopt` (pipe section), called from `eng_main` after `eg_load_layer`.

**Arguments (FUN_00529700, 0x529709..0x5297a1).** Block 0x17 is read into [ebp-0x1c] (sec. 2: +0 BOVektorAufl V, +8
BOMindStuetzp, +0xc BOMindestausd M, +0x10 BOBahnenSchl). K = 25.4 / V (TBYTE 25.4 @0x5297ac, extended). Register args:
EAX = name of layer 1 (FUN_0049a168), EDX = Round(BOMindStuetzp) (0x403390 = `fistp`, round to nearest even), ECX =
BOBahnenSchl. Pushed, first to last: 0.001 (0x3a83126f), M, A = f32(K·K·2.0f·1.05), B = f32(K·1.05), C = f32(K·1.05), 0xc
(1.05 = TBYTE @0x5297bc). So in FUN_0050fa18 (ret $0x18): [ebp+8] = 0xc (< 0x100: progress display on), +0xc = C,
+0x10 = B, +0x14 = A, +0x18 = M, +0x1c = grid g = 0.001. Defaults (V 10000, mm): K = 0.00254, B = C = 0.002667,
A = 1.355e-5.

**Frame of FUN_0050fa18 (P = its ebp; every helper is a nested procedure that gets P, or its own parent's frame, pushed
last).** Records are 13 bytes {u8 tag, f32 x, y, z}. P-0xf60 current record; P-0xf40 read buffer (300 records), P-0xf48 its
count, P-0xf46 its index, P-0xf50 read position; P-0x1e9c write buffer (300), P-0x1ea2 its count, P-0x1ea0 write position;
P-0x2df8 the window (300 records), P-0x1eba its count; P-0xf44 records read; P-0x2e20 index of the last move; P-0x2e0c the
last move record; P-0x2e1c a saved current record; P-0x1eb0 the "last record"; P-0x2e29 first-move flag (1); P-0xf51 EOF;
P-0x2dfe = min(Round(BOMindStuetzp), 299) (word, 0x50fb69); P-0x2e21 BOBahnenSchl; P-0x2dfc = f32(C·C) (0x50fae8).
Initialisation 0x50fa83..0x50fae1 (all counters and positions 0). P-0x1eb0 and the window are not initialised.

**File.** The layer file is opened (FUN_00498274) and read and written in place: FUN_0050edf4 reads 300-record blocks at the
read position, FUN_0050ef64 writes the write buffer at the write position, at the end FUN_0050ef64 (if the write buffer is
not empty, 0x50fc7f) and Truncate (0x404038 -> SetEndOfFile at the current position) cut the file at the write position.
When nothing was ever written (every contour dropped) the position is the end of the file left by the final read, so the
file stays as it was. The display calls (FUN_0047238c progress strings ")QueV: OptV: OptB: OptF:", FUN_0050eca8 /
FUN_0050ed04 / FUN_0050ed90, FUN_00468270 + FUN_0046e43c per written record in FUN_0050ef64) only draw.

**Read FUN_0050edf4.** P-0xf44 += 1; if the buffer is used up, seek + BlockRead(300) (FUN_0046a680); 0 records -> EOF.
Copy the next buffer record to P-0xf60. If g > 0 (f32 0 @0x50ef60) and the tag is 0x0a..0x0c or 0x1b: x := f32(Round(x/g)·g),
y likewise (0x50eef5..0x50ef3d, extended division, `fistp`, `fildll`·g). Otherwise tags 1 and 5 are skipped (read again).

**Main loop (0x50fb69..0x50fc74).**
- While window count < 300 and not EOF: read. For a move (0x0b or 0x1b): the first one only clears P-0x2e29; later ones:
  if P-0xf44 - 2 == P-0x2e20 (the previous record was a move) the window count is decremented (that move is replaced),
  otherwise FUN_0050f7c0 (finish the contour) and the current record is restored from P-0x2e1c. Then P-0x2e0c := the move,
  P-0x2e20 := P-0xf44 - 1. Records with tag 0x0a..0x0c or 0x1b are appended to the window, 0x0a with z := 0 (0x50fc13).
  Every other tag is dropped.
- On EOF: FUN_0050f7c0. With a full window: FUN_0050f1b0, FUN_0050f3f8, FUN_0050f500 (write the window, no contour test).

**FUN_0050f1b0 (collinear points, A).** For count > 2: di = 0, si = 1, bx = 2..count-1: c = FUN_0050f160(w[di], w[si],
w[bx]) = f32((a.x-b.x)(a.y-c.y) - (a.x-c.x)(a.y-b.y)) (extended products, fstps at 0x50f1a1). |c| > A: di = si. Otherwise
w[si].tag = 0, and if bx - di > 2: mid = (di+bx) >> 1; if |FUN_0050f160(w[di], w[mid], w[bx])| > A then w[mid].tag = 0x0a
(restored as a line whatever it was) and di = mid. si = bx. Then FUN_0050f0d8 removes the tag-0 records.

**FUN_0050f3f8 (close points, B and C).** For count > 2: last = 0, si = 1, bx = 2..: if |w[bx].x - w[si].x| > B or
|w[bx].y - w[si].y| > B: FUN_0050f384(w[last], w[si], w[bx]) true -> w[si].tag = 0, else last = si; otherwise w[si].tag = 0.
si = bx. Compact. FUN_0050f384(a, b, c): t = {(b.x + c.y) - a.y, (b.y - c.x) + a.x} (f32), foot = FUN_0050f2d8 intersection
of the line b -> t with the line through a along a - c; true if (foot.x-b.x)² + (foot.y-b.y)² < P-0x2dfc (extended sum,
0x50f3c0..0x50f3e9). FUN_0050f2d8 (nested, one stack arg, ret 4): d1..d4 f32, den = f32(d2·d3 - d1·d4); den == 0 -> false;
t = f32(((c.y-p.y)·d3 - (c.x-p.x)·d4) / den); out = f32(t·d1 + p.x), f32(t·d2 + p.y).

**FUN_0050f7c0 (finish a contour).** Save the current record to P-0x2e1c; FUN_0050f1b0; FUN_0050f3f8; n = P-0xf44 - P-0x2e20
- 1 (input records of the contour, skipped ones included). If n >= P-0x2dfe and FUN_0050f690: if count > 1, P-0x1eb0 := the
last window record. With BOBahnenSchl: if |move.x - last.x| > 0.01 or |move.y - last.y| > 0.01 (TBYTE 0.01 @0x50fa0c) a
closing line is added after the window (the move record with tag 0x0a, 0x50f972..0x50f996), otherwise the last window
record gets the move's x, y. Without: if count > 1 and FUN_004a23e0(move, last) (f32 sqrt of the extended sum) < 0.01, the
last record gets the move's x, y. FUN_0050f500 writes the window. In every case the window count is reset (0x50f9f7): a
contour that fails the test is dropped.

**FUN_0050f690 (keep the contour?).** n > 300 -> yes; P-0x2dfe <= 2 -> yes. Box of the window (start 10000 / -10000,
0x50f6bd..0x50f6d2). FUN_0050f564(yc = f32((ymin + ymax) / 2)) true -> yes. Otherwise yes iff xmax - xmin >= M and
ymax - ymin >= M. FUN_0050f564: yc := f32((Round(yc/g) + 0.5f)·g); along the window's segments find the crossing of y = yc
with 0 < t < 1 (t = f32((yc - p.y)/dy), dy = f32 != 0) whose x = f32((q.x - p.x)·t + p.x) is the smallest (start 10000,
index bx = 1); returns w[bx-1].y > w[bx].y (the contour runs downwards at its leftmost crossing). So a contour with a box
below M in x or y is kept only with that orientation.

**Refused (EG_ERR_NOT_DECODED, never seen in the oracle cases):** a second move right after a window flush (window count 0:
`decw` wraps, 0x50fbbb); the box / crossing test reading a window slot never written (count 1 reads w[1]; the window is
uninitialised stack); BOBahnenSchl with P-0x1eb0 never written; BOBahnenSchl with window count 0 (writes w[-1], which
overlaps P-0x2e05.. = the move record / P-0x2dfe).

**Oracle (Wine, sec. 9).** All 44 Bahnoptimierung = JA cases that do not use C/D roughing are bit-identical (e1 shapes
K/Z/Y, e2, e4, text f0..f3 Z/Y/Grund, coaster op 2 / 4), and 8 targeted cases (open and nearly closed contours with and
without BOBahnenSchl, contours below BOMindestausd in both orientations, a 1000-point circle and a 400-point line (window
flushes), BOMindStuetzp 10, consecutive moves, BOVektorAufl 100) plus the engine input of tools/tests/engrave_test.sh for
both coaster ops. Before: 1 of 31 identical. The 13 Bahnoptimierung = Nein cases stay identical.

**Coaster NCI oracle (tools/tests/engrave_test.sh) with the step on** - worse, although the engine is bit-identical with
estamp7 on exactly that input (the 2 extra Wine cases above): op 2 stored -> port p95 0.00011 -> 0.00080 in, max 0.00315 ->
0.00232; port -> stored p95 0.00020 -> 0.00058, max 0.00206 -> 0.01464; op 4 stored -> port p95 0.00007 -> 0.00040, max
0.00024 -> 0.00064; port -> stored p95 0.00016 -> 0.00022, max 0.00026 -> 0.00041. The optimiser's result depends on the
vertex density of its input (collinear test on |cross| <= A with a fixed anchor point); the oracle's chains are the 0.0001 in
chord substitute for chain_to_ctour_array (sec. 10), so the stored NCI is evidence about Mastercam's real chain points, not
about this step.

### 2. Tool rows and checks: FUN_00529b50 @87749

**Row construction:**
- Reads block 0xf (tool library) and block 0x11 (the locals at `[ebp-0x40]`). Block 0x11 offsets:
  - +0 -> 529e38 `[-0x12c]`
  - +4 Abraeumwinkel, stored times 0.017453 into `[-0x128]` (0x529dc7)
  - +0x10: 10 x {i16 index, char corr}
  - +0x2e AbraeumVerf, +0x2f KonturenAuf, +0x30 GravurTyp
- Slot validity FUN_00529af4 @87726: idx >= 0, D > 0, and (T+0x3a > 0 or rt > 0).
- Invalid row: idx = -1, all fields 0, corr = 0x33 (@87806..87820).
- Valid row: copies T+0x2a/+0x2e/+0x32/+0x36/+0x3a, color = T+0x54, corr.
- **R (+0x18):** rt if KonturenAuf == 'G' (@87834), otherwise FUN_004a2054(T) (@87838).

**FUN_004a2054** [460]@51368 (0x4a2054..0x4a2233), all f32:
- capped = 0; if D < 0 then D = 0. r = rt.
- If r3a > 0:
  - s = (Rs - rt)/r3a and g = (90 - a)·deg.
  - sa = g if s >= 1. Otherwise s = max(s, 0), sa = min(atan(s/sqrt(1-s²)), g).
  - h = (1 - cos sa)·r3a.
  - If h > D: **return** min(sqrt(r3a² - (r3a-D)²) + rt, Rs).
  - Otherwise r = sin(sa)·r3a + rt and hD = h.
- Else hD = 0.
- If a > 0: r += tan(a·deg)·(D - hD). If r > Rs then r = Rs and capped (T+0x8b) = 1.
- tan = FUN_004a1f04 [460]@51307 = sin/cos (0 for 0), sec. 4.5.
- **Precision (2026-11-19):** "all f32" means the locals; every formula runs on the x87 stack in extended and is stored f32 at
  its fstps: s (0x4a209d), g = (90.0f @0x4a2238 - a) * TBYTE pi/180 @0x4a223c (0x4a20b3), sa = ArcTan(s / Sqrt(1.0f @0x4a2248 -
  s*s)) (0x4a2115), h (0x4a2149), the radius sums (0x4a2183 / 0x4a21bf / 0x4a2205; tan(a) is the f32 return of FUN_004a1f04
  times the extended D - hD). The port had them in double with libm sin / cos: R (and so W = FUN_00504268) one f32 ulp off for
  some angles / depths, which moved every precompute sample (VekAR6) by up to W * 2^-23 - pool cases p018_D, p072_D, p168_D,
  p246_R, p357_D, p398_R (max 1e-6, finishing / 3D sharpening records). Now ported in extended.
- For a plain V tool: R = rt + tan(a)·D, capped at Rs. Note that Rs = 0 caps the result to 0.

**Checks** (0x529c50..0x529dae). The result written is `error = dx + 0x38e`, even on success (the
compute resets it to 0 at 0x5287b3). The function returns false when dx < 0.

| dx | error | condition |
|---|---|---|
| -1 | 0x38d | row 0 empty |
| 1 | 0x38f | row 1 empty (single tool, ok) |
| -2 | 0x38c | Advance di from 1 while di < 9 && R[di-1] <= R[di] && row[di+1] used. Then require R[di-1] < R[di] (only the last compared pair must be strict). |
| -4 | 0x38a | Some used row di (1..8) has corr[di-1] == 'E' and 2.5·rt[di-1] > rt[di], or the last checked row (di-1) is 'E'. |
| -5 | 0x389 | The last checked row is 'A' (3D only) and AbraeumVerf != 'K'. |
| -3 | 0x38b | KonturenAuf == 'G' and any block-0x11 slot (up to the first idx < 0) is '3' or 'A'. |

Port: `eg_tool_eff_radius`, `eg_tool_rows`.

### 3. Contour preparation (FUN_00528784 @86908, nested in FUN_0052919c)

**Frame layout.** Block 7 is read into the parent at `[ebp-0x6c]` (0x5287a3). Hence:

| frame | block 7 | frame | block 7 |
|---|---|---|---|
| -0x58 | +0x14 Gegenlauf | -0x53 | +0x19 |
| -0x57 | +0x15 | -0x52 | +0x1a |
| -0x56 | +0x16 | -0x51 | +0x1b |
| -0x55 | +0x17 | -0x50 | +0x1c |
| -0x54 | +0x18 | -0x4f | +0x1d |

Other parent locals: -0x6d = last slot, -0x74 = rows, -0x78 = maxR, -0x7a = colour of the max-R tool,
-0x80 = W, -0x84 = &Abraeumwinkel.

1. **Last slot:** start at 9 and decrement while > 0 and row unused (0x5287c6).
2. **Loop over slots 0..last** (0x528801..0x528864):
   - maxR = max(R + *extra).
   - If a row has r3a > 0, message 0x451.
   - If 0.001 >= rt, **error 0x33 (51)**.
3. **W = FUN_00504268(box = PTR_DAT_0052ed80, maxR)** @59418 (0x50427b..0x504313):
   W = max(1.05·maxR, 0.03, 0.0005·(xmax-xmin), 0.0005·(ymax-ymin), maxR + 0.01).
   The constants are TBYTEs (1.05 @0x504318, 0.03 @0x504324, 0.0005 @0x504330, 0.01 @0x50433c); each product / sum is
   extended and stored f32 (fstps -0x8 / -0xc). The port had them as double products (double rounding, now extended).
   **Correction to the brief:** the last term is a lower bound (`jbe` at 0x5042f6), not an upper
   clamp. Port: `eg_width_W`.
4. **Display only:**
   - FUN_00519508 @76119: view box = union(box ± W, global box) -> FUN_00471310.
   - FUN_00517c20 @74703:
     - Deletes the 16 slot files (FUN_00499f80 [460]@43709 = name of slot n).
     - Writes the tool header (FUN_00517b90 @74671 -> FUN_005045cc, §7) into slot files
       0..first-empty-1.
     - Writes slot 10 (roughing) with **the tool of row[last]**.
5. **FUN_00504154 @59328 -> FUN_004fb97c @52682** (VekARH0.TMP -> VekAR1.TMP). The real pushes are
   at 0x5288c9..0x528906:
   - [ebp+8] = 0.01 (not used anywhere in 4fb97c or its nested functions)
   - +0xc = W
   - +0x10 = float(-KParamNormGr)
   - +0x14 = 1e-4 (tolerance)

**Scale.**
- -6 -> FUN_004c3cf0 copy path. NOT DECODED.
- -5 -> scale 1.
- Otherwise FUN_004fb7b8 @52606:
  - Box over tags 0x0a..0x0c, initial box from FUN_004c4434 (±100000); size = max(dx, dy).
  - Round(param): -1 -> size/50, -2 -> /100, -3 -> /200, -4 -> /500, otherwise 1.
  - Then, if W > 1, scale *= W.
  - engrave.dll writes KParamNormGr = size class 1..4 (formats/engrave.md l.237), so scale is
    normally about 1.

**Contour collection** (@52778..52851). Every record's x and y are divided by scale. Only 0x0b and 0x0a
are used, so **0x0c records and header records are dropped**. A contour is a 0x0b plus up to 0xbffd
0x0a records. It is processed at the next 0x0b or at end of file only if it has more than 2 lines
(4 or more points). Shorter contours are dropped.

**FUN_004fb724 @52576** (0x4fb724-0x4fb7b0). A contour whose last index reaches 0xbffd (the main loop of FUN_004fb97c stops
storing 0x0a records there, 0x4fbbda; so 49149 or more 0x0a records) takes the split path below first; every contour then goes
to FUN_004fb624 @52516, which calls FUN_004fab4c @51856 (0x4fab4c..0x4faf37). Here n = index of the last point,
tol = 1e-4 and eps = f32(TBYTE 0.005 @0x4faf3c / scale).

**Split path (decoded 2026-11-19, ported: split_path / split_pass1 / split_pass2 in engrave.c).** Frame of FUN_004fb97c (P =
its ebp; Ghidra's `local_N` is [ebp-(N-4)]): P-0x3f8 = the layer file (VekARH0.TMP), P-0x3fc = start = the record index before
the contour's first 0x0a (set at that 0x0a: local_404 - 1, normally the 0x0b), P-0x400 = end = the index of the 0x0b that ends
it, or the record count at the end of the file; P-0x150 = the read buffer, P-0x158 / P-0x154 = the point buffer / last index
(both GetMem(0x9bfe6) = 0xbffe records); P-0x14c = the file VekAR2.TMP; P+0x10 = scale, P+0x14 = tol.
- FUN_004fb724: t = tol (P+0x14). While count >= 0xbffd: FUN_004faf4c(file, start, end, f32(t·1.0f), P) (EAX / EDX / ECX, pushes
  P then t: [ebp+8] = t, [ebp+0xc] = P), FUN_004fb334(t, P) (same order), count -= 1, and if still >= 0xbffd FUN_004fab08(&t, 2.0f)
  (x 3.333 while t < 0.0045, else x 2). Then FUN_004fb624 with the ORIGINAL tol P+0x14.
- **FUN_004faf4c (pass 1, records in file order -> VekAR2.TMP).** Rewrite VekAR2, Seek(start), remaining = end - start;
  BlockRead 2 into rec(1000), rec(1001), remaining -= 2; nothing more if fewer than 2 were read. Every record read (blocks of
  min(remaining, 1000) at rec(1002..), 0x4fb06c) gets x, y := f32(x / scale) - **tags are not looked at**: every record of the
  range is a point (0x0c too). A = rec(1000), last = 1000, out: rec(1000). Per block, i = 1001 .. 1000 + got: c = |(A.x - p_i.x)
  (A.y - p_i+1.y) - (A.x - p_i+1.x)(A.y - p_i.y)| (extended, 0x4fb0ea-0x4fb133); c > t: write p_i, A = p_i, last = i; otherwise if
  i - last > 2: mid = i - (i - last)/2 and the same test on p_mid keeps p_mid (A = p_mid, last = mid). After the block rec(1001)
  := the block's last record and **last := 1000** (0x4fb277: the index base restarts per block, so the i - last > 2 test counts
  from the block start). Loop while a full block (1000) was read. Finally the last point rec(1001) is written (0x4fb2f6). The
  0xdac / 3000 juggling is only the write batching.
- **FUN_004fb334 (pass 2, VekAR2.TMP -> P-0x158).** count := 0; eps = f32(TBYTE 0.005 @0x4fb618 / scale). If VekAR2 has 10000
  records or more: count := 0xbffe (so FUN_004fb724 retries with a larger t). Otherwise the main pass 2 on the file's records:
  out[0] = v0, A = v0, B = C = v1, dx = f32(B.x - A.x), dy = f32(B.y - A.y); for Q = v2..: if |Q.x - B.x| > eps or |Q.y - B.y| >
  eps: d = f32|Q.y - ((Q.x - A.x)·dy/dx + A.y)| when |dx| > |dy| (fcompp, 0x4fb4db), else f32|Q.x - ((Q.y - A.y)·dx/dy + A.x)|;
  if d > tol (the [ebp+8] = t of this round): count > 0xbffd -> stop (unreachable: fewer than 10000 inputs), else out[count++]
  = C, A = B, B = Q, dx / dy again. C = Q. The last C and the last point are not written. Then Close and Erase VekAR2.
- The point buffer beyond the new count keeps what the main loop stored (FUN_004fab4c's retry may read it, sec. 3.7.1 item 4);
  the port writes into the same buffer.
- Refused (EG_ERR_NOT_DECODED): dx = dy = 0 in pass 2's else branch (0 / 0 with the x87 invalid / zero-divide exceptions unmasked
  by the control word $1332: Delphi raises), a range of fewer than 2 records or start < 0 (Seek(-1)), and a new count below 3
  (FUN_004fab4c with n < 3 is reached only here). None occurs in the oracle cases.
- **Oracle (Wine):** 16 / 16 bit-identical (scratch sets split / split2, made with tools/tests/engrave_wine_cases.py; tool V 15
  deg, tip r 0.1, depth 0.5, 2D, no roughing): RG.wavy(n) with n = 49148 (normal path: n >= 0x8001 keeps 1 record -> FEHLER 100,
  both), 49149 / 49150 (the threshold), 52000, 60000; with Bahnoptimierung (wavy 52000 / 60000 are reduced below the threshold
  by sec. 7.1a, so the normal path); zig-zag circles of 52000 / 55000 points (+-0.002 / 0.01 / 0.03 / 0.1 mm) where pass 1 leaves
  10000 or more records and t grows once, twice and three times (1e-4 -> 3.333e-4 -> 1.111e-3 -> 3.703e-3); the same with
  Bahnoptimierung, with 2D3D, with a star after it (end = the next 0x0b), and two split contours in one file (with and without
  Bahnoptimierung and zig-zag roughing).

- **Pass 1 (collinearity):**
  - w[0] = p0, A = p0, last = 0.
  - For i = 1..n-1: c = |(A-p_i)×(A-p_{i+1})|, written as
    `(A.x-p_i.x)(A.y-p_{i+1}.y) - (A.x-p_{i+1}.x)(A.y-p_i.y)`.
  - If c > tol: keep p_i (A = p_i, last = i).
  - Otherwise, if i-last > 2: mid = i - (i-last)/2. If the same test on p_mid exceeds tol, keep p_mid
    (A = p_mid, last = mid).
  - Finally w[m] = p_n.
- **Pass 2 (axis distance):**
  - A = w0, B = C = w1, (dx, dy) = B - A.
  - For Q = w2..wm: if |Q-B|∞ > eps:
    - d = |Q.y - (A.y + (Q.x-A.x)·dy/dx)| when |dx| > |dy|, else the x analogue.
    - If d > tol: emit C, A = B, B = Q, recompute dx/dy.
    - In both cases C = Q.
  - The output starts with p0. The last C and wm are never emitted.
- **Closing:**
  - If the last emitted point differs from p0 by more than eps (in x or y), append p0 as 0x0a.
  - If it differs but is within eps, replace the last point with p0 (tag 0x0a).
- **Retry:** if the count exceeds 0x7ffe, tol = FUN_004fab08 (×3.333 if tol < 0.0045, otherwise ×1.5
  or ×2) and the whole pass repeats. The repeat reads index = count, one past the list: decoded 2026-11-19 (sec. 3.7.1 item 4),
  ported. Contours of 0x8001 points or more skip pass 1 and are dropped (same item).
- **Scale back.** FUN_004fb624 keeps the contour only if count > 3. It multiplies x and y by scale
  and writes the records.
- Port: `eg_contour_prep`. The "Bahnoptimierung" parameters are not read here; they act before, on the input layer
  (sec. 7.1a).

**Error 100.** If VekAR1 has fewer than 4 records: error 100 and 0x44f (0x528918).

6. **FUN_004f194c @44002 (VekAR1 -> VekAR10, CL = mark = 1, push 1 = draw, push 0 = param_7):
   orientation.** For each contour (0x0b plus its 0x0a/0x0c, with more than 3 points), in
   FUN_004f188c @43954:
   - ymid = (ymin + ymax)/2.
   - FUN_004f1458 @43688 (param_7 = 0) calls FUN_004f12f0 @43612 for the probe:
     - The probe is the leftmost crossing of the contour with y = ymid.
     - orient = (edge.y0 > edge.y1), i.e. 1 = CCW.
     - If ymid hits a vertex, ymid += 2e-5 and the probe is retried.
   - FUN_004f1458 then counts crossings of all records (segments ending in a non-0x0b record) whose
     x satisfies `xc - xprobe < 0.0005`. That includes the contour itself.
   - inside = count & 1, so 1 = outer or island and 0 = hole.
   - If orient == inside, the contour is written as is (FUN_004f169c @43836). Otherwise it is
     reversed (FUN_004f176c @43883): the last point becomes the 0x0b, followed by the others in
     reverse order as 0x0a.
   - **Result:** outer contours and islands are CCW, holes are CW.
   - With mark set, the start record's z = -4.0 (inside = 1) or -5.0 (inside = 0), see FUN_004f169c @43852/43855.
   - Port: `eg_orient`.
7. **Overlap check.** If error < 1 and KDatencheck (`[-0x56]`): FUN_0051203c @70420 (VekAR10, 0, 0).
   A true result gives **error 0x35 (53)**. The function returns `bl` (0x5123a5).

   **Grid (FUN_00511e58 @70335):** k = Round(sqrt(N/80)) (N < 10000) or Round(sqrt(N/100)).
   - k < 2: one cell [-10000, 10000]².
   - Otherwise k×k cells over the box from FUN_004dce90 (tags 0x0a/0x0b/0x0c/0x11/0x12). Rows run from
     the top, columns from the left.

   **Per cell (FUN_005117e8 @70021):**
   - Segments whose outcodes (FUN_00511790) do not share a bit are copied as 9-byte (tag, x, y)
     records. A 0x0b is inserted when the previous point is outside the cell or is a 0x0b, and 0x0c
     becomes 0x0a.
   - A point within 0.001 (in x and y) of the previous one takes the previous point's tag. These tag
     edits persist across cells.

   **Tests:**
   - FUN_00511960 @70134, strict mode (pushed 1 -> `[+0x18]`): segment pairs (i, j >= i+2) intersect
     with both parameters in (1e-5, 0.999999].
   - FUN_00511cb4 @70261: two non-0x0b vertices with squared distance < 1e-6.

   **Result:** true if any marker was found (markers go to block 0x25, at most 500), or on abort
   (more than 19999 markers). An empty file also returns true (0x5120bb). Port: `eg_overlap_check`.
8. **Reverse.** VekAR10 is renamed to VekAR1. If error < 1 and
   **(KGegenlauf ^ 1) != (GravurTyp == 'V')** (0x5289a3..0x5289ba), i.e. Gegenlauf == vertieft:
   - FUN_004f129c @43587 -> FUN_004f10e8 @43473 -> FUN_004f0fe0 @43396 for each contour.
   - All tags become 0x0a. The last record becomes the 0x0b and receives the z of the old 0x0b. Order
     is reversed.
   - More than 100000 points: 0x44d.
   - Port: `eg_reverse_needed`, `eg_reverse`.

### 4. Per-tool computation FUN_00528dac @87144

#### Start point (FUN_00519380 @76010)
If KStartpunkt (`[-0x54]`) > 1, FUN_00519380 runs (VekAR1, flag = KGegenlauf ^ 1, mode = KStartpunkt,
pushed -1 = no drawing), see 0x528dd9..0x528dfb.

- **Mode 2 = interior corner (FUN_005191dc @75904):**
  - For each vertex i = 0..n-2 (the predecessor of 0 is c[n-2]), FUN_00519088 @75837 computes
    cross = (B-A)×(C-A).
  - If (cross < 0) == flag, the metric is 4 (excluded). Otherwise metric = |u1 + u2|², with u1, u2 the
    unit vectors of A->B and B->C; it is 0 if a segment has zero length.
  - The vertex with the smallest metric (strict <, initial 4, default index 0) becomes the start. That
    is the sharpest corner turning against the flag.
  - **Precision (0x519088-0x5191d6; 2026-11-19):** A = EAX = c[i-1] (c[n-2] for i = 0), B = EDX, C = ECX (0x519217 / 0x519237).
    The cross product is one extended expression (0x5190ab-0x5190cd: (A.x B.y - A.y B.x) - (B.y - A.y) C.x + (B.x - A.x) C.y);
    u1, u2, v1, v2 are stored f32 (0x5190f1..0x51910f), |u|, |v| = Sqrt of the extended sum stored f32 (0x519132 / 0x519155),
    and the metric (u1/|u| + v1/|v|)^2 + (u2/|u| + v2/|v|)^2 stays extended up to its fstps (0x5191b7; only the first square is
    spilled as a TBYTE). The port rounded the two sums to f32 and the cross product to double: symmetric corners (the two foot
    corners of an "A", equal metric up to the last bit) chose the other start - pool p264_R (box record and 2 extra records) and
    p084_D (a finishing contour starting 2 mm away). Fixed.
- **Mode 3 (FUN_00519274 @75949):** the longest segment (FUN_004a23e0 distance, strict >) is split at
  its midpoint, and the midpoint becomes the start.
- **Rotation (FUN_00518f7c @75767):** output = c[k] as 0x0b, then c[k+1..n-1], then c[1..k] with
  c[k] re-tagged 0x0a.
- **Dropped and unchanged contours:** contours with 3 records or fewer are dropped, because only
  processed contours are written. Other modes write nothing, so the file is unchanged.
- Port: `eg_startpoint`.

#### FUN_005041a4 @59358 -> FUN_00501550 @57396
Called with (VekAR1, Gegenlauf) and pushes, at 0x528e31..0x528e5b:
- [ebp+8] = **0.62831855 (0x3f20d97c, constant)**
- +0xc = block 7 +4 (KAbrundungswinkel copy)·deg
- +0x10 = W

It builds corner data (VekAR6/VekAR5, VekAR1.ECK, globals 0x5359xx shared with the offset kernel).
**NOT DECODED** (belongs to the kernel family).

#### Slot loop 0..last (@87216..87270)
- **'E'** -> FUN_00528a1c / FUN_00528b10 (corner tool, sec. 4.9); the error they leave is cleared (l.87224-87229).
- **'3'/'A' with KonturenAuf 'S' and angle > 0** -> FUN_00528c9c (3D sharpening).
- **'3'/'A' with 'G' and angle > 0** -> FUN_0051001c + FUN_00518bd4. This branch is unreachable
  after check -3.
- **Otherwise:**
  - FUN_0051001c(VekAR1 -> VekAR8, Gegenlauf, 1, colour, R + *extra, W).
  - Then FUN_00499380(VekAR8, slot file, 0, 0) **appends** to the slot file - also after an error (VekAR8 then holds what the
    join wrote, e.g. < 4 records for error 100).
  - Any error > 0 ends the slot loop (l.87266) and skips the roughing (l.87271). **Error 100 is not cleared here**: with
    KBlockweise < 2 it ends the run (FEHLER=100); the block loop of FUN_0052919c clears 100 (and short error 0x44f) after each
    block (l.87357-87363 / l.87385-87391), so with KBlockweise >= 2 the run goes on with the next block. Measured (Wine): pool
    cases p013_R, p023_R, p041_R, p138_R, p162_R, p215_R, p288_D, p332_D, p347_D (FEHLER 100, KBlockweise 1; now identical, pool
    302 / 310, before 293) and the block-mode ones with FEHLER 0.
- Kernels: other agents.

#### Roughing and ordering K parameters
- **FUN_004de528 @31735:** boundary file name "RAHMEN.TMP" if flag 0x40 of `*PTR_DAT_0052f3b4` is
  set and the file has more than 3 records; otherwise empty.
- **FUN_00528104 @86733** (roughing):
  - Dispatched on AbraeumVerf: Z/Y/C/D/S/A/B/K; any other value sets error 9999.
  - Stepover: Z/Y/S use (2 - overlap(+8)/50)·rt[slot]; C/D use (1 - overlap(+0xc)/100)·rt.
  - KZeilenVerbinden (+0x1a) is passed to FUN_0051170c.
  - **If KAbrWegZusammen (+0x1c) and AbraeumVerf != 'K'** (@86845), FUN_00527dfc merges the files:
    - KAbraeumZuerst == 0 (+0x19): roughing (slot 10) into the finish file of slot param_13, then
      clear slot 10.
    - Otherwise: finish into roughing, then clear the tool slot.
- **FUN_00528688 @86873 (re-sort of the roughing passes, "nachsortieren"):** if Knachsortieren (+0x1b)
  && !KAbrWegZusammen, for C/D/Z: FUN_0051bff0(slot = last, colour, stepover =
  (1 - overlap(+0xc)/100)·rt[last]). Fully decoded in §4b.
- **Zigzag tail:** if AbraeumVerf == 'Z' && KAbrZeilenRund (+0x1d) && !KAbrWegZusammen,
  FUN_00513e38(slot 10 file, VekAR2, 5, (2 - overlap/50)·rt[last]/2.5) (@87399).

#### KBlockweise (+0x17)
Read in FUN_0052919c (0x5291f3..0x5292c9). Values < 2 mean one pass of FUN_00528dac. Otherwise
FUN_00527850 @86364 runs with (VekAR1 -> VekARH01, mode = KBlockweise-2, last arg = 1 for GravurTyp 'V',
-4 otherwise), followed by FUN_00527c5c and FUN_00528dac for each block. When GravurTyp != 'V' the
loop sets GravurTyp per block to 'V' or 'E' from the FUN_00527c5c flag (@87377).

Cell grid over the contour-index box: cw = max(dx/20, 2), ch = max(dy/20, 2).

| mode | KBlockweise | traversal |
|---|---|---|
| 0 | 2 | rows from top to bottom, cells left to right |
| 1 | 3 | **columns from left to right, cells top to bottom** (the "left to right" seen in our runs) |
| 2 | 4 | one cell (whole box) |

For each cell, FUN_005274e8 @86199 repeatedly picks an unused contour as follows:
- Its type (+0x38) differs from the last argument.
- Its box corner (xmin, ymax) is inside the cell's growing window.
- It is nearest to the cell corner (Euclidean distance).
- That contour is written together with every unused contour whose box lies inside it and which
  passes FUN_00527390 (parity point-in-contour), followed by a 0x80 separator record.

The full decode and the port (`eg_blocks`) are in §4a.

### 4a. Block mode (KBlockweise >= 2), fully decoded

#### FUN_00527850 @86364 (VekAR1 -> VekARH01, mode, last_arg)
Pushes at 0x52920e..0x529224: [ebp+8] = mode = KBlockweise - 2, [ebp+0xc] = last_arg = 1 for GravurTyp
'V', else -4.

**Contour index FUN_00510f10 @69558 -> FUN_00510c6c @69417** (threshold argument 0.0). One entry per
group that starts with a 0x0b. Entries are stored by FUN_00510b60 @69342 (chunks of 0x135 x 0x35 bytes)
and loaded by FUN_00510c20. Entry fields:

| offset | field |
|---|---|
| +4 / +8 | first / last record index (the record before the next 0x0b) |
| +0x10 / +0x14 | start xy (the 0x0b) |
| +0x18 / +0x1c | xy of the last record that is not 0x0b/0x80 |
| +0x20..+0x2c | box (xmin, ymin, xmax, ymax) of 0x0a/0x0b/0x0c records |
| +0x30 | ymid |
| +0x34 | threshold: the argument, or 0.7·z of a 0x80 record with z > 0 (0x510e05) |
| +0x38 | type = Round(z of the 0x0b): -5 -> 0xfb, -4 -> 0xfc, else 0. These are the FUN_004f194c markers: -4 outer/island, -5 hole. |
| object +0x3b | box of all contours (FUN_004c4570 union) |

- A group with a single record is not stored.
- Fewer than 4 records sets 0x44f.

**Grid** (0x5279cf..0x527af9): cw = max((xmax-xmin)/20, 2), ch = max((ymax-ymin)/20, 2), on the
union box.

| mode | traversal |
|---|---|
| 0 | y = ymax going down while ymin <= y; inside, x = xmin going right until x > xmax |
| 1 | x = xmin going right while x <= xmax; inside, y = ymax going down until y < ymin |
| 2 | one cell (xmin, ymax, w = dx, h = dy) |

**Cell FUN_005274e8 @86199** (0x5274e8..). Repeat until no seed is found:
- **Seed:** the unused contour with type != last_arg, ymax_c >= y - ch and xmin_c <= x + cw that
  minimises the distance from (x, y) to (xmin_c, ymax_c). Ties go to the first index (strict <,
  start value 1e20).
- **Islands:** every other unused contour j, in index order, whose box lies inside the seed box
  (<= on all four sides) and whose first point is inside the seed polygon.
  - The inside test is FUN_00527390 @86130: parity count of crossings with xc < px over the seed's
    records.
  - The vertex-hit test compares **prev.x** with py (0x5273e0, as in the binary). On a hit,
    py += 0.0002 and the test repeats.
- **Write** (FUN_00527280 @86059, tags 0x0a..0x0c only): first the islands, then the seed, then a
  0x80 record.
- With last_arg = 1 every type can be a seed. With -4, outer and island contours (-4) never become
  seeds and end up in the leftovers.

**FUN_005277f0 @86336** (called when last_arg != 0, i.e. always): appends every unused contour in index
order, without a 0x80.

#### FUN_00527c5c @86502 (per block)
Copies VekARH01 from the saved position into VekAR1 up to (not including) the next 0x80.
- The flag is 0 when a 0x80 was found and 1 at end of file (the leftover chunk).
- The return value is "some 0x0a..0x0c record or 0x80 was seen" (0x527dc3).
- FUN_0052919c @87347..87394 runs FUN_00528dac once per block. For GravurTyp != 'V' it first sets
  GravurTyp := flag ? 'E' : 'V'.
- FUN_0046d0f8 / FUN_0046d16c are progress-bar calls only.

Port: `eg_contour_index`, `eg_blocks` (blocks in processing order, type 'V' = separator block,
'E' = leftover block).

#### Oracle check
Done with the port: oracle chains (inch × 25.4) -> `eg_load_layer` -> `eg_contour_prep` (KParamNormGr
1 for op 4, 2 for op 2) -> `eg_orient` -> `eg_blocks(mode 1)`, driven by `eg_blocks_test.c`.
- **With last_arg = 1 ('V') the block order equals the oracle's finish order exactly:**
  - op 4: [15,14] (x -0.44..-0.26), [21,22] (x -0.18..0.17), [16,17] (x 0.24..0.43).
  - op 2: [2,1], [11,10], [20,18,19], [5..9], [4,3], [13,12].
- With last_arg = -4 everything falls into one leftover block in file order, which does not match.
  The coaster ops therefore run as GravurTyp 'V'.
- Each oracle chain is one contour: no islands, one contour per block.

### 4b. Re-sort of the roughing passes: FUN_00528688 -> FUN_0051bff0 @78190
Called after all blocks, from FUN_0052919c @87397, only when Knachsortieren && !KAbrWegZusammen and
AbraeumVerf is C, D or Z.

1. **Guard:** run only if the slot-10 file has more than 0xe records (@78203).
2. **FUN_0051bef8 @78148:** moves records 12.. of slot 10 into VekAR10.TMP and truncates slot 10 to its
   12 header records.
3. **FUN_0051bbfc @77982 (VekAR10):** contour index (threshold 0.0), then five orderings plus the
   original. Each variant writes VekAR3..7 and measures its length = Σ distance from the end
   (+0x18/+0x1c) of one pass to the start of the next (f32 accumulation).

   | variant | function | ordering |
   |---|---|---|
   | 1 | FUN_0051af9c @77480 / FUN_0051acd8 @77384 | Grid of 20 (min 2 mm) over the union box. Rows from ymax down, cells from xmin right. Inside a cell, repeatedly the unused pass with sx <= x + cw and sy >= y - ch whose start is nearest to (x, y) (strict <, start value 10000). |
   | 2 | FUN_0051b154(0) @77539 | Repeatedly the smallest sy - ref (ref = ymax, then the chosen sy), i.e. ascending start y. |
   | 3 | FUN_0051b154(1) | Ascending start x. |
   | 4 | FUN_0051b48c @77655 | Distance of the start from the fixed point (xmin, ymax). The reference is never updated. |
   | 5 | FUN_0051b768 @77755 | Nearest-neighbour chain from (xmin, ymax). The reference becomes the end of the chosen pass. |
   | 0 | FUN_0051bb20 @77900 | Original order. Length = Σ distance (previous record -> each 0x0b). |

   - **Choice:** best = variant 1. Variants 2, 3, 4, 5 replace it if strictly shorter, and the
     original wins if strictly shorter than the best (0x51bb20 block).
   - The winner is renamed to VekAR10 (FUN_0051ba68 maps the variant to its file name).
4. **FUN_0051170c @69953 -> FUN_00511310 @69772** (VekAR10, append to slot 10, finish = loaded
   records of the last tool's slot file, forward = 1, connect = 1, colour, stepover):
   - Contour index of VekAR10 with threshold 0.7·stepover (0x511365). cur = start of pass 0.
   - Loop:
     - FUN_005110dc @69677 picks the unused pass whose **start** is nearest to cur (forward index
       order, strict <).
     - **Write case:** if it is the first pass of a group, or (dist < threshold || connect) and the
       link cur -> start does not cross the finish path (FUN_00511194 @69705: segment parameters
       both in [-0.001, 1.001], over 0x0a/0x0c segments of the finish records), write it with
       FUN_00510f84 @69585.
       - When linked, the leading 0x0b becomes 0x0a, or is dropped if the next record is 0x0c.
       - cur = end of the pass.
     - **Restart case:** otherwise start a new group: cur = start of the first unused pass in
       index order, and that pass is written next as a new 0x0b.
   - **Consequence:** the order is greedy nearest-start from the end of the previous pass. A link
     that would cross a finish contour instead jumps to the next pass in the chosen variant's order.

Port: `eg_rough_resort` (writes header + re-sorted, linked passes; `variant` reports 0..5).

#### Oracle consistency (not a full replay)
The passes come from the zigzag kernel (mod_rough), which is not run here.
- **op 4:** the stored rough groups are [15,14], [16,17], [21,22] (A, C, B), while the finish/block
  order is A, B, C.
  - From the end of A (-0.267, 0.111), B's start is nearer (0.701 in) than C's (0.712 in). So the
    jump to C is a restart: the A -> B link crosses A's own finish contour.
  - The next pass in sorted order being C fits variants 1 or 4 (top-left first, the A and C rows
    above B). Variants 2 and 3 would give B first and A, B, C respectively.
- **op 2:** the 11 rough groups start at the top-left (-0.5888, 1.6898). This is consistent with
  variants 1, 4 and 5 starting at (xmin, ymax).
- **An exact check needs the hatch passes from mod_rough fed to `eg_rough_resort`.**

#### Display only
After the compute, FUN_0052919c calls FUN_00518574 (§5) and FUN_004e3194 @35452 (redraw only:
FUN_004e2b88 draw calls).

### 5. FUN_00518574 @75209: dangling "spike" removal
For slot files 0..9 with more than 12 records, FUN_005183e8 @75112 reads from record 12 on.

- **Spikes:** a contour of at most 3 records is a spike when record 1 is 0x0c and record 2 is 0x0a
  (FUN_00518344 @75086). A shorter contour leaves record 2 stale; this is kept as in the binary.
- **Spike list:** entries {index, start xy, end xy, active}. A repeated identical spike (both ends
  within 0.001, FUN_00517cdc) deactivates the stored one. Otherwise FUN_00517dc4 adds it (at most
  0xc30).
- **Connectivity (FUN_005182cc / FUN_00518100):** a spike is connected if some contour ends within
  0.001 of its start, or some 0x0b lies within 0.001 of its end.
- **If any active spike is unconnected:**
  - All active spikes become markers (block 0x25, FUN_00518064).
  - FUN_00517e68 sets the tags to 0 at the stored index and then compacts the file.
  - **The stored index counts from record 12, but it is used as an absolute record index.** The port
    keeps this.
- Port: `eg_spike_filter`.

### 6. Depth cuts: FUN_005299e8 @87649 -> FUN_004fa8b8 @51733 -> FUN_004fa5bc @51587
**Setup (FUN_004fa080 @51227):** block 0x2b gives per slot 0..10 {valid, SpantSchichtZ, SpantAufteil,
SpantEintWi, tool}.
- valid = FUN_004f9f48 @51164: record 4 is 0x96 and the file has 13 or more records.
- The tool is rebuilt from header records 0x96..0x9c.
- If 0x9b.y > 0 (already cut), message 0x20e and abort.

The in-memory copy `*PTR_DAT_0052ee28` overrides the per-slot values and SpantSpiegelt (0x529a2b..0x529a62).

**Per slot:** if valid && (layers > 1 || angle < 90 || Spiegelt > 0), call
FUN_004fa8b8(file, layers, aufteil, [ebp+8] = &abort, +0xc = &tool, +0x10 = Spiegelt,
+0x14 = SpantEintWi).

**FUN_004fa8b8:**
- Requires header record 9 == 0x9b; otherwise message 0x3a and the slot is left unchanged. Sets
  0x9b.y = 1.0.
- zmax = max(T.D, z) and zmin = min(-10000, z) over tags 0x0a..0x0c. If zmin <= -10000, zmin = 0.
  Because of the -10000 start (0x4fa96c), zmin is in practice always 0.
- The file is copied to VekAR2, then rewritten with only the 12 header records.

**FUN_004fa5bc** (pushes 0x4faaae..0x4faadd):
- If T.angle < 1, aufteil = 'L'.
- steep = 89.9 > SpantEintWi. n = min(layers, 999). zmin is forced into (-10000, 0]; start value
  cur = min(zmin, 0).
- **n > 0:** step = (zmax - zmin)/n. For k = 1..n, FUN_004fa130 @51279 (0x4fa130..0x4fa23c) computes:
  - 'L': cur = k·step + zmin.
  - 'V': ta = tan(T.angle) and A = (D·ta + rt)·D/n. With c = (rt/2)/ta:
    cur = sqrt((k·A)/ta + c²) - c, i.e. equal removed cross-section area per layer.
  - ratio = cur/zmax.
  - FUN_004fa354 @51412 builds one layer from VekAR2:
    - 0x0a records become 0x0c with z = zmax·ratio. A preceding 0x0b is emitted unchanged, plus a
      0x0c at its xy with that z.
    - 0x0c records get z·ratio.
    - Every emitted 0x0c/0x1b gets z += SpantSpiegelt (FUN_004fa288 @51333).
    - All other tags are dropped.
  - t = dz/tan(SpantEintWi).
  - If steep && t > 0.01: FUN_004f9824 @50755 (ramp-in with half-length t/2, FUN_004f959c).
    **NOT DECODED.**
  - Otherwise FUN_00499380(..., 0, 0) appends the layer.
- **n <= 0:** step 0.0 is pushed (0x4faabc). The binary loops forever unless zmax - zmin <= 0.002.
  The port returns `EG_ERR_HANG`.

**Meaning of z:** depth below the top, positive, from 0 to zmax = max(D, ...). The last layer is at
the full depth D. Without depth cuts, 2D moves keep z = 0 (sample).

Port: `eg_depth_cut`. Ramps return `EG_ERR_NOT_DECODED`.

### 7. Output
**Tool header FUN_005045cc @59569** (slot, T, out, 10.0). The 12 records written by FUN_00517b90 are:

| # | tag | content |
|---|---|---|
| 0 | 0x01 | 0, 0, int 1 |
| 1 | 0x05 | 0, 0, 0 |
| 2 | 0x06 | 1.0, 0, 0 |
| 3 | 0x07 | 1.0, 0, int slot |
| 4 | 0x96 | Rs, rt, angle |
| 5 | 0x97 | r3a, 0, FUN_004dca0c(T+0x54) (RGB colour, observed 0x808080) |
| 6 | 0x98 | Pascal string Copy(name + pad, 1, 11), last byte zeroed |
| 7 | 0x99 | Copy(Copy(name, 11, 11) + pad, 1, 11), last byte zeroed (chars 11..20) |
| 8 | 0x9a | 0, 0, 0 |
| 9 | 0x9b | 10.0, depth-cut flag 0, LEintVorschZ (T+0x46) |
| 10 | 0x9c | LVorschub (T+0x42), D, LFlugHoehe |
| 11 | 0x9d | LSpindelDreh (T+0x4a), T+0x4e, float(u16 LWechselStat) |

- pad = FUN_00497c44(0xb).
- A 13th record (tag 9) is built but not written.
- Port: `eg_tool_header`. The colour and the pad string are inputs.

**Layer 1 (FUN_0050d074 @65986)** with flags = (KAbraeumZuerst ? 0 : 0x10) + 1 (FUN_0050d1a4 @66078):
- **Slot order:** slots 15..0, descending, so the largest tool comes first.
- **Slot 10 (roughing):** if KAbraeumZuerst == 0, it is skipped in the loop and appended **last**.
  Otherwise it stays in its descending position, before 9..0.
- **Per slot file with more than 12 records (FUN_0050cf5c @65926):**
  - FUN_0050cc2c @65804 copies the file and computes the box over tags 0x0a/0x0b/0x0c/0x1c
    (initial ±10000).
  - A trailing tag 9 is dropped.
  - FUN_0050cddc @65877 patches the block header and writes tag 9 at the end:
    - 0x01 = (xmin, ymin, int records incl. the end tag)
    - 0x05 = (xmax, ymax, xmin)
    - 0x07.y = ymin
- Port: `eg_block_order`, `eg_block_append`.

**FUN_00529884 @87594** dispatches on the output extension:
- ".LG1": FUN_00499380(layer-1 file, out, 5, colour) [460]@43045, i.e. a fresh copy of every record
  except tag 0 (@43189).
- ".DXF": FUN_00525118(..., 5 if unit factor == 1.0 else 7, 3).
- ".NCI": FUN_00527150. NOT DECODED (not needed for LG1).
- Port: `eg_final_copy`, `eg_lg1_encode` / `eg_lg1_decode`.

#### Sample `.../engrave/ENGTOOLP.LG1` (2483 B = 191 records), decoded with the reader
- **Record 0:** 0x01 (xmin 21.7263, ymin 12.3704, int 191 = records).
- **Record 1:** 0x05 (xmax 84.5313, ymax 208.7383, z = xmin 21.7263).
- **Record 2:** 0x06 (1, 0, 0).
- **Record 3:** 0x07 (1.0, ymin 12.3704, slot 0).
- **Tool:**
  - 0x96: Rs 6.35, rt 6.35, angle 0 (12.7 mm flat end mill).
  - 0x97: r3a 0, colour 0x808080.
  - 0x98: "AUSSPITZEN"; 0x99: 10 spaces.
  - 0x9b: 10.0, flag 0 (no depth cuts), plunge 0.8.
  - 0x9c: feed 1.0, D 1.27, fly 5.08.
  - 0x9d: spindle 30000, 0, change status 1.
- **Moves:** two closed CCW contours with all z = 0.
  - Records 12..91: 0x0b (46.377, 204.600) + 79 lines, circle r ≈ 4.26 around (42.12, 204.48).
  - Records 92..189: 0x0b (24.202, 13.561) + 97 lines, box 21.73..84.53 × 12.37..52.37, area 2094 mm².
- **Record 190:** 0x09 end.
- **Check:** rebuilding the file from the decoded tool data with `eg_tool_header` + `eg_block_append` +
  `eg_final_copy` reproduces the sample **byte-identically** (`eg_pipe_test.c`). This confirms the
  header layout, the box/count patch and the end record.

### 8. K parameters (block 7, 0x6b bytes)
| param | off | read at | effect |
|---|---|---|---|
| KAbrundungswinkel | +0 / +4 copy | +4 at 0x528e40 | ·deg as argument of FUN_005041a4: corner-arc step / threshold of the precompute (sec. 3.2) |
| KAbraeumueberlapp | +8 / +0xc | FUN_00528104 @86821..86836 (+8), @86789 (+0xc), FUN_00528688, 52919c @87402 | roughing stepover (2 - o/50)·rt (Z/Y/S), (1 - o/100)·rt (C/D, re-sort) |
| KGegenlauf | +0x14 | 0x5289a6 (reverse), 0x528de9 (start flag ^1), offsets 0x528e52/0x528f49..., FUN_00528104 (+0x14) | reverse condition §3.8; offset side; start corner side |
| KParamNormGr | +0x15 | 0x5288d1 | -value -> FUN_004fb97c scale class (§3.5) |
| KDatencheck | +0x16 | 0x528960 | enables the overlap check -> error 53 |
| KBlockweise | +0x17 | 0x5291f3/0x529210/0x5292c9 | < 2: one pass; else block mode KBlockweise-2 (§4) |
| KStartpunkt | +0x18 | 0x528ddc | > 1: FUN_00519380 mode 2/3 (§4). KStartInnen writes 1/2 here (brief) |
| KAbraeumZuerst | +0x19 | FUN_0050d1a4 @66088; FUN_00528104 @86846 | output order of slot 10 (§7); merge direction |
| KZeilenVerbinden | +0x1a | FUN_00528104 @86792/86806/86838 | passed to FUN_0051170c (roughing link; NOT DECODED here) |
| Knachsortieren | +0x1b | FUN_00528688 @86880 | with !KAbrWegZusammen: re-sort of the roughing passes FUN_0051bff0 for C/D/Z (§4b) |
| KAbrWegZusammen | +0x1c | 0x5293e3, FUN_00528104 @86845, FUN_00528688 | merge roughing and finish (FUN_00527dfc); disables zigzag tail and re-sort |
| KAbrZeilenRund | +0x1d | 0x5293d9 | with 'Z': FUN_00513e38 zigzag tail |
| (display) | +0x35, +0x36.. | FUN_004e269c/FUN_004e2758 | draw order / visibility only |

"Bahnoptimierung" (block 0x17: BOVektorAufl, BOMindStuetzp, BOMindestausd, BOBahnenSchl) is not read by the compute path;
it is the input path optimisation FUN_00529700 -> FUN_0050fa18 run right after FUN_004dd3e4 (sec. 7.1a). The reduction in
FUN_004fab4c uses only 1e-4 and 0.005/scale.

### NOT DECODED / open
- Input tags 0x0d, 0x10, 0x11/0x12, 0x15, 0x19/0x1a, 0x80 in FUN_004dd3e4. The values of the globals
  0x52eee8/0x52ede8 that gate the error-1 check.
- Uninitialised `[529e38 ebp-0x138]` (extra offset) and `[ebp-0x134]`: the binary reads stack garbage.
  Needs a measurement.
- Box PTR_DAT_0052ed80 used for W: believed to be the drawn extent (the union in FUN_00529514); the
  writer is not identified.
- FUN_004c3cf0 (KParamNormGr == 6). The split path for contours with 0xbffd or more points (FUN_004faf4c / FUN_004fb334) is
  decoded since 2026-11-19 (sec. 7.3). The FUN_004fab4c retry above 0x7ffe and contours over 32767 points are decoded since 2026-11-19 (sec. 3.7.1).
- FUN_005041a4 / FUN_00501550 corner data: decoded (sec. 3).
- Corner, 3D and roughing kernels (other agents): FUN_0051001c, FUN_00518bd4, FUN_00528c9c,
  FUN_00528a1c/FUN_00528b10 (decoded, sec. 4.9), FUN_0051064c, FUN_005108d4, FUN_0051a298 / FUN_0051abac (decoded, sec. 6.7), FUN_0051d07c,
  FUN_00527dfc, FUN_00527f94, FUN_00513e38 (FUN_0051170c/FUN_00511310 is decoded in §4b for the re-sort
  call; the roughing call with KZeilenVerbinden belongs to mod_rough).
- Block-mode and re-sort: fully decoded (§4a, §4b). The progress calls FUN_0046d0f8 / FUN_0046d16c in the
  block loop and FUN_0046cb20 / FUN_0046cb34 are progress/display only (no data).
- The ramp FUN_004f9824 / FUN_004f959c (SpantEintWi < 89.9 with t > 0.01).
- FUN_004dca0c (colour index -> RGB) and the pad string FUN_00497c44(0xb).
- DXF/NCI writers FUN_00525118 / FUN_00527150.
- Box init FUN_00499018 used by the overlap grid box (the port assumes an empty box).
- Exact x87 precision: the port rounds through float where the binary stores f32, but keeps
  intermediates in double rather than 80-bit.


## 8. engrave.dll: the records -> moves (FUN_180041910)

The reader FUN_18005ccc0 (lg1.c l.183-405) divides x, y, z by f (25.4 for inch parts). FUN_180041910 (contour_build.c
l.352-728) walks every tool's records (a tool named "BIGTOOL" is skipped when remachining) and writes moves through
Write3dMove (vtable +0x10 -> FUN_18001de90 -> FUN_18001e3c0 -> FUN_18001df60, type 0 = rapid, the NCI feed -2):

- heights (FUN_18004abf0, core6.c l.169-245): depth = top - depth z (absolute) or -depth value (incremental), fly = feed z - top
  (absolute) or the feed value; top (P+0x190), feed plane / clearance / retract z (P+0x198 / +0x1a0 / +0x1a8) and the rapid
  height (P+0x1b0 on, P+0x1b8 z) from FUN_18004aea0 (core6.c l.307-384).
- z per record: tag 0x0c `top - z`; 0x0a `top - z`, or `top - depth` when |z| <= 1e-6 (2D records carry z = 0 = full depth);
  0x0b `top + fly - z`. Any other tag -> error 12.
- before the first record: a rapid to the clearance z when clearance is on (P+0x148).
- 0x0a right after 0x0b: first a feed move to (the 0x0b x, y, top - depth) (the plunge).
- 0x0b after a lower point: with P+0x180 = 0 (op +0x141 = record +0x1a9) a move up to the 0x0b z at the previous x, y with the
  retract feed (T+0x88), then, with the rapid height on, a rapid up to it; with P+0x180 = 1 only the rapid up to the rapid
  height. Then the 0x0b record itself as a rapid.
- every other record: a feed move; the feed by FUN_180046920 (contour_build.c l.739-757): same x, y and z rising more than
  1e-12 -> retract feed, same x, y otherwise -> plunge feed, else the cutting feed.
- after the last record: to the feed plane z (P+0x198; rapid with P+0x180, else retract feed), then a rapid to the retract z
  and / or the clearance z (clearance wins) when on.
- MEASURED (coaster NCI): Mastercam stores a rapid that moves in x, y and down as an x, y rapid at the start height followed
  by the rapid down, and a move to the point it is already at is not stored (mastercam.dll FUN_180204320 / FUN_1801ffe70, not
  decoded); the port does the same (engrave.c split_rapids). The engine does emit such repeats: eg_sharp_spiegel writes the
  restart point of a sharpening run again (sec. 5).
- the op's line / arc filter (record +0x33a) then runs on the op's NCI (formats/engrave.md 1.1 f; viewer/cam_filter.c).

Contours (FUN_18001e400, contour_build.c l.94-350): per chain `chain_to_ctour_array(chain, ..., active view, tol =
S+0x28 / 10)` (engrave.md 1.1 said x10: it is / 10) and per ctour record: 'L' -> a point record (x, y, 0), 'A' -> an arc record
(radius, start angle, sweep, centre; tag 0x0d). The spline breaking inside chain_to_ctour_array is mccore's FUN_180282bd0 /
brk_spline_const_err, decoded 2026-10-26 (formats/spline_chain.md): splines become 'L' records at that tolerance; the app
(cam_chain_polyline) does the same, the standalone oracle still samples the chains to 0.0001 (a SUBSTITUTE; `APP=1
engrave_test.sh` runs the app path, spline_chain.md 6).

## 9. The port, integration rules and the oracle

`viewer/engrave.c` = engrave.dll's driver (engrave_setup: op record -> EST_DAT values, sec. 8 / engrave.md 3; eg_lg1_to_moves)
+ the five decoded engine modules (sec. 3-7, one section each) + the engine driver `eg_engine` (FUN_00529e94 / FUN_0052919c /
FUN_00528784 / FUN_00528dac / FUN_00528104 as decoded in sec. 7; the corner tool sec. 4.9) + engrave_generate (contours -> engine -> moves). The
driver's wiring, with the sources it follows:

| step | rule | source |
|---|---|---|
| input | ENGCONT records (0x0b first point, 0x0a others, x25.4) -> eg_load_layer(tol 0.001) | FUN_004dd3e4, push at 0x529eea |
| global box | Grenze box (0..2540 for inch) united with the layer box | FUN_00529514 |
| tools | one engine tool from the op (engrave.md 3.1 / 3.3); rows by eg_tool_rows | FUN_00529b50 |
| extra offset | `*extra` (an uninitialised local of FUN_00529e38) = 0 | MEASURED: op 4 finish lies at the tip radius, op 2 at r_tip + D tan a (engrave.md 7) |
| Abraeumwinkel | degrees -> radians | FUN_00529b50 0x529dc7 (resolves sec. 6 "NOT DECODED" on the conversion) |
| W, preparation, orientation, overlap (53), reverse | eg_width_W, eg_contour_prep, eg_orient, eg_overlap_check, eg_reverse | FUN_00528784 |
| blocks | KBlockweise >= 2: eg_blocks(mode = KBlockweise - 2, last arg 1 for GravurTyp V else -4), FUN_00528dac per block | FUN_0052919c 0x5291f3.. |
| per block | start point (KStartpunkt > 1), precompute (W, KAbrundungswinkel rad, 0.62831855), per slot: '3'/'A' + Spiegel -> offset core mode 0 at L then eg_sharp_spiegel; 'E' -> eng_corner (sec. 4.9); '2' -> eg_offset_retry + append (error 100 ends the slot loop, cleared per block only in block mode); then roughing: 'Z' / 'Y' boundary = eg_offset_retry(maxR), eg_rough_528104; 'C' / 'D' eng_rough_cd (sec. 6.7) | FUN_00528dac, FUN_00528c9c, FUN_00517ac8, FUN_00528104 |
| after the blocks | eg_rough_resort (Knachsortieren && !KAbrWegZusammen, C/D/Z), spike filter slots 0..9, depth cuts (SpantZustellung), block order / headers / final copy | FUN_00528688, FUN_00518574, FUN_005299e8, FUN_0050d1a4 / FUN_0050d074 / FUN_00499380 |
| read back | per tool block (0x01 .. 0x09) the 0x0a..0x0d records -> eg_lg1_to_moves (sec. 8) | FUN_18005ddf0 / FUN_18005ccc0 / FUN_180041910 |

Refused (an error text, no toolpath): remachining, wrapping, XY stock to leave (its overlap / radius adjustment is decoded in
engrave.md 3.2 but not wired), tool types 13 / 17, the depth warning dialog 1808, KorrekturVerf for "on top" with a 0 deg tool,
the corner tool's undefined / unreached cases (sec. 4.9: B read beyond its records, > 45000 corner contours, a hatch ending in
0x0b), roughing S / A / B (unreachable, sec. 6) and the C / D undefined case (sec. 6.7), depth-cut ramps (SpantEintWi < 90 with a ramp,
FUN_004f9824), the precompute's undefined cases (sec. 3.7: the i16 count wrap, a first segment needing the never-set start id,
grid cells outside the table), the split path's exception / unreachable cases (sec. 7.3), and every NOT DECODED item of sec. 3-7.

**Oracle** (`tools/tests/engrave_test.sh`; engraved coaster ops 2 and 4, inch; the chains sampled to 0.0001 as the SUBSTITUTE
for Mastercam's chain_to_ctour_array; distances are 3D, between the port's moves and the stored NCI with its arcs sampled):

| | op 2 (Spiegel, 2D3D, zigzag) | op 4 (Grund, 2D, zigzag) |
|---|---|---|
| rapids / passes | 51 / 17 = stored 51 / 17 | 18 / 6 = stored 18 / 6 |
| pass order, start points | all 17 in the stored order; starts within 0.0003 except one rough pass (0.0044 in x) | all 6 in order, within 0.0001 |
| z levels | rapids 0.1 / 0.25, feed z -0.135..0 = stored | rapids 0.1 / 0.25, feed z -0.135 = stored |
| feeds | plunge 25 x17, cut 5 (stored 25 x17, 5) | 25 x6, 5 |
| stored points -> port path | p50 0.00002, p95 0.00011, max 0.0032 (on a 3D sharpening descent) | p50 0.00001, p95 0.00007, max 0.00024 |
| port points -> stored path | p50 0.00008, p95 0.00020, max 0.0021 (3D) | p50 0.00004, p95 0.00016, max 0.00026 |
| after Mastercam's arc filter (cam_filter.c, the op's 0.0002) | G1 2270, G2 18, G3 20 (stored 2294 / 14 / 8) | G1 178, G2 5, G3 5 (stored 186 / 5 / 4) |

The remaining differences come from the input: Mastercam breaks the NURBS chains into lines / arcs with its own
chain_to_ctour_array (tol S+0x28 / 10), the oracle samples them uniformly; the vertices therefore differ, the 2D paths agree
to the filter tolerance, the 3D sharpening descents (whose position depends on where the corner vertex lies) to 0.003.

## 10. The vertex-density dependence (coaster op 2, app path 16 / 17 passes) - root cause (2026-11-19)

Reproduced standalone: the app's own chain polylines for op 2 (cam_cli's cam_chain_polyline = the decoded chain_to_ctour_array
breaking, dumped from engrave_generate) fed to `tools/tests/engrave_test` give exactly the app result (16 passes / 48 rapids, max
0.0767 in), the SUBSTITUTE sampling (0.0001) gives 17 / 51. Two separate effects, neither a decode error:

1. **The missing short rough pass (stored pass 10, 4 rows from (-0.1658, -0.8284) in) is an exact tie in FUN_004f3334.** After
   the full-width row at y = -0.8585 in, the link looks for the nearest unused row start within +-1.9 s (strict <, first minimum
   in index order, sec. 6.3.2; d^2 in x87 then f32, 4f35cf-4f35e8). The two candidates are the left-part row above
   (-0.6804, -0.8510) and the row below (-0.6878, -0.8660): both lie on the same straight boundary edge (the offset of one long
   chain segment), mirror-symmetric about the row end (-0.6841, -0.8585). With the app polyline the port gets d^2 =
   0.0451525189 mm^2 for both (bit-equal f32), so the first one (the row above) wins and the left rows are linked into the
   right part's path instead of becoming a pass of their own. With the dense SUBSTITUTE the boundary there is a polyline of
   short pieces, the tie is broken geometrically (0.0451514423 < 0.0451517999) and the row below wins - as in the stored NCI.
   estamp7 decided the same tie by the last bits of its boundary geometry (the f32 crossings of FUN_004f27b0 on that edge and
   the f32 row y values), which depend bit-for-bit on Mastercam's ENGCONT.LG1 and every x87 step before. The port now does the
   crossing and the link distance in x87 extended as the binary (4f2878-4f28cf, 4f35cf-4f35e8), which did not change this tie:
   **the input polyline is not bit-identical to Mastercam's** (and the chain upstream - breaking, ENGCONT f32 write, precompute,
   offset - would all have to be). Not fixable from the decode alone; the proof is an engine run on Mastercam's own
   ENGCONT.LG1 (windows_checklist.md 11).
2. **The 0.077 in sharpening spike (port (0.8725, 0.9369, -0.0356) in) comes from a 0.0006 in (0.0156 mm) segment** in the app's
   breaking of NURBS entity 3/4 of chain 2, at (0.88617, 0.93180)-(0.88593, 0.93125) in - inside the spline, not at an entity
   junction. Its neighbours turn by only a few degrees, so its two line-line bisectors are nearly parallel and meet 0.3 mm inside
   (envelope sample d = 0.285 W at (22.1625, 23.7963) mm, segments 0..2 of that contour in VekAR6), and FUN_00517594 climbs that
   ridge to z = -0.0356 in. The point reduction FUN_004fab4c keeps that short segment's first point only because its predecessor
   is far away: pass 1 tests the triangle area |(A - p_i) x (A - p_i+1)| < 1e-4 mm^2 against the last KEPT point A, so with the
   long segment before it subdivided (the experiment of spline_chain.md 6) A is close, the point is dropped as collinear and the
   spike disappears. **That density dependence is estamp7's own (FUN_004fab4c, decoded)**; with the same polyline estamp7 would
   make the same spike. The stored NCI has none, so Mastercam's polyline there differs from the app's (no such short piece) -
   a question for the breaking (spline_chain.md, mastercam.dll chain_to_ctour_array), not for the engine port.

Both effects need Mastercam's exact engine input to settle: windows_checklist.md 11 asks to copy `ENGCONT.LG1`, `EST_DAT.TXT`
and `ENGTOOLP.LG1` from the engrave data folder after each regen. Running the port on that ENGCONT.LG1 (eg_engine) and comparing
with that ENGTOOLP.LG1 record by record is the bit-level oracle the corpus cannot give (needs an EST_DAT.TXT reader in the test,
not written yet).

**Resolved 2026-11-19 (Wine oracle, user decision EG-2/EG-4).** Both effects were the missing input path optimisation
Bahnoptimierung (FUN_00529700 -> FUN_0050fa18, sec. 7.1a; engrave.dll writes `Bahnoptimierung=JA` with "smooth contours") plus
x87-precision and global-box details, not the spline breaking. With the engine bit-identical to estamp7.exe under Wine, the app
path (`APP=1 tools/tests/engrave_test.sh`, cam_cli at 5f3c39d) now reproduces the stored coaster NCI: op 2 17 / 17 passes, 51 / 51
rapids, stored -> app max 0.00013 in, app -> stored max 0.00008 in, after the op's arc filter G1 2289 / G2 14 / G3 8 (stored
2294 / 14 / 8); op 4 6 / 6 passes, both directions max 0.00000 in, G1 186 / G2 5 / G3 4 = stored. The standalone oracle (chains
sampled at 0.0001 in, a SUBSTITUTE input) is now the worse one (op 2 p95 0.00080 / 0.00058, op 4 p95 0.00040 / 0.00022), because the
path optimisation depends on the input's vertex density. The decoded chain_to_ctour_array breaking is confirmed by this result.

Note (engrave-dll branch, 2026-11-19): engrave_setup now builds the remachining tool lists of FUN_1800525b0 (2 / 3 tools, the corner
tool 'E' on top, "BIGTOOL"; formats/engrave.md 3.4) (gate removed 2026-11-20 after 36 / 36 Wine cases; the corner-tool row check 0x529d03 compares the unrounded x87 product, fixed in eg_tool_rows). The engine's multi-tool run is to be verified with
the Wine oracle (EG-2); the corner tool code (FUN_00528a1c / FUN_00528b10) is planned as a new file, not in engrave.c's engine sections.

