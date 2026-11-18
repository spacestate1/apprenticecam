/* nci_reverse - Toolpaths manager "Reverse toolpath" (Operations Manager command 3582, COpMgrTree::ReversePath(1) ->
 * mccore bnci_reverse -> rev_core) on the binary NCI of an operation stored in the part. ../formats/reverse_toolpath.md */
#ifndef NCI_REVERSE_H
#define NCI_REVERSE_H
#include <stddef.h>
#include <stdint.h>
#include "mcam_load.h"

/* bnci_reverse(&id, copyMode 0, toggleComp 1, markEdited 1, p5 1) on F.ops[index]:
   1 = reversed (F.ops[index].nci holds the rewritten section, its polylines are rebuilt, record +0x148 = 1 / +0xfc = 0);
   0 = refused or failed, nothing changed (MSG says why; the subprogram refusal is Mastercam's message 2911);
   -1 = skipped silently, as Mastercam does (multiaxis operation). */
int nci_reverse_op(McamFile *f, int index, char *msg, size_t msgn);
/* the subprogram message (mccoreres 2911), for the caller's warning box */
#define NCI_REV_SUBPGM_MSG "Editing an operation that has sub-programming enabled is not allowed."

/* rev_core on one op section: NCI = *pcount records of 0x370 bytes (may be reallocated and grow); OPS / NOPS = the part's
   operations (DatabaseRetrieve by id), CUR = the op being reversed (cur_op). 1 = ok; 0 + WHY = Mastercam leaves the op. */
int nci_rev_core(uint8_t **pnci, int *pcount, const Op *ops, int nops, const Op *cur, int metric, int toggle_comp,
                 int use_max_z, char *why, size_t whyn);
#endif
