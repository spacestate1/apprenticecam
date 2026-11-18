/* lathe_tool.c - Mastercam 2017 lathe tools: LATHETOOLCLASS::Make_op_tool 0x180152060 (mclathe.dll) for the part's LTL_ID
 * records, and the LATHETOOLCLASS getters it calls. Source: ~/mastercam-re/work/lathe/dump/mclathe_all.c (mclathe.dll),
 * work/lathe/mdef/mdef_all.c (mcmachinedef.dll: RotatePt 0x1800edd70, dRound 0x1800ee160). Spec: ../formats/lathe.md 8.
 * Oracle: tools/tests/lathe_tool_test.sh (the stored lathe ops' op_tool_info).
 *
 * Layout: LATHETOOLCLASS::SetInfo 0x180158390 copies the LATHETOOL record (ent +0x68, 0xa48 bytes) to this+8, so the class
 * field this+X = body +(X - 8); for tool types 0 / 3 it copies body +0xa8 (this+0xb0) into the INSERTCLASS at this+0xad8
 * (INSERTCLASS::InfoPtr 0x1801501a0 = INSERTCLASS +8; this+0xaf0 = that insert), and body +0x318 (this+0x320) into the
 * HOLDERCLASS at this+0xb00 (HOLDERCLASS::InfoPtr 0x180150190 = +8). Units: the record holds inches; the getters' metric
 * argument (IsMetric(): the part's units) multiplies lengths by 25.4.
 *
 * Refused (NOT DECODED): custom tools (type 5: CUSTOMLTOOLCLASS), thread tools (type 1: THDINSERTCLASS), drills (type 4:
 * LDRILLTOOLCLASS), B-axis ops (TurningOpUsesBAxis: the machine definition's axis combination). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "lathe_tool.h"

#define PI 3.141592653589793
#define DEG 0.01745329251994329

static double bd(const uint8_t *b, uint32_t o) { double v; memcpy(&v, b + o, 8); return v; }
static int32_t bi(const uint8_t *b, uint32_t o) { int32_t v; memcpy(&v, b + o, 4); return v; }
static int bs(const uint8_t *b, uint32_t o) { int16_t v; memcpy(&v, b + o, 2); return v; }
static void wd(uint8_t *o, uint32_t at, double v) { memcpy(o + at, &v, 8); }
static void wi(uint8_t *o, uint32_t at, int32_t v) { memcpy(o + at, &v, 4); }
static void ws(uint8_t *o, uint32_t at, int16_t v) { memcpy(o + at, &v, 2); }

/* body offsets = LATHETOOLCLASS this+X - 8 */
#define B_SLOT 0x0          /* Slot 0x18015c9a0 (this+8) */
#define B_NAME 0x4          /* this+0xc (Make_op_tool strcpy0 0x78) */
#define B_INS 0xa8          /* the insert info (this+0xb0) */
#define B_INS_SHAPEI (B_INS + 0x50)   /* INSERTCLASS +0x58: the shape index (INSERTCLASS::Shape 0x18015c8c0) */
#define B_INS_IC (B_INS + 0x58)       /* INSERTCLASS +0x60: ICDiameter 0x14ff80 */
#define B_INS_LEN (B_INS + 0x68)      /* +0x70: Length 0x150d80 */
#define B_INS_CR (B_INS + 0x80)       /* +0x88: CornerRad 0x18014dcc0 / 0x14dce0 */
#define B_INS_MET (B_INS + 0x140)     /* +0x148: INSERTCLASS::Metric 0x152a20 (= this+0x1f0) */
#define B_TYPE (B_INS + 0x148)        /* this+0x1f8: ToolType 0x18015cd60 / INSERTCLASS::Type 0x18015ceb0 (+0x150) */
#define B_HLD 0x318         /* the holder info (this+0x320) */
#define B_HLD_SHAPEI (B_HLD + 0x60)   /* this+0x380 (HOLDERCLASS +0x68) */
#define B_HLD_L (B_HLD + 0x10c)       /* this+0x42c (Orient) */
#define B_HLD_V (B_HLD + 0x110)       /* this+0x430 (HOLDERCLASS::Vertical 0x15d610 +0x118) */
#define B_CUSTOM 0x758      /* this+0x760: custom tool (ToolType -> 5) */
#define B_TURRET 0x759      /* ActiveTurret 0x18014c2d0 */
#define B_SPINDLE 0x75a     /* ActiveSpindle 0x18014c2c0 */
#define B_TANG 0x760        /* this+0x768: the turret angle, degrees (TurretAngle 0x18015cda0) */
#define B_INV 0x768         /* Inverted 0x150bc0 */
#define B_TNUM 0x76c        /* ToolNum 0x18015ccc0 */
#define B_TOFS 0x770        /* ToolOfs(0) 0x18015ccd0 */
#define B_FAST 0x788        /* GetFastFR 0x18014e8a0: value (inch), +8 'R' per rev / else 'M' */
#define B_SLOW 0x7b8        /* GetSlowFR 0x18014ede0 */
#define B_RPM 0x7e8         /* SpdlSpeed 0x18015c9b0 (int) */
#define B_CSS 0x7ec         /* CSS 0x18014c300 */
#define B_CCW 0x800         /* IsCCW 0x150bd0 */
#define B_COOL 0x804        /* Coolant 0x14dcb0 (short) */
#define B_A1 0x810          /* this+0x818, degrees (Get_ltool_rec +0x20) */
#define B_A2 0x818          /* this+0x820 (Get_ltool_rec +0x28) */
#define B_W 0x820           /* Width 0x15d6a0 */
#define B_H 0x828           /* Height 0x14fcb0 */
#define B_COMP 0x840        /* this+0x848: CompStatus 0x14dc90 (0 -> 2), CenterComp 1, TwoPtComp 3, LTipComp 4, SharpCornerComp (v & ~2) == 0 */
#define B_STATION 0x9d8     /* ToolStation 0x18015cd10 */

/* op_tool_info offsets (Make_op_tool) */
#define T_LT 0x1d8          /* the ltool_rec */

typedef struct { double x, y; } P2;
/* ltool_rec (Get_ltool_rec 0x18014f920): +0 i16 orientation, +8 radius, +0x10 width, +0x18 height, +0x20 / +0x28 the two
   insert angles (rad), +0x30 the tool centre, +0x40 / +0x50 the two edge points, +0x60 / +0x61 their flags */
typedef struct { int orient; double r, w, h, a1, a2; P2 c, pa, pb; int fa, fb; } Ltr;

/* dRound (mcmachinedef 0x1800ee160): modf; |fraction| > 0.5 rounds away from zero, else truncates */
static int d_round(double v) { double ip; double fr = modf(v, &ip); if (fabs(fr) > 0.5) { if (v > 0) return (int)(v + 1.0); v -= 1.0; } return (int)v; }
/* restrict_angle (mccore 0x1804b4440): subtract 2 pi while above HI, add 2 pi while below LO */
static void restrict_angle(double lo, double hi, double *a) { while (hi < *a) *a -= 2 * PI; while (*a < lo) *a += 2 * PI; }
/* RotatePt (mcmachinedef 0x1800edd70): P about C by A */
static P2 rotate_pt(P2 p, P2 c, double a) {
    double cs = cos(a), sn = sin(a); P2 r;
    r.x = ((p.x - c.x) * cs + c.x) - (p.y - c.y) * sn; r.y = (p.x - c.x) * sn + c.y + (p.y - c.y) * cs; return r;
}
/* is_turret_top (mccore 0x1800344b0, lathe.md 6): turrets 1, 3, 4 are top */
static int turret_top(int t) { return t == 1 || t == 3 || t == 4; }
static int tool_type(const uint8_t *b) { return b[B_CUSTOM] ? 5 : b[B_TYPE]; }

/* LATHETOOLCLASS::Orient 0x180153db0 (param_3 = 0 in every call made here: ModifyOrient 0x180152a50 ->
   ModifyToolOrientWithUserDefinedQuadrant 0x1801531f0). Tool types 0 / 2 / 3 / 4 (1 refused before, 5 never reaches here). */
static const short tbl_inv[9] = { 1, 3, 4, 1, 2, 7, 8, 5, 6 };     /* DAT_18030fc58 */
static const short tbl_cw[9] = { 1, 2, 1, 4, 3, 6, 5, 8, 7 };      /* DAT_18030fc70 */
static const short tbl_sp[9] = { 1, 2, 1, 4, 3, 6, 5, 8, 7 };      /* DAT_18030fc88 */
static int orient(const uint8_t *b, const LtCtx *cx, int p1, int p2) {
    int ty = tool_type(b), H = b[B_HLD + 1 + bi(b, B_HLD_SHAPEI)], L = bi(b, B_HLD_L) != 0, V = bi(b, B_HLD_V) != 0, o = 1;
    int grooveIF = (H == 'I' || H == 'F');
    if (!turret_top(b[B_TURRET])) {
        if (ty == 2 && grooveIF) o = V ? 6 - L : L + 1;                  /* LAB_180153f90 */
        else if (ty == 0 || ty == 2) o = V ? 4 - L : L + 7;
        else if (ty == 3) { if (!V) o = H == 'X' ? 6 - L : L + 5; else o = H == 'X' ? L + 1 : 2 - L; if (p1 && 4 < o) o -= 4; }
        else if (ty == 4) o = V ? 4 : 8;
        else o = 4;
    } else {
        if (ty == 2 && grooveIF) o = V ? L + 7 : 4 - L;                  /* LAB_180153e49 */
        else if (ty == 0 || ty == 2) o = V ? L + 1 : 6 - L;
        else if (ty == 3) { if (!V) { o = H == 'X' ? L + 7 : 8 - L; if (p1) o -= 4; } else o = H != 'X' ? L + 3 : 4 - L; }
        else if (ty == 4) o = V ? 1 : 5;
        else o = 1;
    }
    /* ModifyToolOrientWithUserDefinedQuadrant (param_2 = P1, param_3 = 0, param_4 = P2) */
    if (b[B_INV]) o = tbl_inv[o];
    if (ty != 5 && !bi(b, B_CCW)) o = tbl_cw[o];
    if (ty != 5 && b[B_SPINDLE]) o = tbl_sp[o];
    /* UpdateOrientFromOrientAngle 0x18015d360: only when |LATHETOOLCLASS +0xbd0| > 90 - 0 here (SetOpAngles, not B axis) */
    if (cx->op_axis_combo != 0 && p2) {                    /* +0xbd8 = 0 (not B axis): no ToolQuadrantToOrientMap */
        if (cx->op_flip && o >= 1 && o <= 8) o = tbl_sp[o];
    }
    return o;
}

/* LATHETOOLCLASS::CalcToolCenter 0x18014d0f0 (types 0 / 2 / 3; +0xbd8 = 0 -> no quadrant re-mapping); inches */
static int calc_tool_center(const uint8_t *b, const LtCtx *cx, P2 *c, char *err, size_t errn) {
    int comp = bi(b, B_COMP), ty = tool_type(b);
    double cr = bd(b, B_INS_CR) * (bi(b, B_INS_MET) ? 25.4 : 1.0);      /* INSERTCLASS::CornerRad() (insert units) */
    double a = 0, rr = 0;
    c->x = c->y = 0;
    if (comp == 1) return 1;                               /* CenterComp: (0, 0) */
    if (ty == 0 || ty == 3) {
        a = cr; rr = cr;
        int ishape = 0; {                                  /* INSERTCLASS::Shape 0x18015c8c0 (insert types 1 'T', 4 'D' do not occur for 0 / 3) */
            int idx = bi(b, B_INS_SHAPEI), len = (int)strnlen((const char *)b + B_INS + 1, 0x40);
            ishape = idx < len ? b[B_INS + 1 + idx] : 'Z';
        }
        if (comp == 3 && (ishape == 'P' || ishape == 'H' || ishape == 'O')) {
            snprintf(err, errn, "lathe tool: two-point compensation of a P / H / O insert (CalcToolCenter 0x18014d0f0) NOT PORTED"); return 0;
        }
        if (comp == 4) a = 0;                              /* LTipComp */
    } else if (ty == 2) {
        if (comp != 4) a = cr;
        else { snprintf(err, errn, "lathe tool: groove tool with lead-tip compensation (CalcToolCenter 0x18014d0f0) NOT PORTED"); return 0; }
        rr = cr;
    } else { snprintf(err, errn, "lathe tool: tool type %d (CalcToolCenter 0x18014d0f0) NOT DECODED", ty); return 0; }
    switch (orient(b, cx, 1, 1)) {
    case 1: c->x = a; c->y = rr; break;
    case 2: c->x = -a; c->y = rr; break;
    case 3: c->x = -a; c->y = -rr; break;
    case 4: c->x = a; c->y = -rr; break;
    case 5: c->x = rr; c->y = a; break;
    case 6: c->x = -rr; c->y = a; break;
    case 7: c->x = -rr; c->y = -a; break;
    default: c->x = rr; c->y = -a; break;
    }
    /* SetToolCenter 0x18015abf0 stores c / 25.4 for a metric insert and GetToolCenter multiplies back: inches here */
    if (bi(b, B_INS_MET)) { c->x /= 25.4; c->y /= 25.4; }
    return 1;
}

/* FUN_1801c20e0 / FUN_1801c2290: the two edge points from the width / height and the insert angles */
static const double eb[9] = { 0, 0, PI, PI, 0, 1.570796326794896, 1.570796326794896, 4.71238898038469, 4.71238898038469 };
static const double es[9] = { 0, 1, -1, 1, -1, -1, 1, -1, 1 };
static int clamp18(int o) { return o < 1 ? 1 : 8 < o ? 8 : o; }
static void edge_a(Ltr *t, double tol) {               /* FUN_1801c20e0 */
    int s = clamp18(t->orient);
    double ang = es[s] * t->a1 + eb[s], cs = cos(ang), sn = sin(ang), w = t->w - t->r * 2.0;
    double den = fabs(t->orient < 5 ? cs : sn);            /* the divisor's sign bit cleared (& 0x7fffffff) */
    if (den < 1e-6) den = 1.0;
    if (w <= tol) { t->fa = 0; return; }
    t->fa = 1; t->pa.y = w * -(sn / den); t->pa.x = w * -(cs / den);
}
static void edge_b(Ltr *t, double tol) {               /* FUN_1801c2290 */
    if (t->h == 0.0 || t->a2 < t->a1) { t->fb = 0; return; }
    int s = clamp18(t->orient);
    double ang = t->a2 * es[s] + eb[s], cs = cos(ang), sn = sin(ang), h = t->h - t->r * 2.0;
    double den = fabs(t->orient < 5 ? sn : cs);            /* the divisor's sign bit cleared */
    if (den < 1e-6) den = 1.0;
    if (h <= tol) { t->fb = 0; return; }
    t->fb = 1; t->pb.y = h * -(sn / den); t->pb.x = h * -(cs / den);
}

/* Get_ltool_rec 0x18014f920 (+0xbd8 = 0: QuadrantFromOrientation not called) */
static int get_ltool_rec(const uint8_t *b, const LtCtx *cx, Ltr *t, char *err, size_t errn) {
    double m = cx->metric ? 25.4 : 1.0; int ty = tool_type(b);
    memset(t, 0, sizeof *t);
    t->orient = orient(b, cx, ty != 3, 1);
    t->r = bd(b, B_INS_CR) * m;                            /* INSERTCLASS::CornerRad(IsMetric()) (types 0 / 2 / 3) */
    t->w = bd(b, B_W) * m; t->h = bd(b, B_H) * m;
    t->a1 = bd(b, B_A1) * DEG; t->a2 = bd(b, B_A2) * DEG;
    P2 c; if (!calc_tool_center(b, cx, &c, err, errn)) return 0;
    t->c.x = c.x * m; t->c.y = c.y * m;                     /* GetToolCenter(IsMetric()) 0x18014ef40 */
    edge_a(t, cx->tol); edge_b(t, cx->tol);
    return 1;
}

/* TurretAngle(1, 1) 0x18015cda0: (+0x768 + +0xbc8 (0) + ToolQuadrantToOrientMap::OffsetAngle(+0xbd8 = 0) = 0; FUN_18028ad30's
   key 0 = {orient, orient, 0.0}) in radians */
static double turret_angle(const uint8_t *b) { return bd(b, B_TANG) * DEG; }

/* Rotate_ltool_rec 0x180154c00 */
static void rotate_ltool_rec(const uint8_t *b, Ltr *t) {
    double ta = turret_angle(b);
    P2 piv = { 0, 0 }, qa = { 0, 0 }, qb = { 0, 0 };
    int comp = bi(b, B_COMP);
    if ((comp & ~2) == 0) piv = t->c;                      /* SharpCornerComp 0x15c940 */
    int bit = t->orient >= 0 && t->orient < 8 && ((0xb4u >> t->orient) & 1);   /* orientations 2, 4, 5, 7 */
    if (t->fa) {
        P2 d = { t->c.x - t->pa.x, t->c.y - t->pa.y }; qa = rotate_pt(d, piv, ta);
        t->a1 = bit ? t->a1 - ta : ta + t->a1; restrict_angle(-PI, PI, &t->a1);
    }
    if (t->fb) {
        P2 d = { t->c.x - t->pb.x, t->c.y - t->pb.y }; qb = rotate_pt(d, piv, ta);
        t->a2 = bit ? t->a2 - ta : ta + t->a2; restrict_angle(-PI, PI, &t->a2);
    }
    t->c = rotate_pt(t->c, piv, ta);
    if (t->fa) { t->pa.x = t->c.x - qa.x; t->pa.y = t->c.y - qa.y; t->w = fabs(t->orient < 5 ? t->pa.x : t->pa.y) + t->r * 2.0; }
    if (t->fb) { t->pb.x = t->c.x - qb.x; t->pb.y = t->c.y - qb.y; t->h = fabs(t->orient < 5 ? t->pb.y : t->pb.x) + t->r * 2.0; }
}

/* GetXlateVector 0x18017fe80: CompStatus 3 (two-point) -> minus the corner of the rotated tool's box (the centre and the two
   edge points, each grown by the radius) picked by Orient(1, 1, 0); else (0, 0) */
static int xlate_vector(const uint8_t *b, const LtCtx *cx, P2 *v, char *err, size_t errn) {
    v->x = v->y = 0;
    int comp = bi(b, B_COMP); if (comp == 0) comp = 2;
    if (comp != 3) return 1;
    Ltr t; if (!get_ltool_rec(b, cx, &t, err, errn)) return 0;
    rotate_ltool_rec(b, &t);
    P2 p[3] = { t.c, t.fa ? (P2){ t.c.x - t.pa.x, t.c.y - t.pa.y } : t.c, t.fb ? (P2){ t.c.x - t.pb.x, t.c.y - t.pb.y } : t.c };
    double mnx = 1e30, mny = 1e30, mxx = -1e30, mxy = -1e30;
    for (int k = 0; k < 3; k++) {
        if (p[k].x - t.r < mnx) mnx = p[k].x - t.r;
        if (mxx < p[k].x + t.r) mxx = p[k].x + t.r;
        if (p[k].y - t.r < mny) mny = p[k].y - t.r;
        if (mxy < p[k].y + t.r) mxy = p[k].y + t.r;
    }
    switch (orient(b, cx, 1, 1)) {
    case 2: case 6: v->x = -mxx; v->y = -mny; break;
    case 3: case 7: v->x = -mxx; v->y = -mxy; break;
    case 4: case 8: v->x = -mnx; v->y = -mxy; break;
    default: v->x = -mnx; v->y = -mny; break;
    }
    return 1;
}

int lt_make_op_tool(const uint8_t *b, uint32_t n, const LtCtx *cx, uint8_t *o, char *err, size_t errn) {
    if (!b || n < LT_BODY_SIZE) { snprintf(err, errn, "lathe tool: record shorter than 0x%x bytes", LT_BODY_SIZE); return 0; }
    if (cx->b_axis) { snprintf(err, errn, "lathe tool: a B-axis turning op (TurningOpUsesBAxis 0x1801780f0; SetOpAngles' B-axis branch) NOT DECODED"); return 0; }
    int ty = tool_type(b);
    if (ty == 5) { snprintf(err, errn, "lathe tool: custom tool (CUSTOMLTOOLCLASS) NOT DECODED"); return 0; }
    if (ty == 1) { snprintf(err, errn, "lathe tool: thread tool (THDINSERTCLASS) NOT DECODED"); return 0; }
    if (ty == 4) { snprintf(err, errn, "lathe tool: drill / tap / reamer (LDRILLTOOLCLASS) NOT DECODED"); return 0; }
    if (ty != 0 && ty != 2 && ty != 3) { snprintf(err, errn, "lathe tool: tool type %d unknown", ty); return 0; }
    int met = cx->metric != 0; double m = met ? 25.4 : 1.0;
    memset(o, 0, LT_OPTOOL_SIZE); wi(o, 0x268, (int32_t)cx->keep_268);       /* memset 0x288, +0x268 kept */
    ws(o, 4, (int16_t)ty);                                 /* ToolType(0, 1): not custom -> body +0x1f0 */
    wi(o, 0x264, bi(b, B_SLOT)); wi(o, 0x25c, bi(b, B_STATION)); wi(o, 0, bi(b, B_TNUM)); wi(o, 0x2c, bi(b, B_TOFS));
    ws(o, 0x50, (int16_t)bs(b, B_COOL));
    o[0x258] = (uint8_t)met;
    wi(o, 0x270, 0x100);
    if (cx->machine_group) wi(o, 0x274, cx->machine_group);
    o[0x260] = b[B_TURRET]; o[0x261] = b[B_SPINDLE];
    int rpm = (met && bi(b, B_CSS)) ? d_round(bi(b, B_RPM) * 0.3048) : d_round(bi(b, B_RPM));   /* SpdlSpeed(IsMetric()) */
    wi(o, 0x48, bi(b, B_CCW) ? rpm : -rpm);
    o[0x4d] = (uint8_t)bi(b, B_CSS);
    double ff = bd(b, B_FAST) * m, sf = bd(b, B_SLOW) * m;   /* GetFastFR / GetSlowFR: 'R' -> negated */
    wd(o, 0x30, b[B_FAST + 8] == 'R' ? -ff : ff); wd(o, 0x38, b[B_SLOW + 8] == 'R' ? -sf : sf);
    memcpy(o + 0x56, b + B_NAME, 0x77); o[0x56 + 0x77] = 0;  /* strcpy0(.., 0x78) */
    Ltr t; if (!get_ltool_rec(b, cx, &t, err, errn)) return 0;
    rotate_ltool_rec(b, &t);
    P2 xv; if (!xlate_vector(b, cx, &xv, err, errn)) return 0;   /* Xlate_t_center 0x18015dce0 */
    t.c.x += xv.x; t.c.y += xv.y;
    uint8_t *L = o + T_LT;
    ws(L, 0, (int16_t)t.orient); wd(L, 8, t.r); wd(L, 0x10, t.w); wd(L, 0x18, t.h); wd(L, 0x20, t.a1); wd(L, 0x28, t.a2);
    wd(L, 0x30, t.c.x); wd(L, 0x38, t.c.y);
    if (t.fa) { wd(L, 0x40, t.pa.x); wd(L, 0x48, t.pa.y); }
    if (t.fb) { wd(L, 0x50, t.pb.x); wd(L, 0x58, t.pb.y); }
    L[0x60] = (uint8_t)t.fa; L[0x61] = (uint8_t)t.fb;
    wd(o, 0x10, t.r);                                      /* ToolType(1, 0) != 4: +0x10 = the ltool_rec's radius (+0x1e0) */
    return 1;
}

int lt_info(const uint8_t *b, uint32_t n, LtInfo *o) {
    memset(o, 0, sizeof *o);
    if (!b || n < LT_BODY_SIZE) return 0;
    o->slot = bi(b, B_SLOT); o->tool_num = bi(b, B_TNUM); o->station = bi(b, B_STATION); o->offset = bi(b, B_TOFS);
    o->type = tool_type(b); o->turret = b[B_TURRET]; o->spindle = b[B_SPINDLE]; o->nose_r = o->type == 4 ? 0.0 : bd(b, B_INS_CR);
    o->group = bi(b, 0x9dc);
    memcpy(o->name, b + B_NAME, 0x78); o->name[0x78] = 0;
    return 1;
}

/* ToolPrmBase::ToolOrientChanged 0x1801b5960 (FacePgPrm::UpdateCtrlData 0x1801b5d10 after SetLToolAngles): PREV = PrvTool (the op's
   op_tool_info before the pick: ToolPrmBase::SetPrvTool in CLtlPgCmn::GetToolProperty); FIRST_CALL = this[0x30] still 0.
   prev slot (+0x264) 0 -> 1; else NEW = Make_op_tool; not the first call: (slot differs or (|prev +0x240 - LATHETOOLCLASS +0xbc8|
   < tol and |prev +0x248 - +0xbd0| < tol and prev +0x250 (0 when prev +0x251 is 0) == (+0xbd9 ? +0xbd8 : 0))) -> the ltool
   orientations (+0x1d8) differ; otherwise 1. The first call: 1. (+0xbc8 / +0xbd0 / +0xbd8 / +0xbd9 = 0: not B axis.) */
int lt_orient_changed(const uint8_t *prev, const uint8_t *b, uint32_t n, const LtCtx *cx, int first_call, char *err, size_t errn) {
    int32_t ps; memcpy(&ps, prev + 0x264, 4);
    if (ps == 0) return 1;
    uint8_t nw[LT_OPTOOL_SIZE];
    if (!lt_make_op_tool(b, n, cx, nw, err, errn)) return -1;
    if (first_call) return 1;
    int32_t ns; memcpy(&ns, nw + 0x264, 4);
    double a0 = bd(prev, 0x240), a1 = bd(prev, 0x248); int q = prev[0x251] ? prev[0x250] : 0;
    if (ps != ns || (fabs(a0 - 0.0) < cx->tol && fabs(a1 - 0.0) < cx->tol && q == 0)) return bs(prev, 0x1d8) != bs(nw, 0x1d8);
    return 1;
}

/* LATHETOOLCLASS::LHTool 0x180150bf0: Orient(0, 1, 0) is 2, 3, 6 or 7 */
static int lh_tool(const uint8_t *b, const LtCtx *cx) { int o = orient(b, cx, 0, 1); return o == 2 || o == 3 || o == 6 || o == 7; }

/* FacePgPrm::SetDefaultCC 0x1801b5460 (the Face page's comp direction combo, op +0x56a): v = (op +0x1210 != 0) for a right-hand
   tool on the top turret or a left-hand tool on the bottom one, else (op +0x1210 == 0); only when the combo index (*CUR_INDEX) < 2:
   *CUR_INDEX = v, op +0x56a = v. Returns 1 when written. (Its SetOpAngles(cur_op) is undone at the end: no state change.) */
int lt_face_default_cc(const uint8_t *b, uint32_t n, const LtCtx *cx, int away_from_center, int *cur_index, int *op56a) {
    if (!b || n < LT_BODY_SIZE || tool_type(b) == 5) return 0;     /* Orient of a custom tool: CUSTOMLTOOLCLASS NOT DECODED */
    int lh = lh_tool(b, cx), top = turret_top(b[B_TURRET]);
    int v = (lh ? !top : top) ? (away_from_center != 0) : (away_from_center == 0);
    if (*cur_index >= 2) return 0;
    *cur_index = v; *op56a = v;
    return 1;
}

/* IntelliSetEEVector 0x1801b4860 (ToolPrmBase::SetEEVectors 0x1801b56e0: WHICH 0 = op +0x1218, 1 = op +0x1288; skipped when the op
   is canned (OpIsCanned) or OKToUpdate 0x1801b5140 fails: op +0x531 != 0). |V| < tol -> unchanged. Opcodes 0x3e / 0xd0 / 0x44 / 0xd4
   (groove) remap the orientation by op +0x1431: NOT PORTED (refused, 0). Others (Face 0x42): scale = WHICH ? -1 : 1; the signs
   by Orient(1, 1, 0): 1 / 5 (-|x|, -|y|), 2 / 6 (|x|, -|y|), 3 / 7 (|x|, |y|), else (-|x|, |y|); times scale; |x| or |y| < tol -> 0. */
int lt_intelli_ee_vector(const uint8_t *b, uint32_t n, const LtCtx *cx, int opcode, int which, double v[2]) {
    if (!b || n < LT_BODY_SIZE || tool_type(b) == 5) return 0;
    if (sqrt(v[0] * v[0] + v[1] * v[1]) < cx->tol) return 1;
    if (opcode == 0x3e || opcode == 0xd0 || opcode == 0x44 || opcode == 0xd4) return 0;
    double s = which ? -1.0 : 1.0, x = fabs(v[0]), y = fabs(v[1]);
    switch (orient(b, cx, 1, 1)) {
    case 1: case 5: x = -x; y = -y; break;
    case 2: case 6: y = -y; break;
    case 3: case 7: break;
    default: x = -x; break;
    }
    x *= s; y *= s;
    if (fabs(x) < cx->tol) x = 0.0;
    if (fabs(y) < cx->tol) y = 0.0;
    v[0] = x; v[1] = y;
    return 1;
}
