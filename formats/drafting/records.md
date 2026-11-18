# Drafting entity records: D_ID dimension, N_ID note/label, W_ID witness/leader, XH_ID cross-hatch, CD_ID copious data

Mastercam 2017 (mccore.dll, Ghidra VAs, image base 0x180000000). Reader: `tools/draft_dump.py` (`python3 -I`).
Decompiles: `decomp/draft/core_draftwf.c` (Init*, DraftGlobals*, calc_dim_note, draw_*), `decomp/draft/core_draft_rec.c`
(aPoint*, DfAptr, DimIsAssoc, DfOrdType, draw_wl_single, draw_cross_hatch, calc_point_dimension),
`decomp/draft/core_draft_rec2.c` (calc_*_dimension, InitDM, NoteIsAssoc, mTable*, FUN_180023970, DfCreate*),
`decomp/draft/core_draft_cfg.c` (config labels), plus `decomp/mccore_entdebug.c` (bytes_per_ent) and
`decomp/mccore_dbread.c` (v_read_ent_from_disk / post-read fix-ups). Text rendering (font files, `font_attr`
semantics, token replacement) is in the sibling `formats/drafting/text.md`.

Conventions: "ent+X" = offset from the record start (the `ent`, after the u32 length; see `formats/mcam.md`);
"body+X" = ent+0x68+X (class-specific part). `cite: file:line` refers to the decompile files above.
**NOT DECODED** = no source found; "hypothesis" = labelled guess, never used by the reader for anything but a name.

## 0. Framing and what is on disk

* Record = `[u32 len][ent]`; `len` = 0x68 + fixed body (+ variable tail). Fixed bodies (bytes_per_ent,
  mccore_entdebug.c:178ff): N_ID 0x10 and W_ID 0x20 **0x160**, D_ID 0x40 **0x7a8**, XH_ID 0x1000 **0x50**
  (shared branch, l.512-515), CD_ID 0x800 **0x14** + tail (l.239-253). D/N/W/XH have no tail; corpus: every D_ID record
  is 2064 = 0x810 bytes, every N/W 456 = 0x1c8, every XH 184 = 0xb8 (one corrupt 861-byte N_ID in
  `Rubix disassembled.mcam`, which mcam_walk.py also fails on).
* The disk image is the in-memory struct verbatim (v_read_ent_from_disk reads `len` bytes into a zeroed 0x2938
  ent, mccore_dbread.c:5033-5040); so the Init* defaults below are the field layout.
* **Pointers survive as identity tokens.** Every ent's `ent+0x20` (eptr, a DB_LIST_ENT* at save time) is unique
  within a file, and the eptrs stored inside drafting records (aPoint+0x28, ordinate parent, XH->CD, CD->XH) equal
  the `ent+0x20` of the referenced record in the same file (measured: CHANGE ATTRIBUTES.mcam, 1182 distinct eptr values over all records, the only repeat
  being 0 on DB_ID/ASSOC records; every dimension aPoint eptr resolves; corpus check §5). A writer must keep `ent+0x20` unique and reuse it for references.
* **Strings and variable-length leader data live in the memory table** (DB_ID/0xE "Memory Table Entity", the
  out-of-band `[0x28 hdr: i32 id@0, u64 size@0x10][size bytes]... until id==0` block, mTableRead
  core_draft_rec2.c:2416-2497). Records hold i32 ids; `mTableFindData(id)` returns the blob (l.2377).
  Corpus: note text, multi-point leader aPoint arrays and chained leader structs are all found there.
* Strings are 8-bit (Windows-1252 / Latin-1 as stored): measured `0xd8` = Ø (diameter prefix), `0xb0` = ° on angular
  text, line breaks are `\r\n` inside note text.

Class names (mccore strings): `N_ID: Note/Label Entity`, `W_ID: Witness/Leader Entity`, `D_ID: Dimension Entity`,
`CD_ID: Copious data Entity`, `XH_ID: Cross Hatch Entity`, `DB_ID: Memory Table Entity`. EntIsDraft = class 0x40,
0x10 or 0x20 (core_draftwf.c:461).

## 1. Common header fields used by drafting (ent+0..0x67)

Set by InitDIM / InitNOTE (core_draftwf.c:8-55, 239-286) and calc_*_dimension:
| ent+ | type | meaning | cite |
|---|---|---|---|
| 0x00 | u32 | class 0x40 / 0x10 (0x20 for W_ID) | InitDIM l.35, InitNOTE l.266 |
| 0x04 | u16 | 1 on create (also passed as the colour key to find_color in draw_dimension/draw_note) | InitDIM l.36; core_draftwf.c:4641 |
| 0x06 | u8 | colour | l.37 |
| 0x08 | i32 | level | l.38 |
| 0x10..0x1f | attrib | line attributes (`_main_attrib`..); 0x10 double = line width passed to drawing | l.42-45; draw_dimension l.4654 |
| 0x20 | u64 | eptr (identity, see §0) | |
| 0x34 | i32 | ent_idn | formats/mcam.md |
| 0x48,0x49 | u8,u8 | version stamp (corpus: 11.3c, 12.41, 13.45, 14.4a, 17.69). N/W: if major < 0x0b or (==0x0b and minor < 0x24) and the leader's `on` byte is 0, the leader arrow fields are ignored | DraftGlobalsFromNote core_draftwf.c:820-833 |

## 2. D_ID 0x40 dimension (`v8_dimension`, body 0x7a8)

Default body: FUN_180249820 (called by InitDIM), core_draftwf.c:1544-1596.

### 2.1 Type and per-type head (body+0x000..0x29f is a union selected by body+0x2a0)

**body+0x2a0 u8 = dimension type** (ent+0x308). Values from the creators: 0 linear (calc_linear_dimension
core_draft_rec2.c:832), 1 angular (calc_ang_dimension l.101), 3 ordinate (calc_ordinate_dimension l.1787),
4 point (calc_point_dimension core_draft_rec.c:455), 5 perpendicular (calc_perp_dimension core_draft_rec2.c:1960),
6 tangent (calc_tangent_dimension l.2094), 2 circular (calc_circular_dimension @18024b810 core_calcdim.c 672 / FUN_18024f3c0 4816 /
FUN_180253540; create.md 8, port viewer/draft_create_circ.c; 393 records, all on arcs, text `R ...` / `Ø ...`).
No other values occur (switches in DfAptr / DimIsAssoc / calc_dim_note handle 0..6 only). There is **no separate
baseline / chained / horizontal / vertical type**: H/V/parallel is a sub-field of linear (below); baseline/chain
commands (export `BaselineChainProc`, decomp/draft/circ_core.c 305) emit ordinary linear dims (calc_linear_dimension from the base /
previous point; create.md 9).

Definition points are `aPoint`s (0x98 bytes, §2.5). Where they are (DfAptr core_draft_rec.c:619-686,
DimIsAssoc l.761-850):

| type | body+0 | aPoints | other type-specific fields |
|---|---|---|---|
| 0 linear | u8 **orientation**: 0 horizontal, 1 vertical, 2 parallel (calc_linear_dimension l.826 stores it, l.844-865: angle 0 / 90° / atan2 of the points; measured value = |dx| / |dy| / distance, §5) | body+0x08, body+0xa0 | body+0x138 f64 `DfHvpAngle` (rotation of the H/V reference, l.830); body+0x140 u8 anchor 1/2 (FUN_18023d590 core_draft_rec.c:2617ff sets 1 or 2 by comparing distances to the two aPoints — hypothesis: which point the text offset is measured from); body+0x141 u8 0 = linear value, 1 = format as radius, 2 = as diameter (calc_dim_note l.1204-1212, calc_linear l.831 and l.947-955; corpus 983×0, 6×2 = lathe Ø) |
| 1 angular | (aPoint kind) | body+0x000, +0x098, +0x130, +0x1c8 (corpus: line A p1, line B p1, line A p2, line B p2, kinds `1`/`2`) | body+0x260/+0x268 two f64, +0x270 f64 = depth, +0x278 view, +0x280 u8 quadrant code (`FUN_18023f360(…,&qcodes)`), +0x281 u8, +0x288 f64 0.5, **+0x290 u8 add pigtail** (calc_ang_dimension core_draft_rec2.c:122-136; DraftGlobalsFromDim l.711). **value body+0x2d0 is in radians** (measured 2.0944 ↔ text `120.00°`) |
| 2 circular | (aPoint kind) | body+0x000 (arc centre, kind `C`, or `K`) | body+0x98 u8 NOT DECODED; **body+0x99 u8 1 = diameter, 0 = radius** (calc_dim_note l.1219; corpus 131 Ø / 262 R, text prefix agrees in every record that has a prefix); body+0xb0/+0xb8/+0xc0 f64 arc radius / start / sweep used to rebuild the arc when not associative (DfGetCircularArc core_draft_rec.c:244-248) |
| 3 ordinate | (aPoint kind) | body+0x000 | **body+0x98 f64 offset from the zero ordinate** (DfOrdType core_draft_rec.c:743-750 copies it to the value); **body+0xa0 u64 eptr of the zero (base) ordinate dim**, 0 = this is a zero dim (DfOrdType l.729-735; measured: FIXTURE PLATE.mcam, 15 children → 3 zero dims); body+0xba u8 h_txt_aln (0 / 2 the text starts at the pick, 1 centred), +0xbb u8 0 at creation (the regeneration FUN_1803053a0 runs only when it is set: NOT DECODED further), +0xbc u8 negative signs, +0xbd u8 arrowheads (DraftGlobalsFromDim l.713-716; calc_ordinate_dimension asm 0x18024e885-0x18024e8dd). One leader at +0x4a8 (2 or 4 points); the other wl slots off. DfOrdType: 0 = a child (the parent is a live type-3 dim of DfOrdType 1), 1 = zero (offset < tol), 2 = orphan (offset kept). Creation / layout: draw.md 4.5, create.md 8. After an Align: the dragged one h_txt_aln 1, the others +0x7c9 = 1 (df_load_oc) and h_txt_aln 0 / 2 (move_ordinate_dimension; create.md 9.3) |
| 2 circular | (aPoint kind) | body+0x000 (arc centre, kind `C` + the arc's eptr, t 0; `K` with DfAssoc off) | **body+0x98 u8 style: 2 radial (FUN_18024f3c0), 0 quadrant style** (a linear dimension between two quadrant points, quad_to_eps; create.md 8.3; corpus: 393 x 2); **body+0x99 u8 1 = diameter, 0 = radius** (calc_dim_note l.1219; corpus 131 Ø / 262 R, text prefix agrees in every record that has a prefix); body+0xa0 f64 DfHvpAngle (0 radial); body+0xa8 u8 quadrant code (1 radial; determine_cl's 1-8 for the quadrant style); body+0xb0/+0xb8/+0xc0 f64 arc radius / start / sweep (also used to rebuild the arc when not associative, DfGetCircularArc core_draft_rec.c:244-248). Witness slots off, +0x7ac = 0, the leader mode +0x7ae as the command left it (FUN_180253540) |
| 3 ordinate | (aPoint kind) | body+0x000 | **body+0x98 f64 offset from the zero ordinate** (DfOrdType core_draft_rec.c:743-750 copies it to the value); **body+0xa0 u64 eptr of the zero (base) ordinate dim**, 0 = this is a zero dim (DfOrdType l.729-735; measured: FIXTURE PLATE.mcam, 15 children → 3 zero dims); body+0xba u8 h_txt_aln, +0xbc u8 negative signs, +0xbd u8 arrowheads (DraftGlobalsFromDim l.713-716) |
| 4 point | (aPoint kind) | body+0x000 | body+0x98 u8 use 3D, +0x99 u8 XY(Z) labels, +0x9a u8 absolute (calc_point_dimension l.452-454; calc_dim_note l.1224-1240). No samples |
| 5 perpendicular | u8 sub-mode (param 6; 1 → 4th point is a keyboard `K` point) | body+0x008, +0x0a0, +0x138, +0x1d0 (3 or 4 used: `(body+0 > 1) + 3`, DimIsAssoc l.822-833) | +0x268/+0x269 u8 (param 8), +0x26a u8 linear orientation, +0x270 f64 DfHvpAngle, +0x278 u8 copy of linear body+0x140 (calc_perp_dimension l.1959-1975). No samples |
| 6 tangent | u8 mode (1: arc-arc, 2, 3/4) | body+0x008, +0x0a0, +0x138 (2 used if body+0 ≤ 2, DimIsAssoc l.835-847) | +0x1d0 u8 linear orientation, +0x1d8 f64 DfHvpAngle, **+0x1e0 u8 maintain tangency**, +0x1e1 u8 = 1, +0x1e2/+0x1e3 u8; arc 1/arc 2 radius +0x1e8/+0x1f0, start +0x1f8/+0x200, sweep +0x208/+0x210 (calc_tangent_dimension l.2094-2110). 10 samples |

So the "+0x68 short" column of the earlier scan (values 0/1/2/'1'/'2'/'C'/'E'/'I'/'K'/'P') is body+0: the linear
orientation for type 0 and the first aPoint's kind letter for types 1-4.

### 2.2 Common dimension fields (body+0x2a0..0x30f)
| body+ | ent+ | type | meaning | cite |
|---|---|---|---|---|
| 0x2a0 | 0x308 | u8 | type (2.1) | |
| 0x2a8 | 0x310 | p_2d + f64 | **placement point in VIEW coordinates** (x, y) and **+0x2b8 construction depth** (z). Equals the text position for every sample (2.3 DT+0x60) | calc_ang l.110-112; DraftGlobalsFromDim l.607 (`constr_depth = ent+0x320`) |
| 0x2c0 | 0x328 | i16 | view number (0 → 1 on draw); views ≥ 9 are DB_ID view records | Init l.1563; draw_dimension l.4630-4633 |
| 0x2c8 | 0x330 | f64 | drafting scale (`draft_scale`) | Init l.1565 |
| 0x2d0 | 0x338 | f64 | **measured value** fed to the formatter (linear length, radius/diameter, angle in rad, ordinate offset) | calc_dim_note l.1199 (`local_4e8`); DfOrdType l.745; measured §5 |
| 0x2d8 / 0x2e0 | 0x340/0x348 | f64 | + / − tolerance (linear or angular per type) | Init l.1566-1567; DraftGlobalsFromDim l.672-690 |
| 0x2e8 / 0x2f0 | 0x350/0x358 | f64 | witness gap / witness extension | Init l.1568-1569 |
| 0x2f8 / 0x300 | 0x360/0x368 | f64 | linear: text offset pair (FUN_18023d590 uses 0x2f8 for H, 0x300 for V, hypot for parallel — hypothesis: offset of the text from the anchor point in the rotated frame). Angular: +0x290 overlaps the union, not this | core_draft_rec.c:2600-2630 |
| 0x308 | 0x370 | u64 | zeroed by Init; NOT DECODED | Init l.1572 |

### 2.3 Witness lines / leaders (4 × `v8_wit_ldr`, 0x98 each)
`body+0x310` witness 1, `+0x3a8` witness 2 (kind 0), `+0x440` leader/dimension line 1, `+0x4d8` leader 2
(kind 1) (InitDimWL calls core_draftwf.c:1579-1582). draw_dimension draws them in the order 0x310, 0x440, text,
0x4d8, 0x3a8 (l.4660-4664). Layout (InitDimWL l.181-231, draw_wl_single core_draft_rec.c:852-1230,
FUN_180313970 core_draft_rec2.c:2576):

| wl+ | type | meaning |
|---|---|---|
| 0x00 | u8 | on (0 = not drawn; also gates the chain) |
| 0x01 | u8 | kind: 0 witness, 1 leader (InitDimWL arg), ≥2 = arc form (angular dimension lines; measured kind 2 only on angular dims) |
| 0x02 | u8 | arc form: arrow at the end of the sweep instead of the start (draw_wl_single l.1132-1140, 1173); 0 in Init |
| 0x03 | u8 | visible; draw_wl_single returns if 0 (l.995). Init 1 (u16 0x100 at +2) |
| 0x04 | u8 | 1 in Init; measured 0 on witnesses / 1 on leaders. NOT DECODED |
| 0x08 / 0x10 | f64 | arrowhead height (length) / width (`arrow_head_height/width`; notes `note_arrow_head_*`) |
| 0x18 | u8 | arrowhead style (`DAT_1813d61fa`, notes `DAT_1813d61fc`; config "Dimension arrowhead style" / "Note Arrow style selection number"). Measured: witnesses always 3 (draw_wl_single skips the arrow when 3, l.1133), leaders 0 or 2. Styles 4..7 and 10 take a curved path, 4/5 use half the length (l.1170-1174). Style names NOT DECODED |
| 0x19 | u8 | 1 = the points are an aPoint array in the memory table (id at +0x20) |
| 0x1a | i16 | number of points (Init 2) |
| 0x20 | p_3d[4] | inline points (VIEW coords, z = depth), when +0x19 == 0 and kind < 2 (l.1001-1007) |
| 0x20 | i32 | when +0x19 == 1: memory-table id of `npts` aPoints (0x98 each) (l.1011; NoteIsAssoc) |
| 0x20 | arc | kind ≥ 2: centre p_3d (view), +0x38 radius, +0x40 start angle, +0x48 sweep (radians) — same tail as a_3d (l.1121-1127) |
| 0x84 | i32 | memory-table id of the **next** v8_wit_ldr (chain; 0 = end) (FUN_180313970) |
| 0x88 | u64 | 0 in Init. NOT DECODED |

### 2.4 Dimension text (`v8_dim_txt` at body+0x570, 0x1c8 bytes; InitDT core_draftwf.c:61-103)
| DT+ (body+) | type | meaning | cite |
|---|---|---|---|
| 0x000 (0x570) | font_attr 0x60 | main text attributes (see table below / text.md §1) | InitDimFA |
| 0x060 (0x5d0) | p_3d | text position (VIEW coords, z = depth) | FUN_180313380 core_draft_rec2.c:2561 (`drw_string(ent+0x638, …)`) |
| 0x078 (0x5e8) | char[0x58] | **text**: either the formatted result (`R 0.850`, `120.00°`) or a template with the token `%v` = value (`Ø %v`, `%v TYP.`, `4 X %v`, `%v\r\n2 PLCS`); DfReplaceTokens token 0x7625 = "%v" (core_draft_rec2.c:2520) | DfCheckForTokens → drw_string |
| 0x0d0 (0x640) | font_attr | tolerance text attributes; height default `tol_text_height` | InitDT l.94-95 |
| 0x130 / 0x148 (0x6a0/0x6b8) | p_3d ×2 | tolerance text positions | FUN_180313380 |
| 0x160 / 0x189 (0x6d0/0x6f9) | char[0x29] ×2 | tolerance strings; drawn when body+0x750 > 0 / > 1 | FUN_180313380 |
| 0x1b8 (0x728) | u64 | 0 in Init. NOT DECODED | InitDT l.97 |

font_attr (0x60; InitDimFA l.110-174, InitNoteFA l.292-354, DraftGlobalsFromNote l.786-808): +0x00/+0x08 zero;
+0x10 text angle; +0x18 slant; +0x20 character rotation; **+0x28 height**; +0x30 character aspect (width factor);
+0x38 extra char spacing; +0x40 extra line spacing; +0x48 alt height; +0x50 alt aspect; u16 +0x58: bits 0-3 alt
colour, 4-7 fill colour, 8-9 horizontal alignment, 10-12 vertical alignment, 13-15 text path; u16 +0x5a: bit 0
spacing style, bit 1 fill, bits 2-3 mirror; u8 +0x5c **font id** (≥ TRUETYPE_FONT → index into the file's font
table, remapped on read, mccore_dbread.c:3258 (notes), 3286 (dimensions)), +0x5d alt font, +0x5e text box, +0x5f 0. Dimension text is
always h 1 / v 2 when globals are pulled (DraftGlobalsFromDim l.634, 644).

### 2.5 aPoint (associative point, 0x98)
aPointInit / aPointInitWorld / aPointSaveEntSel (core_draft_rec.c:45-194), aPointCopy (0x98 bytes, l.3-42):
| aP+ | type | meaning |
|---|---|---|
| 0x00 | char | **kind** (below) |
| 0x08 | p_3d | **world position** (stored; measured equal to the geometry point, §5) |
| 0x20 | u8 | 0 |
| 0x28 + 0x18·i, i = 0,1 | {u64 eptr, f64 t, f64 t2} | picked entity i (eptr = that ent's +0x20), curve parameter, surface v (only for SURF_ID) (aPointSaveEntSel l.185-193) |
| 0x50 | f64 | kind `O`: plane id (overlaps entity slot 1 t2) (aPointInitWorld l.160) |
| 0x58 | u8 | 1 = a relative point: world = +0x60 + view_to_world(+0x78) (the Angular command's typed-angle point, cad_linear.c:790-815) |
| 0x60, 0x78 | p_3d ×2 | the relative point's base (world) and view offset; init (0,0,0) and (1,1,0) |
| 0x90 | u8[4] | 0 |

Kind letters. Associative test FUN_180023970 (core_draft_rec2.c:149-182): `1 2 A C E M P R U` live if entity 0 is
alive; `I` if entity 0 or 1 is alive; `O` always; anything else (e.g. `K`, 0x20 from aPointInit) is free.
Meanings, measured by matching the stored point against the referenced entity (corpus, §5):
`P` = point entity's position (320/320), `1` = curve start (812 line p1 + 44 arc start), `2` = curve end (462 + 19),
`E` = an endpoint (either end), `M` = midpoint (49 line mid, 8 arc mid), `C` = arc centre (430), `I` = intersection
(two eptrs; 8 at a line end, 9 elsewhere), `K` = keyboard/free (set by aPointInitWorld default, no eptr),
`O` = plane origin (aPointInitWorld l.145-166), `A` = an arc's quadrant point, t = its angle in DEGREES (0 / 90 / 180 / 270 +
DfHvpAngle; FUN_180245600 circ_core.c 948, the circular quadrant style). `R`, `U`: NOT DECODED (`U`: 3 dimension points on arcs, not at
start/end/centre/mid; 3 leader points).

### 2.6 Number format block (body+0x738..0x769)
`dm_params` at body+0x738 (InitDM core_draft_rec2.c:581-647) and bytes after it (Init l.1585-1594,
DraftGlobalsFromDim l.592-700):
| body+ | ent+ | type | meaning |
|---|---|---|---|
| 0x738..0x743 | | | InitDM writes 0x10000, 0x10001, u16 1. NOT DECODED (old files: all 0xff) |
| 0x744 | 0x7ac | u32 | 0x30003 in InitDM; copied to `DAT_1813d61fe` unless df_linear_format fails. NOT DECODED |
| 0x748 | 0x7b0 | i16 | `dd_global` (first field of the `draft_attributes` global); calc_linear writes `(char)dd_global` (l.1073) |
| 0x74a | 0x7b2 | i16 | radius format |
| 0x74c | 0x7b4 | i16 | `DAT_1813d6202`. NOT DECODED |
| 0x74e | 0x7b6 | i16 | tolerance mode (3 → 1, > 3 → 0, InitDM / DraftGlobalsFromDim l.574-580); corpus all 0 |
| 0x750 | 0x7b8 | i16 | number of tolerance strings drawn (0..2) (FUN_180313380; ordinate sets 0/n, core_draft_rec2.c:1839-1856) |
| 0x752 | 0x7ba | i16 | units: `SetUnits(x == 1)` → 1 = metric (l.637). Measured also −2..3 in some X9-era files: NOT DECODED |
| 0x758 | 0x7c0 | i16 | coordinate format |
| 0x75a | 0x7c2 | i16 | fraction units |
| 0x75e/0x75f/0x760 | 0x7c6.. | u8 | DIN tolerance on / DIN character (measured 0x48 'H') / DIN value (measured 7) |
| 0x761 | 0x7c9 | u8 | automatic alignment |
| 0x762 | 0x7ca | u8 | 0x63 ('c') → not drawn and never associative (draw_dimension l.4636, DimIsAssoc l.783); 0 in all samples |
| 0x763 | 0x7cb | u8 | comma as decimal separator |
| 0x764 | 0x7cc | u8 | leading zeroes |
| 0x765 | 0x7cd | u8 | decimal places (linear or angular per type) |
| 0x767 | 0x7cf | u8 | trailing zeroes |
| 0x768 | 0x7d0 | u8 | construction view was 3D (`constr_view_3d()`) |
Note: the stored text is what is displayed; the format fields only matter when Mastercam regenerates it
(measured counter-example: an angular `70.0°` with decimal places = 2).

## 3. N_ID 0x10 / W_ID 0x20 (`v8_note`, body 0x160)

Default: FUN_18024a060 (InitNOTE) core_draftwf.c:1616-1647. Both classes use the same body.
| body+ | ent+ | type | meaning | cite |
|---|---|---|---|---|
| 0x00 | 0x68 | u8 | **kind**: 3 = note (text only), 2 = label (text + leader), < 2 = leader / witness only (no text) | draw_note core_draftwf.c:4724-4760 (text when > 1, leaders unless > 2); Init `*param_1 = 3` l.1623 |
| 0x08 | 0x70 | p_2d | text/anchor position in VIEW coords; z = **+0x18 depth** | draw_note `drw_string(ent+0x70)` l.4752; DraftGlobalsFromNote `constr_depth = ent+0x80` |
| 0x20 | 0x88 | i16 | view | Init l.1636 |
| 0x28 | 0x90 | f64 | drafting scale | l.1637 |
| 0x30 | 0x98 | font_attr | text attributes (InitNoteFA) | l.1639 |
| 0x90 | 0xf8 | i32 | **memory-table id of the text** (NUL-terminated, `\r\n` line breaks) | draw_note l.4746; mccore_dbread.c:3085 |
| 0x98 | 0x100 | v8_wit_ldr | leader (InitNoteWL: kind 1, note arrow style/size) — chain via +0x84 | l.1640; NoteIsAssoc core_draft_rec2.c:2270-2306 |
| 0x130 | 0x198 | u64 | 0. NOT DECODED | |
| 0x138 | 0x1a0 | u8 | associative flag written by NoteIsAssoc (1/0); 0x63 'c' → not drawn | core_draft_rec2.c:2305; draw_note l.4724 |
| 0x140..0x157 | | | 0 in Init. NOT DECODED | |

Kinds measured: N_ID 1432 × kind 3 (note), 270 × kind 2 (label, leader aPoints in the memory table), 1 × 255
(corrupt record); **W_ID 203 × kind 1** (leader without text, 2 points). Kind 0: no samples — hypothesis: a
stand-alone witness line (DfCreateWitness builds a kind-0 v8_wit_ldr, core_draft_rec2.c:489), NOT VERIFIED.
Old-version gate for the note leader's arrow fields: §1.

## 4. Cross-hatch: XH_ID 0x1000 + CD_ID 0x800

A hatch is **two records**: an XH_ID holding the style and an eptr to a CD_ID holding the already-generated line
segments. Drawing (draw_cross_hatch core_draft_rec.c:1712-2060) draws the CD segments; the boundary is not
referenced. WfMgr::AddCrossHatch just adds the CD's copious data (l.2063-2089).

XH_ID body (0x50; draw_cross_hatch l.1837-1846 + l.1863-1905, mccore_dbread.c:3181-3190):
| body+ | type | meaning |
|---|---|---|
| 0x00 | f64 | spacing: the divisor of the perpendicular offset that picks each line's pattern entry (l.1905). Measured 3.5 stored vs 3.4726 actual line spacing (TUTORIAL #7 EXERCISE), 0.01 vs 0.0096 (TUTORIAL_7): exact relation NOT DECODED |
| 0x08 | f64 | angle (radians; lines at this angle, set 2 at +90°) — measured 0.7854 ↔ lines at 45° |
| 0x10 | u8[16] | pattern 1: per-line line style (< 2 solid, else draw_line_style; 0 → next entry) |
| 0x20 | u8[16] | pattern 2 (cross set) |
| 0x30 / 0x31 | u8 | entries used in pattern 1 / 2 |
| 0x38 | i32 | index of the first line of set 2 (= n/2 when there is no cross set, measured 72 / 14) |
| 0x40 | u64 | eptr of the CD_ID |
| 0x48 | i16 | view |

CD_ID body (0x14 fixed; bytes_per_ent mccore_entdebug.c:239-253; draw_cross_hatch reads count/view/data):
| body+ | type | meaning |
|---|---|---|
| 0x00 | u64 | eptr of the owner (measured = the XH eptr) |
| 0x08 | i32 | n points |
| 0x0c | i16 | form: 31 on hatch, 200 = relief data (relief_convert_ent, mccore_dbread.c:3116-3118) |
| 0x0e | i16 | data layout: < 2 → f64 z + n × p_2d (2·n points = line pairs), 2 → n × p_3d, 3 → n × 0x30 |
| 0x10 | i16 | view (points are VIEW coords) |
| 0x14 | tail | the data |
Hypothesis (naming only): form / layout are IGES 106 "copious data" form number and interpretation flag (31 =
IGES section form; config string "Convert IGES copious data to geometry"). Corpus: 3 XH (2 files, X9 Design
tutorial 7), 3 hatch CDs (form 31, layout 1, 144 / 28 / 28 points, all lines at the stored angle); 110 other CDs
are form 200 layout 2 (relief, not drafting).

## 5. Corpus (599 files with drafting/hatch classes under <mastercam-samples>/net; the user's parts have none)

| record | count | files |
|---|---|---|
| D_ID linear horizontal | 293 | 104 |
| D_ID linear vertical | 289 | 108 |
| D_ID linear parallel | 407 | 122 |
| D_ID angular | 172 | 102 |
| D_ID circular (262 radius, 131 diameter) | 393 | 112 |
| D_ID ordinate | 153 | 9 |
| D_ID tangent | 10 | 10 |
| D_ID point / perpendicular | 0 | 0 |
| N_ID note (kind 3) / label (kind 2) | 1432 / 270 | 457 / 101 |
| W_ID leader (kind 1) | 203 | 50 |
| XH_ID / hatch CD_ID | 3 / 3 | 2 |

Consistency checks (`draft_dump.py --check`, every file):
* Dimension aPoints with an eptr: 2,320 resolve to a point/line/arc in the same file (912 have no eptr, all
  free kinds); **2,285 coincide (< 1e-4) with a characteristic point of that entity**, by kind: `P`→point 320,
  `1`→line p1 812 / arc start 44, `2`→line p2 462 / arc end 19, `E`→line p1 86 / p2 23 / arc start 16 / end 8,
  `M`→line mid 49 / arc mid 8, `C`→arc centre 430, `I`→line p1 8. The 35 others: `C` 10 and `M` 13 on arcs whose
  view ≠ 1 (the checker builds centre/mid only for view-1 arcs), `I` 9 (not at an end; on-curve test not done),
  `U` 3 on arcs.
* Linear dims in view 1 with DfHvpAngle 0: stored value = |dx| (H) / |dy| (V) / distance (parallel) of the two
  aPoints in **940 of 948**; the 8 others (SURFACING_8, two editions) use free `K` points and texts like
  `1.000 TYP.` — the value is stored at creation, not recomputed.
* Circular: diameter flag ↔ text prefix (`Ø` vs `R`) agrees in all records that carry a prefix.

Examples (decoded with `python3 -I tools/draft_dump.py FILE --json`):
1. `2017/…Lathe Professional Courseware Files/CHANGE ATTRIBUTES.mcam` @0x49de7: linear parallel, aPoints `P`
   (−4, 0.5, 0) and `P` (−4, −0.5, 0) whose eptrs are two P_ID records at those coordinates; value 1.0; text
   `Ø %v`; witnesses (−4.02,∓0.5)→(−4.3575,∓0.5) (gap 0.02, extension 0.05 beyond the dimension line x = −4.3075);
   leaders (−4.3075, ∓0.5)→(−4.3075, ∓0.1044); text height 0.1, aspect 0.6; tolerances ±0.0005.
2. Same file @0x4a67f: circular radius, aPoint `C` (−1.275, 0, 0) = centre of the referenced arc, value 0.85, text
   `R 0.850`, one 4-point leader from the arc to the text at (−0.396, 1.666).
3. `2017/…Handbook Volume 1 Files/FIXTURE PLATE.mcam`: 18 ordinates; zero dims (parent 0) at (0,0) for X and Y and
   (0,−5.25); a child `C` at hole centre (4.875, −0.625) has parent = X-zero eptr, offset 4.875, text `4.875`;
   another at the same centre has parent = Y-zero, offset 0.625 (stored unsigned), text `0.625`.
4. `2017/…Multiaxis Essentials Training Tutorial Files/TUTORIAL 4.mcam` @0x49953: angular in view 5, aPoints
   `1`,`1`,`2`,`2` on two lines, value 2.0944 rad, text `120.00°`, leaders kind 2 = arcs r 0.869 with b2 = 0 / 1.
5. FIXTURE PLATE.mcam @0x51403: N_ID label, text id 1 = `1/4-20 Tapped Hole\r\n4 PLCS`, leader with 3 aPoints
   in memory-table id 2: `C` on the hole arc + two `K` points.
6. `X9/…Design Training Tutorial Files METRIC/TUTORIAL #7 EXERCISE.mcx-9` @0x7aef1: XH spacing 3.5, angle π/4,
   pattern [1], CD eptr → CD @0x7afad (form 31, 144 2-D points, z 0, owner = the XH).

## 6. Global defaults (where Init* values come from)

The Init* functions copy globals (`dim_text_height`, `arrow_head_height`, `witness_gap`, `draft_scale`,
`num_digits`, …; `dd_global` is a `draft_attributes` struct). They are read from / written to the config file
(`mcamxm.config`, file-name strings in mccore) section **`[draft globals]`**; FUN_1801cc830 (decomp/draft/core_draft_cfg.c)
maps config item numbers to labels: 3100 `[draft globals]`, 3102 Witness, 3103 Witness gap, 3104 Witness extension,
3105 Leaders, 3106 Leader style, 3108 Dimension arrowhead style, 3109/3110 arrowhead height/width, 3111 Arrow position,
3114 Linear decimal places, 3117 Dimension orientation, 3118-3123 linear/angular tolerance (mode, +, −), 3124
Coordinate format, 3126 Diameter format, 3127 Radius format, 3128 Fractional units, 3129 Note height, 3130
Dimension height, 3131 Tolerance height, 3132 Dimension character aspect, 3135 Dimension Font, 3145-3149 DIN,
3151-3153 point-dim / leading-zero options, 3154 Associative Dimensioning, 3157-3159 note arrow style/height/width,
3160 comma separator, 3161-3164 note aspect/font/spacing, 3165 Automatic alignment, 3166 Dimension scale,
3167/3168 ordinate arrowheads / negative signs, 3170 Maintain perpendicular orientation, 3172-3175 note
rotation/box/path/slant, 3177 Angular decimal places, 3178 pigtail, 3179-3184 TrueType font names/italic/bold,
3185 trailing zeroes; `[cad settings]` 753-757 and 767-770 hold factor variants (tolerance height, witness gap /
extension, arrow height/width, dimension/note width, note arrow factors). The label→global pairing is by name only:
the config reader that stores each item into its global is **NOT DECODED**. No sample mcamxm.config is available
locally, and the Windows registry hive was not checked for these.

## 7. Open questions
* body+0x2f8/0x300 (linear text offset?) and body+0x140 (anchor 1/2) semantics: hypotheses only.
* aPoint kinds `R`, `U`; aPoint+0x60/+0x78 vectors; v8_wit_ldr +0x04 and +0x88; arrow style names.
* dm_params first 12 bytes, +0x744 (0x30003), +0x74c; units field values −2..3.
* XH spacing vs measured line spacing (3.5 vs 3.4726, 0.01 vs 0.0096).
* Note kind 0 (no samples); point (4) and perpendicular (5) dimensions have no samples.
* Writing: records are fixed-size images, but a writer must also emit the memory-table entries (note text,
  multi-point leaders, chained leaders) with unique ids, and keep eptr references consistent. Done for linear D_ID records made in
  the app (no memory table needed; create.md 7.4: mcam_saveops.c, aPoint eptrs = the picked entities') and for ordinates (create.md 8.5:
  the base's eptr written at +0x108). Circular D_ID records made in the app (create.md 8.4): the centre aPoint at +0x68 gets the arc's
  eptr. Notes (2026-11-05, notes.md 3): the memory-table writer (new entries above the base table's ids, appended before the terminator;
  mTableRead renumbers every id on load), notes made here written after the table, deleted part notes removed with their entries;
  labels / leaders made here: not yet (no creation).
