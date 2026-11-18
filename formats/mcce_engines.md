# mcce.dll cutting engine — 2D High Speed algorithms (Mastercam 2017), implementation spec

Source: the decompile text only (`decomp/mcce_all.c`, 4831 functions; line numbers refer to that file unless another
file is named). No Ghidra session was used. **Rule followed (no improvisation):** every formula, constant, branch and
ordering rule below cites a function address (FUN_1800xxxxx) and line range. Anything not traced is marked
**NOT DECODED** with the address and a clearly marked *unverified* role. Nothing here is "typical behaviour" filled in.

Offset conventions (used by every section):
- `P+x` / `Parm+x` = `CDynPockParm` offset x (layout in `formats/dynamic.md`). The engine works on a derived copy
  (`CDynPockParmDer`, same layout plus derived fields 0x1b0..0x261, FUN_180069560 l.60203-60268).
- `C+x` / `this+x` = `CCePock` offset x. **C+x = P+(x−8)**: FUN_1800124d0 (l.15620-15636, copy at l.15628) puts the
  parm block at C+8 via FUN_180010ef0 (l.14865-14901). Example: flags P+0x08 = C+0x10; C+0x1f0 = P+0x1e8.
- Flag names (CLIMB_MILL 0x1, BIDIR_MILL 0x4, START_INSIDE 0x10, START_FACING 0x40, CTOUR_ON 0x200000,
  CTOUR_TO_LEFT 0x400000, CTOUR_POCKET 0x800000, MOAT_FIRST 0x10000000, AIR_DWELL 0x40000000, ...) come from mcce's own
  debug dumper FUN_180097640 (l.83640-83775); full table in §3 B.1.

## 0. Which engine runs which style (summary of findings)
| style (op 0x1228) | engine | where |
|---|---|---|
| 0 Dynamic Mill | `MCCECalcDynPock` (§3) | mcmill `CHST2DWorkItemDyn` vtable[1] FUN_1802a5760 → `MCCECalcDynPock` (mcmill_all.c l.461506 function) |
| 1 Area Mill | **not mcce's dynamic engine.** `CHST2DWorkItemStd` vtable[1] = mcmill FUN_1802a5ca0 (mcmill_all.c l.461577ff) writes/renames an `"areaclear2b"` work file (l.461701) and runs FUN_1802ba330 / FUN_1802a1670 (l.458963ff). `MCCECalcStdPock` is never referenced in mcmill_all.c (grep: 0 hits). | **NOT DECODED** (separate mcmill-side engine; follow-up pass) |
| 2 Dynamic Contour | `MCCECalcDynPock` with CTOUR_ON 0x200000: only the **region/material preprocessing** differs (§2 → §3 B.4); motion = §3 | |
| 3 Peel Mill | `MCCECalcDynPock` on boundaries reshaped by `PeelAesthetics`, start selector P+0x0c = 8 (§1); **no peel-specific motion code exists in mcce** | |

## 0.1 Top level (decoded directly; MCCECalcDynPock l.12159-12388, FUN_1800125a0 l.15648-15672, FUN_180011710 l.15111-15199, FUN_18006e620 l.62540-62850)
```
MCCECalcDynPock(parm, bndMotion, bndMaterial, bndThird, outBnds, file, path):        // l.12159
  Der = defaults (FUN_180068ff0); derive(Der, parm) (FUN_180069560)                   // l.12236, 12276
  A = CopyPockBndsToShAr(bndMotion); B = CopyPockBndsToShAr(bndMaterial); T = CopyPockBndsToShAr(bndThird)
  for sh in T: sh+0x50 = 0                                                            // l.12277-12286 (third set: +0x50 = 0, open)
  FUN_180012000(A, chains); FUN_180012000(B, chains)                                  // l.12289-12290
  FUN_180090290(Der, A, B, → M, → AIR)          // preprocessing, §3 B.3 / B.4         // l.12291
  FUN_1800125a0(Der, M, AIR, T, chains, → out, file, path)                           // l.12296
  if outBnds and out non-empty: CopyShArToPockBnds(out, outBnds)                     // l.12300-12302
  debug write FUN_18001eb90 (Motion.geo / Material.geo)                               // l.12303
FUN_1800125a0: construct CCePock (FUN_1800103b0); run FUN_180011710; destruct          // l.15665-15670
FUN_180011710 (run):
  CCetp init (FUN_180085900); clear out list                                          // l.15151-15156
  prepare FUN_1800124d0 (copy parm to C+8, build regions)                             // l.15157
  open output FUN_180085ed0                                                           // l.15159
  while FUN_18002a2a0(C+0x298)  (next region, §3 B.7):  FUN_18006e620(C, cetp, out)   // l.15160-15167
  close FUN_180085ca0; progress 100 (FUN_180114140)                                   // l.15168-15171
FUN_18006e620 (one region):
  state = 0 (local_3e8+4), ramp = 1 (low byte local_3e8), last = -1, i = 0, iProg = 0  // l.62659-62662
  C+0x710 = 0; C+0x718 = C+0x720 = 0; C+0x728 = -1                                    // l.62663-62666
  FUN_180011d50(C+0x730, ...)                                                         // l.62667
  if (C+0x10 & 1) != C+0x1b8: FUN_18006eec0(C)                                        // l.62668-62670 (§3 B.8)
  if FUN_18006e440(C):  if !entry FUN_180070130(C, cetp, ..., &state): goto done      // l.62671-62679 (§3 D.1)
  FUN_18006ef10(C, out)                                                               // l.62680
  if FUN_18006e440(C):
    while material list C+0x420 non-empty:                                            // l.62684-62721
      progress FUN_180114140(C+0x270, FUN_18003d360(C+0x298))
      if cetp+0x20 != last: last = cetp+0x20; iProg = i                               // l.62690-62693
      if iProg + 32 < i: break                    // 32 iterations without new output // l.62694
      if state < 2:  FUN_18006d170(...)           // states 0/1, §3 C.3                // l.62695-62699
      elif state == 2: FUN_18006d330(C, unlimited = (cetp+0x1c == saved), ...); saved = cetp+0x1c   // §3 C.4, l.62701-62707
      elif material non-empty: FUN_18006eec0(C); FUN_180067ce0(C, i <= iProg+16, ...); ramp = 1   // link, §3 D.2, l.62709-62718
      i++
    FUN_1800688e0(C, cetp, ...)                   // final exit + lift (NOT a clean-up pass), §3 D.3   // l.62722
```
(The ring/record counter semantics of cetp+0x1c / +0x20 and the "new record flagged 0x40" reading of the guard are
given in §3 C.)


---------------------------------------------------------------------------------------------------------------------

## 1. Peel Mill (2D HS style 3) — mcce side

Sources: `PeelMotMat.cpp` (catch funclets 0x180175e74/0x180175efe return into FUN_1800e2650 @ 0x1800e276d),
`PeelAesthetics.cpp` (catch funclets 0x180175c18/0x180175ca2 return into FUN_1800df710 @ 0x1800df863).
Line numbers are `decomp/mcce_all.c` unless a file is named.

**Main finding: mcce has no separate peel motion algorithm.** The cutting motion for Peel is the ordinary
`MCCECalcDynPock` engine. Peel-specific mcce code does three things:
1. `PeelMotMat` (FUN_1800e2650) builds the Motion and Material shapes from a `CPeelMotMatParm`. mcmill 2017 never
   references it: `grep -i "motmat|CPeelMot"` finds nothing in `decomp/mcmill_all.c`.
2. `PeelAesthetics` (export 0x180005b64, real body FUN_1800df710) reshapes the two pock_bound lists that mcmill
   hands it. It is called once, from `CHST2DProcess::PreparePockBoundsPeel` (decomp/mcmill_hst2d.c:8717).
3. The engine's only peel-specific input is `CDynPockParm+0x0c = 8` ("explicit start point" selector) together
   with the point in `+0x10/+0x18`. FUN_180076500 is the only reader (see 3 below).

Naming (from code): `MCCECalcDynPock` (12159-12388) passes its 1st shape set to the debug writer FUN_18001eb90 as
`%04i_Motion.geo` and the 2nd as `%04i_Material.geo` (22080-22088). So the engine's inputs are
(Motion = where the tool centre may go, Material = what to remove), and "MotMat" means Motion+Material.

### 1. PeelMotMat — FUN_1800e2650 (122745-122834)
Signature (from use): `int PeelMotMat(const CPeelMotMatParm& p, pock_bound*& out1, pock_bound*& out2)`.
- It sets both outputs to NULL (122805-122806).
- It copies `p` into a local `CPeelMotMatParmDer` with FUN_1800e3150 (122807).
- It calls `ok = FUN_1800e28c0(der, &veA, &veB)` (122808).
- If ok, it converts the two lists with `CopyShArToPockBnds` (122810-122811).
- It always returns 0 (122833).

**CPeelMotMatParm layout** (copy ctor 855-877, operator= 4564-4585, Clear 12010-12029, size 0x48 + vtable). Every use
inside the PeelMotMat closure:
| off | type | use |
|---|---|---|
| +0x08 | pock_bound* | input chain(s). Converted to CSh with `CopyPockBndsToShAr` (122898). The first entity's start point is copied to Der+0x58 (123178-123205). |
| +0x10 | double | width. Used as `|w|` in FUN_1800e2c40 (123052-123058). |
| +0x18 | int | side mode 0/1/2 (FUN_1800e2c40, 123051-123061). |
| +0x20 | double | copied only (123152). No other use in the closure. |
| +0x28 | double | tool radius R: `GetMinRadK(R)` = 0.05·R (213-218), and 0.45·R (123173). |
| +0x30 | double | min path radius. Registry override in FUN_1800df9f0 (see 2). Floored to 0.05·R (123158-123163). |
| +0x38, +0x40 | double | copied only (123156-123157). |
| Der +0x48 | bool | registry `bIsVarRadAvail` (default 1). If the registry is unavailable it is 1 (123164-123172). |
| Der +0x50 | double | 0.45·R. Replaced by +0x30 if 0.45·R < +0x30 or if +0x48 == 0 (123173-123177). Not read later in the closure. |
| Der +0x58/+0x60 | CPt2 | start point of the first entity of the first chain (123178-123206). See the entity start formula below. |

Entity start formula: 'L' → e1 (+0x18/+0x20). 'A' → (cx + r·cos sa, cy + r·sin sa) (+0x18,+0x20,+0x28,+0x30).
'P' → pt. Any other id → (0,0).

**FUN_1800e28c0 (122837-122966) — top level**
```
clear(out1); clear(out2)                                  // 122888-122897
sh = CopyPockBndsToShAr(p.chain)                          // 122898
if (!SideOffsets(p, sh)) return 0                         // FUN_1800e2c40, 122900
loops = FUN_1800942b0(1e-8, 1e-8, sh)                     // 122907
if (count(loops) != 1 || loops[0].n_ents == 0) return 0   // 122908-122910
if (!FindGaps(sh, loops, gaps)) return 0                  // FUN_1800e34e0, 122911
PickStartGap(der, loops, gaps)                            // FUN_1800e3370, 122913
return 1
```
**Unresolved: the outputs are never filled.** The decompile clears `out1`/`out2` (param_2/param_3, 122888-122897)
but never writes them afterwards. `sh`, `loops` and `gaps` are locals that are freed on return (122921-122964). As
decompiled, PeelMotMat therefore always returns empty lists. Either an inlined vector move was lost by the
decompiler or the export is a stub. This needs checking in the disassembly. Mark: **NOT DECODED (output wiring)**.
(Re-checked in §6.6.1: still unwired as decompiled; an unused local `local_50` is built and destroyed.)

**SideOffsets — FUN_1800e2c40 (122969-123110)**
```
if (n(sh) > 2) erase sh[2..]                                         // 123009-123017
if (n(sh) == 2) {                                                    // two chains given
    MCCECalcContourOffset(1, 0.0, sh, tmp, 0x40127, 0.0, 1e-6, 1e-6, 1e-6)   // 123019-123021
    if (n(tmp) > 2) erase tmp[2..]                                   // 123022-123029
    if (n(tmp) == 2) sh = tmp                                        // 123030-123046
    return 1                                                         // 123047 (also when tmp != 2)
}
if (n(sh) == 0) return 0                                             // 123050, 123086
w = |p.+0x10|
if      (p.+0x18 == 2) { dNeg = w;     dPos = 0;     }               // 123051-123052
else if (p.+0x18 == 1) { dNeg = 0;     dPos = w;     }               // 123055-123056
else                   { dNeg = w/2;   dPos = w/2;   }               // 123057-123059
MCCECalcContourOffset(1, -dNeg, sh, offN, 0x40127, 0.0, 1e-6,1e-6,1e-6)    // 123062-123064
MCCECalcContourOffset(1, +dPos, sh, offP, 0x40127, 0.0, 1e-6,1e-6,1e-6)    // 123065-123067
if (n(offN) == 0 || n(offP) == 0) return 0                           // 123068-123069
sh = { offN[0], offP[0] }                                            // 123070-123081 (resize to 2, swap pointers)
return 1
```
- An offset of 0 is a valid call: side mode 1 keeps the chain itself as `offN`, side mode 2 keeps it as `offP`.
- `MCCECalcContourOffset` (export 0x180005272, 8571+) is a geometry kernel: (int, dist, in, out, flags, d, tol, tol,
  tol). Its internals and the meaning of flags 0x40127 are **NOT DECODED** (still so after §6; see §6.9).

**FUN_1800942b0 (81801-81977)** — **NOT DECODED** in this pass; **decoded in §6.6.2: QuickHull convex hull of all
shape vertices** (the role guess below is superseded).
- Inputs: 1e-8, 1e-8 and the two side curves. Output: a shape list.
- FUN_180095100 builds a `CVe<PTA>` of entity endpoints over all shapes (82336-82420).
- FUN_180094840 links them into a list.
- FUN_180095ae0 walks the list.
- FUN_180094d30 emits the shapes.
- Unverified role guess: joins the two offset curves end-to-end into one closed loop. The caller needs exactly one
  output shape (122908).

**FindGaps — FUN_1800e34e0 (123265-123659)**, a = the 2 side curves, loop = the 1 joined shape:
```
require n(loop)==1 && n(a)==2                                        // 123390-123392
gaps = []                                                            // CGapH records, 0x50 bytes
for e in loop[0].entities (index k):
    m = midpoint(e)            // 'L': (e1+e2)/2; 'A': angle sa+sw/2 on the circle; 'P': pt   (123402-123440)
    near = FUN_18013cfe0(m, a) // nearest point on any shape of a: {int shape, int ent, double t, CPt2 pt} (170368-170444)
    if |m - near.pt|^2 > 1e-12:                                      // 123444: entity is not on a side curve
        s = nearest(a, start(e)); t = nearest(a, end(e))             // 123446-123531
        if s.shape != t.shape:                                       // 123532: entity connects the two curves
            gaps.push({ k, len(e), s, t })                           // 123533-123564; len: L chord, A |r·sw|
if n(gaps) <= 1: return 0                                            // 123576, 123646
for i in 0..n-1, prev = (i-1) mod n:                                 // 123580-123599
    if gaps[prev].t.shape == gaps[i].s.shape &&
       (gaps[prev].t.ent > gaps[i].s.ent ||
        (== && gaps[prev].t.t > gaps[i].s.t)):
        RevSh(a[gaps[i].s.shape])                                    // 123592
sort gaps by len descending                                          // FUN_1800e1080 (121698) = introsort;
                                                                     // insertion step FUN_1800e0600 121210-121227
out_idx = first 2 gaps' entity index k                               // 123606-123641 (stops at byte offset 0xa0 = 2 records)
return 1
```
**PickStartGap — FUN_1800e3370 (123211-123262).** For each kept gap, it measures the squared distance from Der+0x58
(the chain start) to the nearest point of that loop entity (`GetNearestEntPt`). The nearest gap is swapped to index
0. The initial best value is 3.4028234663852886e38 (FLT_MAX).

### 2. PeelAesthetics — export 0x180005b64 = FUN_1800df710 (120596-120688)
Call (decomp/mcmill_hst2d.c:8716-8720):

`PeelAesthetics(R = op[0x2e8]·0.5, mr = op[0x2e8]·op[0x15f0]·0.01, P = op[0x14e0], &A = this+0x328, &B = this+0x348)`

- op[0x14e0] is set to the start point of the first entity of the first peel chain (mcmill_hst2d.c:7063-7081). It may
  be reassigned at 7150.
- this+0x348 is built at mcmill_hst2d.c:7635 and this+0x328 at 8242. Both are passed on to PackageMachiningRegion
  (5157-5162).
- Which of them becomes Motion and which becomes Material in the engine call is **NOT DECODED** (mcmill side).
- op 0x15f0 is a percentage of op[0x2e8]. It is not named in formats/dynamic.md.

```
FUN_1800df710(R, mr, P, A**, B**):
  if (!reg "bIsPeelAestheticsAvail" (default 1)) return 0             // FUN_1800df360, 120467-120488
  if (reg "bOverrideMinPathRad" (default 0)) mr = reg "dMinPathRadPctToolDia"(2.5)·R·0.02   // FUN_1800df9f0 120705-120714
  if (mr < 0.05·R) mr = 0.05·R                                        // 120641-120648 (GetMinRadK)
  a = CSh(A); b = CSh(B)                                              // 120649-120650
  if (FUN_1800df410(R, mr, P, a, b)) { A = PockBnds(a); B = PockBnds(b) }   // 120651-120663
  return 0                                                            // 120687
```
**FUN_1800df410 (120491-120593)**
```
require n(a)==1 && n(b)==1                                           // 120524-120526
a' = copy(a); if a'[0].dir(+0x50) != 1: RevSh                        // 120527-120530
b' = copy(b); if b'[0].dir != 1: RevSh                               // 120531-120534
if (!FUN_1800dfaf0(R, mr, P, a', b')) return 0                       // 120536-120538 (step 1: rewrites a')
a = a'                                                               // 120539-120541
if (FUN_1800dc820(R, mr, P, a', b')) b = result                      // 120542-120562 (step 2: rewrites b)
return 1
```
**Common helpers (all decoded):**
- `Isect(X, Y, out)` = FUN_180134270 (165674-165779). Collects every intersection between each shape of list X and
  each shape of list Y into `CVe<ISAB>`, then qsorts it with comparator LAB_1800074dc (**NOT DECODED**: the sort
  key) and post-processes it with FUN_1801349c0 (**NOT DECODED**).
  - ISAB is 0x50 bytes: +0x10 X shape, +0x14 X entity, +0x18 X t; +0x20 Y shape, +0x24 Y entity, +0x28 Y t;
    +0x30/+0x34 indices rebased into the list (165728-165734); +0x38 class (set below).
- `Classify(A, B, isab)` = FUN_1800de890 (120109-120170). For each consecutive pair i → j=(i+1) mod n, it tests the
  B section from (ent,t)_i to (ent,t)_j against A with FUN_1801130f0(..., A, tol 0.001):
  - result 2 (section inside a non-hole shape of A, 148390-148438) → isab[i].+0x38 = 3, otherwise 1;
  - it succeeds iff exactly 2 sections are inside.
- `IsectInside(A, B)` = FUN_1800de670 (120039-120106). Runs `Isect(B, A)`, then `Classify`. On failure it replaces
  B with `MCCECalcOffset(1, 1e-4, B, 0x100, 0.0, 1e-6, 1e-6)` (120069-120071) and retries once.
- `SplitInside(Pt, B, isab, &iNear, &iOther, nearPiece, otherPiece)` = FUN_1800dea80 (120173-120392).
  - For every ISAB with class 3 it cuts the section of B from isab[i] to isab[(i+1) mod n] (FUN_180146e20).
  - It requires exactly 2 pieces (120353).
  - The piece nearest to Pt (FUN_18013d010, nearest over shapes) goes to `nearPiece` with its ISAB index `iNear`.
    The other piece goes to `otherPiece` / `iOther` (120354-120364).
- FUN_180146e20 (175xxx, 343 lines) copies the section of a shape between (ent,t)→(ent,t) and appends it.
  **Internals NOT DECODED**, including the wrap and direction rules.
- `Retrace(sh)` = FUN_18011d800 (153632-153871). Appends to the entity list its own reverse: entities in reverse
  order, 'L' endpoints swapped, 'A' sa += sw and sw = −sw. Sets dir = −1. The open piece becomes a closed
  there-and-back loop.
- `PointAt(dist, sh, from)` = FUN_18006f1c0 (62971-63070). Walks `dist` forward from position {shape, ent, t} using
  entity lengths (L chord, A |r·sw|). A closed shape (+0x50 != 0) wraps. On an open shape it fails if it overruns by
  more than 1e-16.
  - FUN_18006f4f0 (63073-63155) is the same walk backwards.
- `Bez(P0..P3, t)` = FUN_1800aa8d0 (92765-92790): cubic de Casteljau.
- `AppendBezier(tol, P, sh)` = FUN_1800a5b00 (90456-90566). Fits one entity to the cubic (FUN_1800aafd0, **NOT
  DECODED**) and checks it (FUN_1800aca30, **NOT DECODED**) against tol. On failure it splits the cubic at t=0.5
  (FUN_1800ae060) and retries; it keeps splitting until every piece passes.
- `Inside(sh, tol, pt)` = FUN_180112a00 (148133-148165). Over shapes: PtInSh==1 → 1. PtInSh==2 → 4 for a hole
  (+0x54 bit 0), else 2. No hit → 4.

**Step 1 — FUN_1800dfaf0 (120720-121022): pushes A out around the far B piece.**
```
d4 = R + mr + 0.25·R                                                  // 120794
b2 = copy(b)
if (!IsectInside(a, b2, isab)) return 0                               // 120847
if (!SplitInside(P, b2, isab, &i0, &i1, nearB, farB)) return 0        // 120850
// FUN_1800de040 (119886-119967): S/E = start/end of farB[0]; c = |E-S| >= 1e-12 required
q = (S+E)/2 + d4·(-(E.y-S.y), (E.x-S.x))/|E-S|                        // 119955-119958 (left normal of chord)
Retrace(farB[0])                                                      // 120855
ring = MCCECalcOffset(1, d4, farB, 0x100, 0.0, 1e-6, 1e-6)           // 120856-120858
require n(ring)==1; if ring[0].dir != 1: RevSh                        // 120859-120862
if (!IsectInside(a, ring, isab2)) return 0                            // 120863-120865
if (!SplitInside(q, ring, isab2, &j0, &j1, -, -)) return 0            // 120866-120869
k = (j1+1 < n(isab2)) ? j1+1 : 0                                      // 120872-120875
newA = { copy of a[0] header, entities cleared (FUN_1800137a0) }      // 120870-120871
append section ring[isab2[j1].+0x10] from (isab2[j1].ent,t) to (isab2[k].ent,t)          // 120878-120883
append section a[isab2[k].+0x20]     from (isab2[k].+0x24,+0x28) to (isab2[j1].+0x24,+0x28) // 120885-120891
a = newA; return 1
```
The resulting A replaces its part between the two crossings with the arc of the encasing ring that is not on q's
side. q is d4 to the left of the far piece's chord.

**Step 2 — FUN_1800dc820 (119206-119393): rounds the two corners where B meets A, using cubic Béziers.**
```
b2 = copy(b)
if (!IsectInside(a, b2, isab)) return 0                               // 119288
if (!SplitInside(P, b2, isab, &p8, &p9, nearB, otherB)) return 0      // 119290
// FUN_1800df120 (120395-120464): requires isab[p8].+0x30 == (p8+1 mod n)
aPiece = section a[isab[p8].+0x20] from isab[p8](+0x24,+0x28) to isab[p8+1](+0x24,+0x28)
if (!FUN_1800dcce0(R, mr, aPiece, nearB, a, b2, isab, p8, p9, out)) return 0   // 119296
b = out; return 1
```
**FUN_1800dcce0 (119396-119610), with h = R + mr and K = 0.5522847498307935** (the cubic quarter-circle constant):

Lengths — FUN_1800de390 (119970-120036). It requires 1 non-empty shape in each.
```
LA = len(aPiece)  (FUN_180121980)
LB = len(nearB)
if (aPiece has 3 entities && nearB has 1 entity) L = len(aPiece entity 0)
else                                             L = (LA - LB)/2
fail if L < 2h  or  (LB - 1e-4)/2 < h
s1 = 2h
s2 = min(1.5h, (LB - 1e-4)/2)
fail if s2 < 1e-4
```
Corner 1 — FUN_1800dd510 (119613-119723). S = start point of aPiece's first entity.
```
C1.P0 = PointAt(s1, aPiece from start)
C1.P3 = PointAt(s2, nearB from start)
C1.P1 = P0 + K·h·unit(S − P0)
C1.P2 = P3 + K·h·unit(S − P3)          (fail if a unit length is < 1e-12)
OK iff Inside(a, 1e-5, Bez(C1, 0.5)) == 2
```
Corner 2 — FUN_1800dd9f0 (119726-119883). E = end point of nearB's last entity.
```
C2.P3 = PointAt⁻¹(s1, aPiece from end)
C2.P0 = PointAt⁻¹(s2, nearB from end)
C2.P1 = P0 + K·h·unit(E − P0)
C2.P2 = P3 + K·h·unit(E − P3)
OK iff Inside(a, 1e-5, Bez(C2, 0.5)) == 2
```
Assembly of the new B (single shape, entities rebuilt):

`i1 = (p8+1) mod n`, `i2 = (p9+1) mod n`. Fields are .ent/.t at +0x14/+0x18 for the B side and +0x24/+0x28 for
the A side.
1. B section isab[i1] → isab[p9] (119491-119497).
2. A section isab[p9] → isab[i2], from shape isab[p9].+0x20 (119500-119506).
3. B section isab[i2] → isab[p8] (119508-119514).
4. If fewer than 3 entities: fail (119517-119524).
5. 'L' from the end of the last entity to C1.P0; then `AppendBezier(1e-4, C1)` (119557-119567).
6. 'L' C1.P3 → C2.P0; then `AppendBezier(1e-4, C2)` (119570-119580).
7. 'L' C2.P3 → the start of the first entity, closing the loop (119596-119606).
8. Update the bounding box (`GetMinMaxViaShEnt`, 119608).

### 3. What the engine does differently for Peel
- `CDynPockParm+0x0c` is a byte (operator= 12583; Clear 10314). In the engine object it sits at CCePock+0x14 (the
  parm is at CCePock+8).
- Across the closure of FUN_180011710/FUN_180090290/FUN_1800124d0/FUN_18006e620 the only reader is FUN_180076500
  (66889-67066). It is reached from the entry code: FUN_180070130 → FUN_180076b10 → FUN_180075a00 → FUN_180075f60
  (66842) → FUN_180076500.
- No other branch on "peel" exists in mcce. The strings "Peel"/"peel" appear only in the PeelMotMat/PeelAesthetics
  code and in their registry keys.

FUN_180076500 builds the entry-reference chn stored at CCePock+0x120 (`pock_chn`, 0x88 bytes):
```
sh = first non-empty of (param_4, param_3, param_2)                  // 66953-66965
bb = bbox(sh) = (minx,miny)-(maxx,maxy)                              // 66966
switch (parm+0x0c):                                                  // 66967-67019
  1: (midx, miny)  2: (maxx, miny)  3: (maxx, midy)  4: (maxx, maxy)
  5: (midx, maxy)  6: (minx, maxy)  7: (minx, midy)
  8: (parm+0x10, parm+0x18)   // explicit point (CCePock+0x18..+0x24). Peel sets this: formats/dynamic.md (+0x0c = 8,
                              // +0x10..+0x1c <- op 0x1548..0x1554)
  default (0, others): (minx, miny)
(near, ent, t, pt) = nearest point of sh to ref                      // FUN_18013d010, 67023
tan = unit tangent at (ent, t), tol 1e-10                            // FUN_1801204a0, 67025-67033
if (shape hole bit +0x54&1) { if dir(+0x50) != 1: ty = -ty  else tx = -tx }   // 67035-67038
else                        { if dir == 1:      ty = -ty  else tx = -tx }     // 67039-67045
chn = 'L', e1 = pt + (ty, tx), e2 = pt                               // 67046-67054 (unit-length line ending on the boundary)
```
The reference chn's end point is reused as the entry point on the next level. In FUN_1800757e0 (66418-66489) reuse
needs all of:
- its depth field (CCePock+0x198) is within 1e-5 of CCePock+0x88;
- the point lies within 1e-5 of the current material;
- `param_3+0x1c == 0`.

FUN_180075f60 (66792-66825) then uses that point directly. How the entry itself proceeds from the chn is part of the
Entry section (FUN_180070130 / FUN_180052a90), not of Peel.

So, from the engine's point of view, **Peel = Dynamic Mill on the PeelAesthetics-adjusted boundaries, entered at the
explicit point op 0x1548..0x1554** (mcmill sets that point; see formats/dynamic.md). Sweep generation, direction and
linking are the shared engine states (FUN_18006d170 / d330 / 67ce0). No peel-specific trochoid or step code exists
in mcce.

### NOT DECODED (Peel)
- PeelMotMat output wiring: the outputs of FUN_1800e28c0 are never assigned in the decompile (122888-122966).
  → re-checked §6.6.1; still needs disassembly.
- FUN_1800942b0 and its callees FUN_180095100 / 180094840 / 180095ae0 / 180094d30: endpoint-graph joining, role
  unverified. → **Decoded in §6.6.2** (convex hull; FindGaps' loop is the hull).
- `MCCECalcContourOffset` (0x180005272) internals and the meaning of flags 0x40127. (Sign rule decoded: §6.13.2.)
- `MCCECalcOffset` flag 0x100. (Sign rule decoded: §6.13.1.)
- FUN_180146e20: section copy (wrap and direction rules). (Decoded in §6.11.1.)
- FUN_1800aafd0 / FUN_1800aca30: Bézier → entity fit and its tolerance test. (Decoded in §6.14: chord + 3-point check.)
- FUN_1801349c0 and qsort comparator LAB_1800074dc: ISAB post-processing and sort order. This also leaves the exact
  meaning of ISAB +0x30/+0x34 open; FUN_1800df120 only checks `+0x30 == i+1`.
- FUN_1801489a0: section-in-shape test used by FUN_1801130f0.
- mcmill side: which of this+0x328 / this+0x348 becomes Motion vs Material, and the name of op 0x15f0. (Decoded in §6.14:
  +0x328 = Motion = A, +0x348 = Material = B; op 0x15f0 = record 0x1658 "Min toolpath radius %".)

---------------------------------------------------------------------------------------------------------------------
# 2. Dynamic Contour (style 2)
Decoded result: Dynamic Contour has **no separate motion code**. With CTOUR_ON (0x200000) set, the preprocessing
FUN_180090290 calls FUN_180092950 (l.81078-81359), which builds the machining region as a **band along each wall**
(depth from P+0xe8/+0xf0/+0xf8, distance from the wall P+0x100 × P+0x108 + R + min radius, side from CTOUR_TO_LEFT
0x400000, grouping from `IsEaShSepCtour`, CTOUR_POCKET 0x800000 changes the wall set). That band is then machined by
the same region loop as Dynamic Mill (§3 C/D). Full spec: **§3 B.4** below. The band kernel helpers are listed there
as NOT DECODED.

---------------------------------------------------------------------------------------------------------------------
# 3. Dynamic Mill engine (styles 0, 2, 3): setup, cutting states, entry, linking, output

## 3 B. Engine setup, region construction / ordering, Dynamic Contour, mode flags

Line numbers are in `decomp/mcce_all.c`. "Parm+X" means a CDynPockParm offset (formats/dynamic.md). The
engine-side copy (`CDynPockParmDer`) has the same layout. CCePock keeps it at `this+8`, so CCePock+(X+8) = Parm+X.
`CSh` fields used here are set by `CopyPockBndsToShAr` (180001 0e1, l.519-536): +0x30..+0x4f = min/max box,
**+0x50 = direction** (+1 if pock_bound.ccw, otherwise −1; 0 marks an open shape, see FUN_1800292c0),
**+0x54 bit0 = island** (pock_bound.isl), +0x58 = region tag (set by the engine), +0x60 = z.
A `CVe<CSh*>` is {vft +0, dtor +8, begin +0x10, end +0x18, cap +0x20}. A list at object offset X therefore has
its begin pointer at X+0x10.

### B.1 Mode flags (Parm+0x08): names from mcce's own dumper FUN_180097640 (l.83640-83775)
| bit | mcce name / mcce text | test sites (function, line) and effect |
|---|---|---|
| 0x1 | CE_CLIMB_MILL "climb mill (otherwise conventional)" | FUN_18002c1f0 l.28377-28394 orients the tool-centre region: outer +1 / island −1 if set, the reverse if clear. FUN_18002c0e0 l.28327-28356 (kind 4) orients the material opposite to that: set → outer −1, island +1; clear → outer +1, island −1. FUN_18006a3b0 l.60614-60622: if bit 0x4 is clear, set → Parm+0xd8 := Parm+0xc8, clear → Parm+0xc8 := Parm+0xd8. Parm+0x1b0 := flags&1 (FUN_180069560 l.60218). There are many more `&1` tests in CEnCut/CMmCut code (e.g. 180020610, 18005a000, 180035990 l.33376); those are not decoded here. |
| 0x2 | CE_BEG_END_DN "begin and end the tool path with tool down entities, FIRST GENERATION ONLY" | base bit (0x12/0x42). No engine-side test of it was found by the mask greps in this pass. |
| 0x4 | CE_BIDIR_MILL "swap between climb and conventional" | FUN_18006eec0 l.62875-62882: if set, reverse every region list (FUN_18002c2c0 l.28407-28419 = RevSh on RG lists +0x30,+0x58,+0x80,+0xa8,+0x128,+0x150,+0x178,+0x1a0,+0x240) and toggle bit 0x1. Callers: FUN_18006e620 l.62668 (region start, when flags&1 ≠ CCePock+0x1b8 = the original bit 0) and l.62711 (before **every** link state ≥3). FUN_18006a3b0 l.60615 (feed copy above is skipped). FUN_18005e690 l.54349 and FUN_180061cd0 l.56114 are inside the state code (not decoded here). |
| 0x8 | CE_METRIC | FUN_180069560 l.60242-60248: Parm+0x1c0 = 0.254 if set, else 0.01. |
| 0x10 | CE_START_INSIDE "start deep inside the pocket" | FUN_180090290 l.79826: selects FUN_180090fa0 (B.3). |
| 0x40 | CE_START_FACING "start facing (outside outer boundary)" | FUN_180090290 l.79827: selects FUN_180090ae0 (B.3). |
| 0x100/0x200/0x400/0x800/0x1000/0x8000 | CE_ENTRY_HELIX_ONLY / _HELIX_MED_BURY / _HELIX_MED_TROC / _PROFILE_RAMP / _MED_RAMP / _CUS_RAMP | FUN_18006fc70 l.63517 (mask 0x8700 → FUN_18007cb70 when no entry was found); FUN_180072a30 l.64962 (mask 0x700: helix radius limited to 0.9·CCePock+0x98 = 0.9·Parm+0x90). Entry code; see the entry section. |
| 0x2000 | CE_RAMP_ANG "entry ramp specified by an angle of descent" | FUN_18007bc00 l.69814: set → CCePock+0x70 (Parm+0x68 plunge angle); clear → CCePock+0x78 (Parm+0x70 pitch). |
| 0x10000..0x80000 | CE_RETRACT_FOR_ISL / _DIS / _ISL_OR_DIS / _ISL_AND_DIS | no direct mask test found in this pass (NOT DECODED). |
| 0x100000 | CE_CEN_HELIX_ON_PT | FUN_180078840 l.68096: set → try FUN_180077e70 (helix on the chained point) first; otherwise, or if that fails, FUN_180079050. |
| 0x200000 | CE_CTOUR_ON "contour on" = **Dynamic Contour** | FUN_180090290 l.79825 (→ FUN_180092950); FUN_18008f970 l.79510 (→ FUN_18008fca0); FUN_1800298d0 l.27074 (material sliver filter skipped); FUN_18006a3b0 l.60623 (Parm+0xe8 ≥ GetMinMatThick() = 0.001); IsEaShSepCtour. |
| 0x400000 | CE_CTOUR_TO_LEFT "contour to the left (otherwise right)" | sign of every contour offset: FUN_1800930c0 l.81378, FUN_1800918c0 l.80592, FUN_1800914a0 l.80460, FUN_180092950 l.81257-81258. |
| 0x800000 | CE_CTOUR_POCKET "contour inside of pockets, from closed boundaries build pockets" | IsEaShSepCtour (true only when 0x200000 is set and 0x800000 is clear, 1800011e5 l.848; same inline test in FUN_180069de0 l.60393); FUN_1800922e0 l.80980. |
| 0x2000000 | CE_BND_TOOL_CEN "all boundaries are tool center point boundaries" | FUN_18002b840 l.28160, FUN_18002ae40 l.27750/27762, FUN_18006ef10 l.62904 (tool-radius offsets dropped, B.4/B.6); 1800058a3 l.10153 and 18009a430 l.85181 (dump writers). |
| 0x4000000 | CE_AIR_ENTRY_ONLY | FUN_180070130 l.63625: set → no FUN_18006fc70 entry search; the region is flagged instead (CCetp+0x28 \|= 1). |
| 0x8000000 | CE_NO_MICRO_LIFT "no micro lift on first approach to facing area" | FUN_180076b10 l.67198. |
| 0x10000000 | CE_MOAT_FIRST "mill gouge boundaries with Dynamic trochoidal motion first" | set by registry `bTurnOnMoatFirst` (FUN_180069840 l.60330-60334); FUN_18002ae40 l.27733 (FUN_18002be40) and l.27948 (FUN_18002c360); FUN_1800688e0 l.59870; FUN_18006e3f0 l.62446; FUN_180077a50 l.67594. |
| 0x20000000 | CE_RECURSION "recursive call within Dynamic Mill" | suppresses most registry overrides (FUN_180069840 l.60329-60371) and the call counter (FUN_180069560 l.60263); FUN_180076b10 l.67189 (no reduced first pass). |
| 0x40000000 | CE_AIR_DWELL "add air dwell motion" | FUN_180071df0 l.64607, FUN_180074350 l.65780; FUN_180076b10 l.67204: if micro lift (CCePock+0x50 = Parm+0x48) < 1e-7, use CCePock+0x248 = Parm+0x240 (`dAirDwellPos`, default 0.01). |

### B.2 Parameter derivation (FUN_180069560, l.60203-60268) and its helpers
Order of operations:
```
Der = defaults(FUN_180068ff0)            // l.60038-60070, see table below
Der.base = Parm                          // CDynPockParm::operator=   l.60213
if (Der[0x21c]) read_registry(Der)       // FUN_180069840 l.60275-60380 (0x21c defaults to 1)
validate(Der)                            // FUN_18006a3b0 l.60520-60630
Der+0x1b0 = flags & 1                                                   l.60218
Der+0x200 = CalcLnStepover(0.1745329,  R - r)   // s_10                 l.60219
Der+0x208 = CalcLnStepover(2.9670597,  R - r)   // s_170                l.60222
Der+0x210 = CalcLnStepover(0.004363323129985824, R - r) // s_0.25deg    l.60225
Der+0x1f8 = CalcLnStepover(Parm+0x38, R)        // s_work, clamp to [s_10, s_170] l.60228-60234
Der+0x1b8 = 10.666 * R                                                  l.60237
Der+0x1d8 = 0.45*R; if (0.45*R < Parm+0x40 || !Der[0x21e]) Der+0x1d8 = Parm+0x40   l.60238-60241
Der+0x1c0 = (flags & 8) ? 0.254 : 0.01; if (0.125*R < it) it = 0.125*R              l.60242-60251
rr        = Der[0x21e] ? Der+0x1d8 : Parm+0x40                                       l.60252-60255
Der+0x1c8 = Der+0x1c0 + 0.25*rr                                                      l.60256
Der+0x1d0 = (0.25*Parm+0x40 + Der+0x1c0) * 2.5                                       l.60257
FUN_180069350(R, r, Parm+0x38, Der[0x21f], Der+0x230, &Der+0x1e0, &Der+0x228)        l.60258
Der+0x1e8 = 1.5 * Der+0x1e0                                                          l.60261
Der+0x1f0 = |Parm+0x40 * pi|                                                         l.60262
if (!(flags & 0x20000000)) Der+0x218 = ++global_call_counter   // FUN_1800b2be0 l.96826 (atomic)
```
R = Parm+0x28, r = Parm+0x30, CalcLnStepover(θ, d) = d(1 − cos θ).

**March step FUN_180069350 (l.60161-60200)**, inputs (R, r, θ = Parm+0x38, bull = Der[0x21f], a = Der+0x230 deg):
```
d = R - r
if (rl < 1e-7) rl = 0.7*d;   if (rl > 0.9*d) rl = 0.9*d;   if (rl < 0.01*d) rl = 0.01*d     // rl = Der+0x228, written back
chord = | rl*(cos0,sin0) - rl*(cos(a*pi/180), sin(a*pi/180)) |       // = 2 rl sin(a/2)
step  = FUN_180068e10(d, θ)
if (bull && chord < step) step = chord
Der+0x1e0 = step
```
FUN_180068e10 (l.59977-59987): `t = min(θ, 1.0); return |((t − 0.1745329)·0.38913485000000003/0.8254671 + 0.61086515)·d|·2·0.05`.
That is, step = 0.1·(R−r)·k, where k rises linearly from 0.61086515 at θ = 10° to 1.0 at θ = 1 rad and stays at 1.0 above that.
(Correction to formats/dynamic.md: the chord is not a general cap. It applies only while `bIsBullnoseAvailA` (Der+0x21f)
is set, and then the step is the smaller of the two values.)

**Defaults FUN_180068ff0 (l.60038-60070)**: +0x218 = −1; the dword at +0x21c = 0x01010001 (bytes +0x21c=1, +0x21d=0,
+0x21e=1, +0x21f=1); +0x228 = −1.0; +0x230 = 22.5; +0x240 = 0.01; +0x238 = (int)1 (byte +0x238=1, bytes +0x239..+0x23b=0);
+0x248 = −1; +0x24c = 1; +0x250 = +0x258 = 0; +0x260/+0x261 = 0; byte +0x220 = 0; all derived doubles = 0.

**Registry FUN_180069840 (l.60275-60380)**, key `\Configurations\Variables\Toolpaths\Dynamic Mill`. The key is read only
when FUN_1800691c0 (l.60105) can get it from MCCore.dll `get_REG_KEY`. Otherwise the defaults above stay.
| value (default) | stored at | condition |
|---|---|---|
| bIsAssertOn (0) | +0x21d | |
| bIsVarRadAvail (1) | +0x21e | |
| bIsBullnoseAvailA (1) | +0x21f | the old value "bIsBullnoseAvail" is deleted |
| bWriteMatMod (0) | +0x220 | |
| dMinCutRad (−1.0) | +0x228 | |
| dRendAngDeg (22.5) | +0x230 | |
| bIsBandsAvail (1) | +0x238 | |
| bIsCutOrdLenBas (0) | +0x239 | |
| bIsAirCutV3 (0) | +0x23a | |
| bUseRateOfEng (1) | +0x23b | |
| bOverrideMinPathRad (0), dMinPathRadPctToolDia (2.5) | +0x40 = pct·R·0.02 | if override |
| bTurnOnMoatFirst (0) | flags \|= 0x10000000 | !RECURSION |
| dStkPlaceErr (−1.0) | +0x60 = max(v, 0) | !RECURSION and v > −1e-6 (this **overrides the first-pass offset**) |
| dAirDwellPos (0.01) | +0x240 | |
| iCallNumToMill (−1) | +0x248 | !RECURSION |
| iNumRedPass (1) | +0x24c | !RECURSION |
| dDisTransOvr (2R·… = R·2.0) | +0x250 | !RECURSION (default `R*2.0`) |
| dDisTransTot (R·2·2) | +0x258 | !RECURSION |
| bUseOldGslCutOrd (0) | +0x260 | |
| bUseCtourGuide (0) | +0x261 | |
Without registry access, +0x23b (bUseRateOfEng) stays 0 (defaults l.60063), so there is no rate-of-engagement ramp.

**Validation FUN_18006a3b0 (l.60520-60630)**:
- θ (+0x38) < 0.1745329 → 0.1745329; > 2.9670597 → 2.9670597.
- r := 0 if |R − r| < 1e-7, if 2(R − r) < s(θ, R), if bullnose is off (+0x21f), or if r ≤ 1e-7. Otherwise r is clamped to [0, 0.95R].
- +0x40 ≥ 0.05R; +0x50 ≥ 0; +0x58 ≥ 0.001; +0x68 ≥ 0.0017453292519943296 (else set to 0x3f5c987103b761f5 ≈ 0.00174533);
  +0x70 ≥ 1e-4; +0x100 (int) ≥ 0; +0x108 ≥ 0; +0xa8 in [0, 99]; +0xb0..+0xd8 ≥ 0.
- If +0xc8 ≤ 1e-7, +0xc8 := +0xd8; if +0xd8 ≤ 1e-7, +0xd8 := +0xc8; then the BIDIR/CLIMB feed copy (B.1).
- If CTOUR_ON, +0xe8 ≥ 0.001.

### B.3 Input preprocessing: MCCECalcDynPock (180006d61, l.12159-12388) → FUN_180090290 (l.79775-79863)
MCCECalcDynPock inputs are three pock_bound lists: S1 (param_2), S2 (param_3) and S3 (param_4). For every S3 shape it
sets CSh+0x50 = 0 (l.12271-12278). It then calls `FUN_180090290(Der, S1, S2, → M, → AIR)` and
`FUN_1800125a0(Der, M, AIR, S3, chn, out, file, path)` (l.12285-12292).
M (local 0x338) is the **machining region** (tool-contact boundary). AIR (local 0x360) is subtracted from the
material (B.4: material = offset(region) − offset(AIR, −r)).
```
A = copy(S1); FUN_18008f970(Der, A); FUN_18012c200(A, 1e-10, 1e-10)       // l.79818-79820
B = copy(S2); FUN_18012c200(B, 1e-10, 1e-10)                              // l.79821-79822
if (B nonempty)              FUN_180091100(Der, A, B, M, AIR)              // l.79840
else if (flags & 0x200000)   FUN_180092950(Der, A, M, AIR)                 // l.79836  Dynamic Contour
else if (flags & 0x10)       FUN_180090fa0(A, M)        (AIR stays empty)  // l.79832  START_INSIDE
else if (flags & 0x40)       FUN_180090ae0(Der, A, M, AIR)                 // l.79828  START_FACING
```
- **FUN_18008f970 (l.79485-79545)**: UpdMinMaxShAr. If CTOUR_ON, FUN_18008fca0 (l.79608-79703) moves every shape whose
  start and end points are more than Parm+0x110 (chain tolerance) apart (that is, open shapes) into a side list.
  Unless IsEaShSepCtour, FUN_18008fb30 (l.79550-79603) runs: if **every** shape is an island, the shape with the largest
  |area| (FUN_180126ac0, trapezoid area with arcs via FUN_1801270e0, l.158544-158719) becomes a non-island
  (+0x54 = 0). The open shapes are then put back **in front of** the closed ones (AppendByMove plus swap, l.79517-79533).
- **FUN_18008f4a0 (l.79315-79480)** splits a list: islands → SoPockUnionAr(1e-6) → out1; non-islands → SoPockUnionAr(1e-6) → out2.
- **FUN_180153b30(tol, X, Y, out)** (l.182603+) copies X, then combines it with the shapes of Y (islands handled by
  bit0 of +0x54) via FUN_180153390, then DetNestLvlShAr. Every use in this section has the form "region minus
  holes/air" (outer list X, subtracted list Y): FUN_180090fa0 (outers, islands), FUN_180028cb0 (region, AIR). The
  internals of FUN_180153390 are NOT DECODED.
- **FUN_180090fa0 (l.80220-80269), START_INSIDE**: `(isl, out) = FUN_18008f4a0(A); M = FUN_180153b30(1e-6, out, isl)`, that is,
  the union of outer chains with the union of islands removed.
- **FUN_180090ae0 (l.80034-80217), START_FACING**:
  ```
  rect = FUN_18008f150(Der, A)       // l.79201-79310: bbox(A) grown on every side by
                                     //   m = 2*(2R + 2*rr + Parm+0x50 + Parm+0x60), rr = Der[0x21e] ? Der+0x1d8 : Parm+0x40
                                     //   one CSh of 4 lines (FUN_180124c50), dir −1, not an island
  (isl, out) = FUN_18008f4a0(A)
  M = copy(isl); for each: CSh+0x54 += 1     // l.80124-80131 (bit0 1 → 0: islands become outer boundaries)
  M += copy(rect)                            // FUN_18011f2e0 l.80135
  DetNestLvlShAr(M)
  T = SoPockUnionAr(1e-6, MCCECalcOffset(1, Parm+0x60, out, 0x127) ∪ isl)   // l.80138-80144
  AIR = FUN_180153b30(1e-6, rect, T)                                       // l.80145
  ```
  So the tool may travel anywhere in the bounding rectangle except the islands. The air is the rectangle minus
  (chains grown by the first-pass offset, plus the islands).
- **FUN_180091100 (l.80274-80393), S2 present**:
  ```
  M = FUN_180090fa0(A)
  (Bx, Bi) = FUN_1800900c0(B)   // l.79708-79761: shapes that intersect another B shape
                                //   (FUN_180162010 true) → Bx, the others → Bi
  E = FUN_180090fa0(Bx) ∪ Bi; DetNestLvlShAr(E)
  AIR = FUN_180153b30(1e-6, M, MCCECalcOffset(1, Parm+0x60, E, 0x127))   // l.80342-80345
  ```
  As a consequence of this data flow, material = M ∩ offset(S2, first-pass offset), so S2 plays the role of a stock
  boundary. The S2 semantics on the mcmill side are NOT DECODED.
- **S3** (open shapes, dir 0) is copied to RG+0x218 (FUN_1800292c0 l.26858-26896, which sets dir = 0 again).
  FUN_1800752c0 (l.66300-66365, called from FUN_180074560, entry code) picks the S3 shape with the longest length inside
  the tool-centre region (CCePock+0x340), using FUN_180134690/FUN_1801130f0 (class 2)/FUN_180149d80.

### B.4 Dynamic Contour preprocessing: FUN_180092950 (l.81078-81359)
```
s(left) = (flags & 0x400000) ? +1 : -1
L = FUN_180091d40(Der, A)   // l.80681-80839: leading shapes with dir 0 (or all shapes when IsEaShSepCtour) are copied
                            //   with +0x54 := 0; the remaining closed shapes go through FUN_180090fa0 (outers − islands)
                            //   and are appended
for each shape in L (in order):
  if dir == 0:   FUN_1800930c0(Der, sh, → REG, → WALL)                  // open chain
  else:          G = consecutive shapes starting here, ending after the first non-island
                     (each shape ends the group if IsEaShSepCtour)       // l.81184-81253
                 left = IsEaShSepCtour ? ((0x400000 && G[0].dir==1) || (!0x400000 && G[0].dir==-1)) : 1   // l.81255-81267
                 FUN_1800922e0(Der, left, G, → REG, → WALL)
AIR = FUN_180153b30(1e-6, REG, MCCECalcOffset(1, Parm+0x60, WALL, 0x127))   // l.81276-81285
M = REG
```
Band widths. W = Parm+0x100 (int) · Parm+0x108, rad = Parm+0x40 + R, t = Parm+0xe8 + Parm+0xf0 + Parm+0xf8,
u = Parm+0xf0 + Parm+0xf8. FUN_180069120 (l.60075-60088) gives
P = 2(R + rr) + 2R + Parm+0xf0 + Parm+0xe8 + Parm+0xf8 + Parm+0x50, with rr = Der[0x21e] ? Der+0x1d8 : Parm+0x40.
FUN_180069190 (l.60094-60097) gives Q = 2(Parm+0x40 + R) + 1e-4.
- **Open chain, FUN_1800930c0 (l.81362-81419)**: two-sided offset `FUN_1800d2cc0((W + rad)·s, −rad·s, sh, 0x125)`. Each
  resulting shape goes to FUN_1800918c0 if it is open (l.80535-80678) and to FUN_1800914a0 if it is closed (l.80406-80532):
  - FUN_1800914a0: c = −1 if (left && dir==1) or (!left && dir==−1), else +1. WALL += {sh, offset(offset(sh, t·c), −u·c)}.
    REG += {sh, offset(sh, P·c)}. All offsets use mode 0x100.
  - FUN_1800918c0: c = s. FUN_1800d3210(t·c, −u·c, sh, 0x100) → X. FUN_1800aeae0(cb LAB_18000146f, …, (P − Q)·0.1, Q, P) → k.
    On success, FUN_1800d0da0(c·k, sh, 0x300) → Y. Then REG += FUN_18015be30(1e-6, Y ∪ X) and WALL += Y. The root
    search FUN_1800aeae0 is NOT DECODED here. → **Decoded in §6.5** (FUN_1800d2cc0 §6.5.1, FUN_1800d3210 §6.5.2,
    FUN_1800d0da0 §6.5.3, FUN_1800aeae0 bisection §6.5.4, FUN_18015be30 = MCCECalcPockIntersect body §6.5.6).
- **Closed group, FUN_1800922e0 (l.80883-81075)**: c = left ? +1 : −1.
  ```
  O = MCCECalcOffset(1, MCCECalcOffset(1, -(W + rad)·c, G, 0x121), +rad·c, 0x100)   // inset by W+rad, then outset by rad
  for each nest group K of O (FUN_18011f1f0), stopping at a group whose last shape is not an island:
     T1 = offset(offset(K, -t·c), +u·c)
     if !(flags & 0x800000):  REG += nest(K ∪ offset(K, -P·c))           // l.80980-80996 (param_4 = REG; corrected §6.13.3)
     else:                    REG += K
     WALL += nest(K ∪ T1)                                                  // l.81007-81012 (param_5 = WALL)
  ```
  So the machining region is a band of width P inside each wall, and the material is the thinner band of depth t
  (Parm+0xe8 min material thickness and two further thicknesses at Parm+0xf0/+0xf8) next to the wall (corrected in
  §6.13.3: an earlier reading swapped REG / WALL here); its distance from the wall is set by W = passes × spacing
  (Parm+0x100 × Parm+0x108) plus R + min radius. Dynamic Contour also skips the material sliver filter
  (FUN_1800298d0 l.27074).

### B.5 CCePock run (FUN_1800125a0 l.15648 → FUN_180011710 l.15111-15199) and prepare FUN_1800124d0 (l.15620-15636)
```
FUN_1800103b0(pock)  /* ctor */ ; FUN_180011710(pock, Der, M, AIR, S3, chn, out, file, path):
  CCetp tp; FUN_180085900(tp); clear(out)
  FUN_1800124d0(pock, Der, M, AIR, S3, chn):
     FUN_1800122f0: FUN_180029ae0(RG = pock+0x298), clear(pock+0x730), FUN_180011a00(pock)
     pock+8 = Der (FUN_180010ef0 l.14865-14904, copies the base and all derived fields)
     progress init FUN_1801140c0(pock+0x270, Parm+0x1a0, Parm+0x1a8)
     if (pock+0x128 type ∉ {'A','L','P'}) FUN_180012390(pock, AIR, pock+0x120)   // l.15564-15617: type := 0; if AIR is
           // nonempty, copy the first entity of the first non-island AIR shape and set its field [0xf] = pock+0x88 + 1.0
     FUN_18002b840(RG, pock, M, AIR, S3, chn)        // B.6
  FUN_180085ed0(tp, file, path, pock)                // open output
  while (FUN_18002a2a0(RG)) FUN_18006e620(pock, tp, out, ...)   // B.7 / region machining
  FUN_180085ca0(tp); progress 100; FUN_180085d60(tp)
FUN_1800122f0(pock); FUN_180010800; FUN_180027f30(...)   // teardown
```
(pock+0x120 = Parm+0x118, a pock_chn record: the type byte at +0x128 is Parm+0x120.)

### B.6 Region builder FUN_18002b840 (l.28004-28199), RG = CCePock+0x298, RG+0x470 = CCePock*
```
RG+8   = copy(M)                 RG+0x3b0 = chn entities (FUN_180015140)
RG+0x240 = copy(AIR)             RG+0x218 = S3 with dir 0 (FUN_1800292c0)
RG+0x3a0/0x3a8 = start point of the first entity of the LAST non-island shape of M     // l.28118-28151
d = Parm+0x40 (+ R unless CE_BND_TOOL_CEN)                                              // l.28158-28162
RG+0x30 = MCCECalcOffset(1, +Parm+0x40, MCCECalcOffset(1, -d, M, 0x127), 0x100)         // l.28163-28170
          // tool-centre region: inset by R+rmin, outset by rmin (corner radius >= min toolpath radius)
FUN_1800d96d0(RG+0x30, 1)        // l.117596: clears the per-entity byte +0x12 and sets doubles +0x70/+0x60 (values NOT DECODED here; §6.1.9: all zero)
FUN_18002c1f0(flags, 0, RG+0x30) // orientation by CE_CLIMB_MILL (B.1)
FUN_18002ae40(RG)                // per-region material, below
FUN_18003d430(RG)                // RG+0x458 = RG+0x460 = area(RG+0x58 list) (FUN_18010f0e0), RG+0x468 = 0
```
**FUN_18002ae40 (l.27668-28003)**:
```
if (MOAT_FIRST) FUN_18002be40(RG)                                           // NOT DECODED
if AIR nonempty:  A1 = MCCECalcOffset(1, BND_TOOL_CEN ? R - r : -r, AIR, 0x127)
if AIR nonempty && r > 1e-7:  A2 = MCCECalcOffset(1, BND_TOOL_CEN ? R : 0, AIR, 0x127)
i = 0
while i < |RG+0x30|:
   G = shapes i.. up to and including the first non-island  (list order is [islands..., outer])   // l.27782-27904
   FUN_180028cb0(RG, R - r, i_end, G, A1, → MAT)   // l.26664-26751:
        MAT = FUN_180153b30(1e-6, MCCECalcOffset(1, max(R - r - 1e-4, 0), G, 0x127), A1)
        FUN_1800298d0(RG, MAT): unless CTOUR_ON, h = 0.5*Parm+0x58 (= 10 x chain tol);
            if (h >= 1e-12) MAT = offset(offset(MAT, -h, 0x101), +h, 0x100)       // drops slivers thinner than 2h
        FUN_18002c0e0(flags, 4, MAT)   // orientation
        every MAT shape: +0x58 = i_end (region tag); every entity byte +9 = 0
   if MAT empty: remove G from RG+0x30 (a region with no material is dropped)        // l.27909-27930
   RG+0x58 += MAT
   if r > 1e-7 (bullnose):  FUN_180028cb0(RG, R, i_end, G, A2) → RG+0x80
   if MOAT_FIRST:  FUN_18002c360(RG, G, A1, A2)                               // NOT DECODED
```
Hence (Parm offsets) **material of a region** = (tool-centre region grown by R − r) − (AIR shrunk by r), minus slivers.
This is the "feasible tool-centre boundary offset R − r" noted in formats/dynamic.md.

### B.7 Region order / next region FUN_18002a2a0 (l.27305-27665)
Regions are processed **in the list order of RG+0x30**. That order comes from DetNestLvlShAr plus the offset kernel and
is NOT DECODED. The engine neither sorts nor uses a nearest-region rule in the code read here.
**DetNestLvlShAr (7729-7800, read 2026-11-07):** one shape → +0x54 = 0. Otherwise FUN_18013e3e0 (171090-171213) first
`qsort`s the list with LAB_180006abe = `jmp FUN_18013e780` (disassembly 0x18013e780-0x18013e7ac: closed shapes (CSh+0x50 ≠ 0)
before open ones, **every two closed shapes compare equal**), then inserts the shapes one by one into a nesting tree (a shape
inside a node goes below it, otherwise it becomes the last sibling, FUN_1801402a0 / FUN_18013f4b0 / FUN_18013fec0), and
FUN_18013fff0 writes the list back in tree order. The qsort is MSVCR120's (import table of mcce.dll), which is not stable:
with all keys equal it still permutes the list in a fixed way. So the region order = f(the order the offset core emits, that
qsort's permutation, the tree walk). The first is the NOT DECODED part (the offset core, §6.13.1); the rest is decodable.
```
clear current lists RG+0xa8, +0xd0, +0xf8, +0x128?, +0x150, +0x178, +0x1a0; RG+0x120 = 0
move from the front of RG+0x30: leading islands, then the first non-island → RG+0xa8 (current tool-centre region = CCePock+0x340)
if CCePock+0x240 (bIsBandsAvail): for each current shape, a CBands (0x68 bytes, FUN_180144430) built with FUN_180143d20(…, 1e-5, sh) → RG+0xd0
tag = RG+0x58[0].+0x58
move the leading RG+0x58 shapes whose +0x58 == tag → RG+0x178 (current material = CCePock+0x410); +0x58 := 0
move the leading RG+0x80 shapes whose +0x58 == tag → RG+0x1a0 (bullnose material); +0x58 := 0
RG+0x460 -= area(RG+0x178)                                  // progress bookkeeping
RG+0x150 = MCCECalcOffset(1, max(R - r - 1e-4, 0), RG+0xa8, 0x100)
RG+0x128 = MCCECalcOffset(1, -Parm+0x40, RG+0xa8, 0x100)
return (RG+0x178 nonempty && RG+0xa8 nonempty)
```
Progress (FUN_18003d360 l.36903-36930): 100 − clamp(100·(area(RG+0x178) + RG+0x460)/RG+0x458, 0, 100), rounded.

### B.8 Region start in FUN_18006e620 (l.62540-62850): setup parts
- CCePock+0x710 = 0, +0x718 = +0x720 = 0, +0x728 = −1. FUN_180011d50(CCePock+0x730) (l.15342-15388) clears a CVe<CPt2>.
- If (flags & 1) ≠ CCePock+0x1b8, FUN_18006eec0 restores the start direction (B.1).
- **FUN_18006e440 (l.62458-62465)** is a debug filter: the region is machined only if Parm+0x248 (`iCallNumToMill`) < 0,
  Parm+0x218 < 0, or the two are equal.
- **FUN_18006ef10 (l.62890-62922)** runs after the entry: out += MCCECalcOffset(1, R, CCePock+0x340, 0x100), or a plain copy
  under BND_TOOL_CEN. This is the swept boundary returned through MCCECalcDynPock param_5.

### B NOT DECODED (address – role from the evidence)
- FUN_180153390 – the boolean core of FUN_180153b30 (combine region with subtracted shapes). (Still NOT DECODED; its
  pairwise primitive FUN_18015b2e0 is described as far as read in §6.5.6.)
- FUN_18012c740 – per-shape cleanup with tolerances 1e-10 (it may also set dir 0 on open shapes; unverified).
- FUN_180162010 – shape-pair intersection test used to split S2.
- FUN_1800d2cc0, FUN_1800d3210, FUN_1800d0da0, FUN_18015be30, FUN_18011f1f0, FUN_1800aeae0 (+ callback LAB_18000146f) – contour band offset / grouping kernel. → **Decoded in §6.5** (callback target still unverified).
- FUN_1800d96d0 – the per-entity values it sets (+0x60/+0x70). → §6.1.9 (both 0.0, byte +0x12 = 0).
- FUN_18002be40, FUN_18002c360 – CE_MOAT_FIRST region preparation.
- FUN_180143d20 / FUN_180144430 – CBands construction (bIsBandsAvail).
- FUN_180029ae0, FUN_180011a00, FUN_1800103b0 – constructors/reset.
- The order produced by DetNestLvlShAr / MCCECalcOffset (it fixes the region order).
- The CE_RETRACT_FOR_* bits (0x10000..0x80000) and CE_BEG_END_DN (0x2): no direct test site found in this pass.

## 3 C. Region loop cutting states (states 0/1/2 of FUN_18006e620)

All line numbers are in `decomp/mcce_all.c`. "C+x" = CCePock offset x; "P+x" = CDynPockParm offset x.
**C+x = P+(x-8)**: FUN_1800124d0 L15628 copies the parm block to C+8 with FUN_180010ef0 (L14865-14901: operator=
plus every derived field 0x1b0..0x261 at the same offset). Example: C+0x10 = P+8 (flags), C+0x30 = P+0x28 (R),
C+0x38 = P+0x30 (r), C+0x1e8 = P+0x1e0 (march step), C+0x1f0 = P+0x1e8 (1.5 x march step), C+0x200 = P+0x1f8 (s_work).

### C.0 Objects used by the states (locals of FUN_18006e620, L62540-62850)
| local | type | role (evidence) |
|---|---|---|
| `local_2d8` = **E** | CEnCut | trial/step record. Two CMmCut measurement blocks: **M1 = E+8** (circle R-r) and **M2 = E+0x140** (circle R) (vtables set at L62778-62779; E+0x140 = local_198) |
| `local_3c8` = **G** | CGbap | "attach to boundary" result: G+8 byte valid; G+0x10 CSh (lead-in entities, vector at G+0x28/G+0x30, bbox G+0x40..0x58); G+0xa8 int shape index, G+0xac int entity index, G+0xb0 double t, G+0xb8/0xc0 point (written by FUN_180022a50 L23924-23941) |
| `local_2f8` (int) | index into C+0x420 (remaining-material shape list) | -1 = none (L61895 test) |
| `local_3e8` low byte | ramp flag | 1 at start (L62661) and after every state>=3 call (L62717) |
| `local_3e8+4` (int) | state | 0/1 -> FUN_18006d170, 2 -> FUN_18006d330, >=3 -> FUN_180067ce0 (L62695-62718) |
| `param_2` = **T** | CCetp | output writer (see C.5) |

E field map (relative to E; M1 fields at E+8+k; FUN_180030de0 is called with param_9 = E+8, L30622-30624):
- E+0x10..E+0x97: the step entity (`pock_chn`, 0x88 bytes); type char at E+0x18 ('L'/'A'); line start E+0x28/0x30,
  end E+0x38/0x40; arc centre E+0x28/0x30, radius E+0x38, start angle E+0x40, sweep E+0x48 (FUN_18006dfa0 L62347-62357,
  FUN_180030de0 L30675-30682).
- E+0x98: heading change of the trial step (FUN_180017a10 L18468 writes the angle argument; FUN_180017d30 L18559).
- E+0xa0: step length (FUN_180030de0 L30687: L = |end-start|, A = |r*sweep|).
- E+0xa8: swept-region shape list; E+0xd0: its area (FUN_180126630(0.001, ...) L30690; FUN_180126ac0 L158542 sums
  the shape's line/arc entities - area routine, internals not decoded).
- **E+0x128: width w1** (circle R-r), **E+0x260: width w2** (circle R, block M2), E+0x130 / E+0x268: a second value
  per block (written by FUN_180037070/FUN_1800380f0, NOT DECODED), emitted into the output record (C.5).
  → Correction §6.1.0/§6.8: written by FUN_18002fc80 = total engaged sweep on the end-cap circle.
- **E+0x138: step flags** (M2 flags at E+0x270). E+0x13c: pass number byte (C.4).

### C.1 Measurement of one step - FUN_180030be0 (L30614-30651) / FUN_180030de0 (L30654-30732)
```
measure(mat = C+0x298, T, dir, E):
  p = *(mat+0x470)                      // pointer whose +0x10/+0x30/+0x38 are C flags/R/r
  measure_block(flags, R - r, mat+0x178, mat+0x1c8, ..., M1)          // L30622
  if (mat+0x1b0 list non-empty) and (E.flags & 0x18) == 0:            // L30625-30626
      M2.entity = M1.entity; M2.len = M1.len                           // L30627-30645
      measure_block(flags, R, mat+0x1a0, mat+0x1f0, ..., M2)           // L30646-30648
measure_block (FUN_180030de0):
  len = entity length                                                  // L30675-30687
  if !FUN_1800321e0(swept region of the step, radius rad) -> flags |= 8; return   // L30688, L30730
  areaSwept = area(swept region, tol 0.001)                            // L30690-30691
  if !FUN_180034ac0(...) -> flags |= 8; return                         // L30692, L30730
  material: the local window set (mat+0x1c8) if non-empty (FUN_1800331a0) else the full set (FUN_180032b30)  // L30695-30701
  ... FUN_18002fa30, FUN_1800337e0, FUN_180031150, FUN_1800354c0, FUN_180035630, FUN_18002fc80 (NOT DECODED here; → §6.1.3-6.1.6)
  removed = FUN_18003a140(...)  = areaSwept - area(loops rebuilt by FUN_18003a140)   // L35643-35644
  width = (len <= 1e-12) ? 0 : removed / len                           // L30711-30717
  if width < 1e-12 and !FUN_180035360(...) -> flags |= 0x10 (no material = air)    // L30718-30720
  if more than 2 16-byte items in the block's +0x108 list -> flags |= 2  // L30722-30724
  FUN_180037070 (may set flag 4, L34113); FUN_1800380f0 (sets/clears 0x100, L34530-34665)
```
So the width is the swept area that is not left over by FUN_18003a140's loop rebuild, divided by the step length.
That matches dynamic.md's "removed area / step length". The loop rebuild in FUN_18003a140 (L35581-35758) is NOT DECODED
here. → **Decoded in §6.1.5.**
Before each state-0/1 step, FUN_18002a140 (L27266-27302) rebuilds the local material windows. It clips mat+0x178 into
mat+0x1c8 and mat+0x1a0 into mat+0x1f0 to a disk around the current tool point (FUN_18008e4b0) of radius
**10 x C+0x1e8 + C+0x30 = 10 x march step + R** (L27296-27299). The clip routine FUN_180029c30 is NOT DECODED here.
→ **Decoded in §6.1.8.**

### C.2 Limits - FUN_18006cf10 (L61813-61871), exact
```
limits(C, T, &ramp, &lim1, &lim2):
  lim1 = lim2 = s_work (P+0x1f8)                                      // L61829-61830
  if P+0x1b8 (ramp length 10.666R) >= 1e-7 and ramp and P+0x23b:      // L61831-61832
      (prev1, prev2) = last output record (T ring, C.5) +0xa0, +0xa8, else 0   // L61833-61843 (FUN_18008ee10)
      k = (s_work - s_10) / P+0x1b8 * P+0x1e0                         // s_10 = P+0x200, step = P+0x1e0
      lim1 = k + prev1                                                // L61845-61846
      if r (P+0x30) > 1e-7: lim2 = k + prev2                          // L61847-61850 (else lim2 stays s_work)
      for lim in (lim1, lim2):  if lim < s_10: lim = s_10  elif lim > s_170 (P+0x208): lim = s_170;  lim = min(lim, s_work)  // L61851-61864
      if P+0x21f == 0 and |lim1 - s_work| < s_0.25 (P+0x210): ramp = 0   // L61865-61868
```
This confirms dynamic.md's ramp formula, with two refinements. (a) lim2 is ramped only when the corner radius r is
above 1e-7. (b) The "previous width" is the last **output record** in the CCetp 16-slot ring (C.5), which can be an
air move with width 0. It is not a separate measurement ring.

### C.3 States 0/1 - FUN_18006d170 (L61874-61909): search for a path back onto the boundary
```
state01(C, T, &matIdx, &ramp, E, G, &state):
  loop:
    clear E blocks (FUN_18001cfe0 x2), clear G (FUN_18001cef0); progress update      // L61883-61887, L61899-61903
    trial_step(C, T, &matIdx, &ramp, E, G, state)       // FUN_18006db90
    if G.valid: state = 2; return                       // L61889-61893
    if (E.flags & 0x10) or matIdx == -1: state = 3; return   // L61895, L61907
    if state == 1: state = 0                            // L61896-61898
```
`trial_step` = FUN_18006db90 (L62148-62200):
```
  FUN_18002a140(mat, T)                                 // local material windows, C.1
  (lim1, lim2) = limits(C, T, &ramp)                    // FUN_18006cf10 (consumes / clears the ramp flag)
  choose_step(C, T, lim1, lim2, &matIdx, E, G, state)   // FUN_180016c00
  dir = G.valid ? side(C+0x350[G.shape]) : 0          // FUN_18006f7a0 L63160-63181: +-1 from shape +0x50 / bit0 of +0x54
  if (E.flags & 8) == 0:    measure(mat, T, dir, E)     // L62175-62177
  if (E.flags & 0x18) == 0: commit(mat, T, E, &matIdx) // L62178-62180 - NO width test here
  if E.flags & 0x80: emit(T, E.entity, type = firstpass(C,E) | 0x40, E.flags, w1, w2, E+0x130, E+0x268)   // L62181-62186
  clear C+0x470 and C+0x498 lists                       // L62187-62198
```
`choose_step` = FUN_180016c00 (L17896-17911): if state != 1, try FUN_1800167d0. If that fails, or if state == 1, use
FUN_180017d30.
- FUN_180017d30 (L18474-18563) is the **move toward the material**. It builds one entity aimed at material shape
  C+0x420[matIdx] with FUN_18006b170, or FUN_18006b570 when that fails (both NOT DECODED here; → §6.2.1). It stores the heading change
  against the current output tangent in E+0x98, wrapped to [-pi, pi] (L18538-18559). Then it attaches the step to the
  boundary (FUN_180024720) and measures it with dir 0.
- FUN_1800167d0 (L17744-17893) = FUN_180016cb0 plus a P+0x21f refinement. When P+0x21f is set and the chosen step is
  not air/failed, FUN_18001bb60 (NOT DECODED here; → §6.2.3) gives an angle. FUN_180017a10 then re-shoots the step with that angle,
  sets flag 0x20 on the alternative, and keeps it if (w1' <= lim1 or w1' <= w1) and (w2' <= lim2 or w2' <= w2)
  (L17835-17861).
- FUN_180016cb0 (L17914-18348) selects between candidate trial steps (exact structure):
```
  A = FUN_180019de0(...)        // fails -> return false                                      L18140-18145
      = FUN_180019ec0: P = last tool point (FUN_18008e4b0); Q = FUN_1800186a0(lim1, dist = step); d = |PQ|;
        if d > 1.5*step: retry FUN_1800186a0 with 0.5*step                                   L19445-19464
        heading = atan2(Q-P) - atan2(current tangent) wrapped to [-pi,pi]; tangent = FUN_18008e710, else
        FUN_180028ac0 (material direction)                                                   L19465-19484
        step = FUN_180017a10(heading, d)  (a line P -> Q)                                    L19485
        fallback if that fails and P+0x21f: FUN_180017a10(0, step)  (straight on)            L19403-19408
  if within_tol(lim, A) [FUN_18001bab0] and A.flags & 0x100: accept A                        L18150-18151
  B = FUN_180017a10(0, d)       // straight continuation along the current tangent             L18152-18153
  if A.G.valid:
     if inside(lim, A) [FUN_18001bb30] and B.flags&0x18==0 and B.flags&0x100: S = A (backup); A = B   L18157-18183
     if A.G.valid and inside(lim, A): accept A                                               L18184-18188
  if ((A.flags|B.flags) & 2) and FUN_180018990(...) succeeds: done (it writes E/G)           L18190-18193
  elif FUN_18001a2b0(...) succeeds (writes E/G): replace with A when FUN_18001b9a0(A), inside(lim,A),
       |A.E+0x98| < |E+0x98| and A.flags&0x100, and then (FUN_18001ba10(A) or A.flags&6)    L18241-18254
  else: accept S if a backup exists and !inside(lim,A), otherwise accept A                    L18196-18231
```
  Predicates:
  - inside(lim, E) = w1 < lim1 and w2 < lim2 (strict) (L20357-20365).
  - within_tol(lim, E) = NOT[(|w1-lim1| >= s_0.25 or w2 >= lim2+s_0.25) and (|w2-lim2| >= s_0.25 or w1 >= lim1+s_0.25)]
    (L20338-20352).
  - FUN_18001b9a0(E) = !inside(CalcLnStepover(0.5 deg = 0.008726646 rad, R-r) on both circles) (L20296-20311).
  - FUN_18001ba10(E) = true unless w1 <= last record +0xa0 and (r <= 1e-7 or w2 <= last record +0xa8) (L20316-20333).
- FUN_180017a10 (L18381-18471) is the trial line step. Start P = last output point. Heading = current output tangent
  (FUN_18008e710), else the material direction (FUN_180028ac0), plus the angle argument. Then P2 = P + len*(cos, sin)
  (L18417-18441). FUN_180024720 attaches the step to the boundary. If the line has zero length and G is valid,
  FUN_18001c510 runs and dir = side(...) (L18445-18466). The step is then measured.
- FUN_1800186a0 (L18713-18785) finds the target point Q. It steps along the material shape list C+0x410 from
  position *matIdx, in increments of the given distance (FUN_18006f1c0 mode 0), until a point is found or the walked
  distance exceeds **pi x (R - r)** (L18753, L18776). At each candidate, FUN_18001efb0 places the tool centre at width
  `lim1` against the material wall. It uses tolerance max(0.1 x step, 1e-4) (L18739-18742). When P+0x21f is set, it
  first tries the nearest point on C+0x420[matIdx] with tolerance 1e-5 (L18756-18765).
- FUN_180024720 -> FUN_180021e40 -> **FUN_180022a50** (L23796-23976) attaches the step to the boundary. It intersects
  the step with the tool-centre boundary set **C+0x340** (FUN_180023dd0, else FUN_1800241c0). On a hit it cuts the
  step back to the intersection fraction (line end lerp, or arc sweep x fraction). It then sets **G.valid = 1** and
  G.(shape, entity, t, point) from the intersection record (L23904-23941). If t falls outside [0, 1], t is re-projected
  with FUN_18013d200 (L23942-23952). FUN_180023460, FUN_180021f70 and FUN_180022500 (alternatives when no hit) are
  NOT DECODED here. → **Decoded in §6.2.5.**

**State 0/1 role (from code):** each call fires single trial steps from the current tool point, chosen so the measured
width is near the (ramped) limit, and commits and emits them (record type 0x40). It stops when a step reaches the
tool-centre boundary (-> state 2, sweep along it) or finds no material (-> state 3, link).

### C.4 State 2 - FUN_18006d330 (L61912-62145): march along the boundary
```
state2(C, unlimited, T, &matIdx, &ramp, E, G, &state):
  air_max = C+0x1f0  (= P+0x1e8 = 1.5 x march step)                    // L61968
  ramp = 0                                                             // L61988
  dir = side(C+0x350[G.shape])                                         // L61992-61996
  path = candidate_path(C, G)                                          // FUN_18006caa0
  pos = start of path (entity 0, t 0)                                  // L61998-62011
  cut = air = 0; cutSeen = false; trimAt = -1; last = false
  do:
    progress update
    (lim1, lim2) = unlimited ? (FLT_MAX, FLT_MAX) : limits(C, T, &ramp)    // L62015-62029 (3.4028234663852886e38)
    ok = advance(mode 1, C+0x1e8 (= P+0x1e0 march step), path, pos -> npos, &len)  // FUN_18006f1c0 L62030
    if !ok: break
    E.entity = path section pos..npos (FUN_18006fc10; a line is set exactly pos.pt -> npos.pt)  // L62035-62036
    measure(mat, T, dir, E)                                            // L62038
    if E.flags&0x18 == 0 and w1 < lim1 and w2 < lim2: commit(E); cut += len         // L62040-62053
    if E.flags & 0x98 == 0:                         // not committed, not air, not failed   L62054
       if shorten(C, T, lim1, lim2, dir, E):        // FUN_18006dfa0
          len = new entity length; last = true      // L62058-62069
          measure again; if inside limits (same test): commit; cut += len                  // L62070-62081
    f = E.flags
    if f & 8:            air += len                                    // L62117-62119
    elif f & 0x10:       air += len                                    // L62101
         if cutSeen:
            if air > 3*air_max and clear_of_material(C, E): break       // L62103-62104 (FUN_18006e470)
            if air > air_max and trimAt == -1: trimAt = T.count (T+0x1c)                  // L62105-62108
         elif air > 1.5*air_max: break                                 // L62110
         mark_air_feed(T, P.flags & 1, E.entity, &E.flags)             // FUN_18008a4a0 L62111
         emit(T, E.entity, type 0x10, f, w1, w2, E+0x130, E+0x268)     // L62112-62114
    elif f & 0x80:       trimAt = -1                                   // committed cut, L62090-62098
         emit(T, E.entity, type firstpass(C,E) | 0x10, f, ...)
         cutSeen = true; air = 0
         if unlimited and cut > P+0x1e0 - 1e-12: break
    else: break                                    // L62090
    pos = npos
  while !last                                                          // L62126
  state = (E.flags & 0x10 == 0 and matIdx != -1) ? 0 : 3              // L62127-62132
  if trimAt != -1: truncate T to trimAt records (FUN_18008ee40 L79086-79099)   // L62133-62135
```
- **unlimited** = (T+0x1c == T+0x1c value saved after the previous state-2 call) (L62704-62707): no record was
  emitted since the last sweep. In that case the limits are FLT_MAX and the sweep stops once one march step of
  committed cut has been made.
- **candidate_path** = FUN_18006caa0 (L61676-61810). It makes a new CSh with G's lead-in entities (G+0x28..0x30, bbox
  copied, L61766-61794). Then FUN_180146e20 appends the boundary section of shape C+0x350[G.shape], from (G.ent, G.t)
  with t clamped to [0, 1], all the way round to (G.ent, G.t - 1e-12). If G.t - 1e-12 < 0, the end is (G.ent-1, 1.0),
  wrapping to the last entity (L61795-61808). So the sweep candidate is **one full loop of the boundary shape starting
  at the attach point**, not a short section between two parameters.
- **advance** = FUN_18006f1c0 (L62971-63070). It walks dist along the shape from (ent, t) and returns the new
  (ent, t, point) and the length moved. With mode bit 1 (as used here) it **stops at the end of the current entity**,
  so a step never spans two entities (L63055). At the end of an open shape (+0x50 == 0) it clamps to t = 1 and returns
  false if more than 1e-16 of the distance remains (L63042-63050). A closed shape wraps to entity 0. Negative dist is
  clamped to 0.
- **commit** = FUN_180035990 (L33361-33389). It removes the step from material set 1 (FUN_180037790 with M1). It sets
  flags |= 0x80, |= 0x40 if w1 > 1e-6, and |= 0x400 if bit 0 of C+0x10 (P flags) is 0, else 0x200. It removes the
  step from material set 2 when mat+0x1b0 is non-empty and M2 flags&0x18 == 0. FUN_180037790 also sets the pass-number
  byte E+0x13c (FUN_180033b60 L32344-32403): when C+0x758 is set, it is 1 + the maximum of byte +9 of the material
  entities involved, saturating at 255.
- **shorten** = FUN_18006dfa0 (L62259-62367). For each circle whose width is >= its limit (w1 >= lim1: circle 1;
  w2 >= lim2: circle 2), FUN_18006e250 (L62370-62436) finds the step fraction u in [0, 1] where
  `width(u) - (lim - 1e-6) = 0`. FUN_18006de20 (L62203-62256) scales the line end or arc sweep by u and re-measures.
  The root finder is **FUN_1800b0200 (Brent's method, tol 1e-7, eps 2e-12)** (L95479ff, L62430-62431). It is used only
  when f(0) and f(1) have opposite signs. The circle-2 search also needs C+0x448 to be non-empty (L62403-62406). If
  both circles give a root, the smaller u wins (L62320-62336). The entity is cut to fraction u. The return is true if
  any root was found. **After a successful shortening the sweep ends** (last = true, L62069).
- **clear_of_material** = FUN_18006e470 (L62472-62537). It takes the nearest point of material C+0x410 to the step end
  (FUN_18013cfe0). Result: true if no material, or if the distance is > (R - r) + 1e-7.
- **firstpass(C,E)** = FUN_18006ee90 (L62855-62865). It returns 0x20 if C+0x758 is set and E+0x13c <= P+0x24c, else 0.
  - C+0x758 = (P+0xa8 first-pass feed reduction > 1e-7) and (P+0x24c >= 1) and !(P flags & 0x20000000) (FUN_180076b10
    L67187-67195).
  - P+0x24c = registry `iNumRedPass`, default 1 (FUN_180069840 L60355-60360; FUN_180068ff0 L60065).
- **mark_air_feed** = FUN_18008a4a0 (L76846-76868). If the entity type byte has none of 1/2/8/0x20, flags bits 0x200,
  0x400, 0x80 and 0x40 are clear (FUN_18008a090 = (byte & 0xc0) != 0, L76710), it sets flags |= 0x800 when bit 0 of
  P flags is set, else 0x1000.

### C.5 Output records (CCetp) and feeds
- FUN_1800870f0 (L75241-75318) writes record number T+0x1c into slot (T+0x1c mod 16) of the **16-slot ring at T+0x40**,
  0xc0 bytes each. Fields: +8 entity (0x88 bytes); entity byte +0x12 (record +0x1a) = move type; +0x90 = flags;
  +0xa0 = w1, +0xa8 = w2, +0xb0/+0xb8 = the two extra values; +0x98 = feed from FUN_18008a0a0. The entity field +0x78
  gets *(C+0x88) added (L75293-75295). If flags & 0x40, T+0x20 = this record index (the last real cut). Then T+0x1c++
  and the record goes to the file (FUN_18008de70, FUN_18008aa50).
- The runaway guard in FUN_18006e620 watches **T+0x20**. It breaks after more than 32 loop iterations without a new
  record carrying flag 0x40 (L62690-62694). Linking (state >= 3) gets `iter <= lastProgress + 16` as its bool argument
  (L62714).
- **Feed per record** (FUN_18008a0a0 L76719-76741 with FUN_18008a190 L76746-76765; T+8 = CCePock, set by FUN_180085ed0
  L74614 from FUN_180011710 L15159):

| condition, in order | feed |
|---|---|
| type & 1 | C+0xb8 = P+0xb0 (retract rate) |
| type & 2 | C+0xc0 = P+0xb8 (back feed) |
| type & 8 | C+0xc8 = P+0xc0 (ramp/entry feed) |
| type & 0x20 (first pass, C.4) | C+0xd8 = P+0xd0 (first-pass feed) |
| flags & 0x1400 | C+0xe0 = P+0xd8 (conventional feed) |
| otherwise | C+0xd0 = P+0xc8 (base feed) |

So conventional feed applies to cut steps committed with P flags bit 0 clear (flag 0x400, C.4) and to sweep air
steps marked 0x1000. Which user setting drives P flags bit 0 is outside this section; dynamic.md lists "0x1" only as a
mode bit.

### C.6 Corrections to formats/dynamic.md ("Algorithm summary" / "Region loop states")
1. The sweep end thresholds use **C+0x1f0 = P+0x1e8 = 1.5 x march step**, not P+0x1f0 = pi x min radius:
   - before any cut in the sweep: stop when air > 1.5 x C+0x1f0 = 2.25 x P+0x1e0;
   - after a cut: stop when air > 3 x C+0x1f0 and the tool is clear of material by more than R - r;
   - air moves after air > C+0x1f0 are removed afterwards (FUN_18008ee40).
   P+0x1f0 = pi x min radius (FUN_180069560 L60262) is not used in states 0-2.
2. The candidate path is the lead-in plus **one full loop** of the C+0x340 boundary shape from the attach point
   (FUN_18006caa0). It is not a two-parameter section.
3. A step whose width exceeds a limit is **shortened to the exact limit point** (Brent root of width = lim - 1e-6), and
   the sweep then ends. It is not "corrected and continued".
4. The "16-entry ring of step records" is the CCetp **output** ring (T+0x40). The ramp's previous width is the last
   emitted record's width.
5. States 0/1 commit their chosen trial step **without** a width test (FUN_18006db90 L62178-62180). Width control
   happens in the candidate choice (FUN_180016cb0). States 0/1 do not use the first-pass offset (P+0x60), the
   approach distance or the back feed directly. Those parameters do not appear in FUN_18006d170, FUN_18006db90 or
   FUN_18006d330 (feeds are chosen per record, C.5); their users are outside this section.
6. Confirmed:
   - the ramp formula, its clamps and its clear condition;
   - state 2 clears the ramp flag at entry;
   - the ramp flag is set after each linking call and at region start;
   - commit sets flag 0x80;
   - sweep cut records have type 0x10, and state 0/1 records type 0x40;
   - the runaway guard of 32 iterations.

### C.7 NOT DECODED (this section)
**Follow-up:** most items below are decoded in §6 — material model §6.1, state 0/1 generators §6.2, list origins
§6.2.0/§6.4.1. What remains is listed in §6.9.
- FUN_18003a140 loop rebuild (which leftover loops are subtracted from the swept area). Also FUN_1800321e0 (swept
  region build), FUN_180034ac0, FUN_180032b30 / FUN_1800331a0 (material gather), FUN_18002fa30, FUN_1800337e0,
  FUN_180031150, FUN_1800354c0, FUN_180035630, FUN_18002fc80, FUN_180035360 (material-present test), FUN_180037070
  (flag 4), FUN_1800380f0 (flag 0x100, the +0x130 value).
- FUN_180037790 material subtraction internals (only its flag/pass-number effects are decoded). → §6.1.7.
- FUN_180029c30 (material window clip). → §6.1.8.
- State 0/1 generators: FUN_18006b170 / FUN_18006b570 (aim at material shape), FUN_18001efb0 (wall placement at
  width; dynamic.md has a partial note), FUN_180018990, FUN_18001a2b0, FUN_18001bb60, FUN_18001c510, FUN_18001c2c0,
  FUN_180028ac0 (material direction). → §6.2 (comparator LAB_180001b9a still NOT DECODED).
- Attach alternatives: FUN_180023460, FUN_180021f70, FUN_180022500, FUN_180023dd0 / FUN_1800241c0 (intersection
  choice). → §6.2.5.
- FUN_180146e20 / FUN_18006f7d0 (shape section copy) are kernel routines, internals not read. FUN_18008e4b0 /
  FUN_18008e710 return the last output point / tangent (read only as far as their callers use them).
- Origin of C+0x340 (boundary set), C+0x410 / C+0x420 (material lists) and C+0x448 (the circle-2 gate). See
  dynamic.md (FUN_180062a70); not re-verified here. → **Resolved in §6.4.1** (C+0x420 / C+0x448 / C+0x350 are begin
  pointers of C+0x410 / C+0x438 / C+0x340).
- Meaning of P flags bit 0 (0x200 vs 0x400 / 0x800 vs 0x1000), and of P+0x21f (used as a refinement switch in state
  0/1 and as "keep ramping").

## 3 D. Entry, linking, exit ("finishing") and CCetp output — mcce.dll

Line numbers are in `decomp/mcce_all.c` unless a file is named. `P+x` is a CDynPockParm offset. The pocket object
(`CCePock`, `this`) embeds the Parm at +8 (`FUN_1800124d0` 15628 copies it with `FUN_180010ef0(this+8, parm)`), so
**`this+X` = `P+(X-8)`**. Flags = `P+0x08` (`this+0x10`).

Conventions used below:
- `pock_chn` entity (0x88 bytes): `+0x08` id ('L','A','P'); `+0x12` **CeFlags byte**; line `+0x18 x1,+0x20 y1,+0x28 x2,
  +0x30 y2`; arc `+0x18 cx,+0x20 cy,+0x28 r,+0x30 sa,+0x38 sw`; `+0x40..+0x58` bbox (min x,y, max x,y; used in
  FUN_180053e80 48935-48955); **`+0x78` Z relative to the cut level** (see 4.2).
- `CSh` shape: entity vector at `+0x18/+0x20`; `+0x50` direction (1 = CCW, -1 = CW, 0 = open; FUN_1800788d0 68262-68267
  writes 1 for positive sweeps); `+0x54` flags; `+0x58` int (meaning NOT DECODED).
- `len(sh)` = `FUN_180121980` (155622-155723): sum of entity lengths (line = chord, arc = |sw·r|).
- Path position `{int shape; int ent; double t; CPt2 pt}`; `FUN_18006f1c0(mode, d, shapes, from, out, walked)`
  (62971-63070) advances `d` along the shape (arc length, t in [0,1] per entity), wraps on closed shapes (`CSh+0x50 != 0`),
  stops at the end of an open shape (returns 0 if distance left > 1e-16), mode bit 1 = stop at the next entity end.
- `FUN_18008e4b0(cetp, &pt)` = end point of the last emitted record; `FUN_18008e710(cetp, &v)` = end tangent of it.

---------------------------------------------------------------------------------------------------------------------
## 1. Entry

### 1.1 Op field -> flag mapping (mcmill `LoadDynPockParm` FUN_1801af380, decomp/mcmill_hst2d_dyn.c 1497-1780)
`param_1` there is `operation` = entity − 0x68, so `op+0x1653` = entity 0x16bb.

| entity byte | value -> P+0x08 flag | lines |
|---|---|---|
| 0x16bb entry method | 1→0x200, 2→0x400, 3→0x800, 4→0x1000, 5→0x8000, any other (0)→0x100 | 1530-1547 |
| 0x16b9 gap retract | 1→0x10000, 2→0x40000, 3→0x80000, 4→0x20000 (0: none) | 1548-1560 |
| 0x16be | ≠0 → 0x100000 (centre helix on point) | 1561-1563 |
| 0x16bd | **==0** → 0x2000 (use plunge angle; else pitch) | 1564-1566 |
| 0x16bf | P+0x20 = 0/1/2 (else assert) | 1567-1579 |
| 0x16bc | ≠0 → 0x40000000 | 1587-1589 |
| 0x1564 (style 2: 0x5d2) | ==0 → flag 0x1 | 1520-1528 |
| 0x1784 bit 1 | → flag 0x4 (and P+0xd8 = conventional feed 0x17d0) | 1594-1596, 1716-1726 |

Entry numbers: P+0x48 ← 0x16d0 micro lift; P+0x98 ← 0x16c8; P+0x50 ← 0x1660 approach distance only when
`(style & 0xfc)==0 && style != 1` (styles 0,2,3; style 1 → 0) (1620-1625); P+0x90 ← 0x16c0; P+0xa0 ← 0x1690;
P+0x68 ← 0x1678·0.01745329251994329; P+0x70 ← 0x1680; P+0x88 ← 0x1688 (1626-1631). Defaults when ≤ tol:
P+0x68 = 0x3f9acee9f37bebd2 (= 1.5°), P+0x70 and P+0x98 = 0.1 in / 2.54 mm (1748-1775).
**P+0x78 / P+0x80 are not set by LoadDynPockParm.** In the PeelMill.cpp caller (decomp/mcmill_hst2d.c 11400-11401,
11489-11490) P+0x78 = `top_of_stock`, P+0x80 = `pock_depth` (current depth). (CHST2D worker path not checked.)

### 1.2 Top level: `FUN_180070130` (63602-63651), called once per region from FUN_18006e620 (62676)
```
entry(this, cetp, &state_out, ...):
  ok = approach_from_outside(this, cetp)            // FUN_180076b10, §1.3
  if !ok:
     if (flags & 0x4000000) { cetp->hdr_p7 |= 1; return 0; }      // 63625, 63632 (cetp+0x28)
     ok = plunge_entry(this, cetp, &entryShapes)                    // FUN_18006fc70, §1.4
     if !ok { cetp->hdr_p6++; return 0; }                           // 63629 (cetp+0x24)
  post_entry_state(this, cetp, &entryShapes, ...)   // FUN_18005bee0, §1.7
  return 1
```
If `FUN_180070130` returns 0 the region is abandoned without exit (FUN_18006e620 62678 → LAB_18006e91a).
(Who sets flag 0x4000000 / 0x8000000 / 0x20000000 / 0x10000000 — not LoadDynPockParm; NOT DECODED. §6.7.5: the
trochoidal entry FUN_18007a150 sets 0x20000000 (and 0x2000000) for its recursive MCCECalcDynPock call, l.69176. dynamic.md lists
0x10000000 as registry bTurnOnMoatFirst.)

### 1.3 Approach from outside — `FUN_180076b10` (67069-67319)
```
if !FUN_180075a00(this, cetp, &apShape, &targets, &ti): this[0x758]=0; return 0     // 67182-67187
firstPass = (P+0xa8 > 1e-7) && (*(int*)(this+0x254) >= 1) && !(flags & 0x20000000)   // 67188-67195
this[0x758] = firstPass                     // also read by CCetp FUN_18008aa50 77065
ce = firstPass ? 0x24 : 0x04                // 67196  (0x20 = first-pass feed class, §4.3)
setCeFlags(apShape, ce)                     // FUN_18005c8e0 53364-53391 sets byte +0x12 of every entity
if !(flags & 0x8000000) && (cetp->count != 0 || entPrev.id == 0 || |entPrev.z - P+0x80| >= 1e-5):  // 67198-67201
    h = P+0x48 (micro lift); if h < 1e-7 && (flags & 0x40000000): h = P+0x240          // 67203-67206
    p10 = (ti >= 0 && targets[ti].flag22) ? targets[ti].d28 : 0                          // 67207-67216
    applyZProfile(type=1 descent, h, p10, apShape)          // FUN_18003d490, §2.4
    setCeFlags(apShape, ce)
S = start point of apShape
if cetp->count != 0:                                       // 67237-67253
    emit L(last→last) z=0, CeFlags 1 ; emit L(last→S) z=0, CeFlags 1      (rapid class, §4.3)
emit L(S→S) z = apShape.ent[0].z, CeFlags ce, moveFlags = (flags&1) ? 0x800 : 0x1000      // 67254-67266
FUN_18008a1e0(cetp, 0, apShape, mf) ; FUN_180087390(cetp, apShape, mf)                    // emit all
```
`entPrev` is the pock_chn at `this+0x120` (= P+0x118, id at this+0x128, z at this+0x198); FUN_1800124d0 15631-15633
fills it via FUN_180012390 from the third input set when the caller did not give one (meaning: previous tool
position; content of FUN_180012390 NOT DECODED here; → §6.4.3).

`FUN_180075a00` (66501-66702): computes the 4 region sets with FUN_180062a70 (NOT DECODED here; → §6.4.2);
none → fail. If `FUN_180077a50` is false: build open-edge candidates `FUN_180075f60` (records 0x108 bytes), take the first
candidate with byte +0x21 set or for which `FUN_180060770` succeeds (66611-66629); if the candidate has +0x22 set the
approach is the section `FUN_180146e20(shape[+0x30], ent +0x34, t +0x38, ent +0x54, t +0x58)` (66636-66644). If
the approach is shorter than **P+0x50 (approach distance)** (`this+0x58`, 66646): `FUN_180077360` extends it backwards
along its start tangent to the boundary of the region set (straight line, 67450-67510) and `FUN_180077b30` keeps only
the last P+0x50 of its length (67720-67744: cut at s = len − P+0x50 when len − P+0x50 ≥ 1e-5).
NOT DECODED here: FUN_180075f60 (candidate generation), FUN_180060770, FUN_180077a50. → **Decoded in §6.4.4.**

### 1.4 Plunge entries — `FUN_18006fc70` (63421-63601)
```
switch on flags (first match):             // 63485-63515
  0x100  -> FUN_180078840  helix only               (§1.5)
  0x200  -> FUN_180078130  helix + medial (help: "full medial burial")
  0x400  -> FUN_180078500  helix + trochoidal medial
  0x800  -> FUN_18007cb70  profile                  (§1.6)
  0x1000 -> FUN_18007b3c0  medial ramp
  0x8000 -> FUN_180074560  custom entry chain
if no entry shape && (flags & 0x8700): FUN_18007cb70 (profile fallback)        // 63516-63519
if no entry shape: return 0
E = entryShapes[0]
if len(E) < P+0x1e0 (march step, this+0x1e8): if !FUN_18007c700(this, E): return 0   // 63525-63529
FUN_180070b60(this, E, cetp)     // remove material swept by the entry (march with step P+0x1e0, 63904-64209)
FUN_1800707b0(this, E, cetp)     // emit (§1.8)
FUN_180071df0(...)               // only if flags & 0x40000000 (64607); emits an extra shape (→ §6.7.9)
return 1
```
`FUN_18007c700` (70057-70141): at the end point Q of E, fails if a circle of radius P+0x1e0 about Q is not inside
`this+0x340` (`FUN_180110120 & 5`); else appends `FUN_1800a6aa0(1e-8, Q, 0.0, P+0x1e0, a0, ±2π)` with a0 = atan2 of the
end tangent, sign + when flags&1 (70113-70133). FUN_1800a6aa0 (presumably a radius 0 → P+0x1e0 spiral) NOT DECODED.
→ §6.7.8: confirmed spiral, radius linear in swept fraction, pieces ≤ π/2.

### 1.5 Helix only — `FUN_180078840` (68086-68104)
Centre C:
- flag 0x100000 and `FUN_180077e70` succeeds (67772-67865): entry points are the pock_chn list at `this+0x658/0x660`
  (P+0x650; source NOT DECODED). `FUN_1801103a0` picks one inside `this+0x340` (index i). Offset `this+0x340` by
  −P+0x90 (`MCCECalcOffset(1, -helixR, …, 0x100, 0, 1e-6, 1e-6)`); if the point is inside it (`FUN_180112e80 == 2`)
  C = the point, else C = nearest point of that offset (`FUN_18013d010`).
- else `FUN_180079050` (68340-68415): medial axis of `this+0x340` (`CalcMedAx(P+0x21d, …, 0.1, 8.0)`), and
  `FUN_1800c5df0` (106892-106983) returns the medial-edge end point with the **largest clearance** (`FUN_1800c5330`;
  edges of type 0/2 use t=+0x28, type 1 uses +0x38/+0x40) → C (and the clearance ρmax, used by Profile).

Helix loop — `FUN_1800788d0` (68107-68337):
```
if !FUN_18007b130(C, rh = P+0x90): no entry      // full circle r=rh about C must be inside this+0x340
                                                  // (FUN_180113300 result & 5 == 0, 69352-69436)
if rh < 1e-7: entry = degenerate line C→C (no helix)            // 68270-68334
else:
  dir = (flags & 1) ? +1 : -1 ;  if (R - rh < -1e-4) dir = -dir   // R = P+0x28 (this+0x30); 68154-68161
  loop = 4 arcs, centre C, radius rh, start angles 0, π/2, π, 3π/2 (3rd wrapped to (-π,π]),
         sweep dir·π/2 each (0x3ff921fb54442d18), CSh+0x50 = dir      // 68218-68267
  ramp(loop) -> entry                                              // FUN_18007bc00, below
```
**Ramp builder `FUN_18007bc00`** (69718-69986) — shared by Helix, Profile, Medial and Custom entries:
```
H  = max(0, (P+0x78 - P+0x80) + P+0x88)        // top − depth + Z clearance   69767-69781
Lc = len(loop); if Lc < 1e-14: nothing
pitch = (flags & 0x2000) ? tan(a)·Lc : P+0x70 ; a = P+0x68, if ||a|-π/2| < 1e-6: a = 0x3ff921fa47d4b30d   69814-69828
X  = H/|pitch| + 1                              // number of loops incl. one flat loop
D  = (X-1)·Lc                                   // descent length
start = FUN_18006f1c0(0, (floor(X+1) - X)·Lc, loop, pos0)    // so the path ends at the loop start  69831
out = [ L(start.pt→start.pt) ]
then section of the current entity from start.t to 1, then whole entities cyclically (ent+1 mod n)
for each appended entity (cumulative length s before it, own length l):
    z_end = (s >= D) ? 0 : max(0, H - (l+s)/D·H)            // 69961-69970
stop when s > X·Lc - 1e-6                                       // 69972
```
So the descent has a constant slope H/D and ends with one full loop at the cut level.

### 1.6 Profile — `FUN_18007cb70` (70167-70212)
```
ρmax = FUN_180079050(...)            // largest medial clearance of this+0x340
loop = MCCECalcOffset(1, -max(0, ρmax - 0.75·(R - r)), this+0x340, mode 1)     // FUN_18007ca70 70154-70159
keep the longest loop only                            // FUN_180123a30 156743-156807
split every arc with |sw| > π/2 in halves until all ≤ π/2      // FUN_180123bc0(π/2) 156874-156990
smooth: FUN_18004ae80(this+0x340, loop)               // §below
direction: FUN_18007cd50                              // §below
ramp(loop) -> entry                                   // FUN_18007bc00 §1.5
```
`FUN_18004ae80` (44487-44787): d0 = P+0x21e ? P+0x1d8 : P+0x40 (44561-44567). Optional section via FUN_180048320
(NOT DECODED here; → §6.7.7). Tries `ChaikinSmoothSh(d, …)` with d = d0, then 0.9·d0, 0.8·d0 … 0.1·d0 (`d = k·d0·0.1`, k=9..1,
44632-44690) [**correction §6.7.7: 0.1·d0 is never tried; last is 0.2·d0**] and keeps the first smoothed loop that stays inside `this+0x340` (`FUN_180113300 & 4 == 0`).
`FUN_18007cd50` (70215-70390): offsets the loop by ±(R − r) (mode 0x100), clips the material `this+0x410` with that band
(`FUN_180153b30`), and for each piece whose start lies inside the loop tests `FUN_180053e80(piece, 34.5)`.
Final direction: **dir = +1 if flags&1 else −1; if any tested piece fails, the opposite** (70327-70355).
`FUN_180053e80(sh, pct)` (48900-48966): true iff the bbox diagonal of sh ≤ 2·(R − r)·pct/100.

### 1.7 Other entry methods (top level only)
- 0x1000 medial `FUN_18007b3c0` (69452-69535): medial path `FUN_18007b6e0` (NOT DECODED here; → §6.7.3); if P+0xa0 (additional slot
  width) ≥ 1e-6: loop = `MCCECalcOffset(1, P+0xa0·0.5, medial, 0)`, longest kept (69484-69488) else the medial path
  itself; smooth `FUN_18004ae80`; direction = flags&1 ? +1 : −1 (RevSh 69505-69508); ramp `FUN_18007bc00`.
- 0x8000 custom `FUN_180074560` (65829-65960): entry chain `FUN_1800752c0`, `FUN_180132200`, `FUN_180074940`, offset
  `MCCECalcOffset(1, P+0xa0, …, 0x105)`, `FUN_180074d50`, direction by flags&1, ramp `FUN_18007bc00` (NOT DECODED inside here; → §6.7.6).
- 0x200 `FUN_180078130` (67870-67985): medial path `FUN_180079260`; helix centre = start of it (or max-clearance point
  when none, 67921-67940); helix `FUN_1800788d0`; join with `FUN_18011f2e0`, `FUN_18011d800`, `FUN_180079570`,
  `FUN_180146b10` (NOT DECODED here; → §6.7.2, §6.7.4).
- 0x400 `FUN_180078500` (67987-68084): `FUN_180079260`, helix at max-clearance point, then `FUN_18007a150` and
  `FUN_180081ab0` (trochoidal part, NOT DECODED; P+0x98 trochoid loop radius is expected there, not verified).
  → §6.7.5: structure and every P+0x98 use decoded; leaf builders still NOT DECODED.

### 1.8 Entry emission — `FUN_1800707b0` (63809-63901)
```
S = start of E
if cetp->count != 0: emit L(last→last) z=0 CeFlags 1 ; emit L(last→S) z=0 CeFlags 1   // 63853-63866
if first entity of E has length² > 1e-10: emit L(S→S) z = E.ent[0].z CeFlags 8         // 63867-63886 (plunge to start Z)
emit every entity of E with CeFlags 8 (entry/ramp feed)                                  // 63888-63899
```

### 1.9 State after entry — `FUN_18005bee0` (53064-53158)
```
p = FUN_18005bca0: if exactly one entry shape: among remaining-material pieces (this+0x420) that lie inside it
    (FUN_180113300 == 2) take the one with the nearest point to the current position (FUN_18013d200);
    otherwise nearest point of the material this+0x410 (FUN_18013d010)         // 52997-53061
if none: state = 0 ; sweep pos = p
else:
   if | |p.pt - cur| - (R - r) | > 1e-5: general link FUN_1800653d0 (§2.2); return     // 53114-53119
   q = nearest point of this+0x340 to cur ; onB = |q - cur| < 1e-7
   small = FUN_180053e80(piece p, 35.0)
   state = onB ? 2 : (small ? 1 : 0) ; sweep start = q                                  // 53121-53156
```

---------------------------------------------------------------------------------------------------------------------
## 2. Linking (region-loop state ≥ 3)

### 2.1 Dispatcher `FUN_180067ce0` (59300-59314)
`near` = `iter <= lastNewIter + 0x10` (FUN_18006e620 62714; `lastNewIter` = iteration at which `cetp+0x20` — index of
the last record with move flag 0x40, set in FUN_1800870f0 75307-75309 — last changed, 62690-62693; the loop also stops
after `lastNewIter + 0x20` iterations, 62694).
```
if this[0x242]==0 (P+0x23a bIsAirCutV3) && near && local_link(): return     // FUN_180067d80
general_link()                                                             // FUN_1800653d0
```

### 2.2 Local link `FUN_180067d80` (59317-59664) and general link `FUN_1800653d0` (57949-58207)
Both: `*state = 3`; if `FUN_1800653b0` (this[0x710] && this[0x728] == cetp+0x20) push `this+0x718` to list `this+0x740`.
Local:
```
k = FUN_1800686c0:  mode = FUN_18006e3f0 (= 1 if flags&0x10000000 and this+0x350[0]->+0x90 bit 4, else P+0x20)
     mode 1 or 2: remaining piece (this+0x420) nearest to the current point (FUN_18013caf0)
     else       : remaining piece with the largest CSh+0x58                             // 59667-59752
targets via FUN_180066320 → FUN_180066ff0 → FUN_180066ad0 (NOT DECODED here; → §6.3.6)
link = FUN_180061cd0(..., allowLast=0) ; if none → fail (general link)
if FUN_180064180(link, cross): link = FUN_180062140 (retract link)                       // 59515-59527
```
General: region sets `FUN_180062a70` (→ §6.4.2); targets `FUN_180052a90` (NOT DECODED here; → §6.3.6); `link = FUN_180061cd0(..., allowLast=1)`;
if none or `FUN_180064180` → `FUN_180062140` (58097-58114).
Then (both): `FUN_18008a1e0(cetp, (flags>>2)&1, link, mf)`; `FUN_180087390` emits; target bookkeeping; next state
`FUN_180065ff0` (58378-58456):
```
no target          : state 0, clear sweep memo (this+0x710=0, +0x718/+0x720=0, +0x728=-1)
target.flag22 == 0 : state = target.byte20 ? 1 : 0
target.flag22 != 0 : state 2, sweep position = target +0x50..+0x6f
memo: this[0x710]=1, this+0x718 = target[0..0xf], this+0x728 = cetp+0x20
```

### 2.3 Retract decision `FUN_180064180` (57308-57339)
len = len(link), cross = boundary-crossing value returned by FUN_180061cd0 (`param_11`), gap = P+0xe0 (`this+0xe8`):
| flag | retract when |
|---|---|
| 0x10000 | cross > 1e-5 |
| 0x40000 | cross > 1e-5 **or** len > gap |
| 0x80000 | cross > 1e-5 **and** len > gap |
| 0x20000 | len > gap |
| none | never |
(Same as the table in formats/dynamic.md.)

### 2.4 Stay-down link `FUN_180061cd0` (56068-56157)
Try in order until a non-empty path: (1) precomputed path of target 0 (`target+0x70` shape) ; (2) if !(flags & 0x4):
`FUN_18005ff90` ; (3) `FUN_180044870` (sets `cross`) ; (4) `FUN_18005d5f0` ; (5) if allowLast: `FUN_18005f4c0`.
CeFlags 4 on all entities (56139). Unless the path came from (1) or (2): Z profile **type 2** with
h = P+0x48 (micro lift), p10 = target.flag22 ? target.d28 : 0 (56141-56154).
Strategies (2)-(5): NOT DECODED here (55114-55368, 40883-41346, 53770-53990, 54744-54972). → **Decoded in §6.3.2-6.3.5.**

**Z profile `FUN_18003d490`** (36948-37016): struct {int type; h; p10; Lr; Lr}; Lr = P+0x1f0 (= π·P+0x40, this+0x1f8,
36988-36989); if h ≤ 1e-7 the Z values are not written (36981-36987). Breakpoints (s along path, z):
- type 0 lift `FUN_18003da50` (37200-37365): (0,0), (Lr,h) if Lr < L−1e-4, (L,h).
- type 1 descent `FUN_18003d670` (37019-37199): a = clamp(L−p10), b = clamp(L−p10−Lr) to [0,L];
  (0,h), (b,h) if b > 1e-4, (a,0) if a < L−1e-4, (L,0).
- type 2 lift+descent `FUN_18003dd90` (37366-37585): u = clamp(Lr), a = clamp(L−p10), b = clamp(L−p10−Lr);
  if b < u: u = b = L/2; if a < b: a = b; (0,0), (u,h), (b,h) if |u−b| > 1e-4, (a,0) if a < L−1e-4, (L,0).
Applied by `FUN_18003e2f0` (37586-37898): path split at the breakpoints, each entity's end z linearly interpolated
along arc length; entities whose both end z are within 1e-7 of h get **CeFlags |= 2 (back feed)**; all get |= 8 if the
path's first entity already had 8, else |= 4; type 1 prepends a zero-length line at the start with z = first z.

### 2.5 Retract link `FUN_180062140` (56160-56516)
```
exit  = FUN_18005eca0(...)  CeFlags 4 ; Z profile type 0, h = P+0x48            // 56343-56348
appr  = FUN_180060770(target 0) or its section (target+0x22); if len < P+0x50: FUN_180077360 + FUN_180077b30 (§1.3)
        CeFlags 4 ; Z profile type 1, h = P+0x48, p10 = target.flag22 ? target.d28 : 0   // 56350-56379
trav  = L(exit end → appr start), CeFlags 1                                        // 56380-56469
link  = exit + trav + appr
```

### 2.6 Exit move `FUN_18005eca0` (54451-54741)
Lmax = 8·P+0x1c8 (`FUN_180064980` 57642). P = last point, T = last tangent.
- If P is inside the clip set (`FUN_180112e80 == 2`): straight line P → P + T̂·d, d = P+0x21e ? P+0x1d8 : P+0x40,
  trimmed at the first crossing of that set (`FUN_180134690`) (54588-54629).
- Else candidates (ρ, θ) from `FUN_180064980` (57612-57930), in this order:
  if P+0x21e and P+0x1d8 − P+0x40 > 1e-5: ρ ∈ {ρv, m+0.75(ρv−m), m+0.5(ρv−m), m+0.25(ρv−m)} (ρv = P+0x1d8, m = P+0x40);
  then ρ = m; each via `FUN_18005c440` (53211-53363) with θ ∈ {f(ρ), π/4, 5π/16, 3π/8, 7π/16, π/2};
  then (0.75m, f), (0.5m, f), (0.25m, f), (0.1m, f), (0.1m, π/2), (−1, −1).
  f(ρ) = min(π/4, acos(clamp((ρ − 0.25·P+0x1c8)/ρ, −1, 1))), −1 if ρ < 1e-12 (`FUN_1800636d0` 56898-56919).
  Two passes (54633-54666): dir = (flags&1), inverted if flags&4; pass 2 uses the opposite dir.
  Candidate path `FUN_18005dd40` (53991-54253): (ρ,θ) ≤ 0 → line to nearest boundary point; else tangent arc from P,
  centre P + ρ·(−T̂y, T̂x) when dir else P + ρ·(T̂y, −T̂x), sweep +θ when dir else −θ, followed by a tangent line of
  length Lmax, trimmed at the first crossing of the clip set (fails if none). Accept the first candidate that stays
  inside the containment set (`FUN_180113300 & 4 == 0`), has length ≤ Lmax, and is not the last (−1,−1) one in pass 1.
(The clip / containment sets are outputs 4 and 2 of FUN_180062a70; NOT DECODED here; → §6.4.2.)

---------------------------------------------------------------------------------------------------------------------
## 3. "Finishing" `FUN_1800688e0` (59755-59962) is the final exit, not a clean-up pass
```
unless (flags & 0x10000000 and first shape of this+0x350 has +0x90 bit 4):
   sets = FUN_180062a70(...)
   exit = FUN_18005eca0(...)  CeFlags 4 ; Z profile type 0 (lift to P+0x48)     // 59877-59883
   FUN_18008a1e0(cetp, 0, exit, mf) ; emit ; FUN_180085d50 (= FUN_18008aa50(cetp,0,0,0): close first-pass tracking)
```

---------------------------------------------------------------------------------------------------------------------
## 4. Output (CCetp, Cetp.cpp / CetpFile.cpp)

### 4.1 File
Open `FUN_180085ed0` (74588-74640): 16-slot ring of 0xc0-byte records (`cetp+0x40`); temp file `GetTempFileName(…,"~CE")`
when no path (`FUN_18008b7d0` 77538-77575); write header. Close `FUN_180085ca0` (74519-74540): seek 0, rewrite header.
Header (0x30 bytes, `FUN_18008e060` 78592-78629; reader `FUN_18008d230` 78254-78329 checks the first three):
| off | type | value |
|---|---|---|
| 0x00 | u32 | 0x4543 ("CE") |
| 0x04 | u32 | 8 (version) |
| 0x08 | u32 | 0x30 |
| 0x0c | i32 | record count (cetp+0x1c; 0 at open) |
| 0x10 | f64 | P+0x28 (R) |
| 0x18 | f64 | P+0x30 (r) |
| 0x20 | u32 | P+0x08 flags |
| 0x24 | i32 | cetp+0x24: regions whose plunge entry failed (§1.2) |
| 0x28 | u32 | cetp+0x28: bit 0 = a region was skipped because flag 0x4000000 (§1.2) |
`MCCEReadDynPockHd(file, &count, &R, &r, &flags, &p24, &p28)` (3685-3701).

Record (`FUN_18008de70` 78554-78589; reader `FUN_18008ca20` 78028-…): pock_chn (0x88) · u32 move flags · f64 feed ·
4×f64 (march values: emit args 5-8, e.g. step widths `+0x128/+0x260/+0x130/+0x268` from state 2, 62093) · two CSh
arrays (`FUN_18008dc90` 78505-78551; source of these two arrays at the emit call NOT DECODED — the decompiler shows
only 4 args in FUN_1800870f0 75313) · f64 file offset of the record (vtable+0x28 before writing).

### 4.2 `FUN_1800870f0` emit (75241-75318)
```
rec = ring[count % 16]; rec.ent = *ent; rec.ent.CeFlags = ce
rec.ent.z += P+0x80                      // this+0x88: absolute Z = cut level + relative z   75293-75295
if 'A': FUN_1800874e0 — arc whose chord < 1e-4 becomes a line (75357-75420)
rec.moveFlags = mf ; rec.d[4] = args 5-8
rec.feed = feedFor(ent, mf)              // FUN_18008a0a0
if mf & 0x40: cetp+0x20 = count
count++ ; write ; FUN_18008aa50 (first-pass length bookkeeping, active only when this[0x758])
```
`FUN_180087390` (75321-75354) emits all entities of a shape with their own CeFlags and the per-entity move flags.
`FUN_18008a1e0(cetp, split, sh, mf[])` (76768-76841) + `FUN_18008a4a0` (76844-76868): for each entity with no CeFlags
bit 1/2/8/0x20 and mf without 0x400/0x200/0x80/0x40: mf |= climb ? 0x800 : 0x1000, climb = flags&1, inverted for the
entities before the first CeFlags-2 entity (or the first half if none) when `split` (= flags&4 for links).

### 4.3 Feed selection `FUN_18008a190` (76744-76765) → `FUN_18008a0a0` (76717-76741)
| test (first match) | class | feed |
|---|---|---|
| CeFlags & 1 | 1 | P+0xb0 (retract rate) |
| CeFlags & 2 | 2 | P+0xb8 (back feed) |
| CeFlags & 8 | 3 | P+0xc0 (ramp / entry feed) |
| CeFlags & 0x20 | 4 | P+0xd0 (first-pass feed) |
| mf & 0x1400 | 5 | P+0xd8 (conventional feed) |
| otherwise | 6 | P+0xc8 (base feed) |
"Cutting" test `FUN_18008a090` (76708-76714): mf & 0xc0.
CeFlags values seen: 1 rapid/retract traverse, 2 back feed at full lift, 4 link/approach/exit, 8 entry ramp,
0x10 (state-2 march step, 62093/62112), 0x20 first pass, 0x40 (state 0/1 step, 62184).
mcmill (ProcessDynamicMillExt, decomp/mcmill_hst2d_dyn.c 3185-3390) reads count from the header, each record's
pock_chn + feed; bit 1 of CeFlags (`bStack_316 & 1`, 3204) switches to its own retract/plunge handling (FUN_1801afb70 3221 /
FUN_1801afb60 3239), bit 3 (0x8) is tracked as entry; Z = ent+0x78 (`dStack_2b0`); arcs with a Z change are linearised
(`ApxArcWithLnSegs` 3284) with Z interpolated linearly over the pieces (z_i = z0 + i/n·(z1 − z0), line 3320).

---------------------------------------------------------------------------------------------------------------------
## NOT DECODED (address — role as far as visible)
**Follow-up:** see §6 for each item (region sets §6.4, approach §6.4.4, links/targets §6.3, entry internals §6.7);
remaining gaps in §6.9.
- FUN_180062a70 — builds the 4 region sets (feasible centre region, containment, clip) used by entry/link/exit. → §6.4.2.
- FUN_180075f60, FUN_180060770, FUN_180077a50 — open-edge approach candidates / validation. → §6.4.4.
- FUN_180012390 — fills the previous-position entity this+0x120. → §6.4.3.
- FUN_18007b6e0, FUN_180079260 — medial-axis entry paths; FUN_18007a150, FUN_180081ab0 — trochoidal medial;
  FUN_180079570, FUN_18011f2e0, FUN_18011d800 — helix/medial join; FUN_1800752c0, FUN_180074940, FUN_180074d50,
  FUN_180132200 — custom entry chain. → §6.7 (except FUN_180132200 and the trochoidal leaves).
- FUN_1800a6aa0 — spiral/arc appended to short entries. → §6.7.8.
- FUN_180071df0 → FUN_1800721a0 / FUN_180072b40 / FUN_1800737e0 — extra move after entry when flag 0x40000000.
  → §6.7.9 (circle path decoded; FUN_180072b40 / FUN_1800737e0 still NOT DECODED).
- FUN_180048320 — section selection before Chaikin smoothing. → §6.7.7.
- Stay-down link strategies FUN_18005ff90, FUN_180044870 (also the `cross` value), FUN_18005d5f0, FUN_18005f4c0;
  target builders FUN_180066320, FUN_180066ff0, FUN_180066ad0, FUN_180052a90. → §6.3 (target leaves still NOT DECODED).
- First-pass feed bookkeeping FUN_180089cd0, FUN_18008a510, FUN_180086860 (CCetp+0x68 list).
- Producers of flags 0x4000000, 0x8000000, 0x20000000; meaning of CSh+0x58, target fields +0x20/+0x22/+0x28.
  → CSh+0x58 §6.1.7; target fields §6.3.0; 0x20000000 §6.7.5.
- The two CSh arrays in each record (FUN_18008dc90 args).

---------------------------------------------------------------------------------------------------------------------
# 4. Dynamic entry methods (op 0x16bb → CDynPockParm+0x08 flags 0x100..0x8000)
Fully covered in **§3 D §1** above: mapping from op field 0x16bb (mcmill `LoadDynPockParm`, mcmill_hst2d_dyn.c
l.1530-1547), dispatcher FUN_18006fc70 (l.63421-63601; Profile is the fallback for mask 0x8700), helix FUN_180078840,
shared ramp builder FUN_18007bc00, profile FUN_18007cb70, approach from outside FUN_180076b10 (tried first).
Medial / trochoidal / custom entry internals are NOT DECODED (listed in §3 D NOT DECODED). → **§6.7.**

---------------------------------------------------------------------------------------------------------------------
# 5. Corrections to formats/dynamic.md found by this pass (each cited in the sections above)
- Sweep-end air thresholds use C+0x1f0 = **P+0x1e8 = 1.5 × march step** (FUN_18006d330 l.61968, 62103-62110), not
  P+0x1f0 (π × min radius). P+0x1f0 is the stay-down link ramp length (§3 D 2.4).
- The march step is 0.1·(R−r)·k with k linear from 0.61086515 (10°) to 1.0 (1 rad) (FUN_180068e10 l.59977-59987); the
  2·rl·sin(a/2) chord caps it **only while `bIsBullnoseAvailA` (P+0x21f) is set** (FUN_180069350 l.60161-60200).
- The state-2 candidate path is **one full loop of the boundary shape from the attach point** (FUN_18006caa0
  l.61676-61810), not a short section; an over-limit step is shortened to the limit point by Brent's method
  (FUN_18006dfa0 / FUN_1800b0200) and the sweep then ends.
- The 16-entry ring of 0xc0-byte records is the CCetp output ring (CCetp+0x40), not a measurement ring.
- States 0/1 commit their step without a width test (FUN_18006db90).
- FUN_1800688e0 is the final exit move + lift, **not** a clean-up pass.
- Without registry access `bUseRateOfEng` (P+0x23b) stays 0 (defaults FUN_180068ff0 l.60063): no engagement ramp.
- Registry `dStkPlaceErr` (if ≥ 0) overrides the first-pass offset P+0x60 (FUN_180069840).
- Area Mill does not use MCCECalcStdPock or MCCECalcDynPock (§0).

# 6. Follow-up decode (second pass)

Second pass over `decomp/mcce_all.c` only (no Ghidra). Same rule as the rest of the file: every formula, constant and
branch cites a function address and line range in `decomp/mcce_all.c`; anything not read is **NOT DECODED**. Where this
section contradicts an earlier one, the earlier text is kept and a pointer to the correction is added there; the
corrections are collected in §6.8.

Contents: 6.1 material model (FUN_180037790, FUN_18003a140 and the measurement chain) · 6.2 state 0/1 trial-step
generators · 6.3 stay-down link strategies and targets · 6.4 region sets FUN_180062a70 and CCePock list origins ·
6.5 Dynamic Contour band kernel · 6.6 PeelMotMat output and FUN_1800942b0 · 6.7 entry internals (medial, trochoidal,
custom) · 6.8 corrections to earlier sections · 6.9 what is still NOT DECODED.

## 6.1 Material model

All line numbers are `decomp/mcce_all.c`. RG = CCePock+0x298 (region object, §3 B.6); `*(RG+0x470)` = CCePock*, so
"C+x" below is read through RG+0x470. Entity = `pock_chn` (0x88 bytes: +0x08 id 'L'/'A'/'P', +0x09 **pass byte**,
+0x0c int, +0x12 CeFlags byte, line +0x18..+0x30, arc +0x18 cx,+0x20 cy,+0x28 r,+0x30 sa,+0x38 sw, +0x40..+0x58 bbox,
+0x80 int). The material is a plain list of closed CSh loops (`CVe<CSh*>`): **no grid, no pixel map, no
boolean kernel**. Each committed step replaces the loops it crosses by new loops built from the kept pieces of the
old loops plus pieces of the swept-region outline (6.1.6).

### 6.1.0 Data layouts decoded in this pass
**CMmCut measurement block M** (M1 = E+8, M2 = E+0x140; offsets relative to M; E offset = M offset + 8 for M1):
| M off | content | writer |
|---|---|---|
| +0x08 | step entity (pock_chn) | caller |
| +0x98 | step length (L chord, A \|r·sw\|) | FUN_180030de0 L30675-30687 |
| +0xa0 | `CVe<CSh*>` swept region (1 shape) | FUN_1800321e0 L31198-31571 |
| +0xc8 | area of the swept region, tol 0.001 | L30690-30691 |
| +0xd0 | `CVe<ISEC>` crossing records (0x50 each) | FUN_180032b30 / FUN_1800331a0 |
| +0xf8 | `CVe<CPt2>` engagement points (begin +0x108) | FUN_18002fc80 L30508; cleared on air L30720 |
| +0x120 | width = removed area / length | L30711-30717 |
| +0x128 | total engaged sweep on the end-cap circle (rad) | FUN_18002fc80 L30183, L30514 |
| +0x130 | flags (2, 4, 8, 0x10, 0x100; 0x80/0x40/0x200/0x400 set by commit) | |
| +0x134 | pass byte | FUN_180037790 / FUN_180033cf0 |
(Correction to §3 C.0: E+0x130 / E+0x268 = M+0x128 = engaged sweep, written by **FUN_18002fc80**, not by
FUN_180037070/FUN_1800380f0.)

**ISEC record (0x50)**, built in FUN_180032b30 L32013-32026 / FUN_1800331a0 L32177-32190, marker form FUN_180033b30
L32330-32343:
| off | content |
|---|---|
| +0x00/+0x08 | crossing point |
| +0x10 | material shape index |
| +0x14 | material entity index (−1 in a marker) |
| +0x18 | t on the material entity |
| +0x20 | swept-shape entity index (−1 in a marker) |
| +0x28 | t on the swept entity |
| +0x30 / +0x34 | next / previous record in swept-outline order (−1 in a marker) (FUN_180035630) |
| +0x38 | class of the material section from this crossing to the next crossing on the same material shape: 0 = outside the swept region (kept), 2 = inside (removed), 1 = visited by a loop builder |
| +0x3c | byte: 1 = **marker** (a material shape with no crossing; the record stands for the whole shape) |
| +0x40 / +0x44 / +0x48 | after commit: new material shape index / entity index / t (FUN_180036f70 L33997-34020) |
Helpers (all on one ISEC list, records are grouped by material shape, ascending entity, see 6.1.3):
`nextSame(i)` FUN_180034720 L32684-32712 (next record of the same shape, cyclic); `prevSame(i)` FUN_180034890
L32737-32764; `nextClass0(&i)` FUN_1800347e0 L32715-32734 (smallest index > i with class 0);
`posA(i)` FUN_1800349a0 L32767-32793 (ent,t of i; marker → (0, 0.0)); `posB(i)` FUN_180034a00 L32796-32827 (ent,t
of i; marker → (nEnt−1, 1.0)); `chain(i)` FUN_1800346a0 L32661-32681: `j = nextSame(i); k = rec[j].+0x30; while k != j
and class[k] == 2: k = rec[k].+0x30`.

### 6.1.1 Swept region of one step — FUN_1800321e0 (L31198-31571), line case FUN_180032200 (L31574-31767)
Inputs: flags (C+0x10), rad (R−r for M1, R for M2; FUN_180030be0 L30622/L30646), step entity, output list (1 CSh,
entity vector reset to 4 entities, FUN_180027160).
**Line S→E** (FUN_180032200): d = |E−S|; if d < 1e-12 → fail (flag 8 in the caller, L30730). n = rad·(dy, −dx)/d
(right normal, L31699-31700).
```
e0 = A(centre S, r=rad, sa=atan2(dx,−dy) [direction of the left normal], sw=+π)   // L31716-31726
e1 = L(S+n → E+n)                                                                    // L31727-31735
e2 = A(centre E, r=rad, sa=atan2(−dx,dy) [right normal], sw=+π), CeFlags byte = 1    // L31736-31743
e3 = L(E−n → S−n)                                                                    // L31744-31754
```
**Arc** (centre c, radius ρ, sa, sw; S/E = arc start/end):
- sw > 0 (L31446-31557): e0 = A(S, rad, sa+π, π); e1 = A(c, ρ+rad, sa, sw); e2 = A(E, rad, sa+sw, π), CeFlags 1;
  e3 = A(c, ρ−rad, sa+sw, −sw). If ρ ≤ rad (L31481-31487): e3.r = rad−ρ, e3.sa = sa+sw+π; intersect e0 with e2
  (`CalcEntIntersect`, tol 1e-6; 0 hits → fail L31493-31495); with 2 hits keep the one nearer to the mid-angle point
  of e3 (L31497-31513); e2 is cut at its parameter (L: lerp, A: sw·t, L31515-31525); a parameter > 1 is first remapped
  `t = (wrap(t·sw) ∓ 2π)/sw` (L31526-31539); e0 start is trimmed (A: sa += sw·t, sw −= sw·t, L31551-31555); entity 3
  is erased (FUN_180011f40(sh, 3, 1), L31559).
- sw ≤ 0 (L31341-31444): e0 = A(S, rad, sa, π); e1 = A(c, ρ−rad, sa, sw); e2 = A(E, rad, sa+sw+π, π), CeFlags 1;
  e3 = A(c, ρ+rad, sa+sw, −sw). If ρ−rad < 0 (L31377-31378): e1.r = rad−ρ, e1.sa = sa+π; intersect e0/e2 as above,
  e0 end cut (L31404-31415), e2 start trimmed (L31416-31443); entity 1 erased.
- (The `+π` angle wraps at L31321-31327 write a dead variable; the stored angles are NOT wrapped.)
Common tail (L31560-31570 / L31755-31764): CSh+0x50 = 1, CSh+0x54 = 1, bbox; **if (flags & 1) == 0**: rotate the
entity list to start at entity 1 (FUN_18014fd50(1, sh), L180573-180715: entities [k..n) then [0..k)) and `RevSh`.
The outline is a stadium (line) or annular sector with round ends; the end cap at E is the entity carrying CeFlags 1.

### 6.1.2 Point classifier frame — FUN_180034ac0 (L32830-33121) and FUN_180035b40 (L33392-33487)
Frame SF (0x118, zeroed by FUN_18002f330 L29863-29896); dir = side argument (0, +1, −1; §3 C trial_step).
SF+0 type, +0x08 S, +0x18 E, +0x28 rad², +0xa0 (rad+1e-4)², +0xa8 (rad−1e-4)², +0x88 = 1e-4.
- Line (L32866-33021): +0x30 = |SE|, +0x38 = 2·rad; o = rad·(−dy, dx)/|d| if dir == 1 else rad·(dy, −dx)/|d|
  (fail if |d| < 1e-6, L32947); +0x40 = CTMatrix = TranslationMx(−(S+o)) · RotationMx(·) · (ReflectionMx if dir == 1)
  (L32956-32979). The rotation angle, lost in the decompile (it shows a discarded `atan2` and `RotationMx(-π)`), was read
  from the disassembly (2026-11-07, mcce.dll 0x180034d7c-0x180034e4b): a = atan2(−o.y, −o.x) if |o.x| > 1e-6 or
  |o.y| > 1e-6, else 0; θ = π/2 − a (constant 0x18017e380), then θ += 2π if θ < −π, θ −= 2π if θ > π; RotationMx(θ).
  ReflectionMx is called with AxOfRef = dir (= 1): m00 = −1, i.e. x → −x (ReflectionMx L10455-10495). CTMatrix is row-vector:
  TranslationMx puts the shift in row 2 (+0x30/+0x38, L1379-1395), RotationMx(θ) = [cos sin; −sin cos] (L11801-11815), so
  p' = p·M is a CCW rotation by θ; `M *= X` is M = M·X (L3414ff); `M * p` = (y·m10 + x·m00 + m20, x·m01 + y·m11 + m21)
  (L5284-5302). So the frame is: origin S+o, the rotation takes −o to +y and the step to +x (dir ≠ 1) or −x (dir = 1, then
  mirrored to +x): **x along the step, y across from the o side, body in 0 ≤ y ≤ 2·rad** (what the rectangle test of
  FUN_180035b40 implied). +0x90 = −1e-4; +0x98 = −1e-4, or −2e-4 when dir = ±1
  (L32996-33015); +0xb0/+0xb8 = len ± 1e-4; +0xc0/+0xc8 = 2rad ± 1e-4 (L33016-33021).
- Arc (L33023-33118): +0xf0.. = (cx, cy, ρ, sa wrapped to [−π, π], sw); +0xd0/+0xd8 = (ρ+rad ± 1e-4)²;
  +0xe0/+0xe8 = sgn·(ρ−rad ± 1e-4)² (signed square, negative when the base is negative).
`classify(SF, p)` → 2 inside, 1 on the boundary band, 4 outside:
```
dE2=|p−E|², dS2=|p−S|²;  if dE2 < (rad−1e-4)² or dS2 < (rad−1e-4)²: return 2          // L33414-33424
Line: q = M·p
   if 1e-4 < q.x < len−1e-4 and 1e-4 < q.y < 2rad−1e-4: return 2                         // L33430-33432
   if dE2 ≤ (rad+1e-4)² or dS2 ≤ (rad+1e-4)² or (−1e-4 < q.x < len+1e-4 and SF+0x98 < q.y < 2rad+1e-4): return 1  // L33433-33439
   return 4
Arc: d2 = |p−c|²; if not (SF+0xe8 ≤ d2 ≤ SF+0xd0): return 4                              // L33442-33445, L33484
   strict = !(d2 ≥ SF+0xd8 or d2 ≤ SF+0xe0)
   a = wrap(atan2(p−c) − sa) to [−π,π]; if sw·a < 0: a ±= 2π (to the sign of sw)          // L33453-33472
   if |sw| ≤ |a|: return (dS2 > (rad+1e-4)² and dE2 > (rad+1e-4)²) ? 4 : 1               // L33473-33477
   return strict ? 2 : 1                                                                   // L33478-33481
```

### 6.1.3 Crossings — FUN_180032b30 (L31894-32056, full material) / FUN_1800331a0 (L32059-32219, window)
```
out = []; B = bbox of swept shape (CSh+0x30..+0x48)
for each material shape s, each entity e (index k):                       // window: s,k = e.+0x80, e.+0x0c
   if bbox(e) overlaps B within 1e-7:                                      // L31986-31987
      for each swept entity w (index m) whose bbox overlaps bbox(e) within 1e-7:
         n = CalcEntIntersect(1e-5, e, w, tE[2], tW[2], pt[2])            // L32009-32012
         for h = n..1: out += {pt[h-1], s, k, tE[h-1], m, tW[h-1], +0x30=-1, +0x38=0, +0x40=-1}  // L32013-32029
      qsort the records just added for e with comparator LAB_180002969   // L32037-32043 (jmp FUN_1800314a0: ascending shape +0x10, entity +0x14, t +0x18)
```
`FUN_18002fa30` (L30045-30094): for every material shape index without a record, append a marker (FUN_180033b30)
with +0x10 = that index (the list stays grouped by shape). The append is FUN_180039c70 (L35401-35460) = `push_back`:
the markers come **after** all crossing records, in ascending shape order (a cursor over the crossing records, which are
sorted by shape, decides "has a record"; it is never advanced past an inserted marker). So `nextClass0` (measure loop
rebuild and commit) meets the crossed shapes first and the untouched ones last; the commit's new material list is
[loops of crossed shapes…, untouched shapes…] (2026-11-07).
The window variant FUN_1800331a0 is called by FUN_180030de0 (L30695-30701) whenever the window list (RG+0x1c8 for set 1,
RG+0x1f0 for set 2) holds a shape; it walks the window shape's entity copies in their order (= source shape, entity order),
the swept bbox from `**(param_3+0x10)` +0x30..+0x48, the entity bbox from the copy's +0x40..+0x58.

### 6.1.4 Section classification and linking
- **FUN_1800337e0 (L32222-32327)** — for each record i, j = nextSame(i). The material section from posA(i) to posB(j)
  (entity count `nEnt−entA+entB+1` when it wraps, i.e. entB < entA, or equal with tB < tA, or i == j; else
  `entB−entA+1`, L32277-32285) is sampled by the iterator FUN_180148820 (L176621-176703): per entity at fractions
  1/3, 2/3 and 1 of [tStart, tEnd] (tStart = tA on the first entity else 0, tEnd = tB on the last else 1), the end
  sample of the last entity is skipped; point via FUN_180021580. Decision (L32313-32320):
  first sample with classify = 2 → class 2; first with 4 → class unchanged (0); all samples 1 (or no sample) →
  if marker: class 2; elif i == j: make i a marker, class 2; elif |pt_i − pt_j|² < 1e-8: delete the record of the
  pair with more t values outside [0,1] (FUN_180035f30 L33490-33525) and continue; else class 2 (L32288-32310).
- **FUN_180031150 (L30735-30771)**: for each non-marker i, p = prevSame(i): p == i → marker; class(i) == class(p) →
  delete i (adjacent sections with the same class merge).
- **FUN_1800354c0 (L33191-33244)**: if any class-2 record lies on a non-island material shape (CSh+0x54 bit0 clear)
  → nothing. Otherwise every class-2 **marker** on an island shape → class 0.
- **FUN_180035630 (L33247-33358)**: m = indices of the non-marker records, `qsort_s` with comparator LAB_180005c72
  (context = the record array; jmp FUN_180031280: ascending swept entity +0x20, then t on it +0x28 - the records in
  swept-outline order); then rec[m[k]].+0x30 = m[(k+1) mod n], .+0x34 = m[(k−1) mod n].
- `FUN_180035360` (L33124-33152): true iff some record has class 2.

### 6.1.5 Removed area — FUN_18003a140 (L35581-35758), work area W = RG+0x3d8 (FUN_180030be0 L30624)
W+0x08 = `CVe<ISEC>` copy, W+0x30 = `CVe<CSh*>` loops, W+0x58 = pool of reusable CSh.
```
pool += loops (AppendByMove)                                     // L35615
W.isec = copy(isec); swap class 0 <-> 2 (FUN_18003a910 L35811-35840) // L35617-35634
i = -1
while nextClass0(&i):                                            // L35635, L35754
   if rec[i] is a marker: class[i] = 1; continue                  // L35650, L35752 (markers never form loops)
   L = new loop (last pool entry, cleared, or a new CSh)          // L35651-35709
   j = nextSame(i); class[i] = 1
   append material section mat[rec[i].shape] posA(i) → posB(j) (FUN_180146e20)       // L35711-35720
   while j != start:
      k = chain(cur)
      append swept-outline section swept[0] (rec[j].+0x20, +0x28) → (rec[k].+0x20, +0x28)   // L35723-35731
      if class[k] != 0: break
      cur = k; j = nextSame(k); class[k] = 1
      append material section posA(k) → posB(j)                                       // L35735-35745
   FUN_18003a880(flags, L): bbox; +0x54 = 0; +0x50 = (flags&1) ? 1 : −1               // L35788-35808
return loops nonempty ? areaSwept − Σ area(L, tol 0.001) : 0.0   // L35641-35646 (FUN_180126660 L158372-158399)
```
Area FUN_180126ac0 (L158542-158719): trapezoid sum over the entities (arcs via FUN_1801270e0), result |sum/2|; **0 if
two consecutive entities are more than tol (0.001) apart** (L158675). With the class swap the loops start on the
sections that lie inside the swept region; width = this value / step length (L30711-30717). (Which of the two
swept-outline pieces `chain` picks follows the swept-outline order of LAB_180005c72 = FUN_180031280; the code itself does
not state that the loops are "swept minus material".) Comparators decoded 2026-10-28 (Ghidra, the jmp thunks at 0x180002969 /
0x180005c72 made functions in a read-only session).

### 6.1.6 Measure flags and the engagement arc
- **FUN_18002fc80 (L30114-30521)**: needs ≥ 2 records and one non-marker i0. Cap = first swept entity with CeFlags
  bit 1 (the end cap at E, 6.1.1) = arc (c, ρc, sa, sw) (L30207-30232). Walk the +0x30 chain from i0 back to i0
  (L30243-30421); for each record i with class 0 whose `+0x34` predecessor p has class 2:
  `a_p = atan2(pt_p − c)`, `a_i = atan2(pt_i − c)` (0 if both |dx|,|dy| ≤ 1e-12); s = wrap(a_i − a_p) to [−π,π];
  if flags&1 and s < 0: s += 2π; if !(flags&1) and s > 0: s −= 2π (L30278-30298);
  `ov = CalcSharedArcSw(cap, arc(c, ρc, a_p, s), &lo, &hi)` — angular overlap of the two angle intervals; it returns
  the total overlap and the bounds of the largest overlap piece (mcce::CalcSharedArcSw L9611-9712); total += ov;
  if ov > 1e-12 push lo, hi (L30299-30418). Then sort the angle list (qsort_s, comparator LAB_1800034b8, context = cap,
  L30424) and append `c + ρc·(cos, sin)` of each angle to M+0x108 (L30425-30512). M+0x128 = total (L30514).
- **flag 2** (L30722-30724): more than 2 engagement points (more than one engaged arc).
- **flag 4 — FUN_180037070 (L34023-34116)**: T = unit end tangent of the step (line: E−S; arc: radial·|sw| rotated by
  sign(sw), L34062-34087), require |T| ≥ 1e-12. Q = E + rad·(T.y, −T.x) if flags&1, else E + rad·(−T.y, T.x)
  (L34089-34098). If any **even-indexed** engagement point (0, 2, 4, …) is within dist² < 1e-8 of Q → return; else
  flags |= 4 (L34100-34113).
- **flag 0x100 — FUN_1800380f0 (L34494-34669)**, only when the material position pos = {int shape, int ent, double t,
  CPt2 pt} (= §3 C `matIdx`) is given and pos.shape ≥ 0; flag cleared first (L34530). For each record i on pos.shape:
  a marker → flag 0x100 iff class 2, return; else k = next record of that shape (cyclic over the list); if both entity
  indices ≠ −1 and `FUN_1800353f0(pos, i, k)` (L33155-33188: pos.pt farther than 1e-7 from both crossing points and
  u = ent+t strictly inside (u_i, u_k), cyclic when u_k ≤ u_i) → flag 0x100 iff class(i) == 2, return. Fallback:
  `PtInSh(pos.pt, swept, 1e-5) == 2` → flag 0x100 (L34663-34666).
- **flag 0x10** (air): width < 1e-12 and no class-2 record (L30718-30720). **flag 8**: swept region or frame failed.

### 6.1.7 Material subtraction (commit) — FUN_180037790 (L34213-34491)
Called from commit FUN_180035990 (L33361-33389) for set 1 (`isFirst` = 1, material RG+0x178, block M1, pos = matIdx)
and, when RG+0x1b0 is non-empty and M2 flags & 0x18 == 0, for set 2 (`isFirst` = 0, RG+0x1a0, M2, pos = NULL).
The ISEC list used is the one left by the measurement of the same step (no re-intersection).
```
Pend = end point of the step                                        // L34288-34312
FUN_180033cf0(RG, isFirst, mat, isec, pos, &pass)                   // pass number, below
NEW = []
i = -1
while nextClass0(&i):                                               // classes NOT swapped: loops start on KEPT sections
   if rec[i] is a marker: class[i] = 1; NEW += mat[rec[i].shape]; mat[...] = NULL; continue   // L34476-34486 (shape kept unchanged)
   L = new CSh; reserve 1.2 × entity count of the source shape       // L34372-34425
   cur = i
   loop:
      j = nextSame(cur); class[cur] = 1
      n0 = |L|; FUN_180147500(1e-16, mat[rec[cur].shape], posA(cur), posB(j), L)   // L34434-34439
      FUN_180036250(RG, isFirst, L)                                  // collinear merge, below
      FUN_180036f70(NEW, n0, cur, j, isec)                           // rec[cur].+0x40/44/48 = (|NEW|-1, n0, 0.0); rec[j]... = (|NEW|-1, |L|-1, 1.0)
      if j == i: break                                               // L34445
      k = chain(cur); n0 = |L|
      FUN_180147500(1e-16, swept[0], (rec[j].+0x20,+0x28), (rec[k].+0x20,+0x28), L)  // L34452-34456
      FUN_180035ff0(RG, isFirst, mat[rec[cur].shape], rec[j].ent, rec[k].ent, n0, L, &pass)  // L34459-34467
      cur = k
   while class[k] == 0                                               // L34469
   FUN_180147fd0(1e-16, L)       // close: if |end−start|² ≥ 1e-16 append L(end→start) (L176398-176501)
   FUN_180032a20(T, flags, Pend, L)                                  // L34473-34474
if pass == 0: FUN_180033b60(RG, isFirst, mat, &pass)                 // L34320-34322
mat = NEW   (old list and every shape not moved are freed)            // L34323-34346
```
Consequences, as coded: shapes whose marker is class 2 (lying wholly inside the swept region) are dropped; the swept
outline pieces between crossings become new material boundary.
- `FUN_180147500` (L176065-176220): clamps both t to [0,1]; if L is non-empty and the gap from L's last end point to the
  section start is > 1e-16 (squared), appends a bridging 'L'; then the section copy FUN_180146e20.
- **FUN_180032a20 (L31853-31891)** sets the new loop's attributes: bbox; **island (+0x54 = 1) iff Pend is inside the
  loop** (`PtInSh(Pend, L, 1e-5) == 2`); dir +0x50: non-island → (flags&1) ? −1 : +1, island → (flags&1) ? +1 : −1;
  **+0x58 = T+0x1c** (CCetp record count at creation). (Answers §3 D's "meaning of CSh+0x58" for material loops:
  creation record number; FUN_1800686c0 "largest +0x58" = most recently created piece.)
- **FUN_180036250 (L33600-33994)**, collinear merge of the tail of L (only if |L| ≥ 2): for n = min(3, |L|) down to 2:
  A = start of entity |L|−n, B = end of the last entity; every one of those n entities must have its end point (and,
  for an arc, also its mid point) with foot point on line AB (`mcce::CalcLnLnIntersectPt(A, dir AB; P, normal)`)
  within 1e-6 (squared 1e-12) of P, and a parameter in [0,1] unless P is within 1e-6 of A resp. B; when isFirst and
  C+0x758, entities after the first must have the same pass byte (+0x09) as their predecessor (L33855-33870,
  L33920-33928). The first n that passes: the n entities are replaced by one 'L' A→B carrying the last entity's pass
  byte, SetChnMinMax (L33935-33984); return.
- **FUN_180037540 (L34119-34210)**, called by commit after set 1: new pos = among non-marker class-1 records, the one
  with the largest (+0x20, +0x28) swept-outline parameter, unless the record nearest to pos.pt (dist² ≤ 1e-8) has a
  parameter ≥ that maximum; pos = {rec.+0x40, rec.+0x44, t = 0.0, rec.pt}; none → pos.shape = −1.

**Pass number** (only when isFirst and C+0x758 = first-pass feed active, §3 C.4):
- FUN_180033cf0 (L32411-32646): among non-marker class-0 records on pos.shape (valid indices), take the one with the
  smallest forward distance along the shape from pos (FUN_180149d80 L177357-177450, start value FLT_MAX). Walk the
  material entities from pos.ent to that record's entity (cyclic); sum the travelled length per pass byte (+0x09)
  (first entity: from pos.t to its end; last entity: from its start to rec.t; others: full length; L32528-32616).
  Sort by summed length ascending (introsort FUN_18002da00 / insertion FUN_18002d140 key +0x10) and take the longest:
  pass = byte + 1, or 0xff if byte == 0xff (L32636-32644).
- FUN_180035ff0 (L33528-33572): if pass is still 0: pass = min(0xff, min of byte +0x09 over the material entities
  rec[j].ent → rec[k].ent, cyclic) + 1 unless 0xff; then every entity of L appended since n0 (the swept-outline piece)
  gets byte +0x09 = pass.
- FUN_180033b60 (L32344-32408), fallback when pass is still 0 after all loops: 1 + max byte +0x09 over all entities
  of the material set (255 saturates; 0 if the set has no entities).
(This refines §3 C.4 "1 + the maximum of byte +9 of the material entities involved".)

### 6.1.8 Local material window — FUN_180029c30 (L27124-27263)
`clip(P, ρ, src, dst)`: dst = **one** CSh (an unordered bag of entities, not a loop) holding a copy of every entity of
every src shape whose bbox overlaps [P − ρ − 1e-7, P + ρ + 1e-7] and whose nearest point to P (GetNearestEntPt) is at
distance **< ρ** (L27235-27242). Each copy gets +0x0c = its entity index and +0x80 = its shape index in src
(L27246-27247), which FUN_1800331a0 uses to report crossings against the full material. ρ = 10·C+0x1e8 + C+0x30
(FUN_18002a140 L27296-27297). An empty src leaves dst empty (no CSh); otherwise dst holds its one CSh even when no entity
qualifies (then the window is "non-empty" for FUN_180030de0 and every material shape becomes a marker).
**Lifetime (2026-11-07)**: FUN_18002a140 (L27270-27302) first empties both windows, then rebuilds them around the last
output point (FUN_18008e4b0; none → both stay empty). Its only caller is the state-0/1 trial step FUN_18006db90
(L62163), before the limits and the step search FUN_180016c00; after that step's measure and commit, FUN_18006db90 empties
both windows again (C+0x478 = C+0x470 and C+0x4a0 = C+0x498, i.e. RG+0x1e0 = RG+0x1d8 and RG+0x208 = RG+0x200,
L62186-62198). So the window serves exactly the measures and the commit of one state-0/1 trial step; every other measure
(state 2, entries, links) uses the full material.

### 6.1.9 FUN_1800d96d0 (L117594-117647)
For every entity of every shape: byte +0x12 (CeFlags) = 0; double +0x70 = 0.0; if the bool argument is set, double
+0x60 = 0.0. (§3 B.6 calls it with 1: both doubles are zero.)

### NOT DECODED (6.1)
- (closed 2026-10-28 / 2026-11-07) the three qsort comparators: thunk 0x180002969 = `jmp 0x1800314a0` (FUN_1800314a0),
  0x180005c72 = `jmp 0x180031280` (FUN_180031280), 0x1800034b8 = `jmp 0x1800312f0` (FUN_1800312f0: ascending clamped
  |angle − cap sa|) - objdump of mcce.dll.
- (closed 2026-11-07) FUN_180034ac0's rotation angle: see 6.1.2.
- FUN_180146e20 (section copy, L175722-176064), mcce::CalcEntIntersect (L6348ff), mcce::PtInSh,
  mcce::GetNearestEntPt, mcce::RevSh, FUN_180021580 (point at (ent, t)), FUN_1801270e0 (arc area term),
  FUN_180147ed0 (walk used by FUN_180149d80): geometry kernels, internals not read.
- FUN_18001cfe0 (block clear) and the ISEC +0x48 field for records that are not loop ends.

## 6.2 State 0/1 trial-step generators

Line numbers are `decomp/mcce_all.c`. `C+x` = CCePock offset (C+x = P+(x−8)); RG = C+0x298. Constants used below:
R−r = C+0x30 − C+0x38; m = C+0x48 = P+0x40 (min toolpath radius); rl = C+0x230 = P+0x228 (`dMinCutRad`, clamped in
FUN_180069350); step = C+0x1e8 = P+0x1e0 (march step); s_0.25 = C+0x218 = P+0x210; bull = C+0x227 = P+0x21f;
climb = C+0x10 & 1. pock_chn entity: id +0x08, line (x1,y1,x2,y2) = +0x18..+0x30, arc (cx,cy,r,sa,sw) = +0x18..+0x38.
E = CEnCut trial record (entity at E+0x10, heading E+0x98, length E+0xa0, w1 E+0x128, w2 E+0x260, flags E+0x138; §3 C.0).
G = CGbap (valid byte G+8, lead-in CSh G+0x10 with entities at G+0x28/+0x30, shape G+0xa8, ent G+0xac, t G+0xb0,
pt G+0xb8/+0xc0).

### 6.2.0 Resolution of the list offsets used by states 0/1 (closes a C.7 item)
A `CVe` at object offset X has its begin pointer at X+0x10 (§3 B convention). Applied to the region object RG (§3 B.6/B.7):
| C offset | = | meaning |
|---|---|---|
| C+0x340 | RG+0xa8 | current tool-centre region (B.7) |
| C+0x350 | begin ptr of C+0x340 | so `C+0x350[i]` = i-th shape of C+0x340 |
| C+0x3c0 | RG+0x128 | `MCCECalcOffset(1, −P+0x40, RG+0xa8)` (B.7) |
| C+0x3e8 | RG+0x150 | `MCCECalcOffset(1, max(R−r−1e-4,0), RG+0xa8)` (B.7) |
| C+0x410 | RG+0x178 | current material (B.7) |
| C+0x420 | begin ptr of C+0x410 | `C+0x420[i]` = i-th material shape. **C+0x420 is not a separate list** (evidence: FUN_180023460 l.24185 intersects with list C+0x410, then FUN_1800244a0 l.24598-24600 indexes the hits' shape numbers into C+0x420) |
| C+0x448/0x450 | begin/end ptr of RG+0x1a0 (C+0x438) | bullnose material (B.7). "C+0x448 non-empty" = bullnose material present |

### 6.2.1 Move toward a material piece: FUN_180017d30 (18474-18563)
`FUN_18006b170(C, T, mat = C+0x420[*matIdx], E+0x10)`; if it returns 0, `FUN_18006b570(...)` (18503-18510).

**FUN_18006b170 (60838-60982): one arc that swallows a whole small material piece** (only when bull is set):
```
memset(chn, 0)
if !bull: return 0                                                     // 60886
if !FUN_18006aba0(C, mat, &Q): return 0                               // target point
if !FUN_18006bfa0(C, T, Q, chn): return 0                             // tangent arc P -> Q
save chn; if !FUN_18006ad40(C, Q, chn): restore chn                   // 60891-60927 (radius fix-up)
sw = new list with one empty CSh                                       // 60928-60958
if !covers(C, mat, chn, 0.97*(R−r), sw): return 0                     // FUN_18006c4e0, 60959-60962
                                                                       //   (R−r) − (R−r)·0.30000000000000004·0.1
trim(C, mat, chn, sw)                                                  // FUN_18006c690, 60965
return 1
```
- **FUN_18006aba0 (60698-60742) target point**: if mat has entities: bbox (GetMinMaxViaShEnt), M = bbox centre; if
  `PtInSh(M, mat, 1e-5) == 2` → Q = M, else Q = nearest point of mat to M (FUN_18013d200). Returns 1. Empty mat → 0.
- **FUN_18006bfa0 (61291-61416) tangent arc**: P = last output point (FUN_18008e4b0), Tg = last output tangent
  (FUN_18008e710); either missing → 0. Line 1 = P + s·(−Tg.y, Tg.x); line 2 = midpoint (P+Q)/2 + s·(−(Q.y−P.y), Q.x−P.x)
  (perpendicular bisector). `CalcLnLnIntersectPt(line1, line2, &s1, 0, &O)` (61353-61354):
  - no intersection → `L(P → Q)` (61357-61365), return 1;
  - else `A`: centre O, r = |O−P|, sa = atan2(P−O) (fail if |P−O| ≤ 1e-12 per component, 61377-61378/61409),
    sw = wrap_{[-π,π]}(atan2(Q−O) − sa) (fail if Q==O); if (s1 > 0 and sw < 0) sw += 2π; if (s1 < 0 and sw > 0)
    sw −= 2π (61396-61403). `s1` is the 3rd output of CalcLnLnIntersectPt (its exact meaning — parameter along line 1 —
    is the kernel's, internals NOT DECODED).
- **FUN_18006ad40 (60747-60831) radius fix-up**: if chn is 'L' or r > rl (strict): return 1 unchanged (60763-60767).
  Else S = arc start; |c−S| < 1e-12 → 0; new centre c' = S + rl·(c−S)/|c−S|, r = rl; sw' = wrap(atan2(Q−c') − sa)
  (sa kept); force sign of sw' to the old sign of sw: old sw > 0 and sw' < 0 → +2π; old sw ≤ 0 and sw' > 0 → −2π
  (60768-60830). Q == c' → 0.
- **covers = FUN_18006c4e0 (61421-61437)**: `FUN_1800321e0(flags, rad, chn, list)` builds the swept region of chn with
  radius rad (FUN_1800321e0, §6.1.1); sets list[0]+0x54 = 0; result = `(FUN_180113300(mat, list, 1e-5) & 4) == 0`.
  FUN_180113300 (148471-148567) splits `mat` at its crossings with `list` (FUN_180134690) and ORs the section classes from
  FUN_1801130f0 (class 4 = outside, cf. `Inside()` §1 helpers). So covers = **no part of the material piece lies outside
  the swept tube of radius 0.97(R−r)**.
- **trim = FUN_18006c690 (61498-61673)**: bisection on the entity fraction u: lo = 0, hi = 1; repeat: mid = (lo+hi)/2,
  entity scaled to mid (line end lerp / arc sw·mid); if covers(mid) hi = mid else lo = mid; stop when
  (hi − lo)·len < 0.5·step or after the 11th iteration (`10 < i`) (61639). Final entity = fraction hi (61660-61671).

**FUN_18006b570 (60989-61202): fallback aim at the material bbox**
```
memset(chn,0); P = last output point (FUN_18008e4b0)
bb = bbox(mat) grown by 2e-4 on every side                                  // 61053-61077
corners[4] = {(minx,miny), (minx,maxy), (maxx,maxy), (maxx,miny)}
qsort_s(corners, 4, 16, LAB_180001b9a, &P)                                  // 61078; comparator NOT DECODED
c2 = corners[2]; c3 = corners[3]
k = CalcCirCirIntersect(c3, R−r, c2, R−r, &I1, &I2)                          // 61080-61081
k == 0: d = |c3 − P|; if d >= R−r: X = P + (c3−P)·(d − (R−r))/d else X = P;  chn = L(P → X)   // 61086-61110
k == 1: chn = L(P → I1)                                                      // 61189-61198
k == 2: arc centre c3, radius R−r, sa = atan2(I1−c3), sw = wrap(atan2(I2−c3) − sa), alt = sw −/+ 2π (sw ≥ 0 → sw−2π)
        use alt instead of sw if the midpoint of the alt arc is nearer to c2 than the midpoint of the sw arc  // 61112-61182
        if FUN_18006bcb0(C, T, arc, P, chn): done                           // 61183-61184
        else X = GetNearestArcPt(arc, P); chn = L(P → X)                     // 61185-61187
SetChnMinMax(chn)
```
**FUN_18006bcb0 (61209-61284)**: û = unit last tangent (fail if |T| < 1e-12). Candidates in order, each trimmed by
FUN_18006c580 to the arc `arc` (accept the first that hits):
1. `L(P → P + π·m·û)`;
2. `A` centre P + m·(cos(θ−π/2), sin(θ−π/2)), r = m, sa = θ+π/2, sw = −π (θ = atan2 û);
3. `A` centre P + m·(cos(θ+π/2), sin(θ+π/2)), r = m, sa = θ−π/2, sw = +π.
Returns false if none hits.
**FUN_18006c580 (61444-61493)**: `CalcEntIntersect(1e-7, arc, chn, ...)`; no hit → 0; else take the smaller of the (up
to 2) chn-side parameters (local_58/local_50) and cut chn to it (line lerp / arc sw·t), return 1.

### 6.2.2 Target point on the material wall: FUN_1800186a0 (18717-18785) and FUN_18001efb0 (22132-22192)
FUN_1800186a0(C, T, lim1, &pos, d, out) (outline already in §3 C.3) — exact:
```
tol = max(0.1·step, 1e-4)                                                    // 18739-18742
Lmax = π·(R−r)                                                               // 18753
B = last output point; none → 0
if bull and FUN_18001c2c0(C, &pos, d):                                       // 18756-18757
    A = nearest point of C+0x420[pos.shape] to B (FUN_18013caf0)              // 18758-18761
    if place(C, 1e-5, lim1, A, B, out): return 1                              // 18762
walked = d; ok = FUN_18006f1c0(0, d, C+0x410, &pos, &A)                       // 18767
while ok:
    if place(C, tol, lim1, A, B, out): return 1                               // 18771
    if walked > Lmax: break                                                   // 18776
    walked += d; ok = FUN_18006f1c0(0, walked, C+0x410, &pos, &A)             // 18778-18779 (always from the original pos)
return 0
```
**FUN_18001c2c0 (20596-20648) "small piece"**: 0 ≤ pos.shape < n(C+0x410) and the summed entity lengths of that shape
never exceed 2·d (returns 0 as soon as the running sum > 2d).

**place = FUN_18001efb0(C, tol, w, A = wall point, B = tool point, out)**:
```
ρ  = R − r
φ  = CalcLnEngAng(ρ, w) = acos(clamp(1 − w/ρ, −1, 1))         // mcce::CalcLnEngAng 9016-9033 (1−w/ρ < −1 → π)
a  = φ + π/2;  b = (a > π) ? 2π − a : a                        // 22151-22155
h  = ρ·sin(b);  D = |B − A|
fail if D < 1.1920928955078125e-07 or D < h or (h < D < ρ)     // 22162-22165
α  = (π − b) − asin(h/D)                                       // 22166-22167
β  = atan2(B − A)   (0 if both |dx|,|dy| ≤ 1.19e-7)            // 22168-22172
θ  = (climb ? (a > π) : (a <= π)) ? β + α : β − α              // 22173-22181
out = A + ρ·(cos θ, sin θ)                                     // 22183-22188
return |out − B|² ≥ tol²                                        // 22191
```

### 6.2.3 Alternative candidates in FUN_180016cb0 (§3 C.3)
**FUN_180018990 (18790-18816)**, used when A or B has flag 2:
```
ang = FUN_18001bb60(C, T, A, &a) ? a : A.E+0x98
E   = trial_line(C, T, heading = 0.5·ang, len = d)       // FUN_180017a10
return inside(lim, E) && (E.flags & 0x100)              // FUN_18001bb30
```
**FUN_18001bb60 (20372-20591) heading along a circle of radius rl**:
```
S–Pe = FUN_18001b5d0(C, T): last output entity as a line                     // 20434
Q = end point of A's step entity                                             // 20476-20505
side = sign((Pe − S) × (Q − S)); 0 → fail                                    // 20508-20518
Lp = |Pe − S|;  h = sqrt(rl² − (Lp/2)²)                                     // 20519-20531 (no guard: rl < Lp/2 gives NaN)
n = side==1 ? (−(Pe−S).y, (Pe−S).x) : ((Pe−S).y, −(Pe−S).x); |n| < 1e-12 → fail  // 20540-20553
O = (S+Pe)/2 + h·n̂                                                           // 20554-20555
require |O − Q| ≤ rl                                                         // 20556-20559
X = FUN_180018170(O, Pe, rl, A.E+0xa0 (step length), side)                    // 20560
require last output tangent t (FUN_18008e710, non-zero)
*out = wrap_{[-π,π]}(atan2(X − Pe) − atan2 t)                                  // 20568-20584
```
- **FUN_18001b5d0 (20153-20293)**: no records (T+0x1c == 0) → 0. Last ring entity (FUN_18008edd0). 'L' → copied.
  'A' with r ≥ 1e-7 and |sw| > 1e-7: φ = acos(clamp(1 − step²/(2r²))), φ := |sw| if |sw| < φ; result =
  `L(point at fraction (|sw|−φ)/|sw| of the arc → arc end)` (the chord of the last arc piece whose chord = march step).
  Other ids → 0.
- **FUN_180018170 (18569-18596)**: α = acos(1 − L²/(2rl²)) (no clamp); θ = atan2(Pe − O) + (side==1 ? α : −α);
  X = O + rl·(cos θ, sin θ). Pe == O → fail.

**FUN_18001a2b0 (19498-19779) two-circle bracket search**:
```
r1 = search(C, T, sel=1 (circle 1), lim1, lim2, d, matIdx, A, → E1, G1)       // FUN_18001aa70, 19651
r2 = (C+0x448 list non-empty) ? search(sel=0 (circle 2), → E2, G2) : 0        // 19654-19661
if !r1: result = r2 ? E2 : fail
elif !r2: E1
else:                                                                         // 19663-19688
   f1 = E1.flags & 0x100; f2 = E2.flags & 0x100
   if !f1 && f2: E2
   elif f1 && !f2: E1
   else: b1 = FUN_18001bb60(E1); b2 = FUN_18001bb60(E2)
         if !b1 && b2: E1
         elif b1 && !b2: E2
         else: (E1.w1 + E1.w2 < E2.w1 + E2.w2) ? E2 : E1
```
**search = FUN_18001aa70 (19787-20123)**: runs only if sel==1 or C+0x448 non-empty. Three working records:
Elo/Glo, Ehi/Ghi (best inside-limit candidate with flag 0x100), Ebest/Gbest (best inside-limit candidate without 0x100).
`if bracket(...)` (FUN_180019960) succeeded: `refine(...)` (FUN_1800194e0). Result: Ehi if Ehi.flags & 0x100; else Ebest
if it holds an entity (type byte E+0x18 ≠ 0); else fail (20003-20042).

**bracket = FUN_180019960 (19293-19388)**:
```
wA = sel ? A.w1 : A.w2 ; lim = sel ? lim1 : lim2
up = climb ? (lim < wA) : (wA <= lim)                                         // 19309-19329
hA = start-tangent angle of A's step entity (line: atan2(d); arc: tangent at sa, sign of sw)   // 19330-19358
span = wrap_{[-π,π]}( wrap(hA − A.E+0x98 + π) − hA )                          // 19359-19372 (= π − heading, wrapped)
if up: span < 0 → span += 2π   else: span > 0 → span −= 2π                     // 19373-19380
return sweep(C, T, sel, lim1, lim2, d, span, matIdx, A, Elo, Glo, Ehi, Ghi, Ebest, Gbest)   // FUN_18001cc60
```
**sweep = FUN_18001cc60 (20910-20952)**: for i = 1..50: Elo = trial_line(heading = A.E+0x98 + (i/50)·span, len d);
`keep(...)` (FUN_18001d9e0); stop with success when the width crossed the limit: (wA ≥ lim and Elo.w < lim) or
(wA < lim and Elo.w > lim) (w = w1/lim1 for sel 1, w2/lim2 for sel 0) (20932-20949). 50 steps without crossing → 0.

**keep = FUN_18001d9e0 (21336-21377)**: only if `inside(lim1 + s_0.25, lim2 + s_0.25, Ecand)`:
- Ecand.flags & 0x100: replace Ehi if Ehi lacks 0x100, or if Ecand.w1+Ecand.w2 > Ehi.w1+Ehi.w2; then Ghi = Gcand.
- else: replace Ebest if Ebest is empty (E+0x18 == 0), or if Ecand.w1+w2 > Ebest.w1+w2; then Gbest = Gcand.
(When the sum is not larger, nothing — including G — is copied.)

**refine = FUN_1800194e0 (19154-19288)**: `FUN_1800b0200(LAB_1800011ef, ctx, [A.E+0x98, Elo.E+0x98], tol = 1e-7
(0x3e7ad7f29abcaf48), &root)` = Brent's method over the heading between A and the bracketing trial (19244-19248),
then Ehi/Ghi/Ebest/Gbest are copied back from ctx. ctx (local_738): {T, sel, lim1, lim2, d, pos[4×8], C, Ehi, Ghi,
Ebest, Gbest}. The callback LAB_1800011ef is a thunk whose target is not in the decompile; **FUN_180019180
(19032-19147) has exactly this context layout** (param_1[0]=T, [1]=sel, [2]=lim1, [3]=lim2, [4]=d, [5..8]=pos,
[9]=C, [10]=Ehi, [0x59]=Ghi, [0x72]=Ebest, [0xc1]=Gbest) and computes: progress; E = trial_line(heading = x, len d);
keep(E); return sel ? E.w1 − lim1 : E.w2 − lim2 (19105-19119). Identification of the thunk target is by this match
only (unverified).

### 6.2.4 Trial line with zero length: FUN_18001c510 (20655-20905)
Called from FUN_180017a10 when the attached step has length² < 1e-10 and G is valid (18445-18458):
```
path = candidate_path(C, G)                                      // FUN_18006caa0 (§3 C.4: lead-in + one full loop)
pos = start of path; acc = 0
loop: ok = FUN_18006f1c0(mode 1, step, path, pos → npos, &l)     // 20784-20785, 20855-20856
      if !ok: break
      acc += l
      if acc > 1e-6:                                             // 20789
         stepEnt = section(path, pos0 .. npos)  (FUN_18006fc10)   // 20790
         if G lead-in non-empty:
            (e,t) = nearest on lead-in to npos.pt (FUN_18013d200)
            rest = section(lead-in, (e,t) → (last, 1.0)) (FUN_180146e20); lead-in cleared
            if len(rest) > 1e-8: lead-in = rest                   // 20791-20809
         X = lead-in empty ? npos.pt : end point of the lead-in's last entity      // 20811-20840
         (G.ent, G.t, G.pt) = nearest of C+0x350[G.shape] to X (FUN_18013d200)    // 20841-20846
         break
      pos = npos
```
Then in FUN_180017a10, if the lead-in is now empty, dir = side(C+0x350[G.shape]) (18459-18465).

### 6.2.5 Attach chain: FUN_180024720 → FUN_180021e40 (23335-23361)
```
G.clear(); ensure one lead-in slot                                 // FUN_180024720 24625-24630
direct(C, step, G)                                                 // FUN_180022a50
if (G.valid || probe(C, step)) && !offset_attach(C, T, step, G) && !G.valid:
    uturn_attach(C, step, G)                                       // FUN_180022500
```
**direct = FUN_180022a50 (23800-23976)** (exact part not in §3 C.3): S = one-shape list holding `step`.
`i = first_exit(S, C+0x340)` (FUN_180023dd0); if i ≥ count: `i = touch_start(S, C+0x340)` (FUN_1800241c0); if still
≥ count: no attach. Else the step is cut at ISAB[i].+0x18 (fraction on the step), G.valid = 1, G from ISAB[i]
(+0x20 shape, +0x24 ent, +0x28 t, +0x00 point) (23910-23941), t re-projection as in §3 C.3.
- **first_exit = FUN_180023dd0 (24413-24496)**: requires C+0x340 non-empty, exactly 1 step shape with 1 or 2 entities.
  I = Isect(S, B) (FUN_180134270). For i = 0..n−1: section of S from (I[i].+0x14, I[i].+0x18) to (I[i+1].ent, .t), or for
  the last crossing to (same entity, 1.0); if `FUN_18014b720(S, …, tol)` (tol 1e-5 between crossings,
  1e-7 = 0x3e7ad7f29abcaf48 for the last; role NOT DECODED) is false, classify the section with
  `FUN_1801130f0(..., B, 1e-5)`; class 1 or 4 → return i. Else return n.
- **touch_start = FUN_1800241c0 (24503-24573)**: if S has exactly 1 entity, the whole entity classifies 4 (outside) against
  B, and the entity start point is within 1e-4 of B (FUN_18013cfe0): out = one ISAB {+0x00 pt = nearest point,
  +0x20 shape, +0x24 ent, +0x28 t} (resized with FUN_180026f10; its +0x18 is not written here). Always returns 0, so
  index 0 is used whenever a record was appended.

**probe = FUN_180023460 (24140-24233)**:
```
X = half_annulus(C, step)                                    // FUN_180020610
I1 = Isect(X, C+0x3e8); require I1 non-empty and I1[0].+0x14 < 1 (first hit on entity 0)
I2 = Isect(X, C+0x410); require non-empty
mark(C, X, I2)                                               // FUN_1800244a0
return !contact(C, X, I1, I2)                                // FUN_180024d50
```
- **half_annulus = FUN_180020610 (22712-23019)**: n = unit normal of the step end tangent, left (−t.y, t.x) when climb is
  clear, right (t.y, −t.x) when climb is set (22818-22865); Pe = step end; c = Pe − m·n̂ (22874-22894). Shape of 4
  entities, dir = climb ? +1 : −1 (23013-23017):
  e0 `A` centre c, r = (R−r)+m, sa = atan2 n, sw = climb ? +π : −π; e1 `L` end(e0) → start(e2);
  e2 `A` centre c, r = m, sa = atan2(end(e0) − c), sw = −sw(e0); e3 `L` end(e2) → start(e0).
- **mark = FUN_1800244a0 (24578-24614)**: for each ISAB i: section of material shape C+0x420[I2[i].+0x20] from
  (+0x24,+0x28) to I2[I2[i].+0x30] tested against X[0] with FUN_1801489a0(..., single = (n==1), 1e-5); result ≠ 4 →
  I2[i].+0x38 = 3.
- **contact = FUN_180024d50 (24809-24867)**: cyclic scan of I2 from the last record: find a class-3 record, then the first
  following non-class-3 record k (either scan wrapping round → false). True iff I2[k] is on X entity 0 (+0x14 == 0),
  I2[k].+0x18 ≤ I1[0].+0x18, |I2[k].pt − I1[0].pt|² ≥ 1e-10, and `FUN_1800236f0(C, X, I2[k].t, I1[0].t)`.
- **FUN_1800236f0 (24238-24408)**: requires X = {A,L,A,L}, t0 ≤ t1, |e2.sw + e0.sw| ≤ 1e-7, |e2.r − m| ≤ 1e-7.
  n = max(3, round(len(e2 between fractions 1−t0 and 1−t1)/(R−r))) (24306-24340). For t = t0, t0+(t1−t0)/n, … while
  t < t1 + 1e-7: circle (centre = point of e2 at fraction 1−t, r = R−r, sa 0, sw 2π, dir 1) must intersect material
  C+0x410 (FUN_180134270), else return 0. All hit → 1.

**offset_attach = FUN_180021f70 (23368-23575)**:
```
G' = copy(G); s = copy(step)
S2 = FUN_180025700(C, T, step) or fail                        // offset copy of the step, below
i = first_exit(S2, C+0x3c0); require i < count
u = (S2 has 2 entities and I[i] on entity 0) ? 0 : I[i].+0x18 // 23462-23481
s = step cut to fraction u
lead_in(C, s, I[i].pt, G')                                    // FUN_180025a30
if any lead-in arc of G' has |sw| > π: fail                   // FUN_180024f90(…, π) 24872-24903
if !G.valid and FUN_1800247d0(C, G'): fail                    // 23489-23492
step = s; G = G'; return 1
```
- **FUN_180025700 (25063-25154)**: fail if step length² < 1e-8. ρ = climb ? +m : −m. Shape = [optional corner arc from
  FUN_180025040, then FUN_180026120(ρ, step)].
  - FUN_180025040 (24910-25058): a = atan2(last output tangent) (none → nothing); b = start-tangent angle of the step;
    δ = wrap(b − a). Unless (δ > 0 and ρ > 0) or (δ < 0 and ρ < 0), append `A` centre = step start, r = |ρ|,
    sa = wrap(a + sgn(ρ)·π/2), sw = wrap(wrap(b + sgn(ρ)·π/2) − sa).
  - FUN_180026120 (25376-25394) → line FUN_180026170 (25399-25457): both ends moved by ρ·(−û.y, û.x) (û = unit
    direction; length < 1e-12 → fail); arc FUN_180025f80 (25307-25371): r' = r + (sw ≥ 0 ? −ρ : +ρ); if r' < −1e-12:
    r = |r'|, sa = wrap(sa + π); else r = max(r', 0); fail if |r·sw| < 1e-12.
- **lead_in = FUN_180025a30 (25161-25302)**: Pe = step end, X = crossing point; Bp = nearest point of C+0x340 to X
  (FUN_18013d010). If |Pe − X| ≤ 1e-6 (both components): lead-in cleared, G'.valid = 1 (position not rewritten).
  Else a = atan2(Pe − X), b = atan2(Bp − X), δ = wrap(b − a); climb: δ < 0 → +2π; not climb: δ > 0 → −2π
  (25239-25247). If |δ·m| ≤ 1e-7 or 2π − |δ| < 0.017453292519943295 (1°): lead-in cleared; Bp2 = nearest of C+0x340
  to Pe; use Bp2 if |Pe − Bp|² > |Pe − Bp2|². Else lead-in = `A` centre X, r = m, sa = a, sw = δ. Then
  G'.(shape, ent, t, pt) = Bp, G'.valid = 1.
- FUN_1800247d0 (24638-24724): X2 = FUN_18001ff60(C, G') (fail → returns 1); I1 = Isect(X2, C+0x3e8);
  I2 = Isect(X2, C+0x410); FUN_180023100(I1, I2); mark(C, X2, I2); return FUN_180024a00(C, X2, I2) ? 1 : 0.

**uturn_attach = FUN_180022500 (23582-23793)**:
```
G = uturn(C, step)                                   // FUN_1800216c0 on the whole step
if G.valid: lo = 0, hi = 1; repeat 4 times:           // 23693-23742
    mid = (lo+hi)/2; Gm = uturn(C, step cut to mid (from the original step))
    if Gm.valid: step = cut(mid); G = Gm; hi = mid  else lo = mid
```
**uturn = FUN_1800216c0 (23157-23330)**: G.valid = 0; lead-in = one `A`: n = unit normal of the step end tangent (left
when climb is clear, right when set — same rule as half_annulus), centre = end − m·n̂, r = m, sa = atan2 n,
sw = climb ? +π : −π (23203-23271). I = FUN_180134690(lead-in, C+0x340); if I non-empty and I[0].+0x18 ≥ 0: cut the arc
to fraction I[0].+0x18; if |r·sw| ≥ 1e-7: G.(shape, ent, t) = I[0].(+0x20, +0x24, +0x28), G.pt = I[0].+0x00,
G.valid = 1, and if FUN_1800247d0(C, G) → G.valid = 0 and lead-in cleared; else lead-in cleared. No hit → lead-in
cleared (23276-23318).

### 6.2.6 Material direction: FUN_180028ac0 (26583-26615)
`(shape, ent, t, pt) = nearest point of RG+0x178 (current material) to the query point (FUN_18013d010)`; if found,
tangent = FUN_1801204a0(shape, ent, t, tol 1.0000000000000002e-10). Outputs pt and tangent (unnormalised as returned).

### NOT DECODED (6.2)
- qsort comparator LAB_180001b9a (corner order in FUN_18006b570); thunk targets LAB_1800011ef (likely FUN_180019180,
  unverified).
- Kernels: CalcLnLnIntersectPt (meaning of its 3rd output), CalcCirCirIntersect, CalcEntIntersect, GetNearestArcPt,
  FUN_180134270 / FUN_180134690 (Isect / first-crossing), FUN_1801130f0, FUN_1801489a0 (section classification),
  FUN_18014b720 (degenerate-section test), FUN_18013d010 / FUN_18013d200 / FUN_18013caf0 / FUN_18013cfe0 (nearest point
  variants), FUN_1801204a0 (tangent), FUN_180146e20 / FUN_18006fc10 (section copy), FUN_180021580 (point at parameter).
- FUN_18001ff60 (shape built from a G lead-in), FUN_180023100, FUN_180024a00
  (lead-in material test used by FUN_1800247d0).
- FUN_18008edd0 (last ring entity accessor) and FUN_180114140 (progress) — accessors only.
(Flag 0x100, which drives the Ehi/Ebest split above, is produced by FUN_1800380f0, decoded in §6.1.6.)

## 6.3 Stay-down link strategies and targets

All line numbers are `decomp/mcce_all.c`. `C+x` = CCePock offset (C+x = P+(x−8)). "cur" = end point of the last
emitted record (`FUN_18008e4b0`), "curT" = its end tangent (`FUN_18008e710`). Constants are copied from the code.

### 6.3.0 Data used by every strategy

**Region sets S1..S4** = the four outputs out1..out4 of FUN_180062a70, decoded in **§6.4.2** (S1 = material grown
by R−r, S2 = containment set, S3 / S4 = per-group insets by d1 / d2). Callers: general link 58090-58092, final exit
59873, entry 64618, 66589. (A retry-counter caveat raised during this pass is resolved: the counter starts at 0,
`uVar10 = 0` at l.56646 before the group loop, so every group gets the three tries f = 1, ½, ¼.)
- FUN_180153b30 is the boolean "X combined with Y" of §3 B.3 (internals NOT DECODED there). Reading S2 as "tool-centre
  region minus material grown by R−r" follows that section's reading of FUN_180153b30 as X − Y; it is not
  independently verified here.

**Link target record `CTarg`, 0x108 bytes** (fields written by FUN_1800522c0 48211-48232; copy layout in the sort
helper FUN_18004be70 45104-45136):
| off | type | meaning (evidence) |
|---|---|---|
| +0x00 | CPt2 | target point (sort key; 45137-45145) |
| +0x10 | CPt2 | unit tangent of the boundary at the target (FUN_18011ffd0, 48213-48216); used as the arrival direction in FUN_18005d5f0 / FUN_18005c980 |
| +0x20 | byte | if set, FUN_18005c980 makes a plain line from the clip set to the point (53530) |
| +0x21 | byte | (§3 D 1.3: "first candidate with byte +0x21 set"); set to 1 by FUN_180075f60 for the reused previous entry point (l.66825, §6.4.4) |
| +0x22 | byte | "run-in section present" = 1 for boundary targets (48217) |
| +0x28 | double | run-in length d (48218); p10 of the Z profile (56150-56152) |
| +0x30 | {int shape, int ent, double t, CPt2 pt} | run-in start position A on C+0x340 (48220-48225) |
| +0x50 | {int shape, int ent, double t, CPt2 pt} | run-in end position B (48227-48232); state-2 sweep start (§3 D 2.2) |
| +0x70 | CSh (0x98) | precomputed path (strategy (1) of FUN_180061cd0, 56104-56110) |

Every strategy that succeeds and has `target+0x22 != 0` appends the run-in section
`FUN_180146e20(C+0x340[t+0x30], t+0x34, t+0x38, t+0x54, t+0x58)` to its link, so the link ends at B.

**Group selector FUN_180047920 (42563-42688)** `(C, set, P, Q, out)`: if `nonIslandCount(set) == 1`, out = all of
`set` (42604-42621). Otherwise find the nearest shape index of P and of Q (FUN_18013d010) and scan nest groups
[islands…, first non-island] in list order; out = the first group whose index range contains both indices
(42630-42675). No such group → false.

**Route-around FUN_18010c040 (144757-144853)** `(both, set, line, out)` — shortest path from the line's start to its
end that follows the line where it is inside `set` and follows `set` boundaries elsewhere:
```
isab = Isect(line, set)  (FUN_180134270)                                                 // 144793-144794
if isab empty:
   if set non-empty and FUN_1801489a0(line whole, last shape of set, 1e-5) != 2: goto general   // 144797-144806
   mark every line entity: byte +0x13 |= 4; out = { line }; return                       // 144808-144823
general: FUN_180109960, FUN_18010ad50, FUN_180109e20 prepare isab, then FUN_18010a960        // 144826-144830
```
- FUN_1801489a0 (176706-176764) samples points along the section (FUN_180148820, NOT DECODED) and returns the first
  `PtInSh` result that is not 1 (on boundary); PtInSh (33-206): 2 inside (odd crossing count), 4 outside, 1 on.
- FUN_18010ad50 (144148-144186): for each consecutive node pair i→i+1, `isab[i]+0x38 = 1` if the line section
  between them gives FUN_1801130f0(…, set, 1e-5) == 2 (inside a non-island shape and not inside an island,
  148390-148438), else 3; the last node gets 1 (144177-144183).
- FUN_18010a960 (144026-144145) = shortest-path relaxation over nodes in line order. Node i with class 1: edge
  i → i+1 along the **line** (FUN_18010af10 mode 0: section of the line, every entity gets +0x13 |= 4,
  144219-144295). Other nodes: edge along the **set boundary forward** to node isab[i]+0x30 (mode 1, FUN_180146e20,
  144366-144429) and, only if `both`, **backward** to node isab[i]+0x34 (mode 2, FUN_180147d30, 144300-144365).
  Relaxation FUN_18010c3a0 (144856-144875): `dist[j] = dist[i] + len(edge)` if smaller (len = FUN_180121980).
  Mode 1/2 edges: the target j = next (mode 1) / prev (mode 2) of i is passed over along the set while its line position
  (double)X entity + X t is not beyond i's (back at i: no edge), then rejected when j <= i in the list (corrected
  2026-11-07 from the decompile 144338-144352 / 144389-144403: earlier read as "rejected when not after i" without the
  pass-over; the unit oracle tools/peloader --ct shows the DLL's routes taking those longer edges).
  If the last node is reached, FUN_18010b6f0 builds `out` (144121-144123; internals NOT DECODED).
- FUN_18010a6b0(sh, &onLine, &offLine) (143923-143995): sum of entity lengths with +0x13 bit 4 set → `onLine`, the
  rest → `offLine` (the boundary detour). FUN_18010a8c0 (143998-144023) clears bit 4 again.

**Arc–line–arc connector FUN_18011f480 (154721-155014)** `(P, T, ρ1, d1, Q, V, ρ2, d2, out)`:
```
n1 = (d1 == 1) ? (-Ty, Tx) : (Ty, -Tx) normalised (fail if |·| < 1e-12);  c1 = P + ρ1*n1   // 154793-154810
n2 = (d2 == 1) ? (-Vy, Vx) : (Vy, -Vx) normalised;                        c2 = Q + ρ2*n2   // 154811-154828
tangents = FUN_1800a96d0(c1, ρ1, c2, ρ2)    // pairs (p_k on circle 1, q_k on circle 2), type byte per pair
for each pair k with (d1 == d2 ? type&1 : type&2):                                         // 154834-155009
   s = sign((p_k - c1) x (q_k - c1))   (0 if exactly 0)                                    // 154845-154853
   if s == d1 or type == 6:
      a1 = atan2(P - c1); sw1 = atan2(p_k - c1) - a1 wrapped to [-pi, pi];
           if d1 == 1 and sw1 < 0: sw1 += 2pi;  if d1 == -1 and sw1 > 0: sw1 -= 2pi        // 154858-154883
      a2 = atan2(q_k - c2); sw2 = atan2(Q - c2) - a2 wrapped; same sign rule with d2       // 154886-154912
      out = [ 'A'(c1, ρ1, a1, sw1) unless degenerate(ρ1, sw1),
              'L'(degenerate1 ? P : p_k  ->  degenerate2 ? Q : q_k) unless type == 6,
              'A'(c2, ρ2, a2, sw2) unless degenerate(ρ2, sw2) ]                            // 154914-154994
      out+0x50 = 0 (open); return non-empty                                                // 154995-155000
   (a pair that passes the type test but fails s == d1 is skipped - the loop goes on with the next pair, index + 2 over
   the point list, 154854 / 155007; the break at 155004 is taken only when a direction is degenerate (P on c1, p_k on c1,
   q_k on c2 or Q on c2 within 1e-12). Corrected 2026-11-07: earlier read as "ends the search", unit oracle --ct)
```
- degenerate = FUN_180123070 (156372-156381): `|ρ·sw| < 1e-5` or `|ρ·2π| − |ρ·sw| < 1e-5`.
- FUN_1800a96d0 (92232-92294) calls FUN_1800a97c0 with the larger circle first and swaps each pair back.
  FUN_1800a97c0 (92297-92434), circles (c1, r1 ≥ r2), φ = atan2(c2 − c1), D = |c2 − c1|:
  - if r1 − r2 ≤ D + 1e-13: α = acos(min(1, (r1 − r2)/D)); pair (c1 + r1·e(φ+α), c2 + r2·e(φ+α)) type 1; if α ≤ 1e-12
    the type becomes 5 and there is no second pair, else pair (c1 + r1·e(φ−α), c2 + r2·e(φ−α)) type 1.
  - and if r1 + r2 ≤ D + 1e-13: β = acos(min(1, (r1 + r2)/D)); pair (c1 + r1·e(φ+β), c2 + r2·e(φ+π+β)) type 2; if
    β ≤ 1e-12 the type becomes 6 and there is no second pair, else (c1 + r1·e(φ−β), c2 + r2·e(φ+π−β)) type 2.
  - otherwise (r1 − r2 > D + 1e-13) no pairs.
  (e(a) = (cos a, sin a).)

**Radius lists FUN_180064690 (57517-57608)**: the ρ column of FUN_180064980's candidate table (§3 D 2.6), with
consecutive values closer than 1e-5 removed (57545-57570) and negative values removed (57573-57597); also returns
Lmax = 8·P+0x1c8 (FUN_180064980, §3 D 2.6).

**Approach to a target FUN_180060770 / one candidate FUN_18005c980**: decoded in §6.4.4 (used by FUN_180044870
with allowFallback = 0, containSet = S2, clipSet = S4, 41133-41134).

### 6.3.1 Order inside FUN_180061cd0 (56068-56157) — unchanged from §3 D 2.4, now with parameters
`FUN_180061cd0(C, cetp, targets, B = C+0x340 | local window, S2, S3, S4, allowLast, out, &idx, &cross)`;
`idx = -1, cross = 0` at entry (56101-56102). General link passes the FUN_180062a70 sets (58097-58100); local link
passes the window sets of §6.3.6 with `allowLast = 0` (59510).

### 6.3.2 Strategy (2) FUN_18005ff90 (55114-55366) — only if !(flags & 4): short boundary-hugging hop
```
for i over targets in list order:                                                       // 55227-55306
   if |cur - t_i.pt| > C+0x1d8 (P+0x1d0): break            // targets are sorted by distance (6.3.5)   // 55236-55239
   G = FUN_180047920(S2, cur, t_i.pt) or continue                                         // 55242
   if |G| > 1: nearest-shape(cur) == nearest-shape(t_i.pt) and both points within 1e-10 (squared) of G,
               else continue                                                            // 55244-55257
   r = FUN_18010c040(0 /*forward only*/, G, L(cur -> t_i.pt))                           // 55262-55276
   if r non-empty and len(r[0]) <= C+0x1d8:                                             // 55277-55279
      (onLine, -) = FUN_18010a6b0(r[0]); clear bit 4                                     // 55281-55282
      if onLine <= 1e-5: append run-in if t_i+0x22; out = r[0]; idx = i; break          // 55283-55298
```
So the hop is accepted only when the route has (almost) no straight portion inside S2, i.e. it runs along S2
boundaries (FUN_18010c040 marks the inside-S2 line portions with bit 4). `cross` is not written (stays 0).
P+0x1d0 = (0.25·P+0x40 + P+0x1c0)·2.5 (§3 B.2).

### 6.3.3 Strategy (3) FUN_180044870 (40883-41346) — exit + connector + approach, shortest wins
```
if S4 empty: return                                                                     // 41105
(exit, exitFlag) = FUN_18005eca0(C, cetp, S2, S4)  or return     // §3 D 2.6; exitFlag = 1 iff cur is inside the
                                                                  // clip set and P+0x21f (54574, 54586-54597)
best = bestB = 3.4028234663852886e38 (FLT_MAX)                                          // 41067-41068
for i over targets:                                                                     // 41116-41191
   if |cur - t_i.pt|^2 > best^2: break                                                   // 41129-41130
   appr = FUN_180060770(C, 0, t_i, S2, S4) or continue                                   // 41133-41134
   link = FUN_180045470(C, cetp, B, S2, S3, S4, exitFlag, exit, appr, &cr) or continue   // 41137-41139
   append run-in if t_i+0x22                                                            // 41141-41147
   L = len(link); h = CalcMaxShHdChg(link)   (max heading change at entity junctions, 180003f30 l.6762ff)   // 41149-41154
   if C+0x227 (P+0x21f) == 0:  if L < best: take (cross = cr, idx = i, best = L)         // 41155-41163
   else: if L < best and |h| < 0.17453292519943295 (10°): take, best = L                 // 41166-41173
         if L < bestB: backup = (link, cr, i), bestB = L                                  // 41175-41182
if P+0x21f and idx == -1 and backup exists: take backup                                  // 41193-41197
```
**FUN_180045470 (41347-41391)** — tries five connectors, first success wins; `set = P+0x21e ? S4 : S3`
(41361-41363), `*cr = 0` at entry (41359):
1. **FUN_180042630 (39755-39813)**: if exit and approach intersect (FUN_180133c70, first record; ordering NOT DECODED),
   link = exit[start → X] + appr[X → end] (FUN_180146e20 ×2), then FUN_18004a070(C, S2, link) (NOT DECODED, post-process).
2. **FUN_1800410f0 (39088-39751)** — arc–line–arc straight to the target:
   - pre = FUN_18005e690(C, B, S2, exit, appr) (BIDIR retrace, below) (39333).
   - Start E = pre's end point and end tangent, or, when pre is empty, the **start** point/tangent of the exit
     (= cur) (39336-39429). End A = end point and end tangent of the approach (= target) (39431-39485).
   - G = FUN_180047920(set, E, A) or fail (39489).
   - ρ lists LA = LB = FUN_180064690 (and Lmax); if !P+0x21e: LA = [P+0x228 if (P+0x21f and exitFlag)] + [P+0x40],
     LB = [P+0x40] (39491-39507).
   - loop: for (d1, d2) in (1,1), (−1,−1), (1,−1), (−1,1) (local_d8, 39216-39223):
     p = FUN_18011f480(E, ρ=LA[0], d1, A, ρ=LB[0], d2); keep p if FUN_180113300(p, S2, 1e-5) & 4 == 0 and
     FUN_180046f80(G, p) gives head ≤ Lmax, tail ≤ Lmax, ok, and len(p) < best (39512-39536).
     If none: drop LA[0] on even rounds when |LA| ≥ 2, else drop LB[0] when |LB| ≥ 2, else LA[0] if |LA| ≥ 2, else stop
     (39544-39596).
   - link = pre + best (39600-39606) (the approach entities are not appended: best already ends at the target).
   - FUN_180046f80 (42211-42414): nodes = [path start] + Isect(path, G) + [path end]; class_k = FUN_1801130f0(section
     k→k+1, G, 1e-5); first/last = first/last k with class 2; head = length start → node[first], tail = length
     node[last+1] → end (FUN_180149d80); ok = first exists and every class from first to last is 2.
   - FUN_18005e690 (54254-54447), only if flags & 4: D = 2·C+0x1d0 (2·P+0x1c8). S = exit start, T = approach end,
     d = |S − T|; require d ≤ D, S on B (nearest within 1e-8 squared); walk back D − d along that B shape
     (FUN_18006f4f0) to point W; require |W − T| ≥ 0.75·D and ≥ d; pre = reversed section W→S; keep it only if
     FUN_180065b20(C, S2, pre) (NOT DECODED).
3. **FUN_18003fec0 (38544-39084)**: same as 2 but from **exit end** (point + end tangent) to **approach start**, group
   from `set`, accept if FUN_180113300(p, G, 1e-5) & 4 == 0 and shortest; link = exit + best + appr (38974-38978).
   Direction pairs and list shrinking as in 2 (38672-38679, 38909-38972).
4. **FUN_180042890 (39816-40438)**: if C+0x269 (P+0x261 bUseCtourGuide) == 0 → **FUN_180044340** (below). Else: exit end
   → approach start, G = FUN_180047920(S4, E, A); for each direction pair p = FUN_18011f480(...);
   r = FUN_18010c040(1 /*both directions*/, G, p); require exactly one shape and !FUN_1800457d0(r) (FUN_18012f3b0
   non-empty, NOT DECODED); `cr = offLine length of r` (40240); FUN_18004a070(C, S3, r); cand = exit + r + appr;
   FUN_180132200(1e-6, cand); require FUN_1800458c0(exit, appr, cand); keep shortest (40224-40266). After the loop,
   FUN_180045c40(C, S3, link) (NOT DECODED); none → cr = 0 (40313-40322).
   - FUN_1800458c0 (41482-41563): start tangent of cand within 0.017453292519943295 (1°) of the exit start tangent, end
     tangent within 1° of the approach end tangent, and CalcMaxShHdChg(cand) ≤ 1°.
5. **FUN_180043c90 (40442-40702)**: if P+0x261 == 0 → FUN_180044340. Else: E = exit end, A = approach start (points
   only), G = FUN_180047920(S4, E, A); r = FUN_18010c040(1, G, L(E → A)); require one shape; `cr = offLine(r)`;
   FUN_18004a070(C, S3, r); link = exit + r + appr; FUN_180049950(C, S2, exit, appr, link); FUN_180045c40(C, S3, link)
   (40551-40655).
- **FUN_180044340 (40706-40879)**: E = exit end point + end tangent, A = approach start point + start tangent;
  G = FUN_180047920(S4, E, A); FUN_1800f0fa0(cetp, 0.9·C+0x1d0, C+0x1e0, 1, E, A, G, &r); require exactly one shape;
  link = exit + r + appr (40812-40857). `cr` is not written here, so with the default P+0x261 = 0 connectors 4 and 5
  return cr = 0.

**The `cross` value** (input of the retract decision FUN_180064180, §3 D 2.3) is therefore: 0 for strategies (1), (2),
(4) and for connectors 1-3 and FUN_180044340; the boundary-detour (offLine) length of the routed connector for
connectors 4/5 when P+0x261 is set; and the value returned by strategy (5) below.

### 6.3.4 Strategy (4) FUN_18005d5f0 (53770-53987): direct arc–line–arc to target 0
```
ρ0 = C+0x226 (P+0x21e) ? C+0x1e0 (P+0x1d8) : C+0x48 (P+0x40)                            // 53831-53838
T = unit(curT) (fail if < 1e-12)                                                        // 53859-53866
d1 = (flags&4) ? ((flags&1) ? -1 : 1) : ((flags&1) ? 1 : -1);  d2 = (flags&1) ? 1 : -1  // 53867-53884
only targets[0] is tried; V = unit(t0+0x10) (skip if < 1e-12)                           // 53891-53900, 53971-53974
for k in 1.0, 0.75, 0.5, 0.25, 0.1, 0.01:          (table 53822-53830, ends at -1.0)
   p = FUN_18011f480(cur, T, k*ρ0, d1, t0.pt, V, k*ρ0, d2)                              // 53906-53908
   X = FUN_180134690(p, S2); if any X point is > 1e-5 from both cur and t0.pt: p = {}     // 53909-53931
   if p non-empty: m = point at half length (FUN_18014a290); if !ok or FUN_180112e80(S2, 1e-4, m) != 2: p = {}  // 53936-53948
   if p non-empty: append run-in if t0+0x22; out = p; idx = 0; return                   // 53951-53963
```

### 6.3.5 Strategy (5) FUN_18005f4c0 (54744-54969), only when allowLast
- If C+0x269 (P+0x261 bUseCtourGuide) == 0 (registry default, §3 B.2): **FUN_18005fb50** (54973-55110): for each
  target i in order: G = FUN_180047920(S2, cur, t_i.pt); `FUN_1800f0fa0(cetp, 1e-6, C+0x1e0, (flags>>2)&1, cur(+curT),
  t_i(+dir), G, &res)`; first non-empty result wins: append run-in, out = res.shape[0], idx = i,
  `cross = res+0x28` (the double stored right after the result list, 55082).
- Else (54851-54909): for each target i: G = FUN_180047920(S2, cur, t_i.pt);
  r = FUN_18010c040((flags>>2)&1, G, L(cur → t_i.pt)); first non-empty: `cross = offLine(r[0])`, append run-in,
  out = r[0], idx = i.
- **FUN_1800f0fa0 (130591-130606)** = FUN_1800f1080 (guided planner); if that fails or does not give exactly one shape,
  FUN_1800f1580 = the plain FUN_18010c040 route of the straight line, keep shape 0, cross = offLine (130828-130870;
  the decompiler shows its arguments as stack slots). FUN_1800f1080 (130609-130759) internals are **NOT DECODED**:
  it copies G, calls FUN_1800f33a0, FUN_1800f2480(P, Q, G), FUN_1800f3220(0.9·a, G), FUN_1800f3de0(0.1·a, P, Q, G),
  FUN_1800f2b40; on failure FUN_1800e9790(a, b, P, Q, G), FUN_1800f3220, FUN_1800f2ca0, a rotation by FUN_1800f18a0's
  angle (RotationMx + FUN_180125ec0), FUN_1801009c0, FUN_1800f61a0, FUN_1800eda20, inverse rotation, FUN_180136370(a, G,
  …); success needs exactly one output shape with FUN_180113300(out, G, 1e-5) & 4 == 0 (130705-130709).
  (a, b) = (1e-6, P+0x1d8) from FUN_18005fb50, (0.9·P+0x1c8, P+0x1d8) from FUN_180044340.

### 6.3.6 Target builders
**General link FUN_180052a90 (48317-48400)** `(C, B=C+0x340, bands=C+0x368, S1, S2, S4, cetp, C+0x730, mode, targets)`,
mode = FUN_18006e3f0 (1 if MOAT_FIRST and C+0x350[0]+0x90 bit 4, else P+0x20; §3 D 2.2):
```
targets = {} (FUN_180053140 48477-48528); reserve 2*|S1|
FUN_180052070(C, B, S1, C+0x730, targets, &used)        // boundary targets, below       // 48372
FUN_180052d70(C, 0, B, bands, S1, cetp, targets, &used) // targets on S1 shapes           // 48373
sort targets by squared distance of +0x00 to cur, ascending (FUN_18004d4b0 introsort; comparator 45137-45153)
mode 1: P+0x239 (bIsCutOrdLenBas) == 0 ? FUN_180055190(...) : FUN_180055c90(...)          // 48377-48383
mode 2: FUN_180054a70(C, cetp, S2, S4, targets)                                           // 48385-48386
else  : FUN_180054080(C, targets)                                                         // 48389
```
- **FUN_180052070 (48000-48063)**: for each shape i of B: X = FUN_180134690(B[i], S1); FUN_180053480 (NOT DECODED);
  FUN_1800522c0(C, B, i, S1, X, C+0x730, targets, used).
- **FUN_1800522c0 (48067-48313)**: sections = FUN_180054450(B, i, S1, X, …) (NOT DECODED; 32-byte records: +0x08 node
  index, +0x10 class, +0x18 length). For each section k (prev/next cyclic): if class == 2, or class == 1 with prev and
  next class 4 (48177-48180):
  `d = C+0x1f0 (P+0x1e8 = 1.5 × march step); if len(prev) <= d: d = 0.5·len(prev)` (48181-48184);
  Bpos = the node's position on B[i] (X ent +0x14, t +0x18, point +0x00); Apos = Bpos walked back d along B[i]
  (FUN_18006f4f0) (48186-48194); unless FUN_180053cd0(class, A.pt, C+0x730) (NOT DECODED, a filter against the C+0x730
  point list): add target {pt = A.pt, dir = tangent of B[i] at A, +0x22 = 1, +0x28 = d, +0x30 = Apos, +0x50 = Bpos}
  and record the S1 shape index (X +0x20) in `used` (48198-48276). Then FUN_180053760 (NOT DECODED).
  The meaning of section classes 1/2/4 is **NOT DECODED**.
- **FUN_180052d70 (48403-48473)**: curShape = nearest S1 shape to cur. For each S1 shape i not in `used`, with
  FUN_180113300(S1[i], B, 1e-5) != 4 (and, when called with flag 1, result & 2): c = FUN_180053d60(C, S1, i, cetp);
  unless (i == curShape and FUN_180059630(C, B, S1, cetp, c, targets)): c == 0 → FUN_1800512c0(C, B, bands, S1, i, cetp,
  targets), else FUN_180051cd0(C, B, S1, i, cetp, targets). (All four callees NOT DECODED.)
- **FUN_180054080 (48969-49086)** (mode 0): if more than one target: for each target, key = CSh+0x58 of the material
  piece nearest to its point (FUN_18013d010 over C+0x410; the piece list begins at C+0x420); remove every target whose key
  is not the maximum key (order kept).

**Local link (FUN_180067d80, 59488-59520)**: piece k from FUN_1800686c0 (§3 D 2.2), then
- **FUN_180066320 (58475-58740)** window: box = bbox(C+0x420[k]) ∪ cur; m = max(2·(C+0x1e0 + C+0x30 + C+0x1d0) +
  C+0x58, box width, box height) = max(2·(P+0x1d8 + R + P+0x1c8) + P+0x50, w, h); box grown by m on every side
  (58544-58572); if mode == 2: FUN_180067820(C, cur, box) (NOT DECODED); W = FUN_18015b2e0(1e-6, rect(box), C+0x340)
  (NOT DECODED boolean); if nonIslandCount(W) != 1, W = the first nest group containing cur (FUN_180112a00 result 1 or 2)
  (58608-58700); FUN_18002c0e0(flags, 2, W).
- **FUN_180066ff0 (58896-59138)** = FUN_180062a70 on the window: material = pieces of C+0x420 whose bbox overlaps
  bbox(W) grown by R + 1e-5 (FUN_180067930 59176-59299); S1' = offset(material, R − r, 0x100); S2' = FUN_180153b30(1e-6,
  W, S1'); S3'/S4' as in §6.4.2 (same d1/d2; the retry counter starts at 0, `uVar8 = 0` l.59007).
- **FUN_180066ad0 (58744-58892)**: targets as in FUN_180052a90 but with FUN_180052d70 flag 1 and the window sets; sorted
  by distance; if mode == 2: FUN_180054a70; else remove every target whose point (B position +0x60 when +0x22, else
  +0x00) is not at distance (R − r) ± 1e-4 from piece C+0x420[k] (FUN_18013d200) (58835-58867).
- Then FUN_180061cd0(C, cetp, targets, W, S2', S3', S4', allowLast = 0, …) (59510).

### 6.3.7 The P+0x261 = 1 branch (bUseCtourGuide), decoded 2026-11-07 (user decision D-3: X9's NCIs follow it)
P+0x261 (C+0x269) is read only at 40016 (connector 4), 40551 (connector 5) and 54845 (strategy 5); set at 60377 from the
registry (default 0). With it set:
- **Connector 4 FUN_180042890 (39816-40438)** `(C, cetp, S3, S4, exitFlag, exit, appr, out, &cr)`: radius lists LA = LB =
  FUN_180064690 (no Lmax); E = exit's last point, Te = its end tangent (raw: 'L' p2 - p1, 'A' the radius vector times |sw|
  turned by 90 deg), A = approach's first point, Ta = its start tangent; G = FUN_180047920(S4, E, A) or fail; if !P+0x21e: LA =
  [P+0x228 if P+0x21f and exitFlag] + [P+0x40], LB = [P+0x40]. Rounds: for (d1, d2) in local_d8 = (1,1), (-1,-1), (1,-1),
  (-1,1): p = FUN_18011f480(E, Te, LA[0], d1, A, Ta, LB[0], d2); r = FUN_18010c040(1, G, p) (the connector path routed, both
  directions); one shape and !FUN_1800457d0(r) -> cr' = offLine(r) (FUN_18010a6b0, then FUN_18010a8c0), FUN_18004a070(C, S3,
  r), cand = exit + r + appr (FUN_180146b10 x3), FUN_180132200(1e-6, cand), kept if FUN_1800458c0(exit, appr, cand) and
  shorter (FUN_180121980; the first kept wins ties). Nothing kept -> the list shrink of connector 2 (40420-40435, same rule);
  after the loop: none -> cr = 0, fail; else FUN_180045c40(C, S3, out), success.
- **Connector 5 FUN_180043c90 (40442-40702)** `(C, cetp, S2, S3, S4, exit, appr, out, &cr)`: E = exit's last point, A =
  approach's first point; G = FUN_180047920(S4, E, A) or fail; r = FUN_18010c040(1, G, L(E -> A)); exactly one shape or fail;
  cr = offLine(r); FUN_18004a070(C, S3, r); out = exit + r + appr; FUN_180049950(C, S2, exit, appr, out); FUN_180045c40(C, S3,
  out); success. (Call sites, disasm: 0x180044008 route, 0x180044058, 0x180044095, 0x1800440a3.)
- **Strategy (5) FUN_18005f4c0's own loop (54851-54909)**: per target i: G = FUN_180047920(S2, cur, t_i); r =
  FUN_18010c040((flags >> 2) & 1, G, L(cur -> t_i)); the first non-empty: cross = offLine(r[0]) (+ clear), run-in, out = r[0].
- **FUN_1800457d0 (41420-41445)** = FUN_18012f3b0(r, list, 0) gives a non-empty list (the route crosses itself).
- **FUN_18012f3b0(sh, list, 0) (163223-163303)**: FUN_18012f620 (163304-163462): for entity pairs i < j whose boxes (+0x40..+0x58)
  overlap within 1e-5, CalcEntIntersect(1e-7); per hit (last first) unless FUN_18012ff20 (= FUN_18014b720(sh, i, ti, j, tj, 1e-6)
  either way: the hit is the junction of a short section) two ISIS records (0x38: point, +0x10 entity, +0x18 t, +0x30 partner);
  then FUN_180130060 sorts an index list with FUN_18012e7d0 (entity; t when the points differ by > 1e-15; partner's entity / t),
  FUN_18012ed20 (163020-163186) drops a record and its partner when its cyclic neighbour lies at the same point (squared
  9.999999999999998e-15) on a short section (1e-7) and the partners do too, FUN_18012e930 reorders the records with partners
  renumbered, FUN_18012fc00 re-sorts (entity, t) pairs (identity on a sorted list).
- **FUN_180132200(tol, sh) (164690-164790)**: loop removal at the self crossings: for i = 0 .. n-2 push i; FUN_180131740 (164328-
  164353): if the partner of record i+1 is on the stack (searched from the top), the stack is cut there; FUN_180131240
  (164180-164313) builds sh[start -> rec 0] + the stacked pieces rec s -> rec s+1 + sh[rec last -> end] (open), FUN_1800c7f00
  (108013-108052, NOT DECODED: FUN_1801239f0 / FUN_180126ac0 may drop a degenerate result) - then the result replaces sh.
- **FUN_1800458c0 (41482-41563)**: FUN_1801204a0(exit, 0, 0.0) vs FUN_1801204a0(cand, 0, 0.0) and FUN_18011ffd0(appr, last, 1.0) vs
  FUN_18011ffd0(cand, last, 1.0) (tol 1.0000000000000002e-10; a tangent with both components <= 1e-5 fails) each within
  0.017453292519943295 after wrapping the atan2 difference once into [-pi, pi]; and |CalcMaxShHdChg(cand)| <= the same.
- **FUN_18004a070 (44002-44486, disasm 0x18004a6e0-0x18004a815)**: as read above, with the order fixed by the disasm: the first
  successful distance index ends the loop (the decompile hides the jump to 0x18004a815). The distance list is 10m, 4m, 2m,
  1.125m, m, then 0.9m, 0.9m - 0.1m, ... (0.9 lowered by 0.1 nine times).
- **FUN_180048c70 (43250-43369, disasm 0x180048d40-0x180048da3)**: from index i: FUN_180048840(d[k], cur, out, &changed): failure
  -> 0; !changed -> 1 with that result; else cur = out, k + 1; past the end -> 1 with cur. *iout = the last k.
- **FUN_180048840 (43097-43249, disasm 0x180048a21 / 0x180048a48 -> 0x180048ae4 = return 0)**: list = {copy(cur)}; for δ in
  (d, -d, -d, d): MCCECalcContourOffset(1, δ, list, out, 0x101, 0.0, -1.0, 1e-6, 1e-6); FUN_180048f60(out) or fail; on passes 1
  and 3 (index & 0xfd == 1) with cur open: FUN_180049150(cur, out[0], &changed) or fail; list = out. Result = list[0].
- **FUN_180048f60 (43380-43473)**: the longest open shape (> from -1.0), else the longest closed one, stays alone; empty -> 0.
- **FUN_180049150 (43474-43714, disasm 0x180049540-0x18004971a)**: S0/E0 = orig's ends, S1/E1 = res's; |S1 - S0|^2 > 1e-12: q =
  FUN_18013d200(S1, orig), fail if |S1 - q|^2 > 1e-12, tmp = orig[0 -> q] + res; then |E1 - E0|^2 > 1e-12: q = nearest of E1, fail
  if off, tmp (= res if empty) + orig[q -> last, 1.0]; a non-empty tmp replaces res and sets changed; 1.
- **FUN_180049950 (43715-44001)**: as read above (walks FUN_18006f1c0 / FUN_18006f4f0 mode 0 from (0, 0, 0.0) / (0, last, 1.0)).
- **FUN_180045c40 (41567-42210, disasm 0x1800463d0-0x1800465ef)**: section of FUN_180048320 (whole link when no crossing); first /
  last = the first / last non-arc entity of it (all arcs: nothing); each arc strictly between them is replaced by the chords of
  ApxArcWithLnSegs(0.0001, arc, pts, true) (fresh 'L' entities pts[k-1] -> pts[k]) unless |sw| > pi/2 and |r sw| > 1e-4, or |r sw|
  > m pi (m = P+0x21e ? P+0x1d8 : P+0x40, constants 0x18017e380 = pi/2, 0x18017e148 = 1e-4, 0x18017e1b8 = pi) and FUN_1800ab630
  (93134-93184) gives more than one segment; link = link[0 -> I0] + new section + link[I1 -> end].
- **MCCECalcContourOffset (export 8571-8807)** on the open routes: its kernels (FUN_1800ce9a0, FUN_1800cd040, FUN_1800d50d0,
  FUN_1800ce670, FUN_1800da1c0) are NOT DECODED. Measured with the unit oracle (CT_COFF=1 prints every pass's input and output
  at the call 0x180048a11): the output is the raw one-sided offset (d > 0 left) trimmed to the points at least |d| from the
  input, pieces ending where they come to |d| from an input vertex, an arc whose offset radius reaches 0 kept as an arc of
  radius 0 (centre, start angle, sweep), the input's radius-0 arcs themselves not offset, round joins about a vertex with
  start angle atan2(A - V) (+ 2pi when a clockwise join ends at a larger atan2), pieces split where the raw offset crosses
  itself left split. viewer/mcce_dyn.c oc_offset is that construction (ours); every pass of buckle-v8 op 42 call 23 equals the DLL's
  (unit oracle tools/tests/mcce_ct_oracle.sh, CT_COFF=1).

### NOT DECODED (6.3)
- FUN_180153b30 / FUN_18015b2e0 boolean cores (exact set operation; FUN_18015b2e0 is the pairwise step of
  `MCCECalcPockIntersect`, §6.5.6).
- Route internals: FUN_180109960, FUN_180109e20 (ISAB +0x30/+0x34 linking), FUN_18010b6f0 (path build),
  FUN_180148820 (section sample points). (FUN_180146c10 / FUN_180147d30, chord fallback / backward section copy: decoded §6.11, ported in route_edge 2026-11-13.)
- FUN_180133c70 / FUN_180134690 record order (which intersection is "first"/"last").
- Guided planner FUN_1800f1080 and all its callees (0x1800e9790 … 0x1800f61a0, FUN_180136370); FUN_180060cd0
  (approach fallback, 55563-56067).
- Link post-processors FUN_18004a070 (44002-44486), FUN_180049950 (43715-44001), FUN_180045c40 (41567-42210),
  FUN_180132200; FUN_1800457d0's FUN_18012f3b0; FUN_180065b20 (BIDIR retrace check).
  **FUN_18004a070(C, CONT, link) read 2026-11-07 (link smoothing):** m = C+0x226 ? C+0x1e0 : C+0x48 (P+0x1d8 / P+0x40,
  44012-44019); FUN_180048320(C, CONT, link, &I) (§6.7.7) selects the section (whole link if |I| = 0, else I[0] → I[1]);
  distance list d = [10m, 4m, 2m, 1.125m, m, 0.9m, 0.8m … 0.1m] (44150-44312); i = 0; repeat: ok = FUN_180048c70(C, d, i,
  &i, section, → out); if ok and FUN_180113300(out, CONT, 1e-5) & 4 == 0: link = out (|I| = 0) or section(link, 0 → I[0]) +
  out + section(link, I[1] → end) (FUN_180146e20 / FUN_180146b10), stop; else i = i + 1 (44317-44346).
  FUN_180048c70 (43250-43369): cur = section; for k from i: ok = FUN_180048840(C, d[k], cur, → out, &changed); !ok → fail;
  !changed → done (out); cur = out. FUN_180048840 (43097-43249): four passes MCCECalcContourOffset(1, δ, cur, 0x101, 0.0,
  −1.0, 1e-6, 1e-6) with δ = +d, −d, −d, +d (an opening and a closing of the open route); after each pass FUN_180048f60
  (43380ff) keeps the longest open shape (else the longest closed one); on passes 1 and 3 an open input goes through
  FUN_180049150. **FUN_180049150(orig, res, &changed) (43474-43714):** S0/E0 = start/end of orig, S1/E1 of res; if
  |S1 − S0|² > 1e-12: q = nearest point of orig to S1 (FUN_18013d200), fail if |S1 − q|² > 1e-12, else tmp = orig[start → q]
  + res; if |E1 − E0|² > 1e-12: q = nearest of orig to E1, fail if off it, tmp (= res if still empty) += orig[q → end]; a non-
  empty tmp replaces res and sets `changed` (the original end pieces are re-attached after an opening / closing pass).
  **FUN_180049950(C, CONT, exit, appr, link) (43715-44001):** mid = len(link) − len(exit) − len(appr); only if mid ≥ 1e-4:
  a = min(len(exit), 0.49 mid), b = min(len(appr), 0.49 mid); split link at len(exit) + a from the start (FUN_18006f1c0) and
  at len(appr) + b from the end (FUN_18006f4f0); FUN_18004a070(C, CONT, piece) on the first and the last piece; link =
  piece1 + piece2 + piece3 (the two junction zones are smoothed). **FUN_180045c40(C, S3, link) (41567-42210):** after
  FUN_180048320 selects the section, arcs of it are replaced by `ApxArcWithLnSegs(0.0001, arc)` line pieces (conditions on
  |sw| > pi/2 and |·| ≥ 1e-4 at 41950-41990 not fully read): the runs of 0.026-0.032 mm moves in Mastercam's NCI are
  linearised arcs (chord for tol 1e-4 on r ≈ 0.9 mm: 2·sqrt(2·r·1e-4) ≈ 0.027 mm) - geometrically the arcs themselves.
  **Where they run (2026-11-07, corrected):** with the default P+0x261 = 0 (bUseCtourGuide, every corpus op) FUN_18004a070
  is reached only through connector 1 (exit and approach intersect, 39791); FUN_180049950 / FUN_180045c40 only through
  connectors 4/5 with P+0x261 set. Over the 41 ops connector 1 is taken once (BRACKET4 op 14 and its copy). The routed links
  of ops 3, 10, 32, 36, 42, v12 22 (4-31 per op) come from connectors 4/5 → FUN_180044340 → **the guided planner FUN_1800f0fa0
  / FUN_1800f1080**, not from this smoothing (my earlier attribution was wrong).
  **FUN_1800f1080 top level (130609-130762, read 2026-11-07):** G' = copy(G) (AppendByCopy), FUN_1800f33a0 (RevSh of every
  shape), FUN_1800f2480(E, A, G'), FUN_1800f3220(0.9·d, G') (offset), FUN_1800f3de0(0.1·d, E, A, G'), FUN_1800f2b40(G') - if it
  fails: FUN_1800e9790(d, C+0x1e0, E, A, G') → FUN_1800f3220(0.9·d) → FUN_1800f2ca0(E, A, G') → angle = FUN_1800f18a0(G') →
  RotationMx(angle) applied (FUN_180125ec0) → FUN_1800f1580 → FUN_1801009c0(G' → graph) → FUN_1800f61a0(graph) →
  angle' = FUN_1800eda20(graph, …, route) → RotationMx(angle') applied to the route → FUN_180136370(d, G, E-dir, route);
  success iff exactly one shape and FUN_180113300(route, G, 1e-5) & 4 == 0, else FUN_1800f0fa0 falls back to FUN_1800f1580.
  The callee tree is 413 functions / ~33 000 decompile lines (calltree of FUN_1800f1080, depth 9): NOT DECODED below the
  top level.
- Target generation: FUN_180053480, FUN_180054450 (section classes 1/2/4), FUN_180053cd0, FUN_180053760,
  FUN_180053d60, FUN_180059630, FUN_1800512c0, FUN_180051cd0; cut-order filters FUN_180054a70 (mode 2),
  FUN_180055190 / FUN_180055c90 (mode 1; all three call FUN_180050af0); FUN_180067820 (mode-2 window).
- Writer of CTarg +0x20. (+0x21 is written by FUN_180075f60 for the reused entry point, l.66825, §6.4.4. CSh+0x58 of
  material loops = CCetp record count when the loop was created by commit, FUN_180032a20 L31853-31891, §6.1.7.)

## 6.4 Region sets FUN_180062a70 and CCePock list origins

Line numbers are in `decomp/mcce_all.c`. C+x = CCePock offset, P+x = CDynPockParm offset, C+x = P+(x−8). RG = C+0x298.

### 6.4.1 CCePock list origins (FUN_18002a2a0 l.27305-27665)
A `CVe<CSh*>` at object offset X has its begin pointer at X+0x10 and its end pointer at X+0x18 (§3 B). Several
offsets that earlier sections treat as separate lists are in fact the **begin pointers** of the RG lists. Evidence:
FUN_18002a2a0 clears/fills RG+0xa8 through its begin/end at RG+0xb8/+0xc0 (l.27333-27336, 27384-27413), RG+0x178
through RG+0x188/+0x190 (l.27362-27365, 27467) and RG+0x1a0 through RG+0x1b0/+0x1b8 (l.27368-27371, 27559). FUN_180062a70
reads the count of C+0x410 as `*(C+0x428) − *(C+0x420)` (l.56613).

| CCePock offset | = RG offset | content (FUN_18002a2a0 unless noted) |
|---|---|---|
| C+0x340 (list), **C+0x350 = its begin pointer** | RG+0xa8 | current tool-centre region: leading islands + first non-island moved from RG+0x30 (§3 B.7) |
| C+0x368 | RG+0xd0 | CBands per current shape (bIsBandsAvail, FUN_180143d20(…,1e-5,sh), l.27416-27451) |
| C+0x3c0 | RG+0x128 | MCCECalcOffset(1, −P+0x40, RG+0xa8, 0x100) (l.27652-27654) |
| C+0x3e8 | RG+0x150 | MCCECalcOffset(1, max(R−r−1e-4, 0), RG+0xa8, 0x100) (l.27641-27650) |
| C+0x410 (list), **C+0x420 = its begin pointer** | RG+0x178 | current material (circle R−r) (l.27467ff) |
| C+0x438 (list), **C+0x448 = its begin pointer** | RG+0x1a0 | current bullnose material (circle R) (l.27559ff) |
| C+0x470 / C+0x498 | RG+0x1c8 / RG+0x1f0 | local material windows (FUN_18002a140, §3 C.1) |

Consequences for earlier sections:
- "index into C+0x420" (§3 C.0 `local_2f8`/matIdx) = index into the current material list C+0x410.
- "C+0x350[G.shape]" = shape G.shape of the current tool-centre region C+0x340.
- "C+0x448 non-empty" (the circle-2 gate in FUN_18006e250, §3 C.4) = the bullnose material list C+0x438 is non-empty.
- The material list C+0x410 is also rewritten after the entry: FUN_180070b60 appends to it (l.64024), then
  UpdDirShAr(0.001), DetNestLvlShAr and FUN_18002c0e0(flags, 4, C+0x410) (l.64122-64124). The per-step rewrite is
  FUN_180037790 (material subtraction, §3 C.4 commit), not decoded in this subsection.

### 6.4.2 FUN_180062a70 (l.56519-56840): the four region sets
Signature: `FUN_180062a70(C, out1, out2, out3, out4)`; all four outputs are cleared first (l.56589-56609).
```
d2 = C+0x1d0 (= P+0x1c8)                                         // l.56568
d1 = C+0x1c8 (= P+0x1c0)                                         // l.56567
if C+0x226 (= P+0x21e bIsVarRadAvail) != 0:                      // l.56610-56612
    d1 = d2 − 1e-4;  if d1 < 0: d1 = 0.5·d2
if material C+0x410 is empty:                                    // l.56613-56616
    out2 = copy(C+0x340)                     (out1 stays empty)
else:                                                            // l.56617-56632
    tmp  = copy(C+0x410); UpdDirShAr(0.001, tmp); DetNestLvlShAr(tmp)
    out1 = MCCECalcOffset(1, R − r (C+0x30 − C+0x38), tmp, 0x100, 0.0, 1e-6, 1e-6)
    out2 = FUN_180153b30(1e-6, C+0x340, out1)          // centre region minus (material grown by R−r)
FUN_18002c0e0(flags, 2, out2)                                    // l.56633, orientation below
i = 0
while i < n(out2):                                               // l.56635-56786
    G = out2[i..] up to and including the first non-island (islands = CSh+0x54 bit0)    // l.56648-56752
    f = 1.0
    repeat 3 times (k = 0,1,2):                                  // l.56753-56781
        A = MCCECalcOffset(1, −f·d1, G, 0x100, 0.0, 1e-6, 1e-6)  // l.56766-56768
        B = MCCECalcOffset(1, −f·d2, G, 0x100, 0.0, 1e-6, 1e-6)  // l.56769-56771
        if FUN_180112860(A) == 1 and FUN_180112860(B) == 1:      // l.56772-56773
            out3 += A; out4 += B; break                          // l.56774-56776 (AppendByMove)
        f = 0.5·f                                                // l.56780
    i = index after G
```
- `FUN_180112860(list)` (l.148058-148080) = number of shapes in the list that are **not** islands (CSh+0x54 bit0 = 0).
  A group is accepted only if each inset gives exactly one outer shape. If all three tries fail (f = 1, 0.5, 0.25),
  that group adds nothing to out3/out4.
- d1, d2 in terms of B.2: P+0x1c0 = (flags&8 ? 0.254 : 0.01) capped at 0.125R; P+0x1c8 = P+0x1c0 + 0.25·rr.
- `FUN_18002c0e0(flags, 2, X)` (l.28316-28361): kind 2 → key `0x20000 | flags&1`. flags&1 = 1: outer +1, island −1;
  flags&1 = 0: outer −1, island +1 (RevSh on mismatch, l.28344-28358). (Kind 4, used for material, is the reverse,
  §3 B.1.)

Roles of the outputs, from their uses (names are descriptive only):
| output | content | used as |
|---|---|---|
| out1 | material C+0x410 grown by R−r | `FUN_180052a90` targets (param_4), approach candidates `FUN_180075f60` (param_3) |
| out2 | centre region C+0x340 minus out1, oriented | containment set: `FUN_180060770` (param_4, `FUN_180113300(path, out2, 1e-5) & 4 == 0`, l.55481-55482), exit `FUN_18005eca0` (param_3, l.59876), stay-down link `FUN_180061cd0` (param_5, l.58097-58098); FUN_180075a00 fails when out2 is empty (l.66592-66595) |
| out3 | each out2 group inset by d1 (or ½d1, ¼d1) | `FUN_180061cd0` param_6 (l.58098); `FUN_180075f60` param_5 |
| out4 | each out2 group inset by d2 (or ½d2, ¼d2) | clip set: `FUN_18005c980` crossing test (via FUN_180060770 param_5), exit `FUN_18005eca0` (param_4, l.59876-59877), `FUN_180052a90` param_6 |

Callers: general link FUN_1800653d0 (l.58090), final exit FUN_1800688e0 (l.59873), air-dwell FUN_180071df0 (l.64618),
approach FUN_180075a00 (l.66589).

### 6.4.3 FUN_180012390 (l.15564-15617): previous-position entity C+0x120
```
c = type byte of C+0x120 (pock_chn id at +8, = C+0x128)
if c ∉ {'A','L','P'}  (test (c−0x41) ≤ 15 and bit in 0x8801, l.15572-15573):
    type := 0                                                    // l.15574
    if AIR non-empty:
        s = first AIR shape with CSh+0x54 bit0 == 0 (first non-island)    // l.15576-15584
        if found: C+0x120 = copy of the first entity of s (0x88 bytes)    // l.15586-15612
                  entity z (+0x78) = C+0x88 + 1.0  (= P+0x80 + 1.0)       // l.15613
```
So, when the caller supplies no previous position, it is the start entity of the first outer AIR shape, placed 1.0
above the cut depth. With AIR empty the type stays 0. In FUN_180076b10 (§3 D 1.3) type 0 or |z − P+0x80| ≥ 1e-5 enable
the micro-lift descent, so this default always enables it (both conditions follow from l.15574/15613).

### 6.4.4 Approach-candidate code (FUN_180075a00 l.66501-66702)
**FUN_180077a50 (l.67583-67602)**: true iff flags & 0x10000000 (MOAT_FIRST) **and** a last output point exists
(FUN_18008e4b0) **and** `FUN_180112a00(out2, 1e-4, pt) != 4`, i.e. the last tool point lies in a shape of out2
(FUN_180112a00 returns 4 for "not in any shape or in a hole", §1 Peel helpers). When true, FUN_180075a00 skips
candidate generation (l.66597-66598).

**FUN_180075f60 (l.66709-66886)**, called as `(C, C+0x340, out1, out2, out3, out4, cetp, C+0x730, targets)`
(l.66599-66603):
```
mode = (C+0x268 (= P+0x260 bUseOldGslCutOrd) == 0) ? 2 : FUN_18006e3f0(C)        // l.66757-66763
targets = []                                                                   // l.66783-66790
if FUN_1800757e0(C, out1, cetp):            // reuse the previous entry point (§1 Peel 3 / l.66418-66489)
    targets = 1 record; rec+0x00 pt = end point of C+0x120:                    // l.66792-66825
        'L' → (C+0x148, C+0x150);  'A' → (C+0x138 + C+0x148·cos(C+0x150+C+0x158), C+0x140 + C+0x148·sin(…));
        otherwise → (C+0x138, C+0x140)
    rec+0x21 = 1;  return 1
if out2 empty: return 0                                                        // l.66828-66833
if MCCECalcOffset(1, −0.001, out2, 0x100, 0.0, 1e-6, 1e-6) is empty: return 0  // l.66836-66841 (result only tested)
FUN_180076500(C, out2, out3, out4, &C+0x120)   // reference chn (§1 Peel 3): sh = first non-empty of (out4, out3, out2)  l.66842
open a private CCetp on a temp file (FUN_180085ed0(…, path 0, C))              // l.66843
FUN_180052a90(C, C+0x340, C+0x368, out1, out2, out4, tmpCetp, C+0x730, mode, targets)   // l.66845-66847
return n(targets) != 0                                                         // l.66848-66855
(temp file deleted, l.66857-66866)
```
`FUN_18006e3f0` (l.62446-62452): 1 if MOAT_FIRST and C+0x340 non-empty and byte +0x90 of its first shape has bit
value 4; otherwise the byte C+0x28 (= P+0x20, op 0x16bf, §3 D 1.1).

**FUN_180052a90 (l.48321-48400), target builder; FUN_180054080 (mode 0); target record CTarg**: see §6.3.6 and
§6.3.0 (structure, distance sort, mode-0 filter on the maximum material CSh+0x58, record layout).

**FUN_180060770 (l.55373-55560), approach path into a target** — mirror of the exit move (§3 D 2.6):
```
FUN_180060770(C, allowFallback, T, CONT, CLIP, out):
  out = empty
  (ρ[], θ[], Lmax) = FUN_180064980(C)             // same list as the exit move, §3 D 2.6   l.55468
  L = |bbox max − bbox min| of CONT (GetMinMaxViaSh)                                     l.55470-55472
  for i in order:                                                                         l.55474-55489
      if FUN_18005c980(flags, CLIP, T, ρ[i], θ[i], L, → path)
         and FUN_180113300(path, CONT, 1e-5) & 4 == 0 and len(path) ≤ Lmax:  out = path; break
  if out empty and allowFallback: if FUN_180060cd0(C, T, CONT, ρ[], θ[], Lmax, L, → path): out = path   l.55491-55500
  return out non-empty
```
Called from FUN_180075a00 with CONT = out2, CLIP = out4 and allowFallback = 1 (l.66622-66625); from the retract link
FUN_180062140 (l.56350) with allowFallback = 1; from FUN_180044870 (l.41133) with 0.

**FUN_18005c980 (l.53399-53769), one approach candidate (flags, CLIP, T, ρ, θ, L, out)**:
```
if CLIP empty: return 0                                                    // l.53526
if T+0x20 != 0 or ρ ≤ 0 or θ ≤ 0:                                          // l.53530-53557
    q = nearest point of CLIP to T.pt (FUN_18013d010)
    out = 'L' q → T.pt ; return 1
d = T+0x10/+0x18; d' = (flags&1) ? (dx, −dy) : (−dx, dy); fail if |d'| < 1e-12; (ux,uy) = d'/|d'|   // l.53569-53584
c = T.pt + ρ·(uy, ux)                // = T + ρ·(−dy,dx)/|d| if flags&1, else T + ρ·(dy,−dx)/|d|      l.53585-53586
θ0 = θ
repeat k = 0..2:                                                           // l.53593-53702
    a0 = atan2(−ux, −uy)             // angle from c to T.pt                l.53598
    sa = wrap_(−π,π](a0 + (flags&1 ? −θ : +θ))                             // l.53600-53611
    arc = 'A' c, ρ, sa, sweep = (flags&1 ? +θ : −θ)  // ends at T.pt       l.53612-53620
    S = c + ρ(cos sa, sin sa); v = unit(S − c) (if |S−c| ≥ 1e-12)          // l.53621-53673
    v' = (flags&1) ? (vx, −vy) : (−vx, vy)
    line = 'L' (S − L·(v'y, v'x)) → S     // tangent line of length L ending at the arc start   l.53674-53689
    hits = FUN_180134690({line, arc}, CLIP)                                 // l.53691
    if hits non-empty or θ0 ≥ 1.9198621771937625 (110°): break               // l.53692
    θ = θ + (1.9198621771937625 − θ0)·0.5                                   // l.53694 (θ0, (θ0+110°)/2, 110°)
if hits non-empty:                                                          // l.53704-53716
    out = section of {line, arc} from (last ISAB .ent +0x14, .t +0x18) to (entity 1, t = 1.0)  (FUN_180146e20)
    return 1
return 0
```
So the approach is a straight line coming in from the clip set, tangent into an arc of radius ρ that ends at the target
point tangent to the target direction. The arc sweep grows towards 110° until the line crosses the clip set; the path
starts at the last crossing.

### NOT DECODED (6.4)
- FUN_180153b30 / FUN_180153390 boolean (region minus grown material) and MCCECalcOffset internals; also UpdDirShAr,
  DetNestLvlShAr.
- Target generation leaves: FUN_180053480, FUN_1800522c0 (boundary/material crossings → targets), FUN_180053d60,
  FUN_1800512c0, FUN_180051cd0, FUN_180059630 (per-material-shape targets).
- Target ordering modes: FUN_180055190 / FUN_180055c90 (mode 1, P+0x239 selects length-based), FUN_180054a70 (mode 2).
- (Resolved in §6.1.7: material loops created by commit get CSh+0x58 = CCetp record count, FUN_180032a20
  L31853-31891; FUN_18002a2a0 sets it to 0 for the region's initial material, §3 B.7.)
- FUN_180060cd0 (l.55563-56067): fallback approach path.
- Meaning of CTarg +0x20 / +0x21 / +0x22 / +0x28 beyond the uses listed; FUN_180113300 result bits other than 4.
- FUN_180143d20 CBands (C+0x368) contents.

## 6.5 Dynamic Contour band kernel

(Callers: FUN_1800930c0 / FUN_1800918c0 / FUN_1800914a0, §3 B.4. Line numbers are `decomp/mcce_all.c`. "sh" = CSh,
entity vector at +0x18/+0x20 (0x88-byte `pock_chn`), +0x50 dir (1/−1, 0 = open), +0x54 bit0 island.)

### 6.5.1 Two-stage offset — FUN_1800d2cc0(d1, d2, sh, out, mode) (113670-113819)
```
out = []                                                                        // 113717-113721
S1 = MCCECalcContourOffset(1, d1, {sh}, mode | 0x100, 0.0, -1.0, 1e-6, 1e-6)   // 113744-113746
for each shape s of S1 (in order):                                              // 113748-113783
    if s.dir(+0x50) == 0:                                                       // open result
        S2 = MCCECalcContourOffset(1, d2, {s}, 0x100, 0.0, -1.0, 1e-6, 1e-6)    // 113764-113767
    else:                                                                       // closed result
        e = ((d1 > 0 && s.dir == 1) || (d1 < 0 && s.dir == -1)) ? +|d2| : -|d2| // 113770-113774 (sign bit of |d2|)
        S2 = MCCECalcOffset(1, e, {s}, 0x100, 0.0, 1e-6, 1e-6)                  // 113775-113777
    out += S2 (AppendByMove)                                                    // 113779
DetNestLvlShAr(out)                                                             // 113785
```
Each S1 shape is offset on its own (the temporary list is cleared and refilled with the one shape, 113750-113762).

### 6.5.2 Closed band along open chains — FUN_1800d3210(d1, d2, shapes, out, mode) (113822-113889)
```
out = []                                                                        // 113845-113849
for each sh in shapes:                                                          // 113853-113877
    T = FUN_1800d2cc0(d1, d2, sh, mode)                                         // 113856-113857
    for each t in T with t.dir == 0:            // ONLY open results; closed T shapes are dropped   // 113864
        FUN_1800d1a60(d1 + d2, mode, sh, t, out)                                // 113865-113867
```
**FUN_1800d1a60(d, mode, sh, off, out) (113109-113181)** closes the region between the chain `sh` and its offset `off`:
```
(Rs, Re) = FUN_1800d2530(d, off)          // end records {pt, vec} of `off`              113159
posS = FUN_1800d28e0(d, sh, Rs, start=1)  // matching position on sh                       113161
posE = FUN_1800d28e0(d, sh, Re, start=0)                                                    113162
FUN_1800d13f0(d, sh, off, posS, posE, tmp)                                                  113163
if (mode & 0x200):  FUN_1800d1ca0(d, sh, posS, Rs.pt, 1, tmp); FUN_1800d1ca0(d, sh, posE, Re.pt, 0, tmp)   // 113165-113170
out += tmp                                                                                  113171
```
- **FUN_1800d2530 (113441-113556)**: Rs.pt = start point of off's first entity ('L': e1; 'A': c + r(cos sa, sin sa)),
  Re.pt = end point of off's last entity ('L': e2; 'A' at sa+sw; other: +0x18/+0x20). Tangents: start tangent of entity 0
  at t=0 (FUN_1801204a0, tol 1e-12, 113514) and end tangent of the last entity at t=1 (FUN_18011ffd0, 113515-113516).
  Each tangent (tx,ty) is turned into vec = d ≥ 0 ? (ty, −tx) : (−ty, tx) (113517-113540) and scaled to length |d| when
  its length is ≥ 1e-12 (113541-113554).
- **FUN_1800d28e0(d, sh, R, start, out PTA) (113563-113667)**. PTA = {int shape +0, int ent +4, double t +8, CPt2 pt +0x10}.
  Default: start → (ent 0, t 0, start point of sh); end → (ent n−1, t 1, end point of sh) (113592-113643).
  If | |pt − R.pt| − |d| | ≥ 1e-5 (113650): Q = R.pt + R.vec, out = nearest point of sh to Q (FUN_18013d200, 113651-113653);
  then if |out.pt − Q| > 1e-5 **and** | |out.pt − R.pt| − |d| | > 1e-5: out = nearest point of sh to R.pt (113660-113664).
- **FUN_1800d13f0(d, sh, off, posS, posE, out) (112886-113106)**: out = [N], N a new CSh:
  1. section of sh from (posS.ent, posS.t) to (posE.ent, posE.t), FUN_180146e20 flag 0 (113012-113013);
  2. 'L' posE.pt → end point of off's last entity (113022-113052);
  3. FUN_18011de10(off, N) (113054) = append off **reversed**: entities in reverse order, 'L' endpoints swapped, 'A'
     sa := sa + sw, sw := −sw (153874-154122; swap 153942-153952 / 154100-154111);
  4. 'L' start point of off's first entity → posS.pt (113064-113084);
  5. UpdMinMaxSh; N.dir = (d > 0) ? 1 : −1 (113086-113091).
- **FUN_1800d1ca0(d, sh, pos, Q, isStart, out) (113184-113438)**: round end cap (only when mode & 0x200).
  ```
  A = pos.pt; T = isStart ? start tangent of entity 0 (FUN_1801204a0) : end tangent of entity pos.ent at t=1 (FUN_18011ffd0)  // 113261-113268, tol 1e-12
  n = d > 0 ? (−T.y, T.x) : (T.y, −T.x)                                       // 113271-113278
  M = (A+Q)/2;  v = Q − A;  w = ((d>0) == isStart) ? (v.y, −v.x) : (−v.y, v.x)   // 113279-113292
  if |w| < 1e-12: return;  w /= |w|                                           // 113293-113298
  C = CalcLnLnIntersectPt(line A + s·n, line M + u·w); require ok and s ≥ 0   // 113299-113306
  ρ = |C − A|                                                                 // 113307-113309
  require |Q − C| > 1e-6 (x or y) ; θQ = atan2(Q − C)                          // 113312-113313
  require |A − C| > 1e-6 (x or y) ; sw = wrap(atan2(A − C) − θQ) to [−π, π]    // 113316-113326
  if (d>0) == isStart: if sw < 0: sw += 2π   else: if sw > 0: sw −= 2π         // 113327-113334
  e = (ρ < 0.001) ? 0.95·ρ : 0.001;  P2 = M + e·w                              // 113335-113344
  cap = CSh{ 'A'(C, ρ, sa=θQ, sw), 'L'(A → P2), 'L'(P2 → Q) }, dir = ((d>0)==isStart) ? 1 : −1   // 113371-113406
  out = SoPockUnionAr(1e-6, out ∪ {cap})                                       // 113409-113424
  ```

### 6.5.3 One-sided offset of an open chain — FUN_1800d0da0(d, shapes, out, mode, a, tol1, tol2) (112667-112854)
```
out = []
for each sh in shapes:                                                          // 112743-112818
    r = copy(sh); Retrace(r)                    // FUN_18011d800: there-and-back closed loop   112753-112779
    ring = MCCECalcOffset(1, |d|, {r}, mode, a, tol1, tol2)                     // 112780-112782
    if (mode & 0x10000):  out += ring                                           // 112783, 112812-112813
    else if capS = FUN_1800d0480(d, 1, sh, ring) and capE = FUN_1800d0480(d, 0, sh, ring)
            and FUN_1800cf5b0(d, sh, ring, mode, capS, capE, → band):   out += band          // 112784-112808
    else: nothing is added for this sh
```
- **FUN_1800d0480(d, isStart, sh, ring, out PTA) (112333-112591)**:
  ```
  k = isStart ? 0 : n−1 ; tk = isStart ? 0 : 1                                 // 112392-112399
  P = point of entity k at tk ('L' lerp, 'A' c + r(cos, sin)(sa + tk·sw), 'P' pt)   // 112424-112451
  T = isStart ? FUN_1801204a0(sh, k, tk) : FUN_18011ffd0(sh, k, tk), tol 1.0000000000000002e-10   // 112452-112459
  nrm = d > 0 ? (−T.y, T.x) : (T.y, −T.x) ; fail if |nrm.x| ≤ 1e-12 and |nrm.y| ≤ 1e-12   // 112462-112481
  φ = atan2(nrm); fail if ring is empty                                       // 112482-112483
  (near, cand) = FUN_18013d7d0(P, ring.last, 1e-6)                            // 112489-112493
  cand += near                                                                // 112494-112531
  for each c in cand: c.shape = n(ring) − 1                                   // 112539-112540
      fail if |c.pt.x − P.x| ≤ 1e-12 and |c.pt.y − P.y| ≤ 1e-12               // 112543
      δ = wrap(atan2(c.pt − P) − φ) to [−π, π]                                // 112544-112553
      keep c with the smallest |δ| (strict <; first wins on ties)             // 112554-112560
  out = kept c (0x20-byte PTA)                                                // 112568-112579
  ```
  **FUN_18013d7d0(P, shp, tol, &near, list) (170728-170865)**: near = nearest point of shp to P (FUN_18013d200 →
  {+4 ent, +8 t, +0x10 pt}); D = (|near.pt − P| + tol)²; list = every entity start point (t = 0) and end point (t = 1)
  whose squared distance to P is < D, in entity order (start then end per entity; 170776-170861).
- **FUN_1800cf5b0(d, sh, ring, mode, capS, capE, out) (111917-112330)**, s = (d > 0) ? 1 : −1 (111992-111995):
  ```
  S = start point of sh; E = end point of sh                                   // 112040-112079
  B = copy(sh) + 'L'(E → capE.pt) + section(ring.last, capE(ent,t) → capS(ent,t), flag 0) + 'L'(capS.pt → S)   // 112108-112150
  out = {B}; UpdMinMaxShAr(out)
  ok = n(ReconfigSelfIsectSh(0x820, |d|/2, out)) == n(out) and (UpdDirShAr(0.001, out), B.dir == s)   // 112154-112158
  if !ok:                                     // other way round the ring            112159-112214
      B = copy(sh) + 'L'(E → capE.pt) + RevSh(section(ring.last, capS → capE)) (FUN_180146b10 append) + 'L'(capS.pt → S)
      same test; on failure return 0
  if (mode & 0x200):  FUN_1800d1ca0(d, sh, posEnd = {ent n−1, t 1}, capE.pt, 0, out);
                      FUN_1800d1ca0(d, sh, posStart = {ent 0, t 0}, capS.pt, 1, out)      // 112216-112222
  for i in 0 .. n(ring)−2 (every ring shape except the last):                  // 112225-112251
      if (FUN_180113300(ring[i], out, 1e-5) & 4) == 0: move ring[i] out of ring into list K
  if K non-empty: out = K followed by B                                        // 112252-112271
  return 1
  ```
  (In the decompile the ReconfigSelfIsectSh output list is compared with `out` by size only, 112155-112156 / 112208-112209.)

### 6.5.4 Bisection — FUN_1800aeae0(pred, ctx, tol, a, b, &x) (94740-94784)
```
if tol < 1e-7: tol = | |b| − |a| | · 0.125                                     // 94752-94754
lo = min(a, b); hi = max(a, b)                                                 // 94755-94759
if pred(ctx, hi): x = hi; return 1                                             // 94760, 94781-94783
if !pred(ctx, lo): x = lo; return 0                                            // 94762-94765
while hi − lo > tol: m = (lo + hi)/2; if pred(ctx, m) lo = m else hi = m        // 94767-94777
x = lo; return 1                                                               // 94778
```
It returns the largest value in [lo, hi] for which `pred` holds, to within tol, assuming pred is true below some
threshold (it never tests other intervals).

**The predicate LAB_18000146f.** 0x18000146f is an incremental-link jump thunk. The decompile has no body there (the
nearest named export is CRasDev::DrawToolPath @ 0x180001456, l.1180), so the jump target cannot be read from the text.
Candidate: **FUN_180092180 (80840-80880)**. Evidence: (a) no call to FUN_180092180 exists in mcce_all.c (2 hits, both
its own header lines); (b) its layout matches the ctx that FUN_1800918c0 passes, `&local_128`, which is a double c
(±1.0, 80592-80598) immediately followed by the CVe `local_120` = {sh}; (c) the code that runs after the root search
(80629-80630) makes the same call. Body, if it is the target:
```
pred(ctx, x) = ctx != 0 && ctx.list non-empty && | |ctx.c| − 1 | ≤ 1e-7
               && n( FUN_1800d0da0(ctx.c · x, ctx.list, 0x300, 0.0, 1e-6, 1e-6) ) > 0        // 80858-80866
```
**Jump target NOT VERIFIED (needs disassembly at 0x18000146f).**

### 6.5.5 Open-chain contour piece FUN_1800918c0 (80533-80676), restated with the kernels above
```
c = (flags & 0x400000) ? +1 : −1                                               // 80592-80598
X = FUN_1800d3210(t·c, −u·c, {sh}, 0x100)      // band between sh and its offset at (t−u)·c = Parm+0xe8·c, no round caps (no 0x200)   80619-80622
P = FUN_180069120(Der); Q = FUN_180069190(Der)                                 // 80623-80626 (formulas §3 B.4)
if FUN_1800aeae0(pred, {c, {sh}}, 0.1·(P − Q), Q, P, &k):                      // 80627
    Y = FUN_1800d0da0(c·k, {sh}, 0x300, 0.0, 1e-6, 1e-6)                       // 80629-80630: one-sided band, round caps (0x200)
Z = copy(Y) ∪ X                                                                // 80632-80634
REG  += FUN_18015be30(1e-6, Z)   (= intersection of Z's nest groups, 6.5.6)   // 80635-80636
WALL += Y                                                                      // 80637
```
So the band width k is the largest value in [Q, P] (to 0.1·(P − Q)) for which the one-sided offset still yields a shape.
If the search fails, Y is empty, and FUN_18015be30 then returns nothing unless X alone holds ≥ 2 nest groups
(6.5.6: fewer than 2 groups → empty output).

### 6.5.6 FUN_18015be30(tol, X, out) (187429-187616) = body of export `MCCECalcPockIntersect` (11558ff, call 11594)
Nest-group iterator **FUN_18010cf20(list, &i, grp) (145168-145194)**: it appends the shapes from index i while they are
islands (+0x54 bit0), then the first non-island, and advances i. It returns false when no non-island is left.
FUN_18011f1f0 (154627-154651) is the same grouping by index, returning the group size.
```
i = 0
G0 = next_group(X, i); G1 = next_group(X, i); if either is missing: return     // 187508-187510
R = FUN_18015b2e0(tol, G0, G1); if R is empty: return                          // 187511-187513
while (G = next_group(X, i)):                                                  // 187519-187567
    R' = []; j = 0
    while (H = next_group(R, j)): R' += FUN_18015b2e0(tol, G, H)               // 187528-187540
    if R' is empty: return (out unchanged)                                     // 187541
    R = R'
out += R                                                                       // 187568
```
**FUN_18015b2e0(tol, G, H, out) (187022-187080)**, pairwise. The "outer" of a group is its last shape; bbox at +0x30..+0x48.
- It runs only if both groups are non-empty and the outers' bboxes overlap with slack 1.1920928955078125e-07 (FLT_EPSILON),
  exactly: G.minx − ε ≤ H.maxx, H.minx ≤ G.maxx + ε, G.miny − ε ≤ H.maxy, H.miny ≤ G.maxy + ε (min at +0x30/+0x38,
  max at +0x40/+0x48; 187055-187061).
- It also needs `FUN_18015c370(tol, G, H) == 0` (187062).
- If so: FUN_18015aac0 → FUN_180159c60 → FUN_18015a4f0 → FUN_180159230 → DetNestLvlShAr → out (187063-187068).
- Otherwise it adds nothing.

**FUN_18015c370 (187619-187681)**, with FUN_180163960(tol, a, b) a pair classifier:
- returns 1 if the classifier on the two outers gives 3;
- or if it gives 2 for any island of G against H's outer;
- or if it gives 4 for G's outer against any island of H;
- otherwise 0.
The classifier codes are NOT DECODED.
FUN_18015b2e0 is also called from FUN_180153390 (182455, 182561), FUN_180066320 (58607), FUN_180074d50 (66117) and
FUN_18007c4e0 (70026).

### 6.5 NOT DECODED
- Jump target of thunk 0x18000146f (candidate FUN_180092180; needs disassembly).
- `MCCECalcContourOffset` / `MCCECalcOffset` internals and mode bits (0x100, 0x125, 0x121, 0x300, 0x10000 test in
  FUN_1800d0da0, 0x40127).
- `ReconfigSelfIsectSh` (flag 0x820), `UpdDirShAr`, `SoPockUnionAr`, `DetNestLvlShAr`, `CalcLnLnIntersectPt` internals.
- FUN_180146e20 / FUN_180146b10 (section copy / append), FUN_18013d200 (nearest point),
  FUN_1801204a0 / FUN_18011ffd0 (tangent at (ent, t)).
- FUN_180113300 (containment classifier; bit 4 meaning).
- FUN_180163960 classifier codes 2/3/4; the intersection kernel FUN_18015aac0, FUN_180159c60, FUN_18015a4f0,
  FUN_180159230.

## 6.6 PeelMotMat output and FUN_1800942b0

### 6.6.1 Output wiring (re-checked, FUN_1800e28c0 122837-122966; caller FUN_1800e2650 122805-122833)
- param_2 / param_3 (the two output CVe<CSh*>) are touched only at 122888-122897 (clear). Neither pointer is passed to
  any callee:
  - FUN_1800e2c40 gets (param_1, local_78);
  - FUN_1800942b0 gets (local_78, local_c8);
  - FUN_1800e34e0 gets (local_78, local_c8, local_a0);
  - FUN_1800e3370 gets (param_1, local_c8, local_a0).
- FUN_1800e3370 only **reads** param_1 (+0x58/+0x60, 123243-123245).
- A fourth local CVe<CSh*> `local_50` is constructed (122883-122887) and destroyed (122921-122932) with no use in between.
- The caller converts the (empty) lists only when the result is 1 (122808-122811). It always returns 0 (122833).
- Conclusion **as decompiled**: PeelMotMat outputs are always empty. Whether code that filled `local_50`/the outputs was
  removed in the build or lost in decompilation is **NOT DECODED** (needs disassembly of 0x1800e28c0).

### 6.6.2 FUN_1800942b0(tolPt, tolArc, shapes, out) (81801-81977) = **convex hull (QuickHull) of all shape vertices**
(This corrects the earlier unverified "joins the two offset curves end-to-end" guess in §1.)
```
V = FUN_180095100(tolPt, tolArc, shapes)       // vertex list, 81839
L = FUN_180094840(V)                           // initial 2 hull edges with point sets, 81840
H = []                                         // output vertex pointers
while L non-empty:                             // 81843-81976
    e = last node of L
    if e.set non-empty: FUN_180095ae0(L); continue          // split, 81875-81877
    if H empty: H += e.a                                    // 81881-81914
    H += e.b; remove e from L                               // 81915-81975
FUN_180094d30(H, out)                                       // 81846
```
Node layout (list node): +0x18 = a (PTA*), +0x20 = b (PTA*), +0x28/+0x30/+0x38 = CVe<PTA*> point set.
PTA (0x20 bytes) = {int shape, int ent, double t, CPt2 pt (+0x10/+0x18)}.

**FUN_180095100 (82265-82629): vertex list.** For each shape s (index i), closed = FUN_180123770(s, tolPt). FUN_180123770
(156629-156721) returns 0 for an empty shape. Otherwise it returns 1 iff |start of first entity − end of last entity|²
≤ tolPt². Then, for each entity k:
- **Start point.** If the shape is open and k == 0, push (i, k, 0.0, start). Otherwise k == 0 of a closed shape is
  skipped, and for k > 0 the start is pushed only when the list is empty or |last pushed pt − start|² > tolPt²
  (82375-82422).
- **End point.**
  - 'L': push (i, k, 1.0, e2) (82426-82507).
  - 'A': `ApxArcWithLnSegs(tolArc, arc)` gives m points (an exception is thrown on error, 82514-82523). For j = 1..m,
    push (i, k, j/m, p_j) (82525-82608).
  - Other types: no end point.

**FUN_180094840 (81980-82154): initial split.** The list is cleared (82002-82018) and nothing is done unless n(V) > 1.
- Find xmin, xmax, ymin and ymax over V. The first index wins on ties (strict comparisons, 82030-82057).
- Use the x pair (xmin → xmax) unless the x extent ≤ the y extent, in which case use (ymin → ymax) (82060-82066).
- Create 2 nodes (FUN_180096ee0): N1 = (a = min, b = max) and N2 = (max, min) (82068-82081).
- For every other point p, cross = (p.y − a.y)(b.x − a.x) − (b.y − a.y)(p.x − a.x), with a, b from N1.
  - cross ≥ 0 → N1.set;
  - cross < 0 → N2.set (82082-82150).

**FUN_180095ae0 (82632-82784): QuickHull step on the last node e = (a, b).**
1. For each p in e.set, project p onto the line a + s·(b − a) (CalcLnLnIntersectPt with the normal (−dy, dx),
   82693-82707). Keep the p with the largest squared distance to its foot point (strict >, initial −1; 82708-82713) as F.
2. Append a node (FUN_180096ee0). The new last node gets (a, F) and the old node becomes (F, b) (82719-82731).
3. Each old set point except F goes:
   - to the new node's set if cross(a→F, p) ≥ 0;
   - to the old node's set if cross(F→b, p) ≥ 0 (both tests run, 82750-82776);
   - otherwise it is dropped (interior).

Each node's set holds points with cross ≥ 0, i.e. on the left of its edge (cross = z of (b−a)×(p−a)). The hull is
emitted from the last node backwards. The first emitted point is N2.a = the max extreme point. Orientation follows from
this, but no direction value is computed. The shape's dir is set to −1 explicitly (82252).

**FUN_180094d30 (82157-82262): shape out.** One CSh (0x98 bytes). For i = 1..n−1 it adds 'L'(H[i−1].pt → H[i].pt) and
calls FUN_18013bf10 (169611-169687: SetChnMinMax of the new entity, shape bbox update) (82218-82246). If at least one
entity exists: dir (+0x50) = −1, appended to out (82248-82255). So the output is always ≤ 1 shape, and the caller's
"exactly one shape" test (122908) only rejects an empty hull.

Consequence for §1 FindGaps (FUN_1800e34e0): the "loop" is the convex hull of the two side curves' vertices (arcs
linearised at tol 1e-8). Hull edges that do not lie on a side curve (distance > 1e-6 from the midpoint, 123444) and
connect the two curves are the "gaps".

### 6.6 NOT DECODED
- Whether PeelMotMat's outputs are really unwired (disassembly of 0x1800e28c0; unused `local_50`).
- `ApxArcWithLnSegs` internals (whether the arc start point is among the m points, and the segment count rule).
- FUN_180096ee0 / FUN_180096d80 / FUN_1800961f0 / FUN_1800963f0: list/vector growth helpers (only their use is decoded).
- `CalcLnLnIntersectPt` internals (the decompile shows only 3 arguments at 82707).

## 6.7 Entry internals (medial, trochoidal, custom)

Line numbers are `decomp/mcce_all.c`. `C+x` = CCePock offset, `P+x` = CDynPockParm offset, **C+x = P+(x−8)**.
Used here: C+0x30 = R, C+0x38 = r, C+0x48 = P+0x40 (min path radius), C+0x50 = P+0x48 (micro lift),
C+0x88 = P+0x80 (cut depth), C+0x98 = P+0x90 (helix radius), C+0xa0 = P+0x98 (op 0x16c8, "trochoid radius"),
C+0xa8 = P+0xa0 (additional slot width), C+0x118 = P+0x110 (chain tolerance), C+0x225 = P+0x21d (registry
`bIsAssertOn`), C+0x248 = P+0x240 (`dAirDwellPos`), C+0x340 = current tool-centre region, C+0x4c0 = begin of the
S3 list RG+0x218 (CCePock+0x298+0x218+0x10).
Entity start/end points below use the usual rule ('L' e1/e2; 'A' c + r(cos, sin) of sa or sa+sw; 'P' pt; else (0,0)).

### 6.7.1 Medial-axis machinery shared by the medial entries
`MAE` record = 0x118 bytes (vector stride, FUN_1800c4870 l.106263). Fields seen:
`+0x00` int type (0, 1, 2 — FUN_1800c5690 l.106747-106814); `+0xd8/+0xdc` int shape/entity of boundary side 1,
`+0xf0/+0xf4` shape/entity of side 2 (read as `param_2[0x36]/[0x37]`, `[0x3c]/[0x3d]`, l.106750-106759);
`+0x108` int parent index (−1 = root), `+0x10c` / `+0x110` int child indices (−1 = none) (FUN_1800c2c20
l.105485-105492, FUN_1800c47c0 l.106207-106215, FUN_1800c4830 l.106229); `+0x114` flags, **bit 2 (0x2) = pruned**
(set l.106283, cleared l.105826), bit 4 tested together with 2 (`& 6`, l.106268; meaning NOT DECODED).
`mcce::CalcMedAx(bool, region, → shapes, → MAE list, 0.001, 8.0, prog)` is an export; its internals are not decoded.

**Prune — FUN_1800c4870 (106239-106296)**
```
repeat:
  changed = false
  for i over MAE:
    if (mae[i].flags & 6) != 0: continue                                           // 106268
    endLeaf  = FUN_1800c47c0(i): both children are -1 or pruned                    // 106207-106217
    rootLeaf = FUN_1800c4830(i): parent is -1 or pruned                            // 106229-106235
    if (endLeaf || rootLeaf) && mae[i].+0xd8 == mae[i].+0xf0:                      // 106273-106274 (same boundary shape both sides)
       if !FUN_1800c4170(i, endLeaf, rootLeaf):                                    // 106275 (true = the free end point coincides,
                                                                                   //   |d|^2 < 1e-12, with an end of another live edge, 106058-106063)
          a = FUN_1800c5690(i)                                                     // 106277
          if a <= 2.8623399732707004: mae[i].flags |= 2; changed = true            // 106280-106284 (2.86234 rad = 164.0 deg)
until !changed                                                                     // 106292
FUN_1800c3a90: if every edge is pruned: idx = max-clearance edge (FUN_1800c5df0); FUN_1800c64d0(edge, ρ, ρ); un-prune it   // 105804-105828
FUN_1800c4140 = FUN_1800c43e0 + FUN_1800c3c10                                      // 105995-105996 (NOT DECODED)
```
FUN_1800c5690 (106701-106889), the angle `a`: type 0 — directions of the two boundary *line* entities
(e2−e1 of side-1 and side-2 entity), `a = |wrap(atan2(d2) − atan2(d1))|` (106750-106783); type 1 — built from the
doubles at +0x18..+0x40 of the record (106786-106811); type 2 — via `CalcLnLnIntersectPt` on the record's own line
(+0x08..+0x20) and the side-1/side-2 points (106815-106857); types 1/2 return `π − |wrap(Δ)|` (106879). The geometric
meaning of record fields +0x08..+0x40 is NOT DECODED.

**Chains — FUN_1800c1530(sh, MAE, 0.001, 0.0001, → out, prog) (104680-104836)**: for every MAE with parent
`+0x108 == −1` (a root) a new CSh is pushed and filled by the recursive walk FUN_1800c2c20 (105479-105495):
`emit(pre=0, i); for child in (+0x10c, +0x110) if != −1: walk(child); emit(post=1, i)` — emit = FUN_1800c2270
(345 lines, NOT DECODED). Empty shapes are removed; non-empty ones get z (+0x60) from the last shape of `sh`,
UpdMinMaxSh, and **dir +0x50 = 1** (104782-104792). Then FUN_1800c1c90 (NOT DECODED) and a second removal of
empty shapes (104801-104834).

**FUN_180110840 (146986-147109) keep-largest-group**: walks the list in nest groups (islands…, outer); the group
value is FUN_18010f210 = area(outer) − Σ area(islands before it) (area FUN_180126ac0 tol 1e-4, 146238-146256); keeps
only the group with the largest value (strict `>`, start −1.0) and erases the shapes before and after it
(147076-147098).

**FUN_1800c1a40 (104841-104906)**, per chain shape: among entities whose byte `+0x13` has bit 2, take the one with
the smallest FUN_1800c1220 value (start FLT_MAX 0x47efffffe0000000) → index k and an int `j` from the same call;
`FUN_18014fd50(k, sh)`; erase entities from index j to the end (FUN_180027160); `FUN_1800c2d30(sh)`. FUN_1800c1220,
FUN_18014fd50, FUN_1800c2d30 are NOT DECODED, so the role is unverified.

### 6.7.2 Medial path for the 0x200 / 0x400 entries — FUN_180079260 (68420-68516)
```
d = (flags & 0x400) ? P+0x98 + 0.002 : 0.002                         // 68464-68470 (0x3f60624dd2f1a9fc = 0.002)
B = MCCECalcOffset(1, -d, C+0x340, 0x100, 0, 1e-6, 1e-6)              // 68471-68473
if B nonempty:
   CalcMedAx(P+0x21d, B, → S, → MAE, 0.001, 8.0, prog)                // 68475-68477
   prune FUN_1800c4870(S, MAE)                                        // 68478
   out = chains FUN_1800c1530(S, MAE, 0.001, 1e-4)                   // 68479
   FUN_1800c1a40(out)                                                 // 68481
```
### 6.7.3 Medial ramp path (0x1000) — FUN_18007b6e0 (69539-69715), called from FUN_18007b3c0
```
d = 0.5·P+0xa0 + 0.002                                               // 69611
B = MCCECalcOffset(1, -d, C+0x340, 0x100, 0, 1e-6, 1e-6)              // 69612-69614
if B nonempty:
   FUN_180110840(B)                         // keep the largest nest group (6.7.1)   69616
   CalcMedAx(P+0x21d, B, → S, → MAE, 0.001, 8.0)                       // 69617-69619
   (idx, pt, ρ) = FUN_1800c5df0(S, MAE)     // max clearance                         69620
   X = FUN_18007c4e0(C, S, MAE, ρ + d)      // UNPRUNED chains clipped, below        69622
   LX = X nonempty ? len(X[0]) : -1                                                   // 69624-69634
   prune FUN_1800c4870(S, MAE); Y = chains FUN_1800c1530(S, MAE, 0.001, 1e-4); keep longest Y (FUN_180123a30)  // 69635-69638
   LY = Y nonempty ? len(Y[0]) : -1                                                   // 69639-69643
   if LX >= 0 or LY >= 0: out = (LX <= LY) ? Y[0] : X[0]                            // 69644-69656 (tie → pruned Y)
```
FUN_18007c4e0 (69991-70050): `Ch = chains(S, MAE, 0.001, 1e-4)` (unpruned); `L = FUN_18007ca70(C, ρ+d)` =
`MCCECalcOffset(1, −max(0, (ρ+d) − 0.75(R−r)), C+0x340, mode 1)` (70154-70159, same routine as Profile); if L
nonempty: `FUN_180110840(L)`, `X = FUN_18015b2e0(0.001, Ch, L)`, keep the longest (FUN_180123a30) (70022-70027).
FUN_18015b2e0 (187024-187080) is the pairwise boolean primitive also called by FUN_180153390 (the core of
FUN_180153b30) at l.182455/182561; it does nothing unless the bboxes of the last shapes of both lists overlap
(tol 1.1920928955078125e-07, 187055-187061) and FUN_18015c370 is false; then FUN_18015aac0 → FUN_180159c60 →
FUN_18015a4f0 → FUN_180159230 → DetNestLvlShAr. It is the pairwise step of the export `MCCECalcPockIntersect` (§6.5.6); the set
operation of its kernel is **NOT DECODED**.

### 6.7.4 Helix + medial (0x200) — FUN_180078130 (67870-67982), join pieces
```
M = FUN_180079260(C)                                                   // 67921
Cc = M nonempty ? start point of M[0].ent[0] : max-clearance point (FUN_180079050)   // 67922-67940
H = helix FUN_1800788d0(C, Cc) → param_2                               // 67941
if H nonempty and M[0] has entities:
   param_3 += copy(M[0]); Retrace(param_3[0])   // FUN_18011f2e0 (push copy, 154656-154717) + FUN_18011d800   67943-67949
   FUN_180079570(C, M[0], H[0])                                        // 67956
   FUN_180146b10(M[0], H[0])  // insert all entities of M[0] at the end of H[0]; bbox via GetMinMaxViaShEnt  (175610-175635)
```
**FUN_180079570 (68523-68758)** — link from the end of the helix (last entity must be 'A', else nothing, 68561):
let the last helix arc be (c, ρ, sa, sw), θe = sa + sw (68562); t = unit tangent of M at entity 0, t = 0
(FUN_1801204a0 tol 1e-6, 68563); `(x', y') = (flags&1) ? (t.x, −t.y) : (−t.x, t.y)` (68564-68575, sign flips on the
high dwords); φ = atan2(y', x') (68584).
- If |x'| or |y'| > 1e-12 (68576-68581):
  `δ = wrap_[−π,π](φ − θe)`; if climb (flags&1) and δ < 0: δ += 2π; if conventional and δ > 0: δ −= 2π (68585-68602).
  Append arc A1 = {centre c, radius ρ, sa = θe, sw = δ} (68603-68626).
  Append arc A2 = {centre = (end(A1) + c)/2, radius = 0.5·ρ, sa = φ, sw = climb ? +π : −π} (68627-68679).
  So A2 is a half circle from the helix rim to the helix centre.
- Else (degenerate tangent, 68682-68753): append one entity whose type byte is written as 0x41 ('A') but whose
  fields +0x18/+0x20 = end of the last helix entity and +0x28/+0x30 = start of M[0].ent[0] (line-shaped data in an
  'A' record, as decompiled — **verify in disassembly**).

### 6.7.5 Helix + trochoidal medial (0x400) — FUN_180078500 (67987-68083)
```
M = FUN_180079260(C)          // offset by P+0x98 + 0.002 because flag 0x400 is set (6.7.2)       68029
Cc = start of M[0].ent[0], else max-clearance point FUN_180079050                                 68030-68052
H = helix FUN_1800788d0(C, Cc) → param_2                                                         68054
if H nonempty and M[0] has entities: FUN_18007a150(C, M, H); FUN_180081ab0(C, M, H)               68055-68061
```
**FUN_18007a150 (68860-69345): a recursive Dynamic Mill run at the medial start.**
```
e = P+0x90 − (R − r)                                                   // 68997
if |e| < 1e-4: FUN_180079cf0(C, M, H)                                  // 68998-69002
S = start of M[0].ent[0]                                               // 69003-69016
enter = (P+0x90 < P+0x98) or (e > 0 and |e| >= 1e-4)                   // 69021-69023
ρ     = (P+0x90 < P+0x98) ? P+0x98 : P+0x90
if enter:
   Motion   = [ circle 'A' centre S, radius ρ, sa 0, sw 2π, dir 1 ]                             // 69024-69071
   Material = []
   if e > 0 and |e| >= 1e-4: Material += circle centre S, radius P+0x98, sw 2π, dir 1, +0x54 = 1 (island)   // 69075-69121
   Material += circle centre S, radius P+0x98 (+1e-4 if it is not the first shape), sw 2π, dir 1     // 69122-69174
   P' = copy of P (C+8)                                                                            // 69175
   P'.flags = (flags & 0xeff0ffff) | 0x22000000   // clear MOAT_FIRST + retract bits 0xf0000; set RECURSION, BND_TOOL_CEN   69176
   P'+0x60 = P'+0x98 = P'+0x50 = P'+0x90 = P'+0xa0 = P'+0x68 = P'+0x70 = 0                          // 69177-69183
   P'+0x118 = copy of the last entity of H[0] (pock_chn) with its +0x78 := P+0x80 (C+0x88)          // 69184-69204
   P'+0x1a0 = callback LAB_180001690, P'+0x1a8 = C                                                 // 69205, 69211
   if 0.75·ρ < P'+0x40: P'+0x40 = max(0, ((R − r) + ρ)·0.75 − (R − r))                              // 69206-69210
   MCCECalcDynPock(P', Motion, Material, [], → chn, → bnds, tempfile, 0)                            // 69212-69216
   read header (FUN_18008d230) and records (FUN_18008d020) of the temp file → shape list Q; delete the file  // 69217-69227
   if Q nonempty: n = index of the last entity of Q[0] whose CeFlags byte (+0x12) & 5 == 0 (scan back)   // 69228-69240
       append Q[0].ent[0..n] to H[0] with bytes +0x12, +0x13, +0x14 := 0 and z (+0x78) := 0; UpdMinMaxSh   // 69241-69262
```
(Material with an island circle of radius P+0x98 inside an outer circle of radius P+0x98 + 1e-4 is what the code
builds when e > 0; it is reported as decompiled.)

**FUN_180079cf0 (68763-68853)**: S = start of M[0].ent[0], E = end of the last entity of H[0], Mid = (S+E)/2; if
|E − Mid| > 1e-12: append full circle 'A' {centre Mid, radius 0.5·P+0x90, sa = atan2(E − Mid), sw = climb ? +2π : −2π}.

**FUN_180081ab0 (72562-72658): trochoidal walk along the medial path** (structure decoded, leaf builders NOT DECODED)
```
FUN_18007f730(C, M, H)                       // join helix end to the medial path, below          72600
Δ = FUN_180083a40(C)                         // step length, below                               72601
state: seg (0x70 bytes, local_b8), pos (local_f8), prevFlag/curFlag (local_118/local_117 = 1)
while FUN_180083f30(M, Δ, seg, pos, flags):                                                      72632-72654
   if seg.prevType == 0:            FUN_18007f0d0(C, seg, H)       // first step  (217 lines)
   elif !prevFlag and !curFlag:     FUN_18007ef90(C, seg, H)       // append seg's 'L' record as-is (71229-71250)
   elif !prevFlag and  curFlag:     FUN_18007e540(C, seg, H)       // (256 lines)
   elif  prevFlag and !curFlag:     FUN_18007e3d0(C, seg, H)       // FUN_1800805d0 else FUN_18007d5a0(1); then append seg (70932-70958)
   else:                            FUN_18007e360(C, seg, H)       // FUN_18007f8c0 else FUN_18007d5a0(0); then FUN_180081190 (70912-70917)
GetMinMaxViaShEnt(H[0])                                                                           72655-72656
```
- **Step Δ — FUN_180083a40 (73486-73522)**: context {flags, 0.0, d = R−r, a = CalcLnEngAng(R−r, s_work = P+0x1f8),
  P+0x98}; Δ = Brent root (FUN_1800b0200, interval [1e-4, 2d], tol 1e-7) of callback **LAB_180006aaa (thunk; target not
  in the decompile — NOT DECODED)** (73501-73509). If r > 1e-7: same with d = R, a = C+0x40 (= P+0x38, the stepover
  angle) and Δ = min of the two roots (73510-73519). `CalcLnEngAng(d, s) = acos(clamp(1 − s/d, −1, 1))` (export
  0x18000543e, l.9012ff; −1 clamps to acos(−1)).
- **Walker — FUN_180083f30 (73611-73722)**: shifts the "next" state to "previous" (seg[0..6] = seg[7..13], pos[0..7]
  = pos[8..15], prevFlag = curFlag, 73640-73651); advances Δ along M (FUN_18006f1c0 mode 0, 73652); continues only if
  that succeeded or ≥ 1e-4 was walked (73653), else pos.ent = −1 and stop. curFlag = NOT(bit 0 of byte +0x13 of the
  medial entity reached) (73655-73656). If prevFlag && !curFlag: FUN_1800832e0 gives a position; if the section length
  to it (FUN_180149d80) > 1e-4, jump there and set curFlag = 1 (73657-73670). If !curFlag: snap to the end of the
  current entity (t = 1.0) (73672-73707). seg := 'L' from the previous point to the new point (73709-73716).
  The meaning of medial entity byte +0x13 bit 0 (written by FUN_1800c2270) is NOT DECODED.
- **Join — FUN_18007f730 (71476-71516)**: if FUN_180084210 is true nothing is added. FUN_180084210 (73729-73825) is
  true iff |S − E| − P+0x98 ≤ 1e-5 (S = medial start, E = helix end) and the angle between the helix end tangent
  (FUN_18011ffd0, tol 1e-5) and (E − S) rotated by ±90° (sign flips by flags&1, 73801-73806) is ≤ 0.08726646259971647
  (5°) (73821). Otherwise: FUN_1800834f0 (control data) and FUN_180082120 (Bézier points); if both succeed,
  AppendBezier(1e-5, P, H[0]) (FUN_1800a5b00); then if FUN_180083c10(…, n0) is false → FUN_18013c390 (bbox refresh
  of all shapes, 169847-169867) and return; if true → H[0] is resized back to its original n0 entities (Bézier
  dropped) (71505-71511). In every non-returning case FUN_18007d400 appends from the helix end to the point from
  FUN_180082e80 via FUN_18011d2d0 (70443-70466). FUN_1800834f0, FUN_180082120, FUN_180083c10, FUN_180082e80,
  FUN_18011d2d0 are NOT DECODED.
- **Where P+0x98 is used** in this tree (all cited above): offset of the medial region (68467), the recursive
  material circle radius and motion radius ρ (69017-69023, 69114-69165), the helix-to-medial join test (73791), and the
  step-root context (73501).

### 6.7.6 Custom entry chain (0x8000) — FUN_180074560 (65829-65957)
```
K = FUN_1800752c0(C)                         // pick one S3 chain, below                       65891
if K found:
   FUN_180132200(1e-6, K[0], param_3, param_4)   // chain cleanup, NOT DECODED (below)          65893
   if |K| == 1:
      L = FUN_180074940(C, K)                    // closed loop from the chain, below           65895
      if |L| == 1:
         O = MCCECalcOffset(1, P+0xa0, L, 0x105, 0, 1e-6, 1e-6)                                 65898-65900
         if |O| > 1: keep only the LAST shape of O                                               65902-65909
         if FUN_180074d50(C, O, → E):                                                            65910
            want = (flags & 1) ? +1 : -1; if E[0].dir != want: RevSh(E[0])                       65912-65918
            ramp FUN_18007bc00(C, E[0], → entry)                                                 65919
```
- **FUN_1800752c0 (66264-66377)** — for every S3 shape s (list C+0x4c0): `I = FUN_180134690(s, C+0x340)` (crossings,
  0x50-byte records, entity at +0x14, t at +0x18). For i = 0..|I| (i = |I| meaning (last entity, t = 1.0)): if the
  section of s **from its start (ent 0, t 0)** to point i is inside C+0x340 (`FUN_1801130f0(..., 1e-5) == 2`) add its
  length (FUN_180149d80) to `Lin` (66318-66350). (The section start is the constant (0, 0.0) in every call, 66336.)
  The shape with the largest `Lin` (> −1, at least one inside section) is copied out (66352-66364).
- **FUN_180074940 (65962-66053)**: S/E = start/end of K[0]. If |S − E| ≥ 10·P+0x110 or P+0xa0 ≥ 1e-7: L = copy of
  K[0] then Retrace (open chain → there-and-back loop) (66029-66033). Else: L = copy of K[0] plus a closing 'L' E → S,
  and L.dir = FUN_1801270c0(2·P+0x110, L) (66035-66050; FUN_1801270c0 NOT DECODED, an orientation value).
- **FUN_180074d50 (66058-66261)**: X = FUN_18015b2e0(0.001, O, C+0x340) (boolean primitive, 6.7.3); split X into nest
  groups (islands…, outer); value = FUN_18010f0e0(group) (area); output = copy of the **outer shape of the group with
  the largest value** (strict >, start −1.0) (66117-66235). Returns output non-empty.
- **FUN_180132200 (164692-164828)**: FUN_18012f3b0(K[0]) builds 0x38-byte records; FUN_180131740 / FUN_180131240
  rebuild; if the result is non-empty K[0] is replaced by its first shape (164789-164793). Records and rebuild
  NOT DECODED (role guess, unverified: self-intersection cleanup).

### 6.7.7 Smoothing section — FUN_180048320 (42942-43094), used by FUN_18004ae80 (44620)
```
I = FUN_180134690(sh, S)          // crossings of the loop with the containment set S (0x50-byte records)   42971
if |I| == 0 or |I| == 2: return true                                                        42972-42975, 43087
if |I| == 1:                                                                                42976
   I = [ {pt = start(sh), ent 0, t 0, +0x20/+0x24 = -1}, I0 ]                                43017-43025
   if Inside(S, 1e-5, start(sh)) == 2: return true                                          43026-43028 (FUN_180112e80)
   I = [ I0, {pt = end(sh), ent n-1, t 1.0, +0x70/+0x74 = -1} ]                              43031-43080
   return Inside(S, 1e-5, end(sh)) == 2                                                     43082-43083
return false   (|I| > 2)                                                                     43091
```
Use in FUN_18004ae80 (44620-44690): if false, no smoothing. If |I| == 0 the whole loop is smoothed; else only the
section I[0] → I[1] (FUN_180146e20 with (+0x14,+0x18) → (+0x64,+0x68)) is smoothed, and the result is
`section(start → I[0]) + smoothed + section(I[1] → end)` (44670-44684). Candidate distances: d0, then
`k·d0·0.1` for k = 9..2 — **0.1·d0 is never tried**: the loop is `try d; d = k·d0·0.1; k--; while k > 0` with k
starting at 9 (44631-44692). (Correction to §3 D 1.6, which lists 0.1·d0.) Acceptance: `FUN_180113300(smoothed,
S, 1e-5) & 4 == 0` (44654-44659). ChaikinSmoothSh gets callback LAB_180002c6b = `jmp FUN_180048f30` (43371-43378): a progress
update (FUN_180114140) that returns 1 - no geometry. ChaikinSmoothSh (1872-1928) itself: one entity → FUN_18011f2e0; else
FUN_18014c250 (178651ff) → FUN_18014d5c0 (179292ff) → FUN_18014c820(…, 0.001, 6, 3.0) (178856ff) → FUN_18014f740(last shape,
1e-5, callback) (180359ff); decoded and ported 2026-11-07, §6.7.10.
**Composition (44632-44690, decoded 2026-11-07):** the section to smooth is `copy(loop)` when |I| = 0 (header kept), else
FUN_180146e20(loop, I0 → I1) into a fresh CSh (header zero: an open shape, +0x50 = 0). Each try clears the output list
(44634-44642) and runs ChaikinSmoothSh(d, section). Accepted try: |I| = 0 → loop := out[0] (CSh::operator=, header of
out[0] = the loop's) unless the list or out[0] is empty (then the loop is unchanged, 44664-44667); |I| > 0 → loop :=
section(loop, (0, 0.0) → I0) + out[0] (FUN_180146b10, when not empty) + section(loop, I1 → (n−1, 1.0)) built in a fresh CSh,
so **the loop becomes an open shape** (+0x50 = 0; FUN_18007cd50 then reverses it whenever the wanted direction is not 0, i.e.
always). Both FUN_18007cb70 (70202) and FUN_18007b3c0 (69498) call FUN_18004ae80(this, this+0x340, loop).

### 6.7.8 Spiral for short entries — FUN_1800a6aa0(tol, Cc, r0, r1, a0, sweep, out) (91022-91124)
Radius is linear in the swept fraction f: r(f) = r0 + (r1 − r0)·f, angle a0 + f·sweep. Pieces of at most π/2:
```
if |sweep| <= π/2: f = 1, rE = r1, dθ = sweep
else:              dθ = ±π/2 (sign of sweep; sweep <= 0 → −π/2), f = dθ/sweep, rE = r0 + (r1−r0)·f     91043-91056
fPrev = 0, rem = sweep, aS = a0, rS = r0
loop:
   ent = FUN_1800aab40(Cc, rS, rE, aS, dθ)  appended                                          91065-91076
   if FUN_1800acb50(tol, Cc, rS, rE, aS, dθ, ent):                                           91077-91080
       rem -= dθ; aS = f·sweep + a0; if |rem| < 1e-4: return                                  91081-91085
       fPrev = f; rS = rE; dθ = rem; f = 1; rE = r1
       if |rem| > π/2: dθ = ±π/2; f = (dθ/rem)(1 − fPrev) + fPrev; rE = r0 + (r1−r0)·f        91086-91099
   else: remove ent; dθ *= 0.5; f = (f + fPrev)/2; rE = r0 + (r1−r0)·f                         91101-91122
```
FUN_1800aab40 (spiral piece → entity) and FUN_1800acb50 (tolerance check) are NOT DECODED. Caller FUN_18007c700 uses
(1e-8, Q, 0, P+0x1e0, a0, ±2π) (§3 D 1.4): one turn from radius 0 to the march step.

### 6.7.9 Air dwell after entry (flag 0x40000000) — FUN_180071df0 (64540-64683)
```
out = []
if flags & 0x40000000:                                                     64607
   P0 = last output point (FUN_18008e4b0); T = last tangent (FUN_18008e710), |T| >= 1e-12, normalised   64608-64617
   sets = FUN_180062a70(C) → (s1, s2, s3, s4)                              64618-64621
   if !FUN_1800721a0(C, P0/T, s2, out):          // circle, dir = flags&1, then the opposite dir (64701-64706)
      if !FUN_180072b40(C, P0, entryShapes, out): clear(out)               64625-64628 (NOT DECODED)
      else FUN_1800737e0(C, cetp, out)                                     64631 (NOT DECODED)
```
**Circle — FUN_1800730e0(C, P0/T, dir, s2, out) (65180-65383)**:
```
if s2 empty: fail                                                          65231-65233
ρ = FUN_180072a30(...)                                                     65234
centre = dir ? P0 + ρ·(−T.y, T.x) : P0 + ρ·(T.y, −T.x)                      65236-65248
sa = atan2(P0 − centre)                                                    65257
out = [ 'A' {centre, ρ, sa, sw = dir ? +2π : −2π}, CeFlags 8 ], shape dir = dir ? 1 : −1   65306-65327
split arcs at 1.5725416560468908 (FUN_180123bc0)                            65329
c = FUN_180113300(out[0], s2, 1e-5)
if c & 4: fail                                                             65331-65338
if c & 1: accept only if out[0] has 4 entities and the end of entity 1 (half way round) is inside s2
          (FUN_180112a00(s2, 1e-5, pt) == 2); else fail                    65355-65380
Z profile type 2 (FUN_18003d490) with h = P+0x48, or P+0x240 if P+0x48 <= 1e-7   65340-65351
FUN_180074350(C, out[0])                                                   65352
```
FUN_180072a30 (64946-64969): ρ from FUN_180072280 (NOT DECODED, 230 lines); if that fails: ρ = 0.9(R − r); if
1e-4 < P+0x40 < ρ: ρ = P+0x40; if flags & 0x700 and 1e-4 < 0.9·P+0x90 < ρ: ρ = 0.9·P+0x90.
FUN_180074350 (65758-65824), only with flag 0x40000000: k = last entity index whose z (+0x78) equals the maximum z
(within 1e-7); for entities after k: CeFlags &= ~8, and if 0 then := 4 (65802-65817); if k is the last entity:
FUN_1800740b0 (NOT DECODED).

### 6.7.10 ChaikinSmoothSh pipeline (file ShSmooth.cpp) — decoded 2026-11-07
`ChaikinSmoothSh(d, sh, out, cb, arg)` (1872-1928; the same body as FUN_18014c620 178805-178853): sh empty → nothing; one
entity → out = [copy] (FUN_18011f2e0 154654-154718); else:
```
(d, step, tol) = FUN_18014c250(sh, d)            // 178651-178733
chords = FUN_18014d5c0(sh, step, tol)             // 179292-179536
FUN_18014c820(chords, out, &dev, 0.001, 6, 3.0)  // 178856-179289: out = [Chaikin(chords)]
FUN_18014f740(out[last], 1e-5, cb, arg)           // 180359-180570: collinear merge in place
```
**FUN_18014c250 — step and tolerance.** d := min(d, max(bbox w, h) / 2) (written back). With c = d·cos45 (0.7071067811865475):
tol = |(c, c) − (d, d)| / 4 = 0.10355·d; X = CalcLnLnIntersectPt({(c, c), (−cos45, cos45)}, {(d, d), (1, 0)}) (3611-3649:
lines as point + direction) = (0.41421·d, d); step = 4·|X − (d, d)| = 2.34315·d (2·d if the lines were parallel - never).
**FUN_18014d5c0 — chord polygon.** FUN_18014ef90 (180119-180261): only for a closed shape (+0x50 ≠ 0): the first longest
'L' (squared chord length, strict >; arcs and points skipped); if len² ≥ 4·step²: copy rotated to start at it (FUN_18014fd50
180573-180713: [k .. n) then [0 .. k)), a copy of that line appended, the first entity starts and the appended one ends at the
line's midpoint ('L' p := p + (q − p)·0.5; the 'A' branch, sa += sw/2, sw −= sw/2, is unreachable). Otherwise the input is
used as it is. Output: a new CSh with that shape's header and no entities, pushed to the list. Walk positions are
{int valid (−1 = none), int ent, double t, CPt2 pt}; cur = {0, 0, 0.0, start of entity 0}, prev = {−1}:
```
loop: next = FUN_18014dfc0(sh, cur, step, tol)
      append 'L' cur.pt → next.pt (FUN_18014bf20 178488-178608, bbox updated)
      if next.ent == n−1 and |next.pt − end of entity n−1|² < 1e-6: break
      prev = cur; cur = next
FUN_18014f470(sh, out, prev, step, tol) (180264-180356): if prev.valid ≠ −1 and the last chord's length ≤ step/2 and
      FUN_18014e330(sh, prev, {n−1, 1.0, end}, tol): the last chord is erased and the one before ends at the end.
```
FUN_18014dfc0(sh, cur, s, tol) (179623-179709):
```
repeat:
   next = cur; next.t = 1.0
   L: next.pt = end of entity next.ent ('L' e2, 'A' arc end, else +0x18); d² = |cur.pt − next.pt|²
      if (type 'L' or ('A' and |sw| < 1.7278759594743864)) and 0.25 s² < d² < 2.25 s²: goto ok
      if FUN_18014c4f0(sh, cur, next, s): goto ok
      if d² ≤ s²: if next.ent + 1 ≥ n: goto ok; next.ent += 1; goto L
      FUN_18014dce0(sh, cur, next, s)
   ok: if FUN_18014e330(sh, cur, next, tol): return next
   s *= 0.75
```
FUN_18014c4f0 (178736-178803) + FUN_1800abd30 (93346-93423): CalcEntIntersect(1e-5, entity next.ent, full circle
{'A', centre cur.pt, r s, sa 0, sw 2π}) → up to two parameters on the entity, swapped to ascending; on cur's own entity
those with t < cur.t are dropped; the first left gives next.t and next.pt = FUN_180021580(entity, t).
FUN_18014dce0 (179539-179620): next.ent's end (t 1) when ||cur.pt − end| − s| ≤ ||cur.pt − start| − s| or next.ent == cur.ent,
else its start (t 0).
FUN_18014e330(sh, A, B, tol) (179712-179990): chord A.pt → B.pt; |chord| < 1e-12 → true. For every entity k = A.ent .. B.ent,
the piece (same entity: t A.t → B.t; first: A.t → 1; last: 0 → B.t; middle: whole; 'A': sa' = sa + sw·t0, sw' = sw·(t1 − t0)
resp. sw − sw·t, sw·t): its end point must lie within tol of the chord segment (FUN_1800ad500 94199-94241, nearest point on
a segment clamped to [0, 1], midpoint when |b − a|² < 1e-14); for an arc piece also q = c − r·n when cross(chord, c − A.pt)
≥ 0, else c + r·n (n = chord left normal (−dy, dx)/|chord|), if FUN_1800a46e0 (89839-89866: angle(q − c) − sa' into
[0, 2π], then (−2π, 0] for sw' < 0, t = angle / sw'; false when q is within 1e-14 of c or |sw'| < 1e-12) gives t ≤ 1. Any
point farther than tol → false.
**FUN_18014c820(in, out, &dev, 0.001, 6, 3.0) — Chaikin.** passes = max(1, 6); ratio = max(3.0, 1.00001); a = 1/(ratio+1) =
0.25, b = ratio/(ratio+1) = 0.75. FUN_180124110(0.001, in) (156996-157192) copies the shape with every arc replaced by
ApxArcWithLnSegs(0.001, arc, true) chords (lines copied) - the chord polygon has lines only. Per pass, on the last shape of
a local list, a new shape with its +0x50: for each entity i a copy of it with type 'L', e1 = e2·a + e1·b, e2 = e2·b + e1·a;
from the second on, a joining 'L' (previous e2 → this e1) goes before it; an open shape (+0x50 = 0) then gets back the
input's first e1 and last e2; a closed one gets a closing 'L' (last e2 → first e1). On the first pass dev = max |midpoint of
a joint − original vertex|² (sqrt at the end; unused by ChaikinSmoothSh). out = [new CSh: in's header, the last pass's
entities], UpdMinMaxSh.
**FUN_18014f740(sh, 1e-5, cb, arg) — collinear merge.** For i = 0 ..: the callback (progress) is called, FUN_18014c150
(178611-178648) bisects for j: lo = i, hi = n−1, failed = 0; repeat {c = lo; m = (hi + c)/2; if m == c: {if !failed: m = hi;
if m == c: j = c, stop}; if m == hi and failed: j = c, stop; if FUN_18014ec10(i, m): lo = m else {failed = 1; lo = c;
hi = m}}. FUN_18014ec10 (179993-180116): every entity end k = i .. m−1 within tol of the segment start(i) → end(m). A new
'L' start(i) → end(j) is appended (FUN_18013bf10 bbox) and i = j + 1. The shape's entities and bbox are replaced; the header
is kept. A callback returning 0 throws error 0x67 (ShSmooth.cpp line 0xd2); LAB_180002c6b always returns 1.

### NOT DECODED (6.7)
- `mcce::CalcMedAx` internals; MAE fields +0x08..+0x40 and flag bit 4; FUN_1800c2270 (chain emit, 105132-105476),
  FUN_1800c1c90 (104909-105131), FUN_1800c43e0 / FUN_1800c3c10 (prune post-steps), FUN_1800c64d0, FUN_1800c1220,
  FUN_18014fd50, FUN_1800c2d30 (chain trimming in FUN_1800c1a40).
- FUN_18015b2e0 operation (pairwise boolean primitive: FUN_18015aac0, FUN_180159c60, FUN_18015a4f0, FUN_180159230,
  test FUN_18015c370).
- Trochoidal walk leaf builders: FUN_18007f0d0, FUN_18007e540, FUN_18007f8c0, FUN_18007d5a0, FUN_180081190,
  FUN_1800805d0, FUN_1800832e0; join helpers FUN_1800834f0, FUN_180082120, FUN_180083c10, FUN_180082e80,
  FUN_18011d2d0; step callback LAB_180006aaa (thunk target not in the decompile — needs disassembly at 0x180006aaa).
- FUN_180079570 degenerate-tangent branch: type byte 0x41 with line-shaped fields (68693) — check in disassembly.
- Custom chain: FUN_180132200 records (FUN_18012f3b0, FUN_180131740, FUN_180131240); FUN_1801270c0 orientation.
- Spiral piece fit FUN_1800aab40 and check FUN_1800acb50. (Chaikin callback LAB_180002c6b: decoded, progress only - §6.7.7.)
- Air dwell: FUN_180072280 (radius search), FUN_180072b40 (fallback path), FUN_1800737e0 (post-process),
  FUN_1800740b0.

## 6.8 Corrections to earlier sections found in this pass
- **List offsets (§3 C, §3 D):** C+0x350, C+0x420 and C+0x448 are not separate lists. They are the begin pointers of
  C+0x340 (tool-centre region, RG+0xa8), C+0x410 (current material, RG+0x178) and C+0x438 (bullnose material,
  RG+0x1a0) (FUN_18002a2a0 l.27333-27559; FUN_180062a70 l.56613; §6.2.0, §6.4.1). "Index into C+0x420" = index into
  C+0x410; "C+0x448 non-empty" = bullnose material present.
- **§3 C.0 / C.1:** E+0x130 / E+0x268 = total engaged sweep on the step's end-cap circle, written by FUN_18002fc80
  (L30183, L30514), not by FUN_180037070 / FUN_1800380f0 (§6.1.0, §6.1.6).
- **§3 C.4 pass number:** refined in §6.1.7 (FUN_180033cf0 / FUN_180035ff0 / FUN_180033b60).
- **§3 D (CSh+0x58):** for material loops = CCetp record count at creation (FUN_180032a20 L31853-31891); FUN_1800686c0's
  "largest +0x58" therefore selects the most recently created piece (§6.1.7).
- **§1 FUN_1800942b0:** it is a QuickHull convex hull of all shape vertices (arcs linearised at 1e-8), not an
  end-to-end join (§6.6.2). FindGaps' "loop" is that hull.
- **§3 B.4 FUN_18015be30:** it is the body of the export `MCCECalcPockIntersect` (l.11558ff), i.e. it intersects the
  nest groups of its input pairwise (§6.5.6); "REG += FUN_18015be30(Y ∪ X)" is that operation.
- **§3 B.4 FUN_1800aeae0:** bisection returning the largest x in [Q, P] (tol 0.1·(P − Q)) where the predicate holds
  (§6.5.4); the predicate thunk LAB_18000146f is unverified (§6.5.4).
- **§3 D 1.6 Chaikin smoothing:** distances tried are d0, 0.9·d0 … 0.2·d0; **0.1·d0 is never tried** (loop exits
  first, l.44631-44692; §6.7.7).
- **§3 D 1.3 / 2.5 approach FUN_18005c980:** retries use θ_k = θ0 + k·(110° − θ0)/2 (k = 0..2) with θ0 the original
  angle (`local_220` is never updated, l.53516/53692-53694) and stop early only if a crossing is found or θ0 ≥ 110°
  (§6.4.4).
- **§3 D 1.3 FUN_180012390:** default previous position = first entity of the first non-island AIR shape at
  z = P+0x80 + 1.0 (§6.4.3).

## 6.9 Still NOT DECODED after this pass (consolidated; details in each subsection's list)
- Geometry kernels: MCCECalcOffset / MCCECalcContourOffset (mode bits), FUN_180153b30 / FUN_180153390 and the pairwise
  kernel FUN_18015b2e0 (FUN_18015aac0, FUN_180159c60, FUN_18015a4f0, FUN_180159230, classifier FUN_180163960),
  FUN_180146e20 (section copy), CalcEntIntersect, CalcLnLnIntersectPt, CalcCirCirIntersect, nearest-point routines,
  PtInSh, ApxArcWithLnSegs, ReconfigSelfIsectSh, UpdDirShAr, DetNestLvlShAr, `mcce::CalcMedAx`.
- Thunk targets (closed 2026-11-07, objdump of mcce.dll; each thunk is one `jmp`): LAB_180002969 → FUN_1800314a0,
  LAB_180005c72 → FUN_180031280, LAB_1800034b8 → FUN_1800312f0, LAB_180001b9a → FUN_18006c470 (not in the decompile;
  disassembly 0x18006c470-0x18006c4c1: ascending |p − ctx|², ctx = the qsort_s context point), LAB_18000146f →
  FUN_180092180, LAB_1800011ef → FUN_180019180 (both candidates confirmed), LAB_180006aaa → FUN_1800826d0 (trochoid step
  root, L72939), LAB_180002c6b → FUN_180048f30 (Chaikin, L43371), LAB_1800074dc → FUN_180133630 / LAB_180004e21 →
  FUN_180133680 (ISAB orders, §6.11.2). The bodies of FUN_1800826d0 / FUN_180048f30 are not re-read here.
- (closed 2026-11-07) FUN_180034ac0's rotation angle: §6.1.2.
- Link: guided planner FUN_1800f1080 tree; route builder internals (FUN_180109960, FUN_180109e20, FUN_18010b6f0);
  link post-processors FUN_18004a070, FUN_180049950, FUN_180045c40; FUN_180060cd0 (approach fallback).
- Targets: FUN_180053480, FUN_180054450 (section classes), FUN_180053cd0, FUN_180053760, FUN_180053d60, FUN_180059630,
  FUN_1800512c0, FUN_180051cd0; cut-order modes 1/2 (FUN_180055190, FUN_180055c90, FUN_180054a70); FUN_180067820.
- Entry: trochoidal leaf builders (FUN_18007f0d0, FUN_18007e540, FUN_18007f8c0, FUN_18007d5a0, FUN_180081190,
  FUN_1800805d0, FUN_1800832e0) and join helpers; medial chain emit FUN_1800c2270 / FUN_1800c1c90; custom-chain
  cleanup FUN_180132200; spiral piece fit FUN_1800aab40 / FUN_1800acb50; air-dwell FUN_180072280, FUN_180072b40,
  FUN_1800737e0, FUN_1800740b0.
- PeelMotMat output wiring (FUN_1800e28c0, unused local `local_50`): disassembly needed.
- Unchanged from earlier lists: MOAT_FIRST preparation FUN_18002be40 / FUN_18002c360; CBands FUN_180143d20; producers of
  flags 0x4000000 / 0x8000000; the two CSh arrays in each output record. (Area Mill is not an mcce engine: it runs in
  McHmm.dll through mckernel, formats/area_mill.md 0 / 11.)

## 6.10-6.12 Third pass (2026-10-15, for the C port viewer/mcce_dyn.c; formats/mcce_port_plan.md)
Decoded from decomp/mcce_all.c only, same rules as above. These sections supersede §6.9 where they decode an item
listed there (target generation, kernel semantics, entry material removal, attach tests, link bookkeeping, routes).

## 6.10 Target generation (third pass)

All line numbers are `decomp/mcce_all.c`. `C+x` = CCePock offset (C+x = P+(x−8)). B = the tool-centre region list
C+0x340, S1/S2/S4 = the region sets of §6.4.2 (S1 = material grown by R−r, S2 = containment set, S4 = inset clip set),
`bands` = C+0x368, cur = last output point (FUN_18008e4b0), curT = its end tangent (FUN_18008e710). "Position" =
`{int shape, int ent, double t, CPt2 pt}` (0x20 bytes, the layout FUN_18013cfe0 / FUN_18006f4f0 / FUN_18006f1c0 use).
Helper names used below (all already in this spec): PointAt = FUN_18006f1c0 (forward walk), PointBack = FUN_18006f4f0
(backward walk, 63073-63155), nearest(list) = FUN_18013d010 (nearest shape/ent/t/pt over a list; FUN_18013cfe0
170368-170376 is the same call writing one Position), nearestOn(sh) = FUN_18013d200 / FUN_18013caf0 (nearest on one
shape), len(sh) = FUN_180121980, secLen(sh, e1,t1, e2,t2) = FUN_180149d80 (177357-177451: sum of entity lengths over
the FUN_180147ed0 entity count, first entity scaled by (1−t1), last by t2, single entity by (t2−t1)), tangent =
FUN_18011ffd0, Route = FUN_18010c040 (§6.3.0), Section = FUN_180146e20.

### 6.10.0 Records

**ISAB (0x50) as produced by FUN_180134690 / FUN_180134270** (record built in FUN_180133c70 165584-165633):
| off | content |
|---|---|
| +0x00 | crossing point (CalcEntIntersect output) |
| +0x10 | X shape index (index in the first list; for FUN_180134690 always 0) (165728) |
| +0x14 / +0x18 | X entity index / t on the X entity (165628, local_f0) |
| +0x20 | Y shape index (index in the second list) (165729) |
| +0x24 / +0x28 | Y entity index / t on the Y entity (165629, local_e0) |
| +0x30 / +0x34 | next / previous record **on the same Y shape** (cyclic), final values from FUN_1801349c0 (165952-165961) |
| +0x38..+0x4f | 0 (memset 165584; +0x38 = uStack_d0 = 0) |

**CSec (0x20)**, section of B[i] between two crossings (FUN_180054450, 49140-49177): +0x00 vtable, +0x08 int a
(start ISAB index), +0x0c int b (end ISAB index), +0x10 int class, +0x18 double length.

**CTarg (0x108)** default construction (resize FUN_180059380 → FUN_18004e290 → FUN_18004f710): every field 0, the CSh at
+0x70 empty. Writers decoded in this pass:
| writer | +0x00 | +0x10 | +0x20 | +0x22 | +0x28 | +0x30 / +0x50 | +0x70 path |
|---|---|---|---|---|---|---|---|
| FUN_1800522c0 (boundary) 48207-48232 | A.pt | tangent of B[i] at A | 0 | 1 | d | A / Bpos | – |
| FUN_180051480 (circle-tangent) 47865-47875 | pos.pt on S1[i] | tangent of S1[i] at pos | 0 | 0 | – | – | – |
| FUN_180051cd0 (small piece) 47981-47991 | pt on S1[i] | normal of S1[i] (below) | 1 | 0 | – | – | – |
| FUN_180059680 (arc to crossing) 51869-51924 | crossing | circle tangent (not unit) | 1 | 0 | – | – | 1 arc |
| FUN_18005b480 (S-arc) 52837-52846 | q on P | tangent of P at q | 0 | 0 | – | – | 2 arcs |
This closes the §6.3 item "writer of CTarg +0x20".

**CEnp (0x10)** (mode 2, FUN_180054a70): +0x00 byte ok (route found), +0x01 byte flag (S4 route found), +0x08 double len.
Zero-initialised by FUN_18004e160. **CEti (0x48)** (mode 1, FUN_180055190): +0x00 int S1 shape, +0x08 CPt2 P1, +0x18
CPt2 P2, +0x28 CPt2 R, +0x38 byte ok, +0x39 byte flag, +0x40 double len. Zero-initialised by FUN_18004e1b0 (so shape
starts at 0, not −1).

### 6.10.1 Crossing list: FUN_180134690 (165782-165816) → FUN_180134270 (165674-165779)
```
FUN_180134690(sh, Y, out):  X = [sh];  FUN_180134270(X, Y, out)                       // 165798-165806
FUN_180134270(X, Y, out):
  out = []
  for ix over X, iy over Y (nested, X outer):                                          // 165715-165758
     tmp = FUN_180133c70(X[ix], Y[iy])
     for r in tmp: r.+0x10 = ix; r.+0x20 = iy; r.+0x30 += n(out); r.+0x34 += n(out)    // 165726-165736
     append tmp to out                                                                  // 165747
  qsort(out, comparator LAB_1800074dc)                                                 // 165767-165768  (key NOT DECODED)
  FUN_1801349c0(out)                                                                   // 165769
FUN_180133c70(a, b, out):    // one shape pair
  out = []
  if bbox(a) and bbox(b) overlap within 1e-5 (CSh +0x30..+0x48):                       // 165577-165583
     for ea (index i) of a whose entity bbox (+0x40..+0x58) overlaps bbox(b) within 1e-5:   // 165592-165606
        for eb (index j) of b whose bbox overlaps bbox(ea) within 1e-5:                // 165613-165618 (*)
           n = CalcEntIntersect(1e-5, ea, eb, tA[2], tB[2], pt[2])                     // 165619-165621
           for h = n-1 .. 0: out += {pt[h], 0, i, tA[h], 0, j, tB[h], +0x30 = +0x34 = -1, +0x38 = 0}   // 165622-165637
     if out non-empty: qsort(out, LAB_1800074dc); FUN_180133b30(a, b, out); FUN_1801349c0(out)     // 165655-165667
```
(*) Literal decompiler text of the y test is `ymin_a−1e-5 < ymax_b || (ymin_a−1e-5 == ymax_b && ymin_b <= ymax_a+1e-5)`
(165606, 165618); it is only a prefilter before CalcEntIntersect.

- **FUN_180133b30 (165493-165527)**, duplicate removal: for i = 0..n−1, p = i−1 (p = n−1 for i = 0); if p != i and
  FUN_180134800(a, b, rec[p], rec[i]) then erase rec[i] and repeat i (165509-165522).
  FUN_180134800 (165819-165858): true iff |Δx| ≤ 1e-10 and |Δy| ≤ 1e-10 (point), and (same X entity or
  FUN_18014b720(a, eI, tI, eJ, tJ, 1e-10) in either order) and the same for the Y side on b.
  FUN_18014b720 (178227-178374): true iff the forward section (e1,t1)→(e2,t2) of the shape (t clamped to [0,1], entities
  stepped with FUN_180122fb0) has length ≤ tol.
- **FUN_1801349c0 (165861-165975)**, next/prev linking:
```
idx = [0 .. n-1]; qsort_s(idx, comparator LAB_180004e21, context = records)          // 165901-165926 (key NOT DECODED)
walk idx in that order; a "group" is a maximal run of consecutive idx entries with equal +0x20 (Y shape)  // 165929-165943
for every group g = idx[s..e-1], position p in s..e-1:
   rec[idx[p]].+0x30 = idx[p+1 < e ? p+1 : s]                                          // 165945-165953
   rec[idx[p]].+0x34 = idx[p > s ? p-1 : e-1]                                          // 165955-165961
```
So +0x30/+0x34 form a cyclic ring per Y shape. **NOT DECODED: both comparators** (LAB_1800074dc = list order,
LAB_180004e21 = ring order); they are thunk labels with no body in the text. Evidence from the consumers only: the ring
code needs LAB_180004e21 to keep equal +0x20 together, and every consumer treats consecutive list records as
consecutive positions along the X shape (FUN_180113300 148545-148553, FUN_180054450 49147-49177, FUN_18005c980
"last ISAB"), i.e. the list order is presumably X-position order. Not verified.
- FUN_180134270 does not call the multi-pair de-dup FUN_1801339b0 (165443-165490); only per-pair de-dup happens.
- FUN_180133820(s, X) (165387-165440): erases every record with +0x20 == s and decrements every remaining +0x30/+0x34
  that was greater than the erased index.

### 6.10.2 Region classifiers
**Point classifiers.** FUN_180112a00(set, tol, pt) (148133-148165): first shape in list order with PtInSh == 1 → 1;
with PtInSh == 2 → 4 if the shape is an island (+0x54 bit0) else 2; none → 4. FUN_180112e80(set, tol, pt) (148298-148387,
group test at 148360-148363): splits `set` into nest groups (shapes collected until and including a non-island) and runs
FUN_180112a00 on each group; the first group result 1 or 2 is returned, else 4. FUN_180112af0(set, bands, tol, pt) (148168-148295)
(same structure) runs FUN_1801128d0 (148083-148130)(group, bandsOfGroup, tol, pt), which is FUN_180112a00 except that the tested shape
is FUN_180144630(bands[j], pt) when bands are given (FUN_180144630 NOT DECODED).

**Section classifier FUN_1801130f0(sh, e1, t1, e2, t2, whole, set, tol) (148390-148438):**
```
onB = false (bVar4), resetOn = false (bVar3), inIsl = false (bVar2)
for s in set (list order):                                                             // 148408-148432
   p = FUN_1801489a0(sh, e1, t1, e2, t2, whole, s, tol)                                 // 148412
   if p == 1: resetOn = onB = true                                                      // 148414-148417
   elif p == 2:
      if s is island: inIsl = true
      else: if !inIsl and !resetOn: return 2;  inIsl = resetOn = false                  // 148418-148429
return onB ? 1 : 4                                                                      // 148434-148438
```
FUN_1801489a0 (176706-176764): entity count = e2+1+(nEnt−e1) if e2 < e1, or e2 == e1 and t2 < t1, or `whole`; else
e2−e1+1 (176735-176741); samples by the iterator FUN_180148820 (§6.1.4: per entity at 1/3, 2/3, 1 of the t range, last
end sample skipped); returns the first PtInSh(sample, s, tol) that is not 1; all samples 1 → 1; no sample → 4.

Meaning (direct from the code, with set = nest groups `[islands…, outer]` as everywhere in this spec):
- **2** = the section lies inside an outer loop of `set` and, within that group, neither inside an island nor lying
  completely on an island boundary (all samples on a boundary).
- **1** = not 2, and the whole section lies on the boundary of at least one shape of `set`.
- **4** = everything else (outside every outer loop, or inside an island).

**Curve classifier FUN_180113300(sh, set, tol) (148467-148567)** — OR of the section classes of `sh` cut at its
crossings with `set`:
```
X = FUN_180134690(sh, set)                                                             // 148518
nodes = X;  addEnd = (X empty) or secLen(sh, X[last] → (nEnt-1, 1.0)) > tol            // 148520-148532
if X empty or secLen(sh, (0,0.0) → X[0]) > tol: insert node (ent 0, t 0.0) at the front // 148533-148537
if addEnd: append node (ent nEnt-1, t 1.0)                                             // 148539-148544
r = 0; for k = 1..n-1: r |= FUN_1801130f0(sh, node[k-1].ent,.t, node[k].ent,.t, 0, set, tol)   // 148545-148555
return r          // 0 when fewer than 2 nodes
```
Result bits: **1** = some piece runs along a boundary of `set`, **2** = some piece is inside the set's material (class
2 above), **4** = some piece is outside. Uses in this pass: `!= 4` = not entirely outside; `& 4 == 0` = nothing
outside; `& 5 == 0` = entirely strictly inside; `& 2` = touches the inside.

**FUN_1801136d0(sh, set, tol) (148570-148622):** samples the whole of `sh` (iterator FUN_180148820 from (0, 0.0) to
(nEnt−1, 1.0)); for each sample c = FUN_180112a00(set, tol, pt); the first c ∈ {2, 4} is returned
(`(c−2) & ~2 == 0`, 148612); otherwise 1 (also for sh == null or empty set).

### 6.10.3 Boundary targets: FUN_180052070 (48000-48063)
```
FUN_180052070(C, B, S1, X730 = C+0x730, targets, used):
  targets = [];  if used: used = []                                                     // 48020-48039
  for i over B:                                                                         // 48040-48052
     progress FUN_180114140(C+0x270, C+0x700, 0)
     X = FUN_180134690(B[i], S1)            // X side = B[i], Y side = S1 shapes        // 48043
     FUN_180053480(C, B[i], S1, X)          // drop S1 shapes that only touch B[i]      // 48045
     FUN_1800522c0(C, B, i, S1, X, X730, targets, used)                                 // 48047
```
**FUN_180053480(C, b = B[i], S1, X) (48599-48668)** — keeps only S1 shapes that really cross b:
```
k = 0
while k < n(X):                                                                         // 48609-48665
   in = out = 0;  cur = k;  nxt = X[k].+0x30
   loop:                                                                                // 48617-48647
      p = FUN_1801489a0(S1[X[cur].+0x20], X[cur].+0x24, X[cur].+0x28, X[nxt].+0x24, X[nxt].+0x28, 0, b, 1e-5)
      p == 2: in++;  p == 4: out++
      stop when (in and out both > 0) or after the section that ends at k (nxt == k); else cur = nxt, nxt = X[nxt].+0x30
   if in == 0 or out == 0: FUN_180133820(X[k].+0x20, X)   // erase all records of that S1 shape; k unchanged   // 48647-48649
   else: k++ and skip following records with the same +0x20                             // 48651-48663
```
(The S1 sections between consecutive crossings, in ring order along the S1 shape, are tested against the single loop
b with PtInSh, island flag not used.)

**FUN_1800522c0(C, B, i, S1, X, X730, targets, used) (48067-48313):**
```
if n(X) == 0: return                                                                    // 48147
n0 = n(targets);  K = int list resized to n0 (FUN_180058c30 = resize, cf. 165901-165912)   // 48139, 48148-48152
sec = FUN_180054450(B, i, S1, X, C+0x270, C+0x700)                                      // 48155
for k = 0 .. n(sec)-1:                                                                  // 48164-48284
   pv = (k == 0) ? n(sec)-1 : k-1;  nx = (k < n(sec)-1) ? k+1 : 0                       // 48167-48176
   if sec[k].class == 2 or (sec[k].class == 1 and sec[pv].class == 4 and sec[nx].class == 4):   // 48177-48180
      d = C+0x1f0 (P+0x1e8);  if sec[pv].len <= d: d = 0.5·sec[pv].len                   // 48181-48184
      Bpos = {i, X[sec[k].a].+0x14, X[sec[k].a].+0x18, X[sec[k].a].pt}                   // 48185-48193
      Apos = PointBack(0, d, B, Bpos)                                                    // 48194
      if FUN_180053cd0(class, Apos.pt, X730): continue                                   // 48196-48197
      append CTarg{+0x00 Apos.pt, +0x10 tangent(B[i], Apos.ent, Apos.t, tol 1e-10), +0x20 0, +0x22 1, +0x28 d,
                   +0x30 Apos, +0x50 Bpos}                                               // 48198-48232
      K.push(k)                                                                          // 48233-48249
      if used and X[sec[k].a].+0x20 not in used: used.push(it)                           // 48254-48273
FUN_180053760(C, X, sec, n0, K, targets)                                                // 48286
```
**FUN_180054450(B, i, S1, X, prog, progArg, sec) (49090-49234):**
```
sec = n(X) records
for k = 0..n(X)-1:                                                                     // 49139-49180
   progress (if prog)
   sec[k].a = k mod n;  sec[k].b = (k+1) mod n
   sec[k].class = FUN_1801130f0(B[i], X[a].+0x14, X[a].+0x18, X[b].+0x14, X[b].+0x18, whole = (n(X) == 1), S1, 1e-5)
   sec[k].len   = secLen(B[i], X[a].+0x14, X[a].+0x18, X[b].+0x14, X[b].+0x18)
for k = 0 ..:  p = (k == 0) ? n-1 : k-1                                                 // 49186-49212
   if sec[k].class == sec[p].class and k != p: sec[p].b = sec[k].b; sec[p].len += sec[k].len; erase k; redo k
for k: if sec[k].class == 1 and sec[k].len < 1e-7: erase k; redo k                      // 49214-49230
```
So the classes 1/2/4 of §6.3.6 are the FUN_1801130f0 classes of the **boundary B[i] against S1**: 2 = this stretch
of the tool-centre boundary lies inside the grown material S1, 1 = it runs along an S1 boundary, 4 = outside S1.
Targets are placed at the start crossing of every "inside" stretch (and of every on-boundary stretch flanked by outside
stretches), backed off by d along B[i] so the link arrives on B and runs in along it (+0x22 run-in from A to Bpos).

**FUN_180053cd0(class, pt, list) (48828-48852):** true iff some CPt2 q of `list` has |pt − q|² < 1e-10; `class` is not
used. The list C+0x730 holds **target points that were chosen but produced no output**: FUN_180065ff0 (58378-58456)
stores the chosen target's +0x00 in C+0x718 with C+0x710 = 1 and C+0x728 = cetp+0x20 (58448-58453); at the next link
FUN_1800653b0 (57935-57946) is true iff C+0x710 and C+0x728 == cetp+0x20 (the counter has not moved), and then C+0x718
is appended to C+0x730 (58086-58089 general link, 59480-59483 local link). C+0x730 is cleared at region start (62667).

**FUN_180053760(C, X, sec, n0, K, targets) (48671-48767):**
```
for p = n0 .. n(targets)-1:                                                             // 48695-48764
   if sec[K[p]].class == 1:
      for q = n0 .. n(targets)-1:
         if sec[K[q]].class != 1 and X[sec[K[q]].a].+0x20 == X[sec[K[p]].a].+0x20:    // 48708-48712
            erase targets[p] and K[p]; p--; break                                      // 48714-48753
```
(an on-boundary target is dropped when the same B[i] gave an inside target on the same S1 shape).

### 6.10.4 Targets on S1 shapes: FUN_180052d70 (48403-48473)
```
FUN_180052d70(C, onlyInside, B, bands, S1, cetp, targets, used):
  curShape = -1; if cur exists: curShape = nearest(S1, cur).shape                       // 48433-48438
  for i over S1:                                                                        // 48441-48470
     progress;  skip if i in used                                                       // 48443-48453
     r = FUN_180113300(S1[i], B, 1e-5);  skip if r == 4 or (onlyInside and !(r & 2))    // 48455-48457
     c = FUN_180053d60(C, S1, i, cetp)                                                  // 48458
     if i == curShape and FUN_180059630(C, B, S1, cetp, c, targets): continue           // 48458-48460
     if c == 0: FUN_1800512c0(C, B, bands, S1, i, cetp, targets)                         // 48461-48462
     else:      FUN_180051cd0(C, B, S1, i, cetp, targets)                               // 48464-48465
```
**FUN_180053d60(C, S1, i, cetp) (48855-48897) → c ("small piece"):** if cur exists: q = nearestOn(S1[i], cur)
(48889-48891); m = nearest(C+0x410 material list, q).shape (48892); value of FUN_180053e80(C, C+0x420[m], 35.0)
(48893-48894). The function is decompiled `void`; the caller uses its AL (48458). Reading c as FUN_180053e80's result
(its last call) and as 0 when FUN_18008e4b0 fails is from register flow, not explicit in the text.
**FUN_180053e80(C, sh, pct) (48900-48966):** lim = (C+0x30 − C+0x38)·(pct/100)·2 = 0.7·(R − r) for pct 35 (48922);
grow a box over the entity bboxes (+0x40..+0x58) in entity order; as soon as the box diagonal² > lim² return 0
(48957-48959); end → 1. So c = 1 iff the bbox diagonal of the material loop nearest to S1[i] (near cur) is ≤ 0.7(R−r).

**FUN_180051cd0(C, B, S1, i, cetp, targets) (47894-47997)** — three targets around a small piece:
```
L = len(S1[i]);  pos[0] = {i, nearestOn(S1[i], cur)}                                    // 47924, 47946-47951
pos[1] = PointAt(0, L/3, S1, pos[0]);  pos[2] = PointAt(0, L/3, S1, pos[1])             // 47953-47955
for k = 0..2:                                                                           // 47945-47995
   if FUN_180112e80(B, 1e-5, pos[k].pt) != 4:
      (tx, ty) = tangent(S1[i], pos[k].ent, pos[k].t, 1e-10)
      dir = (flags & 1) ? (ty, -tx) : (-ty, tx)                                         // 47961-47972
      append CTarg{+0x00 pos[k].pt, +0x10 dir, +0x20 1}                                 // 47981-47991
```
(+0x20 = 1 makes the approach a plain line from the clip set, FUN_18005c980 53530, §6.4.4.)

**FUN_1800512c0(C, B, bands, S1, i, cetp, targets) (47600-47650):** ρ list = FUN_180064690(C, list, null) (§6.3.0
radius list, 47623); for each ρ in list order: if ρ ≤ 0 stop; if FUN_180051480(C, B, bands, S1, i, cetp, ρ, targets)
return 1 (47625-47637); return 0.

**FUN_180051480(C, B, bands, S1, i, cetp, ρ, targets) (47653-47891)** — first point of S1[i] where a circle of radius ρ
tangent to S1[i] stays clear of the two circles of radius ρ tangent to the current motion:
```
L = len(S1[i]);  lim = 4ρ²                                                              // 47715-47716
T = curT, fail if |T| < 1e-12;  n = ρ·(Ty, -Tx)/|T|                                     // 47752-47760
K0 = cur + n;  K1 = cur - n                                                             // 47761-47822
pos, dist = FUN_1800502a0(first call)                                                   // 47824
while dist <= 1.1·L:                                                                    // 47826
   (tx, ty) = tangent(S1[i], pos.ent, pos.t, 1e-10)                                     // 47829
   (a, b) = (flags & 1) ? (tx, -ty) : (-tx, ty);  m = |(a,b)|, need m >= 1e-12          // 47832-47845
   K = pos.pt + ρ·(b, a)/m          // climb: left normal (-ty, tx); else right normal (ty, -tx)   // 47846-47849
   if |K-K0|² >= lim and |K-K1|² >= lim and FUN_180112af0(B, bands, 1e-5, pos.pt) != 4:   // 47850-47854
      append CTarg{+0x00 pos.pt, +0x10 (tx, ty), +0x20 0}; return 1                      // 47857-47877
   pos, dist = FUN_1800502a0(next)                                                      // 47880
return 0
```
**FUN_1800502a0(C, S1, i, cur, ρ, &first, &tmp, &pos, &dist) (47078-47120):** first call (first.shape < 0, the caller
initialises it to −1, 47751): first = pos = {i, nearestOn(S1[i], cur)}, dist = 0 (47091-47101). Later calls:
pos = PointAt(0, 0.5ρ, S1, pos) and dist += walked length (47104-47112). Then always FUN_180050410(C, S1[i], &pos.ent,
&pos.t, &pos.pt, &w) and dist += w (47115-47118).
**FUN_180050410(C, sh, &ent, &t, &pt, &w) (47123-47318)** — skips runs of entities whose byte +0x12 has bit value 2 (the
meaning of that bit on S1 entities is not established in this pass):
```
D = C+0x1e8 (P+0x1e0, march step); w = 0                                                // 47148-47151
if ent has no bit 2:                                                                    // 47155
   acc = length of ent from t to its end
   j = ent+1 (cyclic); while j != ent: if j has bit 2: break; if acc > D: return; acc += len(j); j++   // 47181-47206
   if acc > D or j == ent: return                                                       // 47208-47213
   ent = j; t = 0; pt = start point of j; w += acc                                      // 47214-47239
if ent has bit 2:                                                                       // 47241
   acc = length of ent from t to end; j = ent+1; while j != ent and j has bit 2: acc += len(j); j++   // 47243-47290
   if j != ent: ent = j; t = 0; pt = start of j; w += acc                               // 47292-47316
```

**FUN_180059630(C, B, S1, cetp, c, targets) (51758-51776):** if C+0x227 (P+0x21f, bIsBullnoseAvailA) == 0 → 0.
Else c == 0 → FUN_180059ca0, c != 0 → FUN_180059680 (both with (C, B, S1, cetp, targets)). Only called for the S1
shape nearest to cur. ρ below = C+0x230 (P+0x228, dMinCutRad).

**FUN_18005a000(C, cetp, outA, outB) (52108-52357)** — circles tangent to the current motion:
```
T = unit(curT) (fail if cur/curT missing or |curT| < 1e-12)                             // 52182-52190
nrm = (flags & 1) ? (-Ty, Tx) : (Ty, -Tx)                                               // 52191-52207
c0 = cur - ρ·nrm;  sa = atan2(nrm.y, nrm.x)   (angle from c0 to cur)                    // 52210-52212
sw = (flags & 1) ? -2π : +2π                                                            // 52213-52218
outA = [ CSh{ 'A'(c0, ρ, sa, sw) }, +0x50 = (flags&1) ? -1 : 1 ]                        // 52267-52280
outB = [ CSh{ 'A'(c0, 2ρ, sa, sw) }, same +0x50 ]   (only if outB requested)            // 52281-52293
```
**FUN_18005a8a0(C, A, S1, cetp, outP, outQ) (52360-52560):**
```
s = nearest(S1, cur) (shape, ent, t, pt); require s.shape >= 0, A non-empty, A[0] first entity 'A'   // 52444-52454
c0 = centre of A[0].ent[0]
end = FUN_18005ae90(C, S1, 3ρ, c0, s)                                                   // 52459
P = Section(S1[s.shape], s.ent, s.t, end.ent, end.t, flag 0)  (fail if 0)               // 52478-52481
Q = MCCECalcContourOffset(1, (flags&1) ? ρ : -ρ, [P], 0x100, 0.0, 1e-6, 1e-6, 1e-6)     // 52485-52491 (internals NOT DECODED)
if Q non-empty or outQ not requested: outP = [P]; outQ = Q; return 1                     // 52492-52531
```
**FUN_18005ae90(C, list, D, c, start, &end) (52563-52760)** — walk forward from `start` sampling the mid point and end
point of each entity: samples are (start.ent, (start.t+1)/2), (start.ent, 1.0), then for each following entity (cyclic)
t = 0.5 and t = 1.0, until back at start.ent, then (start.ent, start.t/2). The first sample with |sample − c|² > D² is
returned (52673, 52741); after the final sample the farthest sample seen is returned (52662-52685). Returns 0 only if
the list or its first shape is empty.

**FUN_180059680 (c = 1) (51779-51968):** A = FUN_18005a000(C, cetp, A, none); P = FUN_18005a8a0(C, A, S1, cetp, P, none);
I = Isect(A, P) (FUN_180134270, 51835-51840); require I non-empty.
```
v = I[0].pt - c0;  dir = (sw(A) > 0) ? (-vy, vx) : (vy, -vx)                            // 51842-51860
append CTarg{+0x00 I[0].pt, +0x10 dir, +0x20 1,
             +0x70 = one entity: copy of A's arc with sw := sw(A)·I[0].+0x18 }          // 51862-51924
if FUN_18005b3c0(C, B, S1, rec+0x70): return 1;  else erase the record, return 0        // 51925-51932
```
**FUN_180059ca0 (c = 0) (51971-52106):** (A, A2) = FUN_18005a000 (radius ρ and 2ρ); (P, Q) = FUN_18005a8a0; I = Isect(A2, Q);
return FUN_18005b480(C, B, S1, I, A, P, targets) (52032-52040).
**FUN_18005b480 (52783-52866):** require I non-empty; q = nearest(P, I[0].pt) with q.shape ≥ 0 (52823-52825); append
CTarg{+0x00 q.pt, +0x10 tangent(P[q.shape], q.ent, q.t, 1e-5)} (52827-52844); if !FUN_18005b760(C, I, A, rec,
rec+0x70) or !FUN_18005b3c0(C, B, S1, rec+0x70): erase it, return 0 (52846-52860); return 1.
**FUN_18005b760(C, I, A, rec, path) (52869-52994)** — two opposite arcs cur → q:
```
p = I[0].pt;  a = A[0].ent[0] (centre c0, start sa0)
sw1 = wrap_[-π,π](atan2(p - c0) - sa0);  climb: if sw1 > 0: sw1 -= 2π;  else: if sw1 < 0: sw1 += 2π   // 52894-52919
a2 = atan2(c0 - p);  sw2 = wrap_[-π,π](atan2(q - p) - a2)                               // 52920-52940
climb: if sw2 < 0: sw2 += 2π;  else: if sw2 > 0: sw2 -= 2π                              // 52941-52950
path = [ copy of a with sw = sw1,  'A'(centre p, radius ρ, sa = a2, sw = sw2) ]         // 52951-52992
(fail if |p - c0| or |c0 - p| or |q - p| ≤ 1e-12 per component)
```
**FUN_18005b3c0(C, B, S1, sh) (52763-52780):** accept iff len(sh) ≤ 1.5·|2π·ρ| and FUN_180113300(sh, B, 1e-5) & 5 == 0
(entirely inside B) and FUN_180113300(sh, S1, 1e-5) & 2 == 0 (does not enter S1).

### 6.10.5 Cut-order filters (all keep exactly one target; nothing happens with fewer than 2 targets)
**FUN_180050af0(C, cetp, Q, S2, S4, out, &flag) (47321-47597)** — common route builder, P = cur:
```
out = []; flag = 0                                                                      // 47458-47461
nP2 = nearest(S2, P); nQ2 = nearest(S2, Q); nP4 = nearest(S4, P); nQ4 = nearest(S4, Q)  // 47465-47468
G2 = FUN_1800475c0(S2, nP2, nQ2) or return                                              // 47471-47473
if (FUN_180112a00(G2, 1e-5, P) == 4 and |P - nP2.pt| > 0.001) or
   (FUN_180112a00(G2, 1e-5, Q) == 4 and |Q - nQ2.pt| > 0.001): return                    // 47474-47482
out = Route(1, G2, 'L'(nP2.pt -> nQ2.pt))                                               // 47483-47496
if out non-empty and (G4 = FUN_1800475c0(S4, nP4, nQ4)) and FUN_1801136d0(G4[0], G2, 1e-5) == 2:   // 47497-47506
   r = Route(1, G4, 'L'(nP4.pt -> nQ4.pt))                                              // 47507-47520
   if r non-empty: flag = 1; if len(out[0]) < len(r[0]): out = r                         // 47521-47545
```
(The longer of the two routes is kept: `if (dVar5 < dVar4)` with dVar4 = len(r[0]), dVar5 = len(out[0]), 47524-47526.)
**FUN_1800475c0(C, set, posP, posQ, out) (42418-42562):** nest groups [k..g] (g = first non-island at or after k, or the
last shape); the first group with k ≤ posP.shape ≤ g and k ≤ posQ.shape ≤ g is copied to out → 1; none → 0
(unlike FUN_180047920 there is no single-non-island shortcut).

**Mode 2 FUN_180054a70(C, cetp, S2, S4, targets) (49237-49462)** — targets already sorted by distance to cur:
```
best = 3.4028234663852886e38                                                            // 49300-49301
for i in order:                                                                         // 49317-49347
   if |cur - t_i.pt| > best: break                                                      // 49322-49326
   FUN_180050af0(C, cetp, t_i.pt, S2, S4, route, &e[i].flag)
   e[i].ok = route non-empty; if ok: e[i].len = len(route[0]); if e[i].flag and e[i].len < best: best = e[i].len
k = -1
for pass in (flag, ok):                // stop after the first pass that chooses           // 49352-49390
   for i: if e[i].<pass> and (k == -1 or e[i].len < e[k].len): k = i
if k == -1: k = 0;  keep only targets[k]                                                // 49391-49432
```
**Mode 1 FUN_180055190(C, cetp, S1, S2, S4, targets) (49464-49795)** (P+0x239 bIsCutOrdLenBas == 0):
```
for each target i:                                                                      // 49548-49598
   e.shape = nearest(S1, t_i.pt).shape;  if e.shape < 0: continue
   o = FUN_180053c60(S1, e.shape)        // first non-island at or after it, else itself (48804-48825)
   e.P1 = nearestOn(S1[o], cur)                                                         // 49561-49563
   FUN_180050af0(C, cetp, t_i.pt, S2, S4, route, &e.flag); e.ok = route non-empty
   if ok: e.len = len(route[0]); e.P2 = nearestOn(route[0], cur)
          e.R = (|cur-P1|² <= |cur-P2|²) ? P2 : P1        // the farther of the two     // 49575-49590
k = -1
for pass in (flag, ok):                                                                 // 49605-49666
   k = argmin over entries with e.<pass> of |cur - e.R|²   (strict <, first wins)
   if k != -1: k = argmin over entries with e.<pass> and e.shape == e[k].shape of e.len (strict <); stop
if k == -1:                                                                             // 49667-49715
   k = argmin over all entries of |cur - e.P1|²;  then k = the first index with e.shape == e[k].shape
   if still -1: k = 0
keep only targets[k]                                                                    // 49716-49757
```
**Mode 1, length based FUN_180055c90 (49797-50071)** (P+0x239 != 0), differences: per target, a_i = |t_i.pt −
nearestOn(S1[outer of its shape], t_i.pt)| (distance of the target from its group's outer loop, 49921-49932) and
L_i = len(route[0]) or −1.0 (49935-49942); the flag output is ignored; k = argmin over routed targets of L_i + a_i
(49944-49950), then argmin of L_i over targets with L_i > −0.9 and the same S1 shape (49965-49975); none → 0; keep
only targets[k]. No distance break and no flag pass.

### 6.10.6 Notes for §6.3 / §6.4
- §6.3.6 FUN_180052070 / FUN_1800522c0 / FUN_180052d70 descriptions are superseded by 6.10.3 / 6.10.4; the classes 1/2/4
  are now decoded (6.10.2, FUN_1801130f0 against S1).
- CTarg +0x70 (strategy (1) path of FUN_180061cd0) is written only by FUN_180059680 / FUN_18005b480 (6.10.4), i.e.
  only for the S1 shape nearest to cur and only when P+0x21f is set.
- FUN_180113300 result bits: 1 on-boundary piece, 2 inside piece, 4 outside piece (closes the §6.4 item).
- Mode 2 (FUN_180054a70) is the default for the approach target list (FUN_180075f60, P+0x260 = 0, §6.4.4) and for
  local links when FUN_18006e3f0 gives 2.

### NOT DECODED (6.10)
- qsort comparators LAB_1800074dc (ISAB list order, used by FUN_180133c70 165660 and FUN_180134270 165767) and
  LAB_180004e21 (ring order in FUN_1801349c0 165926): thunk labels with no body in the text. Hence the exact order of
  the crossing records (which record is "first" in FUN_180059680 / FUN_18005b480 / FUN_18005c980) is not decoded.
- FUN_180147ed0 (176360): entity-count rule used by secLen (FUN_180149d80); FUN_180122fb0 (156331, entity stepper).
- FUN_180146e20 (section copy) and MCCECalcContourOffset internals (used by FUN_18005a8a0).
- FUN_180144630 (band shape selection inside FUN_1801128d0, used by FUN_180112af0).
- Meaning of entity byte +0x12 bit value 2 on S1 shapes (skipped by FUN_180050410).
- FUN_180053d60's return value is taken from register flow (decompiled as void), see 6.10.4.
- Route internals (FUN_18010c040 callees) remain as listed in §6.3.

## 6.11 Geometry kernel semantics (third pass)

All line numbers refer to `decomp/mcce_all.c`. A **position** on a shape is `(ent, t)`: an entity index into the CSh
entity vector (CSh+0x18 begin / +0x20 end, 0x88-byte entries) and a parameter t in [0,1] on that entity.
Entry offsets: +0x08 type 'L'/'A'/'P', +0x09..+0x17 other header bytes (pass byte, +0x0c int, +0x12 CeFlags byte),
line start +0x18/+0x20 and end +0x28/+0x30, arc centre +0x18/+0x20, r +0x28, sa +0x30, sw +0x38, bbox +0x40 minx,
+0x48 miny, +0x50 maxx, +0x58 maxy. CSh+0x30..+0x48 = shape bbox (minx, miny, maxx, maxy). CSh+0x50 = dir/closed
(0 = open, ±1 = closed with direction; RevSh swaps 1 ↔ −1 and leaves 0, l.4282-4290). CSh+0x54 bit 0 = hole/island.
`N` = entity count of the shape in question. `next(e)` / `prev(e)` are FUN_180122fb0 (156331-156349) /
FUN_180123010 (156352-156366): ±1; past the end they wrap (to 0 / to N−1) **only if CSh+0x50 != 0**, otherwise
they return false and leave `e` unchanged.

### 6.11.0 Primitives: point, tangent, length
**pointAt(entry, t) = FUN_180021580 (23111-23153)**: 'L' S + t(E−S); 'A' c + r(cos θ, sin θ) with θ = sa + t·sw;
'P' the point; any other type (0,0). No clamping of t.

**len(sh) = FUN_180121980 (155622-155723)**: Σ over entities of 'L' |E−S|, 'A' |sw·r|, others 0 (loop unrolled ×4,
no other logic).

**secLen(sh, e1, t1, e2, t2) = FUN_180149d80 (177357-177448)** (used by FUN_180113300): t1, t2 clamped to [0,1];
e1, e2 clamped to [0, N−1]; entity count `n` from FUN_180147ed0 (below) with full = 0; walks n entities forward from
e1 with `(e+1) mod N` (no closed test); per entity: n == 1 → (t2−t1)·len; first → (1−t1)·len; last → t2·len; else
len. Returns the sum (0 for a null shape).

**Tangent "out of" (ent, t) = FUN_1801204a0(sh, ent, t, &v, tol2) (155171-155323)**:
```
v = (0,0); if sh null or N == 0: return
P = pointAt(ent, t); E = end point of entity ent ('L' +0x28; 'A' c + r·(cos,sin)(sa+sw); 'P' point; else 0)
if |P − E|² < tol2 and (CSh+0x50 != 0 or ent != N−1):  ent = (ent+1) mod N; t = 0         // 155245-155251
loop:                                                                                        // 155253-155322
    'L': v = E − S (not normalised; |v| = chord length)
    'A': w = (r cos θ, r sin θ)·|sw|, θ = sa + t·sw;  sw > 0: v = (−w.y, w.x);  sw < 0: v = (w.y, −w.x);  sw == 0: v = 0
    other: v = 0
    if wrapped_once: return
    if |v|² > tol2: return
    if CSh+0x50 == 0 and ent == N−1: return                       (v stays ~0)
    ent = (ent+1) mod N; t = 0; if ent == original ent: wrapped_once = 1
```
**Tangent "into" (ent, t) = FUN_18011ffd0(sh, ent, t, &v, tol2) (155017-155168)**: mirror image. Compares P with the
**start** point of the entity; if |P − S|² < tol2 and (closed or ent != 0) → ent = prev (wrap N−1), t = 1.0
(155088-155097). Loop (155098-155168) identical but steps backwards (ent−1, wrap to N−1, t = 1.0), stopping at
ent == 0 on an open shape (155154).
Both return the **parameter derivative**: magnitude = entity length (L chord, A r·|sw|), direction = travel
direction. Callers normalise themselves. tol2 is a *squared* tolerance (callers pass 1e-10 or 1e-14).

### 6.11.1 Section copy
**count(sh, e1, t1, e2, t2, &n, &mode, full) = FUN_180147ed0 (176360-176395)**:
```
if full and (e1 != e2 or |t1 − t2| > 1e-12): full = 0                                     // 176368-176370
if e2 < e1 or (e1 == e2 and t2 < t1) or full:                                             // 176371
    n = full ? N + 1 : e2 + 1 + (N − e1);  mode = 2 (wrap)                                 // 176374-176382
else:
    n = e2 − e1 + 1;  mode = 1 (forward, no wrap)                                         // 176387-176390
```
CSh+0x50 is **not** consulted: an open shape is wrapped exactly like a closed one.

**trimEnds(sh, &e1, &t1, &e2, &t2, &empty, &full) = FUN_1801483e0 (176504-176617)**:
```
n = count(..., full)                                                                      // 176531
if n != 1:
    rest = length of e1 from t1 to 1 ('L' |E − P(t1)|, 'A' |(sw − sw·t1)·r|)              // 176544-176553
    if type ∉ {L, A} or rest < 1e-12:  full = 0; n −= 1; t1 = 0.0; next(&e1)               // 176556-176560
    head = length of e2 from 0 to t2 ('L' |P(t2) − S|, 'A' |sw·t2·r|)                     // 176570-176578
    if type ∉ {L, A} or head < 1e-12:  full = 0; n −= 1; t2 = 1.0; prev(&e2)               // 176581-176584
else:
    piece = length of e1 between t1 and t2 ('L' |P(t2) − P(t1)|, 'A' |(t2 − t1)·sw·r|)    // 176593-176611
    if type ∉ {L, A} or piece < 1e-12:  n = 0; full = 0                                   // 176612-176614
empty = (n == 0)                                                                          // 176616
```
Note: `t1 = 0.0` / `t2 = 1.0` are written even when next()/prev() fails on an open shape (e1 = N−1 / e2 = 0); the
entity index then stays unchanged.

**FUN_180146e20(sh, e1, t1, e2, t2, out, full) (175722-176061) — forward section copy, appended to `out`**:
```
old = N(out)
t1 = clamp(t1, 0, 1); t2 = clamp(t2, 0, 1)                                                // 175826-175841
if full and (e1 != e2 or |t1 − t2| ≥ 1e-12): full = 0                                     // 175843-175851
trimEnds(sh, e1, t1, e2, t2, &empty, &full)                                               // 175853
if empty: return 0                                                                         // 175856, 176061
(n, mode) = count(sh, e1, t1, e2, t2, full)   (recomputed after trimming)                  // 175858
first = copy of entity e1; if n == 1 (one entity, t1 → t2):                               // 175885-175900
    'L': start = S + t1(E−S), end = S + t2(E−S)
    'A': sa' = sa + t1·sw, sw' = (t2 − t1)·sw
else:
    first ('L'): start = S + t1(E−S)       ('A'): sa' = sa + sw·t1, sw' = sw − sw·t1        // 175926-175936
    last = copy of entity e2 ('L'): end = S + t2(E−S)   ('A'): sw' = sw·t2                // 175938-175982
append raw entries: mode 2 → [e1 .. N) then [0 .. e2]; mode 1 → [e1 .. e2]                 // 175985-175995
overwrite appended entry 0 with `first` (header bytes +0x08..+0x17 and geometry +0x18..+0x3f), SetChnMinMax  // 176000-176023
if n > 1: overwrite the last appended entry with `last`, SetChnMinMax                      // 176024-176056
recompute out bbox (GetMinMaxViaShEnt); return N(out) − old                               // 176058-176059
```
Semantics:
- **Direction: always forward** (increasing entity index); the shape's +0x50 sign is not used.
- **End before start** (e2 < e1, or same entity with t2 < t1): the copy **wraps** through the shape's last entity to
  entity 0. This happens on open shapes too (no closed test anywhere in this path). On the same entity this gives
  N+1 entities: e1[t1..1], e1+1 .. N−1, 0 .. e1−1, e1[0..t2].
- **`full` flag**: kept only when start == end (same entity and |t1 − t2| < 1e-12). Then it copies the whole loop
  from that point round to itself (N+1 entities). Without the flag, start == end gives `n == 1` with a zero piece and
  returns 0. The flag is passed as `i == j` (a crossing to itself) by the callers at 166180 and 189697/189726; all
  other callers pass 0.
- **Degenerate pieces**: only the **first and last** pieces are tested (length < 1e-12, or not L/A). A degenerate
  first piece is dropped (start moves to next(e1) at t=0), a degenerate last piece is dropped (end moves to prev(e2)
  at t=1). If nothing is left, nothing is appended and 0 is returned. Interior zero-length entities are copied
  unchanged.
- `out`'s +0x50 is not changed; only entities and bbox. Return value = number of entities appended.

**Wrappers**
- **FUN_180147500(tol2, sh, e1, t1, e2, t2, out) (176065-176218)**: if sh and out are both non-empty: t1, t2 clamped;
  P = end point of out's last entity, Q = pointAt(sh[e1], t1); if |P − Q|² > tol2 append `'L' P → Q`
  (176158-176212). Then FUN_180146e20(..., full = 0) (176216). Returns appended count (+1 for the bridge).
- **FUN_180147a60(sh, e1, t1, e2, t2, out) (176221-176285)**: FUN_180146e20(full = 0); if it appended nothing, append
  a zero-length `'L'` at pointAt(e1, t1) (176238-176283).
- **FUN_180147d30(sh, e1, t1, e2, t2, out) (176288-176357) — "backward copy"**: tmp = FUN_180146e20(sh, e1, t1, e2,
  t2, tmp, 0) (the **forward** section from (e1,t1) to (e2,t2)); RevSh(tmp); append tmp to out (FUN_180146b10)
  (176340-176342). The result runs from (e2,t2) back to (e1,t1). Returns the forward copy's count.
- **FUN_180146c10(sh, e1, t1, e2, t2, out) (175639-175720) — chord**: appends one `'L'` from pointAt(e1, t1) to
  pointAt(e2, t2) (175667-175680) and grows out's bbox. No other entity fields are set.
- **FUN_180146b10(src, dst) (175608-175635) — append**: inserts all entities of src at the end of dst (175630),
  recomputes dst bbox. dst+0x50 unchanged.
- **FUN_180147fd0(tol2, sh) (176398-176501) — close**: if |end(last) − start(first)|² ≥ tol2, append
  `'L'` end(last) → start(first).

**State-2 step entity: FUN_18006fc10(list, posA, posB, outEnt) (63400-63418) → FUN_18006f7d0 (63184-63397)**
(pos = {int shape +0, int ent +4, double t +8, CPt2 pt +0x10}; posB.shape is not read, the shape is
list[posA.shape]):
```
memset(outEnt, 0, 0x88); best = −1.0; e = posA.ent; ta = posA.t
tb = (posA.ent == posB.ent) ? posB.t : 1.0                                                 // 63208-63214
loop:                                                                                      // 63229-63296
    if e == posB.ent: tb = posB.t
    plen = 'L' |P(tb) − P(ta)|, 'A' |(tb − ta)·sw·r|, else 0
    if plen > best: best = plen; keep (e, ta, tb)            // strict: the earliest longest piece wins
    if e == posB.ent: break
    ta = 0; tb = 1.0; e = (e+1) mod N                         // wraps regardless of CSh+0x50
outEnt = copy of entity e_best trimmed to [ta, tb]:  'L' start = S + ta(E−S), end = S + tb(E−S);
         'A' sa' = sa + sw·ta, sw' = (tb − ta)·sw                                         // 63353-63393
FUN_18006fc10: if outEnt is 'L': start = posA.pt, end = posB.pt                            // 63408-63415
```
So the state-2 step is **one entity**: the longest piece of the walked section; if that piece is a line it is
replaced by the straight chord posA.pt → posB.pt. If posA.ent == posB.ent and posB.t < posA.t there is no wrap: the
single piece is taken with tb < ta (a reversed line or a negative-fraction arc).

**Walks**: FUN_18006f1c0 (forward, 62971-63070) / FUN_18006f4f0 (backward, 63073-63155), `(flags, dist, list, &pos,
&out, &walked)`; the shape is list[pos.shape].
```
forward:  dist = max(dist, 0)                                                              // 62995
  loop (at most N+1 entity steps, 63057):
    L = entity length; rem = (1 − t)·L
    if dist < rem:  if L > 1e-12: t = clamp(t + dist/L, 0, 1);  dist = 0; done            // 63022-63035
    dist −= rem; ent++
    if ent ≥ N:  open  → ent = N−1, t = 1.0, success iff dist ≤ 1e-16, done                // 63042-63050
                 closed → ent = 0
    t = 0; if (flags & 1) or dist < 0: done (success)                                     // 63055
  loop exhausted: fail
backward: (no clamp of dist)
  loop:
    if dist < L·t and L > 1e-6: t −= dist/L; done                                          // 63120-63124
    dist −= L·t; ent−−
    if ent < 0:  open → ent = 0, t = 0, success iff dist ≤ 1e-16, done; closed → ent = N−1   // 63128-63136
    t = 1.0; if (flags & 1) or dist < 0: done (success)                                   // 63139-63140
out = {pos.shape, ent, t, pointAt(ent, t)};  walked = dist_in − dist_left
```
Note the different degenerate thresholds: forward skips the parameter update only for L ≤ 1e-12; backward skips
entities with L ≤ 1e-6 entirely. `flags & 1` = stop at the first entity boundary reached.

### 6.11.2 Intersection lists (ISAB, 0x50 bytes)
**Isect(X, Y, out) = FUN_180134270 (165674-165779)**; **FUN_180134690(sh, Y, out) (165782-165816)** is the same
call with X = a one-element list {sh} (165804-165805). It is **not** a "first crossing" routine: it returns the
whole sorted list.
```
out = []
for i over X shapes:  for j over Y shapes:                                                 // 165714-165760
    tmp = pairIsect(X[i], Y[j])                       // FUN_180133c70
    for each tmp record: +0x10 = i, +0x20 = j, +0x30 += |out|, +0x34 += |out|              // 165726-165735
    append tmp to out
qsort(out, cmp = LAB_1800074dc)                       // 165767 (comparator is a thunk: NOT DECODED)
link(out)                                             // FUN_1801349c0, 165769
```
**pairIsect = FUN_180133c70(A, B, tmp) (165530-165671)**:
```
if bbox(A) and bbox(B) do not overlap within 1e-5: return []                                // 165578-165582
for a over A entities (index ka):  if bbox(a) overlaps bbox(B) within 1e-5:                 // 165596-165603
  for b over B entities (index kb): if bbox(a) overlaps bbox(b) within 1e-5:                // 165613-165616
     k = CalcEntIntersect(1e-5, a, b, tA[2], tB[2], pt[2])                                   // 165619
     for h = k−1 .. 0:  push {pt[h], +0x14 = ka, +0x18 = tA[h], +0x24 = kb, +0x28 = tB[h],
                              +0x30 = +0x34 = −1, +0x38 = 0, rest 0}                         // 165620-165636
if tmp non-empty: qsort(tmp, LAB_1800074dc); dedupe(A, B, tmp); if still non-empty: link(tmp)   // 165655-165667
```
**ISAB layout**: +0x00/+0x08 point; **+0x10 X shape index, +0x14 X entity, +0x18 t on the X entity** (X = first
argument of Isect); **+0x20 Y shape index, +0x24 Y entity, +0x28 t on the Y entity**; +0x30 next / +0x34 previous
(below); +0x38 int class (0 from Isect; callers write 1/2/3).

**dedupe = FUN_180133b30 (165493-165527)** (per shape pair only): for i = 0, 1, …: p = (i == 0) ? n−1 : i−1
(cyclic); if p != i and dup(rec[p], rec[i]): erase rec[i] and re-test the same index.
**dup = FUN_180134800 (165819-165858)**: |Δx| ≤ 1e-10 and |Δy| ≤ 1e-10, **and** on the X side (same entity, or
FUN_18014b720(A, p, q, 1e-10), or FUN_18014b720(A, q, p, 1e-10)), **and** the same test on the Y side with B
(165829-165853). (FUN_18014b720 = forward arc length ≤ tol, §6.11.4, so the second clause means "the two positions
are within 1e-10 along the shape in either direction"; on the same entity t is not compared.) No dedupe is done
across shape pairs or after the final sort.

**link = FUN_1801349c0 (165867-165972)**:
```
idx = [0 .. n−1]; qsort_s(idx, cmp = LAB_180004e21, ctx = records)        // 165926 (comparator NOT DECODED)
walk idx in that order in groups of equal +0x20 (Y shape)                   // 165933-165944
for each position k in a group [g0, g1):                                    // 165945-165966
    rec[idx[k]].+0x30 = idx[k+1]  (k+1 == g1 → idx[g0])     // next, cyclic within the group
    rec[idx[k]].+0x34 = idx[k−1]  (k == g0  → idx[g1−1])    // previous, cyclic
```
So the list itself is in the order of comparator LAB_1800074dc, and +0x30/+0x34 give a second, cyclic order per Y
shape (the order of LAB_180004e21). Both comparators are thunks without bodies in the text.

**Order of the main list (inferred from every consumer; comparator body NOT DECODED)**: all consumers treat the list
as sorted by the **X side** (X shape, then X entity, then X t, ascending):
- FUN_180113300 (148467-148567) measures secLen(sh, start → I[0]) and secLen(sh, I[last] → end) and classifies the
  sections I[i−1] → I[i] along sh, which only partitions sh if I is in ascending X position.
- FUN_180023dd0 (24413-24496) classifies the section of the step from I[i] to I[i+1] (+0x64 = next record's
  +0x14, +0x68 = its +0x18), and for the last record to (same entity, 1.0).
- The grouping in link() by +0x20 needs the second order to be Y-shape-major, so LAB_180004e21 presumably sorts by
  (+0x20, +0x24, +0x28). Unverified.
Consequences for callers (under that order): `I[0]` = the crossing nearest the **start** of X (uturn FUN_1800216c0
cuts the lead-in arc at I[0].+0x18); `I[last]` = the crossing nearest the **end** of X (FUN_18005c980 §6.4.4 starts
the approach at the last crossing, i.e. the one closest to the target point at the end of {line, arc}).

**first_exit FUN_180023dd0 (24413-24496), exact**: returns 0 unless Y non-empty, X has exactly one shape with 1 or 2
entities (24440-24447). I = Isect(X, Y) (24449). For i = 0..n−1: from = (I[i].+0x14, I[i].+0x18); to =
(I[i+1].+0x14, I[i+1].+0x18) if i+1 < n else (I[i].+0x14, 1.0); tol = 1e-5 if i+1 < n else 1e-7 (24457-24476).
If `FUN_18014b720(X[0], from, to, tol)` is **false** (section longer than tol) and `FUN_1801130f0(X[0], from, to,
full = 0, Y, 1e-5)` is 1 or 4 → return i (24478-24488). Short sections are skipped. Returns n if none. So "first" =
the smallest list index whose following non-degenerate section of the step is on the boundary or outside Y.

### 6.11.3 Point / section classification (codes 1 = on boundary, 2 = inside, 4 = outside)
**PtInSh(pt, sh, tol) (mcce::PtInSh, 33-206)**:
```
if N == 0 or pt outside bbox(sh) grown by tol (pt.x ≤ minx−tol, ≥ maxx+tol, same for y): return 4   // 108-112
if FUN_180143460(pt, sh, tol, tmp): return 1                       // on boundary         // 117, 171
FUN_1801453d0(tmp)                                                 // post-process tmp (NOT DECODED)  // 119
ray = 'L' from pt to (maxx(sh) + 10.0, pt.y)                       // horizontal ray to +x  // 120-124
c = number of crossing runs of tmp hit by the ray (FUN_180144460)  // 126-163
return (c odd) ? 2 : 4                                             // 165-167
```
- **On-boundary test**: FUN_180143460 (173743-174030) returns 1 as soon as FUN_180144730(pt, entity, tol)
  (bbox of the entity grown by tol, then |pt − GetNearestEntPt| ≤ tol, squared compare) holds for a source entity
  (173844) or for a gap-closing chord it creates (173920). Entities lying entirely more than 1.1·tol above (or
  below) pt.y while the previous kept entity is also on that side are skipped (173826-173840, 173799-173800).
- While scanning, FUN_180143460 builds `tmp`: a copy of the chain in which every kept entity carries a band code in
  byte +0x12: 1 = below pt.y − 0.9·tol, 2 = within ±0.9·tol, 4 = above (173796-173797, 173908-173918,
  173951-173962); consecutive entities with the same side code are not repeated (173969); arcs whose y-extremes
  enter the band go through FUN_180145690 (174011, NOT DECODED); gap chords (previous end → current start) are
  inserted when they cross a band edge (173880-173918).
- **Crossing count** (PtInSh 126-163): runs of tmp between side codes (code & 5 != 0) are scanned; a run counts when
  the side code changes (below ↔ above) across it (143-147). Count test FUN_180144460 (174329-174386): a run of one
  entity → `CalcEntIntersect(1e-7, ray, entity) != 0`; a longer run → x of the end point of the run's first entity
  > pt.x (strict).
- Exact run boundaries / the effect of FUN_1801453d0 and FUN_180145690 are NOT DECODED; for a C port the observable
  contract is: 1 if within tol of the boundary, else even-odd parity of a +x ray with ±0.9·tol vertex robustness.

**Inside(list, tol, pt) = FUN_180112a00 (148133-148165)**: for shapes in list order: PtInSh == 1 → return 1;
PtInSh == 2 → return 4 if the shape is a hole (+0x54 bit 0) else 2. None → 4. The **first** containing shape
decides, so hole-before-outer ordering matters.

**InsideGrouped(list, tol, pt) = FUN_180112e80 (148298-148387)**: walks the list accumulating shapes into a temp
list; at every **non-hole** shape it calls Inside(temp) (148361): 1 or 2 → return it (148363); otherwise temp is
cleared (148364-148367). End → 4. So the list is read as groups `[holes…, outer]` (holes listed **before** their
outer shape); a point in a hole of a group gives 4 for that group and the scan continues. FUN_180112af0
(148168-148295) is the same with an extra CBands list (FUN_1801128d0, NOT DECODED).

**Section sample iterator = FUN_180148820 (176621-176703)**; state {sh +0, e1 +8, t1 +0x10, e2 +0x18, t2 +0x20,
cur +0x28, n +0x2c, k +0x30, s +0x34 (starts −1), pt +0x38} (set up at 176734-176748):
```
next():
  if s < 2: s++                                                                            // 176641-176643
  else: k++; if k ≥ n: return false; cur++; s = 0; if cur ≥ N: cur = 0  (no closed test)   // 176644-176656
  if s == 2 and k+1 ≥ n: return false          // the end sample of the last entity is skipped   // 176658-176661
  a = (k == 0) ? t1 : 0;  b = (k == n−1) ? t2 : 1                                           // 176662-176676
  f = s==0 ? 1/3 : s==1 ? 2/3 : 1.0             // 0x3fd5555555555555, 0x3fe5555555555555   // 176677-176685
  pt = pointAt(cur, a + (b − a)·f); return true                                            // 176686-176696
```
The start point (t1) is never sampled; every interior vertex is sampled (f = 1 of each non-last entity).

**sectionInShape = FUN_1801489a0(sh, e1, t1, e2, t2, full, S, tol) (176706-176760)**: n = e2+1+N−e1 if e2 < e1, or
same entity with t2 < t1, or `full`; else e2−e1+1 (176735-176742) (same wrap rule as FUN_180147ed0, but `full` is
not cleared here). For each sample: r = PtInSh(pt, S, tol); **the first r != 1 is returned** (2 or 4)
(176755-176757). All samples on the boundary → 1 (176758-176760). No sample at all → 4 (176749-176751).

**sectionInList = FUN_1801130f0(sh, e1, t1, e2, t2, full, list, tol) (148390-148438)**:
```
onB = any1 = inHole = false
for S in list (in order):
    r = sectionInShape(sh, …, S, tol)
    if r == 1: onB = any1 = true                                                          // 148414-148417
    elif r == 2:
        if S is a hole: inHole = true                                                     // 148418-148422
        else: if !inHole and !onB: return 2;  onB = inHole = false                        // 148423-148428
return any1 ? 1 : 4                                                                        // 148434-148438
```
Single codes only (1, 2 or 4), never OR-ed. Same hole-before-outer convention as FUN_180112e80.

**shapeVsList = FUN_180113300(sh, list, tol) (148467-148567) — OR-ed result**:
```
I = Isect({sh}, list)                                       // FUN_180134690, 148518
if I empty: insert (ent 0, t 0) at front; append (ent N−1, t 1.0)          // 148521-148522, 148535-148544
else:
    if secLen(sh, (0, 0.0) → I[0]) > tol:  insert (0, 0.0) at front        // 148525, 148533-148537
    if secLen(sh, I[last] → (N−1, 1.0)) > tol: append (N−1, 1.0)          // 148528-148532, 148539-148544
r = 0; for i = 1 .. n−1: r |= sectionInList(sh, I[i−1], I[i], full = 0, list, tol)        // 148545-148555
return r                                                     // bit 1 = touches/along boundary, 2 = inside, 4 = outside
```
Note: sh is cut as an **open** chain (start → first crossing … last crossing → end); for a closed sh the wrap section
is split in two, it is never classified as one piece. With fewer than 2 records nothing is classified and 0 is
returned (cannot happen: the empty case adds 2 records). Callers test `& 4 == 0` ("no part outside") or `& 5 == 0`.

### 6.11.4 Short-section test FUN_18014b720(sh, e1, t1, e2, t2, tol) (178227-178372)
Returns **true iff the forward arc length along sh from (e1,t1) to (e2,t2) is ≤ tol**.
```
t1, t2 clamped to [0,1]                                                                   // 178250-178268
if e1 == e2 and t2 ≥ t1:  return piece(e1, t1 → t2) ≤ tol                                 // 178338-178366
acc = rest of e1 from t1 to 1 ('L' |E − P(t1)|, 'A' |(sw − sw·t1)·r|, else 0)             // 178272-178291
if acc > tol: return false                                                                 // 178292
next(&e)                                                                                   // 178293 (result ignored)
loop:                                                                                      // 178294-178335
    if e == e2: return acc + head(e2, 0 → t2) ≤ tol          // 'L' |P(t2) − S|, 'A' |sw·t2·r|, else 0
    acc += len(e); continue while acc ≤ tol and next(&e) succeeds
return false
```
"Forward" wraps through entity N−1 → 0 only on closed shapes (next()). Used by first_exit (tol 1e-5 between crossings,
1e-7 for the tail; true = skip as degenerate) and by the ISAB dedupe (tol 1e-10, both directions).

### 6.11.5 Last output point / end tangent of the CCetp ring
CCetp: T+0x1c = record count, ring of 16 records of 0xc0 bytes at T+0x40, record entity at rec+0x08 (so entity type
at rec+0x10, geometry at rec+0x20..), T+8 = CCePock, T+0x10 = file object (§3 C.5). Last slot =
(count − 1) mod 16.

**FUN_18008e4b0(T, &pt) (78740-78836)** — end point:
```
if count == 0:                                                                            // 78765
    e = C+0x120 (previous-position entity, §6.4.3; type byte C+0x128)
    if type == 0: pt = (0,0); return 0                                                     // 78767-78771
    pt = end point of e: 'L' (C+0x148, C+0x150); 'A' c + r·(cos,sin)(sa+sw); 'P' (C+0x138, C+0x140); other (0,0)
    return 1                                                                               // 78772-78800
else:
    rec = ring[(count−1) mod 16]; pt = end point of rec's entity ('L' rec+0x30/+0x38; 'A' centre rec+0x20/+0x28,
    r rec+0x30, sa rec+0x38, sw rec+0x40 → c + r·(cos,sin)(sa+sw); other: rec+0x20/+0x28); return 1   // 78802-78835
```
**FUN_18008e710(T, &v) (78839-79019)** — end tangent (unnormalised derivative, same convention as FUN_1801204a0):
```
eps = 9.999999999999998e-15
if count != 0:
    v = derivative at the end of the last record's entity: 'L' E − S; 'A' w = r(cos,sin)(sa+sw)·|sw| rotated +90°
        (sw > 0) or −90° (sw < 0); sw == 0 or other type → (0,0)                          // 78952-78997
    if |v|² > eps: return 1                                                               // 78998, 79008
    sh = FUN_18008e1f0(T)  = one open CSh of the ring records in time order (oldest = slot 0 if count < 16,
                             else slot (last+1) mod 16, up to the newest)                  // 79000; body 78632-78737
    v = FUN_18011ffd0(sh, N−1, 1.0, eps)   (walk back over zero-length records)           // 79002-79003
    if |v|² > eps: return 1, else goto fallback                                           // 79004-79008
fallback (count == 0 or both failed):                                                      // 78875-78899
    if count > 1 and file: reread the records from the file (FUN_18008d020, internals NOT DECODED) into a shape list;  // 78877-78890
       if non-empty: v = FUN_18011ffd0(list[0], last, 1.0, eps); if |v|² > eps: return 1                    // 78891-78898
    if count == 0 and C+0x128 != 0: v = derivative at the end of C+0x120 (same L/A rules); if |v|² > eps: return 1
                                                                                           // 78900-78947
v = (0,0); return 0                                                                       // 78948-78950
```
"None" = return 0 with (0,0): for the point only when count == 0 and C+0x120 has type 0; for the tangent whenever no
non-degenerate tangent was found. Neither function normalises.
Accessors: FUN_18008edd0 (79046-79062) = &rec+8 of the last record or 0; FUN_18008ee10 (79065-79080) = &rec;
FUN_18008ed60 (79022-79043) = rec+0xa0 / rec+0xa8 (w1, w2) of the last record, 0/0 and false when count == 0.

### 6.11.6 Nearest / farthest point routines
| function | input | result | selection / tie rule |
|---|---|---|---|
| **FUN_18013d200(pt, sh, &ent, &t, &pt)** (170449-170474) → FUN_18013d280 (170481-170586) | one shape | entity, t, point | per entity: bbox distance² lower bound; GetNearestEntPt only if first or bound ≤ best; accept if first or dist² **≤** best → ties go to the **later** entity (170559-170571). Empty shape → ent −1, t 0, pt (0,0). Record written as {0, ent, t, pt} (170580-170583). |
| **FUN_18013d010(pt, list, &shape, &ent, &t, &pt)** (170381-170444) | shape list | shape, entity, t, point | FUN_18013d200 per shape; accept if first shape or dist² **<** best → ties go to the **earlier** shape (170417). Init best 3.4028234663852886e38 (170402); empty list → shape −1, ent −1. An **empty shape in the list** yields pt (0,0) and is compared like any other. |
| **FUN_18013cfe0(pt, list, rec)** (170368-170376) | shape list | rec {int shape +0, int ent +4, double t +8, CPt2 +0x10} | = FUN_18013d010 writing into one record |
| **FUN_18013caf0(pt, sh, &ent, &t, &pt)** (170150-170226) | one shape | entity, t, point | **FARTHEST point**: per entity FUN_1800acff0 (93945ff) = farthest point ('L' FUN_1800ad050: the farther end point, t 0 or 1; 'A' FUN_1800acd50: farthest point of the arc, see below; 'P' the point); keep if dist² **>** best (init −1.0) → ties go to the **earlier** entity (170203). |
| FUN_18013c8f0(pt, list, …) (170080-170147) | shape list | shape, ent, t, pt | farthest over shapes: accept if first or dist² > best |
| FUN_18013ccc0(tol2, pt, sh, startEnt, rec, &d2) (170229-170365) | one shape | rec + dist² | nearest, scanning entities cyclically from startEnt; ties to the later entity; **stops early** when dist² < tol2 (170344) |

Entity kernels: **GetNearestEntPt** (8319-8360): 'L' FUN_1800ad500 — projection parameter clamped to [0,1]; if
|E−S|² < 1e-14 the midpoint with t = 0.5. 'A' GetNearestArcPt (6061-6118): φ = atan2(P−c) − sa normalised to
[0, 2π] (to (−2π, 0] when sw < 0); if |φ| > |sw|: φ' = sw if |φ| ≤ |sw/2 ± π| (nearer the end) else 0 (start);
P within 1.1920928955078125e-07 of the centre (both coordinates) → φ' = sw/2; t = φ'/sw (0 if |sw| ≤ 1e-12).
FUN_1800acd50 (farthest on arc) is the same code with atan2(c − P) (the antipode direction).

### 6.11.7 Corrections to earlier sections found in this pass
1. **FUN_18013caf0 returns the farthest point, not the nearest** (170200-170203, FUN_1800acff0 → FUN_1800ad050 /
   FUN_1800acd50). Consequences:
   - §3 D 2.2 / FUN_1800686c0 (59695-59720): in modes 1/2 it picks the remaining piece (C+0x420) whose **farthest**
     point from the last output point is smallest (min-max distance), not "the piece nearest to the current point".
   - §6.2.2 FUN_1800186a0 (18758-18761): `A` = the **farthest** point of the small piece C+0x420[pos.shape] from B.
2. FUN_180134690 is Isect of one shape against a list (full sorted list), not a first-crossing routine (165782-165816).
   "I[0]" / "last ISAB" in callers are the crossings nearest the start / end of that shape (order inferred, §6.11.2).
3. §1 "FUN_1801349c0 NOT DECODED" → decoded (§6.11.2 link): +0x30/+0x34 = cyclic next/prev in the second (Y-side)
   order within the same Y shape. Per shape pair, records are additionally deduplicated (FUN_180133b30).
4. §3 C.4 "E.entity = path section pos..npos (FUN_18006fc10)": the step is **one entity** — the longest piece of the
   walked section; a line becomes the chord pos.pt → npos.pt (§6.11.1).
5. FUN_180146e20 wraps on open shapes too and is always forward; "backward copy" FUN_180147d30 = reversed forward
   section (§6.11.1).

### NOT DECODED (6.11)
- (closed, disassembly; viewer/mcce_dyn.c isab_cmp_x / isab_cmp_y already follow it) **LAB_1800074dc** = `jmp
  FUN_180133630`: ascending (+0x10 X shape, +0x14 X entity, +0x18 X t); **LAB_180004e21** = `jmp FUN_180133680`
  (qsort_s, context = records): ascending (+0x20 Y shape, +0x24 Y entity, +0x28 Y t) - the inferred orders, confirmed.
- PtInSh internals: FUN_1801453d0 (174909, tmp post-processing), FUN_180145690 (174995, arc split at the band), the
  exact run boundaries of the crossing count (PtInSh 126-163). Top-level contract decoded.
- CalcEntIntersect (number/order of solutions, tangency handling at tol 1e-5 / 1e-7).
- FUN_18008d020 (78173, reread of CCetp records from the file) used by the FUN_18008e710 fallback.
- FUN_1801128d0 (CBands variant used by FUN_180112af0).

## 6.12 Entry removal, attach tests, link bookkeeping and routes (third pass)

Line numbers are in `decomp/mcce_all.c`. C+x = CCePock offset, P+x = CDynPockParm offset, C+x = P+(x−8).
RG = C+0x298. "cur" = end point of the last emitted record (FUN_18008e4b0). Positions are
`{int shape; int ent; double t; CPt2 pt}` (0x20 bytes) as in §3 D. ISAB/ISEC records are 0x50 bytes (§6.1.0, §1 helpers).

### 6.12.1 Material removal along the entry path — FUN_180070b60 (63904-64209)
Called once by the plunge entry FUN_18006fc70 (63530) with E = entryShapes[0] (after FUN_18007c700 extended it when
len(E) < C+0x1e8, 63525-63529), before the entry is emitted (FUN_1800707b0, 63531).
```
E_list = [E]                         // local CVe<CSh*> local_3e8; E pushed at 64001-64022 (this is the "append" at ~64024;
                                     // nothing is appended to C+0x410)
FUN_180070280(flags, R−r, E_list, C+0x410)                                  // 64023-64024
if C+0x438 non-empty (begin C+0x448 != end C+0x450):                        // 64025
    FUN_180070280(flags, R,   E_list, C+0x438)                              // 64027-64028
pos  = {shape 0, ent 0, t 0.0, pt = start point of E.ent[0]}                // 64030-64044
dir  = FUN_18006f7a0(E)              // computed once: (+0x54&1) ? (+0x50==-1 ? -1 : 1) : (+0x50==-1 ? 1 : -1)  // 64045, 63158-63181
last = false
do:
    progress FUN_180114140(C+0x270, C+0x700, 0)                             // 64047
    ok = FUN_18006f1c0(mode 1, d = C+0x1e8 (P+0x1e0 march step), E_list, &pos, &npos, NULL)   // 64048-64049
    if !ok:                                                                  // 64050
        last = true
        if E+0x50 != 0 (closed): break          // leaves the loop WITHOUT measuring/committing this step  // 64052
        npos = {shape 0, ent nEnt−1, t 1.0, pt = end point of last entity ('L' +0x28/+0x30, 'A' c+r·e(sa+sw),
                'P' +0x18/+0x20, else (0,0))}                               // 64053-64097
    if npos.ent < pos.ent or (npos.ent == pos.ent and npos.t < pos.t): last = true   // wrap; 64104-64109
    clear M1, M2 (FUN_18001cfe0 on E'+8, E'+0x140: also zeroes flags E'+0x138 and pass byte E'+0x13c, 21080-21087)  // 64110-64111
    E'.entity = section(E_list, pos → npos)  (FUN_18006fc10; a line is set exactly pos.pt → npos.pt)  // 64112
    measure FUN_180030be0(RG, cetp, posOut = NULL, dir, E')               // 64113 (§3 C.1; M2 only if C+0x438 non-empty)
    if (E'.flags & 0x18) == 0: commit FUN_180035990(RG, cetp, E', NULL)    // 64114-64116 (§3 C.4 / §6.1.7)
    pos = npos                                                              // 64117-64120
while !last
UpdDirShAr(0.001, C+0x410); DetNestLvlShAr(C+0x410); FUN_18002c0e0(flags, 4, C+0x410)       // 64122-64124
```
- E' is the local CEnCut `local_318` (constructed FUN_1800155a0, 64000), same layout as E in §3 C.0.
- The commit FUN_180035990 (33361-33389) subtracts the step from RG+0x178 (= C+0x410) and, when RG+0x1b0 (= C+0x448)
  is non-empty and M2 flags&0x18 == 0, from RG+0x1a0 (= C+0x438). Its flag side effects (0x80, 0x40 if w1 > 1e-6,
  0x400/0x200) are on E' only; **nothing is emitted** here (no FUN_1800870f0 call in 63904-64209). The commit's
  FUN_180032a20 gives new material loops CSh+0x58 = current record count (§6.1.7), i.e. the count before the entry.
- Only C+0x410 is re-oriented at the end; C+0x438 is not (64122-64124 name only C+0x410).
- No local-window rebuild (FUN_18002a140) is done in this function; FUN_180030de0 uses RG+0x1c8 when it is non-empty
  (30695-30701). FUN_18002a2a0 (27305-27665) does not reference RG+0x1c8/+0x1d8, so whether the window is empty at
  entry time is **NOT DECODED** (see list).

**FUN_180070280(flags, rad, list, mat) (63654-63800)** — "disk at the entry start removed from the material":
```
D = new CSh with one entity 'A': centre = start point of list[0].ent[0] (L: +0x18/+0x20; A: c + r·e(sa)),
    r = rad, sa = 0, sw = 2π (0x401921fb54442d18); D+0x50 = 1, D+0x54 = 1                // 63738-63761
UpdMinMaxShAr([D]); if (flags & 1) == 0: RevSh(D); D+0x54 = 0                            // 63762-63766
out = FUN_180153b30(1e-6, mat, [D])                                                      // 63767
FUN_18002c0e0(flags, 4, out)                                                             // 63768
swap(mat, out)   (mat's vector replaced by out; old content freed with out)               // 63769-63790
```
FUN_180153b30 is the boolean of §3 B.3 (internals NOT DECODED; read there as X − Y, i.e. mat minus the disk).

**FUN_18002c0e0(flags, kind, list) (28316-28361)**, exact: proceeds only if `kind & 6` is 2 or 4. key = (kind&6)<<16
| flags&1. key 0x20000 or 0x40001: s_noIsland = −1, s_island = +1; key 0x20001 or 0x40000: s_noIsland = +1,
s_island = −1 (28328-28340). For every shape: wanted = (+0x54 & 1) ? s_island : s_noIsland; RevSh if +0x50 ≠ wanted.
So kind 4 with climb (flags&1 = 1) gives outer (+0x54 bit 0 clear) −1 and island +1; without climb the reverse.

**FUN_18007c700(C, list) (70053-70141)** (short entry extension; §3 D 1.4 restated exactly):
```
if list empty or list[0] has no entity: return 0                                         // 70083-70089, 70139
Q = end point of last entity of list[0] ('L' +0x28/+0x30; 'A' c + r·e(sa+sw); else +0x18/+0x20)   // 70090-70111
s = C+0x1e8 (P+0x1e0);  sweep = (flags & 1) ? +2π : −2π                                  // 70112-70117
if FUN_180110120(Q, s, C+0x340, 1e-5) & 5: return 0                                      // 70118-70121
   // FUN_180110120 (146741-146826) = FUN_180113300(full circle (Q, s, sa 0, sw 2π, +0x50 = 1), C+0x340, 1e-5)
T = tangent of list[0] at (last entity, t = 1.0) (FUN_18011ffd0, tol 1e-16)              // 70123-70125
a0 = (|T.x| > 1e-12 or |T.y| > 1e-12) ? atan2(T.y, T.x) : 0                              // 70126-70131
FUN_1800a6aa0(1e-8, Q, 0.0, s, a0, sweep, &list[0]+8)   // appends the spiral (§6.7.8) to list[0]   // 70132-70133
FUN_18013c390(list)  (bbox of every shape, GetMinMaxViaShEnt; 169845-169866); return 1  // 70134-70135
```

### 6.12.2 Post-entry attach point — FUN_18005bca0 (52997-53061), exact
Called by FUN_18005bee0 (53093) with `loops` = the third list of FUN_18006fc70. That list is cleared at 63481 and filled
only by Helix (FUN_180078840 → FUN_1800788d0 param_4: the 4-arc helix loop, 68150-68186) and Profile
(FUN_18007cb70 param_3: the offset loop, 70186-70208); for the other methods it stays empty.
```
cur = FUN_18008e4b0(cetp)
if |loops| == 1:                                                                          // 53027
    found = false
    for i over C+0x410 (begin C+0x420):                                                   // 53028-53053
        if FUN_180113300(mat[i], loops, 1e-5) == 2:          // piece entirely inside the loop   // 53031-53033
            (ent, t, q) = nearest point of mat[i] to cur (FUN_18013d200)                   // 53034-53035
            if !found or |cur − q|² < |cur − out.pt|²: out = {i, ent, t, q}; found = true   // 53037-53047
    if found: return out                                                                  // 53054-53056
out = nearest point of the whole list C+0x410 to cur (FUN_18013d010; shape −1 when none) // 53058-53059
```
FUN_18005bee0 then uses `out` as in §3 D 1.9 (confirmed 53064-53158: shape == −1 → G.valid = 0, lead-in cleared,
G.pos = out, state 0; | |out.pt − cur| − (R−r) | > 1e-5 → general link; else G.valid = (nearest C+0x340 point within
1e-7 of cur), G.pos = that nearest point, state = valid ? 2 : (FUN_180053e80(mat[out.shape], 35.0) ? 1 : 0)).

### 6.12.3 Lead-in material test FUN_1800247d0 (24638-24724)
Returns 1 = **reject the attach** (callers: offset_attach 23489-23492 fails; uturn 23276-23318 clears G).
```
X2 = FUN_18001ff60(C, G) ; if it fails: return 1                                          // 24677, 24689
I1 = Isect(X2, C+0x3e8)        (FUN_180134270)                                            // 24679
I2 = Isect(X2, C+0x410)                                                                   // 24680
FUN_180023100(I1, I2)          // drop I2 records that coincide with I1 records            // 24681
mark(C, X2, I2)                // FUN_1800244a0, §6.2.5                                    // 24682
return FUN_180024a00(C, X2, I2) ? 1 : 0                                                   // 24683-24689
```
**FUN_18001ff60(C, G, out) (22510-22709)** — annular sector outside the lead-in arc:
```
fail unless G+8 (valid) and the lead-in (G+0x28..+0x30) has exactly 1 entity, type 'A', and |r·sw| ≥ 1e-7  // 22537-22545
   (the test is "fail if |r·sw| < 1e-7")
out = [one new CSh with 4 entity slots]                                                   // 22546-22581
e0 = copy of the lead-in arc (+0x08..+0x3f); e0.r = r + ((R − r) − 1e-4)                  // 22582-22598
P0 = end(e0) = c + e0.r·e(sa+sw)                                                          // 22599-22617
if |P0.x − c.x| ≤ 1e-12 and |P0.y − c.y| ≤ 1e-12: fail                                   // 22618-22622
e2 = copy of the lead-in arc (radius r); e2.sa = atan2(P0 − c); e2.sw = −sw                // 22623-22637
e1 = 'L' end(e0) → start(e2) = c + r·e(e2.sa)                                             // 22638-22658
e3 = 'L' end(e2) = c + r·e(e2.sa + e2.sw) → start(e0) = c + e0.r·e(sa)                     // 22659-22698
UpdMinMaxSh; out+0x50 = (flags & 1) ? 1 : −1; return 1                                     // 22700-22706
```
So X2 is the band between radius r and r + (R−r) − 1e-4 over the lead-in's angular span (outer arc forward, inner arc
backward), the same {A,L,A,L} form as half_annulus (§6.2.5) so that FUN_1800236f0 accepts it.

**FUN_180023100(I1, I2) (24029-24135)**:
```
for each a in I1 (in order):
    k = argmin over I2 of |I2[k].pt − I1[a].pt|² (start value 3.4028234663852886e38, strict <, first minimum)  // 24060-24081
    if k exists and d² ≤ 1e-6 and (d² ≤ 1e-12 or I1[a].+0x14 == I2[k].+0x14 (same X2 entity)):              // 24082-24085
        for every j ≠ k:                                                                  // 24086-24124
            for f in (+0x34, +0x30)  (order: +0x34 first):
                if I2[j].f == k: I2[j].f = I2[k].f
                if I2[j].f == k: I2[j].f = j
                if I2[j].f > k:  I2[j].f −= 1
        erase I2[k] (FUN_180022fe0(I2, k, 1): erase `count` records at index, 24000-24027)  // 24126
```
**FUN_180024a00(C, X2, I2) (24727-24804)**:
```
n = |I2|; if n == 0: return false                                                         // 24745-24749
i = n−1; while I2[i].class != 3: i = (i+1) mod n; if i == n−1: return false              // 24750-24761
k = i;   while I2[k].class == 3: k = (k+1) mod n; if k == i: return false                // 24762-24776
if I2[k].+0x14 == 0 (on X2 entity 0 = outer arc)                                          // 24779
   and |I2[k].pt − end(X2.e0)|² ≥ 1.0000000000000002e-10:                                 // 24780-24798
       return !FUN_1800236f0(C, X2, t0 = I2[k].+0x18, t1 = 1.0)                           // 24799-24800
return false                                                                              // 24803
```
FUN_1800236f0 is the sampled-circle test of §6.2.5 (24238-24408): it needs X2 = {A,L,A,L}, t0 ≤ t1,
|e2.sw + e0.sw| ≤ 1e-7 and |e2.r − C+0x48| ≤ 1e-7 (24285-24292; C+0x48 = P+0x40), else returns 0. So the attach is
rejected when, after the first point where the material leaves the band through the outer arc, some circle of radius
R−r centred on the inner arc between that point and the arc end misses the material, or when the band's
preconditions fail.

### 6.12.4 Link bookkeeping: C+0x710..0x728 memo and the C+0x730 point list
**C+0x730 is one `CVe<CPt2>`** (vftable at C+0x730, begin **C+0x740**, end C+0x748, cap C+0x750). "Push to list
C+0x740" in §3 D 2.2 = FUN_180039db0(&C+0x740, &C+0x718) (16-byte push_back; calls 58088 / 59482; body 35466-35494): the same list C+0x730 that the target builders receive.
- Cleared: at every region start `FUN_180011d50(C+0x730)` (62667; clear of the CVe<CPt2>, 15340ff) and in
  FUN_1800122f0 (15545). Memo reset: 62663-62666, FUN_180011a00 15229-15231, FUN_180065ff0 58412-58415.
- (2026-11-07) The output trim FUN_18008ee40 (79086-79099, helpers FUN_18008eeb0 79102ff / FUN_18008efe0 79152ff) never
  writes cetp+0x20: after a trim the last-cut index can point past the end (ported in the exact model).
- **FUN_1800653b0(C, cetp) (57933-57946)** = `C+0x710 != 0 && C+0x728 == cetp+0x20`: the previous link set a target
  and no record with move flag 0x40 (committed cut with w1 > 1e-6, FUN_180035990 33369-33371; cetp+0x20 written in
  FUN_1800870f0 75307-75309) has been emitted since.
- **FUN_180053cd0(_, pt, list) (48828-48848)** = true iff some point of `list` is within squared distance
  1.0000000000000002e-10 of pt (first argument unused). Used by FUN_1800522c0 (48196) with list = C+0x730 and pt =
  the run-in start A.pt of a candidate boundary target: such a target is not added.
- Other receivers of C+0x730: FUN_180052a90 (general link 58094-58096, approach targets FUN_180075f60 66844-66847),
  FUN_180066ad0 (local link 59503-59505); the only reader found is FUN_180053cd0.
So: **a boundary target whose link produced no cutting before the next link is blacklisted for the rest of the
region** (exact point match, 1e-5).

**General link FUN_1800653d0 (57949-58207), order:**
```
idx = −1; cross = 0.0                                                    // 58043, local_210
if matPos: matPos = {−1, −1, 0, 0, 0...}                                // 58069-58078
G.valid = 0; G lead-in cleared; *state = 3                              // 58079-58085
if FUN_1800653b0(C, cetp): push C+0x718 (memo target point) onto C+0x730     // 58086-58089
(S1, S2, S3, S4) = FUN_180062a70(C)                                     // 58090-58092
targets = FUN_180052a90(C, C+0x340, C+0x368, S1, S2, S4, cetp, C+0x730, FUN_18006e3f0(C))   // 58093-58096
ok = FUN_180061cd0(C, cetp, targets, C+0x340, S2, S3, S4, allowLast 1, &link, &idx, &cross)  // 58097-58100
if ok and !FUN_180064180(C, link, cross): goto emit                      // 58101-58103
if ok: link cleared (entity vector end = begin), idx = −1, cross = 0     // 58104-58110
FUN_180062140(C, cetp, targets, C+0x340, S2, S4, &link, &idx)  (retract link, §3 D 2.5)    // 58112-58114
emit: mf = FUN_18008a1e0(cetp, (flags>>2)&1, link); FUN_180087390(cetp, link, mf)          // 58116-58118
FUN_180065ff0(C, cetp, targets, idx, matPos, G, state)                  // 58121
```
**Local link FUN_180067d80** has the same head (59470-59483: G cleared, state 3, memo push) and, after a successful
emit (59529-59531), a target remap before FUN_180065ff0 (59532-59556):
```
if idx ≥ 0 and idx < |targets| and targets[idx]+0x22:
    q = nearest point of C+0x340 to targets[idx].B.pt (+0x60) (FUN_18013cfe0 → {shape, ent, t, pt})
    if |q.pt − B.pt|² ≥ 1.0000000000000002e-10: targets[idx]+0x22 = 0     // → next state from +0x20 (0/1)
    else: targets[idx].B (+0x50..+0x6f) = q        // window shape/entity indices rebased to C+0x340
```
(the window W of the local link is a separate shape list, so its positions are re-expressed on C+0x340.)
If the local link fails, nothing resets the memo, so the general link that follows (FUN_180067ce0 59300-59314) pushes
the same point again (harmless for FUN_180053cd0).

**FUN_180065ff0(C, cetp, targets, idx, matPos, G, state) (58378-58456)**, exact:
```
if matPos: matPos = nearest point of C+0x410 to cur (FUN_18013d010)                          // 58397-58401
if idx < 0 or idx ≥ |targets|:                                                                  // 58402-58416
    G.valid = 0; G lead-in cleared; state = 0; C+0x710 = 0; C+0x718 = C+0x720 = 0; C+0x728 = −1
else:
    t = targets[idx]
    if t+0x22 == 0: G.valid = 0; lead-in cleared; state = (t+0x20 != 0) ? 1 : 0                 // 58419-58428
    else:           G.valid = 1; lead-in cleared; G.pos (+0xa8..+0xc7) = t+0x50..+0x6f; state = 2   // 58429-58446
    C+0x710 = 1; C+0x718/0x720 = t+0x00 (target point); C+0x728 = cetp+0x20                     // 58448-58453
```

### 6.12.5 Output helpers (CCetp)
**FUN_1800870f0(cetp, ent, ce, mf, v0, v1, v2, v3) (75241-75318)**, confirmed:
```
slot = count mod 16 (C remainder of cetp+0x1c, made non-negative)                     // 75253-75256
R = cetp+0x40 + slot·0xc0;  R+0x08..+0x8f = *ent (0x88 bytes)                         // 75257-75291
R+0x1a (= ent+0x12 CeFlags) = ce                                                      // 75292
R+0x80 (= ent+0x78 z) += *(cetp+8)+0x88   (C+0x88 = P+0x80, cut level)                // 75293-75295
if R+0x10 == 'A': FUN_1800874e0(R+8)                                                   // 75296-75298
R+0x90 = mf; R+0xa0..+0xb8 = v0..v3                                                    // 75299-75303
R+0x98 = feed = FUN_18008a0a0(cetp, R+8, mf)                                           // 75304-75306
if mf & 0x40: cetp+0x20 = count                                                        // 75307-75309
count++                                                                                // 75310
pos = file->vtbl[0x28]()  (position before the write)                                  // 75311
FUN_18008de70(file, R+8, R+0x90, R+0x98)  (write)                                      // 75313-75314
FUN_18008aa50(cetp, R+8, mf, pos)  (first-pass bookkeeping)                            // 75315-75316
```
- **FUN_1800874e0 (75357-75420)** exact: S = c + r·e(sa); if |sw| ≤ π: Q = c + r·e(sa+sw), else Q = c + r·e(sa+π)
  (the point opposite S, 75378-75402); if |S − Q| < 1e-4: the record becomes **'L' S → Q** (SetChnMinMax).
  Correction to §3 D 4.2: for |sw| > π the chord tested is the diameter 2r, and a converted line ends at the opposite
  point, not at the arc end.
- **FUN_18008a0a0** (76717-76741): feed = *(cetp+8)+{0xb8, 0xc0, 0xc8, 0xd8, 0xe0, 0xd0} for classes 1..6 of
  FUN_18008a190 (76744-76765) — i.e. P+{0xb0, 0xb8, 0xc0, 0xd0, 0xd8, 0xc8}; confirms §3 D 4.3.
- **FUN_180087390(cetp, sh, mf) (75321-75354)**: for i: FUN_1800870f0(cetp, &sh.ent[i], sh.ent[i]+0x12, mf[i],
  0, 0, 0, 0).
- **FUN_18008a1e0(cetp, split, sh, mf) (76768-76841)**, exact:
```
mf.clear(); if sh == NULL or sh has no entity: return                                 // 76783-76796
mf.resize(n) (FUN_18008b530: new elements = 0)                                        // 76797-76801
k = 0
if split: k = first i with ent[i].CeFlags & 2; if none (k ≥ n): k = (int)(n · 0.5)    // 76802-76822
for i in 0..n−1: climb = *(cetp+8)+0x10 & 1 (C flags bit 0); if i < k: climb = !climb  // 76824-76832
    FUN_18008a4a0(cetp, climb, &ent[i], &mf[i])                                       // 76833
FUN_18008a4a0 (76846-76868): if CeFlags has none of 1, 2, 8, 0x20 and mf has none of 0x400, 0x200
    and !(mf & 0xc0) (FUN_18008a090): mf |= climb ? 0x800 : 0x1000
```
  Since mf starts at 0, the effective rule is: mf[i] = (CeFlags & 0x2b) ? 0 : (climb_i ? 0x800 : 0x1000).
- **FUN_18008edd0(cetp) (79046-79062)**: count−1 < 0 → NULL; else `cetp+0x40 + 8 + ((count−1) mod 16)·0xc0` = the
  pock_chn of the last emitted record. **FUN_18008ee10 (79065-79081)**: same but without +8 = the last ring record
  base (its +0xa0/+0xa8 = v0/v1 = widths, used by the limits §3 C.2).
- **FUN_18008ee40(cetp, n) (79084-79099)** — truncate the output to n records:
```
if n < 0 or n ≥ count: return
removed = count − n
FUN_18008eeb0(cetp, n): scan the 32-byte list cetp+0x68 from the end; erase every record whose int +0x18 ≥ n,
    stop at the first one < n                                                           // 79102-79149
FUN_18008d830(file, removed): step back `removed` records: each step reads the u64 stored in the 8 bytes before
    the current position (the previous record's start offset) and seeks there (CetpFile.cpp checks) // FUN_18008d830 78401-78475
count = n
FUN_18008efe0(cetp, removed): save position; u = max(count − 16, 0); step back (count − u) records; re-read
    min(removed, 16) records into ring slots u, u+1, … (FUN_18008ca20); seek back to the saved position   // 79152-79194
```
  cetp+0x20 (last 0x40 record index) is not touched by the trim. Only caller: state 2 FUN_18006d330 (62133-62135).

### 6.12.6 Route builder internals — FUN_18010c040(both, set, line, out) (144757-144853)
```
L = copy of line (FUN_18011f2e0)                                                         // 144793
isab = Isect([L], set) (FUN_180134270)                                                   // 144794
if isab empty:
    if set non-empty and FUN_1801489a0(L, ent 0, t 0, ent n−1, t 1, single 0, last shape of set, 1e-5) != 2: goto general   // 144797-144806
    every entity of L: byte +0x13 |= 4; append copy of L to out; return                  // 144808-144823
general:
    FUN_180109960(L, isab)        // end nodes                                           // 144826
    FUN_18010ad50(set, L, isab)   // section classes (§6.3.0)                            // 144827
    FUN_180109e20(set, L, isab)   // snap outside ends to the set                         // 144828
    FUN_18010a960(both, set, L, isab, out)                                               // 144829
```
**FUN_180109960(L, isab) (143552-143692)** — add line ends as nodes:
```
S = start of L.ent[0]
if isab empty or |S − isab[0].pt|² > 1.0000000000000002e-10:                              // 143620-143622
    insert at front {pt S, X (shape 0, ent 0, t 0.0), Y (−1, −1, t 0), +0x30/+0x34 = −1, class 0, +0x40.. = 0}  // 143623-143634
    for records 1..n−1: +0x30 += 1, +0x34 += 1                                            // 143635-143645
E = end of last entity of L
if |E − isab[last].pt|² > 1e-10 or |isab| == 1:                                          // 143676-143677
    append {pt E, X (0, nEnt−1, t 1.0), Y (−1, −1, 0), +0x30/+0x34 = −1, class 0}          // 143678-143690
```
**FUN_180109e20(set, L, isab) (143696-143920)** — runs after the classes are set:
```
added = false
if isab[0].class != 1 and isab[0].+0x20 (Y shape) == −1:            // start is an added end node off the set  // 143744-143745
    Q = nearest point of set to start(L) (FUN_18013d010 → shape, ent, t, pt)          // 143783
    if |isab[1].pt − Q|² ≥ 1e-10 or |isab| < 3:                                         // 143786-143788
        isab[0] = {pt Q, X (0, 0, 0.0), Y (Q.shape, Q.ent, Q.t), class 3}               // 143789-143799
        records 1..n−1: X ent (+0x14) += 1                                             // 143800-143808
        insert 'L' Q → start(L) at the front of L; bbox                                // 143809-143820
        added = true
    else: erase isab[0]; every +0x30/+0x34 ≠ −1 is decremented                          // 143823-143841
if isab[n−2].class != 1 and isab[n−1].+0x20 == −1:                                      // 143847-143848
    Q = nearest point of set to end(L)                                                  // 143881
    if |isab[n−2].pt − Q|² ≥ 1e-10 or |isab| < 3:                                       // 143884-143888
        isab[n−1] = {pt Q, X (0, old nEnt, 1.0), Y (Q.shape, Q.ent, Q.t), class 1}       // 143889-143899
        append 'L' end(L) → Q to L; bbox                                               // 143900-143909
        added = true
    else: erase isab[n−1]                                                               // 143912
if added: FUN_18010bd00(isab)                                                           // 143916-143918
```
So when an end of the line lies outside the set, **the route starts/ends at the nearest set point Q**, not at the line
end (the connecting segment Q↔end is only an entity of L; class 3 on node 0 sends the walk along the boundary).
**FUN_18010bd00(isab) (144631-144754)**: if ≥ 2 records: temporarily remove the first record if its Y shape is −1 and
the last if its Y shape is −1 (144694-144727); if ≥ 2 remain: FUN_1801349c0(isab) (re-link, NOT DECODED, 144729-144731);
re-insert the first (and +0x30/+0x34 += 1 for records 1..n−1, 144732-144747) and re-append the last (144749-144751).

**FUN_18010a960(both, set, L, isab, out) (144026-144145)** — greedy forward walk with relaxation bookkeeping
(correction to §6.3.0, which calls it a shortest-path relaxation over all nodes):
```
dag = n records {double dist = 3.4028234663852886e38 (0x47efffffe0000000), int pred = −1, CSh* edge = NULL}
      (FUN_18010c990 145041ff / FUN_1801094b0 143435-143448); dag[0].dist = 0                           // 144069-144071
i = 0
while i != n−1 (and n != 1):                                                             // 144072-144119
    if isab[i].class == 1:                                                               // 144076
        (ok, j) = FUN_18010af10(set, L, isab, mode 0, i, edges); if !ok: break           // 144077-144078
        relax(i, j, lastEdge); next = j                                                  // 144079-144082
    else:
        (okF, jF) = FUN_18010af10(..., mode 1, i); if okF: relax(i, jF, lastEdge)        // 144085-144091
        if both: (okB, jB) = FUN_18010af10(..., mode 2, i); if okB: relax(i, jB, lastEdge)   // 144092-144100
        if !okF: if !okB: break; next = jB                                               // 144102-144106
        else: next = (okB and !(dag[jF].dist < dag[jB].dist)) ? jB : jF                 // 144107-144115 (tie → jB)
    i = next
if i == n−1: FUN_18010b6f0(dag, out)                                                     // 144121-144123
relax = FUN_18010c3a0 (144856-144875): d = dag[i].dist + len(edge); if d < dag[j].dist: dag[j] = {d, i, edge}
```
(`okB` keeps its previous value when `both` is 0; it starts false, 144046.) The edge shapes live in a local list freed
on return (144124-144131); FUN_18010b6f0 copies their entities.
**FUN_18010b6f0(dag, out) (144440-144628)**:
```
chain = []; j = n−1; total = 0
while j != 0:                                                                            // 144491
    if dag[j].pred == −1 or dag[j].edge == NULL: return (out unchanged)                  // 144493-144496
    chain.push(dag[j].edge); total += |edge entities|; j = dag[j].pred                   // 144497-144560
out.resize(+1); out[last] = new CSh (+0x50 = 0, flags 0); reserve total                 // 144566-144595
for e in reverse(chain) (node 0 side first): append all entities of e                   // 144596-144619
bbox (GetMinMaxViaShEnt)                                                                 // 144620
```
**FUN_1800f1580 — plain-route fallback of FUN_1800f0fa0 (130763-130887)**. The decompiler shows the function with a
single double argument and the rest as stack slots (in_CL, stack+0x28/0x30/0x38); what the text shows:
```
M = RotationMx(?)                                                                         // 130808
out (stack+0x30) cleared; if cross (stack+0x38): *cross = 0                               // 130829-130836
L = one 'L' entity: start = M·p, end = M·q (CTMatrix::operator*; p, q not visible)       // 130837-130854
FUN_18010c040(both = CL, set = stack+0x28, L, out)                                       // 130857
if out non-empty: erase shapes 1..|out|−1 (FUN_180011eb0(out, 1, |out|−1)); FUN_18010a6b0(out[0], NULL, cross)  // 130858-130871
```
so `cross` = off-line (boundary) length of route shape 0, as in §6.3.5. The argument binding (which point pair, the
rotation angle, the `both` flag) is **NOT DECODED**: RotationMx takes its angle in XMM1 (RCX = hidden result pointer;
RotationMx body 11801-11816 reads `in_XMM1`), which no caller line shows; FUN_1800f0fa0 calls it as a tail call
(130603) after FUN_1800f1080 has clobbered the argument registers in the text. FUN_1800f1080 also calls FUN_1800f1580
(130698) between its two rotations.

### 6.12.7 BIDIR retrace check — FUN_180065b20(C, S2, pre) (58210-58375)
```
S0 = start point of pre.ent[0]                                                           // 58272-58295
M = T(−S0) · R(θ) · T(+S0)       (TranslationMx / RotationMx / operator*=)              // 58296-58310
pre = M·pre   **in place** (FUN_180125b80(M, pre))                                       // 58311
E = end point of the last entity of pre (after the transform)                            // 58312-58339
if FUN_180112e80(S2, 1e-5, E) == 4 (outside): return 0                                   // 58340-58345
X = FUN_180134690(pre, S2)                                                               // 58348
if any X point is farther than 1e-4 from S0: return 0                                    // 58350-58361
return 1                                                                                 // 58363
```
The caller FUN_18005e690 passes its reversed section and, on success, copies **the transformed** pre to its output
(54416-54422), so θ matters for the output. θ is **NOT DECODED**: RotationMx's angle is in XMM1 (see above); the
text shows `RotationMx(local_e8)` where local_e8 = element 8 of the copied translation matrix (58305-58306), which is the
XMM0 slot, not the angle. Likewise TranslationMx takes x in XMM1 and y in XMM2 (1379-1392): the text shows x = −S0.x
and +S0.x; the y components are not visible (presumably ∓S0.y, unverified).

### 6.12.8 Corrections to earlier sections found in this pass
- §6.4.1 / task note "FUN_180070b60 appends to C+0x410 (l.64024)": nothing is appended. 64001-64022 push E into a
  local one-shape list; 64023-64028 **replace** C+0x410 by FUN_180153b30(C+0x410, disk(start(E), R−r)) and, if
  present, C+0x438 by the same with disk radius R (FUN_180070280); the march then commits each step into both lists.
- §3 D 2.2 "push this+0x718 to list this+0x740": C+0x740 is the begin pointer of the `CVe<CPt2>` at C+0x730, the
  target blacklist read by FUN_180053cd0 (6.12.4).
- §3 D 4.2 FUN_1800874e0: see 6.12.5 (half-circle rule).
- §6.3.0 FUN_18010a960: greedy walk, not a full relaxation; FUN_18010c040 snaps off-set line ends to the nearest set
  point (6.12.6).
- §6.2.5 NOT DECODED items FUN_18001ff60, FUN_180023100, FUN_180024a00 and §6.3 items FUN_180109960, FUN_180109e20,
  FUN_18010b6f0, FUN_180053cd0, FUN_180065b20 (except θ) are now decoded.

### NOT DECODED (6.12)
- FUN_180153b30 boolean core (used by FUN_180070280; reading "mat minus disk" is from §3 B.3, unverified).
- Whether RG+0x1c8 / RG+0x1f0 (local material windows) are empty or stale during the entry march FUN_180070b60
  (no rebuild there; FUN_18002a2a0 does not touch them).
- FUN_1801349c0 (ISAB re-link / post-process used by FUN_180134270 and FUN_18010bd00); FUN_18010af10 edge targets are
  as in §6.3.0 only.
- FUN_1800f1580 argument binding (points p, q, rotation angle, `both`) — decompiler lost the register arguments;
  FUN_1800f1080 internals (unchanged from §6.3.5).
- FUN_180065b20 rotation angle θ (XMM1 of RotationMx, not shown) and the y components of both translations;
  FUN_180125b80 (matrix × shape) internals.
- FUN_18008de70 record write details and FUN_18008aa50 first-pass bookkeeping (unchanged).

---------------------------------------------------------------------------------------------------------------------
## 6.13 HST engines pass (2026-10-23, post-batch 9): MCCECalcOffset / MCCECalcContourOffset sign rules, Dynamic Contour band roles

Line numbers are `decomp/mcce_all.c`. Closes the "sign convention" item of §6.9 for both offset exports; their raw-offset /
trimming core (FUN_1800d5330, FUN_1800dbce0, FUN_1800d48b0, FUN_1800d3d30, post FUN_1800d8d90) stays NOT DECODED and is
replaced in the port by the Clipper round-join offset (viewer/mcce_dyn.c `sl_offset`), whose sign rule below is the same.
**Update 2026-11-13 (M2 offset):** the core is decoded and ported DLL-exact: MCCECalcContourOffset in viewer/mcce_ctour.c
(formats/mcce_ctour_offset.md), MCCECalcOffset in viewer/mcce_offset.inc (formats/mcce_port_plan.md §9 "M2 offset", unit oracle
tools/tests/off_oracle.sh 1411 / 1411). Dynamic Mill's S1 / S3 / S4 use it; the other region sets still use `sl_offset` / the exact
own construction.

### 6.13.1 MCCECalcOffset(n, d, in, out, flags, ang, tol1, tol2) @18000471e (7804-7960)
```
state: +0x00 n, +0x08 d, +0x10 flags & 0xfff8ffff, +0x18 ang, +0x28/+0x30 tol1/tol2, +0x40 d_i, +0x48 |d_i|, +0x58 cos(ang)   7914-7926
tmp = copy(in); FUN_1800da810(in, tmp)          // entity +0x70 := pointer to the source entity (118128-118170)          7927-7929
FUN_1800da1c0(flags, ..., tmp)                   // per shape: CeFlags := 0; bit 4 UpdMinMaxSh / bit 2 bbox; bit 1 clean-up;  7930-7931
                                                 // bit 0x20 split FUN_180132ef0; bit 8 dir from area; bit 0x10 / (0x20 + count change) DetNestLvlShAr (117939-118048)
if |d| >= 1e-6:                                                                                                     7932
    FUN_1800da180(state, tmp):                   // 117926-117936
        if d_i > 0: FUN_1800d92f0                // adds a FRAME shape: bbox(tmp) grown by 2.5·|d|·n on every side (one CSh of 4
                                                 // lines FUN_180124c50), marked CSh+0x90 bit 0, then DetNestLvlShAr: the given
                                                 // shapes become the frame's islands (117465-117547)
        FUN_1800da5e0(tmp)                       // orientation by the island bit: outer (+0x54 bit0 = 0) -> dir -1, island -> dir +1 (RevSh,
                                                 // CSh+0x90 |= 2 marks a reversed shape) (118051-118075)
    for i = 1..n:  d_i = i·d; FUN_1800d50d0(state, tmp, out)        // 7936-7957; stop early when a negative d gives no new shape
        // FUN_1800d50d0 (114916-114998): per nest group [islands..., outer] (FUN_1800d4e80 114847-114880); a group is offset
        // only if its outer's bbox diagonal >= 1.97·|d| (FUN_1800d4fd0 114883-114913) - the core always moves INTO the
        // region by |d| (state +0x48); frame groups (CSh+0x90 bit 0) go to FUN_1800d48b0
        // odd passes toggle the nest parity of the new shapes (+0x54 += 1, 7943-7953)
    FUN_1800d3b60(state, in, out)                // 114136-114182: if d > 0: frame-derived shapes are erased and (n odd) every other
                                                 // shape's nest parity toggles (+0x54 += 1); FUN_1800d3cc0 + FUN_1800da6a0 per shape
                                                 // (start entity = the one with the lowest source index); FUN_1800d8d90; DetNestLvlShAr
else: out = copy(tmp)                                                                                               7958-7960
```
**Output orientation (decoded 2026-11-07):** FUN_1800da5e0 (118051-118075) reverses (RevSh) every input shape whose
direction disagrees with "outer −1 / island +1" and marks it with CSh+0x90 |= 2. The tail FUN_1800d3b60 (114136-114182) calls
FUN_1800d3cc0 (114185-114208) on every output shape: if **all** the shapes in its source list (CSh+0x78, the shapes its
entities came from) carry the mark, the output shape is reversed back. For an input oriented one way (outers d, islands −d -
every list the engine passes), either all sources are marked or none, so **the output keeps the input's orientation**
(outers d, islands −d; for d > 0 the frame trick reverses the given shapes and the parity toggle makes them outers again,
with the same result). Consequence: out1 of FUN_180062a70 (§6.4.2) = MCCECalcOffset(material) has the material's
orientation (FUN_18002c0e0 kind 4: climb → outer −1 = clockwise), the opposite of a CCW-outer polygon offset. Every tangent
taken on out1 (S1 targets FUN_180051480 +0x10, the approach arc side of FUN_18005c980, walks along S1) depends on it.
Then FUN_1800da6a0 (118079-118125) rotates each output shape to start at the entity with the lowest source index
(FUN_1800d9530), one earlier when that entity is an arc preceded by a line with CeFlags bit 2.

**Sign rule (decoded):** the operation is on *regions* defined by the island bits (outer = material side inside, island =
hole), not on the travel direction. **d < 0 shrinks the region** (outers move in, islands grow), **d > 0 grows it** (done as
"shrink the complement inside a frame": the frame trick of FUN_1800d92f0 + the parity toggle of FUN_1800d3b60). The input
direction is irrelevant (FUN_1800da5e0 re-orients by the island bit). Mode bits seen: 0x100 (no extra post), 0x121 / 0x125 /
0x127 = 1 (clean-up) | 4 (bbox) | 0x20 (split self-intersecting) [| 2] | 0x100; their internals (FUN_18012c2b0, FUN_18012c740,
FUN_180132ef0) are NOT DECODED. The port's `sl_offset` (Clipper, non-zero fill of outers CCW / islands CW, d > 0 grows) follows
the same rule; this is also what the 37 user Dynamic Mill ops confirm (region builder §3 B.6: RG+0x30 = offset(M, −(R + min
radius)) is the tool-centre region).

### 6.13.2 MCCECalcContourOffset(n, d, in, out, flags, ang, tol0, tol1, tol2) @180005272 (8571-8760)
```
flags' = flags & ~0x50 (bit 0x20000: & ~0x70)                                                                       8686-8689
tmp = (flags' & 0x40000) ? { copy of in[0] } : copy(in)                                                            8704-8709
FUN_1800da810(in, tmp); FUN_1800ce9a0(d, tmp)    // a RETRACED shape (FUN_1801230c0: even count, each entity has its reversed
                                                 // twin) gets dir := (d <= 0) ? +1 : -1 (111497-111526)
FUN_1800da1c0(flags', ..., tmp)
if |d| >= 1e-6: for each shape s of tmp that is not a lone 'P' (8718-8725):
    (e, s') = FUN_1800cd040(d, s)                // 110684-110781: s' = copy(s) with +0x54 := 0 (an outer)
        closed, dir +1:  e = (d <= 0) ? +|d| : -|d|
        closed, dir -1:  e = (d <= 0) ? -|d| : +|d|
        open (dir 0):    e = +|d|; if d > 0: RevSh(s'); Retrace(s') (FUN_18011d800); FUN_1800d98a0(N/2, N-1, s') clears the
                         source link (+0x70) of the returning half; dir := +1
    FUN_1800da180 + FUN_1800d50d0 per pass with d_i = i·e (as 6.13.1), then FUN_1800ce670 (111406-111494): an open input
        (+0x50 == 0): if d > 0 RevSh(result); FUN_1800da6a0; FUN_1800ccf20; FUN_1800cd7e0 splits the result where the source
        link is missing, FUN_1800cd350(d, ...) post-processes the pieces (NOT DECODED), the pieces replace the result
else: out = copy(tmp)
```
**Sign rule (decoded):** per shape and by **travel direction**: **d > 0 offsets to the LEFT of the shape's direction, d < 0 to
the RIGHT**. Closed CW (dir −1) with d > 0 grows the shape, CCW with d > 0 shrinks it (the effective region offset e above
then goes through MCCECalcOffset's region core, which grows for e > 0). An open shape becomes the closed there-and-back loop
S + rev(S) (d <= 0) or rev(S) + S (d > 0); after FUN_1800da5e0 re-orients it CW, the half that keeps its source link
traverses rev(S) (d <= 0) or S (d > 0), so its grown side is the right (d <= 0) or the left (d > 0) of S: the same rule. The
returning half's pieces lose their source link and are cut away by FUN_1800cd7e0. What FUN_1800cd350 does with the end caps
(the arcs round the open chain's end points) is NOT DECODED.

### 6.13.3 Correction to §3 B.4 (Dynamic Contour closed group, FUN_1800922e0 80883-81075)
FUN_1800922e0(Der, left, G, param_4, param_5) is called by FUN_180092950 with param_4 = its local_70 and param_5 = its local_c0
(81268-81270); then `AIR = FUN_180153b30(local_70, MCCECalcOffset(1, P+0x60, local_c0, 0x127))` and `M = local_70`
(81276-81287). Inside FUN_1800922e0, **param_4 receives nest(K ∪ offset(K, −P·c))** (or K with CTOUR_POCKET, 80980-80996) and
**param_5 receives nest(K ∪ T1)** (81007-81012). So, with REG = local_70 and WALL = local_c0 as in B.4:
**REG (= M, the motion region) = the wide band of width P = 2(R + rr) + 2R + t + approach next to the wall; WALL = the thin band
of the stock thickness t (+0x1748 0x16e0 etc.); AIR = REG − offset(WALL, first pass offset)**, i.e. the material is the stock band
along the wall. §3 B.4 had param_4 / param_5 swapped for the closed group (the open-chain path FUN_1800930c0 → FUN_1800914a0
(param_4, param_3 swapped by the call, 81398-81401) was already right: REG = offset(sh, P·c) wide, WALL = thin). The swap made
the port's AIR empty and its region a band as thin as t ("nothing to cut", formats/opdlg_port_audit.md 13.3).

## 6.14 HST engines pass: Peel Mill wiring, PeelAesthetics leftovers, CE_AIR_ENTRY_ONLY (2026-10-23)

**mcmill side (decoded, decomp/hst2/peel_mcmill_spec.txt; formats/mcmill_2d_engines.md 7):** CHST2DProcess::PreparePockBoundsPeel
builds the closed **Material** loop this+0x348 and the longer **Motion** loop this+0x328 round the two side curves, calls
`PeelAesthetics(R, mr, P = op 0x14e0, &Motion, &Material)` (so in §1.2 **A = Motion, B = Material**), and the work item calls
`MCCECalcDynPock(parm, S1 = Motion, S2 = Material, S3 = NULL)` (executor FUN_1802a5760, decomp/mcmill_hst2d_dyn.c l.397-416:
S2 = file+0x50, else file+0xa0 = +0x348). So Peel runs the S2 preprocessing FUN_180091100 (§3 B.3): AIR = Motion − grown
Material. **Who sets 0x4000000 (CE_AIR_ENTRY_ONLY):** mcmill FUN_18029cb90 for style 3 (decomp/mcmill_hst2d_dyn.c l.136-138).

**PeelAesthetics pieces closed in this pass (mcce_all.c):**
- FUN_1800aafd0 (92947-92966): the "fit" is the chord: entity 'L' from the cubic's P0 to P3.
- FUN_1800aca30 (93774-93816): the check: Bez(0.25), Bez(0.5), Bez(0.75) each within tol of that line (GetNearestEntPt,
  squared distance > tol² fails).
- FUN_1800a5b00 (90456-90566): a stack of control points; the top cubic becomes its chord when the check passes (3 points
  popped), else it is split at 0.5 (FUN_1800ae060, 94475-94557, de Casteljau, first half on top). Before the loop
  FUN_1800ae3c0 splits at the inflection parameters in [0, 1] (FUN_1800aa070: roots of cross(B', B''), via the quadratic
  path of FUN_1800afd30), largest first, the next one rescaled by it [root order ascending: inferred].
- FUN_1800de040 (119886-119967): S = start of entity 0 of the far piece; E = the end of entity index (number of PIECES − 1)
  - with one piece that is entity 0 too (the decompile indexes the entity vector with the CVe<CSh*> count). Ported as read.
- FUN_1800de670 (120039-120106): the retry branch's return value is a decompiler flow artefact; ported as "0 when the retry
  fails" [inferred].
- Retrace + MCCECalcOffset(d4) of the far piece (120855-120858) = the region a disc of radius d4 sweeps along the open piece
  (the offset of a zero-area there-and-back loop); the port builds it that way (Clipper open-path offset).
Still NOT DECODED: CalcEntIntersect for collinear overlapping lines (the Motion and Material loops share their side curves;
the port reports crossings only at shared vertices); FUN_180162010 (S2 split, read as "boundaries cross").

## 6.15 mcce::CalcMedAx (2026-10-23, decode only; spec decomp/hst2/calcmedax_spec.txt)
Exact Voronoi diagram of the chorded region (arcs → ApxArcWithLnSegs at the first tolerance), point sites at reflex vertices,
three bisector kinds (line-line, point-point straight; line-point parabola, clearance h(1+u²)), edges traced from the sharpest
convex corner of the outer loop with a LIFO worklist, each edge ended at the first contact with a third site (six contact
solvers), then smooth-joint removal at cos(8°) and compaction. 148 functions / 12,078 decompile lines reachable (medial-axis
code ~5,200; the rest generic). MAE record fields +0x08..+0x40 and the parabola frames are in the spec. NOT DECODED: the shape
clean-up sub-tree FUN_18012c740, the qsort comparators LAB_180006ee7 / LAB_180002509 (no body in the decompile), the parabola
frame orientation in FUN_1800b3a10 / FUN_1800ba330 / FUN_1800ba8a0 (Ghidra dropped arguments: needs disassembly), part of
FUN_1800ab960. **Not ported** (estimate ~1,500 lines of C). **Correction:** the Pocket "Minimize tool burial" does not use
CalcMedAx - mcmill has its own medial axis (POCKMDL.cpp, FUN_1801d9f60 / FUN_1801ddf20). The Dynamic entry methods 1/2/4/5
also need the leaf builders listed in §6.9 (FUN_1800c2270 / FUN_1800c1c90 chain emit, the trochoidal builders), which remain
NOT DECODED.

### 6.15.1 Gaps closed and port (2026-10-24, medax batch; formats/opdlg_port_audit.md "Medial axis")
Addendum decomp/medax/medaxgaps/medaxgaps_spec.txt (objdump of mcce.dll; Ghidra had lost the CTMatrix arguments):
- **CTMatrix**: 3x3 row-major, row vectors (p' = [x y 1] M, x' = (y M10 + x M00) + M20), `A *= B` = A·B (first factor applied
  first); TranslationMx(tx, ty) in M20 / M21; RotationMx(a) counter-clockwise; ReflectionMx(0) y -> -y; InvertMx = adjugate / det.
- **Parabola frame FUN_1800b3a10**: the line is side 2 when side 2 is 'L' (else side 1, sigma = +1 / -1); F = foot of P
  (CalcLnLnIntersectPt({P, N}, {e1, D}), t1 >= 0 required, N = (-dy, dx)); h = |P - F| / 2 >= 1e-12;
  M = T(-P) · R(pi - atan2(dx, -dy)) [· Refl(0) when the line is side 2] · T(h, 0); +0x90 = InvertMx(M). In closed form
  x_loc = h - (X - P)·n, y_loc = sigma (X - P)·u (u = D/|D|, n = left normal): focus (h, 0), directrix x = -h (the spec's
  inference confirmed). u0 / u1 = y_loc / 2h of the line point at t_start / t_end (1 for side 1, 0 for side 2), u1 clamped by
  ParabolaLimit (right normal of the neighbour's tangent from the point, larger ray parameter, >= 0).
- **qsort comparators** LAB_180006ee7 (EPD) / LAB_180002509 (roots): ascending on the first double, equal -> 0; MSVCR120 qsort
  is not stable (tie order from the CRT source [inferred]: shortsort for <= 8 elements).
- **'P' entities** inserted by FUN_1800c0d70: +0x12 / +0x60 / +0x80 zeroed (FUN_180027160 memsets) - no effect.
- **FUN_18012c740**: a no-op (bbox only) on a chain whose entity ends meet within 1e-7; otherwise it heals gaps (trim to an
  intersection, snap, or a bridging 'L' with flag 0x10). Pseudo-C in the addendum section E.
Still NOT DECODED: FUN_1800a4460 / CalcArcLnSegIntersect / FUN_18013d200 / FUN_180071580 (gap healing only), the root solver's
degree 3-6 paths, FUN_1801126b0 / FUN_1801125a0 (MCCECalcMedAx flag 0x40), InvertByExchange (never reached).

**Port** viewer/mcce_medax.c + mcce_medax_par.c (included by mcce_dyn.c; ~900 lines): CalcMedAx group loop, PrepareShapes
(drop tiny, ApxArcWithLnSegs chords with the exact count rule and the accumulated angle, reflex 'P' sites, orient), seed,
LIFO worklist, TraceEdge with the four AdvanceVariant variants, IsDuplicate, the three bisector builders with the CTMatrix
numerics, the six contact solvers, the degree <= 2 root solver, contact filter / classify (MSVC shortsort tie order), side
parameters, SpawnChildren, PostProcess (joint test at cos 8 deg, same-arc removal, all-removed fallback, compaction) and
FUN_1800c5df0 (max clearance over edge starts then ends, strict >). Not ported: the gap healing of FUN_18012c740 (the region
code hands over closed shapes); the root solver beyond degree 2 (not reachable). Ours: tools/tests/medax_test.c (clearance =
distance to the boundary at 11 points per edge: rectangle, L-shape, obround, square with island, notch - max error 7e-14) and
the Probing diagnostic tools/tests/medax_probe_exact.py / medax_probe_test.c.
**Use** (mcce_dyn.c `medax_centre`, behind `MCCE_MEDAX=1`): FUN_180079050 = CalcMedAx(this+0x340 = C->cur, 0.1, 8.0) +
FUN_1800c5df0 + FUN_1800c5230 for the Helix-only centre and the Profile entry's clearance. Without the switch the former
bisection stand-in stays (ours). Finding: in a slot both spine ends have the maximal clearance and FUN_1800c5df0's strict
compare makes the result depend on rounding; on the exact Probing Example 2_MM regions (lines + arcs) the port gives
Mastercam's NCI helix centre on 5 of 6 ops (op 6: the other end), on the port's Clipper polygon region on 2 of 6.
(MCCE_MEDAX is on by default since N13-25.)
**Fix 2026-11-14 (P-P bisector end normals)**: FUN_1800b3980's point-point branch intersects the bisector with the normal lines
at the two neighbouring lines (l.97560-97571); their direction is (dy, -dx) (auStack_90 / auStack_b0), the port had (-dy, dx), which
flips tA / tB and so drops both limits on nearly straight walls of reflex 'P' sites: s1 = 3.4e38 and the edge ran out of the region
until a far contact. Seen on OptiRough TUTORIAL 7 METRIC op 5 (level -16.975, a 639-entity weave region): centre (1.77, 158.14)
outside the part, the helix did not fit, no plunge, "nothing to cut" (regression since N13-25, c8fbf89). Checked against
mcce.dll on the same engine input (peloader --dyn with MCCE_TRACE_MEDAX=FILE MCCE_TRACE_MAE=1: FUN_180079050's region,
centre and FUN_1800c5df0's MAE list): after the fix the port's medial axis has the DLL's 1038 edges with the same types and
sides in the same order, best edge 1031, centre (-76.590120, -3.068015) rho 50.224545 = the DLL's.

### 6.15.2 Dynamic entry methods on the medial axis (2026-10-24, medax batch; decode decomp/medax/dynentry/)
Specs decomp/medax/dynentry/dynentry_spec.txt (dispatch, helix centre, prune / chain / trim, methods 0x100 / 0x200 / 0x400 top
level / 0x1000) and troch_walk_spec.txt (the trochoidal walk FUN_180081ab0). Dispatch FUN_18006fc70, first matching bit:

| UI | flag | function | medial axis use | port (viewer/mcce_medent.c, included by mcce_dyn.c) |
|---|---|---|---|---|
| 0 Helix only | 0x100 | FUN_180078840 | centre = FUN_180079050 (CalcMedAx(this+0x340, 0.1, 8) + FUN_1800c5df0); flag 0x100000: FUN_180077e70 (entry points) | `medax_centre` behind `MCCE_MEDAX=1` (6.15.1) |
| 1 | 0x200 | FUN_180078130 | path FUN_180079260 (offset -0.002, CalcMedAx 0.001, prune FUN_1800c4870, tours FUN_1800c1530, trim FUN_1800c1a40); helix at the path start + join arcs FUN_180079570 + the path (the helix loops are discarded, as Mastercam) | ported |
| 2 | 0x400 | FUN_180078500 | the same path offset -(C+0xa0 + 0.002), helix at its start, recursive run FUN_18007a150 / FUN_180079cf0, trochoidal walk FUN_180081ab0 (nine leaf builders, Brent step root FUN_1800826d0 on [1e-4, 2d] tol 1e-7) | ported (Bezier join as chords within 1e-5: ours) |
| 3 Profile | 0x800 | FUN_18007cb70 | only rho (FUN_180079050) | rho via `medax_centre` behind `MCCE_MEDAX=1` |
| 4 | 0x1000 | FUN_18007b3c0 / FUN_18007b6e0 | offset -(0.5 C+0xa8 + 0.002), largest group, CalcMedAx; the longer of the pruned full tour and the clipped tours (ties: pruned) | ported; the clipped-tour candidate needs FUN_18015b2e0 (NOT DECODED): taken as empty when its decoded early-outs prove it empty, else the pruned tour is used (ours); no pruned tour -> NOT DECODED error |
| 5 | 0x8000 | FUN_180074560 | none | refused: entry chain source / FUN_180132200 NOT DECODED |

No entry shape and flags & 0x8700 -> Profile fallback (l.63516-63519). Prune FUN_1800c4870: a leaf with both sides on the same
shape, a free end touching no other live edge (1e-12) and a subtended angle (FUN_1800c5690) <= 164 deg; all pruned -> one
zero-length edge at the max-clearance point. Chain builder FUN_1800c1530: closed there-and-back tour of the tree (segments >
1e-4), parabolas as Bezier-subdivided polylines at 0.001, +0x13 bit 0 on backward-pass segments (recomputed after the trim by
FUN_1800c2d30). The inner Dynamic pass of method 2 runs through `dyn_pock_run` (mcce_dyn.c, split from mcce_calc_dyn_pock; takes
the previous-position entity P+0x118). Also fixed: cetp_emit's arc -> line rule (FUN_1800874e0 l.75357-75420: |sweep| > pi
tests the point opposite the start, so a full circle stays an arc). Corrections: §6.7.4 the join tangent is rotated the other
way (+90 deg climb / -90 deg conventional, disassembly); §6.7.5 LAB_180006aaa = FUN_1800826d0 (decoded), the step context has no
0.0 field, FUN_18007d5a0 uses the previous segment's start point / tangent, FUN_18007f8c0 offsets its two segments to opposite
sides, FUN_1800805d0 has no turn test. Still NOT DECODED: FUN_18015b2e0, FUN_180132200 / FUN_1801270c0 (method 5), FUN_1801103a0
and the C+0x648 entry points (flag 0x100000), AppendBezier internals (z / CeFlags of the join), Brent's root choice with several
sign changes. Ours: the method-4 shortcut above, the round-ended open offset of the tour (its mode bits NOT DECODED), no
smoothing (callback NOT DECODED, as Profile), an empty medial axis when CalcMedAx gives up.

# 7. NOT DECODED — consolidated pointer (written before §6; the current list is §6.9)
Each section ends with its own NOT DECODED list (address + unverified role): §1 Peel, §3 B, §3 C (C.7), §3 D. The
biggest gaps for a port, in order: material-model internals (FUN_180037790 subtraction, FUN_18003a140 leftover loops),
state 0/1 trial-step generators (FUN_18006b170, FUN_18006b570, FUN_18001efb0, ...), stay-down link strategies
(FUN_18005ff90, FUN_180044870, FUN_18005d5f0, FUN_18005f4c0), entry/link region sets FUN_180062a70, the contour band
kernel (FUN_1800d2cc0, FUN_1800d3210, FUN_1800aeae0), PeelMotMat output wiring FUN_1800e28c0 / FUN_1800942b0
(PeelMotMat is not called by mcmill 2017), and the Area Mill engine in mcmill (FUN_1802a5ca0 tree).

## 6.16 Guided link planner FUN_1800f0fa0 / FUN_1800f1080 (decode in progress, 2026-11-07)

Callers: connector 4/5 → FUN_180044340 (40706-40879) with (d, step, both) = (0.9·C+0x1d0, C+0x1e0, 1), E = exit end, A =
approach start, G = FUN_180047920(S4, E, A); stay-down strategy (5) FUN_18005fb50 with d = 1e-6, both = (flags >> 2) & 1.
On the corpus (P+0x261 = 0) these are the routed links of ops 3, 10, 32, 36, 42, v12 22 (mcce_port_plan.md §9, third pass).
Our port uses the plain-route fallback FUN_1800f1580 for every call (FUN_1800f1080 NOT PORTED).

**FUN_1800f0fa0 (130591-130606):** ok = FUN_1800f1080(...); if !ok or the result is not exactly one shape → FUN_1800f1580.

**FUN_1800f1080(cetp, d, step, both, E, A, G, out) (130609-130762):**
```
out = []; if G empty: return 0
G' = copy(G)                                         // AppendByCopy
FUN_1800f33a0(G')        (131850-131878): RevSh every shape with island bit and dir +1, or no island bit and dir −1
                         (outers −1 / islands +1)
FUN_1800f2480(E, A, G')  (131308-131553): for P in (E, A): q = nearest point of G' to P (FUN_18013cfe0); if |P − q| ≤ 1e-4
                         and 0 ≤ q.t ≤ 1 and q is ≥ 1e-6 from both ends of its entity: split that entity at q.t (insert a
                         copy, FUN_180071580; line: end := lerp, next start := lerp; arc: sw·t / rest), SetChnMinMax both
                         - E and A become vertices of G'
FUN_1800f3220(0.9·d, G') (131795-131849): G' := FUN_180124620(0.1·(0.9·d), G') (swap) = per shape FUN_180124110
                         (156996-157193): arcs replaced by the chords of ApxArcWithLnSegs(0.09·d, arc, pts, true), the rest copied
FUN_1800f3de0(0.1·d, E, A, G') (132267-132323): per shape: FUN_1800f3440 (131879-131921: rotate the entity list to start
                         at the entity with the largest |+0x30 − +0x20| (FUN_18014fd50)); FUN_1800f3520(E, A, i, G', → list)
                         (131922-132266, not read); FUN_18014f740(0.1·d) on each piece; FUN_1800f21d0(shape, list) (not
                         read); the shape is replaced by list[0]
if FUN_1800f2b40(G') (131554-131607) → return 0      // degenerate: some shape FUN_18012f2b0, or the last shape with area
                                                     // (FUN_180126ac0 tol 0.001) < 1e-6 / < 3 entities / dir ≠ the area's sign
FUN_1800e9790(d, step, E, A, G') (126630-126641) = FUN_1800ec930 then FUN_1800e9440:
   FUN_1800ec930 (128104-128158): for every entity k of every shape: FUN_1800eb810 (127554-127642): incoming tangent
       (FUN_18011ffd0) and outgoing tangent (FUN_1801204a0) at the entity start, turn = wrap(atan2(out) − atan2(in)); a
       vertex with turn < 0 (concave) gives its point V and the bisector angle out − (turn + π)/2; if V is not within 1.5·d
       of E or A (FUN_1800eb6e0, 127504-127524): FUN_1800e9820(d, E, A, angle, (shape, k), G') (126642-126706) = a bisection
       (≤ 9 halvings) on the size of a cut FUN_1800ec230 (267 lines, not read) at that concave corner, then the cut with
       size − 1e-4 replaces the shape
   FUN_1800e9440 (126522-126629): for mode in (0, 1): X = FUN_1800e9c60(mode, d, step, E, A, G') (286 lines, not read);
       R = FUN_180153b30(1e-6, G', X) (G' minus X); if R has exactly one outer: G' := R, FUN_1800f33a0(G')
FUN_1800f3220(0.9·d, G'); FUN_1800f2ca0(E, A, G') (131608-131794, not read)
a = FUN_1800f18a0(G') (130890-131207): angles = [atan((y1 − y2) / (x2 − x1)) for every entity with x2 ≠ x1, read from
       +0x18..+0x30 as written (an arc contributes its cx, cy, r, sa)] + [π/2, −π/2]; sort ascending (FUN_1800eff40);
       a = midpoint of the largest gap between consecutive angles (first maximum)
G' := RotationMx(a) · G' (FUN_180125ec0)             // the slicing direction lies in the largest gap of edge directions
FUN_1800f1580(...)                                   // (arguments lost in the decompile)
graph = FUN_1801009c0(G') (139133-139294):
   recs = FUN_1801003d0(G') (138911-139073): one 0x80 record per vertex (entity start) of G' (count FUN_180100860), bbox of
       G' grown by 0.001; per vertex V: FUN_1800fe1f0(bbox, G', …, V) (137746-138033) intersects the horizontal line
       y = V.y across the bbox with G' (FUN_1801066f0, sorted FUN_1800ff8b0), finds V among the crossings (FUN_1800ffc30),
       classifies the gaps between consecutive crossings (FUN_180108d70, 4 = outside) walking right and left from V until
       the first outside gap; the inside gaps adjacent to V become the vertex's items (0x48: crossing ends, class); no gap
       → one item with class 1 at V itself; FUN_1800feab0 files them (477 lines, not read); each record's 0x28-byte list
       is sorted (FUN_1800fa160)
   edges: for each unused item (+0x44 == 0) of record i: FUN_1800ffd30 (138698-138768) walks the boundary from the item's
       vertex (up to 3 steps, FUN_180100900 / FUN_1800ffa20) to the matching record / item; a match gives an 0x88 edge
       {index, item i ends (0x10-0x40), item j ends (0x48-0x78)} and marks both items used; edges sorted (FUN_1800fa390)
   = a horizontal trapezoidal (cell) decomposition of the rotated region
cells = FUN_1800f61a0(G', graph) (133165-133189): FUN_1800f6d10, FUN_1800f6700, FUN_1800f68c0 (not read)
b = FUN_1800eda20(G', cells, out) (128580-128623): FUN_1800edc30 (search), FUN_1800ee2e0, FUN_1800ed200 (route) (not read)
out := RotationMx(b) · out
FUN_180136370(d, G, E+0x10, out) (166702-166791, not read)
return |out| == 1 and FUN_180113300(out[0], G, 1e-5) & 4 == 0
```
Sizes (planner-specific functions / their decompile lines; with the shared kernels in brackets): FUN_1800f3de0 12 / 1265
[3765]; FUN_1800e9790 17 / 1649 [18051, it uses the offset and boolean cores]; FUN_1800f2ca0 1 / 186; FUN_1800f18a0 8 / 774;
FUN_1801009c0 62 / 4738 [7852]; FUN_1800f61a0 20 / 1510; FUN_1800eda20 13 / 713; FUN_180136370 [10071, shared only];
FUN_1800f1580 1 / 126 [6535]. NOT DECODED: everything marked "not read" above.

**Pass 2 (2026-11-07): the decomposition and the cell channel, decoded and ported** (viewer/mcce_dyn.c, block "guided
planner", behind MCCE_GUIDED=1, debug only; each function cites its lines there). Records:
- Position block (0x20) = {int shape, int entity, double t, CPt2 pt}. Gap item (0x48) = {L block +0x00, R block +0x20,
  class +0x40, used byte +0x44}. Vertex record = CRail (0x80): list A at +0x08 (begin +0x18), list B at +0x30 (begin +0x40),
  link list at +0x58 (begin +0x68) of 0x28 entries {int vertex, pad, position block}. Trapezoid = CTrap (0x88): +0x00 index,
  +0x08 lower item (L, R), +0x48 the upper item's R block, +0x68 its L block.
- FUN_1801009c0 = records (FUN_1801003d0) → for each record i, each unused A item k: partner FUN_1800ffd30 → trapezoid
  {index = count so far, A_i[k], B_j[m].R, B_j[m].L}, both items marked used; std::sort by (lower L.y + upper L.y) / 2
  (FUN_1800f8b60 key +0x20 / +0x80).
- FUN_1801003d0: the vertex point is the entity's RAW +0x18/+0x20 (an arc's centre - the region is expected to hold lines
  here), t = 0; band index CHp FUN_180106d60 (band per entity raw +0x20, std::sort, a band < 1.0000000000000002e-06 above
  its predecessor erased; an entity filed in each band with ymin − 1e-4 < y < ymax + 1e-4); bbox of G ± 0.001; link lists
  std::sort by t (FUN_1800f8a60 key +0x10).
- FUN_1800fe1f0: line (xmin, V.y) → (xmax, V.y); FUN_1801066f0 crosses it with the entities of the band nearest to y
  (FUN_180106b70: lower_bound, previous band when strictly nearer), CalcEntIntersect(1e-5), ONLY the first hit kept; record
  {pt, X (0, 0, t), Y (shape, entity, t)}; CRT qsort by X (LAB_1800074dc); FUN_1801339b0 erases a record duplicating its
  cyclic predecessor (same X / Y shape, FUN_180134800); FUN_1801349c0 rings. FUN_1800ff8b0: of two neighbours with
  |Δx| ≤ 1e-10 the one with Y t outside [0, 1] erased (FUN_180133700 splices the ring). FUN_1800ffc30: crossing nearest to V.
  Classes (all 4 first) right from V's crossing and left from the one before it, stopping after the first 4; every
  non-4 gap → item {L = crossing k (Y side), R = crossing k+1, class}; no item → {V, V, class 1}. No crossing → no item.
- Gap class FUN_180108d70 = FUN_180108dd0, and FUN_180109220 when that gives 1 (disasm 0x180108d70). FUN_180108dd0:
  different Y shapes → 2; midpoint M within 1e-5 of crossing i → 1; per end crossing c (i, then j): the point before
  (FUN_1801089a0: backwards from c's entity to the ring predecessor's entity FUN_180108c80, first raw start with
  |y − c.y| > 1e-5) and after (FUN_180108a80: forwards to FUN_180108b80's entity, first raw end); when both exist and M.y lies
  strictly between their y: cross = (M.y − a.y)(c.x − a.x) − (M.x − a.x)(c.y − a.y), ≥ 0 → 2, < 0 → 4 (end i: NaN → return
  1; end j: NaN → 2); an end without that → 1; result = the common value, else 1. FUN_180108c80 / FUN_180108b80 (disasm):
  the ring neighbour's entity; when it is the adjacent entity and (c.t < 0 or neighbour t > 1) (resp. c.t > 1 or t < 0)
  and the next ring neighbour is not c, that one's entity. FUN_180109220: s = FUN_180126870 (sign of the RAW-chord area
  from position i to j along the shape closed by j → i: +1 / −1) for (i, j) and (j, i); differs from the shape's dir →
  outer 4 / island 2, else outer 2 / island 4 (disasm 0x180109316-0x18010936b).
- FUN_1800feab0 (records): A = items; pass: class 1 with FUN_1800fffc0 true erased; an item after a kept one whose L side
  FUN_1800fdce0 ∈ {2, 3} is merged into its predecessor (R copied) and erased. B = items; class 1 with FUN_1800fffc0 false
  erased; merge on {1, 3}. Then for k = 0..n: block = item k's L (k < n) or item n−1's R, filed once (exact compare, 1e-14)
  as {vertex, block} in the link list of record FUN_180100900(block.shape, block.entity) (n = 0: the engine reads items[−1]).
- FUN_1800fffc0: ends on different shapes → false; ordered along the shape (F, S); |F − S| ≥ 1e-15: forward length F → S
  (FUN_180149d80) within 1e-5 of the chord or shorter than S → F → S.x ≤ F.x, else F.x ≤ S.x; else the start y of the
  entity before F (t < 0.5) or F's entity < L.y. FUN_1800fdce0: backwards (from the entity before when t < 0.5) first
  entity start with |y − p.y| > 1e-6 → 1 above / 2 below; forwards (next entity when t > 0.5) first end → | 1 / | 2.
- FUN_1800ffd30: from the A item's R block entity, up to 3 entities forward: record r of the entity; FUN_1800ffa20 scans r's
  links from (step 0: after vertex i's own link, else 0): vertex j, its B items from the last, the first whose R end lies on
  r's entity: unused → partner; used → stop with none.
- FUN_1800f1080 calls FUN_1800f1580(both, E, A, angle, G', route, X) (disasm 0x1800f1265-0x1800f128b): the plain route of
  the line Rot(angle)·E → Rot(angle)·A through the rotated region, first shape kept.
- FUN_1800f61a0(route, traps, cells): FUN_1800f6d10 - FUN_1800f5b80 at the route's raw start, then per route entity (up =
  y1 < y2; segment up P1 → P2 else P2 → P1): lower_bound on the trap key for (start y ∓ 1e-7), scanning up / down: when the
  segment crosses the trap's mid line (CalcLnSegLnSegIntersect 1e-5: carriers meet, parameters outside [0, 1] only within
  1e-5 of the end) and FUN_1800f7440 allows it, the trap is appended; stop once the mid height passes the segment end ±1e-7;
  FUN_1800f5b80 at the raw end. FUN_1800f5b80: the trap strictly containing p (its sides at p.y), else the nearest
  (FUN_1800f6340), appended unless equal to the last. FUN_1800f7440: last has the same index → no; shares the line
  (|Δy| < 1e-7, x ranges overlap) → yes; else a trap found against the direction on that line overlapping both → appended
  first, yes; else no. FUN_1800f6700 drops traps while their summed height < 1e-8; FUN_1800f68c0 cuts loops (index seen twice
  in the initial sorted index list: erase from after its previous occurrence up to it).
- Sorts: std::sort = MSVC 2015 introsort (insertion ≤ 32, ninther > 40, heap at depth 0) [inferred from the structure of
  FUN_1800fa160]; the crossing list = CRT qsort.
**FUN_1800f2480 (131308-131553), decoded and ported (gp_split_at):** for P in (E, A): q = FUN_18013cfe0 nearest; if
|P − q| ≤ 1e-4, 0 ≤ q.t ≤ 1, |start − q| ≥ 1e-6 (arc: geometric start; others raw +0x18/+0x20) and |end − q| ≥ 1e-6 ('L'
p2, arc geometric end, others raw +0x18/+0x20): FUN_180071580 inserts a copy after the entity; first: 'L' p2 := lerp(t),
'A' sw := t·sw; second: 'L' p1 := lerp(t) of its own ends, 'A' sa += sw·t, sw −= sw·t; SetChnMinMax on both.

**Pass 3a (2026-11-07): the search FUN_1800eda20 decoded and ported** (debug only, same block): it is a channel funnel
("string pulling"), no graph search. FUN_1800f1080 (disasm 0x1800f1290-0x1800f12ec): FUN_1801009c0(G', traps),
FUN_1800f61a0(route, traps, cells), FUN_1800eda20(route, cells, out), out rotated by −a (xmm6 ^ sign), FUN_180136370(d,
G (the caller's original), E + 0x10, out).
- FUN_1800edc30: records CHbt (0x28: y, window +0x08/+0x10, x +0x18, flag +0x20, fixed +0x21), n = cells + 1; record 0 /
  n−1 = the route's raw start / end (fixed); record k between cells k−1, k: the nearest of |prev top − cur bottom| (case 0:
  up, flag 1), |prev bottom − cur top| (1: down, flag 0), |last y − cur bottom| (2), and |last y − cur top| (3) when
  strictly nearer; window = [max(left limit, line's left x), min(right limit, line's right x)] on the cell's bottom (0, 2) or
  top (1, 3) line; limits = prev top (0) / prev bottom (1) / the last record's window (2, 3, unswapped); swapped when flag 0;
  record 0 takes record 1's flag, the last its predecessor's.
- FUN_1800ee2e0: per run of equal flags [i0, i1]: x(i0) = FUN_1800edb70(i0, i0 − 1), x(i1) = FUN_1800edb70(i1, i1 + 1)
  (the window end nearer to the neighbour's window middle, +0x10 on ties, +0x08 when the neighbour does not exist), both
  fixed; FUN_1800ed0f0(i0, i1): from each fixed record, FUN_1800ed5b0 side 1 then side 0 repeated until neither fixes a
  record, then the next fixed record, until i1. FUN_1800ed5b0(side, i): direction to the next fixed record (none if both
  |d| ≤ 1e-12); among the records between, the window end (+0x08 side 0, +0x10 side 1) whose angle from the direction (wrapped
  to [−π, π]) is < 0 (side 0) / > 0 (side 1) with the strictly largest |angle| becomes fixed there.
- FUN_1800ed200: one open shape, lines from record 0 through every fixed record, points within √9.999999999999998e-15 of
  the previous skipped.
On buckle-v6 op 32 (region preparation not ported: our G' = the S4 group, oriented and rotated only) the first link's
path is the same shape as Mastercam's (straight leg to (30.97, 24.72), then along x ≈ 30.45 down to y ≈ 20, then to the
target); Mastercam's path lies ~0.25 mm further in (the 0.9·d offset of the preparation) and has the post-process arcs.
- FUN_180136370 (166702-166789, NOT DECODED beyond its top): per out shape FUN_180136680 (166792-166847) - a binary-search
  shortcut loop FUN_1801368c0 (166850-166999) over FUN_1801380f0 / FUN_180138510 / FUN_1801353a0 / FUN_180135610 /
  FUN_1801351b0 (shared kernels, ~10 000 lines with callees), E + 0x10 per shape (16-byte stride).
Still NOT DECODED for the decomposition: CalcEntIntersect's hit order for arc pairs (only the first hit is kept; the
region is linearised there). Closed in pass 3b below: CalcLnLnIntersectPt, FUN_180125ec0.

**Pass 3b (2026-11-07): unit oracle, calling convention, region preparation** (tools/peloader/mcce_gp.c, README "Guided
planner unit oracle"; port: viewer/mcce_dyn.c gp_prepare / gp_degenerate / gp_snap_ends):
- Real signature (disasm 0x1800f10c1-0x1800f10e8; the decompile's parameter list is wrong): rcx unused, xmm1 d, xmm2
  step, r9b both, stack E, A, G, out, X (X = the 7th argument of FUN_1800f1580). E is followed in memory by the exit's
  end tangent: the caller FUN_180044340 keeps E at -0x18(rbp) and writes FUN_18011ffd0(exit, last, 1.0, -0x8(rbp), 1e-10)
  next to it (disasm 0x180044513-0x180044532), so FUN_180136370's "E + 0x10" (lea 0x10(%r14), disasm 0x1800f12df) is that
  unit tangent. Stay-down (5) FUN_18005fb50: what lies at its E + 0x10 NOT DECODED. step = C+0x1e0 (= P+0x1d8).
- CalcLnLnIntersectPt (l.3611-3640): den = -(d2.y d1.x) - -(d2.x d1.y), parallel when |den| < 1e-14; s = (-(w.x d2.y) -
  -(w.y d2.x)) / den, point = s d1 + p1, u = (w.y d1.x - w.x d1.y) / den; returns |p2 + u d2 - point|^2 <= 1e-12.
  CalcLnSegLnSegIntersect (l.12820-12897): that, then rejects a parameter < 0 / > 1 whose point is farther than tol from
  that end (squared distances dy^2 + dx^2); parameters returned unclamped. CalcEntIntersect (l.6348-6388) dispatches on the
  id pair ('L' 'L' -> that; 'A' 'L' / 'L' 'A' -> CalcArcLnSegIntersect, 'A' 'A' -> FUN_1800a4460).
- FUN_180125ec0 (l.158095-158117) -> FUN_180125b80 (l.157983-158092) per shape: CTMatrix::operator*(CPt2) (l.5279-5300:
  x' = y m[0x18] + x m[0] + m[0x30], y' = x m[8] + y m[0x20] + m[0x38]; RotationMx l.11801-11822 = cos, sin, -sin, cos) on
  'L' ends, 'P' point, 'A' centre; an arc's radius := |start' - centre'|, start angle := atan2 (only when |dx| or |dy|
  > 1e-6), sweep kept; then UpdMinMaxSh (l.3249-3330) = SetChnMinMax (l.9234-9395) per entity, shape box = first entity's
  box then running min / max. The decomposition's scan-line ends come from that box (ULP-sensitive: a different box
  flipped funnel ties on buckle-v8 42).
- FUN_1800f3de0 (132267-132320) per shape: FUN_1800f3440 (131879-131918: first entity with the largest |raw +0x30 - raw
  +0x20|, strict >) and FUN_18014fd50 (start the closed shape there, index > 0); FUN_1800f3520 (131922-132262): positions
  {entity 0, t 0}, FUN_18013d280's nearest point to E and to A when |P - q| < 1e-4, {last, t 1}, std::sort with
  FUN_1800f0f70 (disasm: entity, then t); fewer than 3: the shape is the only piece; else FUN_180146e20 sections between
  consecutive positions into fresh CSh (zero header), empty ones erased; FUN_18014f740(piece, 0.1 d, 0, 0) on each (the
  collinear merge of ChaikinSmoothSh); FUN_1800f21d0 (131208-131304): two or more pieces joined into one shape with the
  original header (CSh::operator=, entities cleared, FUN_180146b10 appends + GetMinMaxViaShEnt); the shape is swapped with
  the first result. The effect: a collinear merge at 0.1 d that keeps E and A (and the steepest edge's start) as vertices.
- FUN_1800f2b40 (131554-131604): fails when a shape intersects itself (FUN_18012f2b0 = FUN_18012f3b0 fills an ISIS list,
  limit 2: NOT DECODED), when FUN_180126ac0(0.001) gives the LAST shape an area < 1e-6 or it has < 3 entities, or when
  any shape's +0x50 differs from the computed dir. FUN_180126ac0 (158544-158718): trapezoid sum from the last entity's end,
  per entity: start gap d^2 > tol^2 stops (area 0, dir 0), a gap > 1e-20 adds (sy + py)(px - sx), a line adds
  (y2 + y1)(x1 - x2) (arcs: FUN_1801270e0 NOT DECODED); dir = sum > 0 ? 1 : -1, area = |sum / 2|.
- FUN_1800f2ca0 (131608-131791): for P in (E, A) with FUN_180112a00(G, 1e-5, P) != 2: q = FUN_18013cfe0 nearest (entity
  k); dP = |P - q|, d1 / d2 = |q - raw start| / |q - raw end|; if d1 < dP or d2 < dP or either < 1e-5: (d1 < d2: k := k - 1
  cyclic) else a copy of entity k with end q is inserted before it (FUN_180071580) and entity k's start := q; then entity
  k's raw end and entity (k + 1) % n's raw start := P (SetChnMinMax); any change: FUN_18013c390 (GetMinMaxViaShEnt per
  shape).
- Unit oracle agreement (buckle-v6 32 / 36, v8 42, v10 3 / 10, v12 22; 90 planner calls; MCCE_GPFEED=1 for the stages after
  FUN_1800e9790): orient, split, linearise, prepare, degenerate, linearise2, f2ca0, angle, rotation, decomposition,
  channel, funnel, search, rotation back within 1e-6 on every call; bit-identical except the plain route FUN_1800f1580
  (12 calls with 1-ulp differences, one reaching the funnel record of the route end). Not ported: FUN_1800e9790 (cuts),
  FUN_180136370 (post-process), FUN_180113300's verdict on the result. The DLL's planner succeeds on 71 of the 90 calls; on the other 19 the post-processed path leaves G (FUN_180113300 = 0x6) and the plain route is used.

**Pass 4 (2026-11-07): FUN_1800e9790 decoded and ported; the planner returns its path** (viewer/mcce_dyn.c, block
"guided planner": gp_corners / gp_end_cut and the exact intersection kernels; unit oracle stages `corners`, `cutset0/1`,
`cutbool0/1`, `cuts`, `inside`, `result`):
- FUN_1800e9790(d, step, E, A, G) = FUN_1800ec930(step, E, A, G) then FUN_1800e9440(d, step, E, A, G) (disasm 0x1800e9790-
  0x1800e97f3). E and A are each followed by a raw tangent: E+0x10 = FUN_18011ffd0(exit, last, 1.0, 1e-10), A+0x10 =
  FUN_1801204a0(approach, 0, 0.0, 1e-10) (l.40812-40837; not normalised; the GPIN dump line `Ad`).
- Concave corners FUN_1800ec930 (128104-128155): every entity k of every shape (counts re-read); FUN_1800eb810 (127554-127640):
  tangents in / out at (k, t 0) (FUN_18011ffd0 / FUN_1801204a0: 'L' p2 - p1, 'A' (point - centre) |sw| turned +-90 deg;
  degenerate (|v|^2 <= tol) entities skipped; l.155017-155323), both non-zero (|x|, |y| <= 1e-12 = zero, disasm
  0x1800eb8ac-0x1800eb90d), turn = atan2(out) - atan2(in) wrapped; turn < 0: V = the entity's start (arc: c + r (cos sa,
  sin sa)), angle = atan2(out) - (turn + pi) / 2; FUN_1800eb6e0: V within 1.5 step of E or A (squared, strict) -> skip.
  FUN_1800e9820 (126642-126703): bisection hi = step, lo = 0, s = step; per trial ok -> lo = s else hi = s; s = mid;
  stop after a first-trial success or after 10 trials; if a result exists (the last success, kept across trials):
  trial at s - 1e-4, then the shape is swapped with the result and k := its entity index (the loop continues at k + 1).
  FUN_1800ec230 (127836-128100): s >= 1e-7; centre = V + (s 0.75) (cos a, sin a) (s * 0.75 first: disasm 0x1800ec390);
  FUN_1800ebc20: circle of radius s, 4 arcs (sa pi, pi/2, 0, -pi/2; sw -pi/2), dir -1, UpdMinMaxSh; FUN_180133c70(shape,
  circle) >= 2 hits; key = entity + t / 2 on the shape side; i = first index whose cyclic predecessor p has key(p) < K <
  key(i) (i = 0 when key(n - 1) < K or K < key(0)); p and i must be ring neighbours (+0x30 / +0x34); piece FUN_1800ebae0
  (ClearAndClearObjSh, shape section p -> i, RevSh, circle section p -> i, dir -1); FUN_1800eb550: with > 1 shape a hit
  of the piece with another shape rejects; FUN_1800eb750: centre within s + 1e-7 of E or A rejects; FUN_1800ebe50 writes
  {header copy; (j != 0) shape (0, 0) -> hit p; circle p -> i (k := count - 1); shape hit i -> (i == 0 ? hit p : (last,
  1))}. FUN_180146e20 ends with GetMinMaxViaShEnt (l.176056).
- CalcEntIntersect (exact now for the planner): CalcArcLnSegIntersect (8395-8551) = FUN_1800a9000 (92047-92171: foot of the
  perpendicular via CalcLnLnIntersectPt with the line c + u (-d.y, d.x); two points f -+ h d/|d| when D <= (r - 1e-14)^2
  or D >= (r + 1e-6)^2 (none if D > r^2), else one at the foot; a point more than 1e-6 off r dropped; |t0 - t1| < 1e-12
  -> one; FUN_1800a9490 (degenerate line) NOT DECODED, ours: none) + per point the segment test (t outside [0, 1] only
  within tol of that end), FUN_1800a4820 (89869-89925, arc parameter with tol / r radians of slack; 1 - t for sw < 0) and
  the arc-point check (1e-12 squared); FUN_1800a4460 (89743-89836) = CalcCirCirIntersect (13288-13380; from the smaller
  circle's centre; h^2 < -1e-7 none, < 1e-13 one; FUN_1800a5150 has no effect) + FUN_1800a4820 on both arcs; common tail:
  two kept points closer than tol, or a bad first parameter, collapse to one.
- End cuts FUN_1800e9440 (126522-126627): mode 0 (P = A, T = A+0x10) then 1 (P = E, T = -(E+0x10)): X = FUN_1800e9c60;
  R = FUN_180153b30(1e-6, G, X) (G minus X); FUN_180112860(R) == 1 -> G := R, FUN_1800f33a0. FUN_1800e9c60 (126764-127048):
  size = step + 1e-4, FUN_1800ea7b0 (127051-127281: q = nearest point of G to P; |P - q| > 0.1 -> size := min(size,
  0.4 |P - q|); else the inward bisector ray at q (a_in + turn / 2 + pi/2, length = the last shape's box diagonal + 1),
  its hits in ray order, midpoint class FUN_180112a00(1e-5): 1 next, 4 stop, 2 -> size := min(size, |q - hit| / 2)), then
  size + 1e-4; |T| < 1e-12 -> none; FUN_1800e9a80 (126709-126761, the same bisection, no final trial) over FUN_1800eaf80
  (127283-127448: circle of radius s at P + s n; centre >= s - 1e-5 from E and A; hits with G (FUN_180134270): none ->
  success, nothing kept; a consecutive hit pair whose circle section is outside G (FUN_1801130f0 == 4, full = n < 2) with
  both ends farther than d + 1e-5 from P fails) on n = (-t.y, t.x) then (t.y, -t.x); arc radii > 1e-4 shrunk by 1e-4;
  two circles: the quadrilateral P' + r1 n2, P' + r0 n1, Q + r0 n1, Q + r1 n2 (P' = P + 1e-4 t, Q = P + min(r) / 2 t;
  dir -1) and SoPockUnionAr(1e-6) of the three; else the circles as they are.
- NOT DECODED, ours (marked): the cores of SoPockUnionAr and FUN_180153b30 - our exact boolean with the source pieces kept
  (no collinear / co-circular merge; the DLL's outputs keep piece boundaries of their sources, §6.13.1); loop starts and
  orientation of the raw result differ (FUN_1800f33a0 orients afterwards). The DLL's union output boxes are the stale
  pre-shrink entity boxes (+1e-4).
- The planner's verdict (disasm 0x1800f12f1-0x1800f1320) on our path: FUN_180113300(out[0], G, 1e-5) & 4 == 0. The post-process
  FUN_180136370 (166702-166789) stays NOT DECODED: FUN_180136680 per shape clears the output, then for each entity i
  FUN_1801368c0 (166850-166999): start tangent FUN_1801380f0(d, E+0x10, path, i), then a binary search on the end index j
  (FUN_180138510 end tangent, FUN_1801353a0(d, G, path, out, i, j, tangents, candidate) success -> keep the candidate and
  search further), finally FUN_180135610 for j = i + 1; no candidate -> entity i copied, else FUN_1801351b0 appends it
  (166114-166188) - a tangent-continuous replacement of entity runs inside G, built by FUN_180138910 (167879-, arc
  kernels FUN_1800a72d0 / FUN_1800a8660).
- Unit oracle (90 calls): corners bit-identical on 90/90; cutset0 / cutset1 (FUN_1800e9c60) equal up to the loop start on
  90/90; the region after the cuts equal up to the loop start on 87/90 (3 calls of buckle-v6 op 32: our boolean keeps one
  extra 1e-5 piece); without MCCE_GPFEED the search output equals the DLL's on 82/90 (rest: ULP-level vertex differences
  from our boolean's crossings); with MCCE_GPFEED=1 (fed at `cuts`) every later stage as before (one channel diff from the
  plain route's ULPs, buckle-v6 op 36 call 2).

**Pass 5 (2026-11-07): the post-process FUN_180136370 decoded and ported** (viewer/mcce_dyn.c, block "post-process
FUN_180136370", gp_post; unit oracle stage `post`):
- Call (disasm 0x1800f12df-0x1800f12ec): FUN_180136370(d, G = the planner's *input* group (param_7, not the prepared copy),
  E + 0x10 (the exit end tangent), out). Per path shape s (FUN_180136680, 166792-166848): a new output shape, i = 0; while
  i < n: FUN_1801368c0(i, &j) -> candidate empty: entity i copied (GetMinMaxViaShEnt), i + 1; else FUN_1801351b0 appends
  it, i = j. The output's +0x50 (dir) = the path's. Shape k reads the tangent at Ed + 16 k (only k = 0 is in bounds; the
  planner's verdict needs exactly one shape).
- FUN_1801368c0 (166850-166999): ti = FUN_1801380f0 (out empty: Ed; else the tangent arriving at out's last entity's end,
  FUN_18011ffd0(.., 1.0, 1e-10); normalised; the 3-point-arc branch needs Ed == NULL - never in the planner, not ported);
  lo = i, hi = j = n; loop: tj = FUN_180138510(j); FUN_1801353a0(i, j) fails -> hi = j, step = (j - lo) >> 1, j -= step;
  succeeds -> the candidate kept, lo = j, step = (hi - j) >> 1, j += step; until step == 0. No candidate: j = i + 1 and
  FUN_180135610(i, i + 1) (success -> *j = i + 1); else *j = lo.
- FUN_180138510 (167758-167878): closed path, j == n, out non-empty -> FUN_1801204a0(out, 0, 0); else FUN_18013a020(path, j,
  2 back, 0 forward, 1e-5) (the start of entity j, or the end of the last one; then entity starts walking back, points
  within 1e-5 per coordinate (strict) skipped, reversed; FUN_18013a3e0 168776-168914): 3 points -> FUN_1800a7310 (centre of
  the 3 points, arc p0 -> p2, sweep wrapped once) and FUN_18013a8e0 (every chord midpoint within d radially of the circle)
  -> the arc's end tangent; 2 points and Ed -> FUN_1800a6f70(Ed, p0, p1) (centre = normal at p0 x the perpendicular
  bisector) + FUN_18013a8e0 -> its end tangent; else the last chord; normalised.
- FUN_1801353a0 (166191-166265), for i < j: FUN_180137b70 (167442-167623): P = start of i, Q = start of j (end of j - 1 at
  j == n), X = (P, ti) x (Q, tj) with s >= 0, u <= 0; L3 dir = ((X + tj) + (X - ti)) / 2 - X (disasm 0x180137e18-
  0x180137e61; the decompile's order is wrong); C = L3 x the normal at P (param e8 >= 0) or at Q (e0 >= 0), the one with
  the smaller parameter (e0 <= e8 -> Q's); then L1 = (P, X - P), L2 = (Q, Q - X), L3 = (X, C - X).
  FUN_180139a40 (168467-168635): sign of the turn (X - P) x (Q - P) must be +-1; vertices i + 1 .. j - 1 dropped from the
  front while within d of segment P X (FUN_1800ad500) and from the back while within d of X Q; the first far one from
  the front must be on the turn's side, from the back on the other sign of its own cross product (see code); the nearest
  vertex to X is remembered (the back loop records r0, not r1: disasm 0x180139dd2); everything dropped -> range = that
  vertex. FUN_1801395e0 (168321-168466): R = FUN_180137a60 (distance of X + L3.d from L1), tmin = FUN_180137880 (scale of
  the circle through X + 0.99 d along L3, clamped [0, 1]); R or tmin < -0.5 fails; per range vertex: FUN_18013ab60 (on the
  outer side of P X / X Q -> pulled to the nearest such segment point, farther than 1.1 d fails), FUN_180137090 (largest
  root of |X - V + t L3.d|^2 = t^2 R^2, FUN_1800afd30) clamped to [tmin, 1]; the mean; empty range: FUN_1801371c0 (the
  turn between the normals of P X and X Q; min(d / (1 - cos(turn / 2)), R) / R). FUN_18013ae30 (169081-169297): C =
  X + s L3.d, T1 / T2 = the feet on L1 / L2, cand = line P T1, arc (C, |C - T1|, atan2(T1 - C), sweep to T2 with the turn's
  sign), line T2 Q, pieces under 1e-6 dropped. FUN_180136090 (166606-166701): path entities i .. j - 1 sampled at 0, 1/3,
  2/3 (entity i from 1/3), each within d of cand (FUN_18013d280 nearest point), and FUN_180113300(cand, G, 1e-5) & 4 == 0.
  Then FUN_180138910 (167879-168320): cand with > 1 entity; b10 = first piece a line shorter than d and out empty or ending
  in an arc; b11 = last piece a line shorter than d; S = cand's start (or the first line's end when only b11), Ts = the
  first piece's start tangent, D = cand's end, Tl = the last piece's end tangent; b11 or last piece an arc: arc =
  FUN_1800a72d0 = FUN_1800a6f70(Ts, S, D); else FUN_1800a8660 (arc from S tangent to Ts and to the line (D, Tl): centre =
  normal at S x the bisector at the lines' crossing) and a closing line to D; the first line kept when only b11; accepted
  by FUN_180136090 -> it replaces cand.
- FUN_180135610 (166266-166605): entity i alone: FUN_180137510 (167256-167361): turn = atan2(D - S) - atan2(ti) (|turn| >=
  1e-12), k = 1 - cos|turn| (|turn| >= pi/2: 1 + cos(pi - |turn|)), r = d / 4 / k, min with FUN_1800a8c00 (the radius of
  FUN_1800a6f70's circle; its failure fails), centre S + r n (n = ti turned towards D); tangent points from D =
  FUN_1800a9600 (CalcCirCirIntersect with the circle on C D); none -> D; the smaller |sweep|; cand = arc + line to D;
  FUN_180136090 (i, i + 1).
- FUN_1801351b0 (166114-166188): FUN_180136d40 merges cand's first entity into out's last (lines: cand's start within 1e-7 of
  the joined line; arcs: radius and centre within 1e-7 and (F.sw > 0) * L.sw >= 0 - as compiled, disasm 0x180136f04-
  0x180136f25 - sweeps added), then FUN_180146e20(cand, merged, 0, n - 1, 1, out, !merged).
- Unit oracle (6 ops, 99 calls): with MCCE_GPFEED=1 stage `post` bit-identical on 99/99 (buckle-v6 op 32 call 0: the r 1.35
  arcs), `inside` / `result` 99/99 (the DLL rejects 1 call: inside 0x6, buckle-v8 42); without the feed `post` 95/99 (the
  4 differences follow from the search's input, i.e. our boolean in the cuts), `inside` / `result` 99/99.
