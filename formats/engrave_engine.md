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
2. FUN_004dd3e4: load `Quelle` (ENGCONT.LG1) into the working layer.
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
| 0x17 (0x1c) | +0 / +4 / +8 / +0xc | BOVektorAufl (10000) / BOMindStuetzp (4) / BOMindestausd (0.05) / BOBahnenSchl |
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
     - FUN_004fcb6c reduces points through **FUN_004fab4c (NOT DECODED)**.
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
- **Ported in full:** FUN_004fd510, 4fd3a4, 4fd1d0, 4fd07c, 4fcec0, 4fcda4, 4fcca0 (minus the overflow branch),
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

### 7. NOT DECODED / deviations
1. **FUN_004fab4c** @51856: point reduction, run only when a contour exceeds 0x7ffd points after the arcs are
   inserted. The surrounding retry loop is decoded: eps 0.0005 → ×3.333 below 0.0045, else ×1.5; arc step +5°
   up to 40°. The port returns `EG_PRE_ERR_NOTDECODED`.
2. **Stale values the original reads:**
   - the own-O record's id bytes (+2..+5, never written); the port uses −1;
   - S_ids/E_ids when FUN_00500770 finds nothing; they stay from the previous segment, and the port initialises
     them to 0 once;
   - L[|L|] when the neighbour edge is missing; the port returns `EG_PRE_ERR_STALE`;
   - the sentinel's x/y/z; the port writes 0;
   - a 0x0a record before any 0x0b; the port skips it.
3. **Grid capacity:** in-memory buckets 0x1f4000/(nx·ny·4), overflow file VekAR3.TMP, error 9. Not modelled.
   Cells outside 0..127 would write outside the original's table; the port returns `EG_PRE_ERR_STALE`.
4. Progress display (FUN_004bc3b0/3b8/3c0/3d0/3d8/3ec) and file block sizes are left out.
5. Transcendentals (atan, cos, sin, sqrt) use libm long double instead of x87 fpatan/fcos/fsin; last-bit
   differences are possible.

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
FUN_00501550 (NOT DECODED here).** The core only intersects and trims straight offset lines.

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
no FUN_0049acb8). If the precompute puts those samples on the corner bisector, the result is a sharp (miter)
corner. That premise is **NOT DECODED** (it is in FUN_00501550).

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
**Tan**: returns (float)(sin x / cos x), and returns 0 when x == 0.0. It is not used by this module.

### 6. Port status (eg_offset.c)
- FUN_00501ec4: ported fully, in all three modes (0, 1, p7 > 0). This includes the stale-value behaviour: P after a
  parallel intersection, z = local_933, B[n704] reads past the end (zero-filled buffers), and E[0] when a
  segment has 0 samples.
- Ported in full: FUN_00503ed4, 00503784, 00503968, 00503e5c, 00503a48, 0050443c, 0051001c, and the helpers
  4fbdec, 4fbe68, 4fbf40, 4fbfdc, 5017c0, 501d80, 501a54, 501c18.
- Left out: the progress display (FUN_004bc3d0/3d8/3ec), file I/O block sizes, and the global name
  DAT_0052eb14.
- Precision: float32 where the original stores float (r, r2, d, t, the factor f, output x, y, z, and all join
  geometry). Double elsewhere; x87 extended is replaced by double, and the TBYTE constants are compared as
  long double.

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

The coaster oracle (op4) **cannot be run**: it needs the real VekAR7/6/5, i.e. the precompute.

### 8. NOT DECODED
1. **FUN_00501550 precompute** (FUN_004fd510 contour cleanup → VekAR7; FUN_004fde5c, FUN_005013cc,
   FUN_00500cac, FUN_004ffc3c, FUN_005001d8 … → VekAR6/5). This is where corner treatment, self-intersection
   removal and the envelope shape are decided. It is required before the port can produce real offsets.
2. Whether the Abrundungswinkel (block 7 +0) or the 0.62831855 rad (36°) value seen as `in_stack` at the
   FUN_005041a4 call (dump 87210) is used by the precompute.
3. Stale values the original reads from uninitialised GetMem memory: E[0] for the first segment if it has
   0 samples, and B beyond n704. The port reads zeros.
4. The second output of variable mode is ported literally, but what it means geometrically (the "Eck" corner tool
   path) has not been confirmed against any measurement.

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

### 5. NOT DECODED / open
1. The geometry of the VekAR2 corner/ridge records: which points FUN_00501ec4 (mode 0) emits and their d values.
   That is the offset module's job. The port takes VekAR2 as input, and the tests only assume V has z = 0.
2. Grund: a 0x0a/0x0c record while count == 0 (after a non-0x0a/0x0b/0x0c tag, or at the start of the file).
   The original then works on "slot -1" = [F-0x87], which overwrites its own count byte and the tool angle.
   The port returns EG_SHARP_ERR_NOT_DECODED.
3. KAbrundungswinkel (block 7 +0) is **not used by either function**. Spiegel has no threshold, and Grund uses
   the constant 36 deg. Whether the offset engine uses it is for the offset module to say.
4. side_flag meaning (frame-0x58 of FUN_00528dac): the same flag is passed to the offset engines.
   Its link to Gegenlauf/GravurTyp has not been traced here.
5. Not ported (I/O and display only): FUN_00468270/4bc3d0 progress, FUN_0046e05c/0046e1d4/0046e3e4 drawing, and
   file buffering/flush errors (0x32).

## 6. The roughing ("Abraeumen") Z / Y and its merge with the finishing

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

### NOT DECODED / not ported
- Value of the uninitialised `e` stack float in FUN_0051064c (@510797); port uses max(e_stack, s/4), pass 0.
- Pass order of the rough passes in the NCI (region 1, 2, 3) differs from the finishing order (1, 3, 2): re-sort
  FUN_00528688 -> FUN_0051bff0 (Knachsortieren && !KAbrWegZusammen; FUN_0051bef8, FUN_0051bbfc tries VekAR3..6 orderings,
  then FUN_0051170c(connect 1, KZV 1, spacing (1-ov/100) rt)). Not decoded, not ported.
- Degree -> radian conversion of Abraeumwinkel before FUN_0052919c (angle must be radians in FUN_004f39e0).
- FUN_004bc3c0 user-abort (error 3) and display calls (FUN_004bc3d8/3ec) - not ported (no geometry).
- 'B' (FUN_00527f94): only the set-up is decoded; FUN_0051a6c8 not decoded.
- FUN_00513b7c with nseg >= 1001 (local count uninitialised) -> EG_ROUGH_NOT_DECODED.
- The boundary offset FUN_0051001c itself (other module).

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
- tan = FUN_004a1f04 [460]@51307 = sin/cos (0 for 0).
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

**FUN_004fb724 @52576.** A contour with more than 0xbffc points takes the split path
(FUN_004faf4c/FUN_004fb334/FUN_004fab08). **NOT DECODED.** Normal contours go to FUN_004fb624 @52516,
which calls FUN_004fab4c @51856 (0x4fab4c..0x4faf37). Here n = index of the last point,
tol = 1e-4 and eps = 0.005/scale.

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
  or ×2) and the whole pass repeats. The repeat reads index = count, which is one past the list.
  NOT DECODED, and the port returns an error.
- **Scale back.** FUN_004fb624 keeps the contour only if count > 3. It multiplies x and y by scale
  and writes the records.
- Port: `eg_contour_prep`. There is no further "Bahnoptimierung": BOMindStuetzp / BOMindestausd / the
  0.01 are not read here.

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
- **'E'** -> FUN_00528a1c / FUN_00528b10 (corner tool).
- **'3'/'A' with KonturenAuf 'S' and angle > 0** -> FUN_00528c9c (3D sharpening).
- **'3'/'A' with 'G' and angle > 0** -> FUN_0051001c + FUN_00518bd4. This branch is unreachable
  after check -3.
- **Otherwise:**
  - FUN_0051001c(VekAR1 -> VekAR8, Gegenlauf, 1, colour, R + *extra, W).
  - Then FUN_00499380(VekAR8, slot file, 0, 0) **appends** to the slot file.
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
| KAbrundungswinkel | +0 / +4 copy | +4 at 0x528e40 | ·deg as argument of FUN_005041a4 (corner data; NOT DECODED) |
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

"Bahnoptimierung" (block 0x17: BOVektorAufl, BOMindStuetzp, BOMindestausd, BOBahnenSchl) is **not read by
the compute path decoded here**. The reduction in FUN_004fab4c uses only 1e-4 and 0.005/scale. Where
block 0x17 is used was NOT DECODED (readers: FUN_00523bf0 parser, FUN_00529700).

### NOT DECODED / open
- Input tags 0x0d, 0x10, 0x11/0x12, 0x15, 0x19/0x1a, 0x80 in FUN_004dd3e4. The values of the globals
  0x52eee8/0x52ede8 that gate the error-1 check.
- Uninitialised `[529e38 ebp-0x138]` (extra offset) and `[ebp-0x134]`: the binary reads stack garbage.
  Needs a measurement.
- Box PTR_DAT_0052ed80 used for W: believed to be the drawn extent (the union in FUN_00529514); the
  writer is not identified.
- FUN_004c3cf0 (KParamNormGr == 6), the split path for contours with more than 0xbffc points, the
  FUN_004fab4c retry when the count exceeds 0x7ffe, and contours over 32767 points (16-bit counters).
- FUN_005041a4 / FUN_00501550 corner data.
- Corner, 3D and roughing kernels (other agents): FUN_0051001c, FUN_00518bd4, FUN_00528c9c,
  FUN_00528a1c/FUN_00528b10, FUN_0051064c, FUN_005108d4, FUN_0051a298, FUN_0051abac, FUN_0051d07c,
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
FUN_00528784 / FUN_00528dac / FUN_00528104 as decoded in sec. 7) + engrave_generate (contours -> engine -> moves). The
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
| per block | start point (KStartpunkt > 1), precompute (W, KAbrundungswinkel rad, 0.62831855), per slot: '3'/'A' + Spiegel -> offset core mode 0 at L then eg_sharp_spiegel; '2' -> eg_offset_retry + append; then roughing: boundary = eg_offset_retry(maxR), eg_rough_528104 | FUN_00528dac, FUN_00528c9c, FUN_00517ac8, FUN_00528104 |
| after the blocks | eg_rough_resort (Knachsortieren && !KAbrWegZusammen, C/D/Z), spike filter slots 0..9, depth cuts (SpantZustellung), block order / headers / final copy | FUN_00528688, FUN_00518574, FUN_005299e8, FUN_0050d1a4 / FUN_0050d074 / FUN_00499380 |
| read back | per tool block (0x01 .. 0x09) the 0x0a..0x0d records -> eg_lg1_to_moves (sec. 8) | FUN_18005ddf0 / FUN_18005ccc0 / FUN_180041910 |

Refused (an error text, no toolpath): remachining, wrapping, XY stock to leave (its overlap / radius adjustment is decoded in
engrave.md 3.2 but not wired), tool types 13 / 17, the depth warning dialog 1808, KorrekturVerf for "on top" with a 0 deg tool,
the corner tool 'E', roughing C / D / S / A / B, KAbrZeilenRund, depth-cut ramps (SpantEintWi < 90 with a ramp, FUN_004f9824),
the precompute's point-reduction path for contours over 32765 points and its stale-memory cases, and every NOT DECODED item of
sec. 3-7.

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
