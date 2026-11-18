# Scallop (HST op 132, UI style 3, kernel style 2) - decode and porting spec (2026-10-26)

Status: **partial decode** (2026-11-12: the 3D boundaries path / Shallow Area Detection added, section 10). The parameter mapping, the plan choice, the pass-generation structure and the distance used for the
constant stepover are decoded. The weave contour extraction, the trimming, the pocket split rules and the linker are **not**.
Nothing here is ported.

Rule for this file: every statement cites decompiled code or Mastercam's own data. Anything taken from data and not from code
is marked [measured] or [inferred]. Anything not found is marked NOT DECODED.

## 0. Sources and citation keys

- **mk l.N**: `decomp/mckernel_all.c`.
- **ms l.N**: `decomp/mcmsurf_all.c`.
- **cs l.N**: McHmm range 180333000-18033c000 (the constant-stepover plans, 66 functions), decompiled for this file with
  `tools/ghidra_scripts/DumpRange.java` on a private copy of `~/mastercam-re/ghidra_hmm`.
  - Saved as `decomp/scallop/cs.c` (decomp/ is local, gitignored).
- **off l.N**: McHmm range 18025c000-18026b000 (the CWeave offset engine, 134 functions), `decomp/scallop/off.c`.
- **d1-d5**: single functions from DecompAt.java, `decomp/scallop/`:
  - d1.c: FUN_180238070, FUN_1802887a0, FUN_18013e0f0;
  - d2.c: FUN_18030ec00, FUN_1802f5dc0;
  - d3.c: FUN_1803092f0, FUN_180305960, FUN_1803065a0;
  - d4.c: FUN_18030ed30;
  - d5.c: FUN_1802f8bf0.
- **reg**: `decomp/hmm_params.tsv` (McHmm parameter id -> name).
- **res**: dialog resources, `~/mastercam-re/regress/old_opdlg_b12/exports/mcmsurfres_dialogs.tsv` (dialogs 1328, 1342).
- **help**: Mastercam help, `~/mastercam-re/chm/sr5axfields/Fields_HMM/*.htm`.
- **Record offsets.** "TP+x" is the TPHSTToolPath field that mckernel reads. Its record offset uses the block shifts of
  hst3d_port_plan.md 1.1 / 6.7 / 7:
  - doubles: TP = record - 0x1078 up to 0x1258, and record - 0x1080 from 0x1268;
  - bytes: TP+0x45c = record 0x16bc, 0x45d = 0x16bd, 0x464 = 0x16c9, 0x46f = 0x16d4, 0x470 = 0x16d5, 0x486 = 0x16ef. These
    are [inferred] in the earlier docs.

  The mccore copy op -> TP is NOT DECODED. Every byte mapping not listed above is marked [inferred] below.

## 1. Op record -> kernel parameters (FUN_180071314, style-2 branch mk l.87912-87986)

| kernel parameter (McHmm id) | value | record field (default op: Mill_MM op 68 / Mill_Inch op 70) | source |
|---|---|---|---|
| MachConstStepoverVertical (0x1bae) | TP+0x1b8 if > 1e-12, else 2.5 (metric) / 0.1 (inch) | 0x1230 Stepover (1.0 / 0.05) | mk l.87913-87928; defaults mk l.87618-87619 (uVar31/32 = 0.1, metric 0x40040000 = 2.5) |
| MachConstStepoverHorizontal (0x1bad) | the same value | 0x1230 | mk l.87929-87934 |
| MachConstLimitOffsets (0x1a47) | false | - | mk l.87935-87937 |
| PosZHi / PosZLo | as Waterline (hst3d_port_plan.md 6.7 / 7): job range or Use Z depths TP+0x464 (0x16c9) with TP+0x1c0 / 0x1c8 (0x1238 / 0x1240) + job +0x28; PosZLo + param_5 | 0x16c9 (0 / 0) | mk l.87938-87948, l.87320-87340 |
| MachTrimZTolerance (0x1b39) | the job tolerance (job +0xa0), only when it is below 0.1 (metric) / 0.0039370 (inch, 0x3f70204081020409) | - | mk l.87949-87962 |
| ContactAngleLo / Hi | mode byte param_2[2]: 1 = degrees of param_2[0..1]; 2 = param_2[0..1] as given; else degrees of min / max(TP+0x1d0, TP+0x1d8) clamped to 0..pi/2 | 0x1248 / 0x1250 (0 / 90 deg) | mk l.87963-87985, l.87266-87302 |
| PosContactOnly (0x1ae1) | false, unless TP+0x46a | - | mk l.87986-87989 |
| ToolThicknessXY / ToolThicknessZ | the common non-Horizontal branch: XY = max(XY, Z stock) | 0x13c0 / 0x13c8 (1 / 1 and 0.05 / 0.05, record offsets [inferred] in cam3d_rough.c c3_values) | mk l.87577-87611 |
| StepMachTolerance (0x1acc) | job +0xa0 (+ local_848, set only for style 5) | record NOT DECODED (hst3d_port_plan.md 6.6) | mk l.87612-87617 |
| tool | ToolCornerRadius / Diameter / ... through FUN_180074e18 when bVar18 | 0x350 / 0x358 | mk l.87481-87527 |

The angle mode comes from the macro builder (mk l.93156-93229):
- with **"Use boundaries as drive curves, then collapse"** (section 2.1) the passes plan gets mode 2 with 0 / 90 (local_c8 = 0.0,
  uStack_bc = 0x40568000 = 90.0);
- the op's angles go to the Shallow Area Detection plan instead (FUN_180079814 mk l.90448-90560: ContactAngleLo / Hi = degrees
  of min / max(TP+0x1d0, 0x1d8) clamped 0..pi/2, plus its own PosZ and stock);
- without the option, the passes plan gets the op's angles.

**McHmm plan defaults** (parameters mckernel does not set). They are default expressions, transcribed from FUN_18038b890 calls
(the string pieces were read with MemDump: ", " 18075fdd4, " ? " 1807625a0, " : " 1807625a8, " + " 1807624c0, " > " 180785868,
" - " 180784900, ")" 180762508, "!" 180762470):

- MachConstNumberOfOffsets (0x1baf) = "%d" of 0 (Ordinal_4612 with 0). MachConstLimitOffsets = "false". Source: FUN_18030ec00 d2
  l.17-29.
- MachConstStepoverHorizontal (FUN_1803092f0 d3 l.27-52) = `CuspHeightEnabled ? GetDroppedPassesStepOver(ToolCornerRadius,
  CuspHeightTarget, 0.0) : ToolStepOver`.
- MachConstStepoverVertical = MachConstStepoverHorizontal.
- MachConstStepover2D (0x1bd8) = empty.
- CuspHeightTarget (0x1ea4) = `CuspHeightEnabled ? CuspHeightDefault : GetDroppedPassesCuspHeight(ToolCornerRadius,
  max(H, V), 0.0)`.
- CuspHeightDefault = StepMachTolerance * 2.0.
- CuspHeightEnabled = false (d2 l.335-339).

  mckernel sets H and V explicitly, so none of the cusp expressions apply to a Mastercam op.
- Step-forward set (FUN_18030ed30 d4, called from FUN_1802f5dc0 d2 l.522 with local_res10 = 0 for plans 0x140c / 0x1445;
  local_res10 = 1 only for 0x140a / 0x1410 / 0x1434, d2 l.84-89):
  - MachFibreOffsetEndpointTolerance (0x1b52) = min(H * 0.02, 0.002);
  - MinHorizontalStepForward (0x1b4c) = FibreTol * 0.45;
  - MaxVerticalStepForward (0x1b4f) = 0.9;
  - MachStepOffsetContactPointChordChange (0x1cc5) = 0.7 (string 1807863d8);
  - MidpointOffsetTolerancePositive (0x1cc6) = max(H * 0.2, FibreTol * 1.9);
  - MidpointOffsetToleranceNegative (0x1cc7) = min(H * 0.4, FibreTol * 6.3);
  - ParallelnessConnect (0x1abf) = 0.03;
  - MachCellSubdivisionLimit (0x1aba) = 140 (0x8c).
- **MachWeaveResolution** (0x1b3a) for 0x140c / 0x140d / 0x1445 = `MaxHorizontalStepChange * 0.7` (d2 l.459-466).
  - MaxHorizontalStepChange (0x1bd9) for these plans (FUN_180305960, d2 l.377-381; d3 l.795-818) = `2.0 * (TSR > StepMachTolerance
    ? sqrt((2.0 * TSR - StepMachTolerance) * StepMachTolerance) : TSR)`.
  - TSR = ThickenedShaftRadius (0x1c93) = ToolShaftRadius + ToolThicknessZ (d2 l.144-150).
  - Also from FUN_180305960:
    - StepMaxNormalChange = 15 deg;
    - StepContactPointChordChange = 2 sin(15/2);
    - StepMinHorizontalChange = 0.003;
    - StepMaxVerticalChange = 2.0;
    - StepMachTolerance default = Tolerance (but mckernel sets it).
  - The value of ToolShaftRadius for a Mastercam tool: NOT DECODED (FUN_180074e18).
- Mach3DBoundaryTolerance (0x1bb1, 3D plan only) = 0.1 (FUN_18033a950 cs l.4649-4655).
- MachBoundaryArcTolerance (0x1bb2): FUN_1802f5dc0 d2 l.241-262, value NOT DECODED.

## 2. Algorithm

### 2.1 Plan choice (FUN_18007d7c8 mk l.93079-93100, l.93155-93240, l.93630-93690)

- bVar9 = 3D Boundaries, when either:
  - TP+0x463 (stock model) and TP+0x475 = 2 -> Detect Theoretical Rest Areas 0x1432 feeds DriveBoundaries; or
  - **TP+0x479 = 1** (read for style 2 only, mk l.93155-93156) -> **Shallow Area Detection Plan 0x140a** (params FUN_180079814)
    feeds DriveBoundaries.
- Otherwise -> **Constant Stepover Passes from 2D Boundaries 0x140c**, inputs "ConstraintBoundaries", "Surfaces" (= ekPlan3 or
  ekPlanTcb3, + ekPlan2).
- bVar9 -> **from 3D Boundaries 0x1445**, inputs "DriveBoundaries", "ConstraintBoundaries", "Surfaces".
- When the check surfaces of FUN_18006f480 are present (TP+0x472), ekPlan4b and FUN_180075ac4 (SurfaceTrimKeepExterior = true)
  follow.
- **TP+0x479 = record 0x16e1** [inferred]. The evidence:
  - 0x16e1 is "Use boundaries as drive curves, then collapse" (dialog 1342 control 696, res; MapToOpEnt FUN_1800a09b0 ms
    l.105405: op +0x3769 = check; InitPage FUN_1800a0920 ms l.105369; record = op - 0x2088, as for 0x16bd / 0x16d4 / 0x16d5 in
    FUN_180088bd0);
  - 0x16e1 - 0x479 = 0x1268, inside the known byte shifts 0x1265..0x1269;
  - help: the option gives "smoother, more natural tool motion around drive surface boundaries" and turning it off restores the
    pre-X2 MR1 toolpath.
- **The default op has 0x16e1 = 1** [measured, Mill_MM op 68 / Mill_Inch op 70]. So the default op takes the **3D path**
  (0x140a -> 0x1445).
- TUTORIAL 2 SWARF_METRIC op 4 has 0x16e1 = 0, so it takes the 2D path [measured].
- The "boundaries", per path:
  - **2D plan**: the plan's check (FUN_180339740 cs l.3965-3994) copies ConstraintBoundaries into DriveBoundaries when
    DriveBoundaries is empty. So the drive boundaries = the containment (or, without one, the Bounding Box Plan of the surfaces).
    The Bounding Box Plan uses OffsetDistance = |XY stock| + D/2 (FUN_18006fee0) + 2 x TP+0x220, styles not in mask 0x209,
    mk l.92862-92868.
  - **3D plan**: DriveBoundaries = the output of the Shallow Area Detection Plan with the op's angle range. Its structure is decoded
    in section 10 (2026-11-12); the weave cell classification itself stays NOT DECODED.

### 2.2 Evaluate (slot [1]: 2D FUN_180339890 cs l.4034-4200; 3D FUN_18033b3f0 cs l.4946-5127)

1. **Setup** FUN_180333880 (cs l.235-792):
   - with EnableTrimToBoundaries (0x1b3f), the ConstraintBoundaries plan's "Boundaries" (0x1b44) become an EvaToolBoxFrags at
     Eva+0x120 (cs l.399-470);
   - DriveBoundaries (0x1a93) give a CTessCollection, copied to Eva+0x10e8, flag Eva+0x1100 (cs l.604-690);
   - its EvaToolBoxFrags goes through **CFindPockets** (FUN_180489280), which groups the boundary curves into **pockets**
     Eva+0x1108 (a list of index lists) (cs l.760-770);
   - a toolbox is built with 3 x MachWeaveResolution (FUN_1802f5b70 / FUN_180221480, cs l.771-784).
   - The CFindPockets grouping rule is NOT DECODED.
2. **Per pocket** (2D: FUN_180339d50 cs l.4202-4512; 3D: FUN_18033b900 cs l.5129-5654):
   - the pocket's curves -> EvaToolBoxBoundaries (FUN_1800caff0). Its flag +0x450 = 0 for 2D, 1 for 3D; in 3D +0x108 =
     MachBoundaryArcTolerance (cs l.4311, l.5170-5176);
   - the main offset parameter block (FUN_18026a190 off l.10006-10074, 0x78 bytes) at Eva+0xff0 is made from:
     H = MachConstStepoverHorizontal; resolution; ratio = H / V (0 with MachConstStepover2D); MaxVerticalStepForward;
     MinHorizontalStepForward; FibreTol; Mid+; Mid-; chord; ParallelnessConnect; CellSubdivisionLimit (cs l.4344-4476).
     Its fields:
     - +0x18 step = H;
     - +0x20 search radius = 2H (if 2H <= 0: 2 (res + max(H, 0)));
     - +0x28 = 2 (res + max(H, 0));
     - +0x30 ratio;
     - +0x38 MaxV;
     - +0x40 MinH;
     - +0x48 FibreTol;
     - +0x50 = H + Mid+;
     - +0x58 = H - Mid- (or -1 if <= 0);
     - +0x60 = chord^2;
     - +0x68 parallelness;
     - +0x70 cell limit.
   - a **CWeave** (FUN_18025d440 off l.952-1144) is built over the pocket with MachWeaveResolution spacing:
     - margin: 2D: 0, cs l.4480; 3D: 2 x res + the first-offset radius, cs l.5339-5341;
     - extents Eva+0x10a0 / 0x10b0, NOT DECODED;
     - it is queued in Eva+0x10c0;
     - FUN_180260490 fills its nodes. It calls the toolbox query FUN_180222920 per fibre point, so the nodes are **tool
       positions over the drive** [inferred from the call pattern; the per-node maths is NOT DECODED].
   - **First pass**:
     - 2D: one offset with step 0 (FUN_18026a190 with step 0, ratio 1, MaxV / Mid / chord / parallelness = -1, MinH =
       Eva+0xcb8, cs l.4502-4510), i.e. the pocket boundary itself on the weave.
     - 3D: up to 4 attempts at radius r = Mach3DBoundaryTolerance, then r x 1.5 per retry (0.1, 0.15, 0.225, 0.3375 by default),
       each with Mid+/- scaled by r / H and flags +0x74 = +0x75 = 1 (FUN_18033aaf0 cs l.4851-4943). The log says "First offset
       failed. Retry with rad = ", then "All first offset attempts failed on this profile." (cs l.5536, l.5579).
3. **Offset loop**: while the queue Eva+0x10c8 is not empty, take the back entry (`*(list+8)`, the last node) and call
   FUN_1803343e0 with the main block (cs l.4140-4147 / l.5065-5072). So the newest spawned pocket is processed first.
   Per call (FUN_1803343e0 cs l.794-1881):
   - a **ToolBoxSetRaw** is built from the previous pass's curves (param_2[0x4d] grids), with MachBoundaryArcTolerance (cs
     l.893-1000);
   - **distance field** FUN_1802601f0 (off l.3117-3207): a 5-stage pipeline over the weave rows (FUN_18025f890, FUN_180265980,
     FUN_180265790, FUN_18025ffe0, FUN_18025fde0). Stage FUN_180265980 (off l.6852-7207) sets, for each node and each fibre
     crossing P:

     **value(P) = d3(P, previous pass) - step**

     - d3 = **MeasurePointDistance3D** (FUN_180238070 d1 l.1-256): the minimum of |P' - Q'| over the previous pass's segments
       (point-segment, FUN_180059560) found in the 2D bucket grid within the search radius 2H.
     - The primes scale Z by the ratio: (x, y, ratio z).
     - A negative value sets bit 0x80000000 on the node.
     - When step = -1 the value is instead the signed 2D interior distance to the boundary toolbox (FUN_1802887a0 d1 l.258-303,
       MeasureInterior2D).
   - **contours**: FUN_180262430 (with Eva+0xcb8), then FUN_1802666a0 (loops), FUN_1802669e0 (groups of loops) and
     FUN_180262c20 (cs l.1286-1296). These extract the 0-level of the field. Their rules are NOT DECODED.
   - per group: a test for first-offset blocks (+0x74), comparing the min distance with -(FibreTol + MinH) ("Getting back thought
     to be discarded profile with an island", cs l.1511-1569).
   - trimming to the constraint toolbox: FUN_1802616b0 / FUN_18011b050, with EnableTrimToSurfaces 0x1b4e and
     TrimToSurfaceSimultanious 0x1ce7 (cs l.1592-1640). NOT DECODED.
   - output: FUN_1802ed680 (cs l.1743) appends the passes to the plan output.
   - stop for this pocket (cs l.1744-1760) when no contour is found (iVar17 = -1), or bVar12, or (MachConstLimitOffsets and
     MachConstNumberOfOffsets <= the pass count param_2[0x50]).
   - with 2 or more groups ("Possible spawning offset pockets: ", cs l.1832), each group becomes a new queue entry (FUN_1803361f0,
     FUN_180263770); otherwise the entry continues in place.
4. After the loop, when ShaftProfileEnable (0x1c9f) is set: FUN_1802ec5a0 (cs l.4167-4175), NOT DECODED.

### 2.3 Stepover / cusp maths

- **Constant 3D stepover.** Each pass is the iso-set at 3D distance H from the previous pass (2.2 step 3). The distance is
  between tool positions, with Z scaled by H/V; H = V here, so the ratio is 1 and the distance is plain 3D. It is measured on
  the weave of tool positions, not on a 2D projection, and not as a surface geodesic. There is no slope-compensation formula: the
  3D distance replaces it.
- The help ("a 3D value measured along the surface profile") agrees.
- Whether "tool position" means the tool centre or the tip is NOT DECODED (FUN_180260490 / FUN_180222920).
- **Dialog scallop height** (display only; FUN_180088430 ms l.88940-89010):
  - r = corner radius (op +0x23e0), flat f = D/2 - r (0 if < tol);
  - if r <= 0: 0;
  - if D < s: the machine group's +0x1240;
  - if f > tol: s' = s - 2f, and the height is 0 when 2f > s;
  - **h = r - sqrt(max(0, r^2 - s'^2 / 4))**.
  - The inverse (height -> stepover, FUN_1800887b0) was not read: NOT DECODED.

### 2.4 Closed / open, direction, order

- Passes are offset loops of the boundary. Whether a trimmed loop is output open at the containment is NOT DECODED (FUN_1802616b0).
- **Generation order**: per pocket, boundary inward. Spawned sub-pockets are processed last-in-first-out (2.2 step 3).
- The **final order and direction are the linker's** (section 4).

## 3. Steep / shallow, stock, tolerances

- Contact angles: section 1. They are not read in the constant-stepover code itself: no 0x1ac4 / 0x1ac5 in cs or off. On the 3D
  path they bound the Shallow Area Detection plan. Where the passes plan's angles take effect is NOT DECODED.
- Stock: ToolThicknessXY = max(XY, Z), ToolThicknessZ = Z (section 1). They act through the toolbox (tool positions).
- Tolerances:
  - StepMachTolerance = job tol;
  - MachTrimZTolerance (section 1);
  - FibreTol / Mid+ / Mid- / chord (section 1 defaults);
  - drive tessellation chord = record 0x12a0 (hst3d_port_plan.md 6.8).

## 4. Links (FUN_18007d028 mk l.92328, style 2 at l.92440-92449; parameters FUN_180075b48 mk l.89806-89880)

- Plan: "//Constant Surface Stepover Linking Plan" **0x1420** (CConstantStepoverLinkingPlan, vftable 1807979d8, McHmm
  1803b1290-1803b2c70). Its evaluation is NOT DECODED.
- Parameters:
  - **Zigzag** (TP+0x45d = 0x16bd) with TP+0x46f = 0: LinkDirectionStyle 1 and LinkReverseOrder = TP+0x474.
  - **Down / Up Mill** (TP+0x46f = 0x16d4 = 1 / 2): style 2 / 3, LinkDMExtendStart = TP+0x2f0, LinkDMShallowSlope =
    deg(TP+0x2f8). TP+0x2f0 / 0x2f8 = Overlap 0x1380 / Shallow angle 0x1388 [inferred: block shift 0x1090, the only page-1328
    doubles that the page enables for Down / Up Mill, FUN_180087e00 ms l.88735].
  - **else** LinkDirectionStyle 0:
    - with TP+0x474 set and Other Way (TP+0x470 = 0x16d5) off: LinkReverseDirection = true;
    - with TP+0x474 off and Other Way on: LinkReverseDirection = true;
    - with both on: no reverse;
    - then LinkReverseOrder = TP+0x474.
- **TP+0x474**: the record byte is NOT DETERMINED. The candidate is 0x16db ("Expand inside to out", shift 0x1267), but the page
  stores it **inverted**: record = (check == 0), FUN_1800a0740 ms l.105254; InitPage FUN_1800a03c0. The default 0x16db = 0 means
  the box is checked. How that maps to LinkReverseOrder cannot be settled without the linker.

## 5. Dialog page 1328 (HSTScallopParamPage, base HSTFinishPageBase) and 1342

Sources: DDX FUN_180087d50 ms l.88717 + FUN_1800a0300 ms l.105040; InitPage FUN_180088180 ms l.88852 + FUN_1800a03c0;
MapToOpEnt FUN_180088bd0 ms l.89312 + FUN_1800a0740; record = op - 0x2088.

| control | label (res) | record | notes |
|---|---|---|---|
| 1239 | Cut method (combo; strings 0xb8, 0xb9, 0xbb, 0xbc, 0xbe) | 0x16bd / 0x16d4 / 0x16d5 | index 0: 0x16bd = 0, 0x16d5 = 0; 1: 0x16d5 = 1; 2: 0x16bd = 1; 3: 0x16d4 = 1; 4: 0x16d4 = 2. Labels One Way / Other Way / Zigzag / Down Mill / Up Mill as s1330 in formats/opdlg_fields.tsv (same base). Default 0 |
| 692 | Tip compensation (combo) | NOT DECODED | not in these DDX functions |
| 1309 | Expand inside to out | 0x16db, inverted | enabled when cut method < 3 (FUN_1800a03c0 / FUN_1800a04b0); help: "start in the center of the part and cut towards the outside" |
| 1248 | Stepover | 0x1230 | |
| 1301 | Scallop height | not stored | 2.3 formula; disabled when r = 0 |
| 1285 / 1286 | Overlap / Shallow angle | 0x1380 / 0x1388 (1.0 mm, 5 deg) | enabled only for Down / Up Mill (FUN_180087e00) |
| 1287 | Use approximate start point | 0x16bf | |
| 1288 | Horizontal offset | 0x16ca | FUN_1800a03c0 calls Ordinal_13643(GetDlgItem(0x508), 0) on it |
| 1205-1208 | Keep tool down within: Distance / % of tool diameter | NOT DECODED | page members 0x2338 / 0x3130 are set to sqrt(1/tan(1 deg) + 1) x stepover and that / tool dia x 100 (FUN_1800a07b0, FUN_1800a0640); the control they belong to is NOT DETERMINED |
| 1506 | (static) | - | a UIPNGImage on the Spiral page (FUN_1800a0b30); on 1328 it is DDX'd as an int, the meaning is NOT DETERMINED |

Page **1342** (HSTScallopSteepShallowPage):
- base HST Finish Steep Shallow 1325: From 0x1248 / To 0x1250, Use Z depths 0x16c9, depth limits as Waterline;
- control **696 "Use boundaries as drive curves, then collapse" = 0x16e1** (FUN_1800a08a0 / 0920 / 09b0).

## 6. NOT DECODED (in order of need for a port)

1. CWeave node positions (FUN_180260490 / FUN_180222920): centre or tip, and the drop maths.
2. The contour extraction and loop grouping (FUN_180262430, FUN_1802666a0, FUN_1802669e0, FUN_180262c20, FUN_180265520), the
   role of Eva+0xcb8, and the region sign rules (bit 0x80000000).
3. CFindPockets grouping; the weave extents Eva+0x10a0 / 0x10b0.
4. The Shallow Area Detection plan 0x140a: structure, parameters and first offset DECODED (section 10); the weave cell classification
   (FUN_180263860 / FUN_18026c540) and MeasureCurveDiameter above 4 vertices (FUN_18004cb90) NOT DECODED.
5. The trimming to the constraint boundaries (FUN_1802616b0, FUN_18011b050) and the open / closed result.
6. The Constant Surface Stepover Linking Plan 0x1420; the TP+0x474 / 0x479 record bytes (0x479 = 0x16e1 [inferred]).
7. The use of ContactAngleLo / Hi on the passes plan; ShaftProfileEnable; MachBoundaryArcTolerance default; ToolShaftRadius;
   Tip compensation; the inverse scallop-height formula.

## 7. Oracle candidates (stored op 132 style 3 with NCI; [measured] with tools/mcam_ops.py)

| part | op | moves | D / Rc | stepover | angles | 0x16e1 (path) | Use Z depths 0x16c9 | stock xy / z |
|---|---|---|---|---|---|---|---|---|
| TUTORIAL 2 SWARF_METRIC (2017, X9) | 4 | 17837 | 10 / 5 | 1.0 | 0-90 | **0 (2D)** | 1 (0x1238 = 12) | 1 / 0 |
| TUTORIAL 2 SWARF (inch) | 5 | 14845 | 0.375 / 0.1875 | 0.0375 | 0-40 | 1 (3D) | 1 | 0.05 / 0 |
| MOLD BOTTLE (X9) | 6 | 16959 | 0.375 / 0.1875 | 0.0375 | 0-40 | 1 (3D) | 1 | 0 / 0 |
| MULTIAXIS ROUGHING FALSE FLOOR (2017) | 6 | 7317 | 0.25 / 0.125 | 0.01 | 0-90 | 1 (3D) | 0 | 0 / 0 |

Paths: `<mastercam-samples>/net/emastercam-training/...` (netscan/scan3d.tsv).

- The first three drive on Mastercam surfaces (read since 6.8). The last drives on 2 solids with SWEPT_SURF / B_SURFACE.
- The best first oracle is **TUTORIAL 2 SWARF_METRIC op 4**: the 2D path, a ball tool, metric, with containment, the same part as
  the Waterline oracle. Run it with Use Z depths off, as for Waterline.
- The default op itself (D 20 flat, stepover 1, 0x16e1 = 1) has no NCI.

## 8. Port outline (in terms of existing code; every item marked ours where it fills a gap)

1. `gen_scallop` in viewer/cam3d_finish.c, on `hst_grid_open` (mesh, height field H, region, stock, refusals).
   - Add refusals:
     - 0x16e1 = 1 (3D path: Shallow Area Detection NOT DECODED), unless the user accepts the 2D path as ours (superseded 2026-11-12:
       the 3D path is ported, section 10);
     - Down / Up Mill;
     - angles other than 0..90;
     - Use Z depths.
2. Drive boundary = the containment (`g->region`), else the drive box widened by |sxy| + D/2 + 2 tol (2.1).
3. Weave = a grid of spacing res = 0.7 x MaxHorizontalStepChange (1). Node positions = the tool tip from `sr_hf_tip` on `g->H`
   (ours until 6.1).
4. Pass 0 = the boundary on the weave. Pass k+1 = the 0-contour of d3(node, pass k) - H.
   - d3 = 3D point-segment distance with Z scaled by H/V, cut off at 2H (2.2 step 3).
   - The contouring is ours: marching squares on the weave (area_mill.c's grid is 2D-only; reuse its loop tracing `am_chain`
     style).
   - Stop when no contour is left. Split into pockets when the contour has more than one component (LIFO order).
5. Output the loops inside the containment as closed passes. Order and direction ours (SAFE links, as Waterline / Raster) until
   0x1420 is decoded.
6. Oracle script `tools/tests/scallop_oracle.sh`, modelled on waterline_oracle.sh: TUTORIAL 2 SWARF_METRIC op 4, ref -> ours
   p50 / p95 per pass.

## 9. Port (2026-10-26, viewer/cam3d_finish.c gen_scallop)
- The 2D boundaries plan as 8: boundary = the containment, else the drive box offset by |XY stock| + D/2 + 2 tol; pass 0 = the boundary, pass k + 1
  = the 0-contour of d3(node, pass k) - H (3D point-segment distance, searched to 2H) over the height-field tool tips; a pocket ends when no node
  is left. Refused: 0x16e1 = 1 (the 3D plan; **the default op**: switch it off on the Steep / Shallow page), Use Z depths, Down / Up Mill,
  angles other than 0..90, the approximate start point.
- Ours: the weave spacing (the height-field cell), the node = the tool tip, marching squares, the done region (nodes within H of an earlier
  pass), the wall rule (a node next to the done region with nothing within 2H joins the front: the tip field jumps across a wall, where
  Mastercam starts a new pocket), every loop kept, the SAFE links, the loop direction. Order: boundary inward, reversed with Expand inside to
  out (0x16db = 0) - the stored TUTORIAL 2 SWARF_METRIC op 4 (which has it) runs from the centre out [measured].
- Pages: 1328 (Cut Parameters) and 1342 (shown as "Steep / Shallow" in the PG_ROUGH slot, ours), formats/opdlg_fields.tsv s1328 / s1342.
- **Oracle** (tools/tests/scallop_oracle.sh, TUTORIAL 2 SWARF_METRIC op 4 with Use Z depths off, our samples kept at z <= 12 = the op's Z
  limit): ref->ours p50 0.265 / p95 0.539 mm, ours->ref p50 0.258 / p95 0.490 mm (stepover 1.0 mm), cut length ref 7593 / ours 6344.
- Scene gen_scallop (BRACKET4, collapse off): 62294 points.

## 10. The 3D boundaries path: "Use boundaries as drive curves, then collapse" (0x16e1 = 1; decode + port 2026-11-12)

Sources: `decomp/scallop3d/` (local, gitignored): `ad.c` = DumpRange McHmm 180310000-180320000 (81 functions: the area-detect plans);
`vt.txt` = VtableDump of the AreaDetect / DetectShallow / DetectContact / ConstantStepover classes; `d_fc80.c` = FUN_18030fc80;
`d_units.c` = FUN_1802e5a70, FUN_18004cf60, FUN_1802f3a80; `dis_317200.txt` = the disassembly of FUN_180317200; `fi_*.txt` = FindImm
of the parameter ids 0x1b40 / 0x1b46 / 0x1baf / 0x1bb1 / 0x1c90.

### 10.1 The plan (mckernel)
- FUN_18007d7c8 mk l.93155-93240: style 2 with TP+0x479 (record 0x16e1) = 1 -> `ekDriveBdy = CreatePlan(0x140a)` "// Shallow Area Detection
  Plan", inputs "ConstraintBoundaries", "Surfaces" (ekPlan3 / ekPlanTcb3 = the containment or the Bounding Box Plan, + the surfaces); then the
  passes plan **0x1445** with "DriveBoundaries", "ConstraintBoundaries", "Surfaces" and contact angle mode 2 = 0 / 90 deg.
- Parameters of 0x140a, FUN_180079814 mk l.90448-90592: the tool (FUN_180074e18), ToolThicknessXY / Z (the XY / Z stock split of l.90470-90495),
  PosZLo (job) or PosZHi / PosZLo (Use Z depths 0x16c9, with job +0x28), **ContactAngleLo / Hi = degrees of min / max(TP+0x1d0, TP+0x1d8)
  (record 0x1248 / 0x1250) clamped to 0..pi/2** (l.90560-90592). Every length goes through FUN_1800edd8c, which writes value x job +0x80
  with "%19.9f": **McHmm works in mm** (job +0x80 = 25.4 on an inch part, FUN_18007bb48 mk l.91625-91633), so the plan defaults below are mm.

### 10.2 CDetectShallowAreasPlan 0x140a (McHmm)
- Constructor FUN_18031dcf0 (ad.c): CreatePlan type 0x140a, inputs 0x1b41 ConstraintBoundaries / 0x1aaf Surfaces, FUN_1802f5dc0 (the shared
  defaults; its area-detect branch FUN_18030fc80).
- **Defaults** FUN_18030fc80 (d_fc80.c), for types other than 0x1410 / 0x1432: MachConstStepover 0x1b40 = 0, SmoothingDivergence 0x1b46 =
  0.14 x 0x1b40 = 0, MachConstStepoverHorizontal / Vertical = 0x1b40, **MachConstNumberOfOffsets 0x1baf = 1**, MachFibreEndpointTolerance
  0x1b3d = max(0.005, min(0x1b46, 0.3 x MachWeaveResolution)) = 0.005, **AreaDetectContactNormalChange 0x1c90 = 0.3**, **MinDiameter
  0x1b49 = 1.0**. (0x1410 / 0x1432: 0x1a55 = 0.03 x 0x1c92, MachConstStepover = min(0.5, 1.6 x 0x1c93), 0x1b46 = 0.4 x 0x1b40.)
- The default values are typed strings (FUN_1802e5a70 -> FUN_1802e5950 formats the double with 15 digits; no unit conversion).
- Evaluator EvaAreaDetectShallowPlan (vftable 180787b68): [1] = FUN_18031dd90:
  1. FUN_180317080: the toolbox (FUN_1802f5b70 / FUN_180221480) with margin 2 x MachWeaveResolution + MachFibreEndpointTolerance + 2 x Eva +0x208.
  2. FUN_180316750: the offset block FUN_18026a190 (as 2.2) at Eva +0xd50, a copy at +0xcd8 with +0x18 = -1 (step -1: the "interior"
     mode), +0x08 = MachFibreEndpointTolerance, +0x10 = AreaDetectContactNormalChange^2; the CWeave FUN_18025d440 over the constraint area
     (margin = MachFibreEndpointTolerance, + Eva +0x138 when BoundaryOffsetContactEnable 0x1c79), filled by FUN_180263860 (off.c l.5456).
  3. FUN_180317200: FUN_180262430 (the weave cells, FUN_18026c540 per cell: NOT DECODED), loops FUN_1802666a0, groups FUN_1802669e0; per
     group FUN_1802616b0 returns MeasureCurveDiameter (FUN_18004cf60: below 5 vertices the largest vertex distance; else FUN_18004cb90, NOT
     DECODED; -1 / -0.5 on failure) and **groups below MinDiameter 0x1b49 are deleted** (disassembly 1803178a3-1803178f2: COMISD against
     0x1b49; the log " Deleting N small profiles before offset"). FUN_1802616b0 also flags the vertices whose contact test FUN_1802f3a80
     fails (bit 0x80000000; the test: the cosine window Eva +0x678..+0x680 of EvaMachLocBasePlan's ctor FUN_1802f2340 from ContactAngleLo /
     Hi / Tol, and the Z window +0x6c0). Then MachConstNumberOfOffsets (1) rounds: with SmoothingDivergence 0 no smoothing, with
     MachConstStepover 0 no offset round. Output: each loop's vertices with z + Eva +0x140 (ToolThicknessZ) as boundary entities
     (FUN_180118ee0 / FUN_1801194d0 / FUN_180121900 / FUN_18011b050).
- So the shallow areas are the regions of the weave whose tool contact lies in the angle window (and in the Z window), as closed 3D
  loops. With 0..90 every contact passes the window (+0x678 = -0.1, +0x680 = 1.1): the area is where the tool touches the drive.
- **The contact angle filter vs Waterline (agent W)**: the window is the same Eva +0x678..+0x690 set by FUN_1802f2340 (hst3d_port_plan.md 7),
  but Scallop applies it here, in the area detection (cell classification + FUN_1802f3a80 on the boundary vertices), not through Waterline's
  per-level vertex filter FUN_18040b6f0. The passes plan 0x1445 itself always gets 0..90.

### 10.3 Constant Stepover Passes from 3D Boundaries 0x1445
- Mach3DBoundaryTolerance 0x1bb1 = 0.1 mm (FUN_18033a950 cs l.4649-4655). FUN_18033aaf0 (cs l.4660-4943): the main block, then 4 first-offset
  blocks at r = 0.1, 0.15, 0.225, 0.3375 (Mid+/- scaled by r / H, parallelness -1, flags +0x74 / +0x75 = 1).
- Per pocket (FUN_18033b900 cs l.5129-5654): the CWeave with margin 2 x res + the block's step, the 3D boundary as its first front (z - Eva
  +0x140), FUN_1803343e0 with the first-offset block; on failure the next r ("First offset failed. Retry with rad = "), after 4: "All first
  offset attempts failed on this profile." So the first pass is the boundary offset by r in 3D, then the stepover H as 2.2.
- [measured] MULTIAXIS ROUGHING FALSE FLOOR op 6 (inch): the stored outermost loop lies 0.0002-0.0027 in inside our contact boundary - the
  0.1 mm (0.0039 in) offset, not 0.1 in; this confirms the mm reading.

### 10.4 Port status (2026-11-13: refused again, no stand-in)
- First port (226bd2a) classified our height-field nodes with stand-ins (contact / Z window / a contact normal from the tip-field slope,
  marching squares, a sub-cell first offset). User rule (2026-11-13, via the coordinator, as for Waterline): no stand-ins - decode first.
- Now: gen_scallop reads 0x16e1 first and calls hst_grid_open2 with rules = 2 (the op's angle limits go to the detection, not refused there);
  the collapse branch computes the decoded parts (the angle window of FUN_1802f2340 with the %19.9f degree rounding of the macro, MinDiameter
  1.0 mm, Mach3DBoundaryTolerance 0.1 mm; the passes loop keeps the first offset at r, its x 1.5 retries and the boundary not being a pass)
  and then refuses: "Scallop: boundaries as drive curves (0x16e1): Shallow Area Detection needs the CWeave cell test (FUN_18026c540), NOT
  PORTED - switch it off". The 2D path is unchanged (byte-identical). Scene gen_scallop3d (BRACKET4, the default op) = the refusal.
- Missing for the port: FUN_18026c540 (the weave cell classification, called per cell by FUN_180262430) and FUN_18004cb90
  (MeasureCurveDiameter above 4 vertices). Section 10.6 records the decode of these.
- Testing only: MCAM_SC_PLANE=m0..m8,ox,oy,oz (drive into a stored op's tool plane) and MCAM_SC_CONTAIN=idn (a closed spline containment).

### 10.5 Measured with the first port's stand-ins (226bd2a, 2026-11-12; withdrawn 2026-11-13 - kept as the target to reach)
| op | stepover | before | after: ref->ours p50 / p95 | ours->ref p50 / p95 (cut) | cut length ref / ours |
|---|---|---|---|---|---|
| TUTORIAL 2 SWARF (2017) op 5, 0..40 deg, Use Z depths | 0.0375 | refused (0x16e1; angles) | 0.011 / 0.014 | 0.013 / 0.019 | 320 / 313 |
| MOLD BOTTLE (X9) op 6, 0..40 deg, Use Z depths | 0.0375 | refused | 0.011 / 0.014 | 0.013 / 0.018 | 343 / 336 |
| MULTIAXIS ROUGHING FALSE FLOOR (2017) op 6, tool plane 52 | 0.01 | refused (0x16e1; tool plane) | 0.003 / 0.006 | 0.006 / 0.035 | 133 / 170 |

- FALSE FLOOR op 6 runs only through the testing harness (MCAM_SC_PLANE from the record's tool plane at 0x5d8, MCAM_SC_CONTAIN=1094, its
  containment spline; Outside, offset 0x12b0 0.125 + D/2): its tool plane (id 52, tilted 32.7 deg) is outside the HST port (needs Top),
  so it stays refused in the app. Its extra cut length is the jagged floor passes (stepover 0.01 on a 0.0078 weave).
- The 2D path is unchanged: tools/tests/scallop_oracle.sh moves byte-identical (p50 0.254 / p95 0.497, ours->ref 0.257 / 0.492).
- Corpus: these three (and the X9 copy of TUTORIAL 2 SWARF op 5) are all the stored Scallop ops with 0x16e1 = 1 and NCI
  (~/mastercam-re/work/hst3d/netscan/scan3d.tsv); the 2018 copies have no NCI.

### 10.6 FUN_18026c540 (weave cell) and FUN_18004cb90 (curve diameter) - decode 2026-11-13
Sources: `decomp/scallop3d/d_c540.c` (FUN_18026c540, FUN_18004cb90, FUN_18004a8f0, FUN_180046210), `d_c540b.c` (FUN_18026a990, FUN_18026b660,
FUN_18026bb10, FUN_18026c250, FUN_18004aca0, FUN_18004b170, FUN_180049be0, FUN_18004a420).

**FUN_18004cb90 (MeasureCurveDiameter above 4 vertices) - DECODED.** FUN_18004cf60 first builds a curve object (FUN_180046210: the loop's
points, consecutive duplicates dropped by FUN_1800082c0, closed / open flag; tolerances 1e-7 and 4e-4 at +0x30 / +0x38; with more than 4
elements FUN_180048220 then FUN_180048f10 until it changes nothing - not decompiled, may merge runs into arcs), then FUN_18004a8f0 lists the
convex-hull features with their normal-direction intervals on a pseudo-angle scale [0, 4) (FUN_180049be0 / FUN_18004a420), and
FUN_18004cb90 takes the maximum over the antipodal pairs (a pair whose direction intervals overlap within 1e-4, mod 4) of: vertex-vertex
squared XY distance, or vertex-arc (FUN_18004aca0: the arc point farthest from the vertex, the arc's sense from flag 0x4000000 / 0x5000000),
or arc-arc (FUN_18004b170); the result is the square root, -1 when empty. These are rotating calipers: **for a loop of line segments the
result is the XY diameter of its convex hull = the largest XY distance between two of its vertices** (the same rule as the < 5 vertex
branch). Only arcs made by FUN_180048220 / FUN_180048f10 (tolerance 4e-4) could move it, by at most that tolerance [inferred: those two
are not decompiled].

**FUN_18026c540 (one weave cell of FUN_180262430) - structure DECODED, port blocked.** Per cell of the CWeave (a cell = the rectangle
between two x-fibres and two y-fibres, its fibre crossings at Eva-weave +0x90, 16-byte entries {crossing, side flag}):
1. FUN_18026a990: pairs the cell's crossings into contour segments. While more than 3 crossings remain it chooses the pairing:
   with the block step -1 (the area-detect / interior mode, block +0x18) by FUN_180289d90 on the pair's midpoints (a single [0, 1]
   interval = keep); otherwise by FUN_180288160 (the distance field) at the two candidate pairings and the smaller sine^2 of the
   segment-to-field angle; or (param 2 set) by FUN_18025f670, a refinement drop at the midpoint. Block +0x68 (parallelness) bounds it.
2. FUN_18026b660 (when the cell has a toolbox, param 6): for each crossing pair the two crossing points (FUN_180254580) get the contact
   test **FUN_1802f3a80** (the Eva contact window +0x678..+0x680 = ContactAngleLo / Hi, and the Z window) - in = >= 0; with block +0x74
   (first-offset flag) also MeasureInterior2D FUN_1802887a0 against the boundary toolbox. Then the cell is **split** (both flags 1) when the
   pair is longer than block +0x18 (param_6[3]) or the **contact normal change** FUN_180252a70 exceeds block +0x10 = AreaDetectContactNormalChange^2
   (param_6[2]), and kept whole when the Z step exceeds param_6[5] or the contact-point distance (FUN_1802538d0 on both, block +0x38)
   exceeds param_6[4]; a pair whose two ends differ in the contact test is a boundary segment (FUN_18026b440 emits it, with the
   "on boundary" flag); pairs shorter than block +0 are ignored.
3. With no crossing result: FUN_18026bb10 (block +0x50 / +0x58 = H + Mid+ / H - Mid- set: MeasureRadSegs2D / 3D finds a closed contour
   inside the cell without crossings) or FUN_18026c250 (two crossings at the same point: FUN_18000b940) emit / split.
4. The cell's result {found, split} drives FUN_180262430's subdivision (up to MachCellSubdivisionLimit 0x1aba, x5 when split;
   "Exceeded cell subdivision between x ... and y ... with strength").

So the shallow area's boundary is where **FUN_1802f3a80 changes sign between two fibre crossings of a cell** - the crossings being the
tool positions the CWeave fill (FUN_180263860 -> FUN_180260490, the toolbox drop FUN_180222920) placed on its fibres, each with its contact
(the tool-profile layer and parameter that FUN_1802538d0 / FUN_180252a70 read). Porting it from Mastercam's code means porting the CWeave
itself: the fibres and their crossings (FUN_18025d440, FUN_180260490, FUN_180263860), the toolbox drop with its contact record
(FUN_180222920; hst3d_weave_drop.c has the drop, not the contact record), the contact test FUN_1802f3a80 (decoded for Project Points,
hst_project_hybrid.md), FUN_180252a70 / FUN_1802538d0, the cell walk FUN_180262430 with FUN_18026a990 / FUN_18026b660 / FUN_18026bb10 /
FUN_18026c250 / FUN_18026b440, and the loop assembly FUN_1802666a0 / FUN_1802669e0. The 2D path (section 9) stands in for this same
engine with a height-field grid, so neither path has it today. Not ported: **0x16e1 = 1 stays refused** (10.4). Estimated size: the weave
range off.c (134 functions, 10.8k decompiled lines) plus the drop's contact record; a separate multi-session item (TODO).
