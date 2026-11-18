# Port plan: mcce.dll Dynamic engine → viewer/mcce_dyn.c (plan items P1 / P2 / P3 / P8)

Started 2026-10-15. Source of every rule: `formats/mcce_engines.md` (cited as §x), `formats/dynamic.md`, and the decompile
`decomp/mcce_all.c` (cited as `l.N`). Rule: **no improvisation**. Anything the spec marks NOT DECODED is stubbed in the
code with a `NOT DECODED` comment and listed in §5 below.

## 0. Goal and switch criterion
- New engine in `viewer/mcce_dyn.c` / `mcce_dyn.h`. It replaces our own clearing pattern in `viewer/cam2d_dyn.c`
  (`gen_dynamic`) for **Dynamic Mill (style 0), Dynamic Contour (style 2), Peel Mill (style 3)**. Area Mill (style 1)
  is not mcce (§0): see area_mill.md 11.
- Switch criterion (met 2026-10-15, §6): at least as close to Mastercam's stored NCI on `tools/oracle.py`
  (ORACLE_FILEOP=1, `--type dynamic`) as the old generator. **Current state (2026-10-16, improvisation_review.md CAM
  item 7):** the old generator and its `MCCE_DYN` switch are removed. Dynamic Mill always runs the port; since 2026-10-23 (HST engines pass,
  opdlg_port_audit.md "HST engines (post-batch 9)") Dynamic Contour (closed chains) and Peel Mill run it too. A refused stored op keeps Mastercam's NCI.

## 1. Structure of the port (mcce function → C function)
All C functions are `static` in `mcce_dyn.c` unless listed in the header. Names keep the mcce address as a suffix in
comments so each can be checked against the decompile.

| mcce | C | spec |
|---|---|---|
| mcmill `LoadDynPockParm` FUN_1801af380 (mcmill_hst2d_dyn.c 1497-1780) + tool geometry FUN_1801afa30 (4420) | `mcce_load_parm()` (from the op record when the op comes from the part; CamParams + the function's own defaults otherwise) | dynamic.md, §3 D 1.1 |
| defaults FUN_180068ff0, registry FUN_180069840 (values = the registry defaults of an installed Mastercam), validation FUN_18006a3b0, derivation FUN_180069560, march step FUN_180069350 / FUN_180068e10 | `parm_derive()` | §3 B.2 |
| MCCECalcDynPock 12159 → FUN_180090290 (B.3: START_INSIDE FUN_180090fa0, START_FACING FUN_180090ae0, S2 FUN_180091100, Dynamic Contour FUN_180092950) | `mcce_calc_dyn_pock()`, `prep_inside()`, `prep_facing()`, `prep_ctour()` | §0.1, §3 B.3, B.4 |
| CCePock run FUN_180011710, prepare FUN_1800124d0, previous position FUN_180012390 | `pock_run()` | §3 B.5, §6.4.3 |
| region builder FUN_18002b840 + FUN_18002ae40 + FUN_180028cb0 + sliver filter FUN_1800298d0 | `regions_build()` | §3 B.6 |
| next region FUN_18002a2a0 | `region_next()` | §3 B.7, §6.4.1 |
| region loop FUN_18006e620 (states, ramp flag, guard 32/16) | `region_machine()` | §0.1, §3 C.0 |
| state 0/1 FUN_18006d170, trial step FUN_18006db90, choose FUN_180016c00 / FUN_1800167d0 / FUN_180016cb0 | `state01()`, `trial_step()`, `choose_step()` | §3 C.3, §6.2 |
| candidates FUN_180019de0/19ec0, FUN_1800186a0 + FUN_18001efb0 (wall placement), FUN_180017a10 (trial line), FUN_180018990, FUN_18001bb60, FUN_18001a2b0 / 1aa70 / 19960 / 1cc60 / 1d9e0 / 194e0 (bracket + Brent) | `cand_*()` | §6.2.2, §6.2.3 |
| move toward material FUN_180017d30, FUN_18006b170 (+aba0, bfa0, ad40, c4e0, c690), FUN_18006b570 (+bcb0, c580) | `toward_material()` | §6.2.1 |
| attach FUN_180024720 → direct FUN_180022a50 (first_exit 23dd0, touch_start 241c0), probe FUN_180023460, offset_attach FUN_180021f70, uturn FUN_180022500 / 216c0 | `attach()` | §3 C.3, §6.2.5 |
| state 2 FUN_18006d330, candidate path FUN_18006caa0, advance FUN_18006f1c0, shorten FUN_18006dfa0 / 6e250 / 6de20 + Brent FUN_1800b0200, clear_of_material FUN_18006e470, firstpass FUN_18006ee90, air feed FUN_18008a4a0 | `state2()` | §3 C.4 |
| limits FUN_18006cf10 (rate-of-engagement ramp) | `limits()` | §3 C.2 |
| measure FUN_180030be0 / FUN_180030de0 (+ swept region FUN_1800321e0, flags 2/4/8/0x10/0x100, engaged sweep FUN_18002fc80) | `measure()` | §3 C.1, §6.1.1-6.1.6 |
| commit FUN_180035990 → FUN_180037790 (+ new loop attributes FUN_180032a20, new position FUN_180037540) | `commit()` | §3 C.4, §6.1.7 |
| local windows FUN_18002a140 / FUN_180029c30 | `windows()` | §3 C.1, §6.1.8 |
| entry FUN_180070130: approach FUN_180076b10 / FUN_180075a00 / FUN_180075f60 / FUN_180076500 / FUN_1800757e0; plunge FUN_18006fc70: helix FUN_180078840 / FUN_1800788d0 / FUN_18007b130, profile FUN_18007cb70 / FUN_18007ca70 / FUN_18007cd50, ramp FUN_18007bc00, short-entry spiral FUN_18007c700 / FUN_1800a6aa0, entry material removal FUN_180070b60, emission FUN_1800707b0, state after entry FUN_18005bee0 / FUN_18005bca0 | `entry()` and helpers | §3 D 1, §4, §6.4.4, §6.7.8 |
| links FUN_180067ce0 → local FUN_180067d80 / general FUN_1800653d0; targets FUN_180052a90 / FUN_180066320 / FUN_180066ff0 / FUN_180066ad0; stay-down FUN_180061cd0 (strategies FUN_18005ff90, FUN_180044870 + FUN_180045470, FUN_18005d5f0, FUN_18005f4c0); retract decision FUN_180064180; retract link FUN_180062140; exit FUN_18005eca0 / FUN_180064980 / FUN_18005dd40; approach FUN_180060770 / FUN_18005c980; next state FUN_180065ff0 | `link()` and helpers | §3 D 2, §6.3, §6.4 |
| region sets FUN_180062a70 | `region_sets()` | §6.4.2 |
| Z profiles FUN_18003d490 (lift / descent / lift+descent) + FUN_18003e2f0 | `zprofile()` | §3 D 2.4 |
| final exit FUN_1800688e0 | `final_exit()` | §3 D 3 |
| CCetp output: ring FUN_1800870f0, shape emit FUN_180087390, move flags FUN_18008a1e0, feed FUN_18008a0a0 / FUN_18008a190, trim FUN_18008ee40, last point / tangent FUN_18008e4b0 / FUN_18008e710 | `cetp_*()` | §3 C.5, §3 D 4 |
| Peel: PeelAesthetics FUN_1800df710 (FUN_1800df410, step 1 FUN_1800dfaf0, step 2 FUN_1800dc820 / FUN_1800dcce0) | `peel_aesthetics()` | §1.2 |
| mcmill ProcessDynamicMillExt (mcmill_hst2d_dyn.c 3185-3390): records → NCI (Z = ent+0x78, arcs with a Z change linearised) | glue in `cam2d_dyn.c` | §3 D 4.3 |

**Code map as built (2026-10-16; the table above is the plan, these are the names in mcce_dyn.c):**
- Public API (mcce_dyn.h): `McceParm` (CDynPockParm + CDynPockParmDer, field comment = Parm offset), `McceRec` (one
  CCetp record: 'L' / 'A', absolute z0 / z1, CeFlags, move flags +0x90, feed), `McceBnd` (one input loop, island /
  open flags); `mcce_parm_from_record` (= LoadDynPockParm, the plan's `mcce_load_parm`; Peel: start selector 8 from
  record 0x14e0 / 0x14e8, else 0x1728), `mcce_calc_dyn_pock` (= MCCECalcDynPock: parm_derive, the FUN_180090290
  preprocessing `prep_ctour` / `prep_inside` / `prep_facing`, the FUN_180012390 previous position, `regions_build`,
  `while (region_next) region_machine`, then records with absolute Z; S2 material boundaries are not used and give an
  error text).
- Internal groups: kernel (Ent / Sh / ShL helpers, `walk_fwd` / `walk_back`, `sh_nearest`, `pt_in_sh`, `isect`, section
  copy `sh_section*`, ISAB lists `il_*`, `short_sec`); Clipper bridges (`sh_to_cpath(_o)`, `sl_offset`, `sl_minus`,
  `sl_bool`, `sl_area`); parameters (`parm_derive`, `ln_stepover`, `ln_eng_ang`); output (`cetp_emit`,
  `cetp_emit_shape`, `cetp_last_pt` / `cetp_last_tan`, `cetp_trim`, `rec_feed`, `mark_air_feed`); regions
  (`sliver_filter`, `regions_build`, `region_next`, `region_sets`); material model (`swept_region`, `measure`,
  `measure_block`, `subtract`, `commit`, `new_pos`, `drop_slivers`, `groups_overlap` / `groups_cpaths` = the plan's
  `windows`); state 2 (`state2`, `candidate_path`, `path_step`, `shorten*`, `clear_of_material`, `firstpass`, `limits`,
  `brent`); states 0/1 (`state01`, `trial_step`, `trial_line`, `zero_len_step`, `toward_material`, `wall_target`,
  `cand_A` / `cand_half` / `cand_bracket` / `cand_table`, `choose_cands` / `choose_refined`, `attach`, `attach_direct`,
  `offset_attach`, `uturn_attach`, `lead_in`, `probe`); links (`link_state`, `local_link`, `general_link`, `targets_*`,
  `order_mode*`, `stay_down`, `connectors`, `conn_route`, `route*`, `arc_line_arc`, `need_retract`, `retract_link`,
  `exit_move`, `approach`, `zprofile`, `emit_link`, `after_link`, `bidir_toggle`); entry (`entry`, `approach_outside`,
  `plunge_entry`, `helix_entry`, `profile_entry`, `ramp_loop`, `short_entry_ext`, `spiral`, `entry_removal`,
  `entry_emit`, `after_entry`, `max_clearance`); `final_exit`. Not present: a separate `pock_run` (inlined in
  `mcce_calc_dyn_pock`) and `peel_aesthetics` (§5 item 32).
- Port numerics (substitutes, not mcce values): `g_chord` = max(chain_tol P+0x110, 0.002 R) is the arc linearisation
  tolerance of every Clipper bridge; `eqtol(t)` = max(t, 1.5 g_chord) for "equal distance" tests (§5 item 8); swept
  polygons use circumscribed chords on convex arcs (`sh_to_cpath_o`); material loops thinner than 2 g_chord after a
  subtraction are dropped (`drop_slivers`); P+0x58 sliver = 20 x chain_tol, at least 0.001. `MCCE_DEBUG=1..3` prints
  records / state traces to stderr.

## 2. Data structures (mcce layout → C)
- `Ent` = `pock_chn` (0x88): id 'L' / 'A' / 'P' (+0x08), pass byte (+0x09), CeFlags (+0x12), line +0x18..+0x30, arc
  centre / r / sa / sw +0x18..+0x38, bbox +0x40..+0x58, relative Z +0x78.
- `Sh` = `CSh`: entity vector, dir (+0x50: 1 CCW, −1 CW, 0 open), flags (+0x54 bit0 island), tag (+0x58), bbox
  (+0x30..+0x48). `ShL` = `CVe<CSh*>`. Group order is mcce's: [islands…, outer] (§3 B.6).
- `Pos` = `{int shape, ent; double t; P2 pt}` (path position, §3 D conventions).
- `Parm` = `CDynPockParm` + `CDynPockParmDer` derived fields, one C field per decoded offset (comment = offset).
- `Pock` = `CCePock`: parm, RG lists (+0x30 tool-centre regions, +0x58 / +0x80 material per region, +0xa8 current
  region = C+0x340, +0x128 = C+0x3c0, +0x150 = C+0x3e8, +0x178 current material = C+0x410, +0x1a0 bullnose material =
  C+0x438, +0x218 S3, +0x240 AIR), previous position C+0x120, sweep memo C+0x710..+0x728, first-pass flag C+0x758.
- `EnCut` (trial record E: entity, heading E+0x98, length, w1 / w2, engaged sweeps, flags, pass byte) and `Gbap`
  (G: valid, lead-in shape, shape / ent / t / pt).
- `Cetp`: every emitted record (entity with CeFlags, move flags, feed, the 4 values) in an array; the 16-slot ring
  reads are the last 16 entries; T+0x1c = count, T+0x20 = index of the last record with move flag 0x40.

## 3. Allowed substitutions (standard geometry with decoded semantics)
| mcce kernel | semantics used (source) | our implementation |
|---|---|---|
| `MCCECalcOffset(1, d, shapes, mode, …)` | offset of closed regions by d (+ grows outer boundaries; islands follow nesting) (§3 B.3, B.6, §6.4.2) | Clipper `cl_offset_nz` / `cl_offset` (round joins); arcs linearised at the chord tolerance |
| `FUN_180153b30(tol, X, Y)` | "X minus Y" (region minus holes / air; §3 B.3, §6.3.0 reading) | Clipper difference |
| `SoPockUnionAr`, `MCCECalcPockIntersect` / FUN_18015be30 | union / intersection of nest groups (§3 B.3, §6.5.6) | Clipper union / intersection |
| `DetNestLvlShAr`, `UpdDirShAr` | nesting levels, island bit, direction (§3 B, §6.1.7) | `cl_group` (outer + holes) reordered to mcce's [islands…, outer] |
| swept area ∩ material (FUN_18003a140 loop rebuild) | removed area = swept area − leftover loops = area(swept ∩ material) (§6.1.5, dynamic.md "mean radial width of cut") | Clipper intersection of the exact swept outline (§6.1.1, built entity by entity, then linearised) with the material |
| material subtraction FUN_180037790 | material := material − swept region; loops wholly inside are dropped (§6.1.7) | Clipper difference; the new loops get the attributes of FUN_180032a20 |
| engaged sweep on the end cap FUN_18002fc80 | angular overlap of the end-cap arc with the material (§6.1.6) | cap arc ∩ material (Clipper open clip) |
| `PtInSh`, `GetNearestEntPt`, `CalcEntIntersect`, `CalcLnLnIntersectPt`, `CalcCirCirIntersect` | point in shape (crossing count; 2 inside / 1 on / 4 outside), nearest point, entity intersections | own code on lines and arcs |
| `mcce::CalcMedAx` + FUN_1800c5df0 (max-clearance medial point) | medial edge end point with the largest clearance (§3 D 1.5) = centre of the largest inscribed circle | distance search on the region (iterative inset to the last non-empty piece, then the point with the largest boundary distance) |
| `ApxArcWithLnSegs`, spiral piece fit FUN_1800aab40 | arc / spiral → line pieces within tolerance (§6.6.2, §6.7.8) | own chord-tolerance linearisation |

## 4. Implementation stages (status)
| stage | content | status |
|---|---|---|
| S0 | this plan; extra decode of target generation / kernels / entry removal (§6.10-6.12 of mcce_engines.md) | DONE 2026-10-15 (§6.10-6.12 merged into mcce_engines.md) |
| S1 | kernel (entities, shapes, positions, walks, nearest, intersections, Clipper bridges), parameters, region build / next region, Cetp output | DONE 2026-10-15 |
| S2 | material model (measure, commit, windows), state 2, limits | DONE 2026-10-15 |
| S3 | states 0/1 (trial steps, candidates, attach) | DONE 2026-10-15 |
| S4 | entry (approach from outside, helix, profile, ramp), final exit | DONE 2026-10-15 |
| S5 | links (targets, stay-down strategies, retract link, exit, Z profiles) | DONE 2026-10-15 (local + general link) |
| S6 | glue in cam2d_dyn.c (records → moves), Dynamic Contour preprocessing, Peel | glue DONE (default since S7; dynamic.md "Glue code"); Dynamic Contour band (MCCE_DCTOUR=1 only) + Peel (refused): see §5 |
| S7 | oracle before / after, fileop + post regressions, switch decision | DONE 2026-10-15: Dynamic Mill switched to the port |

## 5. Stubs: NOT DECODED pieces and what the code does instead
Spec references are formats/mcce_engines.md sections (§6.9-§6.12 hold the open lists).

Geometry kernels (allowed substitutions, §3 above; the open part is ordering, not semantics):
1. `MCCECalcOffset`, `FUN_180153b30`, `SoPockUnionAr`, `MCCECalcPockIntersect` / `FUN_18015b2e0`, `DetNestLvlShAr`:
   Clipper. The order of the output loops and the start vertex of each loop are NOT DECODED; Clipper's are used. This
   fixes the region order (§3 B.7), the start point of a Profile entry loop and the reference positions on offset loops.
2. `mcce::CalcMedAx` + FUN_1800c5df0 (largest-clearance medial point): bisection on the inset distance; the centre is
   the vertex mean of the last non-empty inset. When the maximum is a ridge (a slot), which end mcce picks is NOT DECODED.
3. (closed 2026-11-07) ISAB list order LAB_1800074dc = FUN_180133630, ring order LAB_180004e21 = FUN_180133680: read in
   the disassembly; they are the orders the consumers implied (mcce_engines.md §6.11 NOT DECODED, closed entry).
4. (closed 2026-11-07) FUN_18006b570 corner order LAB_180001b9a = FUN_18006c470: ascending squared distance to the context
   point (disassembly 0x18006c470-0x18006c4c1), as ported.
5. (closed 2026-11-07) Engaged-arc point order LAB_1800034b8 = `jmp FUN_1800312f0`, as ported.
6. (closed 2026-11-07) Bracket refine callback LAB_1800011ef = `jmp FUN_180019180`, as ported.
7. `MCCECalcContourOffset` (one-sided offset of an open chain, FUN_18005a8a0): own parallel-curve construction.

Material model:
8. (default since 2026-11-07: ported, §9) Exact loop rebuild FUN_18003a140 / FUN_180037790. The old substitution stays
   selectable with MCCE_MEASURE=clipper (or MCCE_EXACT=0): Clipper intersection / difference with the swept region
   (§6.1.5 semantics), swept polygons circumscribing their convex arcs, material loops thinner than twice the chord
   tolerance dropped, equality tests between offset curves ("at R − r from the material", ±1e-5 / ±1e-4) with
   max(tol, 1.5 × chord tolerance).
9. Pass bytes of material entities (first-pass feed, C+0x758) are not tracked: records never get the first-pass class
   0x20 from the pass count (all user ops have first-pass reduction 0).
10. Local material windows FUN_18002a140 / FUN_180029c30 + window crossings FUN_1800331a0: ported in the exact model
    (2026-11-07, `windows_build` / `win_clip`, built and emptied by `trial_step` as FUN_18006db90 does). With
    MCCE_MEASURE=clipper: the full material, restricted to the nest groups whose bbox overlaps the swept region.
11. New material position FUN_180037540: exact model - ported (the record's +0x40/+0x44 new loop / entity). With
    MCCE_MEASURE=clipper: the crossing with the largest swept-outline parameter, mapped to the new loops by nearest point.
12. CBands (bIsBandsAvail, FUN_180143d20 / FUN_180144630) not built; FUN_180112af0 runs without bands.

Entries (P8):
13. Helix-only (0x100), Profile (0x800), approach from outside, the shared ramp, short-entry spiral and the entry
    material removal are ported. Medial (0x200, 0x400, 0x1000) and custom chain (0x8000) entries need CalcMedAx chains and
    leaf builders that are NOT DECODED (§6.7): they take FUN_18006fc70's Profile fallback (mask 0x8700).
14. Chaikin smoothing of the entry loop (FUN_18004ae80, Profile and medial-ramp entries): PORTED 2026-11-07, exact model
    only (`smooth_entry_loop` and the cs_* helpers in viewer/mcce_dyn.c; spec mcce_engines.md §6.7.7 / §6.7.10). The
    callback LAB_180002c6b = FUN_180048f30 is a progress update (no geometry): not ported. Ours inside it: the circle /
    entity crossings of FUN_18014c4f0 use our e_isect for mcce::CalcEntIntersect (as everywhere in the port). Not reached
    and not ported: ApxArcWithLnSegs in FUN_180124110 (the chord polygon has lines only). MCCE_MEASURE=clipper keeps the
    unsmoothed loop (Clipper path unchanged). Still open: the loop's start entity (FUN_1800da6a0, lowest source index) comes
    from our offset's output order (our offsets do not track source entities); it matters only when no line of the loop is
    at least 2 x step = 4.69 d long (else FUN_18014ef90 restarts the loop at the longest line's midpoint) and for the ramp's
    start point.
15. Centre helix on point (0x100000; entry point list C+0x650 source NOT DECODED): max-clearance centre.
16. Air dwell after the entry (0x40000000; radius search FUN_180072280 NOT DECODED): not added.
17. Approach fallback FUN_180060cd0 NOT DECODED: none.
18. Spiral piece fit FUN_1800aab40 / FUN_1800acb50: line pieces of 5°.

Links:
19. Guided planner FUN_1800f1080: ported (viewer/mcce_dyn.c block "guided planner (§6.16)", every stage checked against
    mcce.dll, tools/peloader --gp), but OFF by default (MCCE_GUIDED=1 turns it on, exact model only; decision: the NCI oracle
    gets worse, see the 2026-11-07 pass 5 handoff in §9) - by default the plain-route fallback (FUN_1800f1580 = FUN_18010c040
    of the straight line) is used for stay-down strategy 5 and connectors 4 / 5. Still NOT DECODED inside it: the cores of
    SoPockUnionAr / FUN_180153b30 (our exact boolean, marked ours). (FUN_1800a9490, FUN_1800f2b40's self-crossing test
    FUN_18012f2b0, FUN_1801270e0 and the route-edge chord FUN_180146c10: ported 2026-11-13, M2 stage 2 in §9.)
20. Link post-processors FUN_18004a070, FUN_180049950, FUN_180045c40: identity.
21. BIDIR (flag 4) pre-path FUN_18005e690 / FUN_180065b20 (rotation angle NOT DECODED): not added; flag 4 itself
    (list reversal, feed split) is ported. No user op sets it.
22. MOAT_FIRST (registry bTurnOnMoatFirst, default 0) preparation FUN_18002be40 / FUN_18002c360: not ported.
23. Mode-2 local window FUN_180067820: not reached with P+0x20 = 1 (all user ops).
24. FUN_180050410 skips S1 entities with byte +0x12 bit value 2 (meaning NOT DECODED): our offsets carry no such bits.

Inputs / outputs (mcmill side, not part of mcce):
25. Chains -> boundary sets. **Decoded 2026-11-07 for from-outside ops** (dynamic.md "Machining regions (decoded)"):
    role-0 chains are each a region of their own in reverse chain order (ConvertChainsToPockBounds prepends; PackageChains
    takes the first bound of the highest z, FUN_1802a7cf0), never islands of each other; each region grown by the wall stock
    keeping the largest piece (FUN_1801ac7e0); every avoidance region (role 4, grown by the stock) is an island of every
    region; S1 = islands then region, S2 = rest material (record 0x17b8 only) else this+0x348 (set by PreparePockBoundsPeel only), S3 = the
    entry chains (role 5). A "Solid Chain" gives every loop of its top face (CORRELATED, its stored selection is NOT DECODED).
    Still NOT DECODED / measured only: stay inside (region inset by the wall stock, even-odd union of the chains with the
    nested chains as islands - the decode reads no nesting on that path either: open question), record 0x17b8 (trim /
    rest-material path, BRACKET4 ops 14 / 16 stay inside), containment (role 7) / air (role 6) / entry (role 5) chains
    (no user op has them), MCCECalcOffset(+d) on a clockwise region (chain_to_pock_bound keeps the chain direction; ours: CCW), the islands' union (ours; mcmill passes them one by one).
    Superseded 2026-11-09 (formats/audit_2026-11-09_highspeed_2d.md): stay inside is decoded and ported (per chain, FUN_1801ac7e0); chain direction kept; record 0x17b8 and roles 5 / 6 / 7 are refused.
    2026-11-13 (M3): a region nested in another region takes that region's z as its top of stock (HasOuterPocket, the decompile
    drops the setter's arguments; dynamic.md "Nested regions"), and the region-to-region link is the depth writer's group link
    (dynamic.md "Region-to-region link"); both ported. Containment (role 7, PackageChains l.4737-4835: MCCECalcPockUnion of the
    containment bounds, then MCCECalcPockIntersect of each role-0 bound with them, the bound z kept) stays refused: its two
    booleans are mcce cores the port only has as Clipper substitutes (§3 B.3 / §6.5.6), and no user op has role 7.
26. CeFlags-1 records: FUN_1801afb70 / FUN_1801afb60 are decoded (no motion: set_heights / return 1; Mastercam writes the
    engine's rapid records as G0 to their own XYZ). The port keeps the SAFE rule: rapid up to the feed plane, across, rapid
    down to the feed plane and plunge feed (improvisation_review.md CAM small items part 3).
27. P+0x78 / P+0x80 per depth level: op top of stock / level Z (PeelMill.cpp caller; CHST2D worker path not checked);
    the previous-position entity is the FUN_180012390 default at every level.
28. LoadDynPockParm: the tool-manager refinement of R / r (FUN_1801afa30, tool types 0x17 / 0x19 / 0xc / 0x15) is not
    applied; chain tolerance = config 1001 default 0.002 mm / 0.0001 in; tol_exref = config 1000 default 5e-5 (mccore
    FUN_1803bc7d0, decomp/mccore_systol.c).
29. Registry: the default values of an installed Mastercam (bUseRateOfEng = 1, bIsVarRadAvail = 1, bIsBullnoseAvailA = 1).
30. End tangent fallback that rereads the CCetp file (FUN_18008d020): not done.

Dynamic Contour (P2) / Peel (P3):
31. Dynamic Contour closed walls (FUN_180092950 / FUN_180091d40 / FUN_1800922e0) are ported; open chains
    (FUN_1800930c0, §6.5 band kernels around MCCECalcContourOffset) are not. No Dynamic Contour op exists in the user's
    parts, so nothing validates it.
32. Peel: the engine side is the Dynamic Mill engine with start selector 8 (op 0x1548 / 0x1550). PeelAesthetics
    (FUN_1800df710) needs the mcmill lists this+0x328 / +0x348, whose Motion / Material roles are NOT DECODED: not wired.
    Since 2026-10-16 a Peel op is not generated at all (cam2d_dyn.c `gen_dynamic` refuses it; the earlier "slot along the
    open chain" region was ours and is removed). No Peel op exists in the user's parts.
33. Dynamic Contour glue (cam2d_dyn.c `gen_dyncontour_mcce`, mcmill FUN_1801b58f0 decomp/mcmill_hst2d.c l.11126-11653):
    on a plain square with the Mill_MM default op the band (min material thickness 0.025 without finish passes) cuts
    nothing; the band semantics are unverified, so it runs only with MCCE_DCTOUR=1 (improvisation_review.md CAM item 7).

## 6. Results log
- 2026-10-15, first full run (MCCE_DYN=1): tramming-rod op 1 (D 6, 25 %, from outside): 7,642 records (Mastercam's
  NCI: ~7,665 moves), 6 s. Oracle (ORACLE_FILEOP=1): ref->ours p50 / p95 0.024 / 0.312 mm (old engine 0.496 / 1.853),
  ours->ref 0.024 / 0.557 (old 0.495 / 1.021), material missed 0.0 % extra 0.0 %.
- 2026-10-15, final (default selection: Dynamic Mill -> port), all 35 stored Dynamic Mill ops of the user's 16 parts
  (`ORACLE_FILEOP=1 tools/oracle.py --type dynamic`; counts / entries / engagement from a raster walk of both paths at
  the cut level, res D/40):

  | measure | old generator | mcce port |
  |---|---|---|
  | material missed (inside chains), median / worst | 0.0 / 0.1 % | 0.0 / 0.1 % |
  | material extra, median / worst | 0.0 / 6.1 % | 0.0 / 0.2 % |
  | path deviation p50, ref->ours median / worst | 0.514 / 1.117 mm | 0.042 / 0.277 mm |
  | path deviation p50, ours->ref median / worst | 0.418 / 0.557 mm | 0.036 / 0.263 mm |
  | path deviation p95 (worse direction) median / worst | 3.127 / 14.078 mm | 0.461 / 2.589 mm |
  | NCI move count, relative difference median | 0.70 | 0.11 |
  | entry count equal to Mastercam's | 16 / 35 | 31 / 35 |
  | first entry point within 0.1 mm of Mastercam's (median distance) | 0 / 35 (14.97 mm) | 32 / 35 (0.04 mm) |
  | engagement p95 − Mastercam's, median | +5 deg | +0 deg |
  | engagement max − Mastercam's, median (ops > +10 deg) | +38 deg (23) | +1 deg (6) |

  The three first-entry misses are the stay-inside ops (NAKTO op 2 helix -> Profile fallback, BRACKET4 ops 14 / 16
  Profile): the Profile loop's start vertex (MCCECalcOffset output order) and its Chaikin smoothing are NOT DECODED (§5
  items 1, 14). The largest remaining p95 deviations (buckle-v11/12 op 17, buckle-v6/7/8 op 31) are link shapes
  (guided planner, item 19) and solid-chain regions.
- Regressions: fileop (`fileop_all.sh new_dyn`): 154 / 154 non-dynamic ops byte-identical to `base`; the 35 Dynamic Mill
  ops changed as intended (closer to the NCI, above); whole run 4 min 27 s. Post (16 parts, DATE lines removed): 0 / 16
  differ (the post uses the stored NCI).
- Not selected by default: Dynamic Contour and Peel Mill (no reference op in the user's parts), Area Mill (not mcce).
  Superseded 2026-10-16: `MCCE_DYN` no longer exists; Dynamic Contour runs the port only with `MCCE_DCTOUR=1`, Peel is
  refused (§0, §5 items 32-33).
- 2026-10-16 (improvisation_review.md CAM small items part 1): tol_exref 1e-7 -> config 1000 default 5e-5, chain_tol
  per unit (0.002 mm / 0.0001 in, so the sliver width P+0x58 = 20 x chain_tol = 0.04 mm): Dynamic oracle p95 median
  0.461 -> 0.462, worst 1.496 -> 1.496, material missed / extra unchanged. Comparison scripts: dynamic.md "Test
  scripts".

## 9. Exact material model (planned 2026-10-28: Dynamic speed + Mastercam's own geometry)
User 2026-10-28: "the 2d dynamic mill tool path generates very slowly ... it should go pretty quick". Profile (perf, the gen
100 x 50 pocket, 10 mm tool): ~89 % in the engine; ~12 trial measures per committed move (Mastercam's own search:
the 50-step heading scan + Brent to 1e-7, FUN_18001aa70 / FUN_1800194e0); each trial = a Clipper intersection on the
tessellated material (substitution 8 above) - Clipper ~50 %, its input preparation ~10 %, the commit's difference ~20 %.
The tessellation cache (qt_port_plan.md "Tessellation cache") gave 9-17 %, bit-identical.

Mastercam's measure / commit work on the exact loops (§6.1.3-6.1.7): crossings of the swept outline with the material
entities (CalcEntIntersect), the sections classified by 3 samples each, loops rebuilt from material + outline pieces,
area by the exact trapezoid / arc-segment sum. All of it is decoded now: the two qsort keys left were read 2026-10-28
(LAB_180002969 = FUN_1800314a0, LAB_180005c72 = FUN_180031280, mcce_engines.md §6.1.3 / §6.1.4).

Plan (each behind MCCE_EXACT=1 until the switch):
1. measure: crossings (FUN_180032b30 / window FUN_1800331a0), markers (FUN_18002fa30), classification (FUN_1800337e0,
   FUN_180031150, FUN_1800354c0), outline order (FUN_180035630), removed area (FUN_18003a140 + FUN_180126ac0),
   the engagement arc (FUN_18002fc80), flags 4 / 0x100 from the records (FUN_180037070 / FUN_1800380f0).
2. commit: FUN_180037790 (FUN_180147500 sections, FUN_180036250 collinear merge, FUN_180036f70, FUN_180035ff0 pass,
   FUN_180147fd0 close, FUN_180032a20 attributes, FUN_180033cf0 / FUN_180033b60 pass numbers).
3. check: MCCE_EXACT=0 / 1 side by side on the 37 stored Dynamic ops against Mastercam's NCI (tools/dyn_batch.py,
   oracle_results.md) and the op scenes; the switch is the user's decision (the goldens change).
Expected: no tessellation and no polygon boolean per trial - the cost is the crossings of a ~4-entity outline with the
material entities near it.

**Status 2026-10-29 (MCCE_EXACT=1, viewer/mcce_dyn.c "exact material model")**: measure + commit ported (crossings, markers,
classifier, the deletions of FUN_1800337e0 / FUN_180035f30 / FUN_180031150, the island rule, outline order, engagement arc
with CalcSharedArcSw + FUN_1800312f0, removed area FUN_18003a140 with the exact area sum, flags 4 / 0x10 / 0x100, commit
FUN_180037790 with the collinear merge, FUN_180036f70, FUN_180032a20, new position FUN_180037540). Ours: records of the
measure are cached for the commit (a miss recomputes with side 0); far shapes' markers stay class 0 unsampled; pass bytes
(first-pass feed) not tracked, as before. Default off: the 37 ops' moves unchanged with it off.
Speed: gen 100 x 50 pocket 5.05 -> 1.90 s wall (app incl. ~0.65 s start), BRACKET4 op 14 2.88 -> 0.74 s CPU.
Oracle (tools/oracle.py --type dynamic, ORACLE_FILEOP=1, 37 ops): p95 median 0.462 -> 0.422, worst 1.496 -> 1.430; better
on buckle-v12 op 23 (0.288 -> 0.121), buckle-v10/v11/v12 op 3 + v9 op 51 (0.64 -> 0.33), v6/v7 op 36, tramming-rod; worse on
buckle-v8/v9 op 42 (0.31 -> 0.40), v6/v7/v8 op 32 (0.46 -> 0.65), v10/v11/v12 op 10. **Open**: buckle-v12 op 22 cuts 6.1 %
extra material (ours->ref max 4.04 mm) and buckle-v10 op 3 1 % - a bug to find before the switch.

**Status 2026-11-04 (op 22 fixed; user decision: exact-arc region geometry)**: the extra cut was one 13.2 mm feed move at full
depth across the island (record 852). Cause: the starting material came from the Clipper bridges (sl_offset / sl_minus:
arcs linearised at g_chord, 0 arcs in op 22's 179-entity material). A step side line running nearly tangent to the facets
crossed several of them and the commit (correctly, as FUN_180037790 is decoded) kept the tips of the thin wedges between them
as loops of ~4e-7 mm2; the engine later chased one across the island with a zero-width step that commits (marker class 2 ->
not air -> 0x80). Mastercam's MCCECalcOffset keeps arcs (each output entity points at its source, FUN_1800da810, §6.13.1).
Fix (viewer/mcce_dyn.c "exact regions", MCCE_EXACT only): sl_offset_m / sl_minus_m / sl_bool_m / sl_union_evenodd_m build the
same regions exactly on lines and arcs (own construction, §3 semantics): booleans by splitting the boundaries at their
crossings and keeping the pieces whose two sides differ in fill (exact ray-crossing winding numbers, samples 1e-6 off the
piece); offsets by the raw offset (entities moved by d, round joins at convex vertices, concave vertices cut at the moved
entities' crossing) split at its crossings, pieces at distance >= |d| (1e-8) from the boundary kept; coincidence 1e-7, no
piece shorter than 1e-7, link tolerance 1e-6. Loop order / start entity copied from the Clipper result (NOT DECODED either
way). Used for the material only: prep_* (M, AIR), sliver_filter, regions_build's MAT / bull material, entry_removal. The
tool-centre region (tcr / cur) stays Clipper: built exactly, buckle-v6 op 36 stalled (2865 region iterations, 253 mm2 left;
the per-step link / target sets are Clipper offsets of it) - formats/TODO.md. Any exact failure falls back to Clipper
(MCCE_DEBUG counts them; MCCE_EXRDUMP=DIR writes the failing inputs): 0 fallbacks on the 37 ops.
Oracle (37 ops, MCCE_EXACT=1): p95 median 0.385 (off 0.462, exact before 0.422), worst 1.425 (off 1.496); missed / extra worst
0.1 % / 0.2 % = off; 21 ops better than off, 4 worse (buckle-v11 op 1 0.322 -> 0.382, v6/v7/v8 op 32 ~0.46 -> 0.50-0.52).
buckle-v12 op 22: extra 6.1 % -> 0.1 %, p95 0.644 -> 0.546 (ours->ref max 2.056, off 1.524); buckle-v10 op 3 extra 1 % -> 0.
MCCE_EXACT=0: the 37 ops identical to run_m27b. The switch to on stays the user's decision (goldens change).

**Status 2026-11-07 (exact model = default; local windows; speed)**: requested this session: make Dynamic fast and faithful by
using Mastercam's exact kernel instead of the Clipper measurement, the old path selectable, default only if correct.
- Switch (merged into toolpaths2 2026-11-07; the default by user decision D-2, user_decisions.md 2026-11-07): `MCCE_MEASURE=exact` (or `MCCE_EXACT=1`) selects it; as built on the branch `exact_on()` was 1 by default; `MCCE_MEASURE=clipper` (or `MCCE_EXACT=0`) gives the Clipper bridges for comparison
  (bit-identical to HEAD: 41/41 fileop moves, and the Dynamic op scenes equal the goldens up to 10 pre-existing `.mcamui`
  format diffs that both modes share).
- Ported: the local windows **FUN_18002a140 / FUN_180029c30** (`windows_build`, `win_clip`; built at the start of `trial_step`
  = FUN_18006db90 l.62163 and emptied at its end l.62186-62198, both sets; ρ = 10 × march step + R) and the window crossings
  **FUN_1800331a0** (`ex_crossings(mat, win, ...)`, records carry the copy's source shape / entity); measure and commit take the
  window when it holds a shape (FUN_180030de0 l.30695-30701). The marker order of **FUN_18002fa30**: markers are pushed after
  all crossing records (FUN_180039c70 = push_back, l.35401-35460) - before, they were interleaved by shape. Both changes leave
  the 41 ops' moves bit-identical (the window serves 80-95 % of the measures; no step reaches past ρ), as expected.
- Decoded from the disassembly (mcce_engines.md §6.1.2, §6.9, §6.11): the classifier frame's rotation angle θ = wrap(π/2 −
  atan2(−o.y, −o.x)) with ReflectionMx(1) = x → −x and row-vector CTMatrix - the frame our `clf` already used; all qsort /
  callback thunks of the material and ISAB code (LAB_1800034b8 → FUN_1800312f0, LAB_180001b9a → FUN_18006c470, ...).
- Ours, speed only (results identical): `e_bbox` without libm fmin / fmax calls; `e_qbox` (lines: exact box, arcs: circle box)
  as a pre-reject before an arc's exact box in `isect`, `ex_cross_ent`, `win_clip`; `pt_in_sh` skips the nearest-point test
  of entities clearly farther than tol and the crossing count of arcs whose circle misses p's ray.
- CPU, the 41 stored Dynamic ops (cam_cli fileop, 4 parallel, same load): Clipper 119.5 s (session baseline 132.4 s) ->
  exact before this pass 34.3 s -> exact now **19.9 s** (6x); slowest op buckle-v10 op 10 13.1 -> 1.9 s.
- Oracle (tools/oracle.py --type dynamic, ORACLE_FILEOP=1, 37 ops): Clipper p95 median 0.462 / worst 1.496 -> exact
  **0.385 / 1.425**; 22 ops better, 4 worse (buckle-v11 op 1 0.322 -> 0.382; v6/v7 op 32 0.462 -> 0.522, v8 op 32 0.452 ->
  0.502); missed / extra unchanged (worst 0.1 % / 0.2 %); ok 0/37 either way.
- UI goldens NOT blessed: with the exact default these op scenes differ (toolpath point counts / NC): dlg_dynamic,
  gen_dynamic_contour, gen_dynamic_mill, regen_dup_toolnum, rt_arcf_b10_dyn, rt_arcf_b10_planes, rt_b12_reload_stored,
  rt_dctour_b9_switch, rt_dctour_hst, rt_dynamic, rt_dyn_b9_cut, rt_dyn_b9_entry, rt_dyn_b9_helix, rt_dyn_medax_entry1,
  rt_dyn_medax_entry2, rt_dyn_medax_entry4p, rt_face_b7_dynamic, stored_recalc (txt and/or NC).
- Still ours / NOT DECODED in the exact path: the geometry kernels (CalcEntIntersect, section copy FUN_180146e20, PtInSh,
  GetNearestEntPt, arc area term) - semantics decoded, internals ours; the "far" marker shortcut and the record cache
  (commit never misses it: 0 misses over the 41 ops); the pass bytes of §6.1.7 (first-pass feed: FUN_180033cf0 /
  FUN_180035ff0 / FUN_180033b60, col_merge's pass test) - decoded, not ported, no stored op uses the first-pass feed
  (fp_red = 0 on all 41); the tool-centre region (tcr) and the link / target sets stay on the Clipper bridges (TODO.md).

**Status 2026-11-07 (entry loop smoothing, §5 item 14)**: ChaikinSmoothSh and its driver FUN_18004ae80 decoded (spec
§6.7.7 composition, §6.7.10 pipeline) and ported for the Profile entry (FUN_18007cb70 l.70202) and the medial-ramp entry
(FUN_18007b3c0 l.69498, MCCE_MEDAX=1 only), exact model only. Pipeline: step 2.343 d and tolerance 0.1036 d from the
smoothing distance d (FUN_18014c250); the loop restarted at its longest line's midpoint (FUN_18014ef90); a chord polygon
walked with steps aiming at that length, each chord within the tolerance of the loop (FUN_18014d5c0 / FUN_18014dfc0 /
FUN_18014e330); 6 passes of 1/4 - 3/4 corner cutting (FUN_18014c820); collinear runs merged within 1e-5 (FUN_18014f740).
Driver: d = P+0x1d8 (or P+0x40), then 0.9 d .. 0.2 d until no piece of the result is outside this+0x340; with one or two
crossings of the loop with this+0x340 only the section between them is smoothed and the loop becomes open.
- **Check**: on NAKTO op 2's first region Mastercam's ramp lies on our smoothed loop (p50 0.0035 mm, p10 2e-5 mm); on its
  second region the straight sides match to 0.0002 mm once the loop offset is moved by 0.04 mm (below).
- **Oracle** (p95, worse direction; the other 32 ops unchanged): BRACKET4 / bracket4-dashed 14 0.328 -> 0.085, 16 0.337 ->
  0.081; NAKTO-BRAKE-BRACKET 2 0.643 -> 0.372. Median 0.321 -> **0.222**, worst 1.473 (unchanged), ok 0. Unreproduced
  Mastercam path (dyn_cover, 0.3 mm): NAKTO op 2 893 -> 210 mm of 2053; BRACKET4 op 14 92 -> 5 mm, op 16 85 -> 3.5 mm (left:
  the first ~5 mm of the ramp - its start point). Missed / extra material unchanged.
- **Remaining on NAKTO op 2**: the second region's loop is 0.04 mm farther out than Mastercam's: our largest clearance of
  this+0x340 there is 3.0836 (offset bisection and MCCE_MEDAX=1's medial axis agree), Mastercam's ~3.124. The circle touches
  the straight right side and the wavy left side (material left by the earlier cut), so this+0x340's wavy side differs
  (region sets, not the entry). Plus the start entity (item 14).
- **CPU** (41 ops, exact, user time): 19.8 -> 26.1 s. The smoothing itself takes ~2 ms per loop; the rest is the entry
  material removal (FUN_180070b60 walks the entry entity by entity: 13.5 k short lines on NAKTO op 2, 1.7 s) - Mastercam's
  ramp has the same moves. MCCE_MEASURE=clipper: byte-identical to toolpaths2 on the 41 ops.
- TSan harness (buckle-v12 2 3 10 17 22 23; NAKTO 2 3; BRACKET4 14 16): 0 races, all "same". No new statics.
- Op scenes (tools/tests/op_compare.sh, all 265, against the goldens of 9ea6943; not blessed): 4 Dynamic scenes change,
  toolpath point counts / NC only, no errors: regen_dup_toolnum (NAKTO op 2: 5773 -> 18435 pts), rt_b12_reload_stored
  (2849 -> 5969), rt_dyn_b9_entry (4692 -> 5014), rt_dyn_medax_entry4p (5251 -> 5817, the medial-ramp entry). The other
  diffs are not from this change (golden .mcamui files lack the "PROJECT-FORMAT 2" line; gen_blend / gen_sfradial /
  gen_srplunge msg="Translate").

**Status 2026-11-07 (accuracy pass on the exact model, D-2)**: goal: lower the deviation from Mastercam's stored NCI.
New comparison tools (ours): `tools/dyn_diverge.py PART OP OURS.m` (first point where our path leaves Mastercam's, walking
both in order), `tools/dyn_cover.py` (stretches of Mastercam's path that ours does not reproduce anywhere), `tools/dyn_moves.py`
(Mastercam's stored moves of an op).
- **Found and fixed (decoded)**: MCCECalcOffset keeps the orientation of its input (FUN_1800da5e0 marks reversed inputs,
  FUN_1800d3b60 → FUN_1800d3cc0 l.114185-114208 reverses the outputs back; mcce_engines.md §6.13.1). out1 of the region sets
  (FUN_180062a70 / FUN_180066ff0) is the offset of the material, so under climb its outer loop is clockwise; the Clipper bridge
  returned it counter-clockwise. Every S1 tangent was reversed: the first approach target's +0x10 (FUN_180051480), hence the
  approach arc of FUN_18005c980 on the wrong side and rejected - our entries fell back to the straight-line candidate ~1 mm
  away from Mastercam's on 30 of the 37 ops; the walks / K-side tests along S1 were mirrored too. `sl_offset_k` re-orients the
  bridge's output to its input's (exact model only) at out1 / out3 / out4 (both region-set builders) and C+0x3c0 / C+0x3e8.
  Entry start points now within 0.004-0.2 mm of Mastercam's on those 30 ops (line ramp + arc ramp as in the NCI).
- **Ported**: the Profile entry loop as the exact offset (MCCECalcOffset keeps arcs, FUN_1800da810) and the arc split at pi/2
  FUN_180123bc0 (l.156874-156990) - exact model only.
- **Decoded, not ported / not usable**: DetNestLvlShAr's ordering (§B.7: closed-before-open comparator FUN_18013e780 through
  MSVCR120's unstable qsort, then the nesting tree) - its input order is the offset core's (NOT DECODED), so the region order
  stays Clipper's. Chaikin pipeline (item 14).
- **Experiments (kept off)**: `MCCE_EXSETS=1` builds the region sets exactly (sl_offset_m / sl_minus_m): 11 ops better
  (buckle-v9 op 46 0.159 -> 0.042, op 33 0.204 -> 0.072, v11/v12 op 17 0.194 -> 0.069), 3 worse (v8/v9 op 42 0.28 -> 0.43, v10
  op 10), CPU 20 -> 32-42 s; with the tool-centre region still from Clipper the two disagree (op 42 leaves 135 mm of Mastercam's
  path unreproduced instead of 52). Exact tool-centre region (retried after the orientation fix): v6/v7 op 36 end early (88.7 %
  missed). Exact local-link window sets: up to 90 % missed (op 3, 32, 36, 42). Both removed again.
- **Oracle** (tools/oracle.py --type dynamic, ORACLE_FILEOP=1, MCCE_MEASURE=exact; p95 of the worse direction):

| op | Clipper | exact before | exact after | after, MCCE_EXSETS=1 |
|---|---|---|---|---|
| BRACKET4 14 / bracket4-dashed 14 | 0.328 | 0.327 | 0.328 | 0.328 |
| BRACKET4 16 / bracket4-dashed 16 | 0.332 | 0.331 | 0.337 | 0.337 |
| NAKTO-BRAKE-BRACKET 2 | 0.648 | 0.648 | 0.643 | 0.643 |
| buckle-v10 1 | 1.207 | 1.082 | 0.107 | 0.076 |
| buckle-v10 10 | 0.412 | 0.392 | 0.377 | 0.410 |
| buckle-v10/v11/v12 3, v9 51 | 0.641 | 0.322 | 0.322 | 0.322 |
| buckle-v11 1 | 0.322 | 0.382 | 0.100 | 0.079 |
| buckle-v11/v12 10 | 0.413 | 0.385 | 0.385 | 0.385 |
| buckle-v11/v12 17 | 1.380 | 1.178 | 0.194 | 0.069 |
| buckle-v12 22 | 0.628 | 0.546 | 0.546 | 0.546 |
| buckle-v12 23 | 0.288 | 0.100 | 0.117 | 0.075 |
| buckle-v6/v7/v8 31 | 1.496 | 1.425 | 1.473 | 1.473 |
| buckle-v6/v7 32 (v8: 0.452 / 0.502 / 0.411) | 0.462 | 0.522 | 0.410 | 0.403 |
| buckle-v6/v7/v8 33 | 1.041 | 1.041 | 0.204 | 0.072 |
| buckle-v6/v7/v8 35 | 0.055 | 0.054 | 0.055 | 0.055 |
| buckle-v6/v7 36 | 0.260 | 0.230 | 0.222 | 0.224 |
| buckle-v8/v9 42 | 0.311 | 0.30 | 0.28 | 0.43 |
| buckle-v9 45 | 0.838 | 0.765 | 0.055 | 0.055 |
| buckle-v9 46 | 0.205 | 0.174 | 0.159 | 0.042 |
| tramming-rod 1 | 1.013 | 0.879 | 0.262 | 0.264 |
| **median / worst / ok** | 0.462 / 1.496 / 0 | 0.385 / 1.425 / 0 | **0.321 / 1.473 / 0** | 0.322 / 1.473 / 1 |

  Missed / extra material unchanged (worst 0.1 % / 0.2 %). MCCE_MEASURE=clipper: byte-identical to toolpaths2 on the 41 ops.
- **Remaining causes, ranked** (first divergence + unreproduced length, dyn_diverge / dyn_cover):
  1. buckle-v6/v7/v8 op 31 (worst, 1.47; 144 of 548 mm unreproduced): two side pockets (x 0-9 / 13-22, y 10-26) that
     Mastercam enters and clears as their own regions in another order (entries from z -0.55); ours reaches them from the main
     region. Region order (DetNestLvlShAr input = offset-core order, NOT DECODED) and the pockets' entries.
  2. Profile entry (NAKTO op 2: 900 of 2053 mm; BRACKET4 / -dashed 14 / 16: ~80 mm each): Mastercam ramps along a
     Chaikin-smoothed loop (runs of 0.03 mm moves) starting at the entity with the lowest source index; Chaikin not ported,
     source indices not tracked.
  3. Link / target sets on the Clipper bridge (op 10 x3, op 42 x2, op 32 x3, v12 op 22): e.g. buckle-v11 op 10 at the first
     lifted link Mastercam's exit arc has r 0.675 (0.75 x min radius), ours 0.45 - the larger candidates fail the containment
     test against polygonal sets. Exact sets help most ops but need an exact tool-centre region, whose stall is not understood.
  4. op 3 x3 / v9 op 51 (0.32): diverge at a depth change (ours ramps to -4.0 at 24.31, Mastercam stays at -3.75 along a
     smoothed run) - not classified further.
  Not affecting the corpus: pass bytes (§6.1.7; fp_red = 0 on all 41 ops); the "far" marker shortcut (equivalent: every sample
  of such a shape classifies 4); B9-5 plunge (done in dec2 Q1).
- Op scenes (tools/tests/op_compare.sh, the 73 Dynamic-related scenes, MCCE_MEASURE=exact; goldens NOT blessed - they hold
  the Clipper results): against the previous exact run 16 scenes change (toolpath point counts / NC only, no errors):
  dlg_dynamic, gen_dynamic_contour, gen_dynamic_mill, regen_dup_toolnum, rt_arcf_b10_dyn, rt_arcf_b10_planes,
  rt_b12_reload_stored, rt_dctour_hst, rt_dynamic, rt_dyn_b9_cut, rt_dyn_b9_helix, rt_dyn_medax_entry1, rt_dyn_medax_entry2,
  rt_dyn_medax_entry4p, rt_face_b7_dynamic, stored_recalc. Default mode (Clipper on this branch): unchanged.
- CPU (41 ops, exact): 20.0 s before and after (Clipper 113-122 s). TSan harness (buckle-v12 2 3 10 17 22 23): 0 races, all
  "same", in exact, exact + MCCE_EXSETS=1 and clipper mode; every new static is _Thread_local.

**Status 2026-11-07 (second accuracy pass: region order, exact link sets, depth changes)**: outputs of the 41 ops unchanged
in both modes (exact: oracle median 0.321 / worst 1.473 as before; MCCE_MEASURE=clipper byte-identical to toolpaths2). What was
found, item by item:
1. **buckle-v6/v7/v8 op 31 is not a region-order case** (correction of the ranking above). `tools/dyn_entries.py` lists
   Mastercam's feed-ins: per chain one region (199 mm, entered from the feed plane), then two more passes entered from z −0.5 =
   depth + 3 - retract links (FUN_180062140) inside the same region, not new regions. Our run follows the same main sweep (ours →
   Mastercam p95 0.056) and ends at the same point with "0 material loops left"; Mastercam still has two pieces (cut later,
   37.8 and 49.5 mm). Mastercam's extra passes lie wholly over our initial material (checked against a dump of it, MCCE_DEBUG=7
   MATDUMP lines), and both cut the same area inside the chains (303 mm2). So the two material models diverge during the sweep:
   our commits remove two small pieces (2-3 mm2, trace with MCCE_DEBUG=6) that Mastercam's leave. Where exactly needs Mastercam's
   per-step state (widths / material), which the NCI does not store - not decodable from the stored ops alone; a per-step trace
   of the original engine (the DLL under a debugger / the planned Windows-DLL loader) would settle it.
   Region order itself: DetNestLvlShAr's sort (§B.7) is decoded; the offset-core output order feeding it is still NOT DECODED, and
   no corpus op was found whose deviation comes from region order.
2. **Exact tool-centre region / exact link sets.** MCCE_EXTCR=1 (experiment, exact model): RG+0x30 and C+0x3c0 / C+0x3e8 built
   exactly. Alone it no longer stalls op 36 (completes, 0 material left) but is worse overall: 3 ops better, 10 worse (op 32
   0.41 -> 0.91, op 36 0.22 -> 0.44, op 10 / 42 / v12 22 worse), median 0.326. With MCCE_EXSETS=1 too, op 36 still ends early
   (88.7 % missed): the stall mechanism, traced (MCCE_DEBUG=4 state-2 steps): a stay-down link to a non-boundary target (idx 0,
   never blacklisted - FUN_180053cd0 filters boundary targets only) ends at (52.06, 10.03); state 0/1 gives a zero-length step
   with a valid attach point; state 2 walks cur.s[G.sh] from there away from the material (first step the lead-in arc, committed
   with width 0, then air), trims, and the same link repeats until the 32-iteration guard. Not fixed: why Mastercam's state 2 /
   attach go the other way here is not decided by the decode read so far (the sets differ from Clipper's only in geometry).
   Found on the way and ported (exact model): the trim FUN_18008ee40 leaves cetp+0x20 (last-cut index) as it is (no output change).
   **The link-route difference itself is the link smoothing FUN_18004a070** (decoded 2026-11-07, mcce_engines.md §6.9 list):
   distance list [10m, 4m, 2m, 1.125m, m, 0.9m … 0.1m], each try an opening + closing of the open route with
   MCCECalcContourOffset (+d, −d, −d, +d), first result inside the containment set kept. Mastercam's first link on op 32 ends at
   the same target as ours ((32.6253, 16.30) vs (32.625, 16.73)) - only the route differs, by this smoothing. Blocked on
   MCCECalcContourOffset's offset core (NOT DECODED, §6.13.2) and FUN_180049150; FUN_180049950 / FUN_180045c40 not read.
   MCCE_EXSETS stays opt-in (with the Clipper tool-centre region: 11 better / 3 worse, see above).
3. **ops 3 (x3) and buckle-v9 op 51 "depth change"**: z −3.75 is depth (−4.0) + the 0.25 micro-lift - Mastercam is still in a
   lifted link along a smoothed route (0.032 mm moves) where ours has already descended: the same link-smoothing cause as item 2.
Remaining, ranked: (a) link smoothing FUN_18004a070 / FUN_180049950 / FUN_180045c40 + MCCECalcContourOffset core (ops 3, 10,
32, 36, 42, v12 22 - the largest group); (b) op 31 material divergence (needs a per-step trace of the original engine);
(c) Profile entry smoothing (other agent); (d) exact tool-centre region (worse until (a) and the state-2 direction at an attach
point are settled).
Diagnostics added (ours): MCCE_DEBUG=3 region / material dump in regions_build, 4 state-2 step trace, 6 material loops after
each commit, 7 MATDUMP sample points; tools/dyn_entries.py.

**Status 2026-11-07 (third pass: offset core / link smoothing scoping)** - no output change (41 ops, both modes).
- The link smoothing FUN_18004a070 is reached in the corpus's configuration (P+0x261 = 0) only through connector 1 (1 link in
  BRACKET4 op 14 and its copy; `MCCE_DEBUG=3` now prints the stay-down strategy and the connector taken). FUN_180049150 /
  FUN_180049950 / FUN_180045c40 decoded (mcce_engines.md §6.9 list); the last two run only with P+0x261 set; FUN_180045c40's
  0.03 mm runs are linearised arcs. **Correction of the second pass:** the routed-link differences on ops 3 (9 of 10 links),
  10 (26-31), 36 (24), 42 (21), 32 (6), v12 22 (4) come from the guided planner FUN_1800f1080 (connectors 4/5 → FUN_180044340),
  which our port replaces by its plain-route fallback FUN_1800f1580 - not from the smoothing.
- Sizes (call trees in decomp/mcce_all.c): MCCECalcOffset / MCCECalcContourOffset core from FUN_1800d50d0: 175 functions,
  ~14 400 lines (raw offset FUN_1800dbce0, loop builder FUN_1800c9d70 → FUN_1800ca830 / FUN_1800cb410 / FUN_1800cb710 /
  FUN_1800caac0, crossings FUN_1800ca460, clean-up FUN_1800d47b0 → FUN_1800c7bc0 → SoPockUnionAr + the FUN_18015x/16x boolean,
  ~3 000 lines). Guided planner FUN_1800f1080: 413 functions, ~33 000 lines (top level decoded in mcce_engines.md).
- Not ported, by decision: the smoothing port would change one link of the corpus and still needs an own open-curve offset in
  place of MCCECalcContourOffset (its core NOT DECODED). Recommended order instead: (1) the guided planner FUN_1800f1080 (largest
  remaining group of deviations), (2) the offset core with its loop order / start entity (FUN_1800da6a0 by source index;
  needs the core's source links), which every region / link set depends on. Both are multi-pass decodes.

**Status 2026-11-07 (guided planner, part 1 - decode only, no code change)**: the planner FUN_1800f1080 is a trapezoidal-
decomposition path planner on the link region: preprocessing (end points made vertices, a 0.9·d offset, per-shape
re-start and splitting, cuts at concave corners and two region subtractions), rotation by the midpoint of the largest gap
of edge directions, horizontal cell decomposition from every vertex, cell graph, search, rotation back, post-processing,
containment check. Decoded so far (mcce_engines.md §6.16): the top level, FUN_1800f33a0, FUN_1800f2480, FUN_1800f3220,
FUN_1800f3440, FUN_1800f2b40, FUN_1800e9790 / FUN_1800ec930 / FUN_1800eb810 / FUN_1800eb6e0 / FUN_1800e9820 / FUN_1800e9440 (top
level), FUN_1800f18a0, FUN_1801009c0 / FUN_1801003d0 / FUN_1800fe1f0 / FUN_1800ffd30. Planned passes (each behind MCCE_GUIDED=1,
committed with the per-op oracle): (2) the decomposition (FUN_1800feab0, FUN_1801066f0, FUN_180108d70, FUN_1800ffa20, sorts) and
the cell graph FUN_1800f61a0; (3) the search FUN_1800eda20 + FUN_180136370 + FUN_1800f1580; (4) the preprocessing
FUN_1800f3de0 / FUN_1800e9c60 / FUN_1800ec230 / FUN_1800f2ca0 (needs the offset / boolean cores: our exact offsets marked as
such). The planner only changes outputs once all stages run, so the oracle moves at the end of pass (3) at the earliest.

**HANDOFF 2026-11-07 (guided planner, pass 2 started; context limit of the working session)** - nothing of the planner is
ported yet (no code change since 4462eed; outputs = toolpaths2). Where the work stands, for the next session:
- Spec so far: mcce_engines.md §6.16 (top level and stage entries, with line numbers). Tools: scratch scripts were used to
  print functions without boilerplate (`calltree.py ROOT DEPTH` lists the callee tree with line spans, `fn.sh NAME` prints a
  function from decomp/mcce_all.c without declarations) - trivial to recreate; the planner-specific code is the address range
  0x1800e9000-0x180104000 (134 functions, ~11 300 lines); the rest of the callee tree is shared kernels and STL.
- Pass 2 (slicing rest + cell graph), the functions that matter (the others in the FUN_1801009c0 tree are vector / std::sort
  internals: FUN_180103040, FUN_1801018e0, FUN_1800fbc40, FUN_1800fd360, FUN_1800fbf90 (= copy items into a vector),
  FUN_1800f8730, FUN_180101270, FUN_1800fb860, FUN_1800fb950, FUN_180103890, FUN_180103b50, FUN_1801035b0, FUN_1801032f0,
  FUN_180101a10, FUN_180102f10, FUN_180101bd0, FUN_1801014a0, FUN_1801017c0, FUN_1800fbba0, FUN_1800fb8d0, FUN_1800f9870
  (= vector erase-move), FUN_1801016a0, FUN_1800fbb10, FUN_1801031c0, FUN_1800f7b30 / f7940 / f79f0 / f5200, FUN_1800fba10; the
  sorts FUN_1800fa160 (0x28-byte records) and FUN_1800fa390 (0x88-byte edges) are std::sort (introsort: FUN_1800fa9b0,
  FUN_1800fb000, FUN_1800f8a60, FUN_1800f8ea0, FUN_1800fa5c0, FUN_1800f8250, FUN_1800f95d0, FUN_1800f93e0, FUN_1800f8b60,
  FUN_1800f9a00, FUN_1800f90c0, FUN_1800f9f70, FUN_1800fa6f0, FUN_1800f9d20, FUN_1800f83f0, FUN_1800f9710, FUN_1800f94c0,
  FUN_1800fc970) - their comparators decide equal-key order and must be read from the predicate calls inside them):
  - FUN_1800feab0 (138034-138511), read up to ~138330: the vertex record (0x80, index param_3) gets list A at +0x08 (CVe,
    begin +0x18) = copy of the vertex's 0x48 items (FUN_1800fbf90); pass over A: an item with class (+0x40) 1 for which
    FUN_1800fffc0(G, item) is true is erased; for an item following a non-erased one, FUN_1800fdce0(G, item) in {2, 3} → the
    item's +0x20..+0x3f is copied into the PREVIOUS item's +0x20..+0x3f (+0x-28..) and the item is erased (adjacent gaps
    merge). Then list B at +0x30 (CVe, begin +0x40) = a second copy (FUN_1800f8730), the same pass but a class-1 item is
    erased when FUN_1800fffc0 is FALSE; the rest of the function (138330-138511, FUN_180100900 / FUN_1801016a0 use) NOT YET READ.
  - still to read: FUN_1800fffc0 (138769-138910), FUN_1800fdce0 (137628-137745), FUN_180100860 (139074-139100, vertex count),
    FUN_180100900 (139101-139132, (shape, entity) -> record index), FUN_1800ffa20 (138600-138656), FUN_1800ff8b0 (138563-138599),
    FUN_1800ffc30 (138657-138697), FUN_1801066f0 (line x region crossings), FUN_180106d60, FUN_180108d70 (gap class), the
    comparators of FUN_1800fa160 / FUN_1800fa390; then FUN_1800f61a0's FUN_1800f6d10 (133612-133811), FUN_1800f6700
    (133394-133452), FUN_1800f68c0 (133453-133611).
- Item layout known so far (0x48): +0x00..+0x1f two CPt2-sized blocks copied from the crossing records (left end), +0x20..+0x3f
  the right end, +0x40 class (1 = the vertex alone, else FUN_180108d70's class), +0x44 'used' byte (FUN_1801009c0). Edge (0x88):
  +0x00 index, +0x08..+0x47 item i's four blocks, +0x48..+0x87 item j's.
- Port plan unchanged: viewer/mcce_dyn.c, a new block "guided planner" next to route() / conn_route(), behind MCCE_GUIDED=1
  (exact model only, MCCE_MEASURE=clipper untouched), used by conn_route (connectors 4/5, FUN_180044340) and stay-down strategy
  (5) (FUN_18005fb50) in place of the plain route when FUN_1800f1080 succeeds; all new statics _Thread_local; check each step
  with MCCE_DEBUG=3 (strategy / connector lines) and tools/dyn_diverge.py on op 32's first link (Mastercam: exit, arc r 1.35,
  line (31.546, 31.584) -> (30.683, 24.873), long arc linearised along x ~ 30.2 down to y 21.06, line to (29.493, 15.608), arc
  r 1.35, ramp, arc r 1.35 to the target (32.6253, 16.3041); NCI moves #495-#569 of buckle-v6 op 32).
**Status 2026-11-07 (mcce.dll oracle: Mastercam's own engine on our engine input)**: tools/peloader runs mcce.dll (user
decision N-2, no Wine; tools/peloader/README.md "mcce.dll - the Dynamic Mill oracle"): `MCCE_DUMPIN=PREFIX cam_cli PART fileop
OP` writes the input of each `mcce_calc_dyn_pock` call (McceParm by CDynPockParm offset + S1 / S2 / S3, viewer/mcce_trace.c);
`peloader mcce.dll --dyn PREFIX.N` builds the DLL's CDynPockParm (its exported constructor) / pock_bound lists from it, calls
MCCECalcDynPock and prints its CCetp records; `MCCE_RECSIN` feeds those records back through cam_cli's glue so tools/oracle.py
scores the original engine against the NCI. Per-step: the commit FUN_180035990 of the DLL is hooked (its incremental-link
thunk redirected) and the port calls `mcce_trace_commit` at the end of `commit()` (MCCE_TRACE); both write one line per
committed step (step entity, w1 / w2 = M+0x120, engaged sweeps, flags, material position, loops / entities / exact signed area
of C+0x410 and C+0x438; levels 2 / 3 add loops / entities, level 4 the DLL's measures FUN_180030be0); tools/mcce_trace_cmp.py
gives the first differing record / commit. tools/tests/mcce_dll_oracle.sh runs it all (-n: with oracle.py).
- **Harness validated**: the 37 stored mcce ops all run (0 failures). BRACKET4 op 14 (Profile entry): DLL records vs NCI p50 /
  p95 **0.006 / 0.038 mm** (the input is our polyline chain at chain_tol 0.002, Mastercam's had arcs); median p95 over the 37
  ops: DLL 0.250, port 0.321; DLL better on 18 ops, port better on 12 (> 0.02 mm). Thinning op 31's input polygon (111 -> 61
  vertices, <= 0.02 mm) changes the DLL's output by one record: the engine is not chaotic in its input here.

  | op (p95 vs NCI) | port | DLL on our input | port vs DLL, first structural difference (tol 0.2 mm / w 0.05 / area 0.5 mm2) |
  |---|---|---|---|
  | BRACKET4 / -dashed 14, 16 | 0.328, 0.337 | **0.038, 0.046** | record 0: entry start 1.3 / 6.2 mm apart; DLL 10635 records, port 3309 (Profile entry, §5 items 1 / 14) |
  | NAKTO-BRAKE-BRACKET 2 | 0.643 | 0.417 | record 0: entry 44 mm apart; DLL 17096 records, port 4713 (Profile entry) |
  | buckle-v10/v11/v12 3, v9 51 | 0.322, 0.321 | 0.644, 0.668 | commit 0: DLL step is an arc (r = R = 3, sweep -0.1) where the port's is a line |
  | buckle-v10/v11/v12 10 | 0.377-0.385 | 0.250-0.289 | commit 447: same arc-step pattern (r = R = 1.5) |
  | buckle-v12 22 | 0.546 | 0.691 | record 4 (entry, 0.33 mm), commit 1 arc step |
  | buckle-v6/v7 36 | 0.222 | 0.302 | commit 93: arc step (r = R) |
  | buckle-v6/v7/v8 32 | 0.410 | 0.567 | commit 2 / 8: the DLL's state-2 step is twice as long (0.44 vs 0.22 mm), same width |
  | buckle-v8/v9 42 | 0.28 | 0.433 | commit 433: arc step (r = R = 1.5) |
  | buckle-v6/v7/v8 31 | 1.473 | **1.473** | none: 615 / 615 commits per call agree (see below) |
  | buckle-v10/v11 1, v12 23, v9 46, v11/v12 17 | 0.10-0.19 | 0.042-0.079 | commit 7 / 8 / 14: the port's state-2 step ends at about half the DLL's length (0.24 vs 0.45 mm) with the same width |
  | buckle-v6/v7/v8 33, 35, v9 45, tramming-rod 1 | 0.055-0.262 | 0.055-0.072 | lockstep (op 45: commit 514 / 560; tramming-rod: commit 7566 of 7683) |

- **op 31 (buckle-v6/v7/v8; the worst op) - the hypothesis of the second pass is refuted.** On our input the DLL makes the same
  622 records per call as the port; the port follows it commit for commit over all 615 commits of each call (step <= 0.064 mm,
  w1 <= 0.012 mm, material area <= 0.046 mm2, the same loop count after every commit, both end with 0 loops), and its records
  score exactly the port's p95 1.473 vs the NCI; tools/dyn_cover.py leaves the same 144 mm of Mastercam's path unreproduced for
  both. tools/dyn_diverge.py on the DLL path: it follows Mastercam's for the whole first region (188 mm), then Mastercam goes on
  with its extra passes (moves #620-#1037) where the DLL ends the region. So Mastercam's own engine also removes the two pieces
  when it gets our input: the difference is in the **engine input** (the boundary sets / parameters mcmill hands to
  MCCECalcDynPock - §5 item 25, CHST2DProcess ConvertChainsToPockBounds / PackageMachiningRegion, the executor FUN_1802a5760 passes
  S2 = work item +0x50 or +0xa0 and S3 = +0x78, decomp/mcmill_hst2d_dyn.c l.397-416), not in the port's material model. Tried on
  the DLL without effect: the chain as S2 with a rectangle as S1 (622 records, same path). Next step: decode which S1 / S2 / S3
  mcmill builds for a from-outside op and test the candidates on the DLL (an input file is plain text).
- Port-side differences the oracle shows (for the engine work, not applied here): (1) the Profile entry (BRACKET4 / NAKTO; the
  DLL's records are now a per-record reference for it); (2) the DLL commits **arc steps** of radius R (sweep -0.1 rad or less)
  on the tool-centre region's arcs where the port, whose region is Clipper-linearised, commits lines (ops 3, 10, 22, 36, 51) -
  the exact tool-centre region (item (d) above) in the DLL's own form; (3) the entry start (record 0-2) is 0.03-0.18 mm off on
  most buckle ops; (4) a state-2 step that ends at about half the DLL's length with the same width (ops 1, 17, 23, 32, 46:
  commits 2-14; FUN_18006f1c0 / the shorten functions - cause not traced yet; the port's Clipper tool-centre boundary is a
  candidate); (5) move flag 4
  (FUN_180037070, the 1e-8 test) and 0x20 (FUN_1800167d0 refined candidate) differ on near-tie steps (op 31 record 485, op 35
  record 1469, op 45) - informational, the path is the same there.
- Limits: the DLL gets our inputs (polyline chains, our region split / boundary sets), so "DLL vs NCI" measures the inputs and
  the engine together; libm is glibc's (tools/peloader/README.md); registry values are the defaults.
**HANDOFF 2026-11-07 (guided planner, passes 2 and 3a done; next: post-process + region preparation)** - commits 8b41dab
(pass 2), 6804ac9 (pass 3a) and the linearisation commit after it. Code: viewer/mcce_dyn.c, block "guided planner (§6.16)"
(after route()); MCCE_GUIDED=1 (exact model only) runs `guided_plan(d, both, E, A, G)` from conn_route (connectors 4/5,
d = 0.9·P->d1c8 = 0.9·C+0x1d0) and stay-down (5) (d = 1e-6) and prints the decomposition, channel and search path at
MCCE_DEBUG=3 (traps at 4); it always returns 0, so the plain route stays in use and outputs are unchanged (oracle dynamic
37 ops p95 median 0.321 / worst 1.473 = toolpaths2 at 63e1a83; after merging toolpaths2 0a9fac9 the planner-off
outputs are toolpaths2's: 0.222 / 1.473). Decode: mcce_engines.md §6.16 "Pass 2" and "Pass 3a".
- Ported and cited: FUN_1800f33a0 (orient), FUN_1800f3220 / FUN_180124620 / FUN_180124110 (arc linearisation at 0.1·t,
  first call only), FUN_1800f18a0 (angle), rotation, FUN_1800f1580 (in-frame route), FUN_1801009c0 + its whole tree
  (records, bands, crossings, gap classes, items, lists, partners, traps), FUN_1800f61a0 (channel), FUN_1800eda20 (funnel
  search), rotation back. std::sort = MSVC introsort (gp_stdsort), crossing qsort = CRT (gp_msqsort), both [inferred].
- Check: buckle-v6 op 32's first link (connector 4/5): our raw search path is (33.84, 30.41) ... (32.65, 28.77) →
  (30.97, 24.72) → along x ≈ 30.45 down to y ≈ 19.7 → (32.33, 15.38); Mastercam: line (31.546, 31.584) → (30.683, 24.873),
  arc along x ≈ 30.2 to y 21.06, line to (29.493, 15.608), arcs r 1.35. Same topology; the ~0.25 mm offset and the arcs are
  the missing preparation (0.9·d inset) and post-process.
- Still to do, in this order (each needs the decode first; nothing of it is read beyond the entries in §6.16):
  1. DONE (gp_split_at): FUN_1800f2480 (E, A made vertices of G'; 131308-131553). Oracle: tools/tests/mcce_dll_oracle.sh
     runs the real MCCECalcDynPock on our input, but on buckle-v6 op 32 the engines already differ from commit 0 (records
     port 2003 / dll 1298), so whole-engine link comparison is weak: recommended - extend tools/peloader/mcce.c to call
     FUN_1800f1080 (and the stages FUN_1801009c0 / FUN_1800f61a0 / FUN_1800eda20) directly with our G', E, A and diff
     our gp_* stage outputs against them (a unit oracle of the planner).
  2. FUN_1800f3de0 (132267-132323) → FUN_1800f3440 (decoded), FUN_1800f3520 (131922-132266), FUN_18014f740(0.1·d),
     FUN_1800f21d0; FUN_1800f2b40 (131554-131607, decoded summary: degenerate test → planner fails).
  3. FUN_1800e9790 = FUN_1800ec930 (concave-corner cuts: FUN_1800eb810 decoded, FUN_1800e9820 bisection over
     FUN_1800ec230 (267 lines)) + FUN_1800e9440 (FUN_1800e9c60 (286 lines) and the boolean FUN_180153b30 = G' minus X:
     our exact boolean / offset where the core is NOT DECODED, marked as ours). Then FUN_1800f3220 again, FUN_1800f2ca0
     (131608-131794).
  4. FUN_180136370 post-process (166702): FUN_180136680 → FUN_1801368c0 (166850-166999, binary-search shortcut over
     FUN_1801380f0 / FUN_180138510 / FUN_1801353a0 / FUN_180135610, then FUN_1801351b0) - shared kernels, ~10 000 lines
     with callees; E + 0x10 (16-byte stride per out shape) is its direction input. Then the success test
     (|out| == 1 and sh_vs_list(out[0], G, 1e-5) & 4 == 0) and FUN_1800f0fa0's fallback.
  5. Only then let guided_plan return the path (exit + path + approach in conn_route; strategy 5 the same with its
     off-length FUN_18010a6b0) and measure with the oracle; make MCCE_GUIDED the default only if the oracle improves.
- NOT DECODED inside the ported part: CalcLnLnIntersectPt's parallel threshold (ln_ln 1e-300), CalcEntIntersect's hit
  order (the planner keeps hit 0), FUN_180125ec0's body (rotation assumed point-wise, arc start + a), the 4th argument of
  ApxArcWithLnSegs, the raw fields of 'P' entities beyond +0x18/+0x20, FUN_1800fe1f0's items[−1] read when a vertex has
  no crossing (guarded).

**Status 2026-11-07 (engine INPUT = mcmill's: from-outside machining regions decoded; agent branch worktree-agent-af65bbc1258020681)**
- Decoded (formats/dynamic.md "Machining regions (decoded)", §5 item 25): PrepareChains chain lists by role,
  ConvertChainsToPockBounds (reverse chain order), PackageChains region loop (FUN_1802a7cf0: first bound of the highest z,
  every role-0 bound its own region), PackageMachiningRegion -> PackageSomething (FUN_1801ac7e0 with stock + vtable +0x38,
  from outside: grown, largest piece kept; islands grown), the work item FUN_18029cb90 / FUN_1802b1430 (S1 = islands then
  region, S2 rest material / +0x348, S3 entry chains) and the executor FUN_1802a5760.
- **op 31 explained**: its "extra passes" are the two holes of each solid's top face, machined as regions of their own.
  Ported: `facing_regions` (cam2d_dyn.c) + `cam_solid_face_loops` / `xt_top_face_loops` (all loops of the face, CORRELATED).
  DLL oracle: 6 calls (622 / 109 / 148 / 148 / 622 / 109 records, port = DLL record counts), DLL p95 vs NCI 0.065, port 0.059
  (was 1.473 both).
- Oracle (ORACLE_FILEOP=1 --type dynamic, 37 ops): p95 median 0.222 -> 0.204, worst 1.473 -> 0.546; only op 31 (x3) changes.
  DLL oracle all 37 (tools/tests/mcce_dll_oracle.sh -n): port median 0.204 / worst 0.546, DLL median 0.075 / worst 0.691.
- **Remaining input differences: every op with avoidance islands.** DLL vs NCI p95: ops without islands <= 0.08 (BRACKET4 14 /
  16 0.04, v10/11 op 1 0.08, op 17 0.07, op 23 0.075, op 33 0.072, op 35 0.055, op 45 0.057, op 46 0.042, tramming-rod 0.063);
  ops with role-4 islands 0.25-0.69 (op 3 0.644, v12 op 22 0.691, op 32 0.567, op 36 0.302, op 42 0.433, op 51 0.668, op 10
  0.25-0.29) and NAKTO op 2 0.417 (stay inside). So Mastercam's engine on Mastercam's input matches its NCI where we have no
  islands, and something about the island input is still different. Tried on buckle-v12 op 3 (exact-arc input, peloader `B` form,
  scratch tools op3arc.py): exact arcs instead of polylines 0.644 -> 0.642 (not it); island growth 0 / 0.5 / 1 / 2 mm: 0.692 /
  0.646 / 0.642 / 1.342 (1 = the wall stock is right); region before island in S1: same; no island: 13.0. The DLL follows the NCI
  for 90 mm and first leaves it at a micro-lift link round the island (NCI: a smoothed route of 0.033 mm steps, DLL: line + arc
  r 6.67), i.e. the link smoothing FUN_18004a070 / containment set reacts to something in the input. Candidates not tested yet:
  the islands' pock_bound fields (chain_to_pock_bound decomp/mccore_pock.c l.1451: bound +0x18 = the chain z, byte +3 =
  pock_ccw, the chain's direction kept; entity +0x60 / +0x70 = source pointers, the first entity's +0x78 = z, +0x68 = t;
  the peloader leaves those 0), the parameter block bytes
  LoadDynPockParm leaves to the CDynPockParm constructor, and a version difference (the NCI is from Mastercam X9, the DLL is
  2017's).
- Glue (not input): the hole regions' feed-in rapid goes to depth + 3 (-0.5) where Mastercam's outer regions use top + 3;
  DECODED 2026-11-13 (dynamic.md "Nested regions" / "Region-to-region link": HasOuterPocket gives a nested region the outer
  region's z as top of stock; the depth writer FUN_180045770 links the groups): op 31 max 3.5 -> 0.226.
- Checks: TSan par_test (scratch build -O1 -fsanitize=thread): buckle-v12 ops 2 3 10 17 22 23 and buckle-v6 ops 31 32 33 35 36:
  0 races, all "same" (xt_schema_find's cache now has a mutex: solid chains are read on worker threads). op_compare.sh: 55/475
  differ, all of them only the PROJECT-FORMAT 2 header / msg="Translate" lines (golden files predate toolpaths2's project format;
  pre-existing) - no Dynamic scene changes.
- Next: (1) the island input (above); (2) region order / z for multi-depth chains and the containment (role 7), air (role 6),
  entry (role 5) chain paths (no user op has them); (3) stay-inside region building (the decode reads the same no-nesting
  region loop; ours unions the chains even-odd - check on a stay-inside op with nested role-0 chains before changing).
**HANDOFF 2026-11-07 (guided planner, pass 3b: unit oracle + region preparation; branch worktree-agent-af366c0149c1a402d)**
- Unit oracle built (tools/peloader/mcce_gp.c `--gp`, tools/gp_cmp.py, tools/tests/gp_unit_oracle.sh; README "Guided planner
  unit oracle"): `MCCE_GUIDED=1 MCCE_GPDUMP=PREFIX` dumps every planner input and the port's stage results; the loader
  calls mcce.dll's FUN_1800f1080 on the same input with every stage call hooked; `MCCE_GPFEED=1` restarts the port from
  the DLL's region after the first unported stage (now `cuts`). Decode of what it needed: mcce_engines.md §6.16 "Pass 3b"
  (real calling convention, E + 0x10 = the exit end tangent, CalcLnLnIntersectPt / CalcLnSegLnSegIntersect,
  FUN_180125ec0 + UpdMinMaxSh, FUN_1800f3de0 group, FUN_1800f2b40, FUN_180126ac0, FUN_1800f2ca0).
- Ported in this pass (viewer/mcce_dyn.c, block "guided planner"): gp_prepare (FUN_1800f3de0 / f3440 / f3520 / f21d0 +
  cs_merge = FUN_18014f740), gp_degenerate (FUN_1800f2b40; self-intersection NOT DECODED = never), gp_snap_ends
  (FUN_1800f2ca0), second linearisation, exact gp_lnln / gp_segseg, gp_rotate = FUN_180125b80, gp_upd_minmax.
- Agreement (6 ops, 90 calls, tools/tests/gp_unit_oracle.sh -f): every ported stage within 1e-6 of the DLL, mostly
  bit-identical; remaining ULP source: the plain route (route(), FUN_1800f1580 / FUN_18010c040; 12 calls, 1 ulp).
- Still to do, in order: (1) FUN_1800e9790 = FUN_1800ec930 (concave-corner cuts; FUN_1800e9820 bisection over
  FUN_1800ec230, 267 lines) + FUN_1800e9440 (FUN_1800e9c60, 286 lines, and the boolean FUN_180153b30 "G minus X": ours,
  marked) - check with the oracle's `cuts` stage (drop the gp_feed("cuts") then); (2) FUN_180136370 post-process (the
  DLL's output on buckle-v6 op 32 call 0: arc r 1.35 / arc / long arc r 17.8 / line / arcs - the r 1.35 arcs of
  Mastercam's path), compare at stage `post`; (3) the verdict FUN_180113300(out[0], G, 1e-5) & 4 (the DLL fails 19 of 90
  calls there: inside 0x6), stage `inside`; (4) let guided_plan return the path, conn_route / strategy 5 use it, then the
  whole-op oracle. Strategy (5)'s step and E + 0x10 (FUN_18005fb50) are not traced yet: the dump writes 0 / (0, 0).
- Outputs unchanged: MCCE_GUIDED is off by default and guided_plan still returns 0.
- Checks of this pass: 41 Dynamic ops byte-identical to toolpaths2 (5f30963) in both the default exact model and
  MCCE_MEASURE=clipper (NAKTO op 3 produces no moves in either build); TSan harness buckle-v12 ops 2 3 10 17 22 23 with and
  without MCCE_GUIDED=1: 0 races, all "same". Whole-op oracle not rerun (outputs unchanged by construction).
**HANDOFF 2026-11-07 (guided planner, after pass 3b: FUN_1800e9790 started, blocked by the session's context)** - no port
code for the cuts yet. Findings for the next session:
- FUN_1800e9790 (disasm 0x1800e9790-0x1800e97f3): FUN_1800ec930(xmm0 = **step** (the planner's 2nd argument, C+0x1e0),
  E, A, G), then tail-jump FUN_1800e9440(d, step, E, A, G). So the concave-corner cut size is `step`, not d.
- The loader now also prints stage `corners` (G after FUN_1800ec930, hook at call 0x1800e97c9) before `cuts` (after
  FUN_1800e9440). The cuts change the region on all 90 test calls (buckle-v6 32 call 0: prepare 45 lines -> corners 29 lines
  + 43 arcs -> cuts 28 lines + 56 arcs), so dropping MCCE_GPFEED needs both parts ported.
- FUN_1800ec930 (128104-128155): per shape, per entity k: FUN_1800eb810 (concave vertex: point, bisector angle) and not
  FUN_1800eb6e0 (within 1.5 step of E or A) -> FUN_1800e9820 (126642-126703): bisection on the cut size s from step: up to 9
  halvings of FUN_1800ec230(s, ...) success, then FUN_1800ec230(s_final - 1e-4) and the shape is swapped with the result
  (param_5[1] = the entity index it returns). FUN_1800ec230 (127836-128100): s >= 1e-7; centre = V + 0.75 s (cos a, sin a);
  FUN_1800ebc20 (127671-127722, the cut shape at that centre, radius s probably - read it); FUN_180133c70 (165530-165671,
  shape x cut-shape intersection list, 0x50 records) needs >= 2 hits; the hit pair around the vertex's position
  (entity + t/2 key, 127967-127991) must be mutual ring neighbours (+0x30 / +0x34); FUN_1800ebae0 (127643-127668) builds
  the candidate, FUN_1800eb550 (127451-127501) and FUN_1800eb750 (127525-127551) reject it, FUN_1800ebe50 (127725-127833)
  writes the result. FUN_1800e9440 (126522-126627): FUN_1800e9c60 (126764-127048) + boolean FUN_180153b30 (182601-183011,
  core NOT DECODED -> our exact boolean, marked ours).
- Then (unchanged order): post-process FUN_180136370 (stage `post`), inside test (stage `inside`), return the path, whole-op
  oracle + DLL oracle, MCCE_GUIDED default only if it improves.
**HANDOFF 2026-11-07 (guided planner, pass 4: cuts ported, the planner returns its path; branch worktree-agent-a434106e7ee3d6cbb)**
- Ported (viewer/mcce_dyn.c, block "guided planner"; decode in mcce_engines.md §6.16 "Pass 4"): the exact CalcEntIntersect of
  the planner (gp_ent_isect: gp_cirln = FUN_1800a9000, gp_arc_t = FUN_1800a4820, gp_circir = CalcCirCirIntersect, gp_arcln
  = CalcArcLnSegIntersect, gp_arcarc = FUN_1800a4460), gp_pair_isect = FUN_180133c70, gp_isect_list = FUN_180134270 for one
  X shape, the tangents gp_tan_in / gp_tan_out = FUN_18011ffd0 / FUN_1801204a0; concave-corner cuts gp_corners =
  FUN_1800ec930 (FUN_1800eb810, eb6e0, ebc20, ebae0, eb550, eb750, ebe50, ec230, e9820); end cuts gp_end_cut = FUN_1800e9440
  (FUN_1800e9c60, ea7b0, e9a80, eaf80); the verdict FUN_180113300 & 4 (sh_vs_list). E / A and their raw tangents E+0x10 /
  A+0x10 are passed as FUN_180044340 builds them (GPIN gains `Ad`; the loader reads it).
- Unit oracle (6 ops, 90 calls): `corners` bit-identical 90/90; `cutset0/1` equal up to the loop start 90/90; `cuts` equal up
  to the loop start 87/90 (buckle-v6 op 32 calls 3-5: one extra 1e-5 piece from our boolean); without the feed `search`
  matches on 82/90. MCCE_GPFEED=1 still feeds at `cuts` (the booleans are ours): then every later stage matches as before
  (1 channel diff from the plain route's ULPs, buckle-v6 op 36 call 2).
- NOT DECODED (ours, marked): the cores of SoPockUnionAr / FUN_180153b30 (exact boolean, source pieces kept via
  g_ex_keep_pieces); FUN_1800a9490 (degenerate line in FUN_1800a9000: no hit); the post-process FUN_180136370 (skipped: the
  returned path is the funnel's polyline, Mastercam's has the arcs); strategy (5)'s step / E+0x10 (dump only, no path).
- guided_plan returns the path (MCCE_GUIDED=1; conn_route uses it, else the plain route as FUN_1800f0fa0). Whole-op oracle
  (ORACLE_FILEOP=1 tools/oracle.py --type dynamic, 37 ops, p95 of the worse direction) off -> on: median 0.204 -> 0.204, worst
  0.546 -> 0.650; better: buckle-v10 10 0.377 -> 0.339; worse: buckle-v10/v11/v12 3 0.322 -> 0.644, buckle-v9 51 0.321 ->
  0.643, buckle-v12 22 0.546 -> 0.650, buckle-v11/v12 10 0.385 -> 0.393, buckle-v6/v7 32 0.410 -> 0.439, v8 32 0.411 ->
  0.440, buckle-v6/v7 36 0.222 -> 0.250, buckle-v8 42 0.281 -> 0.306, buckle-v9 42 0.278 -> 0.297, NAKTO 2 0.372 -> 0.382; the
  other 21 unchanged. DLL oracle (mcce_dll_oracle.sh -n buckle-v6 32): records port 2003 -> 1430 (DLL 1298), p95 vs NCI
  0.410 -> 0.439 (DLL records 0.567). Decision: MCCE_GUIDED stays OFF by default (worse without the post-process).
- Checks: MCCE_GUIDED off: 41 Dynamic ops byte-identical to toolpaths2 (4b53a55), default and MCCE_MEASURE=clipper (NAKTO op 3
  no moves in either); TSan par_test buckle-v12 2 3 10 17 22 23 with / without MCCE_GUIDED=1: 0 races, all "same".
- Next, in order: (1) decode FUN_180136370 (stage `post`; buckle-v6 op 32 call 0 must get the r 1.35 arcs): FUN_180136680 /
  FUN_1801368c0 (166792-166999) are read (§6.16 Pass 4), still to read: FUN_1801380f0 (167624-167757), FUN_180138510
  (167758-167878), FUN_1801353a0 (166191-166265), FUN_180135610 (166266-166605), FUN_1801351b0 (166114-166188), FUN_180138910
  (167879-) and the arc kernels FUN_1800a72d0 / FUN_1800a8660; (2) compare `inside` (the DLL rejects 19/90 after its
  post-process; our unprocessed path passes more often - a likely cause of the worse ops: check per call whether ours
  accepts where the DLL falls back); (3) rerun the whole-op oracle and decide the default again; (4) strategy (5)'s step
  and E+0x10 (FUN_18005fb50).
**HANDOFF 2026-11-07 (guided planner, pass 5: post-process ported, default decided; branch worktree-agent-a50e700cf73ecf397)**
- Ported (viewer/mcce_dyn.c, block "post-process FUN_180136370" before guided_on; decode mcce_engines.md §6.16 "Pass 5"):
  gp_post = FUN_180136370 / FUN_180136680, gp_pp_search = FUN_1801368c0, tangents FUN_1801380f0 / FUN_180138510 (+
  FUN_18013a020 / FUN_18013a3e0, FUN_1800a7310, FUN_1800a6f70, FUN_18013a8e0), the replacement FUN_1801353a0 (FUN_180137b70,
  FUN_180139a40, FUN_1801395e0 with FUN_180137a60 / 37880 / 37090 / 371c0 / 3ab60, FUN_18013ae30, FUN_180136090,
  FUN_180138910 with FUN_1800a72d0 / FUN_1800a8660), the single-entity arc FUN_180135610 (FUN_180137510, FUN_1800a8c00,
  FUN_1800a9600), the append FUN_1801351b0 / FUN_180136d40. Called with the planner's input group G (disasm 0x1800f12df).
- Unit oracle (6 ops, now 99 calls - the call sequence changed with the post-processed paths): with MCCE_GPFEED=1 `post`,
  `inside`, `result` bit-identical 99/99 (buckle-v6 op 32 call 0 gets the r 1.35 arcs); without the feed `post` 95/99 (the
  4 follow the search's input = our boolean in the cuts), `inside` / `result` 99/99. The DLL now rejects 1 of the 99 calls
  (buckle-v8 42 call 29, inside 0x6) and so does the port: the earlier 19 rejections were on the unprocessed paths' calls.
- Whole-op oracle vs the NCI (ORACLE_FILEOP=1 tools/oracle.py --type dynamic, 37 ops, p95 worse direction) off -> on:
  median 0.204 -> 0.204, worst 0.546 -> 0.717; better 1 (buckle-v10 10 0.377 -> 0.317), worse 12 (v10/v11/v12 3 and v9 51
  0.32 -> 0.64, v12 22 0.546 -> 0.717, v6/v7/v8 32 0.41 -> 0.59, v6/v7 36 0.222 -> 0.297, v8 42 0.281 -> 0.487, v9 42
  0.278 -> 0.498, NAKTO 2 0.372 -> 0.377); v11/v12 10 0.385 -> 0.376; the other 21 unchanged.
- DLL oracle (tools/tests/mcce_dll_oracle.sh -n, all 37 ops; the DLL's records on our engine input): the DLL itself vs the NCI
  has median 0.075 worst 0.691, and on exactly the ops that get worse the DLL is as far from the NCI as the guided port
  (v10/v11/v12 3: port 0.644, DLL 0.644; v9 51 0.644 / 0.668; v12 22 0.717 / 0.691; 32 0.588 / 0.567; 36 0.297 / 0.302;
  42 0.487-0.498 / 0.433). Port vs the DLL's own moves (scratch comparison, the oracle's sampling, p95 worse direction):
  off median 0.214 worst 0.735 -> on median 0.204 worst 0.435; 13 ops closer (v10/v11/v12 3 0.642 -> 0.211, v9 51 0.681 ->
  0.204, v12 22 0.735 -> 0.404, v6/v7/v8 32 0.602 -> 0.289, 36 0.281 -> 0.253, v10 10 0.384 -> 0.320, v11/v12 10 0.402 ->
  0.377), 2 farther (v8/v9 42 0.395 -> 0.435: the engine already diverges from the DLL at commit 91 there, before any
  link), 22 unchanged. Records port / DLL on buckle-v6 32: 2003 / 1298 off, 1306 / 1298 on.
- Decision: MCCE_GUIDED stays OFF by default (the rule: only if the NCI oracle improves overall - it does not). The guided
  planner reproduces mcce.dll; the remaining NCI deviation on those ops is the DLL's own on our input (the engine input /
  mcmill boundary building, cf. the island-input notes above, or the X9 / 2017 version difference) - turning it on is a
  user decision (it trades NCI agreement for DLL agreement on 12 ops).
- Checks: MCCE_GUIDED off, 41 Dynamic ops byte-identical to toolpaths2 (0dfba9f) in the default exact model and with
  MCCE_MEASURE=clipper (NAKTO op 3: no moves in either); TSan par_test (scratch build -O1 -fsanitize=thread, memory-capped
  scope) buckle-v12 ops 2 3 10 17 22 23 with and without MCCE_GUIDED=1: 0 races, all "same".
- Strategy (5) (FUN_18005fb50, 54973-55110, disasm 0x18005fce4-0x18005fd26): FUN_1800f0fa0(d 1e-6, step C+0x1e0, both,
  E = FUN_18008e4b0 (last point) with E+0x10 = FUN_18008e710 (last tangent), A = the target's point with A+0x10 = its +0x10,
  G, out, cross). FUN_1800f0fa0 has a 9th argument `cross` (disasm 0x1800f0fb8) that FUN_1800f1080 only hands to its
  in-frame plain route (0x1800f10e8 -> 0x1800f1265): FUN_1800f1580 zeroes it, then FUN_18010a6b0's off-line length of
  its route (130763-130880). Ported: stay_down's strategy 5 calls guided_plan with these (cross = the in-frame route's
  off length on success), else the plain route as before. No op of the 41 reaches strategy 5 with MCCE_GUIDED=1 (GPIN
  dumps with d = 1e-6: none), so it is not oracle-checked.
- NOT DECODED left in the planner: §5 item 19. Next: (1) the 1-ulp differences of route() (FUN_1800f1580 / FUN_18010c040);
  (2) why the DLL on our input leaves the NCI on buckle-v10 3 / v9 51 / v12 22 / 32 (engine input, see the island-input
  notes); (3) a strategy-5 test case (an op whose stay-down reaches FUN_18005fb50).
**HANDOFF 2026-11-07 (evening: Mastercam's NCI = the DLL with bUseCtourGuide = 1 and mcmill's island growth; the guided planner is NOT what X9 ran)**
- Experiment switches (all off by default; tools/peloader/mcce.c, viewer/cam2d_dyn.c, viewer/mcce_trace.c, marked EXPERIMENT):
  `MCCE_XISL=1` (cam_cli: the from-outside islands are the raw avoidance chains in chain direction, the wall stock written to the
  dump as `grow D`), `MCCE_GROWISL=auto|D` (peloader: each S1 island grown by the DLL's own MCCECalcOffset(1, D, b, &o, 0x101, 0,
  1e-6, 1e-6) as mcmill FUN_1801ac7e0 l.17287-17320), `MCCE_NOGUIDED=1` (peloader: FUN_1800f1080 returns 0 -> FUN_1800f1580),
  `MCCE_REG=name=int,...` (peloader: mcce::GetRegInt answers those registry values, decomp l.10199).
- buckle-v12 op 3: the X9 NCI's links run at d1c8 = 0.254 + 0.25 x 1.35 = 0.5915 from the grown island (the plain S4 route);
  the 2017 DLL's guided planner keeps 0.849 there. Island direction: no effect (the DLL output is byte-identical reversed).
  DLL vs NCI p95: default 0.644; + MCCE_GROWISL 0.644 (median 0.085 -> 0.057, cut area 577 -> 570 = NCI); + MCCE_NOGUIDED
  0.640 / 0.174 (median 0.006); `MCCE_REG=bUseCtourGuide=1` + growth: **0.030 / 0.035, max 0.068**. The NCI's 0.033 mm move
  runs are FUN_180045c40's ApxArcWithLnSegs(1e-4) of the r 1.35 arcs (connectors 4/5 with P+0x261 set, mcce_engines.md 6.3.3).
- All 37 ops (tools/tests/mcce_dll_oracle.sh -n; p95 worse direction vs the NCI):
  port (MCCE_GUIDED off) median 0.204 worst 0.546; DLL default median 0.075 worst 0.691; DLL MCCE_NOGUIDED (no growth) worst
  0.641; **DLL bUseCtourGuide=1 + growth median 0.065 worst 0.434**: v10/v11/v12 3 0.644 -> 0.035, v12 22 0.691 -> 0.058,
  v6/v7/v8 32 0.567 -> 0.049, 36 0.302 -> 0.068, v9 51 0.668 -> 0.020, v10 10 -> 0.131, v11/v12 10 -> 0.287; worse: v8/v9 42
  0.433 (no-planner 0.24); NAKTO 2 (stay inside) 0.416 unchanged; ops without islands unchanged.
- The user's registry hive (2017) has no Toolpaths\Dynamic Mill key: defaults (bUseCtourGuide 0) there. The NCIs are X9's:
  X9's linking behaves as 2017's bUseCtourGuide = 1 branch [inferred: the 2017 flag keeps the pre-planner linking].
- Next (needs the user's choice of target, X9 NCI vs 2017 default): port the P+0x261 branch - connectors 4/5 FUN_180042890 /
  FUN_180043c90, strategy 5's else branch (54851-54909), FUN_18004a070 / FUN_180048c70 / FUN_180048840 / FUN_180049150
  (smoothing with MCCECalcContourOffset - its internals are ours here), FUN_180049950, FUN_180045c40, FUN_180132200,
  FUN_1800457d0 (FUN_18012f3b0 NOT DECODED), FUN_1800458c0 - and the island input as raw chains grown one by one (our
  MCCECalcOffset port); then ops 42 / 10 / NAKTO 2.
**HANDOFF 2026-11-07 (late: the bUseCtourGuide = 1 branch ported and made the default (D-3); islands grown one by one)**
- Island input (viewer/cam2d_dyn.c, facing branch): each raw avoidance chain (chain direction kept) grown on its own by our
  MCCECalcOffset port (mcce_grow_islands in mcce_dyn.c = sl_offset_m, mcmill FUN_1801ac7e0 l.17287-17320; growth direction
  measured with the DLL, see the comment), no union. MCCE_ISLUNION=1 = the old Clipper union + cl_offset_nz. The MCCE_XISL
  experiment and the dump's `grow` line are gone (peloader MCCE_GROWISL=D still works; `auto` now reads 0).
- P+0x261 = McceParm.ctour_guide, default 1 (D-3; registry default 0), MCCE_CTOURGUIDE=0 = the old linking. Ported (mcce_dyn.c,
  block "contour-guide links", decode in mcce_engines.md §6.3.7): connector 4 FUN_180042890 (conn_ctour4), connector 5
  FUN_180043c90 (conn_ctour5), strategy 5's own loop (54851-54909), FUN_1800457d0 / FUN_18012f3b0 (self_isect: FUN_18012f620,
  FUN_180130060 + FUN_18012e7d0, FUN_18012ed20, FUN_18012e930), FUN_180132200 (remove_loops: FUN_180131740, FUN_180131240),
  FUN_1800458c0 (ends_aligned), FUN_18004a070 (link_smooth, also connector 1's call now), FUN_180048c70, FUN_180048840,
  FUN_180048f60, FUN_180049150, FUN_180049950 (junction_smooth), FUN_180045c40 (link_lin_arcs). route() takes a shape
  (route_sh).
- Decode corrections found by the new unit oracle (they change every mode): FUN_18011f480 skips a tangent pair on the wrong
  side (was: stop); FUN_18010af10 mode 1/2 passes over nodes not beyond i along the line before the "after i" test. 20 of the
  37 Dynamic ops change (all with stay-down links: BRACKET4 / bracket4-dashed 14 16, NAKTO 2, buckle ops 3 10 22 32 36 42
  51); the other 17 are byte-identical to e7de28a (also with MCCE_CTOURGUIDE=0 MCCE_ISLUNION=1 the same 20 change, from the
  decode corrections and connector 1's smoothing).
- NOT DECODED / ours: MCCECalcContourOffset's kernels (oc_offset is our construction, fitted to the DLL's pass outputs:
  trimming at |d|, cutting circles about input vertices, radius-0 arcs, join-arc start angles, split pieces kept split -
  all measured, CT_COFF); FUN_1800c7f00 in the loop removal; FUN_180146c10 (section fallback in route edges); our isect()
  uses our e_isect, not CalcEntIntersect + FUN_180133c70's dedupe: buckle-v9 op 51 call 0 gets three crossing records at one
  point where the DLL has fewer, so FUN_180048320 refuses the section and the junction is not smoothed; the DLL drops some
  zero-sweep radius-0 arcs that we keep (buckle-v10 op 10).
- Unit oracle (new): tools/tests/mcce_ct_oracle.sh PART OP ... (MCCE_CTDUMP in cam_cli, peloader --ct = tools/peloader/
  mcce_ct.c calling FUN_180042890 / FUN_180043c90 with the call sites hooked; stages ala / isab / nodes / route / selfx /
  smooth / loops / aligned / junction / lin / result / cr / link; CT_COFF=1 adds every contour-offset pass). On buckle-v12 3,
  v12 22, v6 32, v6 36, v9 51, v10 10, v8 42: 131 connector calls, connector + cross length equal 130 (v9 51 call 4), final
  link identical (1e-6) 95.
- Whole op vs the NCI (MCCE_REG=bUseCtourGuide=1 tools/tests/mcce_dll_oracle.sh -n, 37 ops, p95 worse direction):
  port before (e7de28a) median 0.204 worst 0.546; now median 0.194 worst 0.643; MCCE_CTOURGUIDE=0 0.194 / 0.636; the DLL
  on our (now grown) input 0.070 / 0.434 (0.065 / 0.434 with the DLL's own growth: our grown islands are chorded polylines).
  Per op before -> now (DLL): v10 10 0.377 -> 0.233 (0.144), v12 22 0.546 -> 0.395 (0.238), v6/v7/v8 32 0.41 -> 0.303 (0.116),
  36 0.222 -> 0.169 (0.074), BRACKET4 14/16 0.085/0.081 -> 0.075/0.070, NAKTO 2 0.372 -> 0.359; worse: v10/v11/v12 3 0.322 ->
  0.641 (0.063), v9 51 0.321 -> 0.643 (0.061), v11/v12 10 0.385 -> 0.431 (0.275), v8/v9 42 0.281/0.278 -> 0.471/0.472 (0.434);
  the rest unchanged.
- Why the whole op stays far from the DLL although the connectors agree: the region sets S2-S4 (and the tool-centre region)
  are Clipper polygons in the port and exact arcs in the DLL. A route along a polygonal S4 boundary turns by several degrees
  at every vertex, so FUN_1800458c0's 1-degree test rejects connector 4 where the DLL accepts it (buckle-v12 op 3: the DLL's
  run takes connector 4 at links where ours falls to connector 5 and runs ~0.59 = d1c8 off). MCCE_EXSETS=1 / MCCE_EXTCR=1
  do not help yet (0.642 / 0.646 on op 3). Also the engines diverge from commit 0 on op 3 (the material loop: 88 vs 77
  entities), so links start elsewhere.
- Checks: TSan par_test (scratch build -O1 -fsanitize=thread, systemd-run scope MemoryMax=6G) buckle-v12 2 3 10 17 22 23:
  0 races, all "same".
- Needs the user: the default. D-3 asked for this branch as the default and it stays so; the median improves (0.204 ->
  0.194) but the worst gets worse (0.546 -> 0.643) until the region sets are exact. MCCE_CTOURGUIDE=0 restores the old links.
- Next: (1) exact region sets / tool-centre region for the link sets (S2-S4) so routes are tangent-continuous; (2) isect()
  with CalcEntIntersect + FUN_180133c70's dedupe (v9 51); (3) the remaining 36 link differences of the unit oracle (smoothing
  of long routes: tiny join arcs, v6 36 / v10 10).

**M2 stage 1 (2026-11-13): call-tree map of FUN_1800f1080** (TODO M2, N13-16; branch worktree-agent-a2db03d796f74efe5)
- Correction of the TODO's premise: FUN_1800f1080 is not "top level only". Passes 2-5 (handoffs above) ported every stage,
  checked by the unit oracle tools/tests/gp_unit_oracle.sh. It runs only with MCCE_CTOURGUIDE=0 MCCE_GUIDED=1: under the
  default (D-3 / N11-2, P+0x261 = 1) connectors 4 / 5 are FUN_180042890 / FUN_180043c90 and strategy 5 has its own loop, so
  ~~mcce.dll itself never calls FUN_1800f1080 in the default configuration~~ **corrected by M3 (below): wrong for mcce.dll's
  own default.** P+0x261 is not in the op record: FUN_180069840 reads it from the registry (bUseCtourGuide, default 0) and the
  user's 2017 hive has no Dynamic Mill key, so mcce.dll runs with P+0x261 = 0 and every connector 4 / 5 call of ops 3 / 10 /
  32 / 36 / 42 / v12 22 goes to FUN_180044340 -> FUN_1800f1080 (call counts in "M3"). Only our port's default (D-3,
  ctour_guide = 1, chosen because the X9 NCIs follow that branch) never calls it. The third pass's attribution (2026-11-07:
  the routed links come from the guided planner) holds for mcce.dll as configured.
- Map: BFS over decomp/mcce_all.c from FUN_1800f1080 (thunks resolved; script: one pass of `// ==== NAME @ ADDR` blocks,
  callees = known function names followed by `(`): **484 functions, 39 929 lines** (the earlier "413 / 33 000" counted
  without thunk targets of shared kernels). "Ported" = the address is cited in viewer/mcce_*.c by a port (not as NOT
  DECODED); "STL" = vector / std::sort / allocation internals (no geometry; the port uses its own containers, gp_stdsort =
  MSVC introsort, gp_msqsort = CRT qsort).

  | stage (call order in FUN_1800f1080) | role | decomp lines | subtree fns / lines | status |
  |---|---|---|---|---|
  | AppendByCopy | G' = copy(G) | 13582 | 3 / 163 | ported (sh copy) |
  | FUN_1800f33a0 | orient: outers -1, islands +1 | 131850 | 2 / 203 | ported gp_orient |
  | FUN_1800f2480 | E, A made vertices of G' | 131308 | 11 / 823 | ported gp_split_at (rest STL) |
  | FUN_1800f3220 (x2) | arcs -> chords at 0.09 d (FUN_180124620 / FUN_180124110, ApxArcWithLnSegs) | 131795 | 10 / 529 | ported |
  | FUN_1800f3de0 | per-shape restart / pieces / collinear merge (f3440, f3520, f21d0, FUN_18014f740) | 132267 | 38 / 3065 | ported gp_prepare (FUN_18013bf10 = bbox, in cs_merge) |
  | FUN_1800f2b40 | degenerate test (self-crossing FUN_18012f2b0, area / dir FUN_180126ac0 + FUN_1801270e0) | 131554 | 28 / 1962 | ported; **self-crossing and arc term completed in stage 2 below** |
  | FUN_1800e9790 = FUN_1800ec930 + FUN_1800e9440 | concave-corner cuts (eb810, eb6e0, e9820, ec230, ebc20, ebae0, eb550, eb750, ebe50) and end cuts (e9c60, ea7b0, e9a80, eaf80) | 126630 | 21 / 2050 without the boolean | ported gp_corners / gp_end_cut |
  | ... inside FUN_1800e9440 / FUN_1800e9c60: FUN_180153b30 and SoPockUnionAr | region boolean "G' minus X" / union | 182601, 2138 | **105 / 10 342** | **core NOT DECODED** (ours: exact boolean, marked); see below |
  | FUN_1800f2ca0 | snap E / A onto G' | 131608 | 11 / 1266 | ported gp_snap_ends |
  | FUN_1800f18a0 | slicing angle = middle of the largest gap of edge directions (sort FUN_1800eff40 = std::sort of doubles: f0400, ef660, eef00, ef1c0, efc60) | 130890 | 9 / 816 | ported |
  | RotationMx + FUN_180125ec0 (x2) | rotate G' by a, the path back | 11801, 158095 | 4 / 181 | ported gp_rotate |
  | FUN_1800f1580 | in-frame plain route (FUN_18010c040, route edges FUN_18010af10 / FUN_18010a960) | 130763 | 40 / 2911 | ported route(); the route-edge chord fallback FUN_180146c10 completed in stage 2 below |
  | FUN_1801009c0 | trapezoidal decomposition: records, bands, crossings (FUN_1801066f0), gap classes (FUN_180108d70), items, partners (FUN_1800feab0, FUN_1800ffa20 ...) | 139133 | 105 / 6927 | ported; 74 uncited functions are std::sort / vector internals (FUN_1800fa160 / fa390 introsort trees, FUN_180104c50 / FUN_180105080 partition of 0x70 records) |
  | FUN_1800f61a0 | cell graph / channel (f6d10, f6700, f68c0) | 133165 | 20 / 1617 | ported |
  | FUN_1800eda20 | funnel search | 128580 | 14 / 811 | ported |
  | FUN_180136370 | post-process: binary-search shortcut (FUN_1801368c0), tangents, arc replacement (FUN_1801353a0, FUN_180135610), append | 166702 | 48 / 5406 | ported gp_post; FUN_1800afd30's degree 3 / 4 roots (b1540, b1980, b06b0 / b09e0, af4e0, af730, b11e0) unused (only degree 2 is called) |
  | FUN_180113300 | verdict: path inside G (& 4) | 148467 | 14 / 703 | ported sh_vs_list |

- What is left NOT DECODED in the whole tree: (1) **the boolean core** FUN_180153390 (271 lines, under FUN_180153b30 413)
  with FUN_1801523f0 (227), FUN_18013c2c0 (47), the pairwise primitive FUN_18015b2e0 (61; FUN_180159230 379, FUN_180159c60
  315, FUN_18015a4f0 218, FUN_18015aac0 71 - partly read, ENG 6.5.6 / 6.7.3) and SoPockUnionAr (623) -> FUN_18015c940 ->
  FUN_180163a90 -> FUN_180162010 (221: FUN_180160d00, FUN_180161090, FUN_1801615d0, FUN_18015f270, FUN_18015eea0 ->
  FUN_18015fda0 -> FUN_18015f790 -> FUN_180128bb0 / FUN_18015e720 ...): 16 geometry functions / 2 709 lines uncited, 58
  container functions / 3 169 lines; (2) FUN_180146c10 (route-edge chord; done in stage 2). Everything else in the tree is
  ported or STL.

**M2 stage 2 (2026-11-13): the small NOT DECODED leaves of the planner tree, ported** (viewer/mcce_dyn.c, mcce_medax.c)
- FUN_18012f2b0 (l.163187-163222) = FUN_18012f3b0(shape, list, **mode 2**); FUN_18012f620 returns after the first recorded
  crossing pair in mode 2 (l.163434-163436). Ported as self_isect_m(.., 2) / gp_self_cross; gp_degenerate (FUN_1800f2b40)
  now tests it first per shape (was "never").
- FUN_1801270e0 (l.158734-158835): an arc's term of FUN_180126ac0 (sa wrapped to [-pi, pi], sw clamped to +-2pi, pieces
  ending at 0 / pi then every pi: ((b - lo) - sin(b - lo)) r^2 + chord term, times dir). gp_arc_area; gp_area_dir uses it
  (was: chord term missing).
- FUN_1800a9490 (l.92174-92208): FUN_1800a9000's branch when CalcLnLnIntersectPt returns 0 (|d|^2 < 1e-14 or the residual
  check, l.3611-3640): the quadratic |p + t d - c|^2 = r^2 by FUN_1800afd30(2, ..). gp_cirln_poly (was: no hit). The degree-0
  path of FUN_1800afd30 is now decoded too: FUN_1800b06b0 at degree 0 adds no root (l.95691), FUN_1800b12f0's Newton polish
  (l.96117-96185) stops at once (|p'| = |c1| < 1e-14) - poly_roots2 was already exact, its "[inferred]" is gone.
- FUN_180146c10 (l.175639-175720): FUN_18010af10's fallback when the section copy appends nothing - one 'L' pointAt(e1, t1)
  -> pointAt(e2, t2): modes 0 / 1 from i to j (disasm 0x18010b0dc / 0x18010b52c), mode 2 from j to i (0x18010b316-0x18010b33c:
  edx = j's entity, r9d = i's). route_edge used to return an empty edge there. Shared with the default contour-guide links.
- Checks (MCCE_CTOURGUIDE=0 MCCE_GUIDED=1 for the planner): gp unit oracle (tools/tests/gp_unit_oracle.sh, buckle-v12 3 / 22,
  v6 32 / 36, v10 10, v8 42: 101 planner calls) - report byte-identical before / after; stages equal to the DLL: orient /
  split / linearise1 / prepare / **degenerate** / corners / angle / route / **inside** / **result** 101/101, hbt 80, funnel 72,
  search 94, post 98; cutset / cutbool / cuts and everything fed by them (linearise2, f2ca0, rotate, traps, channel) differ on
  all calls (our boolean; the pass-4/5 notes counted "equal up to the loop start", gp_cmp.py compares literally).
  ct unit oracle (mcce_ct_oracle.sh, 7 ops, 146 connector calls): connector + cross 146/146, final link 108/146, unchanged.
  All 37 Dynamic ops of <user-parts> (tools/dyn_batch.py ops_of), default and MCCE_CTOURGUIDE=0 MCCE_GUIDED=1:
  74/74 move files byte-identical to dc535a5 - the new paths are not reached on the corpus, so the NCI oracle is unchanged
  (0.194 / 0.643 as recorded 2026-11-07).
- **Where M2 stops / next step**: the only NOT DECODED part of FUN_1800f1080's tree is the region boolean. Order for the next
  session: (1) FUN_18015b2e0 (187022-187082) = the pairwise intersection: box overlap test (1.1920928955078125e-07 slack) and
  FUN_18015c370 (no-overlap shortcut), then FUN_18015aac0 (71) -> FUN_180159c60 (315) -> FUN_18015a4f0 (218) -> FUN_180159230
  (379) -> DetNestLvlShAr; (2) FUN_180153390 (182330-182600) = X minus Y: the joint box (FUN_18013c2c0) grown by 10, a
  rectangle (FUN_180124c50) minus Y's shapes pairwise, FUN_18015b2e0 with X, DetNestLvlShAr, FUN_1801523f0 (227), and the
  vector shell FUN_180153b30; (3) SoPockUnionAr (2138-2760) -> FUN_18015c940 -> FUN_180163a90 -> FUN_180162010 (221) and its
  tree (FUN_180160d00, FUN_180161090, FUN_1801615d0, FUN_18015f270, FUN_18015eea0 -> FUN_18015fda0 -> FUN_18015f790 ->
  FUN_180128bb0 / FUN_18015e720). Check: a new peloader mode calling FUN_180153b30 / SoPockUnionAr on the planner's dumped
  cutset / G' (the gp dump already has both) and the gp oracle's cutbool0/1 stages. The same boolean builds the link region
  sets S2-S4 that keep the default contour-guide links off the DLL (handoff above), so it serves both.

**M3 (2026-11-13): mcce.dll's region boolean ported, DLL-identical; who builds the routed links** (TODO M2; branch
worktree-agent-ae8225a36b873ef64)
- Port: viewer/mcce_sobool.inc (included by mcce_ctour.c, reusing its DLL-exact kernels isab_list = FUN_180133c70,
  sec_in_shape = FUN_1801489a0, shape_in_shape = FUN_18013e910, det_nest = DetNestLvlShAr, close_gaps = FUN_18012c740,
  close_forward = FUN_18014b720), API mcce_region_bool (mcce_ctour.h). Decoded and ported (decomp/mcce_all.c lines in the code):
  FUN_180153b30 (X minus Y group by group of Y, DetNestLvlShAr between) -> FUN_180153390 (X with each island of Y taken as an
  outer, plus X with [Y's outer as island, the box of both grown by 10 (FUN_18013c2c0 / FUN_180124c50)]; DetNestLvlShAr;
  FUN_1801523f0 drops groups of area < tol^2 (FUN_18010f0e0)); FUN_18015b2e0 (outer boxes within 1.19e-7, FUN_18015c370
  shortcut = union classification codes 3 / 2 / 4, then FUN_18015aac0 (intersection of the outers + copies of the islands),
  FUN_180159c60 (two or more leading islands united by SoPockUnionAr inside a box +1, re-nested), FUN_18015a4f0 (non-islands
  inside the islands' holes intersected with the trailing results), FUN_180159230 (every non-island minus each island,
  FUN_18015e180), DetNestLvlShAr); the shape operations FUN_1801638f0 (intersection; no crossing -> containment 4 / 2 / 3),
  FUN_180163a20 (union), FUN_18015e180 (minus) over the core FUN_180162010(op 0 union / 1 minus / 2 intersection): box test
  1e-7, FUN_180161f60 (b to a's direction), identical shapes FUN_1801615d0 / coincident boundaries FUN_180161090 (+
  FUN_18013ccc0), FUN_180161f90 (orientation per op), FUN_180133c70 records, FUN_180160d00 classes (FUN_1801489a0: 4 / 2 ->
  kept 1 / dropped 3 per op; cleared without both), FUN_180162520, the default trace FUN_18015eea0 (FUN_180160b30 start,
  FUN_1801627a0 walk with the visit table, FUN_1801625f0 step back over zero joints, FUN_18015fda0 ring = section copies
  FUN_180146e20, FUN_18015f790 for rings through undecided records: FUN_180128bb0 retrace test, FUN_18012f3b0 crossings,
  FUN_18015e720 loops; FUN_1801604e0 / FUN_180160650 after), FUN_18012c740(1e-10, 1e-10); SoPockUnionAr -> FUN_18015c940
  (FUN_18015c370, FUN_18015c5e0, FUN_18015c520) with its replace-and-rescan loop. Not ported: FUN_18015f270 (the
  bUseOldBuildSoFx = 1 trace; registry Toolpaths\Util, default 0, FUN_180162a00). CSh +0x68 child lists: empty here, not kept.
- Unit oracle: tools/tests/sobool_oracle.sh (peloader --sobool = tools/peloader/mcce_sobool.c: the DLL's FUN_180153b30 /
  FUN_18015b2e0 / SoPockUnionAr with the core thunk and FUN_18015b2e0's five steps hooked; cam_cli sobool prints the same
  stages). Cases: tools/sobool_gen.py (60 synthetic: rectangles, two-arc circles, slots, islands; minus / isect / union) and
  tools/sobool_from_gp.py (the planner's own 202 FUN_180153b30 inputs, from the gp oracle's new "cutin" stage, six ops):
  **262 / 262 identical stage by stage** (every core call, aac0 / c60 / a4f0 / 9230 / pw, result).
- Wired (viewer/mcce_dyn.c sl_bool_dll): the planner's FUN_1800e9440 (FUN_180153b30(1e-6, G, X)) and FUN_1800e9c60
  (SoPockUnionAr(1e-6, two circles + quadrilateral)); with MCCE_EXSETS=1 also S2 = FUN_180153b30(1e-6, C+0x340, out1)
  (FUN_180062a70 l.56630). gp unit oracle (MCCE_CTOURGUIDE=0, 101 planner calls on buckle-v12 3 / 22, v6 32 / 36, v10 10,
  v8 42) before -> after: cutbool0 0 -> 81, cutbool1 0 -> 78, cuts 0 -> 78, linearise2 0 -> 100, f2ca0 0 -> 100, traps 0
  -> 100, channel 0 -> 96, hbt 80 -> 96, funnel 72 -> 96, search 94 -> 101, post 98 -> 101, rotate_back 94 -> 101 (result
  101 both). The remaining cutset / cutbool / cuts differences are only the shape box (1e-4: the DLL keeps the side circles'
  entity boxes from before their 1e-4 shrink in FUN_1800e9c60; mcce_dyn's Ent has no entity box) and vanish at linearise2;
  channel / hbt differ on 4 calls whose traps are identical (FUN_1800f61a0, not the boolean; next for the planner).
- **Which mcce.dll code builds the routed links** (peloader --dyn on our engine inputs, new MCCE_COUNT: every call site of
  the listed functions redirected to a counting stub; sites from objdump, scratch list): P+0x261 is not stored in the op
  (FUN_180069840 l.~60376 reads bUseCtourGuide from the registry, default 0; mcmill's LoadDynPockParm never writes it), so
  for every op it is 0 in the DLL as the user runs it. Counts per op, registry default (P+0x261 = 0) | bUseCtourGuide=1:

  | op | FUN_180042890 (conn 4) | FUN_180043c90 (conn 5) | FUN_180044340 | FUN_1800f1080 | FUN_1800f1580 in f1080 / fallback | strategy 5 FUN_18005f4c0 / FUN_18005fb50 |
  |---|---|---|---|---|---|---|
  | buckle-v12 3 | 9 / 9 | 0 / 0 | 9 / 0 | **9 / 0** | 9 / 0, 0 | 0 |
  | buckle-v12 22 | 4 / 4 | 0 / 0 | 4 / 0 | **4 / 0** | 4 / 0, 0 | 0 |
  | buckle-v6 32 | 6 / 6 | 0 / 0 | 6 / 0 | **6 / 0** | 6 / 0, 0 | 0 |
  | buckle-v6 36 | 17 / 17 | 0 / 0 | 17 / 0 | **17 / 0** | 17 / 0, 0 | 0 |
  | buckle-v10 10 | 26 / 26 | 1 / 1 | 27 / 0 | **25 / 0** | 25 / 0, 0 | 0 |
  | buckle-v8 42 (= v9 42) | 24 / 24 | 2 / 3 | 26 / 0 | **22 / 0** | 22 / 0, 0 | 0 |
  | buckle-v9 51 | 9 / 9 | 0 / 0 | 9 / 0 | **9 / 0** | 9 / 0, 0 | 0 |

  So under mcce.dll's own default every routed link of these ops is FUN_1800f1080's (FUN_1800f0fa0's fallback FUN_1800f1580
  is never taken); with bUseCtourGuide = 1 (our D-3 default, the X9 NCIs' branch) the planner is never called and the links
  are connectors 4 / 5's contour-guide path. The guided planner is therefore the right target for the 2017 DLL's default;
  with the boolean done its whole tree is ported (left: channel FUN_1800f61a0 on 4 / 101 calls).
- The ct link oracle's 38 / 146 mismatches (mcce_ct_oracle.sh, bUseCtourGuide = 1 path, first differing stage; the region
  sets are the same input on both sides there, so none of them is the boolean): (a) 25 in the link smoothing FUN_18004a070
  (its MCCECalcContourOffset passes FUN_180048840 / FUN_180048c70, ours: oc_offset): buckle-v8 42 x12 (calls 6-16, 23) a
  radius-0 arc the port keeps at the end of the smoothed link that the DLL drops; v12 3 x4, v9 51 x2, v10 10 x2, v6 32 x1:
  the tiny join arcs (~1e-3 long) trimmed 3e-5 apart, entity counts then diverge; v6 36 x4: a different smoothing radius
  accepted (0.0675 vs the DLL's 0.27); (b) 8 in the junction smoothing FUN_180049950 (v9 51 x3, v12 22, v6 32, v8 42 x3;
  incl. the crossing-record multiplicity noted before); (c) 4 where both sides fail the connector (result / cr equal) and only
  the box of the empty output differs (v10 10 calls 18 / 22, v8 42 calls 17 / 21) - not a real difference; (d) 1 (v8 42 call
  25): the intersection record at an entity joint lands on entity 48 t = 1 in the port, 49 t = 0 in the DLL - our isect()
  (e_isect) instead of FUN_180133c70, which is now DLL-exact in mcce_ctour.c (isab_list) and can replace it.
- NCI oracle (37 ops, ORACLE_FILEOP=1 tools/oracle.py --type dynamic): default unchanged by construction (the boolean is
  reached only with MCCE_GUIDED=1 / MCCE_EXSETS=1): median 0.111 worst 0.643 this run. Planner path: see TODO M2.
- Next: (1) the planner's channel FUN_1800f61a0 (4 calls); (2) for the contour-guide branch: isect() -> isab_list
  (FUN_180133c70), then MCCECalcContourOffset for FUN_180048840 (a, the 25) and FUN_180049950 (b); (3) exact S1 / S3 / S4
  need MCCECalcOffset itself; its FUN_1800d3d30 (several result rings) needs FUN_1800d4170, FUN_1800d42a0, FUN_18015b1c0,
  FUN_180152cb0 on top of the boolean ported here.


**M2 links (2026-11-13): the contour-guide links (D-3 default) DLL-exact on the connector unit oracle** (TODO M2; branch
worktree-agent-ad10432c899f82051, on top of M3)
- Step 1 (7cbefb9): isect() in viewer/mcce_dyn.c = FUN_180134270 (l.165674-165779) built from the DLL-exact FUN_180133c70 already used
  by the planner (gp_pair_isect: SetChnMinMax boxes within 1e-5, CalcEntIntersect = gp_ent_isect, MSVC qsort by X LAB_1800074dc, dedupe
  FUN_180133b30 / FUN_180134800), records stamped X shape / Y shape, the whole list MSVC-qsorted again and linked per Y shape
  (FUN_1801349c0). FUN_180134690 (= isect1) is FUN_180134270 with X = {x} (l.165782-165815). Was e_isect + glibc qsort.
- Step 2 (e95aa3c): the link smoothing's MCCECalcContourOffset (FUN_180048840 l.43188-43191: (1, d, {shape}, 0x101, 0.0, -1.0, 1e-6,
  1e-6), CSh overload) is viewer/mcce_ctour.c mcce_contour_offset (the contour port, formats/mcce_ctour_offset.md 5.3). Was oc_offset
  (ours). New unit oracle tools/tests/coffct_oracle.sh (the DLL's own pass inputs / outputs at 0x180048a11, CT_COFF=1).
- Step 3 (3cc63aa): the two SoPockUnionAr paths of MCCECalcContourOffset that the port refused (ReconfigSelfIsectSh bit 0x80000
  l.1018-1096; FUN_1800d3d30 l.114214-114389 with FUN_1800d42a0, FUN_18015b1c0 -> FUN_1801638f0) use the M3 boolean.
- Oracles (7 ops buckle-v12 3 / 22, v6 32 / 36, v10 10, v8 42, v9 51):

  | oracle | before (c9d2790) | step 1 | step 2 | step 3 |
  |---|---|---|---|---|
  | ct link equal (mcce_ct_oracle.sh, 146 calls) | 108 | 117 | 142 | 142 |
  | coffct passes exact (coffct_oracle.sh) | - | - | 43048 / 43063 (15 refused) | 43063 / 43063 |
  | mcce_coff_oracle.sh corpus / var / syn | 781 / 4800 / 17976 (+24 refused) | = | = | 781 / 4800 / 18000 |
  | gp unit oracle (MCCE_CTOURGUIDE=0) channel / traps / result | 97 / 101 / 101 of 101 | 103 / 103 / 104 of 104 | = | = |
  | sobool_oracle.sh (60 synthetic + 72 from this run's planner) | 262 / 262 (M3's set) | - | 132 / 132 | 132 / 132 |
  | Dynamic NCI 37 ops (median / worst p95) | 0.111 / 0.643 | 0.111 / 0.643 (6 ops better) | 0.111 / 0.643 (+2 better) | = |
  | fileop 205 ops: moves changed vs before | - | 21 | 21 (12 of them changed again) | 0 vs step 2 |

  Ops whose moves change (all Dynamic): BRACKET4 14 / 16, bracket4-dashed 14 / 16, buckle-v10 3 / 10, v11 3 / 10, v12 3 / 10 / 22,
  v6 32 / 36, v7 32 / 36, v8 32 / 42, v9 42 / 45 / 51, NAKTO-BRAKE-BRACKET 2. Better at p95: NAKTO 2 0.361 -> 0.351, v11 / v12 10
  0.332 -> 0.331, v6 / v7 / v8 32 0.306 -> 0.303, v6 / v7 36 0.111 -> 0.110; none worse.
- The ct oracle's planned order was: (d) isect (1 call), (a) smoothing (25), (b) junction (8). Step 1 fixed 9 calls (the isect call
  and the crossing-record multiplicity of the junction cases), step 2 all the rest of (a) and (b): the "radius-0 arc kept", "tiny join
  arcs 3e-5 apart" and "radius 0.0675 vs 0.27" cases were all oc_offset. FUN_180049950 itself needed no change.
- The gp planner's channel FUN_1800f61a0 (4 / 101 calls before): with the same planner input (buckle-v12 22 gp.3) it is now equal -
  the difference came from the list order of isect(), not from the channel code. Left in the gp oracle: buckle-v10 10 gp.23, from cutset1
  on (the cut set's shape box: the DLL keeps the side circles' entity boxes from before their 1e-4 shrink in FUN_1800e9c60; mcce_dyn's Ent
  has no entity box) - the boolean then differs.
- **Where this stops / next**: the links are DLL-exact on the unit oracle (left: 4 calls where both sides fail and only the empty
  output's box differs - the DLL's CSh keeps a stale box). The NCI gap (median 0.111 / worst 0.643) is therefore upstream of the
  connectors: the region sets S1 / S3 / S4 (MCCECalcOffset, FUN_1800d3d30 now ported for the contour offset; MCCECalcOffset itself
  NOT PORTED: FUN_1800d48b0's frame ring, FUN_180152cb0 for several outer-derived rings) and MCCE_EXSETS's S2. Next: port MCCECalcOffset
  on top of mcce_ctour.c + the boolean and switch the link sets to it; the entity box of the planner's cut circles (gp.23).


**M2 offset (2026-11-13): MCCECalcOffset ported DLL-exact; the link region sets S1 / S3 / S4 go through it** (TODO M2; branch
worktree-agent-a8fe68dcaeede06c4, on top of int-m2)
- Port: viewer/mcce_offset.inc (included by mcce_ctour.c after mcce_sobool.inc; API mcce_offset / mcce_dir_nest in mcce_ctour.h). It
  reuses MCCECalcContourOffset's core (raw ring FUN_1800dbce0, loop removal FUN_1800c7bc0, ring filter FUN_1800d4400) and the M3 boolean.
  New, read from decomp/mcce_all.c: the export l.7804-7960 (state, |d| >= 1e-6, the copy otherwise); FUN_1800da1c0 with every flag bit
  (l.117939-118048: CeFlags 0, FUN_1800d99e0, bit 1 without the closedness test (param_2 true here) FUN_18012c2b0 + FUN_18012c740, bit 0x20
  FUN_180132ef0 = self_split, bit 8 dir of the last piece, bit 0x10 / count change DetNestLvlShAr); the frame FUN_1800d92f0 (l.117465-117547,
  GetMinMaxViaSh of the shape boxes + 2.5 |d| n); FUN_1800da5e0; the nest groups FUN_1800d50d0 / FUN_1800d4e80 / FUN_1800d4fd0 (l.114847-114998:
  an island is offset when its group's outer box diagonal >= 1.97 |d|); the frame ring FUN_1800d48b0 (l.114651-114840: the box of the group's
  rings so far grown by |d|, four lines FUN_180124c50, CeFlags 1, source {frame}); the group assembly FUN_1800d3d30 (l.114216-114389) with
  FUN_1800d4170 (rings by their first source's +0x54 bit 0), SoPockUnionAr, FUN_1800d42a0, FUN_18015b1c0 (FUN_1801638f0 for every outer ring x
  hole) and FUN_180152cb0 (l.182106-182300: outer-derived rings minus each island-derived one: FUN_18015dc60 = core op 1 + FUN_1801239f0,
  else FUN_18013e910 with its param_5 "all on the boundary"); the tail FUN_1800d3b60 / FUN_1800d3cc0 / FUN_1800da6a0 + FUN_1800d9530 (the start
  entity by the lowest source index over the whole input list) and FUN_1800d8d90 (FUN_1800d6180; without bit 0x100 also FUN_1800d62d0 /
  FUN_1800d7100). The CSh source list +0x78 (FUN_1800d3cc0 reverses an output shape back when every source carries the FUN_1800da5e0 mark) is
  carried as Sh.nsrc / unm / srcisl: the raw ring {src} (l.118909-118934), every ring cut from it (FUN_1800c6c90), the boolean's walk rings
  (FUN_18015fda0 l.189761-189800: A's list when a section of A was used, B's appended when one of B was) and loops (FUN_18015e720
  l.189030-189066), copies (CSh::operator=).
- Refused (McceResult.refused): n != 1, Infinite look ahead off (0x20000), point shapes ('P', FUN_18011f2e0), an open piece from the split,
  more than 64 shapes in one nesting tree. None is reached by the corpus calls below.
- Unit oracle: tools/peloader --off (tools/peloader/mcce_off.c: the CVe<CSh*> overload with FUN_1800d3d30 (call 0x1800d51f0) and FUN_1800d3b60
  (call 0x1800d39a0) hooked: stages in / rings / group / pre / result, entities with CeFlags), port side tools/off_port.c (= cam_cli sooff),
  tools/tests/off_oracle.sh. Cases: tools/off_gen.py (synthetic: outers with notches / rounded corners / slots / circles, up to four islands,
  level-2 regions, several groups, both signs); peloader --dyn with MCCE_OFFDUMP=PREFIX writes every MCCECalcOffset call of the DLL's own
  Dynamic Mill run (21 call sites: the region builder FUN_180028xxx-18002cxxx flags 0x127 / 0x101 / 0x1, FUN_180062a70's S1 / S3 / S4 0x100,
  the local window sets FUN_180066ff0 0x100, ...) with its in-engine output (.ref; the replay through --off equals it on all 1211 cases).
  Result: **1411 / 1411 identical** (200 synthetic + 1211 from buckle-v12 3 / 22, v6 32 / 36, v10 10, v8 42, v9 51, BRACKET4 14,
  NAKTO-BRAKE-BRACKET 2), 0 refused.
- Wiring (viewer/mcce_dyn.c region_sets, default on, MCCE_DLLOFF=0 = the old Clipper / own construction): S1 = UpdDirShAr(0.001) +
  DetNestLvlShAr + MCCECalcOffset(1, R - r, mat, 0x100, 0, 1e-6, 1e-6) (l.56625-56628); S3 / S4 = MCCECalcOffset(1, -f d1 / -f d2, group of S2,
  0x100, ...) (l.56710-56713); where the port refuses, the old construction. S2 stays the Clipper difference (MCCE_DLLS2=1: the DLL boolean;
  measured: same p95 on every op, one entry point moved 4e-6). With S1's CeFlags now the DLL's, FUN_180050410 (l.47123-47318, called by
  FUN_1800502a0) is ported: the target walk on S1 steps over the corner entities (CeFlags bit 2) within C+0x1e8 (= P+0x1e0, the march step)
  - before, it was a "NOT DECODED, no skip" note.
- Measured (all 37 Dynamic ops, ORACLE_FILEOP=1 tools/oracle.py --type dynamic; MCCE_DLLOFF=0 = before):
  p95 median 0.110 -> 0.075, worst 0.643 -> 0.643 (the median sits on a gap of the distribution: 17 ops at or below 0.075 before, 19 after; it
  moved because buckle-v11 / v12 op 17 went 0.202 -> 0.072). Per op (p95 / max, worse direction): better v11 / v12 17 0.202 / 0.202 ->
  0.072 / 0.196, v6 / v7 / v8 31 max 0.226 -> 0.102, v6 / v7 / v8 32 0.303 -> 0.301, v8 / v9 42 0.437 -> 0.435, v9 45 0.056 -> 0.054,
  v10 1 0.069 -> 0.062 (max 0.201 -> 0.270); worse v11 / v12 10 0.331 -> 0.393 (max 1.463 -> 1.333), v9 46 0.150 -> 0.164 (max 1.221 ->
  0.931), v11 1 / v12 23 0.054 / 0.055 -> 0.057 / 0.058 (max 0.086 -> 0.205), v6 / v7 / v8 33 0.057 -> 0.059, v6 / v7 36 0.110 -> 0.113,
  v10 10 0.437 -> 0.438, tramming-rod 1 max 2.842 -> 2.900. 9 better, 11 worse at the printed p95, 17 same.
- mcce.dll's engine on our input (tools/tests/mcce_dll_oracle.sh -n, MCCE_REG=bUseCtourGuide=1, buckle-v12 10 / 17 / 23): before, the
  port's first record already differed from the DLL's (record 0 on all three); now the records agree up to record 443 / 262 / 540 (op 23:
  every commit equal), and the port vs the NCI equals the DLL vs the NCI on 17 (0.072 / 0.071) and 23 (0.058 / 0.058, max 0.205 / 0.206 - the
  "worse" max of op 23 is Mastercam's engine's own on this input). Op 10: port 0.393, DLL 0.277 - the rest is downstream of record 443.
- ct link unit oracle (7 ops, 146 calls): 141 / 146 links equal (before 142 / 146; the sets are new inputs on both sides): the 4 both-fail
  stale-box calls (now v10 10 calls 19 / 23, v8 42 calls 17 / 20) and v8 42 call 21: the link smoothing FUN_18004a070 takes a different pass
  sequence from pass 30 on (port d 6.75, DLL 0.405) while every MCCECalcContourOffset pass is exact (coffct_oracle.sh v8 42: 6083 / 6083) -
  the smoothing's distance choice (FUN_180048840 / FUN_18004a070) on this input is the next thing to read.
- fileop of the 205 user-part ops: vs MCCE_DLLOFF=0 exactly the 37 Dynamic ops differ, nothing else; vs master's cam_cli (12:47 build) the
  same 37 (24 of them already differ through int-m2). NAKTO-BRAKE-BRACKET op 3 fails in all three (pre-existing).
- Regressions: mcce_coff_oracle corpus 781 / var 4800 / syn 18000 exact (unchanged), sobool_oracle 60 / 60.
- **Where this stops / next**: (1) v8 42 call 21's smoothing pass choice; (2) v12 10 from record 443 on (mcce_dll_oracle.sh -l 3 on it);
  (3) the other region sets that still use the Clipper / own offset (the tool-centre region RG+0x30 and the region builder FUN_18002xxxx,
  the local window sets FUN_180066ff0, C+0x3c0 / 0x3e8): every one of their MCCECalcOffset calls is in the unit set above and identical, so
  switching them is wiring + measuring; (4) S2 = FUN_180153b30 (ported, MCCE_DLLS2) with C+0x340 itself still built by Clipper; (5) planner
  gp.23 (FUN_1800e9c60 cut circles' entity boxes) - not started.


**M2 sets (2026-11-13): link smoothing verdict, zero-length trial step, the region builder on the DLL-exact offset; window sets opt-in**
(TODO M2, N13-16; branch worktree-agent-a036f5997a1cd06b5, on top of M2 offset)
- Step 1 (547b3b8), ct oracle buckle-v8 42 call 21: every MCCECalcContourOffset pass and every FUN_180049150 "changed" flag was equal
  (new CT_SMDBG stages on both sides: reat / vs / vsi / vss, peloader hooks 0x180048a41 / 0x18004a71b / 0x180113397 / 0x180113561); the
  difference was FUN_180113300's verdict on the d = 0.4725 result: port 2 (inside), DLL 6. Its pieces 0.03 outside the region were
  classified by mcce_dyn's own PtInSh (a 5-degree chord crossing count, "l.33-206" in name only). mcce_dyn's pt_in_sh is now mcce_ctour.c's
  DLL-exact PtInSh (FUN_180143460 band collection, FUN_1801453d0, the ray to box x1 + 10 per run with FUN_180144460) on the entities'
  SetChnMinMax boxes and the shape's stored box (new mcce_pt_in_shape_ents); MCCE_PIS=0 = the old count. ct link oracle 141 -> 142 / 146
  (left: the stale-box both-fail calls). Dynamic NCI: 4 ops change, median / worst unchanged.
- Step 2 (d8f9ccb), mcce.dll on our input, buckle-v12 10 record 443: the port's step 446 started 0.15 away from the end of step 445 (a
  broken path, record 449 -> 450). New trace lines on both sides (MCCE_TRACE_LEVEL / --trace-level 4: "M" = every FUN_180030be0 measure,
  5: + the call site (DLL return address / mcce_dyn.c line) and "T" = every trial line FUN_180017a10 with its heading and length). Both
  asked the same trial (heading 0, length 0.147); attach left a zero-length step, and FUN_18001c510 (l.20655-20905) then makes the step
  the walked piece of the candidate path from its start (FUN_18006fc10(path, start, np, step) l.20759) - the port only moved the attach
  point (zero_len_step had no step argument). Fixed. Alone (MCCE_DLLRG=0): 10 ops better, 0 worse.
- Step 3 (d8f9ccb), the region builder through mcce_offset / the M3 boolean where the DLL calls them (MCCE_DLLRG bit mask): bit 1 the
  tool-centre region RG+0x30 (FUN_18002b840 l.28163-28171: MCCECalcOffset(1, -d, M, 0x127), then (+P+0x48, 0x100), FUN_1800d96d0 CeFlags 0);
  bit 2 A1 / A2 (FUN_18002ae40 l.27756-27770, 0x127), the material and bull regions (FUN_180028cb0 l.26696-26704: MCCECalcOffset(1,
  max(x - 1e-4, 0), G, 0x127) + FUN_180153b30(1e-6, ., A); the bull offset is R - 1e-4, the old construction used R) and the sliver filter
  (FUN_1800298d0 l.27075-27084: 0x101 shrink, 0x100 grow); bit 4 C+0x3c0 / 0x3e8 (FUN_18002a2a0 l.27643-27651, 0x100). Measured per bit
  against the zero-length fix alone: bit 1 20 ops worse (median 0.076 -> 0.194), bit 2 2 better / 0 worse, bit 4 12 better / 2 worse;
  bits 2 + 4 together 12 better / 2 worse (v11 / v12 10 0.307 -> 0.324). Default 6; bit 1 opt-in. With bit 1 the initial material equals
  the DLL's (v12 10: material loops / entity counts / area identical at commit 0; v6 32 first differing record 139 with MCCE_DLLRG=0 -> 500 with 7), but the
  per-step consumers of RG+0x30 are not DLL-exact yet: v12 10 then leaves the DLL at record 1460 (a re-entry link after a state-2 air
  exit lands 0.05 away: the target choice), v6 32 at record 500 (same kind, 0.077 along the wall). The material model also differs by
  single entities from early on (v6 32 commit 133: 477 vs 476 entities, areas equal to 1e-5): the material subtraction (FUN_180037790, ours).
- Window sets (opt-in, MCCE_DLLWIN=1): FUN_180066ff0's S1 (UpdDirShAr + DetNestLvlShAr + MCCECalcOffset 0x100), S2 (FUN_180153b30),
  S3 / S4 (l.59065-59070) and the window W itself (FUN_180066320 l.58607 FUN_18015b2e0(1e-6, rect, C+0x340)) through the DLL ports. NCI:
  median 0.072 -> 0.084, worst 0.643 -> 0.436, 13 better / 15 worse: v10 / v11 / v12 3 and v9 51 0.643 -> 0.084 / 0.071, tramming-rod 1
  0.260 -> 0.069, v12 22 0.399 -> 0.220, v6-v8 32 0.251 -> 0.145; worse v11 / v12 17 0.072 -> 0.396, v8 / v9 42 0.242 -> 0.436, v9 45
  0.054 -> 0.123, v6 / v7 36 0.077 -> 0.113. Against mcce.dll on the same input (mcce_dll_oracle.sh -n): tramming-rod 1 port 0.069 = DLL
  0.069 (was 0.260), v8 42 port 0.436 = DLL 0.435 (the default's 0.242 is not what Mastercam's engine does here), v12 22 0.220 vs DLL 0.238,
  v12 3 0.084 vs 0.062; v12 17 0.396 vs DLL 0.071: at commit 555 the DLL stays down to the window target (16.313, 5.026) while the port's
  window link takes a single target (14.880, 7.032) and a lifted link - the window target list (FUN_180066ad0) is the next thing to compare
  (needs a DLL hook on the target list). Default off; question under "Waiting on the user".
- Measured (default build, all 37 Dynamic ops, ORACLE_FILEOP=1 orfiles): p95 median 0.075 -> 0.072, worst 0.643 -> 0.643; 14 better / 0
  worse vs the M2 offset merge: BRACKET4 / bracket4-dashed 14 0.075 -> 0.070, 16 0.070 -> 0.067, v10 10 0.438 -> 0.193, v11 / v12 10 0.393
  -> 0.324, v6 / v7 / v8 32 0.301 -> 0.251, v6 / v7 36 0.113 -> 0.077, v8 / v9 42 0.435 -> 0.242 / 0.248. fileop 205: vs master (rg_new6)
  exactly the 37 Dynamic ops differ; NAKTO-BRAKE-BRACKET 3 fails as before. Unit oracles: ct link 7 ops 144 / 145 (the one left: v10 10
  call 25, both fail, stale box only); coffct 15130 / 15130 passes exact (v8 42 + v12 22); off_oracle 1383 / 1383 (200 synthetic + 1183
  engine cases); sobool 60 / 60.
- The ops that got worse with N13-18, against mcce.dll on the same input (default build): v12 23 / v11 1 / v10 1 port = DLL (0.058 / 0.057 /
  0.062 both; every commit's geometry equal on call 0); v9 46 port 0.164, DLL 0.163 (first differing commit 1185, w 0.011); v11 / v12 10 port
  0.324, DLL 0.277 (first differing record 443: the initial material's island loops come from the Clipper tool-centre region, bit 1 off).
  The first record differences "mf 02c4 / 02e4" on these ops are the move flag 0x20 only (geometry and feed equal): FUN_180030de0 ORs the
  measure flags into the block and only FUN_1800380f0 clears 0x100 (l.34529), so the 0x20 that FUN_1800167d0 sets on the circle-heading
  variant (l.17797) survives the trial step's final measure (0x18006dc75); mcce_dyn's measure_block clears the block. Not changed (no
  output effect: mf 0x20 is not read by the glue); noted.
- **Where this stops / next**: (1) the window target list of FUN_180066ff0 / FUN_180066ad0 (v12 17 commit 555 with MCCE_DLLWIN=1) - hook
  the DLL's target list and compare; then decide DLLWIN's default; (2) the re-entry target after a state-2 air exit with bit 1 on (v12 10
  record 1460, v6 32 record 500); (3) the material subtraction's entity splits (FUN_180037790; v6 32 commit 133); (4) the measure-flag
  accumulation (0x20); (5) S2 = FUN_180153b30 (MCCE_DLLS2) with C+0x340 still Clipper unless bit 1; planner gp.23 not started.


**M2 window targets (2026-11-13): FUN_18005a8a0's contour offset DLL-exact; window sets + tool-centre region match mcce.dll on 27 / 37 ops**
(TODO M2, N13-16 / N13-20; branch worktree-agent-ae0659c76810ed991, on top of M2 sets)
- New oracle traces (both sides, same format): MCCE_TRACE_WIN=1 with MCCE_TRACE (port) / --trace (tools/peloader --dyn) writes the window
  link's target list FUN_180066ad0(C, W, S1, S2, S4, path, C+0x730, k, T) ("WT pre" after its sort FUN_18004d4b0, "WT fin" with the window
  sets' loop / entity counts / areas) and the general list FUN_180052a90 ("GT pre" / "GT fin"), one " wt" line per CTarg (pt, dir,
  +0x20/21/22, run-in, A, B). MCCE_TCDBG=1 (stderr): every target-circle walk step FUN_1800502a0 and the entry's reference chn
  FUN_180076500 (its three sets, the line made). Port only: MCCE_PORT_OFFDUMP=PREFIX writes the window S1 MCCECalcOffset input as
  peloader's MCCE_OFFDUMP (site 0x180067212) does; MCCE_DEBUG=3 prints every record at full precision.
- buckle-v12 op 17 commit 555 (MCCE_DLLWIN=1): both lists had one target, port (14.880, 7.032) from the target-circle walk FUN_1800512c0, DLL
  (16.313, 5.026) with +0x20 = 0 from FUN_180059ca0 (the +0x70 path target of the S1 shape nearest the last point, FUN_180052d70
  l.48447-48451). The port's FUN_180059ca0 failed: its Q (FUN_18005a8a0 l.52455-52462: MCCECalcContourOffset(1, +-P+0x230, {P}, Q, 0x100,
  0.0, 1e-6, 1e-6, 1e-6), P = the S1 section from the last point to 3 rho beyond the motion circle, FUN_18005ae90) was our own one-sided
  polyline offset ("NOT DECODED" note, before mcce_ctour.c existed). Now the DLL-exact port: new mcce_contour_offset_sh = the export's CVe
  overload (AppendByCopy keeps +0x50, so the open P stays open without flag bit 1; FUN_1800ce9a0's doubled-shape rule kept); Q is the whole
  output list (FUN_180134270({A2}, Q) l.52086) and FUN_18005a8a0 fails on an empty Q (l.52466). MCCE_NPCOFF=0 = the old construction.
  Op 17 with MCCE_DLLWIN=1: every commit equal to mcce.dll (was 555), p95 0.396 -> 0.071 (= DLL).
- Not the cause, ruled out on the way: the window's S1 is the DLL-exact offset (its DLL input replayed through tools/off_port: identical);
  its 1-entity difference (161 / 159 vs 158) comes from the material being 1e-8 .. 1e-6 off (the entry: with bit 1 off the entry's region
  sets are Clipper-built (S2 28 vs 8 entities on op 17), so the reference chn FUN_180076500 lands 0.004 away; with MCCE_DLLRG=7
  MCCE_DLLS2=1 the entry is exact and the first difference is the material subtraction's entity splits, item 3 below).
- Measured (37 Dynamic ops, ORACLE_FILEOP=1 tools/tests/orfiles.py dynamic on fileop runs; "before" = the same build with MCCE_NPCOFF=0,
  identical to the M2 sets numbers):
  | config | before median / worst | now median / worst | vs before | vs default (now) |
  |---|---|---|---|---|
  | default | 0.072 / 0.643 | 0.072 / 0.643 | 37 byte-identical moves | - |
  | MCCE_DLLWIN=1 | 0.084 / 0.436 | 0.071 / 0.436 | 11 better, 0 worse | 15 better, 9 worse |
  | MCCE_DLLRG=7 | 0.203 / 0.907 | 0.203 / 0.907 | 37 same | 1 better, 20 worse |
  | MCCE_DLLWIN=1 MCCE_DLLRG=7 | 0.063 / 0.435 | **0.062 / 0.435** (ok 2) | 10 better, 0 worse | **21 better, 3 worse** |
  (MCCE_DLLRG=7 alone measured 0.194 / 0.789 under M2 sets; the same build's MCCE_NPCOFF=0 gives 0.203 / 0.907 now, unchanged by this
  step.) The ops that got better through this step (DLLWIN and DLLWIN + DLLRG=7 alike): v11 / v12 17 0.396 -> 0.071, v9 45 0.123 -> 0.054,
  v6 / v7 / v8 31 0.063 -> 0.060 (max 0.802 -> 0.102), v10 / v11 1 0.069 / 0.059 -> 0.062 / 0.057 (max 1.155 -> 0.261), v10 / v11 / v12 10.
  DLLWIN + DLLRG=7 vs default, per op (p95 worse direction): BRACKET4 / bracket4-dashed 14 0.070 -> 0.054, 16 0.067 -> 0.045, v10 / v11 / v12
  3 and v9 51 0.643 -> 0.062 / 0.061, v10 10 0.193 -> 0.171, v11 / v12 10 0.324 -> 0.266, v11 / v12 17 0.072 -> 0.071, v12 22 0.399 -> 0.238,
  v6 / v7 / v8 32 0.251 -> 0.115, v6 / v7 36 0.077 -> 0.075, v9 46 0.164 -> 0.163, tramming-rod 1 0.260 -> 0.069; worse: v8 / v9 42 0.242 /
  0.248 -> 0.435 (mcce.dll on the same input: 0.435, every commit equal up to 6622), NAKTO-BRAKE-BRACKET 2 0.351 -> 0.354 (mcce.dll on the same
  input 0.390; differs from record 0 in both configurations, see below).
- mcce.dll on our input, all 37 ops (MCCE_DLLWIN=1 MCCE_DLLRG=7 MCCE_REG=bUseCtourGuide=1 tools/tests/mcce_dll_oracle.sh -n): every commit
  equal on 27 ops (v10 / v11 1, 3; v11 / v12 17; v12 3, 22, 23; v6 / v7 / v8 31, 32, 33, 35; v6 / v7 36; v9 45, 46, 51; tramming-rod 1)
  - their first differing records are the move flag 0x20 only (FUN_180030de0, noted under M2 sets) - and port p95 = DLL p95 on 29 of 37; median / worst port 0.062 / 0.435, DLL 0.062 / 0.435.
  Left: v10 10 (commit 6608; port 0.171, DLL 0.144), v11 / v12 10 (commit 6538; port 0.266, DLL 0.277), v8 / v9 42 (commit 6622; 0.435 both),
  BRACKET4 / bracket4-dashed 14 / 16 (record 0: the plunge entry's centre - our bisection; with MCCE_MEDAX=1 (CalcMedAx, FUN_180079050)
  op 14's records are all equal to the DLL's, 0.038 = DLL), NAKTO-BRAKE-BRACKET 2 (record 0: a different entry region, initial material
  area 179 vs 1163; also at record 0 in the default configuration, where the start is 0.156 away; port 0.354 is closer to the NCI than
  the DLL's 0.390).
- The re-entry target after a state-2 air exit with bit 1 (M2 sets item 2: v12 10 record 1460, v6 32 record 500) is gone with the window
  sets on: v6 32 every commit equal, v12 10 now first differs at commit 6538 (record 7450). With bit 1 alone (DLLWIN off) the local link
  still takes the Clipper window sets, which is why bit 1 alone is worse: the two belong together.
- The remaining v11 / v12 10 commit 6538: the materials agree in area but not in entity count from early on (647 vs 643 entities at commit
  6535): the material subtraction FUN_180037790 / its collinear merge FUN_180036250 (our col_merge leaves the pass-byte condition out); then
  the link after the piece ends (commit 6537) goes to (57.60, 10.37) instead of (68.75, 4.75).
- Step 3 (4d6d07b), the start regions: with every MCCECalcOffset input dumped on both sides (peloader MCCE_OFFDUMP, port
  MCCE_PORT_OFFDUMP_ALL=1), v12 10's first differing input was the region builder's very first (FUN_18002b840's M, site 0x18002bbcf):
  same loops, but ordered / directed / started differently. M comes from FUN_180090ae0 (START_FACING, l.80034-80217) / FUN_180090fa0
  (START_INSIDE, l.80220-80269) via FUN_18008f4a0 (l.79315-79480), which the port built with its own boolean / offset (split_isl, sl_*_m).
  New MCCE_DLLRG bit 8: FUN_18008f4a0 = islands (copied, +0x54 = 0) and the other shapes each through SoPockUnionAr(1e-6); START_INSIDE
  M = FUN_180153b30(out, isl); START_FACING M = copy(isl) with +0x54 + 1, the rectangle appended (FUN_18011f2e0), DetNestLvlShAr (new
  mcce_nest_only: DetNestLvlShAr without UpdDirShAr), AIR = FUN_180153b30(rect, SoPockUnionAr(MCCECalcOffset(1, P+0x60, out, 0x127) +
  isl)). With MCCE_DLLRG=15 MCCE_DLLS2=1 the first 12 MCCECalcOffset inputs of v12 10 (FUN_180090ae0, the region builder, the first
  region sets) are byte-identical to the DLL's and the material is entity for entity equal (1e-6) to commit 67. Commit 68: the DLL keeps a
  6.3e-8 bridge line (FUN_180037790's sweep bridge, > 1e-8) where the port's gap is below 1e-8: the tool path itself is not bit-exact -
  full-precision traces (MCCE_TRACE_PREC=17) differ by 1.5e-12 from commit 1, 7.7e-9 at commit 20, 4.4e-7 at commit 73 (floating-point
  order in the step computation; not chased further). Measured: MCCE_DLLWIN=1 MCCE_DLLRG=15 0.062 / 0.435 (vs DLLRG=7: NAKTO 2 0.354 ->
  0.353, rest same); + MCCE_DLLS2=1 0.062 / 0.435, v11 / v12 10 0.266 -> 0.260 (DLL 0.277), vs default 21 better / 3 worse (the same three);
  MCCE_DLLWIN=1 MCCE_DLLRG=7 MCCE_DLLS2=1: v11 / v12 10 0.260, rest = DLLRG=7. Default: 37 ops byte-identical with this build too.
- Unit oracles (this build): off_oracle 202 / 202 identical (200 synthetic tools/off_gen.py seed 1 + the two window S1 calls of v12 17);
  sobool 60 / 60 (tools/sobool_gen.py seed 1); coffct 15130 / 15130 exact (v8 42 + v12 22); ct link oracle 144 / 145 (v10 10 call 25, the
  known both-fail stale box), connector + cross 145 / 145. fileop of the 205 user-part ops vs master (rg_new7): exactly the 37 Dynamic ops
  differ (as before this step); NAKTO-BRAKE-BRACKET 3 fails as on master.
- User decision N13-24 (2026-11-13): MCCE_DLLWIN and MCCE_DLLRG=7 on by default (old: MCCE_DLLWIN=0 / MCCE_DLLRG=6). Default build
  re-measured: 37 Dynamic ops 0.062 / 0.435 (ok 2), fileop 205 vs master f57f39e: exactly the 37 Dynamic ops differ.
- **Where this stops / next**: (1) the material subtraction's entity splits (FUN_180037790 + FUN_180036250, 400 lines, item 3 of M2 sets) -
  on v12 10 the first one is a sub-1e-7 bridge decided by FP drift of the step positions, so bit-exact step arithmetic comes first;
  (2) then v12 10 commit 6538 / v10 10 commit 6608 / v8 42 commit 6622 with MCCE_TRACE_WIN (which list differs); (3) the plunge entry
  centre (MCCE_MEDAX, a user decision) and NAKTO-BRAKE-BRACKET 2's entry region; (4) S2 from the DLL boolean by default (MCCE_DLLS2=1: with
  DLLRG=7 it makes the entry sets identical; not measured on all ops yet).

**M2 bit-exact (2026-11-13, after N13-24): the measure's area sum, the start regions, NAKTO 2's entry** (TODO M2; same branch)
- Tool-position drift (buckle-v12 op 10, full-precision traces MCCE_TRACE_PREC=17 on both sides, MCCE_PLACEDBG = FUN_18001efb0's inputs):
  commit 0 bit-equal, commit 1's step 1.5e-12 off. Its cause: FUN_18001efb0 got a width limit l1 1.1e-12 off, l1 = ramp + the last
  record's width (FUN_18006cf10), and that width (FUN_180030de0: (swept area - loop areas) / length, a cancellation) used our
  cross-product area sum. The DLL sums with FUN_180126ac0 (the trapezoid rule from the last entity's end, arcs FUN_1801270e0) - already
  ported as gp_area_dir for the planner; sh_area_x now uses it (d0478cc, MCCE_AREAX=0 old). Drift at commit 1 1.5e-12 -> 7e-15, first
  > 1e-7 at commit 73 -> 724. Left: a 1.3e-14 width difference at commit 0 (the swept region / loop entities themselves, not the sum) and
  a final +-5e-8 rad refinement step at commit 20 that goes the other way on that noise (FUN_18001a... bracket end); the coarse
  divergences (v12 / v11 10 commit 6538, v10 10 commit 6608, v8 / v9 42 commit 6622) are unchanged. 37-op p95 unchanged.
- NAKTO-BRAKE-BRACKET op 2 (record 0 in every configuration): (1) the region order - with MCCE_DLLRG bit 8 the region builder's inputs are
  equal (11 MCCECalcOffset calls) and the first region is the DLL's (x -1.5..50.5 instead of 61.5..75); (2) the profile entry FUN_18007cb70:
  FUN_18007ca70 = MCCECalcOffset(1, -max(rmax - 0.75 (R - r), 0), C+0x340, 1) - new opt-in MCCE_DLLENT=1 runs it through the DLL-exact
  offset - with rmax from CalcMedAx (MCCE_MEDAX=1; our bisection gives 7.95164 vs 7.95283). With MCCE_MEDAX=1 MCCE_DLLENT=1 MCCE_DLLRG=15
  MCCE_DLLS2=1 NAKTO 2's records equal mcce.dll's to 17114 (was 0), p95 0.390 = DLL; BRACKET4 14 every record equal (0.038 = DLL).
- That configuration on the 37 ops: 0.062 / 0.435, ok 4; vs the N13-24 default 4 better (BRACKET4 / bracket4-dashed 14 0.054 -> 0.038, v11 /
  v12 10 0.266 -> 0.260), 3 worse, each equal to mcce.dll's own result on the same input: NAKTO 2 0.354 -> 0.390 (DLL 0.390), BRACKET4 /
  bracket4-dashed 16 0.045 -> 0.046 (DLL 0.046). User decision N13-25 (2026-11-13): all four on by default (MCCE_MEDAX=0 / MCCE_DLLENT=0 /
  MCCE_DLLRG=7 / MCCE_DLLS2=0 give the N13-24 behaviour).
- **Where this stops / next**: the remaining bit-level differences of the measure (the swept region FUN_180030de0's own entities, 1e-14),
  then the bracket refinement at v12 10 commit 20; the material subtraction FUN_180037790 comparison needs those first.


**M2 bit-exact measure + order mode 1 (2026-11-13, N13-16; branch worktree-agent-a4228046a72d825d2, on top of N13-25)** (TODO M2)
- New traces (both sides, same format; tools/peloader --dyn hooks at the thunks, viewer/mcce_trace.c): MCCE_TRACE_SW=1 with
  MCCE_TRACE_LEVEL >= 4 (inside MCCE_TRACE_RANGE) writes, bit for bit (%a): "SW" = M1's swept region FUN_1800321e0 + its area M+0xc8
  + width x length; "LP" = the measure's loops FUN_18003a140 (W+0x30) + its result; "PS" = the step FUN_18006fc10 takes from the walked
  path; "LI" = every lead-in FUN_180025a30 (step, X, the lead); "LK name" = entries of the link functions (general / local link,
  stay-down, connectors 2-5 + FUN_180045470, routed connector, link smoothing, linearised arcs, junction smoothing; on the DLL side
  register-neutral event stubs at the thunks, printed before the next trace line); level 5 adds "TAN" (FUN_18008e710 / FUN_180028ac0,
  the trial line's base direction); level 2 adds each material loop's tag (+0x58); every "C" line gets " nrec N" (CCetp +0x1c, the
  record count FUN_180032a20 stamps as tag). MCCE_NOMEMO=1 (port) turns the trial-step memo off so every trial is traced. The DLL
  side needs MCCE_REG=bUseCtourGuide=1 (the port's default P+0x261 = 1; without it the DLL takes the planner link path).
- The measure is now bit-exact (MCCE_MEASX, default on; =0 the old forms). What differed, in the order the traces found it:
  1. FUN_180032200 (line swept region): the cap angles are atan2 of the SCALED normal n = (dy, -dx) rad / L (atan2(-n.y, -n.x),
     atan2(n.y, n.x)), not of the unscaled direction - last-bit differences in sa. FUN_1800321e0 (arc): after CalcEntIntersect an
     arc parameter t > 1 is taken back one turn (l.31416-31427 / 31526-31537); the degenerate cut (rho <= rad) uses
     CalcEntIntersect(1e-6) itself (gp_ent_isect, was e_isect).
  2. The crossings of the swept region with the material (FUN_180032b30 l.32009 / FUN_1800331a0 l.32173) are CalcEntIntersect(1e-5,
     material entity, swept entity) = gp_ent_isect (was our e_isect_r): the 1.3e-14 width difference at commit 0.
  3. FUN_1801270e0 (arc term of the area sum FUN_180126ac0): the segment term is ((b - lo) - sin(b - lo)) * (r * r) - disasm
     0x180127289 mulsd xmm6,xmm6 then 0x18012729e (the decompile prints x * r * r); gp_arc_area had (x * r) * r: a 1-ulp area
     difference on the swept region (r = 1.5) that flipped Brent's last +-tol step (the "+-5e-8 rad refinement at commit 20").
  4. The trial line's base direction FUN_18008e710 (and FUN_1801204a0 / FUN_18011ffd0 = tan_out / tan_in): 'A' (end - centre) |sw|
     turned by +-90 deg (gp_tan_vec), not e_tan's (-sin, cos) r: same direction, other bits after atan2.
  5. Candidate A's length FUN_180019ec0 l.19454 / 19462 is sqrt(dy dy + dx dx) (was hypot).
  6. GetNearestArcPt (l.6061-6127) and FUN_1800acd50 (farthest): v = the swept-relative angle normalised the DLL's way (to [0, 2 pi],
     then -2 pi for sw < 0), the point from cos(v + sa) (was from e_at(v / sw)); FUN_18013d280 / FUN_18013caf0 compare squared
     distances (was the root squared).
  7. FUN_180030de0 ORs its flags into the block (only FUN_18001cfe0 clears; 0x100 cleared by FUN_1800380f0 l.34529 on the successful
     path only) and FUN_180030be0 leaves M2 as it was when it does not measure it (the "mf 0x20" record differences: FUN_1800167d0's
     0x20 survives the measure).
  8. Material subtraction: FUN_180036250 (collinear merge, l.33600-33997) operation for operation - CalcLnLnIntersectPt (gp_lnln) of
     the normal through each end / arc middle, the 1e-6 end tests, the first-pass pass-byte condition, the merged line's pass byte 0
     without the first-pass feed (col_merge_x). No trace difference on the 37 ops before or after (the splits seen earlier were
     the step drift above).
  buckle-v12 10 with these: every trial measure, swept region, loop, step, lead-in and commit (geometry, widths, sweeps, flags, material
  loop / entity counts and areas, record counts) bit-equal to mcce.dll through commit 6537 (before: 1.5e-12 at commit 1, 7.7e-9 at
  commit 20, first differing commit 6538 at the 1e-4 level).
- The coarse divergences (v11 / v12 10 commit 6538, v10 10 6608, v8 / v9 42 6622): order mode 1 (P+0x20 = 1, FUN_180055190) - the
  general link's target filter. Its points P1 (on the S1 piece of the target) and P2 (on the route to it) are the FARTHEST points from
  the last point (FUN_18013caf0 at l.49561 / 49573); ours took the nearest, so v12 10 commit 6537 kept the target (57.78, 10.51)
  where the DLL keeps (68.75, 4.75) (same five candidates, "GT" trace). Also: the shortest route of the chosen shape replaces the pick
  only when strictly shorter (l.49652-49654), a target without an S1 shape keeps shape -1. MCCE_ORD1: opt-in on this branch, then ON by
  default by user decision N13-26 (2026-11-13; MCCE_ORD1=0 = the old nearest-point rule).
- After merging master and turning MCCE_ORD1 on (N13-26), re-measured: 37 Dynamic ops 0.062 / 0.435 (ok 4), every op's p95 = mcce.dll's
  own (v10 10 0.144, v11 / v12 10 0.277); fileop 205 vs master (rg_new10): exactly the 37 Dynamic ops differ; op_compare 47 / 635, the
  same scenes as master (no further re-bless).
- mcce.dll on our input (MCCE_REG=bUseCtourGuide=1 tools/tests/mcce_dll_oracle.sh -n, all 37 ops, 56 engine calls), MCCE_ORD1=1: every
  record and every commit equal on all 56 calls (mcce_trace_cmp.py tolerances; was 27 / 37 ops); port p95 = DLL p95 on all 37, median
  / worst 0.062 / 0.435 both. Default (MCCE_ORD1 off): v10 10 first differing commit 6608, v11 / v12 10 6538, v8 / v9 42 6622 (all
  bit-equal before that), the other 32 ops equal.
- 37 Dynamic ops vs the X9 NCIs (fileop runs, tools/tests/orfiles.py): default (MCCE_MEASX on) 0.062 / 0.435 (ok 4), every op's p95
  unchanged vs master (rg_new9); MCCE_ORD1=1: 0.062 / 0.435, v10 10 0.171 -> 0.144, v11 / v12 10 0.260 -> 0.277 (both = mcce.dll's own
  result on the same input), 34 same. fileop of the 205 user-part ops vs master (rg_new9): exactly the 37 Dynamic ops differ.
  op_compare 53 / 635 (master rg_new9: 47): the 6 extra are Dynamic / Face-dynamic scenes whose posted NC coordinates moved in the
  last digits (dlg_dynamic, regen_dup_toolnum, rt_dyn_b9_entry, rt_dyn_medax_entry2 / entry4p, rt_face_b7_dynamic), re-blessed.
  Unit oracles (default build): ct link (v12 3 / 22, v6 32 / 36, v9 51, v10 10, v8 42) 97 calls, connector + cross 97 / 97, link 94 /
  97 (the 3 left: both fail, stale box only); coffct 2592 / 2592 exact (v8 42 + v12 22); off_oracle 200 / 200 (off_gen seed 1);
  sobool 60 / 60 (sobool_gen seed 1).
- Left at the bit level: v12 10 commit 6538 (MCCE_ORD1=1) starts 1.8e-15 off (the boundary target's re-entry point after the link,
  targets_boundary); not chased. mcce_ctour.c's sh_orient_t area is not FUN_1801270e0's piecewise sum (direction / thresholds only;
  the off / sobool / coffct oracles pass).
