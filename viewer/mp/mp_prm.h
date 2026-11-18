/* mp_prm - the Mastercam-side data a post reads besides the NCI stream (port of mcpost.dll CPostRunMCamInfo /
 * CPostMachineInfo::InitialAssignFrom* and mcmpparameters.dll PrmMachine / PrmControl / PrmGroup / PrmAppMPParameter).
 * Spec + call-order rules: ../../formats/mp_engine.md "MD / CD / group / op parameters (mp_prm)".
 *
 * Every parameter list is delivered the way CPostRunMCamInfo::ReadCDInfoLoop @180122b90 (and its MD / group / op twins)
 * does it: one callback per pmachineinfo$ (pparameter$ / pwrttparam$ for ops) call with prmcode$ and sparameter$:
 *   header (code, string)  ->  every numeric param, ascending code ("%.15lg")  ->  every string param, ascending code
 *   ->  footer (19999, "%.15lg" of its value).
 * The interpreter sets rd_exit$ = 0 before calling (ReadCDInfoLoop does), assigns prmcode$ / sparameter$ in the callback
 * and calls the postblock. */
#ifndef MP_PRM_H
#define MP_PRM_H
#include "../mcam_load.h"
#include <stddef.h>

/* One pmachineinfo$ / pparameter$ call. Return 0 to go on, 1 when rd_exit$ != 0 after the call (the remaining params
   are skipped, the footer is still delivered - ReadCDInfoLoop), -1 when the postblock call failed (BlockCall returned
   0: the read stops at once, no footer). (The coordinator's void callback became int: the loop tests rd_exit$ and the
   BlockCall result after every call.) */
typedef int (*MpPrmCb)(void *ctx, int prmcode, const char *sparameter);

/* return values of the readers: 1 = read (Mastercam's ReadXxxInfo returned 1), 0 = Mastercam fails here too (no
   machine / control / group / op: CPostMessages 0x2331 "...", or the postblock call failed), -1 = NOT DECODED in this
   port (nothing delivered) */

/* rd_md$ (CPostRunMCamInfo::ReadMDInfo @180123460, param_1 = rd_mch_ent_no$): 0 = the machine base (PrmMachine,
   header 19960 "MACHINE"); -1 = base without footer, every axis combination, every component, one footer; -2 = the
   combination / component named by sparameter$; > 0 = an axis-combination id (e.g. syncaxis$ = the NCI 950 id): its
   post-circuit components (header 19960 "<TYPE>_COMPONENT", 19959 = component id, 19958 = component type, params; one
   footer after the last), or a component id (header, 19959, 19958, params, footer); < -2 = the combination's own
   params (19960 "MACHINE_AXIS", 19959 = id, 19958 = -1) or that component. An id not found: 1 with no calls.
   mp_prm_md = mp_prm_md2(f, ent, 0, NULL, ...). dump = the read is a dump read (pdump$; CPostRunMCamInfo +0xec: a
   combination id > 0 then delivers the combination's own params). -1 = NOT DECODED (lathe / wire machines, mapped
   axis combinations: see formats/mp_engine.md). */
int mp_prm_md(const McamFile *f, int rd_mch_ent_no, MpPrmCb cb, void *ctx);
int mp_prm_md2(const McamFile *f, int rd_mch_ent_no, int dump, const char *sparameter, MpPrmCb cb, void *ctx);
/* debugging: the decoded machine tree / axis combinations as text (returns the length) */
int mp_md_info(const McamFile *f, char *buf, size_t n);
int mp_prm_cd(const McamFile *f, MpPrmCb cb, void *ctx);              /* rd_cd$  (ReadCDInfo @180122ac0, PrmControl) */
int mp_prm_group(const McamFile *f, MpPrmCb cb, void *ctx);           /* rd_tlpathgrp$ (ReadGroupInfo @180122ea0, PrmGroup) */
/* op parameters of F.ops[op_index] (ReadOpInfo @1801243b0 -> PrmAppMPParameter(operation): read from the operation
   record, NOT from the NCI). Header 10000 + the op type name, ParseOperation's common params + the opcode-specific
   union + 15773 / 15683, footer = SetPathType's error bit; the 20000-series params come from the NCI, not from here.
   Gaps: mp_engine.md. */
int mp_prm_op(const McamFile *f, int op_index, MpPrmCb cb, void *ctx);

/* CPostRun::OverrideInitializeFromMDandCD @1801111a0 = CPostMachineInfo::InitialAssignFromMD @1800b8a80 then
   InitialAssignFromCD @1800b32b0: post variables re-initialised from the machine / control definition before the run.
   Apply in order with CPostRunNciUtil::AssignOutputVariableReInit @180125cb0 semantics: skip a variable whose
   post-table flag 0x1000 is set; otherwise AssignOutputVariable(2, value). */
enum {
    MPO_ADV = 1,        /* only when x_mp_adv_func$ != 0 (InitialAssignFromMD's whole body is inside that test) */
    MPO_DIRECT = 2,     /* written directly (*GetVarRef = value), not through AssignOutputVariableReInit */
    MPO_IFPREV = 4,     /* only when the previous entry was assigned (AssignOutputVariableReInit returned 1: eob$[1]) */
    MPO_CHECK0x1000 = 8,/* written only when the variable's flag 0x1000 is clear (comm_filter$, sub_level$) */
    MPO_COMMFILTER = 16,/* comm_filter$: UnAssembleCommentFilter(current) -> set output argument `arg` (1-based of its 10
                           outputs) to value -> AssembleCommentFilter (CPostComponents) */
    MPO_INTERNAL = 32,  /* not a post variable: a CPostRunComponents field (var = "@+0x13e1" etc.) */
    MPO_MAX749 = 64,    /* after assigning: if the value >= 750 it becomes 749 (maxnccomment$) */
    MPO_MIN1 = 128,     /* after assigning: if the value < 1e-12 it becomes 1.0 (peckacel$) */
};
typedef struct {
    const char *var;    /* MP name with '$' (index into the variable array: [1] = eob$ element 1) */
    double value;
    const char *source; /* cite: struct / control page + offset, mcpost function */
    int flags;          /* MPO_* */
    int msg, bit;       /* log message (CPostMessages::AddRunGeneralMsg id 0x2329.., flag bit) printed when the value
                           changed by more than 1e-12 and mp_cd_quiet() == 0; 0 = none. The text in TEST-MC.err:
                           "CONTROL DEFINITION - - Post variable 'X' was re-initialized from A to B" */
    int arg;            /* MPO_COMMFILTER argument position */
} MpOverride;
/* fills out[0..max-1] in Mastercam's order, returns the count (all of them even when > max), -1 = no machine / control
   (Mastercam: message 0x2331 and nothing assigned). product: unused (kept for the interpreter's call). */
int mp_cd_overrides(const McamFile *f, int product, MpOverride *out, int max);
/* InitialAssignFromMD's rotary part (before its feed rows; MPO_ADV | MPO_DIRECT: plain writes, no flag test, no
   message): matb1$[0..8] (the machine base matrix), pri_ / sec_ limlo$ limhi$ intlo$ inthi$ limtyp$, rotaxis1/2$,
   rotdir1/2$, nut_ang_pri$ for `axcombo` = the axis-combination id of the first tool-table record (strt_tooltable
   +0x38, built from the NCI by the interpreter). Returns the count, 0 for combination 0 / unknown, -1 = NOT DECODED
   (the combination has fewer than two linear axes). mp_cd_overrides_ac = mp_md_rotary then mp_cd_overrides. */
int mp_md_rotary(const McamFile *f, int axcombo, MpOverride *out, int max);
int mp_cd_overrides_ac(const McamFile *f, int axcombo, MpOverride *out, int max);
/* file_ctrl +0xd4a (control page 2 ent +0xdca): non-zero = the "re-initialized" messages are not logged */
int mp_cd_quiet(const McamFile *f);

/* the control's NC output sequence numbers (page 8 nc_output_ctrl +0 / +8 / +0x10 = seqno$ / seqinc$ / seqmax$) */
int mp_cd_seq(const McamFile *f, double *start, double *inc, double *max);

/* debugging: the raw records found (0 = missing) */
typedef struct { int mach, ctl, pages, grp; } MpPrmFound;
MpPrmFound mp_prm_found(const McamFile *f);
#endif
