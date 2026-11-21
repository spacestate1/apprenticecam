/* cam_fileop - operations stored in a Mastercam part as editable CamOps (parameters + chains / points / tabs) */
#ifndef CAM_FILEOP_H
#define CAM_FILEOP_H
#include "cam2d.h"
#include "xt_mesh.h"
int cam_fileop_type(const Op *o);                     /* CAM_* type this program can regenerate, 0 if not */
/* F.ops[op_index] as a CamOp ready for cam_generate (p.file_op_id = its op id); 0 + err if not possible.
   F is not const: chains of solid edges append their edges to F.ents. */
int cam_op_tool_index(const McamFile *f, int op_index);   /* the stored op's tool in f->tools (op_tool: slot, then number), -1 none */
int cam_op_from_file(McamFile *f, int op_index, CamOp *op, char *err, size_t errn);
/* a chain from file entity ids (ent_idn) in order; rev[k] 0/1 from the chain records, -1 unknown */
int cam_chain_from_idns(McamFile *f, const int32_t *idn, const int *rev, int n, Chain *out);
int cam_solid_chain_ents(McamFile *f, int idn, int *count);
int cam_solid_face_loops(const McamFile *f, int idn, XtEdge **out, int *loop_n, int max_loops, int *nloops, int *outer);   /* all top-face loops */   /* top-face loop of a solid as entities; first index or -1 */
/* parameters of Mastercam's default operation of this type (shared Mill_MM / Mill_Inch.mcam-defaults); 1 if found */
int cam_defaults_from_file(CamParams *p, int type, int metric, const Tool *tool);
/* the raw OP_ID record of that default operation (NULL if the defaults file is not readable); *n = its length */
const uint8_t *cam_default_op_record(int type, int metric, uint32_t *n);
int cam_fileop_type3d(const Op *o);   /* a stored op of a 3D port's code -> its port type (op 132 by style 0x16bc), 0 = none */
/* a new lathe op (formats/lathe.md 7.1): InitOperation's default_ops_get - the first op of OPCODE in the shared LATHE_INCH / LATHE_MM
   .mcam-defaults (lathe/Ops; the lathe group's control names it, OpDfltsFile) or in the file cam_lathe_defaults_set gave (cam_cli
   --lathe-defaults); NULL when none. *n = its length (the 2017 OP_ID record, 0x2938) */
const uint8_t *cam_lathe_default_record(int opcode, int metric, uint32_t *n);
void cam_lathe_defaults_set(const char *path);
/* Edit Common Parameters (opmanres dialog 243, CEditCommonDlg): the enabled settings, as the dialog holds them.
   Combos: index as listed (opmanres strings): use 0 No change / 1 Enable / 2 Disable; when 0 No change / 1 Always use
   Clearance / 2 Clearance at Start and End; abs/inc 0 No change / 1 Absolute / 2 Incremental; rapid 0 Rapid retract /
   1 Retract rate; ftc 0 Force tool change = On / 1 Off; batch 0 To batch / 1 No batch; dir 0 CW / 1 CCW. */
typedef struct {
    int on_tool, tool, on_head, head, on_lofs, lofs, on_dofs, dofs, on_rpm, rpm, on_dir, dir;
    int on_feed, on_plunge, on_rrate; double feed, plunge, rrate;
    int on_clr, clr_use, clr_when, clr_inc; double clr;
    int on_ret, ret_use, ret_inc; double ret;
    int on_fp, fp_inc; double fp;
    int on_top, top_inc; double top;
    int on_dep, dep_inc; double dep;
    int on_rapid, rapid, on_ftc, ftc, on_batch, batch;
    const Tool *toolsel;   /* Tool Selection: the tool picked in the list (NULL = off) */
} CamCommonEdit;
/* apply them to one operation as FUN_180145b50 does (parameters + record bytes); orig = the stored op's record when
   the op is being taken over from the part now (its abs / inc / on flags are the "No change" ones), else NULL */
void cam_common_apply(McamFile *f, CamOp *op, const CamCommonEdit *ce, const uint8_t *orig, uint32_t orign);
void cam_common_drop_heights(CamParams *p);
int cam_bt_offset(int opcode);                                  /* Break Through flag's record offset (amount +8), 0 none */
double cam_bt_eff(int opcode, int on, double amount, double fstock);   /* the depth set_depths takes off */
void cam_params_from_patches(CamParams *p);   /* the op dialog's record patches -> the engines' record-derived fields */   /* the parameters dialog regenerated the op: its heights are ours again */
void cam_face_mirror(CamParams *p, int off, int n, const uint8_t *v);   /* batch 7: Facing CamParams fields from a record edit */
void cam_area_macro_params(CamParams *p, const uint8_t *r, uint32_t rn, int metric);   /* batch 9: McHmm macro values (am_*) of an Area Mill record */
void cam_lead_sidecar_compat(CamParams *p);   /* sidecars with one lead for both sides: exit = entry unless the patches hold the exit */
void cam_defaults_reset(void);   /* Q19 (ours): read the defaults file again after a save / reset */
void cam_params_from_raw(McamFile *f, const uint8_t *r, uint32_t n, int type, CamOp *op);   /* srough: a record not in the file */
#endif
