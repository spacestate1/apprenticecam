/* cam_postvals - op dialog batch 11 (2026-10-24): the post-side values of an operation - Coolant (mccoreres 6546 CoolantPage),
 * Canned Text (6548 CannedTextPage), Misc Values (6555 MiscellaneousValuesPage) - as Mastercam keeps them in the op record and
 * passes them into the binary NCI (mccore bnci_wr_sof -> bnci_wr_1011 / 1012 / 1025 / 1002). Decompiles: ~/mastercam-re/work/b11
 * (core_b11.c = mccore, mdef_cool.c = mcmachinedef) copied to ../decomp/b11/; notes formats/opdlg_port_audit.md 17. */
#ifndef CAM_POSTVALS_H
#define CAM_POSTVALS_H
#include <stddef.h>
#include <stdint.h>
#include "mcam_load.h"

/* record offsets (record = operation + 0x68: record +0x6c = op +4 = the opcode) */
#define PV_COOL_OLD   0x398   /* op +0x330 s16: V9-style coolant word (bnci_wr_1002 -> 1000-1002 +0x48; 8 off, 0x10 flood, 0x20 mist, 0x40 thru) */
#define PV_CAN_ON     0xb50   /* op +0xae8 u8: canned text present (bnci_wr_sof passes it to bnci_wr_1025) */
#define PV_CAN        0xb52   /* op +0xaea 20 x s16: canned text + X-style coolant entries (bnci_wr_1025 +0x18) */
#define PV_MISC_SET   0x7e8   /* op +0x780 u8: 0 = "Automatically set to post values" (bnci_wr_1011 / 1012: CMiscInit::InitToPostText) */
#define PV_MISC_INT   0x7ec   /* op +0x784 10 x s32 (bnci_wr_1012 +0x18) */
#define PV_MISC_REAL  0x818   /* op +0x7b0 10 x f64 (bnci_wr_1011 +0x18) */
#define PV_HOME       0x8d8   /* op +0x870 p_3d: home position (ReferencePointsPageBase::InitPage) */

/* ---- the machine's coolant definition (CCNCMachine, mcmachinedef.dll; McamFile.mach_cool_*) */
/* the machine of a part: F itself when its machine record was read, else the front end's fallback (core_ui: the operation defaults
   library's machine, cui_toolpage.c tl6410_mach - the base a new part is saved from), else F */
void pv_set_machine_fallback(const McamFile *(*fn)(void));
const McamFile *pv_machine(const McamFile *f);
int pv_cool_frompost(const McamFile *f);                         /* UseCoolantFromPost: 1 = V9 style (op word PV_COOL_OLD) */
int pv_cool_on_cmd(const McamFile *f, int k, char *out, size_t n);   /* CoolantOnCmd: "base on" or none (0) */
int pv_cool_off_cmd(const McamFile *f, int k, char *out, size_t n);  /* CoolantOffCmd */
int pv_cool_enabled(const McamFile *f, int k);                   /* IsCoolantOptionEnabled(k) */

/* ---- the Coolant page's state: per option k (0 Flood, 1 Mist, 2 Thru-tool, 3-9 Custom option 1-7) the state 0 On, 1 Off,
   2 Ignore (CoolantPageBase DAT_1812d6a30 + 4k) and the position 0 Before, 1 With, 2 After (DAT_1813c793c + 4k) */
typedef struct { int16_t state[10], pos[10]; } PvCool;
void pv_cool_decode(const McamFile *f, const int16_t can[20], int16_t oldw, PvCool *c);   /* SetDialogData */
/* UpdateCannedText (+ MapCoolantToCannedText): CAN / OLDW / CANON updated as Mastercam's page writes them */
void pv_cool_encode(const McamFile *f, const PvCool *c, int16_t can[20], int16_t *oldw, uint8_t *canon);

/* ---- Canned Text page: CannedTextPageBase::SplitCannedSelect - the entries of each list (before / with / after) in order */
int pv_canned_split(const int16_t can[20], int which, int16_t *out);   /* which 0 before, 1 with, 2 after; returns the count */
/* the text of an entry (LoadListBefore / With / After): canned option n -> the post text [canned text] n; X-style coolant
   (|v| 3101-5120) -> the machine's coolant command [inferred: the global text table DAT_18137ce60 is filled elsewhere] */
void pv_canned_label(const McamFile *f, int16_t v, char *out, size_t n);

/* ---- the post text (CPostText page 0x25 [misc integers], [canned text]). Ours: the part's own control text is compressed (NOT
   DECODED, as nc_post.c's drill text); the stand-in is the [CTRL_MILL|DEFAULT] text of Generic Fanuc 3X Mill.pst / MPFAN.pst
   (shared Mcam2017 mill/Posts, the posts the user's parts name; both hold the same text) */
const char *pv_misc_text(int idx, int *has);   /* entry idx (ints 1-10, reals 12-21): its description; *has = SetDlgControlTextFromIndex */
double pv_misc_value(int idx);                 /* GetMiscIntValueFromIndex / GetMiscReal*ValueFromIndex: the "//v" default, else 0 */
const char *pv_canned_text(int n);             /* [canned text] n, NULL = none */

/* ---- a stored operation's NCI with its post values (ours: Mastercam regenerates the op - bnci_wr_sof rewrites exactly these
   records from the op; here the stored section is copied and the records rewritten in place): GROUPS = PVG_* bits of the
   values edited; the record fields come from F.ops[index].raw. 1 = done (F.ops[index].nci holds the section), 0 + MSG = not */
enum { PVG_MISC = 1, PVG_CAN = 2, PVG_COOL = 4 };
int pv_nci_rewrite(McamFile *f, int index, int groups, char *msg, size_t msgn);
/* the post-side record ranges (pp_ok's "post values only" test, the saver's record copy) */
int pv_is_post_field(int off, int n);
#endif
