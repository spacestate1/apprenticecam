/* mcam_grp.h - the toolpath group block that follows the DB_ID sub 0xF record in a part file (X9 / 2017): u32 count, then per group its
 * 0x1ae0-byte op_group record followed by (record +0x110, i32) member entries of 0x17 bytes each. TpGrpList::WriteFileGroups (mccore
 * 0x180901d40, decomp/mccore_sections.c 1934-2190: the record SysFile::Write(.., 0x1ae0), then _Src[0x44] entries of 0x17 bytes for the
 * current version) / TpGrpList::RetrieveFileGroups (0x1808ff7c0, l.1636-1639 of the same file: SysFile::Read(.., 0x17) per entry). A
 * group with members makes the block longer than count * 0x1ae0 (16 of the 2017 corpus parts: the memory table's DB_ID sub 0xE record
 * follows the members). Header only (ours). */
#ifndef MCAM_GRP_H
#define MCAM_GRP_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MCAM_GRP_REC 0x1ae0
#define MCAM_GRP_MEMBER 0x17

/* the block at P (AVAIL bytes from there): its length, 0 when it does not fit; *NRECS (NRECS may be NULL) = the group count, RECS (may be
   NULL) the first MAXRECS group records */
static inline size_t mcam_grp_block(const uint8_t *p, size_t avail, const uint8_t **recs, int maxrecs, int *nrecs) {
    if (nrecs) *nrecs = 0;
    if (avail < 4) return 0;
    uint32_t ng; memcpy(&ng, p, 4);
    size_t off = 4;
    for (uint32_t k = 0; k < ng; k++) {
        if (avail - off < MCAM_GRP_REC) return 0;
        const uint8_t *g = p + off; int32_t nm; memcpy(&nm, g + 0x110, 4);
        if (nrecs) { if (recs && *nrecs < maxrecs) recs[*nrecs] = g; (*nrecs)++; }
        off += MCAM_GRP_REC;
        if (nm < 0 || (size_t)nm > (avail - off) / MCAM_GRP_MEMBER) return 0;
        off += (size_t)nm * MCAM_GRP_MEMBER;
    }
    return off;
}
/* Part files newer than Mastercam 2017 (decision FV-1): refused, with Mastercam's own message. The file major is the header entity's
 * +0x6a byte (get_file_version -> FUN_18006f890, decomp/mccore_dbread.c:8-110: u32 0x98, class 0x200, +0x68 == 8; l.80 returns +0x6a),
 * reported by GetMastercamFileVersion(M) (decomp/mccore_fileio.c:2105-2206). Mastercam's own is current_version[2] (DAT_18143d54a,
 * set to 0x13 - minor 0x45 - by FUN_1808c4ce0, decomp/units/core_units.c:834). A larger file major is refused:
 *   DoFileOpen (mccore 0x1803475b0, decomp/mastercam_filedlg.c:850-857): post_warning(mccoreres 5280) and returns failure; nothing
 *     was read (get_db is not called), the current part stays;
 *   the Open dialog (DoFileDialog 0x180342110 with the open flag, called by OpenFile l.2818 / PromptForFileToOpen l.2408; l.413-424):
 *     MCMessageBox(5280 + "\n" + 5301, MB_OK) and the dialog opens again (its loop), so another file or Cancel.
 * Nothing else is offered (no conversion; the Save As dialog's "newer release" prompt 5515 / 5517, l.381-395, is for overwriting).
 * The threshold here is FV-1's "file major 0x16 and up" (formats/user_decisions.md), not Mastercam's 0x13: the 2018 parts (major
 * 0x14, read with their measured layout - formats/transform_toolpath.md, the drafting corpus tests) keep opening; Mastercam 2017
 * itself refuses them too (MCAM_CUR_MAJOR). Header only (ours): every reader that opens a part file by its path checks it. */
#define MCAM_CUR_MAJOR 0x13         /* Mastercam 2017's own file major (current_version[2]) */
#define MCAM_REFUSE_MAJOR 0x16      /* FV-1: refused from this file major up */
#define MCAM_NEWER_MSG "The part file cannot be loaded because it is from a newer release of Mastercam. A new part file will be opened instead."   /* mccoreres 5280 */
#define MCAM_NEWER_SELECT "Please select a different file."                                                                                       /* mccoreres 5301 */
static inline int mcam_part_newer(const uint8_t *d, size_t len) {     /* 1 = a part header whose file major is refused (FV-1) */
    if (len < 4 + 0x6c) return 0;
    uint32_t hl, cls; uint16_t sub; memcpy(&hl, d, 4); memcpy(&cls, d + 4, 4); memcpy(&sub, d + 4 + 0x68, 2);
    return hl == 0x98 && cls == 0x200 && sub == 8 && d[4 + 0x6a] >= MCAM_REFUSE_MAJOR;
}
#endif
