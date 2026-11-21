/* mcam_saveops - write Mastercam parts: the open part (or a new one) plus this program's geometry and operations */
#ifndef MCAM_SAVEOPS_H
#define MCAM_SAVEOPS_H
#include "cam2d.h"
#include "mcam_write.h"
/* src = the part to add to (never modified), or NULL / "" for a new part; dst must differ. Entities of F without a file
   id (idn 0) are written as new geometry and get ids. Returns 1 on success; msg says what was done or why not. */
/* ours: an aPoint eptr standing for the session entity F.ents[i] that has no eptr yet (drawn here): the save writes that entity's
   eptr in its place (drafting records made by core_ui cui_draft_*.c) */
#define MCAM_EPTR_TOKEN(i) ((1ull << 62) | (uint64_t)(i))
int mcam_save_part(const char *src, const char *dst, McamFile *F, CamOp **ops, int nops, char *msg, size_t msgn);
/* a 2017 part's content (major >= 0x13) under a .mcx-9 name (user decision 2026-11-19, formats/user_decisions.md; ours, no Mastercam
   source): mcam_save_part appends mcam_ext_warning() to its message and still saves */
int mcam_ext_mismatch(const char *dst, int major);
const char *mcam_ext_warning(void);
/* nci_write.c (Q23): the OP_ID record (rec[k], without the u32 length) each of OPS would be saved with (NULL: the save leaves the op
   out, msg says why), and the TL_ID record of a tool the save would add (tl[k], else NULL: the part's record of its slot). Nothing is
   written; F is left as it was. malloc'd buffers, the caller frees them. */
int mcam_op_records(const char *src, McamFile *F, CamOp **ops, int nops, uint8_t **rec, uint32_t *recn, uint8_t **tl, uint32_t *tln, char *msg, size_t msgn);
/* the same, plus the tp entity records (drill points, chains; u32 length + record each, in the save's order) of each op (tp[k] /
   tpn[k], NULL / 0 when none) */
int mcam_op_records_tp(const char *src, McamFile *F, CamOp **ops, int nops, uint8_t **rec, uint32_t *recn, uint8_t **tl, uint32_t *tln,
                       uint8_t **tp, uint32_t *tpn, char *msg, size_t msgn);
int mcam_save_with_ops(const char *src, const char *dst, const McamFile *F, CamOp **ops, int nops, char *msg, size_t msgn);
const McwFile *mcam_defaults_library(int metric);   /* config 2303 (its saved copy, paths.h defaults_read) */
void mcam_defaults_library_reset(void);          /* Q19 (ours) */
int mcam_template_parts(McwFile *out, int max, const char *src);   /* parts to copy operation settings from */
const char *mcam_part_ext(int major);
const char *mcam_config_value(int metric, int key, char *out, size_t n);   /* the user's mcamxm / mcamx.config entry KEY ("" when absent) */
int mcam_save_opcode_for(const CamParams *p, int *style);            /* the opcode (and Surface / 2D High Speed style) the save writes, 3D types too */
int mcam_opcode_for(const CamParams *p, int *style);                 /* Mastercam opcode of one of our op types (0 = none) */                               /* ".mcam" (2017) or ".mcx-9" (X9) */
void mcam_op_record_heights(const McamFile *F, const CamOp *op, const uint8_t *base, uint32_t n, uint8_t *out);   /* the height bytes the save writes for OP */
/* Q19: the op as Mastercam's dialog op would be written to a defaults file (ent bytes, 0x2938) from TMPL (its own stored record when
   OWN, else the defaults file's op of its type); 0 = a type not written here (MSG) */
int mcam_op_dialog_record(const McamFile *F, const CamOp *op, const uint8_t *tmpl, int own, uint8_t *e, char *msg, size_t msgn);
void mcam_fresh_views(McamFile *f);   /* File > New: the library's stored views */
#endif
