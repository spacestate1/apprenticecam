/* nci_write - Mastercam 2017's binary NCI writer (mccore bnci_wr_* / bnci_write_to_disk) for operations made here: an
 * operation's OP_ID record + the engine's moves -> the op's NCI section (-2 .. -3 records, the part-file format
 * nci_ascii.c / nci_reverse.c read). Sources and the record layouts: nci_write.c, ../formats/nci_writer.md. */
#ifndef NCI_WRITE_H
#define NCI_WRITE_H
#include <stddef.h>
#include <stdint.h>
#include "mcam_load.h"
#include "cam2d.h"

typedef struct { uint8_t *b; size_t n, cap; } NciwBuf;   /* section bytes: u16 size + record, as in the part file */

/* the part file's records the writers read (ASSOC 0x4000 records, the toolpath / machine groups, the planes archive) */
typedef struct NciwPart NciwPart;
NciwPart *nciw_part_load(const char *path);               /* NULL path / unreadable file: an empty index (never NULL) */
void nciw_part_free(NciwPart *P);
/* the TL_ID record (ent image, record +0x68 = tp_tool) of tool slot SLOT (record +0x2cc), NULL if none */
const uint8_t *nciw_part_tl(const NciwPart *P, int32_t slot, uint32_t *n);

typedef struct {
    const uint8_t *rec; uint32_t recn;   /* the OP_ID record image (operation = rec + 0x68) */
    const uint8_t *tl; uint32_t tln;     /* its tool's TL_ID record image (NULL: the part's record of slot op +0x544) */
    const McamFile *f;                   /* the loaded part (machine / control settings), may be NULL */
    int metric;                          /* IsMetric() (1016 +0x44) */
    long base;                           /* the file offset the section starts at (the records' +0x10 positions) */
    const uint8_t *tp; uint32_t tpn;     /* drill ops: the op's tp entity records (u32 length + record, the save's order); NULL:
                                            the part's records whose +0x68 is the op id (file order) */
} NciwOp;

typedef struct {
    long records;                        /* records written */
    long not_decoded;                    /* values written with a stand-in (see nci_writer.md "NOT DECODED"); counted */
    char note[400];                      /* the first few of them */
    long eos_off;                        /* byte offset of the -3 record in the section (the op's +0x84 = base + eos_off) */
} NciwStats;

/* Writes the whole section of one op: -2 (bnci_wr_sos), the section-start block (bnci_wr_sof: 1011 .. 1002), one record per
   move (bnci_wr_lin / bnci_wr_arc, 1004 after a cutter-comp end), -3 (bnci_wr_eos). M[0] is the first position (the tool
   change's +0x50 and the first rapid). Moves in op coordinates. Returns 1 ok, 0 refused (err). */
int nciw_section(const NciwPart *P, const NciwOp *o, const Move *m, int nm, NciwBuf *out, NciwStats *st, char *err, size_t errn);

/* the -2 record and the section-start records alone (bnci_wr_sos + bnci_wr_sof(op, first, ..)), for the oracle (tools/tests/nciw_oracle.sh) */
void nciw_sof(const NciwPart *P, const NciwOp *o, const double first[3], NciwBuf *out, NciwStats *st);

/* Mastercam's record size of a gcode (bnci_bytes @18003a800, includes the u16), 0 = unknown */
size_t nciw_bytes(int gcode);

/* Q24: the NC file Mastercam's post writes for an op: build_filename(op +0x170 NCI name, DIR, suffix) (mccore @1806f1da0;
   mcmill CBatch::PostSelectedOperations FUN_180023fa0 / FUN_180023500) - DIR "\" base name of the NCI name (no extension)
   "." suffix; SUFFIX = the control's NC suffix as CPostFileUtil::CheckNcSuffix (mcpost @180077610) leaves it (".NC").
   Here DIR = the directory given (ours: the part's folder). OUT gets the path with '/' as written by the caller. */
void nciw_nc_name(const uint8_t *oprec, uint32_t n, const char *dir, const char *suffix, char *out, size_t outn);
/* CheckNcSuffix: Trim('.') + Trim() whitespace, then ".%s" (empty stays empty) */
void nciw_check_suffix(const char *in, char *out, size_t outn);

/* the mp post (Q23, ours): a copy G of F in which every op made or edited here (CAMOPS with op_index) carries the OP_ID record
   the save would write (mcam_op_records) and the NCI section nciw_section writes from its moves; stored ops are F's. 1 ok, 0 refused
   (ERR names the op). nciw_view_free releases it (not F). */
typedef struct { McamFile G; void **bufs; int nbufs; long nstandin; char note[400]; } NciwView;
int nciw_view(McamFile *F, CamOp **camops, int ncamops, int metric, NciwView *v, char *err, size_t errn);   /* metric = IsMetric() */
void nciw_view_free(NciwView *v);

#endif
