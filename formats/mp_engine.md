# MP post engine port (mcpost.dll -> viewer/mp/)

Goal: run real Mastercam `.pst` posts (MP language) on apprenticeCAM parts. `viewer/nc_post.c` (LinuxCNC) stays the
default post; this engine is an alternative selected per machine (see "Integration" below).
Background / earlier groundwork: `formats/mp.md`, `formats/mp/`.

## STATUS / HANDOFF (keep current)
- 2026-10-23 start. Backup: `~/mastercam-re/backups/pre_mp_engine_2026-10-23.tgz`.
- Ghidra: mcpost.dll + mp.dll + mcwire.dll are already in `~/mastercam-re/ghidra mc2017` (labelled with
  `formats/mp/mcpost_labels.tsv`); no separate ghidra_post project was needed. Full dump:
  `~/mastercam-re/work/mp/mcpost_all.c` (all functions), symbols `~/mastercam-re/work/mp/mcpost_syms.tsv`.
- **Status at end of 2026-10-23 session** (details in the module sections below):
  - Oracle (a) mc-test01.mcam + MPFAN.pst vs Mastercam's Test01.NC: 316/316 lines identical except the date line and the
    two path lines (raw part -> nci_ascii -> interpreter, no patches).
  - Oracle (c) user parts + Generic Fanuc 3X Mill vs <posts>/gcode (custom post): motion blocks 100% for BRACKET4,
    test-cube-10mm, buckle-v8/v9/v9-back/v10/v10-back; buckle-v7 97.99% (Z depths / rapids differ in the stored NCI);
    buckle-v6/v11/v12/NAKTO 0-28% (reference NC posted from different toolpaths). Full-file BRACKET4: 36 of 23272 lines
    differ, all custom-post traits (no `%`/`O` line, `M3 S800` on its own line, `G43 H0`, trailing `M2`, `T3` tool call).
  - Sweep (c): tools/mp_sweep.sh, 37 stock posts x 18 parts = 666 runs: 621 clean, 37 part without ops, 8 = Agie wire post on
    mill NCI (selector out of range, Mastercam raises the same), 0 crashes. 5X posts parse only partly: their logic is in the
    encrypted .psb (out of scope).
  - tools/mp_oracle_parts.sh reads nci_dump's NCI as written (2026-10-23: the test patch tools/mp_nci_fix_tlchg.py was dropped
    and deleted - since nci_ascii ports CBnciPost::AssignToolChanges it renamed the first real 1002 to 1001: NAKTO's program
    changed by 440 lines, 15718 vs 15717 motion blocks; every other oracle line is unchanged). Run user parts with
    `--metric 1` (CLI default is inch).
  - **Integrated in the app (2026-10-23, section "Integration (app)" below):** the machine group's "Post with Mastercam post
    (.pst)" (ours, default off) posts through this engine; `%MCAMDIR%` = paths.conf `shared` (viewer/paths.h host_path).
  - **NCI writer (2026-10-24, branch nciw, formats/nci_writer.md):** ops made / edited here are posted through the .pst too
    (Q23; drill canned cycles still refused); one NC file per op NC name, Mastercam's names (Q24).
  - Biggest NOT DECODED: CPostRunFiveAxis / 5-axis output, lathe NCI records + lathe canned cycles, wire, per-level sub files of
    transform modes 3/4, mapped axis combos, UI/OS functions (launch, dll, runvbs, db_*, info queries), Post-dialog options
    (post_settings +0xb/+0xc/+0xd), mastercam.dll TokenToPath (the real %MCAMDIR% expansion; ours: paths.conf shared, P1).
- Work split (2026-10-23): ASCII NCI port (`viewer/mp/nci_ascii.c`), MD/CD/group/op parameters (`viewer/mp/mp_prm.c`),
  number formatting (`viewer/mp/mp_fmt.c`, contract `mp_fmt.h`), interpreter + NCI loop + CLI (`viewer/mp/mp_*.c`,
  contract `viewer/mp/mp.h`). Each part appends its own section at the end of this file.
- Build: `make -C viewer/mp` (own Makefile; links viewer/*.c read-only into viewer/mp/obj/) for the CLIs. The app compiles
  the engine into its core library since 2026-10-23 (viewer-qt/CMakeLists.txt `MP_C`, viewer/Makefile `core` `CORE_MP`), so
  `./build.sh` builds it too; `mp_cli` etc. still need `make -C viewer/mp`.

## Architecture (Mastercam 2017)
```
part (.mcam / .mcx-9)                         mccore bnci_to_nci @180088920 -> bnci_to_string @180089b70
  binary NCI per op  ─────────────────────────> ASCII .nci  (pairs: gcode line / parameter line; mcwire nci_str_w*)
machine def + control def + group (in the part) ─> CPostRunMCamInfo rd_md$/rd_cd$/rd_tlpathgrp$ -> pmachineinfo$
                                                   (prmcode$ + sparameter$; mcmpparameters PrmMachine/PrmControl/PrmGroup)
.pst ── CPostFindVars::Process (line classes, declarations) ── CPostBuildTable::Process (post table) ──┐
                                                                                                       v
mp.dll MPXRun ─> mcpost CPostRun::Run @180111a50 -> RunTraditional @180112130:
   Read1050NciHeader, file-name strings, BuildEnhancedToolTable (pre-read, pwrtt$), CD/MD overrides of post variables,
   pprep$, OpenNcFile, CPostReadNci::ReadLoop @180101990 (per NCI record: rdN -> AssignReadNci -> predefined
   postblock dispatch via CPostRunPostTable::BlockCall @180129c70), CloseFiles, ppost$
```
Porting map (C file -> mcpost classes): `mp_parse*.c` <- CPostFindVars / CPostBuildTable / CPostPostTable / CPostStringUtil /
CPostFs.. CPostFq; `mp_run*.c` <- CPostRun* / CPostOutput / CPostFormulaSolver / CPostMath / CPostRunBuffers / CPostStreams;
`mp_nciloop*.c` <- CPostReadNci / CPostRunNciUtil / CPostGcode* / CPostArc / CPostProcess* / CPostNciPreProcess;
`mp_fmt.c` <- CPostFs / CPostMath::FormattedNumber*; `mp_prm.c` <- CPostRunMCamInfo / CPostMachineInfo / CPostInitPreDefs
(+ mcmpparameters.dll); `nci_ascii.c` <- mccore bnci_to_nci / bnci_to_string + mcwire nci_str_w*.

## Decompiled sources (all functions of mcpost.dll, 8892)
`tools/mp_split_decomp.py ~/mastercam-re/work/mp/mcpost_all.c decomp/` writes, by class prefix:
| file | functions | contents |
|---|---|---|
| decomp/mcpost_parse.c | 561 | .pst lexer / declarations / post table (CPostFindVars, CPostBuildTable, CPostPostTable, CPostStringUtil, CPostFs, CPostFmt, CPostFstrsel, CPostFlkTbl, CPostFprmTbl, CPostFbuf, CPostFstack, CPostFq, CPostVariable, CPostString, CPostPostBlock, CPostOperator, CPostFunction, CPostBoolean, CPostText, CPostComponents) |
| decomp/mcpost_run.c | 345 | runtime (CPostRun*, CPostOutput, CPostFormulaSolver, CPostMath, buffers, stack, streams, sub files) |
| decomp/mcpost_nciread.c | 684 | NCI loop (CPostReadNci, CPostRunNciUtil, CPostGcode*, CPostArc, CPostProcess*, CPostDrill*, CPostThread, CPostPlane, CPostNciPreProcess, CPostBreakNci, CPostValidateToolChanges, CPostSubs*, CPostRunFiveAxis) |
| decomp/mcpost_mcaminfo.c | 156 | Mastercam data (CPostRunMCamInfo, CPostMachineInfo, CPostInitPreDefs, CPostToolTable) |
| decomp/mcpost_msg.c | 403 | messages / log / MP debugger server / dialogs / comms (UI; not ported) |
| decomp/mcpost_convert.c | 85 | post version / UpdatePost / X conversion |
| decomp/mcpost_coder.c | 63 | encrypted .psb decoder (OUT OF SCOPE, reference only) |
| decomp/mcpost_fun.c | 2086 | unnamed FUN_* (assign by caller when ported) |
| decomp/mcpost_lib.c | 4509 | MFC / STL / CRT / import thunks |
Earlier hand-picked files stay: mcpost_format.c, mcpost_formula.c, mcpost_keywords.c, mcpost_numfmt.c, mcpost_predefine.c,
mcpost_rdnci.c, mcpost_readloop.c. Other dumps: decomp/mp_driver.c (mp.dll, 237 functions: PostMain / m_main / MPXRun
driver), decomp/mcwire_ncistr.c (mcwire nci_str_* ASCII NCI formatters), decomp/mccore_bnci_nci.c (mccore bnci_to_nci +
callers of bnci_to_string). Character classes: `formats/mp/mp_charmaps.tsv` (tools/mp_charmaps.py, from
CPostStringUtil::LoadCharMaps @1801477f0: first character of a line decides its kind - p/l/m postblock, s string, f format
keyword, other letters numeric variable, '#' comment, '[' post text; label modifiers `*` 50 `` ` `` 51 `~` 52 `!` 53 `@` 54).

## Integration (app) - done 2026-10-23 (user decisions P1-P4, formats/user_decisions.md "Post engine")
- Post path chain (decoded, viewer/mcam_load.c resolve_machine, formats/grpprops_tabs.md 2.2): active machine group
  +0x1170 -> machine record (ASSOC sub 22) whose +0x34 matches -> `CCNCMachine::PostProcFile` = record +0x9b1
  (`McamFile.mach_post`, e.g. `%MCAMDIR%mill\Posts\MPFAN.PST`), control definition path +0x8ac (`mach_ctrl`), control
  record = machine +0xe94 -> CNC_CONTROL (sub 23) + pages (sub 24).
- **P1 `%MCAMDIR%`**: mastercam.dll `TokenToPath` (imported by mcpost) stays NOT DECODED; viewer/paths.h `host_path` maps a
  leading `%MCAMDIR%` (any case) to `<APP_DIR_SHARED>` = paths.conf `shared` (the shared Mcam2017 folder; the imported copy
  `<config>/shared` as fallback), then the existing case-insensitive Windows-path lookup. The user's parts name
  `%MCAMDIR%MILL\POSTS\GENERIC FANUC 3X MILL.PST` (BRACKET4) / `...\MPFAN.PST` (test-cube-10mm): both resolve to
  `shared Mcam2017/mill/Posts/*.pst`. (cui_toolpage.c / cui_grpprops.c applied the same rule by hand for the libraries.)
- **P2 the switch** (ours): `McamFile.grp_mppost`, Machine Group Properties > Files check box "Post with Mastercam post (.pst)
  (ours)" (core_ui cui_grpprops.c `GP_MPPOST`, id 0x7a01, on the free right half of the Edit / Replace row of the "Machine -
  Toolpath Copy" box; Mastercam's page shows the post read-only - 686 / 604, FUN_1801e6720 - and has no such control).
  Default off. Kept in the project file (`GRPMPPOST 1`, written only when on; not a part record: the .mcam save is
  unchanged). Script step `grpmppost:0|1`; MCP `post` tool option `mastercam_post`.
- **Off**: `post_ops` (core_ui cui_ops.c) is exactly the LinuxCNC post (nc_post.c), unchanged.
- **On**: `post_ops_mp` -> viewer/mp/mp_post.c `mp_post_part` = mp_cli's part chain: `mp_nci_write_part_env` (all ops, F.ops
  order) into a temporary file -> `mp_post_load(pst)` -> `mp_run_new` (MpEnv: posting time = now, `metric` = IsMetric()
  (core_ui tp_metric), `product` 0 mill / 1 router from CCNCMachine::Type 1 / 3, nc_path = the program, nci_path / part_path =
  the part, prm_ctx = the loaded part) -> `mp_run_nci` -> a pending line flushed. **Output files (Q24, 2026-10-24,
  formats/nci_writer.md 5):** one program per NC file name (op +0x170), named as Mastercam's post names it -
  build_filename(the NCI name, folder, the control's NC suffix): `BRACKET4.NC`, `T.NC`, ... in the part's folder (ours: the folder;
  no control suffix read: `<part>-mcamui.nc`). Post messages (CD re-initialisation, run messages) go to stderr as with mp_cli;
  the status line names the files and counts the run messages.
- **Ops made or edited here (Q23, 2026-10-24):** their NCI is written by `viewer/nci_write.c` (the port of mccore's bnci_wr_*
  writers, formats/nci_writer.md) from the OP_ID record the save would write and the engine's moves (`nciw_view`), then posted
  like a stored op.
- Refused, never a partial program (ours): no machine / post; machine type not mill / router (**P4**: lathe / wire not
  posted); the .pst not found; an op made here that nci_write refuses (drill canned cycles: NOT DECODED; an op the save leaves
  out); an op whose tool changed after Mastercam computed it (nc_post.c's rule); an op nci_ascii could not convert
  completely (`MpNciStats.skipped`, new: no OP_ID record, no NCI positions / section, probe op, an unknown gcode); no toolpath;
  the post stopping (exitpost$ / run error). Disabled ops (op +0xe8) are skipped as Mastercam does.
- **P3 line ends**: `MpEnv.eol` (new): the standard end of block AppendEndOfBlock @1800d9660 appends (CPostOutput +0x1c0 =
  "\r\n" in mcpost) is `"\n"` in the app; `omitcrlf$` / `use_alt_eob$` / `eob$` (the post's and the control definition's own
  settings) are honoured as before. mp_cli keeps "\r\n" by default, `--eol lf` gives the app's. The app opens the program in
  binary mode, so the post's bytes are written as they are.
- Link note: mp_run_prm.c (MD / CD overrides, rd_md$ / rd_cd$, op parameters) is reached only through weak references, which
  do not pull an object out of the app's static core library; mp_post.c holds a strong reference to keep it (without it the
  run silently used the post's own values - caught by the golden test).
- Test: `tools/tests/mpint_cmp.sh OUTDIR` - BRACKET4 (Generic Fanuc 3X Mill.pst, 23272 lines) and test-cube-10mm (MPFAN.pst)
  posted by the app (offscreen, `grpmppost:1;post`) equal `mp_cli PART POST BRACKET4.NC / T.NC --metric 1 --eol lf` apart from
  the DATE line, no CR, same CD messages. Scenes (tools/tests/stockplanes_scenes.txt `gp_mppost_*`): the check box + OK /
  Cancel, the project line round trip, off again, the refusal of an op made here.

## Number formatting (mp_fmt)
`viewer/mp/mp_fmt.{h,c}` (port), `viewer/mp/fmt_test.c` (self-tests + oracle CLI; `make fmt_test`). Every rule cites
the mcpost.dll function/address in `mp_fmt.c`; string constants were read from mcpost.dll .rdata.

**Parsing** (`CPostFs::ParseFs2` @180098130, reached through `ParseFs2AddStatement` @180098ae0 for both `fs` and `fs2`;
the two keywords parse identically). After the keyword: `#` comment cut, fs number (`ParsePostLongFromDbl`, must be > 0),
then option letters are searched ANYWHERE in the rest (`+` `sS` `lL` `tT` `nN` `dD` `iI` `zZ`; `d` wins over `i`),
upper-cased and removed. Digit counts are ONE character each: `<int>[sep]<dec>[round]` for inch, optionally the same
for metric (missing metric part = copy of inch). sep `.` 0x24, `,` 0x48, `^` 0xb4, `%` 0xd8 (the inch separator sets
the metric bits too; a metric part first clears 0xe0). A digit directly after the dec digit is a round digit, valid
`0259` (2/5 = round to steps of 2/5 units of the last place, 9 = 16-digit fcvt, 8 = one extra digit then truncate -
8 is handled by FormattedNumber but cannot be parsed). Errors (bit 0): 0x21 0x41 0x81 0x101 0x401 0x801; warnings:
0x202/0x1002 bad round digit, 0x2002 d+i, 0x4002 z with ^/% (z cleared), 0x8000 duplicate option, 0x10000 trailing
text (statement still added). `SetMaxFormatNumber` @180098e00 computes 99..9.99..9 (1e20 when int digits = 0) — no
reader of that value was found in mcpost_all.c, the integer part is never truncated.

**Value** (`FormattedNumber` @1800bc9d0, what output stores back into the variable and modality compares):
`ValidateNumber` (|v|>1e20 -> ±1e20, |v|<=1e-20 -> 0), optional 2/5 step rounding (`round_long`), then
`RoundFcvtBuffer` to the dec digits and `atof` of "int.frac".
**Text** (`FormattedNumberString` @1800bcd70): zero -> empty digits; else `RoundFcvtBuffer`; empty buffer drops the
sign (no `-0`). int = first decpt digits (`l` pads zeros to int digits, never truncates); frac = digits after it (zeros
first when decpt < 0); no `t`: trailing zeros stripped, `t`: exactly dec digits. Output: `-` | `+` (opt +) | ` ` (opt s);
if int and frac are both empty `"0"` (disasm 1800bd1a8), else int; separator `.`/`,` unless (`^`/`%` and no frac);
no separator + `t` + no `l` + empty int: leading zeros of frac dropped (one kept); then frac, or `"0"` when frac is
empty and `z` is set (also for formats without a separator: `1 0z` prints 5 as `50` — DLL behaviour).
round_opt$: tens digit 1 or 2 switches `RoundFcvtBuffer` to fcvt(25 digits) + digit rounding (1: every dropped digit
>= 5 cascades; 2: only 9s carry into the first dropped digit; a first dropped digit other than '4' cuts the tail).
FormattedNumber passes the tens digit of round_opt$ (`UnAssembleRoundOption`), FormattedNumberString passes
round_opt$/10.

**MSVC `_fcvt_s`** (MSVCR120, disassembled: `_fltout2` @18007d134 calls `$I10_OUTPUT(ld,17,0)`, which generates 18
digits and rounds half-up to 17 (18007f22b); `_fptostr` @1800251e0 rounds half-up on the next of those digits). It is a
double rounding on 17-significant-digit text, not a rounding of the binary value: 0.0115 -> `.012` at 3 places
(exact binary .011499999999999999…), 156.8275 -> `156.827` (17 digits `15682749999999999`), 1.005 -> `1.00`.
Ours: the 18 digits come from the exact binary expansion where $I10_OUTPUT uses 96-bit scaled arithmetic (can only
differ at exact ties at digit 18); `atof` = glibc strtod.

**Incremental** (`DressedFormattedNumberString` @1800bb9a0): post-table types 900/901 with fs flag 0x1000 (`i`) output
value - previous (`mp_fmt_incr_value`); `OutputVariableIncremental` @1800ddbf0 (type 0x329) outputs the value as is.
Debug mode (bug4$ < 0): raw 20/9 digits (`MP_FMT_RAW`).

**Oracle** (`fmt_test check POST FILE.NC…`: every address word re-formatted from its parsed value with the post's
`fmt` letter -> fs candidates, units from G20/G21): MPFAN.pst / Test01.NC 1352/1352; Generic Fanuc 3X / TEST-MC.NC +
<posts>/gcode/*.NC 1,873,820 of 1,875,570 — the 1,736 misses are 1,724 in FreeCAD (`*-fc.NC`, `Z20.000`)
and hand-written probe files, 12 in hand-edited lines without N numbers or with `Z0`/`Z10`/`O0` that fs 2/7 cannot
produce; Generic Haas 3X / WestonShakespear *.NC 755,292/755,292. The oracle checks number shapes only (raw values
are not in the NC files); rounding is covered by the decomp-derived self-tests.

**vs tools/mp_format.py** (differential over 108k cases): mp_format rounds `value*10^n` in binary floating point
(wrong: e.g. 156.8275 -> .828, 1.005 -> 1.01, 2.675 -> 2.68 where the DLL gives .827 / 1.00 / 2.67); omits the "0" for
an all-empty number when `t` without `l` (DLL always appends "0": `fs2 19 2 0 2 0t` zero -> `0`); strips all leading
frac zeros for t-without-l (DLL keeps one, and only when the int part is empty); applies `z` only with a separator;
has no round digit (0.45), round_opt$, ValidateNumber, option letters before the digits, or parse warnings.

NOT DECODED: NaN/inf text of $I10_OUTPUT (ValidateNumber clamps inf; NaN prints empty here); the default format
statement copied in when dec digits > 9 (unreachable from the parser); names of the two parse-table keywords that
call ParseFs2AddStatement (index 0 vs 1, assumed `fs`/`fs2`); ParsePostDouble edge cases for the fs number (ours:
`[+-]digits[.digits]`); bit-exactness of MSVCR120 `atof` (only affects the stored rounded double in the last ulp).

## MD / CD / group / op parameters (mp_prm)
Code: viewer/mp/mp_prm.c / mp_prm.h, test CLI viewer/mp/prm_dump.c (`make prm_dump`; `prm_dump PART [cd|md [ENT [NAME]]|mdd ENT|mdinfo|grp|op N|ovr [AXCOMBO]|all]`).
Tables generated by tools/mpprm_mdcd_extract.py from decomp/mcmpparameters_all.c (review TSV:
decomp/mcmpparameters_mdcd_params.tsv; mccontrol / mcmachinedef excerpts: decomp/mcmpparameters_mdcd_sources.c, full
dumps ~/mastercam-re/work/mp/prm/mccontrol_all.c, mcmachinedef_all.c).

**Delivery (CPostRunMCamInfo::ReadCDInfoLoop @180122b90, ReadGroupInfoLoop @180122f70, ReadMDInfoLoopBase @180124070,
ReadOpInfoLoop @180124640).** rd_exit$ = 0; header call (GetHeaderParameter: code + string; an object with no header gives
code 0 and ""); then one call per parameter: all numerics in ascending code (sparameter$ = CString::Format("%.15lg")), then
all strings in ascending code; the loop stops early when rd_exit$ != 0 after a call; the footer call (19999, "%.15lg" of
the footer value, 0) follows even then. A failed postblock call aborts at once (no footer). With no params at all the
do-while still makes one call with the header's code and string. Postblock: pmachineinfo$ (pdump$ when the read is a
dump read, this+0xec); ops: pparameter$, or pwrttparam$ while the pre-read (pwrtt) pass runs (+0x386), with
SetStandardFprmMethod(1) around each call. Parameters live in std::map<int,double> / std::map<int,CString>
(PrmAppBase +0x10 / +0x20): ascending codes, the first insert of a code wins.

**Headers.** rd_cd$: PrmControl(control, pPostGroup) code 19990 + the group name (op_group +4). rd_tlpathgrp$: 19970 +
group name. rd_md$ base: 19960 "MACHINE". Components: header (GetHeaderParameter) then 19959 / 19958
(PrmAppExtendedMachineHeader::GetHeaderG19959 / G19958, "%.15lg") before their params (ReadMDInfoLoop @1801238c0).
Ops: 10000 + the op type name ("contour", "drill", ...) from ParseOperation's switch on the byte map at 0x180012c50.

**rd_md$ (ReadMDInfo @180123460, rd_mch_ent_no$; ours mp_prm_md / mp_prm_md2(f, ent, dump, sparameter$)):** 0 = machine
base only (header + params + footer); -1 = base without footer, then every axis combination (LoopOnAxisComboInfo, map
order = ascending id; CallMDInfoLoopAC: header + 19959 + 19958 + params, no footer), then every component with id > 0
(LoopOnAxisComponentInfo: ascending id, stable-sorted by (rotary output short >> 4), see below; CallMDInfoLoop header,
no footer; a component without params ends the read with 0) and one footer after the last; -2 = the combination whose
20601 text "<IsMapped> <Descriptor>" equals sparameter$ (-> its components) else the component whose name equals it
(-> header + params + footer); > 0 = an axis-combination id (the Fanuc posts pass syncaxis$ = the NCI 950 id): its
post-circuit components (CallMDInfoLoopAcComponents @180121ad0: params 0x4513..0x451f of the combination, each one whose
GetUidFromUidInfo uid has 36 characters -> header + 19959 + 19958 + params, no footer, result ignored; then one footer
of the last one), or a component id (header + footer); < -2 (and > 0 on a dump read, +0xec) = the combination's own
params with header + footer. An id found in neither map: success, no calls.
Component delivery (ReadMDInfoLoop @1801238c0): ReadMDComponentHeader (GetHeaderParameter: 19960 + "<TYPE>_COMPONENT"),
19959 = GetHeaderG19959 = Ent_idns()[0] (the component's id), 19958 = GetHeaderG19958 = its Type ("%.15lg"), then the
params as for the base. Combination: 19960 "MACHINE_AXIS", 19959 = Id, 19958 = -1.

**Where the ids come from.** mcpost's id -> uid maps (CPostMachineInfo +0x10 components, +0x88 combinations) are read
from NCI lines 20600 / 20601 (ReadTraditionalAcAndComponets @1800ba450 -> BuildEntUidDataMaps @1800b0650 /
BuildAcEntUidDataMaps @1800b03e0). mccore writes them before an op's 999 while CCNCMachine +0x10 is set
(CCNCMachine::WriteComponentUIDsToNCI @18006eda0, which clears it, + WriteAxisCombinationsToNCI @18006ec10; every
CCNCMachine ctor sets +0x10 = 1): 20600 `%d %s "%s" "%s" %s` = id, uid, AxisLabel(0), AxisLabel(1), name for every
component of the tree except a plain CMachineComponent of Type 2 (WriteUIDToNCI @180090380; linear / rotary overrides
always write); 20601 `%d %s %d %s` = id, uid, IsMapped, Descriptor for each axis combination that is not mapped and
IsValidForCutting (@180014c20). uid = MdComponentId::ToString(false) = upper-case UuidToStringA (36 characters).
**The NCI port (nci_ascii.c) does not write these lines yet**; mp_prm builds the same maps from the part records.
The params come from mcmpparameters PrmMachine (ctor @18003c390): AddComponent @18003d640 (tree order, keyed by uid,
first insert wins) -> PrmMachineComponent @18003ed10; BuildAxisComboMap @18003d960 (CCNCMachine::MakeAxisComboList
@18006b370, combinations with a non-empty post circuit) -> PrmPostAxisComboType @1800404f0.

**Machine definition records (measured on mc-test01 / BRACKET4 / the 2017 multiaxis samples; mp_prm.c md_*).**
Machine (sub 22) = CCNCMachine - 0x70: Type = record +0x68, tree root = +0xe98 (InitFromDb @180057e30 ->
BuildComponentTreeFromDb(+0xf08)), axis-combination list (sub 15) = +0xea4 (AxisComboList_ent_idn @180061a20).
Component (sub 12, 0xa78 bytes) = CMachineComponent Params (object +0x20 = record +0x68, object X = record X + 0x48):
name +0x68, Type +0xe4, Ent_idns {self +0x298, next sibling +0x29c, previous +0x2a0, first child +0x2a4}, uid
+0x280 (InitUid @18008d200: object +0x238), Machine_ent_idn +0x2a8, GeoType +0x2b0, SolidGeo +0x2b8, UParams +0x508.
Axis combination (sub 26, 0x1ef8): name +0x68, groups 0 / 1 = sub 13 ids at +0x1e40 / +0x1e44, component lists 0 / 1 =
sub 15 ids at +0x1e48 / +0x1e4c, uid +0x1e58 (InitUid @180014830: object +0x1df8). Group (sub 13): +0xe4 = its params
record (sub 21): class +0x68 (1 part, 2 tool), type +0x6c. Part list = the list whose last component Type is 100..108
(CompListIsPart), tool list = last Type 200..207 / 300..307 (CompListIsTool); post circuit (MakePostCircuit @180015cf0)
= part list reversed + tool list. IsMapped = a group's params class 0x65 / 100. IsValidForCutting (machine Type 1 / 3 /
other except 2 and 5): tool + part lists exist and EnoughMachineAxes(op 1) on the linear (WCSAxis UParams +0) and
rotary (UParams +0x7c) axis counts (op_is_multiax / caxis / mill of op 1 from mccore @1800c6700 / c5c90 / c6150).
Component params: the ctor's common rows (0x4331 name, 0x4332, 0x4333 type, 0x4334 color, 0x4337 / 0x433a / 0x433d
points, 0x4340 / 0x4349 matrices, 0x4352, 0x4564 ChordTol (raised to tol 5e-05), 0x4565, 0x45fb..0x45fd, 0x4566 uid,
0x4355, 0x442b, 0x4356 IsExpanded, 0x4357 GeoType), the GeoType 2..7 struct (GEO_ROWS) and the Type struct (COMP_ROWS;
21 builders walked by the generator, 0x45cc / 0x45ba by hand). Combination params: 0x4513 id, 0x4514..0x451f the first
12 post-circuit ids, 0x4520 name, 0x463d / 0x463e tool / part group params type (-1 without).
Order key for rd_md$ = -1: GetDiameterAndRotaryOutputFromAxisId @1800b2370 = AxisOutputType (UParams +0xb8) + 1,
negated when CWAxis (UParams +0x84); 0 for non-rotary.
Checked: 17 user-parts parts (VMC / HMC, 5 components in the circuit, 349 calls for syncaxis$), mc-test01 (Mill Default:
table A X Y Z B C spindle, 559 calls), 13 of the 2017 multiaxis samples that mcam_load reads (combinations valid,
circuits like Fixture A X Y Z B spindle); the 950 id of every op = a combination with a 20601 entry.

**Record sources.** Machine base = PrmMachine copy of CCNCMachine::Params (machine +0xd8 = the CNC_MACHINE_ID record
+0x68; PrmMachine this+X = record X-0x10), the machine of the active group (mcam_load resolve_machine). Control:
CCNCControl::ReadEntToControl @18003ae00 - page n = ASSOC sub 24 record with +0x68 = n, data at ent +0x80: 0 tolerance,
1 communication, 2 file_ctrl (paths stored as %MCAMDIR% tokens; Mastercam expands them, ControlTokensToPaths, and
SetFileCommon replaces missing dirs - neither ported: the stored text is delivered), 3 the post list record (Mastercam
then overwrites it from the internal post list, GetRecordFromPost - ours: the stored page), 4-7 NC dialog mill / router /
lathe / wire, 8 nc_output_ctrl, 9 nc_path_ctrl (sub-structs at ent 0x80 worksys, 0x18a tool, 0x29e toolMT, 0x3b2 linear,
0x4c0 linearMT, 0x5ce arc, 0x6ee arcMT, 0x80e rotary, 0x914 feed, 0xa28 feedMT, 0xb3c comp, 0xc4a subprogram, 0xd60 /
0xd62 / 0xd63 misc), 10 cycles (0x80 general, 0x18e drill, 0x29a drillMT, 0x3a6 lathe), 11 wire. control_type (main page,
CCNCControl +0x2e94) numerics are always 0 (ctor memset, ControlDefaults; only the manufacturer string +0x10 comes from
the sub 23 record +0x78). Group = the op_group record (0x1ae0). Op = the OP_ID record (ParseOperation this+X = X-0x10).

**X9 parts:** control page 2 (file_ctrl) of a .mcx-9 part (record +0x48 = 0x12) has an older layout (NC path at +0x14,
strings 0x102 apart; 2017: +6, 0x104): only +0 / +2 are used, the rest is reported missing (X9 -> 2017 converter NOT
DECODED). Other pages / the machine record agree between BRACKET4.mcx-9 and mc-test01.mcam where checked.

**Re-initialisation (CPostRun::OverrideInitializeFromMDandCD @1801111a0 = InitialAssignFromMD @1800b8a80 then
InitialAssignFromCD @1800b32b0), mp_cd_overrides().** Each item through AssignOutputVariableReInit @180125cb0 (skipped
when the variable's post-table flag 0x1000 is set), logged "CONTROL DEFINITION - - Post variable 'X' was re-initialized
from A to B" (messages 0x2329..0x2330 with a bit) when it changed by > 1e-12 and file_ctrl[0xd4a] == 0 (mp_cd_quiet).
MD (only when x_mp_adv_func$ != 0): fr_min/max_ipm/ipr/iinv/ideg/mpm/mpr/minv/mdeg$ from the machine record
(+0x1a8.. inch, +0x1e8.. metric; iinv/minv min = [1], max = [0]; *deg min = 0, max = MaxRotaryFeedrate +0x198).
CD: tolerances (page 0 +0..+0xb0), error_msg$ (+ SetErrorToScreenType) / err_file$ (page 2 +0 / +2), seqno$ / seqinc$
(no message) / seqmax$ (page 8 +0 / +8 / +0x10), maxnccomment$ (>= 750 -> 749), dec_seq_right/left$, spaces$, eob$ and
eob$[1] (only when eob$ was assigned), absinc$, comm_filter$ (Un/AssembleCommentFilter with 4 flags = page 8
+0x12b/+0x12a/+0x12d/+0x129 == 0), fastmode$ / omitseq$ (== 0), sub_seq_typ$, use_dec_seq$, omitcrlf$, use_alt_eob$,
workcoord$ / tplanemode$ / maptomachview$, bldnxtool$ = 1 when nc_path[0x118] || [0x22c], (l)nobrk*, (l)cutmode$,
(l)arctype* = short + 1, (l)arcoutput* = short < 4 ? 0 : short - 3, (l)breakarcs*, (l)helix_arc$, (l)do_*_arcs$,
(l)do_full_arc*, (l)arccheck$ = 1·c[+0] + 10000·c[+1] + 10·c[+2] + 1000·c[+3] + (c[+4] ? 100 + 100·short[-8] : 0),
feed types / rotfeed / inversefeed / convert_rpd (mill 0x9a8.., lathe 0x894..), sub_level$ (-2 / 1 / 2 / 3 / 4 from
0xbd6 / 0xbcc / 0xbca, direct), rtnrefht$, peckacel$ = short·0.01 (< 1e-12 -> 1), canned-cycle flags (page 10),
g60_mode$ (page 11 +0x21e). Also two internal fields: run +0x13e1 = file_ctrl[0xd3a], +0x13e2 = file_ctrl +4 (CallReadOpInfo
reads transform / nesting ops' params only when (+0x13e2 & 0xfffd) == 0). Checked: BRACKET4.mcx-9 gives exactly the
values of TEST-MC.err (err_file 1, dec_seq_right 0, omitseq 0, sub_seq_typ 1, arctype* 5, arccheck* 111, rotfeed* 1,
peckacel 1). CPostInitPreDefs::ReInitializeArgsFromMDandCD @1800a9c50 is not data: it re-solves the post's
initialisation formulas (post-table entries 0x7d1 whose formula uses a 0x329 function flagged 0x2000) after the
re-initialisation - interpreter work.
**InitialAssignFromMD's rotary part (mp_md_rotary / mp_cd_overrides_ac(f, axcombo)), before its feed rows, plain writes
(no 0x1000 test, no message), axcombo = strt_tooltable +0x38 of the first tool-table record (interpreter):** matb1$ =
the machine base matrix (GetAxisComboMachineMatrix @1800b1b10: rows X / Y / Z from the linear axes by MachineAxis
UParams +0x198 = 1|7 / 2|8 / 3|9, vector = GetAxisVec(AxisDir UParams +0), a missing row = cross product of the
other two); axis order (GetAxisComboInfoAndAxisOrder @1800b16c0: list1 = slot k <- the circuit component with
MachineAxis k+1 / k+7 (rotary UParams +0x78, linear +0x198), list2 = the first six linear / rotary of the circuit,
SetNumberAxisAndHeadOrderRotary @1800bb060, GetAxisComboReversalRotationWorldRelations @1800b1f00 against
s_MatTopView = identity, 5e-08); primary / secondary rotary = list1[3 + order[0 / 1]] (Type 5 only): limlo / limhi =
record +0x610 / +0x618, intlo / inthi = +0x5a0 / +0x5a8, limtyp = GetLimitType @1800b2650, rotaxis = MapMpTypeToAC
(ConvertToSignedAxis(zero-degree axis +0x588)), rotdir = MapMpTypeToAC(GetDirectionAxis(signed WCS axis +0x584, signed
zero axis, CW +0x58c)), nut_ang_pri$ = +0x598 when the secondary's +0x58d != 0. mc-test01: A primary (rotaxis1 3,
rotdir1 -2), B secondary (3, 1); the SWARF sample's B has MachineAxis 4 like its A, so only the primary is assigned
(as in Mastercam).

**Timing.** rd_cd$ / rd_md$ / rd_tlpathgrp$ run only when the post calls them (the Generic Fanuc posts: rd_cd$ and
rd_md$ with rd_mch_ent_no$ = 0 in pprep$, rd_md$ with syncaxis$ in pset_mach at each tool change). Op parameters are
read implicitly after NCI 1020 (rd1020 -> CPostReadNci::CallReadOpInfo @1800fca30) when pparameter$ has a body
(param_2 = 0) and param_read$ != 3, when op_id$ changed, plus xform_op_id$ / nesting_op_id$ ops (gated by +0x13e2);
the pwrtt pre-read pass calls it with pwrttparam$. **Op parameters come from the operation database
(TpOpList::OpByID -> PrmAppMPParameter(operation)), not from the NCI.**

**Op parameters ported:** header (10000 + name), ParseOperation's common section (OP_ROWS, 361 rows), the
opcode-specific union (OP_UNION_ROWS ~22.9k rows over 87 builders; case -> builder OP_UNION_BUILDER; walked from
this+0x1238 = record +0x1228, added after the common rows so a common code wins), GetAssemblyIdInfo (15773 =
StringFromGUID2 of record +0x1200, "{...}" upper case), GetHolderIdnInfo's 15683 (int record +0x1210), and the footer
value (SetPathType @18000e040: 2 when the opcode is outside its path-type ranges, else 0). Checked: 15346 = short
+0x5d0 (FUN_180013910), 10010 = double +0x2e0 (FUN_180013a30), 10068 = double +0x888 (FUN_180014900).
**20000-series parameters (20001 tool name, 20007 tool values read with rparsngl(sparameter$, 11) ...) are not op-database
parameters:** they are NCI parameter lines; CPostNciPreProcess::wrtt @1800cd4b0 / CPostReadNci::ReadLoopParameterOutput
@1801066a0 call pwrttparam$ / pparameter$ with sparameter$ = the NCI line text - the NCI side delivers them.
Op items still NOT DECODED (listed as UNPARSED in decomp/mcmpparameters_mdcd_params.tsv; 0x357e is read since 2026-10-23:
PrmAppStruct::Add(int, uchar const *) @180001c40 = lower-case UuidToStringA): FUN_180015660 computed
points (0x3b9a.., 0x3b9d.., 9 locals), 0x3b87 / 0x3e1c / 0x3b1b, the FUN_1800137d0 loop, FUN_180009e00 computed values
(0x347f.., 0x350f..), a few pCVar strings and two lost-argument strings (FUN_180018c60 0x3ba2, FUN_180018100 0x3cdf),
the holder's parameters when record +0x1210 != 0 (Cnc::Tool::HolderGetHolder).

NOT DECODED (summary): machine definition - lathe (Type 2: op 0x3d, op_is_multiax jump table) and wire (Type 5: list /
group 2 not in the record, the linear-axis test of IsValidForCutting) machines (rd_md$ returns -1), mapped axis
combinations (MappedAxisCombo, Descriptor's resource string 0x75d), the CComponentGroupParams GetNext chain (one
params record per group), InitTabPg's clamp (object +0xc28), Ordinal_2863's case rule for rd_md$ = -2 (ours exact),
a tree root with siblings (WriteComponentUIDToNCI walks them, AddComponent does not), the combination matrix when it
has fewer than two linear axes (mp_md_rotary returns -1), PrmMachineComponent rows the generator could not read
(UNPARSED in the TSV: FUN_180044490 0x4374 / 0x4377 file-geometry loop); the NCI side's 20600 / 20601 lines;
%MCAMDIR% token expansion and
SetFileCommon's path fallbacks; X9 file_ctrl conversion; the active post record (page 3 vs GetRecordFromPost); the op
items above; ControlDefaults values for a control page missing from the part (reported as missing, not invented).

## ASCII NCI (bnci_to_nci port)
`viewer/mp/nci_ascii.{h,c}` + CLI `viewer/mp/nci_dump` (`make nci_dump`): the stored binary NCI of a part's operations ->
the ASCII `.nci` text mcpost reads (pairs of lines: gcode line, parameter line). Port of mccore `bnci_to_nci` @180088920,
`bnci_convert_section` (FUN_180083560), `bnci_to_string` @180089b70 and its line formatters. The Ghidra decompile drops the
stack arguments of the formatter calls, so argument lists come from objdump of the call sites (addresses in the source).
The `nci_str_w*` exports of mcwire.dll are only the Wire branch (nci_bin +0xc = 2); mill / router / lathe records use
mccore's own FUN_1808cXXXX formatters.

API: `mp_nci_write_part_env(f, env, op_index, nops, out, stats, warn, err, errn)` (file), `mp_nci_record_ascii(cv, rec, n,
opcode, line1, n1, line2, n2)` (one record), `MpNciEnv` = the runtime values (defaults = the DLL globals' static values).

**Numbers** (`append_real` @1808c99e0 -> `fmt_real` @1803698f0, `append_short` @1808c9a60, `append_long` @1808c9860):
- real: `sprintf("%.*lf", n_places_nci, v)` with n_places_nci = 8 (mastercam.dll .data 0x18087b884; only scoped
  save/restore writers), then every trailing '0' removed: `25.` `-6.5` `0.` `-0.` `299.97`. |v| >= 1e38 or NaN ->
  `$$$$$$$$$$$$$$$$$$$$`. (A demo-product branch, GetProdEnableBits & 1, rounds first: not ported.)
- short `%hd`, long `%ld` (32 bit), FUN_1808c97d0 `%d`, append_boolean = short (b == 1).
- one ' ' between items, none leading / trailing; 1013 and 1020 add " " + a text unconditionally (trailing ' ' when empty).
- the finished parameter line has TAB / LF / CR replaced by ' ' (FUN_1806f27d0); gcode line = BnciLineToGcodeString:
  `%d`, 10000 -> 10000 + rec +0x18, 1055 -> `29999`, negative -> empty.
- modal feed (param_5 = 1): G1 / G2 / G3 / 11 print feed -1 when |feed - previous| < tol (mccore ?tol, static 5e-05) and
  feed != -2; G0 prints -2 and sets previous = 1e38; tool change, 1011-1017, 1020, 1025, 1027, 1031/1032, 1003, 80,
  1004, 1040 / 1041 reset previous = -1e38; drill 81 sets previous = its +0x40.
- `pos_to_ascii(mode, flags)` @1800a2dd0 (contour flags word -> decimal code; mode 0 mill, 1 lathe, 2 wire) is ported.

**Lines (mill, nci_bin flag 0)** - record offsets: 0 `cc x y z -2 pos(+0x40)`; 1 `cc x y z fr pos`; 2/3 `plane cc x y xc yc
z fr pos(+0x58) bool(+0x5a)` (x y xc yc z = +0x20 +0x28 +0x38 +0x40 +0x30, plane-local); 4 `dwell (int)+0x20 0`; 11
`p3d(+0x18) p3d(+0x30) fr +0x50 pos(+0x52) p3d(+0x58)`; 81 `+0x18 16 reals(+0x20..) pos(+0xa0) +0xa4 +0xa8`; 82 10 reals;
100 `0 10 reals pos(+0x70) +0x74 +0x78 matrix(+0x80, = the op's last 1014 matrix, copied in by convert_section) +0xc8`;
999 `optype byte(op+0x11b8) opid` (none for op types 4 / 0x50 / 0x6c); 1002 `6 longs(+0x18..) +0x30 +0x38 fr(+0x40)
map(+0x48) p3d first(+0x50) p3d home(+0x68) +0x80 +0x88`, +0x1c / +0x20 replaced by the machine's nc_output_ctrl start /
increment; 1003 `home xyz`; 1011 / 1031 10 reals; 1012 / 1032 10 longs; 1013 `+0x18 +0x20 +0x28 +0x30 +0x32 p3d(+0x38)
+0x50 " "text(+0x52)`; 1014 / 1017 matrix; 1016 19 fields (FUN_1808cbfd0, op-id fix-ups); 1020 `group stock size, origin,
corner, +0x50 +0x58 +0x60 0. +0x70 MaxToolSpindleRPM " "material`; 1025 20 shorts (3101-3120 / 4101-4120 / 5101-5120 ->
x050-x069); 1027 matrix + p3d; 1028 `sh sh p3d p3d sh sh long sh`; 1029 `p3d p3d p3d bool sh`; 950 `0 0 0 id 0 0` + 20 x 0;
20004-20009 / 20800 / 20502 numeric; 1005-1007 / 1026 / 1042 / 1043 / 1051-1055 / 20000-20003 / 20010-20021 / 20300 /
20501 text at +0x18 (10000: +0x1a, "[]" stripped when +0x18 = 0); unknown / gcodes without a case: empty parameter line.

**File order** (bnci_to_nci / convert_section): `1050` + `22 1 D M YYYY h m s PATH` (FUN_1800a1c00: the part file's
last-write time, upper-cased geo_filename) once per NC file (op +0x170 name groups); per selected op in Operations Manager
order (op +0xe8 byte must be 0, -1 < op +0x80 < op +0x84): `999`; records from its -2 to -3 with -2 / -4..-9 / -96
skipped; 20000-20009 / 20020 / 20021 (and 20100-20112) are held and written just before the next tool change, 20800
after them, 20010-20019 at once; before the first 1011 or tool change of each op: 1051 machine comment, 1053 group name
("Undefined" when empty), 1052 group comment, 1008 op comment (bnci_convert_op_comment: Trim()med, one 1008 per line);
a 1000 whose 1016 +0x60 equals the previous tool change's does not flush the held 20000s; inside a 1040..1041 block
tool changes are written as 1000; an unknown gcode stops that op. After the last op of a file: `1003` home of the last
op with a tool (op_is_notool table @1800c6cd4): make_reals_0(world_to_view(op +0x870 - op +0x5c0, view op +0x572)).
Byte-stale emulation: bnci_read_from_disk copies only the stored +0x14 count over the previous record's buffer.

**Machine definition values** (read from the part's ASSOC records with mcam_load.c's framing; record subtypes measured):
- 1020 field 12 = `CCNCMachine::MaxToolSpindleRPM(op, NULL)` (mcmachinedef @18006b850): axis combination =
  `AxisCombinationFromID(op +0x11bc)` (@180061250; `CAxisCombination::Id` = object +0x1e9c = the subtype-26 record's entity
  id, OP_ID +0x1224 names it in every part); none -> 2147483647. Its first component chain whose last component type is
  200-207 / 300-307 (FUN_180018a40 / FUN_1800127a0; chains = the subtype-15 id lists at axis-combination record +0x1e48 /
  +0x1e4c, list = +0x68 count, +0x70 ids) is scanned from the start: type (component record +0xe4 = Params +0x7c,
  Params = record +0x68) 301 spindle / 203 / 204 -> UParams +4, 303 / 307 -> UParams +0x84, 304 / 305 -> 100000
  (UParams = Params +0x4a0 = record +0x508; so the spindle max rpm is record +0x50c). Every user part: 50000.
- 950 (written before the first 1025 after each 1011): `CAxisCombination::Id` = OP_ID +0x1224 of the op it names.
- open mode (FUN_1800a1800): "wt" for the first selected op of an NC file name (else "at" = append, no 1050 header);
  `_fsopen(name, mode, 0x40)` (SysFile::DoOpen @180a3ad70) = text mode, so the Windows file has CR LF
  (`MpNciEnv.crlf` / `nci_dump --crlf`; default "\n").
- also decoded: 83 (FUN_1808cae60: p3d(+0x18) p3d(+0x48) +0x64 +0x68 p3d(+0x30) long +0x60; FUN_1800a5540 = identity),
  923 / 924 (FUN_1808cc4f0 / cc580).

**CBnciPost pre-pass** (mcmill, run by the Post command before bnci_to_nci; it rewrites the binary NCI, WriteToBnci
FUN_18003b930): `AssignToolChanges` (FUN_180037ca0, ported as `assign_tool_changes`) recomputes every 1000-1002 of the
selected ops (op +0xe8 = 0, sections grouped by NC file name): 1001 for the first tool change of a file / after an NC name or
program number (op +0x11b8) change; with the same 1016 +0x60 tool id as the previous tool change (or a wire op): same op
-> 1000 (inside a 1018 subprogram of a transform op with "force tool change" op +0x289: 1002), other op -> 1002 when the
tool number (+0x24) differs, else 1000 (1002 with force); another tool id: 1002, or with the same tool number Mastercam's
yes/no prompt (1002 when forced, other axis combination, or batch mode; MpNciEnv.same_tool_answer); a lathe/mill switch
counts as another tool id. mcpost then still applies its own first-tool-change / CPostValidateToolChanges rules.
`RenumberWorkOffsets` (FUN_180039940, ported as `renumber_work_offsets`; WorkOffsetUtil dumps in
decomp/opman_dialogs.c + decomp/mastercam_workoffsetutil.c): a WorkOffsetUtil map keyed (work offset, machine group id)
is built with BuildParameters {flags 7, automatic 0, number 0, match mode 1, allow conflicts 1, start 0, step 1, include =
the selected ops}: MapAddPlanes (named planes of the part's planes archive with Plane +0x330 >= 0; matrix +0x40, origin
+0x140 with their stored P3D tols), MapAddOperations (non-skipped ops without op_has_no_view, op +0x602 >= 0; matrix op
+0x578, origin op +0x5c0, mg = record +0x5bc), MapAddNci of the ops' 1013/1014/1016/1027 groups with 1016 +0x42 >= 0
(matrix 1014 +0x18, origin ViewToWorld(1013 +0x38, GetViewNo(matrix, true))). Then SetBuildParameters {4, automatic 1,
number 1, ...} and, per selected op's section (ReadSection mask 1000-1002/1013/1014/1016/1017/1027), each complete
1013,1014,1016(+0x42 < 0),1027 group goes through MapAddNci with the op's machine group: AssignWorkOffsetNumber =
GetMatchingWorkOffset (equal matrix + origin within the node tols, kind 4 plane or same mg, no conflict; map order, then
add order) else GetNextWorkOffset(mg, 0, 1) = the first n with (n, mg) not in the map; the node joins the map, so later
groups on the same plane reuse it. The 1016 +0x42 is rewritten (applied in convert_section by record address).
`NCI_WO_DEBUG=1` prints the assignments and the map. Results (2026-10-23): mc-test01 and every user-parts part with
toolpaths: all 1016 work offsets -1 -> 0 (first by GetNextWorkOffset, the rest matched), no plane / op / NCI node had
an explicit offset; MPFAN's G54 = workofs$ + 54 matches Mastercam (Test01.NC; the user's NC files all show G54).
The rest of the Post pre-pass FUN_1800394e0: FUN_18003ac20 SaveBnciTempFile / FUN_18003aaf0 RestoreFromBnciTempFile
(backup of the binary NCI file, no output effect), FUN_1800395c0 (sections; bnci_regenerate_op when a section is
invalid - NOT DECODED), AssignToolChanges, FUN_18003add0 only when `post_settings` +0xc (a Post dialog checkbox, control
at dialog +0x24e8, mcpost_msg.c l.4471: NOT DECODED, static 0 = off here; it rewrites 1013/1014/1016/1017/1027 relative
to planes with find_view_m / view_to_world / Plane::operator-=), RenumberWorkOffsets, FUN_18003b440 (copies the new
tool-change gcodes into the sections by file position), WriteToBnci FUN_18003b930 (RewriteLine), FUN_180038e30 (debug
dump, Ctrl+Shift in a debug build), FUN_180039330 / FUN_1800392a0 (cleanup).
1020 (mill, bnci_to_string 18008acec..18008ad53 -> FUN_1808cc9a0): size, origin, corner, rotary vector +0x50/+0x58/+0x60,
field 11 the constant 0., field 12 record +0x70 (bnci_wr_1020 @18008f6a0: op +0x289 force tool change; +0x74 holds the
stored MaxToolSpindleRPM), field 13 the post-time CCNCMachine::MaxToolSpindleRPM (r13d, call 18008ace3). mcpost rd1020
@18010d190 (objdump of the sscanf_s arguments, 18010d26e..18010d2fa; the Ghidra decompile drops them): fields 1-10 ->
slots 0x18029d330..0x3c0 (stck_* , rotary_vec*), **field 11 -> a stack dummy (rsp+0xc8), field 12 -> 0x18029d3d0
(force_tlchg$), field 13 -> 0x18029d3e0 (maxss$)**; the loop then assigns the 12 slots 0x330..0x3e0. So maxss$ = the
rpm (Test01: 50000 -> S1069 as Mastercam). The wire branch (+0x39e == 3, 12 fields): 1-7 -> 0x3f0..0x450, 8-10 dummies
(r11+0x20), 11 -> 0x460 (tank$), 12 -> 0x470 (force_tlchg$).

**NOT DECODED** (reported on stderr; none occurs in the user's parts): AxisCombinationFromID's index-0 fallback and the
CComponentGroup branch of MaxToolSpindleRPM (no tool chain); groups / machines / controls other than the one McamFile
decodes (1002 sequence fields kept as stored, 1020 empty, 1051-1053 left out); the Post dialog options `post_settings`
+0xd (FUN_18013ad30: 1054 lines of the part's file descriptor, get_descr_info, after the 1050 header) and +0xb
(FUN_180083300 / bnci_op_prm @180086b70 / write_prm_to_nci @180601c90: the op's parameter records after its 1020) - both
static 0 and off here; the overwrite prompt of FUN_1800a1800 (ours: overwrite); machine +0x10 component / axis-combination
writers; probe ops (0x96); RenumberWorkOffsets: Mastercam's session view table beyond the reserved views 1..8 (GetViewNo's FindView: the 1014 matrix itself is used), Plane +0x120 (GetPlanes filter, taken 0), IsLeftHand's cross-product operand (taken row 0 x row 1); external-NCI ops (FUN_180083130); 1018 / 1019 (FUN_180085a80 gate, FUN_1808ccaf0 / cca70
call-site arguments); 20701; 926 (MdComponentId::ToString); the lathe-only (flag 1) formatters 200 / 201 / 900-905 /
20100-20112 and the lathe tool change; all Wire / Agie records (flag 2); home of lathe ops (CLNciFmt::get_lhome_pos_nci),
transform ops and non-reserved views; bnci_read_from_disk's version fix-ups; MSVCR120 printf rounding taken as C99's.

**Validation** (2026-10-23, `~/mastercam-re/work/mp/nci/validate.py`; re-run after the machine-definition decode: 0
NOT DECODED reports, rd950 lines pass too): 17 user-parts parts + mc-test01 / basic-mc01 (the
.emcam samples are encrypted, mcam_load cannot read them; the one WestonShakespear .mcam has no ops): 221 336 records (218 197 line pairs),
0 unknown gcodes; every line pair passes `%hd` + the `%lf` counts of formats/mp/nci_fields.txt (rd1, rd2, rd82, rd999,
rd1003, rd1011-1017, rd1020, rd1027-1029, rd1050); 187 874 motion records equal tools/nci_decode.py within the
8-decimal print (max 5.0e-9); 1003 home = the 1002 home field in every part.

## Interpreter (mp_parse / mp_run / mp_nciloop)
Owner: interpreter agent. Files: `viewer/mp/mp_parse.c` (+ generated `mp_parse_tables.c` from `tools/mp_gen_tables.py`,
flags from `tools/mp_var_flags.py` -> `formats/mp/mp_var_flags.tsv`), `mp_eval.c` (operators, assignment, value access),
`mp_eval_fn.c` (function library), `mp_out.c` (NC output / modality), `mp_run.c` (run state, postblock execution,
BlockCall), `mp_nciloop*.c` (NCI read loop), `mp_cli.c`, `mp_parsecheck.c`, private header `mp_int.h`.
Build: `make -C viewer/mp mp_cli mp_parsecheck` (header deps are declared at the end of the Makefile).

### Model (ours, semantics cited in the code)
- **Symbols**: predefined variables / strings / postblocks in registration order (= mcpost slot order; vectors are 3
  consecutive variables), then user symbols in file order (pass 1 = `CPostFindVars::Process`: column-0 lines; formula
  targets inside postblocks and fstrsel selectors are declared in the same pass, ours). Labels are case-insensitive.
  Flag words per predefined variable (`AddVariable` +0x68 / +0x78 and whether it has a format statement) come from the
  disassembly (`mp_var_flags.tsv`); defaults from `CPostVariable::SetDefaultValues @1801b9960` (constants decoded,
  run-time ones: posting clock, wire tolerances). Aliases `op_id$`, `cc_computer$`.
- **Declarations**: `name : v` init, `name := v` read-only init (+0x68 bit 4), `name = expr` global formula re-solved on
  every read (`GetValue`/`IsGlobalFormula`); s-names are strings when the value is quoted / a string
  (`ParseDeclaration @18014bed0`). `fmt [pre] fs var [suf] [inc] [n|d|s]` (`ParseFmt @18007eda0`), `fstrsel`
  (`ParseFstrsel @180099700`), `flktbl`, `fprmtbl`, `fbuf`, `fq`; `fs`/`fs2` via mp_fmt. Built-in fs -22..1
  (`ProcessPreDefines @1800a9360`); predefined variables use fs 1 (+ default address letter fmt, e.g. `x$` "X").
- **Post table** = AST (`MpItem`): output elements with mcpost element codes 900..0x393 (modifier -> code table from
  `ProcessPostBlock @1800efeb0` case 0xf/0x10: `*` 0x385, backtick 0x386, `~` 0x387, `!` 0x389, `@` 0x38a, `prv_` 0x38b,
  `~prv_` 0x388; strings 0x38c/0x390..0x393; `'..'` 0x38d (does not count as written), `".."` 0x38e, bare number
  0x38f = ASCII char), postblock calls, formulas (`label = expr`), `if`/`while`/`else`, `[ ]` groups.
  if/while scope = rest of the postline (or up to an inline `else`); `else` at the start of the next postline pairs
  with that if. Postline continuation (ours, from the stock posts): line ends with `,` or an operator, or the next line
  starts with `& | + / ^ = < > )` (not `*`, which is the force modifier, and not when the next line is `else`).
- **Formulas**: precedence of `CPostFormula::SetPrecedenceLevels @180080c90`: unary minus, `^`, `* /`, `+ -`,
  comparisons, `& |`, then functions/`not` without parentheses (lowest). A function followed by `(` binds its group
  (`SetParenthesisLevels`). Semantics `SolveFormulaMathOperator @18008e100` (5e-8 tolerances, `fcndiv`, `pwr`, string
  `+`/`=`/`<>`), `SetValue` read-only bits. Vector/matrix results fill 3/9 consecutive variables.
- **Output** (`mp_out.c`): exact port of OutputVariable / IsModal / OutputString (fstrsel modality on the selector
  variable) / OutputUpdate / OutputToNcOnEob (a line is written only when its write count > 0; `*e$` forces) /
  PadSpacesOnLine (spaces$, no_spc$; no blank before a fmt with an empty prefix) / UpdateVariableOptions + UpdateOnLineOutput
  (no_updprv$ 0/1/2, deferred previous update for backtick) / BumpSequenceNumber (n$ += seqinc$, wraps at seqmax$ to seqno$)
  / AppendEndOfBlock (eob$/use_alt_eob$, omitcrlf$, CRLF) / subout$ streams + merge/clear/new sub,aux,ext,lcc /
  command variables (date$ "%d-%m-%y", time$ "%H:%M", day$ "%d", mon$ "%b", month$ "%m", year$ "%y", no_spc$,
  ex$, exitpost$, nci_rewind$, fleader$; NCI ones via `MpRun.cmd_hook`).
- **BlockCall** (`mp_run.c`): rotaxtyp$ > 6 -> pmx0$ first; tlchng_aft$ delayed tool change: motion postblocks
  pmx$..pcan8$ trigger psof/ptlchg/ptlchg0 (m*/l* variants by product, sof0 for tool 0) once (`mp_run_set_tlchg_kind`).

### Status
- Parse: 37 posts (11 generic + 26 shared Mcam2017); 30 clean; 5 five-axis posts warn about undeclared labels
  (enumerators `vecx/vecy/vecz` + labels nothing declares: created as variables), 2 posts have a dangling `else`
  (post bugs: Generic Fanuc 4X MT_Lathe:2321, Makino 4X Wire:801).
- Run: all 37 posts run over a mill NCI (mc-test01) with 0 interpreter run messages (5-axis posts output only the
  header: tool-plane math CPostRunFiveAxis NOT DECODED in mp_nciloop.c).
- Coverage: operators 19/19 (`: := = - ^ * / + - = > < >= <= <> & |` + unary minus); functions 116 of 143 implemented
  in mp_eval_fn.c. Added 2026-10-23 (2nd pass): xll/xla/xaa (SolveFormulaIntersection @18008b800 with CPostMath
  ln_ln_ints @1800c5b20, la_ints_new @1800c5880, aa_ints_new @1800c1c40, pt_near, t_on_line, t_on_arc), pop/push/turn
  + `fstack N size` (CPostRunStack ProcessPop @1801eec00 / ProcessPush @1801eef80 / ProcessRotate @1801ef350,
  AssignFromArray returns 1 per disassembly 0x1801ee883), getstrval/setstrval (strt_string +0x28/+0x30 =
  MpStrVar.num[2], SetStringExtendToFunctionOption @180087240), altgbl (first variable operand of A's global formula
  replaced by C when it is B), setncstr (editor settings, no NC effect). NOT DECODED: breaks (BreakLines with user
  pbreaks), t_list (tool list), fnewsub, and the UI/OS/database ones (launch dll runvbs drwlin drwarc db_* sysinfo
  opchng opmatrix opvector acomboinfo uidinfo opinfo groupinfo cdinfo mdinfo streaminfo ger).
  Per-run copy of the fstrsel table (MpRun.fstrsels): setsel* no longer modify the parsed post. qN prompts keep the
  variable's value (= the dialog default of PromptQuestion @1800ddd00; no UI).
- Arcs (2nd pass): CPostArc::LinearizeArc @180036060 (chord_tol$ segments through LegacyLinearProcess, whole arc for
  linarc$/helix refusals, per piece for CheckArc result 2 inside CircularSegment) and the CPostRunNciUtil break variables
  (brk_*$ copy/backup/zero/restore) are ported. CheckArc @180031c30 fixed: arccheck$ tens digit = end points on one axis
  line through the center FAIL the check (we skipped the rotation test and accepted the arc), full circles go straight
  to 7. CircularSegment now restores gcode$ / +0x38a on every path (a linearized quadrant piece left G1 modal for the next
  piece).
- Oracles: (a) mc-test01 + MPFAN.pst vs Test01.NC: 316/316 lines, all motion/arc/drill/tool-change blocks equal;
  differing: program name / paths in the header comments, G53 vs G54 (workofs$ from the NCI), S50 vs S1069 (1020 maxss$
  placeholder 0 in nci_ascii at the time). (Input then needed the 1001/1000 tool-change codes from a test patch,
  tools/mp_nci_fix_tlchg.py; nci_ascii writes them now and the patch was deleted 2026-10-23.) (b) TEST-MC: the source part is not available (basic-mc01.mcam has no ops).
  (c) user parts + generic_fanuc_3x_mill.pst (--metric 1) vs the custom-post NC (tools/gcode_compare.py, motion blocks
  in order; run all with `tools/mp_oracle_parts.sh`): BRACKET4 / test-cube-10mm / buckle-v8 / v9 / v9-back / v10 /
  v10-back 100% (v8/v9/v10 were 99.8% before the CheckArc/LinearizeArc fix), buckle-v7 97.7% -> 97.99% (all remaining
  differences are NCI depths / extra rapids, i.e. NCI-source, no arc/line differences left), buckle-v11/v12/v6/
  NAKTO-BRAKE-BRACKET 0-28% (block counts differ by up to 6x: the reference NC came from different toolpaths).
  Custom-post header differences expected (no '%', 'M3 S800', H0).

- 3rd pass (2026-10-23), `mp_nciloop_tt.c` (#included by mp_nciloop.c) + mp_nciloop.c / mp_run_prm.c / mp_out.c:
  - **Tool table**: CPostValidateToolChanges (ctor @1801f0a10, ValidateRectifyToolChanges @1801f0e60,
    ValidateFirstToolChange @1801f0cc0, ProcessNeedStartToolChange @1801f0c70, FilterRedundant1001 @1801f0b40,
    ValidateToolChange1000_1002 @1801f0fb0, ForceToolChange1002 @1801f0b90, ProbeToolChangeForce @1801f0c00) used in
    tool-table mode by the tool-change part of CPostStreams::BuildEnhancedToolTable @18013b100 /
    CPostToolTable::BuildAddToolTableRecord @18015bb00 (+0x38e = 0, +0x390 = force_tlchg$ at build time, force = 1020
    field 12 = CPostGcode1020 +0x88, objdump of ReadParametersFromString @18009f140), applied at read time like
    ReadNciData -> UpdateToolChangeFromTooltable @18015fed0: the NCI's 1000/1001/1002 codes are rectified as Mastercam
    does (first = 1001, then 1000 same tool / 1002 other tool when force_tlchg$ == 0). The first record's axis
    combination (strt_tooltable +0x38 = CPostGcode950 +0x38 = the 950's 4th field, objdump @1800a4290) -> MpRun.axcombo
    -> mp_cd_overrides_ac (MD rotary re-init).
  - **PPostToolList** (CPostRunComponents +0x210: reset FUN_180156890, add FUN_180156ef0 / FUN_180157190 /
    FUN_180157490 / FUN_180157620, query FUN_180157fe0 modes 0..16 from its jump table @0x18015830c, searches
    FUN_180158350 / FUN_180158410, FUN_180157740, FUN_180157b30, return code FUN_180156dd0): first_tool$ / last_tool$ /
    ntools$; CPostReadNci::GetNextTool @1800ff8d0 (early returns from the disassembly) -> next_t$ / next_tool$ /
    ttblend$; the `t_list(mode, status [, index])` function (disassembly 0x1800927c3..0x18009294a: result = tool,
    B = status code, C = index).
  - **wrtt** CPostNciPreProcess::wrtt @1800cd4b0 (pass 0 over the whole NCI: pwrtt$ per tool change with
    GetTooltableDrillCycle @1800cc3d0 (nextdc$ = the 81's 1st field, +100 below 100 for tool_op$ 16, disassembly),
    GetTooltableNextop @1800cc6c0, CheckAndSignToolUsage @1800cc0f0 (t$ negated when used before), x_max$/x_min$
    (SetMinMax @180129300; InitMinMax 1e38 after each pwrtt$), pwrttparam$ for parameters / comments / unknown codes,
    pwrtt$ at 1003 or at the end by tooltable$ tens/ones digits (UnAssembleSetGetValueHelper @180129530); pass 1
    re-reads from the start up to the read loop's record (pparameter$, pmiscint$/pmiscreal$, prd1000$ + psof00$ at the
    first tool change); restore of nextop$ nextdc$ nextcflg$ rcc_flg$ cutpos2$ mill5$ force_tlchg$ probe_active$ +
    BackupBufferBreak @180125f80 / RestoreBufferBreak @1801288b0 + bdrl_*$). rd1018_wrtt @1800cd1b0.
  - **rd1020** field 11 is a dummy (objdump 18010d26e..): force_tlchg$ = field 12, maxss$ = field 13 -> Test01 S1069.
    **rd950** @180110760 (objdump: synclath$ syncbfor$ syncmode$ syncaxis$ syncstream$ syncmodebfor$) + ReadLoop case
    0x3b6 (psynclath$). **rd1019** @18010d0a0 (esub_*).
  - **OverrideInitializeFromPprep @180111290** sub_level$ part (transform-sub mode +0x1210 from psub_*_tt$ presence,
    subprg$/auxprg$/extprg$/subout$ re-init, sub_level$ = 0 after pprep$): the CD's sub_level$ = 1 had disabled MPFAN's
    forced G54 (pwcs) -> Test01 G54 now output.
  - **rd_md$ / rd_cd$ / rd_tlpathgrp$** command variables (CommandVariableOutput -> ReadMDInfo(rd_mch_ent_no$) /
    ReadCDInfo / ReadGroupInfo; ReadCDInfoLoop @180122b90: sparameter$/prmcode$ + pmachineinfo$, rd_exit$) via mp_prm
    (mp_run_prm.c mp_run_prm_cmd). 4X posts now read rot_axis etc. from the machine (mc-test01's machine has A, B and C:
    the Generic 4X posts pick C and print their "MORE THAN 1 ROTARY AXIS" message on rotary moves - post behaviour).
  - **BreakLines** CPostProcessLinear::BreakLines @1800aa450 for LegacyLinearProcess (param_5 -1; flow checked in the
    disassembly at 0x1800aaab4: types 1..7 = chord loop L = sqrt(8 tol R) (chord_tol$), 4..6 skipped when
    CPostBreakNci::ToCenterAxis @18003cea0 says the move lies in a plane through the axis; -1 = brklineslen$ pieces,
    8..11 = brklineslen$ length; break variables Copy/Backup/ZeroOnFirstBreak/ZeroPredefined/Restore),
    BreakLinesPostCall @1800ab720 (Break_XY_Z now takes the target point). Tested with a hand-edited NCI (rotaxis field
    10 = polar on mc-test01): C-axis polar output broken into pieces; no Mastercam reference for it.
  - **CircularSegment helix split** (nobrk$ 2, disassembly 0x18003336d..0x1800339b1; the Ghidra "MapY + 1 / MapZ + 2"
    index arithmetic is real: s_MapX/Y/ZfromArcPln are offsets from x$/y$/z$, so component = offset + 0/1/2; only arc
    plane 0 qualifies): cVar19 1 = Z move first (x$/y$ held, nc_prv_modal$ 3), 2 = flat arc first (z$ held,
    nc_prv_modal$ 4) then the Z move (NcPreviousModal @180036780 = 1/2/4 per x$/y$/z$ slot). No stock post / part sets
    nobrk$ 2 with breakarcs$ 1/2: untested against Mastercam.
- Oracles after the 3rd pass: Test01 316/316 lines identical except the header (program name, date, paths); user parts
  unchanged (BRACKET4 / test-cube / v8 / v9 / v9-back / v10 / v10-back 100%, v7 97.99%).
- Validation sweep `tools/mp_sweep.sh [SCRATCH] [POST-REGEX]` (every stock post x mc-test01 + <user-parts>
  parts; table SCRATCH/sweep.tsv): 37 posts x 18 parts = 666 runs: 621 clean,
  37 "nonci" (buckle1.mcx-9 has no operations), 8 with run messages, all "string select sg4code: selector g4code = -1
  out of range" from Agie Generic AC123 4X Wire on mill parts (fstrsel underflow -1 with ss_underflow$ 0 - mcpost
  reports the same, SelectString @18009a2e0 flag 0x1000000; a wire post on mill NCI), 0 failures / crashes / timeouts.
  Parse warnings only on the five 5-axis posts (44 undeclared labels) and the two dangling-else posts.

- 4th pass (2026-10-23):
  - **Op parameters**: CPostReadNci::CallReadOpInfo @1800fca30 (`mp_run_call_read_op_info`, mp_run_prm.c) after every
    NCI 1020 in the read loop (param 0: needs a pparameter$ body) and in wrtt (param = tooltable$ command flag; pass 0
    with +0x386 = 1 -> pwrttparam$, pass 1 with +0x386 = 0 -> pparameter$), param_read$ 3 = nothing, op_id$ change
    vs +0x13e8 (xform_op_id$ / nesting_op_id$ ops first when they differ and (+0x13e2 & 0xfffd) == 0, +0x13f0 / +0x13f8;
    param_read$ 4 forces); the three ids are zeroed at each wrtt pass (wrtt @1800cd4b0). +0x13e2 = control file_ctrl +4
    (the "@+0x13e2" row of mp_cd_overrides). CPostRunMCamInfo::ReadOpInfo @1801243b0 / ReadOpInfoLoop @180124640
    (`mp_run_read_op_info`): op = the part's operation with that id (mp_prm_op), not found = message 0x2331 and nothing;
    rd_exit$ = 0; per parameter sparameter$ / prmcode$ + SetStandardFprmMethod(1) + pparameter$ (pwrttparam$ while
    +0x386) + (0). **rd_params$** (CommandVariableOutput: ReadOpInfo(rd_prm_op_no$, 2), needs a pparameter$ body).
    Result: the Generic Fanuc 3X / MPFAN tool table now gets comp_type / xy_stock / z_stock from pwrttparam$ (15346 /
    10010 / 10068): the "( Tn | ... | XY STOCK TO LEAVE - .. | Z STOCK TO LEAVE - .. )" header lines of BRACKET4,
    test-cube, buckle-v7..v10(-back) are now identical to the reference NC (v11/v12 have other tools in the reference).
    toolismetric (pparameter$ 20007 rparsngl field 11) comes from the NCI 20007 line (ReadLoopParameterOutput, already
    ported) and is 0 on every test part. mp_cli: --part also works in --call mode.
  - **Subprograms** (`mp_nciloop_sub.c`, #included by mp_nciloop.c): CPostSubsTraditional::Case1018 @180150740 /
    Case1019 @180151110, CPostReadNci::rd1018 @18010ca00 (Ghidra fails; objdump: 3rd field <= 0 = non-transform sub,
    "%i x7" into strt_non_trans_sub_data +0x1c..+0x34 (op grp trns sec totl chn inc), +0x13a0 = 1, +0x13a1/2 = 0, no
    assignment; else transform "%i x9 %lf x12 %i x5" into strt_trans_sub_data +0x1c..+0xb0 (ref grp trns sec totl chn
    inc trnstyp trnmthd m1..m9 trnsx/y/z nxt_t nxt_h nxt_tid mny_t nst_flg), +0xb4 = 1, +0xb5/+0xb6 = 0, AssignReadNci of
    the sub_*$ (sub_op_id$ = 0, sub_ref_id$ = 1st field)), CopySubLevelStructure @180151ff0 (sub_*$ / sub_org_*$, value
    slot 0 or 1 = prv_), CopySubNonTransStructure @180152840, DoTransformSubs @180152c90 via c_msng$ / c_mmlt$
    (CommandVariableOutput: level's mny_t == 0 / != 0; level -1 = message 0x2714), the three sub-level arrays +0x1218 /
    +0x1240 / +0x1268 (SetSize 10 in OverrideInitializeFromPprep, FUN_1801540a0 = SetAtGrow, FUN_180153fe0 = SetAt),
    counters +0x13c8 / +0x13cc / +0x13d0 / +0x13d4 / +0x13d8, +0x1212 (transform nesting > 1 in modes 1/2: message
    0x2711 and the extra levels skipped), CPostLatheCannedTraditional::LatheCannedOn1018 @1800aa0c0 / On1019 @1800aa1f0
    (rcc_flg$ 4..8: prcc_call_st$ / pfcc_call$ / prcc_call_end$ / pfcc_end$), SetOutputNcSubs @18011b250 (+0x13dc > 0
    suppresses NC lines in OutputToNcOnEob @1800dc620: the sub_sec_no$ != 0 copies of a transform sub).
    Test NCIs: `tools/mp_make_sub_nci.py IN.nci PREFIX` (from mc-test01's NCI: a non-transform sub around the first cut,
    and a 2-instance translate transform). MPFAN output: non-transform `M98 P1001` + O1001 .. M99 after M30 (sub stream
    merged at the end); transform `M98 P0002` twice, O0002 once (the second instance suppressed). No Mastercam reference
    output exists for them (nci_ascii writes no 1018/1019): untested against Mastercam.
  - **Header**: rd1050 / ReadLoopParameterOutput call FilenameToPredefinedString @18011d080 with the extension string
    too (str_smcext$ / str_strtoolext$ are stack arguments Ghidra drops; objdump 0x18010dcf2 / 0x180106b3b);
    OverrideInitializeFromPprep's tail: sprogname$ = snamenc$ (AssignOutputStringReInit @180125ab0, skipped when string
    flag 0x100) upper-cased when progname$ != 0. Test01 now `O0001(TEST01)` and `(MCX FILE - ...MC-TEST01.MCAM)`.
- Oracles after the 4th pass: Test01 identical except the date and the two paths; user parts unchanged (BRACKET4 /
  test-cube / v8 / v9 / v9-back / v10 / v10-back 100%, v7 97.99%) and their tool-table header lines now equal.

### NOT DECODED (interpreter)
enumerators (`CPostVariable::EnumPreDefineVar` @180161390: Ghidra decompile timed out - needs the disassembly;
AddEnumerator/CheckAndAddEnumerator/FindEnumeratorLabel are simple list ops), postblock arguments (parse
CPostPostTable::ProcessPostBlockArgument @1800f24a0, run CPostRunPostTable::TransferBlockArguments @18012b360 /
RemoveBlockNonArguments @18012b1b0: definition items 0x3f0..0x3f3, call items 0x3ef / 0x3f1 / 0x3f3 / formulas
0x7d6/0x7d7 copied into the definition's variables, read-only bits 4/8 respected; no stock post uses them),
string formats (`fmt` on a string), linked variables (linklvar$ ...), LegacyUpdateOnXr, incremental companion variables
(+0x58, OutputVariableIncremental), TranslateNcString (transchr$), comment buffer mode (+0x115e bits 1/2), probe /
transform-subprogram output suppression, CommandVariableOutput for c_msng$/c_mmlt$/rd_*/db_*/probe commands, fstrsel
modes 4/8 (sub string / formula chains) and the underflow walk, setseloffs's SelectStringValue (strings get no numeric
value from fstrsel yet), flktbl string first column, pop/push with mode -1 (fstack <-> fbuf), exact line-continuation
rules of CPostFindVars, creation point of undeclared labels. NCI loop: BreakLines for gnci$ 11 (BreakLinesPostCallUVW @1800ab8b0) and the breaks() function (param_5 0/1, pbreaks_in$ /
pbreaks_out$, BreakLinesOperationPostCall @1800ab5a0, xresult$/t1$/t2$), CPostRunFiveAxis (setmap/toolpl/load_matm/
fiveax), SetArcMinMax's quadrant points in wrtt (GetQuadCount / MapToCompatibleArcPlane; only the XY end point is used),
wire branches of wrtt (rd1w/rd2w) and rd1020, probing (+0x1150 taken as 0 everywhere: PostSupportsProbe = 0),
GetMcamPostReturnMode (taken as 0), uselstation$ in the tool list (FUN_1801570b0), the rest of the enhanced tool table
(stream info, lathe canned data, CPostToolTable queries for opinfo/opchng...), pdump$ reads of op parameters (no caller found),
listtpr$/listrad$ (FUN_180157b50 / FUN_180159590: the wire taper / radius list comes from MCPost::GetRunPostOperations()
+0x48, a Mastercam-side map not decoded), subprograms of the transform modes 3/4 (psub_*_tt$ posts): per-level sub
files CPostSubsTraditional::FileNewSub @180153580 / CreateLevelCfile @180152b10 / MergeSubFiles @180153d60 /
ClearAndRemoveCFile @180151f50 (strt_trans_sub_data +0x00 / +0x08 = their CFile pointers, kept opaque), fnewsub,
mergesub/aux beyond mp_out's streams, nci_rewind$ beyond re-running the loop.
Enumerators: a declaration label ending in '@' is an enumerator (CPostStringUtil::ReadLabel: kind 2, '$' = kind 1) ->
CPostFindVars::Process -> CheckAndAddEnumerator @180161170 / AddEnumerator @180160d80 (list CPostComponents +0x248,
value = count + 1), looked up by FindEnumeratorLabel @18004b2a0 in formulas. No .pst of the 37 declares one; the 5X
posts' vecx/vecy/vecz (and pfd_shft_inc, p_absinc_chng, ...) come from their encrypted .psb (out of scope), so the
undeclared-label warnings stay. EnumPreDefineVar @180161390 is NOT the enumerators: it numbers the predefined variable
slots (var_v_undefined$ 0, var_x$ ...), which mp_variables.tsv already gives. CPostRunFiveAxis (setmap / toolpl /
load_matm / fiveax / sturtz / find_next_5ax ...) and BreakLinesPostCallUVW @1800ab8b0 (x/y/z + u/v/w, gcode$ 11,
rotaxtyp$ >= 6: vtoolx$ = unit(uvw - xyz), 6 -> pmx$, else plin0$ + LegacyLinearPostCall; < 6 -> fiveax): not
ported - every stock 5-axis .pst (Generic Fanuc 5X Mill / Router, Haas VF-TR 5X) is the front of a .psb and cannot be
run meaningfully; breaks() (no stock post calls it).
maxss$: mcpost sets it ONLY from 1020 field 13 (rd1020 objdump; field 11 is a dummy) and the lathe 950 table
(SetNciArrayEnum @180119780); no default, no MD/CD override. Fixed in the interpreter's rd1020 (3rd pass).

### How to resume
`make -C viewer/mp mp_parsecheck && viewer/mp/mp_parsecheck <posts>`; engine test without NCI:
`viewer/mp/mp_cli POST.pst OUT.nc --call pheader$,psof$ --set t$=2 ...`; full run: `mp_cli FILE.nci POST.pst OUT.nc
[--part PART]` (MD/CD overrides + op parameters from the part, `mp_run_prm.c`) or `mp_cli PART.mcam POST.pst OUT.nc`.
Oracles: `tools/mp_oracle_parts.sh [SCRATCH]` (Test01 + the user parts table, capped; nci_dump's NCI as written). App
path: `tools/tests/mpint_cmp.sh OUTDIR` (section "Integration (app)"). Debug: `MPNCI_DEBUG=1` prints every arc decision (res/brk/helix/sweep).
Next (priority): transform modes 3/4 sub files (FileNewSub / MergeSubFiles), lathe canned cycles
(CPostLatheCannedTraditional, CPostThread) with a lathe NCI, CPostRunFiveAxis only if a plain-.pst 5-axis post appears,
postblock arguments. Disassembly: `objdump -d --start-address=0x... ~/mastercam-re/files/mcpost.dll` + symbol
annotation from ~/mastercam-re/work/mp/mcpost_syms.tsv (stack arguments Ghidra drops, e.g. rd1050's smcext$).
