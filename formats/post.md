# Posting stored toolpaths to G-code without Mastercam (`tools/mcam_post.py`, `viewer/nc_post.c`)

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

**The LinuxCNC post** (2026-10-14): `viewer/nc_post.c` is the C port of `mcam_post.py` and the post mcamui uses
(`cam_cli PART post OUT.nc [--style r180|ij-quad]`, mcamui "Post"). It posts every operation in tree order:
Mastercam's from the binary NCI stored in the part, operations made or edited in mcamui from their generated moves
(arcs refitted). Output = the user's proven LinuxCNC 4020 programs: identical to `mcam_post.py` on BRACKET4 (r180),
tramming-rod, buckle-v8 and v10 (ij-quad); BRACKET4 vs Mastercam's posted program 23231/23231 blocks, 1014/1014 arc
words. `G43 H0` is deliberate (LinuxCNC touch-off; the tool table's lengths must not be applied);
`MCAM_H_OFFSET=tool` writes `H<tool>`. The .pst that produced the user's programs is not on disk (the Windows-disk
`Generic Fanuc 3X Mill.pst` is the stock file and writes a different format), so the MP interpreter has no reference
output to be validated against.

```
python3 tools/mcam_post.py PART.mcx-9 OUT.nc [--style r180|ij-quad] [--ops 3,5,9]
python3 tools/gcode_compare.py OUT.nc REFERENCE.nc [-v]
```
`mcam_post.py` reads the binary NCI that Mastercam stores inside the part file and writes 3-axis Fanuc-style G-code
in the format of the user's own post (LinuxCNC router). It does **not** run the `.pst`/MP language. It reproduces
that post's output rules, which were decoded by diffing against programs Mastercam actually posted.

## Mastercam post (.pst) per machine group (opt-in, 2026-10-23; ours: user decisions P1-P4)
**Default = unchanged:** the LinuxCNC post above. Machine Group Properties > Files has a check box "Post with Mastercam post
(.pst) (ours)" (default off, kept in the project file as `GRPMPPOST 1`; script step `grpmppost:1`, MCP `post` option
`mastercam_post`). When it is on, Post runs the machine's own post (`CCNCMachine::PostProcFile`, e.g.
`%MCAMDIR%MILL\POSTS\GENERIC FANUC 3X MILL.PST`) through the MP engine (viewer/mp, formats/mp_engine.md "Integration (app)")
on Mastercam's stored NCI - and, for ops made or edited here, the NCI viewer/nci_write.c writes as Mastercam's writers do
(user decision Q23, formats/nci_writer.md) - and writes one program per NC file name the ops carry, named as Mastercam names it
(Q24: `BRACKET4.NC`, `T.NC`, ... in the part's folder; an existing file of that name is replaced). That program is the stock post's format, **not** the user's LinuxCNC
format (no `G43 H0`: the stock Generic Fanuc 3X writes `G43 H<tool>`, a `%` / `O` header, `M30` without `M2`, ...) - check it
before running it on the 4020. `%MCAMDIR%` = paths.conf `shared` (viewer/paths.h host_path). Line ends `\n` unless the post /
control definition sets omitcrlf$ / use_alt_eob$ / eob$. Mill and router machines only. Drill ops made here (canned-cycle NCI
NOT DECODED), stored ops with a changed tool and ops the NCI conversion cannot write completely are refused (no partial program). cam_cli `post` and
tools/tests/postreg.sh are not affected (LinuxCNC post only).

## Output rules (all verified against real posted programs)
- Ops are emitted in **Operations Manager order** = op entity **+0xe6 (u16)**. Stored NCI order differs.
- Each op's comment (op entity comment) is printed once, at the op's first tool-change record. One op can contain
  several 1002 records (one per depth pass); only the first produces output.
- Tool change: `T# M6` / `G0 G90 G54 X Y` / `M3 S#` / `G43 H0 Z`. Between tools: `M5` / `G91 G28 Z0.` / `M01`.
  End: `M5` / `G91 G28 Z0.` / `G28 X0. Y0.` / `M30` / `M2`.
- Same tool, next op: `S# M3` only when the rpm changes (`r180`), always (`ij-quad`).
- N100 upward in steps of 2, wrapping 9998 → 100. Numbers: 3 decimals, half-up, no trailing zeros (`15.`, `.299`,
  `-1.2`). Feed has 0 decimals (`F250.` from 250.02). Modal X/Y/Z/F/G.
- **Arc words are computed from the already-rounded start and centre** (674/674 R values match; exact values give
  only 523/674).
- **Arc breaking:** `r180` splits arcs into pieces of ≤ 180° (full circles become two halves). `ij-quad` splits at
  every quadrant boundary. Helix pieces always use I/J; Z is interpolated. Full circles = start == end.
- Arcs with chord < 0.01 mm are written as G1 (all 6 such BRACKET4 arcs).
- Drill point (NCI 81: +0x20 X, +0x28 Y, +0x30 depth, +0x40 feed, +0x68 R plane) → `G99 G81 Z R F`, further holes as
  X/Y lines. NCI 80 → `G80`. The user's post omits the G80 (its N number is skipped); this tool prints it, because
  without it LinuxCNC would drill again at the next move.
- Header: `(NAME)`, `(DATE=…)`, `(MCX FILE - …)`, `(MATERIAL - …)` (NCI 20016), and the tool list `( T# | name | H# )`
  (NCI 20001, leading spaces kept).

- Drill cycles: NCI 81 = first point of a cycle (+0x20 x y depth, +0x40 feed, +0x68 R plane), **100 = each further
  point** (+0x20 x y depth, +0x38 R plane, +0x50 feed), 80 = cycle end. (Before 2026-10-14 only 81 was read, so holes
  after the first were missing; buckle-v10's `G81 ... / X102.122` now matches.)

## Validation (user's part files vs the programs Mastercam posted from them)
| part | style | motion blocks matched in order | feeds | arc words | other text differences |
|---|---|---|---|---|---|
| BRACKET4 (23,231 blocks) | r180 | **100 % / 100 %** | 100 % | 1014/1014 | 3 lines: `T3` first-tool quirk, a stock-to-leave note, a hand-written `(cut out)` comment |
| tramming-rod | ij-quad | 99.97 % | 100 % | 94/95 | |
| buckle-v8 / v9 / v10 | ij-quad | 99.1 / 99.2 / 99.0 % | 100 % | ~96 % | |
| buckle-v7 | ij-quad | 97.3 % | 100 % | 97 % | |

Remaining `ij-quad` differences:
- 0.001 mm rounding at split points that Mastercam computes itself;
- ~50 tiny arc pieces (chord ≤ 0.055 mm) that the older post wrote as G1, by a rule not yet identified.

Machining effect: a few microns at most.

Not comparable: buckle-v6/v11/v12, NAKTO and fight-stick. Their posted programs come from an earlier revision of the
file (per-op check: most ops absent, i.e. regenerated after posting).

## Not done
- Coolant (`M7`/`M8`) by default (opt-in `MCAM_COOLANT=1` since batch 11, below), `H#` = tool (buckle-v12-era post; `MCAM_H_OFFSET=tool` gives it), spindle-on-G0-line variants of
  the earlier posts.
- Dwell / tap / bore / misc cycles (NCI 81 cycle 3-7 and custom: the post refuses the op with a message; dwell, Tap and Bore #1
  with `MCAM_DRILL_CYCLES=1`, batch 8 below), 4/5-axis
  (NCI 11, 20-33), lathe, subprograms; cutter compensation from a *stored* NCI (`cc` field: refused, see above). Peck Drill (cycle 1) and Chip Break (cycle 2)
  are written as `G99 G83` / `G99 G73 ... Q<peck>` (nc_post.c; "pchpbrk$ = ppeck$ with sg73" from the stock
  Generic Fanuc 3X Mill.pst, not from the user's post, which is not on disk).

## Cutter compensation in control - G41 / G42 D / G40 (op dialog batch 5, 2026-10-23; opt-in `MCAM_CUTTER_COMP=1`)

**Pocket finish (batch 6):** the Pocket Finishing page's compensation 0x13a8 (0 computer, 1 control, 2 wear, 3 reverse wear) tags
the finish pass the same way (cam2d_pock_fin.c: G41 / G42 on the pass's first move incl. its lead, 140 on its last; Reverse wear
swapped; FUN_1801cd200 l.318636-318656); the roughing stays computer (GeneratePocketToolpath l.313553). The post is unchanged:
without `MCAM_CUTTER_COMP=1` the dialog offers computer only and a pocket carrying codes is refused, as the contour. Control needs a
kept exit lead line (the G40 move, ours).

**Default = unchanged.** Without the switch the Contour page offers Computer / Off only (as before) and the post refuses any
operation that carries compensation codes (an op set to Control / Wear / Reverse wear while the switch was on, opened later
without it). The user's 16 regression parts and every user op hold Computer (126 contour ops) or Off (23): posts 0/16 differ.

**What Mastercam does (decoded, cited in the code):**
- Record 0x5d0 (op +0x568): 0 Computer, 1 Control, 2 Wear, 3 Reverse wear, 4 Off; direction 0x5d2 (0 left, 1 right). mccore
  `cc_in_computer` (0 / 2 / 3: the toolpath is offset by D/2), `cc_in_control` (1 / 2 / 3: G41 / G42 codes), `cc_control_gcode_2`
  (Control / Wear: left 41, right 42; Reverse wear: swapped) - decomp/mccore_cc.c (DumpDecomp on
  ~/mastercam-re/ghidra_core, 2026-10-23). The contour offset (FUN_18005b340, decomp/mcmill_ctour.c l.1643-1648) adds D/2 only for
  cc_in_computer: **Control** = the toolpath on the chain (plus wall stock / multipass spacings), **Wear / Reverse wear** = the
  normal computer path *and* G41 / G42 (the control applies only the wear amount).
- The NCI writer FUN_180056be0 (decomp/mcmill_all.c l.59875-59890): element 2 of each pass (the first move after the start
  point, i.e. the lead-in line) gets cc 41 / 42, the pass's last element cc 140 (off); `stop_cc` then writes NCI 1004.
- The stock Generic Fanuc 3X Mill.pst (`pccdia`, l.959-963): `G41` / `G42` / `G40` on the motion block (after the G code),
  `D<tloffno$>` = the tool's diameter offset number (op record 0x370 = TL +0x28), forced when the comp state changes.

**Decisions batch dec2 (2026-10-24):** Q29 - on this LinuxCNC post the switch allows **Control only**: an op set to Wear / Reverse
wear (Contour 0x5d0, Pocket finish 0x13a8, Slot / Circle 0x5d0) is refused with a message (its path is already offset by D/2 and
LinuxCNC would add the table radius again). The dialog still offers them (Mastercam does; a .pst post may handle them). Q30 - the
D word is `D<tool number>` (LinuxCNC's D selects a tool; Mastercam's tloffno$ = TL +0x28 equals the tool number in all 205 user
ops, so nothing posted changes). Q32 - stored Mastercam toolpaths with cc codes stay refused (below).

**This post with `MCAM_CUTTER_COMP=1`:** `G1 G41 D<n> X.. Y..` on the first move of the pass that goes somewhere in XY after the
plunge (our lead-in repeats its start point: Mastercam's element 2), `G1 G40 X.. Y..` on the pass's last move; both always
lines (the arc refit never swallows them). D = the op's tool number (dec2 Q30; before: the tool's diameter offset number TL +0x28,
205 / 205 user ops: = the tool number). Mastercam's stored toolpaths with cc codes (NCI G0 / G1 +0x18, G2 / G3 +0x1a) are refused (ours: posting them from the
stored NCI is not ported; before batch 5 such an op would have been posted on the chain without G41 / G42 - none of the
user's parts has one).

**How it meets the user's LinuxCNC 4020 setup** (<posts>/4020-probe: `TOOL_TABLE = tool.tbl`, tool.tbl
`T1 D1, T2 D6, T3 D3 (Z-9.09), T4 D3 (3mm chamfer), T5 D7 ("7mm-rough"), T6 D6, T8 D3, T10 D4, T11 D3`; LinuxCNC 2.9
src/emc/rs274ngc/interp_convert.cc):
- LinuxCNC's D word is a **tool number**: `G41 D5` uses tool 5's table *diameter* / 2 (convert_cutter_compensation_on
  l.2012-2023; no D = the loaded tool; an unknown tool number is an error). Mastercam's D (diameter offset number) equals the
  tool number in every user op, so `D<n>` = the table entry of the same tool. G43 H0 is untouched (length comp still from the
  touch-off; the H word and D word are independent).
- **Control** comp: the program is on the part outline and LinuxCNC offsets by the table radius - the table diameter must be
  the real cutter diameter. Note T5: table D7 for a tool named "7mm-rough"; if that is a 6 mm cutter used to leave 0.5 mm stock
  (a LinuxCNC trick), Control comp on T5 leaves that stock. The user's FACEPLATE1.NC (2024) shows a hand-edited `G41` without D
  on its own block before G2 arcs - not this post's format.
- **Wear / Reverse wear**: the program is already offset by D/2 and LinuxCNC adds the *table* radius again - with the current
  tool.tbl (full diameters) **the radius is applied twice (overcut by D/2)**. LinuxCNC's own docs say the table diameter must be
  "a small number ... the difference between the measured diameter of the tool and the nominal diameter" for nominal-path
  programs. Use Wear only with tool entries holding wear values. (Question for the user, formats/opdlg_port_audit.md 9.)
- Entry: LinuxCNC requires the G41 / G42 move to be longer than the tool radius (straight: "Length of cutter compensation
  entry move is not greater than the tool radius", l.5228; arc entry likewise, l.676): the post refuses an op whose first move
  is not (lengthen the entry line). Exit: after G40 the next move must be straight (l.382, `arc_not_allowed`): the G40 move is
  always a line here. Concave corners / arcs smaller than the tool radius give LinuxCNC gouge errors - what Mastercam's
  "Optimize cutter comp in control" (0x5d4, now ported for Control) removes: concave arcs / gaps the tool cannot reach become
  sharp corners (FUN_180051990 l.56085-56098).
- G41 / G42 work in G17 (and G18) only - this post writes G17 only.

## Facing "Feed rate between cuts" (op dialog batch 7, 2026-10-23; opt-in `MCAM_FACE_LINK_FEED=1`)
Mastercam's Facing (bnci_face 0x180097230, formats/mcmill_2d_engines.md 8) feeds the Zigzag links (High speed loops / Linear)
at record 0x12a0 when 0x1298 is set, else at the feed rate. That changes the links' F word, so this program honours it only
with `MCAM_FACE_LINK_FEED=1` (ours, viewer/cam2d_face.c `cam_face_link_feed_on`); without the switch the links take the feed
rate, as with the box cleared, and the dialog's 1846 / 1710 are greyed with an "(ours)" note. No user op holds 0x1298 = 1;
postreg with the switch: 0/16 (run_opdlg_b7).

## Slot Mill compensation in control (batch 7; `MCAM_CUTTER_COMP=1`)
FUN_180218410 puts cc_control_gcode (41 / 42; Reverse wear swapped) on the finish passes' second record and 140 on their last
(the ramp and rough loops get none, formats/mcmill_2d_engines.md 1.3); with the switch the Slot Mill page offers Control / Wear /
Reverse Wear and viewer/cam2d_ops.c slot_chain tags those moves (Control: the finish passes on the boundary + stock). Without the
switch: today's output (Control refused, Wear / Reverse wear cut as computer, no codes). That second record is the entry arc when
"Perpendicular entry" (0x1261) is off and the sweep is > 0: the post refuses a G41 / G42 / G40 on an arc (LinuxCNC turns comp on /
off on straight moves only; before batch 7 a code on an engine arc would have been dropped silently - now refused).

## Thread Mill compensation in control (threadmill 2026-10-26; `MCAM_CUTTER_COMP=1`)
FUN_180236810 puts 41 / 42 (side byte of FUN_1802374d0: ID !cw, OD cw; 0 -> 42, 1 -> 41; Reverse wear swapped) on the last entry line
and 140 on the first exit line; without entry / exit lines they land on the arcs (formats/mcmill_2d_engines.md 4.4). With the switch the
Cut page 2824 offers Control / Wear / Reverse Wear and cam2d_ops.c tm_pass tags those moves (radius without the tool offset for
Control). This post refuses a code on an arc (the default ID op with Start at center and a 5 mm arc clearance on a small hole: the entry
starts at the centre with no line) and a G41 / G42 line not longer than the tool radius. Without the switch: Control refused, Wear /
Reverse wear cut as computer, no codes (as before).

## Drill cycles: dwell, Tap, Bore #1 (op dialog batch 8, 2026-10-23; opt-in `MCAM_DRILL_CYCLES=1`)
**Default:** G81 / G83 Q / G73 Q only; cycles 3-7 and custom are refused; since decisions batch dec1 (formats/user_decisions.md
B8-2) a drill op **with a dwell** (NCI 81 +0x38 / record 0x1250 != 0) is **refused** with a message naming the switch (before: G81 /
G83 / G73 written without the dwell). No user op has a dwell (postreg 0/16 unchanged). Every user drill
op is cycle 0 with dwell 0, so the switch changes none of the user's programs either (postreg with the switch 0/16).

**With the switch** (the Generic Fanuc 3X Mill.pst drill blocks, l.1071-1247; the user's own .pst is not on disk, so these are the stock
post's words in this post's format):
- Drill/Counterbore with a dwell: `G99 G82 X.. Y.. Z.. R.. P<dwell> F..` (drlgsel = fsg2(dwell$) + drillcyc$ x 2 -> sg81d; pdrill$ writes
  dwell$ with "P" fs2 11 = 3 decimals). LinuxCNC 2.9: G82 needs P (interp_cycles.cc NCE_DWELL_TIME_P_WORD_MISSING_WITH_G82) - written.
- Tap (cycle 3): a `G94` block, `G99 G84 ... R.. F<pitch x rpm>` (G74 when the spindle runs CCW: fsg1(-ss$) -> sg84d), the tool's
  pitch converted to the program's units (ptap$'s four met_tool$ / toolismetric branches), F with fs2 17 (inch 2 / metric 3
  decimals); after G80 a `G94` block again (pcanceldc$, tap_feedtype 1) and the next F is written again. LinuxCNC 2.9 G84 / G74 =
  floating-holder tapping: it reverses the spindle at the bottom and refuses G84 with a CCW spindle (check_g74_g84_spindle). A tool
  without a pitch is refused.
- Bore #1 (cycle 4): `G85` (no dwell) / `G89 ... P<dwell>` (sg85 / sg85d). LinuxCNC: G85 feed in / feed out; G89 needs P (written).
- Refused even with the switch: Bore #2 (pbore2$ writes G86 without P - LinuxCNC refuses G86 without P), Fine Bore / Misc #1 (the post
  writes G76, LinuxCNC's G76 is a lathe threading cycle), Rigid Tapping / Misc #2 (M29 - not in LinuxCNC), custom cycles 8-19.
- Dwell / bore / tap read from a stored NCI 81 record (+0x18 cycle, +0x38 dwell) as well as from ops made here (CamParams.drill_dwell).
- The dialog: Dwell is live only with the switch; the cycle combo offers Tap / Bore #1 only with it.

## Arc filter: Mastercam's Line/Arc filter (op dialog batch 10, 2026-10-23; opt-in `MCAM_ARC_FILTER_PLANES=1`; `MCAM_ARC_FILTER=1` no longer needed, dec2)
The Arc Filter / Tolerance page (mccoreres 10318) turns on Mastercam's Line/Arc filter (record 0x33a): after the engine, the op's
moves go through the filter (viewer/cam_filter.c, a port of mccore CFilter.cpp: bnci_filter / FUN_180948750 / FitArc FUN_18093ab00;
formats/opdlg_port_audit.md 14) and are posted as it leaves them (arcs as G2 / G3, the rest as G1). This program's own arc fitting of
chords (`CamParams.arcs`, "Create arcs in XY", ours) is not applied to a filtered op.
**Stored flag honoured (decisions batch dec2, Q6):** a stored op with the filter on (record 0x33a, and 0x11e8 == 0, as
BNCISection::PostProcess) is filtered when it is regenerated, without an edit of the page (before dec2: only after an edit or
with `MCAM_ARC_FILTER=1`, which is now unnecessary and ignored). No user op has the filter on (205 / 205), so fileop 0/189 after
the Dynamic rebaseline and post 0/16 are unchanged; corpus ops with it on (formats/opdlg_port_audit.md 14) now regenerate filtered.
- XY arcs only by default. `MCAM_ARC_FILTER_PLANES=1` lets the filter fit XZ / YZ arcs too (the page's XZ (G18) / YZ (G19) boxes, live
  only then): posted as `G18 G2/G3 X.. Z.. R..` / `G19 ...` and `G17` again on the next block (the pst's sgplane, fstrsel sg17 plane$;
  the G17 restore is ours). The XZ direction word is the NCI's: mccore writes plane-2 arcs reversed against the (x, z) frame
  (FUN_180944830 / FUN_18094ce10) - confirmed on STEEPSHALLOW.mcx-9 op 1 (3910 XZ arcs all within the filter's 180.2 deg limit only in
  that reading). That is ISO / LinuxCNC G18 (CW seen from +Y). LinuxCNC: G2 / G3 in G18 / G19 work (R or I K / J K); cutter
  compensation is G17 / G18 only.
- Stored XZ / YZ arcs (decisions batch dec2, Q9; before: read with depth +y and the NCI code as the (x, z)-frame direction, and
  posted as G17 arcs - wrong): a stored plane-2 arc is read in view 2 as mccore does (FUN_180944830: depth +0x30 = -y by
  view_to_world, G2 / G3 swapped against the (x, z) frame), so the backplot / verify path (nc_motions) and tools/mcam_ops.py
  (oracle) draw it where Mastercam cuts. Posting: with `MCAM_ARC_FILTER_PLANES=1` as `G18` / `G19` (the XZ code is the NCI's, as
  the filter's own XZ arcs); **without the switch an op with XZ / YZ arcs is refused** with a message naming the switch (Q7: G18 /
  G19 output stays opt-in; no decoded linearisation exists to fall back on). No user op has XZ / YZ arcs (post 0/16); corpus 3D
  ops do (STEEPSHALLOW / PARALLELSTEEP).

## Coolant (op dialog batch 11, 2026-10-24; opt-in `MCAM_COOLANT=1`)

**Default = unchanged**: without the switch the LinuxCNC post writes no coolant words (as before). Every operation of the user's 16
regression parts has its coolant off (X style: no coolant entries in the canned text array; V9 style: word 8), so the posts are
byte-identical with the switch on too (postreg 0/16 with `MCAM_COOLANT=1`).

**What Mastercam passes** (viewer/cam_postvals.c, formats/opdlg_port_audit.md 17): the machine definition's UseCoolantFromPost
(CCNCMachine coolant block, part record +0x228 + 0x669) picks the style. X style (BRACKET4, A001, the buckles, NAKTO, bracket4-dashed):
the Coolant page writes coolant entries into the op's canned text array (record 0xb52, 20 x s16; entry = (position + 3) x -1000 -
(101 + 2k) for option k On, - (102 + 2k) Off; position 0 before / 1 with / 2 after), which bnci_wr_sof copies into the NCI 1025 of
each section start. V9 style (test-cube-10mm, thread-test, tramming-rod, and the Mill_MM / Mill_Inch defaults' machine): the op's coolant
word (record 0x398: 0x10 flood, 0x20 mist, 0x40 thru, 8 off) goes into each tool change (NCI 1000-1002 +0x48).

**With `MCAM_COOLANT=1`** the placement is the stock Generic Fanuc 3X Mill.pst's (the post the parts name; shared Mcam2017 mill/Posts):
- X style: "before" entries = own blocks ahead of `T M6` (same-tool op: ahead of its spindle / first move); "with" on entries appended
  to the `G43 H0 Z` line (same-tool op: an own block before its first rapid); "after" = own blocks after the `G43` line (same-tool op:
  after its first rapid); an on code is skipped while that coolant is on, an off code is written only while some coolant is on;
  at each tool change and at the end the off code of every coolant still on comes before `M5` (pretract). Codes = the post's X table
  scool50-53: Flood `M8` / `M9`, Mist `M7` / `M9` (LinuxCNC's M7 mist, M8 flood, M9 off).
- V9 style: `M8` (flood) / `M7` (mist) on the `G43` line, `M9` on the following `G91 G28 Z0.` line (pretract's scoolant); a same-tool
  op that turns it off: an own `M9` block.
- Refused (message): an op that turns on thru-tool or a custom coolant (the stock post's M88 / M89 / "M8(CoolantN=ON)" are not
  LinuxCNC codes).
- Ours: V9 mist is written M7 (the stock V9 table sm08_1 writes M8 for every option); V9 off ops get no `M9` on their `G43` line;
  a "with" off entry is its own block before the `G43` line; non-coolant canned text (Stop, Ostop, Block delete...) is still not posted.
- Checked against the MP engine: BRACKET4 op 1 with Flood On / With, posted with its own Generic Fanuc 3X Mill.pst through the
  app's Mastercam-post path, gives `G43 H12 Z15. M8` and `M9` ahead of `M5` - the same places as this post (`G43 H0 Z15. M8`).

**Mastercam post (.pst) path**: the values reach mcpost as Mastercam passes them - a stored op whose dialog changed only post values
keeps its toolpath and gets its section-start NCI records rewritten from the record (1011 misc reals, 1012 misc integers, 1025 canned
text / X coolant, 1000-1002 +0x48 V9 coolant; cui_ops.c pp_ok_postonly -> cam_postvals.c pv_nci_rewrite). Ops made here still have no
NCI (refused, as before).

## Force tool change (op dialog batch 12, 2026-10-24; opt-in `MCAM_FORCE_TOOL_CHANGE=1`)

**Default = unchanged**: without the switch an operation that uses the same tool number as the one before it gets no tool change
(only its comment, spindle and first move, as before) whatever its Tool page "Force tool change" says.

**What Mastercam does**: Force tool change 25196 = op +0x289 = record 0x2f1 (ToolPageBase::MapToOp 0x180886e20). The Post command's
pre-pass AssignToolChanges (mcmill FUN_180037ca0; ported for the MP path as viewer/mp/nci_ascii.c assign_tool_changes, formats/mp_engine.md
"CBnciPost pre-pass") gives an op that starts with the previous op's tool number gcode 1002 (full tool change) when its +0x289 is set
(not for op_is_notool / wire ops), else 1000 (null tool change). The stock post (Generic Fanuc 3X Mill.pst) writes a 1002 through
ptlchg$ / pretract - the same blocks as a change to another tool.

**With `MCAM_FORCE_TOOL_CHANGE=1`** (viewer/nc_post.c op_force_tc): such an op gets the full tool change block of this post -
coolant off (MCAM_COOLANT=1 only), `M5`, `G91 G28 Z0.`, `M01`, then `T<n> M6`, `G0 G90 G54 X Y`, `M3 S`, `G43 H0 Z` (**H0 stays**; the
same block as for another tool). The first op of the program is a tool change anyway. The Mastercam post (.pst) path already follows
AssignToolChanges (unchanged). postreg with the switch: 0/16 differ (no op of the user's parts forces a tool change that is not one).

## Code: viewer/nc_post.c / nc_post.h
Port of `tools/mcam_post.py`; not an MP interpreter.
- `NcPostOpts` (style "r180" / "ij-quad", program name, per-op filter); `nc_post_part` posts F.ops in order. Event list
  `Ev` (rapid / feed / CW / CCW / drill 81 / drill end 80 / tool change 1000, plane, feed, R plane, peck, cycle).
- Event sources per op: an op made or edited here -> `camop_events` from its generated moves; an op reversed here
  (`Op.nci`, nci_reverse.c) -> `stored_events` on that rewritten section; otherwise `stored_events` on the part's stored
  NCI (re-read from the file at the offsets found on load). `stored_events` reads 20001 (tool names), 20016 (material),
  1000-1002 tool change (tool +0x24, rpm +0x38), 0-3 motion, within the op's -2 .. -3 section, 81 / 100 drill points (bnci_wr_81 / bnci_wr_100, mcmill_2d_engines.md 5.3-5.4:
  cycle +0x18, first peck +0x48).
- `camop_events`: engine arc runs tagged by cam2d.c `mb_tag_arc` (Slot / Helix Bore / Circle Mill / Thread Mill, mcce
  'A' records, pocket gt arcs) become one arc event with the record's own centre (improvisation_review.md CAM item 18);
  other feed runs are refitted by `fit_arc` (longest run on one circle within max(0.003, 1.2 x op tol), <= 45 deg per
  segment, one direction, Z linear along the sweep) and flagged `fitted`; drill ops give one 81 event per hole (cycle
  and peck from CamParams) then 80 and a rapid to the clearance.
- Refusals (never a partial program): an op whose tool changed after Mastercam computed it (`Op.stale`), an op with no
  toolpath, non-finite / > 1e6 coordinates, feeds or R planes, spindle 0, a cutting move without a feed, a drill cycle
  other than 0 / 1 / 2.
- Output: header `(NAME)` / `(DATE=...)` / `(MCX FILE - ...)` / `(MATERIAL - ...)` / tool list, then `G20` or `G21`
  and `G0 G17 G40 G49 G80 G90`; units from the stored toolpath (never guessed); inch parts 4 coordinate / 2 feed
  decimals, metric 3 / 0. Spindle `M4` for a negative rpm. Arc pieces: `ij-quad` at quadrant boundaries; `r180` at
  most 180 deg, refitted arcs at most 120 deg (R-word error near 180 deg); the tiny-arc limit 0.01 mm is scaled for
  inch parts (0.01 / 25.4 in; improvisation_review.md CAM small items part 1 (b)).
- `nc_motions` / `nc_motion_point`: the same events as a motion list for the backplot (formats/backplot.md).
- Regression script: `tools/tests/postreg.sh OUTDIR [BASE]` (16 user parts posted with cam_cli, diffed against
  ~/mastercam-re/regress/post_base ignoring DATE lines; expected 0/16 differ).
- The post never rewrites toolpaths: it can only post what Mastercam already computed and saved in the file.
