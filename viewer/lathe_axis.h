/* lathe_axis.h - Mastercam 2017 lathe machine axis combinations (mcmachinedef.dll CCNCMachine / CAxisCombination /
 * CComponentGroup) and the default lathe Tplane of a new lathe op (mclathe.dll LatheACTplanes, MTPg3Params), as
 * CLtlPgCmn::GetToolProperty 0x1801ab7f0 runs them after Make_op_tool when the picked tool's slot differs from the op's
 * previous tool. Spec: ../formats/lathe.md section 8 "Axis combination and default Tplane" (every rule cites its function).
 * Reads the part's machine-definition records (McamFile.mdrec), its lathe tools (McamFile.ltl) and planes (planes_list). */
#ifndef LATHE_AXIS_H
#define LATHE_AXIS_H
#include <stddef.h>
#include <stdint.h>
#include "mcam_load.h"

typedef struct {
    int32_t id;        /* CAxisCombination::Id 0x180014810 (this +0x1e9c = the record's ent id): the op's +0x11bc */
    int index;         /* its place in the machine's list (AxisCombinationFromIndex 0x180061310) */
    char name[0x40];   /* CAxisCombination::Name: the record body +0 (shown on the tool page, SetAxisComboInfo 0x1801af790) */
    int spindle;       /* CAxisCombination::ActiveSpindle 0x180011a90: 0 left, 1 right, -1 none -> op +0x541 (SetOpActiveSpindleFromAxisCombo) */
    int turret;        /* CAxisCombination::ActiveTurret 0x180011b90: 1 upper, 0 lower, -1 none -> op +0x540 (SetOpActiveTurretFromAxisCombo) */
    int stream;        /* StreamIndexFromOp(op, 1) 0x180064ce0 = StreamIdx 0x180018950 (record body +0x1e74, clamped 0..7) -> op +0x11b8 */
    int rot39;         /* RotAxComp(3) or RotAxComp(9) found (0x180017840): 1 = the tool page keeps op +0x520 / +0x528 / +0x530 as
                          Make_op_tool's caller set them; 0 = GetToolProperty zeroes them */
} LaCombo;

/* the machine group's machine (OMgetOpMachine: the record whose ent id is the group's +0x1170) -> its axis combination for a lathe
   tool: CCNCMachine::AxisComboFromLatheToolInfo 0x180066d40 / 0x180066d60 (LATHETOOL +0x75a spindle, +0x759 turret, fallback index
   0). LTL = the tool's LATHETOOL body (McLtl.body). 1 = *out filled; 0 = refused (err: NOT DECODED / no machine) */
int la_axis_combo_for_tool(const McamFile *f, int32_t machine_group_id, const uint8_t *ltl, uint32_t ltl_n, LaCombo *out, char *err, size_t errn);
/* the same lookup by turret / spindle bytes (InitAll 0x1802b41d0 calls it with (0,0) (0,1) (1,0) (1,1)) */
int la_axis_combo_tp(const McamFile *f, int32_t machine_group_id, int spindle, int turret, LaCombo *out, char *err, size_t errn);

/* CLtlPgCmn::SetOpDefaultTplane 0x1801af890 (run only when the combination's id differs from the op's +0x11bc):
   LatheACTplanes::GetDefaultACTplaneId 0x1802b3ee0 (MTPg3Params::PrevTurningOpSpindleReference 0x1802509c0 over the part's ops
   before INSERT_INDEX (the new op's place in the operation list; F->ops order), else the first plane of the combination's ACT list)
   then GetACTplanePtr 0x1802b3e50 (that id in the combination's ACT list, InitAll / InitACTplaneList / ACSupportsToolplane).
   CPLANE_M / CPLANE_ORG = the op's Cplane (op +0x6d8 / +0x720) at that moment.
   Returns 1: the op's Tplane = part plane *plane_id (m / org: its matrix rows / world origin; LatheTplane::GetOpTplane = Plane::Convert);
   2: GetACTplanePtr found nothing - the op's Tplane is not changed; 0: refused (err) */
int la_default_tplane(const McamFile *f, int32_t machine_group_id, int insert_index, const LaCombo *c, const double cplane_m[9],
                      const double cplane_org[3], int *plane_id, double m[9], double org[3], char *err, size_t errn);

/* the machine group (root of the +0x10c parent chain) of the group with id GID; 0 = none */
int32_t la_machine_group_of(const McamFile *f, int32_t gid);

/* L-12 (formats/lathe.md 9): the spindle speed limits of an op whose op +0x11bc is COMBO_ID, on the active lathe machine:
   CCNCMachine::MaxLatheSpindleRPM / MinLatheSpindleRPM(operation &, NULL) 0x18006b6c0 / 0x18006bb80 (the chucks of the
   combination's part list). 1 = *mn / *mx set; 0 = refused (err: NOT DECODED) */
int la_lathe_rpm_limits(const McamFile *f, int32_t machine_group_id, int32_t combo_id, long *mn, long *mx, char *err, size_t errn);
/* CAxisCombination::ActiveSpindle of AxisCombinationFromID(COMBO_ID, 0) (an id not in the machine: its first combination) */
int la_combo_spindle(const McamFile *f, int32_t machine_group_id, int32_t combo_id, int *spindle, char *err, size_t errn);
/* the machine's feed limits lim[mode] = {min, max} (mode 0 per rev, 1 per minute, 2 none): ToolPrmBase::GetFeedMin / GetFeedMax */
int la_machine_feed_limits(const McamFile *f, int32_t machine_group_id, int metric, double lim[3][2], char *err, size_t errn);
#endif
