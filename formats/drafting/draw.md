# Drafting entities → geometry (Mastercam 2017, mccore.dll)

How Mastercam turns a dimension / note / leader / witness entity into lines, arcs,
arrowheads and text strings. Scope: GEOMETRY. Record byte layouts belong to
`records.md`; stroke/TrueType text rendering (inside `drw_string`) belongs to the text spec.

Source decomp files (all under `decomp/draft/`, gitignored, mccore.dll, image base 0x180000000):

| file | contents |
|---|---|
| `core_draftwf.c` | GetDraftWireframe, draw_dimension, draw_note, FUN_180313970, FUN_180313380, drw_string, calc_dim_note, FUN_18030f410 (dim text builder), FUN_18030ee50/FUN_18030ec10 (number formatters), df_adjust_ldrs, df_reset_ldr_arrows, add_a_leader, InitDimWL/InitNoteWL/InitDimFA/InitNoteFA, FUN_180249820 (dim body init), FUN_18024a060 (note body init) |
| `core_drawwl.c` | draw_wl_single, FUN_1803126e0 (arrowheads), FUN_1803144a0 (line emitter), FUN_180314360 (arc emitter), FUN_180314cf0, FUN_180314a50/FUN_180314600/FUN_1803147d0 (fill hatching), DfCheckForTokens |
| `core_drwtext.c` | FUN_18031a720 (polyline emitter), FUN_1803183c0/FUN_180318780 (text stroke output), save_draft_as_geo |
| `core_calcdim.c` | calc_linear/circular/ang/ordinate/point/perp/tangent_dimension, make_wl_single, FUN_180250410, FUN_18024fc30, FUN_180251460, FUN_18024f3c0, formatter helpers FUN_18030e2d0/FUN_18030f1b0/FUN_18030f2c0/FUN_18030e5e0/FUN_18030f070, WfMgr::AddDrafting |
| `core_dimlayout.c` | FUN_180252850, FUN_180252e50 (linear wl assembly), FUN_180253540 (radial wl assembly), FUN_180022d40 (angular wl assembly), FUN_1800206f0 (angular layout) |
| `core_dimlayout2.c` | FUN_180254bc0 (dim line ∩ text box), FUN_1802571c0, FUN_180250f60 (radial layout), FUN_180252d60 (text slot writer) |
| `core_dimgeo.c` | FUN_18024c160 (text box), FUN_180255b80 (dimension-line halves), FUN_180256450 (witness lines), box_limits, FUN_18024b250, FUN_180257270, FUN_18024b570 |

Notation: `wl` = `struct v8_wit_ldr` (0x98 bytes). `E+x` = byte offset x in the `ent`
(entity) struct (the dimension/note body starts at `E+0x68`). Angles in radians unless said.
All 2D work is done in **view coordinates** of the entity's view (`world_to_view` /
`view_to_world`); the third coordinate is the depth.

---------------------------------------------------------------------------------------------

## 0. Key finding — display is a replay of stored geometry

`draw_dimension` / `draw_note` do **not** recompute the dimension layout. They replay the
`v8_wit_ldr` records stored in the entity (points, arc, arrow type, arrow size) and draw the
text strings stored in the entity at the stored positions. All the layout logic
(gaps, extensions, text inside/outside, arrows flipped) runs only when a dimension is
created / edited / regenerated (`calc_*_dimension` → `make_wl_single`), and its result is
what is in the file.

Consequence for the port: to display a file's dimensions exactly like Mastercam you need
§1–§3 only (record replay + arrowheads + text call). §4 (creation geometry) is needed only
to create/regenerate dimensions.

The single exception is the dimension **value text** when the dimension's text contains
the `%v` token (§3.2): then the number is re-formatted at draw time.

---------------------------------------------------------------------------------------------

## 1. Call tree and primitive emitters

```
WfMgr::AddDrafting(ent&)                         core_calcdim.c 3282  (just forwards)
└─ GetDraftWireframe(ent&, WireframeCallback*)   core_draftwf.c 1346
     stores cb in global DAT_1813d8f10 (asserts it was null, Drdim.cpp 0x4b9)
     type 0x40            → draw_dimension(ent, 0)
     type 0x10 or 0x20    → draw_note(ent, 0)     ((type-0x10)&~0x10)==0
     else                 → _MCAssert(Drdim.cpp 0x4c4)
     clears DAT_1813d8f10 afterwards

draw_dimension(ent, color)                       core_draftwf.c 4598
├─ FUN_180313970(E+0x378 wl, view, depth, width, color)   witness 1
├─ FUN_180313970(E+0x4a8 wl …)                            leader/dim-line 1
├─ FUN_180313380(ent, view, width, color)                  text (main + tolerances)
│    └─ DfCheckForTokens → drw_string ×1..3
├─ FUN_180313970(E+0x540 wl …)                            leader/dim-line 2
└─ FUN_180313970(E+0x410 wl …)                            witness 2

draw_note(ent, color)                            core_draftwf.c 4682
├─ drw_string(E+0x70, view, font E+0x98, width, color, mode, text = mTable[E+0xf8])
└─ FUN_180313970(E+0x100 wl chain …)                      leaders

FUN_180313970(wl, …)                             core_draftwf.c 7566
   do { draw_wl_single(wl,…); wl = mTableFindData(*(long*)(wl+0x84)); } while(wl)
   (a wl list is a linked chain: next record id at wl+0x84)

draw_wl_single(wl, view, depth, width, color)    core_drawwl.c 74
├─ FUN_1803144a0(l_3d*, color)       LINE      → cb vtable +0x10
├─ FUN_180314360(a_3d*, color, 1)    ARC       → cb vtable +0x18
└─ FUN_1803126e0(l_3d*, h, w, color, type, view)  ARROWHEAD (uses the two above)

drw_string(...)                                  core_draftwf.c 7621
└─ CDrawString … FUN_1803183c0 / FUN_180318780 → FUN_18031a720  POLYLINE → cb vtable +0x08
```

### 1.1 The WireframeCallback interface (as used by mccore)

| vtable slot | call site | arguments | emitted for |
|---|---|---|---|
| +0x08 | FUN_18031a720 (core_drwtext.c 299) | `(cb, p_3d* pts, int n)` world-space polyline | text strokes (stroke fonts; CDrawString flushes runs of points, FUN_1803183c0 core_drwtext.c 162) |
| +0x10 | FUN_1803144a0 (core_drawwl.c 1708) | `(cb, l_3d* line)` world space, 2×p_3d | every line segment |
| +0x18 | FUN_180314360 (core_drawwl.c 1038) | `(cb, a_3d* arc)` world arc | every arc / circle |

Filters before emitting (both cite core_drawwl.c):
* line: emitted only if 3D length ≥ `tol` (line 1681). Zero-length witness lines vanish this way.
* arc: emitted only if radius (`a_3d+0x48`) ≥ `tol` (line 1006).

`a_3d` layout as used here (from `arc_2d_to_3d`, core_drawwl.c 946 and the arrowhead code):
`+0x00` start p_3d, `+0x18` end p_3d, `+0x30` center x, `+0x38` center y, `+0x40` depth,
`+0x48` radius, `+0x50` start angle, `+0x58` sweep, `+0x60` short view.

Without a callback (normal screen draw) the same three emitters call `drw_3d_line`,
`drw_3d_arc`, `drw_polyline`; with global `DAT_1813d8f08` set (`save_draft_as_geo`,
core_drwtext.c 3) they `store_ent` real line (type 2) / arc (type 4) entities instead.

**Filled arrowheads with a callback:** the fill calls (`gl_fill_tri`, `gl_fill_circ`,
`gl_fill_rect`) are chosen only by `graphics_support==1 && !draw_curves_without_opengl &&
!(XOR on && XOR-GDI)`; the callback global is *not* tested there (core_drawwl.c 1363–1366,
1390–1394, 1488–1492). The non-GL fallback hatches the shape with lines spaced 1 screen
pixel apart (`scrn_to_uu` of pixel (0,0)→(0,1)): FUN_180314a50 (triangle), FUN_180314600
(circle), FUN_1803147d0 (rectangle); those hatch lines go through FUN_1803144a0 and so do
reach the callback. Port recommendation: draw the fill as a solid polygon.

### 1.2 draw_dimension gating (core_draftwf.c 4598–4676)

* view = `*(short*)(E+0x328)`, 0 → 1 (4630). depth = `*(double*)(E+0x320)` (4629).
* drawn only if view == current graphics view **or** global `iso_view_dimensions` (4636),
  and `E[0x7ca] != 0x63` (4636), and the level is visible.
* colour: `find_color(E+4)`; if selected-dirty (`DraftIsDirty`) use `df_dirty_color`.
* line width = `*(double*)(E+0x10)` (pen width in hardcopy).
* **Special case** (4648–4652): if leader 1 is active (`E[0x4a8]!=0`) and
  `E[0x308]==3` and `E[0x125]==0`, leader 1's arrow type (`E+0x4c0` = wl+0x18) is forced to
  3 (none) for this draw and restored afterwards (4665–4667).
* draw order: wit1 (0x378), ldr1 (0x4a8), text, ldr2 (0x540), wit2 (0x410) (4660–4664).

### 1.3 draw_note gating (core_draftwf.c 4682–4766)

* view = `*(short*)(E+0x88)` (0→1), depth = `*(double*)(E+0x80)`.
* `E[0x68]` = note kind byte (init value 3, FUN_18024a060 core_draftwf.c 1616):
  * `E[0x68] > 1` → draw the text `mTable[*(long*)(E+0xf8)]` at `E+0x70` with font_attr `E+0x98` (4745–4753);
  * `E[0x68] > 2` → stop (no leaders) (4754);
  * otherwise draw the leader chain at `E+0x100` (4756).
  So: 0/1 = leaders only (this is the type-0x20 standalone witness/leader entity, see §4.6), 2 = text + leaders, 3 = text only.
* entity skipped if `E[0x68] >= 2` and the text id `E+0xf8` is 0 (4725).

---------------------------------------------------------------------------------------------

## 2. draw_wl_single — one witness / leader record (core_drawwl.c 74–444)

### 2.1 v8_wit_ldr fields used by drawing

| off | type | meaning | evidence |
|---|---|---|---|
| +0x00 | u8 | active (0 → not drawn) | draw_wl_single 210; make_wl_single sets 1 (core_calcdim.c 2563) |
| +0x01 | u8 | kind: 0 = witness polyline, 1 = leader polyline (with arrow), 2 = arc | draw 213/302/346; InitDimWL param (core_draftwf.c 181) |
| +0x02 | u8 | arc only: arrow at arc END instead of start ("reversed") | draw 352, 386 |
| +0x03 | u8 | visible (0 → not drawn) | draw 210; set by FUN_180256450 core_dimgeo.c 361–362 |
| +0x04 | u8 | "has arrow" flag used at creation | make_wl_single core_calcdim.c 2567, 2572–2577 |
| +0x08 | f64 | arrow length `h` ("arrow_head_height") | InitDimWL; draw passes it to FUN_1803126e0 |
| +0x10 | f64 | arrow width `w` ("arrow_head_width") | idem |
| +0x18 | u8 | arrow type (§2.3) | disasm 180313e4a / 1803141dc (`movzbl 0x18(%rsi)`) |
| +0x19 | u8 | points are associative (stored as an `aPoint` array in mTable id `*(long*)(wl+0x20)`) | draw 214–227 |
| +0x1a | i16 | number of points | draw 216, loop 241 |
| +0x20.. | p_3d[n] | polyline points (view coords; non-assoc) | draw 218 |
| +0x20..+0x50 | | arc: center x,y,depth (+0x20,+0x28,+0x30), radius +0x38, start angle +0x40, sweep +0x48 | draw 335–345 |
| +0x50 | f64 | arc only: arrow rotation offset about the center | FUN_180314cf0 core_drawwl.c 1045 |
| +0x84 | i32 | mTable id of the next wl in the chain | FUN_180313970 7574 |

Drawing sets line style 1 (solid) and the width passed in; restores width 1.0 at the end
(core_drawwl.c 211–212, 437).

### 2.2 Polyline kinds 0 and 1 (core_drawwl.c 213–343)

Points: non-assoc → `aPointInitView(pt[i], view, wl+0x20+i*0x18, 'K')`; assoc → aPoint array.
Each point projected to the view plane at `depth` (`aPoint2Plane`). Segments are drawn for
i = 0 .. n−2 between P[i] and P[i+1]:

* **Segment 0** (line 254–309): copy `A = P[1]`, `B = P[0]` (B is the arrow tip).
  If kind == 1 and arrow type ∈ {4,5,6,7,10}: move B toward A by
  `s = h` (types 6,7,10) or `s = h/2` (types 4,5) (265–295):
  `θ = atan2(B−A)`, `B' = B − s·(cosθ, sinθ)`.
  Emit line A→B' (298–299). Then if kind == 1, draw the arrowhead with the **unshortened**
  line `L = {P[1], P[0]}` (tip = P[0]) via `FUN_1803126e0(L, h, w, color, type, view)` (300–307,
  args verified in disasm at 0x1803141d6–0x1803141f7).
* **Segment i ≥ 1** (310–328): line P[i]→P[i+1], except: if the direction of this segment
  differs from the previous one by exactly 180° (|(|prev°−cur°|)−180| < 1e‑6, line 312) the
  start point x,y is replaced with the previous segment's start (P[i−1]) (disasm
  0x18031429e–0x1803142ab stores the previous segment start; 0x180314223–0x180314264 copies it),
  i.e. a fold-back is drawn as one overlapping segment.
* Kind 0 (witness) never draws arrows nor shortens.

### 2.3 Arrowheads — FUN_1803126e0(L, h, w, color, type, view) (core_drawwl.c 1183–1563)

Inputs: `L.p0` = tail point (gives direction), `L.p1 = T` = tip. Nothing is drawn if
`greeking_on` (unless hardcopy) or `|h| < tol` (1299–1300).
`θ = atan2(T − p0)` normalised to [0,2π); 0 if |T−p0| ≤ tol (1309–1318).
All shape points are built in view coords at T's depth and converted with `view_to_world`.

| type | shape | formula (cite) |
|---|---|---|
| 0 | two "wing" lines (open) | requires |w| ≥ tol. `α = atan((w/2)/h)`, `ℓ = h/cosα`; `W1 = T − ℓ(cos(θ+α), sin(θ+α))`, `W2 = T − ℓ(cos(θ−α), sin(θ−α))`; lines T→W1, T→W2 (1325–1357). Wing ends lie h back along the axis, ±w/2 across. |
| 1 | closed triangle | type 0 + base line W1→W2 (1358–1362). Leader line runs to the tip (no shortening). |
| 2 | filled triangle | type 1 + fill triangle (T,W1,W2) (`gl_fill_tri` or hatch FUN_180314a50) (1363–1370). |
| 3 | none | no case in the switch. |
| 4 | circle | arc center T, radius h/2, start 0, sweep 2π (`0x401921fb54442d18`), start=end=(T.x+h/2,T.y) (1375–1388). Leader line shortened by h/2. Not rotated. |
| 5 | filled circle | type 4 + `gl_fill_circ` / hatch FUN_180314600 (1389–1398). |
| 6 | rectangle | B = T − h(cosθ,sinθ), M = (T+B)/2. Half extents: if θ is NOT ≈π/2 and NOT ≈3π/2: (hx,hy) = (h/2, w/2) else (w/2, h/2) (1424–1430). If θ ≈ 0, π, π/2 or 3π/2 (within tol) θ := 0 (1435–1441). Corners C1=(M.x−hx,M.y−hy), C2=(M.x+hx,M.y−hy), C3=(M.x+hx,M.y+hy), C4=(M.x−hx,M.y+hy) rotated by θ about M; lines C4–C1, C2–C3, C1–C2, C4–C3 (1442–1486). Leader shortened by h. |
| 7 | filled rectangle | type 6 + `gl_fill_rect` / hatch FUN_1803147d0 (1487–1496). |
| 8 | slash | θ := 0 if θ≈0 or ≈π (1505–1510). Line from T+R_θ(−h/2,−h/2) to T+R_θ(+h/2,+h/2) (rotation about T) (1500–1525). (Only for exactly horizontal/vertical-left lines is the slash at 45°; for vertical θ=π/2 it is rotated 90°.) Not shortened. |
| 9 | integral (two quarter arcs) | same θ rule as 8. Arc A: center T+R_θ(h/2,0), r=h/2, start θ+π/2, sweep π/2 (from T+R_θ(h/2,h/2) to T). Arc B: center T+R_θ(−h/2,0), r=h/2, start θ+3π/2, sweep π/2 (from T+R_θ(−h/2,−h/2) to T) (1526–1558; sweep const `0x3ff921fb54442d15`). Not shortened. |
| 10 | closed triangle, leader stops at base | same drawing as type 1 (case grouped with 0,1,2; base line because `(type−1)&0xf6==0`, 1358) but draw_wl_single shortens the leader line by h (core_drawwl.c 268, 371–373). |

UI names: mccoreres.dll string table has `Triangle`(1773), `Open Triangle`(1777),
`Wedge`(1780), `None`(1782), `Circle`(1784), `Rectangle`(1787), `Slash`(1789),
`Integral`(1791) and a separate `Filled` string. **The UI-name → type-number mapping is
NOT DECODED** (the dialog that fills the combo was not found in mccore.dll; it is probably
in the managed mccore.ui.dll). From geometry only: 3 = None, 4/5 = Circle (unfilled/filled),
6/7 = Rectangle, 8 = Slash, 9 = Integral; 0/1/2/10 are the triangle family.

Defaults: dimension wl arrow type = global `DAT_1813d61fa` (InitDimWL core_draftwf.c 181–231),
note wl = `DAT_1813d61fc` with `note_arrow_head_height/width` (InitNoteWL 361–391).

### 2.4 Arc kind 2 (core_drawwl.c 331–434)

Arc A: center (wl+0x20, +0x28), depth +0x30, radius r = +0x38, start a0 = +0x40, sweep s = +0x48.
* If arrow type == 3: just emit arc A (433).
* Else tip point `T = C + r(cos a, sin a)` with `a = a0` (wl+2 == 0) or `a = a0+s` (wl+2 != 0)
  (347–357).
* If `h > r` or arc length ≤ tol: emit arc A only, no arrow (359, 371, 433).
* Arrow base point: point on the arc at arc length `d` from the tip end,
  `d = h` (h/2 for types 4,5); for reversed arcs parameter `t = (len−d)/len` else `d/len`
  (`pt_on_ent_from_t`) (378–397). Arrow line = {base, T}.
* `FUN_180314cf0` (core_drawwl.c 1045): if wl+0x50 > 1e‑6, rotate base and tip about the
  center by `wl+0x50` (negated if reversed) — arrow moved along the arc.
* Types 4–7,10: the arc is trimmed at the base point: `constr_arc_new(start', end', center)`
  where the arrow-end endpoint is replaced by the base point (400–418). The trimmed arc is
  emitted only if the new sweep < original sweep (disasm 0x180313e1f–0x180313e3f: `jbe` skips
  the emit otherwise — i.e. in that case **no arc is drawn**); if `constr_arc_new` fails the
  original arc is emitted.
* Other types: emit arc A untrimmed (425).
* Then `FUN_1803126e0({base,T}, h, w, color, type, view)` (427).

---------------------------------------------------------------------------------------------

## 3. Text placement (arguments to drw_string)

`drw_string(p_3d const& pos, short view, font_attr* fa, double width, uchar color,
uchar mode, uchar* text)` (core_draftwf.c 7621). `mode` = 0 when a callback is active
(9 otherwise, = draw in all viewports) (FUN_180313380 7593–7595, draw_note 4708–4711);
mode 1 + main colour when `DAT_1813d8f08` (save as geometry).
The position is the **text anchor** interpreted by the font_attr justification bits; the
rendering itself is the text spec's job.

### 3.1 Dimension text — FUN_180313380 (core_draftwf.c 7584–7613)

| string | position | font_attr | condition |
|---|---|---|---|
| main value text (buffer from DfCheckForTokens) | `E+0x638` | `E+0x5d8` | always (7602–7604) |
| tolerance line 1 `E+0x738` | `E+0x708` | `E+0x6a8` | `*(short*)(E+0x7b8) > 0` (7605–7608) |
| tolerance line 2 `E+0x761` | `E+0x720` | `E+0x6a8` | `*(short*)(E+0x7b8) > 1` (7609–7612) |

This matches the v8_dim_txt writer FUN_180252d60 (core_dimlayout2.c 165): slot 0 → font
+0x00, pos +0x60, text +0x78; slot 1 → font +0xd0, pos +0x130, text +0x160; slot 2 →
pos +0x148, text +0x189 (relative to `E+0x5d8`).

Dimension font_attr defaults (InitDimFA core_draftwf.c 110, init_attribs_dim
core_dimlayout.c 2089): height `dim_text_height` (+0x28), angle `dim_text_angle` (+0x10),
slant (+0x18), char expansion `chr_exp` (+0x30), char spacing `extra_sp` (+0x38), line spacing
`extra_ln_sp` (+0x40), bitfield +0x58: horizontal-align bits 0x300 = 1, vertical-align bits
0x1c00 = 2 (0x800), text path bits 0xe000. At creation the text angle is set to the
dimension-line angle unless the flag argument `param_9` forces 0 (FUN_180250410
core_calcdim.c 5419–5422); calc_linear_dimension likewise forces 0 when `DAT_1812d980c`
("horizontal text") is set (core_calcdim.c 1040–1043).

Where the stored positions come from (creation, FUN_180251460 core_calcdim.c 6585): the
dimension's text point `P` is the centre of the whole text block. `box_limits` gives the
block half-extents `bx, by` (text height × `extra_ln_sp` used as inter-line gap). Main text
centre = `P + R_angle((−bx + w0/2), 0)` (or `(+by − h0/2)` vertically when there is a
limits tolerance below it), so the value text is left-aligned in the block. Tolerances:
mode 1 (±, two stacked lines): x = `P.x + bx − max(w1,w2)/2`, y = `P.y + by − h1/2` (upper)
and `P.y − by + h2/2` (lower); mode 2 (limits) second line below main text at
`P.y − by + h1/2`; mode 3 (single ±): `x = P.x + bx − w1/2`, `y = P.y`. All rotated
about P by the text angle (core_calcdim.c 6637, 6639, 6673, 6677, 6685, 6696). (Widths `wi` /
heights `hi` come from `get_ext` — text spec.)

### 3.2 Which value string is drawn — DfCheckForTokens (core_drawwl.c 3–65)

* If `E[0x7a0] != 0` **and** the stored text `E+0x650` contains `%v`: compute the current
  value string with `calc_dim_note` and substitute it for every `%v` (DfReplaceTokens,
  core_draftwf.c 6949; token test DfTxtHasTokens 7039 = `strstr(text,"%v")`).
* Otherwise the stored string `E+0x650` is drawn verbatim (core_drawwl.c 55).

### 3.3 Note text

draw_note: `drw_string(E+0x70, view, E+0x98, width, color, mode, mTable[E+0xf8])`
(core_draftwf.c 4752). Note font defaults: InitNoteFA (core_draftwf.c 292): `note_text_height`,
`note_text_angle`, `h_txt_aln` (bits 0x300), `v_txt_aln` (bits 0x1c00), `mirror` (+0x5a bits
0xc), `note_text_path`, `note_text_bx` (+0x5e box style).

---------------------------------------------------------------------------------------------

## 4. Creation geometry per dimension type (what ends up in the records)

### 4.0 Record slots and type codes

Every dimension (`ent` type 0x40) has four wl slots and a dim_txt:

| slot | role (linear/angular) | role (radial) |
|---|---|---|
| `E+0x378` | witness 1 (kind 0) | unused (`active=0`, core_dimlayout.c 2571) |
| `E+0x410` | witness 2 (kind 0) | unused (2642) |
| `E+0x4a8` | dimension line half 1 (kind 1 linear, kind 2 angular) | leader 1 (kind 1) |
| `E+0x540` | dimension line half 2 | leader 2 (deactivated in some cases, 2643–2645) |

Initial slots (FUN_180249820 core_draftwf.c 1544): +0x310/+0x3a8 (E+0x378/E+0x410) kind 0,
+0x440/+0x4d8 (E+0x4a8/E+0x540) kind 1.

`E[0x308]` = dimension type: 0 linear (FUN_180252e50 core_dimlayout.c 2267), 1 angular
(calc_ang_dimension core_calcdim.c 101; FUN_180022d40 core_dimlayout.c 821), 2 circular
(FUN_18024f3c0 core_calcdim.c 4891; FUN_180253540 core_dimlayout.c 2510), 3 and 4 (4 = ordinate per
calc_dim_note's point-value case), 5, 6. Mapping of 3, 5, 6 to UI names: **NOT DECODED**
(calc_dim_note uses value-format kind 5 for 3 and kind 1 for 5/6; core_draftwf.c 1203–1250).

`make_wl_single(wl, kind, has_arrow, arrow_type, h, w, mkEnt, …)` (core_calcdim.c 2526–2648):
active=1, kind, reversed=0, arrow type := 3, assoc := 0 (2566), has_arrow (2567), h = 
`arrow_head_height`, w = `arrow_head_width` (2568–2569), next = 0; if kind > 0 the arrow type
becomes `has_arrow ? arrow_type : 3` (2572–2578). With `mkEnt` it also builds a standalone
type‑0x20 entity whose note body holds that wl (§4.6).

### 4.1 Witness (extension) lines — FUN_180256450 (core_dimgeo.c 310–371)

Arguments: gap `g` = `witness_gap`, extension `x` = `witness_extension` (passed from
FUN_180250410 core_calcdim.c 5562–5563; also stored in `E+0x350`/`E+0x358`,
core_dimlayout.c 2277), geometry points `P1, P2`, dimension-line ends `D1, D2`, mode `m`.

For i = 1,2: `L = |Di − Pi|` (2D), `u = (Di − Pi)/L` (0 if L = 0);
`start = Pi + g·u`; `end = Di + x·u` if `g < L`, else `end = start` (zero length → not
emitted, §1.1) (335–360). Depth = D1.z for all four points. Two points per wl.
Visibility: wit1 visible iff m ∈ {1,3}; wit2 iff m ∈ {2,3} (361–362). So the witness starts
`g` away from the picked point and overshoots the dimension line by `x`.

Angular dimensions use the same rule (FUN_1800206f0 core_dimlayout.c 600–660 and 700–745):
`Q = C + R(cos a, sin a)` on the dimension arc; if Q lies on the picked line segment
(`pt_near_line`, core_dimlayout.c 567) the witness is degenerate (start = end = Q); else
E = the picked line's second endpoint (`l_2d+0x10`), or — when `vlines_saved` — the end of the
saved line chosen by the projection parameter of Q (t > 0.5 → second end) (575–591):
`start = E + g·u`, `end = Q + x·u`, `u = unit(Q − E)` (621–660). Witness visibility from mode
`param_15` exactly as above (769, 771); the two arcs' visibility comes from `param_14` the same
way when `DAT_181371d6a` is set, otherwise (or in the pigtail case `DAT_181371d70 &&
DAT_181371d6e`) both arcs are forced visible (772–784).

### 4.2 Linear dimension (calc_linear_dimension core_calcdim.c 797 → FUN_180250410 5314 → FUN_180252850 core_dimlayout.c 2131 → FUN_180252e50 2245)

* Orientation `*param_6`: 0 horizontal, 1 vertical, 2 parallel; parallel collapses to
  horizontal/vertical if the points share y/x within tol (after rotating by −DfHvpAngle).
  Dimension angle φ = 0, 90° or the P1→P2 angle; when "horizontal text" is off the text
  angle is normalised by subtracting 180° when it lies in (90°,180°) or (180°,270°)
  (core_calcdim.c 1017–1035).
* Points may be swapped so P1 is left/bottom (swap flag set at core_calcdim.c 1051, 1054, 1065;
  swap 1066–1090).
* Text box: `FUN_18024c160` (core_dimgeo.c 3–37): rectangle centred on the text point,
  half sizes from `box_limits` + padding `pad = text_height × extra_ln_sp`
  (core_dimlayout.c 2167; 0 when the "text on line" flag `in_stack_88` is set), rotated by
  the text angle.
* Dimension-line halves: `FUN_180255b80` (core_dimgeo.c 40–308) works in the frame rotated
  by −φ about the text point. It decides text-inside vs text-outside:
  inside when `D1.x ≤ T.x ≤ D2.x`. Inside: `FUN_18024b250` (core_dimgeo.c 499) — each half
  runs from its witness intersection to the text box edge (`FUN_180254bc0`
  core_dimlayout2.c 207 intersects the dim line with the 4 box edges) minus an extra
  offset, never shorter than `5·tol`; arrows point outward to the witness lines.
  Outside / arrows-outside (`FUN_180257270` core_dimgeo.c 612): halves extend `2·h`
  (h = arrow length) beyond the witness lines with arrows pointing inward; a third point
  joins to the text. Text-inside also requires `dimension length ≥ 2.1·h`
  (FUN_180250410 core_calcdim.c 5447–5450; calc_linear_dimension core_calcdim.c 1173: below
  that, and arrow type ≠ 3, the "arrows outside" flag `uVar18` is forced to 1).
  The y of all half points is the dimension-line offset `T.y` (or the box-edge intersection
  computed by `l_ints` against the text box, 241–255) and points are rotated back by φ
  (258–281).
  Exact branch table of FUN_18024b250/FUN_180257270 (which half gets 2 vs 3 points,
  `param_15` one-sided modes 1/2): decompiled but **NOT fully decoded** — read
  core_dimgeo.c 499–596 and 612–825 when porting creation.
* Record assembly FUN_180252e50 (core_dimlayout.c 2245–2474): wit1 → `E+0x378`
  (make_wl_single kind 0), ldr1 → `E+0x4a8` (kind 1, arrow), text → `E+0x5d8`
  (FUN_180251460), ldr2 → `E+0x540` (kind 1, arrow), wit2 → `E+0x410` (kind 0).
  Also stores `E+0x350` gap, `E+0x358` extension, `E+0x330` draft_scale, `E+0x338` value,
  `E+0x7cb..7cf` comma/leading/trailing/digits, `E+0x7b6` tolerance mode, `E+0x7b8` number
  of tolerance strings (0, 1 or 2) (2276–2333).

### 4.3 Circular (radius / diameter) — calc_circular_dimension core_calcdim.c 672

* "Quadrant" style (`*param_4 == 0`): two quadrant points of the arc (`quad_to_eps`) are
  dimensioned as a linear dimension, then `E[0x308] = 2`, `E[0x101]` = diameter flag
  (core_calcdim.c 750–769).
* Otherwise `FUN_18024f3c0` (4816): `E[0x100] = 2`, `E[0x101] = !radius`, `E[0x308] = 2`;
  if `_dd_global==0` and arrow ≠ none and `radius − arrow_head_height < 0` the
  "arrows outside" flag `_dd_global` is forced to 1 (core_calcdim.c 4952–4956). Layout `FUN_180250f60` (core_dimlayout2.c 52) →
  `FUN_18024a3c0` (core_dimlayout.c 1318; decoded 2026-10-27, create.md 8.3: FUN_180251840 attachment, FUN_180256700 the two
  leaders) and assembly `FUN_180253540` (core_dimlayout.c 2475): witnesses off, two leaders (kind 1) at `E+0x4a8`, `E+0x540`.
  Ported: viewer/draft_create_circ.c (oracle 132 / 164 2017 records fully equal, create_port.md 4). The quadrant style, the style
  choice (determine_cl) and quad_to_eps ('A' aPoints): create.md 8.2-8.3.

### 4.4 Angular — calc_ang_dimension core_calcdim.c 3 → FUN_1800226b0 3295 → FUN_1800206f0 core_dimlayout.c 122 → FUN_180022d40 789

* Vertex = intersection of the two lines (`l_ints`), quadrant selection `param_5` 1..4
  picks which line ends bound the angle (core_calcdim.c 3358–3400).
* Dimension line = two arcs (kind 2) about the vertex, radius = distance of the picked text
  point; one arc has `reversed = 1` (core_dimlayout.c 768), so each arc carries its arrow at
  the angle boundary; arcs at `E+0x4a8`, `E+0x540` (931, 968), witnesses at `E+0x378`,
  `E+0x410` (899, 1002). Start/sweep normalised to [0,2π) and a zero sweep replaced with
  2e‑6 / 2e‑6 (`0x3ec0c6f7a0b5ed8d`) (549–558).
* Arrow on each arc = `has_arrow ? DAT_1813d61fa : none` (local_res18/20, core_dimlayout.c 848–858).
* Pigtail (`df_ang_add_pigtail`) and the arc-split branch table (`DAT_181371d6a/6c/6f/70`)
  in FUN_1800206f0 lines 330–550: decoded from asm 2026-10-27 (decomp/draft/ang_asm_1800206f0.s, port viewer/draft_create_ang.c).

### 4.5 Ordinate, point, perpendicular, tangent, baseline/chain, labels

`calc_point_dimension` (2133),
(Angular's layout FUN_1800206f0 incl. the pigtail / arc-split table is decoded and ported since 2026-10-27: viewer/draft_create_ang.c,
create_port.md 5. Baseline / chain = ordinary linear records from BaselineChainProc, create.md 9.)
`calc_ordinate_dimension` (core_calcdim.c 1234), `calc_point_dimension` (2133),
`calc_perp_dimension` (2072 → calc_linear_dimension), `calc_tangent_dimension` (2379 →
calc_linear_dimension), `BaselineChainProc`, `CalcLabel` (core_dimlayout.c 1036),
`AddLeaders` (core_calcdim.c 203): decompiled, **NOT DECODED**.

**Ordinate — calc_ordinate_dimension @18024d3d0** (core_calcdim.c 1234; its arguments / stores from the disassembly
decomp/draft/ord_core_calc_asm.txt; ported as viewer/draft_ord.c, oracle 102 / 102 on the 2017 / 2018 records, create.md 8.6).
Arguments: ent, aPoint A, text point T (world), p4, p5, p6 (world), view, depth, type (DfOrdType: 0 child, 1 zero, 2 orphan),
&orientation, has-base, base ent, &ok. Every 2-D point is the view x / y (Point2Plane: z = the depth).
1. **The zero line L and the text-row origin P6.** With a base: L = the base's leader 1 points 0 → 1 (stored view points), view /
   depth = the base's (+0x328, the text z); P6 = the base text's start: its slot point moved back by half the first string's extents
   and out by the box half sizes (box_limits of the base text, as FUN_18023dad0), then by −/+bx along the text (h_txt_aln 0 / 2;
   orientation 0: y, 1: x, 2: (cos, sin)·bx); the **orientation from the base text angle** (π/2 → 0, 0 → 1, else 2). Without: L =
   p4 → p5, P6 = p6.
2. **The value**: orientation 0: A.x − L0.x (text angle π/2); 1: A.y − L0.y (angle 0); 2: dist_pt_l(A, L), negative when A is on the
   (dy, −dx) side, angle = atan2 of L's direction taken left to right. `ord_neg_sign` off: |d|. Zero ordinate: 0; orphan: the kept
   +0x100.
3. **The text row** TL through P6: +x (0), +y (1), (dy, −dx) (2). **Snaps** (thr = min(sel_dist, dim_text_height) / 2): a child's text
   point within thr of the line through A parallel to L (l_offset by |d|) is projected onto it (a straight leader); within thr of the
   text row (or auto_alignment on) it is projected onto the row; a zero ordinate's within thr of L (auto_alignment is 0 in its drag)
   is projected onto L. Ctrl held: no row / L snap.
4. **The text**: FUN_18030f410 kind 5 (= linear formatting, no prefix), the text centre T2 = T + W/2 along the text (h_txt_aln 0), −W/2
   (2), T (1).
5. **The leader** (one v8_wit_ldr at +0x4a8, kind 1, arrow DAT_1813d61fa, 4 points; the witnesses and leader 2 off): the row through
   T2 moved towards A by W/2 + H/2 (l_offset; A on the row: A moved 0.01 and retried); **leader style ≠ 0**: that line moved by
   W + H/2 back towards T2 and the projected point Q = T2 + (sin, ±cos)·(Htext/2 + pad) (the leader runs along the text, ending at its
   far end), else Q = T. pt3 = Q projected on that line; pt2 = Q projected on the row moved by a further r = (chr_exp + extra_ln_sp)·H;
   pt1 = A projected on the row moved by a further 2r; pt0 = A, or A + witness_gap towards pt1 when `ord_arrowhd`. **Straight** when
   pt0 and pt3 are equally far (< tol) from L: points pt0, pt3 (npts 2); else the jog pt0 → pt1 → pt2 → pt3.
6. **Record**: type 3, +0x338 = +0x100 = the value, +0x108 = the base's eptr (child) else 0, +0x122 h_txt_aln, +0x124 ord_neg_sign,
   +0x125 ord_arrowhd, +0x123 = 0, the aPoint at +0x68, format / tolerance / scale / gap / extension fields as linear, the text slot at
   T2 with the text angle (FUN_180251460), then FUN_18023d590 case 3: the placement +0x310 from the slot and the offsets +0x360 / +0x368
   = placement − A (view, no rotation). Zero / orphan: p4 / p5 / p6 ← world(pt0), world(pt1 slot), world(T2).
Drawn by draw_dimension's replay (§1.2): leader 1 with its arrow suppressed when +0x125 = 0 (draft.c `force`).

### 4.6 Notes with leaders, standalone leaders / witness lines

* Note leaders are a chain starting at `E+0x100` (next = wl+0x84); count `*(short*)(E+0x1c0)`
  (add_a_leader core_draftwf.c 6394 / core_calcdim.c 4198).
* Kind 1, arrow type `DAT_1813d61fc`, size `note_arrow_head_height/width`
  (InitNoteWL; df_reset_ldr_arrows core_draftwf.c 6357 resets them, or copies
  `E[0x118]`/`E+0x108`/`E+0x110` from the first leader to the others).
* **Shoulder attachment** — `df_adjust_ldrs` (core_draftwf.c 6152), only for note kind 2:
  with the text box from `get_ext`, two candidate pairs of points are made on the left and
  right middle of the box: offset outward by `d1 = −(H·chr_exp + H·extra_ln_sp)` and
  `d2 = −H·extra_ln_sp` (H = note text height `E+0xc0`, `E+0xc8`, `E+0xd8`) via `l_offset`.
  For every leader with more than 2 points, points n−2 and n−1 are replaced by the left pair
  or the right pair depending on whether the arrow tip (rotated by −text angle about the
  box middle) is left or right of the box middle. Exact meaning of get_ext's corner outputs:
  text spec. Shoulder length therefore = `H·chr_exp`, gap to text = `H·extra_ln_sp`.
* `make_wl_single(..., mkEnt=1)` creates a standalone entity type 0x20 with note body
  `[0x68] = (kind != 0)` (0 = witness, 1 = leader), view, `draft_scale`, the wl copied to
  `E+0x100` and its first point to `E+0x70` (core_calcdim.c 2585–2640); drawn by draw_note
  as leaders-only (§1.3). Created via `DfCreateLeader` / `DfCreateWitness`
  (core_calcdim.c 391, 479).

---------------------------------------------------------------------------------------------

## 5. Value formatting (dimension text) — FUN_18030f410 (core_draftwf.c 3985–4592)

(Formatting is shared with the text spec; documented here because it is in the dimension
code path.)

Call: `FUN_18030f410(textPt, kind, fmt, fracUnits, symStyle, …, tolMode, +tol, −tol,
dinFlag, dinChar, …, value*, …, outbuf, scale, ok*)`. `value × scale` is the number
(4071; scale = `E+0x330` draft_scale, angular uses 1.0 — calc_dim_note core_draftwf.c 1215–1216).

* Value-format kind (calc_dim_note core_draftwf.c 1203–1250; linear builder
  core_dimlayout.c 2170–2178): 1 linear, 2 angle, 3 radius, 4/6/11 diameter
  (bitmask 0x850, 4183), 5 (type 3 dims), 7 ordinate point (FUN_180310700).
* If product bit 1 (`GetProdEnableBits()&1`, demo/learning edition) the value is rounded to
  2 significant digits (`two_sig` = `%1.1e` round-trip, core_draftwf.c 6925; 4109–4119).
* Prefix/suffix by `symStyle` = `E+0x7b2`:
  radius: 0 `"R %s"`, 1 `"%s R."`, else none (4161–4180);
  diameter: 0 `"%c %s"` with `%c` = 0xD8 (Ø), 1 `"D %s"`, 2 `"%s Dia."`, 3 `"%s D."`,
  else none (4183–4206).
* Tolerances (`tolMode`): 0 none; 1 plus/minus — if `+tol == −|tol|` magnitudes equal →
  single string `±` (0xB1) + value (4462–4466), DIN mode → `dinChar` + `"%d"` (4469–4470),
  else two strings `+x`/`−y` (sign char `'+'`, `'-'` or `' '`) padded with spaces to equal
  length (4474–4560); 2 limits — two values `v+tol₊`, `v+tol₋` (4134–4147, 4253–4330), with
  radius/diameter prefix the symbol becomes its own line (3 strings, 4343–4370).
* Number formatting `FUN_18030ee50(fmt, digits, v, out, fracDen, leadZ, trailZ, scale)`
  (core_draftwf.c 7066), `fmt` = `E+0x7c0` (coordinate_format):
  * 0 Decimal: CString::Format(`"%.*f"` (DAT_180d27078), digits, |v|) → zero-suppression
    FUN_18030e2d0 (core_calcdim.c 2833): if leading zeros OFF and |v| < 1 − tol and the
    string starts with '0' drop that '0'; if trailing zeros OFF TrimRight('0') then drop a
    final '.'; empty → `"0"`. Then prefix `-` if v < 0 (7095–7100).
  * 1 Scientific: `"%.*e"` (7106).
  * 2 Engineering (FUN_18030f1b0 core_calcdim.c 2985): feet = int((v+tol)/12); `"%d'"` when
    feet ≠ 0 or forced; inches `"%.*lf\""`.
  * 3 Fractional (FUN_18030f2c0 3031): denominator `2 << fracDen`; round to nearest;
    `"%d"` + `' '` + reduced `"%hd/%hd"`.
  * 4 Architectural (FUN_18030e5e0 2873): feet `"%d'"` + fractional inches `"%s\""`.
  * `comma_period`: first '.' → ',' (7111–7116).
* Angles `FUN_18030ec10` (core_draftwf.c 6989), `fmt` from the dimension: 0/4 decimal
  degrees `"%.*f"` + `°` (0xB0) (7024–7029); 1 DMS (FUN_18030f070 core_calcdim.c 2923:
  `%hd°`, `%hd'`, `%hd"`, each rounded with +0.01665 and carried at 60); 2 radians `"%srad"`;
  3 gradians `"%sg"` (deg × 10/9).
* Text extents of every string are measured with `get_ext` against the dim font
  (DAT_1813d5630) — text spec.

---------------------------------------------------------------------------------------------

## 6. Callees not decompiled / not understood

| address | name | status |
|---|---|---|
| 0x180316510 / 0x180316700 / 0x18031a950 / 0x18031b4b0 / 0x18031c9d0 / 0x180317570 / 0x180317e60 / 0x180317440 / 0x18031f170 / 0x1803180a0 / 0x180319360 | CDrawString internals (stroke vs TrueType: `font_attr+0x5c < TRUETYPE_FONT`) | text spec |
| 0x18031d040 | get_ext (text extents/box) | text spec |
| 0x180252790 | sets the slot font_attr (angle) in FUN_180252d60 | not decompiled |
| 0x1802579a0 / 0x1802579d0 | tolerance store / view number helper | not decompiled |
| 0x18024a3c0 | radial layout core | decompiled, NOT DECODED |
| 0x180253a60, 0x180253c10, 0x180253d20, 0x180303b70, 0x180303e20, 0x1803049e0, 0x1803053a0 | regen wrappers calling calc_* | decompiled, not analysed |
| 0x180021e70, 0x180021f00, 0x180022300, 0x1800223c0, 0x180022470, 0x180023440, 0x180023630, 0x180023650, 0x1800206c0, 0x180022d00 | angular helpers (angle normalise, side tests) | partly decompiled, NOT DECODED |
| 0x180251290, 0x18023d590 | post-create validation / assoc | not decompiled |
| 0x180255b80 branch table, 0x18024b250, 0x180257270 | dimension-line halves | decompiled, partially decoded (§4.2) |
| 0x180310700 | point dimension (x,y,z) value string (FUN_18030f410 kind 7) | decompiled (core_calcdim.c 3094), not analysed |
| MFC ordinals 4604, 1623, 8299, 1499, 13818, 5558, 4812, 13822, 1502 | CString Format / GetAt / Mid / =, TrimRight, GetLength, Truncate | semantics inferred from use, ordinal names NOT verified |
| gl_fill_tri / gl_fill_circ / gl_fill_rect, drw_3d_line, drw_3d_arc, drw_polyline | imports from the graphics DLL | not needed for the port |
| UI arrow-style combo → type value | probably mccore.ui.dll (.NET) | NOT DECODED |
