# Lined up with Mastercam (2026-10-26)

The user's request after the 2026-10-26 audits ("lets get these lined up with mastercam"). Specs: formats/align_ops.md (Toolpaths
manager), formats/align_geo.md (geometry delete, transforms, N1). Engrave: formats/engrave.md. Test scenes:
tools/tests/align_scenes.txt.

| item | now | source |
|---|---|---|
| Delete ops | every selected op, stored or made here; Yes / No question (mccoreres 7616 / 8979, Yes default) while Display Options 4061 "Prompt for toolpath deletion" is on (1 at start); soft delete into the delete buffer (out of the list and the screen); the previous delete purged first; the save drops the ops' OP / TP / NCI records; the project file keeps `OPGONE <id>` lines | align_ops 1.1-1.5; FUN_180343d50 (+0x75a4 = 1) |
| Undelete | all ops of the last delete back at their places; never greyed; Ctrl+Z with the Toolpaths manager focused | align_ops 1.7, 4 |
| Delete key | by focus: the Toolpaths manager -> its ops (consumed even with nothing selected); anywhere else -> delete_entities | align_ops 2 |
| Cut / Copy / Paste | Cut = soft delete into the cut buffer (Shift appends), pasted once with the same ids; Copy = references (Shift appends; purges the cut buffer), pasted any number of times; neither touches the delete buffer; Ctrl+X / C / V in the tree | align_ops 3 |
| Undo | operations are no longer on Ctrl+Z's stack (op add / delete / edit); geometry only | align_ops 4 |
| Engrave | greyed (gallery, right-click menu): Mastercam's Engrave is the engrave.dll module (opcode 416) whose engine is the external estamp7.exe - NOT DECODED | align_ops 5, engrave.md |
| Delete geometry used by ops | dialog 6561 "Deleting a referenced entity!" (Delete all / Delete only non-associative / Cancel) unless config 17014 suppresses it; Delete all -> the using ops dirty (keep their toolpath, never deleted); Undo restores the geometry, the ops stay dirty | align_geo 1 |
| Move (transforms) | the moved entity keeps the original's id (xform_swap_idn); the ops follow it and get dirty, no warning; the save writes it as a new record with the old id and re-points the stored ops' TP eptrs | align_geo 2 |
| N1 NC file | the control's post settings (2017 page 2 +0xdc9; X9 default: NC Ask): Ask -> Save As dialog with the platform's overwrite question, Cancel skips the program silently; Overwrite -> written directly | align_geo 4 |

Ours / NOT DECODED, kept visible:
- Paste goes after the selected op (the tree has no insert arrow). A copy of a stored op is a new op computed by this program's
  engines (list_op_copy's NCI handling and the copy's dirty state NOT DECODED).
- Regenerating an op whose chain uses deleted geometry is refused (Mastercam's behaviour NOT DECODED); the op stays dirty.
- Permanent delete: mark_op_dependents and the unused-tool removal NOT PORTED; Display Options 4061's persistence NOT DECODED.
- Scripts have no keyboard focus: the `delete` step deletes ops when ops and no geometry are selected (test glue).
- The chosen NC folder does not become the group's NC folder (SetNCPrefix; N2 keeps the part's folder); X9 post settings offset
  NOT DECODED.
