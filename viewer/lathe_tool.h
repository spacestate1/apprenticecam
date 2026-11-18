/* lathe_tool.h - Mastercam 2017 lathe tools: the part's LTL_ID records (ASSOC subtype 8, struct Cnc::Tool::LATHETOOL at ent +0x68,
 * 0xa48 bytes) and LATHETOOLCLASS::Make_op_tool 0x180152060 (mclathe.dll), the op_tool_info a new lathe op takes from the tool
 * picked on its tool page (CLtlPgCmn::GetToolProperty 0x1801ab7f0). Spec: ../formats/lathe.md 8 ("Lathe tool pick").
 * Pure: no McamFile, no globals. */
#ifndef LATHE_TOOL_H
#define LATHE_TOOL_H
#include <stddef.h>
#include <stdint.h>

#define LT_BODY_SIZE 0xa48      /* LATHETOOL (LATHETOOLCLASS::SetInfo 0x180158390 copies 0x14 * 0x80 + 0x48 bytes to this+8) */
#define LT_OPTOOL_SIZE 0x288    /* op_tool_info (op +0x2e0; Make_op_tool's memset) */

/* what Make_op_tool reads besides the tool (CLtlPgCmn::GetToolProperty: SetLToolAngles = LATHETOOLCLASS::SetOpAngles 0x180159950
   from the new op, before Make_op_tool) */
typedef struct {
    int metric;                 /* IsMetric(): the part's units (1 mm) */
    int32_t machine_group;      /* OMgetActiveMachineGroup()'s id (op_tool_info +0x274), 0 = none */
    int b_axis;                 /* TurningOpUsesBAxis 0x1801780f0 (the op's axis combination has a rotary component 3 / 9; the machine
                                   definition is NOT DECODED): 1 is refused; 0 = SetOpAngles' else branch */
    int op_flip;                /* op +0x2b3 -> LATHETOOLCLASS +0xbe0 (SetOpAngles, not B axis) */
    int32_t op_axis_combo;      /* op +0x11bc -> LATHETOOLCLASS +0xbdc (SetOpAngles) */
    uint32_t keep_268;          /* op_tool_info +0x268, kept by Make_op_tool (the op's own value) */
    double tol;                 /* tol_exref (the system tolerance) */
} LtCtx;

/* Make_op_tool: OUT = the op_tool_info (op +0x2e0, LT_OPTOOL_SIZE bytes). 1 = made; 0 = refused, ERR says what is NOT DECODED */
int lt_make_op_tool(const uint8_t *body, uint32_t n, const LtCtx *cx, uint8_t *out, char *err, size_t errn);

/* the tool's own fields (LATHETOOLCLASS getters; body = LATHETOOLCLASS this + 8) */
typedef struct {
    int32_t slot;               /* Slot 0x18015c9a0: body +0 (the tool system's id; the op's op_tool_info +0x264) */
    int32_t tool_num;           /* ToolNum 0x18015ccc0: body +0x76c */
    int32_t station;            /* ToolStation 0x18015cd10: body +0x9d8 */
    int32_t offset;             /* ToolOfs(0) 0x18015ccd0: body +0x770 */
    int type;                   /* ToolType() 0x18015cd60: 5 (custom) when body +0x758, else body +0x1f0: 0 general turning, 1 thread,
                                   2 groove / parting, 3 boring bar, 4 drill / tap / reamer */
    int turret, spindle;        /* ActiveTurret / ActiveSpindle 0x18015c2d0 / 0x18015c2c0: body +0x759 / +0x75a */
    int32_t group;              /* body +0x9dc: the machine group (CLtlPgCmn::FillListCtrl 0x1801ab1a0 lists 0 = any or the op's group) */
    double nose_r;              /* NoseRadius 0x153440: type 4 -> 0, else INSERTCLASS::CornerRad = body +0x128 (inches) */
    char name[0x79];            /* body +4 (Make_op_tool strcpy0 0x78; the list shows Name 0x153400 = leading / trailing spaces
                                   stripped) */
} LtInfo;
int lt_info(const uint8_t *body, uint32_t n, LtInfo *o);

/* FacePgPrm::UpdateCtrlData 0x1801b5d10 (on every tool pick of the Face page, when ToolPrmBase::OKToUpdate 0x1801b5140: op +0x531
   == 0): ToolOrientChanged -> SetDefaultCC + SetEEVectors. See lathe_tool.c for each rule. */
/* 1 changed, 0 not, -1 Make_op_tool refused (ERR). PREV = the op's op_tool_info before the pick (0x288 bytes) */
int lt_orient_changed(const uint8_t *prev, const uint8_t *body, uint32_t n, const LtCtx *cx, int first_call, char *err, size_t errn);
/* SetDefaultCC: AWAY = op +0x1210; *CUR_INDEX = the comp direction combo's index (op +0x56a); 1 = *CUR_INDEX / *OP56A written */
int lt_face_default_cc(const uint8_t *body, uint32_t n, const LtCtx *cx, int away_from_center, int *cur_index, int *op56a);
/* IntelliSetEEVector on V (WHICH 0: op +0x1218, 1: op +0x1288); 1 = done (V may be unchanged), 0 = refused (custom tool / groove ops) */
int lt_intelli_ee_vector(const uint8_t *body, uint32_t n, const LtCtx *cx, int opcode, int which, double v[2]);

#endif
