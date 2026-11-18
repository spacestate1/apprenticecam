/* lathe_axis.c - Mastercam 2017 lathe axis combinations and the default lathe Tplane of a new lathe op.
 * Spec: ../formats/lathe.md section 8 "Axis combination and default Tplane". Sources: mcmachinedef.dll
 * (~/mastercam-re/work/lathe/mdef/mdef_all.c), mclathe.dll (~/mastercam-re/work/lathe/dump/mclathe_all.c; PrevTurningOpSpindleReference
 * also from the disassembly). Record body = ent +0x68; a class object's "this + 8" is the body (CAxisCombination::GetFromDb
 * 0x180013ab0 copies the 0x1ea0-byte body to this+8; CComponentGroup / CComponentGroupParams: this+0x84 / +0x8 / +0xc read as body
 * +0x7c / +0 / +4 - matched to every record of lathe.mcam_8 / millturn.mcam_8), a component's Params (CMachineComponent::Params)
 * is its body (Type = Params +0x7c, as nci_write.c reads it). */
#include "lathe_axis.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static int16_t rd16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }

static const struct McMdRec *md(const McamFile *f, int sub, int32_t idn) {
    if (!idn) return NULL;
    for (int i = 0; i < f->nmdrec; i++) if (f->mdrec[i].sub == sub && f->mdrec[i].idn == idn) return &f->mdrec[i];
    return NULL;
}
int32_t la_machine_group_of(const McamFile *f, int32_t gid) {   /* OMgetGrpMachineGroup: the root of the +0x10c parent chain */
    for (int hop = 0; hop < 32; hop++) {
        int k = -1; for (int i = 0; i < f->nlgrp; i++) if (f->lgrp[i].id == gid) k = i;
        if (k < 0) return 0;
        if (f->lgrp[k].parent == 0) return gid;
        gid = f->lgrp[k].parent;
    }
    return 0;
}
/* OMgetOpMachine: the machine record (ASSOC 22, CCNCMachine::InitFromDb 0x180057e30 get_assoc_ent_ent_idn(0x16): body = CCNCMachine
   this+0xd8) whose ent id is the machine group's +0x1170 */
static const uint8_t *machine_of(const McamFile *f, int32_t grp, char *err, size_t errn) {
    int k = -1; for (int i = 0; i < f->nlgrp; i++) if (f->lgrp[i].id == grp) k = i;
    if (k < 0 || !f->lgrp_mach) { snprintf(err, errn, "machine group %d is not in the part", grp); return NULL; }
    const struct McMdRec *m = md(f, 22, f->lgrp_mach[k]);
    if (!m || m->n < 0xe40) { snprintf(err, errn, "machine group %d has no machine definition record (ent %d)", grp, f->lgrp_mach[k]); return NULL; }
    return m->body;
}

/* ---- the machine's axis combinations (CCNCMachine::InitAxisCombinationsFromDb 0x1800628d0: the id list (ASSOC 15: +0 count, +8 ids)
   named by AxisComboList_ent_idn 0x180061a20 = machine this+0xf14 = body +0xe3c, in list order; each a CAxisCombination built from
   its ASSOC 26 record (GetFromDb 0x180013ab0); InitSuccf (this+0x3dbc) taken as set for a record that exists) */
typedef struct { const uint8_t *b; int32_t id; const uint8_t *grp[2]; const uint8_t *comp[2][32]; int ncomp[2]; } Ac;
#define AC_MAX 32
static int combos(const McamFile *f, const uint8_t *mach, Ac *out, int max) {
    const struct McMdRec *l = md(f, 15, rd32(mach + 0xe3c));
    if (!l || l->n < 8) return 0;
    int cnt = rd32(l->body), n = 0;
    for (int i = 0; i < cnt && n < max && 8 + 4 * (uint32_t)i + 4 <= l->n; i++) {
        const struct McMdRec *r = md(f, 26, rd32(l->body + 8 + 4 * i));
        if (!r || r->n < 0x1e78) continue;
        Ac *a = &out[n++]; memset(a, 0, sizeof *a); a->b = r->body; a->id = r->idn;
        /* GetFromDb: groups k = 0, 1 (k <= (Type == 5) + 1 - 1; Type 2 here): Group_ent_idn 0x180014510 = this+0x1de0 + 4k = body
           +0x1dd8; FindGroupFromByEntIdn -> the ASSOC 13 record; its component list CompList_ent_idn 0x180012960 = body +0x1de0 + 4k
           (ASSOC 15) -> FindComponentByEntIdn of each id (ASSOC 12; one not found is left out); no group -> an empty list */
        for (int k = 0; k < 2; k++) {
            const struct McMdRec *g = md(f, 13, rd32(r->body + 0x1dd8 + 4 * k));
            a->grp[k] = g && g->n >= 0x80 ? g->body : NULL;
            if (!a->grp[k]) continue;
            const struct McMdRec *cl = md(f, 15, rd32(r->body + 0x1de0 + 4 * k));
            if (!cl || cl->n < 8) continue;
            int cc = rd32(cl->body);
            for (int j = 0; j < cc && a->ncomp[k] < 32 && 8 + 4 * (uint32_t)j + 4 <= cl->n; j++) {
                const struct McMdRec *c = md(f, 12, rd32(cl->body + 8 + 4 * j));
                if (c && c->n >= 0x520) a->comp[k][a->ncomp[k]++] = c->body;
            }
        }
    }
    return n;
}
static int ctype(const uint8_t *c) { return rd32(c + 0x7c); }   /* CMachineComponent::Type 0x18008fec0 = Params +0x7c */
/* CompListIsPart 0x1800126e0: the list's last component's type in [100, 108]; CompListIsTool 0x1800127a0: [200, 207] or [300, 307] */
static int list_is_part(const Ac *a, int k) { if (!a->ncomp[k]) return 0; int t = ctype(a->comp[k][a->ncomp[k] - 1]); return t > 99 && t < 0x6d; }
static int list_is_tool(const Ac *a, int k) { if (!a->ncomp[k]) return 0; int t = ctype(a->comp[k][a->ncomp[k] - 1]); return (t >= 200 && t <= 0xcf) || (t >= 300 && t <= 0x133); }
/* GroupByClass 0x180014480: class 1 -> the group of the part list, 2 -> of the tool list (list 0 first); -1 none */
static int group_by_class(const Ac *a, int cls) {
    if (cls == 1) return list_is_part(a, 0) ? 0 : list_is_part(a, 1) ? 1 : -1;
    if (cls == 2) return list_is_tool(a, 0) ? 0 : list_is_tool(a, 1) ? 1 : -1;
    return -1;   /* class 3 (CompListIsTooling): not needed here */
}
/* CComponentGroup::MatchParams 0x1800a11e0 / IsClassAndType 0x1800a0940: the group's params chain (InitParamListFromDb 0x1800a0390:
   head = group this+0x84 = body +0x7c, each ASSOC 21 record: +0 class (CComponentGroupParams::Class = this+8), +4 type (+0xc), +0xc the
   next record's id) has class CLS and type TYPE (TYPE -1: class only, CComponentGroup::IsClass 0x1800a08b0 with param_2 0) */
static int grp_has(const McamFile *f, const uint8_t *g, int cls, int type) {
    if (!g) return 0;
    int32_t id = rd32(g + 0x7c);
    for (int hop = 0; id && hop < 64; hop++) {
        const struct McMdRec *p = md(f, 21, id);
        if (!p || p->n < 0x10) return 0;
        if (rd32(p->body) == cls && (type < 0 || rd32(p->body + 4) == type)) return 1;
        id = rd32(p->body + 0xc);
    }
    return 0;
}
/* CAxisCombination::IsMapped 0x180014940: a group of class 0x65 or 100 (CComponentGroup::IsMapped 0x1800a0d30) */
static int is_mapped(const McamFile *f, const Ac *a) {
    for (int k = 0; k < 2; k++) if (grp_has(f, a->grp[k], 0x65, -1) || grp_has(f, a->grp[k], 100, -1)) return 1;
    return 0;
}
/* GroupsMatchByClassAndType(int, int) 0x1800146b0 */
static int groups_match(const McamFile *f, const Ac *a, int cls, int type) {
    int k = group_by_class(a, cls);
    return k >= 0 && grp_has(f, a->grp[k], cls, type);
}
/* ActiveSpindle 0x180011a90 (PartGroup 0x1800171b0 = GroupByClass(1) for an unmapped combination) / ActiveTurret 0x180011b90
   (ToolGroup 0x180018a90 = GroupByClass(2)); machine Type 2 checked by the caller */
static int ac_spindle(const McamFile *f, const Ac *a) {
    int k = group_by_class(a, 1); if (k < 0) return -1;
    const uint8_t *g = a->grp[k];
    if (grp_has(f, g, 1, 1000) || grp_has(f, g, 2, 0x3ea) || grp_has(f, g, 2, 0x3eb)) return 0;
    if (grp_has(f, g, 1, 0x3e9) || grp_has(f, g, 2, 1000) || grp_has(f, g, 2, 0x3e9)) return 1;
    return -1;
}
static int ac_turret(const McamFile *f, const Ac *a) {
    int k = group_by_class(a, 2); if (k < 0) return -1;
    return grp_has(f, a->grp[k], 2, 0x3e9) || grp_has(f, a->grp[k], 2, 0x3eb) ? 0 : 1;
}
static void fill(const McamFile *f, const Ac *a, int index, LaCombo *o) {
    memset(o, 0, sizeof *o);
    o->id = a->id; o->index = index; snprintf(o->name, sizeof o->name, "%.63s", (const char *)a->b);
    o->spindle = ac_spindle(f, a); o->turret = ac_turret(f, a);
    int s = rd32(a->b + 0x1e74); o->stream = s < 0 ? 0 : s > 7 ? 7 : s;   /* StreamIdx 0x180018950: this+0x1e7c */
    /* RotAxComp(3 / 9, NULL, NULL, 0, 0) 0x180017840: the rotary axis components (type 5, FindListComponentsByType(k, .., 5, -1)) of
       lists 0, 1 whose WCSAxis (CRotaryAxisComponent::WCSAxis 0x1800afcd0 = UParams +0x7c, UParams = Params +0x4a0) is the axis */
    for (int k = 0; k < 2; k++) for (int j = 0; j < a->ncomp[k]; j++)
        if (ctype(a->comp[k][j]) == 5) { int w = rd32(a->comp[k][j] + 0x4a0 + 0x7c); if (w == 3 || w == 9) o->rot39 = 1; }
}
static int combo_lookup(const McamFile *f, int32_t grp, int p1, int p2, LaCombo *out, Ac *acs, int *nac, char *err, size_t errn) {
    const uint8_t *mach = machine_of(f, grp, err, errn);
    if (!mach) return 0;
    if (rd32(mach) != 2) { snprintf(err, errn, "the machine is not a lathe (CCNCMachine::Type %d)", rd32(mach)); return 0; }
    if (mach[0xf0]) { snprintf(err, errn, "a machine from MC extensions (CCNCMachine::FromMCExtensions, this+0x1c8): its axis-combination / Tplane branches are NOT DECODED"); return 0; }
    int n = combos(f, mach, acs, AC_MAX); *nac = n;
    for (int i = 0; i < n; i++) if (is_mapped(f, &acs[i])) { snprintf(err, errn, "axis combination %d is mapped (CAxisCombination::IsMapped): NOT DECODED", acs[i].id); return 0; }
    /* AxisComboFromLatheToolInfo 0x180066d60 (p1 = spindle, p2 = turret, fallback 1) */
    int part = p1 == 0 ? 1000 : 0x3e9, tt[3];
    int top = ((unsigned)(p2 - 1) & 0xfd) == 0;
    if (p1 == 0) { tt[0] = top ? 1000 : 0x3e9; tt[1] = top ? 0x3ea : 0x3eb; }
    else { tt[0] = top ? 0x3ea : 0x3eb; tt[1] = top ? 1000 : 0x3e9; }
    tt[2] = 0x3ec;
    for (int s = 0; s < 3; s++)
        for (int i = 0; i < n; i++)
            if (groups_match(f, &acs[i], 1, part) && groups_match(f, &acs[i], 2, tt[s])) { fill(f, &acs[i], i, out); return 1; }
    if (n < 1) { snprintf(err, errn, "the machine has no axis combination"); return 0; }
    fill(f, &acs[0], 0, out);                                          /* NumAxisCombinations >= 1 and param_3: index 0 */
    return 1;
}
int la_axis_combo_tp(const McamFile *f, int32_t grp, int spindle, int turret, LaCombo *out, char *err, size_t errn) {
    static Ac acs[AC_MAX]; int n;
    return combo_lookup(f, grp, spindle, turret, out, acs, &n, err, errn);
}
int la_axis_combo_for_tool(const McamFile *f, int32_t grp, const uint8_t *ltl, uint32_t n, LaCombo *out, char *err, size_t errn) {
    if (!ltl || n < 0x75b) { snprintf(err, errn, "the lathe tool record is short"); return 0; }
    /* CCNCMachine::AxisComboFromLatheToolInfo(LATHETOOL &) 0x180066d40: (+0x75a, +0x759) = LATHETOOLCLASS::ActiveSpindle 0x18014c2c0
       (this+0x762) / ActiveTurret 0x18014c2d0 (this+0x761), the LATHETOOL body = this+8 (GetInfo 0x18014e990) */
    return la_axis_combo_tp(f, grp, ltl[0x75a], ltl[0x759], out, err, errn);
}

/* ---- default Tplane */
static int op_is_lathe(int c) { return (unsigned)(c - 0x3c) <= 0xc || (unsigned)(c - 0xc9) <= 0x23; }   /* mccore op_is_lathe 0x1800c6060 */
static int op_is_notool(int c) {   /* mccore op_is_notool: feedspeed.c's port of op_is_notool(c, 1); here called with 0 - the
                                      difference of the second argument NOT DECODED (no lathe op of these parts is affected) */
    switch (c) { case 0: case 4: case 0x11: case 0x1e: case 0x1f: case 0x20: case 0x21: case 0x22: case 0x23: case 0x2f: case 0x4a:
    case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f: case 0x50: case 0x51: case 0x52: case 0x53: case 0x67: case 0x6c:
    case 0x8b: case 0x9a: case 0xd6: case 0xd7: case 0xd9: case 0xda: case 0xdb: case 0xe6: case 0xe8: case 0xe9: case 0xea:
    case 0xeb: case 0xec: return 1; }
    return 0;
}
/* MTPg3Params::PrevTurningOpSpindleReference 0x1802509c0 (disassembly 0x180250a50..0x180250b25): from the new op's index in the main op
   list downwards; an op that is not of this machine group / not lathe / has no tool / another axis combination steps the index down
   once more (dec rbx at 0x180250b18 + the loop's dec at 0x180250a59: the op before it is not looked at - as the code does) */
static int prev_turning_ref(const McamFile *f, int32_t grp, int idx, const LaCombo *c) {
    int si = -1; long rbx = idx;
    for (;;) {
        if (rbx < 0) break;
        rbx--; if (rbx < 0) break;
        const Op *o = rbx < f->nops ? &f->ops[rbx] : NULL;
        if (!o || !o->raw || o->raw_n < 0x2938) { rbx--; goto next; }
        const uint8_t *op = o->raw + 0x68;
        int oc = rd16(op + 4);
        if (la_machine_group_of(f, rd32(op + 0x28c)) != grp || !op_is_lathe(oc)) { rbx--; goto next; }   /* OMgetOpMachineGroup(op, 0) */
        if (oc == 0xe9 && rd16(op + 0x11c2) == 0 && (rd16(op + 0x11fa) == 4 || rd16(op + 0x11fa) == 9)) {
            if (rd16(op + 0x11c0) != c->spindle) si = (((unsigned)(c->turret - 1)) & 0xfffd) == 0 ? (uint16_t)rd16(op + 0x11fe) : (uint16_t)rd16(op + 0x11fc);
        } else {
            if (op_is_notool(oc) || rd32(op + 0x11bc) != c->id) { rbx--; goto next; }
            si = (uint16_t)rd16(op + 0x5d8);
        }
    next:
        if ((int16_t)si != -1) return (int16_t)si;
        si = -1;
    }
    return -1;   /* then (machine FromMCExtensions) AxisComboTplaneVId: not reached (refused above) */
}
/* LatheACTplanes::GetACMatrix 0x1802b3c00: Z row = the axis vector of LinAxComp(1, .., 1, 0) (0x180015070: the first linear axis
   component (type 4) of lists 0, 1 whose WCSAxis (UParams +0) is 1 or ReversedAxis(1) = 7), X row = that of LinAxComp(2) (2 / 8),
   Y = Z x X (the v_cross_3D operand order is read from the result: it is the order that gives right-handed rows, and part plane 9 of
   lathe.mcam_8); GetAxisVec(.., 0, 0) 0x1800a7fa0: AxisDir(0) = UParams +0: 1 / 2 / 3 = +X / +Y / +Z, 7 / 8 / 9 = -X / -Y / -Z, other 0
   (no tilt: param_2 0). No component: the zero matrix (MCAssert) */
static int lin_vec(const Ac *a, int axis, double v[3]) {
    int rev = axis == 1 ? 7 : axis == 2 ? 8 : axis == 3 ? 9 : 1;
    for (int k = 0; k < 2; k++) for (int j = 0; j < a->ncomp[k]; j++) {
        const uint8_t *c = a->comp[k][j]; if (ctype(c) != 4) continue;
        int w = rd32(c + 0x4a0);
        if (w != axis && w != rev) continue;
        v[0] = v[1] = v[2] = 0;
        switch (w) { case 1: v[0] = 1; break; case 2: v[1] = 1; break; case 3: v[2] = 1; break; case 7: v[0] = -1; break; case 8: v[1] = -1; break; case 9: v[2] = -1; break; }
        return 1;
    }
    return 0;
}
static void ac_matrix(const Ac *a, double m[9]) {
    double z[3], x[3]; memset(m, 0, 9 * sizeof *m);
    if (!a || !lin_vec(a, 1, z) || !lin_vec(a, 2, x)) return;
    m[0] = x[0]; m[1] = x[1]; m[2] = x[2];
    m[3] = z[1] * x[2] - z[2] * x[1]; m[4] = z[2] * x[0] - z[0] * x[2]; m[5] = z[0] * x[1] - z[1] * x[0];
    m[6] = z[0]; m[7] = z[1]; m[8] = z[2];
}
static int m_eq(const double *a, const double *b) { for (int i = 0; i < 9; i++) if (fabs(a[i] - b[i]) > 1e-9) return 0; return 1; }
static int m_zero(const double *a) { for (int i = 0; i < 9; i++) if (a[i] != 0) return 0; return 1; }
/* the part's planes in MastercamPlanes::GetPlanes order (class 2, id order: formats/planes_archive.md "Planes manager"; Plane +0x120
   hidden is not stored, taken 0) */
static int planes_sorted(const McamFile *f, const McamPlaneInfo **out, int max) {
    int n = 0;
    for (int i = 0; i < f->nplanes_list && n < max; i++) if (f->planes_list[i].cls == 2 && f->planes_list[i].is_plane) out[n++] = &f->planes_list[i];
    for (int i = 1; i < n; i++) { const McamPlaneInfo *k = out[i]; int j = i - 1; while (j >= 0 && out[j]->id > k->id) { out[j + 1] = out[j]; j--; } out[j + 1] = k; }
    return n;
}
int la_default_tplane(const McamFile *f, int32_t grp, int insert_index, const LaCombo *c, const double cm[9], const double co[3],
                      int *plane_id, double m[9], double org[3], char *err, size_t errn) {
    static Ac acs[AC_MAX]; int nac = 0; LaCombo q[4];
    /* LatheACTplanes::InitAll 0x1802b41d0: the combinations (spindle, turret) (0,0) (0,1) (1,0) (1,1) = list indices 0..3 (ACToIndex
       0x1802b3660: spindle 0 -> (turret != 0), else 2 + (turret != 0)); a list whose AC matrix equals an earlier one's is that one */
    for (int i = 0; i < 4; i++) if (!combo_lookup(f, grp, i >> 1, i & 1, &q[i], acs, &nac, err, errn)) return 0;
    const Ac *qa[4] = { 0 };
    for (int i = 0; i < 4; i++) for (int k = 0; k < nac; k++) if (acs[k].id == q[i].id) qa[i] = &acs[k];
    double am[4][9]; int map[4];
    for (int i = 0; i < 4; i++) {
        ac_matrix(qa[i], am[i]); map[i] = i;
        for (int j = 0; j < i; j++) if (m_eq(am[i], am[j])) { map[i] = map[j]; break; }
    }
    int ci = c->spindle == 0 ? (c->turret != 0) : 2 + (c->turret != 0);   /* ACToIndex */
    if (c->spindle < 0 || c->turret < 0) { snprintf(err, errn, "axis combination %d has no spindle / turret group", c->id); return 0; }
    int li = map[ci];
    /* InitACTplaneList 0x1802b4030: GetPlanes, ACSupportsToolplane 0x1802b3490 (plane +0x118 off, PlaneType +0x368 != 3, the plane
       minus the op's Cplane (op +0x6d8 / +0x720; Plane::operator-= NOT DECODED: only the Cplane Top at the origin is taken, where it
       leaves the plane as it is) has the AC matrix (Matrix33::operator!= tolerance NOT DECODED: 1e-9, the matrices here are exact)) */
    static const double ID[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    if (!m_eq(cm, ID) || co[0] != 0 || co[1] != 0 || co[2] != 0) { snprintf(err, errn, "the op's Cplane is not Top at the origin: Plane::operator-= (LatheACTplanes::ACSupportsToolplane) is NOT DECODED"); return 0; }
    const McamPlaneInfo *pl[512], *act[512]; int np = planes_sorted(f, pl, 512), na = 0;
    if (!m_zero(am[li])) for (int i = 0; i < np; i++) if (!pl[i]->b118 && pl[i]->ptype != 3 && m_eq(pl[i]->m, am[li])) act[na++] = pl[i];
    /* GetDefaultACTplaneId 0x1802b3ee0 */
    int id = prev_turning_ref(f, grp, insert_index, c);
    if (id == -1) {
        if (!na) { snprintf(err, errn, "axis combination %d has no lathe Tplane in the part: LatheACTplanes::GenerateDefaultLatheTplane 0x1802b36d0 is NOT DECODED", c->id); return 0; }
        id = act[0]->id;                                   /* LatheTplane::GetIDLong 0x1802b4a30 (+0x44): taken as the plane's id */
    }
    /* GetACTplanePtr 0x1802b3e50: that id in the list (an empty list: InitAll appends GenerateDefaultLatheTplane's - NOT DECODED) */
    if (!na) { snprintf(err, errn, "axis combination %d has no lathe Tplane in the part: LatheACTplanes::GenerateDefaultLatheTplane 0x1802b36d0 is NOT DECODED", c->id); return 0; }
    for (int i = 0; i < na; i++) if (act[i]->id == id) {
        *plane_id = id; memcpy(m, act[i]->m, 9 * sizeof *m); memcpy(org, act[i]->org, 3 * sizeof *org);
        return 1;                                          /* LatheTplane::GetOpTplane 0x1802b4ab0 = Plane::Convert into op +0x570 */
    }
    *plane_id = id;
    return 2;
}

/* ---- spindle speed and feed limits of the machine (L-12; formats/lathe.md 9) */
/* CCNCMachine::AxisCombinationFromID(id, '\0') 0x180061250: AxisComboIndexFromID, else (param_2 == 0) AxisCombinationFromIndex(0);
   NULL only for a machine without combinations. 1 = acs[*idx] is it; 0 = none */
static int combo_from_id(const McamFile *f, const uint8_t *mach, int32_t id, Ac *acs, int *idx) {
    int n = combos(f, mach, acs, AC_MAX);
    for (int i = 0; i < n; i++) if (acs[i].id == id) { *idx = i; return 1; }
    if (n < 1) return 0;
    *idx = 0; return 1;
}
static const uint8_t *active_lathe_machine(const McamFile *f, int32_t grp, char *err, size_t errn) {
    const uint8_t *mach = machine_of(f, grp, err, errn);       /* ToolPrmBase::Machine 0x1801b4b10 = OMgetActiveMachine */
    if (!mach) return NULL;
    if (rd32(mach) != 2) { snprintf(err, errn, "the machine is not a lathe (CCNCMachine::Type %d)", rd32(mach)); return NULL; }
    return mach;
}
int la_lathe_rpm_limits(const McamFile *f, int32_t grp, int32_t combo_id, long *mn, long *mx, char *err, size_t errn) {
    static Ac acs[AC_MAX]; int i = 0;
    const uint8_t *mach = active_lathe_machine(f, grp, err, errn);
    if (!mach) return 0;
    /* CCNCMachine::MaxLatheSpindleRPM(operation &, NULL) 0x18006b6c0 / MinLatheSpindleRPM(operation &, NULL) 0x18006bb80: the op's
       combination (AxisCombinationFromID(op +0x11bc, 0)); none -> 0x7fffffff / 0 */
    if (!combo_from_id(f, mach, combo_id, acs, &i)) { *mx = 0x7fffffff; *mn = 0; return 1; }
    const Ac *a = &acs[i];
    /* CAxisCombination::PartComps 0x180017160: CompListIsPart(0) -> CompList(0), else CompListIsPart(1) -> CompList(1), else NULL */
    int k = list_is_part(a, 0) ? 0 : list_is_part(a, 1) ? 1 : -1;
    if (k >= 0) {
        /* MaxLatheSpindleRPM(CObArray *) 0x18006b790: the list in order, a chuck (CMachineComponent::Type 0x6a) gives
           CChuckComponent::MaxSpindleSpeed 0x180092a50 = UParams +4 (UParams 0x180092f20 = Params +0x4a0); the first value other
           than 0x7fffffff is returned, none -> 0x7fffffff. MinLatheSpindleRPM(CObArray *) 0x18006bc40: MinSpindleSpeed 0x180092a80
           = UParams +0, the first value other than -1, none -> 0. The callers keep the list's value unless it is -1 */
        long v = 0x7fffffff, w = -1;
        for (int j = 0; j < a->ncomp[k]; j++) if (ctype(a->comp[k][j]) == 0x6a) { v = rd32(a->comp[k][j] + 0x4a4); if (v != 0x7fffffff) break; }
        for (int j = 0; j < a->ncomp[k]; j++) if (ctype(a->comp[k][j]) == 0x6a) { w = rd32(a->comp[k][j] + 0x4a0); if (w != -1) break; }
        if (w == -1) w = 0;
        if (v == -1) { snprintf(err, errn, "axis combination %d: a chuck's maximum speed -1 sends MaxLatheSpindleRPM to the part group (CComponentGroup::MaxLatheSpindleRPM / FindComponentsByType: NOT DECODED)", a->id); return 0; }
        *mx = v; *mn = w;
        return 1;
    }
    /* no part list: PartGroup 0x1800171b0 of an unmapped combination = GroupByClass(1), the part list's group: NULL as well ->
       MaxLatheSpindleRPM(CComponentGroup *NULL) 0x18006b770 = 0x7fffffff, MinLatheSpindleRPM 0x18006bc30 = 0. A mapped one takes
       its class-100 group: CComponentGroup::MaxLatheSpindleRPM 0x1800a1370 (FindComponentsByType over the group's tree) NOT DECODED */
    if (is_mapped(f, a)) { snprintf(err, errn, "axis combination %d is mapped: its spindle speed limits (CComponentGroup::MaxLatheSpindleRPM / FindComponentsByType) are NOT DECODED", a->id); return 0; }
    *mx = 0x7fffffff; *mn = 0;
    return 1;
}
int la_combo_spindle(const McamFile *f, int32_t grp, int32_t combo_id, int *spindle, char *err, size_t errn) {
    static Ac acs[AC_MAX]; int i = 0;
    const uint8_t *mach = active_lathe_machine(f, grp, err, errn);
    if (!mach) return 0;
    if (!combo_from_id(f, mach, combo_id, acs, &i)) { snprintf(err, errn, "the machine has no axis combination"); return 0; }
    *spindle = ac_spindle(f, &acs[i]);                          /* CAxisCombination::ActiveSpindle 0x180011a90 */
    return 1;
}
int la_machine_feed_limits(const McamFile *f, int32_t grp, int metric, double lim[3][2], char *err, size_t errn) {
    const uint8_t *mach = active_lathe_machine(f, grp, err, errn);
    if (!mach) return 0;
    /* CCNCMachine::OpFeedRates(metric) 0x180077550: this +0x218 (inch) / +600 (metric) = record body +0x140 / +0x180 (body = this
       +0xd8); OpFeedPerMin 0x180077500 = +0 (min, max), OpFeedPerRev 0x180077520 = +0x10 (min, max). ToolPrmBase::GetFeedMin /
       GetFeedMax 0x1801b3630 / 0x1801b3570 (IsMetric): mode 0 per rev, 1 per minute, 2 -> 0 / 1e30 */
    const uint8_t *b = mach + (metric ? 0x180 : 0x140);
    memcpy(&lim[0][0], b + 0x10, 8); memcpy(&lim[0][1], b + 0x18, 8);
    memcpy(&lim[1][0], b, 8); memcpy(&lim[1][1], b + 8, 8);
    lim[2][0] = 0; lim[2][1] = 1e30;
    return 1;
}
