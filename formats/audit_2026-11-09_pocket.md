# Provenance audit of the Pocket round (2026-11-09, af98a20..e3fe08b)

Three independent read-only reviews checked the new Pocket code against the decompile and disassembly, opening the cited lines rather than trusting the comments. Nothing was run. Counts are approximate, one per checked decision point.

| Type | Decoded | Citation wrong | Unlabelled improvisation | Labelled inference | Unsettled |
|---|---|---|---|---|---|
| 2 Island facing | ~63 | 3 (+2 stale comments) | 5 | 7 + 5 refusals | 4 |
| 3 Remachining / Contour clearance | ~58 | 3 | 7 | 7 | 4 |
| 4 Open pocket | 56 | 3 | 5 | 11 | 5 |

Status: none of the items below is fixed yet.

## Changes the toolpath, silently

| Id | Where | Port | Decompile | Effect |
|---|---|---|---|---|
| R-U1 | cam2d_pock_rem.c:199-202, 223-225, 329-334 | fixed heights, top_z + feed_plane | set_heights per section (l.315582, l.315998, l.316019, l.316546; mccore 0x18089fa80) | incremental planes: retract and approach below the first depth at the wrong Z |
| R-U2 | cam2d_pock_rem.c:228 | Plunge after first move / Retract before last move (0x992 / 0x993) ignored, not refused | l.315506-315511, l.315914 | with leads: first move cut at depth instead of at retract / clearance height |
| R-U3 | cam_fileop.c:545, 574; cui_opdlg.c:223 | mc_ret_on / mc_isl_depths do not follow dialog edits (`mc` excludes CAM_POCKET) | - | stale retract flag; the 0x892 refusal bypassed. Not traced: whether a later step rebuilds them |
| R-U4 | cam2d_pock_rem.c:141-150 | no post-processing of pass offsets | move_split_off, merge_pock_bounds l.338678, delete_intersecting l.338684, sort_pock_bounds l.338695 | a pass crossing those bounds kept; piece order differs |
| R-U5 | cam2d_pock_rem.c:78-83 | delete_nested tests one point, split-offs included | is_ctour_nested mccore_pock.c l.2730; split-offs moved aside l.338300 | differs for rings touching an island |
| O-C1 | cam2d_pock_open.c:395 | tapered-wall refusal tests dc_taper, set only for Contour: dead code | FUN_180056a70 l.59008-59017: record 0x1288 | tapered walls silently cut straight; also on the close path |
| O-C2 | cam2d_pock_open.c:396, 414-415, 447 | finish tested as n > 0 && spacing > 0 | l.335064-335071, l.335117-335122: finish flag 0x1239 only | wrong capsule offsets when finish is on with n = 0 or spacing 0 |
| I-C1 | pgeo.c:429-433 | top bound intersected with every outer loop | FUN_1805dec50 l.175: only outers sharing a box, not the parent | check bound in two disjoint loops: island silently not faced. The 40 merge cases all have one outer loop |
| I-U2 | cam2d_pock.c:920 | sub = 0, k replaced by sk | marker l.339443 / l.340336 writes sVar16 + param_7; FUN_1801c6520 compares sub before depth | several sub-pockets: cut per depth instead of per sub-pocket |
| I-U3 | cui_opdlg.c | panel 3002 overlap distance not recomputed | FUN_18017da90 l.265876-265881, l.266032-266036 | after a tool change the top bound is wrong |
| I-C3 | cam2d_pock_isl.c:273 | clearance adjusted without the on test | set_heights tests record 0x179 first; ht3 cam2d.c:3173 gates it | clearance off with its incremental flag set: shifts per depth |
| R-C1 | cam2d_pock_rem.c:160 | point pieces dropped; cited lines are only the entity loop | FUN_1801bfcb0 runs first, every piece gets a marker | a retract and plunge at that point lost |

## Refuses or stops where Mastercam does something else

| Id | Where | Port | Decompile |
|---|---|---|---|
| I-C2 | cam2d_pock_isl.c:34-35 | flatness with CAM_SYS_TOL | check_chains_3d_view mccore_chain_range.c l.6215-6220: chain_plane_tol (config 1002; cam_chain_plane_tol exists) |
| I-U1 | cam2d_pock_isl.c:30, pk_region cam2d_pock.c:68 | open chains dropped, closed ones cut (Standard too) | l.313523-313527, l.313789: warning 0x93f, mcmillres 2367, stops |
| O-U1 | cam2d_pock_open.c:405-410 | "tool does not fit" / "NOT PORTED" | FUN_1801e6970 l.334833-334835, machine_pocket l.322541: empty op, no error |
| O-U2 | cam2d_pock_open.c:444 | "nothing to cut" | local_188 stays -1, l.335131 not run, no error |
| O-U5 | cam2d.c:3711 | only type 4 reaches gen_pocket_v2 | l.313699-313710, l.334927: any type with an open chain |
| R-U6 | cam2d_pock_rem.c:244-245 | refuses cell <= 0, roughing diameter <= 0 | no source; MapToOp stores 25 / 1 |
| R-U7 | cui_ops.c:644 | finish_n > 0 for op +0x11d1 | the byte itself (cam_prev_op_entry uses it) |

## Minor or practically unreachable

O-U3 `step > 1e-9` guard needs an "ours" label (cam2d_pock_open.c:398). O-U4 over 999 waypoints returns ok instead of -1 (l.332561, 332585, 332615). I-U4 depth Z from chain 0, not each pocket's first outer (cam_fileop.c:767, 926, pre-existing; l.313602-313612). I-U5 no sliver filter on the no-crossing path (FUN_1805dede0). R-C2 island flag inferred from area sign. Wrong line numbers only: O-C3 panel 3001 (l.267829, l.267852, l.267877), R-C3 FUN_1801c4860 (l.313160). Stale comments: cui_opdlg.c:1072, cam2d_pock.c:691 still say Island facing is refused.

## Labelled inferences with code that could replace them

- Remachining: pk_rem_cross -> pko_bounds_x; gls_offset fallback -> FUN_1805e02e0 (mccore_pock.c l.3933); pass offsets -> offset_pock_bounds (l.666); first link waypoint -> find_path_around_island (cam2d_pock_zz.c); Contour MapToOp 0x1278 and 25 / 1 fallback (l.243362-243363).
- Island facing: gls_face_merge -> pspiral_decomp_merge.c; pk_isl_gt_int -> ints_on_gt_new (ints_gt needs SysTypes.dll disassembly); retract between sections -> set_heights(prev, prev) l.316160; section top for spirals -> adjust_depths (IAT 0x180302e90 in FUN_1801ede50); finish_with_rough l.313559.
- Open pocket: retract between depths (wr_pock_toolpath, FUN_1801bf680 l.309886, FUN_1801e9a90 l.336692); box test -> build_pock_bound_info; several chains partly (l.334912-334919, warning 0x965); finish with 0x13f0 -> FUN_1801cc170; no roughing l.322563; keep tool down l.335153-335194; split passes -> FUN_1801e7930 l.335488; closed chains -> MCCECalcClosedOpen.

Agreed as ours and correctly labelled: the Open pocket pass limit (chain diagonal + R), cam2d_pock_open.c:422-426. FUN_1801e7040 l.335076 has no limit; it only refuses and replaces a hang.

## Unsettled (reviewers could not decide)

Island: o.l[0] against the head of ccomp_pock's list (cam2d_pock_isl.c:220); process_pocket at radius 0 through FUN_1801d4620; the arc flatness second point local_29a8; Ordinal_4282 on control 0x504.
Remachining: whether merge_pock_bounds changes only crossing rings; rm_ccomp_pock marking split pieces (RmB.broken); the 1e-12 Z gate (cam2d_pock_rem.c:220); pk_fin_inner against offset_pock_bounds.
Open: l.332621-332641 waypoints with different pocket numbers; pko_chain_gl snap against FUN_1805d4060; pko_chord_ccw against pock_ccw; ch->closed against FUN_1801e5840; second ccomp direction byte (cam2d_pock_open.c:427).

## Not checked

Island: ccomp_pock internals, MCCECalcOffset, FUN_1805dfc50, FUN_1805df740, the reused Standard roughing engines, the FUN_1801e0f80 link body, hst_depths. Remachining: rm_ccomp_pock internals, the body of rm_fill_bounds (DLL oracle only), pk_lead_ent / pk_lead_check, rm_group, the 3146 layout. Open: pko_trim and the cam2d_pock_open_k.c helpers (DLL oracle only), wr_pock_toolpath's retract for this path.

## Found outside the audited code

- A Standard pocket hangs (over 20 s) with methods 8 and 2 on a 100 x 50 rectangle, D 10, circle island at (70, 25) r 8; at y = 23 it does not.
- Standard Pocket's pk_retract goes to the clearance whenever clearance is on; retract_move as decoded goes to the retract height first when retract is on. Not reviewed against stored NCI.
- tools/gen_opdlg_fields.py does not reproduce the committed cui_opfields.inc; new panel rows were added by hand.

## Independent review of the fixes (35ec3ea..d787511, 2026-11-09; from reading the code against the decompile, nothing run)

Confirmed resolved: I-C1, I-C2, I-C3, I-U2, I-U3, I-U5; R-U3, R-U4, R-U6, R-U7, R-C1, R-C3, Contour 2999; O-C1, O-C2, O-C3, O-U1 to O-U5. The merge port rm_merge_pock matches pspiral_decomp_merge.c.

New bugs and items not resolved:
- V-1 Reference height of later depth cuts: pk_h(p, depth, p->top_z) (cam2d_pock.c:513, cam2d_pock_rem.c:265, cam2d_pock_open.c) against wr_pock_depth_cuts l.315181 and set_heights l.315582: the marker's top is the previous cut's Z. Found by two reviewers. No stored op has depth cuts with incremental planes.
- V-2 "New pocket" from the tool-centre index (cam2d_pock.c:505, 1083) against the marker's chain pocket (l.313710, FUN_1801d36c0 l.339443). Found by two reviewers.
- V-3 Facing with chains at different Z cuts every chain at every chain's depths (cam2d_pock.c:938): tc_all from the whole region. Can gouge.
- V-4 Per-chain top_z shifted while the heights are not (cam2d_pock.c:919).
- V-5 Non-planar chains not refused for types 0 / 1 (cam2d_pock.c:902-903; check_chains_3d l.313530-313541, warning 0x2309).
- V-6 Plunge after first move / Retract before last move incomplete (R-U2): plunge_move after the record l.316463-316467; clear / retract before the record l.316403-316428, local_920 l.316422; rough pieces lifted without leads l.338740-338753, l.338862-338870; entity 0 in the first section l.315847-315850.
- V-7 delete_nested's "by" list has no real boxes (cam2d_pock_rem.c:65; pock_bounds_bbox(param_2)).
- V-8 Remachining depths from chain 0 (cam2d_pock_rem.c:331; set_depths per chain pocket l.313612).
- V-9 Open: a split tapered offset silently drops a depth (cam2d_pock_open.c:515; FUN_1801e6970 l.334829-334834).
- V-10 Open: a closed chain among open ones ignores 0x13d8 (cam2d_pock_open.c:591-593; l.313699-313700, l.334976).
- V-11 Open: end-distance check reads only the list head (cam2d_pock_open.c:423, 438; FUN_1801e6600 l.334661).
- V-12 Open: diagonal rapids at the section start (cam2d_pock_open.c:478-480): from the hand-merged heights change.
- V-13 Open cutting method refused for a cutting method it never reads (cam2d_pock.c:851, 859, 867; FUN_1801e7040 l.322571-322573).
- V-14 Minor: g_pk_ret.on stale after a section error; pk_isl_retract clamps through ht3 (set_heights does not); leak at cam2d_pock.c:1003; taper angle enable (cui_opdlg.c:2415; enable_taper_controls l.247981-248026); single open chain message (cam2d.c:3715-3716); pk_e0f80's comment against pk_retract, mid-section links not on the retract fix; Island retract arm without the distance test (cam2d_pock.c:1087); point chains exempted from 0x93f without a citation; stale header cam2d_pock_open.c:14-16.

Not checked by the reviews: rm_ccomp_pock internals, rm_group, rm_sort, pk_lead_check, the FUN_1801e0f80 link, the _k.c helpers, hst_depths.
