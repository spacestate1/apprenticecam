# Mastercam 2017 mill tool side profiles (parametric outlines)

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

Decoded from the native code of `tlcore.dll` (`TlProfileBuilderParametric`), plus the consumers in `mccore.dll` and
`backplotbnci.dll`. Every rule below cites its function. Anything without a source is marked **NOT DECODED**.

## 0. Sources and conventions

| Binary | Ghidra project | Notes |
|---|---|---|
| `tlcore.dll` (image base 0x180000000) | `~/mastercam-re/ghidra_tl` / `tl` (new; full decompile of 18718 functions in `decomp/native_agent/tl_all.c`) | Incremental-link build: each export is a 5-byte `jmp` thunk. Both addresses are given as `thunk → body`. |
| `mccore.dll` | `~/mastercam-re/ghidra_core` / `mccore` | Owns the `TlToolMill` classes and the `MillTool` display builder. |
| `backplotbnci.dll` (0x67d00000) | `~/mastercam-re/ghidra_bp` / `bp` | Backplot model filter. |

- Other dumps kept in `decomp/native_agent/`: `bpb_16540.c`, `bpb_16db0.c`, `bpb_18a90.c`, `bpb_1a2e0.c`
  (backplotbnci functions `FUN_67d16540` / `16db0` / `18a90` / `1a2e0`, §5), `core_prof.c` (mccore tool-profile
  functions: BuildParametricMillToolProfile, CalcAutoToolProfileParams, GetProfileParams, ..., §4.2 / §4.3),
  `core_wr81.c` (mccore `bnci_wr_80` / `81` / `100*` NCI writers).
- Line numbers are not stable across dumps, so functions are cited by address.
- x64 calling convention trap: `GetLineSegment` and `GetArcSegment` return a struct through a hidden RCX pointer.
  Ghidra therefore shows a garbage XMM0 value as the first "double" argument. All argument lists below are the
  *real* ones (the first displayed double is dropped).

### 0.1 Entry point
`TlCore.dll!RetrieveParametricToolProfile` is export ordinal 1574 at RVA 0x1514, a thunk to body `FUN_18014abd0`.
- It copies its 29 arguments into a `MillToolProfileParams` (200 bytes) inside a stack `TlProfileBuilderParametric`.
- It calls `TlProfileBuilderParametric::CalcProfile` (0x1800086c0 → 0x18013f7e0).
- It writes the `TlProfileResultCode` to `*arg2`.
- It moves the resulting `vector<TlProfileEnt>` into `*arg1`.

**`MillToolProfileParams` layout.** Names are from `toolnetapi/-Module-.cs` `MillToolProfileParamsMarshal`
(L69117-69146). The argument numbers are those of `RetrieveParametricToolProfile`, as listed in `formats/toollib_net.md`
§4.2 and confirmed by the stores in `FUN_18014abd0`.

| off | type | field | arg # | used as |
|---|---|---|---|---|
| 0x00 | u8 | entColor | 4 (=9) | copied to each entity +1 (`initEntity`) |
| 0x01 | bool | isMetric | 5 | not read by any make* function |
| 0x08 | f64 | nc_tolerance | 6 (1e-4) | tolerance for `ints_gt` |
| 0x10 | f64 | tolerance | 7 (5e-5) | `tol` in every comparison below |
| 0x18 | i32 | toolType (MCToolType) | 3 | dispatch |
| 0x1c | i32 | cornerType (TlCornerType) | 8 | Face mill only |
| 0x20 | f64 | toolLength = **OAL** | 9 | |
| 0x28 | f64 | fluteLength = **FL** (CuttingDepth) | 16 | |
| 0x30 | f64 | primaryDiameter = **D** (R = D/2) | 10 | |
| 0x38 | f64 | secondaryDiameter = **Dt** (tip diameter) | 17 | |
| 0x40 | f64 | cornerRadius1 = **r1** | 11 | |
| 0x48 | f64 | cornerRadius2 = **r2** | 23 | |
| 0x50 | f64 | angle1 = **A** | 12 | |
| 0x58 | f64 | angle2 = **C** | 25 | |
| 0x60 | f64 | angle3 = **B** | 15 | |
| 0x68 | f64 | chamferLength1 = **c1** | 20 | |
| 0x70 | f64 | chamferLength2 = **c2** | 24 | |
| 0x78 | f64 | pilotLength = **Lp** | 21 | |
| 0x80 | f64 | pilotDiameter = **Dp** | 22 | |
| 0x88 | i32 | threadShape | 27 | |
| 0x90 | f64 | threadPitch = **P** | 26 | |
| 0x98 | i32 | shankType | 28 | 0 Straight, 1 Tapered, 2 Reduced |
| 0xa0 | f64 | shoulderLength = **SL** | 14 | |
| 0xa8 | f64 | shoulderDiameter = **Dsh** (Rsh = Dsh/2) | 18 | |
| 0xb0 | f64 | taperLength = **TL** | 19 | |
| 0xb8 | f64 | neckDiameter = **Dn** (rn = Dn/2) | 29 | |
| 0xc0 | f64 | arborDiameter = **Da** (Ra = Da/2) | 13 | |

### 0.2 Output entity `TlProfileEnt` (0x78 bytes)
Built by `initEntity` (0x180002cb6 → 0x180140060), `GetLineSegment` (0x180002590 → 0x18013fe60) and
`GetArcSegment` (0x18000f759 → 0x18013fd40).
- **Header:** memset 0. +0 type char `'L'` / `'A'`, +1 entColor, +4 i32 = 1.
- **Line** (x0, y0, x1, y1): +0x10 x0, +0x18 y0, +0x28 x1, +0x30 y1.
- **Arc** (cx, cy, r, start, sweep): +0x40 cx, +0x48 cy, +0x58 r, +0x60 start, +0x68 sweep, +0x70 i16 = 1.
  - Angles are in **radians**.
  - A positive sweep is counter-clockwise in (x, y).
  - The end points (+0x10 / +0x28) are filled by `arc_ep_3d` with an identity view matrix.

### 0.3 Coordinate convention (from the entity values)
- x = radius (≥ 0) and y = height above the **tool tip**, with y = 0 at the tip.
- The chain starts at (0, 0), runs outward and upward, and ends with a line back to x = 0 at y = OAL.
  Every make* function starts with a segment from (0, 0) or (0, d), and every shank function ends with `(x, OAL) → (0, OAL)`.
- Holder rows in `TlProfileData` use the same axes, from their own gauge face (`formats/toollib_net.md` §4.1).

### 0.4 Comparisons (`TlProfileBuilderParametric`, tol = params+0x10)
| helper | thunk → body | meaning |
|---|---|---|
| `IsEqualTo(a,b,t)` | 0x18000db5c → 0x18013fef0 | \|a−b\| < \|t\| |
| `IsGreaterThan(a,b,t)` | 0x18000e200 → 0x18013ff10 | a − b > t |
| `IsLessThan(a,b,t)` | 0x18000bc08 → 0x18013ff40 | b − a > t |
| `IsGreaterThanOrEqTo(a,b,t)` | 0x18000952f → 0x18013ff20 | a − b > −t |
| `IsLessThanOrEqTo(a,b,t)` | 0x18000cf72 → 0x18013ff60 | b − a > −t |

Below, "=", "<", "≤", ">" and "≥" between lengths mean these tolerant tests. Degrees are converted with π/180.

### 0.5 Error codes
The values are the names of `Cnc.Tool.Interop.TlProfileResultCode` (`toolnetapi/Cnc.Tool.Interop/TlProfileResultCode.cs`).
A make* function that sets an error returns without appending more entities. The caller then gets the code and
whatever was appended so far. `CalcProfile` still wraps that partial vector (`TlProfileFactory::MakeProfile`).

## 1. Common checks and dispatch

`CheckToolValidity` (0x180007815 → 0x18013fb70) runs first. Its checks, in order:

| condition | code |
|---|---|
| OAL ≤ 0 | 2 IllegalToolLength |
| D ≤ 0 | 1 IllegalToolDiameter |
| FL ≤ 0 | 4 IllegalFluteLength |
| SL > OAL | 3 IllegalShoulderLength |
| FL > OAL | 4 |
| FL > SL | 6 IllegalFluteShoulderLengths |
| Da ≤ 0 | 5 IllegalArborDiameter |
| r1 < 0 | 10 IllegalCornerRadius |

`CalcProfile` (0x1800086c0 → 0x18013f7e0) then switches on MCToolType (params+0x18):

| MCToolType | name (`toollib.c` type_names) | builder (thunk → body) |
|---|---|---|
| 0, 4, 5, 9, 20 | Undefined, Tap RH, Tap LH, Countersink, Block Drill | `makeCounterSinkOrTapOrNullTool` 0x18000e070 → 0x180144000 |
| 1 | Center Drill | `makeCenterDrill` 0x18000db93 → 0x180142ec0 |
| 2, 3, 22 | Spot Drill, Drill, Brad Point Drill | `makeDrill` 0x18000a4bb → 0x180145200 |
| 6 | Reamer | `makeReamer` 0x18000c42d → 0x1801484b0 |
| 7 | Bore | `makeBoringBar` 0x18000b1d1 → 0x180141320 |
| 8 | Counterbore | `makeCounterBore` 0x1800051f0 → 0x180143db0 |
| 10 | Flat Endmill | `makeFlatEndMill` 0x18000cfa9 → 0x1801472e0 |
| 11 | Ball Endmill | `makeBallEndMill` 0x18000c450 → 0x1801406d0 |
| 12, 21 | Chamfer Mill, Engrave Tool | `makeChamferMill` 0x180005f0b → 0x180143370 |
| 13 | Face Mill | `makeFaceMill` 0x1800020d1 → 0x1801454a0 |
| 14 | Slot Mill | `makeSlotMill` 0x180007ccf → 0x180149080 |
| 15 | Radius Mill | `makeRadiusMill` 0x180001951 → 0x1801481b0 |
| 16 | Dove Mill | `makeDoveTailMill` 0x18000939f → 0x180144760 |
| 17 | Taper Mill | `makeTaperMill` 0x18000a001 → 0x180149910 |
| 18 | Lollipop Mill | `makeLolliPopMill` 0x180001712 → 0x1801477a0 |
| 19 | Bull Endmill | `makeBullEndMill` 0x180003d00 → 0x180142130 |
| 23 | High Feed Mill | `makeFeedMill` 0x1800015c8 → 0x1801466e0 |
| 24 | Thread Mill | `makeThreadMill` 0x180009048 → 0x18014a220 |
| 25 | Barrel Mill | `makeBarrelMill` 0x180009052 → 0x180140a80 |
| other | | code 99 UnknownError |

Each tool builder ends with **`makeShankAboveShoulder(x, y)`** (§2), where (x, y) is the last point of the cutter.

## 2. Shank above the shoulder

### `makeShankAboveShoulder(x, y)` (0x1800062f8 → 0x180148e90)
- Does nothing if the result code is already ≠ 0.
- If y = OAL: line (x, OAL) → (0, OAL), and stop.
- Otherwise dispatch on shankType (params+0x98):
  - 0 → Straight
  - 1 → Tapered
  - 2 → Reduced neck
  - any other value appends nothing.

### Straight (0x180002c0c → 0x180149690)
Ra = Da/2.
1. If x ≠ Ra: line (x, y) → (Ra, y).
2. Line (Ra, y) → (Ra, OAL).
3. Line (Ra, OAL) → (0, OAL).

### Tapered (0x18000904d → 0x180149ca0)
Ra = Da/2, Rsh = Dsh/2.
- If Ra ≤ x or TL ≤ 0: use **Straight**.
- Otherwise the top point starts as (xt, yt) = (Ra, y + TL).
- If OAL < y + TL: xt = tan(atan((Ra − Rsh)/TL))·(OAL − SL) + Rsh and yt = OAL.
  This clip uses SL and Rsh, not x and y.

Segments:
1. Line (x, y) → (xt, yt).
2. If OAL > yt: line (xt, yt) → (xt, OAL).
3. Line (xt, OAL) → (0, OAL).

### Reduced neck (0x180007f86 → 0x180148800)
rn = Dn/2, a = Dsh/2 − rn, b = Da/2 − rn.

Errors:
| condition | code |
|---|---|
| rn ≤ 0 | 33 IllegalReducedNeckRadius |
| a < 0 or b < 0 | 34 ReducedNeckLargerThanShoulderShank |
| TL − a − b ≤ 0 | 35 NoNeckLength |

The segments run from (x0, y0) = (x, y). Each step is skipped once y has reached OAL, and each is clipped so that
y never passes OAL:
1. **45° step in.** If OAL > y and a > 0: d = a, or OAL − y if OAL ≤ y + a.
   Line (x, y) → (x − d, y + d). Call the end (x1, y1).
2. **Neck.** If OAL > y1: L = TL − b − a, or OAL − y1 if OAL ≤ y1 + L. Line (x1, y1) → (x1, y1 + L), ending at y2.
3. **45° step out.** If OAL > y2 and b > 0: d = b, clipped to OAL − y2. Line (x1, y2) → (x1 + d, y2 + d), ending at (x3, y3).
4. If OAL > y3: line (x3, y3) → (x3, OAL).
5. Line (x3, OAL) → (0, OAL).

## 3. Cutter outlines per type

Notation: R = D/2, Rsh = Dsh/2, Ra = Da/2. "tan°(v)" means tan(v·π/180). Each step appends one entity, in order.
Arcs are written arc(centre; radius; start; sweep).

### 3.1 Drill, Spot Drill, Brad Point Drill — `makeDrill`
- h = R·tan°(90 − A/2), with A = TipAngle (included angle).
- Error 12 TipToolDiameterIntersectionError if h < 0 or h > SL.

Segments:
1. Line (0, 0) → (R, h).
2. Line (R, h) → (R, SL).
3. Shank(R, SL).

### 3.2 Undefined / Tap RH / Tap LH / Countersink / Block Drill — `makeCounterSinkOrTapOrNullTool`
- Let A' = A, except that A' = 180 when MCToolType = 0 and A = 0.
- **If r1 ≤ 0:** run `makeDrill` with angle1 = A'. With A' = 180 this gives h = 0, a flat bottom: (0,0)→(R,0)→(R,SL).
- **Else (rounded tip):**
  1. t = tan°(90 − A'/2).
  2. Error 10 if t < 1e-12. Error 9 CornerRadiusTooLargeForRadius if r1 > R.
  3. Cone line: through (−r1/t, 0) with slope t, i.e. y = t·x + r1.
     `l_offset_tol` offsets it by r1 toward (R, 0), which gives the line tangent to the circle of centre (0, r1) and radius r1.
  4. P1 = intersection of that line with the arc (centre (0, r1), r1, start 3π/2, sweep π), nearest (R, 0).
     Error 20 CheckCornerRadius on failure.
  5. P2 = intersection of the line with x = R (from y = 0 to OAL), nearest (R, r1).
     Error 23 CheckCornerRadiusArborDiameter on failure.

  Segments:
  1. arc((0, r1); r1; 3π/2; θ(P1) − 3π/2), where θ(P) = atan2 of P − centre in [0, 2π).
  2. Line P1 → P2.
  3. Line P2 → (P2.x, SL).
  4. Shank(P2.x, SL).

### 3.3 Center Drill — `makeCenterDrill`
Inputs:
- rp = Dp/2 and Lp come from DrillDiameter → arg 22 and DrillLength → arg 21.
- A = TipAngle and B = angle3 = ShoulderAngle.

Values: h1 = rp·tan°(90 − A/2) and h2 = tan°(90 − B/2)·(R − rp) + Lp.

Errors:
| condition | code |
|---|---|
| rp ≤ 0 | 1 |
| Lp ≤ 0 | 2 |
| h1 ≥ Lp | 15 TipAngleTooSteep |
| h2 > SL | 16 ShoulderAngleTooSteep |

Segments:
1. Line (0, 0) → (rp, h1).
2. Line (rp, h1) → (rp, Lp).
3. Line (rp, Lp) → (R, h2).
4. Line (R, h2) → (R, SL).
5. Shank(R, SL).

### 3.4 Reamer — `makeReamer`
Errors:
- Code 18 ShoulderLengthTooShortForDiameter if Ra ≠ R and SL < R/4.
- Code 39 ChamferDistanceTooLargeForDiameter if c1 > R.

Segments:
1. Line (0, 0) → (R − c1, 0).
2. If c1 > 0: line (R − c1, 0) → (R, c1).
3. Line (R, c1) → (R, SL).
4. Shank(R, SL).

### 3.5 Counterbore — `makeCounterBore`
- Error 4 if FL ≤ 0.

Segments:
1. Line (0, 0) → (R, 0).
2. Line (R, 0) → (R, SL).
3. Shank(R, SL).

The pilot (Dp, Lp) is **not read** by this builder.

### 3.6 Flat Endmill — `makeFlatEndMill`
Errors:
| condition | code |
|---|---|
| FL ≤ 0 | 4 |
| c1 > R | 39 |
| Rsh = 0 | 30 ShoulderDiameterCantBeZero |

Segments:
1. Line (0, 0) → (R − c1, 0).
2. If c1 > 0: line (R − c1, 0) → (R, c1).
3. If R = Rsh: line (R, c1) → (R, SL), then shank(R, SL).
4. Otherwise:
   1. Line (R, c1) → (R, FL).
   2. Line (R, FL) → (Rsh, FL).
   3. If FL ≠ SL: line (Rsh, FL) → (Rsh, SL).
   4. Shank(Rsh, SL), or shank(Rsh, FL) when FL = SL.

cornerRadius1 is **not used** for type 10, except by `CheckToolValidity`.

### 3.7 Ball Endmill — `makeBallEndMill`
Errors:
- Code 8 ShoulderLengthCornerRadError if SL < r1.
- Code 30 if Rsh = 0.

Segments:
1. arc((0, R); R; 3π/2; π/2). The ball radius is **R**, not r1.
2. Then as the flat endmill from point (R, R):
   - If R = Rsh: line (R, R) → (R, SL).
   - Otherwise: line (R, R) → (R, FL), line (R, FL) → (Rsh, FL), and if FL ≠ SL, line (Rsh, FL) → (Rsh, SL).
3. Shank.

### 3.8 Bull Endmill — `makeBullEndMill`
Errors:
- Code 9 if r1 > R.
- Code 30 if Rsh = 0.

Segments:
1. If r1 < R: line (0, 0) → (R − r1, 0).
2. arc((R − r1, r1); r1; 3π/2; π/2).
3. Then the flat-endmill tail from (R, r1), as in §3.7.

### 3.9 Chamfer Mill and Engrave Tool — `makeChamferMill`
- Rb = max(D, Dt)/2 and Rt = min(D, Dt)/2. The two are swapped when Dt > D.
- h = tan°(90 − A)·(Rb − Rt). **A is the per-side angle (not halved).**

Errors:
| condition | code |
|---|---|
| Rt ≥ Rb | 19 TipDiameterExceedsToolDiameter |
| h < 0 or h > SL | 12 |
| r1 < 0 | 10 |

**If r1 = 0:**
1. If Rt > 0: line (0, 0) → (Rt, 0).
2. Line (Rt, 0) → (Rb, h).

**If r1 > 0:**
1. Take the bottom line (0,0)-(Rb,0), offset by r1 toward (Rb, r1).
2. Take the flank (Rt,0)-(Rb,h), offset by r1 toward (r1, 0).
3. C = their intersection (`l_ints_tol`). Both parameters must be in [0, 1] and every offset-flank coordinate must be
   ≥ 0, else error 20.
4. T = intersection of the circle (C, r1) with the flank. The intersection with the bottom line is computed first and then
   overwritten.

Segments:
1. Line (0, 0) → (C.x, 0).
2. arc(C; r1; 3π/2; θ(T) − 3π/2).
3. Line T → (Rb, h).

Tail (both cases):
1. If h < SL: line (Rb, h) → (Rb, SL).
2. Shank(Rb, SL).

### 3.10 Face Mill — `makeFaceMill`
Inputs:
- Rb = max(D, Dt)/2 and Rt = min(D, Dt)/2.
- A = angle1. ct = cornerType (params+0x1c; 3 = TL_CORNER_FULL_RADIUS).

Derived values:
- If A ≥ 90 or A = 0: h = 0 and d4 = Rb.
- Otherwise h = tan°(90 − A)·(Rb − Rt) and **d4 = h/4**.

Errors:
| condition | code |
|---|---|
| Rt > Rb | 19 |
| h < 0 or h > SL | 12 |
| r1 < 0 or r1 > FL | 10 |
| Rb = Rt and A ≠ 0 | 12 |
| A = 0 and SL < r1 | 12 |

**Path F1** (A = 0, or ct = 3 with Rb − Rt = r1):
- If r1 > 0:
  1. Line (0, 0) → (Rt, 0).
  2. arc((Rt, r1); r1; 3π/2; π/2).
  3. If Rb ≠ Rt and Rt + r1 ≠ Rb: line (Rt + r1, r1) → (Rb, r1).
  4. If SL > r1: line (Rb, r1) → (Rb, SL).
- If r1 = 0:
  1. Line (0, 0) → (Rb, 0).
  2. Line (Rb, 0) → (Rb, SL).
- Then shank(Rb, SL).

If ct = 3 and Rb − Rt ≠ r1: error 20.

**Path F2** (A ≠ 0 and ct ≠ 3):
1. X = intersection of the line (0, d4)-(Rt, d4) with the line (Rt, 0)-(0, tan°(90 − A)·Rt). Both parameters must be in
   [0, 1], else error 26 CheckTaperAngle.
2. If r1 ≤ 0:
   1. Line (0, d4) → (X.x, d4).
   2. If Rt ≠ Rb: line (X.x, d4) → (Rt, 0), then line (Rt, 0) → (Rb, h).
   3. If h < SL: line (Rb, h) → (Rb, SL).
   4. Shank(Rb, SL).
3. If r1 > 0:
   1. The flank is the line through (Rt − Δ, r1 − tΔ) and (Rb, r1 + tΔ), with Δ = Rb − Rt and t = tan°(90 − A).
      It is offset by r1 toward (Rb, 0).
   2. P1 = intersection with the arc (centre (Rt, r1), r1, start 3π/2, sweep π), nearest (Rb, r1).
      Error 20 on failure or if P1.x ≥ Rb.
   3. P2 = intersection with x = Rb (y from 0 to OAL), nearest (Rt, r1). Error 23 on failure.

   Segments:
   1. Line (0, d4) → (X.x, d4).
   2. Line (X.x, d4) → (Rt, 0).
   3. arc((Rt, r1); r1; 3π/2; θ(P1) − 3π/2).
   4. Line P1 → P2.
   5. Line P2 → (P2.x, SL).
   6. Shank(Rb, SL).

### 3.11 Slot Mill — `makeSlotMill`
Inputs:
- e1 = r1 if r1 > 0, else c1 (lower edge).
- e2 = r2 if r2 > 0, else c2 (upper edge).

Errors:
| condition | code |
|---|---|
| r1 < 0 | 10 |
| e1 > FL/2, e2 > FL/2 or FL < e1 + e2 | 14 CornerRadiusOrFluteLengthError |
| Ra > R − e2 | 13 ArborDiameterTooLarge |

Segments:
1. Line (0, 0) → (R − e1, 0).
2. Lower edge:
   - If c1 > 0: line (R − c1, 0) → (R, c1). This branch wins even when r1 > 0, so then the first line ends at R − r1 —
     taken literally.
   - Else if r1 > 0: arc((R − r1, r1); r1; 3π/2; π/2).
3. Unless e1 = FL/2 and e2 = FL/2: line (R, e1) → (R, FL − e2).
4. Upper edge:
   - If c2 > 0: line (R, FL − c2) → (R − c2, FL).
   - Else if r2 > 0: arc((R − r2, FL − r2); r2; 0; π/2).
5. **Shank(R − e2, FL).** The shank starts at the flute length, not at SL.

### 3.12 Radius Mill (corner rounding) — `makeRadiusMill`
Errors:
- Code 10 if r1 ≤ 0.
- Code 8 if r1 ≥ SL.

Segments:
1. If R > 0: line (0, 0) → (R, 0).
2. arc((R + r1, 0); r1; π; −π/2). This is a concave quarter, clockwise from (R, 0) to (R + r1, r1).
3. Line (R + r1, r1) → (R + r1, SL).
4. Shank(R + r1, SL).

### 3.13 Dove Mill — `makeDoveTailMill`
Inputs:
- A = angle1, tA = tan°(A), hA = tan°(90 − A)·(R − Ra).
- xFL = R − tA·FL.

Errors:
| condition | code |
|---|---|
| r1 < 0 | 10 |
| r1 > FL | 14 |
| r1 > R | 9 |
| Ra > R | 11 ArborRadiusExceedsToolRadius |
| R = Ra | 21 ToolDiameterEqualsArborDiameter |
| A > 89 | 26 |

**r1 = 0:**
- Error 25 TaperAngleOrFluteLengthError if xFL ≤ 0.

Segments:
1. Line (0, 0) → (R, 0).
2. Line (R, 0) → (xFL, FL).
3. If SL > FL: line (xFL, FL) → (xFL, SL).
4. Shank(xFL, SL).

**r1 > 0:**
1. cx = R − r1.
2. The line from (cx, r1) toward (Ra − R + cx, hA + r1) is offset by r1 toward (R, 2r1). Its first point is the tangent
   point P0.
3. If P0.y > FL:
   - θ = asin((FL − r1)/r1). θ is 0 when FL − r1 < tol.
   - P0 = (cx + r1·cosθ, FL) and the sweep is θ + π/2.
4. Otherwise the sweep is θ(P0) − 3π/2 + 2π.
5. x' = P0.x − (FL − P0.y)·tA.
6. Error 25 if x' ≤ 0. Error 20 if the check intersection with y = hA (`l_ints_tol`) fails or falls outside [0, 1].

Segments:
1. Line (0, 0) → (cx, 0).
2. arc((cx, r1); r1; 3π/2; sweep).
3. If P0.y ≤ FL: line P0 → (x', FL).
4. If FL < SL: line → (x', SL).
5. Shank(x', SL).

The r1 > 0 branch is decoded from stack-aliased decompiler output (`dStack_268` / `pMStack_260` hold P0). Re-check
against the disassembly at 0x180144760 before relying on the exact P0.

### 3.14 Taper Mill — `makeTaperMill`
- A = angle1 (per-side taper).
- Error 10 if r1 < 0.

Segments:
1. If R > r1: line (0, 0) → (R − r1, 0).
2. If r1 > 0:
   1. arc((R − r1, r1); r1; 3π/2; (90 − A)°). Its end point is E.
   2. xt = tan°(A)·(SL − E.y) + E.x.
   3. Line E → (xt, SL).
3. If r1 = 0: xt = tan°(A)·SL + R, and line (R, 0) → (xt, SL).
4. Shank(xt, SL).

### 3.15 Lollipop Mill — `makeLolliPopMill`
Errors, in order:
1. r1 < 0 → 10.
2. If shankType = 0, TL ≤ 0 or Rsh = Ra: Ra > R → 11.
   Otherwise (a tapered or reduced shank with TL > 0 and Rsh ≠ Ra): Ra = Rsh → 27. That test is unreachable here.
3. Ra < Rsh → 27 ShoulderDiameterLargerThanArbor.
4. Rsh = 0 → 30.
5. R < Rsh → 37.
6. SL < h → 18, where h = R + √(R² − Rsh²).

Segments:
1. arc((0, R); R; 3π/2; acos(Rsh/R) + π/2). It ends at (Rsh, h).
2. If SL > h: line (Rsh, h) → (Rsh, SL).
3. Shank(Rsh, SL).

### 3.16 High Feed Mill — `makeFeedMill`
Inputs:
- A1 = angle1 (lower taper) and A2 = angle2 (upper taper).
- Rel = angle3 (relief taper).
- r1 = lower radius, r2 = upper radius.
- Rt = Dt/2.

Errors, in order:
| condition | code |
|---|---|
| Rel > 0 and R ≤ Rsh | 47 |
| Rel > 90 or Rel < 0 | 46 |
| R ≤ Rt | 19 |
| A1 > 90 | 44 |
| A2 ≥ 90 or A2 < 0 | 45 |
| A1 < A2 | 44 |
| Rsh = 0 | 30 |

a1 = (90 − A1)° and a2 = (90 − A2)°, in radians.

1. Line (0, 0) → (Rt, 0).
2. **Lower radius.**
   - If r1 > 0: arc((Rt − sin a1·r1, cos a1·r1); r1; a1 + 3π/2; a2 − a1). The current point P = its end.
     Error 42 if R < P.x.
   - Otherwise P = (Rt, 0).
3. **Upper radius.**
   - If r2 > 0: arc2 has centre (R − r2, ((cos(a2+3π/2)·r2 + R − r2) − P.x)·tan a2 + P.y − sin(a2+3π/2)·r2), radius r2,
     start a2 + 3π/2 and sweep (Rel° + 2π) − (a2 + 3π/2). Its start is S.
     1. Error 43 if S.x < P.x.
     2. If S ≠ P: line P → S. Its |Δx| and |Δy| must exceed tol and |a2 − atan(Δy/Δx)| < tol, else error 43.
     3. Append arc2. The current point = its end (x, y).
   - Otherwise: line P → (R, tan a2·(R − P.x) + P.y). The current point = that end.
4. Error 18 if y > SL.
5. **Relief.**
   - Rel = 90: line (x, y) → (Rsh, y), then line (Rsh, y) → (Rsh, SL), then shank(Rsh, SL).
   - Rel = 0: if y ≠ SL, line (x, y) → (x, SL). Then shank(x, SL).
   - 0 < Rel < 90:
     1. yt = (R − Rsh)/tan°(Rel) + y and xe = Rsh.
     2. If yt > SL: xe = x − (SL − y)·tan°(Rel) and yt = SL.
     3. Line (x, y) → (xe, yt).
     4. If yt ≠ SL: line (xe, yt) → (xe, SL).
     5. Shank(xe, SL).

### 3.17 Thread Mill — `makeThreadMill` and `makeThread` (0x180003d32 → 0x18014a050)
- Error 30 if Rsh = 0.
- A running height y starts at 0. Each `makeThread(y)` call returns the entities of one tooth step and advances y.

`makeThread` dispatches on threadShape (params+0x88):
| shape | builder |
|---|---|
| 0, 1 (Unified, Metric) | `makeUNISOThread` |
| 2, 3 (Acme, Acme_Stub) | `makeACMEThread` |
| 4, 5 (Buttress_Pull, _Push) | `makeButtressThread` |
| 6 (NPT) | `makeNPTThread` |

Procedure:
1. First call: emit line (0, 0) → (x0 of the first tooth entity, 0), then the tooth entities.
2. While y < FL: call again and append.
3. Let xe = x1 of the last entity.
4. If Rsh ≠ xe and Rsh ≠ y: line (xe, y) → (Rsh, y). The second comparison really is against y in the decompile —
   taken literally. Then x = Rsh.
5. If SL ≠ y: line (x, y) → (x, SL).
6. Shank(x, SL).

Every tooth builder below stops at FL: when the next piece would reach FL (y + piece ≥ FL), it emits a truncated last
piece ending at y = FL and sets y = FL.

**UN / ISO** (`makeUNISOThread` 0x18000766c → 0x18014a6f0):
- ri = R − 0.866·P and half = P/2. Error 38 ThreadPitchTooLargeForDiameter if ri ≤ 0.

Sequence:
1. Line (ri, y) → (R, y + half).
2. Line (R, y) → (ri, y + half).

Truncated pieces:
- Rising: (ri, y) → ((FL − y)/tan 30° + ri, FL).
- Falling: (R, y) → (R − (FL − y)/tan 30°, FL).

**ACME** (`makeACMEThread` 0x18000204f → 0x1801400c0):
- Depth d = 0.5·P (Acme) or 0.3·P (Acme_Stub, constant 0x3fd3333333333333).
- t = tan(0.2530727 rad) = tan 14.5°.
- dyf = t·d and flat = (P − 2·dyf)/2. ri = R − d. Error 38 if ri ≤ 0.

Sequence:
1. Flank up: (ri, y) → (R, y + dyf).
2. Crest flat: (R, y) → (R, y + flat).
3. Flank down: (R, y) → (ri, y + dyf).
4. Root flat: (ri, y) → (ri, y + flat).

Truncation: a flank becomes (·, y) → (ri + (FL − y)/t or R − (FL − y)/t, FL), and a flat becomes (·, y) → (·, FL).

**Buttress** (`makeButtressThread` 0x180007a45 → 0x1801425d0). Constants:
- α = 45° (Pull, shape 4) or 7° (Push, shape 5).
- β = 52° − α.
- ri = R + 0.0826·P − 0.89064·P. Error 38 if ri ≤ 0.
- rr = 0.07141·P.
- A probe arc of centre (R − rr, 0), radius rr, start α + 270° and sweep 128° (2.234021 rad) gives the crest geometry.

Order: flank line at slope tan α from (ri, y), then two crest arcs of radius rr, then the β flank back to ri. Each piece
is clipped at FL; the clip angle of an arc uses asin((y_end − FL)/rr). The exact argument values are **NOT DECODED**:
the decompile reuses stack slots for the probe arc's fields. Next: disassembly of 0x1801425d0.

**NPT** (`makeNPTThread` 0x18000e395 → 0x180147bc0):
- k = tan(atan(1/32)) = 1/32 per side.
- up = ((k·P)/tan 60° + P)/2, down = P − up, h = 0.8657·P.
- Root radius at height y: xr(y) = R − ((FL − y)·k + h). Error 38 if xr(y) ≤ 0 or xr(y + up + down) ≤ 0.
- Crest at y1 = y + up: xc = R − (FL − y1)·k.

Sequence:
1. Line (xr(y), y) → (xc, y1).
2. Line (xc, y1) → (xr(y1 + down), y1 + down).

The truncated pieces use tan 30° as for UN.

### 3.18 Barrel Mill — `makeBarrelMill`
Inputs: r1 = lower radius and Rp = r2 = profile radius.

Errors:
| condition | code |
|---|---|
| r1 ≥ Rp | 36 |
| R < Rsh | 37 |
| FL > 2Rp or R − Rsh > Rp | 40 |
| Rsh = 0 | 30 |

Derived values:
- a = Rp + Rsh − R and cx = R − Rp.
- cy = FL − √(Rp² − a²). Error 40 if cy > Rp, error 41 if cy < 0.
- φt = acos(a/Rp) and φb = asin(cy/Rp).
- The probe arc has centre (cx, cy), radius Rp, start 2π − φb and sweep (φt or φt + 2π) − start. Its start point S must
  satisfy:
  - S.y ≤ 0;
  - S.x > 0 when r1 > 0, or S.x ≥ 0 when r1 ≤ 0;
  - else error 40.
- Error 36 if cy < r1.
- d = Rp − r1, e = cy − r1, w = √(d² − e²). Error 40 if cx > w and r1 > 0.
- θ = acos(e/d) + 3π/2 and xl = w + cx.

Segments:
1. If xl > 0: line (0, 0) → (xl, 0).
2. If r1 > 0: arc((xl, r1); r1; 3π/2; θ − 3π/2).
3. If θ ≠ 2π and θ ≠ 0: arc((cx, cy); Rp; θ; 2π − θ).
4. If φt > 0: arc((cx, cy); Rp; 0; φt).
5. If FL ≠ SL: line (Rsh, FL) → (Rsh, SL).
6. Shank(Rsh, SL).

### 3.19 Bore (boring bar) — `makeBoringBar`
- A = angle1 (taper).
- xFL = R − FL/tan°(90 − A).

Errors:
| condition | code |
|---|---|
| r1 < 0 | 10 |
| Ra > R | 11 |
| R = Ra | 21 |
| xFL ≤ 0 | 25 |

Cases:
- **r1 > 0 and xFL < R:**
  1. Line (0, 0) → (R − r1, 0).
  2. arc((R − r1, r1); r1; 3π/2; A° + π/2). Its end is E.
  3. X = intersection of the flank (from (R − r1, r1) toward (xFL − R + (R − r1), FL + r1)), offset by r1 toward (R, 2r1),
     with y = FL. Error 20 if outside [0, 1].
  4. Line E → (X.x, FL). xFL := X.x.
- **r1 > 0 otherwise:**
  1. The bottom (0,0)-(R,0) is offset by r1 toward (R, r1), and the flank (R,0)-(xFL,FL) by r1 toward (0, r1).
     C = their intersection; error 20 if outside [0, 1].
  2. T = the tangent point on the flank.
  3. Line (0, 0) → (C.x, 0).
  4. arc(C; r1; 3π/2; θ(T) − 3π/2 + 2π).
  5. Line T → (xFL, FL).
- **r1 = 0:** line (0, 0) → (R, 0), then line (R, 0) → (xFL, FL).

Tail:
1. If SL ≠ FL: line (xFL, FL) → (xFL, SL).
2. If xFL ≠ Ra: line (xFL, SL) → (Ra, SL).
3. Shank(Ra, SL).

## 4. Where the parameters come from

### 4.1 .NET Tool Manager path
`PU.BuildParametricToolProfile` calls `RetrieveParametricToolProfile`. Its column → argument table is in
`formats/toollib_net.md` §4.2.

### 4.2 Native path (part-file tools)
`TlToolMill::GetProfile` → vtable+0x88 `TlToolMill::DoGetProfile` (mccore 0x18083e600):
- If `this+0x150` (display type) = 0: call vtable+0x118 = `GetProfileParams`, then the imported
  `TlProfileBuilderParametric` vftable slot 0 (`CalcProfile`). This is the same math as above.
- Otherwise:
  - 1 → `ToolProfileImporter::ImportFromFile`
  - 2 → `ImportFromLayer`
  - 3 → `ImportFromStoredProfile`
  - In each case the profile is then cloned and `TrimOrExtendToYValue(this+0x7f0)`.

`GetProfileParams` (mccore) fills the struct from object fields. `TlToolMill::GetProfileParams` (0x180840250):
- memset 200, entColor 9, nc_tol / tol, isMetric.
- Fields: type ← +0x8b8, OAL ← +0x7f0, FL ← +0x800, D ← +0x808, shankType ← +0x820, Dn ← +0x830, TL ← +0x828,
  Dsh ← +0x818, SL ← +0x7f8, A ← +0x178, Da ← +0x810.

The subclasses call it first and then set these fields:

| class (address) | fields set |
|---|---|
| Drill 0x18075ed20 | r1 ← vtbl+0x100(), B ← +0x970, A ← +0x968 |
| Boring 0x18075a7d0 | Drill's, then r1 ← vtbl+0x100(), cornerType ← vtbl+0x120(), A ← +0x980, B ← +0x970 |
| CenterDrill 0x180a2fe80 | Drill's, then type = 1, Lp ← +0x988, Dp ← +0x980, A ← +0x968, B ← +0x970 |
| CounterBore 0x180a317d0 | Drill's, then type = 8, Lp ← +0x988 **then overwritten by** +0x980 (Lp written twice, Dp never set) — taken literally |
| Endmill 0x180767840 | r1 ← vtbl+0x100(), A ← +0x968, Dt ← +0x970, c1 ← +0x980, cornerType ← +0x978 |
| SlotMill 0x180a33f20 | Endmill's, then r2 ← +0x998, c2 ← +0x9a0 (others as Endmill) |
| Reamer 0x1808550c0 | c1 ← +0x970 |
| Threading 0x18085f390 | r1 ← vtbl+0x100(), A ← GetTipAngle(), Dt ← +0x988, P ← +0x970, shape ← +0x998 |
| FeedMill 0x180a37750 | Dt ← +0x968, A ← +0x990, C ← +0x980, B ← +0x988, r1 ← +0x970, r2 ← +0x978 |
| BarrelMill 0x180a3a390 | r1 ← vtbl+0x100(), r2 ← +0x970 |

The getter names for these object offsets are **NOT DECODED**. Next: the mccore `TlToolMill` / `TlToolEndmill` getters
that return `this+0x7f0` and the other offsets.

### 4.3 Display tool in Mastercam: `MillTool` (mccore)
`MillTool::BuildParametricMillToolProfile` (0x180529c30) gathers the same values per class and calls
**`MillTool::build_auto_mill_tool`** (0x18052c380). That function:
1. Calls `AddAxisEntities(D, OAL)`.
2. Fills a `MillToolProfileParams` with entColor 9, `IsMetric()`, nc_tol and tol.
3. Runs the imported parametric builder.
4. Converts each `'L'` entry to an ent of type 2 and each `'A'` entry to type 4 (`AddEntToBuffer`).
5. On a non-zero result code, calls `makeBigFatRedX` instead.

## 5. Backplot tool and holder

### 5.1 Tool
- backplot.dll `FUN_66d78a00` draws the tool and holder at the current position (`formats/backplot.md` §5).
- The geometry comes from backplotbnci.dll `FUN_67d1a2e0`, which reads named tool attributes from the NCI tool data
  (`FUN_67d36f20`): `tool_type`, `tool_len`, `tool_dia`, `Tool_corn_rad`, `Tool_tip_angle`, `arbor_dia`, `shldr_len`,
  `Tool_shoulder_angle`, `flute_len`, `Tool_tip_dia`, `hldr_dia`, `hldr_len`, `shoulderDiameter`, `taperLength`,
  `Tool_corn_type1`, `Tool_corn_rad2`, `Tool_chamfer_len1/2`, `Tool_chamfer_angle`, `Tool_Pilot_Len/Dia`, `Threads`,
  `ThreadShape`, `ShankType`, `NeckDiameter`.
- It passes them to `FUN_67d16db0`.

`FUN_67d16db0`:
- Remaps `Tool_corn_type1` 0 → 0, 1 → 2, 2 → 3, i.e. into TlCornerType.
- Sets the thread pitch = `Threads` in mm, or 1/`Threads` in inch.
- For types 12 and 21, swaps the primary and secondary diameter arguments.
- Calls the imported **`MillTool::build_auto_mill_tool`**, which uses the same `TlProfileBuilderParametric` math.
- Takes the entities with `MillTool::get_tool_entity_info`.

If build_auto_mill_tool fails, it shows a single line from (−D/2, 0) to (D/2, 0) at height `tool_len`
(`local_330` / `local_310`).

`FUN_67d1a2e0` also has a fallback for tools without data: type 10, holder diameter D·1.1, `0.99` and `0.25` constants
(line ~195 of the dump). That case is **NOT DECODED**.

### 5.2 Holder (legacy)
`FUN_67d18a90(tool_len, hldr_dia, hldr_len, arbor_dia)`:
- **Without a file** `<ToolPrefix>holder` (`FUN_67d63ed0` = 0), it emits three lines with x = radius, z = height:
  1. (0, tool_len) → (hldr_dia/2, tool_len)
  2. → (hldr_dia/2, tool_len + hldr_len)
  3. → (0, tool_len + hldr_len)

  So the holder face sits at **z = tool_len** above the tip.
- **With the file**, it reads the file with `get_profile_from_mc` (scale 1 inch / 25.4 mm), sorts it with `sort_profile`,
  and places the profile from y = tool_len. Placement details are **NOT DECODED**.

### 5.3 Holder (tool-system assemblies)
`FUN_67d1a2e0` → `Cnc::Tool::GetInternalToolSystem` → `TlAssembly::GetMainHolder` → `FUN_67d16540`:
1. Takes the main holder's `GetStoredProfileConst()`.
2. Takes `GetProfileSegmentsTopDown(…)`.
3. Computes offset = `GetHeight()` − `GetLength()`.
4. Builds the solid via `FUN_67d13e60`.
5. Throws "Backplot: Holder bounds are outrageous" when the bounds diagonal is < 1e-6 or > 1e10.

How `tool_len` relates to the holder stick-out of `formats/toollib_net.md` §5 (AssembleComponents) is **NOT DECODED**.
Next: the writer of the `tool_len` attribute — the backplotbnci NCI 1000-series reader, `FUN_67d36f20` callers.

## 6. Status
- **Fully decoded:** dispatch; common checks; shank (straight, tapered, reduced neck); drill; spot; brad;
  tap / countersink / null; center drill; reamer; counterbore; flat; ball; bull; chamfer / engrave; face; slot; radius;
  taper; lollipop; feed mill; barrel; bore; thread-mill wrapper; UN/ISO, ACME and NPT teeth.
- **Partly decoded:** the dove mill r1 > 0 branch (stack-aliased locals; see §3.13); buttress teeth (§3.17).
- **NOT DECODED:**
  - the mccore getter names for the object offsets in §4.2;
  - the backplot `tool_len` origin and stick-out (§5.3);
  - the holder-file placement (§5.2);
  - the `FUN_67d1a2e0` no-data fallback.
- The port replaced the old `viewer/toollib.c` `tool_outline()` geometry (its 3·D flute / OAL defaults,
  corner-radius-on-any-type rule, 118° default angle and "half the shank sticks out" holder placement had no source and
  are gone); `tool_outline()` now calls the port (below).

## 7. Code: `viewer/toolprofile.c` / `toolprofile.h`

Port of `TlProfileBuilderParametric`; every builder carries its spec section and tlcore thunk → body address.

| Function / struct | Behaviour | Spec |
|---|---|---|
| `TpParams` | `MillToolProfileParams` (200 bytes), one member per offset +0x00..+0xc0 | §0.1 |
| `TpSeg` | one `TlProfileEnt`: type 2 line / 4 arc, end points, arc centre / radius / start / sweep (radians) | §0.2 |
| `EQ` / `GT` / `LT` / `GE` / `LE` macros | the tolerance comparisons with tol = params +0x10 | §0.4 |
| `TP_*` codes | `TlProfileResultCode` values; `TP_ERR_OVERFLOW` (-1, output array full) and `TP_ERR_NOT_DECODED` (-2) are ours, not Mastercam's | §0.5 |
| `tp_build` | `check_validity` (CheckToolValidity), then the per-type dispatch to `make_*` (types 0/4/5/9/20 → countersink-tap-null, 2/3/22 → drill, 12/21 → chamfer, ...); unknown type → 99. Returns the first error with the partial chain, as CalcProfile | §1, §3 |
| `shank*` | straight / tapered / reduced-neck shank above the shoulder | §2 |
| `make_*` | one per cutter outline | §3.1-3.19 |
| `make_dove` with r1 > 0, buttress thread shapes 4 / 5 | return `TP_ERR_NOT_DECODED` (no geometry is invented) | §3.13, §3.17, §6 |
| `tp_polyline` | (radius, height) points; arcs sampled at `arc_step_rad` (ours: a display sampling step) | - |
| `tp_params_from_tool` | entColor 9, nc_tol 1e-4, tol 5e-5, A = 180, C = 45; the .NET `RetrieveParametricToolProfile` arguments per class for library tools (`has_extras`); a part-file tool (no extras) gets only corner radius, tip / taper angle (+0x20), tip dia, pitch | toollib_net.md 4.2, §4.3 |
| `tp_err_name` | result-code names | `TlProfileResultCode.cs` |

Users: `viewer/toollib.c` `tool_outline` (on an error the single line of §5.1; arcs sampled at π/16), which feeds the
scene's tool display (`viewer/scene.c`) and the wizard preview (`viewer/mcamui.c`).
Test: `tools/tests/toolprofile_test.c` (standalone): builds every MCToolType with spec-default parameters, prints the
segments, checks continuity, start point and closure, and writes one SVG per case.
