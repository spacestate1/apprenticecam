/* lathe_newop.h - a NEW Lathe Face operation as Mastercam 2017 makes it from the Toolpaths manager menu 3680 / the Turning
 * ribbon (mclathe.dll lathe_face 0x1801a3290): InitOperation's record, the tool page's tool pick, the Face page's Finish Z.
 * Spec: ../formats/lathe.md section 8 (every step cites its function). The dialog (core_ui/cui_lface.c) and the generator /
 * the save (cam2d.c gen_lathe_face, mcam_saveops.c) build the record with lfn_record from the op's CamParams. */
#ifndef LATHE_NEWOP_H
#define LATHE_NEWOP_H
#include <stddef.h>
#include <stdint.h>
#include "mcam_load.h"
#include "cam2d.h"

#define LFN_REC 0x2938                  /* the OP_ID record (ent), 2017 layout */
#define LFN_OP 0x68                     /* op +X = record +X + 0x68 */

/* the part's lathe tools the tool page lists (CLtlPgCmn::InitToolList 0x1801ac400): indices into f->ltl, in list order */
int lfn_tools(const McamFile *f, int *idx, int max);
/* the active machine group (the last machine group, mcam_load.c stock_grp) as a lathe group: its McGrpLathe, NULL + WHY */
const struct McGrpLathe *lfn_lathe_group(const McamFile *f, char *why, size_t whyn);
/* lathe_face's checks before the op is made (the menu item / ribbon command): 1 = a new Face can be started, else 0 + WHY
   (the text the app shows: what is refused and why) */
int lfn_can_start(const McamFile *f, const CamOp *const *camops, int ncamops, char *why, size_t whyn);
/* the record of a new Face: InitOperation(0x42) + the tool pick of LTL index LTL (-1 = none) + the planes / dialog values of P
   (P NULL = the active planes PL_* given, no patches). *REC malloc'd (LFN_REC bytes). Also the Tplane the tool pick set
   (SetOpDefaultTplane) in *TPLANE (plane id, 0 = left as the active one). 1 = made, 0 + ERR (NOT DECODED / refused) */
typedef struct { int tplane, cplane, wcs; double torg[3], corg[3], worg[3]; } LfnPlanes;
int lfn_build(const McamFile *f, int ltl, const LfnPlanes *active, const CamParams *p, uint8_t **rec, int *tplane, char *err, size_t errn);

/* ---- the tool page's machine checks (L-12, formats/lathe.md 9): spindle speed / feed limits of the machine definition.
   The fields are the record's: rpm |op +0x328| (mode 1 RPM when op +0x32d == 0, else 0 CSS), max op +0x548, feed |op +0x310| (mode
   2 when op +0x32e, else 0 per rev for a negative feed, 1 per minute), finish feed op +0x13a0 (mode: the page's radio index),
   finish rpm op +0x13ac (mode 1 when op +0x13b0 != 1). The page state CLToolPropBase / ToolPgPrm keeps: */
enum { LFS_FEED = 1, LFS_FFEED = 2, LFS_RPM = 4, LFS_FRPM = 8, LFS_MAX = 16 };   /* the fields shown in italics (WM_SETFONT +0x2b780 / ToolPrmBase +0x48) */
typedef struct {
    long prm34;          /* ToolPgPrm +0x34 (MaxSpindleSpeed): the max UpdateToolFeedInfo checks the tool's rpm against */
    int ffeed_mode;      /* page +0x26e30: the finish feed radio index (0 per rev, 1 per minute, 2 micro-in) */
    unsigned italic;     /* LFS_* */
    int raw_rpm, raw_css; double raw_feed;   /* the tool's rpm / CSS / feed (GetToolFeedInfo) before UpdateToolFeedInfo's checks */
    int lock;            /* ToolPgPrm +0x2928 = mccore EnableFeedRateLock: config 17027 "Lock Feedrates?" (ReadCfgFile setter FUN_1801bac10
                            case 0x4283; default true, FUN_1803bc8e0); MCAMUI_TEST_CFG17027 (ours, tests) stands in for the file's value */
    int edited;          /* ToolPgPrm +0x2929: (newOp == 0) at the ctor; 1 after the feed / finish feed / rpm / finish rpm edits (OnKillfocus*) */
} LfnSpeedSt;
/* lfn_build with the page state: CHECKS 1 = the dialog's opening (MapToDlg's clamps, the spindle check of the tool pick,
   UpdateToolFeedInfo; the record lfn_build gives), 0 = none (the tool pick's raw values; *ST still filled) */
int lfn_build_st(const McamFile *f, int ltl, const LfnPlanes *active, const CamParams *p, uint8_t **rec, int *tplane, LfnSpeedSt *st,
                 int checks, char *err, size_t errn);
/* CLToolPropBase::RunMachineDefChecks 0x180265a60 (an edit / radio of the tool page's feeds and speeds): ValidateSpindleSpeeds(false,
   true) + ValidateFeedrates on REC. *CHANGED = a value was changed (then Mastercam shows "Tool Settings Modified", string 2424).
   0 + ERR = refused (a limit NOT DECODED) */
int lfn_machine_checks(const McamFile *f, uint8_t *rec, LfnSpeedSt *st, int *changed, char *err, size_t errn);
/* a tool picked again in the list (CLToolPropBase::OnClickToolbmpsLb 0x180263460): REC = the new tool's record (lfn_build_st
   checks 0) with the dialog's kept fields from OLD (the previous record); the spindle check (CLtlPgCmn::GetToolProperty),
   UpdateToolFeedInfo and ValidateFeedrates. 0 + ERR = refused */
int lfn_repick_checks(const McamFile *f, const uint8_t *old, uint8_t *rec, LfnSpeedSt *st, char *err, size_t errn);
/* "Tool Settings Modified" (dialog 3275, CToolModWarning 0x1801a9100): string 2424 */
extern const char LFN_TOOLMOD_TEXT[];
/* the record of a new Face op made here (CamParams.lf_new): lfn_build with its tool slot, planes and record patches */
int lfn_record(const McamFile *f, const CamParams *p, uint8_t **rec, char *err, size_t errn);
/* the LTL index of tool slot SLOT, -1 none */
int lfn_ltl_of_slot(const McamFile *f, int32_t slot);
/* the Face page's Finish Z when it opens (CLFaceProp::OnInitDialog -> InitZFinEd(1e30), lathe_face_z_init) and the edit's
   range; 0 + ERR when there is no stock ("Use stock" greyed, SetRestCtrls) */
int lfn_finish_z(const McamFile *f, const uint8_t *rec, double *z, double *lo, double *hi, char *err, size_t errn);

#endif
