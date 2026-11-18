/* cam3d_pencil.h - HST Pencil (op 132, UI style 6, kernel style 5): the FullPencilNet engine (formats/pencil.md).
 *
 * cam3d_pencil.c is a part of the cam2d.c translation unit, like cam3d_finish.c: cam2d.c includes it after cam3d_finish.c
 * (it uses MB / mv, the drive mesh C3Mesh / c3_mesh_op of cam3d_rough.c and hst_grid_open of cam3d_finish.c). The dispatcher
 * entry is `static int gen_pencil(const McamFile *f, CamOp *op, MB *b)`. Wiring it in (a CAM_PENCIL type, cam_hst_style 6, the
 * dispatcher case, cam_cli's name) is outside this file; until then a stored op regenerates through cam3d_pencil() below.
 *
 * Inputs: the op 132 record (a stored op's, p.file_op_id), the drive surfaces / solids of the op, the tool (record 0x350 /
 * 0x358 or CamParams.tool_dia / corner_r), the containment chains. Output: Mastercam-style moves (tool tip, part units).
 * What is NOT DECODED is refused with a message (op->error), never guessed: see formats/pencil.md 8. */
#ifndef CAM3D_PENCIL_H
#define CAM3D_PENCIL_H

/* The net's parameters (formats/pencil.md 1, 1.1, 6.2): McHmm values of plan 0x140f. */
typedef struct {
    double R;            /* ball radius of the tool the net drops (tool radius + stock; ball tools only) */
    double hmin;         /* PencilBiTangentMinHorizontalChange = StepMinHorizontalChange (step [0]) */
    double cell;         /* PencilCellResolution (step [3]) */
    double big;          /* PencilBigCellResolution (the weave fibre spacing) */
    double chord;        /* PencilBiTangentMaxContactPointChordChange (step [4]) */
    double chord_thr;    /* PencilBiTangentMaxContactPointChordChangeThreshold (the filter) */
    double vstep;        /* PencilBiTangentMaxVerticalStepforward (step [5]) */
    double minfrag;      /* MinFragLength */
    double max_normal_deg; /* PencilBiTangentMaxNormalChange (degrees) */
} PcParams;

/* counters of one run (testing: tools/tests/pencil_test.c prints them) */
typedef struct {
    int nodes, drops, leaves, marks, chains, kept, conv_dropped, short_dropped, few_dropped, points, inserted, climb_pieces;
    double gxy[3];       /* where (ours, testing) */
    double gouge;        /* the deepest point of the straight moves under the drive (ours, testing) */
} PcStats;
extern PcStats pc_stats;
extern const char *pc_dump_path;   /* ours (testing): when set, the chains / pieces are written there (x y z per line) */

/* regenerate the Pencil op `op` (its record = op->p.file_op_id's) into op->m / op->nm; 0 + op->error on a refusal */
int cam3d_pencil(const McamFile *f, CamOp *op);

#endif
