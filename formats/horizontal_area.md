# HST Horizontal Area (op 132, UI style 4, kernel style 3) - decode and porting spec (2026-10-26)

Status (2026-11-08): the open-side region is DECODED and the op generates (section 9.5; viewer/cam3d_horiz.c, CAM_HORIZAREA, gallery item live). The earlier refusal at the open sides is gone. Earlier status: flat detection and oracle (section 9), decode (sections 0-8). Builds on formats/hst3d_port_plan.md (sections 1.1, 4.3-4.5, 6.6-6.7, 7, 9) and
formats/area_mill.md 11.3 (the Area Clearance pass rule, ported as viewer/area_mill.c `area_mill_passes`).

Citation keys (as hst3d_port_plan.md): **mk** = decomp/mckernel_all.c, **hz** = decomp/hmm_zplan.c, **hp** = decomp/hmm_pass0.c,
**hpl** = decomp/hmm_plans1.c, **hl** = decomp/hmm_link.c, **ms** = decomp/mcmsurf_all.c, **res** = exports/mcmsurfres_dialogs.tsv.
McHmm functions that are not in decomp/ were decompiled for this spec with tools/ghidra_scripts/DecompAt.java on a copy of
~/mastercam-re/ghidra_hmm (program mchmm.dll); the output is kept in the session scratch directory
`decomp/horizarea/`:
**gA** = `ha/d1.c` (FUN_180313650, FUN_180320750, FUN_18031eb70, FUN_18031f660, FUN_1803ba250, FUN_1803ba8f0),
**gB** = `ha/d2.c` (FUN_1801e2690, FUN_180256b40, FUN_180098d40, FUN_1804a1b80). Copy them into decomp/ if they are to be cited
long-term (gitignored scratch disappears). Wide-string constants were resolved with ~/mastercam-re/work/hst3d/scripts/wstr.py.

Labels: **[inferred]** = a mapping argued from consistent block shifts / page semantics, not from a copy routine; **measured** = from
stored data; **NOT DECODED** = not found. The TP -> record copy is mccore's and is NOT DECODED for every field (as for the other HST
ops); the block shifts used below are the ones of hst3d_port_plan.md 6.1b / 6.6 / 6.7 / 9.

## 0. Summary

- Chain: mcmsurf `msurf_hmm_horizontal_area_toolbar` sets op +0x1654 (record 0x16bc) = 4 (ms l.211756-211762). mckernel builds
  **Horizontal Area Passes 0x143a** (`ekPlan4`), or, when TP+0x44c (NumberOfOffsets) >= 1, 0x143a as `ekPlan4x` followed by
  **Axial Offset Passes 0x143b** (mk l.93456-93500). No 2D Stock Model, no Plan of Plans for style 3 (the stock-model branch needs
  local_250, which is 0 for style 3: mk l.93084-93101, l.93503-93509 -> LAB_1800815ae l.93863). Linking: **Horizontal Area Passes
  Linking Plan 0x144c** (mk l.92450-92456).
- McHmm: EvaHorizontalAreaClearancePlan (ctor FUN_18040fa80 hz l.14152-14168: Eva +0xff0 = 1), Evaluate FUN_18040fb90 (hz l.14203-14716):
  1. FUN_1803fb4f0(this, 1) - the Z-profile setup; arg 1 also copies the first weave into the two extra weaves Eva+0x1090 / +0x1260
     (hz l.1219-1222); EnableTrimToBoundaries -> FUN_1802ef450 (hz l.1143).
  2. FUN_180411bd0 - the **flat levels** (hz l.15258-15552; section 2).
  3. per flat: FUN_1804109a0(this, .., 0) builds two weaves just above / below the flat and classifies the lower weave's loop
     points (section 3.1); `FUN_1803faee0(this, 4)` = DoPockets with **pass kind 4** on it (hz l.14324); then `FUN_1803faee0(this, 1)`
     (boundary loops only) on the upper weave (hz l.14422-14440); every kind-4 region is attached to the kind-1 region that contains
     its sample point (MeasureInterior2D, hz l.14523-14680); FUN_1803ffdb0(this, 1) / FUN_1803fc9b0 (hz l.14683-14686).
  4. FUN_180402f40 writes all passes (hz l.14703).
- **Pass kind 4 = the Area Clearance positions** (area_mill.md 11.3): FUN_180406b00 takes `bVar3 = kind - 2 < 3` (hp l.1811), so kinds
  2, 3, 4 read MinOffset / RadExtra / the corner tolerances the same way (hp l.1861-1947) and place passes with FUN_180555f60.
  No other kind-4 test exists in FUN_180406b00 / FUN_180407c10 / FUN_1803faee0 (searched: the kind is only passed through,
  hz l.921-1096, decomp/hmm_zprofile.c l.133 / 648-654). So per flat region: `area_mill_passes(T, MinOffset, MinOffset, MaxOffset -
  MinOffset, tau)`.
- **Biggest gaps**: (a) how CZInflexionsHorizontal turns triangles into (z_lo, z_hi, area) bands (section 2.2); (b) which 2D region
  kind 4 clears - the code builds it from the weave below the flat with edge offsets, the exact region semantics are NOT DECODED
  (section 3.2); (c) the general linker (as every HST op); (d) no stored style-4 op with NCI exists (section 7).

## 1. Op record -> kernel parameters (FUN_180071314 style-3 branch, mk l.87992-88067, + the common part)

TP = the TPHSTToolPath parameter block (`param_1 + 0x18`). Record offsets per the block shifts named in the right column.

| kernel parameter | value (code) | source | record field | mapping |
|---|---|---|---|---|
| MinOffset | min(TP+0x230, TP+0x238) | mk l.87994-88015 | 0x12b8 / 0x12c0 (page 1327: 1245 Minimum / 1246 Maximum) | [inferred], shift 0x1088 as Area Roughing (port plan 9.3) |
| MaxOffset | max(TP+0x230, TP+0x238) | mk l.88016-88021 | same | same |
| PosZHi | dVar3 = job +0x40, or with TP+0x464 max(TP+0x1c0, TP+0x1c8) + job +0x28 | mk l.87308-87332, l.88022-88027 | Use Z depths 0x16c9, 0x1238 / 0x1240 | [inferred] (Waterline port, port plan 7) |
| PosZLo | local_870 + param_5 (param_5 = 0 for style 3) | mk l.88028-88033 | same | same |
| ProfileSmoothing | TP+0x461 != 0 | mk l.88034-88038 | Corner rounding 0x16c3 | [inferred] (Area Roughing / Waterline, same TP byte mk l.87780 / 87875) |
| ProfileSmoothingCornerTolerance | local_838 = max(TP+0x1f0, 2 x job +0xa0) | mk l.87382-87385, l.88042-88046 | Profile tolerance 0x1270 | [inferred] (port plan 9.3) |
| ProfileSmoothingMaxRadius | TP+0x1f8 | mk l.88047-88052 | Maximum radius 0x1278 | [inferred] |
| ProfileSmoothingCornerOffsetTolerance | local_820 = max(TP+0x240, 2 x job +0xa0) | mk l.87386-87389, l.88053-88058 | Offset tolerance 0x12c8 | [inferred] |
| HorizontalTipEdgeOffsetIn | 0.0, only when job +0x10 < 0 | mk l.88061-88067 | - | job +0x10 is the epsilon every other test of these functions uses as "value > eps" (e.g. mk l.89494, 89505, 89624), so this override does not run in practice [inferred]: the McHmm default applies (section 2.3) |
| ToolThicknessXY | TP+0x318, when TP+0x80 + min(TP+0x318, TP+0x320) >= -TP+0x220 | mk l.87501-87515 | 0x13c0 (wall stock) | [inferred] (port plan 6.6, shift 0x10a8) |
| ToolThicknessZ | **min(TP+0x318, TP+0x320)** (the else-arm's name string is elided by the decompiler: `Ordinal_310()` re-using local_878 = "ToolThicknessZ") | mk l.87520-87535 | 0x13c8 (floor stock) | [inferred] |
| ToolThicknessXY / Z (negative stock below the corner radius: TP+0x80 + min(XY, Z) < -TP+0x220) | XY = max(0, XY - Z) - t, Z = -t with t = (TP+0x80 <= 2 tol) ? -2 tol : TP+0x80 - 2 tol (tol = job +0xa0) | mk l.87537-87573 | - | decoded; TP+0x80 = GetHstCornerRadius (FUN_180096984(TP+0x70), port plan 6.7) |
| StepMachTolerance | job +0xa0 (+ local_848, set only for style 5) | mk l.87612-87616 (local_848: mk l.87400-87407) | cut tolerance | as the other HST ops |
| tool | ToolCornerRadius TP+0x218, ToolCutterDiameter TP+0x210, holder profile via FUN_180074e18 | mk l.87408-87457 (condition l.87408-87411) | 0x358 / 0x350 | style 3 alone passes param_3 = local_250 = 0 -> the full tool branch; the plain-cylinder branch (l.87461-87497) is the Area Roughing + Horizontal case (section 6.2) |

Not set for style 3 (so McHmm's plan defaults apply): Stepdown, StepDownEvenLastSteps, AdaptiveStepdown, RadPushUp / RadPushUpLimit
(the style-3 branch returns at mk l.88061-88064). Plan-specific defaults for 0x143a in FUN_18030b160 (hp l.1-678, switch on the plan
type): StepDownInflexionTolerance = **0.0001** (hp l.533-570; other plans min(0.01, Stepdown x 0.01)), ..ToleranceAvoidance and
..ToleranceBottom = 0.5 x that (hp l.579-590); ProfileRemoveUndercuts (0x1bd5) = the constant at DAT_1809bf1c8 for 0x143a, else
ProfileSmoothing (hp l.1036-1046; the constant's value NOT DECODED). PosZHi / PosZLo: style 3 is **not** in the mask 0x5b6, so PosZLo is
not lowered by the corner radius (mk l.91790-91795); floor stock and TP+0x220 as port plan 6.7.

Axial Offset parameters (plan builder, mk l.93466-93500): **NumberOfOffsets = TP+0x44c** (int), **Stepdown = TP+0x310**.
- TP+0x310 = record **0x13b8** [inferred: shift 0x10a8, the same block as 0x13c0 / 0x13c8 = TP+0x318 / 0x320]; page 1327 writes the
  "Depth cuts / Stepdown" edit 1304 to op +0x3440 = record 0x13b8 (local op = record + 0x2088; UpdateLocalOp FUN_18008c460 ms l.91843-91869: l.91852).
- TP+0x44c = record **0x1558** (int) [inferred from the page]: the page shows "Number of depth cuts" n = op +0x35e0 + 1 and writes
  op +0x35e0 = n - 1 (MapToDlg FUN_18008bc90 ms l.91544, UpdateLocalOp ms l.91851); op +0x35e0 = record 0x1558. The Stepdown edit is
  enabled only for n > 1 (ms l.91551-91553, FUN_18008bfe0 l.91639-91655) - the same condition as NumberOfOffsets >= 1 in mk l.93456.

Defaults (Mill_MM.mcam-defaults op 69 / Mill_Inch op 71, the first op 132 with 0x16bc = 4; read with tools/mcam_ops.py, no NCI):

| field | MM | Inch | | field | MM | Inch |
|---|---|---|---|---|---|---|
| 0x12b8 Minimum | 4.95 | 0.12375 | | 0x13b8 depth-cut stepdown | 1.0 | 0.05 |
| 0x12c0 Maximum | 9.0 | 0.225 | | 0x1558 depth cuts - 1 | 1 | 1 |
| 0x1360 % of diameter | 45 | 45 | | 0x13c0 / 0x13c8 stock XY / Z | 1 / 1 | 0.05 / 0.05 |
| 0x16c3 Corner rounding | 0 | 0 | | 0x1270 / 0x1278 / 0x12c8 | 0.2 / 1.0 / 0.3 | 0.005 / 0.01 / 0.0075 |
| 0x16e4 Cut method | 0 (Climb) | 0 | | 0x12a0 tolerance | 0.025 | 0.001 |
| 0x16cd / 0x1308 / 0x1310 keep tool down | 1 / 12 / 100 | 1 / 0.5 / 100 | | 0x2f8 tip compensation | 1 | 1 |
| 0x16d0 entry helix radio | 1 | 1 | | 0x1328 / 0x1330 / 0x1338 / 0x1340 | 10 / 3 / 0.0349 / 20 | 0.25 / 0.125 / 0.0349 / 0.5 |
| 0x350 D / 0x358 r | 20 / 0 | 0.5 / 0 | | 0x1228 stepdown (not used by style 3) | 2.0 | 0.05 |

So the default op makes **2 cuts per flat** (NumberOfOffsets 1, i.e. the Axial Offset plan is on by default) [inferred via 0x1558].

## 2. Flat levels (FUN_180411bd0, hz l.15258-15552)

### 2.1 Decoded
1. Inputs: HorizontalInflexionGradient (0x1c68), PosZHi (0x1ac8), PosZLo (0x1ac7) (hz l.15315-15327). Z range [PosZLo - c, PosZHi - c],
   c = Eva+0x140 (NOT DECODED, as port plan 4.4); XY box = the model box (Eva+0x118 +0x18..+0x30) +- 10 (hz l.15328-15336).
2. `FUN_1804a1b80(CZInflexions, xbox, ybox, zrange, 0, -1.0, gradient)` (gB l.100-163: stores the box, offset 0, gradient), vtable
   replaced by CZInflexionsHorizontal (vftable 180763450: only a dtor and the shared slot [1] FUN_180006680); the model (Eva+0x498) is fed
   through FUN_1801e2690 -> FUN_1801e39b0 (the box-set visitor, gB l.1-38). FUN_180256b40 copies the result into a vector of 16-byte
   (z_lo, z_hi) bands, FUN_180098d40 a vector of doubles = their areas (gB l.40-98).
3. Merge (hz l.15345-15405), repeated until nothing merges. Scanning i from the last band down to 1, with gap_i = band[i].z_lo -
   band[i-1].z_hi:
   - if gap_i <= 0 (band i does not start above band i-1) band i is merged at once;
   - otherwise the candidate is the i with the smallest gap_i < **HorizontalZEpsilonMerge** (0x1c6e) (a later, lower i replaces it only when
     its gap is strictly smaller).
   Merging i into i-1: band[i-1].z_hi = band[i].z_hi, area[i-1] += area[i], band i removed.
4. Keep (hz l.15406-15510; append at l.15483): area >= **HorizontalFlatAreaSize**^2 (0x1c69), logged "HozLevel detected between z_lo, z_hi with area A".
   Kept bands are appended to Eva+0x1958 as (z_lo + c, z_hi + c), from the last band to the first. The merge compares band[i] with
   band[i-1] as if the bands were sorted by ascending Z, so the list is highest flat first [inferred].

### 2.2 The triangle -> band rule (DECODED 2026-10-26, was D1)
Source: **tp** = decomp/hst3d_triplane.c (the CZInflexionsHorizontal range of class_addr_map.txt; its dtor FUN_18003dac0 tp l.1157-1177 frees
the area vector at +0x80, which only FUN_18003db30 fills). Per triangle A, B, C of the model (the box-set visitor of 2.1.2):
1. **FUN_18003dd10** (tp l.1282-1404): the triangle's box must overlap the grid box (x +0x10..0x18, y +0x20..0x28, z +0x30..0x38 = the
   [PosZLo - c, PosZHi - c] range of 2.1.1; tp l.1355-1362). With nz = (Bx-Ax)(Cy-Ay) - (By-Ay)(Cx-Ax), n1 = (Bx-Ax)(Cz-Az) - (Bz-Az)(Cx-Ax),
   n2 = (By-Ay)(Cz-Az) - (Bz-Az)(Cy-Ay) (tp l.1363-1368): the triangle is a flat one when **gradient > 0 and n1^2 + n2^2 <= (nz^2 + n1^2 +
   n2^2) x gradient^2** (tp l.1369), i.e. the normal is within asin(gradient) of vertical - **either way up** (only nz^2 is used: a downward
   face gives a band too). Its band = (min, max) of the three vertex Z, its area = **|n| / 2, the 3D triangle area** (tp l.1370-1386).
2. **FUN_18003db30** (tp l.1179-1280): the bands (+0x58, 16-byte (z_lo, z_hi)) are kept **sorted by z_hi**; pos = the first band with
   z_hi >= the new z_hi (binary search), one back when the band before has exactly the same z_hi (FUN_1800082c0 = exact equality,
   decomp/hmm_vor1.c). From pos on, the **narrowest band with z_lo <= the new z_lo** (so it contains the new range) gets the area added
   (+0x80[i] += area, tp l.1268-1272); if there is none, the new band is inserted at pos with its area (FUN_18003dfb0 / FUN_1800194a0).
So a planar horizontal face gives one band (z, z) with the face's area; slanted faces are never flats (gradient 0.0001). Ported as
viewer/cam3d_horiz.c `horiz_flats`.

### 2.3 McHmm defaults (FUN_180313650, found with FindImm on the parameter ids; gA l.1-127)

| id | parameter | default expression | gA line |
|---|---|---|---|
| 0x1c68 | HorizontalInflexionGradient | 0.0001 | l.30-36 |
| 0x1c69 | HorizontalFlatAreaSize | 1.0 (DDV_MinDistance) -> area threshold 1.0^2; McHmm works in mm (area_mill.md 11.2), so 1 mm^2 [inferred for inch parts] | l.39-44 |
| 0x1df7 | HorizontalZEpsilonTolerance | 0.6 x StepMachTolerance (validated 0 .. StepMachTolerance) | l.47-54 |
| 0x1c6f | HorizontalZEpsilonAbove | HorizontalZEpsilonTolerance | l.60-64 |
| 0x1c70 | HorizontalZEpsilonBelow | HorizontalZEpsilonTolerance | l.67-71 |
| 0x1c6e | HorizontalZEpsilonMerge | 0.75 x (Above + Below) | l.74-84 |
| 0x1c71 | HorizontalWallEdgeOffsetOut | 1.0 | l.92-97 |
| 0x1c72 | HorizontalTipEdgeOffsetIn | `WeaveFlatTip ? -MachWeaveResolution - min(0.5, WeaveCutterRadius * 0.8) : VorProfileFlatRad + 1.0` | l.100-113 |

(WeaveFlatTip / VorProfileFlatRad / WeaveCutterRadius / MachWeaveResolution as port plan 4.5.) mckernel sets none of these for style 3
(apart from the dead TipEdgeOffsetIn override, section 1). Example, D 20 flat, stock 1 / 1, tol 0.025: Above = Below = 0.015,
Merge = 0.0225, TipEdgeOffsetIn = -(MachWeaveResolution) - 0.5.

## 3. The region of one flat and its passes

### 3.1 FUN_1804109a0(this, progress, param_3 = 0) (hz l.14719-15256)
1. The slice object Eva+0x1430 = the one in the list Eva+0x1080 with the largest +0x1c8 (hz l.14799-14812). Its flat index +0x2e0 picks
   level (z_lo, z_hi) = Eva+0x1958[index]; when index = the number of flats the slice is removed (Eva+0x1088 -= 1) and the call returns
   false - FUN_18040fb90 loops while Eva+0x1088 != 0 (hz l.14313 / 14700). +0x2b8 = (index == last). Where the index is advanced is NOT
   DECODED.
2. tol = StepDownInflexionTolerance (0x1bbe), avoid = ..ToleranceAvoidance (0x1bbf), grad = StepDownInflexionGradient (0x1bbd) for
   param_3 = 0 (the StepUp* trio 0x1ec4 / 0x1ec5 / 0x1ec3 for OptiRough's step-ups, param_3 = 1) (hz l.14837-14874).
3. Weave margin: h = model+0x428 - ((z_lo - Below) - tol); r = h > 0 ? r(h) (FUN_18013fff0, the tool radius at height h, port plan 4.5)
   : 0; FUN_180271070(weave, max(r, profile first field)) (hz l.14876-14895).
4. CZInflexions over [z_lo - Below - tol, z_hi + Above + tol] (FUN_1804a1b80 / FUN_1801e25c0, hz l.14896-14911), then two heights chosen
   by FUN_1804a1e10 (CZInflexions, hpl l.1837-1909: FUN_18005ef10 with a FUN_18005f7e0 fallback, not followed):
   - **zA in [z_hi + Above, z_hi + Above + tol]** (hz l.14912-14923) - the flat's cutting height: slice +0x2c8 = zA (hz l.15208), logged
     "non-adaptive Stepdown (top - zA) to zA";
   - **zB in [z_lo - Below - tol, z_lo - Below]** (hz l.14924-14934).
   Which point of the window is returned is NOT DECODED; the window is tol = 0.0001 wide (section 1), so either end is within 1e-4.
5. Weave 1 (the slice's, +0x10) at Z = zA - c, fed with the model (FUN_1801e2fd0, hz l.14945-14950); copied to Eva+0x1090 and to Eva+0x1260;
   weave 2 = Eva+0x1260 at Z = zB - c (Eva+0x13b8), fed again (hz l.14951-14962).
6. FUN_1804031a0(this, 0, zA - zB, zB + 10) (hz l.14963; body hz l.6173-6563) walks every loop of weave 2 and classifies each loop point
   with FUN_1804039a0 (hz l.6565-6641): the contact that ends the fibre interval at that point has height h above the weave (FUN_180253690
   record +0x10); **h < zA - zB -> "tip edge"** (the tool touches the model inside the flat's band), else "wall" (with a profile of type 1
   the height is re-read through the tool profile, hz l.6615-6634). Runs of tip-edge points go to the polyline set Eva+0x15e0, runs of
   wall points to Eva+0x1760 (FUN_1801192a0 / FUN_1801194d0 with the flag, then FUN_180121900; hz l.6380-6520).
7. **HorizontalWallEdgeOffsetOut** != 0 (hz l.14965-15073): the wall runs (FUN_18011cbc0 on Eva+0x1760) are pushed into weave 2 with a
   disc tool of that radius (profile FUN_18013e0f0(value), tbb loop FUN_1804123e0 -> FUN_180412ea0 -> FUN_180318bf0(Eva+0x1260, profile,
   polyline), hz l.16050-16104), after the weave's ends are re-closed (FUN_180010e30 x2, FUN_18012d020).
   **HorizontalTipEdgeOffsetIn** != 0 (hz l.15075-15193): the tip-edge runs (Eva+0x15e0) are pushed the same way with radius
   |TipEdgeOffsetIn| (`& 0x7fffffffffffffff`, hz l.15138) through FUN_180412520 (a different lambda); the weave is re-closed first when
   (TipEdgeOffsetIn > 0) equals "the wall push ran" (hz l.15083-15088). How a negative value differs from a positive one in the push is NOT
   DECODED (lambda edee9... not followed).
8. The slice's fibres become weave 2's: slice +0x20 / +0x68 = Eva+0x1270 / +0x12b8 (FUN_180132160, hz l.15196-15207); +0x2d8 = z_hi.
   FUN_180271330 adds the stock model when Eva+0x1000 / +0x1030 (hz l.15235-15245).

### 3.2 The passes (FUN_18040fb90 after FUN_1804109a0, hz l.14324-14686)
1. `FUN_1803faee0(this, 4)`: DoPockets on the slice (= weave 2 + the edge pushes): per pocket slice FUN_18040b6f0, the Voronoi diagram
   FUN_180407c10, per region FUN_180406b00 with kind 4 = the Area Clearance positions (rule 11.3: first pass at t = 0 = the region boundary,
   then MinOffset / RadExtra = MaxOffset - MinOffset (default FUN_180312970 hp l.1338-1344) / tau = CornerOffsetTolerance when
   ProfileSmoothing). Each region record (0x1a0 bytes) becomes a database entity (FUN_18026fec0 with Eva+0xff0 = 1 -> FUN_1804a9d80, in the
   CTessToolpathHorizontalArea range of class_addr_map.txt [inferred]) and is kept in a list (FUN_180404cd0) (hz l.14326-14420).
2. The slice is reset to weave 1 (zA): FUN_180270fe0 + FUN_18012c0f0(slice, Eva+0x1090), FUN_180271330 (stock model), then
   `FUN_1803faee0(this, 1)` - kind 1: only the boundary loops of the free tool-centre regions at zA (MinOffset = RadExtra = 0, rule 11.3.1);
   each becomes an entity (FUN_18026fb30, hz l.14422-14488).
3. For each kind-4 region: MeasureInterior2D (initial values 1000.0, 1e6, the region's point at record +0x190, -1.0; FUN_180249cc0 over the
   kind-1 region's loops of type DAT_1809a32e0 / DAT_1809a32e4; the sign flipped by the crossing parity) picks the kind-1 region with the
   smallest measure; the kind-4 region is linked under it (FUN_18026fd30 parent +0x108, its id inserted into the sorted set +0x48,
   FUN_18000d400 appends its passes to +0x68) (hz l.14523-14680). Asserts "EvaHorizontalAreaClearancePlan.cpp" line 0xa0 when there is no
   kind-1 region.
4. FUN_1803ffdb0(this, 1) (ordering, NOT DECODED, as port plan 4.6), FUN_1803fc9b0 (NOT DECODED).

**What region kind 4 clears: NOT DECODED.** Decoded facts: the pocket input is weave 2 (just below the flat, so the flat's own triangles
are above it and block the tool there), changed only along its loop runs by the two edge pushes; its passes are then grouped under the free
regions at zA (just above the flat). The reading "the tool-centre positions at zA that are blocked at zB, i.e. where the tool rests on the
flat" fits these facts but is **inferred**, not read from FUN_18040b6f0 (which turns the weave into the pocket slices) - item D2.

### 3.3 Axial Offset (NumberOfOffsets >= 1): CAxiallyOffsetPassesPlan / EvaAxiallyOffsetPassesPlan
- Plan ctor FUN_18031eb70 (gA l.299-420): defaults Stepdown = `ToolCornerRadius == 0.0 ? 1.0 : ToolCornerRadius / (ToolIsBallNosed ? 5.0
  : 3.0)` (l.371-387), NumberOfOffsets = 1 (l.396-402), **ByOffset** (0x1ee1) = `PlanTypeNumber(MachiningPassesIn) != 0x147a` (l.404-414) -
  true for a Horizontal Area input. mckernel overrides Stepdown / NumberOfOffsets (section 1).
- Evaluate FUN_180320750 (gA l.129-297): for an input plan of type 0x143a (and 0x1404, 0x1412, 0x1417, 0x1418, 0x1433, 0x146f, 0x1474,
  0x147a) calls FUN_18031f660(this, MachiningPasses, input type != 0x143a) (l.271-283).
- FUN_18031f660 (gA l.422-1083): EnableTrimToBoundaries -> FUN_1802ef450 (l.553-558); N = NumberOfOffsets, s = Stepdown (l.604-611).
  ByOffset false: for i = N .. 0, every pass offset by i x s (l.620-805). **ByOffset true (Horizontal Area): for every pass, i = N .. 0,
  the copy FUN_18031f160(pass, i x s)** (l.880-890), so each flat pass is cut N + 1 times, the i = N copy first, the last at the flat's
  own Z. The direction of the offset (+Z = up) is inside FUN_18031f160 (NOT DECOMPILED) - up is [inferred] from the order (N first,
  0 last) and the page's "Depth cuts".

## 4. Entries / links
- mckernel link parameters for style 3 (FUN_180075b48, shared with style 0, mk l.89454-89631):
  - TP+0x46b == 1: ProfileRampStyle 1, HelixDiameter = 2 x TP+0x2a0 (if > eps, else 6.0 metric / 0.3 inch), MaxRampAngle = deg(TP+0x2b0)
    (else 3), RampHeightOffset = TP+0x2a8 (l.89480-89520); TP+0x46b == 0: ProfileRampStyle 0, MinProfileRampDiameter = TP+0x2b8 (else
    6.0 / 0.3), MaxRampAngle, RampHeightOffset (l.89521-89568). Record [inferred, shift 0x1088, values match the defaults]: 0x1328 helix
    radius, 0x1338 plunge angle (rad), 0x1330 Z clearance, 0x1340 preferred profile length (page 1332 "HST Rough Transitions", res);
    TP+0x46b = 0x16d0 "Entry helix" radio (1 = helix) [inferred, shift 0x1265 as 0x464 / 0x16c9 and 0x468 / 0x16cd].
  - style 3 only: LiftAngle = deg(TP+0x2e8) when TP+0x2e8 >= 0; HorizontalLeadOutRadius = TP+0x2e0 (l.89571-89587). Record fields NOT
    DECODED (shift 0x1088 would give 0x1370 / 0x1368, but 0x1370 holds 2.0 mm / 0.075 in in the defaults - a length, not an angle - so
    that shift does not hold here; no page control found).
  - then (styles 0 and 3): MinProfileDiameter = TP+0x330 (record NOT DECODED); LinkClimbMilling = false when TP+0x47c (record NOT DECODED;
    cam3d_rough.c l.419 takes it as 0x16e4, the Cut method); ProfileLinkRadius = 0.25 x TP+0x230 when TP+0x230 > eps and 0.25 x TP+0x230
    <= 0.5 mm (l.89221) / 0.019685 in (l.89607-89610) (l.89611-89620); LinkRampHeightOffset = TP+0x30 (l.89622-89627). LinkClimbMilling: l.89602-89606.
  - common: LinkMaxStayOnSurface = TP+0x468 == 1 ? FUN_18009698c(TP+0x70) x TP+0x288 / 100 : TP+0x468 == 0 ? TP+0x280 : 2.5 mm / 0.15 in
    (l.89205-89235) = page 1327 "Keep tool down within" 0x16cd / 0x1310 / 0x1308 [inferred, shifts 0x1265 / 0x1088]; LinkSafeZ / Retract /
    trimming as every HST op (l.89157-89311).
- McHmm: CHorizontalAreaLinkingPlan (vftable 180798f98, type 0x144c, hl l.17669); the linker maps a 0x143a input to category 3
  (FUN_1803ab250 hl l.12905-12990) and treats 0x144c with the roughing linkers 0x1461 / 0x141d (FUN_1803a9720 hl l.12339). The general
  linker is NOT DECODED (port plan 2, area_mill.md 11.7.2). Defaults in FUN_1803ba250 / FUN_1803ba8f0 (gA l.1104-1407; which plan class they
  belong to is NOT DETERMINED).

## 5. Dialog page 1327 "HST Horizontal Area Parameters" (HSTHorizAreaParamPage)
Sources: controls res (dialog 1327); ctor FUN_18008b900 ms l.91355, DDX FUN_18008bb10 ms l.91433-91470 (controls l.91459-91465) (+ the HST base DDX
FUN_18009c580 ms l.102168-102186), MapToDlg FUN_18008bc90 ms l.91502-91550, UpdateLocalOp FUN_18008c460 ms l.91843-91869 (+ base
FUN_18009dc20), OnKillActive check FUN_18008c570 ms l.91886-91935; local op = record + 0x2088.

| control | label (res) | record | rule | source |
|---|---|---|---|---|
| 1239 combo | Cut method | 0x16e4 (u8) | items 192 / 194 (Climb / Conventional, as s1326) added when page +0x1570 = 0; index = (0x16e4 != 0); write 0x16e4 = (index == 1), other: _MCAssert "HSTHorizAreaParamPage::UpdateLocalOp" | ms l.91534-91539, 91554-91558, 91853-91866 |
| 692 combo | Tip compensation | 0x2f8 | base page, as s1326 (items NOT DECODED) | FUN_18009c580 / s1326 row |
| 1302 edit (int) | Number of depth cuts | 0x1558 (int) = n - 1 | n = 0x1558 + 1; Enter / kill-focus FUN_18008bfe0 enables 1303 / 1304 when n > 1 | ms l.91544, 91551-91553, 91639-91655, 91851 |
| 1304 edit | Stepdown | 0x13b8 | shown as max(0x13b8, tol); enabled when n > 1; FUN_18008c240 (ms l.91760-91809) sets the page value to record 0x1228 when the active machine group has +0x11c0 != 0, else from the tool: type 0xb D / 5, 0x13 D / 3, 10 -> 1.0 mm / 0.05 in (when it is called is NOT DECODED) | ms l.91545-91549, 91852 |
| 1263 / 1245 / 1246 | XY stepover % of diameter / Minimum / Maximum | 0x1360 / 0x12b8 / 0x12c0 | base page, as s1326; OnKillActive refuses Minimum < Maximum x 0.5 - tol or Maximum < Minimum - tol (strings 0x1d74 / 0x1d75) | ms l.91920-91931; s1326 rows |
| 25412 group + 1247 / 272 / 275 | Corner rounding: Maximum radius / Profile tolerance / Offset tolerance | 0x16c3 / 0x1278 / 0x1270 / 0x12c8 | base page, as s1326 | FUN_18009c580 |
| 1205 / 1207 radio, 1206 / 1208 | Keep tool down within: Distance / % of tool diameter | 0x16cd / 0x1308 / 0x1310 | base page, as s1326 | s1326 rows |
| 1505 static | (picture) | - | DDX control only | ms l.91465 |

The page has no Stepdown (0x1228), Add cuts or Optimize controls (res). The template XML writer FUN_180391410 (ms l.568763-569040) lists
the same tree: Cut parameters (Depth cuts: Stepdown default `@DIAMETER * 0.10`; XY stepover: Maximum `@DIAMETER * @PERCENT_DIAMETER`,
Minimum `@PERCENT_MAXIMUM * 0.55`; Smoothing: Max radius `@DIAMETER * 0.05`, Profile tolerance `@DIAMETER * 0.01`; Keep tool down
200.0 mm), Transitions (Profile ramp: Preferred profile length `@CORNER_RADIUS * 2`; Entry helix radius) and Linking - the
"Transitions" page is HST Rough Transitions 1332 [inferred from the field set].

## 6. NOT DECODED / open items
- D1: DONE (2.2).
- D2: the kind-4 pocket region: FUN_18040b6f0 (weave -> pocket slices) and the tip-edge push lambda FUN_180412520 (sign handling); the
  wall push's exact effect.
- D3: FUN_1804a1e10's choice inside the [lo, hi] windows (1e-4 wide; negligible) and c = Eva+0x140 (the weave Z offset, also open in port
  plan 4.4).
- D4: where the slice's flat index +0x2e0 advances; FUN_1803ffdb0 / FUN_1803fc9b0 ordering; FragOrderer / the general linker (entries,
  links, retracts) - as all HST ops.
- D5: FUN_18031f160 (the axial offset itself: sign, whether the copy is re-trimmed); EnableTrimToBoundaries' boundaries (FUN_1802ef450).
- D6: record fields of TP+0x2e0 / 0x2e8 / 0x330 / 0x47c / 0x463 / 0x475; ProfileRemoveUndercuts' default constant (DAT_1809bf1c8).
- The TP -> record copy itself (mccore) - every mapping above marked [inferred].

### 6.2 Area Roughing + Horizontal Area (style 0, TP+0x463 set and TP+0x475 = 2)
FUN_18007d7c8 adds "// Horizontal Area Passes Plan" (0x143a, `ekPlan4ah`) after the roughing plan and collects both in a Plan of Plans
before the 2D Stock Model (mk l.93410-93436, 93503-93525); in that case local_250 = 1 and FUN_180071314 gets param_3 = 1 (plain cylinder
tool, mk l.87408-87411 / 87461-87497) and param_5 = -(TP+0x218 + TP+0x1b0 + job +0x10) = -(corner radius + Stepdown + eps) added to PosZLo
(mk l.93097-93102, 93284-93286, 88028-88033). The record fields of TP+0x463 / TP+0x475 are NOT DECODED (port plan 9: NOT DETERMINED).

## 7. Oracle candidates (superseded: an oracle exists, section 9)
- `~/mastercam-re/work/hst3d/netscan/scan3d.tsv` (1460 internet parts): op 132 styles 0 x24, 1 x4, 2 x5, 3 x6, 5 x4, 6 x1, 7 x4 - **no style 4**.
  `~/mastercam-re/work/hst3d/scan.tsv` (the user's / sample parts): no op 132 at all.
- Only the defaults: Mill_MM.mcam-defaults op 69 / Mill_Inch op 71 (style 4, 0 moves) - parameters, no motion.
- So there is no Mastercam oracle; port plan decision 5.1 applies (the user saves a test part with a Horizontal Area op and NCI in
  Mastercam 2017: e.g. a block with a pocket floor, a boss top and a step, D 10 flat + D 10 r 1 bull, depth cuts 1 and 2). Until then
  only analytic self-checks (ours): a pocket floor (region = the floor offset by R + stock from the walls, passes = the 2D Area Clearance
  result), a boss top, two flats closer than HorizontalZEpsilonMerge (merged), a flat below 1 mm^2 (dropped).

## 8. Port outline (in terms of existing code)
New `gen_horizarea` in viewer/cam3d_finish.c beside gen_arearough (same skeleton). Steps marked **ours** need the user's decision
(HARD RULE), or the decode item named.
1. Record (hst_record / c3d / c3b as gen_arearough): Min / Max 0x12b8 / 0x12c0, Corner rounding 0x16c3 (+ 0x1270 / 0x1278 / 0x12c8; refuse
   as gen_arearough does: the smoothing FUN_180557f60 is not ported), stock XY 0x13c0 and **Z = min(0x13c0, 0x13c8)** (section 1 - not
   the max of styles 0 / 1), tol 0x12a0, Use Z depths 0x16c9 / 0x1238 / 0x1240, depth cuts N = int 0x1558 and s = 0x13b8, Cut method
   0x16e4. Refuse the negative-stock branch as gen_arearough.
2. Mesh: c3_mesh_op (deflection 0x12a0). PosZHi / PosZLo exactly as gen_arearough minus the "range below eps" bump (style 0 only); no
   corner-radius lowering (style 3 not in 0x5b6).
3. Tool shape: flat_tip / Rb / Fo / spacing = c3_resolution(...) as gen_arearough; StepMachTolerance = tol; Above = Below = 0.6 tol,
   Merge = 0.75 (Above + Below), area threshold 1 mm^2 x unit^2.
4. Flats: new c3_flats(mesh, PosZLo, PosZHi) -> bands -> merge rule 2.1.3 -> keep rule 2.1.4 -> highest first. The triangle -> band step is
   D1: **ours** until decoded (e.g. triangles whose normal is within HorizontalInflexionGradient of +Z, band = their Z range, area = their
   projected area) - or refuse.
5. Per flat: zA = z_hi + Above, zB = z_lo - Below (window ends, D3 negligible); SA = c3_slice(M, zA, ...), SB = c3_slice(M, zB, ...) (the
   slice's tip-Z convention for c = Eva+0x140 is ours, as in the other ports). Region T: **ours / D2** - the inferred reading T = SB minus SA
   (blocked below, free above, inside the containment C as gen_arearough builds it), with the edge offsets of 3.1.7 (wall runs = parts of
   SB's boundary that are also SA's boundary, grown by HorizontalWallEdgeOffsetOut; tip runs = the rest, moved by
   HorizontalTipEdgeOffsetIn) left out or refused until D2.
6. Passes per region of T: `area_mill_passes(T, Min, Min, Max - Min, tau, arc_tol, unit, ...)` (kind 4 = kind 2, decoded); each region
   grouped under the free region of SA that contains it (3.2.3) - which only matters for ordering.
7. Depth cuts: N >= 1 -> each pass emitted at zA + i x s for i = N .. 0 (3.3, sign [inferred]); N = 0 -> once at zA.
8. Order / entries / links: as gen_arearough (inner passes first, am_ramp on closed passes, SAFE links) - all **ours** (D4); Cut method
   direction as gen_arearough (climb CCW, measured on 2D Area Clearance).
9. Wiring as Area Roughing: CamOpType, cam_cli `horizarea`, the gallery item, page 1327 = an opdlg_fields.tsv block s1327 copied from s1326
   for the base controls + the 3 page-specific rows of section 5; default op = Mill_MM op 69 / Mill_Inch op 71 (cam_fileop.c default_op,
   first op 132 with 0x16bc = 4).

## 9. Second session (2026-10-26): oracle, flat detection port, region analysis

### 9.1 The oracle
The previous scan (section 7) missed it because tools/mcam_walk.py cannot read the file (the XT blob's node layout, 9.4): **Handbook Volume 2
"MCX Files/RASTER.mcam" op 2** (2017; the X9 RASTER.mcx-9 copy is the same op, 21899 NCI points; the 2018 copy has no NCI) is op 132 with
0x16bc = 4. Found with a C-loader scan (viewer/mcam_load.c) of every .mcam / .mcx* / .emcam under <cad-files>, ~/mastercam-re and
<windows> (2120 files, 98 unreadable .emcam): the only other style-4 op is caminstructor "Benchmark 3_0 for 2017.mcam" op 2 (no NCI).
No Area Roughing op with the Horizontal add-on exists (TP+0x463 = FUN_180923880(op +0x494 style, op +0x49d = **record 0x16c5**), decomp/proj/d5.c
l.35-58; TP+0x475 = **record 0x16dc** via FUN_180923f60 decomp/proj/d4.c l.183; HORIZONTAL.mcam op 4 has 0x16dc = 2 but 0x16c5 = 0).
Op 2: inch, tool 5/16 bull r 1/16 (0x350 / 0x358), stock 0 / 0, 0x12a0 = 0.001, Min / Max 0.07734 / 0.14063, 0x1558 = 0 (one cut), Corner
rounding off, Use Z depths off, drive = solid sld_0 (TP kind 4 -> SOLID_ID idn 138).

### 9.2 Measured (NCI of op 2 vs the solid's faces, python XT reader tools/xt_parse.py)
- **Levels**: the NCI cuts at exactly 0, 0.125, 0.45, 0.5, 0.55, 0.65, 0.65744, 1.4 (float NCI) = the Z of every up-facing PLANE face, nothing
  else. **The passes are at the flat's own Z**, not at zA = z_hi + HorizontalZEpsilonAbove (0.0006 in here) - the decompile sets slice +0x168 =
  zA - c and +0x2d8 = z_hi (3.1); which one FUN_180402f40 writes is NOT DECODED, the measured value is z_hi.
- **Wall side**: the outer pass is exactly R + stock from a wall (pocket floor 0.125, wall x = 3.53, pass x = 3.37375 = 3.53 - 0.15625):
  the free region at zA.
- **Open (tip-edge) side**: the outer pass is **0.0454-0.0457 in (1.153-1.161 mm) beyond the flat's edge**, measured at the boss top 1.4
  (flat r 0.57879, fillet r 0.015 + 45 deg chamfer below; pass r 0.6242) and at the 0.125 floor's straight convex edge. The tool blocked just
  below the flat (weave 2 at zB) reaches 0.1024 in beyond the edge, so the region is that boundary moved in by ~1.45 mm.
- **Pass spacing**: concentric passes on the boss at r 0.6242, 0.4836, 0.3430, 0.2024, 0.0618 = MaxOffset steps from the boundary (rule 11.3
  "where the axis rises 1:1 the step is MaxOffset").
- Small flats where the tool does not fit (the 0.125 triangle floors) get no passes; the down-facing bottom face (-0.25) is a band by the
  decoded rule (2.2, nz^2 only) but gets no passes.

### 9.3 The region (D2): what is decoded, what is not
Decoded here (hz l.14963-15207, FUN_180318bf0 decomp/horizarea/g3.c, FUN_180412ea0 hz l.16050-16104):
- weave 2 = a copy of weave 1 (zA) fed again at zB (FUN_18012c0f0 + FUN_1801e2fd0 with Eva+0x13b8 = zB - c, hz l.14952-14962), so B =
  blocked(zB);
- FUN_1804031a0 records **fibre-interval end points (tool-centre positions in weave space)**, not contact points, into the tip / wall run
  polylines (hz l.6340-6420);
- both pushes (wall: lambda 82dc7b8e -> FUN_180412660 -> FUN_180412720; tip: lambda edee9aaf -> FUN_1804126c0 -> FUN_180412a60; vftables
  1807a1140 / 1807a1120, decomp/horizarea/vt_lambda.txt) run the **same body FUN_180412ea0**: FUN_180318bf0(weave 2, disc profile
  FUN_18013e0f0(radius), polyline) = the weave's segment push FUN_1801298f0 / FUN_18012d460 per span, i.e. the disc swept along the run is
  added to the fibre intervals; radius WallEdgeOffsetOut, resp. |TipEdgeOffsetIn|;
- the toggles: FUN_180010e30(fibres, 0) on both fibre sets + FUN_18012d020 before the wall push, before the tip push when (TipEdgeOffsetIn >
  0) == (wall push ran), and at the end when not toggled an odd number of times (hz l.15014, 15083-15089, 15191-15195). FUN_180010e30 runs
  lambda ba51a17d over every CFibreConcurrent with the fibre range (+0x38 / +0x40) - its body (presumably the complement of the intervals
  within the range) is NOT DECOMPILED.
- FUN_18040b6f0 (g3.c) walks the slice's boundary loops and flags each node kept or not by FUN_1801344c0 and, when Eva+0x670, by its contact
  height in [Eva+0x678, +0x680] - the open-pocket mechanism; where +0x670 / +0x678 / +0x680 are set for this plan is NOT DECODED.
NOT DECODED: with the toggle = complement, the pocket is (B minus the wall band) plus the tip band, which grows the region at open edges by
|TipEdgeOffsetIn| (= VorProfileFlatRad + 1.0 mm = 3.38 mm here) - the opposite of the measured ~1.45 mm shrink. So either the toggle is not a
plain complement, or the pockets are trimmed later (kind-1 regions / FUN_1803ffdb0 / FUN_1803fc9b0 / the 0x670 window). The port therefore
**refuses** the op (viewer/cam3d_horiz.c) unless the test hook MCAM_HZ_OPEN=<mm> applies a measured shrink.
**Third pass (2026-10-28, mchmm.dll via ~/mastercam-re/ghidra_hmm, DecompAt / MemDump; output in the session scratch, not committed):**
- **The toggle is a plain complement (DECODED).** FUN_180010e30(fibres, flag) runs tbb parallel_for_each (FUN_1800112c0) whose
  do_iteration_task::execute (vftable 18075fec0 slot 1 = FUN_180011950; the _iter variant 18075fe80 -> FUN_180011f80) calls
  **FUN_180125390(fibre, range, flag)** with range = {fibre +0x38, +0x40, an empty contact}. FUN_180125390: when the first interval
  starts at the range start (FUN_1800082c0, the fibre's equality test) it is removed and the gap starts at its end; every interval
  [a, b] (0x40 bytes: lo, hi, lo contact 0x10-0x1f, hi contact 0x20-0x2f, +0x30 0, +0x38 -1) becomes the gap [previous end, a] with
  the previous end's contact and a's contact; with flag set bit 31 of the contact word +0x28 is cleared; a last gap [last end, range
  end] is appended when last end < range end. So the open-side shrink does not come from the toggle: **the pockets are trimmed
  later** (the remaining candidates below).
- **Eva+0x670 / +0x678 / +0x680 are the contact-angle window, not heights.** Set only in the EvaMachLocBasePlan constructor
  FUN_1802f2340 (the stores at 1802f2eca / 1802f2f73 / 1802f248b-1802f249c), for every plan except 0x140b (Silhouette Boundary):
  +0x670 = 1; +0x678 = cos(ContactAngleHi 0x1ac5) when ContactAngleHi <= DAT_1809bf1a0 - ContactAngleTol (0x1ac6), else -0.1;
  +0x680 = cos(ContactAngleLo 0x1ac4) when ContactAngleTol <= ContactAngleLo, else 1.1; +0x688 / +0x690 the same with Hi + Tol /
  Lo - Tol. DAT_1809bf1a0 is 0 in the image (set at run time; presumably pi / 2: NOT DECODED). With the default 0 / 90 degree
  contact angles the window is [-0.1, 1.1] (or close to it) and keeps every node: **it does not explain the shrink either.**
- Still NOT DECODED: FUN_1803ffdb0 / FUN_1803fc9b0 and the kind-1 regions (hz l.14683-14686), now the only candidates for the
  ~1.45 mm open-side trim and the sliver removal.
- FUN_1803fc9b0 (hz l.1881-2370), first read: region bookkeeping, no offset seen yet - per region of the slice (+0x240 list, 0x18 each)
  the kept bit in the bitmask slice+0x258; when a region is not kept, the kept regions' loops are collected and the two weaves (slice
  +0x18 / +0x60) restricted to them (FUN_180010f20, not read), slice+0x1c8 = the sum of the two results (the measure step 1 picks the
  slice by); without slice+0x278 FUN_180403cb0 / FUN_180404cd0 rebuild +0x1e0 from +0x2a0; FUN_1803fc6f0 only under
  CheckAreaClearanceStockShaft (0x1cc0, a shaft check); at entry FUN_180400fc0 (hz l.4592-5004, calls FUN_180401bd0 / 401960 / 401790 /
  1802ed510 ..., not read) when slice+0x310 == 0 or +0x278. Next: FUN_180010f20, FUN_180400fc0, then FUN_1803ffdb0.
Also seen on the oracle with the inferred T = blocked(zB) - free... (T = B minus blocked(zA)): thin slivers along drafted walls / fillets,
which Mastercam does not cut - consistent with the wall-run band (B minus the 1 mm wall band) of the decoded push.

### 9.4 Drive mesh of the oracle (test infrastructure, ours)
sld_0 is Parasolid 28.0 (SCH_2800143_28002) with CONE / TORUS / SPHERE / OFFSET_SURF / BLENDED_EDGE (154 rolling-ball faces) / INTERSECTION
curves: viewer/occt/xt_occt.cpp refuses it. Two findings for that translator: (1) **'h' fields are 3 doubles (24 bytes) in binary XT**, not
4 (measured: CHART / LIMIT node lengths; with 32 the reader stops at 0xcdac "unsupported node type" - the same error tools/mcam_walk.py
gives); (2) the missing surface types. For the oracle a scratch copy of the translator was extended (cones / tori / spheres / offsets
native OCCT, INTERSECTION = degree-1 B-spline through LIMIT + CHART points, BLENDED_EDGE = a B-spline fitted to spine samples x the ball arc
between the supports' nearest points, planes re-tessellated from their sampled wires, looser edge tolerance, faces that still fail
dropped): 479 / 491 faces built, 444 meshed, 115603 triangles after dropping triangles outside the body box. It is not part of the
product (approximate blends) and is not committed (new-files rule); horiz_test.sh takes its STL as HORIZ_STL.
**Superseded (xtfix, 2026-10-26)**: viewer/occt/xt_occt.cpp now translates and meshes this solid itself ('h' = 24 bytes, cone / sphere /
torus / offset exact, rolling-ball blends and intersection curves fitted to the exact definitions within a checked 1e-5 m, 2 of 492 faces not
meshable and left out) - formats/xt_occt_fixes.md; horiz_test.sh no longer needs HORIZ_STL.

### 9.5 Oracle numbers (tools/tests/horiz_test.sh, RASTER.mcam op 2, inch; the x2s mesh)
- **Flat levels: 8 / 8** Mastercam levels found exactly by `horiz_flats` (0, 0.125, 0.45, 0.5, 0.55, 0.65, 0.65744, 1.4), plus the
  down-facing bottom -0.25 (a band by the decoded rule; Mastercam cuts nothing there) and one mesh artefact (-0.58975, x2s blend).
- Regions + passes (testing only, T = blocked(zB) minus blocked(zA), MCAM_HZ_OPEN = the measured 1.45 mm open-side shrink), sampled
  distances, inch:
  | level | Mastercam -> ours p50 / p95 | ours -> Mastercam p50 / p95 | note |
  |---|---|---|---|
  | 0.125 | 0.0023 / 0.014 | 0.011 / 1.07 | pocket floors: walls exact (R from the wall), inner passes within 0.003; ours adds sliver regions along drafted walls / fillets (9.3) |
  | 0.45 | 0.0064 / 0.038 | 0.023 / 1.43 | ours cuts 58x more length (sliver and over-wide regions)|
  | 1.4 | 0.141 / 0.282 | 0.0017 / 3.5 | boundary pass exact; area_mill_passes on a disc gives only the boundary and the centre (its grid medial axis of a circle is one point) where Mastercam steps by MaxOffset; p95 = stray mesh fragments |
  The other levels were not measured (the test's per-level stats time out on the extra sliver passes).
- Open items (in order): D2 - (the fibre toggle and Eva+0x670..0x690: decoded 2026-10-28, 9.3 "Third pass") FUN_1803ffdb0 / FUN_1803fc9b0
  and the kind-1 regions, to explain the 1.45 mm open-side shrink and the sliver removal; the pass Z (z_hi measured vs zA in the code,
  FUN_180402f40); area_mill_passes on circular regions (viewer/area_mill.c grid medial axis); the XT translator ('h' = 24 bytes and the
  surface types of 9.4) so the port can mesh this part itself; the linker (as every HST op).

### 9.5 The open-side region: DECODED (2026-11-08), oracle RASTER.mcam op 2
The ~1.45 mm "shrink" measured in 9.2 was the sum of three decoded facts, none of them the trim functions FUN_1803ffdb0 / FUN_1803fc9b0 (with param_2 = 1
FUN_1803ffdb0 only builds the (entity, 0) pairs and calls FUN_180400db0, hz l.4440-4456: ordering, no geometry):
1. **StepMachTolerance = 0.5 x the op tolerance**: job +0xa0 = param_1[0x14] = TP+0x220 x 0.5, or the default 0.001 in / 0.025 mm x 0.5 when TP+0x220 <= 1e-12
   (FUN_18006dd68 mk l.84702-84718). So HorizontalZEpsilonAbove / Below = 0.6 x 0.5 x tol (ha 2.3). The port used the full tolerance before. Oracle: tol 0.001 in, Below
   0.0003 in, the corner sag at that height sqrt(2 x 0.0625 x 0.0003) = 0.00612 in.
2. **The weave holds only the ball part of a bull tool.** WeaveCutterRadius = R + stock - VorProfileFlatRad (port plan 4.5); the flat radius is applied later by the Voronoi pass:
   FUN_180407c10 reads Eva +0xff8 (= 0x1bee VorProfileFlatRad, set at hz l.351-355) at hzp l.593-603 and moves the offset radius in by it ("Inflexion offset radius moved in
   from .." log, hzp l.606-625). So the pocket FB of weave 2 lives in ball-tool space and is eroded by the flat radius. The earlier reading compared with a slice that already
   carried the flat radius. FUN_18054a640 (hmm_vorlib.c l.7145) may move the radius further; that adjustment is NOT PORTED (the oracle does not need it).
3. **The tip-edge push is +(VorProfileFlatRad + 1.0) for a bull / ball tool** (ha 2.3, gA l.100-113, in mm). With the complement toggles of 9.3 the tip band is ADDED to the pocket
   when TipEdgeOffsetIn > 0 and the wall band is taken out (hz l.15014, 15083-15089, 15191): three toggles for a positive tip offset, so the pocket is (B - wall band) + tip band.
   A negative tip offset (flat tip, -MachWeaveResolution - min(0.5, WeaveCutterRadius x 0.8)) takes its band out: no oracle for that case (unverified).
Net at an open edge E: E + sag + (Flat + 1 mm) - Flat = **E + sag(Below) + 1 mm**: 0.00612 + 0.03937 = 0.04549 in against the measured 0.0454-0.0457.
Region = ((B_weave - wall band) +/- tip band) eroded by the flat radius, minus the tool-blocked region at zA, intersected with the constraint boundary. Tip / wall runs: an end of a
fibre interval whose contact is above the band (h >= zA - zB) is a wall run (FUN_1804039a0 hz l.6565-6641): a contact above zA also blocks the tool at zA, so the port tests the
boundary point against the zA slice grown by (zA - zB) + the arc tolerance (the polygon form of the decoded test, ours).
**Constraint boundary (decoded here):** with no containment chain FUN_18007d7c8 (mk l.92874-92915) uses the Bounding Box Plan 0x1425 of the drive surfaces as ConstraintBoundaries; for
style 3 no OffsetDistance is set (mk l.92851: 0x209 >> style test + local_24f, and FUN_180071120 mk l.86896 only sets it for style 0). The region is trimmed to the drive's XY box
(the oracle's outer passes lie exactly on the mesh box); with chains the containment offset as page 25624 (as gen_waterline; not checked on an oracle op). S7 (surface_selection.md 8): the chains are every chain of the op, open ones closed (c3_containment), offset by c3_tcb_offset (1e-4 test, warning 295); check surfaces are meshed with the drive (bnci_surface sets 0x172c = 0 for style 4: mcmsurfres 287).

Oracle, tools/tests/horiz_test.sh (the 8 levels of op 2, inch, distances per sample, tolerance 0.001):
| level | Mastercam -> ours p50 / p95 | ours -> Mastercam p50 / p95 |
|---|---|---|
| 0.000 | 0.0273 / 0.0376 | 0.0289 / 0.0430 |
| 0.125 | 0.0014 / 0.0087 | 0.0015 / 0.2617 |
| 0.450 | 0.0034 / 0.0124 | 0.0037 / 0.0174 |
| 0.500 | 0.0018 / 0.0301 | 0.0019 / 0.0307 |
| 0.550 | 0.0017 / 0.0163 | 0.0017 / 0.0371 |
| 0.650 | 0.0013 / 0.0022 | 0.0014 / 0.5061 |
| 0.65744 | 0.0064 / 0.0498 | 0.0068 / 2.2404 |
| 1.400 | 0.0012 / 0.0043 | 0.0013 / 0.0042 |
Open: (a) **extra regions**: seven small pockets of the decoded region get no passes in Mastercam (at 0.125 three, area 0.038 / 0.0006 / ~0 in2; 0.45 one 0.0044; 0.65 two 0.0145 / 0.0063; 0.65744 one 0.024);
they are not separated by size from the kept regions by any rule found (inscribed radius 0.0508 is both a dropped and a kept region at level 0.65): the filter is NOT DECODED, so the port cuts them (the large ours -> Mastercam p95 / max above).
(b) **Level 0's inner passes** differ (p50 0.02-0.03) although its boundaries match exactly: the pass positions on that large region do not follow area_mill_passes there; cause NOT DECODED. (c) links, entries, order: ours as gen_arearough (SAFE links), as every HST op (D4). (d) flat-tip tools, stock != 0 and containment chains: no oracle, unverified.

## 10. N9-15 update (2026-11-09; audit formats/audit_2026-11-09_highspeed_surface2.md)
- **c = Eva+0x140 = ToolThicknessZ** (0x1d20, FUN_1802f4c80, stored at 1802f2cff in FUN_1802f2340, 0 only for plan 0x1439): closes D3 and port plan 4.4. The flats' Z range is [PosZLo - c, PosZHi - c] (hz l.15327); bands are kept as model Z + c (hz l.15470-15510); weaves at zA - c, zB - c (hz l.14945, 14957). Pass Z = slice +0x2d8 = the kept band's z_hi (hz l.15209) -> FUN_180406b00's param_7 -> the pass object's +0x5c0 (hz l.8985). The port now does this; RASTER op 2 (stock 0) is unchanged, stock 0.01 moves every level by 0.01.
- **Depth cuts**: ByOffset = default expression true for 0x143a (gA l.404-414; mckernel does not set it, mk l.93456-93500): each pass N + 1 times before the next (gA l.880-890), raised (FUN_18031f160 -> FUN_1804a7c60 decomp/proj/h3.c l.584 / l.723). The port now cuts in that order.
- FUN_1804a1e10 (the zA window, hpl l.1837): picks through FUN_18005ef10 / FUN_18005f7e0, not read further (1e-4 wide): left.
- The seven extra pockets: FUN_18040b6f0's per-node keep test = FUN_1801344c0 (the contact's tool segment, Eva+0x6e0 / +0x6e4) + the contact-angle window; the run walk starting at g3.c l.480 was not followed to a region drop. Still NOT DECODED.
