# HST general linker (McHmm EvaGeneralLinkerPlan) - decode status (2026-11-13, M1)

The linker that joins the passes of every HST 3D op (Waterline, Raster, Spiral, Radial, Scallop, Project, Pencil, ...) and of
2D Area Mill. Every C*LinkingPlan (Waterline 0x141c, Raster 0x142b, Spiral 0x142c, Radial 0x142d, Area Clearance 0x141d, ...)
registers parameters only; the moves are made by the shared EvaGeneralLinkerPlan (vftable 180795ba8, Evaluate FUN_1803a0c50).
Our ops still join their passes with the SAFE rule (ours, labelled in viewer/cam3d_*.c); this document says what is decoded and
what is missing before Mastercam's linker can replace it. Ported so far: the Raster stay-down link (12) and the Raster retract link / first approach / final exit with their vertical leads (14); the Waterline order (21) and every Waterline surface ramp (25, 26); everything else keeps SAFE (see 7, 9.5, 10.6, 11.3, 12, 13.6, 14).

Citation keys: mk = decomp/mckernel_all.c, hl = decomp/hmm_link.c, hk = decomp/hmm_linker.c, hc = decomp/hmm_linkconnect.c,
d4 = decomp/proj/d4.c, x = decomp/hmm_linker_x1.c, a Ghidra DecompAt dump of mchmm.dll made for this decode (19 functions: FUN_1801d65f0, 1801d69f0,
1801d6de0, 1801d7210, 1801d62e0, 1801dcc30, 1801d0340, 1801d1e70, 1801d2bd0, 18027a590, 180170520, 18018ad90, 180177a90,
1801a54f0, 1801c9500, 1801c9390, 1801a8fe0, 180281580, 1801c3dd0 - regenerate with tools/ghidra_scripts/DecompAt.java on
~/mastercam-re/ghidra_hmm; decomp/ is gitignored). K+x = the kernel's TPHSTToolPath offset (K = TP m - 0x60, record = op + 0x68,
the copy FUN_180923f60 d4 l.60-255).

## 1. mckernel -> McHmm: the link parameters of an HST op (FUN_180075b48, mk l.88917-90260)

| McHmm parameter | value | record | mk |
|---|---|---|---|
| LinkSafeZ = LinkZClearance | clearance K+0x10 + job +0x28 (plane origin Z), minus the corner radius when Tip comp K+0x48 = 0; written only when clearance is on (K+0x19), absolute (K+0x18 = 0) and (K+0x469 = 0 or LinkRetractStyle = 2) | 0x170 / 0x178 / 0x179 | 89177-89205 |
| LinkClearSurfaceBy | the retract K+0x20 when retract on (K+0x29) and incremental (K+0x28), else 2.5 mm / 0.1 in; at least the stepdown for style 0 / 9 with K+0x463; written for LinkRetractStyle 0 / 1 only | 0x180 / 0x188 / 0x189 | 89150-89176, 89295-89304 |
| LinkMaxStayOnSurface | K+0x468 = 1: tool diameter x K+0x288 / 100; = 0: K+0x280; else 2.5 mm / 0.15 in. Style 9 (Scallop?): 0.5 D, or 0 with K+0x482 | 0x16cd, 0x1308, 0x1310 | 89205-89235 |
| LinkTrimmingStyle | 0 when min(lead in, lead out) <= tol, else K+0x46c | 0x16d1 | 89237-89260 |
| LinkVerticalLeadIn / OutRadius | K+0x2c0 / K+0x2c8 | 0x1348 / 0x1350 | 89259-89270 |
| LinkMaxAllowedTrimming | K+0x308 | 0x13a0 | 89269-89272 |
| LinkRetractStyle | K+0x462; style 0 also LinkCurlOverradius K+0x200, LinkCurlDownRadius K+0x208 | 0x16c4, 0x1280, 0x1288 | 89274-89300 |
| Waterline (style 1): LinkByZLevel | K+0x45f == 0 | 0x16c0 | 90013 |
| Waterline: LinkWaterlineDirectionStyle | Zigzag K+0x45d -> 2; else K+0x45e set -> 0; else 1 | 0x16bd / 0x16be | 90015-90037 |
| Waterline: LinkHeadNorthwards | false when K+0x49a (Bottom to top) | 0x16c1 | 90038 |
| Transitions K+0x46d (page 1341 radio 0x16d2) | 0 Tangential: SurfaceRampStyle 1 + LinkSurfaceRampSmooth true; 1 Ramp: (name dropped by Ghidra, value 1 [inferred SurfaceRampStyle]) + Smooth false; 2 Straight: (name dropped) 0 [inferred SurfaceRampStyle]; SurfaceRampAngle = max(0.01, deg(K+0x2d0)) | 0x16d2, 0x1358 | 90090-90128 |
| MaxRampAngle | deg(K+0x2e8) when > tol, else 3 | 0x1378 | 90129 |
| HorizontalLeadIn / OutRadius | K+0x2d8 (when >= 0) / K+0x2e0 | 0x1368 / 0x1370 | |
| RampHeightOffset / LinkRampHeightOffset | K+0x368 / feed plane K+0x30 | 0x1420 / 0x190 | 90160-90168 |
| MinProfileDiameter | K+0x330 ("Skip pockets smaller than") | 0x13e8 | |
| LinkStartX / Y | the job's +0x10 / +0x18 | | 90240 |
| EnableMaxCuttingDist / MaxCuttingDist | K+0x469 = 1: K+0x290; = 2: tool dia x K+0x298 | 0x16ce, 0x1318, 0x1320 | 90195-90215 |

Values of the oracle op (TUTORIAL 2 SWARF_METRIC op 3, Waterline, D12 r3): keep down mode 1 at 20 % -> LinkMaxStayOnSurface 2.4;
LinkRetractStyle 1; vertical lead in / out 3; curl 4.4 / 4.4; Transitions 0 (Tangential) at 30 deg; horizontal lead in / out 2.85 / 2;
MaxRampAngle 10 deg; RampHeightOffset 3; clearance 100 absolute, retract 25 incremental (-> LinkClearSurfaceBy 25), feed plane 5
(-> LinkRampHeightOffset 5). PARALLELSTEEP.mcam op 5 (Raster, inch): keep down 16 % of D 0.25 -> 0.04; LinkRetractStyle 2; leads 0.075.

**Output path**: mckernel never writes NCI for these ops. FUN_18006f4d0 (mk l.85676) extracts McHmm's result through FUN_1800f0e10 /
CMCHstExtractTag (event handler FUN_1800654c4 mk l.79293-79558, 0x88-byte records: code 400 line, 500 arc, markers 600-2400) into
HstToolpathKO.tmp; mccore.dll's MCHSTKOTranslator (0x180918220 / 0x180919340, not decompiled) makes the NCI. **How the clearance /
retract / feed plane become rapids there is NOT DECODED** (needs a ghidra_core decompile of mccore 0x180919340 and callees).

## 2. The Waterline Linking Plan 0x141c defaults (ctor FUN_1803bc3b0 hl l.18584-18601)

Registration chain: FUN_1803a7ed0 (base, type 0x141c; calls FUN_1803aa170) -> FUN_1803a9720 -> FUN_1803bc460 -> FUN_1803bc690 ->
FUN_1803bdae0 -> FUN_1803ab050; the virtual slots +0x68..+0x90 are [inferred] FUN_1803a9990, 1803a9bc0, 1803be1c0 (trim / leads),
1803bd270 (surface ramp), 1803a8b40 (retract), 1803a9490 (the CWaterlineLinkingPlan vtable is not in the vt dumps).
Defaults that mckernel does not override for Waterline (the others are set from the record, section 1):
- LinkWaterlineDirectionStyle 0; LinkDirectionStyle = (0x1cbc == 2 ? 1 bidirectional : 0 one way); LinkReverseDirection = (0x1cbc == 1);
  LinkReverseOrder false (read nowhere but default expressions); LinkByZLevel false (hl l.18604-18655).
- LinkHeadNorthwards true; LinkSimpleOrder false; LinkOrderingType 1 (enabled when !LinkSimpleOrder); LinkORWholeLevelBeforeNext =
  DisableIntelligentOrder (false); LinkORAdjacentLevelOverlap 2.2 x ToolShaftRadius; LinkORClusterEnablePreOrder false (hl l.18656-18862).
- LinkMaxStayOnSurface = Stepdown x 2; SurfaceRampStyle 1; SurfaceRampAngle 5; LinkSurfaceRampSmooth true; SurfaceRampSteps 10;
  SurfaceRampAngleDiff 10; SurfaceRampMaxDeviation 0.0005; SurfaceRampTangentFactor = Stepdown; SurfaceRampMaxVerticalChange 1e6;
  SurfaceRampMax / MinPassExtension = 0.5 D / 0.1 x max; MaxRampAngle 10; RampHeightOffset 3; HelixDiameter = ball ? 2 Rc : 1.9 x flat
  radius; ProfileRampStyle 1 (hl l.18880-19150, FUN_1803bd270).
- LinkTrimmingStyle 1; LinkMaxAllowedTrimming = 1.1 x max(lead in, lead out); LinkTrimMinOverlapDiff = that / 100; LinkVerticalLeadInRadius
  2.0 (base 1.0, hl l.12043); lead out = lead in; HorizontalLeadIn = HelixDiameter / 2; HorizontalLeadOut = lead in; LiftAngle 0
  (hl l.19195-19260, FUN_1803be1c0).
- Base (FUN_1803a8b40 / FUN_1803aa170, hl l.12127-12879): LinkRetractStyle 1; LinkCurlOverradius = LinkClearSurfaceBy x 1.1; curl down =
  curl over; LinkSmoothingRadius = min(lead in, lead out); LinkMinSmoothingAngle 175; LinkClearSurfaceWithin 2.0; LinkClearSurfaceBy 2.0;
  LinkSafeZ = RoundUp(top Z of the surfaces' extents + LinkClearSurfaceBy + max(curl over, curl down)) (enabled when
  !LnkUseClearancePlaneOnly); LinkZClearance the same with the tool rotations; LinkStartX / Y = RoundUp of the extents' centre;
  LinkRFMinPassLength 0.02 (LinkRFDiscardShortPass = > 0) for 0x141c; LinkRampHeightOffset = InputPassesAxiallyOffset ? 0x1ada : 0.
  (ProfileLinkRadius 0x1a7c is not registered by this plan.)

## 3. The linker's state (FUN_18039bcd0 hl l.4632-4975, FUN_18039d590 hl l.4982-5068)

+0x1058 = the source plan's kind (FUN_1803ab250 hl l.12905: Waterline Passes 0x1436 -> kind 0); +0x1060 = (LinkDirectionStyle == 1),
+0x1061 = LinkReverseDirection, +0x1078 = (style 2 / 3, down / up milling); +0x10e0 = LinkOrderingType (0 when LinkSimpleOrder);
+0x10d0 = WholeLevelBeforeNext; +0x10d1 = HeadNorthwards; +0x1108 = (LinkRetractStyle != 0), +0x1109 = (== 2); +0x1110 =
LinkMaxAllowedTrimming; +0x1118 = LinkMaxStayOnSurface; +0x1128 / +0x1130 = vertical lead in / out radius; +0x1138 / +0x1140 = curl over /
down; +0x1148 = smoothing radius (0 unless retract style 0); +0x1158 / +0x1160 = ClearSurfaceWithin / By; +0x1198 = DAT_1809bf1a0 x 0.5
(the clamp of the lead-in angle; DAT_1809bf1a0 is set at run time, presumably pi / 2: NOT DECODED); +0x11a0 = LinkByZLevel (kind 0,
FUN_18039d850 hl l.5081); +0x11e0 = MaxRampAngle (radians), +0x11e8 = LiftAngle, +0x11f0 / +0x11f8 = horizontal lead in / out
(hl l.4904-4911); +0x1200 = (SurfaceRampStyle == 1), +0x1210 = cos(SurfaceRampAngle), +0x1218 = tan(angle x (1 - AngleDiff / 100)),
+0x1120 = Smooth; +0x124a = EnableZProfileRamp.

## 4. Order and direction (Waterline, kind 0)

1. Evaluate FUN_1803a0c50 -> FUN_18039dc40 (hl l.7299) builds the pass adjacency graph EvaFragAdjacency (+0x1020, hl l.5509-5547; kind 0
   bidirectional also FUN_180183a50). The pass filter FUN_18039e420 -> FUN_18018ad90 (x l.1438): spikes only with 0x1b0a, extra passes
   only with +0x1078.
2. FUN_18039ed00 (hl l.5913): clusters (FUN_18039f1a0; one cluster of all passes with ClusterEnablePreOrder off). The current point starts
   at (LinkStartX, LinkStartY) (hl l.5963-5975); each round takes the unvisited cluster nearest in XY (FUN_18039e5d0) and orders it with
   FragOrderer (ctor FUN_1801b78f0 hk l.26804, run FUN_1801b8fc0); the cluster's last pass end becomes the current point (FUN_18039e820).
3. Direction (FUN_1803a1ec0 hl l.7930-8300): one way - the collection's order, with LinkReverseDirection every pass reversed and the list
   reversed; bidirectional - the adjacency graph walked one generation at a time, the direction flipping each generation (hl l.8247),
   starting at LinkReverseDirection ([inferred] a generation = a Z level).
4. FragOrderer (hk l.26995-27030): +0xf40 = HeadNorthwards; LinkByZLevel with kind in {0, 2, 3, 4, 7} calls FUN_180170520 (x l.1378),
   which re-inserts the nodes into a container (FUN_18016f750) - **the Z key is NOT DECODED**.
5. Next pass (FUN_1801b90d0 hk l.27841): OrderingType 0 -> FUN_1801c3bb0 (HeadNorthwards: the first unvisited in list order, else the
   last); otherwise the graph neighbours of the previous pass (FUN_1801c2d70), then FUN_1801bfbd0 / FUN_1801c0320, then all remaining, each
   through FUN_1801b9820 (prerequisites FUN_1801b9490 / FUN_1801bf440).
   First pass (FUN_1801b9820 hk l.28196-28300): candidates sorted by FUN_18027a4f0(pass, point) with the point (StartX, StartY, LinkSafeZ)
   rotated by the tool rotations (FUN_1803a3ae0 hl l.9281) - [inferred] a distance; then the list is walked while the XY gap from the
   first pass's end to the candidate's start is <= +0x1118 and (kind 0) the candidate is a graph neighbour, the last qualifying one taken.
   Later passes: FUN_1801b9cc0 from the previous end (hk l.28371), its comparison FUN_1801b9d70 NOT DECODED.
6. Where a closed pass is entered (FUN_1801c05c0 hk l.33400-33436): FUN_18027a590 finds the point of the loop for the previous end (or the
   start point) and FUN_180281580 re-splits the loop there (x l.1348, 1808) - [inferred] the nearest point. (ProfileLinkAdvanceRound
   +0x11d8 applies to kinds 2-4 and 7 only.)

## 5. The link between two passes (FUN_1801c05c0 hk l.33126-34190)

- Link kind: 2 (retract), or 5 with EnableZProfileRamp (hk l.33439-33443). It becomes **1 (stay down)** when the **XY distance** from the
  previous pass's end to the next pass's start is < LinkMaxStayOnSurface (hk l.33551) and, for kind 0, the next pass is in the previous
  one's adjacency list (+0x58 or +0x28 by HeadNorthwards, hk l.33560-33608). No model test in this decision.
- Kind 1 -> **3 (surface ramp)** in FUN_1801c3dd0 (hk l.35650-36040, x l.1914): needs SurfaceRampStyle 1 (+0x1200) and not (same set and
  SurfaceRampExludeSameLevel); for kind 0 the length d = |dZ| / tan(SurfaceRampAngle) (tan from +0x1210, hk l.35788-35795); the previous
  pass is extended by d (FUN_180279f90, not dumped) and the next one trimmed by d at its start and split (FUN_1802829c0). Measured: op 3's
  level-to-level ramps run 2.08 along the loop for the 1.2 step = 1.2 / tan 30 deg. The smoothing of the ramp's ends (+0x1120, the five
  short moves of the NCI) is NOT DECODED (it is passed to FUN_180194140 / 180197b80 / 180199220).
- Emission (FUN_180198f00 hk l.5808): kind 2 -> FUN_1801a1d70; kinds 1, 3 -> FUN_180199220 (hk l.5922: a CLinkMovementConnect path end ->
  start, FUN_1801d2bd0 x l.937; when its +0x38 exceeds SurfaceRampMaxVerticalChange the link is redone as a retract, hk l.6239 / 6548-6550;
  else the path is emitted, l.6602); kinds 4 / 5 -> FUN_1801a9e50 (not dumped). First pass: FUN_180194140 (or FUN_1801a9740 for 4 / 5); the
  end FUN_180197b80.
- **Retract height** (FUN_1801d7210 -> FUN_1801990c0, hk l.5865-5920, x l.375-464): h = the connect path's +0x30 along the straight link
  ([inferred] the highest Z of the link); LinkRetractStyle 2: h = LinkZClearance, or h + 1.0 when the path value is higher (hk l.5904);
  style 1: h = the path value; then h = max(h, both ends' Z), and the vertical points (x_out, y_out, h) / (x_in, y_in, h) are added where h
  is above that end. Measured on op 3 (style 1, ClearSurfaceBy 25): the rapid tops are 25 above a per-link height between 42.6 and 49.6
  (73.119, 73.329, 72.763, ...). **Where LinkClearSurfaceBy enters the path height is NOT DECODED** (it is read only into plan sums
  +0x1258 / +0x1268 / +0x1278, hl l.4966-4975, and the evaluate's box padding, hl l.7454).
- Retract style 0 (FUN_1801d65f0 -> FUN_1801d6de0, x l.1-375): stay on the surface (FUN_1801d2bd0) when the lead points' XY distance <=
  min(curl over, curl down) x the connect's float +0x1a8; else the curl-over / curl-down profile FUN_180170b10 (NOT DECODED).
- Output tags (FUN_1801a54f0, x l.1574): 0x13d4 the stay-down connect path (hk l.6602); 0x13d5 / 0x13d6 / 0x13d0 the retract-side /
  approach-side moves of FUN_1801a1d70 - which is lead, rapid or ramp NOT DECODED.

## 6. Vertical leads (FUN_18019e010 hk l.8975-10556; callers hk l.3032 (approach), 5399 (exit), 12361, 22637, 23547)

- Lead-in (radius R = +0x1128, angle a = param_15): h = R sin a; the arc starts at S - dir (R - h) at Z z + sqrt(R^2 - h^2) and ends at
  the pass start S tangent to a descent of angle a (centre S + dir h at Z z + sqrt(R^2 - h^2)) (hk l.10172-10213). dir = the unit XY tangent
  at the pass start (hk l.10123-10160). In the first approach a = MaxRampAngle +0x11e0 when it is not 0; with 0 (and the pass not flagged
  +0xb9) a = the pass's start slope clamped to +-0x1198 (hk l.2984-3013).
- Lead-out (R = +0x1130, angle param_16, h = 0 when FUN_1800082c0 is true): rises sqrt(R^2 - h^2) while moving R - h along the end
  tangent (hk l.10130-10170 region; the base Z FUN_18019ab50 + param_13 NOT DECODED).
- Each arc is kept only when FUN_18019a6a0 (hk l.6834) accepts it - the test is NOT DECODED.
- Measured, op 3: lead-outs rise 3.00 over 2.97 horizontally (a = 0); lead-ins drop 2.954 over 2.48 = R cos 10 deg / R (1 - sin 10 deg)
  with R 3 and MaxRampAngle 10 deg. Raster PARALLELSTEEP op 5 lead-in: 0.075 / 0.075 (a = 0; the Raster plan's MaxRampAngle is not set by
  mckernel's style-4 branch - its default is NOT DECODED).
- Curl (LinkCurlOver / DownRadius): read at hk l.3843 (FUN_180195b80) and passed to FUN_1801d6de0; the arc is NOT DECODED.

## 7. Why nothing is ported yet, and what is next

The parts above do not yet make one complete link of any kind: a stay-down link needs the adjacency graph (FUN_180183a50 / 180185f00 /
180182e20) and the connect path (FUN_1801d2bd0 with the wander FUN_1801d4990 / 1801deaf0 / 1801dde40 and the tool-box model); a retract
needs the path height (+0x30) and ClearSurfaceBy's consumer, the lead acceptance FUN_18019a6a0, and mccore's KO translator for the
clearance / feed-plane moves; the first entry of op 3 is a helix (kind 4 / 5, FUN_1801a9740 / 1801a9e50, not dumped). Porting the leads
alone onto the SAFE retract would mix decoded arcs with an undecoded gouge test, so the SAFE rule stays (ours, labelled) everywhere.
Dumped 2026-11-13 into decomp/hmm_linker_x2.c, not yet read (mchmm.dll, ghidra_hmm; also FUN_1801990c0): FUN_18019a6a0, 18019ab50, 1800082c0, 1801d1500, 1801d4270, 1801d97f0, 1801dab60, 1801db250,
180170b10, 180183a50, 180185f00, 180182e20, 18027a4f0, 1801b9d70, 1801bfbd0, 1801c0320, 1801bf440, 1801ba430, 18016f750, 180279f90,
1802829c0, 180281840, 1801a9e50, 1801a9740, 1801a1d70. Still to dump: FUN_1801deaf0 / 1801dde40 (wander); mccore.dll (ghidra_core): 0x180918220, 0x180919340, 0x180919b70, 0x180919660,
0x1809183e0. Spiral pass joining FUN_18037dc90, the Raster / Spiral / Radial linker defaults (their plan ctors) and the 2D Area Mill's
CAreaClearanceLinkingPlan (ProfileLinkRadius) are not started.

**W7 question (pass span flags, hst3d_port_plan.md 7 "W7")**: the only one of the span-dropping readers that the linker code in
decomp/hmm_link.c / hmm_linker.c / hmm_linkconnect.c and the dump x calls directly is FUN_180017470, once, in the horizontal lead
FUN_1801a1240 (hk l.11317) and with param_4 = 1 (not the dropping mode 0). FUN_180249cc0, FUN_180318bf0 and FUN_18047d1f0 are not
called there. Which reader feeds the Waterline passes into 0x141c (through CMachiningPassesCollection in FUN_1803a0c50 / FUN_18039dc40)
stays NOT DECODED.

## 9. Second pass (2026-11-13, M1b): the output path, the retract polyline, where LinkClearSurfaceBy enters

New dumps (gitignored, main repo decomp/): x3 = decomp/hmm_linker_x3.c (21 functions: CLinkMovement* - FUN_1800066b0, 180006910, 1801cf7b0,
1801cece0, 1801cea60, 1801d0240, 1801d3a60, 1801d3c20, 1801d39e0, 1801deaf0, 1801dde40, 1801df5f0, 1801e0ee0, 1801d5290, 1801d2110, 1801d0890,
1801d1090, 180192510, 18019a310, 1801a56d0, 18027ad40), x4 = decomp/hmm_linker_x4.c (FUN_1801d1c20, 1801d2a10, 1801cf6b0, 1801cf5e0,
1801cf500, 1801cdfd0, 1801cf770, 180006e70, 1800071b0, 180007120, 1803a4820, 1803a46d0, 180007ab0, 1801cc690), ko = decomp/mccore_ko.c and
ko2 = decomp/mccore_ko2.c (mccore.dll, ghidra_core: FUN_180918220, 180919340, 180919b70, 180919660, 1809183e0 / 1809191a0, 1809187f0,
1809190a0, 18091ba70). Vtables (VtableDump on ghidra_hmm): CLinkMovementConnect 180774f60 = [1801e0d20, 1801dd430, 1801e1570, 1801dd0f0,
1801dcff0, 1801d4990, 1801d1e30]; CLinkMovementBase 180774df8; start_reduce<LinPathRange, LinPathRangeWorker> 18075fb58 = [180006e70 dtor,
180007050 execute (NOT dumped), 1800046a0]; finish_reduce<LinPathRangeWorker> 18075fb38 = [1800071b0, 180007120 join].

### 9.1 Output path: MCHSTKOTranslator adds no moves of its own (DECODED)
- mckernel's event handler FUN_1800654c4 (mk l.79293-79558) writes one 0x88-byte record per McHmm event, the record type at +0x7c: 300 -> 0,
  400 line -> 1 (FUN_180065c80; 2 / 0xc / 0x15 in its other mode), 500 arc -> 3, 600 -> 7, 700 -> 8, 800 -> 10, 900 -> 0xb, 1000 -> 0xd,
  1100 -> 0xe, 1200 -> 9, 2200 -> 0xf, 2300 -> 0x10, 2400 -> 0x11. A line's move type (its 4th field) goes through FUN_180065b78 (mk l.79570,
  0..6 unchanged, else -1) into +0x80.
- mccore FUN_180919340 (ko l.58-190) opens HstToolpathKO.tmp, reads the op header (FUN_180919b70) and feeds each record to FUN_1809183e0 (ko
  l.600): 0 = set the current point (+0x194), 1 = line from the current point, 4 = line with its own start (both FUN_1809191a0), 3 / 6 = arcs
  (FUN_1809187f0), 0x12 = FUN_1809190a0, 9 = a type-3 marker (FUN_18091ba70), 7 / 8 / 10 / 11 / 13 / 14 / 19 ignored.
- FUN_1809191a0 (ko2 l.1-80): move type 0 -> G0; any other -> G1 with the feed +0xa0 (type 1), +0x90 (3), +0xa8 / +0xb0 / +0xb8 (4 / 5 / 6),
  set from the op record in FUN_180919b70 (ko l.370-400; which record fields they are is not identified). With +0x103 (and +0x89) a type-0 move
  is written as G1 at +0x108 (a "rapid as feed" option of op type 0x84). Coordinates x +0x188 (the unit factor).
- Consequence: every rapid of an HST op's NCI is a McHmm move of type 0. The Waterline op 3 retract tops (73.119, ...) and the stops at the
  lead-in top + 5 (20.0086 = 15.0086 + LinkRampHeightOffset 5) are made by McHmm (9.2), not by mccore. Which McHmm move gets which type is
  NOT DECODED (the move-type field is set in the McHmm emitters FUN_1801a54f0 / 1801a56d0 / 180192510, x1 / x3, not followed).

### 9.2 The retract polyline (DECODED structure, height NOT closed)
- FUN_1801a1d70 (x2 l.5537-5560): LinkRetractStyle != 0 (+0x1108): FUN_1801d62e0 (x1 l.464-611) makes the two-node path [end (t 0), start
  (t 1.0f)], then FUN_1801d7210 (x1 l.375-464) with (+0x1109 = style 2, linker +0x1e8) gets h = FUN_1801990c0 and inserts (end.xy, h) /
  (start.xy, h) where h is above that end; then FUN_1801dcc30 and FUN_1801d0340 (x1 l.802-869: drops a node when the next segment's length
  differs from the chord by more than 3 % and the turn cosine is above +0x48). Style 0: FUN_1801d6de0 with the curl radii (section 5).
- h (FUN_1801990c0, x2 l.5881-5935): a CLinkMovementConnect (FUN_1801d1c20, x4 l.1-56) run end -> start by FUN_1801d2bd0; style 1: h = its
  +0x30; style 2: h = linker +0x1e8 (LinkZClearance), or +0x30 + 1.0 when +0x30 is higher; then at least both ends' Z.
- +0x30 (FUN_1801d2bd0 x1 l.937-1345 / FUN_1801d2a10 x4 l.57-174): the highest node Z after the nodes are dropped (FUN_1801cf7b0); the
  connect mode *+0x90 in {1, 3}: FUN_180006910 (x3 l.15-166) raises +0x30 / +0x38 to the maximum of a TBB reduce over the path
  (LinPathRangeWorker, body FUN_180007050 NOT dumped: its sampling NOT DECODED); other modes: FUN_1801cece0 (x3 l.376-585, an iterative
  refinement through FUN_1801cf230 / 1801cd6f0 / 1801ce270 / 1801cd2f0 / 1801cc840, NOT DECODED).
- **Where LinkClearSurfaceBy enters (closes the question of section 5)**: the linker's layer-offset table (hl l.4958-4970): +0x1250 =
  ToolThicknessZ (0x1d20), +0x1258 = ToolThicknessZ + LinkClearSurfaceBy (0x1b28, +0x1160) + LinkMinThinningTolerance (0x1b2b, float +0x116c);
  +0x1260 = LinkerProtectThickness (0x1ec9), +0x1268 = that + the same two; +0x1270 = 0, +0x1278 = the same two. A node is dropped at a layer
  i (the connect's int +0x98 / +0x9c, ctor args of FUN_1801cc690 x4 l.657): FUN_1801cf500 (x4 l.247-294) drops the tool from z - off[i]
  (layer 0: CToolDropPos FUN_180222920 with the tool profile linker +0x208 - the drop our hd_tip ports; other layers FUN_1802f4b20) and
  returns drop + off[i], at least z; FUN_1801cf5e0 then the protect model (+0x7c0, FUN_1803a46d0, offsets +0x1260); FUN_1801cf6b0 the
  +0x1280 model (FUN_1803a4820, offsets +0x1270). So the retract clears the surface by drop + ClearSurfaceBy + MinThinningTolerance at layer 1.
  Which layer FUN_1801990c0's connect uses is lost by the decompiler (FUN_1801d1c20's base-ctor arguments are dropped).
- **Check against op 3 (failed, so not ported)**: the maximum of hd_tip (D 12 r 3 cutter, 2000 samples) along the straight XY segment from
  each lead-out end to the next lead-in top is 0 - 15 on most links, while the stored rapid tops are 67 - 75 (= 42 - 50 + 25). The straight
  cutter drop + 25 does not give Mastercam's heights: either the connect's drop includes the tool assembly (holder / shank in the profile
  +0x208 - the tool box model, NOT DECODED, as HST_NOFLUTE_WHY) or the connect is not the straight segment (mode 3, FUN_1801d2110 /
  FUN_1801cece0). NOT DECODED.

### 9.3 The lead acceptance FUN_18019a6a0 (x2 l.1-88, partly)
A connect (FUN_1801d1c20 + FUN_1801d2a10) from the lead point param_2 to param_4 (both dropped at the connect's layer); returns 1 when a node
of it is at or above param_3's Z or param_3 is nearer a segment of it (FUN_18005ab30) than |param_2 - param_3|. Same height dependency as 9.2.

### 9.4 Raster PARALLELSTEEP op 5: what the stored links are
The stored NCI is one feed path (link_stats "retracts 2" = the first approach and the final retract): every pass end is joined to the next
pass by a stay-down loop (the 45-degree zigzag's ends 0.0283 apart in XY < LinkMaxStayOnSurface 0.04; the passes are adjacent). Each loop
leaves the containment by up to 0.0106 in., turns, and comes back tangent to the next pass (e.g. moves 30-43: (-0.4676, 0.4999) ->
(-0.5106, 0.4428) -> (-0.5, 0.4392)), all at Z 0. That shape is the stay-down connect (FUN_1801d2bd0 with the wander FUN_1801deaf0 /
1801dde40 when +0xa0, FUN_1801df5f0 / 1801e0ee0 when +0x238 > 0, then FUN_1801cea60 / 1801dcc30), NOT DECODED; so the stay-down decision
(section 5, decoded) cannot be ported without inventing the loop.

### 9.5 Where M1 stops (2026-11-13, second pass)
Nothing is ported: no link of either chain closes. (a) Raster stay-down needs the connect's loop (9.4). (b) Waterline retracts need the
connect height (9.2: LinPathRangeWorker FUN_180007050 or FUN_1801cece0, and the tool assembly in the drop), the move types (9.1) and the
lead acceptance (9.3). Next dumps: FUN_180007050 (LinPathRangeWorker), FUN_180222920's profile at linker +0x208 (what the tool profile
holds: cutter only or holder too), FUN_1801d2110, FUN_1801cea60's callees, FUN_1801dcc30's callees, the callers' FUN_1800066b0 argument
lists in disassembly (objdump around the FUN_1801d1c20 calls, to recover the connect layers).


## 10. Third pass (2026-11-13, M1c): the retract connect's height (nearly closed), the stay-down connect's arguments

New dumps (gitignored, main repo decomp/): x5 = decomp/hmm_linker_x5.c (31 functions: the LinPathRange TBB reduce FUN_180007050 / 180007200 /
1800074f0 / 1800075c0 / 180007740 / 1800078c0 / 1800078e0 / 180007980 / 180007c90 / 180007fd0 / 180006f00 / 180006f80; the layer drop FUN_1802f4b20;
the wander's callees FUN_1801cfd50, 1801de1f0, 1801de3a0, 1801de920, 1801e07f0; FUN_1801df5f0's callees FUN_1800561f0, 1801ce930, 1801cfcf0,
1801ddaa0, 1801df160; FUN_1801cfda0, 1801ce270, 180051d20, 180060d70, 1801cc840, 1801cd2f0, 1801cd6f0, 1801cf230), x6 = decomp/hmm_linker_x6.c
(FUN_1801d0b20, 1801cd970, 180007eb0, 1803a4970, 1801d1e70, 1801a8fe0, 180195760, 1800082c0, 1801d0950). Disassembly: objdump of
~/mastercam-re/files/mchmm.dll (the constructor arguments the decompiler drops).

### 10.1 The connect's constructor (DECODED from the disassembly)
CLinkMovementConnect FUN_1801d1c20 -> CLinkMovementConcurrent FUN_1800066b0 (passes its stack arguments through unchanged) -> CLinkMovementBase
FUN_1801cc690 (1801cc6b7-1801cc81e). Base fields: +0x70 the linker emitter, +0x78 = emitter +0x508 (the EvaGeneralLinkerPlan), +0x50 arg 4,
+0x88 arg 3, +0x98 / +0x9c arg 5 / 6 (the drop layers), +0x90 arg 7 (pointer to the connect mode), +0xa8 arg 8, +0xb0 arg 9, +0xb8 arg 10,
+0xd0 arg 11, +0x80 arg 12, +0xa0 arg 13 (the wander flag: FUN_1801d2bd0 runs FUN_1801deaf0 / 1801dde40 when set), +0x68 arg 14, +0x48 arg 15.
- **Retract height connect** (FUN_1801990c0 1801990c0-180199217): mode = FUN_180177a90(buf, **1**) (it writes edx into the first int and
  leaves edx / r8 untouched, so the wander flag arg 13 = dl = **1** and arg 3 = r8 = emitter +0x280 when the plan kind +0x1058 != 3 and the
  vector emitter +0x3b8..+0x3c0 is not empty, else 0); both layers = (param_4 == 0) - **layer 1 for LinkRetractStyle 1**, layer 0 for
  style 2 (param_4 = plan +0x1109); arg 8 = 0.0, args 10 / 11 / 15 = -1.0 (DAT_1807bbd50), arg 12 = *(emitter[0] + 0x450).
  FUN_1801d2bd0 is called with param_4 = 0 (no FUN_1801df5f0 / 1801d5290 / 1801d0340). Mode 1 -> FUN_180006910 (the TBB reduce).
- **Stay-down connect** (FUN_180199220 -> FUN_1801a8fe0 -> FUN_1801d1e70, call at 1801995aa): layers **0 and 1**; mode = FUN_180199220's own
  argument (rbp+0xc50); arg 8 = 0x40c3878000000000 (9999.0); arg 13 (wander) = 1; +0x198 = plan +0x1158 (ClearSurfaceWithin); +0x1a8 = float
  plan +0x1168, +0x48 = its cosine; +0x68 = plan +0x1120 (LinkSurfaceRampSmooth); +0xd0 = plan +0x1208 in mode 3, else 0; +0x200 = float plan
  +0x110c; +0x238 / +0x240 = plan +0x1148 (LinkSmoothingRadius, 0 unless retract style 0) / +0x1150; +0x1a0 = float plan +0x116c
  (LinkMinThinningTolerance); +0xb0 / +0xb8 = plan +0x1178 / +0x1180; +0x188 = the pair at plan +0x1188; +0x80 = emitter +0x38. FUN_1801d2bd0
  gets param_4 = 0 (1801995ea). Before it FUN_180199220 saves the four (profile, offset) pairs (FUN_1803a42d0) and inflates them by
  SurfaceRampOffset 0x1ecb (plan +0x1238, default 0: FUN_1803a4200 adds it to +0x1250..+0x1268 and radially to the four profiles).

### 10.2 The drop layers (DECODED)
- Plan +0x2d0 = the tool profile +0x208 through FUN_18013e330(out, +0x208, ToolHolderThickness 0x1b56, LinkClearSurfaceBy + LinkMinThinning-
  Tolerance, 0) (FUN_1803a3f50 hl l.9512-9540; xmm2 / xmm3 at 1803a3fe4-1803a3ff4): the cutter grown by ClearSurfaceBy + MinThinningTolerance
  (FUN_18013e330 adds its 4th argument to [0] shaft and [1] corner; FUN_18013e530 offsets the side-profile segments, the holder ones by the 3rd
  + 4th). +0x2c8 = (ClearSurfaceBy + MinThinningTolerance == 0). +0x17a0 = +0x208 and +0x1860 = +0x2d0, both grown by (+0x1260 - +0x1250)
  when that is not 0 (the protect layers).
- LinkMinThinningTolerance 0x1b2b default "min(LinkClearSurfaceBy, 0.1)" (hl l.12207-12213); mckernel does not write it.
- The drop at layer i (FUN_1801cf500): layer 0 FUN_180222920 with profile +0x208 (CToolDropPos flag +0x58 = 0); layer != 0 FUN_1802f4b20 (x5):
  CToolDropPos on the drive model with profile **+0x2d0** and flag +0x58 = **1** (FUN_180223650 then always runs the side-profile pass,
  FUN_180229ac0: the shank / holder count); +0x2c8 set: the plain drop of +0x208.
- So a layer-1 node height = drop(cutter + ClearSurfaceBy + MinThinningTolerance) + ToolThicknessZ + ClearSurfaceBy + MinThinningTolerance:
  the tool clears the model by ClearSurfaceBy + MinThinningTolerance in 3D. FUN_1801cf7b0 then also takes the protect model (+0x7c0, offsets
  +0x1260) and, when arg 3 (emitter +0x280) holds more than one region, the region path (FUN_1801cf6b0 model +0x1280, FUN_1801cdfd0 region
  Z +0x10 / +0x18 + plan +0x1270[i]) - that path is NOT DECODED (what the emitter +0x280 regions are).

### 10.3 The reduce (DECODED structure)
FUN_180006910: +0x30 = the highest node, then a tbb::parallel_reduce (start_reduce<LinPathRange, LinPathRangeWorker>, execute FUN_180007050 ->
FUN_180007200; split FUN_180007c90; body FUN_180007fd0). The body walks the path's segments: a segment is subdivided (FUN_1801cd970) when
FUN_1801cf230 reports a collision (FUN_1801ce270) or its XY length^2 > *(arg 4 + 8); the new node is the XY midpoint (FUN_18000b4e0 with 0.5), its
start Z the mean of the ends' Z (wander flag set; else +0xa8), dropped by FUN_1801cf7b0 at layer +0x98; the subdivision stops when the new node is
within *(arg 4 + 0x10) of the chord (FUN_180059560) or the segment is short. The worker keeps max node Z, max |Z - start Z| (+0x38) and "some
node rose"; the join takes the maxima. So **+0x30 = the highest dropped point along the straight XY segment** (to the subdivision tolerance).

### 10.4 Check against the stored retracts (measured; probe reverted)
A probe (env-gated, not committed) computed, per stored retract of TUTORIAL 2 SWARF_METRIC op 3 (Waterline, D12 r3, stock 1 / 1, style 1,
ClearSurfaceBy 25) the max over 400 samples of the straight segment lead-out end -> lead-in top of hd_tip(flat R - Rc, corner Rc + ToolThicknessZ
+ 25 + 0.1) + ToolThicknessZ + 25.1. **Stored top - this = 1.000 +- 0.005 on all 37 links** (73.119 vs 72.115, 74.639 vs 73.639, 67.122 vs
66.123, ...). Same check on op 4 (D10 ball, stock XY 1 / Z 0, same surfaces, style 1): **1.000 +- 0.012 on 17 of 18 links** (the first,
39.355 vs 38.685, is 0.67). Variants ruled out: the tool grown by 26.1 with offset 27.1 (-0.01 .. -0.15, varying); the corner without the stock
(2.03). So the height is the inflated drop along the straight connect, as decoded, **plus a constant 1.0 mm whose source is NOT DECODED**: it is
not the style-2 "+ 1.0" of FUN_1801990c0 (both ops have LinkRetractStyle 1, record 0x16c4 = 1, so layer 1 and h = +0x30), not ToolThicknessZ
(op 4 has 0), not SurfaceRampOffset (default 0, and it would grow the radius too). Candidates still open: the region path of FUN_1801cf7b0 (emitter
+0x280 = the vector at emitter +0x3b8, FUN_1801cdfd0), the protect model +0x7c0 with LinkerProtectThickness 0x1ec9 (its default is not in
decomp/ - registered by another plan's constructor), and the CToolDropPos side-profile pass with flag +0x58 = 1 (FUN_180229ac0, not dumped).
**Not ported**: porting the drop without the 1.0 would deviate from every stored retract by 1 mm, and adding it is improvisation.

### 10.5 The Raster stay-down loop (a): status
The loop's connect is the stay-down connect of 10.1 (layers 0 / 1, wander on, LinkSmoothingRadius 0 for Raster's retract style 2, so no
FUN_1801df5f0 / 1801e0ee0). Its mode comes from FUN_180199220's caller (not traced). The wander (FUN_1801deaf0 walks windows of 2-6 nodes
with the flags 2 / 8 / 0x80; FUN_1801dde40 walks node triples through FUN_1801e07f0 and moves them with FUN_1801de920 / 1801de3a0, giving up
after 100 tries each) and FUN_1801cea60 / 1801dcc30 are dumped (x3, x5) but not read to the end. The stored loop of op 5 (moves 31-43:
(-0.5, 0.4675) -> (-0.5106, 0.4428) -> (-0.5064, 0.4354) -> (-0.5, 0.4392)) is a long arc out along the pass direction and a tight turn of
about 0.004 back to the next pass; which of these functions makes it is NOT DECODED, so (a) stays SAFE.

### 10.6 Where M1 stops (2026-11-13, third pass)
Nothing ported; oracles unchanged (link_stats: Waterline op 3 retracts ref 39 / ours 56, rapid XY 481 / 1323, plunges at feed 44 / 55, tops ref
67.1-100 / ours 76.7; Raster op 5 retracts 2 / 479, rapid XY 0 / 13.6, plunges 2 / 478). Next: find the 1.0 mm of 10.4 - dump FUN_180229ac0,
FUN_18022c690 / 18022c6f0 / 1802887a0 (the region tests of FUN_1801cdfd0) and find who fills emitter +0x280 / +0x3b8 (grep the emitter
constructor for +0x3b8 writes) and the LinkerProtectThickness default and the +0x7c0 model; then the retract (b) ports as: per link, h =
max over the straight XY segment of the layer-1 drop (10.2) [+ the 1.0 once decoded], at least both ends' Z (FUN_1801990c0), the vertical
points (FUN_1801d7210), the lead-in top stop at + LinkRampHeightOffset (9.1) - plus the lead arcs (6), whose acceptance FUN_18019a6a0 (9.3) uses
the same connect.

## 11. Fourth pass (2026-11-13, M1d): the 1.0 mm is still open; the linker's tool and protect defaults; the stay-down connect's mode

New dump (gitignored, scratch): x7 = Ghidra DecompAt of mchmm.dll FUN_1802f5270 (the tool profile builder), 1801dd980, 1801daf90, 1803a4130,
180140340, 1801154a0, 180112d30, 1802f4c80. "dis" = objdump of ~/mastercam-re/files/mchmm.dll; "builtin" = the expression interpreter's
(declaration, function) table at 1809a1670 (pencil.md 1.1); default expressions were read from the FUN_18038b890 call sites (the string pieces
and the FUN_18038ba40 parameter names before each call).

### 11.1 Decoded
- **LinkerProtectThickness 0x1ec9** default = `LinkProtectThickness(MachiningPasses, ToolThicknessZ)` (dis 1802fdd76-1802fde1f, kind-7 plans).
  The built-in (builtin entry 207 at 1809a2360 -> 1802a8120, `real LinkProtectThickness(const entity& ekPassesPlan, const real& rThickDefault)`)
  returns rThickDefault unless the passes plan is a "Convert To Primary Output Plan" 0x1458 (mk l.93741) whose SourcePlan is an Edit Passes to
  Surfaces plan 0x141f with OutputID 0x1cb2: then that plan's parameter 0x1b51 (dis 1802a8218-1802a8453). So for Waterline / Scallop the protect
  thickness +0x1260 = ToolThicknessZ, (+0x1260 - +0x1250) = 0 and the protect profiles +0x17a0 / +0x1860 are the layer profiles unchanged
  (FUN_1803a3f50 hl l.9516-9530): the protect model cannot add a pure Z offset. mckernel writes LinkerProtectThickness only in the op-6
  branch FUN_18007a300 (mk l.90807-90834, from tmpmmlu.tmp +0x150), not for these ops.
- **The linker's tool (plan kind 7)**: EvaMachLocBasePlan's constructor builds +0x208 from ToolShaftRadius (+0x138), **LinkerToolCornerRadius
  0x1b71** (+0x130) and **LinkerToolThickness 0x1b70** (+0x140) when the plan kind is 7, else ToolCornerRadius / ToolThicknessZ (FUN_1802f2340,
  dis 1802f2ca6-1802f2cff), through FUN_1802f5270 (x7: FUN_18013e200(shaft, max(corner, -thickness)) then FUN_18013e330(.., ToolHolderThickness,
  thickness, 0); the side profile from LinkerToolFluteLength 0x1c82 instead of ToolFluteLength 0x1b58, ToolBodyDefinition 0x1c26 shifted by
  2 (ToolThicknessXY - ToolThicknessZ), ToolHolderDefinition 0x1b38, ToolTaperAngle 0x1b53). Defaults (kind-7 block dis 1802fd84e-1802fdec2):
  LinkerToolThickness = `ToolThicknessZ - ((LinkCutterNarrowByPercent / 100.0) * 2.0 * (ToolThicknessZ + ToolShaftRadius))`;
  LinkerToolCornerRadius = `LinkerToolThickness < 0 ? max(ToolCornerRadius, -LinkerToolThickness) : ToolCornerRadius`; LinkCutterNarrowByPercent
  0x1b29 default 0.02 (hl l.12191-12196, DDV 0..100). So the linker's cutter is narrower by 0.0004 (ToolThicknessZ + ToolShaftRadius) (op 3:
  0.0028 mm) while the layer offsets +0x1250.. stay ToolThicknessZ-based. ToolShaftRadius default = `ToolShaftRadiusUnAdjusted +
  (ToolThicknessXY - ToolThicknessZ)` (dis 1802fbf06): the profile's flat is R - Rc + XY stock - Z stock (op 4: 1 mm; measured, 11.2).
- **The stay-down connect's mode** (10.5 open point): FUN_180198f00 (hk l.5808-5860, dis 180198f00-18019908e) passes its link-kind pointer
  (param_5) as FUN_180199220's 5th argument, which is the connect mode (rbp+0xc50 = entry rsp+0x28). So the mode is the link kind: 1 (stay
  down) -> the TBB reduce FUN_180006910; 3 (surface ramp) -> the reduce plus FUN_1801d3a60 / 1801d4270 / 1801d3c20 / 1801d39e0 and, in the
  constructor, FUN_1801d2110 (dis 1801d1e00-1801d1e0f). Raster PARALLELSTEEP op 5's loop is a mode-1 connect with the wander on.
- The retract polyline's ends: with LinkRampDownToPasses 0x1cea off and retract style != 0 the exit / approach come from FUN_1801d69f0
  (x1 l.108-214): FUN_1801dab60 (x2 l.1041-1160) lifts the lead-out end by LinkRampHeightOffset (+0x1170) and then to FUN_1801dd980 (x7:
  the region heights of FUN_1801cdfd0 when the pass is a 0x13cc pass and emitter +0x88 is set, else unchanged), FUN_1801db250 the same for the
  approach; those two points are FUN_1801990c0's ends (+0x140 / +0x158).
- Constant scan (dis, every rip-relative FP operand): in FUN_1801990c0, 180006910, 180007050, 180007200, 180007fd0, 180007eb0, 1801cd970,
  1801cf7b0, 1801cf5e0, 1801cf500, 1802f4b20, 1801cf230, 1801d1c20, 1801cc690, 1800066b0, 1801d7210, 1801d62e0 the only 1.0 added to a height
  is the style-2 `+ 1.0` at 1801991c3. The 1.0 at CToolDropPos +0x40 (1802f4b20, 180222920, 1803a4820, 1803a46d0) is the drop direction
  (0, 0, 1) of every drop, layer 0 included.

### 11.2 Measured (probe against all stored retracts; env-gated, reverted)
Per stored retract: the max over 2001 samples of the straight segment lead-out end -> lead-in top of hd_tip(flat F, corner Rc + Tz + g) + Tz + g,
g = ClearSurfaceBy + MinThinningTolerance = 25.1.
- op 3 (D12 r3, stock 1 / 1, F = 3): stored - that = **1.000, range 0.9956 .. 1.0089, 37 / 37**; the maximum lies at an end in 32 links
  (t = 0 or 1) and inside in 5 (t 0.14 .. 0.87), all at +1.000.
- op 4 (Scallop, D10 ball, stock XY 1 / Z 0) with F = 1 (= the ToolShaftRadius default above): **0.996 .. 1.012 on 17 / 18**; the first link
  0.6695 (its maximum is inside, t 0.665, in a pocket: tops 39.36 vs 70+ for the others). With F = 0 the residuals are 1.22 .. 1.43 (so the
  flat radius is R - Rc + XY - Z stock, as decoded).
- Ruled out as the source of the 1.0: corner and offset both + 1 (MinThinningTolerance 1.1; op 3 residuals -0.012 .. -0.149, varying); flat
  + 1 (SurfaceRampOffset-like radial growth; op 3 0.45 .. 0.87); the protect model (11.1); LinkerToolThickness (moves the profile by
  0.003, not the offset); FUN_1801dd980 at the ends (the inner maxima carry the 1.0 too; an end-only lift would leave 74.57 where 74.666 is
  stored, link 17 of op 3); the TBB reduce's start Z (mean of the ends: below the stored inner tops); the KO output path (9.1).
- What the 1.0 must be: a pure Z offset on the layer-1 node height (h = drop + ToolThicknessZ + g + 1.0) in both ops (XY stock 1 in both, Z
  stock 1 / 0: a ToolThicknessXY term cannot be told apart from a constant 1.0 mm with these two ops; no third op with a measurable style-1
  retract was found - a scan of the .mcam parts under mastercam-samples/net that tools/mcam_ops.py loads (HST ops with stored NCI) found only these two; WATERLINE.mcam op 2 (inch, style 1)
  fails to mesh in our tessellator, surface 128). The first link of op 4 (0.67) is consistent with the reduce's under-sampling of an inner
  maximum (10.3: the subdivision stops within *(emitter +0x68 + 0x10) of the chord), NOT DECODED (the two tolerances emitter +0x70 / +0x78 are
  not identified).

### 11.3 Where M1 stops (fourth pass)
Nothing ported; oracles unchanged (8 / 10.6). (b) needs the 1.0 (or ToolThicknessXY) term and the reduce tolerances; next candidates, in order:
CToolDropPos with +0x58 = 1 skips the per-cell facet loop of FUN_180223650 (hst3d_boxset2.c l.22907: the +0x680 loop runs only for +0x58 = 0)
and runs FUN_180229ac0's vertex / edge lists +0x610 / +0x640 against `start + profile +0x90` - decompile FUN_180140340 (the side profile's
height, x7) and FUN_1801154a0's flag path to see whether the layer-1 drop measures to the profile's +0x90 reference; the linker toolbox (+0x498)
construction for kind-7 plans in FUN_1802f2340 (not decompiled; dis 1802f2340..); the emitter +0x68 record (+0x70 / +0x78 = the reduce's XY
length / chord tolerances). (a) Raster stay-down: the mode is 1 (11.1); the wander FUN_1801deaf0 / 1801dde40 and FUN_1801cea60 / 1801dcc30 are
still to be read for the 0.0106 in. loop.

## 12. Fifth pass (2026-11-13, M1e): the Raster stay-down link decoded and ported; the 1.0 mm still open

### 12.1 (a) The Waterline 1.0 mm: what was checked this time (still NOT DECODED)
- LinkWanderInitialOffset 0x1b2d (plan +0x1180) defaults to `max(0.006, (StepMachTolerance * 1.1))`, enabled by LinkAllowWandering 0x1eb1
  (+0x1178, default false; hl l.12275-12312, 21600-21640), and goes only to the stay-down connect (+0xb0 / +0xb8, 10.1): not a 1 mm term.
- The `+ 1.0` of FUN_1801990c0 (x2 l.5881-5935) is gated by its 4th argument = plan +0x1109 = (LinkRetractStyle == 2) (hl l.4831; the call
  FUN_1801d7210 x2 l.5552-5556). Both oracle ops write LinkRetractStyle 1 (mk l.89274-89300, K+0x462 unchanged), so it does not apply.
- No 25.4 / 0.03937 / 1 / 25.4 constant in hmm_link.c / hmm_linker*.c / hmm_linkconnect.c / hmm_plans1.c; FUN_180310790 (hp l.679-800,
  BoundaryOutwardsOffsetRad and its neighbours) has no 1.0 term for the linker. LinkClearSurfaceBy = max(retract, K+0x1b0) when FUN_18006fb44
  is true (mk l.89152-89176) - a larger ClearSurfaceBy would also grow the profile (ruled out in 11.2).
- CToolDropPos with +0x58 = 1 (FUN_180223650 hst3d_boxset2.c l.22966-23040): the per-cell facet loop is skipped and FUN_180229ac0 runs when
  FUN_180140340(profile, 0) (the side profile's height at r = 0, hst3d_grid.c l.4245: 0 for r^2 = 0) + start Z - profile +0x90 <= box top +0x428.
  What FUN_180229ac0 measures against (+0x90) was not followed to the end. Stopped here (bounded effort, as asked).
- Still open, in order: FUN_180229ac0 with flag +0x58 = 1 (the layer-1 drop's reference), and a third op with a measurable style-1 retract and
  XY stock != 1 (to tell ToolThicknessXY from a constant 1.0 mm).

### 12.2 (b) The Raster stay-down link (DECODED for links the drop does not lift)
- Link kind (FUN_1801c05c0 hk l.33540-33617): stay down (kind 1) when the XY distance prev end -> next start < LinkMaxStayOnSurface (+0x1118);
  for plan kinds != 0 (not Waterline) no adjacency test unless both passes are region passes (FUN_180283d20: frag type +0x68 == 0x13cc with its
  +0x70 key) or both 0x13fa. Kind 1 -> 3 in FUN_1801c3dd0 (x1) when SurfaceRampStyle 1 (+0x1200) and not (same +0x70 and
  SurfaceRampExludeSameLevel +0x1234). Raster: mckernel style 4 is in the mask 0x590: SurfaceRampStyle = (record 0x16d8 != 1) (mk l.89903-89918).
  The Raster plan (CRasterLinkingPlan, vftable 1807969f8; slot +0x80 = FUN_1803adb40 hl l.13809) defaults: SurfaceRampStyle "1",
  SurfaceRampExludeSameLevel 0x1dcc "true", LinkSurfaceRampSmooth 0x1b20 "true", SurfaceRampSteps 10, SurfaceRampAngleDiff 10.0,
  SurfaceRampMaxDeviation 0x1a80 0.005, SurfaceRampTangentFactor 0x1b61 `EvaluateR(MachiningPasses, MachStepover)`, SurfaceRampScaleByGapSize
  0x1eca "false", SurfaceRampMaxVerticalChange 0x1cfb `2.1 * StepMachTolerance`, SurfaceRampMax / MinPassExtension 0x1ef1 / 0x1ef0 `0.5 *
  ToolCutterDiameter` / `0.1 * max`, LinkMaxStayOnSurface 0x1b1f `LinkRampDownToPasses ? 0.0 : (EvaluateR(MachiningPasses, MachStepover) * 2)`.
  mckernel's block that writes TangentFactor 1 / ScaleByGapSize true / MaxVerticalChange 1e6 / SurfaceRampMaxDeviation 0.0005 (mk l.90052-90082)
  is reached for kernel style 6 only (mckernel.dll 180076fb1 `cmp $0x6,%edx; je 0x1800783cd`); for style 4 SurfaceRampMaxDeviation is written
  only when the cut tolerance is below 0.005 mm / 0.000197 in (mk l.89993-90001).
- Mode 3 connect (FUN_1801d1e70 x6 -> FUN_1801d2110 x3 l.2830ff): the control polygon +0x170 = [P0, P0 + T0 L, P1 - T1 L, P1]; T0 = the unit 3D
  tangent of the previous pass at its end, T1 of the next pass at its start (FUN_18027ad40), L = TangentFactor x (ScaleByGapSize ? |P0 P1| : 1).
  The connect's virtual +8 (FUN_1801dd430) evaluates the curve at a node's float t (+0x40); the first nodes are t 0 / 1.0f (FUN_1801d2bd0 x1).
  The connect's wander flag +0xa0 = both passes' +0x78 (FUN_1801d2110).
- The subdivision (TBB reduce FUN_180006910 x3 -> FUN_180007fd0 x5 -> FUN_1801cd970 x6): a segment (a, b) is split at t = ta * 0.5f + tb * 0.5f
  (float) when FUN_1801cf230 (x5) flags it: (FUN_1801ce270 collision, not followed) or, in mode 3 with LinkSurfaceRampSmooth (connect +0x68), the
  curve point at the middle t lies farther in XY from the infinite line through the curve points at ta / tb than connect +0xd0 = plan +0x1208 =
  SurfaceRampMaxDeviation; else when the nodes' XY length^2 > *(emitter +0x70) and the dropped middle node is farther (3D, the chord segment,
  FUN_180059560 = (clamped s, distance^2)) than *(emitter +0x78); no split when |ta - tb| < FLT_EPSILON. The emitter's +0x68.. block is copied
  from the plan's +0xcb8.. (FUN_1801929d0 hk l.1633-1647, call hl l.6887-6888) and the plan sets it at mchmm.dll 1802ebb0c-1802ebb76:
  +0xcb8 = StepMinHorizontalChange 0x1acf, +0xcc0 = its square, +0xcc8 = StepMachTolerance 0x1acc, +0xcd0 = MaxHorizontalStepChange 0x1bd9.
  Each new node is dropped at layer +0x98 (FUN_1801cf7b0: z = max(curve z, drop + ToolThicknessZ)).
- After the reduce (FUN_1801d2bd0): mode 3 compares the nodes with the curve (FUN_1801d3a60: mean distances of lifted / unlifted nodes from the
  curve at their t; FUN_1801d4270 not read; FUN_1801d3c20; FUN_1801d39e0 drops the lifts) with LinkWanderDistanceRatioLo / Hi (2 / 30, connect
  +0x188 / +0x190); FUN_1801d0240 removes nodes flagged 0x80; the wander (FUN_1801deaf0: a 2-node path is left alone; else FUN_1801dde40) only with
  +0xa0; FUN_1801cea60 removes a node when the connect's slot 0 (FUN_1801e0d20, not dumped) allows it, the layers agree and the shortcut does not
  collide; FUN_1801dcc30 = the path length (+0xc0).
- Emission (FUN_180199220 hk l.6235-6590): when the path rose more than SurfaceRampMaxVerticalChange (+0x38 > 0x1cfb) a pass-extension retry
  (FUN_18019a310 with SurfaceRampMin / MaxPassExtension, a second connect) and then, if still too high, a retract (FUN_1801a1d70); otherwise the
  connect's nodes are the link (FUN_180192510, tag 0x13d4), at the feed.

### 12.3 Check against Mastercam (measured)
PARALLELSTEEP.mcam (2017) op 5 (Raster, inch, D 0.25 ball, stepover 0.02, record 0x16d8 = 0, 0x16cd = 1 with 0x1310 = 16 -> LinkMaxStayOnSurface
0.04, cut tolerance 0x12a0 = 0.001): the stored NCI has 473 U-turn links between passes, all at Z 0. Each is exactly the cubic Bezier above with
L = 0.0200 (= MachStepover; the one link with gap 0.0231 instead of 0.0283 too), and its points are the dyadic parameters of the subdivision. A
simulation of 12.2 (SurfaceRampMaxDeviation 0.005 mm = 0.000197 in, StepMinHorizontalChange 0.003 mm, StepMachTolerance 0.0005) gives the stored
points on **473 / 473 links, worst 4.4e-7 in** (the NCI's rounding). Varying the deviation tolerance: only 0.000195 .. 0.000205 in reproduces all.

### 12.4 Ported (viewer/cam3d_finish.c C3StayDown / c3_staydown, Raster only)
gen_raster joins two consecutive passes with the decoded link when SurfaceRampStyle is 1 (0x16d8 != 1), the XY gap is < LinkMaxStayOnSurface,
and the drop (the passes' own drop, ours) lifts no node and cuts no chord (sampled 8 x per chord, ours); every other link keeps the SAFE rule
(labelled): retracts (the Raster retract height, its leads and the clearance moves are not ported), SurfaceRampStyle 0 (kind 1: the straight
connect with the wander, NOT DECODED), lifted links (FUN_1801d3a60 / 1801d4270 / 1801d3c20 / 1801d39e0, FUN_18019a310: NOT DECODED). The pass
order stays ours (line by line; FragOrderer is not ported), so a stay-down link joins our consecutive passes - the same rule Mastercam applies
to its own order.
Measured (cam_cli, base = the same tree without the port): PARALLELSTEEP.mcam op 5 retracts 479 -> 2 (ref 2), cut length 3154 -> 2553 (ref
2543), ours->ref p95 0.037 -> 0.034, max 1.205 -> 0.855 (ref->ours max 0.061 -> 0.077); RESTPASSES.mcam op 18 (D 0.25 r 0.125) retracts 374 -> 2
(ref 2), cut length 1241 -> 985 (ref 978), ours->ref p95 0.052 -> 0.049; op 19 134 -> 2 (ref 25 retracts: its rest-material passes are not ported,
ours cuts 358 where Mastercam cuts 81, so its links cannot match yet). fileop (205 user ops, no Raster) identical to master.

### 12.5 Where M1 stops (fifth pass)
Next, in order: the Raster retract (style 2: h = LinkZClearance, or the layer-0 path height + 1.0 when higher, FUN_1801990c0 - decoded; the
vertical leads (6) with their acceptance FUN_18019a6a0, the clearance / feed-plane moves of the first approach and the end are not ported); the
lifted stay-down link (FUN_1801d4270, the pass extension FUN_18019a310); the kind-1 straight connect with the wander (SurfaceRampStyle 0) and
FragOrderer's order (FUN_1801b9d70); Spiral / Radial (same plan family: check their plan defaults first); then the Waterline (kind 0: adjacency,
LinkByZLevel key, and the 1.0 mm of 12.1).

## 13. Sixth pass (2026-11-13, M1f): retract height of style 2, lead linearisation and feeds, the lead test's arguments; nothing ported

Sources: dis = objdump of ~/mastercam-re/files/mchmm.dll; ko / ko2 = decomp/mccore_ko.c / mccore_ko2.c (mccore.dll); measured = the stored NCI
of the corpus ops below (probe scripts in the session scratch, not committed: per rapid run the exit / approach feed moves, their feeds, the
rapid top Z and the arc's segment count).

### 13.1 Corpus survey (every .mcam / .mcx-* under mastercam-samples/net that tools/mcam_ops.py loads; op 132 with stored NCI)
34 HST ops with NCI: Waterline 5 (SWARF_METRIC op 3 2017 + X9: LinkRetractStyle 1, stock 1 / 1; TUTORIAL 2 SWARF op 4 2017 + X9 and MOLD BOTTLE
op 4: style 2, stock 0.05 / 0.05), Raster 4 (PARALLELSTEEP 5, STEEPSHALLOW 5, RESTPASSES 18 / 19; all style 2), Scallop 6, OptiRough 4, Hybrid 4,
Pencil 1, Project 1, 9 others. **No Spiral or Radial op has stored NCI**, so their linkers have no oracle (step 4 cannot be checked). **No third
Waterline op with LinkRetractStyle 1 exists** (step 5: the ToolThicknessXY-vs-constant question of 11.2 / 12.1 cannot be settled from the corpus;
SWARF_METRIC op 3 and its X9 copy are the only style-1 Waterline ops).

### 13.2 LinkRetractStyle 2: the retract height is LinkZClearance (DECODED + measured)
FUN_1801990c0 with plan +0x1109 (style 2): h = LinkZClearance (linker +0x1e8), or the layer-0 path height +0x30 + 1.0 (mm, McHmm's unit) when
that is higher (x2 l.5881-5935, dis 1801991c3), then at least both ends' Z. Measured: every rapid top of every style-2 op is the absolute
clearance: RESTPASSES op 19 25 / 25 at 2.0, PARALLELSTEEP op 5 2 / 2 at 2.0, TUTORIAL 2 SWARF op 4 (Waterline) 42 / 42 at 4.0, MOLD BOTTLE op 4
(Waterline) 71 / 71 at 4.0 (clearance K+0x10 absolute, LinkZClearance = clearance + plane origin Z, mk l.89177-89205). So the open 1.0 mm of 10.4
/ 11 / 12.1 concerns LinkRetractStyle 1 only; style-2 heights are closed whenever the path + 1 mm stays below the clearance (true on all 138
stored links: the parts' tops are far below it).

### 13.3 The link's vertical moves and their feeds (measured; move types of 9.1)
Approach side, every op: rapid down to the lead-in top + LinkRampHeightOffset (feed plane K+0x30: 0.025 in / 5 mm / 0.025 in) -> a pure-Z
feed move down by LinkRampHeightOffset -> lead-in arc. Exit side: Raster (PARALLELSTEEP 5, RESTPASSES 19): lead-out arc -> a pure-Z feed move up
by LinkRampHeightOffset (FUN_1801dab60 x2 l.1041-1160) -> rapid to h; Waterline (SWARF_METRIC 3, TUTORIAL 2 SWARF 4): the rapid starts at the
lead-out arc's end (no offset move; op 3's last exit move is the arc's last step, 0.25 of the 3.0 rise) - why the exit lift differs between the
two plans is NOT DECODED (FUN_1801dab60's condition). Feeds (op feed / plunge / retract): SWARF_METRIC op 3 318.2 / 159.1 / 159.1: exit
moves at 318.2, approach moves at 159.1; TUTORIAL 2 SWARF op 4 100 / 50 / 100: exit at 100, approach at 50. So **the approach side (offset move
and lead-in arc) is written at the plunge rate and the exit side (lead-out arc and offset move) at the feed rate** (mccore FUN_1809191a0 ko2 l.1-80:
type 3 = +0x90 the plunge rate, type 1 = +0xa0 the feed; which McHmm emitter sets the types is still not followed, the rates are measured).

### 13.4 Lead arcs are linearised by mccore (DECODED: FUN_1809187f0 ko2 l.83-330, init FUN_180919b70 ko l.439)
A McHmm arc (KO record type 3) that is not "MC compatible" (TP::Utilities::IsHMMArcMCCompatible: not in the XY plane, so every vertical lead) is
written as n lines: sweep = atan2 angle between the arc's start and end radii (0..2 pi), n = ceil(sweep / acos((r - tol) / r)) (at least 1), r in
op units (x +0x188: 1 / 25.4 for inch), tol = translator +0xc0 = **0.45 x the op's cut tolerance** (op struct +0x16d0 for op type 0x84 = record
0x12a0); the points are at equal angle steps, the last one the arc's end exactly. Checked: PARALLELSTEEP op 5 / RESTPASSES op 19 (R 0.075 in,
tol 0.001, 90 deg) n = 15, stored 15 on every full lead; SWARF_METRIC op 3 (R 3 mm, tol 0.025, lead-in 80 deg = 90 - MaxRampAngle 10) n = 17,
stored 17 on 31 / 38; TUTORIAL 2 SWARF op 4 (R 0.075, 80 deg) n = 13, stored 13 on 40 / 41. XY-plane arcs (horizontal leads) stay G2 / G3.

### 13.5 The lead acceptance FUN_18019a6a0 (arguments DECODED from dis 18019a6a0-18019a7c4, 18019fd94-18019fe15)
Lead-out call (18019fdc5): P2 = the lead's pass-side point (rbp-0x40), P3 = the arc centre (rbp+0x60), P4 = rdi (not traced); lead-in call
(18019fe06): P2 = the lead-in top (param_24), P3 = the arc centre (S + dir h, z + sqrt(R^2 - h^2), rbp+0x38), P4 = the pass start S (rbp-0x70,
XY). The connect is FUN_1801d1c20 with **both layers 0**, connect mode 1 (FUN_180177a90(buf, 1)), the region argument as in 10.1, and node start Z
(base +0xa8) = **P3.z - 1.1 |P2 - P3|** (dis 18019a6f9-18019a779, constant 1.1 at 1807bb738; arg 8 = -1.0 at 1807bba88). It runs P2.xy -> P4.xy
(FUN_1801d2a10: nodes at max(base Z, drop + ToolThicknessZ)) and returns 1 when a node is at or above P3.z or the squared 3D distance from P3 to
a segment of it (FUN_18005ab30, dis 18005ab30: point-segment distance squared, clamped parameter) is < |P2 - P3|^2. 1 means the arc meets the
model: the caller then rotates the lead (up to 6 tries, step = |angle range| / 6, hk l.9645, 10249-10262) or falls back (the stored 1-segment
"leads" of RESTPASSES op 19 and SWARF op 4: a straight vertical move of length R) - that fallback and the rotation are NOT DECODED.
**Port blocker found**: for a pass end on a flat stretch (op 5's first and last leads, h = 0) the node at S is at distance exactly R from the
centre, so the strict test `d^2 < R^2` is a tie decided by floating-point rounding of Mastercam's own values; a port can only follow the stored
result there by a rule. N13-21 (user decision): the port evaluates the strict test on its own values exactly as the code does.

### 13.6 Where M1 stops (sixth pass)
Nothing ported; code unchanged, oracles unchanged (8; PARALLELSTEEP op 5 retracts 2 / 2 since 12). Steps of the brief: (1) Raster retract
height closed (13.2), the leads not (13.5: the lead-out base Z FUN_18019ab50 + param_13, the rotation / fallback, the tie) - the retract link
is not ported because its leads are not; (2) lifted stay-down links and SurfaceRampStyle 0: not started (no corpus Raster op has a lifted link:
PARALLELSTEEP 5 / RESTPASSES 18 keep 0 links on SAFE); (3) FragOrderer FUN_1801b9d70: not started; (4) Spiral / Radial: no oracle (13.1);
(5) no third style-1 Waterline op (13.1). Next: decode FUN_18019e010's retry / fallback (hk l.9618-9700, 10215-10330: FUN_18019bba0, FUN_18019b590,
FUN_18019a8b0) and FUN_18019ab50, then port the Raster first approach / exit / retract with 13.2-13.5 (the oracle: PARALLELSTEEP op 5's first and
last leads, RESTPASSES op 19's 23 retracts once its rest passes are ported).

## 14. Seventh pass (2026-11-13, M1g): the Raster retract link, first approach and final exit PORTED (blocked leads keep SAFE)

### 14.1 Decoded / measured for the port (viewer/cam3d_finish.c C3Link, c3_link / c3_link_end)
- Lead-in angle: the first-approach caller (hk l.2984-3013) uses MaxRampAngle +0x11e0 when it is not 0, else the descent angle of the pass's
  start tangent clamped to +-0x1198. The Raster ctor FUN_1803aca70 (hl l.13475-13490) ends with FUN_1803ac690, which registers MaxRampAngle
  0x1a4e with a default built by FUN_1802e5a70(.., 2.0, ..) (hl l.13392-13396) - but **measured**, every stored Raster lead-in follows the pass slope
  (PARALLELSTEEP op 5: a 90 deg arc 0.075 wide at a flat start; RESTPASSES op 19: every fitted arc ends within 3 deg (one step) of the next move's slope, slopes from -35.9 to
  +87.7 deg; a 2 deg MaxRampAngle would give a 0.0724 wide arc). So the port takes the slope (the default expression's evaluation is NOT DECODED;
  the measurement decides). The clamp +0x1198 = DAT_1809bf1a0 x 0.5 is set at run time: slopes up to 87.73 deg are unclamped (measured); ours
  refuses beyond that.
- Lead-out angle (exit caller hk l.5355-5402): atan(Tz / |Txy|) of the end tangent, same clamp; measured mirrors the lead-in (op 19 exits).
- Lead-out base Z = FUN_18019ab50(..) + param_13 (hk l.9675-9680): FUN_18019ab50 (hk l.7014-7140) returns 0 when the horizontal lead's two points
  coincide and otherwise the horizontal arc length x the ramp slope; Raster has no horizontal lead (mckernel's style-4 branch jumps past
  HorizontalLeadIn / OutRadius and MaxRampAngle, mk l.89903-90011 -> LAB_180078c4d; measured: every Raster lead lies in one vertical plane), so the
  base is the pass end's Z.
- Rotation / fallback (FUN_18019e010 hk l.9618-9700, 10215-10330): the retry loop rotates the *horizontal* lead around param_10 (6 tries, step
  |angle range| / 6) and, for a blocked lead-in, re-enters with the approach point moved to the pass start (FUN_18027b070, param_19 = 0,
  hk l.9734-9748); what is emitted after that (the stored straight vertical "leads" of length R) is NOT DECODED. FUN_18019bba0 / FUN_18019b590 /
  FUN_18019a8b0 / FUN_18019c7c0 / FUN_18019afc0 / FUN_18019b180 are the horizontal-lead helpers (not needed without a horizontal lead; not read).
- The test connect's arguments (13.5) and LinkZClearance's record fields: clearance 0x170, absolute 0x178 = 0, on 0x179 (mk l.89177-89205; the
  op struct seen there is the record - 0x160). The term FUN_180096984(op + 0x70) (returns its +0x10) subtracted when the byte at op + 0x48 is 0 is
  not identified; it is 0 on all 138 stored style-2 retracts (13.2), and the port uses the clearance as measured.

### 14.2 Ported (Raster, LinkRetractStyle 2 with the clearance on and absolute)
A link whose XY gap is >= LinkMaxStayOnSurface (kind 2), the first approach and the final exit: lead-out arc + LinkRampHeightOffset up (feed), rapid
to LinkZClearance, across, down to the lead-in top + LinkRampHeightOffset, down to the top and the lead-in arc (plunge rate); the arcs as mccore
writes them (13.4). Each lead runs the test FUN_18019a6a0 on our own values (N13-21); a blocked lead (rotation / fallback NOT DECODED), a lead whose
answer would change with FUN_1801cea60's node removal (NOT DECODED: ours requires the full polyline and the bare chord to agree), a path within 1 mm of
the clearance (the reduce's sampling, NOT DECODED), a slope beyond 87.73 deg, or an off / incremental clearance keep the SAFE link (labelled).
Gaps below LinkMaxStayOnSurface stay as in 12 (the stay-down link, or SAFE when it is lifted / SurfaceRampStyle 0).

### 14.3 Measured (cam_cli raster, MCAM_3D_DEBUG=1 prints the link counts)
- PARALLELSTEEP op 5: 477 stay-down links; the final exit ported (lead-out arc, + 0.025, rapid to 2.0, as Mastercam); the first approach keeps
  SAFE: its lead-in test is the flat-start tie of 13.5 and comes out 1 (blocked) on our values (the polyline's last node is S, squared distance
  0.005625 vs R^2 0.005625, decided by rounding). retracts ref 2 / ours 2; cut length 2553 (ref 2543); ref->ours max 0.077 -> 0.067, p95 0.013 / 0.034
  unchanged.
- RESTPASSES op 18: same picture (final exit ported, first approach the same tie, SAFE); retracts 2 / 2; ours->ref p95 0.049 (unchanged).
- RESTPASSES op 19: ours has no kind-2 link (our passes are not its rest passes: 132 stay-down links); ref 25 retracts / ours 2.
No corpus op exercises a ported inter-pass retract yet (the only Raster op with retracts is op 19, whose passes are not ported).
Regression vs master run rg_new7: fileop 0 / 205 differ; op_compare 51 / 635 (master 47): the 4 new are s4_check_trim and s4_check_trim_stock
(.txt point counts, posted NC), whose Raster passes are now joined by the ported retract link (lead-out arc R 2 in 14 lines at the feed, + 0.5 feed
plane, G0 to the clearance 50, across, down to the lead-in top + 0.5, F1000 plunge to the top, lead-in arc to the pass) instead of SAFE - re-blessed.

### 14.4 Pass order (FragOrderer), partly decoded; not ported
- Raster's LinkSimpleOrder = (K+0x45f == 0) (mk l.89920-89925, kernel styles in the mask 0x590 other than 10); K+0x45f = record **0x16c0** (the TP copy
  FUN_180923f60 d4 l.161: m 0x4bf <- op 0x1658). PARALLELSTEEP 5 and RESTPASSES 18 / 19 have 0x16c0 = 1, so LinkSimpleOrder is false and the order
  runs through the adjacency graph (section 4.5): the previous pass's graph neighbours first (FUN_1801c2d70), then FUN_1801bfbd0 / FUN_1801c0320,
  then all remaining (FUN_1801b9820 / FUN_1801b9cc0). The adjacency graph for plan kinds other than Waterline is NOT DECODED, so the order is not
  ported (ours: line by line, which gives Mastercam's order on PARALLELSTEEP 5 / RESTPASSES 18: all 477 / 372 links are the stored stay-down links).
- FUN_1801b9d70 (hk l.28442-28627, x1 l.1650-1760, x2 l.2135-2182): the "nearest remaining" step: the candidates not yet visited (hash set at
  orderer +0x80 / +0x90 / +0xa8 / +0xb0) are kept sorted by entity key; FUN_1801c9500 scores each with FUN_18027a4f0 = the **squared 3D distance**
  from the previous pass's end to the candidate's start (its end when the reversed flag +0x79 is set; for a closed pass, +0x78, the loop point
  FUN_18027a590 nearest to it) and inserts (score, key) into an ordered map; the smallest score wins, an equal score keeps the candidate with the
  smaller entity key (map insert does not replace); with the remap at orderer +0x390 the result goes through it. FUN_1801c9390 / FUN_1801bf440
  (the prerequisite filter) not read to the end.

### 14.5 Eighth pass (M1h): the adjacency graph and the next-pass chain (decoded in part; not ported)
- **Where the graph comes from**: the linker reads EvaFragAdjacency from the passes collection's +0x190 key (hl l.4491, 5491-5508: FUN_1800c1390 then
  a dynamic cast). The Raster grid plan (FUN_1802eb170, decomp/raster/tb2.c l.138-215) creates the CFragAdjacency entity and adds each pass through
  FUN_1802ed200 (raster/tb.c l.1086-1112, called from raster/grid.c l.1578), which writes only the two pairs (pass, -1) and (-1, pass) into the
  adjacency container (+0xfb8 + 0x20, FUN_18009e8d0). The only writer of real pairs, FUN_18031f660 (scallop3d/ad.c l.7229, proj/h2.c), is reached
  from EvaAxiallyOffsetPassesPlan's evaluator FUN_180320750 (vtable 180788360 [1]; the only call site 180320a01) - not from the Raster grid. So a plain
  Raster op's passes have **no neighbour edges**; FUN_180183a50 runs only for kind 0 with +0x1060 (hl l.5531-5533).
- **Next pass (FUN_1801b90d0 hk l.27841-27987)**, OrderingType != 0: (1) the graph neighbours of the previous pass (FUN_1801c2d70 -> FUN_1801b9820):
  none for Raster; (2) FUN_1801bfbd0 (x2 l.2396-2474): the previous pass's group from FUN_1801bfd80 / FUN_1801bff00 (hk l.32777ff: a cached group
  built from the adjacency - not read to the end), the members FUN_1801bf440 lets through (else all of them), chosen by FUN_1801b9820; (3)
  FUN_1801c0320 (x2 l.2475-2579): the cluster's list +0xf60 (or, with OrderingType 2 and kinds in the mask 0x96, the FUN_1801ca390 subset) filtered
  by FUN_1801bf440, else all passes +0xf50, chosen by FUN_1801b9820; (4) every pass. Then FUN_1801b9490 / FUN_1801c34d0 and, with +0x1107, the stack
  at +0xea8..+0xef0 (not read).
- **FUN_1801b9820 (hk l.28139-28370)**: with no previous pass (+0xf08 = 0): the candidates scored by FUN_18027a4f0 against the start point
  (FUN_1803a3ae0) in an ordered map; the nearest N is taken, then the map is walked in score order and a candidate C replaces the choice when the XY
  distance from N's **start** (+0x88, +0xb0 when reversed) to C's **end** (+0xb0, +0x88 when reversed) is <= LinkMaxStayOnSurface (+0x1118) (and,
  for kind 0, C is N's neighbour) - the last such candidate wins. With a previous pass: FUN_1801b9cc0 -> FUN_1801b9d70 (14.4): the candidate whose
  start is nearest (squared 3D) to the previous pass's end.
- Still NOT DECODED before the order can be ported: FUN_1801bff00 (the group), FUN_1801bf440 (the filter), the clusters for Raster (FUN_18039f1a0
  with LinkORClusterEnablePreOrder, which mckernel sets to true on one style branch, mk l.89632-89640 - which styles reach it not traced),
  FUN_1801b9490 / FUN_1801c34d0 / the +0x1107 stack, the start point FUN_1803a3ae0's value for Raster (LinkStartX / Y, mk l.90240), and the
  direction pass FUN_1803a1ec0 for a bidirectional plan whose graph has no edges (the zigzag). Our order stays line by line.

## 15. Ninth pass (2026-11-13, M1i): Raster pass order and the blocked-lead control flow decoded further; nothing ported

New dumps (mchmm.dll, ghidra_hmm, DecompAt.java; decomp/ is gitignored): x7 = decomp/hmm_linker_x7.c (FUN_1801db570, 1801dbf80, 1801caca0,
1801ca390, 1801c9200, 1801c98a0, 1801bb8a0, 1801d0340, 1801dd980, 1801d2940, 1801a47b0), x8 = hmm_linker_x8.c (FUN_1801db820, 1801d7b00,
1801d7450, 1801d91e0, 18027f1d0, 180280990, 1801cfc70), x9 = hmm_linker_x9.c (the TBB task bodies 1801cb550 / 1801cb680 / 1801cb460 of the
FUN_1801ba430 parallel loop), x10 = hmm_linker_x10.c (FUN_1801cece0, 1801e0ee0, 1801d9330, 1801d87b0, 1801d76a0, 1801d0240, 1801cfbd0,
1801bb970, 1801bc1a0). Code and oracles unchanged (15.4).

### 15.1 Raster pass order (DECODED in structure; three inputs NOT DECODED, so not ported)
- **Raster is plan kind 1**: the Raster passes plan is 0x1404 (hst3d_port_plan.md 3), FUN_1803ab250 (hl l.12945-12985) maps 0x1404 / 0x140c /
  0x140d / 0x140f / 0x1412 / 0x1417 / 0x1418 / 0x1433 / 0x1445 / 0x147a to kind 1 (Waterline 0x1436 0, 0x1435 2, 0x143a 3, 0x1460 4, 0x1466 5,
  0x146f 6, 0x1474 1 or 7 by 0x1eb7).
- **Direction (zigzag)**: FUN_1803a1e50 (hl l.7915-7926) runs FUN_1803a1ec0 only for kinds in the mask 0x9d (0, 2, 3, 4, 7); kind 1 goes to
  **FUN_1803a2750** (hl l.8313-8640): the collection's passes in order, cut into groups of consecutive passes with the same frag +0x70; group g
  (counted from 0, one per group, not per raster line) is reversed when LinkReverseDirection (+0x1061) xor (LinkDirectionStyle == 1 (+0x1060) and g
  odd) - reversed = every pass of the group reversed (FUN_180281840) and the group's passes in reverse order (hl l.8573-8620). mckernel's style-4
  block (mk l.89806-89860) writes LinkDirectionStyle 1 when K+0x45d (Zigzag) and K+0x46f == 0, 2 / 3 for Down / Up Mill (K+0x46f 1 / 2), else 0,
  and LinkReverseDirection when K+0x470. **NOT DECODED: what frag +0x70 is for a Raster pass** (written by the grid plan's FUN_1802ed680 /
  FUN_1803632f0, raster/grid.c l.1555-1600, not followed). If it is the raster line, the only difference from ours (`rev = zig ? (i & 1) : other`,
  cam3d_finish.c gen_raster) is that Mastercam's parity skips lines that hold no pass; ours counts every line. Found at the end of this pass:
  frag +0x70 is the key of the frag's CTessToolpath (raster/tb.c l.870-880: FUN_1802ed680 opens a CTypedDtbObjectPtrR<CTessToolpath> from
  frag +0x70 and reads the frag's segment type at +0xd0); how many CTessToolpaths the grid plan makes (one per raster line or one per op)
  is the open question - with one per op there would be a single group and the zigzag would have to come from the grid plan itself
  (FUN_1802ed680 also reverses its frags, FUN_180281840 at tb.c l.900-925, under a condition not read).
- **Ordering parameters** (Raster plan FUN_1803acd10 hl l.13555-13790, mckernel writes none of them for style 4 except LinkSimpleOrder):
  LinkOrderingType 0x1dce "1" (+0x10e0), LinkORWholeLevelBeforeNext 0x1b0e false (+0x10d0), LinkORLimitRangeSameLevel 0x1b0f true (+0x10e4),
  LinkORSameLevelOverlap 0x1b10 `EvaluateR(0x1ab0, 0x1ad9) * 1.5` (+0x10e8), LinkORSameLevelUseApproach 0x1b11 false (+0x10f0),
  LinkORLimitRangeAdjacentLevel 0x1b12 true (+0x10f1), LinkORAdjacentLevelOverlap 0x1b13 (+0x10f8), LinkORMeasureInUV 0x1b14 true (+0x1100),
  LinkORAccurateClosestPair 0x1b15 false (+0x1101), LinkORRestrictOverlapForReversed 0x1b18 (+0x1105), LinkORCoagulateBunches 0x1b19 false,
  LinkORUsePrerequisiteStack 0x1b1a false (+0x1107), LinkORClusterEnablePreOrder 0x1ce6 false, LinkORCustomSame / AdjacentLevelOverlap 0x1ecc /
  0x1ecd `Format("%d", 0)` = 0 (+0x10d4 / +0x10d8) (names: hmm_params_reg.c l.1754-1830, field offsets hl l.4719-4810).
- **Clusters**: FUN_18039ed00 (hl l.5913-5990) -> FUN_18039f1a0 (hl l.6062ff): with LinkORClusterEnablePreOrder (0x1ce6) false one cluster = all
  passes (FUN_1803a4ed0 on a copy of +0xff0, hl l.6187-6196). LinkStartX / Y (0x1a61 / 0x1a62) are read into +0x1768 / +0x1770 (hl l.5967-5978).
- **FragOrderer ctor for OrderingType 1** (hk l.27000-27030): FUN_1801b8e00 (hk l.27723-27790) only indexes the passes (+0x40 by entity key,
  +0x00 by frag +0x70); FUN_1801bb8a0 needs SameLevelUseApproach (false); with WholeLevelBeforeNext false FUN_1801ba430 (x2 l.2886) runs a TBB
  loop (task bodies x9 -> do_iteration_task vtable 180774ae8, execute 1801cbab0 -> its only call FUN_1801ba5e0 hk l.28783) that
  fills each pass's same-level list +0x110: FUN_1801ba810 only with +0x10d4 > 0 (0 here), FUN_1801ba970 (hk l.28973) with +0x10d8 < 1 = the pass's
  adjacency-graph neighbours (FUN_1801c3cb0 hk l.35609: FUN_180183b80 / FUN_180184200 on +0x1028). **A Raster op has no graph edges (14.5), so
  every list is empty.** (FUN_1801bb970 / FUN_1801bc1a0, x10, are other task bodies; FUN_1801bc1a0 runs the same-level overlap test
  FUN_1801c2940 with +0x10e8 / +0x1100 / +0x1101 - which loop spawns it was not traced.) The prerequisite container +0xf20..+0xf38 is filled only by FUN_180170520 (LinkByZLevel, kinds in 0x9d): empty for Raster.
- **Next pass (FUN_1801b90d0) for Raster**: (1) graph neighbours: none; (2) FUN_1801bfbd0 -> FUN_1801bfd80 / **FUN_1801bff00** (hk l.32836-32960):
  the group = the closure of the previous pass over the +0x110 / +0x340 lists (or, with WholeLevelBeforeNext, over FUN_1801c3cb0 both ways)
  minus the set +0xfb0 ([inferred] the passes already cut) - empty for Raster, so FUN_1801b9820 gets no candidate; (3) FUN_1801c0320 (x2 l.2475): +0xf38 == 0 and
  OrderingType != 2 -> the cluster list +0xf60 minus the passes **FUN_1801bf440** (x2 l.2584-2885) blocks, chosen by FUN_1801b9820 (14.5: the
  nearest start, squared 3D, to the previous end; equal score -> smaller entity key). FUN_1801bf440 = cache (+0xc0 blocked / +0xd0 free) ->
  FUN_1801bd390 (hk l.30965-31495): for OrderingType 1 it is FUN_1801be020 (hk): blocked when the pass's +0x340 list holds a pass not yet cut -
  empty for Raster -> **never blocked**. (4) all passes.
- **Post-processing**: FUN_1801b9490 (hk l.28004-28135) and FUN_1801c34d0 (hk l.35259-35420) only act when FUN_1801bf440 blocks (never for Raster);
  the +0x1107 stack is off. FUN_1801c48d0 (hk l.36157) then **drops a pass shorter than LinkRFMinPassLength** (+0x1068; LinkRFDiscardShortPass
  0x1b06 = `LinkRFMinPassLength > 0`, default 0.02 mm for plans other than 0x1461 / 0x141d / 0x144c, hl l.12335-12375; mckernel writes it only
  for kernel styles 2 and 9, mk l.89465 / 89793) and, with LinkRFDiscardSmallProfile (0x1b08, default `LinkRFMinProfileDiameter > 0`, 0), a small
  closed pass (FUN_18027b0b0 against the float +0x1074); kept passes go through FUN_1801c39d0 (the +0x388 remap, hk l.35425: identity when the
  map is empty) and FUN_1801c05c0 (the link, 5).
- **So for Raster**: one cluster; direction by group parity (FUN_1803a2750); first pass FUN_1801b9820 against the start point (14.5); every next
  pass = the not yet cut pass whose start (after the direction pass) is nearest in 3D to the previous end, ties to the smaller entity key;
  passes under 0.02 mm dropped. Measured on RESTPASSES op 19 (stored NCI): 24 rapid-separated chains that jump between distant line ranges
  (e.g. chain 3 runs x -1.30 -> 0.95 over many lines, chain 4 restarts at x 0.30) - a nearest-start order, not a line sweep.
- **Not ported, because three inputs are NOT DECODED**: (a) frag +0x70 of a Raster pass (the direction groups); (b) the start point: LinkStartX
  / Y = `*(K + 0x10) + 0x10 / + 0x18` (mk l.90180-90190; what object K + 0x10 is - not the plane origin, which mckernel adds as K+0x28 - is not
  followed; RoundUp default hl l.12680-12695 when not written); (c) the entity key order of the passes (the tie rule; [inferred] creation order).
  The +0x388 remap writer was not found (only the ctor init 1801b7a1e and 1801b8c64 touch +0x388 directly); a Raster op is assumed to leave it
  empty (it is filled through a pointer elsewhere if at all - NOT DECODED). On PARALLELSTEEP 5 / RESTPASSES 18 our line order is already
  Mastercam's (14.3), so the port would change nothing there; it matters for op 19 (whose rest passes are not ported) and parts with islands.

### 15.2 Blocked vertical leads in FUN_18019e010 (control flow DECODED; the fallback path NOT DECODED, so blocked leads keep SAFE)
- Flags (hk l.9233-9246): bVar32 = a previous pass (lead-out side), bVar33 = a next pass (lead-in side), bVar34 = (pass +0xb9 == 0); local_fdd =
  (kind 2 or 4). The lead-in end height local_ed8 = DAT_1809bf1a0 x param_19 x tan(a) + param_21 when bVar34 (hk l.9297-9301): param_19 = the
  horizontal lead-in radius, 0 for Raster (14.1), so the pass start Z.
- After building both arcs (hk l.10172-10213) the tests (hk l.10215-10236): bVar9 = lead-out blocked (FUN_18019a6a0 on the lead-out), bVar36 =
  lead-in blocked (skipped when local_fe0). Then (hk l.10238-10276):
  - lead-in blocked, first time: bVar6 = 1, back to LAB_18019ed4f. There (hk l.9733-9840) with kind not 2 / 4 and bVar34 the approach point
    param_18 becomes the pass start FUN_18027b070(pass), the lead-in end Z param_21, param_19 = 0 (hk l.9733-9745; else, with a horizontal lead, the ramp
    FUN_180176760 along the pass); for kinds 2 / 4 instead local_fe0 = 1 (hk l.9846) (the lead-in is then accepted untested). **For Raster the retry builds
    the identical arc** (same start, same Z, same tangent, param_19 already 0), so the test answers the same and the second blocked answer goes
    to LAB_1801a009a with bVar36 set.
  - lead-out blocked (bVar9), lead-in not: up to 6 rotations of the horizontal lead-out point around param_10 by |angle range| / 6 (only when a
    horizontal lead-out exists, local_res8; hk l.9645, 10249-10267), each re-running the whole build; for Raster the range is 0 (no horizontal lead,
    param_11 = 0) so the 6 tries are identical; after the 6th the lead-out top becomes E + R_out (t_xy, 1) (hk l.10268-10273) and bVar9 stays set.
- Emission of a blocked lead: lead-in (hk l.10445-10455): **FUN_18019db50**(linker, pass, param_8, path, param_24) instead of the arc; lead-out
  (hk l.10434-10440): **FUN_18019d5d0**(.., param_23). The straight vertical "leads" of RESTPASSES op 19 (e.g. NCI moves 2918-2920: rapid to
  0.45927, feed to 0.43427 = top + LinkRampHeightOffset 0.025, feed straight down 0.075 = R to the pass start) are this path's output.
- FUN_18019db50 (hk l.8771-8970): a connect FUN_1801d1e70 with the retract-style-0 arguments (+0x1158 / +0x1168 / +0x1120 / +0x110c /
  +0x1148 / +0x1150 / +0x116c / +0x1180 / +0x1188) -> FUN_1801db250 (x2 l.1204): FUN_1801dbf80 (x7: seeds the connect with the pass's own points
  from its start, Z + the lift argument, until the walked XY length reaches float connect +0x248 x R_in and at least 3 points; closed passes
  skip the length test) -> FUN_1801db570 (x7): FUN_1801db820 (x8: inserts two nodes, P + v R and P + v R + t R, v = the unit normal of the
  first segment in its vertical plane, t its horizontal direction backwards; node flags 5 / 2; for a node flagged 8 the horizontal
  perpendicular is used, which makes v vertical) -> FUN_1801d7450 (x8: drop the new node at the connect's layer FUN_1801cf7b0, FUN_1801cece0,
  FUN_1801d0240 (remove 0x80 nodes), FUN_1801e0ee0, then FUN_1801d9330 (x10: is the highest node of the section a lead node) -> done, or
  FUN_1801d87b0 + FUN_1801d76a0 (x10)) -> FUN_1801d91e0; then the LinkRampHeightOffset node (+0x1170 + the path height FUN_1801dd980 - top Z,
  added above when > 0) and the path emission FUN_180192510 / FUN_1801a47b0. **NOT DECODED**: FUN_1801cece0, FUN_1801e0ee0, FUN_1801d76a0 and
  FUN_1801d7b00's curl branch (FUN_180171b50, FUN_1801744a0, FUN_180173610 - the retract-style-0 curl profile of 5, also open), which node is
  P (the node index arithmetic of FUN_1801db250 l.1231-1246 with FUN_1801dbf80's count), and the float connect +0x248. In particular why the
  stored fallback of op 19 move 2918 is exactly vertical although the pass rises steeply after its start (v would tilt) is not explained.
  FUN_18019d5d0 / FUN_1801dab60 (x2 l.1041: the mirror for the lead-out, FUN_1801daf90 not dumped) likewise.
- **FUN_1801cea60** (x3 l.589-700, node removal; DECODED except its collision test): nothing when the path has 2 nodes or connect +0x88 != 0;
  walks (a, b, c) skipping lead nodes (flags 2 / 8) in pairs, and removes b (FUN_1801cfda0) when b is not flagged 0x40, the slot-0 test
  **FUN_1801e0d20** (hc l.3-30) allows it (b is not the protected node +0x1b0 when +0x1b8, not +0x1c0 when +0x1c8 - both set by FUN_1801db570 -
  and not the end nodes +0x18 / +0x20), a, b, c have the same layer (+0x3c), and the shortcut a -> c does not collide (**FUN_1801ce270, NOT
  DECODED**); else it moves on. So the lead test's (13.5) polyline loses every interior node whose shortcut is collision-free: porting that
  needs FUN_1801ce270.
- Port status: unchanged from 14.2 - a blocked lead keeps the SAFE link (labelled). For ops 5 / 18 the first approach is the flat-start tie
  (13.5), blocked on our values (N13-21); Mastercam's answer for a blocked Raster lead-in is FUN_18019db50's path, which is not decoded to the end.

### 15.3 Waterline exit (step 3): not reached
FUN_1801dab60 (x2 l.1041-1200) adds the LinkRampHeightOffset node above the lead-out top whenever +0x1170 > 0 and LinkRetractStyle != 0
(+0x1108) - so the plan's retract style is not what removes the feed-plane move on the Waterline exits (13.3); it is the lead-out fallback path,
the normal lead-out emission is hk l.10318-10420 (`if (local_900 < top + offset) FUN_1801194d0`). Why SWARF_METRIC op 3 / TUTORIAL 2 SWARF op 4
exits have no offset move stays NOT DECODED; the 1.0 mm of style 1 (12.1) was not reopened.

### 15.4 Measured (code unchanged; cam_cli raster, MCAM_3D_DEBUG=1, link_stats.py, oracle3d.py on this branch after the merge)
PARALLELSTEEP op 5: stay-down 477, first approach SAFE (lead blocked 1); retracts ref 2 / ours 2; ref->ours p50 / p95 / max 0.003 / 0.013 / 0.067,
ours->ref 0.012 / 0.034 / 0.855, cut length 2543 / 2553. RESTPASSES op 18: stay-down 372, the same SAFE first approach; retracts 2 / 2; ref->ours
0.006 / 0.010 / 0.095, ours->ref 0.011 / 0.049 / 0.535, length 978 / 985. Op 19: stay-down 132; retracts ref 25 / ours 2; ref->ours 0.042 / 0.082
/ 0.151, ours->ref 0.197 / 0.628 / 1.111, length 81 / 357 (rest passes not ported). Waterline not re-run (no code change).

### 15.5 Where M1 stops (ninth pass) - next, in order
1. Raster order: frag +0x70 of a Raster pass (FUN_1802ed680 / FUN_1803632f0 in raster/grid.c), the object at K + 0x10 (LinkStartX / Y), the
   entity key order; then port FUN_1803a2750's group parity + FUN_1801b9820's nearest-start chain (no graph, no prerequisites, one cluster) +
   LinkRFMinPassLength. Check op 5 / 18 stay identical and op 19's chain order against its 24 stored chains.
2. Blocked leads: FUN_1801cece0, FUN_1801e0ee0, FUN_1801d76a0, FUN_1801daf90, FUN_1801ce270 (also unlocks FUN_1801cea60), the float connect
   +0x248 (FUN_1801d1e70's constructor), the node indices of FUN_1801db250; check against op 19's vertical fallback leads (once its passes are ported).
3. Waterline exit offset move and the style-1 1.0 mm (15.3, 12.1).

## 16. Tenth pass (2026-11-13, M1j): mchmm.dll runs under tools/peloader - a Mastercam reference below the stored NCI

### 16.1 Verdict: FEASIBLE, done - the whole engine runs, not only single functions
The earlier note "mchmm is not loadable by tools/peloader" (audit_2026-11-09_highspeed_surface1.md) was about the loader of that day (no delay
imports, no MFC). Since then pe.c maps a DLL tree with the real VC 2013 runtime (tpif.c / mdef_arcfit.c). What mchmm.dll needs:
- **Imports**: only system DLLs + tbb.dll (objdump -p: MSVCR120, MSVCP120, mfc120u (the Unicode MFC, 668 ordinals), tbb, KERNEL32, USER32, GDI32,
  ADVAPI32, SHLWAPI, SHELL32, COMCTL32, COMDLG32, ole32, OLEAUT32, OPENGL32, GLU32, imagehlp, VERSION; one delay-import descriptor). **No
  Mastercam DLL** (no mccore / Mastercam.dll) and **no licence code**: no import, string or resource of mchmm names a licence / product check
  (the licence is mckernel's business before it calls mchmm). Nothing licence-related was answered, stubbed or patched (N13-22 / N13-23).
- **Mapping**: pe.c autoload with mchmm.dll + tbb.dll from ~/mastercam-re/files and msvcr120 / msvcp120 / mfc120u from
  <windows>/Windows/System32 (read only). The DllMain of each runs, mchmm's included (its CRT start-up and static constructors: the plan /
  parameter registries).
- **Stubs added** (tools/peloader, all W forms of existing A ones or plain OS answers): GetProfile*W, Path*W (SHLWAPI), the registry W functions
  (every key absent, as for mcce), GetTempPathW / GetTempFileNameW, GetFullPathNameW / GetCurrentDirectoryW / GetVolumeInformationW, FindFirstFileW
  family, CreateFileMappingW / MapViewOfFile (the entity store's swap file), FormatMessageW (FROM_STRING), wsprintfW, GetClassInfoW / RegisterClassW
  (MFC's property-page classes: mchmm builds a CNCGPropertyPage per plan, FUN_18051ab70, but shows none), GetActiveWindow (none), MessageBoxW
  (printed), GetEnvironmentVariable (none set), GetNativeSystemInfo (one processor), GlobalMemoryStatus (a fixed 16 GB machine; ours).
- **Threads** (tools/peloader/hmm_threads.c): mchmm runs every plan of a macro in an MFC worker thread (FUN_1800b62d0: AfxBeginThread(FUN_1800b63b0,
  job, CREATE_SUSPENDED) + ResumeThread) and the caller polls the job states (FUN_18002b9c0: WaitForSingleObject(event, 2000), progress message
  200) until the job is done; CEntityStore starts a lazy archiver (FUN_1800a26c0, writes queued entities to the swap file every 5 s) and a lazy
  pager (FUN_1800a2ab0) at Open (FUN_1800a14c0). peloader is single-threaded, so these are cooperative coroutines (ucontext) switched only in the
  wait functions, with events / semaphores / thread handles modelled and TLS / FLS per thread. TBB is told there is one processor, so it starts no
  workers. The scheduling order is ours; the results do not depend on it (the plan workers are joined before their output is read).
- **Driving it** (tools/peloader/hmm_link.c, `peloader mchmm.dll --hmm CASES [--out F]`): the exported CNCGDCInterface (ctor, Open, RunMacro,
  ReadTag) with our vtable in place of its two virtuals; RunMacro on a macro file we write, ReadTag on each output tag; the messages are those
  mckernel's CMCHstExtractTag decodes (callback FUN_1800654c4, vtable 18014d790): 100 error (plan id, code), 200 progress (plan id), 300 pass start,
  400 line (x y z, move type: 0 rapid, 1 plunge, 2 cut, 3 retract), 500 / 1000 arc, 600 / 700 / 800 / 1100 / 1200 / 2200 / 2300 / 2400 markers,
  900 point (a pass point of a passes plan). 1500 - 1900 come from tessellation tags (not decoded, not needed).
- **The macro language** (mckernel CMacroWriter, mk l.183366-184570; strings read from mckernel.dll): `entityList main(const entity&
  ekBoundaryPlan, const entityList& elSelectedPlans) { bool bDisplayDialogs = false; ... return [e: ...]; }` with statements
  `entity NAME = CreatePlan(ID, [s: "Input",...], [e: ent,...], [s: "Param",...], [s: "value expr",...], displayDialogs);` (FUN_1800ef300; values
  are expressions: a file name is `"\"path\""`, numbers `%19.9f` x the unit factor, FUN_1800edd8c) and `entity X = GetSinglet(elSelectedPlans, i);`
  (FUN_1800ef530). The Raster chain: Database Loader 0x1421 (FileName = the STL "tmphardmillraw.stl", FUN_18006eac0), Detect Top Surfaces 0x1423,
  Surface Tessellation 0x1424, Bounding Box 0x1425 (OffsetDistance), Raster 0x1404 ("ConstraintBoundaries", "Surfaces"), then the linking macro's
  Raster Linker 0x142b ("MachiningPasses"; mk l.92459-92466). A macro error is reported as message 100 with code 100 for the failing plan.
- **Cost**: a run of the small cases below takes ~2 s and < 300 MB.
First runs (scratch, a ball R 3 on synthetic pyramids, plan defaults except the tool / stepover / tolerance): the Raster plan's passes (tag 1: 800 /
900... / 1100 per pass) and the Raster Linker's toolpath (tag 2: 300, then 400 lines with move types) - Mastercam's own passes, order,
directions, retracts and plunges, below the stored NCI.

### 16.2 The oracle (tools/hmm_oracle.py, MCAM_HMM_DUMP)
`MCAM_HMM_DUMP=PREFIX cam_cli PART raster ... --moves ours.txt` writes the drive mesh (PREFIX.stl, mm) and the values the port feeds the plans
(PREFIX.par: the McHmm names of decomp/hmm_params.tsv; the name <-> value pairing is the port's reading of mckernel, cited in gen_raster, NOT the
decoded macro writer - FUN_180071314 / FUN_180075b48 write many more parameters, the rest keep the plans' defaults here). `tools/hmm_oracle.py
PREFIX` writes the macro, runs peloader and writes PREFIX.moves (op units) for tools/oracle3d.py / tools/tests/link_stats.py.

### 16.3 First measurement (PARALLELSTEEP.mcam op 5, the port's mesh, the port's partial parameter set)
`MCAM_HMM_DUMP=P cam_cli PARALLELSTEEP.mcam raster "" --dia 0.25 --set file_op_id=5,corner_r=0.125`, `hmm_oracle.py P` (mm, x 25.4): the DLL
runs the real part (317914-line STL, 1 run, ~1 min) and returns 31638 lines + 970 arcs. Against the stored NCI (oracle3d.py / link_stats.py):
DLL ref->ours p50 0.031 p95 0.495 max 0.702, ours->ref p50 0.006 p95 0.057 max 0.110, cut length 2543 / 1562 (arcs not counted), retracts
2 / 597, plunges at feed 2 / 1594; the port (unchanged): ref->ours p95 0.013, retracts 2 / 2. So with only the 18 parameters of PREFIX.par the
DLL does NOT reproduce Mastercam's op: its passes cover less (ref->ours p95 0.5 in) and it retracts between most passes (no stay-down). The
missing inputs are the rest of what mckernel writes: FUN_180071314 (mk l.87250-88200: ContactAngleHi / Lo, PosContactOnly, PassExtension,
RasterStepoverMode / Cutoff, the tool body: ToolCutterDiameter / ShankDiameter / FluteLength / BodyLength / TaperAngle, ...) and FUN_180075b48
(mk l.89100-90200: LinkSafeZ, LinkStartX / Y, LinkSimpleOrder, LinkOrderingType + the LinkOR* set, LinkTrimmingStyle, LinkSurfaceRamp* /
SurfaceRamp*, MaxRampAngle, LinkClearSurfaceBy, LinkCutterNarrowByPercent, LinkRFMinPassLength, ...). Until those are written as mckernel
writes them, the DLL output is a reference for the engine's behaviour on given parameters, not for the stored op.

### 16.4 Measured with the DLL (synthetic, plan defaults; scratch macros, not committed)
Two pyramids side by side (x -45..-15 and 15..45, y -15..15), ball R 3, MachStepover 4, RasterAngle default, Raster + Raster Linker:
- Tag 1 (the passes plan) lists 14 passes line by line (y -14, -10, ..., 14), a line split where it leaves the drive: so a Raster line can hold
  several passes, each its own frag.
- **Direction (FUN_1803a2750)**: the passes of one raster line are reversed together, every second LINE (y -10, -2, 6, 14 reversed; lines that
  hold one pass or two count the same), the reversed line's passes also in reverse order - i.e. the direction groups are the raster lines here
  (consistent with frag +0x70 = one CTessToolpath per line, 15.1 (a); measured, not decoded).
- **Order**: not the line order; e.g. line -14 right pass, line -10 right pass, line -14 left pass, ... the left pyramid last. LinkStartX / Y
  change the first pass: (0, 0) -> the line -14 pass starting at x 18.2; (50, 20) -> the line -14 pass starting at x 45.6 (not the nearer line
  14 pass); (-50, 0) -> the left pyramid's line -2 pass. So the first-pass choice is not the plain nearest start to (LinkStartX, LinkStartY)
  in 2D or 3D (checked by hand on the three cases) - FUN_1801b9820's first-pass score stays NOT DECODED; the DLL can now answer such
  questions case by case.
- Retracts / plunges with the plan defaults (LinkRetractStyle default, no leads): retract (type 3) 1 above the pass end, rapid up to a height
  that varies per link (5.8 ... 12.1), rapid across, rapid down to 1 above the next start, plunge (type 1); these heights were not checked against 10.2 / 13.2.

### 16.5 Where M1 stops (tenth pass) - next, in order
1. The oracle's macro: write the Raster macro exactly as mckernel does (port FUN_180071314 / FUN_180075b48 / FUN_18006f08c ... as a macro
   writer, cited), until the DLL run reproduces PARALLELSTEEP op 5's stored NCI (retracts 2, stay-down links); then the same for RESTPASSES 18 /
   19 and the Waterline ops (SWARF_METRIC 3 / 4). From then on every linker question of 15.5 (pass order, blocked leads, the "exactly vertical"
   lead of op 19 move 2918, the Waterline exit) can be answered by the DLL on our own inputs.
2. A pass-level unit oracle: feed the DLL's tag-1 passes to the port's linker (cam_cli reading a passes file) and compare with the DLL's tag 2.
3. 15.5 1-3 unchanged (decode).
Code: the port is unchanged (the dump is env-gated); oracle numbers of 15.4 unchanged (PARALLELSTEEP op 5: retracts 2 / 2, ref->ours p95 0.013,
ours->ref p95 0.034).

## 17. Eleventh pass (2026-11-13, M1k): mckernel's macro writer for the DLL oracle - the DLL reproduces the stored Raster ops

### 17.1 The writer (tools/hmm_macro.py; tools/hmm_oracle.py uses it when cam_cli dumped PREFIX.rec)
`MCAM_HMM_DUMP=PREFIX cam_cli PART raster|waterline ...` now also writes PREFIX.rec (the stored op record, without --rec patches), PREFIX.job
(units, the mesh's Z range) and PREFIX.tc (the containment as tmptc.tmp holds it, c3_tcb_offset). hmm_macro.py writes the macro as mckernel does:
- **K -> record**: mccore FUN_1809232b0 (ghidra_core, decompiled this pass) fills the kernel's TPHSTToolPath: FUN_180923f60 (d4 l.62-255; table KREC,
  record = op + 0x68, K = TP + 0x60), FUN_180923d30 (d4 l.603: K+0x10..0x48 = record K + 0x160, except K+0x40 = 0x2e0 and K+0x48 (op 132's
  "minus the corner radius" flag) = record 0x2f8), K+0x45c = FUN_1809239b0(record 0x16bc) (d5 l.1), K+0x463 rest = UI style in {0-3,5,6,9,10,15}
  and record 0x16c5 (FUN_180923880 d5 l.35). The tool K+0x70 (KcHstTool) comes from SetHstTool in FUN_180924880 (d4 l.380-560): +8 = record
  0x350 (D), +0x10 = 0x358 (corner radius), +0x48 = 0x378; +0x18 / +0x28 / +0x38 / +0x40 / +0x58..+0x68 come from the tool system's geometry
  (Cnc::Tool vtable +0x98) - **NOT DECODED**, so ToolBodyDefinition / ToolBodyLength / ToolFluteLength are not written (plan defaults).
  For a tool without taper FUN_1800969b4 (mk) is the diameter at any height: ToolShankDiameter = D, the Bounding Box offset width FUN_18006fee0 = D.
- **Job** (mckernel FUN_18006dd68 mk l.84700, FUN_18007bb48 mk l.91572): factor job +0x80 = 25.4 inch / 1 mm; job +0xa0 = K+0x220 (else 0.001 in /
  0.025 mm) x **0.5** (StepMachTolerance, the tessellation Tolerance, MachTrimZTolerance); job +0x38 / +0x40 = hstJobIdDat +0x28 / +0x30 (the
  tessellation's vertex Z range; here our mesh) with FUN_18007bb48's floor stock / tolerance / corner radius adjustments; Use Z depths replaces them.
- **LinkStartX / Y** (open in 15.1 (b)): TP +0x30 / +0x38 = hstJobIdDat +0x10 / +0x18 (mccore FUN_1809251c0 copies hstJobIdDat into TP +0x20..0x5f;
  run_mchardmill_macro 180597240), set by mcmsurf process_3rd_party_algorithm FUN_1800199d0 (ms l.18086-18103, l.18265-18268): the op's start point
  in the view (world_to_view) when msurf_op_has_hmmpoint / msurf_op_has_start_pt, **else -9999, -9999** (0xc0c3878000000000). DECODED.
- Statements: FUN_18007d7c8 (Loader 0x1421, Detect Top 0x1423, Tessellation 0x1424 with Tolerance, Bounding Box 0x1425 with OffsetDistance, the
  containment as Create2DBoundary / Create2DBoundaryCollectionEx (FUN_180081f6c / FUN_1800ee404 / FUN_1800ef000, list syntax `[r: ...]`, `[i: ... ,0]`,
  `[e: ...]` from the DLL's strings) and the Boundaries Plan 0x1427 ekPlanTcb3, the passes plan with FUN_180071314's parameters (Raster style 4 /
  Waterline style 1), then the linking plan (0x142b / 0x141c) with FUN_180075b48's parameters. The unnamed Ordinal_310 calls of the decompile are
  LinkDirectionStyle / SurfaceRampStyle / LinkWaterlineDirectionStyle (string operands read from the disassembly of 180075b48-180078e44).
- Refused (not written): rest material / stock model, check surfaces, Tool contact point, Down / Up Mill, perpendicular passes, K+0x472, an
  approximate start point. SystemContext +0x10 (the small-value threshold of several comparisons) is NOT DECODED, taken as 0 (only compared
  against values far from 0 in the cases run). The arcs of the DLL (messages 500 / 1000) are written as 16 chords by hmm_oracle.py (ours).
- **tools/tests/seg_dist.py** (new): every feed vertex of one path against the nearest feed segment of the other, both ways. oracle3d.py samples
  at 0.1 and reads two equal paths with different vertices as 0.02 apart on the steep walls (measured on op 5); seg_dist does not.

### 17.2 Does the DLL reproduce the stored ops? (the DLL on this port's mesh, seg_dist / link_stats against the stored NCI)
| op | vertex-to-path ref->dll / dll->ref (p50 / p95 / max) | retracts ref / dll | notes |
|---|---|---|---|
| PARALLELSTEEP op 5 (Raster, inch) | 0.0000 / 0.0003 / 0.0044, 0.0000 / 0.0002 / 0.0042 | 2 / 2 (plunges 2 / 2) | **reproduced**: first approach (rapid 2.0 -> 0.1, feed 0.075, lead arc R 0.075), 477 stay-down links, exit; 0 vertices > 0.005 |
| RESTPASSES op 18 (Raster, inch, D 0.25) | 0.0002 / 0.0005 / 0.0019, 0.0001 / 0.0005 / 0.0017 | 2 / 2 | **reproduced** (needs --dia 0.25 for the port's own run; cam_cli's default tool is 0.0625) |
| TUTORIAL 2 SWARF op 4 (Waterline, inch, contact 50-90) | 0.0025 / 0.0133 / 0.112, 0.0025 / 0.0142 / 0.118 | 42 / 42 (rapid XY 51.2 / 50.7, plunges 69 / 108) | close; per-level lengths within 0.1-3.6 in (top levels longer) |
| TUTORIAL 2 SWARF_METRIC op 3 (Waterline, mm) | 0.049 / 1.90 / 7.15, 0.045 / 2.81 / 7.16 | 39 / 2 | **not reproduced**: loops +1.8 mm per level (0.3 mm farther out), levels drift (+0.003 / level, 40.828 .. 8.486 vs 40.826 .. 8.489, ref 1.2 to 14.43 then ~1.19), the 4 lowest DLL levels are full loops (378 mm) where the ref cuts 239 / 102 / 35 / 10 mm. The port's own run has the same full lowest loops (46 % extra floor), so the input (our drive mesh / containment) is the first suspect; not followed further |
| RESTPASSES op 19 (rest passes) | not run: rest material (K+0x463) macro not written | | |
What still differs on 5 / 18 and why: the DLL's input mesh is this port's tessellation (mckernel's tessellation differs: residual 0.002-0.004 in),
and the tool body (ToolBodyDefinition / Flute / Body length) keeps the plan defaults (no shank collision on these parts, else the paths would differ).

### 17.3 The port's linker against the DLL (step 2, first results; no port change)
- **PARALLELSTEEP op 5**: port vs DLL vertex-to-path: DLL->port max 0.062 (22 vertices > 0.01, all the first approach), port->DLL max 0.015 (the
  port's passes end 0.001 below the floor at the containment edges). The links agree except the first approach: the DLL accepts the lead-in arc,
  the port's lead test (N13-21, strict) says blocked. MCAM_LK_DEBUG shows why: the port's first pass starts at (-0.50914, 0.48666, -0.00033), 0.013
  farther out than the DLL's (-0.49999, 0.49581, 0) and 0.0003 below the floor, so the lead's last node lies exactly at R (d2 = r2 = 0.005625).
  The difference is the passes plan's pass end (trim at the containment, the drop's Z), not the lead test - the linker code agrees with the DLL here.
- **RESTPASSES op 18**: port vs DLL p50 0.005 / p95 0.0073 both ways (a uniform 0.005 offset of the passes: passes plan, not linker); the final exit
  differs: the DLL ends at (1.9039, -2.3856), the port at (1.9089, -0.0331) - the last raster line's passes are taken in another order / direction
  (FUN_1803a2750 group parity + FUN_1801b9820, 15.1). Not ported: needs the pass-level feed (below).
- Not done (budget): a cam_cli mode that takes the DLL's tag-1 passes as input to the port's linker (link-by-link comparison), the order /
  blocked-lead / Waterline-exit pieces of 15.5. The DLL now answers them for any record the writer handles.

### 17.4 TBB processor count (harness choice)
The harness reports one processor (GetNativeSystemInfo) and a fixed 16 GB (GlobalMemoryStatus); TBB then runs every parallel_for / reduce serially
in index order. The one parallel loop decoded in the linker (FUN_1801ba430 -> FUN_1801ba5e0, 15.1) writes each pass's own same-level list, so its
result does not depend on the thread count. A parallel_reduce that combines floating-point partial results (sums, minima with ties broken by
arrival) could give a different last bit or a different tie winner with more processors; none was found in the linker functions read so far, but
the passes plans' TBB bodies were not audited. The DLL output with one processor matched the stored NCI of ops 5 / 18 to 0.0044 in.

### 17.5 Where M1 stops (eleventh pass) - next, in order
1. Waterline op 3: why the DLL's loops sit 0.3 mm out and the lowest levels stay full (mesh / containment input; compare tmptc.tmp, the drive set).
2. Pass-level feed: cam_cli reads the DLL's tag-1 passes (return [e: ekPlan4, ekPlan5]) into gen_raster's linker; compare link by link; then the
   Raster order / direction (op 18's last line), blocked leads (op 19 once the rest macro is written), Waterline exits.
3. The rest-material macro (op 19: tmphmmstk / Detect Theoretical Rest Areas, FUN_18007d7c8 K+0x463 branches) and the tool body (Cnc::Tool +0x98).
4. The passes-plan differences the DLL exposed (op 5's pass ends below the floor at the containment, op 18's 0.005 offset) - not linker, but they
   decide the port's lead test.
Port code: unchanged (only the env-gated dump); port oracle numbers as 15.4.

## 18. Twelfth pass (2026-11-13, M1l): the Raster passes plan matches the DLL; the DLL's passes through the port's linker

### 18.1 The pass-level harness (step 1)
- `tools/hmm_oracle.py PREFIX --passes`: the macro returns `[e: ekPlan4, ekPlan5]`, so the DLL also reports the passes plan's own output
  (tag 1: per pass 800, 900 points / 1000 arcs, 1100) besides the linked toolpath (the last tag). Written as PREFIX.passes ("PASS n" + n lines
  "x y z", op units; arcs as 16 chords); PREFIX.moves now holds the last tag only (before, a second tag would have been appended).
- `MCAM_HMM_PASSES=PREFIX.passes cam_cli PART raster ...`: gen_raster drops its own passes and runs its linker (direction pass, order, links)
  on the DLL's passes; the frag key is the raster line (consecutive passes with the same d = p . u2). `MCAM_HMM_DUMP=PREFIX` also writes the
  port's own passes before the linker (PREFIX.ourpasses) and, for ops with checks, the check mesh (PREFIX.chk.stl).
- `tools/tests/pass_cmp.py A.passes B.passes`: pass lists matched by their end pairs (counts, start / end distances, direction).
- `tools/tests/link_cmp.py PASSES A.moves B.moves [--tol T] [--eq E]`: cuts both toolpaths at the pass ends ([approach, pass, link, ...,
  exit]) and compares the order / directions and every link (kind S = feed only, R = with rapids; vertex-to-path distance both ways).

### 18.2 The two Raster passes-plan differences (step 2), found with the DLL and ported
- **RESTPASSES op 18, the uniform 0.005 in**: the raster lines are placed on the range of the **ConstraintBoundaries** (FUN_18037ca90 measures
  the plan's +0x118 +0x438 list): without a containment that is the Bounding Box plan's box (offset by OffsetDistance), not the drive's XY
  range the port measured. DLL line x0 = -1.80611 in = -1.81332 - 0.127 + frac(3.97843 / 0.01) x 0.01 / 2 + 13 x 0.01 (box), the drive range gave
  -1.81110; 372 lines in both now (the port had 373). This also fixes op 18's last line (its exit at (1.9039, -2.3856), as the DLL).
- **PARALLELSTEEP op 5, the first pass 0.013 in farther out and 0.0003 in below the floor**: FUN_180361bd0's second loop (raster/grid.c l.2344-2440)
  bisects between a kept position and its removed neighbour (the drop FUN_180222920 at the XY midpoint) until the XY gap is <= StepMinHorizontalChange
  (+0xcb8, 0.003 mm), the side decided by the tight test FUN_1802f3a80(.., 0) = [PosZLo, PosZHi] (+0x6c0 / +0x6c8; set at mchmm 1802f2e61-1802f2eba,
  +0x6d0 / +0x6d8 = the window widened by MachTrimZTolerance 0x1b39); the kept midpoints join the pass. The DLL's ends lie on the tight window:
  measured, MachTrimZTolerance 0.0127 -> 0.3 mm in the macro leaves op 5's pass ends where they are, so a stretch's end positions that are only
  in the widened window are not kept (the widened window keeps interior positions, as before). Ported in gen_raster (raster_bisect); the
  containment trimming stays at the samples (its rule is NOT DETERMINED) - the bisection runs only where the removed neighbour is inside it.
- Result (pass_cmp, the DLL's tag 1 vs the port's passes): op 5 478 / 478 passes, same directions, starts / ends within 0.00008 in (p95 0.00006);
  op 18 372 / 372, within 0.00007 in. Before: op 18 every line 0.005 off and one line more, op 5 the ends up to 0.013 out.

### 18.3 The linker on the DLL's passes (link by link, step 3)
- Direction pass FUN_1803a2750 ported on the frag key (groups of consecutive passes with the same raster line; group g reversed when Other Way
  xor (Zigzag and g odd), a reversed group's passes reversed and in reverse order): the old `rev = zig ? (i & 1) : other` counted empty lines
  and ignored Other Way under Zigzag.
- LinkRFMinPassLength (FUN_1801c48d0 hk l.36157, called hk l.27974): a pass shorter than 0.02 mm (default; mckernel writes it for kernel styles
  2 / 9 only) is taken but not linked. Length = FUN_180279600 ([inferred] the 3D length).
- LinkSafeZ / LinkZClearance = the clearance minus the corner radius when record 0x2f8 is 0 (FUN_180075b48, tools/hmm_macro.py link_params);
  the port used the clearance alone (same value on every stored Raster op checked: 0x2f8 != 0).
- The lead direction is computed as the DLL does: T.xy x (1 / sqrt(Tx^2 + Ty^2)) (hk l.10137-10142; it was T.xy / hypot).
- link_cmp, the port's linker on the DLL's passes vs the DLL's linker: **PARALLELSTEEP op 5: order and directions equal; 477 / 477 stay-down links
  and the exit equal (<= 0.0001 in, the exit's arc chords); RESTPASSES op 18: equal, 371 / 371 stay-down links and the exit.** The one
  difference is the first approach: the DLL keeps the lead-in arc, the port's strict lead test (N13-21) finds it blocked by 2 ulps
  (MCAM_LK_DEBUG: r2 = 0.0056250000000000015, the last segment's d2 = 0.0056249999999999998 for a flat start where both are R^2) and keeps SAFE.
  Computing the lead in the DLL's operation order did not change the tie; the DLL works in mm (the macro's x 25.4), the port in op units.
- With the port's own passes (link_cmp --tol 2e-4 --eq 5e-4): op 5 478 / 478 links equal except the first approach; op 18 the same.

### 18.4 Pass order (FragOrderer): FUN_1801b9820 decoded, the prerequisites measured; PORTED (rp_prereq / rp_next)
FUN_1801b9820 (hk l.28139-28370) read again: with no previous pass (+0xf08) the candidates are scored by FUN_18027a4f0 (squared 3D distance from
the start point (LinkStartX, LinkStartY, LinkSafeZ) through FUN_1803a3ae0 to the pass start); N = the smallest; the walk over the next ones in
score order stops at the first whose END is farther than LinkMaxStayOnSurface (XY) from N's start (or that is closed), the last one before
that becomes the choice. With +0xf08 set, FUN_1801b9cc0 / FUN_1801b9d70: the start nearest the previous end. A dropped pass (FUN_1801c48d0)
is taken (+0x1098 counts it) but not linked (FUN_1801b90d0 hk l.27974-27983).
- **The DLL starts PARALLELSTEEP op 5 and RESTPASSES op 18 with the plan's first pass for every start point tried** (LinkStartX / Y = -9999 in,
  0, +-500 mm, +-2540 mm), while the reading "no graph edges, so FUN_1801bf440 never blocks" (15.1) predicts the start nearest the start point
  (op 5: pass 310 at (-0.478, -8.250) for -9999). So FUN_1801bf440 / FUN_1801be020 do block on a Raster op: the +0x340 lists are not empty.
- **Measured on synthetic parts** (op 18's macro with an STL of two pyramids, RasterAngle 180, MachStepover 4 mm, LinkMaxStayOnSurface 5 mm;
  scratch, not committed): (a) two full pyramids side by side, start (-9999, -9999) / (0, 0) / (50, -20) mm: one pyramid's passes line by
  line from the first line, then the other's from its first line - the start point picks the pyramid, never a middle line; (b) a small
  pyramid crossing only the first two lines on the left and a full one on the right: start (50, 20) -> the right pyramid's first pass (on
  line 1, while line 0 holds only the left pass) and down the right pyramid, then the left; start (-50, 20) -> line 0 left, line 1 left,
  line 2 left, then line 1 right, ... So **a pass is blocked while a pass of the previous raster line whose range along the line overlaps
  its own is not taken** ((b) rules out "any pass of the previous line"). With that filter, FUN_1801b9820's two rules above reproduce all
  five DLL orders and ops 5 / 18 (a Python model on the DLL's tag-1 passes, then the port). The +0x340 writer stays NOT DECODED (the
  prerequisites are measured); the overlap margin (LinkORAdjacentLevelOverlap 0x1b13, LimitRangeAdjacentLevel true) is NOT DECODED: closed
  intervals in the port; "previous line" = the previous frag key holding passes in the plan's order ([inferred]: an empty line has no frag).

### 18.5 Waterline SWARF_METRIC op 3 input (step 3)
- **Its 43 check surfaces were missing from the DLL macro.** mckernel trims Waterline passes with Edit Passes to Surfaces 0x141f when the op has
  checks (mk l.93590-93603): check loader FUN_18007ca68 (mk l.92160-92180: 0x1421 FileName only, 0x1423, 0x1424 ekPlanC2 with Tolerance),
  FUN_180070e8c's parameters (mk l.86832-86889), ekPlan4a = 0x141f ("MachiningPassesIn", "ProtectedSurfaces" from ekPlan4b, ekPlanC2), then
  OutputID = 0x1cb2 and the Convert To Primary Output Plan 0x1458 ekPlan4 ("SourcePlan", mk l.94055-94062). Now written by tools/hmm_macro.py
  (check_plans / trim_plans, Raster and Waterline) on the port's check mesh (mckernel's loader reads tmpmn5.emm, FUN_18006ea44; here an STL).
- DLL vs stored NCI (seg_dist, mm): before (no checks) ref->dll p50 0.049 p95 1.90 max 7.15, dll->ref 0.045 / 2.81 / 7.16, retracts 39 / 2;
  with the checks: ref->dll 0.049 / 1.06 / 7.15, dll->ref 0.040 / 0.76 / 3.38, retracts 39 / 26, rapid XY 481 / 439, plunges at feed 44 / 71.
- **The "0.3 mm out" is not an XY offset**: the top level of the DLL (Z 40.8284) lies within 0.0007-0.0038 mm (median per 30 deg sector) of the
  stored one (Z 40.8256); its extra length (386.6 / 384.8 mm) is not in the loop. What differs is the **level Z**: the DLL's levels drift up
  from the stored 1.2 mm grid (40.8284, 39.6292, 38.4292, 37.2376, ..., 14.5252 vs 40.8256 - 1.2 k down to 14.4257: +0.003 at the top, +0.1 at
  14.5), i.e. the DLL pushes some levels up on this mesh (the StepDownInflexion* / RadPushUp group, 0x1bbd-0x1bc0, NOT DECODED) where Mastercam
  on its own tessellation did not; the port's own levels are the stored ones (40.8256 - 1.2 k, then 13.2377 / 12.0497 / 10.8617 / ...). On the
  sloped walls the Z drift moves the loops in XY (the p50 0.05 mm). Suspect: the DLL's input mesh (ours) - not followed further.
- The lowest levels: the stored op cuts 238.6 / 101.6 / 10.0 mm at 12.05 / 10.87 / 8.49; the DLL with the checks (12.37 378.9, 11.40 378.6, 10.44
  152.6, 9.47 120.1, 8.49 62.1) and the port (12.05 379.8, 10.86 222.0, 9.67 113.9, 8.49 62.6) trim less. The record says check method 0
  (0x172c = 0, surface_selection.md 8.3 / oracle_results "waterline"), so the stored bottom levels are not a method-1 (0x141f) reference; the
  DLL and the port agree with each other better than with the NCI there.
- Waterline links, the exit and the style-1 retract +1.0 mm (15.5 / 12.1): not reached in this pass.

### 18.6 Measured (this branch; cam_cli raster, seg_dist.py vertex-to-path, inch)
| op | port vs stored NCI ref->ours / ours->ref (p50 / p95 / max) | port vs DLL | retracts ref / port |
|---|---|---|---|
| PARALLELSTEEP 5, before | 0.0001 / 0.0031 / 0.0621, 0.0000 / 0.0004 / 0.0153 | 0.0001 / 0.0031 / 0.0621, 0.0000 / 0.0003 / 0.0153 | 2 / 2 |
| PARALLELSTEEP 5, after | 0.0001 / 0.0006 / 0.0750, 0.0000 / 0.0003 / 0.0042 | 0.0001 / 0.0005 / 0.0750, 0.0000 / 0.0002 / 0.0031 | 2 / 2 |
| RESTPASSES 18, before | 0.0050 / 0.0070 / 0.1164, 0.0050 / 0.0073 / 0.1278 | 0.0050 / 0.0073 / 0.1164, 0.0050 / 0.0073 / 0.1278 | 2 / 2 |
| RESTPASSES 18, after | 0.0002 / 0.0009 / 0.0750, 0.0000 / 0.0004 / 0.0017 | 0.0000 / 0.0010 / 0.0750, 0.0000 / 0.0002 / 0.0012 | 2 / 2 |
The max 0.075 = the first approach (SAFE instead of the lead-in arc of R 0.075, 18.3). finish_oracle.sh RA (oracle3d, sampled): ref->ours p50 0.003
-> 0.002, p95 0.013 / 0.013, cut length 2553 -> 2542 (ref 2543); ours->ref p95 0.034 / 0.035. RESTPASSES op 19 (rest passes not ported, the
plain Raster): unchanged (retracts 25 / 2, ref->ours p95 0.0141, length 358 -> 357).

Regression (this branch vs its base 96cc64a = master 977df9e + the env-gated M1k branch, both built here): fileop 0 / 205 differ; op_compare
47 -> 54 / 635, the 7 new = the Raster scenes gen_raster.txt, s4_check_trim(.txt / .nc), s4_check_trim_stock(.txt / .nc), s4_pick_check.txt,
s4_pick_drive.txt - **re-blessed**: the .txt change only in the point counts (e.g. s4_pick_drive 20911 -> 26963), the NC in the toolpath. The
causes are the three ports above, each checked with the DLL: lines over the Bounding Box plan's box (on a flat loft that adds the lines
within the tool's reach beyond the drive's edges, where the ball rolls over the edge down to PosZLo = floor - corner radius - measured with
mchmm.dll on a flat 100 x 60 mm square, op 18's macro with PosZLo -R: 17 passes on lines y = -2 .. 62, the outer two at z -3.14 .. -0.71, every
pass from x -3.17 to 103.17, i.e. over the edges down to the window); the bisected pass ends (the roll-off to PosZLo); the order (s4_check_trim:
the left pieces of all lines, then the right ones, as the pyramids of 18.4, instead of crossing the check strip on every line).
tools/tests/checktrim_test.sh's stop check now reads the straight floor cuts (the links' lead arcs / turns pass the trimmed ends): 35 / 65 and
34 / 66 as before, on both the old and the new NC.

### 18.7 Where M1 stops (twelfth pass) - next, in order
1. Decode the +0x340 writer (the prerequisites are measured, 18.4) and LinkORAdjacentLevelOverlap's margin; check the order against a
   stored op with several passes per line (op 19 once the rest macro is written).
2. The first-approach tie (N13-21): compute the linker in mm as the DLL does, or ask the user (the arc appears in the DLL and the NCI).
3. Waterline linker 0x141c on the DLL's passes (MCAM_HMM_PASSES for gen_waterline is not written yet): links, exit, style-1 retract.
4. The level push (StepDownInflexion*) and the rest-material macro (op 19), the tool body.

## 19. Thirteenth pass (2026-11-14, M1m): the pass-order prerequisites DECODED, the linker in mm (N13-27), the Raster leads are not FUN_18019a6a0's

New tool: `HMM_TRACE="ADDR[:a][:k][:v],..." peloader mchmm.dll --hmm ...` (tools/peloader/hmm_link.c) - a call tracer for reading the DLL's
control flow: an int3 at each ADDR (restored for one single step, nothing answered or skipped), "T> ADDR ret rcx rdx r8 r9" on entry ([rdx] with
:k, three doubles at rdx / r8 / r9 with :v), "T< ADDR rax xmm0" on the matching return (the EntityList in argument a with :a). The DLL's messages
are now printed with %.17g and hmm_oracle.py --passes also writes PREFIX.mmpasses (the DLL's tag-1 passes in mm, unrounded) for
`MCAM_HMM_PASSES=PREFIX.mmpasses`. New dumps (scratch, not committed): FUN_180283c20, 180183b80, 180184200, 1801ca490, 1801c9bd0, 1801ca390,
1801be7b0, 18027a8d0, 18027b290, 18027a7a0, 180062650, 18027d4e0, 18027d940, 180013ab0 (DecompAt on ghidra_hmm).

### 19.1 The prerequisites (+0x340): DECODED (replaces the measured rule of 18.4)
- **Where the graph's edges come from.** The grid plan adds every raster line's CTessToolpath with FUN_1802ed200 (raster/tb.c l.1086, called
  for each element of the line vector, raster/grid.c l.1578): node index i = its place in the list, placeholder pairs (i, -1) and (-1, i) in
  CFragAdjacency +0x20. Right after, the plan's virtual +0x20 (grid.c l.1581) is **FUN_180013ab0** for Raster (traced: 495 calls of
  FUN_1802ed2c0 for 496 lines, all from 180013ad6): for i > 0 FUN_1802ed2c0(plan, &key[i-1], &key[i]) (proj/h2.c l.2202) finds the pair
  (i, -1) and completes it to (i, i-1) - **one edge i -> i-1 per pair of consecutive lines**. (18.4 / 14.5 read only FUN_1802ed200.)
- The linker's graph (FUN_180182e20, x2 l.1600-1850, the orderer's +0x1028): a pair (a, b) with both >= 0 puts b in a's successors (+0x40 /
  int map +0x58) and a in b's predecessors (+0x10 / +0x28); the int nodes map to the toolpath keys of the passes collection's list (+0xc8, a
  node without passes gets key 0). FUN_180183b80 (successors) / FUN_180184200 (predecessors) return the neighbours' keys, walking through
  key-0 nodes (BFS over the int graph) - so **the neighbour is the nearest line that holds passes**.
- **FUN_1801ba430 / FUN_1801ba5e0** (hk l.28708-28870, TBB loop): for each pass P, list +0x110 = FUN_1801ba970(P, dir) (OrderingType 1,
  LinkORCustomAdjacentLevelOverlap +0x10d8 = 0 < 1; +0x10d4 = 0 so FUN_1801ba810 is not run): FUN_1801c3cb0 -> FUN_180184200 when dir != 0
  (dir = +0xf40 = LinkHeadNorthwards +0x10d1 (0x1a95), default `!LinkReverseOrder` (FUN_1803acd10 hl l.13590; mckernel writes it only for
  Waterline, mk l.90038); not flipped since LinkORLocalNorth 0x1b16 = `LinkDMMaintainDirection && (LinkDirectionStyle == 2 || 3)` is false for
  Zigzag / one way, hl l.13690-13704) = the passes of line i+1, filtered by FUN_1801c9bd0 (LinkORLimitRangeAdjacentLevel 0x1b12 true) with the
  overlap test. **FUN_1801c98a0** (x7 l.865-960) inverts +0x110 into **+0x340**: the prerequisites of a pass Q on line i+1 = the passes P of line i
  that overlap Q. FUN_1801bf440 -> FUN_1801bd390 -> **FUN_1801be020** (OrderingType 1, WholeLevelBeforeNext false): Q is blocked while one of
  them is not taken (FUN_1801ca390: minus the taken set +0x80). Traced on PARALLELSTEEP op 5: FUN_180184200 478 calls, each one neighbour line,
  FUN_1801c2940 477 calls.
- **The overlap test** FUN_1801c2940 (no caches: param_7 / 8 = 0) -> FUN_1801c2300 (hk l.34418) -> FUN_18027a8d0: A = P, B = Q, swapped when
  B has fewer points (FUN_18027b290 counts the frag iterator's points); d = **LinkORAdjacentLevelOverlap 0x1b13 = `EvaluateR(MachiningPasses,
  MachStepover) * 1.5`** (hl l.13650-13665; enabled when LimitRangeAdjacentLevel && !WholeLevelBeforeNext); d^2 = 0 -> false; false when the
  squared gap of the XY boxes (FUN_180062650) is > d^2; else from A's start (frag +0x88, +0xb0 when reversed): q = nearest point of B, a =
  nearest of A to q, b = nearest of B to a (FUN_18027a7a0), true when |a - b|^2 (XY: LinkORMeasureInUV 0x1b14 true) <= d^2; then the same from
  A's end; else false (LinkORAccurateClosestPair 0x1b15 false -> param_6 true -> no FUN_18027aaa0). The nearest point FUN_18027d4e0 /
  FUN_18027d940 (UV): per segment the interior foot (0 < t < 1 of the XY projection; the point interpolated in 3D), then the segment's end
  vertex, the first vertex once; strict < keeps the first of equal distances. mckernel writes none of these for Raster (style 4; the
  LinkOR* block at mk l.89660-89730 is style 9's).
- **Checked with the DLL** (synthetic, op 18's macro on two boxes, scratch boxg.py / ordsim2.py): box L x[-40, -G/2] y[0, 6], box R x[G/2, 40]
  y[0, 40], stepover 4 mm; the DLL's linked order vs the decoded rule vs the old "closed intervals" rule: G 5 (pass gap 0.38 mm) starts (60, -5) /
  (-60, -5) / (-60, 50), G 9 (gap 4.38 mm < 1.118 x 4) starts (-60, -5) / (-60, 50): **decoded = DLL 5 / 5; closed intervals 1 / 5**. Ops 5 / 18
  (port linker on the DLL's passes, link_cmp): order and directions EQUAL.
- PORTED (viewer/cam3d_finish.c rp_prereq / rp_overlap / rp_npt): the measured rule and its guessed margin are gone.

### 19.2 The linker in mm (N13-27)
gen_raster puts the passes (its own or MCAM_HMM_PASSES) into mm (x 25.4 for an inch part) before the direction pass, runs the order and every
link with mm parameters (LinkMaxStayOnSurface, SurfaceRampTangentFactor = MachStepover, the lead radii, LinkRampHeightOffset, the tolerances,
LinkZClearance, LinkSafeZ, 0.02 mm LinkRFMinPassLength, 0.005 / 0.003 mm, 1.5 x MachStepover) and the drop in mm (raster_zdrop_mm), and divides
the linked moves by 25.4 after the final exit - mchmm's unit (the macro writer FUN_18007d7c8 / FUN_180075b48 writes every length x 25.4,
tools/hmm_macro.py; mckernel reads the toolpath back through CMCHstExtractTag FUN_1800654c4). On the first approach's tie (18.3) it changed
nothing: in mm r2 = 3.6290250000000022, the node's d2 = 3.6290249999999991 - still blocked by FUN_18019a6a0's strict test. That test is
however not Mastercam's for Raster (19.3).

### 19.3 Raster leads: FUN_18019e010 / FUN_18019a6a0 are not run (DECODED + traced)
- FUN_180194140 (first pass, hk l.2572-3513) calls FUN_18019e010 only when **HorizontalLeadInRadius** (+0x11f0 = 0x1afd, hl l.4904) > 0
  (bVar29, hk l.2892-2930 / 3032); else cVar6 stays 1 and the approach is the connect FUN_1801d1e70 + **FUN_1801db250** (hk l.3086-3120, the
  lead nodes of FUN_1801db820, 15.2). FUN_180197b80 (exit) likewise (+0x11e8 / bVar29, hk l.5300-5399) -> FUN_1801dab60; FUN_1801a1d70
  (retract) calls FUN_18019e010 only when +0x11f8 (HorizontalLeadOutRadius 0x1b32) or +0x11f0 > 0 (hk l.11975-12075, 12245-12361). mckernel's
  style-4 branch writes neither (they are written for style 1, tools/hmm_macro.py l.212); the plan default is <= 0.
- **Traced** (HMM_TRACE on PARALLELSTEEP op 5): FUN_18019e010 0 calls, FUN_18019a6a0 0 calls, FUN_180194140 1 -> FUN_1801db250 1, FUN_1801dab60
  1 (from 180198885 in FUN_180197b80). So the lead test of 13.5 and its tie (M1-L1, N13-21) **do not apply to Raster**: Mastercam builds the
  lead-in through the connect path without that test.
- FUN_1801db250's collision steps (FUN_1801d7450: FUN_1801cf7b0 at the connect's layer, FUN_1801cece0, FUN_1801e0ee0, FUN_1801d76a0) stay NOT
  DECODED. **User decision N13-29 (2026-11-14): every Raster lead (first approach, retract, exit) falls back to the SAFE link until that
  collision handling is decoded.** (For a few hours the branch emitted the lead whenever the drop lifted none of its points - that guard, which
  made the ops 5 / 18 first approach equal to the DLL's, is removed; the numbers measured with it are in 19.4 for reference.)

### 19.4 Measured (this branch; inch; seg_dist vertex-to-path)
| op | port vs NCI ref->ours / ours->ref (p50 / p95 / max) | port vs DLL | link_cmp (port linker on DLL passes) |
|---|---|---|---|
| PARALLELSTEEP 5, before (18.6) | 0.0001 / 0.0006 / 0.0750, 0.0000 / 0.0003 / 0.0042 | max 0.0750 / 0.0031 | 477 / 479 equal, approach SAFE |
| PARALLELSTEEP 5, after | 0.0001 / 0.0006 / 0.0078, 0.0000 / 0.0003 / 0.0042 | 0.0001 / 0.0005 / 0.0078, 0.0000 / 0.0002 / 0.0031 | order equal, all 479 within 0.0001 |
| RESTPASSES 18, before (18.6) | 0.0002 / 0.0009 / 0.0750, 0.0000 / 0.0004 / 0.0017 | max 0.0750 / 0.0012 | 371 / 373 equal, approach SAFE |
| RESTPASSES 18, after | 0.0002 / 0.0009 / 0.0063, 0.0000 / 0.0004 / 0.0017 | 0.0000 / 0.0009 / 0.0063, 0.0000 / 0.0002 / 0.0012 | order equal, all 373 within 0.0001 |
Retracts ref / port 2 / 2 on both. The remaining max (0.0078 / 0.0063) = pass-point differences, not links.
finish_oracle.sh RA (oracle3d, sampled): ref->ours max 0.079 -> 0.061, ours->ref max 0.854 -> 0.069 (p95 0.013 / 0.034 unchanged), cut length 2541 (ref 2543).

Regression (this branch vs master be499fa built from the same tree export): fileop 0 / 205 differ (one always-refused Area Mill op, same
message); op_compare 48 / 635 vs master's 47: the one new = gen_raster.txt, point count 15588 -> 16172 (the vertical leads now emitted where
the old FUN_18019a6a0 gate kept SAFE) - re-blessed; s4_check_trim(_stock) NC unchanged; checktrim_test.sh: all ok, stops 35 / 65 and 34 / 66.

### 19.6 The connect path's collision handling (N13-29 step 3): call map traced (the lead part DECODED + ported in 20)
Traced with HMM_TRACE on PARALLELSTEEP op 5 (the DLL run of hmm_oracle.py; dumps decomp/hmm_linker_x1/x2/x3/x5/x7/x8/x10.c, sizes in lines):
- First approach (FUN_180194140 -> FUN_1801d1e70 x1 l.870 (67), the connect; -> FUN_1801db250 x2 l.1204 (105)): FUN_1801dbf80 x7 l.131 (546, returns
  2 = the seeded pass points) -> FUN_1801db570 x7 l.1 (130) -> FUN_1801db820 x8 l.1 (218, the two lead nodes) -> FUN_1801d7450 x8 l.587 (108):
  FUN_1801cf7b0 x3 l.167 (209, the drop at the connect's layer) once, then **FUN_1801cece0** x10 l.1 (209: the refinement loop - per node pair
  FUN_1801ce270 (collision of the chord, x5 l.1880, 214) and FUN_1801cf7b0; split FUN_1801cc840 / FUN_1801cd2f0, the flat test FUN_1801cf230 /
  FUN_1801cd6f0, the 0.2 corner test on lead nodes flagged 8; on op 5's start: 18 ce270 calls, 4 splits FUN_1801cc840, returns the highest
  node Z in +0x30), FUN_1801d0240 x10 l.733 (45, removes 0x80 nodes), FUN_1801e0ee0 x10 l.210 (160), FUN_1801d9330 x10 l.370 (120) -> returns 1
  (FUN_1801d87b0 / FUN_1801d76a0 not reached on op 5), then FUN_180192510 (the emission).
- Exit (FUN_180197b80 -> FUN_1801dab60 x2 l.1041 (163) -> FUN_1801daf90 (not dumped) -> the same FUN_1801cece0 / FUN_1801e0ee0 / FUN_1801d9330 chain.
- The stay-down links use the same engine: every one of the 477 links runs FUN_1801cf230 / FUN_1801ce270 / FUN_1801cf7b0 per subdivision step
  (10973 / 11957 / 11005 calls on op 5) then FUN_1801d0240 / FUN_1801cea60 - the port's stay-down link reproduces the subdivision (12.3) but
  replaces FUN_1801ce270 / FUN_1801cf7b0 by its own "no lift" guard (c3_staydown), the same kind of guard N13-29 now rules out for the leads.
- What a faithful port needs (not started): FUN_1801ce270 (calls FUN_1801cdfd0, FUN_1801d0760 / 0860 / 09d0 / 0c10, FUN_1801d15f0, the model
  query FUN_180252d90) and FUN_1801cf7b0 (FUN_1801cf500 / 5e0 / 6b0 / 770, FUN_1803a46d0 / 4820, FUN_180252d90) - the layer drop and the
  chord collision against mchmm's model; then FUN_1801cece0's splits, FUN_1801d0240, FUN_1801e0ee0, FUN_1801d9330 / FUN_1801d87b0 / FUN_1801d76a0,
  FUN_1801daf90; check with HMM_TRACE (:v on the node lists) node by node against the DLL on ops 5 / 18 (19 approach moves each). Until then the
  Raster leads are SAFE (N13-29); ops 5 / 18 port vs NCI max 0.0750 (the first approach).

### 19.7 After N13-29 (leads SAFE; merged master e79ad45)
Ops 5 / 18 (seg_dist, inch): port vs NCI op 5 ref->ours p50 / p95 / max 0.0001 / 0.0006 / 0.1086, ours->ref max 0.0042; op 18 0.0002 / 0.0009 /
0.1216, ours->ref 0.0017; port vs DLL op 5 max 0.1086 / 0.0031, op 18 0.1216 / 0.0012 (the first approach and the final exit are SAFE now:
before 19.3 the exit passed FUN_18019a6a0's test and was emitted, so the max is above 18.6's 0.0750). Order: link_cmp equal (unchanged).
Regression vs master e79ad45 (built from the same export): fileop 0 / 205; op_compare 54 / 635 before re-blessing = master's set + the 7 Raster
scenes: gen_raster.txt (master's golden still has the old passes plan: 14540 -> 15555 pts, the M1l passes + SAFE leads), s4_check_trim(_stock)
.txt / .nc, s4_pick_check.txt, s4_pick_drive.txt (the lead arcs of the retracts / approach / exit replaced by SAFE links: e.g. the first move
X-1.999 -> X.001, the lead start -> the pass start) - re-blessed; checktrim_test.sh ok (35 / 65, 34 / 66) on the new NC.

### 19.5 Where M1 stops (thirteenth pass) - next, in order
1. The connect path of the Raster leads: decode and port the call map of 19.6 (FUN_1801ce270 / FUN_1801cf7b0 first); until then the leads
   are SAFE (N13-29). A synthetic wall next to a pass start and RESTPASSES op 19 exercise FUN_1801d87b0 / FUN_1801d76a0.
2. Waterline (another agent, from this branch): the port's Waterline links are all SAFE (no Waterline linker port yet); MCAM_HMM_PASSES for gen_waterline, then the Waterline
   Linking Plan 0x141c link by link with link_cmp (its exit and the style-1 retract +1.0 mm, 12.1 / 15.3), in mm like Raster.
3. The DLL's Waterline level pushes on our mesh (StepDownInflexion* / RadPushUp 0x1bbd-0x1bc0, 18.5) and op 19's rest-material macro + the
   tool body (17.1, hmm_oracle "not written") - not started this pass.

## 20. Fourteenth pass (2026-11-14, M1n): the Raster lead connect's collision handling DECODED + ported (N13-29 step 4)

Tool: `HMM_TRACE` gained `:l` (the connect's node list at rcx: std::list at +8 = {head, size}, iterators +0x18 / +0x20; a node = [0] next,
[1] prev, +0x10 x y z, +0x28 double, +0x30 contact code, +0x38 flag byte (2 = arc centre, 8 = corner, 4 / 0x10 orientation, 0x80 = removed),
+0x3c layer, +0x40 float t; printed on entry and return), `:r` (eight doubles at rcx: a parameter block) and `:R` (also 24 doubles at
[rcx + 0x38]: FUN_180252d90's tool block); with `:v` the point at rdx is printed again on return (FUN_1801cf7b0 writes its Z there). Scratch:
the traces of PARALLELSTEEP op 5 / RESTPASSES op 18 (the DLL runs of 19.4), `lcseq.py` / `lccmp.py` (the lead-check calls of the DLL vs the
port's MCAM_LK_DEBUG lines).

### 20.1 What the DLL does with a Raster lead (traced on ops 5 / 18, every call below; hk / x* = decomp/hmm_linker*.c)
- The lead connect: approach FUN_1801db250 -> FUN_1801db570 -> FUN_1801db820 (x8 l.5-217): two nodes put before the pass start S: the lead top
  T and the arc centre C (flag 2), list [T, C, S, next pass point]; exit FUN_1801dab60 -> FUN_1801daf90 (dump d1) -> FUN_1801da4a0 (dump d3, the
  mirror): [prev, E, C, T]. Both = the vertical lead arc the port computes (c3_lk_lead), to 1e-13 mm on ops 5 / 18.
- FUN_1801d7450 (x8 l.587-694): FUN_1801cf7b0 on T at layer +0x98 (0 on Raster); T rising (its Z below the dropped Z) removes the neighbour
  (l.627-636). Then (approach: T .. S; exit: E .. T) FUN_1801cece0, FUN_1801d0240, FUN_1801e0ee0, FUN_1801d9330; 0 from FUN_1801d9330 ->
  returns 1; else FUN_1801d87b0 + FUN_1801d76a0 (0 calls on ops 5 / 18).
- FUN_1801cf7b0 (x3 l.167-375) on Raster: the connect's +0x88 (region list) is 0, so it is FUN_1801cf5e0 -> FUN_1801cf500 (x4): layer 0 =
  CToolDropPos FUN_180222920 on the linker's mesh / tool +0x208 from z - off[0] (+0x1250 = ToolThicknessZ), z = drop + off when higher; the
  protect model +0x7c0 (FUN_1803a46d0) and the +0x1280 model (FUN_1803a4820): 0 calls traced.
- FUN_1801cece0 (x10 l.1-209): +0x30 = the highest node Z, then over the node pairs: a pair whose second node is an arc centre (flag 2) is
  the arc (a, C, b): FUN_1801ce270(a, b) (x5 l.1880-2093; XY gap 0 -> 0; +0x88 = 0 -> FUN_180252d90(*(connect + 0x50), a, b)); when 0: with C
  not flagged 4, the XY angle test (a - C).xy . (b - C).xy >= 0 and the arc midpoint (FUN_180060d70(.., a, C, b, |b - C|, 0)) dropped at layer
  +0x98 without a rise -> the arc stays; otherwise FUN_1801cc840 (x5 l.1-470; +0x88 = 0 branch): the midpoint M = FUN_180060d70(a, C, b,
  |b - C|, C's flag 4) dropped; M not rising -> [copy of C (flag 2), M] inserted before C and the loop goes on from a (depth first); M rising ->
  the move / "lifted" (+0xc8) branches (vtable +0x18, FUN_18005bdd0) - not ported. Straight pairs go to FUN_1801cf230 / FUN_1801cd6f0, corners
  (flag 8) to FUN_1801cd2f0 with the 0.2 test - not in a Raster lead.
- FUN_180252d90 (dis 180252d90-180252e95): XY gap^2 d2 <= hmin^2 -> 0; d2 > (+0x18)^2 -> 1; |dz| > +0x28 -> 1; +0x20 set and FUN_1802529d0 (the
  distance of the two points' contact normals FUN_1802538d0(point + 0x18, tool) > +0x20) -> 1; +0x10 set and FUN_180252a70(d2, |dz|) > (+0x10)^2
  -> 1; else 0 (-1 = test off). The block is the plan's +0xcb8.. (set at 1802eb7b6-1802ebb76): StepMinHorizontalChange 0x1acf, its square,
  StepMachTolerance 0x1acc, MaxHorizontalStepChange 0x1bd9, StepContactPointChordChange 0x1ace, StepMaxVerticalChange 0x1b3b, an int (tool
  +0x88 != 0), +0xcf0 = the tool +0x208. Traced values ops 5 / 18 (mm): 0.003, 9e-6, 0.0127, 0.567393, 0.261052, 2 = FUN_180305960's defaults
  (scallop.md 1: 2 sqrt((2 TSR - tol) tol), 2 sin 7.5 deg, 2.0). Tool block: [0] 3.175, [1] 3.175, [3] 0, [18] 0 (so FUN_1802538d0 gives (0, 0, 1)
  for code 0; every lead node and pass end has code 0 on ops 5 / 18: the +0x20 test never fires there).
- FUN_180252a70 (dis 180252a70-180252d86; Ghidra's decompile loses its xmm0 result, ported from the disassembly): the squared distance between the
  chord and the tool's swept profile (r = [1], flat f = [3], R0 = [0]) for (sqrt d2, dz), clamped parameter 0.5 outside [0, 1].
- FUN_180060d70 (dis 180060d70-180060f00): m = (a + b) * 0.5 (FUN_18005c730), d = |m - C|; d >= 1e-4: m s + C (1 - s), s = r / d; else C.z -/+ r.
- FUN_1801d0240 (x10 l.733-778) removes 0x80 nodes (none without a rise); FUN_1801e0ee0 (x10 l.214-373) + FUN_1801cfda0 (dump d2) merge
  consecutive arcs whose centres are both flag 2 with the same flag 4: the split lead arc is one arc again (traced: 18 -> 4 nodes);
  FUN_1801d9330 (x10 l.370-490): 0 when the highest node from the lead node to the far end is the lead node, followed by an arc centre whose far
  node equals (FUN_1800082c0) the end node. FUN_1801d91e0 (dump d2) then finds T, C, S unchanged and FUN_1801db570 / FUN_1801daf90 keep the lead.

### 20.2 Ported (viewer/cam3d_finish.c c3_lc_*), checked node by node
- c3_lc_lead: the top drop, then the arc refinement c3_lc_arc (FUN_1801cece0's arc branch + FUN_1801cc840's no-rise branch, c3_lc_chord =
  FUN_1801ce270 / FUN_180252d90, c3_lc_dev2 = FUN_180252a70, c3_lc_mid = FUN_180060d70); without any rise the plain vertical lead arc is emitted
  (the merge and FUN_1801d9330 give it back unchanged, 20.1); any rise (the top or a midpoint: the DLL's not-ported branches) keeps SAFE.
  The step block: MaxHorizontalStepChange from FUN_180305960's formula with TSR = ToolShaftRadius + ToolThicknessZ, StepMaxVerticalChange 2.0.
  The linker's tool profile is taken as (R, Rc, R - Rc) - traced for the stockless ball of ops 5 / 18; with stock to leave (LinkerToolThickness /
  LinkerToolCornerRadius, 11) it is NOT DETERMINED, so such ops keep SAFE leads (tool_ok).
- Enabled for the first approach (FUN_1801db250) and the final exit (FUN_1801dab60). A retract between passes (FUN_1801a1d70 -> FUN_1801a1240,
  its own connect path, 0 calls on ops 5 / 18, not traced) keeps SAFE.
- Check (MCAM_LK_DEBUG vs the DLL trace, the DLL's passes MCAM_HMM_PASSES): op 5 and op 18, approach + exit: 62 / 62 calls each (FUN_1801ce270
  30 / 30, FUN_1801cc840 14 / 14, FUN_1801cf7b0 32 / 32 incl. the two tops), same order, same results, every point within 1.1e-13 mm.

### 20.3 Measured (inch; MCAM_HMM_PASSES = the DLL's passes, or the port's own)
- link_cmp on the DLL's passes: op 5 479 / 479 links equal within 0.00015 (the two leads: 0.00010, our mccore line steps vs the DLL's arcs as
  16 chords), op 18 373 / 373. Stay-down links unchanged: 477 / 371, SAFE 0.
- seg_dist, port (own passes) vs NCI: op 5 ref->ours p50 / p95 / max 0.0001 / 0.0006 / 0.0078 (was 0.1086 with SAFE leads), ours->ref max
  0.0042; op 18 0.0002 / 0.0009 / 0.0063 (was 0.1216), ours->ref 0.0017. Port vs DLL: op 5 max 0.0078 / 0.0031, op 18 0.0063 / 0.0012.
  link_stats: retracts 2 / 2, rapid XY 0, plunges at feed 2 / 2 on both.
- Regression vs master e79ad45: fileop 0 / 205 differ (also 0 vs the branch before this pass); op_compare 47 / 635 = master's set (no golden
  re-blessed: the 7 Raster scenes re-blessed in 19.7 keep their SAFE leads - their ops leave stock, so the linker's tool profile is NOT
  DETERMINED and c3_lc_lead refuses before any call, MCAM_LK_DEBUG: 0 calls); checktrim_test.sh ok (35 / 65, 34 / 66).

### 20.4 Where M1 stops (fourteenth pass) - next
1. The rise branches (FUN_1801cc840's move / lifted path, FUN_1801d7450's node removal, FUN_1801d87b0 / FUN_1801d76a0): needs a case where a lead
   midpoint rises (a wall next to a pass start; the DLL harness takes a synthetic STL, the port side needs the same mesh - not set up).
2. The stay-down links still use the port's own chord guard (12.3, c3_staydown): the DLL's is the same engine (FUN_1801cf230 = FUN_1801ce270 +
   the deviation test, FUN_1801cd970's drop of each new node, then FUN_1801d3a60 / 1801d4270 / 1801d3c20 / 1801d39e0). Replacing the guard by
   "FUN_1801ce270 in the split test + a rise at any dropped node -> SAFE" is the next step (counts to check: op 5 FUN_1801cf230 10973,
   FUN_1801ce270 11957 - 30, FUN_1801cf7b0 11005 - 32).
3. Retract leads between passes (FUN_1801a1d70 / FUN_1801a1240), the linker's tool profile with stock (LinkerToolThickness), Waterline (other agent).
## 21. Fourteenth pass (2026-11-14, M1n): the Waterline linker - the DLL on the stored ops, the pass order DECODED + PORTED; links still SAFE

hz = decomp/hmm_zplan.c. New: Ghidra DecompAt of mchmm.dll FUN_1804a20b0 / 1804a2270 / 1804a2040 / 1804a1b80 (scratch, the CZInflexions
collectors); `HMM_TRACE ADDR:r` prints the u64 at rax on return (a returned key); tools/tests/wl_graph.py / wl_order_model.py / wl_pinfo.py;
tools/hmm_macro.py `HMM_NO_INFLEXIONS=1` (testing only, below).

### 21.1 Step 1: the DLL on the stored Waterline ops
| op | DLL vs stored NCI ref->dll / dll->ref (p50 / p95 / max) | retracts ref / dll | notes |
|---|---|---|---|
| TUTORIAL 2 SWARF op 4 (inch, style 2) | 0.0025 / 0.0133 / 0.112, 0.0025 / 0.0142 / 0.118 | 42 / 42 (all at 4.0) | unchanged from 17.2: close |
| MOLD BOTTLE op 4 (X9, inch, style 2, D 0.375 ball) | 0.0037 / 0.0128 / 0.248, 0.0038 / 0.0239 / 0.258 | 71 / 71 (all at 4.0) | first run: close (rapid XY 84.2 / 81.7) |
| SWARF_METRIC op 3 (mm, style 1), macro as mckernel writes it | 0.049 / 1.10 / 7.15, 0.045 / 0.87 / 3.38 | 39 / 26 | as 18.5 |
| SWARF_METRIC op 3, StepDownInflexionToleranceEnable off (testing) | 0.0036 / (capped 0.5) / -, 0.0026 / - / - | 39 / 27 | **the DLL's levels = the stored levels exactly** (40.8256 - 1.2 k ... 8.4857) |
- **Why the DLL pushes op 3's levels up (DECODED)**: CZInflexions (FUN_1804a1b80) collects, per triangle in the box and Z window, FUN_1804a2270:
  a flat triangle's Z, a near-flat triangle's [zmin, zmax] (gradient +0x50), then **FUN_1804a20b0 on its three edges** (an edge with equal end Z
  (FUN_1800082c0) -> that Z in the set +0x70; a near-flat edge (dz^2 <= L^2 g^2) -> its [z1, z2] interval) and **FUN_1804a2040 on its three
  vertices** (each vertex Z in the box -> the set). The level is then moved off every such height by Avoidance (c3_infl_level). The port's
  c3_levels_infl leaves the vertex / edge heights out (ours, cam3d_rough.c ci_tri comment: "our tessellator's vertex and edge heights are not
  Mastercam's"); the DLL, run on this port's mesh, takes them - and our tessellation of op 3's sloped walls has vertices at arbitrary heights, so
  nearly every nominal level lies within 0.012 (= Stepdown x StepDownInflexionTolerance) below one and is pushed up (steps 1.1911 .. 1.2000 instead
  of 1.2). Mastercam's own mesh (mcmsurf) evidently has no vertex heights in those windows (its levels are the plain grid). With the snap off
  (HMM_NO_INFLEXIONS=1, a testing switch in hmm_macro.py, NOT mckernel's macro) the DLL reproduces the stored levels; so the reference for op 3's
  links is that run, and the cause of the push is **the input mesh**, not a DLL rule the port lacks. Question M1-W1 (TODO): the port's omission of
  the vertex / edge heights is an existing deviation from the decoded code that only compensates for our tessellator.
- Op 3's check surfaces: the record says check method 0 (0x172c = 0, 18.5); the DLL / the port trim with the method-1 plans (0x141f + 0x1458),
  so op 3's lowest levels are not a reference for either, and its linker graph after the trim plans is NOT DECODED (21.3): op 3 keeps the old order.

### 21.2 The region graph (DECODED; traced)
- The Waterline passes plan (FUN_1803ffdb0 hz l.3903) gives every region of a level its toolpath (FUN_18026fb30: the region's frags) and, on the
  default branch (0x1bd5 off; traced: FUN_180401e90 0 calls, FUN_18026fb30 / FUN_1803fe090 45 calls on op 4), the previous-level regions it hangs
  from: FUN_1803fe090 (hz l.2672-3330) walks the region's curves; FUN_1803fedb0 (hz l.3330) takes a curve's mid-length point and its unit tangent,
  moves it along the right-hand normal by 0.1 (then 0.05, 0.025, 0.0125; while > 0.01) until FUN_1802887a0(point, 0.01, region polygon) > 0 (inside
  the region; the fallback after that loop, hz l.3368-3640, is NOT DECODED); FUN_1803ff7b0 (hz l.3648) returns the first previous region (not yet
  found) whose box and polygon hold the point. FUN_180400db0 (hz l.4510) adds the node (FUN_1802ed200) and the edge node -> parent (FUN_1802ed2c0).
- Traced on op 4: 45 nodes, 44 edges, a chain down the levels with the bottom pockets (5 nodes at 0.4614) hanging from the 0.5111 loop; frag keys
  follow their node key (node 7d: frags 7e, 7f), and the tag-1 pass order is the key order.

### 21.3 The order (DECODED, checked with the DLL, PORTED)
FragOrderer with the Waterline plan's defaults (OrderingType 1, WholeLevelBeforeNext false, LinkHeadNorthwards true, LinkByZLevel false):
- the lists as for Raster (19.1): +0x110 of P = the passes of P's child regions (FUN_180184200, through regions without passes) that overlap P
  (FUN_1801c2300) at **LinkORAdjacentLevelOverlap 0x1b13 = `2.2 * ToolShaftRadius`** (decomp/hmm_link.c l.18719-18726, the Waterline plan;
  ToolShaftRadius = radius + ToolThicknessXY - ToolThicknessZ); +0x340 = the inverse;
- next pass (FUN_1801b90d0): (1) FUN_1801c2d70 -> FUN_1801c2f40 = the previous pass's +0x110 minus the taken (traced with `:3`: one list per pass,
  e.g. 90 -> {93}); FUN_1801b9820 / 1801b9cc0 / 1801b9d70 take the nearest by FUN_18027a4f0 = squared 3D distance from the previous end to the
  start, **for a closed pass to its nearest point** (FUN_18027a590 -> FUN_18027d4e0, x2); (2)-(4) the nearest free pass; the first pass from
  (LinkStartX, LinkStartY, LinkSafeZ) with FUN_1801b9820's walk;
- **FUN_1801b9490** (hk l.27988): a blocked choice (FUN_1801bf440) becomes the nearest (FUN_1801c9500 / FUN_1801c9390, from the previous end) of
  its free prerequisites followed down the chains - traced: after pass 90 the list {93} (the 1.557 loop, blocked by 91 <- 8e <- 8b <- 87 / 88 <-
  82 / 83) resolves to 82 / 83 and 83 is taken (FUN_1801c38a0's map +0x388 was the identity on every call);
- the entry of a closed pass (FUN_1801c05c0): re-split at the nearest point S to the previous end; a stay-down link (XY gap < LinkMaxStayOnSurface,
  the next region a child of the previous one's) with SurfaceRampStyle 1 trims the next pass's start by |dZ| / tan(SurfaceRampAngle)
  (FUN_1801c3dd0), so a closed pass starts and ends that far past S (DLL op 4 moves 4953 / 5729: loop 15 from (1.9745, 1.3895) back to it, the
  ramp to loop 16 at (1.889, 1.351)); that end is the next choice's reference. Without it the model left op 4 at pass 37 (the bottom pockets).
- **Checked**: tools/tests/wl_order_model.py on the DLL's passes and its traced graph: **op 4 65 / 65, MOLD BOTTLE op 4 98 / 98** passes in the
  DLL's order (both XY or 3D nearest points, both the overlap-list and the region-child stay-down test give the same). Op 3 (checks): the
  first 25 (the untrimmed levels) equal; the trimmed frags (keys db.., new toolpaths of 0x141f / 0x1458) are not in the passes plan's graph
  (no adjacency writer runs for them: traced) - how the linker places them is NOT DECODED.
- **Ported** (viewer/cam3d_finish.c wl_link, wl_probe, wl_resplit; gen_waterline collects the passes in mm per region first): for ops without
  check surfaces, LinkByZLevel false, LinkWaterlineDirectionStyle 0, no start point, clearance on and absolute; else (and when the probe has no
  inside point) the old order. The port's regions: the loops of a level whose probe (the stored loop's right-hand normal: Clipper keeps the
  forbidden region on a loop's left) has the same innermost containing loop (ours: the slice gives loops, not McHmm's regions). The links stay
  SAFE (ours) and an open pass is not trimmed.

### 21.4 Measured (cam_cli waterline; seg_dist inch; link_stats)
| op | before (old order) | after |
|---|---|---|
| SWARF op 4: rapid XY (ref 51.2, DLL 50.7) / retracts (ref 42) | 81.5 / 71 | 63.9 / 71 |
| SWARF op 4: ref->ours p95 / ours->ref max | 0.1037 / 0.0509 | 0.1029 / 0.0869 |
| MOLD BOTTLE op 4: rapid XY (ref 84.2, DLL 81.7) / retracts (ref 71) | 204.2 / 115 | 104.9 / 115 |
| MOLD BOTTLE op 4: ref->ours p95 / ours->ref max | 0.1167 / 0.0019 | 0.1146 / 0.0019 |
| SWARF_METRIC op 3 | unchanged (checks: old order) | |
The retract count is the SAFE link (every link retracts). The port's passes differ from the DLL's at the upper levels (the contact-angle split:
op 4 70 vs 65 passes, MOLD BOTTLE 114 vs 98) - a passes-plan difference that the order follows faithfully but cannot hide (wl order debug:
MCAM_WL_ORDER=1). ours->ref max 0.087 on op 4 = an entry point of a closed loop now where Mastercam enters (the SAFE plunge there).
Regression: op_compare 48 / 635 (the branch's base: 48, the same scenes) - gen_waterline.txt changes only in the point count (5059 -> 5070: the
re-split closed loops' entry points), re-blessed; fileop 205 user ops: none is a Waterline op (gen_waterline is the only function changed), all
ran.

### 21.5 Where M1 stops (fourteenth pass) - next, in order
1. The Waterline links on the decoded order: the stay-down surface ramp (the previous pass's extension FUN_180279f90, the trim FUN_1802829c0,
   the ramp's smoothing +0x1120 = the five short moves), the retract style 2 at LinkZClearance (13.2) with the horizontal + vertical leads
   (Waterline writes HorizontalLeadIn / Out, so FUN_18019e010 and the lead test FUN_18019a6a0 do run here - to be traced), the exit without the
   feed-plane move (15.3), style 1's +1.0 mm (12.1; reference: op 3 with HMM_NO_INFLEXIONS).
2. MCAM_HMM_PASSES for gen_waterline (the region graph needs regions: dump the DLL's graph with wl_graph.py and feed it, or rebuild regions from
   closed passes); the passes-plan differences (contact-angle splits) first, since the order follows them.
3. The trimmed frags' graph (op 3 / any op with checks), LinkByZLevel's key FUN_180170520, direction styles 1 / 2.
4. Question M1-W1 (CZInflexions vertex / edge heights, 21.1) - answered N13-30, see 22.1.

## 22. Fifteenth pass (2026-11-14, M1o): N13-30 applied; the Waterline linker on the DLL's own input

### 22.1 N13-30: the Waterline levels with FUN_1804a2270's edges and vertices
User decision N13-30: apply the decoded rule although our tessellation moves the levels (mckernel's tessellation is a separate task). Ported in
cam3d_rough.c ci_edge (FUN_1804a20b0: an edge in the box and Z window, equal end heights (FUN_1800082c0, exact) -> its height into the set,
else dz^2 <= L^2 g^2 -> its [z1, z2] interval) and ci_tri's tail (the three edges, then the three vertices, FUN_1804a2040); switched on by
gen_waterline only (c3_infl_full: the decision names the Waterline levels; OptiRough / Area Roughing keep the old collector - to be asked).
Measured (port levels vs the DLL's on the same mesh / vs the stored NCI; levels.py, scratch):
| op | port -> DLL levels mean / max (before -> after) | port -> stored mean (before -> after) | seg_dist p50 ref->ours (before -> after) |
|---|---|---|---|
| SWARF_METRIC op 3 (mm) | 0.074 / 0.426 -> 0.0050 / 0.0147 (11 of 28 exact) | 0.0011 -> 0.087 | 0.0047 -> 0.0587 (DLL 0.049) |
| SWARF op 4 (in) | 0.0033 / 0.0065 -> 0.0004 / 0.0012 | 0.0005 -> 0.0025 | 0.0005 -> 0.0025 (DLL 0.0025) |
| MOLD BOTTLE op 4 (in) | 0.0037 / 0.0072 -> 0.0001 / 0.0006 (31 exact) | 0.0004 -> 0.0034 | 0.0004 -> 0.0039 (DLL 0.0037) |
So the port's levels now follow the DLL on the same mesh (the remaining gap: the inflexion box ibx and the vertex set of our slice), and move away
from the stored ones by the mesh difference, as the decision expects. Op 3 rapid XY 945 / retracts 57 (the old order: checks).
Regression (this branch after `git merge master`, full build): op_compare 47 / 635, the same scene list as the run before N13-30 minus
gen_waterline.txt (its re-blessed golden holds: the scene's levels do not move), so no golden re-blessed for N13-30; fileop 0 / 205 differ from the run before N13-30 (no user op is a Waterline op).

### 22.2 MCAM_HMM_PASSES for Waterline
`MCAM_HMM_PASSES=PREFIX.mmpasses MCAM_HMM_WLGRAPH=PREFIX.wlgraph cam_cli PART waterline ...`: gen_waterline replaces its collected passes and
regions by the DLL's tag-1 passes and the DLL's region graph (tools/tests/wl_graph.py TRACE OUT.wlgraph, from an HMM_TRACE of
1801c05c0:k,1802ed200:k,1802ed2c0:k) and runs wl_link on them. The port's order on the DLL's input = the DLL's traced order: **SWARF op 4 65 / 65,
MOLD BOTTLE op 4 98 / 98** (MCAM_WL_ORDER prints the pass indices). link_cmp.py does not yet segment re-split closed passes (their ends move), so
the link-by-link comparison of the (SAFE) links is not run.

### 22.2a N13-30 extended (2026-11-14): every Z-profile op
User answer: the rule applies to OptiRough and Area Roughing too (they call the same c3_levels_infl). ci_tri now always takes the edges and vertices
(the Waterline-only switch is gone). Measured: tools/tests/arearough_oracle.sh (SWARF_METRIC Area Roughing) moves byte-identical before / after
(its levels did not move: level ref 20.831 / ours 20.800, ref->ours p50 0.138 p95 1.608 unchanged); tools/tests/hst3d_oracle.sh (TUTORIAL 7 METRIC
op 5 OptiRough) fails before and after with "nothing to cut (3 levels, 1 regions)" - the same with the main checkout's master build (09:01), so a
pre-existing master failure, not this change. Regression (after merging master and worktree-agent-aa1d21ab8b233e49a, full build): op_compare
47 / 635, the same scene list as before (no golden re-blessed); fileop vs the main checkout's master build: 0 / 205 differ (the user parts'
OptiRough / Area Roughing ops keep their levels).

### 22.2b link_cmp.py for Waterline
A closed pass is found in a toolpath by its points (a run of feed moves on the loop within --tol reaching 0.9 of its length), since the linker
re-splits it anywhere. Port (on the DLL's passes, SAFE links) vs DLL, SWARF op 4: A 65 / 65 passes found, B (the DLL) 60 / 65 (its loops 34-36 run
into the bottom pockets through the surface ramps, not yet matched); order equal up to position 34; every link differs (SAFE vs the DLL's
stay-down 19 / retract 42) - the expected starting point for the link ports.

### 22.2c The upper-level passes-plan difference (op 4 70 vs 65)
Per level the port has more open passes where the DLL has one (1.7053: 7 vs 2, 1.6553: 6 vs 2): the port cuts a DLL pass into pieces at gaps of
1.0-1.2 mm (e.g. pass 12 end (-2.2023, -0.4439) -> pass 13 start (-2.1586, -0.4415)), i.e. the contact-angle filter (FUN_18040b6f0) drops vertices
the DLL keeps, and the gap then exceeds PassesJoinGaps 1.0 mm. The port's contact normal (wl_contact_nz, a nearest-triangle construction) is not the
weave's contact word, so this is the filter's input (hst3d_port_plan.md 7 "Contact angle filter inputs"), not the linker. Not fixed this pass.
**Fixed 2026-11-14 (28)**: the weave contact word ported (= the DLL vertex by vertex) and the window's degrees as mckernel writes them (28.2):
MOLD BOTTLE 98 / 98; op 4 (bull) stays on the offset path, 28.6.

### 22.3 Where M1 stops (fifteenth pass) - next, in order
1. link_cmp for Waterline (match feed runs to passes by the points on them, not by the pass ends).
2. The passes-plan differences at the upper levels (op 4 70 vs 65, MOLD BOTTLE 114 vs 98 passes: the contact-angle split of the loops) - not started.
3. The links (21.5 1.), each against the DLL with HMM_TRACE; the Raster collision engine of worktree-agent-aa1d21ab8b233e49a (c3_lc_*) is not on
   master yet, so not reused here.

## 23. Fifteenth pass (2026-11-14, M1o): the stay-down links through the DLL's engine, the retract leads, the linker's tool with stock

Tools (testing): `MCAM_MESH_STL=FILE` (Raster's drive mesh replaced by an ASCII STL in mm) and `MCAM_TC=FILE` (its containment replaced by loops in
hmm_dump_tc's format) - with MCAM_HMM_DUMP the harness gets the SAME mesh / containment / record (the record patches given to cam_cli with --rec are
written into PREFIX.rec too), so synthetic cases run through the port and mchmm.dll alike. `MCAM_SD_DEBUG` prints the stay-down calls ("sd ce270 /
ce270c / cf7b0"), `MCAM_LK_DEBUG` also the retract height's ("rt ..."); `link_cmp.py --dump K` prints link K of both toolpaths. Scratch (not
committed): sdcmp.py / rtcmp.py (the DLL's calls inside FUN_180199220 / FUN_1801990c0 from an HMM_TRACE log vs the port's lines), valley.py /
wall.py (the synthetic STLs), syn2.sh (RESTPASSES op 18's record on a synthetic mesh, port + DLL + port on the DLL's passes).

### 23.1 The stay-down link = the DLL's connect engine (step 1, PORTED; our chord guard removed)
- FUN_180007fd0 (x5 l.507-620, the reduce body) walks the node pairs: FUN_1801cf230 (x5 l.3029-3140) = FUN_1801ce270 on the nodes, and when set in
  mode 3 again on the undropped curve points at the nodes' t (the 0x2c layer 2 / contact 0 points); else the LinkSurfaceRampSmooth XY deviation. When
  set or the nodes' XY gap^2 > StepMinHorizontalChange^2: FUN_1801cd970 (x6 l.65-300) drops the new node (FUN_1801cf7b0 at +0x98) and, not set
  yet, tests its 3D distance from the dropped chord; a set pair gets the node (FUN_180007eb0 -> FUN_1801d0b20, node flags from the connect's
  virtual +0x20 with 8: every stay-down node carries 0x40, traced) and the walk goes on depth first. FUN_180007eb0 keeps the highest node (+0x30),
  the largest |dropped z - curve z| (+0x38, FUN_180006910 x3 l.40-125) and a lifted flag (+0xc8).
- Mode 3 after the reduce (FUN_1801d2bd0 x1 l.1259-1330): FUN_1801d3a60 / 1801d4270 / 1801d3c20 / 1801d39e0 (x3 l.736-1060, x2 l.252) only compare
  and undo the wander offsets of nodes with +0x44 bit 1, set by the connect's virtual +0x28 that FUN_1801cd970 calls only with connect +0xb0
  (LinkAllowWandering) in mode 3 - none on Raster (no change, traced); FUN_1801d0240 removes 0x80 nodes (none); the wander FUN_1801deaf0 runs
  only with +0xa0 (not on the mode-3 stay-down links, traced: 0 calls of 119); FUN_1801cea60 (x3 l.589-690) removes a node only when it lacks
  0x40, is not the connect's end (FUN_1801e0d20), the layers agree and FUN_1801ce270(prev, next) is 0 - every stay-down node has 0x40: none.
- Kind 1 (FUN_1801c3dd0 x1 l.2014-2018: both passes on the same raster line, frag +0x70, and SurfaceRampExludeSameLevel +0x1234, default true):
  the mode-1 connect - FUN_1801cf230 = FUN_1801ce270 on the nodes only; FUN_1801cd970's node = the XY midpoint (FUN_18000b4e0, 0.5) at the mean
  of the nodes' Z (connect +0xa0 set: the wander flag = both passes' +0x78; FUN_1801deaf0 traced on these links), dropped, split when farther
  than StepMachTolerance; FUN_1801deaf0 (x3) returns 1 for a 2-node path (left alone): an unsplit kind-1 link is the straight move. A split one
  goes to FUN_1801dde40 (the wander, NOT DECODED: SAFE).
- FUN_180199220 (hk l.5928-6400): the connect's +0x38 above SurfaceRampMaxVerticalChange 0x1cfb (default 2.1 x StepMachTolerance) -> the
  pass-extension loop FUN_18019a310 (when neither frag has +0x78, both extension parameters 0x1ef0 / 0x1ef1) / a mode-1 retry, and then the
  retract FUN_1801a1d70 (traced on the 80 deg valley below: 4 links, FUN_18019a310 40 + 40 calls, then 4 retracts from 18019a08d) - NOT DECODED
  here: such a link keeps SAFE (labelled "rise"); else (path length +0xc0 > 0) the connect's nodes at the feed, lifted nodes included.
- Ported (viewer/cam3d_finish.c c3_sd_split / c3_sd_line / c3_staydown): the guard "drop sampled 8 x per chord" is gone.
- Checked call by call (sdcmp.py; the DLL's passes, MCAM_HMM_PASSES): PARALLELSTEEP op 5 FUN_1801cf230 10973, FUN_1801ce270 11927 (+ 30 of the
  lead check = the trace's 11957), FUN_1801cf7b0 10973 (+ 32 = 11005): 22900 / 22900 calls equal, points within 8.5e-14 mm; RESTPASSES op 18
  5565 / 5565 / 5565: 11130 / 11130 equal (1.4e-14 mm); the toolpaths of ops 5 / 18 are unchanged byte for byte (no lifted node, the 954 ce270
  splits of op 5 coincide with the deviation splits). Synthetic 45 deg valley (valley.py, op 18's record, Use Z depths PosZHi 0.3 in): 124
  stay-down links, 20 with lifted nodes (max rise 0.0095 mm) and 5 kind 1: 3572 / 3572 calls equal, link_cmp 74 / 74 stay-down links equal.

### 23.2 The retract between passes (step 3, PORTED)
- FUN_1801a1d70 (hk l.11351-12400) runs FUN_1801a1240 only with a horizontal lead radius (+0x11f8 / +0x11f0 > 0, hk l.11981-12051) - never on Raster
  (0 calls traced). Raster: FUN_1801d69f0 (x1 l.112-215; FUN_1801d65f0 without +0x1108): two connects, the exit lead FUN_1801dab60 at the previous
  pass's end (its result, the lead top raised by LinkRampHeightOffset +0x1170, at +0x140), then the approach lead FUN_1801db250 at the next pass's
  start (+0x158) - the lead connects of 20 with the same collision handling - then FUN_1801d62e0 and FUN_1801d7210 (x1 l.375-467): h =
  FUN_1801990c0(+0x140, +0x158) and the two nodes at h when above the lead points.
- FUN_1801990c0 (hk l.5868-5915, dis 1801990c0-18019920e): a mode-1 connect A -> B (FUN_1801d1c20, layers 0 for LinkRetractStyle 2, the new nodes
  at the mean Z: traced) through FUN_1801d2bd0; its highest node z (+0x30) hz; h = LinkZClearance when it is >= hz, else hz + 1.0 (mm), at least
  max(A.z, B.z). Replaces our sampled "1 mm below the clearance" guard.
- Ported (c3_link: the exit lead check first, then the approach lead's, c3_lk_pathz / c3_lk_mid1). Checked on the 45 deg valley: the lead calls
  366 / 366 (FUN_1801ce270 174, FUN_1801cc840 78, FUN_1801cf7b0 192) and FUN_1801990c0's 344 / 344 equal (1.4e-14 mm); link_cmp 80 / 84 links equal,
  the other 4 (the approach and 3 retracts) within 0.0001 in (our mccore lines vs the DLL's arcs as 16 chords).

### 23.3 The linker's tool profile with stock (step 4, PORTED)
- Measured with mchmm.dll (HMM_TRACE 180252d90:R, the tool block at [rcx + 0x38] of the linker's FUN_1801ce270 calls, RESTPASSES op 18's macro with
  stock XY / Z in mm): 0 / 0 -> [0] 3.17373, [1] 3.17373, [3] 0; 0.508 / 0 -> 3.6815268, 3.1735268, 0.508; 0.508 / 0.254 -> 3.6815268, 3.4275268,
  0.254; 0 / -0.254 -> 3.17373, 2.91973, 0.254; 0.254 / 0.254 -> 3.4276284, 3.4276284, 0 (0 / 0.254 is refused by the passes plan, ERR 5124 100).
  All = 11's FUN_1802f5270: S = ToolShaftRadius = R + XY - Z, t = LinkerToolThickness = Z - 0.0002 x 2 (Z + S), c = LinkerToolCornerRadius =
  t < 0 ? max(Rc, -t) : Rc, FUN_18013e200(S, c) then FUN_18013e330(.., t, 0) (decomp/hst3d_grid.c l.2957-3060): [0] = S + t, [1] = c + t, [3] = S - c.
  So the stockless ball's block is NARROWED too (3.17373, not 3.175: 20.1's "[0] 3.175" was read from the passes plan's FUN_180252d90 calls, the
  linker's are the narrowed ones) - ported; the 62 lead calls of ops 5 / 18 stay equal.
- The connect's layer-0 drop (FUN_1801cf500 x4 l.251-283) is CToolDropPos with the plan's +0x208 = this profile from z - ToolThicknessZ
  (+0x1250), + ToolThicknessZ: the port's links now drop with their own HstDrop (ball c + t, flat S - c, + Z) instead of the passes' drop.
- MaxHorizontalStepChange's TSR = ToolShaftRadius + ToolThicknessZ = R + XY (was R + Z: equal only without XY stock); traced 0.611185 for
  0.508 / 0.254 (= 2 sqrt((2 x 3.683 - 0.0127) 0.0127)).
- So the stock-leaving ops (the 7 Raster scenes of 19.7) no longer keep SAFE leads for want of the profile.

### 23.4 The rise branches (step 2): the synthetic harness works; the branches traced, NOT DECODED
- Same mesh on both sides: MCAM_MESH_STL / MCAM_TC (above). Valleys (45 / 80 / 89.9 deg walls, the passes cut by the Z window): no lead rises
  (the vertical leads follow the pass slope). Wall case (wall.py: a floor, a 12.7 mm block whose vertical face is 0.15 in beyond the containment's
  far edge, stepover 0.03 in so every link is a retract): mchmm.dll FUN_1801d7450 87, FUN_1801d9330 87, FUN_1801d87b0 58 (from 1801d7653) + 203
  (from 1801d8572, inside FUN_1801d8240), FUN_1801d76a0 58, FUN_1801cc840 551; the port keeps the 15 wall-side retracts (approach / exit leads
  rising) SAFE and its other 14 retracts + the approach equal the DLL's (link_cmp, 0.0001). The DLL's wall-side link: the leads replaced by a
  straight vertical move up by the lead radius (then + LinkRampHeightOffset, the clearance, across, down).
- What is decoded of that path: FUN_1801d7450 (x8 l.587-694): T rising removes the arc centre (FUN_1801d0890), then FUN_1801cece0 / FUN_1801d0240 /
  FUN_1801e0ee0 and FUN_1801d9330; nonzero -> FUN_1801d87b0 (x10 l.490-520: removes the lead's nodes between the connect's end and +0x1b0 / +0x1c0)
  and FUN_1801d76a0 (x10 l.526-735): with its 7th argument FUN_1801d8240, then FUN_1801d7b00 (x8 l.219, ~420 instructions); without the connect's
  +0x1c9 / +0x1ca flag the lead re-seeded on the neighbouring pass (FUN_1801dbf80) and FUN_1801d7b00 again; finally FUN_1801d8630 (dis
  1801d8630-1801d87af): one node at the pass end + (0, 0, the lead radius |T - C|) - the measured vertical lead. FUN_1801d8240 (dis
  1801d8240-1801d862c) and FUN_1801d7b00 (the lead's other attempts, each through FUN_1801d87b0) are NOT DECODED, so the fallback cannot be
  ported yet (it is reached only when they fail): a lead whose check rises keeps SAFE (N13-29).

### 23.5 Check surfaces: the protect model (NOT PORTED -> SAFE links; PORTED in 24.1)
With a check model the linker's drop also runs FUN_1803a46d0 (linker +0x7c0, the protected surfaces of Edit Passes to Surfaces, LinkerProtectThickness
+0x1260 and the grown profile +0x17a0, 11) on every FUN_1801cf7b0 (FUN_1801cf5e0 -> 1801cf65a): traced 9734 calls on the 45 deg valley with a
check strip added (PREFIX.chk.stl, hmm_macro's 0x141f chain), 0 without. That drop is not ported, so gen_raster keeps EVERY link of an op with
check surfaces SAFE (stay-down, retracts, leads; before this pass its stay-down links were made against the drive alone - our guard, wrong).

### 23.5b Step-block defaults (step 5)
FUN_180305960's block and the linker's tool for other tools, traced (180252d90:R, op 18's macro): flat end mill (Rc 0): block 0.003 / 9e-6 / 0.0127 /
0.567393 / 0.261052 / 2, tool 3.17373 / 0 / flat 3.17373; bull Rc 1.0 with stock XY 0.2 / Z 0.1: 0.585026, tool 3.37365 / 1.09865 / 2.275;
bull Rc 1.0, StepMachTolerance 0.05: 0.003 / 0.05 / 1.122497, tool 3.17373 / 0.99873 / 2.175 - all = the port's formulas (TSR = R + XY,
23.3's profile). The corpus has no other Raster op with a DLL macro (RESTPASSES op 19's rest-material macro is not written, 17.1).

### 23.6 Measured (inch; ops 5 / 18 identical to 20.3)
- PARALLELSTEEP op 5 / RESTPASSES op 18: the port's toolpaths are byte-identical to the fourteenth pass's (own passes and DLL passes): seg_dist / link_stats
  as 20.3 (op 5 vs NCI ref->ours max 0.0078, op 18 0.0063; retracts 2 / 2).
- Regression (this branch vs the branch before this pass, aa1d21ab = master e79ad45 + 20): fileop 0 / 205 differ (1 FAIL in both, the always-refused
  Area Mill op); op_compare 54 / 635 = the previous set (47) + the 7 Raster scenes, re-blessed (explained): gen_raster.txt 15555 -> 16174 pts and
  s4_pick_drive.txt 26930 -> 26977 (stock to leave: the leads / retract leads now through the decoded check instead of SAFE, stay-down links with
  lifted nodes kept), s4_check_trim(_stock).txt / .nc and s4_pick_check.txt (check surfaces: every link SAFE, 23.5 - the stay-down turns at the
  check become feed-plane links); checktrim_test.sh: all ok, stops 35 / 65 and 34 / 66 on the new NC. (BLESS also writes
  ~/mastercam-re/regress/golden/op.)

### 23.7 Where M1 stops (fifteenth pass) - next
1. The rise branches: FUN_1801d8240 / FUN_1801d7b00 (the lead's other attempts) then FUN_1801d8630's vertical fallback (decoded) - on the wall case
   (syn2.sh, wall.py) call by call. FUN_1801cc840's lifted branch (vtable +0x18, FUN_18005bdd0) belongs to the same runs (551 calls there).
2. The stay-down rise above SurfaceRampMaxVerticalChange: the pass extension FUN_18019a310 and the mode-1 retry of FUN_180199220 (80 deg valley:
   4 links), then the retract it falls back to (FUN_1801a1d70 from 18019a08d, the same retract as 23.2 once its inputs are known).
3. The kind-1 wander FUN_1801dde40 (a split same-line link); Waterline (other agent).

## 24. Sixteenth pass (2026-11-14, M1p): the protect drop (check surfaces) and the stay-down rise branch

Tools (testing): `MCAM_CHK_STL=FILE` (the check model of c3_chk_open replaced by an ASCII STL in mm - also for an op without checks, so the synthetic
drive STL of MCAM_MESH_STL gets a check model; with MCAM_HMM_DUMP the harness gets it as PREFIX.chk.stl and hmm_macro.py writes the 0x141f /
0x1458 chain). `MCAM_LK_DEBUG` prints the protect drop count ("protect FUN_1803a46d0 N"), `MCAM_SD_DEBUG` the rise branch ("sx ext / ce270 /
cf7b0 / rise" lines). Scratch (not committed): syn3.sh (syn2.sh + a check STL), lkcmp.py (the stay-down links link by link: endpoints, the first
connect's FUN_1801ce270 / FUN_1801cf7b0 calls point by point), sxcmp.py (the rise branch's calls: the extension drops from 18019a59f, the
extension connects from 18019a5fb, the mode-1 retry from 180199ff7).

### 24.1 The protect drop FUN_1803a46d0 (PORTED, gen_raster raster_zdrop_mm)
- FUN_1801cf7b0 without the +0x88 region path (x3 l.167-375; Raster: the plain path) = FUN_1801cf5e0 (x4 l.214-240): the layer drop FUN_1801cf500,
  then with a protect model (linker +0x7c0, its virtual +0xa0 = empty) FUN_1803a46d0 (x4 l.551-600): CToolDropPos (+0x58 = 1: no tall-cell
  pre-pass, every facet with the full profile - FUN_180223650 / FUN_180229ac0 / FUN_1801154a0 / FUN_180113220: the same maximum contact as the drive
  drop, hd_tip) of the protect profile (+0x17a0 at layer 0, +0x1860 at layer 1) on the model from z' - P (P = +0x1260 + 8 x layer); its result h
  gives z' = h + P when h is above that start, and the node keeps the highest: z = max(z, drive + ToolThicknessZ, h + P).
- The model (hl l.7560-7650): the EvaBoxSet of the plan behind the passes plan's key 0x1a9a, built by FUN_1802210c0 with the profile +0x17a0 - the
  Edit Passes to Surfaces 0x141f plan's ProtectedSurfaces = the check model c3_chk_open already builds for the check trimming.
- P = LinkerProtectThickness 0x1ec9 = LinkProtectThickness(MachiningPasses, ToolThicknessZ) (11.1): mckernel's passes plan for an op with checks is
  the Convert To Primary Output 0x1458 of the 0x141f plan with OutputID 0x1cb2, so P = that plan's OverThickness 0x1b51 = the check stock (record
  0x12a8, c3_chk_open's t).
- The profile (FUN_1803a3f50 hl l.9508-9540, dis 1803a3f50-1803a4125): +0x17a0 = the linker's tool +0x208 (23.3), +0x1860 = +0x2d0; when P -
  ToolThicknessZ != 0 both get FUN_1803a4130(profile, 0, P - ToolThicknessZ) (hl l.9547-9575): FUN_18013e330's offset (shaft and corner + d, the
  flat part unchanged; hst3d_grid.c l.3000-3060), left as it is when corner + d < 0.
- Checked call by call (MCAM_MESH_STL = the 45 deg valley, MCAM_CHK_STL = a flat strip at z 12 mm over x 8..16, y 30..45 = 23.5's case; RESTPASSES
  op 18's record, Use Z depths PosZHi 0.3 in): the first connect of every stay-down link equal to mchmm.dll, endpoints, every FUN_1801ce270 /
  FUN_1801cf7b0 call and its raised z (protect raises up to 9.9 mm): 176 / 176 links (1.4e-13 mm); check stock 0.02 in (P - Z = 0.508 mm, the grown
  profile) 180 / 180; Z stock 0.01 in, check stock 0 (d = -0.254 mm) 173 / 173. Flat floor 100 x 60 mm + a check strip over x 40..60 at z 30 / 55 mm
  (above the clearance 50.8): FUN_1803a46d0 4591 / 4591 and 4639 / 4639 calls (port / DLL), all 288 stay-down links and the retract across the
  strip equal (h = 56.0 mm = the strip + 1.0 above the clearance: FUN_1801990c0's calls 518 / 518 in order; the DLL's 36 further FUN_1801ce270 calls
  from 1801cec07 are FUN_1801cea60's node removal after the reduce, which cannot change +0x30), link_cmp 291 / 291 (0.0001: the arc chords).
- So gen_raster no longer keeps every link of an op with checks SAFE (23.5's rule removed): its links go through the same decoded paths as
  without checks.

### 24.2 The stay-down rise branch (FUN_180199220 hk l.6203-6560; PORTED except the extension loop)
- After the first connect, when its +0x38 is above SurfaceRampMaxVerticalChange: with the connect mode's +8 / +0x50 empty and neither pass's frag
  +0x78 (traced: the extension runs on every such link of the synthetic cases) the pass-extension loop: T0 = the previous pass's end tangent
  (FUN_18027ad40 x 1 / |T|), T1 = the next pass's start tangent x -1; ext from SurfaceRampMinPassExtension 0x1ef0 (default 0.1 x
  SurfaceRampMaxPassExtension) in steps of it up to SurfaceRampMaxPassExtension 0x1ef1 (default 0.5 x ToolCutterDiameter; hl l.13326-13345, not
  written by mckernel).
- FUN_18019a310 (hk l.6655-6830, dis 18019a310-18019a69f): Q = ext T + P; a mode-1 connect (FUN_1801d1c20, mode struct 1, base Z +0xa8 = min(P.z,
  Q.z) - the ctor's stack arguments at 18019a507-18019a535, which the decompiler drops) drops Q's XY from the base Z (FUN_1801cf7b0 at 18019a59a,
  |drop - base| into +0x38); when that is not above Q.z FUN_1801d2bd0 connects P -> Q and the extension is taken when the connect lifted no node
  (+0xc8, 18019a5fb); else ext + step (the step past the maximum is clamped and tried once more: 10 tries for D 6.35 mm).
- Side A (the previous pass) first; when it is taken, side B. When one is not taken (LAB_180199e80 -> LAB_180199eaa): a mode != 1 connect is retried
  as a mode-1 connect between the pass ends (FUN_1801d1e70 with FUN_180177a90(.., 1)); then FUN_1803a4400 and, the path length > 0, the connect is
  emitted when its +0x38 is not above SurfaceRampMaxVerticalChange, else FUN_1801a1d70 with a kind-2 link struct and flag 1 - the same call as
  FUN_180198ef0's retract between passes (hk l.5832-5838; the progress object FUN_1802e3b30 alike): c3_link.
- Ported (c3_staydown / c3_sd_ext / c3_sd_line1): the extension tries, the retry, a straight (2-node) retry emitted, a rising retry (and a kind-1
  rise whose extension is not taken) handed to the retract. Kept SAFE: both extensions taken (the loop's new connect between the extension ends,
  the emitted extension moves FUN_1801a54f0 tag 0x13d4, and the repeats - NOT DECODED), a retry that splits (its node path / wander, as kind 1).
- Checked (sxcmp.py): the check-strip valley 2361 / 2361 rise-branch calls equal (583 extension drops; 3.1e-12 mm), check stock 0.02 in 2681 / 2681,
  Z stock 2321 / 2321; 31 rises retried straight and 22 handed to the retract, as the DLL. The 80 deg valley (no check): its 4 rises take both
  extensions on the first try (6 / 6 calls equal up to there) and stay SAFE (the DLL then loops 10 times and retracts).
- link_cmp (port on the DLL's passes vs the DLL, 0.0001 in): check-strip valley 79 (24.1 alone) -> 110 / 140 links equal (24.3). The rest are retracts whose lead rises into the check (the DLL's vertical fallback leads FUN_1801d8630 after FUN_1801d8240 /
  FUN_1801d7b00 fail: 23.4, still SAFE) and the 80 deg valley's 4 loop links.

### 24.3 Measured
- PARALLELSTEEP op 5 / RESTPASSES op 18 (own passes and DLL passes): byte-identical to 23.6 (no rise, no check).
- Synthetic link_cmp before -> after this pass (port on the DLL's passes, links equal within 0.0001 in): check-strip valley 0 (all SAFE in 23.5) ->
  110 / 140; check stock 0.02 -> 111 / 144; Z stock 0.01 -> 158 / 183; flat + strip at 30 / 55 mm -> 288 / 291 + 3 within 0.0001 (arc chords).
- Regression: fileop 0 / 205 differ (vs the fifteenth pass); op_compare, the 29 Raster scenes: after 24.1 the 3 check scenes differ
  (s4_check_trim(_stock) .txt / .nc, s4_pick_check.txt: their links leave SAFE - s4_pick_check now = s4_pick_drive's 26977 points, its check
  touches nothing; s4_check_trim 19775 -> 20274 pts) - re-blessed (explained); after 24.2: 0 / 29 (no rise in the scenes).
- checktrim_test.sh on the new NC: all ok, stops 35 / 65 and 34 / 66.

### 24.4 Where M1 stops (sixteenth pass) - next
1. The lead rise branches (23.4 / 23.7.1): FUN_1801d8240 (dis 1801d8240-1801d862c: a bisection on a distance d from LinkVerticalLead*Radius x 0.5,
   step x 0.5 per round, at most 10000 rounds, stopping when the change is below +0x200; per round FUN_180171b50 (hst3d_chop.c l.2431, with
   FUN_180172020 / 1801725b0 / 180172950 / 1801747f0) then FUN_1801d8ce0 / FUN_1801d8b30 and, on failure, FUN_1801d87b0) and FUN_1801d7b00 (x8 l.219,
   FUN_180171b50 / FUN_1801744a0) before the decoded vertical fallback FUN_1801d8630. On the check-strip valley every such lead ends in the vertical
   fallback (the DLL's 12 + 12 retracts with the straight vertical leads): decoding when the two attempts fail would port them.
2. The extension loop when both sides are taken (24.2): the new connect between the extension ends (FUN_1801d1e70's tangents), the extension
   moves (FUN_1801186d0 / 180118ee0 / 180121900, FUN_1801a54f0 tag 0x13d4), the repeat rule (local_res8 / bVar3 at the maximum) - the 80 deg valley.
3. A retry / kind-1 link that splits (the wander FUN_1801deaf0 / FUN_1801dde40).

## 25. Seventeenth pass (2026-11-14, M1q): the stay-down extension loop PORTED; the lead attempts and the wander traced

Tools (testing, scratch: <scratch>): syn3.sh / syn4.sh (DIA / CR env) as 22 (a new case: valley2.py OUT W SLOPE H =
valley.py with the slope height H; cases in m2/syn), port.sh / dll.sh (the port on
the DLL's passes / mchmm.dll with HMM_TRACE on a kept case), sxcmp.py now also takes the loop's connect (FUN_1801d2bd0 from 180199e23) and the
mode-3 curve chords ("ce270c"). `MCAM_SD_DEBUG` prints the loop ("sx loop", "sx loop rise R ext EA EB"). Ghidra DecompAt (ghidra_hmm) wrote the
functions the earlier passes read only from the disassembly: FUN_1801d8240, 1801d8ce0, 1801d8b30, 180172020, 1801725b0, 180172950, 1801747f0,
1801744a0, 180173610, 1801775c0, 1801d8630, 1801dbf80, 1801dde40 (decomp/hmm_linker_x11.c, local, not committed; "x11" below).

### 25.1 The extension loop when both pass extensions are taken (FUN_180199220 hk l.6283-6497; PORTED, c3_staydown)
- Round 1 is 22.2's: side A (the previous pass's end along T0) then side B (the next pass's start along -T1), FUN_18019a310 each. Both taken: the
  side whose extension value equals SurfaceRampMaxPassExtension after its try (local_res8 for A, bVar3 for B; the value FUN_18019a310 leaves, the
  clamped last step included) is not extended again. Each round builds the extension frags - A = [pass end, Q_A], B = [Q_B, pass start]
  (FUN_180118ee0 + FUN_180121820 / FUN_180121900, emitted by FUN_1801a54f0 tag 0x13d4) - puts them into a copy of the connect mode (FUN_180177b90;
  local_4c8 / local_4c0 = the mode's +0x20 / +0x28) and builds a new connect with the arguments of FUN_1801a8fe0 (FUN_1801d1e70 hk l.6458-6470):
  in mode 3 its curve (FUN_1801d2110 x3 l.3100-3175) runs from frag A's end along its end tangent to frag B's start along its start tangent
  (FUN_18027ad40 of the 2-point frags, x 1 / |v|, x SurfaceRampTangentFactor: c1 = r v tf + Q_A, c2 = Q_B - r v tf); FUN_1801d2bd0(Q_A, Q_B)
  (180199e23). bVar23 = its +0x38 <= SurfaceRampMaxVerticalChange.
- The loop ends: bVar23; or both sides at the maximum (LAB_180199e80 with bVar23 as it is); or a further extension not taken (LAB_180199e80,
  bVar23 false). bVar23 -> local_c80 / local_c88 = the last extension frags, and (hk l.6537-6607, FUN_1803a4400 first, the path length > 0, +0x38
  not above the limit) FUN_1801a56d0 of the previous pass, of frag A, the connect frag (its first node, FUN_180192510 the rest) and frag B - so the
  toolpath runs pass end -> Q_A -> the connect's nodes -> Q_B -> pass start, all at the feed. Not bVar23 -> LAB_180199eaa with the loop's last
  connect in local_ca8: mode != 1 retries mode 1 between the pass ends (22.2), mode 1 goes to the retract (its +0x38 is above the limit).
- Ported as that; a mode-1 loop connect that splits keeps SAFE (the wander, 25.3).
- Checked call by call (sxcmp.py: every extension drop, every call of the extension connects, of the loop's connects and of the retry):
  80 deg valley v80 2432 / 2432 (max 1.6e-13 mm; the 4 links loop 10 rounds, then retry + retract), 75 deg (W 30) 1372 / 1372, 85 deg 5836 / 5836,
  89 deg 8532 / 8532, 70 / 75 deg 1372 / 1372 each; the loop's emission on valleys whose 80 deg slope ends 11 / 12 / 13.5 mm high (the extension
  reaches the top): hl 288 / 288 (4 links, 2 rounds), hm 904 / 904, hn 2400 / 2400 - 12 links emitted with their extensions. link_cmp (port on the
  DLL's passes vs the DLL, 0.00012 in): v80 131 -> 140 / 140, hl 136 / 136, hm 133 / 133, hn 82 / 82, 85 deg 137 / 137, 75 deg W 30 85 / 85, 89 deg 110 / 133 (its 23 others: approaches /
  retracts that c3_link refuses before the lead check - c3_lk_lead's measured-range limit of 87.73 deg on these near-vertical passes - SAFE as
  before this pass; the arc chords of 21.2 are the 0.0001 ones). The check-strip valleys unchanged (c45 2361 / 2361, c45s 2681 / 2681, c45z 2321 / 2321).
- PARALLELSTEEP op 5 / RESTPASSES op 18: byte-identical (no rise).

### 25.2 The lead's attempts before the vertical fallback (traced on c45; NOT PORTED -> SAFE, N13-29)
- On the check-strip valley the 26 retracts whose lead rises run this (HMM_TRACE 1801a1d70 / 1801d69f0 / 1801dab60 / 1801db250 / 1801d7450 /
  1801d76a0 / 1801d8240 / 1801d7b00 / 1801d8630): FUN_1801a1d70 calls FUN_1801d69f0 TWICE. First (from 1801a3911, hk l.12510-12530) with the lead
  attempts on: each lead's FUN_1801d7450 rises -> FUN_1801d87b0 -> FUN_1801d76a0(.., param_7 = 1) -> FUN_1801d8240 (x11 l.1-175, "FindBetterArc":
  a bisection on the overlap d from R / 2 (R = |T - C|), the step halved each round, at most 10000 rounds, stopping when the change is below
  connect +0x200; per round FUN_180171b50 (hst3d_chop.c l.2431: FUN_180172020 walks the connect's pass nodes - FUN_1801dbf80 put them in from
  1801db2e4 / 1801dabf0, over (float +0x248) x R - accumulating the path length, FUN_1801725b0 interpolates the tangent point, FUN_180172950 the
  arc), FUN_1801d8ce0 puts the arc's nodes in, FUN_1801d8b30 checks them, FUN_1801d87b0 removes them): 7 rounds, fails on all 50 leads; then
  FUN_1801d7b00 (x8 l.224-560: FUN_180171b50 with overlap 0) succeeds on all 50 and stores the trim (the walked length v: approach +0x1e8 = v, exit
  +0x1e0 = -v; traced values 0.11 .. 1.85 mm).
- Back in FUN_1801a1d70 (hk l.12735-12917, the connect is the local at -0x15e8, so local_1408 = +0x1e0, local_1400 = +0x1e8, local_13e0 /
  13d8 = +0x208 / +0x210): exit fits (bVar34) = frag +0x78, or |+0x1e0| <= LinkMaxAllowedTrimming (+0x1110; the macro's 1.27 mm = K+0x308) and
  < FUN_180279cc0 (the pass's length; the neighbour pass's when +0x208 is set); the approach likewise (cVar7) with +0x1e8. A side that does not
  fit -> FUN_1801d69f0 again (1801a3eb8, hk l.12880-12898) with param_6 / param_7 = the fit flags (both 0 also for plan kinds 2..4, +0x1058):
  FUN_1801d76a0 with param_7 = 0 makes no attempt; without the connect's +0x1ca / +0x1c9 flag and with no neighbour frag (+0x90 +0x50 / +0x48)
  it ends in FUN_1801d8630 (x11 l.2395-2480): the vertical lead. On c45 every second call had both leads vertical (100 FUN_1801d76a0 calls: 50 with
  the attempts, 50 straight to FUN_1801d8630).
- So the outcome needs the trim, i.e. the arc fitting: FUN_180171b50 with FUN_180172020 / 1801725b0 / 180172950 / 1801747f0 (~1000 lines of
  x11), FUN_1801dbf80 (~550), FUN_1801d8ce0 / 1801d8b30 (~300), FUN_1801d7b00's node build, and for a trim that fits the trimmed pass + arc lead
  (FUN_1801a44a0 / FUN_1801a56d0, hk l.12738-12745). NOT PORTED in this pass: those leads keep SAFE (labelled "lead blocked").

### 25.3 A split retry / kind-1 link: the wander FUN_1801dde40 (decompiled, NOT DECODED, no case)
- FUN_1801dde40 (x11 l.3028-3201) walks the node triples (skipping arc nodes 2 / 8 and removed 0x80) with FUN_1801e07f0, FUN_1801de920 /
  FUN_1801de3a0 (a counter pair, stops past 100 / 100) and ends with FUN_1801de1f0 - those four are not decompiled yet.
- No case: the "kind 1 split" counter is 0 on every synthetic case (c45 / c45s / c45z, v80, h*), on ops 5 / 18 and the op scenes, so there is
  nothing to check a port against; such links keep SAFE. (A note for finding one: a same-line gap made by the Z window over a bump splits only
  when the bump rises more than StepMachTolerance above the chord, but a ball's tip stays above a level over at least 2 sqrt(2 R e) - 0.58 mm for
  R 3.175 and e 0.0127 - which is more than LinkMaxStayOnSurface 0.508 of RESTPASSES op 18, so that link is a retract; a smaller tool lowers
  LinkMaxStayOnSurface with it. A split mode-1 RETRY (a rising mode-3 link whose straight retry rises by 0.0127 .. 0.0267 mm) is the likelier case.)

### 25.4 Measured (regression against a fresh master f8dacad build, rg = scratch)
- op_compare: master 47 / 636 differ, this branch 54 / 636 before re-blessing = master's 47 + 7 Raster files, and the normalised outputs of the
  two runs differ in exactly those 7: gen_raster.txt (BRACKET4: 16174 -> 16222 pts), s4_pick_drive.txt / s4_pick_check.txt (26977 -> 27039),
  s4_check_trim .txt / .nc (20274 -> 20336), s4_check_trim_stock .txt / .nc (19836 -> 19898) - their links that rise at the edges of the flat
  lofts (MCAM_SD_DEBUG on the scene: 6 rises, every one taken through the extension loop, 1-2.5 mm extensions, D 10 ball) were SAFE and are now the
  extended stay-down links. Checked on the scene's own mesh with mchmm.dll (the app with MCAM_HMM_DUMP, hmm_oracle on that dump, the app again
  with MCAM_HMM_PASSES): 419 / 419 rise-branch calls equal (3 extended links on the DLL's passes). Re-blessed those 7 (explained); after that
  47 / 636 = master's set. (The cumulative M1o-M1q changes vs master are these 7 files.)
- fileop (205 user ops, cam_cli): 0 / 205 differ from master (1 FAIL in both: the always-refused Area Mill op).
- checktrim_test.sh: all ok, stops 35 / 65 and 34 / 66 on the re-blessed NC.
- PARALLELSTEEP op 5 / RESTPASSES op 18: byte-identical to M1p (vs NCI max 0.0078 / 0.0063 in).

### 25.5 Where M1 stops (seventeenth pass) - next
1. The lead attempts (25.2): port FUN_1801dbf80 (the pass nodes in the lead connect) and FUN_180171b50 with its helpers first, checked call by call
   on c45 (HMM_TRACE 180172020:v / 1801725b0 / 180172950 give their in / out values), then FUN_1801d8ce0 / 1801d8b30 / FUN_1801d7b00's nodes, then
   the trim test and the second FUN_1801d69f0 call with FUN_1801d8630's vertical lead; a fitting trim (the trimmed pass + arc lead) after that.
2. A case for the wander (25.3): a same-line (kind 1) link whose mode-1 connect splits - e.g. a pass interrupted by the check strip on the same raster
   line - then FUN_1801e07f0 / 1801de920 / 1801de3a0 / 1801de1f0.
## 26. Eighteenth pass (2026-11-14, M1q): the Waterline surface ramp DECODED + PORTED; the Waterline retract mapped

Branch worktree-agent-a19e9e70bbe455091 (merges of worktree-agent-a819e97273f4ddcb9 = sections 21 / 22, master f8dacad, worktree-agent-ade6ca1381ef88939
= sections 23 / 24, renumbered from 21 / 22). New dumps (gitignored): decomp/hmm_linker_x12.c (FUN_18027a300, 18027ca10, 18027f4e0, 180280eb0,
18027a4b0, 18027d4e0, 180281580, 1802833d0), decomp/hmm_linker_x13.c (FUN_1801e0d70, 1801e0cc0, 1801de630, 1801dd130). Tools: HMM_TRACE `:p` (eight
doubles at the 5th argument's pointer, e.g. the CToolDropPos profile of FUN_180222920); tools/hmm_oracle.py HMM_PASS_CHORDS=N (arcs of the DLL's
passes as N chords in PREFIX.passes / .mmpasses; 1024 for the ramp comparison below: the ramp's SurfaceRampMaxDeviation test is 0.0005 mm, finer
than the sag of 16 chords); link_cmp.py finds closed passes through a grid (4 min -> 3 s on op 4); cam_cli MCAM_WL_NOLINKS=1 (testing: the
links of this pass off, the SAFE rule as before), MCAM_WL_DROPPROBE=FILE (+ MCAM_WL_PROF) prints the linker's drop at given XY.

### 26.1 The DLL's Waterline links on SWARF op 4 (traced: FUN_180198f00 62 calls)
- 22 stay-down links (FUN_180199220, kind 3 = surface ramp, every one between two closed passes but the first, from the open pass 14 at
  1.6066 in), 40 retracts FUN_1801a1d70 (each: FUN_1801a1240 twice (the horizontal lead sides at the pass end and start), FUN_18019e010 once
  (both vertical leads, FUN_18019a6a0 twice on the upper levels), FUN_1801990c0 once (the height)), the first approach FUN_180194140 and the
  exit FUN_180197b80 (FUN_18019e010 each). FUN_18019a6a0 315 calls, FUN_1801a1240 246 calls in all (the lower levels retry). No kind 4 / 5.
  (link_cmp's "stay-down 19" of 22.2b counted ramps it could not separate from the arc-fitted passes.)
- Shapes (tag 2): upper levels (open passes): exit = horizontal lead-out arc R 1.905 mm (HorizontalLeadOutRadius) sweeping up to 150 deg,
  vertical lead-out arc R 1.905, + LinkRampHeightOffset 0.635 (type 3), rapid to 101.6, across, down to the lead-in top + 0.635, plunge to the
  top, vertical lead-in arc (R 1.905, ending at MaxRampAngle 10 deg), then a helix of radius HorizontalLeadInRadius 3.01625 descending at 10 deg
  (3 - 4 arcs of ~115 deg, flag 1 in the arc record) into the pass start, tangent to it. Lower levels (the bottom pockets): exit straight up 1.905
  + 0.635 (the blocked lead-out's vertical fallback), approach: vertical lead-in arc then a straight 10 deg ramp (17.0 mm for 3.0 mm) or short
  helix pieces. FUN_1801a1240 (hk l.10822-11350) decides the lead side for Waterline (plan kind 0 is in the mask 0x9d) from the pass's
  CTessToolpathMill grid data (GridC entries 0x1e / 0x1f) - NOT DECODED; the helix and the lead retries are in FUN_18019e010 / 18019bba0 /
  18019c7c0 / 18019d5d0 / 18019db50 - NOT DECODED. So the Waterline retracts keep the SAFE rule (N13-29).

### 26.2 The surface ramp (DECODED, PORTED: cam3d_finish.c wl_ramp_link / wl_ramp_pt / wl_wander)
- FUN_1801c3dd0 (x1 l.1919-2330), plan kind 0, SurfaceRampStyle 1, the next pass N closed: d = |zN - zP| / sqrt((1 - c^2) / c^2), c =
  cos(SurfaceRampAngle) (+0x1210); N is re-split d further along itself (FUN_180279f90 + FUN_180281580) and the link struct gets the pieces
  +8 = P's extension (P from its end d further, FUN_1802829c0; a closed P wraps) and +0x10 = N's ramp piece (from S, d back from N's new start,
  to that start) - the whole-frag branches (d >= a length) are not ported (SAFE). A P that is open (the first ramp of op 4) takes another branch
  (FUN_180015d30 / FUN_18027b580: the toolpath's closed parent) - NOT PORTED, SAFE.
- The connect (FUN_180199220 -> FUN_1801a8fe0 -> FUN_1801d1e70 mode 3 -> FUN_1801d2110 x3 l.2830-3200) keeps the pieces at +0x118 / +0x128;
  its curve FUN_1801dd430 (hlc l.32-180): A(t) / B(t) = the point at t x length along each piece (FUN_18027a300 decomp/hmm_linker_x12.c l.1-80:
  the ends at 0 / 1, else FUN_180279f90 by t L), s = t^2 x 2 (1.5 - t) with the connect's +0x68 = LinkSurfaceRampSmooth (Transitions
  Tangential), the point = A (1 - s) + B s. The reduce is the Raster one (c3_sd_split mode 3) on this curve, with the Waterline defaults
  SurfaceRampMaxDeviation 0.0005 mm and SurfaceRampMaxVerticalChange 1e6 (hl l.18940-18975); the step block and the linker's tool profile are the
  Raster formulas - measured on op 4 (HMM_TRACE 180252d90:R and 180222920:p): 0.003 / 0.0127 / 0.879515 / 2 mm, profile 7.616952 / 4.441952 / 3.175.
- Both frags closed set the connect's wander flag +0xa0 (FUN_1801d2110), so after the reduce FUN_1801d2bd0 (x1 l.1282-1290) runs FUN_1801deaf0
  (0 for 5 or more plain nodes) and FUN_1801dde40 (x3 l.1209-1380): over node triples FUN_1801e07f0 / FUN_1801de920 (x5) flag the nodes below
  the line of their kept neighbours (XY-length weights) and step back; FUN_1801de1f0 puts each flagged run back on that line (FUN_1801e0d70,
  x13 l.1-88, raising only) - the nodes' upper hull in (XY length, Z). Traced: node t = 0.5 of op 4's 4th ramp goes 35.1109 -> 35.1899 between
  FUN_1801deaf0's return and FUN_1801cea60 (no drop lifted it: CToolDropPos returned its start).
- The same flag makes FUN_1801cd970 call the connect's virtual +0x28 = FUN_1801d4990 (hlc l.312-560, the node wander: two drops at
  LinkWanderInitialOffset 0.01397 mm to either side along FUN_1801d4840's direction, a bisection for the lower, the offset kept at node +0x48 and
  compared / undone after the reduce by FUN_1801d3a60 / 1801d4270 / 1801d3c20 / 1801d39e0) for every new node whose drop lifted it - traced
  (138 drops in op 4's 4th link, from z 9.19 = the wander's start at min(+0x80, z)). NOT PORTED: a ramp whose drop lifts a node keeps SAFE.
- Checked node by node (MCAM_SD_DEBUG "sd node" vs HMM_TRACE 180199220,1801d2bd0:l, the port on the DLL's passes with 1024 chords per arc):
  SWARF op 4: 22 DLL ramps; the port emits 7, all 7 = the DLL's node lists (same t, 1e-5 mm: the arcs' chords); 14 keep SAFE (a lifted node:
  the wander) and 1 (open P). link_cmp (DLL output arc-fitted by its writer, 0.0127 mm): those 7 within 0.0003 in.
- The DLL's output stage merges collinear moves and fits arcs (the writer FUN_1801186d0, tolerance 0.0127 mm / cos 45 deg): NOT PORTED, the
  ramp's nodes are written as they are (a node count difference only).

### 26.3 Measured (the port's own passes; MCAM_WL_NOLINKS=1 = before; seg_dist / link_stats vs the stored NCI, inch / mm)
| op | ramps emitted (SAFE) | retracts before -> after (ref) | rapid XY before -> after (ref) | ours->ref p95 / > 0.005 before -> after | ref->ours p95 |
|---|---|---|---|---|---|
| TUTORIAL 2 SWARF op 4 | 18 (3) | 71 -> 53 (42) | 67.9 -> 66.4 (51.2) | 0.0041 / 118 -> 0.0041 / 240 | 0.0988 -> 0.1017 |
| MOLD BOTTLE op 4 | 13 (8) | 118 -> 105 (71) | 102.4 -> 101.5 (84.2) | 0.0057 / 9365 -> 0.0057 / 9435 (max 0.0318 =) | 0.1180 = |
| SWARF_METRIC op 3 (mm, checks: old order) | 0 | 57 = (39) | 945.5 = (481.2) | unchanged | unchanged |
The vertices over 0.005 in that the ramps add sit on links where Mastercam's ramp starts elsewhere (the port's passes and levels are not the
DLL's: 22.1, 22.2c), not on a different ramp rule (on the DLL's passes the emitted ramps are the DLL's).
Regression (full build of this branch): op_compare 48 / 636 vs master's run rg_new15 47 / 637 - the same scene list plus gen_waterline.txt
(BRACKET4's Waterline op 5070 -> 5503 points: its ramps' nodes), re-blessed (explained); fileop 0 / 205 differ from rg_new15 (no user op is a
Waterline op).
Pass counts (unchanged this pass): op 4 70 vs the DLL's 65, MOLD BOTTLE 117 vs 98 (the contact-angle split, 22.2c; step 2 of the task not
started: the weave contact word of FUN_18040b6f0's input).

### 26.4 Where M1 stops (seventeenth pass) - next, in order
1. The node wander FUN_1801d4990 + FUN_1801d4840 and the mode-3 post-checks FUN_1801d3a60 / 1801d4270 / 1801d3c20 / 1801d39e0 (x3 l.732-1064,
   x2 l.248; fresh dumps decomp/hmm_linker_x14.c): the other 14 ramps of op 4. Read so far: FUN_1801d4840 (x14 l.1-37) = the unit XY normal
   (-dy, dx) of the curve chord C(t + 0.001) - C(t - 0.001) at the node's float t (+0x30; t - 0.001 <= 0 and t + 0.001 >= 1 clamp to the ends);
   FUN_1801d4990 (x14 l.38-457 = hlc l.312-560): returns when the dropped node equals the curve point; the direction = the sum of both
   neighbours' wander offsets (+0x48 / +0x50) when both wandered (+0x44 bit 0), else FUN_1801d4840's, else the normal of (a - b); two points at
   +-LinkWanderInitialOffset (+0xb8; default max(0.006, 1.1 StepMachTolerance) = 0.01397 mm on op 4) from Z min(connect +0x80, curve Z) dropped
   (FUN_1801cf7b0); kept as it is when the node is below both; else a bisection between them (the side whose segment from the middle is nearer
   the curve point moves to the middle, FUN_18005ab30; the middle at the XY mean and the mean Z, dropped) while their 3D gap^2 > the step block's
   +8; the lower end becomes the node, its offset from the original stored (+0x48.., the wandered bit). +0x80 = 9.19 mm on op 4 (the lowest
   level, 0.3618 in): its writer is not read yet. FUN_1801d3a60 = the mean distance of the nodes to their curve points with and without the
   offsets; FUN_1801d3c20 / 1801d39e0 the comparison and the undo - not read to the end.
2. The open -> closed ramp (FUN_1801c3dd0's FUN_180015d30 / FUN_18027b580 branch).
3. The retract FUN_1801a1d70 with Waterline's horizontal leads: FUN_1801a1240's side from the toolpath grid, the helix lead-in and the lead
   retries of FUN_18019e010 (18019bba0 / 18019c7c0 / 18019d5d0 / 18019db50), the lead test FUN_18019a6a0 (x2 l.1-88, its arguments 13.5).
4. DONE (28): the weave contact word into FUN_18040b6f0 and the window's degrees - MOLD BOTTLE 98 / 98; op 4 (bull tool) needs the Voronoi offset (28.7).
5. Style 1's +1.0 (SWARF_METRIC op 3; FUN_180229ac0's +0x58 drop, 12.1) and op 3's trimmed frags' order (21.3) - op 3 keeps the old order.
## 27. The HST drive mesh (2026-11-14, N13-30 / N13-33) - what Mastercam hands mchmm, ported for surfaces behind MCAM_HST_MESH

### 27.1 Which code makes the mesh (DECODED; ms = decomp/mcmsurf_all.c, ct = decomp/partline/core_tess.c, msm = decomp/cscallop/msm*.c)
mckernel does not tessellate. Its macro's Database Loader 0x1421 reads the STL `tmphardmillraw.stl` (FileName, FUN_18006eac0; hmm_macro.py
head_plans); Detect Top Surfaces 0x1423 and Surface Tessellation 0x1424 (Tolerance = job +0xa0) are mchmm plans that run on that STL inside
the DLL (the harness already runs them). The STL is written by **mcmsurf**:
- bnci_surface (ms l.12265): op 0x84 sets msurf_use_emm = 1, msurf_use_3rd_party_algorithm = 1. FUN_18012a310 (ms l.197248) therefore takes
  the EMM arm: **FUN_180224190** (ms l.360106) = FUN_180229ba0(.., param_4 = 1, chord = msurf_chord_dev, d = 0, h = 0, c = 0, .., r = 1, ..)
  instead of build_machine_model. msurf_chord_dev = op +0x1238 = record 0x12a0 for op 132 (msurf_map ms l.340012).
- **FUN_180229ba0** (CMutlMachModel::local_build_machine_model, ms l.363767): per TP kind 4 reference (tp_get_all order) of class 0x100
  (surface: local_8284 = 1), 0x40000 (solid: 3), 0x100000 (mesh: 4), 0x10000 with msurf_use_alt_solid_proc (5); a surface goes to **make_surf_model** (mccore 18058dc30 -> FUN_18057a980) with d = h = 0, tol = in_stack_88 = in_stack_90 =
  chord x 0.9, r = 1, the frame of FUN_180229ba0 (the active view turned by cos / sin(pi / 2)); param_4 = 1 skips check_for_ridge and sets
  mms +0x68 = 2 (ms l.364684, l.364764). Solids: make_stl_model_from_sld_face (Parasolid faceting, triangles to tmpstlc.tmp); meshes:
  makePmeshStlModel (tmpstlc.tmp). The models go to tmpmm / tmpmm1 / tmpmm2.
- **FUN_180258310** (ms l.395467), called by process_3rd_party_algorithm FUN_1800199d0 (ms l.18215) with param_1 = 0 when record 0x172c
  != 0 (checks left to the kernel) else 2: per tmpmm2 record with mms +0x68 == 2 and (param_1 == 2 or mms +0x66 == param_1):
  surfaces: setup_mach_model_buffer / **fill_mach_model_buffer** (mccore 180598790 / 18057e9c0) in the active view turned by cos / sin(pi / 2)
  (ms l.395718-395741: for Top the rows (1, -c, -0), (c, 1, 0), (0, 0, 1)); a model whose frame box has |zmax - zmin| < chord x 0.9 has every
  point's z set to zmin (ms l.395789, l.395920); solids / meshes: the triangles of tmpstlc.tmp at the offset mms +0x44. Per triangle: style 4 (Horizontal
  Area) with XY stock < Z stock raises a flat triangle (corner z within record 0x12a0 x 0.01) by (float)(Z - XY stock) (ms l.395652, l.395939);
  x 25.4 for inch parts; FUN_1802284c0 (ms l.362853) writes the corners as floats with the facet normal; the 2-byte attribute = the surface
  ordinal | 0x8000 (none for style 5 with a contact angle strictly inside (tol, pi / 2 - tol), ms l.395613-395637). Binary STL with the
  header "MASTERCAM STL DATA" and the count rewritten at the end. The lowest / highest corner z (before x 25.4) -> hstJobIdDat +0x28 / +0x30
  (PosZLo / PosZHi, hst3d_port_plan.md 6.7). FUN_1802342f0 / FUN_18024d740 keep a tmprawi.tmp cache (style, shift, z range) - not a geometry step.

### 27.2 Ported (DLL-matched)
- **make_surf_model on trimmed surfaces** (kind 10 over a kind 0 base) with d, h < tol: viewer/mcpartline_mesh.c `model_trim` (the arm
  make_surf_polygons takes on kind 10 - plm_mesh's trim engine - with in_stack_90 = tol, the model's step pass, the vertex pass with normals and
  the box: grid points tested against the loops (param_12 = 0, asm 18057c9e6), loop points not (param_12 = 1, asm 18057cb79), mms +0x50 = the
  cell loop's triangle count, the boundary edges of the ears). FUN_18057dd40(0, ..) on the trim loops (only when tol <= d) stays NOT PORTED
  (refused). The loop-point box rule also corrected in plm_model's outline-loop path (the same asm; no oracle change).
- **fill_mach_model_buffer on trimmed models**: plm_model_tris clips every cell when the surface is trimmed (FUN_18057ebd0 msm1.c l.479: kind
  byte 10 or a border cell), outer loop = mms +0x60; mm_fill evaluates the base; mm_record / mm_mm1_bytes write the loop table / +0x60.
- **FUN_180258310 per surface**: viewer/cam3d_mmodel.c `mm_hst_surface` (frame, flatten, style-4 raise, scaling, float corners, z range);
  viewer/cam3d_rough.c c3_mesh_refs uses it for an HST op's drive surfaces (merged checks included) when **MCAM_HST_MESH=1** (testing; the
  default is unchanged); the triangles carry FUN_180258310's attribute, and MCAM_HMM_DUMP then writes PREFIX.stl as the binary
  tmphardmillraw.stl (FUN_1802284c0's normal). MCAM_HST_MESH=2 (diagnostic only, ours): a surface the port refuses keeps mcs_mesh - a mixed
  mesh, NOT Mastercam's (used below for PARALLELSTEEP op 5, whose drive holds one kind 1 surface).
- Oracle: `tools/peloader mccore.dll --msm` (MSM: mccore's own make_surf_model; MMB: its setup_ / fill_mach_model_buffer on the port's
  tmpmm files) with `tools/msm_case.py --all PART --d 0 --kinds 0,10` (d = h = 0 = the HST call), tools/tests/msm_cmp.py / mmb_cmp.py.
  TUTORIAL 2 SWARF_METRIC (49 kind 0 + 45 kind 10 surfaces, chord 0.0254 mm): make_surf_model **94 / 94 identical** (u / v, loops, counts,
  box, 3881 edge records, every vertex / normal), fill_mach_model_buffer **94 / 94 identical** (triangles, points, normals). Corpus (every 10th corpus part, 136 parts, kinds 0 and 10-over-0, d = 0,
  chord 0.001 in / 0.0254 mm, at most 30 a part): **919 surfaces identical, 0 different, 1 refused** (ROUGHPROJNCI: trim repair FUN_180586a50);
  fill_mach_model_buffer on 5 of those parts (237 surfaces) identical. No licence code is reached (the --msm run stops on any licence import; none fired).

### 27.3 Measured (MCAM_HST_MESH off = master's mesh (mcs_mesh / OCCT) / on; tools: oracle3d.py "cut" line, seg_dist.py vertex-to-path,
the DLL harness = MCAM_HMM_DUMP + tools/hmm_oracle.py on the op's mesh; scratch script measure.sh, not committed)
The harness STL with the switch on is the binary tmphardmillraw.stl (attributes included); off it is the ASCII dump as before.
| op | port off / on (cut ref->ours p95, ours->ref p95, length ref / ours) | DLL harness off / on vs stored NCI (seg_dist p50 / p95 both ways) | DLL levels |
|---|---|---|---|
| SWARF_METRIC 3 (mm, Corner rounding off for the port) | 0.464, 0.051, 10239 / 9972 -> 0.457, 0.052, 9974 | ref->dll p50 0.0492 -> **0.0065**, dll->ref p50 0.0450 -> **0.0040** (p95 capped 0.5 both: the lowest levels' rings, 18.5) | off: 40.828 39.629 38.429 37.238 ... 13.33, 12.368 11.486 11.401 10.436 9.469 8.486 (pushed up to +0.1); **on: 40.826 - 1.2 k down to 13.238 = the stored levels exactly**, then 12.05 10.863 9.675 8.486 (stored 12.054 10.874 9.681 8.489; the stored op has Corner rounding on) |
| SWARF 4 (inch, angles 50..90) | 0.087, 0.002, 425 / 404 -> 0.066, **0.172**, 441 | 0.0025 / 0.0133 -> **0.0002 / 0.0065**, 0.0025 / 0.0142 -> **0.0002 / 0.0012** | off: from 1.607 down every level +0.001 (1.607 1.557 ... 0.412 0.362); **on: 1.806 1.756 ... 0.407 0.357 = stored** (plus 1.924 / 1.874 / ... upper levels in both runs, not in the NCI) |
| MOLD BOTTLE 4 (inch, angles 50..90) | 0.194, 0.002, 603 / 541 -> 0.194, 0.003, 541 | 0.0037 / 0.0128 -> **0.0003 / 0.0050**, 0.0038 / 0.0239 -> **0.0003 / 0.0057** | off: 1.83 1.808 1.785 ... below 1.62 every level about +0.001 (1.569 1.532 ... 0.375 0.338); **on: 1.868 1.831 1.793 ... 0.369 0.332 = stored** (plus extra upper / in-between levels 2.023 ... 1.62, 0.585, 0.533 in both) |
| PARALLELSTEEP 5 (Raster; MCAM_HST_MESH=2: its kind 1 surface keeps mcs_mesh) | ref->ours p95 0.0006 -> 0.0007, ours->ref 0.0003 / 0.0003 | 0.0000 / 0.0003 / max 0.0044 both (unchanged) | - |
| RESTPASSES 18 (Raster) | ref->ours p95 0.0009 -> 0.0011, ours->ref 0.0004 -> 0.0007 | ref->dll p50 / p95 / max 0.0002 / 0.0005 / 0.0019 -> 0.0001 / 0.0005 / 0.0029, dll->ref 0.0001 / 0.0005 / 0.0017 -> 0.0001 / 0.0004 / 0.0021 | - |
Reading: on Mastercam's own mesh the DLL's Waterline levels are the stored ones (the StepDownInflexion* pushes of 18.5 came from our mesh), and
the DLL's Waterline paths come to 0.0002-0.0003 in (p50) of the stored NCI. The port's own paths barely move: its Waterline levels use only
the flat-facet part of CZInflexions (the vertex / edge steps FUN_1804a2040 / FUN_1804a20b0 are not applied in c3_infl_level), and its
contact-angle filter takes the normal from the mesh (ours), which makes SWARF op 4 worse with this mesh (ours->ref p95 0.002 -> 0.172).

### 27.4 NOT DECODED / NOT PORTED, and where this stopped
(2026-11-14, N13-33: kinds 1 and 8 ported, the view / the check model / meshes / solids decoded further - see 27.5 and 27.6; the list below is the N13-30 state.)
- **Solids**: FUN_180229ba0 meshes a solid drive with make_stl_model_from_sld_face = Parasolid's faceter (PK_TOPOL_render_facet, interfacial.md
  12.3) into tmpstlc.tmp; FUN_180258310 copies those triangles. Third-party kernel: NOT DECODED; with MCAM_HST_MESH a solid drive is refused
  (no OCCT stand-in, N12-1). Feasible part: everything after the faceter (the copy, flat raise, scaling) is the same code as for surfaces.
- Polygon meshes (makePmeshStlModel), msurf_use_alt_solid_proc faces (code 5): not ported (refused).
- make_surf_model on kinds other than 0 / 10-over-0 (corpus: kind 1 in PARALLELSTEEP op 5's drive; kinds 1 / 2 / 4 / 6 / 7 / 8 need their
  bit-exact evaluators, interfacial.md 12.4), on trimmed surfaces needing the trim repair FUN_180586a50, and FUN_18057dd40(0, ..) (tol <= d).
- The active view: FUN_180258310 / FUN_180229ba0 use the active view (GetActiveViewNo); the port uses Top (identity). Which view is active
  during an op's regeneration is NOT DECODED (all ops measured are Top).
- The arena: setup_mach_model_buffer's block sizes come from the licence-sized alloc_nc_all buffer (N13-23); the port joins the blocks
  (one block), as the DLL oracle shows the result does not depend on them for the triangles / points compared.
- The check model for kernel trimming (TP_REF_CHECK, mckernel's tmpmn5.emm FUN_18006ea44): its writer is not traced; it keeps mcs_mesh.
- Not done: the port's own inflexion vertex / edge steps (another agent / N13-30's first half), and default on (the user decides).
- Regression (default off): op_compare 47 / 635 = the base's known set (2D .mcamui scenes; no 3D scene differs); fileop 0 / 205 against master's cam_cli (0d2e471; NAKTO-BRAKE-BRACKET op 3 refused by both with the same message, the .m differs only by appended FAIL lines).


### 27.5 Fifteenth pass (2026-11-14, N13-33): the remaining surface kinds, the frame's view, the check model, meshes and solids
User decision N13-33: MCAM_HST_MESH stays off by default until the remaining kinds are ported. Corpus survey first (every op 132 of the 908
corpus .mcam parts, TP kind 4 references by entity class / surface kind; scratch script): **the HST drives of the corpus hold only kind 0, kind
10 over kind 0, kind 1 (PARALLELSTEEP / STEEPSHALLOW op 5, 2017 and 2018 copies) and solids (10 ops: Benchmark 3_0 2 / 4 / 6 / 7, RASTER 2 / 4,
HORIZONTAL 4, 2017 and 2018 copies)**; no kind 2 / 4 / 6 / 7 / 8 surface and no polygon mesh is an HST drive. Visible surfaces of those kinds
in the whole corpus: kind 1 in 17 parts (10 over 1: 14), 2: 3 (10 over 2: 4), 6: 2 (10 over 6: 2), 8: 2 (10 over 8: 6), **4 and 7: none**.

**Kind 1 (parametric) - PORTED, DLL-matched.** eval_surf case 1 bit for bit (viewer/mcpartline_mesh.c pp_eval): order 0 = FUN_1805eb9c0
(evalsurf.c l.1124: the u patch index through a short; the patch's coefficients read backwards from its last double, per v row Horner in u,
the rows Horner in v), orders 1-3 = FUN_1805ec000 (l.79) on eval_poly_deriv 0x1805eb630 (l.4054: Horner with the derivative terms
e[j] = t e[j] + e[j - 1] for j = min(deg - k, nder) .. 1, then e[0] = t e[0] + c[k], then e[j] x j!), per v row in u, then per u derivative
over the rows in v; param_6 = 0 on every make_surf_model / calc_surf_norm call (core_tess.c / msm*.c eval_surf call sites), so no scaling by
nu / nv. get_evaluation_grid case 1 (core_tess.c l.3195): the patch counts D+0 / D+4 (at least 2), no knot vectors (the caller's NULLs,
msm1.c l.1053) - uniform spans. Found on the way (kind 10 over kind 1): **calc_surf_norm reads the domain (+8..+0x20) and the size (+0x70)
of the surface it is given** (evalsurf.c l.5071-5110), and FUN_18057a510 / fill's FUN_18057f490 give it the entity itself (local_2d00, the
trimmed surface: its own +0x70.. = the loops' box), while eval_surf follows a kind 10 to its base: the Taylor walk of a degenerate normal
steps 1/100 of the trimmed surface's box (plm_unit_norm_h; 12 normals of SWARF 5 AXIS WALLS CHAINS surface 59 changed by it; no kind 0 / 10
case of the corpus samples reaches the walk with different boxes).

**Kind 8 (offset) - PORTED, DLL-matched.** find_base_surface (0x1805819f0) / find_base_surf (0x1806f75b0): through kind 8 (offset ent
+0x118 summed, base eptr +0x120) and kind 10 (base +0x100); make_surf_model takes the grid of that base (local_2c08) and the offset local_2b78
into the kind 8 copy FUN_180581f00 steps on (+-local_188 + local_2b78); eval_surf case 8 = FUN_180460cd0 in full (l.1911: base from D+0x20,
find_base_surf from there, distance = that sum + D+0x18) for the vertex pass; calc_surf_norm on the kind 8 entity (its own header) walks with
eval_surf case 8 order 3 (base third derivatives copied). The base evaluation inside FUN_180460cd0 now uses the bit-exact kind 0 port
(plm_eval / plm_norm) instead of viewer/mcsurf_nrm.c, whose last bits differ - invisible in the step counts (the d != 0 corpus check below
stays identical), visible in a kind 8 surface's vertices. Refused: a kind 8 surface with tol <= d or h (the side test / outline refinement
on it, not needed by the HST call). The oracle images kind 8 (tools/peloader/mccore_stl.c mk_image: fixed 0x130 bytes, +0x120 -> the base
image).

**Kinds 2, 4, 6, 7 - NOT PORTED (no HST drive of the corpus holds one).** Each needs its eval_surf case bit for bit with derivatives to order
3 and the curve evaluator they all call: kind 2 eval_revsurf (0x18047a110, evalsurf.c l.3287, 640 lines: eval_curve of the profile D+0x48),
kind 4 FUN_18042cad0 (l.2737, 550 lines; no visible kind 4 surface in the corpus, so no DLL case), kind 6 FUN_180412490 (l.329, 800 lines:
eval_curve of D+0x20), kind 7 FUN_18041c730 (l.1311: three eval_curve calls, FUN_1803fca60 / FUN_18041ced0; no visible kind 7 in the
corpus); eval_curve (0x1803fcd10, l.4311) for points, lines, arcs in their view, parametric splines (eval_spline / eval_poly_deriv) and
NURBS curves (nurb_interval / FUN_1805aeb50), each with derivatives; get_evaluation_grid's cases 2 / 6 / 7 (the curve's kind: line / arc 2 x
2, a spline its segment count x 2, a NURBS curve its spans) and 4 (FUN_180581a90). The oracle would also need curve and view images
(as tools/peloader/mccore_csurf.c makes for Curve Surface). make_surf_model refuses them ("NOT PORTED").

**The trim repair FUN_180586a50 - NOT PORTED.** FUN_180591650 (core_tess.c l.2102) calls it when FUN_180598070 finds crossing loops, up to 30
times (l.2440-2454: sVar13 counts down, Esc tested), each try followed by FUN_180598070 again; 1215 decompiled lines (l.6729-7944) plus
FUN_18058a7c0 / FUN_180592ce0 / FUN_18000f270, the arguments to be read from the disassembly as for the rest of the trim engine. Corpus cases
(kind 0 / 10 sample): TUTORIAL 9 (2017 METRIC and inch) surfaces 113 / 114, ROUGHPROJNCI 113 - none is an HST drive. Trimmed surfaces with
tol <= d (FUN_18057dd40(0, ..) on the trim loops) are not reached by the HST call (d = 0) and stay refused.

**Polygon meshes (class 0x100000) - decoded, NOT PORTED (no corpus HST op has one).** FUN_180229ba0 case 4 (mcmsurf l.364660-364681):
makePmeshStlModel (mccore 0x18058bad0, decomp/hstmesh/pmesh_sld.c) with the frame, tol = chord x 0.9, sVar19 = 1 (0 for a multiaxis op or op
0x6b with +0x13e4 bit 8), the tmpstlc.tmp SysFile: mesh kind (ent +0x68 i16) 0 -> pl2CopyPmeshMMFile (the mesh's MM file copied to
tmpPolyMesh.stl) and the ent's matrix +0x78..+0x128 kept; kind 2 -> FUN_1805906c0 writes the STL from the entity data, identity matrix;
other kinds: nothing. Then make_stl_model (0x18058ce30, decomp/hstmesh/stlmodel.c l.1298) on a class 0x40000 (STL) entity built on the
stack: FUN_1803df720 (l.2367, 1540 lines: reads the STL, through the matrix and rot_view into the frame, appends the triangles to tmpstlc.tmp
after a 4-byte count) and the record: box mms +8..+0x30, +0x50 = 1, +0x58 = 3 (STL model), +0x64 = 1, +0x68 = 2 (sVar19 != 0: the record
FUN_180258310 takes), +0x6c = -1, +0x44 = the tmpstlc.tmp offset. FUN_180258310 (l.395823-395960) reads at that offset the count and 0x48
bytes per triangle (3 x 3 doubles, the frame's coordinates, model units) and treats them as a surface's triangles (flat raise, x 25.4, float
STL; attribute: the surface counter as for a surface). To port: FUN_1803df720 (STL ASCII / binary reader + matrix), FUN_1805906c0 and the MM
file format of pl2CopyPmeshMMFile.

**Solids (class 0x10000 / faces 0x40000 with msurf_use_alt_solid_proc) - decoded as far as the third-party kernel, refused.**
make_stl_model_from_sld_face (mccore 0x18058d510, decomp/hstmesh/pmesh_sld.c l.171): MachModelSolidProcHelper::PrepareForFaceProcessing,
then FUN_1803e2460(face list, tol = chord x 0.9, tmpstlc.tmp) (stlmodel.c l.3): count placeholder written, frame rows copied, then
**ps_face_to_mach_model**(faces, kind, tol, file, frame, ..) - mcgeomsld's Parasolid facet call (PK_TOPOL_render_facet, interfacial.md 12.3;
pskernel, third-party, NOT DECODED, cannot run here). Input: the solid's (or the selected faces') Parasolid body, the facet tolerance chord x
0.9 (model units; Parasolid's own option block as interfacial.md 12.3: tol / 39.37 or / 1000 in metres), the frame (the active Cplane rows
turned by cos / sin(pi / 2)). Output: tmpstlc.tmp = int count (back-patched) + per triangle 3 corners x (x, y, z) doubles in the frame, model
units, and the model record (box, +0x58 = 3, +0x54 = count, +0x68 = 1 or 2 as sVar19, +0x44 = offset) - the same record and file FUN_180258310
copies for meshes. Everything after the faceter (the copy, flat raise, scaling, attribute) is the surface code already ported. With
MCAM_HST_MESH on a solid drive is refused (no OCCT stand-in, N12-1); the 10 corpus solid-drive HST ops are refused.

**The frame's view (item 5) - DECODED.** GetActiveViewNo(Planes, 0) = the active Cplane's view (PlaneType 0 = Cplane, formats/
bbox_toolplane.md). bnci_surface (mcmsurf l.11916-11919) makes the op's planes active for the whole regeneration: ActivePlaneSaveRestore
(Planes, 0, op_view op +0x620 = record 0x688, false) and (Planes, 1, op +0x570), restored at l.12761; the group_view overload of that
constructor (decomp/mastercam_stocksetup.c l.4597: GetMatchingPlaneID on the view's +8 matrix / +0x50 origin, else SetActivePlane(type,
matrix, origin)) shows the active Cplane then has the op_view's matrix (the op_view overload mcmsurf calls is not dumped - same pattern
assumed for the documentation only, not used by code beyond the Top check). msurf_map (l.339996-340000) sets plane 1 (Top) when the view
number is 0. The frame of FUN_180229ba0 (l.364015-364060; op 0x2e uses the WCS, GetActiveViewNo(.., 3)) and FUN_180258310 (l.395712-395741)
is that view's rows turned by cos / sin(pi / 2). Corpus: every op 132 has the Cplane plane 1 = Top except MULTIAXIS ROUGHING FALSE FLOOR op 6
(plane 52, a tilted matrix). Port: mm_hst_surface / mm_hst_frame take the view rows; c3_mesh_refs reads record 0x688 (+0x68 plane id, +8
matrix) and **refuses a Cplane other than Top** ("FUN_180258310 writes the mesh in that view's frame, which the port's HST engine does not
take - NOT PORTED"): the triangles would be in the view's frame and the port's HST engine works in world Top.

**The check model tmpmn5.emm (item 6) - DECODED (writer chain).** No DLL writes the literal "tmpmn5.emm" except mckernel's existence test
FUN_18006ea44 (strings of every DLL in ~/mastercam-re/files). mckernel FUN_18007bb48 (mk l.91572; op 132 path) takes mcmsurf's
**tmpmn5.tmp** (the check surfaces' machine model, written by mcmsurf FUN_180220fe0 l.358275-359600 - the tmpmn4 / tmpmn5 counterparts of the
drive's tmpmm4 / tmpmm5, kept by process_3rd_party_algorithm FUN_1800199d0 l.18246-18252 only for styles (record 0x16bc) in the set 0x86a4 =
2, 5, 7, 10, 15, removed otherwise), converts it in place with FUN_1800f200c (mk l.186479: opens it as an EMM model FUN_1800685e4, scales by
job +0x28 (FUN_180068e70), shifts by job +0x28 x (the tool-body values local_68 + local_150 + FUN_180096984(op +0x70)) and that minus job
+0x28 (FUN_180068a18)) and renames it: FUN_180135e60 (the name without its extension) + ".emm" (0x18014bcd0), FUN_180135ec0 = rename. The
same conversion turns tmpmm5.tmp into tmpmm5.emm (the drive model FUN_18006eac0 reads when the job's +0x50 flag is 0, i.e. the non-STL drive
path). So the check model is FUN_180220fe0's machine-model file (the EMM format, not an STL); porting it = FUN_180220fe0's writer + the EMM
reader / scaler of mckernel: NOT PORTED; the kernel-trimming check stays on mcs_mesh (cam3d_checktrim.c).

**The IFF drive path (found on the way, not taken by the corpus).** FUN_1800199d0 (l.18200-18230): when op +0x15fa (record 0x1662) != 0 and
FUN_1803b2750(op id) and FUN_1803accc0(tmpmm4.tmp) hold, the drive STL is not FUN_180258310's: ConvertBinIFFToStlFile writes
tmphardmillraw.stl from the interfacial (IFF) model, scale 25.4 / 1 (op 132 only, _MCAssert otherwise). Every corpus op 132 (2017 layout)
has record 0x1662 = 0. NOT DECODED / NOT PORTED; with MCAM_HST_MESH an op with record 0x1662 != 0 is refused (c3_mesh_refs).


### 27.6 Fifteenth pass: oracle results and measurements with the switch on (2026-11-14; MCAM_HST_MESH unset / =1, default unchanged)
DLL match (tools/tests/msm_oracle.sh `--d 0`, the HST call d = h = 0, chord 0.001 in / 0.0254 mm; mmb_cmp.py for fill_mach_model_buffer):
| set | make_surf_model | fill | refused |
|---|---|---|---|
| kind 1 / 10-over-1, every corpus part holding one (25 parts, --max 30) | **560 / 560 identical** (grid, loop, counts, box, edge records, every vertex / normal) | 161 / 161 (4 parts) + PARALLELSTEEP 1 / 1 | 2 (CURVE 5 AXIS SURFACE EDGE 2017 / 2018 surface 8: a degenerate 1 x 1 patch, calc_surf_norm fails in the offset evaluation - stale stack in the DLL, NOT DECODED as for kind 0) |
| kind 8 / 10-over-8, every corpus part holding one (8 parts) | **8 / 8 identical** | 3 / 3 | 0 |
| kind 0 / 10-over-0 sample (140 parts, every 3rd with surfaces) | 2093 identical | - | 5 (trim repair: TUTORIAL 9 x 2, ROUGHPROJNCI) |
| kind 0 with d = 0.375 / 0.125 (Constant Scallop's call; 47 parts), exact base evaluator | 353 identical | - | 0 |
| make_surf_polygons (ifk_surfpoly_oracle.sh, KINDS=0,10, 35 parts x 2 steps) | 68 / 70 runs byte-identical | - | the 2 = TUTORIAL 9's trim-repair surfaces (refused by the port) |
No licence import is reached (the --msm run stops on any; none fired).

Port and DLL harness (scratch measure script as 27.3; off = master's mesh (mcs_mesh / OCCT), on = MCAM_HST_MESH=1, now the whole Mastercam
mesh for PARALLELSTEEP op 5 too - before it needed the mixed diagnostic =2):
| op | port off -> on (cut ref->ours p95, ours->ref p95, length ref / ours) | DLL harness off -> on vs stored NCI (seg_dist p50 / p95 / max) |
|---|---|---|
| Waterline SWARF_METRIC 3 (mm) | 0.464, 0.051, 10239 / 9972 -> 0.457, 0.052, 9974 | ref->dll 0.0492 / 0.5 -> **0.0065** / 0.5, dll->ref 0.0450 -> **0.0040**; DLL levels = stored down to 13.238 (30 -> 28 levels) |
| Waterline SWARF 4 (inch) | 0.087, 0.002, 425 / 404 -> 0.066, **0.172**, 441 | 0.0025 / 0.0133 -> **0.0002 / 0.0065**, 0.0025 / 0.0142 -> **0.0002 / 0.0012** |
| Waterline MOLD BOTTLE 4 (inch) | 0.194, 0.002, 603 / 541 unchanged | 0.0037 / 0.0128 -> **0.0003 / 0.0050**, 0.0038 / 0.0239 -> **0.0003 / 0.0057** |
| Raster PARALLELSTEEP 5 (inch) | seg_dist ref->ours p95 0.0006 -> 0.0007 (max 0.0078 both), ours->ref 0.0003 -> 0.0004 | 0.0000 / 0.0003 / 0.0044 -> 0.0000 / 0.0003 / 0.0042 |
| Raster RESTPASSES 18 (inch) | 0.0009 -> 0.0011, 0.0004 -> 0.0007 | 0.0002 / 0.0005 / 0.0019 -> 0.0001 / 0.0005 / 0.0029 |
| OptiRough TUTORIAL 7 op 5 (mm, hst3d_oracle.sh) | off: **FAILED "nothing to cut (3 levels, 1 regions)"** - the same on master (rg_base_or, pre-existing); on: ref->ours p50 0.097 p95 0.124 max 4.873, ours->ref p95 0.121, cut length 21522 / 21444 (history: p95 1.03 on 2026-11-12) | - |
Reading: with the switch on every corpus HST op with a surface drive generates from Mastercam's own mesh; the DLL reproduces the stored
Waterline levels and paths to 0.0002-0.0003 in (p50); the port's own Waterline keeps its gaps (its level rule uses the flat facets only, the
contact-angle normal comes from the mesh: SWARF 4 ours->ref p95 0.002 -> 0.172); Raster barely moves (the passes are the DLL's within
0.0007 in either way); OptiRough, which fails on master's mesh, runs and comes to p95 0.12 mm. Refused with the switch on: the 10 solid-drive
HST ops (Parasolid), MULTIAXIS ROUGHING FALSE FLOOR op 6 (Cplane 52, not Top).
Regression (app code changed; default off): op_compare 47 / 637 = master's set exactly (rg_new15, master 56424e8 app code); fileop 0 / 205
against master's cam_cli outputs (rg_new15); ifk_surfpoly_test.sh ALL PASS.
## 28. Nineteenth pass (2026-11-14, M1r): the node wander and the open -> closed ramp PORTED - every Waterline surface ramp = the DLL

Branch worktree-agent-a394c48013b4bcc61 (merges worktree-agent-a19e9e70bbe455091 = section 26 and master 8947feb). New dump (gitignored):
decomp/hmm_linker_x15.c (FUN_18027b580, FUN_180015d30). Tools: HMM_TRACE `:oHEX` (the double at rcx + HEX on entry, up to four, e.g.
`1801d4990:o80:ob8`); tools/hmm_macro.py `HMM_CONTACT_LO=x` (testing only: ContactAngleLo replaced, so the passes plan keeps the whole loops);
cam_cli `MCAM_HMM_ITEMS=FILE` (testing: the closed toolpath items of the DLL's open passes, from such a run). Scratch: wl3/ (sdn2.py matches
the port's "sd link 3" lists to the DLL's FUN_1801d2bd0 lists by their first node; sdn1.py node by node).

### 28.1 The node wander (DECODED, PORTED: c3_sd_wander, c3_sd_wcheck in viewer/cam3d_finish.c)
- FUN_1801cd970 (x6 l.65-300) calls the connect's virtual +0x28 = FUN_1801d4990 (x14 l.38-457) for every new node of a mode-3 connect when
  connect +0xb0 (plan +0x1178 = LinkAllowWandering 0x1eb1) is set (FUN_180007fd0 x5: `+0xb0 != 0 && mode == 3`), right after the node's drop
  and BEFORE the split test (so the test, the node stored, FUN_180007eb0's lifted flag and +0x38 = |node z - curve z| all see the moved node).
  Waterline: on (traced: 486 calls on SWARF op 4, 488 on MOLD BOTTLE op 4; mckernel's macro does not write it, the Waterline plan's default
  writer is not in decomp/ - FUN_1803c55f0 hl l.21583 registers it with an expression, not followed); Raster: off (as before).
- FUN_1801d4990: the node N (dropped) equal to the curve point C (x, y, z; FUN_1800082c0 = exact ==) -> nothing. Direction: both segment ends
  wandered (list node +0x44 bit 0) -> the unit sum of their XY offsets (+0x48 / +0x50); else FUN_1801d4840 (x14 l.1-37; dis 1801d4840: t -+ 0.001
  in double, clamped to 0 / 1, the curve at both, the unit (-dy, dx)); a zero direction -> the unit (-(a.y - b.y), a.x - b.x). P+ / P- =
  N.xy +- dir x connect +0xb8 (LinkWanderInitialOffset 0x1b2d = max(0.006, 1.1 StepMachTolerance), hl l.12286-12292: 0.01397 mm traced) at
  z = min(connect +0x80, C.z), both dropped (FUN_1801cf7b0, the node's layer). N below both (strict) -> kept. Else the bisection from M = N
  while |P+ - P-|^2 > the step block's +8 (StepMinHorizontalChange^2): FUN_18005ab30(C, M, P-) <= FUN_18005ab30(C, P+, M) -> P+ = M, else
  P- = M; M = the XY mean (P+'s XY when equal) at the mean Z (P+.z when equal), dropped; 1000 rounds then stop when the gap stops shrinking
  ("WanderedPosition abandoned" log). The end: P- when P-.z <= P+.z, else P+; when its XY differs from N's the node moves there, flag set,
  offset (dx, dy, dz) kept (x14 l.420-436).
- **Connect +0x80 = the linker emitter's +0x38** (10.1) = the z minimum of the emitter's box (FUN_1801929d0 hk l.1615-1620: [3..8] = min / max
  per axis, min starting at 0x40f869ffe0000000 = 99999.99): traced with `:o80` = 9.1900791379850002 on every call of op 4 and 8.5736994044049997
  on MOLD BOTTLE = exactly the lowest Z of the DLL's passes (tag 1) on both ops; the port takes the lowest Z of the passes it links.
- After the reduce (FUN_1801d2bd0 x1 l.1271-1281, mode 3): FUN_1801d3a60 (x14 l.458-532) = the mean 3D distance of the nodes from the curve at
  their t without / with the offsets (du / dw); FUN_1801d4270 (x2 l.248-440; the disassembly confirms the unwandered copies are kept apart) = the
  largest cosine at a node of a triple (from -2.0) without / with (cu / cw); when du = 0, dw = 0, du / dw < LinkWanderDistanceRatioLo (2.0) or
  (du / dw < LinkWanderDistanceRatioHi (30.0) and cu < cw) (0x1b2e / 0x1b2f, defaults hl l.12295-12309, connect +0x188 / +0x190), FUN_1801d3c20
  (x3 l.808-1035) = the path lengths from the first node without / with the offsets, each node's Z raised to the line first -> last where that is
  higher; without < with -> FUN_1801d39e0 (x14 l.534-560) takes every offset back. Then FUN_1801d0240, the wander pass (wl_wander), as before.
  Traced on op 4: 22 FUN_1801d3a60, 12 FUN_1801d3c20, 1 FUN_1801d39e0; the port: the same 12 length checks, the same 1 undo.

### 28.2 The open -> closed ramp (DECODED, PORTED)
FUN_1801c3dd0 (x1 l.2148-2215), plan kind 0, the previous frag P open (+0x78 = 0): FUN_180015d30 = P's CTessToolpath (key P +0x70); when the
toolpath's item record of P (+0xd0 x 0x40 + item array +0xa8) has +0x20 set (the item is closed), FUN_18027b580 (x15 l.1-70) makes the frag
of the whole item (point 0 to point count - 1, the item's closed flag, P's direction flag +0x79); FUN_180281580 re-splits it at P's end (+0xa8,
+0x80 when reversed) and it takes P's place (local only): the ramp's first piece is d along the item past P's end. The connect's wander-pass
flag +0xa0 stays from the link's own frags (P open): no FUN_1801dde40 pass (traced: op 4's first ramp keeps nodes below that hull). The node
wander (+0xb0) runs as for any mode-3 connect. An item that is not closed: not ported (SAFE). Port: an open pass of a loop whose points are all
inside the region (the vertex filter's pieces, gen_waterline) carries the loop as its item (WlPass.item); wl_link re-splits it at P's end
(wl_resplit) and runs wl_ramp_link without the hull pass.

### 28.3 Checked against the DLL (the port on the DLL's passes, 1024 chords per arc)
| op | DLL ramps | port = DLL (same t on every node) | max node distance |
|---|---|---|---|
| TUTORIAL 2 SWARF op 4 | 22 | **22 / 22** (21 closed -> closed, before 7; the open -> closed one with MCAM_HMM_ITEMS) | 4.8e-5 mm (the passes' chords; the first ramp 8.7e-11) |
| MOLD BOTTLE op 4 | 28 | **27 / 27 closed -> closed**; the first (open -> closed) 28 / 29 nodes | 4e-5 mm; the first 0.07 mm |
MOLD BOTTLE's first ramp: its item comes from the no-filter DLL run, whose loop at z 39.856 passes 1.2e-4 mm beside the filtered pass's end
(the loop's arcs are fitted anew), so the ramp's first piece is that far off and one subdivision differs: the test input, not the rule (op 4's
item from the same kind of run lies on the pass end exactly).

### 28.4 Measured (the port's own passes; seg_dist / link_stats vs the stored NCI, inch)
| op | ramps emitted (SAFE) before -> after | retracts (ref) | rapid XY (ref) | ours->ref > 0.005 | ours->ref p95 / ref->ours p95 |
|---|---|---|---|---|---|
| TUTORIAL 2 SWARF op 4 | 18 (3) -> 21 (0) | 53 -> 50 (42) | 66.4 -> 66.1 (51.2) | 240 -> 302 | 0.0041 = / 0.1017 -> 0.1029 |
| MOLD BOTTLE op 4 | 13 (8) -> 21 (0) | 105 -> 97 (71) | 101.5 -> 101.0 (84.2) | 9435 -> 9494 | 0.0057 = / 0.1180 -> 0.1183 |
| SWARF_METRIC op 3 (checks: old order) | 0 | 57 = (39) | 945.5 = (481.2) | = | = |
The new ramps are the DLL's rule on the port's passes; where the port's passes differ from the DLL's (op 4 70 vs 65, MOLD BOTTLE 117 vs 98: the
contact-angle split, 22.2c) a ramp runs where Mastercam has none (the vertices over 0.005 in), the retracts left are the SAFE ones (28.6 3.).
Regression (full build of this branch): op_compare 48 / 637 vs master's run rg_new16 47 / 637 - the same scene list plus gen_waterline.txt
(BRACKET4's Waterline op 5503 -> 5931 points: the ramps that kept SAFE for a lifted node or an open previous pass are now emitted), re-blessed
(explained); fileop 0 / 205 differ from rg_new15 (no user op is a Waterline op). Shared functions touched: c3_sd_add / c3_sd_split (C3Sd gained the
wander fields; the split's node is 7 doubles; FUN_180007eb0's rise now read from the node after the wander - identical for Raster, whose
connect does not wander: fileop and the Raster scenes unchanged).

### 28.5 The retract with leads (step 3): mapped further, NOT DECODED (SAFE)
FUN_1801a1240 (hk l.10822-11350) for plan kinds in the mask 0x9d (Waterline 0 is in it) skips the two-probe-connects branch (which is for the other
kinds) and builds from the pass's CTessToolpathMill the GridC entries of type 0x1e / 0x1f (FGridC, 0x48 bytes each: +8 type, +0x10 / +0x18,
+0x20 / +0x21 flags, +0x28.. the values) into a ToolBoxSetRaw (FUN_180231f50, FUN_180017470, FUN_180231900(set, 0, -1.0, 1.0)) and tests it
with FUN_180176540 - the lead side comes from the passes plan's per-toolpath grid data, which the port does not have (the passes plan's
CTessToolpathMill grids are not ported). FUN_1801a1d70 (hk l.11351-13030) calls FUN_1801a1240 twice, FUN_18019d5d0 / 18019db50 (3 each),
FUN_18019e010 once, FUN_1801a54f0 x12 / FUN_1801186d0 x10 (the writer), FUN_1801d65f0 / 1801d69f0 / 1801d62e0 / 1801d6de0 / 1801d7210 (the
connects). Not decoded further this pass.

### 28.6 Where M1 stops (eighteenth pass) - next, in order
1. Style 1's +1.0 mm (SWARF_METRIC op 3; 12.1, FUN_1801990c0 returns +0x30 for style 1): measured this pass with the DLL on op 3
   (HMM_NO_INFLEXIONS, mckernel's macro by hmm_macro.py; HMM_TRACE 1801990c0 returns 25 heights, 66.444 .. 74.033): where a DLL retract matches
   a stored one (start and end within 2.3 mm: 3 links) the stored top is 0.99 / 1.06 / 1.13 mm higher - so the DLL run on this macro has no
   +1.0 either: the term comes from the DLL's input (a parameter mckernel writes that hmm_macro.py does not, or the check surfaces' trim
   plans of op 3), not from FUN_1801990c0. Next: diff hmm_macro's op-3 macro against mckernel FUN_180075b48's linking block for the
   clearance / protect parameters (LinkClearSurfaceBy, LinkerProtectThickness 0x1ec9, SurfaceRampOffset) and run the DLL with each.
2. The Waterline retract FUN_1801a1d70: FUN_1801a1240's ToolBoxSetRaw test (FUN_180176540, FUN_180231900, the GridC 0x1e / 0x1f data of the
   passes plan's toolpath - first: what the passes plan writes there), then FUN_18019e010's helix lead-in and the lead retries (18019bba0 /
   18019c7c0 / 18019d5d0 / 18019db50), the lead test FUN_18019a6a0 with the horizontal radii; then the first approach / final exit (step 4).
3. The passes-plan difference (op 4 70 vs 65, MOLD BOTTLE 117 vs 98): the weave contact word into FUN_18040b6f0 (22.2c) - not started.
4. Op 3's trimmed frags' order (21.3); an open pass whose item is not closed (FUN_1801c3dd0 leaves P alone: the connect without P's piece).


## 29. Nineteenth pass (2026-11-14, M1s): the Waterline retract's lead side, centre, helix top and helix DECODED; nothing ported yet (SAFE)

Branch worktree-agent-a2083b8a0dffed7d6 (merges worktree-agent-a394c48013b4bcc61 = section 28, renumbered from 26, and master da1ebd0). New dump
(gitignored): decomp/hmm_linker_x17.c (FUN_180176540, 180017470, 180231900, 180231f50, 18017e5a0, 180176760, 1801d3530, 1801b3670, 1801dd980,
1801d2940, 18000e6a0, 180177080). Harness: HMM_TRACE `:g` (tools/peloader/hmm_link.c: on FUN_180017470 entry the FGridC at r8 - type, flags,
CGridVal list - and the FMsArray of doubles at rdx). Scratch (not committed): m1s/ (o4t.tr / seq.txt: the call tree of every link of SWARF op 4,
o4g.tr: the GridC data, o4o.tr). The viewer code and the oracles are unchanged this pass (only the harness option).

### 29.1 FUN_1801a1240 for Waterline (CORRECTS 26.1 / 28.5): the side and centre come from the region curve, the return is always 1
- hk l.10941-10950 + dis 1801a12bc-1801a12d6: plan kind +0x1058 in the mask 0x9d (Waterline = 0) sets the flag [rsp+0x51]; the tail (dis
  1801a1b01-1801a1b2c) then returns 1 whatever the test gives. The two-probe branch (hk l.10951-11010) is for kind 3 / no region list only.
- The test itself is not dropped: Ghidra lost FUN_180176540's arguments (dis 1801a194c-1801a1984 / 1801a1ac5-1801a1aef): it is
  **FUN_180176540(set, -, P = the pass point (+8), T = the frag's tangent there (FUN_18027ad40), R, &centre, &side)** and it WRITES the
  centre (param_5) and the side (param_6) that FUN_1801a1d70 hands to FUN_18019e010 (local_1818 / local_1954 for the lead-out at the
  previous pass's end, local_1838 / local_1955 for the lead-in at the next pass's start).
- FUN_180176540 (x17 l.170-238): C0 = P + R (-Ty, Tx) / |T| (left), side 0; if FUN_1802887a0(C0, 1000, set) >= R - 0.012 -> C0, return 1; else
  C1 = P - R (-Ty, Tx) / |T|, side 1; >= R - 0.012 -> C1, return 1; else the one with the larger value (ties to C1), return 0.
  FUN_1802887a0 = MeasureInterior2D (decomp/hst3d_trimsurf_meas.c l.1-45; scallop.md): min(1000 + set +0x30, distance to the set's curves),
  negative when the crossing count is even (outside).
- The set (ToolBoxSetRaw, FUN_180231f50 / FUN_180231900(set, 0, -1.0, 1.0): box grown by 1 + 1, cell = max(10, 0.002 x the larger side)) is
  built by FUN_180017470 (x17 l.51-168: lines FUN_1800613a0, arcs FUN_180062160 / FUN_1800624e0 by the CGridVal flags) from the GridC entries
  of type 0x1e / 0x1f of the frag's CTessToolpathMill (cached per toolpath at linker +0x3d0 / +0x420, FUN_18000e6a0 / FUN_18017e5a0).
  **Traced (`:g`) on SWARF op 4: every Waterline toolpath carries one type-0x1e GridC = the region's full Waterline loop at that level,
  arc-fitted** (2D points, the FMsArray's origin Z = the level: 48.3937, 47.1292, 45.8691 ...; e.g. 47.129: 200 items / 227 points, closed,
  through the open pass's ends), written in the passes plan at mchmm 1804a391a (the CZ slice, before the contact-angle vertex filter) and
  read again at 18025299e. So the lead circle goes on the side of the pass where it fits inside the region (the tool-centre region of that
  level) with 0.012 mm slack. Checked: first approach C = (-39.38603, -13.54248) = the DLL's helix centre, FUN_1802887a0 = 3.01625 = R exactly
  (the pass point is the loop's nearest point); retract 1's lead-out C = (-16.14639, -32.62060) = its arc centre in tag 2.
- Port mapping (not yet coded): the region's loops of the pass's level (gen_waterline: the PL loops with the pass's node, unfiltered, mm), the
  signed distance by parity and segment distance; our loops are polylines where the DLL's are arc-fitted (input difference, not a rule).

### 29.2 What FUN_18019e010 does with them (DECODED in parts; traced on op 4)
Call tree per link (m1s/seq.txt; FUN_180194140 = the first approach, FUN_1801a1d70 x40, FUN_180197b80 = the exit):
| links | callees per link (besides FUN_1801a1240 x2 in FUN_1801a1d70) | what it is |
|---|---|---|
| approach | FUN_1801af3d0, 18019afc0, 18019c7c0, 18019a6a0 x1, 18011a350 | vertical lead-in + helix |
| retracts 1-14 (upper levels) | FUN_1801af3d0, 18019afc0, 18019b180, 18019bba0, 18019c7c0, 18019a6a0 x2, 18019b590 x2 (retract 2: 18019b590 + 18011a350), FUN_1801990c0, FUN_1801a4da0 | the "clean" path: lead-out arc, vertical leads, helix |
| retract 15 | + FUN_180176760, 18019afc0 x2, FUN_1801a1240 x1 in the retries | mixed |
| retracts 16-40 (bottom pockets) | FUN_18019bba0 3-7 x, FUN_1801a1240 6-9 x, FUN_180176760 3-6 x, FUN_18019a6a0 6-14 x, FUN_18019d5d0 | lead-out blocked: rotated targets (local_ec0 < 6), the vertical fallback FUN_18019d5d0, the ramp along the toolpath FUN_180176760 |
| exit | FUN_18019bba0 x6, 18019a6a0 x6, 18019b180 x6, FUN_1801a1240 x7, 18019d5d0 | blocked lead-out: vertical fallback |
- Helix top (hk l.9296-9343, local_ed8): with the frags' +0xb9 set (always on op 4) it is max(z_next + (float)RampHeightOffset (+0x11a4),
  v), v = FUN_1801af3d0(next frag), lowered by the excess over linker +0x1e8 (= LinkZClearance: traced 101.6 on all 42 calls) of
  v + sqrt(Rv^2 - (Rv sin a)^2) + max(0, LinkRampHeightOffset +0x1170) (Rv = LinkVerticalLeadInRadius +0x1128, a = MaxRampAngle). Without +0xb9:
  DAT_1809bf1a0 x R_in x tan(a) + z_next (DAT_1809bf1a0: presumably pi, to be confirmed).
- FUN_1801af3d0 (hk l.20792-20895): a 0x13fa toolpath -> LinkRampHeightOffset + frag +0xb0; else the toolpath's parent set (map linker
  +0x430 +0x70): empty -> (float)RampHeightOffset + plan +0x1240 + the frag's end Z (op 4: 2.999994 + 1.27 + 47.1292 = 51.3992; +0x1240 = the
  op's stock 0.05 in, presumably ToolThicknessZ - to confirm), else max(frag +0xb0, the first parent toolpath's +0x88) - traced: the next
  level up (47.1292 for a pass at 45.8691). Traced values 51.399 (x2), 47.129, 45.869, 44.608 ... = the helix starts of tag 2.
- FUN_18019afc0 (hk l.7139-7195): the helix start on the lead-in circle: L = (top - z) / tan(a), n = int(L / (2 pi R)) full turns (out
  count), the rest angle (L - n 2 pi R) / R back from the pass start around C (sign by the side) -> H.
- FUN_18019b180 (hk l.7196-7328): the tangent point from a point to a circle (both tangents, picked by the side): the lead-out arc ends where
  its tangent points at the helix start (retract 1: X = (-15.41673, -30.86088) = the lead-out arc's end in tag 2).
- FUN_18019b590 (hk l.7329-7446): writes an arc P2 -> P3 around P4 as one arc, or two (through the antipode of P3) by the side test /
  FUN_18004d7e0 angle > 2.0; FUN_18011a350 (decomp/hst3d_weave_drop.c l.6537-6700): the helix from (P, top) to (Q, bottom) around C: the
  base sweep P -> Q plus k = int((top - bottom) / pitch - sweep / 2 pi + 1) (>= 0) full turns, written as int(sweep / (2 pi / 3) + 1) equal
  arcs (op 4 approach: 460 deg in 4 arcs of 115 deg, as tag 2).
- Vertical leads at the horizontal-lead ends (hk l.10165-10216) and their tests FUN_18019a6a0 (13.5) with the horizontal centres; the writer
  FUN_180118ee0 / FUN_18011a840 (vertical arc) / FUN_1801194d0, + LinkRampHeightOffset (+0x1170), the retract height FUN_1801990c0 and the
  assembly FUN_1801a4da0: read, not decoded to the end.
- Not decoded: FUN_18019bba0 / FUN_18019c7c0's outer test in the clean case (their collision check FUN_1801d3530 + drops; on retracts 1-14
  neither calls FUN_1801a1240, so the inner trim loop is not entered - the way out is to be read in the disassembly: Ghidra's output of both
  has unreferenced labels LAB_18019c3a2 / LAB_18019c742 / LAB_18019d55f), the blocked branches (FUN_18019d5d0, FUN_180176760 = the ramp along
  the toolpath's own GridC, FUN_1801b3670), FUN_180197b80's exit.
So the Waterline retracts still keep the SAFE rule (N13-29): nothing of 29.1 / 29.2 is in the port yet.

### 29.3 Style 1's +1.0 mm (SWARF_METRIC op 3): not in mckernel's linking block
- FUN_180075b48 (mk l.88917-90217) walked line by line against hmm_macro.link_params for kernel style 1 / LinkRetractStyle 1: every parameter,
  condition and value is the same (LinkClearSurfaceBy 25, the clearance, LinkMaxStayOnSurface ...); the style-1 branch writes no
  ToolThickness / LinkerProtectThickness / LinkMinThinningTolerance / LinkClearSurfaceWithin. Only difference found: the LinkSafeZ
  corner-radius flag is K+0x120 instead of K+0x48 when K+0 != 0x84 (mk l.89179-89193) - LinkSafeZ only, not the retract height.
- Mastercam runs two macros (FUN_18007d000: passes FUN_18007d7c8, then linking FUN_18007d028 with `entity ekPlan0 = GetSinglet(elSelectedPlans,
  0);`, mk l.92310-92445); hmm_macro writes one. Tried in the harness (m1s/m3a.mac + m3b.mac): the passes macro returns, the linking macro
  spins (> 10 min at 90 % CPU, killed; the one-macro run takes 4 s) - the two-macro form does not run under tools/peloader as it is.
- The DLL with ToolThicknessXY / Z 1.0 also written into the linking plan (testing only): every retract height identical (the linker already
  takes the passes plan's thickness). So the ~1 mm (0.99 / 1.06 / 1.13 on the 3 matched links: not a constant) is still unexplained;
  candidates left: the drive mesh (N13-33: op 3's sloped walls, FUN_1801990c0 measures the path height on it) or op 3's check-surface plans.

### 29.4 Where M1 stops (nineteenth pass) - next, in order
1. Port 29.1 (FUN_1801a1240 / FUN_180176540 on the region's loops); check the centres against the FUN_180176540 trace on the DLL's passes
   (needs the DLL's region curve with MCAM_HMM_PASSES: dump the GridC per toolpath key with `:g`).
2. Read FUN_18019bba0 / FUN_18019c7c0's clean exit in the disassembly and FUN_1801d3530 (x17 l.885-1095), then port the clean path of retracts
   1-14 and the approach (lead-out arc to the tangent point, vertical leads, LinkRampHeightOffset, FUN_1801990c0's height, helix top / start /
   writer); check call by call against m1s/o4t.tr. Retracts 15-40 and the exit (blocked leads) keep SAFE until FUN_18019d5d0 / FUN_180176760
   are read.
3. Style 1's +1.0: run op 3's DLL harness on Mastercam's mesh (MCAM_HST_MESH=1) and compare the matched retracts' tops; find why the
   two-macro form spins in the harness.
(The passes-plan difference / the contact-angle filter's input is now another agent's task, N13-34.)
## 28b. Nineteenth pass (2026-11-14, M1r): the retract leads' attempts PORTED; the wander FUN_1801dde40 PORTED for mode-1 paths

Code: viewer/cam3d_hstlink.c (included by cam3d_finish.c; the shared linker connect code - the wander is there, Waterline's linker reaches the
same FUN_1801dde40: wl_wander of section 26 now calls hl_wander, one implementation). Decompiled for this pass: decomp/hmm_linker_x15.c (FUN_180051e00, 180177520 / 540 / 860 / 900, 1802800f0, 180279600,
18002d030, 180279cc0, 180192400, 1801dcc30, 1801d0340, 1801d07e0, 1801daf90, 1801da4a0), x16 (FUN_18027c660, 180280990, 1801cfbd0 / fc70,
1801e07f0, 1801de920, 1801de3a0, 1801de1f0; the latter four were already in x5). Tools (testing): peloader HMM_TRACE `:o` / `:s` print the
doubles at the pointer arguments rcx..r9 / the stack arguments 5..12 on entry and on return (tools/peloader/hmm_link.c); scratch
(<scratch>, not committed): run.sh DIR NAME (the port on the DLL's passes of a synthetic case, record patches and
tool from its .rec / .par, then link_cmp), fitcmp.py (FUN_180171b50 call by call: the DLL's :o / :s trace vs the port's "la fit" lines),
ridges.py / grid.py / tc.py (the wander cases), syn3.sh / dll.sh as before.

### 28.1 The lead attempts (25.2) PORTED (cam3d_hstlink.c hl_rise, cam3d_finish.c c3_lk_attempts)
- A retract's lead whose top T rises (FUN_1801d7450: C removed, FUN_1801d9330 = 1, FUN_1801d87b0) goes to FUN_1801d76a0 with the attempts flag:
  FUN_1801d8240's bisection (the overlap from |S - T|xy / 2, stop below connect +0x200 = (float) LinkTrimMinOverlapDiff = LinkMaxAllowedTrimming
  / 100 by default), each round FUN_180171b50 (FUN_180172020 the walk, FUN_1801725b0 the pass point, FUN_180172950 the arc's point at the walk
  start and the direction, the centre / top recomputed for a nonzero overlap) and FUN_1801d8b30 (the new top dropped; the arc's refinement
  c3_lc_arc); then FUN_1801d7b00 (overlap 0); the trims (approach +0x1e8 = v, exit +0x1e0 = -v). The walk runs on FUN_1801dbf80's pass nodes
  (segment starts until 2.5 R of XY length and three nodes, then the last end; the frag iterator FUN_180280990 taken as the polyline's
  segments). The connect mode's neighbour frag (+0x48 / +0x50) is empty on every traced Raster retract; FUN_1801d8630 the vertical lead.
- FUN_1801a1d70: first call with the attempts (LinkMaxAllowedTrimming > 0 and LinkTrimmingStyle 1 or 2: plan +0x110a / +0x110b, hl l.4836-
  4841); each side fits when |trim| <= LinkMaxAllowedTrimming and < its pass's XY length (FUN_180279cc0 = FUN_18027c660(.., 1), dis 1801a3c4a);
  both fit: trims 0 -> the retract as before (a rising side with its vertical lead), else the pass trimming FUN_1801a44a0 / 1801a47b0 (NOT
  PORTED: SAFE, counted "a trim"); one does not -> the second call with the fit flags as attempts. LinkTrimmingStyle 2 skips the lead nodes
  (FUN_1801db570 param_5): NOT DECODED, SAFE. LinkMaxAllowedTrimming is record 0x13a0 (section 1's table said 0x1390: fixed), LinkTrimmingStyle
  record 0x16d1.
- Emission: the exit's vertical lead = E -> E + R z at the feed, then + LinkRampHeightOffset; the approach's = down to S + R z, then S.
- Checked on c45 (HMM_TRACE :o / :s): 400 / 400 FUN_180171b50 calls = the DLL (max 7e-15: v, c0, the three arc points), 50 vertical leads =
  50 FUN_1801d8630 calls, 25 retracts through the second call as the DLL. link_cmp (port on the DLL's passes): c45 136 / 140 (was 110: the 26
  lead-blocked retracts now equal; the 4 left are the arc-chord artefacts of 23.2, 0.0001 in), c45s 190 / 194 (461 fits, 56 vertical leads),
  c45z 182 / 182. The other synthetic cases (v80, ct1-3, f6 / f10, h70 .. hn, v60-v75) unchanged against the seventeenth pass's port (the
  pre-existing ct2 clearance case and the 87.73 deg refusals of hk / hd / ha stay as they were).

### 28.2 The wander FUN_1801dde40 PORTED for mode-1 connects (hl_wander, c3_sd_wander)
- FUN_1801d2bd0 (x1 l.1262-1297): the reduce, FUN_1801d0240 (drops nodes flagged 0x80), then with the wander flag FUN_1801deaf0 (a two-node path:
  nothing; line paths of three or more: FUN_1801dde40), FUN_1801cea60. FUN_180007eb0 (x6 l.306-345) inserts a reduce node with the connect's node
  virtual +0x20: code 8 in mode 3 (0x40), else 6 when the dropped Z equals the curve's (0x80: removed by FUN_1801d0240) and 5 otherwise (0).
- FUN_1801dde40 walks the triples (a, b, c): FUN_1801e07f0 (line nodes) is 1 when b lies above the line a-c (Z by XY distances) - the triple moves on;
  else FUN_1801de920 marks b 0x80 (virtual code 9) and steps back (or forward from the first node); FUN_1801de1f0 drops the marked nodes. A
  0x40 node (mode 3: Waterline's closed ramp) follows section 26's reading (z_i <= b.z: the next triple; marked runs raised onto the line,
  FUN_1801e0d70); the arc branch FUN_1801de3a0 is not ported. One implementation: hl_wander (cam3d_hstlink.c), wl_wander calls it.
- Note for the Waterline port: on the ridge case w1 FUN_1801deaf0 returned 0 for a three-node (mode-1, inner flag 0) path and FUN_1801dde40 ran
  (traced), while wl_wander skips paths below 5 nodes (section 26's reading of FUN_1801deaf0 for 0x40 nodes); left as it is (Waterline's), to
  be checked against a trace there.
- Used for the kind-1 links, the rise branch's mode-1 retry and the extension loop's mode-1 connect (all made by FUN_1801d1e70 with arg 13 = 1).
- Cases (RESTPASSES op 18's record on synthetic meshes, stepover / LinkMaxStayOnSurface patched: 0x1230, 0x16cd = 0, 0x1308): w1 (a 0.02 mm ridge
  across a containment gap of 0.9 mm on each raster line: 5 kind-1 links, 5 FUN_1801dde40 calls, nothing removed), w2 (three 0.026 mm ridges
  over a 2 mm gap: 6 links, the collinear middle node removed, path 5 -> 4 nodes), w3 (ridges between 1 mm raster lines and a wall past the
  containment's end: 5 rising links whose extensions fail; the mode-1 retries split by the chord length, their middle node flagged 0x80 and
  removed, no FUN_1801dde40 call - as traced): every link = mchmm.dll (link_cmp; only the arc-chord artefacts differ).

### 28.3 Measured (regress3 of this branch after merging master da1ebd0, against the orchestrator's master run rg_new16)
- GUI suites: wf 1 / 524, srf 0 / 37, chain 0 / 63, solid 2 / 83, cad 0 failed, misc 5 / 88, stockplanes 16 / 125 = master; op 51 / 637 vs
  master 47 / 637 - exactly 4 more: s4_check_trim .txt / .nc and s4_check_trim_stock .txt / .nc (the flat lofts with the check strip: 16 / 18
  retract leads that rose are now FUN_1801d8630's vertical leads after the attempts, 4 kind-1 / retry paths through the wander). Checked on the
  scene's own mesh with mchmm.dll (the app with MCAM_HMM_DUMP, hmm_oracle on the dump, the app again with MCAM_HMM_PASSES): the first 128 of the
  DLL's 136 FUN_180171b50 calls equal (max diff 0); the other 8 and one FUN_1801d8630 are the final exit's (FUN_180197b80, not ported: that exit
  keeps SAFE). Re-blessed those 4 (explained), then = master's set. checktrim_test: all ok, stops 35 / 65 and 34 / 66.
- fileop 0 / 205 differ from master; unit tests (units / blend / hst_rest / stockmodel) = master, horiz / pencil pass here (master's run had
  the link error fixed in b054c77); the 3D oracles (OptiRough mm / inch, Waterline, surface rough, finish RA / SF, area roughing, scallop,
  sfcontour) identical to master's.
- Synthetic cases (port on the DLL's passes, link_cmp): c45 136 / 140 (110 before), c45s 190 / 194, c45z 182 / 182, w1 / w2 / w3 every kind-1 /
  retry link equal; the other 21 cases unchanged (their differences are the arc-chord artefacts and the known refusals).

### 28.4 Where M1 stops (eighteenth pass) - next
1. The pass trimming of a fitting lead (FUN_1801a44a0 / FUN_1801a47b0, hk l.12738-12745 / 12935-12990): PORTED in the twentieth pass (33.2).
2. LinkTrimmingStyle 2 (lead nodes skipped, FUN_1801db570 param_5): PORTED (33.3); the connect mode's neighbour frag in FUN_1801d76a0, arc
   nodes in the walk: still open.
3. The first approach / final exit leads that rise: PORTED (33.1).
4. The wander's arc branch FUN_1801de3a0 / FUN_1801de630 and FUN_1801ce930 / FUN_1801e0d70 (0x40 nodes): Waterline's mode-3 / arc paths.

## 8. Oracle baseline (link structure; tools/tests/link_stats.py, ours = SAFE)

| op | retracts ref / ours | rapid XY length ref / ours | plunges at feed ref / ours |
|---|---|---|---|
| TUTORIAL 2 SWARF_METRIC op 3 (Waterline) | 39 / 57 | 481 / 1088 | 44 / 56 |
| PARALLELSTEEP.mcam op 5 (Raster) | 2 / 479 (2 after M1e) | 0 / 14 (0) | 2 / 478 (1) |

## 30. Twentieth pass (2026-11-14, M1w5): the Waterline retract with horizontal leads PORTED for the clean path (approach + retracts 1-14 of SWARF op 4)

Branch worktree-agent-a7ddf1635849485e0 (merges worktree-agent-a2083b8a0dffed7d6 = section 29, master 1264038, worktree-agent-aec0e44b919baf8b9 =
section 28b). hk = decomp/hmm_linker.c, x5 / x10 / x17 = decomp/hmm_linker_x*.c, "dis" = objdump of mchmm.dll. Code: viewer/cam3d_finish.c
`wl_retract` and its helpers (wl_side, wl_meas, wl_tangpt, wl_hstart, wl_arcchk, wl_chk_out, wl_chk_in, wl_vtop, wl_arcmv, wl_varc), called from
wl_link for every link that is not a stay-down link (FUN_1801c05c0's test now applied to open next passes too) and for the first pass.

### 30.1 Tools (harness reading aids; nothing of the DLL answered)
- tools/peloader HMM_TRACE: `:n` (an address inside a function: "B> ADDR rax xmm0 xmm1", no return tracking = basic-block coverage), `:sN` (N stack
  arguments on entry, dereferenced only where mapped), `:wK` (on return two doubles at stack argument K's pointer + the byte at K+1's: e.g.
  FUN_180176540's centre and side), `HMM_TRACE=@FILE` (the list in a file; TR_MAX 8192). M1r's `:o` / `:s` are `:O` / `:S` here.
- Scratch m1w5/ (not committed): bbs.py (basic-block starts of an address range from the disassembly, as `:n` items), covsplit.py (a coverage
  trace cut into links: approach FUN_180194140, retracts FUN_1801a1d70, exit FUN_180197b80), blk.py (asm of a range with the visited blocks
  marked), gloops.py (the region loops from an HMM_TRACE 180017470:g: FUN_180017470's points + CGridVal entries, an entry with bit 26 = an arc
  centre from the point before to the point after, flags 5 = counterclockwise / 4 = clockwise - checked: closed loops of the expected length),
  run.sh / prep.sh (dump, DLL passes with 1024 chords per arc, region graph, loops, coverage).
- cam_cli `MCAM_WL_LOOPS=FILE` (testing, with MCAM_HMM_PASSES: the DLL's region loops per pass), `MCAM_WL_LEADDBG=1` (the lead geometry and the
  check drops per link), `MCAM_WL_NOLEADS=1` (testing: the SAFE links as before). tools/hmm_oracle.py writes helical arcs with Z linear in the
  angle (it kept the start Z before; the helix comparisons showed 1 mm steps).
- With MCAM_HMM_PASSES the tangent at a pass end is that of the circle through the next three chord points when the end lies on it (the arcs the
  DLL's passes were chorded from; FUN_18027ad40 is the frag's derivative) - the centres then equal the DLL's to 1e-9 mm.

### 30.2 The clean path, block by block (coverage traced on op 4; MOLD BOTTLE op 4 the same set)
Links of op 4 by coverage: approach and retracts 1-14 take one path ("clean"); retract 15 retries in FUN_18019c7c0 (FUN_1801a1240 along the
pass); retracts 16-40 and the exit run FUN_18019bba0 6 x (the lead-out rotation, FUN_18019a6a0 returns 1 each time) and the fallbacks. MOLD
BOTTLE op 4: approach + retracts 1-4, 6, 7, 9-20 clean (18), the others blocked.
- FUN_1801a1d70 (hk l.11960-12360): FUN_1801a1240 at the previous pass's end (HorizontalLeadOutRadius +0x11f8) and at the next one's start
  (+0x11f0) -> FUN_180176540 (29.1): centre and side; angles: in = MaxRampAngle +0x11e0, out = LiftAngle +0x11e8 (pass +0xb9 set: traced on every
  Waterline pass). FUN_18019e010's arguments mapped from the call at dis 1801a2ea2-1801a2f95 (p9 E, p10 C_out, p11 R_out, p12 side_out, p13 z_E,
  p15 / p16 the angles, p17 C_in, p18 S, p19 R_in, p20 side_in, p21 z_S, p23 / p24 the lead tops out / in).
- FUN_18019e010 (dis 18019e010-1801a123a): the helix top (29.2; dis 18019e2a5-18019e3df: max((float)RampHeightOffset + z_S, v), v =
  FUN_1801af3d0 lowered by its excess over LinkZClearance); FUN_1801af3d0 for a Waterline frag: its toolpath's set in the map linker +0x430 +0x70
  - empty: (float)RampHeightOffset + plan +0x1240 + z; else max(z, the first entry's toolpath +0x88). Plan +0x1240 = max(Stepdown 0x1ada,
  MachStepover 0x1ad9) (hl l.4892-4899; traced 1.27 / 0 on op 4). The map's writer is NOT DECODED: traced, it is empty for the regions whose
  parent region holds no pass and gives the parent's level otherwise (port: wl_vtop, the parents with passes).
  The helix start FUN_18019afc0 (n full turns, H); local_fe1 = the two lead circles overlap (|C_in - C_out| <= R_in + R_out): with n = 0 that is
  another branch (hk l.9530) - SAFE here; the tangent point X = FUN_18019b180(H, E, C_out, R_out, side_out).
  The rotation loop (LAB_18019ed4f): one round when nothing blocks: FUN_18019bba0, FUN_18019c7c0, the vertical leads (dis 18019f8af-18019fd84:
  the units (-dy, dx) / R of X - C_out and H - C_in by side; out: centre (X - d h, z_E + sqrt(Rv_out^2 - h^2)), h = 0 with LiftAngle 0 (FUN_18019ab50
  x tan(0)), top = centre + d Rv_out; in: h = Rv_in sin(MaxRampAngle), centre (H + d h, top + sqrt(Rv_in^2 - h^2)), top_in = centre - d Rv_in),
  FUN_18019a6a0 on both (c3_lk_test, 13.5). Then the writers (dis 1801a00b0-1801a0fc8): exit = FUN_18019b590(E, X) (the horizontal lead-out arc,
  split at the antipode when over a half turn - same geometry), the point X, FUN_18011a840 (the vertical arc to top_out), top_out.z +=
  LinkRampHeightOffset and the line to it (FUN_1801194d0, +0x1108 set), a drop at it (a rise: NOT DECODED -> SAFE); lead-in = top_in raised to
  its drop (FUN_1801d2940) and FUN_1801dd980 (no 0x13cc frag: unchanged) + LinkRampHeightOffset, the point there, the line down to top_in,
  FUN_18011a840 to (H, top), the helix: n > 0 FUN_18011a350 (k = int(n - sweep / 2 pi + 1) more turns), else FUN_18019b590(H, S).
- FUN_18019bba0 (dis 18019bc33-18019c7b0): base z = min(z_E, *(emitter[0] + 0x450)) (-1.905 on op 4; every comparison below is against a Z at or
  above the base, so the base never decides); the arc E -> X within a half turn (the cross product's sign = the side and FUN_18004d7e0 <= 2.0;
  FUN_18004d7e0 = the "diamond" angle, dis 18004d7e0-18004d87e): the drop at X (FUN_1801d2940) not above z, then FUN_1801d3530(E, C_out, X); over a
  half turn (dis 18019bfc2-18019c303): M = FUN_180051e00(arc, 0.5), the drops at M (<= the mean Z) and X, then FUN_1801d3530 twice (E-M, M-X).
- FUN_18019c7c0 (dis 18019c853-18019d5cd): dz = (top - z_S) / (n + 1); n > 0 (or a zero sweep): M = the antipode of S, else the half-sweep point of
  H -> S; the drops at M (<= z_S + dz / 2) and H (<= z_S + dz); FUN_1801d3530(A, C (z_S + 0.75 dz), M (z_S + dz / 2)) with A = S (n > 0) or H at
  z_S + dz, then (M, C (z_S + dz / 4), S (z_S)); H.z = top. Within a half turn and n < 1 (dis 18019cad5-18019cb4a fall-through) another path: SAFE.
- FUN_1801d3530 (x17 l.885-1095): the list [A (flag 0x40), C (0x08, + 0x10 for side 1: traced with :l), B (0x40)]; FUN_1801cece0's corner branch
  (x10) runs FUN_1801cd2f0 (x5 l.2688ff, t = 0.5 at dis 1801cd3de) until the diamond angle at C is <= 0.2 and FUN_1801ce270 clear: the new point at
  the half sweep (the quarter point over 1.9), Z the mean of its neighbours, dropped; the connect's lifted flag +0xc8 (a rise) = blocked.
  Checked: the 15 / 15 + 15 points of retract 1's three checks = the DLL's node lists (HMM_TRACE 1801cece0:l) to 1e-11 mm.

### 30.3 Measured

On the DLL's passes (MCAM_HMM_PASSES + MCAM_HMM_WLGRAPH + MCAM_WL_LOOPS, 1024 chords per arc; link_cmp.py --eq 0.0005 in, the DLL's .moves
written with 16 chords per arc):
| op | links that take the clean path in the DLL (coverage) | port emits | equal to the DLL (link_cmp) | the others |
|---|---|---|---|---|
| TUTORIAL 2 SWARF op 4 | approach + retracts 1-14 | approach + 14 retracts | 15 / 15 (vertex-to-path <= 0.0004 in = the 16-chord sag) | retract 15 (in-check rises: the DLL retries) and 16-40 + exit (lead-out blocked) keep SAFE, as their checks fail in the port too |
| MOLD BOTTLE op 4 | approach + 18 retracts (1-4, 6, 7, 9-20) | approach + 18 retracts | 19 / 19 | retracts 5 / 8 / 21 (lead-out blocked) and the local_fe1 link keep SAFE |
Every FUN_180176540 centre = the DLL's (W< trace) to 1e-9 mm; the helix tops / starts, the tangent points, the check points (FUN_1801d3530 node
lists) equal the DLL's.
On the port's own passes (cam_cli waterline; MCAM_WL_NOLEADS=1 = before; seg_dist inch / link_stats vs the stored NCI):
| op | leads emitted (SAFE) | ref->ours p95 before -> after | ours->ref p95 / max before -> after | retracts (ref) | plunges at feed (ref) |
|---|---|---|---|---|---|
| TUTORIAL 2 SWARF op 4 | approach + 20 retracts (27) | 0.1029 -> **0.0138** | 0.0041 / 0.0997 -> 0.0124 / 0.2454 | 50 = (42) | 49 -> 69 (69) |
| MOLD BOTTLE op 4 | 26 retracts (51 + the approach) | 0.1183 -> **0.0239** | 0.0057 / 0.0318 -> 0.0124 / 0.3790 | 97 = (71) | 96 -> 122 (112) |
| SWARF_METRIC op 3 | 0 (checks: the old order, wl_link not run) | = | = | 57 = (39) | = |
ref->ours: Mastercam's lead arcs and helices now have a counterpart. ours->ref: the leads the port adds on its own extra passes (the upper levels'
contact-angle split, 22.2c: op 4 70 vs 65 passes, MOLD BOTTLE 117 vs 98) have none in the NCI (op 4's worst 0.245 in = helices at z 1.77 in, the
1.7053 level's extra passes); the port's first MOLD BOTTLE pass differs from the DLL's and its lead-in check rises there (SAFE). The retract count
does not change (a retract stays a retract; its leads replace the SAFE plunge).
- The exit side keeps the DLL's LinkRampHeightOffset line (FUN_1801194d0); the stored NCI has none on the Waterline exits (13.3: the rapid starts
  at the vertical arc's end) - the step between McHmm's output and the NCI that drops it is NOT DECODED (it lies on the rapid's own line: no
  distance either way).

### 30.4 Where M1 stops (twentieth pass) - next, in order
1. The blocked lead-out (retracts 16-40 and the exit of op 4; MOLD BOTTLE 5, 8, 21, ...): the rotation loop of FUN_18019e010 (dis 18019fe36-
   18019ff55: X turned by |angX - angE| / 6 the side's way, up to 6 rounds, FUN_18019bba0 + FUN_18019a6a0 again) and the fallback FUN_18019d5d0
   (FUN_1801d1e70 + FUN_1801dab60, the connect lead: a straight vertical move of the lead radius when blocked - 15.2, NOT DECODED), blocks
   1801a0172 / 1801a019b. Coverage traces in m1w5/l16.seq, l41.seq (op 4) list the blocks.
2. The blocked lead-in (retract 15): FUN_18019c7c0's retries along the pass (the FUN_180279f90 / FUN_1801a1240 loop, dis 18019d12e-18019d4a4)
   and FUN_180176760 (the ramp along the toolpath's GridC, e010 blocks 18019f50f-18019f862).
3. local_fe1 with n = 0 (hk l.9530: FUN_18019b180 from E), the one-arc lead-in check (n < 1 within a half turn), a rising drop at the lead tops.
4. Why the NCI has no exit offset line (13.3), and the map linker +0x430 +0x70's writer (FUN_1801af3d0).
5. Style 1's +1.0 mm on op 3 (29.3; 30.5).
   Done in the twenty-first pass (34): 1 and 2 ported for the step-0 checks (the rotation, FUN_18019d5d0 via FUN_1801dab60, FUN_18019c7c0's
   one retry, FUN_180176760); 4's map writer decoded (34.3); the rest moved to 34.4.

### 30.5 The wander threshold (orchestrator question, M1r saw FUN_1801dde40 on a 3-node mode-1 path)
Traced on SWARF op 4 (HMM_TRACE 180199220,1801deaf0:l,1801dde40): FUN_1801deaf0 runs 61 times - 39 on 2-node lists (the retract-height mode-1
connects; it returns 1, no FUN_1801dde40) and 22 on the surface ramps (10-95 nodes; FUN_1801dde40 22 times). No Waterline (mode-3) path of 3-4
nodes occurs on op 4, so wl_wander's `n < 5` early return is neither confirmed nor refuted there; with this branch's merge of
worktree-agent-aec0e44b919baf8b9 wl_wander calls the shared hl_wander (cam3d_hstlink.c). If FUN_1801deaf0 runs FUN_1801dde40 for 3-node mode-1
paths (M1r), the `n < 5` guard should become FUN_1801deaf0's own condition - to be checked on a 3-4 node mode-3 ramp (none in the corpus ops run).
SWARF op 4 on the DLL's passes: moves byte-identical before / after the merge.

### 30.6 Style 1's +1.0 on Mastercam's mesh (29.4 step 3)
SWARF_METRIC op 3 through the DLL harness with MCAM_HST_MESH=1 (the port's dump of mcmsurf's mesh, Corner rounding off as the port needs; scratch
m1w5/o3m.sh, tops.py): DLL vs NCI p50 0.0065 / 0.0040 mm, retracts 28 (ref 39); the 4 retracts whose ends match the stored ones within 2.3 mm have
their tops 1.007 / 0.989 / 1.017 / 1.078 mm below the stored tops - the same ~1 mm as on the port's mesh (29.3). So the term does not come from
the drive mesh; left: op 3's check-surface trim plans (0x141f / 0x1458) and the two-macro form (it still spins under tools/peloader; not
investigated further this pass).

### 30.7 Regression (this branch after merging master 4249f6a; regress3.sh vs master's run rg_new17)
wf 1/524, srf 0/37, chain 0/63, solid 2/83, cad, misc 5/90, stockplanes 16/125 = master; op_compare 48/637 = master's 47 + gen_waterline.txt
(BRACKET4's Waterline op 5931 -> 13014 points: the retract leads; re-blessed); units / horiz / pencil / blend / hst_rest / stockmodel = master;
fileop 0 / 205 differ; every 3D oracle identical (waterline_oracle = op 3: checks, wl_link not run). M1r's Raster cases c45 / c45s / c45z / w1-w3
on this branch: moves byte-identical to M1r's (link_cmp 186/190, 190/194, 182/182, 5/11, 6/13, 9/11).

## 31. The weave contact into Waterline's contact-angle filter (2026-11-14, N13-34; branch worktree-agent-aa8e60cca3ef18e5b)
The passes-plan difference of 22.2c (op 4 70 vs 65, MOLD BOTTLE 117 vs 98): the contact word McHmm's filter FUN_18040b6f0 reads is DECODED and
PORTED for the weave's own loops, checked vertex by vertex against the DLL. Two more differences turned up on the way and are fixed: the window's
degrees (31.2) and the weave's fibre count (31.3). Shared functions touched: cam3d_rough.c push_vertex / push_edge / push_face / push_tri / iv_take
(a contact word per interval end, computed only when c3_ct_on, i.e. Waterline's weave), C3Iv (+ w0 / w1), fib_addw, wv_add / wv_border /
wv_consistent / wv_loops (the clip char -> the 32-bit word), c3_slice (Waterline: no fib_merge, the + 1 fibre); cam3d_finish.c wl_angle_keep,
gen_waterline's window and fibre loop. Not touched: the links (cam3d_hstlink.c, wl_ramp_link and on).

### 31.1 What FUN_18040b6f0 reads (DECODED; hz = decomp/hmm_zplan.c, wd = decomp/hst3d_weave_drop.c, tb / tb2 = decomp/raster/tb.c / tb2.c)
- The level's loops (hz l.11870-11950): W = Eva +0x1430, level = Eva +0x1438; the loop heads W +0x240 [level] (u64 per loop, the start node in the high
  half); the nodes W +0x1f8 (0x1c bytes: +0 set byte, +1 end (0 lo / 1 hi), +4 fibre, +8 interval, +0x10 next, +0x18 the keep flag the filter writes);
  the node's interval: set byte 0 -> the fibres at W +0x60, else W +0x18 (+8 the 0x38-byte fibres: +8 the 0x40-byte intervals, +0x28 position); its point
  (value, position) for set byte 0, else (position, value). The contact record of the end: interval +0x10 (lo) / +0x20 (hi), 16 bytes: +0 a direction
  code (FUN_18004d7e0 of the contact point's XY: only the normal's XY uses it), +8 the contact word. FUN_1803faee0 calls the filter once per weave
  (hz l.1005; the lower levels of MOLD BOTTLE op 4 have 5-6 calls, one loop each).
- The word (FUN_180253420 tb2, called by FUN_180125b70 wd l.15072 for each end of one triangle's interval): bits 0-23 q = round(t x 0xffffff) capped to
  [0, 0xffffff], top byte = 2 layer + flag (capped 0x7f): bit 24 the flag, bits 25-30 the tool profile layer, bit 31 the clipped mark (FUN_180125b70 ORs
  0x80000000 into an end clipped to the fibre window and leaves the rest 0). t: with the flag |n_xy| / (|n_xy| + n_z) of the contact normal n; without
  it, layer 0: the contact point's horizontal distance / the profile's +0 radius; layer k > 0: the contact's parameter along the profile segment k - 1 ->
  k (the profile table at +0x40, (r, h) pairs).
- The contacts come from the drop (FUN_180128800 wd l.16375, per triangle and fibre from FUN_180130b40 wd l.22900): an accumulator holding lo / hi and, for
  each, its contact point (+0x18 / +0x30), the unit normal from the contact to the tool axis (+0x48 / +0x60), layer (+0x78 / +0x7c) and flag (+0x80 /
  +0x81), replaced with the end when a feature moves it out (a strictly lower lo / higher hi: the first feature wins a tie). The ball (profile type 3 at
  +0x10, radius +8): FUN_180126130 (vertex), FUN_180126e90 (edge), FUN_180127d50 (facet) - layer 0, flag 1. The bands above it, k = the table index:
  FUN_180125dd0 / FUN_180126340 / FUN_1801275c0 (FUN_180126b10 at a band's rim) - layer k, flag 0; on the band above the table's last point with the
  profile's +0x88 set, layer k - 1 and flag 1. Traced on both ops: the profile table holds one point, (ball radius, ball radius), +0x88 = 0, so the
  flute above the ball is layer 1 without the flag: word 0x02000000.
- The ends travel with their words (hst3d_port_plan.md 7 "The weave box and the boundary fill"): the union FUN_18005fd30, the fill FUN_180131810
  (0x80000000), the border closing FUN_18012cde0 (point intervals 0x80000000) / FUN_18012cf20, the consistency rounds FUN_180130300 (an extension
  moves the end and keeps its word, a twist point interval is 0x80000000, a join carries the joined end's word). A loop vertex is an interval end
  (FUN_1804031a0), so its word is the drop's contact of the triangle that set that end, or the old contact of an end the rounds moved.
- FUN_18040b6f0 per vertex: dropped when bit 31 is set, or FUN_1801344c0 (wd) is true - layer 0 never; Eva +0x6e0 = -1 never, 0 any layer; a layer
  above +0x6e0, or equal to it with Eva +0x6e4 = 0 and the flag set (+0x6e0 = PosContactOnly ? 1 : -1 by FUN_18040ea00; +0x6e4 = 1 by
  EvaZProfilePlan's constructor FUN_1803f9c20 hz l.349, no other writer; the trace reads 1 / 1 on both ops) - or (Eva +0x670) the normal's Z outside
  [+0x678, +0x680]. The normal: FUN_1802538d0 (tb) on the record and the weave's profile W +0xa8: with the flag nz = (1 - t) / sqrt(t^2 + (1 - t)^2),
  t = q x 5.960464832810452e-08; layer 0 without it: r = t x prof[0], nz = sqrt(prof[0x13]^2 - r^2) / |(r prof[0x14], sqrt(..))| ((0, 0, 1) when
  prof[0x12] = 0); layer k inside the table: nz = dr / |(dr, dh)| of segment k - 1 -> k; past the table nz = 0. The run restore as 7 / wl_angle_keep.

### 31.2 The window's degrees (DECODED, PORTED)
The op record holds 90 deg as 1.570796326794896 rad (just below pi / 2). mckernel (FUN_180071314 mk l.87268-87304) clamps to [0, 0x3ff921fb54442d15],
multiplies by 57.29577951308233 (89.99999999999997) and hands the value to the plan as macro text "%19.9f" (FUN_1800edd8c, mk l.183616): "90.000000000".
The plan therefore gets 90 / 50 exactly, ContactAngleTol = 1 deg, +0x678 = -0.1 (traced: "win -0.1 0.64278760968653936 -0.1 0.65605902899050728").
The port took the record's radians directly: hid = 89.99999999999997 < 90 gave a tolerance of 3e-15 deg and +0x678 = cos(Hi) = 6e-16 > 0, so every
contact with nz = 0 (the flute, vertical walls) was dropped - most of the MOLD BOTTLE split (117 -> 98 with this alone, whichever normal). gen_waterline
now rounds through the same text, converts with FUN_180389560's deg x DAT_1809bf198 / 360 (2 acos(-1)) and compares with DAT_1809bf1a0 = acos(-1) / 2;
its window = the DLL's to the bit (MCAM_WL_CTDUMP "wlwin"). (Scallop's Shallow Area Detection already did the same rounding.)
The same slip hit every Waterline with the default 0..90: a new op's record holds 0 / 1.570796326794896 too (BRACKET4's scene, MCAM_HMM_DUMP), so the
port switched the filter on with the window [6e-16, 1.1] and dropped every flute / vertical-wall contact (nz = 0 from the mesh) - the scene
gen_waterline's Waterline lost its wall passes (5503 points; now 11337, the walls round the bracket and its bores) - and refused a flat tool ("contact
angle limits with a flat tool") at 0..90. Now 0..90 gives Mastercam's [-0.1, 1.1]: no filter, as the DLL.

### 31.3 The weave's fibre count (DECODED, PORTED for Waterline)
FUN_180131690 (wd) stores max(3, int((hi - lo) / spacing) + 2) + 1 fibres, positions (1 - t) lo + t hi, t = i / (n - 1) - the + 1 was missing from
hst3d_port_plan.md 4.5 and c3_slice (traced: set +0x28 = 272 on MOLD BOTTLE op 4 where int(269.1) + 2 = 271; the ranges themselves were already the
DLL's to 1e-8 mm). Ported for Waterline's weave (c3_weave_rng) and its fill-only fibres of the inflexion box; the other styles' weaves keep their own box
(ours) and the old count (no DLL check there).

### 31.4 Ported (cam3d_rough.c c3_ct, cam3d_finish.c wl_angle_keep)
push_tri's features compute the word of each interval end they set (c3_ct_on, Waterline's weave only): the ball below the centre -> layer 0 + flag,
t = |n_xy| / (|n_xy| + n_z) of (centre - contact) (vertex / nearest edge point / facet normal turned to the centre), coded as FUN_180253420; the cylinder
above the centre -> 0x02000000 (layer 1, flag 0: the band's t is not needed by FUN_1802538d0's nz and is left 0). The triangles' intervals go into
wv_loops unmerged, in the drop's order, through wv_add (FUN_18005fd30's union, which keeps the setting end's word); the window clip ORs 0x80000000;
the fill / border / twist ends are 0x80000000. wl_angle_keep takes, for the weave's own loops (ball tools: no flat-radius offset), the word's normal
(wl_ct_nz) and FUN_1801344c0 (wl_ct_layer_drop); for the Clipper offset polygons of a bull tool (no words) the mesh normal as before (ours, 31.6).
Testing switches: MCAM_WL_CTDUMP (wlct / wlcd / wlck / wlwin lines; with a flat-radius offset also the weave loops' filter, "wlweave"), MCAM_WL_LEVELS
(levels from a file, e.g. the DLL's), MCAM_WL_MESHNZ (the mesh normal everywhere, the old behaviour); peloader HMM_TRACE=18040b6f0:W (the loops,
words, nz and keep flags at the filter); tools/tests/wl_ctcmp.py compares the two.

### 31.5 Checked against the DLL (MCAM_HST_MESH=1: the DLL's own input mesh; the port at the DLL's levels, MCAM_WL_LEVELS = weave Z + ToolThicknessZ)
| op | DLL loop vertices matched (position) | words | keep after the filter | passes port / DLL |
|---|---|---|---|---|
| MOLD BOTTLE op 4 (ball D 0.375, stock 0.05, 50..90) | **38846 / 38846** (<= 1.8e-8 mm) | identical (q 0 apart; nz <= 5e-10) | identical | **98 / 98** |
| TUTORIAL 2 SWARF op 4 (bull D 0.5 r 0.125, 50..90): the weave loops | **34620 / 34620** (<= 1.8e-8 mm) | identical | identical | 54 / 65 (the offset path, 31.6) |
The port has 4 more vertices per level: its outline loop round the grid (all clipped, dropped; McHmm nests the loops instead, 7).
Own levels: MOLD BOTTLE 98 passes with the switch off and on (master 117 / 116); SWARF op 4 62 off / 54 on (master 70 / 67).

### 31.6 Measured (inch / mm; oracle3d "cut" p95 ref->ours / ours->ref, cut length; seg_dist p95 both ways; master = 1264038)
| op | master off | this branch off | master on | this branch on |
|---|---|---|---|---|
| WL SWARF_METRIC 3 (mm, Corner rounding off) | 0.449 / 0.529, 10284; seg 0.5 / 0.5 | 0.444 / 0.530, 10290; = | 0.457 / 0.052, 9975; seg 0.5 / 0.133 | 0.454 / 0.052, 9980; 0.5 / 0.129 |
| WL SWARF 4 | 70 passes; 0.098 / 0.004 (max 0.510), 403; seg 0.102 / 0.0041 | 62; 0.098 / 0.004 (0.519), 404; 0.105 / 0.0041 | 67; 0.064 / **0.171**, 443; 0.110 / 0.0009 | 54; 0.065 / **0.171**, 444; 0.113 / 0.0009 |
| WL MOLD BOTTLE 4 | 117; 0.194 / 0.006 (max 0.032), 544; seg 0.118 / 0.0057 | **98**; 0.195 / 0.006 (0.032), 546; 0.132 / 0.0057 | 116; 0.194 / 0.002 (0.016), 543; 0.130 / 0.0009 | **98**; 0.194 / 0.002 (**0.006**), 544; 0.134 / 0.0009 |
| RA PARALLELSTEEP 5 (seg) | 0.0006 / 0.0003 | = | refused (kind 1, master has no N13-33) | 0.0007 / 0.0004 |
| RA RESTPASSES 18 (seg) | 0.0009 / 0.0004 | = | 0.0011 / 0.0007 | 0.0011 / 0.0007 |
| OptiRough TUTORIAL 7 op 5 (mm, hst3d_oracle.sh) | 1.020 / 1.032, 21344 (rg_new16) | = (regress3) | - | 1.034 / 1.028, 21218 (not touched by this change) |
MOLD BOTTLE links (link_stats, ref 71 retracts / rapid XY 84.2): master off 105 / 101.5 -> 81 / 79.4; on 96 / 88.2 -> 76 / 78.3. Its seg ref->ours p95
rises (0.118 -> 0.132 off): the stored lead / ramp moves are further from the port's links on the joined passes (the linker's part).
**SWARF op 4 with the switch on is not fixed by this** (ours->ref cut p95 0.171 on master and here, 0.004 off). It is not the links (MCAM_WL_NOLINKS:
0.172) and not the weave contact (31.5): the bull tool's passes are the Clipper offset of the weave region by the flat radius, filtered at the offset
polygon's own vertices with the mesh normal (wl_contact_nz, ours); Clipper keeps only the ends of a straight run (collinear points removed), and on
Mastercam's mesh the planar groove walls give exactly straight runs, so a whole wall side between two kept ends is cut where Mastercam's dense weave
vertices are dropped (seg_dist sees only the vertices: ours->ref p95 0.0009). McHmm filters the weave vertices and carries the keep flags through
its Voronoi offset (FUN_180406b00 -> FUN_180407c10 -> FUN_180409f40's input polygons, flags into the sites - stage 3 of W3, hst3d_port_plan.md 7),
which is NOT PORTED; a transfer of the flags to the Clipper polygons was refused as a stand-in (user 2026-11-13).

Regression (regress3.sh, this branch vs master's run rg_new16 = da1ebd0's code, master 1264038 adds a decision line only): wf 1/524, srf 0/37, chain
0/63, solid 2/83, cad 0 failed, misc 5/88, stockplanes 16/125 - the same sets; op_compare 48/637 = master's 47 + gen_waterline (re-blessed: 5503 ->
11337 points, the 0..90 window above); units / horiz / pencil / blend / hst_rest / stockmodel pass (horiz / pencil failed to link in rg_new16 -
fixed on master since, b054c77); fileop 0/205 differ; 3D oracles identical except Waterline (SWARF_METRIC 3 with links: ref->ours p95 0.539 -> 0.531,
cut 13622 -> 13628 vs 10447, the fibre count; op 4 with links: ours->ref p95 0.670 -> 0.608, cut 505 -> 493 vs 428).

### 31.7 Where this stops
- Bull / flat-radius Waterline (SWARF op 4): the flags through McHmm's Voronoi offset (W3 stage 3; stages 1-2 not ported either). Until then the
  offset polygons keep the mesh-normal filter (ours) and MCAM_HST_MESH makes SWARF op 4's cut metric worse (31.6) - question N13-34a (TODO).
  **Done 2026-11-14 (N13-35, W8, hst3d_port_plan.md 7 "W8"):** the weave loops' keep bytes now go through McHmm's own chain (pockets FUN_18012b1e0,
  the filter writer Eva +0x1440, FUN_180407c10's Voronoi offset, the pass writer, the span rule); SWARF op 4 with the switch on = the DLL's 65
  passes vertex for vertex (32 below).
- Not checked: a profile with more table points (shank / holder bands, +0x88 set) - every corpus Waterline traced has the one-point table; the port
  models the flute as one band (layer 1).

## 32. Bull-tool Waterline through mchmm's own Voronoi offset (2026-11-14, N13-35; branch worktree-agent-a722b6e517f8bf72d)
The decode and the port are in hst3d_port_plan.md 7 "W8" (the pockets FUN_18012b1e0, the filter's writer Eva +0x1440, FUN_180407c10's offset, the
span rule). Shared functions touched: cam3d_rough.c wv_loops (an optional pocket export, WvPockets / c3_weave_pk - nothing changes when it is not
asked for), viewer/hmm_pipethin.c (hpt_new_filter added), gen_waterline (the flat-radius branch, the run extraction by span flags, the Corner
rounding refusal moved), the build lists (viewer/Makefile cam_cli + CORE_C, viewer-qt/CMakeLists.txt, horiz_test.sh / pencil_test.sh: hmm_voronoi.c
and hmm_pipethin.c are linked into the apps now). Not touched: the links (cam3d_hstlink.c, wl_retract, wl_link_hst), the direct (ball) path.

### 32.1 Checked against the DLL (MCAM_HST_MESH=1; the port at the DLL's levels; scratch dllcmp.sh / cmp_passes.py / wcmp.py)
| op | passes port / DLL | element end points equal (<= 1e-6 mm) | port vertices on the DLL path | starts / ends / direction |
|---|---|---|---|---|
| TUTORIAL 2 SWARF op 4 (bull D 0.5 r 0.125, 50..90) | **65 / 65** | **2705 / 2705** (max 1.4e-10 mm) | max 7e-12 mm | all equal |
| TUTORIAL 2 SWARF_METRIC op 3 with Corner rounding (D 12 r 3; the DLL macro without the check plans) | **28 / 28** | 2889 / 2914: 27 passes exact, the lowest level (z 8.486) not | max 5e-12 mm | 27 equal |
SWARF_METRIC op 3's lowest level: the offset writer's calls are the DLL's (FUN_1801192a0 / 1801194d0 / 1801195f0 traced: 21082 / 21083 calls equal
to 1e-7 mm with their flags over all 28 levels - the one extra is a zero-length line the DLL writes after an arc on level 22, which the writer
ignores) and mchmm's own writer + flush on that pocket in the --hmmvor harness gives the port's record (MCAM_WL_VORCASE: 28 cases identical with
msvcr120's libm, 2 last-bit lines with glibc); the plan's pass there starts inside an arc (3.38, -29.80) and has four more split points near
(52.5, -35.1) - a step after FUN_18011ca00 on that level, NOT DECODED. The order of the open pieces of one record differs on a few records (SWARF
op 4; the pieces themselves are equal). MOLD BOTTLE op 4 (ball): not on this path (Fo = 0; WeaveSubdivision is on there - traced Eva +0x1001 = 1 -
and FUN_18040af10 is NOT DECODED, so MCAM_WL_VOR_ALL (testing) gives 3607 / 4182 equal end points against the direct path's 3833).

### 32.2 Measured (inch / mm; oracle3d "cut" p95 ref->ours / ours->ref and cut length; seg_dist p95 ref->ours / ours->ref; base = this branch before
the wiring = master fb90997 + the weave contact)
| op | base off | W8 off | base on | W8 on |
|---|---|---|---|---|
| WL SWARF 4 | 0.011 / 0.005, 420; seg 0.0126 / 0.0044 | 0.006 / 0.004, 412; seg 0.0053 / 0.0075 | 0.011 / **0.169**, 460; seg 0.0152 / 0.0045 | **0.005 / 0.002**, 411; seg 0.0039 / 0.0057 |
| WL SWARF_METRIC 3, Corner rounding off (diagnostic) | 0.444 / 0.530, 10290; seg 0.5 / 0.5 | 0.446 / 0.530, 10289; = | 0.454 / 0.052, 9980; seg 0.5 / 0.129 | 0.461 / 0.052, 9980; seg 0.5 / 0.189 |
| WL SWARF_METRIC 3 as stored (Corner rounding on) | refused | 0.446 / 0.529, 10274; seg 0.5 / 0.5 | refused | 0.461 / **0.051**, 9965; seg 0.5 / **0.017** |
| WL MOLD BOTTLE 4 (ball, direct path) | 0.098 / 0.006, 566 | = | 0.097 / 0.002, 564 | = |
| RA PARALLELSTEEP 5 (seg) | 0.0006 / 0.0003 | = | 0.0007 / 0.0004 | = |
| RA RESTPASSES 18 (seg) | 0.0009 / 0.0004 | = | 0.0011 / 0.0007 | = |
Stored cut lengths: SWARF 4 425, SWARF_METRIC 3 10239, MOLD BOTTLE 4 603. SWARF 4 links (link_stats, ref 42 retracts / rapid XY 51.2): base off 40 /
52.2, on 27 / 37.1 -> W8 off / on 44 / 41.9. The W8 passes carry McHmm's arcs as chords (arc_tol), so the port has about a third of the vertices
it had (25745 -> 8417 cut moves on SWARF 4): seg_dist's ours->ref p95 counts vertices and moves with that (0.0044 -> 0.0075 off), the sampled cut
metric improves (0.005 -> 0.004 off, 0.169 -> 0.002 on). SWARF_METRIC 3 with Corner rounding off is a diagnostic only (the stored op has it on).
The port's linker on the DLL's passes (M1W5's check, MCAM_HMM_PASSES + WLGRAPH: SWARF op 4, MOLD BOTTLE op 4) is byte-identical to M1W5's own
cam_cli: the links are unchanged by the merge and the wiring.

Regression (regress3 of this branch vs master's run rg_new17; master fb90997 differs from it only by gen_waterline): wf 1/524, srf 0/37, chain 0/63,
solid 2/83, cad 0 failed, misc 5/90, stockplanes 16/125, op_compare 48/637 = master's 47 + gen_waterline; units / horiz / pencil / blend /
hst_rest / stockmodel pass; fileop 0/205 differ; 3D oracles identical except waterline_oracle (SWARF_METRIC 3, Corner rounding forced off):
ref->ours p95 0.539 -> 0.517 mm, cut 13622 -> 13627. mchmm Voronoi oracle seeds 1-5: 30 runs identical. **gen_waterline (BRACKET4's default
Waterline, a bull tool) is now refused** at Z -0.975 ("the Voronoi construction failed ... FUN_180407c10's fallback is NOT DECODED"): the pocket
dumped with MCAM_WL_VORCASE fails in mchmm.dll's own construction too (peloader --hmmvor: the 7 cases RES-identical, 6 built, this one not), so
the DLL would take the fallback hzp l.253-586 there; golden re-blessed (13014 points -> refused; M1W5's leads and the weave contact are in it
otherwise). The drive is a solid (our OCCT mesh, not Mastercam's), so whether Mastercam's own input fails as well is not known.

### 32.3 Where this stops
- NOT DECODED (refused on the Voronoi path): FUN_180407c10's fallback (AllPolygonsChecked or the construction failing) and FUN_1803faee0's retry
  that depends on it; WeaveSubdivision's FUN_18040af10; K+0x472 (ToolHolderDefinition -> Eva +0x15c0); XY stock other than Z stock; the step after
  FUN_18011ca00 seen on SWARF_METRIC op 3's lowest level; the order of the open pieces of a record.
- Ball tools (Fo = 0) keep the direct path: the DLL runs the same chain there with r' = 0 (thinning, arcs), but with WeaveSubdivision on (the
  default for a ball: VorProfileFlatRad 0 < 1.5 MachWeaveResolution) it needs FUN_18040af10 first.

## 33. Twentieth pass (2026-11-14, M1s, Raster; numbered 30 on its branch, 31 before the merge with master c59ce7c): rising first approach / final exit, the pass trimming, LinkTrimmingStyle 2 PORTED

Code: viewer/cam3d_finish.c (c3_link, c3_link_end, c3_lk_one, c3_lk_s2, c3_lk_cut_prev, c3_lk_trim_out / _in, c3_emit_run's trimmed start),
viewer/cam3d_hstlink.c (hl_rise now hands back the fit, hl_at3 / hl_len3, hl_arc_to, hl_trim_arcs). Scratch (<scratch>
m1s2, not committed): syn3.sh / run.sh / dll.sh as in 28b (run.sh now also passes 0x13a0 LinkMaxAllowedTrimming and 0x16d1 LinkTrimmingStyle),
plates.py (horizontal check plates), fitcmp.py (FUN_180171b50 call by call), arccmp.py (FUN_1801713f0's FUN_18011a840 arcs: end + centre vs the
port's "la arc" lines), allsyn.sh (every synthetic case, port on the DLL's passes), nc2moves.py. HMM_TRACE `:o` / `:s` of that branch are `:O` / `:S` in the merged harness (M1w5 took `:o` = an rcx offset, `:sN` = N stack arguments). `:O` on FUN_1801a47b0 with `:S` crashes
the harness (a stack argument that is not a pointer): trace FUN_1801713f0 / FUN_18011a840 `:O` instead.

### 33.1 The first approach / final exit leads that rise (FUN_180194140 hk l.3091-3205, FUN_180197b80 hk l.5461-5559)
- The same connect path as the retract's sides (c3_lk_one): the first FUN_1801db250 / FUN_1801dab60 call with the attempts flag - approach:
  LinkTrimmingStyle 1 and LinkMaxAllowedTrimming > 0 (+0x110b; kinds 2..4 never), exit: style != 0 and > 0 (+0x110a); the fit test |trim| <=
  LinkMaxAllowedTrimming and < the pass's XY length (approach hk l.3131-3138, exit dis 1801988e0-180198907: `<=` then `<`, Ghidra's rendering
  of l.5498-5501 is misleading); not fitting: the nodes cleared, the second call without the attempts -> FUN_1801d8630's vertical lead.
- An arc refinement that rises (FUN_1801cc840's rise branch, connect +0x88 = 0) takes the same path as a rising top: it hands C and the lifted
  midpoint to the connect's virtual +0x18 (code 5), so FUN_1801d9330 (x10 l.370-490) returns 1 and FUN_1801d87b0 / FUN_1801d76a0 follow with
  R / d taken before the drop (FUN_1801d7450 x8 l.598-616); inside FindBetterArc FUN_1801d8b30's FUN_1801d91e0 then finds no arc node: the fit
  is bad (0). Traced on e1's first approach: FUN_1801d8240's 8 fits follow (both were SAFE before).
- c3_lc_arc's recursion bound (ours) 60 -> 2000: a lead-in whose S lies a hair (1e-11 mm) past the bottom of the circle fails the 90 deg rule
  until the halved arcs' ends coincide, 60-80 halvings down (s4_check_trim's first approach on the DLL's passes was SAFE for this alone).

### 33.2 The pass trimming of a fitting lead (FUN_1801a44a0 / FUN_1801a47b0) PORTED
- Retract (hk l.12740-12760, 12935-12990): a nonzero exit trim cuts the previous pass (FUN_1801a44a0 param_5 = 0: from the end back by |v|, 3D
  length - FUN_180279f90 -> FUN_18027ca10 on line segments), it is emitted to P, then FUN_1801a47b0 (exit), the connect (FUN_1801a4da0), then for
  a nonzero approach trim FUN_1801a44a0 (param_5 = 1: the next pass from the start + v) and FUN_1801a47b0 (approach); after the second
  FUN_1801d69f0 call the approach's fit flag is forced 1 (l.12895: its trim is then 0 or a refitted fitting one). First approach hk l.3196-3205,
  final exit hk l.5546-5559 the same with one side. The previous pass is already in the move buffer when the retract is made: c3_lk_cut_prev
  takes its moves past P back off (checked against the pass's points, else SAFE before anything is emitted).
- The lead nodes after a fit: FindBetterArc's success leaves the fitted arc [T', C', Q] (FUN_1801d8ce0; approach: p6 / p7 / p8, exit: p8 / p7 /
  p6 of hl_fit) - the connect's end pointer moves one node (x11 l.98-108); FUN_1801d7b00's success the one node Q (its pass-side point, x8
  l.226-290). The connect keeps for FUN_1801a47b0: v (+0x1e8 / +0x1e0), c0 (+0x220 / +0x228 = FUN_180171b50's param_1[1]), a0 (+0x1f8 / +0x1f0 =
  the bisection's overlap - its e8; 0 after FUN_1801d7b00), the closed-pass count (+0x234 / +0x230; nonzero: NOT DECODED, SAFE).
- FUN_1801a47b0 (hk l.13223-13508): positions a / b on the pass (approach: start + c0 / start + v; exit: end - v / end - c0, FUN_180279f90), the
  point written first (FUN_180118ee0 code 0x19: approach (a.xy, Q.z), exit a = P), then FUN_180170f00 (hc l.2021-2169: approach acc = a0, exit acc
  = the XY length a -> b (FUN_180279ea0 = FUN_18027c660(.., 1)) + a0; the angle bookkeeping writes nothing) -> FUN_1801713f0 (hc l.2170-2390) per
  segment of FUN_18027f780's iterator from a to b: t = R - acc, centre C = p + dir t (exit: dir negated) at p.z + sqrt(R^2 - t^2), acc +/-= the
  segment's XY length, q = (end.xy, C.z - sqrt(R^2 - (R - acc)^2)); the arc FUN_18011a840 p -> q about C is written where FUN_180280c80 (dis
  180280c80-180280cd6) is 1: the iterator's last segment always; before it only with the iterator's +0x28 set and FUN_1802810a0 (dis
  1802810a0-1802810e9) finding +0x30 / +0x38 both 0 - NOT DECODED which array that is; on every traced Raster call it was 0 before the last
  segment (e1 4, e2 50, e3 56, e4 64, s4 17 calls: one arc each, ret 18017199e, or the final write ret 180171b2a when the last segment has XY
  length 0). The -0.998 half-circle split (an arc to C - R z first) is ported, never met. Arcs as lines with c3_lk_arc's rule; the approach's
  pieces at the plunge rate, the exit's at the feed (as the plain leads).
- c3_emit_run starts the next pass at P (the points past the 3D length v), and keeps that trimmed pass for the next exit's FUN_1801dbf80.

### 33.3 LinkTrimmingStyle 2 (record 0x16d1 = 2) PORTED
- FUN_1801db570 (x7 l.1-120; the exit's FUN_1801daf90 the mirror) with param_4 = 1: no FUN_1801db820 lead nodes, FUN_1801d7b00 with the lead
  radius (+0x1128 LinkVerticalLeadInRadius / +0x1130 Out; 0 only when the frag's length +0x1d8 = FUN_180279600 <= 0); v != 0 -> the one node Q,
  v = 0 -> no lead node. Callers with param_4 = 1: the retract's first FUN_1801d69f0 call (both sides; the exit with param_6), its second call
  for a side whose trim fits (hk l.12868-12880), the first approach's and final exit's first calls; their second calls pass 0 with the attempts
  off (+0x110b = 0). The fit test, the trimming and the second call as in 33.2 (c3_lk_s2).

### 33.4 Checked (port on the DLL's passes, HMM_TRACE; synthetic cases on c45's valley STL, record patches given to both)
| case | what | FUN_180171b50 | trimmings (FUN_1801a44a0 / arcs) | FUN_1801d8630 | link_cmp equal (0.0001 in) |
|---|---|---|---|---|---|
| s4_check_trim scene (DLL passes) | final exit rises, trim does not fit | 136 / 136 | - | 17 / 17 | final exit = DLL |
| e1 (c45 + 2 plates: check plate x -6..-0.5, y 28..34.65, z -0.87 mm) | first approach refinement rises; 3 retract trims | 465 / 465 | 4 / 4, arcs 4 / 4 exact | 53 | 184 / 189 (5 arc chords, 0.0001) |
| e2 (c45, LinkMaxAllowedTrimming 0.25 in) | retract trims both sides | 250 / 250 | 50 / 50, arcs exact (8.9e-15) | 0 | 133 / 140 (7: 0.0001-0.0002 arc chords: the harness writes the multi-segment arc with fewer chords) |
| e3 (e1, 0.25 in) | + the first approach trimmed | 277 / 277 | 56 / 56 exact | 1 | 129 / 137 (chords) |
| s4_check_trim with 13a0 = 10 mm (dump .rec patched) | FUN_1801d7b00 one-node trims, final exit trimmed | 85 / 85 | 17 / 17 exact | 0 | - |
| e4 (c45, style 2, 0.25 in) | style 2: 6 sides without a lead (v = 0) | 70 / 70 | 64 / 64 exact (4 final writes) | 0 | 128 / 131 (chords) |
| e5 (c45, style 2, 0.05 in) | style 2, the second calls | 72 / 72 | 14 / 14 exact | 50 / 50 | 180 / 180 |
Every other synthetic case (c45 / c45s / c45z, ct1-3, v80, f6 / f10, h*, v60-75, w1-w3): the port's output byte-identical to the eighteenth pass.

### 33.5 Measured
- Regression (regress3 of 8604280 = this branch after merging master 4249f6a, against the orchestrator's master run rg_new17): wf 1 / 524, srf 0 / 37, chain 0 / 63, solid 2 / 83, cad 0 failed, misc 5 / 90, stockplanes 16 / 125, op 47 / 637 - every DIFF list = master's; the normalised op outputs = master's except the 4 s4_check_trim .txt / .nc files of the M1r branch (its re-blessed goldens, unchanged here: their own passes have no rising first approach / final exit and no fitting trim at 1 mm); fileop 0 / 205; unit tests = master; 3D oracles identical; checktrim_test all ok (stops 35 / 65, 34 / 66); PARALLELSTEEP op 5 / RESTPASSES op 18 byte-identical to M1r. No golden re-blessed.

### 33.6 Where M1 (Raster) stops (twentieth pass) - next
1. The wander's arc branch FUN_1801de3a0 / FUN_1801de630 (and FUN_1801ce930 / FUN_1801e0d70 for 0x40 nodes): NOT PORTED. No Raster path
   reaches it: the connects that wander (FUN_1801d1e70 arg 13 = 1: kind-1 links, the mode-1 retry, the extension loop's last connect) hold
   line nodes only, and the lead connects (the only Raster connects with arc nodes) run without the wander flag (+0xa0). It belongs to
   Waterline's mode-3 / arc paths (the Waterline linker work), to be checked against a Waterline trace there.
2. FUN_1802810a0's iterator array (+0x20 / +0x28 flag, +0x30 / +0x38 doubles: FUN_180008780 of the tessellation's +0x68): which data it is is
   NOT DECODED; on every traced Raster trimming the arc came out at the last segment only (hl_trim_arcs does that).
3. The closed-pass count (+0x230 / +0x234, FUN_1801d7b00 / FUN_180171b50's param_9) and the connect mode's neighbour frag (+0x208 / +0x210):
   a nonzero value keeps SAFE / is never set on Raster; arc nodes in the lead walk (Raster passes have none).
4. Shared functions changed in this pass (Waterline's linker may call them): hl_rise (new HlSide argument; behaviour as before), hl_check (an
   arc refinement that rises = a bad fit, was "NOT DECODED"), c3_lc_lead_r (a refinement rise = 2, was 0), c3_lc_arc (recursion bound 2000);
   hl_wander untouched. New: hl_at3 / hl_len3 / hl_arc_to / hl_trim_arcs, c3_lk_one / c3_lk_s2 / c3_lk_cut_prev / c3_lk_trim_out / _in.

## 34. Twenty-first pass (2026-11-14, M1w6; numbered 32 before the merge with master c59ce7c): the blocked Waterline leads PORTED (FUN_18019e010's rounds, the ramp lead-in FUN_180176760, the lead-out fallback FUN_18019d5d0); the parent map's writer DECODED

Branch worktree-agent-a9c0ae11b074b90d4 (merges worktree-agent-a7ddf1635849485e0 = section 30, worktree-agent-a054bf162af18f8d9 = section 33,
master fb90997). Code: viewer/cam3d_finish.c wl_retract (rewritten as FUN_18019e010's round loop), wl_chk_out / wl_chk_in (1 clear, 0 blocked,
-1 NOT DECODED), wl_step0, wl_ramp (FUN_180176760), wl_d5 (FUN_18019d5d0); WlLead gained tstyle / maxtrim (LinkTrimmingStyle /
LinkMaxAllowedTrimming, mckernel's linking block mk l.89237-89272 as for Raster) and the counters nfall / nramp. Shared functions touched: none
(c3_lk_lead, c3_lc_lead_r, c3_lc_r, c3_lk_test, c3_lk_pathz are called, unchanged). Scratch m1w6/ (not committed): t1.tr / t2.tr (HMM_TRACE
lists: the lead functions with :O / :S, FUN_1801dab60's callees), summ.py (per link: the lead calls and their results), wl.sh (the port on the
DLL's passes with MCAM_HMM_ITEMS + MCAM_WL_LOOPS, then sdn2.py and link_cmp), ab.sh (own passes vs NCI).

### 34.1 What the DLL does with a blocked lead (traced: o4t1.tr / mbt1.tr, summ.py; decoded from dis 18019ec0b-1801a0fc8)
Per link (SWARF op 4): approach + retracts 1-14 one clean round; retract 15: in check blocked -> ramp lead-in, one round; retracts 16-40 and
the exit: out arc blocked, in check blocked -> ramp, FUN_18019a6a0 out blocked every round, 3-6 rounds, then FUN_1801dab60 + FUN_18019d5d0.
MOLD BOTTLE: retract 5 (round 1 out vertical blocked, rotation, round 2 clear: the lead-out arc to the rotated X), 8 / 21 (6 rounds, helix
lead-in, fallback), 23-69 mostly ramp + fallback, 57 / 69 (out arc blocked, vertical at E clear -> fallback anyway), exit fallback.
- Locals of FUN_18019e010 (rbp = entry rsp - 0xf68; p_k at rbp + 0xf90 + 8 (k - 5)): p9 E, p10 C_out, p11 R_out, p12 side_out, p13 z_E, p14
  "run the out check" (FUN_18019bba0's return, 1 on every traced call), p17 C_in, p18 S, p19 R_in (0 once the ramp is taken), p20 side_in, p21
  z_S, p22 "run the in check", p23 / p24 the lead tops out / in; -0x50 X, -0x70 H; 0xf70 = FUN_18019bba0's param_13 ("bl": blocked, X put back
  to E); -0x7b = FUN_18019c7c0's blocked flag ("sil"); -0x80 = FUN_18019a6a0 in of the round (read as register bl at 18019ef8d / 18019fa1b /
  18019fe18 before it is rewritten at 18019fe15, i.e. the previous round's: "alp"); -0x7c = the ramp taken; 0x130 = angle(E), xmm13 =
  angle(X) about C_out, 0x1c8 = the step |angle(X) - angle(E)| x 1/6 (DAT_1807bb580; both angles wrapped to [0, 2 pi) with DAT_1809bf198 =
  2 pi, written by the static initialiser at 180002300 = pi x 2.0); 0xa8 = the rotation counter.
- FUN_18019bba0's blocked result (hk l.7880-7990): the drop at X above the lead Z, the drop at the half-sweep point above its Z, or the check
  connect's lifted flag -> *param_13 = 1, then the retry loop with the step min(0.1, 0.1 LinkMaxAllowedTrimming) - 0 when the frag is closed
  (+0x78) or neither plan +0x110b (style 1) nor LinkTrimmingStyle 2 (hk l.7682-7700): the one retry at the same end point gives the same circle,
  E and X are put back (X = E, l.7962-7975 / LAB_18019c6a0). FUN_18019c7c0 the same (hk l.8177-8195, retries dis 18019d12e-18019d4a4). Ported
  for the step 0 only (wl_step0); a nonzero step keeps SAFE ("out retries" / "in retries").
- The ramp (dis 18019ef8d-18019f862): when sil or alp, with plan kind 0, frag +0xb9 set, plan +0x1054 / +0x1249 clear (traced: the path is
  taken on both ops) and tan(MaxRampAngle) > 0: FUN_180176760(out, list, the next frag's toolpath item (CTessToolpath +0xa8 + frag +0xd0 x 0x40:
  its GridC and the toolpath's MsArray), FUN_18027b030(frag) = S's item position, top, tan(MaxRampAngle), frag +0x79) - x17 l.244-600: the
  item walked from S against the pass's direction, Z rising by XY length x tan, every node put in front of the list, the node that passes the top
  cut at that fraction (FUN_18000b4e0) at the top Z; out = the top node and the direction to the next node. Empty list: the pass starts plainly
  (R_in = 0; not ported: SAFE "ramp").
- The vertical leads: out along the circle's tangent at X unless bl (or R_out <= 0): then along the previous frag's end tangent (FUN_18027ad40,
  dis 18019f968-18019f9e3); in along the helix circle's tangent when R_in > 0 and neither sil nor alp, else along the ramp's direction
  (dis 18019fab7-18019fb0a), else the pass's start tangent. Checked on link 38 of op 4: the DLL's lead-in arc centre = H + d R sin(10 deg),
  top + R cos, the ramp's slope 0.176325 = tan 10 deg.
- The decision (dis 18019fe18-18019ff6d): alp and al -> writers; not dil -> writers when not al, else another round; dil and al -> another
  round; dil alone -> counter + 1, the 6th round ends it; else the angle of X steps the side's way (side 0 down), and when it passes angle(E)
  the loop ends (1801a0048 writes p23 = X + d Rv_out at zb + Rv_out, replaced below); with bl the new X = C_out + R_out (cos, sin)
  (18056997c = cos, 180569964 = sin). With the step 0 the out check's trim local 0x8 never changes, so the 0x1d0 / 0x8 comparison always lets
  the rotation run.
- The writers: bl or dil -> FUN_18019d5d0 (1801a019b-1801a01fe): FUN_1801d1e70 (the retract's arguments) and FUN_1801dab60(connect, out, prev,
  param_4 = param_7 0 || frag +0x78, param_5 = LinkTrimmingStyle != 0 && LinkMaxAllowedTrimming > 0, off for a closed frag or plan kinds 2..4)
  -> FUN_1801daf90 -> FUN_1801d7450: traced (t2.tr) on all 26 calls of op 4: param_4 = param_5 = 0, FUN_1801d9330 1, FUN_1801d87b0,
  FUN_1801d76a0 -> FUN_1801d8630 (the vertical lead E.z + Rv_out), the result's top 2.54 mm = Rv_out 1.905 + LinkRampHeightOffset 0.635 above E;
  no FUN_1801a47b0 (the trim 0). The port: c3_lk_lead / c3_lc_lead_r (the Raster exit check): the lead arc when clear, else the vertical lead,
  then the offset node; param_4 = 1 (a closed frag: the skip path walks round, NOT DECODED) and param_5 = 1 keep SAFE. al -> FUN_18019db50
  (not ported: SAFE). Else the lead-in writer as before, then the ramp nodes (FUN_180177080: the list from the top to S, lines; the item's arcs
  come as the passes' chords) or the helix.
- A round that the DLL would repeat unchanged without the counter (not dil / al again with the same state) keeps SAFE ("round repeats"): not
  on the traced ops.

### 34.2 Measured
On the DLL's passes (wl.sh: MCAM_HMM_PASSES + WLGRAPH + MCAM_WL_LOOPS + MCAM_HMM_ITEMS, link_cmp --eq 0.0005 in):
| op | links equal to the DLL before -> after | retracts emitted (+ approach) | still SAFE (reason) |
|---|---|---|---|
| TUTORIAL 2 SWARF op 4 | 37 -> **49 / 57** (the order differs from link 56: the passes' set, 22.2c) | 14 -> 27 (12 fallbacks, 13 ramps); every emitted comparable link = the DLL | 12 "input" (no MCAM_WL_LOOPS loop through the pass: the gloops trace lacks those regions), 1 fe1, 1 FUN_18019db50, 1 a6a0 undecided |
| MOLD BOTTLE op 4 | 47 -> **69 / 90** (retracts 5 / 8 / 21 and the exit now equal) | 18 -> 47 (25 fallbacks, 18 ramps) + the exit | 11 "ramp: no item" + 5 "start off the item" (MCAM_HMM_ITEMS's no-filter loops do not reach every open pass / lie 1e-4 mm aside, 28.3), 5 a6a0 undecided, 1 fe1 |
The surface ramps on the same runs: 22 / 22 (op 4) and 28 (27 node for node + the first, 28.3) = before (sdn2.py).
On the port's own passes vs the stored NCI (ab.sh; seg_dist inch / link_stats):
| op | leads emitted (SAFE) | ref->ours p95 / max | ours->ref p95 / max | retracts (ref) | plunges at feed (ref) |
|---|---|---|---|---|---|
| TUTORIAL 2 SWARF op 4 | 21 (27) -> 40 (8) | 0.0138 / 0.500 -> **0.0110 / 0.171** | 0.0124 / 0.245 -> 0.0137 / 0.285 | 50 (42) | 69 -> 106 (69) |
| MOLD BOTTLE op 4 | 27 (52) -> 79 (16) | 0.0239 / 0.500 -> **0.0132 / 0.215** | 0.0124 / 0.379 -> 0.0248 / 0.379 | 97 (71) | 122 -> 208 (112) |
| SWARF_METRIC op 3 | = (wl_link not run) | = | = | 57 (39) | = |
ref->ours: Mastercam's ramps and vertical exits now have their counterpart. ours->ref and the pure-Z feed count: the fallback exit writes the
vertical lead and the LinkRampHeightOffset node, the NCI has the vertical lead only (13.3: the step that drops the offset line is not decoded),
and the leads on the port's extra passes (22.2c) have none in the NCI.
Regression (regress3 of this branch, against master's run rg_new17): wf 1 / 524, srf 0 / 37, chain 0 / 63, solid 2 / 83, cad 0 failed, misc
5 / 90, stockplanes 16 / 125 - every DIFF list = master's; op_compare 48 / 637 = master's 47 + gen_waterline.txt (BRACKET4's Waterline op 13014
-> 13355 points: the fallback exits, ramp lead-ins and the final exit; re-blessed); units / horiz / pencil / blend / hst_rest / stockmodel =
master; fileop 0 / 205 differ; every 3D oracle identical (waterline_oracle = op 3: wl_link not run).
That run is of 159de30 (before the merge with master c59ce7c). After the merge (d47368d): the build is clean, SWARF op 4 / MOLD BOTTLE on the
DLL's passes give the same 49 / 57 and 69 / 90, every M1s synthetic Raster case (m1z / m3 / m1s2 syn) byte-identical to M1s's final run;
gen_waterline's golden is master's (VOR refuses BRACKET4's Waterline before the linker). **Stopped here (session wound down): the regress3
run of the merged tree was started and stopped in wf_compare - not completed.**

### 34.3 The parent map FUN_1801af3d0 reads (DECODED: its writer)
Dumped decomp/hmm_linker_x18.c (FUN_18018[2-6]*, not committed). The object at linker +0x430 is the CFragAdjacency holder (FUN_180182e20, x18
l.681-1280; clear FUN_180183a50, free FUN_180182ba0): +200 the region table {region index (the toolpath's +0xc0), toolpath key} built from the
toolpath list and sorted (FUN_180188540), the gaps filled with null keys; +0x80 the region graph; **+0x70 the map toolpath key -> {EntityList
+0x10, EntityList +0x40}**: for every adjacency edge (a, b) of the CFragAdjacency's edge vector (its [4] .. [5], int pairs) with both regions
holding a toolpath: map[tp(a)].+0x40 gets tp(b) and map[tp(b)].+0x10 gets tp(a) (an edge whose region a has no toolpath only creates map[tp(b)];
x18 l.899-990); each list sorted by key or appended by its flag (+0x28 / +0x58, FUN_18000eab0 / FUN_18000dff0). FUN_1801af3d0 reads the +0x40
list (node + 0x68, its flag node + 0x80): so the toolpaths of the regions an edge leads to from the pass's own region - direct edges only, no walk
through regions without a toolpath (that walk is FUN_180183b80's, x18 l.1327-1617, a different reader). That is what wl_vtop does (the direct
parents holding passes): the trace-only reading of 30.2 is now the decoded rule. The list's flag default (FUN_1801877e0, not dumped) decides
"the first entry" = the lowest key or the first edge; wl_vtop takes the lowest node index (unchanged; no corpus case with two such parents
traced).

### 34.4 Where M1 stops (twenty-first pass) - next, in order
1. (Done, same branch: the final exit FUN_180197b80 -> FUN_18019e010 with no next frag runs the same rounds without the in side; X = the
   quarter turn from E about C_out the side's way, dis 18019eaab-18019eb02 = the path traced on op 4's exit (l41.seq); then up to
   LinkZClearance as c3_link_end. MOLD BOTTLE's exit = the DLL (link_cmp 68 -> 69 / 90); op 4's lies past the order difference.)
2. FUN_18019db50 (the in vertical lead blocked, 1801a0943), the undecided FUN_18019a6a0 (FUN_1801cea60's node removal), local_fe1 with n = 0,
   the checks' nonzero retry step (open passes with LinkTrimmingStyle 1 / 2), FUN_1801dab60 with param_4 / param_5 = 1 for Waterline.
3. The NCI's missing exit offset line (13.3; now also after every fallback exit): the step between McHmm's output and the NCI. A lead for the
   next pass: with LinkRetractStyle set (+0x1108) FUN_18019e010's normal exit writer puts the offset line FUN_1801194d0 into the exit path
   local_a08 and then starts the retract frag local_4b8 at the vertical arc's end local_910 / local_900 (hk l.10350-10425, FUN_180118ee0 tag
   0x19) - so the retract itself begins below the offset point; how the NCI writer joins the two (which of the overlapping moves it keeps) is
   not read.
4. Op 3's +1.0 mm (29.3, 30.6): the check-surface plans or the two-macro form - not started this pass.
5. The test inputs: MCAM_WL_LOOPS lacks some regions' loops on op 4; MCAM_HMM_ITEMS misses / misplaces some open passes' items on MOLD BOTTLE.
## 35. N13-36 / N13-37 (2026-11-14, branch worktree-agent-ae2b6e5dad11bd410): where this stopped
- **N13-36 (MCAM_HST_MESH on by default): NOT flipped.** The code takes the user's rule for solids (a drive holding a solid keeps today's mesh for
  the whole drive - OCCT for the solid, mcs_mesh for its surfaces - since Mastercam's Parasolid faceter (make_stl_model_from_sld_face, mcmsurf
  FUN_180229ba0) cannot run; viewer/cam3d_rough.c c3_mesh_refs, citing N13-36), but the default stays OFF (unset = off, MCAM_HST_MESH=1 on): the
  session was wound down before this branch's regress3 run; master c59ce7c's run (rg_master, session scratchpad meshon/) gave wf 1/524, srf 0/37,
  chain 0/63, solid 2/83, op 47/637, misc 5/90 (= rg_new17). Next: regress3 of this branch with the default on, explain every changed scene /
  oracle (expected: Waterline better, Raster +0.0001-0.0002 in, SWARF_METRIC 3 0.446 -> 0.461, OptiRough mixed), then flip the one line.
- **N13-37: FUN_180407c10's fallback PORTED and checked against the DLL** (hst3d_port_plan.md 7 "W9", tools/tests/hmm_vorfix_oracle.sh: 27 / 27
  fallback cases byte-identical, 100 / 100 constructions identical; mchmm Voronoi oracle seeds 1-5 30 / 30 unchanged). Correction: on
  gen_waterline's own dump mchmm.dll never fails a construction; the port's pockets differ from the DLL's from the second Voronoi level on
  (FUN_1803fc9b0's per-pocket weave bookkeeping, NOT DECODED). gen_waterline is still refused, now at Z -6.975 ("pocket's Voronoi failed (0x4c3,
  18 tries): FUN_1803fc9b0 NOT DECODED") instead of Z -0.975; golden re-blessed for that message only.
- Not started: WeaveSubdivision FUN_18040af10 (hz l.11453, 386 lines), XY != Z stock, K+0x472.
