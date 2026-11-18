# MP post engine (mcpost.dll, Mastercam 2017)

`mp.dll` ("MPXRun") is a thin driver; the MP language interpreter is **mcpost.dll** (MCPost.pdb).
Encrypted posts (`.psb`, `NSC^` header, `CPostDecoder::DeCoder`) are out of scope.

## Pipeline
```
.mcam binary NCI ──bnci_to_string (mccore)──> ASCII .nci ──CPostReadNci::ReadLoop──> postblock calls ──> .NC text
                          field map: formats/mcam.md          rdN(char*) parse each line
```
`CPostReadNci::rd1/rd2/rd11/rd81/rd100/rd1000_proc/...` parse ASCII NCI lines; `AssignReadNci` stores fields into
predefined variables; `CPostRunPostTable::BlockCall(slot)` executes a postblock; `CPostMath::GetVarRef(slot)` reads a variable.

## Predefined symbols (`formats/mp/`)
Extracted by `tools/mp_predef_extract.py` from the four registration functions (pattern-matched in the
disassembly because `PreDefineVar` is too large for the decompiler):

| table | source function | count |
|---|---|---|
| `mp_operators.tsv` | `CPostOperator::PreDefineOperator` @0x1800d2670 | 168 operators + functions (`abs sqrt fmtrnd rpar force ucase ...`) |
| `mp_postblocks.tsv` | `CPostPostBlock::PreDefinePostBlock` @0x1800e1130 | 225 predefined postblocks |
| `mp_variables.tsv` | `CPostVariable::PreDefineVar` @0x18016a940 | 1159 numeric variables |
| `mp_strings.tsv` | `CPostString::PreDefineString` @0x180140210 | 142 string variables |

Columns: name, immediates seen in the registration block (token group / type / flags — e.g. vars 0x3ea positions,
0x3ee ref/home points; postblocks 0xbba..0xbd9 groups), code address, other names (alias like `op_id_mp$`->`op_id$`,
or the default output label like `x$`->"X"), slot global.

**Coverage check:** every `$` name used in 5 generic posts (fanuc 3x/5x mill, haas 4x, fanuc lathe, fanuc wire) resolves
to these tables (1151/1151), excluding `prv_*` which is a label modifier (`CPostStringUtil::ReadLabelModifier`) that
derives "previous value" companions automatically.

`mcpost_labels.tsv` (1653 entries) maps each slot global to `pb_*/var_*/str_*/op_*`; `tools/ghidra_scripts/ApplyLabels.java`
has applied them to the `mc2017` Ghidra project, so decompiled mcpost code shows symbol names
(`$` is rendered as `_`, e.g. `pb_plin_` = `plin$`).

## NCI code -> postblock dispatch (`CPostReadNci::ReadLoop` and helpers)
| NCI | postblocks referenced |
|---|---|
| 0 / 1 linear | `call_case1` -> `plin0$`, then `LinearPostCall`, then `plin_end$` |
| 2 / 3 arc | `call_case2` -> `pcir0$`, `LegacyCircularPostCall` (`mcir$` / `lcir$` / `pcir$`, legacy `pcir1..3$`), `pcir_end$` |
| 4 dwell | `pdwl_spd$` |
| 11 5-axis | `call_case11` -> `pmx$`, `plin0$`; `pmx_end$` |
| 80 / 81 / 100 drill | `pcancelcyc$`; `pcan0$`, `pblockdrl$`, `pdrill0$`, `pdrl_end$`, `pcan_end$` (`CPostProcessDrill`) |
| 201 thread | `pthread0$`, `pthd_end$` |
| 901..905 lathe stock/chuck/tailstock/steady rest | `pstck_flip$ pstck_bar_fd$ pchuck$ ptailstock$ psteadyrest$` |
| 950 | `psynclath$` |
| 999 | `pmiscop_999$`, `pcomment_999$` |
| 1001 start of file | `psof00$`, `pheader$`, `pwrtt$`, `prot0$`, `psof_end$`, `ptlchg1002$` |
| 1000 / 1002 tool change | `ptlchg00$`, `pheader$`, `prot$`, `ptlchg_end$` / `ptlchg0_end$`, `ptlchg1002$` |
| 1003 end of file | `peof00$`, `peof0$`/`meof0$`/`leof0$`, `peof$`/`meof$`/`leof$` |
| 1004 | `pcancelcc$` |
| 1010 | `pconchg$` |
| 1011 / 1012 / 1013 | `pmiscreal$` (+ `ptoolend$`/`mtoolend$`/`ltoolend$`), `pmiscint$`, `pmiscparam$`/`protofs$` |
| 1015 | `psubcall$` |
| 1031 / 1032 | `ptransreal$`, `ptransint$` |
| every record | `pq$` (top of loop), `pstck_trans$` for motion codes |
`rd1000_proc` also calls `prd1000$`.

### Product-specific fallback (`LinearPostCall` @0x1800acd30, same shape for arcs/eof/toolend)
For a linear move (`gcode$ == 1`):
1. mill/router (`product < 2`) and `motst$ == 4` and `z_pstblock$ != 0` -> **`pz$`**
2. mill/router and `mlin$` is defined in the post -> **`mlin$`**
3. lathe (`product == 2`) and `llin$` defined -> **`llin$`**
4. otherwise **`plin$`**

Rapids: `pzrapid$` / `mrapid$` / `lrapid$` / `prapid$` with the same rules. When `whatno$ == 0` the legacy
`whatline$` counter selects `plin1$/plin2$/plin3$` (and `pz1$`) instead.

## Source language (.pst) structure
Top-level keywords (`CPostFunction::Initialize`): `fs fs2 fmt fstrsel flktbl fq fbuf fprmtbl fstack`;
control flow (`CPostBoolean::Initialize`): `if while else`. `tools/pst_scan.py` classifies every line of all 16
bundled posts (~42k lines) with zero unknowns:
| kind | form |
|---|---|
| declaration | `fs2 n ...`, `fmt "X" 2 xabs`, `fstrsel sg00 gcode$ sgcode 5 -1`, `flktbl 1 3` + indented rows, `fprmtbl 17000 4` + rows |
| numeric init | `name : value` (column 0) |
| string init | `sname : "text"` |
| postblock | name at column 0, body = following indented lines (output lists, formulas `a = b + c`, calls, `if`/`else`/`while`) |
| post text | `[CTRL_MILL|DEFAULT]`, `[misc integers]`, `[simple drill]` ... `[CTRL_TEXT_END]` pages with `N. "description"//default` (CPostText) |
The parser (`CPostPostTable::ProcessPostBlock/ProcessFormula/ProcessPostBoolean`, `GetElementType`) builds a linked list
("post table") of tokens; `CPostFormula` assigns parenthesis + precedence levels; `CPostRunPostTable::BlockCall` executes.

## Expression evaluation (`CPostFormulaSolver`)
Precedence passes (`CPostFormula::SetPrecedenceLevels`, operator-id ranges), evaluated first to last:
1. parentheses  2. `^`  3. `* /`  4. `+ -`  5. comparisons `= > < >= <= <>` (one tier)  6. `& |` (one tier)  7. `not` / functions.
Semantics (`SolveFormulaMathOperator`):
- `:`, `:=`, `=` (assignment form) and juxtaposition assign; string targets copy strings.
- `+` concatenates when the target is a string; other arithmetic on strings sets error 0x80000.
- comparisons use an absolute tolerance of **5e-8**; `<>` on strings is a string compare.
- `&` / `|` treat |x| > 5e-8 as true; `/` goes through `CPostMath::fcndiv` (divide-by-zero guarded); `^` via `CPostMath::pwr`.
- assigning to a variable flagged "linked" (flag 0x20) propagates via `linklvar$`, `linktolvar$`, `linkplnvar$`.
Operator/function groups -> solver: 0xfa4 math fn, 0xfa5 trig, 0xfa6 2D vector, 0xfa7 3D vector, 0xfa8 matrix, 0xfa9 string
select, 0xfaa lookup/params (`fsg flook frange finc rpar fprm`), 0xfab buffers, 0xfac variable ops (`force raw vlout`),
0xfad stack/breaks, 0xfae strings, 0xfaf format (`newfs nwadrs`), 0xfb0 files, 0xfb1 launch/dll/vbs, 0xfb2 screen/draw,
0xfb3 system queries (`opinfo sysinfo`), 0xfb4 bitwise, 0xfb5 other. Type word 2 = argc<<12 | result<<8 | 0x10
(result 1 real, 2 vector, 4 string, 8 matrix).

## Number formatting (`fs2`) — re-implemented in `tools/mp_format.py`
`fs2 <n> <inch> <metric> [opts]`, each of `<inch>`/`<metric>` = `<int digits><sep><dec digits>`:
| sep | inch flag | metric flag | meaning |
|---|---|---|---|
| `.` | 0x04 | 0x20 | decimal point |
| `,` | 0x08 | 0x40 | decimal comma |
| `^` | 0x14 | 0xa0 | point only when there is a fraction |
| `%` | 0x18 | 0xc0 | comma only when there is a fraction |
| space (`1 0`) | – | – | integer, no separator |
Options (`CPostFs::ParseFs2`, letters `+SLTNDIZ`): `+` 0x1 plus sign, `s` 0x2 space for positive, `l` 0x100 leading zeros
to int width, `t` 0x200 trailing zeros to dec width, `n` 0x400 non-modal, `i` 0x1000 incremental, `d` 0x2000 delta,
`z` 0x4000 zero after a bare separator. Rounding: `_fcvt_s` to dec digits; `round_opt$/10` in {1,2} switches to
custom digit rounding (`RoundFcvtBuffer`). Zero prints as `0.` with a point format; whole numbers print `2.`; values < 1
print without a leading zero (`.25`) unless `l`. 13 self-tests derived from the decompiled logic + the generic post's
own comments pass — **not yet validated against output from a running Mastercam.**

`fmt "<prefix>" <fs#> <var> ["<suffix>"]` binds a format + address to a variable (`CPostFmt::ParseFmt`,
`PrefixSuffixString`); `DressedFormattedNumberString` assembles prefix + number + suffix, and for incremental
formats (flag 0x1000) outputs current-minus-previous.

## NCI fields -> MP variables (2026-10-14)
The `CPostReadNci::rdN` readers (`decomp/mcpost_rdnci.c`, 116 functions) `sscanf` an ASCII NCI line into a table of
`{double value; int slot}` pairs and call `AssignReadNci` per pair. The slots are bound once by an initialiser
(~0x1801198a0) that loads each variable's slot id from its global and stores it into the table; `tools/mp_nci_binds.py`
tracks those register loads/stores in an objdump of mcpost.dll (`formats/mp/nci_slot_binds.tsv`, 633 bindings) and
`tools/mp_nci_fields.py` joins them with each reader's format: `formats/mp/nci_fields.txt`. Examples:
0/1 `cc x y z fr cflg`; 2/3 `plane cc x y xc yc z fr full_arc_flg cflg`; 1011 `mr1..mr10`; 1012 `mi1..mi10`;
1014 `m1..m9`; 1015 subprogram; 1020 stock; 1050 `vers_no m_vers_no mc_day mc_mon mc_year mc_hour mc_min mc_sec`;
drill 81/100 table: `x y z u v w fr drillcyc drl_depth_xyz dwell frplunge peck1 peck2 peckclr retr ...`; 1016 `op_id
tool_typ tool_op cplnno corgx/y/z cc_computer workofs met_tool n_flutes spindle_no ...`. The tool-change readers
(1000/1001/1002, 22/17/18 fields) share tables between mill/lathe/wire variants and still need pairing with mccore's
writer (`decomp/mccore_bnci_str.c`).

Target post for validation: the user's programs were posted with **Generic Fanuc 3X Mill.pst** (V18 / X9, named in
the part files; `<windows>/Users/Public/Documents/shared mcamx9/mill/Posts/`). Plan: `tools/mp_run.py` = .pst
parser (pst_scan) + predefined symbols + fs2/fmt (mp_format) + output lists / formulas / if-while + NCI dispatch,
checked against BRACKET4.NC with gcode_compare.

## Decompiled sources
`decomp/mcpost_readloop.c` (labelled), `decomp/mcpost_predefine.c`, `decomp/mcpost_format.c`, `decomp/mcpost_numfmt.c`,
`decomp/mcpost_formula.c`, `decomp/mcpost_keywords.c`, string xrefs `decomp/mcpost_strrefs*.tsv`.
