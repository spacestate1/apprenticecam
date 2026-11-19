# OCCT -> Parasolid XT writer, Extrude history replay, end-to-end proof (2026-10-23)

Status 2026-11-18: **in the main build** (SOL-1..SOL-3: the writer `viewer/occt/occt_xt.cpp` writes the XT of every solid made here, the
Extrude kernel is `viewer/occt_solid.cpp`; section 10). Status 2026-10-23: standalone code in `viewer/occt/`. Companion docs:
formats/occt_accuracy.md (XT -> OCCT), formats/solid_history.md (SOLID_ID / SOLIDXMT / ops records, task B),
formats/xt.md, formats/occt_3d_plan.md. Rule 1 applies: "ours" = our choice, NOT DECODED = unknown.

Sources:
- Parasolid's schema `~/mastercam-re/files/sch_26105.s_t` (every node is written from its schema entry at run time).
- **Parasolid XT Format Reference** (UGS, Oct 2006), a free copy: `~/mastercam-re/work/occt/ref/Parasolid-XT-format-reference-2006.pdf`
  (+ `.txt`; sha256 27dadddb...; from 13thmonkey.org via the Wikipedia citation). Cited as "ref p.N" (printed page).
- Mastercam-written XT data: the 108 solids of the user's parts (`~/mastercam-re/work/occt/xb/`) and 820 solids of
  the internet sample parts (`<mastercam-samples>/net`, read-only; blobs + manifests in
  `~/mastercam-re/work/occt/netx/`; 646 binary, 378 parse with our schemas). Survey scripts are in
  `~/mastercam-re/work/occt/xtw/` (survey*.py) and `netx/` (conv.py, cone.py, orient3.py, aux.py, per.py,
  attdefs.py, ept.py, opcensus.py).

## 1. Files (viewer/occt/)

| file | what |
|---|---|
| `occt_xt.h/.cpp` | the writer: `occxt::write(shape, schema, Options, bytes, Report)` |
| `xt_refgeom.h` | XT curves / surfaces evaluated from the reference formulas (+ data corrections), used for face senses and as the oracle |
| `occt_xt_check.cpp` | oracle: `rt` (round trip of stored solids) and `synth` (OCCT-made solids checked with the reference formulas) |
| `xt_canon.py` | structural comparison of two XT bodies after canonical ordering |
| `sld_ops.py` | history records: serialise ops, `spec` for the replay, `splice` / `addsolid` into a copy of a part |
| `sld_regen.cpp` | replays Extrude / Extrude Cut / Boss history in OCCT (chains from viewer/mcam_load.o, read-only) |
| `xt_occt.h/.cpp` | (previous agent) + two accessors added: `Model::ival`, `Result::vertex_of` |
| `Makefile` | `make xtcheck` (writer checks), `$(OUT)/sld_regen`; binaries in `~/mastercam-re/work/occt/` |

Proof scratch: `~/mastercam-re/work/occt/proof/` (`load_harness.c` = the app's C loader, `regen/`, `out/`).

## 2. The writer

Output = the exact SOLIDXMT payload Mastercam embeds: text header (22 lines, byte-identical to the 108 user blobs
except KEY / FILE / DATE: `KEY=sld_N; FILE=SLD_N.X_B`, line 2 is `**PARASOLID !"#%&'()*+...` - without the `$` that
ref p.13 shows), then `B`, u32 len + ": TRANSMIT FILE created by modeller version 2700142", u32 len +
"SCH_2700142_26105", u32 1 (USFLD_SIZE), nodes, terminator `u16 1` + index 0 (stored `u16 1`; ref p.4 - this is
the 2 bytes our readers left unconsumed). Root BODY = index 1. Pointers = index + 1 as u16, big-index pair for
index >= 32767 (ref p.17). User field 0 on every node (all 108 + 378 blobs).

Entities written (fields from the schema; constants from the user-solid survey `xtw/survey.txt`):

| node | written as |
|---|---|
| BODY | res_size 1000.0000000000001, res_linear 1e-8, state 1, body_type 1 (solid), nom_geom_state 1, shell = first solid-region shell, region = the void region, chains of edges / vertices / points / curves / surfaces, highest_node_id = max, lowest 0, index_map_offset 0 |
| REGION / SHELL | one void region (`V`, first) + one solid region (`S`) per OCCT solid; a cavity shell -> its own void region (ref p.19-20; not in any data, unverified); solid-region shells body = BODY, void shells body = null; back faces chained by FACE.next, front faces by next_front |
| FACE | sense from the natural normal (below); surface sense always `+`; tolerance null-double; one surface per face |
| LOOP / HALFEDGE | fins in BRepTools_WireExplorer order; OCCT seams + degenerated edges dropped; a wire that falls apart after that is split into loops nose-to-tail; fin.vertex = forward vertex, EDGE.halfedge = the `+` fin, VERTEX.halfedge + next_at_vx = fins ending there (ref p.21, 92-93; data survey3.py: 3365/3365 edge fins `+`, 6730/6730 nose-to-tail) |
| EDGE / VERTEX / POINT | tolerance null-double (accurate); a closed edge whose vertex has no other edge -> ring edge (no vertex, as Parasolid; 284 rings in the user solids); one curve per edge |
| curves | LINE, CIRCLE, ELLIPSE native; B_CURVE + NURBS_CURVE + BSPLINE_VERTICES + KNOT_MULT + KNOT_SET + CURVE_DATA (self_int 1, as 163 Parasolid ones); Bezier / parabola / hyperbola -> exact B-spline; periodic OCCT B-splines in Parasolid's periodic form (n_knots = n_vertices + degree + 1, wrap-round vertices; checked by evaluation); knot_type / curve_form 1 (unset) |
| surfaces | PLANE (normal = X x Y), CYLINDER, CONE, SPHERE, TORUS native; B_SURFACE + NURBS_SURF (u-major vertices, periodic form as curves, SURFACE_DATA not written - optional, ref p.68); Bezier / revolution / extrusion surfaces -> exact rational B-surface of the patch the face uses |
| attributes | ATTRIB_DEF "CNC/mcEptrPtr" (type 9000, actions [0,0,0,0,3,6,0,0], legal owners face/edge/surface/curve, fields [9]) + one ATTRIBUTE with an empty pointer field on every face and edge (all 108 + 374 sample solids); LIST (node_id 0, type 4, notransmit 1, length = #defs, block 20) + POINTER_LIS_BLOCK (20 entries, heads of the per-definition chains). Optional `Options::face_color` -> "CNC/mcColor" (type 9000, int field, owners face/edge; INT_VALUES [colour]; first in the face's attribute chain, as in sample 0104) |

Face sense: XT natural normal from the reference formula (`xt_refgeom.h`, central differences) compared with OCCT's
outward normal at a regular point of the face; `+` iff they agree (ref p.78: face orientation = face sense x
surface sense). A point that the XT form does not reproduce (e.g. a cone face on the other nappe) is refused.

Refused (named in `Report::unmapped`): torus with major <= minor (Parasolid's apple / lemon split NOT DECODED),
offset curves / surfaces, edges without a 3D curve, sheet / wire bodies, non-manifold edges. OCCT tolerances are
not written: every edge / vertex is written "accurate" (null tolerance = half of 1e-8 m); `Report::max_occ_tolerance`
reports what OCCT had (1e-5 mm max on the user round trip; up to 1.1e-4 mm on replayed chains with gaps, sec. 5).

Ours: node numbering, chain orders, node_ids (Options::node_id keeps given face / edge / vertex ids), one curve per
edge and one surface per face (Parasolid shares a few: 3 curves in the user parts, 144 curves / 71 planes in the
samples), surface / curve senses always `+`.

## 3. Conflicts with the reference / formats/xt.md, decoded from Mastercam's own XT data

| item | reference | Mastercam data | used |
|---|---|---|---|
| CONE formula | ref p.55: `R = P - vA + (X cos u + Y sin u)(r + v tan a)`, "axis points away from the half in use" | 164 cone faces fit only `P + vA` (radius grows along A), 0 the printed sign; 210/210 two-ring band faces obey the loop rule with `+vA` (netx/cone.py, orient3.py; cylinder control 788/788) | `+vA` |
| B_SURFACE vertex order | not stated | 221 faces fit only u-major (index i*n_v + j), 0 only v-major, 458 either (conv.py) | u-major |
| binary modeller-version length | ref p.16: 2-byte | 4-byte in every blob | 4-byte (formats/xt.md) |
| user fields | ref p.3: only on PK-visible nodes | on every node incl. HALFEDGE, LIST, ATT_DEF_ID | every node |
| ELLIPSE.sense | missing in ref p.32 struct | in the schema | schema |
| periodic B-curves | ref p.34 recommends repeated wrap vertices | Parasolid's own periodic curves / surfaces are clamped or unclamped standard forms with periodic=1 (per.py) | both legal; ours = wrap form |
| ATTRIB_DEF.next | "ignore except in partitions" (p.99) | an unresolved index in all 108 user blobs | null |
| B_CURVE.data / B_SURFACE.data | optional (pointer0) | always present | CURVE_DATA written, SURFACE_DATA not |
| BODY.res_size | "normally 1000" | 1000.0000000000001 (user, 209) or 1000.0 (109 samples) | 1000.0000000000001 |

Everything else checked agrees: fin vertex = forward vertex, EDGE fin positive, body.region infinite void, shell
body fields, loop left rule, terminator, pointer + 1 encoding (formats/xt.md "p u16 index (1 = null)" is the same
thing; our three readers do not handle the big-index pair, i.e. bodies over 32766 nodes - none exist here).

Also seen in the samples (for later work): body_type 3 sheet bodies (61), other attribute definitions (CNC/mcEptr
int - stale 32-bit pointers, CNC/mcColor, SDL/TYSA_COLOUR and *_2001 import attributes), SP_CURVE / TRIMMED_CURVE
(tolerant edges), INTERSECTION, BLENDED_EDGE, SWEPT_SURF, SPUN_SURF, OFFSET_SURF, GEOMETRIC_OWNER; 171 solids in
schema 28101 (Mastercam 2018+, no sch_28101.s_t here), 174 blobs that are not binary XT, ~40 parse failures with
our schemas.

## 4. Writer verification

**4.1 OCCT-made solids (`occt_xt_check synth`, `xtw/synth.tsv`):** box, cylinder, cone (both half-angle signs),
sphere, torus, box with all 12 edges filleted (cylinders + spheres), chamfer (+ an mcColor face), box minus
cylinder, fillet round a hole (torus), ellipse prism (B-surface from an extrusion), cylinder and box scaled 2/1/1.5
and 1/2/3 by BRepBuilderAPI_GTransform (B-surfaces, B-curves, periodic B-surface), revolved B-spline profile.
**14/14 OK**: every OCCT mesh node lies on its XT surface (max 3.9e-17 m), every edge on its XT curve (1.1e-17 m),
XT normals agree with OCCT's outward normals, XT curve direction = edge direction, positive fin / vertices
correct, all loops nose to tail with opposite-sense fins, planar loop areas = OCCT face areas (rel <= 1e-4 =
the 256-segment chord sampling), parse by xtocc::parse and viewer/xt_mesh.c (xt_mesh meshes only plane / cylinder).
Mutation: writing B-surfaces v-major (`OCCXT_BSURF_ORDER=v`, test hook) fails 4/4 B-surface cases (1.7e-2 m).
Not verified against data: SPHERE / TORUS face orientation (point sets of tori verified: 232 vertices within
1.3e-16 m) - the natural normal comes from the reference formula only.

**4.2 Round trip of the user's 108 solids (`occt_xt_check rt`, XT -> OCCT -> XT, face / edge / vertex node_ids
kept):** 108/108 written, re-read by xtocc::parse + build: valid, volume and area equal to 1e-9 (all dV = 0 to
1e-12), viewer/xt_mesh.c parses and meshes every face; 144 OCCT seams dropped, 284 ring edges restored. Run at
scale 1 (metres in OCCT) and 1000 (mm).
`xt_canon.py` (canonical form: nodes keyed by meaning, chains as sets / cyclic sequences, all fields compared):
**106/108 canonically equal, the other 2 differ only in BODY.highest_node_id** (the original is not the maximum
node_id there). Doubles: 50,639 compared, 48,376 bit-identical at scale 1 (45,845 at 1000), max |diff| 3.3e-16 m
(gp_Dir renormalisation / mm round trip). Differences Parasolid does not care about (ref p.3, 23, 78, 87):
node indices and chain order, node_ids of non-topology nodes, 6 LINE + 54 CIRCLE `-` senses written as flipped
`+` curves, 67 CYLINDER `-` senses folded into FACE.sense, 3 shared curves written unshared (+3 LINE nodes),
ATTRIB_DEF.next null instead of unresolved. Byte identity is not achievable: Parasolid's node order and node_ids
are its internal creation order (the regenerated cube of sec. 6 has the same size, 5720 bytes, other order).

**4.3 Round trip of the internet sample solids** (only plane / cylinder / line / circle bodies; xt_occt refuses
the rest): schema 26105: 92 of 226, schema 28002: 95 of 235 (others: unmapped entity types 172, parse
failures). Canonical comparison 104/187 equal; every difference is an attribute the writer does not carry
(CNC/mcEptr ints, SDL/TYSA_COLOUR + FACE_ID_2001 + BODY_* import attributes, their INT/REAL_VALUES and list
lengths) or a surface shared by several faces; geometry max |diff| 1.1e-13 m.

## 5. Extrude history replay in OCCT (`sld_regen`)

Replays each solid's Extrude / Extrude Cut / Extrude Boss ops from the stored history (sld_ops.py spec), with the
chain geometry from the app's loader, per formats/extrude.md 4.1-4.7 (direction = the group's first chain's
direction, Reverse, Distance / Through all from the target box + 0.01 mm, even-odd chain grouping, Create / Cut /
Boss). Not replayed: Both directions, Draft, Thin wall, Trim to faces (none in the user parts). Ours: chain gaps
(Mastercam chains within its tolerance: up to 2.2e-4 mm here) closed by one shared vertex per joint at the gap
midpoint with tolerance = half the gap (how Mastercam closes them, chain_to_pock_bound, is NOT DECODED); no face
merging; exact booleans (no fuzzy value).

All 108 user solids (295 extrude ops, 8 toolpath ops skipped): **83 identical** to the stored body (volume equal
to <= 1e-13 relative, symmetric difference 0, valid; 81 also with the same face count - BRACKET4 and its copy have
38 faces against Parasolid's 34: OCCT leaves faces split that Parasolid's booleans merged, [inferred], merging rule
NOT DECODED); **25 differ by 1.3e-9 .. 6e-5 relative volume** (max 0.024 mm3), all in the buckle-v* parts, same
face counts, symmetric difference 0 at OCCT's tolerance - [inferred] the chains with gaps (the gap closing differs
from Mastercam's). Results: `proof/regen_all.tsv`.

## 6. End-to-end proof (task C) - `~/mastercam-re/work/occt/proof/out/`

Part: <user-parts>/test-cube-10mm.mcx-9, solid sld_378 (Extrude 10 x 10 square 6 mm down + Extrude Cut
circle r 1.5 Through all). Replay in OCCT = the stored body (volume 557.588499 both, 7 faces, symmetric difference
0).

| file | content |
|---|---|
| `cube_A_regenxt.mcx-9` | sld_378's XT replaced by the OCCT-regenerated XT (5720 bytes, same size); history, SOLID_ID, everything else byte-identical except the SOLID_ID box (recomputed, last-bit differences) and the XT bytes |
| `cube_B_newsolid.mcx-9` | + a NEW solid sld_379 (SOLID_ID cloned: new eptr, idn 331, N 379, box; SOLIDXMT + our XT; history = 1 Extrude op written by sld_ops.py: the square chain, distance 3, no McEptr records), dirty byte 0 |
| `cube_C_newsolid_dirty.mcx-9` | as B with the ops "needs regen" byte = 1 (Mastercam should replay the Extrude itself on open) |
| `cube_D_newsolid_records.mcx-9` | as B with the source op's 18 McEptr name records copied (ids made unique) |

Re-opened with our readers: tools/mcam_walk.py walks every file to its end; the app's C loader
(viewer/mcam_load.o via `proof/load_harness`) loads 7 entities, 4 operations and 1 / 2 solids with the SOLID_ID
idn <-> N links (267 <-> 378, 331 <-> 379); tools/sld_hist.py parses every op to its exact size; replaying each
file's history with sld_regen gives bodies identical to the XT stored in it (378: 557.588499, 379: 300.0). The
serialiser itself: **all 295 extrude ops of the user parts and 2,081 extrude ops of the sample parts re-serialise
byte-identical** (`sld_ops.py roundtrip`; 0 differ).

**Not tested: opening in real Mastercam** (no Windows / Wine here). What to check on Windows, Mastercam 2017,
opening the four files in this order:
1. `cube_A`: opens without a message; the solid looks as before; Solids manager shows Extrude + Extrude Cut;
   Solids > Regen (right-click solid > Regen) works; Analyze > Dynamic / a 2D contour on a solid edge picks.
2. `cube_B`: two solids in the Solids manager ("Extrude" under the new one); the new one is the 10 x 10 x 3 block
   (z -3..0) overlapping the cube; Edit the new Extrude (distance 3 -> 4) regenerates; Regen All has no error;
   File > Save then reopen keeps it.
3. `cube_C`: same as B, but Mastercam rebuilt solid 379 on open (any message "Error reapplying operation" sld 207 /
   255 means our op record is wrong).
4. `cube_D`: same as B; check that selecting a face of the new solid / Regen does not crash (tests the copied
   name records).
5. Any message on open (sld 259 / 225 size mismatch, 676 / 678 version, PK errors) -> send the exact text.

## 7. Integration plan (main-tree changes, in order; another agent owns these files now)

1. **Build**: add `viewer/occt/occt_xt.cpp`, `xt_occt.cpp` to the C++ (Qt) build, link TKBO, TKBool, TKPrim,
   TKFillet, TKShHealing, TKTopAlgo, TKGeomAlgo, TKBRep, TKGeomBase, TKG3d, TKG2d, TKMath, TKernel; a C wrapper
   `occt_bridge.h` (extern "C": build body from extrude params, write XT, mesh) so viewer/*.c stays C.
2. **Extrude on OCCT** (user decision done): port `sld_regen.cpp`'s replay as the kernel behind the existing
   Extrude command (viewer/solid25.c stays for the 2.5D display until the mesher moves to OCCT's BRepMesh);
   the regen of an edited op = replay of the whole history (formats/solid_history.md 4).
3. **mcam_write.c** (section 13, formats/mcam_write.md row 13): for a solid made / edited here write SOLIDXMT +
   our XT + trailer + ops block from the in-memory history (`sld_ops.py` w_* functions -> C: McOperation base,
   McChainOp, SldExtrudeOp, McOpsFile header; formats/solid_history.md 5.1); unchanged loaded solids keep their
   stored bytes as today. Section 12: the SOLID_ID record (5.1 step 2) with a new eptr / idn.
4. **mcam_load.c**: keep the ops block per solid (today it is skipped) so history survives load / save and can
   be replayed.
5. **Solids manager (core_ui)**: the op list from the ops block (names / types, formats/mcamui_dialogs.md
   "Solids manager"); Regen; Remove History (= one Body op holding the XT, 5.3).
6. Then Boolean / Fillet / primitives: their op readers are dumped but not decoded (solid_history.md 6); the
   sample parts now provide data for them (solid_history.md 7).

## 8. NOT DECODED / open

- Sphere / torus face orientation against data; torus apple / lemon; SURFACE_DATA contents (Parasolid fills
  original / extended ranges, 'B'/'P'/'D' boundary chars); tolerant edges (SP-curves) - OCCT bodies with
  tolerances above 5e-6 mm are written as accurate edges anyway (risk: Parasolid's checker may flag them).
- How Mastercam closes chain gaps (the 25 near-identical replays); Both / Draft / Thin wall replay conventions
  (extrude.md 4.3 / 4.6).
- Whether Mastercam accepts zero McEptr records, regenerates with the dirty byte, or needs SURFACE_DATA: only a
  Windows test (sec. 6) can tell.

## 9. Decisions needed from the user (2026-10-23; 2026-11-18: item 1 settled by the decode - the names are written as Mastercam writes them (solid_history.md 3.2.1, 8) with dirty byte 0 as in all 807 corpus solids; items 2 - 4 still open, TODO.md "Waiting on the user")

1. New solids: write McEptr name records (copy the decoded kinds, sec. 6 file D) or none (file B), and dirty byte
   0 (trust our XT) or 1 (let Mastercam rebuild on open)? Depends on the Windows test.
2. Chain gaps: close them as here (midpoint joints, tolerant vertices) or snap the chain entities exactly?
3. OCCT boolean fuzzy value: Parasolid uses 1e-5 m (occt_3d_plan Q5) - exact booleans here.
4. Should foreign / extra attributes of stored solids (colours, import ids) be carried when such a solid is
   regenerated? (Today only CNC/mcEptrPtr and optional CNC/mcColor are written.)

## 10. In the app (2026-11-18, SOL-1..SOL-3)

- **Transmit version** (`Options::pk`): 27 = modeller 2700142 / `SCH_2700142_26105` for X9 parts, 28 = 2800143 / `SCH_2800143_28002` for
  Mastercam 2017 parts. The 235 schema-28002 blobs of the corpus have byte-identical headers apart from KEY / FILE / DATE (as the 26105
  ones); sch_26105 and sch_28002 transmit the same fields for every node the writer emits (they differ only in BODY.data / EDGE.data,
  not transmitted); BODY / ATTRIB_DEF / LIST constants are the same in both groups (res_size 1000.0000000000001 in 150, 1000.0 in 60 + 24
  sheets - which Mastercam path writes 1000.0 is NOT DECODED; the extrude-made user solids all have ...0001, kept).
- **Kernel**: `viewer/occt_solid.cpp` (formats/extrude.md 7); the bodies are shown through their XT; the part save writes them with
  their history (formats/solid_history.md 8).
- **Round trip harness** `tools/tests/xt_roundtrip.sh` (golden tools/tests/golden/xt/summary.txt): every Mastercam-written solid of the
  user's parts and of the corpus (X9 schema 26105 and 2017 schema 28002 manifests of ~/mastercam-re/work/occt/netx) read -> written (same
  pk) -> read, one process each, then compared node by node with the original (`xt_canon.py`, now with every geometry node type, a null
  pointer guard, per-solid STATUS, classes *simple* = planes / cylinders / cones / spheres / tori / B-surfaces / lines / circles / ellipses /
  B-curves without tolerant edges, *complex* = anything else, attribute nodes of definitions the writer does not carry counted as
  *foreign*). Results: section 10.1.

### 10.1 Results (run 2026-11-18)
| set | solids | round trip OK (vol / area 1e-9, valid) | not written | canonically EQUAL (+ foreign attributes only) | simple DIFF | complex DIFF |
|---|--:|--:|--:|--:|--:|--:|
| user (X9) | 108 | 108 | 0 | 106 (+0) | 2 (BODY.highest_node_id only) | 0 |
| corpus X9 (26105, pk 27) | 226 | 148 | 45 | 52 (+30) | 59 | 40 |
| corpus 2017 (28002, pk 28) | 235 | 151 | 51 | 54 (+30) | 59 | 41 |

Not written / failing: 26 + 28 sheet bodies ("no solid": sheets are refused), 5 + 7 lemon tori (major <= minor, NOT DECODED), 2 + 2 edges
with equal-sense fins (non-manifold after the reader), 11 + 13 reader refusals, 3 + 3 crashes in the reader / writer on blend-heavy
solids (not analysed), MISMATCH / REREAD_FAIL = solids whose OCCT tolerances reach 1e-3 .. 3.8 mm (tolerant edges, blends) - the writer
writes every edge as accurate, so its own re-read differs; none of these is a body this program's commands make.
Per geometry type (solids containing the type that came back canonically equal): PLANE 82 / 226 and 84 / 235, CYLINDER 34 / 134 and
33 / 134, CONE 7 / 58 and 7 / 63, TORUS 1 / 33 and 1 / 33, **SPHERE, B_SURFACE, ELLIPSE, B_CURVE 0** in both. The "simple DIFF" solids
differ mostly in fin / loop / vertex identities (HE.vertex, HE.loop, LF missing): the reader rebuilds faces it repairs (ShapeFix: seams,
closed edges with a vertex), so their node_ids are not carried and the node-id keyed comparison loses them - a limit of the harness, not
shown to be a writer error, but also **not a proof**: the FACE.sense / curve conventions of SPHERE, TORUS, B_SURFACE, ELLIPSE and B_CURVE
remain proven only against the reference formulas (4.1 synth) and by OCCT round trips, not against Mastercam-written data. The geometry the
Extrude kernel makes (planes, cylinders, lines, circles) is proven on the user parts (106 / 108 canonically equal, the 2 others only
BODY.highest_node_id) and in the corpus (PLANE / CYLINDER / LINE / CIRCLE solids above). Cones (drafted arcs) 7 solids.
