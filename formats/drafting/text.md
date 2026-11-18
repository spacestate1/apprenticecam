# Drafting text rendering (Mastercam 2017, mccore.dll): fonts, font_attr, layout

Scope: everything from `drw_string` down: the text renderer object, `font_attr`, the Mastercam stroke
fonts (`fontN.fnt`), the TrueType path, and character/line layout plus justification. Record layouts are in
`records.md` and dimension geometry is in `draw.md`. Those files are owned by other agents.

Sources (all mccore.dll, Ghidra project `mccore`, decompiled read-only; the files are gitignored):
- `decomp/draft/core_draftwf.c`: `drw_string` @18031a760 and its dimension-side callers.
- `decomp/draft/text1.c`: roots `InitDimFA InitNoteFA DfCheckForTokens FUN_180316510/700/750 FUN_180317570
  FUN_18031c9d0 get_ext`, callee depth 2.
- `decomp/draft/text2.c`: font loader, TrueType list, glyph transforms, callee depth 1.
- `decomp/draft/text3.c`: TrueType glyph and token functions.
- `decomp/draft/text4.c`: font discovery `FUN_180365a90`.
- `decomp/draft/text5.c`: `FUN_18030e3b0` (character remap), `FUN_18031f170`, `FUN_18031e760`.
- The font files were measured with `tools/mcfont_dump.py` (new; it parses the user's own `fontN.fnt` at run time).

Citations take the form `function @address (file:line)`. The line is where the function starts in the named
decomp file, unless a line inside the function is given.

Notation: the **cell** is the 255x255 glyph design square. `u,v` are pen coordinates in cells. `h` is the text
height (`font_attr+0x28`) and `e` is the width factor (`font_attr+0x30`).

---------------------------------------------------------------------------------------------------------
## 0. Call chain

`drw_string(p_3d const& pos, short view, font_attr*, double width, uchar color, uchar mode, uchar* text)`,
@18031a760 (core_draftwf.c:7617):
- `mode == 9` with more than one viewport calls `drw_string_all_views`. That function is **imported from
  MASTERCAM.DLL** (`EXTERNAL:000007d9`, SymList) and is not in mccore: NOT DECODED.
- `mode` 1..3 creates a temporary `CDrawString` (0x150 bytes, `FUN_180316510` @180316510, text1.c:207, which
  stores the mode at obj+0x118). It calls `FUN_180316700` and then the virtual destructor.
- `mode` 0 (and 9 with a single view) uses the static object `DAT_1813d8f18`. The sequence is
  `FUN_18031c9d0` (reset), `FUN_180316700` (draw), then `thunk_FUN_180317570` (flush the queued segments:
  GL lines, `store_ent`, pick, or extents; text1.c:451).
- The renderer modes, from `FUN_180317570` and `FUN_1803199e0` (obj+0x118):
  - 0: draw to the screen.
  - 1: create geometry (`store_ent`; Bezier/NURBS via `put_nurbs_in_db`).
  - 2: pick distance (`dist_to_finite_line`).
  - 3: extents (min/max).
- The `width` double is copied to obj+0x120 in mode 0 and used as the GL line width (`set_gl_width`,
  `FUN_18031a950` text1.c:1330, `FUN_180317570`).
- The dimension caller (core_draftwf.c:7590-7612) first runs `DfCheckForTokens` (section 3.6). It then draws
  the main text with `font_attr` at ent+0x5d8 and position ent+0x638. If ent+0x7b8 > 0 it draws up to two
  extra strings (ent+0x738 and ent+0x761, positions ent+0x708 and ent+0x720) with `font_attr` at ent+0x6a8.
- `FUN_180316700` @180316700 (text1.c:222) dispatches on the font:
  - `fa->font (0x5c) < TRUETYPE_FONT`: the stroke font path, `FUN_18031a950` (text1.c:1180).
  - Otherwise: the TrueType path, `FUN_18031b4b0` (text1.c:1659). If the TrueType font cannot be selected,
    that function sets `fa->font = 0` and falls back to `FUN_18031a950` (text1.c, after the `ReleaseDC` block).
- `get_ext` @18031d040 (text1.c:733) runs the layout (`FUN_180316750`) only, and returns the extents and box
  corners.

---------------------------------------------------------------------------------------------------------
## 1. `struct font_attr` (0x60 bytes)

Field meaning comes from `InitDimFA` @180249c70 (text1.c:3), `InitNoteFA` @18024a120 (text1.c:74), and the
consumers named in the table.

| off | type | global source (dim / note) | meaning and how it renders |
|-----|------|----------------------------|-----------------------------|
| 0x00 | p_2d | 0,0 | Arc centre used only for text paths 6/7 (per-character rotation `atan2(pen - centre)`, `FUN_18031a950` text1.c ~1570). Init sets 0. |
| 0x10 | double | `dim_text_angle` / `note_text_angle` | String angle. Becomes the rotation matrix `FUN_18031e700` (text1.c:4143) through `sin`/`cos` with no conversion, so the stored value is **radians**. |
| 0x18 | double | `dim_text_slant_angle` / `note_text_slant_angle` | Slant (shear about the baseline). Radians: it is zeroed when `|slant| >= 1.570796` (`FUN_18031de00` text1.c:3001). |
| 0x20 | double | `chr_rot` / `note_chr_rot` | Rotation of each character about the cell point (128,104). Radians in `FUN_18031de00`. When it is non-zero the slant is ignored (`FUN_18031ef90`, see 3.2). |
| 0x28 | double | `dim_text_height` / `note_text_height` | Text height `h` = cap height (glyph y 47..255 = 208 units). |
| 0x30 | double | `chr_exp` / `note_chr_exp` | Width factor `e`. One cell of x is `h*e` world units (help: "Character width" when fixed, "Aspect ratio" when proportional). |
| 0x38 | double | `extra_sp` / `note_extra_sp` | Extra character spacing, in cells of x (world = `extra_sp*h*e`; matches help "Extra_Character_Spacing"). |
| 0x40 | double | `extra_ln_sp` (both) | Extra line spacing, in cells of y (world = `extra_ln_sp*h/0.8157`). Help says `h*extra`; the code says cell, see 3.4. |
| 0x48 | double | `alt_height` | Height used after control character 0x0E (alternate font). |
| 0x50 | double | `alt_chr_exp` | Width factor after 0x0E. |
| 0x58 | u16 | bits 0-3 `alt_color`; 4-7 `fill_color`; 8-9 h-align; 10-12 v-align; 13-15 text path | See below. |
| 0x5a | u16 | bit0 `space_sty`/`note_space_sty` (1 = proportional); bit1 `fill`; bits 2-3 mirror (`mirror` for notes, **always 0 for dimensions**) | |
| 0x5c | u8 | `font` / `note_font` | Font id. `< TRUETYPE_FONT` is stroke font N; otherwise TrueType (section 2.2). |
| 0x5d | u8 | `alt_font` | Alternate font id (after 0x0E). |
| 0x5e | u8 | `text_bx` / `note_text_bx` | Text box and line flags (3.5). |
| 0x5f | u8 | 0 | Cleared by both Init functions. Use NOT DECODED. |

Field values of the 0x58 word:
- **h-align** (bits 8-9): 0 Left, 1 Center, 2 Right. Help "Horizontal Alignment"; geometry in `FUN_18031d180`.
- **v-align** (bits 10-12): 0 Top, 1 Cap, 2 Half, 3 Base, 4 Bottom. Help "Vertical_Alignment.htm"; geometry in
  3.3.
- **text path** (bits 13-15): 0 Right, 1 Left, 2 Up, 3 Down (help "Text_Path.htm"). Values 6 and 7 are an
  arc path (per-character rotation about `fa+0x00`, `FUN_18031a950`). The UI does not offer 6/7.
- **mirror** (0x5a bits 2-3): 1 = about the X axis (glyph `y -> 255-y`), 2 = about the Y axis (glyph
  `x -> 255-x`), 3 = both. Help "Note_Text_Mirror"; code `FUN_18031ef90` text2.c:1375.

Dimension defaults forced by `InitDimFA`:
- h-align 1 (Center) and v-align 2 (Half): `uVar5 & 0x9ff | 0x100 | path<<13 | 0x800`, text1.c:52-66.
- mirror 0: `& 0xfff3`.

`InitNoteFA` takes h-align, v-align and mirror from the globals `h_txt_aln`, `v_txt_aln` and `mirror`.

The startup values of the globals (`font`, `note_font`, heights and so on) come from the drafting configuration.
That configuration is NOT DECODED here. Global addresses (SymList): `font` 1813d61eb, `note_font` 1813d61f6,
`alt_font` 1813d61ea, `dim_text_height` 1812d9938, `chr_exp` 1812d9920, `extra_sp` 1812d9930, `extra_ln_sp`
1812d9928, `space_sty` 1812d9940, `text_path` 1813d6248, `text_bx` 1813d621c, `mirror` 1813d5600.

---------------------------------------------------------------------------------------------------------
## 2. Font sources

### 2.1 Mastercam stroke fonts: `<install>\Common\Fonts\fontN.fnt`

**Discovery.** `FUN_180365a90` @180365a90 (text4.c:1):
- `font_prefix` = the application path (`GetApp()->vfunc+0x268(…, 2)`) + `"Common\\Fonts\\"`.
- It probes `"%sfont%d.fnt"` for N = 0..7 (`num_fnts < 8`) and stops at the first missing file or load failure.
- It then sets **`TRUETYPE_FONT = num_fnts`**, which is 8 for a complete install. The static initial byte at
  1812d9865 is also 0x08 (MemDump).
- If no font loads it shows a message box (strings 0x2ee, 0x1543, 0x1413).

**Lazy reload.** `FUN_180366590` @180366590 (text1.c ~6315) builds `"font%hd"` and calls
`add_prefix_suffix(…, font_prefix, ".fnt")` (`DAT_1812dd418` = ".fnt", MemDump).

**Fallback.** `FUN_1803667b0` (text1.c:2434):
- If the main font fails to load, the alternate font is used, and the reverse.
- If both fail, font 0 is used.
- If font 0 fails, nothing is drawn.

**Files on the user's machines** (byte-identical copies, `cmp`):
- `~/mastercam-re/files/font0..7.fnt` (from the 2017 MSI).
- `<windows>/Program Files/mcamX9/common/Fonts/FONT0..7.FNT`.
- `<windows>/mcamx/common/fonts/`.

On a 2017 install the path is `C:\Program Files\mcam2017\Common\Fonts\` (the application path plus the
suffix above). Our app must read these files from the user's install (for example, add `font*.fnt` to the
patterns `setup_from_install.py` extracts). They are never copied into the repo.

| id | file | name in file | glyphs | ops (move / line) |
|----|------|--------------|--------|-------------------|
| 0 | font0.fnt | Stick | 130 | 251 / 2919 |
| 1 | font1.fnt | Roman | 130 | 235 / 6866 |
| 2 | font2.fnt | European | 130 | 234 / 5207 |
| 3 | font3.fnt | Swiss | 130 | 234 / 5195 |
| 4 | font4.fnt | Hartford | 130 | 235 / 4881 |
| 5 | font5.fnt | Old English | 130 | 463 / 8960 |
| 6 | font6.fnt | Palatino | 130 | 233 / 6640 |
| 7 | font7.fnt | Dayville | 130 | 253 / 1810 |

(Measured by `tools/mcfont_dump.py`.)

**File format, version >= 1000.** Reader: `FUN_180368f10` @180368f10 (text2.c:4712). It is reached from
`FUN_180366850` (text2.c:2086) when the version is >= 1000. All values are little-endian.

```
+0   32-byte header, kept verbatim at fontobj+0x40..+0x5f
     u16 version (2000 in all eight files)
     u16 64, u16 32         (legacy-format record size / glyphs per block, see below)
     u16 ?                  (10,12,10,10,9,15,11,5 - meaning NOT DECODED, not read by the renderer)
     u16 first code (0x21), u16 last code (0xff)
     byte 0x0f = DBCS type  (FUN_1803650a0 reads fontobj+0x4f; 4..8 = double-byte, all 8 files: 0)
+32  i32 n, n bytes name   (last byte forced to NUL; stored as CString at fontobj+0x60)
     i32 glyph count
     glyph * count:  u16 char code, i32 k, k * 7-byte command {u8 op, u8 b1..b6}
```

Each glyph becomes a `CTypedPtrArray<fnt_command*>`. Glyphs live in a hash keyed by the code (FNV-1a over the
2 code bytes, `FUN_180360fd0` and the lookup `FUN_180364de0`). All eight files have 0 trailing bytes.

**Legacy format, version < 1000.** `FUN_180366850`:
- Header, then an index of `(last-first+1)` u16 entries (0xffff = absent).
- Glyph data sits in blocks of `hdr[1]*hdr[2]` bytes at `file offset = (entry / hdr[2]) * blocksize +
  2*(nidx) + 0x1e`.
- Inside a block, a glyph starts at `(entry & (hdr[2]-1)) * hdr[1]`. Its commands are variable length:
  ops 0/1/2/8 take 3 bytes, op 3 takes 7 bytes, op 0xf ends the glyph.
- Ops 4, 5, 6, 7 and 9 are logged as `FONTFILE-COM_FILL_BEG / REG_BEG / REG_END / FILL_ST / NURBS` and skipped.
- None of the shipped files use this format.

**Command semantics.** Renderer `FUN_1803199e0` @1803199e0 (text1.c:4603); width lookup `FUN_18031ca90`
(text1.c:6408).

| op | bytes | meaning |
|----|-------|---------|
| 0 | b1 | Advance width 0..255 for proportional spacing (`FUN_18031ca90` returns b1 of the first op 0). Not drawn. |
| 1 | b1,b2 | Move to (x,y). |
| 2 | b1,b2 | Line to (x,y). A segment is emitted only if a current point already exists in this glyph (`bVar6`). |
| 3 | b1..b6 | Cubic Bezier from the pen through control points (b1,b2),(b3,b4) to end point (b5,b6). Flattened by `FUN_1803195d0` / `FUN_180318f50` (text2.c:483), see below. **Unused in all 8 shipped fonts** (op histogram). |
| 8 | - | Toggle the line colour between the entity colour and `fa->alt_color` (0x58 & 0xf). Not in the shipped fonts. |
| 0xf | - | End of glyph. In mode 1 it closes a fill region (`FUN_18031e7e0`). |

Bezier flattening:
- Recursive de Casteljau split at t = 0.5, up to depth 7 (`DAT_1813d9041 > 6`).
- The recursion stops when the chord is at most `obj+0x138`, or when the control-polygon flatness test passes.
  It then emits the three control-polygon segments P0P1, P1P2, P2P3.
- `obj+0x138` = 2 x |screen (0,0) -> (1,1)| in world units (`FUN_18031f0f0`, text1.c:3297).

**Glyph design units** (measured; matching constants in the code):
- The cell is 0..255 in both axes.
- **Baseline y = 47.** Code constant `47.0` (`FUN_18031ef90`) and 47/255 = 0.1843137 (`FUN_18031d180`,
  `FUN_18031e120`).
- **Cap top y = 255.** Cap height 208: `0.8156862745 = 208/255` and `255/208` in `FUN_18031ef90`.
- **x-height y = 193.** Measured: 'x' in every font has bbox y 47..193. Code: 0.7568627 = 193/255 and
  0.2431373 = 62/255 (the "Cap" alignment).
- **Descender bottom y = 0.** Measured: 'g' has y 0..193.
- Letters and digits fill the full x range 0..255 (width byte 255). Narrow glyphs ('i', 'l', '.') are drawn
  centred about x = 128 and carry a smaller width byte (Stick: 'i' 37, 'l' 3, '.' 37).
- Codes present (all eight fonts): 0x21-0x7e and 80 81 84 8e 94 99 9a a1 ae af b0 b1 bf c1 c4 c9 cd d1 d3
  d6 d8 da dc df e1 e4 e5 e9 ed f1 f3 f6 f8 fa fb fc.
- **Space 0x20 has no glyph** (handled in 3.1).

### 2.2 TrueType path (font id >= TRUETYPE_FONT)

- **Font list.** `ttfontlist`, entries of 0x28 bytes, is the session list (`init_ttfontlist` text2.c,
  `FUN_180366f60`). The font id is `TRUETYPE_FONT + index`; the reverse mapping is `FUN_180367e00`
  (text1.c:5535).
- **Registering a font.** Fonts are registered by `get_ttfont_number(name, italic, bold(700/400), charset)`
  (text2.c:1817):
  - If an entry with the same name, italic and weight exists it is reused (`FUN_180367aa0`).
  - Otherwise it builds a LOGFONTA with `lfHeight = -GetDeviceCaps(LOGPIXELSY)` (1 inch em) and charset
    0x80 on Japanese systems (else 1), then calls `CreateFontIndirectA` (`FUN_180366290` text2.c:4508).
  - The display title is `name [+" Italic"][+" Bold"]` (`FUN_1803659a0`).
  - Where a part file stores the TrueType font names is NOT DECODED here. The readers of `TRUETYPE_FONT` that
    are not decoded include `FUN_1802a0a90`, `FUN_1802d1b60`, `FUN_1802d2b90`, `FUN_1802d2f90`,
    `FUN_18030b930`, `FUN_1803660a0` and `FUN_180362eb0` (SymWrites).
- **Glyph source.** `GetGlyphOutlineA(…, GGO_NATIVE=2, MAT2)` through `GetGlyphCurves` @1808b3050
  (text3.c:543), with a per-font cache (`FUN_1808b2e70/2da0/2fc0`, NOT DECODED).
  - For TT_PRIM_LINE / polyline records, `FUN_180318780` (text1.c:5714) draws a polyline. It uses GL
    `glBegin(3)` in mode 0, otherwise `FUN_180317440` segments.
  - For TT_PRIM_QSPLINE (type 2), `FUN_1803183c0` (text1.c:6097) builds a quadratic NURBS (order 3, knots
    0,0,0,1,…,n,n,n). Mode 0 draws it with `drw_nurbs`, or tessellates it in `FUN_180317e60` with step
    `1/ceil(1/sqrt(3.2/(yscale*…)))`. Mode 1 creates it with `put_nurbs_in_db`.
  - A contour is closed with a line back to its start when the last point differs (`FUN_180364a20`).
  - The result is **outline only (wireframe)**. No fill was found on this path; the `fill` bit is not read in
    `FUN_18031b4b0`.
- **Scale.** `find_tt_pixel_scale` (text1.c:6019) with `FUN_1808b2f30` (text3.c:506):
  - `s = h / Xh`, where `Xh` = `gmBlackBoxY` of `'X'` (falling back to `|gmCellIncY|`). In other words, the
    capital X black box equals the text height.
  - x scale = `s*e`.
  - For DBCS characters `s = h / tmHeight`.
- **Point transform.** `FUN_18031f170` (text5.c:58): `x += tan(slant)*y`, then rotate by `chr_rot`, then add
  the pen. Mirror bit 2 negates x and bit 1 negates y. The result goes through the same matrix as the stroke
  path (3.2).
- **Advance.** `find_char_width` (text3.c:1459) uses `GetCharABCWidthsA`, falling back to 'l' (0x6c):
  - proportional: `(A+B+C)*sx`
  - fixed: `B*sx + extra_sp*h`

  (`FUN_18031b4b0` around `find_char_width`).
- **Lines and alignment.** The line pitch is `h*extra_ln_sp + h`. Justification is `FUN_18031db20`
  (text1.c:6213) / `FUN_18031d6f0` (text1.c:5550), using TEXTMETRIC: Cap = `h - descent·s`, Half = block/2,
  Bottom = descent·s.
- **Vertical fonts.** Vertical ('@' face, or path Up/Down) layout uses glyph metrics. It is only partially
  decoded (the `local_8c8` branches).

### 2.3 Which font is the default

- Dimensions use the global `font`; notes use `note_font`. Alternate font segments use `alt_font`.
- Their values come from the drafting configuration, NOT DECODED.
- A TrueType font is used only when the id is >= 8.
- `MapDraftingTextToTrueType` @18030e4a0 (text2.c:77) rewrites the **text bytes** of a stroke-font entity:
  - It applies to ent type 0x40 (all three strings) and type 0x10 (the note via `mTableFindData`).
  - Each byte goes through `FUN_18030e3b0` (text5.c:1), a CP437 to CP1252 table:
    `81→FC 84→E4 8E→C4 94→F6 99→D6 9A→DC E1→DF E5→20 ED→D8 F1→B1 F8→B0 FB→20`. Other bytes are unchanged.
  - The stroke fonts carry glyphs at both code sets. 84/8e/94/99/f8 are identical to their CP1252 partners;
    81/9a/e1/ed/f1 differ (measured).

---------------------------------------------------------------------------------------------------------
## 3. Layout (stroke font path, `FUN_18031a950` @18031a950, text1.c:1180)

### 3.1 Character code and advance

- **Code.** `FUN_180318e80` (text1.c:2974) uses the byte. A two-byte code `0x100 + …` applies only for DBCS
  font types 4..8 with lead-byte ranges from tables at 1812d9858+. Not needed: all shipped fonts are type 0.
- **Width `w`** in cells. `FUN_18031dd60` (text1.c:2470, text2.c:4091):
  - fixed: `w = 1.0`
  - proportional: `w = widthbyte/255`
  - glyph missing in proportional mode: `0.6` for space (0x20), else 1.0
  - codes > 0xff are doubled
- **Missing glyph.** If the code is not in the font, `FUN_18031ca90` returns -1. Codes below 0x21 are not drawn;
  other codes fall back to 0x20, which is also absent, so nothing is drawn.
- **Advance.** `FUN_18031e120` (text1.c:5014) for path Right; Left (and 6) negates:
  - Fixed: the glyph is drawn at pen `u`, then `u += 1 + extra_sp`.
  - Proportional: before drawing, `u -= (1-w)/2`, which centres the 255-wide design cell on the glyph's width.
    After drawing, `u += 1 - (1-w)/2 + extra_sp`. The net advance is `w + extra_sp`.
  - The post-advance is skipped before `'\0'` and `'\n'`.
  - Up/Down paths advance `v += ±(extra_ln_sp + 1 - 47/255)`, with Up = +.
- **Kerning.** None (no pair table exists in the format or in the code).
- **Control bytes:**
  - `'\r'` is skipped.
  - `'\n'` starts a new line (3.4).
  - `0x0E` switches to the alternate font: font `fa+0x5d`, matrix built from `alt_height` and `alt_chr_exp`.
  - `0x0F` switches back (text1.c:1543-1560).

  The pen `u,v` is kept in cells and is not rescaled at the switch, so positions after 0x0E are in alt-scaled
  cells. Whether callers rely on this (tolerance stacks?) is NOT DECODED.

### 3.2 Glyph point to world

Point transform: `FUN_18031ef90` @18031ef90 (text2.c:1375), using sin, cos and 1/cos prepared by
`FUN_18031de00` (text1.c:3001).

```
x' = mirrorY ? 255-x : x ;  y' = mirrorX ? 255-y : y
if |chr_rot| > tol:  (X,Y) = rotate((x'-128, y'-104), chr_rot) + (128,104)      # slant ignored
else:                X = x' + tan(slant) * (y'-47) * 255/(208*e) ;  Y = y'      # shear about baseline
P = (u + X/255, v + Y/255)                                                     # pen + glyph, in cells
world_view = T(pos.xy) * R(angle) * S(h*e, h*255/208) * P                      # FUN_18031ef70 / e700 / f410 / e080
```

- `FUN_1803180a0` applies the 3x3 matrix to the point. `FUN_180317440` drops segments shorter than `tol`, then
  `view_to_world(view)`; z = `pos.z`.
- `mirror` and `slant` are negated before use when any mirror bit is set (`FUN_18031de00`).
- **Path 6/7** (arc): each character uses `chr_rot' = deg(atan2(pen - centre)) - 90 + chr_rot`, but
  `FUN_18031de00` feeds that value straight into `sin`/`cos`. The degree/radian mix is as decompiled; verify
  before porting.
- **Mirror and path.** With mirror Y set and path 0/1, the path is swapped (Right and Left). The decompile shows
  odd results for the other paths (2/3 become 7, and 6/7 with mirror X become 2). Treat those as NOT DECODED.

### 3.3 Start pen (justification): `FUN_18031d180` @18031d180 (text1.c:4226)

Inputs: per-line length `L` = Σ(w+extra_sp) - extra_sp, taking the **maximum over lines**, and the line count
`n`. With path Right and no mirror:

| h-align | start u |
|---------|---------|
| 0 Left | 0 |
| 1 Center | -L/2 |
| 2 Right | -L |

Path Left starts at `u = L - 1` (h-align 0); the other alignments shift that start by the same amounts. With
mirror Y: `u = 3L - u_path` with the factors in text1.c:4340-4365.

| v-align (path Right/Left) | start v | puts at the anchor |
|---------------------------|---------|--------------------|
| 0 Top | -1 | glyph y = 255 (cap top) |
| 1 Cap | -193/255 | y = 193 (x-height, the help's "top of lower-case") |
| 2 Half | ((n-1)·extra_ln_sp + n·208/255)/2 - 1 | middle of the block from the cap top of the first line to the baseline of the last line |
| 3 Base | -47/255 | baseline of the first line |
| 4 Bottom | 0 | y = 0 (descender bottom) |

With mirror X the reference flips (the `uVar14 = -1` branch). The Up/Down paths use the dedicated formulas
in text1.c:4375-4415, with `L = 1` cell wide and the block height `Σ(extra_ln_sp + 1)`.

### 3.4 Multi-line notes

- On `'\n'` (path Right/Left), `v -= (extra_ln_sp + 1) - 47/255`. The pitch is therefore
  `(208/255 + extra_ln_sp)` cells = `h + extra_ln_sp*h*255/208` world units.
- `u` is reset to the **start u of the first line**. Each line starts at the same x, so Center and Right align
  the block by its longest line; individual lines are not re-centred (text1.c:1515-1540).
- For Up/Down, `'\n'` moves `u` by `(maxw + extra_sp)` (the max char width returned by `FUN_18031d180`).
- The extents variant (`FUN_180316750`, text1.c:238) uses the same rules. Height is
  `cell·extra_ln_sp·(n-1) + (n-1)·h + cell` with `cell = h/0.8157`.
- **Dimensions do not use `'\n'`.** Extra lines are separate `drw_string` calls (section 0).

### 3.5 Box, underline, greeking

Box corners come from `FUN_18031cb80` (text1.c:2782) and `FUN_18031e380` (text1.c:3043): p0 = left-bottom,
p1 = right-bottom, p2 = left-top, p3 = right-top. Bottom is the descender line (y=0) and top is the cap
line (y=255). The box is rotated by `angle` about `pos`.

`fa+0x5e` bits (text1.c:1394-1440):

| bit | effect |
|-----|--------|
| 0x01 | p2-p3 (top) |
| 0x08 | p0-p1 (bottom) |
| 0x10 | p1-p3 (right) |
| 0x20 | p0-p2 (left) |
| 0x02 | Line at the x-height (cell y 193) across the box |
| 0x04 | Line at top - h (baseline) across the box |
| 0x40 | Underline at y = 47 per line |
| 0x80 | Overline at y = 255 per line (`FUN_18031a450` text1.c:4475; x from 0 to 255 of the first and last glyph) |

The help names Top, Bottom, Left and Right ("Text_Box_Lines"); the mapping of each help name to a bit is
inferred from the geometry.

**Greeking.** If `greeking_on` is set, or the text is shorter than 6 screen pixels (`uu_to_scrn` of (0,0) and
(0,h)) and `DAT_1812d980a` is set, only the four box lines are drawn and the glyphs are skipped. This is not
done for hardcopy or mode 1 (text1.c:1364-1380, 1430).

**Clipping.** If the box is fully outside the view clip, nothing is drawn (`FUN_18031f440`).

### 3.6 Special characters and tokens

- **Symbols are single CP1252 bytes** looked up directly in the font. The dimension formatter writes them:
  - Ø = 0xD8, with style 0 `"%c %s"`. Styles 1/2/3 are `"D %s"`, `"%s Dia."` and `"%s D."`
    (core_draftwf.c:4185, 4357).
  - ± = 0xB1 (core_draftwf.c:4464).
  - ° = 0xB0 (core_draftwf.c:7027).
- All three glyphs exist in every stroke font (2.1). There is no `%%c` / `<D>` style escape in the renderer.
- **Note token.** `"%v"` in ent type 0x40 text (ent+0x650, active when ent+0x7a0 != 0) is replaced with the
  value from `calc_dim_note` (`DfTxtHasTokens` text3.c:43, `DfReplaceTokens`, `DfCheckForTokens` text1.c:143).
  The value format is NOT DECODED here.
- Help mentions "Add Symbol" (a symbol picker for notes). Which codes it inserts is NOT DECODED.

---------------------------------------------------------------------------------------------------------
## 4. Callees not decoded

| function | address | role (guess, unverified) |
|----------|---------|--------------------------|
| drw_string_all_views | MASTERCAM.DLL import (ordinal 0x7d9) | Draws in every viewport. |
| FUN_18031a720 | 18031a720 | Emits a GL polyline of the queued points. |
| FUN_180317a50 / FUN_180317a80 | 180317a50 / 180317a80 | Segment queue / immediate line. |
| FUN_18031e7e0 | 18031e7e0 | Fill region / create entities (mode 1). |
| FUN_180319360 | 180319360 | NURBS in pick/extents modes. |
| conv_pfx_to_p3d | 1808b3c00 | TrueType FIXED to point conversion (scales). |
| FUN_1808b2e70, FUN_1808b2da0, FUN_1808b2fc0 | | TrueType glyph cache. |
| FUN_180365310 | 180365310 | Gets the HDC. |
| TrueType vertical / '@' layout branches | | Inside `FUN_18031b4b0`. |
| create_tt_letters, calc_tt_spacing | 1808b3c80, 1808b3ab0 | Create-letters command (decompiled in text3.c, not read). |
| FUN_180369480, edit_fnt_file | 180369480, 180364030 | Font writer / editor. |
| Readers of TRUETYPE_FONT and the TT font names in files | 1802a0a90, 1802d1b60, 1802d2b90, 1802d2f90, 18030b930, 1803660a0, 180362eb0, 1801d05f0, 1801cd620, 18018eb60 | |
| Config load of `font`, `note_font`, heights and other text globals | | Default values. |
| DBCS lead-byte tables | 1812d9858.. | |
| Meaning of header u16 @6 | | |
