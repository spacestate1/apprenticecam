/* part_tools - the part file's TlMgr archive (ASSOC_ID subtype 33): each tool's full definition, its holder and its
 * assembly. Spec: ../formats/part_tools.md (every layout decoded from mccore.dll / tlcore.dll, cited there). */
#ifndef PART_TOOLS_H
#define PART_TOOLS_H
#include "mcam_load.h"

/* REC / N = one ASSOC_ID record with subtype 33 (ent +0x2c) and ent +0x56 != 0 (spec 1). For each TOOLS[i] whose
   slot (TL_ID +0x2cc) names an assembly in the archive and whose TL_ID body TL_BODY[i] (ent +0x68, 0x288 bytes;
   NULL = record too short) equals that tool's legacy block (spec 1.1), fill the TlMgr fields (spec 4) and set
   has_extras. Returns the number of tools filled, or -1 when the archive does not parse (nothing is changed). */
int part_tools_apply(Tool *tools, int ntools, const uint8_t *const *tl_body, const uint8_t *rec, size_t n);

/* every slot-table entry of the archive as a Tool (num / dia / type / name from TlTool / TlToolMill / TlObject,
   the rest as part_tools_apply). *OUT is xmalloc'd (free each holder_rz, then *OUT). Returns the count, -1 on error. */
int part_tools_list(const uint8_t *rec, size_t n, Tool **out);

/* writing the archive back (spec 10). One PtEdit per part tool: NOW = the tool as edited here, BASE = the same tool as
   loaded from the part (mcam_load; BASE->slot names the assembly, spec 1.1); BLOCK_NOW / BLOCK_BASE = their op_tool_info
   (0x288 bytes, toollib.c tool_op_info, both from BASE's block): the TlTool level's block (TlTool::Serialize writes
   GetOpToolInfo) gets the bytes in which they differ. RESULT on return: PT_TOOL | PT_HOLDER bits for what changed, 0
   nothing, PT_NOSLOT (no assembly with that slot) or PT_CLASS (the type needs another tool class: not written). */
enum { PT_TOOL = 1, PT_HOLDER = 2, PT_NOSLOT = -1, PT_CLASS = -2 };
typedef struct { const Tool *now, *base; const uint8_t *block_now, *block_base; int result; } PtEdit;
/* REC / N as part_tools_apply. *OUT (xmalloc'd, *OUTN bytes) = the record body rebuilt: ent +0 up to the archive kept,
   then the archive as TlMgr::Serialize stores it, with the edits applied (no edits: the same bytes, spec 10.3).
   Returns the number of tools changed, -1 when the archive does not parse (*OUT NULL). */
int part_tools_write(const uint8_t *rec, size_t n, PtEdit *ed, int ned, uint8_t **out, size_t *outn);
#endif
