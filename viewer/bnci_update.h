/* bnci_update - Mastercam 2017's BnciUpdate (mccore): the NCI patch of an operation whose dialog changed only feeds / spindle speed /
 * coolant / D / H offsets / program and sequence numbers - its toolpath is kept (user decision D2; bnci_update.c for the sources). */
#ifndef BNCI_UPDATE_H
#define BNCI_UPDATE_H
#include <stddef.h>
#include <stdint.h>
#include "mcam_load.h"

typedef struct { long records; long minarc; } BnciuStats;   /* records rewritten; feeds raised to MinArcFeedRate */

/* SEC = an op's NCI section (u16 size + record each, file format), patched in place. OLDOP / NEWOP = the operation before / after the
   dialog (record + 0x68). MINARC = MinArcFeedRate when the machine's AdjustArcFeedrates is on, else -1. 1 = patched (or nothing to
   patch), 0 = Mastercam would refuse (DontAllow) or a case NOT PORTED (ERR): the op is marked dirty instead. */
int bnciu_patch(uint8_t *sec, size_t n, const uint8_t *oldop, size_t oldn, const uint8_t *newop, size_t newn, double minarc,
                BnciuStats *st, char *err, size_t errn);
/* the app: stored op INDEX of F gets NEWREC (its OP_ID record with the dialog's values) and its section patched (mcam_op_set_nci) */
int bnciu_apply(McamFile *f, int index, const uint8_t *newrec, uint32_t newn, double minarc, BnciuStats *st, char *msg, size_t msgn);
#endif
