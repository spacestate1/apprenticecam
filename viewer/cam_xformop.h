/* cam_xformop - Mastercam 2017 "Transform toolpath" (Toolpaths > Transform, opcode 4, mccore BnciXform @18013f900 -> the
 * CBnciXfm engine: FUN_18013fb40 / FUN_1801422b0 setup / FUN_180141400 BuildRunList / FUN_180144480 CreateBnci /
 * FUN_18014d300 TransformBnciLineByToolplane / FUN_18014a300 TransformBnciLineByCoordinate): the transform operation's
 * NCI section made from its source operations' NCI sections, one transformed copy per instance.
 * Spec, decompile citations and what is refused: ../formats/transform_toolpath.md. Records are Mastercam 2017's layout
 * (nci_bin, 0x370 bytes; the part file stores the first bnci_bytes(gcode) - 2 of them after a u16 size). */
#ifndef CAM_XFORMOP_H
#define CAM_XFORMOP_H
#include <stddef.h>
#include <stdint.h>
#include "mcam_load.h"

#define XF_RS 0x370                      /* sizeof(nci_bin) */

/* Mastercam's view table (mcViews: number -> matrix rows), as find_view_m / AddMCView use it: 1..8 the reserved views,
   then the part's stored views; a matrix not found is added as view n_views + 1 (AddMCView). */
typedef struct { int n, cap; int *num; double (*m)[9]; int nmax; int added; } XfViews;
void xf_views_init(XfViews *V, const McamFile *f);        /* reserved 1..8 + F's stored views (F may be NULL) */
void xf_views_free(XfViews *V);

typedef struct {
    int id;                              /* op id */
    const uint8_t *op; uint32_t opn;     /* the operation struct (OP_ID record + 0x68), 2017 layout */
    uint8_t *recs; int nrec;             /* its NCI section without the -2 / -3 records, XF_RS bytes each (zero-filled) */
    uint16_t *rsz;                       /* each record's stored size (the u16 before it in the part file) */
} XfSrc;

typedef struct {
    int output_op_comment;               /* the control's CCNCMachine::OutputOpComment (where it is stored: NOT DECODED;
                                            the caller supplies it): writes the 1008 op comment before each copy's 1020 */
    long base;                           /* file offset of the section (the records' +0x10 positions) */
} XfOpts;

typedef struct {
    long records;                        /* records written */
    long runs;                           /* copies written (999 .. 1019 blocks) */
    long views_added;                    /* views find_view_m had to add (AddMCView) */
    long not_decoded;                    /* values written with a stand-in (counted; listed in note) */
    char note[400];
} XfStats;

/* The transform op's whole section (-2 .. -3, u16-size-prefixed records, 2017 layout) from its op struct XOP and its
   source ops SRC (in Operations Manager order). 1 = ok (*out malloc'd), 0 = refused (ERR says why; NOT DECODED / NOT
   PORTED cases are refused, never approximated). */
int xfop_section(const uint8_t *xop, uint32_t xopn, const XfSrc *src, int nsrc, XfViews *V, const XfOpts *o,
                 uint8_t **out, size_t *outn, XfStats *st, char *err, size_t errn);

/* F.ops[INDEX] (a stored Transform op of a 2017 / X9 part): its source ops (TP_ID kind 23 records), their stored NCI
   sections, the part's views -> xfop_section. */
int xfop_from_part(McamFile *f, int index, const XfOpts *o, uint8_t **out, size_t *outn, XfStats *st, char *err, size_t errn);

/* the parameters (prm_xform = op +0x11c0, 0x2b8 bytes; formats/transform_toolpath.md section 2), for the UI / tests */
typedef struct {
    int type;               /* +0x00: 0x10 Translate, 0xd Rotate, 8 Mirror (xform_type) */
    int method_sub;         /* +0x70: Rotate 0xe about the origin / 0xf about a point; Translate 0x11 rectangular, 0x12 polar
                               (delta), 0x13 between points, 0x14 between planes; Mirror 9 X, 10 Y, 0xb line, 0xc points, 0x15 angle */
    int plane_flag;         /* +0x72: use the type page's plane (Rotation plane / Mirror plane / ...) */
    int count, count_y;     /* +0x74 / +0x76: instances (Rotate #, Translate X / Y) */
    double pt[3];           /* +0x78: Rotate centre / Translate first point */
    double angle_step;      /* +0x90: Rotate angle between instances */
    double plane_m[9];      /* +0x98 (view +0xf8): the Rotate page's rotation plane */
    int total_sweep;        /* +0x101: angle is the total sweep (divided by #) */
    int rot_translate;      /* +0x102: Rotate method "Translate" (CalcRotateT) */
    double angle_start;     /* +0x120: the first rotated instance's angle */
    int custom;             /* +0x18: custom parameters (1031 / 1032 values +0x1c.. / +0x30..) */
    int remove_comments;    /* +0x50 */
    int keep_xform;         /* +0x52 */
    int wo_mode;            /* +0x54: work offset numbering 0 automatic, 1 maintain source's, 2 assign new (3 with +0x60) */
    int wo_start, wo_inc;   /* +0x58 / +0x5c */
    int match_wo;           /* +0x60 */
    int unique_subpgm;      /* +0x61 */
    int disable_src_post;   /* +0x62 */
    int copy_src;           /* +0x63 Copy source operations (the identity instance is output) */
    int group_by_op;        /* +0x64: Group NCI output by: 1 Operation type (op-major), 0 Operation order (instance-major) */
    int incremental;        /* +0x65 */
    int save_planes;        /* +0x66 */
    int new_ops;            /* +0x67 Create new operations and geometry */
    int coord_method;       /* +0x68 Method: 0 Tool plane, 1 Coordinate */
    int subprogram;         /* +0x69 */
    int include_origin;     /* +0x6a */
    int include_wcs;        /* +0x2b0 */
    int source_nci;         /* +0x51 0 = Source NCI, 1 = Geometry */
    int by_toolplane;       /* !+0x17: work offset numbering By instance (+0x17 = 1) / By toolplane */
} XfPrm;
void xfop_prm(const uint8_t *op, uint32_t opn, XfPrm *p);

#endif
